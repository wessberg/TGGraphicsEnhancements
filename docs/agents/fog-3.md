> MERGED 2026-10-12 12:20, PART A ONLY (the coordinator): the check's corrected exact value, its pins and the night's
> cases are in the tree (seventeen cases). THE TRIAL OF PART B IS NOT (frame flag bit 27, the hidden setting, the recipe
> `fog-sky`): it is HDRP's shortcut (the sky's probe added to the fog undimmed and unshadowed), and the owner ruled that
> day: "whenever you ask me if we should use HDRP stuff or fake stuff instead of Path Traced stuff, I pick Path Traced
> stuff". Its code stays in the worker's copy (`%TEMP%\tg-agents\wt\fog3`; its build `%TEMP%\tgb\fog3-handover`).

# fog-3: the ground under fog in the fog check (part A), and the night's missing haze (part B) (PT-37, PT-138)

Worker: Claude subagent `fog3`, copy `%TEMP%\tg-agents\wt\fog3`, base `base20q`, brief `docs/agents/briefs/brief-fog-3.md`.
Started 2026-10-12 09:48. Mine outside the copy: `%TEMP%\tgb\fog3-build` (the one build tree and its scripts),
`%TEMP%\tgb\fog3-tools` (desk models, the scripts that made the edits), `%TEMP%\tgb\fog3-hdrp` (HDRP 17's files,
downloaded to read: Unity's `Graphics` repository, branch `6000.0/staging`; nothing of them is in the tree),
`%TEMP%\tgb\fog3-out` (the check's output), `%TEMP%\tgb\fog3-handover` (the binaries handed over).

The coordinator's messages taken in: 10:46 (frame flag bits 25 and 26 are caustgrid's: the trial's flag moved to bit
27) and 11:10 (the owner: "Lamps light the mist ... Anything can light the mist - not just the sun and moon", and
"All bounces": the lamps trial is out of main; the copy's six files that held it are main's own again with this
job's trial on top, and the recipe below names no lamps switch).

## State (last written 11:50; the job is ended here)

(a) PART A IS DONE. The fog check's ground was too bright because of THE CHECK, not the tracer. The check's black
    ground is black by its base colour only and still reflects 4 %: the sun's and the lamp's highlight on it was
    missing from the check's "exact" value. Written into it by hand, the three failing cases read x0.983 to x1.001
    in every region (the furthest off 0.3 %, 0.5 %, 1.7 %); with a ground that reflects nothing the tracer reads the
    old value. The pins are filled (PT-138 (6)). The whole fog check now: 18 cases, every one judged but the mist's
    fixed point, ALL PASS, every picture finite (run `fog3-out\c2-all`, native 49390C624153; 17 cases without the
    trial's).
(b) PART B'S FINDING. The game's blue night haze is NOT the moon's light, it is THE SKY'S. HDRP adds the sky's light
    (its ambient probe) to every point of its fog at full strength: no fog in its way, no shadow. The tracer dims
    the sky's light by the fog above the point, exactly as it (and HDRP) dims the moon. THE FACTOR: the game's haze
    is x8.5 (red) to x19 (blue) of the traced one's in the game's own picture; by hand the tracer's sky share on a
    level ray is a 28th of the sky above (a 56th of HDRP's whole cube map), in the fixture x0.023 to x0.009 of
    HDRP's with the light scattered once and x0.037 to x0.020 with every order. THE TERM: the sky's light in the fog (`sky_radiance(w) * directional_fog(x, w.y)` in `pt_fog_froxel.hlsl`
    and `pt_medium.hlsli`). IT IS NOT A DEFECT OF THE TRACER, IT IS HDRP'S SHORTCUT: a fog 1.66 optical depths thick
    that swallows three quarters of what it meets is dark underneath, and the fixture shows the cells do what
    single scattering says there to 1.4 % (the moon's share to 0.8 %).
(c) THE TRIAL EXISTS IN THE COPY, UNMERGED, BY THE OWNER'S RULE ("whenever you ask me if we should use HDRP stuff or
    fake stuff instead of Path Traced stuff, I pick Path Traced stuff"): the sky undimmed and unshadowed in the fog
    is HDRP's way, the dimmed sky is the traced one. So the choice this report lays out below is made: (1), as
    today. The trial's code (frame flag bit 27, the hidden `Traced fog: the sky lights the mist undimmed`, the
    recipe `fog-sky`, the check's case 17) stays in the copy as it is and is not to be merged; its binaries in
    `%TEMP%\tgb\fog3-handover` were never run in the game. "For the merge", below, names its lines in the files
    part A hands over.
(d) THE ONE REAL GAP FOUND: a directional light's VOLUMETRIC DIMMER is not applied to the light it sends into the
    fog. HDRP multiplies it in (`lightColor.a *= light.volumetricLightDimmer`); the game's moon has 1.23 (a second,
    switched-off sun in the same audit has 1.75), so the traced fog's moon share is x0.813 of HDRP's (the fixture: x0.813 to
    x0.815). It is an artist's knob, not physics: a choice like the others, and small at night (the moon is a tenth
    of the game's haze: 2 % of it). Two smaller ones beside it: the Fog volume's `globalLightProbeDimmer` is not
    sent (1 outdoors, 0.566 in `stairwell`), and the light the ground sends into the fog is not there (PT-37 open 4).

Stopped on the coordinator's word (11:40, the owner: save tokens and GPU). The fixture runs that were still queued
(the fog's later orders at night, the trial on the two daylight fogs: `fog3-out\d1-*`) had run a minute before the
word reached this worker (seven runs of 10 to 14 s, 11:41); nothing was started after it. Their numbers are in "The
fog's later orders at night" below, and they CORRECT what this report said until 11:50: the later orders are not
small at night. Nothing of this job runs any more.

## For the merge: the trial's lines in the files part A hands over

The coordinator takes `tests/pathtracing_fog_froxel_checks.inl`, `tests/pathtracing_fog_froxel_pins.inl`, the fog
check's lines of `tests/source_checks.py`, `docs/TESTING.md`'s and `CHANGELOG.md`'s lines and this report. THE
TRIAL'S LINES IN THEM, to leave out (line numbers of the copy's files):
- `tests/pathtracing_fog_froxel_checks.inl`: line 42 (in the header: `, or not: the case "not dimmed",
  TGPT_FRAME_FOG_SKY_UNDIMMED, on trial`); lines 70-71 (the switch `TGPT_TEST_FOG_SKY_UNDIMMED`); lines 152-154
  (the comment's `trial: ...` and the record's last field `uint32_t trial;`) and the `, 0u` that ends the row of
  "the case asked for"; line 293 (`const uint32_t trial=...`); line 435 (`const bool undimmed=...`); in line 455
  `(undimmed ? above*w.taken : w.skyPart)` becomes `w.skyPart`; in line 466 `|trial` goes. Everything else there
  is part A's or the night's two cases with HDRP's fog by hand (which need no flag: keep them, they are the first
  exact check of the cells in a fog that thins with height).
- `tests/pathtracing_fog_froxel_pins.inl`: line 37 (the case "... not dimmed by the fog above (on trial)") whole;
  the `, 0u` at the end of lines 35 and 36; the second sentence of the comment in lines 33-34.
- `tests/source_checks.py`: lines 6259 to 6261 (the cells' pass factor, the flag's define, the plugin's constant);
  in line 6257 the pinned text becomes `total+=(a*(sunE*sunShare*w.sunPart+s*w.skyPart)+behind)*double(exposure);`;
  the comment's second half in lines 6255-6256.
- `docs/TESTING.md`: in the fog check's paragraph "eighteen cases" is seventeen without the trial's, and the half
  sentence "and the last of them runs the trial `TGPT_FRAME_FOG_SKY_UNDIMMED` (`TGPT_TEST_FOG_SKY_UNDIMMED=1` for
  any case)" goes.
- `CHANGELOG.md`: in the entry's lines 11 to 14 the sentence "ON TRIAL, off unless asked: the hidden ... the harness
  recipe `fog-sky`. No change of the picture without it." goes, and "18 cases" is 17.
NOT taken, the trial's alone: `native/pathtracing/include/tgpt_api.h`, `native/pathtracing/src/fog_froxel.inl`,
`native/pathtracing/shaders/pt_fog_froxel.hlsl`, `src/TGPathTracing/Settings.cs`, `NativeTracer.cs`,
`HdrpPasses.cs`, and the one line `"fog-sky"` of `tests/ingame/scenarios.json`.
Part A's check was run with the trial's lines in it; without them it has not been compiled (they are removals).

## HAND-OVER A (the fog check)

Changed for part A: `tests/pathtracing_fog_froxel_checks.inl`, `tests/source_checks.py` (its pinned lines), and
`docs/TESTING.md`'s paragraph. No shader, no native source.

What the check says now (the whole check, 18 cases, run `fog3-out\c2-all`, the copy's last build, native
49390C624153; main's DLL 18784FDDA14D gave the same three open cases to the last digit but one, `fog3-out\a1-c7`
to `-c9`):

| case | sky / round the lamp | above the horizon | far ground | near ground | the furthest off |
|---|---|---|---|---|---|
| open: sun and sky, the light scattered once | x1.000 | x0.999 | x0.997 | x0.998 | 0.3 % |
| open: a lamp 6 m ahead | x0.997 | x1.000 | x1.001 | x0.995 | 0.5 % |
| open: a lamp 60 m ahead | x0.983 | x0.996 | x0.998 | x0.989 | 1.7 % |
| the night's fog: its moon alone (new) | x1.008 | x1.003 | x1.000 | x1.000 | 0.8 % |
| the night's fog: its sky alone (new) | x1.014 | x1.002 | x0.999 | x0.998 | 1.4 % |
| the night's fog: its sky alone, the trial (new) | x0.999 | x0.998 | x0.998 | x0.998 | 0.2 % |

"fog-only checks passed"; 18 times "fog image finite and nonnegative". The mist's fixed point (printed, not
judged) reads x0.72 to x0.74 as before (PT-137). The room's seven cases and the two daylight fogs are judged against
their pins now: the furthest off 0.0 to 0.3 %, and 1.7 % with the lamp 0.02 m ahead (below).

## Part A: the ground under fog

### What was wrong: the check, not the tracer

The open cases' ground was meant to show "the fog's own light and nothing else": its base colour is 0. A base colour
of 0 takes away the diffuse light only. The material is still a dielectric with the room's smoothness (0.3): F0 0.04,
roughness 0.7. The direct light's pass shades its highlight of the sun (`pt_di_spatial.hlsl`: `g.sunColor.rgb *
through * ggx_specular(...)`) and of the lamp (`pt_rtxdi.hlsli rtxdi_shade`), and the fog dims it on the way to the
camera. `TGPT_OPTION_NO_SPECULAR`, which the check sets, only leaves out the reflection RAYS (`pt_specular.hlsl`), so
the sky makes no highlight; the lights' own highlights stay. At a grazing angle Schlick's Fresnel takes the 4 % to
100 %: that is the lamp 60 m ahead, whose far ground read x3.43.

None of the brief's three suspects on the tracer's side is there (the light's way to the surface not dimmed by the
medium, the surface's own bounced light, the fog in front counted twice): the excess is that highlight to within 2 %.

### Term by term (desk model `%TEMP%\tgb\fog3-tools\model_a.py`, then the GPU)

"known": the check's exact value as it was. "highlight": `E x GGX(n, v, l; F0 0.04, alpha 0.49) x e^(-k way)`, with
the lamp's `E = I / d^2 x e^(-k d)`. "before": the coordinator's run of 09:37. "now": main's DLL 18784FDDA14D with
the mended check (`fog3-out\a1-c7`, `-c8`, `-c9`). "absorber": the same with `TGPT_TEST_FOG_ABSORBER=1`
(`a1-c7abs` ...), held against the value WITHOUT the highlight.

| case | region | known | highlight | before, over known | now, over known + highlight | absorber, over known |
|---|---|---|---|---|---|---|
| sun and sky | sky | 0.14886 | 0 | x1.000 | x0.999 | x0.999 |
| sun and sky | far ground | 0.06574 | 0.00372 | x1.054 | x0.997 | x0.998 |
| sun and sky | near ground | 0.01852 | 0.00451 | x1.241 | x0.998 | x0.999 |
| a lamp 6 m ahead | round the lamp | 1.00064 | 0.00002 | x0.997 | x0.998 | x0.999 |
| a lamp 6 m ahead | near ground | 0.05845 | 0.05475 | x1.926 | x0.995 | x1.000 |
| a lamp 60 m ahead | round the lamp | 1.42238 | 0.25950 | x1.162 | x0.984 | x0.985 |
| a lamp 60 m ahead | far ground | 0.09648 | 0.23560 | x3.434 | x0.997 | x1.032 |
| a lamp 60 m ahead | near ground | 0.00622 | 0.01034 | x2.633 | x0.989 | x0.999 |

Read: with the ground's highlight in the exact value every region is within 1.7 %; with the ground made to reflect
nothing the tracer reads the old value. Both say the same thing.

One thing the absorber shows that is the cells' own (not this job's to mend, PT-138 "far lamps"): the far ground
under the lamp 60 m ahead reads x1.032 of the fog's exact light there, and round the lamp x0.985. Out there a slice
of the cells is 14 m deep (each a quarter deeper than the one before), and the walk reads a cell's light between
cell centres: a way that ends on the ground 5 m in front of the lamp takes some of the bright cell behind it. The
light is moved by a few percent between neighbouring regions, not made.

### What changed in the check

- `groundShine`: the highlight by hand (Schlick's Fresnel with the grazing reflectance min(50 F0, 1), GGX's D,
  Smith's G1 for either side, over 4 n.v). The open cases' exact value takes the ground's highlight of the sun and of
  the lamp, dimmed by the fog on the way back.
- `TGPT_TEST_FOG_ABSORBER=1`: a ground that reflects nothing (a metal of base colour 0), for the listed cases as
  they are.
- The sky a pixel shows BEHIND the fog is the sky HDRP drew (the fixture's raster: 0.55, 0.75, 0.95), not the rays'
  sky (0.5, 0.6, 0.8). No number of the open cases moves (their fog lets nothing through); the night's case found
  it (its sky's region read x1.069 with the rays' sky there, x1.014 with the drawn one).
- How sharp the mended check is (the desk model, what a region would read with a term missing; no mutant was
  built: `pt_common.hlsli` is in every pass). The lamp 60 m ahead holds both of the brief's first suspects: the
  highlight's way BACK to the camera not dimmed: its far ground x1.67, round the lamp x1.16; the lamp's way TO the
  ground not dimmed: its far ground x1.17, its near ground x1.70 (the lamp's light crosses 55 m of fog to get
  there). The lamp 6 m ahead holds neither (x1.05 and x1.03 on its near ground).

### The pins (PT-138 (6)): filled

From the run of the last build that has both fog routes (`%TEMP%\tgb\fogrule2-t5`: trial 5's DLL C05BB7040E17 and
the candidate's test exe; `fog3-out\t5-all`, 11:08, 501 s of which 368 for its passes). A pin is THE PER-PIXEL
ROUTE'S number of that run; "cells then": the cells' in the same run; "cells now": the copy's build today
(`fog3-out\c2-all`). The regions' furthest:

| case | the pin (sky strip, back wall, centre, candle, frame) | cells then, over the pin | cells now, over the pin |
|---|---|---|---|
| no lamp near the camera | 0.02895, 0.04366, 0.03665, 0.04376, 0.06755 | x0.995 to x1.000 | x1.000 |
| a lamp 2 m ahead | 0.06184, 0.11431, 0.25570, 0.15831, 0.10654 | x0.999 to x1.002 | x0.999 to x1.000 |
| a lamp 0.3 m ahead | 0.25148, 0.43289, 0.79449, 0.55943, 0.26464 | x1.002 to x1.006 | x1.000 to x1.003 |
| a lamp 0.02 m ahead | 0.51215, 0.55113, 0.56195, 0.55037, 0.57729 | x1.013 to x1.016 | x1.011 to x1.017 |
| a candle-like emitting quad | 0.02895, 1.02160, 0.03656, 5.05183, 0.18097 | x1.000 to x1.002 | x1.000 to x1.001 |
| no sun, a lamp 2 m ahead | 0.06076, 0.11407, 0.25589, 0.15812, 0.10625 | x0.996 to x1.002 | x0.997 to x1.001 |
| no sun, the emitting quad | 0.02788, 1.02121, 0.03629, 5.05153, 0.18062 | x0.998 to x1.000 | x1.000 |
| the fog scene's fog, scattered once | 0.06610, 0.05018, 0.14353, 0.07206, 0.07848 | x1.000 to x1.002 | x1.000 to x1.002 |
| the vista's fog, scattered once | 0.15563, 0.09970, 0.36088, 0.13235, 0.17684 | x0.999 to x1.002 | x0.999 to x1.001 |

The two daylight fogs WITH the mist's cache are pinned to the cells' own numbers of today (0.14981, 0.12576,
0.16390, 0.07628, 0.12121 and 0.17049, 0.13436, 0.36318, 0.13361, 0.18746; the coordinator's run of 09:37 read the
same six digits; the per-pixel route read them within 1.1 % in trial 5's run). They only hold the mist's light
where it was today.

Two things to know about these pins. (1) The lamp 0.02 m ahead stands 1.7 % from its pin, of the check's 3 %: the
cells read a lamp right at the camera that much brighter than the per-pixel route did; which is right is not known.
(2) If the trial of part B becomes the rule, the four daylight pins must be taken again (their fogs thin with
height: the sky's share changes); the room's do not move (no sky reaches that fog).

Trial 5's run also says once more what part A found: BOTH routes read the open cases' ground x1.24, x1.93 and x3.4
of the old exact value ("the old route over what is known exactly ... near ground x1.241"): two ways of tracing the
fog, the same excess: the ground's highlight, not the fog.

## Part B: the night's haze

### What HDRP does (read in its source, `%TEMP%\tgb\fog3-hdrp`)

`VolumetricLighting.compute`, `FillVolumetricLightingBuffer`, per slice of a froxel column (line 752):

    totalRadiance += transmittance * scattering * (phase * blendValue.rgb + probeRadiance);

- `blendValue.rgb`: the lights. For the directional lights (`EvaluateVoxelLightingDirectional`): every directional
  light whose `volumetricLightDimmer` is above 0, through `EvaluateLight_Directional` (`LightEvaluation.hlsl`), which
  DIMS IT BY THE HEIGHT FOG ABOVE THE POINT (`TransmittanceHeightFog(..., cosZenithAngle, fragmentHeight)`; the line
  `// #define LIGHT_EVALUATION_NO_HEIGHT_FOG` in the compute shader is commented out), times its volumetric dimmer,
  times its shadow map, times `CornetteShanksPhasePartVarying(anisotropy, dot(L, ray.centerDirWS))` and the phase
  function's constant. The tracer's `fog_phase` is that function and `directional_fog` is that dimming.
- `probeRadiance`: `EvaluateVolumetricAmbientProbe(ray.centerDirWS)` times the slice's share. Its own comment: "The
  contribution of the ambient probe does not depend on the position, only on the direction and the length of the
  interval." It is the sky's cube map as 9 spherical harmonics, folded with the phase function
  (`AmbientProbeConvolution.compute`, the kernel `AmbientProbeConvolutionDiffuseVolumetric`: 256 directions over the
  WHOLE sphere, times the Fog's `globalLightProbeDimmer`, `GetCornetteShanksPhaseFunction`). NO FOG DIMS IT AND
  NOTHING SHADOWS IT: a point deep in the fog, or under a roof, gets the open sky's light. (The game has no probe
  volumes: its frame setting is off and no `PROBE_VOLUMES` keyword is set, so this probe is what its fog gets.)
- The lamps: only when the Fog volume's "directional lights only" is off (indoors, the ghost realm).
- Past the grid's last slice (its depth extent: 300 m at night) the "distant fog": `GetFogColor(V, t) x albedo x
  (1 - T)`, `GetFogColor` the tint (0.906, 1, 0.862, x1.226) times the sky's cube map at a blurred mip. THE TINT
  APPLIES THERE ALONE, and no sun at all. At night nothing shows from past 300 m (33 mean free paths).
- "Multiple scattering intensity" (1 at night): not in this compute shader. It is the screen-space blur of the
  picture by the fog's opacity (`docs/agents/fog-2.md` 9): it moves light around, it adds none.
- The night's second directional light, `.../Lights/Sun` (60,511 lux): `affectsVolumetric` false, volumetric dimmer
  0, `affectDiffuse` false, `interactsWithSky` TRUE: it lights the SKY (a Physically Based Sky, with a space
  emission of 18,925 times its texture besides). So the night's sky is as bright as a dim day's.

### The night by hand, a level ray from the camera (desk model `%TEMP%\tgb\fog3-tools\model_b.py`)

The medium (the audit of 2026-10-08): extinction 1/9 per metre under the base (67.56), scale height (184.63 -
67.56) x 0.144765 = 16.95 m, the camera 2.14 m over the base: 0.098 per metre there (a mean free path of 10.2 m), and
THE FOG ABOVE THE CAMERA IS 1.66 OPTICAL DEPTHS THICK straight up (1.88 at the ground). Albedo (0.243, 0.239, 0.208),
anisotropy 0.48. Along a level ray everything is the same at every point, so the fog's light at the end of a long
way is `albedo x S`, S the light a point takes, per unit of scattering:

| term | HDRP | the tracer's cells | the factor |
|---|---|---|---|
| the moon: E x T x phase | 60,511 lux x 1.23 x e^(-1.66 / sin elevation) x phase | the same without the 1.23 | x0.81 (1 / 1.23) |
| its size, red (elevation 35 deg, 90 deg from the view) | 124 nits | 101 nits | |
| the sky, per unit of a uniform sky's radiance | 1 (the whole cube map; 0.5 counting the sky above the horizon alone) | 0.0180 (above the horizon, each direction dimmed by e^(-1.66 / its height)) | x0.018 (1/56), or x0.036 (1/28) of the sky above alone |
| the sky's shadow | none | traced (trees, a roof) | scene by scene |
| the fog boxes' shadow on the moon and the sky | none (HDRP's fog casts no shadow) | traced through them | scene by scene (5 boxes at night) |
| the cube map under the horizon | counted | not counted (below is the ground) | in HDRP's 1 above |
| light scattered more than once | none | the mist's cache | NOT small: in the fixture x2.1 to x2.6 on the moon's share, x1.7 to x2.3 on the sky's (below) |

(The sky's 0.018 is so small because the fog scatters forward: a level ray takes the sky's light from near the
horizon, where the way up through the fog is longest. At anisotropy 0 it would be 0.026, at 0.8 0.007.)

### The same in the fog fixture (the check's cases 15 to 17; run `fog3-out\b3-all`)

The night's medium over a ground that reflects nothing, the moon 40 degrees up at a right angle to the view (or a
uniform sky and no moon), the light scattered once, shown at a hundred times the fixture's exposure. "exact": single
scattering by quadrature (the check's `nightWay`; a second sum in Python, `model_b2.py`, agrees to 0.3 %). "HDRP":
its fog by hand in the same units.

| case | region | the tracer (GPU) | exact | the tracer over exact | HDRP by hand | THE TRACER OVER HDRP |
|---|---|---|---|---|---|---|
| the moon alone | above the horizon | 0.09878 | 0.09849 | x1.003 | 0.12114 | x0.815 |
| the moon alone | far ground | 0.06558 | 0.06561 | x1.000 | 0.08070 | x0.813 |
| the moon alone | near ground | 0.02765 | 0.02766 | x1.000 | 0.03402 | x0.813 |
| the sky alone | above the horizon | 0.08183 | 0.08169 | x1.002 | 3.59417 | x0.023 |
| the sky alone | far ground | 0.04886 | 0.04889 | x0.999 | 3.47896 | x0.014 |
| the sky alone | near ground | 0.01693 | 0.01696 | x0.998 | 1.92709 | x0.009 |
| the sky alone, the trial | above the horizon | 1.93831 | 1.94275 | x0.998 | 3.59417 | x0.539 |
| the sky alone, the trial | far ground | 1.55560 | 1.55831 | x0.998 | 3.47896 | x0.447 |
| the sky alone, the trial | near ground | 0.54329 | 0.54426 | x0.998 | 1.92709 | x0.282 |

(HDRP's sky here is its probe of the fixture's uniform cube map: the whole sphere. Counting the sky above the horizon
alone, the tracer's sky is x0.042, x0.031 and x0.031 of it, and with the trial x0.998.)

So: THE TRACER'S MOON IS EXACTLY HDRP'S WITHOUT THE DIMMER (x0.813 = 1 / 1.23). THE TRACER'S SKY IS 1/43 TO 1/110 OF
HDRP'S, by where the ray goes. And the cells do in this fog what single scattering says, to 1.4 %: it is the first
exact check of the cells in a fog that changes with height (in rays that climb steeply the fog's own light reads
2 % high: the light there doubles within one far slice).

### The fog's later orders at night, and the trial on the daylight fogs (the fixture, runs `fog3-out\d1-*`, 11:41)

The night's fog with the mist's cache (every order), over the light scattered once (the exact value):

| lit by | sky | above the horizon | far ground | near ground |
|---|---|---|---|---|
| the moon alone | x1.51 | x2.14 | x2.37 | x2.59 |
| the sky alone (dimmed: today) | x1.25 | x1.65 | x1.88 | x2.25 |
| the sky alone (the trial) | x1.03 | x1.03 | x1.03 | x1.04 |

So in this fog the later orders are more than the first, though it scatters only a quarter of what it meets: the
moon's light is 3 to 6 times stronger ten and twenty metres up, where the fog above is thinner, and comes down
from there; and the first order toward a camera that looks at a right angle to the moon is the phase function's
weak side (0.030 for a mean of 0.080), where the later orders are taken evenly. With every order the tracer's sky
share is x0.037 (above the horizon), x0.026 and x0.020 (the ground) of HDRP's by hand: a 27th to a 50th, where the
light scattered once gave a 43rd to a 110th. The finding stands; the factor in the game's picture (x8.5 to x19) was
measured with every order in it.

The trial on the check's two daylight fogs (cases 11 to 14), over today's (their pins), for the record:

| case | sky | above the horizon | far ground | near ground |
|---|---|---|---|---|
| the fog scene's fog, scattered once | x1.97 | x2.45 | x1.21 | x1.08 |
| the fog scene's fog, every order | x1.43 | x1.58 | x1.19 | x1.08 |
| the vista's fog, scattered once | x1.09 | x1.31 | x1.01 | x1.00 |
| the vista's fog, every order | x1.08 | x1.23 | x1.01 | x1.00 |

### The same from the game's own picture (run `20261012-093855`, `night`, the haze left of the figure)

Display-linear (the JPG's median, sRGB undone): the game's (0.0116, 0.0152, 0.0252), the traced "sun and moon alone"
(0.0018, 0.0015, 0.0012). Back through a fitted ACES curve and the exposure the harness logged (2^-12.51), the traced
light's gain of 2.3 taken off; a rough reading (no grading, bloom or vignette taken off):

| | red | green | blue |
|---|---|---|---|
| the game's haze, nits | 207 | 247 | 340 |
| ... per unit of scattering (over the albedo) | 853 | 1,032 | 1,637 |
| the traced haze, nits | 24.5 | 21.3 | 17.8 |
| ... per unit of scattering | 101 | 89 | 86 |
| THE GAME'S OVER THE TRACED ONE'S | x8.5 | x11.6 | x19.1 |

The traced haze is the moon's share and next to nothing else (101 red: the hand value for a moon 35 degrees up),
and it is yellowish, as the moon (1, 0.988, 0.843) times the albedo is. The game's haze is blue and 9 to 19 times
that: about 125 nits of it are the moon's (with its dimmer), the other 730 to 1,500 are the sky's probe: a sky of
about that mean radiance all around, which a Physically Based Sky under a 60,000 lux light has.

### THE FACTOR AND THE TERM

- THE FACTOR between HDRP's fog light and the tracer's, for the night's medium on a level ray: about 10 (8.5 in red
  to 19 in blue, from the pictures).
- THE TERM THAT HOLDS IT: THE SKY'S LIGHT IN THE FOG. HDRP gives every point of the fog the sky's light undimmed
  and unshadowed (its ambient probe); the tracer dims it by the fog above the point (`pt_fog_froxel.hlsl
  froxel_direct`, `pt_medium.hlsli fog_light`: `sky_radiance(w) * directional_fog(x, w.y)`). In the fixture: x0.023
  for a ray just over the horizon.
- NOT the moon: its share is the same in both but for the volumetric dimmer (x1.23), and it is a tenth of the
  game's haze. NOT the lamps: with them the torch's glow is added, the haze is still missing.

### Is it the tracer's fault or a choice: a choice, and it is the owner's

- By the medium's own numbers the tracer is right. A fog 1.66 optical depths thick that swallows three quarters of
  what it meets IS dark underneath: the sky's light does not get down to the camera's height. The fog's later
  orders (in the picture: the owner's rule of 11:10) bring more of it down than I first wrote: they more than
  double the traced fog's light at night (the fixture, below), and the game's traced picture has them already: it
  is with them that the traced haze is a tenth of the game's. HDRP's probe lets the sky's light through the fog as
  if the fog were not there, and the game's night was painted with that.
- But the tracer is not of one mind either: its SURFACES take the sky undimmed (a bounce ray that misses reads
  `sky_radiance(dir)` as it is: `pt_gi.hlsl`, `pt_walk.hlsl`, `pt_probe.hlsl`), as HDRP's surfaces do; only its fog
  has the sky dimmed. The design the fog was built from (`docs/agents/fog-2.md`, "Sky: a traced sky ray (an
  overhang shades the mist), times the Fog's GI Dimmer") has no dimming either.
- THE CHOICE: (1) as today: the fog above dims the sky's light on the fog. Physically what the medium says; the
  night's haze stays away, whatever else is done. (2) THE TRIAL: the sky lights the fog as it lights the surfaces,
  undimmed by the height fog, with its traced shadow (which HDRP lacks: under trees and roofs the traced mist
  stays darker, by real occlusion). The night's haze comes back to about half the game's sky share on a level ray
  (the fixture: x0.54; the tracer counts the sky above the horizon, HDRP its whole cube map), less looking down.
  It changes every outdoor scene's fog, by day too: the sky's share of the fog rises x7.4 in the forest and the
  town (their fog is 0.8 optical depths thick above the camera), x1.9 at the vista, x6.9 in the ghost realm. With
  the fog's later orders on top, that light is counted undimmed AND scattered on: brighter than the medium's
  numbers allow. The ghost realm, which reads x1.30 of the game's today, will read more.
- What I would have done: (2), because it is what the game's artists lit their fog with and what our own surfaces
  already do. THE OWNER'S RULE DECIDES IT THE OTHER WAY (the coordinator, 11:40: "Path traced, always"): (1), the
  traced way, stays; the trial is not merged and was not run in the game. What that leaves open is the same
  question for the tracer's SURFACES, which take the sky undimmed by the game's fog today (a bounce ray that misses
  reads the sky as it is): by the same rule the fog above would dim it there too, and the night's surfaces would
  lose their blue ambient light. Named here, not touched.

Not copied from HDRP, named as choices with their numbers:
- the moon's volumetric dimmer: x1.23 on a tenth of the haze, 2 % of the night's haze;
- the cube map under the horizon in the sky's share: up to x2 of the sky's share (the fixture: the trial reads
  x0.54 to x0.28 of HDRP's with it). HDRP counts what its sky draws below the horizon; in the tracer what is below
  is the ground, and the light the ground sends into the fog is a gap (PT-37 open 4);
- the Fog volume's `globalLightProbeDimmer` (1 at night and in every outdoor scene; 0.566 in `stairwell`): the
  plugin does not send it (the design named it). A gap, small today;
- HDRP's tint: past its grid's 300 m alone, nothing at night;
- the fog boxes' shadow on the sun, the moon and the sky: the tracer's alone, and right.

### The trial (for the game)

- Frame flag bit 27, `TGPT_FRAME_FOG_SKY_UNDIMMED`; the plugin's hidden `Traced fog: the sky lights the mist
  undimmed` (Advanced; off: as before). In the cells' pass the sky's dimming is multiplied out by a factor (`lerp(
  directional_fog(x, skyDir.y), 1.0, bit)`): no branch round the sun's or the lamps' code, the two pinned lines as
  they were, and the check's no-sun cases stay finite (18 of 18).
- NOT covered by it: the fog a reflected or refracted ray crosses and what feeds the mist's cache (`pt_medium.hlsli
  fog_light`: in every pass). So with the trial on the fog's later orders still start from the dimmed sky. If the
  owner chooses (2): that one factor in `fog_light`, every pass compiled once, and the trial becomes the rule.
- The recipe `fog-sky` (`tests/ingame/scenarios.json`, before `traced-fog`): the game's; the sky dimmed (today);
  undimmed; undimmed with the fog's light scattered once; dimmed and once; undimmed under NRD; off. For `night`
  first, then `fog`, `forest`, `vista`.
- What to read: `night`'s haze left of the figure (the game's display-linear 0.0116, 0.0152, 0.0252; today's
  0.0018, 0.0015, 0.0012) and its luma (the game 38.7); `fog`'s luma (the game 70.2, today 91.4).

## HAND-OVER B

`%TEMP%\tgb\fog3-handover` (built from the copy at 11:16; the whole fog check passed on it at 11:20):
- `TGPathTracing.Native.dll`, SHA-256 `49390C624153FD9CC4F52602C55BAA3CFBCC4C1ED6606A9AF90C39C7DFD9EF82`
- `TGPathTracing.dll` (the plugin: it has the hidden setting), SHA-256
  `2723AABBAB85F0B0DBACB53ACEEA76E7F28F288B5571C798037FAD4E759B1E23`
- `tgpt_gpu_test.exe` (the fog check with its pins).
THE TWO GO TOGETHER: the DLL is `base20q`'s sources (main of about 09:45) with the six fog files as main has them
since 11:10 and the trial on top; it does not have what main merged after the base (the water's groups, the sun
cell's change). For a look at the trial alone that is enough: both steps of the recipe are the same DLL. With the
trial off this DLL's fog is main's fog (the check's numbers are the same to the last digit but one). The cleaner
way is to merge the copy and build main (the cells' pass alone compiles: seconds).
What to run: `night@fog-sky` first (then `fog@fog-sky`, `forest`, `vista`): the recipe's steps are the game's own,
the sky dimmed (today), undimmed, undimmed with the fog's light scattered once, dimmed and once, undimmed under
NRD.

## Switches, log lines, files (against `base20q`)

- `tests/pathtracing_fog_froxel_checks.inl`: part A's `groundShine`, `TGPT_TEST_FOG_ABSORBER=1` and the drawn sky
  behind the fog; part B's place 6 "the night's fog" (the medium above, a ground that reflects nothing, the moon 40
  degrees up at a right angle to the view, or a uniform sky; shown at a hundred times the exposure), its exact value
  by quadrature (`heightOver`, `heightBack`, `skyDimmedTable`, `nightWay`), HDRP's fog by hand printed beside it,
  `TGPT_TEST_FOG_SKY_UNDIMMED=1`, `TGPT_TEST_FOG_PLACE=6`; a case's record has one more word (`trial`).
- `tests/pathtracing_fog_froxel_pins.inl`: three cases more (15 the night's moon alone, 16 its sky alone, 17 its sky
  alone with the trial).
- `tests/source_checks.py`: `test_path_tracing_fog_is_traced_in_cells` follows (the judged line; the highlight, the
  absorber, the night's exact value and HDRP's, the trial's factor and flag). 120 checks pass.
- `native/pathtracing/include/tgpt_api.h`: `TGPT_FRAME_FOG_SKY_UNDIMMED` (frame flag bit 27).
- `native/pathtracing/src/fog_froxel.inl`: the cells' pass constant's bit 1.
- `native/pathtracing/shaders/pt_fog_froxel.hlsl`: the sky's dimming times a factor that the bit makes 1.
- `src/TGPathTracing/Settings.cs`, `NativeTracer.cs`, `HdrpPasses.cs`: the hidden `Traced fog: the sky lights the
  mist undimmed` (Advanced, off), the flag.
  (These six files are MAIN'S OF 11:10 with the lines above added: the lamps trial is out of them as it is out of
  main. Against `base20q` they therefore also show main's removal; it is the same text as main's.)
- `tests/ingame/scenarios.json`: one line, the recipe `fog-sky`, before `traced-fog`. Nothing else differs from
  the base (the base's `fog-lamps` line is left for main's removal to merge by itself).
- `docs/TESTING.md` (the fog check's paragraph), `CHANGELOG.md` (one entry at the top).
- No log line added. No export, record or FrameConstants row.

## Not done

- No mutant was built (the picture's verdict comes first; the terms part A's check holds are in `pt_common.hlsli`,
  which is in every pass). What each missing term would read is reckoned in the desk model, above.
- The trial is not in `pt_medium.hlsli` (reflected rays' fog, the mist's cache's feed).
- The game has not been run with it; the owner has not seen it.

## State when stopped

Ended in order at 11:50 on the coordinator's word, not by a limit. Nothing running, nothing queued
(`fog3-build\STOP` is set). The copy builds; 120 source checks pass; the whole fog check passed on its last build.
