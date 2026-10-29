# Grass lit by a fire boils (PT-93): the grass-boil worker's report

Written 2026-10-09 in the copy `wt\grassboil` (base `wt\base64`). Nothing deployed, nothing committed, the game never
started by this worker, no GPU test run. Build folder: `%TEMP%\tgb\grassboil` (NMake, NRD off: its configure writes
into the shared SDK folder; `pt_nrd_prep.hlsl` compiled by hand, see "Build").

STATE (kept current, 08:59): the trial is built in the copy and handed over. It compiles and the source checks pass
(see "Build" for exactly what compiled). **Not run on a GPU, not seen in game, no offline number of its own**: the
brief asks for the game first. Waiting for one run: `campfire@grass-product`.

## In short

- **The largest cause (read in the code, fits the kept frames; the game has not confirmed it):** a light's shadow ray
  flips a coin at every blade of grass it meets (`pt_common.hlsli` `alpha_opaque`: a `TGPT_MATERIAL_TRANSLUCENT`
  triangle lets a light's ray pass with a chance of one half). A ray from grass to a fire on the ground runs level
  through the tuft and meets many blades. With n blades in its way the ray arrives in one frame of 2^n and then
  carries all of the light. The mean is right. A single frame is black or 2^n times too bright.
- **The trial:** the same ray takes the PRODUCT of what each blade lets through. That is the coin's own mean, so the
  picture converges to the same thing, and there is no coin. Hidden setting `Advanced / Grass shadow trial`: 0 off
  (every ray as before), 1 on.
- **Why NRD does not hold it:** NRD never sees the direct light. See "Question 2".
- One switch only. I found no second cause of the same weight.

## The switch

| | |
|---|---|
| Name | `Advanced` / `Grass shadow trial` (hidden, an int) |
| Values | 0: off, the default, the picture as before. 1: on |
| What 1 does | The final shading's shadow rays (lamps, the fire, the sun, at camera pixels and at path vertices) multiply by what each blade of grass or leaf in the way lets through, in place of a coin at each blade |
| What it does not touch | ReSTIR's own yes-or-no visibility test (`rtxdi_visible`, used only with Direct Light Visibility on), hair, thin cloth, and the coverage coin of things without the game's own texture: all as before |

How it travels: `Settings.GrassShadowTrial` -> `TGPT_Frame.fogMip[3]` (the fog's `w`, which no caller filled) ->
bit 22 of `FrameConstants.wetGrid.w` (the lamps' trial number uses bits 0 to 21; a float holds 24) ->
`pt_common.hlsli` `leaf_product_on()`. No option bit taken, no size or ABI change. **If another worker also uses
`fogMip[3]` or bit 22 or 23 of `wetGrid.w`, one of us has to move: tell me which.**

The recipe for one run (`tests/ingame/scenarios.json`, added): `grass-product`, on `campfire`:

```
.\tools\harness\Run-PtHarness.ps1 -Scenario campfire@grass-product
```

| step | what |
|---|---|
| 1-game | the game's own picture, 4 frames |
| 2-nrd-coin-picture | NRD, the switch 0, the picture, 16 frames |
| 3-nrd-coin-direct | the same, the Direct view |
| 4-nrd-product-direct | the switch 1, the Direct view |
| 5-nrd-product-picture | the switch 1, the picture |
| 6-none-product-picture | no denoiser, the switch 1 |
| 7-none-coin-picture | no denoiser, the switch 0 |
| 8-off | everything back |

What to read: the speck spots a frame of steps 2 against 5 (459.5 before), the block difference of the Direct view of
steps 3 against 4 (6.81 before; the post beside it 0.15), the mean of the patch (must not move: see "Same mean"), and
`gpuMs` in each step's `step.json` (the cost: see "Cost"). The log says which is on: "Grass shadows on trial: ...".

What to expect if the coin is the cause: a large drop, not zero. What stays is real: the shadow ray aims at a new
point of the fire's disc every frame and so meets other blades (2 in one frame, 3 in the next: a factor of two, where
the coin's stake was everything or nothing). My guess from the table below is a quarter to a third of today's
frame-to-frame difference in the Direct view; a guess, not a number. If steps 3 and 4 come out alike, the coin is
not the cause and the next suspect is the reuse on blades (its samples' weights), which this switch does not touch.
Steps 2 and 3 run this build with the switch off: they must repeat today's 459.5 and 6.81, or the build itself
changed something.

## Question 1: what makes the direct light on a grass pixel differ from frame to frame

### What the code does today

`alpha_opaque` (`pt_common.hlsli`), for a light's shadow ray (`g_lightRay`) that meets a leaf or a blade:

1. With a chance of one half (`TRANSMISSION`) the ray passes. The random number is the ray's own for that triangle
   (`ray_chance`: the ray's origin, direction and the FRAME, and the triangle): a new coin every frame.
2. Otherwise the blade stops it where its texel is covered.

The ray then returns 1 or 0 (`transmittance_from_surface`), and the pixel's direct light is that times the light's
whole unshadowed strength (`pt_di_spatial.hlsl`: `localDiffuse = d * W * through`). So with n covered blades in the
way a pixel gets 2^n times its mean in one frame of 2^n, and nothing in the others.

| blades in the way | frames lit | one frame's spread about the mean | what a 16-frame history still shows |
|---|---|---|---|
| 1 | 1 in 2 | 1.0 times the mean | 18 % |
| 2 | 1 in 4 | 1.7 | 31 % |
| 3 | 1 in 8 | 2.6 | 47 % |
| 4 | 1 in 16 | 3.9 | 70 % |
| 6 | 1 in 64 | 7.9 | 143 % |

(Third column: the standard deviation of a coin that pays 2^n, sqrt(2^n - 1). Fourth: times 0.18, what an average
that takes a sixteenth of each new frame leaves of it. Arithmetic, not a measurement.)

A fire on the ground is the worst case: its light reaches the grass around it level, through the whole tuft. A lamp
overhead meets one blade or none. The post and the stones beside the grass have no blade in the way: 0.15.

The same coin sits on the path's vertices: bounced light gathered from grass asks the fire the same way
(`direct_light` -> `transmittance`), which is why the Indirect view jumps too (2.11), less.

### What the kept frames say (read only; scripts in `scratchpad\grassboil-work\`: `frames_stats.py`, `frames_flips.py`, `frames_crops.py`)

The grass box (x 3060 y 1532, 400 x 400), run `20261009-081146`, the Direct view, 16 frames:

- The box's mean stays put: 138.6, 138.3, 138.4, 137.8, 136.5 ... 136.5 levels (within 1 %). The light that
  arrives is the same every frame. WHERE it arrives changes: that is a lottery, not a light that flickers.
- Half of the box's pixels change; their values range over 56 levels (median), 116 (the top tenth).
- 46 % of those take more than one step of over half their own range within 16 frames. A history of 16 frames can
  move a sixteenth of the way to a new sample a frame, so such a step needs a sample many times the pixel's level.
- The post's box: 97.8 % of its pixels stay within 6 levels.
- The crop (`scratchpad\grassboil-work\direct-nrd.png`: frame 0, frame 1, the median, 8 x their difference, 8 x the
  spread): the grass is blotches a few pixels across that are elsewhere in the next frame; the posts are clean.
  Blotches are what the filter's two passes make of single bright pixels.

This fits the coin. It does not prove it: a rare bright sample from any other source would look the same. The run
with the switch is the proof.

### The other suspects

- **The fire as a light.** Not read as a cause. The shadow ray aims at a new point of the lamp's disc every frame
  (`pt_di_spatial.hlsl`, on purpose: a penumbra's noise that changes every frame). In grass that is a second, smaller
  lottery (which blades this frame's ray meets). It stays. With the product its stake is the difference between
  0.5^2 and 0.5^3, not between everything and nothing.
- **ReSTIR's reuse on thin blades.** Not read as a cause of the direct light's jump. By default the reuse carries no
  visibility at all (`biasCorrectionMode` Basic, `enableVisibilityShortcut` 0); the final shadow ray is traced fresh
  every frame, and that ray is where the coin is. On a blade the reuse across frames is not shuffled
  (`surface.s.complex`).
- **The cut-out's edge by a random number.** Only for things without the game's own texture (an alpha tile's
  coverage, `ray_chance(..., 0x616C7068u) < a`, also for the camera's own ray). The campfire's grass does not flip
  there: the Albedo view is still (0.06). The game's own cut-out is a plain cutoff.

## Question 2: why NRD does not hold it

1. **NRD is never handed the direct light.** `pt_nrd_prep.hlsl` gives RELAX the bounced light only. The direct
   light (and the reflections) go through the tracer's own filter in every mode, NRD included
   (`tracer.cpp` `denoiseOwn`: `pt_temporal` with at most 16 frames, then two `pt_atrous` passes, steps 1 and 2).
   So the jump of 6.81 is that filter's, not NRD's.
2. **The debug views never show NRD.** `useNrd` needs `debugView == TGPT_VIEW_NONE`. The "Direct view with NRD" and
   the "Indirect view with NRD" of `grass-views` are both the built-in filters' pictures. The Indirect view's 2.11
   says nothing about RELAX. (A finding for the harness: a view step named "nrd" is not NRD.)
3. **A 16-frame history cannot hold a coin that pays 2^n** (the table above). And the filter makes it worse to look
   at. `pt_atrous` lets a pixel take from its neighbours as far as its OWN noise explains the difference; in grass
   every pixel's noise is high (and a pixel with under 4 frames of history is given a high one on purpose:
   `pt_temporal`, `variance = max(variance, l * l * (4 - len))`), so a bright sample is spread over its neighbours:
   the blotches in the crop. The second pass writes them back into the history. My reading of why the picture with
   NRD jumps three to four times as much as the frame with no denoiser (1.43 against 0.39): with no denoiser a bright
   sample stays one ray pixel, and what is left of it in the kept frames does not change from one frame to the next
   (`frames_stats.py`: a pixel's rise is still all there a frame later), as an upscaler's own history after the
   tracer would hold it. Read from the frames, not from the code after the tracer: I did not read that part.
4. **What our passes hand RELAX for a blade's pixel**, and whether it flips on a still blade:
   - View depth: the blade's own, from the tracer's first hit. Still when the camera's shift is off.
   - Normal: the triangle's own, turned to the CAMERA (`fetch_hit`: `h.normal = dot(n, rayDir) > 0 ? -n : n`),
     never to the light. Still.
   - Roughness, motion: the hit's own; motion is 0 in a frozen world.
   - Hit distance: a constant 1 for every pixel (`RELAX_FrontEnd_PackSh(light, 1.0, ...)`). RELAX's diffuse path
     reads it only in its pre-pass, which is off, so this changes nothing. Noted so nobody takes it for a cause.
   - With the camera's shift ON a pixel's ray lands on another blade or on the ground in the next frame (rays at
     1924 x 1084 for a 3840 x 2160 picture: a blade is about one ray pixel wide). Depth and normal then differ and
     the history is dropped for that pixel, for our filter (`pt_temporal`: normals under 0.85 alike, or off the
     plane) and for RELAX alike. But the brief's own numbers say this is not the main thing: with the shift OFF there
     are MORE spots (562.6 against 459.5). So: no guide is wrong for grass. The input is.
5. **How long a history a blade's pixel can get:** the direct light 16 frames, the bounced light RELAX's 30 with a
   fast one of 6 that the long one is held to (its defaults; we set none of them). Neither is the limit here.
6. **The bounced light has the same coin** at every path vertex on grass, so RELAX is handed blotches too. Its
   firefly pass takes a pixel brighter than all eight around it down, but ReSTIR GI's reuse has spread such a sample
   over neighbours before RELAX sees it. The trial covers these rays as well.

## The trial, exactly

In `pt_common.hlsli`:

- `leaf_product_on()`: the switch.
- `alpha_opaque`: with the switch on, and only while one of the two final shadow-ray functions is tracing
  (`g_leafProduct`), a blade is counted (`g_leafMet`) and passed. Off: the old line runs, untouched.
- `leaf_pass`: what one crossing lets through: `1 - (1 - TRANSMISSION) * cover`, where `cover` is the alpha tile's
  coverage, or 1 or 0 by the cutoff for the game's own cut-out. This is the chance the two old coins let the ray
  pass, term by term.
- `leaf_walk`: the product. It meets each crossing once, nearest first, with a query of its own, as `glass_walk`
  does for glass. The visibility query cannot take the product itself: it may report a triangle twice (the
  geometry is built without `NO_DUPLICATE_ANYHIT_INVOCATION`, and a source check keeps it so).
- `transmittance`, `transmittance_from_surface`: multiply by it.

### Same mean

- The old coins are independent between triangles (`ray_chance` is keyed by the triangle), so the chance a ray gets
  through is the product over the triangles it crosses. That product is what the trial returns.
- Every caller multiplies light by the result (I read all eight call sites); none asks "is it above zero" to decide
  something else, but one: light that crosses a closed glass object takes a bent way in the straight ray's place
  (`caustic_through`), and that light was there only as often as the coin let the straight ray pass. It now takes
  the same share (`g_leafThrough`, 1 with the switch off).
- What the wind moves is met from the same distance on as in the old test (`SWAY_REACH`, the near part's end):
  `leaf_walk`'s `swayFrom` follows `occluded_from_surface_rays` line by line.

Three places where it is not exactly the old mean, all tiny, all named:

1. **One coin is left.** Once a ray's share has fallen under a thousandth (`LEAF_WALK_FLOOR`, ten blades deep), the
   walk goes on by chance, as often as keeps the mean. Its stake is a thousandth of the light, not all of it. Without
   it a sun ray through a tree's crown would walk hundreds of leaves. It changes noise, not the mean.
2. Two crossings nearer each other than a hundredth of a millimetre along the ray count as one (two copies of one
   plant in the same place would shade as one).
3. After a ray has passed the inside of a mesh near its start (the old near-surface rule), the old test skipped a
   millimetre behind that face; the walk does not.

### Cost

Not measured: no GPU run here. With the switch on, a ray that meets blades costs one more query for each blade it
crosses (up to about a dozen: see the floor), and it no longer stops at the first blade that would have stopped it.
Grass under a level light and sun through crowns pay; a ray that meets no blade pays nothing. Read `gpuMs` in steps
5 and 6 against 2 and 7. If the game says yes, the cheap way is the product inside the one query, which needs the
geometry built with `NO_DUPLICATE_ANYHIT_INVOCATION`: a decision for the coordinator (a source check forbids it today).

## Files changed (all in the copy)

- `native/pathtracing/shaders/pt_common.hlsli`: the trial (above); a comment on `wetGrid`.
- `native/pathtracing/src/tracer.cpp`: `fogMip[3]` into bit 22 of `wetGrid.w`; one log line when it changes.
- `native/pathtracing/include/tgpt_api.h`: the comment on `fogMip`'s `w`.
- `src/TGPathTracing/Settings.cs`, `src/TGPathTracing/HdrpPasses.cs`: the hidden setting and where it is sent.
- `tests/ingame/scenarios.json`: the recipe `grass-product`.
- `tests/source_checks.py`: a new check `test_path_tracing_grass_shadow_on_trial_default_off`; and one pin in
  `test_path_tracing_no_placed_lights` lost the word `const` (`float3 waterThrough = ...`: the value now takes the
  blades' share too; the pin still matches the old text).
- `docs/agents/grass-boil.md`: this file.

Line endings: the copy's sources are LF as the base's are (`tests/source_checks.py` CRLF); kept as found.

## Build

Four claims, kept apart:

- **Built:** yes, in `%TEMP%\tgb\grassboil` (script `scratchpad\grassboil-work\grassboil-build.cmd`: configure,
  `Build-PtShaders.ps1 -Throttle 8`, then `TGGraphicsBridge.Native` and `TGPathTracing.Native`). All 40 shader
  headers compiled, 0 failed (`grassboil-b3-shaders.log`), from the final text (last source edit 08:41:59, first
  header 08:42:46). `TGPathTracing.Native.dll` 08:58:25, SHA-256 `2361A21A7237...`. Nothing installed anywhere.
- NRD: the copy is configured with `-DTGPT_WITH_NRD=OFF` (NRD's configure writes into the shared SDK folder, which is
  outside the copy). So `pt_nrd_prep.hlsl` is not in that build; I compiled it by hand with the build's own DXC line
  (`scratchpad\grassboil-work\nrd-prep-compile.cmd`, exit 0, on the final text). **The NRD path of this DLL is
  untested and unbuilt here: the coordinator's merged build is the first with NRD in it.**
- Plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472 -o
  %TEMP%\tgb\grassboil-plugin`: 0 warnings, 0 errors.
- **Source checks:** `python tests/source_checks.py`: 100 of 100 pass (99 in the base, one new).
- **Run on a GPU: no. Seen in game: no.** No offline test was run, not even a small one (the brief: the game first).
  The coordinator's run is the first time these passes run at all, with the switch off as well as on. Steps 2 and 3
  of the recipe are the check that "off" is the old picture.

The shader passes took 908 s side by side here (`pt_specular` alone 870 to 906 s). The main tree's note says 10.5
minutes for all. I did not time the base's text in this folder, so I cannot say how much of the difference is the
trial's extra query, which every pass that asks a light now carries, and how much is other work on the PC.

I stopped my own build twice to change the shader text (a source check pinned two lines I had rewritten; then the
glass caustic's share, "Same mean"). No header of an earlier text is left: all 40 are newer than the last edit.

## Workarounds and gaps met (none removed: none became dead)

- The coin itself is the thing on trial. If the game says yes, the old line in `alpha_opaque` goes for the final
  shadow rays, with its setting, and `rtxdi_visible` needs a decision of its own.
- `TRANSMISSION` is one half for every leaf and blade ("a look, not a material's number", its own comment). The
  owner's shortcuts list wants each material's own value (PT-51 and the list's "leaves' 50 %"). Not this job; the
  product makes the swap easy (`leaf_pass` is the one place).
- The same lottery is still there for hair's strands, thin cloth's weave and every alpha tile's coverage on a light's
  ray (`ray_chance < a`): straw, fences, chains. Same cure, when wanted.
- The harness's view steps named "nrd" show the built-in filter (Question 2, point 2).

## After the game has said yes (not done: the brief says trial first)

- One small offline check in its own `..._ONLY` mode: thin cut-out blades between a lamp near the ground and a
  floor; the mean of many frames with the switch off against one frame with it on (equal within noise), and one
  frame's spread (far lower with it on). Mutants: `leaf_pass` returning 1; the walk counting a crossing twice.
- Then the speed (above), the clean-up (the old coin out), the docs.
