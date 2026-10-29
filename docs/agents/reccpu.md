# reccpu - the CPU time to record a frame's scene (PT-99)

Worker: Claude subagent "reccpu", copy `%TEMP%\tg-agents\wt\reccpu` (base `base20k`), started 2026-10-12 ~03:05.
Brief: `docs/agents/briefs/brief-reccpu.md`. Build folder `%TEMP%\tgb\reccpu-build`, staged DLL
`%TEMP%\tgb\reccpu-native\TGPathTracing\TGPathTracing.Native.dll`, scripts `%TEMP%\tgb\reccpu-scripts`.

## HAND-OVER 2 (04:08): hand-over 1 plus the grid's build beside the record, and the water's units by an index

`%TEMP%\tgb\reccpu-native-8\TGPathTracing\TGPathTracing.Native.dll`
SHA-256 `EBA867F293038C4B9A22456ECC7E3E49055C818CDB80886E14EFBE3182E2E75D` (built 04:07; the stage folder is whole for the
offline test: main's bridge and runtime files beside it). Its two small GPU checks: `%TEMP%\tgb\reccpu-gpu-c.log`
(plants in the wind) and `reccpu-gpu-d.log` (the water's fixture); see "GPU checks" below for what they said.
(`reccpu-native-2` to `-7` are earlier steps of the same: not to be timed.)
What it adds to hand-over 1 (each still the same bytes; "Changes made" 7 to 12 below):
- The plants' list is kept ACROSS THE PLUGIN'S SCANS that changed nothing (hand-over 1 makes it anew at every scan,
  every 0.25 to 2 s: the plugin hands over a new list object each time; 12).
- THE WATER'S PIECES ARE FOUND BY A HASH INDEX where a `std::map` of 4 x 64-bit keys was searched for every piece
  every frame (1,400 to 1,800 searches of eleven steps: by reckoning most of "the records' uploads, the water's
  plan", 0.46 to 0.67 ms in both scenes). The map still holds the units; expected: 0.3 to 0.4 ms less.
- The grid's two arrays are left for the GPU by the build itself, in an upload buffer of the scene's own: the record
  no longer copies 2 MB and more into the frame's ring after its wait (11).
- "PT plants' plan 10s" now ends with what PT-125's marks cost ("The plants' steps' marks (PT-125), timed in one
  frame of 64: x ms a frame for n marks": two markers on the list after every dispatch and refit of every group of
  plants, every frame): a number for the choice of keeping them.
- THE LIGHT GRID IS BUILT ON ANOTHER THREAD, BESIDE THE RECORD, AND THE FRAME WAITS FOR IT. Whether the grid is made
  anew is decided right after the emitters (everything it is made from is the frame's by then); the build starts
  there and runs while the record goes on with the plants', the water's and the characters' poses (3 to 4 ms in
  both scenes); the record waits for it at the place the grid was built before, then uploads it. The same grid of
  the same frame: never a frame late. The log line "PT light grid 10s" says how long a build took on its thread
  and how long the record waited for it (expected: 0). Expected: the forest's lights 3.35 ms to about 0.5 (the
  compare, the uploads of the cells' entries, the lights' records).
- A plant group's build description is made once a group, not once a group a frame.
- The water's buffers' addresses asked of D3D12 once a frame, not four times a piece.
If hand-over 1 is in the game when you read this, time that one first: 2 is 1 plus these.

## HAND-OVER 1 (03:41): a DLL to time in the game

`%TEMP%\tgb\reccpu-native\TGPathTracing\TGPathTracing.Native.dll`
SHA-256 `07E46D94163DE96FEC4565CD58F1FD2F336DDE9B9D71B5C25A19B66380850C25` (33,588,736 bytes; built 03:39 from this
copy's sources with main's 57 shader headers of 03:36; MEGA_GEOMETRY and RAIN_WETNESS on, as main's build).
GPU-checked 04:06 (`TGPT_TEST_OMM_ONLY` with the record check: every picture the same, the grid's cells and the
plants' list agree with the old way at every build and frame; "GPU checks" below). Not timed in the game yet.
- What is in it: the light grid's cells in one walk (CPU-tested byte for byte against the old loop), the far lists
  no longer copied and uploaded at every build, the plants' list kept between frames (checked against the old way
  every 512th frame by itself), winds by tables, and the sections of "PT record CPU 10s" named for what runs under
  them and split (the water's pose apart from the characters, the plants' plan in five, the grid in four).
- What to read in the tracer's log: "PT record CPU 10s" (the new names), "PT light grid 10s" (WHAT made the grid
  anew: a light's range, place, kind, ...: the forest's every-frame build will be named there) and "PT plants' plan
  10s" (how often the list was kept). A line "PT record check: THE PLANTS' KEPT LIST DIFFERS" would be a defect of
  mine (the build then goes on the old way by itself): tell me.
- What I expect (from the run 033350's numbers): the forest's lights 3.35 ms to about 1.7 (the grid is still made
  every frame there, cheaper); the plants' plan 0.95 to about 0.3 in the forest and 1.75 to about 0.4 in the town;
  the cloth's plan 0.63 to about 0.3 in the town. The water's pose and the characters (2.3 to 3.0) are NOT cut: they
  are one driver call a piece (1,379 and 1,793 refits of the water's pieces a frame); the line now shows them apart.
- For one run with every result also made the old way and compared (slow, so not for timing): the environment's
  `TGPT_TEST_RECORD_CHECK=1` for the game's process; the log then says "PT record check: ... the bytes of the loop
  they took the place of" and "... is the list made the old way", once each, or the difference.

## FOR THE COORDINATOR: something deleted files in EVERY tree copy at 03:28 (not this job's doing)

At 2026-10-12 03:28 every file last written before about 2026-09-28, and every SDK junction, went from the tree copies
under `%TEMP%\tg-agents\wt`. Seen in `reccpu`, `base20k`, `base20e`, `caustcut`, `psr`, `megageo6`, `fogfix` (their
`native\bridge\include\tgb` folders changed at 03:28 and lost `d3d_interop.hpp`, `bridge_api.hpp`, `bridge_c_api.h`);
`campspeed` still had its file. In `reccpu`: 138 files of the 1,156 git lists (the newest one of 2026-09-28 07:11; the
oldest survivor is of 2026-10-06), among them `CLAUDE.md`, `native\bridge\src\core.cpp`, three shader sources
(`pt_cache.hlsl`, `pt_boil.hlsli`, `pt_gi_boil.hlsl`), `tests\*.cpp` of the released mods, and the ten junctions
(`DXC-1.9.2607`, `RTXDI-Library-3.1`, `NRD-SDK-4.17.3`, ...) plus `Build.Local.props`.
- The REAL SDK folders in the main tree are whole (looked: the junctions went, not what they point at). The main tree
  itself was not touched.
- What did it: not found. It takes files by their last-written time, which a copy keeps from main, so a copy made
  today holds files "three weeks old". Windows' Storage Sense is on for temporary files on this PC (its registry
  policy: on, temp files on), its scheduled task last ran 10-06, and C: is 98 % full (84 GB free), which is when
  Storage Sense runs by itself. A guess, not a finding.
- What it means: (1) a worker's build in a copy can fail from one minute to the next (mine did: `tgb/d3d_interop.hpp`
  not found); (2) `merge_copy.py` handles deletions: a copy that lost files its BASE still has would merge those
  deletions into main. Here base20k lost the same files at the same minute, so a three-way merge sees no change, but
  check `merge_copy.py --dry` for deletions before every merge from now on; (3) it will happen again as files age past
  the limit (the next: files of 10-06), unless the copies move out of `%TEMP%` or Storage Sense is told to leave
  temporary files.
- What I did in my copy alone: copied the 138 files back from main (`%TEMP%\tgb\reccpu-scripts\restore.py`: only
  files main has not written since the copy was made, so they are what the copy had; they carry the time of the
  copy now, so they are not taken again), and pointed my build's `TG_DXC` at main's `DXC-1.9.2607` (the copy's
  junction is gone). I did not make the junctions again and touched no other copy.

## State when stopped

(kept current; newest first)

- 04:58 DONE, handed back. Both hand-overs are built and GPU-checked in the offline test's small modes; NEITHER HAS
  BEEN TIMED IN THE GAME (the game folder held other builds all night: main's at 03:57, another at 04:09). So
  every number of mine below is a reckoning from run 033350's lines, not a measurement. What is owed: the town and
  the forest with hand-over 2 (and one run with the file `record-check`), then its "PT record CPU 10s", "PT light
  grid 10s" and "PT plants' plan 10s" lines. No script of mine is left running; nothing is half-edited; the copy's
  sources are hand-over 2's; source checks 118 of 118.
  WHAT I EXPECT of hand-over 2 against run 033350: the forest, world running, 9.5 to 9.8 ms a frame to about 5.5
  (lights 3.35 to about 0.3: the grid is still made every frame, beside the record; the plants' plan 0.95 to about
  0.25; the water's plan 0.6 to about 0.3; the plants' pose 1.25 to about 1.05; the water's pose and the characters
  2.9 unchanged); the town, world frozen, 5.8 to about 4.0 (the plants' plan 1.75 to about 0.4; the cloth's plan
  and the instances' loop 0.63 to about 0.45; the water's plan 0.46 to about 0.2; the water's pose and the
  characters 2.3 unchanged).

- 04:12 The sources in the copy are those of hand-over 2 (`reccpu-native-8`); source checks 118 of 118; the grid's
  CPU test passes; nothing is half-edited. Hand-over 1 is GPU-checked (04:06). Hand-over 2's two small GPU checks
  wait for the card in one background script (`%TEMP%\tgb\reccpu-scripts\gpu.ps1`, tags c and d): the main tree's
  `-GpuCheck` says no because STARFIELD IS OPEN (04:09); the script asks again every 20 s until 05:38, runs the two
  modes one after the other when it may, and writes `%TEMP%\tgb\reccpu-gpu-c.log` and `-d.log` (the test's output,
  its exit code, then the tracer's "PT record check" lines). If I am gone by then, those two logs are the result:
  a line "PT record check: ... DIFFERS" or a FAIL other than the stale "42 plain triangles" one (see "GPU checks")
  would mean hand-over 2 is not to be used; hand-over 1 is the checked one.

- 04:00 Hand-over 1 (`reccpu-native`) and hand-over 2 (`reccpu-native-4`; `reccpu-native-5` is 4 plus a marker file
  for the check, see "Switches") are built and wait for the game: neither has been timed there (the game folder
  held main's own DLL at 03:57). Hand-over 1's small GPU check (`TGPT_TEST_OMM_ONLY` with the record check) started
  at 03:58 when the card came free; its log: `%TEMP%\tgb\reccpu-gpu-a.log`. Nothing is half-edited in the copy:
  the sources are those of `reccpu-native-5`; source checks 118 of 118; the grid's CPU test passes.

- 03:35 Built so far (syntax-checked, not linked: the shader headers come from main's build, as the coordinator
  said): the light grid (one walk a light, the far lists not copied and uploaded again, what makes it anew named in
  the log), the plants' plan (its list kept between frames), the sections named for what runs under them. A CPU
  test of the grid passes (4,002 scenes, every byte; a town's cells 3.5 ms a build before, 1.4 ms now on this PC).
  Next: the first DLL as soon as main's headers are there.
- 03:30 Reading done for the plants' plan, the lights' section, the instances' loop, the emitters.

## The rule this job keeps

Nothing a pass reads may differ: the same bytes in every buffer, the same structures, frame by frame. Two kinds of
change only: (1) the same result with less work; (2) work not done again when every input it depends on is unchanged
(the inputs listed one by one below, per change). No limit, nothing a frame late, no thread whose result the frame
does not wait for. No shader is changed.

## What the sections' names really cover (read from `GpuScene::record`)

`record_cpu.section(name)` starts a section; it runs until the next call. So some names are one step off:

| the log's name | what runs under it |
|---|---|
| meshes retired and compacted | `retire_meshes`, `compact_meshes` |
| new meshes' uploads and builds | only taking the converted meshes off the queue |
| the emitters' list | `upload_meshes` and the new meshes' BLAS builds (and, per new mesh, a walk over every instance for "is it water", and Mega Geometry's "is it a plant" walk) |
| materials' textures and programs | `upload_decals`, the materials', map records' and programs' uploads, the material signatures |
| the plants' plan | `mega_prepare`, `plan_plants` |
| moving things, characters' and the water's materials | the replaced set, the combined material index list and its upload |
| the instances' loop | nothing (two lambdas) |
| the cloth's plan and what follows it | the cloth's plan AND the loop over every instance |
| moved instances | the things the game moves |
| the records' uploads, the water's plan | the water records, `plan_water`, the mesh and geometry records' upload |
| the cloth's pose, the emitters' build | `sway`, `build_emitters` |
| the plants' pose | `compact_plants`, `pose_plants`, the groups' instances |
| the water's pose, the characters | `pose_water`, the characters' refits, the instance records' upload |
| lights: the search tree, the grid, the emissive clusters | `lamp_tree::build`, the lights' two uploads, the hash, `build_light_grid` and its two uploads |
| the top level | the instance descriptions' copy, the top level's build |

The first hand-over renames them and splits the big ones (see "Log lines added").

## What was read, and where the time must be (before the game's numbers)

The forest's line, standing (sum 16.65 ms): the plants' plan 3.76, lights 3.47, the water's pose and the characters
3.39, the plants' pose 2.11, "the emitters' list" 1.84 (which is the new meshes' uploads: see the table above).

- WHY THE GRID IS BUILT EVERY FRAME IN A TOWN (read in the code; the new log line "PT light grid 10s" will show it):
  the plugin sends each light's `Range` as `Reach()` = max(HDRP's range, min(sqrt(intensity / 50 lux), 40 m))
  (`SceneLights.cs`), so a flame that FLICKERS changes its range every frame, and the grid is made anew whenever a
  range moves a sixteenth of a metre. The comment there ("flicker keeps it") is true of the colour only. The grid's
  content does depend on the range (which cells list the light, so which lights the far list must still count), so
  a build cannot be skipped: it is made cheap instead. Making the range stand still while a flame flickers (the
  plugin's side: the reach from the flame's mean intensity) would take the grid's build out of nearly every frame,
  but it changes which cells list a light: a choice, under "Choices".
- What a build cost: every light's box of cells walked TWICE (count, then fill), three subtractions, branches and
  multiplications a cell each time; then the far lists (720 lists of every light that falls off with distance: 5.7 MB
  with a town's 985 lights), which are kept from build to build, were appended to the cells' entries (a 5.7 MB copy)
  and uploaded with them (another, into the ring) at every build.
- The plants' plan: every instance of the scene asked every frame, a search of an unordered map a submesh, 60 bytes
  and more hashed a byte at a time for every plant, a `std::map` of vectors made, filled, sorted and freed.
- The water's pose (1,800 pieces) and the plants' pose (600 groups): one `BuildRaytracingAccelerationStructure` call
  a piece and a group a frame (2,400 calls into the driver), a dispatch a group, and PT-125's marks (two
  `WriteBufferImmediate` after every dispatch and refit). Those are the commands themselves: not mine to cut. What is
  the CPU's own there is small (see "Not done").
- `lamp_tree::build`: measured alone, 0.075 ms for 985 lamps. Left as it is.

## Changes made (all in `native/pathtracing/src/scene.cpp`, `scene.hpp`, the new `light_grid.hpp`; no shader)

Each with why what it records is the same.

1. THE LIGHT GRID'S CELLS IN ONE WALK (`light_grid.hpp` `build_cells`, called by `build_light_grid`). Each light's
   cells are walked once; each entry's cell and weight are kept and dealt out to the lists afterwards in the order of
   the lights, so every list holds its lights in the order it did. A cell's squared distance is the sum of three
   numbers that each depend on one axis: they are worked out once a light and axis and added in the loop's own order
   (x, then y, then z), so the sum has the same bits (the build has no fused multiply-add: no `/arch`).
   PROOF: `tests/light_grid_test.cpp` (CMake: `tgpt_light_grid_test`, ctest `tgpt_light_grid`; no GPU) runs the old
   loop (kept as `build_cells_reference`) and the new over 4,000 made-up scenes (lights on cell borders to the last
   bit, ranges that end on a border, outside the grid, covering all of it, no range, NaN and infinite ranges and
   places, every kind) and a town of 985 lights and 300 clusters, twice through the same scratch: every byte of the
   cells, the entries, the weights and the three counts is the same (127.9 million entries compared). On this PC
   (others compiling beside it): a town's cells 3.51 ms a build before, 1.38 ms now.
2. THE FAR LISTS ARE NOT COPIED AND UPLOADED AGAIN AT EVERY BUILD (`upload_light_lists`). The buffer the passes read
   (`gridIndexBuffer_`) holds the cells' entries, then the far lists', as before, at the same places. The cells'
   entries go through the frame's ring as before. The far lists' are copied behind them ON THE GPU from an upload
   buffer of their own, which is written once when the far lists are made (`farUploadDirty_`, set where they are
   made) and never written again: each new set of far lists gets a new buffer, and the old goes when the GPU is past
   the frame (a buffer a frame in flight may read is never written). Same bytes: the far part of the buffer is
   `farEntries_` in both ways. Saves two copies of up to 5.7 MB a build, and as much of the ring.
   Memory: the upload buffer (as large as the far lists) is counted in the memory line's `transfer`.
3. THE GRID'S HASH IS ITS INPUTS, COMPARED WHOLE. The numbers that were mixed into a hash (in the same order: the
   corner, each light's place to a quarter metre, kind, range to a sixteenth, falls-off mark, the clusters' start and
   count, each cluster's place, range and red) are kept in a vector and compared with the last build's. The same
   decision (bar a hash collision, which would have kept a stale grid), without 48 multiplications a light, and the
   log can now say which number made each build necessary.
4. THE PLANTS' LIST IS KEPT BETWEEN FRAMES (`plan_plant_list`, `PlantPlan`). What is kept: which instances are plants
   in the wind, each one's key, its cell, each cell's members in the order of (key, place in the scene's list), the
   signature of those keys and their count, the members' vertices and indices together. What it is made from, one by
   one, and how a change of each is seen:
   - `instances_` and `materialIndices_`: copied from the scene's list object in `set_lists` and nowhere else written
     (cleared in `clear`). In hand-over 1 the list OBJECT is compared (`plan.list==sceneList_`; the plan holds it, so
     no new object can take its address). The plugin makes a new list object at every scan of the scene (every 0.25
     to 2 s, `SceneCapture.cs`), also when nothing changed, so hand-over 1 makes the plants' list anew at every scan.
     From hand-over 2 on `set_lists` compares the new list with the old BYTE FOR BYTE and counts `listEpoch_` on only
     when they differ (and `clear` counts it on): the list is kept across a scan that changed nothing ("Changes" 12).
   - `materials_` (their count and flags): written in `intake` alone; `materialsEpoch_` is counted on there.
   - the meshes: a handle's mesh object, its being ready, `skinned`, `meshRecords_` (submeshes, vertices, where its
     geometry records start), `geometryRecords_` (index counts): written in `intake` (a handle gets a new object),
     in `record`'s new meshes (ready or failed, both record tables), in `retire_meshes`, in `clear`;
     `meshEpoch_` is counted on at all four. (`skinned_positions` writes a character's `normals` number in its mesh
     record: not read by the list.)
   - which materials have a plant wind: `set_plant_wind` runs every frame; the sorted set of those materials is
     compared with the one the list was made from.
   - the Mega Geometry trial's split of a cell's small plants (trial 4 to 6): the list is never kept then.
   Everything else is done every frame as before: each plant's turn at its mesh's micromaps IN THE ORDER OF THE
   SCENE'S LIST (the order micromaps are asked for is the order they are baked in), and the whole of the cells'
   loop (the groups, their signatures' trial marks, their micromaps, the frame's room, the groups made anew).
   Where the list IS made, it is made with less work: a table says which materials have a wind; an instance's key
   is kept while the scene's list and the materials are the ones it was hashed from and its mesh has as many
   submeshes; one sort in place of a map of vectors. THE KEY IS THE SAME FNV NUMBER: a group's members stand in
   their keys' order, so another hash would lay a group's vertices out another way (other bytes in its buffer).
   PROOF AND NET: the old way stays in the code (`plan_plant_list_reference`), is made beside the kept list and
   compared cell by cell and member by member at every frame with the environment's `TGPT_TEST_RECORD_CHECK` set, and
   WITHOUT it at every 512th frame that kept its list (a few milliseconds once in 512 frames). A difference is said
   in the log ("PT record check: THE PLANTS' KEPT LIST DIFFERS ...") and from that frame on the list is the old
   way's, made every frame, for the rest of the session. `tests/source_checks.py` counts the places that write
   what the list is made from, so that a new one is not added without counting its epoch on.
5. A PLANT RANGE'S WIND AND A CLOTH'S WIND BY A TABLE (`plantWindTable_`, `clothWindy_`), filled where the maps are
   (`set_plant_wind`, `set_wind`): the map's own answer for every material number (a material numbered a million or
   more is asked of the map itself). Was: a search of an unordered map a range of every group a frame, and a submesh
   of every instance a frame.
6. NEW MESHES: "is it HDRP's water" asked of the scene's list once a frame with new meshes, not once a new mesh
   (24,000 instances walked for each of up to hundreds of meshes). The same answer: a mesh is water when an
   instance with the water flag names it.

In hand-over 2 (not in 1):

7. THE LIGHT GRID'S BUILD BESIDE THE RECORD (`record`, `build_light_grid`; `std::async`, as `ray_regeneration.cpp`
   uses it). Decided after `build_emitters`: from there on everything the grid is made from is this frame's (the
   lights since `intake`; the emissive clusters and where their records start since `build_emitters`; the camera).
   Where it is made anew, `build_light_grid` runs on another thread while the record goes on, and the record WAITS
   for it (`gridJob.get()`) at the place the grid was built until now, before the grid's two uploads. Why nothing
   can differ: it is the same function over the same inputs; between the start and the wait the record only READS
   `lights_` (twice: the lights' copy, `previousLights_`) and touches none of what the build reads or writes
   (`clusterLights_`, `clusterBase`, `gridCells_`, `gridIndices_`, `gridWeights_`, the builder's scratch, the far
   lists and their mark; grep: no use of them in `pose_plants`, `compact_plants`, `pose_water`, the characters'
   block or the uploads between); the counts for the log go under `statsMutex_` as before; the build calls no D3D12
   and none of the record's clocks, and `gridCorner_` (read every frame for `gridMin`) is set by the record after
   the wait. If the record throws in between, the future's destructor waits for the build before the stack unwinds.
   This is a second kind of change than the brief's two (the work is the same, the frame no longer waits for it):
   it is not "a result a frame late" (the brief's line), but say so if it is not wanted: it is one `std::async` to
   take out (the build then runs where the wait is).
8. A PLANT GROUP'S BUILD DESCRIPTION IS KEPT WITH THE GROUP (`PlantGroup::geometry`): made at the group's first pose
   by the same `plant_geometry`, named by every refit after. It was made anew for every group at every frame (three
   arrays allocated, zeroed, filled: 136 bytes a submesh of every plant in the wind). What it is made from is the
   group's own and written only where the group is made (`plan_plants`): its buffer and where positions and indices
   start in it, the vertex count, the submeshes' index ranges, which are solid, the links to micromaps. A group made
   anew is another `PlantGroup` object with none kept. (No `PlantGroup` is ever copied: grep.)
9. THE WATER'S BUFFERS' ADDRESSES asked once a frame (`pose_water`): `GetGPUVirtualAddress` was called four times a
   piece (1,400 to 1,800 pieces a frame). The same addresses.
10. THE WATER'S UNITS BY A HASH INDEX (`waterUnitIndex_`, `scene_water.cpp`). `plan_water` asks for every piece of
   the surface every frame by a key of four 64-bit numbers; the units live in a `std::map` (eleven steps through
   scattered nodes a search). They still do, and the map is still what is walked wherever the units' order matters
   (which unit is let go first, so which block of a pool is free first: another container would hand later pieces
   other places in the pools' pages, other bytes in the water's records). The index is a hash of the same keys onto
   the map's own units (a map's nodes stay where they are until erased), written at the three places the map gains
   or loses a unit (a new piece, a piece that could not be made, a piece let go; `release_water` clears both).
   The same unit for every key. With `TGPT_TEST_RECORD_CHECK` every frame checks that the index names every unit of
   the map and nothing else. `tests/source_checks.py` pins the three places.

11. THE GRID'S TWO ARRAYS LEFT FOR THE GPU BY THE BUILD ITSELF (from `reccpu-native-6` on). The cells and the
   cells' entries (0.4 MB and 2 MB and more in a forest) were copied into the frame's ring by the record after its
   wait. Now the build copies them, on its own thread, into an upload buffer of the scene's own, and the record only
   records the two copies on the GPU. Three such buffers (`gridStages_`); one is written again only once the GPU is
   past the frame that last copied from it (its fence against `completed_`, the test every retired buffer of the
   scene goes by); the record picks one BEFORE the build begins and makes it a quarter larger than the last build's
   arrays; where none is free, one cannot be made or the arrays do not fit, they go through the ring as before.
   The same bytes into the same two buffers. Memory: up to three upload buffers of about 3 MB, in `transfer`.

12. THE SCENE'S LIST COUNTS AS ANOTHER ONLY WHEN ITS BYTES DIFFER (`set_lists`, `listEpoch_`; from `reccpu-native-8`
   on). See 4: the plugin hands over a new list object at every scan. Two compares of about 2 MB a scan (not a
   frame). A list equal byte for byte has the same instances and material indices, so everything made from it is
   what it would be made again.

## GPU checks (the offline test's small modes, each with `TGPT_TEST_RECORD_CHECK`; logs in `%TEMP%\tgb`)

- HAND-OVER 1, `TGPT_TEST_OMM_ONLY` (two trees in the wind's pose in one group, a character, micromaps; 03:58 to
  04:06, 457 s of it the passes' first compile; `reccpu-gpu-a.log`): every picture comparison of the mode passes
  ("the four runs took the same pictures", 0 pixels differ between the marked run, every triangle tested and
  marked again); the tracer's log: "PT record check: the light grid's cells are the bytes of the loop they took
  the place of ... checked at every build" and "... the plants' kept list is the list made the old way ...
  checked at every frame" (2,864 frames checked, the list kept in all but 2 and made anew when the meshes
  changed), no "DIFFERS" line.
  ONE FAIL OF THAT MODE, NOT THIS JOB'S: "as it ships, the 42 plain triangles ... are spared the shader's test". The
  log line it reads does say "(42 more are spared it"; the check also wants the text
  "Micromaps trial: 0 (as it ships)", and since round 20 `cutout_text` writes "(leaves' and grass's micromaps at
  level 6, ...)" between the two (`scene.cpp`, unchanged by this job; the check's file is of 10-10, the same in
  main and in base20k). So `TGPT_TEST_OMM_ONLY` fails this one check on main's own build too: the check's expected
  text is stale. Not touched (not this job's file): for the coordinator.
- HAND-OVER 2 (`reccpu-native-8`), the same mode (04:33, the passes from the cache in 4 s; `reccpu-gpu-c.log`): the
  same result: every picture comparison passes with 0 pixels differing, the same one stale check fails; the log:
  "PT record check is ON ...", "... the light grid's cells are the bytes of the loop they took the place of"
  (now built on its own thread), "... the plants' kept list is the list made the old way" (2,958 frames checked),
  no "DIFFERS". Its "PT light grid 10s": a build 0.33 ms on its thread, the record waited 0.27 ms for it: in this
  scene of 13 instances the record has nothing to do meanwhile (its whole sum is 0.09 ms a frame); in the game it
  has 3 to 4 ms of poses between the start and the wait.
- HAND-OVER 2, the water's fixture (`TGPT_TEST_WATER_SURFACE` with `TGPT_TEST_WATER10_ONLY`, part `view`; 04:49 to
  04:57, `reccpu-gpu-d.log`): "PASS water10 checks", exit code 0, no FAIL; the log: "PT record check: the water's
  index names the map's own units ...; checked at every frame", the grid's cells again the same bytes, no
  "DIFFERS".
- Two small modes were run for hand-over 2 where the brief says one: the plants' mode does not reach the water's
  code, and the water's index is new there. Each took under a minute of the card once the passes were cached.
- NOT covered by any of this: a large scene. The fixtures hold 2 plants and 1 piece of water. The game with the file
  `record-check` beside the DLL (see "Switches") is the check that counts: one run of the town and the forest,
  a walk in it, then the log searched for "DIFFERS".

MUTANTS of the grid's builder against `tests/light_grid_test.cpp` (made in a copy, never in the tree;
`%TEMP%\tgb\reccpu-scripts\mutants.cmd`): a cell on the range's edge left out (4,667 failures), the entries dealt out
in the lights' reverse order (7,711), the weight's floor at 1 m forgotten (7,930): each FAILS the test. A fourth, the
distance's shares added in another order (z before y), PASSES it: for lights at such places the three squares add up
without rounding in a double, or the last bit of the sum is below what a `float` weight holds, so no output byte can
tell the two orders apart. The order is nevertheless the old loop's (x, y, z), and a source check pins the line.

## Log lines added or changed (the tracer's log)

- "PT record CPU 10s": the same line, its sections now named for what runs under them, and the large ones split:
  `new meshes' uploads and builds`; `decals`; `materials' textures and programs`; the plants' plan in five
  (`before it (scratch, Mega Geometry's cuts)`, `the list of plants by cell`, `each plant's micromaps`, `the cells'
  groups`, `groups made anew`, and `the check of the kept list` when one ran); `the material index list ...`;
  `the cloth's plan`; `the instances' loop`; `things the game moves`; `the water's records and plan`; `the mesh and
  geometry records' upload`; `the cloth's pose`; `the emitters' build`; `the plants' compaction`; the plants' pose in
  three (`winds, ranges and group records`, `the pose pass's dispatches`, `builds and refits`, and `Mega Geometry's
  clusters` on trial); `the plants' groups' instances`; `the water's pose`; `the characters`; `the instance records'
  upload`; `the lights' records and the lamps' tree`; `the light grid: what changed`; `the light grid's build: the
  cells`; `... : the far lists`; `the light grid's uploads`; `the top level`.
- NEW "PT light grid 10s": in how many frames the grid was made anew, and for each cause in how many of those builds
  it applied: a light's range (and how many lights' at once at most), a light's place, kind, falls-off mark, the
  lights' count, the emissive clusters, the grid's corner.
- NEW "PT plants' plan 10s": the plants and cells, in how many frames the list was kept and in how many made anew,
  and what was no longer what it was made from (the meshes, the scene's list, the materials, the winds), checks run;
  and (hand-over 2) what PT-125's marks cost a frame, timed in one frame of 64.
- (hand-over 2) "PT light grid 10s" also says how long a build took on its own thread (the cells, the far lists and
  the copy for the GPU) and how long the record waited for it; "PT record CPU 10s" has `the light grid: the wait
  for its build ...` in place of the two build sections.
- NEW, only with a check: "PT record check: ..." (once when the first comparison agreed, at every difference).

## Switches added

- Environment `TGPT_TEST_RECORD_CHECK` (any value; read once, at the first recorded frame): the light grid's cells are
  also made by the old loop at every build and compared; the plants' list is also made the old way at every frame
  and compared; the water's index is compared with its map every frame. For the offline GPU test and for one run of
  the game. Slow (it does the old work on top).
- From `reccpu-native-5` on, the same by a FILE named `record-check` beside the DLL
  (`BepInEx\TGPathTracing\Native\record-check`, empty; as `measure-waits` is read): for the game, which Steam
  starts. The log then begins with "PT record check is ON". Take the file away again after the run.
- No setting, no FrameConstants row, no export, no record changed.

## Tests

- `tests/light_grid_test.cpp` (new; CMake `tgpt_light_grid_test`, ctest `tgpt_light_grid`: the tests are 18 now).
- `tests/source_checks.py`: three pinned lines moved with the code (the entry's weight is in `light_grid.hpp`; the
  far lists follow the cells' entries through `upload_light_lists`), one check added
  (`test_path_tracing_record_keeps_only_what_it_can_prove`). 118 pass.

## Not done, and why

- THE WATER'S POSE AND THE CHARACTERS (2.3 ms in the town with 1,379 pieces and 98 characters, 2.8 to 3.0 in the
  forest with 1,793 and 67; the largest thing left). Read line by line: the record's own work there is a few dozen
  nanoseconds a piece; the time is the calls themselves, one `BuildRaytracingAccelerationStructure` a piece of the
  water's surface and a character every frame (about 1.3 microseconds each in the driver), plus a dispatch a
  lattice level. Under this job's rule (the same commands) there is nothing to cut but the four address calls a
  piece (done, "Changes" 9). The two ways that would cut it are under "Choices".
- THE PLANTS' POSE (1.2 to 1.3 ms in the forest with 258 groups refitted a frame): a dispatch, a refit and PT-125's
  four markers a group a frame, again calls. Cut: the description (8). The markers are timed apart now.
- THE NEW MESHES' FRAMES (the first seconds after a load: 1.4 to 2.2 ms a frame on average, single frames of 100 to
  165 ms): one upload buffer as large as the frame's new meshes is created, its fresh pages faulted in and filled,
  and `GetRaytracingAccelerationStructurePrebuildInfo` is asked twice a mesh. Not touched: it is the world's
  arrival (PT-98's budgets by the clock), not the steady frame. Cut there: the per-mesh walk of the scene's list (6).
- THE INSTANCES' LOOP (every instance of the scene every frame: 0.2 to 0.6 ms with the cloth's plan): it depends on
  too much that changes by the frame (what the game moves, the cloth chosen by distance, each mesh's micromaps, the
  trial switches) to be kept between frames with a test I could prove. Cut: the cloth's wind by a table (5).
- THE LIGHT GRID STILL MADE WHOLE when one light moved. Asked for by the coordinator ("partial when a few lights
  moved"). Not built: with the build beside the record (7) the frame no longer waits for it, so a partial build
  would save CPU time of another core, not frame time; and a partial build that gives the same bytes must still deal
  every entry out to its list again (a list's place in the buffer moves when a list before it grows). The cheaper
  step that remains: keep each light's cells from build to build while its place, its range and the grid's corner
  are bit-equal (about half of a build's 1.4 ms on the other thread). Say if it is wanted.
- `lamp_tree::build` every frame: 0.075 ms for 985 lamps (measured alone). Left.

## Choices for the owner (each would change something this job may not)

1. A FLAME'S RANGE THAT DOES NOT FLICKER (the plugin, `SceneLights.cs` `Reach`). Today a light's range follows its
   intensity (as far as it still gives 50 lux), so every flicker moves the range and the grid is made anew every
   frame in a town of flames. With the reach taken from the flame's own steady intensity (the light's intensity
   before the game's flicker script scales it, or the largest of the last second) the grid would be made only when
   a light really moves. What changes: which cells list a light, by up to the flicker's share of its reach (the far
   list counts it past that, as now): the same picture in the mean, other noise. With (7) it no longer costs frame
   time, only a core's 1.4 ms and 0.2 ms of uploads a frame: so this is small now.
2. THE WATER'S PIECES IN FEWER STRUCTURES. 1,400 to 1,800 pieces are 1,400 to 1,800 bottom-level structures, each
   refitted by a call of its own every frame (2 to 2.5 ms of the CPU that records, and 0.6 to 0.8 ms of the GPU's
   scene stage). The plants show what grouping does: a group is one structure of dozens of submeshes and one call.
   Pieces grouped by pool page or by cell into structures of many geometries each would be a few dozen calls. The
   same triangles; but an instance would be a group, not a piece, so the records the passes read change (which
   record a hit names): a renderer's change with its own check, not this job's.
3. THE STRUCTURES' COMMANDS RECORDED ON A SECOND LIST BY A SECOND THREAD (the water's and the plants' poses and
   refits: 3 to 4 ms). D3D12 is made for it: a list a thread, executed in the same order
   (`ExecuteCommandLists` of [before, water and plants, after]). The same commands in the same order on the GPU,
   so the same frame. It means the tracer's one list a frame becomes three (`tracer.cpp`: allocators, the stage
   marks, the progress markers, the error paths), and the scene's uploads through the frame's ring must stay on
   one thread. A job of its own, with the game as its test; it is the one way I see to take the driver's time for
   2,000 calls a frame out of the frame without changing a structure.
4. PT-125'S MARKERS: two `WriteBufferImmediate` after every dispatch and every refit of every group of plants,
   every frame. The cause is fixed (the brief); "PT plants' plan 10s" now ends with what they cost a frame (timed
   in one frame of 64). Taking them out, or keeping the frame's two stamps and dropping the per-group ones, is the
   coordinator's.
5. THE CHECK EVERY 512TH FRAME (the plants' list made the old way and compared, by itself, in every build): a few
   milliseconds once in 512 frames. It is there because a stale list is a picture defect; if the hitch is not
   wanted, `&511u` in `plan_plants` is the number (or the check only with the environment's switch).
