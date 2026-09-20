# TG Frame Generation

Native frame generation for Tainted Grail: The Fall of Avalon with NVIDIA DLSS, AMD FSR or Intel
XeSS, plus NVIDIA Reflex, Intel XeLL or AMD Anti-Lag 2 for lower input latency. The settings are
rows in the game's own Video tab.

Native means the frame generation works from the game's own motion vectors and depth, the same
inputs a game with built-in frame generation hands to these SDKs. Driver-level frame generation,
such as AMD Fluid Motion Frames or NVIDIA Smooth Motion, only sees finished frames and estimates
the motion from them. TGFG calls each vendor's SDK directly, with no OptiScaler or other wrapper
in between.

Frame generation uses the scene captured before the overlay HUD is drawn, keeping the HUD
out of interpolation with DLSS, FSR and XeSS. The HUD and first-person hand flicker reported
with FSR is resolved in the tested Mono setups, including ReShade, RenoDX HDR and RenoDX
DLSS 5, with and without upscaling.

The game renders with Direct3D 11. TGFG copies each finished frame to a private Direct3D 12 device
and generates frames there with NVIDIA Streamline for DLSS Frame Generation, AMD's FSR API
(FidelityFX SDK 2.3) or Intel XeSS-FG (XeSS SDK 3.0).

## Status

Version 0.5.0 (native ABI v5), for both versions of the game: the standard version (IL2CPP) with
BepInEx 6 and the Mono modding branch with BepInEx 5.

Tested in game on an RTX 5080 with the Mono build:

- DLSS Frame Generation, fixed 2x to 6x and Dynamic Multi Frame Generation, with and without
  ReShade + RenoDX HDR, and together with the RenoDX DLSS addon ("DLSS 5").
- FSR Frame Generation with NVIDIA Reflex, including stable HUD and first-person hands
  during camera movement, and switching technology while playing.
- XeSS Frame Generation with Intel XeLL, with verified HUD-less input.
- DLSS, FSR and XeSS Frame Generation with DLSS upscaling off, on HDRP's own motion vectors.
- The HUD fix with ReShade and RenoDX DLSS 5 enabled, with and without upscaling and with
  the other upscalers tested by the reporter. Logs confirm HUD-less input reaches all three FG backends.

The standard IL2CPP build also has a successful reporter retest after fixing a crash when
opening Pause → Options with upscaling active. The broader compatibility coverage above
was tested on Mono. AMD Anti-Lag 2 (AMD GPUs) and Intel XeLL on its own (Intel GPUs) have
not yet been confirmed in game.

When DLSS upscaling is on, frame generation takes its motion vectors and depth from the DLSS
upscaler. With any other setting (TAA, STP or no anti-aliasing), a custom pass takes HDRP's own depth
and motion vectors instead, so FSR and XeSS frame generation also work on AMD and Intel GPUs. The
HDRP path gives the SDKs the same jitter and motion vector scale HDRP gives DLSS.

## Settings

| Video tab row | Choices |
|---|---|
| Frame Generation | Off, plus each of DLSS, FSR and XeSS whose runtime reports support on this GPU. |
| Frame Generation Mode | DLSS only: Fixed or Dynamic, when the runtime reports Multi Frame Generation and Dynamic support. |
| Frame Multiplier | 2x up to the selected technology's maximum. Switching technology clamps the value. |
| Max Frame Multiplier, Target Framerate | DLSS Dynamic only: the upper limit and the framerate to aim for. |
| Low Latency | Off, NVIDIA Reflex, NVIDIA Reflex + Boost, Intel XeLL, AMD Anti-Lag 2, as supported. DLSS needs Reflex and XeSS always runs with XeLL, which greys the row out. FSR requires none, so Off stays available with it. |

The maxima come from the runtimes: DLSS-G's `numFramesToGenerateMax` (up to 6x on RTX 50) and
XeSS's `maxSupportedInterpolations` (up to 4x on Intel Arc, 2x elsewhere). The FSR API has no
multi-frame control, so FSR is 2x, and neither FSR nor XeSS has a dynamic mode. Intel XeLL works on
its own only on Intel GPUs; elsewhere only together with XeSS Frame Generation.

The rows use the game's Apply Changes, Undo and Restore Defaults. Changing technology recreates only
TGFG's private presentation swapchain between two frames. Frame generation pauses in menus, while
the cursor is unlocked and while the game is paused.

Values are stored in `BepInEx/config/com.wessberg.tgframegeneration.cfg`. FoA Mod Manager lists
the same six rows with the same choices and rules, and a change in either place shows up in the
other. Avalon Mod Manager's MODS tab leaves TGFG out: on Apply it writes back the values it read
when the Options screen opened, which would undo what the Video tab just applied. The `[Advanced]`
section of the config file holds options without a row: a base FPS cap through the active
low-latency technology, the emergency off key (F8), the pause rules and a diagnostic report switch.
Settings from 0.4.x and earlier builds of 0.5.0 are migrated on first start.

## How it works

```text
FoA + ReShade/RenoDX, rendering through D3D11
  ├─ the game's DLSS upscaler calls pass through unchanged
  ├─ frame generation inputs (depth, motion vectors): copied at the D3D11 NGX evaluation when
  │  DLSS runs, otherwise from HDRP's buffers by a custom pass before post-processing
  └─ the final image is taken at the D3D11 Present
       ↓ shared textures and GPU fences on the same adapter
TGFG's private D3D12 device and queue
       ↓ depth, motion vectors, camera, jitter and motion vector scale
Presentation swapchain of the selected technology
  ├─ Off / DLSS: NVIDIA Streamline (DLSS-G, Reflex)
  ├─ FSR: AMD's frame generation swapchain
  └─ XeSS: Intel's XeSS-FG swapchain (with XeLL)
       ↓ rendered and generated frames
Display

Steam → BepInEx preloader (TGFG.Autoload) → native bootstrap
BepInEx plugin → Video rows, camera and frame events, diagnostics
```

Streamline is loaded only when the GPU supports DLSS-G or Reflex. Other GPUs get a plain D3D12
device, and a Streamline failure doesn't stop FSR or XeSS. The Streamline interposer ships as
`TGFG.Streamline.dll`, so the RenoDX DLSS addon, which hooks `sl.interposer.dll` by name, leaves
TGFG's copy alone.

| Area | Implementation and limits |
|---|---|
| Managed | `net472` for Mono, `net6.0` for IL2CPP. One native x64 backend serves both. |
| DLSS | Streamline 2.14.1 DLSS-G: fixed multipliers, `eDynamic` with `dynamicTargetFrameRate`, Reflex required. |
| FSR | AMD's signed loader and frame generation DLLs: FSR 4 ML frame generation on Radeon RX 9000, FSR 3.1 elsewhere. |
| XeSS | Intel's signed `libxess_fg.dll` and `libxell.dll`; inputs tagged per frame, present IDs are XeLL frame IDs. |
| Inputs | The DLSS upscaler's depth and motion vectors with its jitter, motion vector scale and feature flags. Without DLSS, HDRP's own buffers with HDRP's TAA jitter and the scale HDRP gives DLSS. Both are copied to shareable R32_FLOAT and R16G16_FLOAT textures. |
| Synchronization | GPU fences between the D3D11 and D3D12 queues. The CPU never reads an image back. |
| HDR | FP16 scRGB is encoded to RGB10A2 PQ/BT.2020; SDR and HDR10 are copied. FSR and XeSS accept HDR10. |
| HUD | Scene color is captured after HDRP post-processing and before overlay UI. DLSS-G, FSR and XeSS receive that HUD-less image after pixel verification. For the supported RenoDX Unity output shader, the same live color conversion is applied to the capture so HDR processing preserves HUD separation. |
| Loading | BepInEx preloader. No launcher, and no replaced `winhttp.dll`, `dxgi.dll` or `version.dll`. |

## Building

Windows x64 with the Visual Studio C++ build tools, CMake 3.24 or newer, Windows SDK 10.0.22621 or
newer, and a .NET SDK that builds `net472` and `net6.0`. The SDKs go next to this repository (they
are git-ignored):

| Folder | Contents |
|---|---|
| `Streamline-2.14.1` | NVIDIA Streamline 2.14.1 source: headers and `external/ngx-sdk/include` |
| `Streamline-2.14.1-bin` | `bin/x64` and `license.txt` from the `streamline-sdk-v2.14.1.zip` release |
| `FidelityFX-SDK-2.3.0` | AMD FSR SDK 2.3.0 headers (`Kits/FidelityFX/...`) and, in `bin/`, the signed `amd_fidelityfx_loader_dx12.dll` and `amd_fidelityfx_framegeneration_dx12.dll` from the prebuilt package |
| `XeSS-SDK-3.0.2` | Intel XeSS SDK 3.0.2 release archive |
| `AntiLag2-SDK-2.0.0a` | AMD Anti-Lag 2 SDK 2.0.0a release archive |

CMake fetches MinHook v1.3.4 unless `-MinHookRoot` is given. The game needs the matching BepInEx
already installed. `docs/SOURCES.md` links every SDK release.

```powershell
# Native backend (VS x64 developer shell)
.\Build-Native.ps1 -StreamlineRoot Streamline-2.14.1 `
  -NgxIncludeDir Streamline-2.14.1\external\ngx-sdk\include

# Signed NVIDIA, AMD and Intel runtimes with their license notices
.\Stage-Runtime.ps1 -RuntimeDirectory Streamline-2.14.1-bin\bin\x64 `
  -FidelityFXRoot FidelityFX-SDK-2.3.0 -XeSSRoot XeSS-SDK-3.0.2

# Plugin and preloader, then package and install (game closed)
.\Build.ps1 -Runtime Mono `
  -GameDirMono 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' -Deploy
```

For the standard version, use `-Runtime IL2CPP -GameDirIl2Cpp <game folder>`. BepInEx 6 has to have
started the game once, so that its interop assemblies exist.

`Build-Native.ps1` defaults the FSR, XeSS and Anti-Lag 2 roots to the folders above. `-Deploy`
backs up every TGFG file it replaces under `BepInEx/TGFrameGeneration/Backups`.
`Build.ps1 -Runtime Both -Package` builds player ZIPs for both runtimes without installing, and
`Package.ps1 -Runtime Mono -Deploy` repackages existing artifacts. Every packaging run also writes the
Nexus Mods downloads to `artifacts/Nexus`: `TGFrameGeneration - mono.zip` for the Mono branch and
`TGFrameGeneration.zip` for the standard version. A package contains the plugin,
the preloader, the native backend, the vendor runtimes with their license notices and
`TGFG-Package-manifest.json`. Install one runtime package per game installation, then start the game
from Steam as usual.

`Test-Setup.ps1 -GameDir <game folder> -Runtime Mono` (or `IL2CPP`) checks an installed copy without starting
the game. Native builds also run the portable ABI, policy and interop tests and compile the HLSL;
`tests/source_checks.py`, `tests/test_autoload_integration.py` and `tests/package_tests.ps1` check
sources and packaging. `docs/TESTING.md` has the in-game test sequence.

## Diagnostics

```text
BepInEx/TGFrameGeneration/Autoload.log
BepInEx/TGFrameGeneration/Native/TGFG.Native.log
BepInEx/TGFrameGeneration/diagnostics/
```

At startup the native log lists what each runtime reports: DLSS-G, the FSR providers, the XeSS
maximum, XeLL and Anti-Lag 2. Every five seconds a `Pipeline 5s:` line counts frame events,
captured DLSS inputs, HDRP input packets, presents with inputs and frames with generation on, and
names the active
output (`standard`, `FSR`, `XeSS`) and latency technology, with Reflex's latency report or the FSR
and XeSS frame counts. Technology switches are logged as "Presentation switching from ... to ...".

To stop the native backend from loading, close the game and create an empty
`BepInEx/TGFrameGeneration/disable-autoload.txt`. Setting Frame Generation to Off keeps the bridge
running.

## Known limits

- The HDRP input path needs HDRP to render motion vectors for the gameplay camera, which it does
  with TAA, STP and DLSS; without them the log says so and frame generation stays off.
- The HUD-less frame relies on the HUD being screen-space overlay UI (or drawn by a later camera).
  A canvas rendered by the gameplay camera stays part of the interpolated image. The tested
  ReShade/RenoDX setup preserves HUD separation. Other effects that change most of the image
  after capture can still cause verification to fail; TGFG then withholds the HUD-less input
  and reports this in the native log. RenoDX shader recognition covers the validated revision.
- FSR and XeSS create their swapchain while the game presents. ReShade/RenoDX has been tested;
  other overlays and capture tools may need separate compatibility testing.
- One window, one adapter and one stable D3D11 back buffer. No stereo, MSAA swapchain, GDI
  composition, dirty-rect or scroll Present1, non-blocking Present, several presenting windows,
  device-loss recovery or unloading the native DLL while the game runs.
- HDR10 output is 10-bit PQ and clips outside its range and gamut. There is no HDR-to-SDR tone
  mapper.
- The private swapchain and the frame copy stay active with frame generation Off.
- A game that waits on the swapchain's frame-latency object gets a semaphore owned by the bridge,
  and the private swapchains pace through a blocking Present. Pacing with such a game is
  unverified.
- The gameplay camera choice, HDRP's camera-relative matrices and the plugin-event order were
  validated with DLSS-G.

`docs/ARCHITECTURE.md`, `docs/TESTING.md`, `docs/SOURCES.md` and `docs/AUTOLOAD-INTEGRATION.md`
cover the implementation, the test plan and the primary sources.
`docs/FSR-FLICKER-INVESTIGATION.md` records the resolved HUD/hand flicker investigation,
including confirmed ReShade/RenoDX results and runtime evidence. Read it before changing the FSR path.

## License

TGFG's source is MIT-licensed. The NVIDIA, AMD and Intel runtimes in a package keep their own
licenses, which ship next to them; see `LICENSES/THIRD_PARTY.md`.
