# Rain wets the traced world (PT-108, PT-104): worker "rain1", 2026-10-09

Copy `wt\rain1`, base `wt\base74`. Brief: `docs/agents/briefs/brief-rain-1.md`.

## State (kept current)

- 1. What the game does: READ. Every line of the pass's shader and its three C# classes. Written below.
- 2. The tracer: the FIRST PIECE is built: the wet roughness at every hit, for every ray, from the game's own
  moisture and its own noise texture. NOT built: the shelter (dry under a roof). See "Choices".
- 3. Switches: hidden `Advanced / Rain wetness` (0), and CMake's `TGPT_RAIN_WETNESS` (OFF).
- 4. Hand-over: below ("For the in-game run").
- Built: the plugin compiles (0 warnings, 0 errors). Source checks: 108 of 108 (107 in the base, one new).
  Native: see "Build state" at the end (kept current).
- **Run on a GPU: no. Seen in game: no.** The game is the test here.

## What the game does to a wet surface, exactly

Read from: the game's `TG.Main.dll` (ilspycmd, four types: ScreenSpaceWetness, PrecipitationController,
TopDownDepthTexturesLoadingManager, ScreenSpaceWetnessVisibilityService) and the pass's compiled shader
`TG/Weather/Rain/ScreenSpace_Wetness_2` in `resources.assets` (its pixel program disassembled, 190 lines, both
variants; its constant layout from Unity's own record of it). All of it outside the tree (`scratchpad\rain1-work`:
`dec\`, `sh\0\blob3.txt`, `shader_params.txt`); nothing of the game is copied here.

**The short of it: the pass changes ONE number per pixel, the roughness. It does not touch the normal, and it does
not touch the colour.** No ripples, no drops, no darkening. It reads the camera's normal buffer (normal + roughness),
writes the same normal back (decoded and encoded again: the same bits) and a new roughness. It has no stencil test
and no layer mask: every opaque pixel is treated, characters and skin too. It runs after HDRP's G-buffer and its
decals, so everything HDRP lights from the G-buffer (terrain, rock, wood, HDRP Lit, the game's Lit graphs, skin) is
lit with the new roughness. Things HDRP shades forward (hair, eyes, see-through things) compute their own shine and
are not changed by it.

What it reads:

| Thing | Where from | Value |
|---|---|---|
| Moisture, 0..1 | PrecipitationController, every frame | rises by precipitation x 0.25 per second while precipitation > 0, falls by `dryingRate` (0.05) per second when it is 0. Not saved: it starts at 0 after a load |
| Pass on or off | PrecipitationController | on while precipitation > 0.05 and no part of the game asked for it off (the lockpicking view does). Off = dry AT ONCE, whatever the moisture. The pass also does nothing while its shelter array is not there |
| Rain intensity | set on the pass | NOT USED by the shader at all |
| A noise picture | built into the shader | the game's `Tex_Noise_RGB_Clouds01` (256 x 256, BC1, its red channel), laid over the WORLD: one tile per 10 m of x and z, bilinear, repeating |
| The same noise, again | | laid over the SCREEN: screen position x 0.1 (the screen shows one tenth of the picture each way, always the same part). Locked to the screen (see "Choices") |
| Shelter heights | TopDownDepthTexturesLoadingManager | baked files `StreamingAssets/DepthTextures/<map>/depth_tex_X_Y.raw` (600 x 600 floats, 1.44 MB each; 148 for Horns of the South, 316 and 399 for the other two maps), streamed into a 4-layer float texture array around the hero. Each texel: how far below the map's top the highest surface is. Loaded only while precipitation > 0.05 |
| The surface | the camera's depth and normal buffer | world position, world normal, roughness |

What it computes, per pixel (smoothness = 1 - roughness, HDRP's "perceptual" ones, as the G-buffer stores them):

1. `noiseW` = the noise at world (x, z) x 0.1. `noiseS` = the noise at screen uv x 0.1.
2. How shiny a soaked surface is, by its distance from the camera: `soaked = clamp01(0.9 + min(dist / 100, 1) x
   (0.75 x noiseW - 0.9))`. So 0.9 right at the camera, fading over 100 m to 0.75 x noiseW (0 to 0.75). In the
   snow variant of the shader (keyword `_SNOWMAP` on the pass's material) the 0.9 is 0.33.
3. Puddle pattern from the moisture: `puddle = smoothstep01(clamp01(5 x (1.2 x moisture - noiseW)))`.
4. How much a surface faces up: `up = clamp01(normal.y) ^ 5`.
5. `wet = clamp01(clamp01(puddle x up + moisture) - 0.33 x noiseS)`.
   At moisture 1 every surface is wet whatever way it faces (walls, ceilings); at low moisture only flat ground
   where the noise is low.
6. `smoothness' = smoothness + wet x (soaked - smoothness)`. A blend, not an "at least": a surface shinier than
   `soaked` gets LESS shiny when wet (far away everything goes toward 0.75 x noiseW).
7. Shelter: `roof` = the height of the highest surface over this point (map top - what the array stores);
   `k = 1 - smoothstep01(clamp01((roof - y + 0.2) / (maxHeightDiff + 0.2)))`. The surface that IS the highest gets
   k of about 1 (0.996 with the default maxHeightDiff of 5 m); what stands `maxHeightDiff` or more under a roof
   gets 0.
8. `roughness' = roughness + k x ((1 - smoothness') - roughness)`.

The shelter array's reach: up to four chunks around the hero (those a square of 0.65 chunk around the hero
touches). **Outside them the shader does not say "wet" or "dry": it reads texel (0, 0) of layer 0**, whatever chunk
is there. An accident of the game's shader, not a rule (see "Choices").

Numbers at the save, once its moisture is 1: near the camera a dry gravel of smoothness 0.3 becomes 0.3 + wet x 0.6
with wet between 0.76 and 0.99: smoothness 0.76 to 0.89, roughness 0.11 to 0.24. That is the terrain's "98 %
another roughness" (the yardstick flags more than 0.2 apart).

**A surprise for the brief: the pass does not change the normal.** The terrain's "another normal 63 %" is not the
rain's, and this job will not move it. The game's picture of that ground has strong relief (big bumps, lit and
dark sides) which the traced one lacks; the hundreds of glints are the wet film ON that relief. With the rain
built, the traced ground will be shiny, and it will show glints only as far as its normals carry the relief. So
after this the terrain's normals are the next thing between the two pictures (`materials-2.md`: the mesh's own
tangents; or the terrain program's normal).

## What is built (the first piece)

`pt_common.hlsli` `rain_roughness` (the eight lines of steps 1 to 6 and 8 with k = 1), called last in
`hit_material`, after the decals, as the game's pass comes after its G-buffer. `hit_material` is the one place
every ray's hit gets its material: the camera's own ray (`pt_primary`), reflections, bounces, the water's paths,
glass's far sides. So a mirror shows the wet ground the camera sees. Not applied to what HDRP shades forward
(glass, eyes, hair) nor to unlit surfaces, nor inside a material light walks in.

What it reads, all from the game each frame, nothing from the screen:

- The moisture as the game's pass applies it: the plugin (`RainWetness.cs`) finds the game's
  PrecipitationController, reads its `_moisture`, and sends 0 while the game's pass is off (pass disabled, its
  volume off, or its shelter array not there: the same tests the game's own code makes).
- The soaked smoothness of the shader variant in use: 0.9, or 0.33 when the pass's material has `_SNOWMAP`.
- The noise: the game's own texture, found on the pass's material, opened by the tracer with no copy
  (`TextureShare`, followed when the game's streaming makes it anew), sampled at world x, z x 0.1 with the ray's
  footprint.
- The distance: from the camera to the hit, whatever ray asks (the mirror shows what the camera would see there).

How it travels: a record of its own, `TGPT_Rain` (64 bytes), through a new export `TGPT_SetRain`, kept with the
frame it is for (as the plants' wind is). **No field of `TGPT_Frame` and no option bit is used**: the brief asked
which spare field; none was free for five numbers and a texture, so the switch IS the record (moisture 0 = off).
In the shaders it is one new row at the end of the frame constants, `float4 rain` (8048 -> 8064 bytes; the five
places `tests/source_checks.py` pins that number are updated). `TGPT_ABI` is unchanged: the export is new, and the
plugin binds it only if it is there (a plugin with it still starts on an older native side).

Switches:

- Hidden `Advanced / Rain wetness`, 0 by default. 0: nothing is looked up, registered or sent.
- CMake `TGPT_RAIN_WETNESS`, OFF by default (as `TGPT_LAMP_AROUND`): OFF, no pass holds a line of it (shader
  define `PT_RAIN_WETNESS`), and the tracer's log says so if the setting is set. ON for the build that goes to the
  game.

Logs to read: the plugin's lines start with "Rain wetness on trial:" (what it found, the moisture, the noise's
handle); the native log's too (on, moisture, or why not).

## Choices for the coordinator (not taken silently)

**1. The shelter (NOT BUILT). The game's array cannot be had by the tracer as it is.** Why: a D3D11 texture can be
opened by the tracer's D3D12 device only if it was born with a sharing flag. The tracer's hook gives that flag only
to plain textures made for shaders to read (`texture_share.cpp` `shareable`, `plain`). The game's array is a
render texture with random write, four layers (TopDownDepthTexturesLoadingManager: 600 x 600, R32 float): the hook
leaves it alone, and the registry opens no arrays. Three ways, as a choice:

| | What | Data | Cost | Risk |
|---|---|---|---|---|
| A | Share the game's own array: the hook flags that shape, the start-up self-test proves it on the card, the registry opens an array | the game's own, no copy | 5.8 MB counted twice by Windows; about a day | no vendor promises the legacy flag on an array with random write; if the card refuses, back to B or C. The game also fills it only while precipitation > 0.05, six frames a chunk |
| B | The plugin reads the SAME baked files the game streams from (`DepthTextures/<map>/depth_tex_X_Y.raw`) and the tracer keeps the chunks round the camera in a texture of its own | the same bytes; and it reaches as far as rays do (9 chunks: 13 MB), where the game's array stops at 4 | 6 to 13 MB of video memory that the game also holds (a second copy of exact data, never resampled); about half a day: one export for float texels, 4 or 5 constant rows, 15 shader lines | it IS a copy in memory; the owner's "Don't do copies" was said of pictures made worse by copying, and of memory |
| C | A ray straight up at every hit that is shaded while it rains | none: the traced world itself, as it stands that frame (an open door, a moved cart) | one more ray per shaded hit while it rains: my estimate 15 to 25 % of the tracing time then (not measured); nothing when dry | differs from the game where its bake differs from the world (the bake's settings name a smoothing radius, 1 m by default, and the game has a "RainObstacle" layer of things no camera draws, which the bake may hold: I did not read the baker) |

My advice: B. It is the game's data at its source, needs no change to the hook, and gives reflections and bounces
the right answer beyond the game's four chunks. A if the owner wants no second copy at all. I built no ray.

Until then: with the switch on, **everything counts as under the open sky, indoors too** (a tavern while it rains
outside would be shiny). That is why the switch stays 0 and the run below is of the outdoor scene only.

**2. Outside the array's reach.** The game reads one arbitrary texel there (above): far ground is wholly wet or
wholly dry by that texel's height. No rule to copy. With B the real chunk is there; with A I would take "open sky"
beyond the four chunks and say so.

**3. The screen-locked noise.** Step 5's `noiseS` is the one thing in the game's pass that hangs on the screen: a
soft blotch that stays on the glass as the camera turns (the screen shows the picture's part from 0 to 0.1 each
way: red from 0.03 to 0.74, mean 0.454). Nothing in the tracer may hang on the screen, so the MEAN of that part
stands for it at every hit. The plugin measures it from the game's own texture (drawn small, read back, once a
texture and loaded level). The difference from the game: at most 0.14 of `wet`, 0.08 of roughness on dry gravel:
under the yardstick's 0.2. If the owner wants the camera's own pixels to match the game's blotch exactly, the
camera's ray could use its pixel's place and every other ray the mean: I did not build that (a mirror would then
show another wetness than the camera).

**4. The compile switch's default.** OFF, by today's rule (a trial costs frame time switched off). The build for
the run needs `-DTGPT_RAIN_WETNESS=ON`. When the game has answered and the shelter is in, this is no trial but what
the game's materials are in the rain: the option and the hidden setting should go, and the rain be read always.

**5. If the noise texture cannot be opened** (made before the hook, refused by the driver, no room in video
memory): the trial stays DRY and says so in the log ("cannot open the game's noise texture"), and asks again every
10 s. It does not pause path tracing: the plugin asks the native side first and hands the texture to
`TextureShare` (which would pause, as for a material's texture) only once it is open. When this stops being a trial
the project's own rule should hold for it too (no picture without it).

## The shelter's arithmetic, for whoever builds it

Read from the same shader and from TopDownDepthTexturesLoadingManager; in my words. To check in the game before
trusting: the two points marked (check).

- The world is cut into square chunks of C metres (GameConstants' `chunkTextureSizeInUnits`; the manager's private
  `_chunkTextureSizeInUnits`), counted from the ground bounds' lowest x and z (`GroundBounds.CalculateGameBounds`).
  Chunk (i, j) has the file `depth_tex_i_j.raw`: 600 x 600 little-endian floats, 1.44 MB. Horns of the South: a
  grid of 14 x 14, 148 of them with a file (the rest is outside the map: no file, never loaded).
- A texel holds a depth between 0 and 1 of a camera that looks straight down from the map's top (`CameraWorldPosY`,
  the bounds' highest y; near 0.01, far = the bounds' height). The files I opened hold 0.22 to 0.60.
- The roof's height over a point: `top - (near + (1 - d) x (far - near))`. The shader takes `d` or `1 - d` by the
  sign of HDRP's `_ProjectionParams.x`; on Direct3D HDRP's projection is flipped and that sign is -1, which gives
  `1 - d` (check: read one texel under the open sky and compare the roof with the ground's own height).
- Which texel: inside chunk (i, j), u = (x - boundsMinX - i C) / C, v the same with z; point filter. The game keeps
  at most four chunks in its array's four layers (those under a square of 0.65 C round the hero; the manager's
  `DepthTexturesLayers` names each one's layer: x the lower-left, y the lower-right, z the upper-left, w the
  upper-right; -1 not loaded) and hands the shader each one's offset in that square's coordinates. For a texture of
  the tracer's own (choice B) only C, the bounds' corner and which chunk sits where are needed. (check: that a
  file's first row is the lowest z: the game fills its array by a compute shader I did not read.)
- Then step 7 and 8 above, with `maxHeightDiff` (the manager's `MaxHeightDiff`).
- The plugin already logs these numbers of the game's array once, in its "wet" line (a chunk's metres, the array's
  size and format, the map's top and depth, which layers are loaded): the first run tells them.

## For the in-game run (the hand-over)

Build the main tree with the merge and `-DTGPT_RAIN_WETNESS=ON` (configure again, then `Build-PtShaders`, build,
install, deploy as always; every pass recompiles: `pt_common.hlsli` changed).

`.\tools\harness\Run-PtHarness.ps1 -Scenario spec-terrain -Recipe rain` (the recipe is in
`tests/ingame/scenarios.json`): the game's picture; no denoiser with `Rain wetness` 0 (14 s, 16 frames); no
denoiser with it 1 (24 s, so that a whole 10 s yardstick line is wet; 16 frames); off. Nothing more.

What to read:

1. `LogOutput.log`, "Rain wetness on trial: wet. Moisture M ...". If it says the pass is off, or the noise is not
   found or not open, the run shows nothing: the line says which. M is the game's moisture when the harness froze
   the world (it starts at 0 after a load and rises by precipitation x 0.25 a second: if M is well under 1, the
   game's own picture is only that wet too, and both sides agree on it).
2. The native log: "Rain wetness on trial: on, moisture ..." (not "compiled out").
3. The Yardstick lines of step 2 and step 3, for "Mat_UT_B_01_c_CampaignMap_HOS (TG/Environment/MeshTerrain)":
   - another roughness: **98 % in step 2; in step 3 I expect it to fall to under 15 %** if the dry terrain's own
     smoothness is right (what is left then is what the dry terrain is off by; if 30 % or more stay, the dry
     material is the next suspect, not the rain). The whole screen's 59.9 % should fall under 20 %.
   - another normal: **63 % in both. The rain does not touch it.**
   - another colour: 0 % in both.
   The other names of that line should fall with it: the wood (`Atlas_Wood_Construction_KF_02`, 73 %), the floor
   (`MI_Floor_01_HoS`, 93 %), the castle's trim (65 %), the grass (56 %). The run of 19:06 already fits the game's
   rule: what faces up differs nearly everywhere, the castle's WALL (`Mat_Castle_Wall_01`) on 12 % only, as a
   rule that wets by "faces up" would have it while the moisture is under 1.
4. The frames: the ground by the brazier shiny in step 3: the fire's orange streak sharper and longer, the stones'
   tops catching it. Hundreds of separate glints as in the game: only if the terrain's normals carry the game's
   relief, which they do not today (the 63 %).

If step 3's roughness share does not fall: the direction is wrong or the moisture is low; the two log lines tell
which.

## What is not built

- The shelter (choice 1). Indoors and under roofs the switch makes things wet that the game keeps dry.
- The game's `_RainIntensity` on its custom WATER materials (PrecipitationController sets it on water surfaces'
  materials: rain rings on water, likely): not read; the water is another job's.
- The rain itself (the drops: a VFX graph) and the rain's fog (a local volumetric fog whose density the controller
  sets): not the wetness pass's, not touched.
- No offline check: the brief asks for the game first. One that would be quick, later: a flat floor, the record
  set by the test, the first hit's roughness against the formula.

## Cost

- When it does not rain, or the switch is 0: nothing per hit (one test of a constant), nothing in the plugin.
- When it rains: one read of a 256 x 256 texture and about 15 arithmetic steps per `hit_material`. No ray.
- Compiled out (the default): nothing at all, the passes are as before.
- Memory: the noise texture opened (64 KB page). Frame constants 16 bytes longer.
- Not measured on a GPU.

## Struct and export changes (say-so)

- New: `TGPT_Rain` (64 bytes), `TGPT_SetRain`. `TGPT_ABI` unchanged. `TGPT_Frame` unchanged.
- `FrameConstants` (tracer.cpp, pt_common.hlsli): one row appended, `rain`: 8048 -> 8064 bytes, 46 single rows.
  `tests/source_checks.py` pins it in five places: updated. A merge with another job that grows the constants
  must add the two up by hand.
- `Engine.rain`, `FrameSlot.rain` (runtime.hpp), `State.rain` (tracer.cpp).
- New file `src/TGPathTracing/RainWetness.cs`; calls from `Session.Tick` and `HdrpPasses.Capture` (before the
  cloth's wind, not between the decals and the submit: a source check pins those three lines).
- `tracer.cpp` and `scene.cpp` (the memory worker's): tracer.cpp touched in four small places (the constants'
  row and size, the state's member, one block in `trace` before the highlight shoulder's, one line in
  `trace_event`); scene.cpp not touched.

## Workarounds met

- None removed. Met: `pt_wet.hlsl`'s wet shore (PT-52) is its own physical model (a film, darker ground); the
  game's rain does none of that. They do not overlap in code; where both apply, the shore's runs first and the
  rain's blend comes after, as the game's pass comes after everything.

## Files

`native/pathtracing/include/tgpt_api.h`, `src/extension.cpp`, `src/runtime.hpp`, `src/tracer.cpp`,
`shaders/pt_common.hlsli`; `CMakeLists.txt`; `src/TGPathTracing/RainWetness.cs` (new), `NativeTracer.cs`,
`Settings.cs`, `Session.cs`, `HdrpPasses.cs`; `tests/source_checks.py`, `tests/ingame/scenarios.json`;
this report. Research scripts (outside the tree): `scratchpad\rain1-work\*.py`.

## Build state (kept current)

- Plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472 -o
  %TEMP%\tgb\rain1-plugin`: 0 warnings, 0 errors.
- Source checks: `python tests/source_checks.py`: 108 of 108.
- Native (`%TEMP%\tgb\rain1`, NMake, no NRD, `-DTGPT_RAIN_WETNESS=ON`): configured; the shaders are compiling two
  at a time behind the other trees (started 19:32). NOT DONE YET when this line was written.
- The NRD path is not built here (workers build without NRD): the coordinator's merged build is the first with it.
  Nothing of this job touches NRD's files.
