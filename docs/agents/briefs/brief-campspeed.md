# Brief: campspeed - a camp in the forest at 3 frames a second: which part of the water's or the glass's path costs 330 ms (PT-131)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-12 ~00:05). Follow `AGENTS.md` in your copy; read
first its rulings "Speed is no longer left for last", "The order of work", "No limits that change the picture", "Shader
Execution Reordering stays on", "The game is the first test" and "Code that is switched off still costs frame time".
Report: `docs/agents/campspeed.md` IN YOUR COPY; the plan within 20 minutes, kept current; "State when stopped" at once if
a limit shows.

## Why

The owner's first priority is a frame rate one can play at. The slowest place seen so far (`docs/PT-REQUESTS.md`, grep
"### PT-131", read it whole): a torch-lit camp in the forest of Horns of the South (camera about -1246, 58, -2916; the
harness walks there: scene `forest`, recipe `camp`). Measured in the game on 2026-10-11, the world running, no denoiser:

| what runs | tracer GPU | reflections | bounced light | direct light | prepare | run |
|---|---|---|---|---|---|---|
| as it is (the Caustics row on) | 366 ms | 345 | 122 | 2.7 | 1.35 | `20261011-223913` |
| the Caustics row off | 32 ms | 11.3 | 6.0 | 2.65 | 1.75 | the same |
| the plain passes, no Shader Execution Reordering | 360 ms | 361 | 103 | | | `-215835` |
| measuring build: no "am I under water" question (8192) | 346 ms | 333 | 110 | | | `-233418` |
| measuring build: no lamps at a hit (512) | 340 ms | 371 | 133 | | | the same |
| measuring build: NO GLASS FOLLOWED AND NO WATER PATH (16384) | 31 ms | 4.5 | 3.7 | | | the same |
| measuring build: no shading at all (32) | 24 ms | 2.3 | 1.6 | | | the same |

Where the walk starts (the forest's save, 140 m away) the same build takes 70 to 85 ms. The two pictures at the camp,
the row on and off, look the same (`forest\2-camp-as-it-is\frame.jpg`, `3-camp-no-caustics\frame.jpg`): torches or lanterns
behind palisades, a pine, rain; no water in view, though 1,800 pieces of the water's surface (1.55 M triangles) lie around.
479 traced lights. So: the cost is in `shade_path`'s special branches (`pt_common.hlsli`, grep `SPEED_M_NO_SPECIAL`:
glass followed, `hit_is_glass`, `pt_dielectric.hlsli`; the water's own path, `pt_water_path.hlsli` with `pt_water_sun`,
`pt_water_lights`), only with the Caustics row on (`TGPT_OPTION_NO_CAUSTICS` off: `water_passes_light()` false, the sun
through the waves, the caustics' landed light, what the glass walk counts of a lamp), and a part of the rays pays for all
of it: with the row off those branches cost about 7 ms there.

One theory is already dead (PT-131): the camera is NOT judged "in the water" there.

## What to do

(a) Find which code it is, by MEASURING. Offline first, if a small fixture can show it (minutes a run; your own mode
    only): the existing water fixtures (grep `sea`, `water` modes in `tests/pathtracing_gpu_test.cpp`) at night with
    lamps around, a lantern (a lamp inside a closed glass shell) beside rough ground, rays that reach water from far.
    If nothing offline shows a cost of that kind, do not build a fixture on a guess: go to (b).
(b) Give the coordinator ONE MEASURING BUILD for the game (CMake `-DTGPT_SPEED_MEASURE=ON`, as the main tree's
    `%TEMP%\tgb\measure-build` was configured: `<scratch>`-free, the line is in `docs/rounds/round-20.md` "State at 23:10"
    and in `docs/agents/perf-1.md`) with FINER switches inside the two special branches, each a bit of the hidden
    `Speed trial` that is free in a measuring build (grep `SPEED_M_` in `pt_common.hlsli`; 128 and 256 also switch the
    leaves' micromap level in `tracer.cpp`: do not use them), for example: glass not followed; the water's path not run;
    inside the water's path: no sun through the waves (`water_sun`), no patch of waves made at the point, no caustics'
    landed light, no lamps, no scattering in the water, the path ended at its first vertex; inside the glass walk: no
    lamp's ball or sun's disc counted, the walk ended at its first interface. AND COUNTERS in the tracer's log, once a
    second (a small readback; see how `pt_probe` and the caustics' counters do it): how many pixels' reflection rays and
    bounce rays entered the water's path and the glass walk, their vertices in all, their rays in all. A recipe
    `camp-measure` in `tests/ingame/scenarios.json` (the main tree has one for the switches above: copy its shape; each
    step walks 150 m at 10 m/s and holds for 30 s; at most 8 steps). Within your first two hours, or say why not.
(c) When the coordinator sends the numbers: the fix. It must keep the picture (no cap, no cut that changes what the
    picture converges to; Shader Execution Reordering stays on). If the cause is a loop that runs long for nothing (a
    path that cannot end, a lamp list walked whole at every vertex, a solve that does not converge), fix that. If it is
    honest work that is simply dear, say what it buys and what a cheaper exact way would be.

Not yours: the fog grid (worker fogfix), primary surface replacement (worker psr-2), RTX Mega Geometry.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small mode only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (wait with one
  background script, never poll; a cold pass cache needs `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test`
  or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\campspeed-...`; use the main tree's prebuilt NRD libraries (`-DTGPT_NRD_PREBUILT_LIBRARY=<main>/build/native/nrd/NRD.lib
  -DTGPT_NRD_PREBUILT_DEPS=<main>/build/native/_deps/shadermake-build/ShaderMakeBlob.lib`), `-DNVAPI_ROOT=<main>/NVAPI-R615
  -DTGPT_MEGA_GEOMETRY=ON -DTGPT_RAIN_WETNESS=ON`, `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` with
  `-DMINHOOK_ROOT=<main>/build/native/_deps/minhook-src -DFETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER=<main>/build/native/_deps/meshoptimizer-src`.
  A measuring build compiles all 57 passes: about half an hour on this PC while others compile. For any `dotnet` call
  set `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true` and `MSBuildEnableWorkloadResolver=false` first. RTXDI and NRD
  sources are never copied or quoted. Grep and read ranges of the big files. Say every switch, counter, export, record
  or FrameConstants row you add. A measuring switch lives between `#ifdef PT_SPEED_MEASURE` and its end: a build for
  players holds none of it. Run `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
