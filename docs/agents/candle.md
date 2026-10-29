# Candle worker: PT-73, PT-06, PT-40, PT-41

2026-10-08. Native and managed built. New checks pass at the origin; a newly added translated-skin check exposed an unresolved precision defect. The single full GPU run finished with 43 failed checks; scene appearance remains unconfirmed. Worker copy candle against base44. No game, deployment, main-tree writes, commits, SDK writes or downloads. Coordinator-owned memory files untouched.

## Recovery

The eight inherited edits were in tgpt_api.h, d3d11.hlsl, extension.cpp, runtime.hpp, tracer.cpp, HdrpPasses.cs, NativeTracer.cs and pathtracing_gpu_test.cpp. Their first build was attempted before further edits: native compilation succeeded, test compilation failed because pathtracing_candle_checks.inl did not exist. Initial log: tools/candle/initial-build.log. Read edit.py and fix_edit.py. Removed the inherited post-transparent motion repair, its export, render event, shader, scratch texture and custom pass. The actual candle material has a switch that prevents the bad write in the first place. No ABI, struct or option-bit change is needed.

## Rectangle evidence

Read the None flicker map, six crops, game comparison and NRD boil crops from main artifacts/harness/20261007-231354/candle. Eight sharp rectangles correspond to the eight active VFX_CandleFlame_Single objects in the earlier effects census (20261007-191814/candle/1-game-effects/effects.json).

The inventoried shader is Hidden/VFX/VFX_CandleFlame_Single/Flame/Output Particle|Shader Graph Quad - Unlit. Only DepthForwardOnly and ForwardOnly passes exist, no DistortionVectors pass. Its material is additive (_SrcBlend=1, _DstBlend=1), ZWrite=0, TransparentDepthPrepassEnable=0, TransparentDepthPostpassEnable=0, AlphaCutoffEnable=0. _TransparentWritingMotionVec=1, keyword _TRANSPARENT_WRITES_MOTION_VEC enabled. The already-extracted actual pixel shader (tgb/vfx-shader/single/dis/09-ps-chunk0-96ee.txt) writes a constant (2,0,0,1) to target 1, unconditionally and without alpha discard. The simple variant reads only flame texture and noise texture plus graph values; noise offsets FLAME UVs, not scene colour. The fog variant also reads HDRP fog/probe data. Flame UV distortion is not heat haze.

Therefore the strongest concrete mechanism is motion-history rejection after PT/NRD, in HDRP's temporal upscaler, over each flame's whole quad. It explains why None and NRD both show rectangles but the flames are steady. The offline reproduction passes; final causal proof still needs the coordinator's game rerun. This is not evidence that heat haze should be deleted: this candle output contains no heat-haze field to trace.

20261007-231354 has camera audits only for 1-game, where Distortion=true is correct. It does NOT record effective frame settings while traced, so no claim that SuppressDistortion worked can be made from that audit. The new traced-camera log makes the next run decisive.

## Changes

- `EffectPasses.cs` replaces the inherited candle-name-only switch. Every loaded material is checked for the **LightMode** `DistortionVectors`, including unnamed VFX passes. That pass alone is disabled while tracing; the prior enabled state is restored on Off/disposal. All fire families follow the same rule, including off-screen effects. The global `SuppressDistortion` camera-settings override is deleted. This suppresses the old screen distortion; it does not supply traced haze. New loaded materials are found once per second (a newly loaded material can precede discovery by one second); tracked switches are reasserted each frame.
- Additive VFX Shader Graph Unlit quads with no precomputed velocity select the shipped colour-only variant by `_TransparentWritingMotionVec=0` and the matching keyword. This is an output-type predicate, not a list of candle/torch/fire names. Other transparent materials keep their motion. Both property and keyword return to their exact original values on Off.
- Motion predicate limitation: the constant marker is directly proved for the captured candle shader. Applying the switch to other additive Unlit outputs without precomputed velocity is an inference from the shared output type. Verify their shipped variants in the torch/fire rerun; a different output with valid internally computed motion would need its simulation/output metadata to distinguish it. The state tests prove preservation of the explicit precomputed-velocity case, not every possible VFX graph.
- Shader switch API checked against Unity's own documentation: [SetShaderPassEnabled](https://docs.unity3d.com/ScriptReference/Material.SetShaderPassEnabled.html) uses the LightMode tag, not the displayed pass name; [FindPassTagValue](https://docs.unity3d.com/ScriptReference/Shader.FindPassTagValue.html) reads that tag, including unnamed passes. Managed net472 build passes with no warnings/errors. No package downloaded: local cached packages, local empty feed, deployment false.
- Subsurface colour conversion now inverts the isotropic half-space transport equation numerically (a compact polynomial for sqrt(absorption), conservative white exactly 1), preserving the existing diffusion-distance conversion. The old polynomial's reflectance for A=0.3/0.5/0.7/0.9/1 was 0.300220/0.501966/0.704816/0.870414/0.925985. No colour boost and no second multiplication of colour. This fixes the bright-end energy error, not a proven explanation of the entire brown/red defect.
- Removed the 4096-flight hard stop. Weighted roulette remains (no fixed path-length limit). No export, shared struct, option bit or ABI change.
- Removed the 40-material diagnostic cap and added an explicit message for double-sided subsurface exclusions. The next game run must record the wax exclusion instead of silently omitting it.

### GPU checks added

`tests/pathtracing_candle_checks.inl`, included early in the full test, or `TGPT_TEST_CANDLE_ONLY=1`: actual D3D11 additive quad/MRT reproduction, and cream/skin/white slabs lit by a constant white cube map, own camera rays, undenoised. The MRT check preserves colour across invisible pixels, reproduces marker writes, and measures the resulting simple temporal gather. It is not the proprietary game upscaler. Slabs compare demodulated energy to a white diffuse reference (colour once), within 3.5%. The reference CPU walk solves transport independently rather than copying the shader remap. The first four cases pass; the fifth translates skin 110 m from the origin and exposed a precision defect. The single full suite is complete; its failure list is below.

## What the captures establish, and what they do not

The highest rectangle belongs to `VFXGraph_CandleFlame` instance 4573636, not a separate haze object. The eight active near-camera single-flame components in the 19:18 census are 4572444, 4572552, 4572658, 4573082, 4573156, 4573626, 4573636 and 4573712. All eight use the same `Hidden/VFX/VFX_CandleFlame_Single/Flame/Output Particle|Shader Graph Quad - Unlit` output. Their projected pivots at the captured camera/FOV 58.7155 degrees, in the 960x540 map, are approximately (529,319), (454,370), (508,333), (405,438), (568,346), (383,393), (477,289), (402,351). These align with the eight flame bases and the rectangles above them. This is a cross-run census using the same save; exact drawn corner buffers are not in this run, so do not claim pixel-exact matching of all four corners.

All expose Size=0.1, NoiseScale=0.25, KnotPositionVariation=0.5, Brightness=1 and the same 512x512 flame texture. Object scales vary from 1.226 up to 2.227 vertically. Those are flame/quad inputs, not hot-air strength. The actual extracted pixel shader has no scene-colour sampling: two texture reads distort/sample the flame texture. Its colour-only variant exists in the shipped shader (08-ps-chunk0-926e); motion variants write the marker at target 1, no alpha discard. Thus disabling Distortion globally cannot solve these particular rectangles. The test is meant to distinguish that from true fire distortion.

HDRP depth is not written by this ForwardOnly material (ZWrite=0, no transparent depth pre/post passes enabled). Our trace, reuse and NRD happen before it. `motion_at` selects the tracer's own camera-hit motion for NRD (`pt_nrd_prep`), along with the traced surface's view depth, geometric normal and roughness. The later VFX draw cannot change that frame's NRD inputs; it changes motion presented to the later temporal upscaler. This also explains a rectangle surviving NRD. The actual upscaler and the switch still require the coordinator's game rerun for causal closure.

### Colour and scale audit

The old code did not use surface colour directly as single-scattering albedo; it already inverted it approximately. The error was the bright end of that fit (white reflected only 0.92599). The new inverse maps example skin (0.60,0.35,0.22) to per-collision survival (0.95105,0.80776,0.64425). Bulk cream (0.90,0.85,0.70) maps to (0.99791,0.99501,0.97597). Both include multiple scattering rather than applying a colour tint each collision.

The plugin's diffusion-profile shape conversion, multiplied by 1000*worldScale, is already inverse metres and stays unchanged. The mean free path is profile distance times s(A)=1.9-A+3.5(A-0.8)^2. For skin distances (0.30,0.16,0.09) mm and the example colour above, flights average (0.43200,0.36140,0.25717) mm. Colour is in the walk's throughput, divided out for the demodulated signal and restored once by compositing, not multiplied twice. Flights terminate by exit/absorption or compensated roulette; the removed hard bound was a separate correctness defect. Material logs also assign a skin profile to some bow/quiver materials: a material-classification issue for the material worker, not proof that these are skin.

### Wax: a separate confirmed classification gap

The candle run's yardstick calls the visible wax `Mat_Prop_Candles_02`, material #134. Its Off audit (`candle/1-game/audit-a.txt`) proves `_DOUBLESIDED_ON`, `_CullMode=0`, `_MATERIAL_FEATURE_SUBSURFACE_SCATTERING`, `_MATERIAL_FEATURE_TRANSMISSION` and alpha testing. `ReadSubsurface` excludes MaterialDoubleSided before assigning a medium, and native MATERIAL_NO_WALK excludes it too. These particular candles therefore never ran the new walk. A two-sided raster material does not prove the mesh is a thin sheet. The yardstick already reports **0% albedo mismatch** for this wax, but 12% normal mismatch and 1% position mismatch. The brown comparison is not evidence of using the surface colour as single-scattering albedo here.

I have not blindly enabled a volume walk for every double-sided material: sheets have no enclosed volume, and doing so absorbs their light indefinitely. Needed: establish closed, consistently oriented connected mesh components at registration, allow the actual solid wax components to scatter independently of their culling mode, retain thin transport for genuine sheets, and carry the correct entry orientation at a back-facing camera hit. This is an unresolved PT-40/PT-73 defect for the coordinator to add to the request list. The new colour conversion helps bright materials that actually walk; it cannot make this excluded candle cream on its own. Do not advertise the scene's wax/skin colour as fixed.

The source run's exposure was stable at log2 -12.66; automatic traced gain 1.3-1.4 (0.4-0.5 EV), traced median -4.9 to -4.6. Read those again after merging before crediting any colour/brightness change. The pale-to-red skin crop was inspected too; skin uses 0.30/0.16/0.09 mm profile distances. Equal-white tests isolate transport; the scene's warm lights and remaining material-program/normal differences still need separation from transport.

### NRD boiling

No NRD blur/history settings changed. The rectangle fix acts on the corrupt later motion, not on NRD blur. NRD only filters bounced light on this tree; direct light and reflection retain the existing filters. The foliage at the top left has no quad footprint. Its causes are not proven by the 16 final frames: stochastic coverage/visibility, material mismatches and moving-plant motion are candidates, not findings. Yardstick in this run reports Hazel place mismatch 12-15% and roughness mismatch 43-44%, grass place 60%; the latter two are relevant to unstable visibility/guide matching and belong beside the materials/moving-world work. No invented light size, extra blur or changed settled history constant was added.

Another finding to track: logs give several leaf materials transmitted shares above one (beech 3.38/3.94/0.68; several flowers 1.43/1.73/0.23). Audit the decoded diffusion tint/mask and energy bookkeeping with the material worker; these logs alone do not prove the final shader creates energy. Not altered here.

## Heat haze design (item 1c: designed, not implemented)

The brief explicitly permits doing a/b and designing c. This job takes that route. A flame's UV noise is not a refractive-index field. None of these candle outputs supplies such a field; no guessed heat column was added. Existing fire graphs **do** expose distortion outputs and parameters. Use the output's LightMode/type for discovery, including off-screen sources and all sizes of fire, never an effect-name whitelist.

Measured live-source examples from `20261007-191814`, all Distortion=true, LegacyScale=64, BlurScale=0.01, SoftParticle=0.2:

| Scene / instance | Distance | Object scale | DistortionSize / ScaleSize | Spawn position (local m) | Alpha | Turbulence frequency / intensity / scale |
|---|---:|---:|---|---|---:|---|
| campfire / 9835772 | 6.27 m | 1.52015 | 1 / (0.6,1.2) | (0,0.3,0) | 0.17 | 3 / 4 / 0.3 |
| night torch / 6192002 | 2.88 m | 1.5 | 0.8 / (0.5,1) | (0,0.2,0) | 1 | 2 / 0.02 / 0.3 |
| tavern-room / 2489772 | 16.14 m | 1.8789 | 1 / (0.6,1.3) | (0,0.7,0) | 0.17 | 3 / 4 / 0.3 |

If the decoded output multiplies these dimensions directly, projected support would be 0.912x1.824 m, 0.600x1.200 m and 1.127x2.443 m respectively. These are **candidate dimensions to verify against the simulation buffer**, not bounds to ship by guess. `DistortionFadeDistance=(0,2)` is camera fading, not a physical volume border. The simulation output's particle transform, size, lifetime, alpha field and animated turbulence must be captured at simulation completion, as the VFX worker is arranging. Renderer bounds are culling bounds and are not sufficient. The generic exposed properties above do not establish a third dimension or a physical index contrast; LegacyScale=64 is not an IOR.

Next implementation:

1. Decode each distortion output's actual particle/buffer mapping, its mask, turbulence texture/time and screen-offset units. Share the game's texture by its existing handle. Establish the field's physical depth from effect data/decoded shape; where absent, record that missing input rather than silently choose a depth. A 2D displacement alone does not uniquely define a 3D refractive field.
2. Define a compact world-space index perturbation n(x,t) using that support and turbulence; border value **and gradient** go to zero. If a source mask lacks a smooth boundary, smooth the *physical density support* with an explicitly justified field model, not a final-image blend. Match the decoded deflection in the weak-field limit; no copied scene colours. Scale/animate from the source, so a bigger fire has the game's own bigger support and strength.
3. Integrate d x/ds = v, d(n v)/ds = grad n using adaptive steps, intersecting real geometry along the curved path. Numerical tolerances to start validation: direction error <1e-5 rad and relative optical-path error <1e-5; refine by halves and prove convergence, not a maximum number of segments. Camera rays first, with their bent hit's albedo/normal/depth and previous-time bent trajectory for motion. One sharp sample; no resampling of noisy output.
4. Use the same world field for reflection and continuation rays (off-screen flames included). Shadow/light connections need a curved connector and its sampling Jacobian. Until that solver exists, light reached by continued paths is the unbiased estimator through a field; a straight shadow ray is not advertised as correct. Do not call camera-only bending full haze.
5. Tests: zero gradient is identity; constant gradient deflection against integration reference; forward/reverse reciprocity; border continuity; refinement convergence; mirror sees off-screen hot air; denoised and undenoised means agree. `night@quick` is the closest useful second look (torch 2.88 m); `campfire@quick` covers a larger fire. `tavern-room`'s nearest recorded distortion source is 16.14 m away.


### Validation update: inverse representation

The first targeted GPU attempt timed out during pipeline creation (180 s): `FAIL: the passes were made`, then ended before the new checks. No result from it validates transport. I attempted to stop only my own test PID 40148 while replacing the large constant table, but it had already exited; no process was stopped. The table is now replaced by a compact seventh-degree Horner polynomial for sqrt(absorption)/(1-A), fitted against the same exact transport solution. This improves accuracy too: maximum diffuse reflectance error 0.000018564 across 4097 colours (the table was 0.000092923). Black/white endpoints remain exact. The mathematical Monte Carlo test (250,000 independent walks per colour) returns 0.220040, 0.351148, 0.601320, 0.700476, 0.849280, 0.899020, 0.970020 for requested 0.22, 0.35, 0.6, 0.7, 0.85, 0.9, 0.97. Worst difference 0.00132. It ran 50,547 steps for the last survivor at 0.97, illustrating why a 4096 cutoff is not a general energy guarantee. No intentional mutants run.

`EffectPasses` state-transition tests run the actual class against a small Unity API stand-in: eight checks pass (generic output detection, valid precomputed motion unchanged, reassertion, late loading, original property/keyword and pass enabled/disabled restoration). This checks our logic; actual Unity variant selection still needs the game. Managed build remains clean. Two-sided subsurface candidates now also log why no volume is assigned, so the excluded candle will be explicit in the next run.


## Coordinator validation and honest remaining scope

Run `candle@quick` (None then NRD), then `night@quick` for the nearby torch; `campfire@quick` for the larger fire. In the first traced-frame log expect `Traced effect material` entries for motion and/or distortion by output, plus the effective camera Distortion bit. That camera bit may now be **true**: the output passes, not the camera, are disabled. A material's `DistortionVectors` must be off while tracing and return to its original enabled/disabled value on Off; candle motion property/keyword must return too. Verify Off against the first game step.

Run flicker.py on both traced steps and --boil on NRD, using that run's game step. Expected direction, not a promised number: no bright hard-edged flame rectangles; their wall strips should move towards the adjacent wall's <1 level/frame instead of 6-8 None / 2-3 NRD. Record the actual reductions and the wall's 16x16 block wander. Whole-frame mean block wander was 0.096 (game 0.037), >1-level blocks 0.69%; those are baseline comparisons, not thresholds proved offline. The leaf region can remain the worst until its own cause is resolved. Changing NRD smoothing to meet a number is not this fix.

Under even white light the scattering material must retain cream/skin/white. **The captured scene's double-sided wax is still excluded from volume transport, so a cream-white in-game candle is not promised by this patch.** Confirm its new explicit exclusion log. The owner still judges scene colour after closure/material/light defects are fixed. Physical haze and foliage boiling are also still open; item 1c is the design above, not an implemented effect.

### Workarounds encountered

- Inherited post-transparent motion repair: removed before this session's edits by the predecessor; verified the inherited native/API/event repair hunks are gone; the six untouched native/API files match base44 byte for byte. HdrpPasses and the GPU test now contain only this job's changes. No new after-denoiser patch or motion texture.
- `SuppressDistortion` camera override and saved camera settings: deleted here; per-effect LightMode suppression replaces it and restores materials.
- New colour-only selection for raster additive effects: transitional adapter, explicitly retained until the VFX worker traces those surfaces and their guides. Retire it with that migration.
- HDRP flame draw remains. NRD does not see the flame's own surface; complete traced VFX geometry/emission/motion is the other worker's job.
- Existing double-sided-means-no-volume rule: retained pending mesh closure/orientation data; recorded as an open defect, not validated as correct.
- Fixed 4096 scattering-flight cap and inaccurate bright-end remap: removed. No denoiser or light-size tuning.
- HDRP compositing/matching depth and external upscaler motion still coexist with traced first hits (PT-72); not redesigned here.

### Files / ABI

Production edits: `src/TGPathTracing/EffectPasses.cs` (new), `HdrpPasses.cs`, `native/pathtracing/shaders/pt_common.hlsli`. Checks: `tests/pathtracing_candle_checks.inl` (new), `pathtracing_gpu_test.cpp`, `candle_transport_test.py` (new), `candle_effects_test.cs` (new), and the changed source-check expectations. Reproduction commands/logs and inverse generator are under `tools/candle/`. No export, shared struct, option bit or ABI change (still coordinator's 0x00012100). Native build is NRD=OFF as required; actual NRD validation is the coordinator's NRD build/game run. Nothing deployed, committed or written to main/game/SDKs.

### Test readiness

The worker runner now allows 600 s for cold pipeline construction (`TGPT_TEST_STARTUP_SECONDS`); the new-check-only mode also uses that allowance. The general default remains 180 s. The GPU test records actual startup seconds and exits if still starting, instead of running empty frames after the timeout. This is not a production startup fix; PT-74 remains the memory/startup worker's. The first attempt's timeout stays recorded above. Obsolete inherited edit scripts were removed; generated build/cache files stay local under tools/candle's ignore file. Automatic review rejected a recursive scratch cleanup, so only the named obsolete text files were deleted via the file-edit tool; no directory outside the copy was affected.

### First completed GPU measurements

The compact inverse's pipelines finished in 172.8 s (other GPU workers were active). The new D3D11 test passed: 220 invisible-border pixels wrote unavailable-motion markers in the shipped-output reproduction, zero in colour-only mode. A simple temporal gather's successive-frame RMS changed from **16.620 to 1.513 levels**. Both variants left the background colour exactly unchanged at invisible pixels. This is offline mechanism proof, not a measured game improvement.

Skin demodulated return was **0.99416 / 1.00095 / 1.00700**, conservative white **0.99424 / 1.00006 / 1.00794**, relative to a diffuse-white environment reference. Both passed 3.5%. The initial cream slab produced **1.05918 / 1.07343 / 1.07241** and failed the assertion that every slab must have its bulk reflectance. Investigated instead of darkening it: the test's 30x12x30 cm block with 10 mm diffusion scale admits light from its other sides under an all-direction white environment. Independent scalar CPU walks returned **1.06676 / 1.07205 / 1.05993**; that extra return is real transmission through a finite volume. The final test therefore retains this finite case against an independent geometric CPU walk, and adds bulk cream at 1 mm diffusion scale, where the same block is optically thick. No transport parameter was tuned to make the failed assertion pass. The nonzero-background-motion preservation check was also added before the final suite.


### Targeted validation and precision finding

`gpu-targeted.log`: all four transport cases passed. Bulk cream 0.99410/0.99994/1.00988; skin 0.99416/1.00095/1.00700; white 0.99424/1.00006/1.00794; finite cream 1.06072/1.07458/1.07044 against independent CPU 1.06654/1.07312/1.06008. The nonzero background motion check passed too.

`gpu-targeted-final.log`: the added fifth case moves the same skin slab to (-110,110,-110) m, within a scene-origin cell. It failed at 0.97532/0.96702/0.95485. The blue loss is 4.5%, exceeding the unchanged 3.5% tolerance. A walk keeps flight positions relative to its entry, but its boundary distance was recovered by subtracting rounded world positions. I tested a shader-local Hit.rayT that preserved the query distance. No offset, tint or test tolerance was tuned. Its completed result is below; this finding remains open.

All 93 source checks pass (`source-final.log`). The source check for the test's startup wait now allows its explicit timeout override, verifies the ordinary 180-second default, and requires stopping if pipelines remain unready.

`gpu-distance.log`: exact query distance made no measurable change in any case (translated skin again 0.97532/0.96702/0.95485). The provisional distance reconstruction change is removed: it did not establish the cause. The remaining candidate is boundary/origin quantization when tracing submillimetre free flights in the +/-128 m coordinate cells, including the rounded primary surface position. This needs precise entry/object-relative transport, not a tint, offset or looser tolerance. The failing translated-skin check stays in the suite and must be tracked under PT-40.

Final native build succeeded (`build-final.log`), NRD off. Native build and staged DLL SHA-256 both **07D6677202D766309CBBD0F222E264AB1195A3308261C5F0127A48FF60171A9D**. The single full-suite run passed the shared gate, completed through `gpu.ps1 -Full`, and exited 1 (`gpu-full.log`); no game was started. Effect state tests can now be reproduced exactly with `tools/candle/check-effects.ps1` (eight passed); final source checks are 93/93.

## Full GPU test (single run)

Completed once, exit 1, **43 failed checks** (`tools/candle/gpu-full.log`). Pipelines took 198.2 s with other GPU work active; do not grade performance from this run. Relevant measured failures include:

- Translated skin: the known 0.97532/0.96702/0.95485 return, unchanged by the final build.
- Existing glass IOR caustics check: the 1.2-index ball returned 1.63 times the opaque twin's blocked light (required 0.5 to 1.1), while its light pattern differed from the 1.5-index one by 1.784 (required >0.3). Named 1.5 vs default matched exactly, and IOR 1 matched straight transmission within 0.051. Glass code was not changed here; without a base44 run I do not call this pre-existing or attribute a cause. Coordinator: track the failed energy check with the glass work.

The full run also reports a non-asserted cache gap: the extreme furnace returns 0.789 rather than 1 because each cache cell is held to 2,000 times the reference radiance (the scene would be 6,000). Normal furnace transport/cache checks pass. This cap changes converged light and belongs on the shortcuts list if not already tracked. It was not changed here and is disabled in the new slab checks.

Existing subsurface tests in this full run pass after the transport correction: 3 mm wax transmits 0.991/0.991/0.991 of the independent CPU reference, 10 mm wax 0.994/0.993/0.992. Thick wax returns 0.983/0.995/0.998 of a painted surface under even light. Behind a slab, the green/red and blue/red shares are 0.930/0.850, matching the independent walk's 0.930/0.851. These validate closed test volumes; they do not enable the excluded double-sided game candles or close the translated-skin failure.

Final totals: 24,429 frames traced, **1,150 failed**, last error **0x887A0006**, disabled=0, 24,428 composites. The preserved native log is `tools/candle/gpu-native-full.log`. At tick 141560531 it reports `Interop fence timeout`, then repeated `Interop fence woke before its value`. Both device-removed reasons are **not lost**; the log's blanket wording about a driver reset is not evidence of a reset. The progress marker had finished prepare at frame 17,230, with frame 17,232 recorded, awaiting bounced light. The bridge uses this error code for an unmet fence wait, too. The root cause is unproven; do not blame the other jobs or this transport change from that code alone. Later images during failed frames cannot establish shader correctness. Some other assertions had already failed before the fence errors, so the timeout is not an explanation for the entire list. No full-suite rerun and no mutation runs were made.

The translated skin test uses independent frame seeds from its origin case. Its reproducible 4.5% blue deficit fails the absolute 3.5% white-environment test; coordinate precision is the leading candidate, not a demonstrated root cause. A paired-seed/object-relative boundary investigation remains open.

### Full-run failure lines

All 43 lines, in order. These are observations from this copy; no base44 full run was made to establish regression status.

```text
FAIL: coloured or white bulk slab retains its diffuse colour under even white light within 3.5 percent
FAIL: glass bends light by its material's own index of refraction (another index lays the light elsewhere)
FAIL: emissive surfaces are sampled as lights except a light's own, moving ones and those past the triangle budget
FAIL: with Emissive Lights off none is sampled, and a light's own surface still lights nothing
FAIL: every mesh with alpha-tested instances has opacity micromaps
FAIL: a mirror shows the wall in the fog as HDRP fogs a view of it from the mirror's place (a constant fog color)
FAIL: a mirror shows the wall in the fog as HDRP fogs a view of it from the mirror's place (the fog's color from the sky)
FAIL: the inside of the hero's first-person body does not show in a mirror 0.5, 1 or 3 m ahead (the back of its head, never drawn from in front)
FAIL: the metallic half of a mask map bounces its specular color's light
FAIL: the dielectric half of a mask map bounces its light as before
FAIL: the metallic half of a mask map is reflected lit as a metal, not black
FAIL: a material without a number, or whose texture died, reads its atlas tile as before
FAIL: the game's normal map tilts the shading at a hit, in the mesh's tangent frame
FAIL: a surface drawn forward takes its roughness from its mask map: read linear, from its shader's channel, remapped
FAIL: a metal drawn forward has no diffuse color
FAIL: a mask map not baked yet reads as its remaps' low ends: the least metal at its roughest
FAIL: KF_Architecture's base layer is its color map times its tint
FAIL: KF_Architecture's smoothness is its mask's alpha times its base smoothness, fading with distance where the material says so
FAIL: grass is its map times its healthy color, toward its dry color by the game's noise over the world
FAIL: TriplanarArchitecture2's layers are laid over the world, not by the mesh's coordinates
FAIL: TriplanarArchitecture2's layer B comes in by its blend map and its height
FAIL: HDRP Lit's detail map lightens the base color as HDRP's Lit does
FAIL: HDRP Lit's detail map moves the smoothness as HDRP's Lit does
FAIL: the yardstick counts a character's pixels
FAIL: a character's pixels move by its own previous positions (the skinning's of the frame before)
FAIL: a surface seen by camera rays only casts no shadow
FAIL: clear glass casts no shadow
FAIL: a character's eye drawn forward is shaded by the tracer, not left to HDRP
FAIL: with Crevice Light, ReSTIR's bounced light reaches the opening of a dark pocket (reuse counts a pixel only for samples it sees)
FAIL: with Crevice Light, ReSTIR's bounced light deep in a dark pocket is neither lost to the boiling filter nor inflated by restarted histories
FAIL: the radiance cache brings the light deep in a dark pocket most of the way to four bounces'
FAIL: frames with 128 characters trace
FAIL: the water's reflections are filled, with the full weight, and finite
FAIL: the water reflects a light panel only the traced scene has, yellow and bright
FAIL: the water reflects the red wall where its mirror sends it
FAIL: the water reflects the green wall where its mirror sends it
FAIL: the water reflects the sky through the ceiling's hole
FAIL: the water's reflections keep their edges
FAIL: where HDRP's SSR found the whole reflection, the traced one replaces it
FAIL: where HDRP's SSR found part of the reflection, the traced one replaces it
FAIL: the fill is divided by HDRP's reflection lighting multiplier
FAIL: the fill is pre-exposed with the traced frame's exposure
FAIL: no frame failed
```

## Handoff

Ready for coordinator review: three production files, the new checks and reproduction tools listed above; no shared ABI change. Native and managed compile; source checks 93/93 and effect state checks 8/8. New GPU quad and origin/finite-volume transport checks pass; translated skin fails. The full GPU run is **not clean**. No in-game improvement is claimed.

Keep open in the coordinator's request/backlog entries: actual double-sided wax volume eligibility; translated submillimetre skin transport; physical haze implementation from simulation data (design above, all fires); foliage NRD boiling; motion-predicate verification on other output families; and the full run's failures/fence timeout/cache cap. Do not mark PT-73, PT-06, PT-40 or PT-41 done from this patch. Follow the candle/torch None-then-NRD validation and exposure checks above.
