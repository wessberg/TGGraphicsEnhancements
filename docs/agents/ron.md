# Ron's hair, brows and eyes: Ray Reconstruction's guides (PT-01, focus 2)

Agent "ron", 2026-10-06. Copy of the tree: `scratchpad\wt\ron` (base `wt\base10`). Job: Ray Reconstruction's (RR's) guides at
hair, brow and eye pixels only. Not the texture sizes (another agent), not the skin and eye shading (PT-43, PT-44).

The owner: "he certainly looks a lot better. But it still seems blurry/low-res and not as finely detailed as the vanilla
game, including his brows and eyes". Measured (fine detail against the game's picture, `detail_boxes.py`): crown 0.36 with
RR, 0.63 without a denoiser; eyes 0.68 and 0.94; cheek 0.74 and 0.92. RR's guides are the pictures that tell it what the
surface is: its colour (diffuse albedo), how much it mirrors (specular albedo), which way it faces (normal) and how rough
it is. RR treats light that the guides don't explain as noise and smooths it.

## 1. What each guide held at a hair, brow and eye pixel (before)

The guides pass (`d3d11.hlsl` `TGPT_GUIDES`) writes them from HDRP's GBuffer each frame; the composite corrected only
see-through glass.

| Guide | Hair or brow drawn as a layer (`pt_hair`) | Eye (HDRP draws it forward, the tracer shades it) |
|---|---|---|
| Diffuse albedo | the skin, scalp or wall behind the hair (the plugin hides HDRP's hair passes, so the GBuffer holds what is behind), leaning to the skin's GBuffer3 light | 0.5 grey (the GBuffer holds nothing of a forward surface) |
| Specular albedo | the skin's or wall's (its f0, HDRP's roughness, the view angle) | a 0.04 dielectric's at HDRP's roughness |
| Normal | the skin's or wall's | HDRP's normal buffer (the eye's own) |
| Roughness | the skin's or wall's | HDRP's normal buffer |
| Hit distance | 0 where the hair covers more than half the pixel (`pt_hair` blend), else the surface's reflection's | the eye's reflection's |
| Depth (RR's) | HDRP's: the skin or wall | the eye's |

The colour RR gets at a layer pixel is the skin's light times the share the hair leaves plus the hair's own light. With the
skin's guides, the strands' light looked like noise on skin: RR smoothed it away. At an eye, the iris's colour looked like
noise on a grey surface.

## 2. What NVIDIA's RR guide asks

`DLSS-SDK-310.9.1/doc/DLSS-RR Integration Guide.pdf` has no hair or "subsurface" material hint; the NGX header's only
related inputs are the screen-space subsurface scattering guide and the transparency layer, neither of which fits a layer
the tracer lights itself. What it does say:

- 3.4.1 (diffuse albedo): "For pixels where overlaid transparent surface color dominates ... it may be beneficial to blend
  albedos, normal, and roughness guides towards transparent surface values", "should not result in noisy guide buffers".
  And: "In case the app does not provide specular motion vectors, it is important to set depth, normal, and specular hit
  distance consistently based on either opaque background or transparent surface. Albedos and roughness can still use
  blending."
- 3.4.3 (normals): "If the app relies on RR computing specular motion, these should be World Space, and guide blending ...
  should not be used for this buffer." TG Better Upscaling gives RR a hit distance, no specular motion vectors: so normals
  are switched, never blended.
- 3.5 "Noisy guide buffers": noise in the guides confuses RR; constant values beat noisy ones.
- Appendix: the specular albedo is the lobe's average reflectance at the view angle.

## 3. What the guides hold now

Where the tracer knows what the pixel shows better than the GBuffer, it writes its own guides, and the composite lays them
over the guides pass's, taken from the same traced texel whose light the pixel takes (with RR: the nearest one).

| Guide | Hair or brow drawn as a layer | Eye (and any surface HDRP draws forward that the tracer shades) |
|---|---|---|
| Diffuse albedo | the hair's own colour over the skin's or wall's, blended by the hair's cover (layers summed front to back, as its light is) | the eye's own traced colour |
| Specular albedo | the hair's, blended the same way: hair's reflectance (0.0465) at its roughness and view angle, times its specular map's texel (the game's fine streaks); a scalp or beard mask, or hair without highlights, its material's | the eye's traced finish (its f0, roughness, normal, view angle) |
| Normal | where the hair covers more than half the pixel: the strand's normal facing the camera (the one the tracer lights it about), of the card that covers the most; else the skin's or wall's. Switched, never blended (NVIDIA 3.4.3), at the same 0.5 where the hit distance already switches | the traced normal (HDRP's own for an eye) |
| Roughness | blended by the cover: hair with highlights as rough as the tracer makes hair drawn forward (its second lobe's roughness x 0.4), else its material's | the eye's traced roughness (its wet finish) |
| Hit distance, depth | unchanged | unchanged |

The 0.05 floor on the two albedos (NVIDIA's RTXPT) is kept. Brows go the layer's way (round 4 made them layer hair); a brow
with no detail tile is still held opaque and drawn forward, and gets the forward column (its card's traced colour, mixed
with the skin behind it by the strands' coverage, as `pt_prepare` lights it).

How: two new outputs of the tracer per frame parity, `g0` (sRGB colour, and how much of the pixel the guides describe) and
`g1` (octahedral normal, roughness, square root of the specular albedo), 8 bits a channel, shared with D3D11.

1. `pt_hair` mode 0 (`layer_guides`) writes the layer's guides and its cover, zero where there is no hair.
2. `pt_compose` (`traced_guides`) writes a forward surface's guides with a share of 1, blending the layer's over it where the
   layer ran this frame (an eye under lashes and brows); elsewhere it leaves the layer's, or writes zero when no layer ran.
3. The composite (`d3d11.hlsl traced_guides`, only when RR's extras are written) reads them at the texel it took the light
   from (`g_guideTexel`: the nearest texel with RR, the heaviest bilinear one otherwise, the sky's too for hair against the
   sky) and blends them over the guides pass's by that share: albedos and roughness blend, the normal switches at 0.5.

## 4. Files, ABI, FrameConstants

- `native/pathtracing/shaders/pt_hair.hlsl` (LF): `layer_guides`, the guide sums in `layer`.
- `native/pathtracing/shaders/pt_compose.hlsl` (LF): `traced_guides`, called right after `load_surface`.
- `native/pathtracing/shaders/pt_common.hlsli` (LF): `guide_octahedral`.
- `native/pathtracing/shaders/d3d11.hlsl` (LF): `tracedG0`/`tracedG1` (t10, t11), `g_guideTexel`, `guide_normal`,
  `traced_guides`, its call after `glass_guides`.
- `native/pathtracing/src/tracer.cpp` (LF): `Outputs` gains `g0`, `g1` (R8G8B8A8_UNORM, shared, made and released with the
  others, in the frame's UAV transitions); pt_hair mode 0's constants `{0, -, g0, g1}`; compose's `{rrg, c, nrd, g0, g1,
  layer ran}`; the composite binds 12 SRVs; the size log's per-pixel sum (outputs 2x24 B).
- `tests/pathtracing_gpu_test.cpp` (LF): `readGuide`, the layer-hair guide check (the card's smoothness now 0.9, which its
  light doesn't use), the eye guide check.
- `tests/source_checks.py` (LF): the compose dispatch and the release line updated; the new wiring pinned in
  `test_path_tracing_glass_tint_and_guides`.
- `CHANGELOG.md` (CRLF kept): the entry at the top of the path tracing part.

No export, no `TGPT_ABI` change, no `FrameConstants` change (the new indices go in the passes' own eight constants). No new
option bit. Memory: 2 x 8 bytes a pixel more (about 33 MB at 1924x1084).

## 5. Offline checks

The offline harness never runs RR (AGENTS.md); these read the guide textures RR is handed (`TGPT_GetGuides`) back.

- `tgpt_gpu_test` on this build (`%TEMP%\tgb\ron-native`, output `scratchpad\ron\gpu2.txt`): all path tracing checks
  passed (139 s). The new ones:
  - Layer hair (`46`'s red checkered card 2 cm before a grey wall HDRP draws, frames asking for RR's guides): the diffuse
    guide differs from the albedo view's blend of hair and wall by 0.001 on average over the card's 1764 pixels; on the
    strands 0.800 0.051 0.039 (the card's red), between them 0.762 0.610 0.608; roughness 0.10 on the strands (the card's
    0.9 smoothness), 0.58 where the wall is bare (HDRP's 0.6); unit normals, a dielectric's specular albedo.
  - Eye (the character's cube with a blue eye material, 0.10 0.30 0.70, smoothness 0.9, on the box HDRP draws forward):
    diffuse 0.098 0.302 0.702, roughness 0.102, specular 0.043, unit normals.
- Mutation checks (`scratchpad\ron\mutate.ps1`, each built to its own prefix, then restored and rebuilt; the restored
  shaders' hashes equal the originals):
  - A, the composite never calls `traced_guides` (`gpuA.txt`): 5 checks fail. Over the card the guide is the wall's grey
    0.753 everywhere, roughness 0.60; the eye's 0.498 grey with roughness 0.600: what the guides held before.
  - BC, `pt_hair` writes no layer guides and `pt_compose` none for a forward surface (`gpuBC.txt`): the same 5 fail.
- Unchanged checks still pass: the layer's look (643 red, 1121 grey), the shell (754, 746, 0), the red wall's and glass's
  guides, the leaf's specular guide.
- `python tests\source_checks.py`: 80 of 80 (two pinned lines updated: the compose dispatch and the release line; the new
  wiring pinned).
- The D3D11 shaders compile with fxc (`cs_5_0` composite, `ps_5_0` raster composite, the guides pass); fxc's one warning
  (X4000 in `composite`) is there on the base too.

## 6. What the next harness run should show

`.\tools\harness\Run-PtHarness.ps1 -Scenario hair-eyebrows -Recipe views` (after a deploy of this build), then the same boxes:
`python <scratchpad>\detail_boxes.py crown=0.465,0.36,0.53,0.41 eyes=0.47,0.435,0.51,0.465 cheek=0.475,0.47,0.50,0.51 -- <game 1-game>
<run>\hair-eyebrows\02-traced-rr <run>\hair-eyebrows\12-traced-no-denoiser`.

1. With Ray Reconstruction (`02`): the crown's number rises from 0.36 toward the undenoised 0.63, the eyes' from 0.68 toward
   0.94; the cheek's (0.74, undenoised 0.92) less, as it is mostly skin, which this does not change, besides the beard's edge.
2. Without a denoiser (`12`) and with NRD: the same as before (only RR's guides changed).
3. The debug views (albedo, normals, surfaces) are the tracer's own pictures, not RR's guides: unchanged.
4. `LogOutput.log` / the TGPT log: "Render size ...: tracer textures and reservoirs recreated (about N MB)" is about 33 MB
   more at 1924x1084 than before; no "Ray Reconstruction guides failed" or "composite failed" line.
5. In game, the owner's eye: strands, brows and the iris crisper under RR; nothing else should look different, apart from
   props HDRP draws forward (they now have their own colour in RR's guides too).

If the crown's number does not move: look at the hit distance and the colour before transparency next (the hair's light is
in the "specular" half of the traced light; RR may still treat the layer as a reflection), then at the hair layer's own noise
(one light sample a frame, docs/agents/hair-4.md section 7, item 4).

## 7. Risks

- RR may now keep more of the hair layer's own grain: its light is one light sample a frame (hair-4 section 7, item 4). With
  the right albedo RR smooths less; if the hair boils or sparkles under RR, that grain is the next thing to reduce.
- Every surface HDRP draws forward that the tracer shades (props, held brow cards) gets its traced guides, not only the
  eyes: better by NVIDIA's guide (the GBuffer has nothing there; it was grey 0.5 and a dielectric's 0.04), but not seen in
  game. The specular albedo guide is one grey number: a forward metal loses its tint in the guide (it had none before).
- The normal switches from the skin's to the hair's where the hair covers half the pixel: a hard line along the hair's
  outline in the normal guide, as NVIDIA asks (no blended normals with a hit distance and no specular motion vectors).
- The guides come from the previous frame's traced texel, as the light does: in fast motion, for a frame, the hair's
  guide can sit one texel off the GBuffer's. Disoccluded pixels take the guide of the texel whose light they borrow.
- Memory: about 33 MB more at 1924x1084 (two 8-bit textures a parity).
- AMD Ray Regeneration's guides (`pt_rrg_guides`) still describe the surface under the layer: not changed (RR only).
- Without RR nothing changes: the composite writes the guides only for frames that ask for them.
