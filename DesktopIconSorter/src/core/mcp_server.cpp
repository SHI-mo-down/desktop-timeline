// mcp_server.cpp : MCP stdio server — JSON-RPC 2.0 over stdin/stdout
// Tools are atomic operations over the same core as the CLI. Only protocol
// messages may touch stdout; core functions used here must not print.
#include "mcp_server.h"
#include "desktop_state.h"
#include "desktop_ops.h"
#include "layout_engine.h"
#include "screenshot.h"
#include "grid_system.h"
#include "snapshot_manager.h"
#include "../utils/console.h"
#include "../utils/json_extract.h"
#include <io.h>
#include <fcntl.h>
#include <strsafe.h>
#include <stdio.h>
#include <string>
#include <vector>

extern wchar_t g_exeDir[MAX_PATH];  // main.cpp
void InitPaths();                   // main.cpp

// ── Line reader (MCP stdio: one JSON-RPC message per line, UTF-8) ──

static std::string g_pending;

static bool ReadLine(std::string& line) {
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    char buf[8192];
    for (;;) {
        size_t nl = g_pending.find('\n');
        if (nl != std::string::npos) {
            line.assign(g_pending, 0, nl);
            g_pending.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        DWORD n = 0;
        if (!ReadFile(h, buf, sizeof(buf), &n, nullptr) || n == 0) {
            if (g_pending.empty()) return false;
            line.swap(g_pending);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        g_pending.append(buf, n);
        // Runaway stream guard: a line this large is not a valid MCP message
        if (g_pending.size() > 16 * 1024 * 1024) g_pending.clear();
    }
}

static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (wlen <= 0) return L"";
    std::wstring out(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], wlen);
    return out;
}

// ── Response builders (single-line JSON) ──

static std::wstring RespOk(const std::wstring& id, const std::wstring& result) {
    return L"{\"jsonrpc\":\"2.0\",\"id\":" + id + L",\"result\":" + result + L"}";
}

static std::wstring RespErr(const std::wstring& id, int code, const std::wstring& msg) {
    return L"{\"jsonrpc\":\"2.0\",\"id\":" + id + L",\"error\":{\"code\":" +
           std::to_wstring(code) + L",\"message\":\"" + Json_Escape(msg) + L"\"}}";
}

static std::wstring ContentText(const std::wstring& id, const std::wstring& text,
                                bool isError = false) {
    std::wstring r = L"{\"content\":[{\"type\":\"text\",\"text\":\"" +
                     Json_Escape(text) + L"\"}]";
    if (isError) r += L",\"isError\":true";
    return RespOk(id, r + L"}");
}

static std::wstring WStrArray(const std::vector<std::wstring>& v) {
    std::wstring out = L"[";
    for (size_t i = 0; i < v.size(); i++)
        out += L"\"" + Json_Escape(v[i]) + L"\"" + (i + 1 < v.size() ? L"," : L"");
    return out + L"]";
}

// ── Base64 (screenshot content blocks) ──

static std::string Base64Encode(const std::vector<unsigned char>& data) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < data.size(); i += 3) {
        unsigned v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];  out += tbl[v & 63];
    }
    if (i + 1 == data.size()) {
        unsigned v = data[i] << 16;
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == data.size()) {
        unsigned v = (data[i] << 16) | (data[i + 1] << 8);
        out += tbl[(v >> 18) & 63]; out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];  out += "=";
    }
    return out;
}

// ── Tool catalog ──

static const wchar_t* kToolsJson = LR"TOOLS([
{"name":"list_icons","description":"Get the current desktop layout as JSON: screen size, DPI, grid metrics (origin, cell size, columns/rows), settings (auto-arrange, snap-to-grid, icon size, spacing, system icons) and every desktop icon with name, path, pixel position (x,y), grid cell (col,row) and is-folder flag. Call this before proposing any layout change.","inputSchema":{"type":"object","properties":{},"required":[]}},
{"name":"screenshot","description":"Capture the desktop as a PNG image so you can see the actual arrangement. Use together with list_icons.","inputSchema":{"type":"object","properties":{},"required":[]}},
{"name":"move_icon","description":"Move a single desktop icon to a grid cell (col,row). Grid origin and cell size are in the list_icons output.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"Icon display name as shown by list_icons"},"col":{"type":"integer","minimum":0},"row":{"type":"integer","minimum":0}},"required":["name","col","row"]}},
{"name":"apply_layout","description":"Move many icons at once. icons is an array of objects, each with name plus either col+row (grid cell) or x+y (exact pixels - preferred when copying a layout that has them). Returns a report: moved, missing (in layout but not on desktop), failed, untouched (on desktop but not in layout), auto_arrange_on (while true, positions will NOT stick - call set_autoarrange first). Use dry_run=true to preview without moving.","inputSchema":{"type":"object","properties":{"icons":{"type":"array","items":{"type":"object","properties":{"name":{"type":"string"},"col":{"type":"integer","minimum":0},"row":{"type":"integer","minimum":0},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["name"]}},"dry_run":{"type":"boolean","description":"Report what would change without moving anything (default false)"}},"required":["icons"]}},
{"name":"create_shortcut","description":"Create a shortcut (.lnk) on the desktop.","inputSchema":{"type":"object","properties":{"name":{"type":"string","description":"File name, .lnk may be omitted"},"target":{"type":"string","description":"Target executable, file, folder or URL"},"args":{"type":"string"},"workdir":{"type":"string"},"icon":{"type":"string","description":"Icon file path"}},"required":["name","target"]}},
{"name":"set_autoarrange","description":"Turn Windows 'Auto arrange icons' on or off. While ON, positions set by apply_layout/move_icon will not stick; turn it off before arranging. Returns the value actually applied.","inputSchema":{"type":"object","properties":{"on":{"type":"boolean"}},"required":["on"]}},
{"name":"set_snapgrid","description":"Turn 'Align icons to grid' on or off. Keep ON for tidy grid layouts; turn OFF to allow arbitrary pixel positions.","inputSchema":{"type":"object","properties":{"on":{"type":"boolean"}},"required":["on"]}},
{"name":"set_iconsize","description":"Set desktop icon size in pixels (16-512) or \"small\"/\"medium\"/\"large\" (32/48/96). Omit size to just read the current value.","inputSchema":{"type":"object","properties":{"size":{"type":["integer","string"]}},"required":[]}},
{"name":"set_spacing","description":"Set desktop icon spacing in pixels (minimum 16). Also visible as cell size in list_icons grid after the view refreshes.","inputSchema":{"type":"object","properties":{"h":{"type":"integer","minimum":16},"v":{"type":"integer","minimum":16}},"required":["h","v"]}},
{"name":"set_system_icon","description":"Show or hide a Windows system desktop icon (This PC, Recycle Bin, Network, Control Panel, User files).","inputSchema":{"type":"object","properties":{"key":{"type":"string","enum":["thispc","recycle","network","controlpanel","userfiles"]},"visible":{"type":"boolean"}},"required":["key","visible"]}},
{"name":"save_snapshot","description":"Save the current desktop layout as a named snapshot - the safety net before rearranging.","inputSchema":{"type":"object","properties":{"name":{"type":"string"}},"required":["name"]}},
{"name":"restore_snapshot","description":"Restore the desktop layout from a named snapshot created by save_snapshot (undo).","inputSchema":{"type":"object","properties":{"name":{"type":"string"}},"required":["name"]}}
])TOOLS";

// Source formatting gives kToolsJson embedded newlines; JSON-RPC stdio
// framing requires exactly one message per line.
static const std::wstring& ToolsJsonOneLine() {
    static std::wstring cached;
    if (cached.empty()) {
        for (wchar_t c : std::wstring(kToolsJson))
            if (c != L'\n' && c != L'\r') cached += c;
    }
    return cached;
}

// ── Handlers ──

// Snapshot names come from the client: keep them plain file names
static bool ValidSnapshotName(const std::wstring& name) {
    return !name.empty() && name.size() <= 100 &&
           name.find_first_of(L"\\/:*?\"<>|") == std::wstring::npos;
}

static std::wstring HandleInitialize(const std::wstring& id, const std::wstring& req) {
    std::wstring ver = Json_ExtractString(req, L"protocolVersion");
    if (ver.empty()) ver = L"2024-11-05";
    return RespOk(id,
        L"{\"protocolVersion\":\"" + Json_Escape(ver) + L"\","
        L"\"capabilities\":{\"tools\":{}},"
        L"\"serverInfo\":{\"name\":\"desktop-timeline\",\"version\":\"1.0.0\"},"
        L"\"instructions\":\"Desktop icon layout bridge for Windows. Workflow: 1) call "
        L"list_icons and/or screenshot to see the current desktop; 2) design a layout "
        L"(grid col/row, or exact x/y pixels); 3) call save_snapshot for a safety net, "
        L"set_autoarrange off if needed, then apply_layout (dry_run first if unsure); "
        L"4) verify with list_icons or screenshot. Settings tools control auto-arrange, "
        L"snap-to-grid, icon size, spacing and system icons.\"}");
}

static std::wstring HandleScreenshot(const std::wstring& id) {
    // Unique temp name: two MCP instances must not share the temp file
    wchar_t tmpDir[MAX_PATH], path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tmpDir) ||
        !GetTempFileNameW(tmpDir, L"dtl", 0, path))
        return ContentText(id, L"screenshot failed", true);
    if (!Desktop_Screenshot(path)) {
        DeleteFileW(path);
        return ContentText(id, L"screenshot failed", true);
    }

    std::string b64;
    bool ok = false;
    FILE* fp = nullptr;
    _wfopen_s(&fp, path, L"rb");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (size > 0) {
            std::vector<unsigned char> data(size);
            ok = fread(data.data(), 1, size, fp) == (size_t)size;
            if (ok) b64 = Base64Encode(data);
        }
        fclose(fp);
    }
    DeleteFileW(path);
    if (!ok) return ContentText(id, L"screenshot read failed", true);

    return RespOk(id, L"{\"content\":[{\"type\":\"image\",\"data\":\"" +
                  Utf8ToW(b64) + L"\",\"mimeType\":\"image/png\"}]}");
}

static std::wstring HandleToolCall(const std::wstring& id, const std::wstring& req) {
    std::wstring tool = Json_ExtractString(req, L"name");

    // Tool arguments: raw inner text of the "arguments" object. It doubles as
    // the layout document for apply_layout (must contain "icons").
    std::wstring args;
    size_t a = 0, b = 0;
    if (Json_FindObjectBlock(req, L"arguments", &a, &b))
        args = req.substr(a, b - a);

    if (tool == L"list_icons") {
        std::wstring j = DesktopState_BuildJson();
        if (j.empty()) return ContentText(id, L"no desktop icons found or desktop unreadable", true);
        return ContentText(id, j);
    }

    if (tool == L"screenshot") return HandleScreenshot(id);

    if (tool == L"move_icon") {
        std::wstring name = Json_ExtractString(args, L"name");
        int col = Json_ExtractInt(args, L"col", -1);
        int row = Json_ExtractInt(args, L"row", -1);
        if (name.empty() || col < 0 || row < 0)
            return ContentText(id, L"need name, col (>=0), row (>=0)", true);
        if (LayoutEngine_MoveIcon(name.c_str(), col, row))
            return ContentText(id, L"moved \"" + name + L"\" to col " +
                                    std::to_wstring(col) + L", row " + std::to_wstring(row));
        return ContentText(id, L"move failed: \"" + name +
                                L"\" not found or could not be positioned", true);
    }

    if (tool == L"apply_layout") {
        bool dryRun = Json_ExtractBool(args, L"dry_run", false);
        LayoutApplyResult r = LayoutEngine_ApplyJson(args, dryRun);
        if (r.moved == 0 && r.missing.empty() && r.failed.empty())
            return ContentText(id, L"nothing to do: layout must contain an \"icons\" "
                                    L"array with name plus col/row or x/y", true);
        std::wstring report =
            L"{\"moved\":" + std::to_wstring(r.moved) +
            L",\"missing\":" + WStrArray(r.missing) +
            L",\"failed\":" + WStrArray(r.failed) +
            L",\"untouched\":" + WStrArray(r.untouched) +
            L",\"auto_arrange_on\":" + (r.autoArrangeOn ? L"true" : L"false") + L"}";
        return ContentText(id, report);
    }

    if (tool == L"create_shortcut") {
        std::wstring name = Json_ExtractString(args, L"name");
        std::wstring target = Json_ExtractString(args, L"target");
        if (name.empty() || target.empty())
            return ContentText(id, L"need name and target", true);
        std::wstring sa = Json_ExtractString(args, L"args");
        std::wstring wd = Json_ExtractString(args, L"workdir");
        std::wstring ico = Json_ExtractString(args, L"icon");
        bool ok = Desktop_CreateShortcut(name.c_str(), target.c_str(),
                                         sa.empty() ? nullptr : sa.c_str(),
                                         wd.empty() ? nullptr : wd.c_str(),
                                         ico.empty() ? nullptr : ico.c_str());
        return ContentText(id, ok ? L"shortcut \"" + name + L"\" created"
                                  : L"shortcut creation failed", !ok);
    }

    if (tool == L"set_autoarrange" || tool == L"set_snapgrid") {
        if (args.find(L"\"on\"") == std::wstring::npos)
            return ContentText(id, L"missing required argument: on (true|false)", true);
        bool on = Json_ExtractBool(args, L"on");
        if (tool == L"set_autoarrange") {
            Desktop_SetAutoArrange(on);
            return ContentText(id, Desktop_GetAutoArrange() ? L"auto_arrange: on"
                                                            : L"auto_arrange: off");
        }
        Desktop_SetSnapToGrid(on);
        return ContentText(id, Desktop_GetSnapToGrid() ? L"snapgrid: on" : L"snapgrid: off");
    }

    if (tool == L"set_iconsize") {
        std::wstring tok = Json_ExtractRawValue(args, L"size");
        int px = -1;
        bool valid = true;
        if (tok.empty()) {
            // no size given: read-only call
        } else if (tok[0] == L'"') {
            std::wstring s = tok.substr(1, tok.size() - 2);
            if (_wcsicmp(s.c_str(), L"small") == 0) px = 32;
            else if (_wcsicmp(s.c_str(), L"medium") == 0) px = 48;
            else if (_wcsicmp(s.c_str(), L"large") == 0) px = 96;
            else valid = false;
        } else {
            px = _wtoi(tok.c_str());
            if (px < 16 || px > 512) valid = false;
        }
        if (!valid) return ContentText(id, L"size must be 16-512 or small|medium|large", true);
        if (px > 0 && !Desktop_SetIconSize(px))
            return ContentText(id, L"set icon size failed", true);
        return ContentText(id, L"icon_size: " + std::to_wstring(Desktop_GetIconSize()));
    }

    if (tool == L"set_spacing") {
        if (args.find(L"\"h\"") == std::wstring::npos ||
            args.find(L"\"v\"") == std::wstring::npos)
            return ContentText(id, L"need h and v (pixels, min 16)", true);
        int h = Json_ExtractInt(args, L"h", 0);
        int v = Json_ExtractInt(args, L"v", 0);
        if (h < 16 || v < 16) return ContentText(id, L"spacing must be >= 16 px", true);
        if (!Desktop_SetSpacing(h, v))
            return ContentText(id, L"set spacing failed", true);
        int gh = 0, gv = 0;
        Desktop_GetSpacing(&gh, &gv);
        return ContentText(id, L"spacing: " + std::to_wstring(gh) + L" x " + std::to_wstring(gv));
    }

    if (tool == L"set_system_icon") {
        std::wstring key = Json_ExtractString(args, L"key");
        if (key.empty() || args.find(L"\"visible\"") == std::wstring::npos)
            return ContentText(id, L"need key (thispc|recycle|network|controlpanel|"
                                    L"userfiles) and visible (true|false)", true);
        bool visible = Json_ExtractBool(args, L"visible");
        if (!Desktop_SetSystemIcon(key.c_str(), visible))
            return ContentText(id, L"unknown system icon key: " + key, true);
        return ContentText(id, key + (visible ? L": visible" : L": hidden"));
    }

    if (tool == L"save_snapshot") {
        std::wstring name = Json_ExtractString(args, L"name");
        if (!ValidSnapshotName(name))
            return ContentText(id, L"invalid snapshot name (1-100 chars, "
                                    L"no \\ / : * ? \" < > |)", true);
        if (!Snapshot_Save(name.c_str()))
            return ContentText(id, L"snapshot save failed", true);
        return ContentText(id, L"snapshot \"" + name + L"\" saved");
    }

    if (tool == L"restore_snapshot") {
        std::wstring name = Json_ExtractString(args, L"name");
        if (!ValidSnapshotName(name))
            return ContentText(id, L"invalid snapshot name (1-100 chars, "
                                    L"no \\ / : * ? \" < > |)", true);
        wchar_t fp[MAX_PATH];
        if (FAILED(StringCchPrintfW(fp, MAX_PATH, L"%s\\snapshots\\manual\\%s.snap",
                                    g_exeDir, name.c_str())))
            return ContentText(id, L"snapshot path too long", true);
        if (!Snapshot_Restore(fp))
            return ContentText(id, L"snapshot not found or restore failed: " + name, true);
        return ContentText(id, L"snapshot \"" + name + L"\" restored");
    }

    return RespErr(id, -32602, L"unknown tool: " + tool);
}

// Returns empty string for notifications (no id → no response).
static std::wstring Dispatch(const std::wstring& req) {
    std::wstring id = Json_ExtractRawValue(req, L"id");
    if (id.empty()) return L"";
    std::wstring method = Json_ExtractString(req, L"method");
    if (method == L"initialize") return HandleInitialize(id, req);
    if (method == L"ping") return RespOk(id, L"{}");
    if (method == L"tools/list") return RespOk(id, L"{\"tools\":" + ToolsJsonOneLine() + L"}");
    if (method == L"tools/call") return HandleToolCall(id, req);
    return RespErr(id, -32601, L"method not found: " + method);
}

int Mcp_Run() {
    InitPaths();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    GridSystem_Init();

    // Protocol stream must be exact bytes — no CRLF translation
    _setmode(_fileno(stdout), _O_BINARY);

    std::string line;
    while (ReadLine(line)) {
        if (line.empty()) continue;
        std::wstring req = Utf8ToW(line);
        if (req.empty()) continue;
        std::wstring resp = Dispatch(req);
        if (!resp.empty()) Console_PrintW(resp + L"\n");
    }

    CoUninitialize();
    return 0;
}
