# Primary references

Reviewed 2026-09-18. The source targets Streamline 2.14.1, rather than assuming
older DLSS-G headers include Dynamic MFG. These links are documentation/source
references, not bundled dependencies or evidence of successful game testing.

- NVIDIA Streamline DLSS-G integration guide (required/optional inputs, API and HDR restrictions, resource lifetime, dynamic targets):
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md
- NVIDIA general integration guide (initialization, proxies, loading):
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuide.md
- NVIDIA Reflex guide (Sleep, markers, frame identity):
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideReflex.md
- NVIDIA actual DLSS-G option/state definitions (multiplier semantics, Dynamic, runtime caps):
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/include/sl_dlss_g.h
- NVIDIA resource types and lifecycle definitions:
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/include/sl_core_types.h
- NVIDIA matrix/constant conventions:
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/include/sl_consts.h
- NVIDIA implementation of slGetNativeInterface (COM reference ownership):
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/source/core/sl.api/sl.cpp
- NVIDIA NGX declarations and parameter definitions:
  https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx.h
  https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_defs.h
  https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_params.h
- Microsoft DirectX headers (API declarations; Windows compilation remains required):
  https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3d12.h
- MinHook v1.3.4, the detouring dependency:
  https://github.com/TsudaKageyu/minhook/tree/v1.3.4
- Unity ScriptableRenderContext.ExecuteCommandBuffer:
  https://docs.unity3d.com/ScriptReference/Rendering.ScriptableRenderContext.ExecuteCommandBuffer.html
- Unity CommandBuffer.IssuePluginEventAndData:
  https://docs.unity3d.com/ScriptReference/Rendering.CommandBuffer.IssuePluginEventAndData.html
- The user's linked DLSS 5 addon (documents Mono/BepInEx 5 + D3D11):
  https://www.nexusmods.com/taintedgrailthefallofavalon/mods/320
- FoA Mod Manager (optional, separate configuration UI; not a native-menu API dependency):
  https://www.nexusmods.com/taintedgrailthefallofavalon/mods/167

## Additional v0.3 references

- Microsoft shared D3D11/D3D12 fences:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nf-d3d11_4-id3d11device5-opensharedfence
- Microsoft full D3D11 pipeline-state preservation:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-swapdevicecontextstate
- Microsoft scRGB units and HDR10 output color-space requirements:
  https://learn.microsoft.com/en-us/windows/win32/direct3darticles/high-dynamic-range
- Microsoft DXGI buffer access / flip-model behavior:
  https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getbuffer
  https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model
- ReShade's actual D3D11 stable-buffer-zero usage (source reference, not copied):
  https://github.com/crosire/reshade/blob/main/source/d3d11/d3d11_impl_swapchain.cpp
- Microsoft Windows debugging-event lifecycle used by the launcher:
  https://learn.microsoft.com/en-us/windows/win32/debug/debugging-events
- Open Shaders DX11/DX12 implementation, studied as an architectural precedent,
  not a dependency, port of Skyrim code, or evidence that TGFG has been tested:
  https://github.com/alandtse/open-shaders/blob/dev/src/Features/Upscaling/DX12SwapChain.cpp

The upstream `main`/`dev` links are reference snapshots reviewed during this task,
not version-pinned downloaded dependencies. Build inputs are the user's actual
SDK checkout, MinHook v1.3.4, and Streamline 2.14.1. No hardware performance result
from another mod is attributed to this implementation.

## v0.5.0 references (AMD FSR, Intel XeSS, latency technologies)

Reviewed 2026-09-19. Build inputs are the exact releases below; the runtimes are the signed
binaries shipped in them.

- AMD FSR SDK v2.3.0 (FSR API headers at the tag; signed loader and frame generation DLLs from
  FidelityFX-Samples-v2.3.0-prebuilt.zip):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases/tag/v2.3.0
- FSR API, DLL structure (the loader finds effect DLLs by module name):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.3.0/Kits/FidelityFX/docs/getting-started/ffx-api.md
- FSR frame generation API (configure, PrepareV2, callback dispatch, shutdown, thread safety):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.3.0/Kits/FidelityFX/docs/techniques/frame-interpolation-api.md
- FSR frame generation swapchain (creation for an HWND, pacing, waitable object):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.3.0/Kits/FidelityFX/docs/techniques/frame-interpolation-swap-chain.md
- FSR 4 ML frame generation requirements (RX 9000, camera data):
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.3.0/Kits/FidelityFX/docs/techniques/frame-interpolation-ml.md
- Intel XeSS SDK v3.0.2 (XeSS-FG developer guide 1.3, XeLL developer guide 1.3, headers, signed DLLs):
  https://github.com/intel/xess/releases/tag/v3.0.2
- AMD Anti-Lag 2 SDK v2.0.0a (header, FSR frame generation private-data handshake):
  https://github.com/GPUOpen-LibrariesAndSDKs/AntiLag2-SDK/releases/tag/v2.0.0a
- NVIDIA Streamline SDK 2.14.1 release (streamline-sdk-v2.14.1.zip: signed production runtime in
  bin/x64 with nvngx_dlss.license.txt and reflex.license.txt, Streamline's MIT license.txt):
  https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1
- NVIDIA DLSS SDK 310.9.1 (signed `lib/Windows_x86_64/rel/nvngx_dlss.dll`, file version 310.9.1.0,
  SHA-256 3975567B8943C53ACCE397F2B72380092F84F162D00B0D2C7D08A1025C563983; `LICENSE.txt`, the NVIDIA
  RTX SDKs license):
  https://github.com/NVIDIA/DLSS/releases/tag/v310.9.1
- NVIDIA Streamline DLSS-G guide, swapchain teardown/recreation (section 18):
  https://github.com/NVIDIA-RTX/Streamline/blob/v2.14.1/docs/ProgrammingGuideDLSS_G.md

## 1.1.0 references (TG Better Upscaling, TG Graphics Bridge)

Reviewed 2026-09-22. The runtimes are the signed binaries shipped in the releases above.

- FSR upscaler API (context creation flags, dispatch description, version query and override, HDR,
  inverted depth, jitter and motion vector conventions), shipped as
  `Kits/FidelityFX/docs/techniques/super-resolution-upscaler.md`:
  https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/v2.3.0/Kits/FidelityFX/docs/techniques/super-resolution-upscaler.md
- AMD's rule for the FSR version selection: only version IDs the runtime reports, never hard-coded,
  and the choice left to the default or to the player in the options UI ("Version selection" in the
  FSR API document above, `Kits/FidelityFX/docs/getting-started/ffx-api.md`). TG Better Upscaling leaves it
  to the default and logs which version that is.
- FSR's jitter sequence (Halton(2,3), `8 * (display / render width)^2` phases) and XeSS's minimum
  sequence length (72 for Ultra Performance), from the two documents above and the XeSS SR developer
  guide's "Jitter Sequence" section.
- Where the game's DLSS DLL comes from: NVIDIA's runtime (`_nvngx.dll`, in the driver) loads
  `<application folder>/nvngx_dlss.dll` with `LoadLibraryExW` by full path while the application
  initializes NGX (`NVSDK_NGX_D3D11_Init_with_ProjectID`), and only when that file exists; observed
  on an RTX 5080 on 2026-09-23 with the NGX SDK library from Streamline 2.14.1. Given the file from
  the DLSS SDK instead, NGX reports DLSS available and creates the feature from it.
- DLSS quality levels and models: `NVSDK_NGX_PerfQuality_Value` (MaxPerf 0, Balanced 1, MaxQuality 2,
  UltraPerformance 3, UltraQuality 4, DLAA 5) and the default model per level (K for DLAA, Quality
  and Balanced, M for Performance, L for Ultra Performance) in `nvsdk_ngx_defs.h` of the NGX SDK in
  Streamline 2.14.1; Unity's `GraphicsDevice.GetOptimalSettings` and `DLSSQuality` in the game's
  `UnityEngine.NVIDIAModule` (6000.0.64).
- FSR quality modes and their ratios: `FfxApiUpscaleQualityMode` and
  `ffxQueryDescUpscaleGetUpscaleRatioFromQualityMode` in `ffx_upscale.h`; null-context queries with a
  chained `ffxCreateBackendDX12Desc` in `Kits/FidelityFX/docs/getting-started/ffx-api.md` (FidelityFX
  SDK 2.3.0).
- DLSS sharpening: `NVSDK_NGX_DLSS_Feature_Flags_DoSharpening` and `sl::DLSSOptions::sharpness` are
  marked `[[deprecated("Sharpness is not supported")]]` in the NGX and Streamline 2.14.1 headers.
- Intel XeSS Super Resolution developer guide (quality presets and their input-resolution ranges,
  `XESS_INIT_FLAG_INVERTED_DEPTH`, jitter in input pixels, motion vector scale, exposure), shipped as
  `doc/xess_sr_developer_guide_english.md` in the XeSS SDK v3.0.2 release:
  https://github.com/intel/xess/releases/tag/v3.0.2
- Unity HDRP 17 (the game's HDRP): `HDRenderPipeline.SetupPartnerUpscalers` (which upscaler a camera
  gets), `HDRenderPipeline.DoStpPasses` and `PopulateStpConfig` (STP's inputs: color, depth, motion,
  jitter, near/far from `projectionParams`, `finalViewport` output, `resetPostProcessingHistory`),
  and `CustomPass.ExecuteInternal`/`RenderTargets` (recording a custom pass on given targets). Read
  from the game's `Unity.RenderPipelines.HighDefinition.Runtime.dll`; the package source is
  https://github.com/Unity-Technologies/Graphics/tree/master/Packages/com.unity.render-pipelines.high-definition
- The game's `Awaken.TG.Main.Settings.Graphics.UpScaling` (the Upscaling row, its preference key
  `UpScalingType` and quality divisors) and `UpScalingController` (camera setup per upscaler), read
  from the game's `TG.Main.dll`.
