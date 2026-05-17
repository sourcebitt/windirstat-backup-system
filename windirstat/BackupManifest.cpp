// WinDirStat - Directory Statistics
//
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "pch.h"
#include "BackupManifest.h"

// =============================================================================
//  Minimal recursive-descent JSON parser
//
//  Only the structures that appear in the backup manifest are handled:
//    object, array, string, integer, and floating-point number.
//  Unknown keys are skipped safely so future format extensions are forward-
//  compatible with this reader.
// =============================================================================

namespace {

// ---------------------------------------------------------------------------
// Scanner — character-level cursor over an in-memory JSON byte string
// ---------------------------------------------------------------------------

struct Scanner
{
    const char* p;
    const char* end;

    explicit Scanner(std::string_view sv) noexcept
        : p(sv.data()), end(sv.data() + sv.size()) {}

    bool Ok() const noexcept { return p < end; }

    // Skip ASCII whitespace in-place.
    void Ws() noexcept
    {
        while (p < end && static_cast<unsigned char>(*p) <= 32u) ++p;
    }

    // Consume one character after skipping whitespace; return false on mismatch.
    bool Eat(char c) noexcept
    {
        Ws();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }

    // Like Eat() but emits a TRACE on failure to ease debugging.
    bool Expect(char c) noexcept
    {
        if (Eat(c)) return true;
        TRACE(L"BackupManifest: JSON expected '%C' near byte offset %zu\n",
              c, static_cast<std::size_t>(p - (end - static_cast<std::ptrdiff_t>(end - p))));
        return false;
    }

    // Parse a JSON string value.  Cursor must be positioned on the opening '"'.
    // Handles all standard escape sequences including \uXXXX (encoded as UTF-8).
    std::string ParseString()
    {
        if (!Expect('"')) return {};
        std::string s;
        s.reserve(128);

        while (p < end && *p != '"')
        {
            if (*p != '\\') { s += *p++; continue; }

            ++p; // consume back-slash
            if (p >= end) break;

            switch (*p++)
            {
                case '"':  s += '"';  break;
                case '\\': s += '\\'; break;
                case '/':  s += '/';  break;
                case 'b':  s += '\b'; break;
                case 'f':  s += '\f'; break;
                case 'n':  s += '\n'; break;
                case 'r':  s += '\r'; break;
                case 't':  s += '\t'; break;
                case 'u':
                {
                    if (p + 4 > end) break;
                    char hex[5] = { p[0], p[1], p[2], p[3], '\0' };
                    const unsigned long cp = std::strtoul(hex, nullptr, 16);
                    p += 4;
                    // Encode the Unicode code-point as UTF-8
                    if (cp < 0x80u)
                    {
                        s += static_cast<char>(cp);
                    }
                    else if (cp < 0x800u)
                    {
                        s += static_cast<char>(0xC0u | (cp >> 6));
                        s += static_cast<char>(0x80u | (cp & 0x3Fu));
                    }
                    else
                    {
                        s += static_cast<char>(0xE0u | (cp >> 12));
                        s += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                        s += static_cast<char>(0x80u | (cp & 0x3Fu));
                    }
                    break;
                }
                default: s += p[-1]; break;
            }
        }

        if (p < end) ++p; // consume closing '"'
        return s;
    }

    // Parse a JSON number token and return it as a raw string.
    // Handles optional leading minus, digits, decimal point, and exponent.
    std::string ParseNumber()
    {
        Ws();
        const char* start = p;
        if (p < end && *p == '-') ++p;
        while (p < end && (std::isdigit(static_cast<unsigned char>(*p))
                           || *p == '.' || *p == 'e' || *p == 'E'
                           || *p == '+' || *p == '-'))
            ++p;
        return { start, static_cast<std::size_t>(p - start) };
    }

    // Skip any JSON value without interpreting it (forward-compatibility).
    void SkipValue()
    {
        Ws();
        if (p >= end) return;
        if (*p == '"') { ParseString(); return; }
        if (*p == '{') { SkipObject();  return; }
        if (*p == '[') { SkipArray();   return; }
        // Bare token: number, true, false, null
        while (p < end && *p != ',' && *p != '}' && *p != ']') ++p;
    }

    void SkipObject()
    {
        if (!Expect('{')) return;
        if (Eat('}')) return;
        do { ParseString(); Expect(':'); SkipValue(); } while (Eat(','));
        Expect('}');
    }

    void SkipArray()
    {
        if (!Expect('[')) return;
        if (Eat(']')) return;
        do { SkipValue(); } while (Eat(','));
        Expect(']');
    }

    // Iterate every member of a JSON object.
    // fn receives the key as std::string; it must consume the corresponding value.
    template<typename Fn>
    bool ForEachMember(Fn fn)
    {
        if (!Expect('{')) return false;
        if (Eat('}')) return true;
        do
        {
            std::string key = ParseString();
            if (!Expect(':')) return false;
            fn(std::move(key));
        }
        while (Eat(','));
        return Expect('}');
    }

    // Iterate every element of a JSON array.
    // fn must consume each element value.
    template<typename Fn>
    bool ForEachElement(Fn fn)
    {
        if (!Expect('[')) return false;
        if (Eat(']')) return true;
        do { fn(); } while (Eat(','));
        return Expect(']');
    }
};

// ---------------------------------------------------------------------------
// JSON string writer — appends a properly escaped JSON string literal to out.
// ---------------------------------------------------------------------------

static void AppendJsonString(std::string& out, std::string_view s)
{
    out += '"';
    for (const unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20u) out += std::format("\\u{:04x}", static_cast<unsigned>(c));
                else            out += static_cast<char>(c);
                break;
        }
    }
    out += '"';
}

} // anonymous namespace

// =============================================================================
//  CBackupManifest implementation
// =============================================================================

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool CBackupManifest::Load(const std::wstring& manifestPath)
{
    std::unique_lock lock(m_mutex);
    m_files.clear();

    // A missing manifest is normal on first run — not an error.
    std::error_code ec;
    if (!std::filesystem::exists(manifestPath, ec)) return true;

    std::ifstream f(manifestPath, std::ios::binary);
    if (!f.is_open())
    {
        TRACE(L"BackupManifest: cannot open '%s' for reading\n", manifestPath.c_str());
        return false;
    }

    std::string json(
        std::istreambuf_iterator<char>(f),
        std::istreambuf_iterator<char>{});

    // Strip UTF-8 BOM if present (Python json.dump never writes one, but be safe)
    if (json.size() >= 3 &&
        static_cast<unsigned char>(json[0]) == 0xEFu &&
        static_cast<unsigned char>(json[1]) == 0xBBu &&
        static_cast<unsigned char>(json[2]) == 0xBFu)
    {
        json.erase(0, 3);
    }

    return ParseJson(json);
}

bool CBackupManifest::Save(const std::wstring& manifestPath) const
{
    std::shared_lock lock(m_mutex);

    const std::string json = SerializeJson();
    const std::wstring tmpPath = manifestPath + L".tmp";

    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
        {
            TRACE(L"BackupManifest: cannot open '%s' for writing\n", tmpPath.c_str());
            return false;
        }
        f.write(json.data(), static_cast<std::streamsize>(json.size()));
        if (!f.good()) return false;
    }

    // Atomic replace — MoveFileExW on the same volume is rename() under the hood
    if (!MoveFileExW(tmpPath.c_str(), manifestPath.c_str(), MOVEFILE_REPLACE_EXISTING))
    {
        TRACE(L"BackupManifest: MoveFileExW failed (error %lu)\n", GetLastError());
        DeleteFileW(tmpPath.c_str());
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool CBackupManifest::Contains(const std::wstring& normPath) const
{
    std::shared_lock lock(m_mutex);
    return m_files.contains(normPath);
}

const BackupFileEntry* CBackupManifest::Find(const std::wstring& normPath) const
{
    std::shared_lock lock(m_mutex);
    const auto it = m_files.find(normPath);
    return it != m_files.end() ? &it->second : nullptr;
}

BackupFileEntry* CBackupManifest::Find(const std::wstring& normPath)
{
    std::unique_lock lock(m_mutex);
    const auto it = m_files.find(normPath);
    return it != m_files.end() ? &it->second : nullptr;
}

std::size_t CBackupManifest::GetFileCount() const
{
    std::shared_lock lock(m_mutex);
    return m_files.size();
}

std::unordered_map<std::wstring, std::vector<std::wstring>>
    CBackupManifest::BuildHashIndex() const
{
    std::shared_lock lock(m_mutex);
    std::unordered_map<std::wstring, std::vector<std::wstring>> index;
    index.reserve(m_files.size());
    for (const auto& [path, entry] : m_files)
        index[entry.currentHash].push_back(path);
    return index;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void CBackupManifest::AddOrUpdate(const std::wstring& normPath, const std::wstring& hash,
                                   const std::wstring& status, ULONGLONG size, double mtime)
{
    BackupFileVersion ver;
    ver.hash       = hash;
    ver.backedUpAt = CurrentIsoTimestamp();
    ver.size       = size;
    ver.mtime      = mtime;
    ver.status     = status;

    std::unique_lock lock(m_mutex);

    auto it = m_files.find(normPath);
    if (it == m_files.end())
    {
        m_files.emplace(normPath, BackupFileEntry{ hash, { std::move(ver) } });
    }
    else
    {
        it->second.currentHash = hash;
        it->second.versions.push_back(std::move(ver));
    }
}

bool CBackupManifest::Purge(const std::wstring& normPath, std::wstring& outOrphanedHash)
{
    std::unique_lock lock(m_mutex);

    const auto it = m_files.find(normPath);
    if (it == m_files.end()) return false;

    const std::wstring hash = it->second.currentHash;
    m_files.erase(it);

    // Determine whether the object is now unreferenced
    const bool hasOtherRef = std::ranges::any_of(m_files, [&hash](const auto& kv)
    {
        return kv.second.currentHash == hash;
    });

    outOrphanedHash = hasOtherRef ? std::wstring{} : hash;
    return true;
}

// ---------------------------------------------------------------------------
// Path normalization
// ---------------------------------------------------------------------------

std::wstring CBackupManifest::NormalizePath(const std::wstring& path)
{
    // weakly_canonical resolves . and .. components without requiring the path
    // to exist on disk.  We then normalize the separator to '/'.
    std::error_code ec;
    std::wstring result = std::filesystem::weakly_canonical(path, ec).wstring();
    if (ec) result = path;
    std::ranges::replace(result, L'\\', L'/');
    return result;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

std::wstring CBackupManifest::CurrentIsoTimestamp()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    return std::format(L"{:04}-{:02}-{:02}T{:02}:{:02}:{:02}",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
}

// ---------------------------------------------------------------------------
// String conversion helpers
// ---------------------------------------------------------------------------

std::string CBackupManifest::WideToUtf8(std::wstring_view s)
{
    if (s.empty()) return {};
    const int sz = WideCharToMultiByte(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return {};
    std::string result(static_cast<std::size_t>(sz), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        result.data(), sz, nullptr, nullptr);
    return result;
}

std::wstring CBackupManifest::Utf8ToWide(std::string_view s)
{
    if (s.empty()) return {};
    const int sz = MultiByteToWideChar(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        nullptr, 0);
    if (sz <= 0) return {};
    std::wstring result(static_cast<std::size_t>(sz), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        s.data(), static_cast<int>(s.size()),
        result.data(), sz);
    return result;
}

// ---------------------------------------------------------------------------
// JSON parser
// ---------------------------------------------------------------------------

bool CBackupManifest::ParseJson(std::string_view json)
{
    Scanner sc(json);
    bool ok = true;

    ok = sc.ForEachMember([&](std::string topKey)
    {
        if (topKey == "version")
        {
            sc.ParseNumber(); // consume; we accept any version
        }
        else if (topKey == "files")
        {
            sc.ForEachMember([&](std::string rawPath)
            {
                BackupFileEntry entry;

                sc.ForEachMember([&](std::string fileKey)
                {
                    if (fileKey == "current_hash")
                    {
                        entry.currentHash = Utf8ToWide(sc.ParseString());
                    }
                    else if (fileKey == "versions")
                    {
                        sc.ForEachElement([&]()
                        {
                            BackupFileVersion ver;

                            sc.ForEachMember([&](std::string vKey)
                            {
                                if      (vKey == "hash")         ver.hash       = Utf8ToWide(sc.ParseString());
                                else if (vKey == "backed_up_at") ver.backedUpAt = Utf8ToWide(sc.ParseString());
                                else if (vKey == "status")       ver.status     = Utf8ToWide(sc.ParseString());
                                else if (vKey == "size")
                                {
                                    const std::string n = sc.ParseNumber();
                                    if (!n.empty()) ver.size = std::stoull(n);
                                }
                                else if (vKey == "mtime")
                                {
                                    const std::string n = sc.ParseNumber();
                                    if (!n.empty()) ver.mtime = std::stod(n);
                                }
                                else
                                {
                                    sc.SkipValue();
                                }
                            });

                            entry.versions.push_back(std::move(ver));
                        });
                    }
                    else
                    {
                        sc.SkipValue();
                    }
                });

                if (!entry.currentHash.empty())
                    m_files.emplace(Utf8ToWide(rawPath), std::move(entry));
            });
        }
        else
        {
            sc.SkipValue();
        }
    });

    TRACE(L"BackupManifest: loaded %zu entries\n", m_files.size());
    return ok;
}

// ---------------------------------------------------------------------------
// JSON serializer
// ---------------------------------------------------------------------------

std::string CBackupManifest::SerializeJson() const
{
    std::string out;
    out.reserve(m_files.size() * 512u);

    out += "{\n  \"version\": 2,\n  \"files\": {";

    bool firstFile = true;
    for (const auto& [path, entry] : m_files)
    {
        if (!firstFile) out += ',';
        firstFile = false;

        out += "\n    ";
        AppendJsonString(out, WideToUtf8(path));
        out += ": {\n      \"current_hash\": ";
        AppendJsonString(out, WideToUtf8(entry.currentHash));
        out += ",\n      \"versions\": [";

        bool firstVer = true;
        for (const auto& v : entry.versions)
        {
            if (!firstVer) out += ',';
            firstVer = false;

            out += "\n        {\n";
            out += "          \"hash\": ";          AppendJsonString(out, WideToUtf8(v.hash));       out += ",\n";
            out += "          \"backed_up_at\": ";  AppendJsonString(out, WideToUtf8(v.backedUpAt)); out += ",\n";
            out += std::format("          \"size\": {},\n", v.size);
            out += std::format("          \"mtime\": {:.6f},\n", v.mtime);
            out += "          \"status\": ";        AppendJsonString(out, WideToUtf8(v.status));     out += "\n";
            out += "        }";
        }

        out += "\n      ]\n    }";
    }

    out += "\n  }\n}\n";
    return out;
}
