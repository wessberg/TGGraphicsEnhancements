# Brief: the bounced light's pass as the game measured it best becomes the rule (PT-99, PT-110) - one worker, tidy

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/rule-2.md`; write it early, with "State when stopped" at its top after every step.

## Why

AGENTS.md: once the game has answered, a trial "comes out or becomes the rule THE SAME DAY"; and "Shader Execution
Reordering stays on; the tracer is designed for it". The game answered on 2026-10-10 13:34 (run
`20261010-130621`, `ser2`'s split trial; read in your copy `docs/agents/ser-2.md` sections 1, 5, 6, 7, 8 and
`docs/PT-REQUESTS.md` PT-99's newest note). ms, a frame / the bounced light's stage / the reflections', no denoiser,
the same picture in every step:

| Step (`Speed trial`) | the tavern | the town (`metal`) | the beach |
|---|---|---|---|
| as it ships (0) | 65.0 / 41.9 / 9.6 | 101.2 / 57.7 / 24.7 | 169.2 / 61.5 / 92.9 |
| the plain compute passes (8) | 39.9 / 13.7 / 12.7 | 74.4 / 27.6 / 27.7 | 274.4 / 64.6 / 195.2 |
| split, regrouped at the top (32) | 33.4 / 9.6 / 10.4 | 75.5 / 22.9 / 33.7 | 145.0 / 29.5 / 100.9 |
| **split, regrouped after the first ray (48)** | **33.4 / 9.5** / 10.4 | **75.3 / 22.7** / 33.8 | **144.7 / 28.9** / 101.1 |
| split, only water and glass regrouped (64) | 36.9 / 13.1 / 10.3 | 83.9 / 32.3 / 32.8 | 146.1 / 31.8 / 99.6 |

The driver's own stack a thread: 48 and 32 carry 0 bytes (today's pass 2,592; the reflections' 128). So "ahead"
(48) is faster than the plain yardstick in every scene: reordering, designed for, wins everywhere. BUT the
reflections' pass in steps 32 to 64 ran with `ser2`'s 3-bit key and its guide stored once, and was WORSE: the town
33.8 against 24.7, the beach 101 against 93. So the reflections' pass stays EXACTLY as it ships.

## The job

| What | Becomes |
|---|---|
| the bounced light's pass split (the reuse across frames in `pt_gi_reuse`, a pass of its own) with ONE hint-less regrouping after the first ray, outside the rays' loop ("ahead", 48) | THE RULE: the only form, in every build, for the reordering passes; the compute variants (Shader Model 6.6 and 6.9, cards without reordering) take the split form too, so one computation is written once (`ser-2.md` section 8) |
| the hint-less call INSIDE the loop (today's shipped line of 12:12), "top" (32), "gain" (64), "split" (16), "split as plain" (24) | OUT |
| the reflections' 3-bit key and its guide stored once (`split_regroup_seen` and what only it needs) | OUT: the reflections' pass as it ships today, to the byte where it can be |
| CMake `TGPT_SER_SPLIT` and every `#ifdef` of it, the `Speed trial` numbers 16 to 64 as split trials, the recipe `ser-split` | OUT |
| the reordering study (`TGPT_SER_STUDY`, `pt_ser_study.hlsli`, its recipes `ser-study`, `ser-study-keys`, its check lines; `ser-1.md` lists them) | OUT (the study has answered) |
| the start-up line with the driver's stack of the shipped passes | STAYS in every build |

And `docs/PROJECT-CONTEXT.md` gets the learning in at most four lines, in its section on the tracer's passes (9.x;
replace what it says of reordering being "within noise"): a regrouping call never stands inside a loop of a big
pass, nor before work in the screen's order; what a thread carries across it is the cost (the driver's stack line
is the measure); with that, regrouping beat the plain pass in every scene (the table above). `CHANGELOG.md`: one
entry with the table. Every other file that names what goes follows (source checks become checks of the rule; the
docs in `docs/agents/` only by a line at their top).

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- NO NEW BEHAVIOUR: with the job done, a default build does what today's build does with `Speed trial` 48 (and the
  reflections' pass as it ships). Say in the report how you checked that (the text the compiler sees, pass by pass,
  against the copy's base with the right defines, as the worker `rule1` did).
- Every pass compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 3` (configured with
  `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON`), never `-First`, never a raised limit; the DLL links; the plugin
  builds (0 errors, 0 warnings); `python tests/source_checks.py` passes. No GPU test: the coordinator's run of
  `tavern-room`, `metal`, `water-day` (`structures`, defaults) is the test; say what each must read.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed.
- RTXDI and NRD sources never copied or quoted.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: six lines at most: what is now the rule, what is out, how "no new behaviour" was checked, what the
  run must read, that every pass compiles and the checks pass, the report's path.
