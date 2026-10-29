# megageo-5: RTX Mega Geometry for the plants as NVIDIA designs it: micromaps in clusters, a structure per copy, measured walking (PT-109)

Worker `megageo-5` (Claude subagent), copy `...\tg-agents\wt\megageo5`, base `base20mg5`. Brief:
`docs/agents/briefs/brief-megageo-5.md` (main tree). 2026-10-11, from about 17:08.
Never the game, a deploy, a commit, the main tree; no `tgpt_gpu_test` as a whole, no ctest. GPU runs: my own small
check only, the game closed and the main tree's `Run-PtHarness.ps1 -GpuCheck` at exit 0.

## State (kept current)

- 17:25: read the brief, AGENTS.md's three rulings, megageo-3 and -4, PT-109, NVAPI R615's cluster and micromap
  declarations, the scene's plant groups, the bake (`record_omm`) and the harness's walk. Plan below; coding.
- 17:37: (a), (b) and (c) are written and build with no warning of mine (`%TEMP%\tgb\megageo5-build`, the main tree's
  build options; its 52 shader headers copied from the main tree's `build\native`, made there from shader sources, flags,
  dxc and RTXDI headers identical to my copy's: no shader changed in this job). Source checks: 116 run, all pass (one
  updated: below). `tests/harness_analyze_test.py` passes. The harness plugin builds.
- 17:38: my small check (`TGPT_TEST_MEGAPLANTS_ONLY`, extended) running on the GPU (`-GpuCheck` exit 0 at 17:37; the pass
  cache is cold for this build: the pipelines now carry NVAPI's micromap support too).

## READ THIS FIRST

- **Trial values** (hidden "Mega Geometry trial", Advanced; the plugin already sends 0 to 15, no plugin change needed):
  - **5** = the small plants' groups made of clusters (megageo-4's), now **with their micromaps** as NVAPI's own arrays
    built from the same bake. Its yardstick is **4** (the same split, plain, with micromaps). 6 stays (the split without
    micromaps).
  - **7** = **NVIDIA's layout**: every plant group (one a 16 m cell, small and tall plants together, as it ships) made of
    clusters with micromaps, **each plant its own bottom level** built from its clusters, **each its own instance**, in the
    **partitioned top level** (128 m cells). Yardsticks: **0** (as it ships) and **2** (the partitioned top level alone, 128 m).
  - **8** = 7 without micromaps (what the micromaps in clusters are worth).
  - 1, 2, 3, 4, 6 as before.
- **Micromaps in clusters, to the letter of NVAPI R615**: the driver is asked for NVAPI's micromaps
  (`NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP`); the pipelines are made with `ENABLE_OMM_SUPPORT` beside
  `ENABLE_CLUSTER_SUPPORT` (nvapi.h 19636-19646: "If the pipeline is not created with the correct primitive support, and
  that primitive type is encountered during traversal, behavior is undefined"); a plant mesh's micromaps, baked as before
  by `pt_omm_bake`, are built a second time with `NvAPI_D3D12_BuildRaytracingOpacityMicromapArray` from the same bake
  output (same bytes, one descriptor a triangle, 4-state, its level, PREFER_FAST_TRACE) in the same frame; the bake's
  index buffer (already read back for PT-110's count) is written in each cluster's triangle order and linked per template
  (`opacityMicromapArray`, `opacityMicromapIndexBuffer`, 32-bit; special indices kept; NVAPI's CLUSTER_SKIP_OMM for
  submeshes without micromaps, which no template names); `FLAG_ALLOW_OMM` on every cluster call (templates, stamps,
  bottom levels) while a trial links micromaps (nvapi.h 20873).
- **The cut keeps every triangle's own corners in their own order** (a micromap hangs on the corners' order: the bake's
  barycentrics are the mesh's) and knows each triangle's number in its submesh (the bake's indices are in that order).
  meshoptimizer may hand a triangle back turned; it is matched to its own and put back as the mesh has it. Side effect:
  the "closed pieces first" count is now exact (by the triangle's number, not by its corners).
- **Cluster size**: NVIDIA's for animated clusters (vk_animated_clusters: 64 triangles, 64 vertices), which megageo-3
  measured best for rays through trees and equal on small plants (5 used 128/256 in megageo-4's build).
- **NVIDIA's layout and our records**: the groups stay the unit of the wind's pose (pt_sway), of the buffers and of the
  records; only the bottom levels and the instances change. A plant's instance carries its group's record, place, mask
  and flags; its clusters' geometry indices are their records in the group: every hit finds its triangle as before, **no
  shader changed**.
- The first start of this build makes every pass anew (minutes): the pipelines' flags changed again (cache kind
  "compute+clusters+omm").
- When the trial switches to 5 or 7 the plant meshes' micromaps baked before it are baked again once (they had no NVAPI
  array), as M1 does for its index buffer: a few seconds in which plants trace still or without micromaps.

## What was built (against `base20mg5`)

- `native/pathtracing/src/mega_geometry.hpp/.cpp`: `MegaCaps.omm`; the pipelines' flags; `mega_cluster_micromaps()`;
  `kMegaClusterTriangles/Vertices` 64/64; `mega_cut` (corners and order kept, `source`); `MegaMicromaps` and
  `mega_build_micromaps` (NVAPI's array from a bake); `MegaTemplateKey` (opaque submeshes, linked submeshes, the
  micromaps' build, the ALLOW_OMM flag); `mega_templates` (the cluster-order micromap index buffer once a build, kept per
  build in `ommOrders`; each template links its cluster's micromaps); `mega_release_maps`; the builder's calls and room
  questions carry ALLOW_OMM when asked.
- `scene.hpp/.cpp` (all new code inside `#if TGPT_MEGA_GEOMETRY`): `Mesh::megaMaps`; `PlantGroup::MegaMember.linked/serial`,
  `PlantGroup::perCopy/mapped/copies`; `mega_clusters_on()`, `mega_maps_on()`, `mega_cluster_plant()`; plan_plants
  (7 and 8: every group made of clusters, the trial's number in the signature; a cluster group waits, plain, for a linked
  member's indices to come back; micromaps baked before the trial asked for NVAPI's array are baked again once);
  pose_plants (templates by key; a bottom level a plant at 7 and 8; the frame's calls with ALLOW_OMM; the log line also
  when the trial or the count of plants that link micromaps changes); record (an instance a plant at 7 and 8; the
  partitioned top level at 7 and 8, 128 m); record_omm (NVAPI's array from the bake, for plant meshes, while 5 or 7 runs);
  count_omm (the bake's indices kept for it); release_omm, the mesh's retirement and the scene's clear (the array, its
  templates and index buffers go, after the GPU is past the frame); memory lines count them.
- `tracer.cpp`: the trial's log line names 5, 7, 8; the pass cache kind; `TGPT_GetStageTimes` adds the scene stage's
  structures by kind over the same second ("; structures: plants 1.20 ms (2.0 built, 600.0 refitted a frame), ...").
- `tgpt_api.h`, `Settings.cs`: comments and the hidden setting's text (no ABI change, no value change).
- Harness (c): `Request.cs` (`Step.perSecond`; `PerfInfo.over50/over100/perSecond`; `PerfSecond`), `Runner.cs` (`Perf` takes
  a row a second when the step asks or walks), `Traced.cs` (`Diagnostic(field)`: the plugin's TGPT_Diagnostics),
  `tools/harness/analyze.py` (`per_second_tables`: a table a measured walk in report.md).
- `tests/ingame/scenarios.json`: recipe `mega-plants` rewritten (below), new recipe `mega-walk`.
- `tests/pathtracing_megaplants_checks.inl`: 5 against 4, 7 and 8 against 0, 4 again, 6, the control; the log must say the
  bushes' clusters link micromaps at 5 and 7 where the driver reports them; GPU times of 0, 5, 7, 8 printed.
- `tests/source_checks.py`: the partitioned top level's two pinned lines (7 and 8 added), and new pins: the micromap
  caps question, NVAPI's array build, the pipelines' flags, the template's 32-bit micromap index format, the corners kept,
  ALLOW_OMM on both kinds of call, 64/64; the `mega-walk` recipe's trial values and walk.

Added: no export, no record layout, no FrameConstants row, no option bit. TGPT_Frame.flags bits 2 to 5 take 7 and 8 (new
values of the existing trial). New log lines: "Mega Geometry: the driver reports NVAPI's own opacity micromaps (which its
clusters take): YES/no"; the pipelines' line names the micromap support; "Mega Geometry trial N: G groups of plants
stamped from clusters this frame (clusters, triangles, bottom levels[, one a plant]; room); of P groups of the trial's
plants, Q are made of clusters (MB); micromaps in clusters: on/off, L of their M plants link them, A meshes' as the
clusters' own micromap arrays (MB; built, failed, baked again for them); meshes cut ..."; "Mega Geometry trial: a plant
mesh's micromaps could not be made as the clusters' own micromap array (...)". (The words "NVAPI" may not appear in
scene.cpp or tracer.cpp: a source check.)

## Departures from NVIDIA's setup, said plainly

- A plant's vertices stay in its group's space (one buffer a group, posed by pt_sway), so its instance carries the
  group's place, not the plant's own transform (vk_animated_clusters: each copy in its own space, the transform in the
  instance). The shading reads one buffer a group; moving each plant into its own space would change pt_sway and the
  records. megageo-3 measured the difference on spheres: +33.7 % against +26.7 % TraceRay (rows 5 and 6).
- No `instantiationBoundingBoxLimit` (follows from the above: a template is stamped anywhere in a 16 m cell).
- Geometry index: a record a cluster (megageo-4's), not 0 with the cluster ID read in the shader (that needs NVAPI's
  shader extension in every pass); megageo-3 measured no speed difference.
- Rooms at the driver's worst case (megageo-4's), not exact sizes from a GET_SIZES pass.
- NVIDIA's samples use no micromaps with clusters at all; ours follow NVAPI's header to the letter, but no sample of
  NVIDIA's shows it working: the game's numbers (8 against 7) say whether the driver honours them.

## What the plain groups stay for

Non-NVIDIA cards and drivers without clusters; any group that holds glass (its checks read a plain group's records); a
group whose members' meshes are not cut yet, or whose linked micromaps' indices are still on their way back (a few
frames); trial values 0 to 4 and 6; and the moment any cluster call fails (then for good, said once).

## (c) The walk

Recipe `mega-walk` (run it on `metal` and `forest`): 1-game; 2-plain-walk (0); 3-nvidia-layout-walk (7);
4-plain-walk-again (0); 5-nvidia-layout-walk-again (7); 6-off. Each traced step settles 25 s and 120 frames at the save's
view, then the camera walks straight forward 150 m at 5 m/s (30 s) and holds; frames are measured for 34 s from the
walk's start (`perfSeconds` 34, `perSecond`), Denoising None. The 0 and 7 alternate, so each has a walk that is the
route's first for its structures and one that is not. report.md then has, per walk: the frames over 50 and 100 ms and the
worst, and a row a second: frames, mean and worst frame time, frames over 50 ms, the tracer's GPU time, the scene stage,
the plants' structures (ms, built and refitted a frame), the top level, what the traced world still waits for
(instances, plants, meshes, micromaps, tiles), the acceleration structures' memory, the camera's x and z. The native
log's "PT structures" and "Mega Geometry trial 7: ..." lines every 10 s give the rest.
Caveats: the camera moves, the hero does not (`CameraSlide`): what streams in with the hero's position does not come;
what comes with the camera's distance (levels of detail, plants' groups made anew, micromap bakes) does. The straight
route may pass through a wall or a hill: the frames still count.

## The standing recipe `mega-plants` (rewritten)

1-game; 2-plain-frozen (0, Denoising None, 16 frames); 3-small-clusters-maps-frozen (5); 4-nvidia-layout-maps-frozen (7);
then live, 8 s of frame times each: 5-plain-running (0), 6-small-clusters-maps-running (5), 7-small-split-plain-running
(4), 8-nvidia-layout-maps-running (7), 9-nvidia-layout-nomaps-running (8), 10-partitioned-top-only-running (2),
11-plain-running (0); 12-off. What to read: 6 against 7 (the small plants' clusters with micromaps against the same split
plain: last run 68.6 against 66.1 ms in the town, 100.8 against 83.5 in the forest without micromaps in the clusters);
8 against 10 and 5/11 (NVIDIA's layout against the partitioned top level alone and as it ships); 8 against 9 (what the
micromaps in clusters are worth: the forest's +11 ms was that); 5 against 11 (drift). The two frozen pictures (3, 4)
look as 2.

## (d) The trial build for the coordinator

(Filled in when my check has passed.)
