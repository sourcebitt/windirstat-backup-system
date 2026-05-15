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

#pragma once

#include "pch.h"
#include "TreeListControl.h"
#include "BackupEngine.h"

using ITEMBAKCOLUMNS = enum : std::uint8_t
{
    COL_ITEMBAK_NAME = 0,
    COL_ITEMBAK_STATUS,
    COL_ITEMBAK_SIZE,
    COL_ITEMBAK_BACKED_AT,
};

class CBackupItem final : public CTreeListItem
{
public:
    CBackupItem(std::wstring path, BackupFileStatus status, ULONGLONG size, std::wstring backedAt);
    ~CBackupItem() override = default;

    std::wstring GetText(int subitem) const override;
    int CompareSibling(const CTreeListItem* other, int subitem) const override;
    CTreeListItem* GetTreeListChild(int) const override { return nullptr; }
    int GetTreeListChildCount() const override { return 0; }
    CItem* GetLinkedItem() noexcept override { return nullptr; }
    HICON GetIcon() override { return m_visualInfo ? m_visualInfo->icon : nullptr; }

private:
    std::wstring     m_path;
    BackupFileStatus m_status;
    ULONGLONG        m_size;
    std::wstring     m_backedAt;

    static std::wstring StatusString(BackupFileStatus s);
};

class CFileBackupControl final : public CTreeListControl
{
public:
    CFileBackupControl();
    ~CFileBackupControl() override;

    static CFileBackupControl* Get() { return m_singleton; }

    void Populate();
    void RunBackupPass();

protected:
    static CFileBackupControl* m_singleton;
    std::unique_ptr<CBackupEngine> m_engine;

    bool EnsureEngine();
    std::wstring BrowseForFolder(const std::wstring& title);

    DECLARE_MESSAGE_MAP()
    afx_msg void OnDestroy();
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint pt);
};
