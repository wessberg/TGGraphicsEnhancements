# Brief: materials as the game shows them, proven by the yardstick (PT-64) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\mats`
(pristine base: `...\scratchpad\wt\base36`, the main tree at 17:03). Follow `AGENTS.md` there; read "The owner's rulings"
first ("The tracer fires its own camera rays, for every pixel", "The owner's word on the shortcuts list", "Judge the
picture without a denoiser first"). Report: `docs/agents/materials.md`; write it early, keep it current.

## Why

Since 16:50 every pixel's surface is the tracer's own camera ray's hit (`pt_primary.hlsli`; `docs/agents/hair-6.md`: read
its design and its "yardstick" how-to). So what the player sees of a surface is what the TRACER reads of its material,
no longer what the game's shader drew. The owner, told the tracer re-reads a handful of the game's shaders its own way:
"sounds like a big problem, fix that"; of the fixed numbers (every leaf passes 50 % of the light, thin cloth 30 %, every
glass bends light as 1.5): "how can we fix that? Big problem too"; of 64x64 copies of textures under 64 KB: "why?! Now
we share textures with the game ... Don't do copies"; of a reflection borrowing the game's on-screen material: "Well,
don't".

## The yardstick, and where it stands in game

`pt_yardstick.hlsl` compares the tracer's first hit with HDRP's G-buffer pixel by pixel (the native log's "Yardstick"
line, the plugin's every 10 s, the debug view `TGPT_VIEW_YARDSTICK`). First run in game, the tavern
(`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-165954`: `TGPathTracing.Native.log`,
`LogOutput.log`): the same surface on 99.2 % of the screen; of those pixels another colour 3 to 5 %, another normal 2 to
4 %, another roughness 0.2 %. The materials that differ most, by index: 118 (84 % of its own pixels), 134 (96 %), 99, 15,
162, 131, 111, 222. The index is the tracer's material list's: make the log name the material and its shader, then you
know what to fix.

## What to do

1. **Every material the game shows, right at a hit.** For each shader the game's scenes use (HDRP Lit and its variants,
   the game's own: RealSkin, RealHair, Kandra_Lit, Eye, the vegetation and terrain shaders, VFX_Lit...), what the tracer
   reads of a material (colour, normal, roughness, metal, emission, cut-out) must be what the game's shader shows. Work
   from the yardstick: the worst material first, find why it differs (a map the tracer does not read, a tint, a detail
   map, a blend, a UV transform, vertex colour), fix it, until none stands out. Offline, the test's scene must exercise
   each case you fix.
2. **Each material's own values, no fixed numbers:** a leaf's and a thin cloth's share of light let through from the
   material's own data (HDRP's transmission tint, thickness and diffusion profile where it has them; for cloth with
   none, the optics of a thin sheet from its colour and thickness: say what you take them from), glass by its own index
   of refraction (the plugin already logs each glass material's `_Ior`). The subsurface job (another agent ports it:
   `wt/port2`) replaces `TRANSMISSION` where a material has a diffusion profile: do not redo that; do the cases it left.
3. **No copies of textures:** textures under 64 KB are still read as 64x64 atlas copies (`kMinBytes` in
   `native/pathtracing/src/texture_share.hpp`; the plugin's "285 under 64 KB ... their atlas tiles serve"). Share them
   all; measure the video memory it costs across a session and report it. Then what still reads an atlas tile, and why.
4. **No borrowing from the screen:** `hdrp_material_at` (`pt_common.hlsli`, used by `pt_specular.hlsl` and the water
   passes) swaps a ray's hit material for HDRP's on-screen one. Remove it once 1 makes the hit's own material right; a
   thing must look the same in a reflection whether or not it is on screen.

If the job is too big for one agent: 1 and 4 first, then 3, then 2; write the rest up with numbers.

## How it is judged

- Offline (`tests/pathtracing_gpu_test.cpp`): each fixed case as a check, mutation-checked (a mutant's run ends at its
  FAIL line). `python tests/source_checks.py` passes; the plugin compiles.
- In game the coordinator runs ONE scene per part, Denoising "None" first: name the scene, the recipe, and what the
  yardstick's line must say after your work (your target numbers for "another colour", "another normal").

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling. Build into your own `%TEMP%\tgb\mats-...` folders, never `artifacts/`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files; build only
  `TGPathTracing.Native` and `tgpt_gpu_test`; source checks that fail from its work in progress are not yours (name them).
- Keep tokens down: grep and read ranges of big files (`SceneCapture.cs`, `pt_common.hlsli`, `tracer.cpp`, the GPU test,
  `tests/source_checks.py`), never whole. Stop when done; list ideas in the report.
- Others work in their own copies now: the port of subsurface scattering and the wet ground (`wt/port2`: it owns
  `pt_walk.hlsl`, the walk's material fields, `pt_wet.hlsl`), the whole world traced (`wt/world`: which objects are in
  the scene, their level of detail, swaying plants, decals). Do not take their parts.
- Option bits are used up: take none; add a field if you need a switch. Material bits: 67108864 is yours if needed.
  Main's ABI is 0x00011F00; say if an export or struct changes.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
