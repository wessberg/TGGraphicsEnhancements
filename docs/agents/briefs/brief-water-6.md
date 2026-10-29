# Brief: the tracer's water in the game is a flat milky sheet; make it the game's water (PT-81, PT-80, PT-14) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/water-6.md`; write it early and after every step.

## The owner, 2026-10-08, watching the first runs with the tracer's own water

"The water looks off. The caustics look off. Many issues". And the standing rulings: "Water is the tracer's ... Full
Path Tracing means full path tracing"; "Do. not. fake. stuff. Real path traced caustics"; a mirror must show the
world as the camera sees it.

## Evidence (the MAIN tree, read only: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`)

- `artifacts\harness\20261008-083001\water-day\game-vs-traced.jpg`: the game has a dark green sea with wave shapes, foam
  in streaks and the bed showing at the shore; the traced picture (no denoiser) is ONE PALE BEIGE-WHITE SHEET from the
  shore to the horizon with a soft cloud pattern: no waves to see, no dark water, no sky in it, no bed. The shoreline
  is right. `docs/PT-REQUESTS.md` PT-81 (grep `### PT-81`) has what was ruled out: the foam texture the tracer reads
  holds the game's texels (mean 123, 114, 142, 89 of 255; red from 90 to 165: four different channels, none near 0).
- That run's `LogOutput.log` and `TGPathTracing.Native.log` (same folder): the `Water:` lines are the game's real
  numbers for each surface as the plugin sent them ("Ocean" 3 bands, rivers, a lake): foam amount and intensity,
  colours, the waves' sizes. Use THEM in your offline fixture; the first water jobs tested with numbers of their own
  and never saw the game's.
- `artifacts\harness\20261008-111121\lantern-lake\overview.jpg` (a lantern over a cave lake, no denoiser): with the
  Caustics row on and off the pictures are the same to the eye (luma 24.1 and 23.7); no rippling light on the lit rock
  over the water. And `mirror-water\mirror.jpg` there: the water in a mirror is at 0.62 of the brightness it should have.
- The offline test's own check "the caustics and their lookup stay affordable where 25 lamps light a lake (under 10 ms
  here; the hash took 33.7 ms)" fails with that same 33.7 ms in every whole run since the water merge.
- The jobs that built it: `docs/agents/water-4.md`, `docs/agents/merge-water.md` (in your copy): what the water is
  (waves as triangles, one boundary for glass and water), what they left open (the guides at a water pixel, the
  waterline, still water refitted every frame, 56 to 240 MB).

## What to do

1. **Reproduce the white sea offline** with the game's own numbers for "Ocean" from that run's log (a fixture beside
   `tests/water_surface_gpu.inl`): a sea to the horizon under a sky, a sloping bed at the shore, the foam texture's
   statistics as above. Until your fixture shows the pale sheet you have not found it.
2. **Find what makes it white and fix it at the cause**: how the simulation's foam amount, the foam texture's
   channels and the foam's tiling become foam (what the game's water shader does is the reference: learn it from
   HDRP's public documentation and behaviour and from the constants the plugin already reads; copy no shader text);
   the water's own colour (absorption and scattering in the game's units); the sky's reflection by Fresnel; what the
   automatic gain and its hold (`pt_common highlight_hold`) do to a bright surface. One cause at a time, with numbers.
3. **Caustics**: why the lantern's light off the lake does not show on the rock, and why the lookup takes 33.7 ms
   where 10 was the limit. Real traced caustics only: no pattern texture, no flat stand-in for water that has waves.
4. Say what a mirror must show of water and whether a reflection ray's water is the camera ray's (0.62).
5. For the coordinator's game run (`water-day@quick`, `lantern-lake@dots`, `mirror-water`): what each picture should show
   after your change and what will still differ from the game and why.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or the game folder. GPU tests only
  while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0; wait with one
  background script, never by polling; leave no waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <folder> -Throttle 8` before `cmake --build` (or a Ninja
  tree). `TGPT_TEST_STARTUP_SECONDS=900` for every GPU run.
- Another agent works beside you on the filters (`pt_atrous.hlsl`, `pt_nrd_prep.hlsl`, `pt_compose.hlsl`,
  `nrd_denoiser.cpp`): not yours. Yours: `pt_water_*.hlsl*`, `pt_interface.hlsli`, `scene_water.cpp`,
  `WaterCaustics.cs`, `WaterSurfaceRendering.cs`, the caustics passes; small, local edits in shared files.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Keep tokens down: grep and read ranges; never read `pt_common.hlsli`, `scene.cpp`, `tracer.cpp`,
  `tests/pathtracing_gpu_test.cpp` or `tests/source_checks.py` whole. The Bash tool eats backslashes: use Write/Edit.
- A struct field, export or option bit: say so with sizes (main is at ABI 0x00012600; the next free is 0x00012700).
- No mutation checks. The whole GPU test ONCE at the end, FAIL lines written down. Known on the base: the candle
  job's "skin 110 m from scene origin", the glass index check, three tinted-glass checks, the lake's 33.7 ms (yours to
  fix). A run with failed frames or `last error 0x887A0006` is void: run it again. Start it as soon as the code is final.
- RTXDI and NRD sources are never copied or quoted; nothing copied from Unity/HDRP or the game's shaders.
- Final message: five lines at most, plain words, pointing at the report: what is built, what passed, what is left.
