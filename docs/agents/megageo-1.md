# megageo-1: RTX Mega Geometry, "the Alan Wake 2 BVH" (PT-109)

Worker `megageo1` (base `base76`). Brief: `docs/agents/briefs/brief-megageo-1.md`, continued by a second worker
under `brief-megageo-2.md` (2026-10-09 23:45 to 2026-10-10 00:40). Never the game, a deploy, a commit, the main
tree. The plain path is whole and is the default.

## READ THIS FIRST: where it stands (2026-10-10 00:40)

- **This PC has both parts.** RTX 5080, driver 616.92: cluster operations YES, the partitioned top level YES
  (asked of the driver on the tracer's own device, not read from a table).
- **Hand-over 1 is done**: the driver's answers, the log line "PT structures 10s" (in every build), the recipe
  `structures` (one step frozen, one running), the plan with numbers. All below.
- **The first piece is built and checked offline: the partitioned top level.** Behind CMake's
  `TGPT_MEGA_GEOMETRY` (OFF by default) and the hidden `Advanced / Mega Geometry trial` (0 by default; 1, 2, 3 are
  cells of 64, 128, 256 m). Its own check: every picture is the plain top level's to the last pixel, in 20
  pictures a run, with all three sizes. The recipe `mega` is the game's run.
- **The honest number: it saves 0.03 ms a frame offline** (the top level 0.124 ms built anew over 8,573 instances,
  0.095 ms partitioned), and tracing is as fast through it as through the plain one (2.29 against 2.30 ms a frame).
  It loses nothing and wins almost nothing. Its worth is that the whole chain is proven on this PC.
- **Where the gain is: the plants as clusters** (next piece): about 220 MB and 2 to 3.5 ms in the town by NVIDIA's
  own sample's ratios, and it needs NO new shader passes (the plan says how). Not started.
- Compiles with the switch ON and OFF (0 warnings in my files), the plugin 0 warnings 0 errors, source checks
  109 OK. No game was started, nothing deployed, nothing committed, nothing written outside this copy and
  `%TEMP%\tgb\megageo1*`.

## 1. HAND-OVER 1

### 1.1 What this PC reports (measured 2026-10-09 23:54)

How it was asked: the copy's own build, the tracer's GPU test in its `TGPT_TEST_STARTUP_ONLY` mode (the bridge
makes the shared Direct3D 12 device with the Agility SDK, as in the game; nothing is traced), read from the
tracer's own log. Card: NVIDIA GeForce RTX 5080. The three lines, word for word:

```
Mega Geometry: the driver 616.92 (r616_69; NVAPI NVidia Complete Version 1.10, header 61683) reports cluster
  operations: YES (caps 0x1), the partitioned top level: YES (caps 0x1). The header's limits: a cluster at most 256
  triangles, 256 vertices and 256 submeshes, a submesh's number at most 16777215; a partitioned top level at most
  16777216 instances and as many partitions
Mega Geometry: the driver's own sizes for a town's plants (11303 plants in 636 groups, 6672229 triangles, 9513841
  vertices; today 428 MB of plain bottom levels) cut into 78190 clusters of at most 128 triangles, upper bounds:
  clusters built from triangles 429.5 MB (scratch 10.1 MB); templates for a twentieth of them 9.5 MB (scratch 0.5
  MB); clusters stamped from templates 429.5 MB (scratch 0.0 MB); the 636 bottom levels built from those clusters
  4.0 MB (scratch 6.4 MB)
Mega Geometry: the driver's own sizes for a partitioned top level (upper bounds; a town's plain one holds 16234
  instances): 32768 instances in 256 partitions (at most 4096 in one, 4096 in the global one): 8.2 MB, scratch 4.2
  MB; 32768 instances in 1024 partitions (at most 2048 in one, 4096 in the global one): 9.2 MB, scratch 6.0 MB;
  131072 instances in 4096 partitions (at most 2048 in one, 8192 in the global one): 35.0 MB, scratch 20.9 MB; a
  plain top level of 32768 instances: 9.4 MB, scratch 3.4 MB
```

In plain words:
- Both parts are there on this PC.
- The partitioned top level costs about what the plain one does: 8.2 to 9.2 MB for 32,768 instances against 9.4.
- **The driver's size for the plants as clusters is an UPPER BOUND, and it is no smaller than today's**: 429.5 MB
  against 428. The driver cannot know before it builds how small a cluster gets, so it promises room for the
  worst case. This number does NOT say what clusters save. NVIDIA's own sample measured 271 MB where plain
  structures took 564. Whoever builds the cluster part must measure the real size first.
- The bottom levels built FROM clusters are tiny: 4.0 MB for all 636 groups. That is why a rebuild each frame
  gets cheap there: it sorts 78,190 boxes, not 6.7 million triangles.
- A side note: that first test ended itself at its 8 GB memory ceiling 2 minutes in (8,340 MB), while the driver
  made the two reordering passes, as the brief foresaw. The three lines were written before that. I did not raise
  the ceiling: every later run of mine used `TGPT_TEST_NO_AGILITY=1`.

### 1.2 What today's structure work costs: the line is built; a real scene's numbers are the game's

"PT structures 10s: ..." is in every build (no switch: it counts and puts GPU timestamps, nothing else). Per kind,
the GPU time a frame and what was built or refitted: skinning, new meshes, compaction, decals, cloth, plants,
water, characters, micromaps, top level, "the rest of the scene stage"; then their sum against the scene stage's
own time, and the MB held. Seen working offline (section 2.5): its kinds add up to the stage (0.147 of 0.148 ms).

What is known before the game's run:

| | tavern | town (`metal`) | beach |
|---|---|---|---|
| the whole "scene" stage, world frozen (the harness's logs, `perf-1.md`) | 0.97 to 1.0 ms | 1.25 to 1.48 ms | 1.6 to 1.7 ms |
| of it the plants, world frozen | 0 | 0 (636 groups held still) | 0 |
| the plants in play (`perf-1.md`'s reckoning, NOT MEASURED) | 0 | 3 to 5 ms | under 1 ms |
| the top level alone (my reckoning from the offline run: 0.124 ms over 8,573 instances) | about 0.1 ms | about 0.2 ms | about 0.15 ms |

So ALL structure work today is about 1.5 ms a frame frozen and, by reckoning, 5 to 6.5 ms in play in the town, of
a frame of 80 to 180 ms. That is the most any Mega Geometry part can give back in time.

### 1.3 The run that reads both lines (the coordinator's: one scene, two steps, about 2 minutes)

The build: merge this copy; configure the main tree with `-DTGPT_MEGA_GEOMETRY=ON` added (it needs `NVAPI-R615\`
beside `CMakeLists.txt`: copy the folder from this copy, or clone `github.com/NVIDIA/nvapi` at
`70d337db9186e968eab622f7e786de7e437faf3d`); no shader changed, so no shader compiles; build, deploy.

`.\tools\harness\Run-PtHarness.ps1 -Scenario metal -Recipe structures`

- `2-frozen-none`: 25 s traced, no denoiser, the world frozen. `3-running-none`: the same with the world RUNNING
  (`"live": true`: the harness already had that; nothing had to be built in the plugin's runner).
- Read in the tracer's native log: the three "Mega Geometry: ..." lines at start-up, and of each step the SECOND
  "PT structures 10s" line (the first holds the frames of the change).
- What I expect: frozen, `plants 0.000 ms (0.00 built, 0.00 refitted)`, water about 0.7 ms, `top level` about
  0.2 ms "built anew over 16234 instances", the sum close to the scene stage's 1.3 to 1.5 ms. Running, `plants`
  2 to 5 ms with 636 refitted and 6.7 M triangles a frame, the scene stage up by that. If `plants` stays near 0
  while the world runs, the wind's clock did not move in the harness's running world: then the number has to
  come from the owner's own walk (the same lines are written while playing).

### 1.4 THE PLAN, with numbers

Where the numbers are from: NVIDIA's sample `vk_animated_clusters` (its README; NVIDIA measured on an RTX 6000 Ada,
8.43 million animated triangles): plain structures 564 MB, clusters from templates 271 MB (48 %); refitting the
plain ones 2.78 ms a frame, stamping clusters from templates and building the bottom levels from them 0.80 ms
(29 %); rays through them 5 % slower (1.45 to 1.52 ms). Our counts: `memory-1.md`, `perf-1.md`, `speed.md`. Remedy's
patch (Alan Wake 2 1.2.8, 2025-01-30): "Improved Ray Tracing CPU and GPU load, VRAM usage and image quality thanks
to RTX Mega Geometry"; NVIDIA's blog of 2026-03-10: "a 5-20% boost in FPS and 300 MB VRAM reduction" there, and
"millions of detailed, uniquely animated foliage elements" as what it is for.

| Part | What it replaces | town | forest | by the water | Shader change |
|---|---|---|---|---|---|
| **1. The partitioned top level (BUILT, on trial)** | the top level built anew over every instance each frame | under 0.1 ms (measured offline: 0.03 of 0.12 ms at 8,573 instances); 0 MB | less | less | none |
| **2. The plants as clusters stamped from templates (next)** | 636 groups refitted each frame, 428 MB of structures | about 220 MB; 2 to 3.5 ms in play | 287 groups (their triangles are in no report; by the town's 590 a plant, 3.8 M): about 125 MB, 1 to 2 ms | 1.1 M triangles: about 35 MB, 0.4 ms | NONE (see below) |
| 3. The water's pieces the same way (one template a level of the shared pattern) | 1,379 to 2,459 pieces refitted each frame | about 40 MB, 0.4 ms | | about 70 MB, 0.8 ms | none |
| 4. Characters the same way | 59 refitted, rebuilt in turn | under 25 MB, 0.2 ms | | | none |
| Still meshes as clusters | built once, compacted to 38 % | NOT WORTH IT: compacted plain structures are about 25 bytes a triangle, clusters about 32 | | | |
| **All four** | | **about 280 MB, 3 to 4.5 ms** | **about 150 MB, 1.5 to 2.5 ms** | **about 105 MB, 1.5 ms** | |

In plain words:
- **It is a small gain here, and I would rather say so now.** A frame is 80 to 180 ms of tracing and shading.
  Everything the structures cost is 1.5 ms frozen and maybe 6 ms in play. Mega Geometry can give back about half
  of that, and 100 to 280 MB of the 3 to 5 GB the traced world holds. Alan Wake 2 got 5 to 20 % because building
  structures was a large share of its frame. Ours is not.
- **Part 1 went first because it is the small, safe one**: C++ only, no pass compiled, the same instances, checked
  offline against the plain one. It proves the whole chain on this PC: NVAPI on the bridge's device, inside the
  tracer's own command list, a ray query tracing a structure NVIDIA's call built, the way back to the plain path.
  Its own gain is the smallest of the four, and now measured: 0.03 ms.
- **Part 2 is where the memory is, and it needs NO new shader passes.** The brief feared that `fetch_hit` must
  learn NVIDIA's cluster number (`NvRtGetCommittedClusterID`, a fake UAV slot in every pass, a second set of all
  the big passes to compile). It does not have to. A cluster may carry a submesh number of its own (up to
  16,777,215: `nvapi.h` `baseGeometryIndexAndFlags`); a hit in a cluster reports that number through the plain
  `GeometryIndex()`, and `PrimitiveIndex()` is the triangle INSIDE the cluster (NVIDIA's sample README; Microsoft's
  DXR spec part 2 says the same). That is exactly how `fetch_hit` finds a triangle in a submesh today: the
  submesh's first index plus three times the primitive. So:
  - every cluster gets its own entry in the group's submesh records (`plantGeometries_`: first index, index count,
    the member's layers and flags) and in its material list: about 78,000 more records in the town, 1.6 MB;
  - clusters are runs of consecutive triangles of the mesh's own index order whose corners lie within 256
    vertices of each other: then a cluster's vertex buffer is a window into the group's posed positions, with no
    second copy of any vertex and no change to the pose pass;
  - one template a cluster of each MESH, built once; each frame every plant's clusters are stamped from its posed
    positions and the 636 bottom levels built from them: two calls a frame, the arguments written once a group.
- What I do NOT know, and the next worker must measure before promising the 220 MB: the real size of a stamped
  cluster on this driver (its promise beforehand is the worst case, 429.5 MB: no saving), and whether rays through
  cluster-built plants are slower here (5 % in NVIDIA's sample). A forest's frame is mostly rays through plants:
  5 % of that could cost more than the 2 to 3.5 ms the refit gives back. The memory is the surer gain.
- Later, for every card: Microsoft is bringing both parts into DirectX itself (DXR functional spec part 2, v0.38 of
  2026-09-08: raytracing tier 2.0, `ClustersAndPTLASSupported`, the same operations under Direct3D's own names).
  When an Agility SDK and AMD's and Intel's drivers report it, `MegaTopLevel` gets a second door beside NVAPI's;
  the choice stays what the runtime reports. Its partitioned top level HAS the "no micromaps" instance flag
  NVAPI's lacks.

## 2. The first piece: the partitioned top level

### 2.1 What it is, and its switches

With the hidden `Advanced / Mega Geometry trial` at 1, 2 or 3, on a native side built with
`-DTGPT_MEGA_GEOMETRY=ON`, and where the driver reports the feature, the frame's top-level structure is a
partitioned one: kept from frame to frame, and each frame the driver is told only which instances changed. With 0
(the default), without the compile switch, on AMD's and Intel's cards, on an older NVIDIA driver, or after any
call of it failed: today's code, the plain top level built anew. The tracer's log says which, in a line each.

- 1, 2, 3 are the size of its cells for what stands still: 64, 128 or 256 m (1,024, 256 or 64 partitions over
  2 km around the scene's origin). NVIDIA advises 100 to 1,000 instances a partition. The game's world is not my
  test's field, so the game says which size suits it: one run, three steps.
- **Which field** (the brief asked): `TGPT_Frame.flags`, bits 2 to 5 (`TGPT_FRAME_MEGA_SHIFT` 2,
  `TGPT_FRAME_MEGA_MASK` 15 in `tgpt_api.h`). No size or ABI change; no option bit. Why not one of the spare floats
  the other trials ride in: every one of them is read with a clamp (`cameraPosition`'s w to 127,
  `prevCameraPosition`'s w to 524287, `fogMip`'s w as "above 0.5"), so a number added on top would switch those
  trials on. The frame's flags had 30 free bits and are only ever tested bit by bit.

### 2.2 How the picture stays the same

A slot of the partitioned structure is the instance of the same number in the frame's list, written with the
same transform, InstanceID, mask, flags and bottom level (`mega_geometry.cpp`, `MegaTopLevel::build`). A ray query
takes it as a plain top level (NVIDIA's and Microsoft's documents both say so) and reports the same InstanceID,
so no pass knows.
- One thing a partitioned top level CANNOT say through NVAPI: an instance's "no micromaps" flag (`DISABLE_OMMS`).
  The tracer sets it on an instance that goes without its mesh's opacity micromaps. While the partitioned one
  serves, such an instance gets the mesh's plain bottom level instead (it exists beside the micromap one and
  holds the same triangles), with the same opaque or tested flag: three lines in `scene.cpp`, which read exactly
  as before when the trial is off. A frame with any other flag it cannot say goes to the plain build, and the log
  says so once.
- What is moved, posed or refitted each frame (the things the game moves, the plants' groups, the water's pieces,
  characters, swaying cloth) lies in the driver's "global" partition and is told to the driver EVERY frame, so
  that the top level takes each one's new bounds. What stands still is told only when its slot's instance changed,
  or when a bottom level was built anew at the address its slot names (`megaFresh_`: a new structure may stand
  where a retired one stood).
- No limit of ours: any partition may hold every instance; when the scene outgrows the structure's room it is
  made anew half as large again and written whole once.
- Any failed call: one log line ("Mega Geometry: the partitioned top level failed (...); the plain top level
  serves from now on") and the plain build from that frame on.

### 2.3 Files (against `base76`)

New: `native/pathtracing/src/mega_geometry.hpp`, `mega_geometry.cpp`, `tests/pathtracing_mega_checks.inl`,
this report. Changed: `CMakeLists.txt` (the switch, NVAPI's notice with a switched-on build), `.gitignore`
(`/NVAPI-*/`), `native/pathtracing/include/tgpt_api.h` (two defines), `native/pathtracing/src/scene.hpp`,
`scene.cpp`, `scene_water.cpp`, `tracer.cpp`, `src/TGPathTracing/Settings.cs`, `HdrpPasses.cs`, `NativeTracer.cs`,
`tests/pathtracing_gpu_test.cpp` (the trial's variable, one line in `run`, the mode), `tests/source_checks.py`
(one new check, one pinned count), `tests/ingame/scenarios.json` (recipes `structures`, `mega`),
`docs/PT-HARNESS.md` (those two recipes). Nothing of `native/bridge`, no shader, no `.md` of the coordinator's.

### 2.4 Its own offline check (small; never in the whole test)

`TGPT_TEST_MEGA_ONLY=1` (`tests/pathtracing_mega_checks.inl`), run with `TGPT_TEST_NO_AGILITY=1` and the 8 GB
ceiling: `%TEMP%\tgb\megageo1\tgpt_gpu_test.exe %TEMP%\tgb\megageo1-out <images>`; one and a half minutes once the
driver has the passes (the first run compiled the Shader Model 6.6 ones for 5 minutes). The same frames traced six
times: plain, partitioned (64 m), plain again, a control, partitioned with 128 m and with 256 m cells. The
tracer's own camera rays, no denoiser, 20 pictures a run (the lit picture and the first hits' colour after each
change). Run four times as it grew (00:13, 00:22, 00:24, 00:29); the last, with the card to itself:

| The scene changed like this | pixels that differ from plain: 64 m, 128 m, 256 m cells | plain against plain |
|---|---|---|
| a field of 4,071 boxes over 450 m, nothing moving (from above, and the room) | 0, 0, 0 | 0 |
| a thing the game moves appears, then moves 3 m | 0, 0, 0 | 0 |
| a character's vertices move 5.5 m, its instance staying put (its bottom level refitted in place) | 0, 0, 0 | 0 |
| 300 boxes leave from the middle of the list (every later instance gets another number) | 0, 0, 0 | 0 |
| 4,489 boxes arrive (8,573 instances: over the structure's room of 6,144, so it is made anew) | 0, 0, 0 | 0 |
| the camera goes 300 m (the scene's origin steps 256 m) | 0, 0, 0 | 0 |
| back to the first scene, then three frames with nothing changed | 0, 0, 0 | 0 |
| the control: the moved thing 0.5 m off | 74,989 (it must differ) | |

Every picture is the plain one's to the last pixel, of 921,600. The tracer's log said the partitioned top level
served ("the top level is partitioned (on trial): 4083 instances, room for 6144 in 1024 partitions of 64 m and a
global one for what moves: 3.1 MB, scratch 3.8 MB") and no call failed.

Not covered offline: opacity micromaps under it (no Agility SDK in this mode, so no micromaps), plants' groups and
the water's pieces (they are told to the driver the same way the character is), an address reused by another
bottom level (reasoned, not provoked). No mutation check was run (the rule: not before the game says it is right).
The ones to run then: leave the per-frame telling of what moves out (the character's step must fail), and write
a slot with another slot's InstanceID (the first hits' colour must fail).

### 2.5 What it costs and saves, offline (the card to itself, 8,573 instances, one thing moving, one character)

The four in turn, sixteen rounds of 200 frames, a frame's whole GPU time:

| top level | least | tenth quantile | median | frames a second |
|---|---|---|---|---|
| plain | 2.29 ms | 2.31 | 2.32 | 343.4 |
| partitioned, 64 m cells | 2.30 ms | 2.31 | 2.32 | 343.1 |
| partitioned, 128 m cells | 2.29 ms | 2.30 | 2.31 | 344.3 |
| partitioned, 256 m cells | 2.26 ms | 2.27 | 2.28 | 346.9 |

And the first "PT structures" lines ever printed, 10 s of each:

```
PT structures 10s: skinning 0.005 ms; new meshes 0.000 ms (0.00 built, 0.00 refitted, 0 triangles a frame); ...;
  characters 0.010 ms (0.25 built, 0.75 refitted, 12 triangles a frame); micromaps 0.000 ms (...);
  top level 0.124 ms (built anew over 8573 instances, 2.47 MB); the rest of the scene stage 0.007 ms;
  sum 0.147 ms of the scene stage's 0.148 (3392 frames timed, 3392 recorded); held, MB: acceleration structures 19.8, ...
PT structures 10s: ...; top level 0.095 ms (partitioned in 3437 of 3437 frames: 2.0 of 8573 instances told to the
  driver a frame, 5.90 MB); the rest of the scene stage 0.007 ms; sum 0.118 ms of the scene stage's 0.120 (3437
  frames timed, 3437 recorded); held, MB: acceleration structures 25.7, ...
```

- **The top level: 0.124 ms built anew, 0.095 ms partitioned. 0.03 ms saved.** About 0.09 ms of it is a fixed
  cost either way, even with only 2 instances told.
- **Tracing is as fast through it**: the whole frame is the same to 0.01 ms with 64 m cells, and 0.03 ms less
  with 256 m cells. (An earlier run seemed to show it 2 ms slower: that was Starfield's load moving between two
  windows. Run in turn, the two were equal there too: least 2.31 and 2.31 ms.)
- It holds 3.4 MB more here (5.90 MB with room for 13,312 instances, against 2.47).
- The scene is boxes: thousands of tiny things, the case where the top level matters MOST. So this is close to
  the best it can do.

### 2.6 The run that shows it in the game (one scene, about 4 minutes)

`.\tools\harness\Run-PtHarness.ps1 -Scenario metal -Recipe mega` (the build of 1.3)

- `2-plain-frozen` and `3-partitioned-frozen`: 16 undenoised frames each, the world frozen. THE SAME PICTURE: their
  means must differ by no more than two plain runs do. If a crop differs (leaves with their holes filled or cut
  away, a character clipped), the trial is wrong: tell me where.
- `4-partitioned-64m-running`, `5-partitioned-128m-running`, `6-partitioned-256m-running`, `7-plain-running`: the
  world running, 25 s each. Read each step's second "PT structures 10s" line (`top level ... ms (partitioned in N
  of N frames: X of 16234 instances told to the driver a frame, ... MB)` against `top level ... ms (built anew
  over 16234 instances, ... MB)`) and its stages' line.
- What I expect: the top level from about 0.2 ms to 0.1 to 0.15 ms; about 2,100 instances told a frame (the
  water's pieces, the plants' groups, the characters) of 16,234; the tracing stages (prepare, bounced light,
  direct light, reflections) the same within what two plain steps differ by. The log holds "Mega Geometry: the
  top level is partitioned (on trial): ..." once a size and no "failed" line.
- What it decides: if a size makes the tracing stages slower by more than the top level saves, that size loses.
  If all three do, the piece comes out the same day (the rule in AGENTS.md); part 2 does not need it. If none
  does, it is harmless and nearly worthless alone: keep it only while part 2 is wanted, as its proven base.
- **The game's first answer, not checked offline: opacity micromaps under a partitioned top level.** In the game
  there are micromaps on 236 meshes in the town. Leaves and grass are where to look in the frozen pair.

### 2.7 What could go wrong

- Micromaps (above). If leaves change, the fallback is one line: while micromaps are on, let the plain top level
  serve (say so in the log). I did not build that, because nothing says it is needed.
- While the world streams in (meshes arriving, levels of detail changing), instances change their numbers and
  many slots are written again each frame. That is correct but may cost more than the plain build in those frames.
  The line's "told to the driver a frame" shows it; the first run after a load is where to look.
- Another mod wrapping Direct3D 12's command lists (ReShade does, and is off for test runs) could make NVAPI
  refuse the tracer's list: then the one log line, and the plain path.
- A build with the switch ON installs one more notice beside the DLL (`TGPathTracing/Notices/LICENSE-NVAPI.txt`,
  MIT asks for it): a file more in the zip. Nobody ships such a build today; it is the owner's call if ever.

## 3. What is next

1. The coordinator's two runs (1.3 and 2.6): the structures' real numbers in the town, frozen and running, and
   the game's verdict on the partitioned top level.
2. Then part 2, the plants as clusters, in this order: (a) a small offline program that builds templates for ONE
   plant mesh, stamps a few hundred posed copies and reads back the real sizes and times on this driver (half a
   day; it says whether the 220 MB is real before anything is built in the tracer); (b) the clusters cut at mesh
   conversion; (c) the groups' bottom levels built from stamped clusters behind the same trial setting (a value
   of its own), with each cluster as a submesh record; (d) its check: the plants' picture the plain one's.
3. Not before the game asks: the water's pieces and the characters the same way; Direct3D's own version of both
   parts when a runtime reports it.

## 4. Sources and their licenses (for `docs/SOURCES.md`, which is the coordinator's)

| Source | Version | License | What was taken |
|---|---|---|---|
| NVAPI, `github.com/NVIDIA/nvapi` | release R615, commit `70d337db9186e968eab622f7e786de7e437faf3d` of 2026-09-18, `nvapi.h` of Sep 2 2026, `NVAPI_SDK_VERSION 61683`. SHA-256: `nvapi64.lib` 901E479E548D41C30688C7481F0C6353C0BE7FDEE978796D27D87C32E19CE172, `nvapi.h` 76BBB71107C1388134D41712BED56BB6D80EC426F9BB2C168E2D6941B90F16CA | MIT (`License.txt` for the `.lib` files; every header `SPDX-License-Identifier: MIT`) | Downloaded into `NVAPI-R615\` in the copy's root (a shallow clone; nothing run from it; never committed). The tracer includes `nvapi.h` and links `amd64/nvapi64.lib` ONLY with the switch ON. No line of it is copied into the repo |
| RTX Mega Geometry SDK, `github.com/NVIDIA-RTX/RTXMG` | its README as of 2026-10-09 | NVIDIA RTX SDKs License (object code only) | README and license read. NOTHING downloaded, nothing copied |
| `nvpro-samples/vk_animated_clusters` | README as of 2026-10-09 | Apache-2.0 | README read: the measured numbers of 1.4, how a hit names its triangle. Nothing copied |
| `nvpro-samples/vk_partitioned_tlas` | README as of 2026-10-09 | not shown on the page read (nvpro samples are Apache-2.0 as a rule: not verified) | README read: partitions as a grid, the global partition, "TLAS and PTLAS are referenced identically". Nothing copied |
| Microsoft, DXR Functional Spec part 2, `microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html` | v0.38 of 2026-09-08, "early preview" | the DirectX-Specs repository's (not looked up) | Read: what an instance update does ("a refit will take place to update to the new bounding box"), the instance flags, `GeometryIndex()` in clusters. Nothing copied |
| NVIDIA developer blog, "RTX Mega Geometry now available with new Vulkan samples" (2025-02-06) and "NVIDIA RTX innovations are powering the next era of game development" (2026-03-10) | | | Read: the Alan Wake 2 numbers quoted in 1.4 |
| Remedy, Alan Wake 2 update notes, `alanwake.com/story/alan-wake-2-update-notes/` | 1.2.8, 2025-01-30 | | Read: the one sentence quoted in 1.4 |

## 5. For the coordinator

- Merging: `scene.cpp` and `tracer.cpp` are also the memory and the speed workers'. My edits there are small
  and named `PT-109` or `mega`; the "PT structures" stamps sit beside calls, not inside them.
- `tests/source_checks.py`: one new check (`test_path_tracing_mega_geometry_on_trial_beside_the_plain_path`), and
  `test_cmake_inputs_exist_and_shader_test_is_windows_only` now counts 4 notices and 10 "COMPONENT TGNative" (the
  fourth notice is NVAPI's, inside `if(TGPT_MEGA_GEOMETRY)`).
- Not mine, so not touched: `README.md` (its SDK list needs NVAPI as an optional download; its build section the
  switch), `CHANGELOG.md`, `docs/SOURCES.md`, `BACKLOG.md`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`.
- Nothing was needed of the bridge.
- Learned, for PROJECT-CONTEXT: `nvapi.h` needs `<dxgi.h>` before it once `d3d12.h` is in; `nvapi64.lib` links
  into a DLL with the static C runtime; NVAPI's partitioned top level has no "no micromaps" instance flag
  (Direct3D's coming one has); every spare float of `TGPT_Frame` is read with a clamp, so a new trial cannot
  ride on top of one; the frame's `flags` have room. An offline timing with another game on the card can be
  trusted only run in turn and read at its least and tenth quantile.
- My scratch: build folders `%TEMP%\tgb\megageo1` (switch ON) and `megageo1-off` (OFF), the test's stage
  `megageo1-out`, its shader cache `megageo1-cache` (a copy of the game's, read only there); scripts in the
  session's scratchpad, `megageo-scripts\`.

## 6. Workarounds met

None removed, none added. One limit of NVAPI's met and worked around inside the trial only (the missing "no
micromaps" instance flag: 2.2); the plain path's lines are untouched by it when the trial is off.

## State when stopped (the first worker's record)

(As the first worker left it on 2026-10-09 at 20:33, kept unchanged as the record. Everything it lists as open
is answered above.)


**Nothing is compiled. Nothing was run. Hand-over 1 is NOT done.** What this PC's driver reports is NOT known yet,
and today's structure cost is NOT measured yet: the code that would say both is written and has never met a
compiler. No download, build or test of mine is running.

### Sources found, with their licenses

| Source | Version | License | State |
|---|---|---|---|
| NVAPI, `github.com/NVIDIA/nvapi` | release R615 (its `docs` folder), commit `70d337db9186e968eab622f7e786de7e437faf3d` of 2026-09-18, `nvapi.h` dated Sep 2 2026, `NVAPI_SDK_VERSION 61683` | MIT (`License.txt` covers the `.lib` files; every header carries `SPDX-License-Identifier: MIT`) | DOWNLOADED into `NVAPI-R615\` in the copy's root (a shallow git clone, nothing run from it). SHA-256: `nvapi64.lib` 901E479E548D41C30688C7481F0C6353C0BE7FDEE978796D27D87C32E19CE172, `nvapi.h` 76BBB71107C1388134D41712BED56BB6D80EC426F9BB2C168E2D6941B90F16CA |
| RTX Mega Geometry SDK, `github.com/NVIDIA-RTX/RTXMG` | not noted | `LICENSE.txt` NOT READ: copy nothing from it until it is | only its README read, through a web fetch. It says: needs "GeForce Game Ready Driver 570 or later", an RTX card, "Cluster BVH build using NVAPI for DX12" |
| nvpro samples `vk_animated_clusters`, `vk_lod_clusters`, `vk_partitioned_tlas` | not noted | NOT READ | found by search only |
| NVIDIA's blog "RTX Mega Geometry now available with new Vulkan samples" (developer.nvidia.com) | | | found by search only, not read |
| NVIDIA's and Remedy's numbers for the Alan Wake 2 patch | | | NOT looked up |

`.gitignore` needs the line `/NVAPI-*/` (not added). `docs/SOURCES.md` and README's SDK list are the coordinator's.

### What `nvapi.h` itself says (read lines 19140 to 19235 and 20846 to 21410; this is checked, not memory)

- The question: `NvAPI_D3D12_GetRaytracingCaps(device, type, &caps, size)` with
  `NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS` (3) and `..._PARTITIONED_TLAS` (4); each answers `NONE` or
  `STANDARD`. The cluster and partitioned calls are "since Release: 560".
- Clusters: one call does everything, `NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation`, with a size
  question before it (`NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo`). Its kinds: build
  CLAS from triangles, build cluster templates from triangles, stamp CLAS from templates with new vertices, build
  a bottom level from CLAS addresses, move or copy. Limits in the header: a cluster holds at most 256 triangles,
  256 vertices, 256 different submesh numbers; a submesh number at most 16,777,215. Opacity micromaps are allowed
  in clusters (a flag on every call that touches them). Arguments are read by the GPU from buffers.
- The partitioned top level: `NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect` and its size question. At most
  16,777,216 instances and as many partitions. Three operations, read from GPU memory: write an instance, update
  an instance (its structure's address only), write a partition (a translation for all its instances). There is
  a "global" partition for things that fit no cell. An instance written this way carries the SAME fields as a
  plain one: transform, InstanceID, mask, the cull, opaque and non-opaque flags. So `fetch_hit`'s instance
  number would mean what it means today.
- NOT CHECKED YET (do not rely on it): whether an inline ray query takes a partitioned top level as it takes a
  plain one, with no shader change; and how a hit inside a cluster-built bottom level names its triangle
  (`nvHLSLExtns.h` was not opened: the cluster's number and the triangle's number inside the cluster are likely
  read through NVIDIA's shader intrinsics, and then `fetch_hit` must learn that). This decides which part goes
  first.

### Found by reading the code: where today's structure work is counted (the brief's first question)

ALL of it is inside the "scene" stage. `tracer.cpp` puts the frame's first timestamp (`mark(0)`), then the
characters' skinning, `GpuScene::record`, the micromaps (`record_omm`) and the emitters, then `mark(1)`. Inside
`record`, in this order on the list: compaction copies, new meshes' uploads and builds, decals (their small top
level), cloth's pose and refit, emitters, the plants' pose and refit, the water's pose and refit, the characters'
refit or rebuild, the top level built anew over every instance. (The caustics' own two structures are built in
the "caustics" stage.) So `perf-1.md` section 4.1 is right.
- BUT the harness's "scene" number (1.0 to 1.6 ms) holds NONE of the plants' cost: a group is posed and refitted
  only in a frame in which the wind's clock or the camera moved (`pose_plants`, the key `posedFor`), and the
  harness freezes the world. In play every group in use is posed and refitted every frame, however far. A
  harness step with the world NOT frozen (or the camera moving) is needed to see it.
- How each kind is built today: still meshes `PREFER_FAST_TRACE | ALLOW_COMPACTION`, built once, compacted;
  plants `PREFER_FAST_TRACE | ALLOW_UPDATE`, one structure a 16 m group, refitted, never rebuilt, never
  compacted; water's pieces the same flags, refitted every frame, frozen or not; characters
  `PREFER_FAST_BUILD | ALLOW_UPDATE`, refitted, rebuilt in turn every few frames; the top level
  `PREFER_FAST_TRACE`, built anew every frame.

### What is written (C++ only, NEVER COMPILED: expect errors)

1. **The line "PT structures 10s: ..."** (for every build, no switch: it only counts and stamps).
   - `native/pathtracing/src/scene.hpp`: `#include <functional>`; in `GpuScene`, before `record`: `StructureKind`
     (other, skinning, new meshes, compaction, decals, cloth, plants, water, characters, micromaps, top level),
     `StructureWork`, `structureStamp`, `structureWork[]`, `structure_stamp()`.
   - `native/pathtracing/src/scene.cpp`: `structure_stamp(...)` at ten places in `record` (after
     `compact_meshes`, after the new meshes' block, after `upload_decals`, before and after `plan_water`, before
     and after `sway`, after `build_emitters`, before and after `pose_water`, after the characters' block, before
     and after the top level's build); counts in `compact_meshes`, the new meshes' loop, `sway`, `pose_plants`,
     the characters' loop, the top level, `record_omm`'s `build`, `upload_decals`.
   - `native/pathtracing/src/scene_water.cpp`: the count in `pose_water`.
   - `native/pathtracing/src/tracer.cpp`: `kStructMarks` (24; a frame uses 18) and `kStructureNames`; in `State`
     a second timestamp heap (`structQueries`, `structReadback`, `structCount`, `structResolved`, `structKinds`,
     `summaryStruct...`); made beside the stages' heap, reset beside it; the stamps read back where the stages'
     are; the stamp function set after `mark(0)`; stamps after skinning and around `record_omm` and
     `record_emitters`; the counts summed before `mark(1)`; the resolve beside the stages'; the line itself in
     `summarise`, before the caustics' counters are emptied. It prints, per kind, GPU ms a frame and what was
     built, refitted and how many triangles; the sum against the scene stage's time; and the MB held.
2. **The capability question**, new files, wired to NOTHING yet:
   `native/pathtracing/src/mega_geometry.hpp` and `mega_geometry.cpp`: `mega_probe(device, vendor, lines)`. On a
   card that is not NVIDIA's it does not even start NVAPI. Else: the driver's version, the two capability
   answers, the header's limits, and the DRIVER'S OWN SIZES for a town's plants as clusters (6,672,229 triangles
   in 78,190 clusters of at most 128: built from triangles, as templates, stamped from templates, the 636 bottom
   levels from them) and for a partitioned top level of 32,768 and 131,072 instances, beside a plain one's.
   Those sizes are the numbers the plan needs.

### Half done, and what could be wrong

- NOT DONE: the CMake switch `TGPT_MEGA_GEOMETRY` (OFF by default; ON: add `mega_geometry.cpp` to
  `TGPathTracing.Native`, the include folder `NVAPI-*` beside `CMakeLists.txt` or `NVAPI_ROOT`, link
  `amd64/nvapi64.lib`, define `TGPT_MEGA_GEOMETRY=1`); the call in `tracer.cpp` `device_ready`, after the
  "Shared D3D12 device" line, inside `#if TGPT_MEGA_GEOMETRY`; the `.gitignore` line; any source check; the
  hidden `Advanced / Mega Geometry trial` (no field chosen yet); the first piece itself.
- NOT VERIFIED: that the edited files are still CRLF (edits were made with the Edit tool; the two new files were
  written with the Write tool and are probably LF: convert them). Count bare LF with PowerShell first.
- NOT RUN: `python tests/source_checks.py` (a check may pin a line I changed in `tracer.cpp`, for instance the
  one with `mark(1)`, which now stands a few lines below `b.flush(list)`).
- NOT KNOWN: whether `nvapi64.lib` links into a DLL built with the static C runtime; whether NVAPI's calls
  behave on the bridge's device (Agility SDK 1.619.6). Nothing is needed of the bridge so far.
- The main tree's `build\native\generated\pathtracing\*.h` were not copied; no build folder exists for this copy.

### The plan with numbers: NOT WRITTEN, because the numbers are not in yet

What can be said from the code and the other workers' reports, none of it from NVIDIA's documents:
- The ceiling of the partitioned top level is the top level's share of the "scene" stage, which is 1.0 to 1.6 ms
  in all: under 1.6 ms a frame of 80 to 180, and no memory worth naming. Its change is the smaller one IF a ray
  query takes it unchanged (not checked).
- The plants are where the size is: 428 MB of structures in the town (67 bytes a triangle), 795 MB with their
  posed copies, and by `perf-1.md`'s reckoning 3 to 5 ms a frame in play (not measured). Templates fit them (one
  mesh, many posed copies). What clusters would save there is exactly what the probe's second line asks the
  driver.

### The next step, in order

1. Check line endings of the five edited files; make the two new ones CRLF.
2. Wire the CMake switch and `device_ready` (above). Configure `%TEMP%\tgb\megageo1` with `-DTGPT_WITH_NRD=OFF
   -DTGPT_MEGA_GEOMETRY=ON`, copy the main tree's generated shader headers in, build `TGPathTracing.Native` only,
   fix what the compiler says. Source checks.
3. To learn what this PC reports without the game: a ten-line test program of the copy's own that makes a
   Direct3D 12 device and prints `mega_probe`'s lines (through the main tree's `-GpuCheck` gate).
4. Hand-over 1: the build, the run (`metal`, the plainest recipe, plus one step with the world not frozen for
   the plants), the two lines to read ("Mega Geometry: ..." at start-up, "PT structures 10s: ..." every 10 s),
   and the plan from the driver's sizes.
5. Read `nvHLSLExtns.h` (cluster intrinsics) and RTXMG's `LICENSE.txt` and documents before choosing the first
   piece.

### Workarounds met

None removed, none added. Nothing outside the copy was written.
