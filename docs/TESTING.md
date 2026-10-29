# Local test sequence

This is a test plan, not a list of passed game tests. Use a normal single-player
session and close the game before rebuilding. Do not test multiple new renderer
changes simultaneously or infer success from the in-game FPS counter alone.

## Build boundary

TG Neural Rendering: [build evidence and pending game checks](DLSS5-IMPLEMENTATION.md), plus the
[A/B/C and placement tuning run](DLSS5-TUNING.md). Its GPU probe
actually evaluates the supplied signed model; the existing PT offline test does not. `neural_plan.py` creates NR
scene plans for the same in-game harness. NR acceptance requires real evaluation counts, neutral/Off restoration,
moving scenes, unchanged HUD, shared FG/RR operation and the owner's appearance verdict. NR deployment/testing is
authorized while FoA and Starfield are closed; preserve the parallel PT build and respect the shared harness lock.
Menu acceptance for NR-20/21: Video and FoA order is Neural Rendering, Placement, Strength, Local Tone,
Advanced, then (Advanced On) Model, Passes, Detail, Automatic Mask, and (mask On) Skin Detail. Toggling Advanced
only changes visibility; values and model history stay. Confirm Apply/Cancel/Restore Defaults also reach
hidden controls, and RR still restricts placement with its explanatory tooltip. Both versions compile;
menu update deployed10-08 10:31; Passes1-4 deployed11:22 and tavern7 takes pass, real-game UI acceptance pending.

Repeated-pass correctness: in a VS x64 shell, run `python tests/neural_multipass_mutations.py
build/nr/probe/BepInEx build/nr/tgnr_gpu_smoke.exe build/nr/multipass-check` with both games closed and the
shared harness free. It builds an isolated adapter copy with a test-only raw-output readback export, then
checks 1-4 passes at Strength0.5/1 against separate single-pass evaluations, exact neutral SDR/HDR, alpha,
Off, invocation counts and history retention. Three broken chains are rejected. The export is not shipped.
The oracle must feed exact encoded pixels: decode/encode rounding below1e-6 changes this model's output
by up to0.012 linear RGB in the fixture; raw input agrees exactly. Native diagnostics record pass count,
the analyser verifies it against the plan, and existing comparison recipes explicitly set Passes1.

For temporal colour diagnosis, generate a plan with `python tools/harness/neural_plan.py <new-plan.json> --stages`,
then pass it to `Run-PtHarness.ps1 -Plan <new-plan.json> -Out <new-run> -KeepFrames`. This captures the tavern and
face close-up at Off, neutral and A/B, both placements, with 48 moving frames and a 96-frame stationary hold.
`python tools/harness/neural_stages.py <new-run>` matches native records to the exact screen frame and writes
`nr-stages.md`, JSON curves and plots. Seven 64x36 float grids average every pixel: scene input, encoded model
input/output, restored scene output, absolute motion, depth and signed motion in model pixels. The same GPU invocation
also reads HDRP's exposure texture. Model RGB is reported in 0–255 levels; scene RGB remains linear HDR.
The records include resets, tuning, scales and jitter. Missing exposure, mismatched frame IDs, invalid pixels or
camera movement during the hold invalidate the evidence. These synchronous readbacks are off in normal play and
invalidate performance measurements; the harness turns them off and restores settings when it ends.

Use `--replay` instead of `--stages` to compare Model A with ordinary inputs, retained inputs and retained inputs
with one history reset, at both placements in both scenes. After camera movement ends, the diagnostic retains
one colour/depth pair and clears its motion texture to zero. This is a controlled diagnostic, never a player mode.
The analyser requires constant held colour/depth grids, zero motion, the requested source frame and exactly the
requested reset. Live scene input continues changing; only the model output isolates NR's response to identical
inputs. Format v2 adds depth and v3 signed motion; the reader accepts all three versions.
Add `--long-hold` to `--replay` for a four-take tavern plan: Off, 600 held frames with one reset at each
placement, then Off again. This measures longer settling without treating a short tail as convergence.
Add `--reset-every-frame` to `--replay` instead for six tavern takes: Off, one reset versus a reset on every
retained frame at both placements, then Off. This diagnostic separates accumulated history from fresh
evaluations; it is never a player workaround. The analyser verifies each requested reset and the diagnostic
mode in the native CSV. Its first game run failed readiness before the comparison completed; no result yet.

Use `--signal` for full-strength A with diagnostic input multipliers 0.25/1/4 at both placements and both
scenes (48 moving, 128 held frames). The multiplier is inverted on output and is never a player setting.
Actual per-frame input conditioning must match the plan. The GPU probe compares the encoding to independent
CPU math and exercises the inverse at nonzero near-neutral strength; omitting the inverse must fail.

Use `--model-history` for A/B/C at both placements with 192 retained frames; add `--auto-mask` to repeat
with Automatic Mask On and full Skin Detail. `neural_history.py <run>` writes local late-range maps and worst-cell
curves. `--live-models` compares A/C with normal changing inputs in face/tavern/forest, including Off bookends.
Stage analysis verifies actual native controls against the plan on every frame, not only requested config values.

Use `--motion-check` and launch with `-KeepFrames` for neutral NR and sharp images at both placements.
`neural_motion.py <run>` uses OpenCV (opencv-python) to track image features independently, checking forward/backward
tracking consistency before comparing signed NR vectors in capture pixels. It reports sign/scale alternatives,
not a universal pass threshold. `tests/neural_motion_direction_test.py` checks known translated images and rejects
wrong signs/scales. This tests geometry movement; it cannot establish that NR's temporal colour is correct.

`python tests/neural_stages_test.py` checks seventeen cases and rejects sixteen deliberate analyser faults,
including otherwise-valid frames captured outside gameplay. The runner rechecks readiness before freezing,
before each take and each captured frame; leaving gameplay fails that scenario and stops its later takes.
Failures include loading/player/UI-interactivity/camera state. `neural_temporal.py <run> [--scene <key>]`
requires retained raw screen frames and reports NR-minus-Off colour changes on tracked moving features,
alongside a repeated Off control. It reuses the independent image tracker; a two-pixel blur limits pixel
resampling noise in the measurement. These local frame pairs are not persistent object identities and can
include intentional view-dependent changes. `python tests/neural_temporal_test.py`: three tests, two faults.
The native probe accepts `--diagnostics <new-output-directory>`; `python tests/neural_stage_probe.py <directory>`
checks known colours, encoding, orientation, neutral output, signed/scaled motion and same-frame exposure.
Add `--mutations` to reject four corrupted signed-motion readbacks while preserving the real captures. A deliberately
inverted readback shader fails the independent CPU reference. Generic RR motion is preserved; the Unity SR
no-motion-marker correction is checked in the real game against the earlier failing capture:
`python tests/neural_motion_capture_test.py <before-run> <after-run>`. It requires the two frozen scenes,
constant exposure and held motion below 0.01 model pixel, and must reject the pre-fix capture. It does not
claim that correcting motion fixes colour instability or that different camera routes are valid image A/Bs.

At Strength 0, NR must still evaluate for its history but leave the scene pixels untouched. The native GPU
probe requires bit-identical HDR pixels and verifies that raising strength afterward does not reset history.
`python tests/neural_neutral_capture_test.py <pre-fix-stage-run> <fixed-neutral-run>` requires exact scene
input/output grids at both placements in game, and must reject the older conversion round trip.

`python tests/neural_hdr_capture_test.py <old-signal-run> <fixed-signal-run>` rejects NR-17's inverse-peak
glare and requires the HDR gain bound at all 12 full-strength scene/placement/input-scale combinations.
It also requires nonzero model and scene effects, actual current-frame evaluation and matching camera routes.
The bound uses same-frame linear HDR grids, not display screenshots; see round-09 for its derivation.

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

TG Path Tracing's light rules (which light the names leave out stands at a glowing thing and is
kept, and the size each light is traced at), without the game:

```powershell
dotnet build tests/LightRules.Tests.csproj -c Release
& ./tests/bin/Release/net472/LightRules.Tests.exe
```

Its cloth rules (which of the game's materials are thin cloth: the census of the harness's scenes, PT-49), the same way:

```powershell
dotnet build tests/ClothRules.Tests.csproj -c Release
& ./tests/bin/Release/net472/ClothRules.Tests.exe
```

Its world rules (what of the world is traced: the level of detail the game draws, by each of its renderers' own rule, and
statically batched renderers in their combined mesh; PT-63, PT-61), the same way:

```powershell
dotnet build tests/WorldRules.Tests.csproj -c Release
& ./tests/bin/Release/net472/WorldRules.Tests.exe
```

Its rules for the things the game moves (what counts as a move, which entities are placed every frame, a thing's identity
and its previous transform; PT-71 item 4), the same way:

```powershell
dotnet build tests/MovingRules.Tests.csproj -c Release
& ./tests/bin/Release/net472/MovingRules.Tests.exe
```

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
   Defaults behave as for the game's own entries. On game builds from 2026-10-06 (the game's own FSR 2
   entry): still one FSR entry, named "FSR 3.1" or "FSR 4"; BepInEx logs `this game build has an FSR
   entry of its own (FSR 2), which this mod's FSR takes the place of` and `HDRP's upscaler order: ...`.
   Where AMD's runtime reports no FSR to the mod, the entry keeps the game's name and slider and
   BepInEx logs `The game sets up its own FSR 2`.
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

In the real game, by an agent: `.\tools\harness\Run-PtHarness.ps1` (`docs/PT-HARNESS.md`). It loads the owner's saves
by name and compares the game's own picture, the traced one with Ray Reconstruction and with NRD, and the game's again
after tracing was turned off, frozen and with the world running, with frame rates. It only starts the game when no
other program uses the GPU. Its analysis has checks of its own: `python tests/harness_analyze_test.py`; so has
the script that frees its disk space: `powershell -File tests\harness_cleanup_test.ps1`.

Offline first: `ctest --test-dir build/native -R tgpt_gpu_test` must pass and its PNGs in
`build/native/tgpt-images` look right (`1-path-tracing.png` lights the room's shadowed ceiling and shows the
box's and the checkered leaf's sun shadows and the warm point light). Its `noise`
lines measure the built-in denoiser against an undenoised converged frame (48 frames of 8 rays a pixel
without the denoiser, averaged, the camera still), still, jittered and moving, by day and at night with 24
torches and a light over the checkered leaf, on flat and on normal-mapped walls; a denoised reference would
blur like the denoiser and hide it. `17-full-still.png` against `18-full-still-reference.png` shows the sun's
sharp shadows (the floor's error must stay under 6%), `19`/`20` the night, and `21` to `26` the night's direct
and bounced light apart, with and without ReSTIR's spatial reuse (`night floor` lines: the mean ratio says
whether light is lost). Compare them before and after a change to the tracer or the denoiser. The `NRD:` lines
run four of those cases with NVIDIA NRD (RELAX) on the bounced light and compare its error and flicker with the
built-in denoiser's (each within half again of it, the bounced light alone within a quarter). The `panel metallic
on its left half` lines trace a panel whose mask map is metallic on one half against the same panel plain and with
that half black (one ray a pixel, the same frames, none of the estimator's own changes): the metallic half bounces and
shows in a mirror floor's reflection as the plain panel's does (a path vertex sends a metal's reflected light on as
diffuse light of its base color; with its diffuse color alone it was black), the other half is as before (`35` to `38`
show them); the
`mask map's finish` line reads the roughness, albedo and reflections of the box drawn forward in a mask map's finish,
and one never baked. The `glass cube's
shadow` lines give, of the direct light an opaque cube stops on the floor, the part a glass one lets through, channel
by channel, from the sun and from a lamp (green glass its color 0.15 0.6 0.15, grey 0.36, clear 1;
`35-green-glass-shadow.png`), the `bounced light under the ceiling` line the same tint at path vertices under a sheet
of green glass, and `guides on see-through glass` Ray Reconstruction's guides there (no diffuse albedo, a specular
albedo that covers what the glass lets through, roughness 0.03, the distance to what is seen behind it). `a lamp
reflected in see-through glass` compares a lamp's mirror image on a glass cube with its radiance by the Fresnel term,
worked out on the CPU (`36-glass-reflects-a-lamp.png`), and `the sun reflected in see-through glass` adds up the sun's
mirror image on a turned pane (reflected light times each pixel's solid angle) against the sun's illuminance by the
Fresnel term (`37-glass-reflects-the-sun.png`). Water is a surface of the traced scene (PT-71 item
5) and has a fixture of its own, `tests/water_surface_gpu.inl`: `TGPT_TEST_WATER_SURFACE=1` runs it alone at 128 x 72
with no denoiser and ends the test (`WATER` lines, `water-*.png`; `TGPT_TEST_WATER_CHECK=<a check's name>` judges that
one alone). Its optics against physics: a glowing bed through still water (Fresnel over the index squared), no bright
dots, absorption over each ray's own way, the camera under water, total internal reflection, the same water in a mirror,
the sky mirrored at a low angle, the waves' normals, a wavy pool's mean, waves in a mirror, the nearer of water and land
at a sloping shore. And the surface itself, which is triangles in the acceleration structure: what the tracer reports
under the screen's centre against the wave data along the same ray, near the camera (to 2 mm) and at 30 to 180 m across
a lake 512 m wide (within what a lattice edge of 1/32 of the distance allows), and the sun over the water's open edge
onto a dry floor, three pictures of the same pixels: an opaque liquid with the Caustics row on, whose shadow must have
the wave's outline (a shadow ray meets the same waves); clear water with the row off, which must cast no shadow (the
row's "off" is the straight light); and clear water with the row on, where the sun arrives bent through the wave: its
line (`WATER wave shadow, clear water, the row on (NOT JUDGED, no reference)`) is printed and not judged, because
nothing yet says where that light should land beyond an open edge. `TGPT_TEST_WATER_COST=1` with it:
the cost run alone at 1280 x 720 (`WATER COST` lines: each stage's time for that lake dry and wet). With `TGPT_ENERGY=1` set, the test also measures the light
through ReSTIR's reuse against no reuse at all, undenoised and averaged over hundreds of frames (`ENERGY` lines:
1 means no light lost or gained), for 24 torches and for one light 10 cm from a wall, and saves the averages as
`energy-*.png`. The `glowing quad` lines test emissive surfaces as lights (the Emissive Lights row) in the dark room
with a glowing quad in it and nothing else lit: the same light with the row on and off (the floor's within noise; off a
white wall, less the 1 % its rays' offset from the wall explains), a frame's error around its foot with ReSTIR's reuse,
with its temporal reuse alone and denoised, its reflection in a glossy floor counted once, its direct light pixel by
pixel against a Lambertian square's (mirrored too), its back lighting nothing, a light 2 cm under it that keeps it from
adding light of its own, and what the Emissive debug view makes of five glowing quads (`39-view-emissive.png`).

The radiance cache has a furnace of its own: a closed box whose walls all glow (0.5) and reflect half the light that
reaches them, so their radiance is exactly 1. The `furnace` lines give the paths alone (0.75 with one bounce, 0.97 with
four), one and two bounces ending on the cache (1, within 4%), and the reflections of the walls, twice as bright with the
cache as without (the reflected wall's glow and the light it reflects). The `radiance cache` lines compare one and four
bounces under the room's ceiling with and without it, and check that a camera cut keeps its light while a new start
empties it (every case in the harness starts with an empty cache: its first frame runs without it). Checks that sum
every pixel an object darkens (the characters' shadows through cut-outs and glass) run without the cache, since a green
cube with holes traps light in a way that grows with every bounce followed.

The `under a canvas close over sunlit sand` line measures ReSTIR's bounced light, undenoised, on sand under a canvas 40 cm
over it against a boat's hull, with a 12 cm slit at its front (the canvas is traced only, so the camera sees the sand
under it), against the plain path tracer: bands at the front edge, the middle and by the hull, with the Crevice Light
row and without. With the row the front edge must be within 5 %, the middle from 8 % short to 5 % over and the sand
by the hull from 15 % short to 5 % over; `crevices-light-indirect.png` shows the bounced light there. With `TGPT_CREVICES=1` set it runs the study behind it
(`CREVICES` lines, a few minutes): the honest reference at 1 to 4 bounces in stops below the sunlit sand, each of
ReSTIR's changes undone in turn, the sky seen straight from the sand, and NRD and the built-in denoiser against the
undenoised image, for the canvas open at the front (`roof`) and with its skirt (`tunnel`).

Characters' light has checks of its own. `a hair card in the sun` casts a traced-only card of 4x4 quads whose texture
covers every other column of texels (every alpha-tile texel half covered) and compares its shadow as hair (strands stop a
ray by chance, as much as they cover) with the same card cut out by the cutoff: 0.5 of it expected. Light through thin
skin (an ear) is the walk's: `a thin slab of wax (an ear) passes the light a reference random walk gives for its thickness`
puts 3 mm and 10 mm slabs of a material light walks inside between a lamp and the camera, whose own rays meet them, and
compares their far side's light with a random walk on the CPU (HDRP's thin-object transmission by its thickness map, and
its check `thin skin lit from behind`, went with PT-58). `a table HDRP shades itself` draws a table forward, in the traced scene for camera rays only and shaded by
HDRP as the eyes are, and reads its color against "HDRP's" (the raster's) under a lamp a traced block shadows, without the
block, with TGPT_OPTION_NO_HDRP_SHADOWS, with the lamp marked TGPT_LIGHT_HDRP_SHADOWS, missing from the traced scene (left
as HDRP made it), and in the sun with and without the sun's w of 2. `characters' acceleration structures` times 64 posed characters of 8,192 triangles against none, for the record (the
GPU is shared: its low quantiles say most).

PT-84, the edges check (`tests/pathtracing_edges_checks.inl`; alone with `TGPT_TEST_EDGES_ONLY=1`, some seconds after
the warm-up): a red bar about a pixel wide and a green card with holes cut out by its texture, in the albedo view with
the tracer's own camera rays. Sixteen frames, each with another sub-pixel shift; every pixel is compared with what a
ray through its shifted centre meets, worked out on the CPU from the camera alone. Three times: the camera's matrices
holding the shift, handed without it (as the game handed them until 2026-10-08: the tracer then puts it in), and with
the camera moving. It passes when next to no pixel differs from its own frame's shift, and thousands differ from the
frame before's and from none. It writes `84-edges-*.png`. A build from before PT-84 fails all of it. Evidence and
what to look for in game: `docs/agents/edges.md`.

Other GPUs and validation (run `build/native/tgpt_gpu_test.exe artifacts/Native <images folder>` directly; two runs
at once need `TGPT_TEST_STAGE=<another folder name>` on one, since the DLLs are staged beside the exe, and a copy of
the exe keeps a rebuild from failing while a long run goes):

- `TGPT_TEST_ADAPTER=1002` runs on the first AMD GPU (`10DE` NVIDIA, `8086` Intel), an integrated one too. Its
  numbers should match another GPU's to within a few tenths of a percent; the DirectX Raytracing 1.2 check expects
  what that GPU's driver reports.
- `TGPT_TEST_DEBUG_LAYER=<folder>` runs the bridge's Direct3D 12 device under the debug layer, with
  `TGPT_TEST_GPU_VALIDATION=1` under GPU-based validation too (much slower). The folder holds the `d3d12SDKLayers.dll`
  of the Agility SDK version TG Path Tracing ships (`build/native/bin/x64` in the Microsoft.Direct3D.D3D12 NuGet
  package). Windows' Graphics Tools optional feature must be installed as well: without it the runtime refuses the
  Agility SDK's debug layer too (`DXGI_ERROR_SDK_COMPONENT_MISSING`, 0x887A002D; the Agility SDK specification requires
  both). Every error and warning is printed; an error fails the run.

1. Start: BepInEx logs `TG Path Tracing 1.2.0 loaded`, `Hooked HDRP's RenderSSR ...` and `Path tracing
   is available on this GPU`; `BepInEx/TGPathTracing/Native/TGPathTracing.Native.log` has `Shared D3D12
   device: DirectX Raytracing tier ...` and, on the first gameplay frame, `Path tracing ready` and
   `Render size WxH`. The bridge log registers the extension beside the others.
2. In gameplay (Path Tracing On, the default): every 10 s BepInEx logs `Path tracing hook 10s:`
   with `recorded`, `executed` and `handed to the native side` equal and non-zero, and `Path tracing:`
   with the scene (objects, Drake, terrain, vegetation counts, meshes to register falling to 0) and
   lights; the native log's `Path tracing 10s:` has `composited` close to `frames`, `gpuWaits=0`, the
   GPU time and the render-thread split. Shadowed areas and interiors get coloured bounce light;
   HDRP's baked indirect light is gone (`Replace HDRP indirect light`). An earlier version's config with
   `Mode = Global Illumination` or `Full` comes up as On, and its `Indirect intensity` line is gone.
3. Debug views (Shift+Insert): Normals and Albedo line up with the scene (not upside down or shifted);
   Coverage colours most buildings, rocks, trees and terrain, dark where only HDRP draws (characters,
   grass); Materials shows textures where the objects are, and metal (armour, pots, weapons, a
   candlestick) dark: it shows the diffuse color, which a metal has none of (paths send its base color on); Indirect is smooth after a second of
   standing still and follows the camera without trails. BepInEx's `Path tracing:` scene line counts `mask maps`, the
   `Traced material shader` lines name each shader's (`mask map _MaskMap (tile N): metallic R 0-1, smoothness A ...`),
   and the F8 report's material under the crosshair says `its mask map traced` where it has one.
4. Sun shadows are traced (trees included), torches and lamps light their surroundings with traced
   shadows, reflections on wet or metallic surfaces come from the traced scene. Insert turns path tracing
   off and on: compare with Off at the same spot for brightness, the same order of magnitude, no black or
   white surfaces. Water's migration check: `lantern-lake@dots` with None, then `water-day@quick`: reflected shore,
   bed brightness, continuous wave/rock intersection and moving surface guides. No old water-pass stage or F8 toggle
   remains; the native log says how much of the water's surface is traced ("The water's surface: N pieces, T
   triangles"). Read exposure/gain and the scene stage's time. What each run must show and the known gaps:
   `docs/agents/water-4.md` section 5.
5. Walk and turn quickly: no smearing of bounce light behind the camera's motion beyond a frame, no
   flicker along silhouettes worse than a few pixels. Fast travel or load a save: the scene rebuilds
   (meshes to register rises, then falls) without errors.
6. Performance: note the frame rate with Off and On, and the F8 report's GPU time by stage; Bounces 2 and
   Spatial Samples 8 should cost clearly more bounced light than the defaults (1 and 4).
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
   Reconstruction, and the lighting is clean without NRD's blur; NRD switches back. A Radeon RX 9000: the native log has `AMD Ray Regeneration available on this GPU` and
   `AMD Ray Regeneration context for WxH`, the overlay says `AMD Ray Regeneration`; any `AMD Ray
   Regeneration failed` or `could not start` line means NRD took over. Other GPUs log
   that Ray Regeneration needs an RX 9000. NRD, on any GPU with DirectX Raytracing (where neither of those runs,
   an RX 7000 for one, Denoising starts on it): the native log has `NVIDIA NRD 4.17.3 available` at start and,
   once it denoises, `NVIDIA NRD textures for WxH`; the overlay and the F8 status say `NRD`; `NVIDIA NRD could not
   start` or `NVIDIA NRD failed` means the tracer's own filters took over (the overlay says built-in denoiser; the row
   never offers them). Somewhere dim, NRD's bounced light is calm without blurring the sun's and the lamps' shadows.
11. The Video settings: a Path Tracing row after the upscaling and frame generation rows, with its options nested
   under it only while it is on (the row offers Off and On); FoA Mod Manager
   shows exactly the same rows in the same order, and a change on either side shows on the other after the
   settings close. Denoising lists None, NRD, Ray Regeneration (RX 9000 only) and Ray Reconstruction
   (NVIDIA with TG Better Upscaling), and starts on the best of them; Restore Defaults picks it again. Exposure
   (0 automatic) brightens or darkens the traced light by that many stops; Dark Adaptation at 0 % leaves interiors
   at the game's own exposure (dark), at 100 % brightens them to a mid tone, and sunny exteriors stay at the game's
   exposure either way. The brightening holds back where the picture is already bright: beside a lantern in a dark
   room the wall keeps its grain and colour (it is not white), the flame and the lamp's glass stay the brightest
   things, and no ring shows on the wall where the brightening ends. An earlier config's hidden `Auto exposure compensation` carries over to Exposure. Shadow
   Detail at 0 % against 50 % in a room lit by one candle: the unlit side shows texture at 50 % while the candle's
   bright wall stays as it was; in the main hall and outdoors in the sun the two look the same (the BepInEx
   `Path tracing:` line's `shadow detail N%` is the effective strength: near 0 there, up to the row's value in the
   dark room). Ray Reconstruction
   with FSR or XeSS upscaling: its tooltip says to select DLSS, the overlay says NRD. With DLSS 5
   loaded, Denoising still starts on Ray Reconstruction and it runs. Indirect Color at 3 visibly tints surfaces
   next to strongly colored ones (the Indirect debug view shows it), at 0 the bounced light is grey, and neither
   changes its brightness. An earlier version's config
   (`Denoiser = Auto`) becomes Denoising on the best; `Denoiser = Built-in` and `Denoising = Built-in` become NRD. With FoA Mod Manager
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
   Hair's shadow on a face or a neck is soft and lets light through between the strands (no hard, card-shaped edges).
   A character with the sun or a candle behind them: the ears' rims and the nostrils' wings glow red, the cheek in front
   of the head does not; BepInEx's `Subsurface material "..."` lines name the skin's materials light walks inside.
   An NPC in a candle-lit corner, a wall or their own head between them and the candle: their hair and eyes are as dark as
   the traced face beside them, Hair Shadows Off lights them again as before; in the open they look the same either way,
   and in dialogue the hair shows no crawling noise. A far NPC (50 to 100 m) in the sun casts a shadow on the ground (the
   native log's `characters=` count may rise outdoors). BepInEx logs `Characters: hair shader "TG/Character/RealHair"
   colors and values:` once.
13. Emissive Lights (on by default): the Emissive debug view shows glowing surfaces green where they are sampled as
   lights, blue where they are a kept light's own surface (a lantern's glass round its candle), red where only the rays
   that meet them see them (moving, past the budget, too dim, or the row off). Embers, lit windows and glowing crystals
   light what is around them with little noise, and brighter than with the row off (unsampled, the clamps cut their light
   as rare bright hits); a lantern's surroundings look the same either way. BepInEx logs `Emissive material "..."` for the
   first 40 (nits, emissive map, albedo, exposure weight) and `has emission the tracer doesn't read` once per shader
   whose glow it misses; the native log's 10 s line has `emissive: N submeshes, T triangles, S sampled as lights in C
   clusters` (and `over the budget` when some are left out).
14. Glass (the Caustics row, On): a bottle or flask on a table in the sun or by a lamp puts a bright spot or line in its
   shadow, which goes with the row off (a plain tinted shadow). An F8 with the crosshair on a bottle says how many times
   the camera ray crosses it: 2, its caustics are solved; 4 or more (hollow, an inner wall), it keeps the plain tint.
   The alchemist's jars (see-through glass) are sharp, not blocky; seen against the sky (a window from inside, a bottle
   on a sill) the glass is there, tinting the sky behind it, with its reflection.
15. Water (the same row): a lake or pool bed in the sun seen from the shore ripples with the water's caustic pattern,
   also from under water, and not in the shade of a bridge or a wall, nor at night; the underwater light shafts keep
   theirs. The underside of a bridge, a hull, the rock over a pool take a faint moving light the water reflects, strong
   when the sun is low. With the row off neither shows (nor the game's own): the bed is lit by the sun's and the lamps'
   straight light then, without the pattern. The F8 report's water line lists each
   surface's caustics settings and `caustics traced:` with up to four slots, their meshes and pattern size; the stage
   times show the caustics' rays in `caustics` and the water's own surface (its waves as triangles, posed and refitted
   every frame) in `scene`. The water itself, with the row on or off: its waves move, the far shore mirrors in it at a
   low angle, the bed shows through near the shore and fades with depth, the waterline follows rock and sand; a water
   surface past the tracer's four slots still looks like the game's own. With Ray Reconstruction, look for trails behind
   the moving pattern on a lit bed.
16. Eyes (traced since round 3): the whites, iris and a darker limbal ring, wet highlights from the room's lights, no
   black eyes; eyebrows and lashes are opaque at every distance while tracing, as is hair, whose colors (root to end,
   highlight bands) match the game's from far away too.
17. Hair up close (round 4): strands and clumps, not flat LEGO bricks; brows are brows and lashes are thin dark lines,
   not black bars; no black band at the hairline, and the eyes next to the lashes keep their own color. With the sun or a
   candle on a head of hair, a shiny band runs across it, moving along the hair as the camera or the light moves (the
   game's own hair highlights); a beard has its own, fainter. BepInEx's `Characters: shader "TG/Character/RealHair"`
   line ends `its highlights along its strands: tint ...`, and the hold line says `cut where at least 0.5 opaque`.
   Tracing turned off with Insert: hair, brows and lashes look as the game draws them (its soft layers back).
18. Starting (round 4): after an update the game shows its menu without a freeze of 15-20 s; the overlay says
   `starting` for a few seconds, then traces. The native log has `Path tracing passes made in N s` (the D3D12 and
   D3D11 ones), and `Path tracing ready ... in 0.1 s` (it was the whole wait). If the game ever freezes with a driver
   reset, the native log's `The GPU stopped` line names the stage that hung.

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
   limit."). The NVIDIA App's Max Frame Rate and Vertical sync On give no alert (Vertical sync at 1/2
   refresh does, after about ten seconds); a Max Frame Rate at or below the frames frame generation
   shows is named as the cause in the framerate check (7).
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

## Framerate Limit 45

1. With TG Frame Generation, VSync off and Borderless Fullscreen, the Video settings' Framerate Limit
   row goes 30, 40, 45, 50 (the entries the display gives). BepInEx's log says "The game's Framerate
   Limit offers 45 fps between 40 and 50". Choose 45 and apply: the performance overlay's rendered rate
   holds at 45, and 90 on screen with 2x frame generation.
2. Restart the game: the row still reads 45 and the rendered rate is 45 once in play; the log says
   "Framerate Limit 45 chosen again". Choose 60 (or 100, 120, 144) and restart: that choice comes back,
   not the one above it.
3. With 45 chosen, turn VSync on: the row moves to the nearest VSync rate and 45 is skipped by the
   arrows; turn VSync off again and 45 can be chosen. Switch to Exclusive Fullscreen with frame
   generation off: 45 moves to 40 and is skipped.
4. Choose 45, close the game, remove TG Frame Generation and start: the row reads the game's default
   (60) and every entry works as before.

## Frame generation pipeline changes in 1.2.0

In game (none of this has been run yet). Compare against 1.1.0 where a number is asked for.

1. D3D11 no longer waits for frame generation. With DLSS, FSR and XeSS at 2x, uncapped and VSync
   off, note the performance overlay's rendered and displayed framerate in the same spot as with
   1.1.0. Both should be the same or higher; the GPU-bound case gains most. In the Pipeline line's
   `d3d11Waits buffer/inputs=`, `buffer` equals `presents=` (the game renders every frame into one
   back buffer, whose copy was queued just before); `inputs` should stay near 0. Watch for corruption: flickering depth or motion (FSR debug flag 4 shows them), a
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

PT-71 glass/eyes/forward checks: `TGPT_TEST_GLASS_ONLY=1` runs the 320x180 raw optical
fixtures and exits. `TGPT_TEST_STOP_AT_FAIL=1` stops a mutant at its first failure.
The cases measure Unlit emission, eighty real interfaces, Beer-Lambert absorption,
material IOR, glass guides, a hollow jar behind two panes, its rim, and an iris under
a refracting cornea directly and through a mirror. No denoiser runs; requesting guides
only produces the inputs and does not evaluate Ray Reconstruction. Evidence, mutants,
remaining gaps and the coordinator's three in-game recipes: `docs/agents/glass-eyes.md`.

PT-75 and PT-80, the filters' checks (they need a build with NRD for NRD's part; `docs/agents/filters.md`):
`TGPT_TEST_NRD_MEAN_ONLY=1` runs `nrdMean` alone (a denoiser's picture converges to the undenoised picture's: four
scenes, without reflections, the whole picture, the debug views; `TGPT_TEST_NRD_MEAN_BRIEF=1` the whole test's
shorter form, `TGPT_TEST_NRD_MEAN_SCENE=<part of a name>` one scene, `TGPT_TEST_NRD_MEAN_QUICK=1` its first part,
`TGPT_TEST_NRD_MEAN_FLAT=1` without normal maps). `TGPT_TEST_MIRROR_FILTERS_ONLY=1` runs `mirrorFilters` alone (a
mirror's picture keeps its detail and light with NRD, the built-in denoiser and as Ray Reconstruction is handed it,
with the camera still and moving). Each mean is 64 frames after 200: a scene takes two to five minutes.

PT-85, the lamps' highlights (`docs/agents/glints.md`): `TGPT_TEST_GLINTS_ONLY=1` runs `glints` alone (a glazed ball
under eight small lamps, no denoiser, ReSTIR's reuse as in the game: at the pixels that mirror a lamp, each pixel's
largest sample of the Specular layer over its own mean, for five roughnesses and four sets of lamps: eight in the
cell, forty in the cell, eight on the far list among 208, 128 in the cell). `TGPT_TEST_GLINTS_FRAMES=<N>` frames a
case (128), `TGPT_TEST_GLINTS_SET=eight|forty|far|crowd` one set, `TGPT_TEST_GLINTS_OPTIONS=<bits>` more options
for a study (2048: ReSTIR's boiling filters off; its checks then do not run). About three minutes once the passes are
made; it needs no NRD.

PT-118, the camera's fog in cells (`docs/agents/fogrule2.md`): `TGPT_TEST_FOG_FROXEL_ONLY=1` runs the fog's check alone
(`tests/pathtracing_fog_froxel_checks.inl`, never part of the whole test): seventeen cases, the mean of 16 frames with
no denoiser, region by region. Outdoors against what is known exactly (a uniform fog under an unshadowed sun and a
uniform sky; a lamp 6 m and 60 m ahead in a uniform fog; the game's night fog, which thins with height, under its
moon alone and under a uniform sky alone, by quadrature); in the room and with the game's two daylight fogs against
pins (`tests/pathtracing_fog_froxel_pins.inl`: the per-pixel route's numbers from the last build that had both fog
routes, filled 2026-10-12; a case whose pin is all 0 is printed, not judged). The open cases' ground is black by its base
colour and still reflects 4 %: its highlight of the sun and of the lamp, dimmed by the fog, is part of the exact
value (`docs/agents/fog-3.md`: left out, the check's first run read the ground x1.24 to x3.43);
`TGPT_TEST_FOG_ABSORBER=1` makes it reflect nothing. The night's cases print beside the tracer's numbers what HDRP's
own volumetric fog gives for the same medium, light and sky, by hand (not judged: PT-37). `TGPT_TEST_FOG_CASE=<n>` one
case, with its picture; the file's header lists the switches for a closer look (a lamp's distance, a candle, fog
boxes, no sun, the outdoor places). About ten minutes once the passes are made (the first start of a build makes
them: seven minutes with nothing else on the card).

PT-133, a frame without a sun (`docs/agents/nosun.md`; the game's interiors and nights have none, and a Shader Model 6.9
pass returned 1e33 there while every offline scene had a sun): `TGPT_TEST_NOSUN_ONLY=1` runs
`tests/pathtracing_nosun_checks.inl` alone (a closed room, so the sun lights nothing in it: the picture with the sun's
code run, with it skipped, skipped again with other random numbers, and skipped as the plugin sends it, each the mean of
16 frames; with no denoiser, with NRD, in traced fog with seven fog boxes by the old route and by the grid). For any
mode: `TGPT_TEST_NOSUN=1` (every frame without a sun: `sunColor`'s w 0) or `2` (as the plugin sends it: direction and
colour 0 too), `TGPT_TEST_TRACE=<file>` (one line of numbers a frame), and in the tracer `TGPT_TEST_SM66=1` (its Shader
Model 6.6 passes on a card that offers 6.9). Two traces of the same mode, one with `TGPT_TEST_SM66=1`, laid side by
side: `python tools/agents/nosun_compare.py <6.6 trace> <6.9 trace>` (its own check: `python tests/nosun_compare_test.py`).
