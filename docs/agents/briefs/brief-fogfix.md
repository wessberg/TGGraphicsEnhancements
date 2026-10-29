# Brief: fogfix - the fog grid over-counts lamp light near lamps and whites out the tavern (PT-118)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-11 21:20). Follow `AGENTS.md` in your copy;
read first its rulings "No limits that change the picture" and "No shortcuts", "Follow NVIDIA's and AMD's guides to the
letter", "The game is the first test" and "Code that is switched off still costs frame time". Report:
`docs/agents/fogfix.md` IN YOUR COPY; the plan within 20 minutes, kept current; "State when stopped" at once if a limit
shows.

## Why

The owner chose the fog grid as RTX Remix builds it (PT-118, `docs/PT-REQUESTS.md`, grep "### PT-118"; the worker's
report `docs/agents/fog-froxel.md`), because the traced fog is noisy: "now of course we see boiling/artifacts from our path
traced fog (I guess?) which is still super noisy". The grid is the hidden `Traced fog: froxel cache` (frame flag bit 19):
`pt_fog_froxel.hlsl` (a cell's light, reservoirs, the "excess" kept apart), `pt_fog_filter.hlsl`, `pt_fog_integrate.hlsl`,
`pt_fog_tail.hlsl`, `fog_froxel.inl`. The old route is `pt_fog.hlsl`: one point a pixel, the lamps by equiangular
sampling along the ray (`pt_medium.hlsli` `fog_lamps_along`).

What the coordinator found on 2026-10-11 evening (`docs/rounds/round-20.md` "State at 21:10" and below it):

- IN THE GAME THE GRID IS WHITE: Ron's Tavern (514 traced lights), luma 255 on every pixel, with NRD and with no denoiser
  (runs `20261011-185528`, `-204941`, `-205518`, `-210543` in the main tree's `artifacts\harness`). Not the mist's cache,
  not the fill lights, not a stale build, not a descriptor slot (fresh slots: still white). With the cells dark or the
  grid not read the picture is right. With the "excess" not added by the filter the picture is right (luma 17.1 against
  the old route's 17.7); with no excess split it is white again. So some cells hold enormous finite light.
- OFFLINE IT IS REPRODUCED IN SMALL: `TGPT_TEST_FOG_FROXEL_ONLY=1` with the new `TGPT_TEST_FOG_LAMP=<metres>` (a second
  lamp that far in front of the camera). Only sky pixels show the fog offline (the strip above the back wall). The
  sky strip's light, old route / grid: lamp 2 m ahead 0.0408 / 0.0607 (x1.49), 0.3 m 0.1188 / 0.2501 (x2.11), 0.02 m
  0.2268 / 0.5398 (x2.38); without the lamp the two routes agree (0.07555 / 0.07551 over the frame; also with fog boxes
  crossing, with the mist's cache, and with the sun 16,000 times brighter: `TGPT_TEST_FOG_BOXES`, `_INTERSECT`,
  `_CACHE`, `_SCALE`). Leaving out the old proposal (diagnosis 7), the excess (11), the tail (10), the fast path (5)
  changes nothing: the over-count is in what the cells hold of a lamp's light. `<scratch>` scripts are not needed: the
  numbers print as "FOG pixel: mean" / "FOG froxel: mean", and the two means are saved as `tgpt-images\fog-*-mean.f32`
  (1920 x 1080 x 3 floats, bottom row first).
- The offline check asks only for a finite picture. It must compare the two routes.
- TEMPORARY switches the coordinator added for this hunt, all marked "TEMP DIAG": the hidden `Fog grid diagnosis`
  (frame flags bits 24 to 27; `fog_froxel.inl`, the three shaders, Settings.cs, HdrpPasses.cs), `allocate_fresh` and the
  slot log in `fog_froxel.inl`, recipes `fog-froxel-why`, `fog-froxel-diag`, `fog-froxel-slots`, the test's
  `TGPT_TEST_FOG_DIAG`. THEY COME OUT in your job (the descriptor heap's misuse guard in `gpu.hpp` stays).

## What to do

(a) Find the cause first, by measuring offline (minutes a run): why a cell's lamp light is too high. Candidates the
    coordinator did not get to: a lamp's light point-sampled in a cell is 1/d^2 (infinite variance when the lamp is in or
    near the cell; the cells near the camera are needles, millimetres wide and centimetres deep), a 254-frame history
    that never forgets one enormous sample, straight-line interpolation of a field with a pole, the lamp's own radius
    (`light_incident`, `triangle_incident` at points inside or on an emitter), the sample weights of `froxel_lamps`.
    Say which, with numbers.
(b) Fix it so that the grid's fog converges to the old route's (the old route is the yardstick: exact on average), with
    no clamp that cuts light and no cap. One way that keeps the grid's gain (the coordinator's idea, not a ruling): the
    grid holds the part of each lamp's light that is smooth at the cell's size (for a lamp at distance d from the cell's
    point, I / (d^2 + r^2) with r tied to the cell's size), and the rest, I r^2 / (d^2 (d^2 + r^2)), which lives within
    about r of the lamp, is added per pixel by equiangular sampling as the old route does; the two parts sum to the
    lamp's light exactly. Read how Remix treats lights near its cells before choosing (the worker's report lists what of
    Remix is readable), and say what you depart from and why (PT-127's rule: a departure is named).
(c) Make the offline check a comparison: the two routes' sky-strip light within a stated tolerance, without a lamp and
    with one at 2 m, 0.3 m and 0.02 m, and with a candle-like emitting triangle inside the grid. Keep it one small mode
    (`TGPT_TEST_FOG_FROXEL_ONLY`).
(d) Hand the coordinator ONE build for `tavern-room@fog-froxel-b` within your first two hours, or say why not. Remove the
    TEMP DIAG switches in that build.

Not yours: RTX Mega Geometry, the characters' micromaps (worker ommout), NRD, the caustics.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small mode only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (wait with one
  background script, never poll; a cold pass cache needs `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test`
  or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\fogfix-...`; NRD's own CMake writes into its SDK folder: configure as the vendor-rr worker did
  (`docs/agents/vendor-rr.md` "Build isolation") so the main tree's SDK is never written. For any `dotnet` call set
  `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true` and `MSBuildEnableWorkloadResolver=false` first. RTXDI and NRD sources
  are never copied or quoted. Grep and read ranges of the big files (`pt_common.hlsli`, `tests/pathtracing_gpu_test.cpp`,
  `tests/source_checks.py`, `tracer.cpp`). Say every export, record, FrameConstants row, option or flag bit you add. Run
  `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
