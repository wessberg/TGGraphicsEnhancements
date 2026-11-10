# TG Graphics Bridge, TG Frame Generation, TG Better Upscaling, TG Path Tracing and TG Neural Rendering

Four mods for Tainted Grail: The Fall of Avalon, built from this repository, and the part they share:

| | What it does |
|---|---|
| **TG Frame Generation** | Frame generation with NVIDIA DLSS, AMD FSR or Intel XeSS, plus NVIDIA Reflex, Intel XeLL or AMD Anti-Lag 2. |
| **TG Neural Rendering** | DLSS 5 Neural Rendering before the camera effects, interface and frame generation capture, with a choice of before or after upscaling. Supports official and compatible community runtime DLLs, supplied by the player. Starts Off and requires DLSS upscaling. |
| **TG Better Upscaling** | AMD FSR (FSR 4 on Radeon RX 6000 and newer and some Ryzen processors, FSR 3.1 elsewhere) and Intel XeSS upscaling, as two more choices of the game's own Upscaling row next to STP and DLSS, and a newer DLSS DLL for the game's own DLSS. |
| **TG Path Tracing** | A prototype of ReSTIR path traced lighting (the sun, every lamp, bounced light and reflections) for the Mono version, traced with DirectX Raytracing on the bridge's Direct3D 12 device, denoised by DLSS Ray Reconstruction (with TG Better Upscaling), AMD FSR Ray Regeneration or its own denoiser. In testing. |
| **TG Graphics Bridge** | Bundled with each mod, not a download of its own. Hooks the game's Direct3D 11 device and swapchain from a BepInEx preloader, creates one private Direct3D 12 device on the same GPU, and loads the native part of every mod built on it. Also carries what both mods share in game: the Video-tab plumbing, the mod-manager integration and two fixes to the game's motion vectors. With both mods installed, one copy runs. |

The mods are native: they work from the game's own motion vectors and depth, the inputs a game
with built-in support hands to these runtimes, and call each vendor's runtime directly, with no OptiScaler
or other wrapper in between. Their settings are rows in the game's own Video tab.

The game renders with Direct3D 11. The bridge shares the game's textures with its Direct3D 12
device through GPU fences on the same adapter; frame generation presents there with NVIDIA
Streamline, AMD's FSR API (FidelityFX SDK 2.4) or Intel XeSS-FG (XeSS SDK 3.0), and upscaling runs
FSR or XeSS Super Resolution there and hands the result back to the game's frame before
post-processing.

## Status

TG Neural Rendering 1.2.3 is built for both game versions. I have tested it with SDR and ReShade/RenoDX HDR on
my RTX 5080. The community DLL also passes rendering, SDR/HDR, Ray Reconstruction and multipass checks on
that card; its in-game check and testing on older RTX cards are still pending. Video and FoA
Mod Manager have **Neural Rendering** (Off / DLSS), **Placement** (Quality / Performance), **Strength**,
and **Advanced**. Advanced shows **Local Tone**, **Style** (Default/A/B/C),
**Passes** (1-4), **Detail**, **Automatic Mask** and, with
masking on, **Skin Detail**. Hiding those controls keeps your choices. Neural Rendering starts Off.
I use NVIDIA's Default style, full strength, tone and detail, with automatic masking Off for both placements. Quality runs
after upscaling and is the default. Performance uses fewer pixels before upscaling, but can soften detail
and show larger colour or brightness changes after you stop moving. Ray Reconstruction requires Quality.
For a look closer to the original lighting and colour, lower Local Tone. Lower Strength to reduce the whole
effect, including detail; zero keeps the original scene. These controls range from 0 to 1 and apply to each pass.
I default to one pass. Extra passes feed the result back through NR, using the same settings each time.
They cost more GPU time and memory and can exaggerate colour changes or artifacts. Two passes at half
Strength are not the same as one at full Strength; Strength is not a final opacity blend.
The main row greys out unless DLSS and the other requirements are available, with the reason in its tooltip.
The download includes NVIDIA's signed `nvngx_dlssnr.dll` 310.9.16.0, NVIDIA's faster model of October 2026, unmodified, from
NVIDIA's public RTX Remix release. To use another DLL, close the game and copy it over
`BepInEx/TGNeuralRendering/Native/DLSS/nvngx_dlssnr.dll`, then restart. Do that again after updating the mod.

The NVIDIA-signed 310.9.16.0 and 310.8.0.0 DLLs use the normal NVIDIA path. Compatible patched DLLs use a separate
adapter included with the mod. Selection is automatic: no extra setting, separate adapter download or list of
approved community files. Compatible new patches can be swapped in without updating the mod; a change to
the runtime's interface may still need a mod update.

This includes the community patch intended for RTX 2000, 3000 and 4000 cards. I have tested the patched DLL
on my RTX 5080, but have not confirmed it on those older cards. [DLL guidance](docs/nexus-neural-rendering.md).
[Test results and remaining checks](docs/DLSS5-COMPATIBILITY.md). [Implementation](docs/DLSS5-IMPLEMENTATION.md).

Version 1.2.4 of TG Frame Generation, TG Better Upscaling, TG Neural Rendering and the bridge, for the game's Unity 6000.3
(1.2.3 is what is on Nexus Mods until it is uploaded) (bridge and frame generation ABI 0x00010100, upscaling 0x00010400), for both versions of the game: the
standard version (IL2CPP) with BepInEx 6 and the Mono modding branch with BepInEx 5. TG Path Tracing
(1.2.0, ABI 0x00011300) is a prototype for the Mono version only.

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

FSR and XeSS take STP's place. For them the game's upscaling controller sets up the camera exactly as
for STP (dynamic resolution at the mode's render scale, DLSS off), HDRP renders and jitters as for STP,
and where HDRP would run STP, TG Better Upscaling hands the frame's color, depth and motion vectors to the
native side and returns the upscaled image in the texture STP would have written. Everything after
it sees an upscaled frame exactly as it would after STP: HDRP's post-processing, RenoDX, ReShade,
and TG Frame Generation, which then takes HDRP's own depth and motion vectors at render resolution
as it does with STP. The camera's jitter stays HDRP's own: a Halton(2,3) sequence over 1024 frames,
more than the 8 x (output / render width)^2 samples AMD and Intel ask for.

## TG Path Tracing

A prototype of path traced lighting, for the Mono version of the game only. Test builds have run in the
game on an RTX 5080; DirectX Raytracing 1.2, DLSS Ray Reconstruction and AMD FSR Ray Regeneration have
not yet. It is tested outside the game on an RTX 5080: `tests/pathtracing_gpu_test.cpp` boots the
real bridge with the extension, registers a small scene the way the plugin registers FoA's (D3D11
vertex and index buffers in Unity's layout, a texture, materials, a point light, the sun, a sky
cubemap), rasterizes it into targets laid out as HDRP's GBuffer, and traces and composites it through
the same render events; it checks the images and writes them as PNGs.

Path Tracing is On or Off (the Path Tracing config section, also in FoA Mod Manager; Insert turns it on
and off, Shift+Insert cycles the debug views; F8 writes to the log what is under the crosshair and the
lights reaching it). On lights deferred surfaces entirely by the tracer: ReSTIR DI (Bitterli et al.
2020) over the point and spot lights plus the sun, with traced shadows; ReSTIR GI (Ouyang et al. 2021)
with temporal and spatial reuse for the light that bounces off the scene, each path vertex sampling the
sun and the lights, and the path ending on a radiance cache that carries the light of every bounce after its
last (one bounce by default), the sky's too; and traced reflections. HDRP's own indirect
diffuse light (probes, lightmaps, SSGI) is off while tracing, and only lights that stand for something that
emits light (a flame, a candle, a lamp, a magic effect) are traced: the ones placed to fake the light raster
rendering lacks (fill lights, the light that follows the player) are left out. HDRP's emission is kept. Forward-lit
surfaces (hair, eyes), transparents, particles and fog keep HDRP's lighting, with the game's own
ambient light; hair and eyes darkened by the traced shadows of the lights HDRP casts no shadows for.

How it fits into HDRP's frame: a Harmony prefix on HDRP's opaque `RenderSSR`, which runs right after
the GBuffer, records a custom pass that hands GBuffers 0 to 3, depth with stencil, motion vectors and
the exposure to the bridge's Direct3D 12 device. There, compute shaders with inline ray tracing
(DirectX Raytracing 1.1, Shader Model 6.6) trace the scene near the camera on an async compute queue,
alongside HDRP's shadows and lighting. The camera's rays carry the sub-pixel shift HDRP gives its
camera that frame and tells its upscaler. The result is denoised (SVGF-style temporal accumulation and
a-trous filtering) and laid into HDRP's color buffer in that same frame by a custom pass after opaque
lighting and the sky, before fog: Direct3D 11 waits there on the GPU for the frame's trace, and each
pixel takes its own texel. (Until 2026-10-08 the light was applied a frame later, moved by that frame's
motion vectors, so that Direct3D 11 never waited for Direct3D 12: the upscaler then got samples that
did not lie where it was told, and every traced edge came out as a stair of render pixels.)

The traced scene is rebuilt from what the game draws near the camera every two seconds: GameObjects
with mesh renderers, the world Awaken's ECS renderer (Drake) draws, the trees and large objects
Vegetation Studio Pro places (with alpha-tested leaves), and Unity terrains (as heightmap grids), one
LOD per group or instance. Each mesh is read back from its GPU buffers once, converted to compact
positions and UVs on a worker thread and given its own bottom-level acceleration structure. Materials
keep their base color and texture (baked into a 64x64 tile of a shared atlas), emission and alpha
testing, and their metallic and smoothness, texel by texel from the mask map where the tracer knows how the
material's shader reads it (HDRP Lit's and the game's own); a metal drawn forward has no diffuse color, as in
HDRP's metallic workflow, and where a path meets one it sends its reflected light on as diffuse light (its base color). Lights are HDRP's point and spot lights and its sun, in HDRP's units; rays that escape
sample HDRP's sky reflection. Not traced, though HDRP still draws and lights them: statically batched
renderers and particles. They
cast no traced shadows and bounce no traced light.

Where the GPU and driver offer DirectX Raytracing 1.2 (Shader Model 6.9 and raytracing tier 1.2, for
example a GeForce RTX 40 or 50 with a current driver), every pass runs its Shader Model 6.9 build:
alpha-tested meshes (leaves, fences) get opacity micromaps, which settle most of their triangles'
texels ahead of the alpha test, and on GPUs that reorder (Shader Execution Reordering) the GI and
reflection passes regroup their threads by the material their rays hit. Windows' own Direct3D 12
runtime is often too old for Shader Model 6.9, so TG Path Tracing ships Microsoft's Agility SDK runtime
(`Native/D3D12/D3D12Core.dll`, 1.619) and TG Graphics Bridge points the game's Direct3D 12 at it before
anything loads Direct3D 12, when it is newer than both Windows' runtime and the game's own request.
The micromaps change no pixel: outside the game the same frames with and without them are identical.

The options are rows of the game's Video settings, after the upscaling and frame generation rows:
Path Tracing (Off, On) and, nested under it while it is on, Denoising, Exposure, Dark Adaptation,
Rays Per Pixel, Bounces, Radiance Cache, Indirect Color, Reflections, Caustics, Fill Light Fog, Spatial Samples,
Boiling Filter, Emissive Lights, Crevice Light, Sky Intensity, Reflection Boost and Debug View.
FoA Mod Manager lists exactly the same rows; the hotkeys are in the config only.

I'm moving water into the traced picture, with the game's waves, reflections, refraction and absorption. The waves
are real triangles in the traced scene, moved every frame by the game's own wave data, so the camera, mirrors, bounced
light and shadows all meet the same water. The separate Water Reflections row is gone. This work is still being
checked; the remaining gaps and build status are in [the water report](docs/PT-DETAILS.md).

Caustics (On by default): the sun and lamps shining through a glass bottle, flask or potion are bent and focused by
it into a bright spot or line in its shadow (manifold next event estimation on the glass's smooth surface). Sunlight
reaching a lake bed or a pool's floor bends through the waves. Light reflected from those waves can reach a bridge,
boat or overhanging rock. The tracer follows those light paths; it does not use the game's caustic pattern texture.

The Denoising row lists what this PC runs: None; NRD, NVIDIA's Real-Time Denoisers (RELAX, on any GPU with
DirectX Raytracing); Ray Regeneration, AMD's FSR Ray Regeneration (Radeon RX 9000, AMD's signed DLLs in
`Native/FidelityFX`); and Ray Reconstruction, NVIDIA's DLSS Ray Reconstruction (RTX, with TG Better Upscaling
installed, until NVIDIA's library says it doesn't run here), which denoises and upscales in one step while DLSS is
the Upscaling setting (with another upscaler NRD serves, and the row's tooltip says so; so it does while Ray
Regeneration can't). Denoising starts on the best one: Ray Reconstruction, then Ray Regeneration, then NRD; Restore
Defaults picks the same. Until the player picks another it follows the PC (a new GPU); a pick of their own
stays while the PC runs it. Ray Reconstruction works alongside RenoDX's DLSS 5.

Indirect Color scales how much color the bounced light keeps from the surfaces it bounced off, at the same
brightness: 1 as traced, 0 grey, more exaggerates the tint (a red cloth reddening the floor beside it); the
default is 3. Reflection Boost makes every surface glossier and more reflective than the game made it: 0 leaves the
materials as they are, the default 0.1 adds a little, 1 turns them nearly into mirrors to judge the traced reflections.

Emissive Lights (on by default) samples glowing surfaces (embers, lit windows, magic crystals) as lights, as it
does lamps, so the light they cast is far less noisy; off, only the rays that happen to meet them see it. A
glowing surface right around a kept light (a lantern's glass round its candle) lights nothing itself: the light
has its light. The Emissive debug view shows which glowing surface is which.

Characters are traced too. The game skins them on the GPU (its Kandra renderer) into one buffer each frame;
the renderers within 100 m of the camera (the nearest first, at most 64, a character being several) are copied
from it to Direct3D 12 every frame and their acceleration structures refitted to the pose (rebuilt every fourth
frame), so they shadow themselves and the floor and block the light that bounces around them. Their triangles are
read back once (only the submeshes the game draws), with their texture coordinates, so torn cloth edges are cut
out by their texture's alpha as HDRP draws them (skin never is), and hair stops a ray as much as its strands
cover a texel, casting soft shadows; transparent parts are left out of the traced scene, and eyes are seen by camera
rays only. Hair and eyes are shaded by the tracer, their colors baked from the game's own hair and eye shaders; while
tracing, hair, eyebrows and lashes stay opaque as the game draws hair far away, and the eyes' transparent cornea is
hidden. Thin skin lit from behind (ears, nostrils, fingers) lets through the light HDRP's transmission does, where
the traced geometry is thin enough.
Rays start from the surfaces HDRP drew, which can lie a few centimetres inside the traced meshes (an arch drawn
smoother than its triangles): a ray that first meets the inside of a mesh within about 15 cm passes out through
it rather than finding no light. Lamps cast shadows as sharp as their HDRP Radius makes them.
Large static sets drawn by Awaken's Medusa renderer are traced from its own instance data (read once per scene
from its archive, at the LOD it draws), and the entities Entities Graphics draws without Drake (the castle's
walls, arches and battlements) from its own mesh and material registry. Shadow-only meshes (the castle's
cluster meshes, coarser copies of its walls the game casts shadows with) are left out, the drawn walls casting
the shadows; only if Entities Graphics can't be read do they stand in for them. Meshes on layers the camera
leaves out are not traced. Vegetation drawn by the game's Leshy renderer (grass, plants, bushes, ivy,
trees) is traced from its loaded cells near the camera: grass and plants within 20 m, at most 60,000 instances. The scene is gathered every 2 seconds, and after half a second once the camera has moved 4 m, so the level of
detail traced keeps up with the one drawn.
Alpha-tested materials (straw, rope nets, leaves) are tested against their texture's alpha at 256x256, in an
atlas of their own; the 64x64 color tile made coarse opaque blocks of them (black patches under straw).

How the three mods share NVIDIA's DLSS library (NGX) and Direct3D 12:

- TG Better Upscaling starts NGX for Ray Reconstruction only when TG Path Tracing asks for it (tracing,
  with Denoising on Ray Reconstruction), never at start-up; without that, NGX sees exactly what it saw
  before (the game's own DLSS, frame generation's Streamline, a DLSS mod such as RenoDX's DLSS 5).
- RenoDX's DLSS 5 (ReShade add-on) hooks NGX's calls, Ray Reconstruction's included, and works alongside
  it. At its Present hook stage it processes the finished frame, menus included.
- NGX keeps one session per Direct3D 12 device. With TG Frame Generation installed, that is Streamline's
  on the shared device, which only searches Streamline's folders for NVIDIA's DLLs. With TG Path Tracing
  and TG Better Upscaling installed too, TG Frame Generation adds TG Better Upscaling's `Native/DLSS`
  folder after its own, so Ray Reconstruction is found; DLSS Frame Generation still loads its own files.
- The Agility SDK runtime TG Path Tracing ships serves every Direct3D 12 device in the game's process.
  Should Direct3D 12 fail to create a device with it, the bridge notes that, and the next start uses
  Windows' or the game's runtime as without TG Path Tracing (on Windows 10 that includes the game's
  610/611 correction); a newer runtime or Windows update tries it again.

It needs DirectX Raytracing 1.1 and Shader Model 6.6 (GeForce RTX 20, Radeon RX 6000, Intel Arc or
newer, with a recent driver), about 300 MB of video memory at a 1080p render resolution plus the
scene's geometry. The debug views show what the tracer sees: Albedo, Normals, Coverage (every traced
object in its own color, dark where HDRP draws something the traced scene lacks), Materials (the
traced scene's colors), Indirect, Indirect raw and Direct. `BepInEx/TGPathTracing/Native/TGPathTracing.Native.log`
summarizes every 10 s (GPU time, render-thread time by part, meshes, objects, triangles, memory), and
BepInEx's log has the scene capture and the HDRP hook's counts.

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
| DLSS-G | Streamline 2.14.16 DLSS-G: fixed multipliers, `eDynamic` with `dynamicTargetFrameRate`, Reflex required. |
| FSR | AMD's signed loader, frame generation and upscaler DLLs: FSR 4 on Radeon RX 6000 and newer and some Ryzen processors, FSR 3.1 elsewhere; the version list comes from the runtime. |
| XeSS | Intel's signed `libxess.dll` (upscaling), `libxess_fg.dll` and `libxell.dll` (frame generation). |
| DLSS DLL | NVIDIA's signed `nvngx_dlss.dll` 310.9.16 from the DLSS SDK, for the game's own DLSS when the game folder's is older. |
| Upscaling inputs | HDRP's STP inputs: pre-exposed linear color, reversed-Z depth, motion vectors scaled to render pixels, HDRP's jitter negated as the SDKs expect, HDRP's own history reset. FSR gets HDRP's exposure as its pre-exposure and exposure, as HDRP's DLSS pass takes it. Texture mip bias as each vendor asks: HDRP's `log2(render / output)` for XeSS, one mip more for FSR. |
| Synchronization | GPU fences between the D3D11 and D3D12 queues; every wait has a matching signal. The CPU never reads an image back. |
| HDR | FSR and XeSS upscale HDR color before tonemapping. Frame generation encodes FP16 scRGB to RGB10A2 PQ/BT.2020 for its swapchain. |

## Building

Windows x64 with the Visual Studio C++ build tools, CMake 3.24 or newer, Windows SDK 10.0.22621 or
newer, and a .NET SDK that builds `net472` and `net6.0`. The SDKs go next to this repository (they
are git-ignored):

| Folder | Contents |
|---|---|
| `Streamline-2.14.16` | NVIDIA Streamline 2.14.16 source: headers and `external/ngx-sdk/include` |
| `Streamline-2.14.16-bin` | `bin/x64` and `license.txt` from the `streamline-sdk-v2.14.16.zip` release |
| `FidelityFX-SDK-2.4.0` | AMD FSR SDK 2.4.0 headers and, in `bin/`, the signed `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_framegeneration_dx12.dll` and `amd_fidelityfx_upscaler_dx12.dll` |
| `XeSS-SDK-3.0.2` | Intel XeSS SDK 3.0.2 release archive |
| `AntiLag2-SDK-2.0.0a` | AMD Anti-Lag 2 SDK 2.0.0a release archive |
| `DLSS-SDK-310.9.16` | `lib/Windows_x86_64/rel/nvngx_dlss.dll` and `LICENSE.txt` from NVIDIA's DLSS SDK 310.9.16 |
| `RTXDI-Library-3.1` | NVIDIA's RTXDI runtime library 3.1 (`git clone https://github.com/NVIDIA-RTX/RTXDI-Library`), compiled into TG Path Tracing |
| `NVAPI-R615` | NVIDIA's NVAPI R615 (`git clone https://github.com/NVIDIA/nvapi`): its header and `amd64/nvapi64.lib`. Needed only for a build with `-DTGPT_MEGA_GEOMETRY=ON` (TG Path Tracing's RTX Mega Geometry path, in testing); a build without that switch does not look for it |

CMake fetches MinHook v1.3.4 unless `-MinHookRoot` is given. The game needs the matching BepInEx
already installed. `docs/SOURCES.md` links every SDK release.

```powershell
# Native DLLs: TGGraphicsBridge.Native, TGFG.Native, TGBetterUpscaling.Native, TGPathTracing.Native (VS x64 developer shell)
.\Build-Native.ps1 -StreamlineRoot Streamline-2.14.16 `
  -NgxIncludeDir Streamline-2.14.16\external\ngx-sdk\include

# Signed NVIDIA, AMD and Intel runtimes with their license notices, into each mod's folder
.\Stage-Runtime.ps1 -RuntimeDirectory Streamline-2.14.16-bin\bin\x64 `
  -FidelityFXRoot FidelityFX-SDK-2.4.0 -XeSSRoot XeSS-SDK-3.0.2 -DlssRoot DLSS-SDK-310.9.16

# Plugins and preloaders, then package and install the mods (game closed)
.\Build.ps1 -Runtime Mono `
  -GameDirMono 'C:\Program Files (x86)\Steam\steamapps\common\Tainted Grail FoA' -Deploy
```

For the standard version, use `-Runtime IL2CPP -GameDirIl2Cpp <game folder>`. BepInEx 6 has to have
started the game once, so that its interop assemblies exist.

`-Mod TGFrameGeneration|TGBetterUpscaling|TGPathTracing` limits packaging and deployment to one mod (refused when
another installed mod carries a different build of the same bridge version). TG Path Tracing is built and
packaged for Mono only; an IL2CPP build skips it. Its path tracing passes are compiled at build time by the
Windows SDK's `dxc.exe`.
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

## Freeing disk space

Three folders keep growing. The in-game test runs (`docs/PT-HARNESS.md`) land in `artifacts/harness`, and their
raw frames took 307 GB after three days. Every packaging run leaves a folder and a zip per mod in
`artifacts/Packages`: 62 GB after two weeks. The parallel workers (`docs/AGENT-WORKERS.md`) build and test in
`%TEMP%\tgb`: 145 GB after six days. `tools/harness/Clear-PtHarnessRuns.ps1` clears all three:

```powershell
# What would go and how much space that frees. Deletes nothing.
.\tools\harness\Clear-PtHarnessRuns.ps1

# The same, with a line for every run
.\tools\harness\Clear-PtHarnessRuns.ps1 -List

# Delete it
.\tools\harness\Clear-PtHarnessRuns.ps1 -Apply

# Delete it, but leave the neural rendering runs alone
.\tools\harness\Clear-PtHarnessRuns.ps1 -Keep 'nr-*' -Apply

# Keep only the newest 3 runs whole (the default is 10)
.\tools\harness\Clear-PtHarnessRuns.ps1 -KeepWhole 3 -Apply

# Delete every old package, the newest ones too (the default keeps 1 of each)
.\tools\harness\Clear-PtHarnessRuns.ps1 -KeepPackages 0 -Apply

# Delete the workers' folders nothing was written to for half a day (the default is 2 days; 0 leaves them alone)
.\tools\harness\Clear-PtHarnessRuns.ps1 -WorkerDays 0.5 -Apply
```

The newest 10 runs stay as they are. Older runs lose their raw frames (`.raw`, `.hdrraw`, `.bin`) and keep
their pictures, reports and logs. Runs older than 14 days are deleted whole unless a doc names them;
`-DeleteAfterDays 0` turns that off. Of the packages, the newest of each mod, version and game version stays,
folder and zip. The zips for Nexus in `artifacts/Nexus` are never touched. Of the workers' folders in
`%TEMP%\tgb`, those nothing was written to for 2 days go; the workers' tree copies are never touched. The script
leaves `builds` and `_config-backup` alone, and deletes nothing while a test run is going.

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
- TG Path Tracing traces only what it captures (see its section): statically batched renderers,
  particles are missing from the traced scene. Forward-lit surfaces (hair, eyes) keep HDRP's
  lighting rather than the traced light. Meshes stay registered as the camera travels; past 2.5 GB of traced geometry the traced scene
  starts over from what is near the camera.

`docs/ARCHITECTURE.md`, `docs/TESTING.md`, `docs/SOURCES.md` and `docs/AUTOLOAD-INTEGRATION.md`
cover the implementation, the test plan and the primary sources. `docs/FSR-FLICKER-INVESTIGATION.md`
records the resolved HUD/hand flicker investigation; read it before changing the frame generation
input path.

## Development documentation

Current work: [BACKLOG.md](BACKLOG.md). Path Tracing: [status](docs/PT-STATE.md), [issue register](docs/PT-REQUESTS.md).
Build inputs: [dependencies](docs/BUILD-DEPENDENCIES.md). [Contributing](CONTRIBUTING.md) and [public source preparation](docs/PUBLIC-SOURCE.md).

## License

The source is MIT-licensed. The NVIDIA, AMD and Intel runtimes in a package keep their own licenses,
which ship next to them; see `LICENSES/THIRD_PARTY.md`.

TG Path Tracing's native DLL contains NVIDIA's RTXDI library, under the NVIDIA RTX SDKs License, which allows it
in object code only; it is never part of this repository. This software contains source code provided by NVIDIA
Corporation.
