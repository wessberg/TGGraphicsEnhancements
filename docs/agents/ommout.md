# ommout: the characters' micromaps trial comes out, the solid-submesh fix stays (PT-110 M4)

Worker `ommout`, copy `%TEMP%\tg-agents\wt\ommout`, pristine base `base20e`. Started 2026-10-11 ~21:20.
Brief: `docs/agents/briefs/brief-ommout.md`. No game, no deploy, no commit, no write outside this copy and
`%TEMP%\tgb\ommout-*`.

## State when stopped

DONE, 22:15. M4 is out of the code, the setting, the flag, the recipe, the source checks and the docs; M3 stays.
Source checks pass (116). The plugin and the native DLL are built (hashes under "Build and checks"). The one small
GPU check (`TGPT_TEST_MICROMAPS_ONLY=1`) passed. Nothing was run in the game: "built" and "passes the small check"
are the two claims here; "seen in game" is the coordinator's next step. What I expect there, not measured: one
tavern run with the tracer's time about that of the trial's "off" steps (36.2 ms on that build), and a "PT
cut-outs" line with the characters' cut-outs without a micromap and their plain triangles spared (109,800 then).
Left for the coordinator: merge this copy (`merge_copy.py` against `base20e`), and the ruling on the "Micromaps
trial" 16 (kept here as a yardstick; the brief's default was out).

## Why

The game answered the trial twice, alone on the card and in front (the brief's numbers):
- tavern, tracer time, off / on / off: 36.20 / 36.25 / 36.20 ms (run `20261011-172725`);
- town, a frame 87.69 / 87.23 / 86.97 ms, the tracer 67.81 / 68.17 / 67.40 ms (run `20261011-185528`).
No gain. AGENTS.md: a trial the game has answered comes out the same day.

## What is M3 and what is M4 (from the worker's diff, `base20omm` -> `ommchars`)

Read from `diff base20omm ommchars` over the 11 source files of `omm-chars-source.patch`. My copy has moved on
since (megageo-5 and -6, the NRD confidence trial, the fog), so nothing was reverse-patched blindly: see "How it was
taken out".

### M3: STAYS (one glass submesh no longer vetoes a character's solid marks)

| File | What |
|---|---|
| `native/pathtracing/src/scene.cpp`, `GpuScene::record`, `character_marks` | the loop that looked for a GLASS material and then gave no marks at all is gone: `if(!plainTested) for(...) marks[g]=... ALPHA_TEST ? 0 : 1;` with its comment |
| the same function, where the instance gets its flags | the comment only ("trial 16 sends its plain submeshes through the test too") |
| `native/pathtracing/shaders/pt_common.hlsli`, `caustic_clear` | `q.TraceRayInline(g_scene, RAY_FLAG_FORCE_NON_OPAQUE, MASK_ALL, ray);` and its two comment lines: the solved caustic's segments ask for candidates themselves, so they can still skip their own glass instance whole, solid parts too |
| `tests/source_checks.py`, `test_cutouts_are_counted_and_their_cost_can_be_bound` | the two lines that pin the above. Their comment said "M4": it is M3, corrected; and the old glass loop is now pinned as gone |

Checked while here: a glass material always carries the alpha-test flag (`SceneCapture.cs` 1440), so a glass
submesh itself is never marked solid by the rule above.

### M4: OUT (opacity micromaps for skinned characters, hidden `Character micromaps (trial)`)

| File | What came out |
|---|---|
| `native/pathtracing/include/tgpt_api.h` | `TGPT_FRAME_CHARACTER_MICROMAPS (1u << 20)` and its comment. A one-line comment says bit 20 is free |
| `src/TGPathTracing/NativeTracer.cs` | `FrameCharacterMicromaps` |
| `src/TGPathTracing/Settings.cs` | the entry `CharacterMicromaps` and its `file.Bind(Advanced, "Character micromaps (trial)", ...)` |
| `src/TGPathTracing/HdrpPasses.cs` | the line that set the flag from the setting |
| `native/pathtracing/src/tracer.cpp`, `trace` | `st.scene.characterMicromaps=...` and `set_strand_tiles(st.strandTiles)` |
| `native/pathtracing/src/scene.hpp` | `Mesh::skinMapSerial`, `skinMapScratchBytes`, `skinMapUpdateBytes`, `skinMapBlasBytes`; `SceneStats::characterMicromaps`; `GpuScene::characterMicromaps`; `set_strand_tiles`; `character_inputs`; `tileVersions_`, `alphaTileVersions_`, `tileVersionsDirty_`, `strandTiles_` (nothing else read the strand rows' copy) |
| `native/pathtracing/src/scene.cpp` | `kOmmBoundsHair`; `character_inputs`; `tileVersionsDirty_` in `texturesChanged`; the material signature's `hairAlpha`, tile generations and strand rows; `count_cutouts`' line for a mapped character; in the characters' loop the map serial, the prebuild query and the resize of the structure (with its `mega_fresh`), the map scratch sizes, the mapped structure as build target; the instance's `mapped` flags, `ommInstances`, `ommBlasVa`; `stats_.characterMicromaps`; `tile_settled`'s wider range and the generations in it and in `alpha_tile_settled`; `set_strand_tiles`; `omm_possible`'s `!mesh.skinned &&` (its comment is the one from before); the log's "Character micromaps trial: on/off"; in `record_omm` the skinned guard at the queue, the glass/camera-only skip, hair's bounds (`Bounds::material`, the wanted list, the `kOmmBoundsHair` dispatch, the bounds' UAV by material), root constant bit 31, a skinned mesh's geometry flags and build flags, `sized && !skinned` (twice), the map sizes after the build, `kept` for skinned; `clear`'s resets |
| `native/pathtracing/shaders/pt_omm_bake.hlsl` | every change: `omm_level`'s mask (bit 31 of `pass.b.z`), `OMM_BOUNDS_HAIR` and its branch in `bounds_step`, `omm_hair_alpha`, `hair_camera_state`, `detailTile`, `coverage`, the glass/camera-only "unknown" branch, the wider footprint for detail tiles, the non-finite alpha branch, the four lines after the cloth rule. The file is `base20omm`'s, byte for byte (SHA-256 `C5B86498...6DE5F0F5`) |
| `tests/ingame/scenarios.json` | the recipe `micromaps-characters` (8 steps) |
| `tests/source_checks.py` | the three `omm_possible` lines with `!mesh.skinned &&`; `mega_fresh(` counted 5 (4 again); the lines about a skinned mesh's build flags and `sized && !k.mesh->skinned`; and three pins the coordinator had turned to M4's text at the merge (`scratch_place(..., bytes)`, `SourceAccelerationStructureData=desc.Dest...`, the bake's `alphaTile || detailTile` footprint) |

So: a skinned mesh is never queued for a bake (no `omm_instance` call in the characters' loop, and `omm_possible`
refuses a mesh with hair, glass or a camera-only submesh as before). A character's structure is its plain one,
refitted, with M3's marks. Plants' and still meshes' micromaps: untouched code paths.

### Three things M4 changed in the bake that also touched other meshes: OUT with it

M4 wrote these for hair's readers, in the part of the bake every mesh goes through. Nothing measured them for the
world's meshes, and the brief says "nothing else changes", so the bake is the text from before M4:
- an alpha tile's texel counted covered above 0.001 and bare below 0.999; M4 made that exactly 0 and 1;
- an alpha tile's footprint got the half-texel guard (`OMM_GUARD`) on both sides;
- an alpha that is no number made the piece unknown.
In the game every alpha-tile material is also STRANDS (`SceneCapture.cs`), and a mesh with STRANDS gets no micromaps
again, so these lines run in the offline test alone. If the coordinator wants them as rules of their own, they are in
`ommchars`' `pt_omm_bake.hlsl` near lines 324 and 342.

### The "Micromaps trial" 16 (`TGPT_OMM_TRIAL_PLAIN_TESTED`): STAYS, as a yardstick

The brief's default was "out with M4". Why I kept it, to be overruled if wanted:
- It is not M4's and not the ommchars worker's. It came with the first M3 (worker omm-1, 2026-10-10) and covers the
  plants' groups as well as the characters (`scene.cpp`, two `plainTested`).
- It is the yardstick of the offline check `TGPT_TEST_OMM_ONLY` (`tests/pathtracing_omm_checks.inl`): "the same hits
  with the marks (0) and without them (16)". Without 16 that check has nothing to compare against, and it is the
  only pixel proof the marks' rule has. The part of M3 that stays from ommchars (solid parts beside glass) has no
  offline pixel check yet; 16 is what such a check would compare against.
- It costs nothing in the passes: two `bool`s on the CPU, no shader line.
- Taking it out means editing the plants' path and rewriting that check, which I may not run (my one GPU check is
  `TGPT_TEST_MICROMAPS_ONLY`).
What it would take to remove later: `tgpt_api.h` (the define), `scene.cpp` (two `plainTested`, the group signature,
`cutout_text`), `tracer.cpp` (the log), `Settings.cs` (the tooltip's 16), `scenarios.json` recipe `micromaps` steps 3
and 10, `pathtracing_omm_checks.inl` whole, and the source checks that pin them.

## How it was taken out

1. `pt_omm_bake.hlsl`: my copy's file was `ommchars`' byte for byte, so `base20omm`'s file was copied back.
2. `scene.cpp`, `tracer.cpp`, `HdrpPasses.cs`, `Settings.cs`: a three-way merge (`git merge-file`) of "what ommchars'
   copy would be without M4" (its base plus M3's two hunks) against `ommchars` into my copy, with the line endings
   made alike first (`ommchars`' `scene.cpp` is CRLF, the tree's is LF). All four merged clean; I read the
   resulting diff of `scene.cpp` against my base hunk by hunk (25 hunks, all M4).
3. `scene.hpp`, `tgpt_api.h`, `NativeTracer.cs`, `scenarios.json`: by exact replacements (another worker's lines
   stand next to M4's there), each asserted to match once.
4. Every file keeps the line ending it had (`scene.cpp`, `scene.hpp`, `pt_common.hlsli`, `round-20.md` are LF in
   the tree, the rest CRLF).
5. Afterwards no M4 name is left in `native`, `src`, `tests`, `tools` (grep of 19 names).

## Workarounds and leftovers met

- Removed: all of the above (nothing of M4 is switched off; it is gone).
- Met, not removed, not mine: megageo-5's block in `record_omm` has `|| k.mesh->skinned` in its guard
  (`scene.cpp`, "megaMapped"). It was written while M4 could bring a skinned mesh there. It is dead now, and it
  is the Mega Geometry trial's line, which this job may not touch.
- Met, left as it was before M4: a change of a character's marks builds its structure anew at the size its
  registration asked for (no new size query). M4 had added a query there for its map links; the first M3 (in the
  game since 10-10) never had one. Named here in case a driver ever sizes a structure by its geometry flags.

## Docs changed in this copy

`CHANGELOG.md` (M3 as the rule, M4 measured and out, what a second try needs), `docs/PT-REQUESTS.md` PT-110 (a new
first entry, two stale lines), `docs/PROJECT-CONTEXT.md` (9.2's entry shortened to what was learned; 9.12
`TGPT_FRAME_*`: bit 20 free; a DEAD ENDS entry), `BACKLOG.md` (one phrase of the speed line), `docs/rounds/round-20.md`
(the "not done yet", a worker addendum, the starter prompt's "hair's micromaps (M4)"), `docs/agents/omm-chars.md` (a
pointer at its top).

## What a second try needs (also in PT-110 and the CHANGELOG)

1. The bound first: the "Micromaps trial" 2 (the characters' cut-outs solid for a step; recipe `micromaps`, step 6)
   in a clean `-Measure` run. If the frame is no faster with them solid, no map can help.
2. Then one of: a proven bound on the camera's footprint over hair (it reads the strands through changing mips with
   an anisotropic footprint), a map for each reader (the camera's and the other rays'), or one reader for hair's alpha.
3. The probe's camera lookup made to agree (it passes covered hair layers, which is why covered hair stayed unknown).

## Build and checks

- Source checks: `python -I tests/source_checks.py`: 116 tests, OK (after the docs too). Log:
  `%TEMP%\tgb\ommout-build\source-checks-2.log`.
- Plugin: `src\TGPathTracing\bin\Release\net472\TGPathTracing.dll` in this copy, 0 warnings, 0 errors,
  SHA-256 `E8F94004FC224F789D42A666A707E6FDFEE639DCE53BE0E3F7B0E51987D47080` (`build-plugin.ps1`, `plugin.log`).
- Native: configured as caustcut's (`configure.cmd`: the main tree's SDKs read-only, NRD from the main tree's
  prebuilt `NRD.lib`, `-DTGPT_MEGA_GEOMETRY=ON -DNVAPI_ROOT=...\NVAPI-R615 -DTGPT_RAIN_WETNESS=ON` as the main
  tree's own build). Shaders: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\ommout-build` (never `-First`):
  57 compiled, 0 failed, 2,051 s behind the PC's shared limit (`shaders.log`). Then `build-native.cmd`
  (`TGPathTracing.Native` and `tgpt_gpu_test`): exit 0, no warning in the DLL's sources; the test's own source gives
  its 4 old double-to-float warnings (`build-native.log`).
  DLL: `%TEMP%\tgb\ommout-build\TGPathTracing.Native.dll`, 33,211,392 bytes, built 21:59:58,
  SHA-256 `54E32B8BEAD6795E523AA10B3AA3C8FA0218FE0B0BD3BC4D1858C13318EA2E8D`.
- The review diff against `base20e`: `%TEMP%\tgb\ommout-build\ommout.patch` (66 hunks; 145 lines in, 285 out).
- The small GPU check (`TGPT_TEST_MICROMAPS_ONLY=1`; `run-micromaps.ps1`, which waits for the main tree's
  `Run-PtHarness.ps1 -GpuCheck` to exit 0 and for no waiting game run, and ends its own test if the game opens):
  PASSED. It waited from 22:00:34 while the coordinator's game ran, started at 22:04:10 when the gate cleared, and
  ended at 22:11:36 with exit 0: 62 checks ok, none failed, "micromap checks passed" (`micromaps.log`,
  `run-micromaps.log`). On DirectX Raytracing 1.2 (the staged Agility runtime); the passes were made in 437 s (a
  cold pass cache). What it shows: three cut-outs that read the game's own texture trace with micromaps (3
  instances with them, 0 with them switched off), and the largest difference of any pixel between micromaps off
  and on is 0.00000 for the first texture, for another cutoff and for another texture; the maps are made anew
  at both changes. So the bake as it was before M4 still settles the world's cut-outs the same way as the alpha
  test. It says nothing about characters (none has maps now) nor about alpha tiles (not in this check).
  Its stage is `%TEMP%\tgb\ommout-stage`: my DLL, the main tree's staged bridge DLL (`artifacts\Native`, SHA-256
  `BAFE28DC...60C1A7E3`) and Agility runtime, copied.
- Not run, on purpose: the whole `tgpt_gpu_test`, ctest, `TGPT_TEST_OMM_ONLY` (M3's own check), any mutation
  check, the game.

## Seen, not mine, not run

- `tests/pathtracing_omm_checks.inl` (the check `TGPT_TEST_OMM_ONLY`, which I may not run) expects the log to hold
  `Micromaps trial: 0 (as it ships)`. Since round 20 the log line puts the leaves' level between the two
  (`Micromaps trial: 0 (leaves' and grass's micromaps at level 6, ...) (as it ships)`, `scene.cpp` `cutout_text`).
  Read from the code only: that expectation looks as if it fails today, with or without M4. To check when that
  test is next run.
