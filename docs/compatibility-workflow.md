# Compatibility workflow

1. Run `moduleid` against the installed taskbar modules.
2. Run `compatgen <module> <pdb> compat/symbol-spec.yaml <fragment.json>`. It verifies the PDB GUID/age, resolves every exact symbol alternative through DIA, rejects ambiguity, and validates PE section permissions.
3. Review function/data section classification and required backend groups.
4. Compile deterministic JSON with `manifestc`.
5. Sign the binary using an offline ECDSA P-256 CNG private-key blob.
6. Test every record on the exact binaries before publication.

Merge the reviewed per-module fragments into a record JSON file for `manifestc`; never publish a generated record until the real taskbar behavior has been tested.
