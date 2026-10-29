# PT-50 agent: limits that change the picture (caps)

Copy: `wt/caps`, base `wt/base25`. Three agents: the first stopped at 10:08 on 2026-10-07 (a usage limit), the second
took over at 10:14 and stopped at a usage limit too, with the far lists' cache written and tested; the third (12:20)
checked it, rebuilt, ran the test and finished this report. Builds: `%TEMP%\tgb\caps-build4` and `caps-inst5` (this
copy, final), `caps-base-build` and `caps-base-inst` (the base). Helper scripts: `scratchpad\caps\` and
`scratchpad\caps\fin\`. State: done in the copy; passes the offline harness; not seen in game.

The owner's word for this job (2026-10-07): a budget may cost noise or speed, never change what the picture converges to;
"we should probably loosen them up": where removing a cap is cheap it goes, where it is a buffer's size or costly it is
raised past anything a scene in the game reaches.

## In short

- Gone: the three radiance clamps, the 25 cm floor on a lamp's distance, the caustic focus cap, the lamps' reach cut, the
  1,024 light count, the 20 m grass ring, the 100 m character ring, the 60 m rings (swaying cloth, hair, forward
  surfaces). NRD's 250 stays as what its buffer holds, but the light past it is added back, not thrown away.
- A lamp past its reach is now drawn from a "far list" (one more light candidate a sample), counted once wherever the
  point is: unbiased. The far lists are kept from one light grid to the next and made anew only when a light came, went,
  changed its kind or moved 4 m. They hold every such light whatever its position, and their weights only steer which
  light is drawn, so the cache can cost noise, never light (checked in the code at 12:20, and by the `farkind` mutant).
- What it costs: in a candle-lit night the clamps took 15 % of the picture's light; that light is back, and the boil
  there is half again as high (0.23 to 0.36 of an 8-bit level). Where the clamps did nothing, nothing changed. GPU time
  not measurable today (the GPU was busy); the render thread 0.2 to 1 ms more on a frame that remakes the light grid.
- Final: the tracer's GPU test passes (996 checks, none failed); 88 source checks pass; the plugin compiles. ABI: one new light flag
  (`TGPT_LIGHT_NO_RANGE`) and `TGPT_MAX_SKINNED` 64 -> 256; the coordinator sets the number (main is at 0x00011C00).
- Raised (buffer sizes): lights 65,536, glowing triangles 262,144, characters 256 renderers and 2M vertices, swaying cloth
  4,096 copies and 16M vertices, alpha tiles 1,024, vegetation 1,000,000 instances, glass crossings 64.
- Kept, each with a log line that says when a scene meets it: 4 water surfaces, 8 submeshes a character, 256 cookies,
  the glass index 1.5. Kept as they are, not limits: the glows' reach (paths count the rest), `MaxThingSize`.
- Not this agent's: `TRANSMISSION` (0.5, leaves) and `CLOTH_TRANSMISSION` (0.3). The subsurface scattering agent (`wt/sss`)
  replaces both with the material's own diffusion profile (the coordinator, 10:20); nothing of them is touched here.
- Found on the way, not on the owner's list, left alone and measured below ("Still holding light back"): the radiance
  cache holds a cell's light to 2,000 times the reference (the clamps hid it), and the denoisers and ReSTIR's reuse
  themselves hold back part of the light. Each wants its own backlog line.

## What the game reaches today (40 harness runs' native logs, 2,776 summary lines, the highest of each)

lights 985 (cap 1,024); emissive triangles kept 16,225 of a 16,384 budget, 36 submeshes over it; characters 64 renderers
(cap 64), 763k triangles; swaying cloth 96 copies (cap 96) of 240 thin cloths, 47,908 vertices; light grid at most 319
lights in a cell, 21 on average; vegetation (LogOutput) at most 1,224 instances (cap 60,000), 401 grass within 20 m.
So the light, emitter, character and cloth caps are reached in game today.

## The table

| Cap | Where | What it changed in the converged picture | What now | Cost |
|---|---|---|---|---|
| Lights 1,024 | `SceneLights.MaxLights`, `runtime.hpp kMaxLights` | the 1,025th light lit nothing (985 seen in game) | 65,536 (a slot list; light indices stay under the emitters' 2^20) | none until used; the grid and the light buffer grow with the lights there are |
| Lights only within Scene Radius + reach of the camera | `SceneLights.Tick` | a lamp past that lit nothing in the traced scene | every loaded light is sent; an effect's light (windowed) still only where its range reaches the scene | plugin: one record a light a frame (CPU, not measured offline) |
| Lights cut at their reach (where they fall under 50 lux, 40 m at most) | `SceneLights.Reach`, `pt_common.hlsli light_incident` (HDRP's smooth window at `range`) | past it a lamp lit nothing | the light falls off with distance alone (`TGPT_LIGHT_NO_RANGE`); the reach now only says how far the light grid lists it as near; past it the far lists sample it. An effect's light keeps HDRP's window at its range (the portal's two 131,072 cd lights, owner-confirmed) | GPU: one more candidate a sample (8 near + 1 far), a binary search in the cell's list. Render thread: see "GPU and CPU cost". Memory: 721 lists x lights, 8 bytes an entry |
| Light grid box (384 x 160 x 384 m round the camera) | `scene.hpp kGridDims`, `light_cell` | a point outside it got no lamp light at all | outside the grid every light is drawn from one list by its power | noise only there |
| Glowing triangles 16,384 | `scene.hpp kEmitterBudget` | noise only by design (a path that meets one counts it whole), but the radiance clamps cut those rare bright paths, so their light went; the game filled the budget (36 submeshes over) | 262,144 | buffers grow with what a scene has: 80 bytes a triangle, two frames' records |
| Each glow cut at its reach (2 lux, 40 m) and glows under 0.25 left to paths | `scene.cpp kEmitterReachLux`, `kEmitterMinIntensity` | nothing: paths count the rest (`emission_weight`), unbiased; with the clamps gone this holds in practice too | kept | none |
| Characters within 100 m, 64 renderers, 524,288 vertices | `CharacterCapture`, `TGPT_MAX_SKINNED`, `runtime.hpp kSkinRows` | a character past them cast no shadow and showed in no reflection (64 renderers reached in game) | the Scene Radius row (250 m), 256 renderers, 2M vertices | skin texture 32 MB a frame parity (64 MB, was 16 MB); GPU: see "GPU and CPU cost" |
| 8 submeshes a character | `TGPT_SKINNED_MATERIALS` | a 9th part would be missing from the traced character | kept (a struct of the ABI); the plugin logs a renderer with more, once, for the evidence | none |
| Vegetation 60,000 instances; grass and plants within 20 m | `LeshyCapture` | past 20 m grass cast no shadow and showed in no reflection | as far as Leshy draws them (its own LOD distances, within Scene Radius); 1,000,000 | in game more grass instances (not measurable offline): the harness's GPU ms on `vista`, `npc-day` say |
| 4 water surfaces | `TGPT_MAX_WATER`, 2 bits of instance flags | a 5th water surface in reach would not be traced as water | kept: the water agent's area, and the constants hold 4 slots; the plugin logs once when more are in reach (no harness log has such a line yet: the line is new) | none |
| Swaying cloth 96 copies within 60 m, 1M vertices | `scene.hpp kMaxSwayCopies`, `kSwayReach`, `kSwayVertexBudget` | the 97th cloth was traced still while HDRP swayed it (96 reached in game) | every swaying cloth in the traced scene; 4,096 copies, 16M vertices as buffer bounds | one BLAS update and a small dispatch a copy a frame |
| Hair layer within 60 m | `pt_hair.hlsl HAIR_REACH` | hair HDRP no longer draws while tracing vanished past 60 m | as far as rays go (Ray Distance) | a longer ray on the strands' mask only |
| Hair 8 cards deep | `pt_hair.hlsl HAIR_LAYERS` | nothing since the strands agent's change (PT-01): cards past 8 are taken by chance, the mean right | kept: speed only | none |
| Forward surfaces within 60 m; their traced shadows | `pt_prepare.hlsl`, `pt_hdrp_shadow.hlsl HDRP_SHADOW_RANGE` | past 60 m HDRP shaded them ("No HDRP shading while tracing") | at any distance; a card the traced scene lacks still keeps HDRP's look | one short ray a forward pixel past 60 m |
| 8 glass crossings | `pt_specular.hlsl SHEETS` | past 4 jars' walls the next surface showed with no glass color | 64 (a loop bound; it stops at the first surface that is not glass) | only where that much glass is |
| 256 alpha tiles | `runtime.hpp kMaxAlphaTiles` | the 257th alpha-tested texture tested alpha from its 64x64 color tile (blocky straw, leaves) | 1,024 (atlas 4096 x 16384); the plugin logs once if it fills | 64 MB (was 16 MB) |
| 256 cookies | `runtime.hpp kMaxCookies` | none seen: 13 cookie textures in all 40 harness runs | kept | none |
| Radiance clamps 48 / 8 / 4 | `pt_restir.hlsli LIMIT_*` | cut every sample brighter than 48, 8 or 4 times the scene's reference: the light of rare bright paths (a vertex by a lamp, an emissive, the sun in the sky map) | gone: only NaN and infinity are dropped (`finite_radiance`) | see "Clamps" below |
| 25 cm floor on a light's distance at path vertices | `direct_light` | everything right by a candle bounced as if 25 cm away | gone: I / d^2 to the light's own size | noise |
| Caustic focus at most 16 times the straight light | `pt_common.hlsli CAUSTIC_MAX` | a lens's bright line was cut | gone (the glass ball's spot in the sun: 13.5 times the open sun before, 18.2 now) | noise |
| NRD's 250 | `pt_nrd_prep.hlsl NRD_LIMIT` | bounced light over 500 times the median was cut | NRD still gets at most 250 (its half floats hold no more), the rest is added back undenoised in the composite | noise (a bright speckle stays one) |
| The radiance cache's 2,000 a sample | `pt_common.hlsli cache_add`, `CACHE_SAMPLE_MOST` | hidden behind the radiance clamps until they went (they held a sample to 64 times the reference); without them a glow over 2,000 times the reference (a magic crystal in a cave lit by one candle: over 7,600 nits beside a 12 cd flame) loses the rest in every path that ends on the cache. The test's furnace with such a lamp comes to 0.79 where 1 is right | kept: 31 times looser than the clamp that hid it, and what a cell's 32-bit sums hold (512 samples x 2,000 x 4,096). See "Still holding light back" | none |
| The composite's 256 | `pt_compose.hlsl`, `pt_hair.hlsl rr_clamp` | a pixel's light past 256 in exposed units (8 stops over white) is held there: no display shows the difference; bloom of such a pixel is a little smaller | kept: the bound the upscalers' and Ray Reconstruction's inputs are given | none |
| Glass index 1.5 | `pt_common.hlsli GLASS_IOR` | none known: 1.5 is HDRP Lit's default `_Ior`, and the game draws its glass without refraction | kept; the plugin logs each glass material's own `_Ior` for the evidence | none |
| Leaves let half through, thin cloth 30 % | `TRANSMISSION`, `CLOTH_TRANSMISSION` | path vertices and shadow rays use the grey shares where the camera's pixels take the profile's | not this agent's: the subsurface agent (`wt/sss`) | |
| A glowing thing at most 6 m across | `LightRules.MaxThingSize` | not a light budget: it decides whether an HDRP light stands for a glowing thing (kept) or is a fill light (left out, "no fake lights"); the glowing surface itself is traced as an emitter whatever its size | kept | none |

## How the lights work now (the far lists)

A light the plugin keeps (every one but an effect's) carries `TGPT_LIGHT_NO_RANGE`: `light_incident` gives it no window at
its range, so it falls off as 1 / d^2 forever, as a real light does. Its `range` (the plugin's `Reach`: 50 lux, 40 m at most,
or HDRP's range if farther) now only says how far the light grid lists it as a near light. Every place that samples lights
for shading (`pt_di` for the camera's ReSTIR DI, `direct_light` at path vertices, `pt_hair`) takes up to 8 candidates from
the point's cell, as before, and one more from a far list (`light_lists`, `lists_candidate`): the point's coarse cell (4 x 4
x 4 cells, 32 m) lists every no-range light that is far from part of it, weighted by its power over its squared distance;
outside the grid one list holds every light by its power. A far candidate that the point's own cell lists weighs nothing
(a binary search in the cell's list, whose entries are in light order), so each light is counted by exactly one of the two
at every point: unbiased however far the light is, and the far candidate costs noise, never light. Glass's mirrored lamps
(`reflected_lamps`) go through both lists too. The emissive clusters stay near-only (their light past their reach is the
paths', unbiased already). The scene's lights' weights keep a floor (a thousandth of the mean), so a light that was dark
when the grid was built (one fading in) is still drawn.

## Clamps

What they took away (offline, each in a scene that shows it; "before" is the base's tracer):

- A night lit by eight candles standing on the floor, each flame 5 cm over it (new in the test's denoiser study,
  `nrdStudy`, "night, candles on the floor": the clamps' own case, half a candle's light lands within centimetres of it).
  The old tracer under today's test (`TGPT_TEST_BEFORE_PT50=1`, two runs): the whole picture's light 0.1247 with the
  clamps and 0.1472 with them off (`TGPT_OPTION_NO_CLAMPS`), so the clamps and the 25 cm floor took 15 % of the picture's
  light, the directly lit floor included. Today 0.1469 and 0.1472: the option changes nothing any more.
- A glowing quad over a floor, Emissive Lights off (only paths find the glow): the floor had 0.222 of its light with the
  clamps, 0.976 now (what is missing is ReSTIR's boiling filter).
- A candle 3 cm over wax: its top 98.5 with the clamps, 105.6 without and now. The red wall's bounce beside it
  (Ray Reconstruction's extra color): 0.451 before, 0.555 now. A glass ball's focus in the sun: 13.5 times the open
  sun before, 18.2 now.
- NRD's 250 is not reached in any scene of the study once ReSTIR has reused the samples (NRD's picture holds 0.98 of
  the built-in denoiser's light with the add-back and without), so the add-back is held by a source check alone.

What their going costs in flicker, measured as the in-game harness measures a frozen world (`nrdStudy`: boil is how much
an 8x8-pixel block's mean changes from frame to frame, flicker the same for single pixels, both in 8-bit levels of the
picture as shown; 16 frames, the camera still, the game's defaults, one ray a pixel). The same test on both tracers:

| Scene | Denoiser | Boil before | now | Its 95th percentile before | now | Flicker before | now |
|---|---|---|---|---|---|---|---|
| candles on the floor | built-in | 0.23 | 0.34 to 0.36 | 0.42 | 0.67 to 0.69 | 0.27 | 0.38 to 0.39 |
| candles on the floor | NRD | 0.22 | 0.31 to 0.32 | 0.39 | 0.60 | 0.26 | 0.35 to 0.36 |
| sun | built-in | 0.205 | 0.205 | 0.382 | 0.382 | 0.230 | 0.230 |
| night, 25 torches on the walls | built-in | 0.278 | 0.278 | 0.486 | 0.485 | 0.338 | 0.337 |
| night, 25 torches on the walls | NRD | 0.264 | 0.264 | 0.447 | 0.448 | 0.337 | 0.337 |

So where the clamps did nothing to the picture (the sun, torches on walls) nothing changed, to the third digit. In
their own case the boil is half again as high as it was, and still a third of one 8-bit level on average (two thirds at
the 95th percentile). Ray Reconstruction is not run offline: the harness's boil and flicker numbers on `tavern-room` and
`glow`, and the owner's eye, say what it makes of it.

If it boils in game, the ways to calm it that keep the light, in the order of what they cost:

1. A flame as the sphere it is. `light_incident` floors a lamp's distance at its own radius (plus 1 cm); a light that
   stands for a flame has the flame's size from the plugin (`TGPT_LIGHT_AT_EMITTER`). Lit as a sphere (its solid angle
   sampled), no point by it is brighter than the flame itself: the rare path that ends by the wick is bounded by physics.
2. More samples where the light is: a bounce ray aimed at a lamp's lit surroundings as often as its light deserves (the
   light grid knows the lamps near a point), weighted against the cosine rays by their probabilities. The bright spot
   under a candle is then found by every pixel, not by one in a thousand.
3. Longer or wider reuse where ReSTIR's weights vary a lot: a bright sample is shared with its neighbours at its full
   weight instead of standing alone.
4. In the denoisers: a pixel far brighter than its neighbourhood held to the neighbourhood's range with the part taken
   off spread over the neighbours, so the block's light stays. RELAX's own firefly suppression takes it off and drops it
   (see below).
5. More rays a pixel (the Rays row): the plain way, at the GPU's cost.

## Still holding light back (found, measured, not changed)

None of these is on the owner's list; each changes what the picture comes to, a little.

- The radiance cache holds a cell's light to 2,000 times the reference radiance (`CACHE_SAMPLE_MOST`: a cell's light is
  its sums over at most 512 samples a frame, in 32 bits). The radiance clamps held a sample to 64 times, so this was
  never met before. The test's furnace with a lamp so dim that the walls glow 6,000 times the reference comes to 0.79
  where 1 is right (a number in the test, not a check). In game it takes a glow of some 7,600 nits beside nothing
  brighter than a 12 cd candle, or a surface within 2 cm of a very small lamp. Tried and taken back out: a brighter
  sample at the most with a chance (its mean is then below the most: the furnace came to 0.76). The ways out: a smaller
  `CACHE_SCALE` or fewer samples a frame (both cost noise in dim cells), or a reference radiance that counts the scene's
  brightest glow.
- The denoisers. The frames' mean light against the undenoised reference's, whole picture: in the sun's room the built-in
  denoiser 0.94 and NRD 0.92 to 0.93 (undenoised 0.995); with the torches 0.94 and 0.91; with the candles on the floor
  0.83 and 0.81 (0.87 and 0.85 with the clamps; undenoised 0.956, the 16 frames missing a few of the rare bright paths
  the reference's 1,280 rays a pixel hold). The filters weigh a pixel far brighter than its neighbours down and do not
  give its light to them. RELAX's own firefly filter (`enableAntiFirefly`) is part of it: turned off, NRD keeps 0.831 of
  the candle-lit night's light instead of 0.813 (the built-in denoiser 0.832), its boil 0.325 instead of 0.311. So it
  takes 2 % there; the rest is the filters' blending itself.
- ReSTIR's reuse: the candle-lit room with the game's reuse holds 0.934 of the light of the same paths with no reuse at
  all; 0.939 without the boiling filters, 0.942 with the exact Jacobian too (0.929, 0.931, 0.933 with the clamps). So 6 %
  is held back by the reuse itself, not by its filters.
- The composite holds a pixel to 256 in exposed units (`rr_clamp`, 8 stops over white): see the table.
- With a light 40,000 times the brightest in view far away (the study's last scene) the built-in denoiser keeps 0.31 of
  the room's light and NRD 0.66: the reference radiance is set by a light that lights nothing in view, and the stored
  light's precision goes. Old, and the NRD agent's finding; here only because the same column shows it.

## Material numbers

- `TRANSMISSION` (0.5) and `CLOTH_TRANSMISSION` (0.3): the subsurface scattering agent's (`wt/sss`), which replaces both
  with the material's own diffusion profile. Not touched here, so the two copies do not clash in `pt_common.hlsli`.
- `GLASS_IOR` (1.5): not changed. It is HDRP Lit's default `_Ior`, and the game draws its glass without HDRP's refraction,
  so no material of its own is known to differ: the number is the material's own unless an artist typed another into a
  field the game's rendering never reads. The plugin now logs each glass material's `_Ior` (`GlassFinish.Add`), so the
  next harness run says. If one differs: `TGPT_Material.textureReserved[0]` is free for it (the material record is
  uploaded as it is, `MaterialRecord.textureReserved` in the shaders), and eight lines read `GLASS_IOR`: four in the
  caustics (`pt_common.hlsli`, which have the glass's hit and so its material) and four in `pt_specular.hlsl`, two of
  which start from the camera's pixel and have no material there (a channel for it in `pt_prepare`'s material bits, or
  one ray). That is the cost, and the jars agent works in the same lines.
- `MaxThingSize` (6 m): a rule for which lights stand for something, not a light budget (see the table).

## Files changed

Native: `include/tgpt_api.h`, `src/runtime.hpp`, `src/scene.hpp`, `src/scene.cpp`, `src/tracer.cpp`; shaders
`pt_common.hlsli`, `pt_restir.hlsli`, `pt_di.hlsl`, `pt_di_spatial.hlsl`, `pt_gi.hlsl`, `pt_hair.hlsl`, `pt_hdrp_shadow.hlsl`,
`pt_nrd_prep.hlsl`, `pt_compose.hlsl`, `pt_prepare.hlsl`, `pt_probe.hlsl`, `pt_specular.hlsl`, `pt_water.hlsl`.
Plugin: `SceneLights.cs`, `NativeTracer.cs`, `CharacterCapture.cs`, `LeshyCapture.cs`, `SceneCapture.cs`,
`WaterCaustics.cs`, `GlassFinish.cs`. Tests: `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py` (a new
`test_path_tracing_no_limits_that_change_the_picture`, and the checks that pinned the old values). Docs: this file,
`CHANGELOG.md`, `docs/PT-REQUESTS.md` (PT-50's state, and what was found on the way).

## ABI and bits

- New: `TGPT_LIGHT_NO_RANGE` (a `TGPT_Light.flags` bit, 0x80000, next to `TGPT_LIGHT_AT_EMITTER`; the managed
  `Tgpt.LightNoRange`). No other copy has a light flag there (checked `wt/sss`, `wt/lights2` and the main tree at 10:20:
  theirs end at `TGPT_LIGHT_AT_EMITTER` 0x40000). No option bit and no material bit is added.
- Changed in meaning: `TGPT_MAX_SKINNED` 64 -> 256 (a count, no struct changes), `TGPT_OPTION_NO_CLAMPS` now does nothing
  (its bit stays taken; the test still passes it and checks that it changes nothing). The light grid's cell buffer holds
  the far lists after the cells (the shaders' and the native side's own layout, not the plugin's).
- So the ABI number needs its next step at the merge (the new flag; a plugin that sends it to an old tracer gets windowed
  lights, an old plugin with this tracer gets windowed lights too: nothing breaks either way). No number picked here.
- The test reads one new environment variable, `TGPT_TEST_BEFORE_PT50=1`: today's test on a tracer from before (no alpha
  fillers), for the before numbers of the candle scene. And `TGPT_TEST_FAR_LIGHTS=1`: the study's torches fall off with
  distance alone (for timing the far candidate).
- No export changed and no struct changed its layout. The copy's `TGPT_ABI` is the base's 0x00011B00; main is at
  0x00011C00: the coordinator picks the next number at the merge.

## How to merge

- Merge against `wt/base25` file by file (the list under "Files changed", plus `docs/PT-REQUESTS.md`). Skip the `obj/`
  folders under `src/` (leftovers of the plugin's compile check) and `docs/agents/caps.md` goes in as it is.
- Where other copies touch the same lines: `pt_common.hlsli` (the subsurface agent's `TRANSMISSION` lines are not touched
  here; the jars agent's glass lines sit beside `GLASS_IOR` and `CAUSTIC_MAX`'s old place), `pt_specular.hlsl`
  (`SHEETS`), `pt_hair.hlsl` (the strands agent's `HAIR_LAYERS` change: keep theirs, this copy only drops `HAIR_REACH`),
  `SceneLights.cs` and `tgpt_api.h` (light flag bits: `TGPT_LIGHT_NO_RANGE` is 0x80000; another agent's new light flag
  must take another bit), the water agent's `pt_water.hlsl` and `WaterCaustics.cs`.
- `tests/source_checks.py`: the new `test_path_tracing_no_limits_that_change_the_picture`, and the old checks that pinned
  the clamps, the 60 m rings and the old counts were changed to the new values; a merge that keeps an old check fails it.
- After the merge: build, `tgpt_gpu_test`, the source checks; then the in-game harness (below) before the owner looks.

## GPU memory

- Skin texture: 4096 x 512 RGBA32F, 32 MB a frame parity, 64 MB in all (was 16 MB).
- Alpha atlas: 4096 x 16384 R8, 64 MB (was 16 MB).
- Light grid: the far lists, 720 coarse cells + 1, an entry (8 bytes) for each light far from each coarse cell: with the
  game's 985 lights about 5.7 MB at most (the near lists as before: 21 lights a lit cell on average).
- Emitter records, sway copies, character BLASes: grow with what a scene has, as before; the raised bounds allocate
  nothing up front.
- In all, 96 MB more allocated up front (the two textures).

## Offline checks (mutation-checked)

The final state, built into `%TEMP%\tgb\caps-build4` and installed to `caps-inst5` (12:25): `tgpt_gpu_test` passes, 996 checks ok, none failed, "all path tracing checks passed",
EXIT 0 (`%TEMP%\tgb\caps-test-z-clean.txt`, its pictures in `caps-images-z-clean`).
`python tests/source_checks.py`: 88 checks pass. The plugin compiles (`dotnet build`, 0 warnings, 0 errors).

GPU mutants (each a tracer built with one thing broken, `scratchpad\caps\mutate.py` and `fin\mutate2.py`, the test run
on it; "caught" means the named check failed):

| Mutant | What it breaks | Result |
|---|---|---|
| `nofar` | no far candidate: a lamp stops where the grid stops listing it | caught: "a lamp's light goes on past the light grid's reach for it (the far lists)" |
| `twice` | a far candidate also counts a light the cell lists (counted twice) | caught by the same check |
| `lights1024` | the native light count back at 1,024 | caught: "the 1,101st light is traced (no cap of 1,024)" |
| `chars64`, `skin128` | 64 character renderers; the skin texture's old 524,288 vertices | both caught: "the 128th character is traced" |
| `alpha256` | the alpha atlas back at 256 tiles | caught: "alpha-tested texture registered for the alpha atlas, the 301st" |
| `floor25` | the 25 cm floor at path vertices again | caught: "no radiance clamp is left: the candle-lit room holds the same light with and without the old clamps' option" |
| `clamp4` | a clamp at 4 times the reference on every sample again | caught by the same check |
| `farkind` | the kept far lists not made anew when a light changes its kind | caught: "the far lists follow a light that changes its kind" (run on the final code, 11:38) |
| `noaddback` | NRD's light over 250 cut again, not added back | built; no GPU check can catch it (its run's output was not kept): no scene in the study reaches NRD's 250 once ReSTIR has reused the samples. A weak spot, held by a source check alone (that one caught its mutant) |
| `nofirefly` | RELAX's own firefly filter off | a study, not a check (passes, as meant): the numbers are in "Still holding light back" |
| `emit16k`, `sway96`, `caustic16` | the old emitter budget, sway count, caustic cap | written, never built or run. The emitter one has a check that should catch it (a 33,800-triangle glow is sampled); the other two are held by source checks only |

The far lists' "moved 4 m" rule has no GPU mutant on purpose: a light that moved less keeps its old weight in the far
list, which changes how often it is drawn, never what it gives, so the only thing a test could see is noise.

Source-check mutants (`fin\mutate_checks.py`, the result in `fin\mc.txt`): 18 of 19 caught by
`test_path_tracing_no_limits_that_change_the_picture`. The 19th (`LeshyCapture.cs` `float reach = radius;`) was not
applied: the line is there twice, so the script skipped it. The 20 m grass ring coming back is still caught by the other
Leshy mutant (a `GrassDistance = 20f` constant).

## GPU and CPU cost

GPU, before and after: not measured to better than the day's noise. Other programs and other agents' tests used the GPU
all morning: the same frames of the base took 5.7 ms in one run and 56 to 80 ms in the next. What was taken (the
denoiser study alone, three runs each of the base, this copy, and this copy with the torches falling off with distance
alone, in turn; 1280 x 720, one ray a pixel; `scratchpad\caps\fin\run4.ps1 -Runs s-base1,s-caps1,s-far1,...`, then
`gpums.py` prints the least of each):

- The sun's room (one windowed lamp: the same work in all six runs of this copy): the base's quietest run 5.8 ms a frame
  with the built-in denoiser and 5.7 with NRD, this copy's 6.9 and 6.3. So at most about a millisecond more, and
  possibly nothing: no two runs were equally quiet, and in the room with a far light this copy's least was under the
  base's (5.1 against 6.1 ms).
- The torches' room: the base 6.0 and 5.8 ms; this copy 10.1 and 8.8 in its quietest run with the torches windowed, and
  7.3 and 8.5 with them falling off with distance alone (`TGPT_TEST_FAR_LIGHTS=1`: the far candidate). The far candidate
  measured cheaper than its absence: the runs' noise is larger than whatever it costs.
- Characters, from the whole test: 128 renderers of 8,192 triangles posed every frame cost 0.47 ms in this copy's
  quietest run; 64 cost the base 0.32 ms in its. In step with their number.
- What decides it is the in-game harness's GPU ms on `vista` and `npc-day` (grass to Leshy's distance, every cloth
  swayed, more characters): none of that can be made offline. Take the GPU numbers again when the GPU is quiet.

The render thread (CPU), the light grid with 1,000 lamps that fall off with distance alone, one of them moving half a
metre every frame so that the grid is made anew every frame (the whole test, medians of 20 frames; the base's tracer
under the same test):

| | a frame's recording, the grid made anew | the slowest | no lamp moving |
|---|---|---|---|
| base | 1.65 ms | 2.25 ms | 0.20 ms |
| this copy | 1.88 to 2.73 ms | 4.46 to 5.13 ms (the far lists made anew too) | 0.18 to 0.32 ms |

(1.88, 4.46 and 0.18 are the final run at 12:30.) So a frame that makes the grid anew costs 0.2 to 1 ms more (the kept far lists, 5.8 MB with 1,000 lamps, copied behind
the cells and uploaded with them), and one that makes the far lists anew 2.5 ms more on top. In game the grid is made
anew on a quarter of the frames (256,941 builds in 1,037,990 traced frames, the harness's native log, up to 985 lights):
a range or a position changed a little. That is why the far lists are kept from grid to grid and made again only when a
light came, went, changed its kind or moved 4 m (`kFarStale`): the first agent's version made them with every grid, 9 ms
for 985 lights in a stand-alone timing of its loop (`fin\farbench.cpp`; 1.9 ms for 250, 40 ms for 4,000). Their weights
only steer which light is drawn, so a light that moved less is still counted right. What is left to take off, without
changing the picture: a buffer of their own for the far lists, so that they are uploaded only when made.

## What the next harness runs should show

`-Recipe quick` on `tavern-room`, `vista`, `glow`, `npc-day`:
- `tavern-room`: the room a little brighter where candles stand by the table and walls (no 25 cm floor, no clamps: the
  bounced light by each flame is all there), and the far corners lit by lamps past their old reach (dim, now not zero).
  More speckle in the traced picture with NRD than before (what NRD can't hold comes back undenoised); RR's look decides.
  Native log: `lights=` may pass 1,024 in big scenes; `emissive: ... over the budget` should be gone. The render thread's
  ms in the summary line against the last run: candles flicker, so the light grid and its far lists are made anew often.
- `vista`: grass past 20 m in the traced scene (its shadows on the ground, where they stopped at a ring 20 m out); swaying
  cloth past 60 m in its wind pose; forward-drawn surfaces past 60 m shaded by the tracer; GPU ms higher by the grass (the
  one cost not measurable offline). LogOutput: the Leshy line ("as far as Leshy draws them"), and none of "Alpha atlas
  full", "Water: N water surfaces", "draws N submeshes" (each is the evidence a remaining cap is met). The "Glass:" lines
  now end with each material's own index of refraction: any that is not 1.5 is the evidence for wiring it through.
- `glow`: emissive things keep their full light (no clamp on the rare path that meets a small bright glow); the sampled
  glows unchanged.
- `npc-day`: characters past 100 m cast shadows; hair past 60 m stays on heads; GPU ms a little higher with many people.
- Boil and flicker numbers in `report.md` against the last run: the clamps' removal is the one change that can raise them.

## Risks

- Flicker: the clamps held rare bright paths down; without them a bright path's light stays in ReSTIR's reservoirs and the
  denoisers' history for a few frames. The offline numbers are in "Clamps" above; the owner's eye in game decides.
- Forward surfaces past 60 m: a camera ray's window there is 0.03 + 1 % of the distance (60 cm at 60 m, 2.5 m at 250 m);
  a far forward-drawn thing the traced scene lacks could take the shading of what the ray meets within that window (the
  ground behind it). Not testable offline: look at far hair, far glass and anything odd on the horizon in `vista`.
- The far candidate adds noise where most of a point's light comes from lamps past their near reach (a dark field lit by a
  distant village): one candidate drawn from up to every light by its power over its distance from the coarse cell.
- A lamp outside the Scene Radius is sent and lights the scene, but the walls around it are not traced: its light arrives
  unshadowed by them (1,000 cd at 250 m: 0.016 lux).
- Grass to Leshy's own distance: in a meadow, many more instances (the GPU ms of `vista` and `npc-day` say how many).
- Sending every loaded light: one record a light a frame on the main thread (985 today; a big town could hold more).
- `TGPT_LIGHT_NO_RANGE` is a new light flag bit: a merge with another agent's light flag must keep them apart.

## Not changed, and why

- Scene Radius (250 m) and Ray Distance (500 m) are the player's own rows, not fixed limits.
- The glass index and the two transmission shares: see "Material numbers".
- 4 water surfaces, 8 submeshes a character, 256 cookies: buffer or struct sizes no harness scene is known to meet; each
  now logs when one does.

## Ideas, not started

- Speed without changing the picture: a buffer of its own for the far lists, uploaded only when they are made (today
  they ride behind the grid's cells and go up with every grid); the grid itself made on a worker thread.
- The GPU cost, measured again when the GPU is quiet: the in-game harness's GPU ms on `vista` and `npc-day`.
- Tests that are weak today: a GPU check for NRD's add-back (a scene that passes NRD's 250 before reuse calms it), and
  the `emit16k` and `sway96` mutants run on the GPU.
- If the candle-lit boil shows in game: the five ways to calm it in "Clamps", the flame as a sphere first.
- Each item of "Still holding light back" as its own PT-REQUESTS entry (the cache's 2,000, the denoisers' share, ReSTIR's
  reuse): the coordinator gives them IDs.
