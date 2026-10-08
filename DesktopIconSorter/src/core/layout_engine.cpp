// layout_engine.cpp : Per-icon layout application (LLM bridge core)
#include "layout_engine.h"
#include "icon_manager.h"
#include "grid_system.h"
#include "desktop_ops.h"
#include "../utils/com_ptr.h"
#include "../utils/json_extract.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <map>
#include <algorithm>

// Build name → PIDL map of all desktop icons. Caller frees PIDLs.
static std::map<std::wstring, PITEMID_CHILD> BuildPidlMap() {
    std::map<std::wstring, PITEMID_CHILD> m;
    ComPtr<IFolderView> fv;
    if (FAILED(GetDesktopFolderView(&fv))) return m;
    IShellFolder* sf = nullptr;
    if (FAILED(fv->GetFolder(IID_IShellFolder, (void**)&sf))) return m;
    IEnumIDList* en = nullptr;
    if (FAILED(fv->Items(SVGIO_ALLVIEW, IID_IEnumIDList, (void**)&en))) {
        sf->Release();
        return m;
    }
    PITEMID_CHILD pidl = nullptr;
    ULONG fetched = 0;
    while (en->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
        if (pidl) {
            STRRET sr;
            wchar_t name[MAX_PATH] = L"";
            if (SUCCEEDED(sf->GetDisplayNameOf(pidl, SHGDN_NORMAL, &sr)))
                StrRetToBuf(&sr, pidl, name, MAX_PATH);
            if (name[0] == 0) {
                CoTaskMemFree(pidl);
            } else {
                // ponytail: duplicate display names → last one wins
                auto it = m.find(name);
                if (it != m.end()) CoTaskMemFree(it->second);
                m[name] = pidl;
            }
        }
    }
    en->Release();
    sf->Release();
    return m;
}

// Case-insensitive lookup (LLM may change name casing)
static PITEMID_CHILD FindPidl(std::map<std::wstring, PITEMID_CHILD>& m,
                              const wchar_t* name) {
    for (auto& [k, v] : m)
        if (_wcsicmp(k.c_str(), name) == 0) return v;
    return nullptr;
}

static POINT ClampToScreen(POINT pt) {
    // Virtual screen: supports secondary monitors (negative or offset coords)
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw < 50) { vx = 0; vw = 1920; }
    if (vh < 50) { vy = 0; vh = 1080; }
    if (pt.x < vx) pt.x = vx;
    if (pt.y < vy) pt.y = vy;
    if (pt.x > vx + vw - 40) pt.x = vx + vw - 40;
    if (pt.y > vy + vh - 40) pt.y = vy + vh - 40;
    return pt;
}

// Parse the icons array: name + (col,row | x,y)
static std::vector<std::pair<std::wstring, POINT>> ParseLayout(const std::wstring& text) {
    std::vector<std::pair<std::wstring, POINT>> out;
    if (text.empty()) return out;

    size_t start, end;
    if (!Json_FindArrayBlock(text, L"icons", &start, &end)) return out;

    for (auto [a, b] : Json_SplitObjects(text, start, end)) {
        std::wstring obj = text.substr(a, b - a);
        std::wstring name = Json_ExtractString(obj, L"name");
        if (name.empty()) continue;

        POINT pt = {-1, -1};
        bool hasPos = false; // pt.x can legitimately be negative (left monitor)
        // Exact pixels win when present (roundtrip fidelity); col/row is the
        // fallback for LLM-written grid layouts and cross-machine mapping
        int x = Json_ExtractInt(obj, L"x", -9999);
        int y = Json_ExtractInt(obj, L"y", -9999);
        if (x != -9999 && y != -9999) {
            pt = ClampToScreen({x, y});
            hasPos = true;
        } else {
            int col = Json_ExtractInt(obj, L"col", -9999);
            int row = Json_ExtractInt(obj, L"row", -9999);
            if (col != -9999 || row != -9999) {
                POINT gp;
                if (GridSystem_GridToPixel(row < 0 ? 0 : row, col < 0 ? 0 : col, &gp)) {
                    pt = ClampToScreen(gp);
                    hasPos = true;
                }
            }
        }
        if (!hasPos) continue;
        out.push_back({name, pt});
    }
    return out;
}

LayoutApplyResult LayoutEngine_ApplyJson(const std::wstring& layoutJson, bool dryRun) {
    LayoutApplyResult result = {};
    result.autoArrangeOn = Desktop_GetAutoArrange();

    auto targets = ParseLayout(layoutJson);
    if (targets.empty()) return result;

    // Desktop icon names for untouched/missing reporting
    auto desktopIcons = GetDesktopIcons();
    std::vector<std::wstring> desktopNames;
    for (const auto& d : desktopIcons) desktopNames.push_back(d.name);

    auto pidlMap = BuildPidlMap();
    if (pidlMap.empty()) return result;

    ComPtr<IFolderView> fv;
    if (FAILED(GetDesktopFolderView(&fv))) {
        for (auto& [_, pidl] : pidlMap) CoTaskMemFree(pidl);
        return result;
    }

    std::vector<std::wstring> targetNames;
    for (const auto& [name, pt] : targets) {
        targetNames.push_back(name);
        PITEMID_CHILD pidl = FindPidl(pidlMap, name.c_str());
        if (!pidl) {
            result.missing.push_back(name);
            continue;
        }
        if (!dryRun) {
            POINT p = pt;
            if (SUCCEEDED(fv->SelectAndPositionItems(
                    1, (PCUITEMID_CHILD*)&pidl, &p, SVSI_POSITIONITEM)))
                result.moved++;
            else
                result.failed.push_back(name);
        } else {
            result.moved++;
        }
    }

    for (auto& [_, pidl] : pidlMap) CoTaskMemFree(pidl);

    // Untouched = on desktop but not in layout (case-insensitive)
    for (const auto& dn : desktopNames) {
        bool inLayout = false;
        for (const auto& tn : targetNames)
            if (_wcsicmp(dn.c_str(), tn.c_str()) == 0) { inLayout = true; break; }
        if (!inLayout) result.untouched.push_back(dn);
    }
    std::sort(result.untouched.begin(), result.untouched.end(),
              [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return result;
}

LayoutApplyResult LayoutEngine_ApplyFile(const wchar_t* jsonPath, bool dryRun) {
    return LayoutEngine_ApplyJson(Json_ReadFileUtf8(jsonPath), dryRun);
}

bool LayoutEngine_MoveIcon(const wchar_t* name, int col, int row) {
    if (!name || !name[0] || col < 0 || row < 0) return false;
    POINT gp;
    if (!GridSystem_GridToPixel(row, col, &gp)) return false;
    gp = ClampToScreen(gp);

    auto pidlMap = BuildPidlMap();
    PITEMID_CHILD pidl = FindPidl(pidlMap, name);
    if (!pidl) {
        for (auto& [_, pidl] : pidlMap) CoTaskMemFree(pidl);
        return false;
    }

    ComPtr<IFolderView> fv;
    HRESULT hr = GetDesktopFolderView(&fv);
    if (SUCCEEDED(hr))
        hr = fv->SelectAndPositionItems(1, (PCUITEMID_CHILD*)&pidl, &gp, SVSI_POSITIONITEM);

    for (auto& [_, pidl] : pidlMap) CoTaskMemFree(pidl);
    RefreshDesktopView();
    return SUCCEEDED(hr);
}
