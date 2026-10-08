// timeline_engine.h : Core timeline engine - polling, debounce, auto-record, smart merge
#pragma once
#include <Windows.h>
#include <string>
#include "timeline_record.h"

struct LayoutRules; // forward declaration

// Engine state
enum class TimelineState {
    IDLE,           // No change detected, monitoring
    DEBOUNCING,     // Change detected, waiting for stability
    CONFIRMED       // Change confirmed after debounce window
};

// Configuration for the timeline engine
struct TimelineConfig {
    // Polling
    int pollIntervalMs;       // 2000 (2 seconds between hash checks)
    int debounceWindowMs;     // 2000 (wait 2s after last change before recording)

    // Noise filtering
    int minPixelDrift;        // 5 (ignore moves less than 5 pixels)
    int minIconChanges;       // 2 (ignore if fewer than 2 icons changed)

    // Smart merge
    float mergeThreshold;     // 0.1 (if < 10% icons changed, replace last record)
    int maxRecords;           // 500 (max total timeline records)
    int dailyMergeDays;       // 7 (merge records older than 7 days into daily)

    // Notifications
    bool notificationsEnabled;   // Master notification switch
    bool notifyOnMajorChange;    // Notify when > 5 icons move
    bool notifyOnRestore;        // Notify when layout is restored

    // Defaults
    static TimelineConfig Default() {
        TimelineConfig cfg = {};
        cfg.pollIntervalMs = 2000;
        cfg.debounceWindowMs = 2000;
        cfg.minPixelDrift = 5;
        cfg.minIconChanges = 2;
        cfg.mergeThreshold = 0.1f;
        cfg.maxRecords = 500;
        cfg.dailyMergeDays = 7;
        cfg.notificationsEnabled = true;
        cfg.notifyOnMajorChange = true;
        cfg.notifyOnRestore = true;
        return cfg;
    }
};

// Auto-save callback type - engine calls this when it decides to record
// Returns the snapshot ID if successful, empty string on failure
typedef std::wstring (*TimelineAutoSaveFunc)(const TimelineSnapshot& snapshot);

// Notification callback type
typedef void (*TimelineNotifyFunc)(const wchar_t* title, const wchar_t* message, bool isWarning);

// Initialize the engine
// baseDir: root directory for timeline storage
// cfg: engine configuration
// saveFunc: callback to persist a snapshot (called when engine decides to record)
// notifyFunc: callback for user notifications (can be nullptr)
void TimelineEngine_Init(
    const wchar_t* baseDir,
    const TimelineConfig& cfg,
    TimelineAutoSaveFunc saveFunc,
    TimelineNotifyFunc notifyFunc
);

// Main polling function - call every pollIntervalMs
// Returns the current engine state after this poll
TimelineState TimelineEngine_Poll();

// Force an immediate snapshot record (manual trigger)
// Returns the snapshot ID
std::wstring TimelineEngine_ForceRecord();

// Save the current layout as a named checkpoint (no engine init needed,
// Timeline_Init must have run). Returns the checkpoint ID for later
// TimelineEngine_Restore, or empty on failure.
std::wstring TimelineEngine_SaveCheckpoint(const wchar_t* name);

// Restore desktop icon positions from a stored snapshot ID.
// Returns icons restored, -1 on failure.
int TimelineEngine_Restore(const wchar_t* snapshotId);


void TimelineEngine_UpdateConfig(const TimelineConfig& cfg);

// Shutdown - cleanup resources
void TimelineEngine_Shutdown();


// Set active layout rules for automatic new-icon placement
// Pass nullptr to disable auto-placement
void TimelineEngine_SetRules(const LayoutRules* rules);
