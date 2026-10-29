# Water 10 - shore foam, lamps inside water, off-screen light, mirror audit

State: READY FOR HAND-OVER, 2026-10-09 11:02. Native and Mono builds pass; 101 source checks pass; the corrected focused GPU fixture passes all four cases in sequence, zero failed render frames. No in-game acceptance claim. Worker in water10, base67. No game start, deploy, main-tree writes, commits, downloads or SDK writes; own build/staging only, NRD off.

## Read this first

| Hidden Advanced key | Values | What true does |
|---|---|---|
| Water shore foam | false / true, default false | Adds world-space breaking foam from the traced bed and real current/previous wave state. Every ray uses the same surface state. |
| Water body lamps | false / true, default false | Samples the existing lamp lists at water scattering events, through real waves or directly from submerged lamps. |
| Water light without view | false / true, default false | Replaces underwater surface cache queries with sun/lamp connections traced at that hit, independent of the camera view or finite landed cache. |

Transport: unused TGPT_Frame.cameraPosition[3] carries integer flags 1/2/4 into the retired FrameConstants.prevCameraPos.w word. No option bit, ABI size or export change. Other readers of prevCameraPos use XYZ only. Water-9's two switches remain as they were. PT-90 has a measured diagnosis and design, not a new switch or a brightness correction.

Final offline results (128x72, 128-frame means, None):

| Check | Measured | Reference / comparison | Result |
|---|---:|---:|---|
| Body, lamp above | 0.00472456967 | first scatter 0.00457356323; ratio 1.033017 | pass; later scattering is included in the renderer |
| Body, lamp submerged | 0.0832994825 | first scatter 0.0809935256; ratio 1.028471 | pass |
| Body switch off | 2.30499642e-9 | deliberately no-specular lamp, no sun | pass |
| Receiver outside straight view, old route | 1.00922383e-7 | bent camera ray sees a bed point culled by the old caustic pass | loss reproduced |
| Same receiver, lamp | 0.179364698 | 0.180295782; ratio 0.994836 | pass |
| Same receiver, sun | 0.352495489 | 0.351899920; ratio 1.001692 | pass |
| Foam albedo, shallow moving / deep / still / off | 1 / 0 / 0 / 0 | traced depth and wave state | pass |
| Material seen directly / reflected by water | 1 / 0.0279884438 | Fresnel reflection of the material | pass |
| Same mirror with lamp sphere | 4.4880956 | 160.355 times material-only reflection; sphere radiance 159.154943 | PT-90 cause demonstrated |

The first full focused run had two fixture failures, both retained in the journal below: unsupported 4x4 wave buffers, and a one-hit sun reference applied to a wide bed that receives further internally reflected light. Only the fixture changed. The corrected view/foam parts passed, then the final combined TGPT_TEST_WATER10_ONLY run passed. No shader correction or tolerance widening was used to clear either failure. No whole GPU suite, ctest, mutation check, NRD or RR run.

Costs and remaining work: the beach still needs the coordinator's 140-ms-on / 55-ms-off comparison. One foam query adds a bed ray and wave reads. Each lamp candidate adds the proposal/visibility work detailed below; candidate and proposal budgets change variance, not support. Under contention, the tiny fixture's total GPU times were 0.7-3.0 ms; they cannot estimate in-game cost or be subtracted into reliable deltas. Cold driver setup took 453.1 s and a sampled working set reached 23.7 GB; the final cached startup was 5.6 s. This startup cost must not be mistaken for a frame-time improvement. Full bubble age/advection, PT-90's real emitting geometry/radiance pairing, and above-water users of the old finite caustic cache remain open. No game look has been judged.

Coordinator runs, one scene at a time (recipe water10; 16 frames; None then NRD; final live animation step):

- `.\tools\harness\Run-PtHarness.ps1 -Scenario water-day -Recipe water10`
- `.\tools\harness\Run-PtHarness.ps1 -Scenario water-torch -Recipe water10`

Build evidence: all 40 shader variants succeeded; TGPathTracing.Native and tgpt_gpu_test built; Mono net472 build 0 warnings / 0 errors; source checks 101/101. Build, stage and loaded-runtime DLL SHA-256 all match: `8361C02EF0007C759DD50FFFDCC7AC54DC542E90554A8F4D5C033E3C5BDBBC79`.

Review files: new shader `pt_water_lights.hlsli`; water surface/foam/path headers; the one field assignment in tracer.cpp and its API/common comments; Settings/HdrpPasses; CMake dependency; water10 GPU/CPU fixture and the small dispatch/source-check edits; water10 recipe. No wet-ground change. `tools/Build-PtShaders.ps1` was updated externally during the run and matches the main helper; it is not this worker's water change. Coordinator-owned backlog/context/round/request files were not edited.

Logs: own build folder `C:/Users/Frede/AppData/Local/Temp/tgb/water10-build`, water10-all.log (final PASS), water10-view.log and water10-foam.log (corrected parts); images-all contains final images. Copy logs in .testruns/water10 include first-complete-gpu.log (initial failures), final-gpu.log, source-handover.log, managed.log, shaders-final.log. The images were inspected: dark off-view baseline, lit receiver, white shallow foam/black deep albedo, bright proxy mirror. They are controlled fixtures, not game pictures.


## Plan and reasons (before code)

1. Shore foam is a material state at a world point, using the traced bed and actual wave state. Blend its coverage with the existing crest foam; every ray uses the same state. Never derive contact from camera depth. Read the wet history only if its world-space contract provides what this needs; do not change wet-shore ownership.
2. Add lamp sampling at water scattering vertices. Above-water lamps must connect through the actual wave surface and account for refraction and Fresnel; submerged lamps connect directly. Use the existing uncapped light list, with probability weights if sampled.
3. Remove view-dependent loss behind its own default-off switch and audit finite landed-light storage/reach. A fallback must retain light outside storage; merely enlarging a view is insufficient.
4. Audit reflected_lamps against actual emitting surfaces. A small sure correction is allowed; otherwise report an explicit design and measured mismatch.
5. Hand over one recipe, with None before NRD, 16 frames and moving-water steps. Leave water-9's two switches unchanged. No claim of in-game quality.

Each item has its own hidden default-off switch using spare words, never another option bit. Names and exact transport fields will be recorded after the field audit.

## Required checks

Focused offline checks only, gated by the main tree's GPU check and live harness lock. Build TGPathTracing.Native, tgpt_gpu_test and Mono plugin (no deploy). Run source checks. No full GPU suite, ctest, NRD build or mutation run. Record timings as offline only and compare against worked references.

## Known inherited gaps

Water-9: thin receivers lose light under the caustic grid; both trials remain off. Landed triangles beyond 1.5 times view are dropped (PT-94). Body light samples sun only. Shore foam absent. Mirror lamp balls carry a light object's strength rather than necessarily the flame's visible radiance (PT-90, to verify). Caustics in above-water secondary hits remain a separate inherited gap.

## Progress

- Read backlog, path-tracing context and dead ends, newest round and worker instructions. Read water-9 hand-over/trials/remaining gaps and water-8's game-run findings. Coordinator-owned memory files remain untouched as the brief requires.

- Step 1 written: shore coverage uses a downward scene ray, local present/previous wave height and slope, depth-limited breaking and a 2-second equilibrium bubble lifetime. Union with crest coverage makes the join continuous. This is a local foam model, not bubble advection; foam history is NOT borrowed from the finite wet-ground store. No wet-ground code changed.
- Fields claimed: TGPT_Frame.cameraPosition[3] (previously unused), integer flags 1/2/4; FrameConstants.prevCameraPos.w (retired HDRP ambient, no shader readers in base67). No struct size, export or option bit change. Settings: Advanced / Water shore foam, Water body lamps, Water light without view; all false.
- Steps 2/3 written: angular importance sampling through the actual traced wave hit and its normal, finite lamp emitter, Fresnel and n-squared radiance conversion, extinction, foam and both shadow segments. A Newton aim is only a proposal; a full-sphere component gives every direction nonzero support. Existing uncapped near/far light lists supply lamps with inverse-probability weights. Mesh-emitter paths remain on the existing continuation (no new duplicate).
- PT-94 design decision: simply retaining off-screen landed triangles leaves the finite grid, 200-m ray limit and overflow loss. The trial instead replaces underwater surface queries with direct angular transport at the actual point. It does not enlarge or depend on the landed grid. It samples sun and above-water lamps; direct_light keeps submerged lamps. Single-crossing lamp contributions from the continuation are suppressed only where explicitly sampled, while extra crossings remain continuation paths. The old landed route remains the switch-off comparison, as requested.
- Configure succeeds in water10-build, NRD off, local dependencies only. Compilation and correctness checks not yet run.


## Hand-over recipe (written before tests)

`water10` is in tests/ingame/scenarios.json. Every captured step is 16 frames. None steps precede NRD; the last traced step has live=true/stride=3 for moving water. No Ray Reconstruction or Neural Rendering. Water-9's switches are not changed.

Coordinator only, after merging/building/deploying and freeing the card:
- `.\tools\harness\Run-PtHarness.ps1 -Scenario water-day -Recipe water10`
- `.\tools\harness\Run-PtHarness.ps1 -Scenario water-torch -Recipe water10`

Water-day: compare 3 vs 2, and 7 vs 6: a white rough breaking edge where waves reach shallow sand/rock, joining the old bands. No white band on still deep water. Compare 5 vs 4 / 9 vs 8 for view-independent bed light, shadows and highlights; extra noise is expected until the sampling is judged. Step 10 shows motion. Wet shore is the previous job's and must remain as it was.
Water-torch: compare 4 vs 2 / 8 vs 6: lamp-coloured scattering inside the water, blocked by real obstacles, with refraction through the real waves. Compare 5 vs 4 / 9 vs 8 for lamp light on submerged surfaces outside the camera view. Record water-reflections GPU time, caustics time and exposure/gain in every pair. The beach's existing 140 ms on / 55 off are the baseline; no speed claim until game measurements.

## PT-90 audit and proposed fix (no mirror switch or brightness multiplier)

`reflected_lamps` intersects a sphere of the source radius and adds `light_incident / (light_falloff * pi * r*r)`. This deliberately cancels Lamp near size: even when near-field illumination is softened, the visible ball carries the lamp object's full candela. A directly visible flame instead gets `material_emission` from the game's material, its texture, colour and exposure weight. These are two different source radiances. The ball can also sit in front of the visible flame and add to it. A candle's measured artwork emission is not present in the LightRecord, so changing a divisor cannot make its image truthful.

Small sure fix rejected: just removing or dimming the proxy would leave point lights with no traced emitter shape (and change mirror energy independently of their emitted flux). Proper fix: pair each light with its traced emissive geometry/particle source, preserve the material's visible emitted radiance in camera AND secondary rays, derive area-light sampling from that same radiance and shape, and replace the paired point-light sampling rather than count both. Unpaired light sources need their real geometry captured. Decide explicitly how artist candela and material radiance are reconciled; matching both while they disagree is impossible. The body-lamp trial retains the existing finite lamp contract and therefore does not claim PT-90 is fixed.

- Managed build succeeded: 0 warnings, 0 errors. Initial edit re-encoded HdrpPasses.cs's UTF-8 BOM through the Windows default encoding; corrected from the byte audit, then rebuilt successfully. All subsequent edits specify UTF-8 and preserve CRLF.
- Added tests/water10_gpu.inl and TGPT_TEST_WATER10_ONLY, with part=body/foam/view/mirror. Only these focused checks will run. References are first-scatter integrals (above/below surface), flat-interface irradiance, and Fresnel/material-vs-proxy mirror radiance. Results pending.

- Source checks now pass: 101/101. Only the inherited assertion naming crest coverage was updated, plus safeguards for full angular support, no view/cache reads in the new light query, inverse-probability lamp weighting, default-off switches and continuation suppression.
- Independent CPU flat-water quadrature (tests/water10_reference.py): finite sphere / paraxial point reference = 1.000089 at 5 cm, 1.000086 at 55 cm, 1.000076 at 105 cm. The first-scatter body reference is 0.00457356323242. The finite-source correction is far below the GPU fixture's statistical tolerance.
- GPU runner prepared in .testruns/water10/run.ps1. It checks the main tree's live RUNNING holder and -GpuCheck, then starts only water10's executable. If a game/harness starts meanwhile it stops ONLY that child and waits again. No other worker is stopped.


## Workarounds met, removed or retained

| Mechanism | State in this copy |
|---|---|
| Only sun sampled at water scattering events | Replaced for lamps by Water body lamps; retained with the requested default-off comparison. |
| View culling, finite caustic grid, receiver-cell interpolation, overflow and 200-m refracted reach | Underwater receivers bypass all of these with Water light without view. The existing cache still serves the default-off route and above-water caustics; no claim that those inherited restrictions are fixed globally. |
| Retired HDRP ambient constant prevCameraPos.w | Removed its assignment and reused the unused word for these water trials. No live shader reader depended on it. |
| Shore foam absent | Added world-space bed/wave material state; no post-denoiser layer, screen depth or game foam-buffer copy. |
| Lamp sphere standing for flame radiance | Retained, diagnosed and designed for replacement under PT-90; removing it alone cannot give lights their missing emitting geometry. |
| Wet-ground history is finite and camera-centred | Not reused for foam; wet ground is not this worker's. |
| Full bubble transport/age history | Not implemented: the foam model uses local equilibrium production, current/previous waves and the existing current's time-dependent drift. Its 2-second constant controls coverage, not a promise that foam persists for exactly two seconds after a breaker. Game judgement pending. |

Native compilation is slower while many other workers compile. No GPU test has started during this wait. The small fixture also passes an MSVC syntax-only compile; its warnings point to existing double-to-float conversions elsewhere in the large test, not the new include.


## Sampling and cost contract

At a receiver, the new estimator samples a direction with density `0.85*narrow cone + 0.10*broad cone + 0.05/full sphere`. The smooth-wave shooting solution and its finite iteration count only aim those cones. Radiance is evaluated by tracing the actual wave hit, a sampled visible GGX facet, refraction, Fresnel, the ratio of refractive indices squared, extinction and the two visibility segments. A wrong aim cannot remove a connection: the full sphere gives it support. The receiver multiplies by its material response, or by 1/(4*pi) at a scattering event, and divides by the direction's probability. The existing light-list candidate probability is divided out separately. No inverse-square or refractive Jacobian is guessed: those emerge from the source's measured solid angle.

This is one-crossing direct transport. Multiple interface crossings stay on the continuing path. Only the lamp proxy contributions already sampled are suppressed; primary/mirror chains keep their lamp image. Mesh-emitter contributions remain on their previous path and are not sampled a second time here. The sun at scattering vertices keeps water-8's sampler.

Cost before measurement: shore foam adds one bed ray plus current/previous wave reads at every foam query. Each chosen lamp adds at most 5 proposal iterations (3 water queries each), then a real boundary query and two visibility rays for a successful above-water connection; there is also a broad/full-sphere sample for missing focusing branches. These iteration and light-candidate budgets change variance, not the set of contributing paths. PT-94's switch adds the analogous sun/lamp work at underwater surface hits and still builds the old caustic buffers for their other consumers. No global speed improvement is claimed.

- Strengthened the PT-94 fixture before running it: a 45-degree camera in air, 0.1-degree field of view. Its bent ray meets the bed outside 1.5x the camera's straight frustum. The test measures the old cache's loss, then checks the new lamp and sun result against Fresnel and flat-water refraction, including the camera's entry factor. This tests an actual off-view receiver, rather than just a source outside the picture.


- First native build: all 40 shader headers compiled (0 failures, 2242 seconds under CPU contention); both requested native targets built. The GPU gate is waiting for the game/live harness, so there are no GPU numbers yet.
- Review fixes applied before the final build: the foam bed test now saves/restores the caller's ray state and uses a fixed world-point alpha sample with a zero cone. Its result cannot depend on whether a camera, shadow or mirror asked. The normals resource is opened only after its validity check. Underwater surface-light response now carries thin-material transmission and tint, matching the existing direct-light response. Explicit loop attributes keep proposal searches and light-list sampling as loops; they do not change the estimator. Every replacement was inspected. Final shaders compiling separately from the already staged first build.

- At 09:57 the gate permitted the first focused run beside one offline test. At 10:01 a game/harness started, so the guard stopped ONLY this worker's child (PID 9408) during driver pipeline compilation, before any image measurements. Its log is .testruns/water10/initial-startup.log. Retired the worker's now-obsolete waiter after verifying its process and parent; one final-build waiter remains. No other process was stopped.

- Final shader run ended without a completion line; its process returned 1, no diagnostics, and four headers had no logged success. The copy's Build-PtShaders.ps1 was updated at 10:07:11 and matches the main tree's new machine-wide memory/compile gate; this worker did not write that helper. Do not count it as a water change. Rebuilding those four outputs with the updated helper. A tool policy rejected removing the four generated headers, so their timestamps were safely made stale instead; CMake's commands will overwrite them. No bypass or permission request.

- A second compiler run ended without diagnostics/completion. Its two reflection variants completed successfully (730 s and 754 s); GI DXR1.2 and the basic reflection pass remained stale. At 10:27:47 the shared helper was updated again to honor GAME-WANTED as well as a running game. Restarted ONLY the two stale outputs with that helper. No completed header was rebuilt, and the final link/test waiter still waits for a complete successful shader run.

- The GPU waiter now also yields to the coordinator's fresh %TEMP%/tgb/GAME-WANTED marker (the helper's same three-hour freshness check). It reads the marker only; it never clears or writes it. This avoids repeatedly warming the driver just before an intended game run.

- Final native build completed at 10:45: all shader variants now compiled successfully, TGPathTracing.Native and tgpt_gpu_test both built. Build/stage SHA-256: 8361C02EF0007C759DD50FFFDCC7AC54DC542E90554A8F4D5C033E3C5BDBBC79. The main -GpuCheck gate allowed the focused test beside another offline test and Starfield (its documented policy); no FoA game, live harness lock or fresh GAME-WANTED marker was present. Timings from this run are therefore contention measurements, not performance claims. No other process was touched.


## First completed focused GPU run: two fixture defects to resolve

Cold driver startup took 453.1 seconds under contention. Its process had a 23.7 GB working set at about six minutes; this is a real startup/resource cost to review, not a frame-time claim. The actual image checks then took seconds, with no failed render frames.

Body above: 0.00472456967 / 0.00457356323 = 1.033017; submerged: 0.0832994825 / 0.0809935256 = 1.028471. The references count one scattering event; the renderer includes later events. Both pass. Body off: 2.305e-9.
Off-view lamp: old 1.02856e-5, new 0.189067392, one-hit reference 0.180295782 (1.048651). Pass. Sun: 0.427997411 against one-hit 0.35189992 (1.216248), FAIL. The fixture's wide diffuse bed allows light to reflect internally at the water surface and return to the bed. The one-hit reference excludes that real light. Revised the fixture to a 4-cm receiver at the bent camera hit, with no surrounding reflecting bed; this isolates the analytic connection without limiting any renderer path. The revised run must confirm the diagnosis; no tolerance was widened and no shader was changed.
Foam: FAIL, all zero. The native log says the 4x4 simulation buffers were refused; copy_waves accepts sizes of 8 and above. Changed the fixture to 8x8 and checks texture creation succeeds. No renderer fallback or wave limit changed.
PT-90: directly visible material 1.000000; its water reflection 0.0279884438; with the lamp proxy 4.4880956, 160.355 times brighter. The proxy's radiance is 159.154943. This confirms both the radiance mismatch and additive proxy in this controlled case. The mirror checks pass.

Only the corrected view and foam parts will be rerun. The final source checks pass 101/101; 17 job files passed UTF-8/CRLF/original-BOM audit after correcting the new CPU reference file's line endings.


## Final validation and stop

Final combined focused run: PASS, exit 0, zero failed render frames. Corrected isolated parts also PASS. The final combined ratios, rather than the isolated run's different random sequence, are in the table at the top. Source checks rerun after the fixture corrections: 101/101, 0.384 s. UTF-8/CRLF and original BOM states checked for all 17 job files. Native build/stage/runtime hashes match; no deployed copy exists from this worker.

The wide-bed reference diagnosis has an independent physical check: the diffuse hemisphere's Fresnel reflectance from inside water is 0.47485365, including total internal reflection. An infinite Lambert bed of albedo 0.4 would gain 1/(1-0.4*R) = 1.23447868 from repeated returns, before its extra GGX term. The finite wide fixture measured 1.216248 against a one-hit reference; the isolated receiver measured 1.001692. The extra light was not a sampler error.

Stop here. The coordinator must merge, build/deploy, and judge water-day and water-torch with the recipe before enabling any switch by default. PT-90 remains a design hand-over, not fixed. Foam's local equilibrium model has no transported bubble age. The old finite landed-light route is still used by default and by above-water consumers. No additional features or performance tuning were attempted.
