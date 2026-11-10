# Public source preparation

The public files contain engineering requirements, current status, stable issue IDs and technical lessons.
Full prior development history is preserved separately; see [DEVELOPMENT.md](DEVELOPMENT.md).

## Source snapshot

From the repository root:

```powershell
python tools/maintenance/public_source.py --check
python tools/maintenance/public_source.py
```

The exporter selects current working-tree source, including new unignored files and excluding deleted files.
It includes source, tests, build scripts, concise docs, product images and notices. It excludes SDK downloads,
game/runtime binaries, build output, scratch trees, generated test images, the private archive and Git metadata.
Each included file has a SHA-256 entry in SOURCE-MANIFEST.json. Output goes under artifacts/public-source and
an existing export is never overwritten.

This is a source snapshot, not a tested player package. Rendering behaviour and released packages are unchanged.
Personal attributions were removed from code comments; three mutation tools now reference the current DLSS SDK.
Dependencies remain separate: [BUILD-DEPENDENCIES.md](BUILD-DEPENDENCIES.md).

## Existing Git history

Deleting or ignoring a file does not remove earlier committed copies. The repository's existing history still
contains detailed conversations, scratch files and images. Changing its visibility would expose that history.
Use the exported snapshot for a new public history, or separately plan a reviewed history rewrite. Neither
publishing nor history rewriting is performed by this preparation.

Keep the private archive and existing repository backed up before choosing a publication route. A fresh public
repository does not include private research; essential decisions and open work are documented in public notes.

## Dependency inventory

```powershell
python tools/maintenance/audit_sdks.py
```

The audit is read-only. Superseded DLSS/Streamline/FidelityFX inputs and their obsolete build caches have been
retired into the private archive. Cache references are informational, not reasons to retain obsolete SDKs.
PT's RTXDI, NRD, NVAPI, DXC and Agility inputs remain required by the native build even while managed PT is tabled.
