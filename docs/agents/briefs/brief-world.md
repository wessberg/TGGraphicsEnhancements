# Brief: the whole world in the traced scene (PT-63, PT-61) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\world`
(pristine base: `...\scratchpad\wt\base34`). Follow `AGENTS.md` there; read "The owner's rulings" first: "The tracer fires
its own camera rays, for every pixel", "The owner's word on the shortcuts list", "What moves is traced as it moves".
Report: `docs/agents/world.md`; write it early, with the design before the code, and keep it current.

## Why

The owner: "I think number one thing to do before anything else is make sure WE trace the world, not the game." Another
agent (`wt/hair6`) is building the tracer's own camera rays for every pixel. Those rays show only what is in the traced
scene: whatever is missing from it, or differs from what the game draws, becomes a hole or a wrong edge on screen. Your
job is that the traced scene IS the world the game draws. The owner's words on each part (`docs/PT-SHORTCUTS.md`):

1. **Objects left out.** Statically batched renderers are skipped (`src/TGPathTracing/SceneCapture.cs`, grep
   "isPartOfStaticBatch": "combined meshes would be traced once per member"; logged once as "N statically batched
   renderers near the camera are left out"). The owner: "that sounds like a big problem, if things suddenly disappear
   when PT is enabled. Fix that." Find every other way an object the camera draws stays out of the traced scene
   (capture paths: GameObjects, Drake's entities, Medusa, Leshy vegetation, terrain, Kandra characters; layers, hidden
   flags, unreadable meshes, the per-frame mesh and BLAS budgets) and close each, or name it with a number.
2. **The same geometry the game draws** (PT-61): a finished agent found the tracer tracing a coarser level of detail of
   rocks than the one drawn (its report: `...\scratchpad\wt\wedges\docs\agents\water-edges.md`, grep "level of detail").
   The traced level must be the drawn one, per object, as the game picks it.
3. **No distance past which nothing is traced.** `Scene radius` 250 m and `Ray distance` 500 m. The owner: "Why? Do
   that." Everything the game draws at any distance is in the traced scene; say what it costs in memory and time and
   how the far scene is kept cheap without changing the picture (instancing, the game's own far levels of detail).
4. **Grass and leaves sway as the game sways them.** They are traced standing still (`SceneCapture.cs`, grep "Sways(";
   rays distrust such geometry near them: `MASK_SWAYS` in `pt_common.hlsli`). The owner: "of course our shadows and
   reflections should move alongside the grass and leaves swaying in the wind. Otherwise what's the point of realtime
   ray tracing." Cloth already sways in the tracer (`pt_sway.hlsl`, `src/TGPathTracing/ClothRules.cs`,
   `TGPT_SetClothWind`: the game's compiled wind shader worked out for the traced vertices, the BLAS refitted): do the
   same for the game's grass and leaf shaders, then the distrust goes.
5. **Decals seen by rays.** HDRP's decals (blood, dirt, wet patches, glowing runes) exist only in the game's G-buffer.
   The owner: "they should! Of course they should." A ray's hit takes the decals over it (their colour, normal, mask,
   emission) as HDRP's decal pass gives them to the pixel.

Order: 1 and 2 first (they decide whether the camera rays' picture has holes), then 4, 3, 5. If the job proves too big
for one agent, finish 1, 2 and 4, and write 3 and 5 up as designs with numbers.

## How it is judged

- The camera-ray agent builds a yardstick (the tracer's first hit against HDRP's G-buffer, pixel by pixel, by material
  and instance kind; `docs/agents/hair-6.md` in `...\scratchpad\wt\hair6` says how far it is). Until it is in main, use
  what exists: the Coverage and Surfaces debug views and the plugin's scene summary ("N instances (objects, Drake,
  entities, Medusa, terrain, vegetation, water)", "left out" lines) in a harness run's `LogOutput.log`; runs with those
  views: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-043153\glow-lake\`, `...\20261007-124601\vista\`.
- Offline (`tests/pathtracing_gpu_test.cpp`): a swaying blade's shadow moves with it; a decal shows in a mirror; each
  new check mutation-checked (a mutant's run ends at its FAIL line). The capture's own logic is C#: test what can be
  tested without the game (`tests/light_rules_tests.cs` shows the pattern), and log counts the in-game run can check.
- In game the coordinator runs ONE scene per part (the owner's rule), judged with Denoising "None" first (AGENTS.md):
  name the scene and what its log and picture must show, per part.

## Working rules

- Never start the game, never deploy, never commit. GPU tests only while the game is closed,
  `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and the MAIN
  tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one background
  script, never by polling. Build into your own `%TEMP%\tgb\world-...` folders, never `artifacts/`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, `CMakeLists.txt`): your copy holds its half-done files. Three
  source checks fail from that and are not yours (`test_cmake_inputs_exist_and_shader_test_is_windows_only`,
  `test_optional_hudless_is_not_fabricated`, `test_sliders_pass_a_step_the_interop_accepts`); do not touch its files; build
  only `TGPathTracing.Native` and `tgpt_gpu_test`.
- Keep tokens down: grep and read ranges of big files (`SceneCapture.cs` is 166 KB, `pt_common.hlsli`, `tracer.cpp`,
  `scene.cpp`, the GPU test, `tests/source_checks.py`), never whole. Stop when done; list ideas in the report.
- Others work in their own copies: camera rays (`wt/hair6`: `pt_primary.hlsli`, `pt_prepare.hlsl`, the composite), a
  port of subsurface scattering and wet ground (`wt/merge3`). Do not take their parts.
- RTXDI and NRD sources are never copied or quoted. Bits: option bits are nearly used up (taken or promised: every bit
  up to 2147483648): take none; if you need a switch, add a field instead and say so. Material bits: 33554432 is yours
  if needed. Instance flags: say which you add. Main's ABI is 0x00011E00; say if an export or struct changes.
- Final message: five lines at most, pointing at the report.
