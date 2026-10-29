# Brief: the tracer's video memory, the cuts that keep the picture (PT-87) - one worker, C++ only, hand over early

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/memory-2.md`; write it early and after every step.

## The owner

2026-10-09 17:30, of playing: "I very, very frequently get this [Path tracing paused: 129 textures unavailable], so
I'm barely able to test PT", "constantly exceeding our VRAM budget". 2026-10-10 00:50: "The priority is getting
this under control, and then fix the remaining rendering issues and missing features."

## What is known (read first, in your copy: `docs/agents/memory-1.md`, the worker before you: sections 5 to 8;
`docs/PT-REQUESTS.md` PT-87, its two newest notes: grep the heading)

- The first cuts are in the game (the build of 2026-10-09 20:19): the tracer holds 2,930 MB in a tavern, 3,885 at
  the beach (NRD), 4,389 in a town (no denoiser); Mode Off gives 1.2 to 1.5 GB back.
- A town in the rain (`spec-terrain`), the memory line of the run `20261009-202514` (the MAIN tree, read only:
  `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261009-202514\TGPathTracing.Native.log`,
  the lines "PT memory bytes" and "PT memory by distance"): the game's textures held open 1,316 MB; the plants'
  posed copies 783; acceleration structures 504 to 528; mesh geometry 208 to 218; atlases 347; targets 455;
  histories 225; of geometry, acceleration, scratch, records and transfer the water's surface holds 263 (in a
  courtyard). And its last sentence: "the meshes use 148,066,468 of the geometry's and 205,029,632 of the
  acceleration structures' chunks": so THE CHUNKS HOLD FAR MORE THAN THE MESHES USE.
- A question the numbers of that run seem to answer, yours to settle first, WITHOUT a build: does Windows count the
  game's textures a second time when the tracer opens them? When a scene loaded, the tracer's total rose by
  3,362 MB (776 of it the game's textures), Windows' count by 2,172, and "notTracer" FELL by 1,190. If the
  textures were counted twice it would not fall. Say what the lines prove, and correct the memory line's wording
  and its `notTracer` if it misleads (the game's real part, the tracer's real addition).

## The job: cuts that keep the picture, largest first, each handed over as it is done

1. **The chunks' free room given back** (the arenas of mesh geometry and of acceleration structures: chunks held
   whole for a few live pieces after compaction): how much is free inside them in the tavern, the town and at
   the beach (the log line says), and the cut: free room returned or chunks sized to what lives in them. HAND-OVER
   1 within your first hour.
2. **The plants' acceleration structures compacted** (the worker before you reckoned 150 to 250 MB of the town's
   783; the still meshes' shrink to 38 % on this card): they are rebuilt or refitted when the wind's clock or the
   camera moves: say what a compacted structure allows there and what it costs each frame, and build it only if
   the picture and the frame time stay.
3. **The water's pieces** (263 MB in a courtyard, 137 in a tavern with no water in sight): their structures
   compacted, their pools' pages given back.
4. Written up, not built, with MB: a plant's texture coordinates, colours and normals read from its mesh instead
   of copied into every posed copy (it needs a shader change: other workers are in the shaders now).
5. STOP when 1 to 3 are handed over or shown not to pay.

For each cut: its own line in the tracer's log that proves it in the game (bytes before and after), the worker
before you's check extended (`tests/pathtracing_memory1_checks.inl`, `TGPT_TEST_MEMORY1_ONLY=1`: the same frames with
and without the cut give the same picture pixel for pixel), and the recipe the coordinator runs (`memory` exists:
NRD, none, off, NRD again).

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- THE PICTURE DOES NOT CHANGE: the same triangles, the same textures at the same size, nothing left out by
  distance, nothing coarser. What would change it is a choice you write up with its MB.
- C++ only (`native/pathtracing/src`), and the plugin if a log line needs it. NO shader change: so no shader
  compiles. Configure your build folder `%TEMP%\tgb\COPYNAME` with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON`
  (leave `TGPT_MEGA_GEOMETRY` off: your build then needs no NVAPI) and copy the main tree's
  `build\native\generated\pathtracing\*.h` into your build folder's `generated\pathtracing\` right after
  configuring: then nothing compiles but C++.
- Offline: never the whole GPU test or ctest as a whole. Your check runs under a memory ceiling of 10 GB, never
  raised, in the test's `TGPT_TEST_NO_AGILITY=1` mode (the worker before you found that the driver takes a test
  past the ceiling while it makes the two reordering passes), through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs; wait with ONE background script.
- RTXDI and NRD sources never copied or quoted.
- Never read `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or `tests/source_checks.py` whole: grep,
  then a range. Sources are CRLF: keep them so. After a scripted edit, check every hunk landed. Another worker is
  in `tracer.cpp` in small places (speed): change what you must, move nothing else. The Mega Geometry path
  (`mega_geometry.cpp`, the partitioned top level in `scene.cpp`) is parked: leave it as it is.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- `python tests/source_checks.py` passes. Never describe anything with the word the source checks ban
  (`test_testing_limits_disclosed`): say "prototype" or "on trial".
- Final message: eight lines at most: the answer on the double count, each cut with its MB (measured offline or
  expected) and the run that shows it, what does not pay, what is written up, that it compiles and the checks
  pass, the report's path.
