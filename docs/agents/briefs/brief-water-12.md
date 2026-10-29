# Brief: water-12 - the sea bed under the water is a dark slab; its cure brings white blobs and 17 ms (PT-81)

Copy: `%TEMP%\tg-agents\wt\water12` (pristine base: the one the coordinator names when it starts you). Follow
`AGENTS.md` in your copy; read first its rulings on path tracing: "No limits that change the picture", "No shortcuts",
"Do. not. fake. stuff. Real path traced caustics", "Everything on screen is part of the main traced picture", "A
mirror must show the world as the camera sees it", "Judge the picture without a denoiser first", "The game is the
first test". Report: `docs/agents/water-12.md` IN YOUR COPY; the plan within 30 minutes, kept current; "State when
stopped" at once if a limit shows.

## Why

The traced water is not good yet, and the owner has said so three times (`docs/PT-REQUESTS.md`, grep "### PT-81":
read the whole entry, it is the history of six water jobs; its two newest bullets, 2026-10-12, are today's evidence
and today's ruling). The owner: "The water looks off" (10-08), "water and foam is improved, but far from there
still", "our PT water is still far from perfect". AND, 2026-10-12 11:00: "the game's water doesn't look good in the
first place. It's fine that ours look different, so long as it looks amazing". SO THE GAME'S OWN PICTURE IS NOT YOUR
YARDSTICK: not its brightness, not its colour, not its foam. Yours is what physically arrives (worked out by hand,
and the same scene with the lights' straight rays) and a picture that is clean on its own: no light missing, no
rare bright samples, no blobs under a denoiser. Where the numbers below name the game's value, it is there as a
landmark, not as a target.

What today's runs say (main E98E321830C2; the beach `water-day`, the world frozen, no denoiser; the region is the
shallows; the game's own picture there: luma 49.9; pictures in `artifacts\harness\20261012-090731\water-day` and
`-091756\water-day`, each with `overview.jpg`):
1. WITH THE CAUSTICS ROW ON (the default) the bed under shallow water is a dark slab with a hard edge, a crab in a
   dark halo: region 37.3. With the row off (lights' straight rays pass the surface) 42.2, the sand's ripples and
   the crab plain. With the row on, light reaches what lies under water only through the waves (`pt_caustics.hlsl`,
   `caustic_light`; `pt_common.hlsli` ~3035: "light reaches what lies under the water only through its surface"), and
   what lands leaves the slab out.
2. THE HIDDEN `Water light without view` (Advanced; PT-94; the sun's light asked through the waves wherever a path
   meets a point under water: `pt_water_sun.hlsli`) CURES THE SLAB: region 49.6, the game's 49.9. It cannot be the
   rule as it is: UNDER NRD THE SEA IS COVERED WITH WHITE BLOBS (rare, very bright samples: the worker campfix
   measured single samples 20 to 1,552 times the mean in the sea fixture), the flicker with no denoiser is half
   again as high (1.81 for 1.19), and a frame costs 17 ms more (the reflections 70.9 ms for 53 to 56).
3. Everything in a water pixel is made by the reflections' stage (the views "Direct" and "Indirect" are black over
   the water and the bed under it): the water's own path, `pt_water_path.hlsli`.
4. The foam is there (thin lines, fewer than the game's); the body has no green; far out the surface is pale. Those
   come AFTER the bed.

## What to do, in this order. Stop after 1 and 2 if they take the day.

1. FIND OUT, with numbers, WHY THE LANDED LIGHT LEAVES THE SLAB DARK. Candidates the notes name, none proven: landed
   triangles kept only where they land in the camera's view (the pass's header), triangles whose corners land on
   different surfaces left out (the hidden `Caustics land split light` lands them: +9 %), the grid's levels and
   where the fine ones lie, the light's direction lost under water (`Caustics light keeps its direction`: +5 %),
   the sky's light (not the sun's) that the surface lets through and the landed light does not carry at all. The
   sea fixture (`tests/water_sea_gpu.inl`) has a bed, a sun and a camera: build the case there (shallow water over
   sand, the sun, an overcast sky) and split the bed's light by its sources, against the same scene with the row
   off and against what you work out by hand.
2. THE BED'S LIGHT, RIGHT, WITH THE ROW ON, WITHOUT RARE BRIGHT SAMPLES. The mean must be the light that physically
   arrives (the sun through the waves, focused where they focus it; the sky's through the surface; lamps'), and a
   single sample must not be hundreds of times the mean: that is what NRD turns into blobs, and no denoiser setting
   is the cure (AGENTS.md: a defect seen with no denoiser is the tracer's). Which estimator is yours to choose and to
   argue: the landed light made complete, the light asked at the point with its samples drawn where the light is, a
   blend with a proof that nothing is counted twice. NOT yours: a clamp on the sample (it changes the mean: the
   owner's ruling on limits), a blur, a stand-in pattern, the row switched off, a light that is a frame late.
   Cost: the cure may not cost what the switch costs today (17 ms); say what it costs in the fixture, off and on.
3. Then NRD on it (the fixture's frames through the test's NRD path if it has one; else the coordinator's game run).
4. Then the foam's count against the game's and the body's colour (the entry's `water11` bullets: its foam line was
   the right kind; its copy `%TEMP%\tg-agents\wt\water11`).

## How it is measured and checked

- Offline, your own small mode of the sea fixture only: the bed's region's mean with the row off / on / your way,
  against the worked-out value; the largest single sample over the mean (print it: today's switch is the yardstick
  to beat); the stage times. Mutate your estimator once (drop a term) and see your check fail.
- THE GAME, by the coordinator: hand a DLL (path, SHA-256) and any plugin change as soon as the bed is lit and the
  largest sample is down in the fixture; the coordinator runs `water-day@water-noview` (the pattern: frozen with no
  denoiser, frozen under NRD, then the world running for the time) and sends you the numbers and the pictures'
  paths. Behind ONE hidden switch or frame flag bit (bits 24 to 29 are free; 19 is another worker's), default off =
  as before, so that one run shows it off and on. A first hand-over within three hours, or say why not.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree
  (`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration`: read only). Write only in the copy and in
  `%TEMP%\tgb\water12-*`. One build tree; delete your own test pictures once read.
- GPU tests: your own small mode only, only while the game is closed and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0, asked right before EVERY test start; wait with one background
  script, never poll. NEVER the whole `tgpt_gpu_test` or ctest. If a game of the owner's opens while a test of
  yours runs, the coordinator's watchdog ends the test: start it again later.
- Memory: the caustics' buffers were 689 MB at the beach and 1,322 MB in a walk with `water11`'s way, against 215:
  it was taken out of the tree for that. Print what your way holds (the tracer's "PT memory bytes" line, `caustics=`)
  and keep it where it is today.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`), then the native build into
  `%TEMP%\tgb\water12-build`; configure as `docs/AGENT-WORKERS.md` and `docs/agents/briefs/brief-campspeed.md`'s
  build line say. Copy the main tree's compiled headers (`build\native\generated\pathtracing`) into your build
  before your first shader edit. The water's files are included by the two big passes: a change there compiles
  them (10 minutes each); a change in `pt_common.hlsli` compiles all 53.
- Two other workers are in the tree's neighbourhood: `waterblas` (the water's pieces in fewer acceleration
  structures: `scene_water.cpp`, `scene.cpp`, the water's hit records) and `caustgrid` (`pt_caustics.hlsl`'s cost).
  Do not restructure what they hold; where you must touch `pt_caustics.hlsl`, keep the change small and say so in
  the report's first lines so that the merges can be planned.
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files, never whole. The Bash
  tool eats backslashes: scripts and text with backslashes go through the Write tool. Say every switch or log line
  you add. Run `python tests/source_checks.py` before you hand over.
- Final message: five lines at most, pointing at the report.
