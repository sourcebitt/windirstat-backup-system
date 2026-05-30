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
#include "BackupManifest.h"
#include "BackupStore.h"

// Statistics returned by a single backup pass.
struct BackupPassResult
{
    std::size_t filesNew       = 0; // first-time backup of this path
    std::size_t filesModified  = 0; // content changed since last backup
    std::size_t filesRelocated = 0; // path is new but content already stored (moved/copied)
    std::size_t filesUnchanged = 0; // mtime+size unchanged — skipped without re-hashing
    std::size_t filesMissing   = 0; // in manifest but absent from all source folders
    std::size_t objectsStored  = 0; // new content objects written to the store
    std::size_t errors         = 0; // I/O or hashing failures
};

//
// CBackupEngine — Incremental backup orchestrator.
//
// Owns a CBackupStore and a CBackupManifest and coordinates one backup pass
// across a configured list of source folders, porting the logic from
// sync_core.py (run_backup_pass / results_from_manifest).
//
// Typical call sequence:
//   CBackupEngine engine(backupRoot, { L"C:\\Users\\me\\Documents", ... });
//   engine.Initialize();           // creates store dir + loads manifest
//   BackupPassResult r = engine.RunPass();
//   engine.Save();                 // persists updated manifest
//
class CBackupEngine final
{
public:

    explicit CBackupEngine(std::wstring backupRoot, std::vector<std::wstring> sourceFolders);

    CBackupEngine(const CBackupEngine&) = delete;
    CBackupEngine(CBackupEngine&&) = delete;
    CBackupEngine& operator=(const CBackupEngine&) = delete;
    CBackupEngine& operator=(CBackupEngine&&) = delete;

    // ── Lifecycle ─────────────────────────────────────────────────────────────

    // Create the object store directory and load the manifest from disk.
    // A missing manifest is treated as an empty one (first-run scenario).
    // Returns false on store-initialization or manifest-parse failure.
    [[nodiscard]] bool Initialize();

    // Atomically save the manifest to disk.
    [[nodiscard]] bool Save() const;

    // ── Backup pass ───────────────────────────────────────────────────────────

    // Run one incremental backup pass over all configured source folders.
    //
    // For each regular file found:
    //   • mtime + size unchanged  →  skip (filesUnchanged)
    //   • path not in manifest    →  hash, store, record as "new" or "relocated"
    //   • path in manifest,
    //     content unchanged       →  skip (filesUnchanged, metadata drift only)
    //   • path in manifest,
    //     content changed         →  hash, store, record as "modified"
    //
    // After scanning, manifest entries whose paths were not visited are counted
    // as missing (filesMissing); they are NOT removed from the manifest here —
    // use Purge() / purge_manifest logic for that.
    [[nodiscard]] BackupPassResult RunPass();

    // ── Classification ────────────────────────────────────────────────────────

    // Classify every manifest entry against the current filesystem state.
    // For each normalized path in the manifest, returns one of:
    //   BackedUnique    — present on disk; hash referenced by only this path
    //   BackedDuplicate — present on disk; hash shared with at least one other path
    //   MissingSurvived — absent from disk; same hash found at another manifest path
    //   MissingDeleted  — absent from disk; no other manifest path shares the hash
    //
    // Files present on disk but absent from the manifest (Unbacked) are not
    // included here — they are only visible during a RunPass() scan.
    [[nodiscard]] std::unordered_map<std::wstring, BackupFileStatus> ClassifyAll() const;

    // ── Accessors ─────────────────────────────────────────────────────────────

    [[nodiscard]] const CBackupManifest& GetManifest() const noexcept { return m_manifest; }
    [[nodiscard]]       CBackupManifest& GetManifest()       noexcept { return m_manifest; }
    [[nodiscard]] const CBackupStore&    GetStore()    const noexcept { return m_store; }
    [[nodiscard]] const std::vector<std::wstring>& GetSourceFolders() const noexcept { return m_sourceFolders; }

private:

    CBackupStore              m_store;
    CBackupManifest           m_manifest;
    std::vector<std::wstring> m_sourceFolders;

    [[nodiscard]] std::wstring ManifestPath() const;

    // Convert a filesystem file_time_type to seconds since the Unix epoch (1970-01-01).
    [[nodiscard]] static double FileTimeToUnix(std::filesystem::file_time_type t) noexcept;
};
