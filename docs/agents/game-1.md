# game-1: what the game still renders and holds under the traced picture (PT-107, PT-87)

Worker `game1` (copy `wt\game1`, base `base77`), 2026-10-09. Plugin only (`src/TGPathTracing`). Never ran the game.

## State when stopped (20:23, a few minutes in: the coordinator stopped every worker, the owner's usage limit)

- **Built: nothing.** No source file of the tree is changed. This report is the only file the copy holds that the
  base does not. The switch `Advanced / Game passes while tracing` does not exist yet. No recipe was written.
- **Done:** the reading (below), the game's pipeline asset and quality settings read out of the game's data files
  (section 1: these numbers are READ), the frame's order of passes read from HDRP (section 2).
- **Half done:** the inventory table. Its sizes in section 3 are RECKONED from the read settings (pixels times
  bytes), and one fact that decides most of them is NOT checked (the first open question of section 4).
- **Not started:** the first cut (job item 2), the atlases that are kept (job item 3), the build, the source checks.
- **The next step, for a fresh worker:** answer the three open questions of section 4 from the decompiled HDRP
  (it is already in the scratch folder: section 5), finish the table, then build the cut of section 6.

## What the plugin already stops while tracing (read from the code, base77)

A Volume of our own (`HdrpPasses.Initialize`, priority 1,000,000, on only while the traced light is on screen,
`Session.Tick` -> `HdrpPasses.SetIndirectOverride`):
- indirect diffuse lighting multiplier 0 (probes' and lightmaps' diffuse light);
- screen-space global illumination off; ambient occlusion's intensity 0; contact shadows off;
  screen-space reflections off for opaque surfaces (transparent ones keep the game's);
- a second Volume scales the reflection multiplier by the light at the camera.
`SceneLights.DarkenPlaced`: the placed fill lights' `lightDimmer` 0 (their fog part kept by the Fill Light Fog row).
`WaterCaustics`: HDRP's own water caustics off. `EffectPasses`: distortion passes of effect materials off.

NOT stopped today: HDRP's shadow maps (`HdrpPasses.ShadowMaps` only READS the camera's frame setting, and tells
the tracer which lights HDRP shadows itself, so that hair and eyes take no shadow twice), the deferred lighting
pass (its result is replaced by the composite), subsurface scattering's pass, reflection probes, the colour
pyramid, the volumetric fog (kept on purpose).

## 1. The game's own settings, READ from its data files

Read with UnityPy from `Fall of Avalon_Data\globalgamemanagers.assets` (read only; the JSON is in the scratch
folder, section 5). Two quality levels: "Ultra" (the PC's; pipeline asset `HDRenderPipelineAsset`, path id 11609)
and "SteamDeckPerformance" (asset `HDRP_Xbox_Performance`, 11608). The Ultra asset:

| Setting | Value read |
|---|---|
| Lit shader mode | deferred only; no MSAA; colour buffer RGBA16F (8 bytes a pixel); custom pass buffer RGBA16F |
| Shadows: sun | 32-bit depth; resolutions 256 / 512 / 1024 / 2048; the largest 2048 |
| Shadows: punctual lights' atlas | 2048 x 2048, 32-bit depth, viewports rescaled to fit |
| Shadows: area lights' atlas | 2048 x 2048, 32-bit depth |
| Shadows: the cached atlases | punctual 1024, area 1024; no mixed cached sun shadows |
| Shadows: most requests a frame | 1024; screen-space shadows supported, 4 slots, RGBA16F |
| Light cookies' atlas | 2048 x 2048, 4 bytes a pixel (B10G11R11) |
| Reflection probes' atlas | 2048 x 1024, 4 bytes a pixel, cube mips down to 3; at most 48 cube and 16 planar probes on screen |
| Decal atlas | 128 x 128 (tiny); decal layers on; at most 256 decals on screen |
| Lights on screen at most | 1024 punctual, 64 area, 16 directional |
| Supported | SSR (opaque and transparent), SSAO, SSGI, subsurface scattering (20 / 40 / 80 samples), volumetric fog, volumetric clouds, water (64, water decals' atlas 1024), distortion, low-resolution transparents |
| Not in use | HDRP's ray tracing (off), adaptive probe volumes (the old light probes), the GPU resident drawer, shadow masks, thickness |
| Dynamic resolution | on; upscalers in order DLSS, FSR 2, STP |
| Unity quality "Ultra" | mip streaming ON, **budget 1,548 MB**, texture limit 0 (full size), shadow distance 300 |

Also dumped, not read yet: the default Volume profile's `HDShadowSettings` (the cascade count and distance),
`Fog`, `AmbientOcclusion`, `ScreenSpaceReflection`, `ContactShadows`, `SubSurfaceScattering`, `WaterRendering`.

## 2. What HDRP runs each frame for the game's camera, in order (READ from HDRP's frame recording)

Prepass (depth, motion vectors, decals' DBuffer, the G-buffer) - the light lists - a history-validation buffer -
ambient occlusion - contact shadows - opaque SSR (our capture is recorded right before it) - **the shadow maps** -
SSGI - screen-space shadows - **deferred lighting** - forward opaque - **subsurface scattering** - the sky - custom
passes "after opaque and sky" (our composite is here) - the transparent prepass - the fog's voxelization and
**volumetric lighting** - the opaque fog - clouds - transparents (with their SSR, water, low-resolution
transparents) - the interface - distortion with a colour pyramid - custom passes before post - post-processing.

So everything between the capture and the composite that only feeds HDRP's lit colour of deferred surfaces is
drawn and then written over: the shadow maps as far as only deferred surfaces use them, the deferred lighting
itself, subsurface scattering. What is drawn forward and shown (hair's layer, eyes, effects, water's own, the
fog) still reads the light lists, the shadow maps and the probes.

## 3. The inventory as far as it is (sizes RECKONED: pixels times bytes; none measured)

| What | Size, MB | Needed while tracing? | Stopping it gives |
|---|---|---|---|
| Sun's cascade atlas | 64 if four cascades of 2048 (4096 x 4096 x 4 bytes); the cascade count is not read yet | only by what HDRP still shades (hair, eyes, effects, transparents) and by the fog's light shafts | ms not known; MB only if the atlas is let go |
| Punctual lights' shadow atlas | 16 | the same | the same |
| Area lights' shadow atlas | 16, more if it keeps blurred copies (not checked) | the same | the same |
| Cached shadow atlases | 4 + 4 | the same | kept from pipeline start |
| Light cookies' atlas | 16 to 21 | yes: the fog's lights, forward surfaces | nothing |
| Reflection probes' atlas | 8 to 11 | yes for forward surfaces and transparents | nothing |
| Decal atlas | under 1 | yes (the G-buffer the tracer is checked against) | nothing |
| One full-screen RGBA16F target at 3840 x 2160 | 63.3 (31.6 for a 4-byte format) | - | the unit for everything below |
| HDRP's screen targets: colour, G-buffer 0 to 3, depth and its pyramid, normals, motion, DBuffer, lighting buffers, colour pyramid and its history, TAA or upscaler history, volumetric buffers and their history, post-processing's | NOT reckoned: depends on question 1 below (a factor of 4) | the G-buffer, depth, motion, the fog's, post: yes | |
| Streamed textures | up to 1,548 (the budget) | yes: the tracer reads the game's own textures | nothing without blurring the traced picture |
| Everything else of the 9 GB | not known | | |

**What this already says (reckoned, not measured):** HDRP's fixed atlases are about 130 to 140 MB together.
They are NOT where the game's 9 GB is. Stopping passes can give back at most what HDRP's screen targets and
histories hold; how much that is hangs on question 1. The rest (textures that are not streamed, meshes, the other
mods' memory) no frame setting gives back. A fresh worker should expect hundreds of MB from this job, not
gigabytes, and should say so early.

## 4. Open questions, each to be settled from the decompiled HDRP before the table is done

1. **At which size does HDRP allocate its screen targets while DLSS upscales to 4K: 3840 x 2160 or the render
   size (1924 x 1084)?** The tracer's log gives the render size; HDRP's software dynamic resolution usually
   allocates at the full size and draws into a part of it. This decides every full-screen figure by a factor of
   4. Read `HDCamera` (its sizes and `RTHandles` reference size) and `DynamicResolutionHandler` in `core`.
2. **Which of HDRP's textures are kept for ever and which are let go by themselves?** The render graph's pooled
   textures are freed some frames after their pass stops being recorded (find the lifetime in `core`,
   `RenderGraphResourcePool`); the camera's history buffers, the cached shadow atlases, the cookie and probe
   atlases are kept. If the shadow atlases are pooled, turning shadow maps off gives their memory back without
   any rebuild. Read `HDShadowManager`, `HDShadowAtlas`, `HDCachedShadowManager`, `HDCamera` (history buffers).
3. **Does a light with `lightDimmer` 0 still render its shadow map?** The fill lights the plugin darkens may
   still cost shadow draws. Read `HDProcessedVisibleLightsBuilder` / `HDGpuLightsBuilder`.

## 5. Scratch files a fresh worker can use at once (none of it is in the tree; read only)

In `...\scratchpad\game1-work\`:
- `hdrp\` (678 files) and `core\`: the game's HDRP and Core runtime, decompiled with `ilspycmd -p`.
  `hdrp\UnityEngine.Rendering.HighDefinition\HDRenderPipeline.cs` is 1.1 MB: grep, then a range. The frame's
  recording is `RecordRenderGraph` at line 18563; `RenderShadows` 12712, `RenderDeferredLighting` 12815,
  `RenderSubsurfaceScattering` 20674, `RenderSSR` 12958, `GenerateColorPyramid` 19920.
- `asset\mb_11609.json` (the Ultra pipeline asset), `mb_11608.json` (the other), `QualitySettings.json`,
  `GraphicsSettings.json`, `mb_11634.json` (shadow settings), `mb_11635.json` (fog), and five more Volume
  components; `monobehaviours.txt` lists the file's 72 objects by id.
- `scripts\dump_ids.py <Data folder> <out> <pylib> <UnityPy folder> <ids...>`: how they were read. It needs
  `game1-work\pylib` (TypeTreeGeneratorAPI) and `scratchpad\pytools-unity` (UnityPy); run with `python -I`.
  Ids 11610 (HDRP's global settings: the default frame settings) and 11623 failed or were not checked.

## 6. The first cut as planned (NOT built, NOT checked: a plan, not a finding)

Behind a hidden `Advanced / Game passes while tracing` (0 as now, 1 the cut, 2 with memory let go), applied and
taken back where `HdrpPasses.SetIndirectOverride(on)` already runs (it is called every frame from `Session.Tick`
and with `false` from `Session.Stop` and `HdrpPasses.Fail`), through the game camera's
`HDAdditionalCameraData` custom frame settings and their override mask, each saved before and put back after:

| Candidate | Why it may go | What must be checked first |
|---|---|---|
| Subsurface scattering (frame setting) | it blurs HDRP's lit skin, which the composite then writes over | that no forward-drawn surface that is shown uses it; that the G-buffer's skin data the tracer reads does not change |
| Shadow maps (frame setting) | deferred surfaces' shadows are traced | NOT certain: the fog's light shafts read the shadow maps (the fog must stay as the game's), and hair and eyes are still HDRP's. `HdrpPasses.ShadowMaps` already tells the tracer when they go, so traced shadows would take over on hair and eyes. Likely a choice for the owner, not part of value 1 |
| The darkened fill lights' shadows | a light at dimmer 0 lights no surface | question 3; their fog part must keep its shadows if the fog shows them |
| Deferred lighting itself | its result is replaced | no frame setting found yet that stops it alone; not looked for |

The recipe asked for (scene `metal`: the game; traced with the switch 0, then 1, NRD, 16 frames each; off; the
game again) is not written. Expectation in `notTracer` and frames per 10 s: cannot be given before questions 1
and 2 are answered.

## What could break (known so far)

- Shadow maps off changes the game's fog (no light shafts): against the ruling "Keep the game's fog".
- A frame setting left overridden after tracing is off changes the game's picture: every one must be restored in
  the same call that turns the Volume off, and after a camera is re-created (the override lives on the camera's
  `HDAdditionalCameraData`, so a new camera starts clean but the old one must not keep ours).
