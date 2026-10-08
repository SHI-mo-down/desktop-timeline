// snapshot_manager.cpp : Multi-snapshot layout management
#include "snapshot_manager.h"
#include "icon_manager.h"
#include <shlwapi.h>
#include <stdio.h>
#include <time.h>
#include <algorithm>

static wchar_t g_snapshotDir[MAX_PATH] = {};
static wchar_t g_lastAutoSnapshot[256] = {};

bool Snapshot_Init(const wchar_t* baseDir) {
    wsprintfW(g_snapshotDir, L"%s\\snapshots", baseDir);
    CreateDirectoryW(g_snapshotDir, nullptr);

    // Create subdirectories
    wchar_t sub[MAX_PATH];
    wsprintfW(sub, L"%s\\auto", g_snapshotDir);
    CreateDirectoryW(sub, nullptr);

    wsprintfW(sub, L"%s\\manual", g_snapshotDir);
    CreateDirectoryW(sub, nullptr);

    return true;
}

// Convert icon positions to a simple file format: Name|X|Y
static bool WritePositionsToFile(const wchar_t* filePath) {
    auto icons = GetDesktopIcons();
    if (icons.empty()) return false;

    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"w, ccs=UTF-8");
    if (!fp) return false;

    if (fwprintf(fp, L"Count:%zu\n", icons.size()) < 0) { fclose(fp); return false; }

    for (const auto& icon : icons) {
        std::wstring name = icon.name;
        for (auto& c : name)
            if (c == L'|' || c == L'\n' || c == L'\r') c = L'_';
        if (fwprintf(fp, L"%s|%d|%d\n", name.c_str(), icon.x, icon.y) < 0) {
            fclose(fp);
            return false;
        }
    }

    return fclose(fp) == 0;
}

// Load icon positions from a snapshot file
static std::vector<IconPosition> LoadPositionsFromFile(const wchar_t* filePath) {
    std::vector<IconPosition> result;
    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"r, ccs=UTF-8");
    if (!fp) return result;

    wchar_t line[1024];
    while (fgetws(line, 1024, fp)) {
        if (line[0] == L'#' || line[0] == L'\r' || line[0] == L'\n') continue;
        if (wcsncmp(line, L"Count:", 6) == 0) continue;

        wchar_t* ctx = nullptr;
        wchar_t* name = wcstok(line, L"|", &ctx);
        wchar_t* xs = wcstok(nullptr, L"|", &ctx);
        wchar_t* ys = wcstok(nullptr, L"\r\n", &ctx);

        if (name && xs && ys) {
            IconPosition ip;
            wcsncpy_s(ip.name, name, MAX_PATH - 1);
            ip.point.x = _wtoi(xs);
            ip.point.y = _wtoi(ys);
            result.push_back(ip);
        }
    }
    fclose(fp);
    return result;
}

// Restore icon positions from loaded data
static bool RestorePositionsFromData(const std::vector<IconPosition>& positions) {
    if (positions.empty()) return false;

    ComPtr<IFolderView> spFolderView;
    IShellFolder* spShellFolder = nullptr;
    IEnumIDList* spEnum = nullptr;

    if (FAILED(GetDesktopFolderView(&spFolderView))) return false;
    if (FAILED(spFolderView->GetFolder(IID_IShellFolder, (void**)&spShellFolder))) return false;
    if (FAILED(spFolderView->Items(SVGIO_ALLVIEW, IID_IEnumIDList, (void**)&spEnum))) {
        spShellFolder->Release();
        return false;
    }

    int restored = 0;
    PITEMID_CHILD pidl = nullptr;
    ULONG fetched = 0;
    while (spEnum->Next(1, &pidl, &fetched) == S_OK && fetched > 0) {
        if (pidl) {
            STRRET strret;
            wchar_t name[MAX_PATH] = L"";
            if (SUCCEEDED(spShellFolder->GetDisplayNameOf(pidl, SHGDN_NORMAL, &strret)))
                StrRetToBuf(&strret, pidl, name, MAX_PATH);

            for (const auto& ip : positions) {
                if (wcscmp(name, ip.name) == 0) {
                    POINT pt = ip.point;
                    if (SUCCEEDED(spFolderView->SelectAndPositionItems(
                            1, const_cast<PCUITEMID_CHILD*>(&pidl), &pt, SVSI_POSITIONITEM)))
                        restored++;
                    break;
                }
            }
            CoTaskMemFree(pidl);
            pidl = nullptr;
        }
    }
    spEnum->Release();
    spShellFolder->Release();
    return restored > 0;
}

// Generate a timestamp-based file name
static void MakeFileName(const wchar_t* prefix, const wchar_t* subdir, wchar_t* out, size_t outSize) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfW(out, L"%s\\%s\\%s_%04d%02d%02d_%02d%02d%02d.snap",
              g_snapshotDir, subdir, prefix, st.wYear, st.wMonth, st.wDay,
              st.wHour, st.wMinute, st.wSecond);
}

bool Snapshot_Save(const wchar_t* name) {
    wchar_t cleanName[128];
    wcsncpy_s(cleanName, name, 127);

    // Sanitize name for file system
    for (wchar_t* p = cleanName; *p; p++)
        if (*p == L'\\' || *p == L'/' || *p == L':' || *p == L'*' ||
            *p == L'?' || *p == L'"' || *p == L'<' || *p == L'>' || *p == L'|')
            *p = L'_';

    wchar_t filePath[MAX_PATH];
    wsprintfW(filePath, L"%s\\manual\\%s.snap", g_snapshotDir, cleanName);
    return WritePositionsToFile(filePath);
}

bool Snapshot_AutoSave() {
    wchar_t filePath[MAX_PATH];
    MakeFileName(L"auto", L"auto", filePath, MAX_PATH);
    wcscpy_s(g_lastAutoSnapshot, filePath);
    return WritePositionsToFile(filePath);
}

bool Snapshot_Restore(const wchar_t* fileName) {
    auto positions = LoadPositionsFromFile(fileName);
    return RestorePositionsFromData(positions);
}

bool Snapshot_Undo() {
    if (g_lastAutoSnapshot[0] == 0) {
        // Try to find the most recent auto snapshot
        wchar_t autoDir[MAX_PATH];
        wsprintfW(autoDir, L"%s\\auto\\*", g_snapshotDir);

        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(autoDir, &fd);
        if (hFind == INVALID_HANDLE_VALUE) return false;

        wchar_t latest[MAX_PATH] = {};
        FILETIME latestTime = {};
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (CompareFileTime(&fd.ftCreationTime, &latestTime) > 0) {
                latestTime = fd.ftCreationTime;
                wsprintfW(latest, L"%s\\auto\\%s", g_snapshotDir, fd.cFileName);
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);

        if (latest[0] == 0) return false;
        wcscpy_s(g_lastAutoSnapshot, latest);
    }

    return Snapshot_Restore(g_lastAutoSnapshot);
}

std::vector<SnapshotInfo> Snapshot_List() {
    std::vector<SnapshotInfo> result;

    // Search both auto and manual directories
    const wchar_t* dirs[] = { L"auto", L"manual" };
    for (int d = 0; d < 2; d++) {
        wchar_t searchPath[MAX_PATH];
        wsprintfW(searchPath, L"%s\\%s\\*.snap", g_snapshotDir, dirs[d]);

        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath, &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            SnapshotInfo si = {};
            wsprintfW(si.fileName, L"%s\\%s\\%s", g_snapshotDir, dirs[d], fd.cFileName);

            // Extract name from file name (remove .snap suffix and timestamp prefix)
            wchar_t displayName[128];
            wcscpy_s(displayName, fd.cFileName);
            wchar_t* dot = wcsrchr(displayName, L'.');
            if (dot) *dot = 0;

            if (wcscmp(dirs[d], L"auto") == 0) {
                wsprintfW(si.name, L"[Auto] %s", displayName);
            } else {
                wcsncpy_s(si.name, displayName, 127);
            }

            // Format timestamp
            SYSTEMTIME st;
            FileTimeToSystemTime(&fd.ftCreationTime, &st);
            wsprintfW(si.timestamp, L"%04d-%02d-%02d %02d:%02d:%02d",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond);

            // Read icon count from file header
            FILE* fp = nullptr;
            _wfopen_s(&fp, si.fileName, L"r, ccs=UTF-8");
            if (fp) {
                wchar_t line[256];
                while (fgetws(line, 256, fp)) {
                    if (wcsncmp(line, L"Count:", 6) == 0) {
                        si.iconCount = _wtoi(line + 6);
                        break;
                    }
                }
                fclose(fp);
            }

            result.push_back(si);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    // Sort by timestamp (via file name, newest first)
    std::sort(result.begin(), result.end(), [](const SnapshotInfo& a, const SnapshotInfo& b) {
        return wcscmp(a.fileName, b.fileName) > 0;
    });

    return result;
}
