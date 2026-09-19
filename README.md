# TG Frame Generation 0.4.0 — native Video settings, Reflex modes, DX11 bridge

**FoA stays on Direct3D 11. TGFG supplies a separate Direct3D 12 presentation
backend to NVIDIA Streamline. No OptiScaler or FidelityFX backend is used.**
The existing BepInEx settings-menu adapter and the Mono/IL2CPP build split are
retained. The native backend is v0.4.0 / ABI v4 (adds the Reflex mode). This update integrates the
preloader, build, package and deployment into the existing repository.

## Status — experimental source, not a verified release

The Windows native DLL, launcher, HLSL shaders, Mono target and IL2CPP target have
**not been compiled in the preparation environment**. Since then, in-game runs on an
RTX 5080 have confirmed frame generation with and without ReShade + RenoDX HDR; the
combination with DLSS 5 (RenoDX DLSS) is not yet confirmed. Portable C++ ABI/control/interop
policy tests and package consistency tests were run; see `docs/VALIDATION.md`.
The archive contains source, **not precompiled Windows binaries**.

There is implemented bridge code, not just an interface for a future backend:
D3D11-facing swapchain interception, shared textures, D3D11/D3D12 shared fences,
private D3D12 device/queue/swapchain creation through Streamline, D3D11 NGX input
capture, shader-based input copies, optional scRGB-to-HDR10 conversion, resource
tagging, presentation, Reflex markers and fixed/Dynamic DLSS-G options.
Windows compilation or game testing may reveal defects requiring corrections.

Your supplied diagnostics established **v0.3.0 Mono loading and UI insertion**, not
native FG. No IL2CPP game test has been supplied. The UI is preserved structurally;
its status area is now shorter and the full report has more native diagnostics.

## Architecture

```text
FoA + RenoDX/ReShade, rendering through D3D11
  ├─ original DLSS SR calls are forwarded unchanged
  ├─ depth/motion copies at the native D3D11 NGX evaluation
  └─ final color at the D3D11-facing Present boundary
       ↓ same-adapter shared textures + GPU fences
TGFG private D3D12 device / DIRECT queue
       ↓ tagged camera/depth/motion/(optional HUD-less image)
NVIDIA Streamline DLSS-G + Reflex + private output swapchain
       ↓ rendered and generated output frames
Display

Steam Play → BepInEx preloader TGFG.Autoload → native bootstrap
BepInEx gameplay plugin → native controls, camera/frame events, diagnostics
```

The source color is taken at the wrapper's Present boundary, so it includes the work of
outer ReShade/RenoDX wrappers. Frame generation has been confirmed in game with ReShade 6.8
and RenoDX HDR (scRGB swapchain, presented as HDR10).

TGFG loads NVIDIA's `sl.interposer.dll` under the private file name
`TGFG.Streamline.dll` (same bytes, same NVIDIA signature). Streamline-aware injectors such as
the RenoDX DLSS addon ("DLSS 5") find a game's Streamline by that module name and hook its
exports and DXGI factories. That addon then releases any swapchain ReShade did not see being
created, which made TGFG's private output swapchain fail with `E_UNEXPECTED`. In FoA the addon
runs DLSS 5 Neural Rendering directly through NGX on its own D3D12 device and does not start the
Streamline 2.13 DLLs it ships, so there is no second Streamline to share. The mod neither
disables nor replaces DLSS 5. Its NGX hooks still see TGFG's DLSS-G evaluations; the combined
setup still needs confirmation in game.

## What is retained or added

| Area | Implementation / limitation |
|---|---|
| Managed build | `net472` Mono; `net6.0` IL2CPP. One native x64 backend serves either. |
| In-game controls | Real game settings rows in Options > Video, directly after Dynamic Resolution: Frame Generation [Off / DLSS]; when DLSS is selected and the runtime reports Multi Frame Generation: Frame Generation Mode [Fixed / Dynamic], Frame Multiplier [2x .. reported max] (Fixed), Max Frame Multiplier [Up to 2x .. Up to max] and Target Framerate [Auto / 60 .. display maximum] (Dynamic); plus NVIDIA Reflex [Off / On / On + Boost], with Off forbidden while FG is on. They use the game's Apply Changes / Undo / Restore Defaults and persist in `BepInEx/TGFrameGeneration/VideoSettings.cfg`. Mono runtime only; the IL2CPP build edits that file directly. |
| Fixed FG/MFG | Requests 2×–6× total output; runtime caps gate activation. 4× means three generated frames. |
| Dynamic MFG | Uses Streamline `eDynamic` and `dynamicTargetFrameRate`, not a loop switching fixed multipliers. |
| Target framerate | Dynamic target Auto (0) asks NVIDIA to use the current display refresh; with VSync on the runtime targets the refresh rate regardless. This does not change monitor Hz. |
| NVIDIA Reflex | Streamline Reflex low-latency (optionally Boost) with per-frame sleep and PCL markers from the plugin. The game itself has no Reflex integration. |
| D3D11 inputs | Copies the actual SR depth/motion inputs to shareable R32_FLOAT (depth) and R16G16_FLOAT (motion, lossless for HDRP's FP16 vectors) textures; preserves jitter and scales from NGX. R32G32_FLOAT is not NT-handle shareable on NVIDIA's D3D11 driver. |
| GPU synchronization | Producer signal/consumer wait, private queue submission, NVIDIA input-consumption fence, consumer signal/D3D11 wait. No per-frame image download to CPU. |
| HDR | FP16 scRGB source can be converted to RGB10A2/PQ/BT.2020 for presentation. Existing SDR/HDR10 formats are copied. The HDR10 output path requires compatible HDR output. |
| Diagnostics | Source/output APIs, source/output formats, bridge flags, interop HRESULT, source presents, copies, captured input pairs, NGX calls, NVIDIA-reported output count, native PID. |
| Loading | Automatic BepInEx preloader; no TGFG launcher and no replacement of `winhttp.dll`, `dxgi.dll` or `version.dll`. |

The DX11-facing adapter exposes **a stable logical `GetBuffer(0)`**. The private
DX12 output has three physical buffers and uses its actual current buffer index.
This avoids incorrectly rotating the texture behind a cached D3D11 render target.
Indexed D3D11 clients (`GetBuffer(1+)`) are not implemented; see limitations below.

## Requirements

Use Windows x64 with Visual Studio C++ build tools, CMake 3.24+, Git, Windows SDK
10.0.22621 or newer, and a .NET SDK able to build the two target frameworks.
Provide official **NVIDIA Streamline 2.14.1** headers/matching production runtimes,
and the NVIDIA DLSS/NGX headers. Review NVIDIA's licenses yourself. The native
source uses typed SDK declarations; no proprietary headers or runtime binaries
are bundled. CMake fetches MinHook v1.3.4 unless a local `-MinHookRoot` is supplied.

The game installation must already have the matching BepInEx runtime. IL2CPP
requires generated interop assemblies from that exact game build. GPU, driver,
OS and hardware-scheduling capability are determined by NVIDIA's runtime, not
inferred solely from the GPU model.

## 1. Build inside this repository

Autoload now lives in `src/TGFG.Autoload`, beside the gameplay plugin, and is built
by the existing `Build.ps1`. There is no second repo or `-ProjectRoot` argument.
Both projects use this repo's existing `Build.Local.props`.

For the current Steam installation running Mono:

```powershell
.\Build.ps1 -Runtime Mono `
  -GameDirMono 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' `
  -Deploy
```

`-Deploy` builds both managed DLLs, assembles the complete package using this
repo's existing `artifacts/Native`, backs up replaced TGFG files, and installs it.
No process is launched. Native v0.3.0 (ABI `0x00030000`) is reused unchanged.
The managed pair is v0.3.1. The old standalone Build-Autoload.ps1 is not needed.

Without `-Deploy` or `-Package`, only the managed pair is built/staged. Use
`-Package` to create a complete player ZIP without installing it:

```powershell
# After configuring both installations in Build.Local.props:
.\Build.ps1 -Runtime Both -Package

# Repackage existing managed/native artifacts without compiling:
.\Package.ps1 -Runtime Mono -Deploy
```

The managed artifacts are kept separate:

```text
artifacts/Mono/BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll
artifacts/Mono/BepInEx/patchers/TGFG.Autoload.dll
artifacts/IL2CPP/BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll
artifacts/IL2CPP/BepInEx/patchers/TGFG.Autoload.dll
```

Player ZIPs are staged under `artifacts/Packages` with runtime-specific names.
They contain the plugin, matching preloader, native backend, matching NVIDIA
runtime and installation instructions, not a launcher or development scripts.
Do not install both runtime packages into the same game installation.

## 2. Native prerequisites (existing artifacts can be reused)

This integration changes no native source and requires no native rebuild when
`artifacts/Native/TGFG.Native.dll` matches its v0.4.0 build manifest.
If native artifacts or runtime files are missing, use the same existing scripts:

```powershell
.\Build-Native.ps1 `
  -StreamlineRoot C:\SDKs\Streamline-2.14.1 `
  -NgxIncludeDir C:\SDKs\DLSS\include

.\Stage-Runtime.ps1 -RuntimeDirectory C:\SDKs\Streamline-2.14.1\bin\x64

.\Build.ps1 -Runtime Mono `
  -GameDirMono 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' `
  -Deploy
```

Official NVIDIA DLLs are not part of the source patch. Packaging rechecks native
hashes and NVIDIA production signatures. Review NVIDIA distribution requirements
before publishing runtime-containing archives. No existing game/RenoDX runtime
DLLs are overwritten: these are separate TGFG-private copies.

## 3. Press Play in Steam

After deployment, start the game normally. No TGFG launcher, Steam launch options,
app-ID file or monitor script is required by this package. BepInEx's preloader
calls the native bootstrap in that game's own process.

This backend still requires Unity's **DX11** rendering path. Autoload does not
force an API, change the player's display mode, or add launch arguments. Native
DX12 is private to the bridge. A game running DX12 remains unsupported by it.

Optional installation check (does not launch the game):

```powershell
.\Test-Setup.ps1 `
  -GameDir 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' `
  -Runtime Mono
```

The old Launch-Game.ps1 is retired as a launcher; it now only performs this check.
Do not combine an externally injected native backend with automatic loading.

## First test and diagnostics

Leave Frame Generation **Off** (Options > Video). Check these paths inside the GAME installation:

```text
BepInEx/TGFrameGeneration/Autoload.log
BepInEx/TGFrameGeneration/Native/TGFG.Native.log
BepInEx/TGFrameGeneration/diagnostics/
```

The gameplay diagnostic includes a bounded tail of Autoload.log, with an explicit
warning that old sessions may be present. Compare timestamps and PIDs. A loaded
DLL and successful bootstrap return are not proof that loading occurred early
enough or that a bridge was created. BepInEx preloader timing in this game has not
been validated for either runtime.

First verify source DX11, private presentation DX12, bridge flag and increasing
copy counts. Those may advance with FG Off and do not prove interpolation. Then
test Fixed 2x in gameplay outside Video settings, and measure presented output.

To disable automatic native startup, close the game and place an empty
`BepInEx/TGFrameGeneration/disable-autoload.txt`. Restart normally. Deployment
preserves this marker and all existing settings. Remove/move the marker to resume
autoload. Startup failure may leave partial hooks; restart after correcting it.

## Known limits — do not interpret the package as certified compatibility

- No game-specific post-RenoDX/pre-HUD hook is installed. The native
  `TGFG_CaptureHudlessD3D11` export can copy an explicitly provided correct frame;
  it is not invoked by the generic managed adapter. Initial FG uses final color
  with depth/motion and can distort HUD/crosshairs. It does not invent a clean HUD.
- The selected gameplay camera, its matrices, HDRP camera-relative behavior,
  motion-vector conventions and plugin-event ordering need visual validation.
  NGX must use dynamically resolved D3D11 SuperSampling entrypoints. Static or
  hidden providers, multiple evaluations/views, typeless motion input requiring
  an unknown typed interpretation, or a different immediate context are rejected.
- One primary HWND, one adapter, one stable DX11 logical buffer zero. No stereo,
  MSAA swapchain, GDI composition, indexed source buffers, dirty/scroll Present1,
  nonblocking Present, multiple presenting windows, device-loss recovery, or live
  native unloading. Nonblocking requests return `WAS_STILL_DRAWING` without
  consuming the frame; a client that only uses that mode is unsupported.
- HDR10 conversion is not lossless FP16 presentation: it uses 10-bit PQ output and
  clips outside its representable range/gamut. Linear scRGB input must actually
  use the standard 80-nit unit. No arbitrary HDR format is reinterpreted as PQ,
  no extra exposure is applied, and no automatic HDR-to-SDR tone mapper is supplied.
- The private output swapchain remains allocated in Off mode, with bridge copies
  still occurring. Turning FG Off is not equivalent to removing the bridge and
  does not promise zero overhead. Mode-change swapchain teardown/recreation is
  not implemented. NVIDIA-runtime behavior on those transitions needs testing.
- Existing waitable-object flags are preserved. Their pacing interaction with FG
  and your driver is unverified; they are not silently removed. Reflex timing is
  based on Unity callbacks, not an NVIDIA-certified engine integration.
- Some interfaces/events can be observed by multiple graphics injectors. The
  interception order, COM wrappers, HUD ordering and resize/fullscreen behavior
  with your actual RenoDX and ReShade versions remain unverified.

To return to the original native path, exit and disable/remove the autoload
preloader before launching normally through Steam. Removing only the gameplay
plugin does not stop preloader initialization. No graphics proxy is replaced.

See `docs/ARCHITECTURE.md`, `docs/TESTING.md`, `docs/VALIDATION.md` and
`docs/SOURCES.md` for implementation detail, test boundaries and primary sources.

Repository integration details: `docs/AUTOLOAD-INTEGRATION.md`.
