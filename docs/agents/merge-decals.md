# Decals integration (PT-63 part 5 / PT-71 item 2)

Status: merged decals/base40 into merge5/base47. Plugin builds (zero warnings/errors),
93 source checks pass; native targets build. The one GPU attempt stopped at startup:
41 ok, one FAIL, before rendering checks. Final stable-source rebuild finished;
that DLL was not GPU-tested (no rerun). NRD OFF. Not a rendering-validated handoff.
No game launch, deployment, commit, source-copy or main-tree writes.
Evidence: `%TEMP%/tgb/merge5-*`; normalized review diffs in `merge5-work`.

## Conflicts resolved (56 blocks in 13 files)

- CMakeLists (1): add only the decal graph PT dependency; keep waves/materials/
  dielectric and all Neural Rendering build rules. No other mod source changed.
- tgpt_api (2): MeshDesc keeps UV4 and appends tangents; Instance 68, Moving 116.
- pt_caustics (1): retain deletion of the old wave functions. Their one changed
  fetch_hit call now passes ray.Origin in the shared pt_waves helper.
- pt_common (5): keep previous-transform constants, UV4, tinted scattering,
  material programs, wet ground, unlit and optical normal handling; add decoder
  helpers and ONE apply_decals call immediately before hit_material returns,
  after all those terms, so programs/wetting cannot overwrite the decal. Carry
  the wet reflectance into the base used for partial decal paint. Do not
  revive trace_see_through (removed by the glass migration).
- extension (3): capture both UV4/tangents; keep SetMaterialPrograms and add
  SetDecals; clear both along with frame-owned scene/moving lists. VFX observer stays.
- runtime (1): retain both UV4 and tangent stream metadata.
- scene.cpp (11): retain program/subsurface intake plus decals; upload/free all
  UV4/tangent/float-colour streams. Keep previous-motion index AND receiver layer
  in every static/water/cloth/skinned/moving GPU record. Plant groups carry each
  source's layer per geometry (the group-wide layer is unused); include layers
  in group identity so a layer-only change invalidates the cached group.
  Keep both jobs' buffer release paths.
- scene.hpp (3): combine mesh fields/buffers; GPU instance gets a distinct layer
  word after previous. No field repurposed from moving.
- tracer (1): keep wet constants and six-per-material yardstick; add one decal
  row. Restore both descriptor heaps after NRD/RRG, including the water pass.
- NativeTracer (4): keep UV4/moving/material exports and add tangents/decals;
  one ABI, all size pins updated.
- SceneCapture (18): keep SubmeshMaterials and frame-follow logic, remove only
  CountDecals and obsolete scan-motion code. Keep graph exposure arithmetic,
  expose it for decals. Keep UV4/vertex colour plus optional decal tangents.
  Preserve layer grouping and metadata through static batches, Drake/Entities
  LOD lists and both still/moving submission. Followed MeshRenderers read their
  live renderingLayerMask; Kandra and terrain retain the incoming layer capture.
- GPU test (2): retain every existing export/fixture, add decals and count the
  three new fixture meshes (seven total). Added two image checks for layers on
  TGPT_Moving: identical paint when enabled, original surface for the wrong layer.
- source_checks (4): keep graph exposure and moving rules; reconcile size/row
  counts. Pin decoder ordering, layer metadata and the plant cache key.

## ABI and record sizes

ABI **0x00012200** in header, managed code and source checks; reason in the checks'
ABI history. Previous fields retain their meanings. Moving has the same first
68 bytes as Instance, then its 48-byte previous transform.

| Record | Bytes |
| --- | ---: |
| TGPT_Decal / NativeDecal / shader DecalRecord | 928 |
| MeshDesc / NativeMeshDesc | 192 |
| Instance / NativeInstance | 68 |
| Moving / NativeMoving | 116 |
| SkinnedInstance / NativeSkinnedInstance | 100 |
| Material / NativeMaterial / MaterialRecord | 204 |
| GPU MeshRecord; InstanceRecord; GeometryRecord; DecalNode | 48; 20; 16; 48 |
| FrameConstants (native and HLSL) | 6528 |
| Frame; Status; Yardstick; Guides | 600; 216; 632; 64 |
| CpuMeshDesc; Submesh; Light; SharedTexture | 56; 16; 80; 96 |
| HairLobes; ClothWind; PlantWind; PlantWindFrame | 48; 48; 48; 32 |
| WaterCaustics; WaterShade | 192; 568 |

MeshDesc offsets: UV4 160, tangents 176. MeshRecord: UV4 36, tangents 40,
float colours 44. InstanceRecord: previous 12, layers 16. Moving: layers 64,
previous 68. No new layer/option bit conflicts; receiver-disable material bit
33554432 stays the decals job's. New export: TGPT_SetDecals.

## Coordinator records and remaining gaps

The incoming job added NOTHING to BACKLOG, ROUNDS, docs/rounds, PT-REQUESTS or
PROJECT-CONTEXT: verified against base40. These files are restored from base47.
The source job's full design/evidence stays in [decals.md](decals.md).

Still open, inherited from that report (not made complete by this merge):
- GPU VFX/particle, skinned and non-Renderer Entities/Drake decal geometry.
  Unsupported Renderer slots are counted; non-Renderer counts remain unknown.
- Mesh raster view/depth bias, exact equal-priority OptimizeStateChanges order,
  curved/nonuniform tangent interpolation, ray/raster texture derivatives.
- WaterCurrentDecal uses water-current passes, owned by water work.
- Shared-texture resources unavailable under the registry's constraints: logged,
  no copied atlas substitute added. The source report's below-64-KB rejection
  is already removed in base47 (wants() has no size test); do not reopen it.
- Specialized glass/cloth shadow tint bypasses hit_material. The old unused
  see_through_cover helper remains inherited; it was not restored as a camera path.
- Receiver layer source for Medusa/Leshy/VSP remains the existing default 1;
  their metadata capture is not proven. Records and plant grouping preserve the
  layer supplied. Kandra's live layer is now carried through its specialized path.
- Tiny glowing decals are reached by paths but not explicit emitter sampling,
  so they are noisier. Full graph compilation/startup cost remains.

Removed workarounds: count-only decal scan/log, DBuffer mesh materials treated as
solid opaque scene geometry. No overlay, texture copy, overlap cap, fake light or
HDRP material borrow added. All other merge-4 cleanup stays in place.

## One in-game scene (coordinator only)

`hair-moving` (save `PT hair-moving-when-character-moving`, Ron's Tavern), the
normal None-first then NRD recipe; no RR yet. Older inventory: 254 projectors,
108 glowing, 16 materials, NOT this build's captured count. Identify a visible
blood/dirt/wet patch in that saved view before grading; loaded counts alone cannot
prove one is visible. Compare its traced colour/normal/finish to Off in the same
pixels, with unchanged depth/place. A reflected patch must keep its decal; a
nearby surface must receive its glow where present. If the save shows no visible
patch, report that limitation; do not claim in-game coverage from counts.
Read managed capture ms/projector/mesh/shader-gap/missing-map counts and native
ready/unavailable, bytes, tree reuse and upload ms. Check patch-local yardstick
agreement and exposure/gain after glow changes. Owner judges appearance.

## Validation

Plugin: built net472, zero warnings/errors. Source checks: 93/93 initially;
final run after integration assertions also 93/93. Managed reflection confirms
MeshDesc 192, Instance 68, Moving 116, SkinnedInstance 100, Material 204;
full Validate/NativeDecal reflection requires UnityEngine.CoreModule, absent
from the isolated output folder, so no full reflection-Validate pass claimed.
DXC reflection confirms constants 6528 (decals at 6336, primary 6352, wet 6368),
instance stride 20, mesh stride 48, material stride 204 and decal stride 928.
Both native targets built; the four overlapping compiles were rebuilt from stable
sources and relinked successfully. Native assertions compile; no stale shader remains. Serial NMake compile switched to existing Ninja (four compile
jobs) for speed; only this worker's build processes stopped. No tool installed.
GPU: ONE full-suite attempt, **41 ok / 1 FAIL**, exit 1. Its only FAIL line:
`FAIL: the passes were made`
The harness waited its 1800 x 100 ms startup window; status.starting remained true,
startSeconds was 0. Native timestamps span about 196 seconds without a pipeline-ready
line. The existing failures guard after scene-shader setup then returned before
warm-up/rendering. No decal, moving-layer, optical or lake timing fixture was reached.
No GPU rendering pass is claimed. No stop-at-first-failure flag, mutants or rerun.
The four known baseline failures were NOT reached. This startup result needs the
coordinator's PT-74/startup work before the rendering verdict; no unmeasured change to
startup limits or shader behaviour was made to suppress it.

### Validation caveat: the one GPU run started before the final four-shader rebuild

A brief source-ready marker opened the GPU gate while I was invalidating generated
outputs. Ninja considered lowered output timestamps clean; deleting the four generated
headers correctly forced the rebuild. The gate had already passed its readiness check.
I did not stop/restart the GPU suite: the brief explicitly says one run and no reruns.
The single run tests native SHA-256
`DC5E02AEAFA38367B31209657436FDF6F82B11A2A8E54A1BA2EF83CC2AF6D292`.
It contains the final ABI/record/layer/decals integration and both new moving-layer
checks, but its three GI variants and SM6.9 prepare were compiled while the last
one-line wet receiver colour assignment landed. Those four shaders were rebuilt
from stable final sources. The tested and final hashes must remain distinct in the
handoff; the final rebuilt DLL is NOT claimed to have run the full suite.
Evidence: merge5-work/overlap-outputs.txt, merge5-build-first.log,
merge5-build-incremental.log, merge5-build.log, merge5-hashes.json and merge5-gpu.log.

Final native/private-stage SHA-256 (matched, untested on GPU):
`8DCB1E16A4245EC8ABEE5C539D132055E875583B95DB8E62CA9D078B0D84D6CE`.
Test-loaded DLL hash verified against the single-run hash above; test exe:
`F47DA5A2C8D65C8948427367E80208D356B8A0CA14D8E012D34B16821E1AEAD7`.
Final logs: merge5-build.log (six rebuild/link steps), merge5-source-final.log
(93/93), merge5-managed.log (zero warnings/errors), merge5-final-hashes.json.
Protected coordinator records rechecked byte-for-byte against base47. No active
worker build, GPU test or slot waiter remains. No GPU images were produced.
