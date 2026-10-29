# Brief: stale - what else keeps a number or a view that goes stale (PT-125's kind of fault, PT-129's hang)

Copy: `%TEMP%\tg-agents\wt\stale` (pristine base: `base20m`; made from the main tree 2026-10-12 ~04:00). Follow
`AGENTS.md` in your copy. Report: `docs/agents/stale.md` IN YOUR COPY; the plan within 20 minutes, kept current;
"State when stopped" at once if a limit shows.

## Why

Two GPU resets a day were the rule in the forest until tonight. The cause (read `docs/PT-REQUESTS.md`, grep
"### PT-125", its first bullet, and `native/pathtracing/shaders/pt_sway.hlsl`'s `plant`, steps 4 and 5): a group of
plants wrote, once, at its making, each vertex's range as A PLACE IN A BUFFER THAT IS LAID OUT ANEW EVERY FRAME; when
other groups came or went the place pointed at other groups' records, at times at records left past the buffer's
end whose meshes had been let go, and the read through such a mesh's view reset the GPU. Short of a reset it posed
every plant from other plants' meshes: after a walk the traced picture had no grass and no trees.

Nothing in the tracer's design keeps this from happening elsewhere. PT-129 (a GPU hang in the reflections' stage after
loading a second scene; not reproduced; grep "### PT-129") may be one of them. The tracer keeps much from frame to
frame: records in GPU buffers that are uploaded "only when changed", view numbers (the descriptor heap's slots) written
into records, places in shared buffers, handles of the game's objects, structures kept and refitted.

## What to do

AN AUDIT BY READING, then fixes for what is real. For every thing the tracer keeps across frames, on the GPU or on the
CPU, that NAMES SOMETHING ELSE by a number, find whether that something can move, be reused or be let go while the
number stays. In particular:
1. Places in per-frame buffers stored in longer-lived data (as PT-125): grep the shaders for `Store` of an index that
   is later `Load`ed in another frame, and the C++ for records uploaded "when they changed" that hold offsets into
   buffers laid out every frame (members, ranges, mesh records, geometry records, material indices, instance records;
   the swaying cloth's `SwayRecord`s and copies; the water's pieces; the characters; the emitters' and the emissive
   clusters' records and slots; the light grid and the lamps' tree, which names lights by their index; the decals'
   tree; the radiance cache and the caustics' grid if they store instance or material numbers).
2. View numbers (descriptor heap slots) kept in records or members while the view can be released: `heap.release(`
   and every `Srv` / `Uav` number written into a record (grep `positionsIn`, `positionsSrv`, `srv=`, `uav=`). The heap
   hands a released slot out again at once (read `gpu.hpp` `DescriptorHeap`): who still holds the old number, on the
   CPU or in a GPU record not uploaded again? Within ONE frame's record a slot released and taken again changes what
   every command already recorded in that frame reads through it (the heap is read when the GPU runs the list): find
   such orders.
3. Instance and material numbers kept by the reuse of samples across frames (ReSTIR's reservoirs, the radiance cache,
   NRD's and the mirrors' records, the yardstick): what happens to a kept light or instance index when the scene's
   list changes (a scene load is the hardest case: everything is renumbered). A stale light index must never read
   past the lights' buffer or take another light for the one it was.
4. What a scene load (`GpuScene::clear`, the tracer's reset paths) leaves behind: buffers, slots, queues of read-backs
   and compaction batches that name meshes or groups by handle or address (PT-125's compaction batches check a
   serial; do the others?).
For each finding: the file and lines, the exact order of events that makes it stale, what the GPU then reads, and
whether it can reset the GPU (a read through a released view, a type mismatch, a loop with no end) or only spoils the
picture. Rank them. Say plainly where you looked and found the code right.

Then FIX the real ones in your copy, each the way PT-125 was fixed: the number kept is one that cannot go stale (a
number within its owner, a serial checked at use), and a read is bounded by its owner's own counts. No caps that
change the picture. Small and exact; a fix you cannot make small, describe and leave.

You need no GPU for the audit. For a fix that touches a shader, compile that pass alone to see that it compiles (the
whole 57 passes take an hour here: do not, unless a fix needs a test). An offline GPU check is welcome for the worst
finding only (a fixture that makes the stale case happen: the old code must fail it, the new pass); gated as below.

Not yours: the scene's record speed (worker reccpu is in `scene.cpp`'s `record`, `plan_plants`, the lights' section:
do not restructure them; a one-line fix there, say it in the report so the merge knows), the water's sun (campfix),
the fog.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in `%TEMP%\tgb\stale-*`.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start (it says no while a game
  run waits and while a game of the owner's is open; never remove that wait; one background script, never poll).
  NEVER the whole `tgpt_gpu_test` or ctest.
- Build, if you need one: `docs/AGENT-WORKERS.md` and `docs/agents/briefs/brief-campspeed.md`'s build line; compiled
  headers of the main tree's current shaders are in `<main>\build\native\generated\pathtracing\*.h` (copy them into
  your build folder's `generated\pathtracing\` and only the passes you changed compile).
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files (`scene.cpp`,
  `tracer.cpp`, `pt_common.hlsli`, `tests/pathtracing_gpu_test.cpp`), never whole. Run `python tests/source_checks.py`
  before you hand over.
- Final message: five lines at most, pointing at the report.
