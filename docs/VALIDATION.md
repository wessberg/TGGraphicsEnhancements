# Validation record - integrated v0.3.1

This update integrates the existing native backend and preloader into one source
repository. The preparation environment has no .NET SDK, PowerShell runtime,
Windows SDK, game assemblies or game/GPU execution. No new managed binary has
been compiled here and no PowerShell script was executed here.

Executed for this integration:

- 14 existing source consistency checks and 12 new autoload/build-graph checks.
- Two portable C++ test executables (80 ABI/policy/ring and 61 interop checks).
- Git patch checks described in validation/integration-patch-checks.txt.

The current run logs are in `validation/integration-*`. They record portable C++
test execution and Python source/structure checks. Patch application is tested
against the supplied v0.3.0 baseline and with synthetic local edits; this is not
a test against unuploaded changes in the user's working tree.

The native files and build scripts are unchanged. Their portable regression tests
remain useful but do not validate automatic startup, GPU code or Windows APIs.
The PowerShell scripts include local parser/behavior tests for execution on the
user's Windows machine. Those checks were not executed in this environment.

---

## Historical v0.3.0 preparation record (not proof of v0.3.1 startup)

# Validation record — v0.3.0

Date: 2026-09-18. Preparation environment: Linux x86-64, GNU C++ 14.2.0,
CMake, Python 3. No Windows SDK, MSVC, .NET SDK, PowerShell runtime, game
assemblies, NVIDIA runtime, or compatible GPU is available in this environment.

## Executed successfully

```text
cmake -S . -B <test-build> -DTGFG_BUILD_NATIVE=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build <test-build> --parallel
ctest --test-dir <test-build> --output-on-failure
<test-build>/tgfg_policy_tests
<test-build>/tgfg_interop_tests
python tests/source_checks.py
```

Results:

- **80 portable ABI/control/frame-ring checks passed.** They exercise packet
  sizes/offsets, valid and invalid requests, source-DX11/private-bridge gating,
  missing inputs/frame states, capability limits, suspension/timeouts,
  target-to-Reflex calculations and non-overwriting CPU frame-ring behavior.
- **61 portable interop-policy checks passed.** They exercise texture/color-space
  eligibility, crop overflow, resize defaults/bounds, stable logical buffer zero,
  reference PQ values and monotonically increasing fence numbers. They do not
  create or synchronize real GPU fences.
- **14 source/package consistency tests passed.** These inspect framework/version
  declarations, ABI/export/reason-enum agreement, build input existence, direct
  SDK call sites, API separation, textual producer/consumer order, optional HUD
  input handling, shader source presence, early-loader structure, launch preflight
  and testing disclosures. These are textual checks, not C++ type checking or
  proof that graphics behavior is correct.

CTest runs **two portable test executables**, not 141 independent GPU tests.
The logs in `validation/` record the actual runs made for this source package.

## Included but NOT executed here

The native Windows build includes `tgfg_shader_tests`, which compiles the four
embedded HLSL entrypoints using the installed D3DCompiler. The runtime embeds the
same HLSL file, so this is a real compiler check when run on Windows; it does not
create a GPU device or test displayed HDR colors. `Build-Native.ps1` invokes CTest
after a successful build and stages the binaries only after tests pass.

`Test-Setup.ps1` checks local paths, managed version, runtime selection and the
native build's file hashes. It does not load the native backend into PowerShell,
initialize Streamline, or certify GPU compatibility. PowerShell execution of these
scripts has not been tested in the preparation environment.

## Not established

- MSVC compilation/linking of the native DLL or launcher against actual SDKs.
- Mono or IL2CPP compilation against the user's installed game assemblies.
- The entrypoint-rendezvous launcher's behavior on Windows and with Steam startup.
- NVIDIA runtime loading, signature checks, project identity and feature support.
- Actual D3D11/D3D12 shared-resource creation or GPU-fence synchronization.
- The full swapchain COM contract with Unity, ReShade and the user's RenoDX addons.
- This FoA build's NGX entrypoint discovery, resource formats or frame correlation.
- Unity/HDRP camera conventions, motion-vector scaling, camera cuts or history.
- Successful generated output, pacing, latency, fixed MFG or Dynamic MFG.
- Correct scRGB-to-HDR10 output in the user's Windows/monitor/RenoDX configuration.
- A game-specific post-RenoDX/pre-HUD extraction hook (not installed by this build).
- Resize/fullscreen transitions, process exit, driver failure or mode transitions.

The supplied user diagnostic established **v0.2 Mono plugin loading and Video-menu
insertion**. It did not run this v0.3 native bridge, nor establish IL2CPP behavior.
The adapter's discovery structure is retained; its compact status text is changed.

This package must not be relabelled a tested binary release on the basis of
portable policy tests. Windows compilation may expose build errors; game testing
may expose integration faults that require source changes.
