// timeline_engine.cpp : Core timeline engine - polling, debounce, auto-record
#include "timeline_engine.h"
#include "timeline_db.h"
#include "icon_manager.h"
#include "layout_engine.h"
#include <shlwapi.h>
#include "../utils/com_ptr.h"
#include <stdio.h>
#include <time.h>
#include <set>
#include <map>
#include <utility>
#include <vector>
#include "rule_engine.h"
#include "cleanup_policy.h"

// Static state
static TimelineConfig g_config;
static TimelineState g_state = TimelineState::IDLE;
static TimelineAutoSaveFunc g_saveFunc = nullptr;
static TimelineNotifyFunc g_notifyFunc = nullptr;
static wchar_t g_baseDir[MAX_PATH] = {};
static const LayoutRules* g_pRules = nullptr;
static int g_cleanupCounter = 0;  // periodic cleanup every N polls

static DWORD g_lastKnownHash = 0;
static DWORD g_pendingHash = 0;       // The hash that triggered debounce
static ULONGLONG g_debounceStart = 0; // Tick count when debounce started
static bool g_initialized = false;

static int g_pendingChangeCount = 0;
static ULONGLONG g_nextNotificationTime = 0; // Absolute tick deadline for next allowed notification
static const ULONGLONG NOTIFICATION_COOLDOWN_MS = 300000; // 5 minutes in milliseconds

// Forward declarations
static DWORD ComputeLayoutHash();
static void RecordSnapshot(const wchar_t* label);
static void SendNotification(const wchar_t* title, const wchar_t* msg, bool isWarning);

void TimelineEngine_Init(
    const wchar_t* baseDir,
    const TimelineConfig& cfg,
    TimelineAutoSaveFunc saveFunc,
    TimelineNotifyFunc notifyFunc)
{
    wcscpy_s(g_baseDir, baseDir);
    g_config = cfg;
    g_saveFunc = saveFunc;
    g_notifyFunc = notifyFunc;
    g_state = TimelineState::IDLE;
    g_lastKnownHash = 0;
    g_pendingHash = 0;
    g_debounceStart = 0;
    g_pendingChangeCount = 0;
    g_nextNotificationTime = 0;
    g_initialized = true;

    // Snapshot initial layout
    g_lastKnownHash = ComputeLayoutHash();
}

TimelineState TimelineEngine_Poll() {
    if (!g_initialized) return TimelineState::IDLE;

    // Periodic cleanup (every ~3 min at 2s poll)
    g_cleanupCounter++;
    if (g_cleanupCounter >= 100) {
        g_cleanupCounter = 0;
        CleanupConfig cc = { g_config.maxRecords, g_config.dailyMergeDays, true };
        Cleanup_Execute(g_baseDir, cc);
    }

    DWORD currentHash = ComputeLayoutHash();
    if (currentHash == 0) return g_state; // Can't read layout

    ULONGLONG now = GetTickCount64();

    switch (g_state) {
    case TimelineState::IDLE:
        if (currentHash != g_lastKnownHash) {
            // Change detected - enter debounce
            g_state = TimelineState::DEBOUNCING;
            g_pendingHash = currentHash;
            g_debounceStart = now;
            g_pendingChangeCount = -1; // Will be computed on confirm
        }
        break;

    case TimelineState::DEBOUNCING: {
        ULONGLONG elapsed = now - g_debounceStart;

        if (currentHash != g_pendingHash) {
            // New change during debounce - reset timer
            g_pendingHash = currentHash;
            g_debounceStart = now;
        } else if (elapsed >= (ULONGLONG)g_config.debounceWindowMs) {
            // Stable for debounce window - confirm the change
            g_state = TimelineState::CONFIRMED;

            // Record the snapshot
            RecordSnapshot(L"auto");

            // Update state
            g_lastKnownHash = currentHash;
            g_state = TimelineState::IDLE;
        }
        break;
    }

    case TimelineState::CONFIRMED:
        // Shouldn't stay in this state - transition handled above
        g_state = TimelineState::IDLE;
        break;
    }

    return g_state;
}

// Compute a hash of current desktop icon layout
static DWORD ComputeLayoutHash() {
    auto icons = GetDesktopIcons();
    if (icons.empty()) return 0;

    DWORD hash = 0;
    for (const auto& icon : icons) {
        for (const wchar_t* c = icon.name.c_str(); *c; c++)
            hash = hash * 31 + *c;
        hash = hash * 31 + icon.x;
        hash = hash * 31 + icon.y;
    }
    hash = hash * 31 + (int)icons.size();
    return hash;
}

// Build a TimelineSnapshot from current desktop layout
static TimelineSnapshot CaptureCurrentLayout(const wchar_t* label) {
    TimelineSnapshot snap = {};

    // Generate ID
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t id[64];
    wsprintfW(id, L"%04d%02d%02d_%02d%02d%02d_%s",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond,
             label);
    snap.id = id;

    // Timestamp
    wchar_t ts[64];
    wsprintfW(ts, L"%04d-%02d-%02dT%02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    snap.timestamp = ts;

    // Name
    if (wcscmp(label, L"auto") == 0) {
        wchar_t name[128];
        wsprintfW(name, L"[Auto] %04d-%02d-%02d %02d:%02d:%02d",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);
        snap.name = name;
        snap.isAutoGenerated = true;
    } else {
        snap.name = label;
        snap.isAutoGenerated = false;
    }

    // Monitor info (primary monitor)
    snap.monitorWidth = GetSystemMetrics(SM_CXSCREEN);
    snap.monitorHeight = GetSystemMetrics(SM_CYSCREEN);

    HDC hdc = GetDC(nullptr);
    snap.dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);

    // Enumerate icons
    for (const auto& di : GetDesktopIcons()) {
        TimelineIcon icon = {};
        icon.name = di.name;
        icon.path = di.path;
        icon.x = di.x;
        icon.y = di.y;
        icon.gridRow = 0;
        icon.gridCol = 0;
        snap.icons.push_back(icon);
    }

    snap.iconCount = (int)snap.icons.size();
    snap.isCheckpoint = (wcscmp(label, L"auto") != 0);

    // Capture monitor layout
    snap.monitorCount = 0;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT r, LPARAM p) -> BOOL {
            auto* snap = (TimelineSnapshot*)p;
            if (snap->monitorCount < 8) {
                snap->monitors[snap->monitorCount++] = *r;
            }
            return TRUE;
        }, (LPARAM)&snap);

    return snap;
}

// Count how many icons differ between two snapshots
static int CountChangedIcons(const TimelineSnapshot& a, const TimelineSnapshot& b) {
    int changed = 0;
    // Count icons in 'b' that moved or are new
    for (const auto& ib : b.icons) {
        bool found = false;
        for (const auto& ia : a.icons) {
            if (ia.name == ib.name) {
                int dx = abs(ia.x - ib.x);
                int dy = abs(ia.y - ib.y);
                if (dx > g_config.minPixelDrift || dy > g_config.minPixelDrift) {
                    changed++;
                }
                found = true;
                break;
            }
        }
        if (!found) changed++; // New icon
    }
    // Count removed icons
    for (const auto& ia : a.icons) {
        bool found = false;
        for (const auto& ib : b.icons) {
            if (ia.name == ib.name) { found = true; break; }
        }
        if (!found) changed++;
    }
    return changed;
}

static void RecordSnapshot(const wchar_t* label) {
    TimelineSnapshot snap = CaptureCurrentLayout(label);
    if (snap.icons.empty()) return;

    // Noise filtering: skip if too few icons changed
    if (wcscmp(label, L"auto") == 0) {
        // Load the previous snapshot to compare
        TimelineSnapshot prev;
        TimelineSummary latest;
        if (Timeline_GetLatest(&latest)) {
            if (Timeline_Load(latest.id.c_str(), &prev)) {
                int changed = CountChangedIcons(prev, snap);

                // Skip if below threshold
                if (changed < g_config.minIconChanges) return;

                // Smart merge: if change is small (< mergeThreshold%), replace last
                if (g_config.mergeThreshold > 0.0f) {
                    float changeRatio = (float)changed / (float)snap.iconCount;
                    if (changeRatio < g_config.mergeThreshold && !prev.isCheckpoint) {
                        // Replace the last record instead of creating new
                        snap.id = prev.id; // Reuse ID
                        snap.isCheckpoint = prev.isCheckpoint;
                    }
                }

                g_pendingChangeCount = changed;

                // Auto-place new icons if rules are active
                if (g_pRules && g_pRules->loaded && g_pRules->onNewIcon == L"place") {
                    int cellW = 96, cellH = 49;
                    int screenW = snap.monitorWidth;
                    int screenH = snap.monitorHeight;

                    // Build name→PIDL map in ONE COM enumeration (not per-icon)
                    std::map<std::wstring, PITEMID_CHILD> pidlMap;
                    ComPtr<IFolderView> spFV;
                    if (SUCCEEDED(GetDesktopFolderView(&spFV))) {
                        IShellFolder* sf2 = nullptr;
                        IEnumIDList* spE2 = nullptr;
                        if (SUCCEEDED(spFV->GetFolder(IID_IShellFolder, (void**)&sf2))) {
                            if (SUCCEEDED(spFV->Items(SVGIO_ALLVIEW, IID_IEnumIDList, (void**)&spE2))) {
                                PITEMID_CHILD pidl2 = nullptr;
                                ULONG f2 = 0;
                                while (spE2->Next(1, &pidl2, &f2) == S_OK && f2 > 0) {
                                    if (pidl2) {
                                        STRRET sr;
                                        wchar_t n2[MAX_PATH] = L"";
                                        if (SUCCEEDED(sf2->GetDisplayNameOf(pidl2, SHGDN_NORMAL, &sr)))
                                            StrRetToBuf(&sr, pidl2, n2, MAX_PATH);
                                        pidlMap[n2] = pidl2; // Store — will free later
                                    }
                                }
                                spE2->Release();
                            }
                            sf2->Release();
                        }
                    }

                    // Precompute zone assignments for all snap icons
                    std::map<int, std::vector<POINT>> zoneOcc;
                    for (const auto& si : snap.icons) {
                        int zi = RuleEngine_MatchIconEx(*g_pRules, si.name, false);
                        if (zi >= 0) zoneOcc[zi].push_back({si.x, si.y});
                    }

                    // Find NEW icons: in snap but NOT in prev
                    std::set<std::wstring> oldNames;
                    for (const auto& oi : prev.icons) oldNames.insert(oi.name);

                    for (const auto& icon : snap.icons) {
                        if (oldNames.count(icon.name)) continue; // Not new

                        int zoneIdx = RuleEngine_MatchIconEx(*g_pRules, icon.name, false);
                        if (zoneIdx < 0) continue;

                        POINT target = {0, 0};
                        if (RuleEngine_PlaceInZone(g_pRules->zones[zoneIdx],
                                screenW, screenH, cellW, cellH, zoneOcc[zoneIdx], &target)) {
                            auto it = pidlMap.find(icon.name);
                            if (it != pidlMap.end() && it->second) {
                                spFV->SelectAndPositionItems(
                                    1, (PCUITEMID_CHILD*)&it->second, &target, SVSI_POSITIONITEM);
                                zoneOcc[zoneIdx].push_back(target); // Mark as occupied
                            }
                        }
                    }

                    // Free all PIDLs from the map
                    for (auto& pair : pidlMap) {
                        if (pair.second) CoTaskMemFree(pair.second);
                    }
                }
            }
        }
    }

    // Save via callback
    if (g_saveFunc) {
        g_saveFunc(snap);
    } else {
        Timeline_Save(snap);
    }

    // Notify if appropriate
    if (g_config.notificationsEnabled) {
        if (wcscmp(label, L"auto") == 0) {
            if (g_config.notifyOnMajorChange && g_pendingChangeCount > 5) {
                wchar_t msg[256];
                wsprintfW(msg, L"Detected layout change: %d icons moved. Click to view timeline.",
                         g_pendingChangeCount);
                SendNotification(L"Desktop Timeline", msg, false);
            }
        } else {
            // Manual checkpoint save - always notify
            wchar_t msg[256];
            wsprintfW(msg, L"Checkpoint '%s' saved (%d icons).", label, snap.iconCount);
            SendNotification(L"Desktop Timeline", msg, false);
        }
    }
}

static void SendNotification(const wchar_t* title, const wchar_t* msg, bool isWarning) {
    // Cooldown check: max 1 notification per 5 minutes (absolute time based)
    ULONGLONG now = GetTickCount64();
    if (now < g_nextNotificationTime) return;
    g_nextNotificationTime = now + NOTIFICATION_COOLDOWN_MS;

    if (g_notifyFunc) {
        g_notifyFunc(title, msg, isWarning);
    }
}

std::wstring TimelineEngine_ForceRecord() {
    if (!g_initialized) return L"";

    // Save current layout as named checkpoint
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t label[64];
    wsprintfW(label, L"checkpoint_%02d%02d_%02d%02d",
             st.wMonth, st.wDay, st.wHour, st.wMinute);

    TimelineSnapshot snap = CaptureCurrentLayout(label);
    snap.isCheckpoint = true;
    snap.isAutoGenerated = false;

    if (g_saveFunc) g_saveFunc(snap);
    else Timeline_Save(snap);

    g_lastKnownHash = ComputeLayoutHash();
    return snap.id;
}
std::wstring TimelineEngine_SaveCheckpoint(const wchar_t* name) {
    if (!name || !name[0]) return L"";
    TimelineSnapshot snap = CaptureCurrentLayout(name);
    if (snap.icons.empty()) return L"";
    snap.isCheckpoint = true;
    snap.isAutoGenerated = false;
    if (!Timeline_Save(snap)) return L"";

    wchar_t id[160];
    Timeline_MakeCheckpointId(name, id, 160);
    return id;
}

int TimelineEngine_Restore(const wchar_t* snapshotId) {
    TimelineSnapshot snap;
    if (!snapshotId || !snapshotId[0] || !Timeline_Load(snapshotId, &snap)) return -1;
    if (snap.icons.empty()) return -1;

    std::vector<std::pair<std::wstring, POINT>> pairs;
    pairs.reserve(snap.icons.size());
    for (const auto& ic : snap.icons)
        pairs.push_back({ic.name, {ic.x, ic.y}});
    return LayoutEngine_ApplyPairs(pairs);
}

void TimelineEngine_UpdateConfig(const TimelineConfig& cfg) { g_config = cfg; }

void TimelineEngine_Shutdown() {
    g_initialized = false;
    g_saveFunc = nullptr;
    g_notifyFunc = nullptr;
}

void TimelineEngine_SetRules(const LayoutRules* rules) {
    g_pRules = rules;
}
