// icon_manager.h : Desktop icon position management via IFolderView
#pragma once
#include <Windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include "../utils/com_ptr.h"

// Get desktop IFolderView via IServiceProvider::QueryService
HRESULT GetDesktopFolderView(IFolderView** ppFolderView);

// Save all icon positions to file
HRESULT SaveIconPositions(const wchar_t* filePath);

// Load icon positions from file
struct SavedPosition { wchar_t name[MAX_PATH]; POINT point; };
std::vector<SavedPosition> LoadIconPositions(const wchar_t* filePath);

// Restore icon positions from file
HRESULT RestoreIconPositions(const wchar_t* filePath);

// Get current desktop icon count
int GetDesktopIconCount();

// Full icon info for state export / LLM bridge
struct DesktopIcon {
    std::wstring name;
    std::wstring path;   // filesystem path, or ::{CLSID} for virtual items
    int x, y;
    bool isFolder;
};

// Enumerate all desktop icons with name, path, and position
std::vector<DesktopIcon> GetDesktopIcons();
