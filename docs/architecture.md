# Architecture

The host is a small Unicode Win32 tray process. It validates the same-session system Explorer, enumerates the loaded taskbar modules, selects an exact compatibility record, extracts the embedded DLL atomically, verifies its SHA-256, creates a current-session shared mapping, and installs one thread-specific `WH_CALLWNDPROC` hook.

The payload opens the PID-scoped mapping from the taskbar thread, validates the protocol and host process, independently matches module identities from the loaded PE images, validates every RVA against section permissions, and only then creates a queued MinHook transaction. Commands are acknowledged through interlocked sequence fields. Disable gates behavior before draining callbacks and removing hooks.

No compatibility record means no `SetWindowsHookEx` call. The payload performs no network access and resolves module identity from memory rather than reading files inside Explorer.

## Task-list UI dependency

```text
EnumThreadWindows
  -> Shell_TrayWnd/TaskbandHWND or Shell_SecondaryTrayWnd/WorkerW
  -> MSTaskListWClass extra bytes
  -> CTaskListWnd (one per taskbar)
       classic: signed DIA base adjustment -> ITaskListUI
       XAML: TaskItemThumbnail constructor -> ITaskListUI
             | weak thumbnail + non-owning group/item/UI + generation
             `-> source and target must have the same group, UI, and generation
  -> move the task-group array and every matching primary/secondary taskbar array
  -> ITaskListUI::TaskInclusionChanged
```

The classic backend has no constructor-provided interface and is enabled only when the exact PDB
contains an unambiguous, aligned, in-bounds `CTaskListWnd` to `ITaskListUI` base adjustment. The
adjustment is a typed manifest value, never an RVA. The XAML backend does not require that adjustment:
it captures the constructor argument without `AddRef`, rejects cross-interface mappings, and discards
all mappings on disable, restart, or generation invalidation. If a hybrid record supplies both sources,
the constructor interface must equal the adjusted interface for at least one enumerated taskbar.

Primary and secondary taskbars can contain separate `CTaskListWnd`/`ITaskListUI` instances. All
matching arrays are validated before mutation and moved as one transaction. Notification failure rolls
the arrays back. Raw taskbar interfaces never outlive the enabled generation and are never used to
extend Explorer object lifetime.

## Exact compatibility architecture

Manifest version 2 separates backend requirements. Shared task-model symbols are required by both
backends, classic thumbnail symbols and the typed interface adjustment are required only by classic,
and constructor/vector/flyout symbols are required only by XAML. A record may contain either complete
backend or both complete backends; unused backend data, optional required entries, duplicates, invalid
module indices, misaligned or out-of-bounds adjustments, and incomplete one-of constructor sets are
rejected. The payload repeats identity, section-permission, backend-completeness, and adjustment checks
inside the target process before queuing hooks.

The following alternatives are deliberately rejected: scanning a vtable, copying an RVA from another
build, storing a base adjustment as a synthetic symbol RVA, deriving offsets from instruction patterns,
making a required dependency optional, or retaining constructor interfaces with ownership beyond the
thumbnail mapping generation.
