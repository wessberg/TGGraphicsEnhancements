# Water 3 â€” PT-71 item 5

## Result

The core migration is implemented in this isolated copy. Water is intersected by the camera and common world rays, with dielectric and medium transport. The old water passes and their supporting code are deleted. Native and Mono builds pass; 89 source checks pass; 11 undenoised optical checks pass and each fails under its intended mutation. The restored, unmutated final DLL passed again: 2,071 traced frames, zero GPU failures.

PT-71 item 5 is still open for full coverage and in-game acceptance. Decal foam, water masks/current maps/deformers and the common RR depth/motion handoff remain gaps. The on-demand geometry traversal is far too costly for a final renderer. No game was started, no files were deployed, and the main tree was never written.

## Build and evidence

The inherited changes were compared with `../base37` and built successfully before further implementation (`water3-build-initial.log`). Build tree: `%TEMP%/tgb/water3-build`, NMake Release; SDKs read from the main tree. Only `TGPathTracing.Native` and `tgpt_gpu_test` were built. NRD is OFF because its configure step writes into the SDK source, which would write through the junction into the forbidden main tree. These tests use Denoising None. The coordinator must build with NRD and test its guides after merging.

Final evidence, all under `%TEMP%/tgb/`:

- `water3-build-final-check.log`: native/test build succeeds. Existing C4244 conversions in the large GPU harness remain warnings.
- `water3-managed-final2.log`: net472 build succeeds using restored packages (`--no-restore`), zero errors, three cached NU1900 warnings. Nothing installed or downloaded.
- `water3-source-final.log`: 89/89 source checks. The removed-pass guard was separately mutation-checked: `water-3-source-mutant.txt` beside this report.
- `water3-test-final.log`, `water3-images-final/`: restored unmutated run, all water checks pass. Shader startup 89.6 s under the ordinary 180-second limit. The earlier 600-second diagnostic override is removed.
- `water3-test-mut-*.log`, `water3-mutations-lane1.txt`, `water3-mutations-lane2.txt`: control passes and all 11 targeted mutants stop at the expected FAIL. The fixture's optional `TGPT_TEST_WATER_CHECK` selects the assertion being mutation-checked; setup/GPU errors always remain fatal.

Permanent numerical evidence: [water-3-validation.txt](water-3-validation.txt). Temporary mutation edits are recorded in [water-3-mutations.patch](water-3-mutations.patch); they are restored out of production source. The original mutation binary remains only in `%TEMP%/tgb/water3-mutant-binaries`.

Native SHA256: `BC2EF957213E42901DAD8F452BC04168B95D79E88DA5EFB72211785A89A9C86A`. Build DLL, staged DLL and the final test's loaded DLL all match. This is a worker staging check, not a game deploy.

| Undenoised check | Final measurement | Reference or limit |
|---|---:|---:|
| Flat bed through water | 0.550993 | Fresnel / nÂ²: 0.551050 |
| Brightest pixel / neighbours' mean | 1.0256 | below 1.08 |
| Absorption over 0.4 m | 0.669167 | exp(-0.4): 0.670320 |
| Underwater camera, 0.2 m segment | 0.817826 | exp(-0.2): 0.818731 |
| Total internal reflection | 1.000000 | 1 |
| Flat water in mirror / camera | 0.997551 | 1 |
| Grazing sky reflection | 0.408314 | exact Fresnel: 0.407495 |
| Wave normal error | 0.000734 | below 0.018 |
| Wavy bed | 0.550840 | Fresnel/Snell reference: 0.550891 |
| Waves in mirror / camera | 1.000111 | 1 |
| Nearest water/land at sloping shore | 0 / 9,023 wrong | below 2% |

Exposure and traced gain are both 1. References are analytic, integrated over the fixture's camera rays; estimates average 128 or 256 undenoised frames. I viewed the water, grazing reflection, mirror, wave-normal and curved shore images myself. They are small synthetic fixtures, not evidence of the game's appearance. The mirror comparison keeps the same ceiling mirror in both scenes; removing it would change the lighting by about 2% because it sends water's reflected light back down.

Mutations: remove etaÂ² (flat bed and wavy bed), multiply one pixel by five (dots), remove extinction (absorption and underwater camera), disable total internal reflection, hide water from mirror rays (flat and wavy mirror), remove grazing Fresnel, flatten displaced triangles, and let water win even when land is nearer. Each corresponding assertion failed. No denoiser was involved.

## Design and implementation

The original design was written before code: put water into common world-ray intersections, use Fresnel roulette and index-squared radiance transport at the boundary, integrate attenuation along each segment, provide first-hit guides, then delete the old passes. It initially preferred a height field if every crossing and footprint could be found correctly. I chose displaced parametric triangles because horizontal choppiness can fold a surface; a single-valued height field loses those crossings.

Each base triangle is subdivided at the wave simulation's texel spacing. Its vertices use the shared HDRP displacement, including horizontal motion. Conservative bounds prune an exact stackless traversal of this triangulation. There is no traversal-count or water-crossing budget. This is a finite geometric discretization, not an exact analytic intersection with the interpolated field; agreement with HDRP still needs measurement. An acceleration structure may cache exactly these triangles without changing the result.

`pt_waves.hlsli` follows `../merge3`'s filename, function names and order. Its common helpers remain first; only the diagnostic overload is appended. `water_base` now filters the requested slot during traversal rather than stopping after four overlapping faces. Its older four-step `water_height` is unused by these intersections and belongs to the other port. Caustics keep their own pass; this worker does not claim new caustic image validation.

`pt_water_optics` uses the water index 1.3333, exact Fresnel in both directions, stochastic reflection/refraction, etaÂ² for camera radiance and total internal reflection. `pt_water_path` continues through arbitrary water crossings with roulette, using the common material reader and diffuse/GGX lobes. RGB channel-mixture free flights provide attenuation and isotropic scattering: sigma_t comes from HDRP extinction, sigma_s = sigma_t Ã— scattering colour. That is an explicit physical interpretation of the authored colours; no white diffuse surface stands in for volume scattering.

Simulation foam reads the existing Jacobian and original erosion/mask textures, with the surface's own colour and roughness. Foam's dielectric F0 follows the water index. Decal foam is absent: its render texture has not been shared. The managed capture reports this gap. Current maps, masks and deformers also remain unported.

Primary water writes its own position, normal, foam albedo/roughness and previous-wave motion into the main surface buffers. Previous position is interpolated at the same displaced microtriangle barycentrics. Both simulation-buffer parities stay alive until their D3D12 reader finishes. The specular signal's hit distance comes from the next actual path event, not camera-to-water depth.

## Startup and cost

Early versions failed before producing an image. Forced DXIL `noinline` functions crashed NVIDIA's `nvgpucomp64.dll` with assertion 0x80000003; scalar signatures and explicit state arguments did not fix it. Do not repeat that approach. Fully inlining duplicated the integrator at many call sites and exceeded 600 seconds of driver compilation.

Fixes: one water continuation each in `shade_path` and `shade_seen`; one common foam/solid lighting block; dyadic parent recovery instead of a dynamically indexed traversal stack; one continuation for the three legacy glass branches; removal of a redundant second water shadow query; reusing a path's known medium for its shadow samples. No geometry or light was cut. Startup improved through 517 s, 203.5 s and 106.7 s, ending at 89.6 s in the final run.

The geometry cost remains a real defect. At only 128Ã—72, the final wavy-pool stage sample was prepare 20.49 ms, reflections 5.41 ms; the mirror scene was prepare 29.02 ms, bounced light 28.28 ms, reflections 8.84 ms. The final shore frame reported 76.484 ms total. Other workers can affect timings, but these values plainly do not establish acceptable game performance. Flat water was around 0.04 ms prepare and 0.05 ms reflections. The next geometry job should cache/accelerate the same displaced surface or improve conservative bounds; it must not discard waves or impose crossing limits.

## Workarounds met

Removed in this copy:

- `pt_water.hlsl`, `pt_water_under.hlsl`, their pipelines, separate water queue/list state, textures, 12-frame histories, timers, render events 4/6 and dispatches.
- `d3d11.hlsl` water depth extraction, reflection fill, 3Ã—3 refraction filter, water shading and recomposition.
- `TGPT_SetWater`, `TGPT_SetWaterShade`, managed bindings, `HdrpPasses.WaterPass` and the water SSR hooks.
- `WaterShade.cs` and its HDRP water-lighting transpiler; temporary caustics-intensity and fog mutation hooks.
- Water Reflections row, F8 comparison, obsolete water-pass GPU fixture and related source assertions/docs. Proposed row decision: remove it, because Fresnel reflection is intrinsic to the surface. The owner decides the row at merge.
- Common `trace_from_surface`'s guessed near-HDRP retry/offset corrections, and the dead first-bed caustic add-back flag.

The new `WaterSurfaceRendering` hooks suppress gameplay-camera water drawing and lighting while keeping simulation alive. HDRP's `waterGBuffer.valid=false` then naturally suppresses its underwater shading variant; atmospheric fog stays. Verify this in game.

Kept, outside this job: the caustic photon pass, legacy glass shell and HDRP material lookup inside it, eye/forward/fog routes, `occluded_from_surface_rays`' old near-depth correction, the common frame compositor's depth/motion matching, and inherited wave copies/slot limits. Glass continuation calls were only consolidated to avoid shader duplication; the glass worker owns deleting that model.

The generic D3D11 sky compositor is renamed `sky_surface` and now accepts every complete traced primary surface. The old glass-only condition discarded underwater/water primary pixels when raster depth was sky. Keep this fix when merging glass. It is frame composition, not a replacement water shading pass.

Outside review findings 1â€“4 are removed with the old material substitution, averaged-distance absorption, shore snapping and white scattering proxy. Finding 5's old reflection estimator is gone. Its demand for full material parity remains open across the common renderer: water continuations currently use diffuse/GGX, not the other workers' finished skin/hair/glass models. Do not call mirrors complete from this fixture.

## Remaining scope and merge notes

- RR still receives HDRP frame depth/motion through the common cross-mod handoff. Own water world position and previous-wave motion exist in the main surface buffers used by NRD/RRG; their runtime validation is still the coordinator's. The compositor's matching-distance field remains HDRP's until PT-71's frame handoff changes.
- Medium membership assumes connected water below the nearest upward boundary. Caves below a lake and closed/overlapping water volumes need explicit membership. Secondary air fog belongs to PT-37. No complete general-purpose volume renderer is claimed.
- The full path must not receive the old first-bed photon add-back without estimator weighting; that would count light twice. The separate caustic pass is retained for its owner. Rock LOD belongs to world capture, as the water-edges report found.
- No option bit added. Packed surface kind 10 identifies water; coordinate this with other material/primary changes.
- `TGPT_WaterCaustics` / managed `NativeWaterCaustics`: 192 to 288 bytes. `FrameConstants`: 7,120 to 8,496 bytes. New `WaterRecordGpu`: 64 bytes. Exports `TGPT_SetWater` / `TGPT_SetWaterShade` and events 4/6 are removed. Historical status water counters remain zero. The coordinator assigns the merged ABI number; this copy keeps the base number.
- CMake changes are confined to the path-tracing shader lists/dependencies. Merge shared constants, scene records, `shade_path`, `shade_seen`, primary guides, specular and generic sky composition carefully. See `water-3-changed.txt`.

## Coordinator checks

Build with NRD after merging and assigning the ABI. The worker commands are in `tools/Build-Water3.ps1` and `tools/Run-Water3.ps1`; the latter stages matching files into a private `water3-*` directory and obeys the main harness GPU gate. Mutation helpers use only the saved private mutation binary. No game folder or main-tree artifact was changed.

Run ONE game scene per part: `lantern-lake@dots` with None should show no isolated hot pixels on the averaged water, and no old water reflection/shade stages or histories in the native log. `water-day@raw` should show the attenuated bed, far shore in reflections, and the same nearest displaced boundary at rocks and sand. Read exposure/gain because the corrected bed transport removes light. Inspect primary depth/normal/albedo/roughness/motion, especially moving waves; then check RR and NRD. Record prepare/reflections/GI costs separately. The current synthetic cost predicts a serious performance problem; do not describe these builds as a finished playable water migration. No in-game appearance or performance verdict has been obtained.
