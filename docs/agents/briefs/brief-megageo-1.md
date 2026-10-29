# Brief: RTX Mega Geometry, "the Alan Wake 2 BVH" (PT-109) - one worker, hand over early, then build the first piece

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/megageo-1.md`; write it early and after every step.

## The owner, 2026-10-09

13:40: "we [can] also add [the] nanite like new thing nvidia did for optimizing bvh construction, that was
implemented in a post patch for Alan Wake 2". 20:05, after the measuring run: "and add the alan wake 2 bvh".
So it is to be ADDED. Not a study of whether.

## What it is (from the coordinator's memory: CHECK EVERY LINE against NVIDIA's own documents before you rely on it)

NVIDIA RTX Mega Geometry: (1) cluster-level acceleration structures (CLAS): a mesh cut into small clusters of
triangles, each built once (or stamped from a template when only its vertices move), and a bottom-level structure
built FROM clusters far faster than from triangles; (2) a partitioned top-level structure (PTLAS): only the
partitions whose instances changed are rebuilt, not the whole top level every frame. On Direct3D 12 both come
through NVAPI (the cluster operations and the partitioned top level calls of `nvapi.h`, a capability query for
each, and shader intrinsics of `nvHLSLExtns.h` for a hit's cluster), on Vulkan through two NV extensions. Alan
Wake 2 took it in a patch early in 2025 (NVIDIA's own numbers for it, to be looked up: video memory saved and
frames gained). Sources to find and read: NVIDIA's RTX Mega Geometry SDK and its documents on GitHub, NVAPI's
header and its license, NVIDIA's and Remedy's notes on the Alan Wake 2 patch, the nvpro samples for clusters and
the partitioned top level. Write each source, its version and its license terms into the report (for
`docs/SOURCES.md`). Licenses decide what may be copied: when in doubt, learn and copy nothing.

## What the tracer does today (read first, in your copy)

- `docs/PT-REQUESTS.md`: PT-99 (grep the heading; its notes of 2026-10-09, the measuring run), PT-87 (the newest
  note: what the memory worker measured), PT-98 (how long the traced world takes to be whole).
- `docs/agents/measure-run.md`: where a frame's time goes. `docs/agents/speed-1.md` sections 1 to 3.
- In the scratch folder beside your copy, read only: `wt\memory1\docs\agents\memory-1.md` sections 5 and 6 (what of
  the memory grows with the world; the plants; the arenas), and `wt\perf1\docs\agents\perf-1.md` if it has a
  section "Does the unlimited world cost the speed?" (another worker is asked where the structures' work is timed).
- The numbers, a town square (`metal`): 16,234 instances traced of 25,273 registered; 4.4 M triangles of still
  meshes; 11,303 plants posed for the wind every frame in 636 groups of a 16 m cell (9.5 M vertices, 6.7 M
  triangles: every plant its own posed copy, 32 bytes a vertex, and its own structure, about 67 bytes a
  triangle: 795 MB); 1,379 pieces of water surface (1.2 M triangles) made by distance; 59 characters (0.7 M
  triangles) skinned every frame; acceleration structures 411 to 918 MB; opacity micromaps on 236 meshes. The
  frame's "scene" stage is 1.0 to 1.6 ms of GPU time; WHETHER THE PLANTS', THE CHARACTERS' AND THE WATER'S
  STRUCTURES ARE BUILT INSIDE THAT STAGE OR OUTSIDE EVERY TIMED STAGE IS NOT KNOWN: find out first.
- The tracer's rays are inline ray queries (`PT_RAYQUERY` in `pt_common.hlsli`), with Shader Execution
  Reordering for two passes; a hit is found by instance, geometry and primitive index (`fetch_hit`).
- All mods share ONE Direct3D 12 device, made by the bridge (`native/bridge`: not yours to change; say what you
  would need of it). Agility SDK 1.619.6. NVAPI is not in the tree today.

## The job, in this order

1. **What the card and driver of this PC report.** Get NVAPI (its own new, empty folder in your copy's root, as
   the other SDKs sit in the main tree's root; never committed: say which line `.gitignore` needs), link it into
   the tracer's native side ONLY behind a CMake switch (`TGPT_MEGA_GEOMETRY`, OFF by default: a build without it
   is the build of today, and so is every run on a card that is not NVIDIA's or a driver without it), and log at
   start-up what the device reports: cluster operations, the partitioned top level, their limits. Capabilities
   come from what the runtime reports, never from a table of cards.
2. **What today's structure work costs**, so that the gain has a ceiling: GPU time and bytes, per frame and per
   kind (still meshes at arrival, the plants' rebuild or refit each frame, characters, water's pieces, the top
   level), as a log line every 10 s beside the stages' ("PT structures 10s: ..."). C++ only. Where the code
   already says it, read it; where not, time it.
3. **HAND-OVER 1, within your first hour** (written into the report, then go on): the capability line and the
   structures' line built (C++ only: copy the main tree's `build\native\generated\pathtracing\*.h` into your build
   folder's `generated\pathtracing\` after configuring, once the main tree's build is done, so no shader
   compiles), the one harness run that reads them (`metal`, the plainest recipe), and YOUR PLAN with numbers:
   which of the two parts goes first and what it saves in ms and MB in the town, the forest and by the water,
   from NVIDIA's documents and our counts. The coordinator runs it in the game while you build on.
4. **The first piece, behind the compile switch and a hidden `Advanced / Mega Geometry trial`** (a spare field,
   never an option bit: all 32 are used; say which field), with the plain path untouched beside it and chosen
   whenever the runtime does not report the feature or a call fails (a log line says which). My guess, yours to
   overrule with reasons: the partitioned top level is the smaller change (no shader reads a cluster), the
   clusters for the plants' posed copies the larger gain (templates: the topology stays, the vertices move). THE
   PICTURE DOES NOT CHANGE: the same triangles, the same hits; a hit's instance, geometry and primitive must mean
   what they mean today, or `fetch_hit` learns the cluster's way of saying it in the same piece.
5. Hand over each piece with its switch, the run that shows it (the structures' line and the memory line, on and
   off) and what you expect. STOP when the first piece is handed over and say what the next would be.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- The plain path stays whole and is the default: AMD and Intel cards, older NVIDIA drivers and every build without
  the switch run exactly today's code. Nothing a player must install or set.
- No limits that change the picture, no coarser geometry, no level of detail of our own (the tracer takes the
  level the game draws). What would change the picture is a choice you write up, not a thing you build.
- Shader compiles only if your piece needs them: `tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME
  -Throttle 2` before `cmake --build` (configure with `-DTGPT_WITH_NRD=OFF`); never `-First`, never a raised
  limit; "the coordinator's build of the main tree goes first" means: wait, nothing is wrong.
- Offline: never the whole GPU test or ctest as a whole ("full test suite never in subagents"). A small check of
  your own in a `..._ONLY` mode, through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never
  while the game runs; a memory ceiling of 8 GB on any test run (if the driver's first making of the two
  reordering passes takes a test past it, use the test's `TGPT_TEST_NO_AGILITY=1` mode or leave it to the
  coordinator: do not raise the ceiling); wait for the GPU with ONE background script, never by polling.
- RTXDI and NRD sources never copied or quoted. NVIDIA's Mega Geometry SDK and samples: read; copy only what its
  license allows into a repo like this one, and say which license. Unity's, HDRP's and the game's code: learn,
  copy nothing. Downloads go into their own new, empty folders; run nothing from a download.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit, check
  every hunk landed. Two other workers are in `tracer.cpp` and `scene.cpp` (memory: allocations; speed: the
  water's path): change what you must, move nothing else.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`,
  `docs/SOURCES.md`; `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`,
  `native/upscaling`.
- Build the plugin too if you touch it (0 errors, 0 warnings). `python tests/source_checks.py` passes.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: what the PC reports, what today's structure work costs, what is built and
  behind which switches, the run that shows it and what you expect, what is next, the sources and their
  licenses in a line, that it compiles and the checks pass, the report's path.
