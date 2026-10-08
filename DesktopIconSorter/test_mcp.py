"""MCP roundtrip test for DesktopIconSorter --mcp. Restores desktop afterwards.
Run from anywhere: exe resolved to x64/Release/DesktopIconSorter.exe.
Moves one real icon and restores it via snapshot; requires the built exe."""
import base64, json, os, subprocess, sys, time

EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "x64", "Release", "DesktopIconSorter.exe")
p = subprocess.Popen([EXE, "--mcp"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, text=False)
fails = []

def send(obj):
    p.stdin.write((json.dumps(obj) + "\n").encode())
    p.stdin.flush()
    raw = p.stdout.readline()
    if not raw:
        raise RuntimeError("server closed: " + p.stderr.read().decode(errors="replace"))
    return json.loads(raw.decode())

def call(mid, tool, args=None):
    m = {"jsonrpc": "2.0", "id": mid, "method": "tools/call",
         "params": {"name": tool, "arguments": args if args is not None else {}}}
    return send(m)

def text_of(resp):
    return resp["result"]["content"][0]["text"]

def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + ((" - " + detail) if detail and not cond else ""))
    if not cond:
        fails.append(name)

init = send({"jsonrpc": "2.0", "id": 0, "method": "initialize",
             "params": {"protocolVersion": "2025-06-18", "capabilities": {},
                        "clientInfo": {"name": "test", "version": "0"}}})
check("initialize", init["result"]["serverInfo"]["name"] == "desktop-timeline")

# 1. list_icons -> parseable state
r = call(10, "list_icons")
state = json.loads(text_of(r))
orig = {i["name"]: (i["x"], i["y"]) for i in state["icons"]}
check("list_icons parse", len(orig) > 0, f"{len(orig)} icons")
cols = state["grid"]["columns"]
print("  icons:", len(orig), "grid:", state["grid"])

# 2. safety net
r = call(11, "save_snapshot", {"name": "mcp-test"})
check("save_snapshot", "saved" in text_of(r), text_of(r))

# 3. move_icon one icon +1 col
target = next(iter(orig))
tinfo = next(i for i in state["icons"] if i["name"] == target)
tcol, trow = tinfo["col"], tinfo["row"]
newcol = tcol + 1 if tcol + 1 < cols else tcol - 1
r = call(12, "move_icon", {"name": target, "col": newcol, "row": trow})
check("move_icon", '"moved"' in text_of(r) or "moved" in text_of(r), text_of(r))
r = call(13, "list_icons")
after = {i["name"]: (i["x"], i["y"]) for i in json.loads(text_of(r))["icons"]}
moved = [n for n in orig if after.get(n) != orig[n]]
check("move_icon changed layout", moved == [target], f"changed: {moved}")

# 4. apply_layout dry_run (no side effects)
layout = {"icons": [{"name": target, "col": tcol, "row": trow}], "dry_run": True}
r = call(14, "apply_layout", layout)
rep = json.loads(text_of(r)) if "text" in r["result"]["content"][0] else {}
check("apply_layout dry_run", rep.get("moved") == 1 and rep.get("auto_arrange_on") in (True, False),
      text_of(r))
r = call(15, "list_icons")
still = {i["name"]: (i["x"], i["y"]) for i in json.loads(text_of(r))["icons"]}
check("dry_run did not move", still == after)

# 5. apply_layout real (restore this one icon via apply, then snapshot-restore all)
r = call(16, "apply_layout", {"icons": [{"name": target, "col": tcol, "row": trow}]})
rep = json.loads(text_of(r))
check("apply_layout real", rep["moved"] == 1 and rep["missing"] == [], text_of(r))

# 6. restore_snapshot -> everything back to exact positions
r = call(17, "restore_snapshot", {"name": "mcp-test"})
check("restore_snapshot", "restored" in text_of(r), text_of(r))
r = call(18, "list_icons")
final = {i["name"]: (i["x"], i["y"]) for i in json.loads(text_of(r))["icons"]}
diff = [n for n in orig if final.get(n) != orig[n]]
check("desktop fully restored", diff == [], f"diff: {diff}")
check("icon set unchanged", set(final) == set(orig))

# 6b. tolerant parsing + input validation
r = call(30, "restore_snapshot", {"name": "x" * 150})
check("long snapshot name rejected", r["result"].get("isError") is True)
r = call(31, "apply_layout", {"icons": [{"name": target, "col": str(tcol), "row": str(trow)}],
                              "dry_run": "true"})
rep = json.loads(text_of(r))
check("quoted scalars parse (str col/row, str dry_run)", rep["moved"] == 1, text_of(r))
r = call(32, "list_icons")
still = {i["name"]: (i["x"], i["y"]) for i in json.loads(text_of(r))["icons"]}
check("quoted dry_run did not move", still == orig)

# 6c. create/rename/delete roundtrip on a throwaway item (net zero change)
# sleeps cover the shell's async view refresh after each filesystem op
r = call(40, "create_shortcut", {"name": "mcp-item-test", "target": "notepad.exe"})
check("create_shortcut", "created" in text_of(r), text_of(r))
time.sleep(1.5)
r = call(41, "rename_icon", {"name": "mcp-item-test", "new_name": "mcp-item-renamed"})
check("rename_icon", "renamed" in text_of(r), text_of(r))
time.sleep(1.5)
r = call(42, "delete_icon", {"name": "mcp-item-renamed"})
check("delete_icon", "recycle bin" in text_of(r), text_of(r))
time.sleep(1.5)
r = call(43, "list_icons")
names = [i["name"] for i in json.loads(text_of(r))["icons"]]
check("test item gone, set unchanged", "mcp-item-renamed" not in names
      and "mcp-item-test" not in names and set(names) == set(orig))
r = call(44, "rename_icon", {"name": "no such item zzz", "new_name": "x"})
check("rename missing -> isError", r["result"].get("isError") is True, text_of(r))

# 7. screenshot -> image block with PNG magic
r = call(19, "screenshot")
content = r["result"]["content"][0]
ok = content["type"] == "image" and content["mimeType"] == "image/png"
raw = base64.b64decode(content["data"]) if ok else b""
check("screenshot PNG", ok and raw[:8] == b"\x89PNG\r\n\x1a\n" and len(raw) > 10000,
      f"{len(raw)} bytes")

# 8. settings read-only paths
r = call(20, "set_iconsize", {})
check("set_iconsize read", text_of(r).startswith("icon_size:"), text_of(r))
r = call(21, "set_autoarrange", {"on": state["settings"]["auto_arrange"]})
check("set_autoarrange same value", "auto_arrange:" in text_of(r), text_of(r))

# 9. error paths
r = call(22, "not_a_tool")
check("unknown tool -> -32602", r.get("error", {}).get("code") == -32602)
r = call(23, "move_icon", {"name": "no such icon xyz"})
check("move missing icon -> isError", r["result"].get("isError") is True, text_of(r))

# 10. notification gets no response (send then ping; only ping answers)
p.stdin.write((json.dumps({"jsonrpc": "2.0", "method": "notifications/initialized"}) + "\n").encode())
p.stdin.flush()
r = send({"jsonrpc": "2.0", "id": 24, "method": "ping"})
check("notification skipped", r.get("id") == 24)

p.stdin.close()
p.wait(timeout=10)
print("\n" + ("ALL PASS" if not fails else f"FAILURES: {fails}"))
sys.exit(1 if fails else 0)
