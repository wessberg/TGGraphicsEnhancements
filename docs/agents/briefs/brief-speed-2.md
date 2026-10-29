# Brief: two speed regressions of 2026-10-08: the scene stage, and the opacity micromaps that are gone (PT-08, PT-73 c) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/speed-2.md`; write it early and after every step. The last speed job's report, with its method and its
script for reading stage times out of a native log: `docs/agents/speed.md` (`stages.py` is in
`...\scratchpad\speed\stages.py`; copy what you need into your own scratch folder, do not edit it there).

## Why (the player sees: frames a second)

The owner's target is 55 fps; the tracer is at 57 to 112 ms a frame. Two things got slower in the night of
2026-10-07 to 10-08 that have nothing to do with how the picture looks. Evidence: the native logs of three in-game
runs in the MAIN tree (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\<run>\TGPathTracing.Native.log`,
read only; the "Path tracing 10s" status lines; 1924x1084, RTX 5080, the GPU to itself in the first and the last):

| | `20261007-224654` (before) | `20261008-015158` | `20261008-034922` (now) |
|---|---|---|---|
| "scene" stage, tavern | 0.59 ms | about 9.5 ms | 8.9 to 9.3 ms |
| "scene" stage, forest (6782 instances) | 0.66 to 0.75 ms | | 9.6 to 10.0 ms |
| "scene" stage, vista (8002 instances) | 0.66 to 0.75 ms | | 12.5 to 12.6 ms |
| opacity micromaps (status line `micromaps:`) | meshes=456, 786,689 triangles, 151 MB | meshes=1, 2 triangles, 0 MB | meshes=1, 2 triangles, 0 MB |
| forest: prepare / direct / reflections | 8.0 to 8.8 / 8.7 to 11.5 / 21.9 to 24.0 ms | | 9.0 to 9.3 / 12.4 / 26.5 to 27.4 ms |

Between the first and the second run three jobs were merged: light and flicker (`docs/agents/light-flicker.md`: cutout
alpha is now read from the game's own texture with its real cutoff), memory and start-up
(`docs/agents/memory-startup.md`: 16 MB recyclable arenas, meshes retired, static BLAS compaction with size readback,
"shared scratch for sequential BLAS builds", a pass cache on disk) and decals (`docs/agents/merge-decals.md`,
`decals.md`: decal records and an ordered box tree a frame, mesh decals with their own acceleration structures).

## What to do

1. **The scene stage.** Find what the 9 to 14 ms are (the stage is timed on the GPU: `tracer.cpp`, where the stages'
   timestamps are taken, and `scene.cpp`'s per-frame work: the top-level structure, the moving things' and
   characters' bottom-level updates, plants and cloth in the wind's pose, the decals' buffers and tree, uploads).
   The coordinator's first suspect, NOT measured: bottom-level builds made one after another over one shared scratch
   buffer need a barrier between each, so 59 characters' updates no longer overlap on the GPU. Measure before you
   believe it (the offline GPU test prints "GPU time by stage"; its scenes with characters, plants and decals). Fix
   it without giving the memory back blindly: say what each choice costs in MB and in ms, and take the one that is
   right for a frame (scratch sized for what is built in one frame, not one buffer for all, is one way).
2. **The micromaps.** Find why no mesh gets an opacity micromap any more (`pt_omm_bake.hlsl`, where `scene.cpp`
   queues the bake, what the bake reads as a cutout's alpha and cutoff now that the tracer reads the game's own
   textures). Bring them back, baked from exactly what a ray's alpha test reads (the same texture, the same cutoff,
   the same coordinates), so that a micromap never changes the picture: it only answers early what the test would
   answer. Say what they cost in MB and how long the bake takes after a scene loads, and whether the memory job's
   scene clear frees them.
   The GPU test has a check for it ("every mesh with alpha-tested instances has opacity micromaps"); it failed in the
   speed worker's whole run of 01:50. Make it pass for the right reason.
3. Whatever else the stage lines show got slower between the first and the last run and is not a change of the
   picture: name it with its number. Do NOT touch the scattering walk (another agent is on who walks) or the
   lantern's brightening (another agent) or water.

## How it is judged

- Offline: the GPU test's stage times before (your base) and after, on the same scenes, alone on the GPU or said
  otherwise; the micromap check; the picture unchanged (the test's pictures of base and yours, or its existing checks).
- The whole GPU test ONCE at the end, FAIL lines written down. A run with failed frames or `last error 0x887A0006`
  is void (GPU overloaded): run it again. A run that fails only at "the passes were made" runs once more.
- In game the coordinator runs the five scenes of `quick` on a free GPU: say what the scene stage and the forest's
  frame should become.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or the game folder. GPU tests only
  while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0; wait with one
  background script, never by polling; leave no waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...` with
  `-DTGPT_WITH_NRD=OFF`.
- Compile the shader passes side by side before `cmake --build` (a full build is an hour otherwise):
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\Build-PtShaders.ps1 -BuildDir <your build folder> -Throttle 8`.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Keep tokens down: grep and read ranges; never read `tests/pathtracing_gpu_test.cpp`, `pt_common.hlsli`, `scene.cpp`,
  `tracer.cpp` or `tests/source_checks.py` whole. The Bash tool eats backslashes: write files with Write/Edit.
- Small and local edits in `scene.cpp` and `tracer.cpp`: three other jobs are merged into them. No export, struct or
  option bit should be needed; say so if one is (the next free ABI number is 0x00012400; the coordinator settles it).
- No limits that change the picture, no stand-ins. No mutation checks.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
