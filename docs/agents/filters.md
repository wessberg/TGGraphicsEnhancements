# Filters: keep the light, keep a mirror's picture (PT-75 finished, PT-80 a) - agent notes, 2026-10-08

## State (14:20) - read this first

- STOPPED BEFORE THE GPU TESTS, on the coordinator's order at 14:10 (the owner needs the graphics card). All my queues
  and test processes are stopped; nothing of mine starts a GPU test by itself.
- BUILT: all fixes, with NRD (`%TEMP%\tgb\filters-build`, NRD's options as the main tree's `build\native`). The copy's
  source is consistent and compiles; source checks pass (95). The last build (14:20, every pass compiled again) is
  staged in `%TEMP%\tgb\filters-rt`: the DLL's SHA-256 begins `CF0105B2B7A7`, the test's `046920480809`.
- MEASURED ON THE GPU: only the first part of the mirror check, on the unchanged tracer and on two earlier versions
  of my filter, from runs that were killed half way ("First numbers" below). They reproduce PT-80 and showed that my
  first mirror filter was wrong; it is replaced.
- NOT MEASURED: the code as it stands now has never run on the GPU. Not the mirror check, not the mean check, not the
  noise study, not the whole GPU test. Everything below marked "expected" is from the earlier agent's runs, from
  arithmetic, or from two small models on the CPU. Nothing here is "passes the harness" or "seen in game".
- OWED, in this order: see "Tests still owed, with their command lines".

Copy: `...\scratchpad\wt\filters` (base `base54`, ABI 0x00012600). No export, struct, option bit or ABI change. Two
frame-constant slots that held INVALID are used: `material.z` (the reflections' guide texture) and `denoise1.y` (NRD's
kept direction).

## In plain words

Three things took light away or moved it, and one smeared mirrors:

1. The tracer's own blur filter (the "a-trous" pass) was unfair between a bright pixel and its dim neighbours. The
   bright one took their light, they refused its own. Light vanished. Fixed so that what one pixel gives is exactly
   what the other takes.
2. NRD's "anti-firefly" switch cuts rare bright samples. In dim places those samples ARE the light. Turned off.
3. NRD and normal maps: the step that puts a normal map's shading back after NRD lost 4 to 5 % on every bumpy
   surface. Rebuilt so that the same factor is divided out before NRD and multiplied back after.
4. Mirrors: the reflections' filter treated a mirror's picture as noise on a flat surface and blurred it. Now a
   pixel only shares light with neighbours that show the same thing in the mirror.

## What the earlier agent's unread runs say (`%TEMP%\tgb\nrdmean-out`, base52)

Light as a share of the undenoised mean, R G B, "the sky through a slot", settled, without reflections:

| run | what | brown floor | walls |
|---|---|---|---|
| `s-base` | as shipped | 0.908 0.920 0.956 | 0.914 0.908 0.892 |
| `s-firefly0` | RELAX's anti-firefly filter off | 0.957 0.966 0.980 | 0.959 0.956 0.937 |
| `s-linear` | all four non-linear RELAX steps off | 0.963 0.971 0.986 | 0.967 0.964 0.948 |
| `s-flat` | as shipped, NO normal maps | 0.955 0.963 0.985 | 0.966 0.973 0.990 |
| `s-plain` | as shipped, RELAX's plain (not SH) form | 0.954 0.967 1.004 | 0.949 0.943 0.925 |
| `s-diag2` | as shipped, the normal map's shading NOT put back | 0.954 0.966 1.004 | 0.949 0.943 0.924 |
| `s-units4`, `s-units025` | NRD's units x4, x0.25 | 0.908 0.919 0.955 / 0.909 0.920 0.956 | as shipped |
| `s-hist1` | a history of one frame | 0.974 0.981 0.998 | 0.973 0.968 0.946 |

- `s-linear-flat` is VOID (16 unrelated FAIL lines, "settled?" 0.44 to 0.92: it shared the GPU badly). Still owed
  (below: `flat`).
- The third cause is settled by `s-flat`, `s-plain` and `s-diag2` together: with the anti-firefly filter on, the
  picture holds 0.954 when the normal map's shading is NOT put back, 0.955 with no normal maps, 0.908 with the shading
  put back. So that step loses 4 to 5 points by itself, the anti-firefly filter about 5 more. NRD's units do nothing.
- The tracer's own filters, settled (`f2-*`), candles / sky: with the brightness weight made the same both ways
  (still dividing by each pixel's own weight sum): direct light on the floor 0.748 -> 0.978, reflections on the metal
  0.679 -> 0.932 and 0.921 0.900 0.860 -> 0.985 0.982 0.972, the built-in denoiser's bounced light 0.960 -> 0.968;
  with NO brightness weight at all: 0.979, 0.983, 0.975. So a symmetric weight alone does not reach 3 %.
- What the anti-firefly filter costs to turn off (`n-firefly0` against `n-base`, `TGPT_TEST_NRD_ONLY=1`):

| scene | boil (mean) | flicker | error against the reference | light |
|---|---|---|---|---|
| sun | 0.179 -> 0.191 (+7 %) | 0.214 -> 0.224 | 4.11 -> 3.57 | 0.926 -> 0.940 |
| night (24 torches) | 0.264 -> 0.273 (+3 %) | 0.338 -> 0.344 | 6.35 -> 5.87 | 0.909 -> 0.924 |
| night, a light 40x far away | 0.259 -> 0.269 (+4 %) | 0.324 -> 0.332 | 6.37 -> 5.91 | 0.912 -> 0.928 |
| night, a light 40000x far away | 0.298 -> 0.305 (+2 %) | 0.412 -> 0.416 | 19.3 -> 18.0 | 0.656 -> 0.672 |
| candles on the floor | 0.311 -> 0.324 (+4 %) | 0.349 -> 0.362 | 8.31 -> 7.35 | 0.813 -> 0.831 |

  In plain words: without it the picture boils 2 to 7 % more, flickers 1 to 5 % more, and is 7 to 13 % CLOSER to
  the undenoised reference. All five checks of the study still passed. The owner decides this trade; my
  recommendation is off (it is a cut of light, which the owner's rule forbids).

## What is in the copy now

### 1. The a-trous pass keeps light exactly (`pt_atrous.hlsl`)

For the bounced light (built-in route), the direct light and the reflections.

- Every weight is the same seen from either of two pixels: the brightness weight takes both pixels' noise (the mean
  of their variances; the 3x3 smoothing of one pixel's variance is gone, it cannot be made the same both ways), the
  plane term the farther of the two planes, the normal power and the mirror blend the two pixels' mean roughness.
- A pixel keeps what its neighbours do not take: `center * (1 - taken) + sum`, the kernel's 25 weights adding up to
  1. Before, each pixel divided by the sum of the weights it found.
- Why both are needed: a bright pixel that some neighbours half refuse was still diluted by the ones it took (they
  are many and dim) and gave each of them little back (they divide by a large sum).
- CPU model (`docs/agents/filters/filters-sim-atrous.py`: a flat surface, light arriving as rare bright samples, pt_temporal's
  history of 16, two iterations with the feedback; the settled picture's mean over the samples' mean, and a pixel's
  spread over time over the mean):

| a bright sample every | old | weight the same both ways | keeps light (the copy) |
|---|---|---|---|
| 2 frames | mean 1.000, noise 0.015 | 1.001, 0.015 | 1.000, 0.016 |
| 10 frames | 0.909, 0.047 | 0.944, 0.045 | 1.001, 0.048 |
| 50 frames | 0.762, 0.108 | 0.900, 0.103 | 1.006, 0.120 |
| 200 frames | 0.552, 0.226 | 0.914, 0.210 | 1.009, 0.254 |

  The model's "old" and "both ways" columns land where the earlier agent's GPU numbers are (0.75 and 0.93 to 0.98),
  which is why I trust its third column until the GPU says otherwise. The price in the model: 2 to 12 % more noise.
- The price in the picture, to be measured: where neighbours are refused (an edge, a pixel far brighter than the
  noise explains) the pixel keeps more of itself, so it is filtered less there.

### 2. NRD's anti-firefly filter off (`nrd_denoiser.cpp`)

`relax.enableAntiFirefly=false`. Numbers above.

### 3. The normal map's shading around NRD (`pt_gi_spatial.hlsl`, `pt_nrd_prep.hlsl`, `pt_compose.hlsl`, `tracer.cpp`)

Why the old way lost light: the bounced light's rays are drawn about the SHADING normal (`pt_gi.hlsl`), so a pixel's
samples cover only the half-space over its own bump. `pt_gi_spatial` re-weighed that sample for the geometric normal
(`resultFlat`), NRD filtered it, and mode 2 multiplied by NRD's linear SH ratio. That ratio's mean over a normal map is
not 1.

- `pt_gi_spatial` hands NRD the light as the shading normal takes it (the same sample the undenoised picture shows).
  `resultFlat` is gone.
- `pt_nrd_prep` mode 1 divides it by the normal map's share (`bump_share`) taken from the LAST frame's filtered light
  and direction where the pixel was then; mode 2 keeps those in NRD's direction output (brightness in its alpha) and
  the tracer passes that texture in `g.denoise1.y`. Where there is none (a cut, off screen) the sample's own direction
  serves. Mode 2 multiplies this frame's share back.
- `bump_share` is a single direction plus an even half-space, solved from the filtered light's brightness and
  direction (two numbers, two kinds of light). It is exact for either kind and always 1 on a flat surface. It replaces
  NRD's linear SH ratio held to [0, 2]. Held to [1/8, 8] on both sides, so the limits cut nothing.
- NRD's direction input is weighted by the brightness measure RELAX's result uses (it was Rec. 709 against the Y of
  YCoCg: two measures in one ratio).
- `NRD_LIMIT` applies after the division; mode 1 leaves the share NRD was handed in the traced light's alpha and
  `pt_compose` adds `traced * (1 - handed)` back.
- CPU model (`docs/agents/filters/filters-sim-share.py`: a patch of bumps, rays drawn as pt_gi draws them, NRD taken as
  blurring the whole patch into one value, which is the worst case for the new way; where NRD blurs nothing the new
  way is exact by construction). The picture's mean over the truth:

| light | bumps tilted about | old | the copy |
|---|---|---|---|
| from all around, evenly | 10 / 20 degrees | 0.990 / 0.962 | 1.000 / 1.000 |
| a lit wall 30 degrees off the normal | 10 / 20 | 1.004 / 1.016 | 1.000 / 0.998 |
| a lit wall 60 degrees off | 10 / 20 | 1.003 / 0.984 | 0.997 / 0.976 |
| one low light, 78 degrees off | 10 / 20 | 0.944 / 0.792 | 0.972 / 0.899 |
| even light plus a wall 60 degrees off | 10 / 20 | 0.992 / 0.967 | 1.000 / 1.000 |
| two low lights from opposite sides | 10 / 20 | 0.986 / 0.909 | 1.000 / 1.002 |

  The old column's 0.962 for even light on strong bumps is the 4 to 5 % the GPU runs show in the closed room.
- A LIMIT THAT IS LEFT, said plainly: one low light on strongly tilted bumps still loses up to a tenth where NRD
  blurs everything (half of what it lost before). Cause: a bump that faces away from the light draws no ray toward it,
  so its pixel cannot say how bright the plane is, and it pulls the blurred value down. The exact way needs rays that
  see the whole half-space over the plane at every pixel (drawn about the geometric normal, with ReSTIR's target to
  match): a change of the tracer's sampling, not of a filter. Not done.
- The share's detail also got truer in the model: against the real shading's contrast, the old ratio gave 1.27 for
  even light (too much) and 0.30 to 0.52 for a lit wall (too little); the new one 0.96 and 0.41 to 0.80.

### 4. Reflections are filtered by what they show (`pt_specular.hlsl`, `pt_common.hlsli`, `pt_atrous.hlsl`, `pt_temporal.hlsl`, `tracer.cpp`)

- `shade_seen` leaves the normal and the reflectance (diffuse colour plus what the lobe reflects) of the surface it
  shaded; `pt_specular` writes them per pixel into a new texture (`specSeen`, R32G32_UINT, `g.material.z`;
  `pack_seen`: kind, normal 22 bits, reflectance 3 x 10 bits). Kinds: none (the sky, or a rough surface's light from
  the cache), reflected, seen through glass, the water's own path. A mirror seen in the mirror, and water seen in it,
  carry no guide.
- `pt_atrous`, specular: `picture` is 1 at roughness 0.15 and below and 0 at 0.35 and above. By `picture`, a neighbour
  counts only as far as it shows the same surface: its normal, the plane through the shown point, and its reflectance
  to within a few hundredths (`SAME_REFLECTANCE`: half weight at one hundredth apart, 0.04 at five). So an even wall in
  the mirror is denoised, and a texture's cells, a letter, an edge stay as traced. A pixel with no guide is left to
  its history alone. All four iterations run on a mirror (before: the first only, unguided).
- The first iteration is not fed back into a picture's history (fed back, the little that crosses faint detail each
  frame adds up over 16 frames).
- `pt_temporal`, specular, `picture` >= 0.5: the history is fetched where the shown point was last frame (the point as
  far behind the surface along the camera's ray as the hit distance: a flat mirror's image, or the thing behind
  glass), and a history texel counts only if it showed the same point (its hit distance is kept in the moments'
  fourth value). Before, a mirror's history followed the mirror's face.
- The water's own path keeps "the first iteration only" where it is mirror-like, and is filtered with water alone.

What this does NOT do, said before it is found:
- Faint detail in a mirror (contrast under about 5 %) is still softened where the picture is noisy. Strong detail
  is not.
- A noisy, finely textured thing in a mirror is denoised only by its 16 frames of history, so it is noisier than
  the same thing seen directly. (The clean way is below.)
- What a mirror shows is taken as standing still: a moving thing in a mirror loses its history (noise), it does not
  trail.
- A curved mirror keeps its history under a moving camera only where the surface stays within the plane test.
- Glass's own reflection and what is behind the glass share one signal and one guide (what is behind).

## First numbers (14:05; runs killed half way, to be taken again; `%TEMP%\tgb\filters-out\b-mirror.txt`, `f1-mirror.txt`, `f3-mirror.txt`)

`mirrorFilters`, the perfect mirror floor, over the panel's middle as the mirror shows it. Detail kept and light are
against the undenoised picture (1 = the same); flicker is a pixel's spread over its mean.

| build | texture | detail kept: NRD / built-in / as RR is handed it | light: NRD / built-in / RR | flicker: NRD / built-in (undenoised) |
|---|---|---|---|---|
| unchanged tracer | fine checker | 0.224 / 0.227 / 0.996 | 1.205 / 1.202 / 1.024 | 0.087 / 0.085 (0.922) |
| unchanged tracer | coloured cells | 0.511 / 0.566 / 0.646 | 1.186 / 1.168 / 0.990 | 0.070 / 0.082 (0.845) |
| my first guided filter (replaced) | fine checker | 2.807 / 2.703 / 0.996 | 2.172 / 2.063 / 1.024 | 0.112 / 0.118 |
| the same with the light-keeping exchange (replaced) | fine checker | 1.860 / 2.063 / 0.996 | 1.848 / 1.774 / 1.024 | 0.114 / 0.116 |

- The offline check REPRODUCES PT-80 on the unchanged tracer: a mirror's fine checker keeps 0.22 of its detail with
  NRD and with the built-in denoiser (in game: 0.09). Ray Reconstruction is handed the picture whole (0.996).
- New finding on the unchanged tracer: the mirror's picture holds 17 to 20 % MORE light with a denoiser than
  without. The old filter also makes light.
- My first guided filter divided each pixel's light by the reflectance of what it shows, filtered, and multiplied
  back. It nearly doubled the mirror's light and the checker's contrast. Cause, worked out and not yet proven by a
  run: the six lamps' own highlights on the mirror sit in the same signal as the reflection; a highlight over a black
  cell became nine times as much "lighting" as over a white one, flowed to the white cells and was multiplied by
  their reflectance. TAKEN OUT: the copy's filter never divides by the reflectance, it only exchanges light between
  pixels that show the same thing, so it cannot make light. A source check now says so.
- "As RR is handed it" kept only 0.65 of the coloured cells' detail on the unchanged tracer too, with the right
  light. Likely the check's own fault: the undenoised pictures were 48 frames after 16, the others 16 after 48, and
  the room is still settling then. The check now takes every picture as 32 frames after 64. Look at that column
  first when the card is free.

## Tests still owed, with their command lines

The build is ready: `%TEMP%\tgb\filters-build\tgpt_gpu_test.exe`, runtime folder `%TEMP%\tgb\filters-rt` (the build
script stages the DLL there). Rebuild after any edit: `cmd /c docs\agents\filters\filters-build.cmd` (Ninja,
with NRD; it names this session's copy and scratchpad: change its `SP` and `WT` lines for another copy).
GPU rules as always: the game closed, the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0.

One run, by hand (PowerShell):

```
$env:TGPT_TEST_STARTUP_SECONDS='900'; $env:TGPT_SHADER_CACHE="$env:TEMP\tgb\filters-shadercache"; $env:TGPT_TEST_STAGE='filters-stage'
$env:TGPT_TEST_MIRROR_FILTERS_ONLY='1'      # or the switches of the row below
& "$env:TEMP\tgb\filters-build\tgpt_gpu_test.exe" "$env:TEMP\tgb\filters-rt" "$env:TEMP\tgb\filters-out\mirror-images" *> "$env:TEMP\tgb\filters-out\mirror.txt"
```

Or the queue script (each run waits for a free GPU by itself; `-Snap <name>` runs from a copy so a build does not
disturb it): `powershell -File docs\agents\filters\filters-run.ps1 -Snap final -Runs "<tag>|<ENV=1;ENV=..>,<tag>|..."`,
then `python docs\agents\filters\filters-show.py <tag,tag>`.

| order | tag | environment | what it answers | minutes |
|---|---|---|---|---|
| 1 | `mirror` | `TGPT_TEST_MIRROR_FILTERS_ONLY=1` | PT-80: detail, light and flicker in a mirror, still and moving, glossy floors | 3 |
| 2 | `mean-candles` | `TGPT_TEST_NRD_MEAN_ONLY=1;TGPT_TEST_NRD_MEAN_SCENE=candles` | PT-75 where it was worst: direct light, reflections on metal | 5 |
| 3 | `mean-slot` | `TGPT_TEST_NRD_MEAN_ONLY=1;TGPT_TEST_NRD_MEAN_SCENE=slot` | PT-75, bounced light alone, NRD's part | 5 |
| 4 | `mean-grille` | `TGPT_TEST_NRD_MEAN_ONLY=1;TGPT_TEST_NRD_MEAN_SCENE=grille` | the hardest case for NRD (the sky through small holes) | 5 |
| 5 | `flat` | `TGPT_TEST_NRD_MEAN_ONLY=1;TGPT_TEST_NRD_MEAN_SCENE=the sky through a;TGPT_TEST_NRD_MEAN_FLAT=1;TGPT_TEST_NRD_MEAN_QUICK=1` | what is left with no normal maps: RELAX's own share (replaces the void `s-linear-flat`) | 3 |
| 6 | `nrd` | `TGPT_TEST_NRD_ONLY=1` | the noise study: boil, flicker, error, detail kept, against the table above (`n-base`, `n-firefly0`) | 20 |
| 7 | `brief` | `TGPT_TEST_NRD_MEAN_ONLY=1;TGPT_TEST_NRD_MEAN_BRIEF=1` | the mean check as the whole test runs it | 7 |
| 8 | whole | none | the whole GPU test, once, its FAIL lines written down | long |

How to read them:
- `mirror`: "detail kept" should be 0.9 to 1.1 for NRD and the built-in denoiser (0.22 before), "light" within 3 %
  (1.20 before). If the light is off, the filter is not at fault any more (it cannot make or lose light); look at the
  temporal pass and at what crosses the measured region's border.
- `mean-*`: each region prints "the largest shift of a channel: NRD x, the built-in denoiser y, against a tolerance".
  Expected from the numbers above: the built-in denoiser and the direct light within 1 %; reflections within 1 to 2 %;
  NRD's bounced light on bumpy surfaces from 0.91 to about 0.99 in the slot's room. The grille's walls are the open
  question (0.76 to 0.84 before; nobody has run them with the fixes).
- If a `mean` region still fails for NRD after this: it is NRD's own or the limit named in 3. Do not widen the
  tolerance to make it pass and do not add a correction after NRD. Write the number down; the owner decides.
- `nrd`: compare boil and flicker of "built-in" and "NRD" with `n-firefly0` (the earlier agent's run with only the
  anti-firefly filter off). The difference is what the light-keeping filter costs. The study's own five checks must
  still pass ("NRD keeps the normal map's shading ... under the ceiling" should rise from 0.36).
- Thresholds I set without a run, to be confirmed or corrected from the first real numbers, never loosened to hide a
  loss: the mirror's moving camera (last frame keeps more than 0.7 of the undenoised frame's detail), the glossy
  floors (flicker under half the undenoised picture's).
- The whole test's known FAIL lines on the base: "skin 110 m from scene origin", the glass index check, three
  tinted-glass checks, the lake's 33.7 ms; and "coloured or white bulk slab retains its diffuse colour ... within 3.5
  percent" (it failed in every one of my runs, before any of my code ran in them, and in the earlier agent's).
- Checks of the whole test my change may move (look at these first): the decal block's mirror shots (`mirrorOn`,
  `mirrorFlat`, `mirrorNormal`: they read the denoised specular view), "NRD flickers no more than half again as much
  as the built-in denoiser", "NRD boils no more than a quarter above the built-in denoiser", and the GPU-time checks
  (the specular a-trous reads two more textures a neighbour).

## For the coordinator's game runs (after the offline tests pass)

- `mirror-tavern@mirror-check` (`mirror.py`): "traced, NRD" detail against the tracer's own view should go from 0.08
  to about what "traced, no denoiser" has there (1.0); brightness against it from 0.94 toward 1.0. The mirror will
  look noisier than before where it shows textured things (it was smeared before).
- `vista@mean`, `candle@quick`, `glass-jars@quick` (`mean_colour.py`): the NRD step's light as a share of the
  undenoised step's should move toward 1 in every channel, most on candle-lit floors (direct light) and metal
  (reflections), and the blue should stop going first in the vista's shade. Blocks off by more than 10 % should drop
  from tens of percent to a few.
- `flicker.py --boil`: expect a little more with NRD: 2 to 7 % from the anti-firefly filter being off, plus what
  the light-keeping filter costs (about a tenth in the CPU model, not yet measured). If it is clearly more than
  that, tell me or the next worker before tuning anything: the filter must not get its light back.
- `glass-jars`: what is behind glass is filtered like a mirror's picture now. Sharper, and noisier where textured.

## Workarounds met (removed or not)

- REMOVED: `resultFlat` in `pt_gi_spatial.hlsl` (a second estimate of the bounced light for NRD alone).
- REMOVED: NRD's linear SH ratio as the normal map's share, its [0, 2] clamp (it cut light on bumps that face a low
  light), and the two brightness measures in one ratio.
- REMOVED: the mirror rule "the first iteration only" for everything but water, the 3x3 variance smoothing, the
  per-pixel division by the weights found.
- KEPT, named: the water's own path keeps "the first iteration only" on mirror-like water (`water_rests`) and is
  filtered with water alone. The water is another agent's; its filter should follow what it shows too.
- KEPT, named: `NRD_LIMIT` (a firefly over 500 medians is handed to NRD at the limit and the rest comes back as
  traced): exact in light, but that rest is noise NRD never sees.
- MET, not mine: ReSTIR's own reuse changes the mean before any filter (the study prints it: the candle room's
  undenoised light is 0.934 of the same paths with no reuse at all).
- MET, not removed: the reflections' signal holds the surface's own lamp highlight and its reflection together, and
  glass's own reflection with what is behind it. One signal each is the clean way: then the reflection alone could
  be filtered as lighting (divided by the shown surface's reflectance), which denoises textured things in a mirror
  too. It needs three more textures and one more temporal pass. Not done; the variant of the filter that divides is
  kept as `docs/agents/filters/pt_atrous_demodulated.hlsl.txt` for whoever does it.

## Left for a next worker

1. The owed tests, in the order above.
2. If NRD's bounced light still misses 3 % on bumpy surfaces under low light: the sampling change named in 3.
3. The split of the reflections' signal (above), for a clean mirror showing textured things.
4. The moving thing in a mirror (the hit's own motion), and curved mirrors' history.
5. The filter's feedback and the normal weights on bumpy surfaces were about 2 % of the built-in denoiser's bounced
   light in the earlier agent's runs even with no brightness weight; the light-keeping exchange should have taken that
   too. `mean-slot`'s "built-in denoiser" column says.

## Files changed

`native/pathtracing/shaders/pt_atrous.hlsl`, `pt_temporal.hlsl`, `pt_nrd_prep.hlsl`, `pt_gi_spatial.hlsl`,
`pt_compose.hlsl`, `pt_specular.hlsl`, `pt_common.hlsli` (the guide's packing, `shade_seen`'s two values, two
comments); `native/pathtracing/src/tracer.cpp` (the guide texture, the two slots, the reflections' temporal flag, the
kept direction's state), `nrd_denoiser.cpp` (one setting); `tests/pathtracing_gpu_test.cpp` (`nrdMean`,
`mirrorFilters`, both called from the whole test after `nrdStudy`; the brown floor and the tinted metal);
`tests/source_checks.py`; `CHANGELOG.md`; `docs/TESTING.md`; this file. Not touched: every file named as not mine.

Beside this file, `docs/agents/filters/`: `filters-build.cmd`, `filters-run.ps1`, `filters-show.py` (build, queue,
read), `filters-sim-atrous.py`, `filters-sim-share.py` (the two CPU models: `python <file>` prints the tables above)
and `pt_atrous_demodulated.hlsl.txt` (the replaced variant of the filter).
