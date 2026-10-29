# PT-01 hair: the hair agent's report (2026-10-06)

Written by the hair agent in its copy (`wt\hair`), for the main session to merge. Nothing here ran in the game.

## In short

- Two things make the traced hair look broken. One is fixed here and proven offline. The other is in the game, and
  this copy cannot test it: the main session must look.
- **Fixed, the layer's own drawing.** The hair layer (`pt_hair.hlsl`) covered a pixel by the texture's alpha as it is.
  The game's hair shader does two more things (read from its compiled code): it remaps the alpha by `_AlphaPower` and
  `_AlphaRemap`, and any texel at or above `_DepthPrepass` (0.5) covers the pixel whole. The layer let the rest of such a
  texel show what lies behind the hair: the scalp, in the hair's own shade, black.
- **Found, not fixed, in game.** In the owner's Ron save, HDRP still writes the hair's depth and normals while tracing,
  though the plugin logs its passes switched off and its cut raised to 1.1. The layer looks for cards only in front of
  HDRP's depth, so it finds only the faint ones and never covers a pixel whole. That is most of what shows in Ron's
  hair today, and my fix alone will not change it much there.
- The failing offline check was mostly its own card, not the layer (below).

## 1. The layer's alpha (fixed)

What the game does, from RealHair's compiled shader (`tools/research/unity_shader_blob.py "TG/Character/RealHair"`, its
pixel shaders disassembled):

- Depth pass: `a = pow(saturate(tex.a / _AlphaRemap), _AlphaPower)`; `a = max(a, a >= _DepthPrepass ? 1 : 0)`; discard
  below `_AlphaClip`.
- Forward pass: the same three lines, then the color times `a`, and `a` as the output alpha. So every texel at or above
  the prepass is fully opaque on screen.

What the layer did: coverage = `tex.a`, and at the prepass it stopped with that coverage. What showed through the rest
was HDRP's surface behind the hair (the scalp, held opaque in the GBuffer, traced in the hair's shadow: black).

How much that matters, measured on Ron's own hair texture (`Tex_H_m001_BaseMap`, 2048, read from the game's bundle with
`hairwork/hair_alpha.py`; box-filtered mips as the strand atlas holds them):

| mip | hair texels (>= 0.1) at or above 0.5 | of those, short of 1 | pixel they left uncovered before the fix |
|---|---|---|---|
| 2048 | 75.5 % | 35.7 % | 11.5 % |
| 1024 | 66.5 % | 49.3 % | 18.4 % |
| 512 | 49.8 % | 44.3 % | 23.7 % |
| 256 | 41.4 % | 39.7 % | 27.5 % |

Ron's hair has `_AlphaPower` 1. Others in the same runs have 0.5 (`Tex_H_m008`) and 0.25 (`Tex_H_f001`): a texel of 0.25
is 0.5 or 0.71 of the pixel in the game, 0.25 traced. Brows: prepass 0.95; beards: 0.5, power 1.

The fix (`pt_hair.hlsl`): `hair_game_alpha` remaps the filtered alpha; `strand_cover` makes it 1 from the prepass on;
the clip compares the remapped value. The power and remap travel in `TGPT_Material`'s last field, until now a reserved
float of 0: `uint32_t hairAlpha`, two IEEE halves (power low, remap high), 0 reads as 1. Same size, same place: no ABI
change, an old plugin gets the old behaviour. `SceneCapture.HairAlpha` fills it for layer hair, and the log line of each
layer material now names its power and remap.

## 2. The offline check (it failed since the layer was written)

- Why it failed: its card was the unit quad, whose texture coordinates run 0 to 4. Its 64x64 checker repeated 4 times:
  32 squares across 52 pixels, 1.6 pixels a square. The layer reads the strands at the pixel's footprint (mip ~4.3) and
  rightly averaged them to about half: the faint pink patch (438 red, 187 grey of 1764). The old picture's coverage
  values were soft (only 5 % of its pixels near 0 or 1), so the layer did read the strand atlas with its mips: the strand
  tiles were ready and had their block offline. In game they are ready too (part of Ron's hair is covered by the layer,
  below), and the native log has no strand atlas or bake failure.
- The check now: the card mesh with its texture once across (8 squares of about 6.5 pixels), full squares 0.3 with
  `_AlphaPower` 0.5 (0.55 as the game takes it: at least the prepass, so drawn whole), empty squares 0. Red counts only
  the card's own color (green under 0.1: a 0.55 cover would be 0.36, a 0.3 cover 0.53; the wall's grey is 0.73 in the
  albedo view, not the 0.42 this report first assumed: corrected in round 3); grey counts pixels the wall makes at least
  a third of (the squares' filtered edges, raised by the power, leave faint strands a pixel or two into the
  empty squares, as the game's shader would).
- Results (`tgpt_gpu_test.exe`, my build, 18:00; full output in `scratchpad\hairwork\gpu-*.txt`; the cards side by side
  in `scratchpad\hairwork\layer-card-runs.png`):

| build | red (card) | grey (wall) | whole test |
|---|---|---|---|
| the fix | 643 of 1764 | 1121 | all 557 checks pass |
| the fix without the remap (`hair_game_alpha` returns `a`) | 0 | 1764 | 2 fail (this check and "lit") |
| the fix without the whole pixel at the prepass (`strand_cover` returns `a`) | 0 | 1764 | 2 fail |
| the layer as it was (`wt\base`'s `pt_hair.hlsl`) | 0 | 1764 | 2 fail |

- First try of the new check (grey still "the wall alone"): 643 red, 451 grey, failed on grey; the classifier was
  loosened as above, nothing in the shader.
- Source checks: `test_path_tracing_no_placed_lights` stopped at the line asked about, and nine of its lines were older
  than the layer, hidden behind that first failure. The line `if (g_candidateT < g_strandsFrom) return false;` became
  part of the line the check already asserts above it (`return (m.flags & MATERIAL_STRANDS) == 0 || g_candidateT >=
  g_strandsFrom;`): the same rule for strands, so the stale assertion is gone. Also stale: the strands' cut at the camera
  (`STRANDS_CAMERA_CUT`, `g_strandsPassed`, `passed`), replaced by `strand_coverage` and `behindShown` in `pt_prepare`;
  the instance masks (`kMaskStrands=0x10`, `kMaskStill=0xE1`); and `MASK_STRANDS`, now allowed in `pt_hair.hlsl` only.
  Four new assertions hold the layer's alpha rules. 76 of 76 pass (75 before).

## 3. What shows in game (not fixed here)

From `artifacts/harness/20261006-172608/hair-eyebrows` (the tree's build, Ron, all debug views):

- Surfaces view: hair and beard are forward pixels (no deferred bit) the tracer did not find. In a 500x480 box around his
  head, 56,681 such pixels: 62 % plain magenta (no layer over them at all), 38 % tinted toward cyan (the layer over part
  of the pixel), none fully cyan. (Read from the post-processed colors, so the split is approximate.)
- Albedo view: Ron is bald. Materials view (the tracer's own camera ray, which passes layer hair): bald, the barrel seen
  through where his hair is.
- Normals view: the hair's normals are in HDRP's normal buffer. Coverage view: darkened along the hair's outline (the
  traced scene lies more than 2 % + 10 cm behind HDRP's depth there).
- So HDRP's depth (and normal and stencil) pass for the hair ran while tracing. The layer only searches up to HDRP's
  depth, and in front of the hair HDRP drew there are only the faint texels HDRP's own cut left out: partial coverage,
  never whole. The rest of each pixel is HDRP's color there, where nothing traced lights the hair: the black gaps.
- The plugin's log of that run says the hide happened: `HDRP's passes SHADOWCASTER, DepthForwardOnly, ForwardOnly
  switched off and its cut raised to 1.1`. HDRP's own `ValidateMaterial` (which the game's `HairController.Refresh` calls)
  does not switch ForwardOnly or DepthForwardOnly back on for Shader Graphs (`BaseUnlitAPI.SetupBaseUnlitPass`,
  decompiled; it does switch TransparentDepthPrepass, Postpass and Backface on for a transparent material). RealHair's
  alpha-tested depth variants discard below `_AlphaClip` (1.1: nothing drawn), but some variants cut at `_DepthPrepass`
  (0.5) instead (in HDRP that is the transparent depth prepass's threshold; which pass each variant is, I did not pin). 0.5 is the cut the picture suggests: in front of HDRP's hair depth the layer
  finds only texels below 0.5. So one lead: at render time the hair is transparent, with its depth prepass on (the hold
  on opaque not holding for it). Kandra draws characters through a BatchRendererGroup (`SkinnedBatchRenderGroup`) with
  the same material objects the plugin edits. I could not settle offline what draws the hair.
- Added, read-only: `CharacterCapture.HideLayerHair` now logs, up to 12 times, when it finds a hidden hair material
  drawn by HDRP again (`Characters: layer hair "..." #id was HDRP's again (passes ... on, _AlphaClip ..., _SurfaceType,
  queue): hidden again`). No such line while the picture still shows HDRP's hair depth means the flags stay off and
  HDRP draws it anyway (then the hide must happen in Kandra itself, for example its renderer's filtering settings).
- Also changed: RealHair names its shadow pass `SHADOWCASTER`, and `HideLayerHair` compared `"ShadowCaster"` as written,
  so it switched the hair's HDRP shadow off too, against its own comment. Now compared without case: the hair keeps
  casting HDRP's shadow while tracing (on what HDRP still shades, the fog). Drop that line if it is not wanted.

## Files changed

- `native/pathtracing/shaders/pt_hair.hlsl`: `hair_game_alpha`, `strand_cover`, `strand_alpha` and `next_layer` use them,
  `layer` breaks on a whole cover; header comment.
- `native/pathtracing/shaders/pt_common.hlsli`: `MaterialRecord.hairReserved` is `uint hairAlpha`.
- `native/pathtracing/include/tgpt_api.h`: `TGPT_Material.reserved` is `uint32_t hairAlpha` (same size and offset);
  comments of `TGPT_MATERIAL_HAIR_LAYER` and `TGPT_HairColor`. ABI number unchanged (0x00011400).
- `src/TGPathTracing/NativeTracer.cs`: `NativeMaterial.HairReserved` is `uint HairAlpha`.
- `src/TGPathTracing/SceneCapture.cs`: `HairAlpha(m)` (halves of `_AlphaPower`, `_AlphaRemap`), set on layer hair; its
  log line names power and remap.
- `src/TGPathTracing/CharacterCapture.cs`: the "was HDRP's again" log in `HideLayerHair`; the shadow pass compared without
  case (`IsShadowCaster`).
- `tests/pathtracing_gpu_test.cpp`: the layer card (mesh, alpha, power, classifiers).
- `tests/source_checks.py`: the stale lines, the new layer assertions.
- `CHANGELOG.md`: two bullets at the top of "what the in-game harness found first".
- `docs/agents/hair.md`: this report.
- No new option, material, instance or view bit. FrameConstants unchanged.

Merge note: all of these are LF except `CHANGELOG.md` (CRLF, kept). Built: native (Ninja, `%TEMP%\tgb\hair`, installed to
`%TEMP%\tgb\hair-native`), managed (`dotnet build ... net472`, 0 warnings). Not deployed.

## What to look at in game

1. Deploy both the native DLL and the plugin (the plugin fills `hairAlpha`; an old plugin leaves it 0 = as before).
2. Run the views recipe on `hair-eyebrows` (Ron). In `LogOutput.log` look for `Characters: layer hair "..." was HDRP's
   again (...)`. If such lines come: they name what turned back on (a pass, the cut, `_SurfaceType`, the queue): find
   who does it. If none comes and the Surfaces view still shows Ron's hair magenta, the material flags stay off and HDRP
   draws the hair anyway: then hide it at Kandra's level instead (its renderer's filtering settings, or a material
   swap), which the plugin has not tried. An F8 with the crosshair on his hair prints the same per material
   ("on now: ...; _AlphaClip ...").
3. What the fix itself changes is seen once the layer covers the hair (Surfaces view cyan over it): the hair then dense
   where the game's is, no scalp between strands. Ron's hair has `_AlphaPower` 1, so for him it is the prepass rule;
   hair with `Tex_H_m008` (power 0.5) and `Tex_H_f001` (0.25) changes most: in run `20261006-170351` they were
   registered in `candle` and `water-day` (m008) and `metal` (f001).
4. Scenes with people: `hair-eyebrows` (close-up), `npc-day`, `npc-candle`, `tavern-room`. Brows: prepass 0.95, so a
   brow texel covers its pixel whole only from 0.95.
5. Not touched, worth knowing: hair colors take one 512x512 detail tile per material instance (each character's clone),
   and only 32 exist (shared with eyes and glass): in run `20261006-170351` the log shows 29 of 53 baked hair materials
   not drawn as a layer (19 of 42 in `20261006-155214`), held opaque on a 64x64 tile instead, most likely for want of a
   tile. Many are clones with the same values; sharing a tile between them would keep them all on the layer.
   `HAIR_LAYERS` (4 cards a pixel) unchanged.
