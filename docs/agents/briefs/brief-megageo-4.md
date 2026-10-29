# Brief: megageo-4 - RTX Mega Geometry in the game: the small plants as clusters, NVIDIA's way (PT-109)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-11 ~15:40). Follow `AGENTS.md` in your copy;
read first its rulings "Follow NVIDIA's and AMD's guides to the letter", "The game is the first test" and "Code that is
switched off still costs frame time". Report: `docs/agents/megageo-4.md` IN YOUR COPY; the plan within 20 minutes, kept
current; "State when stopped" at once if a limit shows. You are the one Claude subagent running.

## Why

The owner, 2026-10-11: "RTX Mega Geometry I think we'll have to get into the game really soon", and ~15:00: "Still want us
to get this done". megageo-2 and megageo-3 (`docs/agents/megageo-2.md`, `docs/agents/megageo-3.md`: read megageo-3's
"READ THIS FIRST" and its NVIDIA setup section whole; benches `tests/megageo_cluster_bench.cpp`,
`tests/megageo3_cluster_bench.cpp`) measured offline: with NVIDIA's own setup (meshoptimizer's spatial clusterizer, MIT;
64 or 128 triangles a cluster; 32-bit indices into each copy's own vertices, no per-cluster vertex lists; FAST_TRACE),
clusters in our plant groups cost trees +31 to +55 % a ray but grass +9 to +27 % and bushes and ferns +4 to +14 %, and save
45 to 58 % of the wind's per-frame work and 7 to 35 % of the structures. The verdict: a GAME trial for the small plants
only, trees plain. Only the game can say what rays spend inside plant groups.

## What to do

(a) In the tracer (`native/pathtracing/src/scene.cpp`/`.hpp`, `mega_geometry.hpp`, CMake's `TGPT_MEGA_GEOMETRY` with
    NVAPI R615 in the main tree's `NVAPI-R615\`): the plant groups whose meshes are small plants (grass, bushes, ferns:
    find how the scene tells them from trees, and say) built from stamped cluster templates as megageo-3's bench does
    (templates once a mesh at conversion, before its CPU indices are dropped; each frame's wind pose stamped and the
    group's bottom level built from the clusters), with the micromaps the plants have today if NVAPI's clusters take them
    (FLAG_ALLOW_OMM, `opacityMicromapArray`: say what you found), each cluster a submesh record so that `fetch_hit` finds
    the triangle as today. Behind the hidden "Mega Geometry trial" (TGPT_Frame.flags bits 2 to 5; a value of its own;
    every other value as today). meshoptimizer: fetched by CMake like the other dependencies (say how; MIT notice in
    `LICENSES/` and `docs/SOURCES.md` as the other SDKs are noted), never copied by hand.
(b) Hand the coordinator a trial build for ONE run of the town and the forest (the trial off and on, the world running,
    no denoiser; `-Measure`: the coordinator says when), within your first two hours, or say why not.
(c) After the game has answered: a small check of your own that the plants' picture is the plain path's (no denoiser,
    pixel for pixel but ties), and the report.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU runs: your benches and your own
  small check only, only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0;
  NEVER the whole `tgpt_gpu_test` or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`) then the native build into
  `%TEMP%\tgb\megageo4-...`; configure with `-DNVAPI_ROOT=C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\NVAPI-R615
  -DTGPT_MEGA_GEOMETRY=ON`. Grep and read ranges of the big files. RTXDI and NRD sources are never copied or quoted.
  Say every export, record, FrameConstants row, option or flag bit you add.
- Final message: five lines at most, pointing at the report.
