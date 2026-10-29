# Brief: the small fireflies that stay with NRD: lamps' highlights that pop on glossy things (PT-85) - one agent, one thing

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\glints`
(pristine base: `...\scratchpad\wt\base57`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/glints.md`; write it early and after every step.

## What the owner sees (2026-10-08 18:25; PT-85 in `docs/PT-REQUESTS.md`, grep it)

"There are still fireflies here and there when NRD is enabled. Not big ones, but they are there." And of the whole
family: "the NRD orbs isn't a new thing. We've had them for a long time."

Measured in the game (the MAIN tree, read only: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261008-182327\tavern-room`,
the recipe `fireflies`: NRD on, 16 kept frames of the picture and of each debug view; sheets
`...\20261008-182327\tavern-room-specular.jpg` and `tavern-room-direct.jpg`: the game, the picture, three frames of
the layer round a speck, the layer's usual). Places a frame where pixels are 40 levels over what the same pixel
usually is:

| layer | places a frame | distinct places in 16 frames | a place lasts |
|---|---|---|---|
| the picture | 6.2 | 48 | 2.1 frames |
| Direct | 6.2 | 28 | 3.6 |
| Indirect (the only layer NRD itself denoises) | 0.0 | 0 | - |
| Specular (reflections) | 7.0 | 62 | 1.8 |
| Emissive | 0.5 | 3 | 2.7 |

So the specks are NOT in the bounced light and not NRD's making. They are in the two layers the tracer's own filters
handle. What the sheets show: in the Specular layer a lamp's highlight on a glossy thing that is dim in most frames
and very bright in a few (a cider bottle's shoulder, the edge of a painted beam, a lattice); in the Direct layer the
candle's own wax changing from frame to frame, and dots on an armour's ribs.

## The rule that binds this job (the owner; AGENTS.md)

A defect seen with Denoising None is the tracer's; never tune a denoiser or a filter to hide it. On 2026-10-08 two
attempts at the filters went wrong and were taken back (PT-85, PT-75): a filter that kept the light of rare bright
samples drew soft orbs everywhere; the one that refuses them loses light. BOTH go away only when the samples stop
being rare. That is this job: find which estimator makes a highlight rare-and-huge instead of often-and-right, and
fix the sampling. Do not touch `pt_atrous.hlsl`, `pt_temporal.hlsl` or NRD's settings.

## What to do, one thing at a time

1. **Find the estimator.** For a lamp's highlight on a smooth surface: which code gives it (the lights' samples
   through `direct_light` / the direct-light pass with RTXDI's reservoirs; the reflection ray, which today does not
   count an emitter it meets, `g_pathGathers = false` in `pt_specular.hlsl`; the radiance cache)? With several
   hundred lights (the tavern has 482) a narrow lobe is lit only when the one lamp it mirrors is the one picked.
   Show it offline in a small fixture of your own (a glossy bottle or sphere, a rough floor, eight small lamps):
   the layer's largest sample over N frames against the pixel's mean, a pixel; today's number.
2. **Fix the sampling**, the standard unbiased way for the case you found: the lobe's own ray counts the emitter it
   meets, weighed against the lights' samples so that nothing is counted twice (multiple importance sampling), or
   what RTXDI's public guide says for glossy surfaces. The mean must not move (check it against a long
   accumulation); the largest sample over the mean must fall, and you state by how much.
3. Then, only if 1 and 2 are whole: the same look at the Direct layer's specks (the candle's wax, which the tracer
   walks through as a subsurface material; the dots on stepped armour: black dots are missing light, PT-27).
   Findings and numbers are enough; fix only what is small and certain.

## Rules

- Your own small GPU check is allowed (the owner, 17:55), gated as AGENTS.md says: only while the game is closed and
  the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` lets a test start; wait with ONE background script.
  NEVER the whole GPU test or the whole of ctest (the owner, 18:00: "full test suite never in subagents").
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\glints` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`.
- RTXDI and NRD sources are never copied or quoted (their public headers' names and public guides may be named).
- Keep tokens down: never read `pt_common.hlsli`, `tracer.cpp`, `tests/pathtracing_gpu_test.cpp` whole; grep, then a range.
- For the coordinator's ONE game scene (`tavern-room` with the recipe `fireflies`): what the table above should become.
- Final message: five lines at most: the estimator, the fix, the number before and after, what is built and not run,
  the report's path.
