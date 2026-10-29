# Round 3 - TG Path Tracing: see-through glass, hair's colors, the eyes, caustics in glass and water (the owner asleep)

Dates: 2026-09-28 from ~02:00 (after commit d31dff9). The owner played until ~02:40, reported and took F8 captures,
then went to bed: "please see if you can resolve most of these things and get some work done throughout the night.
Don't use subagents for that, that way we can spread out token usage". Everything below is uncommitted until the owner
asks. Technical detail: `docs/PROJECT-CONTEXT.md` section 9; every change with its numbers: `CHANGELOG.md`.

## In game with the owner (02:00 to 02:40)

- **See-through glass** (deployed 02:21): HDRP's transparent jars are the tracer's now (`TGPT_MATERIAL_SEE_THROUGH`, a
  mask bit of their own, HDRP's passes hidden while tracing). The owner, 02:28 (capture 20260928-022829): "Jars looked
  quite pixelated". Cause: their stain texture came from the 64x64 atlas tile, point-sampled. Fixed 02:52 with 512x512
  **detail tiles** (`TGPT_RegisterDetailTexture`, rows of their own below the 64x64 tiles, filtered bilinearly). Harness:
  a 4-texel checker's contrast 1.06 from the detail tile, 0.00 from the 64x64 one.
- **The cage** (capture 015409, before DI history 4): as traced 0.48 at the crosshair, 0.11 without DI reuse, 0.14 at
  history 4, 0.10 with no reuse at all. The owner after history 4: "significantly less boily, but still boily". What is
  left is near the no-reuse floor: per-frame sampling noise on thin metal bars. The F8 capture now adds a pair without
  the traced reflections (`01i-no-reflections`) to tell whether it is their one reflection ray a pixel.
- **The bartender's grey hair from far away** (capture 023440): the F8 "every renderer of" listing named the hair
  renderer (a Kandra "Cloth" with `FirstPass_Mat_H_m001(Clone)`, RealHair). Its `_DiffuseColor` is 0.70 grey; the auburn
  lives in `_RootColor`/`_EndColor`. The tracer colored hair by `_DiffuseColor` alone.

## Overnight (the agent alone)

- **RealHair's color from its compiled shader.** The shader is a Shader Graph in `resources.assets`; its program blob
  unpacks (one D3D11 platform, 8 LZ4 chunks) and d3dcompiler's D3DDisassemble reads the DXBC (no reflection names:
  UnityPerMaterial is CB3, laid out in property order, Vector3 + float packing). The formula (PROJECT-CONTEXT 9.7):
  diffuse map lerped from white by _BaseColorStrength; toward lerp(_RootColor, _EndColor, root map green) by
  lerp(_RootColorStrength, _EndColorStrength) x _GlobalStrength; two highlight bands on the ID map's green (smoothstep
  tents, faded toward one end by the overlap); x _DiffuseStrength, blend map, mask map green as occlusion; x
  _DiffuseColor. Baked texel by texel into each hair material's own 64x64 tile (`TGPT_RegisterHairColor`, D3D11
  `TGPT_HAIR`), baked again when the game recolors it. Harness: a red-to-blue gradient with a green band, then all green.
- **Hair, eyebrows and lashes held opaque while tracing** (the hard rule): up close the game made hair transparent
  (BodyFeatures bands 0-1) and eyebrows/lashes are always transparent, so HDRP shaded them over the traced image. Hair
  goes through a Harmony prefix on the game's `HairController.Refresh` (what the game asks for is kept and given back);
  eyebrows and lashes (alpha cutoff on already) are switched on their materials and Kandra told.
- **Fog Adaptation row** (On, for the owner to compare): the tavern's haze looked thin because Dark Adaptation
  brightens the traced room (x5) while HDRP lays its fog at the game's exposure. A pass at BeforePreRefraction (render
  event 5, `TGPT_SetFog`) adds (gain - 1) x the fog's own light, from the composite's copy of the color before the fog
  and the height fog's transmittance; the gain no longer fades behind fog when it is on. The lights' volumetricDimmer
  was not touched: the game's LightWithOverride caches it and writes it back. Harness: fog light x2 at a gain of 2.
- Deployed 03:21, hash-verified (hair colors, hold opaque, Fog Adaptation, the F8 pair). ABI 0x00011000 (new exports).
- **The eyes are the tracer's** (deployed ~03:44). The eye shader (TG/Character/Eye/Eye, also a Shader Graph in
  `resources.assets`) decoded: sclera map x tint (tiling 1.8 about the centre); iris map x tint (tiling 3.88, magnified
  toward the centre by the pupil: scale = tiling + 8 x pupil - 6 within an eighth of the UVs) over it by the iris
  alpha; the limbal ring an overlay (ring = (0.1 + 0.9 t)^intensity in the outer ringSize, 1 elsewhere, which doubles a
  dark iris); a fake cubemap screen-blended (left out); an iris glow e^(emission - 3) (left out). One shared sampler that
  must clamp (repeating, iris discs tiled the sclera: the harness caught it). Baked per kind of eye into a 512x512 detail
  tile (`TGPT_RegisterEyeColor`), traced as a wet camera-only surface (`TGPT_MATERIAL_EYE`, smoothness 0.9) with HDRP's
  own normal where it agrees (the traced mesh has face normals only). HDRP's cornea layer is hidden while tracing.
  Bug found in review, not by the harness: the far-hair stopgap treated every non-hair forward hit on a character as
  "missing" (HDRP-shaded), eyes included; fixed, with a harness case (a character's cube on a box drawn forward: plain
  material 0.000, HDRP's; eye 0.090, traced).
- **No Hair Shadows row**: nothing it described is HDRP-shaded any more. `pt_hdrp_shadow` still gives the stopgap
  pixels the traced shadows, always.
- **Caustics through glass** (the Caustics row, On; deployed 04:36). The owner: "Bottle caustics are most interesting
  I think, but to all of the things you mentioned". Manifold next event estimation (Hanika et al. 2015), solved as
  Blender Cycles' shadow caustics (its Apache-2.0 kernel read for reference, not copied), in the sun's and RTXDI's
  final shadow rays through one closed glass object crossed twice; meshes bring vertex normals now (TGPT_MeshDesc 160
  B). Two findings, both from numpy re-implementations (`tools/research/`): Cycles' per-vertex "no progress" rule
  fails every flat pane under the sun (the first vertex needn't move), fixed; and a cube's hard edges bend 84 % of the
  sun's light out of its straight shadow (physically dark there, so the old glass-cube tests now run with
  TGPT_OPTION_NO_CAUSTICS). A harness ball first measured 0.39 of its light back: its caustic lay under the traced-only
  leaf's checkered shadow; moved clear, 0.67 (numpy 0.68), a spot x13.5 the open sun, a dark ring x0.06, a clean image.
- **Caustics under water** (the same Caustics row; `docs/CAUSTICS-RESEARCH.md` Step 1). HDRP's water goes into the traced
  scene as water (`TGPT_INSTANCE_WATER`, mask bit 0x08; every other ray now asks for `MASK_ALL` 0xF7, and a source check
  bans a literal mask in any ray query): custom surfaces' own meshes, a quad for quads, a large quad at sea level under
  the camera for the open sea. The four surfaces with caustics nearest the camera take a slot each
  (`TGPT_SetWaterCaustics`): HDRP's pattern texture (copied whole each frame, its mips too) and its mapping, folded into
  one affine map in double precision. In `pt_compose`, a water-only ray from the lit point towards the sun (straight up
  with no sun) finds the crossing; steep crossings (waterfalls) are passed; the direct light takes 1 + intensity x the
  pattern, read as HDRP's `EvaluateSimulationCaustics` reads it (triplanar by the normal, faded in and blurred over the
  blend distance; the mip from the pixel's footprint). HDRP's own: a prefix zeroes a surface's intensity only while
  HDRP writes its constants (the pattern keeps rendering; the underwater light shafts keep theirs), and the underwater
  opaque fog pass records without the pattern. Harness: a striped pattern under a water quad; the factor and the
  pattern moved half a repeat add to 4 within 0.000 on 1765 floor pixels; 0.0000 change outside; in the box's shade the
  image changes by exactly the (lamp's) direct change x0.753 (the floor's 0.75 albedo); the water alone changes 0.00000.
  Mutation-checked: water in the ordinary mask, the mapping's offset dropped, the pattern on bounced light: each fails.
  Defaults taken for section 7 (the owner's to overrule): every surface with caustics on (also the open world's lakes,
  which never showed them from above), the game's strength (1 + intensity x pattern: about x1.5 at the open world's 0.5),
  one row (Caustics), the underwater light shafts keep HDRP's pattern. The F8 report lists each surface's caustics
  settings and the traced slots. A point above every slot's highest water point skips the water ray (dry land: most of
  the screen). All 16 ctest suites and 71 source checks pass; deployed 05:41, hash-verified (TGPT only).
- **See-through glass in front of the sky** (deployed 05:59, hash-verified). The prepare pass looks for such glass on
  sky pixels too (up to Ray Distance); the specular pass traces only its reflection there; the compose pass writes the
  share of the sky it lets through, (1 - F) x its color, into A and marks B's alpha -2; the composite multiplies HDRP's
  own sky by that share and adds the reflection, interpolating as if the open sky were glass letting all through. Such
  texels never feed a pixel HDRP drew (texel_kind -1, left out of the fallbacks). Harness: a green pane in the ceiling's
  gap, 6610 sky pixels: green against red x1.61, red x0.62 (x1.00 and x1.00 with the prepare pass as before).
  Found in review, not by the harness: the traced light's meter (the exposure gain) counted such texels' A (a share of
  the sky, not light) as light; it leaves them out now, as it does the sky (deployed 06:34 with the row's new text).
- **Sunlight reflected off water** (the Caustics row; research Step 2a; deployed 06:28, hash-verified): in pt_compose,
  for a surface facing the water above it, a water-only ray along the sun's direction mirrored in level water finds the
  point the light comes up from; water's Fresnel (0.02 face on) at the sun's angle, HDRP's pattern there (mean about 1,
  blurred by the sun's disc over the distance) and two shadow rays (receiver to water, water to sun); diffuse only,
  added after the direct light's filter. Both water rays stop at the water's span (TGPT_WaterCaustics top and bottom:
  the ray passes through the ground otherwise). Harness: a wall by the pool, 200681 pixels at the plain mirror's value
  within 3 % (0.00282 predicted from the sun), stripes in bands, 0 where the path misses water or the water is in the
  ceiling's shade, 0 with the option off. Mutation-checked: Fresnel dropped, the water-to-sun shadow dropped.
- **Are the game's bottles hollow?** Glass caustics solve one closed object crossed twice; a hollow bottle (an inner
  wall: four crossings) keeps the straight tint. The alchemist's jars (`Prop_Alch_Jar_03/04`, spawned from
  `ItemTemplate_1_Jar_0x` in `templates.items_assets_all.bundle`) have their visual outside the catalog's named
  entries, so the mesh wasn't found offline. Instead the F8 report now says how many times the camera ray crosses the
  first glass object along it (pt_probe `glass_crossings`, pickSurface bits 24-27; harness: a ball 2, a hollow ball 4).
  Deployed 06:50, hash-verified. Then answered offline after all: `tools/research/glass_mesh_crossings.py` scanned the
  game's bundles (35 s): single bottles are closed shells (caustics solved), the jars `Prop_Jars_01*`, drinking glasses
  and `Potion_02` hollow (the straight tint, close to right for thin empty walls). Last deploy of the night 07:01 (the sky's
  glass looked for within 100 m, not Ray Distance: every sky pixel asks when the scene holds see-through glass).
- **The D3D12 debug layer** (the Agility 1.619.6 NuGet package's layers DLL, in a scratch folder): the whole tracer
  test with 0 errors and 0 warnings. GPU-based validation was started and then stopped by the agent's host for low
  system memory (the owner's `eye-clean` training run held 48 GB); not rerun.
- **Deploy trap found**: the game's bridge copies came from `%TEMP%\tgb\main` (the previous session's Ninja tree), and
  `cmake --install build\native` put build/native's own bridge build (same sources, another hash) into
  `artifacts/Native`; the TGPT-only deploy then refused ("TGFrameGeneration is installed with a different build of TG
  Graphics Bridge"). Fixed by copying the installed build back (PROJECT-CONTEXT 11).

## Open (R3), in priority order

1. The owner's look at the traced eyes (brighter and wetter than HDRP's? the iris glow and SSS are left out), and at
   eyebrows/lashes held opaque.
2. The owner's look at: jars with detail tiles, far hair colors, eyebrows/lashes opaque, Fog Adaptation On vs Off, the
   F8 no-reflections pair at the cage.
3. Caustics: glass, water, and sunlight reflected off water done (R3). The single bottles are closed shells (solved);
   the jars and drinking glasses are hollow and keep the tint: the four-crossing walk (two MNEE chains) only if the
   owner misses their caustics. In game: a lake shore at noon (new from
   above), the same place diving (the same pattern), a bridge's shadow on a river bed (none), a bridge's underside and
   the rock over a pool (the reflected light: too strong, too weak?), a cave pool against the game's own look, and Ray
   Reconstruction trails behind the moving pattern (if any: its Responsivity Mask over lit water beds); the GPU cost
   of the water rays in the F8 stage times ("composite"). CAUSTICS-RESEARCH section 7
   stays the owner's (defaults above). Hollow bottles (four crossings) keep the straight tint.
4. The owner's look at glass against the sky (a window from inside, bottles on a sill). The far-hair "missing" stopgap
   (HDRP shading where the traced scene lacks a character's forward surface) once no case needs it.
5. Hair highlights: RealHair is HDRP's Kajiya-Kay hair (two shifted, tinted lobes, a flow map for the strands); the plan
   is in PROJECT-CONTEXT 9.7 "Hair highlights" (vertex tangents, the lobes' values, strand lobes in the direct light and
   reflection passes). Not started: it touches every lit pass, and the owner hasn't seen the traced hair's colors yet.
6. Still open for the owner: move the F8 capture off F8 (it also turns TGFG off and opens AvalonAtlas's HUD editor)?
   Answered from TGBU's log: Ray Reconstruction loads TGBU's bundled `nvngx_dlssd.dll` (preset F), not the game root's.

## ▶ Starter prompt (R4)

Read AGENTS.md's START HERE: BACKLOG.md (the rulings and the Open list), PROJECT-CONTEXT section 9 (9.7 for glass,
eyes, hair, and both kinds of caustics) and DEAD ENDS, and this file's "Overnight" and "Open". Everything since d31dff9
is uncommitted: commit and push only when the owner asks. The owner slept through round 3's second half; they will play
the 07:01 deploy (ABI 0x00011100) and report. First read their newest F8 captures in `BepInEx/TGPathTracing/Captures/`
(report.txt: the water line now lists each surface's caustics settings and the traced slots; the pair
`01i-no-reflections` answers whether the cage's boiling is its reflection ray; at glass, "the camera ray crosses the
first glass along it N times" says whether a bottle's caustics can be solved: 2 yes, 4 hollow) and the LogOutput.log lines for
"caustics", "water" and "Hair keeps". Then take their verdicts, in this order: the traced eyes and opaque eyebrows and
lashes; far hair colors; jars and windows (see-through glass, now also against the sky); Fog Adaptation On vs Off;
caustics (bottles; water beds from above and diving; the reflected sunlight under bridges; Ray Reconstruction trails
behind the moving pattern) and `docs/CAUSTICS-RESEARCH.md` section 7, whose defaults this round took. Fix what they
report before starting anything new; the hair highlight plan (Open 5) is next after that. Deploy after each round of
fixes with the game closed (a TGPT-only deploy refuses when TGFG carries another bridge build: PROJECT-CONTEXT 11),
hash-check, and at the end write round 4 and update BACKLOG, PROJECT-CONTEXT and ROUNDS.
