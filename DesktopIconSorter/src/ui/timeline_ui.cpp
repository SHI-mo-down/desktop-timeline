// timeline_ui.cpp : Timeline panel — snapshot list, restore, save checkpoint, rename, delete
#include "timeline_ui.h"
#include "resource.h"
#include "../core/timeline_db.h"
#include "../core/timeline_record.h"
#include "../core/icon_manager.h"
#include "../core/snapshot_manager.h"
#include <CommCtrl.h>
#include <uxtheme.h>
#include <stdio.h>
#include <shlwapi.h>
#include <shellapi.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")

#define WM_REFRESH_LIST (WM_APP + 100)

static const wchar_t* CLASS_NAME = L"DesktopTimelineWindow";
static HWND g_hwndTimeline = nullptr;
static HWND g_hwndList = nullptr;
static HWND g_hwndStatus = nullptr;
static wchar_t g_baseDir[MAX_PATH] = {};
static HINSTANCE g_hInst = nullptr;

static LRESULT CALLBACK TimelineWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void PopulateList();
static void OnRestore();
static void OnSaveCheckpoint();
static void OnDeleteSelected();
static void OnRenameSelected();
static void OnSettings();
static int  GetSelectedIndex();

void TimelineUI_Show(HINSTANCE hInstance, HWND parentWnd, const wchar_t* baseDir) {
    if (g_hwndTimeline && IsWindow(g_hwndTimeline)) {
        SetForegroundWindow(g_hwndTimeline);
        PopulateList();
        return;
    }

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    wcscpy_s(g_baseDir, baseDir);
    g_hInst = hInstance;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = TimelineWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    // Try custom icon (use g_baseDir, same directory as exe)
    wchar_t iconPath[MAX_PATH];
    wsprintfW(iconPath, L"%sapp.ico", g_baseDir);
    HANDLE hIco = LoadImageW(nullptr, iconPath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (hIco) wc.hIcon = (HICON)hIco;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    // Size by screen percentage — looks right on any resolution
    int w = GetSystemMetrics(SM_CXSCREEN) * 40 / 100;
    int h = GetSystemMetrics(SM_CYSCREEN) * 45 / 100;
    if (w < 640) w = 640; if (h < 420) h = 420;
    if (w > 1200) w = 1200; if (h > 800) h = 800;
    // Center on virtual screen (works for multi-monitor)
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN) +
            (GetSystemMetrics(SM_CXVIRTUALSCREEN) - w) / 2;
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN) +
            (GetSystemMetrics(SM_CYVIRTUALSCREEN) - h) / 2;

    g_hwndTimeline = CreateWindowExW(0, CLASS_NAME, L"Desktop Timeline",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, w, h, parentWnd, nullptr, hInstance, nullptr);

    if (g_hwndTimeline) { ShowWindow(g_hwndTimeline, SW_SHOW); UpdateWindow(g_hwndTimeline); }
}

bool TimelineUI_IsOpen() { return g_hwndTimeline && IsWindow(g_hwndTimeline); }
void TimelineUI_Close() {
    if (g_hwndTimeline && IsWindow(g_hwndTimeline)) DestroyWindow(g_hwndTimeline);
}
void TimelineUI_Refresh() {
    if (g_hwndTimeline && IsWindow(g_hwndTimeline))
        PostMessageW(g_hwndTimeline, WM_REFRESH_LIST, 0, 0);
}

static int GetSelectedIndex() {
    if (!g_hwndList) return -1;
    return ListView_GetNextItem(g_hwndList, -1, LVNI_SELECTED);
}

static void PopulateList() {
    if (!g_hwndList) return;

    int oldCount = ListView_GetItemCount(g_hwndList);
    for (int i = 0; i < oldCount; i++) {
        LVITEMW item = {}; item.iItem = i; item.mask = LVIF_PARAM;
        ListView_GetItem(g_hwndList, &item);
        if (item.lParam) delete[] (wchar_t*)item.lParam;
    }
    ListView_DeleteAllItems(g_hwndList);

    auto snapshots = Timeline_List(200);
    for (size_t i = 0; i < snapshots.size(); i++) {
        const auto& s = snapshots[i];
        LVITEMW item = {};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)i;

        wchar_t idx[16]; wsprintfW(idx, L"%d", (int)(i + 1));
        item.pszText = idx; item.cchTextMax = 16;
        ListView_InsertItem(g_hwndList, &item);

        wchar_t timeStr[16];
        wcsncpy_s(timeStr, s.timestamp.c_str() + (s.timestamp.length() >= 16 ? 11 : 0), 5);
        ListView_SetItemText(g_hwndList, (int)i, 1, timeStr);

        ListView_SetItemText(g_hwndList, (int)i, 2,
            (LPWSTR)(s.isCheckpoint ? L"Checkpoint" : L"Auto"));
        ListView_SetItemText(g_hwndList, (int)i, 3, (LPWSTR)s.name.c_str());

        wchar_t countStr[16]; wsprintfW(countStr, L"%d", s.iconCount);
        ListView_SetItemText(g_hwndList, (int)i, 4, countStr);

        wchar_t* idCopy = new wchar_t[s.id.length() + 1];
        wcscpy_s(idCopy, s.id.length() + 1, s.id.c_str());
        item.mask = LVIF_PARAM;
        item.iItem = (int)i;
        item.lParam = (LPARAM)idCopy;
        ListView_SetItem(g_hwndList, &item);
    }

    if (g_hwndStatus) {
        int cpCount = 0;
        for (const auto& s : snapshots) { if (s.isCheckpoint) cpCount++; }
        wchar_t status[160];
        wsprintfW(status, L"%d records (%d checkpoints) — Enter restore, Del delete, F2 rename",
                 (int)snapshots.size(), cpCount);
        SetWindowTextW(g_hwndStatus, status);
    }

    // Re-auto-size columns to fit content, but never narrower than header
    for (int i = 0; i < 4; i++) {
        ListView_SetColumnWidth(g_hwndList, i, LVSCW_AUTOSIZE);
        int w = ListView_GetColumnWidth(g_hwndList, i);
        if (w < 55) ListView_SetColumnWidth(g_hwndList, i, LVSCW_AUTOSIZE_USEHEADER);
    }
    ListView_SetColumnWidth(g_hwndList, 3, LVSCW_AUTOSIZE_USEHEADER); // Name fills rest
}

static void OnRestore() {
    int sel = GetSelectedIndex();
    if (sel < 0) { MessageBoxW(g_hwndTimeline, L"Select a snapshot first.", L"Timeline", MB_OK); return; }

    LVITEMW item = {}; item.iItem = sel; item.mask = LVIF_PARAM;
    ListView_GetItem(g_hwndList, &item);
    wchar_t* id = (wchar_t*)item.lParam;
    if (!id) return;

    TimelineSnapshot snap;
    if (!Timeline_Load(id, &snap)) {
        MessageBoxW(g_hwndTimeline, L"Failed to load snapshot.", L"Error", MB_OK); return;
    }
    if (snap.iconCount == 0) {
        MessageBoxW(g_hwndTimeline, L"This snapshot has no icon data.", L"Error", MB_OK); return;
    }

    wchar_t msg[256];
    wsprintfW(msg, L"Restore %d icons to recorded positions?\n\nSnapshot: %s",
             snap.iconCount, snap.name.c_str());
    if (MessageBoxW(g_hwndTimeline, msg, L"Confirm Restore", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    // Save current layout before restoring (undo support)
    {
        TimelineSnapshot cur;
        ComPtr<IFolderView> spFV2;
        if (SUCCEEDED(GetDesktopFolderView(&spFV2))) {
            // Quick save via snapshot manager
            Snapshot_AutoSave();
        }
    }

    int restored = 0;
    ComPtr<IFolderView> spFolderView;
    IShellFolder* sf = nullptr;
    IEnumIDList* spEnum = nullptr;

    if (FAILED(GetDesktopFolderView(&spFolderView))) goto done;
    if (FAILED(spFolderView->GetFolder(IID_IShellFolder, (void**)&sf))) goto done;
    if (FAILED(spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList, (void**)&spEnum))) {
        sf->Release(); goto done;
    }

    {
        PITEMID_CHILD pidl = nullptr; ULONG fetched = 0;
        while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
            if (pidl) {
                STRRET strret; wchar_t name[MAX_PATH] = L"";
                if (SUCCEEDED(sf->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret)))
                    StrRetToBuf(&strret, pidl, name, MAX_PATH);
                for (const auto& icon : snap.icons) {
                    if (wcscmp(name, icon.name.c_str()) == 0) {
                        POINT pt = { icon.x, icon.y };
                        if (SUCCEEDED(spFolderView->SelectAndPositionItems(
                                1, (PCUITEMID_CHILD*)&pidl, &pt, SVSI_POSITIONITEM)))
                            restored++;
                        break;
                    }
                }
                CoTaskMemFree(pidl); pidl = nullptr;
            }
        }
    }
    spEnum->Release(); sf->Release();

    wsprintfW(msg, L"Restored %d of %d icons.", restored, snap.iconCount);
    MessageBoxW(g_hwndTimeline, msg, L"Done", MB_OK);
    return;

done:
    MessageBoxW(g_hwndTimeline, L"Could not access desktop icons.", L"Error", MB_OK);
}

static void OnSaveCheckpoint() {
    if (MessageBoxW(g_hwndTimeline,
        L"Remember current icon layout?\n\nThis creates a named checkpoint you can restore later.",
        L"Save Checkpoint", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t displayName[64], safeName[64];
    wsprintfW(displayName, L"Checkpoint %02d-%02d %02d:%02d",
             st.wMonth, st.wDay, st.wHour, st.wMinute);
    wsprintfW(safeName, L"checkpoint_%02d%02d_%02d%02d",
             st.wMonth, st.wDay, st.wHour, st.wMinute);

    TimelineSnapshot snap = {};
    snap.id = safeName;
    snap.name = displayName;
    snap.isCheckpoint = true;
    snap.monitorWidth = GetSystemMetrics(SM_CXSCREEN);
    snap.monitorHeight = GetSystemMetrics(SM_CYSCREEN);
    HDC hdc = GetDC(nullptr); snap.dpi = GetDeviceCaps(hdc, LOGPIXELSX); ReleaseDC(nullptr, hdc);

    bool comOk = false;
    ComPtr<IFolderView> spFolderView;
    if (SUCCEEDED(GetDesktopFolderView(&spFolderView))) {
        IShellFolder* sf = nullptr;
        if (SUCCEEDED(spFolderView->GetFolder(IID_IShellFolder, (void**)&sf))) {
            IEnumIDList* spEnum = nullptr;
            if (SUCCEEDED(spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList, (void**)&spEnum))) {
                comOk = true;
                PITEMID_CHILD pidl = nullptr; ULONG fetched = 0;
                while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
                    if (pidl) {
                        TimelineIcon icon = {};
                        STRRET strret; wchar_t nameBuf[MAX_PATH] = L"";
                        if (SUCCEEDED(sf->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret)))
                            StrRetToBuf(&strret, pidl, nameBuf, MAX_PATH);
                        icon.name = nameBuf;
                        POINT pt = {0, 0};
                        spFolderView->GetItemPosition(pidl, &pt);
                        icon.x = pt.x; icon.y = pt.y;
                        snap.icons.push_back(icon);
                        CoTaskMemFree(pidl);
                    }
                }
                spEnum->Release();
            }
            sf->Release();
        }
    }

    if (!comOk || snap.icons.empty()) {
        MessageBoxW(g_hwndTimeline,
            L"Could not read desktop icons.\n\n"
            L"Make sure the desktop is visible (not on lock screen)\n"
            L"and Explorer is running.",
            L"Save Failed", MB_OK | MB_ICONWARNING);
        return;
    }

    snap.iconCount = (int)snap.icons.size();

    wchar_t ts[64];
    wsprintfW(ts, L"%04d-%02d-%02dT%02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    snap.timestamp = ts;

    if (Timeline_Save(snap)) {
        wchar_t done[128];
        wsprintfW(done, L"Checkpoint saved: %d icons.", snap.iconCount);
        MessageBoxW(g_hwndTimeline, done, L"Done", MB_OK);
        PopulateList();
    } else {
        MessageBoxW(g_hwndTimeline, L"Failed to write checkpoint file.", L"Error", MB_OK);
    }
}

static void OnDeleteSelected() {
    int sel = GetSelectedIndex();
    if (sel < 0) { MessageBoxW(g_hwndTimeline, L"Select a snapshot first.", L"Timeline", MB_OK); return; }

    LVITEMW item = {}; item.iItem = sel; item.mask = LVIF_PARAM;
    ListView_GetItem(g_hwndList, &item);
    wchar_t* id = (wchar_t*)item.lParam;
    if (!id) return;

    TimelineSummary sum;
    TimelineSnapshot snap;
    Timeline_Load(id, &snap); // Just to get the name
    wchar_t msg[256];
    wsprintfW(msg, L"Delete \"%s\"?\n\nThis cannot be undone.",
             snap.name.empty() ? id : snap.name.c_str());
    if (MessageBoxW(g_hwndTimeline, msg, L"Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) return;

    if (Timeline_Delete(id)) {
        PopulateList();
    } else {
        MessageBoxW(g_hwndTimeline, L"Failed to delete.", L"Error", MB_OK);
    }
}

static void OnRenameSelected() {
    int sel = GetSelectedIndex();
    if (sel < 0) { MessageBoxW(g_hwndTimeline, L"Select a checkpoint first.", L"Timeline", MB_OK); return; }

    LVITEMW item = {}; item.iItem = sel; item.mask = LVIF_PARAM;
    ListView_GetItem(g_hwndList, &item);
    wchar_t* id = (wchar_t*)item.lParam;
    if (!id) return;

    TimelineSnapshot snap;
    if (!Timeline_Load(id, &snap)) return;
    if (!snap.isCheckpoint) {
        MessageBoxW(g_hwndTimeline,
            L"Auto records cannot be renamed.\nSave a checkpoint first.",
            L"Rename", MB_OK | MB_ICONINFORMATION);
        return;
    }

    // Simple rename dialog using a message box with edit control
    // Create a small popup window for input
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"STATIC", L"Rename Checkpoint",
        WS_POPUP | WS_CAPTION | WS_VISIBLE, 0, 0, 320, 110, g_hwndTimeline, nullptr, g_hInst, nullptr);
    SetWindowTextW(hDlg, L"Rename Checkpoint");

    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", snap.name.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        12, 12, 280, 22, hDlg, nullptr, g_hInst, nullptr);
    SendMessageW(hEdit, EM_SETSEL, 0, -1);

    CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        130, 45, 75, 24, hDlg, (HMENU)IDOK, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE,
        215, 45, 75, 24, hDlg, (HMENU)IDCANCEL, g_hInst, nullptr);

    // Center on parent
    RECT pr; GetWindowRect(g_hwndTimeline, &pr);
    SetWindowPos(hDlg, nullptr,
        pr.left + (pr.right - pr.left - 320) / 2,
        pr.top + (pr.bottom - pr.top - 110) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    // Run modal loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            msg.hwnd = hDlg;
            msg.message = WM_COMMAND;
            msg.wParam = IDOK;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            DestroyWindow(hDlg);
            break;
        }
        if (msg.message == WM_COMMAND && (LOWORD(msg.wParam) == IDOK || LOWORD(msg.wParam) == IDCANCEL)) {
            if (LOWORD(msg.wParam) == IDOK) {
                wchar_t newName[128];
                GetWindowTextW(hEdit, newName, 128);
                if (newName[0]) {
                    snap.name = newName;
                    // Delete old file, save with new name (id stays same, name changes)
                    Timeline_Delete(id);
                    Timeline_Save(snap);
                    PopulateList();
                }
            }
            DestroyWindow(hDlg);
            continue;
        }
        if (!IsWindow(hDlg)) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void OnSettings() {
    // Open the rules.dtrules file location for editing
    wchar_t rulePath[MAX_PATH];
    wsprintfW(rulePath, L"%s\\rules.dtrules", g_baseDir);

    // If file exists, open it; otherwise show info
    if (GetFileAttributesW(rulePath) != INVALID_FILE_ATTRIBUTES) {
        ShellExecuteW(g_hwndTimeline, L"open", rulePath, nullptr, nullptr, SW_SHOW);
    } else {
        MessageBoxW(g_hwndTimeline,
            L"No rules.dtrules found.\n\n"
            L"Create this file in the program directory to define\n"
            L"layout zones and auto-placement rules.",
            L"Settings", MB_OK | MB_ICONINFORMATION);
    }
}

static LRESULT CALLBACK TimelineWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc; GetClientRect(hwnd, &rc);
        int clientW = rc.right - rc.left;
        int clientH = rc.bottom - rc.top;

        // DPI-aware sizes
        HDC hdcFont = GetDC(hwnd);
        int dpiY = GetDeviceCaps(hdcFont, LOGPIXELSY);
        ReleaseDC(hwnd, hdcFont);
        int pad = MulDiv(12, dpiY, 96);
        int listFontH = -MulDiv(11, dpiY, 72);
        int statusFontH = -MulDiv(9, dpiY, 72);
        int btnW1 = MulDiv(90, dpiY, 96);
        int btnW2 = MulDiv(80, dpiY, 96);
        int btnW3 = MulDiv(65, dpiY, 96);
        int btnH = MulDiv(26, dpiY, 96);
        int btnY = clientH - btnH - 8;
        int footerH = btnH + 32;  // space for buttons + status

        // --- ListView with proper spacing ---
        g_hwndList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
            pad, pad, clientW - pad * 2, clientH - footerH,
            hwnd, (HMENU)IDC_TIMELINE_LIST, g_hInst, nullptr);

        ListView_SetExtendedListViewStyle(g_hwndList,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(g_hwndList, L"Explorer", nullptr);
        // Columns — lock column widths, no manual resize, no horizontal scrollbar
        int col0 = 45, col1 = 80, col2 = 100, col4 = 60;
        int col3 = (clientW - pad * 2) - (col0 + col1 + col2 + col4 + 4);

        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        col.fmt = LVCFMT_CENTER;
        col.cx = col0; col.pszText = (LPWSTR)L"#";     ListView_InsertColumn(g_hwndList, 0, &col);
        col.fmt = LVCFMT_LEFT;
        col.cx = col1; col.pszText = (LPWSTR)L"Time";   ListView_InsertColumn(g_hwndList, 1, &col);
        col.cx = col2; col.pszText = (LPWSTR)L"Type";   ListView_InsertColumn(g_hwndList, 2, &col);
        col.cx = col3; col.pszText = (LPWSTR)L"Name";   ListView_InsertColumn(g_hwndList, 3, &col);
        col.cx = col4; col.pszText = (LPWSTR)L"Icons";  ListView_InsertColumn(g_hwndList, 4, &col);

        // Remove horizontal scrollbar — columns fill exact width
        SetWindowLongPtrW(g_hwndList, GWL_STYLE,
            GetWindowLongPtrW(g_hwndList, GWL_STYLE) & ~WS_HSCROLL);

        // Disable column resize: remove HDS_FULLDRAG
        HWND hHeader = ListView_GetHeader(g_hwndList);
        if (hHeader) {
            SetWindowLongPtrW(hHeader, GWL_STYLE,
                GetWindowLongPtrW(hHeader, GWL_STYLE) & ~HDS_FULLDRAG);
        }

        // --- Buttons ---
        CreateWindowExW(0, L"BUTTON", L"Restore",
            WS_CHILD | WS_VISIBLE | BS_FLAT,
            pad, btnY, btnW1, btnH,
            hwnd, (HMENU)IDC_BTN_RESTORE, g_hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Save Now",
            WS_CHILD | WS_VISIBLE | BS_FLAT,
            pad + btnW1 + 8, btnY, btnW2, btnH,
            hwnd, (HMENU)IDC_BTN_CHECKPOINT, g_hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Delete",
            WS_CHILD | WS_VISIBLE | BS_FLAT,
            pad + btnW1 + btnW2 + 16, btnY, btnW3, btnH,
            hwnd, (HMENU)IDC_BTN_DELETE, g_hInst, nullptr);
        // Settings — right-aligned, fills the empty space
        CreateWindowExW(0, L"BUTTON", L"Settings",
            WS_CHILD | WS_VISIBLE | BS_FLAT,
            clientW - pad - btnW3, btnY, btnW3, btnH,
            hwnd, (HMENU)IDC_BTN_SETTINGS, g_hInst, nullptr);

        // --- Status bar ---
        g_hwndStatus = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE,
            pad, btnY - 22, clientW - pad * 2, 18,
            hwnd, (HMENU)IDC_STATUS_TEXT, g_hInst, nullptr);

        // Fonts
        HFONT hfList = CreateFontW(listFontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        HFONT hfStatus = CreateFontW(statusFontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        SendMessageW(g_hwndList, WM_SETFONT, (WPARAM)hfList, TRUE);
        SendMessageW(g_hwndStatus, WM_SETFONT, (WPARAM)hfStatus, TRUE);

        Timeline_Init(g_baseDir);
        PopulateList();

        // Auto-size columns: fit content, minimum = header width
        for (int i = 0; i < 4; i++) {
            ListView_SetColumnWidth(g_hwndList, i, LVSCW_AUTOSIZE);
            int w = ListView_GetColumnWidth(g_hwndList, i);
            if (w < 55) ListView_SetColumnWidth(g_hwndList, i, LVSCW_AUTOSIZE_USEHEADER);
        }
        ListView_SetColumnWidth(g_hwndList, 3, LVSCW_AUTOSIZE_USEHEADER);
        break;
    }

    case WM_REFRESH_LIST:
        PopulateList();
        break;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BTN_RESTORE:    OnRestore(); break;
        case IDC_BTN_CHECKPOINT: OnSaveCheckpoint(); break;
        case IDC_BTN_DELETE:     OnDeleteSelected(); break;
        case IDC_BTN_SETTINGS:   OnSettings(); break;
        }
        break;

    case WM_NOTIFY:
        if (((NMHDR*)lp)->idFrom == IDC_TIMELINE_LIST) {
            if (((NMHDR*)lp)->code == NM_DBLCLK)
                OnRestore();
            if (((NMHDR*)lp)->code == HDN_BEGINTRACKW ||
                ((NMHDR*)lp)->code == HDN_BEGINTRACKA)
                return TRUE;
            // Custom draw: yellow background for checkpoints
            if (((NMHDR*)lp)->code == NM_CUSTOMDRAW) {
                NMLVCUSTOMDRAW* cd = (NMLVCUSTOMDRAW*)lp;
                if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
                    return CDRF_NOTIFYITEMDRAW;
                if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    LVITEMW item = {};
                    item.iItem = (int)cd->nmcd.dwItemSpec;
                    item.mask = LVIF_PARAM;
                    ListView_GetItem(g_hwndList, &item);
                    wchar_t* id = (wchar_t*)item.lParam;
                    if (id) {
                        // Check if it's a checkpoint by looking for "checkpoint" in the ID
                        if (wcsstr(id, L"checkpoint")) {
                            cd->clrTextBk = RGB(255, 255, 180); // light yellow
                            return CDRF_NEWFONT;
                        }
                    }
                    return CDRF_DODEFAULT;
                }
            }
        }
        break;

    case WM_KEYDOWN:
        if ((HWND)wp == g_hwndList) {
            if (lp == VK_RETURN) OnRestore();
            else if (lp == VK_DELETE) OnDeleteSelected();
            else if (lp == VK_F2) OnRenameSelected();
        }
        break;

    case WM_CLOSE:
        if (g_hwndList) {
            int count = ListView_GetItemCount(g_hwndList);
            for (int i = 0; i < count; i++) {
                LVITEMW item = {}; item.iItem = i; item.mask = LVIF_PARAM;
                ListView_GetItem(g_hwndList, &item);
                if (item.lParam) delete[] (wchar_t*)item.lParam;
            }
        }
        DestroyWindow(hwnd);
        g_hwndTimeline = nullptr; g_hwndList = nullptr; g_hwndStatus = nullptr;
        break;

    case WM_DESTROY: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
