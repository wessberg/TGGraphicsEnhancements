# reuse-shift: the passes that reuse last frame's data and the camera's sub-pixel shift (PT-84, PT-85)

Worker `reuse`, 2026-10-08 to 10-09, copy `wt\reuse`, base `wt\base59`. Brief: `docs/agents/briefs/brief-reuse-shift.md`.

## 0. HAND-OVER FOR THE GAME TRIAL (the coordinator's order of 23:55: the game first)

**The switch: the hidden setting `Advanced / Reuse follows camera shift` (an integer, default 0 = off).**
- `0`: as before. Every fetch of the frame before is by the pixel's motion alone.
- `1`: THE TRIAL, the fix as measured. The tracer's OWN HISTORIES (direct light, reflections, bounced light, NRD's
  kept direction) are fetched where the pixel's point was in the previous frame's own pixel grid (the motion plus
  this frame's shift less the previous one's), with a second try (the nine texels round the place when none of the
  four showed the pixel's surface). ReSTIR's reuse keeps its fetch as it was: section 6 says why.
- `2`: for finding causes only. ReSTIR's reuse across frames (direct and bounced) follows the shifts as well.
- It needs `Advanced / Camera shift` = true (the main tree's setting): with the shift off there is no difference
  to add, and 0, 1 and 2 are the same picture.
- The native log says once which it is: "Reuse across frames: the tracer's own histories are fetched in the previous
  frame's own pixel grid (PT-85 on trial ...)", or "... by the motion alone (the trial of PT-85 is off)".

**How it travels (no ABI change, no struct size, no option bit, no export):**
- plugin to tracer: `TGPT_Frame.jitter[2]` (was unused; 0 from every older caller);
- tracer to shaders: `FrameConstants.look.y, .z` (the two shifts' difference, pixels; 0 while off) and `look.w`
  (0 off; 2 for the setting's 1, the histories alone; 1 for the setting's 2, everything: the shaders were compiled
  before the measurement that swapped the two, `tracer.cpp` maps them).
  THIS JOB TOOK `look.yzw` (they were "unused"; `sizeof(FrameConstants)` stays 7920) and `jitter[2]`: tell other
  workers. (The `water8` copy changes the struct's size but not `look`.)

**Files changed** (against base59):
- `native/pathtracing/src/camera_shift.hpp`: `held_camera_shift` (the shift a frame's matrices hold), split out of
  `ensure_camera_shift`, which behaves as before.
- `native/pathtracing/src/tracer.cpp`: the last frame's shift kept (`st.cameraShift`), `look.yzw` filled, one log line.
- `native/pathtracing/include/tgpt_api.h`: a comment (`jitter`'s z).
- `native/pathtracing/shaders/pt_common.hlsli`: `previous_place(p, motion)`, the one place the previous place is made.
- `native/pathtracing/shaders/pt_rtxdi.hlsli`: `rtxdi_motion` (ReSTIR, direct and bounced) uses it at the setting's 2 only.
- `native/pathtracing/shaders/pt_temporal.hlsl`: the history fetch uses it (a mirror's picture too); the second try.
- `native/pathtracing/shaders/pt_nrd_prep.hlsl`: NRD's kept direction uses it (one line; compiled by hand with NRD's
  headers into `%TEMP%\tgb\reuse-nrdprep`: it compiles; NOT run, this build has no NRD).
- `src/TGPathTracing/Settings.cs`, `HdrpPasses.cs`: the hidden setting, and `frame.Jitter[2]`. Both edits sit away
  from the lines the main tree changed for `Camera shift` (a new field line, a new bind after "Bounced light
  history", a new line after `frame.Forward[3]`), so they should merge without a conflict.
- `tests/pathtracing_reuse_checks.inl` (new), `tests/pathtracing_gpu_test.cpp` (its include, `reuseTrial`).

**For the harness:** a recipe step that sets `Advanced / Reuse follows camera shift` to 1 (and one at 0) with
`Camera shift` on, `tavern-room`, the debug views as in `shift`/`fireflies`.

**THE SWITCH MEASURED OFFLINE, one build, one run (00:14; the tracer `CD3ADDEE0CC77696`, log
`%TEMP%\tgb\reuse-out-trial\gpu.log`; all six of the check's conditions pass):**

| the Direct layer, a still scene | specks a frame | jumps a frame | the light on the pixel grid (x, y) | change inside flat surfaces |
|---|---|---|---|---|
| no shift | 4.3 | 257 | - | 0.534 |
| the shift, the switch 0 (today's tracer) | **120.9** | **2,371** | x1.26, x2.11 | 0.544 |
| the shift, the switch 1 | **93.6** | **759** | x1.03, x0.95 | 0.529 |
| the shift, the switch 2 | 94.9 | 758 | x1.03, x0.94 | 0.613 |

The Specular layer: specks 11.4 to 5.2 (0.3 without a shift), jumps 312 to 179 (55). The mean light is the same in
every row (Direct 0.18102, 0.18091, 0.18095). By how wide a thing is (of a thousand pixels that show it, how many
jump from one round to the next):

| the Direct layer, by kind of thing | the switch 0 | the switch 1 | no shift |
|---|---|---|---|
| the floor, the ball, the table top | 0.04 to 0.34 | 0.00 | 0.00 |
| a flat bar or an upright slat 4 pixels wide | 8 and 46 | 0.0 and 0.0 | 0.0 |
| the same, 2 pixels wide | 39 and 252 | 0.0 and 0.2 | 0.0 and 1.3 |
| the same, 1 pixel wide | 149 and 483 | **8 and 11** | 5 and 22 |
| the same, HALF a pixel wide | 339 and 520 | **276 and 485** | 81 and 61 |
| a lattice of 1-pixel bars | 79 | 26 | 0.5 |
| a square post (one face under a pixel wide) | 334 | 123 | 47 |

**So: everything a pixel wide and wider is cured, to the level of no shift at all. What is thinner than a pixel is
not, and cannot be by where the history is fetched (section 5).**

**What I expect the game to show. Please read this before judging the run:**
- The Direct layer's JUMPS should fall by more than half, and the light's own stairs go (shadows and highlights no
  longer sit on the render pixel grid).
- The Direct layer's SPECKS fall as far as the tavern's thin things are a pixel wide and wider (chair slats,
  railings, shelf edges: yes; a rug's fringe, hair, far lattices: no). NOT all the way to the shift-off column.
  What stays is the level a MOVING camera always had on things thinner than a pixel.
- Boiling inside flat surfaces should not rise with 1 (0.529 levels a round against 0.544 with 0). With 2 it rises
  (0.613): that is why 2 is not the trial (section 6).
- "Then the boiling filter's neighbourhood, the a-trous feedback" (the coordinator's list): neither reads the
  frame before by a pixel's place (table below, rows 9 and 10). There is nothing to move there.

**Built and not run: section 8 and the state log at the end (kept current).**

## 1. The rule, in plain words

- A pixel's centre in frame N looks at the point that lies `+jitter(N)` render pixels from it in the picture
  without a shift (`camera_shift.hpp`; x to the right, y down the render target's rows).
- The motion the tracer keeps for a pixel (`motion_at`, `pt_common.hlsli`) is HDRP's kind: this frame's place less
  the previous one's, BOTH WITHOUT the shift (`pt_primary.hlsli point_motion`: `motionViewProj` and `prevViewProj`
  are the two unshifted ones).
- So the point a pixel `p` shows in frame N sat, in frame N-1's own grid, at
  `p + 0.5 - motion * size + (jitter(N) - jitter(N-1))` (pixels).
  A pass that leaves out the last term looks up to one render pixel beside the point.
- Before 18:41 the rays had no shift, so the term was 0 and every fetch was exact in a still world.
- This is how NVIDIA's RTXDI sample hands its temporal pass its motion vectors (the two frames' pixel offsets are in
  them), and how SVGF fetches its history (the four texels round the place, each checked; a second try in 3 x 3).

## 2. Every pass that reads the frame before by a pixel's place

| # | Pass | Where the previous place is made (base59) | What it fetches there | Followed the two shifts? | Now, with the switch at 1 |
|---|---|---|---|---|---|
| 1 | ReSTIR, direct light, across frames | `pt_di.hlsl:65` through `rtxdi_motion`, `pt_rtxdi.hlsli:244` | the previous reservoir and surface, nearest pixel; RTXDI then tries 8 more places within 2 pixels | No | left as it was (section 6); moved only at the setting's 2 |
| 2 | ReSTIR, bounced light, across frames | `pt_gi.hlsl:103`, the same `rtxdi_motion` | the same | No | the same |
| 3 | The tracer's own history, direct light | `pt_temporal.hlsl:59` (`tracer.cpp:2413`) | history and moments, the 4 texels round the place, each checked against its own previous surface | No: in a still world exactly ONE texel, the pixel's own | yes, and a second try |
| 4 | The same, reflections | `pt_temporal.hlsl:59` (`tracer.cpp:2418`); a mirror's picture `:55` | the same; for a mirror the place of the SHOWN point | No | yes, both |
| 5 | The same, bounced light (built-in denoiser; the debug views use it) | `pt_temporal.hlsl:59` (`tracer.cpp:2401`) | the same | No | yes |
| 6 | NRD's prep, the normal map's share | `pt_nrd_prep.hlsl:132` | last frame's filtered direction, one nearest texel | No | yes (not run: no NRD here) |
| 7 | NRD's own inputs | `pt_nrd_prep.hlsl:122`, `tracer.cpp:2480` | NRD does its own fetch | Yes by its contract (motion without the shift, both shifts told) | left |
| 8 | AMD Ray Regeneration's guides | `pt_rrg_guides.hlsl:38`, `tracer.cpp:2436` | the SDK's own | Yes by its contract | left |
| 9 | The a-trous feedback | `pt_atrous.hlsl:73` | writes THIS frame's history at the pixel itself | reads nothing of the frame before by place | nothing to do |
| 10 | The boiling filters | `pt_boil.hlsli`, `pt_di.hlsl:73`, `pt_gi_boil.hlsl` | this frame's 8x8 group | no previous frame | nothing to do |
| 11 | The radiance cache, the wet store, the caustics | `pt_cache.hlsl`, `pt_wet.hlsl`, `pt_caustics.hlsl` | cells of the world | not keyed by a pixel | nothing to do |
| 12 | The composite | `d3d11.hlsl` | each pixel's own texel of this frame | no previous frame since 18:41 | nothing to do |

Six fetches were off (1 to 6), all by the same amount: `jitter(N) - jitter(N-1)`, up to a render pixel each way.
By how much each matters (offline, sections 4 and 6): rows 3 to 5 are all of it; rows 1 and 2 nothing at the edges,
and moving them made flat surfaces boil more, so they stay as they were.

## 3. What the game says

- The coordinator's run `20261008-235410` (`tavern-room`, recipe `shift`, only `Camera shift` flipped): the Direct
  view 63.8 specks a frame with the shift, 9.8 without; jumps 1,393 and 221; the means the same. The lead is right.
- Where the specks sit (`scratchpad\reuse-work\specks_where.py` over the kept frames of `20261008-182327` and
  `20261008-224604`): 96 to 99 % of the Direct layer's specks are where the layer's own usual picture steps by more
  than 20 levels within 2 render pixels (25 % of all pixels are such): chair slats, railings, shelves' edges, the
  seated person, a rug's fringe. Thin things and outlines.

## 4. The same offline, and whose part it is

The check: `tests/pathtracing_reuse_checks.inl`, alone with `TGPT_TEST_REUSE_ONLY=1`. A still scene, the tracer's
own camera rays, the game's settings (ReSTIR history 4 and 8 frames, the built-in denoiser, which is what the game's
debug layers show): a rough floor, a glossy ball, a glossy table top, thin bars, a lattice of one-pixel bars, a row
of square posts; four lamps near and, as in the tavern, most lamps far (8 on the far list that light the scene, 200
behind a wall). 16 shifts (a pixel's quarters) in turn, 6 rounds of them after 2.

HOW IT COUNTS, AND A FIRST WAY THAT WAS WRONG. My first numbers compared a frame with the frame before. Without an
upscaler that counts a thin thing as a speck every time the shift moves it onto another pixel, fix or no fix. So a
frame is compared ONLY with frames of the SAME shift, one round apart: under the same shift every pixel looks at the
very same point, and what differs is the light's own noise. The control (no shift) is counted the very same way.
Levels are 8-bit levels of the layer as a picture, the Direct layer's mean at level 130 as in the game.
- speck: a pixel 40 levels over its usual (the median of its shift's six frames);
- jump: a pixel more than 20 levels from what it was a round before;
- "edges": the 3.3 % of pixels where the shift changes which surface the pixel or a neighbour shows; "inside": the rest.

Numbers so far are of three separate builds (before the switch existed): the base, step 1 (the place alone), step 2
(with the second try). Logs: `%TEMP%\tgb\reuse-out-base`, `reuse-out-fix1`, `reuse-out-fix2` (`gpu.log`).

| the Direct layer, as the game shows it | specks a frame | jumps a frame | change at edges (levels) | the light on the grid (x, y) |
|---|---|---|---|---|
| no shift (the control; the same in all three builds) | 4.3 | 236 | 1.94 | - |
| the shift, the base | **110.1** | **1,860** | 5.07 | x1.26, x2.13 |
| the shift, step 1 (the place) | 93.0 | 752 | 3.03 | x1.04, x0.94 |
| the shift, step 2 (and the second try) = the switch at 2 | 87.4 | 684 | 2.91 | x1.04, x0.94 |
| the shift, step 2 with ReSTIR's fetch left as it was = THE SWITCH AT 1 | **85.9** | **674** | 2.82 | x1.04, x0.95 |

| the Specular layer | specks a frame | jumps a frame | the light on the grid |
|---|---|---|---|
| no shift | 0.3 | 62 | - |
| the shift, the base | 11.2 | 301 | x1.92, x2.35 |
| the shift, step 2 | 6.4 | 191 | x1.03, x0.94 |
| the shift, the switch at 1 | 6.5 | 188 | x1.02, x0.95 |

(The bounced light's layer: specks 14.7 to 10.7, jumps 479 to 386; 0.0 and 0.4 without a shift.)

- All of it is on "edges" pixels. Inside: 0.0 specks, 0 to 1 jumps, in every build.
- The mean does not move: Direct 0.18097 (base), 0.18087 (step 2), 0.18092 (no shift); Specular 0.00458, 0.00463, 0.00459.
- Sharpness of the light's own picture ("detail") 0.185 to 0.182: the history fetched between pixels does not blur it.
- "The light on the grid": the 16 shifts' frames laid out 4 x 4 to a pixel, on inside pixels, the step across a
  pixel's border over the step inside a pixel. On the base the denoised light stayed on the pixel grid while the
  surfaces moved with the shift: shadows and highlights were stairs of render pixels for the upscaler, as the
  surfaces' edges were before PT-84. With the fix: 1.0.
- Whose part it is (the base, the Direct layer):
  - ReSTIR alone (no denoiser, its raw samples): specks at edges 1,031 with the shift, 1,002 without; jumps 12,576
    and 12,704. **No difference.** ReSTIR's own search (8 more tries within 2 pixels) finds the surface.
  - The tracer's own history alone (ReSTIR not across frames): **179.5 specks against 1.6**, jumps 1,965 against 160.
- Why: in a still world `pt_temporal.hlsl:59` read exactly ONE texel, the pixel's own, and asked whether the previous
  frame's surface THERE is this pixel's surface. Under another shift it often is not (a post one frame, the floor the
  next). The history is refused, its length starts at 1, and the pixel shows this frame's raw sample.
- Matrices handed without the shift (the game's old way) give the same numbers.

## 5. What stays, and why (the honest part)

**The fix brings a still world under the shift to the level a moving camera has always had. Not to the level of a
still world without a shift.** The camera moving 0.4 pixels a frame (to one side and back, so that rounds compare):

| the Direct layer, the camera moving | specks a frame, no shift | with the shift | jumps a frame, no shift | with the shift |
|---|---|---|---|---|
| the base | 93.4 | 89.2 | 724 | 970 |
| step 2 | 90.0 | 70.0 | 662 | 638 |

So about 90 specks and 650 to 720 jumps a frame is what this scene shows whenever a fetch lands between pixels, with
or without any shift, before and after 18:41. A still camera used to be the exception (4 and 236), because every
frame looked at the very same points. With the shift in the rays there is no still camera any more.

Where the rest sits: 1,968 of the 2,056 pixels that ever show a speck with step 2 are on the posts, on ONE of their
two faces: the one seen edge-on, 0.5 to 0.95 pixels wide, lit only by far lamps (a raw sample there is zero in most
frames and very bright in a few). The reason is not the fetch:
- a thing thinner than a pixel is not drawn by ANY pixel in many frames. All pixels share one shift, so a thin
  upright strip is hit along its whole length or not at all. HDRP's shifts step about half a pixel sideways from
  frame to frame, the worst case for a strip half a pixel wide: hit, missed, hit, missed;
- the tracer keeps ONE frame of history, by pixel. A frame that did not draw the strip keeps nothing of it. The next
  frame that draws it has nothing to fetch, wherever it looks. It starts at length 1 and shows its raw sample;
- and a strip has no neighbours of its own surface beside it for the spatial filter either.
This is a limit of one screen-space history under shifted rays, in every denoiser of this kind. The second try
(3 x 3) recovers only a little (specks 93 to 87, jumps 752 to 684): what it finds was itself found a frame ago.

What would lower it further (none built; each is a design to weigh, not a tweak):
1. **Fewer rare bright raw samples** (the `glints` job's subject: candidates chosen for the pixel's own surface).
   A strip with a calm raw sample needs no history. That job was taken out at 21:00 because the game's specks rose
   with it; that run had this very defect in it, so it may deserve a second look once this one is settled.
2. **History that outlives a frame in which the thing was not drawn**: keep the frame before last as well (its
   surfaces and histories, a third set of textures, about 200 MB at 1924 x 1084), and fetch there when the last
   frame has nothing. With HDRP's shifts the frame before last is hit when this one is, most of the time.
3. A history kept by place in the world for the direct light (as the radiance cache is for bounced light).

## 6. ReSTIR's fetch stays as it was (measured, 00:04)

Moving ReSTIR's fetch too (rows 1 and 2 of the table) is what the brief asked for and what NVIDIA's sample does. It
was built and measured (step 1 and step 2 had it), then measured against the same build with ReSTIR's fetch left
alone (`reuse-keep\expY`, log `%TEMP%\tgb\reuse-out-expY\gpu.log`):

| the Direct layer, the shift on | specks a frame | jumps a frame | change INSIDE flat surfaces (levels a round) |
|---|---|---|---|
| the base | 110.1 | 1,860 | 0.540 |
| histories follow the shifts, ReSTIR's fetch too (the setting's 2) | 87.4 | 684 | **0.622** |
| histories follow the shifts, ReSTIR's fetch as before (the setting's 1) | 85.9 | 674 | **0.521** |
| no shift | 4.3 | 236 | 0.532 |

- At the edges it gains nothing (87 against 86 specks, 684 against 674 jumps), as ReSTIR's raw samples already said
  (section 4: the same with and without the shift; its own search of 8 more places finds its surface).
- Inside flat surfaces the denoised direct light changed 19 % more from round to round with it (the Specular layer
  12 %: 0.173 against 0.154). The likely reason: a pixel's light choice then stays with one point for longer, and the
  16-frame history averages choices that are less independent. Small slow noise, the kind the owner calls boiling.
- ReSTIR is unbiased with either fetch. So it keeps the one it had. The setting's 2 is there to show this in the
  game if wanted; it is not the trial.

## 7. Next (after the game's answer)

- If the game agrees: the switch goes, the fix stays on (the setting's 1), ReSTIR's `rtxdi_motion` goes back to
  its plain form. Then: the offline check's limits
  (its per-kind table is written, not yet run), a source check that pins `previous_place` in the three shaders,
  `docs/TESTING.md`, the CHANGELOG entry, the whole GPU test by the coordinator (older checks that hand a shift on
  the G-buffer route, "history 1, jittered" and "night ... jittered", change with it: for the better if this is right).
- If the game's specks do not fall enough: section 5's list. I would take 1 first, then 2.
- For the coordinator's files: a method trap (a check that compares one frame with the next cannot be used under
  the shift without an upscaler: compare frames of the same shift); and "with the shift in the rays a still camera
  is no longer the easy case: judge noise against the moving camera's level".

## 8. Builds, commands, state

- Native: `%TEMP%\tgb\reuse` (NMake Release, `-DTGPT_WITH_NRD=OFF`); scripts in `scratchpad\reuse-work`
  (`reuse-build.cmd`, `reuse-test-build.cmd`, `reuse-gpu.ps1`: the gate and one focused check).
- Kept DLLs: `%TEMP%\tgb\reuse-keep\base` (`A057962160C970C8`), `fix1` (`8B06DC2E082DA743`), `fix2`
  (`764F9510355B32F2`), `expY` (step 2 with ReSTIR's fetch as before, `B0A6B5072A7EDB3A`), `trial` (the switch,
  `CD3ADDEE0CC77696`: what the tree builds now).
- The check: `powershell -File <scratch>\reuse-work\reuse-gpu.ps1 -Name <tag> -NativeDll <dll>` (it waits for the
  main tree's `Run-PtHarness.ps1 -GpuCheck`, then `TGPT_TEST_REUSE_ONLY=1`, about 90 s once the passes are made);
  in the merged tree: `set TGPT_TEST_REUSE_ONLY=1` and `build\native\tgpt_gpu_test.exe artifacts\Native <images>`.
- The edges check (`TGPT_TEST_EDGES_ONLY`) runs before mine in the same exe: it passed on step 1 and step 2 (0 wrong
  pixel samples), so `camera_shift.hpp`'s split changed nothing there.
- Plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472
  -p:DeployToGame=false -o %TEMP%\tgb\reuse-plugin`: 0 errors.
- Not run: the whole GPU test, ctest, `tgup_gpu_smoke_with_pt`, NRD, the game. The state log says what else.

## Workarounds met

None removed (this job adds a fetch rule). Met and left:
- `pt_temporal.hlsl`'s surface test for a history texel is a plane distance and a normal: two faces of one thin post
  count as two surfaces. Right as it is.
- The offline test's older checks build their own shifted matrices on the G-buffer route; they never compared frames
  of the same shift (the coordinator's, in the whole test).

## State (newest last)

- 23:10 Read the brief, edges.md, AGENTS.md, AGENT-WORKERS.md. Table of passes written.
- 23:12 The base built; the fixture written and run on it.
- 23:20 Step 1 built. My first way of counting was wrong (section 4); the fixture now compares frames of the same shift.
- 23:36 Step 2 built and measured: section 4's table. The rest is things thinner than a pixel: section 5.
- 23:55 The coordinator: the game first. The fix put behind the hidden setting (section 0); the fixture measures the
  switch off, on and at 2 from one build. The plugin compiles. Native building.
- 00:04 Experiment Y measured: ReSTIR's fetch gains nothing at the edges and makes flat surfaces boil 19 % more
  (section 6). So the setting's 1 moves the tracer's own histories only; 2 moves ReSTIR's too.
- 00:09 THE SWITCH IS BUILT: native `%TEMP%\tgb\reuse` (the tracer `CD3ADDEE0CC77696`, kept in
  `reuse-keep\trial`), plugin `%TEMP%\tgb\reuse-plugin` (0 errors). Source checks: 97 pass (96 and the new
  `test_path_tracing_reuse_follows_the_shift_on_trial`). `pt_nrd_prep.hlsl` compiles by hand with NRD's headers.
  Ready to merge: 10 files changed, 2 new (`tests/pathtracing_reuse_checks.inl`, this report); the main tree has
  changed three of them since base59 (`Settings.cs`, `HdrpPasses.cs`, `source_checks.py`), each at other lines.
  The offline check on this build (switch off, on, 2): started, see the next line.
- 00:14 THE SWITCH RUN OFFLINE, one build: section 0's two tables. The check passes (6 conditions), the edges
  check before it too. With the camera moving 0.4 pixels a frame the Direct layer jumps 1,212 a frame with the
  switch 0, 706 with 1, 904 without a shift (specks 98, 68, 95). HANDED OVER for the game's trial.
  NOT RUN: the game, NRD (not in this build), Ray Reconstruction, the whole GPU test, ctest.
  Left for after the game's answer: section 7 (the switch out, the shaders' comments on look.w, TESTING.md,
  CHANGELOG, limits by kind in the check).
