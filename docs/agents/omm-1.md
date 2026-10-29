# omm-1: opacity micromaps where the tracer still tests cut-outs in the shader (PT-110)

Worker copy: `wt/omm1` (base `wt/base84`). Build folder: `%TEMP%\tgb\omm1` (NMake, Release, `-DTGPT_WITH_NRD=OFF
-DTGPT_RAIN_WETNESS=ON -DTGPT_MEGA_GEOMETRY=ON`, the main tree's compiled shader headers copied in: nothing compiles
but C++). Scripts: `scratchpad\omm1-work\` (`configure.cmd`, `build.cmd`, `run-check.ps1`, `eol.ps1`).
Brief: `docs/agents/briefs/brief-omm-1.md`. No shader is changed by this job.

## State when stopped

2026-10-10 12:20. **The job is done as far as a worker can take it. Hand-over 3 is ready: the copy holds M0 (the
measure), M3 and M5 (the two small fixes) and M1 (plants' micromaps, behind the trial's number 32). The copy is
whole and can be merged as it is.** Earlier states are frozen too: `scratchpad\omm1-work\handover1\` (M0 alone),
`handover2\` (M0, M3, M5), `handover3\` (everything; the same as the copy).

- Built from the copy: native `TGPathTracing.Native.dll` SHA-256 `0A2F429E9E6C5833` (no NRD in it: Denoising None
  only), plugin `TGPathTracing.dll` `C4998E26E87EFAFC`, `tgpt_gpu_test.exe`; both DLLs are in `handover3\bin`.
  0 errors; the tracer and the plugin 0 warnings (the test's own source has 4 old ones, not mine). No shader
  compiled or changed. Line endings: every file as its base has them; the two new files are CRLF.
- Source checks: 113 pass (one new: `test_cutouts_are_counted_and_their_cost_can_be_bound`; six lines of older
  checks changed for M5, each with a note). `tests/harness_analyze_test.py` passes. `scenarios.json` parses.
- Tested offline, on Windows' own Direct3D 12 runtime, under the 10 GB ceiling, never the whole test:
  - `a1` `TGPT_TEST_MICROMAPS_ONLY`, M0's build: passed.
  - `a2`, `a3` `TGPT_TEST_MEGA_ONLY`, M0+M3+M5's build and the one with M1: passed both.
  - `a4` `TGPT_TEST_OMM_ONLY` (new, mine), the last build: **passed. M3 on against M3 off: 0 pixels differ** in
    the lit picture, the first hits' colour and the direct light, with a character and two trees refitted every
    frame. The control (cut-outs solid) differs in 316,494 pixels. The log's count: 36 cut-out triangles of
    plants, 2 of the character, 42 plain triangles spared as it ships, 42 handed to the shader with the trial's 16.
- **NOT tested anywhere, because no offline test of a worker can run a micromap**: M0's read-back of the bakes,
  M5's compaction of the micromap structures, and all of M1's own path (a plant group built and refitted with
  micromaps). The game is their first test. Run M0/M3/M5 first (`-Recipe micromaps`); M1 has a recipe of its own
  (`-Recipe micromaps-plants`) so that a fault in it costs no measurement.
- Nothing left half done. No test process of mine is running. Nothing was written outside the copy, the build
  folders `%TEMP%\tgb\omm1*` and `scratchpad\omm1-work`.

### For the coordinator: the order I would run things in

1. Merge the copy (or `handover2` first, if M1 should wait). Build, deploy.
2. `Run-PtHarness.ps1 -Scenario tavern-room -Recipe micromaps`, then `metal`, then `forest`. Read `PT cut-outs`
   and the stage times as the tables below say. This tests M0's read-back and M5's compaction in passing: look
   for `BLAS compaction: N structures with micromaps` and for no new error.
3. Only then `-Scenario forest -Recipe micromaps-plants` (M1). If step 3 or 5 ends the device, "What can go
   wrong" under hand-over 3 says what it means.
4. With the numbers: decide M2, M4, M7 (the table under "Written up, not built").

## Hand-over 1: M0, the measure

### What it is

1. **A count in the tracer's log, every 10 s**, one line that starts with `PT cut-outs:`. It says how many cut-out
   triangles this frame's traced world holds, how many of them trace with a micromap, and what the micromaps
   settle: the share of their pieces (256 a triangle) that are "bare" or "covered" (the graphics card decides
   those itself) against "unknown" (the shader's alpha test is asked). By kind: still meshes and the things the
   game moves; plants in the wind's pose; characters (with the hair's share); cloth in the wind's pose. And the
   triangles that are no cut-out at all but reach the shader's test because their instance is tested as a whole.
   Decals: a mesh decal has a small structure of its own that is searched whole by design; no alpha test there.
2. **A hidden setting, `Advanced / Micromaps trial`** (0 by default, never a row). A number each, added up:

   | Value | What it does for the step | The picture |
   |---|---|---|
   | 1 | the plants in the wind's pose: their cut-outs are solid | WRONG on purpose |
   | 2 | the characters: their cut-outs are solid (hair is whole cards) | WRONG on purpose |
   | 4 | every other cut-out is solid (still meshes, things the game moves, cloth in the wind's pose); where a mesh has a micromap its bare pieces stay bare, only its unknown pieces turn solid | WRONG on purpose |
   | 8 | no instance traces with its micromaps | the same, slower |
   | 16 | (since hand-over 2) the plain submeshes of characters and of plants in the wind go to the shader's test again: M3 undone | the same, slower |
   | 32 | (hand-over 3) ON TRIAL, not a measuring number: the plants in the wind's pose trace with their meshes' micromaps (M1) | meant to be the same, faster |
   | 64 | (hand-over 3) with 32, for finding a cause: such a group is built anew at every pose in place of refitted | the same, slow |

   "Solid" holds for shadows, bounced light and reflections. The camera's own rays still cut the leaves out: they
   make every triangle a candidate by a flag of the ray itself (`pt_primary.hlsli:226`), which outranks what an
   instance says. So the `prepare` stage's time does not move with the trial. That stage's own cost is the
   review's M8.

   These values are for measuring, one step of one run. They are never a default. The log says "A MEASURING STEP"
   when one is set, and in every `PT cut-outs` line while it is.

### The recipe (one run a scene, about 6 minutes each)

`.\tools\harness\Run-PtHarness.ps1 -Scenario metal -Recipe micromaps` (then `forest`, then `tavern-room`).
No denoiser. Steps (hand-over 2's numbering): 1 the game; 2 as it ships, frozen, 16 frames; 3 trial 16, frozen, 16
frames (M3 undone: the same picture as step 2, up to noise); then with the world running, 25 s each: 4 as it ships,
5 trial 1, 6 trial 2, 7 trial 4, 8 trial 7, 9 trial 8, 10 trial 16, 11 as it ships again; 12 off.

What each step bounds (read the `PT stages 10s` line of the step against step 4's, and against step 11's for drift):

| Step | Trial | Its stage times, less step 4's, are |
|---|---|---|
| 5 | 1 | the most M1 and M2 together can give (micromaps for the plants in the wind, and their covered pieces opaque) |
| 6 | 2 | the most M4 can give (micromaps for hair), now that M3 is in |
| 7 | 4 | the most M2 and M7 can give on still meshes (their unknown pieces), plus the still meshes with no map at all |
| 8 | 7 | every cut-out solid: NVIDIA's own check ("trace once with everything opaque"). The whole prize |
| 9 | 8 | what today's micromaps bring (this step is slower: the difference is their gain) |
| 10 | 16 | what M3 brought (this step is slower: the difference is its gain) |

The world must run in steps 4 to 11 (the recipe says `live`): plants are posed and refitted only while the wind's
clock moves.

### What the game must show (this part no offline test can)

- The line `PT cut-outs:` appears every 10 s, and after a scene has settled its kinds no longer say "not counted
  yet". If they say it for good, the read-back of the bakes does not work on this driver: tell me (or the next
  worker); the count of triangles and instances is right all the same.
- No new error in the tracer's log while a scene loads (the read-back adds two buffer copies to each frame that
  bakes micromaps, and one read-back buffer of 68 bytes a cut-out triangle, gone a few frames later).
- With the trial at 0 the picture is as before (step 2 against a run of the build before).

### How the count is made (so that its numbers can be trusted)

- Triangles and instances: counted on the CPU in `GpuScene::record`, as each instance of the frame gets its flags.
- A micromap's pieces by state: the bake's own output (64 bytes of pieces and one index a triangle), copied back
  once a mesh, counted on the CPU (`GpuScene::count_omm`). No shader is changed and nothing is counted in a pass.
  A triangle the bake gave a "special index" (all bare, or all covered) counts as 256 pieces of that state.
- "micromaps settle N % of their pieces": bare and covered pieces, over 256 times all cut-out triangles. A
  triangle with no micromap counts as unknown whole.
- "% of all being leaves', blades' and thin cloth's, whose covered pieces are left unknown on purpose": the
  unknown pieces of meshes with a cut-out material that lets light through. The bake turns every covered piece of
  such a material into "unknown" (`pt_omm_bake.hlsl:265`), so this share is what the review's M2 could settle;
  the unknown pieces that are left over are what its M7 (a finer level, the outline traced) is about. A mesh with
  one leaf material and one other cut-out is counted whole as leaves: an upper bound.
- A plant group's pieces are its members' meshes' pieces, once a member (the map is the mesh's).
- NOT counted: how many candidate hits a frame reach the shader's alpha test. That needs a counter in every big
  pass (`pt_gi`, `pt_specular`, the walk), which costs frame time even when switched off (AGENTS.md). The trial's
  steps answer the question in milliseconds instead.

### Files changed (the whole job)

`native/pathtracing/include/tgpt_api.h` (the flags' bits 6 to 13; no size or ABI change), `src/scene.hpp`,
`src/scene.cpp`, `src/tracer.cpp` (34 lines added, none changed: the trial read before `scene.record`, and one
`log` line after "PT structures"; nothing in the pass making); `src/TGPathTracing/Settings.cs`, `HdrpPasses.cs`,
`NativeTracer.cs`; `tests/ingame/scenarios.json` (recipes `micromaps`, `micromaps-plants`),
`tests/source_checks.py`, `tests/pathtracing_gpu_test.cpp` (11 lines: `ommTrial`, the new mode's include),
`tests/pathtracing_omm_checks.inl` (new), `docs/PT-HARNESS.md` (the two recipes), `docs/agents/omm-1.md`.
Not touched: any shader, `pt_gi.hlsl`, `pt_common.hlsli`, the coordinator's files, the other mods.

With RTX Mega Geometry's trial on (not the default), a change of the micromaps trial lets the partitioned top
level go and writes it whole again: its kept instances would hold the flags of the step before.

## Hand-over 2: M3 and M5, the two small fixes

### M3: a submesh that is no cut-out is no longer sent to the shader's test (characters, and plants in the wind)

- Before: one alpha-tested material (hair, torn cloth, the eyes) made the whole character "non-opaque". A ray's
  query then handed every triangle of the body, the armour and the clothes to the shader's alpha test, which read
  three records to answer "solid". The review counted 109,800 such triangles in a tavern's frame. The same held
  for a plant in the wind's pose: a tree's trunk and branches were tested with its leaves.
- Now: a character's structure, and a plant group's, are their own (no other instance shares them). So each
  submesh carries in the structure what its material is: solid, unless the material is alpha-tested. Only the
  cut-outs reach the shader.
- Why the picture cannot change. I read every loop over candidates in the shaders (`pt_common.hlsli`,
  `pt_compose.hlsl`, `pt_dielectric.hlsli`, `pt_water_foam.hlsli`, `pt_primary.hlsli`, `pt_probe.hlsl`):
  - The alpha test's first line answers "solid" for every material that is not alpha-tested
    (`pt_common.hlsli:1482`). The hardware now gives that same answer itself.
  - The walks that look only at candidates (glass, thin cloth, leaves: `glass_walk`, `cloth_walk`, `leaf_walk`,
    `optical_medium`, the caustics' two) take glass, alpha-tested thin cloth and alpha-tested leaves. Glass is
    always alpha-tested (`SceneCapture.cs:1440`), so none of the submeshes that turn solid was ever taken there.
  - The camera's own rays and the probe force every triangle to be a candidate by a ray flag, which outranks the
    structure's mark: nothing changes for them.
  - ONE exception, kept as before: a character or a plant group that holds a GLASS material stays non-opaque as
    a whole. A solved caustic's own segments pass every candidate of the glass thing's own instance, its solid
    parts too (`caustic_clear`, `pt_common.hlsli:3403`); they could not if those parts were solid in the structure.
- So it is behind nothing. For measuring what it brought there is the trial's number 16 (M3 undone), and the
  recipe's frozen pair (steps 2 and 3) shows the same picture with and without it in the game.
- A refit must name the marks of its build. So when a material's alpha test is switched on or off, a character's
  structure is built anew that frame, and a plant group whose material changes so is another group (the flag is
  in its members' keys).
- The log: `PT cut-outs` says how many plain triangles are spared ("solid in their own structures") and how many
  still go to the test ("no cut-out ... go to the shader's test"). The older `tested by the shader: N instances,
  every triangle` now counts only the instances still tested whole (still meshes without a micromap, swaying
  cloth, characters with glass).
- NOT done by M3: a still mesh without micromaps whose instance is alpha-tested still has every triangle tested.
  Its structure is shared by instances with other materials, so its marks cannot be one instance's. The count's
  "still meshes ... triangles that are no cut-out go to the shader's test" says how many those are.

### M5: no `ALLOW_DISABLE_OMMS`, and the micromap structures compacted

- Before: once a mesh had micromaps, EVERY instance of it used the structure with micromaps; one whose materials
  differ carried the "no micromaps" flag. For that flag every micromap structure was built with
  `ALLOW_DISABLE_OMMS`, of which DirectX Raytracing says it "may result in some reductions in traversal
  performance". And that structure stayed at its build's largest size for good.
- Now: an instance that goes without its mesh's micromaps takes the mesh's plain structure (the same triangles,
  tested the same way). That was already the rule under RTX Mega Geometry's partitioned top level; it is the rule
  everywhere now, for HDRP's water too. No structure is built with the flag and no instance carries it.
- And the micromap structure is built to be compacted, its compacted size is read back, and it is copied compacted
  some frames later, exactly as a mesh's plain structure is (those compact to about 38 %). The micromap ARRAY is
  left as it is (not compacted: I am not sure enough of that call to hand it over untested).
- A guard that matters in the game: mip streaming replaces textures, and the micromaps are then made anew. A new
  structure may be given the address of the old one. So the compaction names the very build it read its size from
  (`Mesh::ommSerial`), not only the address.
- The same picture: the same triangles, the same micromaps, the same alpha test for the rest.
- The trial's 8 ("no micromaps") and the offline test's own option now also lead to the plain structure.

### What the game must show for M5 (no offline test can)

- No new error while a scene loads. In the tracer's log, some frames after the micromaps are made:
  `BLAS compaction: N structures with micromaps, X -> Y bytes`.
- `PT structures 10s`: "held, MB: acceleration structures" lower than with the build before, in `forest`.
- The same picture in step 2 as a run of the build before.
- If a device removal ever points at the micromaps' compaction: the one line to take out is
  `ALLOW_COMPACTION` in `record_omm` (`k.blasInputs.Flags`); with `sized` false the rest is as before.

### Source checks changed for M5 (six lines of older checks, each with a note in place)

- Near line 1587 (the passes' check): the scene no longer holds `INSTANCE_FLAG_DISABLE_OMMS` nor
  `ALLOW_DISABLE_OMMS` (was: it must hold both).
- The Mega Geometry check: `const bool linked=m->omm==Mesh::Omm::Linked && omm;` twice (was: `(omm ||
  !megaTopLevel)`), no `waterLinked`, no `constexpr bool megaTopLevel=false;` (nothing outside that build reads
  it now), `mega_fresh(` four times (the fourth: a micromap structure, compacted).
- Two lines that other checks pin in `pose_plants` are kept to the letter (the scratch's choice and the refit's
  flag by `g.built`): M1's number 64 sets `g.built` back before them in place of changing them.

## Hand-over 3: M1, micromaps for the plants in the wind's pose (ON TRIAL, the trial's number 32)

### What it is, in plain words

- Until now no plant that sways in the wind had a micromap. The comment in the code said they are "made for a
  still mesh". In a town that is 6.4 million triangles, and every cut-out one of them was handed to the shader's
  alpha test by every ray that crossed it.
- But a micromap hangs on a triangle's texture coordinates, and the wind does not move those. DirectX Raytracing
  lets a structure with micromaps be refitted: the positions change, the links to the micromaps are passed again
  unchanged.
- So with the trial's 32: each swaying plant asks for its MESH's micromaps, exactly as a plant that stands still
  does (they are made once a mesh, not once a plant). The plants of a 16 m cell are one "group" with one structure
  of its own; each cut-out submesh in it now names its member mesh's micromap array, at the build and at every
  refit to the wind.
- The picture is meant to be the same: a micromap settles only what the alpha test would decide.
- With the trial at 0 (the default) nothing changes: no plant asks for micromaps and no group links any.

### What had to be built (`scene.cpp`, `scene.hpp`; no shader, no pass)

1. A swaying plant's turn at its mesh's micromaps (`plan_plants`: `omm_instance`, the same call a still instance
   makes). A plant mesh that only ever sways was never even asked for micromaps before.
2. The mesh keeps the bake's index buffer (one index a cut-out triangle: which micromap is its own). It was
   released right after the mesh's own build. A group's structure needs it at its build and at every refit.
   Kept only for meshes the wind moves (and for every mesh while the trial is set): 4 bytes a cut-out triangle,
   one small buffer a mesh.
3. One function describes a group's submeshes for the build and for every refit (`plant_geometry`): a refit must
   name exactly what its build named. This function also serves the default path (no micromaps): the same
   descriptions as before, field for field.
4. A group remembers which build of each member's micromaps it links (`PlantGroup::memberMaps`). Each frame the
   plan compares that with what the members can link now:
   - the same: the group serves as it is (as before);
   - a member's micromaps are GONE or were made anew (a texture the game's streaming replaced, another cutoff):
     the group is never traced again. It is made anew like a group whose members changed. This is what keeps a
     group from tracing with a micromap array that has been freed: the array goes when the GPU is past the frame
     that let it go, and that frame is the last one the group is refitted and traced in;
   - micromaps have only COME since: the group serves as it is until every member's are in and the frame has
     room, then it is made anew once, with them. No plant stands still meanwhile.
5. A new group waits up to 30 frames for micromaps that are on their way, so that it is built once and not twice.

### The recipe, one run a scene (about 3 minutes): `-Scenario forest -Recipe micromaps-plants`, then `metal`

A recipe of its own, so that a fault in M1 costs none of the measuring recipe's steps.

| Step | Trial | What it shows |
|---|---|---|
| 2 | 0, frozen, 16 frames | the picture as it ships |
| 3 | 32, frozen, 16 frames | the groups BUILT with micromaps: the same picture as step 2 (up to noise). Watch leaves and grass |
| 4 | 0, running | the stage times as it ships |
| 5 | 32, running | the groups REFITTED with micromaps every frame: stage times against steps 4 and 6 |
| 6 | 0, running | drift |

What to read:
- `PT cut-outs`: "plants in the wind's pose: N in I instances, M with a micromap (pieces: ... % bare ...)". M was
  always 0 before. With 32, M should come near N within a few seconds of the step's start.
- `Path tracing 10s`: "plants: ... instances waiting their turn" back at 0 after the groups are made anew.
- `PT stages 10s`: bounced light, reflections and direct light of step 5 against step 4.
- `PT structures 10s`: "plants ... ms" of step 5 against step 4 (what the refit with micromaps costs).

### What can go wrong, and what each failure means

- The device is removed in step 3 (frozen): the BUILD of a group with micromaps is at fault (the frozen world
  refits nothing).
- It is removed in step 5 only: the REFIT is at fault. Then set `Micromaps trial` to 96 for one step (32 + 64):
  the groups are then built anew at every pose in place of refitted. Slow, but if 96 runs where 32 does not, this
  driver does not take a refit with micromaps, and M1 needs another way (see "M1's limits").
- The picture of step 3 differs from step 2 in leaves or grass: tell me which scene; the first suspect is a
  group whose triangles are not in the member mesh's order (they are copied in order by `pt_sway.hlsl:160`).

### M1's limits, known (first version; say which matter after the game has answered)

1. **A leaf's covered pieces are still "unknown" (the review's M2).** The bake leaves every covered piece of a
   leaf, a blade or thin cloth unknown on purpose, because a light's ray passes a leaf by chance. So M1 alone
   settles only the BARE pieces of a leaf card. The `PT cut-outs` line now says how much that is: "% of all being
   leaves', blades' and thin cloth's, whose covered pieces are left unknown on purpose".
2. **When the game's streaming replaces a plant's texture**, that mesh's micromaps are made anew (as for still
   meshes today). Every group that links them is then made anew twice: once without (at once), once with them
   (when they are baked). A group that cannot be made anew in the same frame (24 groups and 1.5 million triangles
   a frame, by the clock) has its plants traced where they stand still until its turn comes: the sway of some
   plants stops for a few frames. Before M1 a texture swap cost a swaying plant nothing. If the game shows this
   as popping, the better way is known and not built: keep the old micromap array alive while a group links it,
   switch the group's micromaps off with the instance's flag for the frames in between (which needs
   `ALLOW_DISABLE_OMMS` on the groups' structures, the flag M5 took off the meshes'), and make the group anew in
   the background. About 60 lines.
3. **The kept index buffers** are one small buffer a mesh (at least 64 KB as the device counts it). If M1 becomes
   the rule they can go into the geometry arena.
4. **Swaying cloth's copies** (the review's M9) still have no micromaps. The same idea serves them: 20 lines in
   `record`'s cloth branch and `sway`.

### What an offline test can say about M1: nothing

Micromaps need DirectX Raytracing 1.2. A worker's offline test runs on Windows' own runtime, which has none; with
the Agility runtime the driver must make the two reordering passes, which took an earlier worker's test past 8 GB
in two minutes. So M1's own path (the build and the refit with micromaps) has never run anywhere. Everything else
of this job has (see "Offline checks").

One way to change that, NOT taken (it is the coordinator's and the owner's to allow): a test-only switch that
leaves the two reordering passes unmade, so that the test runs the plain Shader Model 6.9 compute passes with
micromaps under the memory ceiling. Three lines in `build_passes`. I did not add it: pass making is another
worker's, and a switch that turns reordering off, even for a test, is not mine to add.

## Written up, not built: M2, M4, M7, G3, and what M0's numbers say each is worth

The numbers come from the recipe `micromaps` (stage times of a step less step 4's) and from the `PT cut-outs`
line. I have no game run: the table says which number answers what.

| | What it is | Its worth is | Read |
|---|---|---|---|
| **M2** | a leaf's covered pieces opaque in the micromap; the chance a light's ray passes a leaf taken outside the traversal (one more query where a leaf was met) | step 5's saving LESS what M1 brings (recipe `micromaps-plants`, step 5), for plants; step 7's saving for still trees | `PT cut-outs`: "% of all being leaves', blades' and thin cloth's ...". If that share is most of the plants' pieces, M2 is the larger half of the prize and M1 alone will look small |
| **M4** | micromaps for hair: "bare where every texel is bare, unknown elsewhere"; a mesh refused only by its glass submesh | step 6's saving (characters' cut-outs solid). After M3 that step bounds M4 alone | `PT cut-outs`: "characters: N ... (H of hair)". Tavern: about 437,000 triangles |
| **M7** | a level per triangle (today 4 for all), the outline traced, shared maps | step 7's saving, less what M2 would take of it | `PT cut-outs`, still meshes: "% unknown" LESS the leaves' share. If that rest is under some 10 %, M7 is worth little |
| **G3** | characters' structures built for fast tracing, or made for the frame's pose (templates) | NOT bounded by any step here. The review's own two-line trial (characters built for tracing, anew every frame) is not built | `tavern-room`: "bounced light" and "reflections" with that trial against step 4 |

Notes for whoever builds them:

- **M2** changes `pt_omm_bake.hlsl:265` and four functions of `pt_common.hlsli` (`transmittance`, `occluded_any`,
  `occluded_from_surface_rays`, `rtxdi_visible`): shaders, the light's path through every tree. A week with its
  checks (the review). The mean must stay: a leaf's shadow at one half to the power of the leaves in the way.
- **M4**: the trap the review names holds: the camera's own walk reads hair's alpha from the strand atlas, not
  the alpha tile, and a bare piece can never be brought back. A piece is bare only if both readings say so.
  With M3 in, a character's structure already carries marks per submesh, and with M1 in, a refitted structure
  with micromaps is proven or not: M4 should wait for M1's answer.
- **M7** needs the bake changed and the byte offsets a triangle (`scene.cpp`, `record_omm`: the histogram has one
  entry today, one level).
- **G3**: if it is tried, `Mesh::skinOpaque` (M3) must go into the new build as it does now (`blas_inputs`).

## Offline checks (Windows' own Direct3D 12 runtime, 10 GB ceiling, never the whole test)

| Run | Mode | Build | Result |
|---|---|---|---|
| `a1` | `TGPT_TEST_MICROMAPS_ONLY` | M0 | passed (no micromaps on this runtime: every picture the same with the maps "on" and "off") |
| `a2` | `TGPT_TEST_MEGA_ONLY` | M0+M3+M5 | passed (a character refitted every frame; the `PT cut-outs` line in its log) |
| `a3` | `TGPT_TEST_MEGA_ONLY` | M0+M3+M5+M1 | passed |
| `a4` | `TGPT_TEST_OMM_ONLY` (new, mine) | all | passed: M3 on against off, 0 pixels differ in three pictures; the control 316,494; the log's count as expected (55 s, of which 42 s are the two windows of the log) |

Their outputs: `scratchpad\omm1-work\check-a1.out.txt` to `check-a4.out.txt` (and `.native.log`); `a4`'s pictures:
`%TEMP%\tgb\omm1-images-a4` (`omm-marked.png`, `omm-every-triangle-tested.png`, `omm-cut-outs-solid.png`).
The first run made the 46 passes of Windows' own runtime in 450 s; the later ones found them cached (about a minute a
run).

`TGPT_TEST_OMM_ONLY=1` (`tests/pathtracing_omm_checks.inl`) is M3's own check: a character with plain sides and an
invisible lid, and two trees in the wind's pose (plain trunks, one with invisible leaves, one with checkered
ones), refitted every frame. The same frames with the marks, without them (the trial's 16) and with them again:
the same pictures. A run with the cut-outs solid (the trial's 3) must differ. And the log's count must say 36
cut-out triangles of plants, 2 of the character, 42 plain triangles spared as it ships and none with the 16.
It also runs every plant group through `plant_geometry` on the default path (build and refit).

Scripts: `scratchpad\omm1-work\run-check.ps1 -Tag <t> -Mode <..._ONLY>` (stages, waits for the GPU gate by itself,
runs under the ceiling).

## The plan, in the brief's order

1. M0: the measure. Hand-over 1. DONE.
2. M3: characters' submeshes that are no cut-out get the "solid" mark in their own structure. DONE (hand-over 2).
3. M5: no `ALLOW_DISABLE_OMMS`; the map structures compacted. DONE (hand-over 2).
4. M1: micromaps for the plants in the wind's pose. DONE as a trial (hand-over 3), untested by anything but the
   compiler: the game is its first test.
5. Written up, not built: M2, M4, M7, G3. DONE (above).

## What I read in the code (before changing it)

- All 32 option bits of `TGPT_Frame.options` are taken. The Mega Geometry trial travels in `TGPT_Frame.flags` bits
  2 to 5. The micromaps trial takes the flags' bits 6 to 13 the same way: no size or ABI change.
- The bake (`pt_omm_bake.hlsl`) writes two buffers a mesh: 64 bytes a triangle (256 pieces, 2 bits each: 0 bare,
  1 covered, 3 unknown) and one index a triangle (-1: all bare, -2: all covered, else its own map). Both are read
  by the build and then released. So the pieces can be counted on the CPU from a read-back of those two buffers:
  no shader change.
- The camera's own rays force every triangle to be a candidate (`pt_primary.hlsli:226`, a ray flag, which
  outranks an instance's flag). So no value of the trial changes what the camera's rays do or cost: the trial
  bounds the other rays (shadows, bounces, reflections). The camera's rays are the review's M8.
- Decals: a mesh decal has a small structure of its own, searched whole with every triangle a candidate by design
  (`pt_common.hlsli:2344`). No alpha test there, no cut-outs: micromaps have no part in it.
- Windows' own Direct3D 12 runtime (the offline test's `TGPT_TEST_NO_AGILITY=1`) has no DirectX Raytracing 1.2:
  the tracer runs its Shader Model 6.6 passes there, with no micromaps (`tests/pathtracing_gpu_test.cpp:800`). With
  the Agility runtime the driver makes the two reordering passes, which took an earlier worker's test past 8 GB
  in two minutes (`docs/agents/megageo-1.md:60`). So a worker's offline check cannot run a micromap: the game is
  the test for everything on the micromaps' own path.
