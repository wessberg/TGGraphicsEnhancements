# Decals seen by rays - PT-63 part 5 / PT-71 item 2

Status: projector and MeshRenderer decal implementation validated offline in this worker copy.
The full GPU suite,
restored decal fixture, 24 rendering mutations, 19 shader-program mutations, 90 source checks
and the net472 plugin build pass. In-game validation and the coverage gaps below remain.
This is not a claim that the complete renderer migration or in-game decal coverage is finished.
No game launch, deployment, commit, or main-tree write by either worker.

## Design and implemented coverage

The material is changed once, at the end of `hit_material`, by `apply_decals`. Camera,
reflection and bounce hits use that same call. HDRP's DBuffer is never the source, and no
post-denoiser image is added. The ordinary material decoder is otherwise unchanged: its diff against base40 is exactly
the added `apply_decals` call.

Projectors: capture the live HDRP `DecalProjector` box, pivot, scale mode, transform, maps,
channel/blend controls, emission, UV transform, distance/angle fades, draw order and layers.
Capture runs immediately before SubmitFrame, after LateUpdate. `DecalController` animates
ordinary projector fades; `StaticDecalsCuller` enables/disables ordinary projectors and manages
mip streaming. Neither requires an invented second source. Equal draw-order material sets
follow HDRP's reverse dictionary order, and projectors within a set follow handle-index order.
No camera-frustum filtering removes off-screen projectors from reflections.

The spatial index is a stackless ordered interval BVH. It has 2*N-1 nodes and keeps the exact
input draw order without a temporary overlap list or a fixed count. Subtrees outside a hit
are skipped; overlapping decals still cost O(N), explicitly. A grid would need an arbitrary
cell size and still need ordered merging; no evidence justified that extra machinery.
Unchanged bounds reuse the tree. Animated values still update the material records.

MeshRenderer decals use actual registered triangles, submeshes, UVs, normals, tangents and
full float vertex colours. Their private one-instance ray acceleration structure is cached
until the transform or source geometry changes. The incoming camera/reflection/bounce ray
is tested against those triangles up to its receiving surface. Coplanarity allows only floating
point roundoff, not an invented projection thickness. All intersections are blended in
primitive order, with repeated traversal and no layer-count limit. Mesh draws precede
projectors. Sorting uses sorting layer/order, render queue, renderer priority, material and
renderer IDs. Exact engine OptimizeStateChanges tie ordering remains unproven (below).

Mesh capture includes static batches, material slots and MaterialPropertyBlock overrides;
per-slot blocks replace renderer-wide blocks as Unity does. Extra tangent/float-colour storage
is requested only for decal meshes. These materials are excluded from ordinary opaque scene
geometry, so the decorative mesh does not become an invented solid object or cast an opaque
shadow. The old arbitrary 256-submesh registration rejection was removed.

Fifteen shipped DBuffer shader assets have decoders. Paint/emission passes and quality
variants are decoded separately:

- HDRP/Decal; TG/Decal/Decal_Mask; Decal_Wet; Decal_AlphaErosion; Decal_Wet_Ocean;
  Shader Graphs/Decal_Lit_EmissionPanning; Shader Graphs/VFXGraph_Decal_Blood.
- KriptoFX/BFX_Decal; Decal_EmberBurning; Decal_WyrdGhost;
  Decal_RadialGradient_3Panning and its Lit variant; Decal_GlowingCracks (high/medium/low);
  VFXGraph_Decal_FireGround; VFXGraph_Decal_ForeDwellers_Annihilation.

The sixteenth named decal asset, VFX_WaterCurrentDecal, has LargeCurrent/RipplesCurrent
passes rather than DBuffer passes. It belongs to the water worker.

Graph arithmetic was translated from the shipped compiled programs, using exact hexadecimal
float constants and recorded parameter/texture layouts. No guessed common colour program
replaces an unknown shader. Live graph inputs include time, exposure/probe exposure, saturation,
world transforms and view direction. Parallax follows the incoming ray, including reflections.
The original programs and extracted game data remain in private evidence, not the repository.
No RTXDI/NRD source was copied or quoted; HDRP source was studied, not copied.

The shipped HDRP/Decal projector variants are 26/26 DECALS_3RT and 0/26
DECAL_SURFACE_GRADIENT. Capture reads the actual pipeline perChannelMask setting: three-target
mode changes smoothness; four-target mode also changes metal/AO. Colour, normal and finish
have separate opacity sources. Normal accumulation is normalized once; DBuffer colour clamping
and the Mask graph's double-unpacked default bump are preserved.

Textures remain the game's shared resources. Explicit shader defaults use constants, not
allocated copies. Streaming resource replacements are watched. Texture filter, wrap, anisotropy
and mip bias come from the live texture; graph texture/sampler bindings preserve reuse and the
shipped inline Linear/Repeat sampler. There are 816 sampler descriptors for the finite API
combinations, not a decal limit. ForceEnable anisotropy behavior follows
[Unity 6000 Texture.anisoLevel](https://docs.unity3d.com/kr/6000.0/ScriptReference/Texture-anisoLevel.html).
The root signature enables direct sampler indexing; every tracer heap bind restores both heaps.

Emission is real path emission. It is kept separate from a receiver's existing emitter-sampling
suppression, so a glowing decal on an already sampled emissive receiver is not accidentally
removed. `path_emission` calls the same blender again only when the receiver has an emitter
sampling weight below one, to separate its unsampled decal emission; this is an extra decal
query at those hits, not another image layer or ordinary material decode. Decals are not
inserted into the explicit emissive-triangle light sampler; small glowing
patches therefore have more noise, but their light is reached by traced paths. No fake lights.

## Validation and evidence

All evidence paths below are under `C:/Users/Frede/AppData/Local/Temp/tgb/`.
The native build uses NRD OFF, read-only SDK roots from main, and only TGPathTracing.Native
and tgpt_gpu_test targets. Fresh DLLs are copied to this worker's private stage and hash-checked.
Each GPU run waits in one script for the main harness lock and main `-GpuCheck` exit 0.
Every native image fixture uses Denoising None; a mutant exits at its first FAIL.

Before extending the first worker's implementation, `decals-gpu-resume-full.log` passed the
whole suite: 23,758 frames, zero failed frames, all checks passed. Camera red/green was
0.798828/0.039520; mirror red/green 0.279063/0.020081; camera finish difference 0.007504;
glowing-floor wall red 1.628647 versus zero off. All ten initial API/data mutants were killed
at their intended check (`decals-mutations.json`). The early incorrect vertical test-mask sign
was fixed before these runs.

Mirror-only colour, normal and smoothness mutations were also killed. The first normal mutant
survived because the mask included the lower mirror edge and changed foreground. Restricting
the mask to the reflected patch interior gave normal difference 0.01545673 versus mutant
0.00000002; smoothness difference 0.00914030 versus mutant zero. Evidence:
`decals-gpu-core-baseline.log`, `decals-gpu-normal-mutant-core.log`,
`decals-mirror-smoothness-mutation.json`. The first report's claim that mirror shading ignores
secondary roughness was wrong: the code uses shade_seen, which respects it. That claim is
retracted; it must not be added to the backlog.

The remaining shader arithmetic has an independent D3D11 comparison against the original
compiled pixel programs: 19 paint/emission/quality programs pass, including nonzero parameters
and point-filtered gradient textures. HIGH/MEDIUM generally match exactly; LOW's largest
absolute differences were 0.00000530 paint and 0.00002684 emission. All 19 output mutations
fail their intended comparison (`decals-reference-mutations.json`, all correct=true;
`decals-gpu-reference-final.log`). The comparator uses per-channel error/max(1,abs(reference))
<0.0003, so very bright HDR emission is checked proportionally.

This is arithmetic evidence, not proof of identical filtering in game. With anisotropic gradient
textures, WyrdGhost showed maximum difference 0.01584: raster quad derivatives and ray-footprint
forward differences diverge, especially after texture-dependent UV changes. Point-filtering
isolates the formulas; it does not fix that filtering difference. Ember's large HDR emission
error was 0.385986 absolute but 0.000169 proportional. This remains a disclosed limitation.

Final integrated fixture `decals-gpu-final-baseline.log`: passed, zero failures. Native compilation
succeeded; initial pipeline creation took 100.8 seconds (driver compilation, background threads).
The expanded fixture passes original acceptance,
texture wrap modes, actual mesh camera/mirror/normal/finish, depth rejection, submeshes, all
intersected primitives, mesh/projector order, native BFX decoding and mesh bounced emission.
All 20 final API/data/geometry mutants failed at the intended first FAIL
(`decals-final-mutations.json`, all correct=true). The full final-DLL regression passed:
23,882 traced frames, zero failed frames, last error 0, all checks passed
(`decals-gpu-final-full.log`). All four final reflection-only shader mutations failed at their intended first FAIL: projector
colour, normal and finish, plus mesh colour (`decals-final-mirror-mutations.json`, all correct=true).
They alter only the reflection shader; the camera checks still pass. The last waited through
the coordinator harness lock. The original source is restored, temporary mutation code removed,
and the normal DLL has rebuilt successfully. Its final restored-artifact decal fixture passed
with zero failures on 2026-10-08 (`decals-gpu-final-restored.log`). Total final mutation evidence: 24 rendering mutations and
19 independent shader-program mutations.
I inspected the final mesh camera/mirror and glow images: the red floor is reflected in the
mirror and the wall receives red bounced light. Native BFX red/green is 0.798828/0.039520;
mesh/projector camera difference is exactly zero in this fixture.

Final source checks: 90/90 pass (`decals-source-final.log`). Final net472 plugin build:
zero warnings/errors (`decals-managed-final.log`). Validated full-suite DLL SHA-256:
`BB2E4B5F0D107577EC064452B8BDBC37EEB1DC592824A26052465A3162C870B4`.
After all mutations were removed, the rebuild and private stage match SHA-256
`F854ADB33D014EF3A6BBF8E2A9C6A18CE07673EA8AE9340A32F316A8F2D51E7B`.
The build, private stage and actual final test-loaded DLL all have that hash. The restored
fixture passes in `decals-gpu-final-restored.log`; its contention-heavy timings are not used
as performance evidence. This worker has no active builds/tests or mutation edits left.

## Counts, cost and the coordinator's one scene

A read-only coordinator log supplies one real scene count, from the older count-only plugin:
`main/artifacts/harness/20261007-224654/LogOutput.log:254` reports **254 projectors,
254 with materials, 108 glowing, 16 materials**. Its progress log identifies `hair-moving`,
save `PT hair-moving-when-character-moving`, Ron's Tavern, camera (3.2,502.2,-4.4).
Those are loaded-scene counts, not this build's captured/ready counts or proof of a visible patch.
There are still no measured active mesh counts for this build.

Use that scenario for the coordinator's one run if its saved view contains a decal. Otherwise
request a save facing blood, moss or a wet patch on a floor/wall, with a second surface nearby.
Do not infer visibility from a light named StonehengeDynamicDecals. Keep the None-first recipe.
Read the new managed projector/mesh shader-gap/missing-map logs and native ready/unavailable
counts. Compare yardstick colour agreement inside marked decal pixels before/after, with depth
and surface agreement held fixed; the whole-screen colour share can hide a missing small patch.
Read the exposure/gain log before judging changed brightness: restored glowing decals change
the light available to auto exposure. The owner judges the image. No in-game result is claimed
by this worker.

Managed capture reports elapsed milliseconds and counts every ten seconds. Native upload
reports ready/unavailable counts, record/tree bytes, mesh acceleration bytes, upload CPU time,
and whether the ordered tree was reused. The first one-projector native upload measured
0.6341 ms, including initial allocation; that is not a steady per-frame CPU figure. Storage is 928*N + 48*(2*N-1) bytes plus real mesh
attributes/acceleration structures. Mesh primitive traversal is O(K) complete queries for K
intersections, currently a correctness-first cost. The fixture measures 32 frames after eight
warmup frames at 1280x720, None: off, one projector, 257 overlaps, one mesh, BFX, off again.
Measured GPU milliseconds (mean, then min/max), RTX 5080:

| Fixture | Mean | Min-max |
| --- | ---: | ---: |
| Off | 1.2935 | 1.2629-1.3512 |
| One projector | 1.4045 | 1.3244-2.3956 |
| 257 overlapping projectors | 9.2996 | 8.2835-11.0358 |
| One mesh | 1.4451 | 1.4002-2.0044 |
| BFX graph | 1.3657 | 1.3406-1.4064 |
| Off again | 1.2911 | 1.2668-1.3289 |

The gate admitted the run and the before/after baselines agree. Relative to their mean, one
projector costs about 0.112 ms, one mesh 0.153 ms, and 257 overlaps 8.007 ms in this tiny scene.
These are not an in-game performance verdict or the CPU capture cost. Other background work
was not controlled throughout; the real scene needs the coordinator's capture/frame logs.

## Remaining gaps and workarounds

Removed: `SceneCapture.CountDecals`, its scan and misleading count-only summary; replaced
by real per-frame capture. DBuffer mesh materials no longer become ordinary opaque geometry.
No camera-only decal overlay, DBuffer material substitute, overlap cap or texture copy added.

Still not complete, and not hidden behind a passing fixture:

- GPU-generated VFX/particle, skinned and non-Renderer Entities/Drake decal draw geometry needs
  the pose/particle owners' geometry stream. MeshRenderer geometry is implemented; unsupported
  Renderer material slots are counted. Non-Renderer draw counts remain unknown. No evidence
  permits calling them negligible or the whole PT-71 item finished.
- Raster mesh view/depth bias is counted (`raster-biased`) but not applied to ray triangles.
  Compiled vertex programs shift view-biased vertices toward the camera and clip-depth-biased
  vertices in clip space. No arbitrary world-space projection thickness was substituted.
- Exact equal-priority mesh OptimizeStateChanges ordering, curved/nonuniform mesh tangent-frame
  interpolation and ray-versus-raster texture derivatives still need game comparisons. Flat
  fixture meshes cannot establish those cases. LOD selection uses the existing WorldRules path.
- The shared texture registry (`texture_share.cpp`) rejects resources below 64 KB and may reject
  under memory pressure or when the creation hook missed a resource. Decals report a missing
  map instead of borrowing ordinary materials' copied atlas fallback. Removing that global
  sharing restriction is a broader PT-45 migration gap; no copy was added here.
- `glass_color`, `see_through_cover` and `cloth_tint` bypass the common material decoder for
  specialized transmission/shadow tint. Their decals are not covered by this common call.
  Coordinate their cleanup with the glass/eyes/material workers. Opaque shadow visibility does
  not need a decal's paint, but tinted transmission does.
- The pre-existing camera-only unlit route is distinct from DBuffer mesh decals and remains
  owned by the other migration workers. Receiver layer metadata for character-specialized
  paths and Medusa needs merge review. No animation algorithm was changed here.
- Explicit light sampling of tiny glowing patches remains a noise improvement, not missing
  path energy. The new full graph programs also impose shader compilation/startup costs.

## Files, ABI and reproduction

New capture: `src/TGPathTracing/DecalCapture.cs`, `DecalGraphCapture.cs`.
Native export/data: `tgpt_api.h`, `NativeTracer.cs`, `runtime.hpp`, `extension.cpp`.
Capture integration: `HdrpPasses.cs`, `TextureShare.cs`, `SceneCapture.cs`, `CharacterCapture.cs`.
Native geometry/index/upload: `scene.hpp/cpp`; sampler heap/frame inputs: `tracer.cpp`.
`CMakeLists.txt` changes only the PT shader dependency list, adding `pt_decal_graphs.hlsli`
so edits to the decoded programs rebuild the passes; no other mod build rule changed.
Shared hit integration/graph helpers: `pt_common.hlsli`, `pt_decal_graphs.hlsli`; ray origin
propagation: primary/compose/caustics shaders; direct sampler heap: `pt_root.hlsli`.
Fixtures and existing consistency checks: `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`.

ABI changes requiring the coordinator's version assignment before deployment:

| Layout | Before | Now |
| --- | ---: | ---: |
| TGPT_Decal / NativeDecal (new export TGPT_SetDecals) | absent | 928 B |
| TGPT_MeshDesc / NativeMeshDesc | 160 B | 176 B |
| TGPT_Instance / NativeInstance | 64 B | 68 B |
| TGPT_SkinnedInstance / NativeSkinnedInstance | 96 B | 100 B |
| Internal MeshRecord | 36 B | 44 B |
| Internal DecalNode (new) | absent | 48 B |
| Internal FrameConstants | 7120 B | 7136 B |

MeshDesc appends tangent stream/offset/format/dimension; reserved bit 0 requests decal vertex
attributes. Instances append receiver layers. Material bit 33554432 disables decals on a
receiver. No option bit. ABI number deliberately remains 0x00012000 for the coordinator.
The sampler heap is additional state that other workers' heap-bind edits must preserve.

Tracked research tools in `tools/research/`: `lift_decal_graphs.py` reads private extracted
shader trees/blobs and writes the graph decoder plus capture layout; `decal_capture_bindings.py`
emits C# binding cases; `decal_reference.cpp`, `.hlsl` and `_data.py` reproduce the independent
original-program comparison. Run their help/argument checks for input locations. Original
shader data is deliberately not checked in. Private scripts `decals-build.cmd`, `decals-gpu.ps1`,
`decals-reference-gpu.ps1`, `decals-final-mutations.py` and reference/mirror mutation scripts
record this worker's exact builds and bounded runs. GPU staging labels preserve their DLLs.
The tracked lifter was rerun into `decals-repro`: its generated math exactly matches the checked-in
decoder after the three-line provenance header. The review diff normalizes base40's LF versus
this copy's CRLF; the source changes themselves are preserved. Final review covers 27 source/build files
(`decals-files.json`, `decals-diff.patch`); coordinator-owned documents are byte-unchanged.

The coordinator-owned backlog/round/context files were not edited. The main tree, installed
mods and running game were never modified. All intentional outputs are in this copy or the
private `Temp/tgb/decals-*` build/evidence folders.
