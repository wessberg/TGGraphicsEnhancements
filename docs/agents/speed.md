# Speed: what made the tracer slower on 2026-10-07, and what was taken out (PT-73 c, PT-08)

State: MEASURING (the changes are built and in the copy; sections 3 and 7 take their last numbers when the queue ends).
Copy: `scratchpad\wt\speed` (base `wt\base45`, ABI 0x00012100, unchanged). Builds: `%TEMP%\tgb\speed-*`. Scripts and raw
outputs: `scratchpad\speed\` (`out\` has every run's text).

## The short version

- The slow-down is in two passes that shade what a ray meets: bounced light (`pt_gi`) and reflections (`pt_specular`),
  and a little in the camera's own rays. Nothing else grew: not the scene's stage (moved things, plants), not direct
  light, not the denoisers.
- Three things were added to a ray's hit today. (1) Texture reads: a material was read twice a hit, HDRP Lit's maps were
  read and thrown away under every program, and the programs read maps they then multiply by zero. (2) The subsurface walk,
  at every bounce and reflection hit on a "subsurface material", which is far more than skin and wax: clothes, armour,
  bows, the ground and snow cliffs. (3) Glass with no depth limit, each interface asking every triangle of the scene.
- Taken out, with the same picture: all of (1), and the every-triangle part of (3). Not touched: the walk (2) and glass's
  depth, because changing them changes the picture or its noise. They are the larger part of what is left, and they are
  the owner's and the subsurface worker's to decide (section 4).
- The brief's "12 ms" for the tavern at 16:59 is a stale line. The tavern went from 30 ms to 62 to 66 ms: two times.

## 1. What the game's logs say, by stage (the three runs of the brief)

Source: the "Path tracing 10s" lines of each run's `TGPathTracing.Native.log` (`scratchpad\speed\stages.py <log>` prints
them as a table), 1924x1084, 1 ray a pixel, RTX 5080, the GPU to itself. Each cell is the range of that scene's lines.

The tavern was not at 12 ms at 16:59. That run's status lines say 28 to 31 ms (`LogOutput.log` lines 427, 443, 506;
native lines 4709 to 4714). The 10 to 12 ms figure is an older session's line still in the same log file (line 2002).

Tavern (`hair-moving`, 4580 instances, 1.55 M triangles in all three runs), ms:

| Stage (what runs in it) | 16:59 | 18:59 | 22:46 | Change |
|---|---|---|---|---|
| scene | 0.6 | 0.65 | 0.6 | - |
| prepare (pt_wet, pt_prepare: the camera's own rays, pt_probe) | 0.96 | 0.97 | 1.2 to 1.5 | +0.4 |
| bounced light (pt_gi: one ray a pixel and its path) | 11.0 | 10.9 | 38 to 44 | **+30** |
| bounced light spatial reuse | 1.9 | 2.0 | 1.2 to 1.3 | -0.7 |
| direct light (pt_di, pt_di_spatial, pt_walk) | 5.2 | 5.1 | 3.7 to 5.0 | -0.5 |
| hair and eyes (gone at 22:46: in the camera's rays now) | 0.16 | 0.15 | - | -0.15 |
| reflections (pt_specular) | 5.7 | 5.5 | 9.5 to 11.1 | **+4.5** |
| radiance cache, denoising | 0.04 | 0.05 | 0.05 | - |
| caustics | 3.7 | 4.5 | 4.2 to 4.6 | - (but 0 triangles made: section 4) |
| composite, water reflections | 0.8 | 0.7 | 0.8 | - |
| whole frame | 29.7 | 30.2 | 62 to 66 | **+33** |

Forest (6782 instances; 17:15 had the scene before "the whole world", 8228 instances), ms:

| Stage | 17:15 | 18:59 | 22:46 | Change 18:59 to 22:46 |
|---|---|---|---|---|
| scene | 0.7 | 0.8 | 0.7 | - |
| prepare | 4.3 to 5.2 | 5.5 to 6.0 | 8.0 to 8.8 | **+2.7** |
| bounced light | 23.5 to 25.9 | 26.1 to 30.7 | 37.2 to 41.9 | **+11** |
| bounced light spatial reuse | 2.0 to 2.5 | 2.6 to 2.8 | 2.5 to 3.0 | - |
| direct light | 6.1 to 9.6 | 7.5 to 9.9 | 8.7 to 11.5 | +1.5 |
| reflections | 15.6 to 17.9 | 16.8 to 17.8 | 21.9 to 24.0 | **+4.5** |
| caustics | 8.6 to 9.5 | 9.4 to 10.3 | 9.0 to 9.3 | - |
| whole frame | 65 to 70 | 71 to 77 | 90 to 96 | **+19** |

Vista (8002 instances), ms:

| Stage | 18:59 | 22:46 | Change |
|---|---|---|---|
| scene | 0.75 | 0.7 | - |
| prepare | 2.4 to 2.5 | 4.1 to 4.2 | **+1.7** |
| bounced light | 9.5 to 9.9 | 18.2 to 18.6 | **+8.7** |
| direct light | 5.7 to 5.9 | 5.4 to 5.7 | - |
| reflections | 5.0 to 5.6 | 6.9 to 7.1 | **+1.5** |
| caustics | 8.2 to 8.3 | 7.7 to 8.1 | - |
| whole frame | 34 to 36 | 46 to 48 | **+12** |

The shaders are the same files at 17:03 and 18:46 (`wt\base35`, `wt\base39`): all the shader growth is between 18:46
and 23:44. The compiled passes grew with it (DXIL of the DXR 1.2 builds, KB): `pt_gi` 206 to 614, `pt_specular` 542 to
1959, `pt_prepare` 261 to 696, all passes 1984 to 5240. Making them took 143 to 164 s on a busy machine (`tgpt_gpu_test`'s
"passes made in"), which is the start-up worker's subject.

## 2. Why (the code)

Everything that grew is `hit_material` and what its callers do at a hit (`pt_common.hlsli`). Three additions:

### 2.1 Texture reads (taken out: section 3)

1. **A path vertex's material was read twice.** The glass work hands `dielectric_follow` (pt_dielectric.hlsli) the material
   of the surface met: `dielectric_follow(hit, dir, hit_material(hit), ...)`, and the caller then read it again:
   `m = hit_material(hit)`. Only glass uses the first, and only glass moves the hit. The compiler dropped the first one's
   color reads (the glass code uses its normal and roughness alone) and kept the rest: for every surface that is not
   glass, the normal map, the mask map and all of its program's maps but the colors were read a second time. In
   `shade_path` (every bounce), `cache_sample` (the radiance cache's ray) and `shade_seen` (what a reflection shows).
2. **Under a program, HDRP Lit's reading was made first and thrown away.** `hit_material` read the color, normal and
   mask maps, then ran the game's own shader (pt_materials.hlsli), which reads the same three again plus its own and
   replaces all of the first reading.
3. **The programs read maps whose result is multiplied by zero.** The ground (`program_terrain`) read all eight layers'
   color, normal and mask maps though the splat maps give most layers a share of exactly 0 at any one point.
   `program_triplanar` read every map from three sides though a wall's normal leaves one or two sides a weight of exactly 0.
   `program_architecture` read its overlay's color with the overlay off, and its mask noise (three reads) with it off.

Texture reads at one path vertex, counted from the code (a material with its own color, normal and mask map):

| Material at the hit | 18:46 | 23:44 | now |
|---|---|---|---|
| HDRP Lit | 3 | 5 | 3 |
| HDRP Lit with a detail map | 3 | 11 | 6 (two still read twice: section 4) |
| KF_Architecture (the tavern's wood) | 3 | 23 | 10; 6 with its overlay and mask noise off |
| MeshTerrain (the ground, 8 layers) | 3 | 49 | 8 with two layers painted, 11 with three |
| TriplanarArchitecture2 | 3 | 47 | 8 on a wall along the world's axes, 16 on a turned one, 24 on a slope |
| Cliff | 3 | 21 | 8 |
| Trunk | 3 | 17 | 7 |
| Leaves, grass | 3 | 9, 7 | 6, 4 |

(18:46 is not "right": every material was read as HDRP Lit then, which is why rock, ground and furniture looked wrong.
"Now" is what the game's own shader needs.) Every read is `SampleGrad` over the ray cone's footprint: bounce rays read
coarse mips, not mip 0. The one exception is emission (`material_emission` reads the emissive map at its top mip), on
glowing materials only.

### 2.2 The subsurface walk (not changed: it changes the picture)

`subsurface_vertex` runs at every path vertex, at what the cache's ray meets and at what a reflection shows, and
`pt_walk` at every camera pixel, where the material is a "subsurface material". Elsewhere it costs one flag test. But
the plugin marks a material by its diffusion profile alone (`HdrpPasses.cs` `ReadSubsurface`, lines 839 to 845): any
material of the game's own shaders that names a profile, whatever its subsurface mask says. In run `20261007-224654`
the log names the first 40 (`LogOutput.log` lines 352 to 479; the plugin stops logging at 40):

- 30 with `DiffusionProfile_Human_Skin` (scattering 0.3, 0.16, 0.09 mm). Of those 15 are `TG/Character/Kandra_Lit`,
  which is what clothes and armour are made with: `Mat_ChMgenM_07_Torso_AgedMetalVest_02`, `Mat_MVilL_01_Torso`,
  `..._Legs`, `..._Feet`, and a bow, `Mat_Mesh_Prop_Bow_01`. `Mat_Weapon_Long_Bow_01` has the zombie skin profile.
- The ground and snow cliffs with `DiffusionProfile_Snow` (27.7, 30.6, 57.8 mm): three `TG/Environment/MeshTerrain`
  materials of the forest's region (`Mat_UT_C_02_d_CampaignMap_HOS`, `..._b_`, `Mat_UT_B_02_a_`) and `Mat_Cliff_03_Snow`.
  `docs/agents/materials.md` counts 24 terrain materials of the subsurface variant.

What a walk costs: each flight is one ray (`trace_closest`: a full query, the alpha test of what it crosses, the whole
hit record). Counted with `scratchpad\speed\walk_steps.py` (the walk's own rules in a thing thicker than its free path):

| Surface color | rays a walk, mean | 90th percentile | 99th | most of 200 000 |
|---|---|---|---|---|
| 0.3 | 3.2 | 6 | 13 | 50 |
| 0.5 | 6.2 | 14 | 34 | 116 |
| 0.7 | 13.5 | 35 | 108 | 415 |
| 0.8 | 21.4 | 56 | 217 | 898 |
| 0.9 | 36.3 | 88 | 485 | 2655 |
| 0.95 | 49.5 | 109 | 760 | 4096 (the loop's bound) |

A path that is not a walker takes about four rays. And the GPU runs rays in groups of 32 that wait for their slowest:
one walker in a group holds the other 31 for its whole walk (in game the passes sort by material after the first hit,
Shader Execution Reordering, which keeps walkers together there; the cache's ray and later bounces are not sorted).
A camera pixel on such a material pays twice: `pt_gi` and `pt_di` do their whole work for it, then `pt_walk` walks,
lights the exit, traces a bounce ray and shades its path, and writes over both signals.

### 2.3 Glass (the every-triangle part taken out: section 3; the depth not changed)

- Every radiance ray that meets glass now follows it (`dielectric_follow`), with no count of interfaces: Russian roulette
  at 0.95 a crossing, so light trapped by total internal reflection (a thick or faceted piece) takes about 20 crossings
  on average. That is the design ("roulette changes variance, never the expected light").
- Each crossing asks which medium lies outside (`optical_medium`): a ray in a fixed direction to infinity, with
  `RAY_FLAG_FORCE_NON_OPAQUE`, so that **every triangle it crosses in the whole scene comes to the shader** (two or three
  buffer reads each, to pass it by), and a second query for each back-facing glass triangle found. Then the next ray
  (`trace_closest`), the lamps as spheres (`reflected_lamps`: every light of the cell's lists), and the next material.
  For a point in air, which is most of them, nothing ends the ray early.
- The material where the walk ends is read inside `dielectric_follow` and again by the caller (glass only).

### 2.4 What is not the cause

- The scene's stage: 0.6 to 0.8 ms before and after in all three scenes. Moved things: 161 placed a frame in the tavern,
  0.61 ms of the plugin's thread (once 18 ms). Plants: 6453 in 287 groups in the forest, 0 groups made a frame. No
  rebuild shows in the stage.
- `pt_walk` as a pass: the direct light stage did not grow in the tavern or the vista, +1.5 ms in the forest.
- The camera ray's walk (`primary_walk` asks every triangle on the ray, as at 18:59): prepare grew by what the pixel's
  own material read costs (one `hit_material` a pixel, now a program's reads), +0.4, +2.7 and +1.7 ms.
- Direct light, the spatial passes, the denoisers, the composite: flat.

How much of the tavern's +30 ms is each of 2.1, 2.2 and 2.3 cannot be told from the logs, and offline the passes run
without Shader Execution Reordering. Section 4 says how one game run can tell.

## 3. What was taken out, with numbers

(filled in when the queue ends)

## 4. What is left

(filled in when the queue ends)

## 5. Lines changed in shared files

(filled in when the queue ends)

## 6. Workarounds met

(filled in when the queue ends)

## 7. Tests

(filled in when the queue ends)
