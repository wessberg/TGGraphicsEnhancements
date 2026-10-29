# Brief: performance back, water first (PT-99) - one worker, hand over early and often, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/perf-1.md`; write it early and after every step.

## The owner, 2026-10-09

19:16: "one of them should focus on giving us performance back, there must be some extreme bottlenecks hanging
around. I know water is one of them, staying near water is like 1 fps". 10:45: "right now we're at like 8 FPS. It
can barely render". The ruling (AGENTS.md, "Speed is no longer left for last"): first what costs nothing in the
picture; what would change the picture is shown to the owner as a choice with its numbers, never taken silently.

## What is measured (read first, in your copy: `docs/PT-REQUESTS.md` PT-99, every note of 2026-10-09: grep the
heading; `docs/agents/speed-1.md`, the worker before you: its sections 1, 2, 3 and 6)

- The tracer's own stage times (its line "Path tracing 10s" in `TGPathTracing.Native.log`), the card free, the tracer
  at 1924 x 1084, one ray a pixel, DirectX Raytracing 1.2 with Shader Execution Reordering:

  | Scene | a frame's GPU time | bounced light | reflections | direct light | caustics | prepare |
  |---|---|---|---|---|---|---|
  | the tavern (`tavern-room`) | 82 ms | 52.5 | 11.6 | 4.75 | | |
  | a guard in a town (`metal`) | 108 to 115 | 61 to 65 | 28 to 31 | 6.9 | 4.8 | 4.2 |
  | the beach (`water-day`), the Caustics row on | 150 to 190 | | | | | |
  | the beach, the Caustics row off | 58 | | | | | |

  So AT THE WATER THE CAUSTICS PASS IS ABOUT 90 ms OF THE FRAME. That is the first bottleneck to open.
- "1 fps near water" in the owner's own session was two things at once: this cost, and the card's video memory
  overrun (a traced frame took 1,490 ms over the budget). The memory is another worker's job (`memory1`): not yours,
  but say what of the water's memory you meet (the caustics' buffers were 215 MB, and a change that made them 1.3 GB
  was taken out again).
- Code that is switched off still costs: PT-85's lamp trial, compiled out, gave the tavern back 9 ms of bounced
  light; 21 ms more were lost on 2026-10-09 04:50 or after and are not explained (the tavern's bounced light was
  about 31 ms on the builds of 2026-10-08).
- The worker before you built a MEASURING BUILD (CMake `TGPT_SPEED_MEASURE` ON: 11 switches in the hidden
  `Advanced / Speed trial`, 16 and up, each leaves one part of the work out). The coordinator runs it in the game
  now, in the tavern and at the beach (recipe `measure`). ITS RESULT WILL BE IN THE MAIN TREE as
  `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\docs\agents\measure-run.md` (read only). Wait for that file with
  ONE background script while you read the code; do not guess where the time goes once it is there.
- `Speed trial` 1 (a shadow ray asks which water it starts in only under the water's highest point) never came on:
  the tracer's log, "NOT skipping the water question: some water of the scene stands over the highest point the
  plugin gave". The plugin's highest point of the waters is too low or misses a water.

## The job, in this order

1. **The caustics pass** (`pt_caustics.hlsl`, its part of `tracer.cpp`, the sun's grid, the landed light's store:
   grep; `docs/agents/water-8.md`, `water-9.md` for how it is built): what it does every frame, for how many
   triangles, rays and texels, over what area, whether any water is in view or not; where its 90 ms go by its own
   parts (write the table). Then what brings the time down WITH THE SAME PICTURE: work it repeats every frame for
   water that did not move relative to the sun (is the sea's surface the same wave field each frame? then what can
   be kept), work for water nobody can see or be lit by, a store filled where nothing reads it, its ray count
   against what the grid needs. A budget may cost noise or speed, never light: what would change what the picture
   converges to is a choice you write up with its numbers, not a thing you build.
2. **Hand over the first cut within your first hour**: the smallest change that could show the direction, behind a
   hidden switch (default off; a spare field, never an option bit: all 32 are used; the glass and water trials sit
   in `TGPT_Frame.cameraPosition`'s w, the relief and speed trials in `prevCameraPosition`'s w), with the recipe
   for one run of `water-day` (off and on, no denoiser, 16 frames each; the tracer's stage line is the measure) and
   what you expect. The coordinator runs it in the game at once. Then the next cut, as its own switch.
3. **The water question**: make `Speed trial` 1 able to come on (the plugin's highest point of every water the
   tracer holds: where it is computed, what it misses), so that one run measures it.
4. **From the measuring run** (when `measure-run.md` is there): the two largest parts of the tavern's bounced light
   and of the beach's frame; for each, what can go with the same picture. Write the others up as choices.
5. STOP at each hand-over's end; your final message when you have nothing certain left to build.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- THE PICTURE DOES NOT CHANGE by anything you build: no fewer rays that change what it converges to, no shorter
  paths, no coarser grid, no distance past which light is dropped. Such things are written up as choices with
  what they would save and what they would cost in the picture.
- A switch that adds more than a few lines to `pt_gi`, `pt_specular`, `pt_prepare` or the walk goes behind a COMPILE
  switch (AGENTS.md, "Code that is switched off still costs frame time"); `pt_caustics` is yours to change.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 2` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF`); never `-First`, never a raised limit; a line "the coordinator's build of
  the main tree goes first" means: wait, nothing is wrong. To save the first full compile: when the main tree
  builds nothing (`%TEMP%\tgb\BUILD-FIRST` gone), COPY its `build\native\generated\pathtracing\*.h` into your build
  folder's `generated\pathtracing\` right after configuring; then only the passes your change touches compile.
- Offline: never the whole GPU test or ctest as a whole ("full test suite never in subagents"). The sea fixture's
  own parts (`TGPT_TEST_SEA_PART`, `..._ONLY` modes) for a pass's GPU time before and after, through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs; give every test run a memory
  ceiling of 8 GB (the worker before you had one grow to 33 GB) and wait for the GPU with ONE background script.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's code: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit, check
  every hunk landed. Two other workers are in the tree's copies: memory (`tracer.cpp`, `scene.cpp`: allocations)
  and the rain's wetness (`pt_materials.hlsli`, `pt_common.hlsli`: materials): change what you must, move nothing
  else. The lamp trials' removal is another worker's, half done: leave the lamps' code alone.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Build the plugin too (0 errors, 0 warnings). `python tests/source_checks.py` passes.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: each cut with its switch, what it saves (measured offline or expected) and
  the run that shows it; the choices written up; what is left; that it compiles and the checks pass; the report's
  path.
