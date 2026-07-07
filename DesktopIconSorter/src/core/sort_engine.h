// sort_engine.h : Desktop icon sorting engine with multi-partition support
#pragma once
#include <Windows.h>
#include <shlobj.h>
#include <vector>
#include <string>

struct IconItem {
    PITEMID_CHILD pidl;
    std::wstring name;
    std::wstring type;
    ULONGLONG size;
    FILETIME modified;
    POINT position;
    int gridRow;
    int gridCol;
    int partitionIndex; // -1 = unassigned
    int slotIndex;      // position within partition after sort
};

struct SortPartition {
    std::wstring name;
    int left, top, right, bottom; // grid cell bounds
    int sortMode;      // 0=name, 1=type, 2=size, 3=date, 4=custom
    bool ascending;
    bool locked;
};

// Phase 3: Multi-partition sort result
struct SortResult {
    int totalIcons;
    int partitionsUsed;
    int iconsSorted;
    int iconsLocked;
};

// Enumerate all desktop icons
std::vector<IconItem> EnumerateIcons();

// Assign icons to partitions based on grid position
// Sets icon.partitionIndex; unassigned icons get -1
void AssignToPartitions(
    std::vector<IconItem>& icons,
    const std::vector<SortPartition>& partitions);

// Sort all partitions independently
// Each partition's icons are sorted by its own sortMode/ascending
void SortAllPartitions(
    std::vector<IconItem>& icons,
    const std::vector<SortPartition>& partitions);

// Calculate grid positions for sorted icons within each partition
// Icons in a partition fill left-to-right, top-to-bottom
void LayoutAllPartitions(
    std::vector<IconItem>& icons,
    const std::vector<SortPartition>& partitions);

// Apply calculated positions to desktop via IFolderView (takes ownership of PIDLs)
SortResult ApplyPositions(std::vector<IconItem>& icons);

// Full multi-partition sort pipeline
SortResult ExecuteMultiPartitionSort(const std::vector<SortPartition>& partitions);
