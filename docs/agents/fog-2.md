# fog-2: PT-112 step B, the game's fog traced by TG Path Tracing (design, no code)

Worker report, 2026-10-11. Brief: `docs/agents/briefs/brief-fog-2.md`. Read-only research: nothing built, run, deployed
or committed; this is the only file written. Sources read: the game's own HDRP DLL (ilspycmd), HDRP 17 sources
(Unity-Technologies/Graphics, branch `6000.0/staging`), the harness audits and effects inventory, the tracer's code.

State: DONE.

## Summary (one screen)

**What HDRP does today.** HDRP keeps a coarse 3D grid in front of the camera (a "froxel" grid: screen tiles times depth
slices, out to the Fog volume's Depth Extent). It fills it with the fog's density, lights each cell with the sun (through
shadow maps), its lamps and the sky's light, and lays the result over the picture. Past the grid's end it adds a "distant
fog": the sky's blurred colour times a tint, lit by nothing. The game's grid is small: outdoors 322 x 182 x 82 cells at our
render size, in the tavern 196 x 110 x **6** slices. Outdoors HDRP lights the fog with the sun and moon ONLY (the game sets
"directional lights only"): no torch or campfire lights the mist out there. Indoors many lights light it at 3, 8 or 16
times their real strength (each light's "volumetric dimmer"), most with no shadow.

**What I recommend: the tracer computes the fog along each ray itself, for every pixel ("per-pixel fog", design B
below).** For each camera ray: how much fog it crosses (exact, from the game's own fog numbers), and the light the fog
sends to the camera, found by picking one point in the mist and lighting it with the sun, one lamp and the sky, each with
a real traced shadow ray. The same code runs on mirror, glass and water rays, so a mirror shows the same fog. No grid, no
reach limit: shafts are as sharp as the screen. The noise is cleaned by the fog's own reuse over frames. HDRP's
transparent effects (still HDRP's until step C) read a grid in HDRP's format that we fill from the same samples, so
they sit in the same fog. Then HDRP's fog passes are switched off by Harmony prefixes.

**Its cost (estimates; the trial measures them).** About 3 shadow rays a pixel. In the town that is about 3 to 6 ms a
frame at full rate (the direct light's whole stage is 6.5 ms), 1.5 to 3 ms if each pixel samples every other frame (more
noise, same picture in the end). About 42 MB of video memory. A grid of our own (design A) would cost about the same per
frame, blur the shafts to the grid's cells, and still need design B for mirrors and the far fog.

**The first trial (half a day, one scene, hidden switch):** the camera's fog only, the game's height fog only (no local
fog volumes), lit by the sun, the traced lamps and the sky with traced shadows, no denoiser; HDRP's opaque fog pass
skipped; Fog Adaptation off. Scene: `fog` (save "PT fog", thick grey mist, no local fog volumes). It should look like the
game's mist, with the mist a little darker under the big dead tree and rock overhangs (they shade it from the sky). The
deciding numbers: the mist's median brightness in the scene's frame within 20 % of the game's, the far shapes' contrast
within 20 % (the thickness is the same formula), the exposure line moving less than 0.5 EV, and the new pass's ms.

**Questions for the owner (section 5):** (1) which lights light the mist: the game's choice (sun only outdoors; many
lights at x3 to x16 indoors) or the real one (every traced light at its real strength, the fill lights' fog part as now);
(2) the far haze: traced (sky and sun scattered in the fog) or the artists' tint kept on top; (3) a coarser, cheaper fog
as a speed choice, with numbers, once measured.

## 1. What HDRP 17 does for fog in this game, end to end

Order of a frame (the game's DLL, `HDRenderPipeline.RecordRenderGraph`, decompiled; HDRP source
`HDRenderPipeline.VolumetricLighting.cs`):

1. `PrepareVisibleLocalVolumetricFogList` (source line 852): local fog volumes, culled by the frustum and dropped when
   farther than their own `distanceFadeEnd` **or the Fog volume's Depth Extent** (line 887). At most
   `maxLocalVolumetricFogOnScreen`, sorted by priority.
2. `GenerateMaxZPass` (a max-depth mask from the depth pyramid, which since step A is the tracer's depth).
3. Deferred and forward opaque lighting, then the sky (`RenderSky`), then our composite (custom pass AfterOpaqueAndSky).
4. `ClearAndHeightFogVoxelizationPass` (line 1055; `VolumeVoxelization.compute` line 84-98): each froxel gets the height
   fog's density at its centre: scattering `_HeightFogBaseScattering * m(h)`, extinction `_HeightFogBaseExtinction * m(h)`,
   with `m(h)` 1 below the base height and `e^-((h - base)/H)` above, `H = 0.144765 * (maximumHeight - baseHeight)`
   (`Fog.cs` line 199, 262). `_HeightFogBaseScattering` is albedo times extinction while the volumetric fog is on, else
   extinction (`Fog.UpdateShaderVariablesGlobalCBFogParameters`, line 237).
5. `FogVolumeAndVFXVoxelizationPass` (line 1127): local fog volumes are DRAWN into the density grid (instanced boxes with
   the "Fog Volume" shader graph, `LocalVolumetricFog.PrepareDrawCall`). The default graph (texture mask mode):
   extinction = mask x `1/meanFreePath` x fade, scattering = `saturate(albedo x extinction)` (`ShaderPassVoxelize.hlsl`
   line 158-172); fade = the box's face fades (linear or exponential) and the camera-distance fade
   (`distanceFadeStart/End`); blend modes Additive (all the game's), Multiply, Min, Max, Overwrite. VFX Graph particles
   with a "volumetric fog" output draw here too (whether the game has any: unknown, see risks).
6. `VolumetricLightingPass` (line 1315; `VolumetricLighting.compute`): one thread per froxel column marches the slices
   (`FillVolumetricLightingBuffer`, line 528). Per slice: one random point (one shared random number for all lights:
   `GenerateHashedRandomFloat`, line 620, plus a 7-step offset when reprojection is on).
   - The sun/moon: `EvaluateVoxelLightingDirectional` (line 157): shadow from the cascades at the "ultra low" filter,
     times the light's `volumetricLightDimmer` (line 250) and `volumetricShadowDimmer`; HDRP's height fog dims the light
     on its way (`LightEvaluation.hlsl` line 266, `OpticalDepthHeightFog`).
   - Lamps (only when the Fog's "directional lights only" is off: keyword `SUPPORT_LOCAL_LIGHTS`, line 1349 of the C#):
     `EvaluateVoxelLightingLocal` (line 291): every light of the big tile, an equiangular sample along the slice
     (`ImportanceSamplePunctualLight`, line 357), the light's shadow map if it has one, times its volumetric dimmer
     (line 370), the fog on the light's way (line 486 of `LightEvaluation.hlsl`).
   - The phase: Cornette-Shanks with the Fog's anisotropy, taken at the froxel's centre direction.
   - The sky: `EvaluateVolumetricAmbientProbe` (line 90): HDRP's sky as spherical harmonics, already folded with the
     phase function, times the Fog's "GI Dimmer" (`globalLightProbeDimmer`; `SkyManager.cs` line 1206). No shadow: an
     overhang does not shade the mist from the sky.
   - Opaque geometry stops the march (`containsOpaqueGeometry`, line 581): nothing is stored behind what the camera saw.
   - What a froxel stores (line 752-762): the in-scattered light summed from the camera to the end of the slice and the
     optical depth to its middle, as `LinearizeRGBD`, times the exposure.
   - Reprojection (only with denoising mode "Reprojection" or "Both"): a 7-frame moving mean in log space
     (`ComputeHistoryWeight`, line 132-148), the phase kept out of the history. Gaussian filter
     (`VolumetricLightingFiltering.compute`) when the mode has "Gaussian".
7. `RenderOpaqueFog` (`OpaqueAtmosphericScattering.shader`, `AtmosphericScattering.hlsl` `EvaluateAtmosphericScattering`,
   line 241): every pixel reads the grid at its screen position and distance (line 285-301); past the grid's last slice
   the "distant fog": transmittance of the analytic height fog over the rest, and light `GetFogColor(V, t) x exposure x
   albedo x (1 - T)` (line 328). `GetFogColor` (line 23): the constant colour, or (this game, "Sky Color" mode) the tint
   times HDRP's sky cubemap at a blurred mip. The sky's pixels are fogged as a surface at Max Fog Distance (line 266).
8. Transparents and water (`RenderTransparency`, `RenderWaterLighting`) call the same `EvaluateAtmosphericScattering` at
   their own depth, reading the same grid (`_VBufferLighting`).
9. `ScreenSpaceFogMultipleScattering` (the game's DLL, `RenderTransparency`): with the Fog's "multiple scattering
   intensity" above 0 (1 outdoors in this game), a screen-space blur of the picture by the fog's opacity: a glow around
   bright things in mist. A screen-space stand-in, not traced light.
10. Grid size (`ComputeVolumetricFogSliceCountAndScreenFraction`, line 367; the game's DLL the same): in "Balance" mode
    screen fraction = lerp(6.25 %, (1 - ratio) x 43.75 % + 6.25 %, budget), slices = (int) lerp(1, max(1, ratio x 512),
    budget). The game's Fog Quality setting (`Awaken.TG.Main.Settings.Controllers.FogController`) only picks HDRP's
    quality level for that grid (Max Quality: the volume's own numbers).

What the tracer does with fog today: the composite paints before HDRP's fog; Fog Adaptation (render event 5,
`d3d11.hlsl` line 118-217) lifts HDRP's fog light by the traced gain; reflected and refracted rays get the analytic height
fog and HDRP's distant-fog colour (`pt_common.hlsli` `fogged`, line 1060-1073; `docs/agents/fog.md`); lights are dimmed by
the height fog on their way (`directional_fog`, `light_incident`, line 917 and 3537), as HDRP does. mirror-2's copy of
HDRP's grid into reflections (`docs/agents/mirror-2.md`) is not in the tree.

## 2. What the game sets

The blended Fog stack at the camera, per scene (harness audits `*/1-game/audit-b.txt`, all "Sky Color" mode, Balance
mode, Max Fog Distance 5000 m):

| Scenes | mean free path | albedo | anisotropy | depth extent | lights in fog | grid at 1924x1084 (budget, ratio) | denoise |
|---|---|---|---|---|---|---|---|
| outdoors by day (candle, campfire, forest, doorway, metal, npc-day, water-*) | 144-198 m | (0.64, 0.79, 0.69) | 0.43-0.61 | 300 m | sun only | 322 x 182 x 82 (0.4, 0.4) | Gaussian |
| vista | 681 m | 1 | 0.60 | 128 m | sun only | 315 x 177 x 51 (0.33, 0.3) | Gaussian |
| night (foggy-backdrop) | **9 m** | (0.24, 0.24, 0.21) | 0.48 | 300 m | moon only | 322 x 182 x 82 | Gaussian |
| fog (Broc Meala) | 70.8 m | 1 | 0 | 379 m | all | 213 x 120 x 113 (0.33, 0.666) | Both |
| tavern (and Ron's, hair scenes) | 39.9 m | (0.58, 0.97, 0.99) | 0 | 74.8 m | all | 196 x 110 x **6** (0.1, 0.1) | Both |
| stairwell / npc-candle | 47-63 m | blue-grey | 0 | 64 m | all | 213 x 120 x 113 | Gaussian |
| crypt (boil-cage, dark-room) | 86.6 m | (0.71, 0.92, 1) | 0 | 88.8 m | all | 228 x 128 x 17 (0.16, 0.2) | Both |
| mineral cave (glow) | 74.1 m | (0.33, 0.43, 0.49) | 0 | 31.7 m | all | 221 x 125 x 41 (0.2, 0.4) | Gaussian |

Tint outdoors (0.906, 1, 0.862); interiors 1 or (0.104, 0.127, 0.17). GI Dimmer 1 except the stairwell (0.566).
Multiple-scattering intensity 1 outdoors, 0 indoors.

Which lights light HDRP's fog (tavern audit `20261008-183634`, every loaded light): 736 of 1178 lights do not
(`affectsVolumetric` false: the candles' `PointLight_Ambience` and `PointLight_Cookies`); of those that do, "Point
Light_Single" x242 at dimmer 3 or 8, `PointLight_Fire` x57 at 3, `PointLight_Main` x13 at 16, `PointLight_Volumetric` (no
surface light, fog only) at 16; almost none cast shadows (fog glows pass through walls). The tracer today leaves fills
and "Volumetric" lights out of its own light but keeps their fog with Fill Light Fog (`SceneLights.cs` line 576-600).

Local fog volumes (effects inventory `20261007-191814`, CampaignMap_HOS): `LocalFog` boxes 80 to 264 m wide, mean free
path 3.35 to 30 m, masked by a 3D noise texture (`3d_fractal_noise_sample01a`, 32^3 Alpha8, 6 mips), tiled 2-7 times,
scrolling 0.02-0.11 a second (moving mist); a blizzard effect's 40 m boxes (mean free path 1-2 m, camera-distance fade
1-90 m); rain's `LocalVolumetricFog_Precip` (10 x 4 x 10 m, a material mask `Mat_CustomVolumetricFog_Precipitation`, a
custom Fog Volume graph). All additive. None in the `fog` save; the `night` save has `LocalFog` boxes 172-390 m away.

What moves the fog: `TimeOfDayPostProcessesController` blends two day-cycle volume profiles (their Fog's density, albedo,
anisotropy, tint), sets the Fog's maximum height by the time of day and its base height by the hero's height (line
479-504); 49 local Volume overrides with Fog (swamp, castle, interiors) blend by the camera's position; Wyrd night has its
own volume controller; weather (`WeatherController`) drives the precipitation and blizzard effects that carry local fog
boxes; local fog masks scroll with time. The tracer reads the camera's blended stack each frame (`HdrpPasses.FillFog`),
so all of it but the local boxes already arrives.

## 3. Design options and the recommendation

The medium the tracer needs, whatever the option: the density at any point (the camera's blended height fog; the local
boxes with their masks, fades and order), the exact transmittance along any segment (the height fog is analytic and
already in the tracer: `height_fog_depth`; the boxes by ratio tracking, unbiased), and the light at a point in the air:
the sun with a traced shadow ray and the fog on its way (`sun_fog`), one lamp chosen among the cell's candidates by
resampled importance (as `direct_light` does, line 3838, with the phase function in place of the cosine), one sky
direction with a traced ray (`sky_radiance`), the Cornette-Shanks phase with the Fog's anisotropy, and the fog-only
lights' fog part. All in a new header (`pt_medium.hlsli`) that the big passes do not include until they use it.

**A. A froxel grid of our own** (HDRP's way, with traced shadows; Unreal's MegaLights and RTX Remix light fog in a grid).
Cost: at the game's outdoor grid size, 4.8 M cells; 3 rays a cell is 14 M rays a frame at full update, about 7 to 14 ms in
the town; a quarter a frame with history, 2 to 3.5 ms; about 115 MB (cells, history, integrated). Picture: converges to a
grid-blurred fog (shafts in 6-10 pixel steps, slices metres deep; the tavern's would need our own, finer grid); stops at
its reach; covers only the camera's view, so mirrors, far fog and refraction need B anyway. Against the rulings: the
grid's cell size and reach are limits that change what the picture converges to.

**B. Per-pixel fog along each ray** (Kulla and Fajardo's free-flight and equiangular sampling; volumetric ReSTIR, Lin,
Wyman and Yuksel 2021, can build on it later). Per pixel, on the segment from the camera to the pixel's own first hit
(the tracer's camera rays) or to Max Fog Distance for the sky: the exact transmittance, and one in-scatter sample (a
distance drawn in proportion to the fog's density and transmittance, analytic for the height fog), lit with sun, lamp and
sky as above. Work in proportion to the fog the ray crosses (Russian roulette on 1 - T: unbiased, nearly free indoors in
thin fog). The fog's own reuse: a per-pixel history reprojected at the fog's mean depth, then an edge-aware spatial
filter (plain compute). Cost: 2.09 M pixels x about 3 shadow rays = 6.3 M rays, about 3 to 6 ms in the town at full rate
(estimate from the direct light's 6.5 ms stage), 1.5 to 3 ms sampling each pixel every other frame; about 42 MB (fog target
and history RGBA16F, a depth channel). Picture: converges to the exact single scattering of the game's fog, at screen
sharpness, at any distance; the same function along mirror, glass, water and later bounce rays, so the mirror shows the
camera's fog. Noise: one sample a pixel needs the reuse; thick fog with shafts is the hard case.

**C. Hybrid (recommended): B as the core, plus a splat of B's camera samples into a grid in HDRP's format** for HDRP's
transparents and water until step C (each pixel's sample at distance t adds to every slice past t of its tile: an
unbiased estimate of the cumulative fog per slice; the optical depth is analytic). No extra rays, about 0.2 ms, 38 MB for
a shared 2D atlas and 38 MB for the D3D11 3D texture (outdoor grid size). It goes away with step C.

Why C: it is the only option whose converged picture is the exact one (the owner's "no limits that change the picture"),
it serves every ray kind from one function (mirrors, "no screen space"), its cost is the same order as A's, and A could
still be offered later as a speed choice (coarser fog, faster) with numbers.

How it meets the rest:
- Composite: the D3D11 composite (event 2) applies `colour x T + fog` to every pixel, HDRP's sky pixels included (HDRP
  drew the sky before our composite). The fog takes the same traced gain as the surfaces, so `traced_gain_at`'s fade
  behind fog and Fog Adaptation lose their reason. Ray Reconstruction sees the fog where it saw HDRP's (before the
  upscaler); NRD never saw fog (bounced light only), the fog's own reuse is its denoiser.
- Reflections and refraction: `fogged` / `fogged_after` keep their signature, the medium estimator inside (pt_specular,
  water, glass). +3 shadow rays per reflected pixel that crosses fog: the biggest unknown cost; behind a compile switch
  until measured. A cheaper choice for rough reflections (a world fog cache, as the radiance cache) changes the picture
  slightly: the owner's call.
- Lights: traced lights with traced shadows; the fill and "Volumetric" lights' fog part as fog-only lights (a flag on
  `TGPT_Light`; never on surfaces, caches or caustics), with traced shadows (HDRP had none for most); which dimmer: Q1.
- Sky: a traced sky ray (an overhang shades the mist), times the Fog's GI Dimmer (the game's setting); indoors none.
- HDRP's fog switched off while tracing (gameplay camera): Harmony prefixes on `RenderOpaqueFog` (return the colour),
  `ClearAndHeightFogVoxelizationPass`, `FogVolumeAndVFXVoxelizationPass`, `GenerateMaxZPass` (return null handles),
  `VolumetricLightingPass` (return our grid, imported), `ScreenSpaceFogMultipleScattering` (skip). Not the Fog volume
  (never `Add<T>(true)`), not light dimmers (DEAD END), not the frame setting Volumetrics (its transparents would lose the
  grid). The game's Fog Quality row greyed while tracing, with the reason.
- What changes in the picture against HDRP's: shafts and shadows in the mist from every traced light; the far haze from
  real sky and sun light instead of the tinted sky colour (Q2); outdoor lamps light the mist if Q1 says so; HDRP's screen-
  space glow around bright things in mist is gone (traced multiple scattering is a later step); the tavern's 6-slice
  smear is gone.

## 4. The first trial, the build order, ABI, files, risks

**Trial (half a day).** Hidden Advanced setting "Traced fog (trial)" (its bit in `cameraPosition.w`, the trials' way, or
in the new fog row below). Plugin: send the Fog's albedo (as HDRP takes it) and anisotropy; prefix on `RenderOpaqueFog`
for the gameplay camera while the trial is on; Fog Adaptation off while it is on. Native: a new compute pass `pt_fog.hlsl`
(inline ray queries, like `pt_di` and `pt_prepare`; visibility rays only, no hit shading to regroup) after the camera rays,
one sample a pixel, height fog only, sun + one traced lamp + sky; a shared RGBA16F target (fog light as the composite
stores light, transmittance in alpha); the D3D11 composite applies it to every pixel; the traced gain unfaded. Its own
constants through the pass's root constants, so `FrameConstants` and the big passes are untouched (no long recompile, no
driver remake of the big passes). No denoiser of its own yet.
- Scene: `fog` (save "PT fog", Broc Meala: mean free path 70.8 m, albedo 1, no local volumes).
  `Run-PtHarness.ps1 -Scenario fog` with the default recipe (16 frames without denoiser first).
- The picture should show: the same grey veil (the thickness is HDRP's own formula); the mist slightly darker under the
  dead tree and the rocks (shaded from the sky); no seam at the horizon or around the sky.
- Deciding numbers: (1) the ROI's median brightness, 16-frame mean of `2-traced-none` against `1-game`, within 20 %;
  (2) far-shape contrast (the ROI's spread) within 20 %; (3) the exposure and gain line within 0.5 EV of the switch off;
  (4) the fog pass's ms on the 10-second line (a new stage, "fog").
- If (1) is off by more than 2x: units or the sky's strength (the composite's stored scale, the Sky Intensity row).

**Build order after it** (one thing at a time, each in game first):
1. `night` (mist 9 m, a torch, the moon): the torch's glow and the man's shadow in the mist; the owner judges Q1 there.
2. The fog's own reuse (temporal at the fog's mean depth, edge-aware spatial), equiangular sampling for lamps with MIS;
   then NRD and RR runs; speed numbers in town and tavern.
3. Fog-only lights (fill and "Volumetric" lights' fog part) and the volumetric dimmers per Q1; `tavern-room`.
4. Local fog volumes: plugin capture of every LocalVolumetricFog (not culled by view or Depth Extent; their own distance
   fade kept), the 3D mask texture shared (first: a self-test that a D3D11 Texture3D created shared opens on D3D12), the
   precipitation graph decoded; `night` (LocalFog boxes in view).
5. Mirrors, glass and water: `fogged` replaced by the medium estimator; `mirror-fog` and `mirror-day` against the game's
   view from the mirror's side.
6. HDRP's fog off: the prefixes above, our grid for HDRP's transparents and water; then delete what was only for HDRP's
   fog (below).
7. Later: fog on bounced-light path segments; multiple scattering in the medium; cheaper choices for the owner.

Deleted in the same jobs (PT-60): Fog Adaptation (row, `TGPT_OPTION_FOG_ADAPTS`, event 5, `TGPT_SetFog`, `TGPT_FOG`,
`fog_automatic`, `FogPass`), `traced_gain_through`'s fog fade and `fog_gain`, `fog_color`, `fog_sky`, the fog sky tiles
in `update_sky`, `fogColor` (keep `fogMip.w`: the grass trial uses it), the Fog GI-dimmer scaling by camera light,
`SceneLights`' `affectsVolumetric` switching (the restore stays).

**ABI** (bump to 0x00012700): `TGPT_Frame` + `float fogMedium[4]` (albedo, anisotropy) and `uint32_t fogFlags, fogSamples,
fogReserved[2]`; `TGPT_Light` 80 -> 96 bytes (`fogDimmer`, a `TGPT_LIGHT_FOG_ONLY` flag); `TGPT_SetFogVolumes(const
TGPT_FogVolume*, count)` (about 128 bytes each: box, albedo, extinction, blend, order, fades, mask texture, tiling,
scroll offset, program); `TGPT_SetFogGrid(ID3D11Resource*, w, h, slices)` for HDRP's grid; 3D shared textures.

**Files**: `native/pathtracing/shaders/pt_fog.hlsl` (new), `pt_medium.hlsli` (new), `d3d11.hlsl` (composite; the grid
writer), `pt_common.hlsli` (`fogged` later), `pt_specular.hlsl` and the water and glass paths (step 5), `src/tracer.cpp`,
`runtime.hpp`, `extension.cpp`, `texture_share.cpp` (3D), `include/tgpt_api.h`, `CMakeLists.txt`,
`pt_shaders.hpp.in`; `src/TGPathTracing/HdrpPasses.cs` (FillFog), `GamePasses.cs` (the prefixes), `NativeTracer.cs`,
`Settings.cs`, `SceneLights.cs`, `ReplacedRows.cs`, new `LocalFogCapture.cs`; `tests/pathtracing_gpu_test.cpp`
(transmittance against exp(-tau); a homogeneous slab under a directional light against the closed form; a blocker's
shadow in the medium; a mirror showing the medium; a fog-only light leaving a wall dark), `tests/source_checks.py`.

**Risks.**
1. Cost: the ray counts are estimates; the trial's stage time decides. The reflections' extra rays (step 5) are the
   biggest unknown.
2. Noise in thick fog with shafts before the reuse exists (the trial is judged without a denoiser, as the rules say).
3. Exposure: the fog's light now takes the traced gain and the meter sees it; read the exposure and gain line.
4. Units: the fog must go through the composite's stored scale and HDRP's exposure exactly as surfaces do.
5. Transparents and water stay in HDRP's fog until step 6 (the trial shows a mismatch on them).
6. D3D11 Texture3D sharing to D3D12 is untested; if it fails, the 32 KB noise mask needs the owner's word (no copies).
7. VFX Graph "volumetric fog" outputs, if the game has any, would vanish when HDRP's fog pass stops: count what
   `FogVolumeAndVFXVoxelizationPass` draws before step 6.
8. `opticalFogTransmittance` (HDRP's lens flares and clouds read it) stays at "no fog" once `RenderOpaqueFog` is skipped.
9. Far fog points kilometres out are lit with no shadow where the traced scene ends (as the open sky would light them).

## 5. Open questions for the owner

1. Which lights light the mist? (a) the game's choice: outdoors the sun and moon only, indoors the lights the artists
   marked, at x3, x8 or x16; or (b) the real one: every traced light lights the mist as it lights surfaces (x1, with its
   shadow), and the fill lights' fog part stays as the artists set it. (b) puts a warm glow around every torch at night
   in the 9 m mist outdoors, where the game has none. My suggestion: (b), shown at `night` side by side with (a).
2. The far haze: HDRP colours it with the sky times an artist tint (slightly green outdoors). Traced, it is the sky and
   sun scattered in the fog. Keep the tint as an extra colour on the fog, or drop it?
3. Speed: if the measured cost is too high, I would offer a coarser fog (a grid, or a sample every few pixels) with its
   numbers. Nothing taken silently.

## Evidence

- HDRP 17 sources fetched to the scratchpad (`raw.githubusercontent.com/Unity-Technologies/Graphics/6000.0/staging/...`):
  `VolumetricLighting.compute`, `VolumeVoxelization.compute`, `VolumetricLightingFiltering.compute`, `VBuffer.hlsl`,
  `AtmosphericScattering.hlsl`, `OpaqueAtmosphericScattering.shader`, `LightEvaluation.hlsl`,
  `HDRenderPipeline.VolumetricLighting.cs`, `Fog.cs`, `LocalVolumetricFog.cs`, `SkyManager.cs`, `ShaderPassVoxelize.hlsl`.
- The game's DLLs (ilspycmd): `Unity.RenderPipelines.HighDefinition.Runtime.dll` (`Fog`, `HDRenderPipeline`,
  `LocalVolumetricFogArtistParameters`: the same formulas as the sources), `TG.Main.dll` (`FogController`,
  `TimeOfDayPostProcessesController`, `WeatherController`).
- Harness: audits `*/1-game/audit-b.txt` (Fog stack, lights), effects inventory `20261007-191814/*/1-game-effects/
  effects.json` (local volumes), stage times `20261011-011046` and `20261011-014541` (`TGPathTracing.Native.log`).
- Earlier reports: `docs/agents/fog.md`, `mirror-2.md` section 1, `lake.md` section 1, `design-particles-fog.md`.
