# Brief: the game's height maps traced: relief with self-shadows on bricks, walls and floors (PT-28) - one worker

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/relief-1.md`; write it early and after every step.

## The owner

2026-10-06: "We probably need parallax occlusion mapping (with self-shadows support) for bricks and stuff in-game,
like the castle walls." 2026-10-10 00:20: "you also said that bricks/stone had a similar parallax map/bump map or
something, that we can use for our parallax occlusion mapping with self-shadowing for bricks, right?"
The rulings that decide the how (AGENTS.md): the tracer reproduces what every one of the game's shaders shows; no
fakes (never a relief the game's material does not hold); a mirror shows the world as the camera sees it; nothing
that depends on what is on screen.

## What is known (read first, in your copy: `docs/PT-REQUESTS.md` PT-28 and PT-104: grep the headings;
`docs/agents/terrain-1.md`; `docs/agents/materials.md` for how the game's shaders were read)

- From a state dump of the market at Horns of the South (1,104 materials): 11 materials with HDRP/Lit's
  `_HEIGHTMAP` (`MI_Wall_01`, `MI_Wall_03`, ...), 2 with pixel displacement, the floor `MI_Floor_01_HoS` (the
  game's own `TG/Architecture/KF_Architecture`) with `_PARALLAX_OCCLUSION_MAPPING`, 2 terrain materials with
  `_TERRAIN_PARALLAX`. A keyword being set does not prove the game's picture displaces by it. The tracer reads
  none of them. Walls with only a normal map have no height: nothing is made up for those.
- Found tonight (PT-104): a normal map needs a frame, the tracer takes it from the mesh's texture coordinates, and
  where a mesh has none `frame_normal` silently answers flat. The ground's meshes have none. Whatever you build
  needs the same frame: say for each material kind where it comes from, and never fail silently.
- The scene `spec-terrain` (the owner's save "PT spec-highlights-terrain": a castle courtyard in the rain by a
  brazier) holds `MI_Floor_01_HoS` and castle walls; its last run: the MAIN tree's
  `artifacts\harness\20261009-202514\spec-terrain\` (read only: `1-game\full.jpg` is the game's picture).

## The job

1. **The inventory with proof, within your first half hour** (the report's first table): which materials hold a
   height the game's shader really uses, by shader: the map and its channel, the amplitude and its unit, the
   number of steps the game marches, whether the game's variant shadows itself, what the keyword does in the
   compiled variant (read the game's shaders as the workers before you did: learn, copy nothing; HDRP's Lit
   from the game's own HDRP assembly and shaders, the game's graphs from its compiled shaders). How many such
   materials a scene has: a plugin log line, once a scene ("Relief: N materials with a height in use: ...").
2. **The design for the tracer, for every ray**: where a ray meets such a surface, the surface IS the height
   field under the triangle: the ray goes on into it (a march along the ray in the surface's frame, with the
   game's own map, channel and amplitude, refined to the crossing), and the hit's place, texture coordinates and
   normal are the crossing's. A ray that leaves such a surface (a shadow ray, a bounce, a reflection) starts at
   the crossing and marches out through the height field: that is the self-shadow, by the same field, for the
   sun, the lamps and bounced light alike. Say what it costs per hit and per leaving ray, where it sits
   (`hit_material`? the trace functions?), what the silhouette does (the game's parallax does not move the
   outline; say what the tracer's does and whether that differs from the game's picture), and what the camera's
   own first hit and the Yardstick's comparison with the game's depth make of it.
3. **The smallest trial, handed over within your first hour and a half**: ONE material kind (the one the scene
   `spec-terrain` shows most of), the march at the hit and the self-shadow toward the sun, behind a COMPILE
   switch (`TGPT_RELIEF`, OFF by default: AGENTS.md, "Code that is switched off still costs frame time") and a
   hidden `Advanced / Relief trial` (a record or spare field of its own, never an option bit: all 32 are used;
   `Advanced / Rain wetness` and `Terrain frame` show two patterns), with the recipe for
   `tests/ingame/scenarios.json` (the game; traced with no denoiser, the switch 0 and 1; 16 frames each) and
   what the picture should show.
4. STOP at that hand-over: what is built, what the run should show, the cost you expect in ms, what the other
   material kinds need.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- No fakes: only a height the game's material holds and its shader uses, with the game's own numbers; no height
  guessed from a normal map or a colour; nothing screen-space.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 2` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON`, as the main tree's build is, and copy the main
  tree's `build\native\generated\pathtracing\*.h` into your build folder's `generated\pathtracing\` right after
  configuring, once the main tree builds nothing: then only what your change touches compiles); never `-First`,
  never a raised limit; "the coordinator's build of the main tree goes first" means: wait.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"). No fixture before the game
  has answered.
- Unity's, HDRP's and the game's code and shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `pt_materials.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp`
  or `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed. Two other workers are in the shaders (the water's path: `pt_water_path.hlsli`,
  `pt_specular.hlsl`, `pt_gi.hlsl`) and in `tracer.cpp`, `scene.cpp` (acceleration structures): change what you
  must, move nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Build the plugin (0 errors, 0 warnings). `python tests/source_checks.py` passes.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: which materials really have a height in use (counts), what is built and
  behind which switches, the recipe and what the run should show, the cost, what is not built, that it compiles
  and the checks pass, the report's path.
