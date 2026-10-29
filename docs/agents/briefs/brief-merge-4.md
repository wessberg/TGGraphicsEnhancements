# Brief: merge four finished jobs into one tested tree (PT-71 items 1, 3, 4, 6) - one worker, a merge and nothing else

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`: the main tree of 2026-10-07 21:50). Follow `AGENTS.md` there; read "The
owner's rulings" ("The tracer fires its own camera rays, for every pixel", "Finish the migration first", "Remove the
workarounds as we go") and `docs/AGENT-WORKERS.md` (how copies and merges work here). Report: `docs/agents/merge-4.md`;
write it early and keep it current.

## Why

Four workers did four jobs in four copies of the tree, each from an older base. Claude's weekly limit stopped three
of them on 2026-10-07 at 20:15: one had reported, two were in their last test runs (their queued runs have ended
since); the fourth, a GPT-6-astra worker, finished at 21:25. Their work sits unmerged in a scratch folder that may be
cleaned. It must become ONE tree that builds and passes, so the coordinator can copy it into the main
tree in one step. `<scratch>` below is `...\scratchpad` (the folder above `wt`).

| Copy | Base | Job | Its report (in that copy) | State |
|---|---|---|---|---|
| `wt\port2` | `wt\base35` | skin and wax scattering by a walk, the wet ground, the hero's head, all at the camera ray's own hit; the old route's versions deleted (PT-58) | `docs/agents/port-2.md` | finished and reported; a few mutant runs may still be going |
| `wt\mats` | `wt\base36` | what a hit reads of seven of the game's own shaders ("programs" in `pt_materials.hlsli`), no texture copies, no borrowing from the screen (PT-64) | `docs/agents/materials.md` | died in its last runs (a clean test `i`, a mutant `mG`); read its "State" and "What is left" |
| `wt\moving` | `wt\base39` | things the game moves traced where they are each frame, with their own motion (PT-71 item 4) | `docs/agents/moving.md` | died in its mutant runs; read its state section |
| `wt\glass` | `wt\base37` | glass, eyes and forward-drawn things as surfaces of the camera rays; the old passes removed (PT-71 item 6) | `docs/agents/glass-eyes.md` | finished: 10 raw GPU checks, 12 mutants; it names remaining gaps (one it calls PT-72) |

Your copy already holds, beyond those bases: the whole-world merge (plants in the wind, the drawn level of detail, no
distance cuts; ABI 0x00012000) and the effects inventory (harness only).

## What to do

1. **Before reading a copy as a source:** make sure nothing still edits it. No process may have that copy's work
   scripts in its command line (`<scratch>\p2\mutants.ps1` for port2, `<scratch>\mw\` for mats, `<scratch>\moving-work\` for
   moving): wait for them to end with ONE background wait (never kill them: they are not yours). Then check that no
   mutant is left applied: read each job's mutant script to learn how it marks one (port2 leaves `*.p2orig` files), and
   compare a mutated file's hash against what the report says if in doubt.
2. **Merge in this order: port2, then mats, then moving, then glass**, each with your copy's own tool, which merges into the copy it
   lies in: `python tools/agents/merge_copy.py port2 --base=base35 --root=<scratch>` (first with `--dry`). After each:
   resolve every conflict so that BOTH sides' work stays (read both reports where they meet: `hit_material` and
   `pt_common.hlsli` are where port2 and mats both worked; `SceneCapture.cs` where mats, moving and the world merge
   did; `pt_primary.hlsli`, `pt_compose.hlsl` and the removal of `pt_hdrp_shadow.hlsl` where glass meets port2), build, run the whole GPU test, run the source checks, before the next merge. A job's deletions count
   (port2 deletes `pt_sss.hlsl` and two exports, moves the wave functions into `pt_waves.hlsli`).
3. **The coordinator's files stay as your copy has them**: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`,
   `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`. Before each merge put the source's base version of these over the
   source copy's? No: you must not write to the source copies. Instead, after each merge, restore these five from
   `wt\BASENAME` and list in your report what each job had added to them (the coordinator files that).
4. **One ABI number for the four**: `TGPT_ABI` 0x00012100, in `native/pathtracing/include/tgpt_api.h`,
   `src/TGPathTracing/NativeTracer.cs` and `tests/source_checks.py` (its comment block lists each number's reason: add
   one line). Struct sizes and FrameConstants as the merged code has them; every pinned size agreed between the header,
   the plugin's `Validate`, the shaders and the source checks.
5. **The jobs' scripts worth keeping**: copy the materials job's shader-reading TOOLS (`<scratch>\mw\shader_pass.py`,
   `blob.py`, `census.py`, and what they need) into `tools/research/` of your copy with a header line saying what each
   does. Tools only: none of the game's decoded shader text, none of its code.
6. **Finish only what the dead agents had left of their own tests**: if a mutant of theirs had not run, run it
   (their scripts say how) or say it was not run. Build no new feature. Fix what the merge breaks.
7. **Report**: per merge, every conflict and how it was resolved; the final test numbers (GPU test: ok and FAIL
   lines; the lake's timing check read on a GPU you shared with nobody if you can, else say so; source checks; the
   plugin and the world rules test `tests/WorldRules.Tests.csproj`); the ABI and struct table; what each job leaves
   undone (from its report); the in-game scenes each job names (one a part), for the coordinator to run; and the list
   of workarounds the four reports named, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or to the four source copies. GPU
  tests only while the game is closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is
  absent or its process gone, and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run
  side by side). Wait with one background script, never by polling. Build into `%TEMP%\tgb\COPYNAME-...`; NRD's
  configure writes into the shared SDK folder, so build with `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files; in
  `CMakeLists.txt` only the path tracing shader and test lists are yours.
- Keep tokens down: grep and read ranges of big files, never whole. Other workers run now in `wt\water3`, `wt\decals`, `wt\vfx`: not yours.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
