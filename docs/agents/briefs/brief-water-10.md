# Brief: water: foam where a wave meets the shore, lamps and fire lighting the water's body, light that does not depend on the screen (PT-81, PT-42, PT-94, PT-90) - one worker, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/water-10.md`; write it early and after every step.

## The owner, 2026-10-09, what the water MUST do

"and are we reflecting other stuff than just the sun in water now? we must. caustics and refraction must work too.
foaminess must be realistic. the wet [surfaces] where the wave reached against the shore and so on". Earlier:
"Waves hitting the shore looks silly because they don't blend at all, and there's no foam" (PT-42); "our own foam"
(no port of Unity's); "Do. not. fake. stuff. Real path traced caustics".

## Where it stands (read first, in your copy: `docs/agents/water-9.md` "Read this first" and sections 4 and 8; `docs/agents/water-8.md` section 7; `docs/agents/water-7.md` section 4 for the foam; `docs/PT-REQUESTS.md` PT-81's notes of 2026-10-09, PT-90, PT-94, PT-42: grep the headings)

- The surface mirrors what its reflection ray meets and bends the view through it. The light INSIDE the water's
  body is the sun's alone (`pt_water_sun.hlsli`, the sun's grid): no lamp, no fire lights it.
- The sun's caustics are traced through the real waves (`pt_caustics.hlsl`). Two trials of the job before are in
  your base, both OFF by default (`Advanced / Caustics land split light`, `Caustics light keeps its direction`):
  leave their switches as they are; the game has not answered yet.
- Foam: our own formula (`pt_water_foam.hlsli`), thin bands on open water; none where a wave meets the shore.
- The wet shore (`pt_wet.hlsl`, PT-52) is built; it is not yours.
- PT-94: the caustics pass's `in_view` writes no landed triangle outside 1.5 times the view, but paths under water
  read the landed light there too (16 % short in the job before's narrow-view fixture).
- PT-90: a candle mirrored in still water is a burnt-white blob far brighter than its flame seen directly (a guess:
  `pt_common.hlsli` `reflected_lamps` shows a reflection ray the lamp as a ball of its flame's size carrying the
  lamp's whole strength).

## The job, in this order; each behind its own hidden switch (default off: the picture as before; spare or reserved fields, never an option bit: all 32 are used)

1. **Foam at the shore.** What makes foam where a wave runs up a beach or breaks on a rock, from what the tracer
   already knows without anything screen-space: the water's depth under the surface point (the bed is in the
   traced scene: a ray down), the waves' own state there (steepness, the run-up as the level rises and falls over
   a point of the bed: `pt_wet.hlsl` keeps where the water stood), time. Our own formula, said in the report with
   its reasons; the game's foam textures may be read for its look (they are the game's own, shared, never copied).
   Foam is a surface state every ray sees (a mirror shows it), white and rough where it is thick, breaking into
   the existing bands farther out with no seam.
2. **Lamps and fire light the water's body.** A point where a path scatters inside the water takes its lamps'
   light as it takes the sun's: through the real waves where the light comes from above the surface (bent, less
   what the surface mirrors away), straight where the lamp is under water; by the light lists the tracer already
   has, no fixed count of lamps. Its own small offline check against a straight reference (flat water, one lamp
   over it: the brightness inside the water against the worked-out value).
3. **PT-94**: the sun's and the lamps' landed light is there wherever a path under water asks for it, on screen or
   not, with no limit that changes the picture (say what it costs; a budget may cost noise or speed, never light).
4. **PT-90**: find out by reading and by a small offline check why a candle mirrored in flat water is so much
   brighter than its flame seen directly, and what the truthful mirror image of a lamp is ("A mirror must show the
   world as the camera sees it"): fix it if the fix is small and sure, else write the design.
5. STOP at the hand-over: one recipe for `tests/ingame/scenarios.json` (`water-day`: no denoiser and NRD, the foam
   switch off and on; `lantern-lake` or `water-torch`: the lamps' switch off and on; 16 frames; plus an `anim`
   step if the harness has one for moving water: grep the recipes), the switches' names, what each run should show.

## Rules

- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small checks in
  their `..._ONLY` modes only (the sea fixture has parts: `TGPT_TEST_SEA_PART`), through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs (`artifacts\harness\RUNNING` in the
  main tree held by a live process: wait with one background script).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\COPYNAME` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the
  plugin too (0 errors, 0 warnings). `python tests/source_checks.py` passes.
- No fakes: no ready-made pattern texture standing in for traced light, no flat stand-in for a surface that has
  waves, nothing screen-space, no limits that change the picture. The beach costs 140 ms a frame with the Caustics
  row on (55 off): say what each step costs; do not spend the job on speed.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. Other workers are in
  `pt_common.hlsli` too (glass, wax): change what you must, move nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: each switch (name, values) and what it does in one line, the recipe and the
  runs' commands, what was checked offline with its numbers, what is left, that it compiles and the checks pass,
  the report's path.
