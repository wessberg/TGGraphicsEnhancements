# Brief: the inventory of the game's particles, effects and fog, logged by the harness (PT-71 items 7 and 8, job 1) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("The tracer
fires its own camera rays, for every pixel", "Finish the migration first", "Do. not. fake. stuff.", "Keep the game's
fog", "No fake lights"). Report: `docs/agents/effects-inventory.md`; write it early and keep it current.

## Why

The owner wants everything on screen rendered by the tracer ("Focus on finishing the migration to our own renderer").
Particles, transparents and the fog are still HDRP's (`docs/PT-REQUESTS.md` PT-71 items 7 and 8). The design for
tracing them is `docs/agents/design-particles-fog.md`: read it whole, it is yours to serve. Its first job is this one:
nobody knows yet what the game really has. "FoA uses some combination of these; the scanner alone proves none of them."
Every later job (particle capture, the VFX Graph adapter, fog density, flames as emitters) is chosen from this
inventory. No tracing is built here.

## What to do

1. **An effects inventory in the in-game harness** (`src/TGPathTracing.Harness/`: a new file beside `Audit.cs`, switched
   on per step like the state audit: grep "audit" in `Runner.cs` and `Request.cs`; a recipe `effects` in
   `tests/ingame/scenarios.json` that starts and ends with Mode Off, as a source check demands). In one run it writes,
   per scenario, a machine-readable file (JSON) and a short readable table into the run's folder. What it records is
   the design's list (its section after "Extend the existing harness audit"), in short:
   - every `ParticleSystem` with its `ParticleSystemRenderer`, every `VisualEffect` with its `VFXRenderer`, every
     `TrailRenderer` and `LineRenderer`, and every transparent or unlit `MeshRenderer` the tracer's capture rejects
     (`SceneCapture.MaterialIndex`: say why each was rejected): identity (scene, hierarchy path, instance ID), active
     and enabled state, renderer and output type, render mode, shader name and its passes, material name, blend, depth
     and cull settings, render queue, the textures with their sizes, keywords;
   - live count, capacity, bounds, simulation space, culling mode, whether it is playing, current size and colour
     ranges, velocity and lifetime ranges, flipbook settings, custom vertex streams, trails, mesh choices; for a VFX
     Graph each system and output on its own (what Unity's public API gives: say what it does not give);
   - lights attached to an effect and particle light modules; lights near it listed apart (near is evidence, not
     ownership), with units, intensity, colour, range, cookies, volumetric settings; which of them the tracer traces
     today and which it drops, and why (`SceneLights`);
   - the camera's blended `Fog` state and every `LocalVolumetricFog` in the loaded scenes: transform, bounds, mask
     texture or material (type, size, format), density (mean free path), albedo, priority, blend mode, fades,
     animation; every Volume that overrides `Fog`;
   - "unknown" where a value cannot be read, never a plausible default.
   Systems outside the camera's view are listed too (the loaded scenes, with their distance from the camera).
2. **Does freezing freeze them?** The harness freezes the world for a step. Log, for three frames a second apart
   inside a frozen step, whether particle state (count, the first particles' positions) and the shaders' time still
   change (PT-06 says a candle's patch changes in a frozen world).
3. **A summary the next job can act on**, written by `tools/harness/` (a small Python script, with a test like
   `tests/harness_analyze_test.py`): per scenario and over all, the effect families (renderer type x shader), how many
   systems and live particles each has, which have lights, which the tracer handles in any way today.
4. Nothing that changes the picture or the game: the inventory only reads. No `Add<T>(true)` on a Volume.

## How it is judged

- The plugin compiles for the Mono game (`dotnet build src/TGPathTracing.Harness/...`, as the tree builds it: see
  `tools/harness/Run-PtHarness.ps1`, which builds and installs the runner). The Python summary has a test that fails
  when it is broken on purpose. `python tests/source_checks.py` passes.
- In game the coordinator runs it ONCE: `Run-PtHarness.ps1 -Scenario candle,campfire,fog,night,tavern-room -Recipe
  effects` (say if the command should differ). Say what each file must hold and how long the run takes. You cannot run
  the game: write the code so a wrong guess about a type or a field shows as a logged "unknown: <why>", not a crash
  (reflection with null checks, a try around each system; the game's own types are in the game folder's
  `Fall of Avalon_Data\Managed`, read-only: `ilspycmd` is installed if you need a type's members).

## Clean as you go (the owner, 2026-10-07 17:55)

"Also remove all the 'hacks' and workarounds as we go ... So we have a clean base to work from." This job removes
nothing (it only reads), but your report lists every workaround you meet around particles, glows, fog and lights
(file and name), for the jobs that follow.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. No GPU test is needed for this job.
  Build into your copy and `%TEMP%\tgb\COPYNAME-...`, never the main tree's `artifacts/`.
- Another session of the owner's builds a DLSS 5 mod in the main tree and also edits the harness plugin
  (`src/TGPathTracing.Harness/Neural.cs`, parts of `Runner.cs`, `Request.cs`, `tools/harness/neural_*.py`): keep your
  edits to those shared files small and in one place each; put everything else in new files. Do not touch
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`, `CMakeLists.txt`.
- Keep tokens down: grep and read ranges of big files (`SceneCapture.cs`, `SceneLights.cs`, `Runner.cs`,
  `tests/source_checks.py`), never whole. Stop when done; ideas go in the report.
- No ABI or native change is expected; say so if you need one.
- RTXDI and NRD sources are never copied or quoted. Unity's HDRP source: learn from it, copy nothing.
- Final message: five lines at most, pointing at the report.
