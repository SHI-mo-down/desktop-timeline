// DesktopIconSorter.cpp : Desktop Timeline — layout memory
// Remembers desktop icon layouts. Auto-records changes. Restores on demand.
// Same layout = no duplicate saves.
#define NOMINMAX

#include <Windows.h>
#include <ShellAPI.h>
#include <stdio.h>
#include <stdarg.h>
#include <shlwapi.h>
#include <CommCtrl.h>

#include "core/icon_manager.h"
#include "core/grid_system.h"
#include "core/sort_engine.h"
#include "core/config_manager.h"
#include "core/snapshot_manager.h"
#include "core/timeline_record.h"
#include "core/timeline_db.h"
#include "core/timeline_engine.h"
#include "core/desktop_state.h"
#include "core/screenshot.h"
#include "core/desktop_ops.h"
#include "utils/console.h"
#include "ui/timeline_ui.h"
#include "ui/resource.h"
#include "core/rule_engine.h"

#pragma comment(lib, "comctl32.lib")

// ── Globals ──
wchar_t g_exeDir[MAX_PATH] = {};
wchar_t g_configPath[MAX_PATH] = L"desktop_sorter_config.json";
static AppConfig g_config;
static TimelineConfig g_timelineCfg;
static bool g_autoMonitor = true;
static NOTIFYICONDATAW g_nid = {};
static HINSTANCE g_hInstance = nullptr;
static LayoutRules g_activeRules; // Persistent rules for engine access

#define WM_TRAYICON         (WM_APP + 1)
#define ID_TRAY_OPEN_TIMELINE 3001
#define ID_TRAY_SAVE_CHECKPOINT 3002
#define ID_TRAY_AUTO_MONITOR    3003
#define ID_TRAY_SORT_NOW        3004
#define ID_TRAY_EXIT            3005
#define ID_POLL_TIMER           3001
#define ID_HOTKEY_CHECKPOINT    2001

void Log(const char* msg) { printf("%s\n", msg); fflush(stdout); }

void InitPaths() {
    GetModuleFileNameW(nullptr, g_exeDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(g_exeDir, L'\\');
    if (lastSlash) *(lastSlash + 1) = 0;
    wsprintfW(g_configPath, L"%sdesktop_sorter_config.json", g_exeDir);
    Snapshot_Init(g_exeDir);
    Timeline_Init(g_exeDir);
}

// ── Notification callback ──
void TimelineNotify(const wchar_t* title, const wchar_t* message, bool isWarning) {
    // Refresh timeline UI if open — new auto-record was saved
    TimelineUI_Refresh();

    g_nid.uFlags = NIF_INFO;
    wcsncpy_s(g_nid.szInfo, message, 255);
    wcsncpy_s(g_nid.szInfoTitle, title, 63);
    g_nid.dwInfoFlags = isWarning ? NIIF_WARNING : NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// ── Snapshot save callback ──
std::wstring TimelineSaveSnapshot(const TimelineSnapshot& snapshot) {
    if (Timeline_Save(snapshot)) return snapshot.id;
    return L"";
}

// ── Tray icon ──
void InitTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    wchar_t iconPath[MAX_PATH];
    wsprintfW(iconPath, L"%sapp.ico", g_exeDir);
    HANDLE hIcon = LoadImageW(nullptr, iconPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (!hIcon) hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    g_nid.hIcon = (HICON)hIcon;
    wcscpy_s(g_nid.szTip, L"Desktop Timeline");
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void ShowTrayMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_TIMELINE, L"Open Timeline...");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SAVE_CHECKPOINT, L"Save Checkpoint");
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SORT_NOW, L"Sort Icons (Legacy)");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | (g_autoMonitor ? MF_CHECKED : 0),
                ID_TRAY_AUTO_MONITOR, L"Auto Monitor");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

// ── Legacy sort (kept for manual use) ──
void DoSort() {
    Snapshot_AutoSave();
    GridSystem_Init();
    g_config = Config_Load(g_configPath);
    if (g_config.partitions.empty()) g_config = Config_CreateDefault();
    Snapshot_AutoSave();
    ExecuteMultiPartitionSort(g_config.partitions);
    Sleep(1000);

    HWND hwndDesktop = FindWindowW(L"Progman", L"Program Manager");
    if (hwndDesktop) { InvalidateRect(hwndDesktop, nullptr, TRUE); UpdateWindow(hwndDesktop); }
}

// ── Window procedure ──
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        InitTrayIcon(hwnd);
        SetTimer(hwnd, ID_POLL_TIMER, g_timelineCfg.pollIntervalMs, nullptr);
        RegisterHotKey(hwnd, ID_HOTKEY_CHECKPOINT, MOD_CONTROL | MOD_SHIFT, VK_F12);
        Log("[Timeline] Monitoring started. Ctrl+Shift+F12 = checkpoint.");
        break;
    }

    case WM_TIMER:
        if (wp == ID_POLL_TIMER && g_autoMonitor)
            TimelineEngine_Poll();
        break;

    case WM_HOTKEY:
        if (wp == ID_HOTKEY_CHECKPOINT) {
            TimelineEngine_ForceRecord();
            g_nid.uFlags = NIF_INFO;
            wcscpy_s(g_nid.szInfo, L"Checkpoint saved.");
            wcscpy_s(g_nid.szInfoTitle, L"Desktop Timeline");
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
        }
        break;

    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU)
            ShowTrayMenu(hwnd);
        else if (lp == WM_LBUTTONDBLCLK)
            TimelineUI_Show(g_hInstance, hwnd, g_exeDir);
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_TRAY_OPEN_TIMELINE:
            TimelineUI_Show(g_hInstance, hwnd, g_exeDir);
            break;
        case ID_TRAY_SAVE_CHECKPOINT: {
            std::wstring id = TimelineEngine_ForceRecord();
            g_nid.uFlags = NIF_INFO;
            wcscpy_s(g_nid.szInfo, L"Checkpoint saved.");
            wcscpy_s(g_nid.szInfoTitle, L"Desktop Timeline");
            g_nid.dwInfoFlags = NIIF_INFO;
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            break;
        }
        case ID_TRAY_AUTO_MONITOR:
            g_autoMonitor = !g_autoMonitor;
            wcscpy_s(g_nid.szTip, g_autoMonitor ? L"Desktop Timeline" : L"Desktop Timeline — Paused");
            Shell_NotifyIconW(NIM_MODIFY, &g_nid);
            if (g_autoMonitor) SetTimer(hwnd, ID_POLL_TIMER, g_timelineCfg.pollIntervalMs, nullptr);
            else KillTimer(hwnd, ID_POLL_TIMER);
            break;
        case ID_TRAY_SORT_NOW:
            DoSort();
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, ID_POLL_TIMER);
        UnregisterHotKey(hwnd, ID_HOTKEY_CHECKPOINT);
        TimelineEngine_Shutdown();
        TimelineUI_Close();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Tray mode ──
int RunTrayMode(HINSTANCE hInstance) {
    g_hInstance = hInstance;
    InitCommonControls();
    InitPaths();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    GridSystem_Init();
    g_config = Config_Load(g_configPath);
    if (g_config.partitions.empty()) g_config = Config_CreateDefault();
    g_timelineCfg = TimelineConfig::Default();
    TimelineEngine_Init(g_exeDir, g_timelineCfg, TimelineSaveSnapshot, TimelineNotify);

    // Load layout rules if present
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    SIZE cellSize = GridSystem_GetCellSize();

    wchar_t rulePath[MAX_PATH];
    wsprintfW(rulePath, L"%srules.dtrules", g_exeDir);
    g_activeRules = RuleEngine_Load(rulePath, screenW, screenH);
    TimelineEngine_SetRules(g_activeRules.loaded ? &g_activeRules : nullptr);

    if (g_activeRules.loaded && g_activeRules.onStartup == L"apply") {
        int moved = RuleEngine_Apply(g_activeRules, screenW, screenH, cellSize.cx, cellSize.cy,
                                      nullptr, 0);
        if (moved > 0) {
            // Invalidate desktop to refresh
            HWND hwndDT = FindWindowW(L"Progman", L"Program Manager");
            if (hwndDT) { InvalidateRect(hwndDT, nullptr, TRUE); UpdateWindow(hwndDT); }
        }
    }

    const wchar_t* CLASS_NAME = L"DesktopTimeline_Tray";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wchar_t iconPath[MAX_PATH]; wsprintfW(iconPath, L"%sapp.ico", g_exeDir);
    HANDLE hIco2 = LoadImageW(nullptr, iconPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (hIco2) wc.hIcon = (HICON)hIco2;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    HWND hwndTray = CreateWindowExW(0, CLASS_NAME, L"Desktop Timeline",
                                    0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
    if (!hwndTray) { CoUninitialize(); return 1; }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}

// ── Console mode ──
// on|off|1|0|true|false; returns false for anything else
static bool ParseOnOff(const wchar_t* s, bool* out) {
    if (_wcsicmp(s, L"on") == 0 || _wcsicmp(s, L"1") == 0 || _wcsicmp(s, L"true") == 0) { *out = true; return true; }
    if (_wcsicmp(s, L"off") == 0 || _wcsicmp(s, L"0") == 0 || _wcsicmp(s, L"false") == 0) { *out = false; return true; }
    return false;
}

static int ParseIconSize(const wchar_t* s) {
    if (_wcsicmp(s, L"small") == 0) return 32;
    if (_wcsicmp(s, L"medium") == 0) return 48;
    if (_wcsicmp(s, L"large") == 0) return 96;
    return _wtoi(s);
}

// Unicode-safe printf to stdout (see utils/console.h)
static void PrintWF(const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = _vscwprintf(fmt, ap);
    if (len > 0) {
        std::wstring buf(len, L'\0');
        vswprintf_s(&buf[0], len + 1, fmt, ap);
        Console_PrintW(buf);
    }
    va_end(ap);
}
int RunConsoleMode(int argc, wchar_t* argv[]) {
    InitPaths();
    // Take over a console only when run without one (e.g. double-click).
    // When stdout is piped/redirected (CLI scripting, MCP), keep writing there.
    DWORD outType = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    bool interactive = (outType == FILE_TYPE_CHAR);
    if (outType == FILE_TYPE_UNKNOWN) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        interactive = true;
    }

    if (interactive) {
        Log("Desktop Timeline — Layout Memory");
        Log("==================================");
    }

    if (argc < 2) {
        Log("Commands:");
        Log("  /dump          Export desktop state as JSON (desktop_state.json)");
        Log("  /shot          Screenshot desktop + state JSON (for LLM workflow)");
        Log("  /set <what> <value>  Change desktop settings:");
        Log("    autoarrange on|off      snapgrid on|off");
        Log("    iconsize small|medium|large|<px>");
        Log("    spacing <h> <v> (pixels)");
        Log("    sysicon thispc|recycle|network|controlpanel|userfiles on|off");
        Log("  /shortcut <name> <target> [args] [workdir] [icon]  Create desktop shortcut");
        Log("  /timeline      Open timeline panel");
        Log("  /checkpoint    Save a checkpoint");
        Log("  /list          List all snapshots");
        Log("  /tray          Start system tray (auto monitor)");
        Log("  /sort          Sort icons (legacy)");
        Log("  /snap <cmd>    Legacy snapshot operations");
        if (interactive) system("pause > nul");
        if (outType == FILE_TYPE_UNKNOWN) FreeConsole();
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (_wcsicmp(argv[1], L"/tray") == 0) {
        Log("Starting system tray...");
        if (GetConsoleWindow() != nullptr) FreeConsole();
        return 2;
    }
    if (_wcsicmp(argv[1], L"/timeline") == 0) {
        Log("Opening Timeline...");
        TimelineUI_Show(GetModuleHandleW(nullptr), nullptr, g_exeDir);
        system("pause > nul");
    }
    else if (_wcsicmp(argv[1], L"/dump") == 0) {
        GridSystem_Init();
        wchar_t outPath[MAX_PATH];
        wsprintfW(outPath, L"%sdesktop_state.json", g_exeDir);
        int n = DesktopState_Export(outPath);
        if (n < 0) {
            Log("  Export failed - cannot read desktop icons");
        } else if (interactive) {
            wchar_t msg[512];
            swprintf_s(msg, L"  Exported %d icons to %s\n", n, outPath);
            Console_PrintW(msg);
        }
    }
    else if (_wcsicmp(argv[1], L"/shot") == 0) {
        GridSystem_Init();
        wchar_t pngPath[MAX_PATH], jsonPath[MAX_PATH];
        wsprintfW(pngPath, L"%sdesktop.png", g_exeDir);
        wsprintfW(jsonPath, L"%sdesktop_state.json", g_exeDir);
        DesktopState_Export(jsonPath);
        if (Desktop_Screenshot(pngPath)) {
            if (interactive) {
                wchar_t msg[512];
                swprintf_s(msg, L"  Screenshot saved to %s\n", pngPath);
                Console_PrintW(msg);
            }
        } else {
            Log("  Screenshot failed");
        }
    }
    else if (_wcsicmp(argv[1], L"/set") == 0) {
        if (argc < 3) {
            Log("Usage: /set autoarrange|snapgrid|iconsize|spacing|sysicon <value>");
            Log("Run /dump to see current values.");
        }
        else if (_wcsicmp(argv[2], L"autoarrange") == 0) {
            bool on = false;
            if (argc >= 4 && ParseOnOff(argv[3], &on)) Desktop_SetAutoArrange(on);
            Log(Desktop_GetAutoArrange() ? "  autoarrange: on" : "  autoarrange: off");
        }
        else if (_wcsicmp(argv[2], L"snapgrid") == 0) {
            bool on = false;
            if (argc >= 4 && ParseOnOff(argv[3], &on)) Desktop_SetSnapToGrid(on);
            Log(Desktop_GetSnapToGrid() ? "  snapgrid: on" : "  snapgrid: off");
        }
        else if (_wcsicmp(argv[2], L"iconsize") == 0) {
            if (argc >= 4) {
                int px = ParseIconSize(argv[3]);
                if (px > 0 && Desktop_SetIconSize(px)) Log("  iconsize set");
                else Log("  iconsize failed (16-512 px)");
            }
            PrintWF(L"  iconsize: %d\n", Desktop_GetIconSize());
        }
        else if (_wcsicmp(argv[2], L"spacing") == 0) {
            if (argc >= 5) {
                int h = _wtoi(argv[3]), v = _wtoi(argv[4]);
                if (h >= 16 && v >= 16 && Desktop_SetSpacing(h, v)) Log("  spacing set");
                else Log("  spacing failed (min 16 px)");
            }
            int h = 0, v = 0;
            Desktop_GetSpacing(&h, &v);
            PrintWF(L"  spacing: %d x %d\n", h, v);
        }
        else if (_wcsicmp(argv[2], L"sysicon") == 0) {
            if (argc < 4) {
                Log("Usage: /set sysicon thispc|recycle|network|controlpanel|userfiles on|off");
            } else if (argc >= 5) {
                bool on = false;
                if (ParseOnOff(argv[4], &on) && Desktop_SetSystemIcon(argv[3], on)) {
                    PrintWF(L"  sysicon %ls: %ls\n", argv[3], on ? L"on" : L"off");
                } else {
                    Log("  sysicon failed (unknown key?)");
                }
            } else {
                for (const auto& s : Desktop_GetSystemIconStates())
                    PrintWF(L"  %ls: %ls\n", s.key.c_str(), s.visible ? L"visible" : L"hidden");
            }
        }
        else {
            Log("Unknown /set target");
        }
    }
    else if (_wcsicmp(argv[1], L"/shortcut") == 0) {
        if (argc < 4) {
            Log("Usage: /shortcut <name> <target> [args] [workdir] [icon]");
        } else {
            const wchar_t* args = argc > 4 ? argv[4] : L"";
            const wchar_t* workDir = argc > 5 ? argv[5] : L"";
            const wchar_t* icon = argc > 6 ? argv[6] : L"";
            if (Desktop_CreateShortcut(argv[2], argv[3], args, workDir, icon))
                PrintWF(L"  Shortcut '%ls' created on desktop\n", argv[2]);
            else
                Log("  Shortcut creation failed");
        }
    }
    else if (_wcsicmp(argv[1], L"/checkpoint") == 0) {
        TimelineEngine_Init(g_exeDir, TimelineConfig::Default(), TimelineSaveSnapshot, nullptr);
        std::wstring id = TimelineEngine_ForceRecord();
        PrintWF(L"  Checkpoint: %s\n", id.c_str());
    }
    else if (_wcsicmp(argv[1], L"/list") == 0) {
        Timeline_Init(g_exeDir);
        auto list = Timeline_List(50);
        PrintWF(L"  %zu snapshots:\n", list.size());
        for (size_t i = 0; i < list.size(); i++)
            PrintWF(L"  [%zu] %s  %s  %d icons\n",
                   i, list[i].timestamp.c_str(), list[i].name.c_str(), list[i].iconCount);
    }
    else if (wcsncmp(argv[1], L"/sort", 5) == 0) {
        int mode = -1;
        if (wcslen(argv[1]) > 6 && argv[1][5] == L':')
            mode = _wtoi(argv[1] + 6);
        GridSystem_Init();
        g_config = Config_Load(g_configPath);
        if (g_config.partitions.empty()) g_config = Config_CreateDefault();
        if (mode >= 0 && mode <= 3 && !g_config.partitions.empty())
            g_config.partitions[0].sortMode = mode;
        TimelineEngine_Init(g_exeDir, TimelineConfig::Default(), TimelineSaveSnapshot, nullptr);
        TimelineEngine_ForceRecord();
        SortResult sr = ExecuteMultiPartitionSort(g_config.partitions);
        wprintf(L"  Icons: %d  Sorted: %d\n", sr.totalIcons, sr.iconsSorted);
    }
    else if (_wcsicmp(argv[1], L"/snap") == 0 && argc >= 3) {
        if (_wcsicmp(argv[2], L"save") == 0 && argc >= 4) {
            Snapshot_Save(argv[3]); wprintf(L"  '%s' saved\n", argv[3]);
        } else if (_wcsicmp(argv[2], L"list") == 0) {
            auto sl = Snapshot_List();
            for (size_t i = 0; i < sl.size(); i++)
                wprintf(L"  [%zu] %s  %d icons\n", i, sl[i].name, sl[i].iconCount);
        } else if (_wcsicmp(argv[2], L"restore") == 0 && argc >= 4) {
            wchar_t fp[MAX_PATH];
            wsprintfW(fp, L"%s\\snapshots\\manual\\%s.snap", g_exeDir, argv[3]);
            Log(Snapshot_Restore(fp) ? "  Restored" : "  Failed");
        } else if (_wcsicmp(argv[2], L"undo") == 0) {
            Log(Snapshot_Undo() ? "  Undo done" : "  Nothing to undo");
        }
    }

    CoUninitialize();
    if (interactive) { Log(""); system("pause > nul"); }
    if (outType == FILE_TYPE_UNKNOWN) FreeConsole();
    return 0;
}

// ── Silent sort (from shell extension) ──
int RunSilentSort(int sortMode) {
    InitPaths();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    GridSystem_Init();
    AppConfig cfg = Config_Load(g_configPath);
    if (cfg.partitions.empty()) cfg = Config_CreateDefault();
    if (sortMode >= 0 && sortMode <= 3 && !cfg.partitions.empty())
        cfg.partitions[0].sortMode = sortMode;
    TimelineEngine_Init(g_exeDir, TimelineConfig::Default(), TimelineSaveSnapshot, nullptr);
    TimelineEngine_ForceRecord();
    ExecuteMultiPartitionSort(cfg.partitions);
    HWND hwndDesktop = FindWindowW(L"Progman", L"Program Manager");
    if (hwndDesktop) { InvalidateRect(hwndDesktop, nullptr, TRUE); UpdateWindow(hwndDesktop); }
    CoUninitialize();
    return 0;
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc >= 2 && _wcsicmp(argv[1], L"/tray") == 0)
        return RunTrayMode(GetModuleHandleW(nullptr));
    if (argc >= 2 && wcsncmp(argv[1], L"/sort:", 6) == 0)
        return RunSilentSort(_wtoi(argv[1] + 6));
    if (argc >= 2)
        return RunConsoleMode(argc, argv);
    return RunTrayMode(GetModuleHandleW(nullptr));
}
