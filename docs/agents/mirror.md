# PT-46: a mirror shows the world as the camera sees it

Agent report, 2026-10-07. Built and tested offline in a copy of the tree (`wt/mirror`, base `wt/base16`). Nothing ran
in the game, nothing was deployed or committed.

## In three lines

1. The tavern mirror's strong colours are mostly the tracer's own light, not the mirror: the traced picture outside the
   mirror is as much more coloured than the game's as the mirror is. What was the mirror's own fault: a reflected surface
   was lit as a matte path vertex, not as the camera's pixel of it (no Indirect Color, no highlights, a metal lit as matte).
   By day, far things in the mirror are 4 to 7 times too dark and brown where the game's are grey with fog: HDRP lays its
   fog over the mirror at the mirror's own distance, never along the way to what it shows.
2. Fixed: what a reflection meets is now lit as the camera's pixel of it is (`shade_seen`). Offline, a perfect mirror shows
   a lit red wall within 2 % of its brightness seen directly, and its colour within 5 % at Indirect Color 1.
3. The head: the game loads no head in first person. The first-person body ends in a faceless stand-in around the camera,
   which the game never draws. Reflections now pass it near the camera: the mirror shows the body without a head, as the
   game's own view from the mirror's side does. The real head cannot be drawn: it is not loaded.

## Words used here

- **Path vertex**: a point a bounce ray meets. The tracer lights those simply (matte, no highlights), which is fine for
  bounced light but not for what a mirror shows.
- **Indirect Color**: the row that makes bounced light more colourful (the owner plays at 3).
- **Radiance cache**: the tracer's store of bounced light per small cell of the world.
- **Stand-in head**: the plain egg shape on the hero's first-person body, where a head would be.

## Why the mirror's colours and brightness were off

From the last harness run (`20261007-023808`, `mirror.py --frame`, the frame JPGs in linear light).

| | tavern | day |
|---|---|---|
| saturation, the game's own picture outside the mirror | 0.68 | 0.37 |
| saturation, the traced picture outside the mirror | 0.86 (x1.26) | 0.35 |
| saturation in the mirror: reference / traced | 0.76 / 0.98 (x1.29) | 0.39 / 0.54 |
| brightness in the mirror against the reference (exposure out) | 1.54 (RR), 1.62 (no denoiser) | 0.64, 0.70 |

1. **Tavern: the tracer's light, not the mirror.** The traced tavern is more coloured than the game's everywhere (warm
   candle light alone, Indirect Color 3; the game's fill lights and baked light are greyer). Outside the mirror the
   saturation rises x1.26, inside x1.29: the mirror shows the traced tavern as coloured as the traced tavern is. The
   harness compares the mirror with the game's untraced picture, which mixes the two. It now also takes the tracer's own
   view from the mirror's side (step `2b-mirror-reference-traced`), and `mirror.py` holds the mirror against that too.
2. **The material colours at a hit are right.** The Materials debug view (what a ray reads at a hit) against the Albedo
   view (HDRP's GBuffer) in four recent scenes (`candle-up`, `hair-eyebrows`, `npc-day`, `candle`): median colour ratio
   per channel 0.99 to 1.01, saturation 0.67 against 0.69, 0.28 against 0.27, 0.27 against 0.31, 0.43 against 0.42. The
   textures and their colours are not the cause.
3. **The shading at a hit was a path vertex's, not the camera's.** `shade_path` lit what a reflection meets as matte:
   - Indirect Color (3) scales the camera's bounced light's colour up to 3 times; the reflected surface's was not scaled.
   - no specular at all: no highlights of the lamps and the sun, no reflection of the room, on stone, wood or skin.
   - a metal was lit as a matte surface of its colour (armour, the hero's vest, pots).
   - no Reflection Boost (the camera's pixels have it).
   - eyes and unlit decals were invisible to reflections (camera rays only).

   Measured offline (below): a lit red wall in a perfect mirror came out at 0.86 of its brightness seen directly, its
   green and blue at 0.70 and 0.61 of the wall's against its red. Too dark and too coloured: the missing highlights and
   reflections carry the light's colour, not the wall's. Without the lobe alone (mutation D): 0.90, 0.80, 0.75. So the
   matte shading made a mirror's colours stronger even before the tracer's own warm light.
4. **By day: the fog along the reflected ray is missing.** Regions of the day mirror against the reference (no denoiser,
   exposure out): the hut wall 0.23x and brown (saturation 0.85 against 0.19), the rock face 0.14x, the grass 0.49x,
   the hero's chest 1.6x. The reference's far things are grey with the fog's own light; the mirror stands 0.8 m from the
   camera, and HDRP fogs its pixels for 0.8 m. Not changed here: it needs the fog's light along the reflected ray, the
   design job PT-37 is (the tracer lighting HDRP's fog). Taken alone, the fog's dimming would make the mirror darker still.
5. **By day: the egg.** The stand-in head is a large, strongly coloured patch (saturation 0.76, 2.9x the reference's
   brightness there, where the reference shows the hut wall). Without it the day mirror's saturation is 0.55 against 0.39.
6. **By day: the mirror is blurred by depth of field** (seen in `mirror.jpg`): HDRP blurs the mirror's pixels by their own
   depth (0.8 m, out of focus), not the reflected image's. Not changed (HDRP's post effect, not the tracer's).

## The hero's head

- In first person the game loads a separate first-person body (`VHeroController`: `maleHeroBodyFPP` /
  `femaleHeroBodyFPP` under `fppParent`; the third-person body, with the face from the character creator, loads only when
  `Hero.TppActive`). The harness's log of the hero's Kandra renderers (run `20261007-023808`): `Fpp_Body` (skin,
  `Mat_Male_Body_Base_Hero`, culling mask 1, traced), two `Cloth` (arms, vest). No face, hair, eyes or brows exist.
- The egg is that body's own skin around the camera. The game's camera never draws it (the game's view from the mirror's
  side shows nothing above the collar). How the game hides it is not in managed code: not Kandra's culling mask (1), not a
  shadow-only or hidden-layer renderer (none in the log), not Kandra's triangle culler (the tracer reads the same culled
  triangles). It is likely in the skin shader's passes; not read (no decode of the RealSkin shader).
- What a mirror should show: the game has no head loaded to show. Done: reflections pass the hero's first-person skin
  within 0.3 m of the camera (`TGPT_MATERIAL_FIRST_PERSON`, `first_person_head`), so the mirror shows the body without a
  head, as the game's own view from the mirror's side. Its shadow and the light it bounces stay as before.
- Not done: drawing the real head. That needs the third-person head (face mesh, hair, eyes, the character creator's
  settings) loaded and posed for the tracer alone: a job of its own, for the owner to decide.
- Risk: a hand brought within 0.3 m of the eyes (drinking, an animation) loses its skin in a mirror for that moment.

## What changed

1. **`shade_seen`** (`pt_common.hlsli`, called by `pt_specular` for every reflection ray's hit, in place of
   `shade_path(hit, l, 1u, ...)`): the light a reflected surface sends back along the ray, as the camera's pixel of it is
   lit:
   - Reflection Boost on its finish, as `pt_prepare` gives a camera pixel (not a leaf's, a blade's or thin cloth's).
   - **`direct_light`**: `direct_diffuse`'s sampling (the sun, one of eight lights by RIS) with a GGX specular lobe
     evaluated at the same samples (f0, roughness, the reflected ray as the view). `direct_diffuse` is now a wrapper with
     no lobe; path vertices are unchanged.
   - its diffuse colour (`albedo`, not `reflectance`: a metal has none) for the diffuse light.
   - its bounced light: the radiance cache's, times diffuse colour and mask-map occlusion, with **`indirect_color`**
     (Indirect Color, moved out of `pt_gi_spatial` into a shared helper, same maths).
   - its specular lobe's share of its surroundings: a smooth metal (roughness under 0.6, f0 over 0.2) traces one more
     reflection ray (a reflection in a reflection, shaded as a path vertex); everything else takes the cache's light
     times the lobe's directional albedo, as Reflections on Glossy Surfaces do.
   - hair keeps no highlight there (the camera's are Kajiya-Kay along the strands); eyes get their wet highlight.
2. **Eyes and unlit decals in mirrors**: `g_mirrorRay`, set by `pt_specular` when the surface is smoother than 0.15 and
   not skin; `alpha_opaque` lets such a ray meet `MATERIAL_CAMERA_ONLY` (not the hair layer's shells).
3. **The stand-in head**: `g_reflectionRay` (set around `pt_specular`'s and `shade_seen`'s reflection rays);
   `trace_from_surface` passes a hit on `MATERIAL_FIRST_PERSON` within `FIRST_PERSON_HEAD` (0.3 m) of the camera
   (`first_person_head`). The plugin: `CharacterCapture.FirstPersonRoot` (`Hero.Current.VHeroController.fppParent`),
   skin materials of renderers under it get `SceneCapture.FirstPersonMaterial` (a copy of the material with the flag,
   logged once).
4. **The harness**: step `2b-mirror-reference-traced` in the `mirror` recipe (the tracer's own view from the mirror's
   side, Ray Reconstruction, 6 s); `mirror.py` adds a second table against it, with both steps' exposures (HDRP's and
   the traced gain from `step.json`) taken out.

| File | What |
|---|---|
| `native/pathtracing/include/tgpt_api.h` | `TGPT_MATERIAL_FIRST_PERSON` 1048576, `TGPT_FIRST_PERSON_HEAD` 0.3 |
| `native/pathtracing/shaders/pt_common.hlsli` | `shade_seen`, `direct_light`, `indirect_color`, `first_person_head`, `g_reflectionRay`, `g_mirrorRay`, `alpha_opaque` |
| `native/pathtracing/shaders/pt_specular.hlsl` | `shade_seen`, the two ray flags, `MIRROR_ROUGHNESS` |
| `native/pathtracing/shaders/pt_gi_spatial.hlsl` | `indirect_color` |
| `src/TGPathTracing/CharacterCapture.cs`, `SceneCapture.cs`, `NativeTracer.cs` | the first-person body's skin flagged |
| `tests/pathtracing_gpu_test.cpp` | the mirror against the direct view, the guides, the box around the camera; two older checks to the new rule |
| `tests/source_checks.py` | `test_path_tracing_mirror_shows_the_camera_view`; four checks follow moved code |
| `tests/ingame/scenarios.json`, `tools/harness/mirror.py` | the traced reference step and its table |
| `CHANGELOG.md` | the entry |

## ABI and bits

- **No ABI change.** No struct changes size. Added: material bit **`TGPT_MATERIAL_FIRST_PERSON` = 1048576** (the one
  offered; set by the plugin) and the constant `TGPT_FIRST_PERSON_HEAD` 0.3. An old plugin with this DLL: nothing is
  flagged, the egg shows as before. This plugin with an old DLL: the flag is ignored. No option bit, no FrameConstants
  change (6128 B).
- Merge notes: `direct_diffuse` is now `direct_light`'s wrapper (other agents touching it: change `direct_light`);
  `pt_gi_spatial`'s Indirect Color block is `indirect_color(result)`; six source checks were moved with the code
  (pt_specular's call, the Indirect Color lines, the `direct_light` split, the sun line twice, `path_emission` count 4,
  the camera-only line). `pt_gi_spatial` gives the same result as before (the same maths, moved).

## Offline numbers

`tgpt_gpu_test` (run 2, the build tested; exit 0, "all path tracing checks passed"). `python tests/source_checks.py`: 84
tests, OK. The plugin compiles (`dotnet build src\TGPathTracing -c Release` to `%TEMP%\tgb\mirror-managed`, 0 warnings,
0 errors).

The mirror check: the floor a perfect mirror (roughness 0, reflectance 1, black), the camera 1 m over it looking down at
the red wall (the room's warm lamp, the sun, the sky); the reference is the wall seen from the camera's mirror image under
the floor, pixel for pixel (143,758 pixels, the same wall points 0.0 mm apart). One ray a pixel, no reuse, clamps,
boiling filters or denoiser, 32 frames after 16. Bounces 1.

| | brightness | green/red | blue/red (of the wall's own) |
|---|---|---|---|
| now, Indirect Color 1 | 0.982 | 0.989 | 0.981 |
| now, Indirect Color 3 | 1.022 | 0.934 | 0.863 |
| as it was (shade_path), Indirect Color 1 | 0.864 | 0.703 | 0.610 |
| as it was, Indirect Color 3 | 0.794 | 0.825 | 0.750 |

At Indirect Color 3 the mirror comes out a little more coloured than the wall seen directly: the camera's pixel takes the
colour scale per noisy sample of its bounced light (one ray, no reuse here: a strongly coloured sample is held back so
that no channel goes negative), the mirror's on the radiance cache's smooth mean. With the game's reuse and denoiser the
camera's samples are smoother, so the gap should be smaller in game. The check allows 0.8 to 1.03 there.

Ray Reconstruction's guides at the mirror's pixels: specular albedo 0.999, diffuse 0.000, roughness 0.000, normal up
1.000; the reflections' hit distance over the way from the floor to the wall: median 1.000, within 5 % at 99.9 %.

The stand-in head: a box 30 cm across around a camera looking straight down into the mirror, the mirror's centre against
the picture with no box: 0.007 apart with the flag, 0.759 without.

Mutation checks (each broken on purpose, built into its own prefix, the full test run):

| Broken | What failed |
|---|---|
| A: the old shading (`shade_path` in `pt_specular`) | brightness and colour at Indirect Color 1 and 3; the metallic half lit as a metal |
| B: no Indirect Color at the hit | brightness (0.902) and colour (1.162, 1.205) at Indirect Color 3 |
| C: no pass of the first-person body | the box shows in the mirror (0.759) |
| D: no specular lobe at the hit | brightness (0.902) and colour (0.798, 0.746) at Indirect Color 1, and at 3 |

Two older checks follow the new rule (each passes with the change, and as it was):
- "the metallic half of a mask map is reflected with its specular color, not black" became "... lit as a metal, not black":
  in a reflection the metal half now gives 0.459 of the plain half's light (its highlights and its own reflection, no
  diffuse light), not the plain half's diffuse light (1.000 as it was). Bounced light still takes it as diffuse (1.000).
- "Reflection Boost leaves skin's reflections as the game made them": the skin floor's reflection moves 1.022 with the
  boost at 1, because what it reflects (the walls) now takes the boost as the camera sees them. The check allows 4 %; the same floor
  drawn as no skin moves to 0.44.

## GPU cost

Offline, the mirror filling most of a 1280x720 view of the test room (90 frames, the normal pipeline): the reflections
stage 0.38 ms with the old shading, 0.41 and 0.45 ms with `shade_seen` (two runs; other stages moved by as much between
runs, so about +10 to +20 % on reflections here). The extra work per reflection hit: the lobe at the light sample (no
ray), Indirect Color (no ray), and one more ray only where the hit is a smooth metal. In game the F8 report's
"reflections" line is the number that counts; most reflection hits are rough stone, wood and cloth, which add no ray.
`tgpt_gpu_test` run 3 (a clean rebuild after the mutations) passed with the same numbers as run 2.

## What the next harness run should show

Build, install and deploy this copy (TGPathTracing.Native.dll and the plugin together; no ABI change), then
`-Scenario mirror-tavern,mirror-day` and `python tools\harness\mirror.py <run>\<scenario> --frame`:

1. **The new table** ("Against the tracer's own view from the mirror's side"): brightness near 1, saturation near the
   traced reference's, shapes high. This is the mirror's own error. If brightness is near 1 there and the first table
   still says 1.5x (tavern), that 1.5x is the tracer's light against the game's, not the mirror.
2. **The first table** (against the game's picture): by day the mirror's saturation should drop toward the reference's
   (no egg: it was saturation 0.76 over a large patch; the highlights and reflections of the leather and wood now there).
   Brightness by day stays low where far things are (the fog along the reflected ray is still missing).
3. **The hero**: no egg above the collar in `mirror.jpg` (both scenarios); the body and arms as before. The log line
   "Characters: the hero's first-person body (material #N as #M) ..." once.
4. **Highlights in the mirror**: the iron bands and rivets on the tavern door, the hero's vest, wet or polished things
   should glint as they do seen directly.
5. **Cost**: the F8 report's or the harness's "reflections" stage time against run `20261007-023808`.

## Risks

- **Noise in reflections**: highlights at a hit are one light sample (eight candidates by their diffuse light): a sharp
  highlight on a smooth surface seen in a mirror sparkles more than the camera's own (ReSTIR's reuse). The denoisers take
  most of it; watch for fireflies on polished metal seen in mirrors and water.
- **A smooth metal at a hit traces one more ray** (with its own light sample at the next hit): more cost where mirrors or
  glossy floors show armour, pots and weapons.
- **Indirect Color in mirrors** at 3 is a little stronger than the camera's own (above); the owner may see mirrors a
  little more colourful than the room. The fix would be in the camera's path (scaling the bounced light after reuse).
- **The 0.3 m around the camera**: a skin hand within 0.3 m of the eyes vanishes from mirrors for that moment; a
  first-person body without a skin material is not flagged (the egg would show). Third person: nothing is flagged.
- **Eyes in mirrors**: off mirror-smooth surfaces (rougher than 0.15 no) reflections now meet eyes and unlit decals.
  Untested in game: an eye's sphere sticking out of a lid could show as a dot in a mirror where the game hides it.
- **What is not done**: the fog along the reflected ray (PT-37), HDRP's depth of field at the mirror's own depth,
  particles and transparents in reflections, hair's highlights at a hit, skin's light under the surface at a hit,
  water's reflections (`pt_water` still shows path-vertex shading, with its own multi-bounce paths), and see-through
  glass's view behind it (also path vertices).
