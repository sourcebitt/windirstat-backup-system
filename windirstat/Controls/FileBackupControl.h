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

#pragma once

#include "pch.h"
#include "TreeListControl.h"
#include "BackupEngine.h"

// Column indices (must match insertion order in CFileBackupView::OnCreate)
using ITEMBAKCOLUMNS = enum : std::uint8_t
{
    COL_ITEMBAK_NAME = 0,
    COL_ITEMBAK_STATUS,
    COL_ITEMBAK_SIZE,
    COL_ITEMBAK_BACKED_AT,
};

enum class BackupFilter : std::uint8_t
{
    All,        // Show everything in the manifest as a browsable tree
    BackupOnly, // Show only manifest entries whose path no longer exists on disk
    Unbacked,   // Show only files on disk that are not in the manifest (lazy per-dir)
};

enum class BackupNodeStatus : std::uint8_t
{
    Backed,
    BackupOnly, // In manifest, missing from disk
    Unbacked,   // On disk, not in manifest
    Partial,    // Directory containing mixed children
};

// ---------------------------------------------------------------------------
// CBackupTreeBase — abstract base for all backup tree nodes
// ---------------------------------------------------------------------------
class CBackupTreeBase : public CTreeListItem
{
public:
    ~CBackupTreeBase() override = default;

    virtual bool IsDirectory() const = 0;
    virtual const std::wstring& GetPath() const = 0;
    virtual BackupNodeStatus GetStatus() const = 0;

    CItem* GetLinkedItem() noexcept override { return nullptr; }
    // Subclasses override GetIcon() to trigger async shell icon lookup.
};

// ---------------------------------------------------------------------------
// CBackupDirItem — directory node; children populated eagerly or lazily
// ---------------------------------------------------------------------------
class CBackupDirItem final : public CBackupTreeBase
{
public:
    CBackupDirItem(std::wstring normPath, std::wstring name, bool needsDiskScan = false);
    ~CBackupDirItem() override = default;

    void AddChild(std::unique_ptr<CBackupTreeBase> child);
    void SortChildren();

    // Called by CFileBackupControl::OnBeforeExpand for Unbacked lazy dirs
    void LoadChildrenFromDisk(const CBackupManifest& manifest, const std::wstring& searchLower);

    bool IsChildrenLoaded() const noexcept { return m_childrenLoaded; }
    bool NeedsDiskScan()    const noexcept { return m_needsDiskScan; }

    // CBackupTreeBase
    bool IsDirectory() const override { return true; }
    const std::wstring& GetPath() const override { return m_path; }
    BackupNodeStatus GetStatus() const override;
    HICON GetIcon() override;

    // CTreeListItem
    std::wstring GetText(int subitem) const override;
    int CompareSibling(const CTreeListItem* other, int subitem) const override;
    CTreeListItem* GetTreeListChild(int i) const override;
    int GetTreeListChildCount() const override;

    friend class CFileBackupControl;

private:
    std::wstring m_path;  // normalized (forward slashes)
    std::wstring m_name;  // display label
    std::vector<std::unique_ptr<CBackupTreeBase>> m_children;
    bool m_childrenLoaded = false;
    bool m_needsDiskScan  = false; // true → lazy load from disk on expand
};

// ---------------------------------------------------------------------------
// CBackupFileItem — leaf file node
// ---------------------------------------------------------------------------
class CBackupFileItem final : public CBackupTreeBase
{
public:
    CBackupFileItem(std::wstring normPath, std::wstring name,
                    BackupNodeStatus status, ULONGLONG size, std::wstring backedAt);
    ~CBackupFileItem() override = default;

    bool IsDirectory() const override { return false; }
    const std::wstring& GetPath() const override { return m_path; }
    BackupNodeStatus GetStatus() const override { return m_status; }
    HICON GetIcon() override;

    std::wstring GetText(int subitem) const override;
    int CompareSibling(const CTreeListItem* other, int subitem) const override;
    CTreeListItem* GetTreeListChild(int) const override { return nullptr; }
    int GetTreeListChildCount() const override { return 0; }

private:
    std::wstring     m_path;
    std::wstring     m_name;
    BackupNodeStatus m_status;
    ULONGLONG        m_size;
    std::wstring     m_backedAt;
};

// ---------------------------------------------------------------------------
// CFileBackupControl — the tree-list control living in the Backup tab
// ---------------------------------------------------------------------------
class CFileBackupControl final : public CTreeListControl
{
public:
    CFileBackupControl();
    ~CFileBackupControl() override;

    static CFileBackupControl* Get() { return m_singleton; }

    // Rebuild the tree for the given filter/search combination.
    // Called from CFileBackupView when the user changes a checkbox or types in the search box.
    void Rebuild(BackupFilter filter, const std::wstring& search = {});

    void SetFlatMode(bool flat) { m_flatMode = flat; }

    void RunBackupPass();

    static std::wstring StatusString(BackupNodeStatus s);

protected:
    static CFileBackupControl* m_singleton;

    std::unique_ptr<CBackupEngine>   m_engine;
    std::unique_ptr<CBackupDirItem>  m_root;
    BackupFilter                     m_filter    = BackupFilter::All;
    std::wstring                     m_searchLower;
    bool                             m_flatMode  = false;

    bool EnsureEngine();
    std::wstring BrowseForFolder(const std::wstring& title);

    // Build the manifest tree (All / BackupOnly modes).
    std::unique_ptr<CBackupDirItem> BuildManifestTree(
        const CBackupManifest& manifest,
        const std::vector<std::wstring>& sourceFolders,
        BackupFilter filter,
        const std::wstring& searchLower);

    // Build a flat list from the manifest (All / BackupOnly flat-list mode).
    std::unique_ptr<CBackupDirItem> BuildFlatList(
        const CBackupManifest& manifest,
        const std::vector<std::wstring>& sourceFolders,
        BackupFilter filter,
        const std::wstring& searchLower);

    // Build a flat list by scanning source folders for unbacked files.
    std::unique_ptr<CBackupDirItem> BuildUnbackedFlatList(
        const CBackupManifest& manifest,
        const std::vector<std::wstring>& sourceFolders,
        const std::wstring& searchLower);

    // Build an eager tree of unbacked files (no empty dirs, no lazy stubs).
    std::unique_ptr<CBackupDirItem> BuildUnbackedTree(
        const CBackupManifest& manifest,
        const std::vector<std::wstring>& sourceFolders,
        const std::wstring& searchLower);

    // Prune dirs that ended up empty after filtering.
    static bool PruneEmpty(CBackupDirItem* dir);

    // CTreeListControl hook — loads lazy children before expand.
    void OnBeforeExpand(CTreeListItem* item) override;

    DECLARE_MESSAGE_MAP()
    afx_msg void OnDestroy();
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint pt);
};
