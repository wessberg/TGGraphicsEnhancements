# External components and redistribution

TGFrameGeneration source is provided under the included MIT license. This does
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

AMD FSR (FidelityFX SDK v2.3.0): TGFG compiles against the SDK's MIT-licensed API headers and
loads AMD's signed `amd_fidelityfx_loader_dx12.dll` and `amd_fidelityfx_framegeneration_dx12.dll`
at run time. Those binaries are redistributable in binary form under AMD's license, which
requires its notice to accompany them and forbids reverse engineering; packages place that
notice next to them as `Native/FidelityFX/LICENSE-FidelityFX.md`.

Intel XeSS SDK v3.0.2: `libxess_fg.dll` and `libxell.dll` are loaded at run time and
redistributed unmodified under the Intel Simplified Software License, reproduced next to them as
`Native/XeSS/LICENSE-XeSS.txt`. The SDK headers are used for compilation only.

AMD Anti-Lag 2 SDK v2.0.0a is a header compiled into TGFG.Native.dll under the MIT license;
include its notice (and MinHook's) with any distributed binary.

The SDKs themselves are not part of this repository; the build scripts expect them next to it
(`FidelityFX-SDK-2.3.0`, `XeSS-SDK-3.0.2`, `AntiLag2-SDK-2.0.0a`, see docs/SOURCES.md).

BepInEx, Il2CppInterop, Unity, TextMeshPro, and game assemblies are referenced from
the user's installation and are not bundled. Do not upload proprietary game
assemblies with a public mod release.
