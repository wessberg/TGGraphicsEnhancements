# Brief: Opacity Micromaps where the tracer still tests cut-outs in the shader (PT-110) - one worker, measure first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/omm-1.md`; write it early, with "State when stopped" at its top after every step.

## The owner, 2026-10-10

"definitely investigate if there's anything we can do to any of our rendering to make it more compatible with
Shader Execution Reordering, Opacity Micromaps, and RTXDI." And, of such features, when one looks like a cost:
"it's either not due to that, or due to us not designing our tech with it in mind" (AGENTS.md, "Shader Execution
Reordering stays on; the tracer is designed for it": its last sentence names micromaps: read it).

## What is known (read first, in your copy: `docs/agents/compat-1.md`, a review: its section 3 whole, and of its
section 7 the lines about M0 to M11 and G3; grep those labels)

- No plant in the wind's pose has a micromap: 6.4 M triangles in a town are refitted every frame and every
  cut-out of them goes to the shader's alpha test (`scene.cpp` near line 1904; the DirectX specification allows
  micromaps on refitted geometry). And where a leaf's map exists, every covered piece is left "unknown" for
  every ray (`pt_omm_bake.hlsl` near line 265).
- Characters: in a tavern 49 instances have every triangle tested by the shader (547,286 triangles, 109,800 of
  them of submeshes that are no cut-out at all). Their structures are built for fast building.
- The game's own numbers (2026-10-10): a ray alone, nothing shaded, costs the bounced light's pass 28 ms in the
  tavern where the camera's own rays over the same world cost 2.5; a town's frame is 109 ms, a forest's more.
- The log today: "opacity micromaps on 175 meshes (1953 instances)" in a town; in a tavern "micromaps: meshes=46
  waiting=0 triangles=63022 instances=173 15MB".

## The job, in this order

1. **M0, the measure, FIRST (hand-over 1 within your first hour; plugin and C++ only, no shader change if it can
   be)**: a hidden `Advanced / Micromaps trial` (0 = as now; values you name) and a coverage count in the
   tracer's log once a scene: how many cut-out triangles the traced world holds, how many of them a micromap
   settles (opaque or transparent states against "unknown"), by kind (still meshes, plants in the wind's pose,
   characters and hair, cloth, decals), and how many candidate hits a frame still reach the shader's alpha test
   if that can be counted cheaply. With it, the bound runs as trial values: (a) the plants' cut-outs taken as
   solid for a step (the world running, so that they are refitted), (b) the characters' cut-outs taken as solid
   for a step, (c) micromaps off altogether for a step. These change the picture ON PURPOSE for the length of a
   measuring step: they are never a default, and the report says so. The recipe: scenes `metal`, `forest`,
   `tavern-room`; no denoiser; the stages' times per step say what each kind of cut-out costs.
2. **M3, an hour: per-submesh opaque flags for characters** (a submesh that is no cut-out is not sent to the
   shader's test): the same picture; behind nothing if it cannot change it (say why), else the trial's number.
3. **M5** (the review's: no `ALLOW_DISABLE_OMMS`, the map structures compacted): an hour or two, the same picture.
4. **M1: micromaps for the plants in the wind's pose** (C++ only by the review's reading, 1 to 2 days): the
   posed copies keep their mesh's map (the map hangs on a triangle's texture coordinates, not on where it
   stands); what a refit needs for it on this driver; behind the trial's number until the game has answered.
   Start it only when 1 to 3 are handed over, and stop at ITS hand-over.
5. Written up, not built: M2 (leaves' covered pieces opaque: needs the bake changed), M4 (hair), M7 (levels per
   triangle), G3 (characters' structures built for tracing): each with what M0's numbers say it is worth.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- Micromaps are never switched off by what ships; the trial's "off" and "solid" values exist to measure, for one
  step of one run. THE PICTURE DOES NOT CHANGE by anything that becomes a default.
- C++ and the plugin; a shader change only if a step needs it (say which and why first; then
  `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 3`, never `-First`, never a raised limit).
  Configure your build folder with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON` and copy the main tree's
  `build\native\generated\pathtracing\*.h` into your build folder's `generated\pathtracing\` right after
  configuring: then nothing compiles but C++.
- Offline: never the whole GPU test or ctest as a whole; a small check of your own only, under a 10 GB memory
  ceiling you never raise, in the test's `TGPT_TEST_NO_AGILITY=1` mode (which has no micromaps on Windows' own
  runtime? check, and say so: then the game is the test).
- Never read `scene.cpp`, `tracer.cpp`, `pt_common.hlsli`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed. Another worker is in `pt_gi.hlsl`, `pt_common.hlsli`'s reordering lines and
  `tracer.cpp`'s pass making: change what you must, move nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- `python tests/source_checks.py` passes; the plugin builds (0 errors, 0 warnings). Never describe anything with
  the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or "on trial".
- Final message: six lines at most: the coverage count's line and the trial's values, the recipe and what each
  step bounds, what M3 and M5 changed, where M1 stands, that it compiles and the checks pass, the report's path.
