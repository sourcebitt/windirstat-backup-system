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

// A single point-in-time version snapshot stored in the backup.
struct BackupFileVersion
{
    std::wstring hash;        // Full SHA-256 hex (64 lowercase chars)
    std::wstring backedUpAt;  // ISO-8601 UTC timestamp, e.g. "2024-01-01T12:00:00"
    ULONGLONG    size  = 0;
    double       mtime = 0.0; // Unix epoch seconds (st_mtime from stat)
    std::wstring status;      // "new" | "modified" | "relocated"
};

// Complete backup record for a single normalized path.
struct BackupFileEntry
{
    std::wstring                    currentHash; // SHA-256 hex of the current object
    std::vector<BackupFileVersion>  versions;    // All version snapshots, oldest first
};

// How a path relates to the backup when cross-referenced with the live filesystem.
enum class BackupFileStatus : std::uint8_t
{
    BackedUnique,    // In manifest; this hash is unique across all manifest entries
    BackedDuplicate, // In manifest; same hash referenced by at least one other path
    Unbacked,        // Present on disk but not tracked in the manifest
    MissingSurvived, // In manifest, gone from disk; same hash found at another live path
    MissingDeleted,  // In manifest, gone from disk; no surviving copy found on disk
};

//
// CBackupManifest — Loads, queries, and persists the JSON backup manifest.
//
// The on-disk format (version 2) is byte-compatible with the Python sync_backup.py
// manifest so existing backups can be opened immediately without migration.
//
// All public methods that mutate state acquire an exclusive lock; read-only methods
// acquire a shared lock so concurrent readers can proceed without blocking each other.
//
class CBackupManifest final
{
public:

    static constexpr int MANIFEST_VERSION = 2;

    CBackupManifest() = default;

    CBackupManifest(const CBackupManifest&) = delete;
    CBackupManifest(CBackupManifest&&) = delete;
    CBackupManifest& operator=(const CBackupManifest&) = delete;
    CBackupManifest& operator=(CBackupManifest&&) = delete;

    // ── Persistence ───────────────────────────────────────────────────────────

    // Load the manifest from disk.  A missing file is not an error (first-run
    // scenario) — the manifest starts empty.  Returns false on parse failure.
    [[nodiscard]] bool Load(const std::wstring& manifestPath);

    // Atomically serialize to disk (write .tmp, then replace the target file).
    [[nodiscard]] bool Save(const std::wstring& manifestPath) const;

    // ── Queries ───────────────────────────────────────────────────────────────

    [[nodiscard]] bool                    Contains(const std::wstring& normPath) const;
    [[nodiscard]] const BackupFileEntry*  Find(const std::wstring& normPath) const;
    [[nodiscard]] BackupFileEntry*        Find(const std::wstring& normPath);
    [[nodiscard]] std::size_t             GetFileCount() const;

    // Build an inverted index: SHA-256 → list of all normalized paths sharing that hash.
    // Useful for duplicate detection and "missing-survived" classification.
    [[nodiscard]] std::unordered_map<std::wstring, std::vector<std::wstring>>
        BuildHashIndex() const;

    // ── Mutations ─────────────────────────────────────────────────────────────

    // Record a backup event for normPath.
    //   status  — must be one of "new", "modified", or "relocated"
    //   hash    — full SHA-256 hex string (64 chars)
    //   size    — file size in bytes
    //   mtime   — modification time as Unix epoch (seconds, floating-point)
    //
    // If normPath already exists in the manifest the new version is appended and
    // currentHash is updated; otherwise a fresh entry is created.
    void AddOrUpdate(const std::wstring& normPath, const std::wstring& hash,
                     const std::wstring& status, ULONGLONG size, double mtime);

    // Remove a path from the manifest.
    // If the removed entry's hash has no other references outOrphanedHash receives
    // that hash (the caller should delete the corresponding object from the store).
    // Returns false when normPath is not present.
    [[nodiscard]] bool Purge(const std::wstring& normPath, std::wstring& outOrphanedHash);

    // ── Iteration ─────────────────────────────────────────────────────────────

    using FileMap = std::unordered_map<std::wstring, BackupFileEntry>;

    // Direct access to the underlying map.  Callers are responsible for holding
    // any necessary locks if they intend to use the reference across threads.
    [[nodiscard]] const FileMap& GetFiles() const noexcept { return m_files; }

    // ── Path normalization ─────────────────────────────────────────────────────

    // Resolve path to absolute form and replace every back-slash with a
    // forward-slash, matching sync_core.normalize_path() in the Python system.
    [[nodiscard]] static std::wstring NormalizePath(const std::wstring& path);

    // ── Utility ───────────────────────────────────────────────────────────────

    // Current UTC time formatted as an ISO-8601 string ("YYYY-MM-DDTHH:MM:SS").
    [[nodiscard]] static std::wstring CurrentIsoTimestamp();

private:

    FileMap                   m_files;
    mutable std::shared_mutex m_mutex;

    // UTF-8 ↔ wide-string conversion (Windows API backed)
    [[nodiscard]] static std::string  WideToUtf8(std::wstring_view s);
    [[nodiscard]] static std::wstring Utf8ToWide(std::string_view s);

    // JSON round-trip (implemented in the .cpp translation unit)
    [[nodiscard]] bool        ParseJson(std::string_view json);
    [[nodiscard]] std::string SerializeJson() const;
};
