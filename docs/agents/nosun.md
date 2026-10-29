# nosun: does any other pass return garbage in a frame without a sun? (PT-133)

Worker: Claude subagent, copy `%TEMP%\tg-agents\wt\nosun`, base `base20h`, brief `docs/agents/briefs/brief-nosun.md`.
2026-10-12 01:15 to 03:05. Build folder `%TEMP%\tgb\nosun-build` (its scripts: `configure.cmd`, `build.cmd`,
`build-all.ps1`, `run-test.ps1`, `run-matrix.ps1`, `survey-base.ps1`, `survey.ps1`, `wait-any.ps1`), the base build kept
in `%TEMP%\tgb\nosun-base`, stage `%TEMP%\tgb\nosun-stage`, the test's own folder `%TEMP%\tgb\nosun-run`, every run's
log `%TEMP%\tgb\nosun-out\<tag>`, temporary files `%TEMP%\tgb\nosun-tmp`.

## State when stopped (03:05)

**The question of the brief is NOT answered: no frame was traced.** What is done is the way to answer it, and a
reading of the reflections' loops that found four without an end.

- NOT RUN, and why. The test's first start has to make the two reordering passes once. That is the driver's own
  cache, which no file of mine can keep, and it takes longer than any gap the card had: the longest try ran 263 s
  and they were not made (the game itself waited 600 s for them at 01:30). Ten starts between 01:27 and 02:57,
  each stopped within 2 s when a game opened (Starfield once, at 02:01; the others ours) or a run of the game began
  to wait for the card; they ran 9 to 263 s. A stopped start loses that work. The other passes of the 6.9 set are
  made and kept (213 files in `nosun-run\nosun-runtime\TGPathTracing\ShaderCache`).
- WHAT IT NEEDS: one gap of about ten minutes with no game open and no run of the game waiting. After it a run
  takes one to three minutes; the whole survey is about 45 minutes of the card. Then:
  `powershell -File %TEMP%\tgb\nosun-build\survey-base.ps1` (in the background; it waits behind the main tree's
  `-GpuCheck` before every start, stops its test when a game opens or a run waits, starts that run again later, and
  writes each verdict into `nosun-out\survey-base.log`; `nosun-build\STOP` ends it). `survey.ps1 -Set base2` has the
  rest of the modes, `-Set mutant` the mutation check below.
- STOPPED BY ME, nothing left running: the queued survey, and the compile of the guarded build (31 of its 57 passes
  were made, all three builds of the reflections' pass and of the bounced light's among them; the other 26 waited
  for memory behind the game and two other workers' compiles, and a merge compiles all 57 in the main tree anyway).
  So there is NO native DLL of the guarded sources. The base build's DLL (main's passes of 01:20 with the tracer's
  new switch) is `%TEMP%\tgb\nosun-base\TGPathTracing.Native.dll`, SHA-256
  `D92EFE3936839B376D4C80E85260A2A259F7DFD4FA978B0BD067E86CE390D9C5`.
- Checked: `python tests/source_checks.py` (117, OK), `python tests/nosun_compare_test.py` (OK), the test exe builds
  with the new mode, `pt_specular` (three builds) and `pt_gi` (three) compile with the guards (`-WX`).
- Never started the game, deployed, committed, or wrote to the main tree or the game folder. I read the main tree's
  harness folder and the game's log (the fog boxes' line).

## What was built: how a fault like the fog cells' is seen offline

1. `TGPT_TEST_SM66=1` (the tracer, read once when the device is ready): the Shader Model 6.6 set of passes on a card
   that offers 6.9. The same runtime, the same driver; no micromaps, no reordering. The tracer's log says so.
2. `TGPT_TEST_NOSUN=1` or `2` (the test, any mode): every frame of the run without a sun. 1: `TGPT_Frame.sunColor`'s
   w 0 alone (as the test's own checks leave the sun out). 2: as the plugin sends an interior or a night: the sun's
   direction, angular radius and colour 0 too (`HdrpPasses.cs` fills them only `if (sun.Valid)`; the test's own
   "no sun" never sent that). A mode's own checks then fail where they count on the sun: such a run is for its numbers.
3. `TGPT_TEST_TRACE=<file>` (the test, any mode): after every frame the composite is read back and one line is
   written: the frame's number, the checks done before it, the view, the frame's index, options and flags, the sun's
   w, the camera's place; the mean of each channel, the largest and smallest value, how many values are not finite,
   how many pixels are above 1,000 and above 30,000, how many below -0.001; the mean of each block of a 16 x 9 grid.
4. `python tools/agents/nosun_compare.py <6.6 trace> <6.9 trace>`: the two runs frame by frame (they trace the same
   frames with the same random numbers): values not finite or above the bounds in one set only, and windows of 16
   frames whose mean, or a block's mean, parts by more than 2 % (10 % a block) AND five standard errors of the
   frames' paired differences. Exit code 1 where something parts, 2 where the runs traced other frames. Its own
   check, on made-up traces (noise alone passes; a block gone bright without a sun, values not finite, values no
   light reaches and other frames do not): `python tests/nosun_compare_test.py`.
5. `TGPT_TEST_NOSUN_ONLY=1`, THE MODE LEFT IN THE TEST (`tests/pathtracing_nosun_checks.inl`, in the speed study's
   block; some minutes once the passes are made; the whole test does not take it). A room closed all round, so the
   sun lights nothing in it whether its code runs or not: walls of HDRP Lit's three maps, boxes, a clear glass cube,
   a wax cube, a smooth metal cube, a hair card, thin cloth, a glowing panel, a pure mirror on the back wall, eight
   lamps. The tracer's own camera rays, the game's defaults. Each arrangement four times from a reset, the mean of
   16 frames after 32: with the sun; without (w 0); without again with other random numbers (the noise); without as
   the plugin sends it. Arrangements: no denoiser; NRD; traced fog with seven fog boxes by the old route; the same
   by the grid's cells. Judged: nothing not finite, nothing above 30,000, and the picture with the sun and the one
   "as the plugin sends it" against "without", whole and block by block, within three times the noise (and 2 % of a
   block, 1 % of the picture). It needs no second set of passes: the sun's own absence is the comparison. With
   `TGPT_TEST_SM66=1` it judges the 6.6 passes; with `TGPT_TEST_TRACE` in both runs the two sets side by side.
   **IT HAS NEVER RUN.** Its tolerances are a first guess; the first run's line "parted from ..." shows the noise.
   Its mutation check is ready and not run: `survey.ps1 -Set mutant` runs it (and the fog check without a sun,
   6.9 against 6.6) with the build that was white in the game (fogfix's of 23:00, `%TEMP%\tgb\fogfix-handover`): the
   arrangement with the grid's cells must FAIL. (That build has no `TGPT_TEST_SM66`; its 6.6 passes run by
   `TGPT_TEST_NO_AGILITY=1`.)

By hand, in any tree that has these files: `set TGPT_TEST_NOSUN_ONLY=1` and run `tgpt_gpu_test <stage> <pictures>`;
again with `TGPT_TEST_SM66=1`; any other small mode with `TGPT_TEST_NOSUN=2` and `TGPT_TEST_TRACE=<file>`, under
both sets, then the comparison.

### What the survey would cover, and what nothing covers

- `survey-base.ps1`, each under 6.9 and 6.6, with the sun as the mode sets it and with `TGPT_TEST_NOSUN=2`: the mode
  above; the speed study's ten closed rooms (plain, HDRP Lit, detail, architecture, terrain, triplanar, glass, wax,
  a snow floor: no sun by themselves); the fog check (old route and grid, its own no-sun cases) and again with fog
  boxes and the mist's cache; the walks; glass; the water's fixture, the sea, what the caustics land; the mirror
  under NRD; the reuse check; decals. `survey.ps1 -Set base2`: the speed-1 town with its sea, plants, decals and 510
  lamps (its own header says it was never run), water10, moving things, the candle, skin's material, the edges,
  micromaps, the sky.
- By pass: prepare, probe, gi (its reordering build), giReuse, giSpatial, di, diSpatial, specular (its reordering
  build), temporal, atrous, compose, walk, fog and nrdConfidence are in the mode; caustics and wet only in the
  water modes; the fog grid's four in the mode's last arrangement and in the fog check.
- In NO small mode: hair beyond one strands card (the hair layer, eyebrows, lashes), characters (skinned meshes),
  the cliff, grass, leaves and trunk shaders (the whole test's, at its line 8030), Ray Reconstruction's guides, the
  6.9 compute twins of the two reordering passes (cards whose driver does not reorder: `run-matrix.ps1 -Sets 69p`).
  The passes with one 6.6 build for both sets (ommBake, rrgGuides, skinUnpack, giBoil, meter, cache, emissive, sway,
  waterMesh, sunGrid, yardstick, sky, nrdConfidenceFilter, nrdPrep): the two sets cannot differ there.

## PT-129: the reflections' path, loop by loop (read, not run)

What `pt_specular` calls that loops without a fixed count, and what 1e37 and a value that is no number do there.
(HLSL's `min` and `max` hand back the other number when one is no number; `saturate` of no number is 0; a
comparison with no number is false. That is what the DXIL operations are specified to do; not tested here.)

| loop | where | ends by | 1e37 | no number |
|---|---|---|---|---|
| the glass walk, an interface a turn | `pt_dielectric.hlsli` `dielectric_follow` | a roulette of at most 0.95 a turn | survives with 0.95: ends | `min(0.95, NaN)` is 0.95: ends the same; a direction that is no number passes the "wrong side" test, and the next ray meets nothing: ends |
| the water's path | `pt_water_path.hlsli` `water_path` | 0.95 from the third vertex on, and at every scatter point | ends | a surface turn ends at once (`isfinite`); a scatter turn goes on with 0.95: ends |
| the walk inside wax and skin | `pt_common.hlsli` `subsurface_walk` | a roulette by what it carries; from `WALK_LATE` on a half every 128 steps | a flight's start ends it (`sum < 1e30`) | a flight's start ends it |
| a path's bounces | `shade_path` | 4 at most | 4 | `saturate` makes the roulette 0: ends |
| glass on a shadow ray, leaves, thin cloth | `glass_walk`, `leaf_walk`, `cloth_walk` | the crossings the ray's own query counted, each past the last | a count is a count | the query meets nothing: ends |
| the lamps' search tree | `reflected_lamps` | the index only rises, to the tree's size | bounded by the tree | bounded by the tree |
| the decals at a point | `pt_common.hlsli`, the decals' tree | each turn takes a later decal | bounded by the decals | a point that is no number "lies in every box": EVERY decal of the scene is worked out for it (long, not endless) |
| the water's sun patch | `pt_water_sun.hlsli` | 40 turns, a grid of at most `SUN_GRID`, the tiles' counts | bounded | bounded |
| the glass a point is inside of | `pt_dielectric.hlsli` `optical_medium` | the scene: every see-through triangle along an endless ray in one fixed direction, at every interface | a cost, not a hang (the worker campspeed's) | the same |
| **the fog boxes: the light through a box** | `pt_medium.hlsli` `fog_boxes_transmittance` | the point passes the box's far side, or 1e-4 of the light is left | **never, with a bound so large that a step is less than a float holds, where the box's fog is 0 at the point (a faded face)** | **millions of steps: below** |
| **the fog boxes: where a box scatters** | `pt_medium.hlsli` `fog_box_collide` | the point passes the box's far side, or it scatters | **the same** | **the same** |
| **a mirror's record: a direction above the surface** | `pt_psr_capture.hlsli` `psr_finish`, two loops | a draw that points out of the surface | plain values are enough: below | a draw that is no number ends it |

- THE FOG BOXES (traced since 2026-10-11, the day of the hang; the second scene, CampaignMap_HOS, has seven).
  `fog_box_span` works out where a way is inside a box with `min` and `max`, which pass a value that is no number by.
  So a way whose start or direction is no number comes out as inside EVERY box over all of `[0, tmax]`, and `tmax` is
  the game's ray end, 1e6 m (`Tgpt.NoRayEnd`), for a ray that met nothing and for the sun's and the sky's light at a
  fog point. The tracking then takes the box's bound times 1e6 steps, and at a point that is no number the box's own
  fog comes out 0, so "1e-4 of the light is left" never ends it. With that map's boxes as the game's log names them
  (0.033, 0.067, 0.1, 0.299 and three of 1 per metre) one call of `fog_boxes_transmittance` is 3.5 million steps, and
  `traced_fog_on_way` then asks one box where it scatters (up to a million more), for ONE pixel. A plain way through
  the largest of those boxes takes under a hundred.
  - Only the reflections' stage can get there: `fogged` (the traced fog over a ray's own way) is called from
    `pt_specular` and from `shade_seen` alone, and `shade_seen` starts a smooth metal's own reflection ray at
    `hit.position + hit.normal * offset`, a path vertex's normal. Everything else that tracks the boxes (the camera's
    fog, the grid) does it along the camera's own rays, which are finite. The owner's log: "the GPU finished stage
    direct light ... and stopped in stage reflections".
  - How a normal that is no number can come about: `fetch_hit` guards a triangle of no area (`len > 1e-12`), but
    corners that are garbage and large (above about 1e19: the product of two edges passes what a float holds) gave
    `n / len` = inf / inf. A character's skinned corners can be that: `pt_skin.hlsl` turns a corner that is not
    finite into 0 and lets a finite one through however large, and what the game's skinning texture holds for a
    character it has not posed yet (a scene that has just loaded) is whatever was there. THIS STEP IS A GUESS: I did
    not read how the tracer's meshes live through a scene change, and nothing here was run.
- THE MIRROR'S RECORD (`psr_finish`; the rule under NRD since 2026-10-11 21:52, so AFTER the hang of 18:00: it is a
  hang of its own in the making, with plain values). Two loops drew a direction until one came out above the surface,
  with no count. The second draws from the end's own lobe, about the direction its SHADING normal mirrors the view
  to, and asks for one above its TRIANGLE. A smooth, normal-mapped surface met at a low angle (a polished floor seen
  in a wall's mirror) can mirror the view under its own triangle; its lobe is as narrow as an alpha of 1e-4
  (roughness's floor of 0.01), and GGX then gives a facet tilted by 15 degrees once in seven million draws: seconds
  for one pixel. (At a roughness of 0.3 the same tilt comes once in ten draws: it needs the smooth ones.) The first
  loop (a cosine about the shading normal, asked above the triangle) is as slow only where the two normals are
  nearly opposite (it leaves at -0.9999).

### The guards (in the copy's sources; each changes no picture a valid frame can make)

1. `pt_medium.hlsli` `fog_box_span`: a way whose start or direction is not finite is inside no box.
2. `pt_medium.hlsli`: both tracking loops end after `FOG_BOX_STEPS` = 65,536 steps of one box (a plain way: under a
   hundred in the game's boxes).
3. `pt_psr_capture.hlsli` `psr_finish`: both loops draw `PSR_TRIES` = 64 times at most, then leave the direction as
   it is (`psr_lobe_distance` hands the sky's distance for one that points into the surface). A pixel that finds its
   direction within 64 draws draws the same numbers as before. This changes one thing: a pixel that needed MORE than
   64 draws now hands NRD the sky's distance as its hit distance (a guide for the denoiser, no light).
   NOTE FOR THE COORDINATOR: the brief names "mirrors" and NRD as not mine. This is the one edit there, two loops;
   take it or leave it by itself.
4. `pt_common.hlsli` `fetch_hit`: `len < 1e30` beside `len > 1e-12`, so garbage corners give the ray's own direction
   as the normal (what a triangle of no area already gets), not a value that is no number.
- NOT RUN: that they change no picture is by reading (the same frames with the base build and with them was the
  survey's last step: `survey.ps1 -Set guard`, which needs a build of them in `%TEMP%\tgb\nosun-guard`).
- Not guarded, said only: the decals' tree at a point that is no number (every decal of the scene worked out: long,
  not endless); a ray handed to the hardware with a direction that is no number (DirectX leaves what happens open;
  the glass walk can make one); `pt_skin.hlsl` letting a huge finite corner through (a triangle the size of the
  world in the traced scene for that frame: wrong pictures, and with guard 4 no value that is no number).

## Seen on the way (no entry yet; for the coordinator to enter or drop)

- The fault's shape is no rule one can read off the source: the old route's `fog_light` (`pt_medium.hlsli`) and
  `direct_light` (`pt_common.hlsli`, in every big pass) have the very shape that failed in the cells' pass (a sum
  that starts at 0, the sun added under `sunColor.w > 0`, a loop over the lamps after it, the sum used after the
  loop), and fogfix's working shape still carries a three-float value with a zero on the skipped edge across that
  loop (its listing `shapeB.ll`, the value `%5531`; the failing one is `%5523` in `shapeA.ll`: a two-way merge
  where the working one is four-way). So only running tells. For the report to NVIDIA: dxc 1.9.0.5402
  (DXC-1.9.2607), driver 616.92 (32.0.16.1692), RTX 5080, Windows 10.0.26200.9457, Agility SDK 1.619.6.
- The cliff shader's moss (`pt_materials.hlsli`, `sun_forward`): without a sun the "lean toward the sun" is
  normalised about a zero direction, which is no number where the bump is flat too; `saturate` then makes it 0 by
  the specification, and the game's own shader does the same sum. A place to look at if mossy rock ever shows
  specks at night.
- The harness holds a run of the game back while any process named `tgpt_gpu_test` exists, and a worker's test in
  its first minutes only makes passes. A worker whose passes are new to the driver (every pass, after a change in
  `pt_common.hlsli`) cannot get past that start in the gaps between runs of the game. What would let it: the
  coordinator leaving one gap of ten minutes, or the owner's word on whether a start that only makes passes and
  traces nothing may run beside a game (I did not take that on myself).

## What changed (files; against `base20h`)

- `native/pathtracing/src/tracer.cpp`: `TGPT_TEST_SM66` (one line of log when it is set). No export, record,
  FrameConstants row, option or flag bit.
- `native/pathtracing/shaders/pt_medium.hlsli`, `pt_psr_capture.hlsli`, `pt_common.hlsli`: the guards above. New
  constants `FOG_BOX_STEPS` and `PSR_TRIES`. Every pass compiles again (57).
- `tests/pathtracing_gpu_test.cpp`: `TGPT_TEST_NOSUN`, `TGPT_TEST_TRACE` (both in `run`, for any mode), a count of
  the checks done (`checksDone`), `TGPT_TEST_NOSUN_ONLY` (in the speed study's block, whose materials and lamps it
  uses). New: `tests/pathtracing_nosun_checks.inl`, `tests/nosun_compare_test.py`, `tools/agents/nosun_compare.py`.
- `docs/TESTING.md`: a paragraph on the mode and the switches. This report.
