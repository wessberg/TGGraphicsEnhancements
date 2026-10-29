# Brief: megageo-7 - RTX Mega Geometry is on and stays on: find what we do wrong that makes it slower (PT-109)

Copy: `%TEMP%\tg-agents\wt\megageo7` (pristine base: the one the coordinator names when it starts you). Follow
`AGENTS.md` in your copy; read first its rulings "RTX Mega Geometry is ON and stays on", "Shader Execution Reordering
stays on" (the same reasoning), "Follow NVIDIA's and AMD's guides to the letter", "Speed is no longer left for last",
"No limits that change the picture", "The game is the first test". Report: `docs/agents/megageo-7.md` IN YOUR COPY;
the plan within 30 minutes, kept current; "State when stopped" at once if a limit shows.

## The owner's word (2026-10-12 11:00)

"I still want us to enable RTX Mega Geometry. Do not turn it off. I still insist there must be something we've done
wrong if it's not making things faster. Othewise, what's the point?"

So this is not a study of whether to use it. It is on. Your job is to find, by measuring, what in OUR use of it
costs more than the plain acceleration structures, and to mend it, until a frame with it is no slower anywhere and
faster where the game poses thousands of plants a frame.

## What exists (read these before the code)

- `docs/PT-REQUESTS.md`, grep "### PT-109": the whole entry (130 lines): six workers' history, every number.
- `docs/agents/megageo-1.md` to `megageo-6.md`: what was built, how it was checked, what each found.
- The code: `native/pathtracing/src/mega_geometry.cpp`, `.hpp`; in `scene.cpp` the plants (`plan_plants`, the
  plants' pose and their structures; grep `mega`, `cluster`, `MegaGeometry`); CMake `TGPT_MEGA_GEOMETRY` (ON in the
  main build); the hidden `Advanced / Mega Geometry trial` (frame flags bits 2 to 5): 0 plain; 1, 2, 3 the
  partitioned top level alone (cells of 64, 128, 256 m); 4, 5 small clusters; 7 NVIDIA's layout with opacity
  micromaps; 8 the same without micromaps (the recipes `mega`, `mega-plants`, `mega-walk` in
  `tests/ingame/scenarios.json` name them). The tracer's log lines "Mega Geometry CPU trial ...", "PT structures
  10s", "PT plants' plan 10s", "PT record CPU 10s", "PT cut-outs: ...".
- NVAPI is in the main tree's root (`NVAPI-R615`, a junction in your copy). NVIDIA's own documentation and sample
  for RTX Mega Geometry (the cluster acceleration structure API, the partitioned top level, their guidance on
  cluster sizes, on templates and instantiation, on animated geometry, on what to batch in one call) are public:
  READ THEM IN FULL FIRST and write into the report, as a table, every point where our use departs from them. That
  table is the first hand-over. (NVIDIA's sample code is read, not copied.)

## What was measured, and what it points at (none of it proven: yours to confirm or drop)

The town's walk, plain / NVIDIA's layout (trial 7), a quiet card, `20261012-000233`: a frame 140.6 / 148.8 ms; the
tracer's GPU 82.1 / 84.3; THE BOUNCED LIGHT 20.8 / 27.0 (its rays pay about 6 ms more through the clusters); the
scene stage 4.3 / 3.8; THE TRACER'S CPU with trial 7: 13 to 15 ms of scene record "for 10,000 plant structures a
frame" (plain today: 6 to 7 ms for everything).
1. THE COUNT OF STRUCTURES AND CALLS. The plain way puts dozens of plants of one cell into ONE bottom-level
   structure (287 groups for 6,453 plants in the forest; one refit call a group). If NVIDIA's layout makes a
   structure a plant, that is thousands of driver calls a frame (the CPU), and thousands more instances in the top
   level whose boxes overlap in a meadow (the rays). What does NVIDIA's API offer for exactly this (many small
   animated objects of few shapes: templates instantiated, one multi-indirect call for all of a frame's cluster
   work), and what do we call instead?
2. THE RAYS. 6 ms more in the bounced light with the same picture. Candidates: clusters too small or too many per
   plant; build flags (fast build where fast trace is meant, or the reverse); THE OPACITY MICROMAPS under the
   clusters (megageo-1 never checked them offline: read the log's "PT cut-outs" line with trial 7 and 8: if leaves
   lose their micromaps under clusters, every leaf a ray crosses runs the alpha test in the shader, and that alone
   is milliseconds); the partitioned top level's cells and how instances are put into them.
3. WHAT IT MUST SAVE. The scene stage's plant refits are 3.4 to 4.8 ms of GPU a frame in the forest and the town
   today (the stage's own line: "plants 4.77 ms (624 refitted a frame)"), and the record's CPU for them 1.2 ms.
   That is the prize; a design that does not take most of it is not the design NVIDIA means.

## How it is measured and checked

- THE GAME, by the coordinator: hand a DLL (path, SHA-256) and the plugin's change if any; the coordinator runs
  `metal@mega-plants` (standing) and `forest@mega-walk` and sends you the stage times, the CPU lines and the
  log's counts. THE OWNER'S STREAM IS ON (AGENTS.md: speed is measured with it): every comparison is plain / yours
  / plain again WITHIN one run. A first trial DLL within three hours, or say why not.
- Offline, your own small mode only (`TGPT_TEST_MEGA_ONLY`; megageo-6's check that grows, shrinks and replaces
  plants): the pictures with it on are the plain ones' to the last pixel, and the counts of structures, calls and
  instances a frame are printed. Mutate once to see your check fail.
- The default: the coordinator flips `Mega Geometry trial`'s default to NVIDIA's layout in the main tree; in your
  copy work with whatever numbers you need, and say in the hand-over which value is the one to be the default (or
  that the trial number goes and the layout is simply the rule).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in
  `%TEMP%\tgb\megageo7-*`. One build tree; delete your own test pictures once read.
- Never propose switching Mega Geometry off, a smaller scene, a distance past which plants are not posed, or
  plants that do not sway: the owner's rulings. A way that changes the picture is described as a choice with its
  number and not built.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start; wait with one background
  script, never poll. NEVER the whole `tgpt_gpu_test` or ctest. If a game of the owner's opens while a test of
  yours runs, the coordinator's watchdog ends the test: start it again later.
- READ PT-125 AND PT-134 (`docs/PT-REQUESTS.md`) before you keep any index or address between frames: both were
  GPU resets from a number that went stale when the set of plant groups or a pool changed.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\megageo7-build`; configure as `docs/AGENT-WORKERS.md` and `docs/agents/briefs/brief-campspeed.md`'s
  build line say (`-DTGPT_MEGA_GEOMETRY=ON -DTGPT_RAIN_WETNESS=ON`). Copy the main tree's compiled headers
  (`build\native\generated\pathtracing`) into your build before your first shader edit; you should need few shader
  changes or none.
- Other workers in the tree: `caustgrid` (`pt_caustics.hlsl`, frame flag bits 19, 25, 26), `fog3` (the fog's
  passes, bits 27 to 29). The water's pieces are in groups since today (`scene_water.cpp`): not yours to change.
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files (`scene.cpp` 4,600 lines,
  `tracer.cpp` 5,000), never whole. The Bash tool eats backslashes: scripts and text with backslashes go through the
  Write tool. Say every switch or log line you add. Run `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
