# Native architecture and ownership (v1.1.0)

This is original integration code. The Open Shaders implementation was read as an architectural
precedent, not copied as a Skyrim backend dependency. The frame generation and upscaling backends are
the vendors' own SDKs: NVIDIA Streamline DLSS-G, the AMD FSR API (FidelityFX SDK 2.3; frame
generation and upscaling) and Intel XeSS (XeSS SDK 3.0; XeSS-FG and XeSS Super Resolution). Primary
references are listed in SOURCES.md. DLSS-G, FSR and XeSS frame generation (with XeLL) have run in
FoA, with DLSS upscaling and on HDRP's own inputs, and TG Better Upscaling's FSR and XeSS have upscaled in
FoA; Anti-Lag 2 has not yet been confirmed in game.

## Three native DLLs

`native/bridge` builds `TGGraphicsBridge.Native.dll`: the graphics hooks (one MinHook registry for
the process), the observation of the game's real D3D11 device and immediate context, the shared
D3D12 device, and the extension host. `native/framegen` builds `TGFG.Native.dll` and
`native/upscaling` builds `TGBetterUpscaling.Native.dll`, both extensions of the bridge
(`native/bridge/include/tgb/bridge_api.hpp`):

- `Host` is what the bridge offers an extension: hook installation (inline and vtable) with the
  original pointer, a per-thread "internal call" flag so an extension's own DXGI/D3D calls pass the
  hooks untouched, a native DXGI factory, Authenticode signer checks and signed loading, the game's
  D3D11 device and context with a generation counter (bumped when the game creates a new device),
  the shared D3D12 device, the GPU vendor and the BepInEx root.
- `Extension` is what an extension offers: its `needs()` (`NeedD3D12`, `NeedPresentation`),
  `bootstrap()` outside DllMain, `create_d3d12_device()` (frame generation creates it through
  Streamline on NVIDIA GPUs, so DLSS-G sees the device it presents on), `d3d12_device_ready()`, and
  for the one presentation provider `create_swapchain()`.

The bridge creates the D3D12 device inside the game's swapchain creation, on the swapchain's adapter.
That is also where ReShade's guard lets a nested device creation through; creating it earlier, from
the device hook, would be refused while ReShade wraps the game's device.

`TGGraphicsBridge_ReadStatus` reports what happened: extensions registered/active/failed, their
needs, which extension presents and which created the device, whether the game's device was seen,
swapchains seen and replaced, the last error and the loading PID. The bridge's gameplay plugin logs
it; both mods include it in their diagnostic reports.

## Upscaling (TG Better Upscaling)

HDRP decides per camera which upscaler runs (`HDRenderPipeline.SetupPartnerUpscalers`): DLSS if the
camera allows it, else STP when the camera requested dynamic resolution. The game's upscaling
controller (`UpScalingController.OnSettingChanged`) configures that from its Upscaling row. For the
two entries TG Better Upscaling appends (index 3 FSR, index 4 XeSS), a Harmony prefix configures the camera
exactly as the game does for STP - `allowDynamicResolution`, DLSS off, a dynamic-resolution scaler
returning the row's `QualityScaling` - instead of the game's error path, which would turn upscaling
off.

`HDRenderPipeline.DoStpPasses(renderGraph, hdCamera, inputColor, inputDepth, inputMotion,
inputStencil)` is where HDRP would record STP. A prefix records this mod's pass instead, for the
gameplay camera only (`Camera.main`, no target texture) and only while the selected technology is
usable, and returns the same output handle STP would (`GetPostprocessUpsampledOutputHandle`). The
render-graph textures only exist while the graph executes, so the frame is handed over from a pass:
HDRP's own `CustomPass.ExecuteInternal` records one for this mod's `CustomPass` with the output as
its color target (which keeps the pass from being culled, since post-processing reads it), color as a
second color target (which keeps it alive until the pass), and depth and motion vectors. The pass's
owner volume is never enabled, so HDRP never runs it by itself. When the pass executes, it resolves
the four textures to their `ID3D11Resource*`, submits the frame (`TGUP_SubmitFrame`) and issues
render event 1; if that fails, it enlarges color into the output with `HDUtils.BlitCameraTexture` so
the frame is never left unwritten.

On the render thread (`native/upscaling/src/upscaler.cpp`), event 1:

1. copies color, depth and motion vectors on the game's D3D11 context (isolated with
   `SwapDeviceContextState`) into textures shared with the D3D12 device: RGBA16F, R32F, RG16F;
2. signals the shared fence and flushes; the private D3D12 queue waits for it on the GPU;
3. records FSR (`ffxDispatchDescUpscale`) or XeSS (`xessD3D12Execute`) into a command list writing a
   shared RGBA16F output;
4. signals the queue whether or not the SDK accepted the frame, and the game's D3D11 context waits
   for that on the GPU, never the CPU;
5. writes the output into HDRP's destination through a UAV, or a render-target view if it has none;
   if the SDK refused the frame, the shared color copy is enlarged bilinearly instead.

A technology change or a context rebuild (output size, flags) idles the queue first.
After five refused frames in a row a technology is off for the session and STP runs again.

The inputs are STP's: HDRP's pre-exposed linear color (FSR is told HDR and an exposure of 1.0; auto
exposure is an advanced option), reversed-Z depth (FSR gets near and far swapped, as AMD's
integrations do; XeSS gets `XESS_INIT_FLAG_INVERTED_DEPTH`), motion vectors scaled by
(-render width, -render height) into render pixels, the jitter HDRP applied negated, HDRP's own
history reset, the camera's vertical field of view and HDRP's render delta time. FSR's maximum render
size is the output size, because the row's slider can go up to native. XeSS takes the narrowest
preset whose range contains the render scale (XeSS 1.3 and later accept any input resolution within a
preset's range); the preset follows the slider.

Frame generation then sees an upscaled frame exactly as after STP: its HDRP-input custom pass runs
before post-processing and takes HDRP's depth and motion vectors at render resolution.

The jitter is HDRP's own. In STP mode it comes from `STP.Jit16(taaFrameIndex)`, which despite its
name is Halton(2,3) over HDRP's 1024-frame index, longer than the `8 * (output / render width)^2`
samples AMD and Intel ask as a minimum; STP itself derives the jitter it removes from that index, so
an override would break STP whenever it upscales in FSR's or XeSS's place. When STP takes over right
after a frame this mod upscaled, its history is reset (`resetPostProcessingHistory`, read by
`PopulateStpConfig`); a hosted frame after a gap resets FSR's or XeSS's.

Exposure: HDRP pre-exposes its color by its exposure, and only its DLSS pass reads that exposure back
(`HDRenderPipeline.DoDLSSPass`: `hdCamera.RequestGpuExposureValue(GetExposureTexture(hdCamera))`, then
`Clamp(hdCamera.GpuExposureValue(), 0.2, 2)` as DLSS's pre-exposure). For FSR the prefix requests the
same readback every hosted frame and sends the same clamped value; the native side gives it to FSR as
`preExposure` and, through a 1x1 R32_FLOAT texture written from a ring of upload slots, as its
`exposure`. FSR divides the pre-exposure out, so its history is kept independent of the exposure and
survives eye adaptation, and multiplies the exposure back in, so its heuristics see the color HDRP
tonemaps. The limit keeps that history within half precision where the exposure is far from 1. XeSS
has no pre-exposure input and keeps an exposure multiplier of 1.

Texture mip bias: wherever STP runs, HDRP biases texture sampling by `log2(render / output width)`
(`HDCamera.Update`: `globalMipBias = materialMipBias + CalculateMipBias(...)`, applied because STP's
injection point is before depth of field). That is Intel's guidance for XeSS. AMD asks for one mip
more with FSR, `log2(render / output width) - 1`, so while FSR is selected the gameplay camera's
`HDAdditionalCameraData.materialMipBias` gets -1 on top of its own value, which the game never sets;
it goes back as soon as another upscaler is selected.

FSR's version is AMD's default for the GPU: no version override, as AMD asks when the player has no
version choice. FSR's own sharpening is the only sharpening: DLSS has none any more, XeSS never had.

The sign convention was checked on 2026-09-23 with AMD's and Intel's runtimes themselves: jittered
render-resolution frames cut from a supersampled in-game image the way HDRP renders them (frustum
moved by +taaJitter, bottom row first) upscaled closest to that image with the SDKs given
-taaJitter on both axes, the convention HDRP's DLSS pass uses; every other combination scored 3 to
10 dB worse. FSR 3.1's softness at a third of the resolution is its own: it was the same on those
synthetic frames, and its own sharpening recovers part of it.

Quality modes replace the game's slider for DLSS, FSR and XeSS (`QualityModes.cs`, `DlssModes.cs`,
`UpscalerRow.cs`). The modes come from the runtimes: `GraphicsDevice.GetOptimalSettings` for each NGX
quality level (Unity's `DLSSQuality` lacks DLAA but passes 5 through), FSR's null-context
`FFX_API_QUERY_DESC_TYPE_UPSCALE_GETUPSCALERATIOFROMQUALITYMODE`, and `xessGetOptimalInputResolution`
for each XeSS preset at a 4K output (`TGUP_GetModes`). `SwitchRowsOption` shows the game's slider
under STP and a Quality Mode row under the other three. The controller prefix gives FSR and XeSS the
mode's render scale (a tenth of a point above, so HDRP's rounding stays inside a preset's input
range) and XeSS the preset (`TGUP_Frame.mode`, preferred over the ratio search while its range holds
the render size). For DLSS it replaces `UpScalingController.EnableDLSS` with the same camera setup,
the mode as `deepLearningSuperSamplingQuality` and NVIDIA's size as the scale; HDRP recreates the NGX
feature when the quality level changes. A postfix on `UpScaling.SetValueForPreset` moves the modes
with the game's graphics presets. After the rows are installed, `Setting.Apply(true, ...)` raises the
controller's refresh even without a change, since it configured the camera before the rows existed.

The game's own DLSS runs from the `nvngx_dlss.dll` that NVIDIA's runtime (`_nvngx.dll`, in the
driver) loads with `LoadLibraryExW` by full path while the game initializes NGX: the one next to the
game, 3.8.10 as shipped. `native/upscaling/src/dlss_dll.cpp` hooks `kernelbase!LoadLibraryExW` at
bootstrap. For a path ending in `nvngx_dlss.dll` whose file version is older than the bundled,
NVIDIA-signed `Native/DLSS/nvngx_dlss.dll` (310.9.1, DLSS SDK), it loads the bundled file instead,
data-file loads included, so a version query and the loaded module agree. An equal or newer file (a
DLSS swap, the driver's own download) and every other load pass through, and so does a file that
belongs to a DLSS mod's set in its folder (`part_of_dlss_set`): `nvngx_dlssnr.dll` beside it (DLSS 5's
neural rendering), or `nvngx_dlssd.dll` or `nvngx_dlssg.dll` of the same major.minor release. The RenoDX
DLSS 5 add-on stopped scaling with the render resolution when the game's DLSS ran our 310.9.1 beside
its 310.8 set. The decision is made once
per path and logged.

## Performance overlay

`TGGraphicsBridge.PerformanceOverlay` draws the panel from the bridge's per-frame component with IMGUI
(`OnGUI`, Repaint events only), after the game's UI, so frame generation treats it as UI. It records
`Time.unscaledDeltaTime` every frame while shown and rebuilds its text four times a second: its own
Performance section, then the mods' sections in ascending order (upscaling 10, frame generation 20),
each read through the delegate the mod registered; a section that throws shows as unavailable. The
setting is the bridge plugin's (one copy loads, whichever mod bundled the newest), so both mods share
it. `PerformanceOverlayRow` adds it to the game's Video settings as a game `Setting`
(`PerformanceOverlaySetting`: Performance Overlay, and through a `SwitchRowsOption` Overlay Position
and Overlay Size while it is on), as the last graphic setting, and moves it back to the end whenever
the settings screen is closed and other rows were added after it. It adds it once: only if no setting
of that type is in the list already. `TGGraphicsBridge_ReadPerformance` starts a sampling thread on its first call: process and system
CPU time, PDH's `\GPU Engine(*)\Utilization Percentage` summed per engine type (the busiest type,
for this process's `pid_` instances and for all), and the game adapter's local memory use and budget.

The DLSS model the overlay names: NGX has no query for it, and the NVIDIA App's DLSS override is
applied inside the driver. `native/upscaling/src/nvidia_profile.cpp` reads that override where the
NVIDIA App stores it, the driver's settings profile for the game's executable (falling back to the
global profile), through NVAPI's DRS functions from `nvapi64.dll`: `Override DLSS-SR presets`
(0x10E41DF3), `Enable DLSS-SR override` (0x10E41E01), `Override DLSS-SR performance mode`
(0x10AFB768), `Override DLSS mode to be DLAA` (0x10E41DF4) and `Override scaling ratio for DLSS-SR`
(0x10E41DF5), once when the overlay first asks and every 30 seconds after. The driver checks
`NVDRS_SETTING_V1` at 12320 bytes and refuses the 12328 bytes the current nvapi.h gives it (a 64-bit
member pads its unions), so the structure is declared with 4-byte packing.

## Source and output are distinct

`native/bridge/src/hooks.cpp` intercepts native DXGI factory creation and the combined
D3D11 device/swapchain entrypoint. Game factory calls still use the original
D3D11 path. For one suitable game HWND, the returned object implements
IDXGISwapChain through IDXGISwapChain4 but returns the **D3D11 device and a stable
D3D11 logical buffer zero**. It never returns a D3D12 texture to Unity.

On NVIDIA GPUs the Streamline device export creates a private D3D12 device on the
same adapter LUID; Streamline binds to it when DLSS-G or Reflex is supported. Other
GPUs get a plain D3D12 device. The bridge owns a DIRECT command queue and a
three-buffer flip-discard output swapchain on the real game window, created by
whichever frame generation technology is selected (see below). `insideSL` prevents
these internal calls from being intercepted as game calls.

The wrapper intentionally does not implement every possible DXGI client pattern:
indexed source buffers, multiple HWNDs, arbitrary QueryInterface requests,
nonblocking/dirty-rectangle presentation, VR and MSAA source swapchains are not
supported. Unknown interfaces return E_NOINTERFACE rather than leaking a native
D3D12 identity. Returned device/resource interfaces hold owning COM references.

Creation failure releases the partial bridge and falls back to original game
swapchain creation. After a running bridge encounters a GPU error, it does not
attempt unsafe live renderer replacement; the process needs restarting.

## Capture before the original upscaler evaluation

`ngx_capture.cpp` discovers the D3D11 NGX create/evaluate/evaluate-C/release exports
through GetProcAddress. Separate typed forwarding thunks retain provider-local
function pointers and feature handles. Nested wrapper calls are not double-counted.
All original API arguments and return values are preserved.

For one SR evaluation inside the selected Unity camera interval, the capture
checks frame identity, dimensions, typed resource interpretation, subrect bounds,
create flags, jitter and motion-vector scales. It normalizes depth and motion
into shader-readable, shareable R32_FLOAT and R16G16_FLOAT resources. The shader
loads the observed subrect; it does not estimate motion from color or synthesize
missing depth. D3D11 context state is swapped to an isolated scratch state during
GPU work and restored afterward.

The feature's original evaluation still runs. Captured inputs become valid only
if that evaluation succeeds. Unsupported/missing data results in no FG request,
not guessed native resource states or fabricated camera history.

## HDRP inputs when DLSS doesn't run

With TAA, STP or no anti-aliasing there is no NGX evaluation. `HdrpInputs.cs` adds a global HDRP
custom pass at BeforePostProcess, where the frame's depth and motion vectors are final. For the
gameplay camera it sends `TGFG_SetHdrpInputs` the native pointers of HDRP's depth and motion vector
render textures (cached per texture, since `GetNativeTexturePtr` waits for the render thread), the
camera's render size and `HDCamera.taaJitter`, then queues render event 3 in the pass's command
buffer. On the render thread that event copies both into the frame's input slot through the same
path as the NGX capture, on the game device's immediate context. The constants match what HDRP's
DLSS pass gives NGX: jitter `-taaJitter`, motion vector scale minus the render size (normalized
-1, -1), unjittered vectors, reversed-Z depth. The pass runs before post-processing and so before
DLSS: while DLSS supplied the previous frame's inputs, event 3 leaves the frame to the NGX capture.

## One frame's GPU handoff

1. At the D3D11-facing Present, the source color is already rendered. FP16 scRGB
   is converted on D3D11 when necessary. Source buffers stay owned by the wrapper.
2. D3D11 signals the shared producer fence and flushes; the private D3D12 queue
   waits on that value. No image is mapped to CPU memory. Only D3D11 signals the
   producer fence and only the D3D12 queue signals the consumer fence (steps 4, 7
   and `drain()`): a fence Signal sets its value, so a fence with two signalling
   queues can move backwards once neither waits for the other.
3. The current **private D3D12** buffer index selects its allocator and backbuffer.
   The allocator's previous retirement fence is checked before reset.
4. A D3D12 command list transitions/copies final color to the actual output buffer
   and returns the shared source resource to COMMON and output to PRESENT. Right
   after it is submitted, and before Present, the queue signals the value that
   records this virtual back buffer as copied.
5. The captured frame's constants/depth/motion and, once verified, the HUD-less
   texture are tagged `eValidUntilPresent`. `before_present` applies requested
   options only if all readiness conditions pass. Otherwise it requests Off and
   clears the frame's tags.
6. The Streamline swapchain presents. The present thread queries its reported
   output count and borrowed input-processing fence, retaining its own COM ref.
7. The private queue waits on that completion dependency and signals the value
   that retires this present, which becomes the retire value of the frame's input
   slot. The selected `eBlockPresentingClientQueue` mode is also explicit in the
   options.
8. D3D11 waits only for what it is about to overwrite: the next virtual back
   buffer until D3D12 copied it (an earlier present when there are two or more),
   and an input slot until the present that used it retired, just before the NGX
   or HDRP capture rewrites it three frames later. No wait is queued when the
   shared fence has already passed the value; the pipeline line counts the ones
   that were (`d3d11Waits buffer/inputs`).

Until 1.2.0, D3D11 waited after every Present for that present's retire value, so the
game's next frame could start on the GPU only once frame generation had finished the
current one. The per-resource waits keep the same guarantees without that. Nothing here
has been GPU-profiled on hardware other than an RTX 5080; no speedup/latency/VRAM figure
is promised. Off mode still uses the output bridge and copy. No mode-toggle
output-swapchain recreation or device-loss recovery is implemented.

## Frame generation technologies

The presentation swapchain belongs to the selected technology:

| Setting | Presentation swapchain | Generation |
|---|---|---|
| Off, DLSS | Streamline's proxy (native swapchain when Streamline is not bound) | DLSS-G options per frame; Off/Fixed/Dynamic |
| FSR | FSR API frame generation swapchain (`ffxCreateContextDescFrameGenerationSwapChainForHwndDX12`) | configure + `PrepareV2` recorded on the bridge list; FSR dispatches generation in its Present callback |
| XeSS | XeSS-FG proxy (`xefgSwapChainD3D12InitFromSwapChainDesc`) | frame constants, depth and motion tagged `RV_ONLY_NOW` on the bridge list; `SetPresentId` = XeLL frame id |

A technology change recreates only this swapchain, between two frames on the presenting
thread: generation is paused, the queue drained, the old swapchain released (SDK contexts after
their proxy's last reference), the new one created from the stored client description, and color
space and HDR metadata re-applied. The D3D11-facing wrapper, its buffers and the shared input
textures are untouched. An SDK failure marks that technology failed for the session and the
standard swapchain is recreated. The FSR/XeSS factory comes straight from the system dxgi.dll
(hook trampoline), never from Streamline or an overlay.

All technologies consume the same inputs: the DLSS SR evaluation's depth and motion vectors,
its jitter and motion scale (NGX pixel scale), the camera packet and the observed DLSS feature
flags (inverted depth, jittered or render-resolution vectors). XeSS additionally gets the
world-to-view matrix from the camera packet. The SDKs copy the inputs during the bridge's own
command list, so the consumer fence still covers their use.

Maxima come from each runtime: Streamline's `numFramesToGenerateMax`, XeSS's
`maxSupportedInterpolations` (queried before and after initialization), and one for FSR, whose
API exposes no multi-frame control. Requests are clamped to the reported maximum.

## Low latency

One setting selects NVIDIA Reflex (+ Boost), Intel XeLL or AMD Anti-Lag 2. `select_latency`
applies the pairings: DLSS-G requires Reflex; XeSS-FG runs with XeLL and nothing else, and on
non-Intel GPUs XeLL works only together with XeSS-FG; Anti-Lag 2 needs an AMD driver. Reflex is
offered as a choice only when Streamline reports `lowLatencyAvailable` (NVIDIA's rule for the
Reflex UI); DLSS-G still runs it. The sleep runs in `TGFG_BeginSimulation` (outside the state
mutex), which the plugin calls from an entry in Unity's PlayerLoop right after
`TimeUpdate.WaitForLastPresentationAndUpdateTime` (`FrameStart`): after Unity's own wait for the
previous presentation, which Intel's XeLL guide wants before XeLL's sleep, and before any input is
read (Unity's in EarlyUpdate, Rewired's in Update). The SDKs ask for the wait just before input is
sampled; from a MonoBehaviour's Update it came after. XeSS-FG generates only from frames XeLL slept
for while its low latency mode is on. If another mod replaces the PlayerLoop, the frame begins in
Update again. Markers follow the existing PCL points (simulation, render submit, present), and
XeLL's sleep mode changes only after a GPU drain, as Intel requires. XeLL's frame limit counts
every Present, so under XeSS-FG the Base FPS limit reaches it multiplied by the presents per
rendered frame. With FSR on AMD GPUs the frame generation swapchain reports interpolated frames
to Anti-Lag 2 through the documented swapchain private data.

## HDR

`interop.hlsl` has four compiled entrypoints: depth copy, motion copy, fullscreen
conversion vertex shader, scRGB-to-HDR10 pixel shader. For FP16 source, linear
BT.709 values use the standard 80-nit scRGB scale, are transformed to BT.2020,
encoded with ST 2084 PQ, then stored in RGB10A2. Output color-space support is
queried and PQ/BT.2020 is set explicitly. Unsupported source color spaces are
rejected. Existing SDR/HDR10 color can use direct GPU copies.

This is a real encoding conversion, not an FP16 texture relabelled as HDR10.
It is nevertheless not lossless or colorimetrically validated on the user's
monitor; out-of-range/gamut values are clipped and a 10-bit output is used.

## BepInEx and frame identity

The managed plugin sends simulation boundaries, camera packets and HDRP input
packets, and queues native render-thread callbacks through Unity command buffers.
The ABI is `0x00050000`; structures use pack 4: request 48 bytes, camera 416,
status 188, HDRP inputs 48. Both managed runtimes use the same native interface
and the same C# sources. Status fields distinguish D3D11 source rendering from
D3D12 output presentation.

Under IL2CPP, the classes TGFG derives from game and HDRP types (the Video tab's
Setting and DependentOption, HDRP's CustomPass) are registered with Il2CppInterop,
which sends the base class's virtual calls to the managed methods of the same name
and parameter types. Il2CppInterop allocates these objects without running the
IL2CPP base constructor, so `Il2CppTypes.Construct` runs it.

Fixed N× sends N-1 generated frames. Dynamic sends NVIDIA's eDynamic and target
FPS. Requested and accepted modes/sequences remain distinct. A request is not
accepted when caps, format, camera, input or frame identity are missing, or when
configured suspension/heartbeat checks fail. Reported NVIDIA output frames are
summed from getState deltas; they are not inferred from copy counts and are not
a physical display measurement.

## Input orientation

HDRP renders into Y-inverted render targets and flips the image back in its final blit, so the frame
every runtime interpolates - the presented back buffer, and the HUD-less copy verified against it at
each Present - is vertically mirrored against the depth and motion vectors HDRP produced. No frame
generation SDK can be told that a tagged texture is upside down: Streamline, the FSR API and XeSS all
sample depth and motion at the coordinates of the frame they interpolate, and a projection matrix
describes a sign convention rather than texture row order.

Depth and motion copies are therefore Y-flipped within their source subrect for DLSS-G, FSR and XeSS
alike, and the vertical motion scale and jitter are negated with them. `TGFG_SetCamera` turns the
camera's four clip-space matrices the same way, so the camera describes the space those buffers are
in. That turn is `F = diag(1,-1,1,1)`: `F*P` for a projection and `F*C*F` for a transform between two
clip spaces, which on the packet's transposed row-vector storage is a negated row 1, a negated
column 1, or both. `native/include/camera_orientation.hpp` holds it, with the derivation.
`worldToView`, the camera basis and the scalar frustum are outside clip space and unchanged.

`tests/camera_orientation_tests.cpp` checks the turn against reference matrices built in both
orientations through an independent matrix inverse, and `tests/hudless_color_tests.cpp` runs the
depth and motion copies on WARP over even and odd subrect heights.

## HUD-less frame

`HudlessUi` installs a global `AfterPostProcess` custom pass. Its presence makes HDRP
render post-processing into `postProcessDest`, an intermediate color texture, before the final
blit and overlay UI composition. We no longer change `rendersUIOverlay`. Capturing the swapchain
at `endCameraRendering` was one frame late because Unity had not written it yet.

`TGFG_SetHudless` submits a 32-byte optional ABI packet containing the current frame ID,
color resource, viewport size, and color/orientation flags. Render event 4 executes inside the
custom pass, before HDRP can recycle the resource. The native shader reads only the valid
viewport, flips Y like HDRP's final blit, and writes the output swapchain's format: sRGB for SDR
or PQ for HDR. HDRP HDR color is linear nits in the output primaries; scRGB output with Unity HDR
inactive follows the bridge's existing 80-nit convention. Unknown HDR gamuts are withheld.
The native side checks device identity, viewport bounds, dimensions and texture layout.

At Present a compute shader compares, at every fourth pixel, the presented frame with that copy and
the copy with the previously presented frame, and the presented frame with its predecessor;
three counters are read back two presents later without waiting. The copy is trusted only when
it differs from the presented frame over at most
about a third of the sample points (HUD-sized) and is not identical to the previous frame (a stale
copy or incorrect render-event ordering). Still frames are skipped. After 30 consecutive trusted results the capture is
supplied (DLSS-G `kBufferTypeHUDLessColor`; FSR `HUDLessColor` in the per-frame configure; XeSS
`XEFG_SWAPCHAIN_RES_HUDLESS_COLOR` with UI composition switched on, AUTO mode). Thirty untrusted
results withdraw it again, with a log line naming which check failed; the HUD is then interpolated
with the scene. FSR resets interpolation history when the HUD-less source changes and rebuilds
its context when input encoding flags change, including enabling its debug checker.

The color remains after lens/post-processing effects. Moving capture before those effects alone
would make FSR mistake the effects for UI. Applying them after FG requires rendering them on each
generated presentation, which the current D3D11-to-D3D12 bridge does not implement.

`TGFG_CaptureHudlessD3D11(frameId, immediateContext, colorResource)` remains for an external
caller with its own pre-HUD stage; it feeds the same texture and the same verification.

## Frame-latency object

A client created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` receives a
bridge-owned semaphore, released once per presented frame, because a handle from the physical
swapchain would stop being signalled when that swapchain is recreated. The physical swapchains
are created without the flag and throttle through a blocking Present.

## Coexistence with ReShade, RenoDX and RenoDX DLSS

- ReShade 6 wraps the game's D3D11 device and context. NVIDIA's Unity plugin evaluates DLSS on
  that proxy context while the swapchain hook sees the real device, so the bridge tags the real
  immediate context with private data (ReShade forwards private data) instead of comparing COM
  identities. HUD-less capture likewise checks the resource device's immediate context when
  `GetDevice` returns a different COM identity; a ReShade proxy must not be mistaken for a
  different GPU device. Unknown contexts still fail validation.
- TGFG's private D3D12 device and swapchain are created inside the game's own swapchain creation
  call, where ReShade does not attach, so ReShade and RenoDX only process the D3D11 swapchain.
  RenoDX upgrades it to scRGB FP16; the bridge encodes that to HDR10 for the D3D12 output.
- For the supported RenoDX Unity final-output shader, the bridge retains HUD-less scene color
  as FP16 until RenoDX's full-screen backbuffer draw, observing direct, indexed and indirect
  draw entry points without assuming three-vertex game geometry. It replays the actual shader, sampler and
  b13 constant-buffer range onto that scene, then encodes PQ. Two additional full-size FP16
  intermediates are used only for this path. Pixel verification still gates all FG consumers.
  The installed stripped shader is identified explicitly; arbitrary ReShade effect chains and
  unknown stripped shader revisions are not covered by this replay.
- The RenoDX DLSS addon ("DLSS 5") calls `GetModuleHandleW(L"sl.interposer.dll")`, detours that
  module's `D3D12CreateDevice`/`CreateDXGIFactory*` exports and patches the returned factory's
  `CreateSwapChain*` slots. Its detour releases the new swapchain and returns `E_UNEXPECTED`
  unless ReShade observed the creation, which it never does for TGFG's private output. TGFG
  therefore loads the interposer as `TGFG.Streamline.dll`; Streamline itself only uses the name
  to find an optional NGX updater. The addon runs DLSS 5 through NGX directly on its own D3D12
  device; its NGX D3D12 hooks (with pre/post evaluation callbacks) also see TGFG's DLSS-G.

## Automatic startup

The only startup entry point is TG Graphics Bridge's BepInEx preloader, `src/TGGraphicsBridge.Autoload` (one copy per mod, one of which starts the bridge; see AUTOLOAD-INTEGRATION.md),
loaded by BepInEx inside the game process Steam starts. It registers the extensions it finds and
synchronously invokes the bridge's bootstrap. See AUTOLOAD-INTEGRATION.md for the Mono and IL2CPP
patcher contracts. The development launcher of earlier versions is gone.
