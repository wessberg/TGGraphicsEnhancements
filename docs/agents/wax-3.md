# Candle wax: bluish white where the game's is cream, and the brown foot (PT-40): agent "wax-3", 2026-10-09

Copy: `scratchpad\wt\wax3` (base: `wt\base66`). Brief: `docs/agents/briefs/brief-wax-3.md`. Build folder:
`%TEMP%\tgb\wax3` (NMake, NRD off). My scripts (never in the repo): `scratchpad\wax3work\` (`configure.cmd`,
`build.ps1`, `plugin.ps1`, `gpu.ps1`). HDRP's public source as read (never in the repo): `scratchpad\wax3work\hdrp\`.

## State (kept current)

- Step 1 (the units): ANSWERED, section 1. **The units are right. No factor of ten or a hundred.**
- Step 2 (a trial): BUILT, section 3. One hidden switch, `Advanced / Subsurface reading` (0 off, 1 on).
  The plugin compiles (0 warnings, 0 errors). Source checks: 101 of 101.
- Step 3 (the foot): the same switch covers it (section 4). No second switch.
- Nothing is seen in game. Nothing deployed, nothing committed, the game never started.
- Built: native, 09:51 (`%TEMP%\tgb\wax3`, NRD off; `TGPathTracing.Native.dll` SHA-256 DFCE1202...2A40E825; all 40
  shader passes and the GPU test). No shader file was touched after that build.
- Run: my own offline checks alone (`TGPT_TEST_WAX_READING_ONLY=1`), twice: section 5. Run 1 (09:57): the
  tracer's numbers right, two of my own expectations wrong. Run 2 (10:25, expectations corrected, the same
  tracer): **passes, exit 0.** Nothing else was run on the GPU: no whole test, no ctest.
- The coordinator replaced `tools\Build-PtShaders.ps1` in this copy at about 09:55 (it waits for room on the PC).
  That file's change is the coordinator's, not this job's.
- **Cost, before anyone runs it: reading 1 is slow as built.** About 10 times the walk's time on skin, 12 to 21
  times on the snow wax, on the pixels it touches (section 5). Fine for a look in one scene; not shippable as it is.
- **The one run only the game can give:** `.\tools\harness\Run-PtHarness.ps1 -Scenario
  candle@subsurface-reading,npc-candle@subsurface-reading` on a build with this copy merged (section 3 says what
  to look at).

## In short (plain words)

1. **The units are right.** A profile's distance is in millimetres. The tracer makes the same free path of it as
   Unity's own path tracer does. For the candles: 29, 32 and 71 mm, in candles 3 to 8 cm thick.
2. **The wax is clear because the artists' number says so, and the game never reads that number as a substance.**
   The candles use the SNOW's profile (28, 31, 58 mm; skin's is 0.3 mm: a hundred times less). The game's own
   shader only smears the light sideways along the surface and then multiplies by the wax's colour. It never lets
   light out the other side, and it never asks how thick the thing is. So in the game any wax is cream.
   Read as a substance that fills a 5 cm candle, the same number is thin ice: light goes through after one to
   three bounces, hardly tinted. Shown in the wax's cream colour it comes out bluer than white.
3. **There is a second truthful reading: the game's own.** Not a tint, not a dial: the same sum the game's shader
   computes, with the material's own numbers, but the points found with rays on the real meshes (hidden sides and
   off-screen included) and the light at them traced. In it a closed candle and the open shell at its foot are the
   same wax. That is the trial: `Subsurface reading = 1`.
4. **Measured offline, the same cream stick with a white lamp behind it** (section 5, check 3):
   as a substance it shows blue 1.07 times red: bluer than white. In the game's own reading 0.78: the wax's cream
   (0.78). And about half as bright (0.37 against 0.68 to 0.94 of the lamp's light on a white surface).
   So the game's reading still lets light through a thin candle, by the profile; less of it, and in the wax's colour.
5. **Which one is right is the owner's call, and the game shows both in one run** (recipe `subsurface-reading`,
   scenes `candle` and `npc-candle`).
6. **Reading 1 is slow as built** (10 to 21 times the walk's time on the pixels it touches) **and has one limit I
   could not remove** (section 3, "what it is not yet"). It is a trial for the look. If the owner likes the look,
   both are the next job.

## 1. The units (step 1): right, with the numbers

Read: HDRP 17's own source, branch `6000.0/staging` of Unity-Technologies/Graphics
(`DiffusionProfileSettings.cs`, `SubsurfaceScattering.compute`, `LitPathTracing.hlsl`, `PathTracingBSDF.hlsl`,
`Raytracing/Shaders/SubSurface.hlsl`). Learned, nothing copied.

What a profile's numbers mean in HDRP:

| the profile's value | what HDRP's source says | unit |
|---|---|---|
| Scattering Color x Multiplier = "scattering distance" d, per channel | the shape of Burley's profile: R(r) = (e^(-r/d) + e^(-r/3d)) / (8 pi d r). The shader's S is 1 / d | millimetres (the blur pass measures r in millimetres: "convert from world units to millimeters") |
| World Scale | "Size of the world unit in meters": millimetres per world unit = 1000 x world scale | metres per unit |
| Max Radius (the filter radius) | the r that holds 99.7 % of the profile: 16.6 x the largest d | millimetres |
| the material's subsurface mask | in the blur pass it DIVIDES the millimetres per unit (a mask of 0.5 halves the blur's reach); in HDRP's own path tracer it is the chance of taking the walk | 0 to 1 |

What HDRP's own path tracer makes of them (its random walk for the same materials):
free path = 0.001 / (S x world scale) x s(A) world units, with s(A) = 1.9 - A + 3.5 (A - 0.8)^2 and A the surface's
colour. In words: d millimetres, times a factor between 1.04 (white) and 1.7 (a colour of 0.5).
(It also stops a walk after 16 steps and shows the plain surface then. The tracer's walk has no such stop.)

What the tracer makes of them: the plugin sends S x 1000 x world scale (`HdrpPasses.ScatteringOf`), the shader
divides by the same s(A) (`pt_common.hlsli subsurface_medium`). **The same free path as HDRP's own path tracer, to
the last factor.**

For the candles (`Mat_Prop_Candles_02`, `MI_Candle_01`, `Mat_Prop_Candles_01`: HDRP/Lit, `DiffusionProfile_Snow`,
mask 1; run `20261009-071155`, `LogOutput.log` line 981):

| | red | green | blue |
|---|---|---|---|
| the profile's distance d | 27.72 mm | 30.57 mm | 57.76 mm |
| the free path the game's values stand for (for a wax colour of 0.90, 0.85, 0.70: the job before's test colour; in the game it is the wax's texture, and the factor stays between 1.04 and 1.7) | 28.7 mm | 32.4 mm | 71.3 mm |
| the free path the tracer uses | the same | the same | the same |
| how far the game's own blur moves light on average (2.5 d) | 69 mm | 76 mm | 144 mm |
| the game's blur radius (16.6 d) | | | 0.96 m |

The game's other profiles, from the same log: skin 0.30, 0.16, 0.09 mm; teeth 0.67; zombie skin 1; slime 0.11.
**The snow's is 100 to 600 times any of them.**

For skin: its walk moves light by tenths of a millimetre, less than a pixel of a face at 4K (0.2 mm). **So skin's
look (too dark, orange: PT-73) is not a matter of the walk's units either**: on skin the walk and a plain surface
show the same picture but for the grain.

## 2. Then why is the game's wax cream and ours clear?

**What the game's shader does** (HDRP's subsurface pass, read in its source): for a pixel x of a scattering material

    shown(x) = colour(x) x  sum of R(distance from x to y) x light(y)  /  sum of R(distance from x to y)

The sum runs over the pixels y around x on screen that are also scattering material. R is the profile, at the
straight distance between the two points. Three things follow, and each is a reason for "cream":

1. **The colour is multiplied on at the end, whatever happened to the light.** The profile only says how far light
   spreads. So wax is always its own colour times some light.
2. **The weights are divided by their own sum.** No light is ever lost through "the other side", and a thin thing
   and a thick thing are treated alike. The model has no thickness at all: under every point the wax goes on for
   good.
3. Only what is on screen is in the sum. (A fault of doing it on screen, not a wish of the artists.)

**What the tracer does today** (and Unity's own path tracer): it reads the same two numbers as a substance. The
colour becomes "how much light a deep block of it sends back", the distance becomes the free path. Both readings
agree for anything much thicker than the distance: skin, teeth, a snow field. They part where the thing is THINNER
than the distance, and the game's candles are (3 to 8 cm of a 3 to 7 cm substance):

- light crosses the candle in one to three bounces, so it is hardly tinted (the tint was worked out for a deep
  block, where light bounces dozens of times);
- about half of the light that falls on a side leaves through the other sides;
- the only tint that light gets is what it picked up on those few bounces, which is little. Next to the cream the
  wax is drawn in everywhere else, it reads bluer than white. (My offline check 3 puts a number on it: section 5.)

So the candles are not wrong for a substance with these numbers. The numbers are not a wax's (real wax scatters
within about a millimetre). They are a blur radius that looked right in the game's own picture, where they could
never make a candle clear.

**The corner's light does the rest** (the job before measured it; nothing of mine): the tracer has the flames
(orange, 2500 K) and daylight from the left. The game also has white fill lights at the camera, which the tracer
leaves out by the owner's ruling. Clear wax pipes the white daylight from its far side to the camera's side, so the
candles are white and bright where the wall is orange and dim. The foot is an open shell: no inside, a plain surface,
cream times orange flame light: brown. **That is the whole of "white candles on a brown foot": the candles carry
daylight through, the foot cannot.**

**The truthful traced reading of such a material: there are two, and they are both honest.**

| | reading 0: a substance (today) | reading 1: the surface's (the trial) |
|---|---|---|
| whose reading | Unity's own path tracer's | the game's own shader's |
| what the profile is | the free path inside the thing | how far light spreads along the thing's surface |
| where the colour comes from | absorbed on the way, bounce by bounce | the surface's own, as drawn |
| a thing thinner than the distance | clear, pale: light crosses it hardly tinted | its own colour; its sides share their light by the profile at the straight distance between them (a lit far side shows on the near side, a dark far side takes its share), and nothing is lost or made |
| light through a 4 cm cream stick from a white lamp behind (measured, section 5) | 0.68, 0.73, 0.94 of the lamp's light on a white surface: shown bluer than white | 0.37, 0.37, 0.37: shown cream |
| an open shell (the foot) | no inside: plain; or with the job before's part 2: a film that shows the rock | the same wax as the candle |
| skin, teeth (thick against the distance) | the same picture in both | the same picture in both |
| what it gets wrong | the artists' wax is not a substance with these numbers | physics: a 5 cm candle is not endlessly deep |
| a tint or a dial? | no | no: the game's formula, the material's own numbers |

Reading 1 is not "making it look like the game's picture". It takes no colour and no number from that picture. It is
the game's material model (which every scattering material in the game was authored against), with its sum taken
over the real surfaces by rays where the game can only use the screen.

## 3. The trial (step 2)

**The switch: `Advanced / Subsurface reading`** (hidden, an integer). 0: off, the picture as before (the default).
1: a diffusion profile is read as the surface's, for every scattering material: wax, skin, teeth.

How it travels: bits 22-23 of the trial number the plugin already sends (`TGPT_Frame.jitter`'s w, the small-lamp
switches' word: its two spare bits). **No option bit, no struct, no size, no ABI number.** The native clamp of that
number went from 22 to 24 bits. The tracer's log says the reading whenever it changes
(`Subsurface reading on trial: 1, the surface's ...`).

**What reading 1 does, in the shader** (`pt_common.hlsli subsurface_walk`, the same function and the same ONE
trace call as the walk; `subsurface_hit`):

- Where a ray (the camera's, a bounce's, a reflection's) meets a scattering material, the point the light is taken
  at is not found by a walk inside the mesh. It is a point of the SURFACE, drawn with a chance that goes exactly as
  the profile at its straight distance. Then the light there is gathered as before (lamps, sun, one ray for the sky
  and the bounce), in the entry point's own colour. Because the point is drawn by the profile itself, the light
  needs no weight, and the weights "add up to one" by construction: no light made, none lost.
- How the point is drawn: a line through a random point of a disc around the entry (the disc's radius from the
  profile), along the entry's normal or one of its two tangents. Every scattering surface the line crosses gets its
  exact chance (the profile at the straight distance over the profile at the line's distance, evened out for how
  the surface faces the three lines). A line that yields no point is drawn again.
- Every scattering material counts, as in the game's pass (wax next to wax of another material shares light).
- A sheet (an open shell, a card) is a surface like any other: lit on the side it was met on.
- One colour channel at a time (the three channels reach differently far, and each must add up to one by itself).

**Files changed** (against `wt\base66`):

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_common.hlsli` | `subsurface_reading()`, the comment block above it, `subsurface_hit` (one line), `subsurface_walk` (the surface's branch) |
| `native/pathtracing/src/tracer.cpp` | the trial number's clamp (24 bits), the log line |
| `src/TGPathTracing/Settings.cs` | the hidden setting |
| `src/TGPathTracing/HdrpPasses.cs` | `SubsurfaceTrial`, added to `frame.Jitter[3]` |
| `tests/ingame/scenarios.json` | the recipe `subsurface-reading` |
| `tests/pathtracing_wax_reading_checks.inl` (new), `tests/pathtracing_gpu_test.cpp` | the offline checks; the test sends the trial number (`trialNumber`, 0 everywhere else) |
| `tests/source_checks.py` | one new check; the clamp's quoted line |
| `tools/candle/surface_sampler_check.py` (new), `tools/candle/README.md` (one line) | the CPU check of the point's drawing on a cylinder |
| `docs/agents/wax-3.md` | this report |

That is every file that differs from `wt\base66` (compared file by file, line endings evened out), but one:
`tools\Build-PtShaders.ps1`, which the coordinator replaced in this copy while I worked. Not mine: merge it or not
as the coordinator's own change.

**The recipe** (`tests/ingame/scenarios.json`, `"subsurface-reading"`; NRD, 16 frames a step):

    .\tools\harness\Run-PtHarness.ps1 -Scenario candle@subsurface-reading,npc-candle@subsurface-reading

| step | what it shows |
|---|---|
| `1-game` | the game's own picture |
| `2-medium-nrd` | reading 0, NRD: as today |
| `3-surface-nrd` | reading 1, NRD |
| `4-surface-none` | reading 1, no denoiser: its 16-frame mean is what it converges to, and one frame shows its coloured grain |
| `5-off` | everything back (the reading at 0) |

**What to look at.**

- `candle`: the candles in step 3 against step 2. Expect: the wax's own cream in place of the bluish white, and
  less bright where the light comes from behind (about half, by the offline stick: 0.37 against 0.68 to 0.94).
  Still lit by the daylight that falls on their hidden sides (the profile reaches 7 to 14 cm, a candle is 5), so
  still brighter than the orange wall. The foot: no longer a plain surface. It takes the light of all the wax around
  it, the candles' feet included, in its own cream. Where only flames light the wax around it, it stays the brown
  of cream under an orange flame: that part is the light, not the wax.
  A candle's underside (its closed mesh has one, dark, on the rock) takes its share of the light of the wax just
  above it: expect the candles a little darker towards their feet. That is the sum over real surfaces; the game's
  own pass never sees an underside.
- `npc-candle`: the face in step 3 against step 2 should be the same picture (skin's distances are far under a
  pixel). If it is not, something is wrong with reading 1 and I want to know. With no denoiser (step 4) the skin has
  red, green and blue grain: one channel a sample.
- The log: `Subsurface reading on trial: 1 ...` at step 3, `Subsurface reading: 0 ...` at step 5. The yardstick's
  "took the walk" share rises at step 3 by the foot's pixels.
- GPU time. **Reading 1 is slow as built**: it draws about 24 lines for a point on flat skin, one or two rays each.
  Offline (720p, the material over the whole view, section 5, run 1): the direct light's stage goes from 3.5 to
  35 ms on skin and from 3.7 to 78 ms on snow wax. By the pixel that is 38 and 84 nanoseconds. At 4K a face over a
  third of the screen is about 100 ms a frame more, the candle corner's wax about 40 ms if it is 6 % of the screen
  (my guess at its share). The run will be slow at steps 3 and 4 and its frame rates mean nothing; the pictures do.
  (No risk of a device reset that I can see: one pass, a tenth of a second.)

**What reading 1 is NOT yet** (also in the shader's comment, `subsurface_surface_note`):

1. **One limit, to be replaced.** The chances of the surfaces on one line must fit in one draw. `SURFACE_ROOM` (8)
   is how many fit at the least: a line that crosses more than 8 (if they face it squarely) to 13 (at mixed angles)
   scattering surfaces within the profile's reach leaves the ones it crosses last without their chance. A tight
   group of candles can get there with the snow's profile, on a line that runs through four or more of them; skin
   never does. How often it happens in the game's candle corner I have not measured. Under the limit every point
   has exactly its chance (check 2 measures it on a slab; the CPU check on a cylinder).
   I do not know an exact way without such a number yet. It is a limit of the kind the owner forbids: do not make
   this the rule before it is solved.
2. **Coloured grain.** One channel a sample. The mean is exact. The way out, not built: one exit per channel at the
   camera's pixel.
   **And its cost** (above): the room of 8 is a factor of 8 in lines, two thirds of all lines on a flat thing run
   along it and meet nothing, and nothing is reused between pixels or frames. Each of these can be worked on without
   changing what the picture converges to; none is done.
3. **The mask.** In the game's pass the subsurface mask shrinks the profile's reach. Here it is still "how often the
   route is taken" (as with reading 0). All of the game's wax has a mask of 1; skin's map is below 1 in places, where
   it does not matter at 0.3 mm.
4. **The profile's texturing mode is not read**: the colour is the entry point's (HDRP's "post-scatter").
5. **Nothing in between is asked.** Wax behind a 5 cm wall is within a 6 cm profile's reach. The game's pass cannot
   do that (it only has the screen); a sum over real surfaces can.
6. **A camera pixel on the BACK of a sheet** takes the light of the sheet's front (the pixel's record has no room
   for which side was met). A path's vertex knows its side.

**My first form was wrong, and I caught it before it ran.** I first picked one of a line's kept points evenly. A
slab's far face then counts half of what the sum says. The form above gives each point exactly its chance; check 2
exists to catch the old one.

## 4. The foot (step 3)

**Decided again, as the brief asks. The free path was right all along, so the job before's warning stands:** a walk
between the open shell and the rock under it (its part 2) shows the rock. A centimetre of a substance with a 3 to 7
cm free path is a clear film. That is correct for reading 0 and it is not what the owner asks for ("still brown
while the candles are white").

**Under reading 1 the foot needs nothing of its own.** The shell is a surface of wax like the candle's side: the
game's own model never asked for an inside. So the smallest correct form IS the switch: `subsurface_hit` says yes
for a sheet when the reading is 1, and the exit is on the side the sheet was met on. A real open sheet in the air is
treated the same (the game treats it the same): its light spreads along it. It gets no light from its other side.

**Not built: the job before's part 2** (the walk between a shell and what it lies on), for reading 0. Its design is
in `wt\wax2\docs\agents\wax-2.md` section 3. Build it only if the owner chooses reading 0, and expect a darker,
stone-coloured foot.

The offline check the brief asks for (a wax shell on a stone block against the same wax as a closed lump) is check
1 of section 5. **Its answer has a catch worth knowing:** the shell on stone is lit exactly as the top of a DEEP
lump of the same wax (measured: 0.869, 0.850, 0.716 of a plain surface's light under a near lamp, against the
profile's own 0.868, 0.851, 0.713). A lump only 12 cm thick is darker on top (0.735, 0.706, 0.508), because its own
dark underside is within the snow profile's reach and takes its share of the sum. So in this reading "the same wax"
does not mean "the same brightness" for a thin closed thing: its far side counts, lit or dark. The shell has no far
side, so it never loses light that way.

## 5. Tests

Mine, alone: `TGPT_TEST_WAX_READING_ONLY=1` (`tests/pathtracing_wax_reading_checks.inl`; 1280 x 720, own camera
rays, no denoiser, no reuse, no cache; the direct light's view). The wax is the game's snow profile.

What each check expects is worked out in the test from the profile alone (never from the tracer's own output).

| check | what it expects | measured (run 1, 09:57 to 10:05, `%TEMP%\tgb\wax3-out-r1.txt`) |
|---|---|---|
| 1a. a wax block 12 cm thick under a lamp 25 cm over it, its top's light over a plain surface's, red green blue | the profile's own mean of the lamp's light (0.868, 0.851, 0.713), less its dark underside's share of the sum: 0.736, 0.705, 0.507 | **0.735, 0.706, 0.508** (the medium's: 0.759, 0.762, 0.627) |
| 1b. the same wax as an open sheet on a stone block | the plane's mean: 0.868, 0.851, 0.713 | **0.869, 0.850, 0.716**; with the reading off 1.0000 of a plain surface and 0 of 921,600 pixels walk; on, 921,600 of 921,600 |
| 1c. a block 1.2 m deep (the "closed lump" the sheet is held against) | the plane's mean, as the sheet | added after run 1: run 2, below |
| 2. a slab 2 cm thick lit from below alone, its top over the lit face's light | the far face's share of the profile's sum, W / (1 + W) = 0.3863 (my first form would read 0.3147) | **0.385, 0.386, 0.384; mean 0.3849** |
| 3. a cream wax stick 4 cm thick, a white lamp behind it, its near side's light on a white surface | reading 1: white (each channel within 4 %). Reading 0: bluer than white (blue over red above 1.08) | reading 1: **0.370, 0.367, 0.373** (blue 1.008 of red). Reading 0: **0.684, 0.731, 0.942** (blue 1.377 of red). As shown in the wax's colour, blue over red: 1.071 as a substance, 0.784 as the surface's; the wax's own colour: 0.778 |
| 4. cost, the direct light's stage (the camera pixels' route is in it), 720p, the material over the whole view | printed, not judged | skin: **3.49 ms** as a substance, **35.39 ms** as the surface's. Snow wax: **3.65 ms**, **77.85 ms**. The other stages do not move (bounced light 10.6 and 11.2 ms on skin) |

Run 1 printed "FAILED 2 checks", and both were my own expectations, not the tracer:
- I had expected the 12 cm block's top to show the plane's mean (0.868...). It showed 0.735, 0.706, 0.508. That is
  right: the block's dark underside is 12 cm away, inside the snow profile's reach, and the sum counts it. With its
  share taken (the same formula as check 2) the numbers are met to 0.002.
- So "the sheet is lit as the closed lump" was false for a 12 cm lump, and true by the formula for a deep one.
The test now holds each of the three against its own number (a deep block, a thin block, the sheet). Run 2 below.

**Run 2** (the same tracer, DLL DFCE1202...; only the test's expectations and the deep block are new; 10:25 to
10:33 after 17 minutes at the gate; `%TEMP%\tgb\wax3-out-r2.txt`): **"wax reading checks passed", exit 0, frames
failed 0.** Every judged check passes:

| check | expected | measured |
|---|---|---|
| a block 1.2 m deep | 0.868, 0.851, 0.712 | 0.868, 0.852, 0.706 |
| a block 12 cm thick | 0.736, 0.705, 0.507 | 0.737, 0.706, 0.507 |
| an open sheet on a stone block | 0.868, 0.851, 0.713 | 0.867, 0.852, 0.717 |
| the sheet with the reading off | a plain surface, no pixel walks | 1.0000 1.0000 1.0000; 0 of 921,600 |
| a slab's far face | 0.3863 | 0.3849 |
| the stick, reading 1 | white | 0.370, 0.367, 0.373 |
| the stick, reading 0 | blue over red above 1.08 | 1.377 |

Cost in run 2 (the card was shared with another test or the game's start: read the ratios): the direct light's
stage on skin 11.0 ms as a substance, 99.8 ms as the surface's; on snow wax 9.8 and 119.8 ms. With run 1: **about
10 times the walk on skin, 12 to 21 times on the snow wax.**

Not judged by any check of mine, so not claimed: reading 1 at a path's vertex (bounced light, reflections: the
code is the same function, the checks look at the camera's pixels), a sheet met on its back, a group of candles
(the one limit), NRD or Ray Reconstruction on the coloured grain. The game is where those show.

**On the CPU, run and read by me** (`python tools/candle/surface_sampler_check.py [distance] [points]`, new; no
GPU): the shader's way of drawing the point, line for line, on a candle-like cylinder 4 cm thick, against what it
claims to draw (the profile at the straight distance, by quadrature). This is the check of the part the GPU checks
do not reach: a curved thing, the three lines, how a surface faces them.

| the profile | points drawn | worst of 15 bins (angle round the candle, height along it) | lines a point with room for 2 (a plane: 6) | the candle's surface holds, of a plane's sum |
|---|---|---|---|---|
| 30 mm | 434,662 | 2.4 standard deviations | 7.4 | 0.82 |
| 57.76 mm (the snow's blue) | 246,490 | 2.5 | 9.7 | 0.62 |
| 27.72 mm (the snow's red) | 335,243 | 3.0 | 7.2 | 0.84 |
| 0.3 mm (skin's red) | 400,288 | 1.1 | 6.0 | 1.00 |

So the arithmetic is right: the points come out exactly as the profile at the straight distance, straight behind the
candle included (6.6 % of them land in the 22 degrees straight behind, 30.7 % in the 22 degrees by the entry, at
30 mm). In the shader the room is 8, so the lines are four times these: 24 a point on flat skin, 29 to 39 on a lone
candle.

Source checks: `python tests/source_checks.py`: 101 of 101 (one new:
`test_path_tracing_subsurface_reading_on_trial_default_off`; one quoted line changed in
`test_path_tracing_small_lamps_on_trial_default_off`: the clamp, 4194303 to 16777215).
`python tests/harness_analyze_test.py`: exit 0.
The plugin: `dotnet build ... -f net472 -o %TEMP%\tgb\wax3-plugin`: 0 warnings, 0 errors.

No whole GPU test, no ctest as a whole, no mutation checks (the rules).

## 6. Noted, not built (the brief's item 4) and things for the coordinator's lists

- **The red canopy (PT-49).** The owner's first question was "would they really be SO white under that darkened,
  reddish cloth?". Neither reading answers it: how much daylight reaches the candles is the cloth's doing (a fixed
  30 % of the sun through any thin cloth, in the cloth's hue at full brightness: PT-51's list) and the open side on
  the left. Reading 1 changes the HUE the daylight shows in on the wax (cream, not blue-white), not how much arrives.
- **The corner's darkness (PT-39)**: the owner ruled dark is right there. Not touched.
- **HDRP's own path tracer and the game's shader disagree about the subsurface mask** (a chance against a reach).
  The tracer follows the path tracer. Under reading 1 the reach would be the faithful one.
- **Skin's walk buys nothing at 0.3 mm but grain and time.** Whatever the owner chooses for wax, skin shows the
  same picture as a plain surface in both readings. (By the numbers, not measured: an ear lit from behind does not
  glow in either, 5 mm is 15 free paths. The game makes that glow with a separate transmission term and a thickness
  map; whether the tracer still has a counterpart of it for skin I did not check.)
- If reading 1 becomes the rule, the walk, its albedo fit, `mark_closed`'s use for wax and the "sheet, no walk" log
  line are dead and go in the same job (the owner: remove as we go). Not removed now: it is a trial.

## 7. Workarounds met

| workaround | what was done |
|---|---|
| "an open piece of a two-sided material is a sheet: no walk" | met; reading 1 does not need it (a sheet is a surface). Kept for reading 0 |
| the profile's distance read as a substance whatever the thing's size | met; this is the question the trial puts to the game |
| thin cloth passes a fixed 30 % of light at its hue's full brightness | met, not touched (PT-49, PT-51) |
| HDRP's 16-step stop in its own walk | not in the tracer; noted only |

## 8. Not done by this job, on purpose

`CHANGELOG.md`, `README.md`, `docs/TESTING.md`, `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
`docs/PROJECT-CONTEXT.md`, `docs/PT-HARNESS.md`: not edited (the coordinator's; the text is here).
