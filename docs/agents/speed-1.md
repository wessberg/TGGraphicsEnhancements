# Speed 1: where the bounced light's 73 ms and the reflections' 26 ms go (PT-08, PT-99)

State at 2026-10-09 15:20: **handed over at the coordinator's word, before any offline run.** The worker has stopped;
nothing of its builds or tests is running.

Read these four things first.

1. **Found in the game's own logs: about 30 ms of the bounced light came in one build, with a switch that is off**
   (the lamp trial `Lamp trial lit surroundings`, PT-85; section 1). The trial is now compiled out by default
   (CMake's `TGPT_LAMP_AROUND`, OFF). **Not confirmed yet**: neither offline (no run) nor in the game with the trial out.
2. **No GPU test of this job has run to its end. Not one offline number exists.** The one start (14:54 to 15:04, the
   first measuring build) was stopped by the coordinator at 33 GB of memory before its first frame. The tables the
   brief asks for (the bounced light's parts, the reflections') are NOT made; the "same picture" check of the one
   switch built has NOT run. What is here instead: the game's logs, the code read, and the tools to make the tables.
3. **The 33 GB: not found, and possibly a defect of the mod, not of the test.** What is known: the test's own mode
   never ran (the run was still waiting for the tracer's passes: its output ends at "the passes are made on a thread
   of their own"), and no frame was traced. The tracer's log of that run shows the driver still making the two
   reordering passes (`giSer`, `specularSer`) when it was stopped at 9.5 minutes; every other pass was made by then.
   So the memory grew inside the tracer's start-up (`build_passes`: the driver compiling those two passes), not in
   anything a frame allocates. What is NOT known: whether a start of today's ordinary build on passes the driver has
   not made before grows the same way (in the game such a start took 628 s today, section 9), or whether this job's
   measuring build made it worse (it had a branch around Shader Execution Reordering's call in those two passes; that
   branch is taken out again). One watched start of a new build settles it; section 9 says how.
4. **The native DLL in its hand-over state was not compiled.** The compile waited behind the game and other workers'
   compiles and was stopped so as not to hold up the coordinator's own build. What was compiled, and why the rest is
   expected to compile, is in section 8, exactly.

- Copy: `scratchpad\wt\speed1` (base `wt\base69`). Build tree: `%TEMP%\tgb\speed1` (NMake, NRD off; half built:
  12 of 40 headers). Scripts and raw outputs: `scratchpad\speed1-work\` (`out\` has the game-log tables).
- Source checks: 105 pass. The plugin compiles (0 warnings). The GPU test's exe compiles.

## 1. What the game's own logs say

`scratchpad\speed1-work\history.py` reads the main tree's harness runs (read only): for one scene, the stage times of
every build of the tracer that ran it (`TGPathTracing.Native.log`'s "Path tracing 10s" lines of the run's own session,
1924 x 1084, one ray a pixel; the DLL by `build.json`). The tavern (`tavern-room`: 5,331 instances, 1.53 M triangles,
514 lights, no plants, Shader Execution Reordering on), medians:

| The tracer's DLL written | hash | log lines | bounced light | reflections | direct light | prepare |
|---|---|---|---|---|---|---|
| 2026-10-08 19:14 | 9D124B3B0B30 | 7 | 30.9 ms | 11.4 | 4.4 | 2.4 |
| 2026-10-08 20:50 | E1818BE069FE | 7 | 31.3 | 14.4 | 7.3 | 2.4 |
| 2026-10-08 21:05 | 280AAC70ECAC | 21 | 33.2 | 11.9 | 4.4 | 2.4 |
| 2026-10-09 00:23 | 497C642DB260 | 22 | 31.5 | 11.4 | 4.4 | 2.4 |
| 2026-10-09 01:20 | 3BA39B40CA96 | 21 | 29.4 | 10.9 | 4.5 | 2.4 |
| 2026-10-09 04:50 | 7F19186600E1 | 44 | **61.9** | 11.5 | 4.5 | 2.4 |
| 2026-10-09 06:58 | 9D4B6DA1B9D6 | 29 | 68.6 | 12.8 | 4.7 | 2.5 |
| 2026-10-09 07:30 | 609143540829 | 27 | 60.8 | 11.3 | 4.7 | 2.4 |

(`out\game-history-tavern.md` has the builds of 10-08 before these too: 34.9 to 41.8 ms, and one build at 86.0 ms
whose reflections, direct light and prepare were two to three times the others' as well: not looked into.)

- Every other stage is the same before and after, to tenths of a millisecond; the scene, its lights, its micromaps
  and the reordering are the same (the log lines' own counts).
- What the 04:50 build added (`docs/rounds/round-06.md`, "STATE AT 05:10"): `water8`, and "`near`'s two trial
  switches (both 0)": PT-85's hidden `Lamp trial lit surroundings` (`pt_common` `lamp_around`,
  `docs/agents/flame-near.md`) and the highlights' shoulder (the composite's).
- `water8` alone was in the game one build earlier (03:48, `6684C89C38AC`), at the beach only: its bounced light there
  was 27.2 ms, less than the builds before it (32.4 and 36.0 ms). So the step is not `water8`'s. The tavern itself was
  not run on that build: that is the one gap in this reading.
- The step is there with the trial's setting at 0: the last line of that build's first run was traced with "lit
  surroundings found on purpose within 0 cm (0: off)" and has 60.9 ms. (Which of the 44 lines had the setting at 30
  was not sorted out; their median is the 61.9.) The brief's own reference run (`metal`, 10:33) has 70.1 ms.
- Outdoors, between the last run before and the first after (no run in between): the campfire scene 40.4 ms (10-08
  12:44) to 59.8 to 69.3 ms; the night scene 40.9 to 54.2 to 77.0 ms; the forest was 36.4 to 37.8 ms on 10-08 evening.
  (`out\game-history-*.md`.)

What the trial added to the pass (compiled size as a header, from the workers' build trees of that night and today):

| | `pt_gi_dxr12.h` | `pt_gi_ser.h` | `pt_specular_dxr12.h` |
|---|---|---|---|
| `water8-build`, 10-09 01:09 (before the trial) | 32.3 MB | 32.6 MB | 45.4 MB |
| `near`, 10-09 04:35 (the trial in) | 50.9 MB | 51.3 MB | 58.1 MB |
| today's trees (`glass3`, `wax3`, `normals`) | 51.2 to 51.7 MB | 51.6 to 52.1 MB | 58.4 to 59.1 MB |
| this job's second measuring build, the trial compiled out, 14:51 | 38.1 MB | (not compiled) | (compiled, size not noted) |

The trial calls `lamp_around_light` in three places of the bounced light's pass (`pt_gi` before its rays, `shade_path`
at every bounce, `cache_sample`) and one of the reflections' (`pt_specular`). HLSL has no calls: each is a whole path
vertex inlined (a ray from the lamp, the material of what it meets with every program and decal graph, a subsurface
walk, a shadow ray, two more rays back). And it put one more ray (`near_first`) into every copy of `direct_light`.
With the setting at 0 none of it runs. The compile of `pt_gi_dxr12` took 664 s with it and 359 s without (this job's
two builds, the same PC and hour).

**Why a pass is slower for code it never runs is not shown here.** Two things point the same way: the same tavern went
from 11 to 38 ms on 2026-10-07 when the pass tripled in size (`docs/agents/speed.md`), and the bounced light's time is
nearly the same in scenes that have little in common (61 to 77 ms in the tavern, the town, the campfire, the night).
Both say the pass's own size, not the scene, sets much of its time. The reflections' pass grew by 28 % in the same
build and did not get slower, so "bigger is slower" is not the whole rule either. Section 5, item 6 has what would tell.

## 2. What one pixel's bounced light does today (read from the code, one ray a pixel, one bounce)

`pt_gi.hlsl` `gi_pixel`, then `pt_common.hlsli` `shade_path`:

1. The pixel's surface from the tracer's own G-buffer (`load_surface`).
2. One ray, cosine-distributed (`trace_from_surface`): the nearest hit, with the alpha test of every triangle it crosses
   in an instance that is not forced opaque (`alpha_opaque`: the instance's and the material's record; for a cut-out
   three index and three texture-coordinate loads and one read of the game's own texture at its top mip). Plants in
   the wind's pose are traced through their group's acceleration structure, which has no micromaps
   (`docs/agents/speed-2.md` section 7).
3. With Shader Execution Reordering: the threads sorted by the material the ray met (`PT_REORDER`).
4. The vertex the ray met (`shade_path`, one turn of its loop):
   - its material (`hit_material`: 3 to 24 reads of the game's textures by its shader, the wet ground's store near
     water, and every decal box the point or the ray crosses: `apply_decals`);
   - glass: the walk through it (`dielectric_follow`); water: the water's own path (`water_path`);
   - its emission (`path_emission`);
   - a subsurface material: a random walk inside it, one ray a flight (`subsurface_vertex`);
   - its direct light (`direct_light`): the sun's shadow ray, then up to 9 lamps weighed (8 of the cell's list, 1 of the
     far list; a cluster of glowing triangles picks one of its triangles) and one shadow ray to the winner. Each
     shadow ray nothing stopped then asks which water its start lies in (`water_through` to `water_medium`: one more
     ray, straight up to infinity, whenever the scene holds any water), and walks the glass, cloth or leaves it counted;
   - the radiance cache's light at the vertex (`cache_light`).
5. In a quarter of the 8x8 pixel blocks each frame (`cache_feeds`): the cache's own ray from that vertex
   (`cache_sample`): a second ray, a second material, a second direct light with its two shadow rays, a second cache
   read; then two writes to the cache (`cache_add`).
6. RTXDI's temporal reuse of the pixel's reservoir (no ray of its own unless the Crevice Light row is on:
   `tracer.cpp` `giBias`).

So a pixel costs about 3 rays (the bounce ray, two shadow rays), up to 2 more "which water" rays, a quarter of 3 (and
2) more for the cache, and what its vertex's material, decals, glass, water or subsurface walk add.

The reflections' pass (`pt_specular.hlsl`, then `shade_seen`) is the same shape: one ray from the lobe (every pixel,
with the Reflections row at "All Surfaces", the default; "Glossy Surfaces" takes the cache's light for a roughness over
0.6 and traces nothing there), the material of what it met, its direct light with a highlight (the same two shadow
rays and their two "which water" rays), two cache reads, the cache's ray in a quarter of the blocks, and for a smooth
metal one more reflection ray and a path vertex behind it.

How large the passes are: `hit_material` (with every material program and decal graph) is inlined at each of its call
sites: `shade_path`, `cache_sample`, each copy of `dielectric_follow`, of `subsurface_walk`, of `water_path`, and
(with the trial in) each of the three `lamp_around_light`. Ten or more copies in the bounced light's pass. Its compiled
code is 6 to 9 MB (a header of 38 to 58 MB).

## 3. The measuring build (made, never run)

CMake's `-DTGPT_SPEED_MEASURE=ON` (OFF by default; never for a build that is deployed) compiles 11 switches into the
passes (`pt_common.hlsli`, `PT_SPEED_MEASURE`). Each leaves one part of the work out, and the light with it; the
stages' GPU times with and without it say what that part costs. They are bits 4 to 14 of the same hidden number as the
switch for players (section 6), so the game can set them too, with a measuring build deployed:

| Value | Leaves out | Where |
|---|---|---|
| 16 | the pass's own ray (bounced light, reflections): the pass's floor | `pt_gi`, `pt_specular` |
| 32 | the shading of what the ray met: the ray alone | `pt_gi`, `pt_specular` |
| 64 | the material's textures, the game's shader for it, the wet ground, decals | `hit_material` |
| 128 | decals | `apply_decals` |
| 256 | the sun at path vertices (its shadow ray) | `direct_light` |
| 512 | the lamps at path vertices (their weighing and the shadow ray) | `direct_light` |
| 1024 | the lamps' shadow ray alone (they are still weighed) | `direct_light`, `transmittance` |
| 2048 | the radiance cache's feed (its ray and what it shades) | `cache_feeds` |
| 4096 | the cache's reads | `cache_light` |
| 8192 | the "which water" ray | `water_medium` |
| 16384 | the walk through glass and the water's own path | `hit_is_glass`, `shade_path` |

In a build without `TGPT_SPEED_MEASURE` none of these lines exists: every one lies between `#ifdef PT_SPEED_MEASURE`
and its `#else` or `#endif` (a source check holds that), and the preprocessed passes of the hand-over state are the
base's plus 8 lines (section 8).

The offline scene (`tests/pathtracing_speed1_checks.inl`, `TGPT_TEST_SPEED1_ONLY=1`, 1920 x 1080): a town by day. A
ground of the game's terrain shader, 80 buildings of its architecture, triplanar and HDRP Lit shaders (read from
shared textures as in the game), crates, 40 standing figures (half a subsurface material), glass, 48 glowing cubes,
8,000 tufts of grass and 250 trees whose blades and leaves are cut-outs read from a shared texture and stand in the
wind's pose, 510 lamps, 41 decals, a sea with its bed. `TGPT_TEST_SPEED1_TABLE=1` adds the table: each switch, options,
the rows Reflections, Bounces and Rays Per Pixel, and the scene with parts left out (plants standing still with and
without micromaps, no plants, no sea, 8 lamps, no decals, no walkers, and every solid instance flagged as holding a
cut-out). **It compiles; it has never traced a frame.** Expect its first run to need fixing. It ends itself over 8 GB
of its own memory (`TGPT_TEST_MEMORY_CEILING_MB`; a thread of the test looks twice a second).
What the scene is NOT: the game's town. The table would say what each part costs per ray on this GPU; how the game's
73 ms split is the game's to say, and the measuring switches work there too.

## 4. Where the time goes (tables)

**Not made.** No offline run reached a frame. What exists is section 1 (the game's stages by build) and
`out\game-stages-20261009.md`, `out\game-stages-20261008.md` (every run's stages by scene state).

## 5. What can go without changing the picture

Ordered by what is known of each. Only the first has a number, and that number is the game's, not a test's.

| | What | Saves | Known how | Work |
|---|---|---|---|---|
| 1 | The lamp trial's code out of the passes (`TGPT_LAMP_AROUND` OFF, built) | about 30 ms of the bounced light (tavern 61.9 to about 30; outdoors 20 to 35) | the game's logs before and after the build that brought it in; NOT yet run with it out | none: one game run to confirm |
| 2 | "Which water" asked only under the water's top (`Speed trial` 1, built) | not measured: up to 2 rays a path vertex and 2 a camera pixel, each straight up through the scene | read from the code | none: one game run |
| 3 | Opacity micromaps for the plants in the wind's pose | not measured | the town has 11,303 plant instances (6.7 M triangles) in groups, none with micromaps; 1,975 other instances have them | hours (`docs/agents/speed-2.md` section 7) |
| 4 | Plain submeshes of an instance that holds one cut-out made opaque | not measured; the log now counts the triangles | such an instance is traced with EVERY triangle handed to the shader's alpha test (`scene.cpp`: `FORCE_NON_OPAQUE`): a building with one glass or ivy submesh, a character with hair | hours: a second acceleration structure a mesh, as micromaps already have |
| 5 | Shader Execution Reordering against plain compute passes | not measured | `Speed trial` 8 runs both passes as compute passes in the same run (needs `TGPT_TEST_PLAIN_PASSES` set at the start) | none to measure |
| 6 | Smaller passes: one call site for `hit_material` and `direct_light`, rare paths (water, glass, the walk) out of the hot pass | unknown, possibly the largest: section 1's two steps (11 to 38 ms, 30 to 62 ms) came with size alone | the game's logs | days; first the experiment below |

- Item 1, if the owner keeps the trial (PT-85 waits for the owner's word on it): it needs one call site, not three
  and one in every `direct_light`, before it comes back into the default build.
- Item 4's count: the native log's 10 s line now ends its cloth part with "tested by the shader: N instances, every
  triangle (T triangles, P of them of submeshes that are no cut-out)". P is what item 4 would spare the shader.
- Item 6's experiment (an afternoon, in the game): the measuring build's switches 16 and 32 give the pass's floor and
  its ray; then a build with one more piece compiled out at a time (the water's path, the glass walk, the subsurface
  walk: each one `#ifdef` like `PT_NO_LAMP_AROUND`), the tavern each time. If each piece buys milliseconds while it
  never ran in that scene, the passes should be split by what a scene holds.
- Not candidates (they are right as they are, read): the bounce ray's one query (`trace_from_surface` asks again only
  where it met something the wind moves), the radiance cache's read, decals' box walk where no mesh decal is.

## 6. What is built, behind which switch

**`TGPT_LAMP_AROUND` (CMake, OFF by default).** OFF: `pt_common` `lamp_around()` returns 0 at compile time, so every
`if (lamp_around() > 0.0)` and what hangs on it is compiled away in every pass; the hidden setting
`Lamp trial lit surroundings` then does nothing, and the tracer's log says so when it is set ("... but this build has
it compiled out"). With the setting at 0 the passes compute the same as before: the code removed is behind that test.
ON (`-DTGPT_LAMP_AROUND=ON`): the trial as it was. **The recipes `lamp-near`, `near-sweep` and `watch-near` do nothing
on a default build now.**

**Hidden plugin setting `Advanced` / `Speed trial`** (an integer, 0 by default: every ray as before). It travels in
`TGPT_Frame.prevCameraPosition`'s w, which no caller filled (no size or ABI change), to the frame flags' bits 16 to 31
(`FrameConstants.size.w`: bits 0 and 1 are the resets, bits 8 to 15 glass's trial), `pt_common` `speed_trial()`.
Bits 0 to 3 are for players' builds (1 and 8 taken; 2 and 4 free), bits 4 to 14 the measuring build's.

- **1: "which water" asked only where water can be.** Every shadow ray that nothing stopped (`transmittance`,
  `transmittance_from_surface`) asked `water_medium(origin)`: one more ray, straight up, to infinity, against the
  water's surface alone, whenever the scene holds any water. Its answer is used only if the point lies IN water. With
  the bit, a point above the height no water stands over answers "in the air" without the ray. That height
  (`pt_water_surface` `water_ceiling`): the highest point of any slot's water instances (`TGPT_WaterCaustics.top`,
  which the plugin takes from their meshes' bounds) plus how far the waves can lift the surface, the same reach
  `water_over` (the wet ground) looks down from. The plugin's height is not taken on trust: `scene_water.cpp` keeps
  the highest corner of every water surface it traces in a frame (`GpuScene::waterHighest`), and `tracer.cpp` hands
  the bit to the shaders only in a frame in which that lies at or under the plugin's highest point (else every point
  asks, as before, and the log says "NOT skipping the water question"). Why the picture is the same: the skipped ray
  could only have met water above the point. **Not run: neither its saving nor its picture is measured.**
- **8, for finding causes:** the bounced light's and the reflections' passes run as the plain compute passes they
  also are, without Shader Execution Reordering (`dispatch_pass`). Only where those were made: the tracer makes them
  beside the reordering ones only with the environment variable `TGPT_TEST_PLAIN_PASSES` set at its start (two more
  large passes to make). Without it the bit does nothing, and the log says so.

**A count in the log** (`scene.cpp` `count_tested`, the 10 s line): section 5, item 4.

## 7. What would change the picture (for a later "quality level" row only; none measured)

- Reflections: "Glossy Surfaces" (the row exists): no reflection ray above a roughness of 0.6, the cache's light
  there. A rough wall's faint reflection of a lamp goes. Probably most of the reflections' stage in a scene of rough
  stone and wood.
- The radiance cache fed by an eighth of the blocks, not a quarter: the cache follows a change of light half as fast.
- Fewer lamps weighed at a path vertex (4 of the cell's list, not 8): more noise in bounced light near many lamps.
- Rays at half resolution (the row exists as a plan): softer bounced light and reflections, a quarter of both stages.
- Bounces stays at 1 and Rays Per Pixel at 1: both already the least.

## 8. Files changed, what compiled, hand-over

Against `base69` (`scratchpad\speed1-work\changed.py`; every shader change is an addition, no line removed):

| File | Lines | What |
|---|---|---|
| `CMakeLists.txt` | +25 -3 | the options `TGPT_SPEED_MEASURE` (OFF) and `TGPT_LAMP_AROUND` (OFF), their defines on the three dxc commands of the ray passes, `TGPT_NO_LAMP_AROUND` for the DLL |
| `native/pathtracing/shaders/pt_common.hlsli` | +88 | `speed_trial`, the measuring switches (all inside `#ifdef PT_SPEED_MEASURE`), `lamp_around` compiled out under `PT_NO_LAMP_AROUND` |
| `native/pathtracing/shaders/pt_water_surface.hlsli` | +14 | `water_ceiling`, the early answer in `water_medium` |
| `native/pathtracing/shaders/pt_gi.hlsl`, `pt_specular.hlsl` | +7 each | the measuring switches 16 and 32 |
| `native/pathtracing/src/tracer.cpp` | +54 -3 | the speed trial's block, `plainPasses`, `TGPT_TEST_PLAIN_PASSES`, the log's count, the "compiled out" line |
| `native/pathtracing/src/scene.cpp`, `scene.hpp`, `scene_water.cpp` | +20, +7, +2 | `count_tested` and its three numbers, `waterHighest` |
| `native/pathtracing/include/tgpt_api.h` | +7 -1 | `prevCameraPosition`'s w described |
| `src/TGPathTracing/Settings.cs`, `HdrpPasses.cs` | +16, +3 | `Speed trial` |
| `tests/pathtracing_gpu_test.cpp` | +42 -1 | the memory ceiling, `speedTrial`, the mode's hook |
| `tests/pathtracing_speed1_checks.inl` | new | the mode (never run) |
| `tests/source_checks.py` | +81 | `test_path_tracing_speed_on_trial_default_off` |
| `tests/ingame/scenarios.json` | +8 | the recipe `speed` |
| `docs/agents/speed-1.md` | new | this report |

`tools/Build-PtShaders.ps1` differs from `base69` too, and is NOT this job's: it was replaced in the copy at 15:05 and
is byte for byte the main tree's.

**What compiled, exactly.**

- The plugin: `dotnet build` of `src\TGPathTracing` for net472, 15:10, 0 warnings, 0 errors
  (`scratchpad\speed1-work\plugin.ps1`; its `obj` folder was removed from the copy again).
- The GPU test's exe, in its hand-over state: 15:11.
- The first measuring build (measuring switches ON, lamp trial IN): all 40 passes, the DLL and the test linked, 14:44.
  It held the measuring switches in an earlier form (plain `if`s where `#ifdef` blocks stand now) and the branch
  around the reordering call that was taken out since.
- The second (measuring switches ON in their present form, lamp trial OUT): 8 passes compiled before it was stopped:
  `pt_gi`, `pt_specular` and `pt_prepare` each for Shader Model 6.6 and 6.9, `pt_walk` (6.6), `pt_gi_spatial` (6.9).
- **Not compiled: the hand-over state itself** (no measuring switches, lamp trial OUT): no pass, and not the DLL
  (`tracer.cpp`'s four "compiled out" lines have never been through the compiler). Not compiled with the lamp trial
  out: the two reordering passes, `pt_walk` (6.9), `pt_caustics`, `pt_compose`, `pt_di`, `pt_di_spatial` and the
  smaller ray passes.
- Why it is expected to compile: the preprocessor alone (`dxc -P`, no compile; `scratchpad\speed1-work\preprocess.ps1`,
  `compare.py`, `out\preprocessed-compare.txt`) over seven passes, as the hand-over state builds them against as
  `base69` builds them: the code handed to the compiler is the base's plus the same 8 lines in each (`speed_trial`,
  `SPEED_WATER_CEILING`, `water_ceiling`'s five, the early answer in `water_medium`), nothing removed; and with the
  lamp trial out, one line differs (`float lamp_around() { return 0.0; }`). Those 9 lines are in the builds above.
- Source checks: `python tests/source_checks.py`, 105 pass (`scratchpad\speed1-work\source-checks-7.txt`).

**The game's answer (the one run that matters now).** Scene `tavern-room`, recipe `speed`
(`tests/ingame/scenarios.json`): `.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room -Recipe speed`. Its steps:
the game's own picture; NRD as it is; `Speed trial` 1; no denoiser with 1; no denoiser with 0; off. Each traced step
lasts 25 s and more, so a "Path tracing 10s" line falls wholly inside it. Read in `TGPathTracing.Native.log`:

- step 2's "bounced light" against **61.9 ms** (today's builds) and 29.4 to 33.2 ms (the builds before 04:50). About
  30 says the lamp trial's code was the step. Near 60 says this reading is wrong, and the default should go back ON.
- step 3 against step 2: "direct light", "bounced light" and "reflections" with `Speed trial` 1 (the tavern's scene
  holds 762 pieces of water); the line "Speed on trial (1): ..." must not say "NOT skipping".
- steps 4 and 5: the same picture with and without the switch, no denoiser, 16 frames each.
- `metal` the same way against 70.1 ms, for the town.

**Not done.**

- Every offline number: the bounced light's table, the reflections', the savings' milliseconds, the offline A/B of
  the lamp trial (the second measuring build was made for it).
- The switch's "same picture" check (it is written; it has not run).
- The cause of the 33 GB (section 9).
- A compile of the hand-over state.

**Workarounds met** (none removed): every triangle of an instance with one cut-out handed to the shader
(`FORCE_NON_OPAQUE`, because a mesh's acceleration structure is shared by instances with other materials); the
"which water" ray after every shadow ray (the medium is not carried along the path outside the water's own
integrator); trial code compiled into every pass.

## 9. The run that was stopped at 33 GB

- What ran: `tgpt_gpu_test.exe` with `TGPT_TEST_SPEED1_ONLY=1` on the first measuring build
  (`%TEMP%\tgb\speed1-stage-m1-check`), from 14:54:42. The coordinator stopped it at 15:04: 33 GB of memory, growing
  5 GB a minute, the PC paging.
- Its output (`scratchpad\speed1-work\out\m1-check.txt`) ends at "the passes are made on a thread of their own": the
  test was in its start-up wait. The mode of this job had not begun; no scene of it existed; no frame was traced.
- The tracer's log of the run (`out\m1-check-native.log`): every pass made but two. `prepareDxr12` 117 s, `walkDxr12`
  217 s (both "driver compile/link included", a cache miss), and no line for `giSer` or `specularSer`, nor "Path
  tracing ready". So for 9.5 minutes the driver was making the two reordering passes, and the memory is what it held
  for that.
- The same two passes in the game today, on passes the driver had not made before (`TGPathTracing.Native.log` of the
  harness runs): the 04:50 build `specularSer` 234 s and `giSer` 296 s; the 08:37 build 428 s and **628 s** ("Path
  tracing passes made in 628.4 s"). So ten minutes for these two passes is today's ordinary start-up on new passes,
  not this run's own. The run's memory during those starts in the game was written down nowhere.
- What this job's build had that the game's did not: the measuring switches, among them a branch around the
  reordering call (`if (...) dx::MaybeReorderThread(...)`) in exactly those two passes. Whether that made the driver
  need more is not known. The branch is gone (the reordering call is one unconditional line again; a source check
  holds it). The tracer's bit 8 measures the same thing without touching the passes.
- How to settle it, with the PC safe: any start of a build whose reordering passes are new to the driver (the next
  merged build's first start), watched: the process's memory every few seconds until "Path tracing passes made".
  For the offline test: `TGPT_TEST_STARTUP_ONLY=1` with `TGPT_TEST_MEMORY_CEILING_MB=8192`: it ends itself at 8 GB.
  If an ordinary build passes 8 GB there, the growth is the tracer's start-up on today's passes: a player's first
  start after an update would take as long and as much, and that is a defect of the mod (PT-99; the passes' size,
  section 5, item 6, is its cause too).
- Since: the test ends itself over a ceiling (8 GB in this mode unless `TGPT_TEST_MEMORY_CEILING_MB` says otherwise);
  this job's compiles ran two at a time from 15:09 (none got a turn).
