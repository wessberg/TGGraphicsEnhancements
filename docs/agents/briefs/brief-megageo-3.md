# Brief: megageo-3 - why our clusters trace 8 times slower than NVIDIA's, and the plants on NVIDIA's setup (PT-109)

Copy: `C:\Users\Frede\AppData\Local\Temp\tg-agents\wt\megageo2` (the last worker's copy: its bench is there; pristine base
`...\wt\base20mg`). Follow `AGENTS.md` (the MAIN tree's copy is newer: read its ruling "Follow NVIDIA's and AMD's guides to
the letter", 2026-10-11). Report: `docs/agents/megageo-3.md` IN YOUR COPY; the plan within 20 minutes, kept current;
"State when stopped" at once if a limit shows. You are the one Claude subagent running.

## Why

megageo-2 (`docs/agents/megageo-2.md` in the copy: read "READ THIS FIRST", the table and "Other things measured" whole)
measured the plants as clusters stamped from templates with NVAPI R615 on this RTX 5080 (driver 616.92): structures
23-50 % smaller, the wind's per-frame work 45-60 % faster, the same hits to the bit, but rays through clusters 40-56 %
SLOWER even on a smooth sphere (trees 60-130 %), where NVIDIA's own sample reports about 5 %. The owner, 2026-10-11:
"Clearly we are integrating it incorrectly? Still want us to get this done", and the ruling: "Obviously we want to 100%
follow NVIDIA's and AMD's implementation guidelines for these technologies". So the 8x gap is taken as OUR fault until
NVIDIA's own setup, reproduced, shows otherwise.

## What to do, in this order

(a) NVIDIA's guidance, all of it, written into the report with links: the NVAPI cluster API's comments (`nvapi.h` in
    the MAIN tree's `NVAPI-R615\`, lines megageo-2 cites), NVIDIA's RTX Mega Geometry samples and libraries
    (nvpro-samples: `vk_animated_clusters` (its README's 5 %: which scene, which GPU, cluster sizes, what it compares),
    `vk_tessellated_clusters`, `vk_lod_clusters`, `nv_cluster_builder` (NVIDIA's clusterizer for ray tracing),
    `vk_partitioned_tlas`; NVIDIA-RTX `RTXMG` (the DX12 sample) if it exists; NVIDIA's developer docs and blog posts
    on clusters). For each: cluster triangle/vertex counts, the clusterizer and its options, vertex format and position
    truncation, index format, the template vs direct CLAS choice, every build flag (CLAS, templates, BLAS from CLAS:
    FAST_TRACE/FAST_BUILD, ALLOW_OMM, maxGeometryIndex, maxClusterCountPerArg...), alignment, how they measure.
(b) Diff that against megageo-2's bench (`tests/megageo_cluster_bench.cpp` etc.) and list every difference.
(c) Reproduce NVIDIA's setup in the bench on the sphere first (their clusterizer: `nv_cluster_builder` is Apache-2.0,
    fetch it beside the copy, e.g. `%TEMP%\tgb\megageo3-deps`, never into the main tree; their cluster sizes; their
    flags), and find which difference closes the gap. Write the numbers into the report AT ONCE as you get them.
(d) Then the game's plants on that setup (the same meshes and table as megageo-2), and a verdict: does it pay now?
Stop after (d), or earlier if (c) shows NVIDIA's exact setup is still 40 % slower here (then: say what is left, e.g. a
D3D12-vs-Vulkan or driver question, with the evidence).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU runs (your bench only; NEVER
  `tgpt_gpu_test` or ctest) only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck`
  exits 0; quote times only from runs with the card to itself. Build into `%TEMP%\tgb\megageo3-...`.
- Grep and read ranges of big files. NVAPI is MIT (read it, cite lines). RTXDI and NRD sources are never copied or
  quoted. Code you take from NVIDIA's Apache-2.0 samples: say which, and keep it out of the main tree unless the report
  proposes it with its license.
- Final message: five lines at most, pointing at the report.
