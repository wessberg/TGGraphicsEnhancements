# Brief: nrd-confidence - NRD's history confidence from lighting gradients, as NVIDIA documents it (PT-127)

Copy: COPYNAME (pristine base: BASENAME). Follow `AGENTS.md` in your copy; read first its rulings "Follow NVIDIA's and
AMD's guides to the letter", "The game is the first test" and "Code that is switched off still costs frame time".
Report: `docs/agents/nrd-confidence.md` IN YOUR COPY; the plan within 20 minutes, kept current; "State when stopped" at
once if a limit shows.

## Why

The owner, 2026-10-11: "Obviously we want to 100% follow NVIDIA's and AMD's implementation guidelines for these
technologies". NRD now runs as its README sets it up (PT-127: `docs/PT-REQUESTS.md`, grep "### PT-127"; the code:
`native/pathtracing/shaders/pt_nrd_prep.hlsl`, `native/pathtracing/src/nrd_denoiser.cpp`, the NRD block of `tracer.cpp`,
grep `nrdKind`) except for what the README calls "optional, but highly recommended": IN_DIFF_CONFIDENCE and
IN_SPEC_CONFIDENCE (`NRD-SDK-4.17.3/README.md` "HISTORY CONFIDENCE" and the IN_DIFF/SPEC_CONFIDENCE input; `NRDSettings.h`
`isHistoryConfidenceAvailable`; NRD's CMake define NRD_SUPPORTS_HISTORY_CONFIDENCE). With NRD's history set in seconds at
~10 frames a second, lag is short already; confidence lets a lamp that flickers or a light turned off clear NRD's
history where it changed, instead of NRD's own anti-lag alone.

## What NVIDIA's README asks (read it whole)

A gradient: "stored" radiance of the previous frame against the same samples "traced" in the current frame with the
previous frame's random seed, at about a fifth of the render resolution (strata), no previous TLAS needed, some relaxation
on dynamic objects; blurred spatially (5 passes of 5x5 with growing stride, geometry and normal weights), converted to a
confidence in [0, 1] (REBLUR and RELAX use it differently: RELAX `historyLength = min(historyLength, maxAccumulatedFrameNum
* confidence)`). The tracer's direct light is RTXDI's ReSTIR DI (`pt_rtxdi.hlsli`, `pt_di.hlsl`, `pt_di_spatial.hlsl`);
NVIDIA's own RTXDI sample computed such gradients for ReSTIR DI (its "gradients" pass; RTXDI 3.1's library has no helper:
the method is the README's). The bounced light is ReSTIR GI (`pt_gi.hlsl`, `pt_gi_spatial.hlsl`).

## What to do

(a) The design in the report first: which signal's gradient (the direct light's at least: re-shade each stratum's previous
    light sample at its previous surface with this frame's lights and visibility; the bounced light's if the README's
    route fits), the strata, the passes, the textures, what NRD's settings change (`isHistoryConfidenceAvailable`, the
    confidence-driven relaxations: their defaults), behind a hidden switch first (a value of the hidden `NRD trial` setting
    or a frame flag bit 21 and up: say which).
(b) Build it, one pass of its own (never more code in `pt_gi`, `pt_specular`, `pt_prepare` or the walk), and hand the
    coordinator ONE trial build for a tavern run (the switch off and on, NRD, the world running: the candles flicker)
    within your first two hours, or say why not.
(c) After the game answers: a small check of your own (a light switched off between frames: confidence 0 there, 1 in a
    still scene), the report.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small mode only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (a cold pass cache
  needs `TGPT_TEST_STARTUP_SECONDS=900`); NEVER the whole `tgpt_gpu_test` or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\nrd-confidence-...`; NRD's own CMake writes into its SDK folder: configure as the vendor-rr worker did
  (`docs/agents/vendor-rr.md` "Build isolation") so the main tree's SDK is never written. RTXDI and NRD sources are never
  copied or quoted (identifiers only). Grep and read ranges of the big files. Say every export, record, FrameConstants
  row, option or flag bit you add.
- Final message: five lines at most, pointing at the report.
