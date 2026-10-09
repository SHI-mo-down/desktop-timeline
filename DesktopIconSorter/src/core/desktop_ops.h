// desktop_ops.h : Desktop settings operations for the LLM bridge
// Auto-arrange, snap-to-grid, icon size, spacing, system icons, shortcuts,
// rename/delete of individual items.
#pragma once
#include <Windows.h>
#include <string>
#include <vector>

// ── Query ──
bool Desktop_GetAutoArrange();
bool Desktop_GetSnapToGrid();
int  Desktop_GetIconSize();               // pixels (32/48/96...)
void Desktop_GetSpacing(int* h, int* v);  // pixels

struct SystemIconState { std::wstring key; bool visible; };
std::vector<SystemIconState> Desktop_GetSystemIconStates();

// ── Set ──
bool Desktop_SetAutoArrange(bool on);
bool Desktop_SetSnapToGrid(bool on);
bool Desktop_SetIconSize(int px);
bool Desktop_SetSpacing(int h, int v);
bool Desktop_SetSystemIcon(const wchar_t* key, bool visible);  // thispc|recycle|network|controlpanel|userfiles

// ── Shortcuts ──
// name may omit ".lnk". args/workDir/iconPath may be nullptr or empty.
bool Desktop_CreateShortcut(const wchar_t* name, const wchar_t* target,
                            const wchar_t* args, const wchar_t* workDir,
                            const wchar_t* iconPath);

// ── Item ops ──
// Rename a desktop item by display name. The extension is kept when the
// new name has none of its own ("notes" keeps .txt); system icons and the
// user profile root are refused.
bool Desktop_RenameIcon(const wchar_t* name, const wchar_t* newName);
// Delete a desktop item by display name: moves it to the recycle bin
// (undoable). System icons and the profile root are refused.
bool Desktop_DeleteIcon(const wchar_t* name);
