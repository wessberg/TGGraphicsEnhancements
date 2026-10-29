# Brief: the atlases' second count given back (PT-87) - one worker, one cut, C++ only

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/memory-4.md`; write it early and after every step, with "State when stopped" at its top each time
(the usage limit cut two workers off last night).

## The owner

"I very, very frequently get this [Path tracing paused: 129 textures unavailable], so I'm barely able to test PT"
(2026-10-09). "The priority is getting this under control" (2026-10-10).

## What is known (read first, in your copy: `docs/agents/memory-2.md`: its section "memory3", above all M3-6, which
says exactly what to change; then section 1 for why)

- Windows counts a texture that both graphics devices hold open a second time. The tracer's own atlases are made
  on the game's device and opened on the tracer's: 261 MB of second count (347 with hair), for nothing on the card.
- The worker before you PROVED the way three times with a probe and did not build it: when the game's device
  lets go of its side while the tracer's device holds the texture, Windows' count falls by the texture's size;
  the game's device opens it again by a kept handle; what it wrote is still there; the second count comes back
  only while that device uses it again. So: the game's device holds an atlas only while it bakes into it.

## The job

1. Build what M3-6 describes, for the atlases (and for any other of the tracer's OWN shared textures M3-6 names
   as fit for it; not for the game's textures: those are the game's to hold). The picture does not change: say
   why (what reads the atlases, when the game's device writes them, what happens to a bake that comes while its
   side is let go, what a failed re-open does: it must fail safe, to today's behaviour, with a log line).
2. Its proof in the tracer's log: a line with Windows' count before and after, and the "PT memory" lines'
   own figures right (`ownShared`, and the worker before you's new lines).
3. The existing check extended (`tests/pathtracing_memory2_checks.inl` or `..._memory1_...`,
   `TGPT_TEST_MEMORY1_ONLY=1`-style, in the test's `TGPT_TEST_NO_AGILITY=1` mode under a 10 GB memory ceiling you
   never raise): the same frames with and without the cut give the same picture pixel for pixel, bakes included
   (a tile baked after its side was let go and opened again), and the count is lower by what you say.
4. Hand over: what to build (C++ only: no shader compiles), the recipe (`memory`: NRD, none, off, NRD again, in
   `tavern-room` and `spec-terrain`), what the lines should say in MB. STOP there.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- THE PICTURE DOES NOT CHANGE. C++ only (`native/pathtracing/src`); no shader, plugin, export or struct change
  unless M3-6 needs one: then say which and why, first.
- Configure your build folder `%TEMP%\tgb\COPYNAME` with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON
  -DTGPT_REGROUP_FEED=ON -DTGPT_LAMP_TREE=ON -DTGPT_WATER_FLIGHT=ON` (as the main tree's build is, without Mega
  Geometry: your build then needs no NVAPI) and copy the main tree's `build\native\generated\pathtracing\*.h`
  into your build folder's `generated\pathtracing\` right after configuring: then nothing compiles but C++.
- Offline: never the whole GPU test or ctest as a whole; your own check only, through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate; wait with ONE background script.
- Never read `tracer.cpp`, `scene.cpp`, `texture_share.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `native/bridge` (say what you would need of it), the other mods.
- `python tests/source_checks.py` passes. Never describe anything with the word the source checks ban
  (`test_testing_limits_disclosed`): say "prototype" or "on trial".
- Final message: six lines at most: what is built, the MB it gives back (measured offline), why the picture is
  the same, the run that shows it, that it compiles and the checks pass, the report's path.
