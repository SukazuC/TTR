# Manual Explorer qualification

This procedure is intentionally separate from automated qualification. It is the first point at
which the payload may be loaded into Explorer. Do not begin until every entry criterion is met.

## Entry criteria

- Debug, Release, `/analyze`, and AddressSanitizer builds are complete with no unresolved diagnostics.
- Debug, Release, and AddressSanitizer CTest runs pass.
- `TaskbarThumbnailReorder.exe --diagnose-offline` passes from the package directory.
- The installed module identities exactly equal the identities in the candidate record.
- `compatgen` verified each PDB GUID and age and every required symbol resolved uniquely.
- Manifest version 2 contains a complete backend-specific dependency set. Classic records include an
  exact DIA-derived `CTaskListWnd` to `ITaskListUI` adjustment; XAML records include at least one
  constructor that provides `ITaskListUI` and do not carry unused classic requirements.
- Every generated RVA was reviewed for the required executable or read-only section permission.
- The record remains marked unqualified and has not been published or copied to the live application
  directory.
- The manifest is compiled deterministically and signed by the intended personal key.
- Work is saved and the tester accepts that private Explorer internals can still crash Explorer even
  after static qualification.

If any item is false, stop. Never substitute a nearby Windows build, PDB, or guessed RVA.

`compatgen` is invoked separately for each exact module and selected backend:

```text
compatgen <module> <matching-pdb> <symbol-spec> <classic|xaml> <output-json>
```

It verifies the module/PDB CodeView identity before emitting anything. DIA base adjustments are written
to the JSON `adjustments` array with their type size and are never represented as symbol RVAs. Missing,
ambiguous, misaligned, or out-of-bounds results are fatal. Combine only the emitted exact-module
fragments into a version 2 manifest input; every symbol and adjustment must name its module index.

## Controlled activation

1. Record the SHA-256 of the package EXE, manifest, and signature.
2. Run `--diagnose-offline --manifest <compat.bin> --signature <compat.sig>` and save the passing report.
3. Confirm the report says `live_integration=false`, names the exact installed modules, and reports an
   exact record match with all symbol permissions valid.
4. Copy only the reviewed `compat.bin` and matching `compat.sig` pair to
   `%LOCALAPPDATA%\TaskbarThumbnailReorder\compat\`.
5. Launch the packaged EXE normally. This is the first live integration action.
6. Confirm the tray reports Active once. If it reports Faulted or Explorer restarts, disable the
   feature, exit the host, preserve diagnostics, and stop qualification.
7. With at least five windows in one group, verify click-without-drag, same-thumbnail press/release,
   close, middle-click, hover, and Aero Peek before attempting a reorder.
8. Test one adjacent drag left and one adjacent drag right. Confirm exactly one move occurs and release
   does not activate the wrong window.
9. Complete slow first-to-last and last-to-first moves, rapid movement, scrollable thumbnails, primary
   and secondary monitors, auto-hide, 100/150/200% DPI, multiple application groups, and virtual
   desktops. Unknown layouts must remain unsupported.
10. Toggle disable/enable ten times, then restart Explorer once through the normal user interface and
    confirm one clean reattachment. Do not terminate Explorer during the first qualification pass.
11. Export diagnostics and compare module identities, record ID, hook count, and payload hash with the
    pre-activation report.
12. Only after the functional matrix passes, perform the long-run matrix: eight hours, 500 adjacent
    reorder transitions, 20 controlled Explorer restarts, and 100 enable/disable cycles while observing
    Explorer handles, memory, CPU, and crashes.

## Exit and publication gate

Disable the feature and exit the host. A record may be changed from `unqualified` to `tested` only when
all behavior, restart, stress, and resource observations pass on the exact module identities. Any
Explorer crash, stale capture, wrong activation, array divergence, leak trend, missing symbol, or
identity change invalidates the record and blocks publication.
