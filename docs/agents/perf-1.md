# Perf 1: performance back, water first (PT-99)

> Made the rule on 2026-10-10: see `rule-1.md`. Of perf2's three cuts the lamps' search tree (`TGPT_LAMP_TREE`,
> `Speed trial` 16) and the feeders' key (`TGPT_REGROUP_FEED`, 32768) are in every build with no switch; the water's
> short rays (`TGPT_WATER_FLIGHT`, 32) came out; the recipe `speed2` is gone. Those switches, as this report
> describes them, no longer exist. `TGPT_REGROUP_WATER` (2 and 4) and the measuring build are as written here.

Copy `wt\perf1` (base `wt\base75`). Worker started 2026-10-09 about 19:17. Scripts and raw tables: the session's
`scratchpad\perf1-work\`. Build tree: `%TEMP%\tgb\perf1`. Nothing is written to the main tree or the game folder; the
game is never started.

State at 19:52: **in work.** Section 1 is a finding the coordinator should read now. Section 3 holds the first cuts:
three regroupings behind one compile switch and three numbers of the hidden `Speed trial` (the code is in the copy;
source checks 107 of 107, the plugin compiles; the first build of the two passes compiled and linked, its offline run
waits for the game to close; the second build, with the third regrouping, is compiling). Section 1b: what the
measuring run in the game says so far (read from the game's live log while it ran).

**A fresh worker went on from here on 2026-10-09 23:45: read `# perf2` at the end first (its "In short", then its
State). What follows is the first worker's report as it stood; where the two differ, perf2 is the newer.**

## State when stopped (20:29, at the coordinator's word: the owner's usage limit)

Every build and test of this worker is stopped (three processes ended at 20:28: a `cmake --build` of
`%TEMP%\tgb\perf1` and the dxc it had started for `pt_specular_ser.h`). Nothing of this worker runs.

**No cut has run a frame, offline or in the game. Nothing here is measured as a saving.** What exists is code behind
switches that are OFF, two findings from logs (sections 1, 1b, 4), and the switches for the next measuring run.

### What is built, and behind which switch (all default OFF; a default build's passes are the base's to the byte)

| What | Compile switch (CMake) | Run-time switch | Files |
|---|---|---|---|
| the two big passes regroup also by "this pixel feeds the radiance cache" (no regrouping added: another key) | `TGPT_REGROUP_FEED` | `Speed trial` 32768 | `pt_water_path.hlsli` (`regroup_key`), `pt_gi.hlsl`, `pt_specular.hlsl` |
| the water's path regroups at every turn; a point that makes its own patch regroups first | `TGPT_REGROUP_WATER` | `Speed trial` 2; 4 | `pt_water_path.hlsli` (`water_regroup`), `pt_water_sun.hlsli` |
| measuring switches, round 2 (section 6) | `TGPT_SPEED_MEASURE` (it refuses to be built with either of the two above) | `Speed trial` 2, 4, 8 without the environment variable, and 32768 + n: a second page | `pt_common.hlsli`, `pt_water_path.hlsli`, `pt_water_sun.hlsli`, `pt_caustics.hlsl`, `tracer.cpp`, `CMakeLists.txt` |
| the log line of `Speed trial` 1 says the two heights it compares when it refuses | none | `Speed trial` 1 | `tracer.cpp` |
| a test's start makes its pipelines N at a time | none | environment `TGPT_TEST_PASS_THREADS` | `tracer.cpp` (`in_parallel`) |
| every frame of an offline run carries a speed trial | none | environment `TGPT_TEST_SPEED_TRIAL` | `tests/pathtracing_gpu_test.cpp` |
| recipes `regroup`, `measure2`, `measure-water` | | | `tests/ingame/scenarios.json` |
| the hidden setting's text | | | `src/TGPathTracing/Settings.cs` |

### What compiles, exactly

- With ONE option for all three regroupings (the state until 20:17): dxc compiled both reordering passes
  (`pt_gi_ser.h` 365 s, `pt_specular_ser.h` 446 s), the DLL and the test linked, the plugin built with 0 warnings
  and 0 errors, source checks 107 of 107 (20:14), and the tracer's C++ compiled as a measuring build too.
- THEN the option was split in two (`TGPT_REGROUP_FEED`, `TGPT_REGROUP_WATER`: the last edits, in
  `pt_water_path.hlsli`, `pt_gi.hlsl`, `pt_specular.hlsl`, `CMakeLists.txt`, `tracer.cpp`, `Settings.cs`, and a comment
  in `pt_common.hlsli`). SINCE THE SPLIT: `pt_gi_ser.h` compiled with both ON (20:23, by NMake itself); **nothing
  else was compiled or checked: not `pt_specular_ser.h` (stopped half way), not the tracer's C++, not the plugin,
  not the source checks, and no build with `TGPT_REGROUP_FEED` alone.**
- The measuring code: one small pass (`pt_probe`) compiled with `-D PT_SPEED_MEASURE` BEFORE the second page was
  written. **The second page's lines (`speed_measure2`, `SPEED_M2_*`, `PT_RAY_FLAGS`; in the five shader files above)
  have never been compiled.**

### What is half done

- `%TEMP%\tgb\perf1` (both regroupings ON): `pt_specular_ser.h` is MISSING (its compile was stopped); `pt_gi_ser.h`
  is new; the other 38 headers are the main tree's of 19:03, touched. A `cmake --build` there compiles the missing
  pass by itself, one core, 8 minutes: use `tools\Build-PtShaders.ps1 -Throttle 2` first.
- `%TEMP%\tgb\perf1b` (the unchanged pass compiled with -O2, to see whether ANY new reordering pass takes the driver
  past 8 GB, or only one with the regrouping in its loops): its script (`perf1-work\baseline-o2.ps1`) ended with an
  error that was not read; the question is NOT answered. `perf1m`, `perf1m-both`: compile checks only.
- The scripts `perf1-work\baseline-o2.ps1` and `check-measure-cpp.ps1` still pass the old `-DTGPT_REGROUP`.
- This report, section 3, still says `TGPT_REGROUP` in its text and its run line: read `TGPT_REGROUP_FEED` and
  `TGPT_REGROUP_WATER` (the table above is right). Section 6 says "every triangle is solid to the rays" for the
  measuring switch 2: it is so for the two big passes' OWN rays alone (`trace_from_surface`; two source checks pin
  the other queries' lines).
- NOT STARTED: the brief's item 3 beyond the log line (the plugin's highest point of the waters: by the code the two
  heights should agree, so the next run's log line with `Speed trial` 1 has to say which water stands over which);
  the caustics stage's split in ms; any cut inside `water_sun`.

### The next step, in this order

1. `python tests/source_checks.py` and the plugin's build in the copy; mend what the split broke.
2. A tree with `-DTGPT_REGROUP_FEED=ON` ALONE, its two reordering passes compiled, the DLL built. That is the cut to
   give the game first: it adds no regrouping, so the driver makes the two passes as it makes any new build's.
   `tavern-room@regroup` and `water-day@regroup`, step 5 (32768) against steps 2 and 7.
3. `TGPT_REGROUP_WATER` only after that, and with the game's memory watched at its first start: offline the driver
   took the test past its 8 GB ceiling while it made the bounced light's pass with those regroupings (one pipeline
   at a time: 8.0 GB after 180 s, not done). Whether an ordinary new pass does the same is the open question of
   `perf1b`. A worker's offline test cannot make ANY new reordering pass under that ceiling, as far as this shows.
4. Compile one pass with `-D PT_SPEED_MEASURE` (any pass parses all of it), then the measuring build and the runs
   of section 6: they say what the tavern's bounce ray (28 ms) and floor (14 ms) are, and split the water's path.

## 1. READ FIRST: the 90 ms at the beach are NOT in the caustics pass

The brief says "at the water the caustics pass is about 90 ms of the frame". The tracer's own stage lines say
otherwise. What costs 90 ms and more is what the Caustics ROW turns on inside the REFLECTIONS stage (`pt_specular`,
where every water pixel's whole light is made: `water_path`), not the stage named "caustics".

The beach (`water-day`, 1924 x 1084, one ray a pixel, read from the main tree's harness logs with
`perf1-work\stages.py`; each line is a 10 s line of `TGPathTracing.Native.log`, the row ON):

| Run, build | a frame | reflections | bounced light | caustics (the stage) | direct light | composite | caustic triangles a frame (built over; room) |
|---|---|---|---|---|---|---|---|
| `20261009-144507`, 14:46, steps 2 to 4 | 161 to 185 ms | **103 to 120** | 41 to 49 | **6.8 to 7.4** | 2.0 | 2.8 to 4.5 | 1.26 to 1.59 million (2,097,152; 2,097,152) |
| the run before it, 14:3x, the same scene | 165 to 233 | **104 to 154** | 42 to 64 | 6.9 to 7.2 | 2.1 to 2.7 | 2.0 to 3.6 | 1.21 to 1.40 million |
| `20261009-165909`, 17:04, steps 2 to 4 | 183 to 257 | **132 to 180** | 42 to 58 | **8.2 to 9.5** | 2.1 to 2.5 | 2.4 to 3.1 | 1.71 to 1.91 million (4,194,304; 4,194,304: 632 MB) |

- The caustics STAGE (its grid's rays, the landed triangles, their acceleration structure) is 7 to 9.5 ms at the beach,
  4.7 to 5.2 ms in the town and by the campfire (130,000 to 260,000 triangles), and **3.9 ms where it lands NOTHING**
  (the tavern: "0 triangles", 762 pieces of water in the scene, none of it lit in view).
- With the row off the whole frame was 58 ms (the coordinator's number). So the row costs about 90 to 120 ms in the
  reflections stage, about 20 in the bounced light (`pt_gi` runs the same `water_path` where a bounce ray meets
  water), 7 to 9 in its own stage and 2 to 3 in the composite (`caustics_at`).
- What the row turns on in `water_path` (`pt_water_path.hlsli`; `docs/agents/water-8.md` section 7 has the table): at
  every point where a path scatters INSIDE the water, the sun through the waves (`water_sun`, `pt_water_sun.hlsli`:
  tiles of kept wave corners, 4 tiles of 25 corners summed a point; past about 60 m from the camera a whole patch
  made at the point, the old cost); at every surface under water, `caustic_light` (a short ray into the landed
  triangles). With the row off: one straight shadow ray.
- It fits water-8's own forecast: its fixture's deep water costs 43 ns a pixel for the sun's part with the tiles and
  135 ns where the patch is made at the point; 1.2 million water pixels, a tenth to a fifth of them past the grid:
  80 to 100 ms, and 20 more for the path itself.
- THE MEMORY (for `memory1`): the landed triangles' room is 4,194,304 triangles at the beach since the build of
  17:00 (632 MB with their acceleration structure; it was 2,097,152 at 14:46, 316 MB; 1,048,576 is 158 MB). The
  17:04 status line says "the sun's light under the water:" without "in view".

So the order of this job changes: the caustics stage is worth 4 to 9 ms; the water's own path in the two big passes
is worth 100 and more. Both are looked at below.

### Why a water pixel is slow (read from the code)

`water_path` is a loop with no end but chance (each turn: one ray to whatever is next; then EITHER a scatter point
inside the water with the sun through the waves, 100 wave corners and 200 triangles summed; OR the water's surface,
with the sun's glint and its shadow ray; OR a solid thing, with its material, its lights and the landed caustic
light). A GPU runs 32 pixels as one group. Every turn the group pays for EVERY branch any of its 32 pixels takes,
and it loops until its LONGEST path ends. (Worked out, not measured: if a path's own length is 2 to 4 turns, the
longest of 32 is 8 to 12, and a group does about three times the work its pixels need.) water-8 MEASURED the same
thing inside `water_sun`: one point in a thousand that makes its own patch cost 2.0 of the sun's 9.7 ms, because
its group waits.

### What ONE pixel of water does in the reflections' pass (read from `pt_specular.hlsl`, `pt_water_path.hlsli`, `pt_water_sun.hlsli`; counts by the code, none measured)

Before the path: one short ray from the camera to the water's surface (`trace_water`). Then `water_path`, a loop;
EVERY turn starts with one ray to the nearest thing, with no end to its length, and with the alpha test of every
cut-out triangle on its way (`trace_closest`), and, unless the last thing met was a rough solid, a loop over
**every lamp of the cell's list and of the far list** to see whether the ray runs into a lamp's ball
(`reflected_lamps`: the far list holds every lamp that is far from the cell, so at the beach nearly all 468 to 976
of them: a lamp's record read and a ray against a ball for each, every turn).

| The turn meets | What it does | Rays | Loops |
|---|---|---|---|
| the water's surface (the first turn always; later ones from below) | a foam coin (the foam texture and its mask); the sun's glint: **one shadow ray to the sun, as far as the world reaches, for every pixel whose facet faces the sun, however little of the glint's lobe it holds**; one facet drawn; Fresnel's coin: mirrored or through | 1 | |
| a scatter point inside the water (the free flight was shorter than the way to the next thing) | **the sun through the waves** (`water_sun`, below); with the hidden body-lamps trial also the lamps; a new direction, any; then chance decides whether the path goes on (the water's scattering colour: 0.60 at most for the sea, so 2.5 scatter points a path that has one, on average) | 1 (up to the surface) + 2 (shadow) | the tiles: below |
| something solid (the bed, a rock) | its material (every texture of the game's shader for it, decals), its emission, its direct light (the sun's shadow ray, up to 9 lamps weighed, one shadow ray), the landed caustic light (one short ray into the landed triangles' structure, each triangle it crosses read from two buffers); a new direction | 3 | |
| nothing | the sky; the sun's disc where no question asked for it | | |

`water_sun` at a scatter point, with the Caustics row on (off: one straight shadow ray, and nothing below):
1. one ray up along still water's bent ray to the wave surface (`water_above`), three vertices read;
2. for each wave band (3 for the sea) the band's fade; the block of tiles a ray could reach the point through, in
   closed form from the whole grid's record (64 bytes);
3. **each tile of the block asked: 64 bytes read and some 60 sums a tile; 25 to 60 tiles for most points, up to 256**;
4. **the tiles left (2 to 7 for most points; at most 4 summed): 25 corners a tile, each 24 bytes read, a facet
   drawn, Snell's law and the corner followed to the point's plane; then 32 triangles a tile, each against the
   sun's footprint (for the few that reach it: the exact area of a triangle inside a circle, three `atan2`)**;
5. where the tiles cannot serve the point (the sea past about 60 m from the camera; a point deeper than 25 m; a
   tilt at the far end of the facets' lobe; a river's bent mesh; a third and fourth water): a patch made at the
   point, up to 40 rounds, each round up to 33 x 33 corners, each corner the waves read from the game's buffers
   (3 bands x 5 texture reads) and bent;
6. one triangle chosen: the foam there, and two shadow rays (the point to the surface, the surface to the sun).

What it repeats for every pixel that could be done once a frame: the tiles' corners ARE made once a frame already
(`pt_sun_grid.hlsl`, water-8). What stays per pixel is the facet's tilt (one draw of the lobe for the whole
patch, by the pixel's own random numbers) and the bending of 100 corners with it. The sun's glint ray and the
lamps' loop are per turn and could be asked far less often with the same result on average (section 5).

## 1b. The measuring run in the game, the tavern (run `20261009-193722`, read from the game's live log, 19:50)

The measuring build's own frame in the tavern: 81.3 ms (bounced light 55.3, reflections 12.1, direct light 4.7,
caustics 3.9, prepare 2.5, the reuse 1.3, scene 1.0). Each switch's step, its two clean 10 s lines:

| Step | bounced light | reflections | what it says |
|---|---|---|---|
| everything | 55.3 ms | 12.1 | |
| 16: no ray of the pass's own, no shading | 14.3 to 14.5 | 2.5 to 2.6 | the pass's floor: its surface, the sky, the reuse across frames, the regrouping |
| 32: the ray, no shading | 41.2 to 42.3 | 3.1 to 3.3 | **the bounce ray (with the regrouping by what it met) is 27 ms of the bounced light's 55** |
| so: the shading of what the ray met | 13 to 14 | 9 | materials, lamps, sun, cache, decals together |
| 128: no decals | 52.9 to 54.8 | 9.8 to 10.4 | decals: about 2 ms in each |
| 512: no lamps at path vertices | 55.3 to 57.3 | 8.9 to 9.1 | in the reflections 3 ms; in the bounced light inside the run's own wander |
| 2048: no cache feed | 53.9 to 55.5 | 8.0 to 8.3 | **the cache's feed is 4 ms of the reflections' 12** |
| 4096, 8192, 16384 | 57.7 to 61.2 | 11.4 to 13.3 | not readable: the run's own numbers wandered up by 5 ms from 19:46 on (the last step, "everything again", is the yardstick) |
| 64: no materials | 62 | 6 to 12 | not a clean subtraction: the direct light went from 4.6 to 15.5 ms with it (the picture changes what is lit) |
| 32768 | | | this build has no such switch (it was taken out on 10-09): the step is "everything" |

- The tavern's status line: 5,331 instances, 1.53 million triangles, of which **59 characters with 704,000 triangles**,
  and "tested by the shader: 49 instances, every triangle (547,286 triangles, 109,800 of them of submeshes that
  are no cut-out)". No plants.
- So HALF of the tavern's bounced light is its one ray a pixel, in a closed room where no ray flies far. A ray that
  costs 13 ns is not traversal on this card (a tenth of that is). What a ray does on its way there: every triangle
  it crosses in those 49 instances (hair cards, lashes, fur: the characters) is handed to the shader's alpha test
  (`alpha_opaque`: the instance's, the material's and the mesh's records, three indices, three texture coordinates,
  one read of the game's texture), because they have no opacity micromaps (173 standing instances have; no
  character has). And the regrouping itself (Shader Execution Reordering saves and restores every thread's state).
  WHICH of the two it is, this run cannot say. Two switches would: "no alpha test" (every triangle met is solid),
  and the plain compute passes (`Speed trial` 8 with `TGPT_TEST_PLAIN_PASSES`: the same work without the
  regrouping). Section 6 has them as the next measuring run.
- The floor of 14 ms is a quarter of the pass with nothing traced and nothing shaded: to open next (the same two
  switches split it too).

## 2. The caustics stage (4 ms in every scene of the island, 7 to 9.5 at the beach)

What it does every frame while any water is in the traced world and the Caustics row is on (read from
`pt_caustics.hlsl` and `tracer.cpp`; its split in ms is NOT measured: the two switches for it are built, below):

| Step | How much |
|---|---|
| the grid | levels of 512 x 512 cells round the camera, the finest 2 cm, each next twice as coarse; as many levels as reach the farthest water. The sea's quad reaches to the far plane, so it is always all 8: out to 1.3 km |
| threads started | 35 x 35 tiles x 8 levels x 4 water slots x 256 = 10 million; the tiles of a slot with no water, and those inside the next finer level's square, end at once |
| corners that do work | 7,777 tiles a slot with water everywhere under it (the sea): 2 million corners a frame, seen or not |
| each corner | the tile's three lights picked (every thread reads 2 of the scene's 500 to 1,000 lamps, twice); one short ray down onto the level water and the game's waves there (3 bands x 5 texture reads); for each of the three lights one shadow ray to it; where the corner or a corner beside it is lit, two rays on (the reflected one with no end, the refracted one to 200 m) |
| each cell | 2 triangles x 2 branches x 3 lights; written only where the three rays landed on one surface AND in view: 0 in the tavern, 130,000 to 260,000 in the town, 1.2 to 1.9 million at the beach |
| the landed triangles' structure | built anew every frame over a power of two at least half again the recent count (65,536 in the tavern, 2 to 4 million at the beach; the room past the count marked empty first), and its one-instance top level |
| read back | 128 counters |

- The three lights of a tile are "the two that send its water the most light, and one of all the others". By day
  the sun is the first; the other two are lamps, however far (a lamp has no range since PT-50): two of every
  corner's three shadow rays, and their rays on, go to lamps that send the water a millionth of the sun's light.
  Leaving them out would change the picture (by that millionth); asking them by chance in proportion to their
  light would not, and is a choice of noise (section 5).
- In the tavern all of it is for nothing: the sea lies 470 m under the room, the sun lights it, 2 million corners
  trace, and no triangle lands in view. No test was found that says so beforehand without a ray: light mirrored
  off a wave can land anywhere above the water.
- Its memory: 96 bytes a landed triangle and its structure: 158 MB at a million triangles (the room it starts
  with), 316 MB at 2 million, 632 MB at 4 million (the beach since the build of 17:00).
- BUILT for the next measuring run (a measuring build's alone): `Speed trial` 32770, the grid's corners and
  lights with no ray of a light's (the stage's floor, with the structure's build); 32772, with the shadow rays and
  no ray on. The recipe `measure-water` holds both steps.

## 3. Cuts, each with its switch

### Cut 1 (HAND-OVER 1): threads regrouped by what they do next, three ways, one compile switch

With Shader Execution Reordering (which the game runs: "ser=on") a pass can regroup its threads, so that the 32
pixels a GPU runs as one group do the same thing. The two big passes do it once, by the material their ray met.
Three more, each its own number of the hidden `Advanced / Speed trial` (they add up):

| `Speed trial` | What is regrouped | Where it should show |
|---|---|---|
| **2** | the water's path, at every turn of its loop after the first, by what the thread does next (scatters inside the water / meets the water's surface / meets something solid, by its material / meets nothing) | the beach: reflections (118 to 180 ms) and bounced light |
| **4** | inside `water_sun`, the points that make their own patch of waves, before they make it (one point in a thousand near the camera; every point of the sea past the kept corners, about 60 m out) | the beach: reflections |
| **32768** | the two passes' OWN regrouping takes "this pixel's path feeds the radiance cache" into its key. A quarter of the screen's 8 x 8 blocks feed the cache each frame, whole blocks ON PURPOSE (`cache_feeds`: "so that the threads of a wave take the extra ray together or not at all"); the regrouping by material scatters those threads over every group, and every group then runs the cache's ray and its shading for its few feeders | every scene: the tavern's reflections (the feed is 4 of their 12 ms by the measuring run) and bounced light |

- The picture: the same. No thread's numbers change (each keeps its random numbers and draws them in the same
  order; only which threads run side by side changes).
- Where: `pt_water_path.hlsli` (`water_regroup`, `regroup_key`, the turn's key), `pt_water_sun.hlsli` (3 lines),
  `pt_gi.hlsl` and `pt_specular.hlsl` (the key, 4 lines each), `CMakeLists.txt` (the option **`TGPT_REGROUP`, OFF
  by default**: then no pass holds a line of it; ON: the define goes to the two reordering passes alone,
  `pt_gi_ser.h` and `pt_specular_ser.h`; every other pass is the same to the byte; it refuses to be built together
  with `TGPT_SPEED_MEASURE`), `tracer.cpp` (the log says what the numbers do, or that the build has them compiled
  out), `Settings.cs` (the hidden setting's text), `tests/ingame/scenarios.json` (the recipe `regroup`).
- The run: a build configured with `-DTGPT_REGROUP=ON` (only the two `_ser.h` passes compile anew, 6 and 8 minutes;
  the driver then makes those two pipelines again at the game's first start), then
  `Run-PtHarness.ps1 -Scenario water-day@regroup` and `-Scenario tavern-room@regroup`: no denoiser, 16 frames a
  step; steps: as now / 2 / 4 / 32768 / 32774 (all three) / as now again. The measure: the "reflections" and
  "bounced light" stage times of the tracer's 10 s lines in each step, against the two "as now" steps.
- What to expect: NOT KNOWN for 2 and 4; that is the question the run answers. If the waiting is what costs
  (section 1), the reflections stage at the beach falls by a third or more. If it does not fall, the work itself is
  the cost and the next cut is in `water_sun`'s sums. A regrouping costs too (every thread's state is saved and
  restored): 2 can come out slower. 4 alone should be worth what water-8 measured for the rare points (a fifth of
  the sun's part) plus the far sea's share. 32768 costs nothing more than today's regrouping (the same call, one
  more bit in its key): up to 3 of the tavern's reflections' 4 ms of cache feed, and its share of the bounced
  light's.
- The risks: (1) the driver takes the reordering passes badly (longer to make, or refuses them: then the log says
  "Shader Execution Reordering could not be set up" and the plain passes run, as on a GPU that does not reorder);
  (2) the first start of that build makes the two pipelines again: watch the memory (PT-96).
- **NOT RUN, and why (20:10).** dxc compiles both passes (`pt_gi_ser.h` 365 s, `pt_specular_ser.h` 446 s; the
  compiler takes the regrouping inside the loops). The DRIVER then has to make the two pipelines anew, and in the
  offline test that never ended under the 8 GB ceiling a worker's test has: with every pipeline made at once the
  test stopped itself at 8.2 GB after 94 s; with one pipeline at a time (`TGPT_TEST_PASS_THREADS=1`, new in
  `tracer.cpp`) `walkDxr12` took 149 s and the bounced light's reordering pass then took the test to 8.0 GB in 180 s
  more, not done. So no frame was traced with it. In the game the two reordering passes of an ordinary new build
  take 217 to 340 s each (628 s once), and nobody has watched its memory meanwhile (PT-96).
  Whether the regrouping makes the driver's work worse than an ordinary new pass is being measured now: the same
  start with an UNCHANGED pass that is new to the driver (the default source compiled with -O2), same ceiling.
  Result here when it is in. UNTIL THEN: the first start of a build with `TGPT_REGROUP` in the game is a risk to
  the game's memory; watch it, and stop it if the tracer's passes are not made after 12 minutes.

## 4. Does the unlimited world cost the speed? (the owner's question, 19:40)

**The verdict: no, it is a small part.** What grows with the SIZE of the traced world is 5 to 7 ms of a frame of 80
to 180 ms in the harness's frozen world, and by reckoning 3 to 5 ms more in play for the plants in the wind (not
measured). What makes the frame slow is the water's own light (100 ms and more at the water) and what one bounce ray
costs (28 ms in the tavern, a closed room where no ray flies far): neither is about distance. What a ray's unlimited
LENGTH costs outdoors is the one thing not measured yet; the switch for it is built (below).

### 4.1 What grows with the size of the world, by stage (the tracer's own 10 s lines; the harness's world is frozen)

| Scene | instances traced | triangles | the "scene" stage | of which |
|---|---|---|---|---|
| a room with no water in the world (`candle`, `refract-jar`) | | | 0.54 ms | |
| the tavern | 5,331 | 1.53 M | 0.97 to 1.0 | 59 characters (704,000 triangles) refitted; 762 pieces of sea (0.69 M triangles) posed and refitted |
| the town (`metal`) | 16,234 | 4.37 M | 1.25 to 1.48 | 1,379 pieces of water (1.21 M triangles); 636 groups of plants (6.7 to 7.6 M triangles), held still by the frozen world |
| the beach | 10,275 | 2.72 M | 1.6 to 1.7 | 2,459 pieces of water (2.05 M triangles) |
| while a scene loads (single lines of the harness runs of 10-09) | | | 2.0 to 4.0 | new meshes uploaded and built |

- Where it is counted: all of it is INSIDE the "scene" stage's GPU time (the stage runs from the frame's start to
  the top-level structure's build: uploads, characters' skinning, cloth, plants, water, the top level). `prepare`
  (2 to 5 ms) is the camera's own rays: it grows with the screen, not with the world.
- What is done every frame, read from `scene.cpp` and `scene_water.cpp`:
  - the top-level structure is BUILT ANEW over every instance (5,000 to 16,000);
  - EVERY piece of the water's surface is posed from the game's waves and refitted, seen or not, however far (the
    sea is cut finer near the camera and coarser far out: 762 to 2,459 pieces reach to the camera's far plane);
  - every character is refitted (rebuilt in turn every few frames);
  - every group of plants is posed and refitted in every frame in which the wind's clock OR THE CAMERA moved (the
    camera is in the pose's key: leaves fade near it), all of them, however far: 6.7 M triangles in the town. **In
    the harness the world is frozen, so its numbers hold NONE of this, and no log of a moving world was found**
    (every line of the logs in `artifacts\harness` is a harness run's). By the water's rate (posed and refitted
    every frame, frozen or not: about 0.55 ms a million triangles, from the three scenes above) the town's plants
    would be 3 to 5 ms a frame in play. NOT MEASURED: one step of a harness run with the world NOT frozen, or the
    "scene" stage of the status line while the owner plays, says it.
- The caustics stage is the other thing that grows with the world, by how far the WATER reaches: its grid has as
  many levels as reach the farthest water, the sea's quad reaches to the far plane, so every scene of the island
  has all 8 levels (out to 1.3 km: 2 million grid corners a frame, each with a ray down, up to three shadow rays
  and two rays on). 3.9 ms in the tavern, where it lands not one triangle; 4.8 in the town; 7 to 9.5 at the beach.
- On the CPU, outside every stage's GPU time: "cpu=7.7 to 10.8 ms" a frame in the status line, of which "record"
  5.5 to 5.9 ms is the tracer's own thread walking every instance each frame (the instance list, the plants'
  cells, the water's lattice) and "flush" 0.4 to 4.8. With a frame's GPU time at 80 to 180 ms it does not hold
  the frame rate; at 30 frames a second it would start to.

### 4.2 What grows with how far a ray flies

- The measuring run, the tavern: the bounced light's pass with NO ray 14.4 ms, with its ray and nothing shaded
  42.3: the ray is 28 ms. In a closed room, where it ends at a wall after a few metres. So its cost is not its
  length. (What it is: section 1b; the next measuring run's switches split it.)
- The beach: the same two steps give 4.0 and 11.0 ms: the bounce ray is 7 ms there, under an open sky.
- No ray has an end since 2026-10-07 (the plugin sends a thousand kilometres). What that costs was never measured,
  and these two steps cannot say. BUILT for the next measuring run (a measuring build's alone, it changes the
  picture): `Speed trial` 32768 there ends every ray after 60 m (`tracer.cpp`). One step of the recipe `measure2`
  in the town or at the beach gives the number.

### 4.3 What could be saved without a limit that changes the picture

| What | Saves | How |
|---|---|---|
| plants posed and refitted only where their pose changed | most of the plants' share while the wind stands still; little in play (the wind moves them all) | the camera out of the pose's key for a group no leaf of which is near enough to fade |
| the water's pieces: the same poses when the waves did not change (a paused game) | under 1 ms | compare the waves' numbers, as the plants compare the wind's |
| the caustics stage where it can land nothing | up to 4 ms in every scene away from the water | not found yet without a bound on where reflected light can land: section 2 |
| far things built once, kept | nothing: it is done already (only what moves is rebuilt) | |

What WOULD change the picture, as choices for the owner, with nothing of them built: plants past some distance
held still (their shadows and reflections stop swaying there: the ruling of 2026-10-07 says no); coarser far
geometry for rays (the game's own levels of detail are what is traced today); a ray end (the switch above will say
what it is worth before anyone asks for it).

## 5. Choices (written up, not built)

**A. The same picture on average, other noise** (a budget: "may cost noise or speed, never light"; each would be a
hidden switch, default off, and the owner's eye decides; none is built):

| What | Saves (by reckoning; the next measuring run says) | What it costs in the picture |
|---|---|---|
| the sun's glint ray off the water asked by chance where its lobe holds next to nothing: every water pixel whose facet faces the sun sends a shadow ray to the sun today, though the glint is a small patch of the sea; a pixel whose share is under a five-hundredth of white would ask with the chance of its share and count for more | one ray with no end for most pixels of water, every time a path meets the surface: `measure-water` step 7 is its whole | none where the glint shows; elsewhere a grain of at most that five-hundredth |
| the sun through the waves asked one time in two (and counted twice) at a path's second and later scatter points; today every scatter point sums its 100 wave corners, and a path that scatters has 2.5 of them on average (by the sea's scattering colour, 0.60; worked out, not counted) | up to a third of the sun's part (2.5 questions a path become 1.75) | more grain in the water's body: the light of the later scatter points (over half of it) arrives in every second sample, twice as bright |
| the caustics' two lesser lights of a tile asked by chance in proportion to their light against the first's | two thirds of the caustics stage's rays by day (step 13 and 14 say what the rays are of its 4 to 9 ms) | a lamp's caustic by day arrives in rare frames, as bright as a few hundredths of the sun's there |
| the lamps' balls along the water's path found by the hardware's search (a structure of one box a lamp) in place of the loop over every lamp at every turn | `measure-water` step 8 is its whole | NONE: the same answer. Not built because it needs one more entry in the shaders' frame constants, which two other workers' jobs touch today |

**B. What would change what the picture converges to** (for the owner; with what each would save):

| What | Saves | The picture |
|---|---|---|
| the sea past the kept wave corners (60 m) lit from a coarser set of corners | water-8's reckoning: a third of the sun's cost that is left, about 10 ms and more at the beach | the sum of the light stays; its pattern past 60 m is coarser than the waves, under a pixel there |
| the caustics' grid: fewer levels (not to 1.3 km), or only the sun by day | up to 4 ms in every scene | lamps' light off far water, and the sun's off water past the last level, gone |
| a ray end | not known: `Speed trial` 34816 of the measuring build says (section 4.2) | what lies past it is not reflected, shadows nothing, lights nothing |
| plants past some distance held still | by reckoning 3 to 5 ms in a town in play | their shadows and reflections stop swaying there |

## 6. The next measuring run (everything for it is in the copy)

A measuring build from this copy (`-DTGPT_SPEED_MEASURE=ON`, never together with `TGPT_REGROUP`; every pass
compiles anew). New in it:

- it makes the plain compute passes of the two big passes without the environment variable, so `Speed trial` 8
  (no Shader Execution Reordering) works in the game;
- `Speed trial` 2: every triangle is solid to the rays (none is handed to the shader's alpha test); 4: a ray from a
  surface takes its first query's answer (no second look);
- 32768 turns to a second page of switches: with it 16, 32, 64: no sun in the water's body / the tiles asked and
  none summed, no patch / no patch; 128: no glint ray; 256: no lamps' balls along the path; 512: nothing that is
  no water shaded; 1024: one scatter point a path; 2048: every ray ends after 60 m; 2, 4: the caustics' grid with
  no ray / with its shadow rays alone.
- Recipes: `tavern-room@measure2` and `metal@measure2` (what the bounce ray's 28 ms and the floor's 14 ms are:
  solid, one query, plain passes, each with "everything" at the start, in the middle and at the end, as the first
  run lacked); `water-day@measure-water` (the water's path and the caustics' stage by their parts, and the rays'
  length).
- The measuring code compiles (one small pass compiled with the define); the big passes were not compiled with
  it here.

# perf2: a fresh worker continues (started 2026-10-09 23:45)

Scripts: the session's `scratchpad\perf2-work\`. Trees: `%TEMP%\tgb\perf2-*`. Nothing is written to the main tree or the
game folder; the game is never started. Kept current after every step.

## perf2 in short (written 01:55; the offline picture check was still waiting for the GPU: perf2.6 says how it ended)

1. **The stopped worker's finding is true, and larger than written.** A ray off water or see-through glass asks EVERY
   lamp of the scene whether it runs into it (the far list is every lamp): 510 to 976 lamps, 2 to 3 times a water
   pixel, and at every face of every glass pixel in the tavern. By the counts about 2.7 thousand million lamp
   questions a frame at the beach: 15 to 40 of the reflections' 110 ms (reckoned, not measured).
2. **Three cuts, one build, each behind its own compile switch (OFF) and `Speed trial` number. The picture is the
   same with each.** Configure with `-DTGPT_REGROUP_FEED=ON -DTGPT_LAMP_TREE=ON -DTGPT_WATER_FLIGHT=ON`; run
   `water-day@speed2` and `tavern-room@speed2` (perf2.1).
   | Cut | Compile switch | `Speed trial` | Should save | Section |
   |---|---|---|---|---|
   | the lamps found by a search tree made once a frame, the same lamps and the same sum | `TGPT_LAMP_TREE` | 16 | 15 to 40 ms at the beach, 2 to 4 in the tavern | perf2.2 |
   | a ray inside the water asked only as far as its path flies | `TGPT_WATER_FLIGHT` | 32 | 2 to 3 ms at the beach | perf2.3 |
   | the two passes regrouped by "feeds the cache" too (the stopped worker's) | `TGPT_REGROUP_FEED` | 32768 | 2 to 3 ms in the tavern | perf2.1 |
3. **Checked without a GPU:** every default pass's source is the base's, text for text (40 variants); the tree
   against the loop, lamp by lamp, on the CPU (2.9 million yeses, 220,000 of them rounding's own, 44,000 sums bit
   for bit; three broken variants fail); the build with all three compiles and links (8 passes); source checks 108
   of 108; the plugin builds.
4. **The measuring build's second page** is parsed and type-checked in every big pass and has three new switches;
   its recipes are `measure-water` and `measure2` (perf2.5).
5. **What is next by size** (perf2.4): the groups' waiting in the water's path (the stopped worker's
   `TGPT_REGROUP_WATER`: built, the driver is the open question), then the far sea's patches (a second ring of kept
   wave corners: exact where the fine waves have faded; not built), then small things.
6. **One finding that is not speed:** a far lamp's mirror image is drawn by rounding, not by its ball (perf2.2).

## perf2.0 State (kept current)

- 23:50: `python tests/source_checks.py` in the copy: 107 of 107. The plugin builds (0 warnings, 0 errors). So the split
  of the one option in two (`TGPT_REGROUP_FEED`, `TGPT_REGROUP_WATER`) broke no check and nothing of the plugin.
- 00:04: **HAND-OVER 1 is ready (perf2.1).** The tree `%TEMP%\tgb\perf2-feed` (`-DTGPT_REGROUP_FEED=ON` alone): both
  reordering passes compiled (`pt_gi_ser.h` 330 s, `pt_specular_ser.h` 472 s, `Build-PtShaders.ps1 -Throttle 2`), the
  DLL and the GPU test linked. Its other 38 headers are those of `%TEMP%\tgb\perf1` (this copy's default passes,
  compiled by the main tree at 19:03 before it moved on; the same dxc command).
- 00:10: "a default build's passes are the base's to the byte" is now CHECKED, not only claimed: dxc preprocessed all
  25 passes' sources of the copy and of `base75` with the default build's defines (`perf2-work\same-source.ps1`): the
  same text, pass for pass, in all 40 variants. (The check of the check: with `-D PT_LAMP_TREE` the text differs.)
- 00:15: **the lamps' search tree is written (perf2.2)**, behind `TGPT_LAMP_TREE` (OFF) and `Speed trial` 16. Its CPU
  check passes (295,601 times a lamp said yes, 22,709 of them by rounding alone, every one reached by the walk; two
  broken variants fail). Source checks 108 of 108 (one new), the plugin builds.
- 00:28: a second, small cut is written (perf2.3: a ray inside the water is asked only as far as its path flies;
  `TGPT_WATER_FLIGHT`, `Speed trial` 32). The tree `%TEMP%\tgb\perf2-cuts`
  (`-DTGPT_REGROUP_FEED=ON -DTGPT_LAMP_TREE=ON -DTGPT_WATER_FLIGHT=ON`: the build the game should get) compiles its 8
  passes from a snapshot of the sources (`perf2-work\snap\s2`), 2 at a time; it waited 8 minutes for the
  coordinator's own build. Then: the offline picture check (perf2.6).
- 00:35: the measuring build's second page is PARSED and type-checked by dxc in all four big passes, in the compute
  and the reordering variants (`perf2-work\parse-pass.ps1`: dxc's `-ast-dump`, a second a pass, nothing compiled; an
  error put in on purpose is seen), with three new switches (perf2.5). The same for the cuts' defines, with and
  without `PT_REGROUP_WATER`. Two small passes compiled for real with the measuring define (`pt_probe`,
  `pt_caustics`). The measuring tree's own full compile comes after the cuts' tree (one tree at a time).
- 01:04: **the build the game should get compiles and links**: `%TEMP%\tgb\perf2-cuts`
  (`-DTGPT_REGROUP_FEED=ON -DTGPT_LAMP_TREE=ON -DTGPT_WATER_FLIGHT=ON`), all 8 changed passes (the reflections' three
  547 to 560 s each, the bounced light's 284 to 422 s, the walk's two 140 s; 34 minutes at 2 at a time, part of it
  held back because a game run was waiting), the DLL (hash E7A50C9B7337, never deployed), the GPU test, and ctest's
  `tgpt_lamp_tree` (passed, 1.7 s). The other 32 headers are the default build's. Source checks 108 of 108; the
  plugin builds; the default passes' text is still the base's (`same-source.ps1`, after every edit).
- 01:20: one thing more went into the lamps' tree before any frame was traced with it: its sum in the loop's own
  order (perf2.2, 3), so that the offline picture check can ask for the SAME BITS and any difference is a fault,
  not rounding. So `perf2-cuts` (the tree without that) is NOT the build to judge: `%TEMP%\tgb\perf2-final` is,
  compiling now from the snapshot `perf2-work\snap\s4` (8 passes, 2 at a time; a game run of the coordinator's is
  on, so the script holds back). After it, in the same ONE background job: `perf2-regroupw` (the same and
  `-DTGPT_REGROUP_WATER=ON`: 2 passes) and `perf2-measure` (28 passes). The CPU test's draws are now one a statement
  (two in one expression ran in an order the compiler chose: the printed counts changed from build to build).
- 01:39: **`%TEMP%\tgb\perf2-final` is built: THE build to judge** (`-DTGPT_REGROUP_FEED=ON -DTGPT_LAMP_TREE=ON
  -DTGPT_WATER_FLIGHT=ON`, from the copy's sources as they stand): its 8 passes compiled (1,534 s at one or two at a
  time, a game run being on), the DLL (hash 7A72749C0345, never deployed), the GPU test, ctest's `tgpt_lamp_tree`.
  The offline picture checks wait in their one script for the coordinator's harness run to end (perf2.6).
- Kept clean: a stray `pt_specular.i` this worker's first dxc call left in the copy's shader folder is deleted; a
  `__pycache__` folder this worker's test run made in the BASE (`base75\tools\harness`) is deleted again (the base
  holds no file newer than this worker's start).

## perf2.1 HAND-OVER 1: the cache feeders' regrouping alone (`TGPT_REGROUP_FEED`, `Speed trial` 32768)

What it is: section 3, cut 1, the third row. The two big passes regroup their threads once already, by the material
their ray met. With the trial the key also says whether the pixel feeds the radiance cache this frame (a quarter of
the screen's 8 x 8 blocks do). No regrouping is added. The picture is the same.

- **What the coordinator builds** (after merging the copy): ONE build for all three cuts of this report, the native
  tree configured with `-DTGPT_REGROUP_FEED=ON -DTGPT_LAMP_TREE=ON -DTGPT_WATER_FLIGHT=ON` (perf2.2, perf2.3). Then
  `tools\Build-PtShaders.ps1`, `cmake --build`, install, deploy. With the first switch alone the two `_ser.h` passes
  compile anew (330 s and 472 s of one core each here); with all three, 8 passes (the bounced light's and the
  reflections' three each, the walk's two). Every other pass is the default build's, to the byte. The driver makes
  the changed pipelines again at the game's first start, as for any new build: wait for the tracer's "passes made"
  line. With `Speed trial` 0 that build does what today's does (each cut is one test of a number more).
- **The recipe:** `Run-PtHarness.ps1 -Scenario water-day@speed2`, then `-Scenario tavern-room@speed2` (new in
  `tests/ingame/scenarios.json`; no denoiser, 16 frames a step, 25 s a step so that each step holds two of the tracer's
  10 s lines). Steps: 2 as now / 3 `32768` (this cut) / 4 `16` (the lamps' tree, perf2.2) / 5 `32` (the water's short
  rays, perf2.3) / 6 `32816` (all three) / 7 as now again. Read the stages "reflections" and "bounced light" of the
  10 s lines inside each step, against steps 2 and 7. The log's line "Speed on trial (...)" says what each number
  does in that build, or that it is compiled out. The pictures of steps 2 to 7 must be the same picture (16 frames'
  mean, no denoiser: the frozen world's noise is the only difference).
- **What to expect, in ms:** small. The tavern: the cache's feed is 4 of the reflections' 12 ms (the measuring run,
  step 2048); regrouped, only the groups of feeders pay for it: the reflections' pass should fall by 2 to 3 ms. In
  the bounced light the feed could not be told from the run's own wander (under 2 ms). At the beach: nothing that
  can be told from the noise there (10 ms). If step 3 reads the same as steps 2 and 6, the cut is not worth its
  switch and comes out.
- **Verified here:** it compiles and links (above). Not run: a worker's offline test cannot make a new reordering
  pass under its 8 GB ceiling (section 3). `TGPT_REGROUP_WATER` (2 and 4) stays for after this, as section "The next
  step" says: it ADDS regroupings inside loops, and what the driver makes of that is not known.

## perf2.2 The lamps' loop in the water's path: verified, counted, and replaced by a search tree

### What is true of the stopped worker's finding (read again, `pt_common.hlsli reflected_lamps`, `scene.cpp build_light_grid`)

- TRUE, and worse than written: the loop runs over the cell's list AND the far list, and **the far list of every
  coarse cell is every lamp that falls off with distance alone** (`TGPT_LIGHT_NO_RANGE`: all of the plugin's lamps
  since PT-50). The lists differ from cell to cell only in their weights. So each call asks every lamp of the scene:
  an entry read (8 bytes), the lamp's record read (80 bytes), and a ray against a ball. 510 to 976 lamps in the
  game's logs.
- Who calls it: (1) the water's path, at every turn that follows a mirror, a refraction or a scatter point
  (`pt_water_path.hlsli`: the first turn always, where the stretch to the surface is about a millimetre long and
  no lamp can lie in it; then the turn after the surface; then each turn that reaches the bed or the surface from
  inside); (2) the walk through see-through glass, after every face it crosses or mirrors off
  (`pt_dielectric.hlsli`). So it is also "the same question outside water": **every glass pixel of the tavern, and
  every bounce path that meets glass there, asks all 514 lamps at every face.** No other place in the passes loops
  over a whole list per ray (looked for: `pt_emissive.hlsl` loops a cell's list once per emissive triangle when its
  records are built; the caustics' `pick_lights` shares all lamps over a tile's 256 threads, once a tile).
- Its share of the beach's 110 ms, from the code's counts (NOT measured): a water pixel runs the loop 2 to 3 times
  (the first turn, the turn after the surface, and for paths that go under, the turn that reaches the bed or the
  surface again). 1.2 million water pixels and the bounce rays that meet water (a quarter of the other 0.9 million)
  make about 3.3 million loops a frame, each over some 800 lamps: **2.7 thousand million lamp questions a frame.**
  What one costs on this card is not known: at 5 to 15 picoseconds (a simple loop over records every thread
  shares) it is **15 to 40 ms of the 110**. The measuring build's step 8 of `measure-water` (`SPEED_M2_NO_BALLS`,
  already there: no new measuring switch was needed) and the trial below both say it in the game.
- A FINDING on the way, not built (it would change the picture): far from a lamp the loop's question is decided
  by rounding. `b * b - (oc . oc - r * r)` is single precision; its error is up to about 2 millionths of the
  squared distance, so from a few tens of metres on (a 2 cm flame) a ray that passes a lamp by up to 0.14 % of its
  distance can be told "you ran into it", and one that does run into it "you did not". A far lamp's mirror image
  is then a blot a pixel or two wide that rounding draws, not a disc. (Read from the arithmetic and counted by the
  CPU test below, where most yeses of far lamps are rounding's; not looked for in a picture.) The steady formula
  (the ray's distance from the lamp's centre, worked out from the difference vector) would be the correct one. The
  tree below reproduces today's answer, blots and all, so that the picture does not change; mending the formula is
  a choice for the coordinator.

### The cut: the same lamps, found by a tree (`TGPT_LAMP_TREE`, `Speed trial` 16)

- **What it is.** Once a frame the tracer builds a tree of balls over the lamps (`native/pathtracing/src/lamp_tree.hpp`,
  called from `scene.cpp` right where the frame's lights are uploaded: a lamp is a leaf, two children make a node,
  each node a ball round everything under it, halved at the middle lamp along the longest side; laid out in the
  order a walk visits it, each node with the index to go on at when the ray passes its ball by). The shader walks
  it (`pt_common.hlsli reflected_lamps_tree`) and asks only the lamps whose ball the ray can meet.
- **Why the picture is the same, exactly.** (1) A leaf's lamp is asked the loop's own question by the loop's own
  lines (a source check holds the two side by side), and it is counted only where the loop counts it: a lamp that
  falls off with distance alone always (the far list holds it), any other lamp where the point's cell lists it or
  the point lies outside the grid (the far list there is every lamp). (2) The walk passes a ball by only where the
  question could not say yes. Because the question is rounded arithmetic, its yes reaches past a lamp's ball; by the
  reckoning in `lamp_tree.hpp` a lamp it accepts lies within `2.02 r + (2.2 sqrt(2e-6 + e) + e) D` of the ray's
  stretch (r the ball, D the lamp's distance, e how far the direction's squared length is from 1). So a leaf's ball
  is 2.1 times the lamp's largest ball (its own, five times its own or HDRP's: every size the hidden lamp trial B
  can give it), and the shader adds `2.3 sqrt(2e-6 + e) + e` of the distance per ray. (3) The sum: the walk meets
  lamps in another order than the loop, and a float sum of three numbers depends on their order. So the walk holds
  the lamps it met (up to four) and adds them at its end in the loop's own order (the cell's lamps by their index,
  then the others by theirs): the same bits. Only a ray through FIVE or more lamps at once can differ, in its last
  bit.
- **Checked on the CPU** (`tests/lamp_tree_test.cpp`, 2 s, no GPU; `perf2-work\cpu-test.ps1`, and ctest's
  `tgpt_lamp_tree` in a tree with the switch): 8 worlds (a room of 40 lamps, a tavern of 514, a town of 976 on its
  plane, 1,500 lamps over kilometres far from the origin, a cluster of candles, one lamp, two, lamps metres wide),
  84,000 rays aimed at lamps and just past them, from inside their balls, with stretches that end just before and
  after them, with directions up to a thousandth off unit length, in all three sizes of the lamp trial. The
  question is asked the plain way and the way a GPU may fuse it; the walk is walked both ways. **292,811 times a
  lamp said yes, 21,645 of them by rounding alone: the walk reached every one, the lamps counted through the two
  lists were the same lamps, and for the 4,308 rays through three or four lamps at once the walk's sum had the
  loop's bits.** With ten times the rays (18 s): 2,938,630 yeses, 220,474 by rounding alone, 44,005 such sums, no
  failure. Broken on purpose, the test fails: the distance's share at 0.5 for 2.3, 12,416 times; the balls at 0.4
  of their size, 182,310 times; the lamps added as the walk meets them, 1,200 times.
- **What it should save.** The same test counts what a walk reads for ordinary rays over lamps of the game's
  sizes: in a tavern of 514 lamps 45 to 49 nodes a ray (the loop: 514 lamps); in a town of 976, 34 nodes from
  among the lamps, 34 off water beside it and 7 off water some hundreds of metres out (the loop: 976). A node costs
  about what a lamp costs. Making the tree costs the tracer's thread 37 microseconds a frame for 514 lamps and 72
  for 976 (the same test), so it is made anew every frame from the very records uploaded and kept from none. So the lamps' share falls to a tenth in the tavern and to a thirtieth or less at the
  beach: **by the reckoning above 15 to 40 ms of the beach's frame, and 2 to 4 ms in the tavern** (its glass). What
  it truly is, step 4 of `speed2` says.
- **Where it lives, and why there:** the tree's records lie after the frame's lights in the lights' own buffer (the
  root is record number `light count`), in the lights' own layout. So no frame constant, no buffer and no export is
  added while it is on trial (the stopped worker did not build it for want of a constant). When it becomes the
  rule, give it its own buffer and constant, and take the loop out.
- **Its switches:** CMake `TGPT_LAMP_TREE` (OFF: no pass and no line of the tracer holds any of it; ON: the
  define goes to `pt_gi`, `pt_specular` and `pt_walk` alone, in all their variants, and to the tracer's C++; it
  refuses to be built with `TGPT_SPEED_MEASURE`, where 16 is a measuring switch). Run time: `Speed trial` 16; with
  0 the loop runs as before, in the same build. The log says "a ray off water or glass finds the lamps it runs into
  by the lamps' search tree (N nodes over M lights now)", or that the build has it compiled out.
- **Files:** `native/pathtracing/src/lamp_tree.hpp` (new), `tests/lamp_tree_test.cpp` (new), `pt_common.hlsli`
  (`reflected_lamps_tree`, one early way out of `reflected_lamps`, the trial's line in the `speed_trial` comment),
  `scene.cpp` (the tree after the lights; `lightCount` stays the lights' own), `scene.hpp` (`lampTreeNodes`, for the
  log), `tracer.cpp` (the log), `CMakeLists.txt`, `src/TGPathTracing/Settings.cs` (the hidden setting's text),
  `tests/ingame/scenarios.json` (`speed2`), `tests/source_checks.py` (`test_path_tracing_lamp_tree_on_trial`),
  `tests/pathtracing_speed1_checks.inl` (below).
- **What could still be had after it** (not built): boxes in place of balls would about halve the nodes read where
  lamps lie on a plane (a town); the first turn's call, whose stretch is a millimetre long, costs a handful of
  nodes now and needs nothing more.

## perf2.3 The next cut that leaves the picture as it is: a ray inside the water is asked only as far as its path flies (`TGPT_WATER_FLIGHT`, `Speed trial` 32)

- **What was wrong** (read from `pt_water_path.hlsli water_path`): every turn of the path started with a ray that asks
  "what is nearest, however far" (`trace_closest` to 1e30), with the whole answer fetched (the triangle's records,
  corners, normals, the water's own base point). THEN, inside the water, the turn drew how far it flies before it
  scatters, and where the flight was the shorter the answer was thrown away. In the sea that is most turns under
  water (the scattering colour is up to 0.60: a path that scatters does so 2.5 times on average).
- **The cut:** the flight is drawn before the ray, by the same two random numbers in the same order (the ray draws
  none: a source check holds that `trace_closest` names no random number), and the ray ends a little past the flight
  (a thousandth of it and a millimetre more: more than a hit's distance, worked out again from its place, can differ
  from the ray's own count). A hit within that is the same hit as before; a turn whose ray finds nothing there
  scatters, exactly as before. The same turns, the same numbers, the same picture, bit for bit.
- **What it should save:** part of the ray's search and the whole fetch of the answer, for every turn that scatters:
  about one such turn a water pixel. By reckoning 1 to 2 ns of the 86 ns a water pixel costs (below): **2 to 3 ms
  at the beach, nothing elsewhere.** Small, certain, and five lines.
- **Its switches:** CMake `TGPT_WATER_FLIGHT` (OFF: no pass holds a line of it; ON: the same three passes as the
  lamps' tree); `Speed trial` 32. The log: "a ray inside the water is asked only as far as its path flies before
  it scatters". Files: `pt_water_path.hlsli` (`water_flight_reach`; the flight's early draw is shared with the
  stopped worker's regrouping, which drew it between the ray and its regrouping: the same numbers), `CMakeLists.txt`,
  `tracer.cpp`, `Settings.cs`, the recipe's step 5, the same source check.

## perf2.4 Where the rest of a water pixel's time goes, and what is next (read from the code; the measuring run decides)

The game's numbers: the reflections' pass is 103 ms of water at the beach, over about 1.2 million water pixels: **86 ns
a water pixel** (a pixel of the tavern's reflections: 6 ns). The same path in the bounced light's pass: 23 to 34 ms
more. Split by reckoning (water-8's fixture gave the sun's part; the rest from counts; NONE of it measured in the game):

| Part of a water pixel's 86 ns | Reckoned | What it is | State |
|---|---|---|---|
| the sun through the waves (`water_sun`), on the kept corners | 35 to 45 ns | at each scatter point: one ray up to the waves, 25 to 60 tile records asked, 100 corners bent, 128 triangles laid over the sun's footprint, two shadow rays | as it is |
| the same where a patch is made at the point (the sea past the kept corners, about 60 m; one near point in a thousand) | 135 ns for a tenth to a fifth of the pixels; and the others of its group wait for it | up to 40 rounds of up to 33 x 33 corners, 15 texture reads a corner | as it is; the stopped worker's regrouping 4 is for it |
| the lamps' loop | 15 to 20 ns | perf2.2 | **cut: the tree** |
| the turns' own rays (5 or so a pixel) | 10 to 15 ns | one "nearest thing" ray a turn | **cut in part: perf2.3** |
| the glint's shadow ray, the foam, the facet | 5 ns | at each meeting with the surface | choice A (section 5) |
| the bed: its material, its direct light, the landed light | 10 ns | where a path reaches it | as it is |

What is next, by size, with the picture as it is:

1. **The groups' waiting** (not in the table: it multiplies every row). A GPU runs 32 pixels as one group, and a group
   runs every branch any of its pixels takes. In a water pixel's path the branches are costly and differ from pixel
   to pixel (scatter and ask the sun / meet the surface / meet the bed / make a patch). The stopped worker's
   `TGPT_REGROUP_WATER` (2: regroup at every turn; 4: before a patch is made) is the cut for it, it is built, and it
   is the one that could be worth tens of ms. What stands in its way is not the picture but the driver: it ADDS
   regroupings inside loops. perf2.6 says what the offline test could find out about that.
2. **The far sea's patches.** The kept corners reach about 60 m (1,024 corners a side at the finest waves' cell).
   Past them every scatter point makes its own patch: three times the cost, and its group waits. Where the finest
   waves have faded with distance (HDRP fades each band by distance; `water_sun` already asks only the bands that
   still move the surface there: `cellWanted`), a second, coarser ring of kept corners would serve those points with
   the SAME light: the method is exact for any cutting of the surface whose cell is no larger than the waves want
   there, and the code already tests that (`gridCell <= cellOfWaves`). What it needs: a second grid a slot (29 MB
   each), `pt_sun_grid.hlsl` run twice, and `water_sun` choosing the ring. What is not known: where the game's finest
   band fades (the bands' fade is not in any log). If it never fades inside the view, the ring serves nothing, and
   a coarser ring would be choice B of section 5 (coarser than the waves: changes the picture). **First the
   measuring run:** `measure-water` steps 4 and 5 say what the patches cost; then one log line of the bands' fades.
   Not built: a day's job with water-8's fixture, not a certain one.
3. **A tree over the tiles' records** (a record for every 4 x 4 tiles, asked first): fewer of the 25 to 60 tile
   records asked a point. The same tiles are left (a block that cannot reach the point holds no tile that can: the
   test is the same intervals, wider). At most a tenth of the sun's part. Not built.
4. Small and certain: perf2.3 (built). The turn that only re-finds the surface the camera's ray already met (the
   path starts a millimetre before the water and asks for the nearest thing again): one ray a water pixel, 1 to 2 ms
   at the beach; handing the camera's hit over changes the path's start by rounding (the same picture on average, not
   bit for bit). Not built.

What would change the noise or the picture stays a choice (section 5, A and B: none built). One more for the list,
found tonight: the far lamps' mirror images are drawn by rounding (perf2.2); the steady formula would change those
few pixels, for the better.

## perf2.5 HAND-OVER 2: the measuring build's second page, and its two recipes

A measuring build leaves work out, and light with it: never deployed for play, never packaged (CMake warns). It is
built APART from the cuts (CMake refuses `TGPT_SPEED_MEASURE` with any of `TGPT_REGROUP_FEED`, `TGPT_REGROUP_WATER`,
`TGPT_LAMP_TREE`, `TGPT_WATER_FLIGHT`: the same numbers mean other things there).

- **What the coordinator builds:** a tree of its own (as `%TEMP%\tgb\measure` was) configured with
  `-DTGPT_SPEED_MEASURE=ON` and nothing else; every pass compiles anew. Deploy with `Deploy-PtBuild.ps1`, run, put the
  normal build back.
- **The recipes** (`tests/ingame/scenarios.json`; no denoiser, 24 s a step):
  `Run-PtHarness.ps1 -Scenario water-day@measure-water,tavern-room@measure2` (and `metal@measure2` for a town).
- **`measure-water`: the water's path by its parts.** Each step leaves ONE part out; the part's cost is "everything"
  (steps 2, 6, 16) less the step, in the stages "reflections" and "bounced light":

  | Step | `Speed trial` | Leaves out | Answers |
  |---|---|---|---|
  | 3 | 32784 | the sun in the water's body (`water_sun` gives none) | the whole of the sun through the waves |
  | 4 | 32800 | the tiles are asked, none is summed, no patch is made | 3 less 4: what asking the tiles costs; 4 less 2: the sums and the patches |
  | 5 | 32832 | no patch at the point (the tiles as they are) | what the far sea's patches cost (perf2.4, 2) |
  | 7 | 32896 | the glint's shadow ray | the sun's glint off the surface |
  | 8 | 33024 | the lamps' balls along the path | the lamps' loop (perf2.2: what the tree can save at most) |
  | 9 | 33280 | whatever the path meets that is no water (the path ends there) | the bed's shading and what follows it |
  | 15 | 36864 | the landed light at the bed (NEW tonight) | the caustics' landed light, as the path reads it |
  | 10 | 33792 | every scatter point after the first | the later scatter points (choice A, row 2) |
  | 11 | 33680 | sun, glint, lamps' balls and the bed together | the path's bare turns: its rays and the surface |
  | 12 | 34816 | every ray ends after 60 m (the tracer's own switch) | what the rays' unlimited length costs (section 4.2) |
  | 13, 14 | 32770, 32772 | the caustics' grid with no ray; with its shadow rays alone | the caustics STAGE by its parts (section 2) |

- **`measure2`: the tavern's bounce ray (28 ms) and its floor (14 ms).**

  | Step | `Speed trial` | What runs | Answers |
  |---|---|---|---|
  | 10, 11 | 16, 32 | no ray of the pass's own; the ray and no shading | the floor and the ray again, in this build (14 and 28 ms in the first run) |
  | 3, 12 | 2, 34 | every triangle solid to the rays; the same with the ray alone | how much of the 28 ms is the shader's alpha test of the 49 characters' triangles (section 1b) |
  | 4, 13 | 4, 36 | one query a ray from a surface; with the ray alone | the second look past a near back face |
  | 7, 8, 9, 14 | 8, 24, 40, 42 | the plain compute passes: all; no ray; ray alone; ray alone and solid | what Shader Execution Reordering's regrouping costs or gains in each |
  | 15 | 40960 | NEW: no ray of the pass's own, on the second page (it should read as step 10) | the floor, so that the next two can be laid beside it |
  | 16 | 57344 | NEW: no ray and no reuse across frames | **what of the floor's 14 ms is RTXDI's reuse across frames** |
  | 17 | 57352 | NEW: the same as plain compute passes | the floor's last part: the surface, the sky, the cache's feed, the stores, without the regrouping |
  | 18 | 49152 | NEW: everything, without the reuse across frames | the reuse's cost in a whole frame |
  | 5 | 34816 | every ray ends after 60 m | the rays' length (indoors: expect nothing) |

  Why steps 15 to 18: read tonight in `pt_gi.hlsl`, a pixel with no ray of its own still does four things: it loads
  its surface, takes the sky's light for its direction, feeds the cache (a quarter of the pixels), and runs RTXDI's
  reuse across frames (the previous frame's reservoirs and surfaces read, the sample weighed again). The first
  measuring run's "no ray" step left none of these out, so its 14 ms are theirs and the regrouping's. The reuse
  traces a ray of its own only with the Crevice Light row on (it is off by default): if the owner's row is on, that
  ray is in the floor too, and step 16 says so.
- **New in the measuring code tonight** (all between `#ifdef PT_SPEED_MEASURE` and its end; a default build's
  passes hold none of it): `SPEED_M2_NO_LANDED` (4096), `SPEED_M2_NO_RAY` (8192), `SPEED_M2_NO_REUSE` (16384), in
  `pt_common.hlsli`, `pt_water_path.hlsli`, `pt_gi.hlsl`, `pt_specular.hlsl`.
