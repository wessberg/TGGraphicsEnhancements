# Performance review by GPT-6-astra (xhigh, read-only), 2026-10-11 ~11:00

Asked by the owner, 2026-10-11: "ask gpt-6-astra xhigh in an independent review if it can see any problems we haven't caught ourselves in how we do shader execution reordering, opacity micromaps, integrate rtxdi, trace against it, with focus on performance specifically". Prompt: `codex exec -m gpt-6-astra -s read-only -c model_reasoning_effort=xhigh`, the brief in this round's notes (round-20 section 11). Its last message, unchanged, below. Line numbers are of the working tree at ~10:45 (the fog cleanup of ~11:00 was in progress while it read).

The tracer uses these technologies, but I cannot call its current use performance-optimal. There are concrete remaining inefficiencies, plus several potentially large costs that the existing measurements do not resolve.

The strongest next steps are:

| Priority | Finding | Likely importance |
|---|---|---|
| 1 | Reflections prepare substantial hit data before SER; water then enters a large, divergent continuation | Big potential in reflections/water |
| 2 | Micromap arrays still contain unnecessary entries and remain uncompressed after construction | Medium memory and loading benefit; frame-time benefit uncertain |
| 3 | The micromap baker marks some regions unknown because of unnecessarily broad texture bounds | Medium potential in foliage-heavy scenes |
| 4 | New fog code repeatedly searches for distances and scans every fog volume | Medium; potentially big with many volumes |
| 5 | Plant refits have no production policy for restoring traversal quality | Potentially big, but unverified |
| 6 | Light selection performs avoidable work, including evaluating candidates already assigned zero weight | Small to medium across several passes |

55 fps allows **18.2 ms for the whole frame**. Your supplied tracing times alone exceed that by roughly 1.7–6.5 times. These findings justify further work; they do not establish enough savings to meet the target.

This review examined the current working files, audit 4, and the relevant measured dead ends. It made no changes and collected no new GPU timings. “Verified” below means verified in the source; performance estimates remain hypotheses unless explicitly identified as arithmetic from existing measurements.

## 1. Reflections still do expensive hit preparation before SER

**Verified in the code.** [pt_specular.hlsl:115](native/pathtracing/shaders/pt_specular.hlsl:115) calls `trace_from_surface`, which constructs the full `Hit` before the reorder at line 132. [pt_common.hlsli:2788](native/pathtracing/shaders/pt_common.hlsli:2788) calls `fetch_hit`, whose work includes geometry reads, UVs, tangent frames, transforms and texture footprints.

The bounced-light pass already demonstrates the better boundary: it obtains a compact query result, reorders, and then calls `fetch_hit` at [pt_gi.hlsl:159](native/pathtracing/shaders/pt_gi.hlsl:159).

Primary glass also performs its dielectric traversal before this reorder, at [pt_specular.hlsl:42](native/pathtracing/shaders/pt_specular.hlsl:42).

**Documentation.** NVIDIA recommends reordering after intersection is known and before material-dependent processing. It also recommends minimizing values that must survive reordering. See the SER whitepaper, “ReorderThread and raytracing” and “Optimizing live state,” printed pages 2 and 9–10. [NVIDIA SER whitepaper](https://developer.nvidia.com/sites/default/files/akamai/gameworks/ser-whitepaper.pdf)

**Why it costs time.** The geometry reads and calculations before the call receive no benefit from that call’s improved coherence. Their results may also need to survive it. The compiler decides the actual storage cost, so the large source-level `Hit` is evidence of an opportunity, not proof of a particular spill size.

**Estimated size:** medium; potentially larger where glass or expensive materials dominate.

**Fix — keeps the picture.** Give reflections the GI pass’s query/fetch separation. Carry only the intersection information and state that cannot cheaply be recovered. Fetch the full hit after reordering. Preserve the existing accepted-hit rules and random-number sequence.

**Measurement:** compare both SER-enabled versions using shader timings, memory traffic, registers, spills and active threads per warp. Check undenoised output, including glass, cloth and first-person geometry.

## 2. Water needs smaller shader continuations for SER to work effectively

**Verified in the code.** The current ray-tracing state object exports one ray-generation shader, without material hit shaders: [tracer.cpp:907](native/pathtracing/src/tracer.cpp:907).

The main hint is 16 bits: 15 material bits plus the cache-feeding bit in the highest position, at [pt_common.hlsli:87](native/pathtracing/shaders/pt_common.hlsli:87). That describes the first hit. It does not describe the later sequence of water scattering, surface crossings, material shading and continued paths in [pt_water_path.hlsli:27](native/pathtracing/shaders/pt_water_path.hlsli:27).

The additional water reorder calls remain behind the disabled `PT_REGROUP_WATER` build option. Audit 4 already identifies that gap. The further issue is the shader structure around those calls.

**Documentation.** NVIDIA’s “Optimizing warp coherence” recommends specializing large shaders and measuring active lanes. Microsoft prioritizes the more significant hint bits and permits implementations to ignore lower bits. Thus material-number proximity is not necessarily a useful fallback grouping. [NVIDIA SER whitepaper, printed page 9](https://developer.nvidia.com/sites/default/files/akamai/gameworks/ser-whitepaper.pdf), [Microsoft SER specification, “MaybeReorderThread with coherence hint”](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)

**Suspected cost:** big in the beach reflection/water pass. Its actual cause could be divergent execution, instruction-cache pressure, registers, traversal, or a combination. Source size alone cannot distinguish them.

**Fix — keeps the picture.** Prototype a smaller continuation boundary: traverse, classify the next event, reorder, then execute a specialized material or medium stage. Keep common work shared. Preserve path probabilities, termination rules and all geometry.

HitObject tracing with specialized hit shaders is a legitimate candidate. Inline RayQuery remains legitimate too; converting every query is not required.

Two corrections to the previous audit matter here:

- `HitObject::FromRayQuery` can supply hit information for reordering without an assigned hit shader. Its earlier unsuccessful test establishes a result for that implementation, not an API limitation. [Microsoft SER specification, “HitObject::FromRayQuery”](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)
- `GetShaderStackSize` reports invocation stack requirements. A result of zero or 128 bytes does not establish register pressure, occupancy or the complete cost of values surviving SER. [Microsoft, “GetShaderStackSize — Return value”](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12stateobjectproperties-getshaderstacksize)

**Measurement:** obtain an optimized shader profile of the beach pass before another architecture trial. Record active lanes, instruction stalls, registers, spill traffic and traversal utilization. NVIDIA’s live-state analysis gives the relevant method. [NVIDIA, “Ray-tracing live-state spills”](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/)

The existing failed loop-reordering and three-bit-key trials should remain dead ends unless this restructuring supplies a new reason to revisit them.

## 3. Micromap compaction is incomplete

**Verified in the code.**

At [scene.cpp:2687](native/pathtracing/src/scene.cpp:2687), every alpha-tested triangle receives an input block and descriptor. The array histogram still includes all those entries at line 2724.

Meanwhile, [pt_omm_bake.hlsl:283](native/pathtracing/shaders/pt_omm_bake.hlsl:283) writes the complete block even when the triangle subsequently receives a fully opaque or transparent special index. Such an index does not reference that block.

The array build uses `PREFER_FAST_TRACE`, without `ALLOW_COMPACTION`, at [scene.cpp:2728](native/pathtracing/src/scene.cpp:2728). The compaction added by M5 applies to the **BLAS referencing the array**, beginning at line 2753.

**Documentation.** DXR explicitly supports compaction of OMM arrays. NVIDIA’s baker documentation also describes removing uniform blocks and sharing identical blocks. [Microsoft DXR specification, “Acceleration structure build flags” and “Opacity micromap related API and HLSL impacts”](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html), [NVIDIA OMM guide, “Algorithm Overview,” steps 1, 4 and 5](https://github.com/NVIDIA-RTX/OMM/blob/main/docs/integration_guide.md)

**Estimated size:** medium for memory, bake work and loading. Steady-state frame savings need measurement.

**Fix — keeps the picture.**

- Build descriptors only for referenced blocks.
- Share bit-identical blocks.
- Use the corresponding special index for uniform unknown blocks too.
- Enable and perform array compaction, updating dependent references safely.

This extends the completed BLAS compaction; it does not repeat it.

**Measurement:** report unique and referenced block counts, array bytes before/after compaction, temporary bake memory, bake time and steady-state ray time separately. Do not assume the existing BLAS compression ratio applies to arrays.

## 4. The micromap baker is more conservative than necessary

**Verified in the code.** [pt_omm_bake.hlsl:121](native/pathtracing/shaders/pt_omm_bake.hlsl:121) classifies the expanded axis-aligned rectangle around each microtriangle.

For larger regions, lines 146–170 inspect coarse minimum/maximum cells. A mixed cell immediately makes the result unknown, including when the mixed portion lies outside the actual microtriangle’s sampling footprint.

This is safe for coverage, but it can produce unnecessary shader alpha tests.

**Documentation.** NVIDIA’s OMM guide distinguishes conservative correctness from good coverage. Its “Optimal resampling micro-triangle space” section describes testing the alpha contour against the microtriangle. [NVIDIA OMM guide, “Sample output” and “Algorithm Overview,” step 3](https://github.com/NVIDIA-RTX/OMM/blob/main/docs/integration_guide.md)

**Estimated size:** medium potential in forest and town traversal. The reported 35% unknown fraction does not reveal how much is genuinely ambiguous and how much comes from loose bounds.

**Fix — keeps the picture.** Retain conservative filtering and rounding protection, but refine mixed bounds only where they intersect the triangle’s actual footprint. Compare against the vendor baker on representative meshes before implementing a replacement.

**Measurement:** at the same subdivision level, compare unknown coverage, non-opaque candidate counts, bake time, memory and ray time. Verify alpha decisions against the existing shader test.

Increasing subdivision again should not be the first response to an unnecessarily loose classifier.

## 5. Fog contains avoidable searches and repeated scans

**Verified in the code.**

[pt_medium.hlsli:74](native/pathtracing/shaders/pt_medium.hlsli:74) uses 22 bisection iterations for every sampled height-fog distance. Each evaluates `height_fog_depth`, which includes distance and exponential calculations.

That density is piecewise constant/exponential: [pt_common.hlsli:912](native/pathtracing/shaders/pt_common.hlsli:912). Its ideal mathematical integral can be inverted piece by piece.

The same medium code repeatedly walks the entire volume list:

- Transmittance: line 154.
- Scattering selection, including a second scan: line 203.
- Free flight: line 240.
- Density at a point: line 335.
- Scattering probability: line 342.

Each intersected volume uses one density bound for its tracking loop.

**Documentation.** Inverse-distribution sampling is the standard basis for exponential free flight. Spatially varying density bounds reduce null collisions without replacing the underlying density. See PBRT’s primary descriptions: [§15.2, “Sampling Volume Scattering”](https://pbr-book.org/3ed-2018/Light_Transport_II_Volume_Rendering/Sampling_Volume_Scattering), [§11.4.3, “DDA Majorant Iterator”](https://pbr-book.org/4ed/Volume_Scattering/Media#sec:majorant-grid).

**Estimated size:** medium; potentially big with numerous or sparse fog volumes. The supplied timings do not isolate this work, so I cannot assign it a share of the beach cost.

**Fix — keeps the picture.** Replace height-fog bisection with a stable piecewise inverse, including horizontal-ray limits and the existing numerical branches. Add an exact spatial index for volume intersections and, where worthwhile, tighter local density bounds. Preserve overlapping volumes and off-screen contributions.

**Measurement:** count distance inversions, volume tests, actual intersections and null collisions. Time fog separately and inside reflection/GI paths. Validate sampled distance distributions and undenoised means, including rays crossing the fog base.

## 6. Plant refits may save build time while increasing every later ray’s cost

**Verified policy; suspected performance problem.** [scene.cpp:1368](native/pathtracing/src/scene.cpp:1368) builds each plant group initially and subsequently refits it. A rebuild trial exists, but normal operation has no traversal-quality-driven rebuild policy.

Groups use 16-metre XZ cells. Whether their bounds contain excessive empty space or overlap cannot be established from this grouping rule alone.

Characters instead use `PREFER_FAST_BUILD | ALLOW_UPDATE` and staggered rebuilds every four frames: [scene.cpp:751](native/pathtracing/src/scene.cpp:751).

**Documentation.** NVIDIA recommends comparing dynamic build policies using both construction and tracing costs. It identifies large deformation as a cause of poor refit traversal and suggests staggered rebuilding where beneficial. [NVIDIA RTX best practices, “Build preference flags” and “Dynamic BLASes”](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)

**Estimated size:** potentially big across forest rays; medium for characters. Neither is proven. Refits do not necessarily deteriorate merely because many frames pass.

**Fix — keeps the picture.** Compare freshly rebuilt and refitted structures containing **identical posed vertices**. Adopt rebuilding or different grouping only where total frame cost improves. Likewise compare character build preferences using total cost.

**Measurement:** separate build time from camera, shadow, GI and reflection traversal time. Inspect bounds, overlap, triangle shapes and group sizes in an acceleration-structure capture. Include rebuild amortization.

Reducing wind updates, dropping distant geometry or culling objects solely because they are outside the camera would change the picture and needs the owner’s choice.

## 7. Light selection evaluates candidates that already have zero weight

**Verified in the code.** [pt_common.hlsli:3714](native/pathtracing/shaders/pt_common.hlsli:3714) assigns zero weight to a far-list candidate already represented by the near list.

Callers nevertheless load and evaluate it:

- [pt_di.hlsl:40](native/pathtracing/shaders/pt_di.hlsl:40) samples its shape and evaluates its target.
- [pt_common.hlsli:3844](native/pathtracing/shaders/pt_common.hlsli:3844) can select an emissive triangle, read its emission and evaluate lighting before multiplying by zero.

This selection machinery also runs at secondary vertices and in fog. Its cost is not confined to the direct-light pass.

**Documentation.** RTXDI supports renderer-specific sampling. Its integration guide describes presampling to amortize distribution lookup and world-space sampling for secondary vertices. [RTXDI 3.1 integration guide, step 5 and “World-Space sampling and ReGIR”](https://github.com/NVIDIA-RTX/RTXDI/blob/v3.1.0/Doc/Integration.md)

**Estimated size:** small for zero-weight candidates alone; medium potential for repeated light-distribution and emissive-cluster searches.

**Fix — keeps the picture.** Skip expensive evaluation once the candidate’s weight is known to be zero. Preserve reservoir sample counts and random-number advancement; a naïve `continue` can change normalization or subsequent samples.

Then profile the repeated binary searches before considering presampled records or an exact faster lookup structure. A ReGIR replacement is not automatically faster than the existing grid.

**Measurement:** count zero-weight candidates and time distribution search, light loading, shape evaluation and visibility separately. Include GI, reflections and fog. Any presampling change also needs noise and temporal-correlation checks.

I found no basis for reopening the failed visibility-reuse approach merely to save these costs.

## 8. GI traces final visibility for some pixels whose result is overwritten

**Verified in the code.** [pt_gi_spatial.hlsl:63](native/pathtracing/shaders/pt_gi_spatial.hlsl:63) traces visibility for the selected GI sample.

Later, subsurface pixels receive their own random-walk illumination. [pt_walk.hlsl:110](native/pathtracing/shaders/pt_walk.hlsl:110) overwrites their direct diffuse and bounced diffuse RGB. The dispatch order is explicit at [tracer.cpp:3782](native/pathtracing/src/tracer.cpp:3782).

The earlier reservoir work can still help neighboring pixels. The discarded final shading is the narrower opportunity.

**Documentation.** RTXDI separates resampling from final shading and its visibility ray. [RTXDI 3.1 integration guide, steps 7–8](https://github.com/NVIDIA-RTX/RTXDI/blob/v3.1.0/Doc/Integration.md)

**Estimated size:** small generally; medium potential in character close-ups.

**Fix — keeps the picture.** After required reservoir processing, skip final GI visibility/shading where the same conditions guarantee the subsurface pass will replace it. Preserve the hit-distance alpha used by the non-NRD route.

**Measurement:** count skipped visibility rays, compare full-frame output and check neighboring pixels over time. Do not remove initial GI generation wholesale.

## 9. Every dispatch receives a global UAV barrier

**Verified in the code.** [tracer.cpp:2424](native/pathtracing/src/tracer.cpp:2424) inserts and flushes a global UAV barrier after every compute dispatch. The ray-generation dispatch helper does the same at line 2517.

For example, each water slot runs four consecutively synchronized grid stages at [tracer.cpp:3758](native/pathtracing/src/tracer.cpp:3758), although different slots occupy separate ranges.

**Documentation.** NVIDIA identifies UAV barriers as a source of queue-draining waits and recommends using GPU Trace to find the resulting gaps. [NVIDIA, “Advanced API Performance: Async Compute and Overlap,” discussion before “Recommended”](https://developer.nvidia.com/blog/advanced-api-performance-async-compute-and-overlap/)

**Estimated size:** small to medium across the frame. This does not plausibly explain the entire water-pass cost.

**Fix — keeps the picture.** Move synchronization decisions to the pass dependency graph. For water grids, batch independent slots at each stage, then synchronize before the dependent stage. Retain required barriers between genuine producers and consumers.

**Measurement:** GPU timeline gaps and total frame time. Merely replacing global barriers with resource-specific barriers does not guarantee a saving.

## 10. Water-only queries unnecessarily opt into micromaps

**Verified in the code.** The global macro at [pt_common.hlsli:67](native/pathtracing/shaders/pt_common.hlsli:67) gives every query `RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS`.

That includes the water-only queries at [pt_water_surface.hlsli:95](native/pathtracing/shaders/pt_water_surface.hlsli:95). Their mask selects opaque wave geometry built without OMMs.

**Documentation.** Microsoft explicitly says omitting OMM support is more efficient when no OMM can be encountered. Omitting it when one can be encountered is undefined behavior. [DXR specification, “Opacity micromaps and TraceRayInline”](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#opacity-micromaps-and-tracerayinline)

**Estimated size:** small per query; worth measuring because water repeats these queries.

**Fix — keeps the picture.** Use a separate query declaration for paths proven unable to encounter OMM geometry. Keep OMM support on general scene queries.

**Measurement:** compare otherwise identical water shaders and establish the mask/geometry invariant with a regression check.

## 11. Continuous SER probing has a measurable opportunity cost

**Verified in the code.** [tracer.cpp:3574](native/pathtracing/src/tracer.cpp:3574) probes the alternative hint for eight frames every 128 frames, indefinitely.

Using audit 4’s reported 6–13 ms disadvantage for the losing variant, that schedule would add approximately **0.38–0.81 ms per frame on average** in a scene where the same variant continues losing. This is conditional arithmetic, not a new benchmark.

**Documentation.** SER hints affect scheduling; the API does not prescribe periodic benchmarking. [Microsoft SER specification, “MaybeReorderThread with coherence hint”](https://github.com/microsoft/hlsl-specs/blob/main/proposals/0027-shader-execution-reordering.md)

**Estimated size:** small average cost, with larger individual slow frames.

**Fix — keeps the picture.** Back off after repeated decisive results and retest after meaningful scene or workload changes. SER remains enabled in both choices.

**Measurement:** compare sustained frame-time distributions, including the slow probe frames, against the current schedule.

## Checks that do not justify another change

- **Camera micromaps:** audit 4’s statement that forced non-opaque camera rays leave OMMs unused is incorrect. Transparent microtriangles are rejected before force flags act. The camera still loses opaque-hit acceptance benefits, but OMM benefit is only partially lost. The existing camera-query optimization remains open; measure surviving candidates rather than assuming all OMM work is bypassed. [pt_primary.hlsli:226](native/pathtracing/shaders/pt_primary.hlsli:226), [DXR specification, “Opacity micromaps and TraceRayInline”](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#opacity-micromaps-and-tracerayinline)
- **Shadow flags:** ordinary occlusion queries already use `ACCEPT_FIRST_HIT_AND_END_SEARCH`. Closest-hit processing near pass-through surfaces has a documented correctness reason. Blanket first-hit or force-opaque changes would be wrong.
- **Four-state OMMs:** retaining shader resolution of unknown states is appropriate. Forcing two-state interpretation would alter coverage: **changes the picture (the owner’s choice)**.
- **Static BLAS/TLAS policy:** fast-trace static structures and rebuilt fast-trace TLAS already follow NVIDIA’s guidance. The reported small TLAS cost does not make instance-count reduction the first priority. [NVIDIA RTX best practices, “Build preference flags”](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/)
- **RTXDI integration:** custom initial sampling is permitted. Temporal/spatial reuse, target evaluation and final visibility are present. The known disocclusion-history issue remains separate; I found no new evidence that changing history lengths would solve the main performance problem.
- **Compiler/resources:** optimized compilation is present. The large generated header is not a measurement of machine-code size. I found no obvious large, frequently read scene resource mistakenly left in an upload heap. Removing `NonUniformResourceIndex` where descriptors genuinely differ between lanes would not be a valid optimization.
- **Measured dead ends:** shorter water rays, the previously tested loop reorder, visibility reuse and contaminated no-hint comparisons do not become recommendations again through this review.

The first performance capture should separate the beach pass’s traversal cost from shader execution and carried state. The first contained code trials should be reflection hit-fetch separation, tighter OMM classification at the existing level, and rebuilt-versus-refitted plants with identical poses. All three can preserve the picture and directly test the largest unresolved explanations.