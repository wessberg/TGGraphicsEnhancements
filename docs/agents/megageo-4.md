# megageo-4: RTX Mega Geometry in the game, the small plants as clusters (PT-109)

Worker `megageo-4` (Claude subagent), copy `...\tg-agents\wt\megageo4`, base `base20mg4`. Brief:
`docs/agents/briefs/brief-megageo-4.md` (main tree). 2026-10-11, from about 15:38.
Never the game, a deploy, a commit, the main tree; no `tgpt_gpu_test` as a whole, no ctest. GPU runs: my own small
check only, the game closed and the main tree's `Run-PtHarness.ps1 -GpuCheck` at exit 0.

## State (kept current)

- 15:55: plan written; reading done; starting the code.
- 16:10: the code is in and builds with no warning (`%TEMP%\tgb\megageo4-build`, TGPT_MEGA_GEOMETRY ON, the main
  tree's other trial options). Source checks: 116 run, all pass (two of them updated, below).
- 16:12: my own small check (`TGPT_TEST_MEGAPLANTS_ONLY`) running on the GPU (the driver is making every pass anew:
  the cache key changed, see "Costs to know").
- 16:15: the check found a real bug at once: the driver refused the templates' size question (NVAPI_INVALID_ARGUMENT)
  for a mesh whose clusters hold fewer triangles in all than one cluster's most (a tuft of 80 triangles against 128).
  Fixed: a call's totals are never below one cluster's most. The trial's failure path worked as meant (one log line,
  the groups plain, no device lost).
- 16:27: **the check passes** (below). The trial build is ready for the coordinator: section (b).

## READ THIS FIRST

- **The trial is built** (not yet seen in game): hidden "Mega Geometry trial" (Advanced) **5** = each cell's small
  plants in a group of their own, built every frame from clusters stamped from templates, NVIDIA's setup as megageo-3
  measured it. **4** = the same split, plain (the yardstick for the split alone). **6** = the split, plain, without the
  small plants' micromaps (5's like-for-like yardstick: clusters take none, see below). 0 and every other value: as
  before (1 to 3 stay the partitioned top level).
- **How the scene tells small plants from trees: it did not.** A plant group was every swaying instance of a 16 m
  cell, trees and grass together; the wind's kind is only GRASS or LEAVES, and bushes and ferns are LEAVES like trees.
  So the trial measures each plant's height in the world (its mesh's box, which every mesh already keeps, turned and
  scaled by its instance): at most 4 m is small. The game's meshes (megageo-2's exports): grass 0.5 to 0.75 m, fern
  0.4 m, hazel bush 0.73 m, beech 13.2 m, pine 21.9 m.
- **Micromaps: NVAPI's clusters take them, ours cannot be handed over safely.** Every cluster call has an OMM array and
  index buffer per template (FLAG_ALLOW_OMM on every call that touches it). But the tracer's micromaps are DirectX
  Raytracing 1.2 arrays; NVAPI documents only its own (NvAPI_D3D12_BuildRaytracingOpacityMicromapArray), and NVIDIA's
  cluster samples (vk_lod_clusters, RTXMG) use none: alpha-masked clusters are simply not OPAQUE and tested in the
  shader. The index buffer would also have to follow the clusters' triangle order. So trial 5 has no micromaps on the
  small plants; compare 5 with 6, not only with 0.
- Every stamped cluster is a submesh record of its group: `fetch_hit` and every pass find the triangle as before. No
  shader changed.

## What was built (against `base20mg4`)

Files (all in the copy):
- `CMakeLists.txt`: inside `if(TGPT_MEGA_GEOMETRY)`: meshoptimizer fetched by `FetchContent_Declare(meshoptimizer
  GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git GIT_TAG v1.3 GIT_SHALLOW TRUE)` + `FetchContent_MakeAvailable`
  (MESHOPT_INSTALL and MESHOPT_BUILD_SHARED_LIBS OFF), linked static into TGPathTracing.Native, its `LICENSE.md`
  installed as `TGPathTracing/Notices/LICENSE-meshoptimizer.md`. NOTE for the main tree: `build/native` is configured
  with `FETCHCONTENT_FULLY_DISCONNECTED=ON`; configure it once with that OFF (or set
  `FETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER` to a v1.3 checkout, e.g. `%TEMP%\tgb\megageo4-build\_deps\meshoptimizer-src`).
- `native/pathtracing/src/mega_geometry.hpp/.cpp`: `MegaMeshClusters` (a mesh's cut and templates), `mega_cut`
  (meshoptimizer's `meshopt_buildMeshletsSpatial` 128 triangles / 256 vertices, min 32, fill 0.5, then
  `meshopt_optimizeMeshlet`; a cluster's closed pieces first, so the record's "solid" count stays exact),
  `mega_upload_order`, `mega_templates` (explicit destinations, room from the driver's per-object size; 32-bit indices
  into the mesh's own vertices; the mesh's positions passed to shape them; geometry index = the cluster's number,
  OPAQUE flag where the member's submesh is solid), `MegaClusterBuilder` (each frame: ONE instantiate call for every
  posed group's clusters, then ONE build-bottom-levels-from-clusters call; FAST_TRACE on all three kinds of call; room
  of a stamp and of a bottom level from the driver's per-object sizes), `mega_enable_cluster_pipelines` (NVAPI's
  ENABLE_CLUSTER_SUPPORT pipeline flag, set before the passes are made, where the driver reports clusters).
- `scene.hpp/.cpp` (all new code between `#if TGPT_MEGA_GEOMETRY` and its `#endif`):
  - `plan_plants`: trials 4 to 6 key a small plant's group with `kMegaSmallTag` (a cell's small plants, a group of
    their own; its origin is its cell's, untagged). Trial 5: a small group is made of clusters when every member's mesh
    is cut and on the GPU and no member holds glass; until then plain, and made anew (the signature changes) once ready.
    Such a group has one record a cluster (its triangles where pt_sway's step 4 puts them, as the member's indices are
    read from the cut's order: each submesh's triangles stay in the submesh's own index range), `geometryMaterials`
    one a record, `clas` (a room a cluster), `blas` (the bottom level's room), never compacted.
  - `mega_prepare` (before plan_plants): the meshes the plan wanted are cut (from the CPU copies where a mesh still
    has them, else read back from the geometry arena as the water does) and their order put on the GPU. The Ready loop
    also cuts a small swaying mesh before its CPU copies are dropped when trial 5 is on ("at conversion").
  - `pose_plants`: plain groups as before; then the posed cluster groups: templates once (per mesh and pattern of
    solid submeshes), stamps, the two calls. A failing call turns the clusters off for good (one log line); such
    groups are made plain the next frame.
  - `record`: a cluster group names a material a record. Memory lines count the new buffers.
  - `distance_bands` reads a group's cell from its origin (the same numbers for plain groups).
- `tracer.cpp`: the Mega Geometry probe moved before `start_passes` (so the pipeline flag precedes every pass); the
  compute pass cache keeps such pipelines apart ("compute+clusters"); the trial's log line names 4, 5, 6.
- `tgpt_api.h`: comment for 4, 5, 6 (no ABI change). `Settings.cs`: the hidden setting's text names 4, 5, 6 (no
  value change: the plugin already clamps to 0..15, so the deployed plugin can send 4 to 6 as it is).
- `tests/ingame/scenarios.json`: recipe `mega-plants` (below). `tests/pathtracing_megaplants_checks.inl` + its include in
  `tests/pathtracing_gpu_test.cpp` (TGPT_TEST_MEGAPLANTS_ONLY; no ctest entry).
- `tests/source_checks.py`: the notices count 4 -> 5, `COMPONENT TGNative` 10 -> 11, the link line names meshoptimizer,
  and a check that meshoptimizer is fetched at v1.3 only inside the switch, noted in LICENSES/ and SOURCES, not copied.
- `LICENSES/THIRD_PARTY.md`, `docs/SOURCES.md`: meshoptimizer v1.3, MIT.

Added: no export, no record layout, no FrameConstants row, no option bit. Flag values: TGPT_Frame.flags bits 2 to 5
take 4, 5, 6 (new values of the existing trial). New log lines: "Mega Geometry: the tracer's pipelines are made with
the driver's support for structures built from clusters"; every 10 s while 5 serves: "Mega Geometry trial 5: N groups of
small plants stamped from clusters this frame (clusters, triangles; room MB); of G groups of small plants, C are made of
clusters (MB); M meshes cut (triangles, s), W waiting for their cut"; a failure: "...the small plants' clusters are off
for good...".

## (b) The trial build for the coordinator (ready 16:27, 49 minutes in)

- The DLL: `C:\Users\Frede\AppData\Local\Temp\tgb\megageo4-build\TGPathTracing.Native.dll`, SHA-256 starts
  `2D6F208041EC`, 31,661,568 bytes (the one my check passed with; the same file is staged in `%TEMP%\tgb\megageo4-native`).
  Built from the copy with TGPT_MEGA_GEOMETRY ON, TGPT_RAIN_WETNESS ON, TGPT_LAMP_AROUND OFF, TGPT_REGROUP_WATER OFF,
  NRD ON (the main tree's `build/native` options). Its shader headers: 44 copied from the main tree's `build/native`
  (built 15:30 to 15:48 from shader sources byte-identical to my copy's), 2 compiled here (`pt_gi_spatial.hlsl` changed in
  the main tree at 16:00, after my base: this DLL has the BASE's `pt_gi_spatial`, not the 16:00 one).
- Deploy (main tree): `.\tools\harness\Deploy-PtBuild.ps1 -Native -NativeFrom C:\Users\Frede\AppData\Local\Temp\tgb\megageo4-build\TGPathTracing.Native.dll -Note "megageo-4: small plants as clusters (Mega Geometry trial 4/5/6)"`.
  The plugin needs no change (it already sends 0 to 15); my Settings.cs change is its hidden text only.
- The recipe is in my copy only: `tests/ingame/scenarios.json` recipe `mega-plants` (copy it into the main tree's file, or
  merge the copy). Steps: 1-game; 2-plain-frozen (0, Denoising None, 16 frames); 3-clusters-frozen (5, 16 frames);
  4-plain-running (0), 5-clusters-running (5), 6-split-nomaps-running (6), 7-split-running (4), 8-plain-running (0)
  (live, perfSeconds 8 each); 9-off. Run: `.\tools\harness\Run-PtHarness.ps1 -Scenario metal,forest -Recipe mega-plants`
  with `-Measure` when the coordinator says (`metal` is the town square of round 20's counts, `forest` the forest).
- **The first start of this build makes every pass anew** (my test: 313 to 345 s for all of them with a cold cache).
- What to read: the steps' frame times, 5 against 6 (clusters against the same split, plain, no micromaps: the trial's
  own question), 6 against 4 (what the small plants' micromaps are worth), 4 against 0 (the split alone), 8 against 4-plain
  (drift); the log's "Mega Geometry trial 5: ..." line (groups made of clusters, clusters, room) and "PT structures"
  (the plants' structure time each 10 s); the two frozen means (2 and 3) look the same. Expect another DRAW, not
  another picture, at partly covered texels and in leaf shadows: an alpha tile's coverage and a leaf's light coin hang on
  the hit's instance, record and triangle numbers (`pt_common.hlsli` ray_chance), which the split and the clusters number
  otherwise. The mean of 16 frozen frames is the same; single frames differ there.

## (c) My small check (`tests/pathtracing_megaplants_checks.inl`, TGPT_TEST_MEGAPLANTS_ONLY=1)

Run 16:21 to 16:27, the game closed, `-GpuCheck` at exit 0 (one other offline test on the card; Starfield started
during the run, which the check allows; its GPU times below are NOT to be quoted). Sixteen bushes (stem + cut-out crown,
216 triangles: 3 clusters each) and twelve tufts (80 triangles, 1 cluster) in the wind over four cells, two 5.2 m trees
(plain in every run), no denoiser, the wind running (every cluster group stamped every frame).

| picture | 5 (clusters) vs 6 (split, plain, no maps) | 6 again | 4 (split with maps) | 0 (one group a cell) | control (a bush 0.3 m off) |
|---|---|---|---|---|---|
| lit, 24 frames | 4 pixels (most 0.082) | 0 | 0 | 0 | 46,943 |
| first hits' colour | 0 | 0 | 0 | 0 | 3,823 |
| direct light | 0 | 0 | 0 | 0 | 8,472 |

The slack is 92 pixels (a hundredth of a percent of 1280 x 720). The log: "4 groups of small plants stamped from
clusters this frame (60 clusters, 4416 triangles; ... 0.8 MB)", no call failed. So **with a cut-out that has no chance in
it, the clusters' picture is the plain path's, pixel for pixel but ties.** With the test's alpha-TILE leaves (a chance by
the hit's numbers, above) the first run showed 552 first-hit pixels off for clusters and 1,762 for the split alone (0
against 6) at partly covered leaf edges: another draw, the same mean; the check now uses a plain cutoff for that reason
and says why. Printed only (Starfield on the card): a frame's GPU time 6.77 ms plain, 7.73 ms clusters (median, 320
frames each, in turn) in this tiny scene: not a number to quote, but the game's run should look at it.

## Costs to know (not the picture)

- **Memory is reserved at the driver's worst case.** A stamp's room is the per-object size for any cluster of 128
  triangles / 256 vertices (no GPU read-back of each template's own size, which megageo-2 used offline). The driver's
  bound for the town (its probe line): 429.5 MB for 78,190 stamped clusters, the same as today's 428 MB of plain
  structures. So trial 5 will NOT show the 23 to 46 % memory saving megageo-3 measured with exact sizes; it answers the
  speed question. Exact rooms are a follow-up if the speed pays.
- **The first start of this build makes every pass anew** (minutes, as after a driver update): the cluster flag changes
  the pipelines, and the cache keeps them apart.
- The pipeline flag is set for the whole shared device in this build (NVIDIA: "may incur a small penalty on traversal
  performance overall"): compare 5 with 0 and 6 of the SAME build, never with another build.

## Departures from NVIDIA's setup, said plainly

- No `instantiationBoundingBoxLimit` (vk_animated_clusters uses its cluster box + half the mesh's diagonal, for copies
  in their own space): our stamps are in the group's space, where a member may stand anywhere in its cell; megageo-3's
  measured runs in the group layout used none either.
- The geometry index is a record of its own a cluster (up to 65,535 in a group; a larger group stays plain) where
  NVIDIA keeps it constant and reads the cluster ID: reading the cluster ID would need NVAPI's shader extension in every
  pass (shader code that costs frame time even when off). megageo-3 measured no speed difference between the two.
- Rooms at the driver's worst case (above), not exact sizes from a GET_SIZES pass and a read-back.
- No micromaps in clusters (above).

## Next, if the game says the clusters pay

1. Exact rooms (GET_SIZES per template once a mesh, read back a few frames later): the memory saving megageo-3 measured.
2. Micromaps in clusters: an offline check (megageo-3's bench) whether NVAPI's cluster templates take a DirectX
   Raytracing 1.2 micromap array (FLAG_ALLOW_OMM, the array and an index buffer in the cut's triangle order); if not,
   NVAPI's own micromap arrays built from the same bake. Not done: the game's answer decides whether it is worth it.

## Workarounds met

None removed, none added.

## How to ask again

Build (VS 2022 x64): `%TEMP%\claude\...\scratchpad\mg4\configure.bat` then `build.bat TGPathTracing.Native` and
`build.bat tgpt_gpu_test` (session scratch); shaders: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\megageo4-build`.
Check, the card free: `TGPT_TEST_MEGAPLANTS_ONLY=1 TGPT_TEST_STAGE=tgpt-native-megaplants TGPT_TEST_STARTUP_SECONDS=1500
tgpt_gpu_test.exe %TEMP%\tgb\megageo4-native %TEMP%\tgb\megageo4-images` (a minute with a warm pass cache; raw output
`%TEMP%\tgb\megageo4-check4.log`). `python tests/source_checks.py`: 116 run, all pass.

## State when stopped

Done for my part (16:30): (a) built, (b) the trial build handed over (above), (c) my small check passes. The game's
run is the coordinator's. No build or GPU run of mine is still going. Nothing committed, deployed or written outside the
copy, `%TEMP%\tgb\megageo4-*` and the session scratch.
