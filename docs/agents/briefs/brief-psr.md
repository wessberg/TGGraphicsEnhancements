# Brief: psr - one coherent surface per pixel for the denoisers: NRD's primary surface replacement for mirrors, glass and water, and Ray Reconstruction's guides (PT-127; review-vendor-astra 26, 3, 20)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-11 ~19:00). Follow `AGENTS.md` in your copy;
read first its rulings "Follow NVIDIA's and AMD's guides to the letter" (in "The owner's rulings"), "The game is the first
test", "Remove the workarounds as we go" and "Code that is switched off still costs frame time". Report:
`docs/agents/psr.md` IN YOUR COPY; the design within 30 minutes, kept current; "State when stopped" at once if a limit
shows.

## Why

The owner, 2026-10-11: "make sure NRD is set up to perfection, then, I no longer trust anything I look at which use NRD",
then "Obviously we want to 100% follow NVIDIA's and AMD's implementation guidelines for these technologies". NRD now runs
as its README sets it up (PT-127: `docs/PT-REQUESTS.md`, grep "### PT-127"), with three things left that GPT-6-astra's
review found (`docs/agents/review-vendor-astra.md`, findings 26, 3 and 20; read them whole):

- 26: glass, water and what is under water never reach NRD (`pt_nrd_prep.hlsl`, grep `glass`): our own reflection filter
  smooths them (`pt_atrous.hlsl`, `pass.b.z` "only through": since 10-11 it runs only on those pixels under NRD; part of
  the 2.8 ms of the tavern's denoising stage, run 20261011-164811). NRD's README "INTERACTION WITH PRIMARY SURFACE
  REPLACEMENTS" (`NRD-SDK-4.17.3/README.md`, ~line 601 to 640, and lines 9 to 11 on the sample's glass) and the linked
  NVIDIA article describe the documented way: the guides of a pixel that shows a pure mirror or smooth glass describe the
  first surface that is not a pure mirror behind it (its depth, normal, roughness, motion, material ID), and its light is
  split and demodulated there. Mirrors of smooth metal (the mirror scenes of `tests/ingame/scenarios.json`, PT-46) take
  NRD's ordinary specular path today: say in the design whether NVIDIA's rule makes them PSR too.
- 3: a hair pixel's normal guide can describe the first hair card while its depth, motion and reflection distance
  describe the surface the ray really stopped at (`pt_primary.hlsli`, grep "hair" near the guides; `pt_compose.hlsl`'s
  guide choice by coverage).
- 20: glass hands Ray Reconstruction the length of the whole bent path as its reflection distance
  (`pt_dielectric.hlsli`, `pt_specular.hlsl`, grep the glass distance), where the RR guide asks for the world-space
  distance from the primary surface to the hit (`%TEMP%\tgb\vendor-docs\dlss-rr-guide.txt` sections 3.4.1 to 3.4.9;
  around its line 511 the dense motion vectors for reflections).

## What to do

(a) The design in the report first, before code: which pixels get a replaced surface (NVIDIA's rule for "pure mirror"
    and smooth glass, with its numbers), what every guide then holds (NRD's and RR's, one surface per pixel: normal, depth,
    motion, roughness, hit distance and material ID describing it together; NRD README's hitT rules for bounces before a
    PSR, ~line 508), how the mirror's or glass's own reflectance and transmission enter demodulation and remodulation, what
    happens to our reflection filter on glass and water under NRD (it is a workaround of ours: removed when the documented
    way covers those pixels, in the same job), and what RR's guide asks that NRD's does not. Say what you cannot follow to
    the letter and why.
(b) Build it behind frame flag bit 22 (`NativeTracer.cs` `1u << 22`, a hidden `Advanced` setting "Primary surface
    replacement (trial)", default off; say the native name you give it), for NRD (kinds 1 and 2) and no denoiser; RR's
    guides in the same switch where they fit. Add a recipe `psr` in `tests/ingame/scenarios.json` in the pattern of
    `nrd-confidence` (the game's picture, then NRD with the switch off and on, the world running), and name the two or three
    scenes it should run on (water, glass, a mirror). Hand the coordinator ONE trial build for those scenes within your
    first two hours, or say why not.
(c) After the game answers: your own small check (a mirror and a glass pane in the offline fixture: the guides describe
    the surface behind; the light with the switch on and off the same in a still scene), then the report.

Not yours: RTX Mega Geometry (worker megageo-6), the caustics' cost (worker caustcut), RTXDI's parameters.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small mode only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (a cold pass cache
  needs `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test` or ctest as a whole. The ctest GPU tests never
  run Ray Reconstruction: say what of RR's part only the coordinator's `tgup_gpu_smoke_with_pt` and a game run can show.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\psr-...`; NRD's own CMake writes into its SDK folder: configure as the vendor-rr worker did
  (`docs/agents/vendor-rr.md` "Build isolation") so the main tree's SDK is never written. For any `dotnet` call set
  `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true` and `MSBuildEnableWorkloadResolver=false` first. RTXDI and NRD sources
  are never copied or quoted (identifiers only). Grep and read ranges of the big files (`pt_common.hlsli`,
  `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`, `tracer.cpp`). Say every export, record, FrameConstants
  row, option or flag bit you add. Run `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
