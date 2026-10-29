# Highlights held: the traced light brightened where it is dark, kept where it is bright (PT-73 e, the lantern's "option 4b")

Worker: Claude, 2026-10-08, copy `wt\highlights` (base `wt\base48`). Brief: `docs/agents/briefs/brief-highlights.md`.

**State (04:30): BUILT, ITS OWN CHECKS PASS, THE WHOLE GPU TEST IS RUNNING.** The source checks pass (94 of 94), the
plugin compiles, the native build is done, the new offline check passes on both routes and fails on the untouched
base, and at a gain of 1 the picture is bit for bit the base's. Not yet done: the whole GPU test's result. Not seen in
game (never deployed). This file is kept current; "Checks and results" says what is built and what passed.

## What changed, and where

| file | change |
|---|---|
| `native/pathtracing/shaders/pt_common.hlsli` | after `fog_gain()`: `HIGHLIGHT_KEPT`, `highlight_hold(lit, gain)`, `traced_ungained(shown, gain)` |
| `native/pathtracing/shaders/pt_compose.hlsl` | one line before the lit outputs: `exStored *= highlight_hold(...)` |
| `native/pathtracing/shaders/pt_meter.hlsl` | the meter's luminance through `traced_ungained` |
| `native/pathtracing/shaders/d3d11.hlsl` | TGPT_FOG: its copy of `HIGHLIGHT_KEPT` and `traced_ungained`, used for the histogram's traced light |
| `native/pathtracing/src/tracer.cpp` | one line: `f.constants.fog[3]=player;` (the fog pass's constants; the field was unused) |
| `src/TGPathTracing/HdrpPasses.cs` | the log line's words ("x5.3 where it is dark, less where bright"), a comment; `AutomaticGainInverse` (the dialogue light, see its section). The gain's own target is untouched |
| `src/TGPathTracing/Settings.cs` | a comment. The tooltip is unchanged: it is true now |
| `tests/pathtracing_gpu_test.cpp` | the old gain check moved to a dark exposure; a new block after it (the curve, pixel by pixel) |
| `tests/source_checks.py` | the meter's pinned line follows the change; the new lines pinned |
| `docs/TESTING.md` | one sentence in the in-game plan (step 11) |

| `CHANGELOG.md` | one entry, above "TG Path Tracing, PT-71 item 6" (not at the top, where another session writes) |

Not touched: the coordinator's files, NRD's and RTXDI's code, every export, struct and option bit.

## The short version

- Today the tracer multiplies its whole picture by one automatic gain (5.3 in the tavern). That lifts the dark room, and
  it lifts the wall beside the lantern just as much, so the wall burns to white.
- New: each pixel's gain depends on how bright that pixel already is. Dark pixels get the whole gain. A pixel at a
  set brightness W gets none. Above W nothing changes: the picture is the game's exposure there, never darker.
- It is how Dark Adaptation works from now on. No new row, no option bit, no export, no struct change.

## The curve

x = L / W, where L is the pixel's traced light (its luminance) as the game's exposure and the player's Exposure row
show it, without the automatic gain. G is the automatic gain at that pixel (the same one as before: it still fades
behind HDRP's fog). b = G - 1.

    below W:  L' = W - (W - L) / (1 + b x (1 - x))
    from W:   L' = L

The same thing written as a gain: k = L' / L = 1 + b (1 - x)^2 / (1 + b x (1 - x)), and k = 1 from W up. The colour is
scaled by k / G on top of the old gain, so there is no hue shift.

**How it differs from the brief's formula, and why (the join).** The brief's curve is
L' = L G / (1 + L (G - 1) / W), which is the same as W - (W - L) / (1 + b x). At W it meets the line L' = L with a
kink: its slope there is 1 / G (0.19 at G 5.3) and the line's is 1. On a wall whose light falls off smoothly through W
that kink is a visible ring. No curve can keep the brief's formula all the way up and also arrive at W with slope 1,
so something has to give near W. What I chose: one term changed, `b x` to `b x (1 - x)`. That gives:

| property | holds |
|---|---|
| dark pixels get G | yes: slope G at 0 (k = G - (G^2 - 1) x for small x) |
| a pixel at W is unchanged | yes, exactly |
| above W: the game's exposure, never darker | yes, exactly (k = 1) |
| smooth at W | value and slope both match the line (slope 1); only the curvature jumps |
| order of brightness kept | yes for every G: the slope is (1 + b (1 - x)^2) / (1 + b x (1 - x))^2, always above 0 |
| never below L | yes: L' - L = b x (1 - x)^2 W / (1 + b x (1 - x)), never negative |
| G = 1 | k is exactly 1; the shader also leaves early, so the pixel is multiplied by exactly 1.0 |
| no tuning number besides W | yes |

The price: between the dark end and W it is a little darker than the brief's formula. At G 5.3: 3 % at x 0.05, 5 % at
0.1, 9 % at 0.3, 10 % at 0.5, 9 % at 0.7, 5 % at 0.9, 0 at 1. That is at most 0.15 of a stop, in the range the wall near
the lamp sits in. Other joins I worked through and dropped: a blend of the two curves over the last half of the range
(not monotonic at G 5.3: the picture's order of brightness would flip in a band), a cubic bridge (exists only up to
G 9), a subtracted term (fails from G 16). The automatic gain can go far above that (up to 7 stops in a dark cave).

One thing to know: the higher G is, the flatter the upper mid tones get and the shorter the last stretch into W. At
G 16 everything between 0.2 W and 0.9 W lands between 0.77 W and 0.96 W. That comes with "nothing lifted past W" at a
high gain, in the brief's formula too. Worth one look in a very dark scene (a cave).

## W, and how its scale was fixed

`HIGHLIGHT_KEPT = 0.54` in `pt_common.hlsli` (one constant; a copy in `d3d11.hlsl` for the fog pass, pinned to it by a
source check).

- The units: what the composite stores and HDRP's colour buffer holds: radiance times HDRP's exposure ("pre-exposed").
  The game's tone mapping reads exactly that.
- The game's tone mapping is HDRP's ACES, full (the game's own log line, `BepInEx/LogOutput.log`: "tonemapping ACES
  (full ACES)", in a tavern at fixed EV 14 and outdoors alike). It never reaches white at a fixed value: 0.18 shows at
  0.10 of the screen's white (linear light), 1.0 at 0.62, 1.4 at 0.72, about 16 at 1.0. (Worked out with the published
  fit of ACES's two transforms, the one the coordinator's `aces.py` uses. Not copied from Unity.)
- The preview the owner picked (4b) worked on the screen's picture, with W = 0.4 of the screen's white in linear light
  (screen level 168 of 255). The pre-exposed value ACES shows at 0.40 is **0.54**. So W is the brightness the game's
  tone mapping shows at 0.4 of white.
- The other reading of the brief's words ("0.4 of what the tone mapping shows as white" as 0.4 times a white input)
  does not work with ACES: its white input is about 16, and 0.4 of that shows at 0.95 of white, so nothing would be
  held. If "white" is taken as 1.0 (as `HdrpPasses.cs`'s "1.4 times white" does), W would be 0.4, screen level 155: a
  little firmer. One number to change if the owner wants the wall darker or lighter.
- Honest note: the preview was made from screen pictures with a plain 2.2 curve, and it also darkened pixels above W
  (the correction "never below L" removes that). For mid-dark pixels the preview behaves like a W near 0.85 in these
  units, for the pixel that is left alone like 0.54. One W cannot match both. I took the one that sets how bright the
  wall may get, since that is what 4b was chosen for. Expect the mid tones about 10 screen levels under the preview.

## Where the gain is used, and what each does now

| place | before | now |
|---|---|---|
| `pt_compose.hlsl`, the lit outputs (own-ray surfaces, G-buffer surfaces, glass over the sky) | light x `exStored` (exposure x reference x gain) | the same, times `highlight_hold(luminance of the pixel's traced light at the plain gain, the gain)`. Diffuse and specular take the same factor. A surface's own emission stays at the game's exposure, outside the curve, as before |
| `pt_compose.hlsl`, the debug views (Specular, Indirect, Direct) | plain gain | unchanged: each shows one part of the light, and the hold is the whole pixel's |
| `pt_meter.hlsl` | luminance / gain | `traced_ungained(luminance, gain)`: the curve solved backwards (a quadratic, exact), so the meter still reports the light at the game's exposure without the gain. The managed side's target (median to a mid tone, the brightest fiftieth at most 1.4) is untouched, so G stays what it was (5.3 in the tavern) |
| `d3d11.hlsl` TGPT_FOG, the histogram behind Fog Adaptation's own gain | traced = seen / gain | the same inverse, so the fog's gain comes out as before. Needs the player's Exposure factor: `FogConstants.fog[3]`, which was unused (no layout change) |
| `d3d11.hlsl` TGPT_FOG, the fog's own light | x the fog's gain | **unchanged, not held** (see "Not done") |
| `pt_common.hlsli` `fogged()` (a reflection's own way through fog) | ratios of `traced_gain_through` | unchanged: they move light between the ray's gain and the pixel's before the hold, which then acts on the pixel's sum |
| `pt_water.hlsl`, `pt_water_under.hlsl` | plain gain | **unchanged, not held** (see "Not done") |
| `HdrpPasses.cs` seen meter (`seenLog2Mid - seenLog2Gain`) | a lower bound of the view's light | still a lower bound (a held pixel divided by G is under its light); the traced meter, which is exact, wins the `max` |
| `HdrpPasses.cs` log line | "traced light x5.3" | says the gain is the dark parts' |
| `SceneLights.ApplyGain` (lights HDRP still shades with get the gain) | x gain | unchanged: see "Not done" |
| `HdrpPasses.AutomaticGainInverse` (the dialogue light of Conversation Lighting Overhaul, PT-17: traced at 1 / gain so the gain brings it back to the mod's brightness) | 1 / G | **changed: the curve's inverse at a mid tone.** See the next section: a decision for the coordinator |

## The dialogue light (PT-17): found on the way, changed, please decide

The dialogue light is traced dimmer by 1 / G so that the gain, multiplying it back, shows the face as bright as the
mod set it. That only works with one gain for every pixel. With the hold, a face lit to a mid tone gets less than G
back, and 1 / G would leave it too dark. No single factor is exact any more (it would need that light's share of each
pixel kept apart). The three choices, as shown / intended at G 3.1 (a tavern), by how bright the mod means the face:

| the face as the mod means it (pre-exposed; screen level) | 1 / G, as it was | the mid-tone factor (built) | no factor |
|---|---|---|---|
| 0.05 (level 31) | 0.92 | 1.27 | 2.47 |
| 0.09 (53) | 0.87 | 1.17 | 2.13 |
| 0.18 (91) | 0.77 | 1.00 | 1.64 |
| 0.36 (141) | 0.63 | 0.78 | 1.16 |
| 0.50 (165) | 0.55 | 0.67 | 1.01 |
| 0.80 (195) | 0.44 | 0.53 | 1.00 |

Built: `AutomaticGainInverse` is now the factor that is exact for a mid tone (0.18, a third of W; the same quadratic as
the meter's). It is a guess that a lit face is a mid tone. If dialogue faces are meant bright (0.5 and up), "no
factor" is the right one and the special case can go (the hold itself then keeps the face from being brightened). One
line either way (`HdrpPasses.cs`, `AutomaticGainInverse`). Nobody has looked at a dialogue since the scaling went in
(PT-17's state), and the harness has no dialogue scene, so this is untested in game, as before.

## Denoisers

- NRD: the curve is in `pt_compose`, after NRD. Nothing NRD takes in or keeps changed (`pt_nrd_prep.hlsl`,
  `nrd_denoiser.cpp` untouched): its input stays linear in light.
- Ray Reconstruction and None: these denoise (or not) after the composite, so they get the light with the curve on
  each raw sample. A curve that bends over noisy samples averages a little darker than the same curve over the clean
  picture (a dark pixel whose few samples are bright gets less lift). So in noisy dark places the None picture's mean
  and RR's picture will sit a bit under NRD's. Measured offline: 1 % under over the whole picture, 3 % in its dark
  part (see "Checks and results"); a noisier game scene will show more. To remove that, the hold would need a steady
  brightness for each pixel (a small history of its luminance); not built: a new texture and reprojection, more than
  this job.
- **What touches the NRD path:** only that `pt_compose`'s last lines run after it (the branch with `pass.a.z` valid,
  which a build with `-DTGPT_WITH_NRD=OFF` never runs). Test in the main build with NRD: the lantern wall, and that the
  picture's brightness matches None's on a still frame.

## Not done (gaps, named)

1. **Water**: the tracer's water (reflection, the light from under it, its own diffuse light) still takes the whole
   gain. The three parts are put together in D3D11 (`TGPT_WATER_SHADE`: Fresnel, foam, absorption), and the hold must
   act on that sum; that shader does not know the pixel's gain. Needs the gain per pixel there (fog constants and the
   gain in its constant buffer). A lantern's reflection in a lake is brightened as before.
2. **HDRP's fog light** (Fog Adaptation): still times the fog's gain, not held. Bright mist round a lamp can still
   burn out. The same curve fits there in two lines, but it changes three settled checks' numbers (PT-48) and what
   the owner saw of fog: a decision, not a detail.
3. **What HDRP still shades** with the gained lights (`SceneLights.ApplyGain`: hair up close, glass, eyes on the old
   route): the whole gain. Next to a held wall such a surface is brighter than its surroundings.
4. **None and Ray Reconstruction** get the curve per raw sample (above).
5. `tools/harness/mirror.py` `step_exposure` still takes the gain as one factor for the whole picture.

## Checks and results

Four separate claims: built; passes offline; seen in game (no: never deployed); the owner says it is right (no).

**Built.** Native: `%TEMP%\tgb\highlights-build` (Ninja, `-DTGPT_WITH_NRD=OFF`, targets `TGPathTracing.Native` and
`tgpt_gpu_test`; DLL FAF6739AF466, test 7234A5A9BBC4). The untouched base48 the same way into
`%TEMP%\tgb\highlights-pristine` (DLL 017FA90456D1). The plugin compiles in the copy (`dotnet build`, no deploy, 0
warnings). Scripts: `%TEMP%\tgb\highlights-build.cmd`, `highlights-gpu.ps1` (one waiting loop inside it).

**Source checks:** 94 of 94 pass (`python tests/source_checks.py`).

**What the two builds' compiled shaders differ in:** `pt_compose` (both shader models) and `pt_meter`. The other 40
of the 43 files the shader build writes are byte for byte the same (the new functions are in the shared include;
unused, they leave no trace). So nothing else in the tracer can have changed: not the direct light, the bounced light, reflections, water.

**The new offline check** (`tests/pathtracing_gpu_test.cpp`, right after the old gain check; it runs in the first part
of the test, `TGPT_TEST_SURFACES_ONLY=1`, some 90 s). The picture at a gain of 5 against the same frames at 1, pixel by
pixel, at an exposure that puts the ceiling's underside at W, on both routes. Its lines at 04:22:

| | G-buffer surfaces | the tracer's own camera rays |
|---|---|---|
| surface pixels | 894,362 | 894,362 |
| off the curve by more than 2 % | 0 | 0 |
| darker than at gain 1 | 0 | 0 |
| at W (within 2 %): gain 5 over gain 1 | 11,643 pixels: x1.0002 | 11,554: x1.0003 |
| above W: more than 1 % off x1 | 0 of 220,383 (the most 0.24 %) | 0 of 221,299 (the most 0.68 %) |
| under half of W: the least gain | x1.499 (281,918 pixels) | x1.498 (280,100) |
| out of order (a brighter pixel shown darker) | 0 | 0 |
| of another colour than the curve gives | 28 | 14 |
| the emissive panel's 3,293 pixels: off | 0 | 0 |
| in a dark picture (exposure 2000 times lower): the ceiling | x4.988 | x4.988 |
| the meter, median and 98th percentile (log2), at gain 1 / at 5 | -1.43, 0.96 / -1.43, 0.96 | -1.41, 1.11 / -1.41, 1.11 |

The old check ("the traced light's gain scales the traced light", 4 against 1) now runs at an exposure 1000 times
lower, where the gain is whole: x3.996. It passes.

**The same test on the untouched base's DLL fails, as it must:** 894,362 of 894,362 pixels off the curve, x4.9997 at W,
every pixel above W five times brighter. So the check sees the hold being absent (this came for free with the
comparison below; no mutants were run, as the brief says).

**G = 1, bit for bit.** `TGPT_TEST_GAIN_DUMP=1` writes the picture at a gain of 1 as raw floats. The change against
the untouched base (`%TEMP%\tgb\highlights-compare.py`):
- G-buffer surfaces: **0 of 2,764,800 floats differ in their bits.**
- Own camera rays: 64 floats differ (0.002 %, the most 0.00006). That route is not the same from run to run by
  itself: two runs of one build differed in 6 % of the floats (the most 0.0015) when the frames before them differed.
  So: no change seen, within what that route varies by on its own.
- By construction: `highlight_hold` returns exactly 1.0 when the automatic part is not above 1, and the meter's
  `traced_ungained` is then the old division.

**Raw samples (None, Ray Reconstruction), measured, not a check:** 48 undenoised frames' mean at gain 5 over the curve
on the same frames' mean at gain 1: 0.991 over the pixels under W, 0.970 over those under a quarter of W. So in this
scene the mean of None's frames sits 1 % under the clean picture, 3 % in the dark part. A game scene with more noise
will show more.

**The fog pass** compiles with fxc (`/T cs_5_0 /D TGPT_FOG`); the tracer compiles it at run time, and the whole test's
Fog Adaptation checks run it.

**The whole GPU test, once:** (running; its FAIL lines go here)

**Not run:** NRD (this build has none), Ray Reconstruction, the game. No mutation checks (as briefed).

**How the first GPU runs were lost, for the next worker:** after new shaders the driver compiles the ray generation
pipeline `specularSer` for about 200 s, the test waits 180 s for the passes and then ends (two FAIL lines, exit at
"scene shaders compile"), and the driver keeps nothing of a compile the run ended before. So "run it once more" never
helps. What worked: one run that only starts up and waits longer (a copy of the test built with a 15-minute wait,
`%TEMP%\tgb\highlights-warm`, with `TGPT_TEST_STARTUP_ONLY=1`), then the real run: 5 s to start. The test's own wait is
pinned by a source check, so I did not change it; the coordinator may want it longer than the compile.

## What to expect in game

A PREDICTION, not a render: worked out from the harness's two real pictures of the frozen lantern frame (run
`20261008-001912`, `glass-jars`: gain 1 and "as now"), each pixel split into what takes the gain and what does not,
the built curve put on the first, ACES back on top (`%TEMP%\tgb\highlights-predict.py`; the game's bloom, vignette and
grading are not modelled; the model's own "as now" comes out at 25.6 against the real 25.3). Pictures:
`%TEMP%\tgb\highlights-predict\predicted-W0.54.jpg` and `lamp-gain1-asnow-predicted-W0.54.jpg` (gain 1, as now,
predicted, side by side).

| mean screen level (0 to 255) | whole picture | the room (left) | round the lamp | of that at 250 and over | at 200 and over |
|---|---|---|---|---|---|
| the game | 14.3 | 6.4 | 26.5 | 0.00 % | 0.02 % |
| gain 1 (option 1) | 5.2 | 1.7 | 20.6 | 0.01 % | 1.6 % |
| as now, one gain of 5.25 | 25.3 | 18.3 | 61.0 | 1.33 % | 17.3 % |
| **predicted with this change** | **21.9** | **17.2** | **47.1** | **0.01 %** | **5.2 %** |
| (the brief's formula with its kink, for scale) | 22.3 | 17.4 | 48.6 | 0.01 % | 7.7 % |

So in `glass-jars@lantern-choice`, step `1-as-now`:
- **The wall beside the lantern**: wood grain and a warm orange, no white patch. Nothing that the gain lifted goes past
  W (screen level about 168 for grey; an orange wall's red channel reaches about 200). Only what is that bright at the
  game's own exposure stays above it: the flame, the lamp's glass, the few centimetres of table under it (about 1 % of
  the pixels round the lamp).
- **The room**: nearly as bright as now (about 6 % under: 17 against 18).
- **The picture's luma against the game's**: about 22 where it is 24.8 now and the game's is 13.9. The log line still
  says x5.3 (the gain itself is not changed; it now reads "x5.3 where it is dark, less where bright").
- **No ring** on the wall where the brightening ends. If one shows, it is this job's defect (the join).
- `candle@quick`: the same in small: the table at the flame keeps its colour, the room's lift stays.
- `vista@quick` (daylight): the gain's automatic part is 1 there, so the picture must be the same as before this
  change. If its luma moves, look at the log's gain: it was not 1.
- With NRD the picture is the curve on clean light. With None the mean of the frames can sit a little under NRD's in
  noisy dark corners (see "Denoisers").
