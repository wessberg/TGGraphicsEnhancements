# Character opacity micromaps (PT-110 M3/M4)

LATER (2026-10-11 evening): M4, the `Character micromaps (trial)`, gave no speed in two clean game runs and was taken
out by worker ommout (`docs/agents/ommout.md`). M3's fix stays the rule. What follows is this worker's report as written.

Started 2026-10-11 16:16 CEST. Worker copy `ommchars`, pristine base `base20omm`.
No main-tree writes, deployment, game launch, git mutation, install or download is permitted.

## Plan (16:20 CEST)

1. Read the vendor OMM guide and DXR update rules; inspect both hair alpha readers and material invalidation.
2. Explain the tavern's 109,800 solid character triangles still tested in shaders. Fix only when the glass/caustic behavior remains correct.
3. Add an off-by-default character-map trial. Bake UV-space maps once for the current mesh/material, retain their indices, and describe the same links at every posed build/refit. Rebuild when those links change.
4. Build the native trial and plugin in this copy/`%TEMP%/tgb/ommchars-build`, with existing SDKs read-only. Hand off one undenoised tavern comparison (off/on, frozen/running) before 18:16 CEST, or record the blocking reason.
5. Only after the game's answer, add/run this job's pixel comparison mode. No whole GPU test and no fixture before the game verdict.

## Initial evidence and decisions

- Read `BACKLOG.md`, PT context and DEAD ENDS, latest R20 handoff, the required AGENTS rulings, and omm-1's M1/M3 and complete “Written up, not built”.
- Main-tree run `20261011-161034`, native log: 567,982 cut-out triangles; characters 421,395 in 32 instances, 212,962 hair, none mapped; 109,800 solid triangles tested, zero spared. No new in-game result exists.
- `character_marks` disables every solid mark if any material is GLASS, to preserve `caustic_clear` skipping the entire glass instance. The log at trial 0 and this branch establish the M3 cause; see the implementation below.
- Hair has an additional correctness trap: the camera reads anisotropically filtered strand mips with a view/pose-dependent footprint; secondary rays read the alpha tile. A top-mip-only local strand bound does not prove a piece is bare for every camera footprint. The trial must retain unknown state wherever both readers cannot be proven to agree; no camera filtering change to make maps fit.
- Existing Micromaps trial values overlap internal leaf-level bits. The trial uses a separate, previously free frame flag at bit 20, with a hidden boolean. No new exported function or ABI record is planned.

## State when stopped

Stopped at the required game handoff (17:05 CEST): trial source and binaries ready, within the first two hours.
Native DLL and Mono plugin built; all 50 shader variants compiled with zero failures. The existing focused source
check passes, its in-memory solid-mark mutation is rejected, and the tavern recipe parses. No game or GPU test ran.
Part (c), pixel proof and after-counts, remains undone: the brief requires the game's answer first, and this worker
is forbidden to start the game or deploy. AGENTS.md's "The game is the first test" also says "builds no fixture
before the game has answered." No game answer was supplied. The report does not count that missing step as passed.
Mixed hair can remain entirely unknown; useful hair acceleration remains unproven, not completed.

## Implementation and limits (16:34)

- M3: the log plus `character_marks` establishes the cause: trial 0 and 109,800 plain triangles with zero marks can
  only occur when those mixed character meshes contain GLASS. A single glass submesh vetoed all marks. Removed
  that character-wide exception; `caustic_clear` now forces candidates so its existing whole-source-instance skip
  still applies to solid submeshes. The plant exception is untouched. Trial 16 still restores the old marks.
- M4: `Character micromaps (trial)` is a hidden Advanced boolean, false. `TGPT_FRAME_CHARACTER_MICROMAPS` and
  managed `FrameCharacterMicromaps` are frame flag bit 20. No exported function, ABI layout, option bit or
  FrameConstants row added. Legacy G-buffer mode does not use the trial.
- Internal CPU state: `Mesh::skinMapSerial`, map build/update scratch sizes and BLAS capacity; `GpuScene` copies
  existing strand rows and tracks color/alpha tile generations; `SceneStats::characterMicromaps` labels the log.
  `character_inputs` repeats array/index links at each rebuild/refit. Different links or geometry marks force a
  rebuild and a fresh prebuild size query. The existing deferred retirement protects old maps and index buffers.
- The mapped character BLAS uses ALLOW_UPDATE/PREFER_FAST_BUILD, unchanged cadence. Its indices stay alive;
  static-map BLAS compaction remains unchanged. Unsupported OMM hardware and the maps-off option use the plain BLAS.
- Glass/camera-only parts bake unknown instead of vetoing other submeshes. Hair combines secondary-ray alpha
  coverage with camera strand bounds. Camera bounds include every actual mip texel, alpha power/remap, clip and
  depth-prepass coverage. Bounds also cover arbitrary anisotropic footprints by taking the whole block's extrema.
  This is deliberately conservative: mixed hair textures can remain entirely unknown, so hair speedup is NOT
  claimed. It is the safe first build of the requested once-per-mesh/material map. Tighter bounds need a proven
  restriction on possible footprints, or separately linked maps for distinct readers; camera filtering is unchanged.
  Covered hair also stays unknown: `alpha_opaque`'s non-primary camera lookup deliberately passes HAIR_LAYER (the
  probe still uses it), so a hardware opaque answer would disagree with that reader. Bare hair needs both texture
  proofs. This first trial is not a claim that the remaining hair alpha-test cost has been removed.
- Bake-only addition: root constant `pass.b.z` bit 31 identifies characters (level now bits 28-30). Character
  translucent parts cannot take the plant-only opaque optimization because their instances use another ray mask.
  No additional code in the main walk, GI, specular or prepare passes; the sole common-shader change is the
  caustic query flag. The new functions are confined to the bake shader.

## Coordinator's first run

After merging/building and normal hash-checked deployment, ONE `tavern-room@micromaps-characters` run. Use
`-Measure` only with the usual announcement because it takes the foreground. Recipe: old solid tests frozen,
solid submeshes frozen, character maps frozen, maps off/on/off while running; 16 undenoised frames each.
The recipe ends with PT off and trial false. Inspect hair, eyes, armour, shadows/reflections; read `PT cut-outs`
after bakes settle. No claim about fixed pixels or saved milliseconds exists until that run.

## Vendor references

[NVIDIA OMM integration guide](https://github.com/NVIDIA-RTX/OMM/blob/main/docs/integration_guide.md): four-state
maps must agree with the runtime reader; dynamic mip selection needs conservative bounds over all applicable
mips. The trial preserves uncertain results rather than changing the camera's sampling.
[DXR specification, build flags](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html#d3d12_raytracing_acceleration_structure_build_flags):
position updates repeat their original OMM linkage. Changed linkage/geometry flags use a full rebuild here.
NVIDIA SDK source was neither copied nor quoted.

## Merge inventory

Only the character path and common bake were changed in `scene.cpp`; no plant structure functions were edited.
Functions touched there: `GpuScene::record` (material signatures, character marks/build/refit/instances and counts),
`tile_settled`, `alpha_tile_settled`, `omm_possible`, `record_omm`, `cutout_text`, `clear`.
New internal functions: `character_inputs` and `set_strand_tiles`. The header adds only the CPU fields described above.
`tracer.cpp`: `trace` sets the trial flag and supplies the strand rows only while that trial is enabled.
`pt_common.hlsli`: only `caustic_clear` (one ray flag and its explanation).
`pt_omm_bake.hlsl`: `omm_level`, `bounds_step`, `main`; new `omm_hair_alpha` and `hair_camera_state`.
Managed changes: one constant in `NativeTracer.cs`, one entry/binding in `Settings`, and one frame flag in the
existing `HdrpPasses` frame submission. `tgpt_api.h`: bit 20 only. No exported function, ABI record, FrameConstants
row or option bit is added. Bake-private root constant bit 31 and bounds dispatch mode `0xFFFFFFFC` are new.
`tests/ingame/scenarios.json`: `micromaps-characters`. `tests/source_checks.py`: existing expectations updated;
no new GPU fixture. `docs/agents/omm-chars-source.patch` contains only those 11 source/recipe/check files against
`base20omm`; merge it separately from this worker's BACKLOG/context/round notes. Build folders, bin/obj and build
logs are local outputs, not source to merge.

## Validation and handoff status

- Mono plugin: `src/TGPathTracing/bin/Release/net472/TGPathTracing.dll`, built, 0 warnings/errors.
  SHA256 `35B1975FA3B15987C8FB85BE9063CCAB692EB6792F4434C61D1E293618708041`.
- Native DLL: `%TEMP%/tgb/ommchars-build/TGPathTracing.Native.dll`, built at 17:03 CEST.
  SHA256 `E52E8693BED461ACC82C58D9AE41483C9EE1FBD039AEFB03181F1B6063500B2E`.
  All 50 shaders passed (17:02:34, 2,030 seconds under the shared guard), including the new bake under `-WX`.
  Native compilation/link passed without compiler warnings. `vcvars64.bat` emits a preexisting `vswhere.exe`
  lookup message; its exit was successful and CMake used MSVC successfully.
- `tgpt_gpu_test.exe`: built, 4 existing conversion warnings in its unchanged test code; never run.
- Existing focused source check: `SourceConsistency.test_cutouts_are_counted_and_their_cost_can_be_bound` passed.
  Its solid-mark branch replaced with `if(false)` in memory was rejected. This is a text check, not pixel proof.
- Build logs and exact configure command: `%TEMP%/tgb/ommchars-build/` (`configure-ommchars.cmd`, `plugin.log`,
  `compile-scene.log`, `build-test.log`, `build-native.log`, `shaders.log`, `source-omm-final.log`, `source-mutation.log`). No SDK installed or
  downloaded. NRD uses the main tree's prebuilt library read-only; no NVIDIA SDK source copied.

| PT cut-outs field | Before: 20261011-161034 | Trial after |
|---|---:|---|
| All cut-out triangles | 567,982 | Pending game |
| Character cut-outs / instances | 421,395 / 32 | Pending game |
| Hair triangles | 212,962 | Pending game |
| Character cut-outs linked to maps | 0 | Pending game |
| Plain character triangles shader-tested | 109,800 | Pending game |
| Plain character triangles spared | 0 | Pending game |
| All cut-out pieces settled | 16.6% | Pending game |

No speed or picture-equivalence claim. Character maps being linked is not evidence that hair pieces became known.
After the coordinator's game verdict, the focused GPU comparison must include mixed solid/glass characters,
alpha-tile cut-outs, divergent strand/alpha textures, alpha power/remap, changing pose, and tile/material changes.
Compare map-off/on/off pixels without a denoiser and mutate the reader intersection or linkage to prove it fails.
Run only that new mode, with `TGPT_TEST_STARTUP_SECONDS=900`, after the main tree's `-GpuCheck` exits 0 and the game
is closed. The existing whole test and existing multi-feature modes are not this worker's permission to run them.
