# Desktop Timeline

**A desktop icon bridge for LLMs.** Dump your desktop as JSON + screenshot,
tell an AI how you want it arranged (or show it another machine's layout),
and apply the result. Or wire it up as an MCP server and let the agent
read and arrange your desktop directly.

Single portable exe. No installer, no runtime, pure Win32.

## Two Ways To Use It

### 1. As an MCP server (agent-driven)

Add to `.mcp.json` (or `claude mcp add`):

```json
{
  "mcpServers": {
    "desktop": {
      "command": "C:\\path\\to\\DesktopIconSorter.exe",
      "args": ["--mcp"]
    }
  }
}
```

Then just talk: *"look at my desktop and put all the game shortcuts in the
bottom-left corner"*, *"rename that '新建 文本文档' to notes"*, *"save how it
looks now, then clean up the mess"*. The agent calls the tools itself.

### 2. Manual workflow (any chat LLM)

```bash
DesktopIconSorter.exe /shot      # writes desktop.png + desktop_state.json
```

Send both files to the LLM with your wish, e.g.:

- *"move everything toward the top-left, keep the recycle bin where it is"*
- *"把桌面右侧那一列常用的（Steam、微信、QQ）挪到左下角排成一列"*

It answers with a layout JSON; save it and apply:

```bash
DesktopIconSorter.exe /layout check layout.json   # dry-run report
DesktopIconSorter.exe /layout apply layout.json   # for real
```

Layout format (grid cells, or exact x/y pixels — pixels win):

```json
{ "icons": [
  { "name": "Google Chrome", "col": 0, "row": 0 },
  { "name": "Steam", "x": 21, "y": 1325 }
] }
```

## Features

- **Read** — full desktop state as JSON: screen/grid/metrics, settings,
  every icon with name, path, pixel position, grid cell, folder flag
- **Write** — move icons (one or many), create/rename/delete shortcuts and
  files (delete goes to the recycle bin), system icon visibility
- **Settings** — auto-arrange, snap-to-grid, icon size, icon spacing,
  This PC / Recycle Bin / Network / Control Panel / User files visibility
- **Screenshot** — inline PNG for the model to actually *see* the desktop
- **Undo net** — named checkpoints + auto-recorded history (`/snap`,
  `/list`, tray mode); `restore_snapshot` is one tool call away
- **Portable** — one exe, MIT, no dependencies

## CLI Reference

```
(no args) | /tray   Tray mode (auto-monitor, Ctrl+Shift+F12 = checkpoint)
/timeline           Snapshot panel (restore, rename, delete)
/dump               desktop_state.json (pure JSON on piped stdout)
/shot               desktop.png + desktop_state.json
/set autoarrange|snapgrid on|off
/set iconsize <px|small|medium|large>
/set spacing <h> <v>
/set sysicon <key> on|off          thispc|recycle|network|controlpanel|userfiles
/shortcut <name> <target> [args] [workdir] [icon]
/rename <name> <new_name>
/delete <name>                     to recycle bin
/layout save|apply|check <file>
/move <name> <col> <row>
/list                              snapshots, newest first
/snap save [name] | restore <name> | undo
/sort[:0-3]                        legacy partition sort (0=name 1=type 2=size 3=date)
--mcp                              MCP stdio server
```

## MCP Tools

| Tool | What it does |
|------|--------------|
| `list_icons` | Full desktop state JSON |
| `screenshot` | Desktop PNG, returned inline |
| `move_icon` | One icon → grid cell |
| `apply_layout` | Many icons → cells or pixels (`dry_run` to preview) |
| `create_shortcut` | New .lnk on the desktop |
| `rename_icon` | Rename an item (extension kept unless typed) |
| `delete_icon` | Delete an item (recycle bin) |
| `set_autoarrange` / `set_snapgrid` | Layout toggles |
| `set_iconsize` / `set_spacing` | Icon metrics |
| `set_system_icon` | Show/hide system icons |
| `save_snapshot` / `restore_snapshot` | Undo safety net |

## Build from Source

```bash
# Visual Studio 2026 with the v145 toolset, from DesktopIconSorter/
msbuild DesktopIconSorter.vcxproj /p:Configuration=Release /p:Platform=x64

python test_mcp.py    # end-to-end MCP test (moves one icon, restores it)
```

No external libraries — Windows SDK only.

## How It Works

```
src/core/
  icon_manager      COM IFolderView access
  grid_system       DPI-aware grid, calibrated from real icon positions
  layout_engine     layout files + per-icon moves
  desktop_state     JSON state export
  desktop_ops       settings, shortcuts, rename/delete
  screenshot        WIC PNG capture
  mcp_server        MCP stdio server
  timeline_*        .dslayout snapshots + auto-record engine
src/ui/             timeline panel
ShellExt/           right-click "sort by" menu
```

## License

MIT — do whatever you want.
