# Brief: subsurface scattering, the wet ground and the hero's head onto the camera-ray tree (PT-58) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\port2`
(pristine base: `...\scratchpad\wt\base35`, the main tree at 17:03). Follow `AGENTS.md` there; read "The owner's rulings"
first ("The tracer fires its own camera rays, for every pixel", "Do. not. fake. stuff.", "Judge the picture without a
denoiser first"). Report: `docs/agents/port-2.md`; write it early, keep it current. Porting finished work: build
nothing the three jobs did not build, except what item 2 below needs.

## Where things stand

- Since 16:50 the main tree (your copy) traces the camera's first surface itself: `pt_primary.hlsli`, every pixel's
  surface is the tracer's own camera ray's hit (`docs/agents/hair-6.md`: read its design section and "what to file").
  In game the first hit matches HDRP's on 99.2 % of the screen (the yardstick: `pt_yardstick.hlsl`).
- Three finished jobs were ported onto the tree of 15:06 by another agent, in `...\scratchpad\wt\merge3` (its base:
  `wt\base33`; its report `docs/agents/merge-3.md` there: read it whole, it is short): subsurface scattering (a random
  walk inside wax and skin, thin things by their own material; `pt_walk.hlsl`, `pt_sss.hlsl` deleted), the hero's head fix
  from the mirror job, and the wet ground's memory and optics (`pt_wet.hlsl`, `pt_waves.hlsli`). All checks passed there
  (1269). Mind its warning: a GPU hang on the first frame came back when the walk and the wet store's read met in one
  pass; a flag `g_hitInside` keeps them apart; the cause is not understood.
- A trial merge of `wt\merge3` against `wt\base33` into today's main: 21 conflicts (`tracer.cpp` 8, `source_checks.py` 4,
  `pt_common.hlsli` 2, `CMakeLists.txt` 2, `pt_caustics.hlsl`, `pt_shaders.hpp.in`, `Settings.cs`, the GPU test,
  CHANGELOG 1 each), a file deleted (`pt_sss.hlsl`), and the wave functions moved from `pt_caustics.hlsl` to
  `pt_waves.hlsli` while the caustics agent edited them in main (its edits must move too). The tool:
  `python <scratch>\merge_eol.py merge3 --base=base33 [--dry]` merges into the MAIN tree, which you must not write to:
  copy its logic (three texts as LF, `git merge-file`, the target's own line endings back) for your copy, or merge by hand.
  Line endings differ between copies and main for some files: keep each file's line endings as your copy has them.

## What to do

1. Bring the three jobs' changes into your copy and resolve every conflict so that both sides' work stays. Deletions
   count (`pt_sss.hlsl` goes). `CMakeLists.txt`: only the path tracing shader lists are yours; another session edits the
   rest of that file.
2. Make them work on the camera rays: the hit a camera ray finds must get what the old route's pixel got. The camera-ray
   agent named what a hit does not read yet: "skin's scattering, leaves' transmittance". So: the walk runs for the
   camera's own hit on skin and wax as it does at path vertices; wet ground shows at the camera's hit (it is applied in
   `hit_material`, so it may already); the hero's head rule holds for camera rays, mirror rays and bounces alike. Say
   plainly what does not work yet.
3. Build and install into your own `%TEMP%\tgb\port2-...` folders. `tgpt_gpu_test` first after every merge step (the
   hang): every check passes on the final build. The checks the jobs brought stay, pointed at the camera-ray route where
   they tested the old one. `python tests/source_checks.py` passes; the plugin compiles.
4. Report: what was merged and how each conflict was resolved, what works at the camera's hit and what does not, the
   final test run, the ABI/struct changes in total (main is at 0x00011F00; the coordinator sets the number), the cost of
   each job's stage, and ONE in-game scene and recipe per job (the owner's rule; judged with Denoising "None" first).

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files; build only
  `TGPathTracing.Native` and `tgpt_gpu_test`; source checks that fail from its work in progress are not yours (name them).
- Keep tokens down: grep and read ranges of big files, never whole. Stop when done; list ideas in the report.
- Others work in their own copies now: materials against the yardstick (`wt/mats`), the whole world traced (`wt/world`).
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
