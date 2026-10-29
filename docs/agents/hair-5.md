# PT-53 to PT-56: the traced hair's four defects seen in play (agent "hair-5", 2026-10-07)

Copy `scratchpad\wt\hair5` (base `wt\base29`). Evidence: run `artifacts\harness\20261007-122527` (recipe `hair`), its
`LogOutput.log`, Kandra's decompiled code (`scratchpad\kandra-src`). Crops, scripts and GPU logs: `scratchpad\h5\`.
Built and checked offline only; nothing here ran in the game. Stopped at the coordinator's word (14:00).

## In short

- **PT-53 and PT-56 (hair moves while the camera moves; a still layer with a moving one around it): fixed, ready to
  merge.** The composite moved the hair by HDRP's motion vectors of the surface *behind* it. Not Kandra.
- **PT-54 (black oval, "glow on top"): lead from the code, fix built, do not merge yet.** A card's only bounced light was
  the radiance cache's at its own point, which in game is the scalp's cell under the hair, in the hair's own shade. Now a
  traced bounce. But the offline check does not fail on the old code (mutant: 0.85 of the wall), so nothing offline shows
  the dark cell. And how bright the bounce should be is not settled. Merge PT-53 alone ("How to merge").
- **Hair and eye colours: no colour bug found.** The tracer reads the character's own colours. The traced tavern is lit by
  candles alone, so a white shirt shifts to orange exactly as much as the hair does.
- **PT-55 (brows flicker): located, not fixed.** The flicker is on the brow and lash line, with Ray Reconstruction and with
  no denoiser, not with NRD. Best lead: the lashes, held opaque in HDRP's GBuffer.

## The checks

Every GPU run: `tgpt_gpu_test` from the copy against `wt\hair5\artifacts\Native` with this build's
TGPathTracing.Native.dll (`scratchpad\hair5-gpu.ps1`, logs `scratchpad\h5\gpu-<tag>.txt`).

| run | what it is | result |
|---|---|---|
| `a` (native DD0790B23C8F) | **the build handed over**: both fixes, sphere-lobe bounce | all 1147 checks passed, 13:24 |
| `m` | mutant: hair moved by HDRP's motion vectors again (PT-53), card's bounced light the cache's at its point again (PT-54) | PT-53 check FAIL (0.671 after the move); PT-54 check passes (0.85 of the wall): not a guard |
| `b` | cosine bounce (not kept, see PT-54) | 2 failures; not handed over |

- PT-53 check: a layer-hair card 3 m away, a wall at 12 m, the camera moved 10 cm sideways between the traced frame and
  the shown one. Share of the card's middle pixels showing its checker where it really is: **0.892 with the camera still,
  0.902 after the move** (bar: above 0.85, and within 0.06 of still). The wall's motion would put the card about 15
  pixels off; the squares are 25.
- PT-54 check: a whole card of layer hair 1 cm in front of a near wall, sun off, lit by the sky's bounced light only.
  The card's light by its colour against the bare wall's, red against red: **2.43 times the wall's** (bar: 0.7 to 3.0).
  **Not mutation-proof:** with the old cache term the card gets 0.85 of the wall and passes too. At 2.4 m the cache's cells
  are not shadowed by one card the way a scalp is by a head of hair, so the scene does not reproduce the game's dark
  crown. Keep it as a measurement only; a real check needs a thick head of hair on a skull, lit from behind.
- The existing hair-layer checks all still pass on the new composite (lit 0.146 on the strands, 0.150 on the wall; the
  Ray Reconstruction guides as before: diffuse 0.001 from the albedo view's).
- `python tests/source_checks.py`: 89 of 89 (one new check, five updated to the new code).

## PT-53 and PT-56: hair moves while the camera moves

**Cause.** The traced light reaches the screen a frame late. The composite (d3d11.hlsl) moves each traced texel onto the
new frame with HDRP's motion vectors (`prevUv = uv - motion`). pt_hair's `blend` wrote the hair's light into the texel of
the surface HDRP drew behind the hair. The mod hides HDRP's hair while tracing (culling mask 0), so HDRP's motion vector
at a hair pixel is that of the surface behind it:
- over the skull, the skin: the right motion, so that hair sits still (PT-56's still layer);
- around the head, the wall: the wrong motion. When the camera moves sideways, near things move more on screen than far
  ones, so the outer hair slid with the wall (PT-56's moving, see-through layer, and PT-53).

Size of the error: the slide moves the camera about 5.5 cm a frame (`hair-moving/10-slide-traced-anim/step.json`). With
hair at 2 m and a wall at 5 m, that is 0.055 x (1/2 - 1/5) = 0.0165 rad, about 17 pixels of the 1924x1084 render. No
denoiser is involved, as the owner saw.

**The coordinator's lead, tested and dropped.** Kandra skins every registered vertex every frame, whatever the culling:
`SkinningManager.RunComputeKernel` dispatches over all `VerticesCount`, and the hair's bones are the visible body's. Its
output is in world space (the log: "their traced vertices sit where the game draws them"). And the world was frozen.

**Fix.**
- pt_hair mode 0 writes the hair into outputs of its own: light and cover (`hl`, RGBA16F), the first card's distance
  (`hd`, R32F), and its Ray Reconstruction guides (`hg0`, `hg1`, RGBA8). Mode 1 (`blend`) runs only for the debug views.
- The composite (`hair_at`) finds, per pixel, the traced texel whose hair point lands there now. It rebuilds the point from
  the texel's ray and distance, and moves it by HDRP's own motion-vector rule for a still point: this frame's
  view-projection without jitter, and HDRP's previous one. It adds the object motion of the surface behind the pixel
  (HDRP's vector minus a still point's there): a character's skin moves the hair over it; a wall adds nothing.
- The search is a fixed-point iteration from the texel HDRP's vector gives. Where that texel has no hair, it starts from
  texels along the pixel's ray in front of HDRP's surface (12 depths, 0.15 m to 60 m). With a still camera all depths map
  to one texel and the search is skipped, so the hair maps texel for texel and gets no blur.
- The composite lays the hair over the surface's light by its cover, GBuffer3 included, as `blend` did. It keeps
  `blend`'s clamp of 256, sets the reflections' hit distance to 0 where the hair covers more than half, and lays the
  hair's guides over the rest (`hair_guides`). The meter adds the layer. The surface's light under the hair is now kept
  whole, so a wall the hair uncovers as it moves has its own light.

**Left as it was.**
- HDRP's motion vectors at hair pixels are still the surface's. DLSS and Ray Reconstruction accumulate their history along
  them, so with them a trail may remain at the hair's edge over a far wall while moving. The composite's frame is now
  right. Next step: write the hair's motion into HDRP's motion vectors.
- A walking character's hair over a still wall gets no object motion (the wall has none). Exact fix: each skinned mesh's
  previous positions, read at the hit.

## PT-54: the black oval and the "glow on top"

**What the frames show** (`head-glowing.jpg`, `hair-glowing/3-traced-no-denoiser`). The bowed blond man is lit by candles
behind him. Traced: a black oval over the crown, sharp along the skull's outline, with candle-lit strands around it. The
surfaces view shows the layer covering the whole hair shape, so it is the hair, and it is black.

**Cause** (pt_hair `hair_light` before the change):
1. Direct light: the candles are behind the head, so the head shadows its own crown as seen from the camera, and that
   shadow's edge is the skull's outline. The strands outside it get the candles' light from behind (the strand model lights
   a strand from either side): the "glow". That part is physical.
2. Bounced light: the only term was `albedo x cache_light(p)`, the radiance cache at the card's point. Near the camera the
   cache's cells are 10 cm and the hair lies 1-3 cm above the scalp, so that cell is the scalp's. The scalp's pixels (HDRP's
   surface under the hidden hair) feed it, and their rays mostly meet the hair right above. So the cell holds the light
   under the hair. With no direct light, the crown was black. In the game, probes light hair from all around.

**Fix (handed over, build `a`).** Each card traces its bounced light (`hair_bounce`): one ray over the whole sphere, shaded
at its hit as `pt_gi`'s rays are (emission, sun and lamps, then the cache, Bounces deep), times the strand's diffuse
response from `hair_bsdf` (lit from either side, as the direct light already is). The highlights' share of the
surroundings is left out, as before. Cost: one bounce ray and its shadow rays more per lit card.

**Not settled: how bright.** The card takes 2.43 times the bounced light of a matte wall beside it by its colour. That
lobe (sin/pi over the whole sphere) gives up to pi times a matte surface under even light. HDRP's ambient on hair, and the
old cache term, are a cosine about the hair's normal (a matte surface's). So the traced crown may come out brighter than
the game's. I tried the cosine lobe (run `b`). The card then came out 31 times the wall, with a multicoloured per-pixel
pattern: some samples overflow the hair layer's FP16 output. The cause was not found: a plain if/else in place of the `?:`
around `shade_path` changed nothing. The cosine version also failed the normal-map check: "away" stripes 0.82 went to
2.71. So the traced bounce returns too much light for some directions, and the sphere version may carry part of that too.
Two diagnostic builds were made and not run (stopped at wrap-up): `%TEMP%\tgb\hair5-c` (incoming = sky only, no
tracing) and `hair5-d` (sky where unoccluded, 0 on a hit), made by `scratchpad\h5\variant.py`. Run them first: if both
give a sane red card, the excess is in `shade_path` at the bounce's hits.

**Not proven.** No number shows the dark cell yet: the offline check passes on the old code too. The in-game run of the
PT-53-only merge will still show the oval; then the next step is an F8 on the crown (its Indirect and Direct views) with
the old code, then with this one.

## Hair and eye colours not the character's own

**No colour bug found** (stopped here at the coordinator's word).
- The materials the tracer reads are the character's own. The blond man's `FirstPass_Mat_H_m008(Clone)` was read with
  `_DiffuseColor (0.893, 0.827, 0.714)`, root `(0.709, 0.627, 0.507)` and end `(0.872, 0.721, 0.496)` (log line 332), all
  blond. The grey man's hair `_DiffuseColor` is 0.70 grey. No recolour line follows.
- The traced picture is warmer everywhere by the same amount (grey man, 4K frames, linear means of boxes,
  `scratchpad\h5\box.py`). Red's share of r+g+b: hair 0.54 in the game, 0.64 traced; white shirt 0.61 in the game, 0.71
  traced. Hair to shirt brightness: 0.13 in the game, 0.18 traced. In the traced frame the tavern is lit by candles alone,
  which turns white cloth yellow and grey hair brown. In the game, HDRP's probes add a neutral fill. That is the traced
  look as a whole, the owner's call. The hair was also too dark: that is PT-54.

## PT-55: brows flicker (located, not fixed)

The harness's change maps (`eyebrows-flickering/overview.jpg`, row 3; crop `scratchpad\h5\flick.jpg`) show two bright
streaks along the man's brow and upper-lid line, in a frozen world:
- with Ray Reconstruction: 8 levels or more between frames, nothing else on the face moving;
- with no denoiser: as bright;
- with NRD: none.

NRD never touches the hair layer, which is laid on after every denoiser. So if the brows' own layer noise were the cause,
NRD would flicker most, and it flickers least. The streaks are a signal the tracer's own filters smooth under NRD (direct
light, reflections) and Ray Reconstruction does not.

**Best lead: the lashes.** They are held opaque while tracing (cut 0.5), so HDRP draws them into its GBuffer as a black,
glossy (smoothness 0.5) deferred surface a pixel or two thick. They alias with the jitter, and only their specular light
shows. Ray Reconstruction may take the flipping guides for a new surface every frame. Second lead: the wet eye's candle
highlight, a firefly on a near-mirror (smoothness 0.9).

Next: the save with Direct, Specular and Albedo views per frame, and F8 on the brow. If it is the lashes, draw them as a
layer like the brows: filtered cover, no aliasing.

## Files changed in the copy

- `native/pathtracing/src/tracer.cpp`:
  - `Outputs` gains `hl, hd, hg0, hg1`, made and released with the others, and in the `writes` list;
  - `Result` gains `hair` and `invViewProj`; `State::hairLayer` texture becomes `hairLayerRan`;
  - the hair pass (mode 0) writes the new outputs; mode 1 runs for debug views only; compose's hair flag is 0; the meter
    gets the layer;
  - `CompositeConstants` 176 to 368 bytes: `hairViewProj`, `hairPrevViewProj`, `tracedInvViewProj`, and `extras[3]`
    the hair flag; the composite binds 16 SRVs.
- `native/pathtracing/shaders/pt_hair.hlsl`: `layer_distance`, guides into the hair's own outputs, `blend` for views
  only, `hair_bounce` in place of the cache term.
- `native/pathtracing/shaders/d3d11.hlsl`: `composite` becomes `composite_surface`; new `hair_at`, `hair_point`,
  `hair_motion`, `composite`, `hair_guides`; the old `hair_cover` from B's alpha is gone.
- `native/pathtracing/shaders/pt_meter.hlsl`: adds the layer (pass a.z).
- `tests/pathtracing_gpu_test.cpp`: two new checks (PT-53, PT-54), images `47c` to `47e`.
- `tests/source_checks.py`: a new test `test_hair_layer_moves_with_the_hair_and_traces_its_bounced_light`; five
  assertions updated to the new code.
- `CHANGELOG.md`, `docs/PT-REQUESTS.md` (PT-53 to PT-56), this report.

## How to merge

- No ABI change, no new option, material, instance or view bit, no export changed. The new shared textures (8 per pixel
  at RGBA16F, plus 4, 4 and 4, two of each) add about 40 MB at a 1924x1084 render.
- Merge the files above with `git merge-file` against `wt\base29`.
- Watch these overlaps with other agents: `tracer.cpp`'s `ensure_frame_textures`, the record function's hair and compose
  dispatches, and the composite's SRV list; `d3d11.hlsl`'s Composite cbuffer and `composite()`; `pt_hair.hlsl`'s
  `hair_light`.
- The cbuffer layout in d3d11.hlsl must match `CompositeConstants` (static_assert 368).
- To take PT-53 without PT-54: keep `hair_light`'s old last line,
  `if (cache_on()) result += albedo * (0.5 * (cache_light(p, s.normal, rng) + cache_light(p, s.normal, rng)));`, drop
  `hair_bounce`, the PT-54 GPU check and the PT-54 lines of the new source check.

## What the in-game run must show

`-Scenario hair-moving,hair-glowing,eyebrows-flickering,hair-eyebrows@hair`:
- `hair-moving`, slide steps `10-slide-traced-anim` and `11-slide-traced-nrd-anim`: the woman's hair stays on her head as
  the camera swings, with no sliding outer layer. Compare with `9-slide-game-anim`. Any trail at the edge with RR comes
  from HDRP's motion vectors (left as it was).
- `hair-glowing`: with PT-53 alone merged, the black oval stays (PT-54 not merged). With PT-54 too, no oval: the crown
  lit dim and candle-coloured by the room's bounced light, the rim still brighter. Hair brighter than the game's would
  be the "not settled" part of PT-54.
- `eyebrows-flickering`: the brow streaks in the change maps unchanged (PT-55 not fixed).
- `hair-eyebrows` (Ron): his look unchanged or a little brighter. Watch the frame time: the hair pass traces one bounce
  per lit card.
