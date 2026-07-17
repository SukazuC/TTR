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
10. Increment the combined manifest sequence, retain older safe records, sign the pair, and publish
    only `compat.bin`, `compat.sig`, checksums, and reviewed record JSON to the public compatibility
    repository. Never publish PDBs, symbol caches, private keys, dumps, or build output.
11. Download the published raw files again, compare their hashes byte-for-byte with the qualified pair,
    and verify the signature before treating the feed update as complete.

The official endpoint is
`https://raw.githubusercontent.com/SukazuC/TTR-compat/main/compat.bin`; its sibling signature is
`compat.sig`. Runtime installation accepts a sequence only when it is greater than the embedded,
selected external, valid backup, and highest previously installed sequences. Final-location
verification and highest-sequence persistence must succeed or the previous valid pair is restored.

If public symbols omit a required stable symbol, report the exact ID and stop. Pattern scanning, guessed
offsets, and build-number-only reuse are not fallbacks.
