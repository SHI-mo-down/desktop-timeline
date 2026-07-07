// sort_engine.cpp : Multi-partition icon sorting engine
#include "sort_engine.h"
#include "icon_manager.h"
#include "grid_system.h"
#include <shlwapi.h>
#include <algorithm>
#include <cwctype>

#undef max
#undef min

std::vector<IconItem> EnumerateIcons() {
    std::vector<IconItem> result;

    ComPtr<IFolderView> spFolderView;
    if (FAILED(GetDesktopFolderView(&spFolderView))) return result;

    IShellFolder* spShellFolder = nullptr;
    if (FAILED(spFolderView->GetFolder(IID_IShellFolder,
            reinterpret_cast<void**>(&spShellFolder)))) return result;

    IEnumIDList* spEnum = nullptr;
    if (FAILED(spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList,
            reinterpret_cast<void**>(&spEnum)))) {
        spShellFolder->Release();
        return result;
    }

    PITEMID_CHILD pidl = nullptr;
    ULONG fetched = 0;
    while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
        if (pidl) {
            IconItem item = {};
            item.pidl = pidl;
            item.partitionIndex = -1;
            item.slotIndex = 0;

            wchar_t name[MAX_PATH] = L"";
            STRRET strret;
            if (SUCCEEDED(spShellFolder->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret)))
                StrRetToBuf(&strret, pidl, name, MAX_PATH);
            item.name = name;

            size_t dot = item.name.rfind(L'.');
            item.type = (dot != std::wstring::npos) ? item.name.substr(dot) : L"";

            POINT pt = {0, 0};
            if (SUCCEEDED(spFolderView->GetItemPosition(pidl, &pt))) {
                item.position = pt;
                GridSystem_PixelToGrid(pt, &item.gridRow, &item.gridCol);
            }

            item.size = 0;
            item.modified = {0, 0};
            wchar_t fullPath[MAX_PATH] = L"";
            if (SUCCEEDED(SHGetPathFromIDListW(pidl, fullPath)) && fullPath[0]) {
                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(fullPath, &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    ULARGE_INTEGER sz;
                    sz.LowPart = fd.nFileSizeLow;
                    sz.HighPart = fd.nFileSizeHigh;
                    item.size = sz.QuadPart;
                    item.modified = fd.ftLastWriteTime;
                    FindClose(hFind);
                }
            }

            result.push_back(item);
        }
    }

    spEnum->Release();
    spShellFolder->Release();
    return result;
}

void AssignToPartitions(
    std::vector<IconItem>& icons,
    const std::vector<SortPartition>& partitions)
{
    if (partitions.empty()) {
        for (auto& icon : icons) icon.partitionIndex = -1;
        return;
    }

    for (auto& icon : icons) {
        icon.partitionIndex = -1;
        for (size_t i = 0; i < partitions.size(); i++) {
            const auto& p = partitions[i];
            if (icon.gridCol >= p.left && icon.gridCol <= p.right &&
                icon.gridRow >= p.top && icon.gridRow <= p.bottom) {
                icon.partitionIndex = (int)i;
                break;
            }
        }
    }
}

void SortAllPartitions(
    std::vector<IconItem>& icons,
    const std::vector<SortPartition>& partitions)
{
    // Group icons by partition
    std::vector<std::vector<IconItem*>> groups(partitions.size());
    for (auto& icon : icons) {
        if (icon.partitionIndex >= 0 && icon.partitionIndex < (int)partitions.size()) {
            const auto& part = partitions[icon.partitionIndex];
            if (part.locked) continue; // skip locked partitions
            groups[icon.partitionIndex].push_back(&icon);
        }
    }

    // Sort each partition independently
    for (size_t pIdx = 0; pIdx < partitions.size(); pIdx++) {
        const auto& part = partitions[pIdx];
        if (part.locked) continue;

        auto& group = groups[pIdx];
        int mode = part.sortMode;
        bool asc = part.ascending;

        std::stable_sort(group.begin(), group.end(),
            [mode, asc](const IconItem* a, const IconItem* b) {
                bool less = false;
                switch (mode) {
                case 1: less = a->type < b->type; break;
                case 2: less = a->size < b->size; break;
                case 3: less = CompareFileTime(&a->modified, &b->modified) < 0; break;
                default:
                    less = _wcsicmp(a->name.c_str(), b->name.c_str()) < 0;
                    break;
                }
                return asc ? less : !less;
            });

        // Assign slot indices
        for (size_t i = 0; i < group.size(); i++)
            group[i]->slotIndex = (int)i;
    }
}

void LayoutAllPartitions(
    std::vector<IconItem>& icons,
    const std::vector<SortPartition>& partitions)
{
    // Group icons by partition
    std::vector<std::vector<IconItem*>> groups(partitions.size());
    for (auto& icon : icons) {
        if (icon.partitionIndex >= 0 && icon.partitionIndex < (int)partitions.size())
            groups[icon.partitionIndex].push_back(&icon);
    }

    // Estimate max columns per partition based on cell size
    SIZE cell = GridSystem_GetCellSize();
    POINT origin = GridSystem_GetOrigin();

    for (size_t pIdx = 0; pIdx < partitions.size(); pIdx++) {
        const auto& part = partitions[pIdx];
        auto& group = groups[pIdx];

        if (part.locked) continue;

        // Calculate layout: left→right, top→bottom
        int startRow = std::max(0, part.top);
        int startCol = std::max(0, part.left);
        int endCol = part.right;

        // Estimate width of partition in pixels
        int partWidth = (endCol - startCol + 1) * cell.cx;
        if (partWidth <= 0) partWidth = 1920; // fallback

        int row = startRow;
        int col = startCol;
        int maxColsPerRow = std::max(1, (endCol - startCol + 1));

        for (auto* icon : group) {
            POINT pixel;
            GridSystem_GridToPixel(row, col, &pixel);
            icon->position = pixel;

            col++;
            if (col > endCol) {
                col = startCol;
                row++;
            }
        }
    }
}

SortResult ApplyPositions(std::vector<IconItem>& icons) {
    SortResult sr = {};
    sr.totalIcons = (int)icons.size();

    ComPtr<IFolderView> spFolderView;
    if (FAILED(GetDesktopFolderView(&spFolderView))) {
        // Still must free all PIDLs even on error
        for (auto& icon : icons) {
            if (icon.pidl) CoTaskMemFree(icon.pidl);
            icon.pidl = nullptr;
        }
        return sr;
    }

    for (auto& icon : icons) {
        if (icon.partitionIndex >= 0 && icon.pidl) {
            POINT pt = icon.position;
            if (SUCCEEDED(spFolderView->SelectAndPositionItems(
                    1, const_cast<PCUITEMID_CHILD*>(&icon.pidl),
                    &pt, SVSI_POSITIONITEM))) {
                sr.iconsSorted++;
            }
        }
        if (icon.pidl) {
            CoTaskMemFree(icon.pidl);
            icon.pidl = nullptr;
        }
    }

    return sr;
}

SortResult ExecuteMultiPartitionSort(const std::vector<SortPartition>& partitions) {
    SortResult sr = {};

    auto icons = EnumerateIcons();
    sr.totalIcons = (int)icons.size();
    if (icons.empty()) return sr;

    // Count active partitions
    for (const auto& p : partitions) {
        if (!p.locked) sr.partitionsUsed++;
    }
    sr.iconsLocked = (int)partitions.size() - sr.partitionsUsed;

    // 1. Assign icons to partitions
    AssignToPartitions(icons, partitions);

    // 2. Sort each partition independently
    SortAllPartitions(icons, partitions);

    // 3. Calculate layout positions
    LayoutAllPartitions(icons, partitions);

    // 4. Apply positions (preserves sr fields from above)
    SortResult applyResult = ApplyPositions(icons);
    sr.iconsSorted = applyResult.iconsSorted;
    sr.totalIcons = applyResult.totalIcons;

    return sr;
}

