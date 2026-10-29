# Brief: the bounced light's pass rebuilt for Shader Execution Reordering, step 1 and 2 (PT-99, PT-110) - one worker

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/ser-2.md`; write it early, with "State when stopped" at its top after every step.

## The owner, 2026-10-10

"Do not turn that off. If it makes it slower with that on, then it's either not due to that, or due to us not
designing our tech with it in mind." "definitely investigate if there's anything we can do to any of our rendering
to make it more compatible with Shader Execution Reordering, Opacity Micromaps, and RTXDI." The ruling: AGENTS.md,
"Shader Execution Reordering stays on; the tracer is designed for it". READ IT FIRST.

## What is known (read, in your copy, in this order)

1. `docs/agents/ser-1.md`: the worker before you. Its section 6 is THE DESIGN you build; its sections 3 and 5 say
   what it counted in the compiled passes (three regrouping calls where the source has one; 545 bytes carried
   across it, 205 in its rearranged variants) and what it expects of each study variant.
2. `docs/agents/compat-1.md`: a review. Its entries R1, S1, S2, S3, S4 and S7 are about this pass (grep those
   labels): RTXDI's reuse across frames runs INSIDE the reordered pass after the regrouping (`pt_gi.hlsl` near
   lines 83 and 131 to 148), where NVIDIA's own sample keeps it a pass of its own and NVIDIA's guidance warns of
   exactly this; the regrouping compiles to the hit-object form with an EMPTY hit object and a 16-bit hint.
3. `docs/PT-REQUESTS.md` PT-99, its notes of 2026-10-10 (grep the heading): the game's numbers. ms, the bounced
   light's pass: the tavern 56.0 with reordering, 13.9 as a plain compute pass (the yardstick); a town 60.9 and
   27.8; the beach 42.6 and 70.8. With no ray at all the reordered pass is still 14.4 ms in the tavern.
4. THE STUDY RUN is in the game as you start (the study build of `ser1`, scene `tavern-room`, recipe `ser-study`).
   Its result will be in the MAIN tree as
   `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\docs\agents\ser-study-run.md` (read only). Start step 1 at
   once; wait for that file with ONE background script, and read it BEFORE you choose how step 2 is built.

## The job

1. **Step 1 of the design: the sorted part ends after the shading.** The reuse across frames (RTXDI's temporal
   resampling of the bounced light's reservoirs, the store) and everything else that works in screen order moves
   out of the ray generation pass into a plain screen-order pass that runs after it (compute; no new memory if
   the candidate can travel in what is there; say what it needs). The reordered pass then: the ray, the
   regrouping, the shading, the candidate written. The same numbers as today: the same random numbers, the same
   candidate, the same resampling, so that the picture is the same to the noise.
2. **Step 2: the regrouping where a thread carries least, by what it will do.** Right after the ray, before the
   hit is fetched; by a hit object made from the ray query, with a hint of at most 3 bits for what the thread
   does next (the study says which key; NVIDIA documents 8 hint bits at most with a hit object, the highest
   first); what is carried across it counted again in the compiled listing (the worker before you left the tools
   for that in `<scratch>\ser1-tools`), and under 205 bytes.
3. The reflections' pass gets the same two steps only where they cost little; say what you left.
4. Everything behind ONE compile switch (`TGPT_SER_SPLIT`, OFF by default: a build without it is today's to the
   byte) and a `Speed trial` number you name, so that ONE run shows today's pass, the new one and the plain
   yardstick side by side. When the game has answered it becomes the rule the same day and today's pass goes
   (not your job: say what goes).
5. HAND-OVER within two hours, then STOP: what the coordinator builds (which CMake switches), the recipe (scenes
   `tavern-room`, `metal`, `water-day`; no denoiser, 16 frames, 25 s a step; today's, the new, the yardstick),
   and what each must read. TARGET: the bounced light's pass at or under 14 ms in the tavern and 28 in the
   town, no slower than 42.6 at the beach, with reordering on.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- Reordering is never switched off by what you build. The picture does not change.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 3` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON` and your switch; copy the main tree's
  `build\native\generated\pathtracing\*.h` into your build folder's `generated\pathtracing\` right after
  configuring, so that only your passes compile); never `-First`, never a raised limit. A reordering pass takes
  5 to 9 minutes of a core.
- Offline: never the whole GPU test or ctest as a whole. A worker's test cannot make a new reordering pass under
  its 8 GB memory ceiling: do not try; the game is the test. The plain screen-order pass of step 1 can be
  checked for the same picture in the test's `TGPT_TEST_NO_AGILITY=1` mode if you route the plain yardstick
  through it too: do that if it is cheap.
- RTXDI and NRD sources never copied or quoted. NVIDIA's and Microsoft's documents: read; copy nothing a license
  does not allow.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed. Another worker is in `scene.cpp` and the plugin (micromaps): change what you must,
  move nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- `python tests/source_checks.py` passes; the plugin builds. Never describe anything with the word the source
  checks ban (`test_testing_limits_disclosed`): say "prototype" or "on trial".
- Final message: six lines at most: what is built and behind which switches, bytes carried and calls compiled
  before and after, the recipe and what each step must read, what today's pass loses when this is the rule,
  that it compiles and the checks pass, the report's path.
