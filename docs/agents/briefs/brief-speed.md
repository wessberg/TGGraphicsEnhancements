# Brief: why the tracer got five times slower in the tavern, and taking the waste out (PT-73 c, PT-08) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`: the main tree of 2026-10-07 23:40). Follow `AGENTS.md` there; read "The
owner's rulings" first ("No limits that change the picture", "No shortcuts": correct first, speed after, WITHOUT
changing the result; "test what catches defects and nothing more"). Report: `docs/agents/speed.md`; write it early.

## Why

Today's merges made the tracer much slower, measured in the game on the same scenes and card (the plugin's status line
"GPU n ms" in each run's `LogOutput.log`; runs in `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\`):

| Scene | 16:59 (`20261007-165954`: the camera rays alone) | 18:59 (`20261007-185958`: plus the whole world) | 22:46 (`20261007-224654`: plus scattering, material programs, moved things, glass and eyes) |
|---|---|---|---|
| tavern (`hair-moving`) | 12 ms | not measured there | 60 to 68 ms |
| forest | 66 ms at 17:15 | 73 ms | 89 to 97 ms |
| vista | - | 31 to 35 ms | 44 to 48 ms |

The owner's target is 55 fps and "as performant as Cyberpunk's PT"; the owner also ruled that the migration to the
tracer's own renderer comes first and speed after. This job does not chase the target: it finds what today's work
added that buys nothing, so the builds that follow can be judged at a frame rate a person can look at. Indoors five
times slower for work that should cost little in a room is waste somewhere.

## What to do

1. **Measure by stage, old against new.** The native log has "GPU time by stage" in the F8 report and stage times in
   its status lines (grep "stage" in `native/pathtracing/src/tracer.cpp`; `TGPT_GetStageTimes`). In those three runs'
   `TGPathTracing.Native.log`, and offline with `tgpt_gpu_test` (builds of the bases are in the scratch folder's copies:
   `wt\base35` is the 17:03 tree, `wt\base39` 18:46, your copy 23:40; build each into its own folder and run the same
   scene back to back, several times, because other workers share the GPU: report medians and say how noisy they are).
   Which stages grew, by how much, in which scene.
2. **Find the cause of each growth** in the code those merges brought (`docs/agents/merge-4.md` lists them): the
   scattering walk (does it run for pixels that are not skin or wax? how many steps and rays a pixel?), the material
   programs (texture reads a hit: how many for a rock, the ground with its eight layers; are they taken at mip 0 for
   secondary rays?), glass and eyes (roulette depth; `trace_closest` asking for every triangle), moved things (a rebuild
   of the top-level structure or of meshes each frame where a refit would do), plants' groups (refits a frame), the
   camera ray's walk (any-hit work for every triangle on the ray), shaders so large the compiler stops inlining.
3. **Take out what is waste without changing the picture**: work done for pixels or rays that cannot use it, the same
   value computed twice, a rebuild where nothing changed, a texture read whose result is multiplied by zero. Each fix
   with its before and after by stage, and a check that the picture is the same (the GPU test's existing checks are
   that check; an image difference of zero where you claim "unchanged").
4. **What is left, as a list with numbers** (what each remaining cost buys, and what could make it cheaper later
   without changing what the picture converges to), for the owner's later speed work. No quality trade is made here.

Judged (AGENTS.md "test what catches defects and nothing more"): build; the stage table before and after; the whole GPU
test ONCE at the end with its FAIL lines written down; no mutation checks now. In game the coordinator runs
`hair-moving@quick` and `forest@raw` and reads the status line's GPU time: say what to expect.

## Clean as you go

What your work makes dead is deleted in this job; your report lists every workaround you met, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling; never give up a test for want of a slot. When you end, leave no waiting shell
  behind. Build into `%TEMP%\tgb\COPYNAME-...`; NRD's configure writes into the shared SDK folder, so build with
  `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Eight workers run at once tonight; several change the files you will read (memory and start-up: pass creation and
  textures; materials: `pt_materials.hlsli`; lights and flicker: the temporal passes; the candle scene: `pt_walk.hlsl`;
  water; glass and hair). Keep your edits small and local, and say in your report exactly which lines of shared files
  you changed, so the merge is easy.
- Keep tokens down: grep and read ranges of big files, never whole; write your report early so nothing is lost if you
  are cut off. Stop when done; ideas go in the report.
- Say if an export or struct changes (the coordinator sets the ABI number; main is 0x00012100). Option bits: none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
