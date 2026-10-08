// desktop_state.cpp : Export desktop state as LLM-readable JSON
#include "desktop_state.h"
#include "icon_manager.h"
#include "grid_system.h"
#include "desktop_ops.h"
#include "../utils/console.h"
#include "../utils/json_extract.h"
#include <stdio.h>
#include <string>
#include <algorithm>

// UTF-8 without BOM (Python json.load / LLM tools parse it directly)
static bool WriteUtf8File(const wchar_t* path, const std::wstring& content) {
    int bytes = WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1,
                                    nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return false;
    std::string utf8(bytes - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, content.c_str(), -1, &utf8[0], bytes, nullptr, nullptr);

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(h, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(h);
    return written == utf8.size();
}

std::wstring DesktopState_BuildJson(int* outCount) {
    auto icons = GetDesktopIcons();
    if (icons.empty()) return L"";
    if (outCount) *outCount = (int)icons.size();

    std::sort(icons.begin(), icons.end(), [](const DesktopIcon& a, const DesktopIcon& b) {
        return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
    });

    SIZE cell = GridSystem_GetCellSize();
    POINT origin = GridSystem_GetOrigin();
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(nullptr, hdc);

    std::wstring j;
    j += L"{\n";
    j += L"  \"screen\": { \"width\": " + std::to_wstring(screenW)
       + L", \"height\": " + std::to_wstring(screenH)
       + L", \"dpi\": " + std::to_wstring(dpi) + L" },\n";
    j += L"  \"grid\": { \"cell_width\": " + std::to_wstring(cell.cx)
       + L", \"cell_height\": " + std::to_wstring(cell.cy)
       + L", \"origin_x\": " + std::to_wstring(origin.x)
       + L", \"origin_y\": " + std::to_wstring(origin.y)
       + L", \"columns\": " + std::to_wstring(cell.cx > 0 ? screenW / cell.cx : 0)
       + L", \"rows\": " + std::to_wstring(cell.cy > 0 ? screenH / cell.cy : 0)
       + L" },\n";
    int spacingH = 0, spacingV = 0;
    Desktop_GetSpacing(&spacingH, &spacingV);
    j += L"  \"settings\": { \"auto_arrange\": "
       + std::wstring(Desktop_GetAutoArrange() ? L"true" : L"false")
       + L", \"snap_to_grid\": "
       + std::wstring(Desktop_GetSnapToGrid() ? L"true" : L"false")
       + L", \"icon_size\": " + std::to_wstring(Desktop_GetIconSize())
       + L", \"spacing_h\": " + std::to_wstring(spacingH)
       + L", \"spacing_v\": " + std::to_wstring(spacingV)
       + L", \"system_icons\": {";
    auto sysIcons = Desktop_GetSystemIconStates();
    for (size_t i = 0; i < sysIcons.size(); i++) {
        j += L" \"" + Json_Escape(sysIcons[i].key) + L"\": "
           + std::wstring(sysIcons[i].visible ? L"true" : L"false")
           + (i + 1 < sysIcons.size() ? L"," : L"");
    }
    j += L" } },\n";

    j += L"  \"icons\": [\n";

    for (size_t i = 0; i < icons.size(); i++) {
        const auto& ic = icons[i];
        POINT pt = {ic.x, ic.y};
        int row = 0, col = 0;
        GridSystem_PixelToGrid(pt, &row, &col);

        j += L"    { \"name\": \"" + Json_Escape(ic.name)
           + L"\", \"path\": \"" + Json_Escape(ic.path)
           + L"\", \"x\": " + std::to_wstring(ic.x)
           + L", \"y\": " + std::to_wstring(ic.y)
           + L", \"col\": " + std::to_wstring(col)
           + L", \"row\": " + std::to_wstring(row)
           + L", \"folder\": " + (ic.isFolder ? L"true" : L"false")
           + L" }" + (i + 1 < icons.size() ? L"," : L"") + L"\n";
    }

    j += L"  ]\n}\n";
    return j;
}

int DesktopState_Export(const wchar_t* jsonPath) {
    int count = 0;
    std::wstring j = DesktopState_BuildJson(&count);
    if (j.empty()) return -1;
    Console_PrintW(j);
    if (!WriteUtf8File(jsonPath, j)) return -1;
    return count;
}
