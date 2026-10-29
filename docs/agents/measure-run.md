# The measuring run in the game (PT-99), 2026-10-09 19:37 to 19:56

Run `artifacts/harness/20261009-193722`: the measuring build (CMake `TGPT_SPEED_MEASURE` ON, built from the main
tree as it stood at 19:16, in `%TEMP%\tgb\measure`), recipe `measure`, scenes `tavern-room` and `water-day`. The card
free, 1924 x 1084 rays for 4K, one a pixel, no denoiser, DirectX Raytracing 1.2 with Shader Execution Reordering.
Each step leaves ONE part of the work out (the switches: `docs/agents/speed-1.md` section 3). Read by
`<scratch>\measure_read.py`: the tracer's own 10-second lines whose whole window lies inside a step (one or two a
step), their median. The normal build went back into the game folder at 19:56 (native 01130141AB47, hash-checked).

## THE TWO HEADLINES

1. **At the water, the water's own path is the frame.** With the walk through glass and the water's own path left
   out (step 13), the beach's frame falls from 163 to 181 ms to **29.9 ms**: the reflections' pass from 112 to 119
   ms to 2.8, the bounced light's from 35 to 45 ms to 11.6. The pass NAMED caustics is 7 ms: the cost is not there.
   It is in `pt_specular` and `pt_gi`, in what a ray does once it meets water (`water_path`, `dielectric_follow`,
   `shade_path`'s water part, and what the Caustics row switches inside them: with that row Off a frame was 58
   ms on 10-09 morning). The reflections' pass stays at 103 ms even with its own ray left out (step 3): it is the
   water the CAMERA sees that costs, not the reflection ray.
2. **In the tavern, 31 ms are spent with no ray at all in the two big passes, and the bounce ray alone costs 28
   ms**, 11 times what the camera's own ray costs (`prepare`, 2.5 ms) over the same world.

## The beach (`water-day`): 2.72 M triangles, 10,275 instances, 2,555 plants in the wind, 2,459 pieces of sea

| Step | lines | frames in 10 s | a frame's GPU time, ms | bounced light | reflections | direct light | caustics | prepare | scene |
|---|---|---|---|---|---|---|---|---|---|
| 2-everything | 2 | 51 | 181.1 | 45.5 | 119.1 | 2.09 | 7.17 | 2.34 | 1.64 |
| 3-without-pass-ray (16) | 2 | 71 | 123.5 | 4.0 | 102.9 | 2.04 | 7.40 | 2.36 | 1.63 |
| 4-without-shading (32) | 1 | 216 | 29.9 | 11.2 | 2.5 | 2.04 | 7.09 | 2.34 | 1.66 |
| 5-without-materials (64) | 2 | 49 | 189.3 | 41.6 | 132.1 | 2.10 | 7.08 | 2.07 | 1.63 |
| 6-without-decals (128) | 2 | 48 | 193.5 | 49.2 | 127.4 | 2.34 | 7.48 | 2.33 | 1.67 |
| 7-without-sun (256) | 1 | 46 | 203.4 | 49.5 | 136.7 | 2.39 | 7.52 | 2.35 | 1.66 |
| 8-without-lamps (512) | 2 | 54 | 170.8 | 34.5 | 120.1 | 2.15 | 7.00 | 2.30 | 1.62 |
| 9-without-lamp-shadow-rays (1024) | 2 | 53 | 174.8 | 35.6 | 122.6 | 2.10 | 7.07 | 2.30 | 1.65 |
| 10-without-cache-feed (2048) | 2 | 52 | 172.5 | 35.1 | 121.2 | 2.10 | 7.00 | 2.31 | 1.63 |
| 11-without-cache-reads (4096) | 1 | 53 | 175.6 | 35.4 | 123.8 | 2.25 | 7.00 | 2.29 | 1.66 |
| 12-without-water-question (8192) | 2 | 55 | 169.7 | 34.7 | 118.8 | 2.08 | 7.04 | 2.25 | 1.62 |
| 13-without-glass-and-water-walk (16384) | 2 | 218 | 29.9 | 11.6 | 2.8 | 1.99 | 6.87 | 2.31 | 1.61 |
| 14-without-reordering | 1 | 54 | 170.2 | 34.7 | 119.5 | 2.07 | 6.98 | 2.31 | 1.62 |
| 15-everything-again | 2 | 56 | 163.2 | 34.8 | 112.3 | 2.05 | 7.00 | 2.31 | 1.62 |

- "Everything" drifted from 181 to 163 ms within the run (the bounced light's pass from 45.5 to 34.8 over the first
  two minutes, then steady at 34.5 to 35.6 from step 8 on; the reflections' pass wanders between 112 and 137).
  Against "everything again": the lamps, their shadow rays, the cache's feed and reads, the water question and the
  reordering change NOTHING that can be told from the noise here (about 10 ms in the reflections' pass).
- Steps 4 and 13 read the same (29.9): at this camera everything shaded beyond the ray itself is the water's path.
- The bounce ray alone here: 11.2 less 4.0 = 7 ms (in the tavern 28). The bounced light's pass with no ray: 4.0
  ms (in the tavern 14.4). Much of this screen is sea and sky, so the two scenes cannot be compared pixel for pixel.
- A beach frame with the whole traced world in it and no water path is 30 ms.

## The tavern (`tavern-room`): 1.53 M triangles, 5,331 instances, 514 lamps, no plants, 762 pieces of sea

| Step | lines | frames in 10 s | a frame's GPU time, ms | bounced light | reflections | direct light | caustics | prepare | scene |
|---|---|---|---|---|---|---|---|---|---|
| 2-everything | 2 | 105 | 81.3 | 55.3 | 12.1 | 4.67 | 3.95 | 2.48 | 0.99 |
| 3-without-pass-ray (16) | 2 | 213 | 31.1 | 14.4 | 2.5 | 4.88 | 4.00 | 2.48 | 0.98 |
| 4-without-shading (32) | 1 | 132 | 59.5 | 42.3 | 3.3 | 4.77 | 4.07 | 2.53 | 0.99 |
| 5-without-materials (64) | 2 | 89 | 98.1 | 62.1 | 12.0 | 15.46 | 3.95 | 1.79 | 0.97 |
| 6-without-decals (128) | 1 | 111 | 76.3 | 52.9 | 9.8 | 4.67 | 4.02 | 2.07 | 0.98 |
| 7-without-sun (256) | 1 | 106 | 80.8 | 55.0 | 12.0 | 4.66 | 3.93 | 2.47 | 0.98 |
| 8-without-lamps (512) | 2 | 106 | 80.0 | 56.9 | 9.0 | 4.65 | 3.98 | 2.47 | 0.97 |
| 9-without-lamp-shadow-rays (1024) | 1 | 103 | 83.6 | 57.6 | 11.9 | 4.65 | 4.00 | 2.53 | 0.98 |
| 10-without-cache-feed (2048) | 1 | 110 | 77.5 | 55.5 | 8.0 | 4.67 | 3.97 | 2.54 | 0.99 |
| 11-without-cache-reads (4096) | 2 | 98 | 87.9 | 60.7 | 13.1 | 4.68 | 3.98 | 2.47 | 0.98 |
| 12-without-water-question (8192) | 1 | 98 | 87.8 | 60.7 | 13.1 | 4.62 | 4.04 | 2.45 | 0.99 |
| 13-without-glass-and-water-walk (16384) | 1 | 103 | 83.1 | 57.6 | 11.4 | 4.76 | 3.99 | 2.47 | 0.97 |
| 14-without-reordering | 2 | 98 | 87.7 | 60.6 | 13.1 | 4.69 | 3.95 | 2.49 | 0.97 |
| 15-everything-again | 1 | 103 | 84.7 | 57.9 | 12.8 | 4.68 | 3.97 | 2.49 | 0.97 |

### What it says for certain

1. **With no ray in the two big passes a frame is still 31 ms** (step 3): the bounced light's pass 14.4 ms with no
   ray in it, the reflections' 2.5, direct light 4.9, caustics 4.0 (IN A TAVERN: "0 triangles a frame"), prepare
   2.5, scene 1.0. That floor alone is 32 frames a second at best. WHAT DOES THE BOUNCED LIGHT'S PASS DO FOR 14 ms
   WITHOUT A RAY is the first thing to find out there (at the beach the same pass with no ray is 4 ms).
2. **The ray alone, with nothing shaded where it lands, costs 28 ms in the bounced light's pass** (step 4 less step
   3: 42.3 - 14.4) and 0.8 ms in the reflections'. `prepare` (the camera's own rays, one a pixel over the same
   world) is 2.5 ms. So a bounce ray costs about 11 times a camera ray, in a ROOM where a ray ends at a wall after
   a few metres. That is not distance. Suspects, none measured: the pass's "own ray" is a walk of several
   segments, not one ray; its rays meet the 49 instances whose every triangle is tested by a shader (547,286
   triangles); the top-level structure (5,331 instances, 762 pieces of sea, 59 characters) is poor for rays that
   fly every way.
3. **What the ray meets being shaded costs about 22 ms** (step 2 less step 4): 13.0 in the bounced light's pass,
   8.8 in the reflections'.
4. Decals: about 5 ms (step 6: 254 projectors in the scene, 25 captured). The cache's feed: 4 ms of the
   reflections' pass (step 10).

### What it does NOT say

- "Everything" read 81.3 at the start and 84.7 at the end; steps 11, 12 and 14 read the same to 0.1 ms (87.8).
  So differences under about 7 ms (the sun, the lamps, their shadow rays, the water question, the glass and water
  walk, the cache's reads, the reordering) are inside this run's noise in the tavern.
- Step 5 (without materials) is SLOWER (98 ms; direct light 15.5 ms instead of 4.7): without textures the scene
  is another scene to the light's sampling. Its number says nothing about what the textures cost.

## The owner's question (19:22): "isn't [the world without a distance limit] the primary reason for our perf woes?"

What this run answers:
- At the beach, where the traced world is largest (10,275 instances, the plants, the sea), a frame is 30 ms once
  the water's path is left out. So the world's size is not what makes the water 1 frame a second: the water's own
  path is 133 to 151 of the frame's 163 to 181 ms.
- In the tavern the ray's own cost is 28 of 81 ms and it is that high in a closed room, so a limit on how far a
  ray flies would not bring it down.
- NOT answered: whether the SIZE of the traced world makes each ray slower than it need be (the outdoor map stays
  loaded and traced while the player is in the room). That needs one run with the far world left out of the
  top-level structure, as a measurement only; and the town (`metal`: 16,234 instances, 6.7 M plant triangles
  posed a frame), which this run did not take.
