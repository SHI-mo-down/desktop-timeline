// desktop_ops.cpp : Desktop settings operations for the LLM bridge
#include "desktop_ops.h"
#include "icon_manager.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include "../utils/com_ptr.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")

// ponytail: Bags\1\Desktop is the desktop view bag on Win7-11; live view
// flags via IFolderView2 are the primary mechanism, registry is best-effort
static const wchar_t* kDesktopBag =
    L"Software\\Microsoft\\Windows\\Shell\\Bags\\1\\Desktop";
static const wchar_t* kHideDesktopIcons =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel";

struct SystemIconDef {
    const wchar_t* key;
    const wchar_t* clsid;
};

static const SystemIconDef kSystemIcons[] = {
    { L"thispc",       L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}" },
    { L"recycle",      L"{645FF040-5081-101B-9F08-00AA002F954E}" },
    { L"network",      L"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}" },
    { L"controlpanel", L"{5399E694-6CE5-4D6C-8FCE-1D8870FDCBA0}" },
    { L"userfiles",    L"{59031A47-3F72-44A7-89C5-5595FE6B30EE}" },
};

static const SystemIconDef* FindSystemIcon(const wchar_t* key) {
    for (const auto& d : kSystemIcons)
        if (_wcsicmp(d.key, key) == 0) return &d;
    return nullptr;
}

// ── Registry helpers ──
static bool ReadDword(const wchar_t* sub, const wchar_t* name, DWORD* out) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, sub, name, RRF_RT_DWORD,
                     nullptr, &v, &size) != ERROR_SUCCESS)
        return false;
    *out = v;
    return true;
}

static bool WriteDword(const wchar_t* sub, const wchar_t* name, DWORD v) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, nullptr, 0, KEY_SET_VALUE,
                        nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

static HRESULT GetFolderView2(IFolderView2** pp) {
    ComPtr<IFolderView> fv;
    HRESULT hr = GetDesktopFolderView(&fv);
    if (FAILED(hr)) return hr;
    return fv->QueryInterface(IID_IFolderView2, (void**)pp);
}

// FWF_AUTOARRANGE = 0x1, FWF_SNAPTOGRID = 0x4 (shobjidl_core.h)
static bool SetFlagBit(DWORD bit, bool on) {
    // Persist only if the bag value already exists — never overwrite a
    // value we couldn't read (would destroy FWF_DESKTOP etc.)
    DWORD flags = 0;
    if (ReadDword(kDesktopBag, L"FFlags", &flags)) {
        DWORD v = on ? (flags | bit) : (flags & ~bit);
        if (v != flags) WriteDword(kDesktopBag, L"FFlags", v);
    }

    ComPtr<IFolderView2> fv2;
    HRESULT hr = GetFolderView2(&fv2);
    if (SUCCEEDED(hr)) hr = fv2->SetCurrentFolderFlags(bit, on ? bit : 0);
    RefreshDesktopView();
    return SUCCEEDED(hr);
}

// ── Query ──
static bool GetFlagBit(DWORD bit) {
    ComPtr<IFolderView2> fv2;
    DWORD flags = 0;
    if (SUCCEEDED(GetFolderView2(&fv2)) &&
        SUCCEEDED(fv2->GetCurrentFolderFlags(&flags)))
        return (flags & bit) != 0;
    // Fallback to registry
    DWORD reg = 0;
    if (ReadDword(kDesktopBag, L"FFlags", &reg)) return (reg & bit) != 0;
    return false;
}

bool Desktop_GetAutoArrange() { return GetFlagBit(FWF_AUTOARRANGE); }
bool Desktop_GetSnapToGrid()  { return GetFlagBit(FWF_SNAPTOGRID); }

int Desktop_GetIconSize() {
    DWORD size = 0;
    if (ReadDword(kDesktopBag, L"IconSize", &size) && size > 0)
        return (int)size;
    return 48;
}

void Desktop_GetSpacing(int* h, int* v) {
    ICONMETRICS im = {sizeof(im)};
    if (SystemParametersInfoW(SPI_GETICONMETRICS, sizeof(im), &im, 0)) {
        *h = im.iHorzSpacing;
        *v = im.iVertSpacing;
    } else {
        *h = 75; *v = 75;
    }
}

std::vector<SystemIconState> Desktop_GetSystemIconStates() {
    std::vector<SystemIconState> out;
    for (const auto& d : kSystemIcons) {
        DWORD v = 0;
        bool visible = !ReadDword(kHideDesktopIcons, d.clsid, &v) || v == 0;
        out.push_back({d.key, visible});
    }
    return out;
}

// ── Set ──
bool Desktop_SetAutoArrange(bool on) { return SetFlagBit(FWF_AUTOARRANGE, on); }
bool Desktop_SetSnapToGrid(bool on)  { return SetFlagBit(FWF_SNAPTOGRID, on); }

bool Desktop_SetIconSize(int px) {
    if (px < 16 || px > 512) return false;
    DWORD cur = 0;
    if (ReadDword(kDesktopBag, L"IconSize", &cur) && cur != (DWORD)px)
        WriteDword(kDesktopBag, L"IconSize", (DWORD)px);
    ComPtr<IFolderView2> fv2;
    HRESULT hr = GetFolderView2(&fv2);
    if (SUCCEEDED(hr)) hr = fv2->SetViewModeAndIconSize(FVM_ICON, px);
    RefreshDesktopView();
    return SUCCEEDED(hr);
}

bool Desktop_SetSpacing(int h, int v) {
    if (h < 16 || v < 16) return false;
    ICONMETRICS im = {sizeof(im)};
    if (!SystemParametersInfoW(SPI_GETICONMETRICS, sizeof(im), &im, 0))
        return false;
    im.iHorzSpacing = h;
    im.iVertSpacing = v;
    return SystemParametersInfoW(SPI_SETICONMETRICS, sizeof(im), &im,
                                 SPIF_SENDCHANGE) != 0;
}

bool Desktop_SetSystemIcon(const wchar_t* key, bool visible) {
    const SystemIconDef* def = FindSystemIcon(key);
    if (!def) return false;
    if (!WriteDword(kHideDesktopIcons, def->clsid, visible ? 0 : 1))
        return false;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool Desktop_CreateShortcut(const wchar_t* name, const wchar_t* target,
                            const wchar_t* args, const wchar_t* workDir,
                            const wchar_t* iconPath) {
    if (!name || !name[0] || !target || !target[0]) return false;

    // Sanitize file name: strip invalid chars and any path separators
    std::wstring safe;
    for (const wchar_t* p = name; *p && safe.size() < 200; p++) {
        if (wcschr(L"\\/:*?\"<>|", *p)) safe += L'_';
        else safe += *p;
    }
    if (safe.empty()) return false;
    size_t len = safe.size();
    bool hasLnk = len >= 4 && _wcsicmp(safe.c_str() + len - 4, L".lnk") == 0;
    if (!hasLnk) safe += L".lnk";

    ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&link)))
        return false;
    if (FAILED(link->SetPath(target))) return false;
    if (args && args[0] && FAILED(link->SetArguments(args))) return false;
    if (workDir && workDir[0] && FAILED(link->SetWorkingDirectory(workDir))) return false;
    if (iconPath && iconPath[0] && FAILED(link->SetIconLocation(iconPath, 0))) return false;

    IPersistFile* pf = nullptr;
    if (FAILED(link->QueryInterface(IID_IPersistFile, (void**)&pf)))
        return false;

    wchar_t desktopDir[MAX_PATH];
    HRESULT hr = SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr,
                                  SHGFP_TYPE_CURRENT, desktopDir);
    if (FAILED(hr)) { pf->Release(); return false; }

    std::wstring lnkPath = std::wstring(desktopDir) + L"\\" + safe;
    hr = pf->Save(lnkPath.c_str(), TRUE);
    pf->Release();
    if (SUCCEEDED(hr))
        SHChangeNotify(SHCNE_CREATE, SHCNF_PATHW, lnkPath.c_str(), nullptr);
    return SUCCEEDED(hr);
}

// ── Item ops (rename / delete by display name) ──

static const DesktopIcon* FindIconByName(const std::vector<DesktopIcon>& v,
                                         const wchar_t* name) {
    for (const auto& d : v)
        if (_wcsicmp(d.name.c_str(), name) == 0) return &d;
    return nullptr;
}

// The shell's "User files" icon maps to the profile root; treat it (and any
// other real path outside a desktop) as off-limits even if a shell version
// ever enumerates it with a real path instead of ::{CLSID}
static bool IsProtectedPath(const std::wstring& path) {
    wchar_t profile[MAX_PATH] = L"";
    if (!GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH)) return false;
    return _wcsicmp(path.c_str(), profile) == 0;
}

bool Desktop_RenameIcon(const wchar_t* name, const wchar_t* newName) {
    if (!name || !*name || !newName || !*newName) return false;
    if (wcspbrk(newName, L"\\/:*?\"<>|") || wcslen(newName) > 200) return false;

    auto icons = GetDesktopIcons();
    const DesktopIcon* ic = FindIconByName(icons, name);
    if (!ic || ic->path.rfind(L"::", 0) == 0 || IsProtectedPath(ic->path)) return false;

    std::wstring oldPath = ic->path;
    size_t slash = oldPath.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;

    // keep the original extension when the new name has none of its own:
    // "notes" + old .txt -> notes.txt, but "notes.md" is taken as typed
    std::wstring base = newName;
    if (!ic->isFolder && base.find(L'.') == std::wstring::npos) {
        size_t dot = oldPath.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > slash + 1)
            base += oldPath.substr(dot);
    }
    std::wstring newPath = oldPath.substr(0, slash + 1) + base;

    if (!MoveFileW(oldPath.c_str(), newPath.c_str())) return false;
    SHChangeNotify(SHCNE_RENAMEITEM, SHCNF_PATHW, oldPath.c_str(), newPath.c_str());
    RefreshDesktopView();
    return true;
}

bool Desktop_DeleteIcon(const wchar_t* name) {
    if (!name || !*name) return false;

    auto icons = GetDesktopIcons();
    const DesktopIcon* ic = FindIconByName(icons, name);
    if (!ic || ic->path.rfind(L"::", 0) == 0 || IsProtectedPath(ic->path)) return false;

    std::wstring from = ic->path;
    from.push_back(L'\0'); // SHFileOperation wants a double-null list

    SHFILEOPSTRUCTW op = {};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    int r = SHFileOperationW(&op);
    RefreshDesktopView();
    return r == 0 && !op.fAnyOperationsAborted;
}
