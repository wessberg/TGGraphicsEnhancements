# Brief: the light on what lies under the water: the dark crab, and the rippling light that does not show (PT-81) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\water9`
(pristine base: `...\scratchpad\wt\base62`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/water-9.md`; write it early and keep it current. Read
`docs/agents/water-8.md` sections 7 and 8 first (the job before yours: what the Caustics row's on and off do, the
dark crab read from the code, the missing pattern worked out but not seen); `docs/agents/water-7.md` for the waves.

## What the game showed (run `20261009-010307`, the MAIN tree's `artifacts\harness`, read only; `water-day`, recipe `caustics-none`: no denoiser, 16 frames with the Caustics row on, then off; sheet `water-caustics.jpg`)

- With the row ON a crab in the shallows is DARK; with it off it is bright orange, as in the game's own picture.
- The row adds nothing to see on the bed: the whole picture +1.4 %, the shallows -6 %, the open sea -2.9 %, and no
  rippling light in the difference of the two. The white branching lines in the shallows are foam.
- The owner, 2026-10-09 01:00: "our PT water is still far from perfect I think. It doesn't seem to [refract] light
  inside it, does it?", "it's getting there, but there's room for improvement still", and: "the 'crabs' inside the
  water just doesn't seem like they're 'inside' the water".

## The owner's rulings that bind this job

- Real traced caustics. No fakes: no ready-made pattern texture, no flat stand-in for a surface that has waves, no
  screen-space anything ("Do. not. fake. stuff."). No limits that change the picture: a budget may cost noise or
  speed, never light.
- What a surface under water gets must be what the sun (and later a lamp) sends through the real waves.

## What to do, in this order

1. **The dark crab.** `pt_caustics.hlsl` `emit_triangle` leaves out every landed triangle whose three corners
   landed on different surfaces, and its light with it; a surface under water has no other sun while the row is on
   (`pt_water_path.hlsli` `caustic_light`). A small rounded thing loses nearly all its light. Make that light land:
   what a left-out triangle carries goes to what its corners met, in a way you can defend as losing and inventing no
   light (say what the sum of landed light is against what the triangles carry, before and after). Behind a hidden
   switch of the plugin (`Advanced`, default off: the picture as before), so one run shows it off and on.
2. **Hand that over** as soon as it compiles and the source checks pass (aim: within 45 minutes): the switch's name
   and values, the files changed, a recipe for `tests/ingame/scenarios.json` in the style of `caustics-none`
   (`water-day`, no denoiser, 16 frames: the row on with the switch off, then on; then the row off), all in the
   report. Tell the coordinator in a short message that it is ready, then go on with 3 while the game answers.
3. **The rippling light on the bed.** Find out with numbers whether it should show at that beach (the waves' slopes
   and lengths against a bed 20 to 50 cm down: `water-8.md` section 7 works out a mild pattern a hand wide), and
   whether the pass delivers what it should or loses it on the way (the landed cells' size, 2 cm doubling with each
   level out; what is kept over frames; the same left-out triangles). A small offline check of your own may show
   it: a flat bed under the fixture's waves, the landed light's ups and downs against what the waves' own focusing
   gives. If it is lost, say where and what the fix would be; build it only if it is small and sure. If the waves
   simply are too gentle to draw lines there, say so plainly with the numbers: that is an answer.
4. Note, do not build: only the sun lights the water's body and bed (no lamp, no fire); no foam at the shore; the
   far sea is pale. One line each in the report if you meet their cause.

## Rules

- Trial first: no offline fixture and no long study before the hand-over of step 2.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small check only
  (its own `..._ONLY` mode, minutes), through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, and
  never while the game runs (`artifacts\harness\RUNNING` in the main tree held by a live process: wait).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\water9` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`.
- RTXDI and NRD sources never copied or quoted. Unity's and the game's shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range.
- Keep the cost in view: the beach is at 140 ms a frame with the row on (55 off). Say what your change costs; do
  not spend the job on speed.
- Final message: six lines at most: the switch (name, values), what it does in one line, what you found for step 3,
  the files changed, that it compiles and the source checks pass, the report's path.
