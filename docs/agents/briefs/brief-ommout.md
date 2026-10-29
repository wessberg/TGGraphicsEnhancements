# Brief: ommout - the characters' micromaps trial comes out; its solid-submesh fix stays (PT-110 M4)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-11 21:20). Follow `AGENTS.md` in your copy;
read first "Code that is switched off still costs frame time" (a trial the game has answered comes out THE SAME DAY) and
"Remove the workarounds as we go". Report: `docs/agents/ommout.md` IN YOUR COPY, written early; "State when stopped" at
once if a limit shows.

## Why

The worker ommchars (`docs/agents/omm-chars.md`; `docs/PT-REQUESTS.md`, grep "### PT-110") built two things:
- M3, the fix: one glass submesh no longer vetoes the solid marks of a whole character. It is the rule. IT STAYS.
- M4, the trial: opacity micromaps for skinned characters, the hidden `Character micromaps (trial)` (frame flag bit 20,
  `TGPT_FRAME_CHARACTER_MICROMAPS`, managed `FrameCharacterMicromaps`). The game answered twice, nobody else on the card,
  the game in front: the tavern 36.20 / 36.25 / 36.20 ms of tracer time (off, on, off; run `20261011-172725`) and the town
  a frame 87.69 / 87.23 / 86.97 ms, the tracer 67.81 / 68.17 / 67.40 (`20261011-185528`). No gain. So M4 comes out.

## What to do

1. List what M4 added, from the worker's diff: its pristine base is `%TEMP%\tg-agents\wt\base20omm`, its copy
   `%TEMP%\tg-agents\wt\ommchars` (read-only for you). Its report's "Implementation and limits" names the parts:
   `Mesh::skinMapSerial`, the map build and update scratch sizes and BLAS capacity, `character_inputs`, the strand rows'
   copy (`set_strand_tiles`, `strandTiles_`, unless something else reads it: grep), the colour and alpha tile generations
   kept for it, `SceneStats::characterMicromaps`, the bake shader's root constant `pass.b.z` bit 31 and what it switches
   in `pt_omm_bake.hlsl`, the setting, the frame flag, the plugin lines, the recipe `micromaps-characters`, its source
   checks and docs lines. Separate M3's lines (`character_marks`, the removed character-wide exception, `caustic_clear`
   forcing candidates, the "Micromaps trial" 16 that restores the old marks: say whether 16 is still needed as a
   yardstick or comes out too; default: it comes out with M4, the marks' rule stays).
2. Remove M4 from your copy: code, setting, flag (bit 20 becomes free: say so in `docs/PROJECT-CONTEXT.md` 9.12, the
   line "TGPT_FRAME_*"), recipe, source checks, the log's "Character micromaps trial" words. Skinned meshes are again
   never queued for a bake (as before M4), and nothing else changes: plants' and static meshes' micromaps as they are.
3. Add to PT-110 in `docs/PT-REQUESTS.md` and to `CHANGELOG.md`: what was measured, that M4 is out, and what it would
   take to try again (the report's own limits: hair stays "unknown" under its conservative bounds, so the alpha test's
   cost on hair was never removed).
4. Build the native DLL and the plugin, run `python tests/source_checks.py`, and the ONE small check that covers
   micromaps (`TGPT_TEST_MICROMAPS_ONLY=1`), only while the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0.

Not yours: RTX Mega Geometry (`mega_geometry.*`, the "Mega Geometry trial"), the fog grid (worker fogfix), NRD.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. NEVER the whole `tgpt_gpu_test` or
  ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\ommout-...`, configured as the vendor-rr worker did (`docs/agents/vendor-rr.md` "Build isolation") and with
  `-DNVAPI_ROOT=C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\NVAPI-R615 -DTGPT_MEGA_GEOMETRY=ON`. For any `dotnet` call
  set `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true` and `MSBuildEnableWorkloadResolver=false` first. Grep and read
  ranges of the big files (`scene.cpp`, `tracer.cpp`, `pt_common.hlsli`, `tests/pathtracing_gpu_test.cpp`,
  `tests/source_checks.py`).
- Final message: five lines at most, pointing at the report.
