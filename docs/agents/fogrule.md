# fogrule: the fog grid and the old route disagree on the light outdoors; settle it, then the grid is the rule (PT-118, PT-132)

Worker: Claude subagent, copy `%TEMP%\tg-agents\wt\fogrule`, base `base20g`, brief `docs/agents/briefs/brief-fogrule.md`.
Started 2026-10-12 01:17. Everything of mine outside the copy is under `%TEMP%\tgb\fogrule-*` ("Where things are").

## State (last written 03:40, when I handed back BLOCKED)

**Nothing of mine has run on the GPU. No number below is a GPU number, and there is no hand-over.**
**I stopped at 03:40 and handed back as blocked: ten starts in two hours, none reached a frame.**

- FIRST, BECAUSE IT CONCERNS EVERY WORKER: AT 03:28:21 TO 03:28:24 THE TREE COPIES WERE SWEPT. Every folder under
  `%TEMP%\tg-agents\wt`, in the alphabet's order from `base20e` to `vendorrr`, lost every file last written before
  about 2026-10-06 (the oldest file left in mine: 10-06 10:58). `campspeed` alone was not touched; `reccpu` (made
  at 03:06) has its old files at 03:34 and its folders were written again at 03:30:57, I cannot tell why. The
  PRISTINE BASES are among the swept (`base20g`, mine, too: a merge against a base is against a tree with holes). My
  own folders under `%TEMP%\tgb` were swept the same way. The main tree is whole. I do not know who or what did it:
  not my scripts (I read them for it: no delete but a failed header's and a STOP file's; at 03:28 mine were a loop
  that asks which games are open and a shader build waiting for room, neither writing in those folders), and
  Windows' own clean-up tasks last ran on 10-06 by their own records. The disk is at
  98 % (83 GB free).
  - What it took from me: 117 files in each of my three trees (the bridge's and the frame generation's sources,
    old passes such as `pt_cache.hlsl` and `pt_boil.hlsli`, project files, `Build.Local.props`, the SDK links), none
    of them a file I had changed (those were all written today and are all there); the Agility runtime in my test's
    stage (`D3D12Core.dll`, of 09-26); my offline package folders for the plugin build.
  - What I did: put the 117 files back into each tree from the main tree, each only where the main tree's copy was
    last written before my copy was made (all 117 were: the very bytes), written now so they are not old
    (`restore_swept.py`; docs and the SDK links not put back: the builds take the SDKs from the main tree). The
    117 and 118 source checks pass again. `run-fog.ps1` now checks the stage's runtime file by file before every
    start and counts a run on another runtime as no result: a start after 03:28 would have run without DirectX
    Raytracing 1.2. None did (my last start ended at about 03:24).
  - Still broken: my own offline plugin build (the .NET reference packages were swept). The plugin kept aside is
    whole and is byte for byte the one in the copy's `bin`. The coordinator's deploy builds the plugin its own way.

- WHY. The offline test's FIRST start with a build makes the two reordering passes anew in the driver: by the last
  worker's own log 405 s (`specularSer`) and 234 s (`giSer`) once, 3 to 4 s at every start after; over all the workers'
  test logs on this PC 48 of about 200 starts took more than a minute for them, the longest 688 s. So my check needs
  ONE stretch of about seven minutes with no game open before its first frame, and under a minute a case from then
  on. The GPU has not been free for more than three minutes since 01:29:

  | my start | ran | ended by |
  |---|---|---|
  | 01:29 | 7 s | the coordinator's game opening (01:29:42; it ran to 01:59) |
  | 01:59 | 100 s | Starfield opening (02:01:20) |
  | 02:07 | 81 s | Starfield opening (02:08:28) |
  | 02:17 | 186 s | Starfield opening (about 02:20:40) |
  | 02:33 (two tests at once) | 103 s | Starfield opening (about 02:35:45); the coordinator's game ran 02:22 to 02:33 before |
  | 02:43 | 103 s | a game opening |
  | 02:47 | 219 s | a game opening (the coordinator's game is open at 02:58) |
  | about 03:02 | 101 s | a game opening |
  | about 03:17 | 97 s | a game opening |
  | about 03:23 | 79 s | a game opening (the coordinator's game is open at 03:30) |

  (The last two are from the one run folder. The driver's own cache file for the test, 2 GB, was last written at
  02:50:44, the second my 219 s start was ended: whether what it holds of a half-made pass counts toward the next
  start I did not know then. By 03:30 the five starts from the one run folder add up to 599 s with the passes still
  not made, against 405 s for one uninterrupted start in the last worker's log: interrupted starts do not add up.)
  In none of them were the passes made (the test's log never reached "passes made"); no frame was traced beside a
  game: my script ends its test within 2 s of a game's opening, and starts one only when no game is open and the main
  tree's `Run-PtHarness.ps1 -GpuCheck` exits 0. I had also given every kept build a run folder of its own and run two
  at once, which cannot have helped: since 02:45 there is ONE run folder, one exe path and one test at a time.
- WHAT WOULD UNBLOCK IT: about ten minutes with no game open, once. The queue is running and waits by itself
  ("For the coordinator").
- WHAT I DID INSTEAD (CPU only): read the code, wrote three desk checks, built three variants, and corrected my own
  text. At 02:25 I went through what I had written and changed every line, in comments of both trees and here, that
  stated a suspect as a finding or an estimate as a number (`%TEMP%\tgb\fogrule-build\edit_wording.py` is the list).
- PLAINLY, ABOUT SCOPE: what I built is larger than what the brief asked. Asked: which route is wrong where, a fix so
  the two agree, then the grid as the rule. Built, compiled, never run: the cells over the whole way with two more
  rules of their own (the copy), and beyond that a walk that feeds the mist's cache, two cell sizes and quarter
  feeding (the second stage). I started the larger change before I had one measurement, because none could be had;
  that was a choice, and the smallest fix now exists beside it (`fogrule-small`) so the first GPU numbers can decide.
- STATE WHEN STOPPED, if I stop here:
  - THE COPY holds the first hand-over's CANDIDATE: sources that build, 117 source checks pass, a native DLL and a
    plugin kept in `%TEMP%\tgb\fogrule-handover-1`. Untested on the GPU: not a hand-over.
  - THE SECOND STAGE (`%TEMP%\tgb\fogrule-c-src`) is not in the copy. Its C++ and its test compile (in a throwaway
    folder with the wrong passes, never run); its own passes are compiling (7 of 53 done at 02:42); its native DLL
    does not exist yet; 118 source checks pass in its tree; the check's pins are all 0.
  - THE SMALLEST FIX (`%TEMP%\tgb\fogrule-s-src`, one pass changed on the pristine base) is built: `fogrule-small`.
  - Docs (PT-REQUESTS, CHANGELOG, PROJECT-CONTEXT, BACKLOG's line): untouched in every tree. Nothing to write in them
    until there are numbers. A defect entry is owed whatever the numbers say ("Entries owed").
  - NOTHING OF MINE IS RUNNING. At 03:31 I ended the queue myself (it would otherwise go on starting tests in every
    free minute with nobody watching) and the second stage's compile (it held two of the PC's compile places, which
    the work on speed has first call on, for a stage whose direction no number has confirmed). The compile stopped
    at 30 of 53 headers, none half written; it goes on from there with
    `%TEMP%\tgb\fogrule-c-build\run-set.ps1 -Shaders -Native`. The queue: "For the coordinator".
  - The main tree was compiling `pt_gi` again at 03:24 to 03:28: its passes have moved on since my base. The three
    kept builds carry the main tree's passes of 01:20 (but for the fog's).

## What is known, and what each thing rests on

| claim | rests on | not known |
|---|---|---|
| With the grid, past its 20 m of cells, the per-pixel pass reads the mist's cache at points nobody feeds | reading: `pt_fog_tail.hlsl` reads `fog_cache_light` at its point and feeds nothing; the cells' pass feeds at cell points (within 20 m); the old camera pass, which fed along the whole way, does not run with the grid; a cache cell without a sample for 240 frames is given up (`CACHE_MAX_AGE`) and reads 0 | whether this is the cause of the game's 7 % (`vista`) and 24 % (`fog`), wholly or in part. It fits the four scenes of run `20261012-010243` (table below). Not ruled out, with no number: cells sampled behind surfaces, the walk above the fog's base height |
| The comparing check could not see it | reading: its two routes ran one after the other, 160 frames each, without emptying the cache; the game's steps settle 300 frames and more | - |
| The grid's flicker in `fog` (0.70 against 0.33) is the per-pixel pass past the cells | the run's own picture `fog\overview.jpg`: the change maps of the grid's steps have their band over the sky, where the ways are long; that pass has one sample a pixel a frame, no history, no filter (reading) | not measured apart from the rest of the frame |
| The mist's cache is short in BOTH routes: only points inside the camera's view feed it, and it is read all around them | reading (`pt_fog.hlsl` and `pt_fog_froxel.hlsl` feed at camera fog points only; a flight's far point lies anywhere); a CPU model of the feeding in an endless uniform fog (`desk\mist_view.py`, `desk\mist_walk.py`): x0.86, x0.74, x0.56 of the fixed point at albedo 0.3, 0.5, 0.7 | the shader's own number (the check's "open, no ground" case prints it); how much of any game scene's fog light is scattered three times and more (the game's fog has a ground and a top; the model has neither) |
| A walk that goes on from an empty cell cures that | the same model: x1.000, x0.997, x0.975 (one cell size); x1.000, x0.999, x0.993 with two cell sizes, moved reads and a quarter of the feeds (`desk\mist_walk2.py`) | everything about the shader version: it has never run |
| Without coarser cells outside the view the walk would fill a third of the radiance cache's table | my estimate (40,000 fog cells a doubling of distance all round; alive out to a dozen mean free paths): 300,000 to 400,000 of 1,048,576 slots | the table's load in any scene: the tracer does not log it |
| The check's closed form for a lamp is right | CPU: its integral against a sum over the way in half-millimetre steps, 18 cases: x0.9997 to x1.0010 (`desk\lamp_glow.py`) | - |
| The copy's reworded comments changed no code | the six fog headers recompiled: byte for byte the kept ones | - |

The game's constants (run `20261012-010243`, its "Fog grid numbers" lines) and how the suspect fits them:

| scene | extinction, per m | albedo | what lies past 20 m | grid against old route (the game) |
|---|---|---|---|---|
| `tavern-room` | (indoors) | | nothing: every way ends within the cells | equal (17.9 / 17.9) |
| `night` | 0.111, base 67.6, camera 69.7, anisotropy 0.48 | 0.24 | little: 20 m is an optical depth of 2, and the albedo leaves little for a second scattering | 52.1 against 53.1 |
| `vista` | 0.00147, base 128.1, 1 / scale height 0.0101, camera 130.2, 5 fog boxes, anisotropy 0.6 | 1 | nearly all the fog (the way's transmittance 0.975 after the cells, 0.763 at its end) | 121.6 against 130.6: 7 % under |
| `fog` | 0.0141, base 34, 1 / scale height 0.0245, camera 7.2, anisotropy 0 | 1 | three quarters (0.715, then 0.253) | 69.9 against 91.8: 24 % under |

## Three variants, kept aside, none run

Each is a folder under `%TEMP%\tgb` with `TGPathTracing.Native.dll` and the check's `tgpt_gpu_test.exe`.

| folder | what it is | native SHA-256 |
|---|---|---|
| `fogrule-base2` | the copy as it started (the main tree of 01:20): the baseline | `8E5ADEE9BC39203DE5BBAFE345B13328C99338C621A256E18F9645A20B863BD5` |
| `fogrule-small` | THE SMALLEST FIX: the baseline with one change, in `pt_fog_tail.hlsl`: the per-pixel pass past the cells feeds the mist's cache at its point, with the old camera pass's own lines. Tree `fogrule-s-src`, build `fogrule-s-build` (55 of 57 headers are the main tree's) | `941B5AAD44D281C35EC1AECF8F264F2A9789FE0FC775D9EB47B1B66D83355C8E` |
| `fogrule-b2` (= `fogrule-handover-1`) | THE COPY: the cells over the whole way (below). Six fog headers compiled; the other 49 are the main tree's | `3EB0935E741CB202293872EF4E0CA360042FBFEB5431DDE39C9CA821E008105A` |

What the first runs would say (each route from an empty mist cache):
- baseline, the fog scene's fog, the light scattered once (case 11): the two routes together, if the suspect is the
  whole cause; apart, if something else is in it.
- baseline, the same with the mist's cache (case 12): apart as in the game, if the suspect is right.
- the smallest fix, case 12: together, if feeding the cache past the cells is the cure. That alone answers the
  brief's (a) and (b) for the light. It does not touch the grain.
- the copy, all fifteen cases: whether the larger change holds up at all.

## The copy: the cells over the whole way (the first hand-over's candidate)

Against `base20g`. Changed: `CMakeLists.txt`; `native/pathtracing/include/tgpt_api.h`; `native/pathtracing/shaders/`
`pt_froxel.hlsli`, `pt_fog_froxel.hlsl`, `pt_fog_integrate.hlsl`, `pt_fog_tail.hlsl`, `pt_shaders.hpp.in`;
`native/pathtracing/src/` `fog_froxel.inl`, `scene.hpp`, `tracer.cpp`; `src/TGPathTracing/` `Settings.cs`,
`HdrpPasses.cs`, `NativeTracer.cs`; `tests/` `pathtracing_fog_froxel_checks.inl`, `pathtracing_gpu_test.cpp` (one
variable: the check's fog albedo), `ingame/scenarios.json`. Gone: `pt_fog_filter.hlsl`, `pt_fog_reservoir.hlsli`.
New: this file. NOT touched: `pt_medium.hlsli`, `pt_common.hlsli`, `pt_fog.hlsl` (the old route), every other pass.

1. `pt_froxel.hlsli`: 48 slices to 20 m as before (spaced by an exponent of 2), and 48 more from 20 m to the far
   distance (`g.params.y`: 1,000 km in the game, 500 m in the check's room), each deeper than the one before by one
   factor (1.25 in the game). The walk's last slice goes on to the way's own end.
2. `pt_fog_froxel.hlsl`: a cell takes its sample only at a point the camera's rays read (not behind the surface its
   own pixel shows, not over the sky past HDRP's Max Fog Distance): the last worker's trial "leave out 6", as the rule.
   Up to four points are drawn until one is read. A cell keeps what it has when none is; a cell never seen has no
   light and counts for nothing between cells, in the walk and in the history's own fetch (eight loads, counted, where
   one filtered read was). Trials 1 and 2 and every TEMP DIAG line are out. The sun-and-sky shape PT-133 asks for is
   kept, with its comment.
3. `pt_fog_integrate.hlsl`: one walk through all 96 slices (the fast one); trial 1's walk is out.
4. `pt_fog_tail.hlsl`: the lamps' light right by them (`fog_lamps_residual`) over the whole way, and nothing else: no
   point past the cells, no `fog_lamps_along` (so PT-132's loss is not in the camera's fog any more; it stays in the
   fog on reflected and refracted rays' ways). The stretches within the first 20 m are all asked, as before; past them
   a stretch is asked with a chance (what the height fog lets through times 20 m over its distance, a 64th at least)
   and its lamps weigh one over that chance. Its loop's fixed 64 stretches is now the most cells a straight way can
   cross.
5. `fog_froxel.inl`, `tracer.cpp`, `scene.hpp`: the volumes are 96 deep; the read-back, its log line, the probe
   volume, the reservoirs, the filtered volume, the filter's pipeline and `GpuScene::diag_light` are out.
6. The plugin: `Traced fog: froxel cache trial` and `Traced fog: froxel cache leave out` are out. The hidden
   `Traced fog: froxel cache` STAYS (the old route stays behind it for the comparison).
7. `tests/ingame/scenarios.json`: `fog-froxel`, `fog-froxel-b` and `fog-froxel-numbers` are one recipe, `fog-froxel-b`:
   0-game, 1-pixel-none, 2-froxel-none, 3-pixel-once-none, 4-froxel-once-none (both routes with `Traced fog: light
   scattered once only`), 5-pixel-nrd, 6-froxel-nrd, 7-off.

Added or freed (the brief asks for each): FREED frame flag bits 24 to 29 (`TGPT_FRAME_FOG_FROXEL_TRIAL_SHIFT/_MASK`,
`TGPT_FRAME_FOG_FROXEL_DIAG_SHIFT/_MASK`), two hidden settings. ADDED: nothing (no export, record, FrameConstants row,
option or flag bit). The stage "fog filter" still has its name and reads 0.

Departures from RTX Remix in it, named (PT-127's rule): the cells reach the far distance, not a set reach; a cell's
sample only where the camera's rays read; no temporal reservoirs, no extra samples at a fresh cell, no Gaussian filter
(the coordinator's runs of 2026-10-12 showed the same picture with and without them); no firefly clamp; a lamp's light
split between the cells and each pixel; a cell's point over the whole cell. What Remix does past its cells I do not
know: its sources are not in this repo.

## The second stage (its own tree, `%TEMP%\tgb\fogrule-c-src`; not in the copy)

Against the copy. Changed: `CMakeLists.txt`, `tgpt_api.h`, `pt_compose.hlsl` (two comments), `pt_fog_froxel.hlsl`,
`pt_medium.hlsli`, `pt_shaders.hpp.in`, `fog_froxel.inl`, `tracer.cpp`, the plugin's three files,
`tests/ingame/scenarios.json`, `tests/pathtracing_fog_froxel_checks.inl`, `tests/source_checks.py`. Gone:
`pt_fog.hlsl`. New: `tests/pathtracing_fog_froxel_pins.inl`.

- The grid is the rule: the old camera route (`pt_fog.hlsl`, its pipeline, its two history targets), the hidden
  `Traced fog: froxel cache`, frame flag bit 19 (FREED), the stage "fog filter" (the stages are "fog cells", "fog
  walk", "fog lamps": 14, were 15), the recipe `fog-froxel-b`.
- `pt_medium.hlsli`, which EVERY pass includes (all 53 headers compile again):
  - `fog_cache_level`, new `fog_in_view`: a mist cell is four times a surface's cell inside the camera's view, sixteen
    times outside it.
  - new `fog_cache_place`; `fog_cache_light` asks the cell that holds the place it was moved to, at that place's own
    size (it asked at the unmoved point's size; across a step in size that can name a cell nobody feeds).
  - `fog_lamps_along` without `sampleT` (the old camera pass's); its comment now says what it loses (PT-132).
- `pt_fog_froxel.hlsl`: `froxel_feed`, `froxel_mist`: the mist's cache fed along a walk (on from a cell that holds
  nothing; on with one chance in four from one that holds light; every point feeds its own cell; eight points at
  most); a quarter of the cells feed each frame (`cache_feeds`, whole 8 x 8 groups).
- The check: one route; the open cases against what is known exactly, the others against pins
  (`pathtracing_fog_froxel_pins.inl`: all 0 today, so those cases are printed, not judged).
- A source check, `test_path_tracing_fog_is_traced_in_cells`.

ADDED there: nothing in the API. FREED: frame flag bit 19, one hidden setting.

## The check (`TGPT_TEST_FOG_FROXEL_ONLY`)

Fifteen cases (`TGPT_TEST_FOG_CASE=<n>` runs one): 0 to 6 the room (no lamp; a lamp 2 m, 0.3 m, 0.02 m ahead; a candle-like
quad; no sun with a lamp; no sun with the quad), 7 open with sun and sky, 8 and 9 open with one lamp 6 m and 60 m
ahead, 10 open without a ground with the mist's cache, 11 and 12 the fog scene's fog without and with the cache, 13
and 14 the vista's. New in it:
- The outdoor camera, a ground 4 km wide, the game's Max Fog Distance and far distance, the two scenes' own fog.
- What is known exactly, computed on the CPU from the shaders' own formulas, and BOTH routes judged against it: the
  light scattered once from an unshadowed sun and a uniform sky in a uniform fog (case 7); a lamp's light in a uniform
  fog along a way (8, 9); the mist's fixed point in an endless uniform fog, C = a D / (1 - a) (10: printed, not judged,
  in the copy's build, where both routes feed in view only).
- Every route from an EMPTY mist cache (one frame without the cache empties it).
- A region's grain: how far a pixel strays from its own mean over the 16 frames.

## For the coordinator

TO UNBLOCK: about ten minutes with no game open. I ended the queue when I handed back; to start it again (it waits for the GPU by itself, runs one test at a time, ends its test within 2 s of a
game's opening and runs that case again later; `%TEMP%\tgb\fogrule-build\STOP` ends it):

    powershell -File %TEMP%\tgb\fogrule-build\run-queue.ps1 -Sets "fogrule-base2|a-|c12:FOG_CASE=12;c11:FOG_CASE=11|420 # fogrule-small|s-|c12:FOG_CASE=12|420 # fogrule-b2|h1-|c07:FOG_CASE=7;c12:FOG_CASE=12;c11:FOG_CASE=11;c14:FOG_CASE=14;c13:FOG_CASE=13;c08:FOG_CASE=8;c09:FOG_CASE=9;c10:FOG_CASE=10|420"

Results: `python %TEMP%\tgb\fogrule-build\collect.py a- s- h1-` (one line a case and route, with the verdict lines);
each run's log and pictures in `%TEMP%\tgb\fogrule-out\<tag>`.

THE FIRST HAND-OVER, once the copy's fifteen cases have passed (until then this is a plan):
- Files: `%TEMP%\tgb\fogrule-handover-1\TGPathTracing.Native.dll` (`3EB0935E...`, above) and `TGPathTracing.dll`
  (Mono, net472), SHA-256 `7FDE5D98772B287A4FCE5714F3B83BC9E203394922359C0C8B76D7F780F5E5EA`.
- Install (game closed), from the copy:
  `tools\harness\Deploy-PtBuild.ps1 -Native -NativeFrom %TEMP%\tgb\fogrule-handover-1\TGPathTracing.Native.dll`
  (it builds the plugin from the copy; the plugin IS needed: the recipe sets no trial or "leave out" key any more).
  The copy's own build folder holds a relinked DLL (`54CEF6D0...`): the same code, never the one to install.
- Run `fog-froxel-b` of the COPY's `tests\ingame\scenarios.json` on `vista`, `fog`, `night`, `tavern-room`.
- What its steps tell, whatever comes out: 3 against 4 is the two routes with the mist's cache out of the question
  (if they differ, the cause is not the cache); 1 against 2 and 5 against 6 were 7 % and 24 % apart; 1 less 3 is how
  much of a scene's fog light is scattered more than once (not known for any scene); step 2's change map in `fog`
  shows whether the band over the sky is gone.

IF THE COPY DOES NOT HOLD UP: `fogrule-small` is a hand-over that changes one pass. Install its DLL alone from the
main tree (`Deploy-PtBuild.ps1 -Native -NativeFrom %TEMP%\tgb\fogrule-small\TGPathTracing.Native.dll`); the main
tree's own `fog-froxel-b` runs with it unchanged.

## Entries owed (not written: `docs/PT-REQUESTS.md` is untouched in every tree)

- A new entry (I called it PT-134 in comments of the second stage's tree; the number is yours to give): the mist's
  cache is fed only at points the camera sees and read all around them. By reading and a CPU model; no GPU number.
- PT-118: the outdoor disagreement's suspected cause, and the check's blind spot (the cache carried from one route to
  the next).
- PT-132: closed for the camera's fog in the copy's build if it holds up; open for reflected and refracted rays.
- Found on the way, small, unmeasured: `fog_cache_light` read the cell at the unmoved point's size.
- A gap already named in `pt_medium.hlsli`, not mine: the light surfaces send into the fog is 0 in the mist's cache.

## Not done

- Any GPU run. Mutation checks. The whole GPU test and ctest (not a worker's).
- The second stage's native build; its pins; bringing it into the copy.
- Docs: PT-REQUESTS, CHANGELOG, PROJECT-CONTEXT 9.12's flag bits.
- Not measured and worth a look when frames exist: the cells' pass's time with 96 slices; the far lamps' glow, which
  the per-pixel share now samples with a chance and without history (grain within a cell's width of a far lamp); the
  one slow frame after a cache reset in the second stage (walks of up to eight points while every cell is empty).

## Where things are (all under `%TEMP%\tgb`)

- `fogrule-build`: the copy's build folder and every script (`run-fog.ps1`, `run-set.ps1`, `run-queue.ps1`,
  `collect.py`, `ratio.py`, the `edit_*.py` that made each scripted change, `desk\` the three desk checks, `keep\`
  earlier versions of files).
- `fogrule-c-src`, `fogrule-c-build`: the second stage's tree and build. `fogrule-cc-build`: a compile check of its
  C++ with the first stage's passes in place: NEVER to be run (a note in it says so).
- `fogrule-s-src`, `fogrule-s-build`: the smallest fix.
- `fogrule-base2`, `fogrule-small`, `fogrule-b2`, `fogrule-handover-1`: builds kept aside. `fogrule-base`,
  `fogrule-b1`: earlier ones, superseded.
- `fogrule-run`, `fogrule-stage`: the one run folder and stage. `fogrule-cache`: compiled passes, shared by every run.
  `fogrule-out\<tag>`: each run's log and pictures. `fogrule-tmp`: temporary files.

## How it was built (isolation)

- Configure: `configure.cmd` in each build folder (NRD from the main tree's prebuilt `NRD.lib` and
  `ShaderMakeBlob.lib`, read only; no SDK configure, nothing of an SDK written). Mega Geometry and rain wetness ON,
  lamp-around, regroup-water and speed-measure OFF, as main's build.
- Shaders: `tools\Build-PtShaders.ps1 -BuildDir <the build folder>` alone, never `-First`, its limits untouched. Reused
  from the main tree where sources and dxc arguments are the same (`reuse_headers.py`).
- GPU: `run-fog.ps1` alone, as described under "State". Only `TGPT_TEST_FOG_FROXEL_ONLY`.
- Never started the game, deployed, committed, or wrote to the main tree or the game folder. Read from the main tree:
  its SDK folders, its prebuilt libraries and headers, its harness run `20261012-010243`, other workers' test logs
  under `%TEMP%\tgb` (for the passes' start-up times only).
