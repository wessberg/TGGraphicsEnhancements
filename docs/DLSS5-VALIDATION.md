# TG Neural Rendering validation - 2026-10-08

NR-28: owner requests all remaining validation after R14's post-SR alignment audit. This is a test
inventory with explicit evidence levels, not a release approval. NR-14 lighting instability remains open.

R17 community-loader continuation: owner considers the existing rendering ready and asks to finalize runtime
compatibility. The separate adapter passes actual SDR/HDR/control/Off/resize and RR32/NR30 coexistence on5080;
signed route passes with helper absent. Final automatic-selection build and CPU API/file-lock checks pass,
including five deliberate faults and four CPU-only unsigned-fixture cases. No community hash/version allowlist.
Final source102/package101 pass. Final-policy signed/community GPU reruns, RR sharing and multipass plus three
chaining faults all pass. Evidence and limits: [DLSS5-COMPATIBILITY.md](DLSS5-COMPATIBILITY.md) and round-17.
No new older-RTX or community in-game claim; no repeat of the signed-runtime in-game matrix.
No extra temporal filter. Baseline Default0, Quality, full controls, one pass, maskOff, input exposure1.

R17 final game checks (10-09, NR-36..43): normal-version startup hook and nested Options-row lifetime
fixed;20 actual gameplay pause/options/apply/resume cycles with active NR pass, owner says it works great.
Then both game versions pass the same two-cycle combination smoke with official signed310.8 model,
DLSS SR Performance (50%), DLSS FG2x/Reflex and NR Quality then Performance. Both runs use real pause
menus, apply changed settings and resume interactive gameplay with advancing NR/FG counters. No logged
errors or crash. Mono PID22988 finishes13:54: NR resumes219/317 evaluations, zero failures; NVIDIA
reports414/601 output frames for217/315 source frames (includes settling after pause, not display counts).
Gameplay/menu screenshots inspected. Temporary probes removed, configs restored,117 saves unchanged.
Source105 passes. All6 normal/Mono1.2.3 NR/FG/BU zips verified against their staged payloads; Mono files
and official model installed/hash-checked. Final evidence: build/nr/mono-r17/combo-probe and
build/nr/il2cpp-r17/combo-probe. This smoke adds no older-GPU, patched-model in-game or PT-guide proof.

Earlier R15 checks, with later results above superseding their pending status:

| Area | Planned check | Current evidence / result |
| --- | --- | --- |
| Source and policies | Current source suite, NR policy and deliberate policy faults | Latest96 pass. Intermediate PT NRD assertion was corrected by concurrent PT work. NR policy baseline and10 deliberate faults pass |
| Managed builds | Mono and normal version, shared menu/option conditions, install/autoload/package checks | Both NR builds and harness compile. Install/autoload12/package98 pass. Normal-version option routing passes with matching BepInEx6/Harmony |
| Image arithmetic | Real signed runtime, neutral SDR/HDR adapter, alpha, styles, bad regions | Pass. Exact neutral float and sRGB, live strength, Default/A/B/C accepted, alpha, resize, Off/restart and bad viewport checked |
| Failures | Missing/rejected model preserves the bridge and original image | Both pass, zero evaluations and working original swapchain |
| Diagnostics | Signal/stage tests, invalid evidence rejection, no normal-play changes | Real GPU stage oracle passes and rejects4 signed-motion faults. Analysis19 plus16 faults, NR evidence9, tracking3 plus2 faults pass |
| Multipass | Independent model-output oracle, deliberate chain faults | Pass:1/2/3/4 at strength0.5/1 exactly match sequential model calls. All3 broken chains rejected |
| Shared device | NR with SR/RR/FG modules, Off/restart preserves other features | Synthetic RR+NR and loaded FG/PT modules pass, RR32/NR30. This does not prove generated frames or current PT scene geometry |
| Native integration | Guides, motion, colour timing, history and frame matching | R11-R14 pass within documented scope |
| Game lifecycle | Toggle/restore, scene load, resize/cuts, unsupported upscaler gate | Pass201643: DLSS/STP/DLSS,1440p/4K,3m camera cuts and reload. Exactly one reset per tested discontinuity, none while held |
| Standalone | NR plus its bundled bridge, without FG/BU/PT | Pass195951:4 takes at4K, Off/neutral/full/Off. Actual loading list and native NR-only device verified. All9 staged files restored/hash-checked |
| FG backends | NR with actual DLSS/FSR/XeSS generated frames | Functional pass200317. DLSS/XeSS counters show2x. FSR dispatches pass, output count unavailable. FSR performance issue NR-29 reproduced even with NR Off |
| Other upscalers | DLAA and NR gating for STP/FSR/XeSS | Pass201643/202304. Full-resolution DLAA guide area, actual FSR/XeSS dispatches with NR Off, automatic DLSS return and FoA Off-only choices |
| PT denoisers | NR with current PT/RR and PT/NRD | Withheld pending the documented geometry contract fix PT-72. Synthetic RR+NR coexistence passed |
| Menu and FoA Mod Manager | Same rows, conditions and saved controls | Installed0.6.63 metadata reader and5 live menu states pass195305. Native Video greying and automatic return pass201643. Screenshots inspected |
| Normal version runtime | Actual startup/menu/render lifecycle | No separate game installation found yet |
| ReShade/RenoDX HDR | Correct placement, HUD, output range, device cooperation | ReShade disabled, owner controls its toggle |
| Visual coverage | Faces/hair, foliage, water, interiors, sky, HUD during movement | Earlier runs recorded, owner stability rejection stands |
| Cost | Real-frame FPS, GPU time, VRAM without diagnostic readbacks | Run201117:4K tavern83.7/32.4/86.1fps Off/On/Off, process VRAM5.13/6.80/5.19GiB |

Tests use isolated `build/nr` outputs. No full PT suite or PT rebuild is needed for NR validation.
Run GPU jobs sequentially with the shared game lock respected. Game starts only through the harness.
Do not install the normal game over the owner's Mono installation or toggle ReShade on their behalf.

## Evidence and limits

Current CPU evidence: `build/nr/validation-r15/`. `cpu-results.json` keeps the initial failures too.
Two stale mutation search strings were repaired in the NR evidence/stage tests. Retests pass in
`neural-harness-retry.log` and `neural-stages-retry.log`. Package tests initially needed the process-local
PowerShell policy override. Option-condition tests initially lacked their runtime argument, then used
Mono Harmony under CoreCLR. The successful run uses the matching normal-version BepInEx6 core.
These runner/setup failures are not NR rendering failures.

Native build/install and policy results: `.testruns/nr/validation-build.log`. The PowerShell wrapper
returned1 after capturing a CMake warning, but compilation/install, native policy and all10 faults completed.
The built DLL subsequently passed real GPU evaluations. Tested native SHA256:
`83C681B8F6309D08DBB3C8D378013F309A106436A20C1A6BAF50C4823A9590EA`.
No production NR source/default changed in R15. The tested pair was installed19:49 before the live retry:
native83C681B8F630, MonoDDBE0B1415E6, build/install/game hashes checked. Backup:
`BepInEx/TGNeuralRendering/Backups/20261008-174932-619-tuning`. No shared/PT files replaced by that deployment.

GPU input oracle: `build/nr/validation-r15-stages`, log `.testruns/nr/validation-r15-stages.log`.
Signal conditioning/inverse relative error at most0.000065575. All4 corrupt signed-motion readbacks fail.
`gpu-mutations/black-output` and `ignored-strength` reject those defects. The old HDR test assumed the neutral
path ran colour conversion. Its refreshed diagnostic case catches removal of HDR compression through the
near-neutral inverse check. Final HDR and selector fault evidence is in `gpu-mutations-retry/`. A retry with
the old capture directory was correctly refused, not counted as a rejected image fault.
Multipass baseline/fault logs: `multipass/*/result.txt`. Fallback/shared-device logs and command lines:
`fallback-results.json`, `styles.log`, `bad-viewport.log`, `missing.log`, `rejected.log`, `coexistence.log`.

All four style numbers evaluate. On this one synthetic input, Style C3 is bit-identical to B2. This does
not establish separate weights for every number or prove a style aliases another on all content.
The selector fault forces every number to0 and must fail. No style/default tuning change was made.

Archived game-regression checks rerun with current analysis: motion12 takes, exact neutral2 takes at both
placements, HDR bound12 cases all pass. Their corresponding pre-fix recordings fail for the intended reason.
See `motion-archive.log`, `neutral-archive.log`, `hdr-archive.log`. This is archived evidence, not new playtesting.

## Live checks

The owner installed FoA Mod Manager at `BepInEx/plugins/BepInEx/plugins/FoAModManager/FoAModManager.dll`.
Its `Loading [FoA Mod Manager 0.6.63]` line confirms it runs despite the nested extraction directory.
`tests/foa_metadata_tests.ps1` invokes that installed assembly's metadata reader against the bridge's tag type.
New opt-in `neuralManager` takes open NR in the real manager, record its discovered rows and a screenshot,
check basic/advanced/skin visibility and labels, then restore the previous selection and close it.
NR capture frames also record FG's existing cached native status without resetting its presentation counters.
Neither observer changes rendering. Both compile and their live checks completed below.

Plans: `build/nr/validation-r15/{menu,fg,standalone,cost}.json`, generator `.testruns/nr/validation-plans.py`.
Use short individual runs. Standalone used reversible plugin/autoload staging under the shared lock.
The FG run records actual dispatch/presentation counters. FSR has no actual-present counter in this API,
so an estimated2x rate must not be presented as a measured generated-frame count.

Menu attempt `artifacts/harness/nr-validation-menu-r15`, run192702: no game process and no frames.
Steam console19:27:19 reports `SynchronizingControllerConfig` with `syncfailed`, waiting for user response.
Harness times out, restores configs, verifies117 saves unchanged. The analyzer then encounters a missing
scene directory because the game never started. Not a valid menu test. No waiting NR process remains.
Owner asked to clear the Steam prompt. Do not resume a launch from that expired request: use a fresh harness run.

Owner cleared Steam. Retry194932 opens/captures all5 menus and NR evaluates in Quality, but the inspector
incorrectly looked for the hidden Style row during Off/basic takes. The manager removes hidden entries from
its catalogue. Corrected the inspector, explicitly expanded its Video category and captured manager issues.
Final run195305 (`nr-validation-menu-r15-final`) passes all5 takes,0 report problems, saves117/configs restored.
Inspected the expanded in-game menu screenshot. Names, order, Off/DLSS, placement labels, Default/A/B/C and
basic/advanced/mask visibility pass against the actual catalogue. The manager's amber warning is an absent
optional Avalon Mod Manager dependency, not an NR runtime failure. Both advanced takes passed even in the first retry.
Harness now offers `-KeepLock` for callers that must restore temporarily
staged plugins before releasing the shared game slot. Default behaviour is unchanged.

Standalone195951 (`nr-validation-standalone-r15`):4 valid frozen tavern takes, NR evaluations0/5/5/0,
no report problems. Env lists NR, its bridge, harness, FoA manager and Conversation Lighting Overhaul.
FG/BU/PT are absent. Native log PID39900 registers only NR and creates the shared D3D12 device on the
game adapter. The report's disk inventory still lists retained native PT/BU files, which were not loaded.
Inspected overview and full-effect atlas. Neutral/Off return within the game's baseline variation.
No extra GPU user during the run.117 saves/configs preserved, all9 staged files restored with matching hashes.
Journal: `build/nr/validation-r15/standalone-files.json`. Lock released only after file restoration.

FG200317 (`nr-validation-fg-r15`):8 live tavern takes,0 report problems. NR evaluations0/16/16/0/16/16/16/0,
zero failures/skips. DLSS+NR cached-status window has14 input dispatches,14 source presents,28 reported
output frames. XeSS+NR has16/16/32. These are vendor counts, not an external monitor measurement.
FSR+NR has14 successful dispatches, active backend2, accepted mode and no backend/interop errors.
Its output counter is unavailable, so no measured2x claim for FSR. DLSS-only continues after NR Off.
All requested sequences applied. Final Off stops dispatches, NR still runs before lens effects when On.
Overview inspected.117 saves/configs preserved. FG/NR screenshots capture real frames, not interpolated frames.

Cost201117 (`nr-validation-cost-r15`):three15-second live tavern intervals at3840x2160 with PT/FG Off,
no diagnostic readbacks or other GPU workload. Off83.69fps (11.95ms), On32.42fps (30.84ms), Off86.09fps
(11.62ms).1% lows64.39/27.83/60.22fps. Sampled NR evaluation11.32ms is only part of the integration cost.
DXGI process VRAM snapshots5.13/6.80/5.19GiB, budget14.85GiB. These are short scene-specific measurements,
not a general benchmark or a model-memory estimate. Saves117/configs restored, game exited.

Lifecycle201643 (`nr-validation-lifecycle-r15`): opt-in audit uses the installed BU diagnostic upscaler route,
restores it in finally and Runner.Finish. Actual native Video row has alpha1 with DLSS,0.4 with STP,
and1 after returning to DLSS. Mode On is remembered. Evaluations stop303/303 with STP, resume356/395
with DLSS. NR rebuilds for2560x1440 and original3840x2160/FullScreenWindow. Resets increment once on
each resize and3m camera jump/return (2/3/4/5/6), never within held intervals. Zero failures/skips.
Reloading the same save in the same process resumes NR,4 captured evaluations,4K and zero failures/skips.
Both scenarios pass. Inspected Video screenshots, including visibly disabled row. Lifecycle analysis
rejects6 deliberate corruptions. FG capture analysis likewise rejects6 corruptions. Saves117/configs
preserved, game exited. GPU gate waited for Starfield and a PT GPU test, neither ran during captures.

Combinations202304 (`nr-validation-combinations-r15`):11 valid takes. FSR3.1.5 and XeSS2.0.2 upscale
real frames with zero failures/fallbacks, while NR stops and FoA offers only Off with the DLSS explanation.
Returning to DLSS restores NR without changing its remembered On preference. DLAA accepted and NR's
actual-DLSS guide area is the full3840x2160, from its transfer counter. BU's native status retains the last
custom upscaler after switching to Unity DLSS, so that stale backend name is not evidence it still runs.
NR has16 evaluations in each On capture, zero failures/skips. Overview inspected. No competing GPU work.
Real-frame15-second rates with NR: DLSS FG35.86fps, FSR FG15.73fps, XeSS FG33.60fps. DLSS FG with NR Off
88.97fps. All uncapped. These are rendered rates, not rates multiplied by the requested FG multiplier.

FSR follow-up202741 (`nr-validation-fsr-cost-r15`):three15-second FSR FG intervals, NR Off/On/Off:
18.78/15.99/18.48fps (53.26/62.53/54.10ms). Zero backend failures, advancing dispatch counts, NR0/16/0,
no competing GPU work. Thus the large base cost exists with NR disabled. Root cause remains open as
NR-29, a broader FG performance investigation. This does not prove the same cost with the NR plugin absent.
It is not an NR lighting fix. Saves117/configs preserved in both runs, game exited, no remaining NR job/lock.

Remaining setup-dependent work: actual normal-version game runtime, owner-enabled ReShade/RenoDX HDR,
and PT scene combinations after PT-72 provides matching depth/motion to DLSS. Native HDR output remains
gated. HDR fidelity needs final-output evidence, not only Unity's pre-ReShade screenshots. No new appearance
acceptance, package, commit or push. Installed NR hashes still match tested build/scratch install20:30.

## HDR / ReShade continuation (R16)

The owner enabled ReShade, RenoDX and Windows HDR and confirmed that the RenoDX DLSS5 add-on was not deployed.
`artifacts/harness/nr-validation-hdr-r15` is a short `tavern-room` run with six final-output captures for each
state: Off, neutral, full and restored Off. A temporary ReShade 6.8 capture add-on copied the presented
`R16G16B16A16_FLOAT` surface at `reshade_present`, including its colour-space and display metadata. The run
reported `scRGB` (`colorSpace=2`) and a 10-bit PQ display (`displayColorSpace=12`, 1037.51 nits). ReShade.log
shows RenoDX changing the swapchain to the same FP16 format and its runtime reporting extended sRGB linear.

All 24 raw captures completed with no errors, NaNs or fallback pixels. The rendered contact sheet is
`artifacts/harness/nr-validation-hdr-r15/tavern-hdr-contact.png`. Off and neutral are visually close. Full
strength produces the intended brighter/detail-rich output. On a frozen camera, adjacent full-strength frames
still changed more than Off (mean absolute HDR difference 0.00163 versus 0.00042, with 2.58% versus 0.19% of
channels moving more than 0.01). This is evidence that the HDR path is wired correctly. It is not evidence
that NR-14 is fixed. Unity's `HDROutputSettings.main.active` was false because RenoDX owns the FP16 scRGB
swapchain, so the native Unity HDR flag must not be used as the only RenoDX-HDR detection mechanism.

The probe was built from the ReShade 6.8 headers, removed after the run and never belongs in a release package.
The game, ReShade.ini, saves and owner HDR configuration were restored. The GPU report recorded another process
using up to 37.8%, so this run is not a performance measurement.
