// cleanup_policy.cpp : Automatic timeline cleanup
#include "cleanup_policy.h"
#include "timeline_db.h"
#include <shlwapi.h>
#include <stdio.h>

int Cleanup_Execute(const wchar_t* baseDir, const CleanupConfig& cfg) {
    if (!cfg.enabled) return 0;
    int removed = 0;
    Timeline_Init(baseDir);
    auto list = Timeline_List(0);
    if (list.empty()) return 0;

    // Enforce maxRecords limit — delete oldest non-checkpoint records
    if (cfg.maxRecords > 0 && (int)list.size() > cfg.maxRecords) {
        int toDelete = (int)list.size() - cfg.maxRecords;
        for (int i = (int)list.size() - 1; i >= 0 && toDelete > 0; i--) {
            if (!list[i].isCheckpoint && Timeline_Delete(list[i].id.c_str())) {
                removed++; toDelete--;
            }
        }
    }

    return removed;
}

StorageStats Cleanup_GetStats(const wchar_t* baseDir) {
    StorageStats stats = {};
    Timeline_Init(baseDir);
    auto list = Timeline_List(0);
    stats.totalRecords = (int)list.size();
    for (const auto& s : list) {
        if (s.isCheckpoint) stats.checkpointCount++;
        else stats.autoRecordCount++;
    }
    if (!list.empty()) {
        stats.newestRecord = list.front().timestamp;
        stats.oldestRecord = list.back().timestamp;
    }
    return stats;
}
