# Round 10 - 2026-10-08 - NR model history and automatic masking

The owner resumed: "tokens are back, please proceed". Continue NR-14 at full Local Tone. At this round's start,
installed NR matched the 00:44 R9 deployment. Concurrent PT installed a newer pair at 03:37; this round preserves it.

## Completed comparison

Run `20261008-064920`, `artifacts/harness/nr-model-history-20261008`: 16 takes in 7.2 minutes, no report
problems or competing GPU work. Face close-up and tavern, Off bookends, A/B/C at both placements. All controls
1, Automatic Mask Off; 48 moving then 192 held frames. At hold, retain NR colour/depth, zero motion and request
one reset. All 12 NR cases pass stage validation. Saves117 unchanged, configs restored, game closed.
Diagnostic readbacks still invalidate frame-rate comparisons. These two model runs use the R9 build.

Final-32 maximum local model-output range (encoded RGB levels, 64x36 cell means):

| Scene | Placement | A | B | C |
|---|---|---:|---:|---:|
| Face | After | 1.435 | 12.406 | 1.240 |
| Face | Before | 2.892 | 1.617 | 1.755 |
| Tavern | After | 1.490 | 1.358 | 1.195 |
| Tavern | Before | 2.365 | 1.960 | 1.647 |

Face B's spike is confined to about 0.91% of cells above two levels, near the lower-right edge, not broad
exposure drift. Its worst cell continues a slow rise late in the capture; the global average largely hides it.
C has the smallest full-hold change After in both scenes, but also a gentler average effect. It is a candidate,
not proof of corrected inputs or a universal best model. Viewed both final-frame atlases and face local maps.
Input grids are constant within each retained hold; separate takes are not identical source frames.

## Harness improvements

- `neural_plan.py --model-history` reproduces all 16 prepared takes exactly; `--auto-mask` repeats them with
  Automatic Mask On and full Skin Detail. The recipe is now in the reusable harness.
- Stage analysis checks actual native model, placement, mask and four strengths against the requested values
  every frame. Fourteen checks pass, including thirteen deliberately broken analyser variants.
- `neural_history.py <run>` writes `nr-history.md/json` and per-scene `nr-history-local.png`: late spatial ranges,
  affected area and the worst cell's full RGB curve. Two levels is a reporting threshold, not a pass threshold.
  It requires valid stages, matching camera routes and current-frame pre-lens evaluation.

Mask-On repetition `20261008-065841`, `artifacts/harness/nr-model-history-mask-20261008`, completed 16 takes
in 7.3 minutes, clean report, no competing GPU work, saves117 unchanged and configs restored. All 12 NR takes
validate. After face final-32 A/B/C maxima: 5.781/11.519/7.353, versus Off 1.435/12.406/1.240. Before face:
2.632/1.348/2.142. Tavern After: 1.342/1.576/1.496; Before: 2.257/1.899/1.789. No consistent stability win
from masking, and no default changed. Viewed both overview images. Most cells stay steady; late changes are local.

## Motion direction checked independently

Added a seventh diagnostic grid: signed X/Y vectors after the actual NR scale/orientation conversion, plus
magnitude. Format v3; old v1/v2 captures remain readable. No normal-play rendering change or ABI change.
The real GPU probe now supplies known positive/negative Y motion as well as X; CPU checks verify signs,
orientation and magnitudes. Four corrupted readbacks (X/Y reversal, reversed rows, missing motion) are rejected.

NR-only diagnostic deployment at 07:20, managed unchanged `182C4F803FD8`, native
`DEF3E8E3D720620564112836EA1BD62BD480F9E7E93FB791118C3442EEBA2899`; build/scratch/game match.
The old R9 deployment record is `build/nr/hdr-gain-deployment.json`. CMake built and installed successfully;
the enclosing PowerShell command returned 1 because it treated a CMake deprecation warning on stderr as an error.
The real GPU probe independently loaded the resulting DLL and passed. Other mods were preserved.

Run `20261008-072105`, `artifacts/harness/nr-motion-direction-20261008`: four tavern takes, 1.6 minutes,
clean report, saves117 unchanged, configs restored. Off / neutral After / neutral Before / Off. Ninety-six moving
frames then 32 held, motion blur suppressed, raw screen frames retained. Both NR takes pass stage checks.
`neural_motion.py` tracks current-to-previous image features using OpenCV, independent of NR vectors, rejecting
unreliable forward/backward matches. Median errors are 0.1055 / 0.1050 capture pixel (960x540) After/Before;
relative errors 0.884% / 0.879%. Reversing X gives about 28 px error, reversing Y 0.89 px, halving scale 7.2 px.
Thus a basic sign/scale error is not supported in this view. This is not a universal guide/PT correctness claim.
The tracker also passes two synthetic checks, including known translations and wrong sign/scale alternatives.

## Normal-input forest comparison and failed scene loads

Run `20261008-072639`, `artifacts/harness/nr-live-models-20261008`: 18 takes in 5.9 minutes, full controls,
mask Off, normal changing inputs, 48 moving/96 held frames, matching movement and Off bookends. Twelve takes
are invalid: face/tavern left gameplay, including Off, initially showing black. Exclude those scenes. The
forest has six valid takes, including four NR takes which pass actual-stage/route validation. No competing
GPU work. Saves117 unchanged, configs restored, game closed. Viewed forest `signal-final-frames.jpg` and
face black raw frame. The stage report returns failure for eight invalid NR takes; this is correct rejection.

| Forest | Model | Final-32 local model-effect range | Entire hold range | Final display difference from Off |
|---|---|---:|---:|---:|
| After | A | 2.157 | 6.589 | 6.774 |
| After | C | 1.893 | 7.391 | 5.526 |
| Before | A | 4.181 | 10.688 | 8.073 |
| Before | C | 4.828 | 15.286 | 5.721 |

Values are encoded RGB levels; last column is mean absolute display difference. Exposure is constant in all
four holds. Live input still varies, so these are not constant-input replay measurements. After is steadier
in this scene; C has a gentler effect but is not consistently steadier than A, especially Before. No defaults
changed. A whole-scene brightness average alone would miss the local changes.

NR-18 / PT-26 records the harness failure. Cause unknown; no evidence of keyboard interference. The initial
load readiness window was followed by settling/calibration without a readiness recheck before freezing.
Added checks before freezing, before each take and before each captured frame. A failed check now marks the
scenario failed and stops its later takes; exceptions fail closed. The original half-second load window and
settling durations are unchanged. This prevents invalid evidence, not a claimed fix for the load transition.
Harness builds cleanly; the guard has not yet run in game. Stage analysis has a regression for flagged
non-gameplay frames; removing that rejection makes the test fail. Fifteen stage tests/fourteen deliberate
faults, Source95 and six shared analysis tests pass. Nine NR evidence tests and signed probe passed earlier.

Retry command is ready:
`Run-PtHarness.ps1 -Plan build/nr/tuning/live-models.json -Scenario hair-eyebrows,tavern-room -Out artifacts/harness/nr-live-models-retry-20261008 -AllowGpuContention -KeepFrames -TimeoutMinutes 15 -GpuWaitMinutes 1`.
It did not start: another live PowerShell process (31908) owns the shared `RUNNING` lock, marked `held-run`.
No NR waiter is queued and that session is untouched. Recheck the lock and both games before retrying. The
07:20 NR build/scratch/game hashes still match. No later NR deployment, release zip, commit or push.

## Lighting question

The owner asked how NR knows where lights are. Explained: it infers lighting from the scene picture. Our NR
interface supplies colour/depth/motion, not a light list. PT first computes lighting; NR works from that result.
Off-screen lights reach NR through visible illumination/reflections. NVIDIA describes image-based light and
material recognition and better results from higher-quality traced input in its September announcement:
https://www.nvidia.com/en-us/geforce/news/dlss-5-3d-guided-neural-rendering/

## Continued after the owner said "Well go on"

The PT session still owns the lock; a second retry attempt (`nr-live-models-retry2-20261008`) also stopped
before starting the game. Its owner subsequently started PT run `20261008-092242`. Neither lock was removed.
The new readiness failure now includes loading/player/UI-interactivity/camera/time-scale details, rather
than only saying gameplay ended. Both `Game.cs` and `Runner.cs` build cleanly. The guard from the previous
turn already stopped PT run `20261008-085342` during its first Off take; its report records one failed
scenario, confirming the failure is also seen with NR Off. The cause still needs the new state details.

Rechecked NVIDIA's current sample without changing NR's defaults: `dlss.cpp` supplies reversed hardware
depth for SR and scales its render-resolution vectors into NR pixels. `nvpro_core2/nvshaders/tonemapper.slang`
and `tonemap_functions.h.slang` output sRGB after tone mapping. This supports our sRGB signal choice, not
our particular HDR shoulder or the full 310.8 contract. ShortFuse's UI strings expose `UICorrection`, but no
NVIDIA primary source found here establishes its older-runtime default; no speculative parameter change made.
Sources: https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp and
https://github.com/nvpro-samples/nvpro_core2/blob/main/nvshaders/tonemapper.slang .

## Retry completed: face and tavern

After the PT run released the lock, `20261008-092818`, `artifacts/harness/nr-live-models-retry2-20261008`,
completed all 12 takes in 4.6 minutes. Eight NR cases pass stage/route/actual-control checks; all gameplay
checks pass. Read both final-frame atlases. One report warning: concurrent PT GPU tests (50.8% / 82.4%);
no performance conclusion. Saves117 unchanged, configs restored, FoA closed. Starfield started afterwards;
no further game run or NR waiter. The readiness guard now has successful real-game acceptance; the original
intermittent loading failure's cause is still unknown. PT's installed pair changed independently to managed
`21E6AC180A8C`, native `CA3304BF56E7`; NR remains the 07:20 pair. No mod deployment in this continuation.

Final-32 maximum local model-effect range, encoded RGB levels (live inputs; exposure constant throughout):

| Scene | After A | After C | Before A | Before C |
|---|---:|---:|---:|---:|
| Face | 2.943 | 1.345 | 5.312 | 4.364 |
| Tavern | 1.513 | 2.385 | 5.631 | 5.464 |

The atlas shows A's stronger skin/wood contrast and C's gentler effect. After A remains a provisional baseline,
not a resolved stability claim. C is not universally steadier: it settles better on the face but worse in the
tavern. Before's full-hold ranges are larger in both scenes. Keep full Local Tone and existing defaults.

Added `tools/harness/neural_temporal.py`: tracks features in Off images, compares NR-minus-Off at matching
poses, then follows each feature from current to previous frame to measure change in that added colour.
It reuses the independently checked optical-flow tracker, rejects inconsistent/border matches and validates
captured settings, stages, routes and repeated Off controls. A two-capture-pixel blur suppresses pixel
resampling noise for this measurement only. Three tests pass; ignoring motion and hiding a colour pulse
each make the relevant test fail. Three-scene results are in `nr-tracked-colour*.json/md` beside the runs.

Moving-frame median tracked colour change, display RGB levels (not the held-stage measure above):

| Scene | After A | After C | Before A | Before C | Off repeated |
|---|---:|---:|---:|---:|---:|
| Face | 0.846 | 0.890 | 0.435 | 0.366 | 0.114 |
| Tavern | 1.369 | 1.246 | 0.528 | 0.439 | 0.182 |
| Forest | 1.198 | 1.384 | 0.849 | 0.809 | 0.494 |

Before has less immediate frame-to-frame colour change on these tracked features, but greater slow settling
after movement stops. Do not collapse these into "After is always more stable". These are local feature pairs,
not persistent object identities; intentional view-dependent shading can also change. The tool measures
behaviour, not whether each change is wrong. NR-14 remains unresolved. R10 now totals 54 valid takes and
12 excluded takes, across the five completed NR runs.

## History-reset diagnostic and the owner's status question

Added an opt-in reset on every retained-input evaluation, to compare accumulated NR history against fresh
evaluations of the same image/depth with zero motion. `neural_plan.py --replay --reset-every-frame` makes
six tavern takes: Off, reset once/every at After and Before, Off. Native diagnostic flags accept 0/1/3/7;
7 retains inputs and resets every frame. CSV records the mode; the analyser verifies the exact reset sequence.
No default, ABI or normal-play rendering change. Stage tests16/faults15 pass; managed harness builds cleanly.
Isolated native build/install succeeds (the shell wrapper reports CMake warning stderr as an error; build
log verifies success), and `reset-history-probe` passes the real model GPU checks. Source95 passes again.

Both games closed before deployment and launch. NR-only deployment at 09:44:44, backed up and hash-checked:
native `0093215458167D623D84AA3DA24EF1534C3C563A00B20D0E3A17DF60885C4092`,
managed unchanged `182C4F803FD8872D7122D5BFE3666278AC2C5C880E052C055202A70E7E5F66DA`.
`build/nr/tuning-deployment.json` records it; prior signed-motion deployment is archived in
`build/nr/signed-motion-deployment.json`. No PT/BU/FG/bridge files replaced. Vortex should retain the new NR file.

Run `20261008-094455`, `artifacts/harness/nr-reset-history-20261008`, stopped during its second take:
`loading=False, hero=True, UI=UIState, interactive=False, camera=MainCamera, timeScale=0`.
The guard rejected it and stopped later takes. The repeated-reset cases were never reached: no comparison
result, no stability conclusion. Concurrent PT tests (37% / 35.7%) were authorized; no timing conclusion.
Four minutes twenty seconds elapsed; saves117 unchanged, configs restored, FoA quit, both games closed.
No queued NR launch. Another session acquired the shared lock at 09:52 (46232, held-run); leave it alone.
Starfield is active at the final process check (2768); no NR launch is queued behind it.
The five earlier completed comparison runs still total 54 valid takes and 12 excluded;
this failed partial run does not add a valid comparison. UI transition cause remains unknown (NR-18/PT-26).

During that run the owner asked: "Man, you have tested this a million times. Please give me an update - is
the image meaningfully more temporally consistent now than it was when we started the project?"
Answered candidly: specific improvements, not a solved colour problem. The tavern movement-stop brightness
jump fell about 86% in magnitude after fixing placement (+1.130 to -0.163 display levels). That one metric
is not an overall stability percentage. SR invalid-motion markers and HDR glare were fixed separately.
Slow colour/coverage changes remain. R10 has mostly improved diagnosis, with no further visible stability
fix or universal model winner. A/After/full controls/mask Off stays provisional. Stop this test cycle rather
than automatically launching more model sweeps. No release zip, commit or push.

## Follow-up: motion markers beyond NR and a practical stopping point

The owner asks why the inputs were invalid, whether FG/upscalers share the problem, and whether A/After/full
Local Tone is already as good as this NR runtime gets. Source audit: Unity's X>1 is a valid encoded no-motion
marker, not corrupt data. NR used it as displacement (X=2 became 7680 model pixels at 3840 width). NR now
decodes it to zero only for Unity SR. Its private corrected copy does not alter the game's DLSS input or FG.
Both TGFG_MOTION and TGUP_COPY still forward raw components. Recorded NR-19: an adapter gap with no measured
visual impact yet; do not claim all backends broken or vendor internals known. BU's route includes FSR/XeSS/RR.
The HDR glare was a separate NR-only output conversion defect. No code change/build/game test this follow-up.
Unity primary source rechecked: `BuiltinData.hlsl` on Graphics `6000.0/staging`, lines49-55.

A runtime limit is plausible because constant colour/depth and zero motion still produce changing NR output.
It is not proven while our colour preparation remains a possible contributor. Keep A/After/full tone as the
working baseline and stop open-ended model sweeps; shared-input correctness is the concrete next audit.
No universal model winner or perfect-stability promise. The owner has suggested a possibility, not approved
the picture or closed NR-14. This distinction must survive handoff.

## Owner settles the Quality selection

"Great, so that settles A after upscaling with full Local Tone is the quality setting for the plugin."
This supersedes the earlier provisional Quality selection, without declaring NR-14 fixed. Before/Performance
is the follow-up question: recommend the same A/full Local Tone, Strength/Detail/Skin Detail 1, mask Off.
No distinct Before settings consistently beat this baseline. C is gentler and somewhat steadier in some
face/tavern measurements, but worse in held forest variation (final32 A4.181/C4.828; full hold10.688/15.286).
Before still has larger slow changes after stopping. State that limitation, keep full lighting/colour,
and do not present this recommendation as a completed separate optimization or an owner's approval.
Existing defaults already match; no new code, build, deployment or game test this reply.

## Settled defaults, tooltips and Advanced controls

The owner accepts the proposed settings for both placements, asks for Performance downsides in tooltips,
ways to retain more original colour/lighting, then for advanced models/styles and above1 multipliers.
Recorded NR-20/21. Defaults remain A, all four strengths1, maskOff; Quality remains default placement.
Local Tone and overall Strength stay in the basic menu. Advanced Off/On contains Model A/B/C, Detail,
Automatic Mask and conditional Skin Detail. Both Video and FoA use the same descriptions/order/visibility.
Hiding Advanced retains values and does not reset NR history. Existing config values/keys are preserved.
Performance tooltip explains softer detail and larger colour/brightness settling after movement. Strength
zero preserves the original scene; Local Tone reduces lighting/colour changes without turning Detail down.
There is no established independent colour-versus-lighting slider for this runtime.

Current NVIDIA sample rechecked: `dlss.cpp` lines674-678 expose 0..1 strengths. Its newer styles/global tone
do not establish an older 310.8 contract. Our tested A/B/C choices use one Style parameter. Above1 is still
open, unverified and unexposed, not silently clamped behind an overdrive label. No post-effect amplification
added or claimed to run the model harder. Starfield is active (33440), so no GPU check, deployment or game.

Both managed versions compile cleanly after restoring each target's dependencies (the initial no-restore
attempt found stale single-target assets; resolved). Source95 passes. Native unchanged, installed pair still
09:44; newer managed builds recorded in `build/nr/advanced-menu-build.json`, ready for NR-only deployment
when both games are closed and the shared lock is free. Real-game menu acceptance remains pending.
No release zip, git commit or push; "commit to these settings" means settle the defaults.

## Repeated passes (NR-21), after the owner's clarification

The Advanced/tooltips build was subsequently deployed at 10:31:18, managed527E002E8426 and native009321545816;
both game versions build. An isolated native-intensity probe at1/1.5/2 gave bit-identical full-strength pixels,
while its lower-value positive control differed. The owner then clarified: "Yes of course it's capped at 1.
The people going '2x' and above is applying DLSS 5 in multiple passes". Do not repeat that unrelated probe.

Implemented Advanced Passes1-4, default1, in matching Video/FoA rows. Each pass has a distinct NGX feature and
history on the shared device; output becomes the next input. Prepare colour once and restore the original
HDR shoulder/alpha once, only after the final successful stage. Strength0 keeps exact scene bits; Off releases
all stages. Pass count changes recreate/reset the chain. The owner asks whether Strength is per-pass or final
opacity: per-pass, as are tone/detail/masking. Tooltips say so. Two half-strength passes are not equivalent
to one full-strength pass. Settled Quality/Performance defaults remain A/full/maskOff and now explicitly1pass.
NR ABI0x00010300, frame104/status80; status adds actual model evaluations, CSV adds requested passes. Existing
comparison recipes set Passes1, so player overrides cannot pollute the earlier single-pass comparisons.

A naive oracle (separate calls through scene decode/encode) disagreed at2passes: max0.011925668, mean0.001617527
linear RGB. Investigation used scratch raw readbacks. First-pass inputs/outputs exactly matched; second-pass
inputs differed below1e-6 due to transfer rounding. Alpha was also different but fixing alpha did not change
the mismatch. Fresh features and separate queue submissions did not help. Feeding the exact encoded first-pass
result to the independent call matches exactly. This is input sensitivity in this fixture, not proof that it
explains NR-14. Removed intermediate submissions; no tolerance relaxation and no production test export.

`tests/neural_multipass_mutations.py` builds scratch copies with a raw-output helper appended only to the fixture.
The production chain matches independent calls exactly at1/2/3/4/1passes and Strength0.5/1; neutral SDR/HDR,
alpha, Off, per-frame invocation count and retained history pass. Broken original-input-each-pass, wrong-final-
output and one-evaluation cases fail for their intended reason. Evidence: `build/nr/multipass-check/*/result.txt`.
Policy baseline+10 faults pass, stage17/faults16, harness9, Source95 pass. Both managed builds clean. Final native
build/install succeeds in build/nr, with a new log of pass count and actual model calls. No shared-mod deployment.

At11:07:54 the independent PT session acquired artifacts/harness/RUNNING (47480, held-run). Respect it.
NR pass update is built but not deployed. Installed pair remains10:31.
At11:12 the PT lock is run20261008-111121 and FoA49948 is running. No NR launch or deploy. A focused one-tavern/seven-take game
plan is ready at build/nr/multipass-plan.json: Off, Quality1, Quality2 at half Strength, Quality4,
Performance1, Performance4, Off. No broad model sweep. No NR game process or queued launcher.

## Deployment, tavern check and one-pass feedback (11:22-11:30)

Owner: "Try now". PT completed its report at11:22 and cleared the lock. Both games closed. Rebuilt Mono
and deployed only NR managed/native11:22:57; manifest and build/install/game hashes verified.
ManagedE3868ED67C0B0EC33E17693833FFCA83F07D3131E105859B69E98BC2B6249B34;
nativeE011F0C82733921F840F05695835D081950E3A8ED811790C42A12EDC94F87A7C. Backup:
BepInEx/TGNeuralRendering/Backups/20261008-092257-779-tuning. Metadata tuning-deployment.json and
multipass-deployment.json; previous10:31 archived advanced-menu-deployment.json. Normal build clean too;
multipass-build.json. PT/BU/FG/bridge preserved. Vortex should retain the changed NR files.

Shared harness run20261008-112319, artifacts/harness/nr-passes-20261008: one tavern/seven takes. GPU gate
passed, no other3D programs during the run. Loaded28s, all seven completed,0 failed. Five NR takes each
record65 evaluations; all native pass counts/tuning/placement match. No captured skips/failures/extra
resets; before-lens flag true. Off restored: matched brightness shift0.001, range0.026 display levels.
Exposure0.000101725258 /13 constant throughout1pass, equal to the earlier run. Stage5 valid,0 rejected.
Read overview and Quality1 atlas:4pass changes are much stronger; floor/table/clothes brighter and cooler.
This is functional evidence, not owner's quality approval. Synchronous readbacks forbid performance claims.
Game quit11:25:13;117 saves unchanged, configs restored, lock cleared, no queued NR launch. Video/FoA menu
appearance awaits direct viewing; this harness changes config and validates actual rendering.

Owner asks why1pass looked far less stable. Recorded NR-22. Short functional recipe changed motion48 to16
frames, settling60 to32, held96 to16. Actual Quality1 sweep1.1528324s vs6.000732s in
nr-live-models-retry2-20261008; yaw changes9.5674 vs3.26326 degrees per frame. This is much harsher motion;
I should have kept timing consistent for a quality comparison. Evaluation counts do not prove stability.
Current held model-effect range7.8653 Quality1,13.6524 Performance1; different hold lengths/input motion
forbid direct quality ranking against the old run.

An isolated dual-ABI probe runs120 full-A HDR frames: held / translated / held, matching depth/motion,
one initial reset, no skips/failures. Old009321545816 is the actual backed-up installed native binary,
newE011F0C82733 the deployed binary; same signed runtime and installed bridge. All120 output frames are
bit-identical (max/mean error0). Evidence: build/nr/single-regression/comparison.json; preparation script
.testruns/nr/prepare-single-regression.py. No game swap/deploy for that comparison. Faster movement exposing
existing NR colour instability was an initial hypothesis, superseded as sufficient by NR-23 below. NR-14 stays
open, defaults settled. No more game runs started, no broad model sweep, no zip/git commit/push.

The owner now wants to play personally and judge stability. No further NR GPU tests, deployment or game
launch while they do so. All NR harness work is stopped; wait for their feedback.

### NR-23: owner slow-walk feedback and RenoDX comparison, 11:50

Owner reports severe shadow/darkness changes around tavern chairs while walking FORWARD slowly, at both
Local Tone minimum and maximum. Performance is visibly steadier; RenoDX DLSS5 is much steadier than TGNR.
Earlier TGNR playtests also appeared steadier. Treat as possible integration regression, not a model limit.
The latest synthetic binary comparison only narrows the multi-pass hypothesis; it does not validate the
game inputs. Attribution to camera speed was too strong and corrected in the conversation.

Read-only audit: both placements currently use max-RGB Reinhard compression then sRGB before the model;
output uses the original compression factor after NR-17. NVIDIA's current vk_gltf_renderer sample instead
feeds eImgTonemapped. It explicitly accepts low-resolution depth/motion with display-size NR, so different
guide dimensions alone do not prove a bug. Neither observation validates signed310.8's exact input profile.
No implementation/default change, GPU probe, deploy or game launch in this follow-up.

The owner confirms RenoDX **Upscaled**, not Present, and offers a fresh comparison now. Stop NR GPU/deploy
work during their test. Suggested TGNR Off, RenoDX Upscaled/A/one pass/full Local Tone, same chair route.
Saved ReShade.ini is not proof of current hook choice; ReShade.log is dated09-26. Do not blame double NR.
Public current RenoDX NR source still not found. A public OptiScaler fork attributes its Neutwo encoder and
colour composition to RenoDX, but that is not proof of the owner's add-on version; nothing copied.
Sources: [NVIDIA NR evaluation](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp),
[tone-mapped input](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/renderer_pathtracer.cpp),
[community colour adapter](https://github.com/Dagherbou/OptiScaler_DLSSNR/blob/dlss-neural-rendering/OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl). Further work should isolate
colour conditioning from guide/history behaviour with a slow forward route, not another A/B/C sweep.
Source95 passes after documentation updates. No binary or game config changed.

### NR-23 owner comparison result, 12:04

The owner thoroughly tested RenoDX DLSS5 Upscaled, including all settings, with and without RenoDX HDR,
our graphics bridge and FG. With matching settings it is "actually just as unstable as our implementation".
This supersedes the earlier RenoDX-better impression. The symptom does not require those optional
components. Root cause remains unknown; do not claim a proven NVIDIA defect or an unfixable ceiling.

Reproduction: `PT eyebrows-flickering` save, two seated men in the tavern corner; turn180, walk slowly
forward toward the chairs where the lady sits. Shadows/exposure/lighting shift around those chairs.
Do not confuse this view with the Ron close-up (`hair-eyebrows`) or the previous tavern sweep. Exact route
recorded in NR-23 and tuning notes, not yet automated or captured by the agent. Other games being steadier
is the owner's observation; no matched cross-game measurements exist here.

Settled A/full/one-pass defaults remain. Treat remaining instability as shared behaviour to investigate,
not as a demonstrated regression unique to TGNR. The next useful stability test must reproduce this route
and isolate a concrete common input/model hypothesis; another broad model sweep is not warranted by this
report. No new build, GPU test, deployment, game launch, configuration change or commit in this follow-up.

Owner suggests we might improve on RenoDX with game-specific integration; base-lighting issues could
provoke NR's unstable changes. Recorded as hypothesis in NR-23. Constant-input replay already prevents
explaining all drift by changing base lighting, while leaving the colour/input contract open. Future chair
captures should separate source changes from changes introduced/amplified by NR before altering rendering.

Owner asks if Unity runs post-processing before DLSS and whether both integrations occupy the same place.
Read-only inspection decompiled installed HDRP's HDRenderPipeline and HDCamera into ignored
build/nr/pipeline-review. RenderPostProcess (lines13705-13806) agrees with official 6000.0/staging:
exposure/BeforeTAA custom effects, optional BeforePost upscaling, DoF, optional AfterDepthOfField upscaling,
motion blur/bloom/Uber grading+tone mapping, optional AfterPost upscaling, final pass. HDCamera's
UpsampleSyncPoint reads the active pipeline asset's DLSSInjectionPoint; the value in the owner's RenoDX
run is not recorded. TGNR BeginPost explicitly forces BeforePost, restores on exit, and brackets that
upscaler. Correct the broad "before all post-processing" wording: exposure/custom BeforeTAA effects remain
upstream, as do scene lighting/fog. This is verified source ordering, not a measured cause of NR-23.
No production-code/config/deployment/GPU change. Exact pipeline comparison still needs the runtime schedule.

Owner adds that RenoDX DLSS5 Present also has the same problems. This weakens placement relative to post
processing as the sole cause: early TGNR and both RenoDX Upscaled/Present reproduce it. No reason from
this observation to move TGNR later. Shared scene/input/model behaviour is still unresolved. Documentation
only; no new run or deployment. Preserve the exact chair route for a targeted investigation.

### Quality-only chair capture and HDR clarification,12:30

Owner narrows investigation to Quality, asks whether colour space explains instability and whether
tone-mapped input means NR cannot support HDR. NVIDIA's public `src/dlss.hpp` explicitly says NR expects
LDR input and evaluates after tonemapping. That sample is evidence of its LDR path, not a complete HDR
contract for the signed310.8 DLL. General tone mapping can target HDR displays. Our current adapter
compresses pre-exposed linear scene colour to sRGB LDR and restores the original input scale after NR;
it does not feed the game's actual graded/tone-mapped image. Arithmetic success is not proof of correct
model conditioning or HDR appearance. Unity native HDR output is disabled by Session.HdrActive, while
RenoDX HDR can run with that value false. External HDR compatibility remains unvalidated.
Source: https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.hpp .

Harness-only edits: CameraSlide/Request add walkForward, a monotonic forward translation over motionFrames
followed by an endpoint hold. Runner skips the swinging route's warm-up cycle for walks; static-restoration
analysis excludes them. `tools/harness/neural_chairs.py` prepares the exact save plus180deg view,2m over
144 moving/96 held frames: Off, Quality Strength0, Quality Strength1, Off. Full A/one pass/maskOff.
Camera metadata checker rejects six synthetic corrupt paths;2 chair checks and9 existing harness checks
pass, Source95 passes; managed harness builds clean. Real camera-path validation still required.

Run20261008-122849 (`artifacts/harness/nr-chairs-quality-20261008`) passed GPU gating and loaded SaveSlot96
in27.5s, but all four takes failed their first screenshot: CaptureScreenshotAsTexture returned null.
No captured frames, no route/colour/temporal verdict. NR evaluated once in each enabled take; this is not
comparison evidence. RenoDX Unity HDR loaded, RenoDX NR absent; hdrOutput false. Do not attribute the
capture error to either NR or HDR without testing. Record as NR-24. Game quit12:30:12;117 saves unchanged,
configs restored, lock cleared. Only harness DLL installed, no NR pair or PT deployment. Next isolate
capture failure, then validate the view/poses before interpreting the Quality comparison. No broad sweep.

### Continued chair investigation,13:48

Owner says to continue working while answering questions, then confirms ReShade is off again. The PT
session independently had null screenshots with ReShade on; run124542 then saved four scenes with it
off. This narrows NR-24 without proving which resource/copy fails. No speculative capture fallback.
FrameGrab adds a scoped Unity log listener, so native warnings/errors accompany capture failures even
with WriteUnityLog false. Run-PtHarness keeps the default PT guard, adds AllowReShade only for explicit
NR recipes, and starts saved/remembered NR mode Off; the existing config backup restores both.

Added neural_chairs.py --report: require four matching forward walks, Quality/A/one pass/full controls,
same-frame stage data, exact source/output grids at zero Strength. It writes route/speed measurements,
input/output cell ranges and a six-row atlas (Off screen, model input/output, Quality screen). Cells are
64x36 averages, not pixel clipping evidence. Three chair tests pass and reject nine faulty data cases;
harness9 and source95 pass. Managed harness builds clean; PowerShell parses. No NR binary/default change.
Native call audit: observed jitter is diagnostic metadata, not an NR parameter sent by this adapter;
do not attribute constant-colour/depth/zero-motion replay drift to varying jitter merely from CSV fields.

Bounded queue launched: .testruns/nr/queue-chairs.ps1, tool session23884. Waits for both games closed and
the shared PT lock, then calls only Run-PtHarness (normal5s GPU gate, no shared mod deployment) and runs
stage/tracked-colour/chair reports. Two-hour queue limit; no auto retry after capture/analysis failure.
Local cancellation marker build/nr/chairs-stop; shared STOP also works before launch. Intended output
folder recorded in build/nr/chairs-queued-output.txt on launch. At13:48 PT run130029 has finished32 scenes
and quit but retains its lock for analysis; Starfield has started again. NR queue has launched no game.
Do not start a duplicate. Read captured atlas and metadata before concluding anything about NR quality.

### Valid chair evidence, then GPU-contended retry stopped by owner

Run20261008-135437 (`artifacts/harness/nr-chairs-quality-20261008-135437`) passed the normal GPU gate,
with no competing3D processes. ReShade off; installed NR hashes unchanged (managed E3868ED67C0B,
native E011F0C82733). Slot96 loaded in30.4s. Four takes/960 frames: Off, Quality Strength0, Quality
Strength1, Off. A/full controls/maskOff/one pass,144 moving/96 held frames,2m forward. Each NR take has
240 actual evaluations, Off0; neutral scene input/output grids exactly equal. All camera poses match.
Read `chair-view-check.jpg` and `nr-chair-stages.jpg`: correct view towards the lady and chairs. The
stage sheet shows Off screen, model input/output and Quality screen. Model cells average64x36 regions;
they cannot establish pixel-level clipping or fine detail. Full NR adds contrast and shifts ceiling colour.

The general analyzer initially called the requested forward walk accidental camera movement (four false
problems). It now calls the independent check_walk validator for explicit walking, retaining the existing
guard otherwise. Reanalysis of the same files gives0 problems, without another capture. Regression:
general analyzer6, chair3 and source95 pass; chair checks reject nine corrupt-evidence cases.

Held exposure range0. Full model input max cell range3.8591 RGB levels; model-effect max range9.4240,
final32 range2.7885. Neutral model-effect range0. Tracked-feature changes (1078 pairs): p95 full1.699,
neutral0.429, Off-restored0.388. The worst whole-image cell is on the ceiling, not a chair; approximate
chair ROI input max range2.955/effect6.757, median RGB temporal std0.343/0.305. Do not infer uniform
amplification or blame all variation on NR. Source variation remains, and view-dependent changes can
be intentional. Extra variation is already present at raw model output, before HDR restoration; this
does not prove our input conditioning correct or identify a vendor defect. NR-14/23 remain open.

Moving durations Off8.902s, neutral11.575s, full11.424s, Off-restored9.177s: NR speed0.173/0.175m/s.
Readbacks affect timing; no performance verdict. Owner: "Move a little faster than that." Current plan
changes144 to96 moving frames at the same2m, keeps96 held; actual wall-clock speed needs checking.
Original queue23884 ended after the false analyzer failure. A new GPU-idle waiter31670 was canceled
before launching (`nr-chairs-quality-20261008-135910` is only its preparation folder).

Run20261008-140059 (`artifacts/harness/nr-chairs-faster-20261008`, session91350) used earlier permission
for GPU contention, but PT tests used up to58.4% of the GPU. Only Off completed; the owner stopped FoA
during neutral: "Had to stop the game because the GPU was too busy". Exclude this run from comparisons
and faster-speed claims. The launcher finished, all117 saves are unchanged, config backup restored and
removed, lock cleared. Process-command-line check confirms no remaining NR/harness waiter. Starfield
was running at the cleanup check; leave it and other agents' jobs alone. Future NR runs use the normal
five-second GPU-free gate; do not reuse the earlier contention permission. No automatic retry pending.
No NR rendering change, default change, release zip or deployment in this follow-up; only harness work.

### Resumed14:14: faster motion, pixel ranges, one pending tone-curve comparison

Owner: "You can work again now". GPU free, both games closed. Run141444 in
`artifacts/harness/nr-chairs-faster-resume-20261008` completes four192-frame takes,96 moving/96 held,
same2m route. Full NR0.261m/s, about49% faster than135437's0.175. Matching camera routes, exact neutral,
steady exposure, no GPU contention,0 report problems. Full held model-effect range8.7390, final32
range2.6953. Tracked p95 full2.015, neutral0.399, Off0.404 RGB levels. Read its atlas. Different movement
per frame prevents treating the higher per-frame change as a regression from the slower capture.

Added actual pixel-range diagnostics to test whether Output's white-result fallback causes changing
coverage. Stage binary v4 keeps the seven old grids and adds input/output min/max, negative fraction,
and the exact decode/white fallback predicate. The reduction checks all pixels, not extrema of cell means.
Report fractions weight by cell pixel count (including uneven extents). Normal play performs no readback.
Initial focused probe caught an exposure texture view regression introduced by the diagnostic edit;
corrected before deployment (mode2 retains its RG32 view). Fixed probe validates GPU extrema against
independent CPU input values. New CPU weighting check passes; no broad GPU suite or mutations run.

Only NR pair deployed14:23, preserving PT/FG/BU/bridge. Managed remainsE3868ED67C0B; native now
8E9A85D9517160A3C05B628D94C65E8C8F13EACC66ADA61053C9FFAB00731E8D. Build/scratch/game hashes matched;
backup `BepInEx/TGNeuralRendering/Backups/20261008-122304-450-tuning`, manifest in
`build/nr/tuning-deployment.json`. Vortex must keep these files. No normal-play appearance/default change.

Run142304, `artifacts/harness/nr-chairs-pixel-range-20261008`, four192-frame takes,0 problems and no
contention. Same matching route; full speed0.293m/s. Both NR takes evaluate192frames; neutral exact.
Full model input0.01238979..0.99196434; output-0.00064514..0.9727249. Zero HDR fallback pixels across
all captured full-strength frames; at most one slightly negative pixel per frame. Neutral has no negative
or fallback pixels. Held exposure constant, effect max9.3435/final32 range2.5752. Tracked p95 full2.050,
neutral0.412, Off0.451. Atlas inspected: correct chairs, colour changes remain; no new stability fix.
This rules out the white fallback in this route, not the complete colour contract. Both runs quit, restore
configs, leave117 saves unchanged and clear their locks. Total this continuation: eight valid chair takes.

Reread NVIDIA renderer/dlss and `nvpro_core2/nvshaders/tonemapper.slang`: sRGB convention is consistent
with ours (already noted above); no reason to silently switch to linear input. Our peak Reinhard mapper
still differs from the sample's tone mapping/grading. Added one diagnostics-only comparison, not a sweep:
peak Reinhard C=S/(1+max(S)) versus per-channel C=S/(1+S). Restore with ORIGINAL input scale in each
case, never invert the enhanced output's peak. Tests channel coupling, not exact NVIDIA/game grading.
Setter only works with active diagnostics/no retained replay, clears at shutdown, and requests one initial
reset in BOTH comparison modes. Analyser requires observed mode plus exactly that reset sequence.
No player row/default, no frame/status ABI change (additive diagnostic export). Shader/controller changes
are isolated until deployment. Prepared Off/current/alternate/current/Off, full A/Quality, same fast route:
`build/nr/chairs-tone-map-plan.json`. It needs a new output folder because the first request folder exists.

Native and Mono/normal-version NR and harness compile. Managed builds have NU1900 warnings because the
NuGet advisory endpoint was unavailable; no compile errors. Initial unqualified --no-restore build failed
on stale cross-runtime restore targets; rebuilding explicitly per runtime succeeded. NR GPU probe before
the final matched-reset addition passes both conditioning curves and near-neutral HDR inverse checks;
the final tiny reset addition compiled, its new metadata CPU check passes, GPU confirmation still pending.
Final isolated nativeC2D1289A2B2373B5B9EC14C5998E62FED1093CB66FC5F4910322ECAC8EFD748C;
Mono8D4AB9BE3EDF4A4B8808646878E3440CE9C513384E8FF663A981542ACF263391. Source95 passes.

Starfield38756 started before final probe/deploy: both correctly refused, no files installed. The subsequent
launcher waited at its GPU gate; canceled session76444 before game start/config changes because the new
NR pair had not been deployed. Removed only its exact stale lock49828/run143429 after confirming PID
exited. `nr-chairs-tone-map-20261008` contains a request only, NOT comparison evidence. No queued retry.
Installed game still has pixel-diagnostic pair14:23, settled normal look. No release zip/commit/push.

### Tone-curve comparison completed after Starfield closed,14:44

The final process check found Starfield had closed. GPU gate then free. Final focused probe
`build/nr/tone-map-final-probe` passes including both curves and automatic restoration. NR pair deployed
14:41 with matching build/scratch/game hashes: nativeC2D1289A2B23 and Mono8D4AB9BE3EDF (full hashes above).
Backup `BepInEx/TGNeuralRendering/Backups/20261008-124130-966-tuning`; other mods preserved. Launcher
rebuilds/installs current harness (0 warnings/errors). No player default or normal-play mapper changed.

Run144131, `artifacts/harness/nr-chairs-tone-map-resume-20261008`: five192-frame takes,0 problems, no
GPU contention. Off/current/alternate/current/Off, matching2m camera routes, actual modes0/1/0, exactly
one initial reset per NR take, A/full/maskOff/Passes1/Quality unchanged. Speed0.292/0.295/0.293m/s for NR.
`neural_stages`, `neural_temporal` and `.testruns/nr/tone-map-report.py` pass; contact sheet inspected.

| Measure | Current | Per-channel | Current repeat |
|---|---:|---:|---:|
| Tracked moving RGB p95 | 1.965 | 1.977 | 1.983 |
| Mean absolute screen effect from Off | 3.852 | 3.879 | 3.849 |
| Held model-effect max cell range | 10.019 | 8.961 | 10.100 |
| Final32 model-effect range | 2.394 | 2.552 | 2.948 |

All held exposure ranges0. Current fallback count0; alternate at most ONE pixel per frame (1.21e-7
fraction), so neither explains changes across patches of chairs. Slightly lower full-hold maximum alone
does not establish a fix: moving changes sit within baseline repeats, late held changes overlap, and the
atlas remains visually similar. Overall effect is not simply attenuated. Keep original mapping/defaults;
per-channel compression did not provide a meaningful stability win in this route. This does not establish
all tone/grading preparations equivalent or prove an unavoidable vendor limitation. Internal diagnostic
remains available for reproducing the measurement, default-disabled and restored after capture.

NR run quit14:44:00,117 saves unchanged/configs restored. Session70887 completed. No NR waiter. Another
session acquired lock37956/run144459 and started FoA30500 after ours: leave it alone. Current FoA running
does NOT mean this NR run continues. This resumed work totals13 valid takes in three short game runs;
the canceled143429 request is not evidence. No new stability fix, no release zip/commit/push.

## ▶ Starter prompt (R11)

Quality investigation: new evidence rules out our highlight fallback for the current chair route and finds
no meaningful win from per-channel Reinhard. Do not repeat this comparison or another broad model sweep.
Read144131 contact sheet, nr-tone-map-comparison.json and nr-stages/tracked-colour reports. Current
defaults remain A/After/full/maskOff/Passes1; Performance available but outside owner's current focus.
Extra colour changes already at model output, with valid-range input and steady exposure; this does NOT
prove every part of colour/grading preparation correct or NR inherently unfixable. Any next rendering
change needs a new, concrete hypothesis; actual game grading is still different from our input mapper.
Installed14:41 nativeC2D1289A2B23/managed8D4AB9BE3EDF, diagnostics-only additions; normal look unchanged.
Both managed builds/harness compile, focused GPU probe and Source95 pass. Stage v4 has nine grids;
neuralToneMap diagnostics explicitly reset history once for matched comparisons, normal mode is -1.
No NR queue. Another PT session held lock37956/run144459 and FoA30500 at last check; preserve it. Recheck
both games/shared lock and use normal GPU-free gate for any later run. No native HDR/RenoDX HDR claim.
NR-14/19/24/PT-72 and Video/FoA/standalone/normal-version/FG/ReShade-HDR/PT acceptance remain open.
Preserve parallel PT edits. No zip/commit/push requested. Vortex must retain updated NR files.
