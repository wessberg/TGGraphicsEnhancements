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
