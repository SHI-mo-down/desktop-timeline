// grid_system.h : DPI-aware desktop grid coordinate system
#pragma once
#include <Windows.h>

// Initialize grid system with current monitor/DPI settings
void GridSystem_Init();

// Get current icon grid cell size in pixels
SIZE GridSystem_GetCellSize();

// Get icon grid origin (top-left of the icon area) in screen coordinates
POINT GridSystem_GetOrigin();

// Calculate grid position from pixel coordinates (row, col)
// Returns true if position is valid (on the desktop area)
bool GridSystem_PixelToGrid(POINT pixel, int* row, int* col);
bool GridSystem_GridToPixel(int row, int col, POINT* pixel);

// Sort direction within a grid partition
enum class SortDirection { Ascending, Descending };

// Sort mode for a partition
enum class SortMode {
    Name = 0,
    Type = 1,
    Size = 2,
    Date = 3,
    Custom = 4
};
