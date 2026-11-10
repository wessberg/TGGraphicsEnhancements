# Repository guidelines

## Start here

1. Read `BACKLOG.md` and `docs/PT-STATE.md` for current priorities and Path Tracing status.
2. Read the relevant part of `docs/PROJECT-CONTEXT.md`, including **DEAD ENDS**, before changing code.
3. Read the newest `ROUNDS.md` entry and its linked session note.

PT is tabled. Documentation work does not resume rendering work, deployment or GPU tests. Maintained mods
target Unity 6000.3.21f1/HDRP 17 on D3D11. Mono uses BepInEx 5; the normal version uses BepInEx 6.
All mods share one D3D12 device through TG Graphics Bridge.

Write decisions, evidence, limitations and next actions in plain engineering language. Keep conversations,
prompts and execution transcripts out of public notes. Store project knowledge here, not private tool memory.
Exact earlier history is retained locally; `docs/DEVELOPMENT.md` describes access and backup.

## Tracking and collaboration

- `BACKLOG.md`: priorities/releases. `docs/PT-REQUESTS.md`: stable IDs, states and next actions.
- `docs/PT-STATE.md`: implemented baseline, pending validation and unmerged work.
- `docs/PROJECT-CONTEXT.md`: integration facts, measurements and failed approaches.
- `ROUNDS.md` and `docs/rounds/`: concise session records ending with a starter prompt.
- Update these at handoff. Record new defects immediately; preserve IDs and link related issues.
- Re-read shared files before editing; preserve unrelated work. Commit, push, publish and visibility changes
  require a request. Additional agents require an explicit request, isolated trees and narrow tasks. Workers
  never deploy, start games or run the full GPU suite. Use a fresh worker for a new substantial task.

## Path Tracing

- Full tracing only, Off/On. Own camera rays for every pixel/surface: hair, eyes, water, glass, particles,
  off-screen emitters. HDRP retains post-processing, upscaling and HUD. Finish migration before old-route
  comparisons. Remove obsolete workarounds/settings/tests as replacements land (PT-60/PT-71/PT-112).
- Physical transport: no screen-space substitutes for scattering, reflections, haze or caustics. Trace real
  waves. Preserve authored materials/fog. Camera and secondary rays see matching geometry, original-resolution
  game textures, materials, decals and motion. No distance/layer/object/light cap may change convergence.
- Keep SER and RTX Mega Geometry enabled where supported. Plain paths are hardware fallbacks or measuring
  references. Diagnose slower accelerated paths. Opacity micromaps conservatively bound every alpha reader.
  Character micromaps were removed after measured lack of benefit.
- Speed/VRAM come first alongside correctness. Optimize without changing the picture first; show images and
  measured costs before adopting changes to sampling, resolution, path length or responsiveness.
- Trace real emitters, including crystals/magic/fireflies. Reject artist fill/bounce light, not unnamed real
  sources. Classify by attachment/content; use physical emitter size for shadows and highlights.
- Every emitter lights fog with multiple scattering. Preserve accepted lamp glow and authored fog-only light.
  Visible sun/moon direction, physical distant haze and cloud shadows are requirements. Judge water by physical
  consistency and appearance, not rasterized colour parity. Grass shadows are soft and partial.
- Follow vendor guides/samples for RTXDI, NRD, RR, RRG, SER, micromaps and Mega Geometry. Read relevant local
  guides fully before changes. Track departures; check our inputs before rejecting a vendor method.
- Denoisers: None/NRD/Ray Regeneration/Ray Reconstruction, filtered by runtime support. Prefer RR, then RRG,
  then NRD. RR works alongside NR. NRD gets all demodulated light, real distances, shading normals and correct
  motion. RELAX selected; motion comparison pending. No extra temporal filter over Neural Rendering.
- Default full-resolution rays, Indirect Color 1, Reflection Boost 0.1. Boost includes armour/metal more than
  stone, exempts skin/foliage. See `docs/PT-DESIGN.md` for the complete settled design.

## Settings and releases

- Query runtime capabilities, not GPU tables; clamp on backend switches. Match Video/Mod Manager rows exactly.
  One selector per concept. Grey unavailable rows with reasons; amber alerts on affected rows. Apply documented
  safe conflict fixes to game settings; no player chores or separate frame cap. Show only chosen-upscaler
  controls; no FSR version row; STP keeps render scale.
- Player text: normal version / Mono version, established TG names/GUIDs, short tooltips, first-person Nexus
  BBCode. Use prototype or in testing for unfinished work; respect `tests/source_checks.py` wording checks.
- One version across mods/Nexus. Bridge changes require a bump; managed API only grows. Confirm package-content
  changes. Preserve BepInEx layout, package manifest, Vortex override and tested release backups.
- No SDKs, restricted source, game assemblies, runtime DLLs or private history in public source. Preserve notices
  and attribution. No AMD runtime disassembly or copying unlicensed projects. See `LICENSES/THIRD_PARTY.md`.

## Validation

- Read `README.md`, `docs/TESTING.md` and `docs/BUILD-DEPENDENCIES.md`. PT's Unity managed build is blocked:
  `Build.ps1 -Skip TGPathTracing`. Full native builds still include PT SDK/shader dependencies.
- Build, install, test: fixtures load `artifacts/Native`. Hash compiled/staged/installed copies. Replace Vortex
  symlinks rather than writing through them. Check BepInEx `Loading [` lines before blaming an installed mod.
- No games/GPU tests during documentation cleanup. Future authorized runs use the harness GPU gate. No offline
  GPU tests beside another open game; stop existing tests if a game opens. Unknown readings are not idle.
  Keep streaming on for comparable timings; encoding alone does not block the gate.
- Ask each time a measurement run takes the screen. One scene/defect per short iteration; game evidence first.
  Broad regression/mutation checks follow appearance acceptance or explicit request, never in workers.
- Compare undenoised/NRD before RR. Motion blur off for motion comparisons; verify the flag. Native-resolution
  images with paths for appearance decisions. Check exposure/gain when light changes.
- Distinguish built, offline-checked, seen in game and visually accepted. Synthetic checks do not prove other
  hardware or RR correctness. Offline PT fixtures do not run RR; RRG needs supported AMD hardware.
- Run `python tests/source_checks.py` before handoff and relevant CPU checks. Documentation validation must not
  rebuild shaders, deploy, overwrite release packages or start games.
