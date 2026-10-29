# Brief: the lantern that burns the wall out, and what flickers from frame to frame (PT-73 e, j) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("No fake
lights", "A traced light has the size of what emits it", "Judge the picture without a denoiser first", "Do. not. fake.
stuff.", "test what catches defects and nothing more"). Report: `docs/agents/light-flicker.md`; write it early.

## Why

The first session of today's merged build in the game:
`C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-224654`. The owner watched it ("Lots of
problems in these scenes"; "focus on the things that flicker from frame to frame"). Read
`docs/agents/inspection-20261007-224654.md` and `inspection-20261007-224654-gpt.md` (two independent lists of what is
wrong), and look at the pictures they name. Two groups are yours.

### 1. The lantern (scene `glass-jars`, a tavern room at night lit by one lantern on a table)

`glass-jars/crop-shelf.jpg`, `game-vs-traced.jpg`: the wall behind the lantern is burnt out to near white, the lantern's
inside is one glowing block where the game shows a small flame in a dark cage, the table is flooded; the picture's
mean brightness is 1.6 times the game's (luma 13.8 against 22.2). The owner has said it of candles for days: "some of
the candle lights, such as in the tavern, still bloom too much/are overblown". Since today glass is a real refracting
surface (the lantern's panes) and the tracer's exposure runs on its own light.
Find the cause with numbers, not by turning a knob: what the game's light at the lantern is (its intensity, unit,
colour, range, the flame's size), what the tracer makes of it (`src/TGPathTracing/SceneLights.cs`, `LightRules.cs`:
size, the flame's centre, which glowing surfaces are paired with it and stop glowing), whether its light is counted
twice (the light AND a glowing flame mesh or the lantern's glass AND the candle's effect), what the panes do to it,
and what the exposure does (the log's exposure and gain line: AGENTS.md "Anything that removes light from the image
moves the exposure"). Compare against the photometry: a candle is about 12 lumens, 1 candela; what does the game give
this light, and what luminance should a wall 30 cm away have? Then fix what is wrong. The game's own picture is the
reference for how bright the artists meant the room.

### 2. Flicker (the world is frozen in these scenes, so every change between frames is the renderer's)

`tools/harness/flicker.py` cuts out the spots that change most (its sheets are in the run's folders:
`forest/flicker-2-traced-none.jpg`, `glass-jars/flicker-2-traced-none.jpg`, `vista/flicker-2-traced-none.jpg`).
- **A two-frame pattern.** With no denoiser the grain and brightness of whole surfaces change between frames 0-1 and
  2-3 of four (the tavern's walls, the forest's grass): both reviewers saw it. Something alternates, resets or is
  reused every second frame (ReSTIR's reuse, the radiance cache, a checkerboard, the frame's random seed, the frames the
  harness takes). Find what, and whether it is a defect.
- **The forest's near grass**: 5 to 6 levels of change a frame, blotches, and a blur with no blades; it and the tree's
  foliage keep shimmering with a denoiser on. Check first whether the tracer's wind moves the plants while the world
  is frozen (the harness sets the game's time scale to 0; the plants' pose comes from `PlantWind.cs` and
  `TGPT_SetPlantWind`: which clock?). Then what is left: the cut-out's coverage drawn anew each frame, noisy light.
- **The vista's ground**: single bright pixels, new ones every frame (which estimate makes them, and is its expected
  value right?).
Say for each what it is, fix what is a defect, and say what is honest noise of one sample a pixel.

Judged (AGENTS.md "test what catches defects and nothing more"): build; your own new checks; the whole GPU test ONCE at
the end with its FAIL lines written down; no mutation checks now. In game the coordinator runs `glass-jars@quick` and
`forest@raw` again: say what the lantern's wall, the picture's luma against the game's, and `flicker.py`'s numbers
should become.

## Clean as you go

What your work makes dead is deleted in this job; your report lists every workaround you met, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling; never give up a test for want of a slot. Build into `%TEMP%\tgb\COPYNAME-...`;
  NRD's configure writes into the shared SDK folder, so build with `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Other workers run now on: memory and start-up (`texture_share.*`, pass creation), materials (`pt_materials.hlsli`,
  `MaterialPrograms.cs`). Keep out of those.
- Keep tokens down: grep and read ranges of big files, never whole. Stop when done; ideas go in the report.
- Say if an export or struct changes (the coordinator sets the ABI number; main is 0x00012100). Option bits: none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
