// layout_engine.h : Per-icon layout application (LLM bridge core)
// Reads a layout file in /dump JSON format (or a minimal subset LLMs write:
// {"icons":[{"name":"Chrome","col":2,"row":3},...]}) and moves icons.
#pragma once
#include <Windows.h>
#include <string>
#include <utility>
#include <vector>

struct LayoutApplyResult {
    int moved;                        // icons successfully moved
    std::vector<std::wstring> missing;  // in layout, not on desktop
    std::vector<std::wstring> failed;   // on desktop but move failed
    std::vector<std::wstring> untouched; // on desktop, not in layout
    bool autoArrangeOn;               // warning: positions may not stick
};

// Apply a layout. dryRun=true reports without moving anything.
// ApplyJson takes the layout as text (must contain an "icons" array);
// ApplyFile reads that text from a file (dump JSON format or minimal subset).
LayoutApplyResult LayoutEngine_ApplyJson(const std::wstring& layoutJson, bool dryRun);
LayoutApplyResult LayoutEngine_ApplyFile(const wchar_t* jsonPath, bool dryRun);

// Move one icon by display name to grid position (col, row).
// Returns false if the icon was not found.
bool LayoutEngine_MoveIcon(const wchar_t* name, int col, int row);

// Move icons to exact pixels by display name (case-insensitive), the loop
// shared by snapshot restores. No clamping: stored positions are restored
// as-is. Returns icons moved, -1 if COM setup failed.
int LayoutEngine_ApplyPairs(const std::vector<std::pair<std::wstring, POINT>>& targets);
