// grid_system.cpp : DPI-aware desktop grid coordinate system
#include "grid_system.h"
#include "icon_manager.h"
#include "sort_engine.h"
#include <shlobj.h>
#include <shellscalingapi.h>
#include <vector>
#include <algorithm>

static SIZE g_cellSize = {96, 85};
static POINT g_origin = {0, 0};
static RECT g_workArea = {0, 0, 1920, 1080};
static DPI_AWARENESS_CONTEXT g_dpiContext = nullptr;

// LVM_GETITEMSPACING values don't match the real icon pitch, so calibrate
// from actual icon positions: consecutive same-row deltas give the true
// horizontal cell, same-column deltas the vertical cell. Median is robust
// against off-grid icons and holes in the grid.
// Row/col buckets avoid a tolerance-based comparator (which violates
// strict weak ordering); consecutive deltas within a bucket give the pitch
static void RefineCellSize() {
    auto icons = GetDesktopIcons();
    if (icons.size() < 4) return;

    std::vector<int> dxs, dys;

    std::vector<DesktopIcon> byRow(icons);
    std::sort(byRow.begin(), byRow.end(), [](const DesktopIcon& a, const DesktopIcon& b) {
        int ra = a.y / 40, rb = b.y / 40; // same visual row ≈ same bucket
        if (ra != rb) return ra < rb;
        return a.x < b.x;
    });
    for (size_t i = 1; i < byRow.size(); i++) {
        if (byRow[i].y / 40 == byRow[i - 1].y / 40) {
            int dx = byRow[i].x - byRow[i - 1].x;
            if (dx > 10 && dx < 500) dxs.push_back(dx);
        }
    }

    std::vector<DesktopIcon> byCol(icons);
    std::sort(byCol.begin(), byCol.end(), [](const DesktopIcon& a, const DesktopIcon& b) {
        int ca = a.x / 40, cb = b.x / 40;
        if (ca != cb) return ca < cb;
        return a.y < b.y;
    });
    for (size_t i = 1; i < byCol.size(); i++) {
        if (byCol[i].x / 40 == byCol[i - 1].x / 40) {
            int dy = byCol[i].y - byCol[i - 1].y;
            if (dy > 10 && dy < 500) dys.push_back(dy);
        }
    }

    auto median = [](std::vector<int>& v) -> int {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        return v[v.size() / 2];
    };
    int mx = median(dxs), my = median(dys);
    if (mx > 0) g_cellSize.cx = mx;
    if (my > 0) g_cellSize.cy = my;
}

// Origin = top-left of the visual grid: min position among icons in the
// leftmost column / topmost row
static void RefineOrigin() {
    auto icons = GetDesktopIcons();
    if (icons.size() < 4) return;
    int minX = INT_MAX, minY = INT_MAX;
    for (const auto& ic : icons) {
        if (ic.x > 0 && ic.x < g_cellSize.cx && ic.x < minX) minX = ic.x;
        if (ic.y > 0 && ic.y < g_cellSize.cy && ic.y < minY) minY = ic.y;
    }
    if (minX != INT_MAX) g_origin.x = minX;
    if (minY != INT_MAX) g_origin.y = minY;
}

void GridSystem_Init() {
    // DPI awareness
    HMODULE hUser32 = LoadLibraryW(L"user32.dll");
    if (hUser32) {
        typedef DPI_AWARENESS_CONTEXT(WINAPI* GetThreadDpiAwarenessContextFunc)();
        auto pGetContext = (GetThreadDpiAwarenessContextFunc)
            GetProcAddress(hUser32, "GetThreadDpiAwarenessContext");
        if (pGetContext) g_dpiContext = pGetContext();
        FreeLibrary(hUser32);
    }

    // Get desktop window for work area
    HWND hwndDesktop = FindWindowW(L"Progman", L"Program Manager");
    if (!hwndDesktop) hwndDesktop = GetShellWindow();

    if (hwndDesktop) {
        // Find list view
        HWND hwndListView = FindWindowExW(hwndDesktop, nullptr, L"SHELLDLL_DefView", nullptr);
        if (hwndListView)
            hwndListView = FindWindowExW(hwndListView, nullptr, L"SysListView32", nullptr);

        if (hwndListView) {
            // Get icon spacing from list view
            int spacing = (int)SendMessageW(hwndListView, LVM_GETITEMSPACING, (WPARAM)TRUE, 0);
            g_cellSize.cx = LOWORD(spacing) > 0 ? LOWORD(spacing) : g_cellSize.cx;
            g_cellSize.cy = HIWORD(spacing) > 0 ? HIWORD(spacing) : g_cellSize.cy;

            // Get origin by getting position of icon at index 0
            POINT pt = {0, 0};
            LRESULT pos = SendMessageW(hwndListView, LVM_GETITEMPOSITION, 0, 0);
            if (pos) {
                g_origin.x = LOWORD(pos);
                g_origin.y = HIWORD(pos);
            }

            // Get list view rect for work area
            RECT lvRect;
            if (GetWindowRect(hwndListView, &lvRect)) {
                // Adjust for DPI
                HMONITOR hMonitor = MonitorFromWindow(hwndListView, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = {sizeof(mi)};
                if (GetMonitorInfoW(hMonitor, &mi)) {
                    g_workArea = mi.rcWork;
                }
            }
        }
    }

    // Fallback: SystemParametersInfo for work area
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &g_workArea, 0);

    // Calibrate from actual icon positions (listview spacing is unreliable)
    RefineCellSize();
    RefineOrigin();
}

SIZE GridSystem_GetCellSize() { return g_cellSize; }
POINT GridSystem_GetOrigin() { return g_origin; }

bool GridSystem_PixelToGrid(POINT pixel, int* row, int* col) {
    if (!row || !col) return false;

    int dx = pixel.x - g_origin.x;
    int dy = pixel.y - g_origin.y;

    *row = (g_cellSize.cy > 0) ? (dy / g_cellSize.cy) : 0;
    *col = (g_cellSize.cx > 0) ? (dx / g_cellSize.cx) : 0;

    if (*row < 0) *row = 0;
    if (*col < 0) *col = 0;

    return true;
}

bool GridSystem_GridToPixel(int row, int col, POINT* pixel) {
    if (!pixel) return false;
    pixel->x = g_origin.x + col * g_cellSize.cx;
    pixel->y = g_origin.y + row * g_cellSize.cy;
    return true;
}

