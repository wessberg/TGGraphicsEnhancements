# Brief: water is an ordinary surface of the camera rays; the old water passes go (PT-71 item 5) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("The tracer
fires its own camera rays, for every pixel", "Finish the migration first", "Water is the tracer's", "Do. not. fake.
stuff."). Report: `docs/agents/water-3.md`; write it early (the design before the code).

## Why

The owner: "I expect visual regressions until the renderer is fully migrated. Focus on finishing the migration to our
own renderer, then we can A/B with vanilla and ensure we render everything." The list is `docs/PT-REQUESTS.md` PT-71;
yours is item 5. Since 16:50 every pixel's surface is the tracer's own camera ray's hit (`pt_primary.hlsli`;
`docs/agents/hair-6.md`: "The camera-ray design", "For the next agent"). Water is not: HDRP draws its surface, and two
passes of the old route light it (`pt_water.hlsl`, `pt_water_under.hlsl`, their 12-frame history and 3x3 filter in
`d3d11.hlsl`, render event `EventWater` and `WaterPass` in `HdrpPasses.cs`, the Harmony transpiler on HDRP's water
lighting). They cost 12 to 20 ms a frame and make bright dots on lit water in every view (PT-68: proven, run
`artifacts\harness\20261007-164828`, `lantern-lake\dots.jpg`). The owner wants (PT-42, 10:55): "smooth intersections
between waves and surfaces/cliffs/rocks/shore, and path traced water".

## What exists to build on

- HDRP's real waves reach the tracer (`src/TGPathTracing/WaterCaustics.cs`, `TGPT_WaterCaustics`; the wave functions,
  in `pt_waves.hlsli` if the port has been merged, else in `pt_caustics.hlsl`): the height and normal of a water
  surface at any point and time, as HDRP displaces it.
- Findings of a finished agent, all to carry over (`...\scratchpad\wt\wedges\docs\agents\water-edges.md`): light
  leaving water must be divided by n squared (the bed was 1.78 times too bright); the light the surface reflects back
  down onto the bed was missing; the traced rock at a waterline was a coarser level of detail than the drawn one.
- The outside review's water findings 1 to 5 (`docs/agents/review-today-gpt.md`): they describe the old passes; make
  sure none survives in the new way.
- The wet ground's store (`pt_wet.hlsl`, PT-52), if the port has been merged: where water has been stays wet.

## What to do

1. **Water as a surface of the camera ray:** the ray meets the displaced water surface (decide how: the waves as traced
   geometry refitted each frame near the camera, or the ray marched against the height field: say which and why; the
   surface must be the same one for camera rays, mirror rays, bounces and shadow rays), then reflects and refracts by
   Fresnel with water's own index (1.33), chosen by chance, a new draw each frame; the refracted ray goes on to the bed
   through water that absorbs and scatters by HDRP's own water colours for that surface; total internal reflection from
   below; the camera under water. No screen-space step anywhere, no HDRP water shading.
2. **The waterline:** where a wave meets rock or sand the picture is continuous because the water is geometry among
   geometry: no blocky slabs, no hard flat edge (the owner: "it looks blocky and not smooth"). Foam where HDRP's water
   has foam (its foam data, as HDRP gives it), as a property of the surface a ray meets.
3. **The denoiser's guides** for a water pixel (depth, motion, normal, albedo, roughness) from that hit.
4. **Then remove the old passes** and everything only they use; `Water Reflections` as a row goes or changes meaning:
   say what you propose (the owner decides rows).

Not yours: caustics on the bed and on walls (a later job; keep `pt_caustics.hlsl` working), materials of rocks, which
objects are in the scene.

## How it is judged

- Offline (`tests/pathtracing_gpu_test.cpp`), with Denoising None: a pool with waves: the bed seen through it at the
  right brightness (against a many-sample reference), the sky mirrored at grazing angles, the same water in a mirror,
  no pixel far brighter than its neighbours' mean (the dots), a wave meeting a slope without a step; each check
  mutation-checked (a mutant's run ends at its FAIL line). `python tests/source_checks.py` passes; the plugin compiles.
- In game the coordinator runs ONE scene per part: `lantern-lake@dots` (no dots, undenoised), `water-day@raw` (the bed,
  the far shore's reflection, the waterline). Say what each picture and log must show, and the stage's cost.

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
- Keep tokens down: grep and read ranges of big files (`pt_common.hlsli`, `tracer.cpp`, `d3d11.hlsl`, `HdrpPasses.cs`,
  the GPU test, `tests/source_checks.py`), never whole. Stop when done; list ideas in the report.
- Option bits are used up: take none; add a field if you need a switch. Say if an export or struct changes (the
  coordinator sets the ABI number).
- RTXDI and NRD sources are never copied or quoted. Unity's HDRP source: learn from it, copy nothing.
- Final message: five lines at most, pointing at the report.
