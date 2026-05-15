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
#include "BackupStore.h"

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CBackupStore::CBackupStore(std::wstring backupRoot)
    : m_backupRoot(std::move(backupRoot))
{
    // Normalize separator to backslash for Windows API compatibility.
    std::ranges::replace(m_backupRoot, L'/', L'\\');
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

bool CBackupStore::Initialize() const
{
    std::error_code ec;
    std::filesystem::create_directories(GetObjectsDir(), ec);
    if (ec)
    {
        TRACE(L"BackupStore: cannot create objects directory '%s': %S\n",
              GetObjectsDir().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::wstring CBackupStore::GetObjectsDir() const
{
    return m_backupRoot + L"\\objects";
}

std::wstring CBackupStore::GetObjectPath(const std::wstring& hash) const
{
    ASSERT(hash.size() >= 3);
    return GetObjectsDir() + L'\\' + hash.substr(0, 2) + L'\\' + hash.substr(2);
}

// ---------------------------------------------------------------------------
// Object queries
// ---------------------------------------------------------------------------

bool CBackupStore::HasObject(const std::wstring& hash) const
{
    std::error_code ec;
    return std::filesystem::exists(GetObjectPath(hash), ec);
}

// ---------------------------------------------------------------------------
// Object mutations
// ---------------------------------------------------------------------------

bool CBackupStore::StoreObject(const std::wstring& srcPath, const std::wstring& hash) const
{
    const std::wstring dest = GetObjectPath(hash);

    // Fast path: object already present, nothing to do.
    std::error_code ec;
    if (std::filesystem::exists(dest, ec)) return false;

    // Ensure the two-character shard directory exists.
    const std::wstring shardDir = dest.substr(0, dest.rfind(L'\\'));
    std::filesystem::create_directories(shardDir, ec);
    if (ec)
    {
        TRACE(L"BackupStore: cannot create shard directory '%s': %S\n",
              shardDir.c_str(), ec.message().c_str());
        return false;
    }

    // CopyFileW with bFailIfExists=TRUE — races that create the object between
    // our existence check and the copy are handled gracefully: the copy fails
    // with ERROR_FILE_EXISTS, which we treat as a non-error (already stored).
    if (!CopyFileW(srcPath.c_str(), dest.c_str(), TRUE))
    {
        const DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS) return false;

        TRACE(L"BackupStore: CopyFileW('%s' -> '%s') failed, error %lu\n",
              srcPath.c_str(), dest.c_str(), err);
        return false;
    }

    // Make the stored object read-only to guard against accidental modification.
    SetFileAttributesW(dest.c_str(), FILE_ATTRIBUTE_READONLY);

    TRACE(L"BackupStore: stored %s <- %s\n", hash.substr(0, 16).c_str(), srcPath.c_str());
    return true;
}

bool CBackupStore::RetrieveObject(const std::wstring& hash, const std::wstring& destPath) const
{
    const std::wstring src = GetObjectPath(hash);

    std::error_code ec;
    if (!std::filesystem::exists(src, ec))
    {
        TRACE(L"BackupStore: object not found for hash %s\n", hash.substr(0, 16).c_str());
        return false;
    }

    // Ensure destination directory exists.
    const auto destDir = std::filesystem::path(destPath).parent_path();
    std::filesystem::create_directories(destDir, ec);
    if (ec)
    {
        TRACE(L"BackupStore: cannot create destination directory '%s': %S\n",
              destDir.wstring().c_str(), ec.message().c_str());
        return false;
    }

    // FALSE for bFailIfExists: overwrite any partially-restored file.
    if (!CopyFileW(src.c_str(), destPath.c_str(), FALSE))
    {
        TRACE(L"BackupStore: CopyFileW restore failed for hash %s, error %lu\n",
              hash.substr(0, 16).c_str(), GetLastError());
        return false;
    }

    // Restore normal attributes so the user can edit the file.
    SetFileAttributesW(destPath.c_str(), FILE_ATTRIBUTE_NORMAL);
    return true;
}

void CBackupStore::DeleteObject(const std::wstring& hash) const
{
    const std::wstring obj = GetObjectPath(hash);

    // Clear read-only flag before deleting (objects are stored read-only).
    SetFileAttributesW(obj.c_str(), FILE_ATTRIBUTE_NORMAL);

    if (!DeleteFileW(obj.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
    {
        TRACE(L"BackupStore: DeleteFileW('%s') failed, error %lu\n",
              obj.c_str(), GetLastError());
    }
}

// ---------------------------------------------------------------------------
// SHA-256 computation
//
// Unlike CItem::GetFileHash(), which truncates the digest to 16 bytes for
// fast duplicate detection, this function always returns the full 32-byte
// (64 hex character) digest required for content-addressed object identity.
// ---------------------------------------------------------------------------

std::wstring CBackupStore::ComputeFileSha256(const std::wstring& filePath)
{
    // Open the file with sequential-scan and backup-semantics hints.
    const SmartPointer<HANDLE> hFile(CloseHandle,
        CreateFileW(filePath.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS,
                    nullptr));

    if (!hFile.IsValid())
    {
        TRACE(L"BackupStore: cannot open '%s' for hashing, error %lu\n",
              filePath.c_str(), GetLastError());
        return {};
    }

    // Open a BCrypt SHA-256 provider.
    BCRYPT_ALG_HANDLE hAlgRaw = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &hAlgRaw, BCRYPT_SHA256_ALGORITHM, MS_PRIMITIVE_PROVIDER, 0)))
    {
        TRACE(L"BackupStore: BCryptOpenAlgorithmProvider failed\n");
        return {};
    }
    const SmartPointer<BCRYPT_ALG_HANDLE> hAlg(
        [](BCRYPT_ALG_HANDLE h) { BCryptCloseAlgorithmProvider(h, 0); }, hAlgRaw);

    // Query the size of the hash object buffer.
    DWORD hashObjSize = 0;
    DWORD cbResult    = 0;
    BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH,
                      reinterpret_cast<PBYTE>(&hashObjSize),
                      sizeof(DWORD), &cbResult, 0);

    // Create the hash object.
    std::vector<BYTE> hashObj(hashObjSize);
    BCRYPT_HASH_HANDLE hHashRaw = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(
            hAlg, &hHashRaw, hashObj.data(), hashObjSize, nullptr, 0, 0)))
    {
        TRACE(L"BackupStore: BCryptCreateHash failed\n");
        return {};
    }
    const SmartPointer<BCRYPT_HASH_HANDLE> hHash(BCryptDestroyHash, hHashRaw);

    // Feed the file to the hash in 1 MiB chunks.
    constexpr DWORD kChunkSize = 1u * 1024u * 1024u;
    std::vector<BYTE> buf(kChunkSize);
    DWORD bytesRead = 0;

    while (ReadFile(hFile, buf.data(), kChunkSize, &bytesRead, nullptr) && bytesRead > 0)
    {
        if (!BCRYPT_SUCCESS(BCryptHashData(hHash, buf.data(), bytesRead, 0)))
        {
            TRACE(L"BackupStore: BCryptHashData failed\n");
            return {};
        }
    }

    // Finalise — SHA-256 digest is always exactly 32 bytes.
    std::array<BYTE, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptFinishHash(
            hHash, digest.data(), static_cast<ULONG>(digest.size()), 0)))
    {
        TRACE(L"BackupStore: BCryptFinishHash failed\n");
        return {};
    }

    // Format as 64 lowercase hex characters.
    std::wstring hex;
    hex.reserve(64);
    for (const BYTE b : digest)
        hex += std::format(L"{:02x}", static_cast<unsigned>(b));

    return hex;
}
