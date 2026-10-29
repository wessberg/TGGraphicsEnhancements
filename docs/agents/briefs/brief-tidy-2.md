# Brief: take the dead lamp trials out of the passes, for speed (PT-99, PT-85) - one worker, stop at the hand-over

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/tidy-2.md`; write it early and after every step.

## Why now

The owner, 2026-10-09: "right now we're at like 8 FPS. It can barely render". Code that is switched off still costs
frame time in the big passes (AGENTS.md, "Rules that cost a round": PT-85's lamp trial doubled the tavern's bounced
light with both its settings at 0; after a day of merged trial switches the reflections' pass ran 6 ms slower with
every one of them off). So what the game has said no to comes OUT of the passes, not merely off.

## The job

The item list is `docs/agents/briefs/brief-tidy-lamps.md` in your copy, written this morning: its section "What to
take out", items 1 to 7 and 10. Read it, then these changes to it:

- **What stays is the near size as it is NOW**, not as that brief says: `light_falloff` with
  `Advanced / Lamp near size` (default 15, centimetres, for a lamp of a tavern candlestick's strength, wider with
  the square root of a stronger lamp's: `LAMP_NEAR_STRENGTH`; `reflected_lamps` uses `light_falloff`). Read
  `docs/PT-REQUESTS.md` PT-85's newest notes (grep the heading) before you touch anything of it. Not one number of
  it changes.
- **Item 2 (`Lamp trial lit surroundings`) is half done**: since today it is compiled out by CMake's
  `TGPT_LAMP_AROUND` OFF (`-D PT_NO_LAMP_AROUND`, `TGPT_NO_LAMP_AROUND` for the tracer's log; the worker `speed1`,
  `docs/agents/speed-1.md` section 1). Now it goes for good: `lamp_around`, `lamp_around_light`, `near_split`,
  `near_first`, `g_nearArmed`, `NearRay` if nothing else uses it, their call sites in `pt_common.hlsli`,
  `pt_gi.hlsl`, `pt_specular.hlsl`; the setting; the CMake option, both defines, the log line; the source checks
  that pin them (replace by one that holds the names are gone); the recipes that set its key.
- **Leave alone** (other workers are in them, or the game has not answered): everything of glass
  (`Glass trial`, `pt_dielectric.hlsli`), of water and caustics (`Water ...`, `Caustics ...` switches), the grass
  trial (`Grass shadow trial`), the relief trial (`Reflection relief trial`, `pt_atrous.hlsl`), the speed trial and
  the measuring build (`Speed trial`, `TGPT_SPEED_MEASURE`, `PT_SPEED_MEASURE`), `Arrival ...` if you meet it.
- **Items 8 and 9 of the old brief are NOT yours** (the GPU test's failing families; the near size's offline
  check): skip them. No GPU test at all in this job.
- **The speed evidence you CAN give without a GPU**: the compiled size of each big pass before and after, from
  your own build of the copy: `pt_gi.h`, `pt_gi_dxr12.h`, `pt_gi_ser.h`, `pt_specular.h`, `pt_specular_dxr12.h`,
  `pt_specular_ser.h`, `pt_prepare_dxr12.h`, `pt_walk.h`, `pt_walk_dxr12.h` (megabytes; the main tree's of 15:21
  today: 25.7, 37.1, 37.4, 34.9, 50.7, 51.0, 32.3, 20.0, 28.4, before the speed trial was merged). Compile those
  nine FIRST of your shader build, write their sizes into the report, and only then the rest.
- **The picture must not change**: every switch you remove was at 0. Remove, do not rewrite; no reordering of live
  code. Say in the report, for each removed block, why nothing live read it.

STOP at the hand-over: the copy compiles (native with NRD off, the plugin, the harness: 0 errors, 0 warnings),
`python tests/source_checks.py` and `python tests/harness_analyze_test.py` pass, the nine sizes before and after,
what went and what stayed. The coordinator merges, builds and measures in the game (`tavern-room@speed`,
`metal@speed`: the same picture, the stage times).

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder. No GPU test in this job.
- Build in `%TEMP%\tgb\COPYNAME` with `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 2`
  before `cmake --build` (at most 2 shader compiles of yours at a time: the PC ran out of memory twice today; never
  `-First`, never a raised limit; a line "the coordinator's build of the main tree goes first" means: wait, nothing
  is wrong). Wait with ONE background script, never by polling.
- All 32 option bits are used; no ABI or size change. The number in `TGPT_Frame.jitter`'s w keeps its packing for
  the near size (bits 0 to 7) and for the grass trial's bit 22; the bits the removed trials had become "not taken".
- RTXDI and NRD sources never copied or quoted.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit, check
  every hunk landed.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`; `src/TGNeuralRendering*`,
  `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`. `CHANGELOG.md` and
  `docs/PROJECT-CONTEXT.md` as the old brief's item 10 says (five lines; keep PROJECT-CONTEXT within its budget).
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: eight lines at most: what went (by item), what stayed and why, the nine sizes before and after,
  that everything compiles and the checks pass, the report's path.
