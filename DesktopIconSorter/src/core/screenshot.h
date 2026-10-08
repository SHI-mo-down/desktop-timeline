// screenshot.h : Desktop screenshot to PNG via WIC (no external deps)
#pragma once

// Capture the virtual screen (all monitors) and save as PNG.
// Caller must have initialized COM. Returns true on success.
bool Desktop_Screenshot(const wchar_t* pngPath);
