# Missing world inventory (PT-83)

2026-10-09, missing worker. Diagnostic implemented; final checks recorded below. No rendering fix, native change, game run or deploy. Stop at this hand-over; PT-83 remains open.

## Coordinator: one run after merging and installing the managed plugin

The launcher builds/installs the harness, but does not build/install TGPathTracing.dll. Include this worker's managed plugin in the coordinator's normal build/deploy first. No native rebuild or ABI change is required by this job.

```powershell
.\tools\harness\Run-PtHarness.ps1 -Scenario vista,npc-day,npc-candle,stairwell,forest -Recipe missing
```

Then read `artifacts/harness/<run>/missing-report.md`, its five linked scene reports, and the Off/On `missing-view.jpg` context pictures. `missing-summary.json` contains the joined data; each step's `missing.json` retains the complete census. Reanalysis: `python tools/harness/missing.py artifacts/harness/<run>`. The launcher runs it automatically unless `-NoAnalyze` was requested.

The tables put left-out objects first, largest projected overlap first. They give the name/path, draw source, mesh/triangle count, each material's shader/queue/keywords, native input list/mesh/slot, and the exclusion rule. Partial submeshes, deferred registration and unknown evidence are separate states. Off supplies the game's visibility state; On supplies current accepted submissions and capture decisions. No recipe step sets `neural`; Denoising is None. The final step returns PT to Off. No image sequence, performance measurement, RR or GPU test is requested.

## Plan and boundaries

Read the capture gates and existing effects inventory. Add an opt-in `missing` recipe with the inspection's screen boxes, a read-only census of game draw sources, and evidence of submitted tracer geometry. Separate a confirmed exclusion from an unknown or deferred capture; a projected bounds overlap does not prove pixel visibility through occluders. Record materials per slot, because a renderer can be only partly traced. Off and On observations are required to distinguish the game's own visibility from tracing's suppression of it. Build both managed projects and run the offline analysis tests, including deliberately damaged evidence. Stop for the coordinator's single five-scene run.

Only this copy and missing-* build folders may be written. No GPU test is needed. Coordinator-owned backlog/context/round files remain untouched; findings for them go here.

## Progress

- Read BACKLOG, the path tracing context and DEAD ENDS, round 6's current status, AGENTS and AGENT-WORKERS, and effects-inventory.md.
- The inspection identifies visual absences; none yet identifies a runtime renderer/material. Do not infer that the windows use a transparent queue or the plants use a particular vegetation engine.
- Inventory design and exact rejection-rule audit are next.

## Workarounds

No workaround removed: this job must not change the traced scene or the player's image. Existing special material gates and visibility exceptions will be listed with evidence below.

## Step 1: source findings (before runtime evidence)

- VegetationCapture.Wanted accepts Tree, LargeObjects and Objects only; Grass/Plants are skipped (VegetationCapture.cs:67,106). This is a certain class exclusion, not yet a cause of npc-day: the runtime source is unknown.
- CaptureTerrains emits heightmap geometry only (SceneCapture.cs:2609-2625); no Unity treeInstances/detail-prototype capture is present. These sources need a census independent of the tracer.
- ReadMaterial rejects transparent/high-queue/NoRender materials unless the brow or non-Unlit named-glass exceptions apply (SceneCapture.cs:1228-1257). Opaque Unlit is accepted; do not claim all Unlit is excluded.
- CharacterLeftOut has separate transparent/Unlit/NoRender/ScalpMask/Stubble gates, with brow/cornea/shell exceptions (SceneCapture.cs:615-623; CharacterCapture.cs:873-878).
- None proves which shader or source draws any inspection object. Runtime diagnosis remains required.

## Step 2: implementation underway

Opt-in accepted-submission snapshots in NativeTracer; no native ABI or native source changes. Independent censuses read game draw sources, with stable per-run IDs, projected bounds, materials per slot, and exact input-list slots/mesh handles when matched. Occlusion and final alpha coverage remain explicitly unproven; unknown membership is not reported as a confirmed exclusion.

The inspection text records crop names and sizes, not all coordinates. Recovered its actual boxes by OpenCV template matching against the inspection's game overview tile (read only). Correlations: purple canopy .972, npc-day plants .983, leaded window .954, rose window .856, statue/ghost .952, forest NPC crop .894. Rounded to the original hundredths: vista [.44,.42,.60,.70], npc-day [.82,.52,.98,.78], npc-candle [.38,.36,.52,.80], stairwell window [.34,0,.54,.34], statue/ghost [.40,.32,.58,.66], forest [.86,.36,.92,.47]. The stairwell's x2/x3 is brightness gain, not crop magnification. The forest zoom is x3. Evidence tool: C:/Users/Frede/AppData/Local/Temp/tgb/missing-managed/evidence/boxes.py (no image writes).

## Step 3: built diagnostic and initial checks

Both managed projects built with 0 errors and 0 warnings. The offline-only restore read already cached .NET reference packages copied into `C:/Users/Frede/AppData/Local/Temp/tgb/missing-managed/packages`; no network package source or SDK download. No native build needed.

The eight analysis tests pass. They exercise 22 deliberately damaged inventories and eight damaged run layouts, plus ordering, unknown-vs-excluded, readable partial source failures, table formatting and the five-scene recipe. Five in-memory implementation faults were detected by assertion failures: disable validation (24), put included objects first (2), turn missing On identities into exclusions (1), ignore source failures (3), ignore inconsistent crop areas (2). Clean tests pass before and after. No production source was mutated for these checks.

Initial required checks: source checks 101/101; existing harness analysis 6/6; launcher parses with no PowerShell errors. Final repetition follows the review edits only.

Exact branch observations were added for Kandra capacity, instance-data, geometry/readback and per-renderer exceptions, and decal unsupported geometry/shader, missing map, distance/fade and accepted-record slots. They run only while the harness arms the diagnostic. All existing conditions, native arguments and return values stay the same.

## Rejection-rule audit (line numbers in this copy)

Each is a suspect until a runtime row ties the inspected object to it. Old comments at the top of SceneCapture and the older project-context summary predate several migrations; the executable gates below take precedence.

| Source location | What it excludes or delays |
|---|---|
| SceneCapture.cs:212,235-251; 2211-2225 | Scans every 2 seconds (sooner on movement/LOD changes); source settings SceneObjects, DrakeWorld, Terrain, Vegetation; disconnected tracer; caught source exceptions. Follow moves objects every frame. There is no current global distance/radius cut in these enumerations. |
| SceneCapture.cs:146-181 | No material array, no positive registered mesh handle, or all submesh materials Invisible. Null individual material is neutral, not an exclusion. |
| SceneCapture.cs:326-348 | Mesh registration queued, six per frame; destroyed queued mesh skipped; failed registration retained as Failed. A later scan must place a newly registered mesh. Delay is not proof of permanent rejection. |
| SceneCapture.cs:351-425 | Empty vertices/submeshes, missing position, unsupported position format/stream, missing vertex/index buffer; native registration failure. Other missing attributes can degrade shading without removing geometry. |
| native/pathtracing/src/extension.cpp:173-211 | Mesh descriptor size/ABI, empty/malformed geometry, index format, position format/dimension, non-buffer resources, unsupported runtime, exception. A positive handle queues asynchronous conversion; it does not prove a completed acceleration structure. |
| native/pathtracing/src/scene.cpp:442-459 | Non-triangle topology, fewer than three indices or an index run beyond the buffer become degenerate triangles. Report marks the known topology/count cases separately from accepted input membership. |
| SceneCapture.cs:1210-1257; GlassFinish.cs:25-37 | DBuffer mesh goes to the separate decal route; WaterGBuffer to the water route; transparent SurfaceType/queue >= 2750/NoRender rejected except the brow and accepted non-Unlit glass cases. Glass recognition itself has authored refraction/name exclusions. Opaque Unlit is accepted. |
| SceneCapture.cs:1595-1695; 1701-1716 | Static batches: inactive/disabled/forceRenderingOff; wrong LOD; camera layer; shadow-only absent unless serving as a stand-in; missing combined mesh; registration pending; batch submesh range not represented; every material invisible. Static batching itself is supported. |
| SceneCapture.cs:2267-2289 | Moving MeshRenderers: cached mesh missing, disabled/forced off/inactive, LOD skipped, layer hidden, shadow-only without stand-in, failed PlaceMesh. Cache of found renderers/material arrays updates on a scan. |
| SceneCapture.cs:1719-1745; 1764-1820 | Drake: no default ECS world/manager; query component requirements; no MaterialMeshInfo or DisableRendering; hidden layer; shadow-only unless stand-in. Mesh/material handles must be loaded, valid, completed, successful, counter > 0. |
| SceneCapture.cs:1832-1941 | Drake LOD range/reference point and maximum LOD; near LOD registered ahead; another ready level can stand in. Missing mesh index/asset/handle and all-invisible submeshes. No entity for a submesh makes that submesh Invisible; missing material on a drawn pair becomes neutral. |
| SceneCapture.cs:1972-2150 | Other Entities Graphics: default world/system, query excludes Drake/DisableRendering; hidden layer/shadow-only; unresolved runtime/baked mesh or missing RendererRange pair pointer; LOD and registration; unrepresented/all-invisible submeshes. A missing material becomes neutral. |
| SceneCapture.cs:2353-2458 | Medusa: disabled manager, invalid BRG, no renderer/transforms, unreadable/truncated matrix archive, out-of-range transform, LOD mask, missing mesh, unrepresented/all-invisible submesh or pending/failed handle. |
| LeshyCapture.cs:19,39-78,107-149 | Disabled/ForceDisable manager; missing prefab/runtime/cell/filtered data; invalid prefab ID or unloaded cell; outside its LOD; renderer LOD mask/no mesh. Billboards removed and replaced by the coarsest mesh; no replacement means omission. Hard cap 1,000,000 accepted instances, nearest cells first. |
| VegetationCapture.cs:67,89-130 | Disabled/no system, unloaded/empty cell data, missing prefab or no mesh-renderer parts. Wanted rejects every type except Tree/LargeObjects/Objects: Grass/Plants are certainly excluded by this adapter. |
| SceneCapture.cs:2609-2693 | Terrain inactive/missing data/failed mesh; resolution < 2. Only the heightmap is captured, at most 257 by 257 samples. Unity treeInstances/detail instances have no adapter here; no claim that the game's inspected plants use those sources. |
| CharacterCapture.cs:535,838-940 | Characters setting; missing/invalid output buffer/pointer; inactive/unregistered Kandra renderer or non-finite bounds; instance data unavailable; 256 renderers, 2,097,152 vertices, first 8 submeshes; missing/empty mesh and no drawn slots; per-renderer exception; failed native SetSkinned. Capacity and per-candidate branches now record their actual decisions when armed. |
| SceneCapture.cs:615-623,1059-1068; CharacterCapture.cs:893,1028-1095 | CharacterLeftOut drops transparent/high queue/Unlit/NoRender/ScalpMask/Stubble with brow/cornea/textured-shell exceptions. Those submeshes get no triangles. Missing index buffer, invalid index range/stride, failed asynchronous readback or registration prevents the character mesh. |
| DecalCapture.cs:66-115,124-135; 356-445 | Renderer/LOD/layer/ShadowOnly gates, camera Decals disabled; non-MeshRenderer or invalid mesh/submesh; registration; unsupported shader/program or required shared maps unavailable; projector inactive/no material/zero size/distance fade <= 0. Actual branch reasons and accepted record slots are recorded while armed. Disabled decal passes clear effect flags rather than removing the record. |
| SceneCapture.cs:1627; CharacterCapture.cs:842; EffectPasses.cs:9-12 | Ordinary Unity SkinnedMeshRenderer, ParticleSystemRenderer, trails/lines and VFX outputs have no geometry adapter in these scene/character routes. Kandra is a separate renderer and IS captured. EffectPasses is a raster pass-state workaround, not traced particle geometry. |
| native/pathtracing/src/extension.cpp:594-620 | Whole SetScene/SetMoving submission refused for invalid pointer/count, too many instances/material indices or failure. Inventory records return codes and keeps last accepted data visibly stale; analysis fails stale/refused evidence. |

## Decisions and practical limits of this diagnostic

- Production capture functions are never invoked by the census. In particular, it does not call MaterialIndex/ReadMaterial, MeshHandle, SceneCapture.Tick/Follow, the mutable light classifier, or MedusaMatrices. Source enumeration does not depend on which objects the tracer accepted.
- Source coverage includes Unity mesh/static-batch/skinned/particle/trail/line renderers; every loaded ECS world's Drake/Entities Graphics pairs (runtime registry, baked arrays and RendererRange pairs); Kandra including registered draw materials; Leshy loaded filtered instances and billboard alternatives; Vegetation Studio loaded instances; Medusa's game-owned matrix archive; terrain heightmaps, trees and engine-generated detail transforms/cards; decal projectors; VFX renderers/materials/alive counts. No scan radius, result count or triangle cutoff is added.
- The census reads existing data, waits for existing ECS/VSP jobs before reading their arrays, and reads Medusa's immutable matrix archive independently of the tracer's cache. It does not request a draw, simulate particles, bake a mesh, create a material, open/copy textures or create an HDCamera. The existing public Leshy RuntimePrefabs description method supplies current quality/LOD descriptions; the tracer's cached prefab description is untouched.
- Boxes use normalized top-left coordinates. Bounds edges are clipped against the camera near plane before projection, then intersected with the boxes. A renderer can overlap several boxes. Projected bounds can include occluded objects and empty space; this is a conservative census, not a claim that every row writes a visible pixel. isVisible is labeled as any-camera evidence, never used as proof for the main camera.
- VSP's internal instance visibility and Unity tree billboard/LOD selection are not established from prefab bounds; their gameCandidate is unknown, not true. GPU VFX geometry/triangle counts and terrain-generated card shaders/counts are unavailable through these APIs and remain null/explicitly unknown. VFX without a renderer exposing bounds, unreadable source arrays, or other source failures produce an incomplete-coverage issue, not a zero count. Readable rows remain in the report even when another source fails.
- Native mesh/transform matching uses the accepted input lists, including the replacement mask for moved static instances. SetScene/SetMoving/SetSkinned/SetDecals slots are labeled by route, not misrepresented as a GPU TLAS index. Character/decal exact decision events supplement that matching. A submitted mesh may still be waiting for asynchronous GPU conversion; accepted input membership does not prove ray visibility, correct alpha/texture/shading, or that a visual defect is fixed. No native per-mesh completion API exists in this diagnostic.
- The report preserves partial geometry/material slots, cached native material index/flags/program, every shader keyword, shader scalar/vector/colour properties, pass enables, texture identity/size, source mesh and triangle counts. There are no copied texture pixels. Off and On material records both remain available, including the tracer's existing pass/queue changes.
- A missing On identity remains unknown; it is never fabricated into an exclusion. Material acceptance alone never establishes instance membership. Unknown/pending/included objects are not counted as proven omissions. A runtime source that is absent is reported as an empty successful census, distinct from a source API failure.
- The analysis rejects absent/malformed inventories, missing source sections, unreadable sources, missing images/metadata, PT state mismatch, unfrozen worlds, invalid rectangles/overlap areas, duplicated IDs, stale/refused submissions, unsupported inclusion evidence and changed camera pose/FOV/size. It exits nonzero on incomplete evidence while retaining readable candidate data.
- The diagnostic can stall CPU work for enumeration/serialization. No frame-rate or GPU-time conclusion should be drawn from this recipe. The world is frozen but effects using other clocks can still move. Their Off/On identity correspondence is evidence, not guaranteed identical geometry.

## Workarounds met, none removed

The transparent/name-based glass gates; character brow/cornea/shell exceptions and raster hair hiding; static shadow-only stand-ins; Leshy's billboard-to-mesh replacement; VSP grass/plant exclusion; heightmap-only terrain; capped Kandra/Leshy lists; EffectPasses' raster distortion/motion changes; and decal special routing remain exactly as found. They are listed for the coordinator, not presented as fixes. Removing any would exceed this diagnostic-only brief. Source/material/scene inspection, not appearance guesses, chooses the next fix after the run.

## Files for the coordinator's merge

- New: `src/TGPathTracing/MissingWorld.cs`, `MissingRenderers.cs`, `MissingEntities.cs`, `MissingVegetation.cs`.
- Passive observation hooks only: `src/TGPathTracing/NativeTracer.cs`, `CharacterCapture.cs`, `DecalCapture.cs`; `TGPathTracing.csproj` references the game's existing Json.NET (Private=false).
- Harness: new `src/TGPathTracing.Harness/MissingInventory.cs`; `Request.cs`, `Runner.cs` opt in and disarm.
- `tests/ingame/scenarios.json`: missing recipe and six boxes beside the five existing scenarios; no other recipe behavior changed.
- `tools/harness/Run-PtHarness.ps1`: pass the boxes and route analysis; new `tools/harness/missing.py` and `tests/harness_missing_test.py`.
- This report. Scratch scripts/logs are evidence only, not part of the merge.

Coordinator-owned files, production SceneCapture/LeshyCapture/VegetationCapture and the native sources were not changed. BACKLOG/ROUNDS/PT-REQUESTS/PROJECT-CONTEXT match base67 byte for byte. No settled rendering decision was changed, no workaround removed, no game/launch/deploy/commit/push, no new SDK/package download and no GPU test. The runtime identities and the cause of each of the five inspected scenes are deliberately still unanswered until the one run.

## Final validation and hand-over

- TGPathTracing and TGPathTracing.Harness: Release net472 builds, 0 warnings / 0 errors, output only in this copy's bin folders. No native source/ABI change or native build.
- `python tests/source_checks.py`: 101/101 pass.
- `python tests/harness_analyze_test.py`: 6/6 pass.
- `python tests/harness_missing_test.py`: 8/8 pass; 30 deliberate evidence corruptions rejected.
- Five in-memory analysis faults: all detected by assertion failures; clean suite passes before/after. No altered production source left behind.
- Launcher PowerShell syntax: no errors. All 16 deliverable files use CRLF. Every hunk of the eight changed existing files reviewed against base67; protected coordinator files unchanged.
- Scratch evidence: `C:/Users/Frede/AppData/Local/Temp/tgb/missing-managed/evidence/` (build logs, test logs, mutation log, crop-box recovery script, existing-file diff and deliverable list). It is outside the copy and should not be merged.

Stop here. The only next action for the coordinator is the one run at the top, after the managed plugin is installed. This worker has not attributed a visual defect to a particular runtime source and has proposed no rendering fix.
