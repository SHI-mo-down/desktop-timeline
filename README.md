# Desktop Timeline

**Git for your desktop icons.** Auto-records layout changes. Restore any previous state. No more scrambled icons after monitor unplugs or accidental "Sort by Name".

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%2B-blue)

## Quick Start

1. Download `DesktopIconSorter.exe` from [Releases]()
2. Run it — starts in system tray
3. Move some icons around
4. Double-click tray icon to open Timeline panel
5. Select a snapshot → Restore

That's it. Zero setup, zero dependencies.

## Features

- **Auto-record** — detects layout changes, saves snapshots automatically (same layout = no duplicate)
- **Restore any layout** — double-click any snapshot in the timeline
- **Manual checkpoints** — Ctrl+Shift+F12 or tray menu to save named snapshots
- **Layout rules** — optional `rules.dtrules` file to auto-organize icons into zones
- **Clean Win32 native** — no runtime, no framework, 1MB exe

## Keyboard Shortcuts (Timeline Panel)

| Key | Action |
|-----|--------|
| Enter | Restore selected layout |
| Delete | Delete selected record |
| F2 | Rename checkpoint |
| Double-click | Restore |

## Layout Rules

Create a `rules.dtrules` file next to the exe. Example:

```json
{
  "name": "My Layout",
  "on_startup": "apply",
  "on_new_icon": "place",
  "zones": [
    {
      "name": "shortcuts",
      "x": 0, "y": 0, "w": 25, "h": 100,
      "unit": "percent",
      "match": [".lnk", ".url"],
      "match_mode": "extension",
      "fill": "column",
      "sort": "name"
    },
    {
      "name": "documents",
      "x": 25, "y": 0, "w": 55, "h": 100,
      "match": [".pdf", ".docx", ".xlsx", ".txt", ".md"],
      "fill": "row",
      "sort": "date",
      "sort_dir": "desc"
    },
    {
      "name": "recycle_bin",
      "x": 94, "y": 94, "w": 6, "h": 6,
      "match": ["Recycle Bin", "回收站"],
      "match_mode": "exact",
      "fill": "column"
    }
  ]
}
```

### Rule Fields

| Field | Values | Description |
|-------|--------|-------------|
| `on_startup` | `apply`, `none` | Rearrange on launch? |
| `on_new_icon` | `place`, `ignore` | Auto-place new icons? |
| `match_mode` | `extension`, `name`, `folder`, `exact` | How to match icons |
| `fill` | `column`, `row` | Fill direction within zone |
| `sort` | `none`, `name`, `date`, `size` | Sort order within zone |
| `unit` | `percent`, `pixel` | Zone coordinate unit |
| `monitor` | 0, 1, ... | Target monitor index |

### LLM Workflow

1. Screenshot your desktop
2. Send to ChatGPT/Claude: "Shortcuts on the left, documents centered, Recycle Bin bottom-right. My icons are: [list]. Screen is 2560x1440."
3. Paste the generated JSON into `rules.dtrules`
4. Restart the program (or change `on_startup` to `apply`)

## Build from Source

```
Requirements: Visual Studio 2022+ with C++ Desktop Development workload
```

```bash
msbuild DesktopIconSorter.vcxproj /p:Configuration=Release /p:Platform=x64
```

No external libraries. Pure Win32 + COM.

## Architecture

```
src/core/
  icon_manager      COM IFolderView wrapper
  timeline_engine   Polling, debounce, auto-record
  timeline_db       .dslayout file-based storage
  rule_engine       Layout rule parser & applier
  cleanup_policy    Auto-cleanup old records
  sort_engine       Legacy multi-zone sort
src/ui/
  timeline_ui       Snapshot list + restore panel
ShellExt/           Right-click context menu extension
```

## License

MIT — do whatever you want.
