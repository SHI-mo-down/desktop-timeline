// cleanup_policy.h : Automatic timeline cleanup and storage management
#pragma once
#include <Windows.h>
#include <string>

struct CleanupConfig {
    int maxRecords;          // 500
    int dailyMergeDays;      // 7
    bool enabled;            // true
};

struct StorageStats {
    int totalRecords;
    int autoRecordCount;
    int checkpointCount;
    unsigned long long totalSizeBytes;
    std::wstring oldestRecord;
    std::wstring newestRecord;
};

int Cleanup_Execute(const wchar_t* baseDir, const CleanupConfig& cfg);
StorageStats Cleanup_GetStats(const wchar_t* baseDir);
