# Brief: glass that looks like glass: clear jars with thin rims, the tint of coloured glass (PT-13, PT-35, PT-76) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\glass3`
(pristine base: `...\scratchpad\wt\base65`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/glass-3.md`; write it early and keep it current. Background, only
as far as you need it (grep, then read a part): `docs/agents/glass-hair.md` and `docs/agents/glass-eyes.md` (the jobs
that made glass a surface of the camera's own rays), `docs/agents/jars.md`, `docs/PT-REQUESTS.md` PT-13, PT-35, PT-76.

## What the game showed (the inspection of 2026-10-08, `docs/agents/inspection-20261008.md` rows 75, 84, 85; crops in the MAIN tree's `artifacts\inspection-20261008\`, read only)

- `refract-jar-jars.jpg` (scene `refract-jar`, the owner's save for exactly this: glass jars in a cellar): with no
  denoiser the jars are CLOUDY WITH DARK BLOTCHES; the game's are clear, greenish, with thin bright rims and the wall
  showing through. With NRD what is seen through a jar is smeared to brown and boils (detail 0.86 against 3.30 with
  no denoiser and the game's 2.49; up to 15 levels of boiling).
- `glass-jars-lantern.jpg`: a lantern's glass is milky white; clear in the game.
- PT-76 (the whole GPU test's three failing checks since the glass-and-hair job): coloured see-through glass no
  longer tints the light that passes it: since that job a glass material's colour means paint ON the glass (a
  label, a lid), and the glass itself has "absorption per metre 0, 0, 0".
- The owner, 2026-10-09: "is glass and jar translucency and refraction back yet?" It is traced; it does not look
  right.

## The owner's rulings that bind this job

- Everything on screen is a surface of the tracer's own camera rays: glass too, never a layer after the denoiser,
  never HDRP's picture of it. "A mirror must show the world as the camera sees it."
- Each material's own values replace fixed numbers ("glass at 1.5: Big problem too"): HDRP gives a see-through
  material its index of refraction, a transmittance colour and an absorption distance, a thickness, a smoothness.
- No fakes, no limits that change the picture, no copies of textures, nothing screen-space.

## What to do, in this order

1. **Why the jars are cloudy and blotched with NO denoiser** (that is the tracer's own defect): what a camera ray
   meets on a jar (its outer wall, its inner wall, what is inside, the far walls), what each boundary does (reflect,
   refract, absorb, scatter), and where light or the view is lost or roughened (the material's smoothness read as
   rough? a thin wall treated as a solid block? paint and stains taken for the glass itself? shadow rays stopped by
   the glass? total reflection inside a wall that has no thickness?). Name the cause with a number from an offline
   render of your own if you can (a hollow jar on a table: `tests/` has glass fixtures; grep for them).
2. **The trial** for the largest cause, behind a hidden switch of the plugin (`Advanced`, default off: the picture as
   before; carry it in a reserved or spare field: all 32 option bits are used). Hand it over as soon as it compiles
   and the source checks pass (aim: within 45 minutes): the switch's name, the files changed, a recipe for
   `tests/ingame/scenarios.json` (`refract-jar`: no denoiser and NRD, each with the switch off and on, 16 frames;
   the pattern: the recipe `lamp-size`). Tell the coordinator in a short message, then go on with 3.
3. **The tint (PT-76)**: a see-through material's own transmittance colour and absorption distance tint what passes
   it, by the path's length inside the glass where its mesh closes a volume and as a thin pane where it is an open
   sheet; paint on the glass stays paint. The three failing checks of the GPU test pass again, or you say why one
   of them asks for the wrong thing. Its own switch if it can change a picture the owner has accepted.
4. Note, do not build (one line each in the report if you meet their cause): what NRD is handed for a pixel of
   glass and why the view through it smears and boils; the lantern's milky glass if it is not 1's cause; the
   windows that are missing altogether (PT-83: leaded windows in `npc-candle`, a rose window in `stairwell`).

## Rules

- Trial first: no long study before the hand-over of step 2.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small check only
  (the glass fixtures' own `..._ONLY` mode if there is one; else a small one of your own), through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs (`artifacts\harness\RUNNING` in the
  main tree held by a live process: wait).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\glass3` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the
  plugin too.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so.
- Other workers are in `pt_common.hlsli` too (water, grass): change what you must, move nothing else.
- Final message: six lines at most: the switch or switches (names, values), what each does in one line, the cause
  you found for the cloudy jars, the tint's state, that it compiles and the source checks pass, the report's path.
