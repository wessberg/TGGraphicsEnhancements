# The tracer draws the water (water agent, 2026-10-07)

The owner, asked whether the tracer should take over the water's own look: "of course. Full Path Tracing means full path
tracing." (AGENTS.md "No HDRP shading while tracing"). And, during this job: "2 lamps per pixel in water caustics, also
drop that restriction" (every lamp's light off the water counts). Copy `wt/water`, base `wt/base24`. Nothing ran in game,
nothing was deployed or committed.

## In short

- HDRP lights its water in one pass: its lighting writes the lit water into a texture of its own, then its fog goes over
  that into the picture. The plugin now steps in between the two (a Harmony transpiler on that pass) and swaps HDRP's lit
  water for the tracer's. HDRP's fog still goes over it, as over its own water. Path tracing off: nothing is touched.
- The tracer's water, per pixel: its reflection (traced, as before) by the water's Fresnel term; what lies under it (the
  camera's ray bent into the water, traced to the bed, dimmed by HDRP's absorption over that way); its body color and foam
  (HDRP's own numbers) lit by the traced light on the surface; the sun's and lamps' highlights, traced with shadow rays.
- HDRP's water is the data: its shape (depth and normals), its scattering color, foam, absorption distance and refraction
  color. The light is the tracer's.
- Lamp light off water: every lamp now counts. The two strongest a pixel are traced as before; one more, picked in
  proportion to its light among all the others, stands in for them, divided by its chance. Right on average, a little noise.
- Offline: every new check passes, and fails when its piece is broken on purpose (section 4); the older water checks
  still pass; source checks and the plugin build pass. Not seen in game.
- Cost: the water pass about doubles (0.41 to 0.81 ms over the test's lake at 1280x720); section 6.

## 1. How HDRP draws its water today (HDRP 17, the game's build)

Read from Unity's Graphics repo (6000.0 branch: `WaterLighting.compute`, `Water.hlsl`, `WaterUtilities.hlsl`,
`UnderWaterUtilities.hlsl`, `HDRenderPipeline.WaterSystem.cs`) and the game's own `Unity.RenderPipelines.HighDefinition.Runtime.dll`
(ilspycmd, scratch only; the render function's IL checked).

- The water's GBuffer (its shader graph, `TG/Environment/Water/WaterSurface_Ocean` and `_River` in this game): 0 the
  scattering color (the water's body color, worked out from `scatteringColor`, ambient/height/displacement scattering and
  (1 - the absorption over the depth)), 1 the normal (octahedral 8:8:8) and roughness, 2 the low frequency normal and
  foam x 2, 3 tip thickness, the caustics factor, front face and the surface's index (0 to 15). Stencil bit 32
  (`StencilUsage.WaterSurface`).
- `WaterSystem.RenderWaterLighting`, one render-graph pass "Water Deferred Lighting": five lighting kernels write
  (diffuse + specular) x exposure into a transient texture; then `PhysicallyBasedSkyRenderer.SetDefaultGlobalSkyData` (only
  without a physically based sky; the game has one); then the fog kernel: picture = lit x (1 - fog opacity) + fog.
- What the lighting does: specular reflected = SSR texture x FGD (the tracer's reflections since R3); specular transmitted =
  the color buffer at a screen-space refracted position x caustics x (1 - foam) x (1 - FGD), absorption exp(-2 x
  extinction x distance) on it; extinction = -ln(0.02) / absorptionDistance x max(1 - refractionColor, 0.01); diffuse =
  ((1 + phase) x scattering color + foam color x foam) x each light's wrapped diffuse, plus the ambient probe x indirect
  multiplier (0 while tracing); plus each light's GGX highlight.
- While tracing before this job: the reflection traced, everything else HDRP's, lit by the lights the tracer keeps, with no
  ambient (the multiplier is 0) and the fill lights darkened: a cave's lake was a dark slab (PT-48).
- Underwater (camera below a surface): HDRP draws the water's underside and its own underwater fog. Kept.

## 2. The design

The tracer shades a water pixel itself; HDRP's water is its data.

1. Shape. HDRP's displaced, animated surface is already on screen: its depth and normal buffer at the transparent SSR step
   (render event 4) give each water pixel's position and normal for the frame. No traced water geometry is needed for what
   the camera sees (the traced water quads stay for caustics and the light reflected off water). Two waves' worth of
   self-reflection (a wave reflecting another) is not traced: the traced scene has no displaced water.
2. Reflection. As before (pt_water: one GGX sample a pixel around HDRP's normal, 12 frames, 5x5 edge-stopped filter).
3. Under the surface (new, `pt_water_under.hlsl`): the camera's ray refracted by HDRP's normal (Snell, index 1.3333,
   HDRP's WATER_IOR), traced into the scene to what it meets (the bed, a fish, a sunken wall), shaded as a camera pixel's
   bounce would be (direct light with shadow rays, the Bounces setting's bounced light), its color HDRP's GBuffer's where
   the bed is on screen (the water is drawn after it), its direct light taking HDRP's caustic pattern where that light
   crossed the water (`water_caustic_at`, the Caustics row). The way's length under water goes along.
4. The light on the surface (new, same pass): what a white diffuse surface at the water's place takes (direct light with
   shadow rays, bounced light). HDRP lights its scattering color and its foam with exactly that kind of light.
5. Highlights (new, pt_water while the tracer draws the water): the sun's and the lamps' light on the water's GGX lobe
   (`direct_light` with no diffuse: the sun from a point of its disc, a lamp of the cell picked in proportion to its light,
   each with a shadow ray), put into the reflection divided by the Fresnel term the shade multiplies it by.
6. Put together (`d3d11.hlsl` TGPT_WATER_SHADE, render event 6, inside HDRP's pass between its lighting and its fog), as
   HDRP's water composes them: F x reflection + (1 - F) x (1 - foam) x under x exp(-2 x extinction x way) + (scattering color
   + foam color x foam) x light on the surface. F is Schlick with HDRP's f0 0.0204 at HDRP's normal. The extinction and foam
   color per surface index come from the plugin (`WaterSurface.extinction` as HDRP computes it, `foamColor`). Applied one
   frame late, reprojected through the surface (the rest of the tracer works the same way). A pixel the traced frame did not
   cover (uncovered since), or seen from under the water, keeps HDRP's lit water.
7. Foam: HDRP's own amount per pixel (its simulation, foam mask and decals, in GBuffer 2), lit by the traced light. Not
   skipped.

What HDRP still draws over or instead of it:
- Its fog over the water (the game's fog; the ruling keeps it). Fog Adaptation does not reach the water (HDRP draws it after
  that pass), as before.
- Everything when the camera is under the water: the water's underside and HDRP's underwater fog (its color is the
  scattering color times HDRP's ambient probe and sun, its own light model). What that misses: the traced light under water.
- Transparent things HDRP draws after its water (particles, glass in front of the water): untouched.

What it leaves out (written down, not built):
- HDRP's phase term: the direct light's extra scattering through wave tips (`(1 + phase) x scattering color`,
  `directLightTipScattering`, `directLightBodyScattering`). The traced light lights the body color as plain diffuse. On a
  sunlit sea the tips may look less bright green than the game's.
- Light scattered inside the water along the refracted way (a volume): HDRP has none either above water; its scattering
  color stands for it, lit at the surface.
- The lamps' and the sun's light refracted along a bent path onto the bed: shadow rays from the bed go straight up through
  the water (the traced scene has no water for them), and HDRP's caustic pattern moves the light about where it crossed.
- A refracted ray that leaves the water again through a wave, and the water's own self-reflection.

## 3. What is built

All of section 2, foam included.

- Plugin (`src/TGPathTracing/WaterShade.cs`, new): a Harmony transpiler on HDRP's water lighting render function
  (`WaterSystem.<>c.<RenderWaterLighting>b__78_0` in the game's build: found by its parameters, not its name) puts a call to
  `WaterShade.Shade(data, ctx)` right after `PhysicallyBasedSkyRenderer.SetDefaultGlobalSkyData`, moving the branch label
  that jumps over that call onto it, so it runs on both ways, just before the fog kernel. Shade, only for a frame whose
  render event 4 handed its water over (`HdrpPasses.Water` arms it): HDRP's lit water texture, water GBuffer 0 to 3 and
  depth-stencil to `TGPT_SetWaterShade`, with each surface's extinction (computed as HDRP's internal
  `WaterSurface.extinction`) and foam color by its index (`WaterSurface.surfaceIndex`), then render event 6. Any failure:
  one warning and HDRP's lighting for the rest of the session. The 10 s line's water part ends with "water shaded N".
- Native: `pt_water_under.hlsl` (new pass, compiled for both shader sets, dispatched right after pt_water in its
  submission); `pt_water.hlsl` (the highlights, flag bit 2 of pass.b.w, set while event 6 ran in the last two frames);
  `pt_common.hlsli` (`water_caustic_at`, moved from pt_compose so the bed seen through the water takes it too;
  `g_firstDirect`, a factor on shade_path's first vertex's direct light); `d3d11.hlsl` (TGPT_WATER's fill split into
  helpers, unchanged in what it does; TGPT_WATER_SHADE); `tracer.cpp` (the pass, its packed output and history per parity,
  the record of what event 4 applied for event 6 to use, `water_shade_event`); `extension.cpp`, `runtime.hpp`, `tgpt_api.h`.
- Lamps off water (`pt_compose.hlsl` water_reflected_lamps): `water_lamp_weight` (the unshadowed estimate),
  `water_lamp_light` (the old per-lamp body, unchanged); the two strongest of every slot's cell traced; one more chosen
  from the rest by a second walk over the cell, in proportion to its weight, divided by its chance; no cap on the cell's
  lamps (it read at most 64).

## 4. Offline checks

`tests/pathtracing_gpu_test.cpp`, at the end of the water reflections block: a still lake (a quad 40 m square, 30 cm over a
green bed of the same size) under the open sky, nothing else traced, the sun off; HDRP's water GBuffer as constant
textures (normal up, roughness 0, no foam, front face, surface 0); HDRP's lit water a magenta marker. 24 frames from a fresh
start; on the last one the shade runs four times on the same traced frame: absorption 0, absorption (0.9, 0.35, 0.15) per
metre, absorption 1e4 (only the reflection left), and that with a scattering color (0.02, 0.1, 0.08). Image
`35b-water-shaded.png`.

| Check | Result (run 2) | Mutation that breaks it |
|---|---|---|
| every water pixel shaded, nothing else touched | 547760 of 547760; 373840 dry pixels all left | (any failure to shade) |
| the reflection by the Fresnel term (the shaded water with all absorbed / the SSR fill's reflection = Schlick at the view's angle, within 1.5 %); grazing pixels over 0.3 | 547760 of 547760; 91546 of 91546 | constant F: 0 of 547760, 0 of 91546 (mBC) |
| what lies under, dimmed by exp(-2 x extinction x the refracted way), each channel within 3 % | 547142 of 547568; matching the straight way instead: 0 | no refraction: 0 of 539230, straight way 465856 (mA); absorption once: 0 (mBC) |
| a lamp 0.7 m over the water lights the bed seen through it where the refracted rays meet it, (1 - F) x albedo x E / pi, within 10 % (sky off) | 29302 of 29312, mean 0.994 | no refraction: 1875, mean 1.218 (mA) |
| HDRP's scattering color lit by the light on the surface (the lamp's I / d^2 x cos / pi), within 10 % | 27969 of 27989, mean 0.999 | |
| the sun's highlight where the water mirrors it (a camera looking at the sun's mirror point): over 10 x the sky's reflection there, under 1 % of it a quarter screen away | 3.54 against 0.0026; 0.000035 elsewhere | no traced highlights: 0 in the middle (mDE) |
| three spots over the pool: the wall's reflected lamplight is the sum of each alone (per pixel within 3 %) | 0.99999, 291686 of 291686 | the rest not sampled: 0.836, 100163 of 291686 (mDE) |
| four spots: the sum on average, within 5 % per pixel (the two strongest alone: 0.776) | 0.99999, 291686 of 291686 | the rest not sampled: 0.769, 106988 of 291686 (mDE); its sample not divided by its chance: 0.908, 160080 of 291686 (three still 0.99999: the third is chosen every time) (mF) |

The older water checks (reflections, caustics, sunlight and lamplight off water onto a wall) all still pass, and
`tgpt_gpu_test` exits 0. `python tests/source_checks.py`: 86 tests OK. `dotnet build src/TGPathTracing -c Release` (to a
scratch folder): builds.

## 5. Files, ABI, bits

- New: `native/pathtracing/shaders/pt_water_under.hlsl`, `src/TGPathTracing/WaterShade.cs`, `docs/agents/water.md`.
- Changed: `native/pathtracing/shaders/pt_water.hlsl`, `pt_common.hlsli`, `pt_compose.hlsl`, `d3d11.hlsl`,
  `pt_shaders.hpp.in`; `native/pathtracing/src/tracer.cpp`, `extension.cpp`, `runtime.hpp`;
  `native/pathtracing/include/tgpt_api.h`; `CMakeLists.txt` (the pass in the shader list, its dependency on pt_water.hlsl);
  `src/TGPathTracing/HdrpPasses.cs`, `NativeTracer.cs`; `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py` (86
  tests: the pinned compose and shade_path lines updated, asserts for the shade, the pass, the highlights and the lamps);
  `CHANGELOG.md`.
- ABI: adds `TGPT_SetWaterShade`, `TGPT_WaterShade` (568 B, managed `NativeWaterShade`, checked in `Validate`),
  `TGPT_MAX_WATER_SURFACES` 16 and render event `TGPT_EVENT_WATER_SHADE` 6. The ABI number is left at 0x00011B00 here: the
  merge sets the next one. TGPT_Frame, TGPT_Status, FrameConstants unchanged.
- No option bit, no material bit, no instance bit. pt_water's pass flags gain bit 2 (internal). The Water Reflections row
  (`Settings.WaterReflections`) now switches the whole traced water: off, HDRP's water as before (its name and tooltip are
  the owner's call).
- Merge notes: `pt_compose.hlsl`'s `water_caustic` is now a wrapper over `pt_common.hlsli`'s `water_caustic_at` (the body
  moved); the two compose sums call `water_reflected_lamps(s, p)`; shade_path's direct line gained `* (bounce == 0 ?
  g_firstDirect : 1.0)`; `d3d11.hlsl`'s TGPT_WATER block is now `TGPT_WATER || TGPT_WATER_SHADE` with helpers.

## 6. GPU cost

- The water's passes (pt_water and pt_water_under, one submission: the native log's "water reflections" stage), over the
  test's lake at 1280x720, the water on 59 % of the picture, sky light only, Rays Per Pixel 1, Bounces 2: 0.81 ms with the
  water's own light, 0.41 ms without it (the mCost build, its dispatch left out). So the water's own light about doubles
  the water pass: roughly +0.7 ms a picture-full of water per million pixels. At the game's render size (1924x1084, 2.3 times
  the pixels) and with real scenes' lights and geometry expect +1 to 2 ms where water fills the view, nothing where there is
  none. The game's earlier water pass: 0.28 to 0.33 ms on the beach, 0.86 to 0.95 ms in the sea cave (caustics.md).
- The highlights add a direct_light call (two shadow rays at most) a water pixel, only while event 6 runs.
- The shade itself (D3D11, one compute pass over the picture, 34 texel reads a water pixel) was not timed (D3D11 passes are
  not in the stage times); it is about as heavy as the existing SSR fill, which it repeats.
- Lamplight off water with every lamp counting: a second walk over each slot's cell and one more lamp's three rays a pixel
  above the water. The test's 32 lamps over the pool, all reaching the wall (1280x720, 200 frames): the composite took 0.15
  to 1.31 ms with it and 0.16 to 0.37 ms without over seven runs, while other agents' tests shared the GPU: too noisy to
  say more than "under about a millisecond there". Read the native log's "composite" stage in `glow-lake` (0.78 ms in `glow`
  before the lamp reflections).

## 7. What the next runs should show

`-Scenario water-day,water-torch,glow-lake -Recipe views`, and the native log:

- Once: "Water shading: the tracer lights HDRP's water (its lit water WxH format N, water GBuffer formats ...)". No
  "Water shading failed" and no "The water keeps HDRP's lighting" in LogOutput. The 10 s line's water part: "water shaded N"
  growing with the frames.
- The stages line: "water reflections" now holds the water's own light and the highlights too: about twice to three times
  its old 0.28 to 0.95 ms (section 6).
- `water-day` (the beach): the sea's body color is HDRP's scattering color lit by the traced sun and sky (it had no sky light
  while tracing): closer to the game's green-blue than the greyer traced sea of round 3; the sand under the shallow water
  near the shore seen through it, bent and tinted; the sun's glitter traced (it was HDRP's); foam where the game has it.
- `water-torch` (the sea cave): the water lit from the cave mouth; the rock under shallow water visible; reflections as
  before.
- `glow-lake`: the lake no longer one flat slab where traced light reaches it: the lantern's highlight on it, the bed at its
  shallow edges. Still dark overall: the cave's fill lights stay left out (the ruling), so the lake's body color has little
  light to show. Compare the Direct and Indirect views over the lake.
- Path tracing off (Insert): the game's water exactly (the hook does nothing unarmed).

## 8. Risks and what is left

- The hook is tied to the IL of HDRP 17's water lighting lambda (checked against the game's DLL). Another HDRP build
  without that call before the fog: a warning, and the water keeps HDRP's lighting with the traced reflections.
- One frame late, accumulated over 12 frames: the light under and on fast waves trails a little; HDRP's normal of this
  frame drives the Fresnel term, so the waves' shape is current. A lamp's glint whose reflection ray met the sky is
  reprojected as if infinitely far: near a lamp, moving, it may trail.
- Pixels the traced frame did not cover (uncovered since) keep HDRP's lit water, which now has the highlights twice (its
  own and the traced ones in the reflection it takes): a few pixels at moving edges.
- The shore: where the traced ground sits a few centimetres above HDRP's, the refracted ray starts inside it; a back face
  within 10 cm is taken as the bed at the surface. A dark fringe may remain where that misses.
- HDRP's phase term (tip and body scattering of direct light) is left out: sunlit wave tips may look less bright.
- HDRP's water decals and its foam generators are in its GBuffer and so in ours; the foam's own texture detail is HDRP's.
- Cost (section 6): the water's own light roughly doubles the water pass; lamp light off water with every lamp counting
  costs about a millisecond more where dozens of lamps stand over a pool (the owner's rule: right first, faster after).
- Underwater: HDRP's look stays (its underwater fog, the underside). The tracer could take that over next (rays from under
  the surface, HDRP's underwater fog as absorption and scattering).
