# Brief: fixt - offline checks for two faults found tonight: the plants' stale range number (PT-125) and the mesh decal's stale address (PT-134)

Copy: `%TEMP%\tg-agents\wt\fixt` (pristine base: `base20n`; made from the main tree 2026-10-12 ~04:25). Follow
`AGENTS.md` in your copy; read first "A passing test must be mutation-checked", "The full test suite never runs in a
subagent" and "Never start the game while another program uses the GPU". Report: `docs/agents/fixt.md` IN YOUR COPY;
the plan within 20 minutes, kept current; "State when stopped" at once if a limit shows.

## Why

Two faults that can reset the GPU were found and fixed tonight; neither has an offline check that would have caught
it, and one (PT-134) has never been seen in the game at all.
- PT-125 (read `docs/PT-REQUESTS.md`, grep "### PT-125", its first bullet; `native/pathtracing/shaders/pt_sway.hlsl`
  `plant`, steps 4 and 5): a group of plants kept each vertex's range as its place in a buffer laid out anew every
  frame. When a group BEFORE it in the frame's order (the groups go by their cells, sorted) came or went, its
  vertices were posed from other groups' records. In the game: no grass and no trees after a walk, at times a reset.
  The fix: the number within the group. The game has answered (24 walks, the plants back).
- PT-134 (grep "### PT-134"; `docs/agents/stale.md` F1; `scene.cpp` `upload_decals`, `compact_meshes`): a mesh
  decal's own top level kept the address of its mesh's bottom level, which moves when the mesh is compacted two or
  three frames after its build. Fixed by reading; no scene of the harness has mesh decals.

## What to do

In `tests/pathtracing_gpu_test.cpp` (large: grep, read ranges; look at how the existing plant / sway modes and the
decal modes build their scenes, and at how a `..._ONLY` environment switch selects one mode), two small modes of
their own, each minutes at most:
1. `TGPT_TEST_PLANT_SHIFT_ONLY`: plants in the wind in two or more cells. Frames with group B alone; then a plant
   appears in a cell that sorts BEFORE B's (so B's place in the frame's buffers moves); B's pose and what the camera
   sees of B must be what they were (the wind held still for the comparison, or the same wind time: the pose is a
   function of the time), and B's vertices must lie where the plants stand (a bound on its structure or its picture).
   Then the plant goes again: the same. THE MUTANT: `pt_sway.hlsl` with the old number (`rangeFirst + lo` stored,
   read as a place): the check must FAIL on it (B's picture changes or its plants are gone). Run the mutant only as
   far as this check.
2. `TGPT_TEST_DECAL_MOVE_ONLY`: a mesh decal on a surface, its mesh converted in one frame, then QUIET frames (no
   mesh converted, the decals unchanged) long enough for the mesh's structure to be compacted and the builds' chunk
   to go back to the driver (`Arena::collect`, `keepEmpty`: 120 collects; read `docs/agents/stale.md` F1 for the
   exact order of events): the decal must still show in the picture and the device must still be there. THE MUTANT:
   `upload_decals`' early way out as it was before the fix. A mutant that resets the GPU is a result too (say so, and
   make the test report a lost device as its failure, not hang): but FIRST look whether the test can show the stale
   address without touching freed memory (the kept address against the mesh's own, read on the CPU side), so that
   the mutant fails cleanly and no reset is needed to prove it.
Both in the copy's test and, where the test's helpers need it, behind test-only exports as the other modes do it. No
change to what the tracer renders.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in `%TEMP%\tgb\fixt-*`.
- GPU tests: your two modes only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start (it says no while a game
  run waits and while a game of the owner's is open: the owner plays at night, expect long waits; never remove that
  wait; one background script that waits and runs, never poll). NEVER the whole `tgpt_gpu_test` or ctest. A new
  build's first test start has the driver make its passes: 4 to 10 minutes once; `TGPT_TEST_STARTUP_SECONDS=900`.
  A test that reset the GPU while a game of the owner's was open took the game down (2026-10-12 00:20): the mutant
  of the second mode is run ONLY with the card free and no game open, and you say in the report when it ran.
- Build: `docs/AGENT-WORKERS.md` and `docs/agents/briefs/brief-campspeed.md`'s build line. YOU CHANGE NO SHADER for
  the checks themselves: copy the main tree's compiled headers (`<main>\build\native\generated\pathtracing\*.h`) into
  your build folder's `generated\pathtracing\` so that none of the 57 passes compiles; the mutant of the first mode
  changes `pt_sway.hlsl`, one small pass, in a second build folder.
- The stage of a test run needs the runtime files of `<main>\artifacts\Native` (the Agility `D3D12Core.dll`: without
  it a test runs with no DirectX Raytracing 1.2 and proves nothing); Windows sweeps old files out of the temp folder
  (`docs/AGENT-WORKERS.md`, "When Windows sweeps the copies"): check they are there before every start.
- RTXDI and NRD sources are never copied or quoted. Run `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report: which mode passes on the fix and fails on its mutant,
  what was not run and why.
