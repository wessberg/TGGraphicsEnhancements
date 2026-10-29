# Brief: the traced world is whole within seconds of a load, and the game says how far it is (PT-98) - one worker, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/arrival-1.md`; write it early and after every step.

## The owner, 2026-10-09

"I frequently see stuff pop in over time when you playtest right now, so I'm never sure when I can trust the
visuals"; of a guard's textures and a town square filling in: "it took a LONG time before it kicked in".

## What is known (read first, in your copy: `docs/PT-REQUESTS.md` PT-98, PT-95, PT-96: grep the headings)

- The number that shows it (the main tree, read only:
  `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261009-150909\LogOutput.log`, the lines
  "Yardstick 10s"): in the town scene `npc-day`, 10 s after tracing was switched on, "nothing traced where the game
  drew 62.51 %" of the screen. That run's plugin already had the two changes of 13:50 (below).
- Two limits already went from "a frame" to "the clock" (`src/TGPathTracing/SceneCapture.cs` `MeshShare`: meshes are
  registered until a tenth of the last frame's time is spent; `TextureShare.cs`: every watched texture is looked at
  each scan). Other limits are still by the frame or by the scan: the plugin scans the world every 2 s, and the
  native side builds acceleration structures and opens textures by budgets of its own (`native/pathtracing/src/
  scene.cpp`, `tracer.cpp`: grep `MeshesQueued`, the build budgets, the compaction, the texture opens). The tracer
  ran at 5 to 10 frames a second today, so everything counted by the frame is 6 to 12 times slower than at 60.
- The plugin now shows a notice "building the traced world (N meshes to go)" (`Session.cs`, PT-98) from
  `Status.MeshesQueued + SceneCapture.Waiting`: it knows meshes only, not textures or instances still to come.

## The job

1. **Where the seconds go, as a timeline the game writes itself.** One log line a second from the moment tracing
   is switched on (or a scene is loaded with it on) until the world is whole, then silence: seconds since the
   start; instances the game draws and instances traced; meshes seen, registered, built; textures wanted, open,
   at their sharp size; the queue lengths; the frame time. And one summary line when it is whole: how long it
   took and which queue was the last to empty. Cheap: counters that exist or are trivially kept; nothing by pixel.
2. **Read the code for every limit on that path** (plugin and native): what is counted by the frame, by the scan
   or as a fixed number an update, and what each would allow at 8 frames a second against 60. List them in the
   report with file and line. Make them go by the clock (a share of the frame's time, as `MeshShare` does), never
   by dropping work: nothing may be left out or drawn coarser to be sooner ("No limits that change the picture").
   Say what each costs in frame time while the world arrives.
3. **The notice tells the truth about all of it**: one line, plain words, a number that falls (things still to
   come: meshes, textures), gone when the world is whole. The harness reads the same state: a step's pictures are
   taken only when it is whole (`src/TGPathTracing.Harness/Runner.cs` waits for `MeshesQueued` 0 today: is that
   enough? Textures are not in it).
4. STOP at the hand-over: the plugin and the native side compile; what one run should show (the coordinator runs
   `npc-day` and `metal` in the game: the timeline before and after is the test); the recipe if one is needed.
   The game is the first test: hand over the timeline (step 1) ALONE first, within your first half hour, written
   into the report, so the coordinator can run it while you go on with step 2.

## Rules

- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): a small check of your own
  only if you need one, through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the
  game runs (`artifacts\harness\RUNNING` in the main tree held by a live process: wait with one background script).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\COPYNAME` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it>` before `cmake --build` with its defaults, never
  `-First` (the coordinator's build goes first; a line "the coordinator's build of the main tree goes first" means:
  wait, nothing is wrong). Build the plugin and the harness too (0 errors, 0 warnings).
  `python tests/source_checks.py` passes.
- DO NOT COMPILE THE SHADER PASSES (the PC ran out of memory twice today, and you change no shader): your copy's
  shader sources are the main tree's, so take the main tree's finished headers. Wait, with one background script,
  until the main build is done (`%TEMP%\tgb\BUILD-FIRST` is gone AND
  `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\build\native\generated\pathtracing\pt_specular_ser.h` is newer
  than 2026-10-09 15:21), configure your build folder, then COPY (read only on the main side) every
  `build\native\generated\pathtracing\*.h` of the main tree into your build folder's `generated\pathtracing\`, so
  that `tools\Build-PtShaders.ps1 -BuildDir <it>` says "0 to compile". Then `cmake --build` compiles C++ only.
  Until then: read, write the timeline's code, build the plugin. If a shader source of yours must change after
  all, say so in the report and compile with `-Throttle 2`.
- All 32 option bits are used; no ABI or size change of `TGPT_Frame` (the status record may grow at its end if the
  plugin and the native side change together: say so).
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's code: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. Other workers are in the
  shaders (glass, water); stay out of them.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: the timeline's line as it looks, the limits found (count, the worst three),
  what changed, what the run should show, what is left, that it compiles and the checks pass, the report's path.
