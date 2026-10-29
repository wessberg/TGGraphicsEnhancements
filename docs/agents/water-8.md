# Water 8 - the sun in the water at a third of the cost, the same result (PT-81)

Copy: `wt\water8` (base `wt\base58`). Two Claude workers: the first from 2026-10-08 20:00 until a usage limit stopped
it at 21:10; the finisher from 23:00 to about 01:30.
Nothing is written to the main tree or the game folder; the game is never started; nothing deployed or committed.
Builds and logs: `%TEMP%\tgb\water8-*`. Scripts: the session's `scratchpad\water8-work\` (the first worker's
`build.ps1`, `run.ps1`) and `scratchpad\water8b-work\` (the finisher's: the CPU checks, the edit scripts, `keep\`).

## Read this first

**The job is at its end (01:30). The copy is build 6: whole, built, proved. No measuring or debug switch is left in it.**

**The aim was under a fifth. It is a third: the sun's part of the deep-water frame is 9.7 to 9.9 ms where it was 29.3
to 31.0 ms** (640 x 360, 230,000 pixels of deep water, no denoiser). The light is the same in every number of
water-7's sea fixture, and the noise is less (the largest single sample 15.4 times the picture's mean, was 26.6).
`PASS sea checks`, the whole sea fixture, the card to itself, twice: runs `t4-sea` (01:02) and `t4-sea2` (01:20),
`%TEMP%\tgb\water8-test-t4-sea2.log`, build 6 (`water8-stage8`, DLL sha256 7E6A20C7...). Source checks: 96 of 96.
In the game the "before" is the frame with the Caustics row ON: 235 ms (the coordinator's run `20261009-010307`; 57 ms
with the row off). Expect about 135 to 150 ms with this build (section 7). Nothing of it has been seen in the game.

| The fixture's number (its tolerance) | Before: base58, the same test program and frames (`base-sea2`, 00:54) | After: build 6 (`t4-sea`) |
|---|---|---|
| closed form, still water, one scattering (x0.97 to x1.05) | x0.994, x1.010, x1.016 | x0.994, x1.010, x1.016 |
| waves against still water (x0.92 to x1.08) | x1.000, x1.000, x0.999 | x1.000, x1.000, x0.999 |
| 16 frames' mean over 256 frames' (within 0.03) | 0.9995 | 1.0000 |
| largest single sample over the picture's mean (under 60) | 26.6 (13.8 in the 16 frames; 15.5 in still water; 7.8 with the sun off) | 15.4 (13.9; 15.5; 7.8) |
| the tiles against every patch made at its point, one DLL (within 0.01; new) | - | x1.0000, x1.0003, x1.0005 (2 tiles summed: x0.9995, x0.9998, x0.9998; 8: x1.0001, x0.9996, x0.9999) |
| the Caustics row off against on; its largest sample | x1.069, x1.062, x1.066; 13.9 | x1.068, x1.062, x1.065; 13.9 |
| the glint's light against a mirror's (smoothness 1 / 0.947 / 0.7) | x0.973, x0.991, x0.999 | x0.973, x0.991, x0.999 (the same to the last digit: not touched) |
| the glint's brightest pixel; pixels over a tenth of it; largest sample over its pixel's mean | 17.4, 12.8, 0.0; 22, 33, 21299; 5.6, 4.5, 2.7 | 17.4, 12.8, 0.0; 22, 33, 21299; 5.6, 4.5, 2.7 |
| foam covers (as sent / amount 0.7 / amount 0 / intensity 0) | 0.0164 (1.8 % of the pixels, 100 % thin) / 0.0004 / 0 / 0 | 0.0164 (1.8 %, 100 %) / 0.0004 / 0 / 0 |
| **GPU time a frame, deep water (part 7): with the sun, without, the sun's part** | **35.16, 4.16: 31.0 ms** (the first worker's runs of the base: 33.15, 3.81: 29.3 ms; alone 28.9, 29.9) | **13.94, 4.03: 9.9 ms; the second run 13.61, 3.96: 9.7 ms** (with 2 tiles summed 8.8, with 8 10.0; every patch made at its point, the same DLL: 29.1 to 29.5) |
| GPU time a frame, the beach view (part 1, as sent; the Caustics row off) | 9.11 ms; 2.69 | 9.41 ms; 2.73 (the same within the card's wander: that view's water is shallow, its paths meet the bed, and the sun's question is a small part of it) |

(The glint's numbers differ from water-7's x1.024, x1.011, x1.000 because the fixture's part 7 runs more frames now
and part 8 so gets other frame numbers; the base on the same frames gives the same numbers as build 6, digit for digit.
The first worker's "26.8 ms" for the beach view of the base was taken with something else on the card: 9.1 ms.)

**Six things to know before merging:**
1. **A third, not a fifth, and why.** Of the 9.7 ms, 2.8 are not the sun's question at all or come before any tile
   (what else the sun turns on in the frame: its glint's shadow ray, the caustics pass; the ray up to the surface);
   4.0 sum the tiles; 0.7 ask them; and **2.0 ms go to one point in about a thousand that still makes its patch at the
   point** (the far tail of the facets' lobe, the deepest points) while its group of 32 pixels waits: 0.13 % of the
   light. The next two steps (those rare points, and smaller tiles) would reach a fifth: section 8, with numbers.
   They are new code that wants its own proof, so they are a plan here.
2. **The sea past about 60 m from the camera is not served** (the grid is 76 m by 93 m round the camera): there every
   patch is made at its point as before, at the old cost. In `water-day` that is the strip of some 40 pixels under the
   horizon, a tenth of the sea's pixels, and it will be about a third of the sun's cost that is left (section 7).
3. **How it is done is new** (section 3): not the patch that grows, read from kept corners (the first worker's build:
   three quarters of the cost), but tiles of kept corners with a record each, a test that leaves a tile out without
   reading it, and the same bounded work for every pixel. The patch made at the point is the base's own code, word for
   word, and serves what the tiles cannot.
4. **One thing is not the same sample by sample, by design:** a point nearer the surface than 15 cm is now lit from
   the grid's own cells (7.6 cm, half a texel of the finest wave band, as every deeper point always was) and not from
   cells of half its way to the surface. On the CPU against a patch of very fine cells: x0.995 to x1.004 from 2 mm
   under the surface on (section 5). If that is not wanted: `gridCell <= cellOfWaves` back to `gridCell <= cellWanted`
   in `pt_water_sun.hlsli` (one word; it costs half a millisecond or a little more: build 5 was that, 10.5 ms).
5. **Memory: 59 MB** (two slots' corners and records; the first worker's grid was 48 MB), made when a slot first has a
   grid. No ABI change. The frame constants (the shaders' own block, not the plugin's) are 8048 bytes (were 7920):
   `float4 sunGrid[8]`. A merge with another job that grew them needs the sum by hand (`static_assert`, two source checks).
6. **The merge meets one conflict, in `tracer.cpp`** (a dry run of `merge_copy.py`, 01:20; it wrote nothing): at the
   frame constants' last line, where the main tree has reworded `look`'s comment since base58 and the copy puts
   `sunGrid[8][4]` after it. Keep both; the size is 8048 (main's 7920 + 128). `pt_common.hlsli` and
   `source_checks.py` merge by themselves.

## 1. What the finisher found in the copy (23:05)

| Thing | State |
|---|---|
| The grid (the pass `pt_sun_grid.hlsl`, the buffer, the frame constants) and `water_sun` taking a growing patch from it | Whole, built twice, run on part 7 alone: the light the same (x0.9992, x1.0003, x1.0002), **the sun's part 22.7 ms where it was 29.9: three quarters, not a fifth.** |
| Build 2 ("what the surface lets through only for the triangles that light the point") | Whole (it was not cut off). It saved nothing (26.8 ms a frame against 27.0). |
| Keeping a growing patch's sums | Saved nothing either (26.8 against 27.3 ms). |
| The whole sea fixture on the changed build | Never run. The closed-form line FAILS when part 7 runs alone, on the untouched base too (x0.93, x0.96, x0.91: the fixture's, its offset line reads otherwise alone), so the proof has to be the whole fixture. |
| The report | Said "nothing has run". |

The first worker's design and its numbers are in section 9.

## 2. Where the time went (a measuring build, 23:27, the card to itself; `water8-test-m1-times2.log`)

The first worker's build 2 with switches in `water_sun` that stop it at a place or make one patch of a fixed size (they
changed the picture: for measuring only, out of the code again). The frame with the sun off: 3.91 ms.

| What ran (the sun's part of the frame, ms) | From the grid | Made at the point |
|---|---|---|
| all of it | 23.0 | 35.6 |
| only up to the patch's middle (the ray up, three corners) | 3.1 | 3.4 |
| that, and the shadow rays and the foam | 3.2 | 3.7 |
| no patch over the budget (the others as they are) | 8.6 | 14.0 |
| one patch of 4 x 4 cells for every point (25 turns of the loop) | 6.0 | 7.7 |
| one patch of 8 x 8 cells (81 turns) | 10.4 | 15.8 |
| one patch of 12 x 12 cells (169 turns) | 18.1 | 28.5 |

- A GPU runs 32 pixels as one group that loops until its slowest pixel is done. The patches every point makes cost
  5.4 ms; **the patches over the budget that a few points go on to make cost 14.4 ms**, though a point's own share of
  them is small: its group waits.
- A turn of the loop cost 0.088 ms from the grid and 0.15 ms made at the point. The 15 texture reads are 0.06 of it;
  the rest is the corner's own sums and its two triangles'.
- 3.2 ms are not the patch at all.

Two more things tried on the way and dropped, with their numbers:
- **Putting the triangles by** (a triangle whose corners land round the point kept in a list, its costly overlap with
  the sun's disc worked out after the corners' loop): it made the tiles SLOWER (17.3 ms a frame without, 18.7 with)
  and the patch made at the point no faster (33.6 ms against the base's 29.9). Out.
- **A corner carried from one turn of a loop to the next in a variable** (build 3): of a cell's two triangles the second
  then sent nothing, and the tiles gave exactly half the light (x0.554, x0.534, x0.535, the same with 2, 4 and 8 tiles,
  the same all over the picture). Found with switches (one triangle alone, counted twice, gave the whole light). The
  tiles' loop keeps two rows of corners now, as the patch made at the point always did. A source check names the line.

## 3. What is built: the tiles

### What a point does (where the grid can serve it)
1. **The grid**, as the first worker built it: a slot's 1024 x 1024 wave corners on its level plane round the camera,
   made once a frame (`pt_sun_grid.hlsl`), laid along the sun's bearing and across it in the cell the waves ask for
   (half a texel of their finest band: 7.6 cm for the sea), shifted by a random part of a cell every frame, as the
   caustics' grid is. A corner: the wave's point and its normal, `water_displace`'s own answer, 24 bytes.
2. **A record a tile** of 4 x 4 cells (its 25 corners; three more small dispatches of the same pass): the box of its
   corners' moves off their grid places, and the box of the rays still water's facets bend there, in the frame of
   still water's own bent ray; the least cosine a normal makes with the sun. A record a row of tiles, and one for the
   whole grid. 64 bytes a record, 4 MB a slot.
3. **The block.** From the whole grid's record the point works out which tiles any corner's ray could reach it
   through: a corner's landing place on the point's plane is its place plus its ray's reach times the ray's slant, and
   the record bounds all three (closed form; 25 to 60 tiles for most points).
4. **The test.** Each tile of the block against its own record: if every corner's landing place lies to one side of
   the sun's footprint round the point, no triangle of the tile covers the point. One load of 64 bytes and some 60
   sums a tile; no corner is read. 2 to 7 tiles are left for most points.
5. **The sum.** The tiles left are summed whole: every corner (two loads), every triangle, with the patch's own sums.
   More than 4 left (`SUN_PICK`): 4 are drawn evenly by chance and count for the rest. A block of more than 256 tiles
   (`SUN_TESTS`: a point deeper than about 10 m): every 2nd, 3rd, ... tile is asked, counted for the others.
6. **Any depth.** A corner that stands behind the point's plane (the plane lies across the bent ray, 35 degrees off
   level under a sun 40 degrees up: a corner one cell down the light's bearing stands 5 cm deeper along the ray) is
   followed BACK to the plane along its ray's own line. That is where the triangle's light lies on that plane. The
   patch made at the point gives such a corner up, which is why it needs cells of half the point's way to the surface.
7. Then, as before: one triangle, chosen by its light, says how much of the surface there is foam and sends the two
   shadow rays.

No patch grows, and nothing is made by chance for one pixel in 18: every pixel does the same bounded work.

### Where the patch made at the point stays (the base's own code: the same light, the old cost)
- a point whose block of tiles leaves the grid: **the sea past about 60 m from the camera**;
- a block of more than 4,096 tiles (a point deeper than about 25 m under steep waves);
- a tilt of the facets that the bound cannot hold under 0.1 radians: rough water (a smoothness under about 0.85) and
  the far tail of the lobe (for the sea's 0.947: one point in some thousands);
- a sun so low that some wave nearly loses it (the least cosine a normal makes with it near 0);
- a surface that is not one level plane (a river's bent mesh), a third and fourth slot, the Caustics row off.

## 4. Where the 9.7 ms are (a measuring variant of build 6, 01:17, part 7 alone, the card to itself; `water8-test-t4m-find.log`)

Build 6 with three switches that leave a part out (they changed the picture: for measuring only, out again; that
variant is `water8-stage9`, not the copy). The sun's part of its frame: 9.58 ms.

| Part of the sun's 9.6 ms | ms | How it was measured |
|---|---|---|
| before any tile: what else the sun turns on in the frame (its glint's shadow ray, the caustics pass), the ray up to the surface, the block's sums | 2.8 | no tile asked, no tile summed, no patch at the point |
| asking the tiles of the block | 0.7 | the tiles asked, none summed |
| summing the tiles left (4 at most, 25 corners each), the foam and the two shadow rays | 4.0 | the tiles summed too |
| **patches still made at the point** | **2.0** | the points the tiles do not serve given 0 light: the picture's sun share fell from 0.03772, 0.13802, 0.08781 to 0.03767, 0.13791, 0.08771: **they carry 0.13 % of the light** |

Which points those are was not counted. By the code, in this fixture (the camera stands inside the grid): the far tail
of the facets' lobe (a tilt the bound cannot hold under 0.1 radians), and the deepest points (a block of more than
4,096 tiles). Each makes a whole growing patch with its texture reads, and its group of 32 waits.
Before the tiles served the points just under the surface (build 4's measuring variant, `t2m-find`): 2.9, 0.7, 4.2,
and 2.3 ms at the point for 11 % of the light (red; 7 % green and blue).

## 5. Why the result is the same

- **The sum over triangles is what it was**, and the sum of the light is exact for any cutting of the surface into
  triangles (water-7, section 2). A tile left out holds no triangle that covers the point.
- **The test only ever keeps too much.** Its one soft spot is the tilt the patch's facets add to every ray (one draw of
  GGX's lobe for the whole patch). It has a hard bound: a facet stands off its wave's normal by an angle whose
  tangent is at most alpha sqrt(1 - z^2) / z (z from the patch's two tilt numbers and the least cosine any normal
  makes with the sun: `sun_tilt_slack`), and a ray bent into water turns by at most rate(c) times what its facet turns
  (a quarter head on, 1 grazing). Each record's box of rays is widened by that. On the CPU, 4,041 random suns, tilts
  and sets of normals: the ray's turn is 0.9998 of the bound at most (`water8b-work\slack_check2.py`).
- **What is drawn by chance is counted by its chance** (`kept`), as the budget's patches were. The mean is the same.
- **It is more whole than the patch that grows**: that one held what its own corners said could reach the point (times
  1.15); the records speak for every corner of the grid.
- **On the CPU, the shader's own lines in Python, single precision** (`water8b-work\tile_shader_cpu4.py`), against the
  sum over EVERY triangle of a block 8 tiles wider each way: 5 suns (22 to 89.9 degrees up, any bearing), points from
  3 mm to 24 m under the waves (inside crests too), slopes to 0.6, smoothness 0.85 to 0.99, the lobe's far tail:
  **2,400 points, none lost light to the block or the test** (the most: 7e-17 of a point's light, the rounding of a
  triangle that only touches the footprint's box).
- **A point just under the surface** (`water8b-work\near_cpu.py`: the grid's 7.6 cm triangles against a patch of cells
  of an eighth of the point's way to the surface, 3 suns, 150 points a depth):

  | Under the surface | A corner behind the plane followed back (built) | Such a corner given up |
  |---|---|---|
  | 2 mm to 1 cm | x0.9997 to x1.0003 of the fine patch's light; point by point within 2 % of the mean; no point dark | x0.03 to x0.18; 9 points in 10 dark |
  | 1 to 3 cm | x0.998 to x1.000 | x0.19 to x0.79 |
  | 3 to 8 cm | x0.995 to x1.004 | x0.69 to x0.99 |
  | 8 to 16 cm | x1.000 to x1.002 | the same (no corner is behind the plane any more) |
  | 16 to 50 cm (always from the grid) | x0.996 to x1.001; within 4 % | the same |

- **On the GPU**: the fixture's part 7 runs the same sea from the tiles and with every patch made at its point
  (`TGPT_TEST_SUN_GRID=0`), one DLL: x1.0000, x1.0003, x1.0005; and with 2 and with 8 tiles summed
  (`TGPT_TEST_SUN_PICK`): x0.9995 to x1.0001. With a debug build: accepting EVERY tile of the block, or a block 3
  tiles wider each way, changed nothing (0.0208, 0.0734, 0.0468 against 0.0209, 0.0736, 0.0470, in the build that
  still gave half): the test and the block drop no light there either.

### The noise
What is drawn by chance now is "4 of the tiles left", where it was "this patch of 400 corners, 1 chance in 8".
On the CPU's sea (smoothness 0.947; `water8b-work\tile_cpu.py`; the spread: the root mean square of a sample's error
from the drawing alone, over the mean light):

| Depth | Tiles left of those asked | Tiles that send light | 2 summed | 4 summed (built) | 8 summed | The patch by its budget (worked out from its corners, not run) |
|---|---|---|---|---|---|---|
| 0.3 to 1 m | 2.1 of 20 | 1.1 | 0.7 | 0 (all are summed) | 0 | 0 to 0.5 |
| 1 to 3 m | 3.5 of 32 | 1.3 | 1.1 | 0.3 | 0.04 | 0.6 to 1.5 |
| 3 to 6 m | 7.4 of 64 | 2.2 | 1.8 | 0.9 | 0.2 | 1 to 2 |
| 6 to 12 m | 19 of 150 | 6 | 2.3 | 1.3 | 0.8 | 3 to 4 |
| 12 to 24 m | 50 of 420 | 16 | 2.8 | 1.9 | 1.4 | 4 |

On the GPU: the largest single sample 15.4 times the picture's mean with 4 tiles (16.2 with 2, 14.5 with 8; the
base's patches: 26.6; every patch made at its point in the new DLL, other random numbers: 20.1).

## 6. What changed (all in the copy; against base58)

| File | Change |
|---|---|
| `native/pathtracing/shaders/pt_sun_grid.hlsl` | NEW. A slot's corners (stage 0), a record a tile (1), a record a row of tiles (2), the whole grid's record (3). No rays. |
| `native/pathtracing/shaders/pt_water_sun.hlsli` | The tiles, put in before the patch (`sun_frame`, `SunRecord` and its four functions, `sun_span`, `sun_tilt_slack`, `SunBent`, `SunAsk`, `sun_facet`, `sun_bend`, `sun_through`, `sun_triangle`; in `water_sun` the block from "From the slot's kept corners, in tiles" to "Made at the point"). The patch made at the point is the base's code: of its lines only the loop's head changed (`turn = made ? 40u : 0u`: it does not run when the tiles served the point), and one line was put in before it (`cellOfWaves`). |
| `native/pathtracing/shaders/pt_common.hlsli` | the frame constants: `float4 sunGrid[8]` (two rows a slot); `waterGeometry`'s two spare words named (the buffer; a test's count of tiles to sum) |
| `native/pathtracing/src/tracer.cpp` | the grid's constants each frame (the first worker's), the buffer (59 MB for two slots, made when a slot first has a grid), the pass and its four dispatches a slot before `prepare`, the tests' switches `TGPT_TEST_SUN_GRID` (0: no grid) and `TGPT_TEST_SUN_PICK` (1 to 8 tiles summed); the frame constants are 8048 bytes (were 7920) |
| `CMakeLists.txt`, `pt_shaders.hpp.in` | the new pass (in the list of passes that trace no rays: one build for both shader sets) |
| `tests/water_sea_gpu.inl` | part 7 also runs the same sea with every patch made at its point, and with 2 and 8 tiles summed: the same light (within a hundredth), each way's time and largest sample; a frame's time is the mean over the frames counted (it was the last frame's) |
| `tests/source_checks.py` | the frame constants' size; the water's check names the tiles (96 of 96 pass) |

No export, no struct of the API, no option bit: no ABI change. The plugin is not touched.
What the first worker built and the finisher took out again: `water_sun`'s growing patch read from the grid (the block
of corners, the step for wide patches, the sums kept when it grows) and its test bit.

## 7. For the coordinator's ONE game scene (`water-day` with `quick`), and the coordinator's facts from the game

- **The frame time to expect: about 135 to 150 ms** (7 frames a second), where it is 235 ms with the Caustics row on
  (run `20261009-010307`; 223 in the run before), 57 ms with the row off, and was 78 to 93 ms with the row on before
  the sun lit the water. Worked out so: the sun in the water added about 150 ms; the fixture's deep water went from
  31.0 to 9.7 ms (x0.31), which would leave about 47 ms; and the sea past 60 m (a tenth of the sea's pixels) keeps its
  old cost, about 10 ms more. Read the status line's "water reflections" stage: that is where it shows.
- **The open sea's light must not move:** x3.6 / 2.7 / 3.3 of the game's with no denoiser (run `20261008-194341`).
  The fixture says x1.000 within a thousandth; the game's sea box is the check on the real waves.
- The noise in the sea's body should be a little less, not more (no denoiser: fewer single bright pixels in deep water).
- New in the native log only if the buffer could not be made: "The water's kept wave corners could not be made ...;
  the sun's patches inside the water are made at their points" (then the old cost, the same light).
- If the frame time is NOT near that: `TGPT_TEST_SUN_GRID=0` in the game's environment gives the old way (every patch
  at its point) with the same DLL, to tell the tiles' part from the rest.

### What the Caustics row's "off" does differently (read from the code; the coordinator's question, 01:10)

| | The row ON | The row OFF |
|---|---|---|
| A point where a path scatters INSIDE the water (this job) | the sun through the waves: bent at each wave, Fresnel's share mirrored away, summed over every way through (the tiles; before: the patch) | ONE straight shadow ray to a point of the sun's disc (`water_sun`'s third line), the water taking its share over the ray's way (`water_straight`): no bending, no loss at the surface, no patch, no tile, and no grid is made (`tracer.cpp`). The fixture: x1.068, x1.062, x1.065 of the row on; 2.1 ms a frame against 13.6 (35.2 before this job) |
| A surface UNDER the water (the bed, a rock, a crab) | NO straight sun: a straight shadow ray stops at the waves. Its sun and lamps are only what the caustics pass landed on it (`pt_water_path.hlsli`: `caustic_light`) | `direct_light`'s own straight shadow rays, which pass the water (`light_mask` drops the waves) |
| The caustics pass (`pt_caustics.hlsl`) | runs | does not run |

So in the game all of the row's cost is the caustics pass plus this job's part, and with the row off none of this
job's code runs. The row's "off" is brighter by what the surface would mirror away (the fixture's 6 to 7 % of the
sun's share), which is water-4's meaning of "off", not this job's.

### The dark crab (row on): it is the caustics pass, not the sun's patches

Read from the code, not run. `water_sun` (this job) lights only points where a path scatters inside the water. It
never lights a surface. A surface under water with the row on gets its sun from `caustic_light` alone, and the
caustics pass writes a landed triangle only when its three corner rays landed on ONE surface (`pt_caustics.hlsl`
`emit_triangle`, lines 270 to 281: the three smooth normals within 60 degrees of each other, and the landed triangle
lying along that surface). A triangle that fails is LEFT OUT, and its light with it; the comment there says so: "its
light is missed within one landed cell of such an edge". The cells are 2 cm at the finest level and double with each
level out from the camera. A crab is a small rounded thing with legs: nearly every triangle that lands on it has a
corner on a leg, on the shell's other side or on the bed beside it, so nearly all its light is left out, and it has no
other sun. The same seam darkens a pebbly bed a little, which fits the shallows' -6 % together with what the surface
mirrors away (2 to 6 %).
It is NOT that the rays skip small or moving meshes: the caustics' rays are `trace_bounce`, the bounced light's own
rays (its instance mask I did not read further). So: a defect of the caustics route for anything smaller than a few
cells: a left-out triangle's light is dropped where it should go to what its corners met. Not this job's; an entry for
PT-REQUESTS (the coordinator files it).

### The missing pattern on the bed (noted, not chased)

Worked out from the waves' numbers, not seen: with slopes to 0.32 over wavelengths of 60 cm and more (water-7, section
1), a ripple brings the sun's light to a line about 2 m under the surface (the wavelength over 2 pi x slope x 0.25).
On a bed 20 to 50 cm down the pattern can only be a mild brightening and dimming over a hand's width and more, which
16 frames with no denoiser may not show to the eye. Whether the game's own picture has sharper lines there than those
waves can make (HDRP's caustics are a ready-made texture) is a thing to look at in the game's frames; this job did not.

## 8. What is left, and what the next steps would be (plans, not built; the numbers are section 4's)

| Step | What it is | What it would save of the 9.7 ms | What it needs |
|---|---|---|---|
| 1. The rare points still made at the point | One point in about a thousand (the lobe's far tail, the deepest points) makes a whole growing patch and holds its group up: 2.0 ms for 0.13 % of the light. Either the tiles take them too, a block asked in part (every n-th tile, counted n times: the code is there, its limit of 4,096 tiles raised and the tilt's limit of 0.1 dropped), or the tilt gets a tighter bound (the records hold the normals' box too) | about 2 ms | The first way costs noise in that one sample in a thousand (counted up to some hundred times, where a budget patch is counted 18 times): the largest-sample number must be looked at before it ships. The second is more work and no noise. |
| 2. Smaller tiles | 4.0 ms go to 100 corners a point that find the 2 to 6 triangles round it. Tiles of 2 x 2 cells (9 corners), asked through a coarser level of records (8 x 8 cells) first: 6 tiles of 9 corners for the noise of 4 of 25 (the CPU's sea: `tile_cpu.py` with `TILE = 2`) | about 2 ms | 17 MB more a slot; a two-level loop for the asking. |
| 2b. `SUN_PICK` 2 in place of 4 | measured: 8.8 ms (with 8: 10.0) | 0.9 ms | Nothing: one number. The noise is then about the base's (largest sample 16.2; the CPU's spread 0.7 to 2.8 against the budget's 0.5 to 4). Left at 4: less noise was worth more than 0.9 ms. |
| 3. The sea past 60 m | Every patch at its point there, the old cost: about a third of what is left in `water-day` (section 7) | in the game about 10 ms a frame; nothing in the fixture | A ruling first: the waves' finest band asks cells of 7.6 cm as far out as it does not fade, so a grid to the horizon is not affordable at that cell (2,048 corners a side: 100 MB a slot, to 125 m). May the far sea, where a pixel covers more than a cell, take a coarser cell (the sum of the light stays exact; its pattern gets coarser than the waves, under a pixel)? |
| 4. Rough water (a smoothness under about 0.85), a sun so low that a wave nearly loses it, a river's bent mesh, a third and fourth slot | the patch at the point | nothing for the sea | Each is a case of its own. |

Steps 1 and 2 together: about 5.7 ms, a fifth of the base. Both are new code on the path every water pixel takes:
each wants its CPU check and the fixture, as build 6 had.

## 9. The first worker's design and numbers (kept: the grid is its; the patch from the grid was replaced by section 3)

The grid: one a water slot, on the slot's level plane, 1024 x 1024 corners, two slots at most (the first two that have
waves and a flat, level surface). Its two directions are the ones a patch over still water takes: along the sun's
bearing (the cell over the bent ray's cosine) and across it; its cell is half a texel of the slot's finest band, taken
3 % smaller. It lies round the camera, pushed forward along the view, shifted by a random part of a cell every frame.
76 m by 93 m for the sea with the sun 40 degrees up. Within one frame all points share one cutting of the surface (the
mean over frames is the mean over every cutting; the caustics on the bed work this way already).

Its patch from the grid (build 1, run `b1-body`, part 7 alone, another test beside it):

| The same sea, 256 frames | The sun's share against patches made at their points | Largest sample | GPU a frame | The sun's part |
|---|---|---|---|---|
| every patch made at its point | 1 | 18.4 | 45.4 ms | 40.4 ms |
| from the grid, starting over at each growth | x0.9993, x1.0002, x0.9999 | 23.5 | 34.3 ms | 29.3 ms |
| from the grid, sums kept | x0.9992, x1.0003, x1.0002 | 20.2 | 27.0 ms | 21.9 ms |

The same build with the card to itself (`p1-new-1`, 21:04): 26.8 ms a frame from the grid, 39.7 made at the point, the
base 34.0: the sun's part 22.7 ms against 29.9. Its index logic was worked on the CPU (`water8-work\grid_cpu.py`).

## 10. Workarounds and stand-ins met (AGENTS.md: listed, removed or not)

| What | Where | Removed? |
|---|---|---|
| A patch of more than 48 corners made by chance (water-7's `SUN_BUDGET`: costs noise, not light) | `pt_water_sun.hlsli`, the patch made at the point | For what the tiles serve: REPLACED by "4 of the tiles left" (less noise, section 5). Still there for what they do not serve. |
| The patch that grows by what its own corners saw (times 1.15) | the same | For what the tiles serve: REPLACED by the records' bounds (every corner of the grid). |
| The sea past 60 m, rough water, a low sun, a river's bent mesh: the patch at the point, the old cost | `pt_water_sun.hlsli` | NO: named in sections 3 and 8. A cost, not a stand-in: the same light. |
| A grid for the first two slots only | `tracer.cpp kSunGrids` | NO (memory: 29 MB a slot). |
| A tile's record holds boxes (it keeps too much, never too little) | `pt_sun_grid.hlsl` | Not a stand-in: a bound. |
| Lamps light the water's body by chance only; foam at the shore missing; caustics only in 1.5 times the view | water-7's list | NO: not this job's. |

## 11. Tests: what ran, and how to run them again

From PowerShell, the game closed. `$w` is `<session scratch>\water8-work`. `run.ps1` waits for the main tree's
`Run-PtHarness.ps1 -GpuCheck` itself (one waiting shell), sets `TGPT_TEST_STARTUP_SECONDS=900`, writes
`%TEMP%\tgb\water8-test-<Name>.log`. A new DLL's first start makes its passes for 3 minutes; the fixture itself takes
a minute.

| Run | Build | Result |
|---|---|---|
| `base-sea` (20:17), `base-sea2` (00:54), the whole fixture | the untouched base58 (the second with the final test program) | `PASS sea checks`; the "before" column |
| `b1-body`, `b2-body`, `p1-base-1`, `p1-new-1` (part 7 alone) | the first worker's builds 1 and 2 | section 9 |
| `m1-times2` (23:27), part 7 alone | build 2 with measuring switches | section 2 |
| `t1-body` (23:53), `d1-find` (00:15) | build 3 (the tiles, half the light) and its debug build | section 2: the fault found |
| `t2m-find` (00:30), part 7 alone | build 4's measuring variant | the light right; where its 10.2 ms were |
| `t3-sea` (00:50), the whole fixture | build 5 (the tiles; the points near the surface still at the point) | `PASS sea checks`; the sun's part 10.5 ms |
| **`t4-sea` (01:02) and `t4-sea2` (01:20), the whole fixture** | **build 6 (the copy; `water8-stage8`, DLL sha256 7E6A20C7...)** | **`PASS sea checks` both; the "after" column** (the first run's parts 1 to 5 had something else on the card: their times are the second run's) |
| `t4m-find` (01:17), part 7 alone | build 6's measuring variant (`water8-stage9`; not the copy) | section 4 |
| `python tests/source_checks.py` | the copy | 96 of 96 (01:19, after build 6's files were put back from the measuring variant) |
| the CPU's side | `water8b-work\tile_shader_cpu4.py 50 1.0 0.947` (the tests against every triangle), `near_cpu.py` (just under the surface), `slack_check2.py` (the tilt's bound), `tile_cpu.py` (the tiles left, the noise) | section 5 |

Again, by hand:
- the whole sea fixture: `& "$w\run.ps1" -Name sea -StageName stage8 -Grep 'SEA|FAIL|PASS'`
- part 7 alone (its closed-form line fails alone, on any build): add `-Env @{TGPT_TEST_SEA_PART='body'}`
- without the scripts: `$env:TGPT_TEST_WATER_SURFACE='1'; $env:TGPT_TEST_WATER_SEA='1'; $env:TGPT_TEST_STARTUP_SECONDS='900';
  & "$env:TEMP\tgb\water8-stage8\tgpt_gpu_test.exe" "$env:TEMP\tgb\water8-stage8" "$env:TEMP\tgb\water8-images-sea"`
- a build: `& "$w\build.ps1" -Name x -StageName stageX` (Ninja, 8 jobs, NRD off, into `%TEMP%\tgb\water8-build`).

**NOT run, and whose it is:**
- the whole GPU test and the whole of ctest (the brief: never in a worker): the coordinator's, after the merge:
  `ctest --test-dir build\native -C Release --output-on-failure`;
- the water's own fixture (`water_surface_gpu.inl`: `& "$w\run.ps1" -Name surface -StageName stage8 -Surface -Grep 'FAIL|PASS'`):
  its code is not touched, and it has no sun in deep water; not run by the finisher;
- the NRD build (the copy builds with `-DTGPT_WITH_NRD=OFF`): the coordinator's merged build;
- mutation checks (the rule: not until the owner says a thing looks right); the fault of build 3 stands in for one:
  with half the triangles gone the fixture's new line read x0.55 and FAILED;
- anything in the game (section 7).
