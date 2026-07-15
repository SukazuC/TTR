# Release checklist

- Fresh Debug and personalized Release x64 builds pass `/W4 /WX /permissive-`.
- Debug, Release, and AddressSanitizer CTest suites pass.
- MSVC `/analyze` build passes without broad suppressions.
- Tool signing, tamper, PDB mismatch, ambiguity, missing-symbol, and optional-symbol cases pass.
- Host and embedded payload are x64; embedded bytes equal the built DLL.
- Resource icons and version metadata match the project version.
- Static runtime has no VCRUNTIME/MSVCP dependency.
- Package-only offline diagnostics pass and unsupported systems remain fail closed.
- Private key, PDBs, signatures, dumps, and unqualified compatibility files are absent from the package.
- `SHA256SUMS.txt`, `BUILD-REPORT.md`, and `MANUAL-QUALIFICATION.md` accompany the EXE.
- CI completed on an actual Windows runner without executing live shell integration.
- Live qualification status is stated explicitly; it is never inferred from symbol resolution.
