# Native architecture and ownership (v0.5.0)

This is original TGFG integration code. The Open Shaders implementation was read
as an architectural precedent, not copied as a Skyrim backend dependency. The
frame generation backends are the vendors' own SDKs: NVIDIA Streamline DLSS-G, the
AMD FSR API (FidelityFX SDK 2.3) and Intel XeSS-FG (XeSS SDK 3.0). Primary
references are listed in SOURCES.md. DLSS-G and FSR frame generation have run in FoA;
XeSS frame generation, XeLL and Anti-Lag 2 have not yet been confirmed in game.

## Source and output are distinct

`graphics_hooks.cpp` intercepts native DXGI factory creation and the combined
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

## One frame's GPU handoff

1. At the D3D11-facing Present, the source color is already rendered. FP16 scRGB
   is converted on D3D11 when necessary. Source buffers stay owned by the wrapper.
2. D3D11 signals the shared producer fence and flushes; the private D3D12 queue
   waits on that value. No image is mapped to CPU memory.
3. The current **private D3D12** buffer index selects its allocator and backbuffer.
   The allocator's previous retirement fence is checked before reset.
4. A D3D12 command list transitions/copies final color to the actual output buffer
   and returns the shared source resource to COMMON and output to PRESENT.
5. The captured frame's constants/depth/motion and optional explicit HUD-less
   texture are tagged `eValidUntilPresent`. `before_present` applies requested
   options only if all readiness conditions pass. Otherwise it requests Off and
   clears the frame's tags.
6. The Streamline swapchain presents. The present thread queries its reported
   output count and borrowed input-processing fence, retaining its own COM ref.
7. The private queue waits on that completion dependency, signals the shared
   consumer fence, and D3D11 waits before subsequent writes. The selected
   `eBlockPresentingClientQueue` mode is also explicit in the options.

The implementation serializes the cross-API resource handoff deliberately.
It has not been GPU-profiled; no speedup/latency/VRAM figure is promised. Off mode
still uses the output bridge and copy. No mode-toggle output-swapchain recreation
or device-loss recovery is implemented.

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
non-Intel GPUs XeLL works only together with XeSS-FG; Anti-Lag 2 needs an AMD driver. The sleep
runs in `TGFG_BeginSimulation` (outside the state mutex), markers follow the existing PCL points
(simulation, render submit, present), and XeLL's sleep mode changes only after a GPU drain, as
Intel requires. With FSR on AMD GPUs the frame generation swapchain reports interpolated frames to
Anti-Lag 2 through the documented swapchain private data.

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

The existing managed plugin sends simulation boundaries and camera packets, and
queues native render-thread callbacks through Unity command buffers. The ABI is
`0x00030000`; structures use pack 4: request 40 bytes, camera 352, status 152.
Both managed runtimes use the same native interface. New status fields distinguish
D3D11 source rendering from D3D12 output presentation.

Fixed N× sends N-1 generated frames. Dynamic sends NVIDIA's eDynamic and target
FPS. Requested and accepted modes/sequences remain distinct. A request is not
accepted when caps, format, camera, input or frame identity are missing, or when
configured suspension/heartbeat checks fail. Reported NVIDIA output frames are
summed from getState deltas; they are not inferred from copy counts and are not
a physical display measurement.

## HUD hook boundary

`TGFG_CaptureHudlessD3D11(frameId, immediateContext, colorResource)` implements a
real copy/conversion into the frame's protected shared HUD-less texture. The
caller must invoke it after scene effects but before HUD for that exact frame,
after the selected NGX evaluation succeeded. It must use the source resolution
and color format. The generic managed adapter does not call this export because
no verified FoA post-RenoDX/pre-HUD stage has been identified. Default operation
therefore has final color, depth and motion, but no promised HUD reconstruction.

## Frame-latency object

A client created with `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` receives a
bridge-owned semaphore, released once per presented frame, because a handle from the physical
swapchain would stop being signalled when that swapchain is recreated. The physical swapchains
are created without the flag and throttle through a blocking Present.

## Coexistence with ReShade, RenoDX and RenoDX DLSS

- ReShade 6 wraps the game's D3D11 device and context. NVIDIA's Unity plugin evaluates DLSS on
  that proxy context while the swapchain hook sees the real device, so the bridge tags the real
  immediate context with private data (ReShade forwards private data) instead of comparing COM
  identities.
- TGFG's private D3D12 device and swapchain are created inside the game's own swapchain creation
  call, where ReShade does not attach, so ReShade and RenoDX only process the D3D11 swapchain.
  RenoDX upgrades it to scRGB FP16; the bridge encodes that to HDR10 for the D3D12 output.
- The RenoDX DLSS addon ("DLSS 5") calls `GetModuleHandleW(L"sl.interposer.dll")`, detours that
  module's `D3D12CreateDevice`/`CreateDXGIFactory*` exports and patches the returned factory's
  `CreateSwapChain*` slots. Its detour releases the new swapchain and returns `E_UNEXPECTED`
  unless ReShade observed the creation, which it never does for TGFG's private output. TGFG
  therefore loads the interposer as `TGFG.Streamline.dll`; Streamline itself only uses the name
  to find an optional NGX updater. The addon runs DLSS 5 through NGX directly on its own D3D12
  device; its NGX D3D12 hooks (with pre/post evaluation callbacks) also see TGFG's DLSS-G.

## Legacy development launcher (not the player workflow)

The launcher holds only the child process it creates. A temporary software
breakpoint at the PE entrypoint permits the Windows loader to finish; the byte
and instruction pointer are restored, the main thread is explicitly suspended,
and the debugger is detached before native loading. TGFG_Bootstrap executes
outside DllMain, before the main thread starts the game. The original loader
proxies on disk are not replaced. Early TLS graphics and Steam replacement
processes require separate handling and are not covered by this mechanism.

## Integrated automatic startup

The normal startup entry point is now `src/TGFG.Autoload`, loaded by BepInEx's
preloader inside the game process Steam starts. It synchronously invokes the
existing native bootstrap. The gameplay project builds the preloader through a
build-only project reference, but does not reference/load it at runtime. See
AUTOLOAD-INTEGRATION.md for the separate Mono/IL2CPP patcher contracts and the
unverified relationship between this preloader phase and FoA graphics startup.
The legacy native launcher is not included in player packages.
