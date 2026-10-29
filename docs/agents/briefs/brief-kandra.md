# Brief: a character's armour has none of its detail: what the tracer reads of the game's character shader (PT-95, PT-64) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\kandra`
(pristine base: `...\scratchpad\wt\base68`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/kandra-lit.md`; write it early and keep it current. Read
`docs/PT-REQUESTS.md` PT-95 first (grep the heading, read that entry only).

## What the game showed (the MAIN tree's `artifacts\harness\20261009-084003`, read only; scene `metal`, NRD)

- `helmet-flat-smooth.jpg` (the 4K frame's crop x 1540 y 440, 460 x 640; the game, then three traced steps): the
  game's helmet is dark hammered iron, full of dents, scratches and rough patches; traced it is plain smooth polished
  metal: none of its colour, relief or roughness.
- `helmet-normals-view.jpg`: in the tracer's own Normals view the helmet is perfectly smooth, while the brick wall
  behind it shows its relief: no normal detail of the helmet reaches a hit.
- The same guard's scale armour and leather pauldron do show their textures.
- The owner, 2026-10-09: "Where did all the game's texture/normal map/c[...]", "we're not rendering all the detail
  maps on the helmet it seems", "We're lacking all the details on the helmet from the game", "you see all the
  imperfections in the helmet not appearing in either of ours".

## What is known (read in the code by the coordinator; check it)

- The tracer reads the game's shaders by per-shader rules: `src/TGPathTracing/MaterialPrograms.cs` (`Build`: the
  game's architecture, cliff, grass, leaves, trunk, terrain, triplanar, skin and dissolve shaders, and `HDRP/Lit`
  with its detail map), `SceneCapture.cs` (`MaskLayouts`: the mask map's name and channels per shader;
  `ShaderGraphTiling`), `TextureShare.cs` (the names a base colour and a normal map are looked for under).
- The game's character shader is `TG/Character/Kandra_Lit` (and `Kandra_Lit_Dynamic`): the tracer knows it only in
  `MaskLayouts` (its mask map, with occlusion). There is no rule for anything else it has (a detail layer, a
  tiling of its own, wear, dirt or tint masks, remaps), and the detail map is read for `HDRP/Lit` alone.
- Not known: that the helmet's material IS `Kandra_Lit` (likely: it is a character's), and what that shader
  samples. A run that names the material is queued (`metal` with the recipe `yardstick`); the coordinator sends
  you its lines when it lands. Do not wait for it: start from the shader.

## The owner's rulings that bind this job

- "The tracer reproduces what every one of the game's shaders shows" and each material's own values replace fixed
  numbers. Every texture a ray reads is the game's own (shared, never copied), at the game's own size.
- "A mirror must show the world as the camera sees it": what a camera hit reads, a reflection's hit reads too.
- No fakes: not a generic detail texture, not a noise standing in for the game's maps.

## What to do, in this order

1. Find out what `TG/Character/Kandra_Lit` and `Kandra_Lit_Dynamic` compute: their properties and what their
   compiled code samples and how it combines it (base colour and tint, normal map and its strength, mask map
   channels and remaps, every detail or micro-normal layer with its tiling and strengths, wear, dirt, colour masks,
   emission). The way earlier jobs read the game's shaders is in `docs/PT-DETAILS.md` (the hair and eye shaders)
   and `docs/agents/materials.md`; tools may be in `...\scratchpad\pytools-unity` (read only). Learn from the
   game's shader, copy nothing of it into the tree.
2. **The trial**: a reading for those shaders as the other shaders have theirs (a material program or the shared
   path with the right names, whichever the code's pattern asks for), behind a hidden switch of the plugin
   (`Advanced`, default off: the picture as before; a spare or reserved field, never an option bit: all 32 are
   used), so ONE run shows the guard with it off and on. Hand it over as soon as it compiles and the source checks
   pass (aim: within 45 minutes; if decoding the shader takes longer, say so early in the report): the switch's
   name, the files changed, a recipe for `tests/ingame/scenarios.json` (`metal`: NRD and no denoiser, the Normals
   and the Albedo view, each with the switch off and on; 16 frames for the pictures, 4 for the views; the pattern:
   the recipe `char-normals`). Tell the coordinator in a short message, then go on.
3. Then the same question for the other character and clothing shaders the tracer names without a full reading
   (`TG/Clothes/Cloth_Wind`, `TG/Character/GhostCrew`, what else `SceneCapture.cs` and `ClothRules.cs` name): a
   table in the report of what each samples against what the tracer reads; build what is small and sure.
4. One small offline check of your own in its own `..._ONLY` mode once the game has said yes (a quad with a known
   base, normal, mask and detail map through the new reading: the hit's albedo, normal and roughness against the
   worked-out values).

## Rules

- Trial first: no offline fixture and no long study before the hand-over of step 2.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small check only,
  through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs
  (`artifacts\harness\RUNNING` in the main tree held by a live process: wait).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\kandra` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the
  plugin too (0 errors, 0 warnings).
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's shaders and decompiled code: learn,
  copy nothing, commit nothing of them.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `SceneCapture.cs`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so.
- Other workers are in `pt_common.hlsli` and `Settings.cs` too (glass, wax, water, particles): change what you
  must, move nothing else.
- Final message: six lines at most: what the shader samples that the tracer did not read, the switch (name,
  values), what it does in one line, the other shaders' table in a line, that it compiles and the source checks
  pass, the report's path.
