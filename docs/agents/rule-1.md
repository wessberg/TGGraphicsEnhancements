# What the game answered becomes the rule (PT-99, PT-104): worker "rule-1", 2026-10-10

Copy `scratchpad\wt\rule1` (base `wt\base82`). Brief: `docs/agents/briefs/brief-rule-1.md`. Scripts:
`scratchpad\rule1-work\`. Build tree: `%TEMP%\tgb\rule1`. Nothing here ran in the game, nothing was deployed,
nothing committed, nothing written to the main tree or the game folder. No GPU test was run.

The four claims, kept apart: **built**: see "State when stopped"; **passes the harness**: no run (the
coordinator's); **seen in game**: the three trials were (run `20261010-092027`), the rule as built here was not;
**the owner says it's fixed**: no.

## State when stopped

- (kept current; the LAST line is the state)
- Started. Read the brief, `perf-1.md` perf2.1 to perf2.5, `terrain-1.md`, every place that names the switches.
- All edits done (list below). `python tests/source_checks.py`: 112 of 112 (the base: 111; one check became
  two). The plugin builds: 0 warnings, 0 errors (`%TEMP%\tgb\rule1-plugin`).
- dxc parsed and type-checked the three passes that follow paths, `pt_prepare` and `pt_compose`, in every
  variant the build makes and in two it does not (with `PT_REGROUP_WATER`; a measuring build): 19 of 19, no error.
- "No new behaviour" checked on the text the compiler sees (section "How I know nothing else changed").
- 10:06 to 10:30, ONE background job (`rule1-work\build.ps1`; logs `%TEMP%\tgb\rule1-*.log`): configured with
  `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON` (the cache holds no `TGPT_LAMP_TREE`, `TGPT_REGROUP_FEED` or
  `TGPT_WATER_FLIGHT` any more); **all 40 shader headers compiled, 0 failed** (`Build-PtShaders.ps1 -Throttle 3`,
  1,377 s; with NRD on there is one more, `pt_nrd_prep`, NOT compiled here: a worker builds with NRD off); **the DLL
  links** (`%TEMP%\tgb\rule1\TGPathTracing.Native.dll`, hash 72EFF76D2CC6, never deployed; the tracer's own files
  compile with no warning); the GPU test's program compiles and links (never run; its 4 warnings C4244 are in
  `tests/pathtracing_gpu_test.cpp`, which I did not change); **ctest `tgpt_lamp_tree` passes** (1.7 s; by hand
  with `/W4 /WX` too: 292,811 yeses checked, 21,645 by rounding alone, 0 failures: the numbers `perf2` had).
- **DONE, STOPPED as the brief says.** Final: source checks 112 of 112; the plugin 0 warnings, 0 errors; every
  pass compiles; the DLL links; `tgpt_lamp_tree` passes. Not done by design: no GPU test, no game. A dry merge
  against main (10:10): 20 files changed, 1 new (this report), 0 conflicts. NOT verified here: the NRD path
  (built with NRD off) and anything on a GPU: the coordinator's merged build and its two scenes are the test.

## What is now the rule, and what is out

| Trial | Was | Now |
|---|---|---|
| the lamps' search tree | CMake `TGPT_LAMP_TREE`, `Speed trial` 16 | THE RULE. `reflected_lamps` IS the walk through the tree, in every build. The old loop over every lamp is deleted. The tree is laid after the frame's lights every frame. |
| the feeders' key | CMake `TGPT_REGROUP_FEED`, `Speed trial` 32768 | THE RULE. `regroup_key` always carries "feeds the radiance cache" in the two reordering passes. No regrouping added. |
| the water's short rays | CMake `TGPT_WATER_FLIGHT`, `Speed trial` 32 | OUT. `water_flight_reach`, its define, its option, its text: deleted. A turn's ray asks for what is nearest however far, as before the trial. |
| the terrain's frame | hidden `Advanced / Terrain frame` 0, 1, -1 | +1 THE RULE. `program_terrain` always turns the layers' bumps by the object's x axis, sign +1. The setting, its word in the record, its log line: deleted. |

A default build now does what the base did with `Speed trial` 32784 and `Terrain frame` 1. Nothing else differs
in what is traced. Two log texts are new (below).

Not touched, as the brief says: `TGPT_REGROUP_WATER` (2 and 4), the measuring build and its second page, `Speed
trial` 1 and 8, the rain's wetness, Mega Geometry, the relief trial, the lamp trial that is compiled out, memory.

## What the coordinator's run must read

Merge, build (EVERY pass compiles again: `pt_common.hlsli` and `pt_materials.hlsli` changed), deploy. `Speed trial`
stays 0 and there is no `Terrain frame` any more. Two scenes:

1. **`water-day`**, no denoiser, the world frozen, 25 s a step (`-Scenario water-day@structures`, its step
   `2-frozen-none`; or `water-day@speed`, its step `5-none-as-now`). The recipe `speed2` is gone.
   - the frame: about **161 ms** (the run of 09:20 with the tree: 160.6 and 160.8; without: 190.9 to 193.7);
   - the stage "reflections": about **103 ms** (102.6, 102.7; without: 133.8 to 136.2);
   - the stage "bounced light": about 41 ms, as before (40.2 to 41.8 in every step).
   - the log: `Speed on trial: off (everything as before)`, and the tracer's 10 s line now ends its lights' part
     with `; the lamps' search tree N nodes)`: N must be above 0 where the scene has lamps (about twice their number).
2. **`spec-terrain`** (`-Scenario spec-terrain@terrain-frame`: the game, traced with no denoiser, the Normals
   view, the Yardstick view, traced with the rain's wetness, off).
   - the Yardstick 10 s line, step `2-none`: `"Mat_UT_B_01_c_CampaignMap_HOS ..." ... (place 0, colour 0, normal 4,
     roughness 98)`: **normal 4** (63 when flat, 54 with the other sign); the whole screen's "another normal"
     **2.4 %** (18.7 flat);
   - step `5-none-wet` (needs the native side built with `-DTGPT_RAIN_WETNESS=ON`): the terrain's roughness 1, the
     screen's "another roughness" 18.8 %;
   - the log, once a terrain material and kind of mesh: `Terrain material "..." is drawn with mesh "..." (n
     vertices), which brings no texture coordinates and no tangents: ...; the path tracer by the object's x axis
     too, with the sign +1.`
3. The tavern, if wanted (`tavern-room@structures`): the stage "reflections" about 9 to 10 ms (8.8 with all three
   trials, 9.9 with the feeders' key alone, 10.3 with the tree alone; 11.2 to 11.6 without).

If the beach's reflections read 134 again, the tree is not walked: look for the 10 s line's node count first.

## (a) The 37 ground meshes with tangents of their own: what they need

**They need the sign -1. It is not a line, so it is not built.**

- The game's rule (its terrain shader, `terrain-1.md` "The cause", 5): the bitangent is cross(normal, tangent)
  times the tangent's w, times -1 in a mirrored object. For a mesh without tangents Unity hands the shader
  (1, 0, 0) with w +1: the game's answer of this morning. The 37 meshes hold (1, 0, 0, -1) at every vertex (read
  from the game's files by `terrain-1`): the same axis, w -1. So their bitangent points the other way: sign -1.
- **The game draws them, and a lot.** Run `20261010-092027`, the plugin's own lines "is drawn with mesh", terrain
  materials by the kind of mesh they were on:

  | scene | on a mesh WITH tangents of its own | on a mesh without |
  |---|---|---|
  | tavern-room | 4 | 0 |
  | water-day | 11 | 3 |
  | metal | 10 | 4 |
  | spec-terrain | 10 | 4 |

  Among them the largest ground meshes there are (`UT_B_02_c_LOD0_5` 9,806 vertices, `UT_C_02_c_LOD0_0` 9,390).
- **What the tracer does to them now:** it turns their bumps by the object's x axis with +1, like the rest. By
  the rule above that is the wrong way along one axis (the map's green turned over). The number to expect there
  is the -1 step's on the other kind: "another normal" about 54 %, against 63 % flat and 4 % right. So where the
  ground on screen is one of those meshes, this morning's fix is likely only a small gain. (In the scenes
  measured, the ground on screen was the other kind.)
- **The game has NOT been asked about them.** The 4 % was measured on `Mesh_UT_B_01_c_CampaignMap_HOS_LOD0_0`, a
  mesh without tangents. No yardstick line of that run names ground on a mesh with tangents. So "-1" is the
  shader's rule plus the file census, not a measured number.
- **A sign per material cannot do it.** The same material is on either kind, by the place:
  `Mat_UT_B_01_c_CampaignMap_HOS` on `Mesh_UT_B_01_c_CampaignMap_HOS_LOD0_0` (no tangents) in spec-terrain and on
  `UT_B_01_c_LOD0_10` (tangents) in water-day. The terrain's record is one a material. The sign must come from
  the mesh.
- **What it takes** (the design is `terrain-1`'s, "What is not done", row 1; the parts exist for decal meshes):
  1. plugin, `SceneCapture.RegisterMesh`: hand over the tangent stream also for a mesh that has tangents and no
     texture coordinates (today only with `decalAttributes`); 16 bytes a vertex for those meshes (157 KB the
     largest);
  2. shader: at a terrain hit, where `mesh.tangents != INVALID`, read the triangle's tangent (as
     `pt_common.hlsli` does for decal meshes, near "mesh.tangents!=INVALID") and take its xyz for the axis and
     its w for the sign, in place of the object's x axis and +1;
  3. one run to ask the game: stand where the on-screen ground is a mesh with tangents (the log line says which)
     and read the yardstick's "normal" for it, before and after.
  About 20 lines in two files, and it changes what is traced: a trial with a game run, not a tidy job.
- **Said in the log now**, for every terrain material on such a mesh: `...which brings no texture coordinates and
  tangents of its own: the game's shader turns its layers' bumps by those tangents, which the path tracer does
  not read; the path tracer by the object's x axis with the sign +1, whatever those tangents hold (in the game's
  files they are (1, 0, 0, -1): the same axis and, by the game's rule, the other sign; the sign by the mesh is
  not built).`

## (b) Other materials on meshes without texture coordinates: the count's line

Nothing is changed for them: `frame_normal` still answers the flat normal where a mesh gives no frame. The plugin
now SAYS it (`MaterialPrograms.ScanEnds`), one line at the end of a scan that met such a material the log has not
named yet (so once a scene, and again only when the world brings a new one):

`N instances of M materials have a normal map and a mesh without texture coordinates: drawn flat (the path tracer
makes a normal map's frame of the mesh's texture coordinates, and has none there): "<material>" on mesh "<mesh>"
(n vertices), ..., and K more.`

- N: the instances the scan listed that hold one or more such materials. M: those materials. Up to 12 are named,
  each with one mesh.
- Counted: a material whose own normal map is open to the tracer, or whose program keeps its normal maps in its
  record (triplanar, skin); on a mesh the plugin noted as bringing no texture coordinates. Never the terrain.
- If a scene has none, there is no line. I do not know the number: it was never counted, and I ran no game.
- Its cost: one dictionary lookup an instance a scan (by reckoning under a millisecond for a town's 16,000
  instances; not measured).

## What is changed, file by file

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_common.hlsli` | `reflected_lamps` is the walk (the tree's function folded into it; `reflected_lamps_tree`, `SPEED_LAMP_TREE`, the `#ifdef`, the loop: gone). `regroup_key` lives here now, beside `PT_REORDER`, without the test of the `Speed trial`. The `speed_trial` comment says what the numbers still mean. A stray comment that sat above `reflected_lamps` and describes `cache_sample` is moved to `cache_sample` |
| `native/pathtracing/shaders/pt_water_path.hlsli` | `PT_REGROUP_FEED`'s and `PT_WATER_FLIGHT`'s blocks gone; `water_path`'s early draw of the flight is `PT_REGROUPS`' alone again (the water's regrouping trial: the same text for that build as before) |
| `native/pathtracing/shaders/pt_gi.hlsl`, `pt_specular.hlsl` | `PT_REORDER(regroup_key(...))` with no `#ifdef` |
| `native/pathtracing/shaders/pt_materials.hlsli` | `program_terrain`: the frame from the object's x axis, +1, always; its comment says what is still wrong (a) |
| `native/pathtracing/src/scene.cpp`, `scene.hpp`, `lamp_tree.hpp` | the tree is built and laid in every build; comments |
| `native/pathtracing/src/tracer.cpp` | the `Speed on trial` line: the three trials' texts and their "compiled out" lines gone; 16 to 32768 in a build for players now read "the numbers N do nothing in this build"; the 10 s line names the tree's nodes |
| `CMakeLists.txt` | options `TGPT_REGROUP_FEED`, `TGPT_LAMP_TREE`, `TGPT_WATER_FLIGHT` gone, with `PT_PATH_DEFINES` and `PT_PASS_DEFINES` (every pass has every pass's defines again); `tgpt_lamp_tree` is built in every tree |
| `src/TGPathTracing/Settings.cs` | `Terrain frame` gone; the `Speed trial`'s text |
| `src/TGPathTracing/MaterialPrograms.cs` | the terrain frame's plumbing gone (the record's value 17 holds the clamp alone); the mesh line's text; the count of what is drawn flat |
| `src/TGPathTracing/SceneCapture.cs` | `AddInstance` tells the count; the scan's start and end |
| `tests/source_checks.py` | `test_path_tracing_lamp_tree_on_trial` became `test_path_tracing_lamp_tree_is_the_rule` (no switch left anywhere, the loop gone, the tree always laid, the feeders' key, the short rays out, the recipes) and `test_path_tracing_terrain_frame_is_the_rule` (no switch, +1, the flat count). One line of the small lamps' check follows the walk |
| `tests/ingame/scenarios.json` | `speed2` deleted; `regroup` keeps 2, 4 and both (6); `terrain-frame` shows the rule (6 steps) |
| `tests/lamp_tree_test.cpp`, `tests/pathtracing_speed1_checks.inl` | comments only: the test's logic is as it was (it keeps the loop, as what the walk is held against) |
| `CHANGELOG.md` | four entries, with the game's numbers |
| `docs/agents/perf-1.md`, `terrain-1.md` | a line at their top |

No struct, export or ABI changes. The plugin and the native side do not depend on each other's new version.

## How I know nothing else changed

dxc preprocessed (`-P`, nothing compiled) each pass's source from the base WITH the two trials' defines
(`-D PT_REGROUP_FEED -D PT_LAMP_TREE`) and from the copy without them; the texts, less `#line` marks and blank
lines, were compared (`rule1-work\textdiff.py`). `pt_specular` and `pt_gi` as reordering passes, `pt_walk` both
ways, `pt_specular` with `PT_REGROUP_WATER` too:

- 5 hunks in `pt_walk`, 6 in the reordering passes, the same ones each time, and nothing else:
  `regroup_key` moved and without its test of the number; `program_terrain` without `if (v17.y != 0.0)` and with
  `-1.0 : 1.0` for `-v17.y : v17.y`; `reflected_lamps_tree` renamed to `reflected_lamps` with the two lines that
  fetch the light lists; the old loop gone; the old `regroup_key` gone.
- With `PT_REGROUP_WATER` the water's path is the same text as before: that trial is as it was.
- Against the base with `PT_WATER_FLIGHT` on as well, the difference is that trial's lines and nothing more.

So the copy's default passes are the base's trial passes with the number 32784 always "on" and the terrain's
value at 1, line for line.

## What else read the number 16 (the brief's question)

- `pt_common.hlsli`, a measuring build alone: 16 is `SPEED_M_NO_RAY` (the first page) and, with 32768,
  `SPEED_M2_NO_SUN` (the second page). Both are as they were. 32 there: `SPEED_M_NO_SHADE`, `SPEED_M2_NO_SUM`.
- `tracer.cpp`: only the log. A measuring build's line is unchanged. A build for players said "measuring
  switches" for 64 to 16384 and had the trials' texts for 16, 32 and 32768: now one text for 16 to 32768.
- `Settings.cs`: the hidden text. `scenarios.json`: `measure`, `measure2`, `measure-water` use 16, 32, 32784 and
  the like as measuring numbers: untouched.
- CMake refused `TGPT_SPEED_MEASURE` together with the trials (two meanings of one number). The tree and the
  feeders' key read no number now, so a measuring build simply has them too. One refusal is left: with
  `TGPT_REGROUP_WATER` (2 and 4).

## Things that want changing, written down, not done

1. **The sign by the mesh for the ground** ((a) above). The largest thing left of PT-104.
2. **A measuring build is no longer the same build as on 10-09.** It has the tree and the feeders' key too. Its
   step "no lamps' balls along the path" (`measure-water` 8, 33024) now times the tree's walk, not the loop.
   Numbers of the measuring run of 10-09 do not compare one to one.
3. **The tree still lies in the lights' own buffer** (`perf-1.md` perf2.2 asked for "its own buffer and
   constant" once it is the rule). Not done: it changes the frame constants' layout in every pass and in the
   tracer for no change in what runs. Today nothing reads the lights' buffer by its length (the count is
   `settings2.z`); a reader that did would take the tree's records for lights.
4. **A far lamp's mirror image is decided by rounding** (`perf-1.md` perf2.2): the tree reproduces the loop's
   answer, blots and all. The steady formula would change those pixels. A choice for the coordinator.
5. **The offline test does not look at the terrain's frame**: its terrain fixture's layers have no normal maps.
   `terrain-1` owed a fixture and its mutant "after the run". Still owed.
6. `MaterialPrograms.Registered` notes "brings texture coordinates" by the mesh having the attribute. A mesh whose
   coordinates are in a format the plugin cannot hand over (`RegisterMesh`'s `FormatOf` test) is traced without
   them and not counted as flat. Rare; named.
7. `AGENTS.md` says ctest has 17 tests: `tgpt_lamp_tree` is in every build now, one more. `README.md` line 182 says
   the passes regroup "by the material their rays hit": still true, the key holds one bit more. Both left alone.
8. `docs/agents/briefs/brief-memory-4.md`, `brief-perf-2.md` and `docs/agents/memory-4.md` (other workers')
   still give configure lines with the three options. CMake only warns of a `-D` it does not use.
9. The owner's config file keeps an orphan `Terrain frame` line (BepInEx keeps what it does not know). Harmless.

## Workarounds met (the owner, 2026-10-07: "remove all the hacks as we go")

| where | what | removed? |
|---|---|---|
| `pt_common.hlsli` `reflected_lamps`: the loop over every lamp | not a workaround, the slow way | removed (the job) |
| `pt_materials.hlsli` `frame_normal`: `if (!f.ok) return h.normal` | a mesh with no frame is shaded flat | not removed (the brief: do not change them); no longer silent: the count's line |
| `pt_materials.hlsli` `program_terrain`: one sign for every ground mesh | stands in for the mesh's own tangents | not removed ((a)); said in the code, the log and CHANGELOG |
| `pt_primary.hlsli` `write_traced`: guesses "no normal map" from the normal being the triangle's own | `terrain-1` named it | not mine, not touched |
