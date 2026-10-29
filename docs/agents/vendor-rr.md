# Vendor RR / Ray Regeneration — PT-127

Worker copy: `vendorrr`, base `base20rr`. Started 2026-10-11. No questions needed; the brief supplies scope and authority.

## Plan (written before code changes)

1. Read the local NVIDIA RR guide and AMD denoising guide/header fully, then trace the affected inputs and lifecycle.
2. RR 2/18: preserve RGB specular reflectance across the D3D12/D3D11 handoff; remove invented diffuse reflectance.
3. RR 4: require reflection distance independently of the optional transparency snapshot throughout managed/native handoff and evaluation retry.
4. RR 19: expose the RR optimal-settings query and use its exact dimensions for RR. Keep preset F as instructed; record NVIDIA's Default recommendation.
5. AMD 6/14/15: stable radiance units, matching specular factors, and a history reset after every dispatch gap or scale change.
6. AMD 28: hidden validation/debug controls and asynchronous context creation, preserving GPU lifetime ordering.
7. Compile shaders, affected native targets, and both managed plugins; run source checks. Record any failures and all remaining runtime validation.

## Constraints and decisions

- Write only in this copy and `C:/Users/Frede/AppData/Local/Temp/tgb/vendorrr-*`; SDKs in the main tree are read-only inputs.
- No GPU tests, game runs, deployment, packages, downloads, installs, commits, or mutating Git commands.
- RR requires in-game validation by the coordinator. AMD Ray Regeneration cannot execute on this RTX 5080.
- NRD and the excluded producers listed in the brief are out of scope. Shared compose/tracer edits will be confined to the assigned RR/RRG paths.
- NVIDIA RR guide §3.13 recommends Default for every mode; F is supported and stays pinned by the brief's explicit instruction.

## Progress

- Read BACKLOG, relevant PROJECT-CONTEXT sections and DEAD ENDS, the latest round handoff, and the review.
- Read the full local August 2026 NVIDIA RR guide (including §3.13 and §5.5), AMD's full `denoising.md`, and the denoiser API header. No vendor source or guide text added to the repository.
- All eight assigned changes implemented. Both managed builds, both native modules, all 46 PT shader variants and final source checks (116) pass. Completed 2026-10-11 16:04 local time; runtime validation remains the coordinator's work.

## Changes and what to inspect in game

| Finding | Guide and change | Expected visible effect; coordinator's check |
|---|---|---|
| RR 2 | §3.4.2: the compose pass preserves the RGB specular factor. Internal output C now holds RGB reflectance plus hit distance in A (`RGBA16_FLOAT`); D3D11 reads RGB without widening a scalar. No exported guide ABI change. | Colored metals retain their different channel reflectances in RR's guide. Inspect a brass/copper object at several view angles, and the RGB guide. Hair's existing scalar producer and guide-surface selection remain RR 3's separate job. |
| RR 4 | §3.4.8/9/11: reflection distance is required. The optional transparency snapshot is allocated, copied, bound and retried separately. A missing distance skips RR with an explicit reason; removing transparency on retry retains distance and resets history. The managed producer/link preserve valid distance when the snapshot is absent. | Reflection tracking should survive missing/refused transparency input. Coordinator should exercise absent snapshot with a valid distance, then absent distance: the former must run RR, the latter must fall back. |
| RR 18 | §3.4.1: remove the diffuse 0.05 floor and emission-derived tint. Sky is initialized to the guide's recommended neutral 0.5; specular/normal sky guides remain zero. | Dark cloth/soot stays dark in the material guide; emitter material color stays independent of emitted light. Compare an emissive object and dark fabric against their material guides. Changes to RR's reconstruction are expected; none measured here. |
| RR 19 | §3.2/5.5: new additive `TGUP_GetRrOptimalSettings` calls `NGX_DLSSD_GET_OPTIMAL_SETTINGS`; managed RR queries the actual output size and selected NGX mode. HDRP's size calculation returns those exact two dimensions for the selected camera's full viewport, before targets/jitter are sized. No +0.1 percentage point for RR. A mismatch refuses RR. | Render resolution may differ from SR and lose the previous several excess pixels. Verify every RR mode, output resize, DLAA and odd output dimensions; the native creation log must agree with the RR query. SR/FSR/XeSS sizing remains as before. The new hooks are Mono-only; failure to install them disables RR hosting without disabling FSR/XeSS. Preset F unchanged by instruction. |
| AMD 6 | §2.5's radiance contract has no history-rescaling input: convert all three radiance signals from the changing tracer storage reference into fixed physical units before AMD; restore the current storage units after. Change the fixed scale only with a reset (camera cut/gap, or reference outside 1/4–4 times that scale). Alpha/distances never scale. | Prevent brightness lag/pulses caused solely by storage-reference changes. On RX 9000, enter/leave sunlight and switch bright lights while comparing against undenoised accumulated lighting. |
| AMD 14 | §2.5/5.5: one `max(spec, 1e-3)` factor divides and multiplies reflections in the same shader. Both conversion directions run around AMD; composition and metering receive the restored signal. | Avoid darkening low-reflectance channels. Inspect dark/colored metal and very low-reflectance surfaces, including the no-op algebraic round trip. |
| AMD 15 | §2.5.3: the context records its last successful dispatch frame and resets after any skipped frame or failed dispatch. The tracer separately invalidates its scale/history on a denoiser gap. | Selecting None/NRD briefly and then RRG must not reuse history from before the interruption. Also check cuts, resize and temporarily unavailable context. |
| AMD 28 | §2.4: `TGPT_RRG_DEBUG=1` enables `FFX_DENOISER_ENABLE_DEBUGGING` and `FFX_DENOISER_ENABLE_VALIDATION` at creation. Creation runs on a retained-device background CPU task; rendering polls it without waiting and uses its existing fallback until ready. Old live contexts still retire after the GPU fence; a resized pending context is discarded before dispatch. | Enabling RRG/resize should avoid waiting for AMD's pipeline compilation on the render thread. Validation messages reach the native log. Debug flags are exposed; a visual debug-output overlay is not added by this flags-only change. |

Removed workarounds: grey packing of ordinary specular reflectance; the diffuse floor and emission tint; dropping reflection distance with transparency; SR-derived RR render scale and its margin; mismatched AMD specular factors; synchronous AMD context creation. The excluded hair/glass/highlight and NRD findings remain open.

The RGB handoff widens one shared surface from 2 to 8 bytes per render pixel: approximately 11.9 MiB extra at 1920×1080 (before alignment). Existing memory accounting uses resource size and follows the change. AMD also gains one inverse-conversion dispatch; its cost needs measurement on supported hardware.

## Build isolation

- NRD's vendor CMake writes `Shaders/NRDConfig.hlsli` into its source folder. Configuring directly against the main SDK would violate the worker's absolute read-only rule. A scratch-only wrapper at `%TEMP%/tgb/vendorrr-nrd` imports copies of the existing main-tree `build/native/nrd/NRD.lib` and its `ShaderMakeBlob.lib` dependency, with private header/shader copies outside the repository. NRD is unchanged and is not rebuilt.
- Other SDK roots point at the main tree as read-only inputs. `FETCHCONTENT_FULLY_DISCONNECTED=ON`; no downloads.
- Native PT build: `%TEMP%/tgb/vendorrr-build`. Native BU build: `%TEMP%/tgb/vendorrr-upscale`, separate so its NMake build cannot race the PT shader runner.
- Build containment correction: the first native NMake link log showed a response file under the system TEMP directory, outside the permitted roots. Subsequent native commands explicitly set TEMP/TMP to the private build directory. No attempt was made to alter or clean files outside the allowed roots.
- Managed builds use this copy's `bin/obj`, an isolated dotnet home/temp directory, and an offline NuGet source copied from the two already-cached .NET Framework reference packages. No package download or deployment.

## Validation

- TG Better Upscaling managed net472: pass, 0 warnings/errors.
- TG Path Tracing managed net472: pass, 0 warnings/errors.
- TGBetterUpscaling.Native: pass; final NMake command exited 0.
- `tgpt_gpu_test` executable: built only in `%TEMP%/tgb/vendorrr-upscale`; never run. Its unchanged source reports existing double-to-float conversion warnings.
- `python -I tests/source_checks.py`: 116 pass after updating the assertions that required the old guide floor/packing, optional-input coupling, compose-only remodulation, and original hook count. Final rerun passes after review. No mutation checks: AGENTS.md defers them until the owner accepts the direction in game.
- D3D11 shader CPU compile checks: all four affected variants pass. The two composite variants warn about a potentially uninitialized value at line 412; the pristine base produces the identical warning. No device/GPU work.
- Excluded six files and the entire NRD dispatch block: byte/text comparison against `base20rr` passes.
- PT shaders: 46 compiled, 0 failed (16:01:41). Both compose variants and the AMD guide shader have empty warning/error logs. Shared compiler/memory guard respected; queued behind coordinator priority 15:26-15:48.
- TGPathTracing.Native: pass, final NMake command exited 0. Every C++ source compiled. First link failed because the scratch NRD wrapper omitted its ShaderMakeBlob dependency; adding that copied prebuilt dependency fixed the link. No repository or vendor-source fix was needed.
- No GPU tests or game runs. RR still needs the coordinator's `tgup_gpu_smoke_with_pt` and in-game run; RRG needs supported AMD hardware. No visual correctness claimed. For brightness comparisons, read the exposure/gain log and compare against accumulated undenoised light; an image alone cannot establish unchanged exposure.

Build logs: `%TEMP%/tgb/vendorrr-build/{shaders.log,native-final.log,native-link-final.log,managed-bu.log,managed-pt.log,source-checks-final.log,d3d11-compile.log,d3d11-base.log,source-integrity.log}`;
BU native and the unrun test executable: `%TEMP%/tgb/vendorrr-upscale/build-final.log`. Native debug/trial CMake
options use their defaults (including rain wetness and Mega Geometry off); these are compile-check builds.

SHA-256 of completed outputs:

| Output | SHA-256 |
|---|---|
| TGBetterUpscaling.dll (copy: src/TGBetterUpscaling/bin/Release/net472) | `9F7B7278A9EE448EB7A8FB877027327344F21FAF326DFCDE9807F4FCDB8A963F` |
| TGPathTracing.dll (copy: src/TGPathTracing/bin/Release/net472) | `8B0C63EA5E3675678F037BC478B80E673F74DD45D12FF22D5754D2D1E1FB6A82` |
| TGBetterUpscaling.Native.dll (vendorrr-upscale) | `BB7C716665F9A34B5386B3A7391BE1A87DAAC1BB1F6880AE3FACF325C528925E` |
| TGPathTracing.Native.dll (vendorrr-build) | `F6763066E4488E8C2713C18D5AF2003D63F1803AEFA424152F4C50EA780D6141` |

## Integration notes

- Merge the source changes into the coordinator's current tree; these worker binaries use a copied prebuilt NRD library and are compile evidence, not deployment artifacts.
- The overlapping files are `tracer.cpp`, `pt_compose.hlsl`, `d3d11.hlsl`, `pt_rrg_guides.hlsl` and source checks. Preserve the coordinator's NRD, hair, highlight, glass and previous-depth work when merging.
- Rebuild both native modules and both managed plugins together: the RGB shared surface format and RR dimension-query export require matching versions. No bridge API/version change, packaging change or deployment was made.
- Keep PT-127 open until the coordinator's required smoke/game checks and supported-AMD checks pass. No visible fix is claimed here.

## State when stopped

Worker work complete. No running worker builds or tests remain. All assigned source changes and permitted checks are done. No GPU test, game run, deployment, package or commit. RR preset F retained. PT-127 remains open for excluded findings and runtime acceptance; the TEMP-path containment correction is recorded above.
