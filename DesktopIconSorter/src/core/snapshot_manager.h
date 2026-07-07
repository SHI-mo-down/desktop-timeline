// snapshot_manager.h : Multi-snapshot layout management + undo stack
#pragma once
#include <Windows.h>
#include <vector>
#include <wchar.h>

struct SnapshotInfo {
    wchar_t name[128];          // User-assigned name
    wchar_t fileName[256];      // Actual file on disk
    wchar_t timestamp[32];      // "2026-06-07 16:30:00"
    int iconCount;              // Number of icons in this snapshot
};

struct IconPosition {
    wchar_t name[MAX_PATH];
    POINT point;
};

// Initialize snapshot system (creates snapshots directory)
bool Snapshot_Init(const wchar_t* baseDir);

// Save current desktop layout as a named snapshot
// Returns true on success
bool Snapshot_Save(const wchar_t* name);

// Auto-save: saves previous layout with auto-generated name (timestamp)
// Call BEFORE applying a sort or when change detected
bool Snapshot_AutoSave();

// Restore desktop icons from a snapshot file
bool Snapshot_Restore(const wchar_t* fileName);

// Restore the most recent snapshot (undo)
bool Snapshot_Undo();

// List all snapshots, sorted by time (newest first)
std::vector<SnapshotInfo> Snapshot_List();

// Delete a snapshot by file name
bool Snapshot_Delete(const wchar_t* fileName);

// Get the most recent snapshot name (for display)
bool Snapshot_GetLatest(SnapshotInfo* out);

// Create a backup before a potential layout change
// Used by polling auto-detect
void Snapshot_BackupBeforeChange();
