// layout_engine.h : Per-icon layout application (LLM bridge core)
// Reads a layout file in /dump JSON format (or a minimal subset LLMs write:
// {"icons":[{"name":"Chrome","col":2,"row":3},...]}) and moves icons.
#pragma once
#include <Windows.h>
#include <string>
#include <vector>

struct LayoutApplyResult {
    int moved;                        // icons successfully moved
    std::vector<std::wstring> missing;  // in layout, not on desktop
    std::vector<std::wstring> failed;   // on desktop but move failed
    std::vector<std::wstring> untouched; // on desktop, not in layout
    bool autoArrangeOn;               // warning: positions may not stick
};

// Apply a layout file. dryRun=true reports without moving anything.
LayoutApplyResult LayoutEngine_ApplyFile(const wchar_t* jsonPath, bool dryRun);

// Move one icon by display name to grid position (col, row).
// Returns false if the icon was not found.
bool LayoutEngine_MoveIcon(const wchar_t* name, int col, int row);
