# Brief: who takes the scattering walk (PT-40 wax, PT-73 c speed, i armour) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/who-walks.md`; write it early and after every step.

## Why

The tracer lights skin and wax by a random walk under the surface (`subsurface_vertex`, `pt_walk`; PT-58, PT-40). Two
workers found, from two sides, that the wrong things walk:

- Too many (`docs/agents/speed.md` section 2.2): the plugin marks a material by its diffusion profile alone
  (`src/TGPathTracing/HdrpPasses.cs` `ReadSubsurface`), "whatever its subsurface mask says". In the tavern 30
  materials carry the skin profile; 15 of them are `TG/Character/Kandra_Lit`, which clothes and armour are made with
  (a metal vest, legs, feet, a bow); the ground and snow cliffs carry a snow profile. A walk is 3 to 50 rays where a
  plain path takes about four, at every bounce and reflection hit. It is the larger part of why the tavern went from
  30 to 62 ms, and a likely part of dull, oddly coloured armour.
- Too few (`docs/agents/candle.md` "Wax: a separate confirmed classification gap"): the candles' wax
  (`Mat_Prop_Candles_02`: double-sided, subsurface and transmission features on, alpha tested) is left out, because
  `ReadSubsurface` excludes every double-sided material. The game shows the wax cream-white; the tracer brown
  (`artifacts\harness\20261008-015158\candle\crop-candles.jpg` in the main tree).

The owner's rules that decide this: "everything should be path traced" and "each material's own values replace fixed
numbers"; no limits that change the picture; no stand-ins. So the rule is the game's own, per material and per point.

## What to build

1. **A walk only where the game's material scatters under its surface, and as much as it says.** Find what the
   game's shaders use: the material's type or feature (HDRP Lit: subsurface scattering against translucent against
   standard), its subsurface mask (a number times a map's channel) and, for the game's own shaders (`Kandra_Lit`,
   `MeshTerrain`, `RealSkin`: `docs/agents/materials.md`, `materials-2.md`), where they take the mask from. The mask is
   the share of the surface's diffuse light that scatters: a point with mask 0 is a plain surface. Use it at the hit
   as an unbiased choice or blend (say which and why), never a threshold that changes the picture.
   Log, once a material, why it walks or does not (the candle job removed the log's cap of 40).
2. **A closed solid walks, whatever its culling mode; an open sheet does not.** The glass job gave every submesh a
   mark for "its surface closes a volume" (`GeometryRecord.flags & GEOMETRY_CLOSED`, `scene.cpp` `mark_closed`). Use
   it: double-sided wax that closes a volume is a volume; a double-sided sheet with a profile (a leaf, cloth, an ear
   drawn as one skin) keeps the thin transmission it has today. Say what the candles' meshes are (closed or not: read
   it from a harness run's log or add the line) and what happens to the alpha-tested parts.
3. **The ground and snow.** Terrain with a snow profile is an open surface over a half-space: say what the game shows
   there, what a walk does (it can only leave through the top), what it costs, and build what is correct.
4. **Energy:** the logs give several leaf materials transmitted shares above one (beech 3.38, 3.94, 0.68; flowers
   1.43, 1.73, 0.23: `docs/agents/candle.md`, its last section). Find whether the tracer then makes light from
   nothing; fix it at the cause or say why it does not.
5. If time is left: the candle job's failing check (a skin slab moved 110 m from the origin loses 4.5 % of its blue:
   a walk's sub-millimetre flights in world coordinates). Its notes: `docs/agents/candle.md` "Targeted validation and
   precision finding". Otherwise leave it and say so.

## How it is judged

- Offline: checks of your own in the GPU test (a metal plate with the skin profile and mask 0 is lit as a plain metal
  and takes no walk; a closed double-sided wax cylinder is lit as the single-sided one; an open double-sided sheet is
  unchanged; a mask of 0.5 gives the mean of both). Count walks: a status number or a log line the test can read.
  Timing of the stage before and after on the test's scenes (alone on the GPU, or say it was shared).
- The whole GPU test ONCE at the end, FAIL lines written down. A run with failed frames or `last error 0x887A0006` is
  void: run it again. A run that fails only at "the passes were made" runs once more (a cold pass cache).
- In game the coordinator runs `candle@quick`, `hair-moving@quick`, `glass-jars@quick`: say what should change (the wax's
  colour against the game's, how many materials walk in the tavern, the stage times).

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
  `tracer.cpp`, `HdrpPasses.cs` or `tests/source_checks.py` whole. The Bash tool eats backslashes: write files with
  Write/Edit. Small and local edits: other jobs are merged into these files.
- A new struct field, export or option bit: say so in the report with sizes (the next free ABI number is
  0x00012400; the water job may take it first: the coordinator settles it at the merge). `GeometryRecord`'s words are
  taken (`layers`, `flags` with bits 1 and 2): add a flag bit, not a new meaning for a word.
- No mutation checks. No denoiser tuning. Nothing copied from Unity/HDRP or the game's shaders; RTXDI and NRD sources
  are never copied or quoted.
- Final message: five lines at most, plain words, pointing at the report: what is built, what passed, what is left.
