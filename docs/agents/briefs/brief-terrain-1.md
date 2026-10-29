# Brief: the ground has no relief in the traced picture (PT-104, PT-108) - one worker, the game answers first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/terrain-1.md`; write it early and after every step.

## The owner, 2026-10-09

Of the save "PT spec-highlights-terrain": "we can see if PT preserves the specular highlights we see on the ground
terrain in a scene where there's a fire nearby. In the PT presentation, it looks flat."

## What the game answered (the run `20261009-202514`, scene `spec-terrain`, recipe `rain`; read only, in the MAIN
tree `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261009-202514\`: `LogOutput.log`,
`spec-terrain\<step>\full.jpg`, and the sheet `spec-terrain\ground-game-dry-wet.jpg`: LOOK AT THAT SHEET FIRST)

- The game's ground by the brazier: mud and gravel with strong relief, every stone catching the fire, the fire's
  light spread over the ground in warm patches. Traced: the same pebbles as a flat print, no relief at all; the
  fire's light one soft blob (dry), or one round mirror image of the flame (with the rain's wetness on trial,
  which makes the roughness the game's: the terrain's "another roughness" fell from 98 % to 1 % of its pixels).
  Fine detail in that patch: 13.4 in the game, 5.4 traced dry, 5.8 traced wet.
- The tracer's own check of its first hit against the game's G-buffer ("Yardstick 10s" in `LogOutput.log`):
  "Mat_UT_B_01_c_CampaignMap_HOS (TG/Environment/MeshTerrain, computed as its shader computes it)", 26 % of the
  screen: place 0, colour 0, NORMAL 63 % of its pixels another than the game's, in both steps. The rain does not
  touch normals (`docs/agents/rain-1.md`). So the terrain's normal is wrong in the tracer, and by the picture it
  is not a little wrong: the relief is not there.
- Earlier runs: the forest's terrain 27 % another normal, the vista's 71 % (`docs/agents/materials-2.md`,
  "Outdoors"; it suspected the missing true mesh tangents and changed nothing).

## Where to look (in your copy; grep, then ranges)

- The tracer's terrain: `native/pathtracing/shaders/pt_materials.hlsli` `program_terrain` (eight layers laid flat
  over the world's x and z, two splat maps, each layer's colour, normal and mask map), with `graph_normal`,
  `frame_normal`, the `TangentFrame` it is handed, `program_sample`, `planar_gradients`.
- The plugin's side: `src/TGPathTracing/MaterialPrograms.cs`, the case `TerrainShader` (which properties and
  textures it reads by which names, the layers' normal strength), and how a program's textures are shared.
- The game's shader as an earlier worker decoded it: `...\scratchpad\mw\terrain\blob55.txt` (beside your copy's
  parent folder; read only; learn, copy nothing), and `docs/agents/materials.md` (how the shaders were read; its
  table's line for MeshTerrain: "its normal's sign along z (tangent, below)").
- Debug views the harness can set (`Debug view`: `Normals`, `Yardstick`, `Materials`, `Surfaces`): a step with
  `Normals` shows the tracer's first hit's normal; the Yardstick view shows where it differs from the game's.

## Suspects (the coordinator's guesses: check each, believe none)

1. The layers' normal maps never reach the pass (not found by their property names, not shared, a slot missing):
   `program_sample(..., MAP_FLAT)` then answers "flat" and nobody is told.
2. The layers' normal strength arrives as 0 or is read from the wrong property.
3. The maps are decoded by the wrong channels (a two-channel normal map read as three, or the other way).
4. The frame: the layers are laid over the WORLD's x and z, so their tangent and bitangent are the world's x and z
   axes (with the game's own signs), not the mesh's texture-coordinate frame; the code hands `frame_normal` the
   mesh's frame and a sign flip.
5. The blend: weights that cancel the normals where two layers meet, or the height sharpening.
6. The game adds relief the tracer does not have at all (a detail normal, parallax, tessellation: PT-28).

## The job

1. Within your first half hour: which suspect it is, as far as reading can tell, and THE ONE RUN that proves it
   (a recipe for `tests/ingame/scenarios.json`, scene `spec-terrain`: the game; traced with no denoiser; the
   debug views that show it; the fewest steps). If the plugin's log can say it (which textures the terrain's
   program was handed, with their handles and the strengths), add that line: a plugin change needs no shader
   compile and the coordinator can run it at once.
2. The fix, smallest first, behind a hidden switch if its direction is not certain (a spare field, never an
   option bit: all 32 are used; `Advanced / Rain wetness` shows the pattern of a record of its own), and behind a
   COMPILE switch if it adds more than a few lines to the big passes and is still a trial (AGENTS.md, "Code that
   is switched off still costs frame time"). For every ray: a mirror must show the same ground.
3. Hand over: what to build, the recipe, what the Yardstick's "normal" share for the terrain should fall to (63 %
   now), what the ground should look like. STOP there.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- No fakes: no ready-made relief, no screen-space trick, nothing that depends on what is on screen; the game's own
  maps at the game's own size, read where the game holds them.
- Shader compiles only if needed: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 2` before
  `cmake --build` (configure with `-DTGPT_WITH_NRD=OFF`); never `-First`, never a raised limit. To save the first
  full compile, copy the main tree's `build\native\generated\pathtracing\*.h` into your build folder's
  `generated\pathtracing\` right after configuring (they were built with `-DTGPT_RAIN_WETNESS=ON`: configure yours
  the same way, or every pass compiles again).
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"). No fixture before the game
  has answered.
- Unity's, HDRP's and the game's code and shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `pt_materials.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp`
  or `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Build the plugin (0 errors, 0 warnings). `python tests/source_checks.py` passes.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: the cause, what is changed and behind which switch, the recipe and what the
  run should show, what is not done, that it compiles and the checks pass, the report's path.
