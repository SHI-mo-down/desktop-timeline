// shellext.cpp : IExplorerCommand + IEnumExplorerCommand implementation
#define INITGUID
#include "shellext.h"
#include <stdio.h>
#include <shellapi.h>
#include <shlwapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

static HMODULE g_hModule = nullptr;
static LONG g_moduleRefCount = 0;
static WCHAR g_exeDir[MAX_PATH] = {};

const wchar_t* g_titles[4] = {
    L"By Name (Zone)", L"By Type (Zone)", L"By Size (Zone)", L"By Date (Zone)",
};

LONG GetModuleRefCount() { return g_moduleRefCount; }
const wchar_t* GetDllPath() { return g_exeDir; }
void SetDllPath(const wchar_t* p) { wcscpy_s(g_exeDir, MAX_PATH, p); }
void IncModuleRef() { InterlockedIncrement(&g_moduleRefCount); }
void DecModuleRef() { InterlockedDecrement(&g_moduleRefCount); }

// ======== SortActionCommand ========
SortActionCommand::SortActionCommand(int mode) : m_refCount(1), m_mode(mode) { IncModuleRef(); }
SortActionCommand::~SortActionCommand() { DecModuleRef(); }

STDMETHODIMP SortActionCommand::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER; *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IExplorerCommand))
        *ppv = static_cast<IExplorerCommand*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
STDMETHODIMP_(ULONG) SortActionCommand::AddRef() { return InterlockedIncrement(&m_refCount); }
STDMETHODIMP_(ULONG) SortActionCommand::Release() {
    ULONG c = InterlockedDecrement(&m_refCount);
    if (c == 0) delete this;
    return c;
}
STDMETHODIMP SortActionCommand::GetTitle(IShellItemArray*, LPWSTR* ppszTitle) {
    return SHStrDupW(g_titles[m_mode], ppszTitle);
}
STDMETHODIMP SortActionCommand::GetIcon(IShellItemArray*, LPWSTR* ppszIcon) {
    *ppszIcon = nullptr; return E_NOTIMPL;
}
STDMETHODIMP SortActionCommand::GetToolTip(IShellItemArray*, LPWSTR* ppszToolTip) {
    return SHStrDupW(L"Sort desktop icons within their configured zones", ppszToolTip);
}
STDMETHODIMP SortActionCommand::GetCanonicalName(GUID*) { return E_NOTIMPL; }
STDMETHODIMP SortActionCommand::GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pState) {
    *pState = ECS_ENABLED; return S_OK;
}
STDMETHODIMP SortActionCommand::Invoke(IShellItemArray*, IBindCtx*) {
    wchar_t cmd[MAX_PATH], arg[32];
    wsprintfW(arg, L"/sort:%d", m_mode);
    wsprintfW(cmd, L"%sDesktopIconSorter.exe", g_exeDir);
    ShellExecuteW(nullptr, L"open", cmd, arg, nullptr, SW_HIDE);
    return S_OK;
}
STDMETHODIMP SortActionCommand::GetFlags(EXPCMDFLAGS* pFlags) {
    *pFlags = ECF_DEFAULT; return S_OK;
}
STDMETHODIMP SortActionCommand::EnumSubCommands(IEnumExplorerCommand** ppEnum) {
    *ppEnum = nullptr; return E_NOTIMPL;
}

// ======== ZoneSortRootCommand ========
ZoneSortRootCommand::ZoneSortRootCommand() : m_refCount(1), m_enumIndex(0) { IncModuleRef(); }
ZoneSortRootCommand::~ZoneSortRootCommand() { DecModuleRef(); }

STDMETHODIMP ZoneSortRootCommand::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER; *ppv = nullptr;
    if (IsEqualIID(riid, IID_IUnknown))
        *ppv = static_cast<IExplorerCommand*>(this);
    else if (IsEqualIID(riid, IID_IExplorerCommand))
        *ppv = static_cast<IExplorerCommand*>(this);
    else if (IsEqualIID(riid, IID_IEnumExplorerCommand))
        *ppv = static_cast<IEnumExplorerCommand*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
STDMETHODIMP_(ULONG) ZoneSortRootCommand::AddRef() { return InterlockedIncrement(&m_refCount); }
STDMETHODIMP_(ULONG) ZoneSortRootCommand::Release() {
    ULONG c = InterlockedDecrement(&m_refCount);
    if (c == 0) delete this;
    return c;
}
STDMETHODIMP ZoneSortRootCommand::GetTitle(IShellItemArray*, LPWSTR* ppszTitle) {
    return SHStrDupW(L"Zone Sort", ppszTitle);
}
STDMETHODIMP ZoneSortRootCommand::GetIcon(IShellItemArray*, LPWSTR* ppszIcon) {
    *ppszIcon = nullptr; return E_NOTIMPL;
}
STDMETHODIMP ZoneSortRootCommand::GetToolTip(IShellItemArray*, LPWSTR* ppszToolTip) {
    return SHStrDupW(L"Sort desktop icons by zone partitions", ppszToolTip);
}
STDMETHODIMP ZoneSortRootCommand::GetCanonicalName(GUID*) { return E_NOTIMPL; }
STDMETHODIMP ZoneSortRootCommand::GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pState) {
    *pState = ECS_ENABLED; return S_OK;
}
STDMETHODIMP ZoneSortRootCommand::Invoke(IShellItemArray*, IBindCtx*) {
    return S_OK;
}
STDMETHODIMP ZoneSortRootCommand::GetFlags(EXPCMDFLAGS* pFlags) {
    *pFlags = ECF_HASSUBCOMMANDS; return S_OK;
}
STDMETHODIMP ZoneSortRootCommand::EnumSubCommands(IEnumExplorerCommand** ppEnum) {
    // Reset and return self as enumerator
    Reset();
    return QueryInterface(IID_IEnumExplorerCommand, (void**)ppEnum);
}
STDMETHODIMP ZoneSortRootCommand::Next(ULONG celt, IExplorerCommand** ppCmd, ULONG* pceltFetched) {
    if (ppCmd) *ppCmd = nullptr;
    if (pceltFetched) *pceltFetched = 0;
    if (m_enumIndex >= 4) return S_FALSE;

    ULONG count = 0;
    for (ULONG i = 0; i < celt && m_enumIndex < 4; i++) {
        ppCmd[i] = static_cast<IExplorerCommand*>(new SortActionCommand((int)m_enumIndex));
        m_enumIndex++;
        count++;
    }
    if (pceltFetched) *pceltFetched = count;
    return S_OK;
}
STDMETHODIMP ZoneSortRootCommand::Skip(ULONG celt) {
    m_enumIndex += celt; return S_OK;
}
STDMETHODIMP ZoneSortRootCommand::Reset() {
    m_enumIndex = 0; return S_OK;
}
STDMETHODIMP ZoneSortRootCommand::Clone(IEnumExplorerCommand** ppEnum) {
    if (!ppEnum) return E_POINTER;
    auto* p = new ZoneSortRootCommand();
    p->m_enumIndex = m_enumIndex;
    *ppEnum = static_cast<IEnumExplorerCommand*>(p);
    return S_OK;
}

// ======== Class Factory ========
class RootCommandFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER; *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() { return 2; }
    STDMETHODIMP_(ULONG) Release() { return 1; }
    STDMETHODIMP CreateInstance(IUnknown* punkOuter, REFIID riid, void** ppv) {
        if (punkOuter) return CLASS_E_NOAGGREGATION;
        ZoneSortRootCommand* p = new ZoneSortRootCommand();
        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }
    STDMETHODIMP LockServer(BOOL fLock) {
        if (fLock) IncModuleRef(); else DecModuleRef();
        return S_OK;
    }
};
static RootCommandFactory g_factory;

// ======== DLL Exports ========
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (IsEqualCLSID(rclsid, CLSID_ZoneSortRoot))
        return g_factory.QueryInterface(riid, ppv);
    return CLASS_E_CLASSNOTAVAILABLE;
}

STDAPI DllCanUnloadNow() {
    return (g_moduleRefCount == 0) ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    GetModuleFileNameW(g_hModule, g_exeDir, MAX_PATH);
    wchar_t* s = wcsrchr(g_exeDir, L'\\');
    if (s) *(s + 1) = 0;

    const wchar_t* CLSID = L"{A1B2C3D4-E5F6-7890-ABCD-EF1234567800}";
    WCHAR key[512]; HKEY hKey;

    wsprintfW(key, L"CLSID\\%s", CLSID);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"Zone Sort Menu", 28);
    RegCloseKey(hKey);

    wchar_t fullDllPath[MAX_PATH];
    wsprintfW(fullDllPath, L"%sShellExt.dll", g_exeDir);
    wsprintfW(key, L"CLSID\\%s\\InprocServer32", CLSID);
    RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)fullDllPath, (DWORD)(wcslen(fullDllPath) + 1) * 2);
    RegSetValueExW(hKey, L"ThreadingModel", 0, REG_SZ, (BYTE*)L"Apartment", 20);
    RegCloseKey(hKey);

    wsprintfW(key, L"Directory\\Background\\shell\\ZoneSort");
    RegCreateKeyExW(HKEY_CLASSES_ROOT, key, 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)L"Zone Sort", 20);
    RegSetValueExW(hKey, L"ExplorerCommandHandler", 0, REG_SZ, (BYTE*)CLSID, (DWORD)(wcslen(CLSID) + 1) * 2);
    RegCloseKey(hKey);

    return S_OK;
}

STDAPI DllUnregisterServer() {
    const wchar_t* CLSID = L"{A1B2C3D4-E5F6-7890-ABCD-EF1234567800}";
    WCHAR key[512];

    wsprintfW(key, L"Directory\\Background\\shell\\ZoneSort");
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key);

    wsprintfW(key, L"CLSID\\%s", CLSID);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, key);

    return S_OK;
}

BOOL APIENTRY DllMain(HMODULE hM, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hModule = hM;
        DisableThreadLibraryCalls(hM);
    }
    return TRUE;
}
