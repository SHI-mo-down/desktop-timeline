// icon_manager.h : Desktop icon position management via IFolderView
#pragma once
#include <Windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include "../utils/com_ptr.h"

// Get desktop IFolderView via IServiceProvider::QueryService
HRESULT GetDesktopFolderView(IFolderView** ppFolderView);

// Full icon info for state export / LLM bridge
struct DesktopIcon {
    std::wstring name;
    std::wstring path;   // filesystem path, or ::{CLSID} for virtual items
    int x, y;
    bool isFolder;
};

// Enumerate all desktop icons with name, path, and position
std::vector<DesktopIcon> GetDesktopIcons();

// Redraw the desktop view (call after moving icons or changing settings)
void RefreshDesktopView();
