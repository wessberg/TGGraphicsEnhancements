# Brief: the game's particles reach the tracer: fire, smoke, sparks taken from the simulation's own buffers (PT-67, PT-71 item 7) - one worker, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/particles-2.md`; write it early and after every step.

## Why (the owner)

- 2026-10-07: "particles need to be traced"; told the effects would be found by watching the game's draws: "but we
  don't want to render the game twice?"; "off-screen effects must be included, no screen space stuff".
- 2026-10-09: "Do we light [up] smoke particles yet?" No: the game still draws every effect over the traced picture
  with its own lighting; traced light does not light smoke, a fire's sparks cast no light and show in no mirror.
- 2026-10-06/07: "the heat-haze around each flame should also refract via PT", "Heat haze should also apply around
  fire, like torches and fires, stoves, etc" (PT-41): it needs the same captured effects; not this job's to draw.

## Where it stands (read these first, in this order; they are in your copy)

- `docs/agents/vfx-capture.md`: the job before yours. Every effect in the game is VFX Graph (no ParticleSystem at
  all); no managed access to the particles; it built a metadata-only D3D11 OBSERVER (it finds the simulation's
  allocations from compute dispatch outputs and cross-checks them against the candle draws; it reads no buffer) and
  the recipe `vfx-capture`. Its section "Simulation-buffer route" says what comes next and how it must be tested.
- The one game run of that observer: the MAIN tree's `artifacts\harness\20261007-224654` (`vfx-report.md`,
  `vfx-summary.json`, `vfx-overview.jpg`; read only). It is your evidence of which compute kernels write which
  buffers and when.
- `docs/agents/design-particles-fog.md` (the design: one world-space model of media beside surfaces; its ten jobs)
  and `docs/agents/effects-inventory.md` (what the game has).

## The job: stage 2 of the capture, up to ONE game run

1. From that run's evidence, decode what you need of the candle flame's and one fire's (with its smoke and sparks)
   simulation: which buffers hold the particles' attributes, their layout (position, size, age or lifetime, colour,
   alive), how batched systems and live lists are addressed, what is written at start and what every update. Say
   plainly what the evidence does NOT tell and which ONE further diagnostic run would (then build that diagnostic
   and stop there: the game is the first test).
2. Read those buffers where the simulation leaves them (on D3D11, after its update, on screen or not, never from a
   draw and never by simulating or drawing anything a second time), repack what the tracer needs into records
   (world position, size, the system and its output's material, colour, age: your design, with growing storage:
   no fixed count that drops particles), and bring them to D3D12 through the existing shared device
   (`tracer.cpp` `copy_skinned` is the pattern: no bridge API change).
3. The offline test the report before you specifies (known D3D11 data with batch offsets, a shuffled live list,
   dead entries, several systems, changing frames, a resource replaced; read back on D3D12; growth; an empty frame
   after a full one), in its own `..._ONLY` mode, mutation-checked (value, count, index mapping, frame ownership).
4. A hidden switch of the plugin (`Advanced`, default off: nothing captured, the picture as before; a spare or
   reserved field, never an option bit: all 32 are used) and a harness recipe (`candle` and `campfire`) that, with
   the switch on, logs per system what was captured (system, count alive, the box of positions) and writes the
   captured positions over the game's own picture as dots (a diagnostic picture only, made by the harness's
   analysis from the log or a small debug pass: say which), so that ONE run shows whether the captured particles
   sit on the flames, the smoke and the sparks, also for a fire behind the camera.
5. STOP THERE. Your final message is the hand-over for that one run. Drawing and lighting the particles in the
   tracer (emission, absorption, scattering: the design's medium model), and the heat haze, are the next worker's,
   after the game has said the capture is right.

## Rules

- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own check in its
  `..._ONLY` mode only, through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the
  game runs (`artifacts\harness\RUNNING` in the main tree held by a live process: wait with one background script).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\COPYNAME` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the
  plugin too (0 errors, 0 warnings). `python tests/source_checks.py` passes.
- The owner's rules: the game simulates once and the tracer draws once; nothing depends on what is on screen; no
  copies of textures; no limits that change the picture; no guessed particles (never positions made from bounds,
  names or a replayed vertex shader). Hooks on global functions run on driver threads with tiny stacks: no large
  stack arrays. One signalling queue per fence.
- Decompiled Unity or game code is read to learn and never committed, copied or quoted. RTXDI and NRD sources never
  copied or quoted.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. A change of the ABI
  (`tgpt_api.h`, `NativeTracer.cs`) keeps both sides and the source checks' pinned sizes in step; say it with sizes.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: what is captured and for which systems, the switch's name, the recipe and
  the one run's command, what that run should show, the files changed, the ABI with sizes, that it compiles and
  the checks pass, the report's path.
