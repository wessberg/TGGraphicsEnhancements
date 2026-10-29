# Brief: megageo-2 - RTX Mega Geometry part 2: the plants as clusters stamped from templates (PT-109) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\tg-agents\wt\megageo2` (pristine base: `...\wt\base20mg`). Follow `AGENTS.md` there;
read "The owner's rulings" first (no limits that change the picture; Shader Execution Reordering, micromaps and the
denoisers stay on and are designed for; the order of work). Report: `docs/agents/megageo-2.md` IN YOUR COPY; the plan
within 20 minutes, kept current; "State when stopped" at once if a limit shows. You are the ONE subagent at a time.

## Why

The owner, 2026-10-11: "please do add RTX Mega Geometry"; ~13:30, after a session where textures waited near the video
memory budget, things streamed in with "extreme stutter" and the GPU hung: "That's why I still think RTX Mega Geometry
will help if it can help us manage the BVH"; ~13:40: "RTX Mega Geometry I think we'll have to get into the game really
soon". The first worker's report is `docs/agents/megageo-1.md` (read "READ THIS FIRST", 1.4 "THE PLAN, with numbers",
2 and 3 whole; the rest by grep). Part 1 (the partitioned top level) is built behind CMake's `TGPT_MEGA_GEOMETRY` and the
hidden `Mega Geometry trial`; it saves 0.03 ms. Part 2 is where the gain is: the plants' groups (refitted each frame, 640
MB of posed copies and structures in the forest today, 2.8 ms of refits) as clusters stamped from templates, by
megageo-1's estimate about 220 MB and 2 to 3.5 ms in the town, with NO new shader passes (a cluster carries its own
submesh number; `GeometryIndex()` and `PrimitiveIndex()` then find the triangle as `fetch_hit` does today).

## What to do (megageo-1 section 3, item 2, in this order)

(a) A small offline program (a test executable beside `tgpt_gpu_test`, or a `..._ONLY` mode of its own) that builds the
    cluster templates for ONE real plant mesh shape (a few thousand triangles, posed copies with the plants' own pose
    pass if you can reach it, else a fixed deformation), stamps a few hundred posed copies, builds their bottom levels
    and reads back the real sizes and times ON THIS DRIVER (NVAPI cluster operations; `nvapi.h` lines are cited in
    megageo-1), against the plain refitted structures of the same copies. Write the numbers into the report AT ONCE: they
    decide whether (b) to (d) are worth it (megageo-1: the driver's own worst case gives no saving).
(b) If (a) says it pays: the clusters cut at mesh conversion (runs of consecutive triangles within 256 vertices; before the
    mesh's CPU indices are dropped, scene.cpp around the BLAS build), one template set a mesh, built once.
(c) The plant groups' bottom levels built from stamped clusters, each cluster a submesh record (`plantGeometries_` and the
    material list), behind the same hidden trial setting (a value of its own), with the micromaps the plants have today
    (NVAPI clusters accept OMMs: `opacityMicromapArray`, FLAG_ALLOW_OMM).
(d) Its check: the plants' picture the plain path's, pixel for pixel (no denoiser), a small fixture.
Stop after (a) if the numbers say no, and after (d) otherwise. Not yours: the water and characters as clusters, the
coordinator's files (`BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`, `CHANGELOG.md`).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests (your own program and your
  fixture only; NEVER the whole `tgpt_gpu_test` or ctest as a whole) only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0. Build into `%TEMP%\tgb\megageo2-...`; `tools\Build-PtShaders.ps1`
  before `cmake --build` (never `-First`). Grep and read ranges of the big files, never whole.
- NVIDIA's NVAPI (R615, MIT) is in the MAIN tree's `NVAPI-R615\` (not linked into the copy): configure your build with
  `-DNVAPI_ROOT=C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\NVAPI-R615 -DTGPT_MEGA_GEOMETRY=ON`. Read it, cite line
  numbers, copy no sample code text. RTXDI and NRD
  sources are never copied or quoted. Say in the report every export, record, FrameConstants row or option bit you add.
- Final message: five lines at most, pointing at the report.
