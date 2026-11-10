# Backlog

Updated 2026-11-10. Source preparation is complete; publication is pending. Path Tracing remains tabled.

## Public source

- Done: exact history preserved locally; concise engineering notes public.
- Done: PT baseline, pending validation and unavailable worker changes reconciled.
- Done: scratch trees, logs, prompts and generated images excluded from public source.
- Done: superseded SDKs/downloads and two regenerable build trees retired privately.
- Passed: archive integrity, 121 source checks, eight tool tests and documentation links; the standalone snapshot passes without local SDKs/private history.
- Publishing and Git history rewriting are separate. Existing commits retain earlier private material.

## Maintained mods

FG, BU, NR and the bridge are version 1.2.4 in this tree. Six packages were tested on both game branches on
2026-10-30 and prepared for upload. Publication since then has not been verified; the last documented public
release is 1.2.3. Unity 6000.3 CustomPass invocation and DLSS initial-size recreation fixes were tested on both
branches. The DLSS FG Quality/Performance row passed the recorded Mono appearance review.

- Defender: new FG DLL clean in three scans on 10-30. The 1.2.3 false-positive resolution/Microsoft submission
  remain unverified; error 225 needs clearer advice.
- Unverified: FSR 4 on supported AMD, Anti-Lag 2, Intel-specific XeLL/XeSS multipliers, community NR on
  RTX 20/30/40-series, Windows 10 Agility. RTX 5080 results do not establish these.
- NR motion instability and compatibility: [DLSS5-FEEDBACK.md](docs/DLSS5-FEEDBACK.md).
- NR mutation tools now use DLSS SDK 310.9.16 (NR-46). GPU mutation runs were not performed during source preparation.
- Deferred: bridge-owned vendor-runtime search list; shared FG/BU motion guides; fast-pan hitches; stale No FPS
  gain alerts; divided VSync rate; cross-adapter timing; exclusive-fullscreen transitions; extension ABI and
  IL2CPP diagnostics. Keep the existing safe borderless behaviour.

## Path Tracing: tabled

Last active rendering work: 2026-10-12. Unity 6000.3 introduced managed compilation errors on 10-30 and PT was
tabled. This cleanup does not resume it. [PT-STATE.md](docs/PT-STATE.md) is the current handoff.
[PT-REQUESTS.md](docs/PT-REQUESTS.md) indexes all historical PT IDs once; IDs are never reused.

Resume order when renderer development is requested:

1. Fix Unity managed compatibility (PT-150); recover a buildable baseline without replacing maintained-mod files.
2. Verify merged fog-fixture corrections PT-138, then NRD filter motion comparison PT-127, motion blur off.
3. Align the fresh-build default PT-149 and fix Mega Geometry trial transitions PT-148; recover/reimplement missing grouped-plant work PT-109.
4. Re-establish streamed frame-time/VRAM baselines PT-99/PT-87; keep SER/Mega Geometry on where supported.
5. Recover/evaluate unmerged frame-late proposal PT-145 with its latency, RR/FG and effects limitations.
6. Finish migration/vendor audit PT-60/PT-71/PT-112/PT-127: sky, VFX/transparency, NRD glass/water, original
   textures/materials and cap removal. Remove the old caustic path PT-114.
7. Appearance: seabed PT-81; eyes PT-119; bottles PT-120; smoke/heat haze PT-121/PT-144; detail/contact loss
   PT-122/PT-123; streaming PT-124; rain fog PT-126; secondary fog/cache PT-132/PT-137/PT-138; mirror lamps
   PT-139; steady flame reach PT-146; lamp load-order cause PT-147.

Do not resume reproducing PT-129 without a new request. PT-125's plant fix survived 40 recorded walks;
PT-134/PT-135 decal/emitter fixes have narrower validation. PT-113's 44 historical GPU-test failures were not
cleared. A successful new build must not erase these limits.

[Guidelines](AGENTS.md) · [Context](docs/PROJECT-CONTEXT.md) · [Dependencies](docs/BUILD-DEPENDENCIES.md) ·
[Archive](docs/DEVELOPMENT.md) · [Sessions](ROUNDS.md)
