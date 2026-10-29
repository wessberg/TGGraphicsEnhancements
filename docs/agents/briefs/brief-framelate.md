# Brief: framelate - the traced picture one frame late, so the CPU's work overlaps the GPU's trace (PT-145, PT-99)

Copy: `%TEMP%\tg-agents\wt\framelate` (pristine base: the one the coordinator names when it starts you). Follow
`AGENTS.md` in your copy; read first its rulings "Speed is no longer left for last", "The order of work", "No limits
that change the picture", "Follow NVIDIA's and AMD's guides to the letter", "Choices go on a page with pictures",
"The game is the first test". Report: `docs/agents/framelate.md` IN YOUR COPY; the plan within 30 minutes, kept
current; "State when stopped" at once if a limit shows.

## Why

A traced frame today is, one after the other: the game's own CPU work and its Direct3D 11 frame up to our inputs,
OUR CPU WORK TO RECORD THE FRAME, THE TRACE ON THE GPU (Direct3D 12), then the composite into HDRP's frame, HDRP's
post-processing, the upscaler, the interface. While the GPU traces, the game's device WAITS for it; while the CPU
works, the GPU idles (`docs/PT-REQUESTS.md`, grep "### PT-107" and "### PT-99"; `docs/rounds/round-19.md` sections 3
and 5: who waits for whom, measured). The tracer's own 10 s line says it for a town (2026-10-12): "gpu=60.76ms
cpu=7.92ms (... record 7.19) ... game device a frame (ms): the tracer's inputs 0.33, the game to the composite 8.64,
the wait for the trace 58.56". A town's frame is 77 ms; its trace is 60.

The owner, 2026-10-12, asked "Show the traced light one frame late, for more speed?": "Build it behind a hidden
switch", and: "Show me this motion in shots where you grab frames during motion with motion blur disabled so I can
pick from shots instead of feel, and so you can also look at it too."

## What to build: a trial, behind ONE hidden switch (off = today, bit for bit)

The coordinator's reading of the cleanest way, yours to confirm or to replace with a better one (say why):
- Since PT-112 step A the traced frame carries its own depth and motion beside its colour (HDRP draws no opaque
  object while tracing). So a frame of the tracer is a whole 3D picture. LET THE WHOLE TRACED PICTURE OF FRAME N
  (colour, depth, motion, the denoiser's and the upscaler's guides: all of frame N, consistent with one another) BE
  WHAT FRAME N+1's composite hands to HDRP's post-processing and the upscaler, instead of making the game's device
  wait for frame N+1's own trace. The game's CPU work and our record for frame N+1 then run while the GPU traces
  frame N+1 for the frame after. Nothing is reprojected and no light is moved by motion vectors: the picture is
  simply one frame older than the interface drawn over it. A frame should then take about max(the trace, the CPU's
  work) in place of their sum.
- What is NOT one consistent picture then, to be listed with what each looks like in motion: whatever HDRP still
  draws itself over the traced picture from the newer frame's camera (the transparents and effects of PT-112 step C,
  the interface, the first-person hands if they are HDRP's), the camera's own jitter for the upscaler (the jitter
  the upscaler is told must be the traced frame's), Frame Generation and Reflex (their markers and camera data: TG
  Frame Generation reads this frame's; read `docs/ARCHITECTURE.md` and `docs/FSR-FLICKER-INVESTIGATION.md` before
  touching what it is fed), exposure (which frame's).
- DLSS Ray Reconstruction, NRD and the upscalers take a frame's inputs as their documentation says: read what each
  says about the inputs' frame (colour, depth, motion, jitter all of ONE frame) before you design, and write the
  sentence into the report. An integration that hands them inputs of two different frames is a defect.
- The other way, "this frame's geometry, last frame's light moved by motion vectors", is what the owner was first
  told. It smears where the camera turns and where things appear. Build it only if the whole-picture way cannot be
  built, and say so first.

## How it is shown and measured (the hand-over is PICTURES)

- THE GAME, by the coordinator: hand a DLL and the plugin's DLL (paths, SHA-256) and the name of the hidden switch.
  The coordinator runs it with the camera turning and walking (the harness's `turn`, `slide` and walk steps; motion
  blur is off in harness runs: check, and say how), the switch off / on / off in ONE run (the owner's stream is on:
  AGENTS.md "The stream stays on"), raw frames kept, and puts frames grabbed DURING the motion side by side on a
  page for the owner. Say in the report which moments show the difference best (a fast turn past a pillar, a
  particle effect in view, the interface over the picture).
- Speed: the frame's time and the three clocks of the 10 s line, off / on / off in that one run. The gain is the
  frame, not a stage.
- Offline there is little to check (the offline test has no game loop): the switch off must leave the offline
  pictures bit-equal; say what else you could prove without the game.
- A first trial within three hours, or say why not. If reading shows it cannot work in this engine's frame (HDRP's
  render loop, the bridge between the two devices, the fences: `docs/ARCHITECTURE.md`, `native/bridge`), STOP and
  write down exactly where it breaks, with the lines: that is a result too.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in
  `%TEMP%\tgb\framelate-*`. One build tree.
- The bridge (`native/bridge`, its managed API) is shared by every released mod: do not change it unless there is
  no other way, and then say so before you do (a bridge change bumps its version for all mods: the owner's rule).
- One signalling queue per fence (a fence both devices signalled ran backwards once); hooks on global functions run
  on driver threads with tiny stacks: AGENTS.md "Rules that cost a round".
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start; wait with one background
  script, never poll. NEVER the whole `tgpt_gpu_test` or ctest.
- Build: the plugin with `dotnet build` of `src\TGPathTracing` into your copy's `bin` (see `Build.ps1` for the line;
  never `-Deploy`), the native DLL into `%TEMP%\tgb\framelate-build` (configure as `docs/AGENT-WORKERS.md` and
  `docs/agents/briefs/brief-campspeed.md`'s build line say). Copy the main tree's compiled shader headers
  (`build\native\generated\pathtracing`) into your build first; you should need no shader change, or one small pass.
- Other workers in the tree: `megageo7` (plants' structures, `mega_geometry.cpp`, `scene.cpp`), `fog3` (the fog's
  passes). Frame flag bits free for you: 20 to 23, 30, 31 (19, 25, 26 are the caustics', 24, 27 to 29 the fog's).
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files (`tracer.cpp` 5,000 lines,
  `HdrpPasses.cs`), never whole. The Bash tool eats backslashes: scripts and text with backslashes go through the
  Write tool. Say every switch or log line you add. Run `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
