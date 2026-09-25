# TG Graphics Bridge, TG Frame Generation and TG Better Upscaling

Two mods for Tainted Grail: The Fall of Avalon, built from this repository, and the part they share:

| | What it does |
|---|---|
| **TG Frame Generation** | Frame generation with NVIDIA DLSS, AMD FSR or Intel XeSS, plus NVIDIA Reflex, Intel XeLL or AMD Anti-Lag 2. |
| **TG Better Upscaling** | AMD FSR (FSR 4 on Radeon RX 9000, FSR 3.1 elsewhere) and Intel XeSS upscaling, as two more choices of the game's own Upscaling row next to STP and DLSS, and a newer DLSS DLL for the game's own DLSS. |
| **TG Graphics Bridge** | Bundled with each mod, not a download of its own. Hooks the game's Direct3D 11 device and swapchain from a BepInEx preloader, creates one private Direct3D 12 device on the same GPU, and loads the native part of every mod built on it. Also carries what both mods share in game: the Video-tab plumbing, the mod-manager integration and two fixes to the game's motion vectors. With both mods installed, one copy runs. |

Both mods are native: they work from the game's own motion vectors and depth, the inputs a game
with built-in support hands to these SDKs, and call each vendor's SDK directly, with no OptiScaler
or other wrapper in between. Their settings are rows in the game's own Video tab.

The game renders with Direct3D 11. The bridge shares the game's textures with its Direct3D 12
device through GPU fences on the same adapter; frame generation presents there with NVIDIA
Streamline, AMD's FSR API (FidelityFX SDK 2.3) or Intel XeSS-FG (XeSS SDK 3.0), and upscaling runs
FSR or XeSS Super Resolution there and hands the result back to the game's frame before
post-processing.

## Status

Version 1.1.0 of both mods and the bridge, the numbers they have on Nexus Mods (bridge, frame
generation and upscaling ABI 0x00010100), for both
versions of the game: the standard version (IL2CPP) with BepInEx 6 and the Mono modding branch with
BepInEx 5.

TG Frame Generation's behaviour is unchanged from 0.5.0, which was tested in game on an RTX 5080
with the Mono build:

- DLSS Frame Generation, fixed 2x to 6x and Dynamic Multi Frame Generation, with and without
  ReShade + RenoDX HDR, and together with the RenoDX DLSS addon ("DLSS 5").
- FSR Frame Generation with NVIDIA Reflex, including stable HUD and first-person hands during
  camera movement, and switching technology while playing.
- XeSS Frame Generation with Intel XeLL, with verified HUD-less input.
- DLSS, FSR and XeSS Frame Generation with DLSS upscaling off, on HDRP's own depth and motion vectors.
- Borderless and Exclusive Fullscreen, cold started and switched between while playing.

What is new in 1.1.0 was tested in game on the same RTX 5080 with the Mono build: the mods
loading together, the bridge creating its device with frame generation as the presentation
provider, and TG Better Upscaling's FSR 3.1.5 and XeSS 2.0.2 at 33% to 40% render scale, with ReShade +
RenoDX HDR. With the scene frozen, FSR and XeSS were measured against the game's STP and DLSS and a
supersampled reference image: all four clearly beat a plain enlargement of the render resolution,
and the jitter FSR and XeSS receive was checked against AMD's and Intel's runtimes directly. These
have not yet been confirmed in game: frame generation switched on with 1.1.0, frame generation
together with FSR or XeSS upscaling, the IL2CPP build (it compiles but has not run), FSR 4 (Radeon
RX 9000), AMD Anti-Lag 2 (AMD GPUs) and Intel XeLL on its own (Intel GPUs).

## TG Better Upscaling

The game's Upscaling row gets two more entries after None, STP and DLSS: **FSR** and **XeSS**. STP
keeps the game's render-scale slider; DLSS, FSR and XeSS get their own quality modes instead, and FSR
one more row:

| Video tab row | Choices |
|---|---|
| Upscaling | The game's None, STP and DLSS, plus FSR and XeSS where their runtime loads on this GPU. Unavailable entries are skipped, as the game skips DLSS without an NVIDIA GPU. |
| Upscaling Quality | STP only: the game's own render-scale slider. An upscaler whose runtime reports no quality modes keeps it too. |
| Quality Mode | DLSS, FSR and XeSS: the modes the upscaler's runtime reports, each at the render resolution its vendor recommends. DLSS: DLAA (when NGX offers it), Quality, Balanced, Performance, Ultra Performance. FSR: Native AA, Quality (1.5x), Balanced (1.7x), Performance (2x), Ultra Performance (3x). XeSS: Native Anti-Aliasing, Ultra Quality Plus (1.3x), Ultra Quality (1.5x), Quality (1.7x), Balanced (2x), Performance (2.3x), Ultra Performance (3x). Restore Defaults picks Quality. |
| FSR Sharpness | FSR only: FSR's own sharpening after upscaling, 0% (off) to 100%, 50% by default. DLSS and XeSS have no sharpening of their own (NVIDIA dropped DLSS's), so they get no such row. |

The modes are the ones each vendor tunes. NGX picks the DLSS model from the quality level it is
given (NVIDIA's header names model K for DLAA, Quality and Balanced, M for Performance and L for Ultra
Performance), and the game, with its slider, gave DLSS a level from the slider's position whatever the
render size; with a mode, the level and NVIDIA's render size for it match. Each vendor also sizes its
modes for its own cost and reconstruction, so XeSS Performance renders at 43.5% where FSR
Performance renders at 50%. A config without a mode yet starts each upscaler on the highest-quality
mode at or below the game's render scale, so the change never costs frames; the game's graphics
presets set the modes by the same rule.

FSR runs AMD's default version for the GPU: FSR 4 on a Radeon RX 9000, the newest FSR 3.1 elsewhere.
AMD asks that the version is left to that default or chosen by the player, so there is no version
row; the native log names the version AMD chose.

The game's own DLSS gets a newer DLSS DLL: NVIDIA's `nvngx_dlss.dll` 310.9.1 from the DLSS SDK,
bundled in `Native/DLSS`, where the game ships 3.8.10. NVIDIA's runtime loads `nvngx_dlss.dll` from
the game folder when the game starts DLSS; when that file is older than the bundled one, the load
gets the bundled one instead (`native/upscaling/src/dlss_dll.cpp`). An equal or newer file, from a
DLSS swap or the driver's own download, is kept, and so is one a DLSS mod installed as part of a set:
with DLSS neural rendering (`nvngx_dlssnr.dll`, as DLSS 5 installs it) or frame generation or ray
reconstruction of the same release beside it. The RenoDX DLSS 5 add-on works with the DLLs it
ships; with our newer `nvngx_dlss.dll` in their place, its frame rate stopped following the render
resolution. Nothing in the game folder changes. The game's
HDRP can't set a DLSS preset, so the model is the one the DLL picks for the quality mode. The native
log names the DLSS version the game got.

The choice is stored in the game's own preferences like any other: FSR or XeSS stays selected across
sessions, and without the mod the game falls back to its default. XeSS runs the chosen preset; with
the slider (no modes reported) it picks the preset whose range contains the render scale.

FSR and XeSS take STP's place. For them the game's upscaling controller sets up the camera exactly as
for STP (dynamic resolution at the mode's render scale, DLSS off), HDRP renders and jitters as for STP,
and where HDRP would run STP, TG Better Upscaling hands the frame's color, depth and motion vectors to the
native side and returns the upscaled image in the texture STP would have written. Everything after
it sees an upscaled frame exactly as it would after STP: HDRP's post-processing, RenoDX, ReShade,
and TG Frame Generation, which then takes HDRP's own depth and motion vectors at render resolution
as it does with STP. The camera's jitter stays HDRP's own: a Halton(2,3) sequence over 1024 frames,
more than the 8 x (output / render width)^2 samples AMD and Intel ask for.

## Performance overlay

TG Graphics Bridge draws a performance overlay in a corner of the screen. **Performance Overlay** is
the last row of the game's Video settings (Off, On), with **Overlay Position** (the corner) and
**Overlay Size** below it while it is on; FoA Mod Manager lists the same rows under TG Graphics Bridge,
and they are stored in `BepInEx/config/com.wessberg.tggraphicsbridge.cfg` (section Video). The row is
added once, by the one copy of the bridge's plugin BepInEx loads, however many mods bundle it. Its own
section has the frame rate the game
renders at, the frame time and the slowest 1% of the last 600 frames, the render and output
resolution, VSync and the frame cap (the game's `QualitySettings.vSyncCount` and
`Application.targetFrameRate`), CPU and GPU use of the game and the system, and video memory. Each
mod adds a section through `DebugOverlay.AddSection`:

- TG Better Upscaling: the upscaler, its version and quality mode; for DLSS, the model and the DLSS
  DLL the game loaded. NGX has no query for the model in use, so the overlay reads the overrides the
  NVIDIA App (or NVIDIA Profile Inspector) stores in the driver's profile for the game, through
  NVAPI's driver settings: a forced preset shows as that preset, a forced performance mode, render
  scale or DLAA as a line of its own. Without an override it is the DLL's default for the mode. The
  render size in the Performance section is what the game renders at, which DLSS receives either way.
- TG Frame Generation: the technology, fixed or dynamic and the multiplier, the frame rate on screen
  against the rendered one (DLSS-G and XeSS-FG count their presented frames; FSR generates one per
  rendered frame), and the low latency technology.

CPU use comes from `GetProcessTimes`/`GetSystemTimes`, GPU use from Windows' `GPU Engine` counters
(the busiest engine type, as Task Manager shows it), and video memory from
`IDXGIAdapter3::QueryVideoMemoryInfo` on the game's adapter. The native bridge samples them twice a
second from the first time the overlay shows (`native/bridge/src/performance.cpp`).

## TG Frame Generation

| Video tab row | Choices |
|---|---|
| Frame Generation | Off, plus each of DLSS, FSR and XeSS whose runtime reports support on this GPU. |
| Frame Generation Mode | DLSS only: Fixed or Dynamic, when the runtime reports Multi Frame Generation and Dynamic support. |
| Frame Multiplier | 2x up to the selected technology's maximum. Switching technology clamps the value. |
| Max Frame Multiplier, Target Framerate | DLSS Dynamic only: the upper limit and the framerate to aim for. |
| Low Latency | Off, NVIDIA Reflex, NVIDIA Reflex + Boost, Intel XeLL, AMD Anti-Lag 2, as supported. DLSS needs Reflex and XeSS always runs with XeLL, which greys the row out. FSR requires none, so Off stays available with it. |

The maxima come from the runtimes: DLSS-G's `numFramesToGenerateMax` (up to 6x on RTX 50) and
XeSS's `maxSupportedInterpolations` (up to 4x on Intel Arc, 2x elsewhere). The FSR API has no
multi-frame control, so FSR is 2x, and neither FSR nor XeSS has a dynamic mode.

When DLSS upscaling is on, frame generation takes its motion vectors and depth from the DLSS
upscaler. With any other setting (TAA, STP, FSR, XeSS or no anti-aliasing), a custom pass takes
HDRP's own depth and motion vectors instead, so frame generation works on AMD and Intel GPUs too,
and on top of TG Better Upscaling. The scene is captured before the overlay HUD is drawn, which keeps the
HUD out of interpolation with all three technologies.

The rows use the game's Apply Changes, Undo and Restore Defaults. Frame generation pauses in menus,
while the cursor is unlocked and while the game is paused. Values are stored in
`BepInEx/config/com.wessberg.tgframegeneration.cfg`; FoA Mod Manager lists the same six rows with
the same choices and rules. The `[Advanced]` section holds a base FPS cap through the active
low-latency technology, the emergency off key (F8), the pause rules and a diagnostic report switch.

## TG Graphics Bridge

The bridge has no settings of its own, and no download of its own: each mod bundles all of it under
its own folders (its preloader as `BepInEx/patchers/<mod>.Autoload.dll`, its plugin beside the mod's,
its native DLL in the mod's `Native` folder), so no file belongs to both mods and Vortex reports no
conflict. With both installed, the copy bundled with the newer bridge starts it and BepInEx loads the
newer plugin; `docs/AUTOLOAD-INTEGRATION.md` has the rules. What it does for the two mods:

- Starts from the BepInEx preloader, early enough to see the game create its device and swapchain;
  no launcher and no replaced `dxgi.dll`, `winhttp.dll` or `version.dll`. It finds each mod built on
  it by `BepInEx/<mod folder>/graphics-bridge-extension.json` and loads that mod's native DLL. With no
  such mod installed it loads nothing.
- Owns the only set of graphics hooks, observes the game's real D3D11 device (also behind ReShade),
  and creates one D3D12 device inside the game's swapchain creation, which both mods share. On NVIDIA
  GPUs with frame generation installed, that device is created through Streamline.
- Keeps TG Frame Generation and TG Better Upscaling out of Avalon Mod Manager's MODS tab (which on Apply
  writes back the values it read when Options opened, undoing what the Video tab just applied); FoA
  Mod Manager lists their rows instead.
- Corrects the game's motion vectors while either mod reads them: first-person Kandra materials get
  their motion vector pass back, and an extra camera render in the same frame no longer overwrites
  last frame's skinned positions.

## How it works

```text
FoA + ReShade/RenoDX, rendering through D3D11
  │  HDRP: render at the Upscaling row's scale ─┬─ STP (the game's own)
  │                                             ├─ DLSS (the game's own)
  │                                             └─ FSR / XeSS: color, depth, motion → TG Better Upscaling
  │                                                  ↓ shared textures, GPU fences
  │                                              bridge's D3D12 device: FSR API or XeSS SR
  │                                                  ↓ upscaled image back into HDRP's frame
  │  post-processing, UI
  ├─ frame generation inputs: the DLSS upscaler's, or HDRP's own buffers before post-processing
  └─ the final image is taken at the D3D11 Present
       ↓ shared textures and GPU fences on the same adapter
bridge's D3D12 device → presentation swapchain of the frame generation technology
  ├─ Off / DLSS: NVIDIA Streamline (DLSS-G, Reflex)
  ├─ FSR: AMD's frame generation swapchain
  └─ XeSS: Intel's XeSS-FG swapchain (with XeLL)
       ↓ rendered and generated frames
Display

Steam → BepInEx preloader (one mod's copy of TGGraphicsBridge.Autoload) → TGGraphicsBridge.Native
          → TGFG.Native, TGBetterUpscaling.Native (extensions)
BepInEx plugins → Video rows, camera and frame events, render passes, diagnostics
```

Streamline is loaded only when the GPU supports DLSS-G or Reflex. The Streamline interposer ships as
`TGFG.Streamline.dll`, so the RenoDX DLSS addon, which hooks `sl.interposer.dll` by name, leaves
frame generation's copy alone. Each mod carries its own copy of the AMD and Intel runtimes it loads,
so either works without the other.

| Area | Implementation and limits |
|---|---|
| Managed | `net472` for Mono, `net6.0` for IL2CPP. One set of native x64 DLLs serves both. |
| DLSS-G | Streamline 2.14.1 DLSS-G: fixed multipliers, `eDynamic` with `dynamicTargetFrameRate`, Reflex required. |
| FSR | AMD's signed loader, frame generation and upscaler DLLs: FSR 4 on Radeon RX 9000, FSR 3.1 elsewhere; the version list comes from the runtime. |
| XeSS | Intel's signed `libxess.dll` (upscaling), `libxess_fg.dll` and `libxell.dll` (frame generation). |
| DLSS DLL | NVIDIA's signed `nvngx_dlss.dll` 310.9.1 from the DLSS SDK, for the game's own DLSS when the game folder's is older. |
| Upscaling inputs | HDRP's STP inputs: pre-exposed linear color, reversed-Z depth, motion vectors scaled to render pixels, HDRP's jitter negated as the SDKs expect, HDRP's own history reset. FSR gets HDRP's exposure as its pre-exposure and exposure, as HDRP's DLSS pass takes it. Texture mip bias as each vendor asks: HDRP's `log2(render / output)` for XeSS, one mip more for FSR. |
| Synchronization | GPU fences between the D3D11 and D3D12 queues; every wait has a matching signal. The CPU never reads an image back. |
| HDR | FSR and XeSS upscale HDR color before tonemapping. Frame generation encodes FP16 scRGB to RGB10A2 PQ/BT.2020 for its swapchain. |

## Building

Windows x64 with the Visual Studio C++ build tools, CMake 3.24 or newer, Windows SDK 10.0.22621 or
newer, and a .NET SDK that builds `net472` and `net6.0`. The SDKs go next to this repository (they
are git-ignored):

| Folder | Contents |
|---|---|
| `Streamline-2.14.1` | NVIDIA Streamline 2.14.1 source: headers and `external/ngx-sdk/include` |
| `Streamline-2.14.1-bin` | `bin/x64` and `license.txt` from the `streamline-sdk-v2.14.1.zip` release |
| `FidelityFX-SDK-2.3.0` | AMD FSR SDK 2.3.0 headers and, in `bin/`, the signed `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll` and `amd_fidelityfx_upscaler_dx12.dll` |
| `XeSS-SDK-3.0.2` | Intel XeSS SDK 3.0.2 release archive |
| `AntiLag2-SDK-2.0.0a` | AMD Anti-Lag 2 SDK 2.0.0a release archive |
| `DLSS-SDK-310.9.1` | `lib/Windows_x86_64/rel/nvngx_dlss.dll` and `LICENSE.txt` from NVIDIA's DLSS SDK 310.9.1 |

CMake fetches MinHook v1.3.4 unless `-MinHookRoot` is given. The game needs the matching BepInEx
already installed. `docs/SOURCES.md` links every SDK release.

```powershell
# Native DLLs: TGGraphicsBridge.Native, TGFG.Native, TGBetterUpscaling.Native (VS x64 developer shell)
.\Build-Native.ps1 -StreamlineRoot Streamline-2.14.1 `
  -NgxIncludeDir Streamline-2.14.1\external\ngx-sdk\include

# Signed NVIDIA, AMD and Intel runtimes with their license notices, into each mod's folder
.\Stage-Runtime.ps1 -RuntimeDirectory Streamline-2.14.1-bin\bin\x64 `
  -FidelityFXRoot FidelityFX-SDK-2.3.0 -XeSSRoot XeSS-SDK-3.0.2 -DlssRoot DLSS-SDK-310.9.1

# Plugins and preloader, then package and install all three (game closed)
.\Build.ps1 -Runtime Mono `
  -GameDirMono 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' -Deploy
```

For the standard version, use `-Runtime IL2CPP -GameDirIl2Cpp <game folder>`. BepInEx 6 has to have
started the game once, so that its interop assemblies exist.

`-Mod TGFrameGeneration|TGBetterUpscaling` limits packaging and deployment to one mod (refused when the
other installed mod carries a different build of the same bridge version).
`-Deploy` backs up every file it replaces under `BepInEx/<mod>/Backups`, replaces files Vortex
deployed as links instead of writing through them into Vortex's staging folder, and removes what
Frame Generation 1.0.x installed and 1.1.0 no longer uses (`BepInEx/patchers/TGFG.Autoload.dll`). Every packaging run
writes the Nexus Mods downloads to `artifacts/Nexus`, one per mod and game version:
`<mod> - mono.zip` for the Mono branch and `<mod>.zip` for the standard version. Each contains the
mod's BepInEx payload, `<mod>-Package-manifest.json` and `vortex_override_instructions.json`, which
tells Vortex to install the archive at the game root under the mod type it calls "Engine Injector".

`Test-Setup.ps1 -GameDir <game folder> -Runtime Mono` (or `IL2CPP`) checks an installed copy without
starting the game. Native builds run the portable ABI, policy, interop, camera-orientation and
upscaling tests, compile the HLSL and run the GPU smoke test; `tests/source_checks.py`,
`tests/test_autoload_integration.py` and `tests/package_tests.ps1` check sources and packaging, and
`tests/InstallCheck.Tests.csproj` runs the install check on folders laid out the ways players unpack the mods.
`docs/TESTING.md` has the in-game test sequence.

## Diagnostics

```text
BepInEx/TGGraphicsBridge/Autoload.log
BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log
BepInEx/TGFrameGeneration/Native/TGFG.Native.log
BepInEx/TGBetterUpscaling/Native/TGBetterUpscaling.Native.log
BepInEx/TGFrameGeneration/diagnostics/, BepInEx/TGBetterUpscaling/diagnostics/
```

**Create Troubleshooting Report**, nested under Performance Overlay, runs a test of about two minutes
in the game (the player's settings, then each upscaler, frame generation off with each low latency
technology, each frame generation technology with each low latency technology it allows), then
saves one text file for pasting into a bug report as
`BepInEx/TG Troubleshooting Report <date>.txt`: a summary with each step's
measurements, the system, the install and compatibility checks and what the native side knows, then
this session's lines from the logs above, filtered to warnings, errors and the mods' own lines, the
config values and where the mods' files are (`src/TGGraphicsBridge/ReportText.cs`). The mods' problems (a wrong install folder, OptiScaler, frame rate limits
outside the game, no framerate gain from frame generation) show as rows above the game's Upscaling row
and at the top of the performance overlay (`src/TGGraphicsBridge/Troubleshooting.cs`, `InstallCheck.cs`,
`Compatibility.cs`, `src/TGFrameGeneration/FrameRateCheck.cs`).

The bridge's log says which extensions it loaded, when it saw the game's device and swapchain, and
who created the D3D12 device. TGFG's log has a `Pipeline 5s:` line every five seconds. TG Better Upscaling's
log lists the FSR versions and the XeSS version at startup and names the FSR version AMD chose, and every ten seconds an `Upscaling 10s:`
line with the technology, render and output size, frames and fallbacks.

To keep a mod's native DLL from loading, close the game and create an empty `disable-autoload.txt`
in that mod's `BepInEx/<mod>` folder; in `BepInEx/TGGraphicsBridge` it stops the bridge and with it
both mods.

## Known limits

- FSR and XeSS upscaling run where HDRP runs STP, so they need what STP needs (Shader Model 5.0 and
  HDRP's dynamic resolution); at a render scale of 100% HDRP may not run its upscaler at all.
- The HDRP input path needs HDRP to render motion vectors for the gameplay camera, which it does
  with TAA, STP, FSR, XeSS and DLSS.
- The HUD-less frame relies on the HUD being screen-space overlay UI (or drawn by a later camera).
  A canvas rendered by the gameplay camera stays part of the interpolated image.
- One window, one adapter and one stable D3D11 back buffer. No stereo, MSAA swapchain, device-loss
  recovery or unloading the native DLLs while the game runs.
- HDR10 output from frame generation is 10-bit PQ and clips outside its range and gamut.

`docs/ARCHITECTURE.md`, `docs/TESTING.md`, `docs/SOURCES.md` and `docs/AUTOLOAD-INTEGRATION.md`
cover the implementation, the test plan and the primary sources. `docs/FSR-FLICKER-INVESTIGATION.md`
records the resolved HUD/hand flicker investigation; read it before changing the frame generation
input path.

## License

The source is MIT-licensed. The NVIDIA, AMD and Intel runtimes in a package keep their own licenses,
which ship next to them; see `LICENSES/THIRD_PARTY.md`.
