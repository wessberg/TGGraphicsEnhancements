# Path Tracing status

Reconciled 2026-11-10 from the final active 2026-10-12 handoff, the 2026-10-30 compatibility work and current source/configuration. Documentation review only; no new renderer build, deployment, GPU test or game run.

## Current boundary

PT is a Mono-only prototype, tabled since 2026-10-30. Maintained release work is version 1.2.4 of FG, BU, NR and the bridge. PT does not compile against Unity 6000.3: `SetupBeforeExecute` now takes `IUnsafeRenderGraphBuilder`, and net472 references conflict with netstandard 2.1 (CS1705). Referencing the game's facade alone lost `ReadOnlySpan`. Compiling against the game's own mscorlib was proposed, not completed. Use `Build.ps1 -Skip TGPathTracing` for maintained mods.

The old installed PT plugin last loaded with Mode Off. That is historical evidence, not proof of today's files or a working trace. Recheck branch/configuration/hashes before future runs.

## Implemented baseline

| Area | Implemented | Remaining boundary |
|---|---|---|
| Camera/geometry | Own rays; GameObject/Drake/Medusa/Entities/Leshy/VSP/terrain/Kandra; posed cloth/plants/characters | Completeness, streaming, capacity limits |
| HDRP handoff | Duplicate opaque/decal/shading suppressed; traced depth/motion/pyramid | Sky and transparent/VFX effects |
| Transport | RTXDI DI/GI, SER, shared textures/material programs, traced dielectrics/scattering | Atlas readers, fallback terms, cache limits, cleanup |
| Water/caustics | Displaced geometry, real waves, caustics before readers, grouped water, grid default 3 | Four water slots, seabed/edge defects, old path removal |
| Fog | Full-distance froxels, all emitters, multiple scattering, exact extinction, near-lamp residual | Secondary candidates, cache coverage, authored controls/fog-only lights |
| NRD | Full demodulated signals, real distances, shading normals, 2.5D motion, RELAX/confidence | Glass/water, SH detail, motion comparison |
| Mirrors | Primary surface replacement for static flat pure mirrors under NRD | Moving/curved/nested mirrors, materials, lamp noise |
| Acceleration | Plant micromaps (grass/leaves level 6), material-key GI SER, Mega Geometry trial 7 | Grouped plants unmerged, trial-transition bug |
| Stability | Stale plant ranges, decal BLAS addresses, emitter identities corrected | Regression suite red, second-load hang unresolved |

Mega Geometry's fresh CMake default is OFF; local cache is ON and managed trial default 7. Requirement: ON where supported. This fresh-build mismatch is a configuration gap, not a decision reversal. Renderer behaviour is unchanged by this cleanup.

## Final active handoff: 2026-10-12 12:30

Historical deployed native prefix `70948346AD5A`, plugin `1A6C8C6297DA`, ABI `0x00012700`. Compiled/staged/game native matched at 12:14. Later edits concerned tests/harness/docs. Compute fresh hashes before use.

- Caustic grid merged 11:42, default 3: town 5.25 -> 3.12 ms, forest 5.34 -> 2.75 ms. Frozen beach brightness 56.0/56.2/56.3. Old-path removal pending PT-114.
- Fog-fixture part A merged 12:20: reference includes black ground's 4% specular reflection. Executable compiled; source checks passed. Corrected 17 cases **not run in main**, PT-138.
- Plant crash fix survived 40 walks, PT-125. Decal fix has mutant evidence, no game scene, PT-134. Emitter fix still needs a moving scene, PT-135.
- First `nrd-motion` run invalid: motion blur on. Harness `disableMotionBlur: true` added; replacement RELAX/REBLUR motion comparison pending PT-127.
- Lamp mist visually accepted. Game load order changes strength: one town lamp 524,288 after beach, 0.125 when loaded first. Glow in 19/19 beach-first runs, absent in 47/47 others. Hold load order fixed; game setter unidentified, PT-147.

## Unmerged and missing work

| Work | Surviving evidence | State |
|---|---|---|
| Grouped plants, megageo7 | R20 summary; DLL `8A38D375B81B`; trials 9/10/11 | Copy/report missing from repo and documented temp location; no game run/merge |
| Frame late, framelate | R20 summary; native `A9752A40028D`, plugin `D4AE8EB2C4D2` | Copy/report missing; equivalence proof/game comparison never ran; unmerged |
| Fog fixture part A | Archived fog-3 report, merged source | Main GPU check pending |
| Undimmed sky-fog trial | Same report part B | Rejected approximation, unmerged |
| RR hair/reflection-distance proposal | Archived psr-2 report | Game comparison/merge not established |

Grouped plants target CPU planning: 16.6 vs 10.8 ms on a walk, 10,686 top-level instances. Proposed one bottom level/instance per cluster group and arena allocations. Offline images agreed for 9/10/11; final CPU/mutant checks unfinished. Trial predates caustic merges; recover/reimplement against current source.

PT-148: leaving 4/5/6 disables retained plant planning for the session. Fix before mixed-trial runs. Plain/7/plain stationary frames 77.46/74.59/78.39 ms already had inflated absolute CPU cost.

Frame-late forecast: 77 -> 67-69 ms, not 60, because measured D3D11/D3D12 work serializes. Adds one frame input latency (~70 ms), lacks RR, incomplete with FG, HDRP effects/UI one frame ahead. Keep hidden if recovered; validate before adoption.

## Performance and validation

Baseline `20261012-061203`: RTX 5080, running world, no denoiser, **stream off**, before later merges.

| Scene | FPS | Frame ms | Tracer ms |
|---|---:|---:|---:|
| Tavern | 21.5 | 46.5 | 35.7 |
| Town | 12.9 | 77.5 | 59.5 |
| Forest | 11.2 | 89.3 | 75.0 |
| Vista | 18.0 | 55.5 | 41.0 |
| Beach | 11.2 | 89.3 | 73.1 |

55 rendered FPS is a target, not achieved. Streamed baseline pending. Do not add later savings to old numbers as a new result. Use foreground A/B/A with identical streaming conditions.

Last broad PT GPU suite: 44 failures on tested and R19 reference builds, PT-113; never cleared. Offline fixtures do not run RR; RRG needs supported AMD. The 10-30 maintained-mod tests excluded PT.

Restart only after resumption: managed compatibility/ABI, isolated build/stage, fog corrections, NRD motion, PT-148/grouped plants, streamed speed/VRAM, then migration/vendor/appearance. Ask each time a measurement takes the screen.

[Requirements](PT-DESIGN.md) · [Context](PROJECT-CONTEXT.md) · [Issue register](PT-REQUESTS.md) · [Full local archive](DEVELOPMENT.md)
