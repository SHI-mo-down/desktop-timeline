// shellext.h : IExplorerCommand + IEnumExplorerCommand for Win11 menu
#pragma once
#include <Windows.h>
#include <shlobj.h>
#include <shobjidl.h>

// {A1B2C3D4-E5F6-7890-ABCD-EF1234567800}
DEFINE_GUID(CLSID_ZoneSortRoot,
    0xa1b2c3d4, 0xe5f6, 0x7890, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78, 0x00);

enum { CMD_NAME = 0, CMD_TYPE = 1, CMD_SIZE = 2, CMD_DATE = 3 };

// Helpers
LONG GetModuleRefCount();
void SetDllPath(const wchar_t* path);
const wchar_t* GetDllPath();
void IncModuleRef();
void DecModuleRef();

// Leaf command (no sub-commands)
class SortActionCommand : public IExplorerCommand {
public:
    SortActionCommand(int mode);
    ~SortActionCommand();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IExplorerCommand
    STDMETHODIMP GetTitle(IShellItemArray*, LPWSTR* ppszTitle);
    STDMETHODIMP GetIcon(IShellItemArray*, LPWSTR* ppszIcon);
    STDMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* ppszToolTip);
    STDMETHODIMP GetCanonicalName(GUID* pguid);
    STDMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pState);
    STDMETHODIMP Invoke(IShellItemArray*, IBindCtx*);
    STDMETHODIMP GetFlags(EXPCMDFLAGS* pFlags);
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum);

private:
    ULONG m_refCount;
    int m_mode;
};

// Root command with sub-menu
class ZoneSortRootCommand : public IExplorerCommand, public IEnumExplorerCommand {
public:
    ZoneSortRootCommand();
    ~ZoneSortRootCommand();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IExplorerCommand
    STDMETHODIMP GetTitle(IShellItemArray*, LPWSTR* ppszTitle);
    STDMETHODIMP GetIcon(IShellItemArray*, LPWSTR* ppszIcon);
    STDMETHODIMP GetToolTip(IShellItemArray*, LPWSTR* ppszToolTip);
    STDMETHODIMP GetCanonicalName(GUID* pguid);
    STDMETHODIMP GetState(IShellItemArray*, BOOL, EXPCMDSTATE* pState);
    STDMETHODIMP Invoke(IShellItemArray*, IBindCtx*);
    STDMETHODIMP GetFlags(EXPCMDFLAGS* pFlags);
    STDMETHODIMP EnumSubCommands(IEnumExplorerCommand** ppEnum);

    // IEnumExplorerCommand
    STDMETHODIMP Next(ULONG celt, IExplorerCommand** pCmd, ULONG* pceltFetched);
    STDMETHODIMP Skip(ULONG);
    STDMETHODIMP Reset();
    STDMETHODIMP Clone(IEnumExplorerCommand**);

private:
    ULONG m_refCount;
    ULONG m_enumIndex;
};
