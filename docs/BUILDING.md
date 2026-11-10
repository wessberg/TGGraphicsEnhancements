# Building

The game renders through Direct3D 11. The mods use HDRP's own depth and motion vectors and share a Direct3D 12
device through TG Graphics Bridge. [ARCHITECTURE.md](ARCHITECTURE.md) describes the rendering paths.

Path Tracing is paused and its managed Unity 6000.3 build is blocked. Build the maintained mods with
`-Skip TGPathTracing`.

Use Windows x64 with the Visual Studio C++ build tools, CMake 3.24 or newer, Windows SDK 10.0.22621 or
newer, and a .NET SDK that builds `net472` and `net6.0`. Place the SDK folders in the repository root. Git ignores them:

| Folder | Contents |
|---|---|
| `Streamline-2.14.16` | NVIDIA Streamline 2.14.16 source: headers and `external/ngx-sdk/include` |
| `Streamline-2.14.16-bin` | `bin/x64` and `license.txt` from the `streamline-sdk-v2.14.16.zip` release |
| `FidelityFX-SDK-2.4.0` | AMD FSR SDK 2.4.0 headers and, in `bin/`, the signed `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll` and `amd_fidelityfx_upscaler_dx12.dll` |
| `XeSS-SDK-3.0.2` | Intel XeSS SDK 3.0.2 release archive |
| `AntiLag2-SDK-2.0.0a` | AMD Anti-Lag 2 SDK 2.0.0a release archive |
| `DLSS-SDK-310.9.16` | NGX headers, static library, signed DLSS runtimes and `LICENSE.txt` |
| `RTXDI-Library-3.1` | NVIDIA's RTXDI runtime library 3.1 (`git clone https://github.com/NVIDIA-RTX/RTXDI-Library`), compiled into TG Path Tracing |
| `NVAPI-R615` | NVAPI header and `amd64/nvapi64.lib` for PT's Mega Geometry integration; see [PT build status](PT-STATE.md) for the configuration gap |

CMake fetches MinHook v1.3.4 unless `-MinHookRoot` is given. The game needs the matching BepInEx
already installed. [SOURCES.md](SOURCES.md) links the SDK releases.

```powershell
# Build native DLLs in a VS x64 developer shell; this command also runs GPU tests.
.\Build-Native.ps1 -StreamlineRoot Streamline-2.14.16 `
  -NgxIncludeDir Streamline-2.14.16\external\ngx-sdk\include

# Signed NVIDIA, AMD and Intel runtimes with their license notices, into each mod's folder
.\Stage-Runtime.ps1 -RuntimeDirectory Streamline-2.14.16-bin\bin\x64 `
  -FidelityFXRoot FidelityFX-SDK-2.4.0 -XeSSRoot XeSS-SDK-3.0.2 -DlssRoot DLSS-SDK-310.9.16

# Plugins and preloaders, then package and install the mods (game closed)
.\Build.ps1 -Runtime Mono -Skip TGPathTracing `
  -GameDirMono 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' -Deploy
```

For a normal-version IL2CPP build, use `-Runtime IL2CPP -GameDirIl2Cpp <game folder>`. BepInEx 6 has to have
started the game once, so that its interop assemblies exist.

Use `-Mod` with `TGFrameGeneration`, `TGBetterUpscaling` or `TGNeuralRendering` to limit packaging and deployment
to one mod. Deployment refuses to mix different bridge builds under the same version.

PT's native shaders still belong to the CMake build, even with its managed build skipped.
See [BUILD-DEPENDENCIES.md](BUILD-DEPENDENCIES.md) for NRD, DXC and Agility requirements.

`-Deploy` backs up replaced files under `BepInEx/<mod>/Backups`. It replaces Vortex links without writing into
Vortex's staging folder, and removes the obsolete 1.0.x preloader (`BepInEx/patchers/TGFG.Autoload.dll`).
Packaging writes the Nexus Mods downloads to `artifacts/Nexus`, one per mod and game version:
`<mod> - mono.zip` for the Mono branch and `<mod>.zip` for the standard version. Each contains the
mod's BepInEx payload, `<mod>-Package-manifest.json` and `vortex_override_instructions.json`, which
tells Vortex to install the archive at the game root under the mod type it calls "Engine Injector".

`Test-Setup.ps1 -GameDir <game folder> -Runtime Mono` (or `IL2CPP`) checks an installed copy without
starting the game. Native builds run the portable ABI, policy, interop, camera-orientation and
upscaling tests, compile the HLSL and run the GPU smoke test; `tests/source_checks.py`,
`tests/test_autoload_integration.py` and `tests/package_tests.ps1` check sources and packaging, and
`tests/InstallCheck.Tests.csproj` runs the install check on folders laid out the ways players unpack the mods.
[TESTING.md](TESTING.md) has the in-game test sequence.


## Before running GPU tests

`Build-Native.ps1` builds, stages and runs native tests, including GPU tests. Schedule it when the GPU is available.
Use the CPU source checks for documentation changes:

```powershell
python tests/source_checks.py
```

Do not start the game or GPU tests alongside another open game. The test runner and timing rules are in
[PT-HARNESS.md](PT-HARNESS.md) and [TESTING.md](TESTING.md).
