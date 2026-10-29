# waterblas - the water's pieces in fewer acceleration structures (PT-99)

Worker: Claude subagent "waterblas", copy `%TEMP%\tg-agents\wt\waterblas` (base `base20o`), started 2026-10-12 ~08:25.
Brief: `docs/agents/briefs/brief-waterblas.md`. Build folder `%TEMP%\tgb\waterblas-build`, scripts
`%TEMP%\tgb\waterblas-scripts`, staged DLL `%TEMP%\tgb\waterblas-native`, run logs `%TEMP%\tgb\waterblas-runs`.

## State when stopped

(kept current; newest first)

- 10:55 DONE, HANDED OVER (message to the coordinator, 10:55). HAND-OVER 2 (`%TEMP%\tgb\waterblas-native-3`, SHA-256
  B4DA3E0E..., built 10:01) IS THE COPY'S SOURCES AND THE ONE TO MERGE: hand-over 1 plus the pools' free stretches,
  the frame's count in one pass, the wider self-check. All its offline checks passed (10:02 to 10:52: the walk bit
  for bit the old code's at both paces, with a second water of many small triangles too; three mutants fail;
  water10 and foam pass; "Hand-over 2's checks"). NOT SEEN IN THE GAME: hand-over 1 was
  (standing scenes: it works and saves 2.5 ms of the record a frame); OWED BY THE GAME for hand-over 2: a walk and
  the file `record-check`. No script of mine is running; nothing is half-edited; no "MUTANT" is in the source;
  source checks 120 of 120. What a next worker would need is under "Not done, and what I would look at next".
  Left on the disk under `%TEMP%\tgb`: `waterblas-native` (hand-over 1), `waterblas-native-3` (hand-over 2),
  `waterblas-base` (main's DLL of 08:10: the yardstick), `waterblas-runs` (the logs), `waterblas-scripts`,
  `waterblas-build` (the one build tree: to delete once the merge is in). The mutants' stages, the step between and
  the copied pass cache are deleted.
- 09:50 the coordinator ran hand-over 1 in the game: it works and saves what was reckoned ("In the game" below).
- 09:36 hand-over 1 with the coordinator. 09:16 the code written and built. 08:55 the plan.

## HAND-OVER 1 (09:36): a DLL to time in the game

`%TEMP%\tgb\waterblas-native\TGPathTracing\TGPathTracing.Native.dll`
SHA-256 `5343B2B8C137DB7529B1A10EA4ED84D39081631947B1368C25EFABF7A1182E1A` (built 09:14 from this copy; every pass's
header is main's of 08:09 but `pt_water_mesh.h`, the pose pass; MEGA_GEOMETRY and RAIN_WETNESS on, as main's build).
In this DLL a block given back still waits for the next asked for of exactly its size (the pools as they were), and
the frame's count goes through every group's members twice.

### In the game (the coordinator's run `artifacts\harness\20261012-094311`, `speed-now`, the world running, no denoiser)

The record's CPU, ms a frame, main this morning (runs `-085954` beach, `-082625` town and forest) against hand-over 1:

| | pieces, groups | the water's pose | the water's records and plan | the scene's record, sum |
|---|---|---|---|---|
| beach | 2,459 in 66 | 3.72 to 0.20 | 0.79 to 0.86-0.90 | 6.50 to 3.84-4.04 |
| town | 1,379 in 40 | 2.05 to 0.14-0.15 | 0.52 to 0.66-0.68 | 6.65 to 5.43-5.70 |
| forest | 1,793 in 51 | 2.70 to 0.14-0.15 | 0.59 to 0.65-0.66 | 5.69 to 3.20 |

The scene stage on the GPU: beach 2.91 to 2.51 ms, town 6.19 to 6.00, forest 4.93 to 4.64 (the card was not quiet:
indicative). Standing: "1793 pieces in 51.0 groups ... 0 groups built". The structures' blocks: forest 107.2 MB to
94.6, beach 142.2 to 125.3, town 83.8 to 74.0 (a group's structure is smaller than its pieces' own were); the
structures' pages: forest 160 MB to 96, beach 160 to 128. The picture, the world running: beach and forest as main's within the run's
own scatter; the town three levels of luma apart (a running town; the coordinator takes a frozen A/B at the merge).
NOT YET IN THE GAME: a walk, and the record's self-check (the file was not beside the DLL).
THE PLAN COSTS 0.07 TO 0.15 ms MORE in hand-over 1 (it went through every group's members twice a frame and every
piece three times): hand-over 2 counts in one pass ("The frame's count" below).

## HAND-OVER 2 (10:15, its offline checks passed): the one to merge

`%TEMP%\tgb\waterblas-native-3\TGPathTracing\TGPathTracing.Native.dll`
SHA-256 `B4DA3E0E77EE929D36EFDD006E41B1D0ED7471E8637E558D4F67A335E0994C99` (built 10:01 from this copy as it stands; the
same headers as hand-over 1). (`waterblas-native-2`, 3967D12F..., is a step between: the pools alone; not to be used.)
What it adds to hand-over 1, none of it in the picture:
1. THE POOLS KEEP THEIR FREE STRETCHES ("The pools" below).
2. THE FRAME'S COUNT IN ONE PASS over the frame's pieces, no second copy of the pose pass's records, and no look at
   the members of a group whose every piece is wanted ("The frame's count" below).
3. THE SELF-CHECK also checks the pools (every page is its groups' blocks and its free stretches, side by side,
   nothing over or between), the frame's material list, the frame's count against a plain recount, the kept
   geometry records.
FOR THE GAME: one run with the file `record-check` beside it and a walk (`forest@camp-walks`), then the log searched
for "PT record check: THE WATER'S" (a defect of mine) and "PT water groups 10s" (the walk's builds a frame) and the
pages in "The water's surface: ...".

## The coordinator's one-line fix (09:35) is in the copy

`tests/pathtracing_nosun_checks.inl` line 123 (`TGPT_FRAME_FOG_FROXELS`, which `tgpt_api.h` no longer has: the test's
exe did not compile): replaced as the coordinator's message said; the file is byte for byte main's now. Until then my
build of the exe had handed the compiler the name as 0 through its `CL` environment variable; that is out again.

## What was built (as the plan said, with two changes named below)

### What a piece was (read, not changed)

A piece ("unit") was: a block of the vertex pool (its positions, undisplaced points, positions a frame ago, each
`vertexCount` x 12 bytes), a block of the structures' pool (its own bottom level, ONE geometry), a view of its
vertices, a mesh record of its own, an instance of its own. Its indices are one pattern shared by every piece (a
level's pattern: the indices of one base triangle after another, each cut 4^level times, numbered from the piece's
own first vertex).

Two facts decided the design:
1. THE POSE PASS WRITES EVERYTHING EVERY FRAME (`pt_water_mesh.hlsl`: position, undisplaced point AND the position a
   frame ago are worked out from the game's wave data of this frame and of the frame before; nothing is read from
   last frame's buffer). So a piece's vertices carry nothing from frame to frame: they may lie anywhere this frame.
   Only its bottom level is kept (refitted).
2. A PIECE THAT IS KEPT BUT NOT WANTED THIS FRAME IS NOT TRACED (`waterUses_` holds this frame's pieces only; a
   piece is dropped `kWaterKeepFrames` = 120 frames after its last use). So a structure of many pieces must be able
   to leave a piece out for a frame without being built anew.

### A group is a mesh of many submeshes, as a group of plants is; NO pass that traces is changed

A GROUP (`scene.hpp` `WaterGroup`) is some pieces of one water slot and one cut level (how often a base triangle is
cut: 0 to 5), at most 32,768 triangles (16 pieces' worth) and 256 pieces:
- ONE block of the vertex pool for all of them, laid out as a piece's was (all positions, then all undisplaced
  points, then all positions a frame ago), each piece's vertices one after another in it;
- ONE bottom level of one geometry a piece (each geometry names its piece's part of the block and the shared
  pattern from its start: exactly the triangles the piece's own structure had), refitted by ONE call a frame;
- ONE mesh record (the group's view, its vertex count, `geometryCount` pieces) and ONE geometry record a piece;
- ONE instance (the same mask `kMaskWaves`, the same flags, `kInstanceWaves` and the slot).
A hit names its piece by the instance's ID and `GeometryIndex()`, through the code every mesh of several submeshes
already goes through: `meshes[inst.mesh]`, `geometries[mesh.geometryBase + geometry]`, `materialIndices[
inst.materialBase + geometry]`. So `fetch_hit`, `water_hit`, `triangle_indices` and every other place below are
BYTE FOR BYTE what they were, and no pass of the tracer is compiled again but the pose pass.

How the shared pattern still serves (no index buffer of a group's own): a level's pattern is the indices of base
triangle 0, 1, 2, ... each numbered from `t x (vertices a base triangle)`. Pieces of ONE level laid one after
another in a group's block are exactly that numbering continued: the piece that starts at base triangle T of its
group reads the pattern from `T x 4^level x 3` on (its geometry record's `indexStart`), and those indices are its
vertices' numbers in the group's block. For that the pattern holds a group's worth a level (2.4 MB in all, once; it
was 0.15 MB), and a group's pieces share one level. The structure's own geometry names the piece's part of the block
and the pattern's start, as a piece's did.

THE SAME TRIANGLES: a piece's vertices are written by the same pass from the same record (`WaterTri`: only where
they go changes: `at`, `span`), its triangles are the same pattern over them, wound the same way, in the scene's
own space (identity transform, as before). What a hit reads: the three vertices' positions, undisplaced points and
positions a frame ago by the same indices from the same values, the slot from the instance's flags, the material
of no kind from `materialIndices[base + geometry]` (the frame's list now holds 256 entries of that one material,
as many as a group can have pieces; every group names the same base).

### Pieces that come and go: no group is built anew because a piece left

- A piece of a group that this frame does not want is COLLAPSED: the pose pass writes all its vertices to one
  point (its own first corner, undisplaced: so the group's boxes stay where the group is), once, in the frame it
  stops being wanted. Its triangles have no area: no ray can meet them (DXR: a degenerate triangle is legal in a
  build and in a refit, and may become a real one again at a later refit; never NaN, an "inactive" one, which a
  refit may not bring back). The group is refitted as every frame. Wanted again within its 120 frames: posed as
  always, refitted: it is back. This is the one change to a shader: bit 7 of `WaterTri.node` in
  `pt_water_mesh.hlsl` (the record's `index`, `i`, `j` are then the point's bits), the pose pass alone.
  When the scene's origin moves, the collapsed pieces' points are written again (they are relative to it).
- A piece dropped after its 120 frames leaves a collapsed geometry behind in its group ("dead") until the group is
  made anew.
- NEW pieces of a frame (the camera came nearer, water came into reach) are built together: one new group a slot
  and level (the same triangles built as before, in a handful of calls instead of one a piece), in the order the
  walk over the surface met them (along its curve: a group's pieces lie together).
- So a walk makes many small groups. They are put together again by a budget that changes speed and memory alone
  (`plan_water`, step 2): AT MOST ONE GROUP A FRAME is made of several of the same slot and level, at most 32,768
  triangles built for it. First a group half of whose triangles are dead; else, only while more groups are in use
  than half as many again as the fewest that could hold the frame's pieces (and 16 more than those at least): the
  smallest groups first, and after the first two a group only while it is no more than twice what is taken already
  (so a large group is not built again every frame of a walk for the sake of a few small ones). Standing still it
  ends by itself (every such step makes fewer groups or sheds dead geometry) and then a frame is refits alone.
- Nothing ends the water: no count of groups or pieces, no distance; more pieces are more groups. A group that
  cannot get its blocks is halved, down to one piece, which is what a piece was before; a piece that cannot get
  them is not traced that frame and asked for again by the next, as before.

TWO CHANGES AGAINST THE PLAN OF 08:55: (1) "only while more than 48 groups are in use" became "more than the
fewest possible plus half (16 at least)": the forest's 1,793 pieces are 1,550,054 triangles (run `20261012-083016`),
which is 48 full groups by itself, so a fixed 48 would have kept the putting-together busy for ever; (2) a new
group's pieces are collapsed to their OWN first corner, not their base triangle's (a base triangle of the sea is
kilometres long).

### What a frame's calls become (expected; the game's numbers are the coordinator's)

The forest (run `20261012-083016`, main's DLL: 1,793 pieces, 1,550,054 triangles, "the water's pose 2.70 ms" of the
record's CPU a frame, 107 MB of structures and 30 MB of vertices in blocks): 48 groups by its triangles, some
more by its levels: about 55 refit calls a frame standing, where there were 1,793. On a walk: the same, plus one
build a slot and level with new pieces (their triangles only, as before), plus at most one group put together.
The top level has 1,740 instances fewer.

### The pools (hand-over 2): a page keeps its free stretches

The two pools (16 MB pages of vertices, 32 MB pages of structures) handed a block back to a list by its SIZE, and
the next block asked for of exactly that size took it. That fitted pieces (a handful of sizes). A group's blocks
are of every size, and a walk makes and lets go of some every frame: a block given back would mostly wait for a
size that does not come again, and a page with one long-lived group in it never goes back. So a page now keeps its
FREE STRETCHES (offset, bytes; two that touch are one): a block is cut from the first stretch of the first page
that holds it, and one given back joins the stretches it touches (`water_block`, `water_block_free`; `scene.hpp`
`WaterPool`). The sizes are as they were (PT-87: an eighth of a power of two above what is needed; the test's
switch still gives powers of two), a page still goes back 120 frames after its last block, the blocks still go
back at once (one queue: a frame the GPU has not finished reads them before anything recorded later writes them).
Every block starts on a multiple of 512 (a structure wants 256).
What it does (the walks' logs, "The water's surface" lines; MB of pages, vertices' + structures'):

| the fixture's walk | peak of blocks in use | most pages, old code | hand-over 1 | hand-over 2 |
|---|---|---|---|---|
| 3 m a frame, in groups | 89 + 283 | (160 + 544, with 147 + 529 in use: a piece keeps its blocks 120 frames) | 112 + 352 | 112 + 320 |
| 0.5 m a frame, in groups | 55 + 171 | (64 + 224, with 54 + 196 in use) | 64 + 192; after the test's switch 128 + 384 | 64 + 192, and no more after the switch |

So in groups the water holds less than it did (a group made anew drops the pieces no frame wants; a group's
structure is smaller than its pieces' own), and with the free stretches the pages are the peak of what is in use
plus less than a page each. Hand-over 1's lists by size held up in one walk and grew to twice that once blocks of
another kind had been through the pools (the test's switch: not a thing the game does, but the sign of what a long
walk would do).

### The frame's count (hand-over 2): one pass

Hand-over 1 counted each group's frame by going through every member of every group (a look at each piece), went
through them again for the records, and copied the pose pass's records twice. Hand-over 2: the groups' counts are
set to nothing and ONE pass over this frame's pieces adds each to its group (or puts it by, if it has none yet); a
group's dropped pieces are read off its own entries (no piece is looked at); a group every piece of which is wanted
and none of which is marked collapsed (the usual frame) gets its mesh record and its kept geometry records copied
in, and nothing else; the pose pass's records are written once, in the pieces' order by level as before. Nothing
of the count is kept between frames; what is kept in its place is each group's geometry records (made with the
group from what never changes: its level, each piece's first base triangle and count) and one mark "a member may be
collapsed". The self-check counts the plain way beside it every frame and makes the records again.

### Found on the way, NOT this job's: a camera cut does not make two runs the same for 15 frames

At 0.5 m a frame a walk of the part ends 8 m from where the next begins. The three walks (each begun with a cut,
`TGPT_FRAME_RESET`, frame index 0) then differ from one another in their first 15 frames: 85 of 36.6 million
channels in the frames kept, by 0.15 at most. MAIN'S DLL OF 08:10 DOES EXACTLY THE SAME (the three walks' hashes
are `13381c38b40afbf0`, `f73b10c8e558bdfb`, `b7bb5b7c88cf5f6a` with the old code, with hand-over 1 and with
hand-over 2 alike): something of the frames before a cut reaches the 15 frames after it when the camera was near.
At 3 m a frame (48 m apart) nothing does. Which state it is I did not look for (15 frames: a history's length);
`base-groups-slow.log`, `new-groups-slow.log`, `new2-groups-slow.log`. The part now begins every walk with the
same 32 frames at its first place, so that the walks start alike at any pace.

## What is kept between frames, and why it cannot go stale (PT-125, PT-134)

- A piece's place in its group (its first base triangle T, so its vertices' place in the block and its
  `indexStart`), and its pose records' `at`, `span` and `target` (a place in the group's block, the page's view):
  relative to the group's block, which never moves while the group lives (the water's pools do not compact; a page
  goes back only when no block of it is held). Written once, in `water_group_make`.
- A group's list of pieces, in the order of its geometries: never reordered, never shortened (a refit must name
  the geometries of its build). A piece dropped: its entry stays, its unit null. A group made anew is another
  object with other blocks.
- A group's build description (`WaterGroup::geometry`: the addresses of its pieces' vertices and of the level's
  pattern): made with the group. The page's address is the page's for as long as the block is held, the pattern's
  for as long as there is water (`release_water` lets go of every group with it).
- A group's geometry records (hand-over 2: `WaterGroup::records`, each piece's stretch of its level's pattern):
  made with the group from its level and each piece's first base triangle and count, none of which is written
  again; they hold no number of a frame's buffer (where they lie in the frame's list is the mesh record's
  `geometryBase`, written every frame).
- "This piece's vertices hold the one point now" (`Member::collapsed`, and the group's mark that one may be): true
  once the frame's list has run, as "its structure is built" is. A frame whose list is recorded and never runs
  switches tracing off for the session (`tracer.cpp` ~5278: "the scene marked meshes built ... for a list that
  never runs"), so neither can be believed wrongly. The point is relative to the scene's origin: when that moves,
  every mark is taken back and the points are written again.
- Every record a pass reads (mesh, geometry, instance records, the pose pass's list) is laid out again every frame
  from the groups in use: no number of a frame's buffer is kept. Each group's count of the frame (its pieces the
  frame wants) is made anew every frame.
- The links piece -> group and group -> piece are written where a group is made or let go and at the two places a
  piece leaves the map (`water_unit_gone`; `tests/source_checks.py` pins both).
- THE RECORD'S SELF-CHECK is extended to the water (`water_check`, with the file `record-check` beside the DLL or
  `TGPT_TEST_RECORD_CHECK`; every frame): every group's members lie one after another and add up; its kept
  description is what its block and members give again; its indices lie within the pattern; every piece's pose
  records point at its place in its group; every piece a frame wants is in a group the frame traces and is not
  marked collapsed; a traced group holds no piece that is neither wanted nor collapsed; no piece names a group that
  is not held; the frame's records are the traced groups'. The log: "PT record check: the water's groups agree
  with their units and blocks (...); checked at every frame" once, or "PT record check: THE WATER'S GROUPS DIFFER
  from what would be made again: <what>".

## Every place a pass turns a hit on the water into a record (each read against the code)

All of them go through the instance record, then the mesh record and the geometry record by `geometry`, or the
material by `materialBase + geometry`. With a group these are the group's instance and mesh record and the PIECE'S
geometry record (the hit's `GeometryIndex()`), which is what each already does for any mesh of several submeshes.
None of these files is changed.
1. `pt_common.hlsli` `fetch_hit` (the instance, `meshes[inst.mesh]`, `triangle_indices(mesh, geometry, primitive)`:
   `geometries[mesh.geometryBase + geometry].indexStart`, the three positions at `idx x 12` of the mesh's view,
   `h.material = materialIndices[inst.materialBase + geometry]`, then `water_hit`).
2. `pt_water_surface.hlsli` `water_hit` (the slot from the instance's flags; undisplaced points at
   `mesh.vertexCount x 12`, positions a frame ago at `mesh.vertexCount x 24`, by the same three indices: the
   group's vertex count and the group's own numbering, which is how the pose pass's `span` lays them out).
3. `pt_water_surface.hlsli` `trace_water` (through `fetch_hit`), `water_medium`, `water_straight`, `water_over`
   (the slot from `instances[InstanceID].flags`: a group's slot is its pieces').
4. `pt_water_sun.hlsli` `water_above` (the instance, the mesh, `triangle_indices(mesh, GeometryIndex,
   PrimitiveIndex)`, the undisplaced points at `mesh.vertexCount x 12`).
5. `pt_waves.hlsli` `water_base` (HDRP's LEVEL water, `MASK_WATER`: not the pieces; untouched by this).
6. `pt_common.hlsli` `passes_through` ("no" for `INSTANCE_WAVES` before any record by geometry is read),
   `first_person_inside`, the light's straight ray (~3011), `leaf_pass`: the material by `materialBase +
   geometry`: the water's, for every piece of a group. `piece_solid`, `glass_solid` and the decals' light layers
   (~2372): the piece's geometry record's `flags` and `layers`: 0, as the level records' were.
7. `pt_query.hlsli`, `pt_gi.hlsl` ~99, `pt_specular.hlsl` ~146 (the instance, geometry and primitive kept and handed
   to `fetch_hit`; the material by `materialBase + geometry`), `uv_frame`, `triangle_slant`, `caustic_vertex`
   (the mesh by the instance, the triangle by geometry and primitive; the water has no texture coordinates or
   normals, so the first and the last return at once, as before).
8. `pt_primary.hlsli` (`load_tri`, `hit_motion`: the mesh's `previous` is `kInvalid` for the water as before, its
   motion comes from `h.waterPrevious`; ~474: the instance and material into the first hit's words; ~581),
   `pt_psr_capture.hlsli` `psr_previous_point`.
9. `pt_compose.hlsl` 147: the debug view "instances" colours by the instance's ID: it shows groups now, not
   pieces. A debug view; the one visible change.
10. `ray_chance(instance, geometry, primitive, salt)`: random numbers by a hit's instance ID (thin cloth's
   coverage, glass's paint, skin's walk). Not used for the water's own hits. But CHARACTERS come after the water in
   the frame's records, so their IDs are lower by (pieces - groups) and their random numbers are others: other
   noise on a character's skin and cloth, the same picture in the mean. (Those IDs already changed whenever the
   water's pieces changed, at every step of a walk.) The emitters' table (PT-135) is laid out by the frame's
   records every time they differ: fewer records, nothing else.

## The old way, for the comparison

The environment's `TGPT_TEST_WATER_UNGROUPED` (the TEST's; read every frame, as `TGPT_TEST_MEMORY2_OFF` is): every
group holds ONE piece and nothing is put together: one bottom level of one geometry, one instance and one refit
call a piece, in the order of before. Changing it within a run lets go of every group first. Never a setting (a
source check holds that). It is the old STRUCTURE made by the new code, not the old code; so the walk's hashes are
also taken with main's DLL of 08:10 (the old code itself): see the checks.

## Checks (my own small modes of `tgpt_gpu_test`, each asked of main's `-GpuCheck` right before its start)

Logs: `%TEMP%\tgb\waterblas-runs\<job>.log` (the test's lines, then the tracer's record-check and water lines).

### The same picture: BIT FOR BIT, against the old structure and against the old code

`TGPT_TEST_WATER_SURFACE=1 TGPT_TEST_WATER_SEA=1 TGPT_TEST_SEA_PART=groups` with `TGPT_TEST_RECORD_CHECK=1` (new part 9 of
`tests/water_sea_gpu.inl`): a WALK over the fixture's sea at 640 x 360, 212 frames: out 96 m at 3 m a frame, back at
twice the pace, a stand of 140 frames (so the walk's pieces are dropped after their 120 and their groups made anew),
out again. Taken three times with the same random numbers: in groups, ungrouped (the test's switch), in groups again.
Every frame's picture is hashed (every bit of every channel), every 4th kept and compared channel by channel.
- Hand-over 1 (`new-groups.log`, 09:23 to 09:31, 8 minutes of it the driver making two passes for the first time):
  **0 of 212 frames' hashes differ between groups and ungrouped; 0 between the two walks in groups**; of the 53 frames
  kept, 0 of 36,633,600 channels differ. PASS.
- THE OLD CODE (`base-groups.log`: the same test's exe with main's DLL of 08:10, `E98E3218`, staged in
  `waterblas-base`): the hash over all 212 frames is `fd9a9a9be44a164a`, the same as hand-over 1's three walks, and so
  are the six single frames printed (0, 31, 47, 100, 187, 211). So the picture in groups is the old code's picture,
  not only the new code's with groups of one. (That run "fails", as it must: the old DLL has no groups to count.)
- Why bit for bit and not "by rounding": the triangles are the same vertices written by the same pass from the same
  records, in the scene's own space under an identity transform, and a ray-triangle test does not depend on which
  structure holds the triangle; what a hit reads is the same numbers at other places. Only a ray that met a shared
  edge exactly could tell the structures apart (which of two triangles reports it); none did in 212 frames x 3.
- What the walk made the groups do (the log's water lines, `walklines.py`): 1,760 to 1,910 pieces traced in 50 to
  97 groups (150 held at most); 597 groups built in the 212 frames (536 of new pieces, 76 put together of 458);
  18,040 pieces collapsed (and those wanted again brought back); 337 geometries of dropped pieces at most. The
  part checks that each of these happened. THE FIXTURE WALKS FAR FASTER THAN THE GAME (3 m a frame: 71 pieces made a
  frame on average, 346 at most; the game's walk made 16 in a frame): its builds a frame are an upper end.
  Triangles built a frame: 83,294 in groups, 50,572 ungrouped (the new pieces' alone): the putting-together adds
  what its budget allows (32,768 a frame) while a walk goes on.
- THE RECORD'S SELF-CHECK ran every frame of all this: "PT record check: the water's groups agree with their units
  and blocks (50 groups, 1796 units in them); checked at every frame", no "DIFFER" line.
- MUTANTS (`waterblas-scripts\mutant.py`, each built in the copy and taken out again; `mutn-`, `mutc-`, `mutm-groups.log`):
  1. `neighbour`: each piece's geometry record names its NEIGHBOUR's stretch of the pattern (a hit reads another
     piece's vertices): FAILS, 212 of 212 frames differ, 40 % of the kept frames' channels.
  2. `nocollapse`: a piece its group holds and the frame does not want is not collapsed: FAILS, 211 of 212 frames
     differ (all but the first, which has no such piece).
  3. `material`: the frame's material list holds ONE entry of the water's material: PASSES THE PICTURE (twice, the
     second time with every material the scene does not use made clear glass): no picture shows it, because the
     water's own path never asks a water hit's material (`hit.water` decides first; a light's glass walk culls
     opaque triangles). It is held by a source check and, from hand-over 2 on, by the self-check, which the part
     now reads (see hand-over 2's run).
- `TGPT_TEST_WATER10_ONLY` (`new-water10.log`): "PASS water10 checks". The sea's part `foam` (`new-foam.log`: the
  fixture's own checks of the picture): "PASS sea checks".

### Hand-over 2's checks (10:02 to 10:11; `<job>-3.log`, `-3-slow.log`, `-4.log`, `-4-slow.log`)

The part begins every walk with 32 frames at its first place since then (so the hashes are others than above).
- The walk at the fixture's pace, hand-over 2 (`new3-groups-3.log`, again with the test as the copy has it now
  `new3-groups-4.log`): 0 of 212 frames differ between groups and ungrouped, 0 between the two walks in groups; the
  self-check agreed at every frame ("the water's groups agree" once, no "DIFFER"). PASS. THE OLD CODE
  (`base-groups-3.log`, main's DLL of 08:10): the same hash for all 212 frames, `e0f961ae32d5f720`, as hand-over 2's
  three walks.
- The walk at 0.5 m a frame (`new3-groups-3-slow.log`, `base-groups-3-slow.log`): hand-over 2's three walks hash to
  `5173448eeb239301`, `1a7d5e510c862421`, `ccd8ffe799b9c6e2`, AND SO DO THE OLD CODE'S THREE. (The walks differ from
  one another there, by the old code too: "Found on the way" above; so at another pace than 3 m the part prints its
  comparison and holds only the rest.) What that walk made the groups do: 1,966 pieces made in 212 frames (9 a
  frame, 92 at most: the game's kind of pace), 50 to 75 groups traced, 161 groups built (0.76 a frame; 19 of them
  put together of several), 4,282 pieces collapsed, 71 dead geometries at most; triangles built a frame about
  14,000 where the new pieces alone are 8,700.
- WITH A SECOND WATER OF MANY SMALL TRIANGLES (the part as the copy has it now; `new3-groups-5.log`,
  `base-groups-5.log`, 10:35): the sea is two triangles two kilometres long, so each of its pieces is one cut of ONE
  base triangle; a river's or a lake's mesh is cut otherwise, a piece is a RANGE of base triangles, and a group then
  holds pieces of several base triangles each, one after another in its block: the place an offset by a piece's
  first base triangle would go wrong unseen. So the part now also has the test's mound (512 triangles of 1.5 m,
  24 m by 12 m) as a sheet of the same slot's water a metre over the sea, which the walk passes 2 m above. At the
  walk's first place, 60 m from it, the log has 1,800 pieces where the sea alone has 1,796: the sheet's 512 base
  triangles in 4 pieces of 128; nearer they are cut finer and fewer to a piece (by the cut's rule 8 at 10 m, 2
  right under the camera: reckoned, the log names no piece's base triangles). 2,083
  pieces at most in 107 groups at most, 74 groups put together, 19,158 pieces collapsed, 313 dead geometries at
  most: hand-over 2's three walks and THE OLD CODE'S all hash to `b4284e27b62a3550`; 0 of 212 frames differ;
  the self-check agreed at every frame. PASS. `neighbour` and `nocollapse` fail it as before (212 and 211 of 212
  frames), `material` by the self-check (16 times; `mutn-`, `mutc-`, `mutm-groups-5.log`). At 0.5 m a frame
  (`new3-groups-5-slow.log`, `base-groups-5-slow.log`): hand-over 2's three walks hash to `3a180bcde7afad73`,
  `0d0f19d085d2f507`, `fc5ddc342ef7080c`, and so do the old code's three (1,869 pieces at most in 78 groups at most,
  174 groups built in the 212 frames, 23 of them put together, 4,317 pieces collapsed, 159 dead geometries).
  (Twice in these runs a game of the owner's opened while a test ran: the test was ended at once and run again
  when the card was free, as the rule says; `chain.log` has the times.)
- MUTANTS, built again from hand-over 2's sources (`mutn-`, `mutc-`, `mutm-groups-3.log`): `neighbour` FAILS (212 of
  212 frames, and the self-check 238 times: "a group's kept geometry record is not what its member gives");
  `nocollapse` FAILS (211 of 212 frames); `material` FAILS NOW (the picture is the same, as it must be; the
  self-check says 12 times "the frame's material list does not hold the water's material for every piece of a
  group", and the part reads that).
- `TGPT_TEST_WATER10_ONLY`: "PASS water10 checks". The sea's part `foam`: "PASS sea checks".
- Source checks: 120 of 120 (`test_path_tracing_water_surface` holds the groups: the pattern a group's worth, a
  piece's geometry record, the collapse bit on both sides, the two places a piece leaves the map, the frame's count,
  the pools' free stretches, the test's switch never a setting).
- NOT RUN, and why: the whole `tgpt_gpu_test` and ctest (the coordinator's); `TGPT_TEST_MEMORY2_ONLY` with
  `TGPT_TEST_MEMORY2_WATER=1` (PT-87's lake, case 7, which no run takes by itself: it reads the water's line, which
  still begins and names its pages and blocks as before; its "a twelfth less than powers of two" was reckoned for a
  block a piece and now rests on some dozens of blocks: it may need its number looked at when someone runs it);
  the Direct3D debug layer (`d3d12SDKLayers.dll` of the staged runtime is not on this PC).

### What it costs, offline (the water fixture's cost run, `TGPT_TEST_WATER_COST`: a lake at 1280 x 720, 1,376 pieces, 1,250,816 triangles)

| | ungrouped (the test's switch) | in groups (hand-over 1) |
|---|---|---|
| groups, instances traced | 1,376, 1,378 | 39, 41 |
| the render thread's recording, a frame | 2.31 ms | 0.47 ms (dry, no water: 0.13) |
| GPU, a frame | 2.50 ms | 2.16 ms |
| the structures' blocks, the vertices' | 86.3 MB, 24.0 MB | 76.4 MB, 24.0 MB |

So the water's part of the record is 2.18 ms ungrouped (1.6 microseconds a piece; the game's forest: 3.33 ms for
1,793 pieces, 1.9 a piece) and 0.34 ms in groups. By that the forest's 3.33 ms (0.63 "records and plan" + 2.70
"pose") should be about 0.45: THE GAME'S NUMBER IS THE COORDINATOR'S, and it came out as 0.79 to 0.80 (0.65 + 0.15)
with hand-over 1. (`new-cost.log`, `new-costold.log`; other tests and compiles ran on the PC at times: read them
as a size, not to the hundredth. Hand-over 2, two runs each: recording 0.43 and 0.64 ms in groups, 2.85 and 2.70
ungrouped: the PC was busier, and what the one-pass count gains, a few hundredths, is below what this run can
tell: the game's "the water's records and plan" will.)

## Switches and log lines added

- Environment `TGPT_TEST_WATER_UNGROUPED` (any value; read every frame by `plan_water`): above. The log says
  "PT test, the water's pieces: UNGROUPED ..." when it changes.
- `record-check` / `TGPT_TEST_RECORD_CHECK` (reccpu's): now also `water_check` every frame (lines above).
- CHANGED "The water's surface: N pieces in G groups, T triangles, V vertices (... kept, X MB of pages: ...; H groups
  held, B built this frame (n of new pieces, m put together of f), X triangles built, C pieces collapsed this
  frame, D geometries of dropped pieces)". It starts as before ("The water's surface: N pieces") and keeps the
  pages' and blocks' figures where `tests/pathtracing_memory2_checks.inl` reads them. With the test's
  `TGPT_TEST_MEMORY2_OFF` set it is also written in every frame that built a group or collapsed a piece.
- NEW "PT water groups 10s: F frames; a frame traced P pieces in G groups (at most M): C refit or build calls a
  frame where each piece had one; built in these frames: B groups (n of new pieces, m put together of f), b a
  frame, t triangles a frame; x pieces made, y collapsed; now H groups held, K pieces kept". THIS IS THE LINE FOR
  A WALK: how many groups a frame builds.
- CHANGED the tracer's status line: " water: N pieces of its surface (...) in G groups (B built this frame, m of
  them put together of several, X triangles built, C pieces collapsed, D dropped pieces' geometries);".
- No setting, no FrameConstants row, no export. Records changed: the water's mesh records (one a group), geometry
  records (one a piece, where there were six a frame), instance records (one a group), the material list (256
  entries of the water's material where there was 1), the pose pass's records (the collapse bit).
- The TEST's own (`tests/water_sea_gpu.inl`): `TGPT_TEST_SEA_PART=groups` (the new part 9; it also runs when the sea
  fixture runs whole), `TGPT_TEST_SEA_WALK_PACE=<metres a frame>` (another pace than 3; the walks are then printed,
  not held to one another).
- NEW, only with a check: "PT record check: the water's groups agree with their units and blocks (...)" once, "PT
  record check: THE WATER'S GROUPS DIFFER from what would be made again: <what>" at every difference.

## Files changed (against `base20o`; `merge_copy.py waterblas --base=base20o --dry` lists the same)

| file | what |
|---|---|
| `native/pathtracing/src/scene.hpp` | `WaterGroup`, `WaterUnit` without blocks of its own, `WaterPool`'s free stretches, the constants, the groups' members of `GpuScene`, six numbers in `Stats` |
| `native/pathtracing/src/scene_water.cpp` | nearly all of it: `water_block` / `water_block_free`, `water_group_make` / `water_group_free` / `water_unit_gone`, `plan_water`'s groups, `water_check`, `pose_water` |
| `native/pathtracing/src/scene.cpp` | three places: the material list's 256 entries, the water's geometry records into the frame's list, the status when no water is traced |
| `native/pathtracing/src/tracer.cpp` | one place: the status line's water part (~4536) |
| `native/pathtracing/shaders/pt_water_mesh.hlsl` | the collapse branch (the ONE pass to compile; no `.hlsli` is touched) |
| `tests/water_sea_gpu.inl` | part 9 "groups", ONE block of its own before the fixture's last three lines (main's change of 09:45 is in the width part, lines 496 to 552 of the base: no line in common) |
| `tests/source_checks.py` | `test_path_tracing_water_surface`: two pinned lines moved with the code, the groups' pins added (lines 1945 to 2040; main's change since is at 2047 and 3696) |
| `tests/water_surface_gpu.inl` | one print's wording ("the rest are the groups the surface's pieces are traced in") |
| `tests/pathtracing_nosun_checks.inl` | the coordinator's one-line fix: byte for byte main's, nothing to merge |
| `docs/agents/waterblas.md` | this report (new) |

Not touched: every `.hlsli`, every pass that traces, `tgpt_api.h`, the plugin, CMake, BACKLOG, ROUNDS, AGENTS,
PROJECT-CONTEXT, PT-REQUESTS, CHANGELOG (the coordinator's: a text for them is below).

## For the merge

- Main's `tracer.cpp`, `pt_water_sun.hlsli` and `tests/water_sea_gpu.inl` changed at 09:45 (campfix): none of my
  hunks is near theirs (`tracer.cpp`: mine 4535 to 4537, theirs 3633 to 3636). Hand-over 2's DLL is my copy's
  sources (main's of 08:10 plus mine) with main's headers of 08:09: it is for the game's test of THIS change, the
  merged build is the coordinator's.
- After the merge only `pt_water_mesh` must compile (and whatever main's own changes ask for).
- The part's hashes move whenever a pass's picture of the sea moves (campfix's water rule will): they are compared
  between DLLs of one build, never pinned.
- For PT-REQUESTS (PT-99) and CHANGELOG, as a text to cut from: "The water's pieces are traced in groups: some
  dozens of pieces of one slot and cut level share one bottom level of a geometry a piece, one refit call and one
  instance (it was one of each a piece: 1,400 to 2,500 calls a frame). No pass that traces changed: a hit finds its
  piece as any submesh. A piece its group holds and a frame does not want is collapsed to a point by the pose pass,
  so no group is built anew for a piece that left; a walk's small groups are put together one a frame. The pools
  keep their free stretches. In the game (run 20261012-094311, hand-over 1): the water's pose 2.70 to 0.15 ms a
  frame in the forest, 3.72 to 0.20 at the beach, 2.05 to 0.15 in the town; the scene's record 5.69 to 3.20,
  6.50 to 3.9, 6.65 to 5.5. Offline: 212 frames of a walk bit for bit the old code's picture."

## Not done, and what I would look at next

- A WALK IN THE GAME and the self-check there (hand-over 2 with `record-check`, `forest@camp-walks`): the fixture's
  walk says the mechanism holds; the game's water meshes (rivers and lakes of many small base triangles, several
  slots) have other shapes than a sea of two triangles. What to read: "PT water groups 10s" (builds a frame; I
  expect under 1 group and some thousand triangles more than the new pieces' own at walking pace), the pages in
  "The water's surface" (I expect them to stay near the blocks in use), no "PT record check: THE WATER'S".
- THE PLAN'S OWN TIME ("the water's records and plan", 0.6 to 0.9 ms): most of it is as before (the walk over the
  surfaces' trees and a hash lookup a piece every frame: reccpu's "Not done"); what the groups added in hand-over 1
  (0.07 to 0.15 ms) should be mostly gone with the one-pass count. A plan kept between frames while the camera has
  not moved a cell would take the rest out; it is a kept result with its own proof to make (which inputs: the
  camera, the scene's list, the slots' cells and up directions), not part of this.
- The water's scratch buffer is made for the first frame's builds side by side and never made smaller (as before;
  the whole tracer's scratch was 67 MB in the forest's run). Building in waves within a window, as `scratch_place`
  does for the other stages, would hold less: a small memory matter, not looked at further.
- A group is rebuilt only by the budget; its structure is otherwise refitted for ever, as a piece's was. A refit
  keeps the build's tree: the waves move the vertices by centimetres to decimetres, so I expect no decay, and saw
  none in the fixture's GPU times (in groups 9.4 to 10.6 ms a frame against 9.3 to 9.5 ungrouped, on a shared card).
- `pathtracing_memory2_checks.inl` case 7 (opt-in) and the debug layer: see "NOT RUN" under the checks.
