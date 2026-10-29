# Brief: glass: the view through a jar's shell is warped with the trial on (PT-13, PT-35) - one worker, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/glass-4.md`; write it early and after every step.

## What the game answered (2026-10-09 14:38, run `20261009-142733`, scene `refract-jar`, recipe `glass-trial`)

Read first, in your copy: `docs/agents/glass-3.md` sections 1 and 2 (the job before yours: why the jars were cloudy,
the trial's bits), and `docs/PT-REQUESTS.md` PT-13's newest note and PT-100 (grep the headings). The evidence is in
the MAIN tree, read only: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261009-142733\`:
`jars-game-now-trial.jpg`, `jar-close-game-nrd-trial.jpg`, the steps' frames under `refract-jar\<step>\` (`step.json`
names them; raw RGBA bytes, bottom row first, `width` x `height`), `TGPathTracing.Native.log`.

- `Advanced / Glass trial` 7 with no denoiser: the wall is seen through the jars, as in the game (at 0 they are
  cloudy). The direction is right, and the frame is faster (123.3 ms at 0, 97.3 at 7).
- THE FLAW, yours: at 7 the view through a jar is warped over its whole body: horizontal streaks at the shoulder,
  dark upright bands about a third in from each side, the wall's stones shifted in blocks. The game shows the
  wall through the jar unbent. The jar's body is a closed shell 4 to 5.5 mm thick (your predecessor's measure of
  `Mesh_Prop_Jars_01`): glass of index 1.5 that thin bends the view by a pixel or two through its middle and
  strongly only in the last few hundredths of its outline. So the walk through a closed piece does something a
  thin shell does not. Not known which: which face counts as an exit (the shell's inner wall is met from inside
  the glass, then the hollow, then the far inner wall from outside the glass); the smooth normal against the flat
  triangle it belongs to (bit 1 bends by the smooth normal: two walls with different tessellation no longer
  cancel); the lid's open piece sitting inside or against the body; total reflection inside the wall at a slant;
  the film's layer. Find it with numbers, do not guess.
- Not yours: with NRD everything behind glass is a blur at 0 and at 7 alike (PT-100). Another worker is in the
  NRD files now. Do not touch `pt_nrd_prep.hlsl`, `nrd_denoiser.*` or the guides; judge with Denoising "None".

## The job

1. Reproduce it offline, small: the game's own jar if your predecessor's tools can load it (`<scratch>\glass3-tools`,
   `<scratch>\jarscan`, where `<scratch>` is the folder above `wt`), else a made shell of the jar's measures (27 cm
   across, 49 cm tall, walls 5 mm, an inner and an outer wall with different tessellation, smooth normals) before a
   wall of straight stripes. Trial 7. The number: how far a stripe seen through the middle half of the shell is from
   where it is seen beside the shell, in pixels, and how much of its contrast is kept. A thin shell: about nothing.
2. Find the cause and fix it behind the trial's number. Bits 1, 2 and 4 keep their meaning; your fix rides on a
   value you choose and say (bit 8, the fixed fault's mutant, may be given up for it: then say how its offline
   check is mutated instead). Default 0 stays the picture as before.
3. Say what a jar should look like then, by the physics: what bends at the outline, how wide that rim is in
   pixels at the scene's distance, what the film does to the light through it once a wall.
4. STOP at the hand-over: the recipe for `tests/ingame/scenarios.json` (as `glass-trial`: the game; no denoiser at
   0, at 7 and at your value; 16 frames each; no NRD steps), and what the run should show. The coordinator runs
   it in the game: the game is the first test, so hand over the SMALLEST change that could show the direction
   within your first hour, written into the report, before you polish anything.

## Rules

- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small check in its
  `..._ONLY` mode only, through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the
  game runs (`artifacts\harness\RUNNING` in the main tree held by a live process: wait with one background script).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\COPYNAME` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it>` before `cmake --build` (it holds the PC-wide
  limits itself: leave its defaults). Build the plugin too (0 errors, 0 warnings). `python tests/source_checks.py`
  passes.
- No fakes, no limits that change the picture, nothing screen-space: glass is a surface of the tracer's own rays,
  and a mirror shows a jar as the camera does.
- All 32 option bits are used, and the lamps' and the frame's spare bits are taken (glass: the low 4 bits of
  `TGPT_Frame.cameraPosition`'s w; water: the next 3). No ABI or size change.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. Another worker is in the
  water's files and `pt_common.hlsli` too: change what you must, move nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: the cause with its number, the trial's value and what it does, the recipe and
  the run's command, what was checked offline with its numbers, what is left, that it compiles and the checks pass,
  the report's path.
