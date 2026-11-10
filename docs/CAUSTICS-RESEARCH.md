# Caustics for TG Path Tracing: research

2026-09-28. Research only: no code was changed, nothing was built or run. Line numbers are as of this date; the working
tree was being edited while this was written, so trust the function names over the numbers.

Status (same night, round 3): Step 3 (bottles, MNEE) and Step 1 (water, HDRP's pattern as data) are built, both under
the one Caustics row; PROJECT-CONTEXT 9.7 has how. Step 1 as planned, except: HDRP's own caustics go off by a prefix
that zeroes a surface's intensity while HDRP writes its constants (not a postfix on the constant buffer), the mip is
the pixel's footprint plus HDRP's blur, and steep crossings (waterfalls) are passed. Section 7 was not answered; the
defaults taken: all water with caustics on, the game's strength, one row, the light shafts keep HDRP's pattern.

## In short

- Caustics are the bright, moving patterns light makes after water or glass bends it. The net of light on a sunny
  river bed. The bright spot next to a bottle's shadow.
- Path tracing did not lose the game's water caustics. They come from HDRP's water, and HDRP still draws the water,
  caustics included, on top of the traced image.
- The game has them in fewer places than you might think. 156 of its 158 water surfaces switch caustics on. Only 109
  show them when you look at the water from above: cave, dungeon and chapel pools, Sagremor Castle's water and a few
  more. Almost all of the open world's lakes, rivers and the sea use the game's own water shaders, and those never
  show caustics from above. There you only see them when you dive.
- Keeping HDRP's caustics breaks the rule that nothing keeps HDRP's shading while tracing. They also look wrong next
  to traced light. HDRP multiplies all the light on a lake bed by its pattern, so the pattern shimmers in shadow and
  at night, where no sunlight reaches.
- Step 1, my recommendation: the tracer lights the lake bed through the water itself. It uses HDRP's own caustic
  pattern as data (a small texture HDRP already renders every frame). Only sunlight and lamplight that really crosses
  the water gets the pattern. HDRP's own caustics go off while tracing, except in the underwater fog. Cost: under
  0.1 ms by my estimate. It makes no noise, so the denoisers get a clean pattern. It does move, and whether Ray
  Reconstruction follows it without trails needs a look in game. It looks the same above and below the water.
- Step 2, if you want more: the tracer makes the pattern itself from the waves (right sun angle, right depth), and
  adds sunlight reflected off the water onto docks, hulls and bridges, which the game never had. About 0.3 to 0.6 ms.
- Step 3, if you want bottles: the bright spots behind bottles and potions. The tracer would solve the bent light path
  through the glass directly, instead of hoping a random ray finds it. It needs the bottles' smooth surface normals,
  which the traced scene doesn't keep yet. About 0.1 to 0.4 ms.
- Not now: shooting "photons" (rays traced from the light instead of from the camera) and splatting them on screen,
  the way NVIDIA's published glass caustics work. It makes blotchy noise, the kind Ray Reconstruction handles worst,
  and costs more.
- I need your call on a few things (section 7): caustics from above in the open world's lakes, the game's brightness
  or physically correct, cave pools (they will shimmer less), and the underwater light shafts.

## 1. Which caustics players would see

| Kind | Where in FoA | How much it shows | Today, path tracing on | Priority |
|---|---|---|---|---|
| Light through rippling water onto what lies under it | sunny lake, river and sea beds near the shore; cave, dungeon and chapel pools; everything while diving | large areas, always moving; artists tuned it up in pools (section 2.3) | HDRP's pattern, applied by HDRP's water on top of the traced light, only where the game had it (section 2) | 1 |
| Sunlight reflected off water onto things above it | docks, boat hulls, bridge undersides, cliffs and walls by the water | medium; strongest with a low sun (Fresnel: about 2 % of the sun reflects at noon, 13 % at 20 degrees elevation) | missing: water is not in the traced scene and paths never reflect off it | 2 |
| Light focused by bottles, flasks and potions | tavern tables, alchemy stations, market stalls; candles and sun | small (centimetres) but often close to the camera | a tinted, evenly lit shadow (`glass_walk`), no bright focus | 3 |
| Coloured light through windows | stained or tinted glass, where the game has it | medium where it exists | not a caustic (a flat pane doesn't focus). HDRP-transparent glass is invisible to rays (`SceneCapture.ReadMaterial`), so that light lands uncoloured: a separate, small topic | - |
| Glints from polished metal | armour, blades, brass | rare, tiny | a path sends a metal's reflected light on as diffuse light (`hit_material`) | 4 |

Evidence for the glass row: the tavern session's `BepInEx/LogOutput.log` (2026-09-28) names six glass materials the
tracer found: `Mat_Prop_AlchBottle`, `Mat_Prop_BottleBeer_01`, `Mat_Prop_CiderBottle_01`, `Mat_Prop_MeadBottle_01`,
`Mat_Prop_VodkaBottle_01`, `Mat_Prop_WineBottle_Blackcurrant_01`. Evidence that players dive: TG.Main's
`Hero.IsUnderWater`, the oxygen meter in `VHeroController` (it drains while `IsUnderWater`), and
`VCHeroWaterChecker`, which switches the underwater volumes. 150 of the 158 water surfaces have HDRP's underwater
rendering on.

## 2. The game's water caustics and path tracing

### 2.1 How HDRP 17 makes them

Checked in the game's own `Unity.RenderPipelines.HighDefinition.Runtime.dll` (Unity 6000.0.64f1, decompiled with
ilspycmd) and in HDRP's shader sources (Unity Graphics repository, 6000.0 branch; the shaders are not in the DLL).

- Every frame HDRP updates up to 16 water surfaces (`WaterSystem.UpdateWaterSurfaces`), visible or not. For each with
  Caustics on it calls `EvaluateWaterCaustics`: it draws a 256 x 256 grid of quads (`k_WaterCausticsMeshResolution`)
  over one wave band (here almost always the ripples, a 10 m patch) into the surface's own caustic texture
  (R16 float, 256, 512 or 1024 square, with mips).
- The shader, `Hidden/HDRP/WaterCaustics` (`WaterCaustics.shader`), sends light straight down (line 79,
  `incidentDir = (0, -1, 0)`), refracts it by each vertex's wave normal onto a flat plane `virtualPlaneDistance` below
  (8 m in this game), and stores how much each quad shrank or grew: the area ratio, clamped to 0..5 (line 132). This is
  Evan Wallace's WebGL water method, which the shader cites (line 126). The sun's real direction is never used.
- Shading code reads it with `EvaluateSimulationCaustics` (`UnderWaterUtilities.hlsl`, lines 82-148): the texture
  tiles over the water in world space (divided by the tiling factor), is read three ways weighted by the receiving
  surface's normal, fades in over the first `causticsPlaneBlendDistance` of depth, and gives
  `1 + intensity x pattern`.

### 2.2 Where HDRP applies them, and when

Three places. All of them run after the path tracer's composite.

1. Seen from above: the water's own GBuffer pass stores the factor (`ShaderPassWaterGBuffer.hlsl`, lines 69-80 add
   the optional sun shadow), and HDRP's water lighting multiplies the refracted camera colour by it:
   `specularTransmitted = cameraColor * caustics` (`Water.hlsl`, line 782). `WaterSystem.RenderWaterLighting` binds the
   current colour buffer, which already holds the traced light, as that camera colour.
2. Seen from under water: the opaque fog pass switches on `SUPPORT_WATER_CAUSTICS`
   (`SkyManager.RenderOpaqueAtmosphericScattering`) and multiplies everything seen under water by the factor
   (`AtmosphericScattering.hlsl`, lines 360-379).
3. Under water, in the volumetric fog: the sun's light in the fog gets the pattern as a cookie
   (`VolumetricLighting.compute`, lines 228-241).

HDRP's deferred lighting of opaque surfaces, the part the tracer replaces, never applied caustics (`LightLoop.hlsl` has
none). The frame order in `HDRenderPipeline` is: sky, then the `AfterOpaqueAndSky` custom passes (our composite,
`HdrpPasses.cs` line 155), then the transparent prepass with the water GBuffer, volumetric lighting, opaque fog, and
`RenderTransparency` (the transparent SSR step our water reflections hook, then the water lighting).

### 2.3 What the game's files say

I read every `WaterSurface` in the game's 296 scene bundles (method in Appendix A): 158 surfaces in 65 scenes.

| Setting | Values over the 158 surfaces |
|---|---|
| Caustics | on 156, off 2 (Mathir Chapel in the capital, Sewal) |
| Caustics intensity | 1.5 on 85 (the dungeons and pools, raised by the artists), 0.5 on 52 (HDRP's default: the open world), 0.6 on 12, 1.0 on 3, 8.0 on 2 (the endgame), 3.08, 2.57, 0.0 on 2 |
| Tiling factor | 0.5 on 88 (a finer pattern), 1.0 on 61, others 1.5 to 6 |
| Resolution, band | 256 and the ripples band almost everywhere; 512 on the two Afterimages oceans |
| Directional-light shadow on caustics | on 2 (the Afterimages oceans), off 156 |
| Underwater rendering | on 150 |
| Shader | HDRP's default water 111, `TG/Environment/Water/WaterSurface_River` 41, `WaterSurface_Ocean` 4, `WaterSurface_Ocean_Sarras` 2 |

The shader decides whether caustics show from above. HDRP's default water shader and `WaterSurface_Ocean_Sarras` bind
`_WaterCausticsDataBuffer` (the caustic texture) and `_NormalBufferTexture` (which `EvaluateSimulationCaustics` reads)
in their WaterGBuffer passes. The game's `WaterSurface_River` and `WaterSurface_Ocean` bind neither: their compiled
passes never read the caustic texture, and their shadow sampling for caustics was compiled out with it. Their materials
hold only a water-mask noise, rain droplets, cloud noise and a waterfall mask (`Mat_RiverWithFoam_HOS_04`,
`Mat_HOS_Lake_BigLake_01`, `Mat_Ocean`). So:

- 109 surfaces show HDRP's caustics from above: most cave, dungeon and chapel pools, Sagremor Castle's water, one river
  in Cuanacht, the Sarras pools, the endings and prologue lakes (Appendix B).
- The open world's lakes, rivers and sea (Horns of the South, Cuanacht, the Forlorn Lands and the prologue beach: 30
  surfaces) use the game's own shaders, as do 15 rivers and waterfalls in dungeons, endings and ghost realms. None of
  those 45 ever showed caustics from above, with or without mods. HDRP still renders their caustic textures every
  frame, for when the camera is under water.
- There are no hand-made caustic decals: the only "caustic" in the addressables catalog is a spell
  (`ItemTemplate_Magic_Tier5_CausticWrath`).

### 2.4 Verdict: were they lost?

No. Where the game had water caustics, HDRP still puts them on screen with path tracing on, multiplied into the traced
image by passes that run after the composite. Nothing the tracer replaced carried them.

What is wrong with keeping them:

- The rule: they are HDRP shading on top of traced light.
- They multiply all the light on the lake bed: the sky's, the bounced light, the lamps'. The pattern shimmers in the
  shadow of a bridge and at night, where no direct light arrives. Traced shadows are otherwise exact, so this stands
  out more than in the unmodded game.
- They ignore the sun's direction and the real depth (a fixed plane 8 m down), and they come in two view-dependent
  places, so the look changes when the camera crosses the water line.

### 2.5 How to confirm it in game (Step 0 of the plan)

The data above settles it offline. To see it in a capture, the F8 report (`DebugCapture.WaterSurfaces`, which today
prints name, type, geometry and shader) should add, per surface: Caustics on/off, intensity, tiling, blend distance,
resolution, band, underwater on/off, whether its shader shows caustics from above (HDRP's default and
`WaterSurface_Ocean_Sarras`: yes; `WaterSurface_River`, `WaterSurface_Ocean`: no), and whether the camera is under
water (HDRP's underwater surface index, or the game's `Hero.IsUnderWater`). A shot pair with HDRP's caustic
intensity forced to 0 for one frame, with its difference image, would show the pattern in a dungeon pool (Redhat
Cave, Sarras Chapel) and nothing on a Horns of the South lake.

## 3. What the tracer does with focused light today

- Paths only continue diffusely: `shade_path` samples a cosine direction at every vertex (`pt_common.hlsli`, around
  line 1321). A light path through glass or water to a diffuse surface is never followed, so no light is focused.
- Glass (bottles, by material name: `GlassFinish.cs`) lets rays through by chance, as bright as its colour
  (`alpha_opaque`, line 562), and the final shadow rays take its colour crossing by crossing (`transmittance`,
  `glass_walk`, lines 878-922). That is a straight-line tint: the energy is right on average, the focus is missing.
- See-through glass refracts the camera's view (`pt_specular.hlsl`, lines 92-130) with flat triangle normals: the
  traced meshes keep positions, UVs and colours only (`TGPT_MeshDesc`, `tgpt_api.h` line 187), no normals.
- Water is not in the traced scene: a mesh whose materials are all water is dropped (`SceneCapture.ReadMaterial`
  returns Invisible for WaterGBuffer materials, `AddMesh` drops invisible-only meshes, line 98). Lake beds get the
  full sun. `pt_water.hlsl` traces the water's reflections only. HDRP adds its absorption, refraction and caustics on top.
- Denoising paths (`tracer.cpp`, lines 1197-1198 and the `denoiseOwn` block): with Ray Reconstruction and Ray
  Regeneration the direct light reaches `pt_compose` raw; with NRD it first goes through the tracer's own direct-light
  filter (up to 16 frames of history). An animated pattern baked in before that filter would smear; applied in
  `pt_compose` it would not.

## 4. The state of the art

Grouped by approach. Costs are the authors' own figures.

A. Pattern textures. A caustic texture from the waves, projected onto what lies under the water. Wallace's WebGL water
(2011-2016) computes it as HDRP does, from how refracted grid cells shrink or grow; GPU Gems chapter 2 (2004) is the
older game recipe (Guardado and Sanchez-Crespo). Cheap and noise-free, but not tied to the sun's direction, the real
depth or shadows.

B. Light-space caustic maps and ray-guided water caustics. Rasterize the water from the light, refract, find where the
light lands, reconstruct the pattern. NVIDIA's Ray-Guided Water Caustics (Yang and Ouyang, Ray Tracing Gems II,
chapter 30, 2021) traces those rays with DXR and rebuilds the pattern as footprints (Photon Difference Scattering) or
as a mesh (Procedural Caustic Mesh), with cascades for oceans. Measured: a pool, one directional light, 1024 x 1024
map: 0.38 ms (mesh) to 0.55 ms (footprints) on an RTX 3090 at 1080p, 1.0 to 1.7 ms on an RTX 2060; a 240 x 240 m sea
with four cascades: 1.6 ms on an RTX 3090. Limits: first bounce only; caustics are not seen in traced reflections.
Shipped in NVIDIA's RTX branch of Unreal Engine 4 (NvRTX Caustics). Black Myth: Wukong's full ray tracing mode (2024)
lists ray-traced water caustics on river beds; NVIDIA doesn't say which method.

C. Photon splatting. Trace photons from the lights, splat each where it lands. Kim (Ray Tracing Gems, chapter 30,
2019) splats into a screen buffer and blurs it. Adaptive Anisotropic Photon Scattering (Ray Tracing Gems II, chapter
30) sizes each photon's footprint by photon differentials, aims photons with a feedback loop, and handles glass and
metal: 50,000 to 100,000 photons; 0.8 to 1.2 ms on an RTX 3090 and 2.9 to 4.3 ms on an RTX 2060 in its test scenes
("1-2 ms on an enthusiast GPU" in the text). Image Space Photon Mapping (McGuire and Luebke, 2009) is the rasterized
ancestor. Limits: splats are correlated noise, caustics are screen-space only, roughness ignored.

D. Photons gathered with ray tracing hardware. Store photons as small boxes in an acceleration structure and gather
with short rays: Evangelou et al. (JCGT 2021), Kern, Bruell and Grosch's RTPM (JCGT 2023), and ReSTIR FG (Kern et al.,
EGSR 2024), which keeps separate caustic photons and resamples them in reservoirs. Sharp and general, but it is a
whole way of doing bounced light, not an add-on.

E. Path reuse. ReSTIR PT (Lin et al., SIGGRAPH 2022) and RTXDI's ReSTIR PT reuse paths through glossy and mirror-like
vertices with a hybrid shift, but they can only reuse caustic paths the path tracer found, and it almost never finds
one from a small light through glass. ReSTIR BDPT (Hedstrom, Kettunen, Lin, Wyman and Li, ACM TOG 2025) adds light
subpaths and "caustics reservoirs" that build up over frames: at 1920 x 1080 it reaches under a quarter of ReSTIR PT's
error in the same time, about 70 ms a frame. Reservoir Splatting (Liu et al., SIGGRAPH 2025) and ReSTIR PT Enhanced
(Lin et al., 2026) improve reuse, not caustics.

F. Solving the bent path directly. Manifold Next Event Estimation (MNEE; Hanika, Droske and Fascione, EGSR 2015)
connects a surface to a light through refracting interfaces by a few Newton steps from the straight line: exact,
noise-free sharp caustics for small lights. Blender Cycles ships it as "shadow caustics" (3.2, 2022): shadows of
refractive objects only, up to 4 refractions, casters need smooth normals, normal maps ignored. Specular Manifold
Sampling (Zeltner, Georgiev and Jakob, SIGGRAPH 2020), Specular Polynomials (Fan et al., SIGGRAPH 2024: all solutions,
no Newton, GPU-friendly), Bernstein Bounds for Caustics (Fan et al., 2025) and Photon-Driven Manifold Sampling (HPG
2024) extend it, all offline.

G. Regularization. Blur mirror-like interfaces just enough that ordinary sampling finds the light (Kaplanyan and
Dachsbacher, Eurographics 2013; Optimised Path Space Regularisation, Weier et al., EGSR 2021). Unreal's path tracer
has a default-on "approximate caustics" switch for the same reason: less noise, less exact caustics. The result is
soft, still noisy caustics.

H. In a real-time path tracer that shipped. Quake II RTX (NVIDIA, open source, GPL-2.0: read, don't copy) traces each
shadow ray once more against water and glass; where it crosses water it multiplies the light by a caustic factor
worked out from the water's normal at the crossing, plus the water's absorption along the underwater part
(`trace_caustic_ray`, `src/refresh/vkpt/shader/path_tracer_rgen.h`). That is the same place in the pipeline as Step 1
below.

I. Denoisers. The DLSS Ray Reconstruction guide (3.5, "Sampling and Noise") says RR assumes independent samples,
warns against sampling patterns shared across the screen, and suggests a separate pre-denoise pass for correlated
input. Photon splats are exactly such correlated input. A noise-free pattern (A, B with a mesh, F) needs no extra
denoising, only motion RR can follow. For a pattern that moves on a still surface the guide offers an optional
Responsivity Mask (3.4.14, preset F only, which TG Better Upscaling already requests); NVIDIA's own example of where
it may be needed is rain drops in puddles.

## 5. Options for this tracer, compared

GPU costs are for the RTX 5080 at 1924 x 1084. Unless a row says "measured", they are my estimates, scaled from the
published figures above (I assume an RTX 5080 traces roughly 1.5 to 2 times as fast as an RTX 3090) and from this
tracer's own stage times (F8 reports: direct light 3 to 5.5 ms, reflections 4.5 to 11 ms, composite about 0.6 ms).

| Option | What the player gets | GPU | Memory | Denoisers | Work | Verdict |
|---|---|---|---|---|---|---|
| 0. Keep HDRP's (today) | pool caustics from above (109 surfaces), all of them when diving | 0 extra | 0 | HDRP applies it to the colour RR then denoises | none | breaks the rule; shimmers in shadow and at night |
| 1. Traced, HDRP's pattern as data | caustics only on sun and lamp light that crosses a water surface; the same above and below water; every surface with Caustics on | under 0.1 ms | about 1 MB | noise-free; applied after the tracer's own filter, before RR and Ray Regeneration; RR has to follow the pattern's motion (check for trails) | small to medium, about 10 files | do first |
| 2a. Reflected water light, planar | sunlight bouncing off the water onto docks, hulls, bridges, with the same pattern | about 0.1 ms | 0 extra | noise-free | small, after 1 | with or after 1 |
| 2b. Own water caustic map from the waves (B, in compute) | right sun angle and depth, rocks under water shadow it, reflected light computed too | 0.3 to 0.6 ms | 4 to 16 MB | deterministic per frame | medium to large | later, if 1 isn't enough |
| 3. MNEE through glass | bottle and potion caustics in their shadows, from the sun and from candles | 0.1 to 0.4 ms typically, up to about 1 ms in bottle-heavy close-ups | under 1 MB (normals for glass meshes) | deterministic; clamp the brightest lines | medium; mesh ABI gets normals | later, optional |
| 4. Screen-space photon splatting (C) | any glass or metal caustic | 0.5 to 1 ms plus a pre-filter | 20 to 40 MB | correlated splats: needs its own temporal pre-filter before RR; lags on moving water | large | not now |
| 5. Photons into a world-space cache (like `pt_cache`) | static glass caustics, also in reflections | 0.3 to 0.8 ms | 16 to 64 MB | blocky at cell size, slow to react | medium to large | not now |
| 6. Photon gather in hardware, ReSTIR FG (D) | sharp caustics of every kind | not comparable: it replaces the bounced light pipeline | tens of MB | reservoirs | very large | no |
| 7. ReSTIR BDPT (E) | unbiased caustics | 50 to 70 ms at 1080p (measured by its authors) | large | reservoirs | very large | no |
| 8. Regularization (G) | soft, noisy caustics | cheap per ray | 0 | boils | small | no: glass is already the fully blurred limit (straight tinted shadow rays) |

Notes on fit:

- Option 1 fits the tracer's shape: everything direct is next-event estimation (a shadow ray to a chosen light), and
  a water crossing is one more short ray on that path, as in Quake II RTX. The pattern is deterministic, which is what
  RR, NRD and Ray Regeneration want. Its weakness is the pattern itself: HDRP's, for light straight down.
- Option 2b is NVIDIA's water method rebuilt as a compute pass. It needs HDRP's wave data (public
  `WaterSurface.GetDisplacementBuffer()`, or the internal normal buffer by reflection) and HDRP's band layout, which
  ties it to HDRP 17's internals. The game pins its HDRP version, so that is acceptable, but it is the fragile part.
- Option 3 is next-event estimation too: it changes what `transmittance` returns when a shadow ray meets glass. Its
  cost falls only on rays that meet glass. Without smooth normals the game's low-poly bottles would give faceted
  patches, not a focus.
- Options 4 to 8 are what the literature uses for arbitrary caustics. None fits a 1 to 2 ms budget beside ReSTIR GI
  without noise the denoisers would have to fight.

## 6. Recommended plan

### Step 0: evidence in the F8 capture (no player-visible change)

- `src/TGPathTracing/DebugCapture.cs`, `WaterSurfaces()`: the per-surface lines of section 2.5, and a shot pair with
  HDRP's caustics off and on (its difference image), like the existing water-reflections-off shot.
- Also log HDRP's cost: the "Water Surface Caustics" profiler sample (`HDProfileId.WaterSurfaceCaustics`) runs for
  every surface every frame, 11 of them in Horns of the South.

### Step 1: water caustics traced by the tracer, HDRP's pattern as data

Labelled as data in code comments, README and the tooltip: the pattern is HDRP's texture, the light is the tracer's.

What it does: for a surface point that lies under a water surface, the direct light that crosses that surface (the
sun, and lamps above the water) is multiplied by `1 + intensity x pattern` (or an energy-keeping form, question 2),
with the pattern read from the surface's own HDRP caustic texture at that point, as HDRP maps it. Sky light and
bounced light are left alone: caustics from a broad light wash out.

Managed side (`src/TGPathTracing`):
- New `WaterCaustics.cs`: find the water surfaces (`FindObjectsByType<WaterSurface>`, as `DebugCapture` does, on the
  scene scan's schedule). For up to four near the camera with Caustics on: the texture from the public
  `WaterSurface.GetCausticsBuffer(out regionSize)` (its native pointer, re-read when the texture changes), and HDRP's
  mapping: world-to-water matrix (identity for custom meshes, else the inverse of the surface's position and rotation,
  as `WaterSurface.EvaluateWaterSurfaceMatrices`), region size, `1 / causticsTilingFactor`, the game's intensity,
  `causticsPlaneBlendDistance`, the sharpest mip (2, 3, 4 for 256, 512, 1024, as `EvaluateCausticsMaxLOD`).
- Water geometry for the "is this point under water" test, added to the traced scene with a ray mask of its own:
  custom surfaces from `WaterSurface.meshRenderers` (today dropped as invisible, `SceneCapture.AddMesh`), a quad for
  quad surfaces, and for the infinite ocean a large quad at its height, re-centred with the scene origin.
- Turn HDRP's own caustics off while tracing, without touching the game's fog:
  - from above: a Harmony postfix on `WaterSystem.UpdatePerSurfaceConstantBuffer` (private) sets that surface's
    `_CausticsIntensity` to 0 before `UpdateWaterSurface` uploads it. HDRP keeps rendering the texture, since
    `caustics` stays on.
  - from under water: around `SkyManager.RenderOpaqueAtmosphericScattering`, hide the underwater surface's `caustics`
    while HDRP records the pass (its keyword is chosen then), so the opaque fog applies absorption without the pattern.
  - the underwater light shafts (`VolumetricLighting.compute`) keep HDRP's pattern: that is the game's fog.
  - The blunt alternative, `causticsIntensity = 0` on the component, also removes the light shafts' pattern
    (question 4).
- `Settings.cs`, `PathTracingRow.cs`: a "Water Caustics" row (on by default) after Water Reflections, identical in FoA
  Mod Manager. Off means no caustics while tracing; HDRP's stay off too, by the rule.
- `NativeTracer.cs`: the new call below.

Native side:
- `native/pathtracing/include/tgpt_api.h`: `TGPT_SetWaterCaustics` with up to four surfaces (texture, matrix,
  mapping values), a water instance flag, an option bit for the row; ABI bump. `TGPT_Instance.mask` already exists.
- `native/pathtracing/src/tracer.cpp`: copy the caustic textures on D3D11 during the capture event, as the GBuffers
  are copied (four 256 x 256 R16 textures with mips: about 0.7 MB); their SRV and mapping in `FrameConstants`; a
  "water caustics" entry in the stage timings.
- `native/pathtracing/src/scene.cpp`: water instances get mask 0x04 (the instance mask is computed at line 589).
- `native/pathtracing/shaders/pt_common.hlsli`: every existing query must exclude water. Today they use 0xFF (lines
  700, 715, 859, 887, 934 and `trace_from_surface`), `MASK_STILL` 0xFD and `MASK_SWAYS` 0x02; they become 0xFB,
  0xF9 and 0x02. A source check should then ban a literal 0xFF mask in `TraceRayInline`. (A second, water-only
  acceleration structure is the alternative: no mask edits, but a root signature change.) New `water_crossing` (a
  water-only query towards the sun, or straight up where there is no sun, as in caves; steep water triangles such as
  waterfalls skipped, as Quake II RTX does) and `water_caustic` (HDRP's mapping; the mip chosen from the pixel's
  footprint, since compute shaders get no automatic mip, so distant lakes don't sparkle). An early-out below the
  highest nearby water level keeps the query off dry land.
- `native/pathtracing/shaders/pt_compose.hlsl`, line 144: multiply the direct signal (`sun`) by the factor, for
  deferred and forward pixels. Doing it here puts it after the tracer's own direct-light filter (NRD, built-in), so
  nothing smears, and before RR and Ray Regeneration, which get a clean pattern. The direct signal also holds lamps;
  lamps under the water are rare enough to share the sun's factor.
- `tests/pathtracing_gpu_test.cpp`: a pool with a water quad and a known striped texture, sun overhead. Checks: the
  pattern on the floor under the water; none outside it; none in the shadow of a wall across the pool; the floor's
  light the same with the camera under the water; no water ever stops an ordinary ray. Break each piece (the mask, the
  mapping, the shadow gate) and watch its check fail.
- `tests/source_checks.py`: the row in both menus, the mask rule.

Cost (estimate): one short water-only ray per lit pixel below the highest water surface near the camera, three
texture reads, and the copies: under 0.1 ms. Measure it with the new stage timing, not in the harness alone.

In game, check: a Horns of the South lake shore at noon (new caustics from above), the same place while diving (the
same pattern), a bridge's shadow on a river bed (none), a Redhat Cave or Sarras Chapel pool against the game's own look
(question 3), the exposure and gain line (the pattern changes the mean light where it keeps HDRP's brightness), and,
with Ray Reconstruction, trails behind the moving pattern. If they show, RR's Responsivity Mask over lit water beds
is the documented fix (section 4, I); the tracer's own filters never see the pattern.

### Step 2: more water (optional)

- 2a, reflected water light: for a surface a few metres above water, the sun's mirror image in the water plane is one
  more light: find the water along the mirrored sun direction, check the sun from that point, weigh by water's
  Fresnel, pattern it with the same texture (as data again). Two short rays per such pixel, about 0.1 ms. Files: the
  same as Step 1, plus the direct term in `pt_di_spatial.hlsl` or `pt_compose.hlsl`.
- 2b, the tracer's own pattern: a compute pass (new `pt_water_caustics.hlsl`, run after the scene stage) casts a grid
  of sun rays (512 or 1024 square over about 32 m around the camera) through the water plane with the wave normal from
  HDRP's wave data, traces where they land, measures how each grid cell's patch grew or shrank (NVIDIA's caustic mesh
  idea), and writes a light-space caustic map that `water_caustic` reads instead of HDRP's texture. The same rays
  reflected give 2a for real. 0.3 to 0.6 ms, 4 to 16 MB.

### Step 3: bottles and potions (optional)

- Normals: add a normal stream to `TGPT_MeshDesc` (ABI), filled by `SceneCapture` for meshes with glass submeshes;
  `scene.cpp` keeps them packed to 4 bytes a vertex; `MeshRecord`'s `pad` slot can hold their buffer; `fetch_hit`
  returns the smooth normal on glass. `pt_specular`'s see-through glass gains from it as well.
- MNEE in `transmittance` and `transmittance_from_surface` (`pt_common.hlsli`): when a shadow ray to the sun or a
  small lamp met glass, start from the straight line, find the entry and exit points that bend the path onto the
  light (a few Newton steps, each a couple of glass-only rays), and return the glass's colour times the light the
  bent path carries times how strongly the glass focuses it there. Clamp the brightest lines; fall back to today's
  straight tint when it doesn't converge. The sun and lamp shading in `pt_di_spatial.hlsl` and `direct_diffuse` at
  path vertices pick it up without changes.
- Harness: a glass sphere under the sun must put a bright spot at its focus and keep the total light through it.
- Limits, as in Cycles: caustics in the glass's shadow only, one bent path per light sample.

## 7. Open design questions

1. The open world's lakes, rivers and sea never showed caustics from above, though their settings switch them on and
   they show when you dive. With Step 1 they show from above too, wherever the sun reaches the bed. All water, or
   only where the game showed them? (Only-where-the-game-did brings back the change at the water line.)
2. Brightness: HDRP adds `intensity x pattern` on top, and the pattern averages about 1, so the bed gets roughly 1.5
   times its light at the open world's 0.5, 2.5 times in the caves at 1.5, 9 times in the endgame at 8. Keep the
   game's strength, or keep the light's total and only move it around (physical)? I would compare both in game.
3. Caves: HDRP's pattern multiplies all the light on a pool bed, including the fill lights path tracing leaves out.
   Traced caustics come only from real lights (a torch above the pool, sun through an opening). Cave pools will
   shimmer less. Is that right for the look?
4. Under water, HDRP also patterns the light shafts in its fog. I would keep that as part of the game's fog. Agreed?
5. One row "Water Caustics" (and later bottles under the same row as "Caustics"), on by default?
6. Where does this sit against the open items (hair and eyes into the tracer, boiling, performance)? Step 1 is small;
   Steps 2 and 3 are not.

## 8. Sources

HDRP and the game:
- Unity HDRP 17 manual, water caustics: https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/water-caustics-in-the-water-system.html
- HDRP shader sources, Unity Graphics repository, 6000.0 branch (may be slightly newer than the game's 6000.0.64 build;
  the C# side was checked against the game's DLL):
  - https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Water/Shaders/WaterCaustics.shader
  - https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Water/Shaders/UnderWaterUtilities.hlsl
  - https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Water/Shaders/ShaderPassWaterGBuffer.hlsl
  - https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Material/Water/Water.hlsl
  - https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/AtmosphericScattering/AtmosphericScattering.hlsl
  - https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Lighting/VolumetricLighting/VolumetricLighting.compute
- The game's `Unity.RenderPipelines.HighDefinition.Runtime.dll` and `TG.Main.dll` (decompiled with ilspycmd):
  `WaterSystem`, `WaterSurface`, `SkyManager`, `HDRenderPipeline`; `Hero`, `VHeroController`, `VCHeroWaterChecker`.
- The game's scene bundles and `shader-stripping.json` (Appendix A).

Techniques:
- Evan Wallace, Rendering Realtime Caustics in WebGL: https://medium.com/@evanwallace/rendering-realtime-caustics-in-webgl-2a99a29a0b2c
- Juan Guardado, Daniel Sanchez-Crespo, Rendering Water Caustics, GPU Gems, chapter 2 (2004): https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-2-rendering-water-caustics
- Xueqing Yang, Yaobin Ouyang, Real-Time Ray Traced Caustics, Ray Tracing Gems II, chapter 30 (2021):
  https://link.springer.com/chapter/10.1007/978-1-4842-7185-8_30, preprint
  https://developer.download.nvidia.com/ray-tracing-gems/rtg2-chapter30-preprint.pdf
- NVIDIA, Generating Ray-Traced Caustic Effects in Unreal Engine 4, Part 2: https://developer.nvidia.com/blog/generating-ray-traced-caustic-effects-in-unreal-engine-4-part-2/
- NvRTX caustics branch (via the author's demo repository): https://github.com/league1991/RTCaustics
- NVIDIA, Black Myth: Wukong full ray tracing: https://www.nvidia.com/en-us/geforce/news/black-myth-wukong-full-ray-tracing-dlss-3/
- Hyuk Kim, Caustics Using Screen-Space Photon Mapping, Ray Tracing Gems, chapter 30 (2019): https://link.springer.com/chapter/10.1007/978-1-4842-4427-2_30
- Evangelou, Papaioannou, Vardis, Vasilakis, Fast Radius Search Exploiting Ray Tracing Frameworks, JCGT 10(1), 2021: https://jcgt.org/published/0010/01/02/
- Kern, Bruell, Grosch, Accelerating Photon Mapping for Hardware-based Ray Tracing, JCGT 12(1), 2023: https://github.com/SirKero/RTProgressivePhotonMapper
- Kern, Bruell, Grosch, ReSTIR FG: Real-Time Reservoir Resampled Photon Final Gathering, EGSR 2024: https://github.com/TU-Clausthal-Rendering/ReSTIR-FG
- Lin et al., Generalized Resampled Importance Sampling: Foundations of ReSTIR, SIGGRAPH 2022; RTXDI's ReSTIR PT notes: https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirPT.md
- Hedstrom, Kettunen, Lin, Wyman, Li, ReSTIR BDPT: Bidirectional ReSTIR Path Tracing with Caustics, ACM TOG 2025: https://dqlin.xyz/pubs/2025-tog-BDPT/
- Liu et al., Reservoir Splatting for Temporal Path Resampling and Motion Blur, SIGGRAPH 2025: https://research.nvidia.com/labs/rtr/publication/liu2025splatting/
- Lin et al., ReSTIR PT Enhanced: https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/
- Hanika, Droske, Fascione, Manifold Next Event Estimation, EGSR 2015: https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.12681
- Blender Cycles shadow caustics (MNEE), release notes 3.2: https://developer.blender.org/docs/release_notes/3.2/cycles/
- Zeltner, Georgiev, Jakob, Specular Manifold Sampling, SIGGRAPH 2020: https://dl.acm.org/doi/abs/10.1145/3386569.3392408
- Fan et al., Specular Polynomials, SIGGRAPH 2024: https://zhiminfan.work/specPoly.html
- Fan et al., Bernstein Bounds for Caustics, ACM TOG (SIGGRAPH 2025): https://arxiv.org/abs/2504.19163
- Photon-Driven Manifold Sampling, HPG 2024: https://dl.acm.org/doi/10.1145/3675375
- Kaplanyan, Dachsbacher, Path Space Regularization for Holistic and Robust Light Transport, Eurographics 2013: https://onlinelibrary.wiley.com/doi/abs/10.1111/cgf.12026
- Weier et al., Optimised Path Space Regularisation, EGSR 2021: https://onlinelibrary.wiley.com/doi/10.1111/cgf.14347
- Unreal Engine path tracer (approximate caustics): https://dev.epicgames.com/documentation/unreal-engine/path-tracer-in-unreal-engine?lang=en-US
- Quake II RTX source (GPL-2.0, read only): https://github.com/NVIDIA/Q2RTX
- DLSS Ray Reconstruction Integration Guide, section 3.5 (in `DLSS-SDK-310.9.1/doc`).

## Appendix A: how the game data was read

- Tools (session scratch only, nothing added to the repository): UnityPy 1.25.3 installed into a scratch folder, with
  its fallback Unity version set to 6000.0.64f1 (the bundles carry none).
- Every `StreamingAssets/aa/StandaloneWindows64/scenes_*.bundle` (296) was opened and every MonoBehaviour read by its
  type tree; those with a `causticsIntensity` field are `WaterSurface` components.
- `customMaterial` was followed to its material and shader through the bundles' external references (a map of each
  bundle's serialized files, read from the bundles' headers). An empty `customMaterial` means HDRP's default water
  material (`WaterSystem`: `customMaterial != null ? customMaterial : m_InternalWaterMaterial`), whose shader
  `Shader Graphs/Water` sits in `globalgamemanagers.assets`.
- For each water shader, the WaterGBuffer, WaterGBufferTessellation and LowRes passes' name tables (`m_NameIndices`:
  every constant, texture and buffer the pass's programs bind) were compared. HDRP's default and
  `WaterSurface_Ocean_Sarras` list `_WaterCausticsDataBuffer`, `_NormalBufferTexture` and HDRP's shadow atlas;
  `WaterSurface_River` and `WaterSurface_Ocean` list none of them (they keep only the caustic constants, part of a
  constant buffer bound whole).
- `shader-stripping.json` in the game folder confirms `Hidden/HDRP/WaterCaustics` ships (2 variants), and lists the
  passes kept for HDRP's default water shader and the game's River and Ocean shaders.

## Appendix B: water surfaces by region

Counts of surfaces by shader, and whether HDRP's caustics show from above (Caustics on, intensity above 0 and a shader
that reads the texture). Intensities in brackets.

| Region | Shows from above | Not from above |
|---|---|---|
| Horns of the South | - | 10 River (0.5), 1 Ocean (0.5) |
| Cuanacht | 1 HDRP default river (0.5) | 10 River (0.5), 1 Ocean (0.5) |
| Forlorn Lands | 1 HDRP default (1.5) | 6 River (0.5, 0.6), 1 Ocean (0.5), 1 HDRP default with Caustics off (Mathir Chapel) |
| Sarras map | 3 HDRP default (1.5, 0.5) | - |
| Dungeons | 81 HDRP default (mostly 1.5; 8.0 in the endgame), 2 Ocean_Sarras (0.5) | 9 River (1.5, 0.6), 3 HDRP default (intensity 0 or Caustics off) |
| Endings, ghost realms, prologue, test arena | 21 HDRP default (0.5 to 1.5) | 6 River (0.6), 1 Ocean (prologue beach, 0.5) |
