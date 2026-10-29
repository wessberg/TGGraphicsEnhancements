# Round 8 — TG Neural Rendering — 2026-10-07

The owner asked to implement both DLSS 5 documents as TGNeuralRendering, independent of the other mods and
ReShade, while sharing their bridge and pipeline. They explicitly required the Video row to grey out unless DLSS
is selected. Another agent was changing PT in this same tree throughout; its edits were retained.

The loose `Downloads/nvngx_dlssnr.dll` failed NVIDIA signature verification. The owner then supplied
`Downloads/DLSS310.8.0-Streamline2.13.zip`. Its NR DLL has a valid signature and differs from the loose file.
It is copied into ignored `DLSS-NR-Runtime/`, not a package. Exact hashes and profile are in
[DLSS5-IMPLEMENTATION.md](../DLSS5-IMPLEMENTATION.md).

Ordinary public NGX works. No identity trick or direct call into model exports was necessary. Feature discovery,
creation, real GPU evaluation, resize, release and reactivation ran on the bridge's one D3D12 device. The scratch
query returns `0xBAD00002`; normal creation without scratch succeeds. This is recorded, not disguised as query
success. The loaded runtime path and signature are verified.

Native NR, bridge, FG and BU compile. NR, modified FG and BU managed code compile for net472 and net6.0. The shared
scene pass establishes NR before FG capture; the initial search path and bridge NGX mutex allow coexistence with
Streamline and RR. UI code adds Off/On and Strength, with DLSS, SDR, runtime and PT-input gates and saved preference
restoration. The first profile leaves model/style/mask defaults alone. Every source version moves to 1.2.2.

Evidence: 20 NR evaluations across float and sRGB formats, two sizes and three strengths. Neutral float error below
0.0000003; sRGB neutral error zero byte levels, including alpha. Streamline runs on the shared device; 32 RR frames
and 30 following NR evaluations succeed without RR fallback. NR release/restart leaves RR alive. Missing and
rejected DLLs leave a usable bridge. Invalid viewport leaves source pixels unchanged. Six policy mutations and two
real GPU output mutations are detected. Source checks initially 89/89; autoload checks 12/12; filesystem package checks 98;
existing harness analysis 6/6; NR analysis checks 3/3 including mutations. These are separate from game acceptance.

The last whole-tree source check is 88/89: the newly added PT `lantern-short` recipe starts On, but
`test_ingame_harness` requires every recipe to start Off. The recipe belongs to the ongoing PT diagnostic work
(`docs/PT-REQUESTS.md`, lantern test); it was not rewritten or its check weakened by the NR work. Resolve that
baseline-policy mismatch with PT before a final shared release check.

The harness now records actual NR evaluations and GPU time, preserves NR configs, compares Off/neutral pictures
even with PT still on, and generates moving-camera takes. `neural_plan.py` uses the same saves/calibrations with
game, RR or NRD lighting. `-KeepHud` allows HUD/text validation. Failed steps now count toward failed scenarios.
The new harness plan has not been run in game.

When asked about coordinated deployment, the owner replied: **“Keep builds isolated until PT work is ready.”**
Everything remains in `bin/` and `build/nr`; existing artifacts and game files were not replaced. No zip, commit or
push was made. Run the final shared build only after reconciliation with PT. Detailed commands, source contracts,
evidence paths and all remaining acceptance work are in the implementation document. PROJECT-CONTEXT's NR entry
was replaced within its existing four lines; the concurrent PT notes were left in place.

Later in the same conversation, the owner asked whether the mod was installed, then explicitly requested
"Please copy it over". That superseded the deployment hold. The native targets were refreshed (all four hashes
unchanged from the tested build), and NR/FG/BU rebuilt for Mono. All 89 source checks now pass. The first process
name check missed the running game; the actual deployment guard caught it before any game files changed. The
owner closed the game and confirmed it. At 17:02, NR/FG/BU and their bridge 1.2.2 copies were installed through
the shared payload installer, including backups and file replacement safe for Vortex links. The signed model
was copied separately, outside the release payload. All 42 payload files plus model match their hashes.
The PT plugin/native hashes were preserved; its older bridge loses the version election to 1.2.2. No game
launch, release zip, commit or push. Evidence: `build/nr/deploy/deployment-result.json`; preparation/install
script: `.testruns/nr/deploy.ps1`. NR starts Off. Actual game and FoA behaviour and appearance remain unverified.

First in-game feedback followed: the look changes with movement; rename the row to Neural Rendering: Off / DLSS;
offer placement before/after upscaling; can the model be pushed harder; how was it selected and tuned?
All five items are recorded in `docs/DLSS5-FEEDBACK.md` and BACKLOG. Read the actual log: 1,800 evaluations at
3840x2160, about 11.5-12.8 ms each (model only), just the initial reset. No repeated-reset diagnosis is supported.
Asked which visual change the owner means and whether earlier placement is for speed or appearance.

The rename is implemented for Video and FoA (ChoiceLabels preserves config On), compiled for both game versions,
and the Mono plugin alone deployed/hash-verified 17:43 with backup. Evidence: `build/nr/label-deployment.json`.
NVIDIA's sample confirms intensity 0-1 and styles Default/A-G plus tone/structure/masking controls. They are not
yet verified for this DLL. No model comparison or appearance tuning was claimed. Before-upscaling remains open:
the sample expects tone-mapped, clean colour, while our pre-SR/RR image is HDR and pre-RR lighting is noisy.
No invalid input path or dummy selector was added. Keep investigating the motion report and valid placement.

## Tuning follow-up, through 18:21 deployment and tavern load failure

The owner clarified brightness/colour changes, earlier placement for speed/player demand, and A/B/C within NR.
They authorized running alongside other GPU work while FoA and Starfield are closed, then requested stronger
camera movements and the tavern first. The baseline finished two scenes; forest is excluded because the owner
moved the camera. The NPC fallback is not a face close-up. Small Off/neutral restoration differences remain.

Implemented A/B/C (Style 0/1/2 for signed 310.8), local tone/detail/skin/masking, live strength without resetting
history and before/after placement. Before processes render-size HDR through our reversible positive-colour
adapter; it is not the sample's default display-size input. RR forces After with a reason and restores the saved
placement afterwards. Native ABI 0x00010100, frame size 100. Both managed versions compile. The GPU probe passes
HDR neutral (maximum relative error 0.001439), live strength and distinct A/B/C output. 23 policy checks, eight
policy mutations, five NR analysis checks and six shared analysis checks pass; final source checks 90/90.

At 18:21 the NR native/plugin pair alone was installed, backed up and hash-verified. The 23-take tavern run loaded
NR but timed out loading the scene after 420 s, taking no comparison frames. Four PT tests held roughly 10 GB
at launch; the game's memory budget shrank to about 3 GB. Memory pressure is a lead, not a proven diagnosis.
The game quit; all 117 save files were unchanged and configs were restored. Starfield started afterwards, and
another coordinator later held the harness RUNNING lock. A retry once Starfield closed was refused by that lock;
no new NR game was launched and the other run was left alone.

The next comparison uses a 65 cm sway and 25-degree turns, one warmup and one captured 48-frame cycle. Analysis
requires matching poses and grades colour changes against Off at each same pose. The driver now resolves output
paths absolutely and rejects unknown scene keys (the first run's `tavern` silently skipped `tavern-room`). The
complete state, deployed hashes, limitations and next command are in `docs/DLSS5-TUNING.md`. No chosen best
model, final look, pre-upscale speed claim or successful tavern comparison. No release zip, commit or push.

The owner later said "try now": run 20261007-192507 waited for Starfield and completed at 19:32:14, 23 takes.
Run 20261007-194841 then completed castle and forest at 19:57:44, 29 takes each. All 117 saves unchanged and
configs restored. The npc-candle fallback is castle, not Ron; the forest is rainy. Actual evaluations verify
3840x2160 After and 1924x1084 Before. All movement routes match. Read `docs/DLSS5-TUNING.md` and the model/motion
sheets in those output folders. The initial forest remains excluded after the owner's camera movement.

After/A Local Tone 0 lowers matched-motion brightness-shift range from 1.114 to 0.102 in the tavern, 0.750 to
0.126 in the castle, and 1.232 to 0.701 in the forest (whose Off reference varies by 0.377 itself). B is steadier
in the forest; C reduces fine-detail contrast. A tone-zero default was built but withdrawn BEFORE deployment:
the owner wants MORE NVIDIA lighting/colour and describes grading bypassed while moving, returning when still.
Defaults stay After/A, tone/strength/detail 1, mask Off. Diagnose the bug rather than reducing its visible effect.

Restoration is still open. The tavern's largest difference is animated HUD text. The HUD-free castle comparison
had a 3.6 mm / 0.373-degree camera shift between static takes 13 and 14; static 14-17 cannot be graded against
initial Off. Motion uses its own matching Off reference. Static analysis now rejects changed camera poses too;
six NR analysis tests with five faults and six shared tests pass. Other smaller neutral failures remain.
The original report is preserved; the new report records the exclusion. Source checks remain 90/90.

Run 20261007-200519 later timed out with noninteractive-state warnings; it is partial evidence, not a clean
character comparison. No run is active now.

## Before lens effects, deployed 20:44

The owner asked for NR before blur, CA and vignette on both sides of upscaling; RenoDX HDR should follow it.
The old hook received motion blur. Eight diagnostic takes isolated a movement-stop brightness jump of +1.130
display levels, versus +0.095 when blur was suppressed. Evaluations were continuous, without resets/skips.
NR now brackets HDRP's upscaler before lens effects, scoping/restoring the upscaler schedule per camera. Both
routes use the existing HDR adapter. NR and BU share CustomPassRecorder source with the IL2CPP unboxed target
call preserved; no bridge API/binary change. NR independently requests shared motion corrections.

Both NR and BU build for both game versions; only NR was deployed. Native NR unchanged. Managed SHA256
74D7735B87DCFD8E85DA88130DEE77E49AC9F1E365ABD85C65E26AE13EBA730F; build/nr/tuning-deployment.json.
Eight corrected diagnostic takes pass at full tone, before lens effects and evaluated every frame. The stop
jump becomes -0.163. Overall view-dependent grading remains; no universal stability claim. Source checks90/90;
eight NR evidence tests detect nine faults. Revised recorder source check is also mutation-checked separately.

Run 20261007-204924 completed 60 model takes across Ron, tavern and forest, 9 minutes without competing GPU
work. Eleven report warnings: small Ron/tavern Off differences; forest neutral/Off differences; the last three
forest moving takes excluded for UI/FOV state. The motion and model sheets were inspected. No model winner or
owner look approval. Full tone/detail/strength and A/After remain defaults. Both completed runs restored configs,
closed the game and left 117 saves unchanged. Exact results and evidence links: docs/DLSS5-TUNING.md.

RenoDX Unity HDR source confirms downstream grading/output encoding; texture-format upgrades happen earlier.
The actual public NR add-on source was not found. Live HDR coexistence remains untested; own HDR mod is future
work. Owner reminds us PT renders its own fog. Recorded completed-scene contract: use the active renderer's
fog and geometry guides. Found PT does not yet publish own depth/motion for NR/RR (PT-72 / NR-10); coordinate
with PT-71 migration. No PT source changed. No release zip, commit or push.

## Actual DLSS input follow-up, 21:46 deployment

Owner corrected NR to consume DLSS's actual inputs, requested a performance placement option, ShortFuse Tool
research and close-up faces. Implemented shared NGX observation in bridge, FG subscriber and BU RR publisher;
NR arms camera colour identity, uses actual depth/motion/regions/scales/jitter/depth/reset, then evaluates at
the selected side of upscaling. No separate HDRP guide lookup. Native observer exports additive, NR ABI10200.
All source versions 1.2.3; PT only version metadata. Both versions compile; synthetic coexistence/image tests
pass, wrong-camera mutation fails. Source90, recorder mutation, eight NR and six shared analysis checks pass.
Installed/hash-checked 42 entries/model at 21:46; preserved PT. Eight real tavern motion checks pass with actual
NGX inputs; 20.7% GPU contention warning. 117 saves untouched. Close-up framing verified at 1.8 m/FOV40.
Thirty close-up face takes completed (20261007-215715) in 4.1 minutes, no GPU contention. Model/mask/motion
sheets inspected. Before neutral and final Off retain small restoration warnings. Defaults unchanged.
ShortFuse public release controls inspected read-only; no add-on installed. Details in DLSS5-TUNING.

Owner sees temporal colour/exposure/coverage instability. Added local RGB-block comparisons against matched Off,
hold-state heatmaps and changed-area curves. Nine NR tests/ten deliberate faults, six shared analysis tests and
source90 pass. Face hold worst RGB-block ranges 5.27-9.97 versus Off-repeat 0.956 confirm remaining instability;
continuous evaluation is not quality acceptance. This is visible effect coverage, not the model's internal mask.
Per-frame actual exposure/input/output capture is the next diagnostic; currently exposure is per-take PT text.
Run output directory reuse mixed stale custom sheet names; corrected sheets select request.json's exact takes.
Launcher now rejects a prior output folder. No old evidence was deleted.
Live DLSS FG check 20261007-221022 passes both placements: 602 input pairs, 599 extra SDK output frames, zero
native errors. Earlier frozen FG run correctly suspended generation and is not a compatibility pass. All runs
leave 117 saves unchanged and configs restored. Game closed, no lock. Installed files remain 21:46 1.2.3.

## Stage diagnosis and motion-marker correction, 22:57 deployment

Added opt-in same-frame GPU exposure and five NR stage grids to the shared game harness. Six analyser tests
reject five deliberate faults; the native probe's known input validates readback orientation, averaging,
encoding, motion scale and exposure. A deliberately inverted reduction shader fails that CPU reference.
No normal-play readbacks; no performance claims from diagnostic runs.

Runs 223514 and 225820 each complete 16 takes in 5.2 minutes: Ron close-up and tavern, Off/neutral/A/B at
both placements, 48 moving frames then 96 held. Exposure is exactly constant in all 24 NR takes. Colour
changes already exist at model output, before restoring HDR and the game's later processing. Found Unity's
X>1 no-motion marker copied as huge movement. Corrected observed Unity SR decoding only; generic/RR vectors
remain unchanged. All 12 corrected NR takes show <0.00361 pixel held motion (face exactly zero, previously
7680). The real-capture test rejects pre-fix evidence. This does not fix broad colour instability: held local
output-minus-input ranges still 10–20 encoded RGB levels. Input also settles by 4–7 levels. Full tone stays 1.

Face A/B within each run is valid; cross-run face movement differs 2.6 degrees / 8.28 cm, so no direct image
comparison across those runs. Tavern movement matches. Other agent deployed PT at 22:46; preserved it in the
NR-only deploy. No NR+PT appearance claim. Inspected overviews/atlas and stage plots. Both runs leave 117
saves intact, restore settings and close the game. No report warnings means capture/restoration checks pass,
not temporal stability. Current hashes and details: DLSS5-TUNING. Source93 and all NR/stage/shared analysis
checks pass. Scheduling mistake: a short offline probe overlapped PT run224654; document its timing
contamination and use run-stage-probe.ps1's immediate game/lock check before further probes.

## Identical-input replay, 23:28 deployment

Owner asked "Are you just waiting for nothing?" Resumed the pending replay, built both managed versions,
harness and isolated native NR, then installed/hash-checked only the NR pair. Added diagnostic-only texture
retention, zero motion and an optional one-shot reset request; six stage grids now include depth. Twelve
analysis cases reject ten deliberate faults; the real signed-model GPU probe validates depth/colour/motion/
exposure. Source93 passes. Full tone/detail/strength remain 1; no player-facing visual default changed.

Run 232925 completes 16 takes in 5.3 minutes: face/tavern, ordinary/retained/retained-reset at both placements,
Off bookends. All eight replays have constant colour/depth and zero motion; reset/source metadata exactly
matches the request. NR output still changes, final-32 local RGB ranges 1.5-4.3 encoded levels. Thus changing
scene input or exposure alone cannot explain NR-14. This locates the response inside NR with our current
input contract; it does not establish a vendor bug or validate our HDR adapter. Viewed images and curves.
No report warnings, no GPU contention, 117 saves unchanged, configs restored and FoA closed. Full numbers,
hashes and limits in DLSS5-TUNING. PT's 22:46 build remains intact, Off in these runs.

Prepared the next test with `neural_plan.py --replay --long-hold`: 600 stationary frames, both placements,
one reset each, Off bookends. Starfield started at 23:36:49 during launch preparation; the guard stopped
FoA before game configs were touched. The aborted output nr-replay-long-20261007-2336 is not evidence.
Its exact orphaned lock was removed after verifying the process had exited. No waiter is left active.
New output folder required on retry. Current NR hashes 7A5E1F9F0014 / DDA8D30644DE. No release/commit/push.

Starfield then closed and the test was retried: run 234234, nr-replay-long-20261007-retry, completes four takes
in 7.1 minutes. Both 600-frame holds retain constant colour/depth, zero motion and constant exposure. Final-32
local model RGB ranges remain After 1.368 / Before 2.056; smaller fluctuations persist past the initial settling.
One report warning: permitted concurrent tgpt_gpu_test work (84.8%), so no timing claims. No capture/restoration
failures, 117 saves unchanged, configs restored and FoA closed. Viewed overview and stage plots. Starfield was
running again afterward. No waiter remains. NR-16 also recorded: neutral strength still incurs a 0.23% mean
relative linear-colour round-trip error; preserve original scene pixels at zero while keeping NR history warm.
That separate small fix is pending, not the cause of full-strength variation. Source/deployed pair unchanged.

## Exact neutral colour, 2026-10-08 00:04 deployment

Fixed NR-16 before ending the session: at zero strength, NR continues evaluating but leaves original scene
pixels untouched. The strengthened exact-pixel GPU assertion first rejects the old binary, then passes the
fix (maximum relative error 0.001439 to zero), with live-strength/history and the existing checks passing.
Deployed/hash-checked only NR: managed stays 7A5E1F9F0014, native now 694448817770. No full-strength change.
Run 000447 completes four tavern takes, both neutral placements plus Off bookends, in 3.6 minutes. All 144
scene grids in each NR take are exactly preserved; the new capture check rejects the earlier wrong result.
Viewed overview. One timing warning from permitted concurrent GPU tests (44.5%); no capture/restoration
failures. Saves117 unchanged, configs restored, FoA closed, no waiter. Source93 passes. This follow-up completes
24 takes total. NR-14 remains; no model/look winner. No release/commit/push. Full evidence in DLSS5-TUNING.

## ▶ Starter prompt (R9)

Continue NR from BACKLOG, DLSS5-TUNING and NR-01 through NR-16. Mono NR 1.2.3 pair from 10-08 00:04 is installed; check
current game processes and shared harness lock before any launch. No run was left active. Native public NGX
works; do not repeat SDK/caller-workaround research. Keep full Local Tone 1: owner rejected weakening the effect
to hide motion instability. NR-14 now reproduces with constant colour/depth, zero motion, even with one reset request.
The 600-frame replay is done too; smaller changes persist. Next validate HDR-to-model conversion/profile.
NR-16 exact zero-strength colour is fixed and tested in game. NR-15 SR marker fixed; audit FG/BU encoding before extending
that decoding to generic/PT vectors. Resolve NR-06 cross-run face pose mismatch and excluded forest motion.
Before remains the Performance option; tune it separately. Do not weaken thresholds or claim a best model.
Coordinate matching PT colour/fog/depth/motion with PT-72 and ongoing PT-71 migration before grading PT+NR.
Standalone, FoA interaction, normal-version, FSR/XeSS FG and ReShade/RenoDX combinations remain open. HDR display stays
gated. Preserve concurrent PT edits. Deploy authorization persists while FoA and Starfield are closed; launch
only through Run-PtHarness, respecting its lock. No commit/push/release is requested.
