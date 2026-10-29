# Brief: look at every picture of the newest runs and list every defect (the owner's question of 2026-10-08) - one agent, read only

The owner, 2026-10-08 12:55: "Have you taken a thorough look on the images and atlases from the past runs so you know
all the current defects? Including flickering inside leves and stuff?" The coordinator had not. You do it: a complete,
honest list of what is wrong in the traced picture today, scene by scene, with where to look and a number for each.

## Where the pictures are (the MAIN tree, READ ONLY: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness`)

- THE FULL RUN of the build that is in the game, being made now: the newest folder there that has a `campfire`
  folder in it (27 scenes with the recipe `quick`: `1-game` the game's own picture, `2-traced-none` 16 frames with no
  denoiser, `3-traced-nrd` 16 frames with NRD, `4-game-after`; then `mirror-fog`, `mirror-water`, `mirror-cave`,
  `mirror-tavern`, `lantern-lake`). It takes about an hour from 12:55; its `numbers.txt` (flicker, boiling and the mean
  of NRD against the undenoised mean, a scene) is written at its end. Wait for it with ONE background script that
  ends when `numbers.txt` exists; meanwhile start on the runs below.
- Already there: `20261008-124542` (`hair-moving`, `candle`, `forest`, `vista`: the same build), `20261008-111121` (the mirror
  scenes and the cave lake), `20261008-083001` (`water-day`, `glass-jars`; an older build, same picture for these),
  `20261008-101310` (`glass-jars`).
- In each scene's folder: `overview.jpg` (each step's picture, its measured region at 1:1, and a change-per-pixel map:
  bright where pixels change from frame to frame in a frozen world), `game-vs-traced.jpg`, `side-by-side.jpg`,
  `scenario.json`; in each step's folder `frame.jpg`, `full.jpg`, `atlas.jpg` and the raw frames `f00.raw` ...
- Tools (run them from the main tree's folder, they only read; write their output into YOUR scratch folder with
  `--out`): `tools/harness/flicker.py <step> --game <scene>\1-game [--boil]` (spots that change most, and a map),
  `tools/harness/crop.py --box x0,y0,x1,y1 --out <file> <step> <step> ...` (a region of several steps side by side at
  full size: USE IT, a 1920-wide picture hides what a crop shows), `tools/harness/mean_colour.py <step> <step>`,
  `tools/harness/mirror.py <scene folder>`. `docs/PT-HARNESS.md` says what each scene is for.

## What to look for (the owner's own words over two days are in `docs/PT-REQUESTS.md`; grep, do not read it whole)

Everything that is wrong against the game's picture or against what a real camera would see. In particular:
flicker and boiling in a frozen world (inside trees and leaves, grass, hair, shadows' edges, thin things, water),
with no denoiser AND with NRD; wrong colour or brightness of whole things (orange stakes, brown wax, white wax, dark
caves, burnt-out or dull lights); missing or wrong surfaces (untextured, black, see-through, wrong material, missing
decals); water (its colour, waves, foam, reflection, the bed, the waterline); caustics; mirrors and reflections on
metal and wet things; glass and what is seen through it; skin, eyes, hair, eyebrows up close; fog, haze and light
shafts; shadows (missing, too hard, leaking, detached); fireflies; edges and seams (light lines, dark lines);
what the denoiser does that it should not (smear, ghost, lost detail, colour shift); anything that is only in one of
the two pictures.

For each defect: look at the 1:1 crop, not the overview. Say what you SEE, not what you guess the cause is; a guess is
welcome on a line of its own, marked as a guess.

## What to write

One file, `inspection-20261008.md` in your scratch folder
(`C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\inspection2\`),
written scene by scene as you go (a usage limit can cut you off: what is in the file is what survives), your crops
beside it:

1. A table at the top, kept current: every defect on one row: a short name, the scenes it shows in, how bad (a
   number: levels of flicker, a brightness ratio, a share of the screen), with which denoiser, the crop's file name,
   and whether `docs/PT-REQUESTS.md` already has it (its PT number; grep the headings and the newest entries PT-73 to
   PT-81) or it is NEW.
2. Then a section a scene: what the scene is, what is right, each defect with its crop and numbers.
3. At the end: the five worst things a player would notice first, in your judgement, and what you could not judge.

## Rules

- READ ONLY everywhere but your scratch folder. Never start the game, never build, never run a GPU test, never
  deploy, never commit, never edit the repo.
- Keep tokens down: one overview a scene first, then crops only where something looks wrong or a number is high;
  never open raw frames as images; do not read source code (this is about pictures).
- Plain words, short sentences; numbers and file paths are welcome.
- Final message: five lines at most: how many defects, how many new, the five worst, the file's path.
