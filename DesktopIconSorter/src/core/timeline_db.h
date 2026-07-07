// timeline_db.h : Timeline persistent storage (file-based, no SQLite dependency)
#pragma once
#include <Windows.h>
#include <vector>
#include "timeline_record.h"

// Initialize the timeline storage system
// Creates timeline/ and checkpoints/ subdirectories under baseDir
bool Timeline_Init(const wchar_t* baseDir);

// Save a snapshot to disk as .dslayout file
// Auto-generated snapshots go to timeline/ dir
// Checkpoints go to checkpoints/ dir
bool Timeline_Save(const TimelineSnapshot& snapshot);

// Load a full snapshot by ID
// Returns true and fills 'out' on success
bool Timeline_Load(const wchar_t* id, TimelineSnapshot* out);

// List all snapshots (summaries only, no icon data loaded)
// Newest first, up to maxCount (0 = unlimited)
std::vector<TimelineSummary> Timeline_List(int maxCount = 0);

// Delete a snapshot file by ID
bool Timeline_Delete(const wchar_t* id);

// Get total count of stored snapshots
int Timeline_Count();

// Get the most recent snapshot summary (without loading full data)
bool Timeline_GetLatest(TimelineSummary* out);

// Get snapshot file path for a given ID (for file operations)
bool Timeline_GetFilePath(const wchar_t* id, wchar_t* outPath, size_t outSize);

// Load only icon positions from a snapshot (fast, skips metadata parsing)
// Used by diff engine to compare layouts without loading full data
std::vector<TimelineIcon> Timeline_LoadIconsOnly(const wchar_t* id);
