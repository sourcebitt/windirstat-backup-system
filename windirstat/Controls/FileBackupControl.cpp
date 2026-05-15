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
#include "FileBackupControl.h"
#include "Options.h"
#include "../Dialogs/ManageSourceFoldersDlg.h"

// ---------------------------------------------------------------------------
// CBackupItem
// ---------------------------------------------------------------------------

CBackupItem::CBackupItem(std::wstring path, BackupFileStatus status, ULONGLONG size, std::wstring backedAt)
    : m_path(std::move(path))
    , m_status(status)
    , m_size(size)
    , m_backedAt(std::move(backedAt))
{
}

std::wstring CBackupItem::GetText(const int subitem) const
{
    switch (subitem)
    {
        case COL_ITEMBAK_NAME:     return m_path;
        case COL_ITEMBAK_STATUS:   return StatusString(m_status);
        case COL_ITEMBAK_SIZE:     return FormatSizeSuffixes(m_size);
        case COL_ITEMBAK_BACKED_AT: return m_backedAt;
        default: return {};
    }
}

int CBackupItem::CompareSibling(const CTreeListItem* other, const int subitem) const
{
    const auto* o = reinterpret_cast<const CBackupItem*>(other);
    switch (subitem)
    {
        case COL_ITEMBAK_NAME:      return _wcsicmp(m_path.c_str(), o->m_path.c_str());
        case COL_ITEMBAK_STATUS:    return static_cast<int>(m_status) - static_cast<int>(o->m_status);
        case COL_ITEMBAK_SIZE:      return m_size < o->m_size ? -1 : m_size > o->m_size ? 1 : 0;
        case COL_ITEMBAK_BACKED_AT: return m_backedAt.compare(o->m_backedAt);
        default: return 0;
    }
}

std::wstring CBackupItem::StatusString(const BackupFileStatus s)
{
    switch (s)
    {
        case BackupFileStatus::BackedUnique:    return L"Backed (unique)";
        case BackupFileStatus::BackedDuplicate: return L"Backed (duplicate)";
        case BackupFileStatus::Unbacked:        return L"Not backed";
        case BackupFileStatus::MissingSurvived: return L"Missing (survived)";
        case BackupFileStatus::MissingDeleted:  return L"Missing (deleted)";
        default:                                return {};
    }
}

// ---------------------------------------------------------------------------
// CFileBackupControl
// ---------------------------------------------------------------------------

CFileBackupControl* CFileBackupControl::m_singleton = nullptr;

CFileBackupControl::CFileBackupControl()
    : CTreeListControl(COptions::BackupColumnOrder.Ptr(), COptions::BackupColumnWidths.Ptr(), LF_BACKUPLIST, false)
{
    SetOwnsItems(true);
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
    m_engine.reset();
    DeleteAllItems();
    CWdsListControl::OnDestroy();
}

// ---------------------------------------------------------------------------
// Engine management
// ---------------------------------------------------------------------------

bool CFileBackupControl::EnsureEngine()
{
    const std::wstring root = COptions::BackupRoot.Obj();
    if (root.empty()) return false;

    if (!m_engine)
    {
        m_engine = std::make_unique<CBackupEngine>(root, COptions::BackupSourceFolders.Obj());
        if (!m_engine->Initialize())
        {
            m_engine.reset();
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Public operations
// ---------------------------------------------------------------------------

void CFileBackupControl::Populate()
{
    if (!EnsureEngine()) return;

    const auto classifications = m_engine->ClassifyAll();

    const CSetRedrawLock lock(this);
    DeleteAllItems();

    for (const auto& [normPath, status] : classifications)
    {
        const BackupFileEntry* entry = m_engine->GetManifest().Find(normPath);
        const ULONGLONG size     = (entry && !entry->versions.empty()) ? entry->versions.back().size      : 0;
        const std::wstring& ts   = (entry && !entry->versions.empty()) ? entry->versions.back().backedUpAt : L"";
        InsertItem(GetItemCount(), new CBackupItem(normPath, status, size, ts));
    }

    SortItems();
}

void CFileBackupControl::RunBackupPass()
{
    if (!EnsureEngine()) return;

    const CWaitCursor wc;
    (void)m_engine->RunPass();
    (void)m_engine->Save();
    Populate();
}

// ---------------------------------------------------------------------------
// Context menu
// ---------------------------------------------------------------------------

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
    const HWND hwndParent = AfxGetMainWnd() ? AfxGetMainWnd()->GetSafeHwnd() : nullptr;
    if (SUCCEEDED(pDlg->Show(hwndParent)))
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

void CFileBackupControl::OnContextMenu(CWnd*, CPoint pt)
{
    enum : UINT { ID_RUN_PASS = 1, ID_SET_ROOT, ID_MANAGE_FOLDERS };

    CMenu menu;
    menu.CreatePopupMenu();
    menu.AppendMenu(MF_STRING, ID_RUN_PASS,       L"Run Backup Pass");
    menu.AppendMenu(MF_SEPARATOR);
    menu.AppendMenu(MF_STRING, ID_SET_ROOT,       L"Set Backup Root...");
    menu.AppendMenu(MF_STRING, ID_MANAGE_FOLDERS, L"Manage Source Folders...");

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
                COptions::BackupRoot = root;
                m_engine.reset();
                Populate();
            }
            break;
        }

        case ID_MANAGE_FOLDERS:
        {
            CManageSourceFoldersDlg dlg(this);
            if (dlg.DoModal() == IDOK)
            {
                m_engine.reset();
                Populate();
            }
            break;
        }

        default:
            break;
    }
}
