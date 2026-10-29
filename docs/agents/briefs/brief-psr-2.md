# Brief: psr-2 - primary surface replacement after the game's answer: mirrors become the rule, then glass and water (PT-127)

Copy: `%TEMP%\tg-agents\wt\psr` (the worker psr's own copy, with its work in it; pristine base: `%TEMP%\tg-agents\wt\base20pc`).
You are a fresh agent: read the first worker's report `docs/agents/psr.md` in that copy first, whole, then its brief
(`docs/agents/briefs/brief-psr.md`). Follow `AGENTS.md` in the copy; read its rulings "Follow NVIDIA's and AMD's guides to
the letter", "Remove the workarounds as we go", "Code that is switched off still costs frame time" and "The game is the
first test". Report: a new file `docs/agents/psr-2.md` IN THE COPY; the plan within 20 minutes, kept current; "State when
stopped" at once if a limit shows.

## The game's answer (2026-10-11 21:45, run `20261011-213531` in the main tree's `artifacts\harness`, `mirror-tavern@psr-mirror`)

The first worker's build (native SHA 7DD2E5E3..., the hidden `Primary surface replacement (trial)`, frame flag bit 22), the
world running, the mirror's region: no denoiser detail 3.78, flicker 1.10, boil 0.29; NRD with the switch off detail
0.47, flicker 0.61, boil 0.45; NRD with it ON detail 2.41, flicker 0.83, boil 0.27; off again 0.48 / 0.60 / 0.44. Whole frame
luma 15.1 / 15.0 / 15.1. In the picture the mirror shows the room's planks and fittings where it was a blur
(`mirror-tavern\overview.jpg`). The direction is right. `glass-jars@psr` and `water-day@psr` were not run (controls only).

## What to do

(a) Mirrors: finish what the first worker's report lists as owed after the game's answer: the small offline check (a
    mirror in the fixture: the guides describe the surface behind, the light with the switch on and off the same in a
    still scene), and the cost: `pt_specular` carries the capture now (the first report: "a larger specular shader even
    with the frame switch off"): measure the reflections' pass with and without the capture compiled in (your own
    small mode's stage times, the same frames), and say the number. Then make it the rule for NRD: no hidden switch, no
    frame flag (bit 22 free again), the recipe a plain look recipe; or, if the pass costs measurably more for pixels
    that are no mirror, restructure first (what a thread carries; a compile switch is not an answer for a rule).
    The flicker in the mirror rose with it on (0.83 against 0.61; the undenoised 1.10): say whether that is the mirror's
    own noise now shown, or motion and hit distances of the replaced surface to correct (NRD README's rules for PSR).
(b) Glass and water: the first report says why they are not a switch (two delta branches, each needing its own guides
    and signal; water's rough path and volume). Write the design the owner can rule on, with what it costs (passes,
    textures, NRD instances, milliseconds by your estimate) and what NVIDIA's sample does, IN YOUR REPORT, plainly; build
    nothing of it before the coordinator says so. Our own reflection filter on glass and water stays until then.
(c) Ray Reconstruction's guides (review 3 and 20, in the first worker's build): they cannot run in your tests. List for
    the coordinator exactly what to run and what to look at (`tgup_gpu_smoke_with_pt`, the recipe `with-rr`).
(d) Hand the coordinator ONE build with (a) within two hours, with the list of files to merge (the main tree moved since
    `base20pc`: `tracer.cpp`, `gpu.hpp`, `scene.cpp`, `mega_geometry.*`, the fog files, `tests/source_checks.py`,
    `tests/ingame/scenarios.json`; the merge is the coordinator's, three-way against `base20pc`).

Not yours: the fog grid (worker fogfix), the characters' micromaps (worker ommout), RTX Mega Geometry, the caustics.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small mode only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (wait with one
  background script, never poll; a cold pass cache needs `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test`
  or ctest as a whole.
- Build as the first worker did (its report: `tools\psr\configure.cmd`, `build.cmd`, the build folder
  `%TEMP%\tgb\psr-build`; `Build-PtShaders.ps1` never with `-First`). The plugin builds with `dotnet build` of
  `src\TGPathTracing` in the copy (the coordinator built it so on 10-11; set `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true`
  and `MSBuildEnableWorkloadResolver=false` first). RTXDI and NRD sources are never copied or quoted. Grep and read ranges
  of the big files. Say every export, record, FrameConstants row, option or flag bit you add or free. Run
  `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
