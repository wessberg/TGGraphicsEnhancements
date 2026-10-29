# A guard's helmet without its detail: what the game's character shader samples, and why the tracer shows none of it (PT-95, PT-64)

Worker report. Copy: `wt\kandra`. Plugin build: `%TEMP%\tgb\kandra-plugin`. Tools, crops and notes:
`scratchpad\kandra-work` (nothing of the game's shaders or textures is in the repository).

**State (2026-10-09 10:20): the trial is built (plugin only), 102 of 102 source checks pass. NOT seen in the game.
The brief's guess was wrong, so the trial is another one than it asked for. Read "The short answer".**

## The short answer

1. **The shader has no detail layer.** `TG/Character/Kandra_Lit` samples four textures, all by the mesh's own
   texture coordinates: the base map times `_BaseColor`, the normal map times `_NormalScale`, the mask map (metal in
   red, occlusion in green, smoothness in alpha) and the emissive map. No detail map, no micro normal, no wear, dirt
   or tint mask, no remap, no tiling of its own. The tracer reads every one of them, by those names.
2. **The helmet is that shader, and as plain as the torso.** It is `Mat_ChMGenM_02_Head`: a base map (1024x1024, a
   plain mid grey on the metal: linear 0.18), a normal map (1024x1024: the hammered grain, a tilt of 4 degrees at
   the median and 20 at the ninth tenth) and a mask map (512x512: metal 1, smoothness 0.38 to 0.76, median 0.59).
   Tint white, normal scale 1. The torso's material has the same shader, names and values. So the game's "dark
   hammered iron" is a dark metal with a fine normal map and a varying roughness, nothing more.
3. **In the run the brief shows, the whole guard read none of his own textures.** Not the helmet alone: the scale
   armour, the leather pauldron and the sleeve are all drawn in coarse BLOCKS of colour with no relief, and the
   tracer's Normals view is smooth over the whole guard (`scratchpad\kandra-work\guard.jpg`: the game, the traced
   picture, the Normals view; run `20261009-084003`, `metal`, the 4K frame's crop x 1300 y 300, 1000 x 1860). Blocks
   are what the 64x64 stand-in tile looks like (`pt_common.hlsli atlas_sample`: read without filtering;
   `metallic_smoothness`: the same for the mask; `material_normal`: no normal map at all without the game's own).
   The helmet only looks "plain" instead of "blocky" because its base map is a plain grey.
   (The brief's "the same guard's scale armour and leather pauldron do show their textures" does not hold for this
   run. It does for the run before, see 5.)
4. **A material falls back to those tiles when the textures it names are no longer the game's.** The native side
   gives a material -1 for a handle whose texture the game has freed (`texture_share.cpp resolve`), and the shader
   then takes the tile, silently. All the guard's maps are streamed: the game loads and drops their sharp levels
   while it runs, and Unity makes a NEW texture object each time. The plugin must follow each swap
   (`TextureShare.Refresh`), and it can miss one (below). The run's own counts fit: 1656 first openings + 1866
   followed swaps = 3522 opened; but 2350 were freed, **484 more than the swaps followed**, and the tracer held 1172
   textures where the plugin counted 1656 open. The log does not name them, so this is NOT proven for the guard.
5. **There are most likely TWO defects on that helmet, and the trial is about the first.** In the run before
   (`20261009-071155`, `guard-071155.jpg`, `helmet-071155.jpg`) the torso, pauldron and sleeve are sharp, with
   relief, and that run's counts leave no swap unfollowed in `metal` (1643 held of 1643; 7 swapped, 7 freed). The
   helmet is a smooth, bright metal there all the same. So with its maps read, the helmet still loses its grain in
   the NRD picture: a metal's relief and roughness live only in its reflections, and what filters those does not
   keep them (or the metal's shading is off: the game's helmet reflects 18 % at a roughness of 0.41, the traced
   one looks brighter and glossier). No run so far has a Normals view or a frame without a denoiser of the guard
   WITH his own textures: the one Normals view there is (`084003`) was taken in the blocks. The trial's recipe
   takes both, in both states, so one run answers the first defect and shows the second bare.

So the trial is not a new reading of the shader (there is nothing more to read). It is a switch that looks at
every texture a material reads, names in the log the ones the tracer no longer reads, and follows them.

## What `Kandra_Lit` computes (read from the game's files; nothing copied)

- Its property list (`scratchpad\kandra-work\kl-props.txt`, from the game's `resources.assets`): `_BaseColorMap`,
  `_BaseColor`, `_NormalMap`, `_NormalScale`, `_MaskMap`, `_AlphaCutoff`, `_EmissiveColorMap`, `_EmissiveColor`,
  `_EmissionIntensity`, the diffusion profile, and HDRP's usual surface switches. Nothing else.
- Its G-buffer pass (the variant the materials job unpacked, `scratchpad\mw\kl\blob396.txt`, read again line by
  line): four samples at the mesh's uv, with the game's global mip bias:
  - base colour = base map x `_BaseColor`;
  - normal = the normal map unpacked from alpha and green (alpha times red), x and y times `_NormalScale`, z toward 1
    under a scale of 1 (Shader Graph's Normal Strength), in the mesh's tangent frame;
  - mask map: metal = red, occlusion = green, smoothness = alpha, with no remap ranges;
  - glow = emissive map x `_EmissiveColor` x 2^(`_EmissionIntensity` - 3), shown at the same brightness whatever the
    exposure.
  Then HDRP's decal buffers are laid over colour, normal and smoothness (`_SupportDecals` 1) and the result is
  written. Its 96 variants differ by alpha test, double-sidedness, skinning, decals, SSR and subsurface only: none
  switches a texture on.
- Across the game's 1483 `Kandra_Lit` materials (`scratchpad\mw\census.json`): 1480 have a base map, 1438 a normal
  map, 1467 a mask map, 218 an emissive map; normal scale 1 on 1460 (up to 5.4 on 23), tint not white on 67, alpha
  test on 646, a glow on 174.

What the tracer reads of it:

| the shader | the tracer | the same? |
|---|---|---|
| base map x `_BaseColor`, the mesh's uv | the material's colour texture (the game's own) x base colour | yes. 5 materials (statues, a gate, damaged bricks in Sarras: `Mat_StoneStatues`, `Mat_StoneStatues_Stonewarden`, `Mat_StoneGateDoor`, `Mat_Arch_Sarras_Damage`, `..._Bricks`) carry a tiling of 2 or 8 on the base map, which the tracer applies; the shader's pixel code does not. Whether its vertex code does was not read: to check before trusting those five |
| normal map, alpha-green, `_NormalScale`, Normal Strength form | `TextureShare.Normal` (`_NormalMap`, `_NormalScale`), `TEXTURE_GRAPH_NORMAL` | yes; laid in the triangle's own texture directions, not the mesh's tangent (known: `char-normals.md`) |
| mask: metal R, occlusion G, smoothness A | `MaskLayouts["TG/Character/Kandra_Lit"]` | yes |
| glow | `GraphGlow["TG/Character/Kandra_Lit"]` | yes (PT-64) |
| decals over it | the decals' own route | not looked at here |

## The evidence

- **The helmet is material 7, read as the torso is.** The run's "Characters: what Kandra hands HDRP" lines name the
  guard's parts: head `Mat_ChMGenM_02_Head`, torso `Mat_ChHosM_01_Torso`, legs, feet, all `Kandra_Lit`. The
  yardstick names the helmet "Mat_ChMGenM_02_Head (TG/Character/Kandra_Lit, its mask map traced)": the world's
  material reading, with texture coordinates.
- **The yardstick could not see it.** It called the helmet the same as the game's on 97 % of its pixels. Its limits
  are 25 degrees for the normal, 0.1 for the diffuse colour and 0.2 for the roughness; a metal's diffuse colour is
  black either way and the grain tilts 4 to 20 degrees. A plain 64x64 stand-in passes it.
- **The blocks.** `guard.jpg` (run `084003`): torso, pauldron and sleeve in square texels some 30 pixels wide at 4K,
  and a perfectly smooth guard in the Normals view while the walls beside him show their relief.
- **The counts** (the plugin's "Shared textures (PT-45)" lines of that run, `metal`): above. In the run before
  (`071155`) the lines of the `metal` scene show no swap unseen (1643 held of 1643, 7 swapped, 7 freed), and
  there the guard's body is sharp. `084003` waited 572 s for a new build's passes to compile before its first
  traced frame, with the game streaming all the while: 1866 swaps.
- **Not memory:** in this session no texture was kept back for the video memory budget (the budget lines in the
  run's native log are from an earlier session of that morning; the file holds several).
- **Where a swap can be missed** (`TextureShare.Refresh`, read in the code; not seen happening):
  1. it looks at a texture again only when Unity's loaded mip level or update count changed, and writes the new
     level down BEFORE it knows the object changed. If the pointer is still the old one at that moment (or none:
     the new texture is being uploaded, which takes frames when hundreds are queued), the level is remembered and
     the texture is never looked at again;
  2. a level that went down and came back between two looks (each texture is looked at every 14 s with 1656
     watched, and not at all while the scan does not run) reads as "no change", and is a new object all the same;
  3. a new object at the old one's address reads as "no change";
  4. nothing ever asks the native side whether a handle still names a live texture.

## The trial

**The switch:** `[Advanced] Texture follow` (hidden, a whole number, default 0). The plugin alone: nothing travels
to the native side, no option bit, no ABI change, no shader compile.

| value | what it does |
|---|---|
| 0 | as before: none of it runs |
| 1 | each scan (2 s) every texture a traced material reads is asked for the object the game holds NOW, and the registry for the handle it has for that object. Where that is not the handle the materials name, the log names the texture, its size, what the game holds, and the materials that read it. Nothing is written: the picture as before |
| 2 | as 1, and those materials take the new handle and are sent again (`Follow`, the same lines a swap `Refresh` saw goes through) |

It is read every scan: one run shows it at 1 and at 2.

**The log line** (`BepInEx/LogOutput.log`; when something is found or followed, else every 30 s):

```
Shared textures on trial (PT-95, Texture follow 1): of 1640 textures traced materials read, 37 are named by a handle
of another texture than the game holds now (...): 37 replaced by a new texture, 0 by a new one at the old address;
37 found in this look; none followed (1 only looks); 212 are below their full size in the game right now (...);
the look took 3.1 ms. The game's streaming: ... Found in this look: 'Tex_ChMGenM_02_Head_Normal' 1024x1024 (the game
holds 1024x1024 with 11 levels now, its level 0 loaded) read by "Mat_ChMGenM_02_Head (TG/Character/Kandra_Lit, its
mask map traced)"; ...
```

(The numbers above are made up, to show the shape.) It also says how many textures are below their full size in
the game itself, names up to 12 of those that characters read, and gives Unity's streaming numbers (memory now,
wanted, aimed at, the budget): if the game itself holds the guard's maps coarse, that shows here.

**The recipe** (in the copy's `tests/ingame/scenarios.json`):

```
.\tools\harness\Run-PtHarness.ps1 -Scenario metal -Recipe texture-follow
```

| step | switch | what | frames |
|---|---|---|---|
| `1-game` | - | the game's own | 16 |
| `2-look-nrd` | 1 | NRD | 16 |
| `3-look-none` | 1 | no denoiser | 16 |
| `4-look-normals-view` | 1 | the Normals view | 4 |
| `5-look-albedo-view` | 1 | the Albedo view | 4 |
| `6-follow-albedo-view` | 2 | the Albedo view | 4 |
| `7-follow-normals-view` | 2 | the Normals view | 4 |
| `8-follow-none` | 2 | no denoiser | 16 |
| `9-follow-nrd` | 2 | NRD | 16 |

Crops: the helmet x 1540 y 440, 460 x 640; the whole guard x 1300 y 300, 1000 x 1860.

**What each outcome means:**

1. The line names the guard's textures (`Tex_ChMGenM_02_Head_*`, `Tex_ChMHosM_01_Torso_*`) at 1, and at 2 the Albedo
   view turns from blocks to the game's sharp textures and the Normals view shows the scales and the helmet's
   grain: the cause is the unfollowed swap. Then `9-follow-nrd` against `2-look-nrd` is the picture's answer.
2. The guard is in blocks at step 5 and the line says 0: the plugin's handles are right and the native side gives
   them up. Look at `texture_share.cpp resolve_locked` (a slot not published, the heap's state, the serial) next.
3. The guard is sharp at step 5 already (as in run `071155`: this fault comes and goes with the game's streaming).
   Then the run did not catch the first defect; the line's counts still say whether swaps go unseen in this
   scene. A run after a fresh build's long compile (as `084003`: 572 s) catches it. Steps 3 and 4 then show the
   second defect bare all the same.
4. With the guard's textures read (the Albedo view sharp), the Normals view shows the helmet's grain and
   `3-look-none` or `8-follow-none` shows it too, but the NRD picture is a smooth mirror: what filters a metal's
   reflections wipes its relief. A second defect with another fix (the reflections' filter and its guides, not the
   textures), and an entry of its own.
5. The Albedo view is sharp and the Normals view is SMOOTH on the helmet alone: its normal map does not reach the
   hit although its texture is read. Then look at that one material (its record's normal handle and scale: the
   line names what reads each texture) and at `hit_frame_normal` on its mesh.
6. The Normals view shows the grain and BOTH pictures are smooth and far too bright on the helmet: the metal's
   shading (PT-21), not its textures. The game's helmet reflects 18 % at a roughness of 0.41; `helmet-071155.jpg`
   shows the traced one brighter and glossier than that.

**What it costs.** The look asks Unity for every watched texture's native pointer (the first of a scan waits for
the render thread, as the registration already does; the rest are cheap) and the registry for its handle (a lock
and a map lookup). The line says the milliseconds. Not measured: no run. If the trial is kept, the native side
should say which handles are dead (a small export; no pointer asked for), `Refresh`'s own test by the mip level
goes, and the look runs in slices.

**Files changed** (the whole list):

```
src/TGPathTracing/Settings.cs       the hidden "Texture follow"
src/TGPathTracing/TextureShare.cs   Audit, Describe, ReadByCharacter; Refresh calls it; Reset clears it
src/TGPathTracing/SceneCapture.cs   MaterialLabel (a material's name by its index, for the line)
tests/ingame/scenarios.json         the recipe texture-follow
tests/source_checks.py              test_path_tracing_texture_follow_on_trial_default_off
docs/agents/kandra-lit.md           this report
```

`merge_copy.py kandra --base=base68 --dry` lists those and `tools\Build-PtShaders.ps1`: that one is the
coordinator's own replacement in this copy (the version that waits for room on the PC), not this job's.

## The other character and clothing shaders

From each shader's property list in the game's files and the 8,100 materials of the census. ONLY `Kandra_Lit` was
read in its compiled code in this job; "likely" below means: by its names, to be made sure by one look at its code
(`scratchpad\mw\shader_pass.py`, `blob.py`). Nothing of this table was built: none is both small and sure yet.

| shader (materials) | what it has | what the tracer reads | what is missing |
|---|---|---|---|
| `TG/Character/Kandra_Lit` (1483) | base x tint, normal x scale, mask (metal, occlusion, smoothness), glow | all of it | nothing in the reading. To check: the base map's own tiling on 5 materials (above) |
| `TG/Character/Kandra_Lit_Dynamic` (5: floating eggs and bulbs in Sarras, the crawling rot) | the same four maps; a vertex animation (`_Animation`, `_VPO`, `_VPO_Squiz`, `_VPO_Speed`, `_DYNAMIC`, `_CameraDistanceLerp_Min_Blend`, two textures read by the vertex shader) | the four maps by their names, the mask without its occlusion | likely: its glow is read HDRP Lit's way (no entry in `GraphGlow`), so 4 materials with an emission intensity of 1.5 to 3.2 glow far too dark by day, as `Kandra_Lit` did before PT-64; the mask's occlusion (`MaskLayouts` has none for it). Sure: its vertex animation is not traced (the traced mesh is the game's skinned one, before the material moves it) |
| `TG/Character/GhostCrew` (4) | `Kandra_Lit`'s four maps; a dissolve in threads (`_Transition`, `_NoiseScale`, `_NoiseOffset`, `_ThreadScale`, `_ThreadIntensity`, `_EdgeColor`, `_EdgeWidth`, `_Density`, `_Thickness`, `_Waviness`, `_WaveFrequency`, `_ExistenceChance`), `_USEBASECOLORALPHA` | the four maps, the mask without occlusion | likely: its glow (3 materials at intensity 6 and 8, no `GraphGlow` entry: far too dark). Sure: the thread dissolve and its glowing edge are not computed (all 4 materials are saved with `_Transition` 0.3; what the game sets while it runs is not known): the crew is traced whole |
| `TG/Clothes/Cloth_Wind` (39) | `_Base_Map`, `_Normal_Map` (no strength), `_Mask_Map`, `_Alpha_Clip`, `_Tiling`, `_Offset`, the wind | base, normal, mask (smoothness from blue, occlusion), the tiling and offset, the wind's pose | sure it is not read: `_Alpha_Clip`. The tracer cuts these at 0.5 (it looks for `_AlphaCutoff` and `_Cutoff`); 21 of the 39 materials have an `_Alpha_Clip` of 0 to 0.15, 7 have 0.56. Likely the shader's cutoff (its name); one line in `ReadMaterial` once its code is read |
| `TG/Character/RealSkin` (319) | many maps | its program (`MaterialPrograms`) | as its own log line says: tattoo emission and the thickness map |
| `TG/Character/RealSkin_Morph` (5) | `RealSkin`'s maps and a second set blended by `_Morph` (`_MorphDiffuseMap`, `_MorphNormalMap`, `_MorphMaskMap`) | not the skin program (that is for `TG/Character/RealSkin` by name): the diffuse and normal map by their plain names, no mask map | everything `RealSkin`'s program computes, and the morph |
| `TG/Character/RealHair` (27), `TG/Character/Eye/Eye` (16) | - | their own bakes (`docs/PT-DETAILS.md`) | not this job |
| `TG/Character/Ghost/Ghost_2_0_PW` (84), `Ghost_1_0` (2), `Ghost_DisappearWall_SphereMask` (2) | base, normal, mask, two ghost colours, a distortion map, three scrolling noises, a transition, a vertex animation | 83 of 84 are transparent: invisible to rays | the whole look of a ghost: with the particles and transparents (PT-37's neighbours), not a material rule |
| `TG/VFX/VFX_Icicles_Kandra` (4), `Shader Graphs/Kandra_Jelly` (3) | parallax, noise, fresnel, transmission | transparent: invisible to rays | the same |

## Workarounds met (removed or not)

- **The 64x64 tiles as a silent stand-in** (`atlas_sample`, `metallic_smoothness`, the atlas bake): a copy of every
  texture, shown without a word whenever a handle does not resolve. It is what hid this fault as "a helmet without
  detail" instead of a count in the log. Not removed (PT-60's and PT-45's): once every swap is followed for sure,
  a material whose handle is dead should be counted by the native side in its 10 s line, and the tile go.
- `TextureShare.Refresh`'s test by mip level and update count: kept at 0; goes if the trial is kept (above).
- Comments that still say textures of "64x64 or less" keep their tile (`pt_common.hlsli` above `shared_on`,
  `texture_share.cpp` at `shareable`, its log line): every size is shared since PT-64. Not touched.

## Not done

- The brief's step 4 (an offline check): after the game's answer, and its subject is now the follow, not a shader
  reading. A fresh worker: a registered texture replaced by a new object at the same level, and at the same
  address; the material must read the new one within a scan.
- The compiled code of the shaders in the table.
- Why the game swaps so many textures with the camera still (1866 in ten minutes): the trial's line gives Unity's
  streaming numbers; not looked into.
- The native build: nothing native changed, so none was made (and the PC was short of memory for shader compiles).

## Builds, commands

Nothing installed, nothing deployed, the game never started, no GPU test run, nothing written outside the copy,
`%TEMP%\tgb\kandra-plugin` and `scratchpad\kandra-work`.

- Plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472 -o
  %TEMP%\tgb\kandra-plugin`: 0 warnings, 0 errors. `TGPathTracing.dll` SHA-256 starts `FEBBDE7B802F31C5`. (It
  leaves `obj` folders under `src` in the copy.)
- Source checks: `python tests/source_checks.py`: 102 of 102 (101 and the new one).
  `python tests/harness_analyze_test.py`: exit 0.
- Read only, from the game's folder: `resources.assets` (the shaders' property lists), four asset bundles (the
  helmet's and the torso's materials and textures; pictures of the helmet's three maps in
  `scratchpad\kandra-work\tex`, for looking only).
- Read only, from the main tree: `artifacts\harness\20261009-084003`, `20261009-071155`, `20261009-092414` (that
  one traced nothing: no D3D12 device, the PC was out of memory).
