# Who takes the scattering walk (PT-40 wax, PT-73 c speed, i armour): agent "walks", 2026-10-08

Copy: `scratchpad\wt\walks` (base: `wt\base50`). Brief: `scratchpad\brief-who-walks-filled.md`. Build folder:
`%TEMP%\tgb\walks-build` (the untouched base built beside it: `%TEMP%\tgb\walks-base-build`). Research scripts and the
decoded shader text (never in the repo): `scratchpad\whowalks\`.

## State (kept current)

- 03:45 started. 04:00 the game's own rule is known (section 1), written down before any code.
- 04:20 the code is written (section 2): plugin, tracer, shaders, test. Source checks 95 of 95. The plugin compiles
  (`dotnet build`, 0 warnings). The shader passes pt_walk, pt_yardstick, pt_prepare, pt_gi compile.
- 04:37 the native build of the copy is done (10 minutes of shader passes, then the DLL and the test).
- 04:41 the job's own GPU checks: all pass (section 5). 04:42 the whole GPU test started, once.
- Nothing is seen in game. "Built" and "passes the offline test" are the two claims made here.
- The coordinator's message (04:25): the base's own failing checks are listed in section 5; every GPU run waits 900 s
  for the passes; the red dots on the hero's face with no denoiser are answered in section 4.

## In short (plain words)

Today the tracer walks under the surface of every material that names a diffusion profile (a "how far light
scatters" asset). That is not what the game does. The game's shaders scatter only where the material has the
subsurface feature switched on, and then by a mask (0 to 1) per point. Counted over all 8,100 materials in the game's
files:

| shader | materials | name a profile | really scatter (feature on) | their mask |
|---|---|---|---|---|
| `TG/Character/Kandra_Lit` (clothes, armour, weapons, creatures) | 1,483 | 700 | **12** | 1 everywhere |
| `TG/Environment/MeshTerrain` (the ground) | 194 | 194 | 24 | **0 everywhere** (so: none) |
| `TG/Character/RealSkin` (skin) | 319 | 315 | 318 | per point: a map times a number (0.2 to 2) |
| `HDRP/Lit` | 1,631 | 41 | 13 (wax, stained glass, ice, a hanging body) | the material's number (all 1) |
| `TG/Environment/Snow/Snow` | 47 | 31 | 25 | per point: where the snow lies |
| `TG/Environment/Cliffs/Cliff_Snow` | 34 | 34 | 33 | per point: where the snow lies |
| `TG/Environment/IceLake` | 4 | 4 | 3 | 1 everywhere |
| `TG/Dissolve/DissolveWithSnow` | 2 | 2 | 1 | per point: where the snow lies |
| `TG/Character/Eye/Eye` | 16 | 16 | 16 (built into the shader, no switch) | 1 everywhere |
| `TG/Dissolve/Dissolve3D`, `Cliff`, `VFX_Lit_Aura`, `Ghost_2_0_PW`, `KF_Architecture`, `RealHair`, ... | 600+ | 170+ | **0** | - |

So 688 clothes, armour and weapon materials walked for nothing, and 170 terrain materials (the 24 that have the
feature on have a mask of 0: the game's ground never scatters). That is the "too many". The "too few" is the wax:
`Mat_Prop_Candles_02` has the feature on and a mask of 1, and was left out only because it is drawn from both sides.

## 1. What the game's shaders use (the evidence)

**The material type is a keyword on the material.** Every one of the game's lit shaders (HDRP Lit and the game's own
Shader Graphs) is compiled in variants; the variant with scattering is chosen by the material's keyword
`_MATERIAL_FEATURE_SUBSURFACE_SCATTERING` (with it, in this game, nearly always `_MATERIAL_FEATURE_TRANSMISSION`).
`_MaterialID` (0 subsurface, 1 standard, 5 translucent) is the number the keyword is set from; the keyword is what
the GPU program is picked by. A diffusion profile's hash on a material means nothing by itself: it is a property
every material of these shaders carries, set or not.
Evidence: `scratchpad\whowalks\census_sss.py` and `census_list.py` over the materials worker's census of the game's
bundles (`scratchpad\mw\census.json`, 8,100 materials); the variants' keyword lists of each shader's GBuffer pass
(`scratchpad\whowalks\<shader>\gbuffer_pass.txt`, written by `sss_mask2.py`).

**The mask is what each shader writes into HDRP's G-buffer**, read from the compiled GBuffer pass of each shader's
subsurface variant (`sss_mask2.py`; the first target's alpha holds mask x 15/255 + profile index x 16/255, the third
target's blue the same with the transmission mask, its green the thickness):

| shader | subsurface mask | transmission mask | thickness |
|---|---|---|---|
| `HDRP/Lit` | `_SubsurfaceMask` (x its mask map's red with `_SUBSURFACE_MASK_MAP`: no scattering material of the game has that map) | `_TransmissionMask` | `_Thickness` (a map on 4) |
| `Kandra_Lit` | constant 1 | constant 1 | constant 1 |
| `MeshTerrain` | **constant 0** | **constant 0** | constant 1 |
| `RealSkin`, `RealSkin_Morph` | saturate(region scale x `_SubsurfaceScale` x `_SSSMap`'s red; white where no map is set). Region scale: the region masks' channels (`_RGBAMask`; on a head also `_CFULCMask` and `_EarNeckMask`) times their own scatter scales, plus `_UnmaskedScatterScale` times what no region covers. Every scatter scale is 1 on 317 of 318 materials. `_SubsurfaceScale`: 1.0 on 188 materials, 0.27 on 60, 0.85 on 38, 0.8 on 13, 2.0 on 9, 0.2 on 5. `_SSSMap` is set on 265 | constant 1 | from `_ThicknessMap`, `_ThicknessScale`, `_ThicknessScaleMin` |
| `Snow/Snow`, `Cliff_Snow`, `DissolveWithSnow` | a value the shader computes per point (its snow amount: smoothsteps and maxima of its maps and the surface's facing) | the same value | constant 1 |
| `IceLake` | constant 1 | computed per point | the same computed value |
| `Eye/Eye` (HDRP's Eye shader, drawn forward) | constant 1 | - | - |
| `Vegetation_Leaves` (transmission only) | none | computed per point | from the material |
| `Vegetation_Grass` (transmission only) | none | constant 1 | from the material |

What the mask means: in HDRP's raster path it scales how far the screen-space blur reaches (0: no blur at all, the
surface is plain); HDRP's own path tracer uses it as the share of the diffuse light that takes the walk. Both agree
at 0 and at 1. The brief asks for the share, and that is what is built.

**The candles' meshes** (`scratchpad\whowalks\candle-meshes.txt`: the tracer's own closed-mesh rule run over the 40
candle meshes in the game's files): most candle sets are closed at their finest level (`Mesh_Prop_Set_Candles_*_LOD0`:
37 to 73 separate closed candles each; `Mesh_Set_Candles_01/02`). Others are a closed wax body plus a few small open
cards in the same submesh (`Mesh_Candle_Big_LOD0`: 480 of 506 triangles in a closed piece, one card of 26 with 4 open
edges: the alpha-tested part; `Mesh_Ext_Candles_Set_03_LOD0`: 14 closed pieces of 32, 5,724 of 10,676 triangles; the
rest are open shells of run wax). Coarser levels of detail lose a few pieces' closure (`..._LOD1`: 65 of 71 closed).
So "closed" has to be known per connected piece, not per submesh: with the glass job's per-submesh mark, one card
would switch the whole candle's walk off.
Which mesh the candle scene's wax uses is not known offline (the game places it through its own renderer, by
address). The tracer's log now says it in game: section 6.

## 2. What is built

1. **Plugin** (`HdrpPasses.ReadSubsurface`, new `KeywordOn`, `OwnMask`, `WalkLog`): a material walks only with the
   keyword on (declared by its shader and enabled on the material); its own mask goes along
   (`TGPT_Material.subsurfaceMask`). One log line a material says why it walks or does not, e.g.
   `Subsurface material "Mat_MVilL_01_Torso" (TG/Character/Kandra_Lit): names DiffusionProfile_Human_Skin but its
   subsurface feature is off (material type 1): a plain surface, as the game draws it`.
   The mask by shader: HDRP Lit `_SubsurfaceMask`; Kandra_Lit 1; MeshTerrain 0 (so: not marked); IceLake 1; RealSkin
   `_SubsurfaceScale` x `_UnmaskedScatterScale` until its program is open, then per point (below); the three snow
   shaders: not walked yet (gap 1); a shader not in the list: 1, the Shader Graph block's default, and the log says so.
2. **Per point** (`pt_common.hlsli` `SurfaceMaterial.subsurface`, set in `hit_material`): the material's number, or
   what its program computes at the hit. RealSkin's program (`pt_materials.hlsli program_skin`,
   `MaterialPrograms.cs`: texture 12 `_SSSMap`, value rows 12 to 14 the scatter scales and `_SubsurfaceScale`)
   computes the game's own formula.
   **An unbiased choice, not a blend:** a path vertex (`subsurface_vertex`) and a camera pixel (`pt_primary
   write_traced`, read by `pt_walk`) take the walk with a chance equal to the mask and are a plain surface otherwise.
   The mean is mask x walk + (1 - mask) x plain, exactly the blend's. Why not the blend: it would trace the walk at
   every vertex however small the mask (3 to 50 rays against none), for the same mean. Per ray spent the two are
   equally noisy (the choice has 1/mask times the blend's variance from the walk and costs mask times its rays).
   A mask of 0 never walks and costs one comparison.
3. **By the mesh** (`scene.cpp mark_closed`, `pt_common piece_solid`, `subsurface_hit`, `subsurface_bounds`): each
   submesh's triangles are grouped into pieces (joined through shared edges); a piece is solid where every edge of
   every triangle in it has exactly one twin running the other way. The solid pieces' triangles are put first in the
   submesh (before anything reads their order) and their count goes into the flags word's upper 24 bits.
   - A material drawn **from both sides** walks only on a solid piece, entered through its front face. A card, a
     flap or an open shell of the same material is a sheet: a plain surface, as before. Inside a walk, such a sheet
     in the way is passed (it has no inside to enter or leave).
   - A material drawn **one-sided** walks as before, whatever its mesh. Reason: the game never draws its inside, so
     it is a solid's outside by how it is drawn; and "closed" cannot be asked of it: a head is open at the neck, a
     body at the wrists, and a character's mesh comes with no rest positions to weld its seams by. Asking it would
     switch all skin's walk off.
4. **Counting** (`pt_yardstick.hlsl` counters 13 and 14, new export `TGPT_GetWalkPixels`, the plugin's and the
   tracer's yardstick lines): the pixels on a material light walks inside, and those of them that took the walk in
   the frame. In game: `...; on materials light walks inside 3.10 % of the screen, 1.20 % took the walk in this frame`.
5. **The log for the candles** (`scene.cpp`, once per such material on a mesh's submesh):
   `Subsurface material 134 (mask 1.00), drawn from both sides, on mesh 812 (key ...) submesh 0: 480 of its 506
   triangles are in pieces that close a volume: light walks inside those, the rest is a sheet (lit as a plain surface)`.

### Struct, export and number changes (for the merge)

- `TGPT_Material.opticalReserved[3]` (three uint32, zeros) becomes `float subsurfaceMask; uint32_t
  opticalReserved[2];`: same size (204 bytes) and place. C#: `NativeMaterial.SubsurfaceMask` in place of
  `OpticalReserved0`. Shader: `MaterialRecord.subsurfaceMask` in place of the first of `uint3 opticalReserved`.
  A zeroed material does not walk: every test that sets `TGPT_MATERIAL_SUBSURFACE` now sets `subsurfaceMask=1`
  (five places in `pathtracing_gpu_test.cpp`, one in `pathtracing_candle_checks.inl`).
- New export `TGPT_GetWalkPixels(uint32_t* frameIndex, uint32_t* onSubsurface, uint32_t* walked)`; the plugin binds it
  (the source checks require every export bound), so **the ABI number went up: 0x00012300 to 0x00012400** in
  `tgpt_api.h`, `NativeTracer.cs` and `tests/source_checks.py` (the number the brief names; if the water job took
  it too, the coordinator settles it).
- `GeometryRecord.flags`: bits 0 and 1 as they were (closed, own layers); **bits 8 to 31 are new: the count of the
  submesh's triangles that are in solid pieces** (`kGeometrySolidShift` 8; C++ `kGeometrySolidBits`,
  `kGeometrySolidMax`; shader `GEOMETRY_SOLID_SHIFT`). Not a new meaning for a taken word: the flags word's free
  bits. A submesh with 16.7 million triangles or more counts its first 16.7 million at most. The plant groups carry
  it with the closed bit.
- **`mark_closed` now reorders triangles** inside a submesh that has both solid and open pieces (solid first, the
  order otherwise kept). It runs before the clusters, the micromaps and the BLAS are made (the two call sites moved one
  line up). Nothing the plugin sends is per triangle.
- The yardstick's per-pixel word: bit 11 (`YARD_WALKS`) was free and now says "this pixel's own ray took the walk
  this frame". Counters 13 and 14 of `pt_yardstick` were free.
- RealSkin's program record: 13 textures (was 12), 15 value rows (was 12). A record with 12 textures is read as
  before (the material's own number is its mask).
- No option bit. No new pass. No denoiser setting touched.

## 3. The ground and snow (brief item 3)

- **The ground** (`MeshTerrain`, 194 materials, 24 with the feature on): the game's shader writes a subsurface mask
  of 0 and a transmission mask of 0 in its scattering variant. The game shows a plain surface there. Correct is: no
  walk, and that is what is built (the material is not marked; the log says "a mask of 0").
  What a walk did there until now: the terrain is an open surface over nothing, so a walk could only come back out
  through the top; by the walk's own colour conversion it returns the surface's colour, the light moved sideways by
  the snow profile's 3 to 6 cm. So the picture hardly changed, and it cost 13 to 36 rays a vertex (colour 0.7 to
  0.9; `docs/agents/speed.md` 2.2) at every pixel of ground on screen and every bounce that met the ground.
- **Snow, snowy cliffs, the snowy bridge** (`Snow/Snow` 25 materials, `Cliff_Snow` 33, `DissolveWithSnow` 1): their
  shaders compute the mask per point as the snow amount there (1 on snow, 0 on bare rock). The tracer has no program
  for these three shaders (the materials job's open list), so it cannot know the mask at a hit. Built: they do not
  walk, and the log says so by name. This is a gap, not the correct end state (gap 1).

## 3b. Energy: leaves' shares above one (brief item 4) - found, NOT changed, the owner's call

**Does the tracer make light from nothing there? Yes, at bright leaves, by a bounded amount. It cannot run away.**

- What the share is: the plugin computes HDRP's own formula for a thin sheet, the profile's transmission tint / 4 x
  (e^-St + 3 e^-St/3) x the transmission mask. At no thickness that is the tint itself. The game's
  `DiffusionProfile_Foliage` has a tint of 5.26, 5.99, 1.22: an HDR colour, an artist's dial for "leaves glow against
  the sun", not a share of anything. Logged shares (run `20261008-015158`): ivy, wild rose, carrot 5.26 5.99 1.22;
  dead branches 4.51 5.18 0.99; rose 3.70 4.29 0.77; beech 3.38 3.94 0.68; pine 1.43 1.73 0.23;
  `Foliage_Grass`'s 0.2 to 0.66.
- What the tracer does with it (`direct_diffuse`, `direct_light`, `leaf_tint`): a sheet lit from behind sends on
  colour x share of that light, and lit from the front colour x 1. So of light that falls on one side, colour x
  (1 + share) leaves the sheet. That is more than arrived where colour > 1 / (1 + share): for ivy's green above 0.14,
  for beech's above 0.20, for pine's above 0.37, for the grass profile never. Ivy's and many leaves' tints are white
  (`leaf_energy.py`: the colour is the leaf's map itself), so their brighter texels are above that. An ivy leaf with
  a green of 0.3 lit from behind is 1.8 times as bright as white paper lit from the front. The game's own shader
  shows exactly this (HDRP adds the reflected and the transmitted term, both times the colour).
- Why it cannot run away: at a path's vertex only the sun's and the lamps' light is passed through a sheet, once;
  bounced light (the radiance cache, the next ray) is only reflected there, by a colour below 1. (The camera's own
  pixel of a leaf also takes the bounced light from behind, once, at the end of the chain.) So each vertex can add
  at most colour x share once, and every bounce after loses light as it should.
- Where it shows: the forest floor and trunks under sunlit leaves take the made-up light as bounced light, and
  backlit leaves are brighter than physics allows. Both as in the game, where the first is absent (the game has no
  bounce) and the second is its look.
- The same holds for the grey half (`TRANSMISSION` 0.5, where a material says nothing: colour above 0.67) and thin
  cloth's 0.3 (colour above 0.77), and a leaf's shadow lets half the light pass while the leaf takes all of it (the
  owner's ruling on soft grass shadows): all on the shortcuts list already (PT-51).
- The fix at the cause is not a number: it is the thin sheet as a traced thing, what a walk through a slab of the
  material's own thickness and colour reflects and passes (never more than arrived, by construction), the same job
  the request list names for cloth ("a thin scattering sheet"). It drops the HDR tint, so backlit foliage gets
  dimmer than the game's wherever the game's own numbers break the bound.
- A smaller step that I did NOT apply: hold what a sheet passes to what its colour leaves, per point (transmitted =
  min(colour x share, 1 - colour); two lines, `hit_material` and `leaf_transmittance`). It changes nothing for dark
  leaves (colour x (1 + share) at most 1) and dims the bright texels' glow (ivy at 0.3: from 1.8 to 0.7 of white
  paper). Not applied because: (a) the shares were put in on 2026-10-07 to bring the trees' colour to the game's
  (`pt_common.hlsli` at `leaf_transmittance`: saturation 0.10 with the grey half, 0.16 in the game), a look measured
  in game, and this takes some of it back; (b) the owner's rule is no cap that cuts light, and whether "more light
  than arrived" is such a cap or a defect is theirs to say; (c) three checks of the GPU test assert the unbounded
  share on a sheet of colour 0.75 (lines 8838, 9346, 9377 of `pathtracing_gpu_test.cpp`).
  **Question for the owner, in plain words: leaves lit from behind glow up to 6 times brighter than they are lit
  from the front, because the game's artists set it so. A path tracer then makes light that was never there (bounded,
  as above). Keep the game's glow, or hold leaves to what is physically possible (dimmer backlit leaves)?**

## 3c. The slab 110 m from the origin that loses 4.5 % of its blue (brief item 5) - not done, cause narrowed

Left as the brief allows. What I can add to the candle job's notes, by reasoning, not by a test:
- At 110 m a float is 7.6 micrometres apart. A walk keeps its place relative to its entry exactly (`local`), but
  each flight's ray starts at `entry.position + local` rounded to that grid: up to 3.8 micrometres off per axis.
- A scatter point closer to the surface than that can land on the wrong side. Its next flight outward then starts
  outside, meets nothing, and the walk goes on in the air as if inside until roulette ends it: its light is lost.
- Blue scatters every 0.26 mm in skin, red every 0.43 mm: blue has more scatter points close under the surface per
  walk, so it loses most (0.975, 0.967, 0.955: red, green, blue). The size fits: a few micrometres against a
  quarter millimetre is a per cent or two a scatter near the surface.
- The candle job's exact ray distance could not help: the origin is what is rounded.
- What would fix it: rays that start in numbers small enough (the scene's origin kept at the camera, or the walk's
  rays traced in the object's own space). Not a tint, an offset or a looser tolerance. At 110 m skin is a few
  pixels, so in game the loss is small where it is large and nothing where it matters (close to the camera a float
  is a quarter micrometre apart). The check stays failing, honestly.

## 4. What the choice does to the red dots (the coordinator's question)

The dots on the hero's face and chest with no denoiser (`artifacts\harness\20261008-034922\hair-moving\crop-hero.jpg`,
middle panel) are single walk samples: one walk a pixel a frame, each coloured by how far it went (blue is absorbed
first, so a long walk is red and dark, a short one pale).
What this job changes there:
- **Where the mask is below 1, fewer pixels show a walk sample in a frame**: the mask's share of them. The rest show
  the smooth plain-surface light that frame. 130 of the game's 318 skin materials have a scale below 1 (60 at 0.27),
  and the subsurface map lowers it further per point.
- The mean does not move. Where the plain surface and the walk agree in the mean (skin lit from the front: its
  scattering is tenths of a millimetre), the spread of a pixel falls about with the mask. Where they differ (an ear
  lit from behind: plain is black) a pixel flickers between the two, and the spread rises; the offline test prints
  both cases (section 5).
- **Where the mask is 1 nothing changes**: the dots are as before. This job does not touch how a walk is weighted,
  only whether it is taken. Making the walk itself less noisy (reusing exits between pixels and frames) is the
  other job named in `pt_walk.hlsl`'s header.
- Clothes and armour that carried the skin's profile no longer walk at all: their share of the dots is gone.

## 5. Tests

Built: native (NRD off, `%TEMP%\tgb\walks-build`, DLL SHA-256 51C6F0DF...4CF38A) and the plugin (`dotnet build`, 0
warnings). Source checks 95 of 95, with this job's lines added (`test_path_tracing_...` at the walk's block).

### The new checks (`tests/pathtracing_walk_checks.inl`, alone with `TGPT_TEST_WALKS_ONLY=1`; 1280x720, no denoiser, no reuse, no cache)

Run `walks-out-checks1`, 04:36 to 04:41, exit 0, **all pass: 84 of 84 expectations, 19 of them the named checks** (one other offline test ran beside it, so the
stage times are from a shared GPU). The passes took 232 s to make.

| check | measured |
|---|---|
| a wax cylinder lit from behind, its near side (black as a plain surface): mask 1 / 0.5 / 0 | 0.23725 / 0.11853 (0.500 of it) / 0.000000 |
| pixels on the material / pixels that walked in a frame: mask 1, 0.5, 0 | 40,596 / 40,596; 40,596 / 20,295 (0.500); 40,596 / 0 |
| the same closed cylinder, its material drawn from both sides | 1.012 of the one-sided one's light; 40,596 / 40,596 walk |
| a closed cylinder with an open card in the same submesh (the card's triangles first in the mesh) | the cylinder 0.994 of it; 52,764 pixels on the material, 40,596 walk (the card's 12,168 do not) |
| an open tube drawn from both sides, lit from the front | 1.0000 of the same with the walk switched off; 40,596 / 0 walk |
| a metal plate naming the skin's profile with a mask of 0, against the same plate without | the same picture to five digits (0.13745 0.18750 0.23743); 170,736 pixels on it, 0 walk |
| skin's mask per point: the share of a panel's pixels that walk, against the game's formula | its map alone 0.5016 (0.5020); a body's regions and scale 0.4518 (0.4520); a head's 0.3776 (0.3779) |

The grain with no denoiser (one pixel of one frame against the mean: standard deviation over the mean):

| | mean light (r, g, b) | one pixel's spread |
|---|---|---|
| skin lit from the front, mask 1 | 1.0852 1.0830 1.0807 | **1.09** |
| the same, mask 0.27 (60 of the game's skin materials have that scale) | 1.0864 1.0868 1.0870 | **0.57** |
| the same, mask 0 (a plain surface) | 1.0851 1.0851 1.0851 | 0.001 |
| the wax cylinder's near side, lit from behind (plain is black), mask 1 | | 3.30 |
| the same, mask 0.5 | | 4.78 |

So on skin lit from the front the mean is the same at every mask (within 0.4 %), and the grain falls with the square
root of the mask. Where the plain surface is black (light only through the thing) the grain rises as the mask falls
(3.30 to 4.78 at 0.5: the two-state flicker), with the right mean.

### What it costs (the same run; a shared GPU, so read the differences, not the totals)

A plate over the whole 1280x720 view, a slab of the same material over it (what bounced light and reflections meet),
one lamp. GPU time by stage, ms:

| the material | bounced light | direct light (the camera's walk is in it) | reflections | all stages |
|---|---|---|---|---|
| a plain metal | 19.78 | 2.08 | 1.79 | 30.2 |
| the metal naming the skin's profile, mask 0 (**now**: no walk) | 20.81 | 2.37 | 1.75 | 31.1 |
| the same, walked (**the rule before this job**: a profile named was enough) | 22.56 | 2.15 | 8.46 | 38.9 |
| wax, mask 1 | 21.64 | 19.87 | 4.61 | 52.9 |
| wax, mask 0.5 | 21.80 | 15.92 | 3.90 | 47.9 |
| wax, mask 0 | 21.48 | 2.01 | 1.45 | 30.9 |
| wax, mask 1, the walk switched off | 21.44 | 2.41 | 1.94 | 32.1 |

- Armour that named the skin's profile: 38.9 ms before, 31.1 ms now on this scene (a metal has no diffuse colour, so
  its camera pixels' walk died at once; the cost was at what reflections and bounced light met).
- A material whose camera pixels walk costs 18 ms more over a whole 720p view (wax, mask 1).
- **A mask of 0.5 saves less than half of that** (15.9 against 19.9 ms: the walk's extra falls by 22 %, not 50 %).
  The GPU runs a pass's pixels in groups that wait for their slowest, and the choice is made pixel by pixel, so most
  groups still hold a walker. The rays are saved, the waiting is not. Making the walking pixels a list of their own
  (so groups are all walkers or none) is the speed step after this; it changes no picture. Not built.

### The whole GPU test, once

(running: `walks-out-whole1`)

The base's own failing checks (the coordinator's whole run of base50, 04:08 to 04:23, 1,513 ok, 5 FAIL), not this job's:
1. "coloured or white bulk slab retains its diffuse colour under even white light within 3.5 percent" (only its case
   "skin 110 m from scene origin": brief item 5).
2. "glass bends light by its material's own index of refraction (another index lays the light elsewhere)".
3. "light through green glass reaches path vertices green (bounced light)".
4. "grey glass leaves the bounced light's color".
5. "grey glass lets light through as bright as its color (its shadow about 0.64 of a solid one's)".

## 6. For the in-game run (`candle@quick`, `hair-moving@quick`, `glass-jars@quick`)

Nothing of this is seen in game yet. The merged build needs the ABI number settled (section 2) and, since my build
has NRD off, the coordinator's own NRD build and GPU test before installing.

**First, the one thing only the game can show: that the keyword test works on the game's materials.** In
`BepInEx/LogOutput.log` the skin lines must read `Subsurface material "Mat_..._Head..." (TG/Character/RealSkin): light
walks inside it (DiffusionProfile_Human_Skin, scattering 0.3, 0.16, 0.09 mm), as often as its mask says: ...`. If
every material says "its subsurface feature is off", `KeywordOn` reads the game's keywords wrongly: nothing walks,
skin is plain; tell me (or fall back to `m.IsKeywordEnabled(name)`, which the function already does on an exception).

`candle@quick`:
- Plugin log: `Subsurface material "Mat_Prop_Candles_02" (HDRP/Lit): light walks inside it (..., scattering ... mm), as
  often as its mask says: 1 (its _SubsurfaceMask); drawn from both sides: only where its mesh closes a volume`.
- Tracer's log (`TGPathTracing.Native.log`), once for each mesh that has that material: `Subsurface material <n> (mask
  1.00), drawn from both sides, on mesh <m> (key <k>) submesh <s>: A of B triangles are in pieces that close a
  volume: ...`. **This is the answer to "are the scene's candles closed".** A = B or most of B: the wax walks. A = 0:
  those candles are open shells and stay plain (then the picture does not change, and what is left is the thin-sheet
  job of section 3b, or welding at a coarser tolerance: say which mesh).
- Picture, where it walks: the wax near the flame glows from inside, its side away from the flame is no longer flat
  dark brown, folds and the rim are lighter. Its colour is still the candle light's on cream wax (orange 0.70, 0.25,
  0.05). **A cream-white candle as in the game is not promised:** the game lights its wax with white fill lights the
  tracer leaves out by the owner's ruling, and the yardstick already says the wax's own colour is read right.
- With no denoiser the wax pixels are grainy (one walk a pixel: a spread of 3 times the mean where only the walk
  lights them). That is the walk's own noise, for NRD to take.

`hair-moving@quick` (the hero in the tavern):
- Plugin log: the 15 `TG/Character/Kandra_Lit` materials and the bows that walked now say `names
  DiffusionProfile_Human_Skin but its subsurface feature is off (material type 1): a plain surface, as the game draws it`.
- The yardstick's line (plugin and tracer) ends with `on materials light walks inside X % of the screen, Y % took the
  walk in this frame`, and the tracer's adds `(N of the scene's materials)`. Before: 40 and more materials (30 with
  the skin's profile). Expect: the skin materials of the people in view, plus wax.
- Armour and clothes: no red dots, no walk at what a reflection or a bounce meets. If the armour was dull because
  its reflections showed walked skin-coloured light, it is not any more. (I could not see this offline.)
- The face and chest: the dots thin out by the mask's share where the game's mask is below 1 (the grain by its
  square root: 1.09 to 0.57 at a mask of 0.27), with the same mean colour. Where the mask is 1 they stay.
- Stage times: `docs/agents/speed.md` has the tavern at 62 ms with the walk everywhere, 30 ms before it. Expect
  most of that back: what is left is skin's and wax's own walk.

`glass-jars@quick`: nothing of this job should show (no scattering material by the jars that I know of). Use it as
the "nothing else moved" scene; its stage times fall only if clothes or the ground were in view.

Outdoors (`forest`, `vista`): the ground and snow no longer walk (section 3): the same picture, less time in
"bounced light" and "direct light".

## 7. Gaps and things for the coordinator's lists

1. **Snow shaders' mask per point is not read**: `Snow/Snow` (25 materials), `Cliff_Snow` (33), `DissolveWithSnow`
   (1) do not walk; they need their programs (the materials job). Logged by name as "NOT walked yet".
2. **The eye's shader scatters** (HDRP's Eye shader, mask 1, built into the shader); the tracer's eye does not walk,
   and never did. The eyes job's.
3. **A material drawn from both sides on a character** (48 of the game's skin materials: ogres, dryads, skeleton
   heads, fins) never counts as solid: a character's mesh comes without rest positions to weld its seams by. Plain,
   as before. Needs the rest pose's positions at registration.
4. **Open sheets whose material has the game's transmission feature but is not a leaf** (stained glass windows drawn
   as subsurface, wax run over a table as an open shell, two-sided monster skin) get no light from behind. The game
   passes light through them by the profile at the material's thickness. Section 3b's thin-sheet job covers them.
5. `RealSkin_Morph` (4 materials) has no program: its map and regions are not read, its scale is.
6. A material whose feature is on but names no loaded profile (HDRP's default profile would serve it): not walked.
   Logged. (3 skin materials and some particle splatters in the game's files.)
7. HDRP Lit's subsurface mask map is not read (no scattering material of the game has one; the log says so if one
   turns up).
8. A closed body with cut-out texels (alpha test on the body itself, not on a card): a walk's flight can leave
   through a hole and is then lost. Small, at the holes' rims; not measured.
9. **Leaves make light from nothing where colour x (1 + share) > 1** (section 3b): the owner's call.
10. **The slab 110 m from the origin** (section 3c): still fails, cause narrowed, needs rays in small numbers.
11. **A mask of 0.5 saves a fifth of the walk's time, not half** (section 5): the walking pixels as a list of their
    own would make it proportional. A speed job, no picture change.
12. The walk's own grain (a pixel's spread of 1.09 times the mean on skin lit from the front) is unchanged where
    the mask is 1. Reusing exits between pixels and frames is the job `pt_walk.hlsl` names.
13. `IceLake` (3 materials, mask 1, a flat sheet drawn one-sided) walks as before: over a lake bed it behaves as a
    slab. Not looked at in game by anyone; its profile's distances decide what it costs.

## 8. Workarounds met

| workaround | what was done |
|---|---|
| "names a profile, so it scatters" (the plugin's guess in place of the game's own switch) | removed: the keyword and the mask |
| "drawn from both sides, so a sheet" (the plugin's `noInside` and the shader's `MATERIAL_NO_WALK`) | removed: the mesh's own pieces say |
| the log line "Subsurface candidate ... mesh closure not established" (the candle job's stop-gap) | removed with the rule |
| HDRP Lit alone checked by `_MaterialID == 0` | replaced by the keyword that number sets, for every shader |
| the leaf log stopped after 24 lines | removed: once a material, and it says when a share is above 1 |
| a profile counts as thick where its name holds "skin" (`HdrpPasses.IsThick`, sent as `thickProfiles`) | met, not touched: it is read on the route before the tracer's own rays; whether anything still needs it I did not check |
| `TRANSMISSION` 0.5 and `CLOTH_TRANSMISSION` 0.3, a leaf's shadow passing half the light | met, not touched (PT-51's list; section 3b) |
| HDRP's thin-sheet formula with an HDR tint as a leaf's "share" | met, not touched: section 3b, the owner's call |

## 9. Not done by this job, on purpose

- `CHANGELOG.md`, `README.md`, `docs/TESTING.md`: not edited (many jobs append there; the coordinator has the text
  in sections "In short" and 2). `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`: not mine.
- No mutation checks (the brief). No denoiser setting. Nothing deployed, nothing committed, the game never started.
- The untouched base was built (`%TEMP%\tgb\walks-base-build`, the DLL only) for a before-and-after of the times. It
  was not used: with the ABI number raised the new test cannot drive it. The "before" in section 5 is the same
  build with the mask at 1 on the metal, which is what the old rule did to it.
