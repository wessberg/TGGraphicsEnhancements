# PT-01 hair and eyebrows, round 4: the hair agent's report (2026-10-06)

Written by the round-4 hair agent in its copy `wt\hair4` (base `wt\base6`, round 3's code). Evidence first: round 3's build
in the game, run `artifacts\harness\20261006-203513\hair-eyebrows` (and `20261006-185232`, `-185451`, `-203731` beside it),
then the fix. Built and tested offline (GPU test 636 of 636, the new check mutation-checked; source checks 78 of 78; the
plugin compiles); nothing here ran in the game. Crops: `scratchpad\h4\*.jpg` (named below). PT-REQUESTS' PT-01 entry is
left to the coordinator.

## In short

- **The brows (black bars).** The plugin holds the brows opaque while tracing. Opaque, HDRP draws their whole cards into
  its depth and normals before the tracer takes them: the game has no opaque build of RealHair that cuts (written down
  in `pt_prepare.hlsl` on 2026-09-28: "HDRP drew each card whole whatever its cut"). So no pass switch and no cut stops
  it, and nothing lights those pixels: black. Fix: brows the tracer draws as a layer keep the game's transparency. HDRP
  draws transparent things only after the tracer has taken its picture of the frame, and its transparent builds do cut.
- **The crown (a pale veil).** The hair layer already covers 98 to 99 % of every crown pixel, in the hair's color. The
  veil is added on top of it: the composite adds the GBuffer3 of the surface behind (HDRP's "emission" buffer, where it
  also bakes light) to a deferred surface's light, and the hair never covered that term. Behind the crown is skin, behind
  the hair's outer edge the wall: the veil stops exactly at the skull's outline. Fix: the hair's cover now hides the same
  share of GBuffer3. Proven by elimination, not by a picture of GBuffer3 itself (none exists yet): the next run's
  emissive view shows it.
- Round 3's two fixes (scalp shell, three cuts at 1.1) did run, as its log lines say. Neither could help: the crown was
  never under-covered, and the brows are drawn by a build that has no cut.

## 1. What is on screen at the crown

Points read off 4K `full.jpg` frames, 11x11 pixel boxes (`scratchpad\h4\probe.py`). Dome middle = (0.500, 0.445) of the
screen, hair outside the skull = (0.478, 0.440).

| step (203513) | dome middle | hair outside the skull | what it says |
|---|---|---|---|
| 02 traced, RR | 118 51 39 | 98 28 16 | the veil: paler, pinker over the skull |
| 12 traced, no denoiser | 122 53 40 | 101 30 17 | same: not the denoiser |
| 03 albedo | 100 51 31 | 104 53 34 | the layer's color is the hair's over the skull too |
| 04 normals | 13 9 8 | 27 20 21 | the blend multiplies these views by 1 - cover: about 1 % left |
| 07 materials | 0 0 0 | 0 0 0 | same: the layer covers the whole pixel |
| 05 surfaces | 81 199 210 | 76 196 208 | full cyan: covered by the layer |

- Across the veil's top edge (x = 0.497, rows 872 to 932 of the 4K frame) the albedo view stays at about (100-110, 50-56,
  30-35) while the lit picture jumps from (56, 15, 9) to (103, 41, 31) between rows 905 and 908: the skull's outline.
  Crops: `c-crown.jpg` (lit, albedo, direct, indirect, specular, surfaces at 2x), `e-dome.jpg` (3x, traced against game).
- Round 2's run `185232` (no scalp shell yet) has the same veil: dome middle 126 54 40, and the same cover (normals view
  21 17 16). So the shell round 3 added changed nothing, and nothing was missing under the strands.
- The old path's run `203731` (hair held opaque, drawn by HDRP into its depth, the tracer lights it as a forward
  surface) has no veil: dome middle 119 35 17, saturated as the hair outside. The beard shows it too: over the chin the
  layer path is paler (179 77 47) than the old path (161 53 27).
- What the composite does with a pixel the layer covers (d3d11.hlsl `composite`): the surface's light A and B were
  multiplied by 1 - cover in `pt_hair blend`, the hair's light added to B; then `e + base * A + B`, where `e` is HDRP's
  GBuffer3 at the pixel. `e` was the only term the hair did not cover. For a forward pixel (the old path) `e` is 0; for
  the wall behind the hair's edge it is the wall's (dark wood); for skin it is whatever HDRP put there. Every other term
  that differs between "skin behind" and "wall behind" is under the 1 % cover.
- Not proven: how big the skin's GBuffer3 is, or why it is not zero (HDRP is meant to bake no light there while tracing:
  `HdrpPasses.SetIndirectOverride`; either RealSkin writes an emission of its own, or some of the forward ambient that
  `RaiseForwardAmbient` raises reaches the GBuffer pass). The next run's emissive view settles it (section 6). If it is
  baked light, every face gets it too, as a flat fill: worth its own PT entry then.

## 2. What is on screen at a brow

- Surfaces view: magenta over both brows (HDRP's depth with no deferred bit, and the tracer found no forward surface
  there): 1371 pixels in the head box, the near brow 97 x 28 pixels at 4K. The slabs have straight edges and the brow's
  card outline, not the brow's thin soft shape (`g-brows.jpg`, 4x: game, traced, albedo, normals, surfaces, materials).
- What fills them: `pt_prepare` sends that pixel's camera ray to HDRP's depth; camera rays pass through layer cards
  (`alpha_opaque`), so it meets the skin 1 to 2 mm behind, a character surface that is no strand: "HDRP shades it"
  (`missing`), and the composite keeps HDRP's color times the traced shadows. HDRP drew no color there (the forward pass
  cuts at 1.1), so the color is the cleared buffer's: black.
- The log (`LogOutput.log` line 293): `hair material "Mat_BaseHuman_Eyebrows_05" turned opaque (traced)`, and Kandra's
  listing (line 371): `surface 0, queue 2475, _AlphaClip 1.1, _DepthPrepass 1.1; passes on: GBuffer DepthOnly
  ShadowCaster`. Held opaque by `HoldLayersOpaque`, cut and switched by `HideLayerHair`, and drawn into the depth anyway.
- Why: RealHair's compiled variants (round 1's dump, `hairwork\realhair`) include a forward pass and depth passes with no
  discard at all (`ps_3_143c8e`, `ps_3_9ec3a`, `ps_1_1c7a2`, `ps_1_788a`). The ones that discard cut by `_AlphaClip`
  (forward, motion vectors), `_DepthPrepass` (`ps_3_80d26`, the transparent depth prepass), `_DepthPostpass` (`ps_3_6da8e`)
  or `_ShadowClip` (shadow caster). A card drawn by a build without the cut is whole whatever the cuts say.
- HDRP's order (decompiled `HDRenderPipeline`): depth prepass, GBuffer, (the tracer's capture at RenderSSR), lighting,
  forward opaque, the composite at AfterOpaqueAndSky, and only then `RenderTransparentPrepass` and the transparents. A
  transparent brow cannot reach the depth or GBuffer the tracer reads.

## 3. Where round 3's reasoning went wrong

- The crown: it read the veil as bare skin between too few strands (a missing scalp layer). The debug views of round 2's
  run already showed the layer covering about 99 % of the crown in the hair's color; the veil was added on top, so no
  layer under the strands could remove it.
- The brows: it read the magenta strip as "following the brow's shape" and blamed a depth variant cutting at
  `_DepthPrepass`. The strip is the card. And the 2026-09-28 note says opaque RealHair cards are drawn whole: no cut on
  the material could work while the brow is opaque.
- Round 2's "the switches hide his brows" (run `182908`) was HDRP's own hair hanging over them (`h-182908.jpg`).
- Why the offline checks passed: the GPU test's raster pass never draws a layer card (it models a perfect hide), so
  HDRP drawing whole cards is outside it; the layer checks read the albedo view, where the composite adds no GBuffer3;
  the lit check only asks for 0.03 to 2 times the wall; and the test's wall has no GBuffer3 (HDRP's "nothing here" tag).

## 4. The changes

- `pt_hair.hlsl blend`: over a deferred (B's alpha 0) or see-through glass (-1) texel, B's alpha gets 0.4 times the
  hair's cover added (`HAIR_COVER_KIND`). Every reader still reads the same kind (thresholds at -1.5, -0.5, 0.5).
- `d3d11.hlsl`: `hair_cover`, `gbuffer3_emission`; the composite takes the cover with the same texels as the light
  (`g_hairCover`) and adds GBuffer3 times 1 - cover. Debug: the emissive view (TGPT_VIEW_EMISSIVE) adds HDRP's GBuffer3
  over deferred pixels before any hair covers it. Off by default (a debug view); no new option bit, no ABI change.
- `SceneCapture.LayerBrow` (RealHair, "Brow" in the name, alpha test on): never "left out" for being transparent
  (`CharacterLeftOut`, `MaterialIndex`), so the brow is traced (one material key whatever its transparency).
- `CharacterCapture`: `HoldLayersOpaque` skips such brows; `Brows` (after the frame's materials are read): a brow drawn as
  a layer stays transparent (given back if it was held: `_SurfaceType`, `ValidateMaterial`, its queue, the renderer told);
  one without a detail tile is held opaque as before (`HoldLayerOpaque`). Logged once: `Characters: eyebrows "..." drawn
  by the tracer as a layer stay transparent while tracing ... (_SurfaceType 1, queue ...)`. `HideLayerHair` still
  switches the passes off and raises the three cuts; restore on Off unchanged.
- Untouched: the tile pool, `TGPT_RegisterHairColor`, FrameConstants, the ABI number, the layer's own search.

## 5. Offline results

Build `%TEMP%\tgb\hair4-native` (Ninja, round 3's configure line), installed to `%TEMP%\tgb\hair4-inst`. GPU runs waited
for: no game, no live `RUNNING`, `-GpuCheck` free (`scratchpad\h4\run-tests4.ps1`, log `h4\wait.log`).

New check (after round 1's lit check of the layer card): the wall behind the card drawn again with light in its GBuffer3
(`mats[7]`, white with emission 0.6 0.45 0.4; the traced wall unchanged), 24 frames lit, no denoiser.

| build | wall between the strands | strands over it (hair covers the pixel) | whole test |
|---|---|---|---|
| this round | 0.1307 -> 0.2260 (+0.0953) | 0.0511 -> 0.0514 (+0.0002, 0 % of the wall's gain) | 636 of 636 pass |
| mutant: composite adds GBuffer3 whole (round 3's) | 0.1307 -> 0.2502 (+0.1195) | 0.0511 -> 0.1704 (+0.1193, 100 %) | the new check fails |

- The mutant's picture is the veil in small: the red card turns pale pink over the glowing wall
  (`scratchpad\h4\k-glow-check.png`: plain wall, this round, the old composite; outputs `h4\gpu-fixed.txt`,
  `h4\gpu-mutE.txt`, pictures `h4\img-fixed`, `h4\img-mutE`).
- Rounds 1 and 3's layer checks unchanged: 643 red, 1121 grey; the shell under five cards 754, 746, 0.
- `python tests\source_checks.py`: 78 of 78 (round 4's lines added to `test_path_tracing_no_placed_lights`).
- `dotnet build src\TGPathTracing -c Release -o %TEMP%\tgb\hair4-managed`: 0 warnings, 0 errors.
- Not testable offline: the brow change. The GPU test's raster pass is its own HDRP; it never draws a layer card, so
  HDRP's opaque builds drawing whole cards cannot happen there. The next harness run is its test (section 6).
- Trap met on the way: restoring `d3d11.hlsl` with PowerShell's `Copy-Item` kept the older timestamp, CMake did not
  re-read the embedded shader, and the "fixed" DLL carried the mutant. Caught by searching both DLLs for the line; the
  file was touched and rebuilt (each DLL now holds its own version).

## 6. What the next harness run should show

`hair-eyebrows` with the views recipe (the first scene, so hair is on the layer path), then the usual scenes:

1. `LogOutput.log`:
   - once: `Characters: eyebrows "Mat_BaseHuman_Eyebrows_05" drawn by the tracer as a layer stay transparent while
     tracing, as the game draws them (_SurfaceType 1, queue <the game's>) ...`;
   - no `hair material "Mat_BaseHuman_Eyebrows_05" turned opaque (traced)` line;
   - in `what Kandra hands HDRP`: the brow submesh `surface 1, queue <the game's>, _AlphaClip 1.1, _DepthPrepass 1.1`.
2. Surfaces view: no magenta over the brows. The forehead's orange, turned toward cyan in thin brow shapes.
3. Albedo view: thin brows in their own grey-brown over the skin, not cards.
4. Emissive view (`11-view-emissive`): the crown and the face show HDRP's GBuffer3 of the skin. If I am right, the dome
   middle is pale pink there, roughly the veil's excess (traced 118 51 39 against 98 28 16 outside the skull). If the dome
   is black in this view, GBuffer3 is not the veil, and the traced crown stays pale: then look at what else differs
   between "skin behind" and "wall behind".
5. Traced (`02`, `12`): the crown in the same deep red as the hair outside the skull (dome middle near 98 28 16, not 118 51
   39), no edge along the skull's outline; the beard over the chin as saturated as in `203731`'s old path (161 53 27).
6. State check after Off: 0 changed. The brows are given back exactly: the ones never held keep the game's surface type;
   cuts and passes come back through `RestoreLayerHair` as before.

If the brows are still black bars: read the Kandra listing. `surface 0` there means the hold came back (the fallback path:
the brow had no detail tile, so it is held opaque as before). `surface 1` with magenta means a transparent pass writes
depth before the capture, which HDRP's order says it cannot.

## 7. Finer strands and highlights (the owner's second point)

Crops: `j-strands.jpg` (top of the crown, 4x: the game `203731/1-game`, traced RR, traced without denoiser, same camera),
`i-strands.jpg` (the hair's left edge). The game shows long thin strands along the flow with fine bright streaks on
them. The layer shows short grainy flecks and broad soft bands, no single strands. What limits it, biggest first:

1. **Ray Reconstruction's guides describe the surface behind the hair.** The guides pass writes them from HDRP's GBuffer
   (`d3d11.hlsl TGPT_GUIDES`), which under the layer holds the skin or the wall. RR then reads strand detail as noisy light
   on that surface and smooths it: in the box at (0.49 to 0.52, 0.415 to 0.44) the fine-detail measure (mean absolute
   Laplacian over mean) falls from 0.208 without denoiser to 0.120 with RR (-42 %). Fix (not small): the layer's own
   albedo, normal or strand direction and roughness written into the guides where it covers the pixel.
2. **The highlights read a 64x64 lobe tile.** Ron's `_SpecularMap` is `Tex_H_m001_BaseMap` at 2048x2048 and his flow map
   512x512: the tile holds them at 1/32 and 1/8 of their size along each axis. The per-strand variation that makes the
   game's fine streaks is averaged away, leaving broad bands. (The tile pool belongs to the other agent: not touched.)
3. **The color tile is 512x512** for a 2048x2048 base map (1/4 along each axis). The strands' shape is not limited by it
   (their alpha is read at 2048 from the strand atlas, `kStrandBlock` 2048), but their color between strands is.
4. **One light sample a frame.** The undenoised layer is grainy (the same box's measure 0.208 is mostly noise, not
   strands), which leaves RR more to smooth.
5. Not a limit here: the 8 layers (the crown is 99 % covered), the lobes' roughness (the game's own values per lobe).

Also seen: the traced hair is about 3 times brighter than the game's in these boxes (outside the skull 98 28 16 against
34 7 3), as the whole traced frame is (its gain x2.9 in this tavern): not hair's own.

## 8. Risks

- If a transparent RealHair build the game uses had no cut, HDRP would draw the brows with its own light over the traced
  ones again (the 2026-09-28 look: dark outside dialogue). Against it: the game draws brows transparent with the alpha
  test on, and every transparent forward and depth build in the dump discards by the cuts the plugin raises. Seen in the
  traced picture only (transparents come after the debug views' composite).
- Brows with no detail tile (characters after the first scene, until the tile pool is fixed) take the old path: held
  opaque, whole cards. Unchanged by this round.
- B's alpha now carries the cover on deferred and glass texels (0 to 0.4 added). Every reader compares it with the same
  thresholds (composite, meter, sky glass); the blend's own `b.a == 0.0` test runs before it writes.
- The emissive view now includes HDRP's GBuffer3 on deferred pixels: lamps and glowing surfaces look brighter in that view
  than before.
- If the skin's GBuffer3 holds baked light, faces get it too (uncovered). This round hides it only under hair.

## Files changed

- `native/pathtracing/shaders/pt_hair.hlsl` (LF): `HAIR_COVER_KIND`, the cover added to B's alpha in `blend`.
- `native/pathtracing/shaders/d3d11.hlsl` (LF): `hair_cover`, `g_hairCover`, `gbuffer3_emission`; the composite's GBuffer3
  times 1 - cover; the emissive view's GBuffer3.
- `src/TGPathTracing/SceneCapture.cs` (CRLF, kept): `LayerBrow`; `CharacterLeftOut` and `MaterialIndex` keep such brows.
- `src/TGPathTracing/CharacterCapture.cs` (LF): `HoldLayerOpaque` (split out of `HoldLayersOpaque`, which skips brows now),
  `Brows`, its call in `CaptureCharacters`.
- `tests/pathtracing_gpu_test.cpp` (LF): `mats[7]` (white with light in GBuffer3, drawn only), the GBuffer3 check after the
  layer card's lit check (`47b-hair-layer-over-glow.png`).
- `tests/source_checks.py`: round 4's lines in `test_path_tracing_no_placed_lights`.
- `CHANGELOG.md` (CRLF, kept): one bullet. This file.
- No new option bit, no native call, no ABI or FrameConstants change, the tile pool untouched.
