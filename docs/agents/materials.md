# Materials as the game shows them, proven by the yardstick (PT-64): agent "materials", 2026-10-07

Copy `scratchpad\wt\mats` (base `wt\base36`). Scripts, decodes and logs: `scratchpad\mw\`. Nothing here ran in the game.
The four claims, kept apart: **built** yes; **passes the offline harness** see "State"; **seen in game** no; **the owner
says it's fixed** no.

## In short

- **Why the forest looked wrong.** The game draws its rock, grass, ground, leaves, trunks, wooden buildings and stone walls
  with seven shaders of its own. The tracer read all six as if they were Unity's standard shader: one texture, one tint, nothing
  else. So it missed the rock's close-up layer and moss, the grass's two colours, all eight ground layers, and so on.
- **What is done.** For each of the seven the tracer now computes what the game's shader computes at a hit (a "program":
  colour, bumps, shine), from the material's own textures and numbers. Each was read from the game's compiled shader.
  Plus: Unity's detail map, each glass's own bending of light, small textures read as the game's own (no 64x64 copies),
  and a reflection no longer borrows the on-screen picture's material.
- **The yardstick now says how each material differs** (place, colour, normal, roughness), so a line tells a material
  fault from a geometry fault.
- **Not done:** the list under "What is left". The biggest: characters' skin (319 materials), `Dissolve3D` (201), the
  snow shaders, parallax relief, thin cloth's share of light (the material holds no number for it).
- **The coordinator's runs `20261007-191814` (forest) and `20261007-185958` (vista)** were of the main tree's build, which
  has none of this: there the Cliff, MeshTerrain and TriplanarArchitecture2 materials differ on 97 to 100 % of their
  pixels. Those three are the programs CLIFF, TERRAIN and TRIPLANAR here.

## State (kept current)

- 17:20 started. 17:45 found why the tavern's two worst materials differ. 17:50 the coordinator moved the outdoor
  shaders first (run `20261007-171522`, the forest). 18:00 all six programs written; native and plugin build; source
  checks 90 of 90. 17:55 the owner's rule "remove all the hacks as we go": applied (section below).
- 18:15 to 19:20 GPU runs: every new check passes; 36 breaks in six mutant builds, each caught (below). The final
  clean run (`g`) was slowed by Starfield (the owner's rule allows up to four offline tests while they play).

## What the yardstick named, and what each one was missing

Forest (run `20261007-171522`): the same surface on 65 % of the screen; of those pixels another colour 27 %, another
normal 37 %, another roughness 47 %. Tavern (run `20261007-165954`): 99 %; 3 to 5 %, 2 to 4 %, 0.2 %.

| shader (materials in the game) | worst material, % of its own pixels | what the tracer read | what the game's shader does |
|---|---|---|---|
| `TG/Environment/Cliffs/Cliff` (158) | Mat_Cliff_Formation_01_Cuanacht_City 97 | `_L0_BaseMap` over the whole rock, white tint, no normal map | three layers: the formation's own maps (tint `_L0_Tint` 0.46); a tiled rock layer that takes over within 200 m (`_L1_*`, tint 0.43, tiled by the object's size); moss where the rock faces up, by a mask over the world and the mesh's painted red (`_L2_*`); edges brightened and hollows darkened by the mask map; shine from the colour maps' red |
| `TG/Vegetation/Vegetation_Grass` (56) | Mat_Vegetation_Grass_Meadow_01_SG 1, 87 | its map times the mean of two colours | its map times `_HealthyColor`, toward `_DryColor` by a noise texture over the world; times 1.5 with "fuzzy shading" (21 materials); the ground's colour at the blade's foot (47 materials) |
| `TG/Environment/MeshTerrain` (194) | Mat_UT_C_01_d_CampaignMap_HOS 97 | `_L1_BaseColor` by the mesh's coordinates, white | eight layers laid flat over the world (x, z), each with its tint, tiling, normal and mask map, blended by two splat maps and sharpened by each layer's height |
| `TG/Vegetation/Vegetation_Leaves` (66) | pine 77, beech 52, ivy 70, dwarf pine 99 | its map times the mean of two colours | as grass, the noise read from three sides of the world |
| `TG/Vegetation/Vegetation_Trunk` (42) | Mat_Pine_trunk_09_nowind 85 | `_TrunkBaseColorMap`, white, no normal or mask map | trunk colour `_TrunkBaseColor` (0.67, 0.60, 0.49); a bark layer over it by `_TrunkBarkMask`, read by the mesh's fourth set of texture coordinates |
| `TG/Architecture/KF_Architecture` (68) | Atlas_Wood_Furniture_01 82, Mat_Furniture_Wood_01 95 | its map, white (its tint is `_Base_Tint`: not a name the tracer looked for), no normal map (`_Base_NormalMap`) | tint 0.80 grey; normal map with strength (-1.06 on one); a "micro detail" normal; an overlay layer by slope, mask noise and height above the object's foot (31 materials); a slight hue turn from place to place |
| `TG/Architecture/TriplanarArchitecture2` (44) | vista: Mat_3P_Arch_Sagremor_StoneAndDirt_A 99, Cuanacht walls and floors 99 to 100 | `_LayerA_BaseMap` by the mesh's coordinates | two layers and a blend map laid over the world from three sides, layer B by its height, four dirt channels, three-sided normal maps (row below) |
| `HDRP/Lit` (1631) | plain ones 1 to 40 | base map, normal map, mask map: all it has | the same. 26 materials also have a detail map (now read). Their differing pixels are not a missing feature: see "Geometry, not material" |
| `TG/Character/Kandra_Lit` (1483) | Mat_ChFL_TavernAlisa_Torso 22 to 27 | base map, tint, normal map, mask map | exactly that (its shader was read: nothing else in it). Its glow was read wrong: fixed. Its 22 % is not explained by the shader: the new breakdown will say |

Numbers of materials: a count of every material in the game's files (`scratchpad\mw\census.py`, 8,100 materials).

## How each program was read, and how sure it is

Each of the game's shaders is a Shader Graph compiled to machine code. `scratchpad\mw\shader_pass.py` lists a shader's
variants with the names and places of its values (Unity keeps those beside the code); `blob.py` turns one variant into
readable instructions; I read the GBuffer pass (the one that writes colour, normal and shine) line by line and wrote
the same arithmetic in `native/pathtracing/shaders/pt_materials.hlsli`. Nothing of the game's code is in the repository.

| program | read from | sure about | not sure about |
|---|---|---|---|
| Cliff | `mw\cliff\blob156.txt` (with moss), `blob157.txt` (without) | every line of colour, shine and normal, both variants | which mesh tangent the game uses (below); the variant with `_EMISSIVE` (1 material) not read |
| Grass | `mw\grass\blob520.txt`, `blob519.txt` (ground colour) | colour, the two keywords | the colour a gust adds (`_WindAddColor`): needs the wind's noise and speed, which the tracer does not have (the world agent brings them for the sway). Small: 0.2 x wind speed x 0.0025 |
| Leaves | `mw\leaves\blob226.txt` | colour | - |
| Trunk | `mw\trunk\blob58.txt`, vertex shader `blob48.txt` | both layers; that the bark's coordinates are the mesh's TEXCOORD3 | - |
| MeshTerrain | `mw\terrain\blob55.txt` | the layers, the splat blend, the heights | its normal's sign along z (tangent, below); the variant with subsurface scattering (24 materials) read as the plain one |
| KF_Architecture | `mw\kf\blob258.txt` | base, overlay, micro detail, hue, shine, occlusion | parallax relief (1 material) and its glow (4 materials) are not built |
| TriplanarArchitecture2 | `mw\tri\blob52.txt` | both layers and the blend map from three sides of the world turned by its rotation; the blend's smoothstep; layer B by its height; the four dirt channels; the normal maps from three sides, the blend map's normal (by `_Thickness` x `_Blend`, which is -1 on many: the game turns that bump over) and the bump halved where the surface faces away from the sun | - |
| Kandra_Lit | `mw\kl\blob396.txt` | everything (it is short) | - |
| HDRP Lit's detail map | not the game's code: Unity's HDRP Lit, from memory of how it works | the arithmetic matches Unity's documented look | not compared with the game's picture; 26 materials |

**The tangent frame.** The game's shaders turn a normal map by the mesh's own tangents. The tracer does not read
tangents from a mesh; it takes the direction of the texture's u across each triangle (as since PT-46). For meshes Unity
imported that is the same direction. Where it can differ: the terrain (its layers are laid over the world, but turned by
the mesh's tangent). If the terrain's bumps look lit from the wrong side in game, the fix is to read the mesh's tangent
stream (a new mesh attribute, like the fourth texture coordinates added here).

## What was built

1. **Programs** (`pt_materials.hlsli`, `MaterialPrograms.cs`): a material names its program and a record of its textures
   and values. A program runs only when every texture its material has is open in the tracer; until then the material
   is read the old way. Records are sent apart from the materials (`TGPT_SetMaterialPrograms`).
2. **What a hit needed for them** (`pt_common.hlsli` `Hit`): the object's transform (its size tiles the rock's layer;
   its own height sets the grass's ground colour), the ray's footprint in the world (for textures laid over the world),
   a fourth set of texture coordinates (`TGPT_MeshDesc.uv2*`: Unity's TexCoord3), and the mesh's vertex colours (now
   sent for every mesh that has them: the moss follows their red).
3. **Each material's own values:**
   - Glass bends light by its own `_Ior` (`TGPT_Material.ior`) where its shader has one; 1.5 where it has none.
   - The mask map's occlusion goes through the material's own range (`_AORemapMin/Max`: 102 materials end it at 0.39).
   - A normal map's scale as Unity applies it (it was applied before taking z, which flattened steep bumps at a scale
     above 1); the game's graphs' "Normal Strength" told apart; a back face in HDRP's "Flip" mode turns its bumps.
   - The tangent no longer flips with a negative tiling (the game's does not: `Mat_Furniture_Wood_01` has -1.64).
   - The glow of the game's own shaders (characters' `Kandra_Lit`, grass, leaves): colour x map x 2^(intensity - 3),
     always shown at the same brightness whatever the exposure. Read the old way, a character's glowing parts were
     about ten thousand times too dark by day. The emissive map is read as the game's own texture, not a 64x64 copy.
4. **No copies of small textures** (`texture_share`): every size is shared. See "Small textures" for the cost.
5. **No borrowing from the screen:** `hdrp_material_at` is deleted with its two callers' branches.
6. **The yardstick by material says how:** of a material's own pixels, in another place (geometry), another colour,
   another normal, another roughness (`TGPT_Yardstick.materials[]`, the plugin's line, the native line's counters).

## The offline checks

All judged without a denoiser (Denoising None: `TGPT_OPTION_NO_DENOISER`), with the tracer's own camera rays. Every map in
them is 64x64 or 128x128 with one mip (16 and 64 KB): each is read as the game's own texture, none from an atlas tile. The
expected values are worked out in the test from the game's compiled code's arithmetic, not from the tracer's shader.
Logs: `scratchpad\mw\gpu-<run>.txt`; pictures `%TEMP%\tgb\mats-images-<run>`.

| check | tracer | game's arithmetic |
|---|---|---|
| KF_Architecture base: map times tint | 0.1301 (without its program 0.5972) | 0.1295 |
| its overlay by slope / by the occlusion mask / by the mask noise / below the bottom height | 0.2622 / 0.3276 / 0.1980 / 0.3967 | 0.2626 / 0.3291 / 0.1960 / 0.3956 |
| its normal map; with the micro detail normal | (0.498, 0.004, -0.867); (0.485, 0.485, -0.727) | (0.500, 0, -0.866); (0.485, 0.485, -0.728) |
| its roughness; fading with distance | 153; 164 of 255 | 153; 164 |
| its hue turn over the world, 17 points | off by 0.0008 a channel | the turn itself 0.0107 |
| Cliff: rock layer up close | 0.6235 | 0.6252 |
| with edges and hollows / moss / at a coverage leaving a wall bare | 0.3611 / 0.3184 / 0.6235 | 0.3601 / 0.3165 / 0.6235 |
| rock layer tiled by the object's size (four stripes on a 2 m panel) | 0.171 0.624 0.171 0.624 | dark, light, dark, light |
| moss fuzz | 0.6938 | 0.6935 |
| roughness | 24 of 255 | 24 |
| grass: healthy to dry by the noise | (0.279, 0.515, 0.159); without its program (0.502, 0.502, 0.502) | (0.277, 0.514, 0.158) |
| with fuzzy shading; leaves | (0.418, 0.775, 0.238); (0.279, 0.515, 0.159) | x1.5; as grass |
| grass's ground colour at its foot, top / foot | (0.279, 0.515, 0.159) / (0.301, 0.100, 0.100) | grass's / (0.3, 0.1, 0.1) |
| trunk: own layer / bark by the fourth coordinates | 0.1070 / 0.1946 | 0.1079 / 0.1943 |
| terrain: splat map picks layer 1 | 0.3967 | 0.3956 |
| layer 0 over the world at x 0.25, 1, 1.75 m | 0.2158 0.7910 0.2158 | 0.2159 0.7913 0.2159 |
| its roughness | 178 of 255 | 179 |
| HDRP Lit's detail map: albedo, roughness | 0.3562, 102 | 0.3581, 102 |
| normal map 30 degrees at scale 2 / 0.5 Lit / 0.5 graph | 48.9 / 16.0 / 14.9 degrees | 49.1 / 16.1 / 15.0 |
| back face Mirror / Flip; tiling -1 | x 0.497 / -0.498; 0.498 | 0.5 / -0.5; 0.5 |
| emissive map read (red 0.25) / by red alone | (0.251, 1, 1) / (0.251, 0.251, 0.251) | as said |
| a program waits for a texture its material has | (0.498, 0.004, -0.867) | HDRP Lit's (0.500, 0, -0.866) |
| yardstick: a traced-only cube's pixels by kind | place 9591 of 9591, colour 0 | all "place" |
| yardstick: the floor read dark and smooth | colour 521492, roughness 521492, place 0 of 521492 | colour and roughness |
| a jar's view ends on the traced wall's own checker, HDRP drawing it plain or checkered | follows by 1.00, contrast 1.00 | the same |
| glass by its own index (1.5 / 1.2 / 1) | see run `g` | weaker spot at 1.2, none at 1 |
| small textures shared (a 64x64 and a 128x128 BC1, 2 and 8 KB) | numbers 4 and 0, each counted 64 KB | shared |

**Mutants** (`scratchpad\mw\mutants.py`: each set breaks several lines at once, each line failing its own check; graded by
`grade.py`: the check FAILs in the mutant and passes in the clean run):

| set | breaks (each caught by its own check) |
|---|---|
| mA (17) | micro detail normal; distance smoothness; hue turn; occlusion mask; mask noise; moss fuzz; rock layer by object size; the cliff layers' smoothness; fuzzy shading; ground colour; terrain smoothness scale; detail smoothness; Normal Strength as Lit's; Flip mode; glow by red alone; yardstick "place" counted as colour; bark by the first coordinates |
| mB (9) | KF base normal map; overlay share by slope; rock layer up close; grass colour variation; terrain by the mesh's coordinates; detail albedo; normal scale before z (as before); tangent flipped by tiling (as before); emissive map not read |
| mC (8) | overlay at the foot; edges and hollows; moss cover; leaves' third plane; splat map; a program running without its textures; KF tint; **the borrowing from the screen put back** |
| mD (1) | small textures left to copies again |
| mE (1) | every glass bends as 1.5 again |
| mG (4) | triplanar by the mesh's coordinates; its layer B; its dirt; its bump halved away from the sun |
| mF (1) | glass of index 1 solved as if it bent the light |

Caught: see the grade line at the end of this report's state (filled in when `g`, `mE`, `mF` end).

**Two runs went wrong, not by a break:** run `f` (the clean build of 18:17) and mutant `mA` lost their D3D12 device part way
(`DXGI_ERROR_DEVICE_HUNG`, 1525 and 4434 failed frames; every later check failed with it), while four tests ran at once
with the other agents'. The same code in `mB` and `mC`, run at the same time, lost nothing and failed only their own
checks; no GPU reset in Windows' log. I read it as the load, not the code; run `g` is the answer. If `g` hangs too, the
first suspect is `hit_material`'s programs (the terrain's reads 26 textures a hit).
Also failing under that load, in every run: "the caustics and their lookup stay affordable" (a timing: 8.75 ms against
10 when four tests share the GPU; 2.97 ms in run `c`).

## Small textures (item 3)

- Before: a texture under 64 KB with its mips was not shared and was read as a 64x64 copy (31 of 494 in the tavern run,
  285 of 815 in the run the brief quotes). Now every size is shared; the rule's code is deleted.
- **What it costs:** an opened texture takes whole 64 KB pages. So each small one counts 64 KB in the tracer's count:
  285 of them 17.8 MB, 31 of them 2.0 MB. It is the same memory the game already holds (no copy is made); the 64 KB is
  how Windows counts it against the game's budget. Not measured across a session: nothing ran in game. The plugin's
  "Shared textures" line now says how many are 64 KB or less and their MB, so one run gives the real number.
- **What still reads an atlas tile, and why:**
  1. Any material whose texture has no number yet: made before the hook, refused by the driver, in a format the
     start-up self-test failed, held back by the video memory cap, or a streamed texture's small stand-in. The tile is
     the fallback. It cannot go until those cases are zero.
  2. Cut-outs: the alpha test and its micromaps read a 256x256 coverage tile, or the colour tile's alpha
     (`alpha_opaque`, `material_alpha`, `pt_omm_bake`). A copy. Replacing it means reading the game's alpha at a ray's
     footprint and rebuilding the micromaps from it: a job of its own.
  3. Glass's colour for shadows and thin cloth's tint (`glass_color`, `cloth_tint`, `emission_coverage`): the colour
     tile. Not changed: glass and cloth are other jobs' parts.
  4. Hair's and eyes' baked colours (512x512 detail tiles) when their own maps are not open; see-through glass's
     detail tile likewise.
  5. Metallic and smoothness from a mask tile (`metallic_smoothness`) when the mask map is not open.
  6. The emissive map's tile when the emissive map is not open.

## Geometry, not material (for the world agent's job)

- The forest: the tracer's hit is nearer on 19 % of the screen and farther on 16 %. That is another mesh, level of
  detail or pose, not a material.
- The tavern's plain `HDRP/Lit` materials (Mat_Prop_Tablecloth_01 22 to 41 %, Mat_Prop_FloorRag_05 12 to 23 %,
  Atlas_Rope_01 10 to 39 %, Mat_Prop_Curtain_grey, Mat_Prop_Table_01, MI_Wood_01 3 to 7 %): I read all of their saved
  values (`scratchpad\mw\tavern-mats.txt`). They use nothing the tracer does not read: base map, colour, normal map,
  mask map. They are thin things lying on other things (a rag on the floor, a cloth on a table, a rope). The yardstick
  calls two surfaces "the same" within 1 cm plus 1 % of the distance (4 cm at 3 m), so where the traced rag is at
  another level of detail than the drawn one, the ray meets the floor instead: "same surface, another colour". My
  reading: geometry (PT-61). The new breakdown cannot tell this case apart (it lands under "colour"); a tighter
  distance, or comparing which object was hit, would.
- Decals are in HDRP's G-buffer and not at a ray's hit: they show as "another colour" on the material under them.

## What is left, with numbers

| what | how many | why not done |
|---|---|---|
| `TG/Character/RealSkin` | 319 materials | not read yet (tattoos, make-up, detail normals). The subsurface agent owns skin's light |
| `TG/Environment/Snow/Snow` 47, `Cliff_Snow` 34, `KF_Layered` 8 | 89 | not read; the same method fits each |
| `TG/Vegetation/Vegetation_Billboard` | 34 | not read |
| `TG/Clothes/Cloth_Wind` | 39 | read (`mw\cw\blob88.txt`): colour = its map, mask red metal, blue smoothness, green occlusion. Fixed here: its normal map is `_Normal_Map` (cloth had no bumps), its green is occlusion. No program needed |
| `TG/Dissolve/Dissolve3D` | 201 | its colour, normal (`_NormalStrenght`) and mask names found; its compiled code's blob numbers do not fit the unpacked table (the unpack script needs a fix for it), not read |
| `TG/VFX/VFX_Lit_Aura` 97, `Ghost_2_0_PW` 84 and other effect shaders | 200+ | effects: PT-67 |
| Parallax relief | `KF_Architecture` 1, `HDRP/Lit` 6 | needs a height-map walk at the hit |
| KF_Architecture's glow | 4 | a moving 3D noise: needs the game's time at a hit; read (`mw\kf\blob258.txt` lines 539 to 689), not built |
| Cliff's glow (`_EMISSIVE`) | 1 | its variant not read |
| HDRP Lit: specular colour workflow | 9 | not built (metallic 0 is used) |
| HDRP Lit: Double-Sided "None" mode | 28 | a back face is lit as if facing away; not built |
| HDRP Lit: object-space normal map | 1 | not read |
| HDRP Lit: an emissive map with a tiling of its own | 1 | read with the base map's tiling |
| Thin cloth's share of light let through (0.3 for all) | every thin cloth | **the game's material holds no number for it**: plain cloth is HDRP Lit with no transmission, so the game lets nothing through. A sheet's physics needs its thickness or weave density, which is not in the material. I did not invent one. To decide with the owner |
| Leaves' and grass's share of light let through | 122 | the subsurface agent's (`wt/port2`) from the diffusion profile. Note for it: the grass and leaves shaders take thickness = saturate(mask map alpha + `_Thickness`) |
| Glass with no `_Ior` in its shader | not counted | 1.5 is used, named as "ordinary glass", not the material's own |
| Mesh tangents | all normal-mapped meshes | the frame comes from the texture's directions (above) |
| The grass's wind colour | 56 | needs the wind's inputs |

## Workarounds that exist only because the tracer started from HDRP's picture

The owner, 17:55: "remove all the 'hacks' and workarounds as we go ... so we have a clean base to work from."

| where | what | removed? |
|---|---|---|
| `pt_common.hlsli` `hdrp_material_at`; `pt_specular.hlsl` (see-through glass's far side); `pt_water_under.hlsl` (the bed) | a ray's hit took HDRP's on-screen colour and normal | **removed**, with its source check turned around |
| `texture_share.hpp` `kMinBytes`, `texture_share.cpp` `large()`, `TGPT_SHARE_SMALL`, the plugin's reason text | small textures left to 64x64 copies | **removed** |
| `pt_common.hlsli` `material_normal`: the tangent turned with a negative tiling | made the tile-era bumps "look right"; the game does not do it | **removed** |
| `SceneCapture.cs` `ReadMaterial`: grass and leaves tinted by the mean of their two colours | stood in for the shader's noise | kept as the reading while a program's textures are not open yet; dead once they are |
| `pt_common.hlsli` `atlas_sample`, `metallic_smoothness`; `d3d11.hlsl` `TGPT_ATLAS`; `TGPT_RegisterTexture`, `TGPT_RegisterMaskTexture`, `TGPT_RegisterDetailTexture` | 64x64 (and 512x512) copies of textures: enough when rays only carried bounced light | not removed: six readers left ("Small textures") |
| `pt_common.hlsli` `g_firstKnown`, `g_firstMaterial` | the caller hands shade_path a material | not removed: the water pass uses it for its surface (another job's). For the bed it is now a plain copy of the hit's material and could go |
| `pt_common.hlsli` `passes_through`, `near_back_towards`, `trace_from_surface`, `TGPT_INSTANCE_STAND_IN`, `OPTION_NO_PASS_THROUGH` | rays left from HDRP's surface, which sits inside the traced mesh | not mine (hair-6's report names `Surface.onMesh` as the way out) |
| `pt_common.hlsli` `MASK_SWAYS`, `SWAY_REACH` | plants traced standing still | not mine (the world agent's) |
| `pt_prepare.hlsl`: the forward-pixel camera ray, `g_strandsFrom`; `pt_primary.hlsli` `primary_pixel`; `TGPT_OPTION_GBUFFER_SURFACES` | the old route | not mine (the old-route switch) |
| `MATERIAL_CAMERA_ONLY`, `MATERIAL_HDRP_SHADED` | eyes and unlit decals left to HDRP | not mine (eyes) |
| `pt_hdrp_shadow.hlsl` | traced shadows on what HDRP shades | not mine |
| `pt_sss.hlsl`, `leaf_tint`, `leaf_transmittance`, `skin_transmittance` (read HDRP's G-buffer) | skin and leaves from HDRP's buffers | not mine (the subsurface agent's) |
| `TGPT_VIEW_MATERIALS` | a debug view of the traced material beside HDRP's albedo | kept: the yardstick does not replace looking at it |

## For the in-game run

One scene per part, Denoising None first, read the plugin's `Yardstick 10s` line in `BepInEx/LogOutput.log`.

1. **Forest** (scenario `forest`, as run `20261007-171522`). The line now ends each material with
   `(place P, colour C, normal N, roughness R)`: shares of its own pixels. Targets, as predictions (nothing measured):
   - `Mat_Cliff_Formation_01_Cuanacht_City`, the grass, `Mat_UT_C_01_d_CampaignMap_HOS`, the leaves, the trunks:
     colour under 10, roughness under 10. Before: 97, 87, 97, 52 to 100, 46 to 85 in all.
   - The whole screen, of the pixels on the same surface: another colour from 27 % to under 8 %, another roughness from
     47 % to under 10 %, another normal from 37 % to under 20 %.
   - "place" will stay high (19 % nearer, 16 % farther on screen): that is the world agent's.
   - Look for the line `Materials computed as the game's shaders compute them (PT-64): ... N of them not open when
     asked`. If N is large, the programs are waiting for textures: read the `Shared textures` line for why.
2. **Vista** (as run `20261007-185958`): `Mat_UT_C_02_c_CampaignMap_Cuanacht`, `Mat_Cliff_01_Cuanacht_Black`,
   `Mat_Cliff_Formation_01_Cuanacht`, `Mat_3P_Arch_Sagremor_StoneAndDirt_A` and the other `Mat_3P_Arch_*`: colour and
   roughness under 10 each (before 99 to 100 % in all). The screen's "another roughness" from 71 % to under 15 %.
3. **Tavern** (the `hair-moving` save, as run `20261007-165954`): `Atlas_Wood_Furniture_01` and
   `Mat_Furniture_Wood_01` colour under 10 (before 82 and 95 in all). The whole screen: another colour under 2 %.
4. If a program material still shows colour above 30: F8 on it, compare the Albedo view with the game's picture, and
   send me the material's name; its values are in `scratchpad\mw\census.json`.

## Struct and export changes (say-so as the brief asks)

- `TGPT_Material` 160 to **176 bytes**: `program`, `programRow`, `emissiveTexture`, `ior` appended after
  `textureReserved[3]` (which `wt/port2` turns into `subsurface[3]`: no overlap).
- `TGPT_MeshDesc` 160 to **176 bytes**: `uv2Stream`, `uv2Offset`, `uv2Format`, `uv2Dimension` appended.
- `TGPT_Yardstick` 376 to **632 bytes**: each material entry gains `place`, `colour`, `normal`, `roughness`.
- New export `TGPT_SetMaterialPrograms` and `TGPT_PROGRAM_*` 0 to 8 (`TGPT_PROGRAM_COUNT` 9). Removed `TGPT_SHARE_SMALL`.
- New `TGPT_TEXTURE_*` bits 32, 64, 128, 256 and the occlusion range in bits 12 to 27 of `textureFlags`.
- `TGPT_Frame.forward[3]` (was unused): the game's `_GlobalSaturationAdjustment`. Size unchanged.
- `FrameConstants` unchanged in size: `textures.w` (was unused) is the programs' buffer. `MeshRecord` 36 to 40 bytes
  (`uvs2`). `MaterialRecord` 176.
- No option bit and no material bit taken. `TGPT_ABI` left at 0x00011F00: bump it at the merge.
- `CMakeLists.txt`: one line, `pt_materials.hlsli` added to the path tracing shaders' dependencies.

## How to merge

- `merge_agent.py <copy> --base=wt\base36 --dry` lists the files. New: `pt_materials.hlsli`, `MaterialPrograms.cs`,
  this report. Shared with others and merged by hand where they collide: `pt_common.hlsli`, `tgpt_api.h`, `scene.cpp`,
  `scene.hpp`, `tracer.cpp`, `SceneCapture.cs`, `tests/source_checks.py`, `tests/pathtracing_gpu_test.cpp`.
- While a program waits for its textures, a Cliff or Trunk material reads its mask map as a constant (its old smoothness,
  no metal), so it looks as before PT-64 until then, not rougher.
- The plugin registers vertex colours for every mesh now. The cloth sway reads them as its paint mask where the game's
  shader does (`pt_sway.hlsl`): cloth that the game pins by paint is pinned in the tracer too from now on. Not tested
  with a painted cloth.
- `src/TGPathTracing/obj` and `bin` in this copy are my compile check's output: not to merge.

## Ideas, not built

- Read mesh tangents (a fifth stream), then every normal map is turned exactly as the game turns it.
- The yardstick could keep which instance HDRP drew (from its object ID buffer, if the game has one) to tell "another
  thing within 4 cm" from "the same thing read otherwise".
- One generic reader for the game's remaining Shader Graphs is not possible from their compiled code alone; but the
  five scripts in `scratchpad\mw\` turn each into readable arithmetic in a few minutes, and a program is 30 to 60 lines.
