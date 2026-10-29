# Brief: the filters must keep the light and a mirror's picture (PT-75 finished, PT-80 a) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/filters.md`; write it early and after every step.

## Why

The owner judges the picture with no denoiser first, then with NRD, which "should look relatively noise-free" and
must not boil. Two things are wrong with what stands between the tracer's samples and the picture when Denoising is
NRD (NRD takes the bounced light; the direct light and the reflections go through the tracer's own filters):

1. **Light is lost and colours move (PT-75).** An agent measured it and was cut off before fixing it: read its report
   whole, `...\scratchpad\wt\nrdmean\docs\agents\nrd-mean.md` (its copy, base52, READ ONLY; it holds investigation code
   marked `PT75-DIAG` / `pt75` and a check `nrdMean`, `TGPT_TEST_NRD_MEAN_ONLY=1`; its short runs' outputs are in
   `%TEMP%\tgb\nrdmean-out\`, some unread). In short: NRD's picture holds 5 to 24 % less light than the undenoised mean;
   RELAX's anti-firefly filter, which we turn on, cuts about 5 points; the tracer's OWN direct-light and reflection
   filters lose more (0.76 on a candle-lit floor, 0.66 to 0.69 on metal); weighing brightness against both pixels'
   noise in `pt_atrous.hlsl` brought the direct light to 0.993; the normal map's shading put back after NRD is
   suspected for the last 1.5 to 5 %.
   The owner asked (2026-10-08): "isn't it just because it actually cleans up some of the really noisy parts of the
   image?" The answer given: taking noise away does not move the mean; the loss sits in the noisy places because
   there the light arrives as rare bright samples and the filters throw those out. So: nothing thrown out. What a
   filter cannot hold comes back undenoised (as `NRD_LIMIT`'s rest already does in `pt_compose.hlsl`).
2. **With NRD a mirror's picture is smeared (PT-80).** Evidence in the MAIN tree (read only):
   `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261008-111121\mirror-tavern\mirror.jpg` (the
   strip: the game's view from the mirror's side, the tracer's, the mirror with no denoiser, the difference, WITH NRD,
   the game's own mirror) and `mirror.md` beside it: detail against the reference 1.24 with no denoiser, 0.09 with
   NRD; in all four mirror scenes 0.02 to 0.12. The reflections' filter treats a perfect mirror's picture as noise on
   a flat surface. The owner: "In principle, we need our tracer to be able to perfectly render the world in a mirror".
   A reflection is filtered by what it SHOWS (the reflected thing's depth, normal and colour, its own motion), never
   across the mirror's flat face; a perfect mirror's single sample a pixel needs no spatial filter at all.

## What to do

1. Port what is right from the nrdmean copy into yours by hand (your base is newer: main with the lantern's hold, who
   walks, water, the speed job; ABI 0x00012600): the `nrdMean` check, and the fixes its report lists under "Left for a
   finisher", each with its number: the anti-firefly filter off (and what that does to boiling: measure it, the owner
   decides a trade), the a-trous weights, the normal map's shading after NRD (settle it: its run `s-linear-flat`).
   None of its `PT75-DIAG` code.
2. The mirror: an offline check first (a mirror facing a sharp checkerboard and a textured wall, the mirror's picture
   with NRD against with no denoiser: detail within 10 %, mean within the noise), then the fix in the reflections'
   filter (`pt_atrous.hlsl`, `pt_specular.hlsl`, the temporal pass) so that roughness decides how much is filtered and
   the reflected surface decides across what.
3. The same two measures on the built-in route and on what Ray Reconstruction is handed, as numbers; change them only
   where they fail.
4. For the coordinator's game runs (`mirror-tavern@mirror-check`, `vista@mean`, `candle@quick`, `glass-jars@quick`): what
   `mirror.py`'s detail and `mean_colour.py`'s shares should become, and what `flicker.py --boil` may do.

## The one exception to the build rule: this job builds WITH NRD

Workers build with `-DTGPT_WITH_NRD=OFF` because NRD's configure step writes `Shaders/NRDConfig.hlsli` into the shared
SDK folder. This job cannot: build with `-DTGPT_WITH_NRD=ON` and the same NRD options as the main tree
(`build\native\CMakeCache.txt` there), so the file it writes is the same file. Do not change NRD's options; if a
different option is the fix, stop and write it in the report. NRD and RTXDI sources are never copied or quoted into
the repo or the report: describe what a step does in your own words, with the SDK's public header names only.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree, to `wt\nrdmean` or the game folder.
  GPU tests only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0;
  wait with one background script, never by polling; leave no waiting shell behind. Build into
  `%TEMP%\tgb\COPYNAME-...`; `tools\Build-PtShaders.ps1 -BuildDir <folder> -Throttle 8` before `cmake --build` (or a
  Ninja tree). `TGPT_TEST_STARTUP_SECONDS=900` for every GPU run.
- Another agent works beside you on the water (`pt_water_*`, `pt_interface.hlsli`, `scene_water.cpp`, the caustics):
  not yours.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Settled choices have reasons: before changing a constant of the filters, grep its comment, `CHANGELOG.md` and
  `tests/source_checks.py` for why it is so, and keep what it fixed fixed (or show with numbers that yours does).
- Keep tokens down: grep and read ranges; never read `pt_common.hlsli`, `tracer.cpp`, `tests/pathtracing_gpu_test.cpp`
  or `tests/source_checks.py` whole. The Bash tool eats backslashes: use Write/Edit.
- No colour correction after the fact, no blend toward the undenoised frame that only hides a shift, no denoiser
  tuned to hide a defect of the tracer's. No mutation checks. No export, struct or option bit should be needed.
- The whole GPU test ONCE at the end, FAIL lines written down (known on the base: "skin 110 m from scene origin", the
  glass index check, three tinted-glass checks, the lake's 33.7 ms). A run with failed frames or `last error
  0x887A0006` is void: run it again. Start it as soon as the code is final.
- Final message: five lines at most, plain words, pointing at the report: what is built, what passed, what is left.
