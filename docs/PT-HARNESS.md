# The in-game harness for TG Path Tracing

Asked for by the owner on 2026-10-06: a way for an agent to test path tracing in the real game by itself. It loads
saves the owner made at known places, takes frames of the game's own picture and of the traced one, and writes
pictures and numbers an agent can read. State on 2026-10-06: built, its analysis checked offline, and run in the
real game: one scenario from load to report, which found its first defect (see "State and limits").

## One command

For startup/manual checks on the normal or Mono version, `Run-PtHarness.ps1 -LaunchOnly -GameDir <game>`
uses the same GPU counters and waits for five quiet seconds, then starts through Steam and leaves the game
open. It installs no runner, changes no configuration and loads no save. This is not a scripted capture run
or a rendering-quality result. The automatic PT runner below remains Mono-only.

TG Neural Rendering uses these same saved views through `tools/harness/neural_plan.py`. `--compare` adds A/B/C,
placement and strong matched motion; [current evidence](DLSS5-TUNING.md). NR runs use the normal five-second
GPU-idle gate. The owner stopped run140059 on2026-10-08 because competing PT tests made the GPU too busy:
do not reuse the earlier `-AllowGpuContention` permission. FoA and Starfield must both be closed. See
[NR implementation and tuning](DLSS5-IMPLEMENTATION.md#scene-tuning-after-pt-is-ready). Its steps require actual NR
evaluations, compare Off/neutral images, and restore its configuration afterward. `-KeepHud` preserves HUD/text in
captures. The owner has not authorized deployment during the ongoing parallel PT work.

For the NR investigation the owner subsequently authorized NR builds/deployments and game runs with both
games closed. Ordinary PT runs still use the owner's PT-only rules: FG and NR start Off, including NR's
remembered choice; the original configuration is restored afterward. Explicit NR recipes enable NR per take.
`-AllowReShade` is only for an explicit NR compatibility recipe, never an ordinary PT baseline. It does not
enable, disable or alter ReShade. After null screenshot failures with the owner's ReShade/RenoDX setup,
capture errors now include Unity's native warning/error messages without changing BepInEx logging settings.
The chair investigation uses `neural_chairs.py` (Quality only); `--report <run>` validates the camera routes
and exact neutral scene grids before writing a screen/model-stage comparison sheet. See NR-24.
NR diagnostic v4 also records actual pixel extrema and HDR fallback fractions. `neuralToneMap` is an
internal comparison only: omit/-1 preserves the current route,0 selects peak Reinhard and1 per-channel
Reinhard; both explicit values request one initial history reset. It requires NR and stage capture. The
mapper returns to normal when diagnostics stop. No player setting is changed by this comparison.

NR input audit (R11): `neuralStages` saves exact DLSS colour, original depth/motion and the converted
NR depth/motion for three adjacent frame pairs (indices8/9,16/17,24/25). `guide-*.bin` stores a48-byte
NRG1 header and original pixel bits, including Unity's typeless float motion and depth/stencil formats.
`neural_guides.py <run>` compares every copied guide pixel, independently tracks image features and
predicts motion from camera poses, FOV and depth. This proof is for static geometry with SR, not PT/RR.
`walkSpeed` optionally sets metres per real second. Such takes cannot use frame-index colour A/B:
camera positions differ with rendering time. The measured timestamps and route are validated instead.
Diagnostic readbacks invalidate frame times as performance evidence. Any capture error now stops the take.
`neuralPostToneMap` is a harness-only colour comparison after HDRP UberPass, before final output/UI.
It requires Quality, stage capture and `disableLensEffects` in every compared take. Depth/motion still
come from the same-frame DLSS invocation. The colour uses sRGB encoding without our HDR compression
or restoration. This is not a player placement or a claim of HDR support.

```
.\tools\harness\Run-PtHarness.ps1                       every scenario
.\tools\harness\Run-PtHarness.ps1 -Scenario npc-day,fog  only these
.\tools\harness\Run-PtHarness.ps1 -Recipe why-bright -Scenario vista   another list of steps
.\tools\harness\Run-PtHarness.ps1 -Calibrate            find the view each save was made with (after new saves)
.\tools\harness\Run-PtHarness.ps1 -Inventory            only list the saves, with their pictures
.\tools\harness\Run-PtHarness.ps1 -GpuCheck             only say who uses the GPU now
.\tools\harness\Deploy-PtBuild.ps1 [-Native]            the tree's path tracing build into the game, for the next run
python tools\harness\compare.py <older run> <newer run>  two runs side by side: what a change did
.\tools\harness\Clear-PtHarnessRuns.ps1 [-Apply]        free disk space: older runs lose their raw frames (below)
```

Results land in `artifacts/harness/<date-time>/`. Read `report.md` first, then each scenario's `overview.jpg` and
`side-by-side.jpg`. A scenario takes a little over two minutes, the whole list about fifty. `-Recipe smoke` is a
one-minute check of one scenario, `-Recipe frozen` leaves the running-world steps out. `-KeepFrames` keeps the raw
frames (33 MB each at 4K); otherwise they are deleted once measured (the full-size `full.jpg` of the game's first
take and of each traced one stays). Every run notes which build it tested (`build.json`).

**Disk space.** Kept raw frames fill the disk: `artifacts/harness` held 307 GB after three days (2026-10-08), 92 % of
it `.raw` frames. `.\tools\harness\Clear-PtHarnessRuns.ps1` says what it would free and deletes nothing; with `-Apply`
it does it. A run's age counts from the last time anything in it was written.
- The newest 10 runs stay as they are (`-KeepWhole`).
- Older runs lose their raw frames (`.raw`, `.hdrraw`, `.bin`: the neural rendering captures' buffers too). Pictures,
  reports, JSON and logs stay, so what the docs point at still opens. `flicker.py`, `edges_shift.py`, `mean_colour.py`
  and the `neural_*.py` tools read raw frames: they no longer run on such a run.
- Runs older than 14 days are deleted whole (`-DeleteAfterDays`; 0: never), unless a doc names them (`*.md` in the
  repo's root and under `docs/`): those only lose their raw frames.
- It clears `artifacts/Packages` too (62 GB on 2026-10-08): at every run `Package.ps1` leaves a folder and a zip per
  mod there and reads neither again. Of each mod, version and game version the newest stays (`-KeepPackages`; 0:
  none). The zips for Nexus are the copies in `artifacts/Nexus`, which it never touches.
- It clears the workers' old builds too (`%TEMP%\tgb`: build trees, install folders, test pictures and logs; 145 GB
  in 1,523 folders on 2026-10-12, with 8 GB left on the disk; the owner: "delete all the old ones, and make sure
  our Clear-PtHarnessRuns.ps1 tool also does that"). A folder or loose file there goes when nothing in it was
  written for 2 days (`-WorkerDays`; 0: never touched; `-WorkerRoot` names another folder). A link found inside one
  is unlinked, never followed. The tree copies (`%TEMP%\tg-agents\wt`) are sources and are never touched.
- `-Keep 'nr-*','20261008-130029'` leaves the named runs, packages and workers' folders alone; `-List` shows a line
  for every run, every kind of package and the 60 largest workers' folders.
- Never touched: `builds`, `_config-backup`, the `RUNNING` and `STOP` files, a run or package written to in the last
  15 minutes. While a harness run is going, `-Apply` deletes nothing and exits 3.
Its checks, on made-up folders in `%TEMP%` (never the real ones: the test names a workers' folder of its own in
every call): `powershell -File tests\harness_cleanup_test.ps1`.

For the effects/fog census, run `-Scenario candle,campfire,fog,night,tavern-room -Recipe effects`.
It reads loaded scenes with PT Off, then On to observe actual light selection, then ends Off. No image captures.
Each inventory step writes `effects.json` and a family table `effects.md`; the scenario also has `effects-live.json`.
Three probes a second apart check particle state and HDRP shader clocks before/after freezing. The launcher writes
`effects-report.md` and `effects-summary.json`; repeat with `python tools/harness/effects.py <run>`.
Allow an estimated 3-8 minutes for five scenes plus GPU wait; no measured timing yet. Public API limits and the
acceptance checklist: `docs/agents/effects-inventory.md`. This census does not establish that particles/fog are traced.

## The rule before every start: the GPU must be free

The owner, 2026-10-06: "you spawned the game while Starfield was running. Whenever you want to test, check if another
process is using the GPU, and if it is, monitor it, and launch once it settles", and "Not just monitor Starfield, but
any process that could be using the GPU. Make it a general thing."

- The script reads Windows' own GPU counters for every process. A process counts as using the GPU from 10 % of its
  3D or compute engines (a game, a render, an AI job), or when it holds 2 GB of video memory (a game left paused).
- The owner's stream stays on (2026-10-12: "I would prefer if I can stay on the SUnshine/moonlight stream"), and
  speed is measured with it. A run during which Sunshine encoded says so in its report ("THE OWNER'S STREAM WAS
  ON"): its speed is the speed with a stream (the forest's two reordering passes read 2.5 and 3 ms slower than on a
  quiet card, the beach's reflections 13 ms) and is held against runs with a stream alone; a choice between two
  builds or settings is an A/B/A within one run. Sunshine on the 3D engines is no longer a "problem" of a run.
- The video encoder and decoder do not hold a start back (the owner's choice, 2026-10-06: "Ignore the encoder"):
  Sunshine, which streams the owner's screen, encodes whenever the screen changes. The report still names them, as
  they cost a little frame rate. The rule goes by engine type, not by program name.
- It starts the game only after the GPU has been free for 5 seconds in a row (`-QuietSeconds`; the owner: "90 quiet
  seconds are too long. Just 5 quiet seconds"). Until then it
  waits and says once a minute who uses the GPU. It gives up after 4 hours (`-GpuWaitMinutes`), starting nothing.
- A reading that fails is not a free GPU, and "nobody uses it" is not a failed reading. The first version mixed the
  two up (a PowerShell function that returns an empty list hands back nothing): it started the game once without the
  quiet time, then waited forever on a free GPU. Fixed the same day.
- Nothing is written into the game folder while it waits, so a game the owner starts meanwhile plays as normal.
- A file named `STOP` in `artifacts/harness/` ends a wait or a run.
- While a run lasts it notes who else used the GPU (`gpu.json`). If anyone did, the report says the frame rates
  are not to be trusted.
- Never start the game by hand (the exe or the Steam URL) for a test.

## Autosaves and exit saves (what the owner asked first)

Read from the game's code (TG.Main.dll of the 2026-10-06 update, decompiled):

- **There is no save on exit.** Nothing saves when the game quits or returns to the title screen.
- **Automatic saves** come from a timer (1, 3, 5 or 10 minutes), from walking through a door or portal, and from
  resting. All three obey the game's own setting: Settings, Gameplay, **Auto Save: Off**. A quest can still force
  one at a checkpoint.
- Automatic saves and quick saves each keep **3 files per character** and overwrite the oldest. They never touch a
  manual save (`Slot_N`). What does change is the **order of the load menu** (newest first), which is why a
  harness must never pick a save by its place in the list.
- **The harness picks saves by name, never by position**, so the list's order does not matter to it.
- **During a run nothing can be saved at all**: the runner adds the game's own save blocker (every automatic save
  asks for it first) and stops `LoadSave.Save` itself. The script lists the save files before and after a run
  (name, size, time) and the report says "untouched" or names what changed. Two runs so far: 103 files, untouched.
- The save menu takes at most **100 saves**. The owner has 84.

## The saves

Each scenario has a save **named in the game's own save menu**: Menu, Save, new save, type the name. The harness
finds it by that name (capitals do not matter; of two with one name, the newest). No ID has to be looked up.

A save keeps where the hero stands and which way they face, **not the look up or down**: every load comes back
looking level (the owner noticed: "I had my camera pointed down ... but apparently the game ignores that"). The
harness finds the tilt again: `-Calibrate` loads each save, sweeps the camera up and down, and picks the frame that
matches the picture the game keeps with the save (the one in the load menu). The result is in
`tests/ingame/camera.json`, stamped with the save's ID and time, and used for that very save only: after making or
replacing a save, run `-Calibrate` again. Only a clear match moves the camera. So: stand still, look at the thing
however you like, save.

People walk. A guard framed up close was gone two seconds after the load, so the world is frozen half a second
after a load ends (`settleAfterLoad`, in game seconds, adds to that; 0 for a scene with someone who walks).

`tests/ingame/scenarios.json` is the list: for each, the save's name, where to stand (`where`), what it tests, and
the part of the screen it is about (`roi`). Until a save exists, an older save of the owner's stands in where one
fits (`saveFallback`); the report lists the saves still to make.

| Save name | Where to stand | Stands in until then |
|---|---|---|
| `PT npc-day` | Outdoors in daylight, one step from a person who stays put and has hair you can see | Slot_50 (a bearded man in a hat) |
| `PT npc-candle` | Ron's Tavern, one step from a person by candle light, hair visible | Slot_82 (Sagremor Castle, a man by a candle) |
| `PT tavern-room` | Ron's Tavern from a corner: the whole room, guests, candles, haze | QuickSave_3 |
| `PT boil-cage` | The cage with the thin bars, where it boiled most | made 2026-10-06 (the Crypt) |
| `PT glass-jars` | Ron's Tavern, the shelf with jars and bottles, one or two steps away | AutoSave_0 |
| `PT candle-boil` (scenario `candle`) | A lit candle up close: the smoke and gas around its flame | made 2026-10-06 |
| `PT refract-jar` | A glass jar up close, for what the glass bends | made 2026-10-06 (the Crypt) |
| `PT black-spots` | A floor where black spots flicker traced, looking down at it | none |
| `PT hair-eyebrows` | Ron in his tavern, one step away, his face filling the middle: hair, eyebrows, lashes, beard | none |
| `PT caustics-glass` | A glass bottle on a table with a candle or the sun behind it, looking down at its shadow | made 2026-10-06 (a harbour terrace; 42 degrees down) |
| `PT water-day` | The edge of clear shallow water in sunlight, looking along the shore | made 2026-10-06 (a beach, overcast) |
| `PT water-torch` | A pool in a cave or dungeon with a torch beside it | made 2026-10-06 (a sea cave in fog, no torch: water reflections) |
| `PT door-strap` | The door whose metal strap showed the saw-tooth pattern, one step away | made 2026-10-06 (Ron's Tavern) |
| `PT metal` | Plate armour or polished metal up close in good light | made 2026-10-06 (a guard with a helmet in a market; he walks) |
| `PT dark-room` | A very dark room with a single candle or torch (a crypt) | AutoSave_4 (Crypt) |
| `PT doorway` | Inside a dark building or cave, looking out into daylight | QuickSave_1 |
| `PT forest` | Among trees and tall grass in sunlight | Slot_37 |
| `PT vista` | A high open view in daylight with as much in it as possible | Slot_77 (Cuanacht) |
| `PT foggy-backdrop` (scenario `night`) | The bridge at night, the man in the wicker mask by a torch, far hills through the fog | made 2026-10-06; before it Slot_45 |
| `PT fog` | Thick fog outdoors, with a light in it if there is one | Slot_47 (Broc Meala) |
| `PT mineral-cave` (scenario `glow`) | The cave with blue minerals, water on its floor and small lights reflected in it | made 2026-10-06; before it Slot_74 |
| `PT stairwell` | A castle stairwell or closed stone corridor, no window in view | Slot_80 (Sagremor Castle) |
| `PT campfire` | A campfire with a tent or cloth beside it, dusk or night | Slot_26 |

Still to make: `PT black-spots`, `PT hair-eyebrows`. The owner names saves their own way: a scenario's `save` in
`tests/ingame/scenarios.json` is changed to the name they chose, its key stays. Stand-ins on `AutoSave_` and `QuickSave_` slots can be overwritten the next time the owner
plays: `PT tavern-room`, `PT glass-jars`, `PT dark-room`, `PT doorway` come next.

Not covered by a save: a dialogue (the game does not save during one), so TG Conversation Lighting Overhaul's
light is still for the owner's eye.

## One run at a time

`Run-PtHarness.ps1` holds `artifacts/harness/RUNNING` (its process id) while it runs and refuses to start while
another run's process holds it; a lock whose process is gone is taken over. Two runs at once crossed their backups of
the player's configs (2026-10-06). A script that queues runs waits for the previous one's own end line, never for a
word such as "stopped" that every run prints. A queue that tests a build before its run (the tracer's GPU test, then
the install, then the run) holds `RUNNING` itself from the end of its build until its run starts: agents start no GPU
test while a live process holds it, so the queue gets the next gap. Without that, four agents' GPU tests kept a queue
waiting 25 minutes for 20 quiet seconds (2026-10-07).

**A run that has to wait is queued with `tools\harness\Queue-PtRun.ps1`** (2026-10-12), started in the background:
`-Scenario forest@camp-walks` runs the build that is in the game folder (it refuses when that is not `build\native`'s);
`-TrialDll <a worker's DLL>` runs a trial or measuring build: put into the game folder only once the card is free, and
main's build put back when the run has ended (a measuring build is never left in the game, and a wait of unknown
length while the owner's game is open is "left"). It takes `RUNNING` before it waits, and since that day
`Run-PtHarness.ps1 -GpuCheck` exits 1 while a live process holds `RUNNING`: workers start no new offline test, whatever
their own scripts know of the file. Its last lines are the report's problems and steps and, from the tracer's log's
LAST start on (the log runs on across starts: an old "The GPU stopped" line was once read as the run's own), every
GPU stop. `-Measure` as the harness's own. A wait ended by the `STOP` file gives the lock back.

## A scene depends on what was loaded before it (2026-10-12, PT-147)

The game keeps the map loaded from one save to the next in a session, and with it the state of its lights. The
town's save (`metal`) loaded FIRST has a set of its lamps dimmed to nothing ("PointLight_Ambience_02" by the food
stalls: 0.125); loaded AFTER the beach's save (`water-day`, the same map) the same lamps keep their full strength
(524,288; 90 of 1,075 lights differ by more than 1.5 times). The game's own picture differs with it, and the traced
one much more: a wide warm glow in the mist and on the ground, or nearly none. 19 of 19 runs with the beach first
have the glow, 47 of 47 others do not, whatever the build.

- A run takes its scenes in the order of `scenarios.json`, not in the order typed: `water-day` comes before `metal`.
- Hold a scene's picture, noise and speed only against runs with the same scenes before it. To compare two builds
  in one scene, run that scene alone, or the same list both times.
- The audit of a `1-game` step (`"audit": true`: `audit-a.txt`) lists every light with its strength: the way to tell
  a changed light from a changed tracer. `python tools/harness/compare_lights.py <audit of A> <audit of B> --at
  <camera x,y,z>` lays two audits side by side (the same light by name and place: ids change with every start).

A moving step's frames are judged without the game's motion blur: give the step `"disableMotionBlur": true` (and
read `motionBlurActive: false` in its frames). Without it the game's blur is on and smears every frame.
`python tools/harness/motion_clips.py <run> <out folder> <scene>:<x>,<y>,<w>,<h>` makes the owner's pictures from a
run that kept its raw frames (`Queue-PtRun.ps1 -KeepFrames`): two steps side by side as a moving picture at the
game's own pixels, the frames lined up by where the camera looks (the recipe `nrd-motion`'s RELAX and REBLUR by
default; `--a`, `--b` name other steps). It refuses a run whose frames do not say the blur was off.

Offline GPU tests (`tgpt_gpu_test`) run up to 4 side by side: `-GpuCheck` exits 0 while fewer than `-TestsAtOnce` (4)
of them run, also while the owner plays another game (the owner, 2026-10-07: "Yes, up to 4"; `-TestsYield` waits for
other programs too). One test keeps one
CPU thread busy and the GPU at 13 to 36 % (188 s of CPU in 202 s, 2026-10-07), so one at a time left three quarters of
the GPU idle while about 20 runs stood in line. `-TestsAtOnce 1` waits for a GPU with no test on it, for a run whose
GPU timings count. A run of the game still waits until no test runs. And while the game itself runs (a harness run of
this or another session, or the owner playing it) `-GpuCheck` exits 1: no offline test starts beside it (since
2026-10-07 18:05; until then only the agents' own "game closed" rule held them back).

Workers through Codex (GPT-6-astra, `<scratch>/codex-worker.ps1`) use the same check from inside their sandbox.

## What a run does

1. Waits for a free GPU, builds the runner plugin, copies it to `BepInEx/plugins/TGPathTracingHarness/`.
2. Keeps the owner's three TG configs aside, sets Path Tracing to Off and Frame Generation to Off for the run, and
   writes the request. All three configs are put back when the game has closed, also after a run that was cut
   short (the next run does it first).
3. Starts the game through Steam. The runner leaves the game's "press any button" screen by itself. The game is
   silent for the whole run (the owner, 2026-10-07: "keep the game volume turned off when we test"): its sound is
   FMOD's, so `Sound.cs` mutes FMOD's master through the game's own integration (`RuntimeManager.MuteAllEvents`) from
   FMOD's first frame on, and Unity's volume too; Unity's volume alone, the first try, muted nothing. `progress.log`
   says "The game's sound (FMOD) is muted for the run."
4. For each scenario: loads the save (the same call as the title screen's Continue), waits for the loading screen
   to go, lets the world run for 8 seconds of game time, hides the HUD, turns film grain and chromatic aberration
   off (both would only make two pictures harder to compare; the latter at the owner's word), and **freezes the
   world** (time scale 0). Every step then renders the same instant: same poses, same flames, same clouds.
   ON SCREEN (2026-10-09; the owner, watching runs: "I'm never sure when I can trust the visuals"): while a step
   settles, waits for a save to load or for the tracer's shader passes, the runner shows one line at the bottom
   left ("TG Path Tracing test run - <scene>, step <name>: settling, the picture does not count yet"). The line
   is GONE while the step's pictures are taken (a grabbed frame holds what IMGUI drew): no line, the picture
   counts. Real keyboard, mouse and gamepad input is kept from the game for the whole run (`RealInput.cs`). When
   a new build's passes are still being made, the first traced step also saves the game's own screen once, 5 s
   into the wait, as `waiting-for-passes.png` in the step's folder (what a player sees then, notices included).
   Never take a picture of the desktop instead: the owner works at this PC while a run goes.
   WHEN A TRACED STEP'S PICTURES ARE TAKEN (PT-98, `docs/agents/arrival-1.md`; built 2026-10-09, to see in a run):
   when the plugin says the traced world is WHOLE (`Arrival.Whole`: no mesh to register, build or place, no listed
   instance waiting for its mesh, no character being read, no texture kept back, loading or to bake), held for
   1.5 s, then the step's own `settle` and `settleFrames`. Until 2026-10-09 the runner waited for the native queue
   of meshes alone, which is empty between two waves of meshes and knows nothing of instances or textures: run
   `20261009-150909` took a town's pictures with nothing traced on 62 % of the screen. The line on screen says
   what is still to come ("... settling, the picture does not count yet (building the traced world: 412 meshes and
   96 textures to go)"). A step that reaches its `waitTraced` (45 s unless the step says otherwise) before the
   world is whole takes its pictures anyway and says so in `progress.log` in capitals ("THE TRACED WORLD WAS NOT
   WHOLE"); its result holds `worldWhole`, `toCome`, `waitedForWorld` and `arrivalSeconds`. With an installed
   build from before (no `Arrival`), the old wait serves and the note says so. The game's own log has the
   timeline: the lines that start with `Arrival` in `LogOutput.log`, one a second until `Arrival whole after N s`.
   The recipe `arrival` is for that number alone, both ways in one run of one scene: `2-as-before` (the hidden
   `Advanced / Arrival as before` 1: the plugin's rules of before 2026-10-09 16:00), `4-by-the-clock` (0, the
   default), `6-as-before-again` (what the order alone is worth: the game has its textures loaded by then),
   `8-on-again` (switched on with everything registered already); 4 frames at each. A change of that hidden setting
   starts the traced scene over, so each of the three arrives from nothing. The native side's rules have no switch:
   they are by the clock in all of them.
5. The default recipe is `quick` (since 2026-10-06 evening; the owner: "Your tests when you play the game are really
   slow ... Can we speed these up?"): `1-game`, `2-traced-rr`, `4-traced-nrd`, `5-game-after` (Off, with the state
   check), 4 frames each, about 45 s a scene with its load, against 150 s for `ab` (measured on run
   `20261006-185451`: of `ab`'s 150 s a scene, 26 s went to `1b-game-again`, 22 s to the toggles, 47 s to the three
   live steps, 15 s to the load). `quick` does not judge whether the game's picture is back after Off: without
   `ab`'s second take of the game's own picture right before tracing, the game's own drift over the run counted as a
   fault in every scene (run `20261006-230457`). Use `ab` (`-Recipe ab`) for a build's full check: boiling with the world running,
   the frame rate, the picture after toggling, the game's own drift. Its steps, 8 consecutive frames each, as the
   screen shows them at full size:
   - `1-game`: path tracing never on since the load. The reference. `1b-game-again`: the same a few seconds later:
     what the game's picture changes by itself (a lantern flickers even in a frozen world).
   - `2-traced-rr`: On with Ray Reconstruction.
   - `3-game-after-rr`: Off again.
   - `4-traced-nrd`: On with NRD.
   - `5-game-after-toggles`: On and Off four more times, then Off.
   - Then the world runs again, and stays running: `6-live-game`, `7-live-traced-rr`, `8-live-traced-nrd`, each with
     8 frames and its frame rate. A frozen world is so still that a denoiser has it easy (0.05 levels of change with
     Ray Reconstruction in the first run): what boils in play shows here, with the camera's idle sway and the flames.
6. Quits the game. Then `tools/harness/analyze.py` writes the pictures and `report.md`.

The picture is judged without a denoiser first (the owner, 2026-10-07: "test less with Ray Reconstruction and more
without denoising so we better identify the problem areas ... serve it a more truthful base frame"). Since then `quick`
is `1-game`, `2-traced-none` (16 undenoised frames: their mean in the frozen world is what the picture converges to, their
change from frame to frame is the noise a denoiser is handed), `3-traced-nrd` (16 frames), `4-game-after`. ReSTIR's
reuse of samples stays on for it (the owner: "I only mean for Ray Reconstruction, because it does such a good job of
hiding stuff"). The `raw` recipe, only for tracking a defect down further, adds `3-traced-raw` with both reuses off (the
hidden keys "Spatial reuse" and "Temporal reuse" in the Advanced section), 32 frames, then `4-traced-nrd`.

Ray Reconstruction is tested last (the owner, 2026-10-07 23:10: "we need to make this look pretty good before testing
with ray reconstruction. The NRD denoiser should look relatively noise-free, and the denoiser-less presentation should
reveal the problem areas, like spots with a lot of shadow flickering, the weird rectangle gas spots over candle lights
and stuff"). Until the owner says it is time, no default recipe takes it; `with-rr` is `quick` with a Ray
Reconstruction step. How to read a run now:
- `2-traced-none` shows WHERE the problems are. `python tools/harness/flicker.py <run>/<scene>/2-traced-none --game
  <run>/<scene>/1-game` cuts out the six spots that change most between consecutive frames (one row a spot: the game's
  picture there, four consecutive frames, the change) and writes the whole screen's change as a map. A spot that
  flickers there is the tracer's to fix.
- `3-traced-nrd` is the picture to judge: it should be nearly free of noise. Run `flicker.py` on it too: what still
  changes with NRD on is a defect, in the tracer's input or in how NRD is fed.
- Boiling (the owner, 2026-10-07: "we need to catch boiling with NRD and prevent that too"): `flicker.py <step> --game
  <game step> --boil` measures how far the mean of each 16-pixel block wanders over the step's frames (fine grain
  averages out of a block; a blotch that comes and goes does not), prints it beside the game's own figure, and cuts
  out the worst spots with frames spread over the sequence. First numbers (2026-10-07, the candle scene with NRD):
  0.096 levels on average against the game's 0.037; the worst spots 0.5 to 0.75.
- Stair-step edges and thin things as rows of dots (PT-84): `python tools/harness/edges_shift.py <run> <scene>`
  measures whether the traced picture carries the sub-pixel shift the upscaler is told. Two numbers a step: how much
  the render grid shows in the picture (the game's own in `forest`: x1.04; traced before PT-84: x1.27 to x1.50), and
  how far the told shift drags the picture about (the game's own: nothing; traced before PT-84: 0.05 to 0.15 output
  pixels a render pixel of shift). After PT-84 both should read as the game's. The log must also hold the line "The
  camera's rays carry HDRP's sub-pixel shift"; a line "came without the frame's sub-pixel shift" means the plugin read
  HDRP's matrices too late again (the tracer then puts the shift in itself).
- If video memory ran out during the session (the log's "Video memory is nearly exhausted", "kept back near this
  card's video memory budget"), the pictures are not to be trusted: textures were stand-in tiles and denoisers may have
  been swapped. Check that line before reading any picture.
- The yellow region of a scene (its flicker and boil numbers) must sit on what the scene is about: the hair scenes'
  and `glass-jars`' did not (2026-10-07: on the ceiling and on a chair). `flicker.py` finds its own spots.

A check of one defect runs the one scene that shows it, not every scene that might (the owner, 2026-10-07, after closing
the game in the second of three hair scenes: "There's no point in you wasting 10 minutes looking through 4 scenes when
the first one already show all the problems"). The frame rate while tracing is low (5 to 15 fps in caves), so each
scene costs minutes.

A change to what the whole screen shows (the camera's rays, how materials or geometry are read) is another matter: it
gets one scene per kind of place before it is installed, at least one indoors and one outdoors (`hair-moving` and
`forest` with the `raw` recipe take 2 minutes each). The camera-ray build was installed on the tavern's number alone
(the tracer's first hit is HDRP's surface on 99.2 % of the screen); the forest then said 64 % (PT-70). Read the
"Yardstick" lines in the run's `LogOutput.log`: the share of the screen with the same surface, and the materials that
differ, by name and shader.

One game session can run every scene with its own recipe: `-Scenario refract-jar@views,mirror-tavern` (each game start
costs about a minute; a queue that started the game once per recipe was the owner's "why did you play the game again?",
2026-10-07). Other recipes (`-Recipe`): `frozen` (steps 1 to 5 only), `smoke` (a one-minute check), `why-bright` (the traced picture
with one setting changed at a time: Fog Adaptation off, Dark Adaptation 0, Reflection Boost 0; for the owner's
question why it looks "bloomey" outdoors), `control` (the full recipe's frozen steps with the same timing and path
tracing never on: what the game's picture does by itself in that time, to tell the game's own drift from something the
mod leaves behind), `views` (the traced picture, then each of the tracer's debug views: Albedo, Normals, Surfaces,
Coverage, Materials, Direct, Indirect, Specular, Emissive, then the traced picture with no denoiser: which stage a
defect comes from, as blocky hair or a faceted cup), `lights` (the traced picture with each light at its emitter's place
and size, then at HDRP's place and radius by the hidden "Lights sized as their emitters" key, then the Direct view:
for candles that glow too strongly, lights round 2), `caustics` (traced with the Caustics row on, then off;
`tools/harness/caustics.py` writes what the row adds, `--on`, `--off`, `--game`, `--out`, `--box` for other steps),
`lantern` (PT-14, the owner: "I really really want to see caustics on cliffs or rock inside a cave from water ... How
can we make this appear so I can verify it works?"; scenario `lantern-lake`: the harness stands a lantern over the
mineral cave's lake, `Lantern.cs`: a light with the game's big torch's values and a glowing glass, placed by the
scenario's `lantern` [metres ahead, to the right, up from the eye]; a step's `lantern` is `open`, `shade` (a metal shade
over it, the light a spot pointing down: none of its own light reaches the rock above it) or `away`; its steps take the
game's own picture, the traced one with the Caustics row on and off for both, the Direct view, and two `anim` steps with
the world running, whose frames `analyze.py` writes as `anim.gif` and `anim.webp`: ripples do not show in a still),
`structures` (PT-109: what the tracer's acceleration structures cost, 25 s traced with the world frozen and 25 s with
it RUNNING; read the log's "PT structures 10s" lines of each step: plants are posed and refitted only while the wind's
clock or the camera moves, so the frozen step shows them at 0 and the running one shows what they cost in play),
`mega` (PT-109 on trial, a native side built with `-DTGPT_MEGA_GEOMETRY=ON`: the plain top level, then the hidden
"Mega Geometry trial" 1, the partitioned one, with the world frozen, 16 undenoised frames each: the same picture; then
with the world running the partitioned one in cells of 64, 128 and 256 m (the trial's 1, 2, 3) and the plain one, 25 s
each: the "PT structures 10s" line's `top level` and the "PT stages 10s" line of each step),
`micromaps` (PT-110, FOR MEASURING what the cut-outs cost: the picture as it ships, frozen and running; then, with the
world running and 25 s each, the hidden "Micromaps trial" 1 (the plants in the wind's pose solid), 2 (the characters'
cut-outs solid), 4 (every other cut-out solid), 7 (all of them), 8 (no micromap used) and 16 (the plain submeshes of
characters and plants handed to the shader's test again, as before 2026-10-10), and as it ships once more. The steps
named MEASURING show a WRONG picture on purpose (1, 2, 4, 7: leaves and hair are whole cards to shadows, bounced light
and reflections) or the same picture at another speed (8, 16): read the "PT stages 10s" line of each step against
step 3's, and the "PT cut-outs" line, which counts the frame's cut-out triangles by kind and how much of them the
micromaps settle. Scenes: `metal`, `forest`, `tavern-room`),
`micromaps-plants` (PT-110's M1 ON TRIAL, a recipe of its own so that a fault in it costs no measurement: the hidden
"Micromaps trial" 32 gives the plants in the wind's pose their meshes' micromaps. As it ships and with 32, frozen, 16
undenoised frames each: the same picture, the groups built with micromaps; then with the world running, as it ships,
with 32 and as it ships again, 25 s each: the groups refitted with micromaps, and what it brings in the "PT stages
10s" line; the "PT cut-outs" line says how many of the plants' cut-out triangles trace with a micromap. Scenes:
`forest`, `metal`. If a step with 32 ends the device, run it once with 96: the groups are then built anew at every
pose in place of refitted, which says whether the fault is the refit's).
A new question gets a new recipe: a list of steps, each a set of the mod's config keys.
**Since 2026-10-11 night:** (1) A running step keeps the world frozen, and does not start its walk, until the tracer's
passes are made (the owner: "the time of day has also shifted to the middle of the night at the point of time where the
PT shaders are compiled. That makes comparison with the vanilla frames flawed": a new build's first start makes its
passes for 6 to 11 minutes, and the game's clock ran from 16:35 to 21:38 and 01:23 in two runs). What is left: a
running step itself moves the clock 25 to 45 game minutes, so the fifth running step of a recipe is two hours later
than the first: compare pictures of running steps side by side only when they are neighbours, or use frozen steps.
(Setting the clock back at each step rolls the game's date on a day: taken out again.) (2) A run whose build is new
needs those minutes on top of its steps: give it `-TimeoutMinutes 30` (the default 16 stopped two runs half way, and a
run that is stopped while the game still runs leaves the owner's settings in `artifacts\harness\_config-backup`: the
next run puts them back, or copy them back by hand once the game is closed). (3) Sunshine alone no longer ends a run
(it copies every frame of our own game: 4 to 5 % of the 3D engines); the report still flags it.

(A scene named twice keeps only its LAST recipe: `metal@mega-plants,metal@mega-walk` runs `mega-walk` alone, as run
20261011-182915 found. Run a second recipe of the same scene in a run of its own.)

## What the report says

Numbers are in 8-bit levels of the picture as shown (0 to 255), so they mean what the eye sees.

- **flicker**: how much a single pixel changes from frame to frame.
- **boil**: the same for the mean of 8x8-pixel blocks. Single-pixel noise averages out; patches that boil stay.
  The game's own picture is the reference (its upscaler shimmers a little too). A step is flagged above 0.6
  levels and 3 times the game's (first guesses, `tests/ingame/targets.json`).
- **detail**: fine detail in the scenario's region of the frames' mean (hair that turned to blocks or blur shows
  as a different number than the game's).
- **Is the game's picture back?** `3-game-after-rr` and `5-game-after-toggles` are compared with `1-game`: the
  mean difference, and every 32x32-pixel block. Allowed: 0.25 and 1.0 levels, or 3 times what the game's own
  picture changes by itself, **block by block**: between the two halves of a take's frames, between the two takes
  before tracing, and between the two takes after it (a leftover of the mod is the same in both; what differs
  between them moves by itself: a flame, foam, leaves). A flame so neither excuses nor accuses the rest of the
  picture. `restore-*.jpg` shows where it differs. Limit: something in the game that changes once in the half minute
  between the first take and the later ones reads as "not back"; the `control` recipe tells (PT-20).
- **Is HDRP's state back?** At those same steps the runner writes out every light (intensity, dimmers, what it
  lights), every active volume and what HDRP's volume stack adds up to for the camera (fog, indirect light,
  ambient occlusion, reflections, exposure), the camera's frame settings, the custom passes, every material's
  queue, keywords, switched-off passes and surface values (a character's material with every number), the water
  surfaces and the global shader keywords. It asks Unity and HDRP, never the mod's own bookkeeping. Any value that
  differs from before tracing is a failure and is named. Values that change by themselves (a flame's flicker)
  are found by taking the state twice 0.7 s apart, and left out.
- **Frame rate**: frames a second, the 1 % low, and the tracer's own GPU time by stage.

Pictures: `overview.jpg` (every step side by side: the frame, the region at 1:1, the change map), each step's
`atlas.jpg` (the region in all 8 frames at 1:1, with the mean, the change map and two differences).

## The performance target

The owner's goal: "As performant as Cyberpunk's PT". In `tests/ingame/targets.json`, confirmed by the owner on
2026-10-06 ("yes, I guess. We may expose some levers to tweak the visuals to go beyond that visually for a fps hit"):
**55 fps** at 4K with DLSS Performance and Ray Reconstruction, no frame generation, on the RTX 5080.
That is what published tests give for Cyberpunk 2077's path tracing on this card (gamegpu.com: 55 fps; evetech:
"close to 60"). Cyberpunk is not installed on this PC, so it is not measured here: a number from its own benchmark
on this PC should replace it. Measured by the first run (below): 26 to 53 fps with Ray Reconstruction.

## What the game's code offers (TG.Main.dll 1.30.012, decompiled with ilspycmd, 2026-10-06)

- **Saves**: `World.All<SaveSlot>()` has every save once the title screen is up: `ID` ("Slot_83"; the file is
  `<ID>.zip`), `Name`, `IsCustomName`, `HeroLocation` ("Ron's Tavern"), `SceneRef` / `AdditiveSceneRef`
  (`CampaignMap_HOS` + `Interior_Tavern_02`), `LastSavedTime`, and the menu's picture (`_screenshotBytes`, a 320x180
  JPEG). `LoadSave.Get.Load(slot, "why")` loads one, from the title screen or from play (what Continue and the load
  menu call); `slot.CanLoad()` says whether it may. `SaveSlot.CreateAndSave(name, isCustomName)` is the save menu's
  new save. Saves from older game versions (1.21, 1.25) load in 1.30.
- **A load has finished** when `ScenePreloader.IsLoadingCompleted`, `!LoadingScreenUI.IsLoading`, `Hero.Current`
  exists and `UIStateStack.Instance.State.IsMapInteractive`. The hero's `Coords` and `Rotation` are saved and put
  back (`Hero.TeleportTo(position, rotation)` is the call).
- **What saves**: `LoadSave.Save(slot, deleteIfFailed)` is the one door. Its callers: the timer, portals and rest
  (`AutoSaving`, all asking `LoadSave.CanAutoSave`, which reads the Auto Save setting), quest steps
  (`SManualAutoSave`, forced), quick save, the save menu, the character creator. `World.HasAny<SaveBlocker>()` stops
  every one of them; a `SaveBlocker` lives in the gameplay domain, which a load drops.
- **The start**: the game opens on `PressStartUI` ("press any button") and waits until that model is discarded;
  then a file check once per game version (two minutes after an update or a branch switch), shader preloading,
  popups after an update, and the menu (`VTitleScreenUI`). The game has its own `Automations` that skip these.
- **Time**: `Time.timeScale` is the game's own switch (`GlobalTime`). The time of day is
  `World.Only<GameRealTime>().WeatherTime`; `SetWeatherTime(hour, minute)` moves it forward (never back: the next
  day). The mod's own timing uses unscaled time, so it runs on in a frozen world.
- **The HUD**: `HUD.ShowHUD(bool)` (private) and `UIUtils.HideUI()` / `ShowUI()` (every root canvas, the photo
  mode's way; the loading screen is a canvas too, so show it again before a load).
- **Logs**: the game's own log has times: `%USERPROFILE%\AppData\LocalLow\Questline\Fall of Avalon\Logs\<date>.txt`.

## The parts

| File | What it is |
|---|---|
| `tools/harness/Run-PtHarness.ps1` | the start script: GPU check, build, configs, start, wait, save check, analysis |
| `src/TGPathTracing.Harness/` | the runner plugin (BepInEx 5, Mono). In no release zip. Does nothing without a request file. It references no build of the mod: it sets the mod's config entries and reads its state by name, so it tests whatever build is installed |
| `tools/harness/analyze.py` | frames and state to pictures, numbers and `report.md` |
| `tools/harness/saves_sheet.py` | contact sheets of the saves from an `-Inventory` run |
| `tools/harness/calibrate.py`, `tests/ingame/camera.json` | the view each save was made with, found from the save's own picture |
| `tools/harness/Deploy-PtBuild.ps1` | the tree's plugin (and native DLL) into the game for a run, the replaced files kept aside |
| `tools/harness/compare.py` | two runs side by side: numbers that moved, and each scene's region from both |
| `tools/harness/pairs.py` | `game-vs-traced.jpg`: the whole frame, the game's own beside the traced. Look here first; the region's crops miss what changed elsewhere (the analysis writes it; the script does it for an older run) |
| `tools/harness/crop.py` | one part of the screen at full size from several steps or runs side by side (`--box` in shares of the screen): a head, a jar, a flame |
| `tests/ingame/scenarios.json`, `targets.json` | the scenarios and what a run is judged against |
| `tests/harness_analyze_test.py` | the analysis checked on made-up runs, each check with its broken twin |

## State and limits (2026-10-06)

- Run in game: the list of 84 saves; one scenario with the short recipe (13:41, 1.2 minutes); **four scenarios with
  the full recipe** (13:48, `artifacts/harness/20261006-134601`, 8.9 minutes: a man by day at Horns of the South, a
  man in Sagremor Castle, Ron's Tavern's room, Cuanacht by day, all on stand-in saves). Every part ran: the "press
  any button" screen left, loads of 12 to 31 s, the HUD hidden, the world frozen and running, 8 consecutive frames a
  step at 3840x2160, the state (24,000 to 39,000 lines a step), Ray Reconstruction and NRD, frame rates, quit,
  configs back, 103 save files untouched.
- **What it found** (the 2026-09-28 12:45 build of the mod):
  - Turning path tracing off gives the game's **picture** back in all five places, also after four more toggles
    (mean difference 0.13 to 0.45 levels, inside what the game's own picture changes by itself).
  - It does **not** give all of the game's **state** back: hair materials keep another render queue (2750 -> 3000,
    the scalp's 2749 -> 2999; 4 values at Horns of the South, 9 and 14 in the tavern, none of 32,152 in the castle or
    of 23,682 at Cuanacht). HDRP then draws that hair in another pass than the game set. Not fixed yet.
  - **Hair**: at Horns of the South the man's hair is soft and auburn in the game's picture and pale, clumped, with
    dark gaps in the traced one, with either denoiser (`npc-day/hair-crop.jpg`). His neck has a white shine traced.
  - **Frame rates** (4K, DLSS Performance, the world running, Sunshine encoding beside it; target 55):

    | place | the game | Ray Reconstruction | NRD | tracer GPU, RR |
    |---|---|---|---|---|
    | Horns of the South, day | 100 | 37.0 (1 % low 18.6) | 35.2 | 24.0 ms |
    | Sagremor Castle | 144 | 26.4 (17.7) | 25.3 | 36.3 ms |
    | Ron's Tavern | 108 | 42.6 (27.6) | 39.4 | 22.1 ms |
    | Cuanacht, day | 108 | 52.7 (47.7) | 48.9 | 13.1 ms |

    The bounced light is the largest part everywhere (4.2 to 16.4 ms, plus 1.8 to 3.2 for its spatial reuse), then
    reflections (2.9 to 8.7) and direct light (1.3 to 6.6). NRD costs 3.3 to 4.5 ms more than Ray Reconstruction.
  - **Boiling**, 8x8 blocks, in levels: frozen, the game 0.02 to 0.05, traced 0.04 to 0.14. Running, in the tavern:
    the game 0.03, Ray Reconstruction 0.10, NRD 0.11. Outdoors the running world moves by itself (rain, grass): the
    game's own picture changes more than the traced one there. None of these views is the cage: that save is to make.
  - NRD keeps less fine detail than Ray Reconstruction (Cuanacht: the game 7.4, RR 6.8, NRD 4.6).
  - The traced picture is brighter than the game's: x1.1 (Cuanacht) to x2.5 (the castle), x4.3 at the tavern's shelf.
- The thresholds for boiling are first guesses and flagged nothing in these views; set them when `PT boil-cage` exists.
- The reference is the game with the mod loaded and Off, never on since the load. A game without the mod at all
  is not compared yet (it needs a second start with the plugin moved away).
- Only BepInEx and the TG mods were in the game folder for these runs: the owner's other mods (fog, torches,
  conversation lighting) change what both pictures show, and were not deployed that day.
- The pictures the first runs lacked are in the analysis since: each step's whole frame (`frame.jpg`) and the
  region with the game's own beside the traced at full size (`side-by-side.jpg`).
- The harness never judges how it looks. That stays the owner's.

## The normal version

This harness runs on the Mono version only. `.\tools\harness\Run-Il2CppProbe.ps1` is the normal version's test (2026-10-30): it builds and installs the plugin of `tools\harness\Il2CppProbe`, starts the game through `-LaunchOnly`'s GPU guard, walks Frame Generation, the upscalers, Neural Rendering and the Video tab with saving blocked, quits, and takes the plugin and the configs back out. Results: `artifacts\il2cpp-probe\<time>` (`run.log`, a picture of each step). It does not force the game to the front, and DLSS Frame Generation adds no frames behind another window: read `focused=` in the log. BepInEx writes `BepInEx\interop` at the first start after an install or a game update, so start once with `-LaunchOnly` and close the game before building for that version.
