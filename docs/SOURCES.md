# Primary references

TG Neural Rendering (2026-10-07): public DLSS 310.9.1 NGX API, supplied signed NR 310.8.0.0 profile, hashes and
measured calls in [DLSS5-IMPLEMENTATION.md](DLSS5-IMPLEMENTATION.md). NVIDIA's
[sample NR integration](https://github.com/nvpro-samples/vk_gltf_renderer/blob/master/src/dlss.cpp) documents the
tone-mapped display input, SR/RR guides, reset and tuning concepts; its newer helper is not the build dependency.
Unity's HDRP 17 [CustomPass](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/RenderPass/CustomPass/CustomPass.cs)
declares camera resources read-write for render-graph lifetime even with no automatic target binding.
[RenderGraph ordering](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.RenderGraph.cs)
and [post-processing](https://github.com/Unity-Technologies/Graphics/blob/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/HDRenderPipeline.PostProcess.cs)
establish the chosen hook after tone mapping. Model controls beyond Strength remain unvalidated and unexposed.

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

## TG Path Tracing references (ReSTIR, Ray Reconstruction, Ray Regeneration)

Reviewed 2026-09-26.

- NVIDIA RTXDI runtime library 3.1 ("3.1 Update", commit f12037fa8e97ebc08e9e3edfd2de528ed1772a4b,
  2026-09-03), fetched into the gitignored `RTXDI-Library-3.1/` and compiled into
  TGPathTracing.Native.dll, never committed: https://github.com/NVIDIA-RTX/RTXDI-Library. NVIDIA RTX
  SDKs License (LICENSE.txt): distributable "as incorporated in object code format into a software
  application", no source redistribution, and this notice:
  This software contains source code provided by NVIDIA Corporation.
  Integration guide and bridge (RAB_*) reference:
  https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md
- NVIDIA NVAPI R615 (https://github.com/NVIDIA/nvapi, commit 70d337d as fetched 2026-10-09 by the worker
  `megageo1`; MIT license, `License.txt` in the folder), in the gitignored `NVAPI-R615/`, never committed. Its
  header and `nvapi64.lib` are used only by a build with CMake's `TGPT_MEGA_GEOMETRY` ON (TG Path Tracing's RTX
  Mega Geometry path: the capability queries and the partitioned top-level structure; PT-109). NVIDIA's RTX Mega
  Geometry SDK (https://github.com/NVIDIA-RTX/RTXMG; NVIDIA RTX SDKs License, object code only) was READ for that
  work and nothing of it is copied; the nvpro samples' READMEs (`vk_animated_clusters`: Apache-2.0;
  `vk_partitioned_tlas`: license not verified) were read only. Added 2026-10-10, from the worker's report
  (`docs/agents/megageo-1.md`); `License.txt` read again by the coordinator (MIT for the libraries); the commit
  number is the worker's.
- meshoptimizer v1.3 (https://github.com/zeux/meshoptimizer/tree/v1.3, MIT license, `LICENSE.md`): fetched by CMake's
  FetchContent (GIT_TAG v1.3) only in a build with `TGPT_MEGA_GEOMETRY` ON, never committed or copied by hand. Its
  `meshopt_buildMeshletsSpatial` and `meshopt_optimizeMeshlet` cut the small plants' meshes into clusters for the RTX
  Mega Geometry trial (PT-109, `docs/agents/megageo-4.md`): the clusterizer NVIDIA's own samples use
  (nvpro-samples `vk_animated_clusters`, `vk_lod_clusters`; read, nothing copied: `docs/agents/megageo-3.md`). Its notice
  is installed beside the DLL. Added 2026-10-11 by the worker `megageo-4`.
- NVIDIA NRD (Real-Time Denoisers) 4.17.3 (tag v4.17.3), fetched into the gitignored `NRD-SDK-4.17.3/` and compiled
  into TGPathTracing.Native.dll as a static library with its shaders embedded as DXIL (RELAX's diffuse denoiser for
  the bounced light: the Denoising choice NRD), never committed: https://github.com/NVIDIA-RTX/NRD. Its CMake downloads
  ShaderMake, MathLib and DXC when it configures (MathLib and ShaderMake's blob reader are linked in: MIT, their notices
  ship with the DLL). NVIDIA RTX SDKs License (LICENSE.txt) as for RTXDI above: object code only, no source
  redistribution, the same notice (packaged as `Native/Notices/LICENSE-NVIDIA-RTX-SDKs.txt`); its limit to systems with
  NVIDIA GPUs covers DLSS and NGX, not NRD. CMakeLists.txt refuses another release than 4.17.
  Inputs and settings: the repository's README.md (INPUTS) and Include/NRDSettings.h. DOOM: The Dark Ages falls back
  to "Nvidia's real-time denoiser" when Ray Reconstruction is off:
  https://www.xda-developers.com/nvidia-doom-dark-ages-ray-reconstruction-path-tracing/
- DLSS-RR Integration Guide (DLSS SDK 310.9.1, `doc/DLSS-RR Integration Guide.pdf`): section 3.5
  (independent samples; ReSTIR recommended with randomized temporal reuse and permutation sampling),
  3.7 (exposure, auto exposure and sharpening not supported).
- AMD FSR Ray Regeneration 1.2.0 (`Kits/FidelityFX/docs/techniques/denoising.md`, FidelityFX SDK
  2.3.0): section 5.1.2 (white noise; ReSTIR's cross-sample dependencies need permutation sampling or
  randomized temporal reuse).
- RTX Remix (dxvk-remix) presets for Ray Reconstruction, `rtx_options.cpp` `updatePathTracerPreset`:
  https://github.com/NVIDIAGameWorks/dxvk-remix
- Wyman et al., "A Gentle Introduction to ReSTIR" (SIGGRAPH 2023 course notes):
  https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf; Wyman and Panteleev,
  "Rearchitecting Spatiotemporal Resampling for Production" (HPG 2021).
- Chiang, Li and Burley, "Taming the Shadow Terminator" (SIGGRAPH 2019 talk):
  https://media.disneyanimation.com/technology/publications/2019/TamingtheShadowTerminator.pdf
- HDRP 17's `DLSSPass` and `HDRenderPipeline` (pre-exposure `Mathf.Clamp(hdCamera.GpuExposureValue(),
  0.2f, 2f)` handed to DLSS), read from the game's `Unity.RenderPipelines.HighDefinition.Runtime.dll`.
- Xueqing Yang and Yaobin Ouyang, "Real-Time Ray Traced Caustics", Ray Tracing Gems II, chapter 30 (2021), its water
  caustics: rays from the light onto the water, refracted and traced on, the pattern rebuilt as a mesh whose cells' light
  is their area at the start over their area where they land ("procedural caustic mesh"), in cascades for large water.
  TG Path Tracing's caustics (pt_caustics.hlsl, PT-14) take that mesh and its area ratio, and differ: the grid lies on the
  water's own surface (camera-centred levels, each twice as coarse, moved by a random fraction of a cell every frame), point
  lights as well as the sun, reflection as well as refraction, and the landed triangles go into a world-space hash any
  surface point asks, instead of being drawn into the screen. Summary: `docs/CAUSTICS-RESEARCH.md` section B.
- Van Oosterom and Strackee, "The Solid Angle of a Plane Triangle", IEEE Transactions on Biomedical Engineering 30 (2),
  1983: the solid angle a water triangle fills seen from a lamp (pt_caustics.hlsl solid_angle).
- Unity's Graphics repository, HDRP 17 (6000.0 branch), read for how HDRP's water moves its surface, nothing copied:
  `Runtime/Water/Shaders/SampleWaterSurface.hlsl` (each band's UV as the surface's x and z times its scale less its offset,
  its displacement times its amplitude and distance fade, the horizontal move times the choppiness 2.25, the surface
  gradient read bicubic), `Runtime/Water/Shaders/WaterSimulation.compute` (EvaluateNormalsJacobian: the gradient from the
  displaced texels, amplitude in, choppiness out), `HDRenderPipeline.WaterSystem.cs` (UpdatePerSurfaceConstantBuffer's
  band data): https://github.com/Unity-Technologies/Graphics/tree/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Water
