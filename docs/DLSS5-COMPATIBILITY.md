# Neural Rendering community-runtime compatibility - R17, 2026-10-09

The owner wants the supplied RTX 2000/3000/4000 runtime to work while preserving the existing
NVIDIA integration. This is NR-30/31/32. The path tracing agent is working in the same tree.

## Finding

Removing TGNR's signature check is insufficient. NVIDIA's NGX driver loader independently
verifies the runtime and rejects this file before it is loaded. A separate direct-runtime adapter
can get further, but the model also checks the caller's identity. Supporting this patched build
therefore requires an explicitly nonstandard compatibility route. It cannot honestly be described
as an unmodified, fully NVIDIA-supported SDK path.

The separate adapter is implemented and built in scratch. The owner approved the caller-name exception and
then rejected a per-file allowlist: patched builds change frequently and should not need a mod update. Selection
is automatic from the supplied DLL's signature. Community compatibility is checked by exported interface and
runtime support, not a pinned hash or file-version list. No installed game file or release archive has changed.

## Evidence

The supplied `C:\Users\Frede\Downloads\patched\nvngx_dlssnr.dll` is x64, version 310.8.0.0.
Its SHA256 remains `E67DEE209320CDAFE0E93E45675D7AA34323A53ACC57A72B2E40A181581C989A`.
Authenticode reports `HashMismatch`.

The standalone diagnostic is in `build/nr/compat-r17/`. It uses the public NGX SDK and the RTX 5080,
without launching FoA. It has two routes: normal SDK initialization with TGNR's project identity,
and direct calls to the supplied model's exports using their distinct function signatures.

| Probe | Result | What it establishes |
| --- | --- | --- |
| `core-patched.log` | NGX reports a failed signature verification in `NGXLoadAndValidateSnippet`; `DLSSNR.Available=0`; loaded model handle is null | The driver rejects the file before loading it. This is not a hardware-support test. |
| `direct-patched.log` | Windows loads the exact file; direct initialization returns `0xBAD00002` with app ID 0 and SDK API 0x15 | Loading the file alone is insufficient. |
| `direct-patched-reported.log` | Same failure using the model's own reported app ID `0x0E658703` and API 0x13 | Merely matching the reported identity/API does not fix direct initialization. |
| `caller-control.log` | Byte-identical probe executable copied to `nvngx.dll-loader-probe.exe`: direct initialization and shutdown both return success, with the same arguments | The caller-name check is a separate barrier. This control changes only the diagnostic executable's name. |
| `frame-patched-debug.log` | Direct scratch/create/evaluate/release/shutdown succeed; 256x256 RGB has zero invalid or untouched values; mean absolute change 0.01039082 | The patched model can render through the direct route on this RTX 5080. This is a synthetic-frame check, not an older-GPU or in-game result. |

The driver probe was stopped after it had recorded the result and remained in its telemetry shutdown.
An initial direct attempt failed to create a D3D12 device inside the sandbox; the rerun outside the
sandbox succeeded in device creation and recorded the actual model refusal. That device-creation
failure is not counted as runtime evidence. No game executable was renamed, and no driver, model,
Windows trust function or system setting was patched.

This corrects R16's inference that the signature-bypass probe had loaded the model and found the
RTX 5080 unsupported. Successful NGX initialization and a capability value of zero did not establish
either claim. The new verbose driver log and null module handle resolve that ambiguity.

## RenoDX comparison and its limits

The public [RenoDX tree](https://github.com/clshortfuse/renodx/tree/374d07bf0e2fcef112ed1dfee98541bcf16487dd)
does not contain the current DLSS 5 add-on's implementation. Its published DLSS hooks unwrap
ReShade objects and call NGX; they are not the community NR loader. The tree index is preserved
as `build/nr/compat-r17/renodx-tree.json`.

The already-downloaded ShortFuse 26.1003.2350 add-on contains diagnostics for attaching a model,
direct `Init_Ext`, and a required caller-identity hook. See the versioned
[package](https://github.com/RankFTW/rhi-repo/releases/tag/renodx-dlss-SF-26.1003.2350) and local
`build/nr/renodx-review/shortfuse-ui-strings.txt`. These strings support the existence of that route;
they do not prove exactly how every current RenoDX release implements it.

Other public implementations explicitly document a
[direct-model caller adapter](https://github.com/Dagherbou/OptiScaler_DLSSNR/blob/dlss-neural-rendering/OptiScaler/dlssnr/README.md#why-a-forwarder-dll-exists).
Our byte-identical caller-name control independently reproduces that requirement for the owner's
exact DLL. No community integration code was copied. No proprietary DLL was disassembled or modified.

NVIDIA's [SDK header](https://github.com/NVIDIA/DLSS/blob/v310.9.1/include/nvsdk_ngx.h)
distinguishes application-to-driver and driver-to-model signatures. Its
[programming guide](https://github.com/NVIDIA/DLSS/blob/v310.9.1/doc/DLSS_Programming_Guide_Release.pdf)
documents that NGX cannot load a DLSS library with a missing or corrupted NVIDIA signature.

## Implemented boundary

The signed NVIDIA SDK route stays the default. An unsigned or modified player-supplied `nvngx_dlssnr.dll`
automatically selects the separate community adapter. No extra setting is needed. Other mods retain their
existing loaders and trust checks. This implements the owner's later instruction to remove per-file approvals.

- A valid NVIDIA-signed supported profile uses the current SDK route, unchanged.
- Community DLLs must be x64 and expose the direct NGX interface this adapter implements (reported API 0x13
  and all required exports). File versions are logged, not used as a community allowlist. Initialization,
  runtime feature requirements and feature creation can still refuse an incompatible DLL or device.
  SHA256 records which file ran and catches replacement between selection and loading; it is not an approval list.
- The TGNR-owned helper `TGNeuralRendering.nvngx.dll` contains the direct model calls and the caller-name
  exception. It is not installed as the driver's DLL and does not
  intercept Windows trust functions or alter the model. Its unusual name is an explicit compatibility
  requirement, not evidence of NVIDIA support.
- Keep this helper behind the NR backend interface: initialization, capabilities, scratch, create,
  evaluate and release. Never mix a driver-created feature handle with direct-model operations.
  Keep the existing shared D3D12 device, NGX lock, guide inputs, colour conversion and frame ordering.
- Keep patched files out of the shared signed-runtime search-path registration. SR, RR and FG retain
  their existing trust policy. Record the chosen backend, exact loaded path and hash in diagnostics.
- Read support from the selected runtime and require actual creation/evaluation. Do not maintain a
  list claiming that every RTX 2000/3000/4000 card works. Failure leaves the original frame intact.

The helper rechecks the selected file's hash under a handle that denies writes/replacement and retains that
handle while the model lives. It verifies the loaded path, rejects another already-loaded NR model, implements
the public parameter interface and owns every feature through release. A scoped lock also prevents compiler
tail calls from moving the model's return address outside the helper. No core NGX shutdown is called.

CMake and package definitions include the helper beside the main NR DLL. Package checks require the helper
and continue to exclude the model. A PT-only native manifest can retain an older NR stage without the helper;
packaging the new NR build still requires it. No release archive has been produced.

This remains a separate API contract to maintain. A future patch using the same interface needs no mod update;
an incompatible interface still can. The earlier research's licensing limits remain distinct from technical feasibility.

## Integrated checks on RTX 5080

Outputs are under `build/nr/compat-r17/`. Native Release builds use VS 2022 x64, /O2 /W4 and public DLSS SDK
310.9.1. Managed ABI and bridge are unchanged. No PT shader build or full PT suite ran.

| Check | Evidence |
| --- | --- |
| Community DLL through main/helper | `integrated-patched-final.log`: real rendering, finite changed RGB, preserved alpha, live strength/style, Off/restart and resize pass. Neutral sRGB error 0 byte levels; HDR neutral error 0 over 0..981 scene units. |
| Signed SDK route, helper absent | `integrated-signed-final.log`: the same image/control checks pass. |
| Shared bridge, RR and FG/PT modules | `coexist-patched-final.log`: 32 RR frames, 30 following NR evaluations and NR Off/restart pass. Streamline loaded. This does not prove generated-frame output or current PT scene geometry. |
| Multipass | `multipass-check-final.log`: 1/2/3/4 passes exactly match independent chained single passes (maximum error0), retain history, preserve neutral SDR/HDR and alpha, release on Off. All three deliberate chaining faults detected. |
| Final automatic-selection/ABI/file checks | CPU runtime test passes. Four helper/unsigned-fixture cases pass, including acceptance of an unlisted file. Five deliberate faults are detected: unsupported API, writable file, ignored support refusal, changed file and stale feature handle. |
| Packaging | 101 mocked filesystem checks include required helper and model exclusion. Real scratch manifests pass both with the new helper and with an older NR stage during a PT-only build. |
| Source checks | 102 pass in the final run. |

The final GPU logs above use automatic interface-based selection without an allowlist. The earlier logs remain
as investigation history. One mistaken `--multipass` invocation on a production DLL
reported its absent fixture-only export. The proper runner builds that export in scratch. An initial retry
stopped at the PT harness reservation, before GPU work; it now uses the shared `-GpuCheck` guard as AGENTS requires.

## Initial frame proof and remaining limits

Initialization alone is not a successful image. The 256x256 frame probe uses an independent
implementation of the public parameter interface, sentinel output, a fenced readback and checks for
finite, written, changed RGB pixels. It ran after FoA closed. A first attempt terminated with
`0xC0000409` after parameter population; an instrumented rerun completed successfully. No rendering
fix was made between those attempts, so that first termination must remain recorded rather than
being called fixed. The direct population export did not publish `DLSSNR.Available`; successful
feature creation and the measured output establish support for this probe's device.

The integrated main/helper route subsequently passed its image and shared-device checks without that termination;
this does not explain the original standalone-probe crash.

RTX 2000/3000/4000 still need an actual create/evaluate/image check. No such card is available here. A runtime
refusal remains a refusal; there is no spoofed GPU generation or table of available technologies. The community
backend also awaits one focused in-game run. Existing signed-runtime SDR/HDR/ReShade evidence is unchanged;
this session makes no new in-game visual-quality claim.

Deployment follow-up (NR-35,10-09 12:22 local): owner installed the normal game/BepInEx IL2CPP and requested
NR deployment. NR1.2.3 net6.0 built in isolation; eight payload files and the supplied patched runtime installed
and hash-verified. The native/helper are exactly the tested R17 files above. Receipt:
`build/nr/il2cpp-r17/deployment-result.json`. Other mod/config/loader files preserved. Game not launched;
IL2CPP/community rendering and older-card validation remain pending.

## NR-33: can the RTX 5080 validate older cards?

Only partially. Source audit confirms that our adapter does not filter NVIDIA device IDs or require RTX50.
The bridge creates D3D12 with feature level11_0 (`native/bridge/src/device.cpp`); NR's own transfer shaders
targetcs_5_0 (`native/neural/src/extension.cpp`). Runtime interface/support checks decide availability.
The CPU fixtures exercise acceptance/refusal and the real5080 checks exercise rendering and sharing.

Those checks cannot reproduce the runtime's GPU-code selection, instructions, driver behaviour or memory
limits on Turing/Ampere/Ada. Spoofing an adapter name or reported feature level does not change the physical
GPU. NVIDIA's [Blackwell compatibility guide](https://docs.nvidia.com/cuda/blackwell-compatibility-guide/index.html)
explains that GPU binaries target particular compute capabilities and that PTX is compiled for the device
actually running it. Its `CUDA_FORCE_PTX_JIT` procedure checks compatibility with the current newer GPU,
not backward compatibility; applicability of that CUDA switch to this proprietary NR runtime is not established.
Compute Sanitizer checks execution errors; it is not evidence of a different GPU generation being exercised.

No hardware emulation, model disassembly, GPU spoof or additional GPU run was performed for this question.
The useful next evidence is the existing standalone `tgnr_gpu_smoke` on actual RTX20/30/40 cards with the
same patched runtime and mod build. It needs no FoA launch. Check capability result, real feature creation,
finite changed output, neutral SDR/HDR, Off/restart and resizing; keep adapter/driver/runtime identities
with the log. One card per generation demonstrates those devices, not every VRAM/driver configuration.
