# PT-46: why mirrors are soft, and the fix

Agent report, 2026-10-07. Built and tested offline in a copy of the tree (`wt/sharp`, base `wt/base21`). Nothing ran
in the game, nothing was deployed or committed.

## In three lines

1. Mirrors were soft because of our own code: the tracer treated a perfect mirror as slightly rough. Each frame its
   reflection rays went a little off in a random direction (about 4 pixels' worth at the game's render size), and Ray
   Reconstruction averaged those into a blur. The same roughness made the mirror read its textures from a smaller,
   blurrier copy (mip) than the camera uses.
2. Fixed: a mirror's reflection rays now go exactly where a mirror sends them, and read the texture copy the camera would
   read from the mirror's place. Offline, a fine checker in a perfect mirror: fine detail 0.35 of the camera's own view
   before, 0.96 now; the copy read 1.71 before against the camera's 1.01, now 1.00.
3. Also: Reflection Boost no longer roughens a mirror, and Ray Reconstruction gets the reflection's distance measured
   from the mirror's surface, as NVIDIA's guide asks. Not yet seen in game.

## Words used here

- **Mip**: one of the smaller, pre-blurred copies a texture keeps of itself. Mip 0 is the full-size one; each next mip
  is half as wide. Mip 1.7 means a blend of mips 1 and 2.
- **Ray cone**: how wide a ray is where it meets something; that width picks the mip.
- **Lobe**: the spread of directions a surface reflects into. A mirror's is one direction; a rough surface's is wide.
- **Fine detail**: what is left of a picture once a 5x5-pixel blur of it is taken away (texture edges, grain), measured as
  its spread. 1 = as sharp as the reference.

## Findings

What the brief named as suspects, and what the numbers say:

| Suspect | Verdict |
|---|---|
| (a) the mip a reflected ray reads | **Yes, part of it.** The cone widened by the lobe's alpha with `roughness_alpha`'s floor (0.045, alpha 0.002): three times a pixel's angle. Offline: mip 1.71 in the mirror against 1.01 seen directly. |
| (a') the mirror roughened by its lobe | **Yes, the larger part.** The reflection ray was drawn from GGX at alpha 0.002 too: a perfect mirror's rays strayed about 0.004 rad (2 x alpha; the lobe has long tails). The game renders at 1924x1084 with a horizontal field of view of about 90 degrees (from the harness mirror's size on screen): a pixel is 0.00104 rad, so the strays were about 4 render pixels, up to about 2.6 pixels as seen from the camera for the tavern's door. Each frame shows a different stray; Ray Reconstruction averages them. |
| Reflection Boost roughening the mirror | **Not at the default 0.1** (it raised roughness 0 to 0.01, under the 0.045 floor anyway). **At 1 it did** (roughness 0.05): mip 1.86, detail 0.27 offline. |
| The mirror's own normal map | None: the harness's mirror (`Mirror.cs`) is HDRP/Lit with no normal map. |
| (b) Ray Reconstruction blurring reflections | Its guides at the mirror were already right (specular albedo 0.999, roughness 0, the reflection's hit distance; it computes the reflection's motion itself from the hit distance and the matrices we pass, which its guide offers in place of specular motion vectors, 3.4.8). It was given a signal that strayed every frame, which it can only average. Whether it still blurs a steady mirror is for the harness's `mirror-sharp` run (RR against no denoiser). |

The offline measurement (`tgpt_gpu_test`, new check after the shared-texture mirror check): the floor a perfect mirror
(roughness 0, a metal of reflectance 1), the camera 0.8 m over it looking down at a panel 2 m away, render 1280x720
with a 60-degree vertical field of view, one ray a pixel, no reuse, clamps or denoiser, 32 frames. The reference is the
same panel seen directly from the camera's mirror image under the floor, along camera rays (the Materials view: one
pixel's footprint), the floor taken out of the traced scene. Two textures, RGBA8 linear with every mip: a checker of
8-texel squares, and one whose every mip is its own flat grey ((k + 0.5) / 12), which tells the mip read. The texture as
a mirror pixel shows it: (light with texture - light with a black panel) / (light with a white panel - black), the same
frame seeds each, so the lamp and the panel's gloss drop out.

| | mip read in the mirror | mip seen directly | fine detail, mirror / direct |
|---|---|---|---|
| before | 1.71 | 1.01 | 0.350 (0.0952 against 0.2723) |
| now | **1.00** | 1.01 | **0.956** (0.2604 against 0.2723) |
| now, the top-mip switch | 0.02 | | 1.121 (sharper than the direct view: mip 0 aliases) |
| before, Reflection Boost 1 | 1.86 | | 0.269 |
| now, Reflection Boost 1 | 1.02 | | 0.955 |

The picture (`sharp-images-*/67-mirror-fine-checker.png` against `68-fine-checker-from-the-mirror-image.png`): before,
the checker's squares wash to grey with distance; now they hold as far as the direct view's.

## What changed

1. **`reflection_alpha`** (`pt_common.hlsli`): GGX's alpha for drawing a reflection ray and for how much its lobe
   widens the ray's cone: the surface's own roughness squared, floored at 0.01 (alpha 1e-4: a tenth of a pixel's stray).
   `roughness_alpha` (floor 0.045) stays for evaluating highlights at lights, where the floor bounds the peak.
2. **`pt_specular`**: the reflection ray, its estimator weight (`smith_g1`) and its cone spread use `reflection_alpha`.
   The cone is the camera pixel's: one pixel's width at the mirror, widening by one pixel's angle plus the lobe's alpha
   (now about 0 for a mirror) over the way on. Surfaces rougher than 0.045 are exactly as before.
3. **`shade_seen`**: the reflection inside a reflection (a smooth metal seen in a mirror) the same way.
4. **Reflection Boost never roughens** (`reflection_boost` in `pt_common.hlsli`, and the guides pass in `d3d11.hlsl`):
   a surface smoother than the boost's 0.05 keeps its roughness. The owner's rule for stone and metal (everything a
   little glossier, metal more and mirroring more) is unchanged for every surface rougher than 0.05.
5. **Ray Reconstruction's hit distance** (`pt_specular`): measured from the mirror's surface along the ray to the plane
   of what it met (exact for a flat one), where it was from the ray's start, which sits 2 cm plus 0.2 % of the camera
   distance off the surface. NVIDIA's DLSS-RR guide 3.4.9: "the World Space distance between the Specular Ray Origin and
   Hit Point. Specular Ray Origin must be on the Primary Surface." Offline: a ceiling 6 m over a floor mirror now 6.0078 m
   (fp16 steps are 4 mm there); from the ray's start it was 5.9844 m, 1.6 cm near (mutation D below). Grazing hits (the ray within 11 degrees of
   the surface it met) take the straight distance from the mirror.
6. **The harness's switch**: hidden config key **"Reflections read the top mip"** (Advanced, default false) sends
   **`TGPT_OPTION_REFLECTION_TOP_MIP` = 134217728** (the bit offered): `pt_specular` then gives its rays no cone, so
   what a reflection meets is read at mip 0.
7. **The harness recipe `mirror-sharp`** (`tests/ingame/scenarios.json`): the game's view from the mirror's side, the
   traced one (Ray Reconstruction), the traced mirror with Ray Reconstruction and with no denoiser, then with the top-mip
   switch (no denoiser, then Ray Reconstruction), then Reflection Boost 0 (Ray Reconstruction); the last step sets
   Denoising back to Ray Reconstruction, the switch off, Reflection Boost 0.1 and Mode Off (the run restores the owner's
   config after anyway). `tools/harness/mirror.py` adds the three switch steps to its table against the traced view and
   to `mirror.jpg`, and skips steps a recipe does not take.

| File | What |
|---|---|
| `native/pathtracing/shaders/pt_common.hlsli` | `reflection_alpha`, `OPTION_REFLECTION_TOP_MIP`, `reflection_boost`'s `min`, `shade_seen`'s ray |
| `native/pathtracing/shaders/pt_specular.hlsl` | the ray, its weight and cone, the top-mip switch, the hit distance |
| `native/pathtracing/shaders/d3d11.hlsl` | the guides' Reflection Boost `min` |
| `native/pathtracing/include/tgpt_api.h` | `TGPT_OPTION_REFLECTION_TOP_MIP` |
| `src/TGPathTracing/Settings.cs`, `NativeTracer.cs` | the hidden key and its option bit |
| `tests/pathtracing_gpu_test.cpp` | the sharp-mirror check (5 expects), the hit distance to a wall within 1 % and to a ceiling within 0.2 % |
| `tests/source_checks.py` | `test_path_tracing_mirrors_are_sharp` |
| `tests/ingame/scenarios.json`, `tools/harness/mirror.py` | the `mirror-sharp` recipe and its table rows |
| `CHANGELOG.md` | the entry, first under TG Path Tracing |

## ABI and bits

- **No ABI change**: no struct changes size, no new export. Added: option bit **`TGPT_OPTION_REFLECTION_TOP_MIP` =
  134217728** (the one offered). An old plugin with this DLL: the bit is never sent. This plugin with an old DLL: the bit
  is ignored (the switch does nothing). No material bit, no FrameConstants change.
- Merge notes: `pt_specular`'s `alpha` is now `reflection_alpha(s.roughness)`; `shade_seen`'s smooth-metal ray uses
  `rayAlpha` (its `alpha` for `direct_light` is unchanged); `reflection_boost` gained two lines (the source check on its
  lerp line still holds). Agents touching the reflection's hit distance: it is now the block `if (found) { toHit ... }`.

## Offline numbers

`tgpt_gpu_test` on the final build (`sharp-inst-after2`): **928 checks passed, 0 failed, exit 0** ("all path tracing
checks passed"). On the old shaders with the new test (`sharp-inst-before`): 923 passed, the 4 new mirror checks failed
(mip, detail, the top-mip switch, Reflection Boost); the ceiling check came later and is covered by mutation D.
`python tests/source_checks.py`: 86 tests, OK. `python tests/harness_analyze_test.py`: OK. The plugin and the harness
plugin compile (`dotnet build -c Release` to `%TEMP%\tgb\sharp-managed` and `sharp-harness`: 0 warnings, 0 errors).

Unchanged by the fix (the same numbers before and after): the red wall in a perfect mirror 0.982 bright, colour 0.989 and
0.981 (Indirect Color 1); in fog 0.995 to 0.997. Ray Reconstruction's guides on the mirror: specular albedo 0.999,
roughness 0.000; the hit distance to the wall within 1 % at 100.0 % of pixels (96.6 % before: rays meeting the wall's
foot).

Mutation checks (each built into its own prefix, the file put back byte for byte, the full test run; the clean rebuild
after them differs from the tested one in 2 bytes, the link time):

| Broken | Mirror's mip / detail | What failed |
|---|---|---|
| A: the ray drawn from `roughness_alpha` again (the strays), cone kept | 1.00 / **0.402** (top mip 0.442) | "a perfect mirror shows a fine texture as sharp as the camera sees it directly", "Reflection Boost leaves a perfect mirror a mirror" |
| B: the cone widened by `roughness_alpha` again, ray kept | **1.71** / 0.753 | the mip check, the detail check, the Reflection Boost check |
| C: Reflection Boost's `min` gone (`pt_common`) | at boost 1: **1.86 / 0.269** | "Reflection Boost leaves a perfect mirror a mirror" |
| D: the hit distance from the ray's start again | ceiling **5.9844 m** (now 6.0078) | "the mirror's hit distance to a ceiling over it is the way from the mirror's surface, as Ray Reconstruction's guide asks" |
| E: the top-mip switch ignored | top mip 1.00 | "for testing, reflections read the top mip" |

So of the 0.35 before, the strays cost the most (alone they leave 0.40), the wide cone the rest (alone 0.75).

Cost: the reflections stage with the mirror filling most of a 1280x720 view 0.43 ms (the mirror agent's run: 0.41 to
0.45). A mirror now reads finer mips (more texture memory traffic) only where a surface is smoother than 0.045; the F8
report's "reflections" line is the number that counts in game.

## What the next harness run should show

Build, install and deploy this copy (TGPathTracing.Native.dll and the plugin together; no ABI change), then:

1. **`-Scenario mirror-tavern -Recipe mirror-sharp`**, then `python tools\harness\mirror.py <run>\mirror-tavern --frame`
   (with `--frame` to compare with run `20261007-051746`'s 0.36 and 0.23). The table against the traced view:
   - `traced, Ray Reconstruction` detail **well above 0.36**, toward 1; `traced, no denoiser` above 0.23. Brightness
     and shapes as before (0.99, 0.93).
   - **top mip** against the normal steps: within about 10 % of them means the mip is right now; much sharper means the
     cone is still too wide (or the game streamed the door's texture down to a small mip: then the top mip is no sharper
     either, and `mipstreaming.txt` of a `mirror` run tells).
   - **no denoiser** much sharper than Ray Reconstruction: what is left is the denoiser's (RR gets the direct view's
     texture detail from the albedo guide, the mirror's only from its light).
   - **Reflection Boost 0** the same as step 3 (the boost no longer touches a mirror).
2. **`-Scenario mirror-tavern,mirror-day`** with the `mirror` recipe, and `mirror.py --frame`: the same rise in detail;
   by day the far haze is the fog agent's (PT-37).
3. **What to look at in `mirror.jpg`**: the door's planks, iron bands and rivets as crisp as in "the tracer's own view
   from the mirror's side"; no shimmer (a mirror reading a too-fine mip would sparkle on far textures).

## Risks

- **Surfaces smoother than 0.045 roughness** (mirrors, polished metal, see-through glass, which the tracer gives 0.03)
  now reflect sharper than before and read finer mips: a little more texture traffic, and a sharper reflection of lamps
  and the sun on glass. The glass checks (a lamp's and the sun's mirror image on glass) pass as before. Rougher surfaces
  are untouched.
- **The cone uses one pixel's angle at the screen's centre**: towards the screen's left and right edges a pixel spans
  less angle, so a mirror there reads up to one mip coarser than the camera does at the very edge (90 degrees wide).
  Small in the tavern mirror (within 20 degrees of the centre: under 0.2 mip). Not changed.
- **Curved mirrors** (a polished helmet, a bottle): the cone has no curvature term, so a convex one reads finer mips than
  it should and may shimmer. As before for rough surfaces; new for the smoothest.
- **The water** (`pt_water`) keeps its own cone and `roughness_alpha`'s floor: calm water is rougher than 0.045 in the
  game's GBuffer as far as the harness has seen; not measured.
- **Ray Reconstruction**: not run offline (the harness never runs it). Its guides at the mirror were right before and
  are now exact in the hit distance; the in-game run is the test.
- **The top-mip key** is hidden, for the harness; a player who sets it gets sharp but shimmering reflections of far
  textures.
