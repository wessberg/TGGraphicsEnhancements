# External components and redistribution

The source of TG Graphics Bridge, TG Frame Generation and TG Better Upscaling is provided under the
included MIT license. This does
not grant rights to game assemblies, NVIDIA proprietary SDK headers, NVIDIA
runtime binaries, or other mods.

MinHook is fetched separately at v1.3.4 (or supplied locally). Review its license
in that checkout and include the applicable notices when distributing a binary
that incorporates it. No MinHook source or binary is bundled here.

NVIDIA Streamline SDK 2.14.1: this repository contains no NVIDIA headers or binaries. Player
packages contain the signed production runtime from the SDK release, unmodified except that
`sl.interposer.dll` is renamed `TGFG.Streamline.dll`: `sl.common.dll`, `sl.dlss_g.dll`,
`sl.reflex.dll`, `sl.pcl.dll` and `nvngx_dlssg.dll`. Stage-Runtime.ps1 places the release's
license texts next to them: Streamline's MIT license as `LICENSE-Streamline.txt`, the NVIDIA RTX
SDKs license (DLSS, including DLSS Frame Generation) as `LICENSE-NVIDIA-RTX-SDKs.txt` and the
Reflex license as `LICENSE-NVIDIA-Reflex.txt`. Anyone publishing a package should read those
terms for their own release; the RTX SDKs license, for example, allows distribution as part of an
application with material functionality beyond the SDK and not as a stand-alone product.

NVIDIA DLSS SDK 310.9.1: TG Better Upscaling packages carry the signed `nvngx_dlss.dll` from the SDK
release, unmodified, as `Native/DLSS/nvngx_dlss.dll`, with the release's NVIDIA RTX SDKs license next
to it as `Native/DLSS/LICENSE-NVIDIA-RTX-SDKs.txt`. The terms above apply; the license's trademark
exhibit (7.1(b)) also asks applications using the DLSS SDK to credit it.

AMD FSR (FidelityFX SDK v2.3.0): TG Frame Generation and TG Better Upscaling compile against the SDK's
MIT-licensed API headers and load AMD's signed `amd_fidelityfx_loader_dx12.dll` with
`amd_fidelityfx_framegeneration_dx12.dll` (frame generation) or `amd_fidelityfx_upscaler_dx12.dll`
(upscaling) at run time. Those binaries are redistributable in binary form under AMD's license,
which requires its notice to accompany them and forbids reverse engineering; each package places
that notice next to them as `Native/FidelityFX/LICENSE-FidelityFX.md`.

Intel XeSS SDK v3.0.2: `libxess_fg.dll` and `libxell.dll` (frame generation) and `libxess.dll`
(upscaling) are loaded at run time and redistributed unmodified under the Intel Simplified Software
License, reproduced next to them as `Native/XeSS/LICENSE-XeSS.txt`. The SDK headers are used for
compilation only.

NVAPI: `native/upscaling/src/nvidia_profile.cpp` declares the few NVAPI structures, interface IDs and
setting IDs it uses after NVIDIA's `nvapi.h`, `nvapi_interface.h` and `NvApiDriverSettings.h`
(github.com/NVIDIA/nvapi, MIT license); `nvapi64.dll` is the driver's own and not redistributed.

NVIDIA RTXDI 3.1 and NVIDIA NRD 4.17.3 (Real-Time Denoisers): TG Path Tracing compiles both into
`TGPathTracing.Native.dll` (NRD with its shaders embedded) under the NVIDIA RTX SDKs license, which allows them in
object code form as part of an application and not their source: this repository contains none of it (the SDKs are
fetched beside it, see docs/SOURCES.md). The package places that license next to the DLL as
`Native/Notices/LICENSE-NVIDIA-RTX-SDKs.txt`. NRD links NVIDIA's MathLib and ShaderMake's blob reader, both under the
MIT license, whose notices go there too as `Native/Notices/LICENSE-MathLib.txt` and `LICENSE-ShaderMake.txt` (a
build without NRD has neither). CMakeLists.txt installs all three with the DLL.

AMD Anti-Lag 2 SDK v2.0.0a is a header compiled into TGFG.Native.dll under the MIT license;
include its notice with any distributed binary. MinHook is linked into TGGraphicsBridge.Native.dll.

The SDKs themselves are not part of this repository; the build scripts expect them next to it
(`FidelityFX-SDK-2.3.0`, `XeSS-SDK-3.0.2`, `AntiLag2-SDK-2.0.0a`, `DLSS-SDK-310.9.1`, `RTXDI-Library-3.1`,
`NRD-SDK-4.17.3`, see docs/SOURCES.md).

BepInEx, Il2CppInterop, Unity, TextMeshPro, and game assemblies are referenced from
the user's installation and are not bundled. Do not upload proprietary game
assemblies with a public mod release.
