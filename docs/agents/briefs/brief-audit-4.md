# Brief: audit-4 - are RTXDI, Shader Execution Reordering, Opacity Micromaps and RTX Mega Geometry used correctly and fully?

Read-only review of TG Path Tracing (`native/pathtracing`) in the main tree `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`.
Follow `AGENTS.md` (loaded for you). The ONLY file you write is your report `docs/agents/audit-4.md`; write its outline
first and keep it current (a stop loses nothing; if a usage limit shows, write "State when stopped" at once). Never start
the game, deploy, build, run a GPU test, edit code or commit. Never read the big files whole (`pt_common.hlsli`,
`tracer.cpp`, `scene.cpp`, `pt_gi.hlsl`, `pt_specular.hlsl`, `tests/pathtracing_gpu_test.cpp`, `docs/PT-REQUESTS.md`,
`docs/PROJECT-CONTEXT.md`): grep, then read ranges. RTXDI and NRD sources (`RTXDI-Library-3.1/`, `NRD-SDK-4.17.3/`) may be
read but never quoted into the report beyond a line reference (their license).

## Why (the owner, 2026-10-11 02:55, going to bed)

"I especially want you to be absolutely certain we're doing everything correctly in terms of RTXDI, Shader Execution
Reordering, Opacity Micromaps and RTX Mega Geometry. Our target FPS continues to be so much higher than the territory we're
in, and I'm sure there are some clear bottlenecks here and there." The target: 55 fps ("As performant as Cyberpunk's PT").
Today, clean runs (RTX 5080, 4K output, DLSS Performance: render 1924x1084, no denoiser): tavern ~44 ms a frame, a town
~81 ms, the beach 113 to 117 ms. The town's stages (ms, run `20261011-011046`, `TGPathTracing.Native.log` "stages:"):
scene 1.3, prepare (camera rays) 4.2, bounced light 23.2, its spatial reuse 1.8, direct light 6.6, reflections 23.6,
caustics 4.8, composite 0.6. A traced frame also carries the game's own work on the card (~12 to 20 ms; R19).

## Rulings that bind every recommendation (AGENTS.md)

Shader Execution Reordering is never switched off (a slower reordered pass is our design fault). No limit that changes what
the picture converges to (fewer rays, shorter paths, coarser passes are the owner's choice, shown with numbers, never
taken silently). Code switched off still costs in the big passes. The game is the first test: every recommendation names
the one small in-game trial that would prove it (a scene of `tests/ingame/scenarios.json`, the switch, the number).

## Start from

`docs/agents/compat-1.md` (the review of 2026-10-10, ranked R1, S5, M1/M2, S1-S3, ...) and what was built since:
`docs/agents/rule-1.md`, `rule-2.md` (the bounced light's pass split, one regrouping after the first ray, RTXDI's reuse a
pass of its own: `pt_gi_reuse`), `omm-1.md`, `docs/agents/speed-1.md`, PT-99, PT-109, PT-110, PT-111 in
`docs/PT-REQUESTS.md` (grep the headings), PROJECT-CONTEXT 9.5 and 9.13, `docs/rounds/round-19.md` sections 1 to 4.
For each finding of compat-1: done, partly, or open, with the code line that shows it.

## What to answer, per technique, with file and line for every claim

1. RTXDI 3.1 (DI and GI, `pt_rtxdi.hlsli`, `pt_restir.hlsli`, `pt_di*.hlsl`, `pt_gi*.hlsl`): is every bridge function
   (RAB_*) correct and cheap (the light sampling, the target pdf, visibility, the surface and previous-surface loads,
   the Jacobian), are the reservoirs' layout, temporal and spatial reuse, boiling filter, permutation sampling and
   bias correction as NVIDIA's sample does them, and is anything traced or loaded twice. Where the frame time goes in
   `pt_di` and `pt_gi` (DXIL size, live state across ray queries).
2. Shader Execution Reordering (the reordering passes: `pt_gi` ser, `pt_specular` ser, how `dispatch_pass` makes them):
   the hit-object form, the hint and its bits, what each thread carries across the regrouping (the log's "driver's stack a
   thread"), coherence before and after, any regrouping in a loop, against NVIDIA's guidance and the HLSL 2021 / SM 6.9
   specification of HitObject and MaybeReorderThread. Whether the camera rays (`pt_prepare`), the walk and the caustics
   would gain from it.
3. Opacity Micromaps (`pt_omm_bake.hlsl`, the micromap code in `scene.cpp`): what share of the alpha-tested triangles a
   ray meets get maps (the logs' cut-out counts), unknown vs known states, the subdivision level, plants in the wind
   (groups refitted every frame), characters, whether any-hit work remains where a map could settle it, and the prize
   (omm-1.md: 7.6 ms tavern, 17 ms town for perfect cut-outs).
4. RTX Mega Geometry (`mega_geometry.cpp`, PT-109; its first piece is in, switched off): what it would replace (the BLAS
   builds and refits per frame: plants 636 groups and 9.5 M vertices refitted a frame in the town, characters, cloth,
   water units; the top level over 16,000 instances rebuilt every frame), its measured or likely gain now, and whether
   the partitioned top level or cluster structures are the larger win.
5. Anything else you meet that is a clear bottleneck (a pass doing work no pixel uses, a resolution or count higher than
   its result needs without changing the picture, a stall between passes, the composite's waits).

## The report

A one-screen summary on top: the ten most valuable changes, ranked by expected milliseconds saved in the town and the
tavern, each with: what is wrong or missing, the evidence (file:line, log numbers), the change, the trial that proves it,
its size in hours, and whether it changes the picture (if so, say how). Then a section per technique with the details.
Plain words where you can (the owner reads plain words). Stop when the report is done.
