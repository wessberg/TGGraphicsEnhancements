# PT-01 hair, round 2: the hair agent's report (2026-10-06)

Written by the hair agent in its copy `wt\hair2` (the main tree at 18:29:48 with round 1 merged). Managed code only this
round: the plugin compiles (net472, 0 warnings), source checks 76 of 76. Nothing here ran in the game; the proof and the
fix are both read off the next harness run.

## In short

- What shows on Ron is HDRP's own hair and beard, not the tracer's layer. The plugin's switches on their materials do
  hold (the "was HDRP's again" line never came), yet HDRP draws them. The same switches do hide his brows.
- New fix: a renderer that holds only layer hair (and its scalp or beard mask) is taken out of Kandra's culling while
  tracing. Kandra is the game's character renderer; it culls each renderer by a layer mask before HDRP draws anything.
  Mask 0: no camera and no light draws it. Kandra still skins it, so the tracer still has it.
- New proof: the log names, for the two characters nearest the camera, every Kandra renderer, whether its culling lets
  HDRP draw it, and per submesh the material Kandra registered for HDRP to draw beside the one the plugin switches.

## What I read

From `20261006-182908/hair-eyebrows` (the merged build) and `20261006-181324`:

- Surfaces view: hair and beard magenta (forward pixels the tracer did not find), brows not: the brow area is the
  head's skin (orange). In the traced picture Ron has no HDRP brows. So the material route (passes off, cut 1.1) works for
  brows on the head's renderer and fails for the hair and beard renderers.
- The log: `HDRP's passes DepthForwardOnly, ForwardOnly switched off and its cut raised to 1.1` for Ron's
  `FirstPass_Mat_H_m001(Clone)`, and no `was HDRP's again` line: the materials the plugin holds keep that state.
- Ron's hair and beard are traced as a layer in that run (4 `FirstPass_Mat_H_m001(Clone)`, the beards, all with a detail
  tile).

From the game's code (decompiled, read only):

- `HairController.Refresh` = `_SurfaceType` on every material + HDRP's `ValidateMaterial` + `MaterialsTransparencyChanged`.
  HDRP's `ValidateMaterial` for a Shader Graph (`BaseUnlitAPI.SetupBaseUnlitPass`) never switches ForwardOnly or
  DepthForwardOnly back on.
- Every RealHair depth variant I disassembled discards below `_AlphaClip`; with 1.1 nothing passes. So a material in the
  state the plugin holds cannot draw depth. Whatever draws Ron's hair is not in that state. Candidates the log will
  settle: Kandra's batch renderer group drawing another material for that submesh than `RenderingMaterials` holds, or a
  second renderer drawing the same hair that the tracer does not capture.
- Kandra (`Awaken.Kandra`): `SkinnedBatchRenderGroup` registers each submesh's material with Unity's
  BatchRendererGroup and emits procedural draws (flag HasMotion). Its culling jobs keep a renderer only where
  `VisibilityCullingManager.layerMasks[slot]` & the camera's (or light's) culling mask is non-zero. `layerMasks` is public;
  Kandra writes it only when a renderer registers (and 0 when it unregisters).
- Skinning runs over every vertex every frame, visible or not. The animator stays visible while any renderer of it is
  (the body). Texture streaming goes by distance. What does depend on visibility: `CopyBlendshapes` (the face's blend
  shapes copied to a beard) runs only while Kandra calls the beard visible.

## The change (`src/TGPathTracing/CharacterCapture.cs`)

- `HideRenderer`: for a traced renderer whose every drawn submesh is layer hair or a shell (`ScalpMask`, `Stubble` by
  name), with at least one layer hair, while tracing: remember the game's mask for its slot, write 0, every frame. If
  Kandra writes a mask again (the renderer registered anew in the same slot), take it again and log it.
- `RestoreRenderers`: when tracing stops, when the Characters setting is off, and after 30 frames without the renderer
  among the traced ones: the game's mask back, only into the slot it came from and only if that slot still holds 0.
- `DescribeDrawn`, logged 3 s after tracing starts and 20 s later (16 at most a game): `Characters: what Kandra hands
  HDRP for the characters nearest the camera, while tracing:` then per character (grouped by its Kandra rig; at most 16
  renderers and 8 Unity renderers each) every Kandra renderer (active,
  slot, seen by the camera in the last cull, culling mask and the game's, traced or not, HairController or not) and per
  submesh its material and "what Kandra registered for HDRP to draw" (the BatchRendererGroup's material for that
  submesh, read from Kandra's private lists by reflection), each with passes on, cut, surface, queue, traced as what;
  then Unity renderers under the character that are enabled.
- Untouched: the material switches (they stay, and keep the brows hidden), the hold on opaque, the layer itself.
- `tests/source_checks.py`: four lines hold the hide and its exact restore. `CHANGELOG.md`: one bullet.

## What to look at in the next harness run

1. `LogOutput.log`:
   - `Characters: hair renderer "..." (...) left out of Kandra's culling while tracing (its layer mask N to 0)`: the
     fix ran.
   - `Characters: what Kandra hands HDRP ...`: for Ron, does "what Kandra registered" say "the same material" for his
     hair submesh? If it names another material, that one is what HDRP drew (and why the switches missed it). Is there a
     second hair renderer, untraced? Is his hair renderer's mask 0 and "seen by the camera: no"?
   - `had its culling mask ... again`: Kandra re-registered a hidden renderer.
2. Views on `hair-eyebrows`: the Surfaces view should lose the magenta on hair and beard (the skull's orange, tinted
   toward cyan where the layer covers); Albedo shows hair color; Normals no HDRP hair; Coverage no dark outline. Then the
   traced picture shows the tracer's layer, which round 1 made cover pixels as the game does.
3. The state check after Off (all five scenes of `181324`): should stay 0 changed. The masks are not in the audit, so
   also compare the game's picture after Off with before (hair present).
4. If hair still shows magenta: the log's renderer list says what else draws it.

## Risks, said plainly

- A hidden beard keeps its last mouth shape in dialogue (no blend shapes copied while Kandra calls it invisible). Seen
  only on bearded characters who talk, up close.
- Hidden hair casts no HDRP shadow (the tracer's shadows are unaffected; HDRP's shadow maps matter for fog and
  transparents only).
- If the diagnostic's reflection fails (Kandra changed), the line says why and nothing else is affected.
