# Brief: the passes that reuse last frame's samples must follow the camera's sub-pixel shift (PT-84, PT-85) - one agent, one thing

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\reuse`
(pristine base: `...\scratchpad\wt\base59`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/reuse-shift.md`; write it early and after every step.

## What is known (2026-10-08; PT-84 and PT-85 in `docs/PT-REQUESTS.md`: grep them; `docs/agents/edges.md`: read it whole)

Until 18:41 the tracer's camera rays carried no sub-pixel shift, so in a still world every frame's pixel saw the very
same point, and everything that reuses the frame before (ReSTIR's reservoirs, the tracer's own history for direct
light and reflections, the boiling filters' neighbourhoods) lined up exactly. Since the edges job the rays carry
HDRP's shift (a new one every frame, up to half a render pixel either way) and a frame is shown in its own frame.
Edges and grass are smooth now and the whole picture flickers less.

But the owner still sees "stars"/speckles with NRD, and the layers say where they went up. The tavern's main room,
NRD on, 16 kept frames a layer (the recipe `fireflies`; the MAIN tree's `artifacts\harness`, read only): run
`20261008-182327` is before the edges job, `20261008-224604` is today's tree (the lamp job `glints` taken out again):

| layer | specks a frame (distinct places), before | now | pixels jumping over 20 levels a frame, before | now |
|---|---|---|---|---|
| Direct | 6.2 (28) | 90.3 (406) | 135 | 1,948 |
| Specular | 7.0 (62) | 22.2 (112) | 94 | 241 |
| Indirect (NRD's) | 0.0 | 1.5 | 1 | 9 |
| the picture | 6.2 (48) | 9.9 (68) | 126 | 134 |

(Between the two runs also went in: hold 1, the filters' loop cut, the water job. None of them is a likely cause for
the Direct layer; say so if you find otherwise.) With the lamp job in, the Direct layer was the same (82.7, 1,985):
it is not that job.

THE LEAD, not shown yet: a pass that fetches last frame's data for a pixel still looks where the pixel's point was by
the motion alone ("this frame's less the previous one's, without jitter"), in a grid that last frame was shifted
otherwise. The fetch then lands up to a render pixel beside the point: on a thin thing, an edge or a small highlight
that is another surface, the history is refused or wrong, and the raw sample shows for a frame or two.

## What to do

1. List every pass that reads something of the frame before by a pixel's place (ReSTIR's temporal reuse for direct
   and bounced light, `pt_temporal.hlsl` for each signal, the a-trous feedback, the boiling filters, NRD's inputs,
   the radiance cache if it is keyed by a pixel, hair's and water's own histories), with the file and the line where
   the previous place is made, and say for each whether it accounts for the two frames' shifts. A table in the report.
2. Show the lead (or kill it) offline with a number: a still scene handed the GAME's way (the edges job's check does
   that: `tests/pathtracing_edges_checks.inl`, `TGPT_TEST_EDGES_ONLY=1`), a glossy thing and thin things under a few
   lamps; the Direct and Specular layers' frame-to-frame change with the shift against the same with the shift
   held at zero. If the shift does not raise it, the lead is wrong: say so, and say what else changed at 18:41.
3. Fix each pass the standard way for its kind (the previous place takes the two shifts' difference; a history
   fetch that lands between pixels is taken from the pixels round it and checked against each one's surface, never
   from one neighbour blindly; RTXDI's public guide for its temporal pass). The mean must not move.
4. For the coordinator's ONE game scene (`tavern-room` with `fireflies`): the table above should go back toward its
   "before" column while the edges stay smooth (`forest`'s grass: `tools/harness/edges_shift.py`).

## Rules

- Do NOT change a filter's strength or a denoiser's settings to hide specks (three such changes were taken back on
  2026-10-08: PT-85). This job is about WHERE the reused data is fetched from.
- Your own small GPU check is allowed, gated as AGENTS.md says (only while the game is closed and the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` lets a test start; ONE background script to wait). NEVER the whole GPU
  test or the whole of ctest (the owner: "full test suite never in subagents").
- A fixture that gets better while the game gets worse is worth nothing (the lamp job's lesson): build yours so that
  it shows on the base what the game shows (the Direct layer far noisier with the shift than without).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\reuse` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`.
- RTXDI and NRD sources never copied or quoted (public header names and guides may be named).
- Never read `pt_common.hlsli`, `tracer.cpp`, `tests/pathtracing_gpu_test.cpp` whole: grep, then a range.
- Final message: five lines at most: which passes were off and by how much, the fix, the number before and after,
  what is built and not run, the report's path.
