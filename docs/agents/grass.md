# Grass and leaves under the tracer (PT-03): the grass agent's report

Written 2026-10-06 by the grass agent (one of three parallel agents), in its own copy of the tree. Nothing deployed, nothing
committed, the game never started by this agent. Numbers are from the offline harness (`tgpt_gpu_test`, built in
`%TEMP%\tgb\grass`) or from the in-game harness's kept frames (read only). Starting point: `docs/PT-VEGETATION.md`.

## In short

- Built: six changes for grass and leaves, the grass shadows the owner chose (way A), and the same for thin cloth (PT-39:
  tinted, and swaying cloth found again). Each has an offline check, and each check fails when its change is broken on
  purpose.
- Passes the offline harness: yes, but for the hair agent's known failure. Not seen in game.
- The lost green and the grey sheen have one main cause: blades reflected the sky with a reflectance of 0.04, where the
  game's foliage has 0.0023. The second: light through a blade was grey, where the game's is yellow-green and 1.4 to 6
  times the light.
- The white spots in the harness frames went with the fog on the sun (the build of 17:02). What is left of them are wet
  blades glinting, which the 0.0023 reflectance takes down 17 times.

## What the grass looks like now, measured (before these changes)

The scripts and crops are in the session's scratchpad
(`C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad`,
"scratchpad" below). `scratchpad\grass_chroma.py [run...]` over the grass pixels (npc-day: the Surfaces view's green; elsewhere: what the game's own
picture shows green), the sRGB curve undone, the same pixels in both pictures:

| run, scene | game: chroma, blue/green | traced: brightness against the game's, chroma, blue/green | near white, traced |
|---|---|---|---|
| 145526 npc-day | 0.57, 0.43 | x4.73, 0.33, 0.72 | 0.01 % |
| 145526 forest | 0.54, 0.48 | x2.27, 0.36, 0.69 | 0.18 % |
| 170351 npc-day (fog on the sun) | 0.55, 0.47 | x1.53, 0.38, 0.67 | 0.00 % |
| 170351 forest | 0.54, 0.48 | x0.90, 0.34, 0.71 | 0.00 % |
| 170351 night (2,064 pixels only) | 0.46, 0.52 | x1.17, 0.16, 0.99 | 0.00 % |

(metal and candle are left out: there the game's green pixels are mostly the guard's scale armour and props, not grass.)

So the fog on the sun fixed the brightness and the white; the colour is still lost: a third less chroma, and blue at two
thirds of green where the game's is under half. Where it goes (run 163946's debug views, npc-day's grass): the Specular
view holds a quarter of the grass's light (luminance 0.039 of 0.160) at a chroma of 0.15, i.e. a grey film of reflected
sky over every blade; the Indirect view is bluish (blue/green 1.08: the overcast sky); HDRP's own grass colour (Albedo
view) has blue/green 0.52. In the crops (`crop-npc-grass-170351.jpg`, `crop-forest-170351.jpg` in the scratchpad) the
game's blades are lit yellow-green from behind; traced, they are grey with white glints on the wet ones.

## The changes, in the task's order

1. **A blade's reflectance from its diffusion profile.** HDRP gives a transmission pixel its profile's Fresnel0
   (`_TransmissionTintsAndFresnel0[profile].a`): 0.0023 for the game's foliage (an index of refraction of 1.1, 88 of its
   128 grass and leaf materials), 0.087 for its grass profile (23), 0.017 snow. The tracer used 0.04 for all.
   - C#: `HdrpPasses.ThickProfiles` sends every profile's Fresnel0 (`TransmissionOf(...).tint.w`) through the new export
     `TGPT_SetProfileReflectance` when it changes, and logs "HDRP diffusion profiles' mirror reflectance (a leaf's, in place
     of 0.04): ...". ABI 0x00011500 (tgpt_api.h, NativeTracer.cs, the source check).
   - Native: `Engine::profileF0`, `FrameConstants.profileF0` (3040 B), `CompositeConstants.profileF0` (176 B) for the guides.
   - Shaders: `pt_prepare` (`profile_f0(leaf_profile(...))` for deferred transmission pixels through a thin profile, not
     colored transmission, not skin), `d3d11.hlsl` TGPT_GUIDES (Ray Reconstruction's specular guide).
   - The tracer's Schlick keeps its f90 = saturate(50 f0): at 0.0023 a blade's grazing reflection is 0.115, where HDRP's
     f90 is 1 for transmission. Deliberate: it is the rule for every surface, and grazing blades were the grey film.
2. **Light through a leaf from its profile, and the light from behind it.**
   - The plugin sends every profile's transmission constants, not only the thick ones' (`HdrpPasses.cs`: the mask filter
     is gone; skin's use is unchanged, it already reads only solid profiles).
   - `pt_common` `leaf_tint(p, s)`: HDRP's transmittance of the pixel's thickness (`skin_transmittance` on GBuffer2) over the
     grey share, 1 where there is none. Used for the sun and lamps behind a blade (`pt_di_spatial`, after ReSTIR, which
     keeps weighing with the grey share, so nothing is counted twice).
   - The sky's and bounced light from behind (`gathers_behind`): a third of a leaf's rays leave from its far side
     (`pt_gi`, weights by the side's share of the rays, its mean tinted), ReSTIR GI's target and visibility take that side
     (`pt_rtxdi`), the final shading tints them (`pt_gi_spatial`). Thin cloth and hair are left out (hair's bounced-light
     add is a dead end). For every other surface the code path is the same as before (same random numbers).
   - Not like HDRP: HDRP's back light is wrapped 15 degrees (`ComputeWrappedDiffuseLighting`, at most 0.795 of the tint);
     the tracer's is the plain cosine (at most 1). Up to a quarter brighter straight from behind.
3. **The material's own occlusion** (pt_prepare's flag 128 in mat.y): HDRP's AO from GBuffer3's green behind HDRP's tag,
   and GBuffer2's red (specular occlusion on the transmission features), stored in the specular bytes' green and blue (the
   leaf's reflectance in red). `pt_compose` takes the AO off the bounced and sky light only; `pt_specular` the SO off the
   reflections. The sun and lamps keep theirs, as in HDRP. Unknown until captured in game: how strong the game's values
   are, and whether its GBuffer2's red is packed with APV's lightmap bit (then it reads half; PT-SURFACES says APV is off).
   Three of the 128 materials have no mask map (Mat_Vegetation_Grass_Forlorn_01_SG among them): if their AO reads 0, they
   lose their bounced and sky light. Check them in game first.
4. **A thin blade's own normal for its rays** (`pt_prepare`): where the plane through the neighbouring pixels is more than
   60 degrees off HDRP's normal, HDRP's normal serves (also the 14 materials whose back faces keep a normal away from the
   camera). **The bounce colour** (`SceneCapture.cs`): a vegetation material's base colour is multiplied by the mean of its
   linear `_HealthyColor` and `_DryColor`.

**Grass shadows, way A (the owner's choice, 17:06).** `alpha_opaque`: a `TGPT_MATERIAL_TRANSLUCENT` hit stops a light's
shadow ray only if `ray_chance(..., 0x6C656166u) >= TRANSMISSION` (then the coverage test as before), while `g_lightRay` is
set: in `transmittance`, `transmittance_from_surface` (the final shading's rays: the sun, lamps, path vertices, sunlight off
water, HDRP-shaded hair) and `rtxdi_visible` (ReSTIR DI's reuse, so it weighs a sample behind grass as the shading lights
it). Bounce and reflection rays still stop at a blade. `pt_omm_bake`: a translucent material's covered micro-triangles
become "unknown opaque", so the per-ray test runs there (without it, opacity micromaps commit them as solid and the half
shadow never happens on DXR 1.2). Way B (the renderer's shadowCastingMode, instance flag 128) was not built; the flag stays
free. Patch for B, if wanted later: `docs/PT-VEGETATION.md` Change 2, way B.

**Thin cloth (PT-39, asked for at 17:5x).** The same rule for `TGPT_MATERIAL_THIN_CLOTH`: a light's shadow ray passes a
covered texel by `CLOTH_TRANSMISSION`'s chance (0.3), and the crossing is tinted by the cloth's colour at full brightness
(`cloth_tint`; `alpha_opaque` counts it as a glass crossing, `glass_walk` commits covered cloth and multiplies the tint).
Holes pass as before. `SceneCapture.cs` makes every thin cloth alpha-tested (cut-off 0 where it had none), so that rays
test it at all; `pt_omm_bake` leaves its covered texels to the per-ray test.

Why the canopy's underside stays black (`artifacts/harness/20261006-174549/candle-up/views.jpg`): the Surfaces view does
not show thin cloth at all (it colours by HDRP's GBuffer feature; the canopy is HDRP's standard material, grey), so its grey
proves nothing. What the views do show: the Direct view is black on the canopy, so either its pixels are not taken for
cloth, or the sun does not reach its top. Two causes fit, and I can't tell them apart offline:
1. The canopy is `TG/Clothes/Cloth_Wind` ("Mat_Prop_Cloth_PlaneBig_04_HoS_moving" in that run's log): the wind moves it in
   HDRP, the tracer has it still. pt_prepare's camera ray looks for cloth within 3 cm + 1 % of HDRP's depth (6 cm at 3 m),
   so a waving canopy overhead is missed and lit as an opaque sheet from below. Fixed: where the traced scene has nothing at
   all within that window, a swaying thin cloth within 30 cm counts (`CLOTH_SWAY_REACH`; a wall the cloth hangs in front of
   is found in the window first, so it is never taken for cloth). Offline check 8.
2. The sun's shadow ray from the canopy's top meets the canopy's own still copy beyond the 20 cm sway reach (or other
   cloth above): a full blocker until now; with the change it lets 0.3 through, tinted red.
In the game's picture the underside's red glow is HDRP's ambient light from below (the canopy is a standard material in its
GBuffer, not a transmission one). Traced, the underside gets the floor's bounce (dark, in the canopy's shade until now; now
lit by 0.3 of the sun, red) and the light through it. To tell 1 from 2: an F8 under the canopy with the crosshair on it
(the report's pick names the traced material and whether the surface is the tracer's), then the Direct view.

## Files changed

`native/pathtracing/include/tgpt_api.h`, `native/pathtracing/src/{extension.cpp, runtime.hpp, tracer.cpp}`,
`native/pathtracing/shaders/{pt_common.hlsli, pt_prepare.hlsl, pt_di_spatial.hlsl, pt_gi.hlsl, pt_gi_spatial.hlsl,
pt_rtxdi.hlsli, pt_specular.hlsl, pt_compose.hlsl, pt_omm_bake.hlsl, d3d11.hlsl}`,
`src/TGPathTracing/{HdrpPasses.cs, NativeTracer.cs, SceneCapture.cs}`, `tests/pathtracing_gpu_test.cpp`,
`tests/source_checks.py`, `CHANGELOG.md`, and this file. Bits used: ABI 0x00011500, mat.y flag 128. Not used: material bit
131072, instance flag 128, `TGPT_Frame.forward[3]` (still free).

## Offline checks

Before (`wt\base` built as it was at 17:00, Agility staged): `tgpt_gpu_test` 1 failure, the known "layer hair covers the
wall ..."; source checks 1 failure, the known `test_path_tracing_no_placed_lights`.

After (DXR 1.2 and SER on, micromaps on; `%TEMP%\tgb\grass-test-final.txt`): the same single known failure in each. Of the
existing checks' printed numbers only one moved: the sunlit translucent card seen in the water, 0.0882 -> 0.0905 (its
shade lets light through now). The leaf/cloth ratio stays 0.60. New checks (section "Grass and leaves as HDRP
shades them" at the end of `tests/pathtracing_gpu_test.cpp`; new source check `test_path_tracing_grass`):

| check | measured | expected |
|---|---|---|
| a wet blade's reflections with 0.0023 against 0.04 | 0.06 (0.00019 / 0.00317) | < 0.2 |
| Ray Reconstruction's specular albedo there | 0.004 against 0.039 | < half |
| sun through a 4.8 mm leaf of the foliage profile, against the grey half | 7.79, 9.00, 1.64 | 7.79, 9.00, 1.64 |
| sky from behind a leaf facing down, of the sky's | 0.43, 0.46, 0.47 grey; 3.92, 4.49, 0.80 tinted | 0.5; 3.89, 4.50, 0.82 |
| AO 0 / 0.3 / 1 on the picture | 0.30 of the way | 0.30 |
| SO 0 / 0.5 / 1 on the reflections | 0.50 of the way | 0.50 |
| a blade seen from its unflipped back | 0.85 of the camera-facing one | > 0.5 |
| a translucent card's shadow, sun let through | 0.50 (micromaps on: 3 instances) | 0.5 |
| a red cloth card's shadow: sun through in red; green's share of red's | 0.30; 0.107 | 0.30; 0.107 |
| a cloth drawn 25 cm from its traced copy, lit through | 0.73 of in place (broken: -0.24) | > 0.5 |

The grey sky-from-behind is 6 to 14 % short (see below).

Source checks updated, each with its reason in a comment: the ABI number (0x00011500), FrameConstants 3040 (twice), the
cache feed line (`const bool feed = feeds && k == 0 && !back;`: a leaf's far-side ray feeds no cell).

## Mutation checks

`scratchpad/mutate.py` breaks a set of lines on purpose (each found exactly once), the tree is built and the test run, and
the files are written back anew (new timestamps, so the next build takes them).

| set | broken | checks that failed (and their numbers) |
|---|---|---|
| A | pt_prepare's profile reflectance; d3d11's guide reflectance; pt_di_spatial's sun tint; pt_compose's AO; pt_specular's SO; pt_prepare's blade normal | exactly the six aimed at: reflections 1.00 of before, guide 0.039 = 0.039, tint 1.00 1.00 1.00, AO 0.00 of the way, SO 0.00, unflipped back 0.35 |
| B | gathers_behind; the leaf's shadow pass; the cloth's shadow pass; the swaying cloth's second window | the four aimed at: sky from behind 0.00, leaf shadow 0.00, cloth shadow 0.00, swayed cloth -0.24; and the unflipped blade at 0.60, then the threshold of 0.6, now 0.5 (with A's 0.35 it still fails) |
| C | pt_omm_bake's line for leaves and cloth (micromaps on) | the cloth's shadow: 0.00 through. The leaf card's stayed at 0.50: its alpha tile is too fine for its micro-triangles to be settled at all (every one "unknown"), so the leaves' half of that line has no offline check of its own; it is the same line. |
| D | glass_walk's cloth tint | the cloth's shadow: green 1.000 of red's share (0.107 expected) |

Runs C and D also failed the swaying cloth's check at its first threshold of 0.8: in place, the cloth's light through is
0.0659; drawn 25 cm from its traced copy, 0.0478 (0.73), because the still copy in front now shades the lamp on that side
by 0.7. The threshold is 0.5 now (broken, the check reads -0.24).

The same run A measured the sky from behind a leaf without the GI boiling filter: 0.43 0.46 0.48, as with it. So the
filter is not why the grey case is 6 to 14 % short; with the foliage tint it is exact, so the shortfall is a small additive
one on the leaf's front side (its floor light), not in what comes from behind. Not chased further.

## "Pure white" grass: what it is

- In the harness frames the near-white share of the grass was 0.01 % (npc-day) and 0.18 % (forest) before the fog on the
  sun, 0.00 % in both after (run 170351). So most of the white patches were the unfogged sun.
- What is left: wet blades glinting. The game's wetness pass makes blades glossy in rain; HDRP's blade then still mirrors
  0.0023, the tracer's mirrored 0.04 with a grazing reflection of 1: a blade facing the sky at a grazing angle reflected all
  of it, and a smooth blade's sun highlight came to about 100 times a white sunlit surface, cut at the direct clamp (48
  times): pure white. At 0.0023 the highlight's core is about 6 times (before the fog's share), the grazing reflection
  0.115 of the sky. Offline: a wet blade reflects 0.06 of what it did.
- Not found: NaN or overflow (no frame failed; no pixel at all white in the new run's grass); a light reaching grass
  without the fog (every path of the sun and the lamps takes it since 16:35). The direct light's clamp (48 times a white
  sunlit surface) lets any glossy highlight go white; that is not grass's and I left it.
- With the tint, a blade lit from behind by the sun gets up to 9 times the grey half's light in green. That is HDRP's
  look (the game's blades glow yellow-green from behind), but watch for new bright blades with the sun low behind grass.

## The torch's orange on grass (PT-38 is the main session's)

Grass-side reasons a torch would not tint grass, all addressed here: (1) every traced blade between the torch and the lit
blade stopped its light whole: now half per blade; (2) ReSTIR DI's reuse visibility counted blades as solid: now the same
half; (3) a lamp behind a blade lit it grey at half: now tinted by the profile. Nothing else in grass keeps a lamp's light
out; if the torch's own lights are left out (the classifier), none of this shows.

## What to look at in game

Run the harness `ab` on npc-day, forest, metal, vista, night and `views` on npc-day; compare with 170351:
- the grass's chroma and blue/green (`scratchpad/grass_chroma.py <run>`): expected up, towards the game's 0.55 and 0.47;
- the Specular view on grass: nearly black;
- the Direct view: grass shadows softer and lighter, tree shade still dark;
- the exposure and gain line: the AO and the lower reflections remove light, the tint adds some;
- the GPU time by stage (direct light, bounced light): micromaps no longer settle covered grass texels;
- night: grass near a torch (with PT-38's fix);
- the plugin's new log line, once: "HDRP diffusion profiles' mirror reflectance ...: DiffusionProfile_Foliage 0.0023, ...";
- PT-39: `candle-boil` and `candle-up` (`ab` and `views`): the corner under the red canopy (the game's 92 of 255), the
  canopy's underside, and an F8 there with the crosshair on the canopy (is it the tracer's thin cloth?).

## Docs the main session should update when it merges

PROJECT-CONTEXT 9.1 (ABI 0x00011500, FrameConstants 3040 B), 9.7 Translucent and thin cloth (profile reflectance, tinted
transmittance, light from behind, occlusion, half shadows; cloth's tinted 0.3 and the swaying cloth's 30 cm reach), 9.12
(mat.y flag 128 a leaf; ABI), 9.2 Alpha (translucent and thin-cloth micromaps; thin cloth always alpha-tested);
PT-REQUESTS PT-03 and PT-39 states; BACKLOG's lines for them.
