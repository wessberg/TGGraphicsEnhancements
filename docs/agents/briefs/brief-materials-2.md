# Brief: materials, second pass: what still looks wrong in the game (PT-73 d, g, h; PT-64) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("The tracer
fires its own camera rays, for every pixel", "The owner's word on the shortcuts list", "Do. not. fake. stuff.", "test
what catches defects and nothing more"). Report: `docs/agents/materials-2.md`; write it early, keep it current.

## Where things stand

A Claude agent wrote "programs" for seven of the game's own shaders (`native/pathtracing/shaders/pt_materials.hlsli`,
`src/TGPathTracing/MaterialPrograms.cs`; its report `docs/agents/materials.md`: read it whole, with "What is left";
its shader-reading tools are in `tools/research/`: `shader_pass.py`, `blob.py`, `census.py`). It died before any of it
ran in the game. It is in the game since 2026-10-07 22:46, and the first session is
`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-224654`. Read
`docs/agents/inspection-20261007-224654.md` (the coordinator's findings) and `inspection-20261007-224654-gpt.md` beside it.

What that session says of materials (the yardstick's lines in its `LogOutput.log` name the worst materials by name and
shader, with how each differs: place, colour, normal, roughness):
- Colour now matches where the surface is the same: forest 1 % of pixels differ (25 % before), vista 9 % (35 %).
- **Roughness still differs on 45 to 49 % of the outdoor pixels**, normals on 10 to 37 %.
- A caution before you read the vista's pictures: video memory ran out in that session and 869 textures were read as
  64x64 stand-in tiles (another worker fixes that). A flat, blocky or textureless surface in the vista may be that and
  not a program's fault. The yardstick's material lines and the log's "Shared textures" lines tell them apart; say
  which of the findings below are which.

## What to do, worst first

1. **Roughness and normals outdoors.** From the yardstick's lines of the forest and vista: for each of the worst
   materials, why its roughness or normal differs (the Cliff program's far layer looks polished: far rocks "like brown
   plastic" with bright streaks; the rock in the forest has no wet shine; grass and leaves), and fix the program.
2. **Terrain** (`TG/Environment/MeshTerrain`): the vista's ground is flat with no texture and a straight edge where the
   game blends (the forest's path is flat dark tarmac). Program or stand-in tiles: find out, fix what is the program's.
3. **`TG/Architecture/TriplanarArchitecture2`**: the balustrade is near white, the bridge's floor plain.
4. **The tavern's floor** (the game: narrow grey planks; traced: wide orange boards in another direction): tiling or
   the wrong texture coordinates in a program (`KF_Architecture`?) or in plain HDRP/Lit.
5. **What the dead agent left**: its "What is left" (characters' skin: 319 materials; `Dissolve3D`: 201; the snow
   shaders; thin cloth's share of light), and its two test gaps (`docs/agents/merge-4.md`: mutant mE has no clean pass,
   mF survives; an older glass check fails since its change). Skin is in the session too: a woman's skin is orange-red
   and dark where the game's is pale (`hair-moving/crop-head-frames.jpg`): find whether the material reads wrong or
   the scattering walk colours it (the walk is `pt_walk.hlsl`, another agent's work: say what you find, change the
   material side only).
6. Smaller: a tree's twigs bright yellow-green where the game's are dark; the rug's pattern nearly black; a bottle
   that looks metallic; the carpet and fur losing detail.

Not yours: memory and the stand-in tiles; lights' brightness (the lantern); flicker; glass's optics; geometry.

Judged (AGENTS.md "test what catches defects and nothing more"): build; a check for each program you change (its
output against the values you read from the game's shader), the whole GPU test ONCE at the end with its FAIL lines
written down; no mutation checks now. In game the coordinator runs the same scenes again: say what the yardstick's
roughness and normal shares should become, per scene.

## Clean as you go

What your work makes dead is deleted in this job; your report lists every workaround you met, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling; never give up a test for want of a slot. Build into `%TEMP%\tgb\COPYNAME-...`;
  NRD's configure writes into the shared SDK folder, so build with `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Other workers run now on: memory and start-up (`texture_share.*`, `TextureShare.cs`, pass creation in `tracer.cpp`),
  lights and the lantern (`SceneLights.cs`, `LightRules.cs`), flicker (the temporal passes). Keep out of those.
- The game's shaders are read, never copied: none of the game's code or decoded shader text goes into the tree.
- Keep tokens down: grep and read ranges of big files, never whole. Stop when done; ideas go in the report.
- Say if an export or struct changes (the coordinator sets the ABI number; main is 0x00012100). Option bits: none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
