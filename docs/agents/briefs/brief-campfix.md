# Brief: campfix - the fix for the camp in the forest at 3 frames a second (PT-131), after campspeed's measuring build

Copy: `%TEMP%\tg-agents\wt\campspeed` (the worker campspeed's copy: you go on in it; pristine base `base20f`). Follow
`AGENTS.md` in the copy; read first its rulings "Speed is no longer left for last", "No limits that change the picture",
"Shader Execution Reordering stays on", "The game is the first test" and "Code that is switched off still costs frame
time". Report: `docs/agents/campfix.md` IN THE COPY; the plan within 20 minutes, kept current; "State when stopped" at
once if a limit shows. You are a FRESH worker: campspeed's whole reading is in `docs/agents/campspeed.md` in the copy
(17 KB: read it whole, first), its brief in `docs/agents/briefs/brief-campspeed.md`.

## What is known (2026-10-12 02:10)

The camp (scene `forest`, recipe `camp`; camera about -1246, 58, -2916): the tracer's GPU time is 366 ms with the
Caustics row on, 32 ms with it off, the same picture. campspeed's measuring build in the game, run `20261012-012659`
(the world running, no denoiser; a frame's stage times, ms):

| step (hidden `Speed trial`, third page) | tracer GPU | reflections | bounced light |
|---|---|---|---|
| 3: the water's path not run (32773) | 30.77 | 6.71 | 4.21 |
| 4: the water's path run, `water_sun` not asked for the sun through the waves (32785) | 40.15 | 13.63 | 6.18 |
| everything (main's build, earlier runs) | 346 to 366 | 333 to 345 | 110 to 122 |

Step 2 of that run was not valid and the GPU hung in the tracer's SCENE stage during step 5's walk (another defect,
not yours), so steps 5 to 7 are not measured yet. The counters during step 4's hold (lines 32, 38, 45 of
`%TEMP%\tgb\campspeed-run-012659-lines.txt`; one line is 2 KB: read them with `cut -c1-3000`), a frame:
bounced light: 376 water paths (183 start past 60 m; their starts 812 m from the camera on average, 22 m under its
height), 985 turns, 473 scatter points, 429 times the surface met; reflections: 314 water paths (218 past 60 m; starts
1,113 m away on average), 802 turns, 388 scatter points. During a walk past near water a frame had 530,015 and 640,587
water paths at 35 m (line 52), so the counters work.

So: about 700 paths a frame reach the SEA, far away and below, from bounce and reflection rays; asking the sun through
the waves at their about 860 scatter points costs 320 ms (a patch of waves made at each point); and the water's path
without that still costs 9 ms for those 700 paths (7 of them in the reflections).

A second run of the measuring build is under way now (recipe `camp-measure-b`: steps 2, 7, 5, 6 in that order). The
coordinator sends you its numbers when it ends (a short message). Do not wait for it: start.

## What to do

(a) THE FIX, as a build for players (CMake WITHOUT `TGPT_SPEED_MEASURE`; nothing of the counters or of the measuring
    switches in it). campspeed's candidate is "the cell by the ray's width" (its report's section of that name): a
    patch's cell no finer than half the width of the path's own ray at the point. Before you make it the rule, check
    its claim yourself: that what the picture converges to stays the same (the patch's sum exact for any cell, the grid
    shifted by chance), and where the claim does not hold (a mirror's ray, the camera's own ray: they must stay as fine
    as today). If you find a better exact way while reading (a patch that costs its 650,000 texture reads for one
    scatter point 800 m away is the thing to remove, by any way that keeps the mean), say so and take it.
    For the game's A/B keep ONE hidden switch that turns the fix off (the pattern: an `Advanced` setting or a free
    option bit; grep how the hidden `Speed trial` reaches the passes; a few lines, not a page of switches), so that one
    run of `forest@camp` shows it on and off. It comes out the day the game has answered.
(b) THE 9 ms THAT REMAIN (step 4 against step 3): why do 700 paths a frame cost 7 ms in the reflections' pass? Read
    what a path does when it meets the sea 800 m away (the search for the surface among the waves, the turns in the
    water, what 31 other threads of a group wait for). Say what it is; if a cure that keeps the picture is small, put
    it in; if it is large, describe it and stop.
(c) Hand the coordinator the build: the DLL's path, its SHA-256, the recipe to run (add one to the copy's
    `tests/ingame/scenarios.json` beside `camp`: the fix on, then off, each a live step that walks to the camp and
    holds 30 s, as `camp-measure`'s steps do), and what to look at. As soon as it is built; the offline check after.
(d) Offline, your ONE small check (campspeed's `%TEMP%\tgb\campspeed-scripts\sea_cost.ps1` and the sea fixture's body
    part; its report says what to judge: the sun's share with and without the fix over 256 frames, a mirror over the
    sea unchanged). No mutant runs until the game has said the direction is right.

Not yours: the scene-stage hang, the fog (workers fogrule, nosun), mirrors under NRD, RTX Mega Geometry.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in `%TEMP%\tgb\campfix-*`.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start, and not while the file
  `artifacts\harness\RUNNING` of the main tree stands (wait with one background script, never poll; a cold pass cache
  needs `TGPT_TEST_STARTUP_SECONDS=900`). NO GPU TEST WHILE STARFIELD IS OPEN (the owner's rule of 2026-10-12: a
  worker's test beside it reset the GPU driver); never remove that wait. NEVER the whole `tgpt_gpu_test` or ctest.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`; it keeps the PC's memory limit), then the
  native build into `%TEMP%\tgb\campfix-build`; configure as campspeed's build was (its report, "The measuring build",
  and `brief-campspeed.md`'s build line: the main tree's prebuilt NRD libraries, NVAPI, `-DTGPT_MEGA_GEOMETRY=ON
  -DTGPT_RAIN_WETNESS=ON`, the disconnected FetchContent paths), without `-DTGPT_SPEED_MEASURE`. A change to a file
  every pass includes compiles all 57 passes: about half an hour. Two other workers compile tonight too.
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files (`pt_common.hlsli`,
  `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`), never whole. Say every switch, export, record or
  FrameConstants row you add. Run `python tests/source_checks.py` before you hand over.
- Leave campspeed's measuring code where it is (between `#ifdef PT_SPEED_MEASURE` and its end): the coordinator
  decides at the merge what of it stays.
- Final message: five lines at most, pointing at the report.
