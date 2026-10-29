# Brief: glass, eyes and everything HDRP draws forward become surfaces of the camera rays (PT-71 item 6) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("The tracer
fires its own camera rays, for every pixel", "Finish the migration first", "Do. not. fake. stuff.", "A mirror must show
the world as the camera sees it"). Report: `docs/agents/glass-eyes.md`; write it early (the design before the code).

## Why

The owner: "I expect visual regressions until the renderer is fully migrated. Focus on finishing the migration to our
own renderer, then we can A/B with vanilla and ensure we render everything." The list is `docs/PT-REQUESTS.md` PT-71;
yours is item 6. Since 16:50 every pixel's surface is the tracer's own camera ray's hit (`pt_primary.hlsli`;
`docs/agents/hair-6.md`: read "The camera-ray design" and "For the next agent"). Three things on screen are still not
ordinary surfaces of those rays:

1. **Glass.** A glass surface is "seen through" by a special case carried over from the old route (`pt_primary.hlsli`,
   grep "glass"; `pt_compose.hlsl`; the plugin's `GlassFinish.cs`, HDRP's glass hidden; rays cross a capped number of
   panes). Wanted: the camera's ray meets glass as a surface that reflects and refracts by its own index of refraction,
   tint and roughness, and goes on through as many panes as there are (a limit may cost noise, never change the
   picture); a bottle behind a window behind a jar is right. What a mirror ray or a bounce meets is the same glass.
2. **Eyes.** Marked `MATERIAL_EYE`, lit on their mesh, with parts shaded by HDRP and traced shadows
   (`pt_hdrp_shadow.hlsl`, the share path in `pt_compose.hlsl`). Wanted: an eye is a surface of the camera ray (the
   cornea a clear refracting layer over the iris, as the game's eye shader shows it: `docs/PT-DETAILS.md` has the
   decode of the game's eye shader), lit only by the tracer.
3. **What HDRP draws forward** (the yardstick's "drawn forward" share; some props, unlit signs, HDRP's Unlit): the
   tracer's own hit and its light, nothing of HDRP's shading.

Then remove what this makes dead, one at a time, each only after its replacement passes its check: the glass special
case, `pt_hdrp_shadow.hlsl` and its plugin pass (`ShadowDetailPass`, `EventShadowDetail`), the share path in the
composite. Say plainly what you could not remove and why.

Not yours: water (another agent), particles and transparents that are not glass (smoke, fire, sprites: a later job),
hair (done), which objects are in the scene, how a material's textures are read (the materials job).

## How it is judged

- Offline (`tests/pathtracing_gpu_test.cpp`), with Denoising None: a jar behind a pane behind a pane; a glass's rim
  and tint; an eye in a mirror looks as it does on screen; each check mutation-checked (a mutant's run ends at its FAIL
  line). `python tests/source_checks.py` passes; the plugin compiles.
- In game the coordinator runs ONE scene per part: name the scene and recipe (`glass-jars`, `refract-jar`,
  `npc-day`, `hair-eyebrows` exist in `tests/ingame/scenarios.json`) and what its picture and log must show.

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
- Keep tokens down: grep and read ranges of big files (`pt_common.hlsli`, `tracer.cpp`, `SceneCapture.cs`, the GPU
  test, `tests/source_checks.py`), never whole. Stop when done; list ideas in the report.
- Option bits are used up: take none; add a field if you need a switch. Say if an export or struct changes (the
  coordinator sets the ABI number).
- RTXDI and NRD sources are never copied or quoted. Unity's HDRP source: learn from it, copy nothing.
- Final message: five lines at most, pointing at the report.
