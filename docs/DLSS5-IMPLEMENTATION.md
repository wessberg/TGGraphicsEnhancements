# TG Neural Rendering implementation — 2026-10-07

R17 (2026-10-09): separate community adapter implemented with owner approval, selected automatically for a
modified/unsigned NR DLL. No per-file allowlist; direct interface/support checks decide compatibility. The signed
SDK path is preserved. Native build and CPU boundary checks pass; actual rendering and RR coexistence pass on5080.
The managed ABI is unchanged. Nothing deployed during parallel PT work. [Details and remaining checks](DLSS5-COMPATIBILITY.md).

The 1.2.2 implementation builds for the Mono and normal game versions. Ordinary public NGX on the shared bridge
device creates and evaluates the supplied NR model. It also works in the offline check with Streamline and Ray
Reconstruction on that same device. This is a prototype, not an in-game appearance verdict. The owner later asked
"Please copy it over": Mono NR, FG, BU and bridge 1.2.2 were installed at 17:02 on 2026-10-07 with the signed model.
All 42 payload files plus the model match their staged hashes; replaced files have backups. PT binaries were left
intact. No release zip or game launch. Evidence: `build/nr/deploy/deployment-result.json`.
The owner's first game feedback is tracked in [DLSS5-FEEDBACK.md](DLSS5-FEEDBACK.md). The menu row was renamed to
Neural Rendering: Off / DLSS and the Mono plugin alone updated at 17:43; its newer hash is in
`build/nr/label-deployment.json`. The original deployment report remains the historical record of the first copy.
At 18:21 the NR native/plugin pair was updated for A/B/C, tone/detail/masking and before/after placement. Both
versions compile; the native probe passes. The tavern scene timed out loading and no new look was selected.
At 20:44 the NR pair was deployed again for placement before lens effects. Eight motion-diagnostic takes pass;
60 model-comparison takes completed with 11 analysis warnings. PT geometry-guide integration remains open (PT-72).
Current evidence, hashes, input-contract limits and next run: [DLSS5-TUNING.md](DLSS5-TUNING.md).

Latest update: shared NGX observation installed in 1.2.3 at 21:46. NR alone updated at 23:28 for opt-in replay
and depth readbacks, retaining the 22:57 Unity SR marker fix. Both game versions compile. The 16-take replay
run confirms changing NR output with constant colour/depth and zero motion, even with one history reset request.
The 600-frame follow-up completes too; small fluctuations persist. No visual default changed. See DLSS5-TUNING.
At 10-08 00:04, NR was updated again to preserve original scene pixels at exactly zero strength while still
evaluating the model. Exact-pixel GPU checks and both placements in game pass, rejecting the old behaviour.
The full-strength colour issue remains open. At 10-08 00:44, NR alone was updated for the NR-17 HDR glare fix:
restore the original input shoulder factor instead of inverting the model's new peak. A diagnostic input-scale
sweep exposed the defect; the matching 16-take game run passes all 12 full-strength HDR-bound cases and rejects
the old capture. Both face/tavern atlases inspected. Native hash starts `66563493B08E`, managed `182C4F803FD8`.
All 117 saves unchanged and configs restored. R10 completes A/B/C with masking Off and On (32 takes, clean).
No consistent masking win; C is gentler and often steadier, with no default change. On 10-08 07:20 the NR-only
diagnostic update adds signed vector capture: native `DEF3E8E3D720`, managed unchanged. Four neutral tavern takes
validate both placements against independent image motion (0.11 capture pixel median error). No normal-play
rendering change. Live A/C forest has six valid takes, favouring After for stability; C is gentler, not always
steadier. Twelve face/tavern takes were invalid outside gameplay (NR-18); the retry completes 12 valid takes
with the readiness guard. Tracked-feature colour analysis shows Before changes less between moving frames
but more during settling. NR-14 remains open. Full results are in round-10 and DLSS5-TUNING.

At 10-08 09:44, NR alone was updated for an opt-in repeated-history-reset diagnostic. Native SHA256
`0093215458167D623D84AA3DA24EF1534C3C563A00B20D0E3A17DF60885C4092`; managed unchanged.
Build/scratch/game hashes match; native probe, managed harness build and Source95 pass. Stage tests16/faults15.
No normal-play rendering or default change. Its six-take test `20261008-094455` stopped during the second
take because the game's UI became non-interactive. No reset-once/every comparison result. Saves117 unchanged,
configs restored, both games closed and no queued NR launch. NR-14 remains open.

## Runtime and adapter

Menu update after the owner's settled defaults: both managed versions build with Advanced Off/On, shared
Performance tradeoff tooltips, and clearer Local Tone/Strength descriptions. Source95 passes. Deployed at
10-08 10:31 after both games closed; managed hash `527E002E8426`, native unchanged `009321545816`.
Build/game hashes match; `build/nr/tuning-deployment.json` records it, `advanced-menu-build.json` both builds.
Advanced keeps A/B/C and detail/mask tuning accessible. Isolated Intensity1.5/2 has no effect beyond1;
the owner's intended "2x" means repeated passes. Advanced Passes1-4 deployed11:22 after the PT run finished; nativeE011F0C82733, managedE3868ED67C0B.
One default pass; same tuning per stage, independent history, one colour preparation/HDR restoration. No
final-opacity blend. NR ABI0x00010300 (frame104/status80) adds passes and actual model-invocation count.
GPU oracle matches exact independent calls for1-4 at Strength0.5/1, with three chain faults detected;
policy10 faults, stage17/faults16 and Source95 pass. Both managed builds clean; tavern7 valid (5 NR/2 Off), all native pass counts match, Off restored; UI acceptance pending.

- Public build interface: `DLSS-SDK-310.9.1`, `nvsdk_ngx_s.lib`. NR feature 18 and the version-specific parameter
  names are isolated in `native/neural/src/extension.cpp`. Own project GUID, normal SDK-to-driver calls. No model
  export dispatch, executable rename, caller shim, beta helper or second device.
- Validated file profile: Windows x64, NVIDIA-signed `310.8.0.0`. The owner's archive
  `DLSS310.8.0-Streamline2.13.zip` contained the valid copy. SHA256:
  `E16BCF15E16E13F527491CDF7845B2FE6521A738D8F7C9C721866A8496E1FC8E`.
- The earlier loose DLL failed Authenticode with `HashMismatch`; its hash was
  `4B8D19BC3EFF58A084F5ECA7489C921501C203450169FB82FF4F649A4482BA05`.
  Both stay in ignored `DLSS-NR-Runtime/`; the selected `nvngx_dlssnr.dll` is the valid archive copy.
- Installed lookup: `BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll`, then the game-folder file. An invalid
  preferred file is rejected explicitly. Existing game-folder DLSS sets are kept. The model is outside the package
  allow-list. File identity and actual loaded path are checked; hardware support comes from NGX capabilities.
- The public SDK's feature-requirements and scratch queries can refuse the reserved feature ID. Their results are
  logged separately. Capability `DLSSNR.Available` must succeed. Ordinary creation without scratch succeeds on
  this machine even though its scratch query returns `0xBAD00002`. There is no alternate caller route.

## Pipeline and settings

At 20:44, the default after-post-processing hook was replaced: `UpscalePlacement` records NR on the chosen
side of `DoUpscalingAndAntiAliasing`, before lens effects. It scopes the upscaler schedule to BeforePost
for this gameplay camera and restores it even on error. Both routes capture HDRP depth/motion and adapt
scene-linear HDR into the model's LDR range, then restore HDR for downstream effects and tone mapping.
The shared recorder source is also used by BU; the bridge ABI/binary is unchanged. FG's existing
AfterPostProcess capture naturally follows NR. Fog and transparents remain in NR's input. NR requests shared
motion corrections independently. No FG, BU, PT or ReShade installation is required. RenoDX HDR ordering
was inspected in source; compatibility still needs an actual game run. Current evidence: DLSS5-TUNING.md.

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

Frame and camera IDs, both viewports, jitter and depth convention must match. Only the full-screen main gameplay
camera qualifies. Source textures are retained by native tickets; separate producer/consumer fences protect the
shared transfers. Unity's D3D11 state is restored after each operation. A failed operation leaves source colour
untouched and disables NR. Off releases NR's feature and frame textures without shutting down process-wide NGX.

The 310.8 profile uses upright display-size RGBA32F sRGB colour, render-size reversed hardware depth and normalized
HDRP motion converted by `(-displayWidth, +displayHeight)` after the Y flip. Linear and sRGB source views have
separate conversions; alpha comes from the source. HDR is disabled with an explanation until its contract is
validated. Activation, frame gaps, camera changes/cuts, HDRP history resets, resize and incompatible tuning reset
history. PT debug views bypass NR; PT with no denoiser is unavailable. RR and NRD remain eligible.

Video and FoA expose Neural Rendering (Off / DLSS), Model (A/B/C), Placement, Strength, Local Tone, Detail,
Automatic Mask and Skin Detail (masking On only). Starts Off; child controls hide while Off. Missing requirements
grey out the main row, with a reason; DLSS is mandatory. Saved preferences survive temporary unavailability.
Earlier placement uses render-size HDR-adapted colour; RR forces After and explains why. Strength changes keep
history; style/tone/detail/masking changes reset it. New controls compile for both versions but their UI behaviour
and appearance still need game acceptance. See the tuning report for the limits of the earlier colour adapter.

The bridge adds its shared NGX lock and early NR search path to TGFG/Streamline and TGBU/RR. Managed API additions
are additive. FG 1.2.2 now requires bridge 1.2.2 because it uses the shared scene pass. All source versions are 1.2.2;
the later deployment records above identify the installed builds. Existing release zips were not rebuilt.

## Evidence and commands

All builds and installs for this work are under `build/nr`, never `artifacts/Native`. The existing PT binary and
vendor runtimes were copied into this scratch install for coexistence checks; PT was not rebuilt by this work.

| Check | Evidence |
|---|---|
| Native NR, bridge, FG, BU | MSVC Release builds, scratch `cmake --install --component TGNRProbe` |
| Managed NR and modified FG | Mono/net472 and normal/net6.0 compile checks |
| Real NR | 20 successful GPU evaluations: two sizes, strengths 0/0.5/1, Off/restart, RGBA32F and two sRGB layouts |
| Neutral colour | Float mean error below 0.0000003; both sRGB layouts had zero byte-level error, including varying alpha |
| Non-neutral colour | Mean linear change about 0.005 at half strength and 0.010–0.0114 at full strength; finite, non-black output |
| Shared device | Streamline active, 30 swapchain presents; 32 RR evaluations without fallback, 30 following NR evaluations, NR release/restart while RR stays alive |
| RR image regression | Mean 0.191 vs expected 0.194, adjacent-pixel noise 0.0004, moving highlight 3.906; checks pass |
| Failure fallback | Missing and rejected models leave a working bridge/swapchain, zero NR evaluations; bad viewport leaves original pixels unchanged |
| Mutation checks | Six camera/frame/depth/jitter/reset mutations fail policy tests; black output and ignored strength fail the actual GPU image probe |

Managed installation checks, autoload checks (12), mocked package checks (98), harness analysis checks (6) and NR
report checks (3, including mutations) pass. Source checks passed 89/89 earlier in this session; the final shared
tree has 88/89 because the concurrently added `lantern-short` PT diagnostic recipe starts On while
`test_ingame_harness` requires every recipe to start Off (`tests/source_checks.py:3630`). That recipe was left
intact. The later deployment check passed 89/89 on the shared tree; that earlier mismatch is resolved.

Logs: `.testruns/nr/probe-final.log`, `coexistence-final.log`, `fallback.log`, `missing.log`, `rejected.log`.
Policy mutations: `build/nr/mutations/`; GPU mutations: `build/nr/gpu-mutations/`. Probe images:
`build/nr/probe/*-strength-*.ppm`. These are synthetic inputs, not quality or performance targets.

Native diagnostics live in `BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log`, tagged `[TGNR]`. They record
selected/loaded runtime hash, queries, created sizes/strength, frame/camera/reset counts, last NR GPU time and
transfer texture bytes. GPU timestamps cover evaluation only; texture bytes exclude model memory. Use real-frame
FPS and adapter memory alongside them. Synthetic 256-pixel timings are not a prediction for the game at 4K.

From the VS x64 environment, after staging a scratch BepInEx layout with the signed model:

```powershell
build/nr/tgnr_gpu_smoke.exe build/nr/probe/BepInEx
build/nr/tgnr_gpu_smoke.exe build/nr/probe/BepInEx --bad-viewport
build/nr/tgup_gpu_smoke.exe build/nr/install --factory-first --with-fg --with-pt --with-nr --installed
python tests/neural_policy_mutations.py build/nr/mutations
python tests/neural_gpu_mutations.py build/nr/probe/BepInEx build/nr/tgnr_gpu_smoke.exe build/nr/gpu-mutations
python tests/neural_harness_test.py
```

## Scene tuning

Game-lighting comparisons now cover the tavern, castle and rainy forest, A/B/C, before/after and stronger matched
camera motion. See [the tuning report](DLSS5-TUNING.md) for image sheets, camera/HUD exclusions and the owner's
motion-bug clarification. The tone-zero proposal was withdrawn; PT combinations wait for the parallel migration.

`tools/harness/neural_plan.py` reuses the existing saves and calibrated views. Plans compare NR Off, strength 0,
0.25, 0.5, 1 and Off again, then moving-camera NR/Off takes. Run separate plans with the game's lighting, PT+RR,
and PT+NRD. The plan's `neural` flag requires actual native evaluations during capture; an On setting alone cannot
pass. Missing evidence and failed steps fail the report. Off/neutral images are compared even when PT stays on.
The runner and driver restore NR configs as well as PT configs. `-KeepHud` keeps HUD/text visible for its checks.

```powershell
python tools/harness/neural_plan.py build/nr/plan.json
python tools/harness/neural_plan.py build/nr/plan-rr.json --denoiser "Ray Reconstruction"
python tools/harness/neural_plan.py build/nr/plan-nrd.json --denoiser NRD
# Only after coordinated deployment, with DLSS selected and SDR active:
./tools/harness/Run-PtHarness.ps1 -Plan build/nr/plan.json -Scenario npc-day,fog -KeepHud -KeepFrames
```

Deployment refreshed the four native targets and the three Mono plugins. Native hashes remain identical to the
tested binaries; build, scratch install, deployment stage and game copies match. Shared bridge 1.2.2 wins over
the older bridge bundled with the preserved PT install. The source model's NVIDIA signature and installed copy
were checked. No release package was created. Use only Run-PtHarness to start the game, with its five-second GPU-idle gate.

Still unverified: both actual game-version lifecycles; UI disable/restore behaviour in game/FoA; standalone and
ReShade runs; DLSS/DLAA input orientation, cuts, loading and resolution changes in real scenes; all supported FG
backends generating frames; PT+RR and PT+NRD visual comparisons; final acceptance of faces, hair, foliage, water,
dark rooms, bright skies and HUD text; reliable cost/VRAM at game resolution. The finer controls have a pinned adapter and A/B/C dispatch checks;
scene evidence and the owner's appearance verdict still determine final defaults. HDR display stays disabled.
