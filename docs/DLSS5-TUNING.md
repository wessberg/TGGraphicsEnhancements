# Neural Rendering tuning, 2026-10-07

Naming correction10-08: historical A/B/C below mean native DLSSNR.Style0/1/2. NVIDIA calls these
Default/A/B. The menu now offers native Default/A/B/C (0/1/2/3), preserving saved numeric selections.
The settled historical A look is displayed Default. This is the actual NVIDIA Default, not our own
recommended-style alias. Separate creation preset hints0/1/2/3 had identical output in the signed310.8
probe, while Style0/1/2 differed. Style3 matched2 on that synthetic input. These measurements do not
establish independent model weights or universal equivalence. See R11 for the input audit.

Owner decision 2026-10-08: Quality is A / After Upscaling / full Local Tone. Keep that settled selection.
Owner subsequently accepts Performance (Before) with A and full Local Tone too; both defaults are settled.
C is gentler and improves some face/tavern measures, but worsens held forest variation. Before has larger
slow colour changes after stopping; a separate optimized preset has not been established. Strength/Detail/
Skin Detail remain 1 and Automatic Mask Off. This settles the defaults without declaring NR-14 fixed.

Latest10-08: faster resume141444 and pixel-range142304 each complete four takes/768 frames without
competing GPU jobs. Full-NR walk speeds0.261/0.293m/s versus0.175 in135437;96 moving/96 held over2m.
Both preserve exact neutral and constant held exposure. Tracked p95 changes full/neutral/Off are
2.015/0.399/0.404 and2.050/0.412/0.451 RGB levels. Greater movement per frame prevents direct drift-ranking
against the slower run. Pixel-range run held effect max9.3435, final32 range2.5752: NR-14 persists.

New diagnostics v4 append two grids: actual pixel min/max, negative fraction and HDR-fallback fraction.
Every pixel contributes; fractions use exact cell pixel counts, not equal-cell averages. In142304 full
input0.01238979..0.99196434, output-0.00064514..0.9727249, zero fallback pixels, at most one negative pixel
per frame. Neutral input/output0.01120572..0.98946, no negatives/fallback. Existing cell means could not
establish this. Thus our saturated-output fallback does not explain these chair changes; it remains
unproven that the complete input conditioning matches the model's needs. Both atlases inspected.

Pixel diagnostics installed14:23 (native8E9A85D95171, managedE3868ED67C0B), normal appearance unchanged.
The next diagnostic compares peak and per-channel Reinhard with full model settings; original input
shoulder restores HDR per chosen mapping. Each explicitly selected comparison resets history once, then
runs normally. No new player setting/default. Native and both managed builds/harness compile; focused
GPU encoding/inverse checks pass. Starfield first blocked final probe/deploy; that waiter was canceled.
After it closed, final nativeC2D1289A2B23 and Mono8D4AB9BE3EDF were deployed/hash-checked14:41.

Run144131 (`artifacts/harness/nr-chairs-tone-map-resume-20261008`): Off/current/alternate/current/Off,
five192-frame takes, matching2m routes, one initial reset per NR take, no contention,0 report problems.
Full controls retained. Tracked p95 moving colour changes1.965/1.977/1.983 RGB levels; alternate sits
within current-repeat variation. Mean absolute displayed effect from Off3.852/3.879/3.849 levels: it is
not just weakened. Held model-effect ranges10.019/8.961/10.100 and final32 ranges2.394/2.552/2.948.
No meaningful stability win established. Atlas inspected: similar appearance, colour changes remain.
Current curve triggers no white fallback; alternate triggers at most one pixel per frame. Exposure steady.
Keep original mapping/defaults. This rejects per-channel Reinhard as the next default for this route,
not all possible tone/grading preparation. All117 saves unchanged/configs restored; no NR queue.

Current investigation is Quality only at the owner's request. Run20261008-135437 completed the chair
recipe with ReShade off: Off / Strength0 / Strength1 / Off, A/full controls/one pass,144 moving and96 held
frames over2m. All960 frames captured; four camera paths match and neutral scene grids are exactly equal.
Held exposure is constant. Model-effect max held range is9.424 RGB levels (neutral0); final32 range2.789.
Input max held range3.859: source variation remains. Tracked-feature p95 change is1.699 full,0.429 neutral,
0.388 Off control. Colour variation exists before our HDR restoration, but its root cause is unresolved.
The atlas was inspected: correct chair route. NR takes move at0.173/0.175m/s; readback timings are not
performance evidence. Reports: `artifacts/harness/nr-chairs-quality-20261008-135437`.

Owner requests faster movement: current plan uses96 moving/96 held frames over2m. Run20261008-140059 was
interrupted by the owner because competing PT tests used up to58.4% GPU; only Off completed. Exclude it;
faster wall-clock motion has not been validated.117 saves/configs restored, no queued launch. Use normal
GPU-free gating for future NR runs, not the earlier contention override. No NR rendering/default change.
Earlier run122849 captured0 frames with ReShade loaded (NR-24). Capture works with it off; its underlying
failure and ReShade compatibility remain open. Harness errors now preserve native Unity warnings, NR
starts Off for ordinary PT runs, and requested walking is validated rather than flagged as accidental.
Three chair checks reject nine corrupt-evidence cases; existing harness9 and Source95 passed.

HDR distinction: tone mapping can target HDR, but NVIDIA's sample explicitly expects LDR for NR
([declaration](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.hpp)). Our early
HDR-to-LDR-to-HDR adapter uses the retained input scale; its arithmetic checks do not establish perceptual
correctness, native HDR model input, or RenoDX HDR compatibility. Unity's HDR-active gate does not detect
all external HDR modifications. Colour conditioning remains a hypothesis, not an established cause.

Owner comparison, 10-08 12:04: RenoDX DLSS5 Upscaled with matching settings is just as temporally unstable
as TGNR, including tests with/without RenoDX HDR, our bridge and FG. Earlier RenoDX-better impressions are
superseded. Reproduce from `PT eyebrows-flickering`: at the two men in the corner turn180, walk slowly
forward, watch shadows/lighting around the lady's chairs. Our run135437 now captures this route.
Cross-integration reproduction makes a TGNR-only cause less likely; root cause and whether we
can improve it remain open. This is the owner's observation, not a new instrumented comparison.
The owner also confirms the same problem with RenoDX at Present. A later placement did not solve it;
keep the current placement decision and do not attribute the shared symptom to pre-post ordering alone.
The owner suggests base-lighting problems might provoke NR's instability. Compare paired source and output
at the chair route before attributing it: retained identical inputs also drifted in earlier captures, so
live lighting changes cannot be the only explanation. Better native input preparation remains a hypothesis.

The owner requests tooltips describing Performance's softer detail and larger colour/brightness changes
after movement, and controls for less lighting/colour change. Local Tone and overall Strength remain in
the basic menu. Advanced Off/On now groups A/B/C, Detail, Automatic Mask and conditional Skin Detail; both
Video and FoA share descriptions, order and visibility. Hiding Advanced preserves tuning/history. Built for
both game versions and deployed 10-08 10:31 after both games closed. Source95 passes; menu appearance pending.
The owner clarified "2x" means repeated passes. Advanced Passes1-4 is built (default1), with the same Strength/
tone/detail settings per pass, separate histories and HDR restored once. Exact-input GPU reference passes at
Strength0.5/1; three broken chains rejected. Native ABI0x00010300, deployed11:22; tavern7 valid.
Run20261008-112319:5 NR/2 Off takes, pass counts match; four passes visibly stronger, Off restored, saves117
unchanged. This was a fast functional stress test:16 motion/16 hold frames, not the earlier48/96 quality
recipe. Owner reports worse1pass instability (NR-22). Its sweep took1.153s vs6.001s before; exposure constant
throughout, no skips/failures/resets. Old/new native binaries give bit-identical120-frame moving HDR results.
The initial fast-movement explanation was insufficient: the owner's slow walk reproduces it in both integrations.
Intensity above1 itself produces identical pixels to1. No extra weight/style axis inferred for310.8; round-10.

A proposed tone-zero default was built but withdrawn before deployment after the owner
clarified: "the color grading/DLSS 5 changes are bypassed while moving, and then apply when holding steady?"
They want MORE of NVIDIA's lighting/colour changes and ask whether lowering tone only hides a bug. Treat this
as a motion/history/input investigation, not a taste preference. Tone-zero measurements below show attenuation
of the symptom, not its cause. Trace skips, resets, guides and input/output during movement and settling.
A is the chosen Quality model, not a universal winner. Controls range 0–1; 1 is the documented maximum.

## Controls and placement

The current Style row sends NVIDIA Default/A/B/C as `DLSSNR.Style` 0/1/2/3. Historical A/B/C comparisons
below used 0/1/2, now displayed Default/A/B. Saved selections retain their original numeric values.
We have not established independently selectable weight files. There is no second preset row.
The adapter sends LocalToneStrength, LocalStructureStrength, SkinStructureStrength and UseAutoMask explicitly.
GlobalTone and weight-preset hints are not exposed. Fresh-feature preset hints 0..3 produced identical pixels.
The recorded-tavern GlobalTone probe also produced identical pixels when unset, 0, 0.5 and 1 on signed310.8.
These are version/input-specific measurements, not a statement that future runtimes ignore these controls.

Sources: NVIDIA's [NR adapter](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp) and
[settings](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.hpp), plus a community author's
[310.8 A/B/C measurements](https://github.com/zmodelerlover/dlss5-neural-amd/blob/master/docs/styles-model-abc.md).
The latter used community A/B/C names, not NVIDIA's current labels. Our GPU probe independently checks that
native styles 0/1/2 differ. Style3 matched2 on the probe input, which does not establish universal equivalence.
Style, tone, detail and masking changes reset history. Intensity changes preserve it.

Both choices now run before lens effects, on either side of SR/RR inside HDRP's post-process graph. The
20:44 deployment scopes the upscaler schedule to BeforePost for the gameplay camera, then restores it. NR
therefore precedes DoF, motion blur, bloom, chromatic aberration, vignette and game tone mapping. After remains
the default. Both receive scene-linear HDR: our adapter compresses positive RGB as `rgb / (1 + max(rgb))`,
converts it to sRGB for NR, then decodes and restores using the ORIGINAL input's shoulder factor (NR-17).
Invalid or saturated model output falls back to the compressed source pixel before restoration. This is our
colour adapter, not NVIDIA's demonstrated post-tone-map integration. It changes the model's input distribution;
appearance, highlights and exposure must be judged in game. Passing neutral tests does not settle that question.
HDR display output is still unavailable.

Ray Reconstruction combines denoising and upscaling. While PT uses RR, Placement switches to After Upscaling
and is disabled with that explanation; the preferred placement returns when RR no longer requires it. PT with
no denoiser still disables NR, rather than sending it noisy input. Do not tune NR to hide tracer defects.

## Deployment and functional checks

At 20:44 local time, the Mono plugin and native NR DLL were installed, backed up and hash-verified.
The native binary is unchanged; the 18:21 deployment record is preserved as build/nr/controls-1821-deployment.json.
No PT, FG, BU or bridge binary was replaced in this update. `build/nr/tuning-deployment.json` records:

- Managed: `74D7735B87DCFD8E85DA88130DEE77E49AC9F1E365ABD85C65E26AE13EBA730F`.
- Native: `E7FDE680DCCE5B7214369D4C4721770364702B45A20FE8A3F02C10448E46C9AB`.
- NR ABI `0x00010100`, 100-byte frame packet; native and managed must be updated together.

Both managed game versions and the native target compile. The actual GPU probe passes SDR neutral transfer,
HDR neutral values from 0 to about 981 (maximum relative error 0.001439), live intensity without another reset,
and different A/B/C results. Log: `.testruns/nr/tuning-probe.log`. These synthetic checks do not grade aesthetics.
The 23 policy checks pass and eight deliberately broken policies fail. The eight NR analysis checks (including
nine deliberate faults) and six existing harness analysis checks pass. Source checks: 90/90 on the shared tree. The added GPU
mutations for HDR clipping and ignored model are prepared but have not run yet.

## Captured runs and limits

`artifacts/harness/nr-baseline-20261007`: two scenes, npc-day and forest; real NR evaluations succeeded.
The NPC fallback is a landscape view containing a small character, not a face close-up. The owner moved the
camera in scene 2; exclude forest from model/colour judgements. The last forest take also had a menu open.
The NPC overview has been inspected. Neutral/Off restoration flagged small differences (mean 0.271/0.334 display
levels, worst blocks 1.409/2.688); do not call restoration passed. The weak 15 cm live sway was not matched by
rendered frame between variants. It cannot settle the movement complaint.

The first wrapper run used a relative output path; the game wrote its captures under its own folder. They were
recovered into the repo. The driver now sends an absolute path and rejects unknown scene keys: `tavern-room` is
valid, `tavern` was silently skipped in the first plan. None of the 117 save files changed.

`artifacts/harness/nr-tavern-20261007`: launched 18:21, tavern loading began 18:22:26 and timed out at 18:29:32
after 420 seconds. No comparison frames. NR loaded but never evaluated. Four concurrent PT GPU tests each held
about 2.5 GB at launch; the game's memory budget later hovered around 2.8–3.7 GB. This supports investigating
memory pressure, not blaming the NR model. FoA quit and all 117 saves were unchanged; configs were restored.
Starfield started at 18:30:46, after this failure, so it did not cause it. It was running at the subsequent check.
A retry after Starfield closed was refused by the shared RUNNING lock (another PT run, process 39516). No second
NR game was launched; the other coordinator's run was left alone.

## Successful tavern retry

Run 20261007-192507 finished at 19:32:14: 23 takes, no failed capture steps, 220 seconds including loading.
FoA closed, configs restored, all 117 save files unchanged. Read its `report.md`, not only `done.json`: analysis
flags GPU contention and three restoration differences. Overview, the moving atlas and the full-frame A/B/C and
matched-pose sheets were inspected. Sheets: `nr-tavern-retry/tavern-room/nr-models.jpg` and
`nr-motion-matched.jpg`. The actual NR native/plugin hashes match the 18:21 deployment.

Measured brightness-shift range during matched movement (display levels, central 80%, each pose versus Off):

| Placement / model | Local Tone 1 | Local Tone 0 |
|---|---:|---:|
| After / A | 1.114 | 0.102 |
| After / B | 1.045 | 0.288 |
| After / C | 0.945 | 0.227 |
| Before / A | 3.226 | not captured in this run |
| Off again | 0.022 | n/a |

After/A with Local Tone 0 reduces that measured range by about 91% in this view. Static B lifts the room more;
C changes its shadow balance and reduces fine-detail contrast. Before/A
changes the room more and shows stronger colour changes during motion. The early path actually evaluates
1924x1084, versus 3840x2160 after upscaling; shared transfer textures are 79.56 versus 269.04 MiB. Competing PT GPU
tests reached 98.2% GPU use, so these evaluation timings do not establish a real-frame performance gain.

The largest neutral/Off restoration block (42.896/42.894 display levels at 95.4%, 5.2% of the screen) is the animated
top-right "Points available" HUD text, visibly present in the difference picture. Central RGB absolute differences
on the first frames are 0.151 after-neutral, 0.217 before-neutral and 0.205 Off again. The original strict failures
remain in the report; this does not prove every scene pixel is restored. Repeat with the HUD hidden, rather than
relaxing the check to accept the warning. The small after-neutral block failure remains to investigate.

## Castle and forest follow-up

Run 20261007-194841 finished 19:57:44: HUD hidden, 29 takes each, no failed capture steps, 519 seconds including
loading. All 117 save files unchanged and configs restored. The npc-candle fallback Slot_82 is actually Sagremor
Castle; this is castle evidence, not Ron's close-up. Forest is Slot_37's rainy camp. Output:
`artifacts/harness/nr-followup-20261007`. Both model sheets, the forest motion sheet and restoration differences
were inspected. `nr-models.jpg` uses actual individual frames; a motion overview averages different poses.

| Placement / model | Castle tone 1 / 0 | Forest tone 1 / 0 |
|---|---:|---:|
| After / A | 0.750 / 0.126 | 1.232 / 0.701 |
| After / B | 0.444 / 0.113 | 4.611 / 0.451 |
| After / C | 1.181 / 0.147 | 1.872 / 0.571 |
| Before / A | 2.152 / 0.732 | 5.250 / 1.004 |
| Before / B | not captured / 0.502 | not captured / 1.611 |
| Before / C | not captured / 0.554 | not captured / 1.090 |
| Off again | 0.053 | 0.377 |

Same matched-motion brightness-shift metric as above. After/A tone 0 reduces the range about 83% in the castle
and 43% in the forest. Rain/background variation is material in the forest, as the repeated Off measurement
shows. All moving camera routes match. Before remains selectable for its smaller workload, but its colour
changes are stronger; tone 0 helps there too. GPU contention prevents a reliable real-frame speed claim.

The castle camera shifted 3.6 mm and 0.373 degrees between static takes 13 and 14. Exclude static takes 14-17
from comparison with the initial Off image; the old 79.277-level Off block is not evidence of a rendering defect.
The moving takes start from their own Off reference and match. Analysis now rejects camera mismatches for static
restoration too, with a test and a deliberately broken guard. The original report is retained as
`report-before-pose-check.md`; `nr-static-poses.json` records each take's check. The cause of the camera shift is
unknown. Other neutral differences remain open: castle after/before worst blocks 1.583/2.971; forest 2.655/4.761,
Off 5.593. No relaxed threshold and no claim that restoration passes.

## Partial face comparison

Run 20261007-200519 used named `PT hair-eyebrows` (Slot_94, Ron's Tavern), output
`artifacts/harness/nr-face-20261007`. Loaded after 193 seconds; timed out at 15 minutes during moving takes.
All 117 saves unchanged, configs restored. Static A/B/C and moving A/B exist; C motion and final restoration
did not finish. The report also flags the game's noninteractive UI state from static A onward. The inspected A
frame shows Ron and the room, no visible menu; do not blame owner input or silently waive the state warning.
This is partial evidence, not a clean completed character comparison.

## Motion-blur diagnosis and corrected placement

The old AfterPostProcess route received blurred colour. Run 20261007-202332, eight takes in
`artifacts/harness/nr-motion-diagnosis-20261007`, completed at 20:28. Each cycle moves 48 frames and holds
home for 32, with one warmup cycle. Each blur condition has its own Off reference. Every captured NR frame
evaluated without skips, failures or resets within takes: wholesale bypass or repeated resets did not explain
the effect. Old After/A's NR-minus-Off brightness residual jumped +1.130 display levels between frames 48
(last blurred frame at home) and 49 (first sharp frame). Suppressing blur reduced that jump to +0.095.
Frame 0 is sharp after the preceding hold; it is not a moving-frame reference. GPU contention was recorded.

`UpscalePlacement` now records NR before/after DoUpscalingAndAntiAliasing, with the schedule scoped to BeforePost.
NR and BU compile the same CustomPassRecorder source, preserving IL2CPP's unboxed RenderTargets call. The
bridge managed API/binary did not change. NR now independently requests the shared first-person/Kandra motion
corrections. Existing reversible HDR conversion is enabled on both placements. This differs from NVIDIA's
post-game-tone-map sample and still needs highlight/appearance acceptance; full Local Tone remains 1.

The deployed correction was tested in run 20261007-204410, eight tavern takes, 123.7 seconds, no competing GPU
work: `artifacts/harness/nr-sharp-placement-20261007`. All eight capture steps and the report pass, including
Off restoration. Every NR frame evaluates for the current camera frame and records before the earliest lens
pass (DoF). After/A's stop jump is now -0.163; without blur +0.089. Before/A is -0.204, without blur +0.094.
This strongly reduces the abrupt return of grading when movement stops, without weakening Local Tone.
View-dependent brightness still varies (After/A range 1.507; Before/A 3.204); do not claim all instability fixed.
Model-only timings were roughly 11.3-12.4 ms After versus 3.58-3.60 ms Before, transfer textures 269.04 versus
79.56 MiB. These exclude transfers and the rest of the frame, so are not an FPS claim.

Read the overview, motion-stop-comparison.jpg, motion-brightness.png and motion-diagnosis.json in each folder.
All were inspected. Both runs closed FoA, restored configs and left all 117 save files unchanged.

## A/B/C after the placement correction

Run 20261007-204924 completed at 20:58:46: 20 takes each on Ron (hair-eyebrows), tavern-room and rainy forest,
60 captures in 9 minutes with no competing GPU work. Output: `artifacts/harness/nr-sharp-models-20261007`.
All capture steps completed, but the report has **11 warnings**, so this is not a passed acceptance run.
Model sheets, motion sheets and restoration differences were inspected. NR remains visible
in blurred moving frames; A increases fine-detail contrast, B lifts Ron but strongly darkens the forest,
C is softer on face/stone details. Haze remains visible; preservation of fog density is not established.

Matched-motion brightness-shift range, display levels relative to Off at each pose:

| Placement/model | Ron | Tavern | Forest |
|---|---:|---:|---:|
| After A | 1.766 | 1.501 | 2.385 |
| After B | 2.401 | 2.462 | 5.997 |
| After C | 1.493 | 0.943 | 3.346 |
| Before A | 3.728 | 3.318 | 5.278 |
| Before B | 3.080 | 3.465 | excluded |
| Before C | 2.455 | 1.849 | excluded |
| Off again | 0.024 | 0.076 | excluded |

Ron/tavern neutral checks pass. Static Off-restoration worst blocks are 2.304/1.954 (means 0.089/0.154): the
pictures show small scene/light differences, whose cause is not established. Forest neutral/Off worst blocks
are 4.231/4.215/4.248. Its last three moving takes have noninteractive-state and FOV mismatches: exclude Before B,
Before C and final Off motion; do not blame owner input without evidence. Thresholds remain unchanged.
Valid captures pass the evaluation/order checks. Defaults stay A/After and full strength/tone/detail; no winner
or owner look verdict. FoA closed, configs restored, all 117 save files unchanged. No active run remains.

## RenoDX and PT integration

Unity's [post-process source](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.PostProcess.cs)
places BeforePost upscaling before DoF, motion blur and Uber (tone mapping/CA/vignette).
10-08 follow-up verifies the same ordering in the installed HDRP assembly (`build/nr/pipeline-review`,
ignored decompilation). BeforePost still follows DynamicExposurePass and BeforeTAA custom effects.
HDCamera obtains DLSS's normal schedule from the active pipeline asset's DLSSInjectionPoint; AfterDepthOfField
and AfterPost are supported alternatives. TGNR overrides that schedule to BeforePost while active.
The owner's RenoDX Upscaled run did not record its schedule; the hook label alone does not prove identical
placement relative to post effects. No rendering change follows from this inspection.
The [render graph](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs)
composes opaque fog and transparents before post processing; fog is not a single lens pass to relocate.

RenoDX Unity HDR source was inspected at revision `b16c5d6157b66bf49ee951227a7d99c150208913` in
[NotVoosh/renodx-unity](https://github.com/NotVoosh/renodx-unity/tree/b16c5d6157b66bf49ee951227a7d99c150208913).
Its Uber shader calls GradeAndDisplayMap after scene effects; its final shader encodes output as scRGB/PQ.
Those stages follow our new NR hook. Its texture-format upgrades happen earlier at resource creation.
This source ordering is not a live coexistence test. Public NR add-on source was not found; the upstream DLSS
hook framework alone is not its implementation. The current runs have no RenoDX HDR add-on. Own HDR-mod work
is deferred by the owner; HDR display output remains gated pending validation.

### Renderer ownership: fog and geometry guides

Owner, 2026-10-07: "Keep in mind that the PT mod fundamentally renders the scene differently, including its own
rendering of fog". NR consumes the active renderer's completed scene, including its fog and transparents. With
PT enabled these are PT's responsibility; NR must not assume HDRP supplied them, add fog again, or split the
tracer's surface and volume lighting. Placement selects the side of SR/RR, while both choices precede lens
effects. RR still requires After because it denoises and upscales together.

Depth and motion come from the actual selected DLSS invocation. In 1.2.3 the bridge owns the NGX D3D11
observer formerly private to FG. FG and NR subscribe independently. The packet includes the actual resources,
image regions, vector scale, jitter, reset and depth convention. BU publishes its selected D3D11 RR inputs
at the shared-device handoff and only marks success after writing the upscaled output. NR associates that
packet with the gameplay camera's colour resource; missing, failed or mismatched calls disable NR, without
substituting another camera's guides. Before evaluates before SR; After evaluates after successful SR/RR.
The managed render-graph pass only supplies camera identity, settings and resource lifetime dependencies.

NR-10 / PT-72 remains a renderer migration requirement: when PT changes the visible geometry, the inputs to
DLSS itself must match it. Changing lighting or fog alone does not imply different surface motion. NR must
consume whatever DLSS actually uses, not invent a separate NR-to-PT lookup. PT's published guide API does
not yet export its own geometry depth/motion. Validate hair, glass/water and fog after PT updates that common
handoff. Current scene comparisons use PT Off; the real RR+NR GPU check is synthetic, not PT image acceptance.

## Earlier queued state (resolved)

The owner said "try now". The old harness lock cleared. Run 20261007-192507 queued at 19:25, waited for Starfield
to close, then completed the successful tavern retry above. It no longer owns the lock.

## Repeating the comparison

The owner specifically wants the tavern first, stronger movement, A/B/C, before/after upscaling and Off/On.
The current comparison recipe has 20 takes: Off, After neutral/A/B/C/masking, Before neutral/A/B/C/masking,
Off restored, then matched Off/After A/B/C/Before A/B/C/Off-restored movement. Full tone throughout. Motion moves
65 cm and turns 25 degrees each way over 48 rendered frames, then holds home for 32. One complete cycle warms
history, one is captured. Each frame records pose/FOV, native counters and ordering before lens effects;
analysis rejects mismatched routes and compares each pose with Off. The frozen world can still contain image
differences; do not waive restoration checks.

```powershell
python tools/harness/neural_plan.py build/nr/tuning/compare.json --compare
./tools/harness/Run-PtHarness.ps1 -Plan build/nr/tuning/compare.json -Scenario tavern-room -Out artifacts/harness/nr-tavern-next -AllowGpuContention -KeepFrames -TimeoutMinutes 20
```

The owner's explicit NR override allows background GPU work, but never launch alongside FoA or Starfield.
Respect the shared harness RUNNING lock; another coordinator may have queued a PT run. Do not stop their tests.
Timings with competing GPU work are not a performance verdict. Resolve the excluded forest takes and restoration warnings. Check PT/RR and PT/NRD
combinations once the parallel tracer migration publishes its matching guides (NR-10 / PT-72). The owner's look verdict, neutral restoration,
before-upscaling highlights and speed, FoA UI interaction, standalone, ReShade and FG combinations remain open.

## Actual DLSS inputs, 1.2.3 (21:46 local deployment)

Owner corrected the integration: read guides from DLSS, as FG does. Earlier claims that the common HDRP handles
were equivalent omitted actual NGX metadata. The observer now lives in the bridge; details in the renderer
contract above. NR ABI 0x00010200, frame 100/status 72 bytes; events 4/5 bracket actual DLSS. Bridge Host/managed
APIs stay compatible; additive native observer exports. FG and BU were updated together. All source versions
are 1.2.3. No PT implementation/binaries replaced (version metadata only in source).

Both game versions of NR, FG and BU compile. Synthetic RR/NR/FG coexistence passes: 32 RR, 30 NR, no fallback.
A wrong-camera mutation fails the intended NR-evaluation assertion; the later RR image checks also fail because
the test exits its frame loop early. The legacy NR GPU image probe passes neutral SDR/HDR, model switching,
intensity, alpha, Off and resize. Source checks 90/90; recorder unboxing mutation detected; eight NR evidence
checks and six shared analysis checks pass (with their deliberate bad-evidence cases).

Mono deployment: 42 payload entries plus the signed model, backed up/replaced/hash-verified against build and
scratch install. Evidence: `build/nr/observer-deploy/deployment-result.json`, UTC `2026-10-07T19:46:37.7172483Z`.
PT hashes checked unchanged. No zip, commit or push. Vortex must not revert these replaced files.

- `BepInEx/TGFrameGeneration/Native/TGFG.Native.dll`: `55843C485E3796ACE2E7819FEF78B7685BBCAAAEAE4D7695ACE106E021B996CB`
- `BepInEx/TGFrameGeneration/Native/TGGraphicsBridge.Native.dll`: `B339C546F80C71791CF13E0B269EE228AAF11464572B45577C6889A695BF8BBC`
- `BepInEx/TGBetterUpscaling/Native/TGBetterUpscaling.Native.dll`: `C22320FD65CF6D4AB24B699279D1797E6E5DDC37ECE49F091CE011E32404A7C0`
- `BepInEx/TGBetterUpscaling/Native/TGGraphicsBridge.Native.dll`: `B339C546F80C71791CF13E0B269EE228AAF11464572B45577C6889A695BF8BBC`
- `BepInEx/plugins/TGNeuralRendering/TGNeuralRendering.dll`: `2BD802003A0B639C5B5C63EB88ECFB265D0DF0AA6C81DF91B878CABEC89077A4`
- `BepInEx/TGNeuralRendering/Native/TGNeuralRendering.Native.dll`: `5C5D86F98EB5B9F4AB75E250D95537AEBEA6D3CDCB853F46BF2F90B92B84A197`
- `BepInEx/TGNeuralRendering/Native/TGGraphicsBridge.Native.dll`: `B339C546F80C71791CF13E0B269EE228AAF11464572B45577C6889A695BF8BBC`

Game run `20261007-214804`, `artifacts/harness/nr-dlss-inputs-20261007`: eight tavern motion/hold checks plus
one Ron framing take complete in 2.4 minutes, zero failed captures. One report warning: PT GPU test contention
up to 20.7%, so no performance conclusion. All 117 saves untouched; configs restored and game closed.
The bridge log confirms actual DLSS SR guides: 1924x1084 motion, scale -1924/-1084. NR evaluates every captured
On frame before lens effects, with no skips/failures or history reset during capture. Off restoration passes.
At full tone, the stop jump is -0.103 After / -0.170 Before, and view-dependent brightness
ranges are 1.429 / 3.193. This retains the pre-lens fix; observing NGX is not claimed
to eliminate model colour variation. Face framing verified from overview.jpg: camera 1.8 m closer, FOV 40.

NR-11: Video and FoA now label placement Quality (After Upscaling) / Performance (Before Upscaling), preserving
saved config values. Both remain available subject to RR's denoise requirement. Tune their results separately;
no independently optimized defaults are established yet. Full strength/tone/detail remain 1.
NR-13: `neural_plan.py --face` runs 30 Ron close-up takes, Off/neutral/A/B/C at both placements, automatic mask,
skin detail 0/1, then matching moving/holding comparisons and Off restoration. Close-up movement is 20 cm and
10 degrees; the 65 cm/25-degree tavern route takes the close face out of view. The camera/FOV restore each take.
Run `20261007-215715` completed at 22:01:43, 30 takes in 4.1 minutes, no competing GPU work. All captures
completed, 117 saves unchanged, configs restored. Two report warnings remain: Before neutral mean 0.147 / worst
block 1.533; Off restored 0.152 / 1.665 display levels. Matching motion and continuous current-frame NR pass.
The report's brightness-shift ranges are After A/B/C 2.904/1.534/1.075 and Before A/B/C 4.990/1.944/1.634;
Off-repeat range 0.064. These are colour stability measures, not an artistic ranking.

Reviewed `face-models.jpg`, `face-masks.jpg`, `face-motion.jpg` and the framing overview. A has stronger skin and
shadow contrast; B keeps this face brighter; C softens fine detail. Skin zero with the automatic mask changes
skin texture while retaining other NR changes. Mask Off remains a baseline, not a NVIDIA recommendation or a
chosen best setting. No default change justified across scenes. Both placements stay available; Before needs
its own tuning, and the broad run showed B can strongly darken the forest.

The output folder contained older capture folders. The standard report uses the current request's exact takes;
the first custom sheet mistakenly globbed stale names. Corrected and regenerated it from request.json, then
inspected it again. The launcher now refuses an output directory containing an earlier request.json. Do not
delete older evidence to hide this; use unique output folders and the recorded take list.

NR-12 research: [ShortFuse controls](DLSS5-SHORTFUSE.md), current package inspected read-only. No independent
per-model style selector found. Resolution scaling, repeated passes and colour-encoding controls are follow-up
studies, not silently adopted defaults.

NR-14: owner sees patchy temporal colour/exposure/effect changes. Added paired-Off RGB block time series,
stationary-hold heatmaps and visible-change area curves. A fixed grade is ignored; opposite local shifts that
cancel globally are detected. Nine NR evidence tests pass, including a deliberate broken local-range check.
The changed-area threshold is two display levels per 32-pixel block; it is not the model's internal mask.
Exposure is currently captured as a per-take PT diagnostic, not a per-frame GPU exposure trace. Do not attribute
visible brightness changes to engine exposure without that missing measurement. Reanalysis completed: worst
held-still block RGB range After A/B/C 9.882/5.268/6.505; Before 8.381/9.973/8.211; Off repeated 0.956. After A's
95th-percentile block range is 3.665. The maps show changes around the face as well as elsewhere. Evaluation
and reset counters remain steady. These measurements confirm a remaining local temporal defect; do not
hide it with global averages or lower Local Tone. Next: synchronized per-frame exposure and input/output
colour measurements to separate model history, colour conversion and downstream grading. Keep defaults provisional.

## Live DLSS FG coexistence, 22:10

`nr-fg-observer-20261007-2205` completed but did not test generation: the frozen world correctly suspended FG
with TimePaused. The corrected live run `20261007-221022`, `nr-fg-observer-live-20261007-2210`, completes four
takes in 71 seconds, no report warnings or competing GPU work. NR runs After/Before with DLSS FG Fixed 2x,
then both return Off. Managed status accepts Fixed/Fixed, records 602 captured input pairs, zero native errors
and DLSS-G status 0. SDK output count reaches 3481 versus 2882 source presents (599 extra outputs), with its
completion fence used. This proves generated output, not display latency or an image-quality verdict for FG.
FSR/XeSS FG remain untested with NR. All 117 saves unchanged, configs restored, FoA closed, harness lock released.

## Same-frame stage diagnosis and Unity motion marker, 22:57 deployment

Owner asked what comes next after the measured temporal instability. Added opt-in, synchronous GPU readbacks
of scene input, model input/output, restored scene output, scaled motion and the actual exposure texture.
They are indexed by the same gameplay frame/camera, average every pixel into 64x36 cells, and preserve tuning,
reset, jitter and scale metadata. No readback in normal play. `neural_plan.py --stages` reproduces the plan;
`neural_stages.py <run>` rejects missing/mismatched/invalid records and writes stage plots. See TESTING.md.

Two 16-take runs, each 5.2 minutes, cover Ron close-up and the tavern, Off/neutral/A/B at both placements,
48 moving frames followed by 96 held frames. Both finish all takes, no harness report warnings, 117 saves
unchanged, configs restored, game closed. These are diagnostic captures, not performance evidence.

- Baseline: `artifacts/harness/nr-stages-20261007-2235`, run 223514. Exposure texture is exactly constant in
  all 12 NR takes: X 0.000101725258, Y 13. Colour changes already exist at NR output, before the HDR restore
  and later game processing. NR continues evaluating with no reset during the captures.
- Found NR-15: tiny regions of the actual Unity SR motion input contain X=2. Our adapter passed it on as
  movement, up to 7680 model pixels in held face captures. Unity 6000.0's
  [BuiltinData.hlsl](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Material/Builtin/BuiltinData.hlsl)
  defines X>1 as no motion and decodes it to zero. The NR copy now does that for observed Unity SR only;
  generic/RR motion retains its existing meaning. Still uses the actual DLSS texture, not a separate lookup.
- Corrected: `artifacts/harness/nr-stages-fixed-20261007-2258`, run 225820. Held motion is zero in every face
  take and below 0.00361 pixel per cell in the tavern; all exposure channels remain constant. The real-capture
  test passes all 12 corrected takes and rejects the earlier capture for its unhandled motion marker.

NR-14 remains. The model's local output-minus-input range over the held frames (encoded RGB levels, 0–255):

| Scene | After A | After B | Before A | Before B |
|---|---:|---:|---:|---:|
| Face | 11.55 | 10.01 | 20.19 | 13.26 |
| Tavern | 10.60 | 10.40 | 19.02 | 11.35 |

Neutral strength gives exactly zero model effect. The input itself continues settling: maximum local range
4.2–4.5 levels After, 5.3–7.4 Before. Thus constant exposure and valid held motion do not mean constant input.
Output changes exceed input changes, but this does not yet distinguish model history from a nonlinear response
to changing input. The next discriminating test is replaying an identical captured colour/depth frame with
zero motion, followed by a check of our HDR-to-model colour conversion. Keep Local Tone/Detail/Strength 1.
No overall model/style winner or final default selected. C was compared earlier; this focused run uses A/B.

Reviewed the stage plots, face overview/atlas and tavern output. Within-run movement routes match. Between
runs the tavern matches, but Ron's route differs by 2.6 degrees / 8.28 cm; reject a direct face image A/B
across these runs (NR-06), while each take's held-motion/exposure evidence remains valid. The other agent
also deployed a newer PT binary between runs; PT is Off throughout. Do not attribute changes in face scores
to the motion fix. The broad colour problem remains either way.

Installed NR 1.2.3 at 22:57; only the NR pair replaced, PT/FG/BU/bridge preserved:

- Managed `B433013A0DE9DBB24FB765C347084F4B00ACBEB8C8D3F8A9FA66FF9BE86AAAEE`.
- Native `602A864799A7613C99EE29162C0890100A079348C4C85302C45FDD3F59BACA7C`.
- Deployment: `build/nr/tuning-deployment.json`; prior instrumentation-only deploy archived in
  `build/nr/stages-instrumentation-deployment.json`. Native build, scratch install and game hashes match.
- Preserved current PT: managed `D7AB6A600690...`, native `54165A49E5FD...` (other agent's 22:46 deployment).

Both managed targets and harness compile. Native real-model probe validates known colours, orientation,
per-pixel averaging, motion scale, same-frame exposure, neutral output and generic RR preservation.
Deliberately flipping diagnostic rows fails the CPU reference. Six new stage-analysis tests/five deliberate
faults, nine existing NR evidence tests/ten faults, six shared analysis tests and source93 pass.
The public bridge publisher accepts RR only: an initial synthetic SR publication correctly failed; its test
was corrected to check generic RR there and use real game SR captures for the marker correction.

Scheduling error: a 3.6-second offline probe ending 22:55:53 overlapped PT run 224654. Its overlapping performance
samples are contaminated. `.testruns/nr/run-stage-probe.ps1` now checks games and the shared lock immediately
before execution, including after approval waits; no GPU work was run during the NR capture runs.
Shared FG/BU raw-motion adapters still need an encoding-contract audit (NR-15 follow-up); never apply the Unity
marker rule blindly to future PT vectors. No release zip, commit or push.

## Identical-input replay, 23:28 deployment

The owner asked whether work had stopped. Resumed the pending causal test, rather than selecting defaults
from the earlier unstable captures. Added an optional diagnostic export which retains the first stationary
colour/depth textures and zeros motion. A second mode also requests one NR history reset. The harness turns
replay off with the stage collector in its finally block; no player option or default enables it. NR evaluates
every frame. Stage format v2 adds depth, plus source-frame/reset metadata in the CSV; v1 remains readable.

`neural_plan.py --replay` produces eight takes each for Ron's face and the tavern: Off, ordinary/held/held-reset
After, ordinary/held/held-reset Before, Off. Full Strength/Local Tone/Detail/Skin 1, Model A, mask Off throughout.
The moving phase has 48 frames and the hold 96. The input capture begins at index 49 after movement stops.

Run `20261007-232925`, `artifacts/harness/nr-replay-20261007-2329`, completes all 16 takes in 5.3 minutes.
No capture/restoration warnings, no competing GPU work, all 117 save files unchanged, configs restored and
FoA closed. PT stays Off. Viewed both overviews, the tavern atlas, face stage curves and colour/depth alignment.
These synchronous readbacks do not provide representative performance numbers.

All eight replay takes pass the strict checks: colour and depth grids exactly constant; zero motion; exactly
the intended source frame; no extra resets; the requested reset occurs once in the native evaluation metadata.
Exposure stays exactly constant too. Output-minus-input local RGB ranges, in encoded 0-255 levels:

| Scene/placement | Ordinary inputs | Held inputs | Held + reset request | Held + reset, final 32 frames |
|---|---:|---:|---:|---:|
| Face After | 13.302 | 12.290 | 11.250 | 2.680 |
| Face Before | 15.376 | 14.690 | 8.457 | 2.908 |
| Tavern After | 10.338 | 10.950 | 11.120 | 1.805 |
| Tavern Before | 18.468 | 23.711 | 22.589 | 3.850 |

Without the reset request, final-32 ranges are 3.130/2.934 for face After/Before and 1.516/4.342 for tavern.
The early held frames account for much of the total range, but smaller changes continue afterward. Changing
game input, exposure, HDR restoration and subsequent grading cannot alone explain changes measured directly
at model output with retained resources. This locates the response inside NR for our current input contract;
it does not prove the NVIDIA runtime is defective or validate every aspect of that contract. Reset metadata
records our actual NGX reset request, not an inspection of the runtime's private history. NR-14 remains open.

Next discriminator: `neural_plan.py <new-plan> --replay --long-hold` generates four tavern takes, Off then
600 held frames at each placement with one reset request, then Off. Prepared at
`build/nr/tuning/replay-long-ready.json`; it reproduces the manually prepared plan's takes and scene exactly.
The first launch attempt, output `nr-replay-long-20261007-2336`, stopped before touching game configs or
launching FoA: Starfield started at 23:36:49 during launch preparation. That directory contains a request only,
not evidence. Removed its stale lock only after verifying its exact run ID and exited process. No queued
launcher remains. Use a new output directory after Starfield closes. Then validate HDR-to-model conversion;
keep full tone and do not reset every frame as a workaround.

The public NVIDIA adapter was reread: it consumes tone-mapped LDR and supplies depth/motion to stabilize local
structure, with a one-shot reset. Our older 310.8 profile and reversible HDR shoulder still need appearance
validation; this replay does not make the newer sample's defaults a substitute for that work.
[NVIDIA adapter](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp).
Following its renderer call into `nvpro_core2/nvshaders/tonemapper.slang` confirms the sample's LDR output is
sRGB encoded, not linear LDR. Our sRGB encoding is consistent with that sample; no reason was found to switch
to linear input. The shoulder/exposure mapping still differs and needs separate validation.
[NVIDIA tone mapper](https://github.com/nvpro-samples/nvpro_core2/blob/main/nvshaders/tonemapper.slang).

Installed only the NR pair at 23:28, preserving PT/FG/BU/bridge:

- Managed `7A5E1F9F0014A8FDC3B4B0A1924C63192B15AFF621F79BB6BB687AA244527E7C`.
- Native `DDA8D30644DE1CB1E7B6B593238DC5FF470AD47224EF086AFC7DDA06F453ED13`.
- Current deployment `build/nr/tuning-deployment.json`; 22:57 deploy archived as `marker-fix-deployment.json`.
- Both game versions and harness compile. Real-model probe `build/nr/replay-stage-probe` validates colour,
  motion, depth and exposure readbacks. Twelve analysis cases reject ten deliberate faults. Source93 passes.

No model/look default changed. No release zip, commit or push. Vortex must keep the updated NR pair.

## Long replay completed, 23:50

Starfield closed, so the same prepared test was retried through the launch guard, without another deploy.
Run `20261007-234234`, `artifacts/harness/nr-replay-long-20261007-retry`, completes four tavern takes in
7.1 minutes. All 117 saves unchanged, configs restored and FoA closed. The report's one warning is concurrent
`tgpt_gpu_test` work up to 84.8%, permitted for NR comparisons; timings are not usable. No capture/restoration
failures. The earlier blocked attempt is not counted. Starfield was running again after completion.

Both long replays pass the exact input checks over the 599 measured retained frames (the first stationary
frame is excluded): constant colour/depth grids, zero motion, constant exposure, one requested history reset.
The requested hold is 600 frames. Model output-minus-input range over the entire measured hold is 15.3085
After and 18.1094 Before. Final-32 ranges are 1.3681 and 2.0561, respectively. After the first 96 retained
frames, disjoint 32-frame windows still vary by 1.29-2.38 After and 2.10-3.41 Before. The initial large change
settles, but smaller fluctuations persist. JSON window measurements, stage plots and report are in the run.
Viewed the overview and both stage plots. These numbers are local encoded model RGB, not a perceptual pass
threshold or a claim about the internal mask. No model/style winner follows from one model's causal test.

Next: validate the HDR-to-model mapping/profile against the signed runtime while retaining pre-lens placement,
and compare the same capture across A/B/C once the input contract is settled. Constant-input variation locates
the response inside the evaluation; it does not alone distinguish intrinsic model behaviour from an integration
contract problem. Do not weaken Local Tone or repeatedly reset history to conceal it.

A separate small defect was measured while reviewing the earlier neutral takes (NR-16). The encode/restore
round trip at Strength 0 introduces mean relative linear colour error 0.232-0.237%, worst cell 1.06-1.32%,
with denominator `max(scene_in, 0.001)`. The model effect itself is exactly zero. See
`build/nr/neutral-stage-roundtrip.json` for all four takes from run 225820. The clean fix is preserving the
original scene at exactly zero while still evaluating NR for its history. That fix and its exact-pixel probe
remain pending; it does not explain the full-strength fluctuations. Current source and deployed pair still match.

## Exact neutral colour fixed, 2026-10-08 00:04

Fixed NR-16: with Strength exactly zero, still evaluate the model and preserve its existing history, but do not
write its converted result into the game's scene. Raising strength still avoids an explicit history reset.
Full-strength execution is unchanged. The exact-pixel HDR GPU probe first failed the previous binary at the
intended assertion (maximum relative error 0.001439), then passed the fix with zero error. The remaining
SDR/HDR/model/alpha/resize/guide checks pass too. Logs: `.testruns/nr/neutral-original-must-fail.log` and
`neutral-fixed-exact.log`. This observed old-build failure validates the new regression check.

Installed/hash-checked the NR pair at 00:04. Managed unchanged `7A5E1F9F0014...`; new native
`69444881777037C417E3931FA4E4694D93FFE90367421788EBEC95F299A8DD43`. Build, scratch install and game match.
Previous replay deployment archived in `build/nr/replay-deployment.json`; current is `tuning-deployment.json`.
Preserved the existing PT/FG/BU/bridge files and full-strength visual defaults. Vortex must keep the updated pair.

Real game run `20261008-000447`, `artifacts/harness/nr-neutral-exact-20261008`: four tavern takes in 3.6 minutes,
Off / neutral After / neutral Before / Off. Both NR takes have exactly equal same-frame scene input/output
grids for all 144 captured frames, while actual evaluations continue. The new
`tests/neural_neutral_capture_test.py` accepts them and rejects run 225820 for its old neutral colour error.
Viewed the overview; neutral and Off match. The report's sole warning is permitted concurrent GPU tests
(44.5%), not a capture/restoration failure; timings are unusable. All 117 saves unchanged, configs restored,
FoA closed and no harness lock left. Source93 passes. No release zip, commit or push.

This follow-up totals 24 completed takes (16 replay, four long-hold, four exact neutral). NR-14 remains open:
the identical-input experiments locate remaining variation within model evaluation with our current input
contract. Continue the HDR/input-contract investigation without reducing Local Tone or changing model defaults.

## Input brightness and HDR glare, 2026-10-08 (R9)

Added a diagnostics-only input multiplier, default 1, restored when diagnostics end. The `--signal` plan
tests 0.25/1/4 at both placements in the tavern and Ron close-up, with A and all four model controls at 1,
mask Off. Each take captures 48 moving and 128 held frames after a warm cycle. It is not a player setting.

`artifacts/harness/nr-signal-20261008`, run `20261008-002838`: 16 takes in 6.2 minutes, no report problems.
All 12 NR cases pass stage/route validation. Darker input sometimes reduces temporal changes but also
changes effect strength; it is not a consistent fix. Keep input scale 1. Both final-frame atlases inspected.

The stress comparison exposed NR-17: output restoration inverted the model's newly enhanced peak, turning
near-white model results into excessive HDR glare. Default tavern Before had final-frame source/output
cell maxima 3.36/16.20; scale 4 had 3.41/191.37. These are cell averages, not individual pixel peaks.
For source HDR S and multiplier k, model input before sRGB encoding is C=k*S/(1+k*max(S)). Decoded model
output N now restores as N*(1+k*max(S))/k, using the ORIGINAL input shoulder. The factor is recovered from
retained C, requiring no extra scene copy. No model control was lowered. Strength 0 remains exact.

Installed only the NR pair at 00:44, preserving PT/FG/BU/bridge and matching build/scratch/game hashes:

- Managed: `182C4F803FD8872D7122D5BFE3666278AC2C5C880E052C055202A70E7E5F66DA`.
- Native: `66563493B08EAE1AA2B75742B6CFCAF2BCCAF1CC61231DFACBA53BBE8F226065`.
- Deployment record: `build/nr/tuning-deployment.json`; earlier signal build: `signal-deployment.json`.

`artifacts/harness/nr-hdr-gain-20261008`, run `20261008-004444`: the same 16 takes complete in 9.8 minutes.
All 12 NR cases pass `tests/neural_hdr_capture_test.py`; maximum bound ratio 0.688857. The old capture
fails at 1.995x. The check requires full controls, actual evaluation, matching camera routes and nonzero
model AND scene effects. Its conservative cell bound is mean(output_channel) <= mean(sum(source_RGB))+1/k,
allowing 4% transfer/packed-HDR rounding. This detects the original defect without permitting a bypass.
Fixed default tavern Before final-frame source/output cell maxima are 3.2873/3.16145. Viewed both atlases:
the stress-case white speckles are gone. Owner appearance acceptance remains pending.

Both runs leave all 117 saves unchanged and restore configs before closing FoA. The second report has one
warning: concurrent PT GPU tests reached 97.6%, so its timings are invalid. No capture failures. This round
totals 32 in-game takes. `signal-summary.json`, `nr-stages.json/md` and final-frame atlases retain the evidence.
The scene routes match their own Off baselines; face pose/world time can differ between runs, so do not
treat different runs as pixel-identical source scenes.

Native, both managed game targets and harness compile. Source94, thirteen stage checks (eleven deliberate
faults), nine NR harness checks and the real-model GPU probe pass. The inverse-scale GPU mutation failed as
intended and was restored; the new HDR capture test separately rejects the real old-build capture above.

NR-14 remains: raw model-output colour changes persist. This HDR fix removes amplification after evaluation;
it does not prove the remaining cause or settle the older signed runtime's input contract. Keep provisional
A / After / Strength, Local Tone, Detail, Skin Detail 1 / mask Off. No final A/B/C winner or tuned default.

Prepared `.testruns/nr/model-history-plan.py` and `build/nr/tuning/model-history.json`: A/B/C, both placements,
both scenes, Off bookends, 48 moving/192 held frames, retained colour/depth, zero motion and one reset.
NOT RUN: Starfield started after the finished comparison, then the owner requested a status/handoff.
At 00:56 FoA was closed, Starfield running, no harness lock or queued NR launcher. Next session must recheck
processes before using the guarded launcher. Full PT guide matching (PT-72), normal-version runtime,
standalone install and ReShade/RenoDX HDR acceptance remain open. No release zip, commit or push.

## Model history, signed motion and live forest, 2026-10-08 (R10)

Full evidence and tables: [round-10.md](rounds/round-10.md). All four strengths remain 1; no default change.

- `nr-model-history-20261008` and `nr-model-history-mask-20261008`: 32 valid face/tavern takes, A/B/C at both
  placements, mask Off/On. Retained colour/depth, zero motion and one reset during each hold. C changes less
  After but also gives a gentler effect. Masking gives no consistent stability win. Local maps reveal late
  changes hidden by global means. These runs are not identical source frames across models or sessions.
- `nr-motion-direction-20261008`: four valid tavern takes. Independent tracking of sharp neutral-NR images
  agrees with actual signed NR motion to 0.1055 / 0.1050 capture pixel median After/Before. Wrong signs or
  scales score worse. This rules out a basic motion sign/scale fault in this view, not every guide contract.
- `nr-live-models-20261008`: six valid forest takes, twelve rejected face/tavern takes outside gameplay.
  Final-32 local model-effect ranges After A/C: 2.157/1.893; Before A/C: 4.181/4.828 encoded RGB levels.
  Entire-hold ranges After: 6.589/7.391; Before: 10.688/15.286. Live input varies; exposure stays constant.
  C is gentler, not universally steadier. Viewed final forest atlas. Rejected scenes are NR-18 / PT-26.

Only diagnostic NR code changed: seventh signed-motion grid, format v3 (older captures remain readable).
NR-only deployment 07:20: managed `182C4F803FD8`, native `DEF3E8E3D720`; full hash in round-10 and
`build/nr/tuning-deployment.json`. R9 record archived as `build/nr/hdr-gain-deployment.json`. Three-way hashes
match. Normal-play rendering unchanged; synchronous diagnostics still invalidate performance comparisons.

The harness now rechecks gameplay before freezing/takes/every capture, stopping invalid scenes early.
Build passed, runtime guard acceptance pending. Fifteen stage checks/fourteen mutations, Source95, six
shared analysis checks pass. A retry of face/tavern was blocked by another session's live shared lock;
no NR launcher is queued. Do not infer a keyboard cause or treat black frames as NR output. NR-14, model
selection, PT-72 and compatibility acceptance remain open. Keep A / After / full controls / mask Off for now.

Follow-up: `nr-live-models-retry2-20261008` (092818) completes 12 valid face/tavern takes in 4.6 minutes.
Eight NR stage cases pass. Exposure stays constant. Final-32 local model-effect ranges After A/C:
face 2.943/1.345, tavern 1.513/2.385; Before: face 5.312/4.364, tavern 5.631/5.464. Both atlases inspected.
C has a gentler effect, not a universal stability advantage. A/After remains provisional; full controls stay.

New `neural_temporal.py` follows visible features through motion and measures the NR-minus-Off colour change
at corresponding locations, using matching Off routes and repeated Off controls. Three tests/two deliberate
faults pass. Median moving change After A/C: face .846/.890, tavern 1.369/1.246, forest 1.198/1.384 display
RGB levels; Before .435/.366, .528/.439, .849/.809. Repeated Off .114/.182/.494 respectively. Before varies
less from one moving frame to the next here, but more over the held interval. Do not conflate the two measures.
Motion blur, intentional view-dependent effects and the two-pixel measurement blur limit interpretation;
this is evidence about added colour changes, not a universal defect threshold or proof of stable coverage.

The readiness guard passes all 12 retry takes. Original load-transition cause still unknown. Saves117
unchanged, configs restored, FoA closed; Starfield active afterwards, no NR waiter. One report warning is
concurrent PT GPU tests, so no timing conclusion. No new mod deployment; 07:20 hashes unchanged. NR-14 open.

## R10 follow-up: history-reset diagnostic, 2026-10-08 09:44

The NR-only build adds an opt-in reset on every retained-input evaluation. The six-take tavern plan compares
one reset with repeated resets at both placements, with Off bookends and all strengths 1. It changes no player
default or normal-play rendering. Native `009321545816...`, managed unchanged; build/scratch/game hashes match.
Native probe and Source95 pass; stage tests16/faults15 include actual repeated-reset sequence validation.

Run `20261008-094455`, `artifacts/harness/nr-reset-history-20261008`, failed in take 02 before reaching a
repeated-reset case. The game had loading=False, hero=True, UI=UIState, interactive=False, camera=MainCamera,
timeScale=0. The guard stopped the scenario. No reset comparison result; exclude this partial run from stability
claims. Saves117 untouched, configs restored, games closed, no queued NR run. No automatic retry this turn.

The owner's status question calls for a clear distinction: the earlier placement correction reduced the
specific tavern movement-stop brightness jump about 86% (+1.130 to -0.163 display levels). Motion-marker and
HDR-glare defects were also fixed. Slow colour/coverage changes remain. R10 improved diagnosis, not the
normal-play picture; no final A/B/C winner or general temporal-stability claim. Detailed handoff: round-10.
