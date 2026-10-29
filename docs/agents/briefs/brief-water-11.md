# Brief: water: the shore's foam is one white slab in the game; the light under water without the view becomes the rule (PT-81, PT-42, PT-94) - one worker, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/water-11.md`; write it early and after every step.

## What the game answered (2026-10-09 14:41, run `20261009-142733`, scene `water-day`, recipe `water10`)

Read first, in your copy: `docs/agents/water-10.md` (the job before yours: its three switches and how each is
built) and `docs/PT-REQUESTS.md` PT-81's newest note, PT-94, PT-42 (grep the headings). The evidence is in the MAIN
tree, read only: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261009-142733\`:
`water-game-off-foam.jpg`, `water-off-lamps-noview.jpg`, `water-bed-game-off-noview.jpg`, the steps' frames under
`water-day\<step>\` (`step.json` names them; raw RGBA bytes, bottom row first, `width` x `height`; `1-game` is the
game's own picture), `report.md`, `TGPathTracing.Native.log`.

- `Advanced / Water light without view` IS RIGHT: the dark slab of sea bed with its hard edge at the shore is gone.
  A patch of bed under shallow water, levels of red: the game 68, the switch off 28, on 51. It costs about 17 ms
  a frame on the beach (173.5 to 190.8 ms beside the lamps' switch), and the harness flags the step boiling (0.65
  levels between frames in 16-pixel blocks, 20 times the game's own picture).
- `Advanced / Water shore foam` IS WRONG: ONE SOLID WHITE SLAB over the whole wet band of the shore, some 10 m
  deep, and solid white blobs farther out. The game has thin, broken lines of foam along the run-up and on the
  wave crests, a few hundredths of the water's pixels. Your predecessor's fixture said "foam 1 / 0": it did not
  ask how MUCH of a real beach turns white.
- `Advanced / Water body lamps`: no change by day (no lamp near). Not yours now.

## The job, in this order

1. **The foam.** Find out, with numbers from the run's frames and the game's own bed, why the whole shallow band
   is foam (a guess, not proven: the rule turns white wherever the bed is near under the surface, and a beach is
   shallow for metres; or the run-up's memory never ages). The measure to hold yourself to: the share of the
   water's pixels that are foam in the game's picture of that scene (count it from `1-game`: near-white, low
   colour, on water) against yours; the game's is the yardstick, not a number to copy pixel by pixel. Foam is
   where water BREAKS or has just run up and is draining: a wave's steep front, the line the run-up reached and
   the seconds after, a rock's wash; it thins and breaks into the bands on open water with no seam. Still our own
   formula from what the tracer knows (the bed by a ray, the waves' state, where the water stood, time): nothing
   screen-space, no port of Unity's foam, no ready-made pattern standing in for it.
2. **The light under water wherever a path asks for it becomes the rule**: no switch, the `in_view` limit and
   what only it needed removed with their settings, tests' mentions and docs ("Remove the workarounds as we go").
   Then make it cheaper WITHOUT changing what it converges to (say where the 17 ms go; a budget may cost noise or
   speed, never light), and look at why it boils more (what changes between frames in a frozen world).
3. STOP at the hand-over: one recipe for `tests/ingame/scenarios.json` (`water-day`: the game; no denoiser with
   the foam off and on; NRD with the foam on; 16 frames; no more steps than answer the question), what the run
   should show, and the foam's share of the water's pixels you expect. The coordinator runs it in the game: the
   game is the first test, so hand over the SMALLEST change that could show the foam's direction within your first
   hour, written into the report, before you polish anything.

## Rules

- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small checks in
  their `..._ONLY` modes only (the sea fixture has parts: `TGPT_TEST_SEA_PART`), through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs (`artifacts\harness\RUNNING` in the
  main tree held by a live process: wait with one background script).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\COPYNAME` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it>` before `cmake --build` (it holds the PC-wide
  limits itself: leave its defaults). Build the plugin too (0 errors, 0 warnings). `python tests/source_checks.py`
  passes.
- No fakes, no limits that change the picture, nothing that depends on what is on screen. The beach runs at 5 to 6
  frames a second (158 ms with the trials off): say what each step costs; another worker is on speed, do not spend
  the job on it beyond step 2.
- All 32 option bits are used (glass: the low 4 bits of `TGPT_Frame.cameraPosition`'s w; water: the next 3, of
  which the no-view one comes free with step 2). No ABI or size change.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. Another worker is in the
  glass's files and `pt_common.hlsli` too: change what you must, move nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`; the NRD files.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: the foam's cause with its number, what the foam is now and its share against
  the game's, what the no-view rule costs now, the recipe and the run's command, what was checked offline, what is
  left, that it compiles and the checks pass, the report's path.
