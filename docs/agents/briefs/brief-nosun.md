# Brief: nosun - does any other pass return garbage in a frame without a sun? (PT-133)

Copy: COPYNAME (pristine base: BASENAME; made from the main tree 2026-10-12 ~01:20). Follow `AGENTS.md` in your copy;
read first "The game is the first test", "Code that is switched off still costs frame time", and under "Subagents and
reviews" the rule of 2026-10-12: NO GPU TEST WHILE A GAME OF THE OWNER'S IS OPEN. Report: `docs/agents/nosun.md` IN YOUR
COPY; the plan within 20 minutes, kept current; "State when stopped" at once if a limit shows.

## Why

`docs/PT-REQUESTS.md`, grep "### PT-133" (read it whole), and the worker fogfix's report `docs/agents/fogfix.md`, its
"FOUND" on the white tavern: in a frame with NO SUN (the game's interiors and nights: the plugin sends a sun only when
the game's is valid) the fog cells' Shader Model 6.9 pass returned garbage (1e33 to 1e37) for a sum that is zero on the
branch that skips the sun's code, while dxc's own output for the pass is right. It was cured by the pass's shape, by
test. The rule is not known, so any other pass may hold the same trap, and EVERY scene of the offline suite has a sun.
The owner's game crashed on 2026-10-11 with the GPU hung in the reflections' stage after loading a second scene
(PT-129, not reproduced): a pass that turns a zero into 1e37 could make a path run very long.

## What to do

(a) A way to see such a fault, offline, in minutes: the tracer has two sets of every pass (Shader Model 6.6 and 6.9;
    `tracer.cpp`, grep `TGPT_PASS(g,` and "the 6.6 set") and plain compute twins of the two reordering passes
    (`TGPT_TEST_PLAIN_PASSES`, the hidden "Speed trial" 8). Add a test switch that makes the tracer use the 6.6 set on
    this card (say its name), and a switch of the test (`tests/pathtracing_gpu_test.cpp`: the test's `sunOn`) that runs a
    chosen small mode WITHOUT a sun. Then, for the small modes that exist (grep `_ONLY` in the test; pick the ones that
    cover the big passes: the camera's surfaces, the bounced light, the reflections, the direct light, glass, water, hair,
    skin's walk, the caustics, the fog's old route), the same frames with and without a sun under both sets: the two
    sets must give the same picture within noise (mean and the largest pixel; a 16-frame mean), with and without a sun.
    A difference without a sun that is not there with one is the trap. Also count pixels above a bound no light of the
    scene can reach (the fog's cells held 1e33).
(b) Where it shows: the smallest shape that shows it (for a report to NVIDIA: `docs/agents/nosun.md` gets the pass, the
    branch, the two shapes, dxc's version, the driver's), and the cure by shape with a "KEEP THIS SHAPE" comment as
    fogfix did, checked by the same A/B. Say every pass you touch: a change in a file every pass includes recompiles
    all 57 (half an hour): plan for it, and prefer a cure inside the one pass.
(c) Where nothing shows: say which modes, passes and branches were covered and which were not (so the next reader knows
    what "nothing found" covers), and leave the no-sun switch and the A/B as ONE small mode of the test
    (`TGPT_TEST_NOSUN_ONLY` or the like) that takes a few minutes, for the coordinator's whole run.
(d) Look at PT-129's hang with this in mind (the reflections' stage after a second load; `docs/PT-REQUESTS.md`, grep
    "### PT-129"): which loops of `pt_specular`'s paths could run long on a garbage value (a weight above 1 in a roulette,
    a distance, a count), and whether each has a guard that holds for 1e37 and for a value that is not a number. Read,
    then say; a guard that changes no picture may be added.

Not yours: the fog's passes (worker fogrule), `pt_water_path` and the glass walk's speed (worker campspeed), NRD, mirrors.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: your own small modes only,
  only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, checked right
  before EVERY test start (it exits 1 while a game of the owner's is open: never take that wait out; wait with one
  background script, never poll); NEVER the whole `tgpt_gpu_test` or ctest as a whole. A run hundreds of times slower
  than its neighbours is a hang in the making: stop it at once and write down its switches (a worker's run reset the GPU
  on 2026-10-12 00:20).
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\nosun-...`, configured as the worker fogfix's was (`docs/agents/fogfix.md`: its build section; the main
  tree's prebuilt NRD libraries; `-DNVAPI_ROOT=<main>/NVAPI-R615 -DTGPT_MEGA_GEOMETRY=ON -DTGPT_RAIN_WETNESS=ON`; never
  write the main tree's SDK folders). For any `dotnet` call set `DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true` and
  `MSBuildEnableWorkloadResolver=false` first. RTXDI and NRD sources are never copied or quoted. Grep and read ranges of
  the big files. Say every switch, export, record, FrameConstants row, option or flag bit you add. Run
  `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
