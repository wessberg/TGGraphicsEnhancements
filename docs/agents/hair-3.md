# PT-01 hair, round 3: the hair agent's report (2026-10-06)

Written by the hair agent in its copy `wt\hair3` (base `wt\base3`). Built and tested offline: the GPU test passes 634 of
634 with the round's shader, source checks 78 of 78, the plugin compiles (net472, 0 warnings). Nothing here ran in the game:
both fixes are judged in the next harness run.

## In short

- **The crown (a pink veil).** The game paints the skin under the hair in the hair's color, through a scalp mask (and a
  beard mask). Round 2 hides the hair's renderer from HDRP, and the masks sit in that renderer, so they went with it, and
  the tracer left them out. Now the tracer draws them itself, as the lowest layer of the hair.
- **The brows (black bars).** HDRP still wrote the brows' depth and normals. Its forward pass threw every texel away, so
  nothing lit them, and the tracer's layer found no card in front of that depth. Now the brow material's depth cuts are
  raised too, so HDRP writes nothing, and the layer draws the brows over the skin.

## 1. The scalp and beard masks

What they are (read from the game's bundles and its code):

- `FirstPass_ScalpMask_*` and `FirstPass_ScalpMask_Beard_*`: shader `TG/Character/Kandra_Lit`, transparent, `_AlphaCutoff`
  0.05. Texture `_BaseColorMap` = a grey mask (512x512; its color equals its alpha: `Tex_ScalpMask_Male_01` has 25 % of
  its texels at or above 0.05 and 18 % at or above 0.5). Color `_BaseColor` = the hair config's scalp color
  (`CharacterHairColor.ApplyTo` sets it on every material of the hair's renderer).
- The mask's texture is laid out like the head's own coordinates: the shell is a copy of the scalp, lying on the skin.

What the tracer does now:

- `SceneCapture.CharacterMaterial`: a mask with a texture becomes a traced material of its own instead of "left out":
  flags alpha test + camera-only + hair layer. Its texture goes through the hair color bake (`ShellTile`:
  `TGPT_RegisterHairColor` with the map's own color and nothing else, cutoff 0.05, no depth prepass). Its color is the
  traced material's base color (`_BaseColor`), followed when the game recolors it (`RefreshShellColor`): no re-bake.
- Camera-only + hair layer means: no shadow ray, bounce ray or camera ray of another pass meets it. Only the hair
  layer's own ray does. So it shadows nothing and hides nothing.
- `CharacterCapture.Find`: its triangles are registered now (they were skipped as left out).
- `pt_hair.hlsl`:
  - the layer's ray takes a shell up to 1 mm + 0.05 % of the distance past HDRP's depth (`SHELL_PAST`, `tshell`). A
    card must be in front of HDRP's surface; a shell lies on it (the game blends it with ZTest LEqual and no depth of
    its own). Without this the shell at the skin's own depth is never found.
  - `HAIR_LAYERS` 4 to 8. A crown stacks thin strands; at 4 the ray gave out before the shell.
  - a shell is lit as a surface (Lambert about its own normal, `g_shell`), as the game's Lit shader lights it, not as a
    strand.

What I need from the tile allocator (the surfaces agent's PT-35 work): one detail tile with its strand block per mask
texture, asked for through `TGPT_RegisterHairColor` only. I key it by what the tile holds (the texture's id, its tiling,
the cutoff; bit 40 set to stay apart from hair's keys, which are a material's 32-bit id), so every character's clone of
a mask shares one tile. I changed nothing in how tiles are numbered, counted or addressed, and not `strand_tile`. If no
detail tile is left, the mask is left out as before (the log says which).

## 2. The brows

What draws them, from round 2's own log line in `20261006-185232` (Ron's `Mesh_BaseHuman_Face`, submesh 5):

    "Mat_BaseHuman_Eyebrows_05" #-236140 (TG/Character/RealHair; surface 0, queue 2475, _AlphaClip 1.1; passes on:
    GBuffer DepthOnly ShadowCaster; traced as a layer, hidden by the mod)
    what Kandra registered for HDRP to draw (batch entry 72): the same material

- RealHair has no GBuffer or DepthOnly pass (those two read "on" for any name a material never switched off). So every
  pass of the brow material that could draw is off, its clip is 1.1, and Kandra draws that very material. And still the
  Surfaces view has a magenta strip at the brows: a forward depth pass ran there.
- So Kandra's batches draw a material's passes whether they are switched off or not (this is also what the 12:52 note
  of 2026-09-28 saw). What stopped the forward pass is the clip (1.1): it discards every texel. The depth pass was not
  stopped: among RealHair's compiled depth variants some discard below `_DepthPrepass` instead of `_AlphaClip`. That
  fits both pictures: hair was drawn at a cut near 0.5 before round 2 (its `_DepthPrepass`), brows as their solid core
  (0.95).
- Black: depth and normals written, no color pass, so HDRP's color there is what nothing lit. The layer only looks in
  front of HDRP's depth, and the brow card is at that depth: nothing found.

The fix (`CharacterCapture.HideLayerHair`): `_DepthPrepass` and `_DepthPostpass` are raised to 1.1 together with
`_AlphaClip` (`LayerCutProperties`), remembered and given back exactly (`RestoreLayerHair`); `GameCut` hands the tracer
the game's values (the hair color bake reads `_DepthPrepass` through it, so the layer's own prepass stays 0.95 / 0.5).
The round-2 log line now also prints `_DepthPrepass` per material.

Not pinned: which pass each compiled variant belongs to. Raising all three cuts covers whichever it is.

## Offline results (`tgpt_gpu_test.exe`, my build, 20:21 to 20:25)

New check: a blue shell lying on the wall HDRP draws (at HDRP's own depth), behind five thin red cards (0.3 of the pixel
each, none opaque). 1764 pixels over the card:

| build | cards with the shell behind | cards with the wall behind | shell alone | wall | whole test |
|---|---|---|---|---|---|
| round 3's shader | 754 | 0 | 746 | 0 | all 634 pass |
| `HAIR_LAYERS` 4 again | 0 | 687 | 746 | 0 | 1 fails |
| shell only in front of HDRP's depth (`tshell = tmax`) | 0 | 784 | 0 | 747 | 2 fail |

- Round 1's layer check is unchanged: 643 red, 1121 grey.
- The first run of the new check counted wrongly (I took the wall's grey as 0.42; it is 0.73 in the albedo view, the
  PNGs are tone-mapped). It still failed the broken builds. Classifiers and comments corrected, all three rerun.
- Outputs: `scratchpad\hairwork\gpu3-final*.txt`, pictures `scratchpad\hairwork\img3-final*\46b-hair-layer-shell.png`,
  the three side by side (first run) `scratchpad\hairwork\shell-runs.png`.
- GPU runs waited for: no game process, no live `artifacts\harness\RUNNING`, `-GpuCheck` free.

## Files changed

- `native/pathtracing/shaders/pt_hair.hlsl`: `HAIR_LAYERS` 8, `SHELL_PAST`, `next_layer(…, tshell, …)`, `layer`'s
  `tshell`, `g_shell` in `hair_bsdf` / `hair_light` / `layer`. `strand_tile` untouched.
- `src/TGPathTracing/SceneCapture.cs` (CRLF, kept): the mask branch of `CharacterMaterial`, `IsShell`, `LayerShell`,
  `ShellTile`, `RefreshShellColor`, `shellColors`; `HairTile`'s prepass and the log text through `GameCut`.
- `src/TGPathTracing/CharacterCapture.cs`: `LayerCutProperties` and the three cuts in `GameCut`, `HideLayerHair`,
  `RestoreLayerHair`; shells kept out of `HideLayerHair` and counted first in `LayerHairOnly`; `Find`'s `leftOut`;
  `_DepthPrepass` in the diagnostic line.
- `tests/pathtracing_gpu_test.cpp`: the shell check (5 new checks); two comments of round 1's check corrected (0.73).
- `tests/source_checks.py`: round 3's lines in `test_path_tracing_no_placed_lights`.
- `CHANGELOG.md` (CRLF, kept): one bullet. `docs/agents/hair.md`: the wall-grey numbers corrected. This file.
- No ABI change, no new bit, no new native call, FrameConstants unchanged.

## What to look at in the next harness run (views recipe, `hair-eyebrows`; then `npc-candle`, `tavern-room`, `npc-day`)

1. `LogOutput.log`:
   - `... is a scalp or beard mask (a shell on the skin), drawn by the tracer as the hair layer's lowest layer (its
     texture in a 512x512 tile, color (...), alpha clip 0.05)`: the mask is traced. If it says "left out of the traced
     scene" instead, it got no detail tile (or has no texture).
   - `hair drawn by the tracer as a layer ... its cuts (_AlphaClip, _DepthPrepass, _DepthPostpass) raised to 1.1`.
   - In `what Kandra hands HDRP`: the brow material should read `_AlphaClip 1.1, _DepthPrepass 1.1`.
2. Views:
   - Surfaces: no magenta strip at the brows (skin there, tinted toward cyan where the layer's brow covers).
   - Albedo: the crown solid in the hair's scalp color under the strands; brows in their own color, thin.
   - Traced picture: no bare skin through the crown, no black bars.
3. State check after Off: 0 changed. `_DepthPrepass` and `_DepthPostpass` are in the audit (all `TG/Character`
   properties are), so a value not given back would show there.
4. If the crown is still bare: the mask's log line tells whether it is traced; if traced and still missing, the shell
   lies more than about 1.5 mm behind HDRP's skin depth there (raise `SHELL_PAST`).
5. If the brows are still black with `_DepthPrepass 1.1` in the log: the depth pass that draws them has no alpha test
   at all, and the brows need a renderer of their own to hide (not expected: the strip follows the brow's shape).

## Risks and things seen on the way

- 8 cards a ray: the hair stage cost 0.06 to 0.22 ms in the last runs; expect up to twice that where hair is thin.
- The mask is lit as a surface, the cards as strands: at a crown lit from below the mask is darker than the strands
  over it, as in the game.
- Kandra draws switched-off passes. So the cornea the plugin "hides" by its passes (`HideLayer`, a transparent
  Kandra_Lit over the traced eye) is most likely still drawn by HDRP; the same for `HideTransparentHair`. Not touched
  here. A hide that works for them: raise the material's alpha cutoff past 1, as for hair.
- Round 2's notes stay: a hidden beard keeps its last mouth shape in dialogue; hidden hair casts no HDRP shadow.
