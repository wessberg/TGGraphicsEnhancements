# PT-19: NRD noisier and blurrier than Ray Reconstruction (agent notes, 2026-10-07)

State: built, passes the offline harness (`tgpt_gpu_test`, the full run) and the source checks (83). Not seen in game.
The first agent stopped at a usage limit; a second one finished it (see "Finishing" at the end).

## In short
- Blurrier: NRD was handed the bounced light with the normal map's shading in it, but guided by the geometric normal,
  so it filtered that shading away with the noise. Shade outdoors is bounced sky light, so every normal-mapped wall in
  shade went flat. Fixed by running NRD in its directional form and putting the normal map's shading back after.
- Noisier: two things. (1) NRD's input units were tied to the brightest light in the scene (or the sun), so a room lit
  far below that light lost the precision NRD needs for its noise estimate. Fixed: the units now follow the bounced
  light's own level. This matters where a room sits far below that light (a light 40,000 times brighter far away: boil
  0.49 -> 0.30); in ordinary scenes NRD boils about as much as the built-in denoiser, before and after (the day's
  0.210 -> 0.178 below is start-up spread, not a gain: see "Finishing"). (2) Some of the gap to Ray Reconstruction is not NRD's: the direct light and the reflections go
  through the tracer's own filters with NRD, and DLSS upscaling (not Ray Reconstruction) runs after it. Not changed here.
- Defaults: none changed. More rays a pixel buy NRD almost nothing and would cost 25 to 45 ms in game.

## What NRD got (before), checked against NRD 4.17.3's own README and NRDSettings.h
- Which denoiser: RELAX_DIFFUSE on the bounced light only. The direct light and the reflections go through the tracer's
  own filters (temporal + a-trous), with NRD as with the built-in denoiser. RELAX is NVIDIA's pick for RTXDI/ReSTIR
  input, so the choice stands.
- Settings: RELAX's defaults (history 30 frames, fast history 6, 5 a-trous passes), anti-firefly on, pre-pass blur off,
  hit distance reconstruction off. NRD's README allows the pre-pass off for a clean signal with a sample in every pixel
  (this one; there is no probabilistic diffuse/specular split, so no checkerboard mode and no reconstruction).
- Hit distance: RELAX uses it only in its pre-pass and for specular tracking, so for the diffuse denoiser with the
  pre-pass off it is unused. Not a cause.
- Motion: 2D (HDRP's motion, negated to "previous minus current"), non-jittered matrices, jitter in render pixels.
  RELAX uses the jitter only for the size of its frame-to-frame change (its disocclusion slack), so its sign does not
  matter. 2D motion makes RELAX test a moving object's history as if it stood still (NRD recommends 2.5D); this costs
  history on things moving toward or away from the camera, not on the still world. Unchanged (2.5D needs previous
  positions of moving things, which the tracer does not have).
- Normals: the geometric normal (a normal map's per-pixel normals made RELAX's history test fail, measured earlier).
- Radiance: divided by the diffuse color, in the tracer's storage units: light divided by the scene's reference (the
  brightest light at 1 m, or the sun, over pi).

## The offline measure (new, in `tests/pathtracing_gpu_test.cpp`, `nrdStudy`)
The in-game harness's numbers, offline: the harness room with bump-mapped surfaces, the game's defaults (1 bounce,
1 ray, 4 spatial samples, histories 4 and 8), the camera still (no jitter: in game the upscaler resolves it). 48 frames
to settle, then 16 measured. All in 8-bit levels of the picture as the test saves it.
- boil: the standard deviation over the frames of each 8x8 block's mean (the harness's "boil"), mean and 95th percentile.
- flicker: the same per pixel.
- error: the 16 frames' mean picture against an undenoised reference (2 x 80 frames of 8 rays, no denoiser).
- detail kept: how much of the reference's fine detail (its Laplacian) the mean picture keeps, 1 all, 0 none, measured
  against the reference's two halves so that their noise cancels (sum of Lap(mean) x Lap(A+B)/2 over sum of Lap(A) x
  Lap(B)). Over the whole picture, and under the ceiling (bounced light only).
- Scenes: the sun with the lamp; night with 24 torches and one light over the checkered leaf; the night with a light far
  away, out of the room's reach, 40 and 40,000 times the brightest (changes nothing in the room but the reference).
- Run alone: `TGPT_TEST_NRD_ONLY=1 tgpt_gpu_test.exe <prefix> <images>` (some 3 minutes). It also runs in the full test.
- Four new checks. On the code before, the first two fail (detail under the ceiling 0.009 and 0.001; the far light's
  boil 0.49 against 0.28); with both fixes undone on purpose, three fail (the fourth, error, is a guard):
  "NRD keeps the normal map's shading in the bounced light (under the ceiling)" (detail kept > 0.2, sun and night),
  "NRD denoises the room the same with a light 40,000 times brighter far away" (boil within 1.25x, 95th within 1.5x),
  "NRD boils no more than a quarter above the built-in denoiser", "NRD's picture is as close to the undenoised reference
  as the built-in denoiser's" (within 1.15x).

## Findings, with numbers
Before (the tree as it was) and after (this change), 1280x720:

| scene | denoiser | boil (95th) before | after | error before | after | detail kept, whole (ceiling) before | after |
|---|---|---|---|---|---|---|---|
| sun | built-in | 0.205 (0.382) | same | 3.85 | same | 0.270 (0.012) | same |
| sun | NRD | 0.210 (0.372) | 0.178 (0.319) | 3.99 | 4.11 | 0.269 (0.009) | 0.377 (0.371) |
| night | built-in | 0.260 (0.459) | same | 6.17 | same | 0.103 (0.005) | same |
| night | NRD | 0.278 (0.467) | 0.273 (0.459) | 6.31 | 6.34 | 0.101 (0.001) | 0.251 (0.354) |
| night, far light 40x | NRD | 0.270 (0.455) | 0.262 (0.448) | 6.35 | 6.34 | 0.103 (0.016) | 0.248 (0.355) |
| night, far light 40,000x | NRD | 0.493 (1.233) | 0.298 (0.510) | 37.5 | 19.2 | 0.075 (0.000) | 0.283 (0.408) |
| night, far light 40,000x | built-in | 1.173 (3.648) | same | 69.4 | same | 0.086 (0.003) | same |

The sun row's boil and error "after" (0.178, 4.11) are one draw of a spread: the same code gives 0.169 to 0.210 and 4.06
to 4.28 on the first NRD run, depending on how its history happened to start ("Finishing"). The detail columns and the
far-light rows hold in every run.

1. Blur (the main finding). Under the ceiling, lit by bounced light alone, NRD kept 0.1 to 1 % of the reference's fine
   detail: the bumps were gone (pictures: `82-nrd.png` before and after, against `80-nrd-reference.png`). The
   built-in denoiser does the same (0.5 to 1 %). In game this is all shade: outdoors the shade is lit by bounced sky
   light. It matches the harness's detail column (Cuanacht: game 7.4, RR 6.0 to 6.8, NRD 4.6 to 4.9).
2. Units. With the far light 40,000 times the brightest, NRD's boil rose 0.28 -> 0.49 and its error 6.3 -> 37.5, with
   nothing changed in the room. 40x did nothing. In game the reference is the sun by day (tens of thousands of lux) or the
   brightest light or glowing surface in the scene, so a candle-lit room under a daytime sun, or a room with a fire's
   glowing surface, can sit thousands of times below it.
   The remaining error at 40,000x (19 against 6.3) is the tracer's own storage at that ratio (the built-in denoiser's:
   69); not NRD's, not changed here.
3. More rays and more spatial samples barely help NRD (first run, with jitter: night boil 0.328 at 1 ray, 0.326 at 2,
   0.316 at 4, 0.323 with 8 spatial samples; this run: 0.273 -> 0.252 at 4 rays at night, 0.178 -> 0.198 by day).
4. Jitter: with the normal map's shading back, the bounce-lit ceiling changes from frame to frame with the camera's
   jitter (the full test's bump-mapped night case: 0.030 relative change a frame moving, 0.038 still and jittered,
   the same at 8 rays a pixel, so it is the shading at each jittered sample, not noise; 0.003 without the shading put
   back, 0.002 for the built-in denoiser, which blurs it away). It is what the game's own normal-mapped lighting does
   before its upscaler resolves it. The older check "NRD flickers no more than half again as much as the built-in
   denoiser" is therefore no longer applied to that one bump-mapped case (its error still is); NRD's noise is checked
   without jitter in `nrdStudy`.
5. NRD against the built-in denoiser: about equal in boil and error in every scene, before and after. So in game the
   gap to Ray Reconstruction is mostly what both share: the tracer's own filters for the direct light and reflections,
   and DLSS upscaling (not Ray Reconstruction) after them.

## What changed
- `pt_gi_spatial.hlsl`: in frames NRD denoises (OPTION_NRD, which tracer.cpp now clears in `settings2.y` when NRD does
  not run), the bounced light is written as the geometric normal takes it (cosine with `geoNormal`), and its alpha holds
  the direction it came from as a 45 x 45 octahedral cell (a whole number below 2025, exact in a half float) instead of
  the hit distance. Other frames are unchanged.
- `pt_common.hlsli`: `OPTION_NRD`, `direction_code`, `direction_from_code`.
- `pt_nrd_prep.hlsl`: three modes. 0 clears its histogram bins; 1 packs NRD's inputs in the SH form
  (`RELAX_FrontEnd_PackSh`: SH0 the light, SH1 its direction times its luminance), holds light above 250 of NRD's units
  there, and bins log2 of the light (64 half-EV bins from 2^-24) into the meter's buffer (uints 64 to 127); 2 turns
  NRD's result into linear light with the normal map's shading put back: NRD_SH_ResolveDiffuse's irradiance for the
  shading normal over that for the geometric normal (c0 + 3 c1.N), held to [0, 2].
- `nrd_denoiser.cpp/.hpp`: `RELAX_DIFFUSE_SH`; IN/OUT_DIFF_SH0/SH1 mapped; `NrdFrame.direction/directionOut`.
- `tracer.cpp`: two RGBA16F render-size textures (NRD direction in and out); the meter buffer 64 -> 128 uints and its
  read-back slot to match; the NRD scale: its 1 stands for twice the bounced light's median as read back (in HDRP's
  units, `nrdMedian`), a hundredth of the reference until the first reading; restart when the median leaves 1/16 to 2 of
  the scale (before: when the reference moved 4x); the resolve dispatch after NRD; `settings2.y` without OPTION_NRD in
  frames NRD does not denoise.
- `tests/pathtracing_gpu_test.cpp`: `nrdStudy` and its four checks. `tests/source_checks.py`: the changed lines and new
  checks (83 tests pass; the new one mutation-checked).
- `CHANGELOG.md`: an entry at the top of the path tracing part.

## New defaults when NRD is chosen, and their cost
None. Rays Per Pixel 4 cut NRD's night boil by 8 % and raised the day's; offline it cost 1.2 ms at 1280x720, and in game
the bounced light stage takes 8 to 16 ms a ray at 1 ray (harness `20261006-212905`), so 4 rays would add some 25 to 45 ms
to frames already at 25 to 36 fps against a 55 fps target. Spatial Samples 8 and 2 rays changed nothing measurable.
RELAX's own history settings were left at NVIDIA's defaults (longer history needs a history confidence input NRD asks for
first; not done).

The fix's own cost: RELAX's SH form, about +0.1 to 0.15 ms at 1280x720 offline (NRD frames 4.05 -> 4.19 ms by day,
4.48 -> 4.57 at night), so about +0.3 to 0.5 ms at the game's 1920x1080 render size (estimate); memory: two more
RGBA16F textures at the render size (2 x 16.6 MB at 1080p) and RELAX's larger SH pools (NRD's README, at 1080p:
RELAX_DIFFUSE 91 MB, RELAX_DIFFUSE_SH 158 MB), so about +100 MB of video memory at a 1080p render size, only while NRD
runs (its textures are made when it first denoises).

## Files
`native/pathtracing/shaders/pt_gi_spatial.hlsl`, `pt_nrd_prep.hlsl`, `pt_common.hlsli`;
`native/pathtracing/src/nrd_denoiser.cpp`, `nrd_denoiser.hpp`, `tracer.cpp`; `tests/pathtracing_gpu_test.cpp`,
`tests/source_checks.py`; `CHANGELOG.md`; this file.

## ABI and bits
- No `tgpt_api.h` change, so no ABI change: the main tree's 0x00011A00 stays (this copy's base still had 0x00011900).
  No new option or material bit, no FrameConstants slot (the taken ones, material 131072, 262144, 1048576, option
  67108864 and `textures.z`, are untouched).
- Shaders now read the existing option bit `TGPT_OPTION_NRD` (1024) in `settings2.y`, meaning "NRD denoises this
  frame" (tracer.cpp clears it otherwise; the plugin still sets it for every choice but None).
- No FrameConstants slot taken. `pt_nrd_prep` pass constants: b.y the histogram, b.z the mode, b.w the SH1 input.
- The meter buffer grew from 64 to 128 uints (bins 64 to 127 are NRD's; `pt_meter` still only clears and fills 0 to 63).
- While NRD runs, the bounced light's alpha (`signals.y`) is a direction code, not a hit distance (Ray Regeneration,
  which reads the hit distance, never runs in the same frame).

## What the next harness run should show (`-Recipe quick` on `npc-day`, `glow`, `vista`, `tavern-room`)
- The "detail" column for NRD rises toward RR's wherever normal-mapped surfaces sit in shade or bounced light: `vista`
  and other daylight scenes most (NRD was 4.6 to 4.9 where RR had 6.0 to 6.8), `tavern-room` some (4.57 against RR's
  4.63), `npc-day` a little (2.40 against 2.56).
- "boil" for NRD: about as before (this change is about detail, and about rooms far below the scene's brightest light).
  In `tavern-room` (NRD 0.07 against RR's 0.03, 95th 0.25 against 0.07) it falls toward
  RR's only if that room's light sat far below its reference (likely: the fire's glowing surface counts as a light); if
  it does not move, the rest is the direct light's and reflections' own filters and the edges (thin beams).
- NRD's GPU time ("denoising" stage) about +0.3 to 0.5 ms; frame rates otherwise as before.
- The native log at start: `NVIDIA NRD 4.17.3 available (RELAX, directional, N pipelines, ...)`.
- Look at: shaded stone and wood under NRD (bumps back, no new blotches); a walk from a sunny street into a house
  (NRD's history may start over once there: a moment of fresh noise).

## Risks
- More history restarts: when the bounced light's median moves more than 3 EV down or 2 EV up from NRD's scale (a
  walk from sun into a dark house), NRD starts over (a moment of noise). Before, it restarted when the reference moved 4x.
- The linear SH gives bumps a little less contrast than the reference under light from one side (error 3.99 -> 4.11 by
  day) and darkens bumped surfaces by about 0.6 % on average.
- Rays Per Pixel above 1: the pixel's own rays' mean keeps the normal map's shading (pt_gi), so with NRD that share
  (at most 48 %, at 16 rays) gets it twice. The default of 1 is unaffected.
- Leaves lit from behind: their transmitted light is handed over as if it came along the leaf's geometric normal (no
  direction of its own), so the shading put back after NRD darkens a bumped leaf's tilted pixels a little, as light
  from straight ahead would. Not measured on its own (the study's checkered leaf is inside the whole-picture numbers).
- A frame where NRD fails after it was chosen shows the bounced light without the normal map's shading once (the
  built-in denoiser serves from the next frame).
- Not tested: Ray Reconstruction is unaffected by design (OPTION_NRD is cleared in its frames). `tgup_gpu_smoke_with_pt`
  ran but skipped itself (exit 77, "no upscaler runtimes staged": the scratch prefix has no DLSS runtime). Run it in
  the main tree's ctest after the merge.

## Finishing (a second agent, 2026-10-07 morning)
- The temporary diagnostic (a moving and jittered flicker run behind `TGPT_TEST_NRD_MOVING_ONLY` in the test) was
  already gone: no diagnostic code is left in the diff. Its numbers are item 4 of the findings.
- Passes: the full `tgpt_gpu_test` ("all path tracing checks passed", exit 0) on the build of the final sources;
  `python tests/source_checks.py`, 83 tests. Each of the 11 new or changed source-check lines was broken on purpose in
  turn: all 11 caught. The GPU checks' mutation run is the first agent's (both fixes undone: three of the four fail).
- The day's boil gain was start-up spread. In the full test the sun's NRD boil is 0.208 (error 4.28); in the study run
  alone 0.178 (4.11): the same code, only what ran before differs. Two things were tried and dropped:
  (a) starting NRD's history over once more when its own first median is in and off-centre;
  (b) the median's place in NRD's units at 1/2 (as built), 2 and 8.
  NRD started on its own settled median ("NRD again") boils 0.210 and errs 4.269 at all three places, and the night
  numbers agree to three digits. The first NRD run swings from 0.169 to 0.210 with how its history started (an extra
  restart a few frames in gave the low numbers; why was not found). So the units' exact place does not matter, (a)
  buys nothing proven, and the code stays as the first agent left it. Read the boil column as: NRD about equal to the
  built-in denoiser, by day and at night. What holds in every run: detail under the ceiling 0.35 to 0.41 (before
  0.001 to 0.009), and the far light's boil 0.26 to 0.30 (before 0.49).
- Added: the memory cost (about +100 MB at a 1080p render size), the ABI note, the leaves' risk in plain terms.
- Scripts and logs (scratch, not in the repo): `nrd-final-run.ps1`, `nrd-at-run.ps1`, `nrd-at-variants.py`,
  `nrd-mutate-source.py`; results in `%TEMP%\tgb\nrd-final-r2.txt`, `nrd-final-r3.txt`, `nrd-final-mut3.txt`,
  `nrd-at.txt`, `nrd-final3.txt`.
