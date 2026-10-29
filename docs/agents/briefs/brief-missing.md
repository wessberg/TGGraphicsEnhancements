# Brief: things the game shows that are missing from the traced world: say for each what it is and why it is left out (PT-83) - one worker, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/missing.md`; write it early and after every step.

## Why (the owner, 2026-10-07)

"that sounds like a big problem, if things suddenly disappear when PT is enabled. Fix that". Everything the game
draws is in the traced world: no object left out, at any distance.

## What is missing (the inspection of 2026-10-08, `docs/agents/inspection-20261008.md` rows 16, 44, 57, 113, 114, 115, 15; crops in the MAIN tree's `artifacts\inspection-20261008\`, read only)

| scene (`tests/ingame/scenarios.json`) | what | crop |
|---|---|---|
| `vista` | a tree's whole purple canopy is gone: bare pale branches | `vista-purpletree.jpg` |
| `npc-day` | a patch of tall grass, a cabbage and leafy plants is bare ground (8 % of the screen) | `npcday-rightground.jpg` |
| `npc-candle` | leaded windows have no lattice and no glass: an open hole to the night | `npc-candle-window.jpg` |
| `stairwell` | a stained-glass rose window is a plain blue glow; a statue is a black column, its glowing heart a faint dot; a ghost (a see-through glowing person) is a faint grey shade | `stairwell-window.jpg`, `stairwell-statue.jpg` |
| `forest` | far wooden structures the game shows through the fog are nearly gone | `forest-npc-zoom.jpg` |

Nothing is known of why. What each thing is in the game (its renderer, its shader, how it is drawn: a mesh
renderer, a statically batched one, the game's ECS renderer "Drake", instanced vegetation, a decal, a transparent
or unlit material, a VFX effect, a camera-facing card) can only be read in the running game.

## The job: a diagnostic the coordinator runs ONCE, then stop

1. Read how the plugin gathers the traced world (`src/TGPathTracing/SceneCapture.cs` and what it calls: which
   renderers it takes, which it REJECTS and by what rule: layer, shader, queue, material flags, bounds, distance,
   level of detail, hidden or disabled states) and how the harness's existing recipe `effects` lists things
   (`docs/agents/effects-inventory.md`). List the rejection rules in the report with their line numbers: each is a
   suspect.
2. Build a harness recipe `missing` and what it needs in the plugin (harness-only code, off unless the recipe asks):
   for a screen box given per scene (the table's crops: take the boxes from the inspection's rows; put them in the
   recipe or beside the scenarios), it logs every renderer of the game whose bounds project into the box and that
   the game's camera would draw (meshes, skinned meshes, Drake entities, terrain details and trees, decals, VFX,
   transparents), each with: its name and path, its type, its mesh and triangle count, each material's shader name,
   queue and the keywords that matter, whether it is in the traced scene this frame (and under which mesh and
   instance), and if not, WHICH RULE left it out. The analysis writes a table per scene (`missing-report.md`) with
   the left-out things first, largest on screen first.
3. Its own offline test of the analysis (the pattern: `tests/harness_analyze_test.py`), with deliberate
   corruptions.
4. STOP THERE: your final message is the hand-over for that one run (the command, what the report will show). Do
   not guess fixes before the game has answered. If, reading the capture code, you find a rule that certainly
   drops one of the five (say the leaded window's material is a transparent queue the capture refuses), write it
   in the report as a finding with its evidence; still build the diagnostic.

## Rules

- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"). This job should need no GPU
  test at all.
- Never the game, a deploy, a commit, the main tree or the game folder. Build the plugin and the harness in your
  copy (0 errors, 0 warnings); the native side only if you must change it (`%TEMP%\tgb\COPYNAME`,
  `-DTGPT_WITH_NRD=OFF`, `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` first). `python
  tests/source_checks.py` and `python tests/harness_analyze_test.py` pass.
- The diagnostic changes nothing a player sees and nothing the tracer traces. Test runs are of path tracing alone:
  no step of the recipe sets `neural`.
- Never touch a game Setting type with a localizing static constructor at plugin load. BepInEx config section and
  key names may not contain `= \n \t \ " ' [ ]`.
- Never read `tracer.cpp`, `scene.cpp`, `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep
  them so.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: six lines at most: the recipe and the one run's command, what its report shows, what you found
  by reading alone, the files changed, that everything builds and the checks pass, the report's path.
