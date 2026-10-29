# World agent: the traced scene is the world the game draws (PT-63, PT-61)

Copy: `<scratch>/wt/world` (base `wt/base34`). 2026-10-07. Nothing here was run in the game: every "in game" line is
what the coordinator's run must show (section 6). `<scratch>/world-work/` holds the scripts, the decompiled game code and
the logs named below.

## 0. State

| Part | State | Proof |
|---|---|---|
| 1 Objects left out | built: static batches, stray Drake entities, trees past their last mesh; the rest named with numbers | rules tests (15 mutants killed), source checks, plugin compiles; **not seen in game** |
| 2 The drawn level of detail | built, for every capture path | the same; **not seen in game** |
| 4 Grass, leaves and trunks sway | built: the game's maths read from its compiled shaders, every such instance posed every frame | GPU checks 14 and 15 pass, the whole GPU test passes (section 7); 2 of 5 GPU mutants run, both killed; one last run and 3 mutants wait for a free GPU (section 7); **not seen in game** |
| 3 No distance cut | built on the plugin's side at the coordinator's word of 17:55: the Scene Radius and Ray Distance rows and every cut by them are deleted; numbers in section 3 | plugin compiles, source checks; **not seen in game, its cost not measured** |
| 5 Decals seen by rays | design (section 5); the plugin now counts decals a scan | none |

What the coordinator should know first:

1. **The ABI moved to 0x00011F00** in this copy (main is 0x00011E00): two new exports, `TGPT_SetPlantWind` and
   `TGPT_SetWindNoise`, and two new structs, `TGPT_PlantWind` (48 B) and `TGPT_PlantWindFrame` (32 B). No struct that
   existed changed. No option bit, no material bit (33554432 was not needed), no instance flag, no FrameConstants field.
2. **Round 2's rule "within 30 m the finest level whatever the game draws" is gone** (`NearDetail`). It is the opposite of
   PT-61. What comes back with it: a traced object's shadow changes when the game switches its level, because the drawn
   object changes then too. "Keeps a level it has while the one it wants registers" stays.
3. **A third vegetation shader sways: `TG/Vegetation/Vegetation_Trunk`.** Trunks were traced still and trusted while HDRP
   bent them (up to 6 degrees at the game's defaults). They are posed now with the leaves.
4. **A real mistake was found in the Drake capture**, apart from the level of detail: an entity with a visible range was
   traced by the plain distance to its own position, where the game loads it by the scaled distance to its LOD reference
   point (scaled by 1.155 at a 60 degree view and Draw Distance 1) and multiplies the far end by the Draw Distance row's
   extra distance (up to 1.8). So things were traced 15 % farther than drawn, and at the row's top not as far as drawn.
   The capture now reads the game's own state (the entity is loaded for drawing or it is not).
5. **The Scene Radius and Ray Distance rows are gone** (the owner's rule of 17:55: what the work makes dead is deleted,
   not switched off). Every capture path traces what the game draws however far off; rays have no end the player
   sets. What this costs in the game is not measured: section 3 says what to read off the first run.
6. **Plants are no longer flagged as things rays distrust** (`SceneCapture.Sways` names cloth alone). The distrust itself
   (`MASK_SWAYS` and the rest) still serves cloth past its 4,096 copies and is not deleted: section 10 says where it lives.
7. Two files are new on the managed side (`WorldRules.cs`, `PlantWind.cs`); `HdrpPasses.cs` and the camera-ray agent's
   shaders are untouched. `pt_common.hlsli` is untouched.

## 1. Objects left out

Read from `SceneCapture.cs`, `LeshyCapture.cs`, `CharacterCapture.cs`, `scene.cpp` and the game's own code (decompiled
with `ilspycmd` into `<scratch>/world-work/*.cs`). Counts from the harness logs of 2026-10-07 (`artifacts/harness/20261007-124601`,
`-043153`): tavern 1,875 instances (objects 0, Drake 1,874), cave lake 1,125 to 1,722 (Drake 500, Medusa 231, vegetation
391 to 988), vista 13,066 (objects 22, Drake 988, entities 600, Medusa 781, vegetation 10,671).

| # | What stayed out | Why | Now |
|---|---|---|---|
| 1 | Statically batched renderers | each member's mesh is the batch's whole combined mesh, so adding it per member would trace the batch once a member; all were skipped | **Traced**: one instance a combined mesh, each drawn member's materials on its own run of submeshes (`Renderer.subMeshStartIndex`), the submeshes no drawn member names invisible to rays (`WorldRules.PlaceBatchMember`). No harness log has the old "N statically batched" line: the scenes tested had none in reach, so this is a rare path in this game and stays unseen until a scene has one. |
| 2 | Drake entities with a LOD range and no reference point | they matched none of the three Drake queries | **Traced** (a fourth query): Entities Graphics chooses levels only in chunks that have both, so it draws these always. Counted in the status line. |
| 3 | Trees past their last mesh level (Leshy's billboards) | a card that turns to the camera is no geometry a ray from elsewhere can meet | **Traced as the tree's coarsest mesh** where the game draws the card (`LeshyCapture`, `WorldRules.CoarsestMeshLevel`). A choice to confirm: the game shows a card there, the tracer a tree. Counted ("N trees past their last mesh"). |
| 4 | Nothing, but the reverse: an object beyond its last LOD (GameObject LOD groups) was traced at its coarsest level "for light" though the game draws nothing | old choice | **Traced only where drawn** (part 2). |
| 5 | Drake entities the game has unloaded were traced when their mesh happened to be in memory, and loaded ones past the plain distance were not | the visible range was worked out again, wrongly (section 0, point 4) | **The game's own state is read** (`MaterialMeshInfo` on the chunk, no `DisableRendering`). Counted ("N Drake entities the game has not loaded for drawing"). |
| 6 | Meshes the tracer cannot read (formats, streams, no positions), submeshes that are not triangle lists | `RegisterMesh` returns -1 to -4; `scene.cpp` takes topology 0 only | not changed. The status line counts them ("failed N"); the harness logs say 0. |
| 7 | Everything while it registers | 6 meshes a frame handed over; 48 BLAS and 3,000,000 triangles a frame built | a delay, not a hole (841 meshes: about 140 frames). Logged ("N meshes to register"). |
| 8 | Past 2.5 GB of traced geometry the scene starts over (`Session.cs` `GeometryBudget`) | meshes stay registered while the camera travels; the guard drops them all and the next scan registers what is drawn again | not changed. Without a radius what comes back is everything drawn, so a scene that needs more than 2.5 GB at once would start over again and again: the first run's "MB" in the status line says how far off that is (65 to 180 MB in the harness scenes at 250 m). |
| 8b | Anything past 250 m of the camera (`Scene radius`), and a ray's way past 500 m (`Ray distance`) | two rows | **Deleted**, rows and cuts (part 3). |
| 9 | Particles, fire, smoke, magic | drawn by the game over the picture | PT-67. |
| 10 | See-through surfaces other than glass and water | no traced shading for them | PT-64. |
| 11 | Decals | only in HDRP's G-buffer | part 5 (design). |
| 12 | Unity terrain as a 257 x 257 grid, without its detail meshes and trees | built once from the heightmap | not changed: the harness scenes have none ("terrain 0"). |
| 13 | Characters past 256 renderers or 2,097,152 skinned vertices a frame (the nearest first) | native buffers | not changed; the radius is gone. The counts are past what the scenes show (82 renderers at the vista at 250 m; more now). |
| 14 | Swaying cloth past 4,096 copies or 16,777,216 vertices | the limits job's caps | not changed (240 cloths in the busiest scene). The plants' groups (part 4) have no such cap; cloth could move to them. |
| 15 | LOD cross-fade (two levels dithered for a moment) | the tracer takes one level | named. Drake and Medusa do not fade. |
| 16 | Other vegetation shaders that sway | only three were read | the plugin names any other `TG/Vegetation/` shader once in the log ("not one whose wind the tracer has read"). `resources.assets` has only Grass, Leaves, Trunk and Billboard. |
| 17 | Layers the camera's culling mask leaves out, `forceRenderingOff`, disabled renderers, shadow-only meshes | the game does not draw them | right as it is. |

## 2. The same level of detail the game draws (PT-61)

What the game does, read from its code:

- **Entities Graphics and Drake** (`Unity.Rendering.SelectLodEnabled`): a level is drawn when `scale x |camera - LOD reference point|`
  lies in `[MinDist, MaxDist)`, `scale = 2 tan(fov / 2) / lodBias`. Levels finer than `QualitySettings.maximumLODLevel` are
  never drawn; the level at that maximum is drawn from distance 0.
- **Drake** also loads and unloads entities (`DrakeStateJob`): loaded while `visibleRange.x <= scale^2 d^2 <= visibleRange.y x extra^2`,
  `d` to the LOD reference point, `extra` the Draw Distance row's extra distance (`ExtraDrawDistance`, 1 to 1.8). Loaded
  entities have `MaterialMeshInfo`; only those are drawn.
- **Medusa** (`MedusaBrgRenderer.CameraFrustumLodJob`): the first level whose squared distance is above `scale^2 d^2`; none: level 0.
- **Leshy** (`LeshyInstancesCameraCulling.CreateLodMask`): level i when `d^2` lies in `[dist[i-1]^2, dist[i]^2)`, plain metres.
- **GameObject LOD groups**: Unity's rule; beyond the last level: not drawn.
- **The LOD bias**: HDRP sets `QualitySettings.lodBias` and `maximumLODLevel` only for the length of its culling
  (`HDRenderPipeline.TryCull`), from the camera's frame settings. The game writes its Draw Distance row into both
  (`DistanceCullingCameraController`, `LodBiasWatcher`, bias 0.4 to 1.0), so the two agree while its watcher runs; the
  tracer now reads the camera's own resolved values (`HDCamera.frameSettings.GetResolvedLODBias`).

What the tracer did: within 30 m the finest level whatever was drawn; `maximumLODLevel` ignored; the coarsest level of a
culled LOD group kept; Drake's visible range worked out wrongly; levels chosen at a scan (every 2 s, or 0.5 s after 4 m).

What it does now (`WorldRules.cs`, used by `SceneCapture.cs` and `LeshyCapture.cs`): the drawn level, by the rule above,
for every path. A level whose meshes are not registered yet is stood in for by the nearest one that is (never dropped);
the status line counts those groups. A level the camera is about to reach (within a quarter of its range) is registered
ahead. Levels are chosen again within 0.5 s of 1 m of camera movement or of a 1 % change of the scale (a zoom).

The numbers that stay: a traced level can lag the drawn one by up to 0.5 s while moving (the scan is 4 to 31 ms on the
main thread, so it is not run every frame), and by the time a new level's mesh takes to register (a few frames).

Not proven: the water-edges agent's "coarser copy of rocks" at the cave lake was read from code and pictures. Those rocks
are Medusa's, and Medusa's traced level already followed Medusa's own rule at each scan. So the level of detail may not
be what made that picture; the in-game check (section 6) settles it.

## 3. No distance past which nothing is traced

What cut: `Scene radius` (250 m; every capture path, the characters, the lights) and `Ray distance` (500 m, the rays'
end). **Both rows and every cut by them are deleted** (17:55): `Settings.cs`, `PathTracingRow.cs`, the radius argument
of every capture (`SceneCapture.cs`, `LeshyCapture.cs`, `VegetationCapture.cs`, `WaterCaustics.cs`,
`CharacterCapture.cs`), the row list in the README and the source checks. A sea without an end is traced to the camera's
far plane. Left, and why: the native frame still has `maxRayDistance` (33 uses in the passes, the camera-ray agent's
files among them); the plugin now always sends "no end" (`Tgpt.NoRayEnd`, 1,000 km), and the field goes when the
frame's layout next changes. `SceneLights.cs` keeps its distance checks with an infinite radius (one line changed; the
checks are the lights' owner's to delete). Native: the light grid (384 x 160 x 384 m around the camera, far lists
beyond) and the 2.5 GB geometry guard are unchanged.

Why the far world is cheap to trace as the game draws it: the game's renderers already thin it. Drake unloads entities
past their visible range and loads far levels, Medusa picks far levels, Leshy stops at its spawn distances. With part 2
each path traces exactly what its renderer draws, so "no radius" adds the game's own far levels and nothing else: the
same meshes instanced (one BLAS a mesh), or small far meshes.

What it costs, as far as numbers exist:

- Instances: Medusa has 3,935 to 4,324 in a whole scene (its log line) against 781 traced at 250 m. Leshy is already
  traced as far as it draws within the radius. Drake: only what is loaded. An estimate of 2 to 5 times today's count,
  30,000 to 60,000 at the vista. **The plugin's summary line gives the true count when the row is set to its top (2000 m).**
- Memory an instance: 64 B in the TLAS's input, about 150 B in the TLAS, 16 B of the tracer's record, 4 B a submesh:
  about 15 MB for 60,000. Meshes: the far levels, once each.
- GPU: the TLAS is built every frame. Measured offline (RTX 5080, section 7): 19,881 more instances raise the scene's
  stage from 0.07 to 0.15 ms of the GPU and the render thread's recording from about 0.2 to 0.98 ms. So 60,000 instances
  are a few tenths of a millisecond of GPU and about 3 ms of the render thread, before any care is taken.
- The scan is what stands in the way. It runs on the game's main thread: 30.6 ms at the vista (13,066 instances), 26 to
  29 ms in the tavern (1,875 instances), 3 to 5 ms at the cave lake. It grows with what the scene holds, not with what is
  traced, and it now runs twice a second while the camera moves. More instances add about a microsecond each.
- Rays to the far plane: a miss travels farther through an emptier scene; small.

What the first run of the vista must be read for (the plugin's summary and status lines): the instance count (13,066
at 250 m), the scan's milliseconds (30.6), the "MB" of geometry (180 in the tavern), the frame's "scene" stage and the
render thread's recording time. If the scan's hitch is felt, the fix is to spread it: one capture path a frame, each
keeping its last list, the scene sent when a list changed (managed only, no ABI change). Not built.

## 4. Grass, leaves and trunks sway as the game sways them

### 4.1 The game's wind maths, read from its compiled shaders

How: `tools/research/unity_shader_blob.py` unpacks each shader from `resources.assets`; `<scratch>/world-work/veg_vs.py`
disassembles its vertex shaders (grass 469 programs, leaves 208, trunk 44; the smallest of each read line by line:
`grass/vs_1_000b26.txt`, `leaves/vs_0_0033c6.txt`, `trunk/vs_0_000cce.txt`). The names come from Unity's own serialized
data, not from guessing: `veg_layout.py` (the material buffer's members and offsets), `veg_strings.py` (each program's
parameter blob: its globals and its texture), `veg_textures.py` (the textures a shader carries itself). The fuller
variants (G-buffer, instanced, DOTS) move the vertex the same way; the motion-vector variants do it twice (this frame's
time and the last).

Shared: `time` is HDRP's `_TimeParameters.x` (Unity's `Time.time`); `speed` and `dir` are the game's globals
`_VegetationWindSpeed` and `_VegetationWindDirection` (degrees), set every frame by its `WindController` (speed 5 to 40,
drifting over 30 to 60 s). `P` is the vertex in object space, `W` its world position, `W2O` the object's inverse matrix.

**Grass** (`TG/Vegetation/Vegetation_Grass`; material value `_Wind_Strength`, 0 to 100, default 60; texture: the shader's
own "GustNoise", 256 x 256, red channel, repeat, bilinear, mip 0):

```
A = (450 - dir) degrees, B = (225 - dir) degrees
R.x = (W.x - 0.5) cos B + (W.z - 0.5) sin B + 0.5          the world xz turned by B about (0.5, 0.5)
R.y = (W.z - 0.5) cos B - (W.x - 0.5) sin B + 0.5
uv1 = 0.2 (0.001 W.xz + 0.1 R + 0.01 speed time)
uv2 = 0.2 (0.001 W.xz - 0.1 R - 0.002 speed time)
gust   = max(noise(uv2), noise(uv1))
weight = saturate(saturate(6 P.y) gust)
k = 1 + 0.0001 speed _Wind_Strength P.y ;  b = k^4 - k^2
moved  = P + weight W2O ((cos A, 0, sin A) b)              a push along the wind, growing with the height in the object
```

At speed 20, strength 60 and half a metre up a blade: k = 1.06, b = 0.139, so a tip moves up to 14 cm in a full gust.

**Leaves** (`TG/Vegetation/Vegetation_Leaves`; material values `_WindBend` (on or off), `_WindBendStrength` (default 0.3),
`_WindBendHeight`, `_WindBendPivotOffset`, `_WindCameraFade` (default 0, 150); texture: the material's `_WindMaskMap`, its
alpha channel, mip 0):

```
theta = atan2(W2O[0][0], W2O[2][0])                        the object's own turn about the up axis
phi   = (450 - dir) degrees - theta ;  axis = (cos phi, 0, sin phi)      the same world axis for every tree
uv    = 0.001 W.xy + 0.02 time                             (the world's x and y, as the shader has it)
n     = alpha(uv) alpha(2 uv)
angle = n speed _WindBendStrength degrees                  6 degrees at speed 20, strength 0.3 and full noise
bent  = P + saturate((P.y + _WindBendPivotOffset.y) / clamp(_WindBendHeight, 0.1, 100)) (rotate(P, axis, angle) - P)
fade  = saturate((viewZ + _WindCameraFade.x) / (_WindCameraFade.x - _WindCameraFade.y))     viewZ: minus the depth along the camera
moved = _WindBend != 0 ? lerp(bent, P, fade) : P
```

**Trunks** (`TG/Vegetation/Vegetation_Trunk`): the leaves' maths with `_MaskHeight` for the height, no pivot offset and
`_WindDistanceFade` for the fade. The plugin maps both onto one kind.

The bend fades with the distance along the camera's view (gone at 150 m by default): the pose depends on the camera, so
the plugin hands the camera's forward direction over with the wind. "Wind wobble" (`_WIndWobbleFrequency`) is in the
pixel shader (it moves the leaf's texture, not its vertices): a material matter for PT-64, not geometry.

### 4.2 The traced pose

- **Plugin** (`PlantWind.cs`): when a material of one of the three shaders is first traced, its values are read by name
  and its noise map is read once from the game's own texture at its own size (through a render target, since the game's
  textures are not readable from scripts; one byte a texel, `TGPT_SetWindNoise`). Every frame, beside the cloth's wind,
  the time, the game's wind speed and direction and the camera's forward go over (`TGPT_SetPlantWind`). Each material is
  logged once ("Plants in the wind: ... noise map ... red 20 to 240 of 255").
- **Native** (`scene.cpp` `plan_plants`, `pose_plants`; `pt_sway.hlsl` steps 3 to 5). Cloth has a copy and a BLAS an
  instance, which does not scale to 10,671 plants (a buffer costs at least 64 KB; ten thousand refits a frame). So the
  plants of one 16 m cell of the world are one **group**: a mesh of its own, made of its members' vertices in the group's
  space. Its texture coordinates, colors, normals and indices are merged once on the GPU (steps 3 and 4); its positions
  are posed every frame by the maths above (step 5: one dispatch a group, a thread a vertex); its one BLAS is built once
  and refitted every frame. To the hit shading a group is an ordinary mesh with ordinary records, so no shared shader
  changed. The members' own still instances are left out of the TLAS while their group is traced.
- **No count and no distance leaves a plant still.** Every instance with such a material is in a group. A group is made
  anew when a member changes (another level of detail, an instance come or gone). At most 24 groups and 1,500,000
  triangles are made a frame; a group waiting for its turn is traced as before (still, distrusted) for those frames and
  counted ("N instances waiting their turn" in the native status line).
- **The distrust goes for what is posed.** A group is traced as still geometry (mask `kMaskStill`, no
  `TGPT_INSTANCE_SWAYS`), so every ray meets it from where it starts. When no instance is left unposed, the tracer's
  "does the scene sway" bit is off and the extra near query for swaying things is not run at all.
- **A pose is kept** while the time, the wind and the camera are the same (the game paused).

What it costs:

- Memory: a posed plant is no longer an instance of a shared mesh. A group's buffer is 32 B a vertex and 4 B an index,
  plus its BLAS. Not measured in game; the native status line gives the vertices and triangles posed.
- GPU: one dispatch and one refit a group a frame. Measured offline (RTX 5080, another program sharing the GPU): a
  field of 19,881 tufts (477,144 triangles, 30 groups) posed and refitted every frame costs 0.34 ms of the GPU in the
  scene's stage, against 0.15 ms for the same field standing still as 19,881 instances. The render thread's recording
  goes from 0.98 to 2.07 ms: the groups are planned again every frame (every plant's key hashed). That plan only changes
  when the scene does (every 0.5 to 2 s), so it can be kept between scans: an idea, not built.
- The game's grass is denser than a tuft of 24 triangles. The native status line's triangle count in a run of the vista
  times 0.4 microseconds a thousand triangles (the measure above) is the refit's expected cost there.
- Posed plants get no opacity micromaps (they are made for a still mesh): their alpha is tested texel by texel a ray.

What is not covered, each a gap:

1. A posed plant's glowing material is not sampled as a light (as characters'): met by rays, not chosen.
2. Trunks are not flagged as swaying, so a trunk waiting for its group is trusted where it stands still (as before).
3. `_WindMaskMap` is read through a blit: if mip streaming holds only a coarse mip then, the map is coarse.
4. The tracer's own motion vectors for a swaying plant (the camera-ray agent's): the last frame's pose is not kept.
5. Cloth still has its own path with its caps (row 14 of section 1).

## 5. Decals seen by rays (design)

Today HDRP draws decals into its DBuffer before the G-buffer, and the Lit shader blends them into the surface there. A
ray's hit reads the material alone. The plugin now counts them every scan and logs once ("Decals: N projectors in the
scene, M in reach (K glowing, J materials)"): the design's cost rests on those numbers, which no log has yet.

1. **Capture**: every active `DecalProjector`: its box (size, pivot, transform), its material's maps and values (the base
   color map and color, the normal map, the mask map, their blend sources, what it affects, its emission), its fade
   (`fadeFactor`, the angle fade, the draw distance), its draw order and its layer mask. The game also has its own decal
   classes (`StaticDecalsCuller`, `DecalController`); any decal shader of its own needs reading as the vegetation's was.
2. **Find the decals over a hit**: the boxes as a second, small acceleration structure of boxes (one BLAS of all decals,
   rebuilt when they change). A hit asks it with a ray of no length at the hit point; each box holding the point is a
   decal over it. This reuses the GPU's own tree and has no list length to cap. (A grid like the lights' is the cheaper
   start if a scene has few hundred decals; the count decides.)
3. **Blend as HDRP does**, in draw order: each decal's color over the last by its alpha, the remaining weight kept; then
   `base = base x remaining + decals`, the same for the normal and for metal, occlusion and smoothness. Emission adds.
4. **Where**: the one place a hit's material is made (`pt_common.hlsli`), so the camera's own rays, mirrors and bounces
   all get it. Its textures are the game's own (PT-46's shared textures).
5. **Order of work**: color, then normal and mask, then emission (met by rays first; sampled as lights later), then
   decal meshes (geometry drawn into the DBuffer) and shader-graph decals.
6. **Offline check** (the brief's): a decal on the floor shows in a mirror, in its color, and is gone with the decal off.

Cost to expect: one short query a shaded hit, like a shadow ray that ends at once, plus up to three texture reads a
covering decal.

## 6. What the in-game run must show, one scene a part

| Part | Scene | The log must show | The picture must show (Denoising None first) |
|---|---|---|---|
| 1 | `vista` | the plugin's summary: no "drake:", "leshy:" or "renderers:" problem text; "N Drake entities the game has not loaded for drawing (left out)"; "N trees past their last mesh traced as that mesh"; "statically batched renderers traced" only if the scene has any | Coverage view: far trees covered where the game draws cards; no building or roof missing against run `20261007-124601/vista` (if one is, the Drake count above is why: say so) |
| 2 | `glow-lake` | "LOD as drawn (bias B, from level L)" with the game's Draw Distance as B; "groups at another level while theirs registers" gone after a few seconds; an F8 probe on a rock says "(the level drawn: traced)" | the rocks' traced outline on the drawn one (the camera-ray agent's yardstick, or the Surfaces view against run `20261007-043153/glow-lake`) |
| 4 | `vista` | plugin: one "Plants in the wind: ..." line a material, with a noise map whose range is not 255 to 255; native status line "plants: N in the wind's pose in G groups (V vertices, T triangles, ...)" with N near the vegetation count and "0 instances waiting their turn" once settled | grass shadows that move between two frames 170 ms apart; the Coverage view without the dark streaks at blades (PT-VEGETATION's recipe 6); the "scene" stage's GPU time against the last run |
| 3 | `vista` | the summary's instance count and scan time, the status line's MB and "scene" stage, against run `20261007-124601/vista` (13,066 instances, 30.6 ms); no "starts over" line | the far hills, trees and buildings in the Coverage view where the game draws them |
| 5 | any | "Decals: N projectors in the scene, M with a material (K glowing, J materials)" | nothing yet |

## 7. Tests

- **Rules** (`tests/world_rules_tests.cs`, `tests/WorldRules.Tests.csproj`): 53 expectations, each worked out from the
  game's code by hand. Mutation run (`<scratch>/world-work/mutate_rules.py`, log `mutate_rules.log`): 15 mutants, all
  killed, each at its own FAIL line (the finest level traced whatever is drawn; a culled group traced at its coarsest
  level; the maximum LOD level ignored; a batch member placed from submesh 0; and so on).
- **Source checks**: 90 pass (`test_path_tracing_world_is_what_the_game_draws` is new; the `NearDetail` check now says
  the rule is gone; the ABI check says 0x00011F00). The three checks the brief named as failing do not fail in this copy.
- **Plugin**: `dotnet build src/TGPathTracing -c Release -o %TEMP%\tgb\world-plugin`: 0 errors, 0 warnings.
- **Native**: `TGPathTracing.Native` and `tgpt_gpu_test` build (`%TEMP%\tgb\world-b`, DXC with `-WX`).
- **GPU** (`tests/pathtracing_gpu_test.cpp`): the baseline (this copy before the native changes) passed whole
  (`world-work/gpu-base.txt`). Checks 14 and 15 are new: five tufts of grass (turned and scaled each its own way, one
  across a cell's edge: two groups) and a small tree (a trunk and leaves, two materials) in the sun; the floor's direct
  light with the plants posed by the tracer against the same plants posed on the CPU by `grass_wind_reference` and
  `leaves_wind_reference` (written from the compiled shaders' instructions, not from `pt_sway`), against the still
  plants and against a later time; grass and tree judged apart. Check 15: the floor at their feet, against plants rays
  do not trust. **How they went: see the lines below.**

GPU results (`world-work/gpu-plants4.txt`, native 1C4EE34A093A, 2026-10-07 17:23): **all path tracing checks passed**, 0 FAIL lines.

| Check | Pixels | Posed by the tracer against the game's pose (CPU) | The still plants | A later time |
|---|---|---|---|---|
| 14, five tufts of grass (two groups) | 2,144 where the shadows move | 0.0000 | 0.2889 | 0.0812 |
| 14, a tree (a trunk and leaves) | 341 | 0.0000 | 0.1196 | 0.2133 |
| 15, the floor at the plants' feet | 373 the sun reached past distrusted plants | 0.0000 | (distrusted) 0.1503 | |

The plants' tips move up to 67 cm in the test's wind (speed 30, grass strength 80, leaves 0.9). Cloth's check 12 beside
it is unchanged (0.0000 against its CPU pose).

A bug the first run found, worth keeping: a raw buffer view must start on a multiple of 16 bytes
(`D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT`). The group's buffer holds six parts, each read through a view of its own, and the
parts started wherever the last ended. Without the debug layer nothing says so: the device is removed
(`DXGI_ERROR_INVALID_CALL`) and the next creation fails. Each part now starts on 16 bytes.

GPU mutation runs (`world-work/world-mutants.ps1`, `mutate_gpu.py`; summary `world-work/gpu-mutants.txt`): see the
lines below.

| Mutant (one thing broken in `pt_sway` or `scene.cpp`) | Result |
|---|---|
| 1: grass pushed by k - 1 in place of k^4 - k^2 | killed (3 FAIL): the grass's shadows 0.2031 off its CPU pose (0.0000 unbroken); the tree's still 0.0000 |
| 2: the tree's own turn left out of the wind's axis | killed (2 FAIL): the tree's shadows off, the grass's not |
| 3: leaves' bend not faded along the camera's view | **not run** |
| 4: posed plants still distrusted near a ray's start | **not run** |
| 5: a group's indices not counted from each member's first vertex | **not run** |

**Not run, and why:** from 17:44 the GPU was never free for 40 minutes (other agents' tests), so I stopped the queue
rather than leave it applying mutants to the copy while it may be merged. No mutant is left in the copy (no
`*.world-orig` file; the restored `pt_sway.hlsl` checked line by line).

What is left to run once the GPU is free (it builds into `%TEMP%/tgb/world-b`, never `artifacts`):
`powershell -File <scratch>/world-work/world-final.ps1`. It runs the copy's GPU test once (`world-work/gpu-final.txt`),
then mutants 3 to 5, restoring each (`gpu-mutants.txt`; a file `world-work/STOP` ends it before the next). Since the
passing run `gpu-plants4.txt` the native code is unchanged; the test changed in one place (the plants' materials are
plain, as the plugin now sends them: posed plants are never flagged), and the plugin lost the distance cuts (which the
GPU test does not run).

## 8. Files, ABI, bits

- New: `src/TGPathTracing/WorldRules.cs`, `src/TGPathTracing/PlantWind.cs`, `tests/world_rules_tests.cs`,
  `tests/WorldRules.Tests.csproj`, this report.
- Changed, managed: `SceneCapture.cs` (the LOD rule for every path, static batches, Drake's loaded state and stray
  entities, the quicker scan trigger, the status line's counts, the decal count, the plants' hooks, no radius),
  `LeshyCapture.cs` (Leshy's own level rule, trees past their last mesh, no radius), `NativeTracer.cs` (the two exports,
  two structs, the ABI, `NoRayEnd`), `Settings.cs` and `PathTracingRow.cs` (the two rows deleted), `VegetationCapture.cs`,
  `WaterCaustics.cs`, `CharacterCapture.cs` (no radius), and one line each in `SceneLights.cs` (an infinite radius) and
  `HdrpPasses.cs` (`MaxRayDistance = Tgpt.NoRayEnd`): the two files of other parts I touched.
- Changed, native: `include/tgpt_api.h`, `src/runtime.hpp`, `src/extension.cpp`, `src/tracer.cpp` (the hand-off and the
  status line), `src/scene.hpp`, `src/scene.cpp` (the groups), `shaders/pt_sway.hlsl` (steps 3 to 5).
- Changed, tests and docs: `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`, `docs/TESTING.md`, `CHANGELOG.md`,
  `README.md` (the row list), `docs/PT-REQUESTS.md` (PT-63's and PT-61's state),
  `docs/PROJECT-CONTEXT.md` (9.2: the LOD and wind bullets; the file was past its 700 lines before, 786 now).
- ABI 0x00011F00: exports `TGPT_SetPlantWind`, `TGPT_SetWindNoise`; structs `TGPT_PlantWind` 48 B, `TGPT_PlantWindFrame`
  32 B; constants `TGPT_MAX_PLANT_WIND` 4096 (materials, not plants), `TGPT_PLANT_GRASS`, `TGPT_PLANT_LEAVES`,
  `TGPT_PLANT_BEND`, `TGPT_NOISE_*`. `pt_sway`'s pass constants: `pass.a.z` 0 is cloth as before, 3 to 5 the plants' steps.
- Bits: none taken. The status struct is unchanged (the plants' counts are in the native log's status line only).

## 9. Risks, and ideas not built

- **No radius, unmeasured.** How many instances the game draws at any distance, what the scan then costs on the main
  thread and how much memory the meshes take is known only from the first run. The players' saved `Scene radius` and
  `Ray distance` lines stay in their config file as leftovers that nothing reads.

- None of the capture changes ran in the game. The ones most likely to surprise: Drake's "loaded for drawing" test (if
  something the game does draw has no `MaterialMeshInfo`, it is gone, and the count in the status line says how many);
  LOD groups culled past their last level (small props far off no longer cast shadows the game's props do not have);
  trees in place of cards (more instances far off).
- Groups are made anew as Leshy's level rings move with the camera. The budget (24 groups, 1.5 million triangles a
  frame) is a guess; "instances waiting their turn" in the native log says whether it holds while walking.
- The noise read (a blit and a read-back a map, once) runs on the main thread when a material is first traced.
- Ideas: move cloth into the groups (its caps go, and with them the last use of the distrust: section 10); keep leaves past their fade distance as plain instances (their pose
  is the still one there: no refit, the same picture); a group's refit only when a member's pose moved more than a
  fraction of a pixel; micromaps for groups whose alpha maps are settled; the scan spread over frames (section 3).

## 10. Workarounds met that exist only because the tracer started from HDRP's picture

Asked for by the coordinator at 17:55 (the owner: "remove all the 'hacks' and workarounds as we go ... So we have a clean
base to work from"). Each with where it lives and whether this job removed it. "Not removed" means it is outside this
job's part or still serves something; the reason is given.

| # | Workaround | Where | State |
|---|---|---|---|
| 1 | The finest level of detail traced within 30 m whatever the game drew. It hid that a level traced for where the camera was no longer matched the surface HDRP drew ("darkens as I walk up"). | `SceneCapture.cs` `NearDetail`, `PrefetchDetail`, `TracedLevel` | **Removed.** |
| 2 | A LOD group the game culls kept at its coarsest level "for light". | `SceneCapture.cs` `CaptureRenderers` | **Removed.** |
| 3 | Plants flagged as things the wind moves that the tracer holds still, so rays pass them by near their start. | `SceneCapture.cs` `Sways` (the grass and leaves names) | **Removed**: they are posed, the flag is no longer set for them. |
| 4 | The distrust itself: an instance mask for swaying things, rays from a surface asking for still things first and passing swaying ones by within 20 cm, an extra near query, a bit in the frame that says the scene sways. | `tgpt_api.h` `TGPT_MATERIAL_SWAYS`, `TGPT_INSTANCE_SWAYS`; `scene.hpp`/`scene.cpp` `kMaskSways`, `kMaskStill`, `swayingInstances`; `tracer.cpp` (the frame's `radiance[3]` bit); `pt_common.hlsli` `MASK_SWAYS`, `MASK_STILL`, `SWAY_REACH`, `scene_sways`, the retry in the rays from a surface; `NativeTracer.cs` `MaterialSways` | **Not removed.** It still serves cloth past the 4,096 copies the tracer poses (`kMaxSwayCopies`). It is dead once cloth is posed in groups as the plants are (no count); then all of the above goes, with the GPU checks that lean on it (11's "traced still", 15's "distrusted"). It is in shader code every pass shares, which changed in main after this copy was made. |
| 5 | A window in which the camera's ray looks for a swaying cloth's traced surface near HDRP's. | `pt_prepare.hlsl` `CLOTH_SWAY_REACH` (with `scene_sways`) | **Not removed**: the camera-ray agent's file. Dead with the tracer's own camera rays. |
| 6 | Rays start where HDRP drew the surface, which can lie inside the traced mesh, so a ray whose first hit is the inside of a mesh close by passes out. | `pt_common.hlsli` `near_back`, `surface_near_back`, the pass-through in the rays from a surface; `tgpt_api.h` `TGPT_OPTION_NO_PASS_THROUGH`; used in `pt_di_spatial`, `pt_gi`, `pt_gi_spatial`, `pt_hdrp_shadow`, `pt_specular`, `pt_water`, `pt_rtxdi.hlsli` | **Not removed**: the camera-ray agent's. Another level of detail than the drawn one was a main cause; that cause is gone with part 2. |
| 7 | Shadow-only meshes traced as stand-ins that rays leaving a drawn surface skip. | `SceneCapture.cs` `standIns`, `TGPT_INSTANCE_STAND_IN` in `scene.cpp` and `pt_common.hlsli` | **Not removed**: a fallback while Entities Graphics cannot be read. With the tracer's own camera rays a stand-in would show on screen: the camera's rays must skip it too, or the fallback must go. |
| 8 | A ray's hit borrows the game's on-screen material. | `pt_common.hlsli`, `pt_specular.hlsl`, `pt_water_under.hlsl` `hdrp_material_at` | **Not removed**: PT-64. |
| 9 | Trees past their last mesh left out, because the game's card is no geometry. | `LeshyCapture.cs` | **Removed**: the tree's mesh is traced. |
| 10 | A distance past which nothing is traced and a length at which a ray ends, as rows. | `Settings.cs`, `PathTracingRow.cs`, every capture | **Removed** (rows and cuts). The native frame's `maxRayDistance` is left, fed "no end" (section 3). |
| 11 | Statically batched renderers skipped. Not from HDRP's picture, but dead code of the same kind. | `SceneCapture.cs` | **Removed.** |
| 12 | Drake's visible range worked out again by the plugin (wrongly) in place of the game's own state. | `SceneCapture.cs` `CaptureDrake` | **Removed.** |
