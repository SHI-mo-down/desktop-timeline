// timeline_db.cpp : Timeline persistent storage - .dslayout file format
#include "timeline_db.h"
#include <shlwapi.h>
#include <stdio.h>
#include <time.h>
#include <algorithm>

static wchar_t g_timelineDir[MAX_PATH] = {};
static wchar_t g_checkpointsDir[MAX_PATH] = {};
static bool g_initialized = false;

bool Timeline_Init(const wchar_t* baseDir) {
    if (!baseDir || !baseDir[0]) return false;

    wsprintfW(g_timelineDir, L"%s\\timeline", baseDir);
    wsprintfW(g_checkpointsDir, L"%s\\checkpoints", baseDir);

    CreateDirectoryW(g_timelineDir, nullptr);
    CreateDirectoryW(g_checkpointsDir, nullptr);

    g_initialized = true;
    return true;
}

// Format: 20260629_153000_auto or checkpoint_工作布局
static void MakeSnapshotId(const TimelineSnapshot& snap, wchar_t* out, size_t outSize) {
    if (snap.isCheckpoint) {
        // Sanitize name for filesystem
        wchar_t safe[128];
        wcsncpy_s(safe, snap.name.c_str(), 127);
        for (wchar_t* p = safe; *p; p++) {
            if (*p == L'\\' || *p == L'/' || *p == L':' || *p == L'*' ||
                *p == L'?' || *p == L'"' || *p == L'<' || *p == L'>' || *p == L'|')
                *p = L'_';
        }
        wsprintfW(out, L"checkpoint_%s", safe);
    } else {
        wsprintfW(out, L"%s", snap.id.c_str());
    }
}

// Build full file path for a snapshot ID
static void GetFilePath(const wchar_t* id, bool isCheckpoint, wchar_t* out, size_t outSize) {
    if (isCheckpoint)
        wsprintfW(out, L"%s\\%s.dslayout", g_checkpointsDir, id);
    else
        wsprintfW(out, L"%s\\%s.dslayout", g_timelineDir, id);
}

bool Timeline_GetFilePath(const wchar_t* id, wchar_t* outPath, size_t outSize) {
    if (!g_initialized) return false;
    // Try both directories
    wsprintfW(outPath, L"%s\\%s.dslayout", g_timelineDir, id);
    if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) return true;

    wsprintfW(outPath, L"%s\\%s.dslayout", g_checkpointsDir, id);
    if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) return true;

    return false;
}

// Write a snapshot to .dslayout file
bool Timeline_Save(const TimelineSnapshot& snapshot) {
    if (!g_initialized) return false;

    wchar_t fileName[256];
    MakeSnapshotId(snapshot, fileName, 256);

    wchar_t filePath[MAX_PATH];
    GetFilePath(fileName, snapshot.isCheckpoint, filePath, MAX_PATH);

    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"w, ccs=UTF-8");
    if (!fp) return false;

    // Write to temp file first, then rename for atomicity
    wchar_t tmpPath[MAX_PATH];
    wsprintfW(tmpPath, L"%s.tmp", filePath);

    _wfopen_s(&fp, tmpPath, L"w, ccs=UTF-8");
    if (!fp) return false;

    if (fwprintf(fp, L"# Desktop Layout Timeline\n") < 0 ||
        fwprintf(fp, L"Version: 1\n") < 0 ||
        fwprintf(fp, L"Timestamp: %s\n", snapshot.timestamp.c_str()) < 0 ||
        fwprintf(fp, L"Monitor: %dx%d, %d%%\n", snapshot.monitorWidth, snapshot.monitorHeight, snapshot.dpi) < 0 ||
        fwprintf(fp, L"IconCount: %d\n", snapshot.iconCount) < 0 ||
        fwprintf(fp, L"Checkpoint: %s\n", snapshot.isCheckpoint ? L"true" : L"false") < 0 ||
        fwprintf(fp, L"Name: %s\n", snapshot.name.c_str()) < 0 ||
        fwprintf(fp, L"Monitors: %d\n", snapshot.monitorCount) < 0) goto writeFail;

    for (int m = 0; m < snapshot.monitorCount; m++) {
        if (fwprintf(fp, L"Monitor: %d,%d,%d,%d\n",
            snapshot.monitors[m].left, snapshot.monitors[m].top,
            snapshot.monitors[m].right, snapshot.monitors[m].bottom) < 0) goto writeFail;
    }

    if (fwprintf(fp, L"---\n") < 0) goto writeFail;

    for (const auto& icon : snapshot.icons) {
        wchar_t safeName[MAX_PATH];
        wcsncpy_s(safeName, icon.name.c_str(), MAX_PATH - 1);
        for (wchar_t* p = safeName; *p; p++)
            if (*p == L'|' || *p == L'\n' || *p == L'\r') *p = L'_';
        if (fwprintf(fp, L"%s|%d|%d|%d|%d\n",
            safeName, icon.x, icon.y, icon.gridRow, icon.gridCol) < 0) goto writeFail;
    }

    if (fclose(fp) != 0) goto writeFail;
    fp = nullptr;

    if (!MoveFileExW(tmpPath, filePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmpPath);
        return false;
    }
    return true;

writeFail:
    if (fp) { fclose(fp); DeleteFileW(tmpPath); }
    return false;
}

// Load a snapshot from .dslayout file
bool Timeline_Load(const wchar_t* id, TimelineSnapshot* out) {
    if (!g_initialized || !out) return false;

    wchar_t filePath[MAX_PATH];
    // Try timeline dir first, then checkpoints
    wsprintfW(filePath, L"%s\\%s.dslayout", g_timelineDir, id);
    FILE* fp = nullptr;
    _wfopen_s(&fp, filePath, L"r, ccs=UTF-8");
    if (!fp) {
        wsprintfW(filePath, L"%s\\%s.dslayout", g_checkpointsDir, id);
        _wfopen_s(&fp, filePath, L"r, ccs=UTF-8");
    }
    if (!fp) return false;

    *out = {};
    out->id = id;

    wchar_t line[1024];
    bool inHeader = true;
    while (fgetws(line, 1024, fp)) {
        // Remove trailing newline
        size_t len = wcslen(line);
        while (len > 0 && (line[len-1] == L'\n' || line[len-1] == L'\r'))
            line[--len] = 0;

        if (inHeader) {
            if (wcscmp(line, L"---") == 0) {
                inHeader = false;
                continue;
            }
            if (line[0] == L'#') continue;

            // Parse header fields
            if (wcsncmp(line, L"Timestamp: ", 11) == 0)
                out->timestamp = line + 11;
            else if (wcsncmp(line, L"Name: ", 6) == 0)
                out->name = line + 6;
            else if (wcsncmp(line, L"IconCount: ", 11) == 0)
                out->iconCount = _wtoi(line + 11);
            else if (wcsncmp(line, L"Monitor: ", 9) == 0) {
                wchar_t* ctx = nullptr;
                wchar_t* res = wcstok(line + 9, L"x, %", &ctx);
                if (res) out->monitorWidth = _wtoi(res);
                res = wcstok(nullptr, L"x, %", &ctx);
                if (res) out->monitorHeight = _wtoi(res);
                res = wcstok(nullptr, L"x, %", &ctx);
                if (res) out->dpi = _wtoi(res);
            }
            else if (wcsncmp(line, L"Checkpoint: ", 12) == 0)
                out->isCheckpoint = (wcscmp(line + 12, L"true") == 0);
            else if (wcsncmp(line, L"Monitors: ", 10) == 0)
                out->monitorCount = _wtoi(line + 10);
            else if (wcsncmp(line, L"Monitor: ", 9) == 0) {
                // Each Monitor: line fills one slot up to monitorCount
                static int monIdx = 0;
                if (monIdx < 8 && monIdx < out->monitorCount) {
                    wchar_t* ctx = nullptr;
                    wchar_t* l = wcstok(line + 9, L",", &ctx);
                    wchar_t* t = wcstok(nullptr, L",", &ctx);
                    wchar_t* r = wcstok(nullptr, L",", &ctx);
                    wchar_t* b = wcstok(nullptr, L",", &ctx);
                    if (l && t && r && b) {
                        out->monitors[monIdx].left   = _wtoi(l);
                        out->monitors[monIdx].top    = _wtoi(t);
                        out->monitors[monIdx].right  = _wtoi(r);
                        out->monitors[monIdx].bottom = _wtoi(b);
                    }
                    monIdx++;
                }
            }
        } else {
            // Parse icon: Name|X|Y|GridRow|GridCol
            wchar_t* ctx = nullptr;
            wchar_t* name = wcstok(line, L"|", &ctx);
            wchar_t* xs   = wcstok(nullptr, L"|", &ctx);
            wchar_t* ys   = wcstok(nullptr, L"|", &ctx);
            wchar_t* rs   = wcstok(nullptr, L"|", &ctx);
            wchar_t* cs   = wcstok(nullptr, L"|", &ctx);

            if (name && xs && ys) {
                TimelineIcon icon;
                icon.name = name;
                icon.path = L"";  // Path not stored in v1 format
                icon.x = _wtoi(xs);
                icon.y = _wtoi(ys);
                icon.gridRow = rs ? _wtoi(rs) : 0;
                icon.gridCol = cs ? _wtoi(cs) : 0;
                out->icons.push_back(icon);
            }
        }
    }

    fclose(fp);

    // Infer auto-generated from name
    out->isAutoGenerated = (out->name.find(L"[Auto]") != std::wstring::npos);

    return true;
}

// List all snapshots (summaries only)
std::vector<TimelineSummary> Timeline_List(int maxCount) {
    std::vector<TimelineSummary> result;
    if (!g_initialized) return result;

    // Helper: scan one directory
    auto scanDir = [&](const wchar_t* dir, bool isCheckpoint) {
        wchar_t searchPath[MAX_PATH];
        wsprintfW(searchPath, L"%s\\*.dslayout", dir);

        WIN32_FIND_DATAW fd;
        HANDLE hFind = FindFirstFileW(searchPath, &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

            TimelineSummary sum = {};
            // Extract ID from filename (remove .dslayout)
            wcscpy_s(searchPath, fd.cFileName);
            wchar_t* dot = wcsrchr(searchPath, L'.');
            if (dot) *dot = 0;
            sum.id = searchPath;
            sum.isCheckpoint = isCheckpoint;

            // Format timestamp from file time
            SYSTEMTIME st;
            FileTimeToSystemTime(&fd.ftCreationTime, &st);
            wchar_t ts[64];
            wsprintfW(ts, L"%04d-%02d-%02dT%02d:%02d:%02d",
                     st.wYear, st.wMonth, st.wDay,
                     st.wHour, st.wMinute, st.wSecond);
            sum.timestamp = ts;

            // Quick read of header for name + iconCount
            wchar_t fullPath[MAX_PATH];
            wsprintfW(fullPath, L"%s\\%s", dir, fd.cFileName);
            FILE* fp = nullptr;
            _wfopen_s(&fp, fullPath, L"r, ccs=UTF-8");
            if (fp) {
                wchar_t line[512];
                while (fgetws(line, 512, fp)) {
                    if (wcsncmp(line, L"Name: ", 6) == 0) {
                        wchar_t* nl = wcschr(line, L'\n');
                        if (nl) *nl = 0;
                        nl = wcschr(line, L'\r');
                        if (nl) *nl = 0;
                        sum.name = line + 6;
                    }
                    else if (wcsncmp(line, L"IconCount: ", 11) == 0)
                        sum.iconCount = _wtoi(line + 11);
                    else if (line[0] == L'-' && line[1] == L'-' && line[2] == L'-')
                        break; // reached icon data section
                }
                fclose(fp);
            }

            sum.isAutoGenerated = (sum.name.find(L"[Auto]") != std::wstring::npos
                                || sum.name.find(L"auto") != std::wstring::npos);
            sum.changedCount = -1; // Unknown until diff computed

            result.push_back(sum);
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    };

    scanDir(g_timelineDir, false);
    scanDir(g_checkpointsDir, true);

    // Sort: checkpoints always first, then by timestamp descending
    std::sort(result.begin(), result.end(),
        [](const TimelineSummary& a, const TimelineSummary& b) {
            if (a.isCheckpoint != b.isCheckpoint) return a.isCheckpoint; // checkpoints first
            return a.timestamp > b.timestamp; // then newest first
        });

    // Apply maxCount limit
    if (maxCount > 0 && (int)result.size() > maxCount)
        result.resize(maxCount);

    return result;
}

bool Timeline_Delete(const wchar_t* id) {
    if (!g_initialized) return false;

    wchar_t filePath[MAX_PATH];
    wsprintfW(filePath, L"%s\\%s.dslayout", g_timelineDir, id);
    if (DeleteFileW(filePath)) return true;

    wsprintfW(filePath, L"%s\\%s.dslayout", g_checkpointsDir, id);
    return DeleteFileW(filePath) != 0;
}

int Timeline_Count() {
    if (!g_initialized) return 0;

    // Fast file count via FindFirstFile/FindNextFile — no file opens needed
    int count = 0;
    WIN32_FIND_DATAW fd;
    HANDLE hFind;

    wchar_t searchPath[MAX_PATH];
    wsprintfW(searchPath, L"%s\\*.dslayout", g_timelineDir);
    hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) count++; }
        while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    wsprintfW(searchPath, L"%s\\*.dslayout", g_checkpointsDir);
    hFind = FindFirstFileW(searchPath, &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do { if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) count++; }
        while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    return count;
}

bool Timeline_GetLatest(TimelineSummary* out) {
    auto list = Timeline_List(1);
    if (list.empty()) return false;
    if (out) *out = list[0];
    return true;
}

std::vector<TimelineIcon> Timeline_LoadIconsOnly(const wchar_t* id) {
    TimelineSnapshot snap;
    if (!Timeline_Load(id, &snap)) return {};
    return snap.icons;
}
