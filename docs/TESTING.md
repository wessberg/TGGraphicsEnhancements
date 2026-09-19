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

Bridge flags: 1 active, 2 shared-texture path created, 4 shared-fence path created,
8 scRGB-to-HDR10 output path, 16 explicit HUD-less tag supplied for the last tagged
frame. Output counter is NVIDIA-reported, not independently measured. No counters
or flags are hardcoded to pretend frame generation succeeded.

## Rollback

Exit and disable autoload using BepInEx/TGFrameGeneration/disable-autoload.txt,
or remove BepInEx/patchers/TGFG.Autoload.dll. Then start normally through Steam.
Removing only the gameplay plugin removes its menu but does not stop preloader
initialization. No proxy DLL or system setting was overwritten.

Check game-local BepInEx/TGFrameGeneration/Autoload.log first; confirm the current PID.
