# Technical context

Read the relevant section and DEAD ENDS before changing rendering code. Current work is in [BACKLOG.md](../BACKLOG.md), PT boundaries in [PT-STATE.md](PT-STATE.md), released-mod lessons in [PROJECT-CONTEXT-MODS.md](PROJECT-CONTEXT-MODS.md). Complete earlier evidence remains in the [local archive](DEVELOPMENT.md).

## Documentation

The README names the project TGGraphicsEnhancements and gives a short overview of all four mods. Status covers
the whole project. Keep detailed setup, settings and build steps in INSTALLING.md, SETTINGS.md and BUILDING.md.
Preserve commands, measurements, issue IDs and validation limits when editing prose.

## Engine and frame

- Unity 6000.3.21f1 since game 1.31, previously 6000.0.64f1; some executable/log metadata still names 6000.0.41. HDRP 17, D3D11. Shipped DXR shader variants are stripped. The bridge owns one shared D3D12 device.
- Gameplay camera is Camera.main/MainCamera; rect is not bit-exact fullscreen: compare pixel size within two pixels. Ignore Atlas/thumbnail cameras. Autosave thumbnails can briefly leave no game camera.
- beginCameraRendering follows HDCamera.Update and occurs in TryCull. Set camera changes in beginContextRendering. endCameraRendering is after final Submit: use Graphics.ExecuteCommandBuffer for same-frame execution.
- HDRP renders Y-inverted then flips at final blit; depth/motion orientation differs from presented colour. All FG backends need the measured correction.
- GBuffer -> opaque SSR -> lighting -> sky -> AfterOpaqueAndSky -> opaque fog -> transparents/transparent SSR -> post. STP is BeforePost; grain is final. BeforePost follows exposure/BeforeTAA effects.
- HDRP GBuffer3 contains baked/probe light, not only emission. Indirect diffuse multiplier stays zero under PT. Volume overrides use Add<T>(), never Add<T>(true), which resets unrelated parameters.
- HDRP previous VP uses positions relative to the current camera. Supply denoisers correctly signed/scaled motion; character motion uses differences of nearby floats, avoiding cancellation at kilometre coordinates.
- Only DLSS requests exposure readback normally; other passes request it explicitly. HDRP passes Clamp(GpuExposureValue,0.2,2) to DLSS. Gameplay exposure is authored; do not replace it with global HDRP auto exposure.
- Unity 6000.3 changed reflected CustomPass.ExecuteInternal calls. Compile success cannot check reflection signatures. PT also has managed API/framework build blockers; see PT-STATE.

## Capture and materials

- Static world: Drake ECS. Castle kits: Medusa BRG and Entities Graphics without Drake. Vegetation: Leshy/VSP. Characters/hands: Kandra world-space GPU skinning.
- Kandra_Lit retains a G-buffer pass. Eye is DepthForwardOnly; RealHair has alpha-tested and transparent passes, with transparent-only brows. TG/NoRender and Invisible material slots hide body parts under armour.
- Capture all renderer families, shadow-only/layer geometry, terrain, decals and real displaced water. Do not drop cluster stand-ins before replacement geometry resolves.
- Follow each renderer's own LOD rule; use resolved HDRP LOD bias during culling. Keep old LOD until replacement is ready. No fixed scene radius.
- SceneCapture scans about every 2 s, or 0.5 s after 1 m movement/LOD changes, with incremental uploads. Slow arrival/stutter remains PT-124.
- Characters copy live skinned output each frame; cloth poses per instance; plants per 16 m wind cell. Match game wind/view fades, not generic swaying.
- TextureShare/MaterialPrograms are the main route. Atlas readers remain; do not claim all texture copies or capacities are removed. Camera/secondary surfaces must share shading.
- Skin diffusion profiles include THIN human skin; sclera/teeth/zombie profiles can be Regular. Cache decoded profiles, not re-rented array positions.
- Game mask maps and shader property layouts vary. Read the decoded shader/program, not generic _Color/_MainTex assumptions. HDRP Unlit uses _UnlitColor/_UnlitColorMap.
- Physical emitter identity comes from attachment/content. PointLight_Ambience can be a candlestick's only real light. LightWithOverride caches volumetricDimmer and restores it; mutating it may leave permanent game state.

## Bridge, build and settings

- All extensions share device/queue ownership through the bridge; fences have one signalling queue each. Raw pointers/COM/plain values cross the ABI, not C++ runtime objects.
- Static MSVC runtime avoids dependence on missing/older game MSVC DLLs. Use the matching SDK static NGX library.
- Runtime capabilities determine available choices. Video and Mod Manager rows match exactly. Apply documented safe fixes to game settings, then grey those settings with an explanation.
- Build.Local.props is local and ignored. Verify current game branch/assemblies before building/deploying. Vortex can restore older binaries; replace symlinks with real files, then hash build/stage/game.
- Native fixtures load artifacts/Native, updated only by cmake --install. Package.ps1 packages existing builds and requires the build manifest. Packaging overwrites release zips.
- Generated build caches are disposable, not dependency requirements. Retire stale caches with superseded SDKs; check current source and build scripts for actual consumers.
- See README/TESTING/BUILD-DEPENDENCIES for commands. Shader/header changes can trigger all variants and long driver compilation; do not rebuild rendering for docs.
- NRD configure writes NRDConfig.hlsli into its SDK. Read-only SDK builds must use a matching prebuilt NRD library/dependencies. Normal packaging preserves its notices.

## PT integration

- The last active baseline used ABI 0x00012700. Validate managed/native sizes against tgpt_api.h and NativeTracer.Validate. Read current headers and writers before allocating fields or flag bits.
- Prepare uses primary_all for own camera hits. The old GBUFFER_SURFACES route and atlas/surface-offset readers remain PT-60 cleanup.
- GamePasses empties opaque draw lists and suppresses duplicate decals/shading. Traced depth/motion also populate the HDRP depth pyramid, read by fog/transparency. Sky/VFX remain incomplete.
- Same-frame trace/composite with parity buffers and async compute; no merged one-frame-late path.
- SM6.6 inline rays; DXR1.2/SM6.9 via Agility/DXC for SER/micromaps. SER GI regrouping is keyed by first-hit material after minimizing live state; reuse is separate. Plain twins are measuring references.
- Plant micromaps settle only proven coverage. Grass/leaves level 6; covered foliage is opaque except separate light-ray transmission. Character micromaps were removed after clean off/on/off tests found no gain.
- Retained records must name stable identities, never positions in rebuilt frame buffers or moved BLAS allocations. PT-125/134/135 corrected plant ranges, mesh-decal addresses and emitter indices.
- Light candidates include near/far strata and a lamp search tree. NO_RANGE limits candidate sampling; emitted light continues beyond that boundary. Avoid counting an emissive surface and its associated light twice.
- DI history 4, Basic spatial reuse, permutation each frame, final visibility traced; spatial feedback off. GI history 8/max age30, reprojection search/Jacobian/NaN checks; fallback sampling off. Departures remain vendor-audit work.
- NRD receives all demodulated diffuse/specular, shading normals, real distances, 2.5D motion and strand IDs. RELAX/confidence default. Static flat mirrors use reflected virtual surface guides. Moving/curved/nested cases, glass/water and SH recovery remain gaps.
- RR runs through BU on the shared D3D12 device, Default/F policy; hair/reflection-distance proposals need game comparisons. RRG uses runtime query, sqrt albedos, oct normals and 2.5D motion; AMD validation pending.
- Camera cuts reset histories: camera change/HDRP rising reset, >60-degree turn or >20m jump. Radiance cache survives cuts.
- Skin scattering uses traced walks, not Burley blur. Glass uses Fresnel/GGX/Snell/TIR/absorption/material IOR and compensated roulette. Hair is a camera-ray surface with motion, remapped stochastic coverage and strand highlights.
- GGX denominator guard remains max(PI*d*d,1e-30); 1e-7 flattened sharp highlights. Unlit f0=0, roughness1, Schlick f90=saturate(50*f0). Reflection Boost includes armour but exempts skin/foliage.
- Fog: /16 screen grid, 96 slices, full ray distance; exact per-ray extinction, cached light interpolated between centres, per-pixel near-lamp residual. Fog follows surface denoising; NRD does not denoise it. Secondary-ray candidates/cache coverage remain open.
- Traced gain uses 4x4 block means to avoid noise depressing the median. Compare denoisers at equal gain. Brighten=min((-3.06-p50)*DarkAdaptation,0.5-p98), bounded by authored EV/darkest EV7 and 12 stops; exposure -3..3; ease up1.5/down3 EV/s.
- FP16 storage reference is eased from sun/lamps. PT-136 bounds outlier contribution to storage scale without cutting physical emitted light. Read reference/gain logs before diagnosing dark ends.
- The radiance cache uses a world hash with 2^20 slots, eight slots per bucket and distance-scaled 0.1 m cells keyed by dominant normal. It stores incoming E/pi. Remaining limits are 512 samples per cell per frame, a cap of 2,000 per channel, EMA >= 1/16 and expiry after 240 frames. Track these limits in the correctness audit.
- Capacity gaps include 256 skinned renderers, 8 materials/skinned mesh, 256 cloth, 4096 wind records, four water slots and atlas limits. Check source before changing them.
- Mega Geometry is required on supported hardware. The managed default is 7 and the local build cache has it enabled; fresh CMake configuration defaults to OFF (PT-149). PT-STATE records the trial-transition bug (PT-148) and missing grouped-plant work.

## DEAD ENDS

Do not retry without new evidence and a written reason.

- HDRP OpaqueObjects=false causes repeated ECS uploads (~800 ms frames). Empty opaque lists instead. D3D11 differently sized MRT/depth attachments silently lose writes; populate the depth pyramid separately.
- HDRP's built-in ray tracing cannot run stripped variants. GI/hybrid mode, excluding hair/eyes, post-denoiser hair layers and borrowed screen reflections violate renderer requirements.
- Fog Add<Fog>(true) resets its state. Scaling volumetricDimmer sticks through LightWithOverride. Undimmed sky ambient reproduces HDRP's shortcut, not physical fog.
- Character micromaps showed no gain: tavern 36.20/36.25/36.20 ms, town 67.81/68.17/67.40. Hair opacity bounds must cover every mip and anisotropic reader.
- DI history 20 boiled thin geometry. Visibility reuse widened a lamp's shadow edge from 0.8 to 14 px (Basic), or 4.4 px with correction, for a 7-13% noise reduction. Spatial feedback added 6-10% error. Keep defaults until new game evidence supports a change.
- Rejected NRD bounced-light-only setup used wrong normals and distance 1. Correct vendor inputs take all light. Do not revive that conclusion. Denoised references rewarded blur; use raw convergence references.
- Raw 3x3 variance mistakes shadow edges for noise. Uniform light selection produced fireflies. HDRP range cutoffs darkened rooms; a 10 cm light-radius floor widened penumbrae.
- Shadow-only/cluster exclusions removed roofs. Name-only light blacklists removed real candle light. Un-zeroing baked indirect filled shadows and made skin glow.
- The caustic shadow-ray cut measured 4.20/4.30/4.05 ms off/on/off, with no gain. Water split/regrouping runs had GPU contention and need a clean comparison.
- The reported 3 FPS camp run placed the camera inside a hill. Ground-ray changes left its cost unchanged; use a grounded camera for water measurements.
- D3D11 NGX RR crashed; RR uses shared D3D12. Do not change QualitySettings.maxQueuedFrames mid-run: restoring it hung the game.

## Measurement traps

- WRL &ComPtr is an out-parameter helper and can empty it. Use std::addressof for pointer arrays.
- Raw buffer views align to 16 bytes; violations can remove the device. Hooks on driver threads have tiny stacks: no large local arrays.
- GPU stop markers identify stage/stamp; released views and timeouts both appear DEVICE_HUNG.
- Time combines game D3D11 work, traced D3D12 work and waits. Never subtract an old trace from a new frame. Foreground/stream conditions and scene-load order must match.
- Town lamp state depends on previously loaded scenes (PT-147). Running steps advance game time; compare light in frozen steps. Moving frames need disableMotionBlur:true.
- Harness step pictures are at walk start; walk-end.png is the endpoint. @recipe applies to its one scene. Exit 1 with flagged metrics can still mean completed capture.
- Whole-test failures predate the latest PT changes. Offline fixtures do not run RR. Confirm the target path ran before interpreting a passing result; mutation checks must reach the code they break.
- JsonUtility silently loses nested request classes/arrays; use game Json.NET. Get-Counter failure means unknown, not idle. CopyFromScreen can capture a DPI-scaled corner.
- Editing embedded HLSL needs CMake reconfigure. Restoring mutant files with old timestamps can retain mutated objects. Preserve line endings.
- Check BepInEx Loading lines, not folders. Reflection-only API failures evade compile checks. Do not reuse expired launch requests or infer game appearance from successful native evaluation.
