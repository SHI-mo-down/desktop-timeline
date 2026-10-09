# Desktop Timeline (DesktopIconSorter)

A Windows desktop icon bridge for LLMs and humans: a portable single exe
that reads/applies desktop icon layouts, changes desktop settings, and
speaks MCP over stdio so an agent can operate the desktop directly. The
original timeline (auto-record + restore) survives as the undo net.

Typical flow: dump state (JSON) + screenshot → hand to an LLM with the
wanted arrangement (or another machine's layout) → LLM writes a layout
file → apply it. With MCP wired up, the agent does all of that itself.

## Project Overview

- **Language:** C++20, Win32/COM, Windows SDK only — zero external deps
- **Build:** Visual Studio 2026 (v145), x64 Release
- **Portable:** one exe, no installer, no runtime

## Architecture

```
src/main.cpp            CLI dispatch + tray mode + console mode
src/core/
  icon_manager          IFolderView COM access: GetDesktopIcons, RefreshDesktopView
  grid_system           DPI-aware grid, calibrated from real icon positions
  layout_engine         apply_layout (ApplyJson/ApplyFile), MoveIcon, ApplyPairs
  desktop_state         BuildJson/Export — the /dump format
  desktop_ops           settings (autoarrange/snapgrid/iconsize/spacing/sysicon),
                        shortcuts, rename, delete-to-recycle-bin
  screenshot            virtual-screen BitBlt -> PNG (WIC)
  mcp_server            MCP stdio server, 14 tools (thin wrappers over core)
  timeline_db           .dslayout storage (timeline/ auto, checkpoints/ named)
  timeline_engine       poll/debounce/auto-record + SaveCheckpoint/Restore
  timeline_record.h     TimelineSnapshot/TimelineIcon structs
  cleanup_policy        periodic pruning of old auto-records
  sort_engine           partition sort for /sort (legacy but exported)
  config_manager        partition config load (desktop_sorter_config.json)
src/ui/timeline_ui      snapshot panel (list/restore/rename/delete)
src/utils/              com_ptr.h, console.h (Unicode-safe print), json_extract.h
ShellExt/               right-click menu, ShellExecute exe /sort:N
test_mcp.py             end-to-end MCP test (moves one icon, restores it)
```

## Runtime Commands

```
(no args) | /tray   Tray mode: auto-monitor + checkpoints (Ctrl+Shift+F12)
/timeline           Open the snapshot panel
/dump               Write desktop_state.json (pure JSON on piped stdout)
/shot               desktop.png + state JSON
/set autoarrange|snapgrid on|off
/set iconsize <px|small|medium|large>   /set spacing <h> <v>
/set sysicon thispc|recycle|network|controlpanel|userfiles on|off
/shortcut <name> <target> [args] [workdir] [icon]
/rename <name> <new_name>        keeps extension unless the new name has one
/delete <name>                   to recycle bin (undoable)
/layout save|apply|check <file>  apply: exact x/y wins over col/row
/move <name> <col> <row>
/list                            stored snapshots (newest first)
/snap save [name] | restore <name> | undo
/sort[:0-3]                      legacy partition sort (0=name 1=type 2=size 3=date)
--mcp                            MCP stdio server (see below)
```

## MCP Mode (--mcp)

- Newline-delimited JSON-RPC 2.0 on stdin/stdout, one message per line.
- **stdout must carry ONLY protocol messages** — any core function called
  from the MCP path must not print. desktop_state has BuildJson (silent)
  vs Export (echoes, CLI only).
- Tools (14): list_icons, screenshot (inline PNG), move_icon, apply_layout
  (+dry_run), create_shortcut, rename_icon, delete_icon, set_autoarrange,
  set_snapgrid, set_iconsize, set_spacing, set_system_icon, save_snapshot,
  restore_snapshot.
- `.mcp.json` at repo root points Claude Code at
  `DesktopIconSorter\x64\Release\DesktopIconSorter.exe` with `--mcp`.
- The running server **locks the exe**: kill it before rebuilding
  (`powershell -Command "Get-CimInstance Win32_Process -Filter \"Name='DesktopIconSorter.exe'\" | ft ProcessId,CommandLine"` then `taskkill /PID <pid> /F`).

## Build & Test

```bash
# from DesktopIconSorter/ (works in git-bash; MSYS needs // for / flags)
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" \
  DesktopIconSorter.vcxproj //p:Configuration=Release //p:Platform=x64 //v:minimal //nologo

python test_mcp.py    # 25-check end-to-end; moves one real icon and
                      # restores it via checkpoint — safe, self-cleaning
```

CLI args in git-bash also need `//dump` (not `/dump`) — MSYS converts
leading-slash args to paths.

## Key Design Decisions & Gotchas

1. **Grid calibration** — `LVM_GETITEMSPACING` lies (reported 144x73 where
   the real pitch is 115x147 @144dpi). `RefineCellSize/RefineOrigin` in
   grid_system calibrate from actual icon positions (median of consecutive
   same-row/same-column deltas). Always prefer exact x/y when both present.
2. **Storage** — `.dslayout` text files under `<exeDir>\timeline\` (auto)
   and `<exeDir>\checkpoints\` (named). One save/restore path:
   `TimelineEngine_SaveCheckpoint(name)` / `TimelineEngine_Restore(id)`;
   checkpoint id = `Timeline_MakeCheckpointId(name)` ("checkpoint_"+name,
   unsafe chars replaced).
3. **Tolerant JSON** (utils/json_extract.h) — finds keys by substring,
   accepts quoted scalars ("3", "true") because LLMs stringify. Does NOT
   handle escaped quotes inside strings — documents reject rather than
   misparse.
4. **Shell refresh is async** — after filesystem ops (create/rename/delete)
   the shell updates the desktop view ~1s later. Our own ops send
   SHChangeNotify; the test sleeps 1.5s after such steps before asserting.
5. **Move mechanics** — `IFolderView::SelectAndPositionItems(SVSI_POSITIONITEM)`;
   settings via `IFolderView2::SetCurrentFolderFlags` + registry best-effort.
6. **Safety** — MCP `save_snapshot` before big changes; snapshot names are
   validated (1-100 chars, no path chars); delete always to recycle bin.
7. **Two exe outputs** — DesktopIconSorter.exe is the app; the ShellExt
   DLL only ShellExecutes it, no code coupling (contract: `/sort:N`).

## Conventions

- `#pragma once`; `std::wstring` / `wchar_t*` everywhere; COM via
  `ComPtr<T>` (utils/com_ptr.h); file I/O UTF-8 via `_wfopen_s`.
- Comments only for non-obvious "why"; modules use static state in .cpp.
