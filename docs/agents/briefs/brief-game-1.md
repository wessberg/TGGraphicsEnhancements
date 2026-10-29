# Brief: what the game still renders and holds under the traced picture (PT-107, PT-87) - one worker, plugin side

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/game-1.md`; write it early and after every step.

## The owner, 2026-10-09

18:30: "as far as I understand we're also still rendering the vanilla rendering underneath or PT still, aren't we?
So we're paying for vanilla rendering AND PT on top? How much do we win by preventing that?" 17:30, of playing:
"I very, very frequently get this [Path tracing paused: 129 textures unavailable], so I'm barely able to test PT",
"constantly exceeding our VRAM budget". The ruling of 2026-10-07 (AGENTS.md): "The game's own lighting is not
rendered while tracing ('super wasteful, don't do that'). Nor anything else twice".

## What is measured (read first, in your copy: `docs/PT-REQUESTS.md` PT-107 and PT-87: grep the headings; and in
the scratch folder beside your copy, read only: `wt\memory1\docs\agents\memory-1.md` sections 3 to 5)

- Video memory at 4K (3840 x 2160, the tracer's rays at 1924 x 1084): Windows lets the game use 13.0 to 13.8 GB.
  THE GAME ITSELF HOLDS ABOUT 9 GB while tracing. The tracer: 1.9 GB before it has traced anything, 3.2 GB in a
  tavern, 5.0 in a town, 4.3 to 6.7 by the water. Over the budget a traced frame takes 1.5 s, or tracing pauses
  itself. The tracer's side is another worker's (done for now: a few hundred MB). THE GAME'S 9 GB IS YOURS.
- Time: in a town a frame is 139 ms, the tracer's GPU time 121: everything the game does under the traced
  picture is at most 18 ms. With tracing off the game runs a frame in 11 to 12 ms.
- What the plugin already stops while tracing: `src/TGPathTracing/HdrpPasses.cs` (the indirect lighting's
  multipliers set to 0 on a Volume of our own; "what HDRP shadows itself takes no traced shadow twice"),
  `SceneLights.cs` (the game's lights dimmed to 0 on surfaces, their fog kept). Read both before anything else:
  grep, then ranges.
- The memory line of the tracer's log, new today (in the main tree's build from about 20:30 on):
  `windowsBudget=... windowsCounts=... notTracer=... ownShared=...`: `notTracer` is what Windows counts for the
  process less the tracer's own: the game and the other mods. That is your measure in the game.

## The job

1. **The inventory** (the report's first table, within your first half hour, from the game's own assemblies and
   settings; decompile read only, as the workers before you did: `ilspycmd` on the game's
   `Unity.RenderPipelines.HighDefinition.Runtime.dll` and `TG.Main.dll`; learn, copy nothing): every pass HDRP and
   the game's own code still run each frame while tracing, and every large thing they hold in video memory, with
   its size in MB at 3840 x 2160 by the game's own pipeline asset and quality settings (read them from the running
   game's types and the asset: say which numbers are read and which are reckoned): the shadow atlases (the sun's
   cascades, punctual, area, the cached ones) and how many lights still render into them, reflection probes and
   their atlas or cube array, planar reflections, screen-space reflections, ambient occlusion, global
   illumination, contact shadows, subsurface scattering's pass, the volumetric fog's buffers and its history, the
   decal atlas and DBuffer, light cookies, the colour and depth pyramids, history buffers, the game's own custom
   passes (the rain's wetness pass is one; list the rest), the texture streaming budget and how much of the 9 GB
   is textures, post-processing. For each: does the traced picture need it (for what: the tracer's first hit is
   checked against HDRP's G-buffer; the fog is the game's and stays, lit by the game's fill lights; hair's
   see-through layer, effects, the interface and post-processing are still HDRP's), and what stopping it would
   give in ms and MB.
2. **The first cut, plugin only, handed over within your first hour**: what is certainly not needed while
   tracing and can be stopped by HDRP's own switches for the game's camera (its frame settings and their
   override mask, a Volume of our own as `HdrpPasses.cs` already has, the lights' own shadow settings), behind a
   hidden `Advanced / Game passes while tracing` (0 = as now, the default until the game has answered; 1 = the
   cut), restored the moment tracing is turned off (the harness checks that the game's picture is back after a
   traced step: it must be, to the pixel). No shader, no native change: so the coordinator needs no shader
   compile for your trial. With it: the recipe for `tests/ingame/scenarios.json` (the game; traced with the
   switch 0, then 1, NRD, 16 frames each; off; the game again) for the scene `metal`, and what you expect in
   `notTracer` (MB) and in frames per 10 s.
3. **The memory that stopping a pass does not give back** (HDRP makes its atlases when the pipeline starts and
   keeps them): which of them can be let go or made small while tracing and brought back when it is turned off,
   by which of HDRP's own ways, what it costs in a hitch when the player toggles, and what could break. Build
   what is safe behind the same switch (value 2); write the rest up as choices with their MB.
4. STOP when the first cut and the write-up are handed over.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder (read its DLLs; never write there).
- Keep the game's fog and its fill lights' part in the fog (the owner's rulings). Never `Add<T>(true)` on an HDRP
  Volume (a source check scans for it). Never touch a game Setting type with a localizing static constructor at
  plugin load (AGENTS.md).
- Nothing the traced picture or the tracer's inputs need may stop: the G-buffer, depth and motion vectors the
  tracer and the denoisers read, the decals' capture, what is still drawn by HDRP and shown (PT-83's list), the
  sky. When in doubt it stays, and the report says what would settle the doubt.
- With tracing Off the game is exactly the game: every switch restored at once, also after a load, a scene
  change, a resolution change and the plugin's unload.
- Plugin only (`src/TGPathTracing`). Build it: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f
  net472 -p:TargetFrameworks=net472 -o %TEMP%\tgb\COPYNAME-plugin` (0 errors, 0 warnings). No native build, no
  shader compile, no GPU test. `python tests/source_checks.py` passes.
- Unity's, HDRP's and the game's code: learn, copy nothing. Write what it does in your own words and numbers.
- Never read `tests/source_checks.py` or `HdrpPasses.cs` whole if it is long: grep, then a range. Sources are
  CRLF: keep them so. After a scripted edit, check every hunk landed.
- BepInEx config key names may not contain `= \n \t \ " ' [ ]`.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `native/`, `src/TGNeuralRendering*` and the other mods' plugins.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: the three largest things the game holds or runs for nothing while tracing
  (MB, ms), what the cut stops and behind which switch, the recipe and what the run should show, what is written
  up as choices, what could break, that it compiles and the checks pass, the report's path.
