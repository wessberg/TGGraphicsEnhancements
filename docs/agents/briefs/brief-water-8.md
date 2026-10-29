# Brief: the sun in the water at a fraction of the cost, the same result (PT-81) - one agent, one thing

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\water8`
(pristine base: `...\scratchpad\wt\base58`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/water-8.md`; write it early and after every step.

## Why

The water job 7 (`docs/agents/water-7.md`: read it whole) made the sun light the water's body: every point where a
path scatters inside the water asks for the sun through the waves (`pt_water_sun.hlsli`). In the game it works (run
`20261008-194341`, `water-day`, the MAIN tree's `artifacts\harness`, read only; sheet `water-day-sheet.jpg` in it): the
sea is green water with wave shapes and thin foam, the bed shows, and the green dots and NRD's orbs are gone (0.2
bright places a frame with NRD, 11 before).

It costs too much: **the frame went from 78 to 93 ms to 223 ms** (4.5 frames a second at the beach). The job's own
measure: 24 to 35 ms for 230,000 pixels of deep water; 40 to 60 wave corners a scatter point, 15 texture reads a
corner. The owner's rule: correct first, then fast WITHOUT changing the result.

## What to do

Build the speed job that water-7's section 6 planned, or a better one if you find it, with the same result:

1. A buffer of wave corners made once a frame (a small pass before the path stages; levels of a grid round the
   camera; a corner holds the wave's point and its normal), by the wet ground's pass as the plumbing's pattern.
2. `water_sun` takes its patch from that grid where the cell it wants is a level's cell or coarser; shallow points
   keep today's way. Everything else stays: the plane, the landed triangles, the sun's disc, the growth, the shadow rays.
3. Only then, if it is small and certain: the corner's foam and the sun's shadow kept in the corner too.

**The check that the result did not change** is water-7's sea fixture (`tests/water_sea_gpu.inl`,
`TGPT_TEST_WATER_SEA=1`; its parts and numbers are in water-7's sections 2c, 3b, 4b): the closed form (x0.99 to
x1.01), waves against still water (x1.000), 16 against 256 frames, the largest single sample (26.6 times the mean),
the glint's light, the foam's cover. Every one of them must hold within its stated tolerance, and you state each
before and after. Add the fixture's time a frame for the deep-water part, before and after: that is this job's number.
Aim: the sun's part under a fifth of today's cost. If a part of the plan changes a number, do not ship that part.

## Rules

- Your own small GPU check is allowed (the owner, 2026-10-08 17:55), gated as AGENTS.md says: only while the game is
  closed and the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` lets a test start; wait with ONE background
  script, no polling. NEVER the whole GPU test or the whole of ctest (the owner: "full test suite never in subagents").
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\water8` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`.
- No clamp, no fewer corners than the result needs, no stand-in: a cheaper way that changes the picture is not this job.
- Mind the passes' compile time: a new pass is welcome, a second copy of a big function inside the path stages is not.
- RTXDI and NRD sources never copied or quoted; Unity/HDRP and the game's shader code: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `tests/pathtracing_gpu_test.cpp` whole: grep, then a range.
- No ABI change unless needed; say so at the top of the report if there is one.
- For the coordinator's ONE game scene (`water-day` with `quick`): the frame time to expect, and that the sea box's
  light must not move (today: x3.6 / 2.7 / 3.3 of the game's with no denoiser).
- Final message: five lines at most: what was built, the time before and after, each fixture number before and
  after, what is not run, the report's path.
