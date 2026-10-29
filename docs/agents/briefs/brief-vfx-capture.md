# Brief: the game's VFX Graph particles reach the tracer every frame (PT-71 item 7; design jobs 1 to 3) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("The tracer
fires its own camera rays, for every pixel", "Finish the migration first", "Do. not. fake. stuff.", "No fake lights",
"A light is the thing that emits it"). Report: `docs/agents/vfx-capture.md`; write it early (what you find before what
you build) and keep it current.

## Why

The owner: "particles need to be traced", and everything on screen rendered by the tracer ("Focus on finishing the
migration to our own renderer"). The design is `docs/agents/design-particles-fog.md` (read it whole). Its first job, the
inventory, ran in game on 2026-10-07 (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-191814`:
`effects-report.md`, `effects-summary.json`, each scenario's `1-game-effects/effects.json`; the tool:
`docs/agents/effects-inventory.md`). What it found: the game has NO ParticleSystem at all. Every effect is VFX Graph:
840 to 933 `VFXRenderer` components in each open-world scene. The families that matter first: candle flames
(`VFX_CandleFlame_Single`, `_Set_Candles_01`, `_02`, output "Shader Graph Quad - Unlit"; 1,295 components in the
census), fire (`VFX_Fire_EmberGen`: Fire, Smoke, Sparks, Glow, Debris, Distortion outputs), then runes, mist, moths.
Freezing the world freezes them. Unity's public API gives counts and bounds, not the particles. So the design's job 3,
"one VFX family adapter", is the main road: this job.

## What to do

1. **Find where a VFX Graph keeps its particles on the GPU** in this game's Unity (6000.0.64f1, VFX Graph 17): the
   per-system attribute buffers, the alive list or dead list, the indirect arguments, the layout of each attribute
   (position, size, scale, angle, color, alpha, texIndex for flipbooks, lifetime, age, velocity) as the compiled graph
   lays them out. Sources you may read: the game's managed assemblies (`C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA\Fall of Avalon_Data\Managed`,
   read-only; `ilspycmd` is installed): `UnityEngine.VFXModule.dll`, `Unity.VisualEffectGraph.Runtime.dll`; the graphs'
   compiled data in the game's asset bundles if a reader exists in the tree (`tools/research/unity_shader_blob.py` read
   shaders; say what you can read); Unity's public documentation. Learn from Unity's code, copy none of it. Write what
   you find into the report first, with the evidence, before building on it.
2. **A capture for one family, the candle flame**, end to end: each frame, after the game's VFX update and before the
   trace, every live particle of every candle-flame system in the loaded scenes (not only on screen): its position in
   the world, its size and orientation as the output draws it (a camera-facing quad: the design says what that means
   for a ray), its colour and alpha, its flipbook frame, its texture (the game's own, shared, no copy). Handed to the
   native side through a new export and struct, in a GPU buffer the tracer can read (D3D11 to D3D12 through the bridge
   as the other shared resources are: `docs/ARCHITECTURE.md`). No count limit that drops particles.
3. **Proof that the records are the game's particles**, which you cannot see yourself: a log line per frame with the
   counts per family, and a harness recipe step that draws the captured particles' positions over the game's own
   frame (or the flames' screen positions as numbers next to where the game's frame is bright), so ONE in-game run
   proves it. Say exactly what the coordinator runs and what proves success or failure.
4. **What a ray does with them** is the next job (the design's jobs 4 and 7: the flame as an emitting medium, the
   lights it replaces), NOT this one. Here the tracer only receives and keeps the records; a debug view may show them.

**Nothing is rendered twice** (the owner, 2026-10-07 22:00: "but we don't want to render the game twice?"): the game
simulates its effects once and, in the end state, draws none of them while tracing. So the capture reads the buffers
the simulation writes (the VFX update's compute dispatches), for every simulated system, on screen or not. The game's
draws may be watched to learn which buffers those are and to cross-check, never as the source the capture depends on.

If item 1 shows the buffers cannot be found or read: say so with the evidence, and propose the next best real route
(the design names one: the generated output geometry). Never a stand-in built from bounds or names.

## How it is judged

- The plugin compiles; `python tests/source_checks.py` passes; the native side builds and `tgpt_gpu_test` passes, with a
  check that records handed over arrive intact on the GPU (counts, a position, a colour), mutation-checked.
- In game: the coordinator's one run of the recipe you name (`candle` and `campfire` scenarios exist).

## Clean as you go (the owner, 2026-10-07 17:55)

What your work makes dead is deleted in this job; your report lists every workaround you meet around effects, glows
and flames (file and name), removed or not. The glow guessing in `SceneLights.cs` (`GatherGlows`, `EffectGlows`) stays
until the flame is traced as the emitter (a later job): list it.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling. Build into `%TEMP%\tgb\COPYNAME-...`; NRD's configure writes into the shared
  SDK folder, so build with `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files. If the bridge
  must change to share a buffer, say so and stop at a design: the bridge is shared by every mod.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Keep tokens down: grep and read ranges of big files (`SceneCapture.cs`, `SceneLights.cs`, `tracer.cpp`, `scene.cpp`,
  `pt_common.hlsli`, the GPU test, `tests/source_checks.py`, decompiled output), never whole. Stop when done.
- Option bits are used up (take none; add a field). Say if an export or struct changes (main's ABI is set by the
  coordinator). RTXDI and NRD sources are never copied or quoted. Unity's code: learn from it, copy nothing.
- Final message: five lines at most, pointing at the report.
