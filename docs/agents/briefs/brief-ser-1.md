# Brief: the bounced light's pass designed for Shader Execution Reordering (PT-99) - one worker, measure first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/ser-1.md`; write it early, with "State when stopped" at its top after every step.

## The owner, 2026-10-10

Told that the bounced light's pass runs in 14 ms as a plain compute pass and in 56 ms as the ray generation pass
with reordering, with the same picture: "how would Shader Execution Reordering make it slower? It's the feature
that makes PT viable to accelerate on modern GPUs"; and, offered a chooser that picks the faster: "Do not turn
that off. If it makes it slower with that on, then it's either not due to that, or due to us not designing our
tech with it in mind." The ruling is in AGENTS.md ("Shader Execution Reordering stays on; the tracer is designed
for it"): READ IT. Nothing you build may switch reordering off or pick a pass without it. The plain compute
passes are your yardstick, nothing more.

## What the game measured (run `20261010-100720`; in your copy: `docs/PT-REQUESTS.md` PT-99's newest notes: grep
the heading; `docs/agents/measure-run.md`)

ms a frame / the bounced light's pass / the reflections' pass, no denoiser, 1924 x 1084, one ray a pixel:

| | the tavern | a town | the beach |
|---|---|---|---|
| the ray generation passes with reordering (what ships) | 81.1 / 56.0 / 11.6 | 109.4 / 60.9 / 29.3 | 199.4 / 42.6 / 140.2 |
| the same passes as plain compute passes (the yardstick) | 42.4 / 13.9 / 15.0 | 75.5 / 27.8 / 28.4 | 405.3 / 70.8 / 317.7 |

- How the tracer uses the feature today (`pt_common.hlsli`, the lines about `PT_SER`): a pass is ONE ray
  generation shader that does everything inline: it traces with inline ray queries, calls
  `dx::MaybeReorderThread(material & 0xFFFF, 16)` once, then shades inline. No hit objects, no hit shaders.
- On 2026-10-09 the measuring build left that CALL out at run time in the same shader: no gain (87.7 against 81
  to 85 ms). So the cost is not the sorting by itself.
- The measuring run also said: with no ray at all the bounced light's pass is still 14.4 ms in the tavern, and
  the ray alone, nothing shaded, 28 ms. Both were read off the ray generation variant: they are suspects too.
- At the beach the plain step's picture was brighter (57.6 against 53.6 levels): not explained.

## The job

1. **Find the cause by measuring, in the game, with the smallest variants** (HAND-OVER 1 within your first hour
   and a half; the coordinator runs it at once; shader variants compiled side by side behind ONE compile switch
   `TGPT_SER_STUDY`, OFF by default, chosen at run time by a number of the hidden `Speed trial` that you name; a
   build without the switch is today's to the byte). At least these, for `pt_gi` (and `pt_specular` where cheap):
   a. the ray generation shader with NO regrouping call compiled in at all (does the cost stay? then it is the
      pass being a ray generation shader this large, not the regrouping point);
   b. the regrouping with FEWER hint bits (16 today: 0, 2, 4, 8), and by ANOTHER KEY (what the thread does next:
      plain surface, water, glass, sky; not the material's number);
   c. the regrouping point MOVED to where a thread carries least (right after the ray, before anything of the hit
      is fetched), and what the thread carries across it counted and cut (say in bytes what is live there today,
      from the shader: reservoirs, random state, the surface, the cache's feed...);
   d. anything else your reading of Microsoft's and NVIDIA's documents on the feature says matters (read them:
      the DirectX specification's Shader Execution Reordering section, NVIDIA's guidance on hint bits, live
      state and hit objects; write the sources and what each says into the report).
   The recipe: scenes `tavern-room`, `metal`, `water-day`, one step a variant, no denoiser, 16 frames, 25 s; the
   yardstick steps (`Speed trial` 8 with the plain passes; the run uses `TGPT_TEST_PLAIN_PASSES=1` through the
   coordinator's test file) beside them. What you expect of each, written BEFORE the run.
2. **The design, by what step 1 can already say from reading, and finished when the run is in**: the bounced
   light's pass built for the feature: hit objects (`dx::HitObject`: from the ray query or by `TraceRay`),
   regrouping by the hit with a small hint, the shading split so that threads that do the same thing run
   together (hit or callable shaders, or a second regrouping where the water's and the glass's paths begin),
   little carried across each regrouping. What it needs of the tracer's C++ (the state object, shader tables,
   local root signatures), of the acceleration structures (hit groups per geometry?), what opacity micromaps and
   the inline alpha test become, what it costs to make at a new build's first start and in video memory. The
   target, in numbers: the reordered pass at or under the yardstick in the tavern and the town (14 and 28 ms),
   and no slower than today at the beach (42.6).
3. STOP after hand-over 1 and the written design. The redesign itself is the next job, with the run's numbers.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- Reordering is never switched off by what you build (AGENTS.md). The picture does not change.
- A study variant is a variant of the SAME computation: the same rays, the same shading, the same random numbers
  where it can be; say where it cannot.
- Shader compiles: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 3` before `cmake --build`
  (configure with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON` and your switch; copy the main tree's
  `build\native\generated\pathtracing\*.h` into your build folder's `generated\pathtracing\` right after
  configuring, so that only your new variants compile); never `-First`, never a raised limit. Each reordering
  pass takes 5 to 9 minutes of one core: keep the number of variants to what answers the questions.
- Offline: never the whole GPU test or ctest as a whole. A worker's test cannot make a new reordering pass under
  its 8 GB memory ceiling (two workers found that): do not try; the game is the test.
- RTXDI and NRD sources never copied or quoted. NVIDIA's and Microsoft's documents and samples: read; copy only
  what a license allows and say which.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- `python tests/source_checks.py` passes; the plugin builds. Never describe anything with the word the source
  checks ban (`test_testing_limits_disclosed`): say "prototype" or "on trial".
- Final message: six lines at most: the variants built and what each should show, the recipe, what the documents
  say that we do against their advice, the design in two lines, that it compiles and the checks pass, the
  report's path.
