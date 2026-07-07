// rule_engine.cpp : .dtrules JSON parser, glob matching, zone fill, COM apply
// Supports: wildcard name match, folder detection, exclude patterns,
//           in-zone sorting, icon spacing, multi-monitor
#include "rule_engine.h"
#include "icon_manager.h"
#include "../utils/com_ptr.h"
#include <shlwapi.h>
#include <stdio.h>
#include <algorithm>
#include <set>
#include <map>

// ═══════════════════════════════════════════
//  JSON Parser (hand-rolled, zero dependencies)
// ═══════════════════════════════════════════

static std::wstring ReadFile(const wchar_t* path) {
    std::wstring result;
    FILE* fp = nullptr;
    _wfopen_s(&fp, path, L"rb, ccs=UTF-8");
    if (!fp) return result;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* buf = new char[size + 1];
    size_t read = fread(buf, 1, size, fp);
    buf[read] = 0;
    fclose(fp);
    int wlen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, nullptr, 0);
    if (wlen > 0) {
        wchar_t* wbuf = new wchar_t[wlen];
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wbuf, wlen);
        result = wbuf;
        delete[] wbuf;
    }
    delete[] buf;
    return result;
}

static std::wstring ExtractString(const std::wstring& s, const wchar_t* key) {
    std::wstring search = std::wstring(L"\"") + key + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return L"";
    pos = s.find(L'"', pos + search.length());
    if (pos == std::wstring::npos) return L"";
    pos++;
    size_t end = s.find(L'"', pos);
    if (end == std::wstring::npos) return L"";
    return s.substr(pos, end - pos);
}

static int ExtractInt(const std::wstring& s, const wchar_t* key, int defaultVal = 0) {
    std::wstring search = std::wstring(L"\"") + key + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return defaultVal;
    pos = s.find(L':', pos);
    if (pos == std::wstring::npos) return defaultVal;
    pos++;
    while (pos < s.length() && (s[pos] == L' ' || s[pos] == L'\t' || s[pos] == L'\n' || s[pos] == L'\r')) pos++;
    return _wtoi(s.c_str() + pos);
}

static std::vector<std::wstring> ExtractStringArray(const std::wstring& s, const wchar_t* key) {
    std::vector<std::wstring> result;
    std::wstring search = std::wstring(L"\"") + key + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return result;
    pos = s.find(L'[', pos);
    if (pos == std::wstring::npos) return result;
    size_t end = s.find(L']', pos);
    if (end == std::wstring::npos) return result;
    pos++;
    while (pos < end) {
        size_t q1 = s.find(L'"', pos);
        if (q1 == std::wstring::npos || q1 >= end) break;
        size_t q2 = s.find(L'"', q1 + 1);
        if (q2 == std::wstring::npos || q2 >= end) break;
        result.push_back(s.substr(q1 + 1, q2 - q1 - 1));
        pos = q2 + 1;
    }
    return result;
}

static bool FindObjectBlock(const std::wstring& s, const wchar_t* label, size_t* outStart, size_t* outEnd) {
    std::wstring search = std::wstring(L"\"") + label + L"\"";
    size_t pos = s.find(search);
    if (pos == std::wstring::npos) return false;
    pos = s.find(L'[', pos);
    if (pos == std::wstring::npos) return false;
    *outStart = pos + 1;
    int depth = 1;
    size_t i = *outStart;
    while (i < s.length() && depth > 0) {
        if (s[i] == L'[') depth++;
        else if (s[i] == L']') depth--;
        if (depth > 0) i++;
    }
    *outEnd = i;
    return depth == 0;
}

static RuleZone ParseZone(const std::wstring& block, int defaultW, int defaultH) {
    RuleZone zone = {};
    zone.fillMode = L"column";
    zone.sortBy = L"none";
    zone.sortDir = L"asc";
    zone.spacingX = 0;
    zone.spacingY = 0;
    zone.monitorIndex = 0;
    zone.usePercent = true;
    zone.matchMode = L"extension";

    zone.name = ExtractString(block, L"name");

    int rawX = ExtractInt(block, L"x", 0);
    int rawY = ExtractInt(block, L"y", 0);
    int rawW = ExtractInt(block, L"w", 100);
    int rawH = ExtractInt(block, L"h", 100);

    std::wstring unit = ExtractString(block, L"unit");
    if (unit == L"pixel") {
        zone.x = rawX; zone.y = rawY;
        zone.w = rawW; zone.h = rawH;
        zone.usePercent = false;
    } else {
        zone.x = (int)((float)rawX / 100.0f * defaultW);
        zone.y = (int)((float)rawY / 100.0f * defaultH);
        zone.w = (int)((float)rawW / 100.0f * defaultW);
        zone.h = (int)((float)rawH / 100.0f * defaultH);
    }

    zone.matchPatterns  = ExtractStringArray(block, L"match");
    zone.excludePatterns = ExtractStringArray(block, L"exclude");

    std::wstring mm = ExtractString(block, L"match_mode");
    if (!mm.empty()) zone.matchMode = mm;

    std::wstring fm = ExtractString(block, L"fill");
    if (!fm.empty()) zone.fillMode = fm;

    std::wstring sb = ExtractString(block, L"sort");
    if (!sb.empty()) zone.sortBy = sb;

    std::wstring sd = ExtractString(block, L"sort_dir");
    if (!sd.empty()) zone.sortDir = sd;

    zone.spacingX = ExtractInt(block, L"spacing_x", 0);
    zone.spacingY = ExtractInt(block, L"spacing_y", 0);
    zone.monitorIndex = ExtractInt(block, L"monitor", 0);

    return zone;
}

// ═══════════════════════════════════════════
//  Glob / Wildcard matching
// ═══════════════════════════════════════════

static bool GlobMatch(const wchar_t* pattern, const wchar_t* str) {
    // Handle * wildcard: "screen*" "*.pdf" "*report*"
    while (*pattern && *str) {
        if (*pattern == L'*') {
            pattern++;
            if (!*pattern) return true; // trailing * matches everything
            // Find next occurrence of the char after *
            while (*str) {
                if (GlobMatch(pattern, str)) return true;
                str++;
            }
            return false;
        }
        if (*pattern == L'?') {
            pattern++; str++;
            continue;
        }
        if (towlower(*pattern) != towlower(*str)) return false;
        pattern++; str++;
    }
    // Both must be exhausted (or remaining pattern is all *)
    while (*pattern == L'*') pattern++;
    return *pattern == 0 && *str == 0;
}

// ═══════════════════════════════════════════
//  Matching logic
// ═══════════════════════════════════════════

static std::wstring GetExt(const std::wstring& name) {
    size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos) return L"";
    return name.substr(dot);
}

static bool MatchesPattern(const std::wstring& name, bool isFolder,
                            const std::wstring& pattern, const std::wstring& matchMode) {
    // Check exclude patterns first — caller handles this in zone loop
    if (matchMode == L"exact") {
        return _wcsicmp(pattern.c_str(), name.c_str()) == 0;
    }
    if (matchMode == L"folder") {
        return isFolder && GlobMatch(pattern.c_str(), name.c_str());
    }
    if (matchMode == L"name") {
        return GlobMatch(pattern.c_str(), name.c_str());
    }
    // Default: extension mode
    if (pattern == L"*") return true;
    if (pattern == L"<folder>" && isFolder) return true;
    std::wstring ext = GetExt(name);
    return _wcsicmp(ext.c_str(), pattern.c_str()) == 0;
}

static bool IsExcluded(const std::wstring& name, bool isFolder,
                        const std::vector<std::wstring>& excludePatterns) {
    for (const auto& pat : excludePatterns) {
        if (GlobMatch(pat.c_str(), name.c_str())) return true;
    }
    return false;
}

int RuleEngine_MatchIcon(const LayoutRules& rules, const std::wstring& iconName) {
    return RuleEngine_MatchIconEx(rules, iconName, false);
}

int RuleEngine_MatchIconEx(const LayoutRules& rules, const std::wstring& iconName, bool isFolder) {
    for (int i = 0; i < (int)rules.zones.size(); i++) {
        const auto& zone = rules.zones[i];

        // Exclude check
        if (!zone.excludePatterns.empty() &&
            IsExcluded(iconName, isFolder, zone.excludePatterns))
            continue;

        // Match check
        for (const auto& pattern : zone.matchPatterns) {
            if (MatchesPattern(iconName, isFolder, pattern, zone.matchMode))
                return i;
        }
    }

    // Fallback to default zone
    if (!rules.defaultZone.empty()) {
        for (int i = 0; i < (int)rules.zones.size(); i++) {
            if (_wcsicmp(rules.zones[i].name.c_str(), rules.defaultZone.c_str()) == 0)
                return i;
        }
    }
    return -1;
}

// ═══════════════════════════════════════════
//  Placement with spacing
// ═══════════════════════════════════════════

static bool CellInZone(int col, int row, int cellW, int cellH,
                        int spacingX, int spacingY, const RuleZone& zone) {
    int cx = col * (cellW + spacingX) + (cellW + spacingX) / 2;
    int cy = row * (cellH + spacingY) + (cellH + spacingY) / 2;
    return cx >= zone.x && cx < zone.x + zone.w &&
           cy >= zone.y && cy < zone.y + zone.h;
}

bool RuleEngine_PlaceInZone(
    const RuleZone& zone, int screenW, int screenH,
    int cellW, int cellH,
    const std::vector<POINT>& occupied, POINT* outPos)
{
    int stepX = cellW + zone.spacingX;
    int stepY = cellH + zone.spacingY;

    int startCol = zone.x / stepX;
    int endCol = (zone.x + zone.w) / stepX;
    int startRow = zone.y / stepY;
    int endRow = (zone.y + zone.h) / stepY;

    if (endCol <= startCol) endCol = startCol + 1;
    if (endRow <= startRow) endRow = startRow + 1;

    std::set<std::pair<int,int>> occSet;
    for (const auto& pt : occupied) {
        occSet.insert({ pt.y / stepY, pt.x / stepX });
    }

    auto tryCell = [&](int c, int r) -> bool {
        if (!CellInZone(c, r, cellW, cellH, zone.spacingX, zone.spacingY, zone)) return false;
        if (occSet.find({r, c}) != occSet.end()) return false;
        outPos->x = c * stepX;
        outPos->y = r * stepY;
        return true;
    };

    if (zone.fillMode == L"row") {
        for (int r = startRow; r <= endRow; r++)
            for (int c = startCol; c <= endCol; c++)
                if (tryCell(c, r)) return true;
    } else {
        // default "column": top-to-bottom, then next column
        for (int c = startCol; c <= endCol; c++)
            for (int r = startRow; r <= endRow; r++)
                if (tryCell(c, r)) return true;
    }

    outPos->x = zone.x;
    outPos->y = zone.y;
    return true;
}

// ═══════════════════════════════════════════
//  Apply: full rearrange with sorting + multi-monitor
// ═══════════════════════════════════════════

// Determine if a PIDL is a folder
static bool IsFolderPIDL(IShellFolder* sf, PITEMID_CHILD pidl) {
    DWORD attrs = SFGAO_FOLDER;
    if (SUCCEEDED(sf->GetAttributesOf(1, (PCUITEMID_CHILD*)&pidl, &attrs)))
        return (attrs & SFGAO_FOLDER) != 0;
    return false;
}

// Get file date/size for sorting (from path if available)
struct IconMeta {
    std::wstring name;
    FILETIME date;
    ULONGLONG size;
    PITEMID_CHILD pidl;
    POINT targetPos;
    bool isFolder; // Computed before sf is released
};

// Comparator for sorting
static bool IconLess(const IconMeta& a, const IconMeta& b,
                      const std::wstring& sortBy, bool ascending) {
    int cmp = 0;
    if (sortBy == L"name") {
        cmp = _wcsicmp(a.name.c_str(), b.name.c_str());
    } else if (sortBy == L"date") {
        cmp = CompareFileTime(&a.date, &b.date);
    } else if (sortBy == L"size") {
        if (a.size < b.size) cmp = -1;
        else if (a.size > b.size) cmp = 1;
    }
    return ascending ? (cmp < 0) : (cmp > 0);
}

int RuleEngine_Apply(
    const LayoutRules& rules, int screenW, int screenH,
    int cellW, int cellH,
    const RECT* monitorRects, int monitorCount)
{
    if (!rules.loaded || rules.zones.empty()) return 0;

    ComPtr<IFolderView> spFolderView;
    IShellFolder* sf = nullptr;
    IEnumIDList* spEnum = nullptr;

    if (FAILED(GetDesktopFolderView(&spFolderView))) return -1;
    if (FAILED(spFolderView->GetFolder(IID_IShellFolder, (void**)&sf))) return -1;
    if (FAILED(spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList, (void**)&spEnum))) {
        sf->Release(); return -1;
    }

    // Collect all icons with metadata
    std::vector<IconMeta> allIcons;

    PITEMID_CHILD pidl = nullptr;
    ULONG fetched = 0;
    while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
        if (pidl) {
            IconMeta meta = {};
            meta.pidl = pidl;
            meta.date = {};
            meta.size = 0;

            STRRET strret;
            wchar_t name[MAX_PATH] = L"";
            if (SUCCEEDED(sf->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret)))
                StrRetToBuf(&strret, pidl, name, MAX_PATH);
            meta.name = name;
            meta.isFolder = IsFolderPIDL(sf, pidl); // Compute before sf is released

            // Try to get file info for sorting
            wchar_t path[MAX_PATH] = L"";
            if (SUCCEEDED(SHGetPathFromIDListW(pidl, path)) && path[0]) {
                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(path, &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    meta.date = fd.ftLastWriteTime;
                    ULARGE_INTEGER sz;
                    sz.LowPart = fd.nFileSizeLow;
                    sz.HighPart = fd.nFileSizeHigh;
                    meta.size = sz.QuadPart;
                    FindClose(hFind);
                }
            }

            allIcons.push_back(meta);
            pidl = nullptr;
        }
    }
    spEnum->Release();
    sf->Release();  // Now safe — isFolder is cached in meta

    // Assign zones and compute target positions
    std::map<int, std::vector<IconMeta*>> zoneIcons;
    std::map<int, std::vector<POINT>> zoneOccupancy;

    for (auto& meta : allIcons) {
        int zoneIdx = RuleEngine_MatchIconEx(rules, meta.name, meta.isFolder);

        if (zoneIdx < 0) continue; // unmatched — don't move

        int monIdx = rules.zones[zoneIdx].monitorIndex;
        int monOffX = 0, monOffY = 0;
        if (monitorRects && monitorCount > 0 && monIdx < monitorCount) {
            monOffX = monitorRects[monIdx].left;
            monOffY = monitorRects[monIdx].top;
        }

        POINT target = {0, 0};
        if (RuleEngine_PlaceInZone(rules.zones[zoneIdx], screenW, screenH,
                cellW, cellH, zoneOccupancy[zoneIdx], &target)) {
            target.x += monOffX;
            target.y += monOffY;
            meta.targetPos = target;

            POINT cellPt = { target.x - monOffX, target.y - monOffY };
            zoneOccupancy[zoneIdx].push_back(cellPt);
            zoneIcons[zoneIdx].push_back(&meta);
        }
    }

    // Sort within each zone if requested
    for (auto& pair : zoneIcons) {
        int zi = pair.first;
        const auto& zone = rules.zones[zi];
        if (zone.sortBy != L"none" && pair.second.size() > 1) {
            bool asc = (zone.sortDir != L"desc");

            // Sort the metadata pointers
            std::sort(pair.second.begin(), pair.second.end(),
                [&](const IconMeta* a, const IconMeta* b) {
                    return IconLess(*a, *b, zone.sortBy, asc);
                });

            // Re-assign positions in sorted order
            zoneOccupancy[zi].clear();
            for (auto* meta : pair.second) {
                POINT target = {0, 0};
                RuleEngine_PlaceInZone(zone, screenW, screenH,
                    cellW, cellH, zoneOccupancy[zi], &target);

                int monOffX = 0, monOffY = 0;
                if (monitorRects && monitorCount > 0 && zone.monitorIndex < monitorCount) {
                    monOffX = monitorRects[zone.monitorIndex].left;
                    monOffY = monitorRects[zone.monitorIndex].top;
                }
                target.x += monOffX;
                target.y += monOffY;
                meta->targetPos = target;

                POINT cellPt = { target.x - monOffX, target.y - monOffY };
                zoneOccupancy[zi].push_back(cellPt);
            }
        }
    }

    // Apply all moves
    ComPtr<IFolderView> spFV2;
    if (FAILED(GetDesktopFolderView(&spFV2))) return -1;

    int moved = 0;
    for (auto& pair : zoneIcons) {
        for (auto* meta : pair.second) {
            if (SUCCEEDED(spFV2->SelectAndPositionItems(
                    1, (PCUITEMID_CHILD*)&meta->pidl,
                    &meta->targetPos, SVSI_POSITIONITEM)))
                moved++;
            CoTaskMemFree(meta->pidl);
        }
    }

    // Free unmatched icons' PIDLs
    for (auto& meta : allIcons) {
        if (meta.pidl) CoTaskMemFree(meta.pidl);
    }

    return moved;
}

// ═══════════════════════════════════════════
//  Load (public API)
// ═══════════════════════════════════════════

LayoutRules RuleEngine_Load(const wchar_t* filePath, int screenW, int screenH) {
    LayoutRules rules = {};
    if (GetFileAttributesW(filePath) == INVALID_FILE_ATTRIBUTES) return rules;

    std::wstring json = ReadFile(filePath);
    if (json.empty()) return rules;

    rules.name = ExtractString(json, L"name");
    rules.onStartup = ExtractString(json, L"on_startup");
    rules.onNewIcon = ExtractString(json, L"on_new_icon");
    rules.defaultZone = ExtractString(json, L"default_zone");

    size_t start, end;
    if (FindObjectBlock(json, L"zones", &start, &end)) {
        size_t pos = start;
        while (pos < end) {
            size_t brace = json.find(L'{', pos);
            if (brace == std::wstring::npos || brace >= end) break;
            int depth = 1;
            size_t close = brace + 1;
            while (close < end && depth > 0) {
                if (json[close] == L'{') depth++;
                else if (json[close] == L'}') depth--;
                close++;
            }
            if (depth != 0) break;
            RuleZone zone = ParseZone(json.substr(brace, close - brace), screenW, screenH);
            if (!zone.name.empty()) rules.zones.push_back(zone);
            pos = close;
        }
    }

    rules.loaded = true;
    return rules;
}
