# NRD wipes a metal helmet's relief smooth (PT-95, PT-19): where it is lost, and a trial

Worker `nrdrelief`, 2026-10-09. Copy `wt\nrdrelief`, base `wt\base69`. Brief: `docs/agents/briefs/brief-nrd-relief.md`.

State (15:15): DONE, ready to merge. The trial is written, hidden, off by default. The native side builds; source
checks pass (105); its own small offline check ran once (section 4a). NOT seen in the game: the coordinator's run of
`metal` with the recipe `relief` is its first test there. The plugin's part is written and NOT compiled.

The three lines that matter: (1) NRD is not the one that loses the relief; the tracer's own filter for the
reflections is, and mostly by its FEEDBACK. (2) `Advanced / Reflection relief trial` = 4 turns that feedback off for
plain reflecting surfaces: offline a gently hammered metal keeps 0.87 of its relief in place of 0.53, at 1.4 times
the (small) flicker. (3) The same feedback sits in the direct light's filter, which is why a sunlit wall loses 42 %
of its detail: the next trial, not built.

## 1. The answer in short

- **The relief is not lost in NRD.** NRD here is one denoiser, RELAX for DIFFUSE light in its directional form
  (`nrd_denoiser.cpp`: `RELAX_DIFFUSE_SH`; in: depth, motion, normal and roughness, the bounced diffuse light and its
  direction; out: the same two). It is handed the BOUNCED DIFFUSE light only. The brief's "NRD takes the bounced
  light and the reflections" is not so: the reflections were taken away from it earlier (`pt_nrd_prep.hlsl`'s header:
  under RELAX a bump-mapped floor's reflections flickered three to five times as much).
- **The helmet is pure metal.** Its diffuse colour is black (the run's own Albedo view, `5-look-albedo-view`), and the
  composite multiplies the diffuse light by that colour. So nothing NRD returns reaches the helmet, whatever normal
  NRD is told. The guide normal (`guide_plane`) and `bump_share` are not the cause here.
- **All of the helmet's light is the reflections' signal** (the traced reflection ray's light plus the lamps' and
  the sun's highlights: `pt_di_spatial` and `pt_specular` write one signal). With Denoising "NRD" that signal goes
  through the tracer's OWN filter (`tracer.cpp`, `denoiseOwn`): `pt_temporal` (16 frames), then four `pt_atrous`
  passes with steps 1, 2, 4 and 8: 30 render pixels each way, 60 pixels on the 4K screen (the run rendered
  1924 x 1084). With Denoising "None" the signal is shown as traced. That filter is where the relief goes.
- **Why that filter loses it** (`pt_atrous.hlsl`, the reflections' branch; read from the code, and the offline
  check of section 4a agrees; the game has not been asked yet):
  1. **The feedback.** The second pass writes the first pass's result back into the history, every frame
     (SVGF's feedback; a mirror is already exempt since PT-80). So the history holds the BLURRED picture. Each
     frame's new sample brings one sixteenth of the relief back, and the first pass blurs it again. The filter's
     test of brightness compares neighbours in that smooth history, finds no difference, and stops at nothing.
     This is a state the filter settles in and stays in: the run's NRD frames a minute after loading and at its
     end are the same to two digits.
  2. **The loose test of normals** on anything but a near mirror ("glossy surfaces keep their normals' detail, rough
     ones are filtered across it"): `pow(dot(n, n'), normalPower)`, `normalPower` 32 at roughness 0 down to 4 once the
     two roughnesses add up to 0.8. A bump tilted 10 degrees still counts 0.94 there. And the helmet's dents are
     gentle: about 1.5 degrees from pixel to pixel by the run's own Normals view (a rough reading, the view passes
     the game's tone mapping; the brick wall reads 5 degrees the same way): no test of normals that still lets a
     ROUND helmet be filtered (its own curve is 0.7 degrees a render pixel) can tell such dents apart. So the normals
     alone cannot be the cure for this helmet; the history's own brightness has to carry the relief.
  3. The "picture" guide (PT-80, what the reflected ray met) says nothing of bumps: two dents that both reflect the
     sky, or the same even wall, "show the same thing".

## 2. Numbers (the main tree's run `20261009-103336`, scene `metal`, 16 frames a step, 4K frames; read only)

Two patches inside the helmet: `face` x 1660-1860 y 820-960, `dome` x 1700-1880 y 520-650. "Detail" is the harness's
own measure (the mean picture's Laplacian); "relief" is the mean picture's band between 1.5 and 8 pixels, as a share
of the patch's brightness; "flicker" is how much a pixel changes over the 16 frames (8-bit levels).

| step | patch | brightness | detail | relief % | flicker |
|---|---|---|---|---|---|
| the game | face | 52.0 | 8.20 | 7.4 | 0.11 |
| no denoiser, early (`3-look-none`) | face | 41.2 | 10.51 | 11.4 | 0.61 |
| NRD, early (`2-look-nrd`) | face | 45.1 | 0.59 | 1.5 | 0.08 |
| no denoiser, late (`8-follow-none`) | face | 41.0 | 10.60 | 11.5 | 0.65 |
| NRD, late (`9-follow-nrd`) | face | 45.2 | 0.59 | 1.5 | 0.08 |
| the game | dome | 55.6 | 6.20 | 8.5 | 0.11 |
| no denoiser, early | dome | 52.0 | 9.13 | 9.9 | 0.54 |
| NRD, early | dome | 52.7 | 0.90 | 4.4 | 0.10 |
| no denoiser, late | dome | 51.9 | 9.37 | 10.0 | 0.53 |
| NRD, late | dome | 52.7 | 0.91 | 4.4 | 0.09 |

- With NRD the helmet's face keeps 0.59 of 10.51 of the detail the frame with no denoiser has: 6 %. Of the game's: 7 %.
- Early and late are the same to two digits: nothing "arrives" later.
- The frame with no denoiser flickers only 0.61 levels of 41 on the helmet (1.5 %, after the game's upscaler): this
  signal is not very noisy here. Its relief (11 % of its brightness) is several times larger than its noise. That is
  why the trial's first bit can work: a history of the pixel's own samples has the relief well above its noise.
- The frame with no denoiser has MORE relief than the game (10.5 against 8.2) and is darker on the face (41 against
  52). That is the tracer's own picture, not this job's: noted for PT-95.
- The picture with NRD is brighter than the one with no denoiser on the face (45.1 against 41.2 in 8-bit levels
  after tone mapping; a blurred picture's mean of tone-mapped levels is higher than a contrasty one's, so this is
  not proof of added light).

The scripts: `<scratch>\nrdrelief-work\patch.py` and `normals.py` (they only read the run).

## 3. The trial: `Advanced / Reflection relief trial` (hidden, 0 by default)

A bit each, added up:

| bit | what the reflections' own filter does |
|---|---|
| 0 (all off) | as before: every weight and the feedback unchanged, nothing new is read |
| 4 | the first pass is NOT fed back into the history: a pixel's history is its own samples' mean, as a mirror's already is |
| 1 | two pixels exchange a reflection only as far as their normals gather the same light: the normals' term is the lobe itself |
| 2 | a neighbour counts only as far as it reflects alike (its specular colour): a metal's stains and scratches stay |

The ones to try: **4** (the one that worked offline), **1** alone (to tell the two apart), **5**, **7**. If the run
must be short: the steps with 0, 4 and 5.

**The bits it takes:** `TGPT_Frame.prevCameraPosition`'s w (documented "unused" until now; 0 from every caller),
bits 0 to 2 (values 0 to 7). Nothing else of that word is read; values above 7 are held to 7. No option bit, no
frame constant, no ABI or size change: the tracer hands the number to the four reflection passes as their sixth pass
constant (`pt_atrous` `pass.b.y`; a pass that is not given it reads INVALID, which is "off": the direct light's two
passes and the built-in denoiser's bounced-light passes never see it).

**What 4 does** (`pt_atrous.hlsl`, the feedback's `if`). For a plain reflecting surface the second pass no longer
writes the first pass's result into the history. The history is then the pixel's own 16-frame mean: the relief is in
it, and the filter's test of brightness (which lets through what the noise explains, and stops at more) now has
something to stop at. It is the rule a mirror already has since PT-80 (`shown.picture`), for every plain surface.
No weight changes.

**What 1 does** (`lobe_held`). The weight between a pixel and a neighbour is GGX's distribution about the one normal,
read at the other, over its peak, with the pair's mean roughness: `(a2 / (sin2 + a2 cos2))^2`, `a` = roughness
squared, `a2` = `a` squared. That is what a small light's highlight at the middle of the lobe falls to when the
normal tilts that far. So the test is as wide as the surface's own lobe:

| roughness | 2 degrees | 5 degrees | 10 degrees | 15 degrees | 30 degrees |
|---|---|---|---|---|---|
| 0.2 (glossy) | 0.32 | 0.03 | 0.00 | 0.00 | 0.00 |
| 0.4 | 0.91 | 0.60 | 0.22 | 0.08 | 0.01 |
| 0.6 | 0.98 | 0.91 | 0.69 | 0.48 | 0.14 |
| 0.8 | 1.00 | 0.98 | 0.92 | 0.83 | 0.54 |
| before, at a summed roughness of 0.8 and up | 1.00 | 0.98 | 0.94 | 0.87 | 0.56 |

A rough wall is filtered as widely as before (its lobes overlap, which is true). A glossy bumpy metal keeps each
dent's own reflection. A flat surface with no normal map is filtered exactly as before (the same normal: weight 1).
The same seen from either pixel, so the pair exchanges light and takes none. It is what keeps STRONG bumps and a
glossy thing's highlights; by the table it does little for dents of 2 degrees at roughness 0.4 (0.91), which is
why 4 is the first guess for this helmet and 1 its companion. (NRD's own specular denoisers also test a
neighbour's normal against the lobe's width; said in my words, no NRD source is quoted or copied.)

**What 2 does** (`same_reflecting`): `exp(-10 x the largest difference of a channel of the two specular colours over
the larger one)`: 0.37 at a tenth apart, 0.05 at three tenths. The number 10 is a first guess, not measured.

**Left as they were:** glass, water (their signal is what is seen THROUGH them) and hair with its highlights (its
lobes lie along the strand): `plain_reflector`. Mirrors' "picture" guide (PT-80) still multiplies in.

**Why this and not the brief's other candidates**

- "The normal map in what NRD is handed for the reflections": NRD is handed no reflections, and a metal has no
  diffuse light, so no guide of NRD's can bring the helmet back. (The measured failure of its history was the
  diffuse signal's and the reflections' both, by `pt_nrd_prep`'s header; nothing here reopens it.)
- "The reflection's detail split off before and put back after, as the diffuse light's is": for diffuse light two
  numbers (brightness and a mean direction) tell what a bump takes of it. A reflection's lobe is narrow: what a dent
  shows depends on WHAT lies in its own direction, which no two numbers hold. And `pt_temporal`'s header has a
  measurement against dividing the reflections by their directional albedo (more sparkle on normal-mapped surfaces).
- "A roughness-true lobe so a rough hammered metal is not treated as a mirror": that is bit 1, the other way
  round: the filter treated a glossy bumpy metal as a rough flat one.

**No fake:** nothing is sharpened and nothing is laid over the result. The filter still only averages traced
samples; the trial changes which samples a pixel's history holds and which neighbours count as the same reflection.

## 4. What it costs

- GPU: with the trial on, one more texel read of the material words per tap (24 taps, 4 passes). Off: nothing.
- Noise, bit 4: the feedback was the filter's strongest smoothing (each frame's blur added to the last one's). Without
  it the result is a 16-frame mean filtered once over 30 pixels: more noise where single frames are very noisy (a
  dim room lit by few lamps, a firefly: its pixel keeps it for the history's 16 frames, where the feedback spread it
  into its neighbours at once). On the helmet the frame with no denoiser flickers 0.61 levels, NRD's picture 0.08:
  the trial will land between; where, the game says.
- Noise, bit 1: fewer neighbours count on bumpy and on curved glossy surfaces. On the helmet, by its curvature alone
  (about 0.7 degrees a render pixel), the filter's reach (where the weight is a half) shrinks from 30 pixels to about
  8 at roughness 0.4 and about 3 at 0.25: a glossy round thing is then held by its 16-frame history almost alone.
- What to look at in the run besides the helmet: speck spots and boil over the whole picture against the step with
  the trial off, and the stall's lamp-lit corner on the right.
- Mean light: the weights are normalised per pixel as before, and the new ones are the same from either side. The
  trial adds no light and takes none by itself; a bumpy metal's mean moves toward the undenoised picture's.
- Not touched: the direct light's filter, RELAX and everything it is handed, Ray Reconstruction, Ray Regeneration,
  the debug views' built-in bounced-light filter.

## 4a. The small offline check (once, 15:09; `TGPT_TEST_RELIEF_ONLY=1`, 14 minutes with two other tests on the card)

A metal floor (no diffuse colour, reflectance 0.6, roughness 0.4) under six small lamps, the camera still, the
game's defaults, the built-in denoiser (the reflections' passes are the same ones NRD's mode runs). "Relief kept":
the picture's fine detail against two undenoised means' (1: all of it). "Light": against the undenoised picture's.
"Flicker": a pixel's spread over 32 frames over its mean. Pictures: `%TEMP%\tgb\nrdrelief-images\9x-relief-*.png`
(`relief-sheet.jpg`: undenoised, the trial at 0, 4, 1, 5, side by side).

| the floor | the trial | relief kept | light | flicker (undenoised) |
|---|---|---|---|---|
| gently hammered (bumps up to about 3 degrees, as the helmet's) | 0 | 0.531 | 0.982 | 0.005 (0.253) |
| | **4** | **0.870** | 0.988 | 0.007 |
| | 1 | 0.533 | 0.982 | 0.005 |
| | 5 | 0.877 | 0.988 | 0.007 |
| | 7 | 0.873 | 0.988 | 0.007 |
| the harness's bumps (up to about 30 degrees) | 0 | 0.187 | 0.883 | 0.016 (0.719) |
| | 4 | 0.681 | 0.922 | 0.028 |
| | 1 | 0.293 | 0.905 | 0.017 |
| | **5** | **0.764** | 0.945 | 0.028 |
| | 7 | 0.758 | 0.944 | 0.028 |
| flat (nothing to keep) | 0 | 0.997 | 0.984 | 0.004 (0.228) |
| | 4 | 1.011 | 0.990 | 0.006 |
| | 1 | 0.998 | 0.984 | 0.004 |
| | 5 | 1.005 | 0.989 | 0.006 |
| | 7 | 1.015 | 0.989 | 0.006 |

- **The feedback is the loss.** Without it (4) gentle relief goes from 0.53 to 0.87, strong bumps from 0.19 to 0.68.
- **The lobe (1) alone does nothing for gentle dents** (0.531 to 0.533), as its table said, and a little for strong
  bumps (0.19 to 0.29); with 4 it adds there (0.68 to 0.76).
- **The cost:** 1.4 to 1.75 times the flicker (0.005 to 0.007; 0.016 to 0.028), which is still a thirtieth of the
  undenoised picture's. On a flat metal: 0.004 to 0.006.
- **The light moves toward the undenoised picture's**, never away: on strong bumps the filter as it is keeps 0.883
  of it (PT-75's loss on a metal), the trial at 5 keeps 0.945.
- The reflectance bit (2) changes nothing here (the floor has one colour): its test is the game's.
- This fixture is milder than the game: no camera shift from frame to frame, no upscaler, bumps 12 to 17 pixels
  long where the helmet's are 2 to 4. In the game the filter as it is keeps 0.06 of the helmet's detail, here 0.53.
  So the game's numbers will differ; the direction is what this shows.
- One FAIL line in the output is not this job's: "coloured or white bulk slab retains its diffuse colour ...", a
  check that runs before every single-check mode and fails in the base too (`docs/agents/speed-2.md`, `glints.md`).

## 5. The recipe (in `tests/ingame/scenarios.json`): `relief`

`.\tools\harness\Run-PtHarness.ps1 -Scenario metal -Recipe relief` (the helmet's crop: x 1540 y 440, 460 x 640).

| step | what |
|---|---|
| `1-game` | the game's own picture, 16 frames |
| `2-none` | traced, no denoiser, the trial 0 |
| `3-nrd-off` | NRD, the trial 0: the picture as it is today |
| `4-nrd-history` | NRD, the trial 4: no feedback |
| `5-nrd-lobe` | NRD, the trial 1: the lobe alone |
| `6-nrd-history-lobe` | NRD, the trial 5 |
| `7-nrd-all` | NRD, the trial 7 |
| `8-off` | everything back |

What to read: the helmet's detail (0.59 today against 10.51 with no denoiser; `patch.py` gives the same table for
the new run), its flicker (0.08 today), and the whole picture's speck spots and boil against step 3.
The log says the trial's state once per change: "Reflections' filter on trial (4): ...".

## 6. Non-metals (the brief's item 3): the same kind of loss, in another signal

Measured in the same run (fine detail with NRD over the frame with no denoiser; 4K patches):

| patch | kept with NRD | what it is |
|---|---|---|
| a sunlit brick wall (x 1780-1990 y 270-450) | 0.58 | lit by the sun: its light is the DIRECT signal |
| planks on the ground (x 240-720 y 1270-1390) | 0.70 | |
| a barrel by the stall's lamps (x 3120-3340 y 1220-1420) | 0.74 | |
| the guard's leather pauldron (x 1660-1860 y 1260-1500) | 0.85 | |
| cobbles (x 2100-2500 y 1420-1600) | 0.88 | |
| his scale armour (x 1540-1700 y 1130-1340) | 0.92 | |
| the helmet's face, for scale | 0.06 | |

- **A lit plank's grain, a lit wall:** another signal, the same kind of loss. A non-metal's colour texture is
  multiplied in AFTER every filter, so its colour grain stays (that is why these keep 0.6 to 0.9 where the metal
  keeps 0.06). What goes is the normal map's SHADING in the DIRECT light (the sun's and the lamps'), which NRD is
  not handed either: it goes through `pt_temporal` (16 frames) and two `pt_atrous` passes (steps 1 and 2) with a
  normals' term of `pow(dot, 4)` and the SAME feedback into its history. The sunlit wall, all direct light, loses
  the most (0.58). Its share of reflection is this trial's.
- **A cloak's weave (the night scene):** by the code the same two: its lit side's direct light through that filter,
  its reflections through this one; its bounced light through RELAX with `bump_share` putting the weave's shading
  back (PT-75's work). Not measured here: this run has no cloak; `night` with the recipe `relief` would say how much
  of it this trial alone brings back.
- **Proposed, NOT built** (the brief: nothing more without the game's answer). If bit 4 brings the helmet back, the
  same one line for the direct light's two passes (their feedback off) is the next trial: `pass.b.y` is already
  there for it, as a bit 8 of this switch (the source check `test_path_tracing_review_fixes` pins that dispatch
  line and has to follow). After that, the exact way: for the direct light the normal map's share is known per
  sample (the light's direction is known where it is shaded), so the filter can take the light as the smooth plane
  takes it and each pixel's own share is multiplied back, as `bump_share` does for the bounced light.

## 7. What is not known

- **Which of the two it is ON THE HELMET, the feedback or the loose normals.** Offline it is the feedback (section
  4a). In the game no run has the filter with one of them off: steps 4 and 5 of the recipe are that measurement.
- **How much noise bit 4 leaves in the game's dim, lamp-lit places** (the tavern, the night scene): the offline
  floor is well lit. The feedback was put there for noise; a run of `night` or the tavern with the recipe is the
  test before it could become a default.
- **The helmet's roughness.** No view shows it. The lobe's table depends on it.
- **The dents' tilt**: 1.5 degrees is read through the game's tone mapping from a 4-frame mean; the true figure is
  likely larger (2 to 4 degrees).

## 8. Files changed

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_atrous.hlsl` | `RELIEF_*`, `lobe_held`, `plain_reflector`, `same_reflecting`; the feedback's `if` takes bit 4; the trial's two weights after the weights as before; the header says why |
| `native/pathtracing/src/tracer.cpp` | reads `prevCameraPosition[3]`, says it once in the log ("Reflections' filter on trial ..."), hands it to the four reflection passes |
| `native/pathtracing/include/tgpt_api.h` | the field's comment (no layout change) |
| `src/TGPathTracing/Settings.cs`, `HdrpPasses.cs` | the hidden setting and its way into the frame. **NOT COMPILED** (the game folder holds another build, so the plugin's references do not resolve now) |
| `tests/ingame/scenarios.json` | the recipe `relief` |
| `tests/source_checks.py` | `test_path_tracing_reflection_relief_on_trial_default_off` |
| `tests/pathtracing_gpu_test.cpp` | `reliefTrial` into the frame; the harness's bumps take a scale (`bumpScale`, 1 as before); a small check of its own, `TGPT_TEST_RELIEF_ONLY=1` (a metal floor gently hammered, strongly bumpy, flat: relief kept, light, flicker at 0, 4, 1, 5, 7). Not part of the whole run |

Workarounds met: none removed, none added. One found dead: `pt_nrd_prep`'s kept direction (`g.denoise1.y`, always
INVALID since PT-75; `tracer.cpp` says so itself): still to delete, not this job's.

For the merge: `pt_atrous.hlsl` is CRLF, the others LF, as in the base. `changed.py` in the scratch folder lists
exactly these files against `base69`.

## 9. Builds and checks

- Source checks: `python tests/source_checks.py`: 105 tests, OK (run again after the last edit).
- Native: `%TEMP%\tgb\nrdrelief` (NMake, Release, NRD OFF: a copy's SDK folders are the real ones and NRD's
  configure writes into its SDK, `docs/AGENT-WORKERS.md`). Nothing of this job is in an NRD file: `pt_atrous.hlsl`
  and `tracer.cpp` build the same with and without NRD, and with NRD the four passes are the very same dispatches
  (`denoiseOwn`).
- 14:42: `pt_atrous.hlsl` as it stands compiled by the build's own DXC lines, both forms (`pt_atrous.h`,
  `pt_atrous_dxr12.h`), no message. `tracer.cpp` and `tests/pathtracing_gpu_test.cpp` pass the compiler's syntax
  check with the build's own flags (`cl /Zs`, `/W4` for the tracer: no message from the new lines).
- The shader passes took long: the whole PC shares 6 compiles and a game run held them back for a while
  (`tools\Build-PtShaders.ps1 -Throttle 8`, started 13:57; 30 of 40 headers at 14:42).
- **14:44: the native side builds.** All 40 shader headers compiled, 0 failed; `TGPathTracing.Native.dll` and
  `tgpt_gpu_test.exe` built and installed to `%TEMP%\tgb\nrdrelief-inst` (the log: `%TEMP%\tgb\nrdrelief-build-1.log`,
  no warning from the new lines). READY TO MERGE for the game's run from here on; the small check below is extra.
- 15:09: the job's own small check, once, through the main tree's gate (`Run-PtHarness.ps1 -GpuCheck`, no game run
  going): `TGPT_TEST_RELIEF_ONLY=1`, section 4a; its four "on trial" checks ok. Output:
  `%TEMP%\tgb\nrdrelief-relief.txt`. Never the whole GPU test, no mutation checks (the game has not answered yet).
- 15:27: after the last edit (a comment in `pt_atrous.hlsl` with the offline numbers) the two headers were compiled
  again and the DLL built and installed again (`nrdrelief-build-2.log`: BUILD OK); source checks again: 105, OK. The
  tree as it stands is what was built. The small check was run on the build before that comment.
- `tools\Build-PtShaders.ps1` in this copy differs from `base69`: it is the MAIN tree's version of 15:20, copied in
  from outside this job while it ran (byte for byte the main tree's). Not this job's change; `merge_copy.py` will
  list it.
- NOT done, said plainly: the plugin was not compiled; `pt_nrd_prep.hlsl` is untouched and was not built (NRD off);
  with NRD on the same four dispatches run, but that path's first run is the coordinator's merged build; nothing
  was run in the game.
