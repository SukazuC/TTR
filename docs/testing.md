# Testing

Automated tests never load the payload into Explorer. They cover generic and strict PE parsing,
malformed headers and CodeView records, RVA permissions, manifest bounds/overlap/selection, ECDSA key
and tamper cases, deterministic tools, DIA identity and symbol failures, and the offline diagnostic.

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug
cmake --build --preset release
ctest --preset release

cmake --preset vs2022-x64-asan
cmake --build --preset asan
ctest --preset asan

cmake --preset vs2022-x64-analyze
cmake --build --preset analyze
```

ASan uses `RelWithDebInfo`; the build copies the installed MSVC ASan runtime beside offline test
executables. `/analyze` runs with `/WX`, so analyzer findings fail the build. No analyzer diagnostic is
suppressed globally.

The only live behavior procedure is [MANUAL-QUALIFICATION.md](MANUAL-QUALIFICATION.md). Never run an
unreviewed generated RVA against Explorer.
