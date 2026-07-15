# Taskbar Thumbnail Reorder

A native x64 Windows 11 tray utility for reordering thumbnails inside a grouped taskbar button by
dragging with the left mouse button. It uses exact, signed module identities and fails closed instead of
guessing private taskbar addresses.

The distributable is one `TaskbarThumbnailReorder.exe`; its x64 payload DLL is embedded as resource 101.
Unknown taskbar builds remain unsupported and do not reach the Explorer loading path.

## Build and test

Requirements: Visual Studio Community 2022 17.14 with Desktop C++, Windows SDK 10.0.26100.0,
C++/WinRT, DIA SDK, AddressSanitizer, WinDbg, and CMake 3.31 or newer.

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug
cmake --build --preset release
ctest --preset release
```

Warnings are errors for first-party targets. See [testing](docs/testing.md) for `/analyze` and ASan.

## Safe offline commands

```powershell
TaskbarThumbnailReorder.exe --version
TaskbarThumbnailReorder.exe --diagnose-offline
TaskbarThumbnailReorder.exe --diagnose-offline --json
TaskbarThumbnailReorder.exe --diagnose-offline --manifest compat.bin --signature compat.sig
moduleid.exe --inspect path\to\image.dll
moduleid.exe path\to\exact-compatible-image.dll
```

`--diagnose-offline` exits before the single-instance mutex, settings, tray, Explorer discovery,
autostart, payload extraction, or Windows hook code. `moduleid --inspect` validates a generic x64 PE;
plain `moduleid` additionally requires one unambiguous RSDS identity.

## Personal signing key

Generate an ECDSA P-256 key outside the repository and restrict its directory to your account:

```powershell
manifestsign.exe generate C:\Users\you\.ttr\keys\manifest.private.blob `
  C:\Users\you\.ttr\keys\manifest.public.blob
icacls C:\Users\you\.ttr\keys /inheritance:r /grant:r "$env:USERNAME`:(OI)(CI)F"
cmake -S . -B out\build\release -A x64 `
  -DTTR_PUBLIC_KEY_FILE=C:/Users/you/.ttr/keys/manifest.public.blob
```

Back up the private blob in encrypted, user-owned storage. Never print, commit, package, or copy it into
the source tree. Replacing the key requires rebuilding the EXE and resigning future manifests. Tests use
ephemeral keys and delete them. The source placeholder is labeled development-only and fails closed.

## Compatibility and live testing

Compatibility generation is developer-side, exact-PDB, and read-only. Generated files remain under
`out/qualification/unqualified` until the separate [manual qualification](docs/MANUAL-QUALIFICATION.md)
passes. A symbol match alone is not behavioral compatibility.

Licensed GPL-3.0-only. Behavior is based on Michael Maltsev's GPL Windhawk
`taskbar-thumbnail-reorder` mod; see [third-party notices](THIRD_PARTY_NOTICES.md).
