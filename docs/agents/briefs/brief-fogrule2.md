# Brief: fogrule2 - the fog grid's noise in thick fog, then the grid as the rule (PT-118), after fogrule

Copy: `%TEMP%\tg-agents\wt\fogrule` (the worker fogrule's copy: you go on in it; pristine base `base20g`; both were
repaired after Windows swept the temp folder: `docs/AGENT-WORKERS.md` "When Windows sweeps the copies"). Follow
`AGENTS.md` in the copy. You are a FRESH worker: fogrule's reading, its builds and its suspects are in
`docs/agents/fogrule.md` in the copy (read it whole, first) and its brief in `docs/agents/briefs/brief-fogrule.md`.
Report: `docs/agents/fogrule2.md` IN THE COPY; the plan within 20 minutes, kept current; "State when stopped" at once
if a limit shows.

## What is known now (2026-10-12 05:00)

fogrule never got a frame on the GPU (the owner plays Starfield on and off all night, and every offline test waits).
THE GAME HAS NOW ANSWERED for its smallest fix (`%TEMP%\tgb\fogrule-small\TGPathTracing.Native.dll`, SHA 941B5AAD44D2:
main of 01:16 with one change: the per-pixel pass past the cells feeds the mist's cache). Run `20261012-043614`, recipe
`fog-ab` (the world frozen, no denoiser, 16 frames a step); the game's own / the old per-pixel route / the grid:

| scene | luma | flicker, old / grid | boil, old / grid |
|---|---|---|---|
| tavern-room | 24.6 / 17.9 / 18.0 | 0.41 / 0.35 | 0.14 / 0.12 |
| vista | 130.6 / 130.1 / 128.5 (the grid was 121.6) | 0.99 / 0.77 | 0.44 / 0.18 |
| night | 38.8 / 55.1 / 54.3 (was 52.1) | 1.90 / 0.57 | 1.23 / 0.15 |
| fog | 70.2 / 91.7 / 91.3 (was 69.9) | 0.32 / 1.17 | 0.18 / 0.33 |

So fogrule's first suspect was the cause of the light's disagreement (the grid read cells of the mist's cache that
nobody fed), and the two routes now agree to about 1 %. WHAT STANDS BETWEEN THE GRID AND "THE RULE": in the `fog`
scene (thick fog, far views) the grid is 3.6 times as flickery as the old route and its detail measure is 2.59
against 0.58; everywhere else it is the calmer one. (Both routes are 30 to 40 % brighter than the game's own picture
in `fog` and at `night`: HDRP's fog against the traced one, PT-37, NOT this job.)

## What to do

1. WHY the grid is the noisier one in the `fog` scene, and the cure that keeps what the picture converges to. Start
   from the pictures (`artifacts\harness\20261012-043614\fog\2-froxel-none\frame.jpg`, `atlas.jpg` and the old
   route's beside them in the main tree; `tools/harness/flicker.py` cuts out what changes most): is the noise in the
   cells' 20 m, in the per-pixel pass past them (with the smallest fix it both samples AND feeds the cache: a
   feedback?), or at the seam? Then the smallest change that could show the direction, as a TRIAL BUILD FOR THE GAME.
2. fogrule's candidate (`%TEMP%\tgb\fogrule-handover-1`: the cells over the whole way; its own plugin) was never
   run: say from the code whether it would have the `fog` scene's noise too, and whether the smallest fix or the
   candidate is the base to go on from. One of them, not both.
3. When the grid is as calm as the old route or calmer in all four scenes with the same light, with no denoiser AND
   under NRD: the grid becomes the rule in the copy: the hidden `Traced fog: froxel cache` and its trial numbers 1 and
   2, the TEMP DIAG read-back and "leave out" bits (frame flag bits 19, 24 to 29) and the old camera route come out,
   with their settings, recipes, tests and docs (AGENTS.md "Remove the workarounds as we go"). Not before.

## How you get the card: THE GAME IS THE FIRST TEST, AND THE COORDINATOR RUNS IT

Do not wait for an offline GPU test to answer a question the game can answer. Hand the coordinator a trial build
(the DLL's path and SHA-256, the plugin only if it must differ from main's, and the recipe: `fog-ab` in main's
`tests/ingame/scenarios.json` is the short one: the game, the old route, the grid; `fog-froxel-b` has the NRD
steps) and say what to compare. The coordinator queues it (`tools\harness\Queue-PtRun.ps1 -TrialDll`) on
`vista@fog-ab,fog@fog-ab,night@fog-ab,tavern-room@fog-ab` and sends the table back. A first trial build within your
first hour. Compiling is free at any time; a change to a file every pass includes compiles all 57 passes (half an
hour to an hour): keep the first trials to `pt_fog*` and `fog_froxel.inl` where you can, and say when you cannot.
An offline GPU test of your own small mode is welcome when the card is free, never needed first.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in `%TEMP%\tgb\fogrule*`.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start; a watchdog of the
  coordinator's ends every offline test the moment the owner's game opens: start it again later. NEVER the whole
  `tgpt_gpu_test` or ctest. A test's stage needs the runtime files of `<main>\artifacts\Native` (the Agility
  `D3D12Core.dll`): check before every start.
- The main tree has moved on since `base20g` (PT-125's fix in `pt_sway.hlsl`, guards in `pt_medium.hlsli`,
  `pt_common.hlsli`, `pt_psr_capture.hlsli`, C++ in `scene.cpp` / `tracer.cpp`): not yours to merge; the coordinator
  merges three-way. A trial build of yours lacks those: fine for a standing fog comparison.
- No limits that change the picture; a filter or a clamp that hides noise by changing the mean is not a cure (the
  light's numbers above are the yardstick: the grid's luma must stay the old route's within the frames' noise).
- RTXDI and NRD sources are never copied or quoted (NVIDIA's RTX Remix fog is the model fogfix and fogrule read:
  their reports say what was taken from it). Grep and read ranges of the big files. Run
  `python tests/source_checks.py` before a hand-over of sources.
- Final message: five lines at most, pointing at the report.
