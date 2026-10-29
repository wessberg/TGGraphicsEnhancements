# caustgrid - the caustics' stage costs 4 to 7 ms a frame outdoors (PT-114, PT-99)

Worker: Claude subagent `caustgrid`, 2026-10-12 09:05 to 11:30. Copy `%TEMP%\tg-agents\wt\caustgrid`, base `base20p`.
Build tree `%TEMP%\tgb\caustgrid-build` (the build for players, the main tree's compiled shader headers copied in with
their times: only `pt_caustics` compiles, 2 s). Scripts, runs' logs, the kept DLL: `%TEMP%\tgb\caustgrid-scripts`,
`caustgrid-runs`, `caustgrid-dlls`. No game start, no deploy, no commit, nothing written outside the copy and
`%TEMP%\tgb\caustgrid-*`.

## State when stopped (11:30, the job done; no limit was met)

- HANDED OVER, checked offline, NOT YET SEEN IN THE GAME: `%TEMP%\tgb\caustgrid-handover-1`
  - `TGPathTracing.Native.dll`, SHA-256 `A6DA417B13F81A8D9274669CC75CF9AA4EE2DA103D36EAE3C04B5B736FC746EB`
  - `TGPathTracing.dll` (the plugin, net472, built from the copy), SHA-256
    `43DB889189FFD67A3325D123E54E7098E2D2EB17EE75CC0B169BD12852730EFC`
- The copy's sources are the hand-over's (no mutant left in, no measuring define on); source checks 121 of 121;
  `tests/harness_analyze_test.py` passes. No process of this worker runs, no test waits. Test pictures and mutant
  DLLs are deleted; the runs' logs are kept.
- What the coordinator does next (said 11:08): the two recipes on `metal`, `forest` and `water-day`, then the merge.
- If the game says no: read "For the game" and "If the game disagrees" below; a fresh worker needs this file, the
  copy, and `caustgrid-scripts\sea.ps1` (the guarded test run) and `final.ps1` (check, steps, mutants).

## The answer in short

- THE STAGE'S COST IS THE LIGHTS' PICK, not the rays: in every tile the first of 256 threads joined the threads'
  shares alone, twice, while 255 waited: 2.9 ms of a stage of 5.2 with a thousand lamps, whatever lands.
- THE TRIAL, behind the plugin's hidden `Advanced / Caustics grid trial` (0 = as before; frame flag bits 19, 25, 26):
  1 the pick in a pass of its own; 2 and only the tiles with their slot's water under them, and no corner that no
  cell needs; 3 and no light traced that cannot land in view. No limit: the same triangles are written.
- OFFLINE, with 1,003 lamps: the stage 4.8 -> 2.4 ms where no light lands (the sea under ground), 5.0 -> 2.6 with no
  water in view, 8.6 -> 5.2 over a river's bed in view. Nearly all of it is step 1.
- THE SAME PICTURE: the written triangles' count, largest tolerance and checksum equal in every frame, every pixel
  bit-equal, five cameras, two views, 3 and 1,003 lamps, each step. Three mutants fail the check, as they must.

## WHERE THE STAGE'S TIME GOES (the split the brief asked for)

The sea fixture's new part (`TGPT_TEST_SEA_PART=grid`, `TGPT_TEST_GRID_SPLIT=1`; 640 x 360; the sea of 8 levels, about
8,100 tiles a frame on its water, plus a small river), the stage's time with the pass's threads ended at one step
after another (`TGPT_TEST_CAUSTIC_PARTS`), each over more than two of the tracer's one-second windows.

The clean run (10:42 to 10:46, the card quiet; 1,003 lamps, as a town has; the sea under ground, where no light
lands; `caustgrid-runs\run-split3L.txt`), the trial off, ms, each line with what the lines before it ran:

| What runs | The stage | This step |
|---|---|---|
| the stage's other passes (the water's kept wave corners 0.30, where the water has been 0.16, the landed triangles' structure over 65,536 and its emptying 0.10) | 0.50 | 0.50 |
| + all 39,200 groups started, each ended at its first tests | 0.63 | 0.13 |
| + the pick: each thread weighs its share of the lights | 0.81 | 0.18 |
| + THE PICK: THE FIRST THREAD JOINS THE 256 SHARES (the two strongest, the sum) | 2.65 | 1.84 |
| + the pick: each thread draws one of the rest of its share | 2.80 | 0.15 |
| + THE PICK: THE FIRST THREAD JOINS THE 256 DRAWS | 3.82 | 1.02 |
| + the corners' rays straight down | 4.02 | 0.20 |
| + the corners' points on the waves (15 texture reads each) | 4.18 | 0.16 |
| + the shadow rays (three lights a corner) | 4.76 | 0.58 |
| + the two rays on | 4.92 | 0.16 |
| all of it (the writes with it) | 5.20 | 0.28 |

The other two cameras of that run read the same within 0.05 ms up to the shadow rays (no water in view: 5.39 in all;
the sea's bed in view, lit: 6.20 in all, its rays on 0.9 and its writes 0.6). With 3 lamps (an earlier, noisier run)
the pick was 1.7 to 2.7 ms of 3.5 to 4.5.

THE LIGHTS' PICK IS THE COST: 3.2 ms of 5.2, and of that 2.86 ms are two loops the group's first thread runs ALONE
(256 steps each) while the tile's other 255 threads wait at a barrier. About 8,100 tiles a frame do this, whatever
lands. Not the rays: the ray straight down, the waves and the shadow rays together are under a millisecond where no
light lands. That is why the game's stage costs 4.2 ms with 0 triangles written and 4.8 with 136,000 (the game's
scenes hold 319 to 976 lights: its logs' "PT light grid" line), why caustcut's cut of the shadow rays (87 % of them
gone) gained nothing, and why the cost does not follow the screen's size.

With the trial (the same run, the same camera): the pick's own pass 0.75 ms in place of 3.2; all of it 2.5.

## The readers of the landed light, one by one

What `pt_caustics` writes is read through two functions of `pt_common.hlsli`: `caustic_light` (diffuse) and
`caustic_lit` (the trial `TGPT_OPTION_CAUSTICS_LIT`). Both ask the landed triangles' own acceleration structure with
a short ray at a point. Who calls them:

1. `pt_compose.hlsl`, `caustics_at(s)`: at EVERY CAMERA PIXEL's surface (`s.position`, `s.geoNormal`), three places:
   the plain sum (`bounced + sun + caustics_at`), with NRD the part NRD could not hold, and the direct light's debug view.
2. `pt_nrd_prep.hlsl`: at every camera pixel's surface, with NRD (the caustics are in the diffuse light NRD is handed).
3. `pt_water_path.hlsli`, `water_path`: at every SOLID point a path meets while it is inside the water (`medium >= 0`,
   not foam), unless the water trial 4 ("Water light without view") supplies that light another way. Water paths start
   wherever any ray meets water: the camera's rays, the bounced light's, the reflections'. So this reader also asks
   at points off screen.

No other pass reads them (`pt_water_sun.hlsli` and the rest ask nothing of the landed light; `g_scLanded` is only the
measuring build's count of reader 3). PT-130's order (the stage before its readers) holds in this tree.

WHAT A READER CAN FIND is narrower than where readers ask: the producer writes a landed triangle only when
`in_view` passes it (not wholly behind the camera, not wholly beyond one side of the view widened by half). That
rule is still in the pass (`emit_triangle`, `emit_part`), so reader 3 off screen finds nothing today: the header's
"nothing reads it there yet" is no longer true of the readers, but it is still true of what is written. That is
PT-94's gap, not this job's: this job leaves the written triangles exactly as they are.

So the exact rule for a cut is: WORK THAT CANNOT END IN A WRITTEN TRIANGLE may go, and work that gives the same
numbers another way may be done that way. A triangle is written only when (a) its three corners are points of the
slot's water, (b) a light reaches a corner (its shadow ray), (c) its rays on land, and (d) what landed passes
`in_view`.

## The trial: three steps (the plugin's hidden `Caustics grid trial` 1, 2, 3; 0: the pass and its dispatch as before)

STEP 1 (frame flag bit 19, `TGPT_FRAME_CAUSTICS_GRID`): THE LIGHTS' PICK IN A PASS OF ITS OWN.
- The tiles the pass would run are listed on the CPU (`tracer.cpp` `caustic_tile_list`: a slot with water, not
  wholly in the finer level's square, on the slot's extent: the pass's own first tests; the square by the same whole
  numbers, the extent with a centimetre to spare, so the pass's own test still decides the tiles at the edge). A group
  runs for each listed tile (the pass reads its tile from the list, `pass.a.z`), where a group ran for every tile
  of every level and slot: 35 x 35 x 8 x 4 = 39,200 groups of 256 threads, each testing itself.
- Before it, the pass's mode 3 (`pt_caustics.hlsl` `pick_tile`): a thread a listed tile works out that tile's three
  lights as the tile's 256 threads did: share by share in the threads' order (share t: the lights t, t + 256, ...
  and the sun as the light after the last, weighed in that order, drawn with the random numbers of the thread that
  had the share), joined in the same order with the first share's numbers going on. Every sum and every draw
  happens in the same order, so the two strongest lights, the third light and the number it counts by are the same.
  It writes 16 bytes a tile (a buffer of 0.6 MB, made when the trial first runs); the tiles' pass reads its three
  lights, and none of its threads waits for a pick: four barriers and the two lonely loops are gone from every tile.
  (The pick inside the tiles' pass, `pick_lights`, is the base's text word for word, with the measuring build's
  stops: it runs when the trial is off.)

STEP 2 (bit 25 with it, `TGPT_FRAME_CAUSTICS_GRID_WATER`): ONLY TILES WITH WATER, ONLY CORNERS A CELL NEEDS.
- The list leaves out A TILE WITH NO TRIANGLE OF ITS SLOT'S LEVEL WATER UNDER IT. A corner is a point of the slot's
  water only where a ray straight down meets one of the slot's water meshes (`water_base`). The CPU holds those
  meshes' triangles in the world (`scene_water.cpp`, the water surface's plan). `GpuScene::water_tiles` marks every
  tile a triangle's footprint comes within 10 cm of (box against box, then the triangle's three edges; a range of
  triangles no wider than a tile marks by its box; a triangle that holds the whole grid marks it all at once); an
  unmarked tile has no corner, so it lights nothing and writes nothing. When the CPU does not hold every triangle of
  a slot's water (a mesh still on its way back from the card, a record that ended early) nothing is proved and every
  tile of the slot is listed. Why this matters: a slot's extent is the rectangle round ALL its meshes. The game's two
  rivers are long bent meshes; every tile of all eight levels inside such a rectangle ran its pick and 256 rays down
  for nothing.
- A corner that no cell of its tile needs does nothing: where a tile lies partly in the finer level's square, the
  corners that only cells of that square share were worked out (ray down, waves, shadow rays, rays on) and never
  used: a triangle is made from a cell's own four corners.

STEP 3 (bit 26 with both, `TGPT_FRAME_CAUSTICS_GRID_VIEW`): A LIGHT WHOSE RAYS CANNOT LAND IN VIEW IS NOT TRACED
(`way_out_of_view`). A landed triangle is written only where `in_view` passes it. For each of a tile's three lights
and each branch (reflected, refracted), every corner's ray on (its exact direction, by the corner's own point and
normal) is held against the five regions `in_view` turns a triangle away by (behind the camera; in front and beyond
one of the four sides of the widened view). Where ALL the tile's corners' rays lie wholly in one region, no
triangle of that branch can pass `in_view`, wherever the rays land: the branch is not traced, and with both
branches so, the light's shadow rays are not traced either. The proof: each region is convex, so a ray lies in it
when both its ends do (its start on the water and its end at the ray's full reach, 1,000 km upwards, 200 m under
water); a triangle's three corners land on their rays (a corner that lands nowhere stays at its ray's start), so all
three are in the region, and `in_view` says no. Each end must be inside by a ten-thousandth of the sizes of the
numbers it is worked out from: a hundred times what the floats' rounding of a landed point can be; and the two
tests that decide whether a ray is traced at all are taken with room to spare. Not with the split trial
(`TGPT_OPTION_CAUSTICS_SPLIT`), whose parts land off the corners' rays.

What changes besides speed: nothing that is written. Of the log's counters, the tiles' counts and the waves'
numbers (corners counted, a slot's sample point) count fewer tiles and corners, and a light that is not traced marks
no tile as lit.

A test's switch stays (`TGPT_TEST_CAUSTIC_CUTS`, the environment's): 1 no view test, 2 every corner, 32 every tile
listed (what a step not taken does), 16 no pass for the pick (each tile's own threads pick, as before).

TRIED AND TAKEN OUT (for their numbers; none is in the code): the pick left in the tiles' pass with the first
thread's two loops made leaner (a share that cannot hold one of the two strongest passed over; the join's random
numbers worked out by another thread meanwhile): bit-equal too, but 6.2 -> 4.8 ms where the pick's own pass gives
6.1 -> 3.5 (`run-grid3Lg.txt`). And the corners' level points taken from a level sheet's height without the ray
straight down: the ray down is 0.2 ms of the stage, and its result would be the same only to the last bits.

## The counters and log lines added (in every build)

`pt_caustics`'s counter buffer grew from 128 to 256 words (`kCausticCounters`, `COUNTERS`):
- words 128 to 255, per level and slot: the tiles that RAN, that had a corner ON WATER, that a light REACHED, that
  WROTE a triangle. The log's status line ("Path tracing 10s") says them: "caustics' tiles a frame: N ran, N on
  water, a light reached N, N wrote a triangle (ran / on water / lit / wrote per level ...; per slot ...)", and with
  the trial on "; on trial (PT-114), of N frames: N tiles a frame listed, N left out with no water of their slot
  under them (the list took N ms a frame on the CPU)". THIS IS THE LINE TO READ IN THE GAME: it says, per slot, how
  much of the grid runs for nothing.
- words 2 and 3: a checksum of every triangle written (a sum of a hash of its corners, light, tolerance and
  direction: the same whatever order the threads wrote them in). Two frames wrote the same triangles when the
  count, the largest tolerance and these two words agree. The offline check compares them frame by frame.
- A log line when the trial's step changes: "The caustics' grid, on trial (PT-114), step N of 3: ..." / "The
  caustics' grid: every tile of every level and slot is run and picks its own lights (the trial of PT-114 is off)".
- A log line if the picks' buffer cannot be made ("The caustics' picks could not be made ...; each tile picks its
  lights itself").
- Environment switches, tests' alone: `TGPT_LOG_CAUSTICS_COUNTERS` (a log line a frame with the counters),
  `TGPT_TEST_CAUSTIC_PARTS` (parts of the stage left out, for its time by part: its bits 0 to 3 are read by a
  measuring build's pass alone, `#if defined(PT_SPEED_MEASURE) || defined(PT_CAUSTICS_PARTS)`; 0x100 to 0x1000
  leave out the stage's passes on the CPU's side), `TGPT_TEST_CAUSTIC_CUTS` (above).
- No export, no record and no FrameConstants row is added. New: a buffer of 0.6 MB (the picks; in the memory line's
  caustics sum), a view a frame slot of the tile list in the frame's upload ring (157 KB at most).

## The check and its results

`TGPT_TEST_SEA_PART=grid` (`tests/water_grid_gpu.inl`, a part of the sea fixture by its name alone): the sea
fixture's beach and sea, a terrace with a river of two slanted sheets (its extent mostly dry), the sun and three
lamps without a range (`TGPT_TEST_GRID_LAMPS=1000`: a thousand more). Five cameras (the sea's bed through shallow
water; the water from afar; no water in view, the sea behind the camera; the river's bed; the sea under ground),
each as the picture and as the direct light's view, 16 frames with the trial off, on (`TGPT_TEST_GRID_STEP`: 3
unless said), and off again (the same frame numbers: the same random numbers). Per frame the tracer's own counters
(the triangles written, their largest tolerance, their checksum); per pixel the frames 4 to 15. Then the stage's
time off, at steps 1, 2 and 3, and off again.

ON THE HAND-OVER DLL (11:06 to 11:13; `run-final.txt`, `run-finalL.txt`, `run-final1.txt`, `run-final2.txt`):
step 3 with 3 lamps PASS; step 3, step 1 and step 2 with 1,003 lamps PASS. In each:

| Camera | Triangles a frame, 3 / 1,003 lamps | Tiles run, off / step 3 | Counters equal | Pixels that differ |
|---|---|---|---|---|
| the sea's bed | 239,616 / 251,717 | 8,200 / 8,080 | 16 of 16 frames | 0 of 8,294,400 |
| the water from afar | 730,288 / 832,438 | 8,198 / 8,072 | 16 of 16 | 0 |
| no water in view | 174 / 174 | 8,198 / 8,072 | 16 of 16 | 0 |
| the river's bed | 1,268,508 / 1,402,075 | 11,409 / 9,247 | 16 of 16 | 0 |
| the sea under ground | 0 / 0 | 8,404 / 8,121 | 16 of 16 | 0 |

BIT-EQUAL: the count, the largest tolerance and the checksum of the written triangles in every frame, and every
pixel of 12 frames (half floats), in both views. Two runs of the same thing are bit-equal too, so the comparison is
as strict as it can be.

THE STAGE'S TIME PER STEP, 1,003 lamps, ms (`run-final2.txt`; `run-final1.txt` agrees to 0.05):

| Camera | off | step 1 | step 2 | step 3 | off again |
|---|---|---|---|---|---|
| the sea's bed | 5.73 | 3.49 | 3.50 | 3.40 | 5.73 |
| the water from afar | 6.64 | 4.33 | 4.33 | 4.28 | 6.59 |
| no water in view | 4.96 | 2.73 | 2.71 | 2.55 | 4.98 |
| the river's bed | 8.56 | 5.29 | 5.19 | 5.21 | 8.61 |
| the sea under ground | 4.75 | 2.48 | 2.48 | 2.39 | 4.80 |

With 3 lamps (10:28, the build before the clean-up, all steps): 5.00 -> 2.77, 5.81 -> 3.45, 4.25 -> 1.99,
7.50 -> 4.35, 4.10 -> 1.86. Steps 2 and 3 add little HERE: the fixture's river is small (51 to 1,536 tiles left out
of 8,100 to 11,400) and a tile left out now costs only its place in the pick's pass and its rays down. The list
took 0.05 to 0.10 ms a frame on the CPU.

THE MUTANTS (11:14 to 11:19, built from the hand-over's sources, 1,003 lamps; each must fail and does):
- the tiles' proof broken (the river's tiles never marked as having water): 24 FAIL lines (the triangles differ in 8
  of 10 cases);
- the view's test with the wrong width (half the view where `in_view` keeps one and a half): 18 FAIL lines;
- the pick's pass with the join's random numbers not going on from the first share's: 18 FAIL lines.

## For the game

- The plugin: `Advanced / Caustics grid trial` (hidden, an integer 0 to 3, default 0). Recipes
  (`tests/ingame/scenarios.json`, after `caustics-split`): `caustics-grid` (frozen, no denoiser, 64 frames: 0, 3,
  0 again) for the picture; `caustics-grid-speed` (the world running, 12 s each: 0, 1, 2, 3, 0 again) for the
  stage's time per step.
- What to read: the stage's time ("caustics" in the stages' line); the triangles a frame ("caustics: N triangles a
  frame": the same at every step, frozen); "caustics' tiles a frame: ..." per level and slot, and the trial's part
  of that line.
- What to expect: step 1 takes most of the pick's 3 ms out but still picks for every tile inside the rivers' and
  the lake's rectangles (a tile costs the pick's pass about 0.09 microseconds with a thousand lamps: 0.75 ms for
  8,100 tiles); step 2 takes those tiles
  out (how many: the log says); step 3 a little more where the camera looks away from where the light goes. In the
  picture: nothing.

IF THE GAME DISAGREES:
- The triangles a frame differ, frozen, between 0 and a step: which step first (the speed recipe has all three)?
  Step 1: the pick's pass is not the tiles' pick there (a count of lights the fixture does not have: over 16,000?
  an emitter among them? `pick_tile` against `pick_lights`). Step 2: a tile with water was left out: the CPU's
  triangles of that slot are not what the top level holds (`waterCoverKnown_`, the moving things' stand-ins, a mesh
  the plugin sends with another matrix). Step 3: `way_out_of_view` against `in_view`.
- No gain at step 1: read "caustics' tiles a frame" and the lights' count; the pick's pass costs tiles times lamps.
- The CPU's record time went up: "the list took N ms": a river's mesh of very many triangles; the range tree's
  shortcut (a range no wider than a tile marks by its box) is there for it.

## Changed files (against `base20p`)

- `native/pathtracing/include/tgpt_api.h`: three defines and their words, by `TGPT_OPTION_CAUSTICS_SPLIT` (away
  from the fog's lines; the two comments there, "(Bit 19: free ...)" and "(Bits 24 to 29: free ...)", are untouched
  and now out of date: 19, 25, 26 are this trial's).
- `native/pathtracing/shaders/pt_caustics.hlsl`: the header, the counters, `pick_tile`, the pass's mode 3,
  `way_out_of_view`, `corner_to_light`, main. `pt_common.hlsli` is untouched.
- `native/pathtracing/src/tracer.cpp`: `caustic_tile_list`, the picks' buffer, the dispatch, the counters' read-back
  and the status line, the tests' switches.
- `native/pathtracing/src/scene.hpp`, `scene_water.cpp`: `GpuScene::water_tiles` and what it needs
  (`waterCover_`, `waterCoverKnown_`, filled by `plan_water`); `scene.cpp`: one line at `record`'s start.
- `src/TGPathTracing/NativeTracer.cs`, `Settings.cs`, `HdrpPasses.cs`: the constants, the hidden setting, the flags
  (each by the caustics' own lines).
- `tests/water_grid_gpu.inl` (new), `tests/water_sea_gpu.inl` (its include), `tests/source_checks.py`
  (`test_caustics_grid_trial_changes_no_triangle`, new, after the caustics' own; `kCausticCounters=256` in the old
  one), `tests/ingame/scenarios.json` (two recipes), `tests/pathtracing_nosun_checks.inl` (the coordinator's line).
- `docs/agents/caustgrid.md` (this file).

To run the check again: `powershell -File %TEMP%\tgb\caustgrid-scripts\sea.ps1 -Part grid -Dll <a DLL> -Runs
"name:TGPT_TEST_GRID_LAMPS=1000"` (it stages the main tree's native folders with that DLL, asks the main tree's GPU
check before every start and ends its test when a game opens); `final.ps1` is the whole lap with the mutants
(`mutants.py`).

## Choices and ideas not built

- THE PICK'S OWN PASS IS NOW THE LARGEST PART OF THE CAUSTICS' OWN TIME (0.75 ms with 1,003 lamps and 8,100 tiles):
  every tile weighs every lamp twice ("every lamp counts"). Cheaper ways change the random numbers a tile's third
  light is drawn with (a draw among groups of lamps first, then within one): the same light on average, other noise.
  A choice for the owner, not built.
- Each level is an eighth of the tiles' work, and the sea's level surface runs under all the land: a town's eight
  levels of sea tiles are all dark and all run (rays down, waves, shadow rays: 0.9 ms). No exact way was found to
  know a tile dark without its corners' shadow rays (a proof would need one solid thing over the whole tile's
  beam). A reach in metres or fewer levels there would be a limit: not built.
- The stage named "caustics" also holds the water's kept wave corners (0.30 ms) and where the water has been
  (0.16 ms): two other features' passes. Worth a stage of their own in the log, so the caustics' number is theirs.
- The landed triangles' structure is built over 65,536 triangles also where none landed (0.08 ms): the count is
  only known on the card in that frame.
- `in_view` itself (a reader off screen finds nothing): PT-94, not this job.

## Workarounds met (none removed: this job changes no picture)

- `in_view`: the landed light exists only in the camera's widened view (PT-94), so a water path off screen reads none.
- The landed triangles' structure is rebuilt whole every frame, and its room follows the last frames' count (a frame
  that wants more than its room loses the rest: the check warms the room up before it compares).

## Method traps met

- `TGPT_GetStageTimes` says the mean of the last WHOLE one-second window. The sea fixture's "cost" part reads it
  after 90 frames; at 1 to 10 ms a frame that is 0.1 to 0.9 s, so its first line read "caustics 0.00 ms" (the window
  before the sea existed) and its third a mix. The new part runs each case for more than two windows before it reads.
- The base's `tgpt_gpu_test` did not compile (`tests/pathtracing_nosun_checks.inl` line 123, a flag that went this
  morning); the coordinator's one-line fix is in the copy, byte for byte as in main.
- Other workers' GPU tests run beside mine, and the coordinator's game runs and the owner's game hold mine (as they
  must; the guard ended four of my runs when a game opened): stage times are compared only within one run (off, on,
  off again), and the runs before 10:28 are not to be trusted to better than a millisecond.
- A 2 s shader compile waited 20 minutes, twice, behind the coordinator's build of the main tree (`BUILD-FIRST`): as
  it must. A C++ object and the test exe can be compiled alone meanwhile (`compile-one.cmd`, `build-exe.cmd`: no
  shader compile is started).
- The Bash tool's heredocs eat backslashes: a `\n` in a C string became a line break (the test did not compile), a
  path in this report lost its `\t`. Edits go through script files.
- A reasoned guess was wrong for an hour: "four rays a corner, two million corners: the rays are the cost". The
  split said otherwise in its first run. Measure first.
