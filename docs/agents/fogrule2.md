# fogrule2: the fog grid's noise in thick fog, then the grid as the rule (PT-118), after fogrule

Worker: Claude subagent, copy `%TEMP%\tg-agents\wt\fogrule`, base `base20g`, brief `docs/agents/briefs/brief-fogrule2.md`.
Started 2026-10-12 05:02. I go on from `docs/agents/fogrule.md` (read whole). Mine outside the copy: `%TEMP%\tgb\fogrule2-tools`
(scripts, desk checks, pictures cut from the runs, the candidate's sources kept aside in `keep-candidate`).

## State (last written 07:45): THE RULE IS IN THE COPY, BUILT, AND SEEN IN THE GAME; ITS SOURCES ARE HANDED OVER

- THE NOISE: in the valley's thick fog the grid flickered 3.6 times the old route's because the way past its 20 m of
  cells was one raw point a pixel a frame (no history, no filter) and 72 % of that fog's light comes from past 20 m.
  CURED by the cells over the whole way (fogrule's candidate): flicker 1.17 -> 0.17 against the old route's 0.33; the
  calmer route in all four scenes with no denoiser and under NRD, by a factor of 2 to 12 (runs `20261012-051553`,
  `-053455`).
- THE LIGHT: the old route's in three scenes; the vista's 1.5 % was the fog target's 16-BIT FLOATS (trial 5, run
  `-065226`: with 32-bit targets the old route's vista came up from 130 to 133.5 and the rock is the same picture in
  both). With the light scattered once the two routes are 0.4 % apart in the vista (run `-071150`). What is left
  there (0.7 % of the frame, the far castle) is two thirds the mist's cache, which each route feeds at its own
  points: no yardstick (PT-137).
- THE RULE (the cells are the camera's fog; the old route and the trial switches out; the fog's target RGBA32F) is
  applied in the copy, built and RUN IN THE GAME (run `-072747`, its DLL with main's plugin: both steps of `fog-ab`
  are the cells): tavern-room 17.77 / 17.76, vista 132.20 / 132.31, night 54.81 / 54.59, fog 91.82 / 91.68; flicker
  0.35, 0.46, 0.20, 0.18; boil 0.12, 0.11, 0.05, 0.06: trial 5's grid again. The log: "The game's fog traced in
  cells along the camera's rays". 118 source checks pass.
  Binaries, kept aside in `%TEMP%\tgb\fogrule2-rule`: `TGPathTracing.Native.dll`
  `C70025F1C2430545963211540B12B04BE0A2AF4BAB51FEA166FA6BFC3E4737D6`, the plugin `TGPathTracing.dll`
  `2E334D8D74A14A6714513698047452C3E95E937E5DAD93630C8AA1B7ADADCC51`, the check's `tgpt_gpu_test.exe`.
- NOT DONE (all below): the offline check has never reached a frame (five starts, each ended by a game's opening
  while the driver made the passes), so its room cases are held against EMPTY pins: printed, not judged. No mutation
  checks. The cost is not measured in a measuring run and nothing is done about it. PT-138's open points.

## 1. Why the grid was the noisy one in `fog` (shown by the game: trial 1)

The noise was the per-pixel pass past the cells (`pt_fog_tail.hlsl`), not the cells and not the seam.

- The picture (run `20261012-043614`, `fog\overview.jpg`, the grid's change map): the change sat over the sky and the
  far hills, strongest at the horizon, fine grain pixel by pixel; the near ground was as dark as in the old route's
  map; no line at 20 m. A cell is 16 pixels wide and read between cells: it cannot make grain a pixel wide.
- The code: past the cells that pass took ONE point a pixel a frame (the sun's shadow ray, one sky direction over the
  whole sphere, one read of the mist's cache, one lamp) and wrote it straight into the picture: no history, no
  filter. The old route takes the same point but blends it into the frame before's fog (a tenth new) and filters
  5 x 5 pixels. The cells average up to 254 frames. In `fog` 72 % of the fog's light comes from past 20 m.
- The proof: trial 1 has no per-pixel point past 20 m, and the band over the sky is gone
  (`20261012-051553\fog\overview.jpg`): flicker 1.17 -> 0.17, boil 0.33 -> 0.05, detail 2.59 -> 0.59 (the old route:
  0.33, 0.18, 0.60).

## 2. The candidate or the smallest fix as the base: the candidate

By the code it had no per-pixel point past 20 m (the whole way in cells: 48 slices to 20 m, 48 more to the far
distance, each a quarter deeper than the one before, the same 254 frames of history), so it could not have that
noise; the game agreed. The smallest fix with a history of its own for the far pass would have been the old route's
screen-space history kept alive inside the new route. Not built.

## Trial builds handed to the coordinator

| # | when | DLL | SHA-256 | what it is | recipe | the game's answer |
|---|---|---|---|---|---|---|
| 1 | 05:09 | `%TEMP%\tgb\fogrule-handover-1\TGPathTracing.Native.dll` | `3EB0935E741CB202293872EF4E0CA360042FBFEB5431DDE39C9CA821E008105A` | fogrule's candidate as built at 02:13 (main's passes of 01:20, the six fog headers its own), with MAIN'S plugin | `fog-ab` on the four scenes | run `20261012-051553`, below |
| 2 | 05:50 | `%TEMP%\tgb\fogrule2-trial2\TGPathTracing.Native.dll` | `B0C29D3EFD39985645F79213697FB034811128034F9975A32ACC3CE2E3A81081` | the candidate with the cells' light weighed by the medium's scattering (below) | `fog-ab` on the four scenes | superseded by trial 3 before it ran (if it did run: its step 2 is trial 3's step 2) |
| 3 | 05:58 | `%TEMP%\tgb\fogrule2-trial3\TGPathTracing.Native.dll` | `D3DBF12E715EA06C004495B87651705D6C5496F94811B5E0DCF08212F3562A1F` | trial 2, and A DIAGNOSIS in the old route that is never to be merged and is out of the copy again: `pt_fog.hlsl` takes 16 points a pixel a frame where it takes one (the same estimator averaged; its changed source: `fogrule2-trial3\pt_fog.hlsl.diagnosis`) | `fog-ab` on the four scenes | run `20261012-060431`: the vista old (16 points) 129.72, grid (trial 2's) 131.99; the others equal. Neither change moved the vista: both out |
| 4 | 06:16 | `%TEMP%\tgb\fogrule2-trial4\TGPathTracing.Native.dll` | `D8EF98C315C7FF515CEFB99EF06591C69BD80DEBC158FAB1151506E7AEC0942D` | the candidate, and A DIAGNOSIS in the grid's last pass (never to be merged, out of the copy; `fogrule2-trial4\pt_fog_tail.hlsl.diagnosis`): with main's hidden trial number 1 the old route's own estimator in the grid's place (one point over the whole way, no history, no filter), with 2 the walk's points with the light traced at one of them | main's `fog-froxel-b` on the vista | run `20261012-062357`: old 130.99, grid 132.82, number 1: 128.90, number 2: 128.78. The two ways of drawing the points agree |
| - | 06:39 | `%TEMP%\tgb\fogrule-small\TGPathTracing.Native.dll` (fogrule's smallest fix, which has the "leave out" switches) | `941B5AAD...` | no build of mine | main's `fog-froxel-numbers` on the vista | run `20261012-063934`: on the rock the cells' lamps are 0.2 to 1.1 levels, their sun and sky 4.4 to 6.6, the mist's cache 2 to 3.4. Not the lamps |
| the rule | 07:20 | `%TEMP%\tgb\fogrule2-rule\TGPathTracing.Native.dll` | `C70025F1C2430545963211540B12B04BE0A2AF4BAB51FEA166FA6BFC3E4737D6` | THE COPY AS IT IS: the cells are the camera's fog, the old route out, the target RGBA32F | `fog-ab` on the four scenes with main's plugin (both steps are the cells) | run `20261012-072747`: trial 5's grid again in every scene (table under "The rule in the copy") |
| 5 | 06:50 | `%TEMP%\tgb\fogrule2-trial5\TGPathTracing.Native.dll` | `C05BB7040E172022AC65BAF2B11EAFEB36775641D0A93A0A577AAAD9699EBB4D` | the candidate with the fog's three targets in 32-bit floats (one line of `tracer.cpp`: `fogrule2-tools\trial5_edit.py`). THE LAST BUILD THAT HAS BOTH ROUTES | `fog-ab` on the four scenes | run `20261012-065226`: old / grid 17.96 / 17.92, 133.51 / 132.61, 54.50 / 53.83, 91.42 / 91.31. The vista's old route came up by 3 and the rock is the same in both: the 16-bit floats were it |

### Trial 1 in the game (run `20261012-051553`, frozen, no denoiser, 16 frames; old route / grid)

| scene | luma | flicker | boil | detail | the smallest fix's grid had (run `-043614`) |
|---|---|---|---|---|---|
| tavern-room | 17.90 / 17.92 | 0.40 / 0.36 | 0.14 / 0.11 | 5.44 / 5.48 | 17.96, 0.35, 0.12 |
| vista | 130.04 / 131.94 | 0.99 / 0.46 | 0.44 / 0.11 | 9.27 / 9.13 | 128.50, 0.77, 0.18, detail 11.71 |
| night | 52.87 / 52.32 | 1.85 / 0.20 | 1.20 / 0.06 | 2.30 / 2.25 | 54.34 (old 55.05), 0.57, 0.15 |
| fog | 91.68 / 91.70 | 0.33 / 0.17 | 0.18 / 0.05 | 0.60 / 0.59 | 91.29, 1.17, 0.33, detail 2.59 |

The tracer's log: "The fog's cells: 121 x 68 x 96". The fog's stages, each step's last 10 s line (NOT a measuring
run: the game was not held in front; indicative), old route / the candidate's cells + walk + lamps' pass:

| scene | old route | candidate | the smallest fix |
|---|---|---|---|
| tavern-room | 2.96 ms | 0.52 + 1.32 + 1.74 = 3.6 | 3.6 |
| vista | 4.38 | 1.70 + 2.01 + 3.96 = 7.7 | 6.4 |
| night | 8.5 | 2.93 + 2.33 + 4.90 = 10.2 | 13.1 |
| fog | 1.27 | 0.47 + 1.65 + 0.79 = 2.9 | 2.3 |

Where the time is: the lamps' pass (it asks nine candidates of every light cell the way crosses) and the cells (each
feeds the mist's cache every frame; a quarter of them a frame, as surfaces do it, would keep the picture). Untouched.

### Trial 1 under NRD and with the light scattered once (run `20261012-053455`, recipe `fog-rule`; old route / grid)

| scene | NRD: luma | NRD: flicker | NRD: boil | scattered once, no denoiser: luma | flicker |
|---|---|---|---|---|---|
| tavern-room | 16.22 / 15.98 | 0.40 / 0.18 | 0.24 / 0.10 | 17.68 / 17.72 | 0.40 / 0.36 |
| vista | 129.64 / 131.45 | 0.94 / 0.27 | 0.54 / 0.09 | 121.98 / 124.19 | 1.19 / 0.46 |
| night | 55.48 / 54.91 | 1.92 / 0.16 | 1.29 / 0.06 | 53.07 / 52.49 | 1.98 / 0.19 |
| fog | 90.87 / 90.88 | 0.32 / 0.16 | 0.19 / 0.06 | 62.80 / 62.97 | 0.23 / 0.13 |

(The same run's steps with no denoiser repeat trial 1's: 17.77 / 17.84, 130.23 / 132.12, 55.50 / 54.91, 90.67 /
90.76.) Small and not looked into: under NRD the tavern's grid is 1.5 % dimmer (up to 1 level of 255 less in the
upper middle of the picture), where with no denoiser the two are equal; the earlier run `-005626` had the same
(16.7 / 16.4). The fog is laid over the picture after NRD in both.

## The vista: the grid read 1.5 % brighter than the old route (the cause found: the fog target's 16-bit floats)

Measured from the run's own pictures (`fogrule2-tools\diffmap.py <run> vista 1-pixel-none 2-froxel-none full.jpg`:
the two steps' pictures in 16 x 9 blocks):

- Sky, the haze at the horizon, the near ground: equal within 0.6 of 255. The dark rocks at the right and the middle
  ground: +6 to +10 levels on about 70. The far castle: +2 to +4. Picture: `fogrule2-tools\vista-rocks.png` (the
  game's own / old route / grid: the grid's rocks look veiled).
- `fog` and `tavern-room`: equal everywhere within 0.5. `night`: 1 to 2 levels dimmer around the torch.
- The same rocks in the earlier builds' runs: +1 to +3 in the first grid (run `-010243`), +3 to +7 in the smallest
  fix (`-043614`), +6 to +10 in the candidate. So it is not the far cells alone, and it grew with every step that fed
  the mist's cache in more places.
- The vista is the one scene with big sunlit fog boxes: by the plugin's log 200 m boxes, masked, 0.031 and 0.063 per
  metre (its height fog: 0.0015), a 10 m box of 1 per metre. `fog` has no box.

- 06:00, from run `-053455`: the same plus with the light scattered once only (the rocks +6 to +10 again): so it
  is in the directly lit fog, and suspect (b) below is out. On the rock (`fogrule2-tools\regress.py`, a line
  through the pixels in rough linear light): grid = 1.01 x old route + 0.6 % of white (once) and + 0.9 % (all the
  light): THE SAME TRANSMITTANCE AND A VEIL ON TOP. The old route's darkest rock pixels are 7 of 255: it shows next
  to no fog light there. The game's own picture has MORE haze on that rock than either
  (`fogrule2-tools\vista-rock-frames.png`: game / old / grid). The rock is near (6 to 10 m, by the picture): in
  the near cells, which are a few decimetres across. So suspect (a) can hardly be it either: its cells are too
  small there for the fog to change much inside one.
- fogfix measured the two routes in LINEAR light offline with a fog box across short ways: x0.999 to x1.002
  (`docs/agents/fogfix.md` line 153; a box that is the same all through, no mask).
- So the first suspect at 06:00 is (c), the old route's PICTURE: on a short way through a thin box its one point
  a pixel finds fog in few frames (a bright frame, then nothing), and the game's upscaler and tone curve may show
  that as less than its mean. Trial 3 asks the game: the old route with 16 points a pixel (the same mean, calm).
  If its vista comes up to the grid's, the grid was right and the old route's picture was the dark one. If not,
  the grid's plus is real and unexplained.

THE TRAIL (each step a game run; my first three suspects were wrong):
- (a) "a cell holds the plain mean of the light, the pixel's walk needs it where the fog is thick": trial 2 weighed
  the cells' light by the medium's scattering. The vista did not move (131.99 against trial 1's 131.94 and 132.12).
  OUT. (Its change cost 8 to 20 % of the cells' pass and is out of the copy: `fogrule2-tools\keep-trial2`.)
- (b) "the mist's cache is fed in more places": the plus is as large with the light scattered once only (run
  `-053455`: 121.98 / 124.19). OUT.
- (c) "the old route's rare bright frames are shown as less than their mean": the old route with 16 points a pixel
  (trial 3) stays at 129.72. OUT as I meant it. (A real thing beside it: the RAW per-pixel estimates of trial 4, one
  point a pixel with nothing averaged, show 2 luma LESS than the old route with its history: grain does cost light on
  the way to the screen.)
- Trial 4 split the grid: the walk's slice-by-slice points with the light traced at one of them give the same as the
  old route's one point over the whole way (128.78 / 128.90). So the two ways of drawing the medium's points agree,
  and what differs is on the light's side of the grid.
- "the cells keep the lamps the old route loses (PT-132)": I told the coordinator this was it. The run with the
  cells' parts left out (`-063934`, the smallest fix's DLL) says no: on the rock the cells' lamps are 0.2 to 1.1
  levels of 255, their sun and sky 4.4 to 6.6, the mist's cache 2 to 3.4, all of the cells' light 7 to 8.4. OUT.
  NOTHING OF THIS JOB IS EVIDENCE FOR PT-132.
- WHAT I BELIEVE NOW (06:50; from numbers the tracer logged itself, not yet from a trial): THE 16-BIT FLOATS OF THE
  FOG'S TARGET.
  - The tracer stores light relative to a reference radiance: a white wall in full sun, or lit by the scene's
    brightest light 1 m away, whichever is more (`tracer.cpp`, "The reference radiance"). In the vista that is
    5.695e8 (a light or glowing surface of 1.8e9 at 1 m; I have not looked up which); in `fog` 3.9e6, at `night`
    1.2e6.
  - The smallest fix's log line for the vista (run `-043614`, "Fog grid numbers"): the camera's fog after the walk
    is 1.85e-7 storage units on average, 4.636e-7 at most (which is 11, 7 and 6 of the smallest steps in red, green
    and blue). The smallest step of a 16-bit float is 5.96e-8. And "a light of 1 in storage units shows as
    8.819e+04 on screen": ONE STEP IS 0.53 % OF WHITE. The rock's veil is 0.6 to 0.8 % of white.
  - The grid's sum over the cells is the same in every frame, so it rounds to the nearest whole step for good: 0.6
    of a step shows as one. The old route blends a tenth of each frame's sample into the fog of the frame before
    and stores that in the same kind of float: a tenth of a few steps is under half a step and rounds to nothing,
    whatever the samples' mean is, with 16 points as with one. It shows next to no fog there.
  - It fits every run: the light scattered once differs as much; weighing the cells changes nothing; the raw
    estimates of trial 4 keep their light (nothing is blended away) and lose some to their grain; the horizon and
    the sky agree (their fog is hundreds of steps), and so do `fog`, `night` and the tavern.
  - The same view at night (`vista-night`, run `-064400`): the region with the lamps reads 41.2 in the game's own
    picture, 26.7 with the old route, 37.9 with the grid, and the old route's change map has stripes like the lines
    of a map: steps.
  So neither route was right in that view: the old one lost the light, the grid showed it in steps of half a percent
  of white.
- SHOWN (trial 5, run `-065226`, the fog's targets in 32-bit floats): the old route's vista 133.51 (130.04, 130.23,
  130.99 and, with 16 points, 129.72 before), the grid's 132.61 (131.94, 132.12, 132.82 before). On the rock the
  grid = 0.996 x old + 0.05 % of white (it was old + 0.6 to 0.8 %); the old route's darkest rock pixels went from
  7 to 26 of 255; the right half of the frame agrees within 0.5 of 255 (`fogrule2-tools\vista-rock-trial5.png`).
  THE CURE IS IN THE RULE: the fog's one target is RGBA32F (33 MB at 1924 x 1084; the two routes had three of 17).
- THE ROOT is the coordinator's (PT-136, 07:05): a fix in `scene.cpp` for how a glowing mesh counts toward the
  reference is on trial in main. Its own new log line in main's run `20261012-070300` says the vista's 5.695e8 is
  "1 m from the brightest LAMP" (a lamp of 1.8e9), not the glowing mesh (3.2e5 with the fix, 2.4e7 without): so
  that fix does not move this view's reference, and the lamp is the thing to look at. IF the reference comes down
  to the sun's white (2.7e4) a 16-bit fog target would do again there: fog over a near rock is then about 5e-3
  storage units, far above the floor. What 16-bit would GAIN: 17 MB of the 33 and a little bandwidth in three passes. What it would
  COST: the floor is back wherever a real lamp is far brighter than the sun's white at 1 m (a strong effect's
  light at night), and the cells' calm sum would show in steps again there. One run of the rule's DLL with that
  one format changed, on the vista by day and by night, answers it. Not done: RGBA32F stays.
- THE CASTLE, PARTED (run `-071150`, trial 5, `vista@fog-rule`): with the light scattered once the two routes are
  125.84 / 125.37 (the castle's blocks 1.5 to 2.9 of 255 on about 150); with all the light 134.07 / 133.12 (3 to
  6). The mist's cache gives the old route 8.2 luma and the cells 7.8. So two thirds of the castle's difference is
  the light scattered more than once, which each route feeds at its own points (the old one where its one point a
  pixel falls, the cells at every cell the camera's rays read) and neither feeds outside the view (PT-137): no
  yardstick. The third in the light scattered once (at most 2 % there, 0.4 % of the frame) is not explained; it
  is where the old route sparkles most. Under NRD in that run: flicker 0.87 / 0.27, boil 0.49 / 0.10.
- THE FAR CASTLE AS FIRST SEEN (left of the frame, 100 to 300 m): with 32-bit targets the OLD route is the brighter
  one there by 3 to 6 of 255: a veil of about 2 % of white more than the grid's, and 1.9 % less let through
  (`fogrule2-tools\vista-castle3-trial5.png`). Against the game's own picture of that castle: grid = 0.976 x game
  + 0.7 % of white with the same mean (29.64 / 29.64), old route = 0.959 x game + 2.6 %. That is where the old
  route sparkles most. I do not know which is right. The old route goes with the rule; the last build that has
  both is trial 5's.
- FOR THE COORDINATOR, NOT THIS JOB: every 16-bit target that holds light in storage units has that floor in that
  view (31 places name the format in `tracer.cpp`): a white wall in full sun is 4.7e-5 storage units there, under a
  16-bit float's normal range; a shadow at 1 % of white is 8 steps. An entry of its own is owed.

## The rule in the copy (applied 06:52 to 07:21, built 07:19, run in the game 07:27; handed over 07:45)

What it is: the candidate (trial 1) as the camera's fog, the old route and every trial switch out, the fog's target
RGBA32F. Against `base20g`:
- `native/pathtracing/shaders/`: `pt_fog.hlsl`, `pt_fog_filter.hlsl`, `pt_fog_reservoir.hlsli` GONE; `pt_froxel.hlsli`,
  `pt_fog_froxel.hlsl`, `pt_fog_integrate.hlsl`, `pt_fog_tail.hlsl` (fogrule's candidate; `pt_froxel.hlsli`'s header
  says what the game said); `pt_medium.hlsli` (its header; `fog_lamps_along` without the out-parameter the old
  route's history used; its comment names PT-132: EVERY PASS COMPILES AGAIN); `pt_compose.hlsl` (two comments);
  `pt_shaders.hpp.in`.
- `native/pathtracing/src/`: `fog_froxel.inl` (the volumes 96 deep; the read-back, the reservoirs, the filter gone),
  `tracer.cpp` (the old route's pipeline and two of its three targets gone, the target RGBA32F, 14 stages: "fog
  cells", "fog walk", "fog lamps"), `scene.hpp` (`diag_light` gone); `include/tgpt_api.h` (frame flag bits 19 and 24
  to 29 FREED; comments); `CMakeLists.txt` (the passes' list).
- `src/TGPathTracing/`: `Settings.cs`, `HdrpPasses.cs`, `NativeTracer.cs` (the hidden `Traced fog: froxel cache`,
  `... trial` and `... leave out` gone; `Traced fog: light scattered once only` stays, for tests).
- `tests/`: `pathtracing_fog_froxel_checks.inl` (one route: against what is known exactly outdoors, against pins in
  the room; the mist's fixed point printed, not judged), `pathtracing_fog_froxel_pins.inl` (NEW; every pin 0 until an
  offline run fills it: those cases are printed, not judged), `pathtracing_gpu_test.cpp` (fogrule's one variable),
  `ingame/scenarios.json` (`fog-froxel-b` gone; the recipe `fog-turn` and the scenarios `fog-edge`, `vista-night`
  NEW), `source_checks.py` (the stages' names; `test_path_tracing_fog_is_traced_in_cells` NEW: 118 checks, all pass).
- Docs: `docs/TESTING.md` (the fog check's paragraph), `CHANGELOG.md`, `docs/PT-REQUESTS.md` (PT-118, PT-132; NEW entries PT-137 and PT-138: THE NUMBERS ARE THE
  COORDINATOR'S TO CONFIRM, main has PT-136 for the reference radiance), `docs/PROJECT-CONTEXT.md` (9's fog, the frame
  flags, a dead end), `BACKLOG.md` (the one line of PT-118), this file.
ADDED: nothing in the API (no export, record, FrameConstants row, option or flag bit). FREED: frame flag bits 19 and
24 to 29, three hidden settings, one stage, two targets (33 MB less at this size, the remaining one 17 MB more).
How it was made, in order: fogrule's `%TEMP%\tgb\fogrule-build\edit_rule_native.py`, `edit_rule_plugin.py`,
`edit_rule_shaders.py`; then `fogrule2-tools\rule_tests.py`, `rule_tests_2.py`, `rule_target32.py`, `rule_docs.py`,
`rule_api_comment.py`. Every replacement landed once.
HOW TO GO BACK to the candidate with both routes: `fogrule2-tools\keep-candidate-full.tgz` over the copy (it holds
`native`, the three plugin files, the tests named above, `CMakeLists.txt` and the four docs as they were at 06:33).
For the merge: main's `tests/ingame/scenarios.json` has recipes of the comparison that name the setting that is gone
(`fog-froxel`, `fog-froxel-b`, `fog-ab`, `fog-froxel-numbers`, `fog-rule`, and my `fog-turn` in its two-route form):
they go with it. Main's plugin works with the rule's DLL as it is (bit 19 is read by nobody), so the rule's DLL can
be tried before the merge: both steps of `fog-ab` are then the cells.

### The rule's own game run (run `20261012-072747`; the rule's DLL C70025F1C243 with MAIN'S plugin)

Main's plugin still has the hidden switch, the rule's DLL reads it no more: both steps of `fog-ab` are the cells.

| scene | luma, step 1 / step 2 | flicker | boil | trial 5's grid (run `-065226`) | the fog's stages: cells + walk + lamps |
|---|---|---|---|---|---|
| tavern-room | 17.77 / 17.76 | 0.35 / 0.35 | 0.12 / 0.11 | 17.92, 0.36, 0.12 | 0.53 + 1.34 + 1.77 = 3.6 ms |
| vista | 132.20 / 132.31 | 0.46 / 0.46 | 0.11 / 0.11 | 132.61, 0.45, 0.11 | 1.66 + 2.01 + 4.02 = 7.7 |
| night | 54.81 / 54.59 | 0.20 / 0.19 | 0.05 / 0.05 | 53.83, 0.20, 0.06 | 2.87 + 2.36 + 4.90 = 10.1 |
| fog | 91.82 / 91.68 | 0.18 / 0.17 | 0.06 / 0.05 | 91.31, 0.17, 0.06 | 0.47 + 1.65 + 0.80 = 2.9 |

(The game's own picture drifts by up to 1 luma from run to run at the vista and at night: compare within a run. The
first traced step of the run waited six minutes: the driver makes a new build's passes once. Not a measuring run.)

### What the two new entries are (the numbers are confirmed by the coordinator; main has PT-136 for the reference radiance)

- PT-137: the mist's cache (the light the fog scatters more than once) is fed only at the camera's own fog points
  and read wherever a flight ends, also behind the camera or a hill, where nobody feeds it: it reads short. fogrule's
  finding, by reading and a CPU model (x0.86, x0.74, x0.56 at albedo 0.3, 0.5, 0.7); no GPU number. It is also why
  the two fog routes cannot be held against each other in that part of the light.
- PT-138: everything the rule leaves open in the fog's cells, in one entry: (1) far lamps (past 20 m a lamp's light
  right by it is asked with a chance: looked at twice at night, two faint spots at fires with 32-bit targets, no
  sparkle to speak of); (2) a turning camera (a fresh cell has no history: looked at once, no blocks at the edge;
  the steps' numbers cannot be compared); (3) the cost (old route / cells: 2.96 / 3.6, 4.38 / 7.7, 8.5 / 10.1, 1.27
  / 2.9 ms; where it is: the lamps' pass, every cell feeding the cache every frame, cells past the medium tracing);
  (4) the vista's far castle (the old route 2 % of white hazier, two thirds of it the mist's cache); (5) the tavern's
  fog 1.5 % dimmer under NRD than the old route's; (6) the check's empty pins.

### For the merge: main's `docs/PROJECT-CONTEXT.md` has been rewritten since `base20g`

The copy's edits to that file are against the old text and will not merge by themselves. The same content in main's
form (9.4's fog bullets from "`pt_medium.hlsli`: height fog" to "Latest game evidence", the stages' line, the frame
flags' line):

    - `pt_medium.hlsli`: height fog, local fog boxes, the lamps along a reflected or refracted ray's way (equiangular,
      PT-132), multiple scattering from world cache cells. Fog is composed after surface denoising; NRD does not
      denoise it. Sky haze, fog-only lights and box fidelity remain open.
    - The camera's fog is in cells (PT-118, the rule since 2026-10-12; RTX Remix's layout, carried on past its 20 m):
      /16 screen size, 96 slices (48 to 20 m by a square, 48 more to the far distance by a factor), one sample a cell
      a frame where the camera's rays read, 254 frames, RGBA32F, no clamp. Each pixel walks them (`pt_fog_integrate`:
      the medium exact per ray, the cells' light read between centres). Near-lamp light is split: the smooth share in
      cells, the rest per pixel (`pt_fog_tail`, `fog_lamps_residual`). Its target is RGBA32F: at the vista's reference
      of 5.7e8 a 16-bit target held the fog over a near rock in a few of its smallest steps (0.5 % of white each),
      and the old route's blend of a tenth of a frame rounded to nothing (PT-136).
    - Game evidence, frozen, old route / cells (`20261012-051553`, `-053455`, `-065226`): flicker with no denoiser
      0.40 / 0.36 tavern, 0.99 / 0.46 vista, 1.85 / 0.20 night, 0.33 / 0.17 mist; under NRD 0.40 / 0.18, 0.94 / 0.27,
      1.92 / 0.16, 0.32 / 0.16; light 17.96 / 17.92, 133.51 / 132.61, 54.50 / 53.83, 91.42 / 91.31. Open: PT-137,
      PT-138.
    - Learned there: a per-pixel pass with no history IS the picture's grain where most of the fog lies past the
      cells; raw grain costs light on the way to the screen (2 luma in the vista); read the tracer's own numbers for
      a view (reference radiance, storage units, steps) before blaming an estimator for a percent; a frozen run
      cannot judge what shows only in motion.
    - The 14 timed stages (...): ..., denoising, fog cells, fog walk, fog lamps, composite.
    - Frame flags: ... NRD trial 17-18 (shift 17, mask 3). Free: 19-29 but 22 where still held, 30-31 (19 and 24-29
      were the fog grid's switch, trial number and leave-out until PT-118's rule).
    - DEAD ENDS: the fog cells' light weighed by the medium's scattering (2026-10-12): the same picture in four
      scenes, 8 to 20 % more of the cells' pass. RTX Remix's reservoir reuse and Gaussian filter over the cells: the
      same picture in every FROZEN scene, which cannot judge them (PT-138).

## Trial 2: the light where the fog is (run in the game as trial 3's grid; no change in any scene; OUT of the copy)

Changed against the candidate: `pt_fog_froxel.hlsl`, `pt_fog_integrate.hlsl`, `fog_froxel.inl`, `tracer.cpp`
(`fogrule2-tools\trial2_edit.py` made every change; each landed once).
- The cells' pass draws its four points as before (only those the camera's rays read), asks the medium's scattering
  at each (`pt_medium.hlsli fog_scattering_at`: the height fog's and every box's), takes ONE with a chance in
  proportion to it, traces its light there, reads and feeds the mist's cache there.
- A cell now holds, averaged over its frames: the light times the scattering there over that chance (the cell's
  mean of scattering x light, whatever the four points were), and, in a SECOND volume, the four points' mean
  scattering. The walk divides the one by the other, channel by channel; a cell whose points found no medium counts
  for nothing between cells.
- In a medium that is the same all through a cell (the height fog under its base: all of `fog`) this is the cell's
  mean light, as before: nothing should change there.
- ADDED: one more pair of volumes (121 x 68 x 96, RGBA32F: 25 MB at this size; a smaller format would do), two pass
  constants (the cells' pass.b.x and .y, the walk's pass.a.z). No export, no record, no flag bit, no setting.
- It did not trip the Shader Model 6.9 fault PT-133 (the tavern was right: 18.03 against 17.96).

## The two looks no frozen fog scene gives (run `20261012-064400`, the candidate's DLL; the coordinator ran both)

The recipes: `fogrule2-tools\recipes-before-the-rule.json` (the coordinator pasted them into main's scenarios).
- FAR LAMPS AT NIGHT (`vista-night`: the vista save at 23:00, `fog-ab`). What I feared: past 20 m the grid asks a
  lamp's light right by it (about 24 pixels around its image) only with a chance and weighs it one over that
  chance, with no history: a far lamp's core should sparkle. What the run shows: the grid's change map is calm at
  the fires in view (flicker 0.25 against the old route's 0.69, in the region 0.16 against 0.67). NO SPARKLE SEEN.
  AGAIN WITH 32-BIT TARGETS (trial 5, run `20261012-072503`), so that nothing is rounded away: the grid's change map
  has two faint spots at fires at the left and is calm elsewhere; the old route's has bright specks all over the
  castle and the sky. Flicker old / grid 0.47 / 0.16 (region 0.51 / 0.14), boil 0.26 / 0.05. So far lamps do not
  sparkle to speak of in this view. The light: game 58.64, old 58.48, grid 59.07; in the region with the lamps
  40.04 / 35.88 / 36.90 (with 16-bit targets the old route had 26.7 there: it lost a quarter of that region's light
  to the rounding). The grid is 1.5 to 2 of 255 above the old route all across that region (3 %); I have not
  parted that (the lamps the old route loses, PT-132, or the mist's cache).
  What it cannot say: the save has a handful of fires in view, none close to a pixel's way through thick fog.
- THE SCREEN'S EDGE WHILE TURNING (`fog-edge`: the fog save, its region the screen's right fifth; `fog-turn` swings
  the camera 20 degrees to either side every 4 s). What I feared: a cell that comes into view has no history, its
  first frames are one, two, three samples (Remix gives a fresh cell six and filters the cells). What the run shows
  (`fog-edge\2-froxel-none-turn\atlas.jpg`, 16 frames of the edge): the fog at the edge is smooth in every frame, NO
  BLOCKS. The steps' numbers are NOT comparable with each other: each step caught another part of the swing (the old
  route's 16 frames show another view than the grid's), and the change maps are the turn itself. A fair number
  would need the same frames of the swing in every step.

## Desk check: far cells against the exact integral (CPU; `fogrule2-tools\desk\far_cells.py`)

One ray, a uniform fog, a light that changes along the way; the walk through the 96 slices against the exact
integral, the three scenes' extinctions: a light that falls off over 30 to 300 m x0.9993 to x1.0054; a shadow that
begins at 60 or 150 m x0.9989 to x1.0046; light shafts every 25 m x0.996 to x0.999; a slab lit only from 25 to 30 m
x0.96 to x1.03. A model, not the shader. The game's `fog` scene agrees with it: equal within 0.5 of 255 everywhere.

## What is left to do (in this order, by whoever goes on)

1. THE PINS: the whole check once with the last build that has both routes (`%TEMP%\tgb\fogrule2-t5`: trial 5's DLL
   and the candidate's test exe; `powershell -File %TEMP%\tgb\fogrule-build\run-queue.ps1 -Sets
   "fogrule2-t5|t5-|all:|900"`; it waits for the card by itself and needs seven quiet minutes for its passes). Its
   lines "FOG <case>, pixel: ..." are the pins of the room's cases and of the two daylight fogs with the light
   scattered once, where the grid's line is within 3 % of them; the grid's own lines are the pins of the cases with
   the mist's cache. Then `tests\pathtracing_fog_froxel_pins.inl`, the test exe again, and the check with the rule's
   build.
2. The fog target's format once PT-136's cure is in: one run of the rule's build with that one target as
   `DXGI_FORMAT_R16G16B16A16_FLOAT` (`tracer.cpp`, "Create the traced fog"), the vista by day and by night, against
   the same build as it is: 17 MB to gain; steps in the fog's light to lose wherever the reference is still far
   above what the picture shows.
3. PT-138's points, the cost first (the order of work puts speed first).

## Not done

- The offline check has never reached a frame: its pins are empty, so its room cases and the two daylight fogs are
  printed, not judged; the rule's build has not run it at all. Mutation checks: none (the picture's verdict comes
  first). The whole GPU test and ctest: not a worker's.
- The owner has not seen it.
- Far lamps: looked at twice at night, two faint spots. The turning camera: looked at once, nothing seen, no fair
  number. The vista's far castle and the night vista's 3 %: the part in the light scattered once is not
  explained. The tavern's 1.5 % under NRD: not looked into.
- The cost: not measured in a measuring run; nothing done about it.
