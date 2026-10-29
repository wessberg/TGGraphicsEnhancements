# Brief: things the game moves are traced where they are this frame (PT-71 item 4) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("The tracer
fires its own camera rays, for every pixel", "Finish the migration first", "What moves is traced as it moves").
Report: `docs/agents/moving.md`; write it early (the design before the code).

## Why

The owner: "I expect visual regressions until the renderer is fully migrated. Focus on finishing the migration to our
own renderer, then we can A/B with vanilla and ensure we render everything." The list is `docs/PT-REQUESTS.md` PT-71;
yours is item 4. Since 16:50 every pixel's surface is the tracer's own camera ray's hit (`pt_primary.hlsli`;
`docs/agents/hair-6.md`). So the picture shows the traced scene itself, and the traced scene is as old as the plugin's
last scene scan: `src/TGPathTracing/SceneCapture.cs` scans every 2 s (`ScanInterval`; 0.5 s after the camera moved 4 m).
A thing the game moves that is no character (the weapon in the hero's hand, a door, a carried lantern, a cart, a
swinging sign) is drawn by the camera rays where it was up to 2 s ago. Characters are already posed every frame
(`CharacterCapture.cs`, `pt_skin.hlsl`).

## What to do

1. **Every frame, every moved thing at its place.** Find the cheapest exact way: the transforms of all traced
   renderers read each frame (Unity's `TransformAccessArray` or `hasChanged`), only the changed ones sent, the
   acceleration structure's instances updated without a rebuild of meshes. A thing that appears, disappears, is enabled
   or disabled between scans (a door's LOD, a weapon drawn or sheathed, a picked-up item) shows the same frame. Say what
   a frame costs with 2,000 and with 20,000 instances.
2. **Each moved thing's previous transform**, so its motion vector is its own (`motion_at` in `pt_common.hlsli`; the
   camera-ray report: "Missing: the previous transform of a thing the plugin moved"). The denoisers and DLSS place
   light by it.
3. **The first-person view:** the hero's hands, weapon and shield are drawn by the game with their own camera settings
   (field of view, near plane?). Find out how the game draws them and make the camera rays show them where the game
   does; say what you found.
4. The flag `TGPT_INSTANCE_MOVING` ("moved since the plugin's last scene update": rays distrust such geometry) goes
   where 1 makes it untrue.

Not yours: which objects are in the scene and their level of detail, swaying plants (another agent did that: read
`docs/agents/world.md` first, it changed the same file), characters, materials, water, glass.

## How it is judged

- Offline (`tests/pathtracing_gpu_test.cpp`): a moved box is hit where it is this frame, and its motion vector is its
  own; mutation-checked (a mutant's run ends at its FAIL line). The plugin's logic that can run without the game: a
  test like `tests/light_rules_tests.cs`. `python tests/source_checks.py` passes; the plugin compiles.
- In game the coordinator runs ONE scene: name it and its recipe, and the log line that proves it (how many
  transforms changed a frame, how old the oldest traced transform is: 0 frames).

## Clean as you go (the owner, 2026-10-07 17:55)

"Also remove all the 'hacks' and workarounds as we go, all the stuff we did as workarounds for not controlling the
tracing from the camera or whatever. So we have a clean base to work from." (AGENTS.md "Remove the workarounds as we
go".) What your work makes dead is deleted in this job, with its settings, tests and docs: not switched off, not kept
for an A/B, no branch for the old route (`own_rays()` false) kept alive in the code you touch beyond what still must
compile. Stay inside your part: things other jobs own are listed, not deleted. Your report has a section: every
workaround you met that exists only because the tracer started from HDRP's picture, with file and name, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling. Build into your own `%TEMP%\tgb\COPYNAME-...` folders, never `artifacts/`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files; build only
  `TGPathTracing.Native` and `tgpt_gpu_test`; source checks that fail from its work in progress are not yours (name them).
- Keep tokens down: grep and read ranges of big files (`SceneCapture.cs` is 166 KB, `scene.cpp`, `tracer.cpp`,
  `pt_common.hlsli`, the GPU test, `tests/source_checks.py`), never whole. Stop when done; list ideas in the report.
- Say if an export or struct changes (the coordinator sets the ABI number). Option bits are used up: take none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
