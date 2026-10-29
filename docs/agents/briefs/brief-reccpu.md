# Brief: reccpu - the CPU time to record a frame's scene: 5 to 21 ms a frame while the GPU waits (PT-99)

Copy: `%TEMP%\tg-agents\wt\reccpu` (pristine base: `base20k`; made from the main tree 2026-10-12 ~03:10). Follow
`AGENTS.md` in your copy; read first its rulings "Speed is no longer left for last", "The order of work", "No limits
that change the picture", "The game is the first test". Report: `docs/agents/reccpu.md` IN YOUR COPY; the plan within
20 minutes, kept current; "State when stopped" at once if a limit shows.

## Why

The owner's first priority is a frame rate one can play at. A traced frame is, one after the other: the game's inputs,
OUR CPU WORK TO RECORD THE FRAME, the trace on the GPU, the composite. While we record, the GPU waits. Measured
2026-10-12 (the tracer's own 10 s lines, "cpu=... record ..."): the tavern 5.4 ms a frame, the forest 11.0 standing
and 13 to 26 on a walk, the beach 12.7, the town 11.5 to 21.5; frames there are 48 to 150 ms. At the target of 55
frames a second (18 ms a frame) this alone is the budget. None of it is in the picture: a millisecond saved is a
millisecond of every frame (`docs/PT-REQUESTS.md`, grep "### PT-99", its first two bullets).

Most of it is `GpuScene::record` (`native/pathtracing/src/scene.cpp`). It is timed by section since tonight, a line
every 10 s in the tracer's log ("PT record CPU 10s"; `RecordCpu`, `record_cpu.section(...)` in scene.cpp). The forest,
standing, 95 frames: sum 16.65 ms: the plants' plan 3.76; lights (the search tree, the grid, the emissive clusters)
3.47; the water's pose and the characters 3.39; the plants' pose 2.11; the emitters' list 1.84; the cloth's plan
0.72; the records' uploads and the water's plan 0.69; the cloth's pose and the emitters' build 0.28; the rest under
0.2 each; the longest frame 106 ms. On a walk the plants' plan alone is 8 to 11 ms (the log's "Mega Geometry CPU
trial 0" line: "scene record 23.5 ms, plant plan 7.8"). The town's line is not read yet: the coordinator sends it.

What the coordinator saw by reading (not measured, yours to confirm or drop):
- `plan_plants` (scene.cpp ~1169): every frame, for every instance of the scene (12,000 to 24,000): a
  `plantWindOf_.count()` per submesh, a byte-by-byte FNV hash of 60 bytes and more a plant, a fresh
  `std::map<cell, std::vector<Member>>` (600 nodes and their vectors made and freed a frame), `omm_instance` a plant,
  a sort a cell. The signature it makes is compared only with signatures of the same session.
- The lights' section (~2695): `lamp_tree::build` every frame, the previous lights copied and uploaded, a hash, and
  `build_light_grid` whenever the hash moved: IN THE TOWN THE GRID WAS BUILT IN 95 OF 94 FRAMES (the 10 s line:
  "13368 cells lit, 14 lights a lit cell, 95 builds"). The hash holds each lamp's place to a quarter metre and its
  range, and each emissive cluster's place, range and RED to a sixteenth: find which of them moves every frame in a
  town, and whether the grid's content really depends on it (the grid's weights are the lights' luminance over the
  squared distance; the comment says "flicker keeps it").
- The water's pose (1,800 pieces) and the characters; the emitters' list, made though nothing changed.

## What to do

Make the frame's record cheaper WITHOUT CHANGING WHAT IT RECORDS: the same commands in the same order, the same bytes
in every buffer the passes read, frame by frame. Two kinds of change are yours to make:
1. The same result with less work: tables in place of lookups, memory kept between frames in place of made and freed,
   a faster hash where the value never leaves the session, loops that touch only what can have changed.
2. Work not done again when its inputs did not change since the frame before. ONLY with a test of "did not change"
   that you can show covers every input the result depends on (list them in the report, one by one); where you are not
   sure, do it every frame as now. A plant missing, a light list a frame old or a stale record is a picture defect
   and worse than the milliseconds: PT-125, fixed tonight, was a stored number that went stale when the set of
   groups changed (read that entry before you touch the plants).
What is NOT yours without asking: anything that makes a result a frame late (work moved to another thread whose result
the frame does not wait for), any limit, anything that changes a buffer's content. Where such a way would save much,
describe it with its number as a choice for the owner and stop there.

Work in the order of the numbers: add finer sections inside the largest ones first (a clock read a section costs
nothing), then cut. The tracer's own record outside the scene (the line's "record" less the scene's "sum") is yours
too if it is large. `tracer.cpp`'s trace_event and the passes' recording are large: grep, read ranges.

## How it is measured and checked

- Speed: in the GAME alone, by the coordinator. Hand a DLL (path, SHA-256) as soon as a first cut is built: the
  coordinator runs `metal,forest@speed-now` (the town and the forest, the world running, 35 s each) and sends you the
  "PT record CPU 10s" lines and the 10 s lines' "cpu=... record ...". A first hand-over within your first hour.
- Same result: say for each change why the recorded result is the same. Where you can, prove it: a CPU check in
  `tests/` (no GPU; the pattern: `tests/lamp_tree_test.cpp`, grep how CMake builds it) that runs the old and the new
  code over made-up inputs and compares the outputs byte by byte. The offline GPU test's one small mode of your choice
  (plants in the wind: grep `sway`, `plants` among the modes of `tests/pathtracing_gpu_test.cpp`) once at the end, if
  the card is free.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in `%TEMP%\tgb\reccpu-*`.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start (it also says no while a
  game run waits for the card and while a game of the owner's is open; never remove that wait; wait with one
  background script, never poll). NEVER the whole `tgpt_gpu_test` or ctest.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`; it keeps the PC's memory limit and starts
  its compilers below normal priority), then the native build into `%TEMP%\tgb\reccpu-build`; configure as
  `docs/AGENT-WORKERS.md` and `docs/agents/briefs/brief-campspeed.md`'s build line say (the main tree's prebuilt NRD
  libraries, NVAPI, `-DTGPT_MEGA_GEOMETRY=ON -DTGPT_RAIN_WETNESS=ON`, the disconnected FetchContent paths). YOU CHANGE
  NO SHADER, so the 57 passes are compiled once (half an hour to an hour while others compile); after that a build
  is a minute. Start that compile first, read while it runs.
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files (`scene.cpp` is 4,300
  lines, `tracer.cpp` 5,000), never whole. Say every switch or log line you add. Run `python tests/source_checks.py`
  before you hand over.
- Final message: five lines at most, pointing at the report.
