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
#include "FileBackupControl.h"
#include "Options.h"
#include "../Dialogs/ManageSourceFoldersDlg.h"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Backup-specific persistent settings (declared in FileBackupControl.h).
// Keeping these here avoids any modification to Options.h/cpp.
// ---------------------------------------------------------------------------
namespace backup {
    Setting<std::vector<int>>          ColumnOrder  (L"BackupView", L"ColumnOrder");
    Setting<std::vector<int>>          ColumnWidths (L"BackupView", L"ColumnWidths");
    Setting<std::wstring>              Root         (L"BackupView", L"BackupRoot");
    Setting<std::vector<std::wstring>> SourceFolders(L"BackupView", L"SourceFolders");
}

// ---------------------------------------------------------------------------
// Local copy of CBackupEngine::FileTimeToUnix (private in BackupEngine.h).
// Avoids making BackupEngine's private helpers public just for this file.
// ---------------------------------------------------------------------------
static double FileTimeToUnix(const fs::file_time_type t) noexcept
{
    using namespace std::chrono;
    const auto sysTime = clock_cast<system_clock>(t);
    return duration_cast<duration<double>>(sysTime.time_since_epoch()).count();
}

static std::wstring NormPath(std::wstring p)
{
    for (auto& c : p) if (c == L'\\') c = L'/';
    return p;
}

static std::wstring ToLower(std::wstring s)
{
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

// Convert normalized path (forward slashes) to a Windows shell path (backslashes, uppercase drive).
static std::wstring NormToShellPath(const std::wstring& norm)
{
    std::wstring p = norm;
    for (auto& c : p) if (c == L'/') c = L'\\';
    if (p.size() >= 2 && p[1] == L':') p[0] = static_cast<wchar_t>(towupper(p[0]));
    return p;
}

static std::wstring LastComponent(const std::wstring& p)
{
    const auto slash = p.find_last_of(L"/\\");
    return slash == std::wstring::npos ? p : p.substr(slash + 1);
}

CBackupDirItem::CBackupDirItem(std::wstring normPath, std::wstring name, bool needsDiskScan)
    : m_path(std::move(normPath))
    , m_name(std::move(name))
    , m_needsDiskScan(needsDiskScan)
{
}

void CBackupDirItem::AddChild(std::unique_ptr<CBackupTreeBase> child)
{
    child->SetParent(this);
    m_children.push_back(std::move(child));
}

void CBackupDirItem::SortChildren()
{
    std::sort(m_children.begin(), m_children.end(),
        [](const std::unique_ptr<CBackupTreeBase>& a,
           const std::unique_ptr<CBackupTreeBase>& b)
    {
        // Dirs before files, then alphabetical
        if (a->IsDirectory() != b->IsDirectory())
            return a->IsDirectory() > b->IsDirectory();
        return _wcsicmp(a->GetPath().c_str(), b->GetPath().c_str()) < 0;
    });
    for (auto& child : m_children)
        if (child->IsDirectory())
            static_cast<CBackupDirItem*>(child.get())->SortChildren();
}

BackupNodeStatus CBackupDirItem::GetStatus() const
{
    if (m_children.empty()) return BackupNodeStatus::Backed;
    BackupNodeStatus first = m_children.front()->GetStatus();
    for (const auto& c : m_children)
        if (c->GetStatus() != first) return BackupNodeStatus::Partial;
    return first;
}

std::wstring CBackupDirItem::GetText(const int subitem) const
{
    switch (subitem)
    {
        case COL_ITEMBAK_NAME: return m_name;
        default:               return {}; // directories show no status/size/date
    }
}

HICON CBackupDirItem::GetIcon()
{
    if (!m_visualInfo) return GetIconHandler()->m_defaultFolderImage;
    if (m_visualInfo->icon) return m_visualInfo->icon;
    GetIconHandler()->DoAsyncShellInfoLookup(std::make_tuple(
        this, m_visualInfo->control, NormToShellPath(m_path),
        FILE_ATTRIBUTE_DIRECTORY, &m_visualInfo->icon, nullptr));
    return nullptr;
}

int CBackupDirItem::CompareSibling(const CTreeListItem* other, const int subitem) const
{
    const auto* o = reinterpret_cast<const CBackupDirItem*>(other);
    switch (subitem)
    {
        case COL_ITEMBAK_NAME:   return _wcsicmp(m_name.c_str(), o->m_name.c_str());
        case COL_ITEMBAK_STATUS: return static_cast<int>(GetStatus()) - static_cast<int>(o->GetStatus());
        default:                 return 0;
    }
}

int CBackupDirItem::GetTreeListChildCount() const
{
    // Return 1 for unscanned lazy dirs so the expand arrow is shown.
    if ((m_needsDiskScan || m_needsModifiedScan) && !m_childrenLoaded) return 1;
    return static_cast<int>(m_children.size());
}

CTreeListItem* CBackupDirItem::GetTreeListChild(const int i) const
{
    // OnBeforeExpand must have been called before children are read.
    if (i < 0 || i >= static_cast<int>(m_children.size())) return nullptr;
    return m_children[i].get();
}

void CBackupDirItem::LoadChildrenFromDisk(const CBackupManifest& manifest,
                                          const std::wstring& searchLower)
{
    m_children.clear();

    // Walk one directory level only.
    std::error_code ec;
    const fs::path dirPath(m_path); // fs::path accepts forward slashes on Windows
    for (const auto& de : fs::directory_iterator(dirPath,
        fs::directory_options::skip_permission_denied, ec))
    {
        const std::wstring norm = NormPath(de.path().wstring());

        if (de.is_directory(ec))
        {
            // Add a lazy-scan stub for this sub-directory.
            const std::wstring name = de.path().filename().wstring();
            auto dirChild = std::make_unique<CBackupDirItem>(norm, name, /*needsDiskScan=*/true);
            dirChild->SetParent(this);
            m_children.push_back(std::move(dirChild));
        }
        else if (de.is_regular_file(ec))
        {
            if (manifest.Contains(norm)) continue; // backed — skip in Unbacked mode

            if (!searchLower.empty())
            {
                if (ToLower(norm).find(searchLower) == std::wstring::npos) continue;
            }

            const ULONGLONG sz = static_cast<ULONGLONG>(de.file_size(ec));
            const std::wstring name = de.path().filename().wstring();
            auto fileChild = std::make_unique<CBackupFileItem>(
                norm, name, BackupNodeStatus::Unbacked, sz, L"");
            fileChild->SetParent(this);
            m_children.push_back(std::move(fileChild));
        }
    }

    // Dirs before files, alphabetical within each group.
    std::sort(m_children.begin(), m_children.end(),
        [](const std::unique_ptr<CBackupTreeBase>& a,
           const std::unique_ptr<CBackupTreeBase>& b)
    {
        if (a->IsDirectory() != b->IsDirectory())
            return a->IsDirectory() > b->IsDirectory();
        return _wcsicmp(a->GetPath().c_str(), b->GetPath().c_str()) < 0;
    });

    m_childrenLoaded = true;
}

CBackupFileItem::CBackupFileItem(std::wstring normPath, std::wstring name,
                                 BackupNodeStatus status, ULONGLONG size,
                                 std::wstring backedAt)
    : m_path(std::move(normPath))
    , m_name(std::move(name))
    , m_status(status)
    , m_size(size)
    , m_backedAt(std::move(backedAt))
{
}

HICON CBackupFileItem::GetIcon()
{
    if (!m_visualInfo) return GetIconHandler()->m_defaultFileImage;
    if (m_visualInfo->icon) return m_visualInfo->icon;
    // SHGFI_USEFILEATTRIBUTES (in the handler defaults) allows icon lookup
    // by extension even for files that don't exist on disk (unbacked/backup-only).
    GetIconHandler()->DoAsyncShellInfoLookup(std::make_tuple(
        this, m_visualInfo->control, NormToShellPath(m_path),
        FILE_ATTRIBUTE_NORMAL, &m_visualInfo->icon, nullptr));
    return nullptr;
}

std::wstring CBackupFileItem::GetText(const int subitem) const
{
    switch (subitem)
    {
        case COL_ITEMBAK_NAME:     return m_name;
        case COL_ITEMBAK_STATUS:   return CFileBackupControl::StatusString(m_status);
        case COL_ITEMBAK_SIZE:     return FormatSizeSuffixes(m_size);
        case COL_ITEMBAK_BACKED_AT: return m_backedAt;
        default:                   return {};
    }
}

int CBackupFileItem::CompareSibling(const CTreeListItem* other, const int subitem) const
{
    const auto* o = reinterpret_cast<const CBackupFileItem*>(other);
    switch (subitem)
    {
        case COL_ITEMBAK_NAME:      return _wcsicmp(m_name.c_str(), o->m_name.c_str());
        case COL_ITEMBAK_STATUS:    return static_cast<int>(m_status) - static_cast<int>(o->m_status);
        case COL_ITEMBAK_SIZE:      return m_size < o->m_size ? -1 : m_size > o->m_size ? 1 : 0;
        case COL_ITEMBAK_BACKED_AT: return m_backedAt.compare(o->m_backedAt);
        default:                    return 0;
    }
}

CFileBackupControl* CFileBackupControl::m_singleton = nullptr;

CFileBackupControl::CFileBackupControl()
    : CTreeListControl(backup::ColumnOrder.Ptr(),
                       backup::ColumnWidths.Ptr(),
                       LF_BACKUPLIST, false)
{
    SetOwnsItems(false); // tree nodes are owned by m_root, not the control
    m_singleton = this;
}

CFileBackupControl::~CFileBackupControl()
{
    m_singleton = nullptr;
}

BEGIN_MESSAGE_MAP(CFileBackupControl, CTreeListControl)
    ON_WM_DESTROY()
    ON_WM_CONTEXTMENU()
END_MESSAGE_MAP()

void CFileBackupControl::OnDestroy()
{
    SetRootItem(nullptr);
    m_root.reset();
    m_engine.reset();
    CWdsListControl::OnDestroy();
}

bool CFileBackupControl::HasSelectedUnbackedItems() const
{
    int si = GetNextItem(-1, LVNI_SELECTED);
    while (si >= 0)
    {
        const auto* item = static_cast<const CBackupTreeBase*>(GetItem(si));
        if (item && item->GetStatus() == BackupNodeStatus::Unbacked)
            return true;
        si = GetNextItem(si, LVNI_SELECTED);
    }
    return false;
}

std::vector<std::wstring> CFileBackupControl::GetSelectedUnbackedPaths() const
{
    std::vector<std::wstring> paths;
    int si = GetNextItem(-1, LVNI_SELECTED);
    while (si >= 0)
    {
        const auto* item = static_cast<const CBackupTreeBase*>(GetItem(si));
        if (item && item->GetStatus() == BackupNodeStatus::Unbacked)
            paths.push_back(NormToShellPath(item->GetPath())); // same as NormToWinPath, defined earlier
        si = GetNextItem(si, LVNI_SELECTED);
    }
    return paths;
}

std::wstring CFileBackupControl::StatusString(const BackupNodeStatus s)
{
    switch (s)
    {
        case BackupNodeStatus::Backed:     return L"Backed Up";
        case BackupNodeStatus::Modified:   return L"Modified";
        case BackupNodeStatus::BackupOnly: return L"Backup only";
        case BackupNodeStatus::Unbacked:   return L"Not backed up";
        case BackupNodeStatus::Partial:    return L"Partial";
        default:                           return {};
    }
}

bool CFileBackupControl::EnsureEngine()
{
    const std::wstring root = backup::Root.Obj();
    if (root.empty()) return false;
    if (!m_engine)
    {
        m_engine = std::make_unique<CBackupEngine>(root, backup::SourceFolders.Obj());
        if (!m_engine->Initialize())
        {
            m_engine.reset();
            return false;
        }
    }
    return true;
}

std::wstring CFileBackupControl::BrowseForFolder(const std::wstring& title)
{
    IFileOpenDialog* pDlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
        CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))))
        return {};

    DWORD dwOpts = 0;
    pDlg->GetOptions(&dwOpts);
    pDlg->SetOptions(dwOpts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    pDlg->SetTitle(title.c_str());

    std::wstring result;
    const HWND hwnd = AfxGetMainWnd() ? AfxGetMainWnd()->GetSafeHwnd() : nullptr;
    if (SUCCEEDED(pDlg->Show(hwnd)))
    {
        IShellItem* pItem = nullptr;
        if (SUCCEEDED(pDlg->GetResult(&pItem)))
        {
            PWSTR pszPath = nullptr;
            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)))
            {
                result = pszPath;
                CoTaskMemFree(pszPath);
            }
            pItem->Release();
        }
    }
    pDlg->Release();
    return result;
}

std::unique_ptr<CBackupDirItem> CFileBackupControl::BuildManifestTree(
    const CBackupManifest& manifest,
    const std::vector<std::wstring>& sourceFolders,
    const BackupFilter filter,
    const std::wstring& searchLower)
{
    // Virtual root; not displayed, just the container.
    auto root = std::make_unique<CBackupDirItem>(L"", L"Backup Sources");

    // Dir path to raw pointer map for O(1) lookup during insertion.
    std::unordered_map<std::wstring, CBackupDirItem*> dirMap;

    // Create one top-level node per source folder.
    for (const auto& sf : sourceFolders)
    {
        const std::wstring norm = NormPath(sf);
        auto sfNode = std::make_unique<CBackupDirItem>(norm, sf); // show full path at root level
        sfNode->m_childrenLoaded = true; // eager build
        dirMap[norm] = sfNode.get();
        root->AddChild(std::move(sfNode));
    }

    const auto& files = manifest.GetFiles();
    for (const auto& [normPath, entry] : files)
    {
        // Skip backed files in BackupOnly mode.
        if (filter == BackupFilter::BackupOnly)
        {
            std::wstring diskPath = normPath;
            for (auto& c : diskPath) if (c == L'/') c = L'\\';
            if (fs::exists(diskPath)) continue;
        }

        // Search filter
        if (!searchLower.empty())
        {
            if (ToLower(normPath).find(searchLower) == std::wstring::npos) continue;
        }

        // Find the owning source folder.
        CBackupDirItem* sfNode = nullptr;
        std::wstring sfNorm;
        for (const auto& sf : sourceFolders)
        {
            const std::wstring n = NormPath(sf);
            if (normPath.size() > n.size() &&
                _wcsnicmp(normPath.c_str(), n.c_str(), n.size()) == 0 &&
                normPath[n.size()] == L'/')
            {
                sfNorm  = n;
                sfNode  = dirMap[n];
                break;
            }
        }
        if (!sfNode) continue; // path not under any known source folder

        // Split relative path into directory components.
        const std::wstring rel = normPath.substr(sfNorm.size() + 1);
        std::vector<std::wstring> parts;
        std::wstringstream ss(rel);
        std::wstring tok;
        while (std::getline(ss, tok, L'/'))
            if (!tok.empty()) parts.push_back(tok);
        if (parts.empty()) continue;

        // Navigate or create intermediate directory nodes.
        CBackupDirItem* cur = sfNode;
        std::wstring accum  = sfNorm;
        for (std::size_t i = 0; i + 1 < parts.size(); ++i)
        {
            accum += L'/' + parts[i];
            auto it = dirMap.find(accum);
            if (it == dirMap.end())
            {
                auto newDir = std::make_unique<CBackupDirItem>(accum, parts[i]);
                newDir->m_childrenLoaded = true;
                CBackupDirItem* ptr = newDir.get();
                cur->AddChild(std::move(newDir));
                dirMap[accum] = ptr;
                cur = ptr;
            }
            else
            {
                cur = it->second;
            }
        }

        // Add the file leaf
        const auto& vs      = entry.versions;
        const ULONGLONG sz  = vs.empty() ? 0 : vs.back().size;
        const std::wstring& ts = vs.empty() ? L"" : vs.back().backedUpAt;

        const BackupNodeStatus status = (filter == BackupFilter::BackupOnly)
            ? BackupNodeStatus::BackupOnly
            : BackupNodeStatus::Backed;

        cur->AddChild(std::make_unique<CBackupFileItem>(
            normPath, parts.back(), status, sz, ts));
    }

    // Remove directories that ended up empty after filtering.
    PruneEmpty(root.get());

    root->SortChildren();
    return root;
}

std::unique_ptr<CBackupDirItem> CFileBackupControl::BuildUnbackedTree(
    const CBackupManifest& manifest,
    const std::vector<std::wstring>& sourceFolders,
    const std::wstring& searchLower)
{
    auto root = std::make_unique<CBackupDirItem>(L"", L"Backup Sources");
    std::unordered_map<std::wstring, CBackupDirItem*> dirMap;

    for (const auto& sf : sourceFolders)
    {
        const std::wstring norm = NormPath(sf);
        auto sfNode = std::make_unique<CBackupDirItem>(norm, sf);
        sfNode->m_childrenLoaded = true;
        dirMap[norm] = sfNode.get();
        root->AddChild(std::move(sfNode));
    }

    for (const auto& sf : sourceFolders)
    {
        const std::wstring sfNorm = NormPath(sf);
        CBackupDirItem* sfNode = dirMap[sfNorm];

        std::error_code ec;
        for (const auto& de : fs::recursive_directory_iterator(
            sf, fs::directory_options::skip_permission_denied, ec))
        {
            if (!de.is_regular_file(ec)) continue;

            const std::wstring norm = NormPath(de.path().wstring());
            if (manifest.Contains(norm)) continue;

            if (!searchLower.empty() &&
                ToLower(norm).find(searchLower) == std::wstring::npos)
                continue;

            const std::wstring rel = norm.substr(sfNorm.size() + 1);
            std::vector<std::wstring> parts;
            {
                std::wstringstream ss(rel);
                std::wstring tok;
                while (std::getline(ss, tok, L'/'))
                    if (!tok.empty()) parts.push_back(tok);
            }
            if (parts.empty()) continue;

            CBackupDirItem* cur = sfNode;
            std::wstring accum = sfNorm;
            for (std::size_t i = 0; i + 1 < parts.size(); ++i)
            {
                accum += L'/' + parts[i];
                auto it = dirMap.find(accum);
                if (it == dirMap.end())
                {
                    auto newDir = std::make_unique<CBackupDirItem>(accum, parts[i]);
                    newDir->m_childrenLoaded = true;
                    CBackupDirItem* ptr = newDir.get();
                    cur->AddChild(std::move(newDir));
                    dirMap[accum] = ptr;
                    cur = ptr;
                }
                else
                {
                    cur = it->second;
                }
            }

            const ULONGLONG sz = static_cast<ULONGLONG>(de.file_size(ec));
            cur->AddChild(std::make_unique<CBackupFileItem>(
                norm, parts.back(), BackupNodeStatus::Unbacked, sz, L""));
        }
    }

    PruneEmpty(root.get());
    root->SortChildren();
    return root;
}

// Convert normalized path (forward slashes) to a Windows display path (backslashes, uppercase drive).
static std::wstring ToDisplayPath(std::wstring norm)
{
    for (auto& c : norm) if (c == L'/') c = L'\\';
    if (norm.size() >= 2 && norm[1] == L':')
        norm[0] = static_cast<wchar_t>(towupper(norm[0]));
    return norm;
}

std::unique_ptr<CBackupDirItem> CFileBackupControl::BuildFlatList(
    const CBackupManifest& manifest,
    const std::vector<std::wstring>& sourceFolders,
    const BackupFilter filter,
    const std::wstring& searchLower)
{
    auto root = std::make_unique<CBackupDirItem>(L"", L"Results");

    for (const auto& [normPath, entry] : manifest.GetFiles())
    {
        // Skip files not under any known source folder.
        bool known = false;
        for (const auto& sf : sourceFolders)
        {
            const std::wstring n = NormPath(sf);
            if (normPath.size() > n.size() &&
                _wcsnicmp(normPath.c_str(), n.c_str(), n.size()) == 0 &&
                normPath[n.size()] == L'/')
            {
                known = true;
                break;
            }
        }
        if (!known) continue;

        // Skip backed files in BackupOnly mode.
        if (filter == BackupFilter::BackupOnly)
        {
            std::wstring disk = normPath;
            for (auto& c : disk) if (c == L'/') c = L'\\';
            if (fs::exists(disk)) continue;
        }

        // Search filter
        if (!searchLower.empty() && ToLower(normPath).find(searchLower) == std::wstring::npos)
            continue;

        const auto& vs    = entry.versions;
        const ULONGLONG sz = vs.empty() ? 0 : vs.back().size;
        const std::wstring& ts = vs.empty() ? L"" : vs.back().backedUpAt;
        const BackupNodeStatus status = (filter == BackupFilter::BackupOnly)
            ? BackupNodeStatus::BackupOnly : BackupNodeStatus::Backed;

        root->AddChild(std::make_unique<CBackupFileItem>(
            normPath, ToDisplayPath(normPath), status, sz, ts));
    }

    root->SortChildren();
    return root;
}

std::unique_ptr<CBackupDirItem> CFileBackupControl::BuildUnbackedFlatList(
    const CBackupManifest& manifest,
    const std::vector<std::wstring>& sourceFolders,
    const std::wstring& searchLower)
{
    auto root = std::make_unique<CBackupDirItem>(L"", L"Not Backed Files");

    for (const auto& sf : sourceFolders)
    {
        std::error_code ec;
        for (const auto& de : fs::recursive_directory_iterator(
            sf, fs::directory_options::skip_permission_denied, ec))
        {
            if (!de.is_regular_file(ec)) continue;

            const std::wstring norm = NormPath(de.path().wstring());
            if (manifest.Contains(norm)) continue;

            if (!searchLower.empty() && ToLower(norm).find(searchLower) == std::wstring::npos)
                continue;

            const ULONGLONG sz = static_cast<ULONGLONG>(de.file_size(ec));
            root->AddChild(std::make_unique<CBackupFileItem>(
                norm, ToDisplayPath(norm), BackupNodeStatus::Unbacked, sz, L""));
        }
    }

    root->SortChildren();
    return root;
}

bool CFileBackupControl::PruneEmpty(CBackupDirItem* dir)
{
    // Recurse into children first, removing empty sub-directories.
    auto& children = dir->m_children;
    children.erase(
        std::remove_if(children.begin(), children.end(),
            [](const std::unique_ptr<CBackupTreeBase>& c)
            {
                if (!c->IsDirectory()) return false;
                return PruneEmpty(static_cast<CBackupDirItem*>(c.get()));
            }),
        children.end());
    return children.empty();
}

// Returns true when diskPath has changed relative to its manifest entry.
// Uses mtime+size as a fast shortcut (same 2-second FAT32 tolerance as RunPass):
// only reads the file to compute SHA-256 when mtime differs but size matches.
static bool IsFileModified(const std::wstring& diskPath, const BackupFileEntry& entry)
{
    if (entry.currentHash.empty() || entry.versions.empty()) return false;
    const auto& latest = entry.versions.back();

    std::error_code ec;
    const ULONGLONG diskSize = static_cast<ULONGLONG>(fs::file_size(diskPath, ec));
    if (ec) return false;
    if (diskSize != latest.size) return true; // Size differs → definitely modified

    const double diskMtime = FileTimeToUnix(fs::last_write_time(diskPath, ec));
    if (ec) return false;
    if (std::abs(diskMtime - latest.mtime) < 2.0) return false; // mtime+size match → unchanged

    // mtime differs but size matches → hash to confirm
    const std::wstring computed = CBackupStore::ComputeFileSha256(diskPath);
    return !computed.empty() && computed != entry.currentHash;
}

// Scans the manifest for any file under normPrefix that is modified.
// Stops at the first hit (early exit) so the caller never pays for a full scan.
static bool HasModifiedFileInSubtree(
    const CBackupManifest& manifest, const std::wstring& normPrefix)
{
    const std::wstring prefix = normPrefix + L'/';
    for (const auto& [normPath, entry] : manifest.GetFiles())
    {
        if (normPath.size() <= prefix.size() ||
            _wcsnicmp(normPath.c_str(), prefix.c_str(), prefix.size()) != 0) continue;

        std::wstring diskPath = normPath;
        for (auto& c : diskPath) if (c == L'/') c = L'\\';
        if (!fs::exists(diskPath)) continue;
        if (IsFileModified(diskPath, entry)) return true; // early exit
    }
    return false;
}

// Lazy-populate a Modified-filter directory node:
//   • direct file children → hash-checked; only Modified ones are added
//   • direct sub-directories → added as lazy stubs only when HasModifiedFileInSubtree
void CBackupDirItem::LoadModifiedFromManifest(
    const CBackupManifest& manifest, const std::wstring& searchLower)
{
    m_children.clear();
    const std::wstring prefix = m_path + L'/';
    std::unordered_set<std::wstring> seenDirs;

    for (const auto& [normPath, entry] : manifest.GetFiles())
    {
        if (normPath.size() <= prefix.size() ||
            _wcsnicmp(normPath.c_str(), prefix.c_str(), prefix.size()) != 0) continue;

        const std::wstring rel = normPath.substr(prefix.size());
        const auto slashPos = rel.find(L'/');

        if (slashPos == std::wstring::npos)
        {
            // Direct file child: check hash
            std::wstring diskPath = normPath;
            for (auto& c : diskPath) if (c == L'/') c = L'\\';
            if (!fs::exists(diskPath)) continue;
            if (!searchLower.empty() &&
                ToLower(normPath).find(searchLower) == std::wstring::npos) continue;
            if (!IsFileModified(diskPath, entry)) continue;

            const auto& vs     = entry.versions;
            const ULONGLONG sz = vs.empty() ? 0 : vs.back().size;
            const std::wstring& ts = vs.empty() ? L"" : vs.back().backedUpAt;
            auto child = std::make_unique<CBackupFileItem>(
                normPath, rel, BackupNodeStatus::Modified, sz, ts);
            child->SetParent(this);
            m_children.push_back(std::move(child));
        }
        else
        {
            // Direct subdirectory: add as lazy Modified stub if it has any modified files
            const std::wstring subdirName = rel.substr(0, slashPos);
            const std::wstring subdirNorm = m_path + L'/' + subdirName;
            if (seenDirs.insert(subdirNorm).second &&
                HasModifiedFileInSubtree(manifest, subdirNorm))
            {
                auto stub = std::make_unique<CBackupDirItem>(subdirNorm, subdirName);
                stub->m_needsModifiedScan = true;
                stub->SetParent(this);
                m_children.push_back(std::move(stub));
            }
        }
    }

    std::sort(m_children.begin(), m_children.end(),
        [](const std::unique_ptr<CBackupTreeBase>& a,
           const std::unique_ptr<CBackupTreeBase>& b)
    {
        if (a->IsDirectory() != b->IsDirectory())
            return a->IsDirectory() > b->IsDirectory();
        return _wcsicmp(a->GetPath().c_str(), b->GetPath().c_str()) < 0;
    });

    m_needsModifiedScan = false;
    m_childrenLoaded    = true;
}

// BuildModifiedTree: lazily shows only source folders that contain at least one
// modified file. Directory contents are loaded on expand via LoadModifiedFromManifest.
std::unique_ptr<CBackupDirItem> CFileBackupControl::BuildModifiedTree(
    const CBackupManifest& manifest,
    const std::vector<std::wstring>& sourceFolders,
    const std::wstring& searchLower)
{
    (void)searchLower; // search is applied per-directory on expand
    auto root = std::make_unique<CBackupDirItem>(L"", L"Backup Sources");

    for (const auto& sf : sourceFolders)
    {
        const std::wstring norm = NormPath(sf);
        if (!HasModifiedFileInSubtree(manifest, norm)) continue;

        auto sfNode = std::make_unique<CBackupDirItem>(norm, sf);
        sfNode->m_needsModifiedScan = true;
        root->AddChild(std::move(sfNode));
    }

    root->SortChildren();
    return root;
}

// BuildModifiedFlatList: scans all manifest entries with the mtime+size fast check;
// only files that are confirmed modified are added to the flat list.
std::unique_ptr<CBackupDirItem> CFileBackupControl::BuildModifiedFlatList(
    const CBackupManifest& manifest,
    const std::vector<std::wstring>& sourceFolders,
    const std::wstring& searchLower)
{
    auto root = std::make_unique<CBackupDirItem>(L"", L"Modified Files");

    for (const auto& [normPath, entry] : manifest.GetFiles())
    {
        bool known = false;
        for (const auto& sf : sourceFolders)
        {
            const std::wstring n = NormPath(sf);
            if (normPath.size() > n.size() &&
                _wcsnicmp(normPath.c_str(), n.c_str(), n.size()) == 0 &&
                normPath[n.size()] == L'/') { known = true; break; }
        }
        if (!known) continue;

        std::wstring diskPath = normPath;
        for (auto& c : diskPath) if (c == L'/') c = L'\\';
        if (!fs::exists(diskPath)) continue;
        if (!IsFileModified(diskPath, entry)) continue;

        if (!searchLower.empty() &&
            ToLower(normPath).find(searchLower) == std::wstring::npos) continue;

        const auto& vs     = entry.versions;
        const ULONGLONG sz = vs.empty() ? 0 : vs.back().size;
        const std::wstring& ts = vs.empty() ? L"" : vs.back().backedUpAt;
        root->AddChild(std::make_unique<CBackupFileItem>(
            normPath, ToDisplayPath(normPath), BackupNodeStatus::Modified, sz, ts));
    }

    root->SortChildren();
    return root;
}

void CFileBackupControl::CheckHashesForChildren(
    CBackupDirItem* dir, const CBackupManifest& manifest)
{
    bool anyChanged = false;
    for (const auto& child : dir->m_children)
    {
        if (child->IsDirectory()) continue;
        auto* file = static_cast<CBackupFileItem*>(child.get());
        if (file->GetStatus() != BackupNodeStatus::Backed) continue;

        const BackupFileEntry* entry = manifest.Find(file->GetPath());
        if (!entry) continue;

        std::wstring diskPath = file->GetPath();
        for (auto& c : diskPath) if (c == L'/') c = L'\\';

        if (IsFileModified(diskPath, *entry))
        {
            file->SetStatus(BackupNodeStatus::Modified);
            anyChanged = true;
        }
    }
    if (anyChanged) Invalidate();
}

void CFileBackupControl::Rebuild(const BackupFilter filter, const std::wstring& search)
{
    m_filter      = filter;
    m_searchLower = ToLower(search);

    // Detach root from control before deleting it.
    SetRootItem(nullptr);
    DeleteAllItems();
    m_root.reset();

    if (!EnsureEngine()) return;

    const auto& sourceFolders = backup::SourceFolders.Obj();
    if (sourceFolders.empty()) return;

    const CWaitCursor wc;
    if (m_flatMode)
    {
        if (filter == BackupFilter::Unbacked)
            m_root = BuildUnbackedFlatList(m_engine->GetManifest(), sourceFolders, m_searchLower);
        else if (filter == BackupFilter::Modified)
            m_root = BuildModifiedFlatList(m_engine->GetManifest(), sourceFolders, m_searchLower);
        else
            m_root = BuildFlatList(m_engine->GetManifest(), sourceFolders, filter, m_searchLower);
    }
    else if (filter == BackupFilter::Unbacked)
    {
        m_root = BuildUnbackedTree(m_engine->GetManifest(), sourceFolders, m_searchLower);
    }
    else if (filter == BackupFilter::Modified)
    {
        m_root = BuildModifiedTree(m_engine->GetManifest(), sourceFolders, m_searchLower);
    }
    else
    {
        m_root = BuildManifestTree(m_engine->GetManifest(), sourceFolders, filter, m_searchLower);
    }

    SetRootItem(m_root.get());

    // Auto-expand root so source folders are immediately visible.
    ExpandItem(m_root.get());
}

void CFileBackupControl::OnBeforeExpand(CTreeListItem* item)
{
    // All items are CBackupTreeBase; static_cast is safe (RTTI disabled in this control).
    auto* base = static_cast<CBackupTreeBase*>(item);
    if (!base->IsDirectory()) return;
    auto* dir = static_cast<CBackupDirItem*>(base);
    if (dir->NeedsDiskScan() && !dir->IsChildrenLoaded() && m_engine)
        dir->LoadChildrenFromDisk(m_engine->GetManifest(), m_searchLower);

    if (dir->NeedsModifiedScan() && !dir->IsChildrenLoaded() && m_engine)
        dir->LoadModifiedFromManifest(m_engine->GetManifest(), m_searchLower);

    // In All mode: lazily hash-check backed children on expand so Modified files
    // are discovered incrementally without a full upfront scan.
    if (m_filter == BackupFilter::All && m_engine)
        CheckHashesForChildren(dir, m_engine->GetManifest());
}

void CFileBackupControl::RunBackupPass()
{
    if (!EnsureEngine()) return;
    const CWaitCursor wc;
    (void)m_engine->RunPass();
    (void)m_engine->Save();
    Rebuild(m_filter, m_searchLower);
}

// Collect all manifest paths that are direct or nested children of dirNormPath.
static std::vector<std::wstring> CollectManifestPaths(
    const CBackupManifest& manifest, const std::wstring& dirNormPath)
{
    const std::wstring prefix = dirNormPath + L'/';
    std::vector<std::wstring> result;
    for (const auto& [normPath, entry] : manifest.GetFiles())
        if (normPath.size() > prefix.size() &&
            _wcsnicmp(normPath.c_str(), prefix.c_str(), prefix.size()) == 0)
            result.push_back(normPath);
    return result;
}

// Convert normalized path to a Windows path suitable for file operations.
static std::wstring NormToWinPath(const std::wstring& norm)
{
    std::wstring path = norm;
    for (auto& c : path) if (c == L'/') c = L'\\';
    if (path.size() >= 2 && path[1] == L':')
        path[0] = static_cast<wchar_t>(towupper(path[0]));
    return path;
}

// Restore one manifest entry to destPath (creates parent dirs automatically).
static bool RestoreOne(const CBackupEngine& engine,
                       const std::wstring& normPath,
                       const std::wstring& destPath)
{
    const BackupFileEntry* entry = engine.GetManifest().Find(normPath);
    if (!entry || entry->currentHash.empty()) return false;
    return engine.GetStore().RetrieveObject(entry->currentHash, destPath);
}

void CFileBackupControl::OnContextMenu(CWnd*, CPoint pt)
{
    enum : UINT
    {
        ID_RUN_PASS = 1,
        ID_SET_ROOT,
        ID_MANAGE_FOLDERS,
        ID_RESTORE_ORIGINAL,
        ID_RESTORE_FOLDER,
        ID_PURGE,
    };

    CPoint client = pt;
    ScreenToClient(&client);
    const int hitIdx = HitTest(client);
    CBackupTreeBase* hitItem = nullptr;
    if (hitIdx >= 0)
        hitItem = static_cast<CBackupTreeBase*>(GetItem(hitIdx));

    const bool isBackupOnly = hitItem && hitItem->GetStatus() == BackupNodeStatus::BackupOnly;
    const bool isUnbacked   = hitItem && hitItem->GetStatus() == BackupNodeStatus::Unbacked;

    // Unbacked items exist on disk — show a WinDirStat-style context menu with
    // open/delete/explore operations, plus the shell menu as a submenu.
    if (isUnbacked)
    {
        std::vector<std::wstring> paths;
        {
            int si = GetNextItem(-1, LVNI_SELECTED);
            while (si >= 0)
            {
                auto* sel = static_cast<CBackupTreeBase*>(GetItem(si));
                if (sel && sel->GetStatus() == BackupNodeStatus::Unbacked)
                    paths.push_back(NormToWinPath(sel->GetPath()));
                si = GetNextItem(si, LVNI_SELECTED);
            }
        }
        const std::wstring hitPath = NormToWinPath(hitItem->GetPath());
        if (std::find(paths.begin(), paths.end(), hitPath) == paths.end())
            paths.push_back(hitPath);

        enum : UINT
        {
            ID_UB_OPEN            = 0x8001,
            ID_UB_COPY_PATH,
            ID_UB_EXPLORER_SELECT,
            ID_UB_CMD_HERE,
            ID_UB_PWSH_HERE,
            ID_UB_PROPERTIES,
            ID_UB_DELETE_PERM,
            ID_UB_DELETE_BIN,
        };

        CMenu mainMenu;
        mainMenu.CreatePopupMenu();
        mainMenu.AppendMenu(MF_STRING,   ID_UB_OPEN,            L"Open");
        mainMenu.AppendMenu(MF_SEPARATOR);
        mainMenu.AppendMenu(MF_STRING,   ID_UB_COPY_PATH,        L"Copy Path");
        mainMenu.AppendMenu(MF_STRING,   ID_UB_EXPLORER_SELECT,  L"Select in Explorer...");
        mainMenu.AppendMenu(MF_STRING,   ID_UB_CMD_HERE,         L"Open in Command Prompt...");
        mainMenu.AppendMenu(MF_STRING,   ID_UB_PWSH_HERE,        L"Open in PowerShell...");
        mainMenu.AppendMenu(MF_SEPARATOR);
        mainMenu.AppendMenu(MF_STRING,   ID_UB_PROPERTIES,       L"Properties");
        mainMenu.AppendMenu(MF_SEPARATOR);
        mainMenu.AppendMenu(MF_STRING,   ID_UB_DELETE_PERM,      L"Delete Permanently");
        mainMenu.AppendMenu(MF_STRING,   ID_UB_DELETE_BIN,       L"Delete (to Recycle Bin)");

        CComPtr<IContextMenu> shellMenu;
        shellMenu.Attach(GetContextMenu(GetSafeHwnd(), paths));
        if (shellMenu)
        {
            CMenu explorerSub;
            explorerSub.CreatePopupMenu();
            (void)shellMenu->QueryContextMenu(explorerSub.GetSafeHmenu(), 0,
                CONTENT_MENU_MINCMD, CONTENT_MENU_MAXCMD, CMF_NORMAL);
            mainMenu.AppendMenu(MF_SEPARATOR);
            mainMenu.AppendMenu(MF_POPUP,
                reinterpret_cast<UINT_PTR>(explorerSub.GetSafeHmenu()),
                L"Windows Explorer Menu");
            explorerSub.Detach(); // mainMenu now owns the submenu HMENU
        }

        const UINT id = static_cast<UINT>(mainMenu.TrackPopupMenu(
            TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, this));

        auto folderOf = [](const std::wstring& p) -> std::wstring
        {
            std::error_code ec;
            if (fs::is_directory(p, ec)) return p;
            const auto sl = p.find_last_of(L'\\');
            return sl != std::wstring::npos ? p.substr(0, sl) : p;
        };

        switch (id)
        {
            case ID_UB_OPEN:
                for (const auto& p : paths)
                    ShellExecuteWrapper(p, L"", L"open", GetSafeHwnd());
                break;

            case ID_UB_COPY_PATH:
            {
                std::wstring text;
                for (const auto& p : paths) { text += p; text += L"\r\n"; }
                if (!text.empty()) text.resize(text.size() - 2);
                if (OpenClipboard())
                {
                    EmptyClipboard();
                    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
                    if (const HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes))
                    {
                        wmemcpy(static_cast<wchar_t*>(GlobalLock(hMem)),
                            text.c_str(), text.size() + 1);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                    CloseClipboard();
                }
                break;
            }

            case ID_UB_EXPLORER_SELECT:
                for (const auto& p : paths)
                    ShellExecuteWrapper(L"explorer.exe",
                        L"/select,\"" + p + L"\"", L"open", GetSafeHwnd());
                break;

            case ID_UB_CMD_HERE:
            {
                std::unordered_set<std::wstring> seen;
                for (const auto& p : paths)
                {
                    std::wstring dir = folderOf(p);
                    if (seen.insert(dir).second)
                        ShellExecuteWrapper(L"cmd.exe",
                            L"/K TITLE WinDirStat - \"" + dir +
                                L"\" && cd /d \"" + dir + L"\"",
                            L"open", GetSafeHwnd(), dir);
                }
                break;
            }

            case ID_UB_PWSH_HERE:
            {
                static std::wstring pwsh(MAX_PATH, L'\0');
                if (!pwsh.front())
                    for (const auto exe : { L"pwsh.exe", L"powershell.exe" })
                        if (SearchPath(nullptr, exe, nullptr,
                            static_cast<DWORD>(pwsh.size()), pwsh.data(), nullptr) > 0)
                        {
                            pwsh.resize(wcslen(pwsh.data())); break;
                        }
                std::unordered_set<std::wstring> seen;
                for (const auto& p : paths)
                {
                    std::wstring dir = folderOf(p);
                    if (seen.insert(dir).second)
                        ShellExecuteWrapper(pwsh, L"", L"open", GetSafeHwnd(), dir);
                }
                break;
            }

            case ID_UB_PROPERTIES:
                for (const auto& p : paths)
                {
                    PIDLIST_ABSOLUTE pidl = ILCreateFromPath(p.c_str());
                    if (!pidl) continue;
                    SHELLEXECUTEINFO sei{};
                    sei.cbSize   = sizeof(sei);
                    sei.hwnd     = GetSafeHwnd();
                    sei.lpVerb   = L"properties";
                    sei.fMask    = SEE_MASK_INVOKEIDLIST | SEE_MASK_IDLIST |
                                   SEE_MASK_NOZONECHECKS;
                    sei.lpIDList = pidl;
                    sei.nShow    = SW_SHOWNORMAL;
                    ShellExecuteEx(&sei);
                    ILFree(pidl);
                }
                break;

            case ID_UB_DELETE_PERM:
            case ID_UB_DELETE_BIN:
            {
                std::wstring pathList;
                for (const auto& p : paths) { pathList += p; pathList += L'\0'; }
                pathList += L'\0';
                SHFILEOPSTRUCTW op{};
                op.hwnd   = GetSafeHwnd();
                op.wFunc  = FO_DELETE;
                op.pFrom  = pathList.c_str();
                op.fFlags = (id == ID_UB_DELETE_BIN)
                    ? static_cast<FILEOP_FLAGS>(FOF_ALLOWUNDO | FOF_WANTNUKEWARNING)
                    : static_cast<FILEOP_FLAGS>(FOF_WANTNUKEWARNING);
                if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted)
                    Rebuild(m_filter, m_searchLower);
                break;
            }

            default:
                if (shellMenu && id >= CONTENT_MENU_MINCMD && id <= CONTENT_MENU_MAXCMD)
                {
                    CMINVOKECOMMANDINFOEX info{};
                    info.cbSize  = sizeof(info);
                    info.fMask   = CMIC_MASK_UNICODE;
                    info.hwnd    = GetSafeHwnd();
                    info.lpVerb  = MAKEINTRESOURCEA(id - CONTENT_MENU_MINCMD);
                    info.lpVerbW = MAKEINTRESOURCEW(id - CONTENT_MENU_MINCMD);
                    info.nShow   = SW_SHOWNORMAL;
                    (void)shellMenu->InvokeCommand(
                        reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
                    Rebuild(m_filter, m_searchLower);
                }
                break;
        }
        return;
    }

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_RUN_PASS,       L"Run Backup Pass");
    menu.AppendMenu(MF_SEPARATOR);
    const std::wstring& curRoot = backup::Root.Obj();
    const std::wstring rootLabel = L"Set Backup Root... [" +
        (curRoot.empty() ? L"not set" : curRoot) + L"]";
    menu.AppendMenu(MF_STRING, ID_SET_ROOT, rootLabel.c_str());
    menu.AppendMenu(MF_STRING, ID_MANAGE_FOLDERS, L"Manage Source Folders...");

    if (isBackupOnly)
    {
        menu.AppendMenu(MF_SEPARATOR);
        menu.AppendMenu(MF_STRING, ID_RESTORE_ORIGINAL, L"Restore to Original Location");
        menu.AppendMenu(MF_STRING, ID_RESTORE_FOLDER,   L"Restore to Folder...");
        menu.AppendMenu(MF_STRING, ID_PURGE,             L"Purge from Backup...");
    }

    const UINT id = static_cast<UINT>(menu.TrackPopupMenu(
        TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, this));

    switch (id)
    {
        case ID_RUN_PASS:
            RunBackupPass();
            break;

        case ID_SET_ROOT:
        {
            const std::wstring root = BrowseForFolder(L"Select Backup Root Folder");
            if (!root.empty())
            {
                backup::Root = root;
                m_engine.reset();
                Rebuild(m_filter, m_searchLower);
            }
            break;
        }

        case ID_MANAGE_FOLDERS:
        {
            CManageSourceFoldersDlg dlg(this);
            if (dlg.DoModal() == IDOK)
            {
                m_engine.reset();
                Rebuild(m_filter, m_searchLower);
            }
            break;
        }

        case ID_RESTORE_ORIGINAL:
        {
            if (!hitItem || !m_engine) break;

            const CWaitCursor wc;
            int restored = 0, failed = 0;

            if (!hitItem->IsDirectory())
            {
                // Single file: restore to exact original path
                const std::wstring dest = NormToWinPath(hitItem->GetPath());
                RestoreOne(*m_engine, hitItem->GetPath(), dest) ? ++restored : ++failed;
            }
            else
            {
                // Directory: restore every manifest entry under this dir
                for (const auto& normPath : CollectManifestPaths(
                        m_engine->GetManifest(), hitItem->GetPath()))
                {
                    RestoreOne(*m_engine, normPath, NormToWinPath(normPath))
                        ? ++restored : ++failed;
                }
            }

            const std::wstring msg = std::to_wstring(restored) + L" file(s) restored." +
                (failed ? L"\n" + std::to_wstring(failed) + L" failed." : L"");
            AfxMessageBox(msg.c_str(), failed ? MB_ICONWARNING : MB_ICONINFORMATION);
            break;
        }

        case ID_RESTORE_FOLDER:
        {
            if (!hitItem || !m_engine) break;
            const std::wstring destRoot = BrowseForFolder(L"Select Restore Destination");
            if (destRoot.empty()) break;

            const CWaitCursor wc;
            int restored = 0, failed = 0;

            if (!hitItem->IsDirectory())
            {
                // Single file: copy to destRoot\<filename>
                const std::wstring dest = destRoot + L'\\' + LastComponent(hitItem->GetPath());
                RestoreOne(*m_engine, hitItem->GetPath(), dest) ? ++restored : ++failed;
            }
            else
            {
                // Directory: recreate subtree structure under destRoot\<dirname>
                const std::wstring& srcDir = hitItem->GetPath();
                const std::wstring destDir = destRoot + L'\\' + LastComponent(srcDir);
                const std::wstring srcPrefix = srcDir + L'/';

                for (const auto& normPath : CollectManifestPaths(
                        m_engine->GetManifest(), srcDir))
                {
                    // Strip the source dir prefix, replace with destDir
                    const std::wstring rel = normPath.substr(srcPrefix.size());
                    std::wstring relWin = rel;
                    for (auto& c : relWin) if (c == L'/') c = L'\\';
                    const std::wstring dest = destDir + L'\\' + relWin;
                    RestoreOne(*m_engine, normPath, dest) ? ++restored : ++failed;
                }
            }

            const std::wstring msg = std::to_wstring(restored) + L" file(s) restored to:\n" +
                destRoot + (failed ? L"\n" + std::to_wstring(failed) + L" failed." : L"");
            AfxMessageBox(msg.c_str(), failed ? MB_ICONWARNING : MB_ICONINFORMATION);
            break;
        }

        case ID_PURGE:
        {
            if (!m_engine) break;

            CBackupManifest& manifest = m_engine->GetManifest();

            // Collect manifest paths from every selected BackupOnly item.
            // Dirs contribute all their manifest children; dedup handles
            // the case where a dir and a child are both selected.
            std::vector<std::wstring> toPurge;
            int si = GetNextItem(-1, LVNI_SELECTED);
            while (si >= 0)
            {
                auto* item = static_cast<CBackupTreeBase*>(GetItem(si));
                if (item && item->GetStatus() == BackupNodeStatus::BackupOnly)
                {
                    if (item->IsDirectory())
                    {
                        for (const auto& p : CollectManifestPaths(manifest, item->GetPath()))
                        {
                            std::wstring diskPath = p;
                            for (auto& c : diskPath) if (c == L'/') c = L'\\';
                            if (!fs::exists(diskPath))
                                toPurge.push_back(p);
                        }
                    }
                    else
                    {
                        toPurge.push_back(item->GetPath());
                    }
                }
                si = GetNextItem(si, LVNI_SELECTED);
            }

            std::sort(toPurge.begin(), toPurge.end());
            toPurge.erase(std::unique(toPurge.begin(), toPurge.end()), toPurge.end());
            if (toPurge.empty()) break;

            const std::wstring msg = toPurge.size() == 1
                ? L"Remove this file from the backup?\n\n" + NormToWinPath(toPurge.front()) + L"\n\nThis cannot be undone."
                : std::to_wstring(toPurge.size()) + L" files will be removed from the backup.\n\nThis cannot be undone.";
            if (AfxMessageBox(msg.c_str(), MB_YESNO | MB_ICONWARNING) != IDYES) break;
            if (AfxMessageBox(L"Are you absolutely sure? The stored data will be permanently deleted.",
                MB_YESNO | MB_ICONWARNING) != IDYES) break;

            const CWaitCursor wc;
            int purged = 0;
            for (const auto& normPath : toPurge)
            {
                std::wstring orphanedHash;
                if (manifest.Purge(normPath, orphanedHash))
                {
                    if (!orphanedHash.empty())
                        m_engine->GetStore().DeleteObject(orphanedHash);
                    ++purged;
                }
            }

            if (purged > 0)
            {
                (void)m_engine->Save();
                Rebuild(m_filter, m_searchLower);
            }
            break;
        }

        default:
            break;
    }
}
