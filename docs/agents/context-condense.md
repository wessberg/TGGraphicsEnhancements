# Context condensation — 2026-10-12, base20i

Docs only. PROJECT-CONTEXT: 975 to 700 lines. MODS: new, 159 lines.
PT-DETAILS: 243 to 625 lines. AGENTS: 564 to 566 lines.

- Sections 5-8 moved word for word to `docs/PROJECT-CONTEXT-MODS.md`; each old heading has two pointer lines.
  AGENTS changes are only START HERE step 2 and the new Where things live row.
- Section 9 now describes this copy's code and round 20's newest state (10-12 01:25).
  Checked ABI/sizes, every listed option/material/instance/frame bit, masks, samplers, defaults and stage order
  against the native header, managed writer and shader/scene definitions. Comments were sometimes stale too.
- Earlier design reasons and measurements moved verbatim to dated headings at the end of `docs/PT-DETAILS.md`:
  ABI/frame handoff; capture/LOD/alpha; lights; exposure/fog; RTXDI; denoisers and NR validation;
  surface migration; cache; captures; harness; performance; atlas keys. The archive is explicitly historical.
- Removed wrong active inventories: obsolete passes, old capture caps/exclusions, the 25 cm lamp floor and
  radiance clamps, retired settings, old stage counts and obsolete bit allocations. Their replacements describe
  the code; history that explains the design remains archived.
- Sections 10 and 11 are intact. Added five dead ends: separate water pass/loop regrouping, cluster rays/layout,
  caustcut, backwards game clock, and the underwater-camp theory. Character micromaps already had an entry.
- Contradictions kept for the coordinator: DEAD ENDS says full RELAX failed and to denoise bounced light alone;
  PT-127 replaces that with corrected inputs/all light. It also says current DI history is 20; code/default is 4.
  The old denoised-reference entry's final sentence about separate sun/lamp signals likewise predates full NRD.
  The reference lesson itself still holds. No older entry was deleted.
- Limits of the evidence: water-pass rejection rests on two GPU-contended runs, so no clean timing claim.
  Mega Geometry's town walk has late-start flags. The fog routes disagree outdoors; glass/water NRD design,
  RELAX versus REBLUR and vendor audit gaps remain open. No fresh game or performance claim.
- CRLF retained for the existing docs and used for new docs. AGENTS was LF; its LF is retained.
  No build, GPU test, game, deploy, SDK-source copy, git write, or write outside this copy.

Validation: `python tests/source_checks.py` passes all 117 checks (no word exceptions needed).
Verbatim-move, archive, unchanged-section and two-place AGENTS checks pass; all 31 option, 21 material and
6 instance definitions match the header, managed values agree, and frame shifts/stage order match.
