# Architecture

The host is a small Unicode Win32 tray process. It validates the same-session system Explorer, enumerates the loaded taskbar modules, selects an exact compatibility record, extracts the embedded DLL atomically, verifies its SHA-256, creates a current-session shared mapping, and installs one thread-specific `WH_CALLWNDPROC` hook.

The payload opens the PID-scoped mapping from the taskbar thread, validates the protocol and host process, independently matches module identities from the loaded PE images, validates every RVA against section permissions, and only then creates a queued MinHook transaction. Commands are acknowledged through interlocked sequence fields. Disable gates behavior before draining callbacks and removing hooks.

No compatibility record means no `SetWindowsHookEx` call. The payload performs no network access and resolves module identity from memory rather than reading files inside Explorer.
