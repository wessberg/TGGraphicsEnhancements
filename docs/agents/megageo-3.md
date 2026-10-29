# megageo-3: why our clusters trace slower than NVIDIA's, and the plants on NVIDIA's setup (PT-109)

Worker `megageo-3` (Claude subagent), copy `...\tg-agents\wt\megageo2` (megageo-2's, its bench), base `base20mg`.
Brief: `docs/agents/briefs/brief-megageo-3.md` (main tree). 2026-10-11, from about 14:55.
Never the game, a deploy, a commit, the main tree; no `tgpt_gpu_test`, no ctest. GPU runs of the bench only, the game
closed and `Run-PtHarness.ps1 -GpuCheck` at exit 0.

## READ THIS FIRST (15:40)

- **Our integration is not the fault.** NVIDIA's own sample scene, rebuilt in our bench exactly as
  `vk_animated_clusters` sets it up (its clusterizer, sizes, flags, one structure a copy, copies side by side),
  traces only **+3 to +8 % slower** with clusters on this RTX 5080 and driver, through Direct3D 12 and NVAPI. That is
  NVIDIA's "5 %". No driver or Vulkan-versus-D3D12 question is left.
- **Why the plants were +40 to +130 %:** two things. (1) NVIDIA's 5 % compares against a weaker baseline (a
  "fast build" structure that is not compacted, itself 23 % slower than ours); against ours NVIDIA's exact setup is
  +26 to +31 % on overlapping spheres. (2) **Overlap:** the plants' groups put 16 to 18 copies on top of each other
  inside ONE structure, and trees are porous leaf cards. A structure built over clusters cannot split a cluster, so
  a ray passing near many leaves opens many clusters. NVIDIA's own docs warn about exactly this. With no overlap
  (one sphere a structure), the cost falls to +3 to +10 %; one tree a structure still costs +15 to +26 %.
- **Our own departures that did cost speed:** megageo-2's clusterizer and cluster size. NVIDIA's (meshoptimizer's
  spatial builder, 64 triangles) is 16 to 70 points better on the trees and the sphere; on the small plants 128
  triangles with NVIDIA's clusterizer is as good or better. Equal, so not faults: geometry index, OPAQUE flag, index
  width, the calls' maxima, FAST_BUILD vs FAST_TRACE on stamps.
- **megageo-2 was wrong on one design point:** clusters do NOT need their own vertex lists. NVIDIA's default (32-bit
  indices into each copy's own vertices) works here at the same speed, so the +11 to +24 % extra posed vertices go.
- **The plants on NVIDIA's setup, in the game's layout (a group a structure), TraceRay:** trees **+31 to +55 %**
  (were +64 to +118 %), grass +9 to +27 %, bush and fern +4 to +14 %. Per-frame work: -9 to -40 % at 64 triangles
  (beech 1.11 -> 0.68 ms), -44 to -58 % at 128. Structures: -7 to -35 %.
- **Verdict: it does not pay yet for the trees, and maybe for the small plants.** The town's saving is about 0.2 to
  0.8 ms a frame with the world running (nothing when frozen) and 20 to 95 MB; every ray through a tree pays +31 to
  +55 % inside that tree. Only a game measurement can say how much frame time rays spend inside plant groups; in a
  forest it is most of the frame. The best candidate is a trial for SMALL plants only (grass, bushes, ferns: +4 to
  +27 %), trees staying plain, measured in the game. Not measured here: opacity micromaps inside clusters (the game's
  plants use them; NVAPI's clusters accept them).

## Plan (14:58)

1. (a) Read NVIDIA's guidance: NVAPI R615 `nvapi.h` cluster comments; nvpro-samples `vk_animated_clusters`,
   `vk_tessellated_clusters`, `vk_lod_clusters`, `nv_cluster_builder`, `vk_partitioned_tlas`; NVIDIA-RTX `RTXMG`
   (all cloned read-only into `%TEMP%\tgb\megageo3-deps`); NVIDIA's docs and blog posts. Write each one's cluster
   sizes, clusterizer, vertex/index formats, template vs direct, flags, how they measure.
2. (b) Diff that against megageo-2's bench, list every difference.
3. (c) Reproduce NVIDIA's setup on the sphere first (their clusterizer, sizes, flags, scene layout), one difference
   at a time, numbers into this report as they come.
4. (d) The game's plants on that setup; verdict.

## (a) NVIDIA's guidance (read 14:55 to 15:05)

Sources, cloned at these commits into `%TEMP%\tgb\megageo3-deps` (read only, nothing copied into the repo):
`vk_animated_clusters` ea22deb (2026-09-30), `vk_lod_clusters` 20352f0 (2026-10-06), `vk_tessellated_clusters`
00f4758, `vk_partitioned_tlas` 07acd56, `nv_cluster_builder` 2c5634b (archived), `nv_cluster_lod_builder` e27f05a,
NVIDIA-RTX `RTXMG` 93cb79f (2026-10-06, DirectX 12 through nvrhi), meshoptimizer b233043 (2026-10-09, MIT).
All nvpro-samples and RTXMG are Apache-2.0; meshoptimizer MIT; NVAPI MIT.

### The 5 % (vk_animated_clusters README, "Preliminary Performance Results")

- Scene: 121 copies (11x11 grid, no overlap) of the Stanford bunny, 8.43 M triangles, every copy animated, **each
  copy its own BLAS** ("Ray tracing will use a dedicated BLAS per-instance"). GPU: **RTX 6000 Ada**, 2560x1440 with
  2x supersampling.
- What is compared: **"Render" time of the whole frame** (shadow and ambient occlusion rays plus shading), not the
  traversal alone: triangles 10 % rebuilds 1.39 ms, triangles refit only 1.45 ms, clusters with templates 1.52 ms.
  So +5 % against **refit-only** and +9 % against 10 % rebuilds.
- The triangle baseline is NOT ours: "The BLAS was set to `Fast Build`", `ALLOW_UPDATE` always set, **no compaction**
  ("the sample was mostly meant to showcase faster builds for animated content, therefore no compaction is
  performed"). Templates `Fast Trace`, instantiation `Fast Build`, cluster BLAS `Fast Build`.
- NVIDIA's own words in the same README: "the triangle BLAS can provide better ray intersection performance. You can
  observe this in this sample as well: using clusters may render slower with higher numbers of ambient occlusion
  samples ... the sample's simple shading stresses intersection performance"; "As the BLAS is built over clusters
  and not individual triangles, a degradation in actual rendering performance is expected"; "for purely static data,
  the traditional triangle BLAS is still recommended"; recommended when "the triangle mesh is changing frequently
  ... animation updates through skinning or other deformation ... it should supercede triangle BLAS refits".
- So the 5 % is NOT "rays through clusters cost 5 % more than rays through a fast-trace compacted BLAS". Our bench
  measures exactly that harder thing (pure traversal, against PREFER_FAST_TRACE compacted). The two numbers are not
  comparable as they stand; (c) below reproduces both baselines.

### vk_animated_clusters setup (src/scene.cpp, scene.hpp, animatedclusters.cpp/.hpp, renderer_raytrace_clusters.cpp)

- Cluster size **64 triangles / 64 vertices** default (`scene.hpp:28-29`, README: "We found that 64 triangles 64
  vertices works quite well. Larger clusters save more memory and speed up animation updates, but they can
  negatively impact render time"). Options up to 256/256.
- Clusterizer: **meshoptimizer `meshopt_buildMeshletsSpatial`** (`scene.cpp:310`), min triangles = max/4 rounded down
  to 4 (`(64/4)&~3` = 16), fill weight 0.5; then **`meshopt_optimizeMeshlet`** on every cluster (`scene.cpp:372`). The
  triangle BLAS uses the SAME re-ordered triangle buffer (`rebuildGeometryTriangles`).
- `nv_cluster_builder` (the clusterizer the brief names) is **archived**: its README says its ideas went into
  `meshopt_buildMeshletsSpatial`, "to which we refer interested developers". So NVIDIA's clusterizer today = meshopt's
  spatial builder. meshopt's own RT preset (`demo/clusterlod.h` `clodDefaultConfigRT`): min = max/4, max vertices =
  min(256, 2 x max triangles) ("the vertex count is not important for ray tracing performance"), spatial, fill 0.5.
- Vertices and indices: default `clusterDedicatedVertices = false` (`scene.hpp:33`): **32-bit indices into the
  copy's whole vertex buffer**, template `vertexBuffer` = the mesh's positions, instantiation `vertexBuffer` = the
  copy's whole positions buffer (`renderer_raytrace_clusters.cpp:673-684, 963-975`). A cluster is NOT given its own
  vertex list; the template stores the indices. (megageo-2's design finding 1, "each cluster needs its own vertex
  list", does not hold: NVIDIA's default needs none.) 8-bit local indices with dedicated vertices are an option.
- Geometry index **0** for every cluster and **`OPAQUE` geometry flag** (`:665`, `:1131`); identity from the
  cluster ID (`clusterID = c`, offsets 0). README: "`GeometryIndex` ... We highly recommend keeping it constant per
  cluster, or when required use values that require only few bits overall."
- Template bounding box limit: cluster box + 0.5 x mesh diagonal ("Template bbox bloat", UI default 0.5,
  `animatedclusters.hpp:105`).
- Position truncation: 0 by default, "highly recommended" to raise ("can also improve trace performance").
- Destinations: templates COMPUTE_SIZES then EXPLICIT; instantiation EXPLICIT with the per-template worst sizes; BLAS
  IMPLICIT. The ray tracing pipeline has `allowClusterAccelerationStructure`.

### vk_lod_clusters, RTXMG, vk_tessellated_clusters

- `vk_lod_clusters`: **128 triangles / 128 vertices** default (`scene.hpp:49-50`), meshopt clusterlod with
  "prefer ray tracing" (spatial, fill 0.5), CLAS and BLAS **FAST_TRACE** (`scene_preloaded.cpp:277`,
  `scene_streaming.cpp:2234`), `maxGeometryIndexValue` 0, or 1 when alpha-masked (`scene_preloaded.cpp:240-249`),
  opaque clusters flagged `OPAQUE`, alpha-masked ones geometry index 1 without it (`:395-418`), position truncation
  from its compression. Its scene docs: "The ray tracing performance does suffer from the background mountains
  overlapping with the primary buildings" (overlap is the known enemy).
- `RTXMG` (DirectX 12): cluster LOD CLAS and BLAS **FastTrace**, `maxGeometryIndex` 0 (1 with alpha masks),
  `maxUniqueGeometryCount` 1 to 4 (`rtxmg/cluster_lod/preloaded.cpp:608-622, 739`); tessellation templates with
  flags None, `maxGeometryIndex` 0 (`cluster_tess/cluster_tessellator.cpp:425-435`).
- `vk_tessellated_clusters`: templates always FAST_TRACE (README line 22).
- `nv_cluster_builder` README: clusters should have a small box (low SAH), low overlap ("Bounding box overlap is bad
  for ray tracing because rays must enter both while in the overlap volume").
- `vk_animated_clusters` README line 8: "for best performance the triangles should be spatially grouped in such ways
  that the axis-aligned bounding boxes of clusters minimize overlap among each other."

### NVAPI R615 (`NVAPI-R615\nvapi.h`, main tree)

- 20853-20862: CLAS alignment 128, template 32, template bounds 32, maximum geometry index 16,777,215.
- 20867-20874: operation flags FAST_TRACE, FAST_BUILD, NO_OVERLAP (moves only), ALLOW_OMM.
- 20888-20894: geometry flags in the top bits of `baseGeometryIndexAndFlags`: CULL_DISABLE bit 29,
  NO_DUPLICATE_ANYHIT bit 30, **OPAQUE bit 31**.
- 20945-20951: `maxGeometryIndexValue` ("the maximum value of any geometry index that will occur"),
  `maxUniqueGeometryCountPerArg`, max triangles/vertices per arg (256 each), `minPositionTruncateBitCount`.
- 21102: the template's vertex buffer is optional and only "to optimize the cluster template".
- 21112: `instantiationBoundingBoxLimit`. 21121: `geometryIndexOffset` may not push past `maxGeometryIndexValue`.
- 19647: `ENABLE_CLUSTER_SUPPORT` pipeline flag. `nvHLSLExtns.h` 1738-1756, 2032, 2402: `NvRtGetClusterID()`,
  `NvRtGetCommittedClusterID(rq)`, `HitObject.GetClusterID()`: the cluster ID is readable in every kind of shader,
  so the hit can be identified without a large geometry index.
- The comments say nothing about trace speed beyond the FAST_TRACE/FAST_BUILD flags.

### Blog posts
- "Fast Ray Tracing of Dynamic Scenes Using NVIDIA OptiX 9 and NVIDIA RTX Mega Geometry"
  (developer.nvidia.com/blog/fast-ray-tracing-of-dynamic-scenes-using-nvidia-optix-9-and-nvidia-rtx-mega-geometry):
  "For certain types of scenes, a CLAS can provide the speed of a refit, while retaining most of the flexibility and
  trace performance of a full rebuild"; grid templates "often smaller and faster to ray trace". No trace number.
- "NVIDIA RTX Mega Geometry Now Available with New Vulkan Samples"
  (developer.nvidia.com/blog/nvidia-rtx-mega-geometry-now-available-with-new-vulkan-samples): no trace numbers.
- No NVIDIA document found that promises cluster traversal within 5 % of a FAST_TRACE compacted BLAS.

## (b) Every difference between NVIDIA's setup and megageo-2's bench

| # | NVIDIA (vk_animated_clusters unless named) | megageo-2's bench |
|---|---|---|
| 1 | one BLAS per copy (one mesh), copies on a grid, no overlap inside a BLAS | one BLAS per group of 16 copies, randomly turned and placed in a 16 m cell: copies overlap INSIDE one BLAS (as the game's plant groups do) |
| 2 | 64T/64V (animated), 128T/128V (lod) | 128T/256V (also 64/64 tried) |
| 3 | meshopt `buildMeshletsSpatial`, min = max/4, fill 0.5, then `optimizeMeshlet` | own SAH bisection in exact multiples of max (no min/max range), no in-cluster optimisation |
| 4 | 32-bit indices into the copy's own vertex buffer; no per-cluster vertex lists | per-cluster vertex lists (+11 to +24 % vertices), 8-bit indices |
| 5 | geometry index 0 (0/1 with alpha), `maxGeometryIndexValue` 0/1, OPAQUE flag; identity by cluster ID | geometry index unique per copy and cluster (up to 16 x C - 1, about 1,000 to 2,000), `maxGeometryIndexValue` that large, no OPAQUE flag |
| 6 | templates FAST_TRACE; instantiate and cluster BLAS FAST_BUILD (animated) or FAST_TRACE (static, lod, RTXMG) | FAST_TRACE everywhere |
| 7 | template box limit = cluster box + 0.5 x mesh diagonal | none (option: a box of the whole field) |
| 8 | baseline triangle BLAS FAST_BUILD + ALLOW_UPDATE, refit, uncompacted, cluster-ordered triangles | baseline PREFER_FAST_TRACE + ALLOW_UPDATE + COMPACTION, refit, the mesh's own order |
| 9 | whole frame time (shading + AO), RTX 6000 Ada, 2x supersampled 1440p | traversal alone, RTX 5080, 1080p, camera ray + one incoherent ray |
| 10 | position truncation 0 by default (recommended to raise) | 0 |
| 11 | pipeline cluster flag set | set (both kinds of pass) |

## (c) Reproducing NVIDIA's setup on the sphere

The bench: `tests/megageo3_cluster_bench.cpp` (a copy of megageo-2's with NVIDIA's setup as options, each off by
default so megageo-2's runs read the same; its header lists them). Built with meshoptimizer's `clusterizer.cpp`,
`meshletutils.cpp`, `allocator.cpp` (MIT, from `%TEMP%\tgb\megageo3-deps\meshoptimizer`, linked in, nothing copied);
build script `scripts\mg3_build.bat` in the session scratch, output `%TEMP%\tgb\megageo3-bench`. /W4, no warnings.

Clusterizers compared on the CPU (`--dry 1`; a cluster's box diagonal, average):

| mesh | megageo-2's SAH cut, 128T/256V | meshopt spatial 64T/64V (NVIDIA animated) | meshopt spatial 128T/128V or 128/256 (NVIDIA lod) |
|---|---|---|---|
| sphere (8,096 tris) | 64 clusters, 126.5 tris, box 2.24 m | 130 clusters, 62.3 tris, box 1.52 m | 65 clusters, 124.6 tris, box 2.18 m |
| beech (7,603) | 60 clusters, 126.7 tris, box 3.47 m | 210 clusters, 36.2 tris, box 1.91 m | 62 clusters, 122.6 tris, box 3.25 m |
| pine (5,685) | 46 clusters, 123.6 tris, box 5.66 m | 160 clusters, 35.5 tris, box 3.12 m | 53 clusters, 107.3 tris, box 4.70 m |

So at 128 triangles NVIDIA's clusterizer makes nearly the same clusters as megageo-2's (boxes 3 to 17 % smaller);
on the trees at 64 it makes many under-filled clusters (36 of 64 triangles), which meshopt allows (min 16) when it
shrinks their boxes.

GPU runs: the game ran until 15:13 (`-GpuCheck` exit 1); a background script ran each run only after `-GpuCheck`
exit 0, before EVERY run. Raw output: `%TEMP%\tgb\megageo3-bench\sphere1.log`.

### Sphere batch 1 (15:13 to 15:15, the card free; camera 1, 32 groups x 16 spheres, 4.15 M triangles)

"Rays" = clusters against plain, the card's median of 10 rounds, + = clusters slower. Every run: every ray hit the
same triangle at the same distance (100.0000 %).

| # | setup | ray query solid / tested | TraceRay solid / tested | plain ms (solid, ray query) |
|---|---|---|---|---|
| 1 | megageo-2's (SAH cut 128T/256V, group BLAS, unique geometry index, plain FAST_TRACE compacted) | +58.8 / +45.2 % | +43.3 / +38.2 % | 0.282 |
| 2 | 1 + OPAQUE flag on both | +52.9 / +56.9 % | +44.1 / +42.7 % | 0.287 |
| 3 | 1 + geometry index 0 | +55.5 / +47.1 % | +44.2 / +38.3 % | 0.289 |
| 4 | 1 + geometry index 0 + OPAQUE | +60.0 / +53.2 % | +43.5 / +43.5 % | 0.283 |
| 5 | 1 + one BLAS a member (split) | +40.1 / +35.4 % | +33.7 / +29.5 % | 0.357 |
| 6 | 5 + members' vertices in their own space, placed by the instance | +28.4 / +24.5 % | +26.7 / +23.4 % | 0.364 |
| 7 | **meshopt spatial 64T/64V** (group BLAS, else as 1) | **+33.4 / +31.0 %** | **+26.9 / +24.1 %** | 0.286 |
| 8 | meshopt spatial 128T/128V (group BLAS) | +41.7 / +34.7 % | +32.6 / +29.9 % | 0.287 |
| 9 | 1 + 32-bit indices into the member's own vertices | +51.8 / +46.2 % | +44.1 / +38.9 % | 0.290 |
| 10 | 1 + the calls' maxima from the real clusters | +54.8 / +45.5 % | +43.3 / +38.9 % | 0.290 |
| 11 | **vk_animated_clusters exactly** (meshopt 64/64, 32-bit indices, geometry 0, OPAQUE, a BLAS a copy, transforms in the instance, templates FAST_TRACE, stamps and cluster BLAS FAST_BUILD, box limits 0.5) **against its own baseline** (triangle BLAS FAST_BUILD + ALLOW_UPDATE, not compacted, refitted) | **+4.0 / +3.0 %** | **+7.5 / +7.3 %** | 0.446 |
| 12 | 11 against OUR baseline (FAST_TRACE compacted) | +30.9 / +27.1 % | +25.8 / +25.1 % | 0.362 |
| 13 | vk_lod_clusters' choices (meshopt 128/128, all FAST_TRACE, geometry 0, OPAQUE, a BLAS a copy, local) against FAST_TRACE compacted | +22.6 / +18.3 % | +20.0 / +19.7 % | 0.364 |

What this says:
- **NVIDIA's 5 % is reproduced here, on this RTX 5080 and driver, through D3D12/NVAPI** (row 11: +3 to +7.5 %) - but
  only against NVIDIA's own baseline, a FAST_BUILD triangle BLAS that is not compacted. That baseline is itself 23 %
  slower than ours (0.446 against 0.362 ms in the same layout). Against a FAST_TRACE compacted BLAS, which is what
  the plants use, NVIDIA's exact setup is **+26 to +31 %** (row 12), and vk_lod_clusters' all-FAST_TRACE choice
  **+18 to +23 %** (row 13). So there is no driver or D3D12 fault: the "8x gap" was mostly two different questions.
- Things that do NOT matter on this card: the geometry index (0 or unique), the OPAQUE flag, 32-bit or 8-bit
  indices, per-cluster vertex lists, the calls' maxima (rows 2, 3, 4, 9, 10: all within the runs' noise).
- Things that DO matter: the clusterizer (meshopt's spatial builder: row 1 -> 7 and 8, 15 to 25 points better), the
  cluster size with meshopt (64 better than 128 for rays), and the layout (a BLAS a member with its transform in
  the instance: rows 5, 6; but that layout makes the PLAIN rays slower too, 0.282 -> 0.364 ms, so in absolute time
  the group BLAS stays the faster for both).
- megageo-2's "64/64 is worse" (+148 %) came from its own cutter; with NVIDIA's clusterizer 64/64 is the best tried.

### Sphere batch 2 (15:16 to 15:25, the card free before every run; the game ran 15:17 to 15:22, nothing ran then)

Raw: `%TEMP%\tgb\megageo3-bench\sphere2.log`. "MO64" = meshopt spatial 64T/64V, else megageo-2's defaults (group
BLAS of 16 overlapping spheres, FAST_TRACE everywhere, plain FAST_TRACE compacted).

| setup | ray query solid / tested | TraceRay solid / tested | plain ms | same hits |
|---|---|---|---|---|
| meshopt 32T/32V | +40.5 / +33.6 % | +31.3 / +27.1 % | 0.283 | 100 % |
| meshopt 32T/64V | +39.8 / +34.3 % | +32.2 / +27.6 % | 0.285 | 100 % |
| meshopt 64T/128V (meshopt's own RT preset) | +36.0 / +31.8 % | +27.6 / +24.6 % | 0.283 | 100 % |
| meshopt 128T/256V | +42.4 / +36.2 % | +32.7 / +29.6 % | 0.283 | 100 % |
| MO64, fill weight 0 | +33.2 / +30.0 % | +26.2 / +23.1 % | 0.285 | 100 % |
| MO64, fill weight 1 | +37.6 / +29.5 % | +28.3 / +26.4 % | 0.281 | 100 % |
| MO64, only full clusters (min 64) | +34.2 / +32.3 % | +27.7 / +24.7 % | 0.283 | 100 % |
| MO64 without `optimizeMeshlet` | +36.3 / +32.6 % | +27.6 / +24.5 % | 0.283 | 100 % |
| MO64, positions truncated 8 bits | +29.2 / +28.3 % | +24.5 / +22.5 % | 0.285 | 99.83 % (structures 109 -> 85 MB) |
| MO64, truncated 16 bits | +25.7 / +22.9 % | +21.8 / +19.5 % | 0.285 | 74.4 % (changes the picture) |
| MO64, stamps and cluster BLAS FAST_BUILD | +34.8 / +30.0 % | +27.5 / +24.7 % | 0.289 | 100 % |
| MO64, no flags at all | +41.1 / +38.0 % | +31.8 / +28.4 % | 0.286 | 100 % |
| MO64 against the FAST_BUILD uncompacted plain | +25.6 / +26.0 % | +24.1 / +22.5 % | 0.300 | 100 % |
| MO64, camera 0 (across the field) | +22.6 / +13.2 % | +17.5 / +14.7 % | 0.141 | 100 % |
| megageo-2's, camera 0 | +32.3 / +16.0 % | +26.8 / +20.9 % | 0.142 | 100 % |
| **no overlap**: 512 groups of 1 sphere (each its own BLAS in its own 16 m cell), megageo-2's cut | +14.5 / +10.4 % | +15.4 / +13.8 % | 0.160 | 100 % |
| **no overlap, MO64** | **+7.5 / +5.7 %** | **+7.1 / +7.6 %** | 0.161 | 100 % |
| **no overlap, MO64, transform in the instance** (vk_animated_clusters' scene, our FAST_TRACE compacted baseline) | **+4.1 / +3.0 %** | **+6.1 / +5.7 %** | 0.164 | 100 % |
| no overlap, 64 groups of 1, megageo-2's cut | +16.4 / +15.1 % | +11.2 / +10.5 % | 0.121 | 100 % |
| no overlap, 64 groups of 1, MO64 | +10.0 / +10.5 % | +8.9 / +7.4 % | 0.122 | 100 % |

### What closes the gap (the answer to "are we integrating it incorrectly?")

1. **On NVIDIA's own kind of scene the gap is gone, even against our harder baseline.** Copies of one smooth mesh,
   each its own BLAS, side by side on a grid, NVIDIA's clusterizer at 64/64: rays through clusters cost **+3 to
   +8 %** against a FAST_TRACE compacted triangle BLAS. That is NVIDIA's 5 %. The API use, the driver and D3D12 are
   fine.
2. **What makes it +40 to +60 % is the scene, not the integration: many copies OVERLAPPING inside one structure.**
   The plants' groups (16 to 18 copies turned and placed in one 16 m cell, one BLAS a group) are exactly that. A
   triangle BLAS can split overlapping copies triangle by triangle; a BLAS built over clusters cannot split a
   cluster, so a ray that passes close to many copies has to open many clusters' boxes. NVIDIA warns of this in its
   own words: clusters' boxes should "minimize overlap among each other" (vk_animated_clusters README line 8), and
   "The ray tracing performance does suffer from the background mountains overlapping with the primary buildings"
   (vk_lod_clusters `docs/scenes.md` line 61). Putting each copy in its own BLAS moves the overlap into the top
   level, where both kinds pay for it (plain 0.282 -> 0.364 ms), and clusters still pay more (+25 to +30 %).
3. **Our own departures that cost speed, now known:** megageo-2's clusterizer (NVIDIA's meshopt spatial builder is
   10 to 25 points better), the cluster size (64 better than 128 for rays), and the comparison itself (NVIDIA's 5 %
   is against a FAST_BUILD uncompacted BLAS, 23 % slower than ours). Position truncation helps a little (8 bits:
   about 4 points, 22 % less memory, 0.17 % of rays hit a different triangle).
4. Not departures that matter (measured equal): geometry index, OPAQUE flag, index width, per-cluster vertex lists,
   the calls' maxima, FAST_BUILD vs FAST_TRACE on stamps and cluster BLAS, `optimizeMeshlet`, the fill weight.

## (d) The game's plants on NVIDIA's setup (15:25 to 15:33, the card free before every run)

Raw: `%TEMP%\tgb\megageo3-bench\plants.log`. The same six meshes and groups as megageo-2 (32 groups of 16 trees or
18 small plants, 60 wind poses, 10 rounds of rays). Columns: clusters against plain, ray query solid / tested, then
TraceRay solid / tested (the tracer's big passes use TraceRay). "A" = megageo-2's setup (its SAH cut, 128T/256V).
"NV64" = NVIDIA's: meshopt spatial 64T/64V, `optimizeMeshlet`, 32-bit indices into each copy's own vertices,
FAST_TRACE everywhere; in the game's layout (a group a structure). "NV128" = the same at 128T/256V (meshopt's RT
preset for 128). "NV64 split" = NVIDIA's layout too (a copy a structure, its transform in the instance; note that
this layout also slows the PLAIN rays, plain ms in brackets). Every run: 99.998 to 100 % of rays hit the same
triangle at the same distance (the rest: ties on coincident twin triangles, as megageo-2 saw).

| mesh, camera | A | NV64 | NV128 | NV64 split (plain ms: group -> split) |
|---|---|---|---|---|
| beech, across | +79/+71; +75/+64 % | +49/+44; +49/+41 % | +72/+72; +70/+61 % | +42/+37; +37/+33 % (0.282 -> 0.340) |
| beech, down | +83/+79; +76/+71 % | +64/+61; +55/+49 % | +87/+78; +76/+71 % | +40/+36; +37/+34 % (0.475 -> 0.717) |
| pine, across | +118/+93; +97/+83 % | +44/+30; +39/+31 % | +79/+57; +67/+56 % | +40/+30; +32/+29 % (0.443 -> 0.647) |
| pine, down | +131/+125; +118/+108 % | +52/+52; +47/+42 % | +85/+89; +76/+68 % | +36/+32; +34/+31 % (0.636 -> 1.097) |
| grass, across | +4/+1; +18/+17 % | +25/+25; +11/+9 % | -0/-5; +13/+12 % | +26/+27; +10/+10 % |
| grass, down | +41/+20; +38/+33 % | +27/+19; +21/+21 % | +34/+20; +29/+25 % | +23/+19; +17/+16 % |
| grass, packed (5 m) | +48/+37; +39/+35 % | +26/+40; +23/+27 % | | |
| hazel, across | +4/+6; +10/+9 % | +12/+13; +6/+5 % | -1/+1; +7/+6 % | +9/+5; +5/+4 % |
| hazel, down | +7/+8; +13/+14 % | +11/+10; +11/+11 % | +7/+7; +10/+10 % | +5/+4; +7/+7 % |
| hazel, packed | +20/+25; +10/+11 % | +18/+21; +6/+8 % | | |
| fern, across | +2/+1; +7/+7 % | +19/+13; +5/+4 % | +2/+1; +8/+7 % | +18/+12; +6/+5 % |
| fern, down | +6/+5; +12/+12 % | +13/+12; +14/+13 % | +8/+7; +11/+12 % | +13/+10; +13/+11 % |
| fern, packed | +14/+18; +14/+13 % | +18/+23; +10/+11 % | | |

No overlap at all (512 single trees, each its own structure in its own 16 m cell, looking down): beech A +28/+27;
+31/+30 %, beech NV64 +29/+25; +22/+22 %, beech NV64 with the transform in the instance +20/+19; +16/+15 %, pine the
same +26/+24; +24/+21 %. So even a lone tree costs +15 to +26 %: a tree's leaf cards overlap among themselves.
Position truncation (beech, down, NV64, 8 bits): +63/+56; +53/+47 % (from +64/+61; +55/+49), 0.19 % of rays hit
another triangle: no real help on trees.

Sizes and per-frame work (structures plain -> clusters; plain refit -> stamp + cluster BLAS, a frame):

| mesh | A (128T/256V) | NV64 | NV128 |
|---|---|---|---|
| beech (3.89 M tris) | 195 -> 116 MB; 1.11 -> 0.46 ms | 195 -> 140 MB; 1.11 -> 0.68 ms | 195 -> 115 MB; 1.11 -> 0.47 ms |
| pine (2.91 M) | 147 -> 93 MB; 0.90 -> 0.39 ms | 147 -> 110 MB; 0.90 -> 0.55 ms | 147 -> 92 MB; 0.90 -> 0.40 ms |
| grass (0.85 M) | 43 -> 33 MB; 0.35 -> 0.16 ms | 43 -> 40 MB; 0.35 -> 0.25 ms | 43 -> 33 MB; 0.35 -> 0.17 ms |
| hazel (0.34 M) | 17 -> 9.5 MB; 0.12 -> 0.07 ms | 17 -> 11 MB; 0.12 -> 0.09 ms | 17 -> 9.1 MB; 0.12 -> 0.07 ms |
| fern (0.54 M) | 27 -> 16 MB; 0.17 -> 0.09 ms | 27 -> 20 MB; 0.17 -> 0.16 ms | 27 -> 16 MB; 0.17 -> 0.09 ms |

With 32-bit indices (NVIDIA's default) the posed copies need no extra vertices, so these are the whole difference
(megageo-2 added 42 to 91 MB of extra vertices in the town; that cost is gone).

### What it would mean in the game (reckoned, not measured in the game)

The town's plants: 6.4 M triangles in 624 groups (round 20); poses and refits 4.7 ms a frame there with the world
running, of which the refits are about 1.8 to 2.6 ms by this bench's rates (0.28 to 0.41 ms a million triangles).
- NV64 everywhere: refits -> 1.1 to 1.9 ms: **about 0.2 to 0.8 ms a frame saved in play (0.6 to 0.8 for all
  meshes but the fern), nothing frozen**; structures about 34 to 49 bytes a triangle against 51 to 53: **about 20 to
  95 MB less**. Rays: +31 to +55 %
  (TraceRay) inside every tree group a ray enters, +4 to +27 % inside small plants' groups. A town frame that spends
  only 2 ms of its rays inside trees loses about 0.6 to 1.1 ms: the whole saving. A forest frame spends most of its rays in
  trees: a clear loss.
- NV128 for the SMALL plants only (grass, bushes, ferns), trees staying plain: their refits roughly halve, their
  structures shrink 23 to 46 %, their rays cost about -5 to +13 % looking across and +7 to +34 % looking down. Whether
  that pays depends on the small plants' share of the town's 6.4 M triangles and of the rays: unknown here.

### Verdict

Our integration now follows NVIDIA's setup, and on NVIDIA's kind of scene it reproduces NVIDIA's number. On the
game's plants it still does not pay for the trees: they are porous and their groups overlap, which is the case
NVIDIA's own documents say clusters handle worst. What is left, in order of value:
1. A game trial (hidden switch) with the SMALL plants as clusters (NV128, 32-bit indices, geometry index per
   cluster or the cluster ID), the trees plain, read in the town and the forest: the only way to know the share of
   ray time inside plant groups. The pieces and their records are in the bench (stamping, cluster BLAS per group,
   the hit back to the mesh's own triangle through `order[]`).
2. Opacity micromaps inside clusters (the game's leaves have level-6 maps; NVAPI clusters take an OMM array and
   index buffer per template with the ALLOW_OMM flag). Not measured: whether NVAPI's clusters can use the DXR 1.2 OMM
   arrays the tracer builds today, or need NVAPI's own.
3. Not worth a trial now: trees as clusters in any layout (+15 to +55 %), NVIDIA's one-structure-a-copy layout (it
   slows the plain rays 20 to 70 %), position truncation (changes hits, little gain).

## Files (in the copy, against `base20mg`)

- New: `tests/megageo3_cluster_bench.cpp` (megageo-2's bench with NVIDIA's setup as options; MIT; CRLF; /W4 with no
  warnings; it calls meshoptimizer when built with `-DMEGAGEO_MESHOPT`). The shaders are megageo-2's, unchanged.
- This report. Nothing in `native/`, `src/`, CMake, ctest, the plugin or the main tree.
- Build: `scripts\mg3_build.bat` in the session scratch (dxc for the two shaders, `cl` for meshoptimizer's
  `clusterizer.cpp`, `meshletutils.cpp`, `allocator.cpp` and the bench, with `-I` NVAPI R615 and meshoptimizer's
  `src`). Runs: `scripts\mg3_runs.sh <log> <config>` (runs `-GpuCheck` before EVERY run and waits while it fails);
  tables: `scripts\mg3_table.py <log>`. Configs: `mg3_sphere1.cfg`, `mg3_sphere2.cfg`, `mg3_plants.cfg`.
- `python tests/source_checks.py`: 116 run, 1 failure, `test_ingame_harness`, the same one megageo-2 found in the
  pristine base (not mine).

## Sources and licenses

| Source | Used for | License |
|---|---|---|
| nvpro-samples `vk_animated_clusters`, `vk_lod_clusters`, `vk_tessellated_clusters`, `vk_partitioned_tlas`, `nv_cluster_builder`, `nv_cluster_lod_builder` (cloned read-only to `%TEMP%\tgb\megageo3-deps`) | their settings and README text, read and cited; no code copied | Apache-2.0 |
| NVIDIA-RTX `RTXMG` (same folder) | its D3D12 cluster settings, read and cited; no code copied | its LICENSE.txt (NVIDIA) |
| meshoptimizer b233043 (same folder) | `meshopt_buildMeshletsSpatial`, `meshopt_optimizeMeshlet`, `meshopt_buildMeshletsBound`: linked into the bench only, not into the repo | MIT |
| NVAPI R615 (main tree, read only) | declarations and comments cited by line; `nvapi64.lib` linked into the bench | MIT |
| NVIDIA blog posts (two, linked in (a)) | statements on clusters; no numbers for trace speed | - |

If meshoptimizer's clusterizer ever goes into the tracer, it is MIT: its notice goes into `docs/SOURCES.md` and
README's list. No RTXDI or NRD source was read, copied or quoted.

## Workarounds met

None removed, none added. The tracer was not changed.

## How to ask again (about 10 minutes)

Build with `mg3_build.bat`; run, the card free: `megageo3_cluster_bench.exe <mesh.plnt> --groups 32 --members 16
--frames 60 --traces 10 --direct 0 --camera 1 --meshopt 1 --maxtris 64 --maxverts 64 --global 1` (the header lists
every option). A future driver, or DirectX's own clusters, may change the trees' numbers.

## State when stopped

Done (15:45). (a), (b), (c), (d) written. No build or run of mine is still going. Nothing committed, deployed or
written outside the copy and `%TEMP%\tgb\megageo3-*`.
