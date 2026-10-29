# Brief: caustgrid - the caustics' stage costs 4 to 7 ms a frame where nothing of it is seen (PT-114, PT-99)

Copy: `%TEMP%\tg-agents\wt\caustgrid` (pristine base: `base20p`; made from the main tree 2026-10-12 ~09:10). Follow
`AGENTS.md` in your copy; read first its rulings "Speed is no longer left for last", "The order of work", "No limits
that change the picture", "The game is the first test". Report: `docs/agents/caustgrid.md` IN YOUR COPY; the plan
within 20 minutes, kept current; "State when stopped" at once if a limit shows.

## Why

The owner's first priority is a frame rate one can play at (11 to 23 frames a second today, the target 55). The
caustics' stage (`native/pathtracing/shaders/pt_caustics.hlsl`: read its header first, it says what the stage does)
takes, the world running, 1924x1084 (`docs/PT-REQUESTS.md`, grep "### PT-114" and "### PT-99"):
the town 4.8 ms, the forest 4.7, the vista 4.5, the beach 7.3, of a tracer's 33 to 81 ms. In the town and the
forest little or no water is in view. Offline the sea fixture reads 5.2 ms of it at 640x360: the cost does not follow
the screen's size, it is the grid's own.

Interiors are solved since this morning (in an interior the map's water gets no slot: the plugin's
`WaterCaustics.LeftOutside`; the tavern's 4.2 ms went to 0). Yours is OUTDOORS: water that is loaded, that a ray can
meet, and that costs its 4 to 7 ms every frame whether or not any of its light lands where something reads it.

What is known:
- The worker caustcut (2026-10-11, its report `%TEMP%\tg-agents\wt\caustcut\docs\agents\caustcut.md`, also
  `docs/agents/caustcut.md`; read "Readers and exactness") built an exact cut of the SHADOW rays (a tile's corners
  whose ray to the light is proved to be stopped skip it). Offline no gain (2.20 / 2.23 ms), in the game none
  (4.20 / 4.30 / 4.05 ms). So the shadow rays are not where the time goes. Do not build that again.
- The pass's header: a landed triangle "that lands outside the camera's view" is left out, "nothing reads it there
  yet". Check whether that still holds (grep `caustic_light`, `g_scLanded`, "landed" in `pt_common.hlsli`,
  `pt_water_path.hlsli`, `pt_water_sun.hlsli`; PT-130 moved the stage before its readers): WHO READS THE LANDED
  LIGHT TODAY, AND WHERE, decides what may be skipped. List every reader in the report, one by one.
- The measuring build has switches for the stage's parts (`pt_common.hlsli`, grep `SPEED_M2_CAUSTIC_BARE`,
  `SPEED_M2_CAUSTIC_SHADOW`: "its floor (the grid's corners on the waves and each tile's lights ...)"). They have
  never been read in a run.

## What to do

1. MEASURE FIRST, offline, where the stage's milliseconds go: the dispatch itself (how many tiles of how many levels
   and slots run a frame, and how many of them write nothing), a corner's point on the waves (the texture reads), the
   tile's lights, the shadow ray, the two rays on, the writes, the build of the landed triangles' structure
   (`tracer.cpp`). The sea fixture (`tests/water_sea_gpu.inl`; one mode alone: the environment's
   `TGPT_TEST_..._ONLY` switches) prints the stages' times ("SEA WIDTH stages ...: caustics 5.28 ms"). Counters in the
   pass's own counter buffer are the cheap way (its header lists them). Write the split into the report before
   you design anything. If the split says the time is somewhere this brief does not name, follow the split.
2. THEN MAKE IT CHEAPER WITHOUT CHANGING WHAT LANDS WHERE A READER READS IT. Kinds of change that are yours:
   - work that cannot reach any reader, proved, not guessed: a tile none of whose rays can land where the landed
     light is read (the proof is geometry: the tile's place, the water's top and bottom, the light's direction or
     place, the largest slope the waves can have, against the readers' region). A bound that is merely likely is a
     picture defect: where you cannot prove it, trace as now;
   - the same result with less work: a corner shared by neighbouring cells or levels worked out once, a level whose
     cells are wholly inside a finer level's square not traced twice, a slot's tiles outside its water's extent not
     dispatched, texture reads not repeated;
   - a build of the landed triangles' structure that is not made when no triangle landed.
   NOT yours without asking: a coarser grid, fewer levels, a reach in metres, an update every other frame, anything
   a frame late, any count that ends the light. Where such a way would save much, describe it with its number as a
   choice for the owner and stop there.
3. A SWITCH FOR THE GAME: the cut behind one frame flag bit (bits 19 and 24 to 29 are free since this morning:
   `tgpt_api.h`, `NativeTracer.cs`; take 19) and a hidden `Advanced` setting, default off = as before, so that one
   run of one scene shows it off and on. A recipe for it in `tests/ingame/scenarios.json` (the pattern: `sun-cell-still`
   for the picture, frozen, 64 frames; `speed-now` for the time). The coordinator runs `metal`, `forest` and
   `water-day` with it.

## How it is measured and checked

- Speed: offline first (the fixture's stage times, the same frames with the bit off and on), then in the GAME by the
  coordinator. Hand a DLL (path, SHA-256) and the plugin's change as soon as a first cut shows a gain offline: within
  your first two hours, or say why not.
- Same picture: the fixture's frames with the bit off and on, compared pixel by pixel (the same random numbers: say
  whether they are bit-equal, and if not, why and by how much), with a camera that sees the bed, one that sees the
  water from afar and one that sees no water at all; and the count of landed triangles in view (counter 0), which
  must be the same number with the bit off and on in every frame. A check of yours that fails when a tile that lands
  in view is skipped (mutate the bound once to see it fail).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in
  `%TEMP%\tgb\caustgrid-*`. One build tree; delete your own test pictures once read.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start (it also says no while a
  game run waits for the card and while a game of the owner's is open; never remove that wait; wait with one
  background script, never poll). NEVER the whole `tgpt_gpu_test` or ctest. If a game of the owner's opens while a
  test of yours runs, the coordinator's watchdog ends the test: start it again later.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`; it keeps the PC's memory limit), then
  the native build into `%TEMP%\tgb\caustgrid-build`; configure as `docs/AGENT-WORKERS.md` and
  `docs/agents/briefs/brief-campspeed.md`'s build line say (the main tree's prebuilt NRD libraries, NVAPI,
  `-DTGPT_MEGA_GEOMETRY=ON -DTGPT_RAIN_WETNESS=ON`, the disconnected FetchContent paths). Before your first shader
  edit, copy the main tree's compiled headers (`build\native\generated\pathtracing`) into your build so that only
  the passes you change compile. Keep your shader change in `pt_caustics.hlsl` and the files only it includes: a
  change in `pt_common.hlsli` compiles all 53 passes (25 minutes to an hour).
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files (`tracer.cpp` 5,000 lines,
  `pt_common.hlsli`, `tests/pathtracing_gpu_test.cpp`), never whole. The Bash tool eats backslashes: scripts and
  text with backslashes go through the Write tool. Say every switch or log line you add. Run
  `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
