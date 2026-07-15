# Release checklist

- Build Debug and Release x64 with warnings as errors.
- Run all unit tests and the synthetic hook-process tests.
- Verify EXE and embedded payload hashes.
- Verify every published compatibility signature and exact module identity.
- Complete the manual and long-run matrices from the implementation plan on both target computers.
- Confirm unknown identities perform no injection.
- Record Defender results, toolset/SDK versions, sizes, memory, CPU, and latency.
- Publish source, GPL license, third-party notices, and checksums with the binary.
