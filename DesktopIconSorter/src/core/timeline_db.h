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

// Get the most recent snapshot summary (without loading full data)
bool Timeline_GetLatest(TimelineSummary* out);

// Build the on-disk checkpoint ID for a user-supplied name:
// "checkpoint_" + name with filesystem-unsafe chars replaced. This is the
// ID that Timeline_Save writes and Timeline_Load/Timeline_Delete accept.
void Timeline_MakeCheckpointId(const wchar_t* name, wchar_t* out, size_t outSize);
