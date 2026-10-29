# fogfix: the fog grid over-counts lamp light near lamps (PT-118)

Worker: Claude subagent, copy `%TEMP%\tg-agents\wt\fogfix`, base `base20e`, brief `docs/agents/briefs/brief-fogfix.md`.
Started 2026-10-11 ~21:25. Builds: `%TEMP%\tgb\fogfix-build` (native DLL and the small test; `configure.cmd`,
`build.cmd`, `run-fog.ps1`, `run-set.ps1`, `strip.py` there), stage `%TEMP%\tgb\fogfix-stage`, the test's own folder
`%TEMP%\tgb\fogfix-run`, every run's log and pictures `%TEMP%\tgb\fogfix-out\<tag>`.

## State (kept current)

- 00:25 (2026-10-12). HANDED OVER: the numbers build, `%TEMP%\tgb\fogfix-handover-2`: native
  `TGPathTracing.Native.dll` SHA-256 `B406F0A14B45386604311A1554697E16D8E0C98EAB4F609B1D142FE0C34B6093`, plugin `TGPathTracing.dll`
  SHA-256 `A69C6E778929B7395A455379EBB73DF2394F496A5364A0F4DB01373BE71300F4` (the run: "For the coordinator: the numbers run"
  below). Waiting for the game's lines. STATE WHEN STOPPED, if I stop here: everything below is done and written;
  nothing is half-built; the game's answer is the open step.
- AN INCIDENT, MINE TO REPORT (00:20:12): the GPU was reset while one of my offline runs was on it, with the owner's
  Starfield open beside it. What I know: the harness's `-GpuCheck` said at 00:12 "An offline GPU test may start"
  (Starfield at 90 % of the card, 8.9 GB), so at 00:13 I took my own script's extra wait for Starfield out and ran
  my small checks beside it (about 30 runs of 25 s). The run `m-many` (started about 00:19:50) was 600 times slower
  than every other run from its first warm-up frames on (bounced light 2.2 s a frame, reflections 1 s, with one
  light, before the check's own scene was set), then "Interop fence timeout 0x887A0006" and "The GPU stopped (the
  driver reset it) ... stopped in stage bounced light"; Windows logged LiveKernelEvent 141 and the NVIDIA driver
  eight events 153 at 00:20:12 to 00:20:15; my test crashed (d3d11.dll, 0xc0000005). Starfield, running at
  00:12:33, was NOT running at 00:21:05; the Application log has no crash entry for it. I do not know whether my
  test caused the reset or was caught in it, nor whether the owner closed Starfield or the reset did. The run
  after it (`m-cache`) was as fast as usual and passed. My script waits for Starfield to close again; I ran no
  GPU test after 00:21. NOT TO REPEAT without care: `m-many`'s switches (`TGPT_TEST_FOG_NOSUN=1`, `_LAMPS=300`,
  `_CANDLE=1`, `_CACHE=1`, `_BOXES=1`, `_INTERSECT=1`, `_ANISO=0.8`); its slow frames were the OLD route's (the grid
  was not yet on), in passes this job did not touch.
- THE WHITE TAVERN'S CAUSE, REPRODUCED OFFLINE (00:14): the tavern has NO SUN in the frame, and without a sun the
  cells' pass of every build of 2026-10-11 gives garbage.
  - The proof: the very build that was white in the game (the 23:00 native, `A4A15958...`) with the check's sun
    switched off (`TGPT_TEST_FOG_NOSUN=1`: TGPT_Frame.sunColor's w 0) is white offline: every region at the
    composite's ceiling (255.9 of 256), 1,000 to 4,200 times the old route (runs `k-2300-nosun`,
    `k-2300-nosun-nolamp`). With the sun on the same build passes (`c3-cases`). The check's sun was always on. The
    game's interiors have none: the plugin sends a sun only when it finds one (`HdrpPasses.cs`, `if (sun.Valid)`).
    (The tavern's reference radiance of about 30,000 can be a lamp's: it is the larger of the sun's and the
    brightest lamp's over pi. The numbers line says "sun ... (on)" or "(off)": the game will tell.)
  - What goes wrong: in `pt_fog_froxel.hlsl` the light gathered before the lamps (`float3 light`: the sun's and the
    sky's; zero where the sun's code is skipped and the sky is hidden) is added to the lamps' after the lamps'
    loop. On the path that skips the sun's code that value comes out of the loop as GARBAGE in one or two of its
    three channels (green always, red sometimes, blue right): a cell holds a right lamp sample plus about 3e38 in
    green (the largest a float holds). Half a per cent of the cells in one frame, the whole grid after a second of
    history. Nothing is "not finite", so no guard catches it. Numbers (runs `h-sun3`, `i-d3weak`: the same fault,
    met first through my own switch that skipped the sun's and sky's code the same way): the cells' largest
    8.5e33 and 2.8e37 storage units against 0.008 with the sun's code run; that cell's largest lamp sample right
    (1.1, the light itself).
  - Where the fault is: not in the HLSL (zero is zero), and not in what dxc makes of it: the listing of the faulty
    shape (`%TEMP%\tgb\fogfix-tmp\asm\shapeA.ll`, line 6529) has `phi <3 x float> [ the light, ... ],
    [ zeroinitializer, the skipping block ]`, which is right. So it is in what runs that DXIL: the driver's compile
    (616.92, RTX 5080) of a Shader Model 6.9 pass, where dxc keeps vectors whole (the 6.6 variant of the same pass
    splits them into single floats; only the 6.9 variants run in the game and in the check). It depends on the
    code's SHAPE: with the sun's and sky's light summed into a variable of their own first, the same sum does not
    show it, sun on or off. I have NOT pinned down the rule, so the cure is by test, not by proof.
  - The cure in the copy: that shape (a comment in the pass says to keep it and why; the compiled pass is byte
    for byte the same with and without the comment), and FOUR NEW CASES WITH NO SUN in the check, which the 23:00
    build fails by 420,000 % and the copy passes within 0.7 % (run `m-cases`: all eleven cases within 1.9 %).
  - What it says about the day's game runs: every white step had this fault; "the excess not added" looked right
    because its bound cut the garbage off; at 16:10 and 16:48 the pass had a shape that did not trip it, or the
    scene had a sun (I do not know which scene it was).
  - A WARNING beyond this job: any Shader Model 6.9 pass may hold the same trap on a path the checks never take.
    The offline suite's scenes all have a sun; a run of it with the sun off would be a cheap net (Not done).
- THE NUMBERS BUILD (the coordinator's order of 23:30), all of it marked "TEMP DIAG" in the sources:
  - Once a second while the grid runs the tracer's log gets ONE line "Fog grid numbers (TEMP DIAG): ...": the cells
    (how many not finite, mean, how many above 1 and above 100 storage units, how many without history, the means
    of six depth bands, the largest cell with its index, pixel, depth and world place, its history length), the
    lamp that gave that cell its largest sample and the lamp that gave ANY cell the largest sample (light index,
    lamp / spot / glowing cluster, id, strength, radius, reach, flags, world place, distance from the cell's point,
    the weight it was taken with), the filtered cells (trial 2 only), the camera's fog after the walk and after the
    tail (not finite, mean, above 1, above 100, largest and its pixel, the way's transmittance), and the frame:
    HDRP's exposure (read back from its 1 x 1 texture), reference radiance, traced gain, the sun and whether it is
    on, the height fog's constants, max fog distance, the medium's albedo and anisotropy, fog boxes, lights,
    glowing clusters, the light grid's entries, camera and origin.
  - A hidden setting "Traced fog: froxel cache leave out" (Advanced; 0 to 6): 1 the cells dark, 2 no lamps in the
    cells, 3 no sun and no sky in the cells, 4 no mist cache in the cells, 5 no pixel's share of the lamps, 6 a
    trial of a fix for another, smaller thing (no sample at a cell's point the camera does not see). "No excess
    added" no longer exists: there is no excess in this build.
  - New flag bits: `TGPT_FRAME_FOG_FROXEL_DIAG_SHIFT` 26, `_MASK` 15 (TGPT_Frame.flags bits 26 to 29). No export,
    no record, no FrameConstants row, no option bit. New in the tracer's State: `fogProbe[2]` (a second RGBA32F
    volume, 6.3 MB each at 121 x 68 x 48) and six read-back buffers; `GpuScene::diag_light` (scene.hpp); the
    check's `TGPT_TEST_FOG_DIAG`, `_NOSUN`, `_SUN`.
- The 23:00 build (kept aside in `%TEMP%\tgb\fogfix-handover`; white in the game, run 20261011-231804; now the
  check's mutant): native SHA-256 `A4A15958C28DECEBFDF2773477ED244B56F2F5884C12DCD3CA85ACEFCF7CB7D0`, plugin
  `6ABC8E33D375E1772735A404F1737A470F4FC0084AC7D4113F7A34C6E26472D2`.
- FOUND (1), measured: the offline "over-count" of 1.5 to 2.4 times is NOT the grid's. It is the OLD route
  losing light: with a lamp that has a range (the test's lamps: no `TGPT_LIGHT_NO_RANGE`), `fog_lamps_along` takes its
  lamps from the light lists at ONE point of the way (the "anchor": where this frame's fog point fell), and a lamp with
  a range is only in the lists of the cells its range reaches. On the sky's 80 m way the anchor is mostly far from the
  lamp near the camera, so that lamp is not a candidate there and its glow is dropped for that sample. Numbers below.
  With the lamps range-free (as every light the plugin sends but an effect's), the two routes agree. The same loss
  hits glowing surfaces (emissive clusters are sampled as lights only as far as they give 2 lux, and only the lists
  of the cells within that reach hold them) and effects' lights in the game's OLD route (the rule): a defect of its
  own, for the coordinator to enter (not fixed here: it lives in `pt_medium.hlsli`, which every pass includes; the
  exact sampler written for the grid, `fog_lamps_residual`, shows the way: the lists of every light-grid cell the way
  crosses, not of one point).
- FOUND (2), measured: the grid did mis-count a lamp's light near the lamp, by where the lamp sits in its cell (a
  point was drawn over the cell's central half only, as Remix does) and because a cell cannot hold light that
  changes within it: a lamp 0.3 m / 0.02 m from the camera +6 % in the centre / +5 % over the frame (range-free
  lamps, the tracer's own camera rays), and a lamp's glow came out as a blob the size of a cell. FIXED by the split
  (below): 0.9 % and 1.9 % now, and the glow has the old route's shape (rings round the lamp within 1.5 %).
  (An effect's light, 131,072 cd with a range of 0.6 m, read 22 to 31 % brighter in the grid: that one is the OLD
  route's loss again, found (1); the fixed grid reads the same against it.)
- The build's fog design: what was seen in the first trial (8 fresh candidates, one sample a frame, no excess, no
  filter) plus the split of a lamp's light. The fast walk is trial 0, the first trial's walk trial 1, Remix's reuse
  (the frame before's lamp proposed again, 1 to 6 samples, the filter; with the first trial's walk) trial 2.

## Measurements

All offline, `TGPT_TEST_FOG_FROXEL_ONLY=1`, 1920 x 1080, no denoiser, the mean of 16 frames after 144; old route /
grid. Logs and pictures: `%TEMP%\tgb\fogfix-out\<tag>`.

### Before any change

(The copy as it started; shader headers reused from the main tree's build: same sources, same dxc arguments, checked
file by file by `reuse_headers.py`.)

The sky strip's light (rows 808 to 860 from the bottom, columns 600 to 1320), the only place the fog showed (the
check ran on HDRP's G-buffer, where the composite lays the fog over the sky's pixels alone):

| lamps | no second lamp | 2 m | 0.3 m | 0.02 m |
|---|---|---|---|---|
| with a range of 12 m (as the check had them) | 0.0275 / 0.0284 (x1.034) | 0.0408 / 0.0607 (x1.488) | 0.1191 / 0.2509 (x2.107) | 0.2273 / 0.5407 (x2.379) |
| range-free (as the plugin's lights) | (run stopped: the game opened) | 0.0617 / 0.0618 (x1.002) | 0.2505 / 0.2521 (x1.006) | 0.5098 / 0.5418 (x1.063) |

- The grid's number hardly moves when the lamp loses its range (0.2509 -> 0.2521 at 0.3 m): the range's window takes
  almost nothing 12 m out. The old route's more than doubles (0.1191 -> 0.2505): it was missing light (found (1)).
- On the tracer's own camera rays (the fog over every pixel), range-free lamps, the old grid: lamp at 0.3 m: back wall
  x1.012, centre x1.060, frame x1.010; at 0.02 m: strip x1.063, wall x1.045, centre x1.033, frame x1.050 (found (2)).
- Game-like setups, the old grid: 40 lamps x0.998; 40 lamps as strong as the game's with its storage units x1.000;
  300 lamps, anisotropy 0.8, the game's strengths: x1.000 to x1.017 (and the same with the coordinator's diagnosis 7
  and 2; with 11, the excess not added, the grid had 11 to 84 % of the light: there the excess carries most of it).
  Nothing white.

### The build for the game

(Native `A4A15958...`; the check's own cases.)

| case | sky strip | back wall | centre | round the candle | frame | grid over old, furthest off |
|---|---|---|---|---|---|---|
| no lamp near the camera | 0.02894 / 0.02893 | 0.04364 / 0.04348 | 0.03663 / 0.03649 | 0.04374 / 0.04350 | 0.06751 / 0.06740 | 0.5 % |
| a lamp 2 m ahead | 0.06171 / 0.06176 | 0.11403 / 0.11443 | 0.25485 / 0.25526 | 0.15786 / 0.15857 | 0.10639 / 0.10645 | 0.4 % |
| a lamp 0.3 m ahead | 0.25062 / 0.25176 | 0.43135 / 0.43358 | 0.79149 / 0.79849 | 0.55740 / 0.56009 | 0.26385 / 0.26487 | 0.9 % |
| a lamp 0.02 m ahead | 0.51023 / 0.51934 | 0.54909 / 0.55854 | 0.55984 / 0.57065 | 0.54829 / 0.55739 | 0.57529 / 0.58648 | 1.9 % |
| a candle-like emitting quad 4 m ahead | 0.02894 / 0.02893 | 1.02157 / 1.02152 | 0.03654 / 0.03663 | 5.05181 / 5.05178 | 0.18093 / 0.18087 | 0.2 % |
| lamp 0.3 m, trial 1 (the first trial's walk) | 0.25092 / 0.25188 | 0.43144 / 0.43293 | 0.79140 / 0.79649 | 0.55728 / 0.56000 | 0.26386 / 0.26484 | 0.6 % |
| lamp 0.3 m, trial 2 (Remix's reuse) | 0.25036 / 0.25261 | 0.43115 / 0.43338 | 0.79287 / 0.79041 | 0.55689 / 0.55847 | 0.26385 / 0.26553 | 0.9 % |

The check passes ("fog-only checks passed": each region within 3 % and 0.0005 of white; run `c3-cases`).
More, one case a run (`d-...`):
- The glow's shape round the lamps, in rings (pixels from the lamp's place; grid over old): the lamp 0.3 m ahead
  x0.995 (0-4), x1.007, x1.015, x1.014, x1.002, x1.009, x1.003 (128-256); the lamp 7.5 m away x0.952 (0-3), x0.978,
  x0.985, x0.989, x1.002, x1.002 (48-96). Before, that lamp's glow was a blob in the grid's picture.
- 300 lamps, anisotropy 0.8, the game's strengths and storage units, a candle: x1.001 to x1.010; the same with trial
  2: x1.001 to x1.014. Fog boxes across the rays with the mist's cache and a lamp at 2 m: x0.999 to x1.002.
- Not judged, the OLD route's loss (found (1)): lamps with a range, one 0.3 m ahead: sky strip x2.103 (the rest
  x1.004 to x1.008); an effect's light (131,072 cd, range 0.6 m, 5 m ahead): back wall x1.263, centre x1.209, round
  it x2.349, frame x1.297. (With the grid as it started these were x1.27, x1.22, -, x1.31: so that difference was the
  old route's loss too, not the grid's.) A dim emitting quad (reach 3 m): x0.996 to x1.000 (too faint to tell).
- GPU time of the fog's passes, offline (ms): old route 0.72; grid: cells 0.13, walk 0.53, pixel's share 0.47
  (1.13). With 300 lamps: old route 1.66; grid: cells 0.30, walk 0.57, pixel's share 2.65 (3.5). Trial 1's walk
  1.17 to 1.21 against 0.53 to 0.57. So this build is NOT faster than the old route where there are many lamps: the
  pixel's share asks the lists of every stretch of the ray (three to four times the old route's candidates). What
  would cut it without touching the picture: choose the lamp by a cheap measure (its strength and place, no cookie,
  no fog on its way, no texture) and evaluate its light once it is chosen; not done before the game has answered.

### 2026-10-12: no sun in the frame

(`TGPT_TEST_FOG_NOSUN=1`, a lamp 2 m ahead unless said; grid over old route, the five regions' furthest.)

| build | sun on | sun off |
|---|---|---|
| the 23:00 build (`A4A15958...`, white in the game) | within 1.9 % (`c3-cases`) | WHITE: x1,004 to x4,222, every region 255.9 of 256 (`k-2300-nosun`); the same with no lamp near the camera (`k-2300-nosun-nolamp`) |
| the copy now (`B406F0A1...`) | within 1.9 %, seven cases (`m-cases`) | within 0.7 %, four cases: a lamp 2 m 0.5 %, the candle 0.1 %, lamp 0.3 m with trial 1 0.6 % and trial 2 0.7 % (`m-cases`) |

- The fault's numbers, read back from the cells (the copy's own TEMP DIAG line; the faulty shape reached through the
  switch "leave out 3" before the shape was changed; runs `h-sun3`, `i-d3weak`): first frame, no history: 1,810 and
  1,527 of 391,680 cells above 100 storage units, the largest 8.5e33 and 2.8e37 (rgb 0.00021, 1.19e34, 0.000012:
  green alone), that cell's largest lamp sample 1.1 as the light itself (3.8e-5 storage units); after 155 frames
  54,596 cells above 100, the camera's fog 65,480 in every pixel (the half float's largest). With the sun's code
  run: the cells' largest 0.0077, their mean 0.0016, the fog's mean 0.00016.
- The copy's switches with the sun off (each the recipe's step; old route = 1): cells dark 0.25 to 0.70 (`m-nd1`),
  no lamps the same (`m-nd2`: this room's cells hold lamp light alone), no sun and no sky 0.999 to 1.002 (`m-nd3`),
  no mist cache in the cells, cache on 0.963 to 0.988 (`m-nd4`), no pixel's share 0.904 to 1.000 (`m-nd5`), the
  trial "seen points only" 1.000 to 1.002 (`m-nd6`), the mist's cache on 0.999 to 1.001 (`m-cache`).
- With the sun at the game's 90,000 lux and on (`TGPT_TEST_FOG_SUN=15000`): the grid 1.3 to 1.7 % over the old
  route (`h-sun`), with the trial "seen points only" 0.2 to 0.6 % (`h-sun6`).

## What the build does (the design)

1. A lamp's light is split in two shares that sum to it exactly at every point (`pt_froxel.hlsli`):
   the cells hold `e d^2 / (d^2 + r^2)`, each pixel's own ray adds `e r^2 / (d^2 + r^2)`; `d` the distance to the lamp
   (or to the point drawn on a glowing triangle), `r` one and a half cell widths at the point's own view depth (16
   pixels: the depth / 58 at 1080 rows; 13 cm at 5 m). The cells' share is smooth across the screen at a cell's size
   and never above `I / r^2`; the pixel's share is the lamp's light right by it, falling as `1 / d^4`.
   No clamp, no cap: nothing is cut and nothing counted twice, whatever `r` is.
   - `r` goes by a cell's WIDTH, not its depth (a cell is 5 to 20 times deeper than wide): the camera's ray sums the
     light along its way, a cell's value is the mean over the whole cell, and the walk's weights between slices sum
     to each slice's own depth, so only across the screen must the cells' share be smooth.
2. A cell's point is drawn over the WHOLE cell (it was its central half, as Remix): the cell then holds the mean of
   the light over it. With the half, a light in a cell's middle counted up to twice and one near its edge not at
   all, for good (found (2)).
3. The pixel's share is `fog_lamps_residual` (in `pt_froxel.hlsli`, called by `pt_fog_tail.hlsl` for every pixel):
   equiangular sampling as the old route's, but EXACT for lamps with a range and glowing surfaces too: it goes
   through the ray stretch by stretch (each the light grid's cell the ray crosses: at most ten in the cells' 20 m),
   takes the lists of each (they hold every light that reaches any point of that stretch), chooses ONE lamp of all
   their candidates by what its near share can send into its stretch, and one point there in proportion to
   `1 / d^2`. Its shadow ray is paid for by chance where the unshadowed light could add less than a hundredth of
   the cells' fog at the pixel (or a thousandth of white), and what it brings is divided by that chance: the mean
   is unchanged.
4. The cells' estimator is the first trial's, which was right in the game at 16:10: 8 candidates of the point's
   lists, one chosen, its shadow traced, one sample a frame, 254 frames of history, no bound on a sample. The
   "excess" split and its two textures are gone.
5. ON TRIAL, one hidden number (`Traced fog: froxel cache trial`, frame flags bits 24 and 25,
   `TGPT_FRAME_FOG_FROXEL_TRIAL_SHIFT/_MASK`), so the ONE game run tells which part was at fault:
   0 the build as described, with the fast walk through the cells (shared tile, homogeneous path);
   1 the walk as the first trial had it (every slice by the medium's own point, the cells read where it projects;
     slow: 10 ms in the tavern; right in the game at 16:10 and 16:48);
   2 with that walk, Remix's reuse: the frame before's lamp proposed again (right in the game at 16:48), 1 to 6
     samples a cell, the Gaussian filter.
   It comes out, or a part becomes the rule, when the game has answered.

Departures from RTX Remix (PT-127's rule: named):
- No firefly clamp (Remix cuts what a cell's sample has above a threshold). Nothing is cut; the part of a lamp's light
  that has no bound on a sample is not in the cells at all (1).
- A lamp's light right by it is traced per pixel (Remix reads it from the cells: a blob the size of a cell).
- A cell's point over the whole cell, not its central half (2).
- Remix's temporal reservoirs, its 1 to 6 samples and its filter are behind the trial number (5), off, until the game
  has said that they are not what went white. The reservoirs' reuse is weighed against the fresh candidates (the
  earlier worker's correction of Remix's documented bias) when on.
- (As before) the medium is walked per pixel, not voxelized; the cells start at the eye; 20 m, 48 slices, /16.

Desk check of the split (`%TEMP%\tgb\fogfix-build\sim_split.py`: 2D, a lamp on the axis, the cells' means read
back with straight lines as the walk reads them; the ray's integral of what the cells hold, grid over truth, for
rays 0.25 / 0.5 / 1 / 1.5 / 2 / 3 / 5 / 8 cells from the lamp):
- the split, r 1.5 cells, a point over the whole cell, lamp at 5 m: 0.957 0.955 0.998 1.014 1.006 1.005 1.003 1.001
  (lamp half a cell off a cell's centre: 0.950 0.988 0.990 1.004 1.023 1.021 1.011 1.005); the same at 2 m and 12 m,
  at 0.3 m 0.92 to 1.00. Within a cell of the lamp most of the light is the pixel's own share, which is exact.
- no split, a point over the whole cell: 1.14 1.71 1.10 1.21 1.02 1.01 (half a cell off: 0.89 1.78 2.13 1.04 1.10 1.04).
- no split, the central half (as it was): 0.19 0.37 0.66 0.83 0.90 0.98 (the blob: far too little right at the lamp,
  where the truth goes as 1 / distance, and the light pushed outward).

## What changed (files; against `base20e`)

- `native/pathtracing/shaders/pt_froxel.hlsli`: the split (`fog_smooth_radius`, `fog_smooth_share`), the light grid's
  cell along a ray (`fog_light_cell_stretch`), the pixel's share (`fog_lamps_residual`; since 23:05 it chooses its
  lamp by strength and place alone).
- `pt_fog_froxel.hlsl`: the cells' share of a lamp, a point over the whole cell, no excess, Remix's sample counts
  only on trial 2. The sun's and the sky's light summed into a variable of their own before the lamps (the shape
  that does not trip the fault of 2026-10-12; a comment there says so). TEMP DIAG: the "leave out" number, each
  cell's largest lamp sample into a second volume, the trial "no sample at a point the camera does not see" (6),
  and the three numbers that bisected the fault (7, 8, 9).
- `pt_fog_filter.hlsl`: no excess (it runs on trial 2 alone). `pt_fog_integrate.hlsl`: the first trial's walk as
  trial 1 and 2; the fast walk leaves cells without history out of what it reads between cells (for the trial 6;
  no change where every cell has history). `pt_fog_tail.hlsl`: the pixel's share for every pixel, then the way
  past the cells as before; TEMP DIAG 5 leaves the pixel's share out.
- `native/pathtracing/src/fog_froxel.inl`: no excess textures, no fresh slots and slot log; the trial number; the
  filter's texture only on trial 2. TEMP DIAG: the read-back and the log line "Fog grid numbers" (`FogDiagShot`,
  `fog_diag_copy`, `fog_diag_place`, `fog_diag_log`), the probe volume, the "leave out" number.
  `tracer.cpp`: `fogExcess` out, `fogGridWalk` -> `fogGridMode`; TEMP DIAG: `fogProbe[2]`, six read-back buffers,
  `fogDiagFrame` in State (made, dropped and let go with the grid), `record_fog_froxels` takes the frame's constants
  and HDRP's exposure texture. `scene.hpp`: TEMP DIAG `GpuScene::diag_light`. `gpu.hpp`: `allocate_fresh`,
  `next_slot`, `free_slots`, `retiring_slots` out (the misuse guard stays).
- `native/pathtracing/include/tgpt_api.h`: NEW frame flag bits 24 and 25, `TGPT_FRAME_FOG_FROXEL_TRIAL_SHIFT` (24)
  and `_MASK` (3); TEMP DIAG: bits 26 to 29, `TGPT_FRAME_FOG_FROXEL_DIAG_SHIFT` (26) and `_MASK` (15). No export, no
  record, no FrameConstants row, no option bit, no ABI change.
- Plugin: `Settings.cs` (hidden `Fog grid diagnosis` out; NEW hidden `Traced fog: froxel cache trial`, 0; TEMP DIAG
  hidden `Traced fog: froxel cache leave out`, 0), `HdrpPasses.cs` (sends both in the flags), `NativeTracer.cs` (the
  four constants).
- `tests/ingame/scenarios.json`: recipes `fog-froxel-why`, `fog-froxel-diag`, `fog-froxel-slots` out; `fog-froxel-b`
  now: 0-game, 1-pixel-none, 2-froxel-none, 3-froxel-first-walk-none (trial 1), 4-froxel-reuse-none (trial 2),
  5-pixel-nrd, 6-froxel-nrd, 7-off; NEW `fog-froxel-numbers` (below).
- `tests/pathtracing_fog_froxel_checks.inl`: the check compares the routes, eleven cases, FOUR OF THEM WITH NO SUN IN
  THE FRAME (new 2026-10-12: the case the tavern is); switches `TGPT_TEST_FOG_NOSUN`, `_SUN`, `_DIAG` among the
  single-case ones. `tests/pathtracing_gpu_test.cpp` (one variable for the check's fog anisotropy).
- Not touched: `pt_medium.hlsli`, `pt_common.hlsli`, `pt_fog.hlsl` (the old route), every other pass. So only the
  eight fog-grid shader headers were compiled; the other 49 are the main tree's own (same sources, same arguments).
- To take out when the game has confirmed the cause (search "TEMP DIAG"): the read-back and its log line, the probe
  volume, the "leave out" setting and its bits, `diag_light`, the numbers 1 to 9 in `pt_fog_froxel.hlsl` and 5 in
  `pt_fog_tail.hlsl`, the recipe `fog-froxel-numbers`. NOT the shape of the sun's and sky's sum, and not the
  check's no-sun cases.

## For the coordinator: the numbers run

The build: `%TEMP%\tgb\fogfix-handover-2` (native `TGPathTracing.Native.dll`, SHA-256 `B406F0A14B45386604311A1554697E16D8E0C98EAB4F609B1D142FE0C34B6093`; plugin
`TGPathTracing.dll`, Mono net472, SHA-256 `A69C6E778929B7395A455379EBB73DF2394F496A5364A0F4DB01373BE71300F4`). It is `base20e` plus this job (what main got since is not in
it). BOTH files are needed: the hidden setting the recipe sets exists in this plugin alone. It passes the check
(eleven cases, four with no sun: run `m-cases`) and the 116 source checks.

- What I expect: step 02 is NOT white any more, and its numbers line says "sun 0 lux (off)". That would confirm the
  cause (State: no sun in the frame, and the old shape of the cells' pass gave garbage then). If step 02 is still
  white, the numbers say where it comes from, and the other steps narrow it.
- Install (game closed): from the copy,
  `%TEMP%\tg-agents\wt\fogfix\tools\harness\Deploy-PtBuild.ps1 -Native -NativeFrom %TEMP%\tgb\fogfix-handover-2\TGPathTracing.Native.dll`
  (it builds the plugin from the copy; the one hashed above is the same sources, built by `build-managed.cmd`).
- The recipe: `fog-froxel-numbers`, in the COPY's `tests\ingame\scenarios.json` (copy that one recipe into the main
  tree's file, or run with the copy's file). Run `tavern-room@fog-froxel-numbers`. Fourteen steps, no denoiser, about
  two and a half minutes of tracing; each grid step stays at least 8 s, so the log gets about 8 lines for it:

  | step | what it sets | what it tells |
  |---|---|---|
  | 01-pixel | the old route | the picture to hold the others against (no numbers line: the grid is off) |
  | 02-grid | grid, trial 0, nothing left out | THE ANSWER: right or white, with its numbers |
  | 03-cells-dark | leave out 1 | the fog without the cells: what the tail alone gives |
  | 04-no-lamps | leave out 2 | the cells without the lamps (in a room without sun: nearly dark cells) |
  | 05-no-sun-no-sky | leave out 3 | the cells without sun and sky (no change expected in the tavern) |
  | 06-no-mist-cache | leave out 4 | the cells without the light scattered more than once |
  | 07-no-pixels-share | leave out 5 | without the new per-pixel share of the lamps (pt_fog_tail) |
  | 08-scattered-once | nothing left out, "light scattered once only" on | no mist cache anywhere (cells, tail, no feeding) |
  | 09-once-no-sun-no-sky | leave out 3, scattered once still on | both of the above |
  | 10-seen-points-only | leave out 6 (a trial of a fix) | the cells kept to points the camera sees: is there less glow along walls and ceiling than in 02? |
  | 11-first-walk | trial 1 | the first trial's walk against the fast one (02): the same picture? |
  | 12-reuse | trial 2 | Remix's reuse, 1 to 6 samples, the filter, with the first walk |

  The steps' switches were run offline with the sun off and came out sane (runs `m-nd1` to `m-nd6`, `m-cache`;
  not `m-many`: State, the incident).
- Where the numbers are: `BepInEx\TGPathTracing\TGPathTracing.Native.log` (the harness copies it into the run's
  folder), lines that start "Fog grid numbers (TEMP DIAG)". Each names its frame, trial and the part left out
  ("diagnosis N"). A line "Fog froxel trial: ... TEMP DIAG: diagnosis N (...)" marks each step's start. Send me
  two or three lines of each step (the last ones of a step are the settled ones: "history" in the largest cell's
  brackets says how many frames that cell has).
- How to read a line:
  - "CELLS ...": light is in STORAGE units (the light itself over the reference radiance, about 30,000 in the
    tavern). The line says what 1 storage unit is on screen ("a light of 1 in storage units shows as ..."). Sane
    cells are thousandths to a few storage units right at a lamp; THE FAULT of 2026-10-11 reads as "above 100" in
    the tens of thousands of cells and a largest cell of 1e30 and more, with a largest lamp sample that is small.
    "without a sample yet" counts cells that have no history (with the trial of step 10: the cells the camera
    sees nothing of); "means by depth" are six bands of eight slices, near to far. The largest cell comes with
    its pixel (cell x 16 + 8), its view depth and its world place.
  - "That cell's largest lamp sample ...": the lamp light of ONE sample (not the mean), as the light itself and in
    storage units, the light's index in the lists (below 1,048,576 a lamp or spot; from there on a glowing cluster),
    its id, strength (candela), radius, reach, flags and world place, how far it was from the cell's point, and
    the weight the sample was taken with. "The largest lamp sample of ANY cell" is the same for the whole grid.
  - "FOG AFTER THE WALK" and "AFTER THE TAIL": the camera's fog target (storage units, half floats: above 65,504 it
    is "not finite"), after the walk through the cells and after the pass that adds the pixel's share and the way
    past 20 m. Their difference is what the tail adds.
  - "FRAME": HDRP's exposure (read back), the reference radiance, the traced gain, the sun (on or off), the height
    fog's constants (g.fog), HDRP's maximum fog distance (fogColor.w), the medium (fogMedium), the far distance,
    the fog boxes, the lights and glowing clusters of the frame, the light grid's entries and the longest list of
    a cell, the camera's world place and the tracer's origin.
- Cost while it is in: one more RGBA32F volume written by the cells' pass and, once a second, 13 MB of cells and
  33 MB of fog target copied and read on the CPU (a hitch of a few milliseconds in that frame). Not for a speed run.
- If step 02 is right: run `tavern-room@fog-froxel-b` for the picture with NRD (its steps 5 and 6), then the TEMP
  DIAG comes out (What changed, its last point) and the grid's speed can be measured.

## Not done, and what I would do next

- The cause of the white tavern is reproduced offline and avoided by the shader's shape (State), but NOT SEEN IN THE
  GAME yet: the numbers run below is the test. If its step 02 is still white, the line's numbers say where.
- The fault's rule is not pinned down (which shapes of code trip the driver's compile of a Shader Model 6.9 pass).
  What I would try next, each a few minutes offline with the 23:00 shape (kept as
  `%TEMP%\tgb\fogfix-tmp\asm\shapeA.hlsl`, the working one beside it as `shapeB.hlsl`, their listings `.ll`): the
  same pass compiled with dxc's `-Gis` (strict IEEE); the 6.6 variant run in the check (needs a switch in the
  tracer: none today); a newer driver. And the two listings are a ready report for NVIDIA or Microsoft.
- The same trap may sit in other Shader Model 6.9 passes on paths the offline suite never takes: its scenes all
  have a sun. A run of the suite with the sun off would show (the coordinator's: a whole run is not a worker's).
- The old route's loss of ranged lamps' and glowing surfaces' fog light (found (1)) is not fixed: `fog_lamps_along`
  is in `pt_medium.hlsli` (all 57 passes compile again, and mirrors', glass's and water's fog use it). It is also
  still what `pt_fog_tail` uses past the cells' 20 m. An entry of its own, with the owner's eye on it: the rule
  route's picture gets brighter round effects' lights and glowing things when it is fixed.
- The pixel's share of a lamp's light has no reuse over frames (the old route blends 10 frames and filters 5 x 5):
  its grain shows within about three cells (48 pixels) of a lamp, one sample a frame. If the game shows that grain:
  accumulate it per pixel as `pt_fog` does (its targets are there).
- Speed of the pixel's share with many lamps (Measurements, the last point; the cheaper choice of 23:05 is in, its
  time with 300 lamps not measured).
- The cells still take light at points behind surfaces (a cell reaches a slice past the wall a ray ends at): as
  before this job, as in Remix. The trial "leave out 6" (no sample at a point the camera does not see; the fast
  walk then leaves cells without history out) is in the build for the game to answer. Offline, with the sun at
  the game's 90,000 lux outside the check's room, it moved the grid from 1.3 to 1.7 % over the old route to 0.2 to
  0.6 % (runs `h-sun`, `h-sun6`): a small leak there. If the game shows a glow along walls and ceilings with the
  grid and not with the trial, the trial becomes the rule (with the first walk and the filter taught the same).
- No mutation checks beyond the one that came for free (the 23:00 build fails the new no-sun cases). Not run: the
  whole GPU test, ctest.
- Files the coordinator's merge will meet in main (changed there since `base20e`): `tracer.cpp`, `tgpt_api.h`,
  `HdrpPasses.cs`, `NativeTracer.cs`, `Settings.cs`, `scenarios.json`. My managed build left `bin` and `obj`
  folders under the copy's `src\*` (not to merge).
- The run `m-many` (State, the incident) is not explained: why its frames were 600 times slower from its first
  warm-up frame, and whether it or something else brought the reset. I did not run it again.

## How it was built (isolation)

- Configure: `%TEMP%\tgb\fogfix-build\configure.cmd` (NRD from the main tree's prebuilt `NRD.lib` and
  `ShaderMakeBlob.lib`, read only; no SDK configure, nothing of an SDK written; every temporary file under
  `%TEMP%\tgb\fogfix-tmp`). Mega Geometry and rain wetness ON, lamp-around, regroup-water and speed-measure OFF, as
  main's build.
- Shaders: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\fogfix-build` (never `-First`): the eight fog-grid
  headers. The other 49 headers are copies of the main tree's (`reuse_headers.py`: only where main's header is newer
  than every file it depends on, the dxc arguments are the same and the sources are byte for byte the copy's).
- Native and test: `build.cmd TGPathTracing.Native`, `build.cmd tgpt_gpu_test`. Plugin: `build-managed.cmd`
  (offline, `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true`, `MSBuildEnableWorkloadResolver=false`).
- GPU runs: `run-fog.ps1` alone (it waits for the main tree's `Run-PtHarness.ps1 -GpuCheck` to exit 0 and no game
  open, runs a copy of the test, and stops its own test when a game opens). Only `TGPT_TEST_FOG_FROXEL_ONLY`.
- Outside `Build-PtShaders.ps1`, and said here: five single runs of dxc on the cells' pass alone (3 s each, into
  `%TEMP%\tgb\fogfix-tmp\asm`: listings of the two shapes, and the proof that the comment changes no byte of the
  compiled pass). No header of the build came from them.
- GPU runs beside the owner's Starfield, 00:13 to 00:21 (State, the incident): `run-fog.ps1` waited for Starfield
  until 00:13 and waits for it again since 00:21.
- Never started the game, deployed, committed, or wrote to the main tree or the game folder.
