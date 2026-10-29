# Brief: megageo-5 - RTX Mega Geometry for the plants as NVIDIA designs it: micromaps in clusters, a structure per copy, measured walking (PT-109)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-11 ~17:15, megageo-4's trial in it). Follow
`AGENTS.md` in your copy; read first its rulings "Follow NVIDIA's and AMD's guides to the letter", "The game is the first
test" and "Code that is switched off still costs frame time". Report: `docs/agents/megageo-5.md` IN YOUR COPY; the plan
within 20 minutes, kept current; "State when stopped" at once if a limit shows. You are the one Claude subagent running.

## Why

The owner, 2026-10-11 ~17:05, told that megageo-4's clusters made the forest 16 ms slower: "It has to be a bug or an
implementation oversight that [RTX Mega Geometry] is slower? What about over time, as we move around the world? we wont
have to rebuild the bvh constantly, which is the point of it, right?" Read `docs/agents/megageo-4.md` ("READ THIS FIRST",
"Departures from NVIDIA's setup", "Next"), `docs/agents/megageo-3.md` ("READ THIS FIRST" and NVIDIA's setup), and
`docs/PT-REQUESTS.md` PT-109's last two items (the game run `20261011-163231`, main tree `artifacts\harness`: the town
plain 64.5-67.7 ms, clusters 68.6; the forest plain 83.4-85.1, clusters 100.8, the split without micromaps 95.8).

Our departures from NVIDIA's design, which are this job:
1. The clusters carry no micromaps: NVAPI's cluster operations DO take them (`FLAG_ALLOW_OMM`, `opacityMicromapArray`,
   `opacityMicromapIndexBuffer` per template / CLAS: `NVAPI-R615\nvapi.h` ~20873, 21065-21110) but in NVAPI's own OMM
   arrays (`NvAPI_D3D12_BuildRaytracingOpacityMicromapArray`), not DirectX Raytracing 1.2's. Without them every ray
   through a leaf asks the shader: 11 of the forest's 16 ms.
2. The plants are groups of 16 to 18 overlapping copies in ONE structure; NVIDIA's samples (`vk_animated_clusters`,
   `vk_partitioned_tlas`) give each copy its own cluster BLAS, instanced in a partitioned top level (our PT-109 part 1,
   the "Mega Geometry trial" 1 to 3, already built), and NVIDIA's docs warn of overlap.
3. Never measured moving: the point of Mega Geometry is that structures for what changes or streams in are cheap; the
   owner saw stutter and a GPU hang walking into new ground (PT-124, PT-125).

## What to do

(a) Micromaps in clusters: NVAPI OMM arrays built from the same bake our DXR 1.2 micromaps come from (pt_omm_bake's
    output; the index buffer in the clusters' triangle order), linked to the templates/CLAS; the plants' picture as the
    plain path's (`TGPT_TEST_MEGAPLANTS_ONLY` extended).
(b) NVIDIA's layout behind a new trial value: every plant copy (small and tall alike) its own cluster BLAS stamped from
    its mesh's templates in the wind's pose, instanced in the partitioned top level; say what the plain groups stay for.
(c) A walking measurement: the harness already walks (`walkSpeed` in a step, `docs/PT-HARNESS.md`; the DLSS 5 runs used
    it): a recipe that walks the same route through the town and into the forest with the trial off and on, reading the
    frame times' spikes (the stutter), the scene stage's time, structure builds, bakes and memory per second.
(d) Hand the coordinator ONE build for (b)+(a) and the walking recipe within your first two hours, or say why not; the
    coordinator runs it (`-Measure`, the game is in front).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU runs: your benches and your own
  small check only, only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0
  (a cold pass cache: `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test` or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\megageo5-...`, configured with `-DNVAPI_ROOT=C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\NVAPI-R615
  -DTGPT_MEGA_GEOMETRY=ON` and FetchContent connected once (meshoptimizer). Grep and read ranges of the big files.
  RTXDI and NRD sources are never copied or quoted. Say every export, record, FrameConstants row, option or flag bit.
- Final message: five lines at most, pointing at the report.
