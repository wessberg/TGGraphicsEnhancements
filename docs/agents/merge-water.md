# Merge: the water job (wt\water4, base45) onto today's tree (wt\water5, base51)

Worker: Claude, 2026-10-08, 04:25 to about 05:10. Copy `wt\water5`. Nothing was written to the main tree, to
`wt\water4` or to the game folder. The game was never started; nothing was deployed or committed.

## Read this first

**Merged, built, and the water fixture and every glass check pass. Not seen in the game.**

- The tool's merge: 32 files changed, 14 new, 3 deleted, 27 conflicts in 10 files. All 27 are resolved with both sides
  kept. Seven more places needed a change that was no conflict (section 3).
- Built: `TGPathTracing.Native.dll`, `tgpt_gpu_test.exe` (NRD off), the Mono plugin (0 warnings). Source checks 95 of 95.
- Passed offline: the water fixture (87 ok, 0 FAIL), every glass check (the one FAIL in that run is the candle job's
  known "skin 110 m from scene origin"). The whole GPU test: WHOLE-RUN-PLACEHOLDER
- ABI 0x00012400 in the three files. Every record size agrees in the header, the plugin, the shaders and the checks,
  and the plugin's own `Validate()` was run outside the game: it passes.
- The compiled passes are 26 % smaller than main's in all (the water job removed more than it added). Passes from cold:
  215 s; from the disk cache: 2.8 s.
- Three things for the coordinator, not fixed here: any water at all holds 56 MB (one page of each pool and the
  scratch), the test lake 240 MB of pages; the water costs about 2.7 ms of render-thread time a frame for that lake,
  which nobody had measured; and still water is posed and refitted every frame like moving water (section 7).

Three claims, kept apart: **built** (yes), **passes offline** (as above), **seen in game** (no).

If someone picks this up: the sources in `wt\water5` are final for this job. My scripts are in the scratchpad folder
above `wt`: `resolve.py` with `edits_*.py` (the exact text of every resolution, in the order applied),
`water5-work\build.ps1` (configure, build, stage), `run.ps1` (one GPU test behind the gate,
`TGPT_TEST_STARTUP_SECONDS=900`, one pass cache in `%TEMP%\tgb\water5-shadercache`), `final.ps1` (the runs),
`validate-managed.ps1` (the plugin's struct sizes), `pass_sizes.py`.

## 1. The merge

`python tools/agents/merge_copy.py water4 --base=base45 --root=<scratchpad>`, run from `wt\water5` (first with `--dry`):
32 changed, 14 new, 3 deleted, 27 conflicts, as the brief said.

| File | Conflicts |
|---|---|
| `CMakeLists.txt` | 1 |
| `native/pathtracing/shaders/pt_common.hlsli` | 3 |
| `native/pathtracing/shaders/pt_dielectric.hlsli` | 5 |
| `native/pathtracing/shaders/pt_specular.hlsl` | 1 |
| `native/pathtracing/src/scene.cpp` | 2 |
| `native/pathtracing/src/scene.hpp` | 4 |
| `native/pathtracing/src/tracer.cpp` | 4 |
| `src/TGPathTracing/NativeTracer.cs` | 1 |
| `tests/pathtracing_gpu_test.cpp` | 2 |
| `tests/source_checks.py` | 4 |

Merged with no conflict (each diff read): `docs/TESTING.md`, `tgpt_api.h`, `d3d11.hlsl`, `pt_compose.hlsl`,
`pt_primary.hlsli`, `pt_waves.hlsli`, `extension.cpp`, `runtime.hpp`, `HdrpPasses.cs`, `SceneCapture.cs`,
`tests/ingame/scenarios.json`.

Taken whole from the water copy (today's tree still had base45's text): `docs/PT-DETAILS.md`, `pt_caustics.hlsl`,
`pt_nrd_prep.hlsl`, `pt_rtxdi.hlsli`, `pt_shaders.hpp.in`, `pt_wet.hlsl`, `README.md`, `DebugCapture.cs`,
`PathTracingRow.cs`, `Settings.cs`, `WaterCaustics.cs`.

New: `docs/agents/water-3*.txt|md` (5), `docs/agents/water-4.md`, `pt_interface.hlsli`, `pt_water_foam.hlsli`,
`pt_water_mesh.hlsl`, `pt_water_path.hlsli`, `pt_water_surface.hlsli`, `scene_water.cpp`,
`WaterSurfaceRendering.cs`, `tests/water_surface_gpu.inl`.

**Deleted by the water job: `pt_water.hlsl`, `pt_water_under.hlsl`, `WaterShade.cs`. Main had changed none of the three
files since base45** (the tool says "delete", not "changed since"; they are kept in `<scratchpad>\removed-by-merge\water4`).
So no file's own text had to move. But main had three things that depended on what the water job deleted:

| What of main's | Where it went |
|---|---|
| The speed job's one-read loop in `shade_path` honoured `g_firstKnown` / `g_firstMaterial`: the first vertex's material handed in by `pt_water_under.hlsl` | Removed with that pass (nothing sets them; the water job's source check forbids the names). The one read of the material stays |
| Inside the deleted block of `tracer.cpp` (the old water passes' recording, 255 lines) main had changed two lines: the pass bound the decals' sampler heap beside the tracer's | Nothing to move: every place that still binds heaps binds both |
| The memory job's `release_scene_extras` and memory line named the old passes' members (`waterLatest`, `waterBase`, `waterApply`, `waterHist`, `waterUnderHist`, `waterIn`, `waterOut`, `waterUnderOut`, `waterFrameConstants`) | Removed from those lines. What the water holds now is counted in `GpuScene::memory` (section 3) |

## 2. Each conflict and how it was resolved

### `CMakeLists.txt` (1)
Main added `pt_decal_graphs.hlsli` to the shaders' dependencies; the water job took the passes `water` and `waterUnder`
out of the list and added its three headers. Both kept. I added `pt_interface.hlsli` to the dependencies too (the water
copy had left it out: an edit to the shared boundary would not have rebuilt the passes). Nothing else in the file is
touched: against base51 it differs by the pass list, the dependency list, `waterMesh:pt_water_mesh` and
`scene_water.cpp`.

### `pt_specular.hlsl` (1)
Main (glass job) added `g_glassDrawn = true;` before the glass pixel's `dielectric_follow`; the water job rewrote the
same lines so that glass, water and reflections share one `shade_seen` call. Kept the water job's shape with the glass
job's line in front of its `dielectric_follow`. It stays set through the shared `shade_seen`, as in main.

### `NativeTracer.cs` (1)
`Validate`: main's `NativeSkinnedInstance` 100 bytes, the water job's `NativeWaterCaustics` 288 bytes and no
`NativeWaterShade`. Both.

### `pt_dielectric.hlsli` (5): glass keeps the glass job's rules, through the water job's one boundary
- `optical_medium`'s two queries: main's `RAY_FLAG_CULL_OPAQUE` (the speed job: opaque triangles never asked about)
  with the water job's `MASK_SOLID` (the wave triangles are no glass solid's boundary). Both.
- `solid` (closed against open), `ni` and `nt` as the glass job has them. Its `eta` line is gone: the boundary holds it.
- `const Interface boundary = dielectric_interface(dir, halfVector, ni, nt);` then `float F = boundary.reflectance;`
  and the glass job's pane rule `if (!solid) F = 2.0 * F / (1.0 + F);`. The same numbers as main's
  `fresnel_dielectric(dot(v, halfVector), nt / ni)`: the boundary takes the absolute cosine, and gives 0 between equal
  media.
- `next = reflection ? reflected : solid ? boundary.transmitted : dir;` (main: `refract(dir, halfVector, eta)`, the same
  vector; at total internal reflection F is 1 and the ray always reflects, in both).
- `if (!reflection && solid) weight *= interface_radiance(boundary);` (main: `eta * eta`, the same number).
- `segment_transmittance(absorption, distance)` merged by itself.
- Paint by alpha (`glass_cover`, `g_glassDrawn`) and the cornea's thin shell are untouched: they are above the boundary.
Two of the glass job's pinned lines and one of the water job's change their text; the source checks pin the merged
lines. The glass checks are the proof that the numbers are the same (section 6).

### `scene.hpp` (4)
All four are "both": main's `InstanceRecordGpu` with `renderingLayerMask` (20 bytes; `MeshRecordGpu` 48) and the water
job's `WaterRecordGpu` (64); `decalsSrv...` and `waterRecordsSrv...`; main's `build_blas(..., compactSize=0)` with the
water job's declarations; the memory job's compaction and retiring members and the water job's members.

### `scene.cpp` (2)
- After a mesh's upload the CPU copies go: main also frees `tangents` and `decalColors`; the water job keeps a water
  mesh's positions and indices. Both.
- `clear`'s list of record buffers: main's (grid, plants, noise, decals) plus `waterRecordBuffer_`.

### `pt_common.hlsli` (3)
- `fetch_hit`: main's new `rayOrigin` parameter and the water job's `water_hit` declaration. The water job's own call
  (`trace_water`, `pt_water_surface.hlsli`) now passes the ray's origin.
- `shade_path`: the speed job reads a vertex's material once (a loop of at most two turns, `hit_is_glass`); the water
  job leaves for the water's own path. Both: the loop runs `while (hit.water == 0)`, so water met at once or behind
  glass has no material read, and the water job's `if (hit.water != 0) break;` follows it. The `g_firstKnown` lines
  are gone (section 1).
- `shade_seen`: the same loop (`while (hit.water == 0)`), then the water job's continuation.

### `tracer.cpp` (4)
- `FrameConstants`: base45 6,512 bytes; main +16 (the decals' row) = 6,528; the water job +1,376 = 7,888. Both: **7,904**.
  The C++ struct and the shader's have the same rows in the same order (the water's after `fog`, the decals' after
  `motionViewProj`); the static assert holds the sum.
- The pass jobs: main's (the pass cache needs each pass's name in `make_pso`, and `pass_cache::timing` logs each pass's
  time and whether the disk cache had it). The water job had added a `name` to each job for a line of its own
  ("Pipeline X made in N s"): the same thing written twice, so that line and the member are gone. Nothing read it.
- The Shader Model 6.9 pass list: main's `if(!serReorders)` in front of `gi` and `specular`, without the water job's
  two removed passes.
- The old water passes' code is deleted, as the water job deleted it (section 1).

### `tests/pathtracing_gpu_test.cpp` (2)
- The start-up wait: main's (`TGPT_TEST_STARTUP_SECONDS`). The water job's fixed 180 s was the same helper again.
- The check files: the water's fixture first (it runs only with `TGPT_TEST_WATER_SURFACE=1`), then main's candle and
  skin checks, then the glass checks.

### `tests/source_checks.py` (4, and pins that follow from the resolutions)
- `FrameConstants` 7,904 in both places; the comment keeps the water job's sum and adds the decals' 16.
- The count of single rows in the shader's `FrameConstants`: each side had made it 43 (its own one row), so the merge
  kept 43; with both rows it is **44**. A three-way merge cannot see that.
- The start-up pins: main's.
- `pt_dielectric.hlsli`'s pins are the merged lines: `float F = boundary.reflectance;`,
  `... : solid ? boundary.transmitted : dir;`, `if (!reflection && solid) weight *= interface_radiance(boundary);`, and
  that the file calls neither `refract(` nor `fresnel_dielectric(` itself.

## 3. What the merge had to add where the two sides meet (no conflict; each found by reading or by a failing check)

| Where | What | Why |
|---|---|---|
| `scene.cpp` `GpuScene::memory`, new `water_memory()`; `tracer.cpp` the memory line | **The water's surface is counted in the memory line**: its pieces' vertex pages and the one pattern of indices under `meshGeometry`, its acceleration structure pages under `acceleration`, its scratch under `scratch`, its three record buffers under `records`, a water mesh's read-back under `transfer`. The log line ends with "...; of meshGeometry, acceleration, scratch, records and transfer the water's surface holds N" (bytes) when there is any | The memory job's rule. `TGPT_Diagnostics` has no figure for water and I did not grow that record (176 bytes, mirrored in the plugin): the parts are in the figures of what they are, the line's figures still sum to its total, and the test's "scene-owned allocations returned after clear" (`meshGeometry`, `acceleration`, `records`, `scratch` all 0) now covers the water too |
| `scene.cpp` `retire_meshes` | A retired mesh's pending water read-back is released with it | `TGPT_ReleaseMesh` (memory job) and the read-back (water job) did not know of each other |
| `scene.cpp` `record`, `scene.hpp` | The water meshes' records (64 bytes each) are uploaded when they changed, not every frame (`uploadedWaterRecords_`) | The brief: nothing uploaded every frame that does not change every frame |
| `scene.cpp` the material of no kind | It carries `TGPT_MATERIAL_NO_DECALS` | Decals (main) paint a hit's material in `hit_material`; a bounce or cache ray that meets the water reads that material. HDRP's water takes no decal either |
| `pt_water_surface.hlsli` `trace_water` | passes the ray's origin to `fetch_hit` | main's new parameter (decal parallax) |
| `CMakeLists.txt` | `pt_interface.hlsli` in the shaders' dependencies | an edit to the shared boundary must rebuild the passes |
| `tests/pathtracing_gpu_test.cpp` | main's cutout check ("camera rays use the source alpha ...", new since base45) counted "more than 1,000 green pixels"; now the same share of whatever size the run has (`size_t(1000)*W*H/(1280u*720u)`: 1,000 at the whole test's size, as before) | It runs before the water's fixture, which runs at 128 x 72: 919 of its 9,216 pixels are green, so the fixture's run failed on a check that is not the water's |
| `tests/water_surface_gpu.inl` | the cost run's line also prints the render thread's time a frame and its recording part | to say what the water costs the CPU (section 7) |

What I checked and left as it is:
- **Released on clear:** `GpuScene::clear` calls `release_water` (pages, indices, scratch, record buffers, read-backs,
  views) and resets `waterRecordBuffer_` in its own list. The tracer's side of the water is the caustics' wave data
  (`st.caustics`), which main's `release_scene_extras` already releases.
- **The pass cache and the progress count:** the one new pass, `waterMesh`, is made by `pso(...)` like every other
  compute pass, so it is read from and written to the disk cache (`make_pso`), logged by `pass_cache::timing` and counted
  in `pass_cache::total` and `completed`. The two old passes and the old D3D11 water shaders are out of the lists.
  Seen in the runs: 45 of 45 passes report completion; the second process takes 43 from the disk.
- **Geometry records:** a water piece's record is `{indexStart, indexCount, layers 0, flags 0}`: not closed (an open
  sheet; glass's rule never asks it, the water is no glass), no layers of its own. Its instance record's
  `renderingLayerMask` is 0.
- **The Caustics row's "off"** (the finisher's choice, confirmed by the brief): as left. `water_passes_light`
  (`pt_water_surface.hlsli`) with its comment that the straight light is what "off" is, not a feature; the tooltip
  ("... and water casts no pattern"), the README, `docs/TESTING.md` and the check "with the Caustics row off a light's
  straight ray passes clear water: its edge casts no shadow" are all in the merged tree and the check passes.
- **Not fixed, main's own (found while reading `retire_meshes`):** it releases a retired mesh's views but not
  `tangentsSrv` and `decalColorsSrv`, which the decals job added later. Two descriptors leak per retired mesh that has
  them. Not the merge's; for the coordinator.

## 4. The coordinator's files, and the ABI

`BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`, `AGENTS.md`,
`docs/AGENT-WORKERS.md`: after the merge each is byte for byte base51's. The water copy had added nothing to them (its
finisher had already put them back to base45's), so there was nothing to restore. What the water job learned for them
is in `docs/agents/water-4.md` section 6 ("For the coordinator's files"): a mesh shared by water and something else is
read back from the tracer's own buffers (8 frames); the driver's shader cache hides a pass's real making time;
`passes_through` and `ray_offset` are G-buffer era rules still applied to the tracer's own surfaces (a wave's shadow
lost near a shore, shortened by 1.2 cm); quoted heredocs in the Bash tool.
`docs/PT-DETAILS.md` is not on that list: it has the water job's two changed lines.

`TGPT_ABI` is **0x00012400** in `native/pathtracing/include/tgpt_api.h`, `src/TGPathTracing/NativeTracer.cs` and
`tests/source_checks.py`.

## 5. The records (every size, and where it is pinned)

| Record | Bytes | C++ | Plugin `Validate` | Shader | Source check |
|---|---|---|---|---|---|
| `TGPT_ABI` | 0x00012400 | `tgpt_api.h` | `Tgpt.Abi` | - | `test_path_tracing_abi` |
| `FrameConstants` | 7,904 | `tracer.cpp` static assert | - | `pt_common.hlsli` (44 single rows) | both pins |
| `TGPT_WaterCaustics` | 288 | static assert | `NativeWaterCaustics` 288 | - | by the header's number |
| `TGPT_SkinnedInstance` | 100 | static assert | 100 | - | by the header's number |
| `TGPT_Instance` / `TGPT_Moving` | 68 / 116 | static assert | 68 / 116 | - | by the header's number |
| `TGPT_MeshDesc`, `TGPT_Material`, `TGPT_Decal` | 192, 204, 928 | static assert | the same | - | by the header's number |
| `TGPT_Status`, `TGPT_Frame`, `TGPT_Diagnostics` | 216, 600, 176 | static assert | the same | - | by the header's number |
| `MeshRecordGpu`, `GeometryRecordGpu`, `InstanceRecordGpu` | 48, 16, 20 | `scene.hpp` static assert | - | `MeshRecord`, `GeometryRecord` (`layers`, `flags`), `InstanceRecord` | the glass and decal pins |
| `WaterRecordGpu`, `WaterTriGpu` | 64, 32 | `scene.hpp` static assert | - | `pt_water_surface.hlsli`, `pt_water_mesh.hlsl` | `test_path_tracing_water_surface` |

The plugin's check was run for real, outside the game: `water5-work\validate-managed.ps1` loads the built
`TGPathTracing.dll` (the game's and BepInEx's assemblies resolved read-only), prints every `Native*` struct's
marshalled size and calls `NativeTracer.Validate()`. Result: all sizes as above, `Tgpt.Abi = 0x00012400`, "passed (no
packing mismatch)". The native side's static asserts compiled. So the four places agree.

## 6. Build, the passes, the tests

### Build
- Native: `%TEMP%\tgb\water5-build` (Ninja, Release, `-DTGPT_WITH_NRD=OFF`), targets `TGPathTracing.Native` and
  `tgpt_gpu_test`. First build `b1`: exit 0 after 505 s (configure, all shader passes side by side, C++), no error and
  no warning to fix. `b2` (the water records' upload): 15 s. `b3` (the two test edits; the DLL unchanged): 5 s.
  Staged in `%TEMP%\tgb\water5-stage`; `TGPathTracing.Native.dll` sha256
  `D68FDE64EA1949329D8F3E0376DFD77EC07874F813260F6E4036041031E5E4F1`.
- Plugin (Mono): `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:DeployToGame=false`:
  0 warnings, 0 errors (into the copy's own `bin`).
- Source checks: **95 of 95** (main's 94 and the water job's `test_path_tracing_water_surface`).
- NRD is not in this build, and Ray Reconstruction never runs offline. `pt_nrd_prep.hlsl` (taken whole from the water
  copy: water left out of NRD's diffuse input) is not part of it; I compiled it by hand with CMake's own flags
  against the SDK's headers, read only, into `%TEMP%\tgb\water5-nrd-prep-check.h`: exit 0. **Compiled, never run**:
  the coordinator's merged build is the first to run NRD with water.

### The passes' sizes (compiled DXIL, bytes; `water5-work\pass_sizes.py` over the builds' generated headers)

| Pass | main (its `build\native`, headers of 03:36) | this merge | |
|---|---|---|---|
| `pt_specular` / `_dxr12` / `_ser` | 3,149,240 / 4,496,604 / 4,529,688 | 1,982,400 / 2,799,000 / 2,824,776 | 38 % smaller: the water job's one `shade_seen` call for glass, water and reflections |
| `pt_gi` / `_dxr12` / `_ser` | 1,070,836 / 1,537,264 / 1,548,680 | 1,389,768 / 1,972,356 / 1,992,844 | +29 %: the water's path inside `shade_path` |
| `pt_walk` / `_dxr12` | 686,616 / 977,888 | 999,444 / 1,406,896 | +45 %: the same |
| `pt_prepare` / `_dxr12` | 1,413,644 / 2,054,352 | 1,449,048 / 2,093,012 | +2 % |
| `pt_compose` / `_dxr12` | 166,388 / 248,892 | 169,912 / 252,576 | +2 % |
| `pt_caustics` / `_dxr12` | 71,204 / 76,152 | 78,052 / 82,996 | +9 % |
| `pt_water` / `_dxr12` | 569,600 / 808,352 | gone | |
| `pt_water_under` / `_dxr12` | 1,197,452 / 1,752,188 | gone | |
| `pt_water_mesh` | - | 23,132 | new |
| every other pass | | within 1 % | |
| **What this card makes** (Shader Model 6.9 with reordering) | 12,629,356 bytes in 24 passes | **9,309,924 bytes in 23 passes** | 26 % less |
| The Shader Model 6.6 set | 8,926,544 | 6,695,168 | 25 % less |

So the water job takes more off the passes than it adds: two large passes are gone and the reflections pass is 1.7 MB
smaller, against 0.4 MB more in bounced light and 0.4 MB in the walk. (Main's `build\native` also holds headers of
passes long deleted, `pt_hair`, `pt_hdrp_shadow`, `pt_sss`: left out of the sums. I took main's headers as base51's;
they are from 03:36, base51 from 04:10, so a late shader edit of main's would be missing from the left column.)

### Start-up
- **Cold** (run `f1-water`, 04:42: a pass cache folder that had never seen these shaders, nor had the driver):
  **215.3 s**, 45 passes, 0 disk cache hits. The slowest by their own time: `giSer` 214.0 s, `specularSer` 166.9 s,
  `prepareDxr12` 89.4 s, `walkDxr12` 70.7 s, `diSpatialDxr12` 11.0 s; every other pass under 5 s. They are made side by
  side, so the total is the slowest one's. The coordinator's figures for base51: 515 s in the test, 210 s in the game.
  One run, with the owner's Starfield (about 70 % of the GPU) and two other workers' tests running: read it as "no
  slower than base51", not as a measured gain. What the water adds is in the two passes that grew: bounced light (now
  the slowest pass) and the walk; what it takes away is the reflections pass's third and two whole passes.
- **Warm** (the next process): **2.8 s** and 2.6 s, 43 of 45 passes from the disk cache (the two reordering pipelines
  are in the driver's own cache). The test's "second process reads compiled passes from disk" (40 or more) holds.

### Tests
The owner's Starfield and two other workers' GPU tests were running throughout: stage times are "about".
Logs: `%TEMP%\tgb\water5-test-<run>.log`; each run's native log:
`%TEMP%\tgb\water5-stage\water5-test-stage-<run>\TGPathTracing\TGPathTracing.Native.log`; pictures:
`%TEMP%\tgb\water5-images-<run>`.

- **Water fixture** (`TGPT_TEST_WATER_SURFACE=1`), run `f2-water`, the final build: **PASS, 87 ok, 0 FAIL**, 29 s.
  The first run (`f1-water`) had one FAIL, main's cutout check at the fixture's small size (section 3); every water
  check passed in both. The measured optics agree with the water job's own final run to the fifth digit (bed 0.550862
  against 0.550860, absorption 0.670434 against 0.670411, grazing 0.407073 against 0.407063, underwater bed 0.817807
  against 0.817800): another random sequence, not another picture.
- **Glass checks alone** (`TGPT_TEST_GLASS_ONLY=1`), run `f1-glass`: 131 ok, 1 FAIL. The FAIL is the candle job's
  known one ("coloured or white bulk slab ...", its case "skin 110 m from scene origin": 0.965, 0.956, 0.941), which
  runs before the glass checks. **Every glass check passes**, the glass job's four among them: "paint on see-through
  glass covers its share of it", "an open sheet of glass is a thin pane" (0.92680 of the backdrop; a pane's two faces
  let 0.92308 through), "an eye under a cornea's open shell", "a lit card of hair sends light on"; and the older ones
  through the shared boundary: Beer-Lambert 0.36688 0.81742 0.95917 (expected 0.36788 0.81873 0.96079), the index's
  Fresnel 0.92539 (0.92308), Snell's displaced target, eighty interfaces 1.069, the jar's tint and rim, the eye in a
  mirror.
- **The whole GPU test, once** (run `f3-whole`): WHOLE-RUN-DETAILS-PLACEHOLDER
- No mutation checks (the brief).

## 7. What the water does a frame, in ms and bytes

Read from `scene.cpp` (`record`), `scene_water.cpp` (`plan_water`, `pose_water`) and `pt_water_mesh.hlsl`; measured by
the fixture's cost run (`TGPT_TEST_WATER_COST=1`, run `f2-cost`: a floor 512 m wide seen from eye height, dry, then
under 40 cm of water with waves; 1280 x 720, no denoiser, the Caustics row off).

Every frame, while the waves move:
- **Render thread:** the water meshes' records are put together (64 bytes each; uploaded only when they changed).
  `plan_water` walks each surface's tree of triangles from the camera's position and looks each wanted piece up; a new
  piece gets a block of the vertex pool and of the acceleration structure pool, a view and its triangle records.
  `pose_water` records one acceleration structure build or refit a piece, one instance and one instance record a piece.
- **GPU:** one compute dispatch a lattice level (six at most) writes every vertex of every piece in use, 36 bytes a
  vertex (its position, its undisplaced point, its position a frame ago); then every piece's acceleration structure is
  refitted (built once, when new); the top level has one instance more a piece.
- **Uploads from the CPU:** none of the water's own while the same pieces are in use. When the set of pieces changes
  (the camera moved): their triangle records (32 bytes a base triangle) and, as for the plants' groups, the scene's
  whole mesh and geometry record lists again. The scene's material index list is uploaded every frame while the scene
  has water, as it already is while it has characters, plants or moving things (main's `extraIndices`): 4 bytes more.

| The test lake (1,376 pieces, 1,250,816 triangles, 687,888 vertices) | Dry floor | The lake |
|---|---|---|
| GPU, the scene stage (posing, refits, the top level) | 0.03 ms | 1.45 ms (the water finisher, on a quieter GPU: 0.57 ms) |
| Render thread, recording the frame | 0.15 ms | **2.86 ms** |
| GPU, the whole frame | 13.05 ms | 4.42 ms (the dry floor's bounced light is the big number in this scene; water is not free) |
| Bytes the pose writes on the GPU | 0 | 24.8 MB (687,888 x 36) |
| Memory held | 0 | 240 MB of pages (the log's figure, in units of 2^20 bytes), and its scratch on top (8 MB at least; no memory line fell inside this 4 s run) |

- **The 2.7 ms of render-thread time for the lake is new information**: the water job measured the GPU only. It is
  1,376 acceleration structure calls, instances and map look-ups a frame. With Starfield on the machine the number is
  "about", but it is CPU time on the game's render thread, not GPU time, and it grows with the number of pieces.
- **Any water at all holds 56 MB** (units of 2^20 bytes, as the logs count): the pools hand out whole pages (16 MB of
  vertices, 32 MB of acceleration structures) and the scratch starts at 8 MB. The fixture's still pool is "1 pieces,
  2 triangles, 6 vertices ... 48 MB of pages", and the memory line after it reads "the water's surface holds
  59113472" (bytes: the two pages, the scratch, and 0.4 MB of indices and records).
- **Still water is posed and refitted every frame like moving water.** Its pieces are the mesh's own triangles, uncut
  (`water_cuts` returns 0 without waves), so this costs microseconds, but it is the one thing the water does every frame
  that does not change every frame. Skipping it needs the pose to know that nothing it reads changed (the scene's
  origin moves with the camera); not done, the water job's design, named here.
- Nothing else of the water is built or uploaded every frame: the pattern of indices and the lattice's table are made
  once, a piece's blocks and view once, a water mesh's read-back once.

## 8. For the job that follows: where a see-through material's own tint would enter (asked by the coordinator)

I did not fix the three tint checks (green glass, grey glass twice). This is where the tint is today, read from the
merged code, and where it would go.

**What "the shared boundary" holds** (`pt_interface.hlsli`): `dielectric_interface` (Fresnel, the bent ray, total
internal reflection), `interface_radiance` (the index ratio squared) and `segment_transmittance(extinction, distance)`
(what a medium takes from a ray per metre). It knows two media with a boundary between them. It has no thin pane and
no colour of a surface.

| Ray | Glass with an inside (`glass_solid`) | An open sheet (a pane) | Water |
|---|---|---|---|
| Camera rays and bounce rays (`dielectric_follow`, `pt_dielectric.hlsli`; called from `pt_specular`, `shade_path`, `shade_seen`, `cache_sample`) | tinted: `weight *= segment_transmittance(absorption, distance)` over the traced stretch inside, `absorption` the entered material's `MaterialRecord.absorption` | **no tint.** `medium = outer` for `!solid`, so the stretch after the pane is absorbed by what surrounds it, never by the pane | tinted: `water_path` (`pt_water_path.hlsli`) takes `segment_transmittance(sigma, travel)` of the slot's absorption (`water_row(slot, 14)`) over every stretch under water |
| Shadow rays (`transmittance`, `transmittance_from_surface`, `pt_common.hlsli`) | `glass_walk`: `sqrt(glass_color)` a crossing. `glass_color` is the base colour for glass the game draws opaque, and **`open * open` (no colour) for see-through glass** since the glass job | the same `glass_walk`, the same `open * open` | tinted: `water_through` after the visibility test (`segment_transmittance` of the slot's absorption; `water_straight` with the Caustics row off) |
| The caustics' light (`pt_caustics.hlsl`) | none | none | tinted: `tint * segment_transmittance(...)` |

So the water side has its tint on all three kinds of ray, always as absorption per metre over a stretch, through
`segment_transmittance`. The glass side has that same path only for closed glass on camera and bounce rays, and it only
tints when the material carries an absorption (`GlassFinish.cs` sets it from `_TransmittanceColor` and `_ATDistance`,
and only when the game's shader has a refraction model on). **What the glass side lost against the water side: a
pane's tint on camera and bounce rays, and any colour at all for see-through glass on shadow rays** (the coordinator's
lines 3 to 5: green and grey glass over a floor).

Where it would enter, on top of the one boundary:
1. **Camera and bounce rays, a pane:** in `dielectric_follow`, the branch `!solid && !reflection` (the ray goes
   straight on). Today that branch multiplies `weight` by 1 (`matched || (!solid && !reflection) ? 1.0 : smith_g1`).
   A pane's own transmittance for one crossing belongs there. It needs a thickness or a per-crossing colour, which
   the boundary does not have: a small function beside `segment_transmittance` in `pt_interface.hlsli` (a pane's
   transmittance from the material's absorption and a thickness, or from its transmittance colour directly), called
   here.
2. **Camera and bounce rays, closed glass without an absorption:** nothing to add in the shader; the material's
   `absorption` is the place (the plugin's `GlassFinish.cs`). The shader already uses it.
3. **Shadow rays:** `glass_color` (`pt_common.hlsli`), the factor `glass_walk` takes the root of per crossing. For
   see-through glass it returns the uncovered share squared and no colour. The same per-crossing transmittance as in 1
   would multiply it there (a closed solid's two crossings give its colour once, as the comment says), so that a
   shadow ray and a camera ray through the same pane agree. Shadow rays do not go through `dielectric_follow` or
   `dielectric_interface` at all: `glass_walk` is their only place.
4. **The paint** (`glass_cover`, the game's alpha as the share of a texel a lit surface covers) is a separate thing
   and should stay as the glass job left it: it stops a ray by chance, it does not tint what passes.

One thing to keep in mind there: `glass_walk`'s query is `RAY_FLAG_CULL_OPAQUE` with `MASK_ALL`. The water's pieces are
opaque triangles, so that query never sees them, and the water's share of a shadow ray is `water_through`'s alone.

## 9. What the water job leaves undone (from `docs/agents/water-4.md` section 5; nothing here was changed by the merge)

1. **The denoisers' guides at a water pixel.** The tracer's own buffers hold the water's surface, wave motion, normal,
   roughness and albedo. **Ray Reconstruction still gets HDRP's depth and motion, which at a water pixel are the
   bed's** (the hand-off is TG Better Upscaling's file, another session's). The hit distance is a mix of two images
   (the mirrored world and the bed) by chance: expect ghosting of the weaker one under a moving camera. NRD's diffuse
   input leaves water out (compiled, never run). All of it is "written and compiled", none of it seen.
2. **The waterline.** Rock and sand against the waves are the nearest of two real surfaces, ray by ray: done. **Foam at
   the shore is not there** (HDRP's foam generators and water decals draw into a buffer the tracer does not have yet;
   whitecaps from the simulation are traced).
3. **The shadow at clear water's open edge with the Caustics row ON (the default) is "NOT JUDGED".** The water's
   straight shadow on dry ground holds 0.62 of the open floor's light; whether that is right has no reference. The
   fixture prints the line and judges nothing. In game: a shore where a water mesh ends in the air over dry ground.
4. **Memory.** The test lake's 240 MB of pages is not decided and was not measured in the game. New from this merge:
   56 MB for any water at all, and about 2.7 ms of render-thread time a frame for that lake (section 7).
5. **Four water slots.** A scene had 11 surfaces; the rest stay HDRP's. A limit that changes the picture.
6. Current maps, water masks and deformers are not read; glass inside a water path is shaded as a plain surface; far
   water at a low angle mirrors off the flat triangle; the caustics launch from the exact wave field while rays meet
   the triangles; a fast camera builds a few hundred pieces in one frame; a water mesh that ends in the air ends as a
   sheet with an edge; `water_medium` is "under the nearest surface straight above" (a cave under a lake is wrong);
   with the row off, a ray that slips under an open edge is counted as never in the water.

## 10. Workarounds met

Removed by this merge (each existed only for something the other side had already removed):
- `g_firstKnown` / `g_firstMaterial` in the speed job's `shade_path` loop (for the deleted `pt_water_under`).
- The old water passes' members in `release_scene_extras` and the memory line.
- The water job's second pipeline timing line and its second start-up wait in the test (main's serve).

Met and left (the water job's own list, `water-4.md` section 3, still stands):
- The Caustics row's "off" under water is a stand-in (the straight light), by the coordinator's decision; it exists
  only under "off".
- `passes_through` (a back face within 15 cm is passed) and `ray_offset` (2 cm) are G-buffer era rules still used for
  surfaces the tracer's own camera rays made; water is excepted from the first.
- The level water instances (`MASK_WATER`) stay for the caustics' launch.
- Ray Reconstruction's depth and motion at a water pixel are HDRP's (item 1 above).
- The four water slots.

## 11. In game: the scenes, and what their pictures and log must show

From `water-4.md` ("What the coordinator's two game runs must show"), with what this merge adds. Read the exposure and
gain lines before judging brightness: the water no longer goes through HDRP.

Once, at loading: `Shader cache: <path>`, one `Shader pass <name>: ...` line a pass, among them `waterMesh` and no
`water` or `waterUnder`; `Path tracing passes made in N s` (about 215 s from cold here, under 3 s after). The plugin
loads without "native ABI packing mismatch" (ABI 0x00012400: the plugin and the DLL must both be this build's).

`lantern-lake@dots` (no denoiser; the step "2-water-pass-off" is gone from the recipe with the pass):
- Log, once: `Water: "..." has its waves in the tracer (N bands, 64x64, ...)`. Native status line: `water: U pieces of
  its surface (T triangles, V vertices, M made this frame, K kept)` with U above 0, M falling to 0 within a second of
  standing still, and no "could not be made". No "water reflections" or "water shade" stage in "GPU time by stage".
- **New:** every `PT memory bytes:` line ends with "the water's surface holds N" (bytes) while there is water: write
  N down (59 million bytes is the least; the test lake had 240 MB of pages). After leaving the scene (`The plugin
  cleared the scene`) the next line must have no such ending.
- **New:** the render thread's time. If the frame time with water in view is worse than the GPU time explains, the
  water's recording is the suspect (2.7 ms for 1,376 pieces offline): compare U with that.
- Picture `1-all-on` (the mean of the frames): the lake reflects the lantern and the cave; the bed shows through near
  the shore and fades with depth; no isolated bright dots on open water (`flicker.py`). `3-caustics-off` against
  `1-all-on`: with the row off the bed under the lantern is lit by the lamp's straight light, not black, no bright
  lines; with the row on the light arrives bent into the pattern and a little dimmer. A bed that goes black with the
  row off means `light_mask` did not reach the build.
- `6-all-on-world-running`: the waves move and the reflection with them; no cracks (lines of bed colour through the
  surface) where the triangles change size.
- A decal that reaches over the water (blood, moss or a stain on the shore): it must not paint the water's surface, in
  the picture or in a reflection (the material of no kind refuses decals). On the bed, seen through the water, it stays.
  Not checked offline: no test has a decal over water.
- Glass near water (a bottle on the shore, a lantern's panes): as before the merge. Glass under water is shaded as a
  plain surface (item 6 above).

`water-day@quick` (no denoiser, then NRD):
- `2-traced-none`: the bed under shallow water, the far shore in the reflection at a low angle, the waterline
  following the rock. The sea at the horizon is still water if the log says "is traced as STILL water" for it.
- `3-traced-nrd`: `flicker.py --boil` on the water region against the game's own figure. Expect the water to be the
  noisiest thing in the picture (one path a pixel, a mix of two images). **This is the first run of NRD with water at
  all** (its front end was only compiled).
- A far river or lake that looks like the game's own water is one of the surfaces past the four slots.
- A shore where the water mesh ends in the air over dry ground: the "NOT JUDGED" shadow (item 3 above).
