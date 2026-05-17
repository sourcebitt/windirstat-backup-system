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

class CManageSourceFoldersDlg final : public CDialogEx
{
    DECLARE_DYNAMIC(CManageSourceFoldersDlg)

public:
    explicit CManageSourceFoldersDlg(CWnd* pParent = nullptr);
    ~CManageSourceFoldersDlg() override = default;

    enum { IDD = IDD_MANAGE_SOURCE_FOLDERS };

protected:
    void DoDataExchange(CDataExchange* pDX) override;
    BOOL OnInitDialog() override;
    void OnOK() override;

    std::wstring BrowseForFolder(const std::wstring& title);

    CListBox m_listBox;

    DECLARE_MESSAGE_MAP()
    afx_msg void OnBnClickedAdd();
    afx_msg void OnBnClickedRemove();
    afx_msg void OnLbnSelChangeList();
};
