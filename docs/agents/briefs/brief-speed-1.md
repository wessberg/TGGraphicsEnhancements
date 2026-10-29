# Brief: 8 frames a second: where the bounced light's 73 ms and the reflections' 26 ms go, and what can go without changing the picture (PT-08) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\speed1`
(pristine base: `...\scratchpad\wt\base69`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/speed-1.md`; write it early and keep it current.

## Why (the owner, 2026-10-09)

"why the fps is so incredibly poor"; earlier: "Are we still using Shader Execution Reordering, Opacity Micromaps,
and generally anything else we can to accelerate anything related to this Path Tracing implementation as much as
possible?"; the target is 55 frames a second ("As performant as Cyberpunk's PT"). The owner's rule stands: "we want
realism and correctness, and then we will optimize from there": speed comes WITHOUT changing what the picture
converges to. A budget may cost noise, never light, coverage or detail.

## The numbers (the game, scene `metal`, a town square by day: run `20261009-103336`, the tracer's own log line)

1924 x 1084 rays for a 3840 x 2160 picture, one ray a pixel, DirectX Raytracing 1.2 with Shader Execution Reordering
and opacity micromaps (236 meshes); 4.37 million triangles, 16,234 instances, 510 lights (14 a lit cell on average,
at most 312), 11,303 plants posed by the wind. GPU time a frame 118.8 ms:

| stage | ms |
|---|---|
| bounced light | 73.54 |
| reflections | 25.86 |
| direct light | 6.74 |
| caustics | 4.94 |
| prepare (the camera's own rays) | 4.13 |
| bounced light spatial reuse | 1.70 |
| scene | 1.34 |
| composite, cache, the rest | 0.6 |

Other scenes of today (the same log line in the MAIN tree's `artifacts\harness\<run>\TGPathTracing.Native.log`,
read only; grep "stages:"): the tavern about 100 ms, the night scene 110 to 148 ms, the beach 140 ms.

## What to do

1. **Where the bounced light's time goes** (`native/pathtracing/shaders/pt_gi.hlsl` and what it calls in
   `pt_common.hlsli`: the ray, the hit's material, the light it gathers there, the walk under a scattering
   surface, water, the cache). Measure, do not guess: the offline fixture (`tests/pathtracing_gpu_test.cpp` has
   timing modes; grep `TIMING`, `_MS`, `stage`) with parts of the pass switched off one at a time in a measuring
   build of your own, on a scene that is heavy in the same way (many lights, alpha-tested plants, characters). A
   table: each part, its milliseconds, what it is for.
2. The same, shorter, for the reflections' pass (`pt_specular.hlsl`).
3. **What can go without changing the picture**: work done twice, work whose result is thrown away, rays or
   texture reads behind a test that could come first, loops over lights that a tighter list would shorten, shader
   permutations that make every ray pay for the rarest case (water, the walk, glass), reordering hints, payload
   and record sizes. For each: what it would save (measured where you can), why the picture stays the same, the
   risk. Order them by milliseconds saved per hour of work.
4. **Build the first one or two** that are small and sure, each behind a hidden switch of the plugin (`Advanced`,
   default OFF until the game has shown the same picture; a spare or reserved field, never an option bit: all 32
   are used), with your own small offline check that the picture is the same with it on (the mean and a pixel
   difference, in its own `..._ONLY` mode). Hand over: the switches, a recipe for `tests/ingame/scenarios.json`
   (`metal`, NRD, 16 frames, each switch off then on: the pictures must match and the log's stage line must show
   the saving), the files changed.
5. Do NOT: lower the rays' resolution or count, shorten paths, drop lights, raise a threshold that leaves light
   out, change a denoiser. Those change the picture; name them in the report only as what a later "quality level"
   row could offer, with what each would cost in looks.

## Rules

- GPU timings mean little while other tests share the card: say for every number whether the card was yours alone
  (`tools\harness\Run-PtHarness.ps1 -GpuCheck` in the MAIN tree shows who uses it); take the best of three runs.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own timing mode and
  check only, never while the game runs (`artifacts\harness\RUNNING` in the main tree held by a live process:
  wait). At most ONE GPU test at a time.
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\speed1` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`: it waits
  for room on the whole PC (other compiles, memory, a game run): let it wait, never work round it.
- RTXDI and NRD sources never copied or quoted.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so.
- Other workers are in `pt_common.hlsli` too: change what you must, move nothing else.
- Final message: eight lines at most: the bounced light's table in three lines, the reflections' in one, the list
  of savings (top five, with milliseconds), what is built behind which switch, that it compiles and the source
  checks pass, the report's path.
