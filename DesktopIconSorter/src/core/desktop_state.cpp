// desktop_state.cpp : Export desktop state as LLM-readable JSON
#include "desktop_state.h"
#include "icon_manager.h"
#include "grid_system.h"
#include "../utils/console.h"
#include <stdio.h>
#include <string>
#include <algorithm>

static std::wstring JsonEscape(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        switch (c) {
        case L'"':  out += L"\\\""; break;
        case L'\\': out += L"\\\\"; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default:
            if (c < 0x20) { wchar_t buf[8]; wsprintfW(buf, L"\\u%04x", c); out += buf; }
            else out += c;
        }
    }
    return out;
}

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

int DesktopState_Export(const wchar_t* jsonPath) {
    auto icons = GetDesktopIcons();
    if (icons.empty()) return -1;

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
    j += L"  \"icons\": [\n";

    for (size_t i = 0; i < icons.size(); i++) {
        const auto& ic = icons[i];
        POINT pt = {ic.x, ic.y};
        int row = 0, col = 0;
        GridSystem_PixelToGrid(pt, &row, &col);

        j += L"    { \"name\": \"" + JsonEscape(ic.name)
           + L"\", \"path\": \"" + JsonEscape(ic.path)
           + L"\", \"x\": " + std::to_wstring(ic.x)
           + L", \"y\": " + std::to_wstring(ic.y)
           + L", \"col\": " + std::to_wstring(col)
           + L", \"row\": " + std::to_wstring(row)
           + L", \"folder\": " + (ic.isFolder ? L"true" : L"false")
           + L" }" + (i + 1 < icons.size() ? L"," : L"") + L"\n";
    }

    j += L"  ]\n}\n";

    Console_PrintW(j);

    if (!WriteUtf8File(jsonPath, j)) return -1;
    return (int)icons.size();
}
