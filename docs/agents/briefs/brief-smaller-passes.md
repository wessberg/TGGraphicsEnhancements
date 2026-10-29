# Brief: smaller shader passes, so a new build starts in seconds and not minutes (PT-74) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/smaller-passes.md`; write it early and after every step.

## Why (the player sees: "compiling shaders" for three and a half minutes)

The owner, 2026-10-07 23:00: "PT shouldn't take a minute to activate". Since then made passes are kept on disk and a
line on screen says "compiling shaders (47 of 49)", but the first start after an install or a driver update is what a
player meets first, and it got worse: 210 to 215 s in the game, 515 s inside the GPU test (`docs/PT-REQUESTS.md` PT-74
in the MAIN tree, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\docs\PT-REQUESTS.md`). The same size costs every
build 6 to 10 minutes of shader compiling (side by side, on 14 cores) and every test run its start.

The passes are made side by side, so a start takes as long as the slowest one. Measured cold by the water merge
(`docs/agents/merge-water.md` "The passes' sizes" and "Start-up", in your copy): `giSer` 214 s, `specularSer` 167 s,
`prepareDxr12` 89 s, `walkDxr12` 71 s, `diSpatialDxr12` 11 s, every other pass under 5 s. Their compiled sizes: `pt_gi`
1.4 to 2.0 MB a variant, `pt_specular` 2.0 to 2.8 MB, `pt_prepare` 1.4 to 2.1 MB, `pt_walk` 1.0 to 1.4 MB (on 2026-10-07
18:46, before the game's own material shaders, decals and the walk went in: 0.2, 0.5, 0.26 MB; `docs/agents/speed.md`).

Why they are this big (the coordinator's reading; measure it): a compute pass has no function calls, so every place
that reads what a ray met carries its own whole copy of `hit_material` (`pt_common.hlsli`), and with it every
material program (`pt_materials.hlsli`: architecture, cliff, trunk, terrain's eight layers, triplanar, grass, leaves,
skin), every decal graph (`pt_decal_graphs.hlsli` through `apply_decals`) and the waves. `hit_material(` is called
from `shade_path`, `cache_sample`, `shade_seen` and `subsurface_walk` in `pt_common.hlsli`, twice in
`pt_dielectric.hlsli`, twice in `pt_primary.hlsli`, once in `pt_water_path.hlsli`, twice in `pt_prepare.hlsl`, once in
`pt_compose.hlsl`. The water job already showed the way: one `shade_seen` call for glass, water and reflections took
38 % off the reflections pass.

## What to do

1. **Measure first.** For each of the four big passes: how many copies of `hit_material` it holds and what one copy
   weighs (compile the pass with the function's body cut down, in a scratch folder, and take the difference; or
   count in the DXIL), and what of a copy is the programs, the decal graphs, the skin program, the rest. And what the
   driver's time follows: the size, the count of ray queries, the count of texture reads (try it: the pass cache
   logs each pass's time; `TGPT_TEST_STARTUP_ONLY=1` ends the test after the passes are made). Write the table
   before you change anything.
2. **One place a pass reads a hit's material.** Turn the code that follows a ray (the camera's hit, the glass and
   water it passes, the bounce, the reflection's hit, the cache's ray, the walk's exit) into a loop whose body reads
   the material once, with the path's state saying what to do with it, so that the copies go. Pass by pass, the
   largest gain first; stop where the next step buys little.
   **The picture does not change.** Not a material less at a bounce, not a decal less in a reflection, no bound on a
   path that was not there (the owner's rules: no limits that change the picture; rays see what the camera sees).
   The same random numbers in the same order where you can, so the test's pictures stay bit for bit; where the
   order must change, say so and show the mean is the same.
3. **Do not make what is not used.** The game makes 23 passes on this card and the test 45 (the variants for cards
   without the newest shader model). Say which a given card needs, and make only those at start (the others when a
   card needs them). If the test must make all, let it, but not the game.
4. Say what else would cut the first start and what it costs: making the passes when the game starts before a save
   is loaded (are they already? `tracer.cpp start_passes`), the order they are made in (the slowest first), shipping
   a made cache (it is per driver and card: say why not, if not).

## How it is judged

- Numbers before and after, the same machine state: each big pass's compiled size, its cold time
  (`TGPT_TEST_STARTUP_ONLY=1` with a pass cache folder that never saw the build: the brief's known trap is that the
  driver has its own cache too: say how you made a run cold), the whole start, and the shader build's time
  (`tools\Build-PtShaders.ps1`).
- The picture: the GPU test's checks all as on the base (it has over 1,500), and its pictures compared with the
  base's where you claim "bit for bit".
- GPU time by stage on the test's scenes: no slower (smaller code is often faster; say what it did).
- The whole GPU test ONCE at the end, FAIL lines written down, with `TGPT_TEST_STARTUP_SECONDS=900`. Known on the base:
  ask the coordinator's list in `docs/PT-REQUESTS.md` (main tree, PT-73's note "THE WHOLE GPU TEST OF THAT BUILD", and
  what follows it). A run with failed frames or `last error 0x887A0006` is void: run it again.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or the game folder. GPU tests only
  while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0; wait with one
  background script, never by polling; leave no waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <folder> -Throttle 8` before `cmake --build`.
- Other jobs run beside you in these files: the speed regressions (`scene.cpp`, `tracer.cpp`: the scene stage, the
  micromaps) and NRD's colour (`pt_nrd_prep.hlsl`, `pt_compose.hlsl`, `nrd_denoiser.cpp`). Yours are the path code in
  `pt_common.hlsli`, `pt_dielectric.hlsli` / `pt_interface.hlsli`, `pt_primary.hlsli`, `pt_water_path.hlsli` and the
  passes that call it. A job on tinted glass (PT-76) waits for you: `docs/agents/merge-water.md` section 8 says where
  a tint will enter the shared boundary; leave that place easy to find.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Keep tokens down: grep and read ranges; never read `pt_common.hlsli`, `tests/pathtracing_gpu_test.cpp`, `tracer.cpp`
  or `tests/source_checks.py` whole. The Bash tool eats backslashes: write files with Write/Edit.
- This is a large change to code many checks depend on: commit to small steps (one pass, build, its checks, a line
  in the report), so that a cut-off leaves a tree that builds. No mutation checks. No export, struct or option bit
  should be needed.
- RTXDI and NRD sources are never copied or quoted; nothing copied from Unity/HDRP or the game's shaders.
- Final message: five lines at most, plain words, pointing at the report: the start before and after, what is left.
