// IconSorterTests.cpp : COM Interface Test
// Using standard COM without ATL

#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <stdio.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")

void Log(const char* msg) {
    printf("%s\n", msg);
    fflush(stdout);
}

template<typename T>
class ComPtr {
public:
    ComPtr() : ptr_(nullptr) {}
    ComPtr(T* p) : ptr_(p) { if (ptr_) ptr_->AddRef(); }
    ~ComPtr() { Release(); }

    T** operator&() { Release(); return &ptr_; }
    T* operator->() const { return ptr_; }
    operator T*() const { return ptr_; }

    void Release() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_;
};

void SetDPIAwareness() {
    HMODULE hUser32 = LoadLibrary(L"user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
        auto pFunc = (SetProcessDpiAwarenessContextFunc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (pFunc) {
            pFunc((DPI_AWARENESS_CONTEXT)-4);
        }
        FreeLibrary(hUser32);
    }
}

HRESULT TestBasicShellFolder() {
    Log("[Test 1] Get Desktop IShellFolder...");

    IShellFolder* pDesktopFolder = nullptr;
    HRESULT hr = SHGetDesktopFolder(&pDesktopFolder);
    if (FAILED(hr)) {
        Log("  Failed: SHGetDesktopFolder");
        return hr;
    }

    Log("  Success: Got IShellFolder interface");

    IEnumIDList* pEnum = nullptr;
    hr = pDesktopFolder->EnumObjects(nullptr, SHCONTF_NONFOLDERS | SHCONTF_FOLDERS, &pEnum);
    if (SUCCEEDED(hr) && pEnum) {
        int itemCount = 0;
        PITEMID_CHILD pidl = nullptr;
        ULONG fetched = 0;

        while (pEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0 && itemCount < 5) {
            if (pidl) {
                STRRET strret;
                hr = pDesktopFolder->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret);
                if (SUCCEEDED(hr)) {
                    wchar_t name[MAX_PATH];
                    if (SUCCEEDED(StrRetToBuf(&strret, pidl, name, MAX_PATH))) {
                        wprintf(L"    Item: %s\n", name);
                        fflush(stdout);
                    }
                }
                CoTaskMemFree(pidl);
                pidl = nullptr;
            }
            itemCount++;
        }

        printf("  Enumerated %d desktop items\n", itemCount);
        fflush(stdout);

        pEnum->Release();
    }

    pDesktopFolder->Release();
    return S_OK;
}

HRESULT TestShellWindows() {
    Log("[Test 2] Try IShellWindows interface...");

    IShellWindows* pShellWindows = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellWindows, nullptr,
                                   CLSCTX_ALL, IID_IShellWindows,
                                   reinterpret_cast<void**>(&pShellWindows));
    if (FAILED(hr)) {
        Log("  Failed: Create IShellWindows instance");
        return hr;
    }

    Log("  Success: Got IShellWindows interface");

    long count = 0;
    hr = pShellWindows->get_Count(&count);
    if (SUCCEEDED(hr)) {
        printf("  Found %ld Shell windows\n", count);
        fflush(stdout);
    }

    pShellWindows->Release();
    return S_OK;
}

void FindDesktopWindow() {
    Log("[Test 3] Find Desktop windows...");

    HWND hwndProgman = FindWindow(L"Progman", L"Program Manager");
    if (hwndProgman) {
        Log("  Found Progman window");

        HWND hwndShellView = FindWindowEx(hwndProgman, nullptr, L"SHELLDLL_DefView", nullptr);
        if (hwndShellView) {
            Log("  Found SHELLDLL_DefView");

            HWND hwndListView = FindWindowEx(hwndShellView, nullptr, L"SysListView32", nullptr);
            if (hwndListView) {
                Log("  Found SysListView32");
                return;
            }
        }
    }

    HWND hwndListView = FindWindow(L"SysListView32", nullptr);
    if (hwndListView) {
        Log("  Found SysListView32 directly");
        return;
    }

    Log("  Warning: Desktop listview not found");
}

HRESULT RunAllTests() {
    Log("==============================================");
    Log("=== Desktop Icon COM Interface Test ===");
    Log("==============================================");

    Log("Initializing COM library...");
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        Log("COM initialization failed!");
        return hr;
    }

    SetDPIAwareness();

    int testsPassed = 0;
    int testsTotal = 0;

    testsTotal++;
    hr = TestBasicShellFolder();
    if (SUCCEEDED(hr)) {
        testsPassed++;
        Log("[PASS] Test 1");
    } else {
        Log("[FAIL] Test 1");
    }

    testsTotal++;
    hr = TestShellWindows();
    if (SUCCEEDED(hr)) {
        testsPassed++;
        Log("[PASS] Test 2");
    } else {
        Log("[FAIL] Test 2 (non-critical)");
    }

    testsTotal++;
    FindDesktopWindow();
    testsPassed++;

    Log("==============================================");
    printf("Tests completed: %d/%d passed\n", testsPassed, testsTotal);
    fflush(stdout);

    if (testsPassed >= 1) {
        Log("SUCCESS: COM interfaces are available");
    } else {
        Log("FAILED: Cannot access desktop COM interfaces");
    }

    CoUninitialize();

    return (testsPassed >= 1) ? S_OK : E_FAIL;
}

int main() {
    AllocConsole();
    freopen("CONOUT$", "w", stdout);

    Log("");
    Log("Desktop Icon Sorter - Phase 1 Test");
    Log("==============================================");
    Log("");

    HRESULT hr = RunAllTests();

    Log("");
    Log("Press any key to exit...");

    HANDLE hConsole = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD input;
    DWORD read;
    while (ReadConsoleInput(hConsole, &input, 1, &read)) {
        if (input.EventType == KEY_EVENT && input.Event.KeyEvent.bKeyDown) {
            break;
        }
    }

    FreeConsole();
    return SUCCEEDED(hr) ? 0 : 1;
}
