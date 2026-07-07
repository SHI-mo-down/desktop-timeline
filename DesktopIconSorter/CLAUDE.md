# Desktop Timeline

A Windows desktop icon layout manager that works like Git for your desktop icons.

## Project Overview

- **Language:** C++20, Win32 API
- **Build:** Visual Studio 2026 (v145 toolset), x64 Release
- **No external dependencies** - Windows SDK only

## Architecture

```
DesktopIconSorter/
├── src/
│   ├── main.cpp                 # Entry point: tray mode, console CLI
│   ├── core/
│   │   ├── icon_manager.h/cpp   # IFolderView COM interface wrapper
│   │   ├── grid_system.h/cpp    # DPI-aware grid coordinates
│   │   ├── sort_engine.h/cpp    # Multi-partition sort engine
│   │   ├── config_manager.h/cpp # JSON config management
│   │   ├── snapshot_manager.h/cpp # Legacy snapshot save/restore
│   │   ├── timeline_record.h    # Data structures: TimelineSnapshot, TimelineIcon
│   │   ├── timeline_db.h/cpp    # .dslayout file-based persistent storage
│   │   ├── timeline_engine.h/cpp # Polling, debounce, auto-record engine
│   │   ├── layout_diff.h/cpp    # Diff between two layout snapshots
│   │   ├── placement_advisor.h/cpp # Smart new-icon placement
│   │   ├── export_import.h/cpp  # Cross-device .dslayout export/import
│   │   └── cleanup_policy.h/cpp # Auto-cleanup old records
│   └── ui/
│       ├── resource.h           # Control ID definitions
│       ├── timeline_ui.h/cpp    # Timeline panel window (list view + actions)
│       └── settings_ui.h/cpp    # Settings window (monitoring/recording/notifications)
├── ShellExt/
│   └── shellext.cpp/h           # IExplorerCommand right-click menu
├── DesktopIconSorter.vcxproj    # VS project file
└── app.manifest                 # DPI awareness
```

## Key Design Decisions

1. **No system hooks** - Pure observe + record + restore approach
2. **File-based storage** - No SQLite dependency, uses .dslayout text format
3. **Win32 only** - No external libraries, pure Windows SDK
4. **std::wstring** - All strings use wide characters (Unicode)
5. **COM via com_ptr.h** - RAII wrapper for COM interfaces

## Build Commands

```bash
# From Developer Command Prompt for VS 2026:
msbuild DesktopIconSorter.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Runtime Commands

```
/tray              Start system tray mode (auto-monitoring)
/timeline          Open timeline panel (GUI)
/settings          Open settings panel (GUI)
/checkpoint <name>  Save named checkpoint
/export <id> <path> Export snapshot to .dslayout
/import <path>     Import .dslayout file
/diff <id1> <id2>  Compare two snapshots
/list              List all timeline snapshots
/stats             Show storage statistics
/cleanup           Run auto-cleanup
/sort[:mode]       Sort icons (legacy, mode: 0=name 1=type 2=size 3=date)
/snap <cmd>        Legacy snapshot operations

# No arguments = start tray mode
```

## Coding Conventions

- `#pragma once` for headers
- `wchar_t*` / `std::wstring` for all strings
- COM interfaces via `ComPtr<T>` from `com_ptr.h`
- File I/O via `_wfopen_s` with UTF-8 encoding
- Global state in .cpp files (static variables)
- Prefix: `Timeline_`, `PlacementAdvisor_`, `Cleanup_` for module functions
