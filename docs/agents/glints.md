# Glints: the small fireflies that stay with NRD (PT-85)

Worker: Claude, 2026-10-08 18:20 to 20:45, copy `wt\glints` (base `wt\base57`). Brief:
`docs/agents/briefs/brief-glints.md`. Evidence: `%TEMP%\tgb\glints-out-<run>\gpu.log`; a sheet
`docs/agents/glints/before-after.jpg`; my scripts in `docs/agents/glints/`.

## The answer in six lines

1. The estimator: the direct light's CANDIDATES. Which lamps a pixel's reservoir is offered each frame was
   decided without looking at the pixel. The one lamp a glossy surface mirrors came up as seldom as any other.
2. Worst: a lamp across the room is on the "far list", which gave ONE candidate a frame out of all its lamps (470
   in the tavern), drawn by power alone. Its highlight came in one frame of some 200, hundreds of times too bright.
3. The fix, all in the sampling: each candidate is now chosen for the pixel's own surface, the far one as the best
   of 32 looked at (parts A and C). And a smooth surface's highlight is also sampled by a direction drawn from the
   shine itself, sharing the light with the lamps' samples so nothing counts twice (part B, the one the brief names).
4. Numbers (my fixture, Specular layer, a pixel's largest sample over its own mean, median pixel): far lamps
   67 -> 10 at roughness 0.20 (the largest sample anywhere over the highlights' mean: 2642 -> 70); forty lamps in
   a cell 11 -> 6, the same as with eight; a smooth ball (0.05) 18 -> 7.
5. The mean did not move against the unbiased reference. But THE OLD PICTURE HAD LESS LIGHT THAN THAT: its
   boiling filter threw the rare bright samples away (it kept 13 % of a far lamp's highlight). Expect steadier
   and, on average, brighter highlights of lamps across a room.
6. No filter, no denoiser setting, no clamp touched. Shaders and the test only; no ABI, struct or option bit.

## 1. The estimator (from the code, the tavern run's log, and the fixture)

- The tavern run's log (`artifacts\harness\20261008-182327\TGPathTracing.Native.log`): `lights=514`, the light
  grid lists 11 lights a lit cell on average, 43 at most, and `emissive: 0 submeshes, 0 triangles`. Nothing in
  that room is traced as a glowing mesh. Every lamp is one of the game's light objects, a point with a size.
- So a lamp's highlight on an opaque glossy thing had ONE source: the direct-light passes (`pt_di.hlsl` picks,
  `pt_di_spatial.hlsl` shades). The reflection ray never meets a lamp: a lamp is no thing in the traced world.
  (The brief's `g_pathGathers = false` is about glowing MESHES, which the reflection ray does count whole.)
- It was rare-and-huge in three ways:
  1. THE FAR LIST. A light grid cell is 8 m. A candle is listed in a cell only as far as it gives 50 lux, a few
     metres (`SceneLights.Reach`). Every lamp a cell does not list is on the far list, and the far list gave ONE
     candidate a frame, drawn by power (`scene.cpp build_light_grid`: all lamps within 32 m weigh the same). A
     lamp's diffuse light from across a room is small, its highlight on a glossy bottle is not.
  2. MANY LAMPS IN A CELL. Where a cell lists more than 8, 8 were drawn by weights made once for the whole
     cell (power over distance from the cell). The lamp a surface mirrors came up now and then.
  3. A NARROW SHINE. On a smooth surface only a small spot of a lamp's disc lies in the shine. A point drawn
     evenly over the disc is seldom that spot. (Rare in the tavern: a candle's flame is 1.2 cm across,
     `LightRules.DefaultRadius`, smaller than any shine but a mirror's.)
- The same far list fed PATH VERTICES (`direct_light` in `pt_common.hlsli`): the reflection of a floor that far
  lamps light came as rare bright samples too. Those are one-frame events in the Specular layer.
- And the BOILING FILTER (`pt_di.hlsl`, `boiling_outlier`) drops a reservoir far brighter than its 8x8
  neighbours. A rare huge sample is exactly that. So most of these highlights were thrown away, and the ones
  that got through were the fireflies.

## 2. The fix (three parts, all in the sampling, all unbiased)

### Part A: a camera pixel's candidates are chosen for its own surface

`pt_rtxdi.hlsli` `di_candidates`, used by `pt_di.hlsl`. Standard resampled importance sampling in two steps
(Talbot 2005; what ReSTIR's and RTXDI's initial candidates are): many cheap looks, the full light sample only
for the few kept.
- `light_proxy_at` (`pt_common.hlsli`): what a light can matter at a point, from 44 bytes of its record and no
  ray. Its power over its squared distance from the point, times the surface's diffuse colour and cosine plus
  its shine towards the lamp. Never zero.
- The cell's own lights: all of them where it lists up to 8 (as before). Where it lists more, 8 are drawn
  along the pixel's own measure of ALL of them. So a lamp that matters here is always among the 8. (A cell
  with more than 64 lamps: 64 of them are looked at, drawn by the cell's old weights. `NEAR_TRIALS`. The cost of
  a pixel stays bounded; nothing is left out, the others are counted by the weights of those looked at.)
- The far list: 32 of its lamps are looked at, spread evenly over the list, and the one that can matter most
  here is kept, with the standard resampling weight (`far_candidate`, `FAR_TRIALS`). A lamp the cell lists is
  still no far candidate, so every light is counted exactly once.
- Unbiased whatever the measure says: it only decides how often a light is drawn, and each draw is weighted by
  one over that. It knows no spot cone, cookie, fog or shadow. That costs noise there, never light.

### Part B: the shine's own draw, shared with the lights' samples (the cure the brief names)

Multiple importance sampling, Veach's power heuristic. RTXDI's public integration guide has the same idea as
"BRDF samples" among the initial candidates.
- `pt_rtxdi.hlsli` `lobe_lamps`: one direction is drawn from the surface's own shine. Where it meets a lamp's
  disc, the pixel gets the lamp's light there, times its share. A shadow ray decides whether the lamp is seen:
  the same ray the lights' samples use (to another random point of the disc, so both hold the same shadow).
- `rtxdi_shade`: the lights' samples keep only THEIR share of a lamp's highlight (`lamp_light_share`). ReSTIR's
  target function uses it too, so reservoirs stop chasing the one bright spot of a disc.
- `pt_di_spatial.hlsl`: one line, `spec += lobe_lamps(surface, rng)`.
- The shares add to exactly one at every point of every lamp's disc. Both are functions of the surface, the
  lamp and the point only, so a reservoir reused from another pixel or frame is weighed like any other.
- Which lamps: point and spot lights with a size that make highlights (`reflected_as_sphere`), on surfaces
  with a GGX shine (not hair, not see-through glass), smoother than roughness 0.5 (`LOBE_ROUGHNESS`). The cell's
  own lamps always. The far list's lamps only below 0.15 (`LOBE_FAR_ROUGHNESS`: every far lamp's centre is
  tested once a pixel there). Everything else stays wholly with the lights' samples.

### Part C: a path vertex takes its far candidate the same way (added last)

`direct_light` in `pt_common.hlsli` (bounced light, what a reflection shows, a walk's exit, the radiance cache)
keeps the cell's weights for the cell's own lights and takes its ONE far candidate through `far_candidate`, as a
camera pixel does. `lists_candidate` lost its far half (nothing calls it; its check line is replaced).
This touches every pass that traces paths. It is unbiased by the same code as part A's far candidate, but only
my fixture has run on it. THE WHOLE GPU TEST HAS NOT RUN ON ANY OF THIS (not a worker's to run).
To back part C out alone: `%TEMP%\tgb\glints-keep\` holds the six files with A and B only (their DLL,
CFA2488EC33792CD, is in `%TEMP%\tgb\glints-partAB`).

## 3. The fixture

`tests/pathtracing_gpu_test.cpp`, the lambda `glints`. Alone: `TGPT_TEST_GLINTS_ONLY=1`
(`TGPT_TEST_GLINTS_FRAMES=<N>`, 128; `TGPT_TEST_GLINTS_SET=eight|forty|far|crowd`;
`TGPT_TEST_GLINTS_OPTIONS=2048` turns the boiling filters off for a study). It also runs in the whole test,
after `mirrorFilters()`. About two minutes once the passes are made. It needs no NRD.
A glazed ball (0.5 m) on a rough floor, eight lamps of 3 cm about 2.5 m away, the tracer's own camera rays, no
denoiser, ReSTIR's reuse as in the game (history 4, 4 spatial samples). It reads the Specular layer for N frames.
At the pixels whose mirror direction passes a lamp within the shine's width it prints each pixel's largest
sample over its own mean (worst pixel, tenth worst in a hundred, median pixel), the spread, and the highlights'
mean light. Also the Direct layer, and the ball's pixels AWAY from every highlight (they show the floor's
reflection, which a path vertex lights). Five roughnesses: 0.05, 0.1, 0.2, 0.3, 0.45. Four sets of lamps:
- `eight`: the ball's cell lists eight; each is a candidate every frame.
- `forty`: 32 more lamps in the cell (it used to draw 8 of 40).
- `far`: the eight lie outside the reach the cell lists them by, with 200 more behind a wall: the far list
  holds 208 (the tavern's holds about 470).
- `crowd`: 120 more in the cell, 128 in all: more than the 64 a pixel looks at.

## 4. Numbers (128 frames a case)

"Median", "worst": a pixel's largest sample over its own mean (1 would be a steady highlight; 128 means ALL of
its light came in one frame). "Peak": the largest sample anywhere over the highlights' mean. "Light": the
highlights' mean. Before: the unchanged tracer, DLL 366DFF0605A7E3E7 (`glints-out-base0`, `-base-scrowd`).
After: the final build, DLL CAA336A981B32C4C (`glints-out-partc`, `-last`).

### The Specular layer at the highlights

| lamps | roughness | median | worst | peak | light |
|---|---|---|---|---|---|
| eight | 0.05 | 18.0 -> 6.7 | 54 -> 29 | 88 -> 14 | 5.16 -> 7.84 |
| | 0.10 | 11.4 -> 10.7 | 27 -> 26 | 57 -> 40 | 1.685 -> 1.666 |
| | 0.20 | 6.1 -> 6.1 | 22 -> 21 | 25 -> 32 | 0.1762 -> 0.1763 |
| | 0.30 | 4.2 -> 4.2 | 13 -> 12 | 12 -> 12 | 0.0508 -> 0.0508 |
| | 0.45 | 2.8 -> 2.8 | 9 -> 8 | 5 -> 6 | 0.0227 -> 0.0226 |
| forty | 0.05 | 44.3 -> 6.7 | 119 -> 20 | 265 -> 16 | 1.23 -> 8.07 |
| | 0.10 | 24.9 -> 10.8 | 107 -> 26 | 120 -> 38 | 1.008 -> 1.730 |
| | 0.20 | 11.0 -> 6.1 | 50 -> 26 | 79 -> 31 | 0.1720 -> 0.1753 |
| | 0.30 | 6.9 -> 4.2 | 28 -> 13 | 32 -> 11 | 0.0511 -> 0.0510 |
| | 0.45 | 3.9 -> 2.9 | 14 -> 10 | 13 -> 5 | 0.0228 -> 0.0228 |
| far | 0.05 | 66.5 -> 3.8 | 128 -> 30 | 2470 -> 7 | 0.055 -> 7.73 |
| | 0.10 | 70.6 -> 12.6 | 128 -> 36 | 6172 -> 36 | 0.063 -> 1.709 |
| | 0.20 | 66.9 -> 10.3 | 128 -> 33 | 2642 -> 70 | 0.0224 -> 0.1783 |
| | 0.30 | 45.2 -> 7.0 | 127 -> 19 | 818 -> 30 | 0.0170 -> 0.0514 |
| | 0.45 | 26.0 -> 4.3 | 117 -> 14 | 140 -> 11 | 0.0147 -> 0.0229 |
| crowd | 0.05 | 50.7 -> 7.2 | 126 -> 60 | 1293 -> 20 | 0.72 -> 6.94 |
| | 0.10 | 46.7 -> 13.7 | 126 -> 70 | 342 -> 55 | 0.845 -> 1.492 |
| | 0.20 | 22.2 -> 8.0 | 118 -> 27 | 135 -> 42 | 0.1616 -> 0.1752 |
| | 0.30 | 12.6 -> 5.4 | 66 -> 21 | 84 -> 21 | 0.0511 -> 0.0513 |
| | 0.45 | 6.7 -> 3.5 | 29 -> 10 | 31 -> 10 | 0.0236 -> 0.0236 |

- THE FAR LIST: a highlight was one frame in some 200. Now the median pixel's largest sample is 4 to 13 times its
  mean, and the largest anywhere 7 to 70 times the highlights' mean (it was 140 to 6172).
- FORTY LAMPS in a cell now behave exactly as eight. 128 nearly so.
- What stays (3 to 11 at middle gloss, the same with 8, 40 or far lamps) is the ordinary noise of ONE reservoir a
  pixel: it holds one lamp, so a highlight is there in the frames its lamp won. That is "often", not "rare".
- Where the cure has nothing to do it changes nothing: eight lamps at 0.20 to 0.45, the same numbers and the
  same light to four digits.
- THE LIGHT COLUMN. After, a highlight is as bright whichever way its lamp is listed (at 0.20: 0.1763, 0.1753,
  0.1783, 0.1752; the eight lamps are the same in all four sets). Before it was 0.1762, 0.1720, 0.0224, 0.1616.

### One bounce on: the floor's reflection (the ball's 95,680 pixels away from the highlights), median

eight 1.9 to 3.7 (unchanged). forty 3.0 to 4.3 (unchanged). crowd 4.9 to 6.6 (was 6.2 to 7.7).
far: 22 to 26 before part C, 3.2 to 4.7 with it; its light 0.00351 against 0.00350 before (and 0.00346 with the
same lamps listed).

### The Direct layer at the highlights, median (worst), before -> after

eight 0.05: 24.3 (77) -> 28.6 (99). eight 0.20: 5.0 (17) -> 5.1 (22). forty 0.05: 14.2 (108) -> 26.3 (88).
forty 0.20: 7.1 (33) -> 5.1 (24). far 0.05: 15.7 (31) -> 15.5 (33). far 0.20: 16.3 (72) -> 7.3 (32).
The ball's whole diffuse light with the lamps on the far list: 0.155 -> 0.218 (0.216 with the same lamps listed):
the far list's diffuse light was 28 % short before.

### The mean (the boiling filters off, `TGPT_TEST_GLINTS_OPTIONS=2048`, 256 frames; A and B, DLL CFA2488EC33792CD)

The unchanged tracer with its boiling filter off is the unbiased reference: the same light, only rare.

| lamps | roughness | highlights' light, unchanged | changed | the whole ball's Specular, unchanged | changed |
|---|---|---|---|---|---|
| eight | 0.05 | 8.007 | 8.017 | 0.00769 | 0.00769 |
| | 0.10 | 1.7104 | 1.7204 | 0.00767 | 0.00770 |
| | 0.20 | 0.1758 | 0.1763 | 0.00762 | 0.00764 |
| | 0.30 | 0.0508 | 0.0507 | 0.00751 | 0.00750 |
| | 0.45 | 0.0226 | 0.0226 | 0.00725 | 0.00725 |
| far | 0.05 | 6.31 | 8.12 | 0.00699 | 0.00785 |
| | 0.10 | 1.843 | 1.740 | 0.00804 | 0.00783 |
| | 0.20 | 0.1762 | 0.1767 | 0.00773 | 0.00773 |
| | 0.30 | 0.0519 | 0.0511 | 0.00765 | 0.00760 |
| | 0.45 | 0.0231 | 0.0229 | 0.00738 | 0.00735 |

The Direct layer's whole ball: eight 0.21650 against 0.21647; far 0.21762 against 0.21788.
Equal to three digits wherever the unchanged tracer's own mean has settled. On the far list at 0.05 and 0.10 it has
not (a pixel's light there is one frame in 208, so 256 frames hold one or two): 11 % under at 0.05, 3 to 6 % over
at 0.10. With the filter off its median is 98 to 219 and its peak up to 2728: the true size of what the filter hid.
With the filter ON, the changed tracer gives the same light as with it off (far: 7.73, 1.709, 0.1783, 0.0514, 0.0229
against 8.12, 1.740, 0.1767, 0.0511, 0.0229): after the change the filter finds next to nothing to take.
So the light is the same; the OLD PICTURE showed less of it. Of a far lamp's highlight at 0.20 it kept 0.0224 of
0.1762 (13 %), of a smooth ball's with eight lamps 5.16 of 8.0 (two thirds).
(Not run: the same proof on the final build with part C. Its highlights' light with the filter on is the A-and-B
build's to the last digit, and the floor's reflection's is 0.00351 against 0.00350.)

### Which part does what (median; B alone is DLL C2CB514C5C0D1C36, `glints-out-partb`)

before -> B alone -> A and B: eight 0.05: 18.0 -> 7.4 -> 6.7. forty 0.05: 44.3 -> 3.6 -> 6.4; forty 0.20:
11.0 -> 10.8 -> 6.1. far 0.05: 66.5 -> 2.8 -> 3.8; far 0.10: 70.6 -> 14.5 -> 12.6; far 0.20: 66.9 -> 66.9 -> 10.3;
far 0.45: 26.0 -> 26.0 -> 4.6.
Part B mends smooth things (0.05 to 0.1) and nothing else. Part A mends everything from 0.1 up. The tavern's
lamps are small and its things mostly of middle gloss: part A is the one that matters there. Part C mends the
reflections of what far lamps light.

### Cost (the fixture's own frames, 1280x720, least of 128; other tests ran beside it, so a rough guide)

"direct light" stage: eight 0.95 -> 1.08 ms, forty 1.06 -> 1.69 ms, far 0.99 -> 1.36 ms, crowd 1.05 -> 2.66 ms.
"reflections": 1.0 -> 1.06 ms. Whole frame 10.2 to 10.5 ms before, 10.3 to 11.9 after. Not measured in the game.
The tavern's cells list 11 lamps on average and 43 at most (between `forty` and `eight`).

## 5. Checks

- The fixture's seven `expect` lines, all "ok" on the final build with the final test
  (`glints-out-last2\gpu.log`, 20:39): a smooth ball's highlight comes in most frames; forty lamps as steady as
  eight; 128 nearly; a lamp on the far list gives the same highlight as one its cell lists (light within 15 %);
  a far lamp's highlight comes often; a floor that far lamps light shows in a reflection nearly as steadily; and
  as bright.
- The SAME test on the unchanged tracer (`glints-out-basecheck\gpu.log`, 20:41): six of the seven FAIL. The
  seventh ("and as bright") passes there too, as it should: it guards the mean, and a path vertex's light was
  right before, only rare.
- `python tests/source_checks.py`: 96 ran, OK. One new: `test_path_tracing_lamp_highlights_sampled_for_the_surface`.
  Three old lines follow the code: the candidate loop of `pt_di.hlsl` and of `direct_light`, and "a far candidate
  the cell lists counts nothing" (now `if (cell_lists(...)) continue;` in `far_candidate`).
- The one FAIL line in every run of mine is NOT this job's: "[candle] ... skin 110 m from scene origin: coloured
  or white bulk slab retains its diffuse colour under even white light within 3.5 percent". It fails on the
  unchanged tracer too (`glints-out-base0\gpu.log` line 198: 0.975, 0.967, 0.955 against 1).
- No mutation checks (the rule: not until the owner says it looks right). The unchanged tracer is the natural one.

## 6. Built, and not run

Built: `%TEMP%\tgb\glints` (NMake, Release, `-DTGPT_WITH_NRD=OFF`), `TGPathTracing.Native.dll` CAA336A981B32C4C and
`tgpt_gpu_test.exe`. Commands:
- build: `%TEMP%\tgb\glints-build.cmd` (configure once, `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\glints
  -Throttle 8`, then `cmake --build ... --target TGPathTracing.Native tgpt_gpu_test`). A shader change compiles
  all 39 headers: 7 to 9 minutes.
- the fixture: `powershell -File %TEMP%\tgb\glints-gpu.ps1 -Name <run>` (waits for the GPU check itself; add
  `-NativeDll %TEMP%\tgb\glints-base\TGPathTracing.Native.dll` for the unchanged tracer, `-Frames 256 -Options 2048
  -Set far` for the study).
NOT RUN, the coordinator's:
- the whole GPU test: `ctest --test-dir build\native -C Release -R tgpt_gpu_test --output-on-failure` (parts A and
  C change the noise of every lit scene's direct light and of every path's far lamps; the means do not move, but
  checks with noise thresholds have not been tried);
- a build WITH NRD and `tgup_gpu_smoke_with_pt`;
- the game: `.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room` with the recipe `fireflies`.
Not built: the plugin (no C# changed), no install, no deploy.

## 7. For the coordinator's ONE game scene (`tavern-room`, recipe `fireflies`)

What the brief's table should become, and what would prove me wrong:
- Specular, 7.0 places a frame: should fall to a small part of that. Both kinds are addressed: the places that
  last 3 to 4 frames (a reservoir holding a rare far lamp) and the one-frame ones (a reflection's vertex
  drawing a rare far lamp). If it stays near 7, the specks are not the lamps' sampling and this job missed.
- Direct, 6.2: should fall, but not to zero: the candle's own wax and the armour's black dots are other things
  (section 9), and a SMOOTH thing's Direct layer still flashes at its highlight (section 8, number 3).
- The picture, 6.2: should fall with them.
- Indirect stays 0.
- LOOK FOR: highlights of lamps across the room on bottles, beams' edges and metal that are now there in every
  frame, and brighter on average than before. That is the light the boiling filter used to throw away. Read the
  gain line: it can move the exposure a little.
- Speed: read the "direct light" and "reflections" stages against the last run's (4.18 and 11.57 ms there).

## 8. What is NOT fixed (so the owner hears it first)

1. THE FAR LIST IS STILL A LOTTERY, only a 32 times better one. A pixel looks at 32 of its lamps a frame. With the
   tavern's 470 that finds the lamp a surface mirrors in 1 frame of 15 (it was 1 of 470); the reservoir's reuse
   over 4 frames and 4 neighbours does the rest. In the fixture (208 far lamps) the median is 4 to 13. To make it
   "every frame", far lamps must be found by direction (a light tree with direction bounds, or the grid's cells
   along the mirror direction). A job of its own. A cheaper knob: `FAR_TRIALS` (64 halves the lottery again).
2. THE CELL'S OWN LIGHTS AT A PATH VERTEX are still drawn by the cell's weights (8 of them). With 128 lamps in a
   cell the floor's reflection flashes 5 to 7 times its mean (2 to 4 with eight). Ordinary noise, not rare.
3. THE DIRECT LAYER ON A SMOOTH THING still flashes at the highlight's pixels (25 times at 0.05). One reservoir
   a pixel gives both the diffuse light and the highlight. Where the highlight is hundreds of times the diffuse
   light the reservoir nearly always holds the mirrored lamp, and the other lamps' diffuse light comes in the few
   frames another lamp wins, with a big weight. The SUM of the two layers is steady, each layer alone is not, and
   the tracer filters them apart. The cure is a second reservoir (one for diffuse, one for the highlight).
4. THE BOILING FILTER still takes light from whatever rare bright sample is left (measured here: nothing to speak
   of after the change). It is a filter that changes the mean. Not touched (the brief: no filters). It could be
   tried OFF now, in the game, since it has little left to do: `TGPT_OPTION_NO_BOILING`.
5. A MIRROR-SMOOTH OPAQUE THING shows a lamp widened by `roughness_alpha`'s floor (alpha 0.002). Not touched.
6. ONLY THE FIXTURE has seen any of this. Not the whole GPU test, not NRD's path, not the game.

## 9. The Direct layer's other specks (the brief's step 3: findings only)

- THE CANDLE'S OWN WAX changes from frame to frame because of how wax is lit: `pt_walk.hlsl` takes ONE random
  walk a pixel a frame and lights the walk's exit with `direct_diffuse`, with no reuse over frames or neighbours
  (its header says so: "reusing exits ... is the next step"). The exit lies 1 to 6 cm from the candle's own
  flame, where light falls off as one over the distance squared: two exits differ by up to 36 times. That is the
  walks' job (the walks worker's build is in `%TEMP%\tgb\walks-*`). Not touched. A small, sure step there: several
  walks a pixel on these few pixels (the log: 0.06 % of the screen). Part C helps it a little (a walk's exit takes
  its far lamp better); the near flame's own noise stays.
- THE DOTS ON THE ARMOUR'S RIBS are black in every frame of the sheet, the "usual" picture too. Steady black is
  light that is missing, not a sample that is rare: PT-27. Not a sampling matter; not looked into.

## 10. Workarounds met

- `roughness_alpha`'s floor (0.045, alpha 0.002), "for evaluating the lobe at a light, whose peak it bounds"
  (`pt_common.hlsli`): a workaround for exactly the old estimator. NOT removed: it changes what a mirror shows
  of a lamp (the mean), and that is the owner's to judge. For noise it is no longer needed.
- The boiling filter: see section 8, number 4. NOT removed.
- `pt_di_spatial.hlsl`: `if (s.glass && reflected_as_sphere(...)) spec = 0.0`, and `reflected_lamps` (glass and
  water find the lamps by their reflection ray, all of them, all or nothing). Left: the same idea for mirror-like
  see-through things. Opaque things now have it with shares.
- `lists_candidate`'s far half (one far lamp by power): REMOVED, with its check line, in part C.
- `cell_candidate` (the cell's weights) stays: path vertices' near lights and cells of more than 64 use it.

## 11. Merging

Changed (7): `native/pathtracing/shaders/pt_common.hlsli` (`lists_candidate` halved; new `light_proxy_at`,
`far_candidate`, `FAR_TRIALS`; `direct_light`'s loop; one comment at `light_lists`), `pt_rtxdi.hlsli` (two fields
in `RAB_LightInfo`/`RAB_LightSample`; `lamp_point`; the two new sections; `rtxdi_shade`'s one line; `lobe_lamps`),
`pt_di.hlsl` (the candidate loop, its header), `pt_di_spatial.hlsl` (one line and its comment),
`tests/pathtracing_gpu_test.cpp` (the `glints` lambda after `mirrorFilters`' ONLY block, its ONLY block, one call
in the whole run), `tests/source_checks.py` (three lines follow the code, one new check), `docs/TESTING.md` (one
paragraph). Added (5): this report and `docs/agents/glints/` (the sheet, three scripts).
None of the coordinator's files. No C++, no C#, no ABI, no option, material or instance bit, no struct shared
with C++. A job that also edits `direct_light`'s candidate loop or `pt_di.hlsl`'s will conflict there.
For `CHANGELOG.md` (not edited; a paragraph to paste):
"PT-85, the fireflies NRD's picture kept: the direct light's candidates were chosen without looking at the pixel
(the cell's weights; ONE far lamp of hundreds, by power), so the lamp a glossy surface mirrors came in one frame of
some 200 and hundreds of times too bright, and the boiling filter threw most of that light away (it kept 13 % of a
far lamp's highlight in the fixture). Now each candidate is chosen by what the light can matter to the pixel's own
surface (the far one the best of 32 looked at; path vertices' far one too), and a smooth surface's highlight is
also sampled by a direction drawn from its own lobe, the two sharing each point of a lamp's disc (power heuristic).
Fixture `glints` (`TGPT_TEST_GLINTS_ONLY=1`): a far lamp's highlight, a pixel's largest sample over its mean,
median 67 -> 10 at roughness 0.20, the largest anywhere 2642 -> 70 times the highlights' mean; forty lamps in a
cell 11 -> 6; a smooth ball 18 -> 7; the mean the unbiased reference's to three digits."

## Where it stopped

2026-10-08 20:45. Done. Everything above is built and the fixture has run on it; the last two runs (the final
test on the final build, and on the unchanged tracer) ended at 20:41. No script of mine is left waiting.
What is left is the coordinator's: the merge, the whole GPU test, a build with NRD, the one game scene.
