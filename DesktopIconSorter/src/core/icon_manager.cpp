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

void RefreshDesktopView() {
    HWND hwnd = FindWindowW(L"Progman", L"Program Manager");
    if (hwnd) { InvalidateRect(hwnd, nullptr, TRUE); UpdateWindow(hwnd); }
}

std::vector<DesktopIcon> GetDesktopIcons() {
    std::vector<DesktopIcon> icons;

    ComPtr<IFolderView> spFolderView;
    if (FAILED(GetDesktopFolderView(&spFolderView))) return icons;

    IShellFolder* sf = nullptr;
    if (FAILED(spFolderView->GetFolder(IID_IShellFolder, (void**)&sf))) return icons;

    IEnumIDList* spEnum = nullptr;
    if (FAILED(spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList, (void**)&spEnum))) {
        sf->Release();
        return icons;
    }

    PITEMID_CHILD pidl = nullptr;
    ULONG fetched = 0;
    while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
        if (pidl) {
            DesktopIcon icon = {};
            STRRET strret;

            wchar_t name[MAX_PATH] = L"";
            if (SUCCEEDED(sf->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret)))
                StrRetToBuf(&strret, pidl, name, MAX_PATH);
            icon.name = name;

            wchar_t path[MAX_PATH] = L"";
            if (SUCCEEDED(sf->GetDisplayNameOf(pidl, SHGDN_FORPARSING, &strret)))
                StrRetToBuf(&strret, pidl, path, MAX_PATH);
            icon.path = path;

            POINT pt = {0, 0};
            spFolderView->GetItemPosition(pidl, &pt);
            icon.x = pt.x;
            icon.y = pt.y;

            DWORD attrs = SFGAO_FOLDER;
            sf->GetAttributesOf(1, (PCUITEMID_CHILD*)&pidl, &attrs);
            icon.isFolder = (attrs & SFGAO_FOLDER) != 0;

            icons.push_back(icon);
            CoTaskMemFree(pidl);
            pidl = nullptr;
        }
    }

    spEnum->Release();
    sf->Release();
    return icons;
}
