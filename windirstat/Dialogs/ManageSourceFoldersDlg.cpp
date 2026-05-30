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
#include "ManageSourceFoldersDlg.h"
#include "Options.h"
#include "resource.h"

IMPLEMENT_DYNAMIC(CManageSourceFoldersDlg, CDialogEx)

CManageSourceFoldersDlg::CManageSourceFoldersDlg(CWnd* pParent)
    : CDialogEx(IDD_MANAGE_SOURCE_FOLDERS, pParent)
{
}

BEGIN_MESSAGE_MAP(CManageSourceFoldersDlg, CDialogEx)
    ON_BN_CLICKED(IDC_ADD_SOURCE_FOLDER,    &CManageSourceFoldersDlg::OnBnClickedAdd)
    ON_BN_CLICKED(IDC_REMOVE_SOURCE_FOLDER, &CManageSourceFoldersDlg::OnBnClickedRemove)
    ON_LBN_SELCHANGE(IDC_SOURCE_FOLDERS_LIST, &CManageSourceFoldersDlg::OnLbnSelChangeList)
END_MESSAGE_MAP()

void CManageSourceFoldersDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_SOURCE_FOLDERS_LIST, m_listBox);
}

BOOL CManageSourceFoldersDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    for (const auto& folder : COptions::BackupSourceFolders.Obj())
        m_listBox.AddString(folder.c_str());

    GetDlgItem(IDC_REMOVE_SOURCE_FOLDER)->EnableWindow(FALSE);
    return TRUE;
}

void CManageSourceFoldersDlg::OnOK()
{
    const int count = m_listBox.GetCount();
    std::vector<std::wstring> folders;
    folders.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        CString s;
        m_listBox.GetText(i, s);
        folders.emplace_back(s.GetString());
    }
    COptions::BackupSourceFolders = folders;
    CDialogEx::OnOK();
}

std::wstring CManageSourceFoldersDlg::BrowseForFolder(const std::wstring& title)
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
    const HWND hwndParent = GetSafeHwnd();
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

void CManageSourceFoldersDlg::OnBnClickedAdd()
{
    const std::wstring folder = BrowseForFolder(L"Select Source Folder to Back Up");
    if (folder.empty()) return;

    // Avoid duplicates
    for (int i = 0; i < m_listBox.GetCount(); ++i)
    {
        CString existing;
        m_listBox.GetText(i, existing);
        if (_wcsicmp(existing.GetString(), folder.c_str()) == 0)
            return;
    }

    m_listBox.AddString(folder.c_str());
}

void CManageSourceFoldersDlg::OnBnClickedRemove()
{
    const int sel = m_listBox.GetCurSel();
    if (sel == LB_ERR) return;

    m_listBox.DeleteString(static_cast<UINT>(sel));

    const int newCount = m_listBox.GetCount();
    if (newCount > 0)
        m_listBox.SetCurSel(sel < newCount ? sel : newCount - 1);
    else
        GetDlgItem(IDC_REMOVE_SOURCE_FOLDER)->EnableWindow(FALSE);
}

void CManageSourceFoldersDlg::OnLbnSelChangeList()
{
    GetDlgItem(IDC_REMOVE_SOURCE_FOLDER)->EnableWindow(m_listBox.GetCurSel() != LB_ERR);
}
