# Brief: waterblas - the water's pieces in fewer acceleration structures: 2,000 driver calls a frame (PT-99)

Copy: `%TEMP%\tg-agents\wt\waterblas` (pristine base: `base20o`; made from the main tree 2026-10-12 morning). Follow
`AGENTS.md` in your copy; read first its rulings "Speed is no longer left for last", "The order of work", "No limits
that change the picture", "The game is the first test". Report: `docs/agents/waterblas.md` IN YOUR COPY; the plan within
20 minutes, kept current; "State when stopped" at once if a limit shows.

## Why

The owner's first priority is a frame rate one can play at (a town's frame is 77 ms, a forest's 89; the target is 18).
A traced frame is, one after the other: the game's inputs, OUR CPU WORK TO RECORD THE FRAME, the trace on the GPU, the
composite. While we record, the GPU waits. The worker reccpu cut the record from 10 to 6-7 ms a frame last night
(`docs/agents/reccpu.md`: read its "Not done, and why" and "Choices" 2 and 3 first). The largest thing left:

- THE WATER'S POSE: 1,400 to 1,800 pieces ("units", `scene.hpp` ~395-445, `scene_water.cpp` `plan_water`,
  `scene.cpp` `pose_water` ~3032) are 1,400 to 1,800 bottom-level structures, each refitted by a
  `BuildRaytracingAccelerationStructure` call of its own EVERY FRAME: 2.3 ms of the record in the town (1,379 units,
  98 characters), 2.8 to 3.0 in the forest (1,793, 67), and 0.6 to 0.8 ms of the GPU's scene stage. reccpu measured
  the record's own work at a few dozen nanoseconds a unit: the time is the calls (about 1.3 microseconds each in the
  driver).
- The plants show the cure: a group of plants is ONE structure of dozens of geometries and one call (`plan_plants`,
  the plants' groups by cell; a hit names its record by the instance's ID plus `GeometryIndex()`).

## What to do

Group the water's units into structures of many geometries each (by pool page, by cell, by water mesh: your choice,
with its reason), so that a frame's refits are a few dozen calls, not 1,800. THE SAME TRIANGLES IN THE SAME PLACES,
THE SAME PICTURE: what a ray meets, which record it reads and what it reads there must not change.

What changes, and so what needs care:
1. An instance becomes a group, not a unit: every place a pass turns a hit on water into its record (grep
   `INSTANCE_WATER`, `kMaskWaves`, `WaterRecord`, `water_base`, `fetch_hit` and the water's own queries in
   `pt_common.hlsli`, `pt_water_surface.hlsli`, `pt_water_mesh.hlsl`, `pt_water_path.hlsli`) must find the same
   record through the group (the instance's ID plus the geometry's index, as the plants do). List every such place in
   the report, one by one.
2. Units come and go (a unit is dropped `kWaterKeepFrames` after its last use; `plan_water` makes new ones as the
   camera moves). A group whose set of units changed cannot be refitted: it is built anew. Choose the grouping so
   that a walk rebuilds few groups a frame, and say how many (count it in the log's water line).
3. READ PT-125 IN `docs/PT-REQUESTS.md` BEFORE YOU STORE ANY NUMBER (grep "### PT-125"). Last night's GPU resets were
   a number stored once (a place in a per-frame buffer) that went stale when the set of plant groups changed. Any
   index of yours that outlives a frame is relative to something that cannot move, or is written again every frame
   it is read. The same for addresses of structures in pools that compact (PT-134).
4. The units' memory rules stay (the pools' pages given back, the triangle budgets by the clock at the world's
   arrival, PT-98); no count of units or groups and no distance ends the water.

If, reading, you find the grouping cannot keep the picture or would rebuild more than it saves, STOP and say so with
the numbers; reccpu's choice 3 (the structures' commands recorded on a second list by a second thread, the same
commands in the same order) is then the other way, and yours to describe, not to build.

## How it is measured and checked

- Speed: in the GAME alone, by the coordinator. Hand a DLL (path, SHA-256) when the grouping runs offline: the
  coordinator runs `water-day@speed-now` and `metal@speed-now,forest@speed-now` and sends you the lines "PT record
  CPU 10s" (the section "the water's pose and the characters"), the 10 s line's "cpu=... record ..." and the scene
  stage's GPU time. This is a structure's change, not a switch: no trial within half an hour is expected; say in the
  plan when the first DLL can come.
- Same picture, offline: the sea fixture (`tests/water_sea_gpu.inl`; grep how `tgpt_gpu_test` runs one mode alone:
  the environment's `TGPT_TEST_..._ONLY` switches) with the grouping on and off, the frames compared pixel by pixel
  (the same random numbers: they should be bit-equal or differ by rounding alone; say which and why). Keep the old
  way behind an environment variable of the TEST'S for that comparison, never a setting. A check of yours that
  walks the camera so that units come and go, and fails when a hit reads another unit's record (mutate once to see
  it fail).
- The record's self-check (an empty file `record-check` beside the DLL, reccpu's): extend it to the water if the
  grouping keeps anything between frames.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in
  `%TEMP%\tgb\waterblas-*`. THE DISK HAS 13 GB FREE: no second build tree, no image folders kept after a check is
  read (delete your own test images when you have looked at them), no copies of the SDKs.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start (it also says no while a
  game run waits for the card and while a game of the owner's is open; never remove that wait; wait with one
  background script, never poll). NEVER the whole `tgpt_gpu_test` or ctest. If a game of the owner's opens while a
  test of yours runs, the coordinator's watchdog ends the test: start it again later, it is nobody's fault.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`; it keeps the PC's memory limit), then
  the native build into `%TEMP%\tgb\waterblas-build`; configure as `docs/AGENT-WORKERS.md` and
  `docs/agents/briefs/brief-campspeed.md`'s build line say (the main tree's prebuilt NRD libraries, NVAPI,
  `-DTGPT_MEGA_GEOMETRY=ON -DTGPT_RAIN_WETNESS=ON`, the disconnected FetchContent paths). Before your first shader
  edit, copy the main tree's compiled headers (`build\native\generated\pathtracing`) into your build so that only
  the passes you change compile (53 passes take 25 minutes to an hour otherwise). A change in `pt_common.hlsli`
  compiles them all: keep the shaders' change small and in the water's own files where it can be.
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files (`scene.cpp` 4,600 lines,
  `tracer.cpp` 5,000, `pt_common.hlsli`), never whole. Say every switch or log line you add. Run
  `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
