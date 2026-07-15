# Taskbar Thumbnail Reorder

A native x64 Windows 11 tray utility for reordering thumbnails inside a grouped taskbar button by dragging with the left mouse button. It uses an exact-build, fail-closed compatibility manifest and a thread-specific `WH_CALLWNDPROC` hook; it never guesses private taskbar addresses.

The classic and modern XAML taskbar backends, host lifecycle, payload extraction, PE identity and DIA tooling, binary manifest compiler, and protocol tests are implemented. With no exact compatibility record, the application stays in the tray and makes no change to Explorer.

## Build

Requirements: Visual Studio 2022 with Desktop C++, Windows 11 SDK, and CMake 3.28+.

```powershell
cmake --preset vs2022-x64
cmake --build --preset release
ctest --preset debug
```

The `TaskbarThumbnailReorder` target embeds `TTRHook64.dll` as resource 101, producing one distributable EXE.

## First-machine workflow

```powershell
moduleid.exe C:\Windows\explorer.exe C:\Windows\System32\taskbar.dll
manifestc.exe records.json compat.bin
```

Only records backed by public PDB resolution and real taskbar testing should be installed under `%LOCALAPPDATA%\TaskbarThumbnailReorder\compat\compat.bin`.

Licensed GPL-3.0-only. The behavior is based on Michael Maltsev's GPL Windhawk `taskbar-thumbnail-reorder` mod. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
