# The ground has no relief in the traced picture (PT-104, PT-108): worker "terrain-1", 2026-10-09 to 10-10

> Made the rule on 2026-10-10: see `rule-1.md`. The game answered +1 (run `20261010-092027`: the terrain's "another
> normal" 63 % flat, 4 % with +1, 54 % with -1). The terrain's frame is the object's x axis with the sign +1 in every
> build; the hidden `Advanced / Terrain frame`, its record word and its log line no longer exist; the recipe
> `terrain-frame` shows the rule. Still open, in `rule-1.md`: the sign for the 37 meshes with tangents of their own,
> and the other materials drawn flat (now counted in the log).

Copy `scratchpad\wt\terrain1` (base `wt\base78`). Scripts and their output: `scratchpad\t1\`. Nothing here ran in the
game. The four claims, kept apart: **built** the plugin yes, the shaders two passes as a check (below); **passes the
harness** no run; **seen in game** no; **the owner says it's fixed** no.

## In short

- **The cause (read from the game's own mesh files, not guessed).** The game's ground meshes hold positions and
  normals and nothing else: no texture coordinates, no tangents. The tracer makes the frame a normal map is turned
  by from the mesh's texture coordinates. With none, it has no frame, and `frame_normal` then hands back the flat
  normal without telling anyone. So every bump of the ground's eight layers was thrown away, for every kind of ray.
  That is the brief's suspect 4, in a stronger form: not the wrong frame, no frame.
- **What the game does there.** Its terrain shader turns the layers' bumps by the mesh's tangents. A mesh without
  tangents gets Unity's own stand-in tangent at every vertex: the object's x axis. So the game's ground has relief
  although its meshes bring nothing to turn it by.
- **The fix, on trial.** The tracer's terrain program makes its frame from the object's x axis too, as the game
  does. Behind the hidden `Advanced / Terrain frame`: 0 as before (flat), 1 or -1 the fix with that sign for the
  frame's second axis. Default 0 until the game has answered.
- **The one thing reading cannot settle: the sign.** I expect +1 (reasons below). The run has a step for each, and
  the yardstick's number picks.
- **The run:** `.\tools\harness\Run-PtHarness.ps1 -Scenario spec-terrain -Recipe terrain-frame`.

## State (kept current)

- 2026-10-09 23:45 started. Read the brief, the sheet, the run `20261009-202514`, the terrain's program, the game's
  compiled shader (`mw\terrain\blob55.txt`), the material's saved values.
- 23:50 first reading: the traced ground's normal is the flat one, and the frame is the suspect.
- 23:55 the cause, from the game's mesh files (section "The cause").
- 2026-10-10 00:05 the fix and the log lines written; the plugin builds (0 warnings, 0 errors); two shader passes
  compile from this copy; `python tests/source_checks.py` 108 of 108. Handed over. STOPPED here, as the brief says.

## The cause, with the evidence

1. **The picture.** The wet step of run `20261009-202514` shows one ROUND mirror image of the flame on the ground.
   Bumps as strong as this material's cannot show a round image. Only a flat ground can.
2. **The material** (`Mat_UT_B_01_c_CampaignMap_HOS`, its saved values from the game's files,
   `scratchpad\t1\mat_lookup.py`): all 26 textures are set, each layer has its normal map. The layers' normal
   strengths are 3, 4, 3.33, 2, 1.67, 2, 3 and 0.3. Those are very strong: a slope of 0.2 in the map becomes 31
   degrees. The yardstick calls a normal "another" from 25 degrees on, so a flat ground against the game's gives a
   big share: the 63 % fits. The log of the run says "0 of them not open when asked". So suspects 1 (maps missing)
   and 2 (strength 0) are out, as far as reading can tell; the new log line (below) makes the game say it.
3. **The meshes** (`scratchpad\t1\mesh_census.py`, every mesh of the game's bundles named `UT_`; 613 of them):

   | kind | how many | what a vertex holds |
   |---|---|---|
   | `Mesh_UT_<tile>_<map>_LOD0_<n>`: a tile cut in up to 16 pieces of 128 m | 576 | position, normal |
   | `UT_<tile>_LOD0_<n>`: a whole tile of 512 m | 37 | position, normal, tangent |

   None has texture coordinates. The 37 with tangents hold the same one at every vertex: (1, 0, 0, -1)
   (`scratchpad\t1\tangent_census.py`). This scene's tile, `UT_B_01_c`: 16 pieces without tangents, one whole
   tile with.
4. **The tracer's code.** `program_terrain` (`pt_materials.hlsli`) ended with `frame_normal(f, ...)`, and `f` is
   `hit_frame(h)`: tangent along the triangle's dP/du of the mesh's FIRST texture coordinates. `fetch_hit` leaves
   `dpdu` at 0 when the mesh has none (`mesh.uvs == INVALID`). `hit_frame` then says `ok = false`, and
   `frame_normal` returns `h.normal`. Nothing logs it.
5. **The game's code** (`blob55.txt` lines 79 to 92 and 339 to 346; standard HDRP around it). The frame is: T the
   mesh's tangent turned into the world and normalized; N the interpolated normal; B = cross(N, T) x the tangent's
   w x the object's handedness. The blended layer normal n is turned as n.x T - n.y B + n.z N (the graph negates
   y), then normalized. T is not made square to N. The terrain shader reads no texture coordinates at all (splat
   maps by the object's x and z, layers by the world's x and z), which is why its meshes can do without them.

Not the cause, by reading: suspect 3 (the decode `a x r, g` and the strength match lines 120 to 129), suspect 5
(the blend matches lines 252 to 316), suspect 6 (the game's pixel shader has no parallax and no detail normal; its
only other change to the normal is the decals' buffer, lines 347 to 363, and the tracer has its own decals).

## Why I expect +1, and why the run must still say it

- Unity gives a shader that wants tangents, on a mesh without them, a stand-in: (1, 0, 0) with w 1, as far as I
  know Unity. I could not find Unity's own page that says so (two web searches: only that `RecalculateTangents`
  writes (1, 0, 0, 1) on a mesh without normals or texture coordinates). So this is memory, not a source.
- It cannot be zero: a zero tangent would make the game's ground flat too, and it is not.
- The graph's own sign fits +1. With T = +x and w = +1, B is -z on flat ground. The graph negates y, so the map's
  green goes to +z, which is the way the layer's v runs. With w = -1 the green would run against the texture.
  A graph author flips y exactly when the picture looked inside out before.
- That same reasoning says the 37 whole-tile meshes, which carry w = -1, are shown by the game with their green
  turned over. Whether the game draws them at all, the new log line says.

## What is changed

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_materials.hlsli` | `program_terrain`: the record's value [17].y is the frame's rule. Not 0: T = the object's x axis in the world (normalized), N = `h.rounded`, B = cross(N, T) x that value x -1 in a mirrored object; the blended normal goes through `frame_normal` as before (y negated, kept on the ray's side). 0: as before. 9 lines; nothing is computed while it is 0. No new texture, no new field, no option bit |
| `src/TGPathTracing/MaterialPrograms.cs` | the terrain record's [17].y from the setting; a change of the setting rewrites that word in every terrain record already sent (`Flush`), so one run can step through it. Three log lines (below) |
| `src/TGPathTracing/Settings.cs` | `Advanced / Terrain frame`, hidden, 0 |
| `src/TGPathTracing/SceneCapture.cs` | three one-line hooks: a registered mesh is noted (has it texture coordinates, has it tangents), a released one forgotten, an instance with a terrain material names its mesh once |
| `tests/ingame/scenarios.json` | the recipe `terrain-frame` |

For every ray: the program runs in `hit_material`, which the camera's own ray, a mirror's, a bounce's and the walk
all call. A mirror shows the same ground.

No struct, export or ABI changes. A new plugin on an old native side changes nothing (the old shader does not read
the word); a new native side under an old plugin reads 0: as before.

### The log lines (the plugin alone: no shader compile needed to get them)

- `Terrain material "<name>": splat maps <n> and <n> over 512 m (clamped), height transition 0.1; its layers'
  color, normal and mask map (the tracer's numbers; -1: the material has none, or not open yet), metres a repeat
  and normal strength: 0: <n>, <n>, <n>, 12 m, 3; 1: ...` : once a material. A -1 in a normal map's place, or a
  strength of 0, would be suspect 1 or 2 after all.
- `Terrain material "<name>" is drawn with mesh "<mesh>" (<n> vertices), which brings no texture coordinates and
  no tangents: the game's shader turns its layers' bumps by the tangent Unity hands it for such a mesh (the
  object's x axis); the path tracer has no frame for them there: its ground is flat (Terrain frame 0).` : once a
  material and kind of mesh. This is the cause said by the game itself. It also says when one of the 37 meshes
  with tangents of their own is traced ("brings ... tangents of its own").
- `Terrain frame on trial: ...` : when the setting changes.

## The run

`.\tools\harness\Run-PtHarness.ps1 -Scenario spec-terrain -Recipe terrain-frame` (about two minutes of steps):

| step | what | read |
|---|---|---|
| `1-game` | the game's own picture | |
| `2-none-flat` | traced, no denoiser, Terrain frame 0, Rain wetness 0 | the yardstick: terrain normal 63 as before |
| `3-none-plus` | Terrain frame 1 | the yardstick; the picture |
| `4-none-minus` | Terrain frame -1 | the yardstick; the picture |
| `5-normals-plus` | Terrain frame 1, debug view Normals | the ground's bumps as colour (a flat ground is one colour) |
| `6-yardstick-plus` | debug view Yardstick | WHERE the normal still differs (near or far ground) |
| `7-none-plus-wet` | Terrain frame 1, Rain wetness 1 | the owner's picture: the fire's glints on wet stones |
| `8-off` | everything back | |

Read in `LogOutput.log`: for each step, the last `Yardstick 10s` line before the next step's marker, its entry
`"Mat_UT_B_01_c_CampaignMap_HOS ..." ... (place, colour, normal N, roughness)`.

**What it should show (predictions, nothing measured):**

- The terrain's "normal": 63 at step 2. With the right sign it falls a long way: I expect under 20, and it may be
  under 10. With the wrong sign it stays clearly higher (I guess 30 to 50). The lower of steps 3 and 4 is the game's
  sign. It will not reach 0: the yardstick compares pixel by pixel at 25 degrees, and maps this strong differ by
  that much between two samplers at a slightly different sharpness.
- The whole screen's "another normal": 18.7 % now, and nearly all of it is the terrain (26 % of the screen x 63 %
  is 16.4 % of the screen). It falls with the terrain's.
- The picture: the ground no longer a flat print. Stones and ruts shaded on one side, lit on the other, in steps
  3 and 5. In step 7 the flame's one round mirror image breaks up into many small glints over the bumps.
- If steps 3 and 4 both stay near 63: the theory is wrong. Then read the two `Terrain material` lines and step 5.

**With the plugin alone** (no native build): the same run gives the log lines, which prove the cause. Steps 2 to 4
are then one and the same picture.

## What to build

1. Merge: `python tools/agents/merge_copy.py terrain1 --base=base78 --root=<scratchpad>` (dry run: 5 files
   changed, this report new, 0 conflicts against main as of 00:05).
2. Native: `tools\Build-PtShaders.ps1`, then `cmake --build build\native` and the install. `pt_materials.hlsli` is
   part of `pt_common.hlsli`: every pass compiles again.
3. Plugin: `.\Build.ps1 -Runtime Mono -Mod TGPathTracing`; install with `tools\harness\Deploy-PtBuild.ps1`.
4. The run above.

## After the run

- **+1 wins (expected):** make it the rule the same day. Delete the setting and its plumbing in `MaterialPrograms`
  (`terrainFrame`, `terrainFrameWords`, the block in `Flush`), write 1 into the record. Keep the two
  `Terrain material` log lines. Then, if the log shows a mesh with "tangents of its own" traced: the sign by the
  mesh (design below).
- **-1 wins:** either Unity's stand-in has w -1, or this ground is drawn with one of the 37 meshes. The log's "is
  drawn with mesh" line says which. Same clean-up with -1.
- Then, not before: the offline fixture (a ground mesh without texture coordinates, a layer with a tilted normal
  map: the normal turns toward the object's x), its mutant, a source check, the CHANGELOG line.

## What is not done

| what | why | how |
|---|---|---|
| The sign by the mesh | the record holds one number a material; the 576 meshes without tangents and the 37 with (w -1) may need opposite signs | the plugin registers the tangent stream of a mesh that has tangents and no texture coordinates (`RegisterMesh` and the native side already can, for decal meshes: `TGPT_MeshDesc.tangentStream`, `MeshRecord.tangents`); `fetch_hit` reads it there into a new `Hit.tangent`; `program_terrain` takes it in place of the object's x axis and the record's sign. 16 bytes a vertex for those meshes (the largest has 9,806 vertices: 157 KB). Exact for any tangents, not only constant ones |
| Other materials on a mesh without texture coordinates | `hit_frame` still answers "no frame" and `frame_normal` the flat normal, silently, for every program and for HDRP Lit. Only the terrain is told now | the same `Hit.tangent`, used by `hit_frame`; and Unity's stand-in for a mesh that has neither |
| The mesh's own tangents for every other mesh | the tracer's frame is the texture's directions (`docs/agents/materials.md`, "The tangent frame"): the same thing for imported meshes, not proven equal everywhere | the tangent stream for all meshes: 16 bytes a vertex of every mesh. A choice with a cost, not taken |
| The interpolated normal's length | the game scales T and B by one over the interpolated normal's length; the tracer's N is unit length | under 1 % where neighbouring vertex normals are 10 degrees apart. Named, not built |
| A fixture, a source check, CHANGELOG | the game answers first (AGENTS.md) | after the run |
| The whole shader build | "only if needed": the coordinator's merged build compiles all 43 passes anyway | I compiled two passes from this copy with the main build's own command lines, one dxc at a time, no other dxc running: `pt_yardstick` (0.6 s; it includes the file) and `pt_compose` (9 s; it calls `hit_material`, so the new lines are in its code). Both exit 0 with warnings as errors |

## Seen on the way, not this job's

- **The fire's light on the ground.** In the traced picture the brazier lights two patches of ground. In the game
  it lights the ground all around. Relief shows where light grazes it. If the ground still looks dull away from
  those two patches after this fix, the next thing to look at is how much of the brazier's light reaches the
  ground, not the terrain.
- **The yardstick's colour test is loose for dark ground**: "another colour" means more than 0.1 apart in a
  channel, linear. Seven of this ground's eight layers are tinted 0.2 to 0.55, so "colour 0" does not prove the
  layers blend exactly as the game's.
- **Line endings differ by file in the copies** (`pt_materials.hlsli`, `Settings.cs`, `scenarios.json` are LF here,
  `MaterialPrograms.cs` and `SceneCapture.cs` CRLF). I kept each file's own.

## Workarounds met (the owner, 2026-10-07: "remove all the hacks as we go")

| where | what | removed? |
|---|---|---|
| `pt_materials.hlsli` `frame_normal`: `if (!f.ok) return h.normal` | a mesh with no frame is shaded flat and nobody is told | not removed: every program leans on it. The terrain no longer falls into it when the setting is not 0; the log now names the case for the terrain |
| `pt_materials.hlsli` `hit_frame` | the frame from the texture's directions stands in for the mesh's own tangents | not removed (table above) |
| `pt_primary.hlsli` `write_traced`: `all(traced.normal == hit.normal) ? hit.rounded : traced.normal` | guesses "no normal map" from the normal being the triangle's own | not mine; it is why the camera's flat ground at least looked smooth |

## Scripts (in `scratchpad\t1\`, read only on the game's files)

`mat_lookup.py` (a material's saved values from `mw\census.json`), `terrain_mesh.py` (a bundle's meshes: channels,
tangents, texture coordinates), `mesh_census.py` and `ut-meshes.txt` (all 613 ground meshes), `tangent_census.py`
(the 37 with tangents), `pass_cmd.py` (the dxc command the main build uses for one pass).
