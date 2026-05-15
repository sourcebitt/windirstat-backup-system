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

//
// CBackupStore — Content-addressed object store for the backup system.
//
// Files are stored under:
//   {backupRoot}\objects\{hash[0..1]}\{hash[2..]}
//
// This two-level sharding (matching Git's object layout and the Python
// sync_backup.py store) keeps individual directories from growing too large
// while keeping the lookup trivially O(1) given a full SHA-256 hex string.
//
// The layout is byte-compatible with existing Python sync_backup.py backups,
// so any backup produced by the Python tool can be opened directly.
//
class CBackupStore final
{
public:

    explicit CBackupStore(std::wstring backupRoot);

    CBackupStore(const CBackupStore&) = delete;
    CBackupStore(CBackupStore&&) = delete;
    CBackupStore& operator=(const CBackupStore&) = delete;
    CBackupStore& operator=(CBackupStore&&) = delete;

    // ── Setup ─────────────────────────────────────────────────────────────────

    // Create the objects sub-directory if it does not already exist.
    // Safe to call on an existing, populated store.
    [[nodiscard]] bool Initialize() const;

    // ── Object queries ────────────────────────────────────────────────────────

    // Returns true when an object file for hash already exists in the store.
    [[nodiscard]] bool HasObject(const std::wstring& hash) const;

    // Full filesystem path of the object file for hash.
    [[nodiscard]] std::wstring GetObjectPath(const std::wstring& hash) const;

    // Root path of the objects directory ({backupRoot}\objects).
    [[nodiscard]] std::wstring GetObjectsDir() const;

    [[nodiscard]] const std::wstring& GetBackupRoot() const noexcept { return m_backupRoot; }

    // ── Object mutations ──────────────────────────────────────────────────────

    // Copy srcPath into the store under hash.
    // Returns true  when the file was freshly stored.
    // Returns false when the object already existed (no I/O performed).
    // Returns false and emits TRACE on I/O failure.
    [[nodiscard]] bool StoreObject(const std::wstring& srcPath, const std::wstring& hash) const;

    // Copy the stored object identified by hash to destPath.
    // Creates intermediate directories as needed.
    // Returns false when the object is not present in the store.
    [[nodiscard]] bool RetrieveObject(const std::wstring& hash, const std::wstring& destPath) const;

    // Delete the object file from the store.
    // Silent no-op when the object does not exist.
    void DeleteObject(const std::wstring& hash) const;

    // ── Hashing ───────────────────────────────────────────────────────────────

    // Compute the full SHA-256 digest of filePath and return it as 64 lowercase
    // hex characters.  Returns an empty string on any I/O or BCrypt failure.
    //
    // Note: CItem::GetFileHash() intentionally truncates the hash for duplicate
    // detection.  This function always returns the complete 256-bit digest that
    // the content-addressed store requires for collision-free object identity.
    [[nodiscard]] static std::wstring ComputeFileSha256(const std::wstring& filePath);

private:

    std::wstring m_backupRoot;
};
