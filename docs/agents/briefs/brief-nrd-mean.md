# Brief: NRD must not change what the picture converges to (PT-75) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/nrd-mean.md`; write it early and after every step.

## Why

The owner judges the picture with no denoiser first, then with NRD ("The NRD denoiser should look relatively
noise-free"; "we need to catch boiling with NRD and prevent that too"). A denoiser may take noise away; it may not
move the mean. It does: read `docs/PT-REQUESTS.md` PT-75 in the MAIN tree
(`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\docs\PT-REQUESTS.md`, grep `### PT-75`). In short, from the in-game
run `artifacts\harness\20261008-015158` (main tree; frozen world, 16 kept frames a step, compared in linear light by
`tools/harness/mean_colour.py <scenario>\2-traced-none <scenario>\3-traced-nrd`):

- `vista` (the automatic gain is x1.0 in both steps): in the shade NRD's light is R 0.82, G 0.50, B 0.36 of the
  undenoised mean on brown ground; R 0.70, G 0.57, B 0.23 on a metal rim; the worst blocks have B 0.01 and R 0.42; the
  sky 1.000. 56 % of the lit 64-pixel blocks differ by more than 10 % in a channel. Sun-lit parts barely move.
- `candle`: 50 % of the blocks; a dim floor R 0.70, G 0.51, B 0.47. `forest` (grass and leaves, which NRD is not given:
  `pt_nrd_prep.hlsl` skips what is not `deferred`): 3.6 %.
- It was the same the day before (`artifacts\harness\20261007-231354\candle`): not from the night's merges.

What NRD is given and how its result comes back: `native/pathtracing/shaders/pt_nrd_prep.hlsl` (94 lines, read it
whole: the bounced light only, divided by the surface's diffuse colour, in RELAX's directional form, held at
`NRD_LIMIT` with the rest added back in `pt_compose.hlsl` lines 194 to 203), `native/pathtracing/src/nrd_denoiser.cpp`
(the settings, the texture formats, the units) and `tracer.cpp` where the units are set from the histogram.

It is the denoiser's path and not time: the recipe `mean` (no denoiser, NRD, no denoiser again; run
`artifacts\harness\20261008-042521`) gives the same undenoised picture twice (blocks differing: vista 0.2 %, candle
0.4 %, tavern 6 %) and NRD's apart from both (56 %, 53 %, 99 %). And the two shifts have a shape: in the tavern
(warm candle light) green stays (0.98) while red falls (0.80) and blue rises (1.51): one colour difference (red
minus blue) shrinks, the brightness stays. In the vista's shade the three channels lose about the same amount. Both
fit brightness and colour differences being filtered apart, the differences more widely and across edges of
brightness (a dark place beside a bright one takes the bright one's colour difference; blue goes below nothing and
is clamped). Test that first: a dark coloured floor beside a bright one.

The coordinator's earlier reading, NOT proven: red is kept and blue goes to nothing where the light is dim and noisy. That is
what happens when a colour is carried as brightness plus two colour differences, the brightness is cut by a step that
is not linear (a firefly clamp, a history clamp, a value too small for a half float's square) while the differences
are not, and the result's negative channel is then clamped to zero (`max(..., 0.0)` in mode 2 of `pt_nrd_prep`).
Other candidates: a texture format that cannot hold what is written into it; the units (the median at a half) leaving
dim places near a half float's floor; a setting of RELAX that trades bias for stability.

## What to do

1. **Reproduce it offline first** (`tests/pathtracing_gpu_test.cpp`, a check of your own with a `..._ONLY` switch): a
   floor with a strong colour (brown: 0.45, 0.2, 0.05) lit by bounced light only, dim and noisy (light through a small
   opening or off a small bright patch), and a tinted metal beside it; the mean of N undenoised frames against the
   mean of N frames with NRD, a channel, in linear light, after the same warm-up. State the tolerance from the
   undenoised frames' own spread.
2. **Find the cause with numbers**, one variable at a time (count the pixels where the result has a negative channel
   before the clamp; the firefly filter on and off; the directional form against the plain one; the units a few stops
   up and down; history length 1 against the default). Do not guess from the source alone.
3. **Fix it at the cause.** The rule: no light is cut and no colour moved; what a filter cannot hold comes back
   undenoised, as `NRD_LIMIT`'s rest already does. Not a colour correction after the fact, not a blend toward the
   undenoised frame that only hides it. If the unbiased setting is noisier, say by how much (`tools/harness/flicker.py`
   measures in game: the coordinator runs it): the owner decides trade-offs between noise and bias, not a worker.
4. Say what the same finding means for the reflections' and the direct light's own filters (they are the tracer's,
   not NRD's): the same check on them, as a number, without changing them unless they fail it.

## The one exception to the build rule: this job builds WITH NRD

Workers build with `-DTGPT_WITH_NRD=OFF` because NRD's configure step writes `Shaders/NRDConfig.hlsli` into the shared
SDK folder. This job cannot: build with `-DTGPT_WITH_NRD=ON` and the same NRD options as the main tree
(`build\native\CMakeCache.txt` there: `NRD_NORMAL_ENCODING=2`, `NRD_ROUGHNESS_ENCODING=1`, the rest as listed), so the
file it writes is the same file. You are the only worker allowed to. Do not change NRD's options; if a different option
turns out to be the fix, stop and write it in the report for the coordinator. NRD and RTXDI sources are never copied
or quoted into the repo or the report: describe what a step does in your own words, with the SDK's public header
names only.

## How it is judged

- Your offline check: NRD's mean within the tolerance of the undenoised mean, a channel, on the floor and the metal.
- The whole GPU test ONCE at the end, FAIL lines written down. A run with failed frames or `last error 0x887A0006`
  is void (GPU overloaded): run it again. A run that fails only at "the passes were made" runs once more.
- In game the coordinator runs `vista@quick`, `candle@quick`, `glass-jars@quick` and `mean_colour.py` on each: say what
  the numbers above should become, and what `flicker.py --boil` may do.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or the game folder. GPU tests only
  while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0; wait with one
  background script, never by polling; leave no waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...`.
- Compile the shader passes side by side before `cmake --build` (a full build is an hour otherwise):
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\Build-PtShaders.ps1 -BuildDir <your build folder> -Throttle 8`.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Keep tokens down: grep and read ranges; never read `tests/pathtracing_gpu_test.cpp`, `pt_common.hlsli`, `tracer.cpp`
  or `tests/source_checks.py` whole. The Bash tool eats backslashes: write files with Write/Edit.
- Settled choices have reasons: before changing a constant of the NRD path, grep its comment, `CHANGELOG.md` and
  `tests/source_checks.py` for why it is so (`pt_nrd_prep.hlsl`'s header names several harness findings), and keep
  what they fixed fixed.
- No mutation checks. No export, struct or option bit should be needed; say so if one is.
- Final message: five lines at most, plain words, pointing at the report: what is built, what passed, what is left.
