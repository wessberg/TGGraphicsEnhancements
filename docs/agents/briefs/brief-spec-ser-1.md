# Brief: the reflections' pass and Shader Execution Reordering (PT-99, PT-110) - one worker, measure first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/spec-ser-1.md`; write it early, with "State when stopped" at its top after every step.

## The owner, 2026-10-10

"The reflections pass being slower with shader execution reordering instead of faster warrants an additional
investigation." And the ruling (AGENTS.md, "Shader Execution Reordering stays on; the tracer is designed for it"):
where a pass is slower with it, the fault is in our design. Read that bullet first.

## What is measured (read, in your copy: `docs/agents/ser-study-run.md`, `docs/agents/ser-2.md` sections 1, 6 and 7,
`docs/agents/compat-1.md` entries S1 to S5 and S7, `docs/PT-REQUESTS.md` PT-99's notes of 2026-10-10)

The reflections' pass (`pt_specular.hlsl`; its regrouping near line 126: `PT_REORDER(regroup_key(shade ?
seen.material : 0xFFFFu, cache_feeds(p, 2u)))`, the material's number and the cache's feed in 16 bits), ms:

| | the tavern | the town | the beach |
|---|---|---|---|
| as it ships (reordering, today's key) | 9.3 to 9.6 | 24.0 to 24.7 | 92.9 to 95.4 |
| no regrouping call compiled in (`ser1`'s study, 16) | 10.0 | 31.1 | 125.4 |
| the plain compute pass (the yardstick) | 12.6 to 12.7 | 27.7 to 28.4 | 193.8 to 195.2 |
| `ser2`'s trial: a 3-bit key (plain / water / glass / nothing, and the feed) AND its guide stored once | 10.3 to 10.4 | 32.8 to 33.8 | 99.6 to 101.1 |

The driver's stack a thread for this pass: 128 bytes (as it ships and in `ser2`'s trial). So as it ships the pass IS
faster with reordering than without, everywhere; but `ser2`'s two changes made it slower than as it ships by 9 ms in
the town and 7 at the beach, and NOBODY KNOWS WHICH OF THE TWO, OR WHY. And at the beach this pass is the frame's
largest cost by far: 93 ms of 145 to 169, nearly all of it the water's own path (`water_path`, `pt_water_path.hlsli`,
`pt_water_sun.hlsli`), which regroups once for a loop with no fixed end (the review's S5; `perf2` built
`TGPT_REGROUP_WATER` for it, never run in the game).

## The job

1. **By reading, first (half an hour, into the report)**: the pass as it ships: where its call stands (inside a loop?
   before the water's path or the walk through glass begin?), how many calls the compiler made of it (count them in
   the compiled listing: `ser1`'s and `ser2`'s tools are in `<scratch>\ser1-tools`, `<scratch>\ser2-tools`), what a
   thread carries across each, and what `ser2`'s 3-bit key and its "guide stored once" each change in that.
   Microsoft's specification and NVIDIA's guidance on hint bits (the highest bits matter first; how many with and
   without a hit object): write what they say.
2. **ONE study build for one game run (hand-over 1 within your first hour and a half)**, behind one compile switch
   `TGPT_SPEC_STUDY` (OFF by default: a build without it is today's to the byte) and `Speed trial` numbers you name,
   with at least these variants of the reflections' pass, each the same computation:
   a. today's key, the guide stored once (`ser2`'s second change alone);
   b. the 3-bit key alone (`ser2`'s first change alone);
   c. today's key in the HIGH bits (the material's number shifted up, NVIDIA's advice), 16 bits;
   d. fewer bits of today's key (8, 4), and no hint at all;
   e. the water's path regrouped at every turn (`TGPT_REGROUP_WATER`'s regrouping, only in this pass), with what a
      thread carries at that point counted.
   The log line with the driver's stack for each variant (as `ser1`'s study did). The recipe: scenes `tavern-room`,
   `metal`, `water-day`; no denoiser, 16 frames, 25 s; "as it ships" first and last; the plain yardstick. What you
   expect of each, written BEFORE the run.
3. **The design, written** by what reading can say now, finished when the run is in: the reflections' pass and the
   water's path built for reordering (the bounced light's pass got from 2,592 bytes and a call inside its loop to
   0 bytes and one call outside it, and fell from 42 to 9.5 ms in the tavern and from 62 to 29 at the beach:
   `ser-2.md`). Target: under today's 9.3 / 24.0 / 93 ms, the beach by tens of ms.
4. STOP at hand-over 1 and the written design.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- Reordering is never switched off by what ships. The plain passes are a yardstick. The picture does not change.
- ANOTHER WORKER (`rule2`) is at this moment taking `ser2`'s reflections trial out of `pt_specular.hlsl` (its 3-bit
  key, `split_regroup_seen`, the guide's one store) and the study code out of the tree, and making the bounced
  light's split pass the rule. Do not touch the bounced light's pass. Build your variants in a file of your own
  (`pt_spec_study.hlsli`) and keep your lines in `pt_specular.hlsl`, `tracer.cpp` and `CMakeLists.txt` few and
  inside your switch, so that the merge after `rule2` is easy; recreate what you need of `ser2`'s two changes
  inside your study file.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 3` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON` and your switch; copy the main tree's
  `build\native\generated\pathtracing\*.h` into your build folder's `generated\pathtracing\` right after
  configuring, so that only your variants compile); never `-First`, never a raised limit. A reordering pass takes
  5 to 9 minutes of a core: build only the variants that answer the questions.
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
- Final message: six lines at most: what reading found (calls, bytes, place), the variants and their numbers, the
  recipe, the design in two lines, that it compiles and the checks pass, the report's path.
