# Inspection of the traced picture, 2026-10-08 (inspector agent, read only)

(Copied from the agent's scratch folder by the coordinator at 14:20. The crops and sheets named below, and the agent's
read-only tools, are in `artifacts/inspection-20261008/` (not in git). What each row became: `docs/PT-REQUESTS.md`,
PT-82 to PT-89.)

What this is: every defect I can see in the newest in-game harness runs, scene by scene, against the game's own
picture and against what a camera would see. I looked at 1:1 crops, not only at overviews.

How to read the numbers:
- "flicker": the standard deviation of a pixel's brightness over the step's frames in a frozen world, in 8-bit levels,
  averaged over the crop (the game's own is 0.1 to 0.2: rain and particles).
- "boil": how far the mean of a 16-pixel block wanders over the frames, in levels (the game's own: 0.02 to 0.05).
- "detail": the mean picture minus its own 5-pixel blur, in levels (fine texture; low means smeared).
- "x1.27": the crop's mean brightness against the game's crop.
- "none" = Denoising None (16 frames), "NRD" = Denoising NRD (16 frames). Ray Reconstruction is in none of these runs.
- Crops are in this folder. Each crop sheet: top row the mean of the frames (game, none, NRD); bottom row one single
  frame of none and of NRD, then the flicker maps (left none, right NRD; white = 12 levels).
- My tool: `look.py` in this folder (reads the kept frames, cuts a box, prints the numbers).

Runs: `20261008-124542` (forest, vista, candle, hair-moving; the build in the game: plugin 32B99654899A, native
D7F68BB07CCC), `20261008-111121` (mirror scenes, lantern-lake), `20261008-083001` and `-101310` (water-day,
glass-jars), then THE FULL RUN (27 scenes) when it is there.

STATE OF THIS FILE: finished 2026-10-08 13:55. 31 scenes looked at (the 4 of run `20261008-124542`, then all of the
full run `20261008-130029`: 26 traced scenes, 4 mirror scenes, the lantern lake; `metal` was not traced, `black-spots`
did not run). 123 rows in the table; 44 are marked NEW. Sections: 1 the table, 2 scene by scene, 2b one row a
scene, 3 the five worst and what I could not judge.

## 1. The table (kept current)

| # | Defect | Scenes | How bad (number) | Denoiser | Crop | Known? |
|---|---|---|---|---|---|---|
| 1 | Grass flickers, and NRD does not calm it | forest | flicker 1.37 none, 1.25 NRD, game 0.12 (10x) | none + NRD | forest-grass.jpg, forest-grass-zoom3.jpg | PT-73 (j), PT-80 |
| 2 | Grass blades are dotted, stair-stepped lines where the game has smooth blades | forest | to the eye at 3x zoom; NRD's flicker sits on single blades (up to 14.6 levels) | none + NRD | forest-grass-zoom3.jpg | NEW (the blur was known, the dots not) |
| 3 | Grass is brighter and yellower (olive) than the game's dark blue-green | forest | x1.18 to x1.28; blue/green 0.59 to 0.62, game 0.63 to 0.67 | both | forest-grass.jpg | PT-70 / PT-03 |
| 4 | A tree's trunk boils | forest | boil 2.10 none, 0.80 NRD, game 0.03 (70x, 27x) | none + NRD | forest-tree.jpg, forest-boil-nrd-map.jpg | PT-73 (12:55 note) |
| 5 | Tree trunks are red-brown where the game's are grey | forest | trunk RGB 82,81,72 against 75,81,75 (red/green 1.01 against 0.93) | both | forest-leaves-zoom3.jpg, forest-tree.jpg | half known (old list B10 "uncertain") |
| 6 | The canopy's leaves flicker; lighter, yellower, coarser clumps than the game's fine sprays | forest | flicker 1.09 none, 0.40 NRD, game 0.14 | none (NRD 3x the game) | forest-tree.jpg, forest-leaves-zoom3.jpg | PT-73 (12:55 note); PT-61 for the clumps |
| 7 | The palisade's stakes are bright orange | forest | x1.27, red/green 1.44 (game 1.28) | both | forest-stakes.jpg | PT-73 (12:55 note) |
| 8 | No wet shine in the rain: rock, path, barricade are matte | forest | rock: brightest 0.5 % at 147 (game 187), detail 5.4 (game 8.5), x0.90 | both | forest-rock.jpg, forest-stakes.jpg | old list #17; no PT entry of its own |
| 9 | NRD wipes a rock's texture into flat blotches | forest | detail 1.49 with NRD, 5.37 none, 8.50 game (NRD keeps 18 % of the game's) | NRD | forest-rock.jpg | PT-19 in general; this bad: NEW |
| 10 | The far edge of the grass against the fog boils more with NRD than without | forest | boil 0.77 NRD (max 4.1), 0.42 none, game 0.05 | NRD | forest-ridge.jpg, forest-boil-nrd-map.jpg | NEW |
| 11 | Orange speckles all over far grass and ferns | forest | to the eye (the fog hides it from a colour count) | both | forest-ridge.jpg, forest-lantern-right.jpg | NEW |
| 12 | A far lantern pulses with no denoiser and is a glowing ball with NRD; its hut is lit yellow | forest | x1.32 none (flicker 4.0, up to 33.7), x1.56 NRD, peak 246 (none 171, game 124) | none + NRD | forest-lantern-right.jpg | PT-73 (e) indoors; outdoors NEW |
| 13 | Edges of poles and stakes are stair-steps (no smoothing) | forest | to the eye at 2x | none + NRD | forest-lantern-right.jpg, forest-npc-zoom.jpg | old list #15, B8 |
| 14 | A far person: red and white dots on the skin (none); a smooth orange blob for a face (NRD); bracers burnt to white blocks | forest | flicker on her up to 21.7 levels none | none + NRD | forest-npc-zoom.jpg | red dots: PT-73; white blocks: NEW |
| 15 | Far wooden structures the game shows through the fog are nearly gone | forest | to the eye | both | forest-npc-zoom.jpg, forest-lantern-right.jpg | NEW |
| 16 | A tree's whole purple canopy is missing: bare pale branches | vista | the canopy fills about 40 % of a 614x605 crop in the game, 0 traced | both | vista-purpletree.jpg | NEW |
| 17 | A bare tree's twigs are bright yellow-green dots; it flickers through NRD | vista | x1.14; flicker 1.32 none, 0.77 NRD, game 0.17 | both | vista-cliff-tree.jpg | old list ("the yellow-green tree"); PT-77 |
| 18 | A flowering bush glows yellow-white | vista | x1.63 none, x1.56 NRD; peak 238 (game 169) | both | vista-cliff-tree.jpg | PT-77 (a guess that it is the same) |
| 19 | The dirt ground: washed out and flat with no denoiser, dark red with NRD | vista | x1.34 none, x0.91 NRD; NRD is 0.68 of none; red/green 1.65 NRD, 1.38 none, 1.42 game | both | vista-ground.jpg | PT-75 for NRD; the pale flat ground: PT-70 |
| 20 | NRD turns grey metal orange and smooth (shield rim, boss, cart's band), darkens the shield | vista | shield x0.82 NRD against x1.18 none; red/green 1.92 against 1.60 | NRD | vista-shield.jpg | PT-75 |
| 21 | NRD halves the fine grain of wood | vista | cart: detail 4.56 NRD, 9.37 none, 7.84 game | NRD | vista-cart.jpg | PT-19, old list B24 |
| 22 | Weathered grey wood is orange-tan and brighter | vista | cart x1.21, shield x1.18 | both | vista-cart.jpg, vista-shield.jpg | old list #15, B23 |
| 23 | Grass blades by the shield: neon yellow dotted lines; single blades flash with NRD | vista | NRD: up to 30 levels on a blade (the strongest spots of the whole screen) | none + NRD | vista-shield.jpg | PT-73 (j); see row 2 |
| 24 | A thin line on the ground (a rope or wire) is drawn as dashes | vista | to the eye | both | vista-cart.jpg | NEW (small) |
| 25 | The shade under the cart is black-blue; the game shows boards and ground there | vista | x0.11 to x0.21 of the game's | both | vista-corner-left.jpg | PT-39 (the same kind) |
| 26 | The daylit corner is half as dark and orange; the wall on the right is nearly black | candle | whole picture x0.49; right wall x0.14 (13 against 90); 55 % of the screen under half the game's | both | candle/overview.jpg (the run's), candle-plants.jpg | PT-39 |
| 27 | Candle wax with no denoiser is a field of orange and blue dots | candle | flicker 8.8 on the wax (p95 16.8), the game 0.4; still dotted in the mean of 16 frames | none | candle-candles.jpg, candle-wax-zoom3.jpg | PT-73 says it of skin; on wax NEW |
| 28 | Candle wax with NRD: pink-white, no gloss, where the game's is ivory with a sheen | candle | blue/green 0.90 against 0.82 | NRD | candle-candles.jpg | PT-40 |
| 29 | A fine regular grid (every second pixel) lies over NRD's picture in dim scenes | candle, hair-moving, a little in forest and vista | 62 % of the screen's blocks in candle, 30 % in hair-moving, 8 to 9 % outdoors; none: 0 to 2 %; the game 0 to 1 %; about 1.4 levels | NRD | candle-stone-zoom4.jpg, candle-wax-zoom3.jpg | NEW |
| 30 | Stone walls lose their pale mortar and half their texture | candle | detail 2.1 against 4.9 | both | candle-stone-zoom4.jpg | NEW (to the eye: the seams are dark, the game's are pale) |
| 31 | A wall behind the corner is hidden in flat grey haze | candle | to the eye | both | candle-plants.jpg | PT-39 (its "what covers the corner") |
| 32 | A cluster of leaves pulses with NRD, steady without | candle | NRD boil up to 3.2, flicker up to 21.6; none 0.66 and 16 | NRD | candle-plants.jpg | NEW |
| 33 | The hero up close is half as bright and orange-red; the room around her is up to 8 times brighter than the game's | hair-moving (tavern) | hero x0.50 none, x0.57 NRD; red/green 2.07 against 1.45; ceiling x2.2 (blocks up to x8), alcove x2.1 to x2.3 | both | tavern-hero.jpg, tavern-alcove.jpg | PT-73 (the game's white camera light is left out), PT-17 |
| 34 | Eyes are black holes: no white, no iris | hair-moving | to the eye at 2x | both | tavern-face-zoom2.jpg | PT-44 / PT-18 (worse than "flat discs") |
| 35 | Skin with no denoiser: dense red grain, still there in the mean of 16 frames | hair-moving, forest (far person) | flicker 1.77 on the face, the game 0.10 | none | tavern-face-zoom2.jpg | PT-73 |
| 36 | Skin with NRD: smooth like plastic, and a hard stair-step shadow line down the nose and jaw | hair-moving | detail 1.90 against 4.36 in the game | NRD | tavern-face-zoom2.jpg | PT-43; the stair-step line NEW |
| 37 | Hair: dark, stringy, striped with black gaps; strands flash with NRD | hair-moving | flicker 1.05 NRD on the head (up to 60), none 1.77, game 0.10 | none + NRD | tavern-face-zoom2.jpg, tavern-hero.jpg | PT-01, PT-73 (i) |
| 38 | The feather crest is a comb of hard white blocks; a smooth feather in the game | hair-moving | to the eye | both | tavern-hero.jpg, tavern-face-zoom2.jpg | NEW |
| 39 | White dots flash on armour with NRD (fireflies); the ribs' highlights are blocks | hair-moving | single dots up to 87 levels, boil up to 11.2 (the highest of the scene) | NRD | tavern-hero.jpg (right map) | NEW |
| 40 | NRD wipes fine patterns: the pauldrons' scales, the tablecloth's net, wood grain | hair-moving | whole picture detail 1.33 NRD, 2.33 none, 2.27 game | NRD | tavern-hero.jpg, tavern-table.jpg | PT-19 |
| 41 | With NRD a candle is a glowing ball; with no denoiser its wax is dots | hair-moving, forest (row 12) | table crop: peak 107 NRD, 86 none, 68 game | NRD | tavern-table.jpg, tavern-alcove.jpg | NEW as "NRD adds a glow" (the opposite of PT-75) |
| 42 | NRD is brighter and bluer than the undenoised picture at the far table | hair-moving | x1.56 against x1.13; red/green 1.27 against 1.56 | NRD | tavern-table.jpg | PT-75 |
| 43 | A far person's face is covered in black dots with no denoiser | hair-moving | to the eye | none | tavern-table.jpg | PT-73 (the same noise) |
| 44 | A whole patch of tall grass, a cabbage and leafy plants is missing: bare grey ground instead | npc-day | about half of a 614x562 crop; the ground there x1.47 (blocks up to x3.3); about 8 % of the screen differs | both | npcday-rightground.jpg, ov-npc-day.jpg | NEW (PT-63's "no object left out") |
| 45 | A fir tree has orange leaves and white blobs in it | npc-day | to the eye | both | npcday-tree.jpg | NEW |
| 46 | Grass flickers as much with NRD as without (the worst grass so far) | npc-day | flicker 1.97 none, 1.93 NRD (p95 5.25 NRD against 4.34), game 0.12 (16x); NRD boil 0.36 against 0.27 none | none + NRD | (numbers; same look as forest-grass.jpg) | PT-73 (j), PT-80 |
| 47 | With NRD a candle's wax is burnt-out white and throws a glow twice its light over the bar | hair-eyebrows, hair-moving, forest | the bar by the candle: x3.27 NRD, x1.64 none (59 against 29.5); peak 255 | NRD | hair-eyebrows-bar.jpg | row 41; PT-40 has the halo, the doubling by NRD is NEW |
| 48 | Ron's hair and face are saturated orange-red; twice as bright as the game's room | hair-eyebrows | head x1.46; red/green 2.67 (game 1.90); whole picture x1.87 none, x2.13 NRD | both | hair-eyebrows-head-zoom2.jpg | PT-73 (i), PT-38/PT-79 for the room's light |
| 49 | Eyebrows are nearly gone; eye corners and lashes are black square blocks with NRD | hair-eyebrows | to the eye at 2x | NRD (none: faint) | hair-eyebrows-head-zoom2.jpg | PT-01, PT-55 |
| 50 | NRD smears hair into flat orange with block streaks | hair-eyebrows | detail 2.04 NRD, 4.24 none, 2.27 game | NRD | hair-eyebrows-head-zoom2.jpg | PT-19 / PT-01 |
| 51 | A man a metre from the camera is a black shape; only the top of his hair is lit | hair-glowing | x0.21 (6.3 against 29.8); the wall behind up to x20 | both | hair-glowing-npc.jpg, ov-hair-glowing.jpg | PT-54, PT-73 (camera light left out) |
| 52 | A face a metre away is nearly black; hair is a black mass | eyebrows-flickering | face x0.13 none, x0.25 NRD (luma 7 and 13 against 53); whole picture x0.70 | both | eyebrows-flickering-head-zoom2.jpg | PT-73 (camera light left out), PT-01 |
| 53 | Hair flickers with NRD over the whole head (the eyebrows only a little) | eyebrows-flickering, hair-moving, hair-glowing | head: flicker 1.04 NRD, 1.41 none, game 0.07; NRD boil 0.47 (none 0.23) | NRD | eyebrows-flickering-head-zoom2.jpg (right map) | PT-55 (it names the eyebrows; the hair is worse) |
| 54 | Single white dots flash all over dim rooms, more with NRD than without | tavern-room, eyebrows-flickering, hair-moving, hair-eyebrows | dots over 10 levels in calm places: tavern-room 140 NRD, 73 none, 20 game; hair-moving 110, 79, 8; eyebrows 46, 26, 1 | NRD (and none) | ov-tavern-room.jpg, ov-eyebrows-flickering.jpg (right maps), tavern-hero.jpg | NEW |
| 55 | The tavern's floor in the middle is half as bright, the ceiling up to 8 times brighter | tavern-room | floor x0.50 (blocks x0.24); ceiling blocks x8.6; whole picture x1.41 | both | ov-tavern-room.jpg | PT-73 (e), PT-79 |
| 56 | A lace tablecloth's net is ragged blocks; cups and mugs are grain | tavern-room | to the eye at 3x | none (NRD: faint net) | tavern-room-dots-zoom3.jpg | NEW (the cut-out's fine pattern) |
| 57 | Leaded windows have no lattice and no glass: an open hole to the night | npc-candle | the lattice fills the window in the game, 0 in the upper window traced | both | npc-candle-window.jpg, npc-candle-man.jpg | NEW |
| 58 | With NRD what is seen through the window is smeared into flat pale shapes | npc-candle | to the eye | NRD | npc-candle-window.jpg | PT-80 (mirror smeared); through a window NEW |
| 59 | The well's water is a milky pale blue sheet, 2.4 to 8 times brighter than the game's dark water | npc-candle | well x2.42 none, x2.50 NRD; blocks up to x9.7 | both | npc-candle-well.jpg | PT-81 |
| 60 | With NRD the candle flames' reflections in the water are soft blobs | npc-candle | to the eye | NRD | npc-candle-well.jpg | PT-80 |
| 61 | Small black blocks at a sleeve's seam; black dots inside an ear | npc-candle | to the eye | none (smaller with NRD) | npc-candle-man.jpg | PT-27 (a guess that it is the same) |
| 62 | The traced picture is made at 1924x1084 and shown at 3840x2160: every edge is a 2-pixel step, thin things are dots; the game's own picture, made at the same size, is smooth | every scene | `step.json`: renderWidth 1924, renderHeight 1084, screen 3840x2160 (DLSS Performance) | none + NRD | forest-grass-zoom3.jpg, tavern-room-dots-zoom3.jpg, forest-lantern-right.jpg | PT-65 says "full resolution"; the steps: old list #15, B8. This is the likely cause of rows 2, 13, 23, 24, 38, 56 (a guess) |
| 63 | With NRD a mirror's picture is blurred away | mirror-tavern, mirror-fog | detail 0.05 of the direct view's in the tavern, 0.14 in the fog (no denoiser: 1.00 and 0.80) | NRD | mir-mirror-tavern-centre.jpg | PT-80 |
| 64 | In a mirror leather and nail heads are glossy; seen directly they are matte | mirror-tavern | to the eye | none | mir-mirror-tavern-centre.jpg | PT-46 (the same surface must look the same) |
| 65 | NRD does not calm the boil inside a mirror | mirror-tavern | boil 0.30 NRD, 0.31 none, the game 0.01 | NRD | (numbers) | NEW |
| 66 | In a mirror the foggy night is grey and brighter than seen directly; the fire's embers are missing | mirror-fog | x1.61 raw, about x1.76 with the two steps' gains taken out; shapes 0.85 | both | mir-mirror-fog-whole.jpg | fog: PT-46/PT-37; embers: PT-67 |
| 67 | Frame time | every scene | GPU a frame at 1924x1084: forest 123 ms, npc-day 90, tavern-room 75 (8 to 13 frames a second; the target is 55) | both | step.json `gpuMs` | PT-08 |
| 68 | The sea is a flat milky beige sheet to the horizon: no waves, no dark water, no foam | mirror-water (and water-day, below) | x2.87 (135 against 47); detail 0.25 against 3.55; red/green 1.10 against 0.93 | both | ov-mirror-water.jpg, mirror-water-sea-direct.jpg | PT-81 (still so on the 12:45 build) |
| 69 | The sea flickers hard with no denoiser | mirror-water | flicker 5.27, boil 1.66 (the game 0.06, 0.01); NRD 0.27, 0.16 | none | ov-mirror-water.jpg (right map: white) | PT-68 has dots on lit water; a whole flickering sea NEW |
| 70 | Sand has lost its ripples: flat where the game's has wind ripples with shade | mirror-water | detail 5.3 against 10.4 | both | mir-mirror-water-centre.jpg, mirror-water-sand-direct.jpg | NEW (PT-28 asks for relief on walls; here the game HAS it and the tracer drops it) |
| 71 | The mineral cave away from its lamps is black where the game shows mossy walls and a lake | mirror-cave (and the cave scenes below) | x0.29 on average, x0.07 at the bottom; 32 % of the pixels near black (the game 4.5 %) | both | ov-mirror-cave.jpg | PT-48 |
| 72 | The crypt's left wall and bench are black; the wall behind the cage is lit warm where the game's is dark blue | boil-cage | left wall x0.36 (blocks x0.15); the cage's wall x1.1 to x1.5 | both | ov-boil-cage.jpg, boil-cage-cage.jpg (shown x4) | PT-73 kind (fill lights left out) |
| 73 | With NRD the bricks behind the cage lose their texture | boil-cage | detail 0.29 NRD, 0.58 none | NRD | boil-cage-cage.jpg | PT-19 |
| 74 | The lantern washes its wall and bench out to pale peach | glass-jars | round the lamp x2.51 none, x2.90 NRD; peak 220 to 228 (the game 122); the room's left wall x8.9 to x11.2 | both | glass-jars-lantern.jpg | PT-73 (e), PT-79 (hold 2 is the owner's pick) |
| 75 | The lantern's glass is milky white; clear in the game | glass-jars | to the eye | both | glass-jars-lantern.jpg | PT-13 / PT-76 (frosted look: NEW) |
| 76 | A lit plank's wood grain becomes rows of jagged dark steps | glass-jars | flicker 2.96 there | none | glass-jars-plank-zoom3.jpg | NEW |
| 77 | A metal candlestick is grainy and half see-through with no denoiser; a faceted block shape with NRD | glass-jars | to the eye at 3x | both | glass-jars-plank-zoom3.jpg | NEW |
| 78 | With NRD the lit plank and wall lose all texture | glass-jars | round the lamp detail 2.14 NRD, 5.91 none, 3.73 game | NRD | glass-jars-lantern.jpg, glass-jars-plank-zoom3.jpg | PT-19 |
| 79 | A white bottle is brown grain with no denoiser and white with NRD; a small barrel on the shelf is nearly black | glass-jars | shelf: x1.99 none, x2.37 NRD | both | glass-jars-shelf.jpg | PT-75 (the two differ); the bottle NEW |
| 80 | The chair and the floor in front are half as bright and red | glass-jars | chair x0.50 none, x0.59 NRD; red/green 3.29 (game 1.39) | both | ov-glass-jars.jpg | PT-73 kind |
| 81 | Shallow water hides the bed: the game shows sand ripples and foam lines through it, the tracer an opaque sheet | water-day | detail 0.2 to 3 against 5 to 9; x1.7 to x2.5; whole picture x1.78; 46 % of the screen over twice the game's | both | water-day-shadow.jpg, ov-water-day.jpg | PT-81 ("the bed not showing") |
| 82 | The sea with no denoiser flickers over its whole surface | water-day, mirror-water | whole picture 3.23 (the highest of the run), sea 5.5 to 5.6, boil 1.8; game 0.08 | none | ov-water-day.jpg | row 69 |
| 83 | With NRD white dots flash on the sea | water-day | 30 dots over 10 levels (0 in the game); one block wanders 14.9 levels | NRD | ov-water-day.jpg (right map) | PT-68 |
| 84 | With NRD what is seen through a glass jar is smeared to brown mud, and it boils | refract-jar | jars: detail 0.86 NRD, 3.30 none, 2.49 game; boil up to 15 levels with NRD (6 none; cells 2.43 against 0.95) | NRD | refract-jar-jars.jpg | PT-35 (refraction); the smear and boil through glass NEW |
| 85 | Glass jars are cloudy with dark blotches; clear with thin rims in the game | refract-jar | to the eye | none | refract-jar-jars.jpg | PT-13, PT-35 |
| 86 | The cellar is lit from one side: left wall at half, right up to 6.8 times; stones look deeply pitted | refract-jar | left x0.50 (blocks x0.17), right blocks x6.8 | both | ov-refract-jar.jpg | PT-73 kind |
| 87 | A bottle by a brazier is dark red where the game's is lit yellow-orange | caustics-glass | x0.75, red/green 2.55 against 1.73 | both | caustics-glass-bottle.jpg | PT-73 kind (small) |
| 88 | Black rectangles at the gaps between planks | caustics-glass | to the eye | none | caustics-glass-bottle.jpg | PT-27 (a guess) |
| 89 | With NRD single dots flash on a table top outdoors | caustics-glass | 121 dots (0 in the game), up to 30 levels | NRD | ov-caustics-glass.jpg | row 54 |
| 90 | The scene `black-spots` did not run: its save is missing | black-spots | "save PT black-spots was not found (none of the 98 saves)" | - | progress.log | PT-25/PT-27: could not judge |
| 91 | The mineral cave's lake view is nearly black; only a blue haze and one lamp | glow-lake | x0.39 (luma 8.8 against 22.7); 79 % of the screen under half the game's; 27 % near black | both | ov-glow-lake.jpg | PT-48 |
| 92 | Hundreds of single dots flash in the dark cave | glow-lake, lantern-lake, mirror-cave | glow-lake: 254 dots none, 55 NRD (0 in the game); lantern-lake: about 1,000 with no denoiser | none + NRD | ov-glow-lake.jpg, ov-lantern-lake.jpg (maps) | row 54 / PT-34 |
| 93 | Grass at the cave lake flickers hard under one lantern | lantern-lake | flicker 2.42 (the game 0.07: 35x), single pixels up to 66 to 76 levels, blocks wander up to 14 to 24 | none (the recipe has no NRD step) | lantern-lake-grass.jpg, ov-lantern-lake.jpg | PT-80 (the owner's "incredible flicker in grass") |
| 94 | A lantern over the lake throws a wide soft glow ONTO the water, as on a matte floor | lantern-lake | to the eye; the crop is x1.73 | none | lantern-lake-lamp.jpg | PT-81 / PT-48 (water as a milky surface) |
| 95 | The Caustics row changes nothing to see | lantern-lake | whole picture luma 19.8 on, 19.3 off (+2.6 %); the lit ledge +5 %; no rippling pattern | none | lantern-lake-lamp.jpg (2nd and 3rd tile) | PT-14, PT-80 |
| 96 | The lit rock ledge shows square texture blocks | lantern-lake | to the eye (blocks of about 8 pixels) | none | lantern-lake-lamp.jpg | PT-45 (a guess: a coarse texture level) |
| 97 | In a sea cave the water is a matte grey-white floor with a hard straight-edged shadow on it | water-torch | water x2.46 none, x2.52 NRD; whole picture x1.39; 25 to 28 % of the screen over twice the game's | both | water-torch-water.jpg, ov-water-torch.jpg | PT-81 |
| 98 | The cave's rock in the shade is at 0.6 and loses its detail | water-torch | x0.58; detail 1.85 none, 0.91 NRD, game 3.23 | both | ov-water-torch.jpg | PT-70 / PT-39 kind |
| 99 | With NRD one dot flashes by 100 levels on dark rock | water-torch | max 99.8 levels, its block wanders 13.9 | NRD | (numbers; at 0.13, 0.33) | row 54 |
| 100 | PATH TRACING SWITCHED ITSELF OFF in one scene: "Path tracing paused: 6 textures unavailable (18.9 MB)"; the game's picture was shown for both traced steps | metal | the game used 12.25 GB of video memory when Windows' budget fell to 13.52 GB (14.85 GB in every other scene); 13 textures "kept back near this card's video memory budget"; `step.json`: traced False, expected True, waited 51 s twice | - | the game's `BepInEx/LogOutput.log` lines 6326 to 6361; metal/2-traced-none/step.json | PT-73 (b) (said fixed by the memory job; it still happens) |
| 101 | The door strap's saw-tooth: four hard light and dark triangles on the iron strap | door-strap | strap x1.96 none, x2.28 NRD (blocks x11 to x13); black iron in the game | none | door-strap-strap-zoom2.jpg (shown x2) | PT-15 (still there) |
| 102 | With NRD the strap is a smooth glowing orange bar: its hammered surface is gone, its rivets are flat polygons | door-strap | detail 1.79 NRD, 3.54 none | NRD | door-strap-strap-zoom2.jpg | PT-19 / PT-75 kind; this look NEW |
| 103 | The door's room is lit from the left only: left wall up to 10 times brighter, the right post black | door-strap | 32 to 34 % of the screen under half the game's, 21 % over twice; red/green 2.22 against 1.22 | both | ov-door-strap.jpg | PT-73 kind |
| 104 | In dark rooms the floor is drawn in a few flat tones: flagstones and their joints are lost, with stepped patch edges | dark-room, boil-cage | a smooth patch: runs of one colour 6.3 pixels long with NRD (up to 213), 2.8 none, 1.4 in the game; 12 levels against the game's 28 | both, worse with NRD | dark-room-floor-x8.jpg (shown x8) | NEW |
| 105 | A stone stairwell lit through a door is far darker than the game's; its lower steps are nearly black | doorway | whole picture x0.42; walls x0.26, lower steps x0.18 (blocks x0.13); 67 to 69 % of the screen under half the game's | both | ov-doorway.jpg | PT-39 kind (the game lights it evenly; the owner's call how dark is right) |
| 106 | The daylight seen through the door is washed out to pale cream; cobbles lose their moss and colour | doorway | the opening x1.9 to x2.0; gain x2.58 on a view that holds daylight | both | doorway-arch.jpg | PT-73 (e) kind (the gain on bright parts); outdoors through a door NEW |
| 107 | In the crystal cave NRD makes the ivy on the lit wall flash as whole leaves: worse than no denoiser | glow | on the wall: boil 1.19 NRD (up to 7.6) against 0.50 none; flicker p95 8.7 against 7.7; 580 flashing dots with NRD, 222 none, 8 in the game; whole picture NRD 0.74 (the highest NRD figure of the run) | NRD | glow-crystals.jpg (right map), ov-glow.jpg | PT-34; "NRD worse than none" NEW (rows 10, 32 are the same thing) |
| 108 | The cave's lit wall and grass are 1.4 to 1.9 times brighter and flicker hard with no denoiser | glow | wall x1.89, flicker 3.34; grass x1.44, flicker 3.33 (the game 0.08) | none | glow-crystals.jpg | PT-34, PT-80 |
| 109 | The cave's shore is dark and its wet rock, puddles and small lights are gone; a creature in front is a dark shape | glow | shore x0.48 none, x0.45 NRD (blocks x0.04); detail 0.37 NRD, 1.12 none, 2.79 game | both | glow-shore.jpg (shown x2) | PT-48; the creature: PT-73 kind (camera light) |
| 110 | With NRD the creature and a cloak are flat facets: the model's triangles show | glow, night | to the eye | NRD (faint with none) | glow-shore.jpg, night-person.jpg | PT-04 (stepped look); as bad as this with NRD: NEW |
| 111 | With NRD cloth loses its weave and a mask its fine wire pattern | night | person: detail 1.61 NRD, 3.29 none, 2.90 game | NRD | night-person.jpg | PT-19 |
| 112 | The ground round a torch is half again as bright, burnt to white at the torch | night | x1.52, peak 254 (the game 240) | both | ov-night.jpg | PT-38 (the torch's light is back; now strong) |
| 113 | A stained-glass rose window is gone: a plain blue glow where the window is | stairwell | the window fills about 60 % of a 768x734 crop in the game, 0 traced | both | stairwell-window.jpg (shown x2) | NEW (PT-76 is about tint; here the tracery and the glass are both missing) |
| 114 | A statue under the window is a black column: no face, no folds, its glowing heart a faint dot | stairwell | to the eye | both | stairwell-statue.jpg (shown x3) | NEW (the kind: PT-73, lights left out) |
| 115 | A ghost (a see-through glowing person) is all but invisible: a faint grey shade | stairwell | to the eye | both | stairwell-statue.jpg | NEW (PT-71's list of what the tracer does not draw yet, a guess) |
| 116 | The wall torches' light is 2.3 to 6 times the game's; the stairs lose their stone texture | stairwell | by a torch x2.29 none, x2.41 NRD (blocks x5.5 to x6.2); whole picture x0.94 | both | ov-stairwell.jpg | PT-73 (e) kind |
| 117 | By a cooking fire NRD makes the grass flash white: far worse than no denoiser | campfire | by the fire: flicker p95 10.95 NRD against 5.19 none; blocks wander up to 21.6 against 5.4; cells 11.4 against 5.8; 242 flashing dots (86 none, 3 game) | NRD | campfire-cauldron.jpg (right map), ov-campfire.jpg | NEW (with rows 10, 32, 107: NRD worse than none on lit plants) |
| 118 | Wet gravel is a smooth tan sheet, like poured mud | campfire | detail by the fire 4.57 none, 3.45 NRD, 7.77 game | both | campfire-cauldron.jpg | NEW (with row 8 and row 70: fine ground relief and wet glints are lost) |
| 119 | Ivy on a wall is black; green in the game | campfire | x0.74; detail 2.4 against 4.8 | both | ov-campfire.jpg | PT-03 kind |
| 120 | A rock by the fire carries a black scribble pattern with NRD | campfire | to the eye | NRD | campfire-cauldron.jpg | NEW (small) |
| 121 | In dim rooms NRD's picture has up to twice the blue and less red than the undenoised one | hair-eyebrows, hair-moving, tavern-room, glass-jars, candle | NRD light / undenoised light, R G B: hair-eyebrows 1.16, 1.40, 1.95; hair-moving 0.86, 1.08, 1.73; tavern-room 0.84, 0.99, 1.23; 97 to 99 % of blocks differ by over 10 % | NRD | section 2b's table; the run's `numbers.txt` agrees to the second digit | PT-75 |
| 122 | Outdoors NRD loses 5 to 11 % of the light (the gains are equal in both steps) | vista, campfire, water-day, caustics-glass | vista 0.91, 0.94, 0.94; campfire 0.90, 0.89, 0.89; water-day 0.95 | NRD | section 2b's table | PT-75 |
| 123 | (The harness, not the tracer:) in 3 scenes the view shifts by some pixels between the first and the last step, so "is the game's picture back" cannot be checked pixel on pixel there | hair-eyebrows, glow, metal | 8 to 27 % of pixels off by over 8 levels; mean colour the same to 0.1 level; `cameraMoved: False` | - | after-glow.jpg, after-hair-eyebrows.jpg | PT-26 (harness): NEW |
<!-- TABLE END -->

## 2. Scene by scene

### forest (run 20261008-124542; rain, overcast; leaves and grass)

What it is: a camp in a wood in the rain: grass in front, a pine, a rock on the left, a palisade with a brazier.
Whole picture: brightness x1.05 none, x1.03 NRD. Whole-frame flicker 1.03 none, 0.71 NRD, game 0.14.

What is right: the shapes and the layout match the game. The fog's brightness in the far backdrop matches (165 against
165). The blue firefly particles and the rain streaks are there. The pillar and the far cliff are close in brightness
(x0.96). With NRD the rock, the far backdrop and the cliff are steady (flicker 0.18 to 0.26, the game's 0.17 to 0.22).

Defects:

1. GRASS FLICKERS, WITH AND WITHOUT NRD (rows 1, 2). `forest-grass.jpg` (box 0.25,0.62 to 0.41,0.88):
   flicker 1.37 none, 1.25 NRD, game 0.12. NRD's 95th percentile is HIGHER than none's (3.56 against 3.02). In the
   3x zoom (`forest-grass-zoom3.jpg`) the game's blades are smooth lines; the traced blades are rows of dots in a
   zig-zag, and NRD keeps the dots. NRD's flicker map shows whole single blades lighting up (up to 14.6 levels).
   Guess: one yes-or-no coverage decision a pixel a frame on the cut-out blades, and NRD treats each as a new surface.
2. GRASS COLOUR (row 3): olive and brighter (x1.18 to x1.28), the game's is darker and bluer. The game has near-black
   gaps between tufts; the traced has none: it looks flatter.
3. THE TRUNK BOILS (row 4). `forest-tree.jpg`, box on the trunk 0.452,0.19 to 0.462,0.28: blocks wander 2.10 levels
   none, 0.80 NRD, the game 0.03. It is the brightest line of the whole screen in `forest-boil-nrd-map.jpg`.
4. THE TRUNK IS RED-BROWN (row 5). `forest-leaves-zoom3.jpg`: grey with bark in the game, a smooth rust colour traced.
   The upper trunk also loses its bark relief. Guess: light bounced off backlit leaves (PT-77), or the missing wet look.
5. THE CANOPY (row 6): every leaf clump carries grain with no denoiser (flicker 1.09); NRD brings it to 0.40, still 3
   times the game's. The clumps are rounder and lighter than the game's fine needle sprays (a coarser tree model? a
   guess).
6. ORANGE STAKES (row 7). `forest-stakes.jpg`: the palisade behind the brazier is dark red-brown in the game and light
   orange traced (x1.27). The brazier's light on them has no falloff to the eye: the far stakes are as bright as the
   near ones.
7. NOTHING IS WET (row 8). It rains. The game's rock glitters with small white glints and is dark and green; the
   traced rock is matte grey with no glint. The path by the fence is shiny mud with puddles in the game and flat
   red-brown dirt traced. The barricade's wood is dark and wet in the game, light grey and dry traced.
8. NRD SMEARS THE ROCK (row 9). `forest-rock.jpg`, right tile: the rock becomes smooth grey patches with hard dark
   edges, like a watercolour. Detail 1.49 against 5.37 with no denoiser.
9. THE GRASS'S FAR EDGE BOILS WITH NRD (row 10). `forest-ridge.jpg`: where the meadow ends against the fog, NRD's
   blocks wander 0.77 on average and up to 4.1 levels; with no denoiser 0.42. NRD makes this place worse.
10. ORANGE SPECKLES IN FAR PLANTS (row 11). Same crop: the far meadow is sprinkled with orange dots; the game's is
    plain green. The ferns by the hut (`forest-lantern-right.jpg`) are orange-brown where the game's are green.
11. THE FAR LANTERN (row 12). `forest-lantern-right.jpg`: a small flame in the game. Traced with no denoiser it is a
    lit patch that pulses (flicker 4.0 on average over the patch, 33.7 at worst). With NRD it is a round glowing ball,
    brighter than with no denoiser (peak 246 against 171) and the hut's wall and roof are lit yellow. So NRD adds
    light here; elsewhere (PT-75) it loses light.
12. STAIR-STEP EDGES (row 13): the poles' and stakes' outlines are steps of whole pixels in both traced steps; the
    game's are smooth.
13. THE FAR PERSON (row 14). `forest-npc-zoom.jpg`: with no denoiser her skin is covered in red and white dots, with
    NRD her face is a featureless orange shape; her bracers are flat white blocks in both (silver with shading in the
    game). She is about 200 pixels tall: a player sees this on every person at a distance.
14. FAR STRUCTURES LOST IN FOG (row 15). Same crop: behind her the game shows a wooden frame with rope lashings,
    dark green through the fog. Traced: smooth fog with two faint reddish posts and a pale wedge with stair-step
    edges. The same in `forest-lantern-right.jpg` (the tower left of the hut).
15. Smaller: the rain's splash rings on the ground are fainter than the game's (`forest-grass.jpg`). The grass round
    the brazier is a soft mass with no single tufts (`forest-stakes.jpg`).

### vista (run 20261008-124542; a dusty day, sun, haze)

What it is: a road with a cart on the left, a shield on a wheel on the right, a town and a castle in haze.
Whole picture: x1.05 none, x0.98 NRD. Whole-frame flicker 0.99 none, 0.24 NRD, game 0.09. NRD's boil 0.063 (game 0.019).

What is right: the sky, the haze, the castle and the far town match (x0.99 to x1.02; flicker with NRD 0.12). The far
bare tree by the town is right. The ground has its texture and the shield its wood (both were missing on 10-07).
With NRD most of the picture is steady.

Defects:

1. THE PURPLE TREE HAS NO LEAVES (row 16). `vista-purpletree.jpg`: in the game the tree in the middle carries a wide
   lilac canopy. Traced, with and without NRD, there are only bare pale branches and a few dark brown tufts on top.
   A player sees a dead tree where a blossoming one stands. This is the scene's measured region.
2. THE TREE ON THE CLIFF (row 17). `vista-cliff-tree.jpg`: thin dark twigs in the game; traced it is covered in bright
   yellow-green dots, as if in leaf. It flickers: 1.32 none, 0.77 with NRD (the game 0.17).
3. A GLOWING BUSH (row 18). Same crop, at the tree's foot: a yellow-white blob (peak 238) where the game has a dull
   yellow plant (169). Guess: backlit leaves sending on more light than reaches them (PT-77).
4. THE GROUND (row 19). `vista-ground.jpg`: the game's dirt is dark, with small shadows under each pebble. With no
   denoiser it is a third brighter, greyer and flat (no small shadows). With NRD it is dark red-brown: NRD shows
   0.68 of the undenoised light and shifts it to red.
5. NRD RECOLOURS METAL AND WOOD (rows 20, 21). `vista-shield.jpg`: the shield's rim is grey metal in the game and with
   no denoiser; with NRD it is smooth orange-brown, and the boss loses its hammered surface. `vista-cart.jpg`: the
   cart's metal band is dark bronze in the game, light grey with no denoiser, flat tan with NRD; the wood's fine
   grain is gone with NRD (detail 4.56 against 9.37).
6. ORANGE WOOD (row 22): the cart's boards are grey-purple and weathered in the game, orange-tan traced.
7. GRASS BY THE SHIELD (row 23): the blades are neon yellow rows of dots (smooth green-yellow blades in the game).
   With NRD single blades flash by up to 30 levels: the strongest flicker of the whole screen sits there.
8. Smaller: a thin pale line across the ground is a row of dashes (`vista-cart.jpg`, row 24). The balustrade's
   balusters have stair-step edges and lose the dark shade under the rail (`vista-town.jpg`).

9. BLACK UNDER THE CART (row 25). `vista-corner-left.jpg`: between the wheel's spokes the game shows boards and ground
   in brown shade. Traced it is black with a blue tint (0.11 to 0.21 of the game's brightness). In sunlight a real
   shade is dark, not black. The wheel's iron tyre is dark bronze in the game, speckled light grey with no denoiser
   and smooth orange-gold with NRD: three different metals.

### candle (run 20261008-124542; an outdoor corner by day with candles; PT-06, PT-39, PT-40)

What it is: a stone corner in daylight, a group of lit candles, a barrel, ivy at the top left.
Whole picture: x0.49 none, x0.51 NRD (luma 42 and 43 against 85). Red/green 1.45 against 1.19: much more orange.
Whole-frame flicker 1.22 none, 0.18 NRD, game 0.19. NRD's boil 0.076 (game 0.036).

What is right: the flame rectangles are gone (the change map has none; the game's own picture still has them: 0.62
in the region). The flames, their purple base and the wicks match. With NRD the picture is as steady as the game's.
The wax is white again, not brown.

Defects:

1. THE CORNER IS DARK AND ORANGE (row 26). The game's corner is in grey daylight. Traced, the candles are the only
   light: the left wall is at x0.69, the barrel at x0.51, the wall on the right at x0.14 and red (13 against 90).
   55 % of the screen is under half the game's brightness. The game's wall is at 90 in daylight and nothing of that
   daylight arrives.
2. WAX WITH NO DENOISER (row 27). `candle-wax-zoom3.jpg`: every candle's body is orange and blue dots. Flicker 8.8
   levels on average (the game's 0.4). Even the mean of 16 frames is still dotted. It is the brightest thing in the
   run's change map. Guess: the scattering walk's noise (the same as the red dots on skin).
3. WAX WITH NRD (row 28): clean, but pink-white and matte. The game's wax is ivory, with a glossy streak down the
   tall candle. The candles' outlines are steps of whole pixels at the top.
4. A GRID OVER NRD'S PICTURE (row 29). `candle-stone-zoom4.jpg`, right tiles: thin lines every second pixel, both
   ways, like cloth. Measured: neighbouring columns flip sign 95 % of the time with NRD on that stone (40 % with no
   denoiser, 34 % in the game), amplitude 1.4 levels. Over the whole screen 62 % of the 64-pixel blocks show it in
   this scene, 30 % in the tavern, 8 to 9 % outdoors, 0 to 2 % with no denoiser. It is visible at 1:1 on flat dim
   surfaces. Guess: something on NRD's path is made at half size or in a checker pattern and put back unfiltered.
5. STONE WITHOUT MORTAR (row 30). Same crop: the game's stones have pale mortar and lichen in the seams and a rough
   face. Traced, the seams are dark and the face is half as detailed (2.1 against 4.9).
6. THE WALL BEHIND (row 31). `candle-plants.jpg`: behind the ivy the game shows a grey block wall with wet glints.
   Traced there is a flat grey-green haze with faint lines.
7. LEAVES PULSE WITH NRD (row 32). Same crop, the flicker map on the right: one cluster of ivy leaves lights up as
   whole leaves with NRD (boil up to 3.2 levels); with no denoiser it is calm there. NRD also blurs these leaves
   into soft blobs. It is the strongest flicker and boil spot of NRD's picture in this scene (0.04, 0.05).

### hair-moving (run 20261008-124542; Ron's Tavern, the hero in front of the camera)

What it is: the tavern's dark room, the hero's head and chest close to the camera, a table with a person and a
candle behind, an alcove with a candle on the right.
Whole picture: x1.03 none, x1.10 NRD, but the light is spread differently: 14 % of the screen is under half the
game's brightness (the hero), 23 % over twice (ceiling, walls). Red/green 2.00 against 1.37. Flicker 0.69 none, 0.19
NRD, game 0.04. NRD's boil 0.086 (game 0.010). Note: the hero's head is turned differently in the game's step and the
traced steps (her idle pose is not frozen the same), so shapes on her do not line up pixel on pixel.

What is right: the room's shapes and textures are there; the floor's brightness matches (x0.95). With NRD the walls
and floor are steady. Bottles on the table have their colours.

Defects:

1. THE HERO IS DARK AND ORANGE, THE ROOM BRIGHT (row 33). `tavern-hero.jpg`: her face and chest are at half the
   game's brightness and orange-red; her silver armour is dark bronze. The ceiling and the alcove's wall are 2 to 8
   times brighter than the game's. A player looks at the darkest thing in the room when looking at their character.
2. BLACK EYES (row 34). `tavern-face-zoom2.jpg`: the game shows the whites, the iris and the lashes. Traced, each eye
   is a black slit with black blocks at its corners. The eyebrows are faint smudges.
3. SKIN (rows 35, 36): with no denoiser a dense red grain, with a ragged dotted shadow edge down the cheek. With NRD
   smooth like plastic, no blush or pores, and the shadow down the nose and along the jaw is a hard line in whole-pixel
   steps.
4. HAIR (row 37): darker than the game's, in strings with black gaps. With NRD the strands on the left of the head
   flash (the right flicker map of `tavern-face-zoom2.jpg`): 1.05 levels on average, single strands up to 60.
   Thin black arcs stand outside the hair's outline (`tavern-hero.jpg`).
5. THE FEATHER CREST (row 38): a row of hard-edged white blocks. In the game it is one smooth, half see-through
   feather.
6. ARMOUR WITH NRD (row 39): about ten white dots flash on the ribs and pauldrons (up to 87 levels: the strongest
   flicker of the scene). The ribs' highlights are flat white blocks with stair-step edges. The pauldrons' scales are
   gone (row 40).
7. CANDLES (row 41). `tavern-table.jpg`, `tavern-alcove.jpg`: with no denoiser a candle's wax is dots (as in the
   candle scene); with NRD each candle is a round glowing ball several times the candle's width, brighter than in
   the undenoised picture.
8. NRD AGAINST NO DENOISER (row 42): at the far table NRD's picture is 38 % brighter and less red than the undenoised
   one. The tablecloth's net pattern and the table's wood grain are wiped (row 40).
9. The far person's face is covered in black dots with no denoiser, a smooth orange face with NRD (row 43).
10. THE GRID (row 29): 30 % of the screen's blocks show the every-second-pixel pattern with NRD.

## THE FULL RUN: `20261008-130029` (from here on; my own overview sheets are `ov-<scene>.jpg`: the three mean pictures, a map of traced against game brightness, and the two flicker maps)

### npc-day (full run; a rainy day outdoors: rocks, a fence, a laundry pole, pots)

Note: the scene is meant to show a face up close in daylight. No person is in the picture (the save's view has none
now). So daylight skin, eyes and hair could not be judged here.
Whole picture: x1.08 none, x1.07 NRD. Flicker 0.74 none, 0.44 NRD, game 0.10. NRD's boil 0.113 (game 0.030).

What is right: the sky, fog, far trees, rocks' shapes and the pots' colour are close. The laundry pole is steady.

Defects:

1. PLANTS MISSING (row 44). `npcday-rightground.jpg`: in front of the fence the game has knee-high grass, a cabbage and
   broad-leaf plants. Traced there is bare grey ground with a little short grass behind the rail. Both traced steps.
   In `ov-npc-day.jpg` the red area at the right is this.
2. THE FIR TREE (row 45). `npcday-tree.jpg`: dark green in the game. Traced it has orange leaves and several white
   blobs inside the crown, like snow. With no denoiser they also sparkle.
3. GRASS FLICKER (row 46): box 0.50,0.70 to 0.66,0.96: 1.97 none, 1.93 with NRD. NRD's worst pixels are worse than
   without (p95 5.25 against 4.34). The grass is also x1.31 brighter than the game's.
4. The pots and the ground by them are x1.34 brighter; the fence's rails have stair-step edges. With NRD the laundry
   on the pole has one block that wanders 4.9 levels.

### hair-eyebrows (full run; Ron behind his bar, a candle on the bar; PT-01, PT-11, PT-18)

Whole picture: x1.87 none, x2.13 NRD (luma 19 and 22 against 10); 48 to 55 % of the screen is over twice the game's
brightness. Red/green 1.93 against 1.21. Flicker 0.93 none, 0.17 NRD, game 0.03. NRD's boil 0.076 (game 0.007).

What is right: Ron's eyes show an iris (green in the game, yellow-green traced). Barrels, shelves and bottles are
there with their shapes. With NRD the room is steady.

Defects:

1. THE ROOM IS TWICE AS BRIGHT AND ORANGE (row 48). The game's room is dark and blue-grey; traced it glows orange
   from the candle. Ron's hair is a saturated orange-red (red/green 2.67 against 1.90).
2. THE CANDLE WITH NRD (row 47). `hair-eyebrows-bar.jpg`: with no denoiser the wax is dots; with NRD it is one
   burnt-out white shape with a glow as wide as the candle is tall, and the bar beside it is twice as bright as
   without a denoiser (x3.27 against x1.64). So NRD doubles the light there.
3. EYEBROWS AND EYES (row 49). `hair-eyebrows-head-zoom2.jpg`: the game shows ginger eyebrows as hair. Traced they are
   a faint dark smudge. With NRD the eyes' corners and lashes are black square blocks.
4. HAIR WITH NRD (row 50): one flat orange mass with horizontal block streaks; the strands are gone.
5. SKIN: grain and dark red blotches round the eyes with no denoiser; smooth with NRD.
6. With no denoiser the bar's top flickers most of all (5.9 levels in a 96-pixel cell, boil 1.95), and Ron's shirt.

### hair-glowing (full run; a man by a post on the tavern's gallery, a chandelier behind; PT-54)

Whole picture: x1.87 none, x1.78 NRD. 16 % of the screen is under half the game's brightness (the man and the
post), 51 % over twice (the roof, up to x20). Flicker 0.69 none, 0.18 NRD, game 0.03.

Defects:

1. THE MAN IS A BLACK SHAPE (row 51). `hair-glowing-npc.jpg`: his face, chest and shirt are at 0.21 of the game's
   brightness (luma 6 against 30), dark red. Only the top of his hair is lit, orange: that is the "glow on top".
   With NRD the lit hair flickers along its outline.
2. The roof and the wall behind him are 2 to 20 times brighter than the game's (which is black there).
3. The post in front of the camera is black where the game's is brown.

### eyebrows-flickering (full run; an old man sitting in a wooden room; PT-55)

Whole picture: x0.70 none, x0.65 NRD. 22 % of the screen under half the game's brightness. Flicker 0.76 none, 0.17
NRD, game 0.05. NRD's boil 0.078 (game 0.011).

Defects:

1. THE FACE IS NEARLY BLACK (row 52). `eyebrows-flickering-head-zoom2.jpg`: the game shows a lit face with grey hair,
   green eyes and eyebrows of single hairs. With no denoiser the face is a dark red grain at 0.13 of the game's
   brightness and the hair cannot be told from the wall. With NRD the face is a dark orange (x0.25) and smooth.
2. HAIR FLICKERS WITH NRD (row 53). Same sheet, the right flicker map: the whole head of hair lights up in streaks
   (1.04 levels on average over the head crop, the game's 0.07; NRD's boil is twice the undenoised picture's). The
   eyebrows themselves show only as a faint line in that map: today the hair is the worse flicker, not the eyebrows.
3. WHITE DOTS ON THE WALLS WITH NRD (row 54). `ov-eyebrows-flickering.jpg`, the right map: about 46 single dots that
   flash by more than 10 levels in otherwise calm wall (26 with no denoiser, 1 in the game).
4. A black shape fills the bottom left corner (x0.25 of the game's): something close to the camera that the game
   lights and the tracer does not.

### npc-candle (full run; a chapel at night, a bald man by candles, a well with a blue whirl)

Whole picture: x1.71 none, x1.80 NRD. Flicker 0.63 none, 0.15 NRD, game 0.04. NRD's boil 0.065 (game 0.010).

What is right: the man by candle light looks good with NRD: warm light from the candles' side, the red veins on his
head, the shirt's cloth. The flames are mirrored in the well. This is the best face of the run.

Defects:

1. THE WINDOWS (row 57). `npc-candle-window.jpg`, `npc-candle-man.jpg`: the game's tall windows are filled with a
   diamond lattice of lead and glass. Traced, the upper window is an open hole: night sky and a lit tree branch.
   Only at the bottom, in front of a rock outside, a lattice shows.
2. NRD SMEARS WHAT IS BEHIND THE WINDOW (row 58): the branch and the rock outside become flat pale shapes with a
   ghost of the lattice printed on them.
3. THE WELL'S WATER (row 59). `npc-candle-well.jpg`: dark, nearly black water in the game with a blue whirl in the
   middle. Traced the whole surface is a milky pale blue, with grain; the whirl's glow is spread wide.
4. With NRD the two flame reflections in the water are soft blobs; with no denoiser they are sharp streaks (row 60).
5. Candles: the wax is dots with no denoiser (flicker 11.7 in the worst cell) and burnt white with NRD; the floor by
   them is x1.79 with NRD against x1.18 without (row 47 again).
6. Small things (row 61): black blocks at the man's sleeve seam, black dots inside his ear with no denoiser. A
   straight hard shadow edge runs down his skull.

### tavern-room (full run; the tavern's main room: many lights, people at a distance)

Whole picture: x1.41 none, x1.40 NRD; red/green 1.65 against 1.41. Flicker 0.94 none, 0.21 NRD, game 0.05. NRD's
boil 0.085 (game 0.010). With NRD 42 % of the screen's blocks show the every-second-pixel grid (row 29).

Defects:

1. LIGHT IN THE WRONG PLACES (row 55). `ov-tavern-room.jpg`, the map at the bottom left: the ceiling and the gallery
   are 2 to 8 times brighter than the game's; the floor in the middle is at half (blocks at a quarter). The game
   lights its floor; the tracer lights its roof.
2. WHITE DOTS WITH NRD (row 54): 140 flashing dots over the screen (73 with no denoiser, 20 in the game). They sit on
   walls, tables and the floor.
3. CANDLES ARE GLOWING BALLS WITH NRD (row 47). `tavern-room-dots-zoom3.jpg`: the candle on the table is a white
   shape with a halo; at that table NRD's picture is x1.49 against x1.11 without.
4. THE TABLECLOTH (row 56). Same sheet: the game's cloth has a fine white net and a lace edge. With no denoiser it
   is ragged green blocks; with NRD a faint orange net. The mug and the cup are grain with no denoiser.
5. NRD takes the floor's detail: 0.80 against 1.93 with no denoiser (2.76 in the game).
6. The fireplace's wall is x1.35 to x1.47.

### A finding about every scene: the picture's size (row 62)

Every step's `step.json` says `renderWidth 1924, renderHeight 1084` on a 3840x2160 screen (DLSS Performance). The
game's own picture is made at that size too, and its upscaler turns it into a smooth 4K picture. The traced picture
with Denoising None or NRD is not smoothed: poles, stakes, chair slats, candle tops and cloth edges are stairs of
2-pixel steps, and things thinner than 2 pixels (grass blades, a rope, a feather's barbs, lace) are rows of dots.
I tested whether it is a plain doubling of pixels: it is not (neighbouring pixels repeat no more often in even
columns than in odd ones), so something does resample it, without smoothing edges. A guess at the cause: the
upscaler gets no sub-pixel shift from the tracer's camera rays, so it has nothing to build smooth edges from.

### mirror-tavern (full run; a perfect mirror 1.6 m ahead in the tavern; PT-46)

Numbers (my `mir.py`, the mirror's inside, 1353x1015 pixels), against the tracer's own view from the mirror's side:
no denoiser: brightness x0.95, shapes 0.89, detail 1.00. NRD: brightness x0.89, shapes 0.92, detail 0.05.
Against the game's view from there: brightness x1.97, shapes 0.52 (the room is lit otherwise, as in every tavern scene).

What is right: with no denoiser the mirror shows what the tracer shows directly from the mirror's side: the wall,
the nails, the leather cuirass, at the same sharpness.

Defects:

1. NRD BLURS THE MIRROR AWAY (row 63). `mir-mirror-tavern-centre.jpg`, fourth tile: a soft brown smear where the
   wall and the cuirass are. Detail 0.05.
2. THE BOIL INSIDE THE MIRROR STAYS WITH NRD (row 65): 0.30 against 0.31 without a denoiser.
3. GLOSS DIFFERS (row 64). Third tile against the second: in the mirror the cuirass has bright orange glints and the
   nail heads shine; in the direct view both are matte.
4. A note, not the tracer's fault alone: the body in the mirror has no head (the game hides the first-person head, in
   its own view from the mirror's side too). A real mirror in the game would show a headless hero.

### mirror-fog (full run; the foggy night by a fire, a mirror 1.6 m ahead)

Against the tracer's own view from the mirror's side: no denoiser: brightness x1.61 raw (the steps' gains are 1.53
and 1.40, so about x1.76 with them taken out), shapes 0.85, detail 0.80. NRD: detail 0.14.

Defects:

1. THE MIRROR'S NIGHT IS GREY (row 66). `mir-mirror-fog-whole.jpg`: seen directly the scene is dark and warm, with
   orange embers in the air and the fire's glow on the cuirass. In the mirror it is a grey haze, brighter, the cuirass
   steel-blue, and there are no embers. So fog is thicker in a mirror, and the fire's particles are not in it.
2. NRD blurs it (detail 0.14), as in the tavern.

### mirror-water (full run; a beach by day with a stranded ship, a mirror 1.6 m ahead turned 55 degrees)

Against the tracer's own view from the mirror's side: no denoiser: brightness x0.92, shapes 0.97, detail 0.93: the
mirror itself is right here. NRD: detail 0.08 (row 63 again). The scene's own defects show in the direct views
(`ov-mirror-water.jpg`: the game's view, the tracer's view of the same, and the main view with the mirror):

1. THE SEA (row 68): dark green water with waves and white foam in the game. Traced: one flat milky beige sheet
   from the shore to the horizon, like cloud lying on the water; the horizon is gone. It is 2.9 times as bright and
   has no detail at all (0.25 against 3.55).
2. THE SEA FLICKERS (row 69): with no denoiser the whole sea is white in the flicker map: 5.27 levels on average,
   blocks wander 1.66. NRD calms it to 0.27 (the game's 0.06).
3. THE SAND (row 70). `mir-mirror-water-centre.jpg`, first tile against the second: the game's sand is covered in
   wind ripples with dark shade between them. Traced the sand is flat and even.
4. The rocks are paler than the game's, as in the forest.

### mirror-cave (full run; the mineral cave's dark side in a mirror)

Against the tracer's own view from the mirror's side: no denoiser: brightness x0.78, shapes 0.86, detail 0.90. NRD:
x0.98, detail 0.13. So the mirror follows the direct view; with NRD it is blurred (row 63).

1. THE CAVE'S DARK SIDE IS BLACK (row 71). `ov-mirror-cave.jpg`, first two tiles: the game shows mossy walls, a lake
   and grass in dim blue-green light. The tracer's own view of the same is nearly black: x0.29 on average (x0.07 at
   the bottom), 32 % of the pixels under level 4. Only a blue haze and one small lamp show.
2. A STAR FIELD OF DOTS WITH NRD (row 54). The flicker map of that view (it is taken with NRD) is sprinkled with
   single flashing dots over the whole cave wall: 119 of them.

### boil-cage (full run; a crypt tower, an iron cage on a gallows; PT-09)

Whole picture: x0.82 none, x0.85 NRD. Flicker 0.16 none, 0.06 NRD, game 0.03. NRD's boil 0.026 (game 0.009).

What is right: THE THIN BARS DO NOT BOIL in this run. The cage is calm with NRD (flicker 0.10, boil 0.05 on the cage;
0 flashing dots). This is the calmest traced scene of the run.

Defects:

1. THE LIGHT IS ELSEWHERE (row 72). `ov-boil-cage.jpg`: the game shows the brick wall and the bench on the left; traced
   they are black (x0.36, blocks at x0.15). The wall behind the cage is lit warm, with the gallows' hard shadow on it,
   where the game has a dim blue-grey wall in haze (`boil-cage-cage.jpg`, shown 4 times brighter).
2. NRD softens the bricks behind the cage (detail halved, row 73) and the bars get a soft dark fringe.
3. A small thing I could not judge: both traced flicker maps show faint rings across the dark picture (one-level
   steps of a smooth gradient that shift from frame to frame). On a dark screen one level is an 8 % step. I did not
   check whether the game's own picture has the same rings.

### glass-jars (full run; the lantern room: a lantern on a bench, a shelf with bottles)

Whole picture: x1.37 none, x1.61 NRD; 40 to 48 % of the screen over twice the game's brightness, 11 to 18 % under
half. Red/green 2.11 against 1.63. Flicker 1.13 none, 0.17 NRD, game 0.03. NRD's boil 0.084 (game 0.007).

What is right: the shelf's bottles have their colours and labels (the red jug, the cider bottle's label). With NRD
the room is steady.

Defects:

1. THE LANTERN'S SURROUND IS WASHED OUT (row 74). `glass-jars-lantern.jpg`: the wall and the bench round the lantern
   are pale peach, 2.5 to 2.9 times the game's brightness, and their wood texture is nearly gone with NRD (row 78).
   The far left wall, black in the game, is 9 to 11 times brighter. The owner chose this hold; the number is here so
   it can be held against the next build.
2. THE LANTERN'S GLASS (row 75): milky white panes. In the game the glass is clear and the flame shows as a flame.
3. JAGGED STEPS ON THE PLANK (row 76). `glass-jars-plank-zoom3.jpg`, no denoiser: the plank's top is covered in rows
   of dark zig-zag steps, like terraces. The game's plank has fine cracks and pits.
   Guess: the wood's bump map shading itself under a light that grazes it, at 2-pixel steps.
4. THE CANDLESTICK (row 77). Same sheet: with no denoiser its foot is grainy and the plank's pattern shows through
   it. With NRD it is a shape of flat facets with hard edges and three black dots. A solid dark iron thing in the game.
5. THE SHELF (row 79). `glass-jars-shelf.jpg`: the small white bottle is brown grain with no denoiser and white with
   NRD (the two pictures should agree). The small barrel between the cups and the cider bottle is nearly black.
   The cider bottle's turquoise is duller.
6. THE CHAIR AND THE FLOOR (row 80): half as bright as the game's and red. The game lights them; the tracer's light
   goes to the wall.
7. With NRD 65 single dots flash (9 in the game); 33 % of the screen shows the grid (row 29).

### candle (full run): the same picture as run 124542

Its numbers repeat to the second digit (x0.49 none, x0.51 NRD; wax flicker cells 13.4; the grid on 59 % of the
blocks with NRD; the ivy's boil 0.96). Nothing new; see the candle section above.

### refract-jar (full run; a cellar: glass jars on a shelf in front of a stone wall; PT-35)

Whole picture: x1.26 both. Flicker 0.87 none, 0.20 NRD, game 0.06. NRD's boil 0.097 (game 0.014). With NRD 49 % of
the screen shows the grid (row 29).

Defects:

1. THROUGH THE JARS WITH NRD (row 84). `refract-jar-jars.jpg`: with NRD the inside of each jar is brown mud; nothing
   of the wall behind can be made out. A patch in the right jar boils: blocks wander up to 15 levels (the flicker map
   on the right shows it as a white cloud), and dots flash on the left jar's edge. With no denoiser the wall shows
   through, bent and grainy.
2. THE JARS' LOOK (row 85): cloudy glass with dark blotches (the dirt on the glass reads as solid stains). In the
   game the jars are clear, with a thin bright rim, and the wall behind is easy to read. A real jar bends what is
   behind it; a real jar is not this murky.
3. LIGHT FROM ONE SIDE (row 86). `ov-refract-jar.jpg`: the game's cellar is evenly dim. Traced the right is up to 6.8
   times brighter and the left at half or less. The stones show deep dark pits and lose their fine speckle.

### caustics-glass (full run; a deck by a brazier, two bottles on a table)

Whole picture: x0.99 none, x0.97 NRD; red/green 1.94 against 1.64. Flicker 1.36 none, 0.22 NRD, game 0.07.

What is right: this is the best-matching scene of the run: brightness, layout, the bottles' labels and the planks
are right, and with NRD it is clean. The snow at the top left matches (x0.99 to x1.00).

Defects (small):

1. The standing bottle is dark red, x0.75 (row 87). No bright spot of focused light is in its shadow; the bottle is
   painted, not see-through, so none is owed here. The scene cannot show glass caustics.
2. Black rectangles at plank gaps with no denoiser (row 88).
3. With NRD 121 dots flash, most on a table top at the top of the picture (row 89).
4. The picture is more orange than the game's.

### black-spots: did not run (row 90). Its save "PT black-spots" is not among the saves. Could not judge PT-27.

### water-day (full run; a beach by day; PT-81, PT-14, PT-42)

Whole picture: x1.78 none, x1.74 NRD; 44 to 46 % of the screen over twice the game's brightness. Flicker 3.23 none
(the highest of the whole run), 0.25 NRD, game 0.08. NRD's boil 0.129 (game 0.017).

Defects:

1. THE SEA IS A MILKY SHEET (rows 68, 81). `ov-water-day.jpg`: no waves, no foam, no dark green, no horizon. In the
   shallows the game shows the sand's ripples and foam lines through the water; traced the bed cannot be seen
   (`water-day-shadow.jpg`). The waterline is a clean curve; the game's shore has foam running up the sand.
2. THE SEA FLICKERS with no denoiser: 5.5 levels on the water, blocks wander 1.8 (row 82). With NRD it is calm
   (0.3), with about 30 flashing dots (row 83).
3. THE SAND is flat: no wind ripples, no wet band by the water (row 70; PT-52 is the wet band).
4. The player's own shadow lies on the sand in the traced picture and not in the game's. That is right for a traced
   picture; it is the shadow of a body without a head (see mirror-tavern).
5. No rippling light on the bed or thrown up from the water can be judged: the water shows nothing of either.

### glow-lake (full run; the mineral cave's lake; PT-34, PT-48)

Whole picture: x0.39 none, x0.38 NRD. Flicker 0.14 none, 0.11 NRD, game 0.03.

1. NEARLY BLACK (row 91). `ov-glow-lake.jpg`: the game shows mossy rock, a ledge, the lake and reeds in dim blue-green
   light. Traced: a dark blue haze, the outline of the lake, one small red lamp. 79 % of the screen is under half the
   game's brightness.
2. DOTS (row 92): 254 single dots flash with no denoiser, 55 with NRD, over the cave's walls and the reeds.
3. The one lamp pulses with no denoiser (2.1 levels over a 96-pixel cell).

### lantern-lake (full run; the same lake with the harness's lantern over the water; PT-14; no NRD step)

Steps: the game, traced with everything on, the Caustics row off, glowing surfaces off, the world running. All
traced steps are with no denoiser. Whole picture: x0.87. Flicker 0.84 (frozen), 1.73 with the world running.

1. THE GRASS (row 93). `lantern-lake-grass.jpg`: lit by the lantern through its own blades, the reeds flicker 2.42
   levels on average (35 times the game's), single pixels by up to 76 levels. About 1,000 flashing dots over the
   picture. This is the owner's "incredible flicker in grass"; it is still there on this build.
2. A GLOW ON THE WATER (row 94). `lantern-lake-lamp.jpg`: under the lantern a wide soft orange oval lies on the
   lake, beside the lamp's own streak of a reflection. Clear dark water would show the reflection only.
3. CAUSTICS (row 95): with the Caustics row on and off the pictures are the same to the eye (+2.6 % of light
   overall, +5 % on the lit ledge). No moving or rippled light on the rock.
4. TEXTURE BLOCKS (row 96): the lit ledge is made of square blocks about 8 pixels wide.
5. The lantern is a plain white ball; the reeds' lit tips are hard yellow dashes.

### water-torch (full run; a sea cave by day, looking out)

Whole picture: x1.39 none, x1.43 NRD. Flicker 1.24 none, 0.22 NRD, game 0.07. NRD's boil 0.121 (game 0.012).

1. THE WATER IS A FLOOR (row 97). `water-torch-water.jpg`: dark green sea with white foam flecks in the game. Traced:
   a matte grey-white surface with the cave's shadow lying on it as on concrete, with ruler-straight edges, and a
   faint lighter four-sided patch at the right. No waves, no foam, no reflection of the rock or the sky.
2. With no denoiser the lit water flickers 2.1 (cells up to 5.0); with NRD it is calm.
3. THE ROCK IN THE SHADE (row 98): darker (x0.58) and smoother; NRD halves its detail again.
4. One dot on the dark rock flashes by 100 levels with NRD (row 99).

### door-strap (full run; a wooden door with an iron strap in a dim room; PT-15)

Whole picture: x0.89 none, x0.86 NRD. Flicker 0.61 none, 0.11 NRD, game 0.03.

1. THE SAW-TOOTH IS STILL THERE (row 101). `door-strap-strap-zoom2.jpg`: with no denoiser the strap carries four
   triangles with ruler-straight edges, light against dark, in a row. The game's strap is plain black iron.
2. WITH NRD (row 102) the strap is a smooth orange bar that seems to glow, with soft triangle shadows; the hammered
   surface is gone and the rivets are flat polygons.
3. Light from the left only (row 103): a bright glow at the left edge, the wooden sign 4 times brighter, the post on
   the right black (x0.11).

### metal (full run): NOT TRACED. PATH TRACING PAUSED ITSELF (row 100)

Both traced steps hold the game's own picture (x0.97, the same flicker, the same dots as the game's step).
`step.json` says `traced: False, expectedTraced: True`; the harness waited 51 s in each step. The game's log:
"Path tracing paused: 6 textures unavailable (18.9 MB). The game is drawing the scene", and before it 13 lines
"Path tracing texture unavailable: 'Tex_Fore_Collumn_BaseMap' ... kept back near this card's video memory budget".
The bridge's line at that moment: "the game uses 12.25 GB of its 13.52 GB budget". In every other scene the budget
was 14.85 GB and the game used 8.6 to 11.9 GB. So something else took 1.3 GB of the card for a while, and the game
with tracing on had no room left on a 16 GB card.
What a player sees: the traced picture turns into the game's picture without a word they can act on.
Guess, not measured: on a 12 GB card this would happen in most outdoor scenes (they use 10.8 to 11.9 GB here).
Armour and metal reflections (PT-21, PT-32) could not be judged in this run.

### dark-room (full run; near darkness)

Whole picture: x0.87 none, x0.90 NRD. Flicker 0.18 none, 0.06 NRD, game 0.03. NRD's boil 0.028 (game 0.011).

What is right: brightness and layout match; it is calm; no flashing dots with NRD.

1. FLAT TONES (row 104). `dark-room-floor-x8.jpg` (shown 8 times brighter): the game's floor is flagstones with
   joints. With no denoiser the stones are faint; with NRD the floor is a few flat patches of one colour with stepped
   edges. On a dark screen each step is one level in about 20: a visible band.
2. The chair in the corner is at x0.55, nearly lost in the dark.
3. Both flicker maps show faint rings over the whole picture: the bands' edges creep from frame to frame.

### doorway (full run; a stone stairwell, daylight through an arched door)

Whole picture: x0.42 both. Flicker 0.67 none, 0.11 NRD, game 0.09. NRD's boil 0.038 (game 0.012). With NRD 62 % of
the screen shows the grid (row 29).

What is right: sunlight falls through the arch onto the landing with the arch's own shadow: the kind of light the
game cannot make. No daylight leaks along the wall seams. With NRD it is calm.

1. THE STAIRWELL IS DARK (row 105): the walls at a quarter of the game's brightness, the lower steps at 0.18. A room
   lit through one door is dim; whether this dim is right is the owner's call. The eye would adapt more than this.
2. THE VIEW OUTSIDE IS WASHED OUT (row 106). `doorway-arch.jpg`: the walkway's dark mossy cobbles are pale cream, the
   parapet white, a milky veil over it. The brightening meant for the dark room also lifts the daylight.
3. The hanging ivy sprig is a black shape (yellow-green in the game). The sunlit patch's edge is stairs of 2-pixel
   steps (row 62).

### forest and vista (full run): the same pictures as run 124542

forest: x1.03 none, x1.01 NRD; flicker 0.96, 0.63 (game 0.12); the trunk's boil cells 2.31; GPU 118 to 121 ms.
vista: x1.05, x0.98; flicker 0.89, 0.23 (game 0.08); 192 flashing dots with NRD (10 in the game); GPU 67 to 70 ms.
Nothing new; see their sections above.

### night (full run; a foggy night by a torch, a hooded figure; PT-33, PT-38)

Whole picture: x1.05 both. Flicker 0.32 none, 0.15 NRD, game 0.05. NRD's boil 0.053 (game 0.023).

What is right: one of the best scenes. The fog, the night sky, the embers in the air, the torch's warm light on the
figure and the grass all match the game (the figure x0.99 to x1.02). The far hills do not show lit through the fog.

1. FACETS WITH NRD (rows 110, 111). `night-person.jpg`: with no denoiser the cloak has its weave. With NRD the weave
   is gone and the cloak is made of flat triangles with hard edges; the mask's fine wire rings melt into a smooth
   plate. Detail 1.61 against 3.29.
2. The hood's torn flap, lit orange from behind in the game, is dark (light through thin cloth is missing here).
3. The ground by the torch is x1.52 and burnt to white at the torch's foot (row 112).
4. With NRD the grass still flickers 1.3 to 1.6 in 96-pixel cells; the mask's wires flicker in both.

### fog (full run; a stand-in save: grey rocks and a dead tree in thick fog)

Whole picture: x0.99 both. Flicker 0.09 none, 0.10 NRD, game 0.07. THE BEST MATCH OF THE RUN: the fog is as thick
and as bright as the game's; nothing flickers.

1. The rock in the foreground is a little darker (x0.85) and loses its fine detail (1.33 none, 0.79 NRD, 2.63 game).
2. The ground at the right is up to x1.36 brighter.
3. Faint rings in the flicker maps (the fog's smooth gradient in one-level steps that creep). I did not check
   whether the game's picture has them too; its own "line pattern" figure is high here as well (32 % of blocks).

### glow (full run; the crystal cave's bright side; PT-34, PT-47)

Whole picture: x1.12 none, x1.05 NRD, but 25 to 27 % of the screen is under half the game's brightness and 6 to 7 %
over twice. Flicker 1.36 none, 0.74 NRD (the highest NRD figure of the run), game 0.05. NRD's boil 0.304 (game
0.010): 30 times the game's.

What is right: the crystals glow and light the wall blue; the lamp lights the rock orange; both are mirrored in the
water. The cave reads as a lit cave.

1. NRD MAKES THE IVY FLASH (row 107). `glow-crystals.jpg`, right map: over the ivy on the crystal wall whole leaves
   light up and go out with NRD: blocks wander 1.19 levels on average and up to 7.6, twice the undenoised picture's.
   580 single dots flash over the picture (222 with no denoiser). `ov-glow.jpg`: NRD's flicker map is white over the
   wall, the grass and the waterline.
2. NO DENOISER (row 108): the lit wall and the grass flicker 3.3 levels; they are 1.4 to 1.9 times brighter than
   the game's, the ivy a brighter green.
3. THE SHORE (row 109). `glow-shore.jpg` (shown twice as bright): the game has wet rock with puddles, small white
   lights mirrored in them and a colourful creature in front. Traced: a dark blue sheet with orange patches where
   the lamp reaches; the puddle lights are gone; the creature is a dark shape, and with NRD a shape of flat facets
   (row 110).
4. With NRD the leaves on the wall are smeared into soft blobs between the flashes.

### stairwell (full run; a castle hall: wide stairs, a rose window, a statue)

Whole picture: x0.94 both. Flicker 0.20 none, 0.05 NRD, game 0.03. Calm; no flashing dots with NRD.

What is right: the blue haze in the hall and its brightness. No daylight leaks along the wall seams.

1. THE ROSE WINDOW IS MISSING (row 113). `stairwell-window.jpg`: the game has a large round window of stone tracery
   and pale stained glass. Traced there is only the haze's blue glow: no stone, no glass. Both traced steps.
2. THE STATUE IS A BLACK COLUMN (row 114). `stairwell-statue.jpg`: a robed stone figure holding a glowing red heart
   in the game. Traced: a dark shape with no surface at all; the heart is a faint dot.
3. THE GHOST (row 115): a pale see-through running man in the game. Traced: a faint grey shade you would not notice.
4. TORCHES (row 116): the two wall torches throw 2.3 to 6 times the game's light on the wall beside them (with no
   denoiser that wall is the scene's flicker: 5.1 in a cell). The stairs are a flat dark slope; the game shows steps.

### campfire (full run; a courtyard camp in the rain: a fire, a cooking fire under a pot, a tent, cloth on a line)

Whole picture: x0.92 none, x0.89 NRD. Flicker 1.15 none, 0.54 NRD, game 0.09. NRD's boil 0.226 against 0.220
with no denoiser (the game 0.018): NRD does not lower the boil here at all, and its worst blocks are worse
(p99 3.16 against 1.13, max 17.9 against 5.0).

What is right: both fires light the camp; the tent, the cloth on the line and the wall carry warm light and real
shadows (the ivy's shadow on the wall is new and looks right). The left fire's surround matches (x0.96 to x1.01).

1. NRD MAKES THE GRASS BY THE FIRE FLASH (row 117). `campfire-cauldron.jpg`, right map: the grass behind the cooking
   fire is solid white in NRD's flicker map and grey in the undenoised one. This is the strongest NRD flicker of the
   whole run (cells at 11.4 levels).
2. THE GROUND (row 118): wet gravel with glints in the game; traced a smooth tan sheet, smoother still with NRD.
3. The pot is brown-black (dark green-blue iron in the game) with a white glow under it; the logs and stones lose
   their texture with NRD; one stone has a black scribble pattern (row 120).
4. The ivy on the wall is black (row 119). With no denoiser the whole lit wall and the metal window grille flicker.
5. The stair's side at the bottom right is at x0.22: black where the game shows wood.

## 2b. One row a scene (the full run `20261008-130029`; my `summary.py`, from the kept frames)

How to read it: "x game" is the whole picture's brightness against the game's. "NRD light / none light" is the mean
of NRD's 16 frames against the mean of the 16 undenoised frames, in linear light, a channel; the two steps' automatic
gains are in brackets (where they differ, a part of the ratio is the gain). "dots" are single pixels that flash by
more than 10 levels in an otherwise calm 16-pixel block. "under half / over twice" is the share of the screen
(64-pixel blocks) where NRD's picture is under half or over twice the game's brightness.

| scene | game luma | none: x game | NRD: x game | NRD light / none light (R, G, B; gains none, NRD) | flicker none / NRD / game | boil NRD / game | dots NRD / game | detail none / NRD / game | under half / over twice (NRD) | game back after Off |
|---|---|---|---|---|---|---|---|---|---|---|
| npc-day | 105.6 | x1.08 | x1.07 | 0.99, 0.99, 0.99 (1.00, 1.00) | 0.74 / 0.44 / 0.10 | 0.113 / 0.030 | 110 / 22 | 2.65 / 2.16 / 2.49 | 0 % / 2 % | x0.999; mean difference 0.41 levels, pixels off by over 8: 0.01 % |
| hair-eyebrows | 10.2 | x1.87 | x2.13 | 1.16, 1.41, 1.97 (3.12, 2.99) | 0.93 / 0.17 / 0.03 | 0.076 / 0.007 | 54 / 17 | 2.06 / 1.13 / 1.38 | 3 % / 55 % | x1.000; mean difference 3.82 levels, pixels off by over 8: 13.12 % |
| hair-moving | 16.2 | x1.04 | x1.11 | 0.85, 1.09, 1.72 (3.91, 3.60) | 0.69 / 0.19 / 0.04 | 0.084 / 0.008 | 103 / 9 | 2.33 / 1.35 / 2.27 | 10 % / 30 % | x1.000; mean difference 0.14 levels, pixels off by over 8: 0.00 % |
| hair-glowing | 13.5 | x1.87 | x1.78 | 0.89, 0.93, 0.98 (2.86, 2.89) | 0.69 / 0.18 / 0.03 | 0.074 / 0.008 | 214 / 64 | 2.05 / 1.44 / 1.58 | 16 % / 51 % | x1.002; mean difference 0.12 levels, pixels off by over 8: 0.01 % |
| eyebrows-flickering | 31.1 | x0.70 | x0.65 | 0.83, 0.76, 0.74 (3.05, 3.49) | 0.76 / 0.17 / 0.05 | 0.078 / 0.011 | 46 / 1 | 1.57 / 1.31 / 2.19 | 23 % / 2 % | x1.000; mean difference 0.16 levels, pixels off by over 8: 0.00 % |
| npc-candle | 13.8 | x1.71 | x1.80 | 1.12, 1.20, 1.20 (3.76, 3.71) | 0.63 / 0.15 / 0.04 | 0.065 / 0.010 | 166 / 69 | 1.66 / 1.03 / 0.98 | 0 % / 25 % | x1.001; mean difference 0.09 levels, pixels off by over 8: 0.01 % |
| tavern-room | 23.6 | x1.41 | x1.40 | 0.84, 0.99, 1.23 (3.54, 3.26) | 0.94 / 0.21 / 0.05 | 0.085 / 0.010 | 140 / 20 | 2.50 / 1.60 / 2.47 | 6 % / 30 % | x0.998; mean difference 0.18 levels, pixels off by over 8: 0.00 % |
| boil-cage | 15.3 | x0.82 | x0.85 | 1.09, 1.09, 1.07 (4.58, 4.41) | 0.16 / 0.06 / 0.03 | 0.026 / 0.009 | 0 / 0 | 0.30 / 0.13 / 0.52 | 14 % / 0 % | x1.002; mean difference 0.07 levels, pixels off by over 8: 0.00 % |
| glass-jars | 14.6 | x1.37 | x1.61 | 1.03, 1.16, 1.35 (6.44, 5.24) | 1.13 / 0.17 / 0.03 | 0.084 / 0.007 | 65 / 9 | 2.45 / 1.06 / 1.81 | 11 % / 48 % | x1.000; mean difference 0.10 levels, pixels off by over 8: 0.00 % |
| candle | 84.9 | x0.49 | x0.51 | 1.09, 1.15, 1.27 (1.29, 1.33) | 1.23 / 0.18 / 0.20 | 0.080 / 0.036 | 57 / 91 | 2.62 / 1.71 / 4.10 | 56 % / 0 % | x1.001; mean difference 0.45 levels, pixels off by over 8: 0.04 % |
| refract-jar | 36.5 | x1.26 | x1.26 | 0.83, 0.85, 0.89 (4.48, 3.05) | 0.87 / 0.20 / 0.06 | 0.097 / 0.014 | 45 / 0 | 2.91 / 1.53 / 1.89 | 8 % / 16 % | x1.000; mean difference 0.20 levels, pixels off by over 8: 0.00 % |
| caustics-glass | 56.2 | x0.99 | x0.97 | 0.96, 0.94, 0.94 (1.05, 1.05) | 1.36 / 0.22 / 0.07 | 0.075 / 0.018 | 121 / 0 | 4.14 / 2.49 / 3.44 | 3 % / 1 % | x1.001; mean difference 0.38 levels, pixels off by over 8: 0.00 % |
| water-day | 66.6 | x1.78 | x1.74 | 0.95, 0.95, 0.94 (1.00, 1.00) | 3.23 / 0.25 / 0.08 | 0.129 / 0.017 | 30 / 0 | 2.61 / 0.92 / 4.52 | 0 % / 44 % | x1.001; mean difference 0.30 levels, pixels off by over 8: 0.00 % |
| glow-lake | 22.7 | x0.39 | x0.38 | 1.12, 1.00, 0.99 (6.71, 6.72) | 0.14 / 0.11 / 0.03 | 0.053 / 0.008 | 55 / 0 | 0.22 / 0.17 / 1.19 | 79 % / 0 % | x1.000; mean difference 0.11 levels, pixels off by over 8: 0.00 % |
| water-torch | 54.8 | x1.39 | x1.43 | 1.00, 0.99, 1.00 (1.00, 1.03) | 1.24 / 0.22 / 0.07 | 0.121 / 0.012 | 50 / 2 | 2.09 / 0.78 / 3.15 | 3 % / 28 % | x1.000; mean difference 0.26 levels, pixels off by over 8: 0.01 % |
| door-strap | 15.4 | x0.89 | x0.86 | 0.94, 0.94, 0.97 (3.60, 3.85) | 0.61 / 0.11 / 0.03 | 0.046 / 0.006 | 16 / 0 | 2.02 / 1.29 / 2.11 | 34 % / 22 % | x1.001; mean difference 0.10 levels, pixels off by over 8: 0.00 % |
| metal (NOT TRACED) | 70.8 | x0.97 | x0.97 | 1.00, 1.00, 1.00 (1.00, 1.00) | 0.17 / 0.16 / 0.10 | 0.027 / 0.018 | 20 / 11 | 5.23 / 5.23 / 5.04 | 2 % / 0 % | x1.001; mean difference 2.69 levels, pixels off by over 8: 7.79 % |
| dark-room | 13.4 | x0.87 | x0.90 | 1.08, 1.06, 1.05 (4.75, 4.65) | 0.18 / 0.06 / 0.03 | 0.028 / 0.011 | 0 / 0 | 0.36 / 0.16 / 0.40 | 2 % / 0 % | x1.001; mean difference 0.06 levels, pixels off by over 8: 0.00 % |
| doorway | 89.3 | x0.42 | x0.42 | 1.01, 1.01, 1.02 (2.58, 2.58) | 0.67 / 0.11 / 0.09 | 0.038 / 0.012 | 5 / 2 | 1.78 / 1.18 / 5.38 | 67 % / 0 % | x1.002; mean difference 0.38 levels, pixels off by over 8: 0.00 % |
| forest | 76.5 | x1.03 | x1.01 | 0.97, 0.97, 0.98 (1.00, 1.00) | 0.96 / 0.63 / 0.12 | 0.129 / 0.033 | 182 / 118 | 3.72 / 2.82 / 3.83 | 0 % / 0 % | x0.999; mean difference 0.59 levels, pixels off by over 8: 0.08 % |
| vista | 135.0 | x1.05 | x0.98 | 0.91, 0.94, 0.94 (1.00, 1.00) | 0.89 / 0.23 / 0.08 | 0.067 / 0.017 | 192 / 10 | 3.09 / 2.47 / 3.54 | 1 % / 0 % | x1.000; mean difference 0.35 levels, pixels off by over 8: 0.01 % |
| night | 45.2 | x1.05 | x1.05 | 1.01, 1.00, 1.00 (1.77, 1.79) | 0.32 / 0.15 / 0.05 | 0.053 / 0.023 | 58 / 5 | 0.77 / 0.51 / 0.68 | 1 % / 1 % | x1.000; mean difference 0.10 levels, pixels off by over 8: 0.01 % |
| fog | 64.2 | x0.99 | x0.99 | 0.99, 0.99, 0.99 (1.99, 1.99) | 0.09 / 0.10 / 0.07 | 0.039 / 0.030 | 0 / 0 | 0.41 / 0.31 / 0.62 | 0 % / 0 % | x1.000; mean difference 0.11 levels, pixels off by over 8: 0.00 % |
| glow | 29.4 | x1.12 | x1.05 | 0.85, 0.90, 0.98 (5.54, 4.34) | 1.36 / 0.74 / 0.05 | 0.304 / 0.010 | 580 / 8 | 3.10 / 1.62 / 2.45 | 27 % / 6 % | x1.000; mean difference 7.35 levels, pixels off by over 8: 26.82 % |
| stairwell | 22.3 | x0.94 | x0.94 | 1.05, 1.01, 1.00 (10.54, 10.74) | 0.20 / 0.05 / 0.03 | 0.018 / 0.008 | 0 / 0 | 0.46 / 0.20 / 0.58 | 2 % / 3 % | x1.000; mean difference 0.07 levels, pixels off by over 8: 0.00 % |
| campfire | 63.7 | x0.92 | x0.89 | 0.90, 0.89, 0.89 (1.00, 1.00) | 1.15 / 0.54 / 0.09 | 0.226 / 0.018 | 242 / 3 | 3.47 / 2.81 / 5.78 | 4 % / 0 % | x0.999; mean difference 0.49 levels, pixels off by over 8: 0.17 % |

What the last column says (PT-20, "completely essential"): the game's own picture IS back after Off in every scene:
its brightness is within 0.2 % of the picture before tracing, in all 27. In three scenes (hair-eyebrows, glow,
metal) the view itself had shifted by some pixels between the first and the last step, so the two cannot be held
pixel on pixel there (row 123: a harness matter; it also means the traced steps of those scenes may be a few pixels
off the game's step).

What the table shows at a glance:
- Flicker with no denoiser is 5 to 40 times the game's in every traced scene but fog. The worst: water-day 3.23,
  glow 1.36, caustics-glass 1.36, candle 1.23, campfire 1.15, glass-jars 1.13.
- With NRD most scenes come down to 0.05 to 0.25. Four do not: glow 0.74, forest 0.63, campfire 0.54, npc-day
  0.44. All four are plants in light.
- NRD's boil is 2 to 30 times the game's in every scene but fog, and over 5 times in 14 of them (glow 0.304,
  campfire 0.226, water-day 0.129, forest 0.129; the game's own is 0.006 to 0.036).
- In 25 of the 26 traced scenes NRD's picture has less fine detail than the game's: water-day 0.20 of it, doorway
  0.22, stairwell 0.34, campfire 0.49, hair-moving 0.59, vista 0.70, forest 0.74.
- Brightness: 9 of the 26 traced scenes are within 10 % of the game's as a whole picture (npc-day, caustics-glass,
  dark-room, forest, vista, night, fog, glow, stairwell), and some of those only on average (glow: 27 % of the
  screen under half the game's, 6 % over twice).

## 3. The five worst, and what I could not judge

### The five things a player would notice first (my judgement)

1. WATER IS A MILKY SHEET (rows 59, 68, 69, 81, 82, 94, 97; PT-81). Every sea, lake and well: no waves, no foam, no
   dark depth, no bed showing through the shallows, 1.7 to 2.9 times too bright, and with no denoiser the whole
   surface flickers 5 levels. Four scenes. It reads as fog or concrete, not water.
2. PEOPLE UP CLOSE (rows 33 to 39, 48 to 53). Faces at 0.13 to 0.5 of the game's brightness and orange-red, eyes
   black holes, eyebrows gone, hair dark and stringy and flashing with NRD, armour bronze with white dots; a man a
   metre away is a black shape. The player's own character is the darkest thing in a room that is itself too bright.
3. THINGS THAT ARE NOT THERE (rows 16, 44, 57, 113, 114, 115). A blossoming tree is bare, a patch of plants is bare
   ground, a rose window is a blue glow, leaded windows are open holes, a statue is a black column, a ghost is
   gone. A player sees the world change when they flip the row.
4. PLANTS FLICKER, AND NRD DOES NOT STOP IT (rows 1, 2, 4, 6, 10, 23, 32, 46, 93, 107, 117; and the dots: rows 54,
   92). Grass flickers 1.3 to 2.4 levels in a frozen world with and without NRD (the game: 0.1); a trunk boils; in
   the cave and by the cooking fire NRD makes lit leaves flash harder than no denoiser does (boil 1.19 against
   0.50; 10.9 against 5.2). Hundreds of single white dots flash in dim scenes with NRD.
5. LIGHT IN THE WRONG PLACES (rows 26, 33, 47, 55, 71, 72, 74, 91, 103, 105, 106, 116, 121). Rooms are 1.4 to 2.1
   times brighter with ceilings up to 8 to 20 times, while floors and people are at half; a daylit corner is at
   half and orange; the cave's dark side is at 0.3 to 0.4; candles and lanterns burn their surroundings out, and
   NRD doubles the glow round a candle and turns dim rooms blue.

Close behind, and each on every screen: edges in 2-pixel stair-steps and thin things as dots (row 62); NRD wiping
texture off rock, wood, cloth and mirrors (rows 9, 21, 40, 63, 78, 111) and leaving a fine grid over dim pictures
(row 29); nothing looks wet in the rain (rows 8, 118). Outside the picture: path tracing switched itself off in one
scene of 27 for want of video memory on a 16 GB card (row 100), and a frame takes 67 to 123 ms at 1924x1084 (8 to
15 frames a second; row 67).

### What looks right today

Fog and haze (the `fog` scene matches to 1 %, `night` to 5 %); the far backdrop, sky and castle of the vista; the
flame rectangles are gone; thin bars do not boil (`boil-cage`); a perfect mirror shows what the tracer shows
directly when no denoiser is on; a bottle scene by a brazier and a candle-lit bald man look good with NRD; sunlight
through a door and a fire's shadows on a wall are light the game cannot make; the game's own picture comes back
after Off in all 27 scenes (brightness within 0.2 %).

### What I could not judge

- MOTION. Every picture is a still of a frozen world. Smear and ghosts behind moving things, hair that moves with
  the camera (PT-53), swaying grass and cloth (PT-49), the denoiser's lag: not in these runs (the `quick` recipe
  has no moving step; `lantern-lake`'s one running step doubles the flicker, 1.73 against 0.84).
- RAY RECONSTRUCTION: in none of these runs, by the owner's order of judging.
- ARMOUR AND METAL (PT-21, PT-32): the `metal` scene was not traced (row 100). BLACK SPOTS ON FLOORS (PT-27): its
  save is missing (row 90). A FACE IN DAYLIGHT: the `npc-day` view has no person in it now.
- GLASS CAUSTICS and WATER CAUSTICS: the bottles in `caustics-glass` are painted, not see-through, so nothing is
  owed there; the water shows no bed and no rippling light at all, and the Caustics row changes 2.6 % of the light
  with nothing to see (row 95). So: nothing visible, not "judged fine".
- CAUSES. I read no source code. Every cause above is marked as a guess.
- WHETHER DARK IS RIGHT. The stairwell lit through one door (row 105), the cave's dark side (rows 71, 91) and the
  crypt (row 72) are far darker than the game's. A real room may be that dark; the game's is lit by lights with no
  source. Which the owner wants there is theirs to say. I list them because the picture differs by 2 to 10 times.
- THE FAINT RINGS in dark scenes' flicker maps (row 104 and the notes under boil-cage and fog): flat tones whose
  edges creep. I measured that the traced picture has longer runs of one colour than the game's (6.3 pixels
  against 1.4) but did not check the game's own picture frame by frame for the same.
- THREE SCENES' LAST STEP is shifted by some pixels against the first (row 123), so their pixel-on-pixel
  comparisons with the game are a few pixels off; region means are not affected.
- THE OLDER RUNS `20261008-083001` and `-101310` (water-day and glass-jars on older builds) and `-111121` (the
  earlier mirror run): not opened. The full run holds the same scenes on the newer build.
- THE RUN'S OWN `numbers.txt` was still being written when I finished (7 of its scenes were in). Those 7 agree with
  my numbers to the second digit (hair-eyebrows: NRD's light R 1.156, G 1.404, B 1.950 of the undenoised).

### Files

All crops and sheets are beside this file. Names: `<scene>-<what>.jpg` (a crop sheet), `ov-<scene>.jpg` (a scene's
overview: the three mean pictures, traced against game brightness, the two flicker maps), `mir-<scene>-*.jpg` (a
mirror's inside), `after-<scene>.jpg` (before, after Off, the difference). Crops of run 124542 are named `forest-*`,
`vista-*`, `candle-*`, `tavern-*` (the hair-moving scene). Numbers: `numbers-124542.txt`,
`numbers-20261008-130029.txt`, `summary-table.md`. Tools I wrote (they only read the runs): `look.py` (a crop with
numbers), `scene.py` (a scene's whole-frame numbers), `ov.py` (the overview), `mir.py` (a mirror), `summary.py`.
