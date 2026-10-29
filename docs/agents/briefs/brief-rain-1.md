# Brief: the rain wets the traced world (PT-108, PT-104) - one worker, hand over early, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/rain-1.md`; write it early and after every step.

## The owner, 2026-10-09

A new save, "PT spec-highlights-terrain": "where we can see if PT preserves the specular highlights we see on the
ground terrain in a scene where there's a fire nearby. In the PT presentation, it looks flat." And of playing with
it: "I do see a lot of general smoothing going on ... floor planks that look smoothened out and various surfaces."

## What the game answered (run `20261009-190504`, scene `spec-terrain`, 19:06; read only, in the MAIN tree:
`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261009-190504\`: `LogOutput.log`, the steps'
frames under `spec-terrain\<step>\` (`step.json` names them: raw RGBA bytes, bottom row first), `report.md`)

- It rains in that save. In the game's picture the ground by the brazier is wet gravel with hundreds of small
  glints of the fire. In the traced picture WITH NO DENOISER the ground is dry and even, with one soft orange blob:
  fine detail in a patch of ground 15.65 in the game, 4.62 traced (its pattern unlike the game's: 0.28).
- The tracer's own check of its first hit against the game's G-buffer ("Yardstick 10s" in `LogOutput.log`): the
  same surface on 95.2 % of the screen; of those pixels another colour 0.7 %, ANOTHER NORMAL 18.7 %, ANOTHER
  ROUGHNESS 59.9 %. The worst material: "Mat_UT_B_01_c_CampaignMap_HOS (TG/Environment/MeshTerrain ...)", 26 % of
  the screen: colour 0 %, normal 63 %, roughness 98 % of its pixels.
- The lead (read it first, in your copy: `docs/agents/materials-2.md`, its section "Outdoors", the last
  paragraphs): the game has a ScreenSpaceWetness pass that runs after HDRP's depth and normals and REWRITES the
  camera's normal and roughness by the rain, by moisture that builds up and dries (PrecipitationController), and by
  a top-down shelter depth array (dry under a roof). The tracer knows nothing of it: its "wet ground" is the water's
  shore alone (`pt_wet.hlsl`, PT-52: not yours). That worker's advice: read the weather's state and evaluate wet
  surfaces at world hits; do not bring a screen-space pass back.

## The job

1. **What the game does, exactly.** Read the game's own code and shader for that pass (decompile read only, as the
   workers before you did: `ilspycmd` on the game's `TG.Main.dll` for ScreenSpaceWetness and PrecipitationController;
   the pass's shader from the game's compiled shaders the way `docs/agents/materials.md` section "How the shaders
   were read" describes; learn, copy nothing into the tree): what it reads (precipitation, moisture, the shelter
   array and its extent, noise or ripple textures, which layers or materials it skips), and what it writes: the new
   smoothness, the new normal (ripples? drops?), anything of colour (darkening), by which formulas. Write it into
   the report in plain words and numbers.
2. **The same in the tracer, for every ray** (the camera's, a reflection's, a bounce's: "A mirror must show the world
   as the camera sees it"; nothing may depend on what is on screen): at a hit, the material's point is wetted as
   the game wets it: the weather's state from the game each frame (the plugin reads the game's own controller: its
   moisture and precipitation as the game holds them), the shelter from the game's own top-down array where it
   reaches (it is the game's simulation data, read where the game leaves it: shared, never copied, as every texture
   the tracer reads) and by the rule the game itself applies outside it. If the array cannot be had by the tracer,
   say why and what the truthful replacement is (a ray toward the rain) with its cost, as a choice for the
   coordinator: do not build a ray a hit on your own.
3. **Behind a hidden switch first** (`Advanced / Rain wetness`, default off until the game has answered; a spare
   field, never an option bit: all 32 are used; the glass and water trials sit in `TGPT_Frame.cameraPosition`'s w,
   the relief and speed trials in `prevCameraPosition`'s w: find another spare and say which), and behind a COMPILE
   switch if it adds more than a few lines to the big passes (AGENTS.md, "Code that is switched off still costs
   frame time").
4. **Hand over the SMALLEST thing that could show the direction within your first hour**, written into the report:
   even "the smoothness alone, by the moisture, no shelter" is a hand-over, because one run of `spec-terrain` tells
   whether the terrain's 98 % falls. The recipe for `tests/ingame/scenarios.json`: the game; no denoiser with the
   switch off and on; 16 frames each; nothing more. The measure the coordinator reads: the Yardstick line's shares
   for the terrain material, and the ground's glints in the frames.
5. STOP at the hand-over: what the run should show (the terrain's "another roughness" and "another normal" shares
   you expect), what of the game's pass is not built yet, what it costs.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 2` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF`); never `-First`, never a raised limit; a line "the coordinator's build of
  the main tree goes first" means: wait, nothing is wrong. The PC ran out of memory twice today. Wait with ONE
  background script, never by polling. To save the first full compile: once the main tree's build is done
  (`%TEMP%\tgb\BUILD-FIRST` gone), COPY its `build\native\generated\pathtracing\*.h` into your build folder's
  `generated\pathtracing\` right after configuring; then only the passes your own change touches compile.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"). A small check of your own in
  a `..._ONLY` mode only if it is quick; the game is the test here.
- No fakes: no ready-made pattern standing in for what the game computes, nothing screen-space, no limits that
  change the picture.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's code and shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit, check
  every hunk landed. Another worker is in `tracer.cpp` and `scene.cpp` (memory): change what you must, move
  nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Build the plugin too (0 errors, 0 warnings). `python tests/source_checks.py` passes.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: what the game's pass does in two lines, what is built and behind which
  switch, the recipe and what the run should show, what is not built, the cost, that it compiles and the checks
  pass, the report's path.
