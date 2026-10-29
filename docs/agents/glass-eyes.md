# Glass, eyes and forward surfaces (PT-71 item 6)

## State
2026-10-07. Final source build: all ten raw optical checks pass; 12/12 intended mutant failures verified. No game launch, deployment or commit.

## Build
The resumed worker built the five inherited edits before further code changes.
Native: vcvars64 + the installed CMake 4.4.3, targets TGPathTracing.Native and tgpt_gpu_test, succeeded.
Logs: `%TEMP%/tgb/glass-build/build.log` (inherited edits), `build-reviewed.log` and `build-final.log` (final sources). NRD is disabled in this isolated build; new checks use Denoising None.
Managed: net472 build succeeded, zero warnings/errors (NuGet audit disabled to avoid network access).
No downloads, installations, main-tree writes, game access, launches, deployments or commits.

Scheduling: another session launched the game during the first pane-cap mutant. That start-gated run is invalidated (`gpu-start-gated-invalidated.log`). The replacement wrapper checks game/marker/concurrency throughout execution, stops only its own test if a condition changes, and retries. Final mutation evidence must come from that wrapper; no other session's process was stopped.

## Design before code
The camera and secondary rays must use the same glass and eye material evaluation. Glass must remain the first surface for guides, with stochastic Fresnel reflection/refraction and throughput compensation instead of a fixed pane count. Crossings continue until an opaque hit or escape, with unbiased roulette for termination. Material IOR, tint and roughness must be read rather than hardwired. Cornea/iris decoding is read before choosing the eye representation. Forward opaque and unlit surfaces already reached by the camera must emit/shade from their traced material, with no retained HDRP colour share.

Remove the glass pre-camera see-through workaround, HDRP shadow-share pass and composite share after their replacement is checked. Water and non-glass transparent/particle paths belong to other jobs and are listed rather than redesigned. No option bits; any ABI changes will be documented for coordinator numbering.

## Workarounds encountered
- `pt_prepare.hlsl`: glass-only pre-camera pass and 100 m reach, running before own camera rays. Removed after the raw replacement check passed.
- `pt_primary.hlsli`: glass eligibility by roughness, 15 m distance, fixed IOR/roughness. Removed from own-ray material evaluation.
- `pt_specular.hlsl`: `see_through_shell`, SHEETS=64, guessed 3 cm walls, 1 m exit search, straight-through TIR fallback, alpha-cover light, HDRP material borrowing. Replaced by `dielectric_follow`.
- The provisional per-interface diffuse-colour filter in `dielectric_follow`, and diffuse colour in the glass specular guide, are removed too. Zero absorption means clear glass; tint comes from absorption along real segments. Missing optical absorption data stays clear and is logged, pending the material decoder. A coloured base material with zero absorption now tests that distinction.
- `pt_compose.hlsl`: borrowing HDRP's sky behind glass. Replaced with complete traced radiance (A carries zero background share).
- `pt_hdrp_shadow.hlsl`, its pipeline/history and composite HDRP-share branch: removed.
- `ShadowDetailPass`, event 3, `TGPT_SetShadowDetail`, native image curve and Shadow Detail row: removed. This was an image-brightening pass distinct from `pt_hdrp_shadow`.
- `d3d11.hlsl glass_guides`: forced 0.03 roughness and HDRP normal/albedo patch removed. The raw replacement check reads 0.39990 for a material roughness of 0.4, with diffuse albedo 0.
- `Surface.seeThrough/cover` and its alpha-blend guide formula: removed. Tests that required guessed shell thickness, HDRP material/background borrowing or forced near-mirror guides were deleted. Physical smooth-normal/lamp/sun glass fixtures now opt into own camera rays.
- `GlassFinish.HideSeeThrough` / `CharacterCapture.HideLayer`: still required to prevent HDRP drawing transparent glass/cornea again after the tracer. Classification, optical-property reading and suppression remain; these are not shading layers.
- `hdrp_material_at` / `g_firstKnown`: now unused by glass; still called by `pt_water_under`, owned by the water worker.
- D3D11 composite uses current HDRP depth/motion to place a previous traced frame. Glass supplies its actual depth/motion to traced guides; its final composite matching depth remains the displayed frame's. Shared handoff, outside this worker.
- RR depth/motion dependency: `TGPT_Guides` exports albedo, normal/roughness, specular hit distance and colour, but no own-ray depth/motion resources. RR's upscaler handoff still takes HDRP's depth/motion; the tracer's D3D12 denoising/reuse uses `Surface.position` and `motion_at`. The new raw guide check verifies colour/roughness only. This worker cannot claim that RR receives the glass depth/motion; the shared handoff migration must provide them without changing the other session's upscaling code here.
- Shadow glass connections: `glass_walk` tint plus `caustic_through`'s two-interface solver and fixed IOR remain. Not the new radiance walk; caustic completion is PT-14 after migration.
- Skinned cornea smooth normals: native skinned meshes have no normals buffer. Static meshes use their own smooth normals. The moving-geometry/material jobs must supply the character normals; do not claim a smooth in-game cornea yet.
- Mixed bottle materials: a name can identify glass while its texture also paints an opaque label or lid. The old roughness threshold guessed which texels were glass. That guess is removed; the material decode (PT-71 item 2) must identify the actual opaque portions. This migration may therefore make such lids transparent until that decode exists.
- `SceneCapture.EyeTile`: the existing baked eye colour remains the materials job's dependency (PT-71 item 2). Both camera and mirror rays use it; this worker has not replaced it with direct evaluation of every eye-shader texture/channel. The eye fixture proves transport parity, not the game's full shader decode.
- The shared legacy G-buffer selector and non-glass prepare paths still compile for the water/scattering jobs. No legacy glass prepass or HDRP shading-share branch is retained for comparison.

- Initial medium absorption (new PT-72): the walker attenuates segments after interfaces, but paths born inside an absorbing solid do not yet attenuate their initial segment. This is a code-audit finding, not an observed game defect. Entering from air is what the analytical wall/jar checks verify. A general path-medium initialization, including primary opaque hits within a volume, remains required; no guessed thickness was added.

## Validation
Native targets and shaders (SM 6.6, SM 6.9 and SER) compile. Managed net472 compiles with
zero warnings/errors. Source checks pass, 89/89. No full legacy GPU-suite result or NRD/RR
evaluation is claimed. This isolated native configuration has NRD disabled.

`tests/pathtracing_glass_checks.inl` runs at 320x180, full-resolution rays, Denoising None,
no radiance cache, no caustic solver, no clamps; ReSTIR reuse stays enabled. It averages
24-64 frames (8 for the Unlit reference). The regular image suite remains 1280x720.
Producing and reading RR's guide textures does not evaluate RR.

| Raw check | Measured | Reference / criterion |
| --- | --- | --- |
| Unlit RGB | 0.099976 / 0.149902 / 0.199951 | Colour ratios 1 : 1.5 : 2, no light |
| 80 glass interfaces / bare | 1.06854 | 1, within sampling tolerance 0.15 |
| Rough 45-degree pane, equal IOR | 1.00181 | 1, tolerance 0.05 |
| Absorption through 20 cm | 0.36686 / 0.81736 / 0.95912 | exp(-1), exp(-0.2), exp(-0.04) |
| IOR 1.5 pane transmission | 0.92522 | Multiple-reflection sum 0.92308 |
| Slanted-pane refraction | 0.26331 m; target RGB 0.005073 / 0.236995 / 0.005415 | Snell predicts 0.2633 m displacement; narrow green target there |
| Guide roughness / diffuse albedo | 0.39990 / 0 | Material 0.4 / no diffuse lobe; clear specular albedo 0.99216 (must exceed 0.95) |
| Jar centre / rim, red | 0.53633 / 0.13304 of bare | Actual longer wall path darkens rim |
| Eye mirror / direct, green | 0.99393 | Same colour and light, tolerance 20% |

The jar fixture has two clear panes in front of a closed hollow jar. It uses IOR 1 to
isolate measured absorption/thickness; the separate pane and eye fixtures test non-unit
IOR (1.5 and 1.376). These are analytical geometry fixtures, not the game's material decode.
I read the saved jar, direct eye and mirror eye images myself.

Mutation results (each run uses `TGPT_TEST_STOP_AT_FAIL=1`):

| Mutant | Deliberate break | Result |
| --- | --- | --- |
| `cap` | Terminate after eight interfaces | Expected FAIL; stopped immediately |
| `matched` | Apply microfacet masking between equal media | Expected FAIL; stopped immediately |
| `absorption` | Remove volume absorption | Expected FAIL; stopped immediately |
| `ior` | Force IOR 1 | Expected FAIL; stopped immediately |
| `tint` | Restore the diffuse-colour filter on clear glass | Expected FAIL; stopped immediately |
| `snell` | Replace refraction with the incoming direction | Expected FAIL; stopped immediately |
| `jar` | Discard oblique tinted jar walls | Expected FAIL; stopped immediately |
| `rim` | Remove absorption on the longer rim paths | Expected FAIL; stopped immediately |
| `eye` | Blacken the iris material | Expected FAIL; stopped immediately |
| `unlit` | Remove Unlit emission | Expected FAIL; stopped immediately |
| `guides` | Force primary glass roughness 0.03 | Expected FAIL; stopped immediately |
| `guide_tint` | Tint the glass specular guide by diffuse paint colour | Expected FAIL; stopped immediately |

Evidence under `%TEMP%/tgb/`:
- `glass-proof10/gpu.log`: ten raw checks; `glass-proof10/glass-*.png`: their pictures.
- `glass-proof-<mutant>/gpu.log`: each intended first failure.
- `glass-mutants/<mutant>/`: modified owned shaders, compiled DLL and build log.
- `glass-build/build-reviewed.log`, `build-final.log`, `managed-complete.log`, `source-final.log`: builds/checks.
- `glass-complete/gpu.log`: final clean native build, all ten raw checks passed again.

The last small code audit exempts eyes from reflection boost for camera rays as for mirror
rays, and prevents an equal-IOR boundary from widening texture footprints. Equal media also
have no microfacet masking: the new slanted rough-pane check caught that loss.

## Build fingerprints
Final native SHA-256, identical in `glass-build`, `glass-complete` and the runtime used by the final test:
`88CD424F2806B1D63199EE0B01D1D6EFB4B0B43CFEE5E06848F406DAE28DBC05`.
Managed net472 plugin: `41FFFC5E043F723AB8D5DC1614315847AE37B961CF2B681D46AAD1A32A2F4989`.
Test executable, identical in build/runtime: `AEDA3861C2A6FA516E732891B589B3746E4B6A7E27DDB0AAAEB009605B04F33C`.
Local build/test helpers are retained under `%TEMP%/tgb/glass-build/scripts`, outside the merge.

## ABI and merge
`TGPT_Material`, `MaterialRecord`, `NativeMaterial`: 160 -> 192 bytes. Appended IOR, RGB absorption per metre, surface model (0 lit, 1 Unlit) and padding. The material worker may also extend this record: merge fields explicitly and update all three strides together. No new option bit or export. Removed `TGPT_SetShadowDetail`; retired event 3, option bit 4194304, material bit 32. None is reused. The coordinator must bump `TGPT_ABI` and matching managed ABI before integration; this worker left the number unchanged as instructed.

After merging, run `tgup_gpu_smoke_with_pt` before the real-game RR checks because the guide handoff changed. This worker built only the two authorized TGPT targets and did not run that upscaling test or evaluate RR.

### Inherited changes reviewed against base37
Kept optical material fields (192 bytes), material IOR/absorption reading, Unlit emission and eyes visible to all rays.
Kept removal of the shadow tint walk's eight-crossing limit. The material ABI number is the coordinator's job.
Correcting: corneas without a colour texture fell into CharacterMaterial's fallback; primary glass still forced
roughness 0.03; the old radiance ray path could not yet handle the interfaces it now stopped at.

### Implementation design refinement
One dielectric interface walker is shared by camera, reflected and bounced paths. Each interface samples exact
dielectric Fresnel, GGX visible normals at its own roughness, Snell refraction or total internal reflection, and
Beer-Lambert absorption along the actual segment. Radiance transport includes the squared IOR ratio. Termination
uses compensated roulette, never a crossing count. Enclosing media are queried from geometry without a fixed stack.
The primary glass hit remains the pixel's surface and supplies its guides and motion. The spare per-pixel hair
record carries its material/instance/front-face identifiers (glass and hair cannot occupy the same first hit).
Existing non-glass bounce-depth/cache approximations belong to PT-66; existing shadow-caustic shortcuts are named below.

Mutation compilation never changes working shader sources: it generates private headers, links each mutant, then restores the baseline headers. All SDK sources stayed at their existing roots. Only the worker's own DLLs and test executables were staged under `%TEMP%/tgb/glass-*`.

Coordinator recipes, one scene per part: `glass-jars@quick`, `npc-day@quick`, `hair-eyebrows@quick`. Read `2-traced-none` first (16-frame frozen mean, ReSTIR reuse stays enabled), then RR and NRD. Glass must retain the surfaces behind successive walls and its rim/tint; eye/cornea must appear with the iris behind a clear layer; forward props must remain present with their own traced light. Read the glass log's material/fallback IOR, absorption per metre and the yardstick's forward share. After deleting Shadow Detail, compare the exposure/gain line too. These runs and the owner's appearance verdict are not performed by this worker.
