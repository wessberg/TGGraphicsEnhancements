# Brief: performance back, the water's path (PT-99) - a fresh worker continues `perf1` in ITS copy

Copy (already holds the stopped worker's edits; work there): `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there.
READ FIRST, in the copy: `docs/agents/perf-1.md`: "State when stopped" (the top), then sections 1, 3, 5 and 6. Keep
writing into that same report (a new section "perf2", kept current).

## The owner, 2026-10-09

"one of them should focus on giving us performance back, there must be some extreme bottlenecks hanging around. I
know water is one of them, staying near water is like 1 fps". The ruling (AGENTS.md, "Speed is no longer left for
last"): first what costs nothing in the picture; what would change the picture is shown to the owner as a choice
with its numbers, never taken silently.

## What the game measured (the MAIN tree, read only: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\docs\agents\measure-run.md`)

- The beach: a frame 163 to 181 ms; with the walk through glass and the water's own path left out 29.9 ms. The
  reflections' pass 112 to 119 ms, and still 103 with its own ray left out: it is the water the camera sees.
- The tavern: 81 to 85 ms; 31 with no ray in the two big passes (the bounced light's pass 14.4 ms with no ray,
  4.0 at the beach); the bounce ray alone 28 ms (the camera's own rays over the same world, `prepare`: 2.5);
  shading what it met 22.

## The job, in this order

1. The stopped worker's "next step" 1 and 2: mend what its last edit (one option split in two) left unchecked
   (`python tests/source_checks.py`, the plugin's build), then a tree with `-DTGPT_REGROUP_FEED=ON` alone, its
   passes compiled, the DLL built. HAND-OVER 1 (written into the report at once): what the coordinator builds
   (the CMake line), the recipe, what you expect in ms.
2. **`reflected_lamps` in the water's path**: by the stopped worker's reading every turn of every water pixel's
   path loops over EVERY lamp of the cell's list and of the far list (468 to 976 lamps at the beach: a record read
   and a ray against a ball for each). Work out its share of the 110 ms from the code's counts, add a measuring
   switch that leaves it out if the second page has none, and BUILD THE EXACT THING: the same lamps found by a
   search structure over their balls (the lamps' grid walked along the ray, or a small tree made once a frame),
   so that no lamp a ray could meet is missed and none is asked that it cannot meet. The picture does not change.
   Behind its own compile switch and `Speed trial` value until the game has answered. The same question for the
   two big passes outside water (the tavern's 514 lamps): where else is every lamp of a list asked per ray?
3. The measuring build's second page compiled (one pass with `-D PT_SPEED_MEASURE` first), and the recipes
   `measure2` and `measure-water` ready: HAND-OVER 2. They must say: the water's path by its parts (the sun
   through the waves, the lamps' loop, the glint's shadow ray, the bed's shading, the landed light), the tavern's
   14 ms with no ray and its 28 ms for the bounce ray alone.
4. The next largest part of the water's path that can go with the same picture, by what you read (the report's
   section 5 has the candidates). What would change the picture: a choice with numbers, not built.
5. STOP after each hand-over's text is in the report only if you have nothing certain left; otherwise go on. Final
   message when item 4 is handed over or nothing certain is left.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- THE PICTURE DOES NOT CHANGE by anything you build: no fewer rays that change what it converges to, no shorter
  paths, no coarser grid, no distance past which light is dropped.
- Every cut behind a COMPILE switch (OFF by default: a default build's passes stay the base's to the byte) and a
  `Speed trial` value, as the stopped worker's are.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\<tree> -Throttle 2` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF`); never `-First`, never a raised limit; "the coordinator's build of the
  main tree goes first" means: wait. The main tree's passes are now built with `-DTGPT_RAIN_WETNESS=ON` from newer
  sources than this copy's base: do not copy its headers into your trees.
- Offline: never the whole GPU test or ctest as a whole. A test of your own only under a memory ceiling of 8 GB,
  never raised: the stopped worker found that the driver takes a test past it while making a new reordering pass.
  If that stops you, say so and leave the run to the coordinator; do not spend an hour on it.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's code: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: search, then read a range. Sources are CRLF: keep them so. After a scripted
  edit, check every hunk landed.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: each cut with its switches, what it should save and the run that shows it;
  the measuring recipes; the choices written up; what is left; that it compiles and the checks pass; the report's
  path.
