// icon_manager.cpp : IFolderView-based icon position management
#include "icon_manager.h"
#include <shlwapi.h>
#include <exdisp.h>
#include <shobjidl.h>
#include <stdio.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

// Get desktop IFolderView via IServiceProvider::QueryService(SID_SFolderView)
HRESULT GetDesktopFolderView(IFolderView** ppFolderView) {
    if (!ppFolderView) return E_INVALIDARG;
    *ppFolderView = nullptr;

    IShellWindows* pShellWindows = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr,
                                   CLSCTX_ALL, IID_IShellWindows,
                                   reinterpret_cast<void**>(&pShellWindows));
    if (FAILED(hr)) return hr;

    VARIANT vtEmpty = {};
    VariantInit(&vtEmpty);
    IDispatch* pDispatch = nullptr;
    long lhwnd;

    hr = pShellWindows->FindWindowSW(&vtEmpty, &vtEmpty, SWC_DESKTOP, &lhwnd,
                                      SWFO_NEEDDISPATCH, &pDispatch);
    pShellWindows->Release();
    if (FAILED(hr) || !pDispatch) return hr;

    // Key: IServiceProvider::QueryService
    IServiceProvider* pServiceProvider = nullptr;
    hr = pDispatch->QueryInterface(IID_IServiceProvider,
                                    reinterpret_cast<void**>(&pServiceProvider));
    pDispatch->Release();
    if (FAILED(hr) || !pServiceProvider) return hr;

    hr = pServiceProvider->QueryService(SID_SFolderView, IID_IFolderView,
                                         reinterpret_cast<void**>(ppFolderView));
    pServiceProvider->Release();
    return hr;
}

int GetDesktopIconCount() {
    ComPtr<IFolderView> spFolderView;
    if (FAILED(GetDesktopFolderView(&spFolderView))) return -1;

    int count = 0;
    spFolderView->ItemCount(SVGIO_ALLVIEW, &count);
    return count;
}

HRESULT SaveIconPositions(const wchar_t* filePath) {
    ComPtr<IFolderView> spFolderView;
    HRESULT hr = GetDesktopFolderView(&spFolderView);
    if (FAILED(hr)) return hr;

    IShellFolder* spShellFolder = nullptr;
    hr = spFolderView->GetFolder(IID_IShellFolder,
                                  reinterpret_cast<void**>(&spShellFolder));
    if (FAILED(hr)) return hr;

    int itemCount = 0;
    spFolderView->ItemCount(SVGIO_ALLVIEW, &itemCount);

    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"w, ccs=UTF-8");
    if (!fp) {
        spShellFolder->Release();
        return E_FAIL;
    }

    if (fwprintf(fp, L"Count:%d\n", itemCount) < 0) {
        spShellFolder->Release();
        fclose(fp);
        return E_FAIL;
    }

    IEnumIDList* spEnum = nullptr;
    hr = spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList,
                              reinterpret_cast<void**>(&spEnum));
    int savedCount = 0;
    if (SUCCEEDED(hr) && spEnum) {
        PITEMID_CHILD pidl = nullptr;
        ULONG fetched = 0;
        while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
            if (pidl) {
                STRRET strret;
                wchar_t name[MAX_PATH] = L"Unknown";
                hr = spShellFolder->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret);
                if (SUCCEEDED(hr)) StrRetToBuf(&strret, pidl, name, MAX_PATH);

                for (wchar_t* p = name; *p; p++) {
                    if (*p == L'\n' || *p == L'\r' || *p == L'|') *p = L'_';
                }

                POINT pt = {0, 0};
                if (SUCCEEDED(spFolderView->GetItemPosition(pidl, &pt))) {
                    fwprintf(fp, L"%s|%d|%d\n", name, pt.x, pt.y);
                    savedCount++;
                }
                CoTaskMemFree(pidl);
                pidl = nullptr;
            }
        }
        spEnum->Release();
    }

    fclose(fp);
    spShellFolder->Release();
    return (savedCount > 0) ? S_OK : S_FALSE;
}

std::vector<SavedPosition> LoadIconPositions(const wchar_t* filePath) {
    std::vector<SavedPosition> positions;

    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"r, ccs=UTF-8");
    if (!fp) return positions;

    wchar_t line[1024];
    while (fgetws(line, 1024, fp)) {
        if (line[0] == L'#' || wcsncmp(line, L"Count:", 6) == 0) continue;

        wchar_t* context = nullptr;
        wchar_t* name = wcstok(line, L"|", &context);
        wchar_t* xStr = wcstok(nullptr, L"|", &context);
        wchar_t* yStr = wcstok(nullptr, L"\r\n", &context);

        if (name && xStr && yStr) {
            SavedPosition pos;
            wcsncpy_s(pos.name, name, MAX_PATH - 1);
            pos.point.x = _wtoi(xStr);
            pos.point.y = _wtoi(yStr);
            positions.push_back(pos);
        }
    }
    fclose(fp);
    return positions;
}

HRESULT RestoreIconPositions(const wchar_t* filePath) {
    auto saved = LoadIconPositions(filePath);
    if (saved.empty()) return E_FAIL;

    ComPtr<IFolderView> spFolderView;
    HRESULT hr = GetDesktopFolderView(&spFolderView);
    if (FAILED(hr)) return hr;

    IShellFolder* spShellFolder = nullptr;
    hr = spFolderView->GetFolder(IID_IShellFolder,
                                  reinterpret_cast<void**>(&spShellFolder));
    if (FAILED(hr)) return hr;

    IEnumIDList* spEnum = nullptr;
    hr = spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList,
                              reinterpret_cast<void**>(&spEnum));
    int restored = 0;
    if (SUCCEEDED(hr) && spEnum) {
        PITEMID_CHILD pidl = nullptr;
        ULONG fetched = 0;
        while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
            if (pidl) {
                STRRET strret;
                wchar_t name[MAX_PATH] = L"";
                if (SUCCEEDED(spShellFolder->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret))) {
                    StrRetToBuf(&strret, pidl, name, MAX_PATH);
                }

                for (const auto& s : saved) {
                    if (wcscmp(name, s.name) == 0) {
                        POINT pt = s.point;
                        if (SUCCEEDED(spFolderView->SelectAndPositionItems(
                                1, const_cast<PCUITEMID_CHILD*>(&pidl),
                                &pt, SVSI_POSITIONITEM))) {
                            restored++;
                        }
                        break;
                    }
                }
                CoTaskMemFree(pidl);
                pidl = nullptr;
            }
        }
        spEnum->Release();
    }

    spShellFolder->Release();
    return (restored > 0) ? S_OK : S_FALSE;
}
