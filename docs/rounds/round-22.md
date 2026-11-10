# R22: public source preparation, 2026-11-10

## Changes

Replaced conversational handoffs with concise engineering notes and stable issue indexes. The exact 1,185-file
working tree is retained in an ignored local archive with per-file SHA-256 verification. Removed 663 archived
scratch/transcript/image files from public paths, retaining reusable numerical tools. Personal attributions in
code comments were replaced with engineering explanations; original comments remain in the archive.

PT-STATE reconciles the 10-12 rendering handoff with the 10-30 Unity build break. PT remains tabled. It separates
merged work, pending game/GPU checks and missing unmerged worker copies. PT-149 records the fresh CMake
Mega Geometry default mismatch; PT-150 records the managed Unity compatibility blocker. Historical PT-103 had
no standalone entry and remains unused. All 45 historical NR IDs have concise topics, with motion/hardware limits
preserved. NR-46 records three mutation-tool paths updated to the current DLSS SDK.

Added SDK inventory and source-export tools. Four superseded SDK folders, two obsolete build trees and five
extracted download ZIPs moved to `.development/retired/2026-11-10/`. Generated caches are disposable, not reasons
to retain an obsolete SDK. Current dependencies remain in place; vendor code, installed game files and player
packages were not changed. No commit, push or publication.

Export excludes Git metadata, SDK/runtime binaries, private history, scratch copies and generated output.

## Validation

- 1,185 archived files: SHA-256 verified, zero mismatches.
- 121 source checks and eight source-export/SDK-inventory tests pass.
- Standalone 536-file snapshot passes the same checks without local SDKs or private history.
- All local Markdown file links resolve across 38 documents.
- Native/managed/shader executable tokens match the archived baseline across 46 changed files.
- Ten PowerShell files match apart from comments and documented worker wording. Ten Python files match apart
  from documentation and the explicitly updated SDK pins.
- Three mutation tools parse and their current SDK inputs exist; no GPU mutation run.

No game, renderer build, GPU test or new appearance/hardware validation. The source ZIP has a per-file SHA-256
manifest; validation logs are under `artifacts/public-source/verification/`.

## Remaining work

Choose publication route separately: existing Git history still contains earlier private material. Use a new
source snapshot/history or plan a reviewed history rewrite before changing existing repository visibility.
PT implementation stays tabled; use PT-STATE's restart order only after explicit resumption.

## Starter prompt (R23)

Read AGENTS, BACKLOG, PT-STATE and PROJECT-CONTEXT/DEAD ENDS. Preserve the local archive and current concise public
notes. Do not revive older starter prompts or assume missing megageo7/framelate changes were merged.
PT remains tabled. Resolve any requested publication work using PUBLIC-SOURCE.md; do not expose old Git history.
