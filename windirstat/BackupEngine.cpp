// WinDirStat - Directory Statistics
// Copyright © WinDirStat Team
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
#include "BackupEngine.h"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CBackupEngine::CBackupEngine(std::wstring backupRoot, std::vector<std::wstring> sourceFolders)
    : m_store(std::move(backupRoot))
    , m_sourceFolders(std::move(sourceFolders))
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool CBackupEngine::Initialize()
{
    if (!m_store.Initialize())
        return false;

    return m_manifest.Load(ManifestPath());
}

bool CBackupEngine::Save() const
{
    return m_manifest.Save(ManifestPath());
}

// ---------------------------------------------------------------------------
// Backup pass
// ---------------------------------------------------------------------------

BackupPassResult CBackupEngine::RunPass()
{
    BackupPassResult result;

    // Build the hash index once up-front for O(1) relocation detection.
    // (A "relocated" file is one whose content was already stored under a
    // different path — the object exists but the manifest path is new.)
    const auto hashIndex = m_manifest.BuildHashIndex();

    // Track every normalized path we visit so we can count missing entries.
    std::unordered_set<std::wstring> visited;

    const auto processFolder = [&](const std::wstring& folder)
    {
        std::error_code ec;
        const std::filesystem::directory_options opts =
            std::filesystem::directory_options::skip_permission_denied;

        for (const auto& dirEntry :
             std::filesystem::recursive_directory_iterator(folder, opts, ec))
        {
            std::error_code entryEc;
            if (!dirEntry.is_regular_file(entryEc)) continue;

            // ── Metadata ──────────────────────────────────────────────────
            const std::wstring rawPath  = dirEntry.path().wstring();
            const std::wstring normPath = CBackupManifest::NormalizePath(rawPath);
            visited.insert(normPath);

            const ULONGLONG fileSize =
                static_cast<ULONGLONG>(dirEntry.file_size(entryEc));
            if (entryEc) { ++result.errors; continue; }

            const double mtime = FileTimeToUnix(dirEntry.last_write_time(entryEc));
            if (entryEc) { ++result.errors; continue; }

            // ── Fast skip: mtime + size unchanged ─────────────────────────
            const BackupFileEntry* existing = m_manifest.Find(normPath);
            if (existing && !existing->versions.empty())
            {
                const BackupFileVersion& latest = existing->versions.back();
                // Use a 2-second tolerance to handle FAT32 mtime granularity.
                if (latest.size == fileSize && std::abs(latest.mtime - mtime) < 2.0)
                {
                    ++result.filesUnchanged;
                    continue;
                }
            }

            // ── Hash the file ─────────────────────────────────────────────
            const std::wstring hash = CBackupStore::ComputeFileSha256(rawPath);
            if (hash.empty())
            {
                TRACE(L"BackupEngine: hash failed for '%s'\n", rawPath.c_str());
                ++result.errors;
                continue;
            }

            // ── Content-unchanged check (mtime drifted, bytes identical) ──
            if (existing && existing->currentHash == hash)
            {
                ++result.filesUnchanged;
                continue;
            }

            // ── Determine status string for manifest record ────────────────
            std::wstring status;
            if (!existing)
            {
                // Path is new to the manifest.
                // "relocated" if the content was already stored from another path;
                // "new" if this is genuinely fresh content.
                status = m_store.HasObject(hash) ? L"relocated" : L"new";
            }
            else
            {
                // Path exists in manifest but content changed.
                status = L"modified";
            }

            // ── Store the object (no-op if already present) ────────────────
            if (m_store.StoreObject(rawPath, hash))
                ++result.objectsStored;

            // ── Update manifest ───────────────────────────────────────────
            m_manifest.AddOrUpdate(normPath, hash, status, fileSize, mtime);

            if (status == L"new")
                ++result.filesNew;
            else if (status == L"relocated")
                ++result.filesRelocated;
            else
                ++result.filesModified;
        }

        if (ec)
        {
            TRACE(L"BackupEngine: directory iteration error in '%s': %S\n",
                  folder.c_str(), ec.message().c_str());
        }
    };

    for (const auto& folder : m_sourceFolders)
        processFolder(folder);

    // ── Count missing entries ─────────────────────────────────────────────
    for (const auto& [path, entry] : m_manifest.GetFiles())
    {
        if (!visited.contains(path))
            ++result.filesMissing;
    }

    TRACE(L"BackupEngine: pass complete — new=%zu mod=%zu rel=%zu unch=%zu miss=%zu stored=%zu err=%zu\n",
          result.filesNew, result.filesModified, result.filesRelocated,
          result.filesUnchanged, result.filesMissing, result.objectsStored, result.errors);

    return result;
}

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------

std::unordered_map<std::wstring, BackupFileStatus> CBackupEngine::ClassifyAll() const
{
    // Build hash index: hash → [paths that reference it in manifest]
    const auto hashIndex = m_manifest.BuildHashIndex();

    std::unordered_map<std::wstring, BackupFileStatus> out;
    out.reserve(m_manifest.GetFileCount());

    for (const auto& [normPath, entry] : m_manifest.GetFiles())
    {
        std::error_code ec;
        // NormalizePath produces forward slashes; Windows accepts both.
        const bool onDisk = std::filesystem::exists(
            std::filesystem::path(normPath), ec);

        if (onDisk)
        {
            // BackedDuplicate when at least one OTHER path shares the same hash.
            const auto it = hashIndex.find(entry.currentHash);
            const bool isDuplicate = it != hashIndex.end() && it->second.size() > 1;
            out.emplace(normPath,
                isDuplicate ? BackupFileStatus::BackedDuplicate
                            : BackupFileStatus::BackedUnique);
        }
        else
        {
            // MissingSurvived when the same content exists at another path
            // that IS still on disk.
            const auto it = hashIndex.find(entry.currentHash);
            bool survived = false;
            if (it != hashIndex.end())
            {
                survived = std::ranges::any_of(it->second, [&](const std::wstring& p)
                {
                    if (p == normPath) return false;
                    std::error_code ec2;
                    return std::filesystem::exists(std::filesystem::path(p), ec2);
                });
            }
            out.emplace(normPath,
                survived ? BackupFileStatus::MissingSurvived
                         : BackupFileStatus::MissingDeleted);
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::wstring CBackupEngine::ManifestPath() const
{
    return m_store.GetBackupRoot() + L"\\manifest.json";
}

double CBackupEngine::FileTimeToUnix(std::filesystem::file_time_type t) noexcept
{
    // clock_cast converts from file_clock (Windows: 100ns since 1601-01-01)
    // to system_clock (Unix: nanoseconds since 1970-01-01).
    using namespace std::chrono;
    const auto sysTime = clock_cast<system_clock>(t);
    return duration_cast<duration<double>>(sysTime.time_since_epoch()).count();
}
