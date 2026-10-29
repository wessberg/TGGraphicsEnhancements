# Brief: characters shaded smooth: their own vertex normals reach the tracer (PT-92) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\normals`
(pristine base: `...\scratchpad\wt\base63`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/char-normals.md`; write it early and keep it current. Read
`docs/PT-REQUESTS.md` PT-92 and PT-04 first (grep the headings, read those entries only).

## What the game showed (run `20261009-071155`, the MAIN tree's `artifacts\harness`, read only; NRD, 16 frames a step)

- `helmet-game-off-on.jpg` (scene `metal`, the 4K frame's crop x 1540 y 440, 460 x 640): a guard's helmet is a chrome
  mirror in flat bands; the game's is dark hammered iron, smooth in shape, rough in surface.
- `masked-man-game-off-on.jpg` (scene `night`, crop x 1440 y 1000, 640 x 860): a cloak's torch-lit side is flat
  triangles with hard edges, its weave gone; the game's is smooth cloth with its weave.
- The owner, 2026-10-09: "I noticed some blockiness/low-polyness on the armored guards helmet, and the cloth of the
  guy with the weird mask standing next to the bridge. This is a problem we resolved some time ago. Why is the
  low-polyness back".

## What is known (read in the code by the coordinator; check it)

- A character reaches the tracer as skinned POSITIONS only: `native\pathtracing\shaders\pt_skin.hlsl` copies them
  from a texture the plugin fills (a vertex a texel). `pt_primary.hlsli` says it: "the tracer has no vertex normals
  for characters". Static meshes have theirs: `Hit.rounded` (`pt_common.hlsli`, "the mesh's own smooth normal there
  (its vertices', TGPT_MeshDesc), on normal's side; normal where it has none").
- Since the tracer fires its own camera rays, the camera's first surface on a character is that flat face.
- NRD is handed the face's normal as its guide ("NRD keeps the per-face guide", `docs/PROJECT-CONTEXT.md` section 9,
  "Round things"): it smooths inside each face, and the edges stand out. With no denoiser the facets are faint.
- Not known: whether a character's normal map is laid on its surface at all at the camera's hit (the helmet's
  hammered relief is gone: wiped by the denoiser, or never read?).

## The owner's rulings that bind this job

- "A mirror must show the world as the camera sees it": what you give the camera's hit, a reflection's hit has too.
- Nothing is rendered twice for our sake, nothing screen-space, no copies of textures, no limits that change the
  picture: the normals are taken where the game's own skinning leaves them (its skinned vertex data), not from a
  draw of the character and not from HDRP's G-buffer.
- No fakes: not a blur over the facets, not a denoiser setting that hides them.

## What to do, in this order

1. Find where the plugin fills the positions' texture and what the game's skinning leaves beside the positions (its
   skinned normals and tangents). Choose the way that is correct and cheapest per frame; say in the report what
   you rejected and why (normals worked out from the skinned triangles' neighbours split at every texture seam).
2. **The trial**, behind a hidden switch of the plugin (`Advanced`, default off: the picture as before): a
   character's hit takes its smooth normal (`Hit.rounded`), the character's normal map laid on it as on a static
   mesh, for the camera's rays and for the rays that meet it later (reflections, bounces). Hand it over as soon as
   it compiles and the source checks pass (aim: within 45 minutes; if the data path takes longer, say so early in
   the report): the switch's name, the files changed, a recipe for `tests/ingame/scenarios.json` (`metal` and
   `night`, NRD, 16 frames: the switch off, then on; the pattern: the recipe `lamp-size`). Tell the coordinator in
   a short message that it is ready, then go on with 3.
3. The denoisers' guides: what NRD and Ray Reconstruction are handed as the normal of a character's pixel is the
   smooth one (with the normal map, as for other surfaces). Say what the guide was and is.
4. The memory and time it costs (bytes a vertex, a character, a frame; the passes' milliseconds), in the report.
5. Note, do not build: the helmet's chrome against the game's dark iron (its material: PT-21); hair cards (they
   have their own normal: leave them as they are).

## Rules

- Trial first: no offline fixture and no long study before the hand-over of step 2. After the game has said yes:
  one small offline check of your own in its own `..._ONLY` mode.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small check only,
  through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs
  (`artifacts\harness\RUNNING` in the main tree held by a live process: wait).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\normals` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the
  plugin too.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so.
- A change of the ABI (`tgpt_api.h`, `NativeTracer.cs`) keeps both sides and the source checks' pinned sizes in step.
- Final message: six lines at most: the switch (name, values), what it does in one line, what the guides take
  now, the cost, that it compiles and the source checks pass, the report's path.
