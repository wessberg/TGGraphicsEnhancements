# Brief: what stands next to a flame: found on purpose, and shown without burning out (PT-85, PT-40) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\near`
(pristine base: `...\scratchpad\wt\base61`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/flame-near.md`; write it early. Read `docs/agents/flame-specks.md`
first (the job before yours: its four trial switches and how they travel).

## What the game said (run `20261009-012128`, the MAIN tree's `artifacts\harness`, read only; `tavern-room`, NRD, 16 frames a step)

A sensitive count (a pixel at least 8 levels AND 1.4 times over its own median; spots = 24-pixel cells):

| step | the picture: spots a frame (places) | blocks' wander | burnt pixels (over 240) | Specular view: spots (places) |
|---|---|---|---|---|
| as now (lamps in their flames) | 35.2 (156) | 0.063 | 1,362 | 67.9 (354) |
| A: a lamp's light held to what it gives at 30 cm | 17.2 (85) | 0.040 | 19 | 19.2 (77) |
| A at 10 cm | 17.7 (92) | 0.041 | 22 | 22.4 (98) |
| B: the lamp reflected at HDRP's radius | 31.8 (145) | 0.064 | 1,357 | 65.6 (337) |
| C: highlights as for HDRP's radius | 34.4 (171) | 0.062 | 1,328 | 76.9 (351) |
| D: shadow rays at HDRP's disc | 20.6 (86) | 0.049 | 960 | 44.1 (208) |
| the game's own picture | 9 (11 in 4 frames) | | 0 | |

So the specks and the burnt candles are ONE thing: what a flame puts on surfaces within 10 cm of it (the wax, the
holder, the wall right behind a wall candle). B and C are not guilty. D helps partly (it also takes a third of the
burn away: say whether it works through the same near light or through the shadows' edges).

## The owner's rulings that bind this job

- The fire is the emitter: "No, of course I don't want to go back to the game's own light position". Lamps stay in
  their flames at their flames' size. Switch A is a diagnostic that throws light away: it never ships.
- No fakes, no limits that change the picture, no stand-ins. A budget may cost noise, never light.
- Of the burnt candles: "If the cause of the overblownness is that the game candles are lit by 100-watt bulbs, then
  make them not be that". The coordinator answered that a lower power alone does not change wax against wall (1 cm
  against 3 m); check that answer, and say so plainly if it is wrong or if the game's light values near a flame mean
  something else than we take them to (how HDRP itself lights a mesh a centimetre from a point light: its own floor
  on the distance, the light's inner radius or shape, a light layer that leaves the candle's own mesh out).

## What to do: TWO TRIALS, each behind its own hidden switch (default off), handed over fast

1. **The rest of the room finds a flame's lit surroundings on purpose** (the specks). Today a reflection or bounce
   ray meets the few square centimetres of blazing wax by chance: rare and huge. The tracer already samples emissive
   triangles as lights (clusters in the light grid; a path that meets one counts what the sampling leaves:
   `EMITTER_SAMPLED`, `emission_weight`). Candidate: the surfaces within a few radii of a lamp at its emitter are
   sampled on purpose for the light that lamp puts on them (as lights of the light list, by next-event estimation,
   weighed against the chance hit so nothing counts twice), or another standard unbiased way you can defend.
   The mean must not move; the Specular view's specks should fall toward switch A's column WITHOUT losing light.
2. **The picture shows what is far past white as an eye would** (the burnt wax). Dark Adaptation's row says "The
   brightest parts of the view are kept from turning white"; today `highlight_hold` only takes the automatic gain
   away from bright pixels, it never brings down what is past white at the game's own exposure. Candidate: a smooth
   roll-off of the traced light above a knee, in the units the composite stores (before HDRP's tone mapping), by a
   hidden `Advanced` number for the knee and strength, so the coordinator can show the owner three settings in one
   run. The wax should read as bright cream with its shape; nothing under the knee may change (a daylit scene bit for
   bit); the meter and Fog Adaptation's histogram must take it back out as they do the hold (`traced_ungained`), or
   you say what they do instead.
Hand over when both compile and the source checks pass: each switch's name and values, the files changed, a recipe
for `tests/ingame/scenarios.json` (the picture and the Specular view: as now, trial 1, trial 2 at three settings,
both), all in the report. Then stop. The proper clean-up, checks and docs follow the game's answer.

## Rules

- Trial first: no offline fixture and no long study before the hand-over (aim: within 45 minutes).
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"); a small GPU check only if you
  truly need one, through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate.
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\near` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`.
- Do not touch the filters or NRD's settings. RTXDI and NRD sources never copied or quoted.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` whole: grep, then a range.
- If the four trial switches of the job before stand in your way, keep them working (the coordinator still uses A as
  the yardstick) and add yours beside them.
- Final message: five lines at most: the two switches (names, values), what each does in one line, the files
  changed, that it compiles and the source checks pass, the report's path.
