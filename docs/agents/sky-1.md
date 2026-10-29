# sky-1: the sky into the tracer (PT-112, the sky) - research and design, no code

State: DONE (2026-10-11), research and design only. Nothing in the repository changed except this file. Scratch files:
`%TEMP%\tgb\sky1\` (the game's types decompiled in `dec\`, HDRP 17 sources in `hdrp\`, the per-scene table
`sky_table.txt`, the game's sky shader disassembled in `skyprog\`).

## Summary (plain words)

- **What the sky is.** Outdoors the game uses HDRP's "Physically Based Sky": a real atmosphere model (air, haze, ozone)
  lit by two sky-only lights, the Sun and the Moon. On top of it the game draws its own space layer with a Shader Graph of
  its own (`TG/Skybox/PhysicallyBasedSky_Wyrdness`): procedural stars, a nebula, a night picture, a day colour ramp and the
  Wyrdness night. Clouds are HDRP's "Cloud Layer": a flat painted cloud picture 2 km up, lit by the sun, moved by the
  wind, thickened by rain. No volumetric clouds. Interiors use a plain HDRI picture (the tracer gives rays no sky there).
  The game's script `DayNightSystem` changes all of this every frame from the time of day and the rain.
- **What HDRP does for it each frame.** As the sun moves every frame, HDRP re-renders its sky into a 256-pixel cube map,
  blurs it for reflections and turns it into an ambient light. It also draws the sky on screen (since step A on every
  pixel, because HDRP's depth is empty when the sky is drawn) and draws the clouds on top. And its fog pass adds the
  atmosphere's haze to distant objects. The tracer copies HDRP's cube map on the game's D3D11 device every 30 frames,
  which can make that device wait for the tracer. None of this has been timed on the card.
- **The design.** The tracer computes the same sky itself, on its own device: HDRP's atmosphere model with the game's
  settings, the game's star layer (its shader translated, as was done for the decals), HDRP's cloud layer with the game's
  cloud picture, and the sun and moon discs. Tables once per atmosphere change, one sky picture per frame for all rays,
  the exact sky per pixel where the camera sees it. Then HDRP's sky update, sky drawing and cloud drawing are switched
  off while tracing (a sky "update mode" setting and two small patches). My estimate: HDRP saves 0.5 to 2 ms a frame, the
  tracer adds about 0.2 to 0.4 ms. Not measured.
- **One thing to fix now (the fog).** Skipping HDRP's fog pass (step B, now the rule) also removed the atmosphere's haze
  that HDRP adds to far hills and trees outdoors. The traced fog does not add it. Far land in the open world will look
  clearer than in the game until the tracer adds it.
- **Three questions for the owner** (end of section 3): Q1 the sun that lights the world is held at least 30 degrees up
  by the game while the visible sun sets (light from the visible sun, or keep the game's?); Q2 the haze adds light but
  the game set it to dim nothing (keep, or let haze also hide far things?); Q3 the game's clouds cast no shadows (add
  moving cloud shadows?).
- **Not proven.** HDRP's sky cost, and that its sky update really runs every frame in play: one game run answers both
  (section "The game run").

## 1. Which sky the game uses, scene by scene

From the harness audits (`artifacts/harness/<run>/<scene>/1-game/audit-b.txt`, newest run per scene; the table:
`%TEMP%\tgb\sky1\sky_table.txt`). HDRP's sky type ids: 1 HDRI, 3 Gradient, 4 Physically Based; cloud type 1 Cloud Layer.

| Scenes | Sky | Clouds |
|---|---|---|
| Open world by day and night: campfire, candle, caustics-glass, doorway, forest, metal, night, npc-day, water-day, water-torch; vista (Cuanacht) | Physically Based Sky, `renderingMode = Material`, material `Mat_PhysicallyBasedSky_Wyrdness_CM_HOS_B` (Cuanacht: `..._CM_Cuanacht_AJ`), shader `TG/Skybox/PhysicallyBasedSky_Wyrdness`, keyword `LOCAL_SKY` | Cloud Layer, Double, 2000 m, `DefaultCloudMap`, 1024, upper hemisphere, procedural wind, lit, no shadows |
| Dungeon (npc-candle, stairwell: Sagremor castle) | Physically Based Sky, material `HDRI SKY STARS` with HDRP's own `Shader Graphs/PhysicallyBasedSky` | Cloud Layer, opacity 0 or 1 |
| Interiors (tavern-room, hair-*, glass-jars, door-strap) | HDRI `Tex_HDRI_Morning_Foggy_a`, exposure 14.4 | none |
| boil-cage, dark-room, refract-jar | HDRI with no cube map (black) | none |
| fog (Broc Meala) | HDRI `Tex_HDRI_CloudedWeird_01_b`, exposure 3.2 | none |

The open world's atmosphere (volume `DayNightSystem_HOS_UltraPlus/Volumes/Volume_DayNightSystem_Global`): model
`EarthAdvanced` (air at Earth's values), aerosol density 0.5 at the zenith (scale height 1200 m: extinction 5.8e-4 per
metre at sea level, about 58 times HDRP's default haze), aerosol anisotropy 0.47, ground tint 0.074, horizon and zenith
tints white, colour saturation 1, `alphaMultiplier = 0` (the atmosphere dims nothing behind it; it only adds light),
`atmosphericScattering = true` (haze on far objects), update mode OnChanged, planet centre (0, -6378.1 km, 0) (sea level
is world y = 0), rendering space World. Volumetric Clouds: component and frame setting off. Space texture
`Tex_belfast_sunset_puresky` x 18924.9 is set on the volume but unused: in Material mode HDRP leaves space to the material.

**The game's sky shader** (bundle `34fb8d82e3bff3665cb9a1f17452d8c1.bundle`, found by `tools/research/bundlescan.py`; its
program unpacked with `unity_shader_blob.py` and disassembled with `dxbc_split.py`, 5 programs: `skyprog\*.asm`): a Shader
Graph of HDRP's "Physically Based Sky" type, passes "PBR Sky cubemap" and "PBR Sky", LOCAL_SKY on and off. Its
properties: `_SkyTint, _SkyOffset, _DayZenithColor, _DayMidColor, _DayHorizonColor, _SkyNightMap,
_SkyNightEmissionMultiplier, _NightSkyTint, _SkyWyrdness, _StarDensity, _StarSize, _StarSharpness, _StarTint,
_StarColorVariation, _StarColorBoost, _NoiseScale, _FBMOctaves, _NebulaColorA, _NebulaColorB, _NebulaIntensity,
_NebulaDensity, _Planet_Rotation, _Ground_Color_Texture, _Ground_Emission_Texture, _Ground_Emission_Multiplier`, plus
`_SkyBlend, _SkyEmissionMultiplier, _SkyRotation` set by script. The LOCAL_SKY pixel program (`chunk0_ps_44198.asm`,
1565 lines) is HDRP's template with the game's space in the middle: a loop over the celestial bodies (the sun and moon
discs, the moon's surface from the cookie atlas), about 780 lines of procedural star and nebula arithmetic (122
frac/sincos), the ground (two cube maps, the ground irradiance table), the night cube map, then HDRP's atmosphere (a loop
over the celestial lights with six 3D table reads each). All 15 vec4 of its material constants are read.

**Who drives it** (`Awaken.TG.Graphics.DayNightSystem.DayNightSystem.Update`, TG.Main.dll, decompiled), every frame:
- Three directional lights: `Sun` (interacts with the sky, lights nothing: affectDiffuse/Specular false), its child
  `Sun/Moon` (interacts with the sky, 10 degrees across, `surfaceTint` from a gradient, lights nothing) and `ShadowCaster`
  (lights the world with soft shadows, does not interact with the sky). All three get the same intensity
  (`lightIntensityCurve`: 60,511 lux in the night save, 75,702 to 116,983 by day), dimmer and colour temperature. By day
  the ShadowCaster follows the sun, by night the moon (`UpdateShadowCasterOwner`, day = time 0.23 to 0.92), held at least
  30 degrees above the horizon (`HandleShadowCasterRotation`, `shadowMinimumAngle`) and faded out over an hour around
  05:31 and 22:05 (`CalculateShadowIntensityMultiplier`). The sun turns 0 to 180 degrees over the day
  (`HandleLightsRotation`). Time runs every frame: `GameRealTime.ProcessUpdate` adds `deltaTime x 1440 / day length`.
- The sky material: `_SkyBlend` (day and night space), `_SkyEmissionMultiplier`, `_SkyRotation` (time x -360 plus a fixed
  angle: the stars turn), and `WyrdnightSkyboxController` the Wyrdness value.
- The Cloud Layer (`HandleCloudLayer`): opacity, tints and exposures from time-of-day curves (opacity 0 at night: the
  night save has no clouds), and the rain (`WeatherController.PrecipitationIntensity`) blends the cloud map's channel
  weights from (1, 0.2, 0.2, 0) to (0, 0, 0, 0.2), the opacity to 1 and the exposure to -1: an overcast, darker sky.
- Also the Fog, exposure, colour adjustments and indirect multiplier (not sky). `HdriSkyBlender` (other scenes) blends
  two HDRI cube maps into a render texture and calls `RequestSkyEnvironmentUpdate`.
- The fog's "sky colour": every Fog volume is in Sky Color mode (HDRP multiplies its far fog by its sky cube map).

## 2. What HDRP does on the GPU for the sky, and what the tracer reads of it today

HDRP 17 sources, 6000.0 branch (`hdrp\`); the game's `ShaderConfig.s_PrecomputedAtmosphericAttenuation = 1` (read from its
`Unity.RenderPipelines.HighDefinition.Config.Runtime.dll`), so HDRP takes the world-space table path for the sky and a
per-frame screen LUT for the haze. HDRP's "sun" (`GetMainLight`, `LightLoop.cs` 1379) is the shadow-casting directional
light: the ShadowCaster.

Per frame, for the gameplay camera, in order:
1. `SkyManager.UpdateEnvironment` (start of the frame). The sky's hash includes HDRP's sun's position and rotation
   (`GetSunLightHashCode`) and the cloud parameters; with update mode OnChanged a new hash re-renders. The ShadowCaster
   turns with the time of day every frame, so in play this runs every frame (inferred from the code; in the harness's
   frozen world it does not). Each time: the clouds' ambient probe (a low-resolution cube of the sky, then spherical
   harmonics), `RenderSkyToCubemap` (6 faces x 256 x 256 of the game's sky graph plus the cloud layer; 256 px from the
   tracer's own log "Sky: HDRP's cubemap 256px"), mipmaps, `UpdateAmbientProbe` (spherical harmonics, an asynchronous
   readback, `RenderSettings.ambientProbe`), `UpdateSkyEnvironmentConvolution` (the GGX blur of every mip). The tables of
   the atmosphere (`InScatteredRadiancePrecomputation`, three 128 x 32 x 1024 3D textures, the ground irradiance and
   multi-scattering tables) are computed only when the atmosphere's parameters change (`GetPrecomputationHashCode`).
2. `RenderSky` (after opaque lighting, before our composite): the game's sky graph full screen, depth test against
   HDRP's depth. Since step A HDRP draws nothing opaque, its depth is empty there, so the sky shader runs on EVERY pixel
   at render resolution and our composite then paints over most of them (by the code order; not measured). Inside it the
   Physically Based Sky also computes `_AtmosphericScatteringLUT` (32 x 32 x 64, the haze along screen rays up to 128 km).
3. Our composite (AfterOpaqueAndSky).
4. `RenderOpaqueFog`: height fog, volumetric fog AND the sky's haze on every opaque pixel (`_PBRFogEnabled` =
   sky type Physically Based and `atmosphericScattering` and the frame setting, all true outdoors: `Fog.cs` 184,
   `AtmosphericScattering.hlsl` 399-444). With `alphaMultiplier 0` the haze only adds in-scattered light. Skipped since
   step B's rule (`GamePasses.BeforeOpaqueFog`).
5. `RenderClouds`: the cloud layer over the sky's pixels (premultiplied alpha), fogged at its distance.
6. Transparents: lit with HDRP's ambient probe and sky cube map, fogged with its fog and the haze LUT.

What the tracer reads of HDRP's sky today:
- `Session.cs` 176-184 hands HDRP's `GetSkyReflection` (the GGX-blurred cube, one BSDF slice) to `TGPT_SetSky`;
  `tracer.cpp` `update_sky` (render event 1, D3D11) resamples its mip 1 (128 px) into a 512 x 256 lat-long map (`st.sky`)
  and mips 3 to 6 into the fog's sky tiles (`st.fogSky`), every 30 frames, first making D3D11 wait for D3D12's previous
  frame if it still reads them (`wait_for_scene_readers`). The trace of that frame then waits for this D3D11 work.
- `sky_radiance` (`pt_common.hlsli` 3485) samples it for every ray that leaves the scene: bounce rays, reflections,
  refraction, water, glass, the walk, the fog's sky light (`pt_medium.hlsli` 37), the camera-light probe (`pt_probe`).
  `fog_sky`/`fog_color` (1038-1086) for HDRP's fog on reflected rays. The cube has no sun or moon disc
  (`includeSunInBaking` false) and the cloud layer is in it.
- Pixels where the camera sees sky keep HDRP's colour: `pt_compose.hlsl` 223 writes nothing for a miss, 219-221 lays the
  traced fog over HDRP's sky (B alpha -2), 256-259 glass over the sky (B alpha -2), and `d3d11.hlsl` 518 mixes in HDRP's
  sky by A. Only `sky_through` (hair, 207-216) draws the tracer's own sky.
- The sun: `SceneLights.UpdateSun` = HDRP's main light, the ShadowCaster: its colour (intensity x colour x temperature x
  dimmer; no atmosphere, as HDRP lights with it: it does not interact with the sky), its clamped direction, its 0.5 degree
  disc (mirrors show this disc: `pt_dielectric.hlsli` 155, `pt_water_path.hlsli` 126). The moon is never traced as such.

## 3. The design of the tracer's own sky

**The model: HDRP's own, with the game's values.** Same atmosphere (HDRP 17's Physically Based Sky in world space: the
three in-scattering tables in its parameterisation, the ground irradiance and multi-scattering tables, its rescaled
Chapman optical depth, its phase functions, `AtmosphereArtisticOverride`), the same two celestial lights, the same cloud
layer bake and shading, and the game's space layer translated from its shipped shader (the decal precedent:
`tools/research/lift_decal_graphs.py`). Written anew from HDRP's documented model, as for the water ("read, nothing
copied", `docs/SOURCES.md` 202): whether porting HDRP's compute code is allowed (Unity Companion License) is the owner's
and the coordinator's call. The HDRI sky (interiors, windows) is a cube map times HDRP's intensity.

**What the plugin hands over each frame** (one new constant block, a few hundred bytes): from the gameplay camera's
blended volume stack the VisualEnvironment (sky and cloud type, planet, wind), the PhysicallyBasedSky values, the
CloudLayer values, the HDRISky values; every enabled directional light with `interactsWithSky` (today Sun and Moon): its
direction, `EvaluateLightColor`, angular diameter, distance, surface tint and texture, flare size, falloff, tint and
multiplier, shading source, moon phase and earthshine (HDRP's `FillCelestialBodyData`, `PhysicallyBasedSkyRenderer.cs`
643); the sky material instance (`DayNightSystem.SkyboxInstance`): its 15 vec4 and the script's floats; the clouds' wind
offset, which the plugin must keep adding itself (HDRP adds it only inside `RenderClouds`: `scrollFactor += speed x dt x
0.277778`, `CloudLayerRenderer.cs` 125); HDRP's pre-exposure. Textures the rays read, the game's own (no copies): the
night cube map, the Wyrdness and ground textures, `DefaultCloudMap`, the moon's surface texture, HDRI cube maps.
`TextureShare` refuses cube maps today ("arrays or cube maps"): cube sharing is a needed piece.

**The tracer's passes (D3D12), and how often:**
1. Atmosphere tables, only when the atmosphere's hash changes (load, a region with another sky profile): HDRP's
   parameterisation, about 67 million samples once (my estimate 1 to 5 ms, once; spread over frames if it hitches).
   About 48 MiB at R11G11B10 like HDRP's.
2. Cloud bake, when HDRP would (sun rotation by whole degrees, rain's weights): 1024 x 512 x 2 layers, R16G16, 4 MiB.
3. The sky map, every frame (the sun moves and the clouds drift every frame): atmosphere for the camera's position lit by
   both celestial lights, the ground under the horizon, the game's space layer, the clouds over it (their ambient from
   spherical harmonics of the cloudless map, as HDRP's background-clouds probe), no discs. Lat-long 1024 x 512 RGBA16F
   with mips (4 to 6 MiB), replacing `st.sky` and `st.fogSky`. Every `sky_radiance` and `fog_sky` reads it.
   Estimate 0.1 to 0.2 ms.
4. Camera rays that miss: the full sky per pixel (atmosphere, stars at pixel detail, clouds, sun and moon discs and the
   sun's flare halo), pre-exposed, written as complete light (B alpha 1). HDRP's colour is then never shown for sky.
   Cost in proportion to the sky's share of the screen.
5. Mirror-like rays that leave the scene add the discs (the sun and the moon), consistent with Q1.
6. The haze on far objects (the atmosphere's in-scattering between the camera and each surface, and along reflected
   rays): HDRP's 32 x 32 x 64 screen LUT rebuilt per frame (65k threads), read once per pixel in the composite, or the
   air and haze as a second medium in the traced fog. Needed now that step B skips HDRP's fog pass.

**HDRP's passes that stop, and how** (gameplay camera, while tracing; the old behaviour comes back when tracing stops):
- The environment update (the cube map, its blur, the ambient probe, the clouds' probe): the sky's update mode set to
  OnDemand by the plugin's own override volume (only `updateMode` overridden; never `Add<T>(true)`). Update mode is not
  in HDRP's hash (`SkySettings.cs` 175), and OnDemand returns before any rendering once the context is valid
  (`SkyManager.cs` 1160-1163), so HDRP's last cube map and probe stay bound for whatever HDRP still draws. While HDRP
  still lights the transparents (until step C), the plugin can ask for one update now and then
  (`RequestSkyEnvironmentUpdate`, e.g. each degree the sun moves); after C, never.
- `HDRenderPipeline.RenderSky`: a Harmony prefix that skips it for the gameplay camera (the `BeforeOpaqueFog` pattern).
  This also stops the haze LUT.
- `HDRenderPipeline.RenderClouds`: the same (it also draws volumetric clouds, which this game has off).
- `RenderOpaqueFog`: already skipped (B).
- Ours, deleted in the same job (PT-60): `update_sky` and its 30-frame D3D11 resample and wait, the fog-sky tiles,
  `TGPT_SetSky`, `HdrpPasses.SkyTexture`, `Session.cs`'s sky pointer, the composite's "HDRP's sky stays" for misses and the
  two B-alpha -2 sky mixes (`pt_compose.hlsl` 219-221, 256-259; `d3d11.hlsl` 518), the `overSky` test on HDRP's depth.

**The clouds, honestly.** The game's clouds are HDRP's Cloud Layer: a flat, artist-painted RGBA cloud picture
(`DefaultCloudMap`, four cloud types mixed by the opacity weights) on a sphere 2 km up, baked with a 6-step light march
through an 800 m slab (`BakeCloudTexture.compute`) into lit opacity, then seen as a function of direction only, drifted
by the wind (two shifted samples crossfaded, `CloudLayerCommon.hlsl` 86-128), lit by HDRP's sun (the ShadowCaster)
through the atmosphere plus the sky's ambient from below. The tracer reads exactly that layer: the game's own data and
look, not a stand-in. What it is not: a volume. Rays do not enter it, and the game lets it cast no shadow (Q3).

**The cost.** Removed from HDRP, every frame in play: the cube map (6 x 65k pixels of the game's sky graph plus clouds),
its GGX blur, two probes and a readback; the sky graph on every screen pixel; the clouds on the sky's pixels; the haze
LUT; and our 30-frame D3D11 resample with its wait. Added to the tracer: the sky map and the haze LUT per frame, the
sky's pixels, tables and bakes on change, about 55 MiB. My estimate: 0.5 to 2 ms saved, 0.2 to 0.4 ms added, and the
trace no longer waits on D3D11 for the sky. Unmeasured: the run below measures it first.

**The order of work** (each step behind a hidden switch, one scene in the game first):
1. The measurement run (below), with a trial switch that skips HDRP's three sky passes and draws every miss pixel from
   today's map. Answers how much the sky costs.
2. The haze for the fog (B), because B already removed HDRP's: the LUT or the medium; scene `vista`.
3. The atmosphere and clouds in D3D12 (tables, bake, map), the misses drawn by the tracer, HDRP's passes off; scenes
   `vista`, `npc-day` (day), `night`, a rainy save.
4. The space layer translated from the shipped shader (stars, nebula, night map, Wyrdness); `night`. HDRP's `RenderSky`
   stays on until this is in, because the night sky is what the player looks at.
5. HDRI skies (cube sharing) and the dungeon's sky; `tavern-room`, a window.
6. Remove the old code (PT-60 list above).

**Questions for the owner (each changes what the player sees):**
- Q1, the sun's place. The game lights the world from the ShadowCaster: the sun's direction held at least 30 degrees up
  (a shadow-map trick: no long shadows), and at night the moon's, also held up, with the sun's 0.5 degree size. The
  visible sun and moon in the sky are elsewhere at dawn, dusk and night. Today the tracer shows the ShadowCaster's disc
  in mirrors. Choices: (a) light the world from the visible sun and moon (the real direction, long shadows at sunset;
  the game's light curves kept); (b) keep the game's lighting and draw only the visible discs from the sky (two suns in
  water at low sun: the highlight from the light, the disc from the sky). My recommendation: (a).
- Q2, the haze. The game's atmosphere adds a lot of haze light to far land (aerosols 58 times HDRP's default) but its
  `alphaMultiplier 0` makes it hide nothing. Keep that (the game's look), or let the haze also dim what is behind it
  (physically right; far hills paler and flatter)? My recommendation: the game's look first, the physical one as a
  choice shown side by side.
- Q3, cloud shadows. HDRP can shade the sun by the cloud layer; the game turned that off. The tracer could dim each
  sun ray by the layer's opacity where it crosses it: moving cloud shadows on the land. Add it, or keep the game's?

## 4. Risks

- **The fog (now).** B's skip of `RenderOpaqueFog` also removed the atmosphere's haze outdoors (item 4 of section 2).
  The traced fog (`pt_fog.hlsl`) has no atmosphere. Far land is clearer than in the game. Design item 6.
- **HDRP's transparents and effects until step C** read HDRP's sky cube map (reflections, the fog's sky colour), its
  ambient probe and the haze LUT. With the environment frozen they stop following the time of day; the haze LUT is
  frozen in screen space, so far transparents' haze is wrong while the camera turns. Mitigations: an occasional HDRP
  update, or the tracer's map copied into HDRP's cube and probe at the composite (no new wait: the composite already
  waits for the trace). Gone with C.
- **The traced fog and reflections** use the sky three ways (`pt_medium` sky light, `fog_color`/`fog_sky`, `sky_through`):
  all must move to the new map in the same job.
- **The night sky's detail** depends on translating the game's shader exactly (about 780 lines of star and nebula
  arithmetic, 15 vec4 of constants). Mistakes show as a different star field; compare per scene against HDRP's.
- **The clouds' drift** depends on the plugin adding the wind offset the way HDRP does; wrong bookkeeping makes clouds
  jump when tracing is turned on or off.
- **The moon.** Its HDRP data (shading source, surface texture, flare) is not in the audits; read it at run time.
- **Interiors.** HDRI skies are cube maps the tracer cannot open yet; through windows the player would see black or
  HDRP's sky until cube sharing exists. Rays inside interiors see no sky (unchanged).
- **Reflection probe cameras** take the same OnDemand override (it is a volume): their skies freeze too. Only HDRP's own
  transparents use them.
- **Lens flares** read the clouds' opacity only through HDRP's SRP lens flares, which the game has off
  (`enableLensFlare`, `HandleLensFlare` empty). The screen-space lens flare is post-processing and stays.
- **What a player would notice** if done right: nothing, plus a sharper sky in reflections (the map is finer than HDRP's
  blurred 128-px mip), the moon in mirrors, and the answers to Q1 to Q3. If done wrong: stars, cloud speed, the moon's
  look, a seam at the horizon.

## The game run that answers what is open

One scene, world RUNNING (the environment only re-renders while the time of day moves), clean card, foreground, 16 frames
per step: `vista` (the open world by day), then the same in `night`.
- Step a: as now. Step b: a hidden trial switch: sky update mode OnDemand plus prefixes skipping `RenderSky` and
  `RenderClouds` for the gameplay camera, every miss pixel drawn from today's map (as `sky_through` does). Read: the frame
  time and the stage times in `gpu.json`/`report.md`, the difference is HDRP's sky. Count `RenderSkyToCubemap` calls per
  frame (a Harmony postfix counter, logged) to prove it runs every frame in play.
- Step c (the haze): HDRP's fog drawn (the hidden "HDRP's fog in place of the traced fog") with and without the sky's
  `atmosphericScattering` (override volume): the far land's brightness difference is the haze B removed.

## Sources

- The game: `Awaken.TG.Graphics.DayNightSystem.DayNightSystem`, `WyrdnightSkyboxController`, `HdriSkyBlender`,
  `WeatherController`, `GameRealTime`, `FogAndCloudsLayerDisabler`, `CloudsShadowsResolutionController` (TG.Main.dll, by
  ilspycmd); `ShaderConfig` (Unity.RenderPipelines.HighDefinition.Config.Runtime.dll); the sky shader in bundle
  `34fb8d82e3bff3665cb9a1f17452d8c1.bundle` node `CAB-a7fd434d5f26f944d77aa3292c36af6f`.
- The harness audits under `artifacts/harness/*/*/1-game/audit-b.txt` (runs of 2026-10-08), the tracer's log lines
  "Sky: HDRP's cubemap 256px" and "Sky for rays: HDRP's CubemapArray 256px".
- HDRP 17, `Unity-Technologies/Graphics` branch `6000.0/staging`, `Packages/com.unity.render-pipelines.high-definition/
  Runtime/`: `Sky/SkyManager.cs`, `Sky/SkySettings.cs`, `Sky/PhysicallyBasedSky/{PhysicallyBasedSky.cs,
  PhysicallyBasedSkyRenderer.cs, PhysicallyBasedSky.shader, PhysicallyBasedSkyRendering.hlsl,
  PhysicallyBasedSkyEvaluation.hlsl, PhysicallyBasedSkyCommon.hlsl, InScatteredRadiancePrecomputation.compute,
  SkyLUTGenerator.compute, ShaderVariablesPhysicallyBasedSky.cs}`, `Sky/CloudSystem/CloudLayer/{CloudLayer.cs,
  CloudLayerRenderer.cs, CloudLayer.shader, CloudLayerCommon.hlsl, BakeCloudTexture.compute}`,
  `Lighting/AtmosphericScattering/{AtmosphericScattering.hlsl, Fog.cs}`, `Lighting/LightLoop/LightLoop.cs`,
  `RenderPipeline/HDRenderPipeline.RenderGraph.cs`. Read, nothing copied.
- The tracer: `tracer.cpp` 2191-2222 (`update_sky`), 1748-1753; `pt_common.hlsli` 3485, 1034-1086; `pt_compose.hlsl`
  97-104, 204-223, 254-260; `d3d11.hlsl` 66-86, 497-518; `pt_primary.hlsli` 576-619; `Session.cs` 176-184;
  `HdrpPasses.cs` 1407-1416; `SceneLights.cs` 603-608, 806-820; `GamePasses.cs`.
