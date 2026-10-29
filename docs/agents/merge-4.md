# Four-job integration (PT-71 items 1, 3, 4, 6)

## State
2026-10-07 22:39: all four jobs merged in order; native targets and plugin build;
93/93 source checks, managed Validate and WorldRules pass. Final smoke attempt:
**137 ok, 0 FAIL; interrupted when another session launched the game.** The tracer
started and rendered without a hang; this is not a completed GPU-suite result.
Per the owner's 22:20 single-smoke/speed instruction, the waiting automatic retry
was cancelled (only this worker's wrapper). The tree is ready for coordinator review
and game validation; no whole-suite pass or appearance approval is claimed.
NRD is OFF. No game launch, deployment, commit, main-tree write or source-copy write.
No intermediate GPU suites, new mutants or unfinished-test reruns after resuming.

## Merge resolutions
- **port2/base35:** 26 changed, seven added, one deleted; four conflict blocks.
  PT-REQUESTS restored to base42. CHANGELOG keeps both entries. tracer.cpp keeps
  plant wind plus wet clock. GPU test keeps plant and scattering CPU references
  as separate functions, with the shared closing brace restored. pt_sss deleted;
  walk/wet/waves added; obsolete diffusion/transmission exports deleted.
  Native/plugin built; 91 source checks. First worker's full run: 1369 ok,
  one lake timing FAIL, shared GPU. Three interrupted attempts were discarded.
- **mats/base36:** 21 changed, three added; 12 conflict blocks in nine files.
  PROJECT-CONTEXT restored. CHANGELOG keeps both entries; CMake keeps waves and
  adds materials. Header/NativeMaterial/MaterialRecord keep subsurface in the
  old reserved bytes, then append program/row/emission/IOR. pt_common keeps wet
  helpers and tinted SurfaceMaterial; program evaluation precedes leaf/wet terms.
  scene.cpp keeps subsurface counts and program rows. tracer.cpp keeps 6512-byte
  constants, plant wind/wet clock, adds saturation and six yardstick counters per
  material. NativeTracer keeps plant sizes, pins yardstick 632; source checks agree.
  GPU test loads both plant/wind and material exports. Native/plugin built on
  resume; 92 source checks. No materials GPU run.
- **moving/base39:** 18 changed, four added; 14 conflict blocks. PT-REQUESTS
  restored. CHANGELOG keeps both entries. Header keeps programs and adds SetMoving.
  extension.cpp/runtime.hpp keep program rows and adopt frame-owned scene/moving
  lists. scene.cpp keeps scattering/program intake and both buffer cleanup paths;
  scene.hpp keeps UV4 (MeshRecord 40) and previous-transform index. tracer.cpp
  keeps wet rows plus primary.w previous-transform slot. NativeTracer retains
  materials/UV4/yardstick sizes and adds Moving 112. SceneCapture keeps both reset
  paths and both yardstick summaries. GPU export list keeps all jobs; source
  assertions keep exposure-program behavior and remove the old MOVING flag.
  Native/plugin built; 93 source checks. No moving GPU run.
- **glass/base37:** 30 changed, four added, one deleted; 43 conflict blocks.
  PT-REQUESTS restored; all other coordinator records restored too. CHANGELOG
  keeps the glass entry and coordinator's existing Neural Rendering status.
  CMake keeps waves/materials/walk/wet, adds dielectric, deletes hdrpShadow.
  Header (2), NativeTracer (3), MaterialRecord (1): one IOR shared with materials;
  append absorption/model/padding, Material 204. Other NativeTracer sizes stay.
  pt_common's other eight blocks remove shading-share/cover fields, retain the
  scattering removal of thin-skin fields, run programs before Unlit, retain wet
  treatment and add optical interfaces to the tinted scattering/cache result.
  pt_prepare removes glass/HDRP bits without reviving thin-skin terms;
  pt_primary admits glass back faces while retaining port2's head-distance removal.
  pt_specular's three blocks delete shell/cover transport and retain the new
  optical walk, without reviving the old head-reflection flag. tracer.cpp's two
  blocks delete hdrpShadow and keep walk/wet. PathTracingRow (3)/Settings (1)
  delete Shadow Detail while retaining the world's removed distance/radius rows.
  SceneCapture (3) keeps material programs/normal flags/subsurface and reads glass
  optics/finish. GPU test (3) drops all removed exports, retains moving checks,
  adds glass checks, deletes obsolete shell/blending tests. Source checks (9)
  keep materials/scattering/moving assertions and replace obsolete glass assertions.
  Build caught pt_walk's stale hdrpShaded guard: removed. Two stale source assertions
  updated for deleted cover and glass back faces. Native/plugin built; 93 checks.

Merge helper now accepts --work for private temporary files/backups and includes
.inl files (otherwise it silently omitted glass's new test fixture). Materials tools
shader_pass.py, blob.py, census.py and bundlescan.py are in tools/research, with
purpose headers; existing unity_shader_blob.py supplies unpacking. Syntax checked.
No decoded game shaders or restricted SDK sources copied.

## ABI and layout
TGPT_ABI is **0x00012100** in header, managed code and source checks, with an
integration reason added. Native assertions compiled; managed Validate invoked
successfully by reflection. DXC confirms FrameConstants 6512 and material stride 204.

| Record (native / managed / shader where used) | Bytes |
| --- | ---: |
| Material / NativeMaterial / MaterialRecord | 204 |
| MeshDesc / NativeMeshDesc; CpuMeshDesc / NativeCpuMeshDesc | 176; 56 |
| Submesh; Instance; Moving; Light | 16; 64; 112; 80 |
| Frame; Status; Yardstick; Guides | 600; 216; 632; 64 |
| SkinnedInstance; SharedTexture; HairLobes | 96; 96; 48 |
| ClothWind; PlantWind; PlantWindFrame | 48; 48; 32 |
| WaterCaustics; WaterShade | 192; 568 |
| GPU MeshRecord; GeometryRecord; InstanceRecord | 40; 16; 16 |
| FrameConstants (native / HLSL) | 6512 |

Material offsets: subsurface 148, program 160, programRow 164, emissiveTexture 168,
IOR 172, absorption 176, surfaceModel 188, reserved 192. Optical fields are appended;
no existing field is overwritten. New exports: SetMaterialPrograms, SetMoving.
Removed: SetDiffusionProfiles, SetSkinTransmission, SetShadowDetail. Retired event 3,
option 4194304, material bit 32 and instance bit 16 are not reused.

## Validation and limits
Evidence: %TEMP%/tgb/merge4-build (build-*, plugin-*, source-*, managed-validate,
managed-sizes, world-build-final/world-test-final); merge4-final-test for the smoke.
WorldRules: all checks passed. Plugin: zero warnings/errors. Runtime native/exe
hashes match build outputs (final-hashes.json); this is private staging, no game install.
Final smoke attempt: **137 ok / 0 FAIL**, safely interrupted when the main harness
acquired RUNNING. Driver pass creation: 172.0 s; frames rendered without a hang.
FAIL lines: none before interruption. The lake timing check was not reached.
Evidence: merge4-final-test/invalidated-639270093163007442.log. The queued retry was
cancelled to honor one smoke attempt and the immediate in-game handoff; no final
whole-suite result. GPU check/logs and final-hashes.json retain the tested identity. GPU is shared with another offline test and Starfield;
no uncontended lake timing is claimed. The main -GpuCheck allowed the run.

No new mutants, no materials/moving intermediate GPU suite, no reruns of dead
agents' unfinished tests. No NRD, RR, tgup_gpu_smoke_with_pt or in-game checks here.
Source copies were idle and mutation cleanup audited by the first worker; hashes
remain unchanged on resume. Port2's nine queued mutants and moving's four had
finished. Materials i/mG finished: grading 39/41, mE lacks a clean pass in i and
mF survives; older glass-IOR and shared-GPU timing failed in both clean/mutant.
Those inherited test gaps remain reported, not repaired. Glass's own report
records ten raw checks and 12 caught mutants; none rerun separately here.

## Workarounds after integration
- Removed: HDRP-start scattering/transmission tables, thin-skin/leaf G-buffer reads,
  screen scattering, wet-ground G-buffer modification, head-distance exclusion;
  on-screen material borrowing, small-texture exclusion and flipped-tiling tangent;
  old MOVING flag, stale scan transforms and render-thread scene-list substitution;
  glass prepass/shell cap/guessed walls/exit range/background borrowing/cover blend;
  forced glass guide roughness, HDRP shading-share pass/history, Shadow Detail curve,
  row and event. Material programs retain their own properties across all merges.
- Remain: old-route selector/forward search, stand-ins/near-back pass-through/sway
  exceptions; profile F0/thick-profile/old skin-on-mesh guide logic; atlas fallback,
  cutout coverage copies, glass/cloth shadow tint copies, baked hair/eyes, mask and
  emissive fallbacks; g_firstKnown water override, averaged plant colour until its
  program is ready; glass/cornea HDRP draw suppression; shared composite/RR depth
  and motion, shadow/caustic shortcuts. see_through_cover is now an unused inherited
  helper. Source also retains subsurface WALK_WATCHDOG 4096; not redesigned here.
  The source-job gaps and required scenes follow. This is integration, not completion
  of all PT-71 features or an appearance verdict.

## Coordinator records and source-job gaps
All five coordinator records are byte-identical to base42; incoming changes are
in merge4-build/<job>-coordinator.diff. Other-session sources are unchanged too.
- port2: file PT-37/40/42/43/58 notes on own-hit scattering, wet ground and head.
  Gaps: skin terminator/cavity tags, leaf profile F0/per-texel thickness, secondary
  head-interior hits, camera-visible-only wet store and double-sided wax media.
- materials: file program IDs/texture conventions/saturation and terrain progress.
  Remaining shader decodes include RealSkin, Dissolve3D, snow/layers, billboards
  and effects; also relief, some glows/specular/normal/emissive variants, cloth
  transmission, leaf thickness and wind colour. Details: materials.md, "What is left".
- moving: file PT-71 item 4 and previous-transform behavior. New GameObjects and
  non-Drake entities wait for a scan; motion plus wind posing is not combined;
  LOD changes lose one frame of motion. Moving emission is met, not light-sampled.
- glass: file optical transport/removals and shared depth/motion dependency.
  Its proposed PT-72 COLLIDES with base42's shared NR/RR guide request: assign the
  initial-medium-absorption gap a new ID. Other gaps: approximate shadow/caustic
  transport, skinned cornea normals, mixed opaque labels/lids and baked eye decode.
  The walk's initial segment inside a medium remains unattenuated.
All four jobs still need actual material/capture/appearance evidence in game;
full detail stays in port-2.md, materials.md, moving.md and glass-eyes.md.

## In-game handoff (coordinator only; raw None frames first)
- Scattering: `candle@quick`, then the skin-specific `npc-candle`; inspect folds,
  transmitted ear/nose light and the `Subsurface material` log.
- Wet ground: `water-day@caustics`, including animation; film dries in about five
  seconds and the native prepare time measures cost.
- Hero head: `mirror-day@mirror`; collar remains, head interior does not appear
  in mirror or camera-edge views.
- Materials: `forest` for plants/Cliff/terrain, `vista` for triplanar stone,
  `hair-moving` for tavern furniture; read yardstick per-material place/color/
  normal/roughness and the shared-texture/program-readiness counts. Report
  targets are predictions, not measurements.
- Moving: `tavern` or `vista`, raw None; draw/swing/sheathe the weapon and walk.
  Read moving count, zero transform age, previous-motion agreement and cost.
- Glass/eyes/forward: `glass-jars@quick`, `npc-day@quick`,
  `hair-eyebrows@quick`. Check successive walls, rim/tint, iris through cornea,
  forward prop coverage. Read IOR/absorption and exposure/gain after Shadow
  Detail removal. Run `tgup_gpu_smoke_with_pt` before game RR; that target is
  outside this worker's authorized two native targets.

