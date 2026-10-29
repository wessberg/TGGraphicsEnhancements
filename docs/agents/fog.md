# PT-37 (step 1) and PT-46: the game's fog along the tracer's own rays

Agent report, 2026-10-07. Built and tested offline in a copy of the tree (`wt/fog`, base `wt/base20`). Nothing ran in the
game, nothing was deployed or committed. A first agent wrote most of the code and stopped at a usage limit (05:21); a
second checked it, fixed two holes, added the offline test and its mutation checks, and wrote this.

## In three lines

1. A ray the tracer sends off a mirror, a glossy surface or water, or out through solid glass, now gets the game's fog
   over its own way, as HDRP fogs a camera's view: dimmed by the height fog, filled in with the fog's own colour (the
   Fog volume's tint times HDRP's sky, at HDRP's mip and the game's exposure). HDRP still fogs the camera's way to the
   mirror, as before.
2. Offline: a perfect mirror showing a wall 3 m off in thick height fog gives what HDRP's formula gives for a view of
   that wall from the mirror's place: 0.97 to 1.00 of it per channel, with a constant fog colour and with the Sky Color
   mode. Both mutations fail it. tgpt_gpu_test, the 85 source checks and the
   plugin build pass.
3. Not seen in game. The next `mirror-day` run should show far things in the mirror at about the game's brightness and
   in its grey-blue mist (0.14 to 0.23 of it before).

## What HDRP does with its fog

From HDRP 17's `AtmosphericScattering.hlsl` (`EvaluateAtmosphericScattering`, `GetFogColor`), `VolumeRendering.hlsl`
(`OpticalDepthHeightFog`) and `Fog.cs`:

- A pixel at distance t from the camera: within the volumetric fog's reach (the Fog volume's Depth Extent) its fog light
  comes from HDRP's volumetric buffer, lit by HDRP's lights with the volume's anisotropy and by local fog volumes. Past
  it, the "distant fog": transmittance T = e^-(optical depth over the rest of the way), and light
  `GetFogColor(V, t) * exposure * albedo * (1 - T)` added.
- The optical depth: homogeneous below the base height, thinning as e^-((h - base) / H) above it. The tracer's
  `height_fog_depth` is the same integral (the new test checks it against HDRP's own form).
- `GetFogColor`: the Constant Color mode gives the volume's Color; the Sky Color mode its Tint times HDRP's sky cubemap
  in the ray's direction at mip `(1 - MaxMip * saturate((t - Near) / (Far - Near))) * 6`: mip 6 up close, mip 3 far
  away with a Max Mip of 0.5.
- What it scatters (`_HeightFogBaseScattering / _HeightFogBaseExtinction`): the volume's albedo while its volumetric fog
  is on, else 1.
- The sky (depth on the far plane) is fogged as a surface at the Max Fog Distance.
- The game (audit `20261006-234757/vista/1-game/audit-b.txt`, the blended stack): Sky Color mode, tint (0.906, 1,
  0.862), albedo 1, mean free path 682 m, volumetric fog on, Depth Extent 128 m, Max Fog Distance 5000 m. Local volumes
  change it: the swamp's albedo (0.36, 0.38, 0.21), 32 m and Depth Extent 64 m; the castle's Wyrdness volumes 38 to
  200 m, blue albedo.

## What changed

- Plugin (`HdrpPasses.FillFog`): sends the fog's colour as HDRP takes it (albedo times tint or colour; w 1 in the Sky
  Color mode) and its mip fog (Near, Far, Max Mip). A failed read logs one warning and sends nothing.
- Native (`tracer.cpp`): `update_sky` also resamples HDRP's sky cubemap at mips 3 to 6 into a 128 x 256 texture, four
  lat-long tiles stacked (`fogSky`; `d3d11.hlsl`'s sky shader writes at a row offset, c0.z), with the sky every 30 frames.
  The shaders get the colour, the Max Fog Distance, the mip fog and the fog sky's view. In the Sky Color mode without
  HDRP's sky (not a cubemap: the tracer's plain sky) no fog colour goes out (fixed in this round: it sent the tint alone,
  near black in HDRP's units).
- Shaders (`pt_common.hlsli`): `fogged(radiance, from, dir, dist, shownAt)` gives what a ray met as HDRP would show it
  from the ray's origin: times the transmittance over the ray's way (the sky at the Max Fog Distance), plus `fog_color`
  times (1 - T). `fog_color` is the constant colour, or the tint times `fog_sky` (HDRP's sky at the mip fog's mip,
  blended between tiles). The gains: what the ray met takes the traced gain of the whole way's fog (Fog Adaptation fades
  it, as `traced_gain_at` does for the camera), the fog's light the fog's own (`fog_gain`), both over the gain the
  composite gives the pixel. No fog colour: nothing (fixed in this round: before, a missing colour still dimmed).
- Callers: `pt_specular` (the reflection ray: mirrors and every glossy reflection; out of solid glass, both ways a ray
  leaves it) and `pt_water` (water's reflections).
- Left as they were, on purpose: Glossy Reflections' cached light (light from around the surface, no way to fog), a
  sheet of glass (HDRP fogs the view through it), reflected lamps and the sun's disc (dimmed on their way already:
  `light_incident`, `sun_fog`), bounced light (its lights are fogged already).

## Files

- `native/pathtracing/include/tgpt_api.h`: `TGPT_Frame.fogColor`, `fogMip`; size 600.
- `native/pathtracing/src/runtime.hpp`: `kFogSky*`.
- `native/pathtracing/src/tracer.cpp`: `FrameConstants.fogColor`, `fogMip` (6160 bytes); `st.fogSky`; `update_sky`;
  the constants in `trace`; barriers in `trace` and `trace_water`.
- `native/pathtracing/shaders/d3d11.hlsl`: the sky resample's row offset.
- `native/pathtracing/shaders/pt_common.hlsli`: the two rows; `traced_gain_through`, `fog_gain`, `fog_sky`,
  `fog_color`, `fogged`.
- `native/pathtracing/shaders/pt_specular.hlsl`, `pt_water.hlsl`: the calls.
- `src/TGPathTracing/HdrpPasses.cs`, `NativeTracer.cs`: the fields, size 600.
- `tests/pathtracing_gpu_test.cpp`: the fog in PT-46's mirror case; `tests/source_checks.py`: the new layout and lines.
- `CHANGELOG.md`: an entry at the top of the path tracing part.

## ABI and bits

- `TGPT_Frame` grows from 568 to 600 bytes: `fogColor[4]` and `fogMip[4]` at its end (`NativeFrame` the same). The ABI
  number is not changed here (the main tree's is 0x00011A00): the merge raises it once for these two fields.
- `FrameConstants` grows from 6128 to 6160 bytes: two rows at its end, `fogColor` (rgb, w the Max Fog Distance) and
  `fogMip` (Near, Far, Max Mip, w the fog sky's view). `textures.z` untouched.
- No material bit, no option bit. One new texture shared with D3D11: 128 x 256 RGBA16F (256 KB).

## Tests

- `tgpt_gpu_test` (build `%TEMP%\tgb\fog-build`, DLLs installed to `%TEMP%\tgb\fog-inst`, log `fog-test-1.txt`):
  every check passes (895 "ok"). PT-46's mirror without fog is as before: 0.982 brightness, 0.989 and 0.981 colour.
- New, in PT-46's mirror case: the floor a perfect mirror, the camera 1 m over it looking at the red wall 3 m off, in a
  height fog of a 3 m mean free path below 0.3 m, thinning above it with a scale height of 0.4 m (the way from the floor
  to the wall crosses the base). What the mirror shows is held against the wall seen directly from the camera's mirror
  image under the floor (the same fog dims the lights on it), times the transmittance over the floor-to-wall way, plus
  the fog's colour at the exposure times (1 - T): HDRP's formula, its optical depth written out in the test in HDRP's
  own form. 143,758 pixels, T 0.689 on average. Each channel must be within 5 %.

  | fog colour | the mirror (r, g, b) | HDRP's formula | mirror / formula | the wall with no fog on the way |
  |---|---|---|---|---|
  | constant (0.6, 0.8, 1.0) | 0.3275 0.0903 0.0947 | 0.3291 0.0924 0.0975 | 0.995 0.977 0.971 | 0.4108 0.0440 0.0288 |
  | Sky Color, tint (0.9, 1, 0.8) times the sky | 0.3144 0.0778 0.0771 | 0.3153 0.0792 0.0789 | 0.997 0.982 0.977 | 0.4103 0.0433 0.0279 |

- Mutation A, `fogged` hands back what the ray met (the state before this work; DLLs `fog-mutA`, log
  `fog-test-mutA.txt`): both new checks fail, the mirror at 1.23, 0.46, 0.28 of HDRP's formula (constant colour) and
  1.28, 0.54, 0.34 (Sky Color). Nothing else fails.
- Mutation B, the Sky Color mode's colour without the sky (`fog_color` leaves out `fog_sky`; `fog-mutB`): the Sky Color
  check fails (1.11, 1.32, 1.00), the constant colour check passes as it should. Nothing else fails.
- Source checks: `python tests\source_checks.py`, 85 pass. Updated: the constants' size (6160) and rows (36), the fog's
  lines in `pt_common`, `pt_specular` and `pt_water`, the plugin's volumetric check, the new test's name, and the
  refracted ray's line (now through `fogged`).
- Plugin: `dotnet build src\TGPathTracing -c Release -o %TEMP%\tgb\fog-managed`: 0 errors.
- Another agent's GPU test ran alongside these: no timings from them.

## What the next harness run should show

- `.\tools\harness\Run-PtHarness.ps1 -Scenario mirror-day,mirror-tavern`, then `python tools\harness\mirror.py <scenario
  folder>` for each. `mirror-day`: far things in the mirror were 0.14 to 0.23 of their brightness against the game's view
  from the mirror's side; they should now be near it (0.7 to 1.3) and grey-blue like it, near ones (a few metres) as
  before. `mirror-tavern`: indoors the fog over a few metres is thin: within a few percent of before.
- `vista`: water and glossy stone reflecting far hills veiled like the hills themselves; the rest of the traced picture
  as before. Read the exposure and gain log line: added fog light in reflections can only brighten reflections of far
  things, so the exposure should barely move.
- `BepInEx/LogOutput.log`: no "HDRP's fog color could not be read" line.

## Risks

1. Within the volumetric fog's reach (128 m) the camera's fog light is HDRP's volumetric lighting: the sun's glow with
   the anisotropy of 0.31, lamps, local fog volumes. A reflected ray gets the distant fog's sky-coloured light over its
   whole way instead. Looking towards the sun the game's mist glows brighter than the mirror's will. Local fog volumes'
   own density (the castle's, a swamp's patches) is not on the ray at all, only the blended height fog.
2. The fog's light in the mirror is the sky's: if HDRP's sky cubemap is not what `GetFogColor` samples in this game (a
   custom sky with its own fog), the colour is off. The harness's `vista` and `mirror-day` will say.
3. Fog Adaptation with the traced gain away from 1 is not covered offline (the test runs at gain 1).
4. Cost: two optical depths and two texture reads per reflected pixel, one more small resample every 30 frames.

## Step 2: the tracer lighting the fog (design, no code)

What the player would see: sunbeams through trees, windows and doorways, shadows cast into the mist, a glow around a
torch that a wall cuts off, all from the tracer's own lights and shadows, in the game's own fog (its density, colour and
fog volumes stay the game's).

- **What HDRP does now.** Within the Depth Extent (128 m; 64 m in the swamp) HDRP fills a grid of small boxes along the
  view (its volumetric buffer, a "froxel" grid: screen tiles times depth slices) with the light its lights scatter
  there, through their shadow maps, with each light's Volumetric Dimmer and the volume's anisotropy, plus the sky's
  ambient. It integrates that along the view and filters it; the fog pass then adds it to every pixel. Fill lights put
  their fog there too (the Fill Light Fog row keeps that part).
- **Where the tracer's part runs.** A compute pass on the tracer's D3D12 queue, after reflections, a frame behind like the
  rest. Its grid matches HDRP's (size, depth slicing, Depth Extent; the plugin reads them from HDCamera and the Fog
  volume and sends them in `TGPT_Frame`). It reads HDRP's own density grid (shared like the GBuffer), so local fog
  volumes keep their shape. Per box: one ray to the sun through the scene (the same scene the shadows use), one light
  picked from the traced lights' list (the one RTXDI samples from) with its shadow ray, HDRP's phase function with the
  volume's anisotropy. Each frame does a quarter of the boxes; the rest comes from the last frames, moved with the
  camera, then a small blur. Then the same front-to-back integration as HDRP's.
- **How it meets HDRP's.** Light adds up: the traced lights' share integrated through the same fog adds to the share
  HDRP keeps. At render event 2 (after opaque lighting, before HDRP's fog pass) D3D11 adds the tracer's integrated grid
  to HDRP's. While tracing, the plugin sets the Volumetric Dimmer of the sun and of every traced light to 0, so HDRP no
  longer puts their light in the fog (no double light); fill lights keep theirs (Fill Light Fog, the owner's ruling), and
  so does the sky's ambient. Off, it puts every dimmer back.
- **Along reflected rays.** `fogged` takes its fog light from the same grid where the ray's way is inside the camera's
  view (a few taps along it), else the distant fog's light as now: the mirror then shows the sunbeams too.
- **Cost.** A 1440p view at HDRP's medium grid (about 320 x 180 x 64, 3.7 M boxes): a quarter a frame is about 0.9 M sun
  rays and 0.9 M light rays, roughly what the reflections cost now (to be measured, about 1 to 2 ms); the integration
  and blur about 0.2 ms; about 60 MB for the grid and its history.
- **Risks.** HDRP's grid layout must match exactly (HDRP 17's `VolumetricLighting.compute`; read its constants, never
  guess); the beams are noisy and need the temporal pass; brighter mist moves the exposure (read the exposure and gain
  line); with HDRP's volumetric fog off (a player setting) there is no grid: then nothing changes (the game's fog stays
  as it is).
- **First step to test it.** A debug view of the traced grid against HDRP's at the `vista` save, before adding anything.
