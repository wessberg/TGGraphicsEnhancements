# Brief: finish the fix for the stair-step edges (PT-84) - one agent, a finisher

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\edges`
(pristine base: `...\scratchpad\wt\base55`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/edges.md` in the copy (it exists: keep it, fill its "State" section first, keep it current).

## Where the job stands

The agent before you found and measured the cause, chose a design, started writing it, and was stopped by a usage
limit in the middle ("add the check to the GPU test" was its last step). Its report says nothing about what it had
written. Read first:

1. `docs/agents/edges.md` in the copy, whole (90 lines): the cause (the camera's rays carry no sub-pixel shift: the
   plugin reads HDRP's matrices after HDRP took the shift out), the numbers, the table of which frame each thing
   belongs to, the three candidates, the choice (trace and show in the same frame, with the shift fixed first).
2. `docs/agents/briefs/brief-edges.md`: the first brief; its rules hold for you.
3. What the copy holds against the base: `python tools/agents/merge_copy.py edges --base=base55 --root=<the scratchpad
   folder> --dry` run from the MAIN tree lists the files (8: `tgpt_api.h`, `d3d11.hlsl`, `pt_common.hlsli`,
   `pt_compose.hlsl`, `pt_prepare.hlsl`, `pt_primary.hlsli`, `tracer.cpp`, `HdrpPasses.cs`); diff each against
   `wt\base55` (`git diff --no-index`), never read those files whole. Write into the report's "State" what each edit
   is and whether it is whole or half made, BEFORE changing anything.

## What to do

1. Decide from the diffs: finish what is there, or cut it back to a clean step. In this order, each a step that builds:
   - **A. The camera's rays carry the shift the upscaler is told** (the matrices read where they still hold it, or
     the shift applied from `taaJitter`), with the comments that say "jittered" made true. Alone it is not enough
     (the light shown in frame N was traced for N-1), so say in the report what the picture is expected to do with A alone.
   - **B. Trace and show in the same frame** (the first agent's choice), if its edits are far enough to finish
     safely. It deletes workarounds (the composite's motion vectors, distance key, neighbour search, random fill):
     list each one removed or left. Its cost is a wait on the D3D11 side: say how to measure it in the game.
     If B cannot be finished cleanly in this job, leave A whole and B as a written plan, and say so plainly.
2. Ray Reconstruction's route (TG Better Upscaling's `StpHost.cs` and the guides): keep it consistent with what you
   change (colour, guides and the told shift of ONE frame); say what you checked.
3. One small check of your own in the offline test: a still scene, a thin slanted bar and a cut-out, the camera's
   matrices handed over the way the GAME hands them (the first agent found the test builds its own shifted matrices
   and so never saw the bug: the check must fail on the base). Minutes, not the whole suite.
4. For the coordinator's ONE game scene (`forest` with `quick`): what to look at and what the numbers should
   become; `tools/harness/edges_shift.py` (the first agent's script, in the copy) measures the grid and the slide.

## Rules

- START NO GPU TEST until the coordinator tells you the card is free for workers (the owner asked for the card).
  Reading, diffs, code, shader compiles (`tools\Build-PtShaders.ps1 -BuildDir <yours> -Throttle 8`) and the native
  and plugin builds are fine. If only the GPU check is left: its exact command line in the report, then stop.
- Never the whole GPU test; never the game, a deploy, a commit, the main tree or the game folder.
- If the ABI changes (`tgpt_api.h` is among the edited files): say so at the top of the report; plugin and native
  must then be installed together.
- No stand-ins: no blur, no sharpen, no filter that hides the grid.
- RTXDI and NRD sources never copied or quoted; Unity/HDRP: learn, copy nothing (a decompiled reading copy stays in
  scratch and is deleted when done).
- Final message: five lines at most: what is whole, what is built, what is not run (command line), the report's path.
