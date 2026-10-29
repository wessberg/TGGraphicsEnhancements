# Brief: megageo-6 - RTX Mega Geometry's NVIDIA layout: the forest's GPU hang, and where its frame time goes (PT-109)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-11 ~18:55, megageo-5 merged in it). Follow
`AGENTS.md` in your copy; read first its rulings "Follow NVIDIA's and AMD's guides to the letter", "The game is the first
test" and "Code that is switched off still costs frame time". Report: `docs/agents/megageo-6.md` IN YOUR COPY; the plan
within 20 minutes, kept current; "State when stopped" at once if a limit shows. You are the one Claude subagent running.

## Why

The owner wants RTX Mega Geometry done right ("Still want us to get this done"; "we wont have to rebuild the bvh
constantly, which is the point of it, right?"). megageo-5 (`docs/agents/megageo-5.md`: "READ THIS FIRST", what was built,
the walk) built NVIDIA's layout as the hidden "Mega Geometry trial" 7. The game run `20261011-182915` (main tree
`artifacts\harness`; its `report.md` per-second tables and `TGPathTracing.Native.log`, grep "Mega Geometry trial" and "The
GPU stopped"): in the town's walk the frames were 140 / 149 ms with trial 7 against 127 / 126 plain, the worst 319 / 390
against 268 / 264, while the tracer's GPU time was the same (69 to 72 ms); in the forest trial 7 HUNG THE GPU (TDR) in the
scene stage ("stopped in stage scene ... the next kind's work did not end"), frame 4003, during its second walk. And
`docs/PT-REQUESTS.md` PT-109 (grep "### PT-109") whole.

## What to do

(a) The hang first: find why the scene stage's cluster work never ended in the forest (an argument count past the room
    reserved, a scratch or destination too small for a frame with more plants or groups than before, an indirect count
    read from memory not yet written, a barrier missing between NVAPI's cluster operations, a micromap array still being
    built): reason it from the code and NVAPI R615's rules (`NVAPI-R615\nvapi.h`, the cluster operations' comments), and
    make the conditions that hang a check your own small test can hit (`TGPT_TEST_MEGAPLANTS_ONLY`, extended: e.g. a frame
    whose plants outgrow the last frame's room). Fix it.
(b) Where the frame time goes with trial 7: the CPU (the tracer's record time, the plugin's), the GPU outside the traced
    passes, per plant bottom level; against what NVIDIA's own `vk_animated_clusters` does per frame (one indirect call for
    all, arguments written on the GPU; `%TEMP%\tgb\megageo3-deps`). Bring ours to NVIDIA's way where it differs.
(c) Hand the coordinator ONE build for `forest@mega-walk` and `metal@mega-walk` (then `mega-plants`) within your first two
    hours, or say why not.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU runs: your own small check only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (a cold pass cache:
  `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test` or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\megageo6-...`, configured with `-DNVAPI_ROOT=C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\NVAPI-R615
  -DTGPT_MEGA_GEOMETRY=ON` and FetchContent connected once (meshoptimizer). For any `dotnet` call set
  `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true` and `MSBuildEnableWorkloadResolver=false` first. Grep and read ranges
  of the big files. RTXDI and NRD sources are never copied or quoted. Say every export, record, FrameConstants row, option
  or flag bit you add.
- Final message: five lines at most, pointing at the report.
