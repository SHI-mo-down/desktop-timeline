// rule_engine.h : Loads .dtrules layout rule files, matches icons to zones, applies placement
#pragma once
#include <Windows.h>
#include <vector>
#include <string>

struct RuleZone {
    std::wstring name;

    // Position/size — percent (0-100) or absolute pixels
    int x, y, w, h;
    bool usePercent;

    // Matching
    std::vector<std::wstring> matchPatterns;  // ".lnk", "screen*", "*report*"
    std::vector<std::wstring> excludePatterns; // patterns to exclude even if matched
    std::wstring matchMode;    // "extension" (default), "name", "folder"

    // Placement
    std::wstring fillMode;    // "column" (top→bottom then next col), "row" (left→right then next row)
    int spacingX, spacingY;   // Extra pixels between icons (0 = system default)

    // Sorting within zone
    std::wstring sortBy;      // "none" (keep original order), "name", "date", "size"
    std::wstring sortDir;     // "asc" or "desc"

    // Multi-monitor
    int monitorIndex;         // 0 = primary, 1-... = secondary
};

struct LayoutRules {
    std::wstring name;
    std::vector<RuleZone> zones;
    std::wstring defaultZone;
    std::wstring onStartup;   // "apply" | "none"
    std::wstring onNewIcon;   // "place" | "ignore"
    bool loaded;
};

// ── Parsing ──
LayoutRules RuleEngine_Load(const wchar_t* filePath, int screenW, int screenH);

// ── Matching ──
int RuleEngine_MatchIcon(const LayoutRules& rules, const std::wstring& iconName);
int RuleEngine_MatchIconEx(const LayoutRules& rules, const std::wstring& iconName, bool isFolder);

// ── Placement ──
bool RuleEngine_PlaceInZone(
    const RuleZone& zone, int screenW, int screenH, int cellW, int cellH,
    const std::vector<POINT>& occupied, POINT* outPos);

// ── Apply ──
// Returns number of icons moved, or -1 on COM error.
// screenRects: array of monitor rects (for multi-monitor offset)
int RuleEngine_Apply(
    const LayoutRules& rules,
    int screenW, int screenH, int cellW, int cellH,
    const RECT* monitorRects, int monitorCount
);
