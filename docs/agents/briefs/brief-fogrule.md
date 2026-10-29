# Brief: fogrule - the fog grid and the old route disagree on the light outdoors; settle it, then make the grid the rule (PT-118, PT-132)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-12 ~01:20, with the workers fogfix and psr-2
merged). Follow `AGENTS.md` in your copy; read first its rulings "No limits that change the picture" and "No shortcuts",
"Follow NVIDIA's and AMD's guides to the letter", "Remove the workarounds as we go", "The game is the first test", "Code
that is switched off still costs frame time", and under "Subagents and reviews" the rule of 2026-10-12: NO GPU TEST WHILE
A GAME OF THE OWNER'S IS OPEN. Report: `docs/agents/fogrule.md` IN YOUR COPY; the plan within 20 minutes, kept current;
"State when stopped" at once if a limit shows. You are a fresh agent: the last worker's report is `docs/agents/fogfix.md`
(read its "State", "FOUND" and the comparing check's section; not all of it).

## Why

The owner chose the fog grid "as RTX Remix builds it" because the traced fog is noisy (2026-10-12: "btw, the fog right
now - is that the RTX Remix fog? Because it's still super noisy": no, the game still runs the old per-pixel route; the
grid is the hidden `Traced fog: froxel cache`). The grid's white tavern is cured (PT-133) and in the tavern it has the
old route's light and half its flicker and boil under NRD (`docs/PT-REQUESTS.md`, grep "### PT-118", the first lines).
OUTDOORS THE TWO ROUTES DISAGREE ON THE LIGHT (run `20261012-010243` in the main tree's `artifacts\harness`, the recipe
`fog-froxel-b`, the world frozen, main's build 3199B2F4BBF6; luma of the whole frame (of the scene's region)):

| scene | the game's own | old route, no denoiser | grid (trial 0), no denoiser | old, NRD | grid, NRD |
|---|---|---|---|---|---|
| `vista` (open world, daylight) | 131.1 (131.7) | 130.6 (130.6) | 121.6 (116.3) | 130.0 (129.8) | 120.8 (114.8) |
| `fog` (the game's fog in daylight) | 70.5 (77.4) | 91.8 (103.6) | 69.9 (74.4) | 91.9 (103.7) | 70.1 (74.5) |
| `night` (a torch in fog at night) | 38.4 (40.7) | 53.1 (66.4) | 52.1 (65.6) | 53.3 (66.7) | 52.3 (66.0) |

Flicker / boil with no denoiser, old against grid: vista 1.00 / 0.44 against 0.82 / 0.22; fog 0.33 / 0.18 against 0.70 /
0.23 (THE GRID IS NOISIER THERE); night 1.86 / 1.21 against 0.45 / 0.12. Each step's `frame.jpg`, `atlas.jpg` and the
scene's `overview.jpg` are in the run's folder; the tracer's log has the "Fog grid numbers (TEMP DIAG)" lines of the
grid's steps. Both routes are meant to converge to the same fog. They do not: 7 % apart in the vista, 24 % in the fog
scene (where the old route is 30 % over the game's own picture and the grid on it; in the vista the old route is on the
game's and the grid 7 % under). Known of the old route: it loses the fog light of lamps with a range and of glowing
surfaces on long rays (PT-132).

## What to do

(a) WHICH ROUTE IS RIGHT, AND WHERE THE OTHER GOES WRONG. Measure offline first (your own small mode,
    `TGPT_TEST_FOG_FROXEL_ONLY`, minutes a run): cases the comparing check does not have and the two scenes do: fog over
    hundreds of metres in daylight (the sun and the sky; the game's Max Fog Distance far beyond the cells' 20 m; the
    height fog's base and fall-off as the game sends them: read a harness run's log for the constants, "Fog grid
    numbers" prints them), sky pixels and far surfaces, the mist's cache on and off, the tail past the last cell. A
    closed form is the judge where one exists (a uniform fog lit by a uniform sky and an unshadowed sun: the light
    scattered once is known exactly), not either route. Say which route is wrong in which scene and why, with numbers.
(b) FIX IT (in whichever route; the old route's functions are in files every pass includes: `pt_medium.hlsli`: a change
    there recompiles all 57 passes, about half an hour: plan for it), so that the two agree within the check's tolerance
    in those cases too, and the cases are in the check. And why the grid is the noisier one in `fog` (0.70 against 0.33).
(c) THEN THE GRID IS THE RULE: no hidden switch, no frame flag (bit 19 free), the old camera route out (the camera's
    pass of `pt_fog.hlsl` and what only it needs; what reflections and bounces use of the medium stays), the trials 1
    and 2 out (trial 1's walk costs 10.9 ms against 1.15 and trial 2 showed the same picture as trial 0 in every scene:
    say if your measurements disagree), every TEMP DIAG line out (the read-back, `Traced fog: froxel cache leave out`,
    the flag bits 24 to 29, the test's switches that only they need), recipes, source checks and docs with them
    (`docs/PROJECT-CONTEXT.md` 9.12's flag bits; PT-118, PT-132, PT-133 in `docs/PT-REQUESTS.md`; `CHANGELOG.md`).
    PT-132's loss: the tail past the last cell still uses `fog_lamps_along`; `fog_lamps_residual` (fogfix) shows the
    exact way (the lists of every light-grid cell the ray crosses): close it there if (b) has you in that code anyway.
(d) Hand the coordinator ONE build after (b) for `vista`, `fog`, `night` and `tavern-room` (the recipe `fog-froxel-b`
    while the switch still exists), and one after (c). The first within three hours, or say why not.

Not yours: the camp's speed (worker campspeed: `pt_water_path`, the glass walk, measuring switches), NRD, mirrors.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small mode only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, checked right
  before EVERY test start (it exits 1 while a game of the owner's is open: never take that wait out; wait with one
  background script, never poll); NEVER the whole `tgpt_gpu_test` or ctest as a whole. A run that is hundreds of times
  slower than its neighbours is a hang in the making: stop it at once and write down its switches (the last worker's
  `m-many` reset the GPU).
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\fogrule-...`, configured as the last worker's was (`docs/agents/fogfix.md`: its build section; the main
  tree's prebuilt NRD libraries; never write the main tree's SDK folders). For any `dotnet` call set
  `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true` and `MSBuildEnableWorkloadResolver=false` first. RTXDI and NRD sources
  are never copied or quoted. Grep and read ranges of the big files. Say every export, record, FrameConstants row,
  option or flag bit you add or free. Run `python tests/source_checks.py` before you hand over.
- When a build for the game is ready, END YOUR TURN with the hand-over (paths, SHA-256, what to run); the coordinator
  runs the game and resumes you with the numbers.
- Final message: five lines at most, pointing at the report.
