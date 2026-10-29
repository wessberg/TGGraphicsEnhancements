# Water 7 - the sun lights the water's body, the water's own smoothness, the foam (PT-81, PT-85)

Copy: `wt\water7` (base `wt\base56`). Worker: Claude. Started 2026-10-08 about 17:50.
Nothing is written to the main tree or the game folder; the game is never started; nothing deployed or committed.
Builds: `%TEMP%\tgb\water7-*`. Scripts: the session's `scratchpad\water7-work\`.

## Read this first

**All three steps are built and have run on the GPU. The last build passes the whole sea fixture (`PASS sea checks`,
19:30, `%TEMP%\tgb\water7-test-sea23b.log`) and the water's own fixture (`PASS water surface checks`, 97 ok).
Nothing has been seen in the game: that is the coordinator's run (section 5).**

| Step | What it did | Its number |
|---|---|---|
| 1. The sun lights the water's body | Every point where a path scatters inside the water asks for the sun through the waves; the chance hit of the sun's disc is dropped where that covers it. The sea's body is green, not near-black. | The sun's share of the deep sea's picture 0.038 / 0.138 / 0.088 (was 0.000); against the closed form x0.99 to x1.01; waves against still water x1.000; the largest single sample 26.6 times the picture's mean (the untouched tracer: 2,263). |
| 2. The water's own smoothness | The surface is HDRP's smoothness, near and far (it was a clear mirror facet). The sun's glint is asked for through that lobe. | The glint's light x1.02 (smooth), x1.01 (the game's 0.947), x1.000 (0.7) of a mirror's; at 0.947 its peak is 0.71 of the smoothest's over 1.4 times the area; no sample over 5.3 times its pixel's mean. |
| 3. The foam, our own (the owner's choice, 17:55) | No port of Unity's left. Foam starts where the water is squeezed markedly; the game's texture says which part holds out (the coarse web's borders, then the fine web). | Foam on 1.6 % of the fixture's sea (the game's own sea: 1 to 3 %; the build before: 46 %); every foam pixel has open water two pixels away; none at rest. |

**Three things to know before merging:**
1. **It is slow.** Step 1 costs 30 ms a frame for 230,000 pixels of deep water (34 ms against 4 with the sun off, at
   640 x 360). At the game's size with the sea on 40 % of the screen: about 100 ms a frame more. The picture is right;
   the frame rate is not. The cure keeps the result (section 6: the patches' corners kept in a buffer once a frame);
   it is a job of its own.
2. **The foam's amount is fitted to one scene** (`FOAM_ONSET` 0.45, from the game's own frames of `water-day`). The
   plugin's new log line (`Water: "Ocean" squeeze ...`) is what settles it at the next game run.
3. **Foam at the shore is still missing** (HDRP's foam buffer is not handed to the tracer: water 4's finding), and
   lamps still light the water's body by chance only.

ABI: the number and every struct's size are unchanged. BUT the plugin and the DLL must be of the same build (as they
always are): step 2 gives meaning to fields of `TGPT_WaterCaustics` that were spare (`flags` bit 1, `scattering[3]`,
`foamColor[3]`, and the two spare numbers of `foamMaps`, now `smoothnessFade`). An old plugin with the new DLL gives
the old clear mirror facet (the flag is not set), nothing worse.

Coordinator's messages:
- 17:55: the card is free for workers again (my one fixture, gated by the main tree's `-GpuCheck`, one waiting script).
  The foam: the owner chose "our own foam". Step 3 replaces the erosion lines of `water_foam` with a formula of our own.

## 1. The game's own numbers for the sea (run `20261008-144459`, `LogOutput.log` line 861, and the native log)

| What | Value |
|---|---|
| "Ocean" | OceanSeaLake, Infinite; 3 bands of 250 / 50 / 10 m, 64 x 64 texels each (a texel 3.9 m / 0.78 m / 0.156 m) |
| fades | band 0 from far out (a -0.333e-3, b 1.25); bands 1 and 2 never fade |
| the waves | move up to 0.665 m; as the tracer reads them: up to 23.9 cm high, slopes up to 0.32 |
| scattering colour | 0.257 / 0.604 / 0.469 (green) |
| what the water takes a metre | 0.377 / 0.258 / 0.263 (absorption distance 4 m, refraction colour 0.614 / 0.736 / 0.731) |
| smoothness | 0.947 near, 0 far, from 100 m over 500 m |
| foam | on; the surface's amount 1, HDRP's threshold 12.233, intensity 0.54, tiling 0.25, smoothness 0.3, colour white, the mask read |
| the sun's disc | the plugin sends HDRP's angular diameter (`SceneLights.cs` line 799), not logged |

So a path in this water goes 2.7 to 3.9 m between two scatterings, and 26 % (red), 60 % (green), 47 % (blue) of what
it meets is scattered on. Lit by the sun that is a green body. Lit by the sky alone (today) it is near black.

A rough count of what the body should be: still water, the sun 40 degrees up, seen from 2.6 m over it. One scattering
gives about 0.3 times the sunlit sand's brightness in green; with the further scatterings 0.45 to 0.6. The game's sea
is 0.39 (near) to 0.73 (far) times its sand (water-6, section 1). So the right order.

## 2. Step 1: the design (written before the code)

### What was wrong
`pt_water_path.hlsli`: a path that scatters inside the water picks a new direction by chance and goes on. It gets the
sun only if it then leaves the water straight at the sun's disc (line 56). The disc fills about 1 part in 100,000 of
the directions, so 1 path in 100,000 carries the whole sun: the green dots, and the dark body in every other frame.
The same line also counted the sun twice for the bed: a bed point gets the sun from the caustics (`caustic_light`),
and a path leaving it through the surface at the disc got it again.

### Why not one bent shadow ray
A straight ray from the point to the surface, bent there, does not point at the sun: the wave's tilt at that place
decides where it goes. The point is lit through the places of the surface whose tilt is just right, and there can be
several (deep under ripples: many). Finding them is the whole job. What I weighed:

| Way | Verdict |
|---|---|
| A ray up by chance, bent at the wave, counted if it meets the disc | today's. 1 in 100,000 (1 in 200 with good aim). Dots. |
| Solve for the place (Newton, "manifold next event") | finds one place; where several light the point it loses the others: too dark. The unbiased form (repeat trials to count each place's chance) is noisy, has a loop without an end, and its samples at a caustic's fold are unbounded: dots again. caustics-3 ruled it out too. |
| The caustics pass's beams, kept in an acceleration structure | the right carrier in principle. caustics-3 built it: 550 to 670 ms a frame, a GPU hang (boxes of long slanted beams hold half the room). Not again. |
| **The same warped light mesh, made at the point (chosen)** | below |

### The chosen way: a small patch of the caustics' own mesh, made where it is asked
It is the method the caustics pass uses for the bed ("Real-Time Ray Traced Caustics", Ray Tracing Gems II, ch. 30:
a grid of rays over the water, each cell's light spread over the area its corner rays land round), asked from the
other end:

1. From the scatter point P a ray goes up along the sun's direction as still water would bend it. Where it meets the
   waves is the middle of the patch.
2. A grid of surface points round that place (cells of half a texel of the finest wave band in reach: 7.8 cm for the
   sea). At each corner: the wave's place and normal (`water_displace`, the game's own waves), the sun's ray bent there
   (exact Snell and Fresnel), the foam's cover (foam lets no sun through).
3. Each corner's bent ray is followed down to the plane through P across the light. A cell's light (the sun through its
   piece of surface, less what the surface mirrors and the foam covers) lies on the triangle its corners land on. P
   takes it where P lies in that triangle. Rays that bunch give a small triangle and much light, rays that spread
   little: the caustic inside the water, with no noise from the method. Several places lighting P are several
   triangles over P: all counted.
4. The sun is a disc, not a point: P takes each triangle's light by how much of the disc's footprint round P the
   triangle covers (the disc seen through the surface is an ellipse; exact overlap of a triangle and a circle). This
   is what keeps a fold's light finite: no sample can be brighter than the real sun makes it.
5. One landed cell is chosen by its light, and two shadow rays are sent for it: P to its place on the surface (the bed,
   a rock, a hull in the way), and from there to the sun (a cliff, a cloud of leaves, glass's tint, the fog).
6. The water takes its share over the bent ray's own length (per colour).

How large the patch is: its middle first takes two steps towards a place whose ray lands on P; then the patch grows
until it holds everything its own corners say can reach P (the farthest any corner's ray strays from the middle's,
times 1.15). No number of the surface's is needed, and nothing is cut: a wider patch costs time only.
Past 32 x 32 cells the cells grow instead (the pattern of the light gets coarser there, the sum stays exact).

What is exact and what is a resolution:
- The sum of the light is exact for any cell size: every cell's light lands somewhere, once.
- The fine pattern of the light inside the water (shafts) is as fine as the cell. Half a wave texel shows waves of
  four texels and more within 8 %. It is the same kind of resolution as the caustics' 2 cm grid.
- The patch lies on the surface's level plane round its middle. For the sea, lakes and pools that is the surface.
  A river's bent bed mesh is flat over a metre or two to within little; named here as an approximation.

### Counted once
A path's sun now comes from: (a) this, at a scatter point in the water; (b) the caustics' landed light at a surface
under water (as before); (c) the straight shadow ray at a surface in the air (as before). The chance hit of the disc
stays only for what none of these covers:
- dropped: scatter point or bed, then ONE crossing of the surface, then the disc ((a) and (b) cover exactly that);
- kept: the camera's own mirror chain (the sun's image in the water, the sun seen from under water), and a chain
  with two or more meetings of the surface (light that no question above asks for; with waves as flat as the sea's
  it cannot happen: a ray that came in through the surface goes down too steeply to meet it again).

### With the Caustics row off
The row's "off" is the straight light (water-4's decision: no bending, no Fresnel loss, for water as for glass).
So with the row off the scatter point asks the sun with one straight shadow ray through the surface, and the water
takes its share over the ray's way under water (`water_straight`). No patch is made. The body is lit; it has no
shafts. The chance hit is dropped the same way.

### What is not in step 1
- Lamps do not light the body yet, except by chance (a path leaving the water straight at a lamp's ball). The same
  defect as the sun's was, weaker (a lamp's ball is wide seen from near). Named, not built.
- The bed outside 1.5 times the camera's view still has no caustic (`in_view`, water-6 section 9); and now it has
  lost the chance hits that doubled it elsewhere. A mirror's bed there is darker than before. The fix is `in_view`'s.

### Cost, and the one thing drawn by chance
A corner costs 15 texture reads (3 bands: the move, and the gradient bicubic). Counted on the CPU (section 2b): a
scatter point 0.5 m deep needs about 35 corners, 2 m deep 80 (waves with slopes to 0.17) to 310 (ripples with slopes to
0.33), 5 m deep 900. At the game's 1924 x 1084 with the sea filling half the screen that is several thousand million
texture reads a frame if every patch is made: too slow to show.
So a patch of more than `SUN_BUDGET` (48; 96 in the first run) corners is made with the chance 48 over its corners
and counts that much more when it is. The mean is the same; the price is noise in deep water (a sample there counts
up to 18 times; the largest sample seen in the fixture's deep sea is 26.6 times the picture's mean).
This is a budget that costs noise, not light. The cheap way later, without changing the result: the caustics pass
already makes these corners over all the water each frame; kept in a buffer, a corner is two reads instead of 15, and
the budget can go up tenfold.

## 2b. Step 1 worked on the CPU (`water7-work\sun_cpu.py`)

The shader's arithmetic in Python, on a sea of nine waves (swell, wind waves, ripples of 0.55 to 1.3 m; sideways move
2.25 as HDRP's), against a count: the sun's own rays sent through 2.5 million cells of the surface, each from a point
of the disc, counted in 2 cm bins on the plane under water. 256 points each.

| Depth, slopes | The count against still water | The estimator against the count (mean) | Point by point | Largest sample over the mean | Corners a point |
|---|---|---|---|---|---|
| 0.5 m, to 0.28 | x0.990 | x0.9999 | correlation 0.98 | 3.2 (the count's 3.3) | 34 |
| 2 m, to 0.33 | x0.998 | x1.011 | 0.95 | 7.5 (5.8) | 312 |
| 2 m, to 0.17 | x1.004 | x0.966 | 0.95 | 5.4 (4.8) | 80 |
| 5 m, to 0.33 | x1.111 | x0.974 | 0.88 | 4.9 (4.6) | 902 (the 32 x 32 grid) |

Read: waves take no light away (the count's mean is still water's); the estimator has the count's mean within the
count's own noise (256 points of a pattern that swings from 0.2 to 12 times its mean); the pattern agrees point by
point; no sample is far over the mean, as the count's are not. With cells of a whole texel instead of half the
correlation falls to 0.81 and the largest sample rises to 10 times: so half a texel.
The overlap of a triangle and a circle: against a count of points over 300 random triangles, off by 0.0055 at most
(the count's own noise 0.006).

## 2c. Step 1 on the GPU (the fixture's part 7, `TGPT_TEST_SEA_PART=body`, 640 x 360, no denoiser)

A camera 4 m over water 24 m deep, looking down and away from the sun: every pixel is the water's body and the sky's
mirror image. "The sun's share" is the picture with the sun on less the picture with the sun off.
Logs: `%TEMP%\tgb\water7-test-body1.log` (changed) and `water7-test-body1-base.log` (untouched tracer, same test program).

| What | Untouched tracer | Changed (budget 96) |
|---|---|---|
| the game's sea, whole picture (R, G, B) | 0.064, 0.118, 0.146 | 0.101, 0.255, 0.234 |
| the sun's share of it | 0.0001, 0.0002, 0.0002 (nothing) | 0.038, 0.138, 0.088 (green) |
| the same sea as still water | - | 0.038, 0.138, 0.088: x1.000 in each colour |
| the largest single sample of any pixel, over the picture's mean, 256 frames | 2,262.8 | 15.4 (8.0 with the sun off; 14.7 in still water) |
| 16 frames' mean over 256 frames' | 0.998 | 1.0005 |
| the Caustics row off: the sun's share | 0.0000, 0.0001, 0.0001 | 0.040, 0.146, 0.093 (x1.06 of the row on: no loss at the surface, the row's meaning) |
| GPU time a frame, the sun on | 6.5 ms | **39.2 ms** (4.0 with the sun off; 5.9 in still water; 2.3 with the row off) |

- The check "no sample far over the mean" FAILS on the untouched tracer (2,263) and passes on the changed one: it sees
  the dots.
- The closed form for one scattering in still water: the first run's line reads x10. That was the fixture's own
  mistake (its camera rays for the formula pointed the wrong way: grazing, where the surface lets a tenth through).
  Worked by hand for the picture's mean angle the tracer's value is 0.94 to 0.98 of one scattering before the further
  scatterings are added. The repaired check runs in the second run.
- **The second run** (19:05, `water7-test-body2.log`; the budget at 48 corners, 2 steps to find a patch's middle): the
  game's sea with its waves x1.000, x1.001, x1.001 of still water; 16 frames' mean 0.9993 of 256 frames'; the largest
  sample 17.0 times the mean; 32.0 ms a frame (with the sun off 8.2, another worker's test ran beside it).
  The closed form, with the fixture's formula repaired: the sun's share 0.00054, 0.00135, 0.00101 against 0.00060,
  0.00141, 0.00110 (x0.89, x0.95, x0.92). The gap is the same number in every colour (0.00006 to 0.00009), and the
  untouched tracer shows it too: with the sun ON its picture of water the sun cannot light is 0.00009 DARKER than with
  the sun off. So it is the test's, not the water's: the traced light is kept in units of the brightest light, and
  its rounding moves when the sun comes on. Taken off, the tracer is x1.05, x1.02, x1.00 of one scattering (the next
  scatterings add 1 to 2 %). The fixture now measures that offset itself (the same water with no scattering colour)
  and takes it off. **Run in that form (19:16 and 19:30): x0.990, x1.007, x1.012 of the closed form; the check
  passes.** The offset it measured: -0.00003, -0.00004, -0.00007.
- **Halving the budget saved a fifth.** So the time is not in the large patches but in every patch's fixed part: the
  ray up, 3 corners to find the middle, a first patch of 9, a second of 16 to 49. 40 to 60 corners a scatter point
  at 15 texture reads each, two scatter points a path.
- **The cost.** 30 ms for 230,000 pixels of deep wavy water (the last run: 33.6 ms with the sun, 4.0 without). At the
  game's 1924 x 1084 with the sea on 40 % of the screen that is about 100 ms a frame more: the sea scene would run at
  6 to 8 frames a second. It is the price of the patches (a corner is 15 texture reads). The cure is section 6: the
  corners kept in a buffer once a frame.

## 3. Step 2: the water's own smoothness

What was wrong: the tracer's water was a clear mirror facet at the wave's normal. The sun's glint was a chance hit of
the sun's disc by the mirrored direction: single burnt pixels. The game's water has a smoothness: for "Ocean" 0.947 near
the camera, going to 0 between 100 m and 600 m. Far out the game's sea is matt; the tracer's was a mirror to the horizon.

What it is now:
- **The numbers.** The plugin sends the surface's `startSmoothness`, `endSmoothness`, `smoothnessFadeStart` and
  `smoothnessFadeDistance` (they were only logged). The shader blends near to far by the distance from the camera
  (`water_smoothness`, `pt_water_surface.hlsli`). Where this comes from: the meaning of HDRP's four settings as its
  documentation names them (a start value, an end value, a range of distances). No shader of Unity's was read; the
  straight blend between the two ends is my reading of "from ... over ...". 1 minus the smoothness, squared, is the
  width of the facets' lobe (GGX), as everywhere else in the tracer.
- **The surface** (`pt_water_path.hlsli`): where a path meets the water, one of the surface's small facets is drawn
  from the lobe (the facets the path can see, as glass does it in `pt_dielectric.hlsli`), and that facet mirrors the
  path or lets it through by the exact Fresnel share. So the sky's mirror image, the bed seen through the water and the
  lamps' glints are all soft by the water's own smoothness.
- **The sun's glint** is asked for: at every meeting of the surface from the air, a point of the sun's disc is drawn
  and the lobe says how much of it the surface mirrors to the path (exact Fresnel at the facet that does it), with a
  shadow ray. The chance hit of the disc after a mirroring is dropped (`glinted`): counted once.
- **The sun into the water** (step 1's patch): each corner's ray is bent at a facet tilted by one draw of the lobe for
  the whole patch. Near the camera (0.947) that tilt is a sixth of a degree and changes nothing to see; far out it
  spreads the light inside the water as a rough surface does.
- **A lamp's glint** stays as it was: the lobe's own directions meet the lamp's ball. Near the camera the ball is wider
  than the lobe and that is the good way. Far out on rough water it is a rare bright hit (noise). Named: a lamp sample
  weighed against the chance hit is the cure; not built.
- Not changed: the denoisers' guide still says "roughness 0" for a water pixel (`pt_primary.hlsli`). The brief says
  not to touch what the filters see to hide anything; the truthful value there would be 1 minus the smoothness. For the
  coordinator to decide.

water-4 had earmarked the two spare numbers of `foamMaps` for the shore foam's buffer. That job needs more room than
they give (the buffer's region too), so it must grow the struct anyway.

## 3b. Step 2 on the GPU (the fixture's part 8, `TGPT_TEST_SEA_PART=glint`; run 19:16, `water7-test-sea23a.log`)

Still water with no scattering colour, the camera looking where the sun's image is. "Sends the camera" is the image's
light added up over the picture; a mirror sends Fresnel's share of the sun (0.1324 here).

| Smoothness | Sends the camera | Brightest pixel | Pixels over a tenth of that | Largest sample over its pixel's mean |
|---|---|---|---|---|
| 1 (no smoothness sent) | 0.0657 (x0.50) | 253.5 | 52 | 3.7 |
| 0.947 (the game's sea, near) | 0.0849 (x0.64) | 256.0 | 66 | 3.2 |
| 0.7 | 0.1324 (**x1.000**) | 1.6 | 21,066 | 3.3 |

- Rough water: the lobe keeps the sun's light exactly, as a sheen over a tenth of the picture.
- The two sharp ones read low because the picture itself is cut there: **the composite caps a pixel at 256 times the
  exposure's white** (`pt_compose.hlsl rr_clamp`, not mine and not touched). The sun's image in smooth water is over
  1,000 at the fixture's exposure. On a screen both are burnt-out white, so the eye loses nothing; the sum does. The
  check now runs at a sixty-fourth of the exposure, where the whole image is under the cap. A thing for the list: a
  cap of 256 is a clamp, and what comes after the composite (bloom, an upscaler's history) sees the cut value.
- No sample of a glint is more than 3.7 times its pixel's mean: asked for, not found by chance.
- **The last run (19:30, at a sixty-fourth of the exposure, `water7-test-sea23b.log`): all of the glint's checks pass.**

  | Smoothness | Sends the camera | Brightest pixel (times 64 for the exposure of 1) | Pixels over a tenth of that | Largest sample over its pixel's mean |
  |---|---|---|---|---|
  | 1 (none sent) | x1.020 of a mirror's | 19.5 (1,248) | 23 | 5.3 |
  | 0.947 | x1.008 | 13.8 (883) | 32 | 4.5 |
  | 0.7 | x1.000 | 0.03 (1.6) | 21,066 | 3.3 |

  Read: near the camera the game's sea is nearly a mirror by its own number (0.947), so its glints stay small and
  bright there, a little softer than the smoothest surface's (0.71 of the peak over 1.4 times the area: what the two
  lobes and the sun's disc give). They become a wide soft sheen where the game's smoothness falls: from 100 m out.
  Whether the game's own near glints are softer than that I could not see (the game's kept frames are overcast).
- The body's checks with the smoothness on (the same run): the closed form x0.990, x1.007, x1.012; waves against
  still water x1.000, x1.000, x0.998; 16 frames 0.9989 of 256; the largest sample 26.6.

## 4. Step 3: the foam

### What I found out (the brief's questions)

**The game's foam texture, looked at** (HDRP's `FoamMask`, 2048 x 2048, found in the game's own
`globalgamemanagers.assets` and read with UnityPy into the session's scratch folder; nothing of it is in the tree).
Its four channels are four different things, all unrelated to each other (correlations 0.00 to 0.03):

| Channel | What it holds | Size at the sea's tiling (0.25: the texture every 4 m, a texel 2 mm) |
|---|---|---|
| alpha | a COARSE WEB: bright borders round dark cells | cells of 20 to 50 cm, borders 2 to 4 cm |
| red | a FINE WEB of the same kind | cells of 3 to 6 cm |
| green | soft CLOUDS | 0.4 to 0.8 m |
| blue | a fine GRAIN | a texel or two |

**Tiling and scale**: the tracer's reading (the texture every 1 / tiling metres) fits the game's picture: the lace
in the game's foreground has cells of 10 to 30 cm, the coarse web's. Not the fault.

**How much foam** is the fault. Measured in the kept frames of one frozen sea (run `20261008-145509`, `water-day`,
4 game frames, 16 traced frames with no denoiser, the open sea's box of 1824 x 499 pixels;
`water7-work\foam_frames.py`, `foam_squeeze.py`):
- the game has foam on 1.1 to 2.7 % of the open sea (by how bright "foam" must be);
- the traced build of that run (water 6's) had it on 46 %;
- the game's foam sits where the tracer's was densest: 5 to 6.5 % foam in the more squeezed tenth of the sea, 1 % in
  the less squeezed half.
So the tracer read the right signal (the squeeze) and made foam of far too little of it.

**Why.** The foam's cover was `saturate(threshold - sum of the Jacobians) x intensity`. For the sea the surface's
amount is 1, so HDRP's threshold is 12.233, and water at rest sums to 12: by that rule every piece of water that is
not being stretched has foam (0.126 at rest, 0.54 where it is squeezed by one unit). Turned back from the traced
frames, the sea's sum lies between 11.6 and 12.2 for nine tenths of its blocks: more than half the sea is "past the
threshold". The game shows foam only at the squeezed end of that. What HDRP's own shader does between that number and
its picture I do not know and was told not to look up; our rule is below.

**Carried over time?** I found no sign that the simulation's foam is carried over time, and did not look in Unity's
source. What IS carried over time is HDRP's foam buffer for foam generators and water decals (the shore's foam, a
boat's wake): the tracer does not have that buffer (water 4). So: **foam at the shore is still missing**, for the
same reason as before. The way to get it is the buffer itself, handed over by the plugin.

**The logs could not tell** what the simulation really writes where the tracer reads the squeeze. Now the plugin logs
it once a surface (`LogSqueeze`: each band's lowest, 1 %, median, 99 %, highest; the bands' sum over 20,000 points;
how much of the sea HDRP's measure puts over 0, over 0.45 and over 1). The next game run has it.

### Our own foam (`pt_water_foam.hlsli`; the port of Unity's foam erosion is gone)

Derived from two things, neither of them shader code: what the texture's channels hold (above), and how foam behaves.
Foam is made where a wave breaks, as a dense white mat. Its bubbles burst from the middle of each cell outwards; the
borders between cells are thickest and hold out longest. So an ageing mat opens into a web, the web thins to threads,
the threads break up, patchily, and are gone.

- **Cover** (how much of the surface is foam, 0 to 1): none until the squeeze is 45 % of the way from HDRP's threshold
  to HDRP's full foam, then rising to the surface's intensity where HDRP's foam is full. Times the surface's mask.
  **The 45 % (`FOAM_ONSET`) is the one fitted number, fitted to one scene** (the game's 1 to 3 % above). Water at rest
  has no foam at any amount.
- **Which part of the surface**: the places that hold out longest. Each channel is turned into its share (how much
  of the texture lies under the texel's value). A point is foam where the coarse web is in its top cover^0.6, the fine
  web in its top cover^0.35 and the grain in its top cover^0.05. The three are unrelated, so together they keep
  exactly `cover` of the surface: the cover means what it says.
- **Patchy ageing**: the clouds make the cover a little higher here and lower there, the same on average.
- **It rides the water**: laid over the undisplaced surface (it moves with the waves), drifting with the current at
  0.3 m/s times the surface's current influence (a wind-blown surface moves at about 3 % of the wind). One read of
  the texture; the two half-offset reads of the old code are gone.
- The shares' nine numbers a channel are measured from the game's `FoamMask`. A surface with another foam texture
  would get somewhat more or less foam than its cover says.

Worked on the CPU on the game's texture (`water7-work\foam_look.py`, pictures `water7-work\foam\variant_b.png`):

| Cover asked | Foam on the surface | What it looks like | A foam texel lies in a piece this wide |
|---|---|---|---|
| 0.05 | 0.043 | threads along the coarse web's thickest borders | 1.3 cm |
| 0.10 | 0.093 | a thin lace | 1.6 cm |
| 0.20 | 0.178 | lace, cells of 20 to 50 cm open | 1.9 cm |
| 0.35 | 0.337 | a web with dark cells | 3.2 cm |
| 0.8 | 0.81 | a mat with holes | - |

On the fixture's sea with the game's numbers (`foam_cpu7.py`): HDRP's measure is over 0 at 86 % of the sea within
60 m, over 0.45 at 17 %, never over 1; the cover asked is 0.019 on average and the foam covers 0.018 with the game's
texture. The game's own sea: 1 to 3 %.

## 4b. Step 3 on the GPU (the fixture's parts 1 to 4; run 19:16, `water7-test-sea23a.log`; pictures `%TEMP%\tgb\water7-images-sea23a\water-sea-as-sent*.png`)

The fixture's sea now has the game's foam numbers (amount 1, threshold 12.233, intensity 0.54, tiling 0.25) and a
foam texture made as the game's is (a coarse web, a fine web, clouds, grain, with the game's spread of values).

| The sea | Foam covers | Pixels with any | Foam pixels with open water two pixels away | The CPU's number (`foam_cpu7.py`) |
|---|---|---|---|---|
| as sent (amount 1) | 0.0172 | 1.9 % | 100 % | 0.021 |
| amount 0.7 | 0.0004 | 0.0 % | 100 % | 0.001 |
| amount 0 | 0 | 0 | - | 0 |
| intensity 0 | 0 | 0 | - | 0 |

(The CPU counts points spread evenly over the sea 6 to 60 m out, the GPU the picture's pixels, which see more of the
near water: near, not equal.) In the picture: thin white flecks in lines along the crests, clear water between.
All of the foam's checks pass, the new one too ("thin threads and lace, not clouds").
This run had the three shares at 0.5 / 0.3 / 0.2; the copy has 0.6 / 0.35 / 0.05 (the grain cut every thread into
specks of a few millimetres on the game's texture: section 4's table is of the new numbers).
**The last run (19:30), with the new shares: foam covers 0.0164 as sent (1.8 % of the pixels; 100 % thin), 0.0004 at an
amount of 0.7, 0 at an amount of 0 and at an intensity of 0. All of the foam's checks pass.**

Also from that run, parts 5 and 6 of water 6's (never run before): the caustics stage 20.1 ms with the sea open to the
sun and four lamps, 8.7 ms with the same sea under ground (640 x 360); a lamp inside its own opaque emitter throws
0.994 of the bare lamp's light through the water (its check passes).

## 5. For the coordinator's game run (`water-day` with `quick`), step by step

The three steps are in one copy and build together. To look at one step alone in the game, these switches in the
shader source turn the others back (each a one-line edit, for a look only):
- step 2 off: in `pt_water_surface.hlsli` let `water_smoothness` return 1.0;
- step 3 off: there is no old foam to go back to (the port is deleted); `FOAM_ONSET = 0` gives water 6's amount with
  the new look.

| Step | What `water-day` should show | How to count it |
|---|---|---|
| 1 | The sea's body dark GREEN where it was near-black brown. With no denoiser: no single green dots. With NRD: no orbs. The sea darker than the sand (the game: 0.4 to 0.7 of it). | (a) the open sea's box (display x 1000 to 1950, y 300 to 560 of 2000 x 1125) mean R, G, B against the game's 40.6 / 48.4 / 33.3 of 255: green over red and blue; (b) hot pixels: a pixel 60 levels over what it is in the other frames, over the 16 kept frames, as the orbs were counted (the game 0, before 0 to 350 with none and 40 to 2,650 with NRD): expect under 5 a frame in both; (c) the status line's "water reflections" stage: it will be HIGHER (the patches' cost, section 2c): write the number down. |
| 2 | The sun's glints on the near sea small and soft, not single burnt pixels; towards the horizon (past 100 m the game's sea loses its smoothness) no mirror image of the sky's bright parts, a matt sheen as in the game. | the far sea's strip (the top 1.5 degrees under the horizon): its 16 frames' largest pixel over its mean, before and after; the log: `Water: "Ocean" numbers: ... smoothness 0.947 near, 0 far (...: the tracer's water has it)`. |
| 3 | Thin white threads and lace on the crests; clear water between; no clouds of foam. About as much foam as the game: 1 to 3 % of the open sea. STILL MISSING: the foam at the shoreline (HDRP's foam buffer, section 4). | `water7-work\foam_frames.py <run>\water-day out.jpg`: "the game: foam on ... %" against "traced: cover over the box" (was 0.463); the new log line `Water: "Ocean" squeeze ...`: its last numbers say how much of the sea is past 0, 0.45 and 1. If the traced foam is far from the game's 1 to 3 %, `FOAM_ONSET` is the number to move, and that log line says to where. |

What the picture will still not have after all three: foam at the shore; lamps lighting the water's body (by chance
only); caustics outside 1.5 times the camera's view; a frame rate.

## 6. The speed job that should follow (not built; "speed comes after, without changing the result")

Measured: 24 to 35 ms a frame for 230,000 pixels of deep water (640 x 360), so about 100 ms more at the game's size
with the sea on 40 % of the screen. All of it is step 1's patches: 40 to 60 corners a scatter point, 15 texture reads
a corner (three bands: one read of the move, four of the gradient, HDRP's bicubic).

The cure keeps the result and makes a corner two reads:
1. **A buffer of corners, made once a frame** (a new small pass before the path stages): for each water slot, a few
   levels of a grid over the surface round the camera (256 x 256 corners a level; the finest 7.8 cm, each next twice
   as coarse: 20 m, 40 m, ... 1.3 km across). A corner holds the wave's point (3 floats) and its normal (2 floats).
   7 levels x 4 slots x 65,000 corners x 24 bytes = 44 MB; filling it is 27 million texture reads a frame (1 to 2 ms).
   The grid moves with the camera in whole cells (the same corner keeps its place) and is shifted by a random part
   of a cell each frame, as the caustics' grid is.
2. **`water_sun` takes its patch from that grid** where the cell it wants is a level's cell or coarser (a scatter
   point deeper than about 15 cm; shallower ones keep today's way: their patch is a few corners). The patch is then
   a block of the grid's own cells round the middle. Everything else stays: the plane, the landed triangles, the
   sun's disc, the growth, the shadow rays. The fixture's numbers (closed form, waves against still, 16 against 256
   frames, the largest sample) are the check that the result did not change.
3. The corner's foam and the sun's shadow could be kept in the same corner later (one more float each): then the
   chosen cell's two extra reads and its ray to the sun go too.
Plumbing, by the wet ground's pass as the pattern (`tracer.cpp`: `wetStore`, `wetPass`, `k.wet`): a buffer and its
view, a pass in `TGPT_PASS` and CMake's list, a dispatch before `prepare`, the view's number in `waterGeometry`'s two
spare words. No ABI change.
With corners at two reads the budget (`SUN_BUDGET`) can go from 48 to several hundred: deep water loses its extra
noise as well.

## 7. What changed (all in the copy)

| File | Change |
|---|---|
| `native/pathtracing/shaders/pt_water_sun.hlsli` | NEW. Step 1: the sun at a point inside the water, through the waves (`water_sun`, `water_above`, `disc_triangle`, `sun_rays`). Step 2: each corner's ray bent at a facet tilted by the surface's smoothness |
| `native/pathtracing/shaders/pt_water_path.hlsli` | step 1: the sun asked for at a scatter point; who has asked (`asked`, `crossings`, `crossed`); the chance hit of the disc only where nobody asked. Step 2: a facet of the water's lobe mirrors or lets through; the sun's glint asked for through the lobe (`glinted`) |
| `native/pathtracing/shaders/pt_water_surface.hlsli` | step 2: `water_smoothness` |
| `native/pathtracing/shaders/pt_water_foam.hlsli` | step 3: rewritten from its cover on (`FOAM_ONSET`, `foam_shares`, one read of the texture); the port of Unity's foam erosion and its two flowing reads are gone |
| `native/pathtracing/include/tgpt_api.h` | step 2: `TGPT_WATER_SMOOTHNESS`; `scattering[3]`, `foamColor[3]`, `smoothnessFade[2]` (was `foamMaps[2..3]`); the struct's size is 288 as before |
| `native/pathtracing/src/tracer.cpp` | step 2: the smoothness into the frame's constants (11 lines where the foam's numbers are copied) |
| `CMakeLists.txt` | the new shader file in the passes' list of what they depend on |
| `src/TGPathTracing/NativeTracer.cs`, `WaterCaustics.cs` | step 2: the surface's smoothness sent; step 3: `LogSqueeze` (once a surface: what the simulation writes where the foam reads the squeeze) |
| `tests/water_sea_gpu.inl` | the game's own numbers (scattering, absorption, smoothness, foam); `TGPT_TEST_SEA_PART` (one part alone: foam, cost, lamp, body, glint); part 7 (the sun lights the body), part 8 (the glint); the foam's new checks and a foam texture made as the game's is |
| `tests/source_checks.py` | the water's check names all of the above (95 of 95 pass) |

No struct size, export, option bit or ABI number changed. No constant buffer's layout changed (the smoothness rides in
`waterScatter`'s w, the foam colour's w and `waterFoamMaps`' z and w, all spare until now).
Built: `TGPathTracing.Native.dll` and `tgpt_gpu_test.exe` (Ninja, NRD off), staged in `%TEMP%\tgb\water7-stage3` (the
last build); the untouched tracer with the same test program in `%TEMP%\tgb\water7-stage-base`. The Mono plugin
compiles: 0 warnings, 0 errors (`-p:DeployToGame=false`, into the copy's `bin`). NRD's path was not built here
(`-DTGPT_WITH_NRD=OFF`): the coordinator's merged build tests it.

## 8. Workarounds and stand-ins met (AGENTS.md: listed, removed or not)

| What | Where | Removed? |
|---|---|---|
| The sun reaches the water's body only by a chance hit of its disc | `pt_water_path.hlsli` | YES: asked for at every scatter point; the chance hit stays only for what nobody asks |
| The bed's sun counted twice (the caustics' landed light, and a path leaving the bed through the surface at the disc) | `pt_water_path.hlsli` | YES (`asked` covers surfaces under water too) |
| The water's surface a clear mirror facet whatever the game's smoothness says | `pt_water_path.hlsli` | YES |
| A port of Unity's foam erosion | `pt_water_foam.hlsli` | YES: our own, from the texture's channels and how foam behaves |
| "Foam wherever the squeeze is past HDRP's threshold" (half the sea at the game's amount of 1) | `pt_water_foam.hlsli` | REPLACED by an onset fitted to ONE scene of the game (`FOAM_ONSET`): a number to settle with the new log line, not a known fact |
| The shares' curves are the game's default foam texture's | `pt_water_foam.hlsli FOAM_VALUES` | NEW, mine: another foam texture gets a somewhat different amount. The clean way: the plugin measures each foam texture once and sends its curves |
| A patch lies on the level plane round its middle | `pt_water_sun.hlsli` | NEW, mine: exact for the sea, lakes and pools; an approximation over a river's bent level mesh (a metre or two) |
| A patch of more than 48 corners is made by chance | `pt_water_sun.hlsli SUN_BUDGET` | NEW, mine: a budget that costs noise, not light; goes with the speed job (section 6) |
| Foam at the shore (HDRP's foam buffer) is not read | plugin, API | NO (water 4's finding; needs the buffer handed over) |
| Lamps light the water's body by chance only | `pt_water_path.hlsli` | NO: named in section 2 |
| A lamp's glint on far rough water by chance only | `pt_water_path.hlsli` | NO: named in section 3 |
| Caustics kept only in 1.5 times the camera's view | `pt_caustics.hlsl in_view` | NO (water 6, section 9: memory, the owner's word) |
| The composite caps a pixel at 256 | `pt_compose.hlsl rr_clamp` | NO: not mine; found by the glint check (section 3b) |
| The denoisers' guide says roughness 0 for water | `pt_primary.hlsli` | NO: the brief keeps me off what the filters see; the truthful value is 1 less the smoothness |
| The Caustics row's "off" as straight light | `pt_water_surface.hlsli` | NO (the row's meaning; the body follows it now) |

## 9. Tests: what ran, and how to run them again

From PowerShell, the game closed. `$w` is `<session scratch>\water7-work`. `run.ps1` waits for the main tree's
`Run-PtHarness.ps1 -GpuCheck` itself (one waiting shell, it gives up after 45 minutes), sets
`TGPT_TEST_STARTUP_SECONDS=900`, writes `%TEMP%\tgb\water7-test-<Name>.log` and pictures to
`%TEMP%\tgb\water7-images-<Name>`. A new DLL's first start makes its passes for 3 to 5 minutes; the fixture itself
takes under a minute.

| Run | Build | Result |
|---|---|---|
| `body1` (18:44), part 7 alone | step 1, budget 96 | the body's numbers of section 2c; its closed-form line was the fixture's own mistake |
| `body1-base` (18:47), part 7 alone | the untouched tracer, the same test program (`water7-stage-base`) | the sun's share 0.000; the largest sample 2,263 times the mean: the dots check FAILS there, as it should |
| `body2` (19:05), part 7 alone | step 1, budget 48 | section 2c |
| `sea23a` (19:16), the whole sea fixture | steps 1 to 3 | all pass but the glint's three (the composite's cap of 256: section 3b) |
| `surface23a` (19:20), the water's own fixture | steps 1 to 3 | `PASS water surface checks`, 97 ok |
| **`sea23b` (19:30), the whole sea fixture** | **the last build** (`water7-stage3`, DLL sha256 34C0131A...) | **`PASS sea checks`** |
| **`surface23b`, the water's own fixture** | **the last build** | **`PASS water surface checks`, 97 ok** |


Again, by hand:
- the whole sea fixture: `& "$w\run.ps1" -Name sea -StageName stage3 -Grep 'SEA|FAIL|PASS'`
- one part: add `-Env @{TGPT_TEST_SEA_PART='body'}` (or `glint`, `foam`, `cost`, `lamp`)
- the water's own fixture: `& "$w\run.ps1" -Name surface -StageName stage3 -Surface -Grep 'FAIL|PASS'`
- without the scripts: `$env:TGPT_TEST_WATER_SURFACE='1'; $env:TGPT_TEST_WATER_SEA='1'; $env:TGPT_TEST_STARTUP_SECONDS='900';
  & "$env:TEMP\tgb\water7-stage3\tgpt_gpu_test.exe" "$env:TEMP\tgb\water7-stage3" "$env:TEMP\tgb\water7-images-sea"`
- the CPU's side: `python $w\sun_cpu.py 2.0 1.0` (step 1 against a count of rays), `python $w\foam_cpu7.py` (the foam over
  the fixture's sea), `python $w\foam_frames.py <run>\water-day out.jpg` (the game's foam against the traced in kept
  frames). `find_foam.py` reads the game's foam texture out of the game's data again (its copies in the scratch folder
  are deleted; nothing of it was ever in the tree).

NOT run, and whose it is: the whole GPU test (the brief: never), the NRD build (the copy builds with NRD off), mutation
checks (the rule: not until the owner says a thing looks right; the untouched tracer's run stands in for one for the
dots), anything in the game.
