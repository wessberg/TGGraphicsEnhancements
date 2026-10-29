# The game's height maps traced: relief with self-shadows (PT-28): worker "relief-1", 2026-10-10

Copy `scratchpad\wt\relief1` (base `wt\base79`). Scripts and their output: `scratchpad\r1\`. Nothing here ran in the
game. The four claims, kept apart: **built** see "State"; **passes the harness** no run; **seen in game** no; **the
owner says it's fixed** no.

## In short

- **The castle walls have a height map the game never reads.** `Mat_Castle_Wall_01`, `_03`, `MI_Wall_01`, `_03`,
  `Mat_Brick_Stone_Carved` and 13 more hold a `_HeightMap`, but their displacement is set to "None". Proven: the
  game's compiled shader for them has the same 320 instructions and the same 8 texture reads as for a wall
  without a height map, and never reads the map. So in the game's own picture those walls are flat triangles with
  a normal map. By the brief's rule (only a height the game's shader uses) nothing is built for them. **A choice
  for the owner**, below.
- **Really in use, in the whole game: 6 materials** (of 8,100): the floor `MI_Floor_01_HoS` and five of HDRP's
  Lit (`StoneTileCDEP`, `StoneTileCDEP 1`, `Mat_WeaponHit_Ground`, `_Stone`, `_Wood`).
- **None of the game's variants shadows itself.** The game only shifts the texture; its shadow pass knows no height.
- The scene `spec-terrain` shows one of the six: `MI_Floor_01_HoS`, about 4.5 % of the screen (the paving at the
  foot of the stairs). The trial is for that one.

## State (kept current)

- 2026-10-10 00:22 started. Read the brief, PT-28, PT-104, `terrain-1.md`, `materials.md`.
- 00:30 counts from the census of every material in the game's files (`scratchpad\mw\census.json`, 8,100
  materials; `r1\height_census.py`, `r1\height_mats.py`).
- 00:35 the proof from the game's compiled shaders (HDRP's Lit from `globalgamemanagers.assets`, the floor's graph
  from `resources.assets`): the table below. The inventory is done.
- 00:45 the trial written (sections 2 and 3): shaders, plugin, CMake switch, recipe, a source check. The plugin
  builds: 0 warnings, 0 errors. `python tests/source_checks.py`: 109 of 109 (108 before, one new).
- 00:44 the shader build started (`tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\relief1 -Throttle 2`, the
  tree configured with `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON -DTGPT_RELIEF=ON`; log `r1\shaders.log`). It
  WAITS: two compiles of other trees hold the PC's two places while a game run is wanted (`GAME-WANTED`). **Until
  it has run, the shader code is written and read, not compiled.** State of it: the last lines of this list.
- 00:50 `relief_march` changed once before any pass compiled: a leaving ray starts just above the field as the
  mip IT reads shows it (the crossing was found in another mip; without this a shadow of nothing).
- 01:06 the first passes compiled WITH the relief in them, warnings as errors, none failed: `pt_gi_spatial`,
  `pt_gi_spatial_dxr12`, `pt_specular` (436 s), `pt_specular_ser` (485 s). The reflections' pass calls
  `hit_material` and `direct_light`, so the new lines are in its code. 35 of 40 headers still to go.
- 01:38 **all 40 shader headers compiled with the relief in them: 40 compiled, 0 failed** (warnings as errors;
  3,288 s by the clock, most of it waiting for a place; `r1\shaders.log`). Among them the two that hold the
  trial's own lines: `pt_prepare` (the camera pixel's word) and `pt_di_spatial` (its sunlight).
- 01:39 `cmake --build %TEMP%\tgb\relief1 --target TGPathTracing.Native`: links, exit 0 (`r1\build.log`). The
  plugin built again from nothing: 0 warnings, 0 errors. Source checks 109 of 109. The copy's own `bin` and `obj`
  folders removed. **Handed over. STOPPED here, as the brief says.** No fixture, no GPU test, nothing in the game.

## 1. The inventory, with proof

Counted over the whole game (8,100 materials in its bundles), not one scene. "Read" means: the GBuffer pass's
variant disassembled (`r1\lit\blob*.txt`, `r1\kf\blob*.txt`) and its parameter list read (`r1\pblob.py`).

| shader | keyword | materials | the height | in use by the game's picture? the proof |
|---|---|---|---|---|
| `TG/Architecture/KF_Architecture` | `_PARALLAX_OCCLUSION_MAPPING` | **1**: `MI_Floor_01_HoS` | the base mask map's BLUE channel (`_Base_MaskMap`, the same texture that holds metal, occlusion and smoothness); `_Amplitude` 0.56 (hundredths: 0.0056 of one repeat of the texture, see "the amplitude's unit"); `_Steps` 8, a fixed count; then 3 refining steps | **YES.** Variant blob 253 has two marches that read t6 (`_Base_MaskMap`) in a loop, and the parameters `_Amplitude` and `_Steps`; the plain variant (blob 258) has neither (31 texture reads against 22) |
| `HDRP/Lit` | `_PIXEL_DISPLACEMENT` with `_HEIGHTMAP` | **5 in use** (+1 with amplitude 0: `Mat_InventoryGround`): `StoneTileCDEP`, `StoneTileCDEP 1` (2 cm), `Mat_WeaponHit_Ground` (2 cm), `Mat_WeaponHit_Stone`, `_Wood` (10 cm) | `_HeightMap`'s RED channel; `_HeightAmplitude` in metres (0.02 or 0.1; the editor's `_HeightPoMAmplitude` is the same in cm); `_HeightCenter` 1 (the mesh is the top, everything goes inward); 5 to 15 steps by the view angle (`_PPDMinSamples`, `_PPDMaxSamples`), 3 refining steps; fades out with distance (`_PPDLodThreshold` 5) | **YES.** Variant blob 176 reads t6 (`_HeightMap`) in a loop and has `_HeightAmplitude`, `_PPDMinSamples`, `_PPDMaxSamples`, `_PPDLodThreshold`, `_InvPrimScale`, `_InvTilingScale` |
| `HDRP/Lit` | `_HEIGHTMAP` alone (`_DisplacementMode` 0, "None") | **18**: `MI_Wall_01`, `MI_Wall_03`, `Mat_Castle_Wall_01`, `Mat_Castle_Wall_03`, `Mat_Brick_Stone_Carved`, `Mat_Cuanacht_Roofs_Wooden_B`, `_C`, `_D`, `BonePile_01`, `Mat_WeaponHit_Leather`, `_Metal`, six `*_main_material`, ... | a `_HeightMap` is set; amplitude left at HDRP's default (2 cm) | **NO.** Pixel shader blob 177 (with the keyword) against blob 179 (without): the same 320 instructions, the same 8 texture reads; the one differing line is how the pixel's position is interpolated. Its parameter list has no `_HeightMap`. Vertex shader blob 28 against 30: the same 105 instructions. The game has no variant with vertex displacement at all (0 of 336), and no tessellation shader |
| `TG/Architecture/KF_Architecture` | `_DEPTHOFFSET_ON` without the parallax keyword | 41 | none | **NO.** Blob 255 against 258: no `_Amplitude`, no `_Steps`, the same 22 texture reads |
| `HDRP/LayeredLit` | `_HEIGHT_BASED_BLEND` | 2: `Mat_Anthill`, `Mat_stones_roots_01_soil` | no height map set on either; displacement "None" | **NO relief**: the keyword blends layers by a height, it moves nothing. Its variant not read |
| `HDRP/Lit` | `_TERRAIN_PARALLAX` | 0 | - | **NO.** The state dump's two (`Mat_Hunting_Shrine_Wood` ...) are HDRP/Lit materials that carry keywords left over from another shader. HDRP/Lit has no such keyword (its 93 keyword names: `r1\lit_passes.txt`) |
| `Shader Graphs/ParallaxMapping` | `_BUMP_TYPE_PARALLAX_OCCLUSION` | 2: `Mat_DecalParallaxHole`, `ParallaxMapping` | not read | a DECAL shader (its passes are `DBufferProjector`, `DBufferMesh`): a painted hole. Belongs to the decals; not read here |
| `TG/VFX/VFX_Icicles`, `VFX_Icicles_Kandra` | `_UseParallax` 1 | 13 (ice and crystals: `Mat_Weapon_2H_Frostmaw`, `Mat_ChMon_Crystalwalker_01_Crystals`, ...) | `_ParallaxHeight`, `_Parallax` 8 or 20 | not read: see-through effect materials, the look inside ice. Not walls or floors |

**Does the game's variant shadow itself? No, in every case.** The floor's shadow pass with the parallax keyword
shares its parameters with the plain one (parameter blob 22 for both: no height in it), and neither the floor
(`_DepthOffsetEnable` 0) nor the six Lit materials (`_DepthOffsetEnable` 0) write a moved depth. The game's lighting
is deferred: it lights the flat triangle's place with the shifted texture. So self-shadow is something the tracer
adds by tracing the same height field, not something the game shows. It is the owner's request and it is physics
of the same field, not a made-up one.

**The amplitude's unit, the floor** (read from blob 253, lines 177 to 265): the game shifts the texture coordinates
by `(1 - height) x tan(view angle) x _Amplitude x 0.01`, in units of ONE REPEAT of the base texture (the tiling
divides out in the code: `2 / (|t| + |t|) x t`), times the inverse of the object's scale along its x (for u) and
its z (for v). So the field's depth is 0.56 % of the texture's repeat length, measured in the object's own space.
For a texture that repeats every 2 m that is 1.1 cm. Shallow: this floor's relief is mild.

**The floor marches twice.** The second march moves the overlay layer's coordinates, reading the same blue channel
at the overlay's tiling. Two different surfaces for one pixel: a trick of the graph. The tracer has one surface
(the base's field) and reads the overlay at that crossing.

**A choice for the owner (the castle walls).** The owner's words were "bricks and stuff ... like the castle walls".
Those walls' materials hold a height map and an amplitude (2 cm, HDRP's default: no artist set it), and the game
does not use them. Tracing them would use "a height the game's material holds" but show relief the game's own
picture does not have. The brief forbids it ("holds AND its shader uses"); it is one line of plugin code away once
the Lit kind is built (the same march, HDRP's map and number). To ask: "The castle walls have a height map the game
leaves switched off. Trace it anyway (2 cm deep), or leave the walls as the game shows them?"

**How many in a scene:** the plugin's new line, once a scene (section 3).

## 2. The design for the tracer, for every ray

**The surface is the height field under the triangle.** The field has a space of its own: x and y are the base
texture's tiled coordinates, z the height (1 at the triangle, 0 at the deepest). A direction d of the world becomes
`(d.T x a, d.B x a, d.N)` there: T and B the triangle's tangent and bitangent, N its smooth normal, a the game's
amplitude (for the floor: `_Amplitude x 0.01`, by the object's inverse scale). In that space a ray is a straight
line, and the game's own shift of the texture is the crossing of that same line. So one function walks any ray,
in or out, with the game's own map, channel and number (`relief_march`, `pt_materials.hlsli`).

| | what happens | cost |
|---|---|---|
| A ray meets such a triangle (the camera's, a mirror's, a bounce's, the walk's) | it goes on into the field from the entry point down, a texel of the map a step, to the first crossing; the crossing is refined to where the ray meets the line between the two heights around it. The hit's texture coordinates are the crossing's, so colour, normal map, metal, smoothness and occlusion are all read there. **Where: in `hit_material`**, which every one of those rays already calls, so a mirror shows the same relief | one read of the record, then at most 32 reads of one channel of one texture; no ray traced. On every other material: one comparison |
| A ray leaves such a surface (a shadow ray to the sun or a lamp, a bounce, a reflection) | it starts at the crossing and walks UP through the field. Field in the way: the ray ends there (shadowed; a bounce or a reflection sees the field's own wall, shaded at that second crossing). Out through the top: the ray goes on into the world from the point where it left the triangle's plane. **Where: in the few functions every leaving ray goes through** (`transmittance`, `transmittance_from_surface`, `trace_from_surface`: 39 lines in 12 shader files name them) | at most 32 reads before the ray; a ray the field stops is not traced at all, so a shadowed pixel gets cheaper |
| The hit's place | the crossing lies below the triangle by up to the field's depth (about 1 cm here), along the ray. The place moves there, so depth, motion and the denoiser's guides are the crossing's | none |
| The hit's normal | the game's normal map, read at the crossing (as the game does). The field's own slope is not turned into a normal: the game's art has map and height from one sculpt, and two normals would disagree | none |

**The steps are not a limit on the picture.** The game marches 8 steps (the floor) or 5 to 15 (Lit) and so steps
over thin ridges at a flat angle. The tracer's step is one texel of the mip it reads, and it reads a mip coarse
enough that the way is 32 texels at most. A grazing ray crosses many texels and reads a blurrier picture of the
field; it never jumps over a ridge of the picture it reads. (To make it exact at every angle: a pyramid of the
map's highest values, walked like a quadtree. Not built; the map would be read as it is, no copy of it kept.)

**The frame (the lesson of PT-104).** For the floor: the triangle's own texture directions (`hit_frame`: dP/du made
square to the smooth normal, the bitangent on dP/dv's side). The game turns by the mesh's tangents, which for a
mesh Unity imported are the same directions. A mesh that brings no texture coordinates has no frame: the tracer
then leaves the triangle flat, and **the plugin's log names that mesh** ("Relief material ... is drawn with mesh
..., which brings NO texture coordinates ..."). Never silent. For HDRP's Lit kind: the same frame (its five
materials read the mesh's first coordinates, `_UVBase` 0, no planar or triplanar mapping).

**The outline.** The game's parallax does not move the outline: its triangles stay flat, the texture slides
inside them. The tracer's does not either: a ray enters the field only through the triangle, and near the
triangle's edge it reads the texture past the edge, as the game does. Same as the game's picture. (A true
displaced surface would show its side at a step's edge. Neither shows it.)

**The camera's first hit and the yardstick.** The game's G-buffer holds the shifted texture at the flat
triangle's depth. With the field traced, the tracer's colour, normal and roughness at such a pixel are read at
the same shifted place, so the yardstick's "another colour, normal, roughness" for the floor should FALL (now 3,
3 and 7 % of its pixels: its texture sat unshifted). Its "place" stays 0 in the trial (the place is not moved);
in the full design the place moves by at most the depth over the cosine, millimetres against the yardstick's
tolerance of 1 cm plus 1 % of the distance. Only at very flat angles could it count as "another place".

**What the camera's pixel needs for the full design.** The passes after the first hit work from the pixel's
surface record, which keeps neither the triangle nor the texture coordinates. For a relief pixel's leaving rays
they need: the material (20 bits), the crossing (two coordinates and a height) and the tangent. The hair
texture's two words are free for a plain pixel and hold the first two; the tangent needs a third word (33 MB at
4K), or the pixel's triangle is kept and the hit made again. A cost to show before it is taken.

**Two differences from the game's picture, both named:**

1. Self-shadow. The game has none (section 1). The tracer adds it by the same field: the owner's request.
2. The floor's overlay layer. The game shifts it by a second march (the same blue channel read at the overlay's
   tiling: another surface for the same pixel). The tracer has one surface and reads the overlay at the base's
   crossing. On this floor (base tiling 0.5, overlay 1) the overlay then shifts as far as the base, where the
   game's overlay shifts half as far. Small, and only where the overlay shows.

## 3. The trial: what is built

ONE kind: `TG/Architecture/KF_Architecture` with `_PARALLAX_OCCLUSION_MAPPING` (`MI_Floor_01_HoS`, the one the
scene `spec-terrain` shows). Two switches: CMake's **`TGPT_RELIEF`** (OFF by default: no pass holds a line of
it; the shaders' define is `PT_RELIEF`) and the hidden **`Advanced / Relief trial`** (0 by default).

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_materials.hlsli` | one `#ifdef PT_RELIEF` block: `relief_height` (the map's blue at a mip), `relief_march` (the walk, in or out), `relief_enter` (the ray that met the triangle goes on into the field; the hit's texture coordinates become the crossing's), `relief_lit` (0 where the field stands between the crossing and a light) |
| `native/pathtracing/shaders/pt_common.hlsli` | three `#ifdef PT_RELIEF` blocks: `hit_material` calls `relief_enter` before it reads anything (4 lines); `direct_light`'s sun asks `relief_lit` before its shadow ray (3 lines: a path's vertex, a mirror's); the constant `RELIEF_SHADOW` |
| `native/pathtracing/shaders/pt_primary.hlsli` | `write_traced`: the camera pixel's crossing walked toward the sun's centre; where the field is in the way, the pixel's hair-texture word says so (that word was 0 for a plain pixel and read by no pass for it) |
| `native/pathtracing/shaders/pt_di_spatial.hlsl` | the pixel's sunlight is 0 where that word is set (6 lines) |
| `src/TGPathTracing/MaterialPrograms.cs` | the floor's record: its value [5].y is the field's depth (`_Amplitude x 0.01`, with the tiling's sign), 0 without the keyword or with the trial at 0; a change of the setting rewrites that word in the records already sent, so one run steps through it. Three log lines (below) |
| `src/TGPathTracing/Settings.cs` | `Advanced / Relief trial`, hidden, 0 |
| `src/TGPathTracing/SceneCapture.cs` | one line: an instance with a traced height field names its mesh once |
| `CMakeLists.txt` | `option(TGPT_RELIEF ... OFF)`, `-D PT_RELIEF` for the passes |
| `tests/ingame/scenarios.json` | the recipe `parallax` (the name `relief` was taken by PT-95's filter trial) |
| `tests/source_checks.py` | `test_path_tracing_relief_on_trial_default_off` |

No native C++ line, no struct, no export, no ABI change, no option bit, no new texture (the mask map is one the
floor's program already reads). The switch travels in the material's own program record, in a word that was 0.
An old native side under the new plugin changes nothing; the new native side under an old plugin reads 0.

**What the trial does NOT do (it is the smallest thing that shows the direction):**

- The hit's place stays on the triangle. Only the texture coordinates move.
- Of the rays that leave, only the SUN's shadow ray walks out. Lamps (the brazier), bounced light and
  reflections leave from the flat triangle as before. So the self-shadow shows in sunlight, not by the fire.
- The camera pixel's sun shadow is walked toward the sun's centre once, where the pixel is written. The sun's
  disc is half a degree and the field a centimetre deep: its penumbra there is a tenth of a millimetre.
- A path's vertex gets its self-shadow through a value `hit_material` leaves behind for the next
  `direct_light` call, asked by the hit's place. Plumbing for a trial; the full design carries it in the hit.
- At most 32 steps, by reading a coarser mip (section 2).

### The log lines (the plugin alone; no native build needed to get them)

- `Relief: N materials with a height in use: A the path tracer can trace (Relief trial 0: flat, as before):
  <names>; B of HDRP's Lit with pixel displacement, not traced yet: <names>. C more hold a height map the game's
  own shader never reads (displacement off; flat in the game too): <names>.` Once a scene, two seconds after its
  last such material arrived. For `spec-terrain` I expect N = 1 (`MI_Floor_01_HoS`) and C of 2 or more
  (`Mat_Castle_Wall_01`, ...).
- `Relief material "<name>": its height is its base mask map's blue channel, 0.56 hundredths of one repeat of
  that texture deep (tiling 0.5); the game marches 8 steps and does not shadow it.` Once a material.
- `Relief material <name> is drawn with mesh "<mesh>" (<n> vertices), which brings texture coordinates: its
  height field is laid in each triangle's own texture directions.` Or "brings NO texture coordinates ... leaves
  it flat". Once a material and kind of mesh.
- `Relief on trial: 1: ...` when the setting changes.

## 4. The run

Build: configure the main tree with `-DTGPT_RELIEF=ON` (and the rest as it is), `tools\Build-PtShaders.ps1`,
`cmake --build`, install; the plugin; `tools\harness\Deploy-PtBuild.ps1`. Every pass compiles again
(`pt_materials.hlsli` is part of `pt_common.hlsli`). Then:

`.\tools\harness\Run-PtHarness.ps1 -Scenario spec-terrain -Recipe parallax` (about a minute of steps)

| step | what |
|---|---|
| `1-game` | the game's own picture |
| `2-none-flat` | traced, no denoiser, Rain wetness 0, Relief trial 0; 16 frames |
| `3-none-relief` | Relief trial 1; 16 frames |
| `4-off` | everything back |

**What it should show (predictions, nothing measured):**

- **Where:** the paving at the foot of the stairs, right of the middle of the screen, round the post and the
  planks (`MI_Floor_01_HoS`, about 4.5 % of the screen). Nothing else may change: not the terrain in front, not
  the walls. If anything else changes between steps 2 and 3, that is a defect of the trial.
- **The shift:** between steps 2 and 3 the paving's texture moves a little, more the flatter the view, joints
  less than the stones' tops. Step 3's paving should sit closer to step 1's (the game's) than step 2's does:
  lay the three crops over each other.
- **The self-shadow:** in step 3, where the sun reaches the paving, thin dark lines along the joints on the
  side the sun comes from. Not in the game's picture (it has none), not in step 2.
- **The number:** in `LogOutput.log`, the last `Yardstick 10s` line of each step, the entry `"MI_Floor_01_HoS
  ..." ... (place, colour, normal, roughness)`. Run 20261009-202514 had (0, 3, 3, 7). In step 3: place still 0;
  colour and normal lower than in step 2. If they RISE, the shift goes the wrong way (a sign: the bitangent's
  side, or the tiling's): tell me which.
- **How strong:** mild. The field is 0.56 % of a repeat of the texture deep: about a centimetre if the texture
  repeats every 2 m. If the eye sees no difference at all, compare the yardstick's numbers before calling it
  dead.
- **The log:** the three `Relief ...` lines above.

**Cost I expect:** under 0.3 ms a frame in this scene at 4K with the trial at 1 (4.5 % of the pixels walk some
20 texture reads in and as many out; rays that meet the floor after a bounce the same). With the trial at 0 but
compiled in: one comparison a hit, plus what the code's size costs the big passes, which AGENTS.md says must be
measured, not guessed: read the passes' times of step 2 against a build without `TGPT_RELIEF`. With `TGPT_RELIEF`
OFF: nothing, no pass holds a line of it.

## 5. What is not built, and what each other kind needs

| what | needs |
|---|---|
| The lamps' shadow rays, bounces and reflections leaving through the field | `relief_lit` (and its sister that returns the second crossing) in `transmittance_from_surface` and `trace_from_surface`; the crossing carried in `Hit` (two coordinates, a height, the field's number) in place of the value left behind |
| The hit's place at the crossing | `Hit.position` moved along the ray in `fetch_hit` or at the top of `hit_material`'s callers; every leaving ray then MUST walk out first (a ray started under the triangle would meet the triangle's own back) |
| The camera pixel's leaving rays | the pixel's record gains the crossing and the tangent (section 2: a third word, 33 MB at 4K) |
| HDRP's Lit with pixel displacement (5 materials: `StoneTileCDEP`, `StoneTileCDEP 1`, `Mat_WeaponHit_Ground`, `_Stone`, `_Wood`) | the same walk (`relief_march` as it is) with another field: the map is `_HeightMap`'s red (a texture the tracer does not open yet: a small program record for Lit, as the detail map has one). Its space, read from variant blob 176 (lines 185 to 255): x and y the base map's tiled coordinates; a world direction d is `(d.T x sx x a.x, d.B x sz x a.y, d.N x sy)` with `a = abs(_HeightAmplitude) x _InvTilingScale x _InvPrimScale.xy x _BaseColorMap_ST.xy` and sx, sy, sz the lengths of the world-to-object matrix's rows (with `_PIXEL_DISPLACEMENT_LOCK_OBJECT_SCALE`, which all five have; 1 without). The game then fades the shift out as the texture's mip passes `_PPDLodThreshold` (5): to keep or not is a choice (at that mip the field is under a pixel). Which scenes hold them: the new log line says |
| The exact walk at flat angles | a pyramid of the map's highest values per texel block (section 2) |
| The overlay's own shift | a choice: one surface (built) or the game's two |
| The castle walls and the 13 others with an unused height map | the owner's word (section 1). Then: the Lit kind above with HDRP's `_HeightAmplitude` |
| `Shader Graphs/ParallaxMapping` (2 decals) | the decals' job; its graph not read |
| `TG/VFX/VFX_Icicles` (13) | not read: see-through effect materials |
| A fixture, mutants, CHANGELOG | the game answers first (AGENTS.md) |

## To merge and build

- Changed against `base79` (`r1\changed.py`; each file keeps its own line endings): `CMakeLists.txt` (+9),
  `pt_materials.hlsli` (+114), `pt_common.hlsli` (+16), `pt_primary.hlsli` (+8), `pt_di_spatial.hlsl` (+8),
  `MaterialPrograms.cs` (+94, -2), `SceneCapture.cs` (+1), `Settings.cs` (+13), `tests/ingame/scenarios.json`
  (+6), `tests/source_checks.py` (+52), and this report (new). Nothing of the coordinator's four files.
- `python tools/agents/merge_copy.py relief1 --base=base79 --root=<scratchpad>`. The two workers in the shaders
  touch `pt_specular.hlsl`, `pt_gi.hlsl`, `pt_water_path.hlsli`, `tracer.cpp`, `scene.cpp`: none of mine.
  `pt_common.hlsli` is everyone's: my three blocks are at `HAIR_STRAND`, at the top of `hit_material` and at
  the sun in `direct_light`.
- The switch is OFF unless the tree is configured with `-DTGPT_RELIEF=ON`. With it OFF the merge changes no
  pass (every line is inside `#ifdef PT_RELIEF`; the source check counts the blocks) and only the plugin's three
  log lines appear. For the run: configure with it ON, then the usual build.
- **The plugin alone already answers part 1 of the job**: deploy it with any native side and the log line
  `Relief: N materials with a height in use ...` says what each scene holds.

## 6. Seen on the way

- **The castle walls are a fifth of this scene's screen** (`Mat_Castle_Wall_01`: 20 % by the yardstick's line of
  run 20261009-202514), the floor with a height in use 4.5 %. So the owner's choice in section 1 decides far
  more of the picture than the trial does.
- **The decals already have a parallax of their own** in the tracer (`DecalGraphCapture.cs`:
  `_Parallax_Amplitude_1`; `pt_common.hlsli` near line 2168). Whether `Shader Graphs/ParallaxMapping` is one of
  the graphs it reads, I did not check.

- **"Relief" names two things now.** PT-95's filter trial has the recipe `relief`, the setting `Reflection relief
  trial` and `RELIEF_LOBE` in `pt_atrous.hlsl`. Mine: recipe `parallax`, setting `Relief trial`, `PT_RELIEF`,
  `relief_*`. No symbol clashes (checked by name), but a reader may mix them: rename one when this becomes the rule.
- **`_TERRAIN_PARALLAX` in PT-28's entry** is not a height: those are HDRP/Lit materials with keywords left over
  from another shader. To correct in `docs/PT-REQUESTS.md` (the coordinator's file).
- **The game's variant with `_HEIGHTMAP` differs from the plain one in one line**: the pixel's position is
  read "centroid". Nothing to do with height; noted so nobody reads it as a use.

## Workarounds met (the owner, 2026-10-07: "remove all the hacks as we go")

| where | what | removed? |
|---|---|---|
| `pt_materials.hlsli` `frame_normal`: `if (!f.ok) return h.normal` | a mesh with no frame is shaded flat and nobody is told | not removed (every program leans on it). The relief does not fall into it silently: the plugin names the mesh |
| `pt_primary.hlsli` `write_traced`: the surface record keeps neither the triangle nor the texture coordinates | every later pass that needs the hit again cannot have it (glass already packs its own two words for that) | not mine; it is why the trial's sun shadow is walked where the pixel is written |
| `MaterialPrograms.cs`: `"; its parallax relief is not traced"` | the log's note of the gap | replaced by the trial's own line |

## Scripts (in `scratchpad\r1\`, read only on the game's files)

`height_census.py`, `height_mats.py` (the counts; `height_mats.txt`), `shader_any.py` (any shader of any data
file: keywords, passes, variants, unpack), `pblob.py` (a variant's parameters by name), `cmp_blobs.py` (are two
variants the same code), `find_shader2.py`, `peek.py`; `lit\`, `kf\` (the unpacked programs and the variants
read: Lit 176, 177, 179, 27, 28, 30, 8, 10; KF 253, 255, 258); `configure.cmd`, `shaders.log`.
