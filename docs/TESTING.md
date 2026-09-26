# Local test sequence

This is a test plan, not a list of passed game tests. Use a normal single-player
session and close the game before rebuilding. Do not test multiple new renderer
changes simultaneously or infer success from the in-game FPS counter alone.

## Build boundary

Build the matching managed target with the actual game/BepInEx assemblies.
Build-Native.ps1 compiles native C++, links the real Windows/NVIDIA declarations,
and runs tests including compilation of the exact embedded HLSL. Preserve the
first compiler error rather than only the final MSBuild summary if it fails.
The Linux preparation tests do not establish any of those Windows outcomes.

`tgfg_camera_orientation` is portable arithmetic and runs on any host: it builds reference
projection and clip-to-previous matrices in both HDRP's render-target orientation and the
presentation orientation, deriving the latter through an independent matrix inverse, and checks
that `to_presentation_clip` turns one into the other, that a world point reprojects correctly
through the converted `clipToPrev`, and that the turn is its own inverse. It says nothing about
what the runtimes do with those matrices; that was established by an in-game comparison of DLSS,
FSR and XeSS. When repeating such a comparison, check that the deployed native DLLs hash equal to
their `artifacts/Native/<mod>/` builds first: a mod manager may have left a symlink to an older
build in place, which makes a real change look like no change at all. `Test-Setup.ps1` does this
for every installed mod.

The Mono Kandra history regression uses the production Harmony hook and the installed
Harmony runtime, with a stand-in skinning manager (no game launch or GPU):

```powershell
dotnet build tests/KandraMotionHistory.Tests.csproj -c Release
$gameRoot = (Get-Content src/TGGraphicsBridge/bin/Release/net472/TG.GameDir.txt -Raw).Trim()
& ./tests/bin/Release/net472/KandraMotionHistory.Tests.exe "$gameRoot/BepInEx/core"
```

It checks the guard for frame generation's request, upscaling's and both, consecutive single-camera
frames, repeated cameras, separate managers, frame gaps, no request, copy failure/retry, and
unpatch/reinstall. In-game, check the history guard installation and `Kandra motion history:
prevented ...` counters (TG Graphics Bridge's log source) with the minimap camera active.
Use a fresh F6 capture during a camera turn to verify hand reprojection; these unit checks
do not establish Unity GPU ordering or that the hand flicker is fixed.

For the IL2CPP guard branch, using the installed IL2CPP game's Harmony runtime:

```powershell
dotnet build tests/KandraMotionHistory.Tests.csproj -c Release -p:TestRuntime=IL2CPP
$gameRoot = (Get-Content src/TGGraphicsBridge/bin/Release/net6.0/TG.GameDir.txt -Raw).Trim()
dotnet tests/bin/Release/net6.0/KandraMotionHistory.Tests.dll "$gameRoot/BepInEx/core"
```

This patches a stand-in compute dispatcher with the production prefix/finalizer. Additional
cases cover native-pointer aliases, live copy-kernel IDs, unrelated/skinning dispatches,
overlapping kernel IDs, and copy versus skinning failure recovery. It does not load the game's
native IL2CPP runtime. In game, the installed guard must name `RunComputeKernel`; the inspected
native render loop bypasses the inlined `RunCopyPrevious` wrapper. Check duplicate-copy counters
and hand motion visually with FG on and an extra camera active.

For IL2CPP dependent-option visibility routing:

```powershell
dotnet build tests/Il2CppOptionConditions.Tests.csproj -c Release
$gameRoot = (Get-Content src/TGGraphicsBridge/bin/Release/net6.0/TG.GameDir.txt -Raw).Trim()
dotnet tests/bin/Release/net6.0/Il2CppOptionConditions.Tests.dll "$gameRoot/BepInEx/core"
```

This uses a stand-in getter and the production Harmony prefix to check owned/unowned
routing, native-pointer aliases, changing/nested conditions, exception isolation, owner
retention through GC, per-mod removal (one mod rebuilding its rows keeps the other's), and
unpatch/reinstall. It does not run native IL2CPP.
In game, enable upscaling, open Pause then Options repeatedly, and check both TGFG
dependent-option hook installation and evaluation messages. Change FG modes and verify
their dependent rows, Apply/Undo, and returning to gameplay. The reported Options crash
is not considered resolved solely because these stand-in tests pass.

The `tgfg_hudless_color` test executes the capture shader on D3D11 WARP and compares
120 output channels against reference SDR/scRGB/HDR encodings and linear passthrough, with padded input textures and
both Y orientations. Another 120 comparisons cover depth/motion input orientation, offset
subrects, signed FP16 motion and dispatch bounds. This validates conversion, not HDRP timing
or vendor interpolation.

The RenoDX shader-recognition test has positive/negative binding fixtures. To additionally
check an installed RenoDX Unity add-on without loading the DLL, run:

```powershell
& ./build/native/tgfg_renodx_final_pass_tests.exe "$gameRoot/renodx-unityengine.addon64" build/renodx-final-installed.dxbc
& ./build/native/tgfg_hudless_color_tests.exe build/renodx-final-installed.dxbc
```

The optional WARP check executes the extracted supported shader for 288 comparisons using
both `Draw` and `DrawInstanced`, across
brightness/gamma changes, constant-buffer range offsets and image orientation. It does not
exercise ReShade's runtime draw hooks. In-game, verify shader recognition, final-pass replay,
HUD-less pixel verification and increasing supply counts, then assess HUD stability visually.

`tgup_gpu_smoke` (and `tgup_gpu_smoke_unity_order`, with the factory and device created before the
bridge starts, as Unity does) checks that the bridge sees a swapchain and creates its D3D12 device
there. To check the hook order against ReShade, copy the game's ReShade `dxgi.dll` next to a copy of
`tgup_gpu_smoke.exe` and run it there (add `--with-fg` for frame generation). In the `ReShade.log`
beside it, the game's `Redirecting IDXGIFactory2::CreateSwapChainForHwnd` must appear, and no
`Redirecting D3D12CreateDevice` and no second `CreateSwapChainForHwnd`: ReShade wraps neither the
bridge's device nor frame generation's swapchain. The bridge's log says "Created the bridge's DXGI
factory past 3 hook(s)".

The `tgfg_draw_observer` WARP test installs the production MinHook detours through Context4
and invokes all six draw APIs through the base context. It verifies callback delivery while
the pixel shader is still bound, GPU results after forwarding nonzero draw/argument offsets,
four-vertex and indexed geometry, and command-list observation. It does not simulate the
complete RenoDX add-on or Unity's capture timing. Pending-replay diagnostics in game report
draw counts and the first recognized draw's geometry/context/capture state.

For the FSR flicker regression, move the camera with visible HUD and first-person hands in SDR,
then HDR. Check `HUD-less HDRP color` in the managed log, followed by `HUD-less frame verified`,
`HUD-less yes` and increasing `supplied` in the native log. Compare FSR with DLSS-G and XeSS;
API success and a verified capture alone do not establish that the visual flicker is fixed.
Also test FG off/on, loading, resolution/upscaler changes, and HUD capture off/on.
The FSR input line should identify `inputs Y-flipped to presentation`, with positive Y motion
scale (`+1068` for the observed 1896x1068 render size). Test vertical and horizontal camera motion,
hands/weapons and world edges. Also compare DLSS upscaling with TAA/STP to exercise both input sources.

`Test-Setup.ps1` checks expected files, the managed assembly version and the installed package
manifest hashes without loading the native DLL into PowerShell. It cannot
check runtime hook ordering or whether interpolation will work.

## Both mods, in game

Start with both mods installed and every setting as the previous session left it.

1. `BepInEx/TGGraphicsBridge/Autoload.log`: two headers for the PID, one per mod's copy of the
   preloader. The first found both extensions (each "bundles bridge 1.1.0"), loaded its own
   `TGGraphicsBridge.Native.dll`, registered both and `TGGraphicsBridge_Bootstrap returned 0`; the
   second says the bridge was already started by the first. No warning about `TGFG.Autoload.dll`
   (deployment removes 0.5.0's preloader). With only one mod installed there is one header and that
   mod's copy starts the bridge.
2. `BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log`: the game's device observed, the
   D3D12 device created inside the swapchain creation (by TG Frame Generation through Streamline on
   an NVIDIA GPU), frame generation as presentation provider.
3. BepInEx's log: `Skipping [TG Graphics Bridge 1.1.0] because a newer version exists` for one
   copy of the bridge's plugin (expected with both mods: BepInEx keeps one), `TG Graphics Bridge
   1.1.0 loaded (the copy bundled in plugins/<mod>)`, then the two mods, then a bridge status line
   naming both extensions as active.
4. Frame generation behaves exactly as in 0.5.0 (the sections below). Its rows still follow the
   Upscaling row.

## TG Better Upscaling

1. `TGBetterUpscaling.Native.log`: the FSR versions and the XeSS version the runtimes report, and
   `Upscaler interop ready` on the first frame with FSR or XeSS. With FSR: `AMD FSR upscaler <v>
   (AMD's default, the newest this GPU offers)`; FSR 4 on a Radeon RX 9000.
2. Video tab: the Upscaling row offers None, STP, DLSS, FSR, XeSS (unavailable ones skipped). STP
   shows the game's render-scale slider; DLSS and XeSS show Quality Mode; FSR shows Quality Mode and
   FSR Sharpness. No FSR Version row, in the Video tab or in FoA Mod Manager. Apply, Undo and Restore
   Defaults behave as for the game's own entries.
3. Select FSR and Apply: BepInEx logs `Upscaling with FSR <mode> at N% render scale`, the native log an
   `Upscaling 10s:` line every ten seconds with `fallback=0`, render size the mode's share of the
   output (Performance: half). Compare the image with DLSS in the same mode, standing and during
   camera turns: no shimmering edges, ghosting behind moving characters, or a smeared first-person hand.
4. Repeat with XeSS; the `preset` in the status is the chosen Quality Mode.
5. Change the Quality Mode while playing (Apply): the output size stays, the render size follows, no
   black frames. Change the resolution and the display mode: the context is rebuilt once.
6. FSR Sharpness: 0% and 100% are visibly different. The native log's `Upscaling 10s:` line shows
   `preExposure=`, HDRP's exposure between 0.2 and 2; walking from a dark interior into daylight (and
   back) shows no ghosting or smearing while the exposure adapts.
7. Restart the game with FSR selected: FSR is selected again (`restored FSR from the game's
   preferences`). Change the upscaler in FoA Mod Manager: the Video tab follows after closing it.
8. With TG Frame Generation on (each technology): frame generation still starts, its HDRP input line
   reports render-resolution depth and motion, and generated frames show no reprojection wobble.
9. With ReShade + RenoDX HDR: FSR and XeSS upscale the HDR scene (no clipped highlights, no colour
   shift against STP); with the RenoDX DLSS addon: the game still starts and the addon still works.
10. Remove TG Better Upscaling (or add `BepInEx/TGBetterUpscaling/disable-autoload.txt`) with FSR selected: the
    game falls back to its own default upscaler.
11. NVIDIA GPU: the native log has `NVIDIA DLSS 310.9.1.0 bundled in Native/DLSS (NVIDIA-signed)` at
    startup, then `NVIDIA's runtime asked for DLSS 3.8.10.0 (<game folder>\nvngx_dlss.dll); it gets the
    bundled 310.9.1.0 instead`, or `keeps DLSS ...` when a DLSS mod has put the same or a newer file in
    the game folder, or its own set (DLSS 5: `a DLSS mod's set of DLLs, with DLSS neural rendering
    310.8.0.0 beside it`). DLSS still upscales, and with DLSS 5 on, the frame rate still follows the
    quality mode (Ultra Performance clearly faster than Quality), as with the game's own DLSS DLL.
12. Quality modes: `AMD FSR quality modes: Native AA 1.00x, ... Ultra Performance 3.00x` and `Intel XeSS
    upscaling available; quality presets: ...` in the native log, and BepInEx's `quality modes DLSS: ...;
    FSR: ...; XeSS: ...` status. The Video tab shows the slider under STP, Quality Mode under DLSS, FSR
    (with FSR Sharpness) and XeSS, and FoA Mod Manager the same rows. On the first start with this
    version, BepInEx logs `Quality modes set from the game's N% render scale, rounded down`. Picking a
    graphics preset moves the modes with the slider. With TG Frame Generation installed, its log's
    `Observed D3D11 DLSS SR feature` line shows `quality=` the chosen mode's NGX level (2 Quality, 1
    Balanced, 0 Performance, 3 Ultra Performance, 5 DLAA) and `preset hints` all 0 (the DLL's default).

## TG Path Tracing (Mono)

Offline first: `ctest --test-dir build/native -R tgpt_gpu_test` must pass and its PNGs in
`build/native/tgpt-images` look right (`1-path-tracing.png` lights the room's shadowed ceiling and shows the
box's and the checkered leaf's sun shadows and the warm point light). Its `noise`
lines measure the built-in denoiser against an undenoised converged frame (48 frames of 8 rays a pixel
without the denoiser, averaged, the camera still), still, jittered and moving, by day and at night with 24
torches and a light over the checkered leaf, on flat and on normal-mapped walls; a denoised reference would
blur like the denoiser and hide it. `17-full-still.png` against `18-full-still-reference.png` shows the sun's
sharp shadows (the floor's error must stay under 6%), `19`/`20` the night, and `21` to `26` the night's direct
and bounced light apart, with and without ReSTIR's spatial reuse (`night floor` lines: the mean ratio says
whether light is lost). Compare them before and after a change to the tracer or the denoiser.

1. Start: BepInEx logs `TG Path Tracing 1.2.0 loaded`, `Hooked HDRP's RenderSSR ...` and `Path tracing
   is available on this GPU`; `BepInEx/TGPathTracing/Native/TGPathTracing.Native.log` has `Shared D3D12
   device: DirectX Raytracing tier ...` and, on the first gameplay frame, `Path tracing ready` and
   `Render size WxH`. The bridge log registers the extension beside the others.
2. In gameplay (Path Tracing Full, the default): every 10 s BepInEx logs `Path tracing hook 10s:`
   with `recorded`, `executed` and `handed to the native side` equal and non-zero, and `Path tracing:`
   with the scene (objects, Drake, terrain, vegetation counts, meshes to register falling to 0) and
   lights; the native log's `Path tracing 10s:` has `composited` close to `frames`, `gpuWaits=0`, the
   GPU time and the render-thread split. Shadowed areas and interiors get coloured bounce light;
   HDRP's baked indirect light is gone (`Replace HDRP indirect light`). An earlier version's config with
   `Mode = Global Illumination` comes up as Full, and its `Indirect intensity` line is gone.
3. Debug views (Shift+Insert): Normals and Albedo line up with the scene (not upside down or shifted);
   Coverage colours most buildings, rocks, trees and terrain, dark where only HDRP draws (characters,
   grass); Materials shows textures where the objects are; Indirect is smooth after a second of
   standing still and follows the camera without trails.
4. Sun shadows are traced (trees included), torches and lamps light their surroundings with traced
   shadows, reflections on wet or metallic surfaces come from the traced scene. Insert turns path tracing
   off and on: compare with Off at the same spot for brightness, the same order of magnitude, no black or
   white surfaces.
5. Walk and turn quickly: no smearing of bounce light behind the camera's motion beyond a frame, no
   flicker along silhouettes worse than a few pixels. Fast travel or load a save: the scene rebuilds
   (meshes to register rises, then falls) without errors.
6. Performance: note the frame rate with Off and Full, and the native log's GPU
   time; Bounces 1 and Spatial Samples 2 should cost clearly less.
7. With TG Frame Generation and TG Better Upscaling on: all three still work; frame generation's
   pipeline line unchanged.
8. DirectX Raytracing 1.2 (GeForce RTX 40/50 with a current driver): the bridge log's `Direct3D 12:`
   line names Agility SDK 619 from `BepInEx\TGPathTracing\Native\D3D12\` (`the game's request now
   names it`); the native log's `Shared D3D12 device:` line says `DirectX Raytracing 1.2 (opacity
   micromaps) available; Shader Execution Reordering reorders`, and `Path tracing ready:` says `Shader
   Model 6.9 passes (opacity micromaps, Shader Execution Reordering)`. After a minute in a forest, the
   10 s line's `micromaps:` has meshes above 0 and `waiting` near 0; the overlay's path tracing line
   says `DirectX Raytracing 1.2 with Shader Execution Reordering, opacity micromaps on N meshes`. Leaf
   shadows look as before. Compare the GPU time with a GPU or driver without DXR 1.2, if one is at hand.
Offline, the mods together: `ctest --test-dir build/native -R tgup_gpu_smoke` runs upscaling alone, with TG
Path Tracing (`tgup_gpu_smoke_with_pt`, Ray Reconstruction), and installed as in BepInEx with frame generation
(`tgup_gpu_smoke_fg_installed`) and with all three (`tgup_gpu_smoke_all_mods`). Frame generation's Streamline
only starts beside a Unity player: copy the game's `UnityPlayer.dll` next to `build/native/tgup_gpu_smoke.exe`,
or those two runs cover frame generation's FSR and XeSS paths only (the test prints whether Streamline ran).

10. RenoDX DLSS 5, at each DirectNeuralRenderingHookStage (0 Render, 1 Upscaled, 2 Present in ReShade.ini
   [RENODX-DLSS]), with all three mods, DLSS selected and Denoising on Ray Reconstruction: DLSS 5's effect and
   Ray Reconstruction both show, the overlay says `DLSS Ray Reconstruction`, and at no stage does a glow appear
   over the image or the menus (compare with Path Tracing Off; at Present the add-on processes menus too).
   Without DLSS 5, turning path tracing on logs `NGX starts for it when TG Path Tracing first asks` once and
   then `DLSS Ray Reconstruction created ... nvngx_dlssd.dll loaded from ...` (one module, TG Better
   Upscaling's). A refused frame (`Upscaling refused a frame`) gives TG Path Tracing its own denoiser back
   for half a second, without trails.

9. Denoisers, with Denoising never picked (it starts on the best). NVIDIA with TG Better Upscaling and DLSS selected: the
   overlay says `DLSS Ray Reconstruction`, TG Better Upscaling's line says it upscales with Ray
   Reconstruction, and the lighting is clean without the built-in denoiser's blur; Built-in switches
   back. A Radeon RX 9000: the native log has `AMD Ray Regeneration available on this GPU` and
   `AMD Ray Regeneration context for WxH`, the overlay says `AMD Ray Regeneration`; any `AMD Ray
   Regeneration failed` or `could not start` line means the built-in denoiser took over. Other GPUs log
   that Ray Regeneration needs an RX 9000.
11. The Video settings: a Path Tracing row after the upscaling and frame generation rows, with its options nested
   under it only while it is on (the row offers Off and Full); FoA Mod Manager
   shows exactly the same rows in the same order, and a change on either side shows on the other after the
   settings close. Denoising lists None, Built-in, Ray Regeneration (RX 9000 only) and Ray Reconstruction (NVIDIA
   with TG Better Upscaling), and starts on the best of them; Restore Defaults picks it again. Ray Reconstruction
   with FSR or XeSS upscaling: its tooltip says to select DLSS, the overlay says built-in denoiser. With DLSS 5
   loaded, Denoising still starts on Ray Reconstruction and it runs. Indirect Color at 3 visibly tints surfaces
   next to strongly colored ones (the Indirect debug view shows it), at 0 the bounced light is grey, and neither
   changes its brightness. An earlier version's config
   (`Denoiser = Auto`) becomes Denoising on the best; `Denoiser = Built-in` stays Built-in. With FoA Mod Manager
   open in game, frame generation's pipeline line shows it suspended (0x2) without fgOn bursts.
12. Characters: people sitting on a bench cast shadows onto it and the floor, their arms shade their
   bodies, and the floor under them is darker than around them; hair casts no card-shaped shadows. BepInEx logs
   `Characters: their traced vertices sit where the game draws them` once, a `Characters: shader ...` line per
   character shader (traced, traced solid, or left out), the 10 s path tracing line has `characters N renderers`,
   and the native log's 10 s line `characters=N (Mk triangles)`. `A character is left out` warnings name what
   failed. Arches, walls and floors near the camera show no black patches in their shape, standing still or
   walking up to them; covered stairwells and rooms stay dark. The scene line counts shadow-only and other-layer
   Drake entities, and `Traced though the camera does not draw it` lines name such objects.
   Straw on a floor shows no black blocks under it; a rope net casts its pattern on the wall behind it. Reflection
   Boost at 1 turns floors and walls into near-mirrors of the room; 0 looks as before.

## Performance overlay

1. Performance Overlay is the last row of the Video settings, once, with both mods installed as well.
   On shows Overlay Position and Overlay Size below it; Apply shows the panel in that corner, Off and
   Apply hides it; Undo and Restore Defaults behave as for the game's rows. FoA Mod Manager shows the
   same three rows under TG Graphics Bridge, position and size only while it is on, and a change there
   shows up in the Video row after closing it.
2. Performance: the rendered frame rate agrees with the game's own counter or RTSS; the resolution
   line shows the render size under DLSS, STP, FSR and XeSS; VSync and the frame cap follow the game's
   settings; CPU and GPU use are within a few points of Task Manager; VRAM matches Task Manager's
   dedicated GPU memory for the game.
3. Upscaling: each upscaler's version and mode; DLSS names its preset and the DLL (`bundled` when the
   game folder's is older). With a DLSS override in the NVIDIA App (for example preset L), the preset
   reads `L (NVIDIA driver override)`, matching the NVIDIA App overlay's `SR OVR Preset L`, and the native
   log has `NVIDIA driver settings for Fall of Avalon.exe: DLSS-SR preset override 12`.
4. Frame generation: Fixed 2x shows about twice the rendered frame rate on screen, 3x/4x likewise with
   DLSS-G; Dynamic shows the target; the menu shows Paused.
5. With only one of the two mods installed, only its section and the Performance section show.

## Troubleshooting report, problems and the framerate check

Without the game, the install check runs on folders laid out the ways players unpack the mods:

```powershell
dotnet build tests/InstallCheck.Tests.csproj -c Release
./tests/bin/Release/net472/InstallCheck.Tests.exe   # TG_VERBOSE=1 prints what the player reads
```

In game (none of this has been run yet):

1. Create Troubleshooting Report is nested under Performance Overlay: alone while the overlay is off,
   after its position and size rows while it is on. Selecting it closes the settings, with the game's question about unapplied
   changes first if there are any; a message at the top of the screen says to go back to the game.
2. In gameplay the steps begin: the message names each one. The upscaling row switches (Off, STP,
   DLSS, FSR, XeSS as offered) with frame generation off, then frame generation goes through its
   technologies and low latency choices. Escape pauses it, the message says the settings are back,
   and Options > Video shows the player's own upscaler and frame generation. Back in the game, the
   step starts over. Selecting the button during the test stops it and saves what was measured.
3. At the end the player's upscaler and frame generation are back (Video tab, performance overlay,
   both configs unchanged), and a message names the report in the BepInEx folder
   (`TG Troubleshooting Report <date>.txt`, beside LogOutput.log). It is one text file of about 25 KB
   with a line per step, then a section per log with this session's lines only, the config values
   and "Where the mods' files are", with `%USERPROFILE%` in place of the Windows user folder. In the
   TGFG.Native.log section, each step's `Note: Troubleshooting: done:` line is followed by a Pipeline
   line covering that step alone.
4. Close the game during a test (Alt+F4 in a step with FSR or XeSS upscaling): the next start logs
   that the test didn't finish and the Upscaling row shows the player's choice again.
5. Install problems: move the mod's BepInEx folder into BepInEx/BepInEx (or BepInEx/plugins with the
   other mod installed correctly): no rows named after the mods appear. For TG Frame Generation, the
   Frame Generation row reads Off, greyed out, with an amber alert icon after its name; its tooltip reads
   "TG Frame Generation: Wrong folder." and says in a sentence or two where the files are and what to do
   (with Vortex: Engine Injector). For TG Better Upscaling, the Dynamic Resolution row gets the icon and
   the same kind of tooltip. With old files for the other game version in the right place, the tooltip
   says to delete BepInEx/plugins/<mod> "(for the game's normal version)", never "IL2CPP". The report has
   the full explanation with the files to delete. With the same mod also installed correctly and the
   same version running from there, the tooltip reads "Extra copy" instead. The performance overlay
   lists it under Problems as "TG Frame Generation: Wrong folder", no wider than half the screen. The
   icon disappears from a row once its problem is gone, and never stays on another row when the
   settings screen scrolls or switches tabs.
6. Put an empty OptiScaler.ini beside Fall of Avalon.exe: both the Frame Generation and the Dynamic
   Resolution row get the icon, with "OptiScaler: Leftover files." as the tooltip. With RivaTuner's frame
   rate limit set for the game, the Frame Generation row's tooltip names the limit ("RivaTuner: 60 fps
   limit."). With the NVIDIA App's Max Frame Rate set for the game, the same after about ten seconds.
   No tooltip is longer than about three lines; with more than one problem it says how many more the
   performance overlay lists. Remove TG Frame Generation's plugin DLL (keep its preloader): a stand-in
   Frame Generation row reads Off with the icon and the reason.
   Turn Smooth Motion on for the game in the NVIDIA App to check the Smooth Motion ID (the native log's
   `NVIDIA driver settings` line should show `smoothMotion=0x1@game`); if it stays `unset`, the ID is wrong.
7. The framerate check: play a few seconds with frame generation off, then turn it on with the game's
   Framerate Limit at the display's refresh rate and VSync on. About eight seconds into play a message
   says frame generation isn't raising the framerate, with the numbers and the VSync explanation, and a
   "No FPS gain" alert appears on the Frame Generation row. Turning frame generation off clears it; with an
   uncapped framerate and VSync off, the log says it raised the framerate from X to Y.

## Frame generation pipeline changes in 1.2.0

In game (none of this has been run yet). Compare against 1.1.0 where a number is asked for.

1. D3D11 no longer waits for frame generation. With DLSS, FSR and XeSS at 2x, uncapped and VSync
   off, note the performance overlay's rendered and displayed framerate in the same spot as with
   1.1.0. Both should be the same or higher; the GPU-bound case gains most. The Pipeline line's
   `d3d11Waits buffer/inputs=` appears only when D3D11 had to wait and should stay small next to
   `presents=`. Watch for corruption: flickering depth or motion (FSR debug flag 4 shows them), a
   HUD-less image from the wrong frame, or a torn or stale frame. Any of those means a resource was
   overwritten while D3D12 still read it.
2. The frame start: BepInEx's log says once that "Frames begin in Unity's PlayerLoop after
   WaitForLastPresentationAndUpdateTime".
   With Reflex on, the Pipeline line's `pcLatency` should be the same or lower than in 1.1.0 at the
   same framerate, and mouse look should feel no worse. The IL2CPP build uses Il2CppInterop's
   PlayerLoop types, which only a run in game proves.
3. DLSS-G UI recomposition is off: the HUD under DLSS Frame Generation (compass, crosshair, prompts
   while turning) should look no worse than in 1.1.0. If it does, the next step is a UI alpha buffer
   derived from the HUD-less image, not turning recomposition back on without one.
4. DLSS-G's minimum: set the game's resolution very low in windowed mode (the minimum is in the
   native log's facts as `dlssg.minimum`). Frame generation reads "resolution too low" and resumes
   after raising the resolution again, without a restart.
5. XeSS's Base FPS limit: set `Base FPS limit` to 30 in TGFrameGeneration's config with XeSS
   Frame Generation at 2x. The overlay shows about 30 rendered and 60 displayed (it was 15 and 30).
6. The native DLLs carry their C++ runtime: `dumpbin /dependents` on the three `*.Native.dll`
   lists no MSVCP140, VCRUNTIME140 or api-ms-win-crt DLL. The report's System section has a
   "Visual C++ runtime:" line with the System32 version.
7. VSync on another GPU's screen: on a laptop in its hybrid mode (or with the monitor in the
   motherboard's port), choose DLSS Frame Generation with VSync on. After the settings close, VSync
   reads Off and is greyed out with a tooltip naming the integrated GPU, a message says VSync was turned
   off, Target FPS reads Unlimited, and the log says so. Choosing a limit in Target FPS afterwards keeps it. Choosing FSR
   offers the toggle again with VSync still off. On a desktop with the monitor on the NVIDIA card, nothing
   changes. Moving the window to a monitor on the other GPU is picked up within about 5 s. The Pipeline
   line shows `crossAdapterCopy=` on the laptop.
8. Memory rows: with frame generation on at a texture quality that overfills video memory, a "VRAM full"
   row appears after about 6 s; with system memory filled by another program, "RAM full".

## Off-mode output bridge

Start normally through Steam with the integrated preloader installed; keep generation Off. The game report
must remain Direct3D11. The native report must show source DX11, presentation
DX12, an active bridge, source-present/copy counts increasing, and no failing
interop HRESULT. Check the image while moving, resizing and reopening settings.
The same counters may increase while FG is disabled; they are not FG proof.

The current source adapter exposes only stable logical buffer zero. A client
request for another source buffer will be logged and rejected. Do not work around
that by returning the D3D12 output texture to a D3D11 caller.

A source API report of DX11 with native source/output 0/0 and no bridge flag means
bootstrap or bridge creation has not succeeded. A loaded nvngx_dlssg.dll alone is
not proof that TGFG owns a working frame-generation path.

## Fixed 2×

Use ordinary in-game DLSS SR and a scene with a moving camera. Select Fixed 2×,
then close Video settings. The configured menu/focus/cursor guards suspend FG
while applicable. Verify that native NGX calls and successful captured input pairs
increase, the requested mode is accepted, and the DLSS-G status is clear. Compare
NVIDIA-reported output deltas with an independent presentation measurement and
actual visual output, not just the game's rendered FPS counter.

Look specifically for incorrect depth edges, reversed movement, lagging history,
flashing frames, sky/UI smearing, camera-cut trails, loading transition errors and
latency. Any of those can invalidate an otherwise successful API call.

## MFG, targets and transitions

Only after 2× has been verified, test higher fixed counts within the reported cap,
then Dynamic support. A 236 FPS fixed output target at 4× requests a 59 real-FPS
Reflex cap; that is not a guaranteed output rate. Dynamic target 0 delegates the
refresh-rate target to NVIDIA. VSync can govern output and may supersede that
target. The prototype does not change the desktop refresh rate.

Exercise mode changes, gameplay/menu transitions, focus loss, save/load,
resolution changes and fullscreen changes separately. Off retains the bridge's
copy/output overhead. Fatal native errors require restarting the process.

## HDR and RenoDX

With the intended RenoDX stack, inspect the source/output formats. FP16 source
should remain format 10 to DX11 while the private output uses format 24 and
color space 12. Check reference whites, highlights, blacks, gradients and saturated
colors. This validates more than merely having an HDR flag. Source scRGB must use
80 nits per linear unit. HDR-to-SDR fallback/tonemapping is not implemented.

Capture hook ordering and actual colors with the current addons are unverified.
If needed for diagnosis, compare an isolated bridge test with ordinary game DLSS
to the full RenoDX stack, without treating the isolated result as proof of
combined compatibility. Keep backups of addon files; no test script replaces them.

## Report collection

The Video tab's Create Troubleshooting Report puts this session's part of all of the following in one
text file in the BepInEx folder, with a test run. The full files, by hand: collect the new in-game diagnostic (each mod's `Write diagnostic report now`), BepInEx/LogOutput.log,
BepInEx/TGGraphicsBridge/Autoload.log, BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log,
and the native and Streamline logs under BepInEx/TGFrameGeneration/Native and
BepInEx/TGBetterUpscaling/Native in the game installation. Include whether frame generation was Off, Fixed
2× or Dynamic, which upscaler ran, and whether the capture was in gameplay or a menu. Compare managed
and native PIDs with the current session in Autoload.log.

FSR in HDR: the log prints the presentation colour space with the display's luminance range, and
any HDR metadata the game sets. FSR converts the presented frame back to linear light using the
maximum of those, so a wrong or zero maximum puts every comparison FSR makes on the wrong scale.
DLSS-G and XeSS are never given that range, so it is the one colour input only FSR depends on.

FSR: the native log prints "AMD FSR inputs:" when generation starts and whenever the render
size or the HUD-less state changes. The advanced setting "FSR debug flags" (sum of 1 tear lines,
2 reset indicators, 4 debug view, 8 pacing lines, 16 input checker) draws FSR's own overlays on
generated frames; with 4 only generated frames are shown, with motion vectors, depth and masks,
so a screenshot shows whether the inputs line up. Reset indicators flashing every frame mean the
reset flag or the frame ID is wrong.

Add debug flag 32 for a single three-frame source-input capture. After FSR and HUD separation
are active, move the camera with hands visible. The native log reports the saved directory under
`BepInEx/TGFrameGeneration/diagnostics/fsr-inputs-*`. It contains scene images, raw depth/motion
and JSON dimensions, formats, motion scales and jitter. All three GPU copies are queued before
readback; saving may briefly hitch. The files contain source frames, not interpolated frames.
Disable bit 32 after collecting evidence; toggling it off/on or restarting rearms it. Allocation
is limited to 384 MiB. The WARP input-capture test checks frame identity, row-pitch removal and
the one-shot lifecycle independently of the game.

For a specific scene, leave bit 32 off and assign the advanced `FSR input capture key` (default
None). Press it while turning with hands visible. It rearms the same capture after 0.3 seconds;
each subsequent press captures another set. This avoids spending the capture on camera bob
or a transition before the hands appear.

Bridge flags: 1 active, 2 shared-texture path created, 4 shared-fence path created,
8 scRGB-to-HDR10 output path, 16 HUD-less image supplied with the last frame, 32 HUD-less
captures verified against the presented frames. The "Pipeline 5s" line's "hudless" group
shows captures, rejections, supplied frames and the last differing fraction: a few percent
means the HUD alone differs; 100% means something redraws the frame at Present. Output counter is NVIDIA-reported, not independently measured. No counters
or flags are hardcoded to pretend frame generation succeeded.

## Rollback

Exit and create an empty `disable-autoload.txt` in BepInEx/TGFrameGeneration or BepInEx/TGBetterUpscaling
to keep that mod's native DLL from loading, or in BepInEx/TGGraphicsBridge to load nothing native at
all (or remove BepInEx/patchers/TGFrameGeneration.Autoload.dll and TGBetterUpscaling.Autoload.dll). Then start normally through Steam.
Removing only a gameplay plugin removes its rows but does not stop its native DLL from loading. Every
file a deployment replaced is under BepInEx/<mod>/Backups. No proxy DLL or system setting was
overwritten.

Check game-local BepInEx/TGGraphicsBridge/Autoload.log first; confirm the current PID.
