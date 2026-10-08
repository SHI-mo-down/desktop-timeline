// desktop_state.h : Export desktop state as LLM-readable JSON
#pragma once

// Write current desktop state (screen/grid/icons) as UTF-8 JSON to jsonPath,
// echo the JSON to stdout. Returns icon count, or -1 on failure.
int DesktopState_Export(const wchar_t* jsonPath);
