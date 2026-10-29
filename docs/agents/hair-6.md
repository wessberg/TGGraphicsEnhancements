# The tracer's own camera rays for every pixel, and hair inside the picture (PT-59, PT-53 to PT-57): agent "hair-6", 2026-10-07

Copy `scratchpad\wt\hair6` (base `wt\base32`). Scripts, logs and crops: `scratchpad\h6\`. Nothing here ran in the game.
The four claims, kept apart: **built** yes; **passes the offline harness** yes (below); **seen in game** no; **the owner
says it's fixed** no.

## In short

- **Done and handed over.** Every pixel's surface now comes from the tracer's own camera ray. Hair, beards and brows are
  ordinary surfaces of those rays. The hair layer is removed. HDRP's G-buffer is only compared against (the yardstick).
- **Offline:** all 1173 checks pass on the final build (native `4A4B4346A69F`). The new ones are judged without a
  denoiser (two more only make sure the built-in filters and NRD run on own-ray surfaces and keep the light). Eight
  mutants, each caught (table below). Source checks: 88 of 89; the one failing is the neural session's
  (`test_optional_hudless_is_not_fabricated`, also red on the base).
- **The in-game check for hair:** scenario `hair-moving`, recipe `hair-measure`, run with `-KeepFrames`. Read the
  "frame to frame" line of `hair_motion.py` (this copy's version), not the first line: the first one wraps.
- **Expect to see in the A/B, before any other gap:** things the game moves every frame that are not characters (the
  weapon in the hero's hand, doors) are traced where the plugin's last scene scan left them, up to 2 s old. With own
  rays that is what the picture shows. It is the first thing for PT-63 to fix. The hidden key turns the old route back on.

## The camera-ray design (PT-59)

**One sentence:** each pixel's own ray finds the first surface in the traced scene, that hit is the pixel's surface for
everything after, and HDRP's G-buffer is only compared against it.

1. **The ray** (`pt_primary.hlsli` `primary_walk`): from the camera's near plane through the pixel (HDRP's jitter, so it is
   the same sample HDRP drew), nearest hit in the whole traced scene. What covers part of a pixel stops the ray by chance,
   a new draw every frame: a hair card by its strands' alpha, a cut-out (leaves, fences, straw) by its alpha tile's
   coverage, a scalp mask by its alpha. Never a layer, never a fixed cut. It skips what the camera does not draw:
   shadow-only stand-ins, the back of a one-sided surface, the hero's stand-in head around the camera.
2. **The surface record** (`write_traced`, `write_hair`, `write_sky`): the same three textures `pt_prepare` filled from the
   G-buffer (position and distance, shading and geometric normal with roughness, colour and reflectance with flags), so
   nothing after it changes: direct and bounced light, reflections, filters, NRD, the guides. Plus two new ones: the hit's
   emission, and its motion from its own previous position (camera move; characters by their previous skinned positions).
3. **The yardstick** (`TGPT_VIEW_YARDSTICK`, a line in both logs, `TGPT_GetYardstick`): per pixel, the tracer's hit against
   HDRP's G-buffer in depth, normal, colour, roughness and motion; the shares that differ, by kind of instance and by
   material, with the materials' names.
4. **The switch**: `TGPT_OPTION_GBUFFER_SURFACES` (2147483648) is the old route; without it the tracer's own rays. The
   plugin's hidden key `[Advanced] Own camera rays` (default on) sets it.
5. **What still comes from HDRP, and why:** its depth and motion vectors of the frame SHOWN. The traced light is a frame
   old when it is shown (on purpose: waiting for it cost 6 to 8 ms), so only HDRP knows where things are in that frame;
   the composite, DLSS and Ray Reconstruction place the light by them. Nothing of HDRP's shades the picture.
6. **The cost to judge in the A/B:** a surface's colour detail used to be taken from the frame shown (HDRP's albedo, exact
   to the pixel); now it is part of the traced light, a frame old and moved onto the new frame. Textures may look softer
   or shimmer under movement. The cure, if it shows, is tracing the frame that is shown.

## Hair in that design

1. **HDRP draws the hair's depth, not its colour** (`CharacterCapture.HoldLayerDepth`). A head of hair and a beard keep
   their depth passes while tracing, at the game's own alpha clip (0.1), so HDRP's depth, normals and motion vectors at a
   hair pixel are the hair's, in the very frame shown. Hidden from HDRP (until today), a hair pixel had the wall's, and
   everything that places light (the composite, DLSS, Ray Reconstruction) moved the hair with the wall.
2. **The pixel's ray decides what it shows**: it stops at a card as often as the card covers; otherwise it goes on to the
   next card, the scalp, the wall or the sky. A pixel half covered is hair in half the frames. No card count, no blend.
3. **Light**: direct light about the strand, from either side, with the game's normal map and its two highlights (as the
   layer had, now inside `pt_di_spatial` and `rtxdi_shade`). Bounced light is ReSTIR GI's real rays from the hair
   (PT-54: the scalp's cache cell no longer stands in for it).
4. **Brows** stay hidden from HDRP (their shader has no opaque build that cuts). The pixel's ray finds them a millimetre
   over the skin, whose depth and motion are theirs. They are now inside the picture, so the denoisers see them (PT-55).
5. **Scalp and beard masks** should be HDRP's again (in its G-buffer, cut at 0.5 as before the layer), since the hair's
   renderer is drawn again. Not seen in game: look at a hairline. With own rays the mask stops the ray by its alpha.
6. **Guides**: the first card's colour, normal, roughness and specular albedo by its cover, steady from frame to frame.
7. **Lashes** are unchanged on the old route (opaque in HDRP's G-buffer); with own rays they stop the ray by their alpha.

## Where the drift really came from (the coordinator's question, 15:30)

1. **Most of the 16.9 px and 31.9 px was the measure.** `hair_motion.py` lines every frame up against frame 0 by phase
   correlation of the head box. That box is 537 px wide; a shift past 268 px wraps. Traced frames come slower, so the
   head moves 21 to 27 px a frame and crosses 268 px within the 16 frames; the game's moves 15 px a frame and does not.
2. **Frame to frame**, the same run (`20261007-144742`) gives:

   | step | head moves a frame | hair off the head, a frame | at most | change, levels |
   |---|---|---|---|---|
   | game | 15.0 px | 0.1 px | 2 | 2.49 |
   | traced, Ray Reconstruction | 26.7 px | 2.1 px | 14 | 2.15 |
   | traced, no denoiser | 21.3 px | 4.7 px | 16 | 3.50 |

   So the hair did still drift, 2 to 5 px a frame against the game's 0.1, and where it drifts it moves like the far wall.
3. **Why, with hair-5's composite fix in:** the "no denoiser" run still upscales with DLSS (1924x1084 to 3840x2160). DLSS
   and Ray Reconstruction accumulate along HDRP's motion vectors and reject by HDRP's depth; with HDRP's hair hidden, both
   were the wall's at a hair pixel. Not proven by a run of its own; the fix removes the cause, and the offline control
   below shows the same slide when the hair is taken out of the drawn depth.
4. **This copy's `tools/harness/hair_motion.py`** prints a second line per step, "frame to frame", and marks a wrapped
   head. Targets for the in-game run, from the game's own line: about 0.1 px a frame and 2.5 levels.

## The offline checks (all with Denoising None; ReSTIR's reuse on)

Final build: `%TEMP%\tgb\hair6-tested\TGPathTracing.Native.dll` (`4A4B4346A69F`), log `scratchpad\h6\gpu-d.txt`, pictures
`%TEMP%\tgb\hair6-images-d`. The older checks run the G-buffer route they were written for (their scenes hold things only
the traced scene has); the new ones run the tracer's own rays.

| check | number | bar |
|---|---|---|
| Hair 3 m away, wall 12 m, camera moved 10 cm between trace and show: squares where they are | 0.894 still, 0.885 moved | above 0.85, within 0.06 |
| the same with own rays for every pixel | 0.888 moved | the same |
| control: the same card hidden from the drawn depth | 0.672 | must be 0.15 lower (it slides) |
| Five cards of 0.3 each: hair's share of a pixel, one frame / mean of 16 | 0.820 / 0.841 (own rays 0.820 / 0.827) | the game's blend 0.832 |
| pixels that were now hair, now wall over 16 frames | 0.931 (own rays 0.952) | above 0.85 |
| Sixteen cards of 0.2: the wall's share | 0.031 | the game's blend 0.028 |
| Hair over a G-buffer surface: its red pixels red in the lit picture | 643 of 643 | above 85 % |
| Ray Reconstruction's guides over such hair: diffuse off the mean albedo | 0.018 | under 0.04 |
| GBuffer3 of the surface behind hair: the hair's gain | 0 % of the wall's | under 15 % |
| The yardstick over the test room: same surface / nearer / sky | 96.99 % / 0.06 % / 2.96 % | counts every pixel; view and numbers agree |
| camera moved 10 cm: own motion more than a pixel from the drawn vectors | 0.06 % | under 0.5 % |
| a cube only traced, a slab only drawn | nearer 1.10 %, farther 0.28 %; the cube's material named first | both found |
| A character moved 10 cm: its pixels with motion of their own | 1688 of 1688 (0 while it stood) | more than half |
| The picture, own rays against the G-buffer route, where the yardstick says "same" | x0.996, 4.5 % apart a pixel | within 10 % |

Also: `tgup_gpu_smoke --with-pt` (Ray Reconstruction on the D3D12 device, own rays) passes with this DLL. The plugin
compiles (`scratchpad\h6\plugin-check`). `tests/harness_analyze_test.py` passes, untouched.

**Thinly checked offline with own rays:** the tracer's built-in filters and NRD (only that they run and keep the light:
x0.974 and x0.977 of the undenoised picture, x0.997 after a 10 cm step of the camera; their own checks ran on the old
route). **Not checked with own rays:** skin, leaves, glass, water. The plugin's side (HDRP keeps the hair's depth, the
hidden key, the log line) cannot run offline at all: it compiles, and the first game run is its test.

**Mutants** (one line broken on purpose; each run ends at its first FAIL; logs `scratchpad\h6\gpu-mut-<name>.txt`):

| mutant | what is broken | the check that fails |
|---|---|---|
| `card-always-stops` | every card that shows at all stops the ray | layer hair covers the wall HDRP drew where its alpha is, the wall shows between its strands |
| `composite-refuses-own-texel` | the composite as before: a G-buffer pixel refuses the tracer's own texel | hair before a surface of the G-buffer shows as hair in the picture (the pixel takes its own ray's light) |
| `cut-at-half` | a fixed cut at half instead of the chance (hair held opaque, before the layer) | the diffuse albedo guide blends the hair's color over the wall's by the hair's cover, as the albedo view |
| `gbuffer-always` | the switch does nothing (the G-buffer whatever the option) | the yardstick finds what only the traced scene has (the tracer's hit nearer) |
| `no-own-motion` | a hit has no motion of its own | a still world's own motion under a moving camera is the drawn motion vectors' |
| `no-previous-positions` | a character's previous positions are not read | a character's pixels move by its own previous positions (the skinning's of the frame before) |
| `same-draw-every-frame` | the same draw in every frame | the diffuse albedo guide is the hair's own color where it covers the pixel, the wall's between its strands |
| `yardstick-blind-to-depth` | the yardstick does not compare distances | the yardstick finds what only the traced scene has (the tracer's hit nearer) |

Not covered by a mutant: the record's match distance (HDRP's at the pixel, not the hit's), and the second walk behind a
hair card HDRP drew. Their checks pass, but a mutant of either passes too (an empty square reads "not red" either way).

## The yardstick: how to run it and read it (for PT-63, "the whole world traced")

**What it is.** For every pixel, the tracer's own first hit compared with what HDRP drew there. It needs no setup:
it runs whenever the tracer's own camera rays do.

**Three ways to read it**

1. **The picture.** Debug View "Yardstick" (the last entry; `TGPT_VIEW_YARDSTICK` 11). It is also one of the F8
   capture's views. Its frames always use the tracer's own camera rays, whatever the "Own camera rays" key says.

   | colour | meaning | usual cause |
   |---|---|---|
   | dim green | same surface, read the same | nothing to do |
   | yellow | same surface, another colour (more than 0.1 in a channel) | a texture the tracer reads coarser, a decal, terrain layers, a tint the shader adds |
   | blue | same surface, another normal (more than 25 degrees) | a normal map not shared, parallax relief, a detail map |
   | cyan | same surface, another roughness (more than 0.2) | a mask map not read, a wet patch |
   | grey | same surface, HDRP drew it forward | nothing to compare (hair, eyes, some props) |
   | orange | the tracer's hit is nearer | the traced scene has something HDRP did not draw there; or a coarser mesh; or a thing in an older pose |
   | red | the tracer's hit is farther | the traced scene lacks what HDRP drew, or holds it coarser, or in another pose |
   | magenta | the tracer met nothing | the object is missing from the traced scene and nothing lies behind |
   | purple | the tracer met something where HDRP has sky | traced but not drawn (a hidden layer, a far level of detail HDRP culled) |
   | black | sky for both | |
   | whitened | its own motion is more than a pixel from HDRP's motion vector | a thing the plugin moved, wind sway, a character in its first frame |

2. **The log, with names.** `BepInEx/LogOutput.log`, every 10 s: `Yardstick 10s (...)`: the shares of the screen, then up
   to 16 materials with the most differing pixels, each with its name and shader, its share of the screen and the share
   of its own pixels that differ. A material at 100 % of its own pixels is wrong everywhere (missing texture, wrong
   pose); one at a few percent is wrong at its edges only (level of detail, cut-outs). The F8 report has the same line.
3. **The native log**, `TGPathTracing.Native.log`, every 10 s: `Yardstick (...)`: the same shares, by kind of instance
   (still, characters, swaying, moved, hair cards), and the materials by index (the plugin's line has their names).
   From code: `TGPT_GetYardstick(TGPT_Yardstick*)`.

**The tolerances** (`pt_primary.hlsli` `yardstick_code`): the same surface within 1 cm and 1 % of the distance; normal 25
degrees; colour 0.1 linear in any channel; roughness 0.2; motion 1 pixel. Counted every 30th frame (every frame of the
view), over every pixel.

**What it cannot see:** what HDRP draws after the opaque pass (water's own shading, particles, transparents, fog); and
colour, normal and roughness where HDRP drew forward.

## For the next agent: where the pass lives and how it is switched

- **The pass** is `pt_prepare.hlsl`; the camera ray and everything it writes are in `pt_primary.hlsli`:
  `primary_walk` (the ray), `primary_all` (a pixel, every pixel), `write_traced` / `write_hair` / `write_sky` (the surface
  record), `write_own` (motion and emission), `write_guides`, `yardstick_code`.
- **What it writes:** the surface record the G-buffer route wrote (`surf`: position and the distance the composite matches
  by; `nrm`: shading normal, roughness, flags, geometric normal; `mat`: colour, reflectance, flags), the hair texture,
  the guide outputs `g0`/`g1`, and three textures of its own in `FrameConstants.primary`: motion (x), emission (y), the
  yardstick (z).
- **The switch:** `TGPT_OPTION_GBUFFER_SURFACES` (2147483648) set = the old route. The plugin's hidden key
  `[Advanced] Own camera rays` (default true) clears it. `own_rays()` in `pt_common.hlsli` is the one test the shaders use.
- **Not read from a hit yet** (each is a gap the yardstick or the eye will show; the G-buffer route has them):
  skin (scattering profile, thin-skin transmission, the terminator's softening, cavity occlusion), a leaf's own
  transmittance and occlusion and its profile's reflectance, decals, terrain's blended layers, parallax relief.
- **Rays from an own-ray surface** still leave as from HDRP's (2 cm off, passing the inside of a mesh close by), because
  shadow-only stand-ins overlap the drawn geometry. `Surface.onMesh` (1 mm, nothing passed) is the exact way once
  stand-ins are told apart; hair and eyes use it already.
- **Motion:** a character's hit by its previous skinned positions (`MeshRecord.previous`, `pt_skin.hlsl`); everything
  else as still in the world. Missing: the previous transform of a thing the plugin moved, and of what the wind sways.
- **Speed:** the walk asks for every triangle on the ray (it must: to skip stand-ins and back faces, and instances
  are built with culling off). The `prepare` stage in the F8 report's "GPU time by stage" is its cost. Not measured:
  not in game, and the offline run's timing line is the old route's last frames (0.10 ms in a tiny room, no guide).

## What changed, file by file

- `native/pathtracing/shaders/pt_primary.hlsli` (new): the camera ray, the surface writers, the yardstick's word.
  `pt_hair.hlsl` deleted. `pt_yardstick.hlsl` (new): the counters.
- `pt_prepare.hlsl`: calls `primary_all` (own rays) or `primary_pixel` (old route: hair alone); clears the hair texture,
  the guides and the emission for every pixel.
- `pt_common.hlsli`: `own_rays`, `motion_at`, `strand_cos` / `surface_cos`, `hair_bump` (moved), `sky_through`, the
  yardstick's constants and colours, `MeshRecord.previous`, FrameConstants `motionViewProj` and `primary`,
  `g_primaryRays` in `alpha_opaque`, `Surface.strands`, `hdrp_material_at` off with own rays.
- `pt_compose.hlsl`: the yardstick's view, a hit's emission, the sky behind hair, steady guides.
  `pt_rtxdi.hlsli`, `pt_temporal.hlsl`, `pt_nrd_prep.hlsl`, `pt_hdrp_shadow.hlsl`, `pt_rrg_guides.hlsl`: motion from
  `motion_at`. `pt_di_spatial.hlsl`, `pt_rtxdi.hlsli`: `surface_cos`. `pt_skin.hlsl`: keeps the previous positions.
  `pt_meter.hlsl`: no layer.
- `d3d11.hlsl`: `hair_at`, `hair_guides` and their inputs removed; a G-buffer pixel takes the tracer's own texels.
- `tracer.cpp`, `scene.cpp`, `scene.hpp`, `runtime.hpp`, `extension.cpp`, `tgpt_api.h`, `pt_shaders.hpp.in`,
  `CMakeLists.txt` (two lines in the path tracing shader lists only).
- Plugin: `CharacterCapture.cs` (`HoldLayerDepth`), `NativeTracer.cs`, `Settings.cs`, `SceneCapture.cs`
  (`YardstickText`), `HdrpPasses.cs` (the 10 s line), `DebugCapture.cs` (F8).
- Tests: `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`. Docs: `CHANGELOG.md`, `docs/PROJECT-CONTEXT.md`.
  Tool: `tools/harness/hair_motion.py`.

**API (say-so as the brief asks):** new export `TGPT_GetYardstick` and struct `TGPT_Yardstick` (376 B); new option
`TGPT_OPTION_GBUFFER_SURFACES` (2147483648); new view `TGPT_VIEW_YARDSTICK` (11, `TGPT_VIEW_COUNT` 12). No existing
struct changed size. `TGPT_ABI` left at 0x00011E00: bump it at the merge. Material bit 16777216 was not needed.

## How to merge

- `merge_agent.py <copy> --base=wt\base32 --dry` lists the files. `CMakeLists.txt` was also edited by the neural
  session: my two hunks are in the path tracing shader lists (`pt_primary.hlsli` added to the shared dependencies,
  `hair:pt_hair` out of the pass list, `yardstick:pt_yardstick` into the list of passes without rays).
- Others' parts untouched: `pt_caustics.hlsl`, `pt_water.hlsl`, `pt_water_under.hlsl`, the water part of `d3d11.hlsl`.
  `pt_common.hlsli`, `tracer.cpp` and `tests/source_checks.py` are shared: merge by hand where they collide
  (FrameConstants is 7120 B here; add the caustics agent's rows on top and update the two asserts and the source check).
- Deploy the tested DLL itself (`-NativeFrom %TEMP%\tgb\hair6-tested\TGPathTracing.Native.dll`) or rerun the GPU test
  on the merged build.

## What the in-game run must show, and what to file

1. `hair-moving`, recipe `hair-measure`, `-KeepFrames`: the "frame to frame" line near the game's 0.1 px and 2.5 levels,
   with Denoising None first. If the hair still drifts: read `Characters: hair the tracer's own camera rays find keeps
   its depth in HDRP` in `LogOutput.log` (which passes were switched off), then the Yardstick view on the head: a hair
   pixel should be grey (same surface, drawn forward), not orange.
2. `eyebrows-flickering`: brows are inside the picture now; measure the brow box again with Denoising None, then RR.
3. `hair-glowing`: the crown takes real bounced light now; F8 on the crown, Indirect view.
4. The Yardstick line in `LogOutput.log` in each scene: it names what PT-63 has to trace.

To file (the coordinator numbers them): (a) things the plugin moves are traced as old as its last scan (first-person
weapon, doors); (b) not read from a hit yet: skin's scattering, transmission and terminator, a leaf's transmittance and
occlusion, decals, terrain layers, parallax; (c) no previous transform for moved things and wind sway, so their own
motion is the camera's alone; (d) colour detail is a frame old with own rays; (e) a walking character's hair gets
HDRP's object motion only if RealHair's MotionVectors pass runs for it while held opaque (not known: the log line lists
the passes; the tracer's own motion for it is right either way); (f) HDRP's hair depth is cut at the alpha clip, so a
particle behind a soft fringe is hidden by it.

## Ideas, not built

- Per-frame transforms for the instances the plugin already marks as moving (`TGPT_INSTANCE_MOVING`), with their
  previous transform for motion.
- `Surface.onMesh` for every own-ray surface once stand-ins are told apart from the geometry they overlap.
- Lashes and the game's soft outer hair layer as strand cards like the rest.
- The trace of the frame that is shown (no frame of lag): it would retire HDRP's depth and motion vectors too.

## State

- 15:50 design written. 16:40 job widened to every pixel. Run `a` 9 failures (the tests judged one frame of a picture
  that is now a mean of frames; the cube was out of view), fixed. Run `b` all pass. Shell and first-person fixes, run `c`
  all pass; two checks of the denoised paths added, run `d` all pass: the build handed over. Mutants run on run `b`'s source (the mutated lines are the same in `c`).
