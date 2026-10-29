<!-- A read-only review by the pt-max-reviewer agent, 2026-10-10 (PT-110; brief: docs/agents/briefs/brief-compat-1.md). Filed by the coordinator as it was written; nothing in it was built or run by the reviewer. -->
# compat-1: is the tracer built for Shader Execution Reordering, Opacity Micromaps, RTXDI and RTX Mega Geometry? (PT-110)

A read-only review, 2026-10-10. Brief: `docs/agents/briefs/brief-compat-1.md`, and the coordinator's addition during
the job (RTX Mega Geometry as a fourth, shorter section: section 6). Nothing was built, run or changed: no build, no
test, no game. One file written: this one. The vendor documents were fetched into `<scratch>\compat1-refs` to search
them; nothing of them is copied here. RTXDI's sources and documents are described in my own words only.

## 0. In short

The five that would give players the most, first (each has its entry below):

1. **R1. RTXDI's reuse across frames runs inside the bounced light's reordered pass, after the regrouping.**
   NVIDIA's own sample keeps it a pass of its own, and NVIDIA's reordering paper warns of exactly this. It fits
   every number we have (floor 14.4 ms in the tavern, 4.0 at the beach, 2.5 in the reflections' pass). My guess:
   10 to 30 ms of the tavern's and the town's frame. This is `ser1`'s pass: hand it to them. Proof: steps 15 to 18
   of the recipe `measure2`, which exist and have never run.
2. **S5. The water's path regroups once for a loop with no end.** It needs a regrouping a turn, in a pass whose
   threads carry the path and nothing else. The beach's 140 ms. Gain not known; `perf-1.md` reckons a third if
   the waiting is the cost. Proof: `water-day@regroup` (built, never run in the game).
3. **M1 + M2. No plant in the wind's pose has a micromap (6.4 million triangles in a town), and no leaf has one
   opaque micro-triangle for any ray.** The DirectX specification allows micromaps on refitted geometry. Gain not
   known; one step with the plants' cut-outs made solid bounds it. C++ only for M1.
4. **S1 + S2 + S3. The regrouping tells the driver nothing about the hit, asks for 16 hint bits where NVIDIA
   documents 8 with a hit object, and carries the whole fetched hit (68 numbers) across.** Compiled, our call IS
   the hit-object form with an empty hit object (counted in the listing). Gain: a few ms in the reflections, and
   perhaps the 1.2 GB of video memory PT-87 suspects. Proof: one log line (`GetShaderStackSize`), one run with
   the key in 8 bits.
5. **M3 + M4. Characters: every triangle of a character with hair is handed to the shader's test, and hair has no
   micromap though a "bare texels only" map would be exact.** The tavern: 547,286 such triangles. Proof: steps 3
   and 12 of `measure2` (every triangle solid) bound it.

RTX Mega Geometry (section 6): by NVIDIA's and Microsoft's own documents it makes building cheaper and memory
smaller, and rays through clusters a little SLOWER (NVIDIA's sample: 5 %). Where it can give more than
`megageo-1.md`'s estimate: video memory (about 475 MB in a town instead of 220, if the plants' merged copies go
too: a guess from byte counts), and the characters' rays (not known: a two-line trial measures it). For frame
time on plants I expect about nothing, and it can cost.

Three things I COUNTED today that were not written down anywhere:

- The reflections' reordered pass is one shader of 4.27 MB of DXIL with 153 ray queries in it; the bounced
  light's 3.13 MB with 117 (`build\native\generated\pathtracing\pt_specular_ser.h`, `pt_gi_ser.h`).
- Our one regrouping line in `pt_gi.hlsl` is THREE regrouping calls in the compiled pass (listing lines 1294,
  4417, 4421); the reflections' pass has one (line 30886).
- DXC turns `dx::MaybeReorderThread(key, 16)` into "make an empty hit object, then regroup by hit object and a
  16-bit hint" (`HitObject_MakeNop` at lines 1293 and 4410 of the bounced light's listing).

## 1. How to read this

- MEASURED: a number of a game run, with its run. None is mine: they are PT-99's, `measure-run.md`'s,
  `perf-1.md`'s, `megageo-1.md`'s and the coordinator's.
- READ: from our code (file and line; lines are the main tree's as of 10:31 on 10-10) or a vendor document (tag
  below).
- COUNTED: by me, from the compiled passes' listings in `build\native\generated\pathtracing` (built 10:39 to
  10:46 on 10-10; the numbers move with every build).
- GUESS: my reckoning. It always says so. Nothing in this file was profiled.

Sources:

| Tag | What | Where |
|---|---|---|
| DXR | Microsoft, DirectX Raytracing functional spec, v1.48 of 2026-09-08 | https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html |
| DXR2 | Microsoft, the same, part 2 (clusters, partitioned top level), v0.38, a preview | https://microsoft.github.io/DirectX-Specs/d3d/Raytracing2.html |
| SER-HLSL | Microsoft, HLSL proposal 0027, Shader Execution Reordering (completed) | https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md |
| SER-NV | NVIDIA, "Shader Execution Reordering" whitepaper (API, best practices) | https://d29g4g2dyqv443.cloudfront.net/sites/default/files/akamai/gameworks/ser-whitepaper.pdf |
| OMM-NV | NVIDIA, Opacity Micro-Map SDK, integration guide | https://github.com/NVIDIA-RTX/OMM/blob/main/docs/integration_guide.md |
| RTXDI-I | NVIDIA, RTXDI: Integration, ReSTIR GI, Noise and Bias guides | https://github.com/NVIDIA-RTX/RTXDI/tree/main/Doc (`Integration.md`, `RestirGI.md`, `NoiseAndBias.md`) |
| RTXDI-L | NVIDIA, RTXDI library 3.1, the local copy (never quoted) | `RTXDI-Library-3.1\Source\ReSTIRDI.cpp`, `ReSTIRGI.cpp`; `Include\Rtxdi\DI\`, `GI\` |
| MG-A | NVIDIA, `vk_animated_clusters` README (clusters, templates, measured table) | https://github.com/nvpro-samples/vk_animated_clusters |
| MG-P | NVIDIA, `vk_partitioned_tlas` README | https://github.com/nvpro-samples/vk_partitioned_tlas |
| MG-R | NVIDIA, RTX Mega Geometry SDK README | https://github.com/NVIDIA-RTX/RTXMG |

Not read: NVIDIA's blog posts on reordering, RTXDI's sample application's shader files (only its guides, which
name them), the OMM SDK's license.

## 2. Shader Execution Reordering

### 2.1 What the documents want

1. Only a ray generation shader may regroup. Three forms: by a hit object, by a number, by both (SER-HLSL,
   "MaybeReorderThread").
2. NVIDIA's sort order: first the hit's shader, then the number (its highest bit counts most), then WHERE THE HIT
   LIES in the scene. The number alone may have 16 bits; with a hit object 8. Without a hit object the driver
   tries to keep threads near their place on screen (SER-NV, API reference). Microsoft's text: an implementation
   may drop any number of the lowest bits; ask for as few bits as represent the values; the same count at a call
   site (SER-HLSL, "MaybeReorderThread with coherence hint").
3. What a thread carries: every value made before the call and used after it goes through memory. "The smaller
   that state is, the lower the overhead" (SER-NV, "Optimizing live state"). Pack it, compress it, move code.
   Microsoft shows a normal packed across the call for this reason (SER-HLSL, "Live state optimization").
   NVIDIA's Nsight Graphics shows the bytes at each call and the threads active in a group.
4. Where it pays: costly hit shading with rays that scatter; reflections; path loops, with "this path is about to
   end" in the key. Shading inside the ray generation shader counts as well as hit shaders. Where it does not:
   camera rays, shadow rays, cheap work. And: regrouping gives up the threads' places on screen, which hurts
   reading the G-buffer and writing the outputs, and "can turn reordering into a net loss when the rest of the
   workload is particularly cheap" (SER-NV, "When to use (and when not to use) reordering").
5. Trace, regroup, shade called back to back can be ONE reorder point to the driver, the cheapest form
   (SER-HLSL, "Reorder points", "Back-to-back calls"). A ray query is no reorder point at all (DXR, "Wave
   intrinsics"). So with ray queries every regrouping is an extra stop that a `TraceRay` would have made anyway.
6. A hit object can be made from a ray query's hit: `HitObject::FromRayQuery`. NVIDIA names it "an easy path to
   integrate reordering into existing applications that use RayQuery": keep shading in the ray generation shader
   and gain coherent data, or give the hit object a shader table index and run a hit shader (SER-NV, "Executing
   closesthit shading for rays cast via RayQuery"; SER-HLSL, "HitObject::FromRayQuery").
7. Memory written before a reorder point and read after it needs `[reordercoherent]` and a barrier (SER-HLSL,
   "Memory coherence and visibility").
8. Ray queries: any number may be alive, at an unknown and "relatively high" cost in registers, "which can limit
   how many shaders can run in parallel" (DXR, "RayQuery").
9. The driver reserves stack memory for a pipeline's threads when the command list is recorded;
   `GetShaderStackSize` tells one shader's need in bytes (DXR, "Pipeline stack"). The recursion depth should be
   "as low as absolutely necessary"; 0 means no ray is traced through the pipeline (DXR, "Ray recursion limit").

### 2.2 What the tracer does

| Pass | Today | What it does | By 2.1 point 4 |
|---|---|---|---|
| `pt_prepare` | compute | the camera's rays (every triangle a candidate: M8), the first hit's material, hair | no: camera rays are coherent |
| `pt_gi` | ray generation, 1 regrouping line (3 compiled) | bounce paths, THEN RTXDI's reuse across frames | yes, the paths only (R1) |
| `pt_gi_boil`, `pt_gi_spatial` | compute | the boiling filter; reuse from neighbours, 1 visibility ray | no |
| `pt_di`, `pt_di_spatial` | compute | candidates, reuse, 2 shadow rays | no: shadow rays |
| `pt_walk` | compute, whole screen | subsurface walk, then a bounce path with `shade_path` (2.3 MB, 106 ray queries) | a candidate, small prize (S8) |
| `pt_specular` | ray generation, 1 regrouping | reflection ray, `shade_seen`: glass, the water's whole path | yes; the water's path wants more (S5) |
| `pt_caustics` | compute over the water's tiles | 2 million corners: shadow rays, two rays on | divergent, small prize (S8) |
| the rest | compute | no rays | no |

READ:
- The two reordered passes are each ONE ray generation shader with everything inline: `tracer.cpp:864-878`
  (one export, no hit group, shader config {4, 8}, recursion depth 1), `CMakeLists.txt:285-294`,
  `tracer.cpp:942-945`. The call: `pt_common.hlsli:72`, `dx::MaybeReorderThread(uint(key) & 0xFFFFu, 16)`. The
  key: `pt_common.hlsli:86`: bit 15 "feeds the cache", bits 0 to 14 the material's INDEX, `0xFFFF` nothing to
  shade.
- The plain compute twins of the two passes are made only with `TGPT_TEST_PLAIN_PASSES` or where nothing
  reorders (`tracer.cpp:921-927`). So in play nothing is "kept beside": no video memory, no start-up time goes
  to them. The first start's 4 to 7 minutes are the two reordered passes themselves.

COUNTED (listings of 10:39 to 10:46 on 10-10):

| Pass | DXIL | ray queries | regrouping calls |
|---|---|---|---|
| `pt_specular_ser.h` | 4.27 MB | 153 | 1 (line 30886) |
| `pt_gi_ser.h` | 3.13 MB | 117 | 3 (lines 1294, 4417, 4421) |
| `pt_gi_dxr12.h` (plain twin) | 3.11 MB | 117 | 0 |
| `pt_prepare_dxr12.h` | 2.80 MB | 25 | |
| `pt_walk_dxr12.h` | 2.37 MB | 106 | |
| `pt_di_spatial_dxr12.h` | 0.40 MB | 30 | |

MEASURED (PT-99, run `20261010-100720`; `measure-run.md`, run `20261009-193722`), ms:

| | reordered | plain | with no ray, reordered |
|---|---|---|---|
| bounced light, the tavern | 56.0 | 13.9 | 14.4 |
| bounced light, the beach | 42.6 | 70.8 | 4.0 |
| reflections, the tavern | 11.6 | 15.0 | 2.5 |
| reflections, the beach | 140.2 | 317.7 | 102.9 (its own ray left out) |

My reading of that table (a GUESS that fits every row): the regrouping CALL is cheap. The reflections' pass
makes it for every pixel and its floor is 2.5 ms. What costs is what a pass does AFTER it by screen position.
The reflections' pass does almost nothing there (one read, two writes). The bounced light's pass runs RTXDI's
whole reuse across frames there, for every valid pixel: the tavern is all valid pixels (14.4 ms), the beach is
mostly water and sky, which leave the pass at its top (4.0 ms). The plain pass does ALL its work in 13.9 ms. So
its reordered twin's floor is not work, it is work done in the wrong place. Entry R1.

### 2.3 The gaps

**S1. The key: 16 bits, the material's index in the low ones, and an empty hit object.**
- We do: `pt_common.hlsli:72` and `:86`. COUNTED: DXC compiles the number-only form into `HitObject_MakeNop`
  followed by the hit-object form with a 16-bit hint (`pt_gi_ser.h` lines 1293-1294, 4410-4421). That is by the
  book: there is one operation, and it always takes a hit object (SER-HLSL, "DXIL specification",
  "MaybeReorderThread"). Our one line
  at `pt_gi.hlsl:83` became three calls: one for a ray that points under the surface (constant key `0xFFFF`),
  one for a hit, one for a miss.
- It wants: the highest bits to say what matters most; as few bits as possible (2.1 point 2). NVIDIA documents
  8 hint bits with a hit object, 16 without. Which limit the driver applies to an EMPTY hit object is not
  documented. If it is 8, the driver sees bit 15 ("feeds") and bits 8 to 14 of the material's index: a scene
  with under 256 materials is then sorted by "feeds" alone. That would fit three things we saw: the "feeds" bit
  gave a gain (reflections 11.4 to 9.9 ms, the highest bit), leaving the call out gave none, and PROJECT-CONTEXT
  9.13 says reordering was "within noise" in the harness. NOT PROVEN: a guess that fits.
- Also: threads with nothing to shade take part in the regrouping (key `0xFFFF`: `pt_gi.hlsl:83`,
  `pt_specular.hlsl:126`). They have no hit shading to gain from it. A thread that does not reach the call stays
  where it is (SER-HLSL, "Divergence behavior").
- It costs: not measurable from what is there.
- The run: `tavern-room` and `metal`, no denoiser, two steps: as now, and the key in 8 bits with its top bits
  meaningful (nothing to shade / feeds / water / glass or subsurface / the material's program kind, or a hash
  of the material). Read "reflections" and "bounced light" in the 10 s lines. If the 8-bit key is faster, the
  driver was dropping our low bits.
- Size: 5 lines in `pt_common.hlsli`; both reordered passes compile anew (15 minutes of dxc, a first start of 4
  to 7 minutes). Risk: none to the picture (the key changes who runs beside whom, never a number).
- `ser1` has "fewer hint bits and another key" on its list for the bounced light: this is the same change for
  both passes, and the listing's evidence for it.

**S2. The driver is told nothing about the hit, and the hit is fetched before the regrouping and carried
across.**
- We do: `trace_from_surface` fetches the whole hit at `pt_common.hlsli:2806` (`fetch_hit`, `:1559-1655`:
  positions, normals, two sets of texture coordinates, their footprints, the transform, vertex colours). The
  struct is 68 numbers, 272 bytes (`pt_common.hlsli:1150-1176`). Then the regrouping (`pt_specular.hlsl:126`,
  `pt_gi.hlsl:83`). Then the shading reads it.
- It wants: a hit object from the ray query (2.1 point 6). The driver then sorts hits from misses by itself,
  and within a key by where the hit lies in the scene, which is what the shading's reads of vertices and
  textures need. What identifies a hit is 7 numbers (instance, submesh, triangle, two barycentrics, distance,
  side); a hit object holds them and hands them back after the call. `fetch_hit` then runs AFTER the
  regrouping, among threads that read the same meshes.
- It costs: 2 x 272 bytes of memory traffic a thread for the hit alone (written, read back): a GUESS that this
  is over half of what a thread carries (S3 measures it).
- The run: the same two scenes; a variant where `trace_from_surface` hands back a hit object, the regrouping
  takes it with an 8-bit hint, and `fetch_hit` follows. "reflections", "bounced light".
- Size: 40 to 60 lines (`pt_common.hlsli`, the two passes); 1 to 2 days with the game. Risk: a new driver path
  (`FromRayQuery`); the tree already holds one driver hang around a second ray query
  (`pt_common.hlsli:2561-2568`), so the first start of that build is to be watched.

**S3. Nobody knows what a thread carries, and the driver would say.**
- We do: nothing. `GetShaderStackSize` and `GetPipelineStackSize` are called nowhere (`native\pathtracing\src`:
  no match).
- It wants: 2.1 points 3 and 9. `GetShaderStackSize(L"gi")` and `(L"specular")` on the two state objects are
  bytes a thread, as the driver counts them. By reading, the reflections' pass carries about 100 numbers across
  its call (the hit's 68; the surface's fields used after; the glass's weights and light, alive for every
  thread though only glass pixels use them; the ray's weight, origin and direction; the globals of the cone and
  the path): about 400 bytes. A GUESS until the line is in the log.
- It costs: time (above), and perhaps video memory. PT-87 measured +1,935 MB "when the passes first run" in the
  game, 616 of it "the moment the bounced light's pass was first recorded", against 0.66 GB offline where the
  reordered passes do not run. The DirectX spec says exactly that: stack memory is reserved when the command
  list is recorded, times the threads in flight. 1.2 GB over 2.09 million pixels and two passes is about 290
  bytes a thread a pass: the same size as my reading. NOT PROVEN.
- The run: any start: read the new line. Then `tavern-room@no-ser-memory` (it exists, NOT RUN) for the 1.2 GB.
- Size: 6 lines in `tracer.cpp` `adopt_passes` (`:994-999` already has both property interfaces). No pass
  compiles. Risk: none. It is the yardstick for S2, S5, R1: "bytes a thread" before and after each.

**S4. The reflections' pass writes a pixel's guide before the regrouping and again after it.**
- We do: `pt_specular.hlsl:14` stores the default (`seenGuide[p] = 0`), the regrouping is at `:126`, the real
  value is stored at `:137`, `:144` or `:189`. After the call it also reads and rewrites `specular[p]`
  (`:185-187`).
- It wants: 2.1 point 7 gives rules for a store on one side of a reorder point and an access on the other. Two
  stores to one texel from two places on the card are not covered by any rule I found; if the first lands
  late, the guide reads "nothing seen" and the reflections' filter (PT-80) treats the pixel as empty.
- It costs: not known. NOT seen as a defect: a hazard by the spec's own section. Not time.
- The run: none needed. Size: 3 lines: store the default only on the paths that shade nothing. Risk: none.

**S5. The water's path: one regrouping for a loop with no end.**
- We do: a water pixel's whole light is `water_path` (`pt_water_path.hlsli:48-249`), a loop of turns: scatter in
  the water and ask the sun through the waves, or meet the surface, or meet the bed and shade it. It runs
  inside `shade_seen` after the pass's one regrouping (`pt_specular.hlsl:131`, `pt_common.hlsli:4653-4660`).
  Every thread of a group waits for the group's longest path and pays for every branch another thread takes
  (`perf-1.md` section 1). The per-turn regrouping exists on trial (`pt_water_path.hlsli:17-21, 66-67`, CMake
  `TGPT_REGROUP_WATER`) and has never run in the game: offline the driver passed 8 GB making the pass.
- It wants: 2.1 point 4: a path loop is where reordering shines, with the regrouping in the loop and "about to
  end" in the key (SER-HLSL examples 2 and 3). And point 3: the loop's own state is small (origin, direction,
  weight, radiance, the medium, a word of flags, two counters, the random number: 17 numbers, 68 bytes), but in
  `pt_specular` the regrouping inside the loop also carries everything the glass and the reflection branches
  will want after the loop. And the pass is 4.27 MB with 153 ray queries, which is what the driver chokes on.
- A design that fits (a GUESS, to be measured in steps): the camera's water pixels in a ray generation pass of
  their own, which holds the path and nothing else; the turn's hit as a hit object (S2); the key: scatter /
  surface / bed by program kind / nothing, and "ends after this turn". The bed's shading stays inline.
- It costs: the beach's reflections are 140 ms; 103 of them are the water the camera sees (`measure-run.md`,
  step 3). What regrouping a turn brings is NOT KNOWN. `perf-1.md`: a third or more if the waiting is the cost.
- The run: `water-day@regroup` on a build configured with `-DTGPT_REGROUP_WATER=ON`: its steps
  `3-none-regroup-turns` (`Speed trial` 2) and `4-none-regroup-patches` (4) against `2-none-as-now`. Watch the game's memory during that build's first start (PT-96).
- Size: the trial: no new code, one configure switch, two passes compile. The pass of its own: 2 to 3 days.
  Risk: the driver's time and memory for the pass (the reason to make it small first).

**S6. One shader a pass, everything inline: 4.27 and 3.13 MB, 153 and 117 ray queries.**
- We do: every pass that shades a hit holds its own whole copy of the material code, the decals, the lamps, the
  water's path, and of the alpha test at every ray query (table in 2.2). Four big passes, ten variants.
- It wants: the API's own shape is many small shaders compiled apart and joined by a shader table. A hit shader
  or a callable shader for "shade this vertex" would be compiled once for all ray generation shaders of one
  state object. Microsoft names duplicated code as a cost of its own: more code generated, pressure on the
  instruction cache, longer compiles (SER-HLSL, "Motivation" and "Separation of MaybeReorderThread and
  HitObject::Invoke"). And 2.1 point 8 on ray queries.
- It costs: the first start after a new build: 217 to 340 s for each of the two passes, 628 s once (PT-99);
  the driver's memory meanwhile (33 GB once, PT-96). In frame time: not measurable from what is there.
- The run: none proves it cheaply. S3's line and the first start's time of a build with R1 (which takes the
  reuse and its dead visibility rays out of the pass) are the first evidence.
- Size: a week or more, and every material path moves. NOT a first step. Keep ray queries for the traversal
  (the tracer's rules for passing things need them); move only the shading.

**S7. The recursion depth is 1 where no ray is traced through the pipeline.**
- We do: `tracer.cpp:871`, `D3D12_RAYTRACING_PIPELINE_CONFIG pipeline{1}`. The passes trace with ray queries
  alone. It wants: 0 (2.1 point 9). It costs: not known, perhaps nothing. Size: one character, with S1's build
  (the driver makes both passes anew). If S2 later calls `HitObject::Invoke`, it goes back to 1.

**S8. `pt_walk` and `pt_caustics` have no reordered variant.**
- `pt_walk` (`pt_walk.hlsl:69-112`): a compute pass over the whole screen; only subsurface pixels work; each
  walks, lights its exit and follows a bounce path with `shade_path`. Divergent, but its pixels lie together on
  screen (faces, candles), and its time is inside "direct light" (4.7 ms in the tavern with everything else).
- `pt_caustics`: 4 ms in every scene with water in the world, 7 to 9.5 at the beach. Its threads differ by
  "lit or not".
- By 2.1 point 4 both could gain; both are small. After R1 and S5. No run proposed.

## 3. Opacity Micromaps

### 3.1 What the documents want

1. A micromap marks each small piece of a triangle opaque, transparent or unknown. Opaque and transparent are
   settled by the hardware. Only unknown reaches the shader; with a ray query, as a candidate (DXR, "Opacity
   micromaps"). The gain follows the share of pieces that are known: C = (T + O) / all (OMM-NV, "Sample
   output"). Even a scene with moderate cut-outs lost about 30 % of its tracing cost in NVIDIA's sample
   (OMM-NV, "Opacity Micro-Map fundamentals").
2. The alpha test should have the standard form: one texture, one fixed mip, one cutoff. Anything else ("Type
   3") may not fit, and NVIDIA's first advice is to make the shader simpler: "a good general strategy to improve
   raytracing performance" (OMM-NV, "Alpha Testing Shaders"). No mip chosen per ray.
3. Subdivision level by how many texels a piece covers, per triangle, up to 12; a single level for all "may not
   be effective on a mesh with varying triangle sizes". Pieces with the same texture coordinates share one map;
   uniform triangles take special indices (OMM-NV, "Subdivision Level", "Reuse", "Algorithm Overview").
4. A structure with micromaps may be refitted: positions change, the links to the maps are passed again
   unchanged, and the index buffer "must match that of the original build" (DXR,
   `D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE`, `..._PERFORM_UPDATE`,
   `D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC`). So a mesh that only moves keeps its maps.
5. `ALLOW_DISABLE_OMMS`: "may result in some reductions in traversal performance". `ALLOW_OMM_LINKAGE_UPDATE`:
   larger and slower too. The micromap flag on a ray query "should not be set if there are no OMMs present"
   (DXR, build flags, `RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS`).
6. Flags act AFTER the map: a forced-opaque ray cannot bring back a transparent piece; a forced-non-opaque ray
   turns every opaque piece back into a candidate (DXR, "Opacity micromaps").
7. Four states can be read as two (`RAY_FLAG_FORCE_OMM_2_STATE`, or per instance): unknown becomes its nearer
   known state, no shader at all. The spec offers it for effects that need no exact edge, "soft shadows" among
   them. It changes the picture at the size of a piece.
8. To see what cut-outs cost: trace once with everything forced opaque (OMM-NV, "Integration guide checklist").

### 3.2 What the tracer does

READ:
- Every ray query of the DirectX Raytracing 1.2 passes allows micromaps (`pt_common.hlsli:67`). Good.
- The bake is our own (`pt_omm_bake.hlsl`): 4 states, level 4 for every triangle (256 pieces, 64 bytes:
  `scene.hpp:321`, `pt_omm_bake.hlsl:25`), one map a triangle, special indices for uniform ones (`:276-277`). A
  piece is known only if every texel its bounding box can reach agrees (`:117-171`).
- Who gets one: a still mesh with a cut-out material, when no submesh of it is hair, glass or camera-only
  (`scene.cpp:2158-2165`), its textures are there (`:2266-2273`) and the 256 MB budget of input holds (`:47`,
  `:2275`). The first instance's materials decide; an instance with others goes without (`:2209-2220`).
- Who gets none: plants in the wind's pose (`scene.cpp:1904-1906`), swaying cloth's copies (`:1738-1742`),
  characters (`:1976-1982`), hair, glass.
- MEASURED (the tracer's log, 10-10): micromaps on 175 meshes (1,953 instances) in a town; 46 meshes, 63,022
  triangles, 173 instances, 15 MB in a tavern; and there "tested by the shader: 49 instances, every triangle
  (547,286 triangles, 109,800 of them of submeshes that are no cut-out)". In a town 11,303 plants are posed.

### 3.3 The gaps

**M0. We do not know our coverage, nor what the maps bring.**
- We do: the bake counts nothing. `TGPT_OPTION_NO_MICROMAPS` exists (`tgpt_api.h:60`, `tracer.cpp:2709`) but
  only the offline test can set it: no setting of the plugin does.
- It wants: 3.1 points 1 and 8: C is the number to steer by.
- The run: (a) a hidden `Advanced` setting that sets option bit 32: plugin only, no pass compiles. `forest`
  and `metal`, two steps, maps on and off: every stage's time says what today's maps bring. (b) A count of
  known and unknown pieces in the bake (20 lines, the small pass), as a log line: C per scene.
- Size: half a day. Risk: none. This comes before M7.

**M1. No plant in the wind's pose has a micromap.**
- We do: a group's instance is built with plain triangles (`scene.cpp:1066-1075`, `1231-1240`), and the comment
  says why: "no micromaps: they are made for a still mesh" (`:1904`). And a grouped plant's own mesh is never
  even asked for maps: the still path, the only caller of `omm_instance`, skips grouped instances (`:1631`).
- It wants: 3.1 point 4. A map lives in a triangle's own barycentric space and reads texture coordinates; the
  wind changes neither. A group copies each member's triangles in the member's own order
  (`scene.cpp:1036-1045`), one submesh a member's submesh. So each of its submeshes can link the member mesh's
  map array with that mesh's own index buffer. The maps are made once a MESH, not once a plant: a few MB.
- It costs: 6.4 million triangles in a town (MEASURED count), every cut-out one of them tested by the shader
  for every ray that crosses it: three record reads, three indices, three texture coordinates, one texture
  read (`pt_common.hlsli:1469-1540`). In ms: not measurable from what is there.
- The run that bounds it: a C++-only trial that builds the groups with every submesh opaque for one step
  ("plants solid": the picture is WRONG in that step, it is a measurement, as OMM-NV's checklist advises).
  `metal` and `forest`, the world running. Every stage's time against "as now" is the most M1 and M2 together
  can give. Then M1 itself, on and off.
- Size: `scene.cpp` only, about 150 lines: ask for the members' meshes' maps; keep each mapped mesh's index
  buffer (today released after the build, `:2447`; 4 bytes a triangle); build a group's cut-out submeshes as
  micromap triangles; build the group anew once when a member's maps arrive; pass the same links at every
  refit. No pass compiles. 1 to 2 days. Risk: the driver's refit with maps (allowed by the spec, new to us);
  watch leaves and grass in the frozen pair.
- With M2 not done, a leaf's map settles only its bare pieces: M1 alone is about half of the prize (a GUESS).

**M2. No leaf, blade or thin cloth has one opaque piece, for any ray.**
- We do: `pt_omm_bake.hlsl:265` turns every opaque piece of a translucent or thin-cloth material into unknown,
  because a LIGHT's ray passes a leaf with a chance of one half inside the candidate loop
  (`pt_common.hlsli:1507-1508`). The map is one for all rays. So a bounce ray, a reflection ray and the
  camera's ray, for which a covered leaf is simply solid, pay the shader's test on every covered piece too.
  (Thin cloth also lets bounce rays through, `:1521`: it keeps its rule; this entry is about leaves and grass.)
- It wants: 3.1 point 2: this is a "Type 3" test. The chance belongs outside the traversal. One way that keeps
  the mean: a light's ray takes the nearest hit with the map deciding; where that hit is a leaf, the coin (or
  PT-93's product) is taken there, and the ray goes on from just behind it. A ray through n leaves makes under
  two queries on average with the coin. Pieces that are truly mixed keep their test as now.
- It costs: with M1, the larger half of the plants' tests (a GUESS: covered and bare are about even on a leaf
  card). Not measurable from what is there; M1's "plants solid" step bounds both.
- The run: after M1: `forest`, the shadow under trees, 16 undenoised frames a step: the mean must be the same
  picture (leaf shadows at 0.5 to the n), the stages faster.
- Size: `transmittance`, `occluded_any`, `occluded_from_surface_rays`, `rtxdi_visible` and the bake's line
  265: a week with its checks. Risk: medium: it is the light's path through every tree.

**M3. Characters: the whole instance is handed to the shader when one submesh is a cut-out; and their
structures are built for speed of building.**
- We do: `scene.cpp:1976-1982`: one alpha-tested material makes the instance `FORCE_NON_OPAQUE`. The plain
  structure's submeshes carry no opaque flag (`:736`: "instances decide"). So a candidate on a helmet goes to
  `alpha_opaque`, which reads three records to answer "solid" (`pt_common.hlsli:1470-1476`). The same for any
  still mesh without maps (`:1759-1760`). For plants' groups the flag is by the INSTANCE's alpha flag, not the
  submesh's material (`:1035, 1043`): a trunk in an alpha-tested plant is non-opaque.
- It wants: a character's structure is its own (not shared with other materials), so each submesh can carry
  its material's own flag and the instance none. For a group the same.
- It costs: the tavern: 109,800 triangles tested for nothing (MEASURED count). In ms: not measurable.
- The run: `tavern-room@measure2` steps 3 and 12 (every triangle solid for the two big passes' own rays): the
  bound for M3 and M4 together. The second page of the measuring build has never been compiled whole.
- Size: 15 lines (`blas_inputs` for skinned meshes, the two instance blocks, `:1043`). No pass compiles.
  Risk: a material that changes its alpha flag while the structure lives: put the flag in what makes a group's
  signature and rebuild a character's structure when it changes.

**M4. Hair has no micromap, and a mesh with one hair, glass or camera-only submesh gets none at all.**
- We do: `scene.cpp:2158-2165` refuses the whole mesh. The reason given: a map would decide by the cutoff where
  rays pass strands by chance.
- It wants: a map need not decide the covered part. "Transparent where every texel is bare, unknown elsewhere"
  is exact for hair and for camera-only shells: a bare texel stops no ray of any kind (`pt_common.hlsli:1531`,
  `:1478`), on the route of the tracer's own camera rays. (On the old G-buffer route a camera ray stops at any
  hair card, bare or not, `:1487-1488`: that route must be gone first, or keep hair without maps there.) The bake
  already has that rule for alpha tiles (`pt_omm_bake.hlsl:256`). Glass is the exception
  (it stops rays whatever its alpha, `:1491-1497`): only its submesh must stay without. And the refusal should
  be a submesh's, not the mesh's.
- One trap: the camera's own walk reads hair's alpha from the strand atlas, not the alpha tile
  (`pt_primary.hlsli:243-253`), and a transparent piece can never be brought back (3.1 point 6). A piece is
  bare only if BOTH readings say so.
- It costs: the tavern's 437,000 hair triangles (547,286 less 109,800), each tested on every crossing. Most of
  a hair card is bare. In ms: bounded by M3's run.
- Size: 2 to 3 days (the bake's rule for strands, a per-submesh `omm_possible`, micromap links in the skinned
  build and its refits). The bake is a small pass. Risk: medium (the trap above); own check: a hair card's
  shadow and its picture with maps on and off.

**M5. Every micromap structure is built with `ALLOW_DISABLE_OMMS`, and the plain structure is kept beside it.**
- We do: `scene.cpp:2399`. Once a mesh has maps, every instance uses the map structure; one whose materials
  differ gets `DISABLE_OMMS` (`:1756-1762`, `:1814-1820`, `:1666-1669`). The mesh's plain, compacted structure
  stays and is no longer used. The map structure is allocated at its largest size and never compacted
  (`:2411`).
- It wants: 3.1 point 5. `megageo-1` already had to do the other thing for the partitioned top level, which
  has no such instance flag: instances without maps take the plain structure (`scene.cpp:1756`). Made the
  rule, the build flag can go, and the map structure can be compacted like the others (they compact to 38 %).
- It costs: "some reductions in traversal performance" on every mapped mesh (the spec's words; not measurable
  here); memory: the tavern's 15 MB for 46 meshes, more in a town.
- The run: `forest`: stages before and after; "held, MB: acceleration structures" in "PT structures 10s".
- Size: 15 lines, C++ only. Risk: low.

**M7. One level for every triangle, one map a triangle, a bounding-box rule.**
- We do: level 4 everywhere (3.2). A large leaf card (two triangles over 512 texels) has pieces 32 texels
  wide: the leaves' outline crosses many of them, and each of those is unknown (how many: a GUESS until M0). A blade of grass a few texels wide gets
  the same 256 pieces. A piece is known only if its whole BOX agrees, which is twice its area.
- It wants: 3.1 point 3. Level by texels a piece; shared maps; the outline traced, so that only crossed pieces
  are unknown.
- It costs: not known until M0's count says our C. That is why M0 comes first.
- Size: levels per triangle: 2 to 3 days (the bake, the histogram, the byte offsets: `scene.cpp:2344, 2372`).
  NVIDIA's own baker is an option I did not weigh (its license is not read; our test is not the standard form).

**M8. The camera's own ray makes every triangle a candidate.**
- We do: `pt_primary.hlsli:226`, `RAY_FLAG_FORCE_NON_OPAQUE` over the whole scene: stand-ins and back faces of
  one-sided materials are passed in the loop (`:255-256`). Each candidate reads three records (`:239-240`),
  then `alpha_opaque` reads them again (`:262`). By 3.1 point 6 an opaque piece becomes a candidate again.
- It costs: `prepare` is 2.5 ms in the tavern, 4.1 in the town: the prize is perhaps 1 ms (a GUESS).
- It wants: back faces culled by the hardware (`RAY_FLAG_CULL_BACK_FACING_TRIANGLES`, with
  `TRIANGLE_CULL_DISABLE` only on instances drawn two-sided), stand-ins behind a mask bit (the still mask has
  two bits that say nothing: `scene.hpp:181`, 0x20 and 0x40). Size: 2 days. After the rest.

**M9, M10 (small).** Swaying cloth's copies have no maps (`scene.cpp:1738-1742`): M1's change serves them
too. Three ray queries that never meet a map carry the flag: the decals' structure (`pt_common.hlsli:2338`) and
the caustics' landed light (`:4506`, `:4541`): the spec says leave it off there (3.1 point 5); a macro of its
own, 4 lines.

**M11. Two states for some rays: a choice, not proposed.** `RAY_FLAG_FORCE_OMM_2_STATE` on shadow rays would
end every test on mapped geometry for them. It changes shadow edges at the size of a piece (a sixteenth of a
triangle's edge at level 4). By the rulings that is the owner's choice, with numbers: only after M1, M2, M7.

## 4. RTXDI

### 4.1 The reference against ours

READ: RTXDI-L's defaults, RTXDI-I's guides; ours in `tracer.cpp:3392-3451` and the four passes.

| | RTXDI's sample and library defaults | Ours (defaults) |
|---|---|---|
| lights offered to a pixel | 8 local lights (uniform by the library's default; from its world grid, ReGIR, in its sample), 1 infinite, 1 environment, 1 by a BRDF ray | up to 8 from the cell's list by its weights, 1 from the far list (`pt_common.hlsli:3706-3739`) |
| the winner's first visibility ray | traced | not traced (`pt_di.hlsl:52`; DEAD ENDS: measured, shadow edges 14 px) |
| direct light, reuse across frames | history 20, Basic, no ray | history 4 (the plugin's), Basic, no ray (`tracer.cpp:3400-3407`) |
| direct light, neighbours | 1; 8 where history is short, once a target length is set | 4 everywhere; the "8" can never fire (R3) |
| direct light, final ray | traced, then reused up to 4 frames and 16 pixels | traced every frame to a new point of the lamp, plus the sun's (`pt_di_spatial.hlsl:60-63, 81`) |
| bounced light, first sample | a pass of its own | `pt_gi`, the reordered pass |
| bounced light, reuse across frames | A PASS OF ITS OWN; history 8, age 30 | INSIDE `pt_gi`, after the regrouping (`pt_gi.hlsl:137-144`); history 8, age 30 |
| boiling filter | the library's, over a thread group, after the reuse | our own (`pt_boil.hlsli`), as a pass of its own (`pt_gi_boil.hlsl`), because `pt_gi` cannot synchronize a group |
| bounced light, neighbours | 2, Basic | 4, Basic (`tracer.cpp:3444`) |
| bounced light, final | 1 visibility ray; mixed with the first sample on shiny surfaces | 1 visibility ray (`pt_gi_spatial.hlsl:63`); diffuse only |
| bias correction compiled in | a macro limits what is compiled | the ray-traced one in all four passes (R2), run in none |
| checkerboard | optional, halves the work and the reservoirs | off (`tracer.cpp:3398`) |
| reservoirs | the library's packed records | the same: two arrays each (`tracer.cpp:2190-2197`), 225 MB (PT-87) |
| reflections | (library 3.1 adds path reuse, ReSTIR PT) | no reuse at all |

What each of ours traces a pixel with the rows at their defaults: `pt_di` none; `pt_di_spatial` 2 shadow rays;
`pt_gi` the bounce path, none for the reuse; `pt_gi_spatial` 1 ray. No gap there.

### 4.2 The gaps

**R1. RTXDI's reuse across frames lives inside the bounced light's reordered pass, after the regrouping.**
- We do: `pt_gi.hlsl`: the surface is loaded (`:20`), the paths traced and regrouped (`:54-116`, the call at
  `:83`), and then, for every valid pixel, the reservoir is made and `RTXDI_GITemporalResampling` runs
  (`:131-148`). That function tries up to five places in the previous frame, loading a whole surface each (4
  or 5 texture reads, `pt_common.hlsli:657-727`), reads a reservoir, weighs the sample at both surfaces, and
  the result is stored (RTXDI-L, `GI\TemporalResampling.hlsli`). All of it by screen position.
- It wants: RTXDI-I's ReSTIR GI guide allows the reuse in the tracing shader or in a shader of its own,
  whichever is the faster, and says its sample keeps them apart. NVIDIA's reordering paper says why apart is
  the faster here: 2.1 point 4.
- It costs: section 2.2's reading. The floor: 14.4 ms in the tavern with no ray at all, where the plain pass
  does everything in 13.9. And the "ray alone" step (28 ms) ran this same tail after a REAL regrouping, so
  part of the "ray's cost" is the tail's too. A GUESS: 10 ms for sure if the reading holds, up to 30 of the 42
  ms between the reordered and the plain pass in the tavern; the town alike; the beach little.
- The run: `tavern-room@measure2`, steps 15 to 18: "no ray" against "no ray and no reuse", reordered and plain,
  and "everything without the reuse" (`pt_gi.hlsl:134-135`, `SPEED_M2_NO_REUSE`). If "no ray, no reuse" falls
  to a few ms reordered, this is it. It needs the measuring build compiled once (`perf-1.md` perf2.5).
- Size: a small compute pass that takes lines 131 to 148 and the boiling filter with them (`pt_di.hlsl` is the
  pattern: reuse and filter in one compute shader). `pt_gi` then stores its fresh sample and its rays' mean
  and ends. `pt_gi_boil` goes: the pass count stays. About 80 lines of HLSL, 20 of C++; `pt_gi`'s three
  variants compile anew. A day with its game run. Risk: low: the same sums in the same order; RTXDI's random
  numbers are its own stream (`pt_gi.hlsl:139`).
- It also takes out of the big pass: the surface as a thing to carry (S3), and R2's dead ray.
- This is `ser1`'s pass. I did not design it; I found it from RTXDI's side.

**R2. All four RTXDI passes compile the ray-traced bias correction in, and the default never runs it.**
- We do: `pt_gi.hlsl:13`, `pt_gi_spatial.hlsl:9`, `pt_di.hlsl:19`, `pt_di_spatial.hlsl:9`. The rays run only
  with Crevice Light or the hidden direct-light visibility (`tracer.cpp:3403-3404, 3434-3435`).
- It wants: RTXDI's macro is there to compile the unused modes out. And AGENTS.md's own rule: code that is
  switched off still costs in the big passes.
- It costs: in `pt_gi`, a whole `occluded_from_surface` (five ray queries with the alpha test in each) that
  never runs. In ms: not measurable. R1 moves it to a small compute pass, where it harms nothing.
- Size: with R1, nothing more. The run: R1's.

**R3. The boost for pixels without history is set (8) and can never fire.**
- We do: `tracer.cpp:3409-3411`: `numDisocclusionBoostSamples=8`, `targetHistoryLength=0`. The library takes
  the boost only where a pixel's sample count is under the target (RTXDI-L, `DI\SpatialResampling.hlsli`,
  where the number of neighbours is chosen). Under 0 never happens. (The library's own default is 0 too; its
  noise guide describes the boost as the way a disoccluded pixel converges.)
- It costs: noise for a few frames where the camera reveals something. No time.
- The run: `hair-moving` or any slide step, no denoiser: the revealed strip with the target at the history's
  length (4) against 0. Size: one number. Risk: 8 neighbours instead of 4 on those pixels (time, only there).

**R4, R5, R6: choices, not proposed.** Neighbours: 4 for both where the reference takes 1 and 2: each costs two
surface loads (RTXDI-L loads a neighbour again for the Basic correction). DEAD ENDS says 0 is much worse and 8
better: the owner's eye decided. The final ray reused for 4 frames and 16 pixels, as the reference does: it
would save most of the direct light's shadow rays and RTXDI-I says itself it cannot be made unbiased: the
owner's choice, about 1 ms (a GUESS). The first visibility ray: measured, in DEAD ENDS: stays off.

**R7. Our light grid against presampled tiles and ReGIR: no gap found.** Ours offers exact weights from all
lights that matter to a cell, where RTXDI's cells hold a random few hundred. What ours pays: a binary search a
candidate (`pt_common.hlsli:3677-3686`) and an 80-byte record at a random place. With 500 to 1,000 lights the
whole table is under 100 KB. Not worth a job. Emissive triangles: 262,144 kept as lights (`scene.hpp:166`),
the rest met by paths whole: noise, not light.

**R8. Every surface load reads a texture for a flag that is the same for the whole frame.**
- We do: `load_surface` reads `own[p].a` for "under water" (`pt_common.hlsli:661-664`). That alpha is the
  CAMERA's medium, written per pixel (`pt_primary.hlsli:313`), and to write it `pt_prepare` traces the same
  ray, straight up from the camera, once for every pixel: 2 million identical rays a frame
  (`pt_water_surface.hlsli:115-128`).
- It wants: one frame constant. RTXDI's four passes load a surface 20 to 25 times a pixel a frame (each pass its
  own, 4 neighbours twice in each spatial pass, 1 to 5 tries in each reuse across frames).
- It costs: a GUESS: tenths of a ms. Size: 10 lines and a frame constant (every pass compiles: with another
  change). Also: a previous frame's surface takes THIS frame's flag (`pt_rtxdi.hlsli:35`): harmless today.

**R9. Reflections have no reuse.** Every pixel's reflection is one fresh sample a frame; the denoiser does the
rest. RTXDI 3.1 holds path reuse (`Include\Rtxdi\PT\`). That is a design of weeks and belongs with S6. Noise,
not time. Later.

**R10. Checkerboard: off.** RTXDI-I offers it for slower cards: half the pixels a frame, half the reservoirs
(225 MB to 112). It is a way to build the owner's "half resolution rays" choice of the Rays row with the
vendor's own support. A choice, not a gap.

## 5. Across the frame: rays that are more than one ray

**F1. A shadow ray from a pixel's surface is two or three ray queries.**
- We do: `occluded_from_surface_rays` (`pt_common.hlsli:2815-2855`): one query for what the wind moves, one
  NEAREST-hit query for the first 45 cm (to pass a back face or a stand-in there), then the any-hit query for
  the rest. It serves the lamp's final ray, the sun's, and the bounced light's visibility ray: 3 rays a pixel.
- Why: the guard distances are from the days when a pixel's surface was HDRP's and could lie inside the traced
  mesh. With the tracer's own camera rays the surface IS the traced triangle (`pt_primary.hlsli:439`), but the
  record does not say so: `write_traced` marks every plain pixel "forward" (`:448`), `onMesh` stays false
  (`pt_common.hlsli:701`), so `surface_offset` and `surface_near_back` return the old 2 cm and 15 cm
  (`:2727-2728`).
- It wants: one any-hit query a shadow ray. What still needs the near rule: stand-ins (a mask bit would do:
  M8) and the first-person body.
- It costs: 3 to 6 extra short queries a pixel a frame. A GUESS: 0.5 to 1.5 ms. `measure2` steps 4 and 13
  (one query a ray) measure the like for the big passes' own rays.
- It is PT-60's kind of clean-up ("remove the workarounds"), and it also puts the rays 2 cm nearer the
  surface (contact shadows). Size: 2 days with its picture checks. Risk: medium: black spots where a stand-in
  overlaps what is drawn.

**F2. "Which water am I in" is asked by a ray for every shadow ray that nothing stopped.**
`pt_common.hlsli:2954`, `pt_water_surface.hlsli:115-128`. The sun's and the lamp's ray from one vertex ask
twice. `Speed trial` 1 would skip it above the water and refused itself in both scenes (PT-99). Not measurable
in the measuring run (inside its noise). Small.

## 6. RTX Mega Geometry

The owner: "it might end up giving us a bigger boost than we think." My answer after reading NVIDIA's and
Microsoft's documents and `megageo-1.md`: in video memory, yes, it can be about twice the estimate. In frame
time I find one place where it could be more (the characters) and it is not measured. On plants it can cost
frame time. Everything below is READ unless it says MEASURED or GUESS.

### 6.1 What the documents say

- Clusters are small groups of triangles (64 works well; 256 at most) with a structure each. A bottom-level
  structure is then built over clusters, not triangles: about 100 times faster for clusters of 100 (DXR2,
  "Cluster level acceleration structure"). A template holds a cluster's shape without positions; each frame
  new positions go in and a cluster comes out (DXR2, "Cluster templates").
- Microsoft on templates for animated meshes: as fast as refits, less memory, and "better trace performance as
  the resulting BLAS can be recreated efficiently every frame and adapt to the shape"; static parts can be
  clusters of their own, shared (DXR2, the same section). Clusters carry micromaps.
- NVIDIA's measured sample, 8.43 million animated triangles: building 2.78 ms by refits, 0.80 by templates;
  memory 564 MB against 271; RAYS 1.45 ms against 1.52: 5 % SLOWER. "A degradation in actual rendering
  performance is expected"; for still content the plain structure built for fast tracing "is still the best".
  Clusters should be compact in space and overlap little; larger clusters save memory and cost ray time (MG-A).
- The partitioned top level traces like a plain one when its partitions are compact. Its "global" partition
  is for what moves every frame and costs tracing (MG-P; DXR2, "Global partition"). In Microsoft's version an
  instance may carry a box of its own, so its structure can change without the partition being rebuilt.
- Microsoft's version (raytracing tier 2.0) is a preview; NVIDIA's runs today through NVAPI (MG-R: driver 570
  or later; `megageo-1.md`: this PC reports both parts).

### 6.2 Where it could give more than the estimate, and where less

**G1. The plants.** MEASURED (the coordinator's, run `20261010-092027`): 3.90 ms a frame, 624 groups, 6.4
million triangles, 766 to 795 MB. Today one structure a 16 m GROUP (not a plant), built once, refitted for
ever (`scene.hpp:214-222`, `scene.cpp:919-921, 1241-1255`).
- The estimate (`megageo-1.md`): 2 to 3.5 ms and about 220 MB.
- MORE, in memory. The estimate counts the structures (428 MB to about 205). But 795 MB is also the groups'
  merged copies: texture coordinates, colours, normals, a range word, indices: 20 bytes a vertex and 12 a
  triangle (`scene.cpp:999-1005`): about 270 MB at 9.5 million vertices and 6.67 million triangles (reckoned
  from `megageo-1.md`'s counts). They exist because a group is one mesh to `fetch_hit`. Once a hit names its
  plant (a cluster's number does), it can read the plant's own mesh, and those copies go. Then the town's
  plants are about 320 MB instead of 795: 475 less, against the estimate's 220. A GUESS from byte counts; and
  that part needs no clusters at all (a submesh's number already names the member today).
- LESS, in time. In a town the frame is rays, many of them through plants. NVIDIA's 5 % on those rays can
  cost what the refit gives back. `megageo-1.md` says so too. I expect about zero net on plants.
- A risk in the plan: it cuts clusters as runs of consecutive triangles in the mesh's index order. NVIDIA asks
  for clusters compact in space. A plant's cards need not lie together in index order; a cluster whose box
  spans the plant slows every ray. Check the boxes' overlap on one grass mesh and one tree before building.
- Micromaps (M1) can ride along, since clusters carry them; but M1 does not need clusters and is where the
  plants' RAY time is.
- The run: `megageo-1.md`'s own step (a), offline, for the real sizes. In the game: `metal@structures`, the
  world running: "plants ... ms" and "held, MB" in "PT structures 10s", AND "bounced light" and "reflections"
  beside them: the rays are the test, not the build.
- Size: the estimate's week. The merged copies alone: 2 to 3 days (`fetch_hit`, the groups' records; every
  pass compiles).

**G2. The water's pieces.** 0.64 ms, 1,379 pieces refitted, frozen or not (`scene_water.cpp:429-447`). The
estimate: 0.4 to 0.8 ms, 40 to 70 MB, one template a level of the shared pattern. I find nothing more. Each
piece is a top-level instance (762 of the tavern's 5,331 instances are sea); one cluster structure a water
would make them one instance each. Whether rays gain: not measured; I expect little.

**G3. Characters: the one place where the rays could gain.** 0.44 ms; built for fast BUILDING, refitted, built
anew every fourth frame (`scene.cpp:745`, `:51`). The spec: that flag trades tracing speed away; a refitted
structure traces worse the more it moved; NVIDIA: refits lose most under twists like a skeleton's (MG-A). In
the tavern characters are 704,000 of 1.53 million triangles, and a bounce ray was measured at 11 times a
camera ray there (part of that is R1's tail, by my reading; what is left is not explained). Templates give a
structure made for this frame's pose at a refit's price.
- The estimate: "under 25 MB, 0.2 ms". What it leaves out: the rays. NOT KNOWN.
- The run: a two-line trial, C++ only: characters built for fast tracing and built anew every frame.
  `tavern-room`: "bounced light" and "reflections" against as now; "characters" in "PT structures 10s" says
  what the trial costs. If the rays gain X, templates can bring about X without that cost. If they gain
  nothing, characters are a 0.2 ms matter, as estimated.

**G4. The traced world's arrival.** MEASURED (PT-98): whole 2 s after it starts over; the waits the owner saw
were the passes being made. Clusters need no compaction (DXR2), ours takes a read-back and a copy some frames
later, and the per-frame build budgets could go (`scene.cpp:39`, `scene.hpp:221-222`). A GUESS: under a
second. Not a reason.

**G5. Geometry left out or taken coarser because building costs too much.** I found none. What is left out is
left out for other reasons (effects, far trees' billboards with no mesh, see-through meshes: PT-83's census).
The remaining caps are large (cloth: 4,096 copies, `scene.hpp:209`). The tracer traces the level the game
draws (PT-61), so RTX Mega Geometry's other half, levels of detail by clusters for scenes of billions of
triangles, has no job here.

**G6. Does it change what a ray costs?** Clusters: yes, a little slower (above). The partitioned top level: no,
if what moves is few. The trial puts every refitted thing in the global partition: about 2,100 of 16,000
instances. Plants' groups and water pieces stay in place; only their boxes breathe. With a box of their own
(Microsoft's version has it for swapping an instance's structure; whether it serves a structure refitted in
place, and whether NVAPI has it: not checked) they could sit in ordinary partitions. `megageo-1.md` measured
equal tracing with 2 things in the global partition, not 2,100: the run `metal@mega` is still owed.

**G7. Not in the plan: the caustics' landed triangles.** Their structure is built from nothing every frame,
for fast building, over 65,536 to 4 million triangles (`tracer.cpp:1109, 3717-3720`). That is the documents'
own use for templates ("surfaces of dynamically created geometry"). Its share of the caustics stage (3.9 ms in
the tavern, 7 to 9.5 at the beach) is NOT measured; `water-day@measure-water` steps 13 and 14 split the stage.
It fits only if the triangles are written tile by tile; today they are appended in any order, which gives
clusters spread over the whole sea. A GUESS: 2 to 4 ms at the beach, days of work. After S5.

### 6.3 What our rendering would have to be built like

1. A hit names its triangle by cluster and triangle-in-cluster. `megageo-1.md`'s way (a submesh number a
   cluster) keeps `fetch_hit` as it is. What else reads a triangle's number: the "solid pieces first" count
   (`pt_common.hlsli:1400-1405`: a triangle's place in its submesh), the micromap indices, the decals' layers.
2. Meshes cut into clusters compact in space when they are registered (the thread that compacts meshes today).
3. Shading data by member and mesh, not merged a group (G1): the same change that frees 270 MB.
4. Every ray query opts in. Under Microsoft's version that is one more flag in `PT_RAYQUERY`
   (`pt_common.hlsli:67`): one line, every pass compiles once.
5. A frame: pose (as today), stamp the templates (one call), build the structures over clusters (one call),
   the top level.
6. M5 first: with instances that go without maps on the plain structure, the partitioned top level's missing
   "no micromaps" flag stops being a special case.

## 7. The ranking

The five that would give players the most:

| # | Entry | Expected gain | How sure | The run that proves it | Size |
|---|---|---|---|---|---|
| 1 | R1: RTXDI's reuse across frames out of the reordered pass (`ser1`'s) | tavern and town: 10 to 30 ms of the bounced light | a guess that fits every measured row | `tavern-room@measure2` steps 15 to 18 | 1 day |
| 2 | S5: the water's path regrouped a turn, in a pass of its own | the beach: tens of ms of 140 | not known; `perf-1.md`: "a third" | `water-day@regroup` (built, never run) | trial: a build; the pass: 2 to 3 days |
| 3 | M1, then M2: micromaps for plants in the wind, leaves' covered pieces opaque | town, forest: not known; NVIDIA: 30 % of tracing where cut-outs are moderate | bounded by one step | `metal`, `forest`: "plants solid" for one step, then M1 on and off | M1: 1 to 2 days, C++ only; M2: a week |
| 4 | S1, S2, S3: the key in 8 bits, the hit object, the hit fetched after, bytes a thread logged | reflections: a few ms; video memory: up to 1 GB if PT-87's reading holds | the listing's facts are sure, the gain is not | the log line; key as now against 8 bits in `tavern-room`, `metal`; `tavern-room@no-ser-memory` | S3: an hour; S1: an hour and a build; S2: 1 to 2 days |
| 5 | M3, M4, G3: characters' opaque submeshes, bare-texel maps for hair, their structures' quality | the tavern: a few ms | bounded by one step | `tavern-room@measure2` steps 3, 12; the two-line trial of G3 | M3: an hour; M4: 2 to 3 days |

Then the rest, in the order I would take them:

6. M0: the micromaps' switch in the plugin and the coverage count. Half a day. It should come FIRST in time: it
   says how much 3, 5 and M7 are worth.
7. M5: no `ALLOW_DISABLE_OMMS`, map structures compacted. An hour or two.
8. R3: the boost for pixels without history. One number.
9. S4, S7: the guide's double store; recursion depth 0. With S1's build.
10. F1: one query a shadow ray. 2 days. PT-60's kind.
11. M7: levels per triangle. After M0's count.
12. R8, F2, M8, M9, M10: small, each with another change.
13. G1's merged copies (270 MB), then `megageo-1.md`'s plan, for memory.
14. S6, R9: shading as shared shaders; reuse for reflections. Weeks. Not before 1 to 5 have been measured.
15. S8, G7, R10 and the choices R4, R5, M11: when asked.

Runs, so that one start answers several entries:

| Run | Build needed | Answers |
|---|---|---|
| `tavern-room@measure2`, `metal@measure2` | the measuring build, whole (never compiled whole yet) | R1 (steps 15 to 18), M3 and M4's bound (3, 12), F1's like (4, 13) |
| `forest`, `metal`: maps on and off | plugin only | M0 |
| `metal`, `forest`: "plants solid" one step, the world running | C++ only | M1 and M2's bound |
| `water-day@regroup` | `-DTGPT_REGROUP_WATER=ON`, two passes | S5 |
| `tavern-room`, `metal`: key as now, key in 8 bits | two passes | S1 |
| any start; then `tavern-room@no-ser-memory` | C++ only | S3 |
| `tavern-room`: characters built for tracing, every frame | C++ only | G3 |

## 8. What I did not do, and what is open

- No build, no test, no game. Nothing was profiled. Every "expected gain" above is a guess unless it cites a
  run.
- I did not open RTXDI's sample application's shaders, only its guides and the local library.
- Whether NVIDIA's driver honours 16 or 8 hint bits for an empty hit object: not documented, not measured
  (S1's run says).
- Whether the driver overlaps the storage of 153 ray queries: not knowable from here (S3's line is the nearest
  thing).
- Whether a refit of a structure with micromaps behaves on this driver: allowed by the spec, new to us (M1).
- The bounced light's pass itself is `ser1`'s: R1, S1 and S2 touch its lines (`pt_common.hlsli:72, 86`,
  `pt_gi.hlsl:83, 131-148`). Give them this file before they design.
- Beside this file, in the scratch folder only: the fetched vendor documents (`compat1-refs`) and two small
  scripts that patched this file's wording (`compat1-scripts`). Nothing in the repository or the game folder.
- Workarounds met, none removed: the near-surface guard of the G-buffer days (F1); the plain structure kept
  beside the micromap one (M5); the boiling filter as a pass of its own (R1).
