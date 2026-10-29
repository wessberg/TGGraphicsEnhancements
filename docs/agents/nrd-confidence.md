# NRD history confidence (PT-127)

Worker: `nrdconf`, pristine base `base20nc`. Started 2026-10-11 18:30 local.
All explicit writes target this copy or `%TEMP%/tgb/nrdconf-*`. No deployment, game start,
main-tree writes, SDK source copies or git mutations. The .NET build unexpectedly ran
workload advertising and MSI extraction; see the containment incident below. I cannot
claim the intended no-download/no-installer boundary was fully preserved by that tool.

## Plan and design (18:32, before implementation)

The first trial measures changes in direct lighting, including the sun. It re-shades a
saved ReSTIR DI light sample at its saved surface against this frame's lights and TLAS.
It compares that result with the radiance saved when that exact sample was first shaded.
The saved light identity, UV, reservoir weight, surface, view and RNG frame stay fixed:
new random samples must not masquerade as lighting changes. Visibility uses the old
frame's seed too. Light deletion becomes zero illumination, never an unchecked index.

- One sample per 5 x 5 render-pixel stratum, selected inside each stratum. A private
  record preserves the exact surface (including hair), selected light and radiance.
  The record is replaced only after its old value has been compared.
- A separate tracing shader produces a gradient and stores next frame's sample. No
  additions to `pt_gi`, `pt_specular`, `pt_prepare`, their walk or shared frame constants.
- Five separate 5 x 5 spatial filter dispatches, strides 1, 2, 3, 4, 5, with Gaussian,
  surface-plane and normal weights. No temporal smoothing of the gradient.
- A final surface-aware reprojection/upsample maps the previous-frame gradient to the
  current render grid. NRD remains responsible for primary disocclusions. Dynamic
  surface mismatches relax confidence rather than mistaking movement for lighting.
- Gradient normalization is exposure independent: differences and radiance are stored
  in physical FP32 units and blurred together. Convert only after filtering to [0,1];
  unchanged light gives 1, an extinguished sole light gives 0. RELAX uses a stronger
  response than REBLUR, because it caps history for this frame rather than feeding back
  a shortened history. The precise curve is a trial choice, not a vendor-prescribed value.
- One common lighting-confidence texture feeds `IN_DIFF_CONFIDENCE` and
  `IN_SPEC_CONFIDENCE`, explicitly allowed by NVIDIA. Set
  `CommonSettings::isHistoryConfidenceAvailable` only while those textures are valid.
  Keep RELAX's three confidence-driven relaxation settings at their vendor defaults,
  all 0: history response alone is the initial comparison, without added spatial blur.
- Hidden switch: new Advanced boolean `NRD lighting confidence`, default false,
  `TGPT_FRAME_NRD_CONFIDENCE` at frame flag bit 21. Existing `NRD trial` keeps selecting
  RELAX/REBLUR. No export, ABI record or FrameConstants row added.
- Lazy texture/buffer allocation, explicit memory accounting, safe release on resize,
  denoiser retirement and shutdown. Off means no confidence dispatch or allocation.

GI is not claimed covered. ReSTIR GI stores a reconnected vertex and its radiance, not
the complete random path and every selected light needed to replay the previous estimate.
Re-evaluating only its cached radiance would report stale light as unchanged. Build the
direct-light trial first; a separate low-resolution replay of full paths is follow-up work
if the tavern confirms the direction. This limitation remains part of PT-127.

## Vendor reading

Read the complete local NRD 4.17.3 README, settings for confidence and the current NRD
integration, PT-127, the handoff, relevant context and DEAD ENDS. The old bounced-only
NRD dead-end entry is superseded by PT-127 and the owner's 2026-10-11 acceptance.
Read NVIDIA's [gradient pass](https://github.com/NVIDIA-RTX/NRD-Sample/blob/simplex/Shaders/SharcUpdate.cs.hlsl)
and [confidence filter](https://github.com/NVIDIA-RTX/NRD-Sample/blob/simplex/Shaders/ConfidenceBlur.cs.hlsl)
as documentation; no NVIDIA source is copied or quoted into this tree.

## Validation order

1. Compile shaders and native/plugin in isolated folders; link existing matching NRD
   and ShaderMakeBlob libraries read-only (`TGPT_NRD_PREBUILT_LIBRARY/DEPS`). Verify
   `NRD_SUPPORTS_HISTORY_CONFIDENCE` in that build is enabled.
2. Hand coordinator one build for a running-world tavern comparison: NRD confidence
   Off / On, same NRD kind, 16 captured frames per step, exposure/gain checked.
3. Only after the game answers: one small GPU mode, light steady then extinguished,
   checking confidence itself and mutation-checking the replay. Never the full suite.

## Trial ready for the coordinator (18:46)

One binary pair is in
`C:/Users/Frede/AppData/Local/Temp/tgb/nrdconf-build/trial/`:

| File | SHA-256 |
|---|---|
| `TGPathTracing.Native.dll` | `A3BF12BE4668B659262F878DDCE83788B963B7E082095953C3D1BDAA36B22E38` |
| `TGPathTracing.dll` | `CAFEF9411C30EFBAC5CC18EDD66848944238B938E03364BC0BB9165D6FF1FE7C` |

These match the final native build and managed output. The copy's older
`artifacts/Native` DLL was not replaced: it is not this trial. No release zip or bridge
binary was changed. Merge this copy against `base20nc`, then the coordinator alone can
install the plugin/native pair using the normal PT deploy procedure. If main has moved,
rebuild after merging rather than replacing newer unrelated work with this snapshot.

The new recipe is `tavern-room@nrd-confidence`: 16 undenoised frames first, then NRD
Off / On / Off, 16 frames each. Every step explicitly uses `live: true` so candles keep
flickering. The first look uses `Run-PtHarness.ps1 -Scenario tavern-room@nrd-confidence`
without `-Measure`; only a later clean `-Measure` run can support timing claims. Inspect
lamp-lit wood, contact shadows, skin and moving patrons, and read exposure/gain alongside
the images. Stronger flicker can mean restored response to real candle changes, not
necessarily worse denoising: check the undenoised sequence and retained detail too.

The native log identifies allocation/activation and retirement. Its existing denoiser
stage includes all seven new dispatches; no separate timestamp/export was added. The
switch stays off by default. Neither appearance, responsiveness nor cost is validated
in the game yet.

## What was built

- `pt_nrd_confidence.hlsl`: one correlated replay dispatch at ceil(render size / 5).
  It uses each stratum's temporal DI reservoir, not a copy of the noisy final image.
  The previous record saves the light identity/UV/weight/frame, full receiver and view,
  hair data, the leaf's own transmission and physical RGB radiance. Its previous sample
  is re-lit using current light data and visibility before the record is overwritten.
  Sun lighting is evaluated alongside the selected local/emissive light. Direct surface
  emission is excluded. RGB differences detect a colour change even at equal luminance.
- `pt_nrd_confidence_filter.hlsl`: five geometry/normal-weighted 5x5 blurs with strides
  1..5, then one surface-aware resolve to the current pixel grid using `previous_place`.
  Blur the absolute change and radiance scale together. Convert their ratio to
  `1 - sqrt(relativeChange)` for REBLUR and square that for RELAX. No exposure or
  denoised history enters the gradient; the endpoints are exactly 1 and 0 by construction.
  This endpoint statement is arithmetic, not a GPU test result.
- `nrd_confidence.hpp` / `.inl`: private allocation, dispatch and retirement. Five
  textures: two R32G32F gradient buffers, R32G32B32A32F positions, R32G32_UINT packed
  shading/geometric normals, and a full-size R16F confidence map. One structured buffer
  of 176-byte `ConfidenceSample` records (eleven 16-byte rows; C++ stride assertion).
  At 1920x1080 this is about 21.0 MiB before allocation alignment. Actual allocations
  enter the denoiser memory total and the allocation log; no off-state texture allocation.
- New internal `NrdFrame` fields: `diffuseConfidence`, `specularConfidence`. They alias
  the same valid map. Both NRD resource slots are mapped, transitioned and restored.
  Confidence availability is true only when both inputs exist. The three RELAX
  confidence-driven spatial-relaxation parameters remain at NVIDIA's defaults (0).
- New public flag **only**: `TGPT_FRAME_NRD_CONFIDENCE = 1u << 21`, mirrored by
  `Tgpt.FrameNrdConfidence`; hidden Advanced bool `NRD lighting confidence`, false.
  No new export, public record/layout, ABI version, FrameConstants row, visible option,
  debug view or NRD-kind value. Existing RELAX/REBLUR selection is unchanged.
- The ray pass compiles separately for SM 6.6 and 6.9; the filter compiles once for 6.6.
  Shader Execution Reordering on the existing tracing passes remains unchanged. The
  additional PSOs are created during normal warm-up, including when the switch is off;
  no confidence GPU work runs off. Startup cost is not measured.

Reset/skip handling: cuts, changed scene, non-consecutive frame numbers, resize and
denoiser gaps invalidate the saved probes. New storage is never read before being
written. Turning confidence off retires all its resources with the prior consumer fence.
Shutdown and NRD's idle retirement clear them too. An origin relocation suppresses that
frame's gradient: the existing visibility RNG also hashes relative world coordinates,
so a relocation otherwise changes alpha sampling even with the old frame seed. Such
relocations occur after moving over 512 m from the scene origin, not every camera move.

No workaround was removed in this trial. No scene light or radiance is changed; this
only supplies NRD's optional history input. Existing NRD anti-firefly, history durations,
material factors and light sampling decisions remain as accepted in the game.

## Build and checks

- Native: `nrdconf-build`, NMake Release, real `vcvars64.bat`, full-path CMake. Both
  `TGPathTracing.Native` and `tgpt_gpu_test` built; the latter was never run. Native final
  link at 18:43. The existing GPU-test source emits double-to-float C4244 warnings.
- Shaders: `tools/Build-PtShaders.ps1 -BuildDir ...`, never `-First`. All three new shader
  objects compile with `-WX`, no warnings. Initial compiler errors (struct conditional
  selection and missing reservoir-storage include) were fixed before the first native
  build. `shaders-final.log` records the final three successful compiles.
- 54 existing compiled shader headers reused only after comparing compile commands,
  every CMake-declared dependency's bytes against main and its timestamp against the
  existing compiled header. The cache-copy script/log is in the build folder. No vendor
  source copied. Main's rain-wetness compile setting is retained (ON); speed, lamp-around
  and water-regroup compile trials remain OFF.
- NRD linked read-only from main's existing `NRD.lib` and `ShaderMakeBlob.lib` using
  `TGPT_NRD_PREBUILT_LIBRARY/DEPS`; no SDK CMake invocation. Main's CMake cache and
  generated NRD config both report history-confidence support ON. The normal source-build
  path now explicitly enables it too. SDK headers/shaders are read through existing roots.
- Managed net472 build: pass, 0 warnings/errors. Local cached reference nupkgs only for
  project restore. Initial restore attempted the referenced projects' net6 targets and
  failed locally; `-p:TargetFrameworks=net472` resolved it. The compiler reads the usual
  game reference assemblies; no game file was changed. The separate workload-advertising
  incident below means this must not be described as a completely offline build.
- Existing focused source check `SourceConsistency.test_path_tracing_nrd`: pass.
  This is compile/integration sanity, not evidence that the new confidence works.
- Recipe JSON parsed, every step checked live, capture counts 16/16/16/16/0.
- Byte comparison with `base20nc`: `pt_gi`, `pt_specular`, `pt_prepare`, `pt_walk`,
  `pt_common`, `pt_rtxdi`, `pt_nrd_prep`, `pt_di`, `pt_di_spatial` all unchanged.
- Generated DXIL declares the new sample UAV's structured stride as 176 bytes, matching
  the C++ assertion and allocation. Final trial-copy SHA-256 matches each build output.
- No new behavioral test or mutation check before the game answers, as instructed.
  No GPU process, GPU guard wait, game process or deployment was started by this worker.

Logs/scripts: `%TEMP%/tgb/nrdconf-build/{configure.cmd,configure.log,build.cmd,native.log,
native-final.log,shaders.log,shaders-final.log,managed.log,reuse_headers.py,header-reuse.log}`.
Review-only code delta: `nrdconf-build/nrd-confidence-code.patch`, 14 code/recipe paths;
the report is separate. No git operation was used to create it.

## Limits and next work

This is a direct-light gradient, not full-path confidence. Indirect changes visible only
through bounce paths, mirror-only light changes, the radiance cache and changing caustic
patterns are not fully detected. Existing NRD anti-lag still handles what this probe misses.
One sample per stratum can miss an unsampled light's change; newly introduced lights are
not reconstructible from a previous sample that never selected them. The recipe tests
the known flickering candle case before broadening the signal. The normalization curve
and dynamic-surface relaxation still need the game to judge them. No visual/speed win is
claimed, and PT-127 remains open.

After a positive tavern answer, implement the requested small GPU mode: an illuminated
planar receiver with stable light (confidence 1), then the sole light extinguished
(confidence 0 on supported lit strata), with camera motion/empty history checked separately.
Repeat with replay seed restoration deliberately broken and with current-light re-shading
disabled to demonstrate detection. Use only that mode, only with the game closed and main's
`Run-PtHarness.ps1 -GpuCheck` returning 0; `TGPT_TEST_STARTUP_SECONDS=900` for cold startup.

Worker notes remain here per `docs/AGENT-WORKERS.md`'s worker-specific rule. Proposed
coordinator harness entries: PT-127 now has a direct-light confidence trial, bit 21,
default off, native/plugin built, `tavern-room@nrd-confidence` next; no game/GPU verdict.
PROJECT-CONTEXT should retain the visibility-seed replay requirement, GI replay gap,
and the .NET workload-advertising containment lesson. This report is the worker's R20
addendum; the coordinator owns BACKLOG, PT-REQUESTS, PROJECT-CONTEXT and ROUNDS.

## State when stopped

2026-10-11: implementation and trial builds complete; handoff ready in this report.
No game verdict has arrived. The brief makes that a prerequisite for the small GPU
fixture, so neither a new fixture nor a GPU test has been run. No game or deployment.
.NET work stopped after discovering its unrequested advertising refresh; no attempt
to undo Windows Installer state or remove anything outside the allowed folders.

## Containment incident (discovered 18:45)

The first `dotnet build` (PID 50728, 18:41:00) used a private `DOTNET_CLI_HOME`,
private TEMP/TMP, private NuGet package extraction, `RestoreSources` pointing only at
two already-cached .NET reference packages, `NuGetAudit=false`, telemetry disabled and
`DOTNET_GENERATE_ASPNET_CERTIFICATE=false`. Nonetheless .NET 10.0.201's independent
background workload-advertising refresh ran. This was not a requested workload install.

Its logs show `Microsoft.NET.Workloads.10.0.200` 10.204.1 and an MSI `ACTION=ADMIN`
extraction into `nrdconf-build/qv250odi.3st/msi`, followed by a workload-set JSON under
the private dotnet home's `sdk-advertising`. The source of that temporary nupkg is not
established by these logs, so a background network download cannot be ruled out. The
MSI log also names `C:\Config.Msi` rollback housekeeping; I cannot prove every Windows
Installer service write stayed inside the allowed roots. No SDK/workload was explicitly
installed, and no installed workload change is established by the log's administrative
extraction. This remains a containment failure of the chosen build invocation.

Evidence, all under `%TEMP%/tgb/nrdconf-build`:
- `Microsoft.NET.Workload_50728_20261011_184100_124.log`
- `Microsoft.NET.Workload_50728_20261011_184100_124_6d04ff7414ee9b159039c852b47a0121-x64_AdminInstall.log`
- `Microsoft.NET.Workload_36084_20261011_184118_934.log` (second build: no extraction shown)

Do not repeat that invocation as an offline boundary. Future worker .NET invocations
must also disable workload advertising updates (`DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true`)
and the workload resolver (`MSBuildEnableWorkloadResolver=false`) before launching dotnet.
No further .NET process was started after this discovery.
