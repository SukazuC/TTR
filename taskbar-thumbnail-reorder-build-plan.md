# Taskbar Thumbnail Reorder — Implementation Plan

**Document status:** implementation-ready architecture and execution plan  
**Target user environment:** Windows 11 Home x64, build family 26200, Intel x64 hardware  
**Primary goal:** reorder the thumbnail previews inside a grouped Windows 11 taskbar button by dragging them with the left mouse button, without installing Windhawk  
**Research baseline:** 2026-07-15

---

## 1. Executive decision

Build a small native Windows application with two logical components distributed as one executable:

1. **A native Win32 tray host** that owns the user interface, autostart, compatibility checks, Explorer restart recovery, and lifecycle management.
2. **A native x64 hook DLL**, embedded as a resource in the executable and extracted automatically, that runs inside `explorer.exe` and implements thumbnail reordering.

The host will load the hook DLL into only the Explorer taskbar thread by using a **thread-specific `SetWindowsHookEx` hook**. The DLL will then use **MinHook** to intercept the small set of internal taskbar functions required by the existing behavior.

The application will not resolve Windows debug symbols on the user's computer. Instead, a developer-side compatibility tool will generate a compact, signed manifest containing exact symbol RVAs for known versions of `taskbar.dll`, `Taskbar.View.dll`, and `ExplorerExtensions.dll`. At runtime, the application will apply hooks only when the loaded Microsoft modules match an exact manifest record. Unknown builds will be rejected without touching Explorer.

The end-user package will be a single file:

```text
TaskbarThumbnailReorder.exe
```

The user experience on each computer will be:

1. Copy the EXE anywhere.
2. Double-click it.
3. Use the tray icon to enable or disable reordering and optionally enable “Start with Windows.”

No administrator rights, installer, PowerShell, Windhawk, service, driver, or .NET runtime will be required.

---

## 2. Important technical reality

Windows does not provide a documented public API to reorder taskbar thumbnail previews. Public taskbar APIs can add or manipulate application buttons and notification-area icons, but they do not expose the internal ordering of thumbnail items.

The existing Windhawk implementation works by running code inside `explorer.exe`, finding internal taskbar objects and functions, and modifying the task group's internal item array. Its current source handles both the older Win32 thumbnail window and the newer Windows 11 XAML/WinUI thumbnail implementation. The implementation depends on private symbols in Windows modules and has required changes when cumulative Windows updates altered taskbar internals.

Therefore, all credible implementation choices have one of these costs:

- run code inside Explorer and depend on undocumented internals;
- replace the taskbar with another implementation;
- or provide a visually simulated alternative that does not truly reorder Windows' own thumbnails.

For this project, the correct choice is a narrowly scoped Explorer hook with strict version validation and safe failure behavior.

---

## 3. Product scope

### 3.1 Included in version 1.0

- Reorder grouped taskbar thumbnail previews with left-button drag.
- Preserve normal single-click activation when the pointer was not dragged.
- Support the native Windows 11 taskbar on x64 Windows 11.
- Support both thumbnail implementations currently encountered on Windows 11:
  - classic `TaskListThumbnailWnd` previews;
  - modern XAML/WinUI `TaskItemThumbnailList` previews.
- Work on primary and secondary monitors.
- Small notification-area icon with clear enabled, disabled, and incompatible states.
- Enable or disable the Explorer integration without closing the tray host.
- “Start with Windows” toggle.
- One portable EXE for transfer to another computer.
- Automatic recovery after Explorer restarts or crashes.
- Exact-build compatibility validation.
- Local diagnostics export suitable for bug reports.
- No elevated privileges.
- No analytics or telemetry.

### 3.2 Explicitly excluded from version 1.0

- Windows 10 support.
- ARM64 support.
- ExplorerPatcher, StartAllBack, Start11, or other replacement taskbars.
- Reordering top-level taskbar buttons.
- Permanently restoring a saved thumbnail order after windows are closed and recreated.
- Dragging a thumbnail between different application groups.
- Executable self-update.
- General-purpose process injection or mod loading.
- A settings window or framework-heavy UI.
- A kernel driver.

These exclusions are deliberate. They keep the application small, reduce Explorer risk, and avoid recreating a general customization framework.

---

## 4. Decision records

The following decisions are ordered by dependency. Each record compares viable alternatives and fixes the implementation choice so Codex agents do not need to reinterpret the architecture.

## 4.1 Architecture decisions

### A1. Out-of-process automation versus in-process Explorer integration

**Option A — UI Automation and synthetic mouse input**

- Advantages:
  - no DLL inside Explorer;
  - lower antivirus sensitivity;
  - uses documented accessibility APIs.
- Problems:
  - UI Automation can identify and invoke thumbnail controls but exposes no operation that changes the task group's underlying order;
  - visual child ordering is not the authoritative task-item ordering;
  - synthetic dragging cannot create a reorder behavior that Windows itself does not implement.

**Option B — in-process Explorer hook**

- Advantages:
  - can access the same task-group and thumbnail objects used by the working Windhawk mod;
  - can preserve the native taskbar visual behavior;
  - small runtime code path.
- Problems:
  - depends on undocumented implementation details;
  - must be updated for some Windows builds;
  - faults could destabilize Explorer if validation is weak.

**Decision:** choose Option B. A true reorder feature requires in-process access. All subsequent decisions optimize this approach for safety and narrow scope.

**Future problem review:** Windows can change these internals without notice. Mitigation is exact module matching, transactional hook installation, and immediate fail-closed behavior on unknown module identities.

---

### A2. How the DLL enters Explorer

**Option A — `CreateRemoteThread` plus `LoadLibraryW`**

- Advantages:
  - straightforward and common;
  - independent of target message traffic.
- Problems:
  - resembles generic injection behavior commonly flagged by security tools;
  - needs process handles with memory and thread rights;
  - creates more security-sensitive code than necessary.

**Option B — thread-specific `SetWindowsHookEx`**

- Advantages:
  - documented Windows hook mechanism;
  - can target the exact thread that owns `Shell_TrayWnd`;
  - Windows loads the DLL into the target process;
  - avoids remote allocation and remote-thread shellcode.
- Problems:
  - the host and Explorer must have matching architecture;
  - the hook DLL must exist as a physical file;
  - the hook must remain installed while the integration is active.

**Option C — registered Explorer shell extension**

- Advantages:
  - uses a traditional COM loading mechanism;
  - can be registered per user.
- Problems:
  - registration and activation are less deterministic;
  - shell extensions are not guaranteed to load in the taskbar execution path;
  - substantially harder to keep portable and cleanly removable;
  - still places code in Explorer.

**Decision:** choose Option B. Install one thread-specific `WH_CALLWNDPROC` hook on the thread returned by `GetWindowThreadProcessId(Shell_TrayWnd)`. The exported hook procedure must do minimal work, call `CallNextHookEx`, and use one-time initialization guarded by interlocked state.

**Future problem review:** Explorer may create the taskbar before all taskbar modules are loaded. The DLL must not hook `LoadLibraryExW`. Instead, while the Windows hook is active, its callback should perform a cheap `GetModuleHandleW` check until the relevant module appears, then install the remaining hooks once.

---

### A3. Runtime composition

**Option A — one standalone executable with no DLL**

- Advantages:
  - visually simplest distribution.
- Problems:
  - `SetWindowsHookEx` requires the callback code to reside in a DLL when executing in another process;
  - manual mapping would recreate a more suspicious and complex injector.

**Option B — EXE plus adjacent DLL in a ZIP**

- Advantages:
  - simplest development and debugging model.
- Problems:
  - users can copy only one file and break the package;
  - less convenient on the second computer.

**Option C — one distributable EXE with an embedded DLL resource**

- Advantages:
  - one file to copy;
  - still uses a normal DLL loading path;
  - resource hash can be checked after extraction.
- Problems:
  - first run must extract the DLL to disk;
  - versioned cleanup logic is required.

**Decision:** choose Option C. Embed `TTRHook64.dll` in `TaskbarThumbnailReorder.exe`. Extract it atomically to:

```text
%LOCALAPPDATA%\TaskbarThumbnailReorder\payload\<app-version>\TTRHook64.dll
```

Write to a temporary name, verify SHA-256 against the embedded expected hash, then rename into place. Retain only the current and immediately previous payload directories.

**Future problem review:** antivirus software may quarantine a newly extracted unsigned DLL. The tray host must detect load failure, display a concise error state, and offer “Export diagnostics.” Release builds should be Authenticode-signed when practical, but signing must not be a functional requirement for personal builds.

---

### A4. Language and UI framework

**Option A — C# with WinForms or WPF**

- Advantages:
  - quick tray UI development;
  - convenient configuration and update code.
- Problems:
  - .NET runtime dependency or large self-contained publish;
  - separate native DLL remains necessary;
  - higher idle memory and package size.

**Option B — Rust host and native C++ DLL**

- Advantages:
  - strong memory safety in the host;
  - good native binaries.
- Problems:
  - mixed toolchains and FFI increase project complexity;
  - the payload still needs extensive Windows C++/WinRT interaction;
  - harder for one Codex workflow to debug end to end.

**Option C — C++20 Win32 for both host and DLL**

- Advantages:
  - smallest runtime footprint;
  - direct access to Win32, C++/WinRT, PE parsing, CNG, and hooking APIs;
  - one build system and debugger.
- Problems:
  - requires disciplined RAII and explicit bounds checking.

**Decision:** choose Option C. Use C++20, Unicode APIs only, `/permissive-`, `/W4`, `/WX` in CI, and the static C runtime (`/MT`) for release builds. Do not use MFC, ATL UI classes, Windows App SDK, WinForms, WPF, Qt, or Electron.

**Future problem review:** static CRT can increase binary size slightly, but removes deployment dependencies and remains much smaller than framework-based alternatives.

---

### A5. Function interception library

**Option A — hand-written x64 trampolines**

- Advantages:
  - potentially the smallest code.
- Problems:
  - instruction relocation is subtle;
  - high crash risk;
  - unnecessary reinvention.

**Option B — Microsoft Detours**

- Advantages:
  - mature and supported on x64;
  - MIT licensed.
- Problems:
  - larger and broader than required;
  - more code and concepts for this narrow payload.

**Option C — MinHook**

- Advantages:
  - focused x86/x64 hooking library;
  - small footprint;
  - simple create/enable/remove lifecycle;
  - active maintenance and permissive license.
- Problems:
  - hook installation can still fail on functions with unsupported prologues.

**Decision:** choose Option C. Vendor a pinned MinHook release as source under `third_party/minhook`, retain its license, and use queued enable/disable operations so the hook set is committed transactionally.

**Future problem review:** a Windows update can change a target prologue even when a symbol name remains. Treat any `MH_CreateHook` failure as incompatibility and remove all hooks; never leave a partially installed feature.

---

### A6. Symbol resolution strategy

**Option A — download and enumerate PDBs on every user's computer**

- Advantages:
  - can adapt automatically when Microsoft symbols are available;
  - closely resembles Windhawk's general solution.
- Problems:
  - substantially more code and dependencies;
  - PDB files can be large;
  - symbol-server availability can lag behind Insider or cumulative builds;
  - first start may require network access and a large cache;
  - contradicts the “extra light” requirement.

**Option B — hard-code offsets by OS build number**

- Advantages:
  - tiny and simple.
- Problems:
  - OS build number is not a sufficiently precise binary identity;
  - cumulative revisions and feature rollouts can ship different taskbar modules;
  - a wrong offset can crash Explorer.

**Option C — exact module identity plus generated RVA manifest**

- Advantages:
  - tiny runtime;
  - no PDB parser or symbol cache on user machines;
  - exact matching can fail safely;
  - compatibility data can be updated independently of the EXE.
- Problems:
  - a maintainer must generate a record for new Windows binaries;
  - new Windows updates can temporarily be unsupported.

**Decision:** choose Option C. Key records by the PE CodeView identity and image metadata, not by the friendly Windows version alone:

- module base name;
- `TimeDateStamp`;
- `SizeOfImage`;
- CodeView `RSDS` PDB GUID;
- PDB age.

Use OS build and file version only as human-readable metadata.

**Future problem review:** a new build may have no public PDB yet. The application should show “Compatibility update required” and leave Explorer untouched. This is preferable to guessing offsets.

---

### A7. Compatibility manifest delivery

**Option A — only embed records in each executable release**

- Advantages:
  - no network code;
  - simplest trust model.
- Problems:
  - every Windows cumulative update may require a new full application release.

**Option B — download an unsigned JSON offset file**

- Advantages:
  - easy to implement and inspect.
- Problems:
  - a compromised file or endpoint could provide arbitrary code addresses inside Explorer;
  - JSON parsing is unnecessary in the payload.

**Option C — embedded base manifest plus optional signed binary manifest updates**

- Advantages:
  - works offline for known builds;
  - compatibility updates are very small;
  - signature verification prevents untrusted offset data;
  - compact fixed-layout parsing.
- Problems:
  - requires a manifest compiler and signing workflow.

**Decision:** choose Option C. Version 1.0 may ship with executable update checking disabled by default, but compatibility-manifest checking is allowed because it only downloads signed data. The host must use WinHTTP, impose a 1 MiB response limit, verify an ECDSA P-256 signature with an embedded public key through Windows CNG, and atomically replace the local manifest only after full validation.

The host checks for a manifest update only:

- when the current build is unsupported;
- when the user selects “Check compatibility update”;
- or once per 24 hours while enabled.

The injected DLL must never perform network access.

**Future problem review:** repository or hosting compromise is mitigated by offline signing. The private signing key must not be stored in the source repository or CI secrets used by untrusted pull requests.

---

### A8. Host-to-payload communication

**Option A — named pipe**

- Advantages:
  - flexible request/response protocol.
- Problems:
  - excess code and a server thread inside Explorer;
  - unnecessary for a few commands.

**Option B — custom window messages and a hidden payload window**

- Advantages:
  - native message-driven design.
- Problems:
  - creates and manages another window inside Explorer;
  - synchronous message handling can deadlock if used carelessly.

**Option C — shared memory control block plus taskbar-thread wake-up message**

- Advantages:
  - very small;
  - no server thread;
  - commands can be acknowledged through interlocked counters;
  - status and a bounded log ring can be shared.
- Problems:
  - protocol layout must be fixed and versioned.

**Decision:** choose Option C. The host creates a current-user-only named mapping before installing the Windows hook:

```text
Local\TaskbarThumbnailReorder.Session.<explorer-pid>
```

The payload opens the mapping using its own process ID. Use a packed, versioned control structure and Win32 `Interlocked*` operations rather than placing C++ `std::atomic` objects in shared memory.

The host writes a command and increments `commandSequence`, then sends `WM_NULL` to `Shell_TrayWnd` with `SendMessageTimeoutW`. The hook callback notices the sequence change, executes the command on the taskbar thread, writes the result, and increments `ackSequence`.

Commands:

```text
Initialize
Enable
Disable
QueryStatus
PrepareUnload
SetDebugLogging
```

**Future problem review:** if Explorer is hung, all sends must use `SendMessageTimeoutW` with `SMTO_ABORTIFHUNG | SMTO_BLOCK` and a maximum 1-second timeout. A timeout changes the tray state to “Explorer unavailable”; it must not block the host UI.

---

### A9. Licensing strategy

**Option A — copy and adapt the current Windhawk mod under GPLv3**

- Advantages:
  - uses the proven implementation and its recent Windows 11 fixes;
  - fastest route to behavioral parity;
  - avoids pretending the implementation is independent.
- Problems:
  - the resulting application must comply with GPLv3 distribution obligations.

**Option B — clean-room reimplementation under a permissive license**

- Advantages:
  - licensing flexibility.
- Problems:
  - requires separate specification and implementation teams to be meaningfully clean-room;
  - high effort for a personal utility;
  - likely to rediscover the same private structures and risks.

**Decision:** choose Option A and license the full project as **GPL-3.0-only**. Preserve attribution to Michael Maltsev/m417z and the Windhawk mod, include source availability, and retain third-party notices for MinHook and other dependencies.

**Future problem review:** do not combine code with dependencies whose licenses are incompatible with GPLv3. Keep the runtime dependency list minimal and audited.

---

## 4.2 Feature decisions

### F1. Tray interaction model

**Option A — full settings window**

- clearer explanations, but adds UI code and visual complexity.

**Option B — tray-only context menu**

- smallest interface and appropriate for three primary controls.

**Decision:** tray-only. The context menu order is fixed:

```text
Status: Active                      [disabled label]
Enable thumbnail reordering         [checked when active]
Start with Windows                  [checked when configured]
---------------------------------
Check compatibility update
Export diagnostics...
About
---------------------------------
Exit
```

When unsupported, the first item reads `Status: Compatibility update required`, the enable item is disabled, and the update command remains available.

Left-click opens the context menu. Double-click toggles enable/disable. Keyboard activation via `WM_CONTEXTMENU` must work with `NOTIFYICON_VERSION_4`.

---

### F2. Tray icon visual design

**Option A — generic gear or taskbar icon**

- immediately recognizable but visually ordinary.

**Option B — small friendly stack of thumbnail cards with a reorder arrow**

- specific to the feature and satisfies the “cute” requirement without creating a large UI.

**Decision:** Option B. Create one `.ico` resource containing 16, 20, 24, 32, 48, and 256 pixel images.

Visual rules:

- two slightly offset rounded preview cards;
- a small horizontal swap arrow;
- enabled: normal accent-colored cards and a small green status dot;
- disabled: neutral gray cards and no dot;
- incompatible/error: normal cards with an amber dot;
- strong one-pixel silhouette at 16 px for both light and dark taskbars;
- no text inside the icon.

Use separate icon resource IDs for enabled, disabled, and warning states rather than dynamically drawing them.

---

### F3. Autostart mechanism

**Option A — Startup folder shortcut**

- visible to the user, but requires COM shortcut creation and can point to a moved portable file.

**Option B — per-user `HKCU\...\Run` value**

- documented, simple, no administrator rights, and easy to toggle.

**Option C — scheduled task**

- flexible timing, but overpowered and more likely to trigger security concern.

**Decision:** Option B. Use:

```text
HKCU\Software\Microsoft\Windows\CurrentVersion\Run
Value name: TaskbarThumbnailReorder
Value data: "<installed-copy>\TaskbarThumbnailReorder.exe" --background
```

When the user enables autostart, copy the running EXE to:

```text
%LOCALAPPDATA%\TaskbarThumbnailReorder\TaskbarThumbnailReorder.exe
```

Use an atomic `.new` replacement. Then launch the installed copy with `--replace-current`, let it acquire the single-instance handoff, and exit the portable instance. This guarantees that autostart does not break if the originally downloaded EXE is moved or deleted.

When autostart is disabled, remove only this application's named Run value. Do not delete the installed copy automatically while it is executing.

---

### F4. Default state

**Option A — first run immediately enables the integration**

- simplest and matches the user's intent.

**Option B — first run remains disabled pending confirmation**

- more conservative but adds a confusing first step.

**Decision:** Option A, provided the exact module identity is supported. First run adds the tray icon, validates compatibility, injects, and enables. It does not enable autostart automatically.

On unsupported builds, the application remains running in warning state and performs no injection.

---

### F5. Update behavior

**Option A — automatic executable updates**

- convenient, but adds a large trust and replacement system.

**Option B — no updates of any kind**

- smallest, but forces full manual releases for every Windows change.

**Option C — compatibility-data updates only**

- resolves the most common maintenance need without replacing code.

**Decision:** Option C. No executable update in version 1.0. Download only signed compatibility manifests. The About dialog can display the project release page for manual application updates.

---

### F6. Diagnostics

**Option A — always write verbose logs to disk**

- helpful for debugging, but creates unnecessary I/O and data accumulation.

**Option B — no logs**

- smallest, but makes private-symbol failures difficult to diagnose.

**Option C — bounded in-memory logging with explicit export**

- low overhead and sufficient for support.

**Decision:** Option C. The payload writes compact event records to a fixed 64 KiB ring in shared memory. The host keeps a bounded 256 KiB recent log in memory. “Export diagnostics” writes a UTF-8 text report selected through a standard Save dialog.

The report includes:

- application version and build type;
- Windows version from `RtlGetVersion` or a version-helper wrapper;
- Explorer PID, path, file version, and session ID;
- identities of relevant taskbar modules;
- selected compatibility record ID;
- hook installation results by stable symbol ID;
- payload and manifest SHA-256 values;
- current state and recent error codes;
- no usernames, window titles, application names, or file paths outside the application and Windows module paths.

---

### F7. User cleanup

**Option A — installer-style uninstaller**

- familiar but conflicts with a portable single-file utility.

**Option B — manual deletion only**

- simple but can leave autostart and payload files.

**Decision:** include a small `Remove from this PC...` item inside the About dialog, not the main menu. It must:

1. disable and unload the payload;
2. remove the Run value;
3. delete extracted payloads and manifests;
4. mark the installed EXE for deletion by launching the same EXE in `--cleanup-after <pid> <directory>` mode from a temporary copy;
5. exit.

The cleanup helper waits for the original PID, deletes the local application directory, then deletes its own temporary file on the next launch or leaves only a harmless file in `%TEMP%` if Windows holds it open.

---

## 4.3 Optimization decisions

### O1. Idle scheduling

**Option A — poll Explorer and module state every second**

- simple but causes permanent wakeups.

**Option B — event-driven operation**

- slightly more stateful but near-zero idle cost.

**Decision:** event-driven. The host receives the registered `TaskbarCreated` broadcast, recreates its notification icon, and reattaches to the new Explorer process. Use a temporary 250 ms retry timer only while Explorer is missing or attachment is in progress; stop the timer after success.

The payload performs module-presence checks only when its thread-specific hook callback is already invoked for taskbar traffic and only while an expected module remains unhooked.

---

### O2. Symbol and compatibility storage

**Option A — retain downloaded PDBs**

- robust for a framework, but potentially hundreds of megabytes.

**Option B — retain only compact RVA records**

- kilobytes per Windows binary set.

**Decision:** Option B. Runtime must never download or store PDBs. Target total compatibility manifest size under 256 KiB for the first several dozen supported module sets.

---

### O3. Pointer-move processing

**Option A — reconstruct the entire visual tree on every pointer event**

- easy to express but unnecessary work.

**Option B — enumerate realized `ItemsRepeater` elements and stop once source and target are found**

- mirrors the current working approach and excludes virtualized or ghost elements.

**Decision:** Option B. Cache only weak references; never keep strong references that extend taskbar object lifetimes. Iterate at most the repeater item count and break immediately after both pressed and hovered indices are known.

Performance target: under 0.20 ms at the 95th percentile per hooked pointer-move call with 20 visible thumbnails on the target hardware.

---

### O4. Drag movement strategy for animated XAML thumbnails

**Option A — move directly from source index to any target index**

- fewer operations, but current Windows animation behavior can clear the pressed state and break the drag.

**Option B — accept only adjacent transitions as the pointer crosses thumbnails**

- matches the current workaround and is less disruptive to animation state.

**Option C — schedule multiple adjacent moves to catch up to a distant pointer**

- handles extremely fast jumps, but adds timers and reentrancy risk.

**Decision:** implement Option B for version 1.0. Record a diagnostic counter when a non-adjacent jump is observed. Add Option C only if real testing shows frequent missed reorders. Do not guess at intermediate object state in the initial release.

---

### O5. Binary and memory budgets

The implementation must be measured against these release budgets:

| Metric | Target | Hard ceiling |
|---|---:|---:|
| Single EXE size | <= 1.5 MiB | 3 MiB |
| Extracted DLL size | <= 500 KiB | 1 MiB |
| Host private bytes, idle | <= 5 MiB | 12 MiB |
| Additional Explorer private bytes | <= 2 MiB | 5 MiB |
| Host idle CPU after attachment | effectively 0% | < 0.05% average |
| Healthy-state periodic wakeups | 0 | 1 per minute |
| Initial activation after Explorer is ready | < 500 ms | 2 s |
| Disable/unload acknowledgement | < 250 ms | 1 s |

Use link-time code generation, function/data section elimination, and no exceptions across DLL ABI boundaries. Do not apply executable compressors or packers; they often increase security warnings and complicate crash analysis.

---

## 5. Detailed system architecture

## 5.1 Process model

```text
+------------------------------------------------------------+
| TaskbarThumbnailReorder.exe                                |
|                                                            |
|  - hidden Win32 message window                             |
|  - notification-area icon                                  |
|  - settings and autostart                                  |
|  - compatibility manifest validation                       |
|  - Explorer/taskbar discovery                              |
|  - SetWindowsHookEx lifecycle                              |
|  - shared session mapping                                  |
|  - diagnostics                                             |
+------------------------------+-----------------------------+
                               |
                               | thread-specific WH_CALLWNDPROC
                               | plus shared control mapping
                               v
+------------------------------------------------------------+
| explorer.exe                                               |
|                                                            |
|  TTRHook64.dll                                             |
|  - exported bootstrap hook procedure                       |
|  - exact module identity verification                      |
|  - MinHook target setup                                    |
|  - classic thumbnail drag implementation                   |
|  - XAML thumbnail drag implementation                      |
|  - bounded shared-memory log                               |
|                                                            |
|  taskbar.dll                                               |
|  Taskbar.View.dll or ExplorerExtensions.dll                |
+------------------------------------------------------------+
```

No other processes are modified.

## 5.2 Host lifecycle state machine

Use this exact host state model:

```text
Starting
  -> WaitingForExplorer
  -> ValidatingCompatibility
  -> Attaching
  -> Active
  -> Disabled
  -> Unsupported
  -> ExplorerUnavailable
  -> Faulted
  -> Exiting
```

Rules:

- `Active` means the payload acknowledged that all required hooks for one thumbnail implementation are enabled.
- `Disabled` means the tray host is running but no Windows hook is installed and no payload code is intentionally retained in Explorer.
- `Unsupported` means the relevant module identities have no complete compatible record.
- `Faulted` means attachment began but validation or hook creation failed. The host must attempt one clean detach and must not retry continuously. A user toggle or Explorer restart permits one new attempt.

## 5.3 Single-instance behavior

Create a named mutex:

```text
Local\TaskbarThumbnailReorder.Host.v1
```

Also register a private host window message. A second launch should locate the existing hidden window, send `ShowMenu` or `Activate`, and exit. `--replace-current` requests a controlled handoff used when moving to the installed autostart path.

Do not use a global mutex; each interactive Windows session may run its own host.

## 5.4 Explorer validation

Before injection:

1. Find `Shell_TrayWnd`.
2. Get its thread ID and process ID.
3. Confirm the PID belongs to the current terminal session with `ProcessIdToSessionId`.
4. Open the process with the minimum query rights needed for module enumeration.
5. Confirm the executable path resolves to `%WINDIR%\explorer.exe`, case-insensitively after canonicalization.
6. Confirm both host and target are 64-bit with `IsWow64Process2`.
7. Build module identities from the actual loaded files or known system module paths.
8. Select a complete compatibility record.
9. Only then extract/load the payload and install `SetWindowsHookEx`.

Do not inject into an arbitrary process merely named `explorer.exe`.

## 5.5 Payload initialization constraints

`DllMain` must only:

- store `HINSTANCE` on `DLL_PROCESS_ATTACH`;
- call `DisableThreadLibraryCalls`;
- initialize no COM, XAML, MinHook, files, registry, or synchronization that can block.

The exported hook callback must:

1. immediately call or preserve the result of `CallNextHookEx` as required;
2. check the shared mapping and command sequence;
3. perform initialization only when not already initialized;
4. guard the initialization state with `InterlockedCompareExchange`;
5. avoid throwing C++ exceptions through Windows callbacks.

All public callback boundaries must be `noexcept` and protected by structured exception handling. On an exception, set the shared state to `Faulted`, disable custom behavior with a global gate, and continue calling original functions.

## 5.6 Shared control protocol

Create `include/ttr_protocol.h` and use it in both host and payload. Keep the header free of STL types.

Required fields:

```cpp
struct TtrSessionControlV1 {
    char magic[8];                 // "TTRSES1"
    uint32_t byteSize;
    uint32_t protocolVersion;
    uint32_t hostPid;
    uint32_t explorerPid;
    uint64_t sessionNonce;

    volatile LONG commandSequence;
    volatile LONG acknowledgedSequence;
    volatile LONG command;
    volatile LONG commandArgument;

    volatile LONG payloadState;
    volatile LONG payloadError;
    volatile LONG activeThumbnailBackend;
    volatile LONG installedHookCount;

    uint32_t manifestOffset;
    uint32_t manifestSize;
    uint32_t logOffset;
    uint32_t logSize;
    volatile LONG logWriteOffset;
    volatile LONG logDroppedCount;
};
```

Append the selected compact manifest data and a fixed log ring in the same mapping. Validate every offset and size before use.

The mapping security descriptor must allow the current user and SYSTEM, and deny access to unrelated users. Use a random 64-bit nonce in the header even though the name is PID-derived; the payload must verify the expected host PID and nonce copied into an exported shared-data field before acting on commands.

## 5.7 Compatibility record format

The source representation is human-reviewable JSON under `compat/records`. The runtime representation is a packed binary generated by `manifestc`.

Each record contains:

```text
record ID
human-readable Windows build and file versions
tested/not-tested status
backend flags: classic, XAML, animated XAML
one or more module identities
stable symbol ID -> module index + RVA + kind + required/optional
minimum application protocol version
notes and source PDB identity
```

Stable symbol IDs must be an enum and must never be reused. Initial IDs include:

```text
TaskListWnd_Vtable_ITaskListUI
TaskListThumbnailWnd_GetHoverIndex
TaskListThumbnailWnd_GetTaskItem
TaskListThumbnailWnd_GetTaskGroup
TaskListThumbnailWnd_TaskReordered
TaskListThumbnailWnd_WndProc
TaskGroup_GetNumItems
TaskListWnd_GetTBGroupFromGroup
TaskBtnGroup_GetGroupType
TaskBtnGroup_IndexOfTaskItem
TaskListWnd_TaskInclusionChanged
TaskItemFilter_IsTaskAllowed
TaskItemThumbnail_ConstructorV1
TaskItemThumbnail_ConstructorV2
TaskGroup_Thumbnails
TaskItemThumbnailVector_Size
TaskItemThumbnailVector_GetAt
TaskListWnd_HandleExtendedUIClick
HoverFlyoutModel_TargetItemKey
TaskItemThumbnailList_OnPointerMoved
FlyoutFrame_UpdateFlyoutPosition
```

Before using a symbol RVA, verify:

- the module identity exactly matches the manifest;
- the RVA is less than `SizeOfImage`;
- function targets lie in an executable section;
- vtable/data targets lie in an appropriate readable section;
- no arithmetic overflows occur;
- required symbol groups are complete.

A valid backend is either:

- all required classic symbols; or
- all required XAML symbols.

Optional symbols may be absent only when the manifest explicitly marks them optional for that record.

---

## 6. Thumbnail reordering implementation

## 6.1 Source baseline

Port the behavior from the current GPLv3 Windhawk `taskbar-thumbnail-reorder` mod, not from an older fork. Preserve the architectural separation between:

- common task-group item movement;
- classic thumbnail event handling;
- modern XAML thumbnail event handling;
- taskbar refresh behavior.

Remove Windhawk-specific APIs and ExplorerPatcher/Windows 10 branches. Replace them with the project's own manifest lookup, MinHook wrapper, logging, settings, and lifecycle gates.

Do not perform a blind textual extraction. First convert the source into testable internal modules with explicit types and invariants.

## 6.2 Common item movement

Implement these operations in `payload/task_model.cpp`:

```text
GetTaskItemsArray(taskGroup)
MoveTaskInGroup(taskGroup, taskItemFrom, taskItemTo)
MoveTaskInTaskList(taskbarWindow, taskListObject, taskGroup, from, to)
RefreshTaskbarAfterMove(...)
```

Requirements:

- validate task group and item pointers before dereference;
- validate `DPA.cpItems`, `DPA.cpCapacity`, and `DPA.pArray` against conservative bounds;
- reject negative counts or counts above 1024;
- locate both items before modifying the array;
- use `memmove` only after checked size calculations;
- update all taskbar instances, including secondary taskbars;
- bracket temporary redraw suppression correctly;
- always restore `TaskItemFilter` state through an RAII guard even on failure;
- rate-limit the task-inclusion refresh workaround as in the current mod.

The `CTaskGroup::GetNumItems` offset-discovery technique is inherently fragile. Isolate it in one function, validate the derived offset against a small maximum, and verify that the resulting DPA count agrees with the public/internal item count before writing.

## 6.3 Classic thumbnail backend

Hook the classic thumbnail window procedure and implement this state flow:

```text
WM_LBUTTONDOWN
  call original
  find hovered index
  set drag source and capture

WM_MOUSEMOVE
  call original
  when captured and hovered index changed:
    move source item to hovered item
    update source index
    mark drag completed

WM_LBUTTONUP
  release capture
  if a reorder occurred, suppress activation side effect
  otherwise call original normally

WM_TIMER for Aero Peek timer
  suppress only while this window owns capture
```

All state is process-local and taskbar-thread-affine. Do not use a global low-level mouse hook.

## 6.4 Modern XAML backend

The modern backend must:

1. Hook the task-item-thumbnail constructors to map weak XAML thumbnail objects to `ITaskGroup*` and `ITaskItem*`.
2. Capture the current task group's thumbnail observable vector while the hover flyout model resolves its target item.
3. Hook `OnPointerMoved` for both:
   - `Taskbar.TaskItemThumbnailList`;
   - `Taskbar.TaskItemThumbnailScrollableList`.
4. Locate the `ItemsRepeater` by the known child-name paths.
5. Enumerate `ItemsSourceView.Count()` and `TryGetElement(index)`.
6. Determine the pressed index from the `CommonStates` visual state.
7. Determine the hovered index by pointer coordinates relative to each realized element.
8. Reorder only when the indices differ and the current manifest's movement rules allow the transition.
9. Suppress the post-drag activation through the `HandleExtendedUIClick` hook only when a reorder was actually performed.
10. Apply the flyout-position workaround only on records where thumbnail animations are not enabled.

Use weak references for mappings and prune stale entries whenever a new thumbnails vector is observed. Set an upper bound of 256 mapping entries; if exceeded, prune invalid entries and then drop the oldest entries rather than growing indefinitely.

## 6.5 Drag correctness rules

- A click without crossing into another thumbnail must behave exactly as stock Windows.
- A successful reorder must not activate a different window on button release.
- Capture must be released on button-up, cancellation, disable, and unload.
- Reordering must remain inside one task group.
- No move is attempted when source or target mapping is incomplete.
- Adjacent-only behavior is enforced for manifest records affected by the animated-thumbnail pressed-state issue.
- On any validation failure, call original behavior and record a bounded diagnostic event.

---

## 7. Safety and resilience design

## 7.1 Fail-closed compatibility

The application must never infer that two modules are compatible merely because they share a Windows build number. If any required module identity or symbol record is missing, the payload must not install hooks.

The tray status should be explanatory but restrained:

```text
Compatibility update required for this Windows taskbar version.
No changes were made to Explorer.
```

## 7.2 Transactional hook installation

Exact sequence:

1. Validate the full compatibility record.
2. Initialize MinHook.
3. Create every required hook in disabled state.
4. If any creation fails, remove all created hooks and uninitialize MinHook.
5. Queue all enables.
6. Apply queued operations once.
7. Mark payload `Active` only after success.

Disable sequence:

1. Set the global behavior gate false.
2. Release any owned mouse capture.
3. Wait for the bounded active-callback counter to reach zero, maximum 200 ms.
4. Queue disable for all hooks.
5. Apply queued operations.
6. Remove hooks and uninitialize MinHook.
7. Clear weak references and state.
8. Acknowledge `Disabled`.

If callback drain times out, leave the DLL loaded, mark `Faulted`, and do not force-unload code that may still be executing.

## 7.3 Explorer restart recovery

The host registers the `TaskbarCreated` message. On receipt:

- re-add the notification icon;
- invalidate the old Explorer PID, thread ID, Windows hook handle, mapping, and payload state;
- wait for a valid new `Shell_TrayWnd`;
- if the user setting is enabled, validate and attach once;
- otherwise remain disabled.

Also detect stale Explorer handles through process-exit wait registration. Do not use a permanent polling loop.

## 7.4 Crash containment

- All hook functions must retain callable original pointers before enabling.
- Each hook begins with a fast global `enabled` check and immediately calls the original when false.
- Use structured exception handling around pointer-heavy custom logic.
- On first exception, atomically set `faulted`, stop custom modifications, and call original functions thereafter.
- Do not attempt complex self-unhooking from inside an exception handler.
- Host receives `Faulted` through shared state and offers diagnostics.

## 7.5 Security boundaries

- Run at normal user integrity.
- Inject only into the verified Explorer process owning `Shell_TrayWnd` in the same session.
- Use absolute paths for DLL loading.
- Call `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS)` early in the host.
- Verify the extracted payload hash before each load.
- Verify remote compatibility manifests cryptographically.
- Cap all downloaded and parsed sizes.
- Never execute downloaded code or scripts.
- Never load a payload from the current working directory.
- Do not expose a general “inject into PID” command-line option.

---

## 8. Repository and code organization

Use this repository layout:

```text
TaskbarThumbnailReorder/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ LICENSE
├─ README.md
├─ SECURITY.md
├─ THIRD_PARTY_NOTICES.md
├─ docs/
│  ├─ architecture.md
│  ├─ compatibility-workflow.md
│  ├─ release-checklist.md
│  └─ testing.md
├─ include/
│  ├─ ttr_protocol.h
│  ├─ ttr_manifest.h
│  ├─ ttr_symbol_ids.h
│  └─ ttr_version.h.in
├─ host/
│  ├─ main.cpp
│  ├─ app.cpp
│  ├─ app.h
│  ├─ tray_icon.cpp
│  ├─ tray_icon.h
│  ├─ explorer_session.cpp
│  ├─ explorer_session.h
│  ├─ hook_loader.cpp
│  ├─ hook_loader.h
│  ├─ manifest_store.cpp
│  ├─ manifest_store.h
│  ├─ manifest_update.cpp
│  ├─ manifest_update.h
│  ├─ autostart.cpp
│  ├─ autostart.h
│  ├─ settings.cpp
│  ├─ settings.h
│  ├─ diagnostics.cpp
│  ├─ diagnostics.h
│  ├─ cleanup.cpp
│  └─ resources/
│     ├─ app.rc
│     ├─ icon-enabled.ico
│     ├─ icon-disabled.ico
│     └─ icon-warning.ico
├─ payload/
│  ├─ dllmain.cpp
│  ├─ bootstrap_hook.cpp
│  ├─ bootstrap_hook.h
│  ├─ payload_state.cpp
│  ├─ payload_state.h
│  ├─ manifest_runtime.cpp
│  ├─ manifest_runtime.h
│  ├─ hook_transaction.cpp
│  ├─ hook_transaction.h
│  ├─ task_model.cpp
│  ├─ task_model.h
│  ├─ classic_backend.cpp
│  ├─ classic_backend.h
│  ├─ xaml_backend.cpp
│  ├─ xaml_backend.h
│  ├─ winrt_visual_tree.cpp
│  ├─ winrt_visual_tree.h
│  ├─ payload_log.cpp
│  └─ payload_log.h
├─ compat/
│  ├─ symbol-spec.yaml
│  ├─ records/
│  ├─ manifest-public-key.bin
│  └─ generated/
├─ tools/
│  ├─ compatgen/
│  ├─ manifestc/
│  ├─ manifestsign/
│  └─ moduleid/
├─ tests/
│  ├─ unit/
│  ├─ protocol/
│  ├─ manifest/
│  ├─ pe-fixtures/
│  └─ manual/
├─ third_party/
│  └─ minhook/
└─ .github/
   └─ workflows/
      ├─ build.yml
      └─ release.yml
```

Do not put compatibility offsets directly in C++ source files. All build-specific data must originate in reviewed compatibility records.

---

## 9. Developer-side compatibility toolchain

## 9.1 `moduleid`

A small native command-line tool that reads a PE file and prints deterministic JSON containing:

- canonical file path;
- SHA-256;
- machine type;
- `TimeDateStamp`;
- `SizeOfImage`;
- file version;
- CodeView signature, PDB GUID, age, and PDB name;
- section table summary.

This tool must use only Win32 file mapping and PE structures. It is also callable by the host's diagnostics code through a shared library implementation.

## 9.2 `compatgen`

A developer-only x64 tool that resolves the symbol specification against Microsoft public PDBs.

Implementation choice:

- use the Visual Studio DIA SDK through `IDiaDataSource` and `IDiaSession`;
- use `loadDataForExe` or equivalent with a symbol path that points to a developer cache and Microsoft's symbol server;
- accept exact Windows module paths as input;
- search each stable symbol ID against an ordered list of exact symbol-name alternatives from `compat/symbol-spec.yaml`;
- emit RVAs, never absolute addresses;
- fail when a required symbol has zero or multiple unresolved candidates;
- include the PDB GUID/age and generator version in output.

Example developer invocation:

```text
compatgen.exe \
  --taskbar C:\Windows\System32\taskbar.dll \
  --taskbar-view "<actual Taskbar.View.dll path>" \
  --symbol-spec compat\symbol-spec.yaml \
  --out compat\records\win11-26200-<identity>.json
```

This command is for maintainers and CI preparation, not end users.

## 9.3 `manifestc`

Responsibilities:

- validate all JSON records against a schema;
- reject duplicate module identity combinations;
- reject duplicate or unknown stable symbol IDs;
- verify every RVA lies in an appropriate section of a supplied reference binary when available;
- compile records into `compat.bin`;
- produce a deterministic build byte-for-byte;
- output a human-readable manifest index for review.

## 9.4 `manifestsign`

Responsibilities:

- sign the SHA-256 hash of the complete manifest with ECDSA P-256;
- read the private key only from an explicit secure file or hardware-backed store;
- produce a detached signature with a small versioned header;
- provide a `verify` command used in release CI.

The private key must never be embedded in the application or committed.

## 9.5 Compatibility publication workflow

For every new Windows taskbar module set:

1. Obtain the actual binaries from a test machine.
2. Run `moduleid` and compare with existing records.
3. Run `compatgen` against matching public symbols.
4. Review every required symbol and RVA.
5. Build a debug application with the new local record.
6. Run the full manual taskbar test matrix.
7. Mark the record `tested: true` only after successful real-machine testing.
8. Compile and sign the manifest.
9. Publish the manifest and signature as release assets.
10. Keep untested generated records out of the public manifest.

A symbol match is not equivalent to behavioral compatibility. Real taskbar testing is mandatory.

---

## 10. Settings and persistence

Store small per-user settings under:

```text
HKCU\Software\TaskbarThumbnailReorder
```

Values:

```text
Enabled                 REG_DWORD, default 1
CheckManifestUpdates    REG_DWORD, default 1
LastManifestCheckUtc    REG_QWORD
LastKnownAppVersion     REG_SZ
```

Autostart remains represented only by the dedicated Run value; do not duplicate it as a setting that can drift.

Do not store symbol offsets in the registry. Store the verified downloaded manifest at:

```text
%LOCALAPPDATA%\TaskbarThumbnailReorder\compat\compat.bin
%LOCALAPPDATA%\TaskbarThumbnailReorder\compat\compat.sig
```

At startup, choose the newest valid compatible manifest between the embedded base manifest and the local signed manifest. Never prefer a local manifest merely because its file timestamp is newer; compare the internal monotonically increasing manifest sequence.

---

## 11. Build configuration

## 11.1 Toolchain

- Visual Studio 2022 Build Tools or newer compatible MSVC toolset.
- Windows 11 SDK with C++/WinRT headers.
- CMake 3.28 or newer.
- Ninja or Visual Studio generator.
- x64 only for version 1.0.

## 11.2 Compiler rules

Common:

```text
/std:c++20
/permissive-
/Zc:__cplusplus
/Zc:preprocessor
/W4
/utf-8
/DUNICODE /D_UNICODE
```

Release:

```text
/O2
/GL
/MT
/Gy
/Gw
/DNDEBUG
```

Linker:

```text
/LTCG
/OPT:REF
/OPT:ICF
/DYNAMICBASE
/NXCOMPAT
/HIGHENTROPYVA
/CETCOMPAT when supported by all dependencies
```

CI adds `/WX`. Local developer builds may keep warnings non-fatal while actively iterating, but release and pull-request CI must be warning-free.

## 11.3 Reproducibility

- Pin MinHook by commit or release tag.
- Generate version headers from Git metadata plus a release version.
- Produce SHA-256 sums for the EXE, embedded payload, and manifest.
- Record MSVC toolset and Windows SDK versions in release metadata.
- Use deterministic compilation options where supported.

---

## 12. Codex execution plan

The work should be split into ordered work packages. Agents may work in parallel only where interfaces are already fixed by this document.

## Work Package 0 — Repository and legal baseline

**Owner:** architecture/bootstrap agent  
**Dependencies:** none

Tasks:

1. Create the repository layout exactly as specified.
2. Add GPL-3.0-only license and third-party notice placeholders.
3. Vendor pinned MinHook source and license.
4. Add CMake targets:
   - `TaskbarThumbnailReorder` WIN32 executable;
   - `TTRHook64` shared library;
   - `moduleid`;
   - `manifestc`;
   - unit tests.
5. Configure embedded payload resource generation.
6. Create protocol and symbol-ID headers with static layout assertions.
7. Add CI that builds Debug and Release x64 and runs non-Explorer unit tests.

Exit criteria:

- clean x64 Release build on a fresh Windows developer environment;
- EXE embeds the DLL and can extract it with a verified hash;
- no application functionality yet;
- all licenses present.

## Work Package 1 — Host shell and tray UX

**Owner:** host agent  
**Dependencies:** WP0

Tasks:

1. Implement hidden message window and message loop.
2. Implement single-instance mutex and second-launch activation.
3. Implement `Shell_NotifyIconW` with `NOTIFYICON_VERSION_4`.
4. Handle `TaskbarCreated` and tray icon restoration.
5. Implement the fixed context menu and icon state switching.
6. Implement registry settings.
7. Implement autostart copy/handoff and Run value toggle.
8. Implement About and diagnostics-save shell.
9. Implement cleanup mode.

Exit criteria:

- tray application runs with no console window;
- enable toggle updates state locally;
- autostart installs to LocalAppData and survives moving/deleting the original EXE;
- second launch activates the existing instance;
- Explorer restart restores the tray icon;
- no injection yet.

## Work Package 2 — PE identity and manifest runtime

**Owner:** compatibility-runtime agent  
**Dependencies:** WP0

Tasks:

1. Implement safe PE identity parsing shared by host and `moduleid`.
2. Implement packed manifest format and parser.
3. Implement section/RVA validation.
4. Implement embedded-manifest selection.
5. Implement CNG signature verification for external manifests.
6. Implement deterministic `manifestc` with schema checks.
7. Add malformed-manifest and overflow unit tests.

Exit criteria:

- exact identities are produced for Windows DLL fixtures;
- every malformed size, count, offset, duplicate, and bad signature test fails safely;
- host can report supported/unsupported against a synthetic manifest;
- no Explorer memory is modified.

## Work Package 3 — Thread-specific Explorer loading

**Owner:** injection/lifecycle agent  
**Dependencies:** WP1, WP2

Tasks:

1. Implement `Shell_TrayWnd` discovery and Explorer verification.
2. Create secure shared session mapping.
3. Load the extracted payload DLL locally and find the exported hook procedure.
4. Install `WH_CALLWNDPROC` on the taskbar thread.
5. Force one benign callback with `SendMessageTimeoutW`.
6. Implement command/ack protocol and timeouts.
7. Implement clean `PrepareUnload` and `UnhookWindowsHookEx` flow.
8. Implement Explorer process-exit observation and reattachment.
9. Add a payload test mode that only acknowledges initialization and installs no MinHook targets.

Exit criteria:

- the test payload enters only the verified Explorer process;
- enable/disable repeatedly loads and unloads without leaking hook handles;
- Explorer restart reattaches automatically;
- Explorer hang simulation cannot freeze the host UI;
- normal user privileges are sufficient.

## Work Package 4 — Compatibility-generation tools

**Owner:** symbols agent  
**Dependencies:** WP2

Tasks:

1. Implement `compatgen` using DIA.
2. Encode the exact symbol alternatives from the current upstream mod in `symbol-spec.yaml`.
3. Generate records for the exact target computer's installed modules.
4. Build `manifestsign` and verification command.
5. Document how to locate the active `Taskbar.View.dll` or `ExplorerExtensions.dll` module.
6. Produce a reviewed local compatibility manifest.

Exit criteria:

- required symbols resolve uniquely for the target module identities when public PDBs are available;
- generated RVAs pass section validation;
- manifest build is deterministic;
- signed manifest verifies in the host;
- missing PDBs produce a clear maintainer error rather than guessed offsets.

## Work Package 5 — Payload common model and classic backend

**Owner:** payload agent A  
**Dependencies:** WP3, WP4

Tasks:

1. Port common task-group and DPA movement code.
2. Implement MinHook transaction wrapper.
3. Implement payload logging and fault gate.
4. Implement classic thumbnail hooks and drag state.
5. Implement taskbar refresh across primary and secondary taskbars.
6. Add internal validation counters and error codes.

Exit criteria:

- on a compatible classic-thumbnail test build, dragging reorders previews;
- click-without-drag remains stock behavior;
- disable during idle removes all hooks;
- invalid synthetic DPA fixtures are rejected;
- no partial hook installation remains after forced failures.

## Work Package 6 — Modern XAML backend

**Owner:** payload agent B  
**Dependencies:** WP3, WP4, shared payload interfaces from WP5

Tasks:

1. Add C++/WinRT projections required for system XAML and WinUI 2 `ItemsRepeater`.
2. Implement weak thumbnail-to-task-item mapping.
3. Hook target-item resolution and thumbnail vector access.
4. Implement visual-tree helpers and repeater lookup paths.
5. Implement pressed/hovered index detection.
6. Implement adjacent reorder operation.
7. Implement click suppression and flyout-position workaround.
8. Enforce mapping and item-count bounds.

Exit criteria:

- exact target build 26200 reorders modern taskbar thumbnails by LMB drag;
- fast normal clicks, close buttons, Aero Peek, and stock hover behavior remain functional;
- repeated reordering does not grow mapping memory;
- multi-monitor behavior passes;
- all custom behavior stops after disable.

## Work Package 7 — Integration hardening

**Owner:** integration agent  
**Dependencies:** WP1–WP6

Tasks:

1. Unify payload backend selection and report active backend.
2. Add callback-active counters and safe unload drain.
3. Add first-fault behavior and host warning state.
4. Add manifest update download, signature check, and atomic storage.
5. Add full diagnostics export.
6. Add icon assets and final resource metadata.
7. Measure binary size, memory, CPU, startup, and drag latency.
8. Remove unused code and dependencies.

Exit criteria:

- all performance ceilings are met or deviations are documented and approved;
- unknown module records never call MinHook;
- corrupted external manifests are ignored;
- no file or network I/O occurs inside Explorer;
- release binaries contain correct version and license metadata.

## Work Package 8 — Release qualification

**Owner:** test/release agent  
**Dependencies:** all prior work packages

Tasks:

1. Execute the manual matrix below on both target Windows 11 computers.
2. Test a deliberately unsupported module identity.
3. Test Explorer restart, crash, sleep/resume, logoff/logon, and taskbar recreation.
4. Scan with Microsoft Defender and record results.
5. Test from Downloads, Desktop, a USB drive, and a path containing spaces/non-ASCII characters.
6. Test autostart after deleting the original portable copy.
7. Test removal and residual files/registry values.
8. Produce release ZIP containing:
   - the single EXE;
   - `README.txt` only if desired for source distribution, not required for operation;
   - checksums and source link.

Exit criteria:

- both computers pass the complete matrix;
- no Explorer crash or hang is observed;
- unsupported builds fail closed;
- transfer to the second computer requires only copying and double-clicking the EXE;
- release checklist is signed off.

---

## 13. Test plan

## 13.1 Unit tests

- PE parser with valid x64 fixture.
- Truncated DOS, NT, optional, section, debug, and CodeView records.
- Integer-overflow cases in every offset calculation.
- Manifest parser with bad magic, version, counts, offsets, symbol IDs, and duplicate records.
- ECDSA valid and invalid signatures.
- Symbol RVA section-type validation.
- Protocol struct sizes and cross-module layout.
- Shared log ring wraparound and dropped-event accounting.
- DPA movement with 0, 1, 2, and many items.
- Forward and backward item moves.
- Missing source or target item.
- Excessive or invalid DPA counts.
- Host state-machine transitions.
- Autostart command quoting with spaces and Unicode paths.

## 13.2 Integration tests without modifying the real taskbar

Create a test process and synthetic DLL with known exported functions. Use it to verify:

- thread-specific `SetWindowsHookEx` loading;
- shared mapping discovery;
- command acknowledgement;
- MinHook create/enable/disable transaction;
- unload after active-callback drain;
- target process exit handling;
- wrong PID/nonce rejection.

Do not use `explorer.exe` for automated CI tests.

## 13.3 Manual taskbar matrix

Run each scenario with at least five windows in one taskbar group:

| Scenario | Required result |
|---|---|
| Drag one position left and right | order changes once per crossed thumbnail |
| Drag first thumbnail to last slowly | all intermediate adjacent changes occur |
| Drag last to first slowly | same in reverse |
| Click without drag | selected window activates normally |
| Press then release on same thumbnail | stock behavior |
| Drag and release over another thumbnail | no unintended activation after reorder |
| Rapid pointer movement | no crash; missed non-adjacent transitions logged at most |
| Close thumbnail button | stock close behavior |
| Middle-click thumbnail if supported | stock behavior |
| Aero Peek delay | no stuck preview or timer |
| Scrollable thumbnail list | source and target detection remains correct |
| Primary monitor | works |
| Secondary monitor | works |
| Taskbar auto-hide | works without moving the flyout off-screen |
| 100%, 150%, and 200% DPI | pointer hit testing correct |
| Light and dark mode | tray icons remain legible |
| Chrome/Edge group | works |
| File Explorer group | works |
| Notepad group | works |
| Mixed virtual desktops | no cross-group corruption |
| Disable from tray | behavior immediately returns to stock |
| Re-enable | behavior returns without Explorer restart |
| Restart Explorer | host reattaches once |
| Kill Explorer during drag | no host hang; new Explorer attaches cleanly |
| Sleep/resume | remains active or reattaches |
| Unsupported manifest | no injection; warning state |
| Bad external signature | embedded manifest used; error logged |

## 13.4 Long-run test

For each target machine:

- run enabled for at least eight hours of normal use;
- perform at least 500 reorder transitions using a small test automation only for mouse movement, with visual/manual confirmation;
- restart Explorer 20 times;
- toggle enable/disable 100 times;
- monitor Explorer private bytes and handle count;
- verify no monotonic growth attributable to the payload.

## 13.5 Performance measurement

Instrument debug builds with `QueryPerformanceCounter` around only custom pointer-move logic. Collect aggregate counts and percentile buckets in shared memory; never log each mouse event to disk.

Release qualification thresholds are the budgets in section 4.3. Any hard-ceiling violation blocks release.

---

## 14. Known risks and mitigations

| Risk | Likelihood | Impact | Required mitigation |
|---|---:|---:|---|
| Windows update changes private taskbar internals | High | Feature stops or Explorer crash if unchecked | exact module identity; fail closed; signed manifest updates |
| Microsoft PDB unavailable for a new build | Medium, higher for Insider builds | temporary incompatibility | explicit warning; no guessed offsets; retry compatibility publication later |
| Security software flags injection behavior | Medium | launch/load blocked | documented thread hook, no remote shellcode, signed releases, transparent source, diagnostics |
| Hook function prologue incompatible with MinHook | Low to medium | attachment failure | transactional creation; mark record incompatible; no partial hooks |
| Explorer restarts during initialization | Medium | stale handles/state | process-exit observation; generation IDs; discard old mapping |
| XAML visual-tree names change | Medium | modern backend fails | manifest backend flags; structural checks; original behavior fallback |
| Thumbnail animation behavior changes | Medium | drag ends early or visual glitches | per-record movement flags; adjacent-only mode; test before publication |
| Payload unload while callback active | Low but severe | Explorer crash | active-call counter, disable gate, bounded drain, refuse forced unload |
| Autostart path moved/deleted | Low | no startup | install stable LocalAppData copy when enabling autostart |
| Downloaded manifest tampered with | Low | arbitrary invalid hook addresses | offline ECDSA signature, exact parsing, RVA validation |
| GPL attribution omitted | Low | license noncompliance | release checklist and third-party notices |

---

## 15. Release definition of done

Version 1.0 is complete only when all statements below are true:

- A single `TaskbarThumbnailReorder.exe` can be copied to and launched on both target computers.
- No administrator prompt appears.
- Windhawk is not installed or required.
- Left-button dragging reorders grouped taskbar thumbnails on the exact tested module identities.
- Normal click, close, hover, and Aero Peek behavior remain intact.
- Tray enable/disable works repeatedly.
- “Start with Windows” works after the original portable file is removed.
- Explorer restart is recovered automatically.
- Unknown or changed taskbar binaries produce a warning and no hooks.
- Payload unload is safe under stress tests.
- Host and Explorer memory/CPU remain within hard ceilings.
- Diagnostics can identify module identities and hook failures without exposing personal window data.
- Source, GPLv3 license, attribution, MinHook license, build instructions, and checksums are published together.
- Both target computers pass the complete manual and long-run matrices.

---

## 16. First implementation milestone for the supplied PC

The supplied OS line `10.0.26200 / Build 26200` is not precise enough to create safe offsets. The first running prototype must collect the exact identities of these files on that computer:

```text
C:\Windows\explorer.exe
C:\Windows\System32\taskbar.dll
loaded Taskbar.View.dll, when present
loaded ExplorerExtensions.dll, when present
```

Codex should implement `moduleid` before writing any hook offsets. It must then generate and test a compatibility record for the exact PDB GUID/age and image metadata found on the machine.

The project must not claim generic “build 26200 support” after testing only one cumulative revision. Release notes should list exact compatible module identities or file versions and state that unknown revisions fail closed.

---

## 17. Research references

1. Current Windhawk Taskbar Thumbnail Reorder source, including classic and XAML implementations, private symbol names, and the animated-thumbnail adjacent-move workaround:  
   https://github.com/ramensoftware/windhawk-mods/blob/main/mods/taskbar-thumbnail-reorder.wh.cpp

2. Windhawk mod page and supported behavior description:  
   https://windhawk.net/mods/taskbar-thumbnail-reorder

3. Current issue demonstrating breakage on a later Windows 11 Insider taskbar build and the need for exact compatibility handling:  
   https://github.com/ramensoftware/windhawk-mods/issues/4238

4. Windhawk discussion of Microsoft symbol availability and taskbar PDB delays:  
   https://github.com/ramensoftware/windhawk/issues/922

5. Windhawk development notes for symbol-based hooks and caching:  
   https://github.com/ramensoftware/windhawk/wiki/Development-tips

6. Microsoft documentation for thread-specific Windows hooks and `SetWindowsHookEx`:  
   https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowshookexw  
   https://learn.microsoft.com/en-us/windows/win32/winmsg/about-hooks

7. Microsoft documentation for notification-area icons and taskbar recreation notification:  
   https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw  
   https://learn.microsoft.com/en-us/windows/win32/shell/taskbar

8. Microsoft documentation for per-user Run autostart:  
   https://learn.microsoft.com/en-us/windows/win32/setupapi/run-and-runonce-registry-keys

9. Microsoft PE/COFF format documentation:  
   https://learn.microsoft.com/en-us/windows/win32/debug/pe-format

10. Microsoft CNG signing and verification documentation:  
    https://learn.microsoft.com/en-us/windows/win32/seccng/signing-data-with-cng

11. MinHook upstream project:  
    https://github.com/TsudaKageyu/minhook

12. Windhawk GPLv3 license:  
    https://github.com/ramensoftware/windhawk/blob/main/LICENSE

---

## 18. Final instruction to Codex agents

Optimize for **Explorer safety, exact compatibility, and user simplicity**, in that order. Do not weaken module matching to make a new Windows build appear supported. Do not add a general plugin system. Do not add a framework UI. Do not perform network or file operations from Explorer. Do not force-unload the payload while callbacks may be active. Keep every build-specific address in reviewed compatibility data, never in ad hoc source constants.
