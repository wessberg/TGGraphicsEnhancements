# Installation and updates

## Choose the game version

Use the mod archive for the game branch you installed:

- Mono version: BepInEx 5 on the game's Mono modding branch.
- Normal version: BepInEx 6 on the standard game.

Install the matching BepInEx version and start the game once before adding the mods. Extract the mod archive
into the game folder. Vortex users should select Engine Injector so the BepInEx folder lands at the game root.
Close the game before installing or updating files. When several TG mods are installed, keep their versions matched.

Each mod includes TG Graphics Bridge. Its preloader goes under `BepInEx/patchers`, its plugin under
`BepInEx/plugins/<mod>`, and native files under `BepInEx/<mod>/Native`. The
[loading reference](AUTOLOAD-INTEGRATION.md#installed-layout) shows the full layout.

OptiScaler conflicts with the mods' upscaling and presentation hooks. Remove it before testing these mods.
ReShade/RenoDX combinations and Neural Rendering runtime limits are recorded in
[DLSS5-COMPATIBILITY.md](DLSS5-COMPATIBILITY.md) and [TESTING.md](TESTING.md).

## Neural Rendering runtime

The download includes NVIDIA's signed `nvngx_dlssnr.dll`. To use a compatible community runtime, close the game,
replace `BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll` and restart. Updating the mod restores the signed
file, so reapply a replacement afterward. Older-RTX hardware support remains unverified; see the
[compatibility notes](DLSS5-COMPATIBILITY.md).

## Troubleshooting


```text
BepInEx/TGGraphicsBridge/Autoload.log
BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log
BepInEx/TGFrameGeneration/Native/TGFG.Native.log
BepInEx/TGBetterUpscaling/Native/TGBetterUpscaling.Native.log
BepInEx/TGFrameGeneration/diagnostics/, BepInEx/TGBetterUpscaling/diagnostics/
```

Open Options > Video and select Create Troubleshooting Report under Performance Overlay. It runs for about
two minutes, trying the upscalers, frame generation and low-latency combinations your card supports, then
restores your settings. The report includes measurements, system and installation details, warnings and logs.
Paste the contents of `BepInEx/TG Troubleshooting Report <date>.txt` into your bug report.

Amber warning icons mark settings with a detected problem. Hover over an icon for the explanation.

The bridge's log says which extensions it loaded, when it saw the game's device and swapchain, and
who created the D3D12 device. TGFG's log has a `Pipeline 5s:` line every five seconds. TG Better Upscaling's
log lists the FSR versions and the XeSS version at startup and names the FSR version AMD chose, and every ten seconds an `Upscaling 10s:`
line with the technology, render and output size, frames and fallbacks.

To keep a mod's native DLL from loading, close the game and create an empty `disable-autoload.txt`
in that mod's `BepInEx/<mod>` folder. In `BepInEx/TGGraphicsBridge`, the file stops the bridge and all mods using it.

