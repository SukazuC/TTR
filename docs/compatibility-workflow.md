# Compatibility workflow

1. Run `moduleid` against each installed taskbar module and retain its exact timestamp, image size,
   RSDS GUID, and age.
2. Download a PDB only from Microsoft's public symbol service using that exact GUID/age key.
3. Run `compatgen <module> <pdb> compat/symbol-spec.yaml <classic|xaml> <fragment.json>`. DIA rechecks GUID and age
   before symbol enumeration. Missing, ambiguous, or wrong-permission required symbols stop generation.
4. Store all dry-run output under `out/qualification/unqualified/`; never copy it to `%LOCALAPPDATA%`.
5. Review stable symbol IDs, backend groups, RVA permissions, and module identity. Do not substitute a
   PDB based on filename, Windows build number, or a nearby release.
6. Compile reviewed JSON twice with `manifestc` and compare hashes.
7. Sign the binary using the personal ECDSA P-256 private key outside the repository. Verify with the
   public key embedded in the personalized executable.
8. Run `--diagnose-offline --manifest ... --signature ...`; it must report an exact record match and
   validate every symbol RVA.
9. Complete the separate manual qualification before marking or publishing a record as tested.

If public symbols omit a required stable symbol, report the exact ID and stop. Pattern scanning, guessed
offsets, and build-number-only reuse are not fallbacks.
