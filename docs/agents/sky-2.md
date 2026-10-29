# sky-2: the tracer's own sky (PT-112, the sky) - code worker report

State: STOPPED (2026-10-11, the coordinator's hand-off: usage limit). See "State when stopped" at the end. sky-3 then
finished it (14:12): built, its own sky check passes after four fixes; see "sky-3: result" at the very end. Copy `%TEMP%\tg-agents\wt\sky2`, base `wt\base20sky`, build tree `%TEMP%\tgb\sky2-build`
(NMake, Release, `-DTGPT_WITH_NRD=OFF`: judge with Denoising None). Nothing deployed, nothing committed, the game never started.

## In plain words

- One hidden setting, `Advanced / Sky trial` (0 by default: the game's sky as before). 1 is the measuring switch: HDRP's sky
  work is left out and the path tracer shows the sky its rays read today. 2 is the path tracer's own sky by day (HDRP still
  draws the night sky: the game's stars are not made by the tracer yet). 3 is the own sky day and night (no stars: for
  looking only).
- The own sky is the same physics HDRP's sky uses (air, haze, ozone; sun and moon; HDRP's cloud layer with the game's cloud
  picture), written new, on the tracer's own device. Its tables are made once (when the atmosphere changes); the clouds are
  baked when HDRP would (each degree the sun turns); one sky picture (1024 x 512 with its smaller copies) is made every frame
  for all rays and the fog; the sky the camera sees is drawn per pixel with the sun's and the moon's discs.
- A log line every 10 s counts how often HDRP renders its sky into a cube map (to prove it does so every frame in play).

## What the coordinator runs in the game

Scene `vista` by day and `night`, one step each of `Sky trial` 0, 1, 2 (and 3 at night), 16 frames, Denoising None:
- the frame time and stage times (gpu.json, report.md; the tracer has a new stage "sky"); 1 against 0 is HDRP's sky cost;
- the log: "Sky: HDRP rendered its sky cube map N times ... in the last 10 s" (with 0: N near the frame count in play, near 0
  in the frozen harness world; with 1 to 3: near 0), "Sky trial ...: ..." state lines, "Sky (on trial): the tracer's own sky
  map ...", "...tables made", "...clouds baked at 1024 x 1024 (2 layers ...)";
- the sky's pixels against the game's (1-game atlas): colour of the blue, the horizon's haze, the clouds' look and drift,
  the sun's disc and its flare; and the light on the land (the rays read the new map).

## The switch (sky-1 step 1)

- `Settings.cs`: `SkyTrial` ("Sky trial", int, hidden, Advanced). Key has no forbidden characters.
- `SkyModel.cs` (new): Harmony prefixes skip `HDRenderPipeline.RenderSky` and `RenderClouds` for the gameplay camera while
  `GamePasses.Active` and the trial asks and the camera's sky is HDRP's Physically Based Sky (any other sky: as before); a
  volume of its own (priority 1000002) overrides only `PhysicallyBasedSky.updateMode` = OnDemand (`Add<PhysicallyBasedSky>()`,
  never `Add<T>(true)`); with trial 2 and 3 it calls `RequestSkyEnvironmentUpdate` each time HDRP's sun (its main light, the
  shadow caster) turned 1 degree (for what HDRP still lights itself), with 1 never. A prefix on
  `SkyManager.RenderSkyToCubemap` counts HDRP's cube renders (the sky's cube vs the ambient probes'), logged every 10 s.
- `pt_compose.hlsl`: a pixel whose own camera ray met nothing is drawn by the tracer when `sky_draws_misses()` (mode 1: the
  map the rays read; mode 2/3: `sky_for_pixel`), complete (B alpha 1), at HDRP's exposure alone, under the traced fog
  (fog.a, fog.rgb) as the hair's sky_through; not clamped at 256 (no noise; HDRP's sun disc never was). A-alpha = the rays'
  most distance. Under HDRP's own fog (the hidden test switch) HDRP's fog pass fogs it itself.
- Night in trial 2: the game's sky material's `_SkyBlend` > 0 (DayNightSystem: 0 day, from above 0 the night emission) or
  `_SkyWyrdness` > 0: HDRP draws its sky and clouds, the tracer's map still serves the rays (TGPT_SKY_HDRP_PIXELS).

## The tracer's sky (sky-1 step 3)

Model (written anew; HDRP 17's sources read for its parameterisation, nothing copied; `pt_sky.hlsli` header names the
papers): Bruneton-Neyret 2008 media and transmittance table (its (distance to top, horizon distance) parameterisation,
256 x 64), Hillaire 2020 multiple scattering (32 x 32, 64 directions, Psi = L2 / (1 - f)), single scattering marched
(quadratic steps: 32 for the map, 40 per pixel) with Frostbite's per-step integration, Rayleigh + Cornette-Shanks phases,
ozone as a tent layer, a Lambertian ground of HDRP's ground tint lit through a 256-entry irradiance table (direct + sky,
128 cosine-weighted directions), HDRP's artistic controls (saturations, Alpha Multiplier, Horizon/Zenith Tint and Shift), its
sky intensity; discs and flares as HDRP's celestial body settings (emissive: light / disc solid angle; a moon: lit by its sun
as a Lambertian ball, earthshine), the moon's surface texture through texture sharing; the Cloud Layer bake (slab 800 m,
the layer's steps, two HG octaves) and look (two wind-shifted samples crossfaded, lit by HDRP's sun through HDRP's alpha
controls, ambient from the cloudless sky weighed by a Cornette-Shanks lobe of 0.7 towards the way down, as HDRP's clouds'
probe) with the game's cloud map opened by texture sharing (no copy, or the existing "made again after the hook" twin).

Plugin hands over (`TGPT_SetSkyModel`, every traced frame, from the capture pass): HDRP's planet (HDCamera.planet), the
PhysicallyBasedSky numbers through HDRP's own internal Get* methods (reflection, delegates), the bodies (every enabled
directional light with interactsWithSky, lights first; HDRP's FillCelestialBodyData's quantities computed in the plugin), HDRP's
main light (bakes and lights the clouds), the CloudLayer (HDRP's GetBakingHashCode for when to re-bake; the wind: HDRP's
scrollFactor plus the plugin's own sum while HDRP's clouds are left out, so nothing jumps when tracing stops).

Passes (`pt_sky.hlsl`, one SM 6.6 pass, no rays; `record_sky` after the scene stage, a stage of its own "sky"):
0 transmittance, 1 multiple scattering, 2 ground irradiance (when the atmosphere's numbers change), 3 clouds bake (when
HDRP's baking hash, the maps or the size change), 4 the clouds' ambient (one group, every frame), 5 the map 1024 x 512, 6 its
10 mips (every frame). The map then is `FrameConstants.inputs1.w` (every `sky_radiance`: bounce rays, reflections, glass,
water, the walk, the fog's sky light, the camera-light probe) and, in the fog's Sky colour mode, `fogMip.w` with
`skyMaps2.w` = its mip count (`fog_sky` reads its mips at HDRP's levels). `update_sky` (D3D11 resample of HDRP's cube and its
wait) is skipped in mode 2/3 once one resample exists (Ray Reconstruction's guides keep that one).

## Numbers (ABI, constants, bits)

- `tgpt_api.h`: `TGPT_SkyBody` 112 B, `TGPT_SkyLayer` 80 B, `TGPT_Sky` 816 B, export `TGPT_SetSkyModel(const TGPT_Sky*)`;
  defines `TGPT_SKY_*` (mode 0/1/2; flags 1 HDRP_PIXELS, 2 CLOUDS, 4 CLOUDS_UPPER, 8 SHOW_MAP for tests; body flags 1, 2;
  `TGPT_SKY_MAX_BODIES` 4). TGPT_ABI not changed (the export is bound optionally, as TGPT_SetFogBoxes).
- FrameConstants: + `float4 sky[11]; float4 skyBodies[32]; float4 skyClouds[11]; uint4 skyMaps; uint4 skyMaps2;` at the end:
  8112 -> 9008 B; single rows 49 -> 51. Source checks updated (5 size pins, the row count, the mark number of the bounced
  light's stage end: the new "sky" stage shifts marks 2-9 to 3-10; kStages 10 -> 11).
- No option bits, no frame flags.
- Memory: map 1024 x 512 RGBA16F with mips about 5.3 MB; tables about 0.2 MB; clouds 1024 x 1024 RG16F 4 MB.

## Clean as you go (what stays, why)

Kept, because the old route is still the default and still serves interiors (HDRI skies need cube sharing, sky-1 step 5),
the night in trial 2 (the stars, step 4) and Ray Reconstruction's guides (D3D11 reads `st.sky`): `update_sky` and its
resample, the fog-sky tiles, `TGPT_SetSky`, `HdrpPasses.SkyTexture`, `Session.cs`'s sky pointer, the composite's B-alpha -2
sky mixes and `overSky`. They can go once trial 3 is the rule and HDRI skies are traced.

## State when stopped / progress

- Built: the plugin (net472, 0 warnings). Shaders: pt_sky and pt_compose compile; the whole shader build is running.
- Source checks: 2 failures, the same 2 the base has (test_path_tracing_exposure, test_path_tracing_world_is_what_the_game_draws:
  NativePlantWindFrame's line lists NativeFogBox now), none new.
- Own check: `TGPT_TEST_SKY_ONLY=1` (tests/pathtracing_sky_checks.inl), not run yet. It compares the drawn sky at ten directions
  with a CPU reference of the same model integrated with no tables (`%TEMP%\claude\...\scratchpad\sky_reference.py`, copied
  below), within 12 %. The reference already says what the game's atmosphere gives: under a 100 klux sun 30 degrees up the
  sky is 7 to 18 kcd/m2 and nearly white (R 15301, G 13998, B 12423 at 45 degrees up toward the sun): the game's aerosols (58
  times HDRP's default haze) wash the blue out, as the game's own hazy cream sky shows (vista); 3 degrees up, 500 to 800
  cd/m2, red beside the sun (673, 445, 320).
- Waiting on the coordinator's main-tree build (the shader script's BUILD-FIRST lock) before the remaining shaders compile.

## State when stopped (2026-10-11 ~10:55)

Written (all in the copy `%TEMP%\tg-agents\wt\sky2`; line endings kept as the base's):
- Native: `native/pathtracing/include/tgpt_api.h` (TGPT_SkyBody 112 B, TGPT_SkyLayer 80 B, TGPT_Sky 816 B, `TGPT_SKY_*`
  defines incl. SHOW_MAP 8 for tests, export `TGPT_SetSkyModel`; TGPT_ABI unchanged), `src/runtime.hpp` (Engine.skyModel,
  FrameSlot.skyModel), `src/extension.cpp` (TGPT_SetSkyModel with validation; copied into the slot at submit; reset on
  ClearScene), `src/tracer.cpp` (FrameConstants + sky[11], skyBodies[32], skyClouds[11], skyMaps, skyMaps2: 8112 -> 9008 B;
  kStages 10 -> 11 with "sky" after "scene", marks 2-9 -> 3-10; State fields; Passes.sky + TGPT_PASS(1, sky, sky); release
  in release_all/drop_frame_textures; st.skyModel from the slot; update_sky skipped in own mode; fill_sky before the
  constants upload; record_sky after mark(1)), new `src/sky_model.inl` (make_mip_texture, ensure_sky_textures, fill_sky,
  record_sky), new `shaders/pt_sky.hlsli` (the model), new `shaders/pt_sky.hlsl` (modes 0-7), `shaders/pt_compose.hlsl`
  (misses drawn), `shaders/pt_common.hlsli` (rows; fog_sky reads the map's mips), `shaders/pt_shaders.hpp.in`
  (pt_sky.h), `CMakeLists.txt` (sky:pt_sky in the no-ray list; pt_sky.hlsli APPEND DEPENDS for pt_sky.h, pt_compose.h,
  pt_compose_dxr12.h).
- Plugin: `src/TGPathTracing/Settings.cs` (key `Advanced / Sky trial`, int 0-3, hidden), new `SkyModel.cs` (patches, volume,
  record, wind, count log), `NativeTracer.cs` (NativeSky/Body/Layer, Tgpt.Sky* constants, optional binding SetSkyModel),
  `HdrpPasses.cs` (SkyModel.Patch in Initialize; SkyModel.Send(hd) beside FogBoxes.Send), `Session.cs` (SkyModel.Tick after
  SetIndirectOverride).
- Tests: `tests/source_checks.py` (5 size pins 8112 -> 9008, rows 49 -> 51, the giReuse mark(3) -> mark(4)),
  new `tests/pathtracing_sky_checks.inl` included in `tests/pathtracing_gpu_test.cpp` right after the warm-up's
  "every mesh read back" expect (mode TGPT_TEST_SKY_ONLY=1; reference numbers from the scratch `sky_reference.py`).

Built / compiled:
- Plugin: built (net472, 0 warnings, 0 errors); no plugin file changed since.
- Shaders: `pt_sky.hlsl` (latest, with modes 4/7) and `pt_compose.hlsl` compile with DXC by hand (no errors). The full
  `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\sky2-build` was STILL RUNNING when I stopped (log
  `%TEMP%\tgb\sky2-shaders.log`; 7 of 43 headers done, the rest waiting on the coordinator's BUILD-FIRST lock). Its
  pt_sky.h/pt_compose*.h may predate the last pt_sky.hlsli/pt_sky.hlsl edits: a rerun of the script recompiles them (it
  only redoes out-of-date headers). C++ (tracer.cpp, extension.cpp, sky_model.inl) NOT compiled yet: expect small fixes.
- Source checks: 2 failures, the same 2 as the base (exposure; world_is_what_the_game_draws), none new.
- Own check: NOT RUN.

Next steps for whoever continues:
1. Let the shader script finish (or rerun `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\sky2-build`), then
   `%TEMP%\tgb\sky2-build.bat` (vcvars; cmake --build of TGGraphicsBridge.Native, TGPathTracing.Native, tgpt_gpu_test; copies
   the two DLLs into `%TEMP%\tgb\sky2-install\<mod>\`; check the DLL output paths, they may sit in a subfolder). Fix
   compile errors (likely spots: sky_model.inl's lambdas row/bits, offsetof needs <cstddef>).
2. With the game closed and the main tree's `Run-PtHarness.ps1 -GpuCheck` at 0: `set TGPT_TEST_SKY_ONLY=1` and run
   `%TEMP%\tgb\sky2-build\tgpt_gpu_test.exe %TEMP%\tgb\sky2-install %TEMP%\tgb\sky2-images`; read sky-*.png and the
   printed numbers (CPU reference within 12 %, map vs per-pixel under 5 %, sun disc > 1e8 cd/m2, clouds over part of the sky).
3. Then the coordinator's game run (section "What the coordinator runs in the game").
Risks to watch: the per-pixel sky (40 steps x 2 bodies) costs more than sky-1's estimate on a sky-filled screen; HDRP's
transparents keep HDRP's frozen sky between the per-degree updates; at the day/night switch of trial 2 the clouds' wind
offset takes one frame to rejoin HDRP's; the night sky with the game's moon at 60 klux is moonlit "daylight" in HDRP's
model too (the game's star layer and exposure make the night: sky-1 step 4).

## sky-3 (2026-10-11, from 13:21): finishing the build

- 13:21: the shader script of 10:56 ended at 11:31 (43 compiled, 0 failed), but pt_sky.h (10:32) and pt_compose.h (10:33) predate pt_sky.hlsl (10:44) and pt_sky.hlsli (10:40): rerunning the script (only out-of-date headers).
- 13:30: syntax check (cl /Zs, out of tree, the build tree's flags): tracer.cpp (with sky_model.inl) and extension.cpp clean at /W4; tests/pathtracing_gpu_test.cpp had one error: in `tests/pathtracing_sky_checks.inl` a printf's `\n` had become a real line break (line 154). Fixed; the test compiles.
- 13:31: source checks: 116 run, 2 failures, the same 2 as the base (test_path_tracing_exposure, test_path_tracing_world_is_what_the_game_draws); none new.
- The shader rerun waits on the coordinator's BUILD-FIRST lock (main tree build running); 2 headers to compile (pt_sky.h, pt_compose.h).
- 13:25: fixed in `pt_sky.hlsl` mode 6 (the map's mips): the last mip (1 x 1) averaged a 2 x 1 mip with two texels read past its edge (0 in D3D12), so the map's roughest mip came out half as bright; the four reads are now clamped into the mip above. Read-through of extension.cpp's TGPT_SetSkyModel: its word indices of the integer fields match tgpt_api.h's layout (header 0-3, 41-43, a body's 7 and 23, a layer's 11, 15, 17, 18, 200 on).
- Plugin: dotnet build (net472) succeeds, 0 warnings, 0 errors (up to date).
- 13:38: shader headers done (pt_sky.h, pt_compose.h compiled, 0 failed; the rerun had waited 17 min on the coordinator's BUILD-FIRST). 13:39: native build started (`%TEMP%\tgb\sky2-build.bat`, log `%TEMP%\tgb\sky2-native.log`).
- 13:39: native build OK (bridge, TGPathTracing.Native, tgpt_gpu_test; no errors, no new warnings in our files); staged into `%TEMP%\tgb\sky2-install` with the Agility runtime (D3D12 folder copied from the main tree's artifacts, read only). DLL hash matches the build (25665E1E...).
- 13:42: first run of the sky check (GPU free, game closed): stopped by the test's own 180 s wait for the passes (cold pass cache: prepareDxr12 83 s, walkDxr12 168 s of driver compile; the `sky` pass itself made in 0.2 s). Rerun with `TGPT_TEST_STARTUP_SECONDS=900`; at 13:43 the game was running (the coordinator's run), so the rerun waits for `-GpuCheck` to exit 0.
- 13:53: second run (900 s for the passes): the passes made, the check ran: 13 FAILs. The PNGs show why: the test's camera (at z -9, 2 m up, 25 degrees up) stood in front of the warm-up's open room, whose walls and ceiling filled the picture; the sky showed only in two thin bands. So the reference directions hit walls (e.g. 605 660 491 against 15301 13998 12424), the sun's disc was behind the ceiling (1.26e3 cd/m2), the map-vs-pixel compare took the walls' path-traced noise (8.4 %, 18.3 %), and (60 up, 30 round) lay outside the picture (25 + 35 = 60 at the centre only). Where a reference direction did see sky (the top band) the numbers were within about 15 % but not at the exact direction.
- Fix (test only): `skyCam` now at (0, 2, -400) looking 35 degrees up (the picture 0 to 70 degrees up; every reference direction in it): the room is 400 m away, under 1 degree above the horizon; 2 m up as the reference. Rebuilt the test.
- 13:56: third run (camera fixed): 10 of the 13 now pass. CPU reference within 2.6 to 6.0 % at the 30-degree sun and 2.6 to 8.9 % at the 3-degree sun (the tracer a few % brighter everywhere); map vs per-pixel 0.09 % and 0.26 %; the sun's disc 1.67e9 cd/m2 (its light over its solid angle: 1e5 / 5.98e-5 sr); the setting sun's surroundings red. Left: (a) no clouds at all (0.0 %); (b) 11 degrees up beside the 3-degree sun 733 500 380 against the reference's 673 445 320 (18.8 % in blue).
- (a) found: `fill_sky` handed the shaders the plugin's raw shared-texture HANDLES (number plus a serial in the high bits) for the cloud maps, the flowmaps and the bodies' surface textures, where every other reader turns a handle into its heap number first (`share::resolve`, as the rain's noise and the fog boxes' masks). Fixed in `sky_model.inl`: a local copy of the frame's TGPT_Sky with those resolved (-1 while not open; an off layer and bodies past the count: -1), used for the bake key too (so a map that opens later bakes again). The plugin's own handles stay handles.
- (b) checking: sky-2's reference bins its multiple scattering coarsely (heights by 2 km, the sun's cosine by 1/16: a 3-degree sun is taken as 3.6 degrees); a finer reference (`scratchpad\sky_reference_fine.py`: Psi at each step's own height and cosine) is running.
- 13:59: fourth run (resolve fix): the clouds show (83 % of the sky changed at the 30-degree sun, 98 % at 3: the test's own map is heaps plus long streaks everywhere). The only FAIL left was (11 up, 3-degree sun). The finer reference settles it: there the tracer is 5.4 to 6.3 % ABOVE it (733 500 380 against 695 470 360), and over all ten directions within 6.3 % (30-degree sun: 1.9 to 6.0 % below; 3-degree sun: 2.7 to 6.3 % above). The 18.8 % was the first reference's binning. The test's table now holds the finer numbers (tolerance kept at 12 %).
- Seen in the PNGs (sky-clouds-map-30): the clouds in the MAP had dark hearts where the camera's clouds are white: `sky_for_map` left out the atmosphere's light in front of the clouds that `sky_for_pixel` adds (haze x the clouds' opacity), so reflections and bounced light would see darker clouds than the camera. Measured with a new test line (the cloudy map against the cloudy per-pixel sky, same mask as the clear one): 11.44 % apart at the 30-degree sun, 2.81 % at 3. Changed `sky_for_map` to add the same haze (no new cost: the march already returns it); a check (under 10 %) added. sky-2's comment said the map holds the clouds "as HDRP bakes them into its cube map"; whether HDRP's cube adds that haze I could not read here (no HDRP source in reach): this follows the owner's mirror rule (a reflection shows the world as the camera sees it). Easy to undo: one term in `pt_sky.hlsli`.

- 14:12: fifth run: **sky checks passed** (exit 0). Numbers below.

### sky-3: result (2026-10-11 14:15)

State: DONE for this job. Built, the own check passes, nothing deployed, nothing committed, the game never started by me, nothing written to the main tree. The coordinator merges.

What the check shows now (`TGPT_TEST_SKY_ONLY=1`, Agility runtime, Denoising None; log `%TEMP%\tgb\sky2-skytest6.log`, PNGs `%TEMP%\tgb\sky2-images\sky-*.png`):
- Sun 30 degrees up: the drawn sky against the finer CPU reference 1.9 to 6.0 % apart (the tracer slightly darker); away from the sun 6911 6684 6722 cd/m2 (luminance 6735), 4-15 degrees from it 19161 17296 14799; near-white, as the game's dense haze makes it. The map the rays read vs the per-pixel sky: 0.09 %. The sun's disc 1.67e9 cd/m2 (1e5 lux over its 5.98e-5 sr). Clouds: the map's clouds vs the camera's 0.56 %.
- Sun 3 degrees up: 2.7 to 6.3 % apart (the tracer slightly brighter); beside the sun red and orange (522 375 307), the sky above greyish violet; map vs per-pixel 0.26 %; clouds map vs camera 1.55 %; the disc 3.22e8 cd/m2 (dimmed by the long way through the air).
- PNGs: clear skies are smooth gradients with no banding or seams; the sun's disc with its flare (the test sets flare = disc radiance, so it shows as a white ball about 4 degrees wide); the room 400 m off is a speck at the bottom. Clouds: soft heaps and long streaks from the test's own noise map, white heaps under the 30-degree sun; near the top edge (towards the zenith) the streaks converge into spokes: the test map's longitude lines at the pole, not a fault of the sky. Under the 3-degree sun the clouds stay white-grey while the sky darkens: with Alpha Multiplier 0 (the game's), HDRP's controls let the sun reach the clouds undimmed, as sky-2 designed after HDRP.

What sky-3 changed (all in the copy):
1. `tests/pathtracing_sky_checks.inl`: a printf's `\n` that had become a real line break (compile error); the camera moved to (0, 2, -400), 35 degrees up (it stood inside the warm-up room's view); the reference table replaced by the finer reference's numbers; a new check that the map's clouds match the camera's (under 10 %).
2. `native/pathtracing/src/sky_model.inl` `fill_sky`: shared-texture handles (cloud maps, flowmaps, the bodies' surface textures) turned into heap numbers with `share::resolve` each frame (before: no clouds at all, and the moon's texture would have read a wrong slot); the bake key uses the resolved numbers.
3. `native/pathtracing/shaders/pt_sky.hlsl` mode 6: the last mip's reads clamped into the 2 x 1 mip above (it was half as bright).
4. `native/pathtracing/shaders/pt_sky.hlsli` `sky_for_map`: the atmosphere's light in front of the clouds added, as `sky_for_pixel` has it (before: dark-hearted clouds in reflections and bounced light; 11.44 % -> 0.56 %). A design point for the coordinator (see the 13:59 line): it follows the mirror rule; one term to undo.
- Source checks: 116, the same 2 failures as the base. Plugin: builds, 0 warnings. Line endings of the four files: LF, as sky-2 wrote them.
- The CPU references: `%TEMP%\tgb\sky2-sky_reference_fine.py` (sky-3's, used for the table; `python <it> <sun deg> 80 64 24 "el,az" ...`, about 30 s a direction) and sky-2's `%TEMP%\tgb\sky2-sky_reference.py`.
- To run the check again: build with `%TEMP%\tgb\sky2-build.bat` (after `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\sky2-build`), the install `%TEMP%\tgb\sky2-install` holds the Agility runtime; then `TGPT_TEST_SKY_ONLY=1` and `tgpt_gpu_test.exe %TEMP%\tgb\sky2-install %TEMP%\tgb\sky2-images`. A cold pass cache needs `TGPT_TEST_STARTUP_SECONDS=900` (prepare and walk took 83 and 168 s of driver compile the first time).

Not done (outside this job): the whole GPU test, the game run of "What the coordinator runs in the game", mutation checks (the owner's rule: not before the game says the sky looks right), docs outside this report.
