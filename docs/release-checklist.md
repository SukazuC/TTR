# Release checklist

- Fresh Debug and personalized Release x64 builds pass `/W4 /WX /permissive-`.
- Debug, Release, and AddressSanitizer CTest suites pass.
- MSVC `/analyze` build passes without broad suppressions.
- Tool signing, tamper, PDB mismatch, ambiguity, missing-symbol, and optional-symbol cases pass.
- Host and embedded payload are x64; embedded bytes equal the built DLL.
- Resource icons and version metadata match the project version.
- Static runtime has no VCRUNTIME/MSVCP dependency.
- Package-only offline diagnostics validate the embedded signature and supported exact record on the
  qualified machine; unsupported systems remain fail closed.
- Private keys, PDBs, dumps, symbol caches, and unqualified compatibility files are absent from the
  package. The qualified public manifest, signature, and verification key are embedded intentionally.
- `SHA256SUMS.txt`, `BUILD-REPORT.md`, and `MANUAL-QUALIFICATION.md` accompany the EXE.
- CI completed on an actual Windows runner without executing live shell integration.
- Live qualification status and scope are recorded explicitly; they are never inferred from symbol
  resolution. Records `2620013101` and `2620013102` have focused manual drag evidence dated July 16
  and July 17, 2026 respectively.
- The official build has a non-empty HTTPS compatibility endpoint unless
  `TTR_ALLOW_NO_UPDATE_ENDPOINT=ON` was deliberately selected for a development-only build.
- The published compatibility pair is byte-identical to the qualified pair and verifies with the
  embedded public key. Equal, older, corrupt, truncated, and incorrectly signed candidates leave the
  installed valid pair unchanged.
