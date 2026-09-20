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
FSR and XeSS. When repeating such a comparison, check that the deployed `TGFG.Native.dll` hashes
equal to `artifacts/Native/TGFG.Native.dll` first: a mod manager may have left a symlink to an
older build in place, which makes a real change look like no change at all.

The Mono Kandra history regression uses the production Harmony hook and the installed
Harmony runtime, with a stand-in skinning manager (no game launch or GPU):

```powershell
dotnet build tests/KandraMotionHistory.Tests.csproj -c Release
$gameRoot = (Get-Content src/TGFrameGeneration/bin/Release/net472/TGFG.GameDir.txt -Raw).Trim()
& ./tests/bin/Release/net472/KandraMotionHistory.Tests.exe "$gameRoot/BepInEx/core"
```

It checks all FG backends, consecutive single-camera frames, repeated cameras, separate managers, frame gaps, Off/on,
copy failure/retry, and unpatch/reinstall. In-game, check the history guard installation
and `FG Kandra motion history: prevented ...` counters with the minimap camera active.
Use a fresh F6 capture during a camera turn to verify hand reprojection; these unit checks
do not establish Unity GPU ordering or that the hand flicker is fixed.

For the IL2CPP guard branch, using the installed IL2CPP game's Harmony runtime:

```powershell
dotnet build tests/KandraMotionHistory.Tests.csproj -c Release -p:TestRuntime=IL2CPP
$gameRoot = (Get-Content src/TGFrameGeneration/bin/Release/net6.0/TGFG.GameDir.txt -Raw).Trim()
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
$gameRoot = (Get-Content src/TGFrameGeneration/bin/Release/net6.0/TGFG.GameDir.txt -Raw).Trim()
dotnet tests/bin/Release/net6.0/Il2CppOptionConditions.Tests.dll "$gameRoot/BepInEx/core"
```

This uses a stand-in getter and the production Harmony prefix to check owned/unowned
routing, native-pointer aliases, changing/nested conditions, exception isolation, owner
retention through GC, clearing, and unpatch/reinstall. It does not run native IL2CPP.
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

Collect the new in-game diagnostic, BepInEx/LogOutput.log,
BepInEx/TGFrameGeneration/Autoload.log and native/Streamline logs under
BepInEx/TGFrameGeneration/Native in the game installation. Include whether this
was Off, Fixed 2× or Dynamic and whether the capture was in gameplay or a menu.
Compare managed and native PIDs with the current session in Autoload.log.

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

Exit and disable autoload using BepInEx/TGFrameGeneration/disable-autoload.txt,
or remove BepInEx/patchers/TGFG.Autoload.dll. Then start normally through Steam.
Removing only the gameplay plugin removes its menu but does not stop preloader
initialization. No proxy DLL or system setting was overwritten.

Check game-local BepInEx/TGFrameGeneration/Autoload.log first; confirm the current PID.
