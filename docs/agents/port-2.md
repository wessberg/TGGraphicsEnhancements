# port-2: subsurface scattering, the wet ground and the hero's head on the camera-ray tree (PT-58)

Agent report, 2026-10-07. Copy `<scratch>/wt/port2`, base `<scratch>/wt/base35` (the main tree at 17:03). Brief:
`<scratch>/brief-port-2.md`. The three jobs come from `<scratch>/wt/merge3` (its base `wt/base33`, its report
`docs/agents/merge-3.md`, copied here). Nothing ran in the game, nothing was deployed or committed. The four claims kept
apart: **built** yes; **passes the offline harness** see "Final runs"; **seen in game** no; **the owner says it's fixed** no.

State: see "Final runs" and "Log" at the end.

## In short

- **Merged:** all of merge-3 (the walk inside wax and skin, `pt_sss.hlsl` deleted; the hero's head rule; the wet ground's
  store and optics). 25 conflict blocks in 9 files, both sides kept each time (table below). The caustics agent's edits to
  the wave functions moved with them into `pt_waves.hlsli`.
- **At the camera ray's own hit (PT-59), working and checked offline:** the walk starts at the camera ray's hit, by the
  material the ray met; a leaf the camera's ray meets lets its material's own share through; the hit is made wet in
  `hit_material` (where every ray's hit is); the camera's ray passes the inside of the hero's first-person body at any
  distance (the old 30 cm rule was in the camera ray too: gone). Mirrors and bounces keep the head rule (`trace_from_surface`).
- **The owner's rule of 17:55** ("remove all the 'hacks' and workarounds"): the old route's version of each of these is
  removed, with its tests and docs (section "Workarounds"). On the hidden old route nothing walks now and leaves pass the
  grey half.
- **Does not work yet** (each a gap of the camera-ray route, none built here): skin's softened terminator and its cavity
  occlusion at a hit; a leaf's mirror reflectance by its profile (0.0023) at a hit (0.04 there); a leaf's thickness map
  (its share is one number a material); bounced light's further rays and rays through glass still meet the inside of
  the hero's head (`trace_closest`, the hang's place); reusing walk exits (ReSTIR SSS).
- **The GPU hang** did not come back in any run here (`g_hitInside` kept).
- **API:** `TGPT_SetDiffusionProfiles` and `TGPT_SetSkinTransmission` gone; FrameConstants 7280 B with the jobs, 6512 B
  after the clean-up. The ABI number is the coordinator's (main 0x00011F00, untouched here). Section "ABI".

## Tools (in `<scratch>/p2/`)

- `merge_port.py merge3 --base=base33 --into=port2 [--dry]`: `merge_eol.py`'s logic aimed at this copy (three texts as LF,
  `git merge-file --diff3`, each file's own line endings back, mixed files line by line). `resolve.py <file> n=choice ...`
  resolves conflict blocks by number. `eol.py`: every changed file's line endings against `base35`.
- `build.cmd <tag>`: `%TEMP%	gb\port2-build` (Ninja; only `TGPathTracing.Native` and `tgpt_gpu_test`), copies the DLL and
  the exe to `%TEMP%	gb\port2-inst-<tag>`.
- `gpu.ps1 -Tag <tag> [-Build] [-StopAtFail|-KillAtFail] [-UpToCaustics]`: waits for the game, the harness lock and the main
  tree's `-GpuCheck`, then runs the test; output `%TEMP%	gb\port2-test-<tag>.txt`. `-KillAtFail` ignores the test's one
  timing check (it fails whenever tests share the GPU).
- `mutate.py <name> apply|revert`, `mutants.ps1 -Names ...`: the mutants (below).
- Edit scripts, each hunk found exactly once or nothing written: `edit_tests*.py`, `edit_checks*.py`, `edit_clean.py`,
  `edit_docs*.py`, `edit_changelog.py`.

## The merge (merge3 against base33, into base35's tree)

18 files changed, 6 new (`pt_walk.hlsl`, `pt_waves.hlsli`, `pt_wet.hlsl`, three reports), `pt_sss.hlsl` deleted. Clean
merges: `PT-REQUESTS.md`, `tgpt_api.h`, `pt_prepare.hlsl`, `extension.cpp`, `HdrpPasses.cs`, `NativeTracer.cs`,
`SceneCapture.cs`; taken whole (main had not changed them): `pt_specular.hlsl`, `WaterCaustics.cs`.

| File | Conflict | Resolution |
|---|---|---|
| `CMakeLists.txt` x3 | main added `pt_primary.hlsli` to the shared headers, took `hair:pt_hair` out, added `yardstick:pt_yardstick` to the passes without rays; the jobs added `pt_waves.hlsli`, `walk:pt_walk wet:pt_wet`, took `sss:pt_sss` out | all of both, on the same three lines (nothing else in the file touched) |
| `pt_shaders.hpp.in` | `pt_yardstick.h` beside `pt_sss.h` -> `pt_walk.h` | `pt_yardstick.h`, `pt_walk.h` |
| `Settings.cs` | `OwnCameraRays` and `WetGround` on one line | both |
| `pt_caustics.hlsl` | the jobs moved `water_point` & co. to `pt_waves.hlsli`; main (the caustics agent) gave `water_point` two outputs (`lift`, `steep`) its counters read | moved, and the edit moved with them: `water_displace` and `water_point` in `pt_waves.hlsli` give `lift` and `steep` (`water_height` ignores them) |
| `pt_common.hlsli` x2 | `g_primaryRays` (main) beside the head rule's removal of `g_reflectionRay`; `FrameConstants`' camera-ray rows beside the wet rows | both; the wet rows after the camera-ray rows |
| `tracer.cpp` x11 | the same struct rows, the size assert, the pass lists (`yardstick` vs `walk`, `wet`; `hair` gone in main), the caustics' new buffers' reset beside the wet store's, the prepare dispatch (main passes the guides' UAVs) beside the wet pass before it, the composite (main dropped the hair layer, the job the `sss` dispatches) | both everywhere; FrameConstants 7280 B (7120 + 160) |
| `pt_specular.hlsl`, `tgpt_api.h` | none | |
| `tests/source_checks.py` x4 | the size pins, the row count, the pass list pin | 7280, 42 rows, both pass lists |
| `tests/pathtracing_gpu_test.cpp` | main's new own-ray checks end where the job reworded the skin block's comment | main's checks, then the job's comment |
| `CHANGELOG.md` | both added entries on top | both, the jobs' first |

Also: `pt_primary.hlsli` still passed the first-person body within `FIRST_PERSON_HEAD` (0.3 m) of the camera, a constant the
head job deleted (it would not compile). The camera ray now passes that body's back faces as the back of any one-sided
surface, at any distance, and meets its front (a hand, the collar). Its comments name `first_person_inside`.

Build `m1` (the merge alone): `tgpt_gpu_test` exit 0, 1309 ok lines (1185 untouched + the jobs' 124), no FAIL, no hang.

## At the camera ray's own hit

How a pass after `pt_prepare` knows the material a pixel's ray met: the yardstick's word already holds it for every pixel
(bits 12-31; `write_yardstick` runs every frame). `own_material(p)` in `pt_common.hlsli` reads it. No new texture.

| Job | At the camera's own hit | Where |
|---|---|---|
| Walk (wax, skin) | the walk starts at the hit itself (its position and the triangle's orientation from the surface record, the material from `own_material`), told by `subsurface_material` exactly as a path's vertex is; the pass runs while the scene holds such a material (`SceneStats.subsurfaceMaterials`), no profile list needed; the exit's normal map is the tracer's own record's at that place on screen | `pt_walk.hlsl` `walk_own`, `exit_normal`; `tracer.cpp`; `scene.cpp/.hpp` |
| Thin things | a leaf the camera's ray meets passes its material's own share (`thin_through`), with HDRP's wrapped cosine, for the sun, the lamps, the sky and bounced light | `pt_common.hlsli` `leaf_transmittance` |
| Wet ground | `write_traced` reads the hit through `hit_material`, which makes it wet; not a material light walks inside (skin, wax), an eye, glass or hair (as `pt_prepare` kept skin dry) | `pt_common.hlsli` `hit_material` |
| Head | the camera's ray passes the body's back faces at any distance (no 30 cm rule); mirrors, a reflection in a reflection, what is seen through water and bounced light's first ray keep `first_person_inside` | `pt_primary.hlsli` `primary_walk` |

### What does not work yet at the camera's hit (not built: each is the camera-ray route's own gap)

- Skin: HDRP's G-buffer marked skin (`Surface.skin`) for the terminator's softening and its material's cavity occlusion;
  `write_traced` marks neither. The diffuse light of a walked pixel is the walk's, so the terminator softening matters for
  its highlight-free remainder only; a character's skin without a subsurface material (none named in the log) has neither.
- A leaf's mirror reflectance by its profile (0.0023, `TGPT_SetProfileReflectance` by G-buffer index): at a hit a leaf has
  its material's 0.04. A leaf's thickness map: its share is the material's one number (the middle of its remap).
- `trace_closest` (bounced light's further rays, rays through glass, a walk's own rays) still meets the inside of the
  hero's head: the head job's gap, left where the GPU hang lives.
- Wet ground: the store keeps the water seen from the camera's frame; ground under water now stays the water passes'.

## Workarounds (the owner's rule of 17:55)

Every workaround I met that exists only because the tracer started from HDRP's picture, removed or not.

| What | File, name | Done |
|---|---|---|
| The walk on HDRP's subsurface pixels: GBuffer2's feature and profile index, a mesh looked for under HDRP's surface | `pt_walk.hlsl` `walk_pixel`, `walk_entry` | **removed** |
| The walk's exit normal from HDRP's normal buffer and depth | `pt_walk.hlsl` `exit_normal` (G-buffer half) | **removed** (the tracer's own record) |
| A table of profiles' scattering by G-buffer index for the walk | `TGPT_SetDiffusionProfiles`, `FrameConstants.sssProfiles`, `Engine.sssProfiles/sss`, `HdrpPasses` scatter table, `NativeTracer.SetDiffusionProfiles` | **removed** (the material's own shape) |
| HDRP's thin-object transmission through skin by its thickness map (an ear lit from behind) | `skin_transmission`, `through_skin`, `transmitted_unoccluded`, `THIN_SKIN`, `skin_transmittance`, `Surface.transmittance`, `rtxdi_through_skin`, `pt_di_spatial`'s skin terms, `pt_prepare`'s thin skin bit 16 and bytes | **removed** (the walk) |
| Its table | `TGPT_SetSkinTransmission`, `FrameConstants.transmission`, `Engine.transmission`, `HdrpPasses` transmission table | **removed** (`TransmissionOf` stays: the plugin works out a leaf material's share from it) |
| A leaf's transmittance from GBuffer2's profile and thickness | `leaf_transmittance` (G-buffer half) | **removed** (the material's share) |
| The wet ground made on HDRP's G-buffer values | `pt_prepare.hlsl` wet block | **removed** (`hit_material`) |
| The head rule in the camera ray by distance | `pt_primary.hlsli` (`FIRST_PERSON_HEAD`) | **removed** |
| A leaf's mirror reflectance by G-buffer profile | `TGPT_SetProfileReflectance`, `profileF0`, `profile_f0`, `leaf_profile`, `pt_prepare`, `d3d11.hlsl` guides | not removed: no hit-side replacement yet (a leaf material's f0 would need the plugin to send it); not this job's |
| Skin told by a "thick profile" bit mask by G-buffer index | `TGPT_Frame.thickProfiles`, `solid_transmission`, `pt_prepare`, `pt_compose` Surfaces view, `d3d11.hlsl` guides' boost | not removed: the old route and the guides use it; skin at a hit is a gap above |
| Skin on its own traced triangle found from HDRP's surface | `pt_prepare.hlsl` `skinOnMesh` | not removed: the old route's (the switch is another job's) |
| A surface HDRP draws forward found along the camera ray, HDRP's material near a point | `pt_prepare.hlsl` forward search; `hdrp_material_at` | not removed: the brief names them as others' |
| `hdrp_material_at`, `pt_hdrp_shadow`, water passes, glass, eyes | | not touched (others' jobs) |

What the removal changes on the route before the tracer's own rays (the hidden key `[Advanced] Own camera rays` off): no
walk, no light through skin, leaves the grey half, the ground dry. Its checks went with it.

## Tests

The jobs' checks run on the tracer's own camera rays now (the old route's versions removed with the code):
- Wax (2c A to D): the walk from the camera's hit, no profile list sent; E and F (paths) as they were; **G new**: a leaf the
  camera's own ray meets passes its material's own share, tinted.
- The skin floor (shadow edges, light kept): the traced floor a material light walks inside, on the camera's rays.
- HDRP's thin-skin check (`thin skin lit from behind ...`, 4 checks): removed with the code. What an ear lets through is
  the wax slabs' check against the reference walk.
- Leaves (the sun through a blade, edge-on, the sky from behind): a traced leaf with its material's share, met by the
  camera's own ray, in place of a drawn one with HDRP's table. Two had to be made right for a traced leaf: the edge-on
  quad was rotated, then scaled 1.5 along z alone, so its drawn normal (the matrix's column) and its plane's true normal
  differed (a uniform scale and the angle solved for 0.1 towards the sun now); and the sky check runs with the sun off (a
  traced leaf lets half the sun's shadow rays through, an opaque one none: the floor that lights its front differed).
- Wet ground: on the camera's own hit (the Albedo view), the rays' Materials view as before; **new**: a material light walks
  inside stays dry at the camera's hit. The film check's line moved from 5 % to 3 %: the dry rock is its traced material's
  (0.7 rough) where it was HDRP's 0.6 (the film changes it by 6 %; with the film never chosen 0.3 %).
- Head: the mirror checks with the mirror's own pixels the tracer's hits (the traced floor a mirror); **new**: the camera's
  own ray passes the body's inside 20 cm away, meets its front, meets it two-sided.

### Mutants (one at a time; each run to its first non-timing FAIL)

| Mutant | Breaks | Caught by |
|---|---|---|
| mC1 | the walk never starts at the camera's hit | "light scattered under skin softens the shadow edges on it" |
| mC2 | a leaf the camera's ray meets takes the grey half | "a blade lit from behind takes the light its material's own share lets through ..." and the edge-on check |
| mC3 | the camera's hit is never wet | "wet rock is darker by a water film's internal reflection and dries in its time" (finished 19:53, after the hand-back) |
| mC4 | the camera's ray stops at the body's inside | "the camera's own ray passes the inside of the hero's first-person body (the head it sits in)" |
| mC5 | the camera's ray passes all of the body within 30 cm | "the camera's own ray meets the hero's first-person body from in front, 20 cm away" |
| mC6 | a material light walks inside is made wet like the ground | "a material light walks inside (skin, wax) is not made wet where the camera's own ray meets it" (before the clean-up, and again on the final tree, 19:53) |
| mC7 | the sky from behind a leaf grey, not its share (`leaf_tint`) | still running at hand-back |
| mC8 | the walk's entry taken for a back face | still running at hand-back |
| mC9 | a walk's exit takes any record's normal at its place | still running at hand-back |

## ABI, structs and bits in total (against main's 0x00011F00)

| What | Change | From |
|---|---|---|
| `TGPT_Material.subsurface[3]` | was `textureReserved[3]` (uint32, zeros), same place, the struct still 160 B: with `TGPT_MATERIAL_SUBSURFACE` the walk's medium, with `TGPT_MATERIAL_TRANSLUCENT` a leaf's own share | merge-3 (job 1) |
| `TGPT_MATERIAL_SUBSURFACE` 2097152 | new material bit (free on main, checked: no duplicate bits in the header) | job 1 |
| `TGPT_OPTION_NO_SUBSURFACE` 268435456, `TGPT_OPTION_DRY_GROUND` 536870912 | new option bits (free on main) | jobs 1, 3 |
| `TGPT_FIRST_PERSON_HEAD` | `#define` gone; `TGPT_MATERIAL_FIRST_PERSON` keeps its bit, new meaning | job 2 |
| `TGPT_SetDiffusionProfiles`, `TGPT_SetSkinTransmission` | **exports removed** (the plugin no longer binds them) | this port, 17:55 rule |
| `FrameConstants` (shader constants) | 7120 (main) -> 7280 with the wet rows -> **6512** without `sssProfiles[16]` and `transmission[32]` | jobs 3; this port |
| Surface record (`pt_prepare` / `pt_primary`) | the material word's bit 16 now only see-through glass (with bit 2); thin skin's bytes gone | this port |
| Passes | `sss` gone; `walk`, `wet` new (both sets) | jobs 1, 3 |

No exported struct changed size; `TGPT_Frame.thickProfiles` stays (the old route, the guides).

## Cost of each job's stage (offline, 1280 x 720; other agents' GPU tests shared the GPU: read the harness's)

| Job | Stage | Measured |
|---|---|---|
| Walk | the direct light stage with wax over the whole 720p view, the walk from the camera's hit on / off | 4.76 against 1.22 ms (k2; 3.5 ms a full screen of wax; merge-3: 3.82 against 0.97). In game wax and skin cover a small part of the screen. One walk (about 23 rays) more at every path vertex, cache sample and reflection that meets wax or skin: not measured apart |
| Wet | the prepare stage over a 120 m sea with waves, store on / off | 0.56 against 0.32 ms (k2); over the pool 0.26 against 0.23. Offline numbers moved ten times between runs in merge-3 too: read `prepare` in the harness by the sea |
| Head | no stage of its own | |

## One in-game scene and recipe per job (Denoising None first)

- **Walk:** scenario `candle`, recipe `quick` (its `2-traced-none` frames first): the melted wax's folds and pockets at
  the candles' feet no longer black. Then `npc-candle`: an ear or a nostril towards the candle glows red (the walk; HDRP's
  thickness-map transmission is gone). `LogOutput.log`: `Subsurface material "..."` lines name the walked materials (none
  for the game's skin means skin is not walked: then report it).
- **Wet ground:** scenario `water-day`, recipe `caustics` (its `anim` steps: the store's clock stands still in a frozen
  world): behind each wave a glossy film for about 5 s, then dark sand up to the highest reach. The hidden "Wet ground"
  row off for the comparison; the native log's `prepare` stage for its cost.
- **Head:** scenario `mirror-day`, recipe `mirror`: no orange bulb on the hero's shoulders in the mirror; the collar stays.
  And with the camera's own rays: nothing of the inside of the head at the picture's edges when looking up or down.

## Final runs

- Build `k2` (the copy as it stands, native `620C4B7B2377`), `tgpt_gpu_test` whole, Denoising None where the checks say:
  1350 ok lines, **one FAIL: the timing check** "the caustics and their lookup stay affordable where 25 lamps light a lake
  (under 10 ms here)": 17.8 ms with three or four other agents' tests on the GPU (it read 3.4 to 7.7 ms in this copy's
  runs `base` to `t2`, 14.0 in `k1`, also shared). No other FAIL, no hang (`%TEMP%	gb\port2-test-k2.txt`). Nothing of
  the caustics changed in the clean-up; a side-by-side run of `t2` (before it) and `k2` up to the caustics block, same
  GPU load, is in "Log".
- `python tests/source_checks.py`: 90 tests, **OK** (none fails; the neural session's three that failed for merge-3
  pass in `base35`).
- The plugin compiles: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472
  -p:DeployToGame=false --no-incremental`: 0 warnings, 0 errors (the copy's own `bin/`).
- Never run offline: Ray Reconstruction (the harness's), the plugin's reading of materials (`ReadSubsurface`), the hidden
  rows.
- Against `wt/base35`: changed `CHANGELOG.md`, `CMakeLists.txt` (three lines, the shader lists), `docs/PROJECT-CONTEXT.md`
  (lines replaced, none added), `docs/PT-REQUESTS.md`, `docs/TESTING.md`, `native/pathtracing/include/tgpt_api.h`,
  `native/pathtracing/shaders/` `pt_caustics.hlsl`, `pt_common.hlsli`, `pt_di_spatial.hlsl`, `pt_prepare.hlsl`,
  `pt_primary.hlsli`, `pt_rtxdi.hlsli`, `pt_shaders.hpp.in`, `pt_specular.hlsl`, `native/pathtracing/src/` `extension.cpp`,
  `runtime.hpp`, `scene.cpp`, `scene.hpp`, `tracer.cpp`, `src/TGPathTracing/` `HdrpPasses.cs`, `NativeTracer.cs`,
  `SceneCapture.cs`, `Settings.cs`, `WaterCaustics.cs`, `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`. New:
  `pt_walk.hlsl`, `pt_waves.hlsli`, `pt_wet.hlsl`, `docs/agents/` `merge-3.md`, `sss.md`, `water-edges.md`, this file.
  Deleted: `pt_sss.hlsl`. Every file keeps its line endings (`<scratch>/p2/eol.py`). For the merge into main:
  `<scratch>/p2/merge_port.py port2 --base=base35 --into=<target>` is the same tool aimed elsewhere (it writes into a copy
  under `wt/`; main's `merge_eol.py port2 --base=base35` does the same into main).

## Log

- Start: `port2` equals `base35`. Baseline (tag `base`): `tgpt_gpu_test` exit 0, 1185 ok lines, no FAIL, no hang.
- The merge: 25 conflict blocks resolved (above). Build `m1`: exit 0, 1309 ok lines, no FAIL, no hang.
- Build `c1` (the walk from the camera's hit, a leaf's own share at the hit, what is not ground stays dry): exit 0, 1309.
- Build `t1` (the jobs' checks on both routes, new camera-ray checks): exit 0, 1427 ok lines. Build `t2` (two more): 1435.
- A first mutant round beside other agents' tests; stopped for the 17:55 rule; sources hashed as before it.
- 17:55, the owner's rule: the old route's versions removed (above). Build `k1`: 3 FAIL (the timing check, shared GPU;
  the two leaf checks whose drawn-only geometry did not fit a traced leaf: fixed as described). Build `k2`: 1350 ok, the
  one timing FAIL only (above); it waited 58 minutes for the GPU (other agents' tests, the coordinator's harness).
- Mutants on the final tree (`<scratch>/p2/mutants.ps1`, detached, writes `%TEMP%\tgb\port2-mutants.log`; each run in
  `port2-test-mC<n>.txt`): mC1, mC2, mC4, mC5 caught by the check meant for each; mC3, mC7, mC8, mC9 still queued for the
  GPU at hand-back (the script reverts each mutant right after building it: the sources are back as before, checked
  against `port2-hash-before-mutants2.txt`). A side-by-side run of `t2` and `k2` up to the caustics block (tags `t2c`,
  `k2c`, outputs `port2-run-*.out`) was also queued, to show the lake's timing is the shared GPU's, not the clean-up's.
