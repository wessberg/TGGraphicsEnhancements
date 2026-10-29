# Brief: the traced picture's edges are stair-steps and thin things are dots (PT-84) - one agent, one thing

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\edges`
(pristine base: `...\scratchpad\wt\base55`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/edges.md` in your copy; write it early (after the first hour of reading at the latest) and after every step.

## What is wrong (read `docs/PT-REQUESTS.md` PT-84, and PT-85's last point; grep, do not read the file whole)

The game runs at 4K with DLSS Performance: every picture is made at 1924x1084 and the game's upscaler shows it at
3840x2160. The game's own picture comes out smooth. The traced one, with Denoising None and with NRD, does not:
poles, stakes and cloth edges are stairs of 2-pixel steps; grass blades, a rope, lace are rows of dots. Pictures (the
MAIN tree, read only): `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\inspection-20261008\forest-grass-zoom3.jpg`
(top row: the mean of the frames of a FROZEN world: the game's 4, the traced 16 with no denoiser, 16 with NRD; bottom:
single frames and the change maps), `forest-lantern-right.jpg`, `tavern-room-dots-zoom3.jpg`; the inspection's row 62
in `docs/agents/inspection-20261008.md`. The frames themselves: `artifacts\harness\20261008-130029\forest\2-traced-none`
and `3-traced-nrd` (`f00.raw` ..., 3840x2160; `tools/harness/flicker.py` and `crop.py` read them).

Note what the top row shows: even the MEAN of 16 traced frames of a frozen world is blocky and dotted. Frames that
each carried a different sub-pixel shift, resolved by the upscaler, would average to smooth blades as the game's do.

It likely costs more than edges. Grass and leaves flicker 1.3 to 2.4 levels in a frozen world with AND without NRD
(the game: 0.1), and on lit plants NRD is worse than no denoiser (PT-85). A blade thinner than a pixel that is hit in
one frame and missed in the next gives NRD a new surface every frame. The owner asked about exactly this
("flickering inside leves and stuff"). Whether the edges' cause is also the plants' cause is part of the question.

## What the coordinator has read so far (leads, not findings: check each)

- The composite pass is injected at `CustomPassInjectionPoint.AfterOpaqueAndSky` (`src/TGPathTracing/HdrpPasses.cs`):
  the traced light is in HDRP's colour buffer BEFORE the game's upscaler, so the upscaler does get it.
- The tracer's camera rays use `g.invViewProj`, commented "HDRP's, jittered" (`pt_common.hlsli`, `tgpt_api.h`
  `TGPT_Frame.invViewProj`). `HdrpPasses.cs` also hands over `nonJitteredProjMatrix` and `hd.taaJitter`.
- `native/pathtracing/src/tracer.cpp` near line 166: "The traced light a frame left for the next one's composite;
  invViewProj: its camera's". So frame N's composite seems to show the light traced for frame N-1, moved by motion.
  If that light was traced under frame N-1's sub-pixel shift and is laid into frame N, whose shift the upscaler is
  told, then the upscaler lines the samples up wrongly every frame: no smooth edges, thin things as dots. Whether the
  composite moves the light by the two shifts' difference, and how it samples (nearest? bilinear?), is not read yet.
- With Ray Reconstruction (run by TG Better Upscaling where HDRP would upscale) the same question holds: which
  frame's shift goes with which frame's colour and guides. Do not change that route without saying what you found.

## What to do

1. Read, then WRITE DOWN in the report as a short table, for Denoising None, NRD and Ray Reconstruction: which
   frame's camera, sub-pixel shift, depth and motion each of these belongs to at the moment the upscaler runs: the
   traced colour, the tracer's own first hit, HDRP's depth and motion vectors, the shift the upscaler is told. Name
   the file and function for each.
2. Say what is wrong in one paragraph, with the number that shows it (for the kept frames above: a CPU script over
   the raw frames may show it, e.g. whether the traced frames line up better when each is shifted back by the
   previous frame's shift than by its own; the Halton sequence HDRP uses is known. A script, not a GPU test).
3. Fix it properly. No stand-ins: not a blur over the picture, not a sharpen, not turning the shift off for the
   camera's rays unless that is what a correct design does. Candidates to weigh in the report before you choose: the
   camera's rays traced with the shift of the frame they will be shown in; the composite moving the light by the
   shifts' difference with a proper filter; the traced picture made and shown in the same frame. Whatever you
   choose must hold when the camera moves, and must not add a frame of lag.
4. Your own small check: the smallest thing that fails before and passes after (a still scene with a thin slanted
   bar and a cut-out: the mean of N composited frames, each lined up by the shift the upscaler is told, against a
   finely sampled reference). One check, minutes. NOT the whole GPU test.
5. For the coordinator's ONE game scene (`forest` with the recipe `quick`): what to look at and what the numbers
   should become (the grass crop's mean of 16 frames: smooth blades; flicker on grass with NRD; the pole's edge).

## Working rules (the owner's, 2026-10-08 14:05: one thing at a time, short loops; and the card is not ours right now)

- **START NO GPU TEST until the coordinator tells you the card is free** (the owner asked for the card at 14:10). Not
  `tgpt_gpu_test`, not ctest's GPU tests, nothing that opens a D3D12 device. Do not poll or wait in a loop for it.
  Reading, CPU scripts over kept frames, code, shader compiles and the native build are fine. If all that is left is
  your GPU check, write its exact command line in the report and stop.
- When the card is yours: only your one small check. Never the whole suite, never whole runs of two builds to compare.
- Never start the game, never deploy, never commit, never write to the main tree or the game folder. Build in your
  copy: configure a build folder under `%TEMP%\tgb\edges` with `-DTGPT_WITH_NRD=OFF` (as `docs/AGENT-WORKERS.md`
  says), run `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build` (the owner is using the PC:
  no more than 8 compiles at once).
- RTXDI and NRD sources are never copied or quoted; Unity/HDRP and the game's code: learn, copy nothing.
- Keep tokens down: never read `tracer.cpp`, `pt_common.hlsli`, `tests/pathtracing_gpu_test.cpp` or
  `HdrpPasses.cs` whole: grep, then read a range. Plain words and short sentences in the report.
- No ABI change unless the fix needs one; if it does, say so at the top of the report (the plugin and the native
  part must then be installed together).
- Final message: five lines at most: the cause, the fix, what is built, what is not run, the report's path.
