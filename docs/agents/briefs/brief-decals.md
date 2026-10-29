# Brief: decals seen by every ray (PT-63 part 5, PT-71 item 2) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("The tracer
fires its own camera rays, for every pixel", "Finish the migration first", "Remove the workarounds as we go", "The
owner's word on the shortcuts list", "A mirror must show the world as the camera sees it"). Report:
`docs/agents/decals.md`; write it early (the design before the code) and keep it current.

## Why

Since 2026-10-07 every pixel's surface is the tracer's own camera ray's hit (`pt_primary.hlsli`; `docs/agents/hair-6.md`).
HDRP's decals (blood, dirt, moss, wet patches, glowing runes, road markings) exist only in HDRP's DBuffer, blended into
its G-buffer; a ray's hit never sees them. So with our own camera rays they are gone from the picture, and they were
always missing from mirrors and bounced light. The owner, told rays don't see decals: "they should! Of course they
should." The world agent wrote the design (`docs/agents/world.md` section 5: read it; it is yours to build or to
improve with reasons) and made the plugin count decals each scan.

## What to do

1. **Capture every decal the game draws**: HDRP `DecalProjector`s (box, pivot, transform, material maps and values,
   what it affects, blend sources, emission, fades, draw order, layer mask) and the game's own decal classes
   (`StaticDecalsCuller`, `DecalController`: the game's assemblies are in the game folder's
   `Fall of Avalon_Data\Managed`, read-only; `ilspycmd` is installed) and any decal shader of the game's own (read it as
   the world agent read the vegetation shaders: from the compiled shader, `docs/PT-DETAILS.md` says how earlier decodes
   were done). Decal meshes (geometry HDRP draws into the DBuffer) too, or say with numbers why they come later.
2. **Find the decals over a hit** with no list length that caps them (the design's box acceleration structure, or a
   grid if the counts say so: measure, say which and why).
3. **Blend as HDRP does**, in draw order, into the hit's material in the one place a hit's material is made
   (`pt_common.hlsli`), so camera rays, mirrors, bounces and shadow rays all get the same surface. Colour, normal, mask
   (metal, occlusion, smoothness), emission; a glowing decal lights the world as a glowing surface does.
4. **Its textures are the game's own** (the shared textures, PT-45/46): no copies.
5. Report the counts the plugin logs, the cost per frame, and what is still not seen.

Not yours, other workers run in parallel on them: what a hit reads of an ordinary material (the materials worker owns
`hit_material`'s reading of the game's shaders: add your blend as one call after it, in one place, and keep the rest of
that function untouched), water, glass and eyes, moving objects (`SceneCapture.cs`'s transform code), particles.

## How it is judged

- Offline (`tests/pathtracing_gpu_test.cpp`), with Denoising None: a decal on the floor shows in the camera's view and in
  a mirror, in its colour, normal and smoothness, and is gone with the decal off; two overlapping decals blend in draw
  order; a glowing decal lights a wall. Each check mutation-checked (a mutant's run ends at its FAIL line).
  `python tests/source_checks.py` passes; the plugin compiles.
- In game the coordinator runs ONE scene: name a scenario of `tests/ingame/scenarios.json` that has decals (or say what
  save to ask the owner for) and what its picture and log must show (the yardstick's colour share on the decal's
  pixels).

## Clean as you go (the owner, 2026-10-07 17:55)

"Also remove all the 'hacks' and workarounds as we go ... So we have a clean base to work from." What your work makes
dead is deleted in this job with its settings, tests and docs. Your report lists every workaround you met (file and
name), removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling. Build into `%TEMP%\tgb\COPYNAME-...`; NRD's configure writes into the shared
  SDK folder, so build with `-DTGPT_WITH_NRD=OFF` and judge with Denoising None.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Keep tokens down: grep and read ranges of big files (`SceneCapture.cs`, `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`,
  the GPU test, `tests/source_checks.py`), never whole. Stop when done; ideas go in the report.
- Bits: option bits are used up (take none; add a field). Material bit 33554432 is yours if needed. Say if an export
  or struct changes (the coordinator sets the ABI number; main is 0x00012000).
- RTXDI and NRD sources are never copied or quoted. Unity's HDRP source: learn from it, copy nothing.
- Final message: five lines at most, pointing at the report.
