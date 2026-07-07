// grid_system.cpp : DPI-aware desktop grid coordinate system
#include "grid_system.h"
#include "sort_engine.h"
#include <shlobj.h>
#include <shellscalingapi.h>

static SIZE g_cellSize = {96, 85};
static POINT g_origin = {0, 0};
static RECT g_workArea = {0, 0, 1920, 1080};
static DPI_AWARENESS_CONTEXT g_dpiContext = nullptr;

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

