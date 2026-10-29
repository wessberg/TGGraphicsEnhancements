# Brief: the water's path as a pass of its own, built for Shader Execution Reordering (PT-99, PT-110) - one worker

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/water-ser-1.md`; write it early, with "State when stopped" at its top after every step.

## The owner, 2026-10-10

"staying near water is like 1 fps" (10-09); "Do not turn that off. If it makes it slower with that on, then it's
either not due to that, or due to us not designing our tech with it in mind"; "The reflections pass being slower
with shader execution reordering instead of faster warrants an additional investigation". AGENTS.md, "Shader
Execution Reordering stays on; the tracer is designed for it": read it first.

## What is measured (read, in your copy: `docs/agents/spec-ser-1.md` WHOLE: its design is the one you build;
`docs/agents/ser-2.md` and `docs/agents/rule-2.md` for how the bounced light's pass was rebuilt; `docs/PT-REQUESTS.md`
PT-99's notes of 2026-10-10)

The bounced light's pass, rebuilt today for reordering (its reuse a pass of its own, one call after the first ray,
nothing carried), went from 42 to 9.9 ms in the tavern and from 62 to 29 at the beach: it is in main and in the game.
The reflections' pass is now the beach's cost: 90 to 110 ms of a 156 ms frame, nearly all of it the water's own path.
The reflections study in the game (runs `20261010-144426`, `-151637`; the same picture in every step), the reflections'
stage in ms:

| Variant | the tavern | the town | the beach |
|---|---|---|---|
| as it ships (16-bit material key, one call outside every loop, 128 bytes) | 9.8 | 24.9 | 92.0 |
| the study's own build of today's key | 9.9 | 24.4 | 84.9 |
| the material's key in 8 bits | 9.8 | 24.0 | 84.9 |
| `ser2`'s 3-bit key (its whole loss) | 10.8 | 33.4 | 84.8 |
| NO hint at all | 10.7 | 30.8 | **70.8** |
| the water's path regrouped at every turn INSIDE this pass (2,624 bytes carried) | 10.7 | 24.7 | 106.3 |
| the plain compute pass (the yardstick) | 12.9 | 29.0 | 192.3 |

So: the material's key is right for what is not water (the town); for the water a hint-less sort is far better (70.8
against 85 to 92); a regrouping inside the pass's water loop costs, because of what it carries. Your predecessor's
design: the water's path a PASS OF ITS OWN, regrouped at every turn, each thread carrying the path alone (about 76
bytes) and a hit object, the hit fetched after the call, a key by what the turn does next.

## The job

1. **Build that design** behind one compile switch (`TGPT_WATER_PASS`, OFF by default: a build without it is today's
   to the byte) and one `Speed trial` number (and, if you build more than one arrangement, one number each; name them):
   the reflections' pass hands every pixel whose path enters the water to the new pass (a compact list, or a screen
   mask: say which and why), and does the rest as today, with its material key in 8 bits (16 bits if you find a
   reason). The water's pass: a loop of turns, ONE regrouping call a turn at the top of the loop where a thread carries
   only the path's state; the driver's stack figure (the start-up line every build writes) is your measure, and it
   must be small. The same computation: the same random numbers, the same turns, the same picture.
2. **Watch the compiler's traps** (found today, `ser-2.md` section 14, `spec-ser-1.md` 3.4): a regrouping call whose
   hint is a different constant on different paths is copied into each path (use one call with a computed hint); a
   call inside a loop of a big pass keeps everything live across it. Count the calls and the bytes in the compiled
   listing (`<scratch>\ser1-tools`, `<scratch>\ser2-tools`).
3. **HAND-OVER within two hours, then STOP**: what the coordinator builds, the recipe (scenes `water-day`, `metal`,
   `tavern-room`; no denoiser, 16 frames, 25 s a step; as it ships first and last, your arrangements between), what
   each step must read. TARGET: the beach's reflections well under 70 ms (no hint gave 70.8 inside today's pass);
   the town's and the tavern's no slower than today.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- Reordering is never switched off by what ships. The picture does not change.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 3` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON` and your switch; copy the main tree's
  `build\native\generated\pathtracing\*.h` into your build folder's `generated\pathtracing\` right after configuring,
  so that only your passes compile); never `-First`, never a raised limit.
- Offline: never the whole GPU test or ctest as a whole; a worker's test cannot make a new reordering pass under its
  8 GB ceiling: do not try. The game is the test.
- RTXDI and NRD sources never copied or quoted.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit, check
  every hunk landed.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- `python tests/source_checks.py` passes; the plugin builds. Never describe anything with the word the source checks
  ban (`test_testing_limits_disclosed`): say "prototype" or "on trial".
- Final message: six lines at most: what is built and behind which switch, calls and bytes, the recipe and what each
  step must read, what of today's pass goes when it wins, that it compiles and the checks pass, the report's path.
