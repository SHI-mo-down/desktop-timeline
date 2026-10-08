// desktop_state.h : Export desktop state as LLM-readable JSON
#pragma once
#include <string>

// Build the current desktop state (screen/grid/settings/icons) as JSON text.
// Returns empty string on failure. Nothing is printed.
// outCount (optional) receives the icon count.
std::wstring DesktopState_BuildJson(int* outCount = nullptr);

// Write current desktop state as UTF-8 JSON to jsonPath, echo to stdout.
// Returns icon count, or -1 on failure.
int DesktopState_Export(const wchar_t* jsonPath);
