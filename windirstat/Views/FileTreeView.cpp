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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "pch.h"
#include "FileTreeView.h"
#include "DarkMode.h"
#include "Filtering.h"

IMPLEMENT_DYNCREATE(CFileTreeView, CControlView)

BEGIN_MESSAGE_MAP(CFileTreeView, CControlView)
    ON_WM_CREATE()
END_MESSAGE_MAP()

void CFileTreeView::CreateColumns(const bool all)
{
    if (all)
    {
        // Columns should be in enumeration order so initial sort will work
        InsertCol(IDS_COL_NAME, LVCFMT_LEFT, 250, COL_NAME);
        InsertCol(IDS_COL_SIZE_PROPORTION, LVCFMT_RIGHT, CItem::GetSizeProportionWidth() + 30, COL_SIZE_PROPORTION);
        InsertCol(IDS_COL_PERCENTAGE, LVCFMT_RIGHT, 90, COL_PERCENTAGE);
    }

    // reset sort and remove optional columns
    m_control.SetSorting(COL_PERCENTAGE, m_control.GetAscendingDefault(COL_PERCENTAGE));
    m_control.SortItems();
    while (m_control.DeleteColumn(COL_OPTIONAL_START)) {}

    // add optional columns based on settings
    if (COptions::ShowColumnSizePhysical) InsertCol(IDS_COL_SIZE_PHYSICAL, LVCFMT_RIGHT, 90, COL_SIZE_PHYSICAL);
    if (COptions::ShowColumnSizeLogical) InsertCol(IDS_COL_SIZE_LOGICAL, LVCFMT_RIGHT, 90, COL_SIZE_LOGICAL);
    if (COptions::ShowColumnItems) InsertCol(IDS_COL_ITEMS, LVCFMT_RIGHT, 90, COL_ITEMS);
    if (COptions::ShowColumnFiles) InsertCol(IDS_COL_FILES, LVCFMT_RIGHT, 90, COL_FILES);
    if (COptions::ShowColumnFolders) InsertCol(IDS_COL_FOLDERS, LVCFMT_RIGHT, 90, COL_FOLDERS);
    if (COptions::ShowColumnLastChange) InsertCol(IDS_COL_LAST_CHANGE, LVCFMT_LEFT, 120, COL_LAST_CHANGE);
    if (COptions::ShowColumnAttributes) InsertCol(IDS_COL_ATTRIBUTES, LVCFMT_LEFT, 90, COL_ATTRIBUTES);
    if (COptions::ShowColumnOwner) InsertCol(IDS_COL_OWNER, LVCFMT_LEFT, 200, COL_OWNER);

    m_control.OnColumnsInserted();
}

int CFileTreeView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    if (CControlView::OnCreate(lpCreateStruct) == -1) return -1;

    constexpr RECT rect = { 0, 0, 0, 0 };
    m_control.CreateExtended(LVS_EX_HEADERDRAGDROP, LVS_OWNERDATA | WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, rect, this, ID_WDS_CONTROL);
    m_control.ShowGrid(COptions::ListGrid);
    m_control.ShowStripes(COptions::ListStripes);
    m_control.ShowFullRowSelection(COptions::ListFullRowSelection);

    CreateColumns(true);

    return 0;
}

void CFileTreeView::OnUpdate(CView* pSender, const LPARAM lHint, CObject* pHint)
{
    CControlView::OnUpdate(pSender, lHint,
        reinterpret_cast<CObject*>(CDirStatDoc::Get()->GetRootItem()));

    if (lHint == HINT_SELECTIONACTION)
    {
        m_control.EmulateInteractiveSelection(reinterpret_cast<const CItem*>(pHint));
    }
}

IMPLEMENT_DYNCREATE(CFileWatcherView, CControlView)

BEGIN_MESSAGE_MAP(CFileWatcherView, CControlView)
    ON_WM_CREATE()
END_MESSAGE_MAP()

int CFileWatcherView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    if (CControlView::OnCreate(lpCreateStruct) == -1) return -1;

    constexpr RECT rect = {0, 0, 0, 0};
    m_control.CreateExtended(LVS_EX_HEADERDRAGDROP, LVS_OWNERDATA | LVS_SINGLESEL | WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, rect, this, ID_WDS_CONTROL);
    m_control.ShowGrid(COptions::ListGrid);
    m_control.ShowStripes(COptions::ListStripes);
    m_control.ShowFullRowSelection(COptions::ListFullRowSelection);

    // Columns should be in enumeration order so initial sort will work
    InsertCol(IDS_COL_NAME, LVCFMT_LEFT, 500, COL_ITEMWATCH_NAME);
    InsertCol(IDS_COL_TIME, LVCFMT_LEFT, 110, COL_ITEMWATCH_TIME);
    InsertCol(IDS_COL_OPERATION, LVCFMT_LEFT, 100, COL_ITEMWATCH_ACTION);
    InsertCol(IDS_COL_SIZE_LOGICAL, LVCFMT_RIGHT, 90, COL_ITEMWATCH_SIZE_LOGICAL);
    m_control.SetSorting(COL_ITEMWATCH_TIME, false);

    m_control.OnColumnsInserted();

    return 0;
}

IMPLEMENT_DYNCREATE(CFileTopView, CControlView)

BEGIN_MESSAGE_MAP(CFileTopView, CControlView)
    ON_WM_CREATE()
END_MESSAGE_MAP()

int CFileTopView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    if (CControlView::OnCreate(lpCreateStruct) == -1) return -1;

    constexpr RECT rect = { 0, 0, 0, 0 };
    m_control.CreateExtended(LVS_EX_HEADERDRAGDROP, LVS_OWNERDATA | WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, rect, this, ID_WDS_CONTROL);
    m_control.ShowGrid(COptions::ListGrid);
    m_control.ShowStripes(COptions::ListStripes);
    m_control.ShowFullRowSelection(COptions::ListFullRowSelection);

    // Columns should be in enumeration order so initial sort will work
    InsertCol(IDS_COL_NAME, LVCFMT_LEFT, 500, COL_ITEMTOP_NAME);
    InsertCol(IDS_COL_SIZE_PHYSICAL, LVCFMT_RIGHT, 90, COL_ITEMTOP_SIZE_PHYSICAL);
    InsertCol(IDS_COL_SIZE_LOGICAL, LVCFMT_RIGHT, 90, COL_ITEMTOP_SIZE_LOGICAL);
    InsertCol(IDS_COL_LAST_CHANGE, LVCFMT_LEFT, 120, COL_ITEMTOP_LAST_CHANGE);
    m_control.SetSorting(COL_ITEMTOP_SIZE_PHYSICAL, false);

    m_control.OnColumnsInserted();

    return 0;
}

IMPLEMENT_DYNCREATE(CFileDupeView, CControlView)

BEGIN_MESSAGE_MAP(CFileDupeView, CControlView)
    ON_WM_CREATE()
END_MESSAGE_MAP()

int CFileDupeView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    if (CControlView::OnCreate(lpCreateStruct) == -1) return -1;

    constexpr RECT rect = { 0, 0, 0, 0 };
    m_control.CreateExtended(LVS_EX_HEADERDRAGDROP, LVS_OWNERDATA | WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, rect, this, ID_WDS_CONTROL);
    m_control.ShowGrid(COptions::ListGrid);
    m_control.ShowStripes(COptions::ListStripes);
    m_control.ShowFullRowSelection(COptions::ListFullRowSelection);

    // Columns should be in enumeration order so initial sort will work
    const std::wstring hashName = Localization::Lookup(IDS_COL_HASH) + L" / " + Localization::Lookup(IDS_COL_NAME);
    m_control.InsertColumn(CHAR_MAX, hashName.c_str(), LVCFMT_LEFT, DpiRest(500), COL_ITEMDUP_NAME);
    InsertCol(IDS_COL_ITEMS, LVCFMT_RIGHT, 70, COL_ITEMDUP_ITEMS);
    InsertCol(IDS_COL_SIZE_PHYSICAL, LVCFMT_RIGHT, 80, COL_ITEMDUP_SIZE_PHYSICAL);
    InsertCol(IDS_COL_SIZE_LOGICAL, LVCFMT_RIGHT, 80, COL_ITEMDUP_SIZE_LOGICAL);
    InsertCol(IDS_COL_LAST_CHANGE, LVCFMT_LEFT, 120, COL_ITEMDUP_LAST_CHANGE);
    m_control.SetSorting(COL_ITEMDUP_SIZE_PHYSICAL, false);

    m_control.OnColumnsInserted();

    return 0;
}

IMPLEMENT_DYNCREATE(CFileSearchView, CControlView)

BEGIN_MESSAGE_MAP(CFileSearchView, CControlView)
    ON_WM_CREATE()
END_MESSAGE_MAP()

int CFileSearchView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    if (CControlView::OnCreate(lpCreateStruct) == -1) return -1;

    constexpr RECT rect = { 0, 0, 0, 0 };
    m_control.CreateExtended(LVS_EX_HEADERDRAGDROP, LVS_OWNERDATA | WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, rect, this, ID_WDS_CONTROL);
    m_control.ShowGrid(COptions::ListGrid);
    m_control.ShowStripes(COptions::ListStripes);
    m_control.ShowFullRowSelection(COptions::ListFullRowSelection);

    // Columns should be in enumeration order so initial sort will work
    InsertCol(IDS_COL_NAME, LVCFMT_LEFT, 500, COL_ITEMSEARCH_NAME);
    InsertCol(IDS_COL_SIZE_PHYSICAL, LVCFMT_RIGHT, 90, COL_ITEMSEARCH_SIZE_PHYSICAL);
    InsertCol(IDS_COL_SIZE_LOGICAL, LVCFMT_RIGHT, 90, COL_ITEMSEARCH_SIZE_LOGICAL);
    InsertCol(IDS_COL_LAST_CHANGE, LVCFMT_LEFT, 120, COL_ITEMSEARCH_LAST_CHANGE);
    m_control.SetSorting(COL_ITEMSEARCH_SIZE_LOGICAL, false);

    m_control.OnColumnsInserted();

    return 0;
}

IMPLEMENT_DYNCREATE(CFileBackupView, CControlView)

BEGIN_MESSAGE_MAP(CFileBackupView, CControlView)
    ON_WM_CREATE()
    ON_WM_SIZE()
    ON_WM_ERASEBKGND()
    ON_WM_CTLCOLOR()
    ON_EN_CHANGE(IDC_BACKUP_SEARCH,            &CFileBackupView::OnSearchChange)
    ON_BN_CLICKED(IDC_BACKUP_FILTER_BACKUPONLY, &CFileBackupView::OnChkBackupOnly)
    ON_BN_CLICKED(IDC_BACKUP_FILTER_UNBACKED,   &CFileBackupView::OnChkUnbacked)
    ON_BN_CLICKED(IDC_BACKUP_FILTER_MODIFIED,   &CFileBackupView::OnChkModified)
    ON_BN_CLICKED(IDC_BACKUP_VIEW_TOGGLE,       &CFileBackupView::OnChkListView)
    ON_BN_CLICKED(IDC_BACKUP_SYNC_SCAN,         &CFileBackupView::OnBtnSyncScan)
    // Always-disabled: scan/rescan/search/filter/permanent-delete are main-tree-only.
    ON_UPDATE_COMMAND_UI(ID_SCAN_RESUME,             &CFileBackupView::OnUpdateDisableInBackup)
    ON_UPDATE_COMMAND_UI(ID_SCAN_SUSPEND,            &CFileBackupView::OnUpdateDisableInBackup)
    ON_UPDATE_COMMAND_UI(ID_SCAN_STOP,               &CFileBackupView::OnUpdateDisableInBackup)
    ON_UPDATE_COMMAND_UI(ID_REFRESH_ALL,             &CFileBackupView::OnUpdateDisableInBackup)
    ON_UPDATE_COMMAND_UI(ID_REFRESH_SELECTED,        &CFileBackupView::OnUpdateDisableInBackup)
    ON_UPDATE_COMMAND_UI(ID_SEARCH,                  &CFileBackupView::OnUpdateDisableInBackup)
    ON_UPDATE_COMMAND_UI(ID_FILTER,                  &CFileBackupView::OnUpdateDisableInBackup)
    ON_UPDATE_COMMAND_UI(ID_CLEANUP_DELETE,          &CFileBackupView::OnUpdateDisableInBackup)
    // Conditionally enabled when Unbacked (on-disk) items are selected.
    ON_UPDATE_COMMAND_UI(ID_CLEANUP_OPEN_SELECTED,   &CFileBackupView::OnUpdateFileOpOnUnbacked)
    ON_UPDATE_COMMAND_UI(ID_CLEANUP_EXPLORER_SELECT, &CFileBackupView::OnUpdateFileOpOnUnbacked)
    ON_UPDATE_COMMAND_UI(ID_EDIT_COPY_CLIPBOARD,     &CFileBackupView::OnUpdateFileOpOnUnbacked)
    ON_UPDATE_COMMAND_UI(ID_CLEANUP_OPEN_IN_CONSOLE, &CFileBackupView::OnUpdateFileOpOnUnbacked)
    ON_UPDATE_COMMAND_UI(ID_CLEANUP_PROPERTIES,      &CFileBackupView::OnUpdateFileOpOnUnbacked)
    ON_UPDATE_COMMAND_UI(ID_CLEANUP_DELETE_BIN,      &CFileBackupView::OnUpdateFileOpOnUnbacked)
    ON_COMMAND(ID_CLEANUP_OPEN_SELECTED,             &CFileBackupView::OnCleanupOpenSelected)
    ON_COMMAND(ID_CLEANUP_EXPLORER_SELECT,           &CFileBackupView::OnCleanupExplorerSelect)
    ON_COMMAND(ID_EDIT_COPY_CLIPBOARD,               &CFileBackupView::OnEditCopyClipboard)
    ON_COMMAND(ID_CLEANUP_OPEN_IN_CONSOLE,           &CFileBackupView::OnCleanupOpenInConsole)
    ON_COMMAND(ID_CLEANUP_PROPERTIES,                &CFileBackupView::OnCleanupProperties)
    ON_COMMAND(ID_CLEANUP_DELETE_BIN,                &CFileBackupView::OnCleanupDeleteBin)
END_MESSAGE_MAP()

int CFileBackupView::OnCreate(const LPCREATESTRUCT lpCreateStruct)
{
    if (CControlView::OnCreate(lpCreateStruct) == -1) return -1;

    // Create search box.
    m_searchEdit.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        CRect(0, 0, 1, 1), this, IDC_BACKUP_SEARCH);

    // Create filter checkboxes.
    m_chkBackupOnly.Create(L"Backup only", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        CRect(0, 0, 1, 1), this, IDC_BACKUP_FILTER_BACKUPONLY);

    m_chkUnbacked.Create(L"Not backed up", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        CRect(0, 0, 1, 1), this, IDC_BACKUP_FILTER_UNBACKED);

    m_chkModified.Create(L"Modified", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        CRect(0, 0, 1, 1), this, IDC_BACKUP_FILTER_MODIFIED);

    m_chkListView.Create(L"List view", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        CRect(0, 0, 1, 1), this, IDC_BACKUP_VIEW_TOGGLE);

    m_btnSyncScan.Create(L"Sync Scan", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        CRect(0, 0, 1, 1), this, IDC_BACKUP_SYNC_SCAN);

    // Apply dark-mode theming to the strip controls
    DarkMode::AdjustControls(GetSafeHwnd());

    // Create tree list control.
    constexpr RECT rect = {0, 0, 0, 0};
    m_control.CreateExtended(LVS_EX_HEADERDRAGDROP,
        LVS_OWNERDATA | WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
        rect, this, ID_WDS_CONTROL);
    m_control.ShowGrid(COptions::ListGrid);
    m_control.ShowStripes(COptions::ListStripes);
    m_control.ShowFullRowSelection(COptions::ListFullRowSelection);

    InsertCol(IDS_COL_NAME,          LVCFMT_LEFT,  500, COL_ITEMBAK_NAME);
    InsertCol(IDS_COL_BACKUP_STATUS, LVCFMT_LEFT,  150, COL_ITEMBAK_STATUS);
    InsertCol(IDS_COL_SIZE_LOGICAL,  LVCFMT_RIGHT,  90, COL_ITEMBAK_SIZE);
    InsertCol(IDS_COL_BACKED_AT,     LVCFMT_LEFT,  130, COL_ITEMBAK_BACKED_AT);
    m_control.SetSorting(COL_ITEMBAK_NAME, true);
    m_control.OnColumnsInserted();

    return 0;
}

void CFileBackupView::OnSize(UINT nType, const int cx, const int cy)
{
    CView::OnSize(nType, cx, cy);

    if (!IsWindow(m_searchEdit.m_hWnd)) return;

    // Strip layout: [Search | Backup only | Not backed up | Modified | List view | Sync Scan]
    constexpr int PAD       = 4;
    constexpr int H         = STRIP_H - 2 * PAD;
    constexpr int SRCH      = 180;
    constexpr int CHK_BACK  = 125; // "Backup only"
    constexpr int CHK_UNB   = 148; // "Not backed up" (wider + natural gap before Modified)
    constexpr int CHK_MOD   = 100; // "Modified"
    constexpr int LST       = 90;  // "List view"
    constexpr int BTN_SYNC  = 100; // "Sync Scan"

    int x = PAD;
    m_searchEdit.MoveWindow(x, PAD, SRCH, H);
    x += SRCH + PAD;
    m_chkBackupOnly.MoveWindow(x, PAD, CHK_BACK, H);
    x += CHK_BACK + PAD;
    m_chkUnbacked.MoveWindow(x, PAD, CHK_UNB, H);
    x += CHK_UNB + PAD;
    m_chkModified.MoveWindow(x, PAD, CHK_MOD, H);
    x += CHK_MOD + PAD;
    m_chkListView.MoveWindow(x, PAD, LST, H);
    x += LST + PAD * 3; // wider gap — Sync Scan is a distinct action, not a filter
    m_btnSyncScan.MoveWindow(x, PAD, BTN_SYNC, H);

    if (IsWindow(m_control.m_hWnd))
        m_control.MoveWindow(0, STRIP_H, cx, std::max(0, cy - STRIP_H));
}

BOOL CFileBackupView::OnEraseBkgnd(CDC* pDC)
{
    // Fill the strip area with the app's button-face color (dark or light).
    // The base CControlView::OnEraseBkgnd returns TRUE without drawing,
    // which leaves the strip dirty and garbles the control labels.
    CRect rc;
    GetClientRect(&rc);
    rc.bottom = std::min(rc.bottom, static_cast<LONG>(STRIP_H));
    pDC->FillSolidRect(rc, DarkMode::WdsSysColor(COLOR_BTNFACE));
    return TRUE;
}

HBRUSH CFileBackupView::OnCtlColor(CDC* pDC, CWnd* pWnd, const UINT nCtlColor)
{
    // Let the dark-mode layer handle edit/static controls in the strip.
    if (const HBRUSH br = DarkMode::OnCtlColor(pDC, nCtlColor))
        return br;

    // For checkboxes (CTLCOLOR_BTN): transparent text on the strip background.
    if (nCtlColor == CTLCOLOR_BTN)
    {
        pDC->SetBkMode(TRANSPARENT);
        pDC->SetTextColor(DarkMode::WdsSysColor(COLOR_BTNTEXT));
        return ::GetSysColorBrush(COLOR_BTNFACE); // unused by themed buttons, needed for legacy
    }

    return CView::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CFileBackupView::ApplyFilter()
{
    const bool backupOnly = (m_chkBackupOnly.GetCheck() == BST_CHECKED);
    const bool unbacked   = (m_chkUnbacked.GetCheck()   == BST_CHECKED);
    const bool modified   = (m_chkModified.GetCheck()   == BST_CHECKED);
    const bool listMode   = (m_chkListView.GetCheck()   == BST_CHECKED);

    BackupFilter filter = BackupFilter::All;
    if (backupOnly) filter = BackupFilter::BackupOnly;
    else if (unbacked) filter = BackupFilter::Unbacked;
    else if (modified) filter = BackupFilter::Modified;

    CString searchText;
    m_searchEdit.GetWindowText(searchText);

    m_control.SetFlatMode(listMode);
    m_control.Rebuild(filter, searchText.GetString());
}

void CFileBackupView::OnSearchChange()
{
    ApplyFilter();
}

void CFileBackupView::OnChkBackupOnly()
{
    if (m_chkBackupOnly.GetCheck() == BST_CHECKED)
    {
        m_chkUnbacked.SetCheck(BST_UNCHECKED);
        m_chkModified.SetCheck(BST_UNCHECKED);
    }
    ApplyFilter();
}

void CFileBackupView::OnChkUnbacked()
{
    if (m_chkUnbacked.GetCheck() == BST_CHECKED)
    {
        m_chkBackupOnly.SetCheck(BST_UNCHECKED);
        m_chkModified.SetCheck(BST_UNCHECKED);
    }
    ApplyFilter();
}

void CFileBackupView::OnChkModified()
{
    if (m_chkModified.GetCheck() == BST_CHECKED)
    {
        m_chkBackupOnly.SetCheck(BST_UNCHECKED);
        m_chkUnbacked.SetCheck(BST_UNCHECKED);
    }
    ApplyFilter();
}

void CFileBackupView::OnChkListView()
{
    ApplyFilter();
}

void CFileBackupView::OnUpdateDisableInBackup(CCmdUI* pCmdUI)
{
    pCmdUI->Enable(FALSE);
}

void CFileBackupView::OnUpdateFileOpOnUnbacked(CCmdUI* pCmdUI)
{
    const CFileBackupControl* ctrl = CFileBackupControl::Get();
    pCmdUI->Enable(ctrl && ctrl->HasSelectedUnbackedItems());
}

void CFileBackupView::OnCleanupOpenSelected()
{
    const CFileBackupControl* ctrl = CFileBackupControl::Get();
    if (!ctrl) return;
    for (const auto& p : ctrl->GetSelectedUnbackedPaths())
        ShellExecuteWrapper(p, L"", L"open", ctrl->GetSafeHwnd());
}

void CFileBackupView::OnCleanupExplorerSelect()
{
    const CFileBackupControl* ctrl = CFileBackupControl::Get();
    if (!ctrl) return;
    for (const auto& p : ctrl->GetSelectedUnbackedPaths())
        ShellExecuteWrapper(L"explorer.exe", L"/select,\"" + p + L"\"", L"open", ctrl->GetSafeHwnd());
}

void CFileBackupView::OnEditCopyClipboard()
{
    const CFileBackupControl* ctrl = CFileBackupControl::Get();
    if (!ctrl) return;
    const auto paths = ctrl->GetSelectedUnbackedPaths();
    std::wstring text;
    for (const auto& p : paths) { text += p; text += L"\r\n"; }
    if (!text.empty()) text.resize(text.size() - 2);
    if (OpenClipboard())
    {
        EmptyClipboard();
        const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
        if (const HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            wmemcpy(static_cast<wchar_t*>(GlobalLock(hMem)), text.c_str(), text.size() + 1);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

void CFileBackupView::OnCleanupOpenInConsole()
{
    const CFileBackupControl* ctrl = CFileBackupControl::Get();
    if (!ctrl) return;
    std::unordered_set<std::wstring> seen;
    for (const auto& p : ctrl->GetSelectedUnbackedPaths())
    {
        std::error_code ec;
        const std::wstring dir = std::filesystem::is_directory(p, ec)
            ? p : std::filesystem::path(p).parent_path().wstring();
        if (seen.insert(dir).second)
            ShellExecuteWrapper(L"cmd.exe",
                L"/K cd /d \"" + dir + L"\"", L"open", ctrl->GetSafeHwnd(), dir);
    }
}

void CFileBackupView::OnCleanupProperties()
{
    const CFileBackupControl* ctrl = CFileBackupControl::Get();
    if (!ctrl) return;
    for (const auto& p : ctrl->GetSelectedUnbackedPaths())
    {
        PIDLIST_ABSOLUTE pidl = ILCreateFromPath(p.c_str());
        if (!pidl) continue;
        SHELLEXECUTEINFO sei{};
        sei.cbSize   = sizeof(sei);
        sei.hwnd     = ctrl->GetSafeHwnd();
        sei.lpVerb   = L"properties";
        sei.fMask    = SEE_MASK_INVOKEIDLIST | SEE_MASK_IDLIST | SEE_MASK_NOZONECHECKS;
        sei.lpIDList = pidl;
        sei.nShow    = SW_SHOWNORMAL;
        ShellExecuteEx(&sei);
        ILFree(pidl);
    }
}

void CFileBackupView::OnCleanupDeleteBin()
{
    CFileBackupControl* ctrl = CFileBackupControl::Get();
    if (!ctrl) return;
    const auto paths = ctrl->GetSelectedUnbackedPaths();
    if (paths.empty()) return;
    std::wstring pathList;
    for (const auto& p : paths) { pathList += p; pathList += L'\0'; }
    pathList += L'\0';
    SHFILEOPSTRUCTW op{};
    op.hwnd   = ctrl->GetSafeHwnd();
    op.wFunc  = FO_DELETE;
    op.pFrom  = pathList.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_WANTNUKEWARNING;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted)
        ApplyFilter(); // rebuild view to reflect the now-absent files
}

void CFileBackupView::OnBtnSyncScan()
{
    const auto& folders = backup::SourceFolders.Obj();
    if (folders.empty())
    {
        AfxMessageBox(L"No backup source folders are configured.", MB_ICONINFORMATION);
        return;
    }

    // Build newline-delimited string for FilteringIncludeDirs (same format PageFiltering uses).
    std::wstring includeStr;
    for (const auto& f : folders)
    {
        if (!includeStr.empty()) includeStr += L'\n';
        includeStr += f;
    }
    COptions::FilteringIncludeDirs.Obj() = includeStr;
    COptions::FilteringIncludeDirs.WritePersistedProperty();

    // Prepend each source folder to the SelectDrivesFolder MRU so they appear
    // at the top of the folder combo in the scan dialog.
    auto& mru = COptions::SelectDrivesFolder.Obj();
    for (auto it = folders.rbegin(); it != folders.rend(); ++it)
    {
        std::erase_if(mru, [&it](const std::wstring& s)
            { return _wcsicmp(s.c_str(), it->c_str()) == 0; });
        mru.insert(mru.begin(), *it);
    }
    const auto maxHist = static_cast<std::size_t>(COptions::FolderHistoryCount.Obj());
    if (mru.size() > maxHist) mru.resize(maxHist);
    COptions::SelectDrivesFolder.WritePersistedProperty();

    // Apply the updated include filter to the running scan immediately.
    CFiltering::CompileFilters();

    AfxMessageBox(
        L"Backup source directories have been applied to Filtering.\n\n"
        L"Rescan the directory tree to display only the directories selected for backup.",
        MB_ICONINFORMATION);
}

void CFileBackupView::OnUpdate(CView* /*pSender*/, const LPARAM lHint, CObject* /*pHint*/)
{
    // On a new root (e.g. "Scan Backup Sources"), rebuild from scratch.
    // On generic updates, only populate if the tree is currently empty.
    if (lHint == HINT_NEWROOT)
        ApplyFilter();
    else if (lHint == 0 && m_control.GetItemCount() == 0)
        ApplyFilter();
}
