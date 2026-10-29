# Brief: caustcut - the caustics' 3.9 ms in rooms that light nothing, cut exactly (PT-114)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-11 ~19:00). Follow `AGENTS.md` in your copy;
read first its rulings "No limits that change the picture" and "No shortcuts", "The order of work" (speed comes first now),
"The game is the first test" and "Code that is switched off still costs frame time". Report: `docs/agents/caustcut.md` IN
YOUR COPY; the plan within 20 minutes, kept current; "State when stopped" at once if a limit shows.

## Why

`docs/PT-REQUESTS.md` PT-114 (grep "### PT-114", read it whole): in Ron's Tavern the caustics' stage costs 3.9 ms of the
tracer's ~31 to 36 and lands nothing. The game's interiors float about 470 m above the sea of the map around them; the
sea is a water slot, so `pt_caustics.hlsl` lays its 8 levels of grid round the camera over it, finds each corner's point
on the waves and traces a shadow ray to the tile's lights: the tavern's lamps, behind the floor. Every ray is traced, none
lands. The owner wants a frame rate one can play at first (AGENTS.md "The order of work"), and no cut that changes the
picture.

## What to do

(a) The plan in the report first: where the caustics' light is READ (today: the camera's own surfaces in
    `pt_compose.hlsl` and `pt_nrd_prep.hlsl`, and what the water's path meets under the surface in
    `pt_water_path.hlsli`; grep `caustic_light`). A tile's light that can reach none of those places cannot change the
    picture, so a test that proves "cannot reach" is exact. PT-114's second idea is one such test: bound the frame's
    receivers (a box, or a few) and skip a corner's rays, reflected and refracted, whose way out of the water cannot meet
    it; the shadow ray to the light only where one of them can. Others may be better (the light's side: a light whose
    every path to the water is closed by the room; per tile and light, before the corners): measure where the 3.9 ms go
    first (the shadow rays, the wave points, the grid's own work), then choose. Whatever you build must give the SAME
    picture: an exact bound, never a distance, a count or a guess.
(b) Note in the report, without building it: the caustics are read only where the camera looks and under water, not in
    reflections or by the bounced light. AGENTS.md says a mirror shows the world as the camera sees it, and nothing may
    depend on what is on screen; say what that would ask of the caustics and of your bound (the receivers would grow).
(c) Build it behind frame flag bit 23 (`NativeTracer.cs` `1u << 23`, a hidden `Advanced` setting "Caustics receivers
    bound (trial)", default off; say the native name you give it), and a recipe `caustics-bound` in
    `tests/ingame/scenarios.json` (the switch off and on, no denoiser, `perfSeconds` on both; scenes: `tavern-room` for
    the speed, `water-torch` and `water-day` for "the same picture"). Hand the coordinator ONE trial build within your
    first hour and a half, or say why not. Offline: the caustics fixture of `tests/pathtracing_gpu_test.cpp` (grep its
    caustics mode) with the switch off and on gives the same caustic triangles and light.

Not yours: RTX Mega Geometry (worker megageo-6), NRD's and RR's guides (worker psr).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small mode only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (a cold pass cache
  needs `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test` or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\caustcut-...`; NRD's own CMake writes into its SDK folder: configure as the vendor-rr worker did
  (`docs/agents/vendor-rr.md` "Build isolation"). For any `dotnet` call set `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true`
  and `MSBuildEnableWorkloadResolver=false` first. RTXDI and NRD sources are never copied or quoted. Grep and read ranges
  of the big files. Say every export, record, FrameConstants row, option or flag bit you add. Run
  `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
