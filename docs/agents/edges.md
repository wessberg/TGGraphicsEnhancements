# edges: why the traced picture has 2-pixel stairs and dotted thin things (PT-84)

Worker `edges`, 2026-10-08 (a first agent found the cause and wrote most of the fix; a finisher built, checked and
completed it). Copy: `wt\edges`, base `wt\base55`.

**Where it stands (18:30): both steps are whole, built, and checked offline. Not seen in game yet.**
- A. The camera's rays carry the shift the upscaler is told. B. A frame's traced light is shown in that same frame.
- **No ABI change** (no field, no size, no version). Plugin and native can be installed in either order; install both.
- The one check (`TGPT_TEST_EDGES_ONLY=1`): passes on this build with 0 wrong pixel samples of 14.7 million; fails all
  four of its checks on the base. Numbers and pictures: "The check" below.
- Not run: the whole GPU test, `tgup_gpu_smoke_with_pt`, anything with NRD (this build is `-DTGPT_WITH_NRD=OFF`), the
  game. "For the coordinator" below says what to run and what to expect.

## The cause, in plain words

The tracer's camera rays carry **no sub-pixel shift at all**. The game's upscaler is told a new shift every frame.
So the upscaler lays the same unshifted, low-resolution traced picture down at a different place each frame, and what
settles is that picture's own pixel grid: 2-pixel blocks, stairs on edges, dots for thin things.

It is not "the shift of the frame before" (the coordinator's lead). The traced picture has none.

Why the rays have none: the plugin reads HDRP's camera matrices too late in the frame.
- HDRP shifts its projection every frame (`HDCamera.UpdateViewConstants`, jitter from `taaFrameIndex`).
- While HDRP RECORDS its frame, the upscaling step ends with `HDRenderPipeline.RestoreNonjitteredMatrices`, which calls
  `hdCamera.UpdateAllViewConstants(jitterProjectionMatrix: false)` **at record time, not when the pass runs**. From then
  on `hdCamera.mainViewConstants` holds the matrices WITHOUT the shift. HDRP's own passes do not mind: each took its
  copy of the shader constants while it was recorded.
- The plugin's capture pass (`HdrpPasses.Capture`) runs when the graph EXECUTES, after all recording. It reads
  `hd.mainViewConstants.invViewProjMatrix` there: the unshifted one. `tgpt_api.h` and `pt_common.hlsli` call it
  "HDRP's, jittered". In the game it is not.
- So `camera_relative_position(p, depth)` (pt_common.hlsli) aims every camera ray through the plain pixel centre.
  HDRP's depth, G-buffer and the shift the upscaler is told all have the shift.
- The offline GPU test builds its own matrices, with the shift in both (`make_camera`), so it never saw this.

(Read in the game's own HDRP assembly, decompiled to a scratch folder and deleted after; nothing copied.)

## The numbers that show it (CPU scripts over the kept frames of run `20261008-130029`, frozen world)

1. **The picture sits on the render grid.** Mean difference between two neighbouring output pixels, by where their
   border falls in the 1924x1084 render grid (`forest`, 6 frames): traced, no denoiser: **4.70 on a render pixel's
   border, 3.59 in its middle** (down the screen: 4.77 and 3.11); with NRD 3.52 and 2.42. The game's own picture:
   4.25 and 4.0 (flat: no grid). The same from frame to frame, whatever the shift.
2. **The picture slides with the shift the upscaler is told, and not against the shift before.** Each frame's
   whole-picture movement against the 16 frames' mean, then the step from one frame to the next, fitted against the
   frames' shifts (150 frame steps of 10 scenes, no denoiser; the shifts are HDRP's Halton(2,3), recorded in each
   `step.json`):
   - this frame's shift: **+0.057 +-0.005** output pixels per render pixel (x), **-0.056 +-0.005** (y);
   - the frame before's shift: +0.021 (x), -0.012 (y): the SAME sign. A picture traced under the previous frame's
     shift would show the opposite sign and the same size (-0.057, +0.056).
   - one-term fits (forest, tavern-room): "told shift" explains 70 to 87 % of the movement; "told minus the one before"
     29 to 59 %; "the one before" alone 0 to 29 %.
   - The game's own frames move 0.001 to 0.003 pixels (theirs agree with the told shift, so nothing slides).
   So the traced picture is the same samples every frame, dragged about by the told shift.
3. **Putting pixels back by the "previous frame's shift" rule makes it worse, not better**: agreement of fine detail
   with the game's picture 0.46 as shown, 0.34 to 0.44 after (forest grass; rock, trunk and fence the same way).

Scripts: `tools/harness/edges_shift.py` (2 and the grid of 1; see its header).

## Who belongs to which frame when the upscaler runs (frame N), as the code is today

| Thing | Camera | Sub-pixel shift | Where |
|---|---|---|---|
| Traced colour (None, NRD, RR) | frame N-1 | **none** | traced in `trace_event` of N-1; laid into N by `composite_event` / `d3d11.hlsl composite_surface` through N's motion vectors: nearest texel (None, RR), bilinear (NRD) |
| The tracer's own first hit | frame N-1 | **none** | `pt_primary.hlsli primary_all` -> `pt_common.hlsli camera_relative_position` with `g.invViewProj` |
| The distance a traced texel is matched by | frame N-1 | N-1's (it is HDRP's depth) | `pt_primary.hlsli` `match`, stored in output A's alpha |
| HDRP's depth | frame N | N's | rasterised by HDRP |
| HDRP's motion vectors | N against N-1 | none in the vector (both ends unshifted), sampled at N's shifted pixel centres | HDRP |
| The shift the upscaler is told | frame N | N's | DLSS: HDRP's `DLSSPass` (-`taaJitter`); RR: `StpHost.cs` `JitterX/Y = hdCamera.taaJitter` |
| RR's guides from the G-buffer | frame N | N's | `tracer.cpp write_guides` (start of `composite_event`) |
| RR's guides from the tracer (hair), hit distance | frame N-1 | none | outputs g0, g1, c of N-1, taken in the composite |
| NRD (inside the trace of N-1) | N-1 | told N-1's shift (`nrd_denoiser.cpp` `cameraJitter`), its input has none | `tracer.cpp` `n.jitter = f.jitter` |

So two things are wrong, and only the first shows today:
- A. the rays have no shift (the bug above);
- B. even with the shift in, the picture shown in N was traced for N-1: it would carry N-1's shift under N's label.
  Fixing A alone turns today's defect into that one.

## Candidates weighed

1. **Rays with the shift of the frame they will be shown in** (N-1's trace with N's shift; HDRP's sequence is fixed, so
   it can be known a frame ahead). Free. Exact while the camera is still. When it moves, each pixel still takes last
   frame's nearest sample, up to half a render pixel off, which the upscaler cannot be told. And the rays then no longer
   pass through the points HDRP's depth and G-buffer of their own frame were drawn at: the yardstick (own first hit
   against the G-buffer, pixel by pixel) gets a sub-pixel skew for good, and the "match by HDRP's distance" key
   describes another sample than the texel holds. It also needs the plugin to predict HDRP's next shift.
2. **The composite moves the light by the two shifts' difference with a filter.** Cannot be exact: a nearest pick
   leaves up to half a pixel that the upscaler is not told; a bilinear or wider filter blurs and mixes raw samples
   (which Ray Reconstruction must get unmixed). A stand-in. Dropped.
3. **Trace and show in the same frame.** The rays of frame N pass through the very points HDRP's depth and G-buffer
   of frame N were drawn at, with the shift the upscaler is told in N. Exact, still or moving. One frame LESS lag.
   The composite needs no motion vectors, no distance key, no neighbour search, no random fill for uncovered pixels:
   all of that is deleted. The yardstick becomes exact. Ray Reconstruction gets colour and guides of one frame.
   Cost: D3D11 waits for the frame's own trace at the composite. PROJECT-CONTEXT 9 notes "+5.7 ms" for such a wait,
   measured when the tracer was a few ms of bounced light and HDRP lit the frame beside it. To be measured again.

**Chosen: 3**, with A fixed first (it is needed by every candidate). Reasons: the owner's "no shortcuts ... correctness,
then we optimise" (2026-10-07) and "remove the workarounds as we go ... borrowed motion"; 1 keeps an error whenever the
camera moves and skews the yardstick; 3 is the only one where nothing on screen is last frame's light moved by HDRP's
motion vectors. If the wait costs too much frame rate in game, candidate 1 is the fallback; its recipe is at the end.

## State

(kept current below; the newest entry is the last)

### What the first agent left (read by the finisher on 2026-10-08, before any change)

The copy differs from `base55` in 8 code files. Nothing was built: no build folder existed. The first agent made its
edits with three scripts (`scratchpad\edges-work\edit_tracer.py`, `edit_d3d11.py`, `edit_shaders.py`); each hunk in
them stops the script if it does not match, and every hunk is in the files. So no edit is half made. What was missing
is everything after the edits: a build, the check, the docs.

| File | What the edit is | Step | State |
|---|---|---|---|
| `src/TGPathTracing/HdrpPasses.cs` | `armedView`: a copy of `hdCamera.mainViewConstants` taken in `BeforeSsr`, while HDRP still records its opaque passes (the shift is in). `Capture` hands THAT to the tracer, no longer `hd.mainViewConstants` (no shift by then). `SayShift`: one log line a session, whether the matrices handed over hold the shift (a warning if not). | A | whole, not compiled. A stale comment at the file's top (line 13) still says "previous frame's traced light". |
| `native/pathtracing/include/tgpt_api.h` | Comments only: `invViewProj` and `viewProj` are WITH the frame's shift. No field, no size: **no ABI change**. | A | whole |
| `shaders/pt_common.hlsli` | Two comments (the constant's meaning; `motion_at`). | A, B | whole |
| `shaders/pt_prepare.hlsl` | One comment. | B | whole |
| `shaders/pt_primary.hlsli` | A record's distance is its own hit's (it was HDRP's depth at the pixel, the key the old composite matched texels by). Comments. | B | whole, not compiled |
| `shaders/pt_compose.hlsl` | Glass no longer takes "the distance of HDRP's surface behind it" (the same key). Header comment. | B | whole, not compiled |
| `shaders/d3d11.hlsl` | The composite rewritten: each pixel takes its own texel of this frame's light. 190 lines out, 45 in. | B | whole, not compiled |
| `src/tracer.cpp` | `Result` holds only the output set and the fence value; `st.latest` is gone; `CompositeWork.traced`; the composite waits on the GPU for the frame's own trace and binds 8 textures where it bound 12. | B | whole, not compiled |

Not there at all: the check in the offline test (`tests/` is untouched), `tools/harness/edges_shift.py` (this report
named it, but the copy has no such file: its parts are loose scripts in `scratchpad\edges-work`), any doc beyond this
report. And the decompiled HDRP reading copy was NOT deleted (`scratchpad\edges-work\hdrp`): the finisher deletes it.

Decision: finish what is there. Both steps are far enough: A is one small plugin edit, B's edits are complete as text.

### Finisher's log (2026-10-08)

- 18:00 The first agent's edits build as they are: `%TEMP%\tgb\edges-build` (Ninja, Release, NRD off, 8 compiles at a
  time), installed to `%TEMP%\tgb\edges-inst`. All 43 shader passes, `tracer.cpp` and the test compiled, no error.
- 18:05 Added to step A, native side: `native/pathtracing/src/camera_shift.hpp`, called from `TGPT_SubmitFrame`
  (`extension.cpp`). It measures the shift the handed matrices hold against `TGPT_Frame.jitter`. Matrices that hold
  none while the frame has one get it put in; one log line a session says which way they came. Why: the plugin's fix
  (`armedView`) can only be proven in the game; with this the tracer cannot lose the shift again whatever the plugin
  reads, and the offline test can hand the matrices exactly as the game handed them before the fix. HDRP's own matrices
  stay the first choice: when they hold a shift, they are used untouched.
  Checked on the CPU (a scratch program, `scratchpad\edges-fin\shift_check.cpp`, matrices built the way HDRP builds
  them): held is recognised, a missing shift is put in to 6e-8, a pixel's centre then looks at the point that lay
  +jitter pixels away, another shift than told is left alone and said. No ABI change.
- 18:10 The check is written: `tests/pathtracing_edges_checks.inl`, included in `tests/pathtracing_gpu_test.cpp`
  (alone: `TGPT_TEST_EDGES_ONLY=1`). The plugin compiles (`dotnet build src/TGPathTracing/TGPathTracing.csproj`,
  output `%TEMP%\tgb\edges-plugin`).
- 18:11 The coordinator passed on the owner's word (17:55): workers' small GPU checks are allowed again, gated by the
  main tree's `Run-PtHarness.ps1 -GpuCheck`. Run through one waiting script (`scratchpad\edges-fin\edges-gpu.ps1`).
- 18:16 **The check passes on this build** (run r1, 303 s, of which 284 s were the new build's passes being made):
  - matrices with the shift in them: of 14,734,928 pixel samples in 16 frames, **0** are not what their own frame's
    shift says; 18,493 are not what the frame before's says; 9,534 are not what "no shift" says;
  - matrices handed WITHOUT the shift, as the game handed them: **0**; 18,494; 9,535 (the tracer put the shift in);
  - the camera moving 2 cm a frame: **0** of 14,734,691; 54,745 are not the frame before's picture;
  - the tracer's log holds both lines (the matrices came with the shift; came without and it was put in).
  Pictures: `%TEMP%\tgb\edges-images-r1\84-edges-16-frames.png` (16 frames laid out by their shifts: a clean thin bar,
  round holes), `84-edges-one-frame.png` (one frame at that zoom: stairs and dots, as any single frame is).
- 18:21 One set of output textures where there were two (the second only let D3D11 read the frame before's light
  while D3D12 wrote this frame's). Rebuilt. The check again, run r2: the same numbers, passed, 12 s.
- 18:23 **The same test program against the base's DLLs** (`wt\base55` built out of source into
  `%TEMP%\tgb\edges-base-build`, nothing written into base55): **FAILED 4 checks**, as it must. See "The check".
- 18:30 Source checks: 96 pass (95 and a new one, `test_path_tracing_edges_carry_the_shift`; six old ones pinned the
  deleted composite's text and were brought in step). `tools/harness/edges_shift.py` written from the first agent's
  loose scripts and run on the kept frames. The decompiled HDRP reading copy is deleted. Docs: README (the frame's
  description), `docs/TESTING.md`, `docs/PT-HARNESS.md`. A dry merge onto today's main tree: 14 changed, 5 new, 0
  conflicts.

## The fix as built

**A. The camera's rays carry the shift the upscaler is told.** Two holds on it:
1. The plugin (`HdrpPasses.cs`): `armedView`, HDRP's matrices copied in `BeforeSsr`, while HDRP still records its
   opaque passes and its camera still holds the shift. `Capture` hands those over. `SayShift` writes one line a
   session to `BepInEx/LogOutput.log`: "The tracer's camera rays carry HDRP's sub-pixel shift ..." or a warning.
2. The tracer (`native/pathtracing/src/camera_shift.hpp`, called in `TGPT_SubmitFrame`): it works out which shift the
   handed matrices hold (from `viewProj` against `nonJitteredProj * view`) and compares it with `TGPT_Frame.jitter`.
   - They agree: nothing is touched. HDRP's own matrices drive the rays. This is what the game should do now.
   - The matrices hold none and the frame has one: the shift is put into `viewProj` and `invViewProj` (one row
     operation each, no inversion). The plugin's old behaviour, or any future slip of that kind, can no longer take
     the shift out of the picture.
   - They hold another one: left alone (HDRP drew its depth with them), and said in the log with both numbers.
   One line a session for each case, in `BepInEx/TGPathTracing/Native/TGPathTracing.Native.log`.
   Why both: 1 is exact by construction but can only be proven in the game. 2 lets the offline test hand the matrices
   exactly as the game handed them before, and makes the check fail on the base for the game's own reason. If the
   coordinator wants one mechanism only, 2 alone is enough (then `armedView` and `SayShift` can go); 1 alone is not
   testable offline.
   The sign of the shift: HDRP moves its frustum's planes by +jitter pixels, so a pixel's centre shows what lay
   +jitter pixels from it in the unshifted picture, along x and along the render target's rows alike. The CPU reference
   of the check is built from the camera alone with that rule, and agrees with the tracer on every pixel.

**B. A frame's traced light is shown in that same frame.** `composite_event` waits on the GPU for the frame's own
trace (`ID3D11DeviceContext4::Wait` on the consumer fence) and the composite takes each pixel's own texel. The rays of
frame N pass through the points HDRP drew frame N's depth at, under the shift the upscaler is told in frame N.

## The check (`tests/pathtracing_edges_checks.inl`, `TGPT_TEST_EDGES_ONLY=1`)

A blue floor, a red bar about one pixel wide and 17 degrees off the screen's vertical, a green card with 16 round
holes cut out by its texture's alpha. Albedo view, the tracer's own camera rays: no light, no noise. Sixteen frames,
their shifts the 4 x 4 quarters of a pixel in a mixed order. Each pixel of each frame is compared with what a ray
through its shifted centre meets, worked out on the CPU from the camera's place and axes alone (no matrix). Samples
within 0.03 pixels of an edge or 1.5 texels of a hole's rim are left out. Counted: the pixels that are not what their
own frame's shift says, not what the frame before's says, not what no shift says.

| Pixel samples that are wrong against ... | this build | the base (`base55`) |
|---|---|---|
| matrices holding the shift: own shift / the frame before's / none | **0** / 18,493 / 9,534 | 18,561 / **0** / 9,534 |
| matrices handed without it, as the game did: own / before's / none | **0** / 18,494 / 9,535 | 9,345 / 9,348 / **0** |
| the camera moving 2 cm a frame: own frame's picture / the frame before's | **0** / 54,745 | 21,815 / 56,468 |
| verdict | passed (12 s once its passes are made) | FAILED 4 checks |

Read the base's column: with shifted matrices its picture is exactly the frame BEFORE's (defect B). Handed the
matrices as the game handed them, its picture has NO shift (defect A): that is what the owner saw.

Pictures (each is a quarter of the screen, 4 x 4 samples to a pixel: the 16 frames laid out as an ideal upscaler would):
- `%TEMP%\tgb\edges-before-after.png`: the base, this build, and one frame alone, side by side. The base's bar is a
  stair of dots and its holes are jagged; this build's bar is a clean thin line and the holes are round.
- `%TEMP%\tgb\edges-images-base\84-edges-16-frames-late.png`, `%TEMP%\tgb\edges-images-r2\84-edges-16-frames-late.png`.
- Logs: `%TEMP%\tgb\edges-test-r2.txt`, `edges-test-base.txt`.

The command line (the gate, then the test; `edges-gpu.ps1` in `scratchpad\edges-fin` does both):
```
powershell -File <main>\tools\harness\Run-PtHarness.ps1 -GpuCheck        (exit 0: a test may start)
set TGPT_TEST_EDGES_ONLY=1
%TEMP%\tgb\edges-build\tgpt_gpu_test.exe %TEMP%\tgb\edges-inst %TEMP%\tgb\edges-images-r2
```
In the merged tree: `set TGPT_TEST_EDGES_ONLY=1` and `build\native\tgpt_gpu_test.exe artifacts\Native <images>`.
No mutation check was made beyond the base run (the base IS the mutant of both steps).

## Workarounds met: removed, and left

Removed, all of them things that existed only because the light was shown a frame late:
1. HDRP's motion vectors in the composite (each pixel looked for last frame's light through them).
2. The distance key: a record kept HDRP's depth at its pixel for the composite to match texels by, glass took "the
   distance of HDRP's surface behind it". A record's distance is its own hit's now (`pt_primary.hlsli`, `pt_compose.hlsl`).
3. The four texels around the found place, blended by kind (`texel_kind`, `kinds`, `want`), and the switch between
   nearest and bilinear for raw and denoised light.
4. The neighbour search (the eight neighbours, then rings two and four texels out).
5. The random fill: a texel picked at random within 24 pixels for a pixel that found nothing, and its per-frame seed.
6. The ratio of two frames' exposures, and the second exposure texture bound for it.
7. `sky_surface`, a second path for pixels where HDRP has no depth. One rule for every pixel now.
8. `st.latest`, the result record's camera and matrices, the camera's move between two frames in the composite's
   constants. Four of the composite's twelve textures.
9. The second set of output textures (five shared textures; about 26 bytes a pixel, 54 MB at 1924x1084).
10. In the test: the control "the same hair hidden from the drawn depth slides with the wall (the check sees a slide)".
    Nothing can slide any more. It now expects the hair to stay where it is, drawn into the depth or not.
    **Changed blind** (it is deep in the whole test, which this job may not run): if the whole run fails there, look here.

Left, with the reason:
- The route before the tracer's own camera rays (`TGPT_OPTION_GBUFFER_SURFACES`): the composite keeps its two texel
  kinds and multiplies HDRP's G-buffer colour back in for it. The offline test's older checks use it. PT-60's list.
- HDRP's depth is still read in `pt_compose` to tell "glass in front of HDRP's sky" (`overSky`). The glass migration's.
- The input textures stay two sets by parity: a frame's trace reads the frame before's water and skin.
- HDRP's motion vectors are still copied to D3D12: the old route and the yardstick read them. The composite does not.
- The composite's constant buffer is shared with the guides pass and still holds a matrix the composite ignores.
- Three places in the test still run a frame twice "because the composite showed the frame before": harmless, the
  comments say so now. Not changed, since frame counts in a test this job cannot run are not worth the risk.
- `HdrpPasses.cs`'s header still describes a water pass with render event 4 that no longer exists (an earlier job's).

## Ray Reconstruction's route

Checked by reading, nothing changed there. TG Better Upscaling's `StpHost.cs` tells RR `hdCamera.taaJitter` of the
frame, HDRP's unshifted projection, and takes HDRP's colour, depth and motion of the frame. The guides are taken only
when they are this frame's (`GuidesFrame == Time.frameCount`, `ExtrasFrameIndex == FrameIndex`). Before, the colour's
traced part, the tracer's own guides (hair) and the reflections' hit distance were the frame BEFORE's, moved. Now all
of it is one frame's: colour, guides, hit distance and the told shift. Not run: `tgup_gpu_smoke_with_pt` and RR in
the game (the composite's hand-off of guides is on that path: the coordinator's merged build should run the smoke).

## For the coordinator

**Merge.** `python tools/agents/merge_copy.py edges --base=base55 --root=<scratchpad>`: 14 changed, 5 new, 0 conflicts
today. `d3d11.hlsl`'s composite block, `pt_primary.hlsli`, `extension.cpp`, `HdrpPasses.cs` and `tgpt_api.h` are
unchanged in main since the base. `tracer.cpp` merges as text; main has no new use of what was removed.

**After the merge, in this order.**
1. `python tests/source_checks.py` (96).
2. Build with NRD on. This copy was built without it: NRD's part compiles for the first time there.
3. `TGPT_TEST_EDGES_ONLY=1` (seconds). Then the whole GPU test once. Where it may differ from before, and why:
   - the hair control named above (changed blind);
   - any check that reads a picture one frame after a change now sees that frame's own light, not the frame before's;
   - its "throughput" lines ("path tracing adds ... ms a frame when HDRP's work sits between the events") now include
     the wait: compare with an older run's number. That is the wait's cost outside the game.
4. `tgup_gpu_smoke_with_pt` (the RR path's hand-off changed).
5. Deploy plugin and native together, then ONE scene: `forest` with `quick`.

**What to look at in `forest`, and what the numbers should become.**
- The logs first. `BepInEx/LogOutput.log`: "The tracer's camera rays carry HDRP's sub-pixel shift". The native log:
  "The camera's rays carry HDRP's sub-pixel shift (this frame's: x, y render pixels)". If the native log says "came
  without the frame's sub-pixel shift ... the tracer put it into its camera rays itself": the plugin's read point is
  still too late; the picture is right all the same. If it says "hold a sub-pixel shift of A where the frame says B":
  the sign rule is not HDRP's after all; the rays then follow HDRP's matrices, the picture is still right, tell the
  next agent the two numbers.
- `python tools/harness/edges_shift.py <run> forest`. Before (run 20261008-130029):

  | step | the render grid (x, y) | slide a render pixel of told shift (x, y) |
  |---|---|---|
  | the game's own | x1.04, x1.04 | none (it moves 0.001 px) |
  | traced, no denoiser | x1.27, x1.41 | +0.045 +-0.003, -0.058 +-0.005 |
  | traced, NRD | x1.43, x1.50 | +0.142 +-0.016, -0.148 +-0.018 |

  After: the two traced rows should read like the game's row: the grid near x1.0 to x1.1, the slide within its error
  of 0.
- The pictures: the same crops as `artifacts\inspection-20261008\forest-grass-zoom3.jpg` and
  `forest-lantern-right.jpg`. The mean of the 16 frames with no denoiser: blades as lines, not rows of dots; the pole
  and the stakes without 2-pixel steps.
- The yardstick's line in the log: more pixels "the same surface" (the rays now pass through the very points HDRP
  drew; before they were up to half a render pixel off at every edge). A forest was at 64 % on 2026-10-07.
- Flicker on grass and leaves (`flicker.py`, PT-85): to be read, not promised. A single frame now differs MORE from
  the next at render size (a blade is hit in one frame and missed in the next: that is what sampling at a shift
  is), and the upscaler is what resolves it. If it falls, the edges' cause was the plants' cause too.
- NRD is told the same jitter as before, and its input now really has it. Watch its picture for a change at edges.

**What the wait costs, and how to measure it.** Before, D3D11 never waited: frame N's trace ran beside all of frame
N's D3D11 work. Now everything D3D11 does after the composite (fog, transparents, post, the upscaler) comes after the
frame's trace on the GPU. Expect about that much GPU time more a frame (some milliseconds), most felt where tracing is
fast. Measure: `forest` with `quick` on the build before and on this one, the same recipe; compare the frame time of
`2-traced-none` and `3-traced-nrd` (`step.json` `perf`, `report.md`). The native log's periodic line has
`gpuWaits=`: it now counts (nearly) every composited frame; before it counted only the frames whose earlier trace
had not finished in time. If the cost is too high, the
fallback is candidate 1 above (trace N-1 with frame N's shift), which keeps a frame of lag and an error under motion.

**With A alone** (had B not been finished): the picture would carry the frame before's shift under this frame's
label. The base's first row above shows it exactly (0 wrong against the frame before's shift, 18,561 against its
own): in game, edges would shimmer by up to one render pixel instead of standing as stairs. So A alone was never
shippable; both are in.

**For the coordinator's files (not edited here).**
- `docs/PROJECT-CONTEXT.md` section 9, "One frame late on purpose: a D3D11 wait on D3D12 work of the same frame cost
  +7.6 ms ... Frame N composites frame N-1's traced light, ... reprojected with N's motion": no longer so. And "Debug
  views are a frame late: run each twice": no longer so.
- A method trap worth a line: HDRP's `RestoreNonjitteredMatrices` runs when the frame is RECORDED. Anything read from
  `HDCamera.mainViewConstants` in a pass's execute function has no jitter. `hdCamera.taaJitter` survives.
- A method trap: the offline test built its own shifted matrices, so it could not see a plugin handing unshifted ones.
  `lateMatrices` in the test hands them the game's old way.
- `AGENTS.md` says "76 checks" of the source checks; there are 96.
- CHANGELOG: nothing written here. The entry is this section and "The check".

## Files

New: `native/pathtracing/src/camera_shift.hpp`, `tests/pathtracing_edges_checks.inl`, `tools/harness/edges_shift.py`.
Changed: `src/TGPathTracing/HdrpPasses.cs`; `native/pathtracing/include/tgpt_api.h` (comments);
`native/pathtracing/src/extension.cpp`, `tracer.cpp`; `shaders/d3d11.hlsl`, `pt_primary.hlsli`, `pt_compose.hlsl`,
`pt_common.hlsli` and `pt_prepare.hlsl` (comments); `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`;
`README.md`, `docs/TESTING.md`, `docs/PT-HARNESS.md`.
Builds: native `%TEMP%\tgb\edges-build`, installed `%TEMP%\tgb\edges-inst` (NRD off; the tree is up to date and the
installed `TGPathTracing.Native.dll` is the built one, SHA-256 `935D337B2C930722...`); plugin `%TEMP%\tgb\edges-plugin`.
These are a worker's builds for its check, not for the game: the game gets the coordinator's merged build.
Scratch (`scratchpad\edges-fin`): the gate script, the base's build script, the edit scripts, the CPU check of the
shift's arithmetic.
