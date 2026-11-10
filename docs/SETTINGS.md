# Settings

The mods add rows to Options > Video. FoA Mod Manager shows the same names, choices and availability.

## TG Frame Generation


| Video tab row | Choices |
|---|---|
| Frame Generation | Off, plus each of DLSS, FSR and XeSS whose runtime reports support on this GPU. |
| Frame Generation Quality | DLSS only: Quality (NVIDIA's default) or Performance (NVIDIA's Preset C, a little faster and a little less exact). |
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
`BepInEx/config/com.wessberg.tgframegeneration.cfg`; FoA Mod Manager lists the same rows with
the same choices and rules. The `[Advanced]` section holds a base FPS cap through the active
low-latency technology, the emergency off key (F8), the pause rules and a diagnostic report switch.

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

Quality modes keep the upscaler's model choice and recommended input resolution aligned. The game's
slider previously chose a DLSS level even when the render size differed from that level's recommendation. Each vendor also sizes its
modes for its own cost and reconstruction, so XeSS Performance renders at 43.5% where FSR
Performance renders at 50%. A config without a mode yet starts each upscaler on the highest-quality
mode at or below the game's render scale, so the change never costs frames; the game's graphics
presets set the modes by the same rule.

FSR runs AMD's default version for the GPU: FSR 4 on a Radeon RX 6000 or newer and some Ryzen processors, the newest FSR 3.1 elsewhere.
AMD asks that the version is left to that default or chosen by the player, so there is no version
row; the native log names the version AMD chose.

The game's own DLSS gets a newer DLSS DLL: NVIDIA's `nvngx_dlss.dll` 310.9.16 from the DLSS SDK,
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

## TG Neural Rendering

The Video menu and FoA Mod Manager have **Neural Rendering** (Off / DLSS), **Placement** (Quality / Performance), **Strength**,
and **Advanced**. Advanced shows **Local Tone**, **Style** (Default/A/B/C),
**Passes** (1-4), **Detail**, **Automatic Mask** and, with
masking on, **Skin Detail**. Hiding those controls keeps your choices. Neural Rendering starts Off.
I use NVIDIA's Default style, full strength, tone and detail, with automatic masking Off for both placements. Quality runs
after upscaling and is the default. Performance uses fewer pixels before upscaling, but can soften detail
and show larger colour or brightness changes after you stop moving. Ray Reconstruction requires Quality.
For a look closer to the original lighting and colour, lower Local Tone. Lower Strength to reduce the whole
effect, including detail; zero keeps the original scene. These controls range from 0 to 1 and apply to each pass.
I default to one pass. Extra passes feed the result back through NR, using the same settings each time.
They cost more GPU time and memory and can exaggerate colour changes or artifacts. Strength applies within each model pass, so two passes at half Strength produce a different result from one at full Strength.
The main row greys out unless DLSS and the other requirements are available, with the reason in its tooltip.
The download includes NVIDIA's signed `nvngx_dlssnr.dll` 310.9.16.0, NVIDIA's faster model of October 2026, unmodified, from
NVIDIA's public RTX Remix release. To use another DLL, close the game and copy it over
`BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll`, then restart. Do that again after updating the mod.

The NVIDIA-signed 310.9.16.0 and 310.8.0.0 DLLs use the normal NVIDIA path. Compatible patched DLLs use a separate
adapter included with the mod. Selection is automatic: no extra setting, separate adapter download or list of
approved community files. Compatible new patches can be swapped in without updating the mod; a change to
the runtime's interface may still need a mod update.

This includes the community patch intended for RTX 2000, 3000 and 4000 cards. I have tested the patched DLL
on my RTX 5080, but have not confirmed it on those older cards. [DLL guidance](nexus-neural-rendering.md).
[Test results and remaining checks](DLSS5-COMPATIBILITY.md). [Implementation](DLSS5-IMPLEMENTATION.md).

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

## Path Tracing

PT development is paused. The [current status](PT-STATE.md), [requirements](PT-DESIGN.md) and
[issue register](PT-REQUESTS.md) describe its settings, remaining gaps and build blocker.
