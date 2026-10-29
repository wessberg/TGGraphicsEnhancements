# megageo-2: RTX Mega Geometry part 2, the plants as clusters stamped from templates (PT-109)

Worker `megageo2` (base `base20mg`). Brief: `docs/agents/briefs/brief-megageo-2.md`. Never the game, a deploy, a
commit, the main tree. 2026-10-11, about 14:20 to 16:40.

## READ THIS FIRST: the answer is NO, so I stopped after step (a)

- **Step (a) is done: a small offline program measured the real thing on this PC** (RTX 5080, driver 616.92) with
  real plant meshes of the game: two trees, three small plants, and a smooth sphere as a control.
- **Clusters make every ray that meets a plant slower. A lot slower.** Trees: 60 to 130 % slower. Small plants
  (grass, a bush, a fern): 10 to 42 % slower. Even the smooth sphere: 39 to 56 % slower. Same answer from a compute
  pass with ray queries and from a ray generation shader with `TraceRay` (the tracer's big passes). NVIDIA's own
  sample says 5 %; this driver does not get near that with our meshes.
- **What clusters do give:** the plants' structures 23 to 50 % smaller, and the per-frame work while the wind blows
  about 45 to 60 % faster. In the town that is roughly 1.1 to 1.5 ms a frame saved while the world runs (nothing
  when it is frozen), and 0 to about 115 MB of video memory net (the structures shrink, the posed copies grow).
- **Why it is a no:** in a frame of 80 to 180 ms, tracing through plants costs far more than their per-frame
  rebuild. Making that tracing 10 to 130 % slower to save about 1 ms is a loss, and in the forest (trees) a big
  one. And the owner's stutter session was about gigabytes of textures; 0 to 115 MB would not have changed it.
- **The picture would be the same**: every ray hit the same triangle at the same distance, to the last bit (a
  handful of ties on coincident twin triangles). This is purely a speed and memory question.
- So (b), (c), (d) were NOT started. Nothing of the tracer was changed. The program stays, so the question can be
  asked again in 10 minutes on a future driver (or on DirectX's own clusters when they come).

## The numbers (clean runs: the card to itself, 16:00 to 16:20)

32 groups (as the plants' groups: one 16 m cell each), 16 trees or 18 small plants a group (the town has 17.8), each
copy turned, scaled, placed and bent by a wind-like sway; 60 frames of poses; clusters of at most 128 triangles cut
from an order of space (least surface area splits). "Rays": the same 1920x1080 camera rays plus one incoherent ray
from each hit; "solid" = every triangle opaque, "tested" = every triangle a candidate that is accepted (as cut-out
leaves are). Times are the card's median over 10 rounds, the two kinds run in turn (plain, clusters, clusters, plain).

| mesh of the game | triangles in all | structures, plain compacted -> clusters | per-frame work, plain refit -> stamp + build | rays through clusters, against plain: ray query solid / tested; TraceRay solid / tested | share of rays that hit a plant |
|---|---|---|---|---|---|
| beech_tree_02_LOD0 (7,603 tris), across the field | 3.89 M | 195.2 -> 116.1 MB (-41 %) | 1.11 -> 0.46 ms (-59 %) | +85 / +69 %; +75 / +64 % | 47 % |
| beech, looking down into the field | | | | +79 / +81 %; +76 / +71 % | 60 % |
| Forest_pine_03_LOD0 (5,685 tris), across | 2.91 M | 146.9 -> 92.9 MB (-37 %) | 0.90 -> 0.39 ms (-56 %) | +124 / +87 %; +97 / +83 % | 71 % |
| pine, looking down | | | | +132 / +125 %; +117 / +108 % | 74 % |
| Grass_02_Cluster_LOD0 (1,476 tris), looking down | 0.85 M | 42.7 -> 32.7 MB (-23 %) | 0.35 -> 0.16 ms (-54 %) | +42 / +20 %; +37 / +34 % | 11 % |
| grass, packed (18 in a 5 m square) | | | | +40 / +38 %; +38 / +35 % | 5 % |
| hazel_00_bush_LOD0 (592 tris), packed | 0.34 M | 17.0 -> 9.5 MB (-44 %) | 0.12 -> 0.07 ms (-44 %) | +19 / +25 %; +10 / +10 % | 3 % |
| foliage_fern_01_3a_LOD0 (940 tris), packed | 0.54 M | 27.2 -> 16.0 MB (-41 %) | 0.17 -> 0.09 ms (-46 %) | +10 / +15 %; +13 / +13 % | 3 % |
| a smooth sphere (8,096 tris, the control), looking down | 4.14 M | 203.3 -> 102.2 MB (-50 %) | 0.99 -> 0.41 ms (-59 %) | +56 / +48 %; +43 / +39 % | 47 % |
| sphere, clusters of 64 triangles and 64 vertices (NVIDIA's usual) | | 203.3 -> 114.2 MB | 1.00 -> 0.50 ms | +148 / +119 %; +97 / +87 % | 47 % |

When few rays hit (the small plants: 3 to 11 %), the rays that miss everything dilute the numbers: for the rays
that actually go through a small plant, the cost is larger than the table says.

Other things measured (earlier runs, Starfield on the card; sizes exact, times only compared inside one run):
- **The cut decides the ray time.** Clusters cut as runs of the mesh's OWN triangle order (megageo-1's plan, no
  re-ordering): rays through the beech 4 times as slow (+298 %), because the game's index order is scattered over
  the whole tree (a cluster's box 6.45 m across of a 17 m tree). Cut from an order of space: +65 to +82 %. Smaller
  clusters help trees a little (32 triangles: +45 %) but cost memory (only -25 %) and hurt the sphere (64/64: +97 %).
- **Clusters built from triangles every frame (no templates)** are smaller (19 to 32 bytes a triangle against 27 to
  40 stamped) and trace a little faster (beech +49 to +63 %, sphere +39 to +45 %), but take 2 to 3 times as long a
  frame as the plain refit (beech 2.22 against 1.11 ms). No way out.
- **NVIDIA's pipeline flag for clusters** (`NvAPI_D3D12_SetCreatePipelineStateOptions` with ENABLE_CLUSTER_SUPPORT,
  `nvapi.h` lines 19642 to 19689) changes nothing measurable in a compute pass (beech: +93 % without, +82 % with);
  the ray generation pipeline was always made with it (the header says traversal without it is undefined).
- **Sizes: the room the driver asks for is the real size.** Asked per template beforehand (GET_SIZES with no
  vertices), the room of a stamped cluster equalled its real size to the byte in every run. megageo-1's fear (the
  driver's whole-call bound, 429.5 MB for the town: no saving) does not hold when the room is asked per template.
  Stamped clusters: 25 to 40 bytes a triangle; plain compacted refittable structures: 51 to 53. The bottom levels
  built from clusters: 0.1 to 3 MB for 32 groups. The templates: tens of KB a mesh, once.

## What it would mean in the game (reckoned, not measured in the game)

The town's plants (megageo-1: 11,303 plants in 636 groups, 6.67 M triangles, 9.5 M vertices):
- Per-frame work while the world runs: plain refit measured here at 0.28 to 0.41 ms a million triangles, clusters
  0.12 to 0.20: about 1.9 to 2.7 ms against 0.8 to 1.3 ms, so **1.1 to 1.5 ms a frame saved in play, 0 frozen**.
- Memory: structures at 52.6 bytes a triangle (compacted, PT-87) are about 350 MB; as clusters 29 to 40 bytes a
  triangle, 195 to 265 MB: 85 to 155 MB less. But clusters need their own vertex lists (below): 11 to 24 % more
  vertices in the posed copies, at about 40 bytes a vertex (positions 12, previous 8, texture coordinates 8,
  colours 4, normals 4, range 4) on 9.5 M vertices: 42 to 91 MB more. **Net 0 to about 115 MB.**
- Rays: every ray that enters a plant group's box pays +10 to +40 % (small plants) or +60 to +130 % (trees) on its
  way through it. If the town's frame spends even 5 ms tracing through plants, 20 % of that is 1 ms: the whole
  saving. The forest's frame is mostly rays through trees.

## Design findings, for whoever asks again (b to d)

1. **A cluster cannot be a window into the mesh's own vertices.** megageo-1 planned clusters as runs of
   consecutive triangles whose corners lie within 256 vertex numbers, so the posed copy needs no change. In the
   game's trees that fails outright: in beech_tree_02_LOD0 27 of 7,603 triangles have corners more than 256 vertex
   numbers apart (up to 1,835), in Forest_pine_03 46 of 5,685; and a 64-triangle window spans 177 numbers of which
   54 are used. So each cluster needs its own vertex list: the group's merged mesh laid out cluster by cluster, a
   shared vertex listed in each cluster that uses it (+11 to +24 % vertices with a spatial cut, +4 % without), the
   pose pass (pt_sway steps 3 to 5) reading each listed vertex's source through one more table a mesh.
2. **The triangles must be re-ordered in space** (else rays 4 times as slow). Then a cluster's triangles are not
   consecutive in the mesh's own order: the group's merged index buffer is written in cluster order (fetch_hit stays
   as it is: a cluster's own record, first index + 3 x PrimitiveIndex), and every per-triangle table that follows
   the mesh's order must be permuted the same way, above all the micromaps' index buffer (PT-110).
3. **Each stamped cluster's own record works**: geometry index = member x clusters + cluster (template base + the
   stamp's offset); a hit reports it through the plain `GeometryIndex()`, and `PrimitiveIndex()` is the triangle
   inside the cluster in the template's order. Checked: every ray of every run mapped back to the plain path's
   triangle. With `TraceRay` the hit group must not depend on the geometry index (the bench passes 0 as its
   multiplier); the tracer's passes would have to do the same.

## How to ask again (about 10 minutes)

1. A mesh: `python -I export_mesh.py <UnityPy folder> <bundle> <mesh name> <out.plnt>` (my scratch folder holds it,
   see below; it reads the game's asset bundles, read only). The six meshes used are in the scratch folder.
2. Build (VS 2022 x64 environment): the three lines in the header of `tests/megageo_cluster_bench.cpp` (two `dxc`
   calls and one `cl`), with `-I` the main tree's `NVAPI-R615`. Mine is `scripts\build_bench.bat` in the scratch
   folder (below); it writes into `%TEMP%\tgb\megageo2-bench`.
3. Run, the card free (`Run-PtHarness.ps1 -GpuCheck` exit 0, the game closed):
   `megageo_cluster_bench.exe <mesh.plnt> --groups 32 --members 16 --frames 60 --traces 10 --spatial 2 --camera 1`
   (options: `--maxtris`, `--maxverts`, `--spatial 0|1|2`, `--direct 0|1`, `--rt 0|1`, `--pso-flag 0|1`,
   `--bounds 0|1`, `--camera 0|1`, `--spread m`). It prints sizes, times, rays and whether every ray matched.
   A run takes 20 to 60 s and about 1 GB of video memory.

## Files (against `base20mg`)

New, all in the copy: `tests/megageo_cluster_bench.cpp` (the program), `tests/megageo_cluster_bench.hlsl` (its
compute rays), `tests/megageo_cluster_bench_rt.hlsl` (its ray generation rays), this report. All CRLF. No CMake
target, no ctest entry, nothing in `native/` or `src/`: no export, record, FrameConstants row or option bit added.
Whether to merge the program at all is the coordinator's call; it is useful only to ask the question again.

Checks: the program builds with 0 warnings (`/W4`). `python tests/source_checks.py`: 116 run, 1 failure,
`test_ingame_harness` (a recipe's first step sets `Mode` On); it fails the same in the pristine base
(`tests/ingame/scenarios.json` and `tests/source_checks.py` are byte-identical to `base20mg`), so it is not mine.
No whole `tgpt_gpu_test`, no ctest, no game, no deploy, no commit. GPU runs only with `-GpuCheck` at exit 0; the
first ones (14:50 to 15:45) had Starfield on the card (the check allows offline tests beside it), so only their
sizes and in-run comparisons are quoted; the table above is from runs with the card free.

## Sources and their licenses

| Source | What was used | License |
|---|---|---|
| NVAPI R615 (`NVAPI-R615\` in the main tree, read only) | `nvapi.h` declarations: cluster operation types, modes, flags and alignments (lines 20853 to 20928), the input and argument structures (20930 to 21166), the execute call (21204), the pipeline flags (19642 to 19689); linked `amd64\nvapi64.lib` into the program only | MIT |
| The game's asset bundles (read only) | six meshes' positions and indices, exported to the scratch folder; nothing of them is in the repo | the game's |
| UnityPy 1.25.4 (an earlier session's scratch folder, `...\3bcc2cf1-...\scratchpad\pytools-unity`, used in place) | reading the bundles | MIT |
| NVIDIA's `vk_animated_clusters` README (via megageo-1) | the 5 % ray cost it reports, for comparison only | Apache-2.0 |

No RTXDI or NRD source was read, copied or quoted. No sample code was copied.

## Workarounds met

None removed, none added. The tracer was not changed.

## My scratch

- Build: `%TEMP%\tgb\megageo2-bench` (the exe, the compiled shader headers, `clean-runs.log` and `dense-runs.log`:
  the raw output of the table's runs).
- Session scratch (`%TEMP%\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\201dd466-...\scratchpad`):
  `scripts\` (`scan_plants.py`: lists the game's 546 plant meshes with counts; `export_mesh.py`; `spans.py`;
  `sphere.py`; `build_bench.bat`; `clean_runs.sh`; the patch scripts), `meshes\` (the six `.plnt` files, the sphere,
  `plants.tsv`).

## State when stopped

Done. Step (a) answered no; (b) to (d) not started, as the brief says. No build or run of mine is still going.
