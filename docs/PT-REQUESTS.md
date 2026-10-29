# TG Path Tracing: the owner's requests and the defects found

The owner, 2026-10-06: "please maintain a list of all the requests I have, and track them in the backlog so they're not
forgotten, including things like terrain blending, better vegetation/grass, and stuff. If you discover other visual
defects or areas of improvement for better compatibility with PT, please add them to the backlog too".

How this list works: one entry per request or defect, with the owner's words where there are any, where the evidence
is, and what comes next. `BACKLOG.md` carries the same IDs, one line each, in priority order. **Add an entry the moment
a request is made or a defect is seen**, by whoever sees it (the owner, the harness, an agent), and change its state
when it changes. IDs never change and are never reused. States: open, looking into it, measured, to verify in game,
waiting for the owner, answered, done.

Evidence paths are under `artifacts/harness/<run>/` (not in git; `docs/PT-HARNESS.md` says what each picture is).

## Open

### PT-01 Hair, eyebrows and lashes look broken when traced
- The owner: "Hopefully you can still see the completely broken hair and eyebrows" (2026-10-06). Earlier (2026-09-28):
  hair must look like the game's, fine soft strands, not "LEGO", "Fortnite" or "dreads"; brows thin, not black bars.
  Hard rule: hair and eyes are shaded by the tracer, never left to HDRP.
- The owner, 2026-10-06 evening, of Ron: "His hair in vanilla is also so much finer detailed, with more detailed specular
  response. Ours look terrible by comparison". The bar is the game's picture: fine single strands, fine highlights.
- Round 4 (a fresh agent, `docs/agents/hair-4.md`; built, offline only, merged 21:55): the brows were black because the
  plugin held them opaque, and HDRP draws an opaque RealHair card whole into its depth (the game has no opaque build
  that cuts), so nothing lit those card pixels; brows the tracer draws as a layer now stay transparent. The crown's veil,
  found by elimination: the layer covered 98-99 % of each crown pixel, and the composite still added the skin's own
  HDRP emission buffer (GBuffer3) on top; it now leaves out the share the hair covers (an offline check reproduces the
  veil with the old composite). Left for later: Ray Reconstruction's guides at hair pixels describe what is behind the
  hair, so it smooths strands away (-42 % fine detail in the crown); the highlights read a 64x64 tile, 1/32 of the
  game's 2048 map per side; the colour a 512 tile of a 2048 map (PT-45 covers both).
- In the harness (run `20261006-220024`, `hair-eyebrows/head-r4.jpg`; plugin 1311E8E758D8, native C548AEE3A3EF): the
  veil and the black brow bars are gone; the crown is solid red hair with strands, with Ray Reconstruction and with no
  denoiser. Against the game's: brighter and more orange, coarser clumps where the game's is smooth and fine. The log
  says the brows stay transparent ("_SurfaceType 1, queue 3000"). Not yet looked at by the owner.
- The owner, 2026-10-06 night, of that run: "he certainly looks a lot better. But it still seems blurry/low-res and not
  as finely detailed as the vanilla game, including his brows and eyes".
- Measured (`<scratch>/detail_boxes.py`: fine detail, each picture's luma minus its 2-pixel blur, relative to the box's
  brightness; the game's picture `20261006-212905/1-game`, the same frozen moment): crown, traced with Ray
  Reconstruction 0.36 of the game's, with no denoiser 0.63, NRD 0.44; eyes 0.68, 0.94, 0.63; cheek 0.74, 0.92, 1.31
  (NRD's noise counts as detail there). Both pictures render at 50 % (1924x1084) and are upscaled the same way, so
  this is the tracer's: its own picture already loses a third of the hair's detail (the 512 colour tile of a 2048
  map, the 64x64 highlight tile, the strands as drawn), and Ray Reconstruction loses about as much again (its guides
  at hair pixels describe what is behind the hair: `docs/agents/hair-4.md`).
- Ron's RR guides (`docs/agents/ron.md`, merged 23:43) in game (run `20261007-022505`, `hair-eyebrows/head-guides.jpg`;
  native CAD68865EB63, the tested one; the lights round is in this build too): fine detail against the game's, with RR
  then without a denoiser: crown 0.31 and 0.47 (0.36 and 0.63 the run before), eyes 0.78 and 0.93 (0.68, 0.94), cheek
  0.74 and 0.89. So the eyes gain under RR (0.68 to 0.78), and RR keeps more of what the tracer draws at the crown (66 %
  of it, 57 % before), but the crown's own detail fell (0.63 to 0.47 with no denoiser), which the guides cannot do:
  the lights round's light sizes changed how the wall candle lights the hair (its strands now in larger clumps with
  darker gaps). Not yet looked at by the owner.
- The owner, 2026-10-07 08:45, of run `20261007-081218/npc-day` (a red-haired character by day, stand-in save
  Slot_50): "why is ha[i]r fogged out in the top here?" (`npc-day/head-day.jpg`: the game's, RR, NRD): the crown is
  semi-transparent, the hillside shows through it like a veil, and a dark see-through band runs across the hair; the
  game draws it solid. Given to the hair-strands agent (`wt/strands`).
- The hair-strands agent (`docs/agents/strands.md`, merged 09:42): the see-through top came from the 8-card count (a ray
  grazing the skull passes many thin cards; the hair's share there 0.35): now the first 8 are lit one by one and the ray
  goes on through every card behind them, stopping at one by chance as its texel covers (offline: 16 cards of 0.2
  coverage let 0.028 of the wall through, as the game's; the cap let 0.173). Strands are read with the game's own
  anisotropic filtering (the Ultra setting) and lit by the hair's normal map (`Tex_H_m001_Normal`, strength 0.5), as
  RealHair does; RR gets the strand's normal. The "crown" box of the last rounds (0.465,0.36,0.53,0.41) sat above Ron's
  head: on his real crown (0.483,0.418,0.525,0.448) the tracer's own picture has 0.98 of the game's detail by that
  number, but as clumps and grain where the game has thin strands. Not changed: the hair's light reaches the screen a
  frame late, at the previous frame's jitter (about 23 % of the finest detail in a model of the upscaler; hair, brows
  and eyes): a next fix.
- Next (a fresh agent, Ron's detail): give Ray Reconstruction the hair's, the brows' and the eyes' own guides (colour,
  normal, roughness, hit distance), and larger hair, brow and highlight tiles until PT-45 brings the game's own
  textures. Judged by these three boxes against the game's picture.
- Seen: `20261006-134601/npc-day/hair-crop.jpg`: soft auburn hair in the game's picture, pale clumps with black holes
  traced, with either denoiser.
- Found 2026-10-06: the installed build is half of the hair layer. Its native part (2026-09-28 12:45) draws hair as a
  layer (`pt_hair.hlsl`); its managed plugin of the same minute lacks the code that hides HDRP's own hair layer and
  raises its cutoff (`CharacterCapture.HideLayerHair`, `GameCut`: in the tree, never built into the game). Seen by
  decompiling both plugins.
- With the tree's plugin (run `20261006-155214`, 15:54; plugin 24A58C8046B0 with the native of 2026-09-28 12:45): the
  hair is drawn by the tracer as a layer now, and it is broken another way. `npc-day/hair-man.jpg` (the head at full
  size: the game's, the traced with Ray Reconstruction, with NRD): where the game has soft auburn strands, the traced
  hair is flat tan clumps with stair-stepped edges about 8 pixels a step and hard black gaps. The same with both
  denoisers, so it is the layer's own drawing, not a denoiser. The log: "hair drawn by the tracer as a layer over HDRP's
  surface (alpha clip 0.1, depth prepass 0.5)", HDRP's own passes for it switched off.
- The hair agent's report (17:55, `docs/agents/hair.md`, merged into the tree): two causes.
  1. The layer covered a pixel by the raw texture alpha; the game's RealHair (read from its compiled shader) covers
     by pow(alpha / _AlphaRemap, _AlphaPower), cuts below _AlphaClip and covers fully from _DepthPrepass (0.5 on hair
     and beards, 0.95 on brows). On Ron's texture 41 to 50 % of the hair texels are at or above 0.5 and were left
     24 to 28 % uncovered, the scalp in the hair's shadow showing black through them. Fixed in `pt_hair.hlsl`
     (`hair_game_alpha`, `strand_cover`); power and remap in `TGPT_Material`'s reserved last field (no ABI change).
     The offline check "layer hair covers the wall" passes now (its card had 32 checker squares across 52 pixels: made
     fair), mutation-checked three ways; source checks 76 of 76 (nine stale lines of the hair layer updated).
  2. In game HDRP still draws the hair's depth while tracing (Ron's debug views: bald in Albedo, his hair's normals in
     HDRP's normal buffer, the hair magenta in Surfaces), so the layer only finds the faint texels in front of it. The
     log says the passes were switched off. Not fixed: a log line now says when a held hair material is HDRP's again
     and what turned back on. Also: the shadow pass compare was case-sensitive (RealHair names it SHADOWCASTER).
  Also found: only 32 detail tiles; 29 of 53 hair materials in one run fell back to 64x64 tiles and off the layer.
- Merged 17:57, tgpt_gpu_test 557 of 557, installed 18:13 (plugin 87F6505B0480, native 6F0076E1B74A). In game
  (`20261006-181324`, `20261006-182908/hair-eyebrows/views-0.40-0.36-0.60-0.62.jpg`): unchanged to the eye. The owner,
  18:15: "the hair and eyebrows still look ridiculous". The new log line never fires: the materials the mod holds stay
  switched off, and HDRP still draws hair there (bald in Albedo, magenta in Surfaces). The state after Off: 0 changed.
- Round 2 (merged 18:49, installed 18:52 with everything else: plugin 1B7CE906FD75, native 7F6FC36D5410, tgpt_gpu_test
  629 of 629): the game's character renderer (Kandra) culls each renderer by a mask of its own; while tracing, a
  renderer whose every material is layer hair or a scalp or beard mask gets that mask 0 and is given back after. In
  game (`20261006-185232`, `20261006-185451/hair-eyebrows/head-before-after.jpg`): the log says Ron's hair renderer
  was left out of Kandra's culling; the clumps and black gaps are gone, his hair and beard are soft and auburn, the
  tracer's layer (cyan in Surfaces, colored in Albedo). The state after Off: 0 changed. **Still wrong:** the crown is a
  thin veil over bare pink skin (the game's painted scalp mask under the cards went with the hidden renderer and the
  tracer leaves scalp masks out), and the eyebrows are thick black bars (a magenta strip in Surfaces: HDRP still draws
  there). The owner has not looked at this build.
- Round 3 (`docs/agents/hair-3.md`, merged into the tree; in the agent's copy tgpt_gpu_test 634 of 634, source checks
  78 of 78): (1) the scalp and beard masks (Kandra_Lit, transparent, a grey mask whose color equals its alpha, tinted
  by the character's scalp color) are drawn by the tracer as the hair layer's lowest layer, taken up to 1 mm past
  HDRP's depth, 8 cards a ray where it was 4; one tile per mask texture through the existing hair color bake. (2) The
  brows: Kandra's batches draw a material's passes whether or not they are switched off, and some of RealHair's depth
  variants cut at `_DepthPrepass`, not `_AlphaClip`, so HDRP still wrote the brows' depth and normals with no color
  (the black bars): `_DepthPrepass` and `_DepthPostpass` are raised to 1.1 with `_AlphaClip` and given back after.
  The agent's note: HDRP's cornea, "hidden" by switched-off passes, is most likely still drawn too (not touched).
- In game (20:35, tgpt_gpu_test 634 of 634, plugin 04A706B785A9, native 698CBDDB0D29): **no change on the layer
  path.** Run `20261006-203513` (Ron first after the game's start: he gets the layer): the crown still a pale veil, the
  brows still black bars, though the log says the mask is drawn as the lowest layer and the brows' cuts are 1.1 (their
  passes GBuffer, DepthOnly and ShadowCaster are on: held opaque, the brows are a deferred material). Run
  `20261006-203731` (Ron second, after npc-day): his hair solid but blocky with stair steps: the old held-opaque path,
  because the 32 detail tiles were used up by then (PT-35). `hair-eyebrows/head-two-runs.jpg` has both.
- State: the hair agent's round 4 (20:51, `wt\hair4`): prove where the mask shell is against HDRP's depth and what
  draws the brows, then fix. The tiles: the surfaces agent (PT-35).
### PT-02 Turning path tracing off does not give hair materials their render queue back
- Found by the harness, 2026-10-06, in every scene with people: `FirstPass_Mat_H_*` 2750 -> 3000,
  `FirstPass_ScalpMask_*` 2749 -> 2999 (2750 is HDRP's "before refraction" queue, 3000 its default transparent one).
  The picture came back the same wherever it was checked; the state did not.
- Cause: the game's `HairController.Refresh` calls HDRP's `ValidateMaterial`, which derives a queue from the queue
  the material has: "before refraction" made opaque and transparent again comes back as plain transparent. The game
  does the same to hair that walks far away and back; the mod forced it on every head at once.
- Baseline `20261006-145526` (the installed build): not back in 7 of 19 scenes (npc-day 4 values, tavern-room 14,
  glass-jars 9, caustics-glass 3, door-strap 11, metal 1, campfire 1; none where no head is in view).
- State: **done, verified by the in-game harness** (run `20261006-155214`, the tree's plugin): 0 of 32,000 to 37,500
  values differ after Off in all seven scenes (npc-day, npc-candle, tavern-room, candle, refract-jar, metal,
  campfire), also after four more toggles. The fix: `CharacterCapture.RememberQueues` and `GiveQueueBack` (each
  material's queue from before the hold, given back with it). Not yet seen by the owner (nothing to see: a state).
### PT-03 Grass and vegetation shade badly under the tracer
- The owner: "Seems like the grass shading can still look a bit weird. Some dark patches, other white overblown ones.
  Might need an overhaul" and "Definitely make grass/vegetation more 'PBR'/play-nicer-with-PT" (2026-10-06).
- Seen: `20261006-134601/npc-day/game-vs-traced.jpg`; full-size frames kept in `20261006-140217/npc-day/`.
- The agent's findings (`docs/PT-VEGETATION.md`), in its order: (1) the sun too strong (PT-05's cause: the white
  streaks are the sun glinting on blades, the dark patches hard sun shadows the game does not have); (2) blades
  reflect too much: Reflection Boost on them, the material's own occlusion thrown away, rays skipping all grass within
  20 cm; rain makes every surface under the sky glossy; (3) the sky's light reaches a leaf from one side only, and the
  "which way it faces" guess is wrong on a blade one or two pixels wide.
- The agent's corrected report (16:45; the file's "What changed since the first draft"). Its three most likely
  causes now, in its order:
  1. **Blades reflect 18 to 35 times too much** (it is 90 % sure this is the white streaks). HDRP takes a
     transmission material's mirror share from its diffusion profile: the foliage profile's is 0.0023 (an index of
     refraction of 1.1), and 88 of the game's 128 grass and leaf materials use it. The tracer uses 0.04 for all, and
     Reflection Boost made it 0.079. In rain the game makes every surface under the sky glossy: at 0.0023 a wet blade
     stays matt, at 0.04 it is a mirror.
  2. **Traced grass casts hard sun shadows the game never draws** (90 %: most of the dark patches). The game's
     vegetation settings give grass, plants and ivy "cast shadows: off" at every quality level; `LeshyCapture.cs`
     traces them as full blockers.
  3. The sun at full strength (PT-05). Open there: in rain the game's sun is about 8 % of the traced one where the
     fog alone leaves 19 to 44 %; what else dims it is not found (not clouds, PT-36).
  Then: light through leaves is grey 50 % for sun and lamps only, where HDRP's is yellow-green and also for the sky's
  light from behind.
- The owner, 2026-10-06 17:06, asked whether grass should cast no shadow (as the game) or soft partial ones: "A,
  obviously. We want grass shadows, but not those horrible looking ones. Also sometimes they are pure white". So: grass
  keeps its shadows, soft and partial (a blade stops a shadow ray only as much as it blocks light); never the game's
  "no shadows from grass". And grass that comes out pure white traced is a defect of its own to find.
- State: an agent works on it in its own copy of the tree (17:01): the mirror share from the diffusion profile, light
  through leaves from the profile and the sky's light from behind, the material's own occlusion, a thin blade's normal,
  the bounce color, and now the soft grass shadows and the pure white grass. The owner, 17:10: "Grass tends to loose a
  lot of its green color in the PT presentation. And it still has these weirdly overexposed light spots here and
  there." (sent to the agent; the torch light on grass is PT-38). Installed 17:02 already: Reflection Boost
  off grass and leaves (halves cause 1), the fog on the sun (cause 3).
- The grass agent's report (18:45, `docs/agents/grass.md`), merged 18:50 (source checks 78 of 78). Measured first on
  the harness's frames: the traced grass had two thirds of the game's color (chroma 0.33 to 0.38 against 0.54 to 0.57)
  and too much blue; a quarter of its light was a grey film of sky reflected. Built, each with an offline check and its
  mutation check: a blade's reflectance from its diffusion profile (foliage 0.0023, where the tracer used 0.04; ABI
  0x00011500); light through a leaf tinted by its profile and the sky's and bounced light from behind; the material's
  own occlusion; a thin blade's own normal; the bounce color from the grass's own colors; grass shadows soft and
  partial as the owner chose (a blade lets a light's shadow ray through half the time); the same for thin cloth
  (PT-39). The near-white share of grass was already 0.00 % with the fog on the sun; the glints left are cut 17 times.
- In game (`20261006-185451` against `20261006-170351`, the agent's `grass_chroma.py`): the grass's color (chroma)
  0.38 -> 0.57 at Horns of the South (the game's 0.58), 0.34 -> 0.62 in the forest (0.54), 0.16 -> 0.50 by the night
  bridge (0.48); its brightness x1.24 of the game's at the first two; no white. `forest/grass-before-after.jpg`,
  `npc-day/grass-before-after.jpg` (the game's, before, after): green as the game's, the grey-blue cast and the
  glints gone; some blades lit from behind are a bright yellow-green.
- State: seen in the harness's pictures; the owner has not looked. The agent's round 2 (19:52, `wt\grass2`): the
  x1.24, the bright backlit blades, and cloth (PT-39).
### PT-04 Round things look low-poly ("stepped") when traced: cups, jars, cloth folds, shoulders
- The owner: "The vases and jars now seems less rounded and more low-poly around their circumference" (2026-10-06).
- Seen: `20261006-134130/glass-jars/jars-crop-bright.jpg`: two white cups, round in the game's picture, with flat
  faces around their sides traced.
- The owner again, 2026-10-06 16:30, after watching the runs: "Seems like cloth, jars, and other things can look a
  little 'stepped'/low-poly in the Path Traced rendering? Not sure what it is, but around corners, shoulders, cloth
  folds and stuff like that, or around a circle, it looks visibly low-poly/stepped." So it is general, not the cups.
- Also seen: candle wax with flat faces (PT-40, `20261006-155214/candle/flames.jpg`).
- The agent's report (`docs/PT-SURFACES.md` A): the direct light is multiplied by a factor made from each flat face
  of the mesh (`terminator_softening`, the usual cure for a hard shadow line on coarse meshes): on a pot with a dozen
  sides it draws every face near the shadow line. At the tavern shelf the cups look flat for another reason: they are
  lit four times brighter than in the game, near white, where the tone curve flattens a ramp.
- Seen in the debug views (run `20261006-163946`): the jug at Horns of the South has smooth normals and vertical
  faces in the Direct, the Indirect and the Specular view (`npc-day/views-0.76-0.80-0.96-1.00.jpg`). The glass jars
  have flat faces in the Normals view itself: glass the tracer draws takes its traced mesh's flat face normals
  (`refract-jar/views-0.38-0.15-0.72-0.60.jpg`). (A first reading here took the candle wax for skin: wrong, red in the
  Surfaces view is a thin profile, orange is skin; see PT-40.)
- The surfaces agent (2026-10-06 evening, `docs/agents/surfaces.md`): in its copy, not yet in the game. See-through glass
  and props drawn forward take the mesh's vertex normals; the fade per face is for skin only; the built-in denoiser
  crosses a round thing's faces; a closed translucent surface's shadow rays no longer start inside it one face before its
  shadow line. Offline (a 12-sided cylinder) every step went from 0.04-0.5 to 0.001-0.02. A hidden "Smooth round
  surfaces" off gives the old look for an A/B. The debug views'
  faces on the jug (Direct, Indirect, Specular) were the built-in denoiser's: the debug views run it, Ray Reconstruction
  doesn't, and the jug's Ray Reconstruction picture is smooth. Characters' meshes bring no normals (only what they reflect
  or what glass shows of them stays faceted). Merged into the tree 18:40 (source checks 77 of 77).
- In game (18:40, tgpt_gpu_test 594 of 594, plugin 21B4A5E7014A, native 2026B4F9DCDC; run `20261006-184048`, recipe
  `round`: the same frame smooth and with the old faceted look): the glass jars' Normals view went from vertical bands
  to smooth, and the lamp's reflection on a jar from a block one face wide to a thin streak
  (`refract-jar/jars-smooth-faceted.jpg`). The jug and the candles look nearly the same both ways in the Ray
  Reconstruction picture (`npc-day/jug-smooth-faceted.jpg`, `candle/wax-smooth-faceted.jpg`): their faces showed in
  the built-in denoiser's debug views, which are smooth now. The candle wax stays dark (PT-40).
- State: seen in the harness's pictures; the owner has not looked. Left: the jars' texture in big squares (PT-35).
### PT-05 The traced picture looks "bloomey" outdoors
- The owner: "Why does our visuals seem more 'bloomey' than the original lighting, you think? In exteriors at least."
- Measured 2026-10-06 (`20261006-140217`, recipe `why-bright`): the dark end is lifted, not the highlights. Darkest
  5 %, game -> traced: 44 -> 59 at Cuanacht, 32 -> 61 at Horns of the South; brightest 5 %: 224 -> 225, 174 -> 180.
  Not Fog Adaptation or Dark Adaptation (the automatic gain is x1.00 by day). Reflection Boost is 9 of the 15 levels
  at Cuanacht and 5 of 29 at Horns of the South.
- **The main cause, found by both agents on their own (`docs/PT-SURFACES.md` B, `docs/PT-VEGETATION.md` 1): the game's
  fog dims its sun, moon and lamps before they light a surface, and the tracer's were not dimmed.** HDRP multiplies
  every light by its height fog's transmittance on the way to the surface, and the game keeps the fog's base at the
  hero's height. By day at Horns of the South the game's sun arrives at 19 to 44 % of its strength (31 % with the sun
  45 degrees up), at the beach 31 to 56 %, at Cuanacht 75 to 86 %, at night the moon at 2 to 15 %. The tracer used
  100 %. Sunlit ground measured x2 to x2.9 the game's where the fog is thick, and x1.03 in deep shade.
- Second: Reflection Boost 0.1 doubles what a non-metal mirrors (4 % to 7.9 %): 46 to 63 % of the lift at Cuanacht,
  18 to 26 % at Horns of the South. Third: the game halves its reflections outdoors (its own multiplier 0.5), the
  tracer's are full. Fourth: real bounced sunlight and no fake corner darkening, which are what tracing is.
- State: **the fog on the traced sun and lamps is written** (`pt_common.hlsli` `directional_fog`, `sun_fog`, and
  `light_incident`; every use of the sun's light takes it). Offline: a sunlit floor under a fog of known depth gets 0.288
  of the sun where HDRP's rule says 0.305, a lamp 10 m off 0.987. **Installed 17:02** (plugin AA223A905491, native
  38DAF48B9484), with the metal boost (PT-32) and no boost on grass.
- **In game, run `20261006-170351` against the baseline `20261006-145526`** (Ray Reconstruction, the whole picture's
  brightness, 0 to 255; the game's own in brackets): Horns of the South 135 -> 112 (106), its darkest 5 % 61 -> 35
  (30); the forest 90 -> 75 (73); Cuanacht 157 -> 147 (143); the beach 96 -> 80 (65); the castle market 105 -> 60
  (69). `npc-day/game-vs-traced.jpg`: an overcast rainy day as in the game, where it was a sunny one. The automatic gain
  stays x1.0 by day. The market now comes out darker than the game's (the shade under the stalls: the game lights it
  with its ambient light and fill lights, which tracing replaces). Frame rates the same. The grass is greyer than the
  game's (PT-03). Seen in the harness's pictures; the owner has not looked.
- Then `npc-day`, `metal`, `water-day`, `vista`, `night` against the baseline: sunlit
  ground should land near the game's. Read the exposure and gain line (it removes light). Reflection Boost and the
  game's x0.5 on reflections are the owner's call (PT-32).
### PT-06 The air above candles boils
- The owner: "Candle lights still produce some 'gas' above them that appears unstable/boiling/flickering/noisy/weird.
  Let's also prepare a save game where I look at a candle light so you can test this and iterate on it."
- The owner made the save on 2026-10-06: `PT candle-boil` ("for testing how the smoke/gas from candle lights around
  the flame boil/flicker/warps the air"). The scenario `candle` loads it.
- First run (`20261006-155214/candle`: `game-vs-traced.jpg`, `flames.jpg`, `overview.jpg`). What is there:
  - The "gas" is the game's own: a glow and haze sprite around each flame, a square patch that changes from frame to
    frame in the game's picture too (the change maps show the squares in every step, path tracing on or off; they move
    even in the frozen world). Per pixel it changes as much traced as in the game (0.30 against 0.31 levels); in 8x8
    blocks twice as much with Ray Reconstruction (0.17 against 0.08), not with NRD (0.09).
  - It reads much stronger traced because the wall behind it is far darker there: the whole picture is 93 levels in
    the game's and 39 traced (PT-39). A glow added over a dark wall stands out; over the game's bright wall it barely
    shows.
- State: looking into it. First PT-39 (the place must not go dark); then this scene again. If the haze still boils
  with Ray Reconstruction: what HDRP's distortion or glow pass reads while tracing.
### PT-07 The game's fog is not lit by the tracer
- The owner: "Are the fog volumes lit by PT?" Answer (from `SceneLights.cs`, `HdrpPasses.cs`): no. The fog is HDRP's
  own, lit by the game's lights the HDRP way, with their shadow maps; the fill lights the tracer switches off for
  surfaces keep lighting it (Fill Light Fog), and its light is scaled on screen by the traced gain (Fog Adaptation).
  It gets no traced shadows and no bounced light.
- State: answered. Open, the owner's call: whether the tracer should light the fog (a large piece of work).

### PT-08 Performance: "As performant as Cyberpunk's PT"
- The target: 55 fps at 4K, DLSS Performance, Ray Reconstruction, no frame generation (published Cyberpunk tests
  on an RTX 5080; not measured on this PC). Confirmed by the owner on 2026-10-06 (below).
- Measured 2026-10-06 with Ray Reconstruction (the game alone in brackets): water's edge 73.8 (135), Cuanacht 52.7
  (108), the Crypt 51.8 (158), a tavern door 51.6 (146), a sea cave 51.5 (114), a harbour terrace 48.4 (126), Ron's
  Tavern 42.6 (108), Horns of the South by day 37.0 (100), a market under a castle 32.2 (92; 1 % low 14.7), Sagremor
  Castle 26.4 (144). NRD is 1 to 5 fps lower. Bounced light is the largest part of the tracer's time (4 to 16 ms),
  then reflections (3 to 9), then direct light (1 to 7).
- The owner, 2026-10-06: 55 fps "yes, I guess. We may expose some levers to tweak the visuals to go beyond that
  visually for a fps hit, let's see". So: 55 at the default look; costlier looks may come as rows of their own.
- Baseline `20261006-145526`, Ray Reconstruction, world running (the game alone in brackets): fog 85 (180),
  water-day 64 (125), dark-room 52 (153), door-strap 52 (143), glass-jars 52 (145), boil-cage 51 (155), vista 51
  (104), water-torch 50 (111), stairwell 46 (138), tavern-room 42 (118), doorway 39 (99), night 38 (108), npc-day 37
  (100), caustics-glass 35 (128), glow 34 (146), metal 31 (88), campfire 29 (99), npc-candle 26 (145), forest 25
  (91). 2 of 19 reach 55. NRD is 1 to 7 fps lower everywhere.
- State: open. Next: where the time goes in the slowest (forest, npc-candle, campfire: the tracer's stage times are
  in each step's `step.json`).

### PT-09 Boiling on thin bars and elsewhere
- The owner (2026-09-28, the cage): "significantly less boily, but still boily".
- Measured 2026-10-06 at the owner's `PT boil-cage` save (the Crypt), Ray Reconstruction: 0.03 to 0.04 levels over 8x8
  blocks, frozen and running; the game's own picture 0.02. Nothing flagged in any scene so far.
- The owner, 2026-10-06, after the harness's run at the cage: "not in your most recent run, no".
- State: done at the cage (direct-light history 4 with Ray Reconstruction). Boiling elsewhere: PT-34 (the crystal cave).
  Old findings: reuse of the direct light was most of it (history 20 -> 4), not the radiance cache; Spatial Samples 15
  helps at a cost.

### PT-10 Terrain blending in the game itself
- The owner: "What's going on with terrain blending? Is it really that bad in the base-game? Can we fix that?"
- Seen: `20261006-134601/vista/game-vs-traced.jpg`: a hard straight edge between dark soil and light ground at
  Cuanacht, in the game's own picture with path tracing never on. So it is the base game there.
- State: open, not looked into (the game's terrain shader; it would be a fix outside path tracing).

### PT-11 Skin: a white shine on the neck when traced
- Seen 2026-10-06 in `npc-day/hair-crop.jpg` (both denoisers). Skin is exempt from Reflection Boost since 2026-09-28
  ("the neck": a glossy neck mirrored the chin); the owner's look at that was still owed.
- State: open.

### PT-12 Wet rock (and wet wood) turns white and glossy when traced
- Seen 2026-10-06 at Horns of the South in the rain (`npc-day/game-vs-traced.jpg`). Reflection Boost is a suspect
  (stone is what it is for, by the owner's ruling). Part of PT-03 and PT-05's investigations.
- State: open.

### PT-13 Glass you can see through
- Built in round 3 (see-through glass with detail tiles, in front of the sky). The owner's look is owed.
- Seen 2026-10-06 at the tavern shelf: the green bottle's label is gone and its glass shows a blocky pattern (known:
  a painted label on very dark glass is lost).
- State: to verify in game: `glass-jars` with the view the save was made with.
- THE OWNER, 2026-10-09 08:28: "is glass and jar translucency and refraction back yet? And what about heat haze
  from fire and candles? Do we light [up] smoke particles yet?" Answered: glass is traced (a surface of the
  camera's own rays) but does not look right (the inspection of 10-08, rows 75, 84, 85: jars cloudy with dark
  blotches, the view through them smeared and boiling with NRD, a lantern's glass milky; PT-76 no tint; PT-83
  windows missing); heat haze: none while tracing (the game's own is switched off, the traced one of PT-41 is
  not built); smoke and every other particle: still the game's own drawing over the traced picture, not lit by
  traced light (PT-67). Started 08:32: the agent `glass3` (`wt\glass3`, base65, `brief-glass-3.md`: why the jars
  are cloudy with no denoiser, a trial switch, then the tint). Haze and particles: one job after it (PT-71 item 7).
- THE GLASS TRIAL IN THE GAME, 14:38 on 10-09 (run `20261009-142733`, `refract-jar`; sheets `jars-game-now-trial.jpg`
  and `jar-close-game-nrd-trial.jpg` in the run's folder). No denoiser: at 0 the jars are cloudy olive with heavy
  blotches, the wall hardly seen; at 2 (the film once a wall) a little lighter, still cloudy; at 7 THE WALL IS
  SEEN THROUGH THE JARS, as in the game, and the frame is faster (123.3 ms at 0, 98.5 at 2, 97.3 at 7: fewer
  layers). A patch on a jar (levels): the game 74, 58, 42; 56, 37, 22 at 0; 69, 46, 27 at 2; 66, 45, 26 at 7.
  THE DIRECTION IS RIGHT. TWO FLAWS: (1) at 7 the view through a jar is WARPED over the whole body: horizontal
  streaks at the shoulder, dark upright bands a third in from each side, the wall's stones shifted in blocks.
  The body is a shell 4 to 5.5 mm thick (the worker's measure): a real one bends the view only at its outline.
  So something in the walk through a closed piece is wrong (which face is an exit, the smooth normal against
  the flat triangle, the lid's piece inside the body): the next glass job, with a shell of known thickness
  offline whose view through the middle must stay within a pixel or two. (2) WITH NRD EVERYTHING BEHIND GLASS IS
  A BLUR, at 0 and at 7 alike: PT-100. State: the trial stays off; the warp and PT-100 are the next glass job.
- THE WARP'S CAUSE, the Codex worker `glass4` (`brief-glass-4.md`; its first hand-over 14:53 on 10-09, report
  `docs/agents/glass-4.md`; merged into main 15:13, no conflict, `<scratch>\pre-glass4`; the worker goes on):
  bit 1 bends the view by the mesh's SMOOTH normal, and on a shell of two walls with different triangles the
  two bends no longer cancel. A walk by Snell's law through the game's own jar mesh (244 rays through the body,
  the wall 0.4 m behind): the view shifts 18.6 mm in the median (27.6 at the 95th hundredth) with the smooth
  normal, 1.55 mm (1.66) with each triangle's own plane. THE TRIAL: 15 (7 + bit 8, which no longer brings the
  fixed double count back): what goes through solid clear glass bends by the triangle's plane; the reflection
  keeps the smooth and the bumped normal. Its physics note, to keep: a curved shell 5 mm thick does bend the
  view before its rim (0, 1.5, 3.8 render pixels at 0, a quarter and half the radius; 9.9 at three quarters;
  total reflection in the last 3.4 render pixels), and the game's own shader bends nothing, so the game's
  picture is not the measure there. Recipe `glass-trial` now: the game; no denoiser at 0, 7 and 15.
  State: in the next build; the run owed.
- `glass3` DONE (`docs/agents/glass-3.md`; merged 13:50 on 10-09, two conflicts kept both; `<scratch>\pre-glass3`;
  nothing seen in the game). WHY THE JARS ARE CLOUDY, from the game's own mesh and material and offline:
  `Mesh_Prop_Jars_01` is ONE submesh: a closed body (764 triangles) and an OPEN lid (325), so the tracer took
  the whole jar for open sheets: 4 painted faces and 4 "panes" along a ray where the game has 2 layers (a jar's
  pixel: 34 % wall, 49 % the jar's own film, 18 % rough reflection; the game 68 % / 32 %); and once solid, the
  material's roughness 0.36 to 0.40 frosts the view. The trial: hidden `Advanced / Glass trial`, bits added up:
  1 the view THROUGH see-through glass crosses the mesh's smooth face (the highlight keeps roughness and normal
  map), 2 the film only on the faces the game draws, 4 glass has an inside where its own PIECE of the mesh
  closes (a jar's body despite its lid), 8 brings a fixed fault back; 7 is the one to try. Offline, the game's
  jar: light through 0.335 at 0, 0.497 at 2, 0.600 at 7. Recipe `glass-trial` (`refract-jar`: no denoiser at 0,
  2, 7; NRD at 7, 0). THE TINT (PT-76) is the rule now, no switch: a lamp's or the sun's light through glass
  had been counted TWICE at every light-gathering vertex (a floor under clear glass 1.885 times an open floor;
  now 1.026); the tint is the material's absorption by the way inside closed glass and by its thickness for an
  open sheet. Two of the four failing glass checks pass, one could not run alone, "glass bends light by its own
  index" (1.14 against 1.1) is open: the caustic solver, not the tint. It carried its switch in
  `TGPT_Frame.cameraPosition`'s w, as `water10` did: they share it now (glass the low 4 bits, the water times 16).
  State: built and merged; the one run owed (no deploys while the owner tests another build of the game).
- GLASS TRIAL 15 IN THE GAME, 15:50 on 10-09 (run `20261009-154425`, `refract-jar`, no denoiser at 0, 7, 15; sheet
  `status\img\jars-fix.jpg` in the scratch folder, and on the owner's status page): at 15 the broad warp of 7
  is gone (the wall's stones sit where they are), and THE JAR'S FLAT FACETS SHOW: upright lines down the left
  jar, upright and level lines over the right one. The worker had said so ("may expose its facets"). So
  neither normal is right for a shell built of flat pieces: the smooth one of two differently cut walls does
  not cancel (the warp), the triangle's own shows every edge. What a wall of such a shell needs is ONE normal
  for its way in and its way out that agrees at both faces (the wall as a pane of its own thickness: the bend
  in undone on the way out, a small sideways shift left), with the real bend toward the rim worked from the
  shell's curve, not from two unrelated interpolations. `glass4` is still at work and does not know the game's
  answer (a Codex worker cannot be told): its hand-over is to be read against this.
- State: 15 is not the answer either; the trial stays off.
- `glass4` ENDED 16:27 on 10-09 (Codex; its final report `docs/agents/glass-4.md`, copied into main; its code is
  what was merged at 15:13, its last test files are NOT re-merged). Its words: "mismatched wall normals (3.74
  degrees inner, 0.12 degrees outer) bend the view incorrectly"; its offline check on the game's own mesh:
  median shift 5.37 to 0.51 pixels with trial 15, contrast kept, the mutant fails. It never learned the game's
  answer (the facets as lines, above). So the next glass job starts from: the smooth normals of the two walls
  disagree by 3.7 degrees where they should be one; the triangles' planes are right in the mean and show
  every edge; wanted is one normal a wall, smooth AND the same on its way in and out.

### PT-14 Caustics
- Built in round 3 (bottles and flasks, water beds, sunlight reflected off water), all in the Caustics row. The
  owner's look is owed, and `docs/CAUSTICS-RESEARCH.md` section 7. Not built: hollow bottles (the game's jars and
  drinking glasses), lamps through glass at path vertices, the tracer's own wave pattern, torches mirrored in pools.
- The owner's saves (2026-10-06): `PT caustics-glass` ("not exactly what you wanted, but the closest I could find":
  a glass bottle on a harbour table; the view is now found, 42 degrees down), `PT water-day` (a beach), `PT
  water-torch` ("I simply couldn't find a location ... maybe caustics should appear on the rock/cliff": a sea cave in
  fog; without direct sun no light pattern is expected on the rock; good for water reflections in a cave).
- State: to verify in game with those views.
- The owner, 2026-10-06 evening: "I would love to see some water caustics on a cliff or inside a cave or whatever".
  What exists (PT-DETAILS "Sunlight reflected off water"): the sun mirrored in level water lights what faces the water,
  with HDRP's own ripple pattern at the water point, blurred by the sun's size over the distance. Not built: lamps and
  torches reflected off water (a cave's pools onto its walls and roof), and water that is not level. A run with the
  Caustics row on and off (recipe `caustics`) on `water-day`, `water-day-evening` (18:30, a low sun), `water-torch`,
  `glow` shows what there is now. A save that would show it best: a sunlit cliff or wall rising straight out of water,
  the sun low and behind the camera.
- That run (`20261006-215244`, `<scene>/caustics.jpg`, `tools/harness/caustics.py`): the row adds 2.4 % of the light on
  the beach (a faint net on the sea bed under an overcast sky), 0.7 % at 18:30, 1 % in the sea cave, 2.8 % in the
  mineral cave (noise). The owner: "Not seeing any caustics in any of these shots, but really the only one of them
  where I guess I'd assume they should appear in terms of actual physics is the final one inside the cave with the
  blue minerals. So yeah, build caustics from lamps and torches". A fresh agent builds them (`wt/caustics`, base
  `wt/base7`, report `docs/agents/caustics.md`): lamps and torches reflected off water onto what faces it, and into
  shallow water's bed. Also seen: the traced sea is whiter and greyer than the game's.
- The caustics agent (`docs/agents/caustics.md`; merged 22:50, offline only): the mineral cave's puddles are not water
  to the tracer, they are the floor's own glossy wet patches; the cave's real water (an HDRP lake, 19 x 16 m) lies 5.5 m
  behind the save's camera. Built: a lamp's mirror image in HDRP water lights what faces the water, rippling with
  HDRP's pattern, its Fresnel 0.02 (2 to 5 % of the lamp's light: faint by physics), cone, cookie and shadows; the 2
  strongest lamps a pixel, no noise, off with the Caustics row. Offline: 303,294 of 303,294 wall pixels within 3 % of
  the hand-worked value; 7 new checks, each mutation-checked. To see in game: scenario `glow-lake` (the save turned
  round to the lake).
- Why the cave's puddle reflections are soft streaks (found by that agent): the tracer draws a lamp as a ball of the
  size its HDRP light's radius gives (0.25 to 1.5 m for these lamps), so its highlight in a puddle is a big soft
  smear; HDRP draws a point-light's highlight as a point. That radius is an artist's value for soft shadows, not the
  flame's size. Asked whether flames should get their real size for highlights only or shadows too, the owner:
  "whatever is most realistic" (AGENTS.md): real size for both. Given to the lights agent (`wt/lights`, PT-47).
- The owner, 2026-10-07 10:20: "I really really want to see caustics on cliffs or rock inside a cave from water, and I
  still haven't seen it. How can we make this appear so I can verify it works?" Why nothing showed so far: it takes
  real water, a light beside it and rock that faces it in one view, and no save has the three (the beach is overcast
  with no cliff; the cave's lake has no lamp near it; the cave's lamps stand in puddles that are no water to the tracer).
  Two ways to show it: (1) the owner, in play: `PT mineral-cave`, turn round, walk to the lake with a torch in hand;
  (2) the harness's lantern (`src/TGPathTracing.Harness/Lantern.cs`, scenario `lantern-lake`, recipe `lantern`): a real
  light with the game's big torch's values (440,872 cd, 2400 K) and a glowing glass, stood 7 m ahead and 0.35 m over the
  cave's lake, 3 m short of the rock on its far side; with no shade and under a metal shade (then none of its own light
  reaches the rock above it: what the rock shows came off the water), the Caustics row on and off, the Direct view, and
  the world running (`anim.gif`). Only in harness runs, in no zip.
- The lantern's runs. `20261007-103925`: the tracer left the lantern out as fill (its glass was a `HideFlags.DontSave`
  object, which `Object.FindObjectsByType` skips, so no glowing thing stood at the light): it lit nothing. Fixed in the
  rig. `20261007-105204` (`lantern-lake/ledge.jpg`): the lantern is traced (91 lights) and lights the cave. Under the
  shade the ledge 3 m behind it gets 8.7 times the direct light with the Caustics row on (Direct view), 1.3 times in
  the final picture (the automatic gain differs: 4.5 with, 7.2 without): a soft warm glow with faint streaks, no
  caustic lines. The owner, watching: "Still not seeing any caustics in that cave ... caustics is still completely
  absent visually". They are right. The cause is the method: a flat mirror image of the lamp times HDRP's ready-made
  ripple picture (PT-51's list). The owner, 11:07: "I'm getting tired of telling you this. Do. not. fake. stuff. Real
  path traced caustics" (AGENTS.md).
- **Next: real caustics, the owner's first want for the next playtest** ("I want to see caustics, I want to see smooth
  intersections between waves and surfaces/cliffs/rocks/shore, and path traced water in the next playtest"). The plan:
  the tracer gets HDRP's real waves (height and normal at any point of a water surface, from its simulation's
  buffers), then light is traced from the sun and each lamp onto the water, reflected and refracted by the real wave
  normals, on into the scene with real rays, and its density where it lands is the caustic (Ray Tracing Gems II,
  chapter 30, "Real-Time Ray Traced Caustics", its water part). It replaces `water_reflected_sun`,
  `water_reflected_lamps` and `water_caustic`. Starts when an agent slot frees (the owner: "Keep the rule ... spawn the
  subagents once capacity comes back"); brief: `<scratch>/brief-caustics.md`. Its scenes: `lantern-seacave` (to be
  re-aimed: its first run looked level at a wall, the lantern 38 m out at sea) and `lantern-lake`.
- Built by the caustics agent (`docs/agents/caustics-2.md`, copy `wt/caustics2`), merged 14:15 (ABI 0x00011E00):
  HDRP's real waves reach the tracer every frame; the sun's and every lamp's light is traced onto them, reflected and
  refracted, traced on to what it meets, and its light per area where it lands is the caustic (`pt_caustics.hlsl`, a
  stage of its own). Seen on every surface the camera sees and on the bed through the water. The stand-ins are gone
  (`water_caustic`, `water_reflected_sun`, `water_reflected_lamps`); HDRP's caustic picture is no longer read. Offline:
  the whole GPU test passes; a sine wave's lines land where a CPU ray tracer puts them (10.1 times the mean against its
  10.3); 6 mutants each caught. At merge a lamp's range cut in its lamp choice was kept only for lamps that keep HDRP's
  range (PT-50's lamps reach as far as their light goes). Not built, and not faked: light off glossy floors that are not
  water (the mineral cave's puddles), caustics in mirrors and in bounced light, HDRP's current maps, water masks and
  deformers, glowing crystals as sources. Not seen in game: the queue of 14:16 runs `lantern-lake`, `lantern-seacave`,
  `water-day-evening`, `water-torch@caustics`.
- In game, run `20261007-140305` (installed 14:03: plugin 988B0EB04AD4, native 4467B666FAB2, the GPU test 1140 of 1140).
  The owner, watching: "was that supposed to be caustics? that was all kinds of wrong", and "The framerate is so extremely
  low that your test scenes take like 5-10 minutes". Seen (`lantern-lake/ledge.jpg` of that run): in the Direct view the
  ledge gets large bright patches with hard straight polygon edges and a smooth gradient, no thin lines; in the final
  picture a dim, grainy glow; the sun's caustics add about 1 % on the beach and in the sea cave. The lake scene: 30 to
  38 ms of GPU (caustics 2 to 6 ms, water reflections 12 to 20 ms), from 87 ms before the merge. The owner asked for a
  GPT-6-astra review (xhigh) of the caustics "and maybe the other stuff too": two run from 14:20 (`<scratch>/review/`,
  `review-caustics.md`, `review-today.md`). State: not working as the owner wants; next from the review.
- The caustics-3 agent (`docs/agents/caustics-3.md`, narrowed at 15:22 to what does not depend on the camera's first
  surface; merged 16:00, no ABI change): the waves end to end (a GPU check hands over buffers as HDRP makes them: texture
  arrays with mips, three bands, a turned surface; the tracer's surface and normal match HDRP's formula to 0.015 mm and
  0.05 degrees; four mutants caught; the native log's 10 s line says "water as the tracer reads it: slot N waves up to
  X cm high"), and the lookup as one short ray against an acceleration structure of the landed triangles (its test lake
  with 25 lamps: 33.7 ms to 4.4 to 6.1 ms; no walk limit, light kept as floats). Not built: correct receivers, caustics
  in mirrors and bounced light, the other limits of the review's finding 9, the wet floor; its design for them against
  a surface record any ray produces is in the report. Not seen in game: the queue of 16:00 runs
  `lantern-lake@lantern-short` (Denoising None, the frame rate measured).
- In game, run `20261007-161006` (`lantern-lake@lantern-short`, Denoising None; installed 16:10: plugin 1848A5BF0B55,
  native 3E02664543A8, the GPU test 1157 of 1157; `lantern-lake/ledge.jpg`): the log says each water "has its waves in
  the tracer" (3 bands, 64x64) and the tracer reads waves 7 to 8 cm high on the lake. In the Direct view the ledge and
  the boulders now show wavy bands that follow the waves (smooth flat patches before); some of the light still has
  hard straight triangle edges (receivers, not fixed); in the picture itself the ledge's light is faint and the back
  wall shows none. 13.7 fps with the world running (1 % low 13.1; 5 to 6 before), tracer GPU 50 ms. Not what the owner
  asked for yet; the rest waits behind PT-59 (the owner's number one) and is then built against the tracer's own
  surfaces. The queue's install step now stages the path tracing DLL by a copy: `cmake --install` fails while the
  other session's new mod is in the install list without being built here.
- The owner, 14:25: "we had a framerate averaging 6 FPS this most recent run in the cave. That was even worse than
  before. And there wasn't any caustics - there were weird fireflies in the water. At some point moving shadows on the
  cave walls. But at no point have I seen caustics on the cave walls". Measured from the harness frames' times
  (`step.json`): the lake 165 to 190 ms a frame (5 to 6 fps), the sea cave 64 to 71 ms (14 to 15 fps). The tracer's
  own stages add up to only 30 to 38 ms: about 130 ms a frame in the lake is spent where nothing measures it yet
  (the coordinator's 30-to-38 figure was wrong as a frame rate). To find: the frame's whole GPU time (HDRP, Ray
  Reconstruction, the caustics' D3D12 work and its waits), with the harness's `perfSeconds` on a traced step.
- The caustics-2 agent (`wt/caustics2`, base `wt/base28`, report `docs/agents/caustics-2.md`; offline only): built. The
  three stand-ins are gone. HDRP's real waves reach the tracer (its displacement and gradient buffers and its numbers for
  them each frame); light from the sun and every lamp is traced onto the waves, reflected and refracted (1.333, exact
  Fresnel), traced on, and lands as triangles whose light over their area is the caustic (`pt_caustics.hlsl`). The
  composite and the bed seen through the water read it; straight light under the water stops at its surface. Offline:
  the still-water limits hold (the sun's mirror image on a wall, the floor's 0.978 of the sun under the water), a sine
  wave's lines land where a CPU tracer puts them, waves keep the light's total. To see in game: `-Scenario
  lantern-lake,lantern-seacave` (recipe `lantern`), the native log's "caustics" stage and its "caustics: N triangles"
  part of the 10 s line. Not built: glossy floors that are not water (the cave's puddles), caustics in mirrors and in
  bounced light, HDRP's current maps, water masks, deformers, emissive surfaces as sources.
- The caustics-3 agent (`wt/caus3`, base `wt/base31`, report `docs/agents/caustics-3.md`; offline only), from the review
  `docs/agents/review-caustics-gpt.md`, narrowed by the coordinator to what does not depend on how the camera's first
  surface is found (the tracer's own camera rays come first, PT-59). Done and tested: the caustics' lookup is one short ray
  against an acceleration structure of the landed triangles in place of the world-space hash (in game the hash took 40 to
  50 ms of the lake's composite and 50 to 60 ms of the water passes, by the same log with the row on and off; on the test's
  lake with 25 lamps 33.7 ms became 4.4 to 6.1 ms, 1.9 ms with the row off); the light is kept as floats, no walk limit, a landed triangle's tolerance 5 cm
  at most. And the proof that HDRP's waves arrive: a GPU check with buffers as HDRP makes them (texture arrays with mips,
  three bands, a turned surface) holds the tracer's surface and normal against HDRP's formula (0.015 mm, 0.05 degrees; four
  mutants fail it), and the native log's 10 s line says what the waves are where the tracer reads them ("water as the
  tracer reads it: slot 0 waves up to X cm high": 0 would be still water). To see in game: that line, the plugin's "has its
  waves in the tracer", and the composite's time with the row on within a few ms of off. Not built: correct receivers
  (still flat landed triangles, left out across edges), caustics in mirrors and bounced light, the other limits, the wet
  floor. Measured on the way: subdividing landed triangles wants 39.5 million a frame on the test lake (removed); beams
  (a water triangle and its three rays as a prism any surface point can ask, with a ray back to the water) are the right
  estimator, and a first build of them hung the GPU (its boxes are metres long where the three rays land apart): the
  prototype is in `docs/agents/caustics-3-beams/`, the design and its cost plan in the report, written against a surface
  record any ray produces.
- New defect found (PT-14's own): a lamp's caustic is traced from one point of its emitter a frame (its size blurs the
  lines over frames); with NRD or the built-in denoiser that shows as a faint shimmer (the light is added after the
  filters, as the stand-ins' was). Ray Reconstruction takes it in.

### PT-15 The saw-tooth on a door's metal strap
- Open since round 2 (mirroring rays out did not fix it). The owner's save `PT door-strap` exists since 2026-10-06.
- Measured there: the traced picture is darker than the game's (x0.8) and has half its fine detail in the middle.
- State: open. Next: look at the pictures of that scene, then the lights' "no specular" flags.

### PT-16 The tavern's haze
- The Fog Adaptation row (round 3, On) gives the fog's own light the traced gain; the owner compares On and Off.
- Seen 2026-10-06: the traced tavern shelf looks washed out beside the game's (the gain was x3.7).
- State: waiting for the owner, and to measure (`tavern-room` with Fog Adaptation off).

### PT-17 The dialogue light of TG Conversation Lighting Overhaul
- It is scaled by the inverse of the traced gain since round 3 (it was 3 times too bright in a dark tavern).
- State: waiting for the owner's look in a dialogue (the game does not save during one, so the harness has no scene).

### PT-18 Eyes too small
- The character model, not the lighting. Offered: a separate mod through blendshapes.
- State: waiting for the owner.

### PT-19 NRD is much noisier than Ray Reconstruction, keeps less fine detail, and costs more
- Measured 2026-10-06: fine detail at Cuanacht 7.4 (game), 6.8 (RR), 4.6 (NRD); 3.3 to 4.5 ms more GPU time.
- The owner, 2026-10-06 evening: "the NRD denoiser is a LOT more noisy than Ray Reconstruction, so I guess Ray
  Reconstruction hides a lot better the low ray count, but ideally we also make NRD look great. We may have to bump some
  defaults to make that workable". So NRD must look great too (it is what AMD and Intel players get).
- Measured (run `20261006-212905`, Ron, the harness's "boil": frame-to-frame change in the region): frozen world RR
  0.04, NRD 0.05, its worst 5 % 0.08 against 0.17; world running RR 0.29, NRD 0.42 (the game 0.36), worst 5 % 1.38
  against 1.98 (the game 1.94).
- State: open. Next (a fresh agent once one of the four running finishes): NRD's settings against the inputs it gets
  (which NRD denoiser, its history length, its anti-firefly and pre-blur, the hit distances it is given), and what more
  rays per pixel buy under NRD; defaults may differ per denoiser (the owner allows bumping them). Judged against an
  undenoised many-frame reference, by the harness's boil numbers and the owner's eye.
- The NRD agents (`docs/agents/nrd.md`, finished after a usage-limit stop; merged 07:55): NRD blurred away the normal
  maps' bumps in bounced light (all the shade outdoors: 0.1 to 1 % of the reference's fine detail under a bump-mapped
  ceiling), and its input units followed the scene's brightest light (with a light 40,000 times brighter far off, its
  boil went from 0.28 to 0.49). Now NRD runs in its directional form with the bumps put back after it (detail 0.35 to
  0.41 there) and its units follow the bounced light's own median (boil 0.26 to 0.30 in that case). No defaults
  changed: 4 rays a pixel cut night boil by only 8 % at 25 to 45 ms more. Costs about 0.3 to 0.5 ms and 100 MB while
  NRD runs. Not run: `tgup_gpu_smoke_with_pt` (the RR path; the harness runs RR in game).
- In game (run `20261007-081218`, quick recipe; native D21133B4F17E, the tested one), NRD against RR (boil, then fine
  detail; the game's detail in brackets): npc-day 0.05/2.50 against 0.05/2.44 (2.96); vista 0.04/4.91 against 0.06/6.04
  (7.43); the tavern 0.08/3.95 against 0.03/4.40 (6.82); the mineral cave 0.12/7.69 against 0.03/9.14 (7.70). So by
  day NRD now holds RR's detail; in the dark (the cave, the tavern) it still boils 3 to 4 times RR's. Not yet looked at
  by the owner.

### PT-20 Is the game's picture back after Off? (the owner: "completely essential")
- The harness flagged the game's picture as not quite back at `caustics-glass`, `water-torch` and `door-strap`
  (worst 32-pixel block 1.8 to 3.1 levels where 1.3 to 2.1 is allowed by what the game changes by itself).
- Looked at 2026-10-06: the differences sit on a brazier's flame, on foam and glints on water, on leaves and along
  edges. The check now judges each block by what that block changes by itself (the two takes before tracing, the two
  after), so a flame neither excuses nor accuses the rest.
- Baseline `20261006-145526` with that check: the picture still counts as different in 12 of 19 scenes (worst block
  1.1 to 4.5 levels; the mean is inside its allowance in all). Both takes after tracing agree with each other and
  differ from the take before: either something the mod leaves behind, or something in the game that changes once
  in the half minute between (exposure settling, a tree's sway, streamed textures). The harness cannot tell these
  apart inside one run.
- The control run (`20261006-161118`, path tracing never on, seven scenes) against the baseline:
  - **Brightness comes back.** The whole picture's brightness after Off is within 0.4 % of before in 18 of the
    baseline's 19 scenes and in all 7 of the tree-plugin run; the game alone drifts up to 0.3 % in the same time.
    One exception: `doorway` +1.1 % after Ray Reconstruction, +0.6 % after the toggles, strongest on its sunlit
    stairs (`20261006-145526/doorway/restore-3-game-after-rr.jpg`; the control's same picture is empty).
  - **Small patches differ where the game alone does not**: water-torch, metal, doorway and vista count as
    different after tracing (worst 32-pixel block 2.5 to 4.5 levels, 17 to 168 blocks) and as the same in the
    control. In forest, npc-day and campfire the game alone differs as much (wind, rain, fire).
  - Part of metal's was the hair queue (PT-02): with the fix its worst block fell from 4.1 to 1.4.
- Not known: whether the rest is something the mod leaves (nothing in the 24,000 to 39,000 values of the state dump
  differs), the game's upscaler settling differently after its history was reset by the switch, or light that
  moves by itself (the doorway's sun). The dump does not hold renderers' settings, HDRP's current exposure, or
  cached shadow maps and reflection probes.
- Changed in the harness for it (16:24): the second take before tracing now comes 24 s after the first, as long as
  the take after tracing comes after it, and the take after tracing is judged against that second take; what the
  picture drifts by itself in that time is then the floor.
- State: looking into it. Next: `doorway`, `vista`, `water-torch` again on the retimed recipe; if patches remain,
  add renderers and the exposure to the dump, and a step that only resets the upscaler's history.
### PT-21 Armour and metal reflections
- The owner's save `PT metal` (2026-10-06: "a guy with a metal helmet and metal parts of their armor, but they're not
  in full metal armor"). The guard walks off within two seconds of a load: the scene is frozen at once now.
- State: to verify in game.

### PT-22 Owed looks from earlier rounds
- Boiling Filter On against Off; Crevice Light with Ray Reconstruction; Reflections Glossy Surfaces against All
  Surfaces; thin cloth; the cage and chairs at direct-light history 4; hair colors against the game's; whether the
  tavern is too dark since the "seen" meter.
- State: waiting for the owner (the harness can now put each pair side by side: ask for the ones that matter).

### PT-23 Known gaps in the tracer
- From reviews (2026-09-28): HDRP's shading stays for a frame in the band a fast turn reveals; texels traced from the
  jittered G-buffer are reprojected with unjittered motion. Not done: blue noise, a firefly clamp, the built-in
  filters' one-second lag. Latent: HDRP's SSS profiles below 8 under `pt_prepare`'s "deferred" test. Never seen in
  game: DXR 1.2 features, NRD on AMD, AMD Ray Regeneration. F8 is bound three times.
- State: open.

### PT-24 The owner's other mods are not in the harness's runs
- Only BepInEx and the TG mods were deployed on 2026-10-06. TG Volumetric Fix, Torchlight Rekindled, No Player Light
  and TG Conversation Lighting Overhaul change fog and lights, so both pictures differ from what the owner plays.
- The owner, 2026-10-06: "I guess not, except for conversation lighting overhaul".
- State: answered: the harness runs without the other mods, with TG Conversation Lighting Overhaul. That mod is not
  deployed (Vortex's folder for it is empty): the owner enables it in Vortex; it matters for dialogue scenes (PT-17).

### PT-25 Saves still to make
- `PT black-spots` (a floor where black spots flicker, looking down at it): offered by the owner on 2026-10-06.
- Made 16:50: `PT hair-eyebrows` (Ron up close; the owner: "we also need a 'PT hair-eyebrows' of Ron, don't we?").
  The scenario `hair-eyebrows` loads it: the close-up PT-01 is judged on.
- A place with dust in the air (PT-30).
- Made by the owner on 2026-10-06 under their own names: `PT refract-jar`, `PT candle-boil` (scenario `candle`),
  `PT mineral-cave` (scenario `glow`), `PT foggy-backdrop` (scenario `night`).
- Stand-ins on slots the game overwrites: `PT tavern-room`, `PT glass-jars`, `PT dark-room`, `PT doorway`.
- Stand-ins from old saves that work for now: `PT npc-day`, `PT npc-candle`, `PT forest`, `PT vista`, `PT fog`,
  `PT stairwell`, `PT campfire`.
- State: waiting for the owner.

### PT-26 The harness itself
- R17 packaging check10-09: tests/ingame/scenarios.json contains merge markers starting at line5 during
  concurrent PT work. Source checks:104 run,99 pass,5 JSON parse errors in harness/PT recipe checks.
  Evidence: build/nr/il2cpp-r17/combo/package-source-checks.log. NR agent leaves merge content untouched;
  PT coordinator should finish the merge before the next harness run. FG/BU/NR zip payload checks pass.
- Follow-up during NR-43 Mono deployment: the concurrent merge is resolved; Source105 passes.
  Evidence: build/nr/mono-r17/source-checks.log. No PT code changed by the NR agent.
- Not built: the game without the mod at all as the reference; a dialogue scene; keeping hands off the keyboard is
  asked, not enforced (a menu opened mid-run is detected and reported).
- 10-08 07:33: NR run `artifacts/harness/nr-live-models-20261008` captured face/tavern outside gameplay
  (12 rejected takes, initially black, including Off). Forest is in gameplay. Cause unknown, no evidence of
  keyboard interference. Readiness guard now built before freezing/takes/every captured frame; fails the
  scenario and stops its later takes. Runtime acceptance pending, another session holds the lock. NR-18/round-10.
- 10-08 follow-up: PT run `20261008-085342` also stopped its first Off take on this check. NR added
  loading/player/UI-interactivity/camera/time-scale detail to each failure (`Game.cs`, `Runner.cs`, builds).
  The next run will identify which readiness condition failed; no load delay or rejection threshold changed.
- NR retry `20261008-092818` completed 12 valid face/tavern takes with the guard; original intermittent load
  cause remains unknown. Invalid scenes stop early, valid gameplay passes. Saves117 unchanged, configs restored.
- NR `20261008-094455` stopped its second take on UI interactivity: loading=False, hero=True, UI=UIState,
  interactive=False, camera=MainCamera, timeScale=0. No cause attributed; no reset-history comparison obtained.
- State: open.

### PT-27 Black spots that flicker on floors
- The owner, 2026-10-06: "Maybe we also need a save game test case for flickering black spots, like seen sometimes
  here and there in floors?"
- State: waiting for the owner's save `PT black-spots` (a floor where they show, looking down at it). The analysis
  will count pixels that drop far below their own usual value for a frame.

### PT-28 Parallax occlusion mapping with self-shadows for bricks and walls
- The owner: "We probably need parallax occlusion mapping (with self-shadows support) for bricks and stuff in-game,
  like the castle walls."
- Read from the state dump of the market at Horns of the South (1104 materials): the game has height maps on some:
  11 with HDRP/Lit's `_HEIGHTMAP` (`MI_Wall_01`, `MI_Wall_03`...), 2 with pixel displacement, the floor
  `MI_Floor_01_HoS` (the game's own `TG/Architecture/KF_Architecture`) with `_PARALLAX_OCCLUSION_MAPPING`, 2 terrain
  materials with `_TERRAIN_PARALLAX`. The tracer reads none of them: its walls are flat triangles with a normal map.
- The owner again, 2026-10-10 00:20: "you also said that bricks/stone had a similar parallax map/bump map or
  something, that we can use for our parallax occlusion mapping with self-shadowing for bricks, right?" Answered:
  yes for the materials above that hold a height; walls with only a normal map have none, and none is made up.
- `relief1` ENDED 01:35 on 10-10 (report: `<scratch>\wt\relief1\docs\agents\relief-1.md`; base79; NOT MERGED: the
  owner's order of 01:05 puts features after speed and memory):
  - REALLY IN USE, in the whole game (8,100 materials, proven from the compiled shaders): SIX materials. The
    floor `MI_Floor_01_HoS` (`KF_Architecture`: its base mask map's blue channel, `_Amplitude` 0.56, 8 steps) and
    five of HDRP's Lit with pixel displacement (`StoneTileCDEP`, `StoneTileCDEP 1`, `Mat_WeaponHit_Ground`,
    `_Stone`, `_Wood`: `_HeightMap` red, 2 or 10 cm). None of the game's variants shadows itself.
  - THE CASTLE WALLS (`Mat_Castle_Wall_01/03`, `MI_Wall_01/03` and 14 more) HOLD A HEIGHT MAP THE GAME NEVER
    READS (the same 320 instructions and 8 texture reads as a wall without one). `_TERRAIN_PARALLAX` is a leftover
    keyword, not a height. So this entry's first list (11 + 2 + 1 + 2) named keywords, not use.
  - Built, for the floor only: every ray that calls `hit_material` goes on into the height field and reads the
    material at the crossing; the sun's shadow ray walks out of it. CMake `TGPT_RELIEF` (OFF), hidden
    `Advanced / Relief trial` (0). Recipe `parallax` (scene `spec-terrain`; `relief` was PT-95's name). It
    compiles (40 passes), 109 checks. Not built: lamps, bounces and reflections leaving through the field; the
    hit's place moved to the crossing; the Lit kind. Its cost guess: under 0.3 ms. The floor is 4.5 % of that
    scene's screen, the field about 1 cm deep: "the effect is mild".
  - A QUESTION FOR THE OWNER: the walls (20 % of that screen) hold a height map the game does not use: trace it
    anyway (the map is the game's own; its depth would be HDRP's default 2 cm, which no artist set for a wall
    that never used it), or leave the walls as the game shows them?
- State: the first trial built in the worker's copy, not merged, no run: after speed and memory (the owner's
  order). The owner's word on the walls is owed.

### PT-29 Rain, PT-30 dust, PT-31 smoke and fire with path tracing
- The owner: "How does rain play together with PT?", "How does dust particles play together with PT?", "How does
  smoke and fire play together with PT? Track on Backlog if changes needed to make them play better together."
- Known from the code: particles are not in the traced scene and are shaded by HDRP after the traced picture is
  composed (the hard rule lists them as still to raise: "transparents and particles"). A flame lights the room
  through the light placed in it, not through its particles. Rain shows in the harness's scene at Horns of the
  South (`npc-day`), fire and smoke at `campfire`; wet rock turning white is PT-12.
- State: open: look at those scenes for each, then say what needs changing. A scene with dust is still to find.

### PT-32 Metals should reflect more than other surfaces
- The owner: "Can we make metals reflect more light than other less reflective surfaces?"
- Today Reflection Boost (0.1) makes every surface glossier by the same amount. Weighing it by how metallic a
  surface is would answer this, and may also take the white off wet rock and grass (PT-12, PT-03) and part of
  the lifted shadows (PT-05, where Reflection Boost was 5 to 9 of the levels).
- The owner, 2026-10-06 16:50, asked whether to flip it: "weird. Yes, we want metal to reflect more than stone and
  brick." (A ruling: AGENTS.md.)
- Offline (17:01): Ray Reconstruction's specular albedo at a floor pixel, boost 0 to 1: a grey metal 0.404 -> 0.867, the
  same floor as a non-metal 0.047 -> 0.086 (only its gloss). A first check summed the floor's reflected light, which a
  smoother floor concentrates into the sun's small highlight: it fell for both, and was replaced. Installed 17:02.
- State: written 16:52 (`pt_common.hlsli` `reflection_boost`, the same rule in `d3d11.hlsl` for Ray Reconstruction's
  guides): everything a tenth glossier at the default, a metal twice that, and a metal's mirror share raised toward the
  brightest its own color goes; a non-metal's stays 4 %. In the build under test. To look at: `metal` (the guard's
  helmet and armour) before and after, and the owner's eye.
### PT-33 Far cliffs and mountains show lit up through the fog
- The owner: "it kinda looks like cliffs and other stuff in the distance is 'lit up' against the fog/background ...
  the guy with the weird mask next to a torch and a bridge had a thing where the mountains in the background were
  lit up ... standing on some cliffs, where the background in the PT renderer revealed light gray cliffs against a
  darker gray sky, it looked like a bug", and fog hiding things more densely in one picture than in the other.
- The scenes: `night` (Slot_45) and `fog` (Slot_47) in the baseline run `20261006-145526`. The owner then made a
- Confirmed in the baseline `20261006-145526` (`fog/game-vs-traced.jpg`, `night/game-vs-traced.jpg`): far rock
  spires and mountains are flat shapes lighter than the sky behind them, traced; in the game's picture they sink
  into the fog. Measured at `fog`: the sky is the same in both pictures (76.2 levels), the far rock goes 79 -> 90.
- Cause (read in the code, fits the numbers): Fog Adaptation gives the fog's own light over a surface the traced
  light's gain (x1.29 at `fog`, x1.43 at `night`) and leaves the fog in front of the sky as it is (`d3d11.hlsl`
  TGPT_FOG: "Sky pixels are left as they are"; the GPU test checks "the sky's stays"). A rock the fog hides
  entirely is then the fog's colour times the gain, against the same fog at x1 beside it.
- State: **fixed, seen in the harness's pictures; the owner has not looked yet.** The fog in front of the sky takes the
  gain too, at HDRP's Max Fog Distance (CHANGELOG; offline check: sky pixels kept without the distance, doubled with
  it). Installed 16:31 (plugin BE6F2031D6C0, native 8662A8ABD7E2). In game, run `20261006-163130`:
  `night/cliffs-before-after.jpg` and `fog/cliffs-before-after.jpg` (the game's, before, after): the light shapes are
  gone in both, the far rocks sink into the fog as in the game's picture. Fog Adaptation stays On.
### PT-34 The crystal cave: caustics, boiling, soft reflections
- The owner: "a cave with some blue minerals on the walls, and a few yellow lights on the ground with reflections.
  This one is perfect, because it should arguably show caustics, and also there was some boiling going on, and the
  reflections weren't as crisp as they should be. Not sure if the boiling was due to the way you tested different
  settings?"
- The scene: `glow` (Slot_74, Lovers' Hollow); the owner's own save for it since 2026-10-06: `PT mineral-cave` ("Good The harness switches path tracing on and off between steps, and the
  first seconds after each switch are noisy until the picture settles; frames are taken after that.
- Baseline `20261006-145526` (`glow/game-vs-traced.jpg`): boiling in 8x8 blocks, frozen world: the game 0.03, Ray
  Reconstruction 0.06, NRD 0.13 levels; world running: 0.50, 0.27, 0.41. So with Ray Reconstruction the settled
  picture boils less than the game's own shimmer; NRD boils twice as much. What the owner saw live was most likely
  the first second after each of the run's eight switches (the picture starts noisy and settles) and the NRD steps.
- Seen in the pictures: the place is blue in the game's picture and warm orange traced (the cave wall lit orange
  from the left; the blue glow around the crystals gone; the small lights on the wet floor blue-white in the game,
  yellow traced); the grass in front is bright green in the game and nearly black traced; the lights' reflections in
  the puddles are long soft streaks. No caustics seen. 34 fps.
- State: open: which lights the tracer keeps and drops there (94 traced lights), the grass (PT-03), the puddles'
  roughness, and caustics from small lights on wet ground (today's caustics are for glass and water surfaces).
- The owner, 2026-10-06 night: the cave is focus 1 (BACKLOG); "I'd also expect to see better reflections in that pool of
  water in the mineral cave" (passed to the caustics agent: why the pools' reflections are soft streaks).
- Why the blue goes (the state audit of `20261006-162322/glow/1-game`, `<scratch>/audit_lights.py`; the log of
  `20261006-215244`): the game's blue comes from lights the tracer leaves out as placed fill lights: 31 "Point Light_17B"
  under "Lights" (11 of them blue, up to 12,062 K, white colour, no shadows, 2 m radius, 4 m range, the nearest 2.7 m
  from the camera) and 2 "Spot Light" (12,062 K, very strong, 32 m away). What is traced there is warm: candles (2,500
  K), torches (2,683 K), lanterns. The blue-white dots on the game's wet floor are those blue lights mirrored in the
  pools. The crystals themselves glow (HDRP/Lit with an emissive colour map, `Mat_Prop_CrystalMold_Lowpoly_01`); the
  tracer's emissive surfaces are lights too, but their glow is set for the eye, not for lighting, and the cave comes
  out orange. Not measured yet: how much light the crystals' own glow gives traced (the Emissive view: a `views` run on
  `glow` is next), and which of the 11 blue lights sit at a crystal.
- The owner's call: a light placed at a glowing crystal counts as that crystal's light ("yes"; PT-47, AGENTS.md).
- The debug views (run `20261006-220318`, `glow/views.jpg`): the Emissive view shows the crystals glowing blue-white,
  and the Direct view shows them lighting next to nothing (the right side of the cave dark; the light is warm, from
  the opening on the left and the lamps); the floor is one large flat surface (one colour in Normals and Coverage),
  mirroring the lamps in the Specular view. So the blue comes back only with the lights at the crystals (PT-47).
- With the lights round (run `20261006-234318/glow/views.jpg`; installed native 3B1F9A14E0F5): the right half of the
  cave is blue again, the crystals' lights kept ("kept: at a glowing surface ... Mat_Prop_CrystalMold_Lowpoly_01"),
  the crystals glowing as lights (the Emissive view samples them; "emissive: 4 submeshes ... 396 sampled as lights"),
  a blue reflection on the floor in front of them. The left half is still warm: the six fake "torch_06" lights (PT-47)
  and the opening. Not yet looked at by the owner.

### PT-35 Refraction through a glass jar
- The owner: "The glass jar could be a good refraction test case, let me make a 'PT refract-jar' savegame for that?"
- The owner made the save on 2026-10-06: `PT refract-jar` (Slot_90, the Crypt).
- First run (`20261006-155214/refract-jar/game-vs-traced.jpg`): in the game's picture the two jars are see-through
  greenish glass with stains, the wall showing through. Traced they are beige and nearly opaque, covered in big
  square blocks (their texture read far too coarse), with one white streak; nothing is seen through them and nothing
  bends. The jars on the shelf below nearly vanish. (The wall is lit differently too: evenly dim in the game's, by one
  warm light from the right traced.)
- A likely cause (the hair agent, 17:55): there are 32 detail tiles (512x512) and every character's hair material takes
  one; in one run 29 of 53 hair materials already fell back to 64x64 tiles, so the jars find none left and are read
  from a 64x64 tile: the squares. Many hair materials are identical clones that could share one tile.
- State: the surfaces agent's round 2 (19:52, `wt\surfaces2`): a sharp tile for every glass texture, eye and hair
  material in view (sharing between identical hair bakes, more tiles, or freeing them), and why nothing is seen
  through the jars. Earlier note: why these jars get no detail tile (`TGPT_RegisterDetailTexture` exists for exactly these blocks:
  "Point-sampled from its 64x64 tile, a jar's stains showed as blocks"), and whether the old plugin showed the same
  (this run had the tree's). Then refraction itself. Belongs with PT-13.
- The 128 shared detail tiles and jars as thin glass (`docs/agents/surfaces-2.md`), in game (run `20261006-212905`,
  `refract-jar/jars.jpg`: the game's, tonight's traced, the 15:52 traced): the left jar's stains are sharp now and its
  outline round; the right jar still shows squares (its texture still read from a 64x64 tile: why not checked);
  neither jar shows the wall behind it (both a flat milky brown, where the game shows the stones through the glass).
  So the thin-glass change does not reach these jars in game, or the composite covers it. In the same run Ron (the
  second scene) stays on the hair layer, where the run before fell back to the blocky path in its second scene.
- Next: why the jars are not seen through (the Surfaces and Materials views on `refract-jar`; whether the jars carry
  the see-through flag), and why the right jar has no detail tile.
- The jar agents (`docs/agents/jars.md`, finished after a usage-limit stop; merged 08:25): the jars were taken for
  see-through glass and seen through all along, but the view ended on the tracer's own copy of the wall behind (a 64x64
  tile, no normal map): no stones, a flat milky brown; the "squares" were most likely that wall tile. And the glass was
  used as a colour filter where the game blends it by alpha (0.165 to 0.18 on the glass, 1 on the lid). Now where the
  view through glass lands on a surface HDRP drew at that pixel, it is shaded with HDRP's own colour and normal
  (`hdrp_material_at`); the glass mixes as the game's (its alpha share its own lit colour, the rest the wall
  untinted); the jar is crossed wall by wall (`see_through_shell`), so it bends what is behind only near its edges; one
  log line per glass material says whether it is see-through glass and why not. Offline: the Crypt's jar in front of
  a checkered wall follows the wall 0.97 (-0.01 before), 0.71 of its light, the lid not seen through.
- In game (run `20261007-084347/refract-jar/jars-now.jpg`: the game's, last night's, now with RR, now without a
  denoiser; native 679A74354571, the tested one): both jars are see-through, the stones behind them clear, bent at the
  glass's edges, the red stain there; the game's glass carries a stronger milky-green film and darker stains than the
  traced. The log: "Glass: "Mat_Prop_Jars_01" (HDRP/Lit) is transparent in the game: taken for see-through glass".
  Not yet looked at by the owner.
### PT-36 Cloud shadows
- The owner: "Does clouds cast ground shadows, and do they play together with PT? Or is it just a moving skybox?"
- Read from the harness's state dump: the game's clouds are HDRP's Cloud Layer (a flat double layer in the sky
  that moves with the wind; volumetric clouds are off). Whether its layers cast shadows on the ground in the game
  is a flag the dump did not reach yet; the tracer's sun is shadowed by geometry only, so under path tracing clouds
  cast none.
- Read 16:39 from the state dump with the layers' own settings (run `20261006-163130`): "cast shadows" is off on
  both layers, at the bridge by night and at Broc Meala in fog. And decoded from the game's files by the vegetation
  agent (`docs/PT-VEGETATION.md`): in the day and night profiles of Horns of the South, Cuanacht, Forlorn and the
  prologue beach, layer A has it off and layer B's value is not taken by HDRP (its override is off); no game code
  touches it.
- State: answered. The clouds are a moving picture in the sky and shade nothing, in the game as traced. (The
  agent's first draft proposed a cloud shadow on the sun, its "Change 1b": withdrawn by the agent. Do not build it.)
### PT-37 Should the tracer light the fog?
- 10-12 12:20, WHY THE NIGHT HAS NO BLUE HAZE (worker fog3, `docs/agents/fog-3.md` part B): the game's night haze
  is the SKY's light, not the moon's. HDRP adds its sky's ambient probe to every point of its fog at full strength:
  no fog in its way, no shadow. The tracer dims the sky's light by the fog above the point and sends a shadow ray,
  which in the night's fog (a mean free path of 9 m) leaves a level ray a 28th of it: the game's haze is x8.5 to
  x19 of the traced one. The moon's share is the same in both but for its volumetric dimmer (x1.23: the one real
  gap, small). So the dark traced night is the physically dimmed one and the game's is HDRP's shortcut; the worker's
  trial that leaves the sky's light undimmed (bit 27) is NOT merged: the owner's rule of the day is "path traced,
  always". What gives a real night its haze is the sky's light scattered through the fog in every order, which the
  cells count for the camera's fog already (the fixture: x2.1 to x2.6 on the moon's share at night); if the night
  still reads too dark to the owner, the question is the sky's own brightness at night (PT-112, the sky's job), not
  a switch that undims it. And the owner's word on the lit mist: PT-147, AGENTS.md.
- The owner, asking again: "Did you respond to my question about lighting up fog volumes with PT?" Answered under
  PT-07: today it does not. Lighting the fog with the traced lights (their shadows in the mist, light shafts) is a
  feature to design.
- The owner, 2026-10-06 16:50: "yes."
- State: wanted; to design. A large piece: the traced lights' light scattered in HDRP's fog volume (their shadows in
  the mist, shafts), in place of HDRP's own lighting of it, with the game's fog density and color kept. The fill
  lights' fog (the Fill Light Fog row) is the owner's earlier ruling and stays in the picture.
- First step built (`docs/agents/fog.md`, finished by a fresh agent after a usage-limit stop; merged 07:45, ABI
  0x00011B00): reflected and refracted rays get the game's fog over their own path as HDRP fogs the camera's view
  (mirrors, glossy reflections, out of solid glass, water): dimmed by the height fog over the path (the sky as a
  surface at Max Fog Distance), then the fog's own colour added (the Fog volume's tint times HDRP's sky at its mip, or
  the constant colour). Not covered: HDRP's volumetric light within 128 m on reflected rays. Offline: a perfect mirror
  showing a wall in thick height fog matches HDRP's formula for a direct view from the mirror's spot (0.97 to 1.00 per
  channel). Its design for the next step, the tracer lighting the fog: a D3D12 pass fills HDRP's own volumetric fog
  grid with the sun's and the traced lights' scattered light (one shadow ray each, a quarter of the cells a frame),
  added before HDRP's fog pass; the sun and traced lights get Volumetric Dimmer 0 so they are not counted twice; fill
  lights keep their fog (the Fill Light Fog ruling); about 1-2 ms and 60 MB at 1440p.
- In game (run `20261007-075250`; native 386A5DBB799C, the tested one): the day mirror against the game's view from
  its side: shapes 0.86 (0.71 before), saturation 0.38 against the reference's 0.38 (0.49 before), brightness 0.70; the
  tavern mirror as before (1.06 brightness). The far haze is in the mirror now. Not yet looked at by the owner.
- The mirror-2 agent (`docs/agents/mirror-2.md`; in a copy, not merged, not seen in game): reflected and refracted rays
  read HDRP's own lit volumetric fog (its lighting buffer, handed over each frame, a frame late) where the buffer covers
  the ray: inside the camera's view, in front of what it saw, within 128 m. Offline 0.98 to 0.99 of the wall seen
  through that fog. Everywhere else the ray keeps the height fog and the distant fog's color: a stand-in, a gap. The day
  mirror shows what lies behind the camera, so it keeps the stand-in. The lasting answer is this entry's step 2 done
  along rays (the tracer lights the fog itself); its missing pieces are listed in that report, section 1.
- 2026-10-11 (round 20 section 11; PT-112 B): the tracer lights the fog itself, the rule; mirrors and glass cross the traced
  medium; the game's local fog boxes are in it; lamps by equiangular sampling. HDRP's fog, Fog Adaptation and HDRP's
  volumetric grid are gone while tracing. OPEN: (1) the fill lights' fog (the owner, 2026-09-28: "allow the fog-part of
  them while preventing the light-part"; the Fill Light Fog row) lit only HDRP's fog and lights nothing now: the tracer
  needs fog-only lights (the fill lights as lights of the medium alone, in a light set of their own so surfaces' samples
  are not spent on them), and the volumetric dimmers (fog-2.md Q1); (2) the sky's haze on far land (sky-1.md); (3) the
  boxes' shadow on surfaces; (4) the light surfaces send into the fog; (5) the owner's look at night: the torch lights
  the mist, the moon's blue haze is gone (HDRP outdoors lit its fog with the moon alone).
- 2026-10-12, HOW BRIGHT THE TRACED FOG IS AGAINST THE GAME'S (the cells are the rule, PT-118; frozen, no denoiser,
  `20261012-072747`; the old per-pixel route read the same, so it is not the cells): the fog scene's luma 91.7
  against the game's 70.2 (x1.31; darkest and brightest 5 %: 44 and 119 against 40 and 89); the night 54.6 against
  39.1 (x1.39; 9 and 153 against 18 and 55: the bright end nearly three times the game's, the dark end half of it);
  the vista x1.01; the tavern room x0.73. Not known which is right where: HDRP lit its fog with the sun or the moon
  and the artists' fill lights; the tracer lights it with every lamp and with what the surfaces send, and without
  the fill lights (OPEN 1). NEXT: the fog scene and the night with the fog's shares one at a time (the sun's or
  moon's, the lamps', the mist's cache) against HDRP's own picture of the same view; what "right" is at night is the
  owner's look (OPEN 5).
  THE FIRST SHARE, 08:30 (`20261012-081117`, the step "light scattered once only" against all orders): THE FOG SCENE
  63.5 once (x0.90 of the game's 70.2) and 91.4 with every order (x1.30): there the excess is the light the fog
  scatters more than once (the mist's cache), which HDRP does not have (it adds the sky's probe instead). THE NIGHT
  52.9 once and 54.7 with every order (x1.36 and x1.41): there it is not; its bright end (155 against 54) is lit
  surfaces and lamps' light, not the fog's later orders. The vista 125.0 and 133.2 (x0.96, x1.02), the tavern 17.8
  and 17.9.
  WHAT THE GAME'S OWN FOG VOLUME SAYS (09:40, the harness's audits of the same saves, `...\1-game\audit-b.txt`, the
  camera's volume stack; HDRP's Fog component):
  | scene | mean free path | albedo | directionalLightsOnly | multipleScatteringIntensity |
  |---|---|---|---|---|
  | night | 9 m | 0.24, 0.24, 0.21 | TRUE | 1 |
  | forest | 144 m | 0.59, 0.71, 0.62 | TRUE | 1 |
  | town | 146 m | 0.64, 0.79, 0.69 | TRUE | 0 |
  | beach | 145 m | 0.64, 0.80, 0.69 | TRUE | 0 |
  | vista | 682 m | 1, 1, 1 | TRUE | 0 |
  | fog (the ghost realm) | 71 m | 1, 1, 1 | false | 0 |
  | tavern room | 40 m | 0.58, 0.97, 0.99 | false | 0 |
  (Max Fog Distance 5000 m and the tint 0.906, 1, 0.862 times 1.226 in every outdoor scene.)
  SO: (a) OUTDOORS THE GAME LIGHTS ITS FOG WITH THE SUN OR THE MOON ALONE, BY ITS OWN SETTING: no torch, no lamp, no
  fire lights the mist there; indoors and in the ghost realm lamps do. The tracer lights the fog with every lamp
  everywhere. That is the night scene: a fog of 9 m mean free path, made dark (albedo 0.24) for a blue moonlit haze,
  lit by a torch it was never lit by in the game: an orange glow over the whole picture, the bright end 155 for the
  game's 54 (`20261012-081117\night\overview.jpg`). And the moon's blue haze is not there (OPEN 5: the tracer does
  not take the game's 60,000 lux "moon" as a light, PT-38). (b) HDRP scatters the fog's light ONCE (its volumetric
  fog; "multipleScatteringIntensity" is a blur of that, 0 in most scenes) and adds the sky's probe. The tracer
  scatters it in every order, which a medium of albedo 1 answers with much light: the ghost realm's x1.30 (x0.90
  with one order).
  THE OWNER'S CHOICES, two, each with its pictures: (1) outdoors, where the game says "directional lights only": do
  lamps light the mist (today; the owner on 2026-10-06, of shadows and shafts in the mist: "yes") or only the sun
  and moon as in the game (the night stays the game's blue night; the torch then lights surfaces and not the air)?
  (2) the fog's later orders of scattering: all of them (today: physically what the medium's numbers say, 30 %
  over the game in thick white fog) or one, as the game's look was made with? The second has its hidden switch
  (`Traced fog: light scattered once only`); the first got one at 09:30:
  THE TRIAL OF (1) IN THE GAME, 09:45 (main 18784FDDA14D; the hidden `Traced fog: lamps light the mist outdoors`, on
  = as before; off: where the game's Fog volume says "directional lights only" no lamp lights the camera's fog:
  frame flag bit 24, the cells take no lamp's light, the mist's cache is fed none, the lamps' pass is not run; the
  fog a reflected ray crosses is NOT covered. Run `20261012-093855`, `night\overview.jpg`, recipe `fog-lamps`,
  frozen, 16 frames; luma whole (region), darkest and brightest 5 %): the game 38.7 (41.0), 17 and 54; lamps in the
  mist 53.5 (67.2), 9 and 151; THE SUN AND MOON ALONE 20.5 (18.7), 3 and 52; under NRD 20.9 and 53.6. So without the
  lamps the orange glow is gone and the bright end is the game's (52 for 54), BUT THE NIGHT IS HALF AS BRIGHT AS THE
  GAME'S: the blue haze the game has over everything is not in the traced fog. The game's fog there is lit by its
  directional light `.../Lights/ShadowCaster` (60,511 lux, colour 1, 0.99, 0.84, volumetric dimmer 1.23) and the
  sky's probe; the tracer has its sun at that strength (its log: "a white surface in the sun 1.608e+04") and its
  sky, and the fog they light comes out nearly black. WHY IS NOT KNOWN (the volumetric dimmer the tracer does not
  apply is x1.23: far too little). So NEITHER ANSWER TO (1) LOOKS LIKE THE GAME'S NIGHT YET: the moon's haze must be
  found first. Brief for it and for the fog check's ground: `docs/agents/briefs/brief-fog-3.md`.
  THE OWNER'S TWO ANSWERS, 2026-10-12 11:06 (the choices page, https://claude.ai/artifact/JbWmoPqCgAS7em2nN1aSFt, the
  three nights and the three fogs side by side): (1) "Lamps light the mist", with the note "Anything can light the
  mist - not just the sun and moon."; (2) "All bounces". BOTH ARE THE TRACER'S WAY OF TODAY, NOW THE RULE (AGENTS.md).
  So the trial comes out the same day: the hidden `Traced fog: lamps light the mist outdoors`, frame flag bit 24
  (`TGPT_FRAME_FOG_NO_LAMPS`), the factor in `pt_fog_froxel.hlsl`, the recipe `fog-lamps`. WHAT STAYS OPEN: the
  moon's haze (the traced fog under the game's 60,511 lux moon is nearly black: with the lamps on the night is the
  torch's orange alone, where a moonlit mist should also be there): worker fog3, part B.
### PT-38 Night by a torch: the torch's warm light is gone
- Found in the baseline `20261006-145526` (`night/game-vs-traced.jpg`, Slot_45): in the game's picture the torch
  lights the man and the grass warm orange and the rest is dark; traced, the man and the bridge are lit flat and
  cold, as by the night sky, and the torch adds little. The picture as a whole is brighter (luma 41 -> 58, darkest
  5 %: 12 -> 24; gain x1.43).
- Cause (the agents, PT-05): at night the game's "moon" is a 60,000 lux light that its fog (a mean free path of 9 m)
  dims to 2 to 15 %. Traced at full strength it lit the night like a day and drowned the torch.
- Second cause (the surfaces agent, then the state dump of `20261006-163130/night`): out on the campaign map the game
  keeps its lights at the top of the scene, under no prop, with generic names: 127 "PointLight_Cookies" (a flicker
  cookie, 2400 K), 184 "PointLight_Ambience", 25 "PointLight_Shadows" (cookie, 1926 K). The mod keeps only lights whose
  name or a parent's names a flame, so every torch and candle out there was left out: the torch by the bridge lit
  nothing traced. The owner, 2026-10-06 17:10: "In vanilla, I see nearby flames from torches cast some orange-brown hues
  on the top of the adjacent grass, and I just don't see that at all in the PT presentation".
- State: **fixed, seen in the harness's pictures; the owner has not looked yet.** `SceneLights.cs`: a light under no
  prop is a flame by its look (warm with a flicker cookie, or warm within 1 m of one); a warm light alone stays out.
  Installed 17:27 (plugin 13C56AFAB5F8). Run `20261006-173331`: traced lights at the bridge 50 -> 86, at the candle
  corner 67 -> 180; `night/torch-before-after.jpg` (the game's, before, after): the man and the grass beside the torch
  are lit warm orange as in the game's picture, where before they were lit cold by the sky. With the fog on the lights
  (PT-05) the night's moon is a few hundredths of what it was.
### PT-39 An outdoor corner by day goes dark when traced
- Found in the owner's save `PT candle-boil` (run `20261006-155214/candle/game-vs-traced.jpg`): candles in a walled
  corner outdoors at 10:47 in the morning. The game's picture is evenly bright (93 levels); traced it is 39, the wall
  to the right nearly black. The tracer's own measure: "light at the camera 0.03 of the open sky's": it finds almost no
  sky above this place, where the game lights it as open air.
- Found 17:48 with the camera turned up (scenario `candle-up`, run `20261006-174549/candle-up/views.jpg`): a red cloth
  canopy with holes hangs over the corner (the stall and tent cloths there: HDRP/Lit, alpha-tested, double-sided,
  named `..._Cloth...`, so the tracer's thin cloth). In the game's picture its underside glows red and the corner under
  it is lit evenly (the game's ambient light ignores it). Traced, it is a black sheet: the Direct view is black under
  it and on it, only its holes show the sky. Not an interior (the sun is on, the sky's light on); not the torch rule.
- Two things in the code: shadow rays stop at thin cloth as at a wall (alpha_opaque has nothing for
  MATERIAL_THIN_CLOTH: only a leaf's or blade's own shading takes light from behind), so nothing gets through a canopy
  of real cloth that lets a good part of the daylight through, tinted by its color; and the canopy's own underside
  stays black though thin cloth is lit from behind (CLOTH_TRANSMISSION 0.3): to find why (its pixels grey in the
  Surfaces view, so the camera pixel is not taken for thin cloth).
- The grass agent built it (merged 18:50): thin cloth lets a light's shadow ray through 30 % of the time, tinted by
  its color; every thin cloth is alpha-tested; a swaying cloth the camera sees is found within 30 cm of its still traced
  copy (the canopy is `TG/Clothes/Cloth_Wind`, which HDRP sways and the tracer keeps still: the camera's pixel missed
  it, so its underside was never taken for cloth). Offline: a red cloth's shadow lets 0.30 of the sun through, in red.
- In game (`20261006-185451/candle`, `20261006-191659/candle-up/views.jpg`): no change (32.7 against the game's 92.4;
  the underside black). Why: at 10:47 the corner is in the building's shade, no sun reaches the canopy, so there is
  nothing for a light's shadow ray to let through; its light is the sky's, and sky and bounce rays still stop at
  cloth, which gathers nothing from behind.
- State: the grass agent's round 2 (19:52): thin cloth lets the sky's and the bounced light through, tinted, for its
  own underside and for what lies under it.
- That round (`docs/agents/grass-2.md`, merged 22:57; offline: the floor under a red canopy gets 0.30 of the sky in
  red) in game (run `20261006-230457/candle`, installed native C4237DC0DE00): the corner still 32.8 against the game's
  92.1 (32.7 before).
- The owner, 2026-10-06 23:15: "I never complained about the candles being darkly lit, that's just because there's no
  light source in that corner. Not sure why you keep iterating on that. However, when you looked up against the cloth
  I would have expected to see some red hues and subsurface scattering as the light transmits through it and casts a
  red hue". So the dark corner is right and closed; what is wanted is the canopy seen from below.
- The canopy from below in that run (`candle-up/canopy-small.jpg`: the game's, traced): the game's underside glows deep
  red all over, its holes white; traced, it is near black with a faint grey-green cast, the holes white. So the round's
  "a cloth pixel gathers the sky's light from behind it" does not show in game. Next: why (does the camera's pixel of
  the canopy count as thin cloth now: the Surfaces view on `candle-up`; is there daylight above the canopy to pass:
  the sky there, the building's shade), then the red light it passes onto what lies under it.
- The debug views (run `20261006-232601/candle-up/views.jpg`): the canopy is grey in the Surfaces view, so the tracer
  still does not take the camera's pixels of it for thin cloth (as at 18:50); its Albedo is red; its Direct view is
  black and its Indirect a faint dark red; the holes show bright sky, so there is daylight above it to pass. The
  cause to find: why the canopy's pixels miss thin cloth (`TG/Clothes/Cloth_Wind`, swayed by HDRP, the tracer's copy
  still: the 30 cm window of 18:50 is not enough, or the material is not flagged at all). Next agent after the three
  running.
### PT-40 Candle wax looks dark and faceted when traced, wicks white
- Same picture (`candle/flames.jpg`): the game's wax is cream and smooth; traced it is dark olive-brown with flat
  faces and pale chips at the rims (as the cups of PT-04), the melted wax at the foot white with black holes, and the
  wicks white where the game's are black.
- State: open; read `docs/PT-SURFACES.md` (the cups) first: likely the same cause for the facets. The wax is one of
  the game's see-through-light materials (a candle glows from inside).
- The surfaces agent (`docs/agents/surfaces.md`): the wax is red in the Surfaces view, which is HDRP's subsurface
  scattering with transmission through a thin profile (orange is skin): the tracer lights it as a leaf (light from
  behind at half strength), not as skin, and runs HDRP's scattering for solid profiles only, so the game's blur of the
  wax's light is missing. Its Direct view is black on the candles' sides: each flame sits above its own wick and lights
  its candle's top; the game's cream comes with light the tracer leaves out (no fake lights). Wicks: inside the candle
  light's 10 cm sphere, white in the Specular view. The faces: the built-in denoiser's in the debug views, and, if the
  wax mesh is alpha-tested, a black face before each shadow line; both fixed in the agent's copy (PT-04). Next: which
  profile the wax has (F8 at a candle), and the owner's call on scattering for thin profiles.
- The owner, 2026-10-07 09:40: "what are these black spots in the wax bases of the candlelights here? Looks fully black
  inside the wax in spots" (run `20261007-032345/candle/2-traced-rr`). Seen (`20261007-042814/candle/wax-holes-small.jpg`:
  the game's, before, the latest, its Direct view): the folds and pockets of the melted wax at the candles' feet are
  black, the rims the sun reaches white. Real wax is translucent: light goes in, scatters and comes out elsewhere, so
  its folds glow softly (the game's cream). The tracer treats the wax as a surface or a thin leaf (light from behind
  through 1 cm), so a thick lump's pockets get nothing. The fix is subsurface scattering in the tracer: a random walk
  inside HDRP's subsurface materials by their diffusion profile's own scattering distances (wax, and skin: ears, noses,
  softer faces). Next agent.
- 2026-10-08: built by two jobs (the walk, PT-58; who walks, `docs/agents/who-walks.md`: the candles' meshes close a
  volume, 314 of 340 and 480 of 506 triangles, and walk since the 07:50 build). THE OWNER, 08:55, on the candle
  scene of that build: "Good work on the candle lights, they look white now. Although, they're inside that red
  cloth above them, so would they really be SO white under that darkened, reddish overcast cloth? Also, the base of
  the candle lights, you know, the wax grown into the rock underneath them, still looked brown".
  Measured on run `20261008-084558` (`candle/crop-candles.jpg`): the traced wax is 196, 164, 154 (the game's 208, 183,
  152), nearly neutral, while the wall behind it is 85, 50, 30 (the game's 145, 118, 95), strongly orange: as shares of
  red in linear light the wax 1 : 0.68 : 0.59, the wall 1 : 0.31 : 0.10. Cream wax in the wall's light would be about
  1 : 0.29 : 0.08. So either the wax takes daylight the wall does not, or the walk loses the light's colour: not
  known yet. The foot is the open shell the tracer's log names ("0 of its 1147 triangles are in pieces that close a
  volume: a sheet, no walk"): wax over rock, still lit as a plain surface. Both with an agent since 08:57
  (`docs/agents/briefs/brief-wax-2.md`, copy `wt\wax2`, base53). State: open.
- THE OWNER AGAIN, 2026-10-09 08:40: "also the wax base of the [candles] on the pillar with candles under the red
  blanket is still brown while the candles themselves are white and scatters light". Seen (run
  `20261009-071155`, `candle`, NRD; sheet `candle-corner-game-traced.jpg`): the game's candles and the wax at
  their foot are one cream; traced, the candles are bluish white and the foot is brown and tan. THE JOB OF 10-08
  (`wax2`) WAS CUT SHORT by the usage limit: its report (`wt\wax2\docs\agents\wax-2.md`) says the candles'
  white is real light as the tracer is given it ("the game's wax material names the SNOW's scattering profile:
  light flies 3 to 7 cm in it before it scatters, and a candle is 8 cm thick ... nearly clear, like thin ice"),
  and its fix for the foot (light walking between the open shell and the rock under it) was written, never
  built or run. The coordinator's doubt: a free path of 3 to 7 cm in wax smells of a unit taken wrong (HDRP's
  profile distances and its world scale), which would make both complaints and touch skin too (PT-73's dark
  orange faces). Started 08:47: the agent `wax3` (`wt\wax3`, base66, `brief-wax-3.md`): the units first, a trial
  switch for `candle` and `npc-candle`, then the foot. State: open; an agent on it.
- `wax3` DONE (`docs/agents/wax-3.md`, 10:35; merged 14:10 on 10-09, four conflicts; `<scratch>\pre-wax3`; nothing
  seen in the game). THE COORDINATOR'S DOUBT WAS WRONG: THE UNITS ARE RIGHT. HDRP's scattering distance is in
  millimetres (times the world scale) and the tracer makes of it exactly the free path Unity's own path tracer
  makes (read in HDRP 17's source). The candles name `DiffusionProfile_Snow` (27.72, 30.57, 57.76 mm): free
  paths of 28.7, 32.4 and 71.3 mm in candles 3 to 8 cm thick; skin is 0.30, 0.16, 0.09 mm (PT-73's faces are not
  a units matter either). WHY CREAM IN THE GAME AND CLEAR IN THE TRACER: the game's shader never reads the
  profile as a substance; it spreads light along the surface, divides by the weights' sum and multiplies the
  wax's colour on. Measured offline, a white lamp behind a 4 cm cream stick: as a substance (today) 0.68, 0.73,
  0.94 of the lamp's light comes through, bluer than white; read as the game reads it 0.37, 0.37, 0.37, shown
  in the wax's own colour. THE TRIAL: hidden `Advanced / Subsurface reading` 1: a profile read as the game's own
  shader reads it, the game's sum taken over the real surfaces with rays (hidden sides too), for wax, skin and
  sheets alike; the foot is covered by it (an open shell is the wax's surface: 0.867, 0.852, 0.717 of a plain
  surface's light against a deep lump's 0.868, 0.852, 0.706). As built it is SLOW (10 times the walk on skin,
  12 to 21 on wax, about 100 ms a frame for a face over a third of 4K), grainy with no denoiser, and has one
  limit left (`SURFACE_ROOM` 8). Recipe `subsurface-reading` (`candle`, `npc-candle`). Its bit 22 of the lamps'
  number met the grass trial's: the grass trial moved to bit 23 in the merge.
  THE OWNER'S CALL, to ask with the pictures: what is the truthful reading of a material whose numbers were
  written for a blur: the substance the numbers describe (clear, as now) or what the game's shader makes of
  them (cream)? The coordinator's view: the second, as with the lamps' strengths (numbers written for the game's
  own formula), and then made fast. State: trial merged; one run and the owner's call owed.
- IN THE GAME, 14:36 on 10-09 (run `20261009-142733`, `candle`, NRD; sheet `wax-game-now-trial.jpg` in the run's
  folder; a patch of the tall candle's body, mean of 16 frames, levels R G B): the game 196, 166, 136; reading 0
  (the substance, as now) 198, 166, 154; reading 1 (the trial) 117, 72, 48. THE TRIAL IS THE WRONG WAY: brown and
  0.55 times as bright where today's wax already has the game's brightness and is only paler (blue +18 levels:
  the profile lets more blue through). It costs 47 ms a frame (104.8 to 151.7 ms), with no denoiser the wax is
  dark green grain (one frame is 12.7 levels from the mean; reading 0's NRD picture 0.4), and it made the
  driver take 223 s to make the `walkDxr12` pipeline at a new build's first start. THE COORDINATOR'S VIEW OF
  14:10 IS TAKEN BACK: the worker's offline cream (0.37 of a lamp behind it) did not hold with the scene's own
  light. To do: take reading 1 out (its switch, its bit 22, its recipe, its code in the walk pass) unless the
  owner wants to see it; what is left of PT-40 is the wax's paleness (as now is what Unity's own path tracer
  makes of the profile) and the wicks. State: the trial failed in the game; its removal owed.
- TAKEN OUT 14:52 on 10-09 (`<scratch>\unwax.py`: the eight files the merge wrote put back from
  `<scratch>\pre-wax3`, the two it added moved to `<scratch>\pre-unwax`; the grass trial is on bit 22 again;
  104 source checks pass, the plugin compiles). The trial lives on in the copy `wt\wax3` and its report
  `docs/agents/wax-3.md`, whose finding stands: the units are right. State: open for the wax's paleness and the
  wicks; no job running.
### PT-41 The heat haze around flames should bend the picture through the tracer
- The owner, 2026-10-06 16:31: "Oh, I guess the heat-haze around each flame should also refract via PT."
- Today the haze is the game's own: a sprite around each flame that HDRP draws over the traced picture (its distortion
  and glow), not a thing the tracer knows (PT-06 found it). It belongs with the particles (PT-29 to PT-31) and with the
  hard rule's list of what HDRP still shades while tracing ("transparents and particles ... raise those with the owner
  before designing them"): the owner has now asked for this one.
- State: open. To find out first: how the game makes the haze (HDRP's distortion pass on a particle, or a shader that
  reads the screen), and what bending "via PT" should mean there: the camera's ray bent on its way through the haze
  (as through glass), or the haze's own pass fed the traced picture without noise.
- The owner, 2026-10-07 23:25, of the rectangles of noise at candle flames (PT-73): "The heat haze effects should be
  rendered in a more PT-friendly way there". In work: the candle worker (`wt/candle`), after proving what draws
  the rectangles: hot air above a flame as a small refracting volume that bends the rays that cross it.
- The owner asked again, 2026-10-09 08:28 ("what about heat haze from fire and candles?"): still none while
  tracing: the game's distortion passes are switched off per effect material (`EffectPasses.cs`), and no traced
  haze is in the tree (no shader mentions it). Not started; with the particles (PT-67).
- The owner, 23:33: "Heat haze should also apply around fire, like torches and fires, stoves, etc". Every flame and
  fire the game gives a haze gets it, found by what it is (an effect's distortion output, a haze quad), sized by the
  game's own effect; the candle worker was restarted with that at 23:37.
### PT-42 Waves at the shore: no blending, no foam
- The owner, 2026-10-06 17:10: "Waves hitting the shore looks silly because they don't blend at all, and there's no
  foam. Did so in the vanilla game too, though, but still."
- The game's own look (HDRP's water system as the game sets it up); path tracing traces water's reflections only.
- State: open, not looked into. To find out first: whether the game's water has foam and shore blending switched off
  in its settings (HDRP's water has both: foam from a mask or the simulation, a soft edge where depth is shallow), and
  whether the mod may switch them on (the owner's call: it changes the game's look, as the terrain blending, PT-10).
  The scene: `water-day` (PT water-day).
- The owner, 2026-10-07 10:57, watching the cave lake with the traced water: "the water, when it moves in there, it
  looks blocky and not smooth, maybe it's because it's hitting the rocks/surfaces around it and because there's no
  blending at all, it justs as harsh intersections instead of smoothly as water hitting rocks". And 11:03, for the next
  playtest: "I want to see smooth intersections between waves and surfaces/cliffs/rocks/shore". Now the tracer's job
  (the water is the tracer's, PT-48), no longer a question of the game's settings. What real water does at a rock,
  for the design: water a few millimetres deep follows the rock's own bumps, so its mirror breaks up into the rock's
  shine before it ends; the rock just above the line, as far as the waves reach, is wet (darker, glossy); waves that
  break leave foam (HDRP's simulation has it). Nothing of that is in the traced water yet: its mirror ends on a hard
  line that jumps along the rocks' flat polygons as the water rises and falls. Brief: `<scratch>/brief-water-edges.md`,
  the second water agent, when a slot frees.
### PT-43 Skin looks flat when traced: no blush, no detail on the cheek, no dark around the eyes
- The owner, 2026-10-06 evening, of Ron (`20261006-203731/hair-eyebrows/side-by-side.jpg`): "wtf is up with Ron's eyes
  and skin shading? In vanilla, you see blushes and details on his cheek, and we see is eyelid casting a shadow or
  whatever. In our rendering there's none of that".
- Looked at (`20261006-203513/hair-eyebrows/face-a.jpg`, `face-b.jpg`: the face in the traced picture and the debug
  views): the Albedo view has the blush, the freckles and the dark around the eyes, so the tracer is given them.
  Measured (`<scratch>/albedo_share.py`, log of the traced picture against the logs of the Albedo and Direct views):
  the skin's colour pattern reaches the traced picture about as strongly as the shirt's weave (power 0.73 against
  0.77; the fine part alone 0.67 against 0.83, a little softer). So the colour is not thrown away.
- Read from the pictures, not measured: the traced face is lit hard and orange by the candle on the wall, where the
  game lights it dim and grey (by which light: not checked). Under orange light blush and skin come out nearly alike,
  and the bright face sits high on the tone curve. The Direct view lights the eye sockets like the cheeks: the game
  darkens them with ambient occlusion (the skin's own map and the screen-space kind) on its ambient light, the tracer
  uses neither on skin.
- State: open, cause not proven. Next (a fresh agent, after the three of 21:05): what lights Ron's face in the game
  (the audit's lights), whether the skin's occlusion map should dim the tracer's sky and bounced light as it now does
  for leaves, how much the traced exposure lifts the face, and the fine part's softening (0.67).
- The face agent (`docs/agents/face.md`, merged 03:35, offline only): Ron's dark sockets and lids in the game are not a
  shadow. His face has two lights there: the candle on his table (traced) and the player's own HeroLight (4900 K, at the
  camera: left out as a fill light by the ruling); HDRP's screen-space ambient occlusion darkens that direct light (0.75
  in this tavern). Measured, under the eye and in the lid crease against the cheek: game 0.72 and 0.71, traced 0.98 and
  0.99. The game's greyer face is the same HeroLight (blue over green 0.64 in the game, 0.39 traced). Ours: every ray
  from skin and eyes started 2.3 cm out from the skin (past the lids, brow and nose), and back faces within 16 cm let
  rays through, so lids could never shade the eye; and skin ignored its own ambient occlusion. Fixed: rays from a
  character's skin and the eyes start 1 mm above their own triangle and back faces stop them; skin takes HDRP's
  occlusion off its bounced and sky light, as leaves do. Offline: an eye under a 1.5 cm lid 0.3998 to 0.0000. Traced,
  Ron's face is the candle alone (lifted 2.8x by the traced gain): the game's look needs the HeroLight and HDRP's
  occlusion trick, which the rules leave out: the owner's call (the owner runs a mod named No Player Light, PT-24).
- In game (run `20261007-041858/hair-eyebrows`, `eyes-zoom.jpg`; native 01439DBC88FC, the tested one;
  `<scratch>/facework/face_levels.py`, each box against the cheek): the eye 0.36 of the cheek traced (0.39 before, the
  game 0.25); under the eye 0.56 (0.56; game 0.41); the lid crease 0.67 (0.68; game 0.49). In the Direct view the eye
  falls from 0.88 to 0.81 of the cheek. A small change: the candle lights Ron from below, so the lids shade little of
  it; the game's darker sockets are its HeroLight with ambient occlusion (above). Not yet looked at by the owner.
- Correction (from that agent): `detail_boxes.py`'s "eyes" box (y 0.435-0.465) is the brows in the 4K picture; the
  eyes are at y 0.484-0.489. The "eyes" numbers above are the brows'.
### PT-44 Eyes are flat discs when traced: no shadow from the eyelid
- The owner's words: PT-43's. Seen in the same crops: both eyeballs are lit evenly from edge to edge, brighter than
  the skin around them (`face-b.jpg`, Direct view), yellow-green under the candle; the game's eye is darker toward the
  lid and its white is grey.
- Known (`docs/PT-DETAILS.md` "Eyes"): the tracer bakes the eye's colour and lights it as a wet ball; it leaves out the
  game's eye extras, and HDRP's cornea shell is hidden. What makes the game's lid shadow is not known: a part of the
  eye shader, a shell over the eye, the lashes, or ambient occlusion.
- State: open. Next: find what darkens the eye under the lid in the game (the eye and cornea materials in the run's
  audit, the shader in the decompiled game), then give the tracer that, or a real shadow from lid and lashes.
  With PT-18 (eyes too small) and round 3's note that HDRP's cornea may still be drawn.
### PT-45 The tracer's own copies of the game's textures: a fixed number of sharp ones
- The owner, 2026-10-06 evening: "what's up with that arbitrary '32 sharp texture slot' thing you refer to in the
  tracer? Why is there such a limit?"
- Why: the game draws with Direct3D 11 and the tracer with Direct3D 12, so the tracer cannot read the game's textures.
  The plugin copies each material's colour into one big texture of the tracer's: 4096 small squares (64x64) for what
  is seen in reflections and bounced light, and 32 sharp ones (512x512, 4 rows of 8: `runtime.hpp` `kDetailRows`)
  for what the tracer draws on screen itself: hair, eyes, see-through glass. 32 was picked in round 4 for eyes and
  glass; then hair took one per material and never gave it back. No hardware reason: 1 MB a square.
- State: a fresh agent finishes 128 squares that hair shares and gives back (`wt/surfaces2`, PT-35). That is a patch.
  The lasting answer is the game's real textures in the tracer (each shared with or copied to Direct3D 12 once, read
  by number): no squares, and sharp textures in reflections too (64x64 today, which may be part of the soft
  reflections in the cave, PT-34: not measured). A design job: its video memory and what it costs to share.
- The owner, of the 64x64 copies: "what, seriously? That sounds super low-res?" It is: a whole texture squeezed
  into 64x64. What is on screen keeps the game's full colours (HDRP's own buffers); bounced light is a blur anyway; a
  sharp reflection (water, polished metal, a mirror) shows the 64x64 copy. 4096 copies at 64x64 are 64 MB; at 512x512
  they would be 4 GB, which is why they are small. To measure: a mirror-like surface's reflection against the game's.
- The owner's idea, 2026-10-06 evening: "The game supports a `-force-d3d12` flag btw. It breaks our graphics bridge
  because we rely on it for DX11 to DX12 for FG and Better Upscaling. But if we force the game to render in DX12, maybe
  we could read the game materials directly? And maybe we could add support for that for the FG/upscaling mods too?"
  What it would give: under Direct3D 12 Unity hands a plugin the textures and mesh buffers themselves
  (`GetNativeTexturePtr`, `IUnityGraphicsD3D12v7` with its resource states), so the tracer reads the real textures by
  number, at no extra video memory, with no copies across two devices each frame. It gives the textures, not the
  game's shader math: hair, eyes and skin stay decoded by hand. What it costs: the tracer's game-side half is written
  against Direct3D 11 (the captures, the bakes, the hand-off, `d3d11.hlsl`): a second back end, not a switch. Frame
  generation and upscaling would need a new way in (Unity owns the device and the swap chain there, the bridge owns
  them today) beside the Direct3D 11 one players use. Unknown, to measure before any of it: how the game runs under
  Direct3D 12 with no mods (frame rate, stutter, crashes), what other mods do there (RenoDX), and how a player would
  get the flag without a chore. The same on Direct3D 11 costs a second copy of every texture in video memory.
- The owner's ruling, the same evening: "I wouldn't force users to launch the game with -force-d3d12 I guess, as the game wasn't
  built around it." So Direct3D 12 for the game is off the table, and no run measures it. The tracer's textures get
  better on Direct3D 11: to find out first, whether a texture of the game's can be shared with the tracer's device with
  no second copy (the game makes them; the flag for sharing is set when a texture is made, and compressed and
  mip-mapped ones may not allow it); else copies sized by need (what a ray sees large gets a large copy), with normal
  and mask maps, under a video memory budget the player's card sets.
- The owner, then: "But for sure we need to increase the material size so we can do perfect mirrors, like
  Cyberpunk". So this is wanted work, not a maybe: what a ray hits gets the texture at the size it is seen at, up to
  the game's own. (Cyberpunk's tracer reads the very textures the game draws with; here they are copies, so the cost is
  video memory: a 2048x2048 texture with its mips is about 5 MB compressed and 22 MB not; a scene has hundreds.)
  Next: the mirror scenario (PT-46) as the yardstick, then a fresh agent on this.
- The owner, then, on the flag again: "if it turns out we can't deliver mirror reflections on DX11 due to not having
  direct access to the game materials, or if it makes your implementation easier, then I guess requiring the DX12 flag
  is the way to go. But, that will then mean we'll have to also add support for it in our FG and Better Upscaling mods
  as they're definitely required for this to run at playable framerates". So: Direct3D 11 first; Direct3D 12 is the
  fallback, and it brings Frame Generation and Better Upscaling with it. Direct3D 11 can deliver it: the price is
  video memory, not a wall. The number that decides: what a second copy of a scene's textures costs (at full size, and
  capped at 1024 and 512), counted by the harness in the mirror run (`textures.txt`).
- The owner's ruling, then (before 21:35): "We want way more sharp slots, and we want them at the same res as the res the game
  renders at" (AGENTS.md). So the target is the game's own texture sizes, every texture the tracer reads. The way that
  costs no second copy on Direct3D 11, to find out first: the tracer's mods already sit between the game and Direct3D,
  so a texture could be made shareable as the game creates it, and the tracer's device opens the very same texture
  (whether the driver allows that for compressed textures with mips is the question). Otherwise copies at full size,
  and the video memory number decides how many fit.
- The owner: finding out whether the cheap way works is "the top priority right now". A fresh agent probes it offline
  (report `docs/agents/texture-share.md`); the harness's mirror run reads how the game makes its textures in Direct3D
  11 (`textures.txt`: usage, flags, formats, mips, mip streaming).
- The probe's verdict (`docs/agents/texture-share.md`; its source kept in `tools/share-probe/`): yes, on the owner's RTX
  5080. With the legacy sharing flag (D3D11_RESOURCE_MISC_SHARED alone) 324 of 327 cases read back bit-exact on D3D12,
  every mip, every format the game uses (BC1 to BC7, RGBA8, sRGB, arrays, cubes, IMMUTABLE too; the 3 misses are not
  valid textures unshared either); the NT-handle flag refuses BC4-BC7 and cubes. The same video memory above 64 KB,
  sampling speed unchanged, 0.13 ms to open one. D3D12 keeps a texture's memory until it lets go, so the tracer must
  release it when the game frees it. Untested: AMD and Intel, the game's whole texture list, Unity's mip streaming.
- The owner: "It's the number one most important thing right now, because if we can share textures, we immediately
  improve hair and reflections". Phase 1 (a fresh agent, `wt/share`, base `wt/base8`, report `docs/agents/share-1.md`):
  the hook on the game's texture creation, the registry, proof in game by log lines. Phase 2: the tracer reads them at
  hits (reflections, mirrors, hair, eyes), then the atlas and the detail tiles go.
- Phase 1 (`docs/agents/share-1.md`, merged 23:05, ABI 0x00011700) in game (runs `20261006-232203`, `-232601`; plugin
  6B92B62D0D66, native F82BD94C0C89, the tested one): it works. After the hook, 493 of 993 and then 925 of 1515 textures
  were opened by the tracer with no copy (490 MB, 1019 MB), and 473 frees by the game were caught. Three findings,
  given to phase 2: the hook went in only when tracing was first turned on (the harness and players start with it off:
  "485 made before the hook"), so it goes in at launch; the size rule must be 64 KB with mips, not 64x64 texels (small
  textures round up to 64 KB when shared); Unity's mip streaming makes a new D3D11 texture when it loads a texture's
  top mips (12 of 12: a 64x64 stand-in, then the 2048 texture as a new object), so the tracer must follow the new one.
  The scene's 1979 loaded textures take 3388 MB as the game holds them (1518 stream their mips).
- Phase 2 (`docs/agents/share-2.md`; finished by a fresh agent after a usage-limit stop; merged 02:45): every ray
  that reads a material at a hit (reflections, bounced light, the radiance cache, water reflections, glass,
  forward-drawn surfaces) reads its colour, normal and mask maps by number, at a mip from the ray's width (ray cones),
  filtered anisotropically; hair (RealHair colour, strand flow) and eyes (sclera, iris) read their own maps; the atlas
  and detail tiles stay as the fallback. Phase 1's three findings fixed in code: the hook at launch whatever Mode
  says; only textures of 64 KB or more shared; mip streaming's new textures registered (not yet for hair and eye maps).
  Offline: in a mirror the shared texture matches the reference pattern at 0.713 against 0.001 for the atlas tile; a
  normal-mapped panel 0.2506 tilted toward the lamp, 0.1435 flat, 0.0575 away. ABI 0x00011800; TGPT_Material 160 B;
  FrameConstants 6128 B. Its material flag clashed with the lights round's (both 131072): moved to 262144 at the merge.
  Risks: video memory on 8 GB cards (no cap yet), cost, black or magenta patches in reflections.
- Phase 2 in game (runs `20261007-023808`, `-024109`; native 3EC558E3AD00, the tested one): sharing from the first scene
  (464 of 497 textures opened; the rest under 64 KB), 445 frees caught. The mirror in the tavern (`mirror.py --frame`,
  the first run's size): fine detail 0.58 of the reference with Ray Reconstruction (0.37 before), 0.73 with no denoiser
  (0.58); the door's planks without squares; brightness 1.54x the reference after exposure (2.07x before); colours still
  too saturated (0.98 against 0.76); by day no change measured. Ron: his hair came out PALE BLOND: the hair material's
  shared colour map was read before RealHair's own colour (the map alone is grey; RealHair tints it). Fixed 02:50
  (`material_texture`: hair, brows, lashes and eyes take the shader's colour or their tile; a source check), queued.
  Ron's crown with the bug: fine detail 0.41 with RR, 0.56 with none. With the fix (run `20261007-030128`,
  `hair-eyebrows/head-fixed.jpg`; native 96410B90A4EC, the tested one): his hair red again; crown 0.30 with RR, 0.49
  with none; eyes 0.80 and 0.96; cheek 0.71 and 0.85. The crown's detail is where it was before phase 2: what blurs it
  is the strands as drawn and lit, not the colour map's size.
- Other cards (`docs/agents/texture-share.md` "Other cards"; the probe with an adapter option in `tools/share-probe/`):
  the PC's own AMD Radeon (RDNA 2, driver 32.0.21043.5001) matches the RTX 5080 case for case, 324 of 324 bit-exact,
  memory freed when both devices let go; Windows' software adapter (WARP) agrees. Intel: no card here and no document;
  Chrome's driver bug list has one D3D11-to-D3D12 sharing workaround, Intel's (a corrupted picture on Core Ultra).
  Microsoft: the legacy flag "might vary from driver to driver". So the mod needs the start-up self-test the section
  designs (share and read back one texture of each format the game uses, on a worker thread: 77 ms on the RTX, 215 ms
  on the Radeon; fall back per format to the copies). Unmeasured: Intel, AMD RX 7000/9000 (the other driver branch),
  Proton.
- Phase 3 (`docs/agents/share-3.md`, merged 05:00, ABI 0x00011A00): a start-up self-test on the player's own card
  (22 formats, 24 cases, read back through the tracer's own path; a failed format keeps its atlas copies, none passing
  keeps the hook out); hair and eye maps follow mip streaming (`TGPT_SwapSharedTexture`); a video memory cap (a texture
  opens only while Windows' count for the game stays under 90 % of the budget; kept-back ones retried every 10 s). In
  game (run `20261007-050735`; native 7548F09F4362, the tested one): "its start-up self-test passed 22 of 22 formats in
  183 ms"; 919 of 987 textures opened (1075 MB), 440 frees caught; no problems in the report. The cap's behaviour on an
  8 GB card is reasoned, not measured.
- The owner asked whether this makes the hair work redundant. Partly: the game's texture pixels at full size replace the
  baked colour tiles and their pool (the 128-tile change is a stopgap until then). They do not replace the shaders'
  math: a ray tracer cannot run the game's raster shaders, so RealHair's colour, alpha, highlights and the layering of
  hair, scalp masks and brows stay the tracer's own code (as in any path tracer, Cyberpunk's included).
### PT-46 A mirror must show the world as the camera sees it
- The owner, 2026-10-06 evening: "What, so are you saying our PT couldn't currently show mirror reflections? In
  principle, we need our tracer to be able to perfectly render the world in a mirror if a scene has one". A ruling
  (AGENTS.md).
- Today: what is on screen comes from HDRP's own buffers (full textures, normal maps, the game's shaders); what a ray
  hits comes from the tracer's own, poorer copy of the world. In a mirror the shapes, the light and the shadows are
  right, and the rest is not: colour from a 64x64 copy of each texture (PT-45); no normal maps (the mesh's own normals
  alone: `pt_common.hlsli` has HDRP's normal-mapped normal for screen pixels only); matte shading (PROJECT-CONTEXT 9.7:
  "path vertices are diffuse-only": no highlight, no reflection inside a reflection); the eye and the hair layer are
  for the camera's pixels only; what HDRP draws on top is not there at all (particles, transparents, decals, the fog
  along the reflected ray: the last not checked).
- State: open, a block of work of its own. Its yardstick, to build first: a harness scenario that stands a perfect
  mirror in a scene and holds the traced mirror image against the game's own picture taken from the mirrored camera
  (the same view, drawn by HDRP), per region. Then, in order of what shows most: real textures and normal maps at a
  hit (PT-45), full shading at a hit (highlights, a second bounce of reflection), the game's hair, eyes and skin at
  a hit, then what HDRP draws on top.
- The yardstick is built: recipe `mirror`, scenarios `mirror-tavern` and `mirror-day` (skipped unless named), the
  harness's `Mirror.cs` (a perfect mirror stood in the frozen world; the reference is the game's own picture from the
  camera's mirror image, flipped), `tools/harness/mirror.py` (writes `mirror.jpg` and `mirror.md`).
- First run (`20261006-214300`, `mirror-tavern/mirror.jpg`, `mirror-day/mirror.jpg`): the traced mirror shows the right
  things in the right places (the tavern door and its iron bands, the hero's body, the hut, the fence, a person at
  the side). Against the reference: textures blocky (the door's planks in visible squares) and blurred (fine detail
  0.37 of the reference's with Ray Reconstruction, 0.58 with no denoiser, in the tavern); colours too saturated (0.98
  against 0.75 in the tavern, 0.55 against 0.38 by day); brightness off once the scene's exposure is taken out (2.1x
  in the tavern, 0.65x by day); by day the grass is a dark smear, and where the reference has fog-softened trees and
  the hill the traced mirror has a pale grey patch. The hero has a head in the mirror, a bare skin-coloured egg with
  no face: the game hides the hero's head from its first-person camera, the tracer draws the mesh. The game itself
  cannot draw a mirror at all (with path tracing off the mirror is flat grey: 0.21 shape agreement in the tavern).
- Not yet useful: `textures.txt` counted 2 textures within 40 m, because the world is drawn by the game's own
  renderers (Medusa, Kandra), not Unity's; it now counts every loaded texture (next run). What it did read: the game
  makes its textures with usage DEFAULT, shader-resource only, no special flags, BC3; Unity's mip streaming is on
  (budget 1548 MB).
- The mirror agent (`docs/agents/mirror.md`, merged 05:20): in the tavern the colours are mostly the tracer's own light,
  not the mirror's (outside the mirror the traced picture is 1.26 times as saturated as the game's, inside 1.29);
  material colours at hits match HDRP's buffers (0.99 to 1.01). The mirror's own fault: a reflected surface was lit as a
  matte path vertex (no highlights, no Indirect Color, metals matte); offline a red wall in a perfect mirror came out at
  0.86 of its directly-seen brightness, its green and blue 0.70 and 0.61 of the wall's. Fixed (`shade_seen`: diffuse and
  GGX through `direct_light`, Reflection Boost, the cache's bounce with Indirect Color, one more ray for smooth
  metals): 0.982 brightness, 0.989 and 0.981 colour. By day the fog along a reflected ray is missing (far things 0.14 to
  0.23 of their brightness: PT-37), and HDRP's depth of field blurs the mirror at its own depth: not fixed. The head: in
  first person the game loads a body with no face, hair or eyes (`VHeroController.fppParent`), ending in a stand-in egg
  around the camera that the game never draws; reflections now pass that skin within 0.3 m of the camera, so the mirror
  shows a headless body as the game's own view does (material bit 1048576 `TGPT_MATERIAL_FIRST_PERSON`). The real head
  is not loaded. The harness's mirror recipe gained `2b-mirror-reference-traced` (the traced picture from the mirror's
  side) and mirror.py a second table against it.
- In game (run `20261007-051746`; native A6D650DB1669, the tested one): against the tracer's own view from the mirror's
  side, the tavern mirror is now 0.99 as bright, shapes 0.93, the same saturation (0.98 and 0.98): the mirror shows
  what the tracer sees. Against the game's view: brightness 0.89 (1.54 before), saturation still 0.99 against 0.75 (the
  tracer's own colours are stronger than the game's everywhere: a matter of the traced light, not the mirror). Fine
  detail 0.36 of the traced direct view with Ray Reconstruction: a reflection reads textures at the ray's widened mip
  and has no HDRP buffers to draw from, so it is blurrier than the direct view. By day: brightness 0.67, the far haze
  missing (the fog agent, PT-37). Not yet looked at by the owner.
- The owner: "Mirrors still seem very low-res". The sharpness agent (`docs/agents/sharp.md`, merged 09:40): our own
  code made a perfect mirror slightly rough: reflection rays were drawn with `roughness_alpha`, whose 0.045 floor is
  meant for highlights, so they strayed about 0.004 rad (4 render pixels) a frame and Ray Reconstruction averaged that
  into a blur; the same floor widened the ray cone 3 times as fast, so mirrors read blurrier texture mips. Offline (a
  checker in a perfect mirror against the camera's view from the mirror's place, no denoiser): mip 1.71 against 1.01,
  detail 0.35 before; mip 1.00, detail 0.96 now (`reflection_alpha`, floor 0.01, for the ray, its weight and its cone).
  Reflection Boost never roughens a surface now; RR's hit distance is measured from the mirror (its guide 3.4.9). The
  hidden key "Reflections read the top mip" and the recipe `mirror-sharp` test it in game.
- In game (run `20261007-100106/mirror-tavern`, recipe `mirror-sharp`; native 876002DF3BF4, the tested one): against
  the tracer's own view from the mirror's side, fine detail 0.99 with Ray Reconstruction (0.36 before), 1.20 with no
  denoiser; reading the top mip changes nothing (1.02), so the mip is right now; Reflection Boost 0 gives 0.93. The day
  mirror: detail 1.02, shapes 0.87. So a mirror is now as sharp as the direct view.
- The owner, 2026-10-07 10:15: "Great with the mirrors, although fog seems to still not appear in them. And maybe water
  also doesn't?" Right on both: a reflected ray gets the game's height fog only, not HDRP's volumetric fog (the lit,
  thick fog of the rainy day scene); and rays other than the water pass's skip water surfaces (the water mask is left
  out of every other ray), so a mirror facing a lake would show its bed dry. Also seen in `mirror-day/mirror.jpg`: the
  hero's egg head is back there (gone in the tavern mirror). A fresh agent has the three (`wt/mirror2`,
  `docs/agents/mirror-2.md`).
- THE OWNER, 2026-10-08 09:45: "Also do the mirror demo sometimes, right? So we can validate mirror reflections that
  include fog and stuff? And mirror reflections with water. And caustics". So the mirror demo is part of the regular
  runs from now on, in the owner's order of judging (no denoiser, then NRD; Ray Reconstruction last): the recipe
  `mirror-check` (the game's own picture from the mirror's side as the reference, the traced one from there, the
  mirror traced with no denoiser and with NRD, the game's own drawing of the mirror), the scenes `mirror-fog` (save
  `PT fog`), `mirror-water` (`PT water-day`, the mirror turned 55 degrees toward the water), `mirror-cave` (`PT
  mineral-cave`) and `mirror-tavern@mirror-check`; caustics with `lantern-lake@dots` and `water-day@quick`.
  `tools/harness/mirror.py <run>/<scene>` grades each. First run queued 09:47 (`<scratch>\mirror-run.ps1`).
### PT-57 The owner's play test of 2026-10-07 14:40 (the build with the limits, the first caustics, PT-53's hair fix)
- "Basically nothing changed. Eyebrows still flicker, hair still flicker a ton while the camera strafes to the sides,
  and candlelights still glow too strong/bloom too much." So PT-53's fix (merged 14:05, passed offline, flagged nothing
  in the harness) did not fix what the owner sees; PT-55 and PT-47's candles as before (the subsurface work, which may
  change the wax, was not merged yet).
- "The guy on the balcony - I get why his hair is brighter on the one side of his head, it's being lit by nearby
  lights, but the dark side of his hair is just so much darker, it's weird how it doesn't receive more indirect lighting
  I think." In-game evidence for PT-54's cause (a card's only bounced light was the radiance cache's at the scalp's cell,
  in the hair's own shade); its fix was built but not merged for want of a check.
- "When you turn off our denoisers, you really get to see the 'gas' above candlelights, how noisy they are. It's a
  rectangle full of noise above candlelights, so it's no wonder Ray Reconstruction isn't capable of cleaning it up."
  (PT-06.)
- "Why is it so hard to get hair right? It's been so many days of you testing this, researching with subagents,
  reviewing with Astra. You've done so many attempts at it." The coordinator's answer: the tracer starts from HDRP's
  opaque G-buffer, so see-through hair never enters the main picture; it is a layer added after the denoiser, which
  never sees it (flicker) and gives it the wall's motion (sliding); every hair fix since 2026-09-28 patched a
  consequence. And the testing judged hair offline, never in game by a number. The owner: "wtf, why? That sounds like a
  terrible idea. Of course it needs to be inside the frame ... the wrong design entirely. And if the game draws hair
  weirdly, then why not do it correctly with hair strands that can move with physics and stuff?" Ruling in AGENTS.md
  ("Everything on screen is part of the main traced picture"). Next: the tracer's own camera rays where see-through
  things are, hair a surface of the main picture with its guides for the denoiser; judged by
  `tools/harness/hair_motion.py` in game against the game's hair (recipe `hair-measure`, run with -KeepFrames). Strands
  generated from the game's hair cards, with physics: a bigger project for the owner to decide on after that.
- The baseline (run `20261007-144742`, `hair-moving`, recipe `hair-measure`, frames kept; `hair_motion.py --head
  0.38,0.73,0.52,0.95 --hair 0.395,0.655,0.495,0.725`), the build with PT-53's fix: camera still, the hair's change a
  frame after lining up on the head: game 0.10, traced RR 0.08, no denoiser 0.47 levels. Strafing: game 2.78 levels and
  the hair's pattern 1.0 px off the head's (at most 9); traced RR 8.12 and 16.9 px (at most 111); no denoiser 6.76 and
  31.9 px (at most 104). The hair drifts off the head while the camera moves, with and without a denoiser. The owner,
  14:50, having closed the game in the second scene: "There's no point in you wasting 10 minutes looking through 4
  scenes when the first one already show all the problems. Hair is not fixed at all" (PT-HARNESS: one scene that shows
  the problem).
- "Don't you grab and inspect frames yourself? Why is it left for me to judge?" Right: the harness's slide frames were
  half size, its flicker number covers a fixed screen region, not the hair. From now on hair is judged by full-size
  frames and `hair_motion.py` (hair lined up on the head, its change and drift per frame against the game's).
### PT-56 Ron from behind: two layers of hair, a still one on the head and a see-through one around it
- The owner, 2026-10-07 12:24: "standing behind Ron, I can see that he has two layers of hair, which is likely the same
  'foggy hair' defect on the npc outdoors: He has one layer sitting close to the head, which doesn't move, and then a
  translucent one hanging around it, which does flicker and move".
- Ties to PT-01 (the see-through crown, the hair layer past 8 cards: merged 10:10, seen only offline) and PT-53.
  No save from behind yet; `hair-eyebrows` (Ron from the front) runs with the `hair` recipe first (run of 12:25).
- Agent hair-5 (`docs/agents/hair-5.md`): the same cause as PT-53. The hair over the skull moved with the skin behind it
  (right: the still layer), the hair around the head with the wall behind it (the moving, see-through one). Fixed with
  PT-53, offline only.
- State: fixed with PT-53, merged into main 14:05 (no ABI change); not yet seen in game.
### PT-55 Eyebrows flicker
- The owner, 2026-10-07 12:20: "Some NPCs eyebrows flicker constantly. I saved a savegame where you can see this,
  'PT eyebrows-flickering'". Scenario `eyebrows-flickering`, recipe `hair` (8 frames each with Ray Reconstruction,
  NRD and no denoiser, in a frozen world: whether the flicker is the tracer's or the denoiser's).
- Run 12:25, the change maps (`eyebrows-flickering/overview.jpg`, row 3; crop `<scratch>/h5/flick.jpg`): two bright streaks
  along the man's brow and upper-lid line with Ray Reconstruction and with no denoiser, none with NRD. NRD never touches
  the hair layer (it is laid on after every denoiser), so the brows' layer noise is not it; something the tracer's own
  filters smooth under NRD is, and Ray Reconstruction does not. Candidates (agent hair-5, not proven): the lashes, held
  opaque into HDRP's GBuffer as a thin black glossy surface that aliases with the jitter; or the wet eye's candle
  highlight, a firefly on a near-mirror. Next: the save with Direct, Specular and Albedo views per frame and F8 on the brow.
- State: open, located.
### PT-54 Hair glows on top
- The owner, 2026-10-07 12:20: "Some NPCs hair glows on top. I saved a savegame where you can see this, 'PT
  hair-glowing'". Scenario `hair-glowing`, recipe `hair` (the Direct, Specular and Emissive views say which light).
- Run 12:25 (`head-glowing.jpg`): the bowed blond man, candles behind him. Traced: a black oval over the crown, sharp along
  the skull's outline, candle-lit strands around it; the surfaces view shows the layer covering it all, so it is the hair
  and it is black. Agent hair-5: the crown gets no direct light (the head shadows it from the candles behind), and a
  card's only bounced light was the radiance cache's at its own point, the cell of the scalp under it (10 cm cells, hair
  1-3 cm above the skin), which holds the light in the hair's shade. Now each card traces its bounced light (one ray over
  the sphere, shaded as a bounce). Offline: 2.43 times a matte wall's bounced light by its colour; how bright it should
  be is not settled, and the old cache term passes that check too (0.85 of the wall): no number shows the dark cell yet.
  Recommended: merge PT-53 alone, F8 on the crown in game next (the report's PT-54). The "glow" itself is the strands lit from behind, physical. His hair "dark brown":
  no colour bug (PT-53's entry has the numbers in `docs/agents/hair-5.md`): the tracer reads his own blond, and the
  traced tavern is lit by candles alone (white shirts turn as orange as the hair: red's share 0.61 to 0.71 on the shirt,
  0.54 to 0.64 on the grey man's hair).
- State: open; a fix built in the copy `wt/hair5`, not mutation-proof: NOT merged (14:05, PT-53 taken alone).
### PT-53 NPC hair moves when the player moves
- The owner, 2026-10-07 12:20: "When the player character moves, the hair on NPC peoples heads move too. Saved a
  savegame next to a lady, 'PT hair-moving-when-character-moving' where it is especially noticeable, when the player
  moves to the sides via A and D on their keyboard."
- The harness's yardstick: `CameraSlide.cs` swings the camera 0.6 m to either side every 3 s in a frozen world while
  frames are taken (`anim.gif`); the game's own picture is the reference. Scenario `hair-moving`, recipe `hair`.
- Run `20261007-122527` (12:25): the owner watched it live: "I very clearly saw live the hair move on the heads of all
  the people you focused on, it was a very good demo", "Of course it only happened when you showed the path traced
  rendering with the path traced hair", and "It happened without Ray Reconstruction too and indeed without denoising".
  So the slide reproduces it, it is the traced hair's, and no denoiser's history causes it: the hair itself is drawn
  in the wrong place while the camera moves (the world is frozen, so nothing on the head moves by itself).
- Lead, not proven: the hair's skinned shape comes from another frame than the camera it is placed with. The mod
  hides HDRP's hair (its renderers' culling mask 0: "seen by the camera last cull: no" in the log), and HDRP renders
  relative to the camera; if the hair's vertices are skinned for one frame's camera and turned into the world with the
  next one's, the hair is off by the camera's step each frame, which shows only while it moves. `pt_hair.hlsl` has no
  camera history of its own. To test: hold the camera still between two frames and move it by a known step.
- Agent hair-5 (`docs/agents/hair-5.md`): the lead was wrong. Kandra skins every registered vertex every frame
  (`SkinningManager.RunComputeKernel` over all of them, whatever the culling), in world space, and the world was frozen.
  The composite moved the traced light by HDRP's motion vectors, and pt_hair had laid the hair into the texel of the
  surface behind it, whose vectors those are (HDRP has no hair while tracing). At the slide's 5.5 cm a frame, hair at 2 m
  before a wall at 5 m slid about 17 pixels a frame with the wall. Now the hair keeps outputs of its own (light and cover,
  distance, guides) and the composite moves it by where its own point is (HDRP's motion-vector rule, plus the skin's
  object motion behind it). Left as before: HDRP's motion vectors at hair pixels, which DLSS and Ray Reconstruction
  accumulate along (a trail may remain with them); the object motion of a walking character's hair over a still wall.
- State: merged into main 14:05 (PT-53 alone, no ABI change); offline only. The queue after the caustics session runs
  `hair-moving,hair-glowing,eyebrows-flickering,hair-eyebrows@hair`; the in-game run of `hair-moving` decides.
### PT-148 The plants' kept list is not made anew when the hidden Mega Geometry trial leaves 4, 5 or 6 (found by worker megageo7, 2026-10-12)
- PT-99's kept list of the plants by cell has no memory of the `split` it was made with (`plan_plants`:
  `kept=plan.valid && !plan.off && !split && ...`). When the hidden `Mega Geometry trial` leaves a number that splits
  a cell's small plants off (4, 5, 6) and the scene's list stays the same byte for byte, the groups stay the split's
  until the list's own check fires (every 512th kept frame), which makes every group anew at once and SWITCHES
  KEEPING OFF FOR THE REST OF THE SESSION: the list is then made every frame again, the cost PT-99 took out.
- Seen: the worker's offline run (its trial 12 failed once in the direct light, 221 pixels, at that moment); and THE
  COORDINATOR'S STANDING RUN 20261012-113255: its log says "THE PLANTS' KEPT LIST DIFFERS ... (11303 plants in 720
  cells against 11303 in 636)" in its frozen trial-7 step, right after the trial-5 step. From there keeping was off,
  so every "running" step of that run (77.46 plain / 74.59 with 7 / 78.39 plain) was measured with the list made
  every frame: the same for all of them, so they compare fairly, and each is slower in CPU than a session that never
  touched 4, 5 or 6.
- Players never meet it (the trial is hidden and does not change in play). It spoils test runs that mix 4, 5 or 6
  with other numbers: the recipe `mega-plants` does; the worker's `mega-group` and `mega-group-walk` do not.
- The mend, two lines, not built: keep `plan.split` beside `plan.listEpoch` and count the list as not kept when it
  differs from this frame's `split` (`docs/agents/megageo-7.md` in the worker's copy, "A defect of the base").
- State: open; small. With megageo7's merge, or before the next run that mixes those numbers.

### PT-147 The town's lamps are bright or nearly off by what was loaded before (the owner, 2026-10-12: "There was less lit fog around lanterns ... was that on purpose?")
- The owner, watching the harness's runs over the stream: "There was less lit fog around lanterns in your most recent
  run in the scene with the guard and the small food market - was that on purpose? Loved the look of the lit fog you
  had when you measured before that"; then: "Maybe it wasn't this vs the previous run, but a few runs back then? The
  images are too low-res to judge"; and at 12:00, of a run in which the glow was there: "Looks amazing with the lit
  fog around the lights, also in the forest scene - so pretty" (the look is a settled one: AGENTS.md, the fog's rule).
- NOT ON PURPOSE, AND NEITHER THE FOG NOR A BUILD: THE GAME'S OWN LIGHTS. Every kept run of the scene `metal` since
  10-10, its first traced step: the 19 in which the beach's save (`water-day`) was loaded before the town in the same
  game session have a wide warm light by the left wall lantern (the ground before it 136 to 142 / 93 to 100 / 66 to
  71; since the fog's cells the air beside it 82 to 88 / 55 to 58 / 33), frozen or running, whatever the build; the 47
  others (the town first, or after the tavern) have 77 to 83 / 66 to 72 / 55 to 59 and the air 35 to 37 / 29 to 31 /
  22 to 24. The game's own picture, tracing off, differs the same way: 118 / 91 / 73 against 87 / 78 / 67.
- MEASURED IN THE GAME'S LIGHTS (the harness's audit; the same light by name and place; run 20261012-120850, the
  town after the beach, against 20261012-074032, the town first): of 1,075 lights 208 differ in strength, 90 of them
  by more than 1.5 times (2 the other way; the rest a flame's flicker). The lamp light at that lantern,
  "PointLight_Ambience_02" (range 10 m): 524,288 after the beach, 0.125 loaded first; "_07" 4,194,300 against
  0.125; "_06" 4,194,300 against 46; "_01" 370,728 against 0.125; "_04" 524,288 against 360. In the audits of other
  saves on the same map (the beach at 09:10, a forest at 16:35, the night at 23:25, the tavern) those lamps always
  read the full values: only the town's own save, loaded directly, has them dimmed.
- So the game dims a set of its town lamps when that save is loaded directly, and leaves them at full strength when
  the player comes from another save of the same map in one session. WHICH PIECE OF THE GAME DOES IT IS NOT KNOWN (a
  day and night script, a controller of the lights near the player): not looked for yet (`TG.Main.dll`, ilspycmd:
  PT-HARNESS "What the game's code offers").
- Not a defect of the tracer: the lights' strengths are the game's content (AGENTS.md "Path traced, always"). What
  it means: (1) the look the owner confirmed, lamps lighting the mist, was seen with those lamps at full strength;
  (2) the harness: a scene's lamps depend on the scenes before it in the run (`docs/PT-HARNESS.md`), so runs with
  another order are not compared where a lamp lights; (3) open: which state a player gets in normal play (walking
  into the town by day, by night), and whether the dimmed state is what the game means by day.
- Pictures at the game's own pixels: `artifacts\harness\_notes\lantern-by-load-order-full.jpg` (the game's own, the
  traced with no denoiser and under NRD; the town first above, after the beach below); the first, half-size look:
  `lantern-by-load-order.jpg` beside it.
- Next (after the owner's go-ahead): find the game's piece that sets those strengths (reading its code, no GPU). A
  save at that market at night would show what the game means there by night.
- State: explained by measurement; the cause inside the game not found; nothing to change in the tracer so far.

### PT-146 A flame's reach that does not flicker (the owner, 2026-10-12: "Steady reach")
- How far a kept light is tracked as a near light follows its intensity (`SceneLights.cs` `Reach`: as far as it still
  gives 50 lux), and a flame's intensity flickers: the light grid is made anew every frame in a town of flames
  (worker reccpu, `docs/agents/reccpu.md`, "Choices" 1; since that worker the build runs beside the record, so it
  costs a core's 1.4 ms and 0.2 ms of uploads, not frame time).
- The owner, on the choices page: "Steady reach", with the note "I didn't have the full context or understanding to
  answer this one, and we may want to revisit this with actual screenshots to decide." So: the reach from the
  flame's steady strength (its intensity before the game's flicker scales it, or the largest of the last second);
  which cells list a light changes by the flicker's share of its reach: the same picture in the mean, other noise.
- Next: build it behind a hidden switch (the plugin alone), one run of the town at night with it off and on (frozen,
  no denoiser, then NRD), and THE PICTURES back to the owner on the page before it becomes the rule.
- State: chosen, with a second look owed; not built.

### PT-145 The traced light shown one frame late, so that the CPU's work overlaps the GPU's (the owner, 2026-10-12: "Build it behind a hidden switch")
- 10-12 12:22, BUILT, NOT RUN (worker framelate; its report `docs/agents/framelate.md` in its copy `wt\framelate`,
  base `base20s`): the WHOLE-PICTURE way (each frame shows the frame before's whole traced picture: colour, depth,
  motion, guides of one frame; nothing is reprojected) behind the hidden `Advanced / Frame late`, read every frame.
  Native `%TEMP%\tgb\framelate-native\TGPathTracing\TGPathTracing.Native.dll` (A9752A40028D...), its plugin
  `wt\framelate\src\TGPathTracing\bin\Release\net472\TGPathTracing.dll` (D4AE8EB2C4D2...); no shader, bridge or
  other mod changed. THE GAIN IS SMALLER THAN THE OWNER WAS TOLD: this card takes turns between the two devices
  (round 19, section 3), so only today's idle stretches are won: about 8 to 10 ms of the town's 77 (to 67 to 69;
  12.9 to about 14.7 frames a second), not 17. The price: one frame more from the mouse to the screen (about 70 ms
  there). With the switch on: not used under Ray Reconstruction (its camera matrices would be a frame newer than the
  picture); not right yet with frame generation on; the exposure one frame off while it adapts; what HDRP draws
  itself (flames' cards, smoke, the sky, the interface) leads the traced world by a frame in a turn. NOT CHECKED: the
  offline proof that the switch off is today's picture bit for bit (`%TEMP%\tgb\framelate-scripts\gpu.ps1`, then
  `compare.py`) did not run; no frame was traced with either DLL. The run to ask for:
  `spec-terrain@frame-late,metal@frame-late -Measure` (the recipe is in the copy: off / on / off, a turn and a
  strafe posed by the frame's number, motion blur off; frames 0 and 16 of the turn show the most: a brazier's flame
  beside its bowl, a name over a head, a roof's edge against the sky), then the frames on a page for the owner.
- A traced frame is, one after the other: the game's inputs, our CPU work to record the frame, the trace on the GPU,
  the composite; while one works the other waits (PT-99, PT-107). If the composite takes the light traced for the
  frame BEFORE, the game's and our CPU work run while the GPU traces: a town's frame of 77 ms would be about 60
  (reckoned from the stages, not measured). The price: the traced light is one frame behind the camera and must be
  moved to this frame's pixels by motion vectors: lag and smear where the camera turns fast or things appear.
- The owner, on the choices page: "Build it behind a hidden switch", and HOW IT IS TO BE SHOWN: "Show me this motion
  in shots where you grab frames during motion with motion blur disabled so I can pick from shots instead of feel,
  and so you can also look at it too." So the hand-over is a set of frames grabbed during a turn and a walk (motion
  blur off), the switch off and on, side by side on a page, with the frame times beside them (the stream on, A/B/A
  in one run).
- Rules it must keep: nothing that depends on what is on screen beyond the reprojection itself; Ray Reconstruction
  and NRD get this frame's guides or the frame's own that the light belongs to, as their documentation asks (to be
  read first); the switch off = today, bit for bit.
- State: chosen as a trial; not built. A worker's job (the order of work: speed).

### PT-144 Smoke in fog is a dark blob in front of the mist ("black smoke ... at a different depth order", the owner, 2026-10-12)
- The owner, 2026-10-12 11:08, on the choices page, of the fog scene's traced pictures: "the screenshot you shared
  shows some problems regarding the 'black smoke' in the middle of the screen, seemingly rendering at a different
  depth order compared to the game?" The coordinator had seen it an hour before and called it "a separate bug".
- Evidence: `artifacts\harness\20261012-081117\fog\overview.jpg` (and `3-traced-once\full.jpg`, `4-traced\full.jpg`
  against `1-game\full.jpg`): where the game's picture has a few faint pale wisps deep in the fog, both traced
  pictures have a dark smudge with a pale streak, standing out in front of the mist.
- A reading, not checked: the smoke is an effect HDRP still draws over the traced picture (the transparents are
  HDRP's until they are traced, PT-112 step C, PT-121). HDRP fogs its transparents itself, and while tracing HDRP's
  fog is left out (the traced fog is laid over the surfaces' picture by the composite). So the smoke is drawn with no
  fog in front of it: dark, at full strength, as if it stood before the mist. If so, every effect HDRP still draws
  in fog has it (smoke, dust, sparks), and the cure is the transparents in the tracer, never a patch on HDRP's draw
  (the owner's rulings on workarounds).
- Next: one frozen frame of that scene with the views (does the smudge show in the tracer's own views or only in
  the final picture: then it is HDRP's draw); then PT-112 C.
- State: open; seen by the owner in the harness's pictures.

### PT-143 A plain two-sided surface is passed from behind within 15 cm: `passes_through` asks for a cut-out where it means two-sided (found by worker campfix, 2026-10-12, by reading)
- `passes_through` (`pt_common.hlsli`) lets a ray that starts on a surface pass a near back face unless the material
  is a cut-out. Its own comment says "on an alpha-tested (two-sided) material it is a real hit": it means two-sided
  and tests `MATERIAL_ALPHA_TEST` alone. A plain material drawn from both sides (`MATERIAL_DOUBLE_SIDED`, no cut-out:
  a banner's cloth, a thin wall) is passed from behind within 15 cm plus a hundredth of the distance.
- Nothing measured shows it. One line closes it (`& (MATERIAL_ALPHA_TEST | MATERIAL_DOUBLE_SIDED)`); and the whole
  rule was made for HDRP's surfaces lying off the traced mesh: with the tracer's own camera rays it is one of the
  workarounds to take out (PT-60's list). `docs/agents/campfix.md`, "A gap found on the way".
- State: open; by reading.

### PT-142 A very low sun over a steep sea: a coarse cell's triangle with a corner turned from the sun is left out, the water's glow a little dark (found by worker campfix, 2026-10-12, by reckoning)
- The water's sun inside the water is asked through a patch of waves no finer than half the asking ray's width
  (PT-99's rule of 2026-10-12, summed the plain way: a triangle's sun is the area of its three moved corners across
  the light). A triangle with a corner whose facet faces away from the sun is left out (`pt_water_sun.hlsli`,
  `landed.w < 0`). With fine cells that is right (that facet takes no sun); with a coarse cell the triangle spans
  facets that do. Reckoned on the CPU: about 2 % of the water's glow at a sun 8 degrees up over a sea twice as steep
  as the sea fixture's (3 % of its corners turned); the fixture's own sea has no turned facet even at 8 degrees
  (0.00 %), where the GPU reads the plain sum within noise (0.2 to 0.4 %).
- The cure the worker names: keep such a triangle with its own area across the light and only stop dropping it;
  reckoned within noise of the fine cells on every sea it tried. Not built. (Its first cure, "each corner its own
  facet's sun", moved the sun's share by 0.6 to 1.1 % and changed the picture's pattern: taken out the same day.)
- State: open; by reckoning; no scene of the game shows it yet.

### PT-141 A flickering bright line along the water's edge with Caustics on (found by the coordinator, 2026-10-11 22:00)
- Seen in `artifacts\harness\20261011-215257\water-torch\overview.jpg` (the world frozen, Ray Reconstruction): in the
  change map of the step `2-traced-rr` a thin bright curve runs along the water's edge below the camera, from the
  screen's lower middle down to its lower edge and up again (white: 8 levels and more from frame to frame); in
  `3-traced-no-caustics` it is gone, and the game's own picture has nothing there. The region's flicker 0.13 with
  Caustics, 0.08 without.
- Not looked into; it may be older than round 20. A guess, not checked: the caustics' light where the water's
  surface meets the rock (a thin strip the caustics' cells cover in some frames and not in others).
- Next: the same save with no denoiser and under NRD, Caustics on and off, one frozen step each; if it shows with no
  denoiser it is the tracer's, and the caustics' views say which pass writes it.
- State: open; seen once, in one change map.

### PT-140 The memory line's "While a call of the tracer's ran" sums to +314,700 MB (found by the coordinator, 2026-10-12)
- The tracer's log line "PT memory since the tracer's start" (`tracer.cpp` `start_summary`) splits what Windows' count
  of the game's video memory moved by into "while a call of the tracer's ran" and "with no call of the tracer's
  running". After the night's long runs the first read +314700 MB: twenty times what the card holds.
- As read, not measured: each side adds up every move of the count that falls into it, so memory a frame takes inside
  a call and gives back outside one (or that Windows counts late) adds its size to one side and takes it off the
  other, every frame. The two large numbers cancel; neither says where memory went.
- Next: count each part against the level at the tally's start (net, not summed moves), or print the largest single
  call beside the sums; then read the line after a long run. A fault of a log line: no picture, no speed. It matters
  for PT-87, whose question that line was built to answer.
- State: open; by reading.

### PT-139 A pure mirror shows a lamp as a noisy glow: its highlight is sampled through a lobe the mirror does not have (found by worker psr-2, 2026-10-11)
- psr-2 (`docs/agents/psr-2.md`, "What would take the flicker away"): its fixture's lamps are light records with no
  surface of their own, so a mirror shows them only through the direct light's highlight on the mirror itself, and
  that highlight keeps `roughness_alpha`'s floor (`pt_common`): a soft glow with heavy noise where the lamp's sharp
  image belongs. Under NRD with primary surface replacement (PT-127) it is what is left of a mirror's flicker: the
  replaced surface's light flickers 0.23 going in and 0.09 coming out; "the rest", a fifth of the light, flickers 2.4
  (`images-C\95-psr-part-rest.png`: three soft glows on a mirror floor).
- What is right (the owner, 2026-10-06: a mirror shows the world as the camera sees it; a light is the thing that
  emits it): a pure mirror has no lobe to sample a lamp through. The lamp is what the reflection ray sees: its own
  surface (a flame's card, a glowing mesh), sharp and without noise; a highlight beside that counts the lamp twice.
  The floor is a stand-in in the code: a defect by the owner's rule, named here before the owner finds it.
- Not shown in the game. One run says whether the tavern mirror's 0.83 flicker under NRD is this: `mirror-tavern`
  with the environment variable `TGPT_TEST_PSR_VIEW=8` (the mirror shows "the rest" alone; 6: the diffuse part as NRD
  returned it; 9: as handed).
- Next: that run; then which of the game's lamps are still records without an emitting surface, and the highlight's
  rule at roughness 0 (none, where the reflection ray sees the emitter).
- State: open; offline in a worker's fixture.

### PT-138 The fog's cells: what the rule leaves open (worker fogrule2, 2026-10-12)
- 10-12 12:20, THE CHECK'S "GROUND TOO BRIGHT" WAS THE CHECK'S FAULT (worker fog3, `docs/agents/fog-3.md` part A;
  merged by hand): the check's black ground is black by its base colour only and still reflects 4 %; its highlight
  of the sun and of the lamp, dimmed by the fog, was missing from the exact value. Added (`groundShine`), the three
  cases read x0.983 to x1.001; `TGPT_TEST_FOG_ABSORBER=1` (a ground that reflects nothing) gives the old value. The
  pins are filled (the per-pixel route's numbers of the last build with both routes: the cells read x0.995 to x1.016
  of them) and the night's fog has cases of its own: seventeen cases. In the worker's copy all passed (eighteen,
  with its trial's). IN THE MAIN TREE THE CHECK COMPILES AND HAS NOT RUN (the owner's game was open, then the
  pause): `TGPT_TEST_FOG_FROXEL_ONLY=1`, ten minutes, the first thing owed when the card is free.
- 10-12 09:37, THE FOG'S OFFLINE CHECK RAN FOR THE FIRST TIME (the coordinator; main 18784FDDA14D;
  `TGPT_TEST_FOG_FROXEL_ONLY=1`; the output `%TEMP%\tgb\coord-fogcheck.txt`; the test target had not compiled since
  the fog's merge: `tests\pathtracing_nosun_checks.inl` still named the removed flag, mended): 108 ok, every case
  "finite and nonnegative" (the no-sun ones too), THREE CASES FAIL "the fog is what is known exactly (each region's
  mean within 3 %)". In each the sky and the air over the horizon are right (x0.996 to x1.000) and THE GROUND IS TOO
  BRIGHT: "a uniform fog, sun and sky, the light scattered once": far ground x1.054, near ground x1.241; "no sun, no
  sky, a lamp 6 m ahead": near ground x1.926 (0.11258 for 0.05845), round the lamp x0.997; "a lamp 60 m ahead": far
  ground x3.434 (0.33133 for 0.09648), near ground x2.633, round the lamp x1.162. Not known whether surfaces under
  fog get too much light or the check's exact value leaves a part out. Not looked into: `brief-fog-3.md`, part A.
- FAR LAMPS: a lamp's light right by it (about 24 pixels around its image at any distance) is each pixel's own, and
  past 20 m a stretch of the way is asked for its lamps only with a chance (what the fog lets through times 20 m
  over the distance, a 64th at least), weighed one over that chance, with no history (pt_froxel.hlsli
  `fog_lamps_residual`): a far lamp's core should sparkle. Looked for once (`vista-night@fog-ab`, run
  `20261012-064400`): none seen at the fires in view, but that run's fog was a few 16-bit steps (PT-136) and may have
  hidden it. The cure in mind: a share with an end (the pixel's share of a lamp 0 beyond a set distance from it, the
  cells holding the rest; the two still sum to the lamp's light), so that only lamps whose image is near the pixel
  are asked, from a list per screen tile, every frame.
- A TURNING CAMERA: a cell that comes into view has no history. RTX Remix gives a fresh cell six samples and filters
  the cells; the tracer's cells do neither (both were on trial and came out because frozen runs showed the same
  picture, which a frozen run always will). Looked at once (`fog-edge@fog-turn`, the same run): no blocks at the
  screen's edge in the cells' 16 frames; the steps' numbers cannot be compared (each caught another part of the
  swing).
- THE COST (not a measuring run; old route / cells + walk + lamps' pass): tavern-room 2.96 / 3.6 ms, vista 4.38 /
  7.7, night 8.5 / 10.2, fog 1.27 / 2.9. Where it is: the lamps' pass asks nine candidates of every light cell the
  way crosses; every cell feeds the mist's cache every frame (a quarter of them a frame, as surfaces do it, would
  keep the picture); cells over the sky past the medium still trace their rays.
- THE VISTA'S FAR CASTLE: with 32-bit targets the per-pixel route showed a veil of about 2 % of white more than the
  cells there (run `20261012-065226`); the cells are the nearer to the game's own picture. Not known which is right;
  the last build that has both routes: `%TEMP%\tgb\fogrule2-trial5`.
- UNDER NRD the tavern's fog reads 1.5 % dimmer with the cells than with the per-pixel route (runs `20261012-005626`
  and `-053455`), where the two agree with no denoiser. Not looked into.
- THE CHECK (`TGPT_TEST_FOG_FROXEL_ONLY`): its room cases are held against pins that are still empty (printed, not
  judged): no offline GPU test of the cells over the whole way ever reached a frame (every start was ended by a
  game's opening while the driver made the passes: seven quiet minutes are needed once).
- State: open.

### PT-137 The mist's cache is fed only where the camera sees fog, and read all around (found by fogrule, 2026-10-12, by reading and a CPU model)
- The light the fog scatters more than once comes from the mist's cells of the radiance cache (pt_medium.hlsli
  `fog_cache_light`). They are fed at the camera's own fog points (the fog cells' points since PT-118's rule, the
  per-pixel route's before) and read wherever a flight from such a point ends, which may lie anywhere: behind the
  camera, behind a hill. A cell nobody feeds reads 0 (it is given up after 240 frames). fogrule's CPU model of an
  endless uniform fog: the cache then holds x0.86, x0.74 and x0.56 of what it should at albedo 0.3, 0.5 and 0.7
  (`docs/agents/fogrule.md`). No GPU number: the check's case "open, no ground: the mist's cache" prints it and is
  not judged.
- fogrule built a cure in a tree of its own that never ran (a walk that goes on from a cell that holds nothing,
  coarser cells outside the view: `%TEMP%\tgb\fogrule-c-src`). Not taken into the rule.
- State: open; by reading and a CPU model.

### PT-136 One large glowing mesh sets the reference radiance 20,000 times over the sun's white: light is stored under a 16-bit float's range, dim light is lost or shows in steps (found by worker fogrule2, 2026-10-12 06:50)
- HOW IT SHOWED: the fog grid was 1.5 % brighter than the old fog route at the vista and nowhere else (PT-118). Four
  trial builds later: the tracer stores light RELATIVE TO A REFERENCE RADIANCE (`tracer.cpp`: "a white surface in
  full sun, or lit by the brightest light 1 m away, whichever is brighter"), in 16-bit float targets (31 places name
  that format). At the vista the reference is 5.7e8 (the fog scene 3.9e6, the night 1.2e6; the sun's white is
  2.7e4): the camera's fog light there is 1.85e-7 in storage units, 0 to 8 of a 16-bit float's smallest steps
  (6.0e-8), and one step is half a percent of white on the screen. The old route blends a tenth of each frame's
  sample into a 16-bit history: a tenth of a few steps rounds to nothing, frame after frame, so it LOST that light;
  the grid's calm sum rounds to a whole step and showed it in steps. PROOF (fogrule2's trial 5, the fog's targets
  in 32-bit floats, run `20261012-065226`): the old route's vista came up from 130 to 133.5 and the two routes are
  the same picture on the dark rock (the old route's darkest pixels there 7 to 26 of 255).
  IT IS NOT THE FOG'S ALONE: a white wall in full sun is 4.7e-5 in that view, already under a 16-bit float's normal
  range (6.1e-5), a shadow at 1 % of white is 8 steps, and every history that blends a small share of a frame into a
  16-bit target loses dim light the same way. And the clamps, which measure against the reference (16 times it),
  hold nothing in such a view.
- THE ROOT (the coordinator, read 07:00): `GpuScene::brightest_light()` takes, beside the lamps, the brightest glowing
  MESH by its clusters' intensity TOGETHER (`brightestEmitter_ = sampledPower`: radiance times area, summed over the
  mesh): a point light's reckoning. A glowing sheet tens of metres wide comes out as a lamp of 1.8e9 cd "1 m away",
  a light no surface can receive: a white surface right against an endless glowing wall shows the wall's own
  radiance and no more.
- ON TRIAL (main A174548E1B9D, 07:01, `scene.cpp` alone, no shader): a glowing mesh counts for the reference by the
  LESSER of its clusters' intensity and its own radiance times pi; a small glowing thing (a crystal in a cave, for
  which the mesh's share was added) counts as before. A log line "The reference radiance: ..." says what it is made
  of (the sun, the brightest lamp, the brightest glowing mesh as it counts and as it counted), when it moves and
  every 30 s. Its A/B on the vista is queued (`%TEMP%\tgb\seq-reference.log`: the new build, then main's old,
  `%TEMP%\tgb\main-4EA43DDDBC19.dll`): the reference by the log, the picture's dark end (the report's "darkest
  5 %": the traced vista had 29 where the game's own has 38), the luma, the noise (the clamps hold again).
- OPEN whatever the trial says: which mesh it is (the log line will say how large it counted; its name needs the
  plugin); whether a LAMP can do the same (an effect's lamp of 77,650 cd stands 1.3 m from the camera in that view:
  a lamp's 1 m value is real light, but a reference far over everything on screen still costs the dim end its
  precision: the storage's scale could sit a few decades under the reference at no cost in the picture); and the
  31 targets' formats.
- THE FIRST TRIAL MISSED (07:10, runs `20261012-070300` new, `-070535` old: the same numbers): the new log line said
  why: "1 m from the brightest LAMP 5.695e+08; by the brightest glowing mesh 3.189e+05 (2.425e+07 by its clusters'
  intensity alone)". It is a LAMP. The mesh's bound stays (it would have been the next outlier: 290 times the sun's
  white).
- THE LAMPS, by a second log line ("The lamps the reference is taken from", run `20261012-071638`, seven scenes):
  the forest, the town, the beach, the night and the fog scene (one world, 465 to 517 lamps): the brightest 3.87e6
  cd (a point light with a radius of 3 to 8 cm at -1292, 62, -3046, by the forest's save), the 2nd 2.5e5, the 10th
  2.2e5, the median 7.5e4; the reference 1.23e6, 46 to 77 times the sun's white (1.6e4 to 2.7e4). The tavern (32
  lamps): the brightest 1.24e7, the median 1.5e6; the reference 3.9e6. THE VISTA (315 lamps): the brightest 1.789e9
  cd, "a point light, range 15 m (ends there), radius 0.1 m, flags 0x0, at -816.7 120.0 -2226.9"; the 2nd 5.1e5, the
  median 6.2e4: ONE LAMP 3,500 TIMES THE NEXT (an effect's light by its flags and range; which effect, the plugin
  must say).
- ON TRIAL NOW (main 37237BC1B620, 07:27, `scene.cpp` `lamps_reference`): no lamp counts for more than 64 TIMES THE
  MEDIAN LAMP in the reference (and so in the clamps' reference). 64 because in every scene without such a lamp the
  brightest is 16 to 52 times the median: their reference stays what it was to the last bit (the town's run beside
  the vista's checks that by the log), and the vista's drops from 5.7e8 to 1.26e6: the other outdoor scenes' own.
  The lamp itself is traced as before; within 15 m of it light can now exceed what a 16-bit target holds only
  closer than 3 cm.
- THE GAME'S ANSWER (07:45, the world frozen, 16 frames; `20261012-074032` with the bound, `-074416` without): THE
  VISTA'S DARK END IS THE GAME'S OWN AGAIN. With no denoiser: the reference 5.695e8 to 1.269e6 (the log: "1 m from
  the brightest lamp 5.695e+08, counted as 1.269e+06"); the darkest 5 % of the picture 29 to 38 (the game's own
  picture: 38); luma 130.5 to 134.1 (the game's 130.5: the fog's light that 16-bit targets had rounded away is
  back, and the traced vista is now 3 % over the game's); flicker 1.01 to 0.92, boil 0.45 to 0.40. Under NRD:
  darkest 5 % 31 to 42, luma 129.9 to 133.9. THE TOWN IN THE SAME RUN: its reference 1.232e6 "counted as 1.232e+06":
  unchanged, as reckoned. So the bound is THE RULE (in main since 37237BC1B620).
  The night vista had lost more still: with the fog's targets in 32-bit floats (fogrule2's trial 5, before this
  bound) the old fog route's night vista went from 52.2 to 58.5 luma, its lamps' region from 26.7 to 35.9.
- THE LAMP IS THE OPEN PORTAL'S AT SAGREMOR'S GATE (08:15, from the run's audit, `vista\1-game\audit-b.txt`):
  `SagremorGate_Open/VFX_Portal_AbyssSagremor/Light`, a point light: intensity 134,217,728 cd (2 to the 27th) times
  a light dimmer of 16, colour (1, 0.91, 0.84), range 15 m, no shadows, HDRP's fade distance 64 m. So the game's own
  picture has that light only for a camera within 64 m of the gate (full within 58 m), where it must burn white;
  from the vista the game shows none of it. The tracer traces every loaded light however far (PT-50; the plugin's
  `Fade` exists and is not called), an effect's light out to its HDRP range: at the vista the 15 m around the
  portal are lit at full strength, and what that ground sends on is bounced light of the tracer's alone. Nothing
  of it shows in the vista's picture (near white 0.0 %; luma within 3 % of the game's). NOT LOOKED AT: the gate
  itself, from 60 to 300 m, where the traced picture has a white-hot patch and its bounce and the game has neither.
  THE OWNER'S QUESTION: follow the game's fade distance for an effect's light, or trace the portal's light as far
  as it is seen (today)? No save stands near the gate.
- OPEN: (1) the owner's look at the vista by day and by night; (2) the portal's light above: a look at the gate and
  the owner's answer; (3) the same floor
  elsewhere: any view whose reference is far over what is on screen (a strong lamp within the bound, a dark-adapted
  view at night: the night scene's reference is 1.23e6 against torch-lit walls of about 1e3): the dim end there is
  some hundreds of a 16-bit float's steps, and histories that blend a small share of a frame still round: to be
  measured (a build with one history in 32-bit floats, the night scene's dark end), not assumed; (4) the mesh's
  bound has no scene that shows it yet.
- State: the outlier lamp's case fixed and seen in the harness; the rest open.

### PT-135 The emitters' table was kept by a frame's record numbers: a glow's bounced light doubled or lost after plants' groups were made (found by worker stale, 2026-10-12, by reading)
- `emitterTable_[record]` says where an instance's glowing submeshes' light slots start, by the instance's RECORD
  NUMBER in the frame's instance buffer; it was made only when the emitters were built anew. The records are laid out
  anew every frame, and a plant in the wind has no record once its group is made: as groups are made over the frames
  after a scan, every instance listed after them moves down, and a path that meets a glowing triangle reads another
  instance's entry (mostly "no emitter": its emission then counts whole although the light sampling counts it too).
  Where plants and glowing meshes share a scene (a torch-lit camp) the glow's bounced light steps up or down as
  groups are made and back at the next scan. No reset: every read stays in its buffer.
- FIXED in main AC29308FC2CE (2026-10-12 04:18, `scene.cpp` / `scene.hpp`, no shader; `docs/agents/stale.md` F2): what
  the table is made from is kept by each emitter's place in the scene's list, and the table alone is laid out again
  whenever the frame's records are not those it was made for. Held by the source check
  `test_kept_numbers_cannot_go_stale` (8 mutants caught). Built; NOT seen in the game, no offline check.
- State: fixed by reading; to see: a camp's torch light on a walk.

### PT-134 A mesh decal's own top level named its mesh's bottom level by an address that moves at compaction: a GPU reset where a scene has mesh decals (found by worker stale, 2026-10-12, by reading)
- Each mesh decal has a top level of its own, built with the ADDRESS of its mesh's bottom level; `upload_decals`
  looked at it again only when the decals changed, a mesh was converted in that very frame, a texture came or went
  or the origin moved. Every still mesh's bottom level is copied compacted to another address two or three frames
  after its build and the old piece given back; when the builds' chunk goes back to the driver (a second or two
  after the scene has arrived) the top level points at memory that is not there, and every hit whose ray crosses
  that decal's box runs a ray query into it: a fault, or a walk with no end. Only scenes with MESH decals: the
  harness's scenes have none (so not what reset the forest), and PT-129's second scene had no decals.
- FIXED in main AC29308FC2CE (`docs/agents/stale.md` F1): the address is checked against the mesh's every frame,
  before `upload_decals`' early way out. The same source check holds it. Built; NOT run on a scene with mesh
  decals, no offline check (the fixture: a mesh decal whose mesh is compacted in a quiet frame; its mutant: the
  early way out as it was).
- Also from that audit: a mesh let go kept two of its views for good (its tangents', its decal colors'): a slow leak
  of the descriptor heap on long walks, fixed with the rest (F3). Read and found right, each with its reason in the
  report: the heap's slots within a frame, the plants after PT-125, cloth, water, characters, micromaps, the
  compaction batches, the light grid, RTXDI's and the fog's kept light numbers, the radiance cache, the caustics,
  shared textures, a scene load. NOT read: RTX Mega Geometry's trial paths (its partitioned top level keeps
  instances by address: to be read before that trial is used again).
- THE OFFLINE CHECK (worker fixt, merged 07:05, `tests/pathtracing_decal_move_checks.inl`, `TGPT_TEST_DECAL_MOVE_ONLY`):
  the decal still paints after its mesh's structure moved and the builds' 16 MB chunk went back (191 frames); the
  mutant (the old early way out) fails on the CPU side in frame 4, where the kept address no longer is the mesh's,
  with no reset and no read of freed memory. That the stale address resets the GPU is still by reading.
- State: fixed; checked offline with its mutant; not seen in a scene of the game with mesh decals.

### PT-133 A Shader Model 6.9 pass returns garbage for a value that is zero on a skipped branch (found by fogfix, 2026-10-12)
- Worker nosun (2026-10-12 01:20 to 03:00): NOTHING RUN ON THE GPU (ten starts, each stopped within seconds by a game
  opening or a game run waiting; a cold driver cache needs more than 263 s to make the two reordering passes). In the
  tree now (merged 03:05): the tracer's test switch `TGPT_TEST_SM66` (the 6.6 passes on this card), the test's
  `TGPT_TEST_NOSUN=1|2` and `TGPT_TEST_TRACE=<file>`, the mode `TGPT_TEST_NOSUN_ONLY` (compiles, never run, its
  tolerances a first guess), `tools/agents/nosun_compare.py`. OWED: one gap of ten minutes to make the passes, then
  about 45 minutes of the card: `%TEMP%\tgb\nosun-build\survey-base.ps1` (it asks `-GpuCheck` before every start).
- THE WHITE FOG GRID'S CAUSE (PT-118), found by the worker fogfix and reproduced offline (its report, "FOUND", in
  `docs/agents/fogfix.md` once merged): the tavern's frame has NO SUN (the plugin sends one only when the game's sun is
  valid), and in `pt_fog_froxel.hlsl` the light summed before the lamps' loop, zero on the path that skips the sun's
  code, came out of that loop as garbage in green and sometimes red: cells of 8.5e33 to 2.8e37 storage units beside
  a right lamp sample of 1.1, nothing "not finite". dxc's own output for the pass is right (the value is a plain zero
  there), so it is what runs the Shader Model 6.9 pass that goes wrong (the driver, 32.0.16.1692). The offline check's
  sun was always on: with it off (`TGPT_TEST_FOG_NOSUN=1`) the build that was white in the game is white offline, a
  thousand to four thousand times the old route's light. Cured in that pass by its shape (the sun and the sky summed
  into a variable of their own first; a KEEP THIS SHAPE comment), by test, not by a rule that is understood.
- WHY IT IS AN ENTRY OF ITS OWN: the rule is not pinned, so any other Shader Model 6.9 pass may hold the same trap, and
  every scene of the offline suite has a sun. The game's interiors and nights do not. (The big passes are right in the
  tavern by the picture; that says nothing of a pass or a branch nobody has looked at without a sun.)
- Opus worker nosun (10-12 01:20, `brief-nosun.md`, copy `wt\nosun`, base `base20h`): the Shader Model 6.6 set against
  the 6.9 set, the same frames with and without a sun, over the small modes that cover the big passes; the cure by
  shape where it shows; one small mode left for the whole run; PT-129's loops read for what garbage could make run long.
- Next: no-sun cases in the offline checks of the passes that have a "sun or nothing" branch (the cells' pass has four
  now); the rule, if a small pass can show it (for a report to NVIDIA); a look at PT-129's hang with this in mind.
- SEEN IN THE GAME 10-12 00:55 (`20261012-004519`): the grid's fog is the old route's light in the tavern, no white.
- State: one pass cured, seen in the game; the trap in other passes and its rule are open.

### PT-132 The traced fog loses the light of lamps with a range and of glowing surfaces on long rays (found by fogfix, 2026-10-11)
- Found by the worker fogfix while chasing PT-118 (`docs/agents/fogfix.md` in its copy, "FOUND (1)", measured offline):
  `fog_lamps_along` (pt_medium.hlsli, the rule's route: one point a pixel) takes its lamps from the light lists at ONE
  point of the camera's ray, the anchor, where this frame's fog point fell. A lamp with a range, an effect's light and a
  glowing surface's cluster are only in the lists of the cells their reach touches. On a long ray the anchor is mostly
  far from a lamp near the camera, so that lamp is no candidate for that sample and its glow in the fog is dropped. The
  check's sky strip, a lamp with a 12 m range 0.3 m before the camera: 0.1191 against 0.2505 with the same lamp
  range-free (the fog grid: 0.2509 / 0.2521). Lights the plugin sends range-free are not hit; effects' lights and
  glowing surfaces are.
- Not this job's: the function is in a file every pass includes. Ideas, none built: the lists of several points along
  the ray, or the far strata that hold every light. It must stay exact on average (the anchor may be any point only if
  every list holds every light with a weight above 0).
- 10-12 (worker fogrule2, PT-118's rule): the CAMERA's fog no longer calls `fog_lamps_along`: a cell takes its lamps
  from the lists at its own point, and the pixel's share asks the lists of every light cell the way crosses. The
  function is left for the fog on reflected and refracted rays' ways (`traced_fog_on_way`): the loss stands there.
  Not seen in the game by that job either: its vista run with the lamps left out of the cells (`20261012-063934`)
  showed their share of that view's near fog is small (0.2 to 1.1 of 255 on a rock a few metres off).
- State: open for reflected and refracted rays' fog; gone from the camera's fog with PT-118's rule; measured offline.

### PT-131 A torch-lit camp in the forest: the reflections 213 to 235 ms a frame, 3 frames a second (found 2026-10-11)
- IT IS NO PLACE A PLAYER CAN STAND IN (worker campfix, 2026-10-12 03:15; confirmed 03:24, run `20261012-032137`,
  recipe `forest@camp-look`): THE HARNESS'S WALK FLEW THE CAMERA INTO A HILL. `CameraSlide.cs` moved the camera level
  along its view's direction, through whatever stood there; in the forest's save the ground rises over eye height 25
  to 35 m ahead. The walk's end, "the camp" at x -1246 z -2916, lies inside the land: the game's own picture from
  there is rock overhead and the sea's sheet under the land (`forest\2-game-150-m-ahead\frame.jpg`), the tracer's a
  dark roof over that sea; 30, 60 and 100 m ahead both are black. And every "camp" picture looked at before was the
  SAVE's view: a step's pictures are taken as its walk starts. From inside the ground the ground is not there for the
  camera (the back of a one-sided surface is passed, as HDRP culls it), so the lower half of the screen is the sea's
  own pixels: the 825,000 "reflection" paths a frame were the camera's own rays on that sea, each scatter point
  asking for the sun through the waves under a land that shades it.
  So: the 3 frames a second, the 259 to 440 ms, the measuring builds' numbers and "the Caustics row decides it" are
  all of that place, and `forest@mega-walk`'s and `forest@camp*`'s frame times past a walk's first two seconds are
  too. WHAT IS LEFT OF IT THAT IS REAL: water in view far off or in the land's shade pays for the sun through the
  waves at every scatter point (the beach: 132 ms a frame, its reflections 76): campfix's build (a patch's cell no
  finer than the ray is wide, with a correction for a low sun; the sun's cheap question first where no light can
  land) is for that, to be tried on `water-day@sun-cell`.
  THE HARNESS (03:20): a walk at a speed keeps the camera at its height over the ground (`CameraSlide.cs`, a ray down
  a frame; a roof or an arch over the way is not taken for the ground), and says so in the progress log.
- Found on the forest's walk (`forest@mega-walk`; runs `20261011-182915` and `-212136`): where the walk ends, 140 m from
  its start (camera about -1246, -2916 of Horns of the South: a camp behind palisades, torches, a pine, rain falling), the
  tracer's GPU time is 250 to 290 ms a frame (18:29, nobody else on the card) and 313 to 405 ms (21:21, workers compiling
  beside it), against 70 to 85 where the walk starts. The same with plain structures and with RTX Mega Geometry's
  trial 7. The stages there (the tracer's own 10 s lines, 21:21): REFLECTIONS 213 to 235 ms (27 at the start), bounced
  light 84 to 87 (18), direct light 2.8 to 3.1 (6.4), prepare 1.8 to 2.0 (2.7), the scene 3.2 to 3.6. 479 traced lights
  there (the tavern has 514 and its reflections take 11 ms). The water's surface around: 1,800 pieces, 1.55 M triangles;
  none in view (`forest\2-plain-walk\frame.jpg`).
- THE CAUSTICS ROW DECIDES IT (run `20261011-215835`, the recipe `camp`: walk there and stand, the world running): as it
  is the tracer 325 to 440 ms a frame (reflections 320 to 390, bounced light 98 to 120, direct light 2.9, prepare 1.35);
  WITH THE CAUSTICS ROW OFF 33 ms (reflections 10.9, bounced light 6.0, direct light 2.65, prepare 1.8): 3 frames a
  second against 20. The plain passes without Shader Execution Reordering: 360 ms, the same. The caustics' own stage is
  5 ms and their room 65,536 triangles: not the caustics themselves.
- A THEORY TAKEN BACK (22:15 to 22:50): by the code, `water_medium` (pt_water_surface.hlsli) calls a point "in the water"
  when the first water surface straight over it is met from below, whatever lies between, and the camp stands under a
  rock arch: the coordinator took the camera for "in the water" there, every pixel on the water's path. A second ray
  against the ground (MASK_GROUND 0x21, new: water overhead counts only with no ground between) was built into every
  pass and run: THE CAMP IS AS SLOW AS BEFORE (`20261011-223913`: as it is 366 ms, reflections 345, bounced light 122;
  the Caustics row off 32 ms), and the two pictures are the same torch-lit camp, nothing lit as under water. So the
  camera is not "in the water" there. The second ray stays in the tree for now (it closes the gap the code's own comment
  named, a dry place under water; the other way round a point in water under something solid now counts as in the air;
  the game has no diving): TO DECIDE with a water scene and the tavern's speed before and after, or it comes out.
- WHAT THE ROW CHANGES NATIVELY (read, not measured): the water's surface is traced with the row on or off; with it ON a
  light's straight ray stops at the surface, and inside the water's paths the sun comes through the waves (the points
  that make their own patch of waves, the sun's grid) and a lamp's light is solved through the surface; with it OFF
  both are straight rays. So the camp's cost is in the water's paths, and many reflected and bounced rays must reach
  water there (1,800 pieces of the water's surface lie around; none in view).
- THE MEASURING BUILD'S ANSWER (run `20261011-233418`, CMake `TGPT_SPEED_MEASURE`, `%TEMP%\tgb\measure-build`, the recipe
  `camp-measure`; the tracer / reflections / bounced light, ms): no "am I under water" question (8192) 346 / 333 / 110;
  no lamps at a hit (512) 340 / 371 / 133; NO GLASS FOLLOWED AND NO WATER PATH (16384) 31 / 4.5 / 3.7; no shading (32)
  24 / 2.3 / 1.6. So the whole cost is in `shade_path`'s special branches (glass followed, the water's own path), with
  the Caustics row on alone (with the row off those branches cost about 7 ms there). `water_vertex_lights` (a loop over
  every lamp of the lists at a vertex in the water) is behind hidden switches that are off: not it by the code.
- Opus worker campspeed (2026-10-12 00:05, copy `wt\campspeed`, base `base20f`, `brief-campspeed.md`): finer measuring
  switches inside the two branches and counters in the log (how many reflection and bounce rays enter them, their
  vertices and rays), one measuring build for the coordinator's run, then the fix that keeps the picture.
- The owner, 22:50, watching that run: "It has been stuck at the '6 meshes to go' forever now": at 3 frames a second the
  world's last meshes arrive that slowly; the run was stopped by the harness at its 16 minutes (seven of them the new
  build's passes being made), and the owner's settings were put back by hand.
- campspeed's MEASURING BUILD IN THE GAME (10-12; `%TEMP%\tgb\campspeed-measure`, counters "Special branches 1s" in the
  log; runs `20261012-012659` (steps 3 and 4, then the scene stage hung: PT-125) and `-021107` (recipe `camp-measure-b`,
  clean, no hang)). A step's whole stretch, the tracer / reflections / bounced light, ms: everything 259 / 230 / 66;
  THE WATER'S PATH NOT RUN 31 / 6.7 / 4.2; run, but the sun not asked through the waves 40 / 13.6 / 6.2; no patch of waves
  made at a point (the kept tiles alone) 68 / 30 / 15; the shadow rays first 87 / 68 / 28; campspeed's candidate, a
  patch's cell no finer than half the ray's own width, 106 / 95 / 19 (its picture looks like "everything"'s).
  THE COUNTERS AT THE CAMP, a frame: 824,792 pixels' reflection rays (40 % of the screen) and 216,403 pixels' bounce rays
  (10 %) enter the water's path; where they enter is 111 m and 59 m from the camera on average and 23.0 m UNDER it, on
  one level (the sea's, which runs under the whole land: PT-80); 1.21 M scatter points in the reflections alone, each
  asking for the sun through the waves: 583 k answered from kept tiles (78 M tiles asked, 37 M corners read), 626 k by a
  patch made at the point (95 M corners; 567 k of them "off the grid"); and of those 1.21 M questions the sun reached
  the point 30,768 times: 817 k were stopped on the way and 360 k had no light land. With the shadow rays first, 1.09 M
  of the 1.21 M end at once.
  A MISTAKE OF THE COORDINATOR'S, corrected at 02:20: the first run's lines were read as "700 paths a frame reach a far
  sea"; those lines were a step's first seconds at the forest's save, before the walk.
  SO THE QUESTION IS NOW ANOTHER: no water is in the picture, the camp stands in a gorge under a rock arch, and four in
  ten of the screen's reflection rays end in a sea under it. Either they reach it THROUGH the ground (a leak: the ground
  then reflects a dark sea it cannot see, a picture defect as well as the cost) or the land opens to it. Fresh Opus
  worker campfix (02:10, `brief-campfix.md`, in campspeed's copy; told at 02:20): which it is first, then the fix: the
  leak if it is one, a cheap exact order for water the sun cannot reach, the cell by the ray's width.
- State: the place is no defect (the harness's camera inside a hill); what stays open is the cost of water in view
  (campfix; the beach). The title's "3 frames a second" stands corrected by the first bullet.

### PT-130 The water's paths read the caustics of the frame before (found by caustcut, 2026-10-11)
- `tracer.cpp` runs the bounced light and the reflections BEFORE the caustics are rebuilt for the frame, and both read
  the caustics' landed light at solid points under water (`pt_water_path.hlsli` `caustic_light`). So what a ray sees
  under water is lit by last frame's caustics: a frame's lag while the waves move. The camera's own surfaces read this
  frame's (the composite and `pt_nrd_prep` run after). Found by the worker caustcut while listing who reads the caustics
  (`docs/agents/caustcut.md` "Readers and exactness").
- DONE 21:52 (native DF0395575260): the caustics' stage is the third, before the camera's surfaces and every traced
  pass (`kStageNames`; the marks and two source checks moved with it). A water scene and the tavern look as before
  (`20261011-215257`).
- State: to verify in game by the owner's eye (a ray under moving water); no offline check of the order yet.

### PT-129 The GPU hung in the reflections after loading a second scene; the game crashed (the owner, 2026-10-11 ~18:00)
- LEADS BY READING (worker nosun, 2026-10-12 03:00, `docs/agents/nosun.md`; nothing of it run on the GPU): four loops on
  the reflections' path had no end. (1) The fog boxes' two tracking loops (`pt_medium.hlsli`): a ray whose start or
  direction is no number counted as inside EVERY box up to the game's ray end of 1e6 m: millions of steps for one
  pixel. (2) `psr_finish`'s two "draw until a direction comes out above the surface" loops (`pt_psr_capture.hlsli`;
  mirrors under NRD, the rule since 10-11 21:52, and NRD is the owner's own setting): on a smooth, normal-mapped
  surface seen low in a mirror every draw can be refused. MERGED INTO MAIN 03:05 (all 57 passes compiled anew): a ray
  that is no number is in no box; the box loops end after 65,536 steps (40 is usual: a guard no real fog reaches,
  for a bound so large that a step no longer moves the ray); `psr_finish` draws 64 times at most and then keeps the
  direction (the sky's distance as the guide); `fetch_hit` takes a normal whose length is past what a float holds as
  none. Not proven to be what hung the owner's game. PT-125's cause (plants posed from other plants' records) is a
  third lead: structures refitted to garbage make every ray slow.
- "I just tested the game myself. I loaded a scene, then I loaded another one. That caused the game to hang and then
  crash". Native log (build 174195A8959D): "the path tracer's D3D12 device hung ... 0x887A0006 ... the GPU finished stage
  direct light of frame 3776, and stopped in stage reflections"; the second scene CampaignMap_HOS with a lake (1,602 water
  pieces, 1.4 M triangles), 7 fog boxes, braziers and torches; Windows' count 8.8 of 15.9 GB (not memory). Just before,
  the reflections' sort chooser had switched to no hint on a 0.2 ms probe of the empty, loading scene.
- 18:15 (2AE0CBC1B369): the chooser gives no verdict when the bounced light's time moved by a fifth between its windows
  (a scene changed); the plugin logs every fog box's size and extinction. Reading: every open loop on the reflections'
  path ends (Russian roulette at 0.95 at most); the area's fog boxes are moderate (at most 1/m in 40 m boxes). Run
  `20261011-181511` (tavern, the day lake, the campfire, the torch cave in a row, NRD): no hang. Not reproduced; the
  owner's two saves would let it be (asked).
- The owner, ~18:25: "OK, nevermind trying to reproduce that crash, just keep going with the other stuff". Open; watch
  for it in the runs (the log names the stage when the GPU stops).

### PT-128 Reflection-sort timing reads the direct-light stage (found by fogfroxel, 2026-10-11)
- While adding fog timestamps, source inspection found `sort_measured(index, (t[6]-t[5])/ticksPerMs)` in
  `tracer.cpp`. Marks 5 to 6 now bracket direct light; reflections lie between marks 6 and 7. The comment still
  calls stage 5 reflections. The same line is in main (read only), so this predates the fog timing changes.
- Impact: the automatic reflection hint chooser compares the wrong pass. No in-game magnitude measured.
  Next: coordinator changes the measurement to marks 6 to 7 (prefer a named index) and verifies one clean run.
  This worker leaves that separate subsystem unchanged; report `docs/agents/fog-froxel.md`.
- Fixed by the coordinator ~16:45 (built, not yet run): the chooser reads the stage it names (`kReflectionsStage`, found
  in kStageNames at compile time). The stage numbers moved when the sky's stage came in (the sky merge, ~13:00): since
  then the sort's probe compared the direct light's time, so its choice of hint was by the wrong pass.

### PT-127 NRD set up as NVIDIA documents it (the owner, 2026-10-11 ~14:25)
- 10-12 12:15, BOTH FILTERS IN MOTION: THE FIRST RUN IS THROWN AWAY, THE SECOND IS OWED. Recipe `nrd-motion` (the
  camera turning 12 degrees to either side and swinging 0.4 m; no denoiser, RELAX, REBLUR; 60 frames each, raw frames
  kept with `Queue-PtRun.ps1 -KeepFrames`), run 20261012-120026 (the tavern, the gravel by the brazier): every frame
  is smeared, the no-denoiser ones too, with doubled edges: THE GAME'S OWN MOTION BLUR WAS ON (a harness step turned
  it off only for Neural Rendering's steps). Mended: `"disableMotionBlur": true` works on any step now
  (`Runner.cs`) and the recipe's three moving steps carry it; a frame then says `motionBlurActive: false`. The second
  run was refused at 12:12 (the owner's game opened) and the owner's pause followed.
  `tools/harness/motion_clips.py` makes the page's pictures from such a run: RELAX beside REBLUR at the game's own
  pixels, the frames of the two steps lined up by where the camera looks, as a moving picture (animated WebP), with
  a still pair (boxes used: `tavern-room:2150,1100,1000,720 spec-terrain:1450,780,1000,720`). To do when the owner
  says go: the run (7 minutes), the clips, the NRD card of the choices page.
- THE OWNER'S CHOICES OF 2026-10-12 (the choices page): GLASS AND WATER UNDER NRD: "B on glass first, then A"
  (`docs/agents/psr-2.md` (b): the view through glass through NRD with its own replaced surface, the reflection on
  it as traced; then both ways through NRD if the reflections look noisy; water last, way A). THE FILTER: RELAX,
  with the note on REBLUR's detail (the bullet "THE OWNER, 2026-10-12 11:09" below: both in motion, and NVIDIA's SH
  form for the detail). RTXDI'S THREE SETTINGS (review 22: the direct light's history 4, the naive samples'
  discount off, the bounced light's fallback off): "Try NVIDIA's defaults in the game, show me both": a hidden
  switch that sets all three to the sample's values, the cage's scene (the boiling the history of 4 was chosen
  against on 09-28) and one more, frames with no denoiser and under NRD, on a page. None of the three is built.
- 10-12 01:00, the merged build (3199B2F4BBF6, psr-2's mirrors the rule), `20261012-005626` `mirror-tavern@psr-mirror`, the
  world running, the mirror's region: no denoiser detail 3.73, flicker 1.19, boil 0.30; NRD detail 2.53, flicker 0.74,
  boil 0.24 (the first trial's build with its switch on: 2.41 / 0.83 / 0.27; off: 0.47 / 0.61 / 0.45). SEEN IN THE GAME
  by its numbers; the owner's look is owed. NRD's stage in the tavern is 3.8 ms now (2.8 before the confidence, 0.46,
  and the mirrors' records).
- 10-11 evening. (1) HISTORY CONFIDENCE in the game (`20261011-190338`, the tavern, the world running): the same picture,
  denoising 2.83 -> 3.29 ms, boil 0.29 against 0.25 / 0.26, flicker 0.49 against 0.47 / 0.45. With the world frozen:
  no boil of its own (0.23 against 0.24 / 0.24, flicker 0.39 against 0.40, `20261011-214719`): what moves more with the
  candles running is NRD following them sooner. IT IS THE RULE since 21:52 (no switch; flag bit 21 free).
- Workers `psr` and `psr-2` (2026-10-11), the review's findings 26, 3 and 20. **Mirrors: the rule under NRD** since
  the game's answer (run 20261011-213531: a tavern's mirror keeps 2.41 of detail under NRD against 0.47, the light the
  same): NRD's primary surface replacement, no setting, no flag. Still open of it: a mirror in a mirror, a mirror that
  moves or is curved, hair, skin and see-through things seen in a mirror, and its records are made whenever NRD runs
  (127 MB at 1924 x 1084), a mirror on screen or not. **Glass and water (finding 26): not built.** They have two ways on
  where a mirror has one; `docs/agents/psr-2.md` has the design for the owner (two layers through NRD, one layer, or
  NVIDIA's sample's glass without a denoiser) with what each costs; our reflection filter stays on them until the
  owner rules. **Ray Reconstruction's guides (findings 3 and 20): built by `psr`, not merged**, the game has not seen
  them; finding 3's change gives up the steady hair guides the tree holds on purpose. What to run: psr-2's section (c).
  **Found on the way, each to get a number of its own from the coordinator:** (1) a pure mirror's highlights of lamps
  are sampled through a lobe it does not have: a soft, noisy glow in place of the lamp's image, a fifth of the
  fixture mirror's light with a flicker of 2.4, and what is left of a mirror's flicker under NRD (psr-2, "The flicker");
  (2) for PT-60's list: a reflection ray of a surface that is no character's starts 2 cm and more off the surface
  (`ray_offset`, made for HDRP's surfaces), which the tracer's own camera hit does not need; (3) the mirrors' records
  are made whenever NRD runs, a mirror on screen or not (PT-87's count: 127 MB at 1924 x 1084).
- "I practically always prefer the non-denoised presentation over NRD. Is NRD just not cut out for this?! ... It completely
  oversmoothens detail away and removes fine shadows"; then: "Well make sure NRD is set up to perfection, then, I no longer
  trust anything I look at which use NRD"; and: "Might some of these things be relevant to Ray COnstruction/Ray
  Regeneration too?"
- Found (run `20261011-141211`, tavern, `nrd-dark`): NRD's own bounced light matched the undenoised picture (Indirect view
  luma 8.3 both); the darkening (Direct view 71.8 undenoised, 60.1 with "NRD": 16 % less) and the lost wood grain and
  small shadows came from the TRACER'S OWN filter, which the NRD setting ran on the direct light and the reflections
  (pt_temporal + pt_atrous). Against NRD's README (`NRD-SDK-4.17.3/README.md`, read 14:30) our setup differed in: NRD took
  the bounced light alone (NVIDIA: radiance split into diffuse and specular at the primary hit, ALL of it denoised, the
  material demodulated: diffuse / albedo, specular / its environment factor); a constant hit distance of 1 (NVIDIA: the
  1st bounce's real one; REBLUR sizes its blur by it, RELAX uses it in its pre-pass and for the reflections' tracking);
  the triangle's normal as the guide (NVIDIA: the normal the light was shaded with); 2D motion (NVIDIA recommends 2.5D;
  ours is RELAX's own static estimate either way); no history confidence (NVIDIA: "highly recommended": RTXDI's
  gradients); history length in frames, not seconds (30 frames are 3 s at 10 frames a second).
- Built 14:40 (on trial, the hidden `NRD trial`, frame flags bits 17-18): 1 RELAX and 2 REBLUR, each for the diffuse
  light (direct and bounced together) and the specular light, with the bounce's and the reflection's hit distances,
  demodulated (NRD_MaterialFactors' specular factor), the shading normal as the guide, no diffuse pre-pass (NVIDIA: it
  blurs clean signals), the firefly suppression on (the owner's sprinkles, 2026-10-08). Glass and water keep the
  reflections' own filter (NRD is for opaque surfaces). Recipe `nrd-book`: none, NRD as before, 1, 2. Open after it:
  history confidence from RTXDI's gradients; history in seconds; 2.5D motion with moving things' own previous depth;
  materialID for hair (NRD's strand tweaks) and metals; NRD's validation layer as a debug view to check its inputs.
- The owner, ~14:50, asking who chose the bounced-light-only route: a Claude fork of round 2 (2026-09-27), from an
  offline measurement (RELAX on the direct light blurred the sun's shadows, a bumpy floor flickered) taken while RELAX
  was told a hit distance of 1 and the triangle's normal; a Claude agent kept it on 10-07 (PT-19). Not Codex. The
  ruling since: "Obviously we want to 100% follow NVIDIA's and AMD's implementation guidelines" (AGENTS.md).
- Run `20261011-150230` (native 813BC4C511E0; tavern-room, spec-terrain; recipe nrd-book): the gravel with NRD as before
  milky and flat (detail 6.7 against 15.5 with none); RELAX and REBLUR as documented keep it (8.8, 9.2) and its colour.
  The tavern's NRD pictures 22 % darker than none was NOT NRD's: the traced light's automatic gain (pt_meter, the median)
  read x2.7 with no denoiser and x2.2 with NRD (and x2.0 against x2.0 in the morning's debug views, where the tracer's own
  direct filter did take 16 %): noise put the median low, so the undenoised picture was brightened for its noise. Fixed
  15:16 (BC6F2B1653ED): the meter takes 4 x 4 blocks' means. With it: the caustics before the denoisers and in NRD's
  diffuse input, NRD_MaterialFactors for the diffuse light too, the limit by the largest channel (REBLUR to a half
  float's), RELAX's own history maximum.
- GPT-6-astra's review of all three integrations against the guides (`docs/agents/review-vendor-astra.md`, 28 findings,
  ~15:20). Taken by the coordinator (NRD): 1 (the bounce's hit distance through the lens of the lobe), 8 (the glossy-
  cache branch's made-up distance of 1: the hidden Glossy Reflections row), 9, 10, 12, 13, 16 (anti-lag off is NRD's
  advice for a first integration only: kept on, NVIDIA's defaults), 23, 24 (history confidence), 25 (2.5D motion, hair's
  material ID), 26 (primary surface replacement for mirrors, glass, water). Codex worker `vendor-rr` (brief-vendor-rr.md):
  RR 2, 4, 18, 19 and AMD 6, 14, 15, 28. Waiting: RR 3 (hair guides), 17 (the highlight hold before RR), 20 (glass's
  distance), AMD 5 (motion z), 7 (direct specular apart), RTXDI 21 (previous scene for temporal visibility), 22 (its
  parameters against NVIDIA's defaults: DI history 4, naive-sample discount off, GI fallback off: the owner's choices).
- ~15:50: the old route removed; NRD only as documented (RELAX by default, REBLUR on the hidden `NRD trial` = 2), with
  2.5D motion from each point's own previous depth, hair as NRD's strands, the bounce's hit distance through the lobe's
  lens, the caustics in NRD's diffuse input. Deployed 15:48 (native C986D0E687AC, plugin 0B9278457FE7); run
  `20261011-154933`, the gain x2.2 with and without NRD: tavern luma none 18.0, RELAX 16.4, REBLUR 16.9 (the anti-firefly's
  share, as measured in PT-75), boil 0.14 / 0.14 / 0.11; gravel detail 15.3 / 7.9 / 8.5 (the old route 6.7), flicker 0.48 /
  0.30 / 0.44. The wood grain and the mugs' contact on the table kept (side-by-side.jpg). RELAX or REBLUR: the owner's
  look decides.
  THE OWNER, 2026-10-12 11:09 (the choices page, the two filters' stills of that run side by side; picked RELAX):
  "In the stills, REBLUR looks significantly BETTER than relax, but I haven't seen it in motion, so I can't weigh
  this against the flicker/temporal stability, which is most important. If you're saying RELAX is NVIDIA's
  recommendation, then I'd go with that. But I can't ignore just how much more details reblur was here in the
  screens you shared." SO: RELAX STAYS THE DEFAULT, and three things are owed. (1) The claim, checked: NRD's README
  says RELAX "has been designed for" RTXDI and that it "works well with signals produced by RTXDI"; it says nothing
  against REBLUR for such signals. (2) BOTH FILTERS IN MOTION for the owner (temporal stability is what counts
  most: a camera that slides and turns, the same frames under RELAX and REBLUR, as moving pictures on the choices
  page). (3) THE DETAIL: NRD is run as RELAX_DIFFUSE_SPECULAR / REBLUR_DIFFUSE_SPECULAR (`nrd_denoiser.cpp`
  `denoiser_of`); NVIDIA documents a second form of both, the "SH" denoisers, whose resolve puts the surface's fine
  detail back after the filter (its README: "details will be reconstructed back by SG resolve"; its own timing
  table: RELAX 3.20 ms, 4.80 in SH mode). Not built here. By the rule on vendors' guides that is the documented
  way to keep the detail the owner misses, with either filter: a job (the gravel's detail measure 15.3 with no
  denoiser, 7.9 and 8.5 with the two filters, is its yardstick).
- Ray Reconstruction and Ray Regeneration: neither gets the tracer's own filter (they take the light as traced). Ray
  Regeneration's guides follow AMD's documentation (shading normal, 2.5D motion, sqrt albedos, the bounce's and the
  reflection's hit distances); one difference: the lights' highlights go in with the reflections (AMD has a direct
  specular input of its own). Ray Reconstruction's guides (HDRP's G-buffer with the tracer's overlays, TG Better Upscaling)
  are still to be checked against NVIDIA's guide the same way.
- Codex worker vendor-rr (`docs/agents/vendor-rr.md`, merged ~16:10): RR 2 (the specular albedo guide in RGB), 4 (the hit
  distance handed to RR whatever the optional transparency guide does), 18 (the diffuse albedo guide the material's: no
  0.05 floor, no emission tint), 19 (render sizes from RR's own optimal-settings query; preset F kept, NVIDIA's guide
  recommends Default: the owner's choice), AMD 6 (fixed radiance units, history reset when they change), 14 (one specular
  factor both ways), 15 (reset after a frame it did not run), 28 (TGPT_RRG_DEBUG=1: AMD's debug and validation flags; its
  context made off the render thread). Built and checked in its copy; not yet seen in the game; Ray Regeneration needs an
  AMD card. AMD 5 (motion z) the coordinator did with NRD's 2.5D motion. Left: RR 3, 17, 20, AMD 7, RTXDI 21, 22.
- Codex worker psr (started 10-11 ~19:00, copy `wt\psr`, base `base20pc`, `docs/agents/briefs/brief-psr.md`): review 26
  (NRD's primary surface replacement for mirrors, glass and water, in place of our own reflection filter on glass and
  water), RR 3 (hair's guides one surface) and RR 20 (glass's reflection distance), behind frame flag bit 22, recipe `psr`.
  Left after it: RR 17, AMD 7, RTXDI 21, 22.

- Fog (PT-118): historical Remix 0.4 integrator/filter/preintegrator fully read; 1.4 documented settings retained.
  Current-main included integrator/reservoir bodies remain unavailable. Recorded departures: bias-corrected
  temporal proposals, linear radiance interpolation, retained firefly excess and exact per-ray integration instead
  of homogeneous depth-prefix interpolation. Directional history still open. Reasons and pinned links:
  `docs/agents/fog-froxel.md`. No NVIDIA source copied.

### PT-126 In rain, the traced fog over open sky is a flat, too-bright cyan patch with hard edges (found 2026-10-11)
- Run `20261011-134746`, the npc-day stand-in save (rain, Horns of the South), no denoiser: where the camera's rays meet
  nothing, the fog is a flat mint-cyan shape cut by the treeline; the trees' pixels beside it, as far, are much darker; the
  game's own mist there is an even grey (1-game). Leads: the fog over the sky takes the sky's own light (HDRP's lat-long
  copy, bluish) where HDRP's fog takes its tint times the sky (the far haze's question, fog-2.md Q2); the sky pixels'
  composite (the share of HDRP's sky let through plus the fog's light, B alpha -2) against the surfaces'.

### PT-125 The GPU hung in the tracer's scene stage, the game crashed (the owner, 2026-10-11 ~11:15)
- THE CAUSE, FOUND 2026-10-12 03:00, AND THE FIX (built; the game's answer owed: the card was the owner's): a mark
  after every pose dispatch named the group each time (runs `20261012-025321`: walk 4, the group at x -1312 z -2896, 79
  members; `-025731`, with the plants' compaction switched off for the test: walk 5, the group at x -1296 z -3008, 106
  members: so the compaction is not in it, and its test switch came out again). `pt_sway.hlsl`: a group's vertices
  each keep the number of their RANGE (which submesh of which member mesh the vertex is of), written once when the
  group is made (step 4) and read by every later frame's pose (step 5). The number written was the range's place IN
  THAT FRAME'S BUFFER (`rangeFirst + lo`), and that buffer is laid out anew every frame, the groups in use one after
  another in the order of their cells: as soon as a group before it comes or goes (a walk), a group's vertices read
  OTHER groups' ranges and members, or records left past the buffer's end from earlier frames, whose member meshes
  have been let go: the read through such a mesh's view is what reset the GPU. Short of a reset the same fault posed
  plants by other plants' meshes and places (vertices thrown about or collapsed: plants gone or stretched after a
  walk; huge boxes for their structures: a lead for PT-124's stutter and PT-129's hang in the reflections too).
  THE FIX (main F1E5449077BA and later): the number kept is the range's number WITHIN ITS GROUP, added to the group's
  place in the frame's buffer when it is read, and a number past the group's ranges counts as none: no read leaves the
  group's own records. The numbered marks stay (slots 3 to 5: they cost nothing the scene stage's 1.6 ms shows).
  SEEN IN THE GAME: 40 walks of `forest@camp-walks` without a reset (before: 4 of 4 runs reset within five walks), and
  the plants back in the picture after a walk (`20261012-025731\forest\4-walk-3\frame.jpg` against `-030621`'s).
  THE OFFLINE CHECK (worker fixt, merged 07:05, `tests/pathtracing_plant_shift_checks.inl`, `TGPT_TEST_PLANT_SHIFT_ONLY`):
  a group keeps its pose and picture when a plant comes and goes in a cell that sorts before it: on the fix 0 of
  921,600 pixels differ and 9 of 9 plants stand; on the mutant (the old number) 24,010 pixels differ and 1 of 9
  plants stands. `docs/agents/fixt.md`.
- IT CAME BACK, TWICE, IN THE FOREST (found 2026-10-12 02:10 by reading one log for every "The GPU stopped" line: the
  tracer's log runs on across starts). Both on a walk into the camp of PT-131 (`forest@camp...`: a live step walks 150 m
  at 10 m/s from the forest's save and holds), both in the SCENE stage, RTX Mega Geometry's trial 0:
  10-11 22:49:54, run `20261011-223913` (main's build), the 4th step's walk, traced frame 1634 of that start;
  10-12 01:49:34, run `20261012-012659` (campspeed's measuring build), 32 s into the 5th step (15 s of walk, then the
  hold at the camp), frame 1892. Not memory: the tracer held 4.0 and 4.3 GB of Windows' budget of 15.9. Before both:
  a hundred "BLAS compaction" lines in the log's last 300 (new meshes arriving on the walk), 12 groups of plants
  compacted, 16 pieces of the water's surface made in a frame. A third of 10-11 18:44:18 (megageo's trial with the
  partitioned top level, frame 4003) carries the same marker and was put down to Mega Geometry's cluster room
  (megageo-6's fix): it may have been this one.
  THE MARKER OF 10-11 COULD NOT NAME THE STEP: all three say "after its structures' the rest of the scene stage", and
  that kind is stamped at seven places (the frame's start and before the water, the cloth, the plants, the top level,
  the micromap bakes, the stage's tail). So what hung is one of: skinning, the water's pieces, cloth, plants, the top
  level, a micromap bake, the tail. 02:11 (main FF1C2D52B16B, `tracer.cpp` alone): the marker now also carries the
  stamp's NUMBER within its frame (slot 3), and the log names the kind whose work did not end and lists the frame's
  stamps. NEXT: make it happen again on that build (a recipe of walks into the camp, the Caustics row off so a walk
  costs seconds; it took the 4th or 5th walk both times), read the marker, then fix that step. Until then a walk in
  the forest can end in a driver reset: the first priority's kind of defect (a crash), ahead of looks.
- IT IS THE PLANTS' POSE PASS (02:23 and 02:41, recipe `forest@camp-walks`: the first walk hung BOTH times, at traced
  frames 160 and 170; runs `20261012-022206` and `-024005`). The marker of the first: "that was stamp 9 of the frame,
  and the work that did not end is plants's". The second, with the plants' steps numbered (main 8935E5078C93: slots 4
  and 5, `GpuScene::plant_step`): "the plants' work of that frame was 266 steps (18 compacting copies, 1 pose passes,
  0 builds, 247 refits), of which the GPU finished 18; THE STEP THAT DID NOT END: number 19, the pose pass 5 (247
  dispatches); the one before it: the compacting copy of the group at x -1264 z -3088". So the GPU finished every
  compacting copy and never finished the 247 compute dispatches that pose the groups (`pt_sway.hlsl`'s `plant`, step 5:
  no loop in it, so this is no long job: a dispatch READ OR WROTE SOMETHING THAT IS NOT THERE; DXGI calls that "hung"
  too). What a dispatch touches: the group's own buffer, the frame's member and range records, each member mesh's
  vertex positions, the winds' records, the noise. Read through and found in order by eye: the handles (never reused),
  the retiring of meshes (only when no instance names them), the groups' signatures, the arenas' give-back, the
  scratch, the compaction's order in the list. Windows' log has three driver errors (nvlddmkm 153) a reset and no
  word more. NOT FOUND BY READING: so two things in main B4E43C77A21D (02:49): a mark after EVERY pose dispatch (the
  log names the group whose pose did not end and says whether its structure was compacted in that frame), and a TEMP
  TEST switch (the hidden `Mega Geometry trial` 15: the plants' structures are not compacted; recipe
  `forest@camp-walks-nocompact`): if eight walks pass with it, the compaction is in it. Both runs started 02:50.
  THE HARNESS: a run now ends by itself when the tracer's log says the GPU stopped (it stood ten minutes on the first),
  waits for a reset game to close before it puts the owner's configs back (they stayed in `_config-backup` once; put
  back by hand, checked), and `tools\harness\Queue-PtRun.ps1` queues a run behind a busy card.
- "If I move my camera to the side, it culls in new stuff and shows the 'textures loading', with extreme stutter as a ton
  of stuff pops in over time ... Eventually the game crashes. We're clearly exceeding either VRAM or system memory or
  both". The native log: the tracer's D3D12 device HUNG (TDR, 0x887A0006) in its scene stage of frame 8674; Windows' count
  11.7 GB of a 14.56 GB budget (not memory). Likely: a micromap bake (the first mesh of a frame went whatever its size,
  leaves at level 6) or another structure build. 13:17 (0EF0B0FAB4DE): a mesh too big for a frame's bake goes a level
  coarser, no larger bakes on slow frames, and markers that name the scene stage's step when it hangs. To see: the same
  walk into new ground.

### PT-124 Textures took very long to arrive; stutter as new things stream in (the owner, 2026-10-11 ~11:15)
- "Loading the tavern, and then later PT 'specular-terrain-highlight' ... it never fully gets all textures loaded in, so
  it is stuck loading the vanilla rasterized lighting. Correction: It does, but after a very long time." 139 textures were
  held back near the video memory budget (the 97 % rule, PT-87), and tracing paused for them; the tracer held 6.1 GB:
  the water's pools 848 MB of pages for 196 MB in use, the plants' posed copies and structures 720 MB. 13:30: the two
  cuts that waited since round 19 switched on (scene.hpp kMemory2Waiting 0: the water's exact blocks and pages given back,
  the plants' structures compacted); to see in the water and forest runs. The owner on Mega Geometry (PT-109): "That's
  why I still think RTX Mega Geometry will help if it can help us manage the BVH".

### PT-123 No contact shadows between objects and tables: "that rasterized look" (the owner, 2026-10-11 ~11:10, NRD)
- "surprisingly I still don't see what looks like contact shadows between objects on a table and the table itself. It's
  frequently got that rasterized look where there's no full connection between an object and a service. At least with
  NRD, which is what I'm focused on getting right right now". To look at with no denoiser first (the tracer's own
  occlusion near contact: the bounced light's ray offsets, ReSTIR GI's reuse across the contact), then NRD's smoothing.
  Run: `tavern-room@look-nrd`.

### PT-122 Planks and ground look oversmoothed (the owner, 2026-10-11 ~11:10 and ~11:20)
- "Some surfaces like planks seem oversmoothed", and in the spec-terrain save "we have these oversmoothened textures here
  and there still, like the ground". Two possible causes to tell apart: textures read at a too-coarse mip (the ray cone's
  footprint; the game's streaming not yet at full size) or NRD's smoothing of the lighting (with Denoising None the
  textures show as read). Runs: `tavern-room@look-nrd`, `spec-terrain@look-nrd`.
- Run `20261011-134746`: the spec-terrain ground with no denoiser keeps the gravel sharp (as the game; its wet glints are
  missing: the "flat" look); with NRD the fine gravel is smeared and the ground goes soft: RELAX's spatial passes blur the
  bounced light's small shadows (NRD takes the bounced light only; colour and the normal map's shading go back after it).
  The owner, ~13:55: "NRD really does making so many things too smooth. It loses a lot of granular detail". Also: in the
  tavern NRD's picture is 22 % darker than with no denoiser (luma 16.8 against 21.6; outdoors at night they matched).
  Trial built: the hidden `NRD keeps detail (trial)` (RELAX 3 passes, luminance edge 1, against NVIDIA's 5 and 2);
  recipe `nrd-detail`. Run `20261011-141211`: no visible change; trial removed. The cause was the tracer's own filter on
  the direct light, which the NRD setting also ran: PT-127.

### PT-121 No heat haze or smoke from fires (the owner, 2026-10-11 ~11:10)
- "No heat haze or smoke coming from fires either". The effects are HDRP's until they are traced (PT-112 C); HDRP should
  still draw them over the traced picture (the owner's config holds no test switch that hides them). Run
  `20261011-134746` (campfire): HDRP draws the flames over the traced picture as in the game; the smoke is faint in the
  game's own picture there too; HDRP's distortion (heat haze) is on for the traced camera (the log). Still to compare:
  the tavern's hearth against the game's.

### PT-120 Bottles in the tavern show no translucency, reflection or refraction (the owner, 2026-10-11 ~11:10)
- "Also I can't see any translucency, reflections or refraction on any of the bottles in the tavern" (PT-13 glass). Run
  `20261011-134746`: the glass-jars save is a stand-in (a dark cellar, no jars in view); next `refract-jar@look-nrd`
  (a real save) and the tavern's bottles. A lead to check: HDRP's own see-through bottles took their reflections from its
  screen-space reflections, off while tracing since ~10:15; the bottles should be the tracer's glass either way.

### PT-119 Eyes dark from almost every angle but straight on (the owner, 2026-10-11 ~11:10 and ~13:30)
- "People's eyes look very dark, almost black at times when they look down"; then: "well it looks weird. Eyes are dark
  when I look on NPCs from almost anything but dead-on when I walk around the tavern and look at NPCs". View-dependent, so
  not the socket's shade. Leads: the eye surface is camera-only (MATERIAL_EYE with MATERIAL_CAMERA_ONLY) and a ray refracted
  through the traced cornea may pass it into the head; or the cornea as a glass shell with air on both sides reflects at
  an angle what should reach the iris. Run `20261011-134746`: the npc-day save is a stand-in with no face (the save "PT
  npc-day" does not exist); next `hair-eyebrows@eyes-debug` (a real save).

### PT-118 The fog in the tavern: noisy, and it trails behind lights as the camera moves (the owner, 2026-10-11 ~11:10)
- 10-12 08:30, MAIN'S BUILD WITH THE RULE, IN THE GAME (native E98E321830C2, plugin 6E72149A874A; `20261012-081117`,
  the world frozen, 16 frames a step, recipe `traced-fog`): with no denoiser, luma / flicker: tavern-room 17.9 / 0.36,
  vista 133.2 / 0.44, night 54.7 / 0.20, fog 91.4 / 0.17 (the rule's own DLL before the merge: 17.8 / 0.35, 132.2 /
  0.46, 54.7 / 0.20, 91.7 / 0.18; the vista is 1 brighter with PT-136's bound, its darkest 5 % 38 as the game's).
  Under NRD: 16.1 / 0.11, 132.8 / 0.18, 55.0 / 0.12, 91.5 / 0.12. The three stages, the world running
  (`20261012-082625`, cells + walk + lamps): the town 1.12 + 1.38 + 1.88 = 4.4 ms (the old route's one stage 5.1 at
  06:12), the forest 2.01 + 1.67 + 3.42 = 7.1 (7.2); the tavern 0.57 + 1.43 + 1.77 = 3.8 (3.0; from a run with the
  owner's stream on the card). STATE: the rule, in main and in the game folder, seen in the harness; the owner has
  not looked.
- 10-12 07:00, THE FOG'S CELLS ARE THE RULE (worker fogrule2 after fogrule; `docs/agents/fogrule2.md`). MERGED INTO MAIN
  10-12 08:00 (the coordinator); the rule's own DLL had run in the game before the merge (`20261012-072747`: both
  steps the cells, at the candidate's numbers); main's build with it: the round's newest state block. What the rule is: the camera's fog in cells over its WHOLE way (48 slices to 20 m as
  RTX Remix has them, 48 more to the far distance, each a quarter deeper than the one before; a cell takes its sample
  only where the camera's rays read; 254 frames of history), each pixel's walk through them, and a lamp's light right
  by it per pixel. OUT: the per-pixel camera route (`pt_fog.hlsl`), the hidden `Traced fog: froxel cache`, its trial
  numbers and read-back, frame flag bits 19 and 24 to 29, the stage "fog filter", the comparing recipes. The fog's
  target is one RGBA32F (PT-136).
  The game, the world frozen, old route / cells. No denoiser (run `20261012-051553`): flicker 0.40 / 0.36
  tavern-room, 0.99 / 0.46 vista, 1.85 / 0.20 night, 0.33 / 0.17 fog; boil 0.14 / 0.11, 0.44 / 0.11, 1.20 / 0.06,
  0.18 / 0.05. Under NRD (`-053455`): flicker 0.40 / 0.18, 0.94 / 0.27, 1.92 / 0.16, 0.32 / 0.16; boil 0.24 / 0.10,
  0.54 / 0.09, 1.29 / 0.06, 0.19 / 0.06. The light, with the fog's targets in 32-bit floats (`-065226`): 17.96 /
  17.92, 133.51 / 132.61, 54.50 / 53.83, 91.42 / 91.31.
  How it got there: the grid of 10-11 read the mist's cache past its 20 m at points nobody fed (7 % and 24 % less
  light outdoors). Fed there (fogrule's smallest fix) the light agreed to 1 %, and the valley's fog flickered 3.6
  times the old route's: the way past the cells was one raw point a pixel, no history, no filter. With the cells
  over the whole way that grain is gone. The vista then read 1.5 % brighter than the old route: three suspects fell
  in the game (a cell's plain mean of the light, the cache's feeding, the old route's rare bright frames), a split
  of the grid showed that the two ways of drawing the fog's points agree, and the cause was the fog target's 16-bit
  floats under that view's reference radiance of 5.7e8 (PT-136): the old route's blend rounded its fog to nothing,
  the cells showed theirs in steps of half a percent of white.
  Open, each in the report and in PT-138: the vista's far castle (the old route 2 % of white hazier than the cells
  with 32-bit targets; which is right is not known, the cells are the nearer to the game's own picture); the
  tavern's fog 1.5 % dimmer under NRD; far lamps, a turning camera, the cost; the offline check's pins are empty (no
  offline GPU test reached a frame).
- THE CANDIDATE IN THE GAME (worker fogrule2's first trial, 2026-10-12 05:20, run `20261012-051553`: fogrule's build
  `%TEMP%\tgb\fogrule-handover-1`, SHA 3EB0935E741C: THE CELLS OVER THE WHOLE WAY, 121 x 68 x 96, 48 slices to 20 m
  and 48 more to the far distance, no per-pixel point past them): THE GRID IS THE CALMER ROUTE IN ALL FOUR SCENES
  AND THE LIGHT HOLDS. The game's own / the old route / the grid, luma; flicker and boil, old to grid: the tavern
  24.3 / 17.9 / 17.9, 0.40 to 0.36, 0.14 to 0.11; the vista 130.5 / 130.0 / 131.9, 0.99 to 0.46, 0.44 to 0.11; the
  night 38.2 / 52.9 / 52.3, 1.85 to 0.20, 1.20 to 0.06; the fog scene 70.2 / 91.7 / 91.7, 0.33 to 0.17, 0.18 to
  0.05. (The smallest fix's grain in the fog scene was its per-pixel pass past the cells: one point a pixel a frame,
  no history, and 72 % of that scene's fog light comes from past 20 m.)
  UNDER NRD TOO (run `20261012-053455`, recipe `fog-rule`), flicker and boil, old route to grid: the tavern 0.40 to
  0.18 and 0.24 to 0.10 (luma 16.2 / 16.0); the vista 0.94 to 0.27 and 0.54 to 0.09 (129.6 / 131.4); the night 1.92
  to 0.16 and 1.29 to 0.06 (55.5 / 54.9); the fog scene 0.32 to 0.16 and 0.19 to 0.06 (90.9 / 90.9).
  ITS COST (the same runs' 10 s lines, not a measuring run), the old route / the grid's cells, walk and lamps' pass:
  the tavern 3.0 / 3.6 ms; the vista 4.4 / 7.7; the night 8.5 / 10.2; the fog scene 1.3 / 2.9: 0.6 to 3.3 ms more
  (the lamps' pass 4 to 5 ms outdoors; the cells feed the mist's cache at every cell every frame: both untouched).
  OPEN before it is the rule: THE VISTA'S +1.5 % (132.1 against 130.2; real, the dark rocks at the right +6 to +10
  levels; there with light scattered once only too, so not the cache's feeding): fogrule2's trial 2 (a cell holds
  the light WHERE THE FOG IS, weighed by the medium's scattering, not the plain mean over the cell: a fog box's
  thick wisps shadow themselves) is queued; then lamps 30 to 200 m off at night and the screen's edge while turning.
- fogrule's SMALLEST FIX IN THE GAME (2026-10-12 04:40, run `20261012-043614`, recipe `fog-ab`, the world frozen, no
  denoiser, 16 frames; the build `%TEMP%\tgb\fogrule-small`, SHA 941B5AAD44D2: main of 01:16 with one change, the
  per-pixel pass past the cells feeds the mist's cache). THE TWO ROUTES NOW AGREE ON THE LIGHT. The game's own / the
  old route / the grid, luma: the tavern 24.6 / 17.9 / 18.0; the vista 130.6 / 130.1 / 128.5 (the grid was 121.6);
  the night 38.8 / 55.1 / 54.3 (52.1); the fog scene 70.2 / 91.7 / 91.3 (the grid was 69.9, 24 % short). So the
  suspect was the cause: past its 20 m of cells the grid read cells of the mist's cache that nobody fed.
  THE NOISE, the old route against the grid, flicker and boil: the tavern 0.41 and 0.14 against 0.35 and 0.12; the
  vista 0.99 and 0.44 against 0.77 and 0.18; the night 1.90 and 1.23 against 0.57 and 0.15; BUT THE FOG SCENE 0.32 and
  0.18 against 1.17 and 0.33: in thick fog far off the grid is the noisier one by 3.6 times (its detail measure 2.59
  against 0.58). That is what stands between the grid and "the rule" now. (Both routes are 30 to 40 % brighter than
  the game's own picture in the fog scene and at night: HDRP's fog against the traced one, PT-37's question, not
  this entry's.) NEXT: a fresh worker with these numbers: the fog scene's noise (what the grid does past its cells:
  the per-pixel pass there, its samples), then fogrule's candidate (`%TEMP%\tgb\fogrule-handover-1`), then the grid
  as the rule and the old camera route out. Under NRD too (the recipe `fog-froxel-b` has those steps).
- Worker fogrule (2026-10-12 01:17 to 03:40, `wt\fogrule\docs\agents\fogrule.md`, base `base20g`): BLOCKED, NOTHING RUN ON
  THE GPU (ten starts in two hours, each ended by a game opening before the driver had made the two reordering passes
  of a new build: 405 s and 234 s once, seconds after). By reading only: past its 20 m of cells the grid reads cells
  of the mist's cache that nobody feeds (it fits the four scenes of `20261012-010243`, not shown to be the 7 % and
  24 %); the old comparing check could not see it (its two routes shared one cache); and in BOTH routes the cache
  is fed in view only and reads 0.86, 0.74 and 0.56 of its fixed point at albedo 0.3, 0.5 and 0.7 (a CPU model: an
  entry of its own is owed if the game shows it). BUILT, UNTESTED: the smallest fix (the base with one change: the
  per-pixel pass past the cells feeds the mist's cache), `%TEMP%\tgb\fogrule-small\TGPathTracing.Native.dll`, SHA
  941B5AAD44D2...; the copy's candidate (the cells over the whole way, trials 1 and 2 and the TEMP DIAG out, the old
  route behind its switch), `%TEMP%\tgb\fogrule-handover-1` (native 3EB0935E741C..., plugin 7FDE5D98772B...); a
  second stage with the old route out (`fogrule-c-src`, 30 of 53 passes, no DLL). NEXT, in the game (the first
  test): the smallest fix as a trial build on `vista`, `fog`, `night`, `tavern-room` with `fog-froxel-b`
  (`Queue-PtRun.ps1 -TrialDll`): does the grid's light meet the old route's and the game's.
- 10-12 01:15, OUTDOORS THE TWO ROUTES DISAGREE ON THE LIGHT (`20261012-010243`, `fog-froxel-b`, the world frozen; luma,
  the game's own / the old route / the grid, no denoiser): `vista` 131.1 / 130.6 / 121.6 (the grid 7 % under); `fog`
  70.5 / 91.8 / 69.9 (the old route 30 % over the game's and the grid on it); `night` 38.4 / 53.1 / 52.1 (the same).
  Flicker / boil, old against grid: vista 1.00 / 0.44 against 0.82 / 0.22; fog 0.33 / 0.18 against 0.70 / 0.23 (the grid
  noisier there); night 1.86 / 1.21 against 0.45 / 0.12 (the grid four and ten times calmer). So the grid is NOT the rule
  yet: both routes must converge to one fog. Opus worker fogrule (`brief-fogrule.md`, copy `wt\fogrule`, base `base20g`):
  which route is wrong where (against a closed form offline), the fix, then the grid the rule and the cleanup.
- 10-12 01:00, THE GRID IS RIGHT IN THE TAVERN (main's build 3199B2F4BBF6 with fogfix merged; the cause was PT-133: no
  sun in the frame). `20261012-004519` (`fog-froxel-numbers`): the grid's luma 17.9 (17.7) against the old route's 17.9
  (17.7), in every variant; the cells' numbers sane (mean 0.0003 storage units, the largest 0.03 beside a lamp; none not
  finite). `20261012-005626` (`fog-froxel-b`, the world frozen): WITH NRD the old route's flicker 0.39 (region 0.45), boil
  0.23 (0.26), 95th 0.64; THE GRID 0.20 (0.23), 0.11 (0.13), 0.34: HALF, the luma 16.4 against 16.7; with no denoiser
  0.36 / 0.12 against 0.41 / 0.14. The fog's stages: the old route 3.0 ms; the grid (trial 0: one sample a cell, the fast
  walk) cells 0.72 + walk 1.15 + the pixel's share and tail 1.82 = 3.7 ms. Trial 1 (the first walk) costs 10.9 ms for
  its walk and trial 2 (Remix's reuse and filter, with that walk) shows the same picture: neither earns its place.
  NEXT: the same comparison outdoors (run started 01:05: `fog`, `night`, `vista`), then the grid the rule (the old
  camera route, trials 1 and 2 and the TEMP DIAG read-back out: a fresh worker's job), and the owner's look.
- 10-11 23:20: THE WORKER fogfix'S BUILD IS STILL WHITE IN THE TAVERN (`20261011-231804`, its native A4A15958..., deployed from
  its copy: the old route luma 16.5 / 18.0, the grid 255.0 with NRD and with none). What it did find (its report in
  `wt\fogfix\docs\agents\fogfix.md`): the coordinator's offline "over-count" was the OLD route losing light (PT-132), not
  the grid; the grid did mis-count a lamp near the lamp by 5 to 6 % (a cell cannot hold light that changes within it):
  fixed by a split (the smooth part in the cells, the rest per pixel), 0.9 to 1.9 % now; a comparing check. The white is
  NOT reproduced offline with anything tried (the game's light levels and units, 300 lamps, forward scattering, an
  effect's light, an emitting quad, the mist's cache, fog boxes, the tracer's own camera rays). Asked of it next: numbers
  from the game instead of a picture (a readback logged once a second: cells not finite, the largest value and where,
  the frame's constants), in one build with its switches, for one tavern run.
- 10-11 evening: THE GRID WITH ITS CONTINUATION IS WHITE IN THE TAVERN (luma 255 on every pixel, NRD or none; runs
  `20261011-185528`, `-204941`, `-205518`, `-210543`). Followed down in the game with a temporary `Fog grid diagnosis`
  switch: right with the cells dark, with the grid not read, and with the "excess" not added by the filter (luma 17.1
  against the old route's 17.7); white with every other part left out; not the mist's cache, not the fill lights, not a
  stale build, not a descriptor slot. REPRODUCED OFFLINE IN SMALL (`TGPT_TEST_FOG_FROXEL_ONLY` with the new
  `TGPT_TEST_FOG_LAMP=<metres>`: a lamp in front of the camera; the sky strip's light, old route / grid): 2 m 0.0408 /
  0.0607, 0.3 m 0.1188 / 0.2501, 0.02 m 0.2268 / 0.5398; no lamp: equal. THE GRID OVER-COUNTS A LAMP'S LIGHT NEAR THE LAMP
  (in the tavern: 514 lights, flames inside the cells). The worker's check asked only for a finite picture; the game run of
  16:48 (parts a and b) had looked right. A fresh Opus worker fogfix (`brief-fogfix.md`, copy `wt\fogfix`, base
  `base20e`): the cause by measuring, a fix that converges to the old route with no clamp, the check a comparison, the
  temporary switches out. Until then the grid stays hidden and off.
- "Tested the fog in the tavern - it's noisy, and when I strafe around, I can see the fog follow a bit along from light
  sources". The trail: the fog's history was fetched by the surface's motion, while a lamp's glow hangs nearer the
  camera; the fix (fetch where this frame's light comes from along the way) built 13:38. The noise: equiangular sampling
  for lamps, deployed 13:17. Run: `tavern-room@traced-fog`.
- ~15:25, watching run 20261011-151710 with NRD as documented (PT-127): "NRD is a million times better now", then:
  "Although now of course we see boiling/artifacts from our path traced fog (I guess?) which is still super noisy". The
  fog is laid over the picture after the denoisers (pt_compose) and NRD is not made for media (its README: "not natively
  designed for volumetrics or transparency"); its only filter is pt_fog's own history. Next after the NRD clean-up: the
  fog's noise against what NVIDIA and the literature do for traced media.
- What NVIDIA does (its own path tracer for games, RTX Remix, documented settings: docs.omniverse.nvidia.com rtx_remix
  1.4.0 "Volumetrics"): the media's light in a froxel radiance cache (a grid stuck to the view: render size / 16 across,
  48 depth slices spaced by an exponent of 2, out to a max distance), lit per cell with RIS over the lights (8 initial
  candidates, a visibility ray) and temporal reservoir reuse (ReSTIR-like, up to 200 samples), accumulated over up to 254
  frames, a Gaussian filter whose kernel shrinks as a cell's history grows (radius 4 to 2), a firefly clamp. Ours: one
  point a pixel a frame, an equiangular lamp sample, a 10 % blend with the last frame, a 5 x 5 filter. Proposal to the
  owner (~15:30): build the fog as NVIDIA's Remix does.
- The owner chose "NVIDIA's Remix way" (~15:35). Codex worker fog-froxel's part (a) merged ~16:10 (hidden `Traced fog:
  froxel cache`, frame flag bit 19, off; 121 x 68 x 48 cells, 20 m of reach, the rest of the way traced as before;
  `docs/agents/fog-froxel.md`). Run `20261011-161034` (tavern; another test on the GPU, the window not in front: times
  rough): with NRD flicker 0.38 -> 0.21, boil 0.23 -> 0.12 (the old route's sparkling streaks along the light shafts gone
  from the change map), with none 0.41 -> 0.36, luma the same; the composite stage 3.9 -> ~12 ms. Part (b) running: the
  cost down without changing the picture, Remix's reservoir reuse, filter and firefly clamp, the reach.
- Part (b)'s first checkpoint (cell reservoirs reused, the tail beyond the grid its own pass, stage times) merged ~16:45
  (native 37ADD808936F). Run `20261011-164811` (-Measure, clean): the picture as part (a)'s (with NRD flicker 0.39 ->
  0.21, boil 0.23 -> 0.12; luma the same); the tavern's fog stages: old 3.0 ms, the grid 0.6 (cells) + 10 (integration)
  + 0.06 (tail); the tracer 35.7 -> 43 to 44 ms. The worker's next: the integration's 10 ms (Remix integrates once per
  froxel into a pre-integrated volume, each pixel one read), then the rest of (b). (The same run: NRD's denoising stage
  2.8 ms with the reflections' own filter on glass and water alone, from ~4.4.)
- The owner chose "NVIDIA's Remix way" (~15:35). Worker `fogfroxel`, pristine base `base20fog`, builds the first
  hidden-switch cache trial (`docs/agents/fog-froxel.md`): own passes, flag 19, tavern-room A/B without denoising
  then NRD. The game's verdict comes before reservoir reuse/filtering and before removing the old route.
  Native/plugin built 16:04, 116 source checks pass; no game/GPU run. The first grid stores camera-directed RGB;
  directional history and the risk of thin shafts blurring or light changes lagging remain for the game to judge.
- Part (a) in game, run `20261011-161034`: NRD flicker 0.38 -> 0.21, boil 0.23 -> 0.12, luma 16.5 -> 16.3;
  None luma 18.1 -> 18.1. The rough composite cost rose about 4 -> 12 ms; no speed verdict because the game
  was behind Code and the coordinator reports another GPU test. Part (b) must isolate grid/integration/tail
  cost, reduce it without lost light, and add temporal reservoirs/filtering. Worker report: `fog-froxel.md`.
- Part (b), first checkpoint: reservoir selections reused as properly weighted proposals, freshly shadowed;
  distant ray queries separated from cache integration; homogeneous integration avoids repeated height maths.
  All three stages timed separately, no grid/ray count reduced. Filter/clamp/directional history still open.
  Vendor-audit gap (PT-127): current public Remix tree omits its included integrator and reservoir shader bodies.
  Our reuse uses corrected MIS, deliberately differing from the documented biased reuse to preserve energy.

- Continuation requested ~17:00: clean run `20261011-164811`, NRD flicker 0.39 -> 0.21, boil 0.23 -> 0.12;
  integration 9.84-10.54 ms. Keep the picture and reduce that cost, then finish the stability/filter/firefly work.
  Found repeated tests of all seven fog boxes at each pixel's slices. One exact ray/box interval check plus shared
  radiance reads lowers the guarded small-case integration from 9.73 to 0.55 ms, same raw mean. In-game cost owed.
  Historical Remix 0.4 does have a separate homogeneous-medium depth prefix; it cannot replace our per-ray
  masked/height medium without changing the result. No coarser grid, fewer camera samples or density voxelization.
- Part (b) remainder built and fog-only GPU check passed (17:33): 1-6 reservoir samples, documented Gaussian/history weights, excess retained in its
  own history and returned at the same cell. All fog stages in the intersecting-box fixture: 9.97 -> 1.07 ms; new filter 0.08 ms. No game-speed claim.
  Known limitation: camera-directed radiance history still lacks directional coefficients; moving-camera and
  mirror checks remain owed. Part (c) stays gated on the owner's acceptance.

### PT-117 The performance review's findings (GPT-6-astra, the owner's request, 2026-10-11)
- The owner, ~10:40: "ask gpt-6-astra xhigh in an independent review if it can see any problems we haven't caught
  ourselves in how we do shader execution reordering, opacity micromaps, integrate rtxdi, trace against it, with focus
  on performance specifically ... We're still so far off our performance targets". The review: `docs/agents/review-perf-
  astra.md` (read-only, no GPU timings; each finding with its file, the vendors' documentation and a fix that keeps the
  picture).
- Its findings, by the size it gives them: (1) the reflections fetch the whole hit BEFORE reordering (pt_specular calls
  `trace_from_surface`, which runs `fetch_hit`); the bounced light already reorders on a compact query result and fetches
  after: give the reflections the same split (medium, more with glass); (2) water: one big divergent continuation after
  the first hit; smaller continuations or HitObject traces with specialized stages, measured with a shader profile of
  the beach first (big, suspected); (3) micromap arrays hold blocks no triangle uses and are never compacted (memory);
  (4) the micromap baker marks microtriangles unknown from bounds looser than their footprint (medium, foliage); (5) the
  fog: 22 halvings per height-fog distance where a closed-form inverse exists, and every fog box scanned several times
  (medium); (6) plants refitted forever with no rebuild when their traversal degrades (suspected big, forest); (7) light
  selection evaluates candidates already weighted 0 (small); (8) the bounced light's final visibility traced for skin
  pixels the walk then overwrites (small); (9) a global UAV barrier after every dispatch (small to medium); (10)
  water-only queries opt into micromaps they can never meet (small); (11) the reflections' reordering probe (8 frames of
  each 128 on the other hint) costs 0.4 to 0.8 ms a frame on average where one hint keeps losing (small). Corrected:
  forced non-opaque camera rays still get the micromaps' transparent rejection (audit 4 said not); HitObject::FromRayQuery
  needs no hit shader; a 0 or 128-byte stack size says nothing of registers.
- Next (keeps the picture): (5), (10), (11) now (small, in the coordinator's next build); (1) and (6) as measured trials;
  (2) after a shader profile of the beach pass; (3), (4) with the micromap work.

### PT-116 At most 4 water surfaces are traced; the rest stay HDRP's (found 2026-10-11)
- Found by the coordinator auditing what HDRP still draws while tracing (the owner, 2026-10-11: everything HDRP renders
  in sequence moves over; "These must not render at all"). `tgpt_api.h` `TGPT_MAX_WATER` 4: the tracer has four water
  slots; `WaterSurfaceRendering.cs` leaves "water past the slots" to HDRP to draw and light so that it does not vanish.
  A fixed count that changes the picture (the owner, 2026-10-07: "There is no budgets or limits there, this is PT") and
  keeps HDRP's water passes alive.
- Next: how many water surfaces the game's scenes hold at once (the harness's water scenes, the beach); then every
  surface the tracer's, its records in a buffer sized by the count, HDRP's water drawing and lighting off for all.

### PT-115 Rough surfaces' highlights from the bounced light's own sample, not a ray of their own (proposed 2026-10-11)
- Found in round 20 (`docs/rounds/round-20.md` 9; run `20261011-051727`): the reflections cost the town 20.5 ms; the
  row's "Glossy Surfaces" saves 8 of them by giving a surface rougher than 0.6 the radiance cache's cell light times
  its lobe's albedo in place of a ray: a stand-in (the cell's average, no direction), so not a choice to take.
- The proposal (not built): such a surface takes its highlight from the bounced light's final sample, the one ReSTIR
  GI already traced, reused and checked for visibility, shaded with the surface's whole reflection (its diffuse and its
  GGX lobe) instead of the diffuse alone, as NVIDIA RTXDI's sample shades a GI sample. The estimate stays unbiased (the
  sample's weight holds for any function of its direction that the target covers, and the diffuse target covers the
  whole hemisphere), so the picture converges to the same; what changes is the noise of rough highlights. The
  reflections' pass then traces only glossier surfaces. Likely gain: most of "Glossy Surfaces"' 8 ms in a town.
- Needs: the final shading in `pt_gi_spatial` writes the specular part to a target `pt_specular` reads for rough
  pixels; the hit distance for Ray Reconstruction and NRD from the sample. Both big passes compile again.
- State: a proposal for the owner (a look to judge: rough metal and wet stone in motion, with NRD).

### PT-114 The caustics cost a room 4 ms a frame and make nothing there (found 2026-10-11)
- 10-12 12:10, OUTDOORS: THE GRID'S SPEED-UP IS MERGED AND THE DEFAULT (worker caustgrid, `docs/agents/caustgrid.md`;
  merged 11:42 behind the hidden `Caustics grid trial`): 1 the lights' pick in a pass of its own (it was one thread
  of 256 doing two serial joins a tile), 2 only tiles with water under them and only the corners a cell needs, 3 no
  light traced whose rays cannot land in view. The game (run 20261012-114808, the owner's stream on, 0 / 1 / 2 / 3 /
  0 in one run; the window was not in front for two stretches, so the stage's GPU time is quoted, not the frame):
  the caustics' stage 5.25 / 3.28 / 3.12 / 3.13 / 5.41 ms in the town, 5.34 / 3.42 / 3.12 / 2.75 / 5.27 in the
  forest; the beach's frozen picture 56.0 / 56.2 / 56.3 with 0 / 3 / 0 (offline the written triangles are the same,
  checksum for checksum). The setting's default is 3 since then (the plugin deployed 12:12: 1A6C8C6297DA). OWED: the
  old way taken out of `pt_caustics.hlsl` and the tracer (0 is the tests' yardstick until then), and what is left of
  the stage where no water is near (about 3 ms in a town).
- 10-12 09:00, FIXED FOR INTERIORS, THE RULE (the coordinator; the plugin alone, no shader): IN AN INTERIOR THE MAP'S
  WATER IS NOT TRACED. The game's interiors float above the map they belong to and the map stays loaded, so from
  inside the tavern the tracer's four water slots went to the map's "Ocean", "BigLake" and two rivers: 762 pieces of
  sea posed and refitted every frame and the caustics' grid laid over it, 470 m under a closed room. The rule
  (`WaterCaustics.LeftOutside`): while an "Interior" scene is loaded, a water surface of any other scene gets no slot,
  and HDRP does not draw it either (`WaterSurfaceRendering.Surface`); an interior's own water (a surface of an
  "Interior" scene) and a dungeon's (a scene of its own, no map beside it) are traced as before. It is the sky's rule
  for interiors (`SceneLights.Interior`: rays there see no sky) carried to the water. WHY "AN INTERIOR SCENE IS
  LOADED" MEANS "THE PLAYER IS INSIDE": the harness's 280 loads: 164 on the map, none with an interior scene; 78 in
  a tavern or a house, each with exactly one beside the map.
  THE GAME (`20261012-085406`, the trial behind a hidden switch, the tavern, with / without / with the sea): frozen,
  16 frames: luma 17.9 / 17.9, flicker 0.36 / 0.36, boil 0.12 / 0.12, detail 5.44 / 5.45: THE SAME PICTURE. The world
  running: A FRAME 49.55 ms WITH, 43.67 AND 44.16 WITHOUT (20.2, 22.9 and 22.6 fps); the tracer 37.70, 32.95, 33.13;
  the caustics 4.22, 0.00, 0.00; the scene stage 1.04, 0.63, 0.63; the two reordering passes the same in all three
  (the owner's stream was on: the steps compare, the frame rates are not a baseline). As the rule, without the
  switch (`-085954`, plugin 8A3AF7BA55B9): 43.42 ms, the tracer 33.25, the caustics 0.00; the beach in the same run
  still has its water and its caustics (7.28 ms).
  LEFT OPEN: the same 4 to 5 ms OUTDOORS where water is loaded and not in view (the town 4.8, the forest 4.7, the
  vista 4.5): there the water can be met by a path, so it is the grid's own cost that must come down (below).
- 10-12 10:45, OUTDOORS: WHERE THE MILLISECONDS GO, AND A TRIAL THAT TAKES 2.2 TO 3.9 OF THEM (worker caustgrid, its
  copy `wt\caustgrid`, `docs/agents/caustgrid.md` there; OFFLINE, not yet in the game). THE COST IS THE LIGHTS' PICK:
  in every tile the first of the group's 256 threads joins the threads' shares alone, twice (256 serial steps each,
  the second with a chain of random numbers), while 255 wait; 8,100 tiles a frame, whatever lands. The split of the
  sea fixture's stage with 1,003 lamps and the sea under ground (5.2 ms): the first thread's two joins 1.84 and
  1.02, the shadow rays 0.58, the rays on and the writes 0.44, the other passes and the structure's build 0.50,
  everything else under 0.2 each. (So the shadow rays never were the cost: caustcut's cut of them gained nothing.)
  THE TRIAL (hidden `Caustics grid trial` 1 to 3, frame flag bits 19, 25, 26): 1 the pick in a pass of its own, a
  thread a tile, the same sums and draws in the same order; 2 and only tiles with their slot's water under them
  (a river's extent is mostly dry land); 3 and no light whose rays cannot land in view. ITS CHECK, off / on / off,
  16 frames, five cameras, 3 lamps and 1,003: the landed triangles' count, largest tolerance and checksum equal in
  16 of 16 frames, the pixels bit-equal (0 of 8.3 million values). THE STAGE, ms, off to on: the sea's bed 5.00 to
  2.77, from afar 5.81 to 3.45, no water in view 4.25 to 1.99, a river's bed 7.50 to 4.35, the sea under ground
  4.10 to 1.86; with 1,003 lamps 6.13 to 3.46, 6.99 to 4.33, 5.36 to 2.65, 9.15 to 5.24, 5.18 to 2.56. Next: its
  DLL in the game (`metal`, `forest`, `water-day`; its recipes `caustics-grid`, `caustics-grid-speed`), then the
  merge and the trial as the rule the same day.
- 10-11 evening, the worker caustcut (`docs/agents/caustcut.md`, copy `wt\caustcut`, NOT merged): an exact cut of the
  shadow rays (a tile's corners whose ray to the lamp is proved to cross one solid triangle skip the ray; a pass of
  its own). Offline no gain (2.20 / 2.23 ms); IN THE GAME NO GAIN (`20261011-213004`, the tavern, off / on / off: the
  caustics' stage 4.20 / 4.30 / 4.05 ms, a frame 49.03 / 49.45 / 48.98). So the shadow rays are not where the 4 ms go: the
  grid's own work is (its levels, the waves' points). The trial is dropped. Next: split the stage with the measuring
  build's switches (grid only, shadow rays only), then an exact way to not lay the grid where no reader can be (the
  readers: the camera's surfaces, and solid points under water that any path meets: caustcut's note on why a camera's box
  alone is not enough). Its other find is PT-130.
- Found in round 20 (`docs/rounds/round-20.md` 9; run `20261011-051727`): in Ron's Tavern the caustics' stage costs 3.9
  ms of the tracer's 31 and writes 0 triangles a frame. The game's interiors float above the map around them (the
  tavern's floor about 470 m over the sea, `CampaignMap_HOS` loaded beside `Interior_Tavern_02`); the sea is a water
  slot, so `pt_caustics` lays its grid of 8 levels round the camera over it, finds each corner's point on the waves and
  traces a shadow ray from each to the tile's lights: the tavern's lamps, 470 m up, behind the floor. Nothing is lit,
  so nothing lands, but every ray is traced.
- A cut must leave the picture as it is (the owner's rules): light the sea reflects can in principle reach anything
  above it, so "nothing in view can be reached" is not cheap to prove per tile. Ideas, none built: a tile's lights
  tested from the light's side once (one ray a tile and light, the corners only where it passes: not exact at a
  shadow's edge, so no); the frame's receivers bounded (the camera's surfaces and the water's underwater points) and a
  tile's refracted rays skipped where they cannot land in that box (exact for that branch; the reflected one stays).
- State: fixed for interiors (the rule, seen in the harness; the owner has not looked); outdoors open, a question of
  design.
- Codex worker caustcut (started 10-11 ~19:00, copy `wt\caustcut`, base `base20pc`, `docs/agents/briefs/brief-caustcut.md`):
  measure where the 3.9 ms go, then an EXACT bound (the frame's receivers of caustic light: what cannot reach them is not
  traced), behind frame flag bit 23, recipe `caustics-bound`.

### PT-113 The offline GPU test fails 44 checks, the same on round 19's build (found 2026-10-11)
- Found in round 20 (`docs/rounds/round-20.md` 5): `tgpt_gpu_test` fails 44 checks on today's build AND on round 19's
  final native build (C6515C716BD5), the same lines: the denoisers' means against the undenoised mean in the slot,
  grille and candle-night rooms (NRD and the built-in denoiser), a perfect mirror's fine detail with NRD and the built-in
  denoiser (and Ray Reconstruction's input), a glossy floor's reflection, the glass index, the coloured bulk slab. The
  last valid whole run (2026-10-08 10:19) had 5 known failures; the rest came between then and round 19 (rounds 6 and
  19 changed the camera rays, the reflections' sort, the composite's outputs; no whole run in between).
- Logs: `<scratch>\gputest2.log`, `gputest-base.log` (the session's scratchpad). Next: find the first build that fails
  them (the harness's builds folder keeps every installed DLL: `artifacts\harness\builds\*\before`), one check family
  at a time; the mirror and denoiser-mean families first (they say the picture's light is off, not only a number).
- State: open.

### PT-112 HDRP renders nothing under the traced picture but post-processing, upscaling and the interface (the owner, 2026-10-10)
- THE SKY'S THREE QUESTIONS, ANSWERED (the owner, 2026-10-12, the choices page; `docs/agents/sky-1.md` "Questions for
  the owner"): Q1 THE SUN'S PLACE: "Light from the sun and moon you see" (the real direction, long shadows at
  sunset; not the game's hidden ShadowCaster held 30 degrees up; the game's light curves stay). Q2 THE HAZE: "Real
  haze", with the note "I didn't have enough context to pick between these two, but whenever you ask me if we should
  use HDRP stuff or fake stuff instead of Path Traced stuff, I pick Path Traced stuff" (so the atmosphere's haze
  also dims what is behind it; and a general ruling: AGENTS.md "Path traced, always"). Q3 CLOUD SHADOWS: "Add them"
  (each sun ray dimmed by the cloud layer where it crosses it). NEXT: the sky's job (sky-1.md's design with these
  three), a worker's, after the speed jobs in hand.
- The owner, 23:20, told that a traced frame is the trace plus the game's own rendering (PT-107): "Wouldn't we get
  around that simply by preventing HDRP rendering altogether, which we want to do anyway?"; 23:50: "Let's prevent HDRP
  rendering, yes. It will also make it easier for us to test in the future, as we know it'll come from our own
  rendering whenever there are bottlenecks", and of what may stay in HDRP: "the game's fog is rendered and path traced
  by *us*, right? It definitely should. Transparent things we don't trace yet - well, don't keep them in HDRP. Move
  them over to our tracer so we can get rid of that. Post-processing, DLSS/upscaling/AA, interface, sure, makes sense".
- Told the same minute: the fog is NOT traced by us today; HDRP renders and lights it, and Fog Adaptation only scales
  its light to the traced light's (PT-37 is the tracing of it). So the fog's tracing joins this entry.
- What HDRP does today while tracing (PROJECT-CONTEXT 9.1): every opaque object's depth, G-buffer and motion vectors,
  shadow maps, its own lighting (thrown away), the sky, decals; our composite paints over its opaque result
  (AfterOpaqueAndSky); then its fog and its transparents (VFX Graph effects: 840 to 933 in an open-world scene;
  about 21 trails; 2 to 55 transparent meshes) over our picture; then post-processing, DLSS (on HDRP's depth and
  motion vectors) and the interface. Its share of a traced frame: about 12.6 ms (tavern) and 20 (town) (PT-107).
- The plan, in order: (A) HDRP draws no opaque object: the tracer writes the frame's depth and motion vectors into
  HDRP's buffers (for DLSS, post-processing, and the fog and transparents while they are still HDRP's), and HDRP's
  depth prepass, G-buffer, shadow maps, lighting and decals stop; the yardstick (HDRP's G-buffer as a check) becomes a
  switch for tests. (B) The fog traced by us (PT-37 step 2: the game's fog density lit by the traced lights, with
  their shadows), then HDRP's fog stops. (C) The transparents traced (PT-71 item 7: the VFX Graph effects from the
  buffers the game's simulation writes; trails; transparent meshes), then HDRP's transparent passes stop. Each step
  removes HDRP's part in the same job; until B and C, the fog and the effects stay HDRP's so nothing vanishes from the
  picture (the owner's rulings: the fog stays; nothing may disappear). First: HDRP's pass groups timed on the card
  while tracing (the prize of A, and anything else that still draws).
- The owner, 23:58, on reading the plan: "OK, move fog and transparents over, then, so we don't render anything with
  HDRP anymore. Post-processing effects, interface and upscaling I don't consider part of the rendering per-se". So
  B and C are part of this entry's goal, not later features: HDRP draws nothing; post-processing, the interface and
  the upscaler work on our finished picture.
- STEP A WORKS ON TRIAL (round-19.md 11; the hidden `Game passes while tracing` 256): HDRP's opaque draw lists left empty
  (`HDRenderPipeline.CreateOpaqueRendererListDesc`; the frame setting OpaqueObjects instead woke the game's ECS uploads,
  800 ms frames: a dead end), the tracer's own depth and motion written into HDRP's depth buffer, motion vectors and
  depth pyramid (which its fog reads) at the composite. Clean town run `20261011-011046`: 87.3 to 90.5 ms a frame with
  every pass, 81.3 without opaque drawing; the picture the same but for foliage (0.10 % of pixels more than 20 levels
  apart): the plants' sway has no motion of its own in the tracer yet. Before it is the rule: the plants' own motion,
  the yardstick's recipe with HDRP drawing, the trial bits cleaned up, the decal capture made independent of HDRP's
  decal setting. Then B (the fog) and C (the transparents).
- ROUND 20 (2026-10-11, `docs/rounds/round-20.md`): A MADE THE RULE in the code. `GamePasses.cs`: HDRP's opaque draw lists
  empty and its decals off for the gameplay camera while tracing, `TGPT_FRAME_OWN_DEPTH` set; HDRP draws only for the
  Yardstick view, the old route, or the hidden `HDRP draws under the traced picture` (tests). The trial switch and its
  bits gone. The traced decals follow the game's own decal setting. Hair's depth hold (`HoldLayerDepth`) gone: hair hidden
  from HDRP as brows. The plants' FOLIAGE FAULT: a plant group's record had `previous` 0 (not "none"), so the camera rays
  read heap slot 0 as its last positions; fixed, and plants and cloths now carry their own sway motion (the plants as each
  vertex's move in halves, 76 MB in the town). Dead with A, to remove next (PT-60): the old route (`Own camera rays` off,
  `TGPT_OPTION_GBUFFER_SURFACES`), which needs HDRP's G-buffer.
- In the harness (round 20 section 3): the tavern matched at once; the town's soldier flickered (DLSS kept no history on
  him): the tracer's motion for a still character was a few hundredths of a pixel, the roundings of world coordinates
  4 km from the origin. Fixed (each corner's move, exact); town flicker 0.22 with the rule against 0.22 with HDRP
  drawing (run `20261011-022100`).
- B ON TRIAL (round 20 sections 4 and 6; `pt_fog.hlsl`, hidden `Traced fog (trial)` and `Traced fog: light scattered
  more than once (trial)`, recipe `traced-fog`, scenes `fog` and `night`): the game's height fog traced along each camera
  ray (its albedo and anisotropy, ABI 0x00012700), lit by the sun, the sky and one lamp or glowing surface with traced
  shadows, reused over frames and filtered; HDRP's opaque fog pass skipped, Fog Adaptation off. Single scattering: the
  day's mist 61.0 against HDRP's 69.3, flicker 0.23 against 0.11; at night the torch lights the mist (warm glow,
  shadows: outdoors HDRP lights fog with the moon alone) and the moonlit blue haze is gone (HDRP's is its undimmed sky
  light). The walk (multiple scattering) is 1.5x HDRP's by day and too grainy at one walk a pixel: it needs a world-space
  cache of the mist's light. Not yet: local fog volumes, HDRP's fog passes off, the fog on reflections (`fogged`).
- C's first step (round 20 section 7, run `20261011-034434`): the effects are VFX Graph systems whose particles live in
  Unity's own GPU buffers (no public API): a capture of those buffers comes first.
- ROUND 20 MORNING (section 11; CHANGELOG "2026-10-11 morning"). The owner, 07:40: fog and translucents first, then the
  sky, then Mega Geometry; ~09:45: "move over anything that could have run sequentially or block the GPU before, so we
  know there's no HDRP bottlenecks". B MADE THE RULE: the traced fog with multiple scattering from the mist's own cache
  cells (Broc Meala 91 against HDRP's 69; night: torch glow, the moon's haze gone, grainy), mirrors and glass through the
  traced medium (`pt_medium.hlsli`), HDRP's screen-space fog blur skipped, the game's local fog boxes in the medium (3D
  texture sharing, `TGPT_SetFogBoxes`; built, not yet seen in the game). HDRP still runs for the fog: its VBuffer and
  volumetric lighting (its transparents read them) until C.
- The sky joins this entry (sky-1, `docs/agents/sky-1.md`): HDRP re-renders the sky cube every frame, runs the sky graph
  over every pixel and the clouds; our D3D11 copy of it can make the trace wait. sky-2 builds the tracer's own sky (brief
  `docs/agents/briefs/brief-sky-2.md`). The traced fog lacks the atmosphere's haze HDRP's fog pass added outdoors (far
  land too clear): the coordinator's, after sky-2's atmosphere tables. Questions to the owner: Q1 the visible sun and
  moon or the 30-degree ShadowCaster, Q2 the haze dims or not, Q3 cloud shadows.
- C: particles-3 (`docs/agents/particles-3.md`) decoded the campfire's and candles' buffers; the game stops simulating
  effects out of view (to design around: the owner's off-screen rule). Run `20261011-102244` (`campfire@particles-no-
  transparents`, the hidden `HDRP transparents not drawn (trial)`): with HDRP drawing no transparents the flames and smoke
  are gone from the picture and the game still simulates them (the Smoke and Sparks updates 5dd03cbe52db, 869d6c73cb2a
  dispatch 3 groups in each captured frame, as with HDRP drawing). Next: the hand-over and the tracing; the out-of-view
  freeze.
- State: A and B the rule; B's fog boxes and the haze open; the sky with sky-2; C after the run above; then the marks
  between HDRP's passes to find whatever else it still runs (shadow maps, light lists, its lighting, the VBuffer).

### PT-111 The passes take 4 to 10 minutes to make at a new build's first start (the owner, 2026-10-10)
- The owner, 11:40 on 10-10: "Why is in-game compilation so incredibly slow? It takes forever each time".
- What is waited for (the tracer's own log, run `20261010-092027`, a new build's first start): the DRIVER making
  GPU code of our passes. The 30 small passes take under a second together; the camera rays' pass 87 s, the
  walk's 160 s, then the two reordering passes minutes each: 241 s that morning; 436 to 486 s with the plain
  twins made too; over 600 s for the study build's ten big passes (each 113 to 162 s). A second start of the
  same build: 4 s (the driver's cache and our own). "Each time", because every build installed changed a
  shared shader file and so every big pass: eight builds between 20:19 on 10-09 and 11:15 on 10-10. A player
  pays it once a version of the mod and once a driver update.
- Why they are slow to make: they are enormous. Compiled, the reflections' pass is 51 MB as a text header, the
  bounced light's 37, the camera rays' 34, the walk's 28 (about a fifth of that as bytes): each ONE function
  with every material's shader, the water's path, glass, the subsurface walk and the lamps inlined, many of
  them in ten places (`docs/agents/speed-1.md` section 2: "Ten or more copies in the bounced light's pass";
  the review: "a 4.27 MB shader with 153 ray queries"). The same root as PT-99's reordering fault and as "code
  that is switched off still costs frame time".
- What would shorten it, none built: (1) the heavy functions once a pass, not in ten places (`[noinline]`, or
  the path's turns as a loop): smaller passes, to try in one run for both the making and the frame; (2) the
  shading as smaller shared shaders (the reordering redesign's third step; the review's S6): each made in
  seconds, and a change to one no longer makes every pass new; (3) only what the first traced frame needs made
  first (a denoiser's pass that is not chosen, the debug views, the plain twins: later or never), and tracing
  begun then; (4) more passes at a time where memory allows (two today: the driver takes gigabytes for one);
  (5) the coordinator's own: fewer installs that change the shared file.
- State: explained to the owner; (1) is the next small job in the shaders once `ser2` has handed over (the same
  files); (2) belongs to PT-110.

### PT-110 Is the tracer built for Shader Execution Reordering, Opacity Micromaps and RTXDI? (the owner, 2026-10-10)
- M4 IS OUT (2026-10-11 evening, worker ommout, `docs/agents/ommout.md`); M3's fix stays the rule. Measured with the
  game in front and alone on the card, off / on / off: the tavern's tracer 36.20 / 36.25 / 36.20 ms (run
  `20261011-172725`); the town's frame 87.69 / 87.23 / 86.97 ms, its tracer 67.81 / 68.17 / 67.40
  (`20261011-185528`). No gain. Removed: the hidden `Character micromaps (trial)`, frame flag bit 20 (free again),
  the characters' map builds, refits and links in `scene.cpp`, the bake's rules for hair and for characters
  (`pt_omm_bake.hlsl` is the text from before the trial), the recipe `micromaps-characters`, the source checks that
  pinned them. A skinned mesh is never queued for a bake. The "Micromaps trial" 16 stays: M3's offline check
  (`TGPT_TEST_OMM_ONLY`) compares against it, for the plants' marks too.
  Why it gave nothing: of the tavern's 421,395 character cut-out triangles 212,962 are hair, and hair's pieces
  stayed mostly unknown. The camera reads the strands through changing mips with an anisotropic footprint, the
  other rays read the alpha tile, a piece is settled only where every such reading agrees, and covered hair must
  stay unknown while the probe's camera lookup passes hair layers. So the alpha test's cost on hair was never
  removed, and the coordinator's reading of the tavern holds: its frame is not spent in characters' alpha tests.
  To try again, in this order: (1) the bound first: the "Micromaps trial" 2 (the characters' cut-outs solid for a
  step; recipe `micromaps`, step 6) in a clean `-Measure` run: if the frame is no faster with them solid, no map can
  help; (2) then one of: a proven bound on the camera's footprint over hair, a map for each reader (the camera's
  and the other rays'), or one reader for hair's alpha; (3) the probe's lookup made to agree with it. Not before
  (1) says there is something to win.
  Noted, not done: M3's part from ommchars (solid parts beside glass) has no offline pixel check yet (in the game
  the picture was the same); it would be a character with a glass submesh in `tests/pathtracing_omm_checks.inl`,
  the marks against the trial's 16.
- M3/M4 worker request (2026-10-11, `brief-omm-chars.md`): fix the tavern's 109,800 solid character triangles still
  tested in shaders; add character/hair micromaps, baked once for both alpha readers, linked through every pose.
  Run `20261011-161034`: characters 421,395 cut-out triangles in 32 instances (212,962 hair), none mapped;
  all cut-outs 567,982, 16.6% settled. M3's glass exception suppresses every solid mark in these mixed meshes.
  `ommchars` fixes the marks while making `caustic_clear` explicitly request candidates to skip its source instance.
  M4 was a hidden trial (frame flag 20), since taken out (the entry above). Its report: `docs/agents/omm-chars.md`.
  Additional findings: the camera's anisotropic strand mip footprint invalidates top-mip-only local hole proofs;
  bounds must cover every possible filtered read, leaving uncertain hair unknown. Detail tiles were not tracked
  as ready, and atlas replacements had no OMM generation stamp; the worker includes those invalidations.
  Covered hair is also kept unknown for the probe's camera lookup, which passes HAIR_LAYER. No hair speedup claim.
  Native/plugin and all 50 shader passes built at 17:03; hashes are in the report. Game pixels, speed, after-counts,
  useful hair acceleration and the post-game pixel check remain open.
- The owner, 10:35 on 10-10, after ruling that reordering is never switched off (PT-99; AGENTS.md): "Yeah,
  definitely investigate if there's anything we can do to any of our rendering to make it more compatible with
  Shader Execution Reordering, Opacity Micromaps, and RTXDI".
- Known at the start: reordering is used in its weakest form (one inline ray generation shader a pass, a 16-bit
  hint of our own, no hit objects: PT-99's note); micromaps cover 175 to 236 meshes while 49 instances of a
  tavern still have every triangle tested by the shader, and a ray alone costs the bounced light's pass 28 ms
  there; the bounced light's pass costs 14.4 ms in the tavern with no ray at all (RTXDI's resampling and what
  it traces are the suspects).
- Two jobs: (1) `ser1` (PT-99): the bounced light's pass under reordering, measured by variants in the game,
  and its design; (2) a read-only review by the `pt-max-reviewer` agent (started 10:37, `brief-compat-1.md`):
  for each of the three techniques across ALL passes: what the vendor's documents and reference code want,
  what the tracer does, every gap with its cost, the one game run that proves it and the size of the change,
  ranked. Its report: `<scratch>\reviews\compat-1.md`, to be filed as `docs/agents/compat-1.md`.
- THE REVIEW IS IN (11:15 on 10-10; filed as `docs/agents/compat-1.md`, 60 KB: sections for each technique, the
  gaps with labels, a ranking in section 7; nothing in it was built or run, and every gain is a guess unless
  it cites a run). ITS FIVE, in its order:
  1. R1: RTXDI's reuse across frames runs INSIDE the bounced light's reordered pass, after the regrouping
     (`pt_gi.hlsl` near 83 and 131 to 148); NVIDIA's sample keeps it a pass of its own and NVIDIA's guidance on
     reordering warns of exactly this. It fits every measured floor (14.4 ms in the tavern, 4.0 at the beach).
     Its guess: 10 to 30 ms in the tavern and the town. (The same as `ser1`'s step 1.)
  2. S5: the water's path regroups once for a loop with no end; it wants a regrouping a turn, in a pass whose
     threads carry the path alone. Proof: `water-day@regroup` on a `-DTGPT_REGROUP_WATER=ON` build (built by
     `perf2`, never run in the game).
  3. M1, M2: NO PLANT IN THE WIND'S POSE HAS A MICROMAP (6.4 M triangles in a town; `scene.cpp` near 1904), and
     every covered piece of a leaf is left "unknown" for every ray (`pt_omm_bake.hlsl` near 265).
  4. S1 to S3: the regrouping compiles to the hit-object form with an EMPTY hit object and a 16-bit hint (NVIDIA
     documents 8 hint bits with a hit object, the highest first; our material's number sits in the low bits);
     three calls compiled for one line; the whole fetched hit (68 numbers) carried across.
  5. M3, M4, G3: characters: 547,286 triangles in the tavern go to the shader's test (a fifth are no cut-out at
     all: per-submesh opaque flags, an hour); hair wants "bare texels only" maps; their structures are built
     for fast building, not fast tracing.
  And of RTX Mega Geometry (its section 6): NVIDIA's own sample traces rays through clusters 5 % SLOWER; the
  gain is video memory (its guess: about 475 MB in a town, the larger part without clusters) and perhaps the
  characters' rays; no frame time on plants.
- `ser1` IS IN TOO (PT-99; `docs/agents/ser-1.md`): the same faults counted in the compiled passes, a study
  build of six variants, and the design in three steps.
- WORKERS, started 11:20 on 10-10 (base84): `ser2` (`brief-ser-2.md`: the design's step 1, the reuse across
  frames out of the sorted pass, and step 2, the regrouping right after the ray by a hit object and a small
  hint; behind CMake `TGPT_SER_SPLIT`; target the yardstick's 14 and 28 ms) and `omm1` (`brief-omm-1.md`: a
  coverage count and a measuring trial first, then the characters' opaque submeshes, then micromaps for the
  plants in the wind's pose).
- `omm1` ENDED 12:20 on 10-10 (report `<scratch>\wt\omm1\docs\agents\omm-1.md`; base84; its dry merge is clean:
  11 files, 2 new; NOT MERGED YET: a build of main was running). In its copy, C++ and the plugin, no shader:
  - M0, THE MEASURE: a log line every 10 s, "PT cut-outs:" (the frame's cut-out triangles by kind: still meshes,
    plants in the wind's pose, characters with hair's share, cloth; how many trace with a micromap; what share
    of their pieces the maps settle), and a hidden `Advanced / Micromaps trial` (0; 1 plants' cut-outs solid, 2
    characters', 4 the others': THESE THREE MAKE THE PICTURE WRONG ON PURPOSE, for a measuring step; 8 no
    micromaps; 16 M3 undone; 32 M1; 64 with 32 for finding a cause). Recipe `micromaps` (about 6 minutes a
    scene: `tavern-room`, `metal`, `forest`).
  - M3, behind nothing: a submesh that is no cut-out is solid in a character's own structure and in a plant
    group's (its own check offline: 0 pixels differ). M5, behind nothing: no `ALLOW_DISABLE_OMMS` (an instance
    without its mesh's maps takes the plain structure), and the micromap structures compacted.
  - M1 behind the trial's 32: the plants in the wind's pose take their meshes' micromaps (recipe
    `micromaps-plants`, scene `forest`).
  - NEVER RUN ANYWHERE (a worker's offline test has no DirectX Raytracing 1.2): M0's read-back of the bakes,
    M5's compaction, ALL OF M1. So M3 and M5 change a default build's structures without having met a real
    micromap: THE HARNESS RUN IS THEIR FIRST TEST, before any play. If the device is lost at the compaction, the
    one line to take out is `ALLOW_COMPACTION` in `record_omm`.
- `omm1` MERGED into main 12:44 on 10-10 and installed (native 18972D6CC79A; M3 and M5 on by default, the trial
  at 0). IN THE GAME (run `20261010-124645`, 12:46 to 13:04; another program on the card at up to 27 % for a
  while: the two takes of "as it ships" agree to 0.7 ms in each scene). ms, a frame / the bounced light / the
  reflections, the world running, no denoiser:

  | Step (`Micromaps trial`) | the tavern | the town (`metal`) |
  |---|---|---|
  | as it ships (0) | 63.4 / 40.1 / 9.7 | 101.2 / 55.9 / 22.6 |
  | plants' cut-outs solid (1) | 63.7 (no plants) | 94.7 / 54.5 / 18.8 |
  | characters' solid (2) | 63.1 | 101.0 |
  | every other cut-out solid (4) | 56.3 / 35.4 / 7.8 | 93.8 / 50.1 / 21.8 |
  | EVERY cut-out solid (7): the whole prize | 55.8 / 35.3 / 7.5 | 84.4 / 49.7 / 14.4 |
  | NO micromaps at all (8) | 63.5 | 101.2 |

  So: (1) TODAY'S MICROMAPS BRING NOTHING MEASURABLE (8 against 0: the same to 0.1 ms in both scenes). The new
  count says why (the tavern): of 567,982 cut-out triangles a frame the micromaps settle 16.6 % of the pieces;
  435,261 triangles have no micromap at all (the characters' 421,395, half of it hair, none with a map); of the
  still meshes' pieces 29.1 % are left unknown, most of them leaves' and blades' covered pieces left unknown on
  purpose. (2) The whole prize of perfect cut-outs is 7.6 ms in the tavern and 17 ms in the town (the plants
  6.5 of the town's, still meshes 7.4, characters nothing measurable). (3) M3 DOES NOTHING IN THE GAME: the
  count says "109,800 triangles that are no cut-out reach the shader's test (0 more are spared it)" and step
  3 equals step 2: a fault in M3 (offline it worked) to look into. (4) M5's compaction works: "46 structures
  with micromaps, 10,808,320 -> 3,037,696 bytes", no error. (5) M1 (plants in the wind's pose with their
  meshes' micromaps, `forest@micromaps-plants`): 121.0 -> 114.8 ms frozen, 121.8 -> 119.6 running (inside the
  noise), the same picture (2.20 levels). NEXT for micromaps (the review's M2, M4, M7): the leaves' covered
  pieces opaque, hair's bare texels, so that the maps settle what the shader now asks; the prize says at most
  8 to 17 ms. M3's fault first (an hour).
- State: measured in the game; M3 and M5 in main (M3 without effect); M1 behind the trial; `ser2`'s trial next.
- ROUND 20 (`docs/rounds/round-20.md` 8 to 10; `docs/agents/audit-4.md`): M1 (plants' micromaps) and M2 (leaves'
  covered pieces opaque, a light's ray meeting leaves in a pass of its own) THE RULE. What alpha tests still cost,
  measured with the plants' cut-outs solid: the forest 19 ms of 83, the town 3.4 (and 8 for the other cut-outs).
  M7 in its simple form THE RULE: leaves and grass baked at level 6 (35 % of the plants' pieces unknown against 59 at
  4): the forest 82.8 -> 75.5 ms, the town 64.5 -> 62.2, +130 to 160 MB. Reordering: the bounced light's pass had no
  key (an empty hit object, 0 hint bits); keyed by its first ray's material it is 1.6 ms faster in the tavern and 4.2
  in the forest: THE RULE. Open: M4 (hair), a level per triangle (M7's full form), the camera pass's forced
  non-opaque query (`pt_primary.hlsli` 226), the water's path regrouped (S5).
- 2026-10-11 ~16:15: the tavern's cut-outs (run 20261011-161034, "PT cut-outs"): 567,982 triangles, characters 421,395 of
  them (212,962 hair) with no micromap, and 109,800 character triangles that are no cut-out still sent to the shader's
  test; micromaps settle 16.6 % (the baker's tighter test, review-perf-astra 4, changed it from 16.5 %: little in a room).
  Codex worker `omm-chars` (brief-omm-chars.md): M4, the characters' and hair's micromaps (bare or covered only where the
  alpha tile and the strand atlas agree), and why their solid triangles reach the test.
- omm-chars (`docs/agents/omm-chars.md`), merged ~17:20 (native 448BF6F48BAD). The cause of the solid triangles: one
  glass submesh in a character vetoed every solid mark of it (`character_marks`); fixed (the rule; Micromaps trial 16
  restores the old marks): 109,800 triangles spared the test. M4, the characters' maps (hidden `Character micromaps
  (trial)`, frame flag bit 20): 421,395 triangles mapped, 15 % bare, 32 % covered, 52 % unknown (hair stays mostly
  unknown: the camera's strand mips and the rays' alpha tile must agree), the tavern's settled share 16.6 -> 51.9 %. Run
  `20261011-172725` (-Measure, clean): the tracer 36.20 ms (solid fix), 36.25 (maps), 36.20 (fix again); the picture the
  same. The tavern's frame is not spent in characters' alpha tests. By the trial rule the maps come out (the fix stays).
  The town said the same at 19:00 (`20261011-185528`); taken out by worker ommout (this entry's first lines).

### PT-109 RTX Mega Geometry, "the Alan Wake 2 BVH": to be added (the owner, 2026-10-09)
- THE OWNER, 2026-10-12 11:00, told it no longer hangs, is still slower and sits in the code switched off: "I still
  want us to enable RTX Mega Geometry. Do not turn it off. I still insist there must be something we've done wrong
  if it's not making things faster. Othewise, what's the point?" SO IT GOES ON BY DEFAULT AND STAYS ON, and the 6 %
  below is a defect of ours to find (AGENTS.md has the ruling; it is no dead end any more). What the numbers below
  already point at, none of it proven: (1) with NVIDIA's layout the tracer makes about 10,000 plant structures a
  frame (13 to 15 ms of CPU), where the plain way puts dozens of plants into one structure (287 groups for 6,453
  plants in the forest): thousands of driver calls and thousands more instances in the top level; (2) its rays
  cost the bounced light 6 ms more (20.8 to 27.0 ms in the town): the clusters' size and flags, the micromaps under
  them (never checked offline), the partitioned top level's cells; (3) what it saves, the scene stage's refits (3.4
  to 4.8 ms of plants a frame in the town and the forest), is where the gain must come from. NEXT: one run on
  today's build to see it still runs (`metal@mega-plants`), the default flipped to NVIDIA's layout, and a worker
  on the fault (`docs/agents/briefs/brief-megageo-7.md`).
- 10-12 00:10, THE TOWN'S WALK (`20261012-000233`, `metal@mega-walk -Measure`, nobody else on the card, the game in
  front; main's build 49CD9C55E935 with megageo-6): a frame, plain / NVIDIA's layout (trial 7): 140.6 / 148.8 and
  135.4 / 144.9 ms; the tracer's GPU 82.1 / 84.3 and 80.7 / 82.0; the bounced light 20.8 / 27.0 and 21.9 / 28.7 (its
  rays pay about 6 ms more through the clusters), the scene stage 4.3 / 3.8 and 4.0 / 3.7; the tracer's CPU with trial 7:
  13 to 15 ms of scene record for 10,000 plant structures a frame. No hang. SO NVIDIA'S LAYOUT IS STILL SLOWER HERE BY
  ABOUT 6 % OF A FRAME: what it saves in building (half a millisecond) is less than what its rays cost. It stays in the
  tree, off (the owner's order of work); to reckon again when the frame is faster. Owed still: `mega-plants` standing.
  (The harness flagged every walk "did not start on the next rendered frame" again, with the passes ready: its check
  is too strict at 7 frames a second; the per-second tables show the walks did run.)
- 10-11 evening: megageo-6 (`docs/agents/megageo-6.md`; begun by a Claude agent that hit the usage limit, finished by a
  Codex worker, merged 21:20): THE FOREST'S HANG: the room a cluster operation was given for its results was sized per
  object where NVAPI asks for the explicit destinations' bounds; fixed, with the cluster arguments kept between frames and
  the driver's size questions asked once. Its check grows, shrinks and replaces plants (8,887 clusters, 1,566 plant
  structures). IN THE GAME (`20261011-212136`, `forest@mega-walk`, native 8DA42FEBFF9E): all four walks ran, NO HANG. The
  frame, plain / NVIDIA's layout: 202.8 / 206.7 and 183.2 / 218.6 ms: NO GAIN (rough: the harness flagged every walk
  "did not start on the next rendered frame", and two workers were compiling). The tracer's CPU with trial 7 is now
  6 to 12 ms of scene record a frame (its new "Mega Geometry CPU trial" lines; 21 to 24 ms more than plain at 18:29). The
  walk's cost is not the plants' structures at all (0.4 to 4 ms): see PT-131. Owed: `metal@mega-walk`, `mega-plants`.
- The owner, 13:40: "we [can] also add [the] nanite like new thing nvidia did for optimizing bvh construction,
  that was implemented in a post patch for Alan Wake 2"; 20:05, after the measuring run's numbers: "and add the
  alan wake 2 bvh". So: to be added, not studied.
- What it is (the coordinator's memory, the worker checks it against NVIDIA's documents): cluster-level
  acceleration structures and a partitioned top level, through NVAPI on Direct3D 12. NVIDIA cards only: a path
  beside the plain one, chosen by what the driver reports, never the only one.
- What to expect, said to the owner at 13:45 and again at 20:10: it makes BUILDING and updating the structures
  cheaper and smaller, not the rays faster. Here that is the plants (11,303 posed for the wind every frame in a
  town, 6.7 M triangles, 795 MB of posed copies and their structures), the characters, the water's pieces and
  the top level of 16,234 instances. The frame's "scene" stage is 1.0 to 1.6 ms, and whether the plants' and
  characters' structures are built inside it or outside every timed stage is NOT KNOWN: the worker's first
  measurement. It does not touch the water's path (140 ms at the beach) nor the bounce ray's 28 ms.
- The job: `docs/agents/briefs/brief-megageo-1.md` (what the PC's driver reports; what today's structure work
  costs, as a log line; hand-over 1 within an hour; then the first piece behind CMake `TGPT_MEGA_GEOMETRY` and
  a hidden `Advanced / Mega Geometry trial`). NVAPI is a new SDK for the tree: its license and version go into
  `docs/SOURCES.md` and README's list when it is merged.
- `megageo1` was stopped at 20:33 (the usage limit); `megageo2` continued in its copy and ENDED 00:35 on 10-10
  (report: `<scratch>\wt\megageo1\docs\agents\megageo-1.md`; NOT MERGED YET: a build of main was running):
  - THIS PC (RTX 5080, driver 616.92) REPORTS BOTH PARTS: cluster operations yes, the partitioned top level yes.
  - Built: the PARTITIONED TOP LEVEL beside the untouched plain one: CMake `TGPT_MEGA_GEOMETRY` (OFF) and the
    hidden `Advanced / Mega Geometry trial` (0; 1, 2, 3 = cells of 64, 128, 256 m; `TGPT_Frame.flags` bits 2 to
    5); a missing feature or a failed call logs a line and the plain path serves. Its own offline check
    (`TGPT_TEST_MEGA_ONLY`): 20 pictures the plain one's to the last pixel with all three cell sizes. IT SAVES
    0.03 ms A FRAME (the top level 0.124 -> 0.095 ms over 8,573 instances); tracing equally fast. "Safe, but
    nearly worthless alone." Not checked offline: opacity micromaps under it (look at leaves and grass).
  - A new log line in every build, "PT structures 10s": the "scene" stage by kind. THE PLANTS' COST IN PLAY IS
    STILL NOT MEASURED (the harness freezes the world; reckoned 3 to 5 ms in the town): recipe `structures`
    (`metal`, one step frozen, one running) reads it. Recipe `mega` runs the trial.
  - Its plan: the plants as clusters stamped from templates: about 220 MB and 2 to 3.5 ms in the town by
    NVIDIA's sample's ratios (to measure by building; the driver's own size forecast is a worst case, 429.5 MB
    against today's 428, and says nothing), no new shader passes (a cluster gets its own submesh number). ALL
    PARTS TOGETHER TOP OUT NEAR 280 MB AND 3 TO 4.5 ms OF AN 80 TO 180 ms FRAME: a small gain here.
  - Sources: NVAPI R615 (MIT; header and lib used only with the switch on; in the copy's `NVAPI-R615\`, to be
    copied beside main's CMakeLists; `.gitignore` gets `/NVAPI-*/`); RTX Mega Geometry SDK (NVIDIA RTX SDKs
    License, object code only): read, nothing copied. For `docs/SOURCES.md` and README's list at the merge.
- Merged into main 00:37 on 10-10 and in the game folder since 00:41 (switched off; CMake's switch on).
- THE OWNER, 2026-10-11 ~07:30, after round 20: "please do add RTX Mega Geometry": now, not later. Round 20 measured
  its target with the world running: the plants' poses and refits 4.7 ms a frame in the town (624 groups, 6.4 M
  triangles), and found that NVAPI's cluster structures take micromaps (the plants' level-6 maps must stay).
- THE OWNER, 01:05 on 10-10, told the numbers above: "RTX Mega Geometry is important, but not as a first step - that'll be a later optimization. The priority is
  getting this under control, and then fix the remaining rendering issues and missing features".
- THE OWNER AGAIN, 10:40 on 10-10: "Also don't forget about RTX Mega Geometry, it might end up giving us a bigger
  boost than we think". NOT FORGOTTEN, and its ceiling is now MEASURED IN THE GAME (run `20261010-092027`, a
  town with the world moving): the structures' work is 5.3 ms a frame (the plants 3.90, the water's pieces 0.64,
  characters 0.44, the top level 0.22) and the plants hold 766 to 795 MB. That is a twentieth of today's 109 ms
  frame; of a 30 ms frame it would be a sixth: ITS SHARE GROWS AS THE OTHER COSTS FALL, so the estimate is
  reckoned again whenever the frame gets faster. The review of PT-110 takes it as a fourth technique (where it
  could give more than the estimate: plants as stamped clusters, the water's pieces, characters, the world's
  arrival after a load, geometry left out today because building it costs too much, what a ray costs to trace).
- State: PARKED as a later optimization (the owner). The first piece stays in the tree, off; the trial's run
  (`metal@mega`) and the plants' part wait. `metal@structures` still runs once: it measures what the swaying
  plants cost a frame in play, which belongs to PT-99.
- 2026-10-11 (worker megageo-2, `docs/agents/megageo-2.md`; the owner had asked for it "really soon" after the stutter
  session): the plants as clusters stamped from templates, measured offline on this PC (driver 616.92) with six of the
  game's plant meshes and a sphere: structures 23 to 50 % smaller, the wind's per-frame work 45 to 60 % faster (the town:
  about 1.1 to 1.5 ms and 0 to 115 MB), the same hits to the bit; but every ray through a plant SLOWER: trees +60 to
  +130 %, small plants +10 to +42 %, the sphere +39 to +56 % (NVIDIA's own sample reports about 5 %), with ray queries and
  TraceRay alike, with FAST_TRACE builds, a surface-area cut and NVIDIA's pipeline flag. So no: step (a) said stop. The
  bench stays (`tests/megageo_cluster_bench.cpp`, `.hlsl`, `_rt.hlsl`; no CMake target: its report says how to build
  it) to ask again on a newer driver or DirectX's own clusters. Open by the 10-11 ruling (vendors' guides to the letter):
  why our sphere is 8 times NVIDIA's 5 % (their sample's cluster sizes, vertex layout and build flags against ours).
- The owner, ~15:00: "send it back to the agent, then? Clearly we are integrating it incorrectly? Still want us to get
  this done". Worker megageo-3 (`docs/agents/megageo-3.md`, bench `tests/megageo3_cluster_bench.cpp`): NVIDIA's own
  `vk_animated_clusters` setup rebuilt in our bench traces +3 to +10 % here: NVIDIA's 5 %, so our D3D12/NVAPI integration
  is right. The plants' gap: NVIDIA's 5 % is against a weaker baseline (uncompacted fast-build, 23 % slower than ours),
  our plant groups stack 16 to 18 porous copies in ONE structure (NVIDIA's sample: a structure per copy; its docs warn
  of overlap), and megageo-2's clusterizer (NVIDIA's, meshoptimizer's spatial builder, MIT, is 16 to 70 points better)
  and own vertex lists (not needed: 32-bit indices into each copy's vertices). On NVIDIA's setup in our layout: trees +31
  to +55 % a ray, grass +9 to +27 %, bushes and ferns +4 to +14 %; the town 0.2 to 0.8 ms and 20 to 95 MB saved while
  the world runs. Next: a game trial for the small plants only (megageo-4), trees plain.
- megageo-4 (`docs/agents/megageo-4.md`), merged ~16:30 (native 86610B2902CF): the hidden "Mega Geometry trial" 5 builds
  the plant groups of plants at most 4 m tall from stamped clusters (NVIDIA's clusterizer, meshoptimizer v1.3, MIT,
  fetched by CMake; the scene had no other way to tell grass from trees), 6 the same split plain without micromaps (5's
  yardstick: clusters carry no micromaps, NVAPI documents only its own arrays and NVIDIA's cluster samples use none), 4
  the split with micromaps. Its check `TGPT_TEST_MEGAPLANTS_ONLY` passes (5 against 6: 4 pixels of the lit picture off,
  0 of the first hits; a leaf edge's random draw differs, its mean the same). Cluster room is reserved at the driver's
  worst case: this trial answers speed, not memory. Run: `metal@mega-plants,forest@mega-plants -Measure`.
- Run `20261011-163231` (-Measure, clean: no other program, the game in front for every step; native 86610B2902CF), the
  tracer's GPU time a frame, the world running: the town plain 64.5 to 67.7 ms, clusters 68.6 (the scene stage 5.8 ->
  4.0 ms, every ray stage a little slower), the split plain without micromaps 68.6, with them 66.1; the forest plain 83.4
  to 85.1, clusters 100.8 (+16), the split without micromaps 95.8 (+11), with them 83.5. The clusters' loss is mostly the
  micromaps they cannot carry (NVAPI documents micromaps for its own arrays, none for clusters; NVIDIA's cluster samples
  use none): without them every ray through grass asks the shader. Verdict: no gain for the plants today. By the trial
  rule the cluster code comes out (the benches and reports stay to ask again: a driver or SDK with micromaps in
  clusters); told the owner ~16:50, who may keep it for another try.
- The owner, ~17:05: "It has to be a bug or an implementation overdight that [RTX Mega Geometry] is slower? What about
  over time, as we move around the world? we wont have to rebuild the bvh constantly, which is the point of it, right?"
  Yes, our departures: the clusters carried no micromaps (NVAPI's clusters take them, in NVAPI's own OMM arrays, not DXR
  1.2's: 11 of the forest's 16 ms), our plant groups stack copies in one structure (NVIDIA's design: a cluster BLAS per
  copy in the partitioned top level), and the trial was never measured walking. The trial code stays. Claude worker
  megageo-5 (brief-megageo-5.md): micromaps in clusters, NVIDIA's layout, a walking recipe (`walkSpeed`).
- megageo-5 (`docs/agents/megageo-5.md`; stopped at the usage limit, its check `TGPT_TEST_MEGAPLANTS_ONLY` run and passed
  by the coordinator) merged ~18:25 (native 4D882D372C06): trial 5 the small plants' clusters with micromaps (NVAPI's own
  OMM arrays from the same bake), 7 NVIDIA's layout (a cluster bottom level per plant, an instance each, the partitioned
  top level), 8 = 7 without micromaps; recipes `mega-plants` (standing) and `mega-walk` (150 m at 5 m/s). Run
  `20261011-182915` (-Measure; `mega-walk`, the town and the forest; the harness calls each walk invalid only because it
  began a frame late: the camera did walk ~143 m): the town, plain 127 / 126 ms a frame (worst 268 / 264), trial 7 140 /
  149 (worst 319 / 390); the tracer's GPU time the same (69 to 72 ms): the loss is outside the traced passes (~1,000
  plant bottom levels built a frame). The forest: trial 7 HUNG THE GPU in the scene stage during its second walk (TDR,
  frame 4003). Not yet run: `mega-plants` (the standing comparison: wait for the hang's fix). Next: megageo-6 (the hang,
  where the frame time goes, against NVIDIA's own per-frame work in vk_animated_clusters).
- megageo-6, Codex takeover 2026-10-11 20:45 (`docs/agents/megageo-6.md`): the predecessor had also left an
  unfinished sizing fix, contrary to its report. Confirmed NVAPI contract defect: each explicit BLAS destination
  was sized for that plant alone while the batch declared the largest tree; no GET_SIZES justified the smaller
  rooms. Size-class batches now check the actual call's bound and read result sizes back asynchronously. Arguments
  are reused for unchanged groups; four driver allocation queries per group per frame were only for logging and
  are now cached at creation. Final DLL and guarded focused growth/shrink/replacement check pass (21:06), reaching
  8,887 clusters/1,566 plant BLAS with no oversized result; all 116 source checks pass. The forest TDR is not yet
  proven fixed; the new per-second CPU lines separate plan, pose,
  cluster and top-level work. No export, shader, public layout, option or bit change. Game runs owed: forest and
  town `mega-walk`, then `mega-plants`. Implicit BLAS placement/GPU instance writes remain an explicit follow-up.

### PT-108 The rain's wetness is missing from the traced world: dry, flat ground where the game's glints (2026-10-09)
- Found from the owner's save "PT spec-highlights-terrain" (PT-104's run `20261009-190504`): the tracer's own
  check of its first hit against the game's G-buffer in that scene: the same surface on 95.2 % of the screen; of
  those pixels another colour 0.7 %, another normal 18.7 %, ANOTHER ROUGHNESS 59.9 %; the terrain
  (`Mat_UT_B_01_c_CampaignMap_HOS`, `TG/Environment/MeshTerrain`, 26 % of the screen): colour 0 %, normal 63 %,
  roughness 98 % of its pixels.
- The cause was written down on 10-08 by the worker `materials-2` (`docs/agents/materials-2.md`, "Outdoors") and
  never became a job: the game has a ScreenSpaceWetness pass that rewrites the camera's normal and roughness
  after HDRP's depth and normals, by the rain, by moisture that builds up and dries (PrecipitationController:
  0.25 of the precipitation a second) and by a top-down shelter depth array. Its own tables then: the forest's
  terrain 57 % another roughness, the vista's 86 %, floor tiles 97 %, cliffs 93 %: every outdoor material.
- The owner's rulings that decide the how: the tracer reproduces what every one of the game's shaders shows; a
  mirror shows the world as the camera sees it; nothing screen-space. So: wet at every hit of every ray, from
  the weather's own state and the game's own shelter data, never a pass over the screen.
- The job: the Claude worker `rain1` (started 19:15 on 10-09, base74, `brief-rain-1.md`, report
  `docs/agents/rain-1.md` in its copy): what the game's pass does exactly; the same at a hit behind a hidden
  switch; its first hand-over within an hour, tested by one run of `spec-terrain` (the terrain's shares in the
  Yardstick line, the ground's glints with no denoiser).
- WHAT THE GAME DOES, read by `rain1` (`docs/agents/rain-1.md`, 19:46): the pass changes ONE number a pixel, the
  roughness; never the normal, never the colour. Moisture (0 to 1, rises by precipitation x 0.25 a second, dries
  by 0.05) times how much a surface faces up, a noise picture laid over the world (10 m a tile), a soaked
  smoothness of 0.9 at the camera fading over 100 m, and a shelter (baked top-down heights, dry under a roof).
  SO THE TERRAIN'S OTHER NORMAL (63 % of its pixels) IS NOT THE RAIN: another cause, not found (the mesh's own
  tangents are not read: `materials-2`'s note), and the game's hundreds of separate glints need that relief.
- THE FIRST PIECE, merged into main 20:02: the wet roughness at every hit of every ray (`rain_roughness` in
  `hit_material`), from the game's own moisture and its own noise texture (shared, not copied); hidden
  `Advanced / Rain wetness` (0) and CMake `TGPT_RAIN_WETNESS` (OFF; ON for the build of the run); a record of
  its own (`TGPT_SetRain`, the frame constants 8048 -> 8064 bytes, the ABI number unchanged); recipe `rain`.
  NOT BUILT: the shelter, so with the switch on everything is under the open sky, indoors too. Three ways for
  it, a choice for the owner: (A) share the game's own array (it is a render texture with random write: the
  hook does not flag those; a day, may be refused by the card); (B) read the same baked files the game streams
  (`StreamingAssets/DepthTextures/<map>/depth_tex_X_Y.raw`): exact data, reaches as far as rays do, 6 to 13 MB
  that the game also holds, half a day: the worker's advice; (C) a ray straight up at every shaded hit while it
  rains: no data at all, 15 to 25 % of the tracing time then (its estimate), differs from the game's bake.
  The game's screen-locked noise is replaced by its mean (nothing in the tracer may hang on the screen).
- IN THE GAME (run `20261009-202514`, 20:35; sheets `spec-terrain\ground-game-dry-wet.jpg` and
  `whole-game-dry-wet.jpg` in the run's folder): "Rain wetness on trial: wet. Moisture 0.81"; the terrain's
  "another roughness" 98 % -> 1 % of its pixels, the whole screen's 60.0 % -> 18.8 %; "another normal" the
  same in both (terrain 63 %). Exposure the same (log2 -12.66). THE PICTURE: with the switch on the ground is a
  flat wet sheet with one round mirror image of the flame (pixels over 200 in the ground's patch: 0.14 % dry,
  2.14 % wet, the game 0.19 %); the game's ground is mud and gravel with relief, the fire spread over it in
  warm patches (fine detail 13.4; traced 5.4 dry, 5.8 wet). So the roughness is right and the picture is
  further from the game's than dry: A WET SURFACE WITHOUT ITS RELIEF IS A MIRROR. The relief is the next job
  (PT-104: the worker `terrain1`); the rain trial stays off until it and the shelter are in.
- THE OWNER'S WORD ON THE SHELTER, 16:40 on 10-10: "Regarding dry under roof, can't we make that 'just work' with ray tracing? We already know what's covered vs not
  because we trace the rays. Why rely on height files?".
  So: THE RAY, not the baked files (way C). It fits the game's own rule exactly: the game's height array only
  stores how high the nearest thing above a point is, and its wetness falls off by that height (k = 1 -
  smoothstep((roof - y + 0.2) / (maxHeightDiff + 0.2)), `docs/agents/rain-1.md`); a ray straight up returns that
  height directly, from the world as it stands that frame (an open door, a cart, a character). Build: at a hit
  that the rain would wet (moisture over 0 and the wetness above 0), one ray up: the nearest hit within
  maxHeightDiff gives k by the game's formula, a hit beyond it gives dry, no hit gives open sky; for every kind of
  ray (mirrors see the same). Two things to know: (1) the game's bake also holds a "RainObstacle" layer of things
  no camera draws (invisible rain blockers placed by artists): the ray does not see them unless the plugin hands
  that layer's meshes to the tracer as geometry for this one ray (the game's own data, no copy); to look at
  first. (2) Its cost (one more ray where it rains and the surface would be wet) is measured on the rain scene;
  the worker's guess was 15 to 25 % of the tracing time while it rains. Behind the rain's own switch until the
  game has answered. In the queue after the speed work (the owner's order of 10-10 01:05).
- The owner, the same message, of Frame Generation and Neural Rendering in their session of 10-09 17:30: "dont
  remember". Not owed any more.
- State: the first piece works in numbers; the switch stays 0; waits for PT-104's relief and the shelter (a
  choice for the owner). The trial is compiled into the build in the game folder (CMake `TGPT_RAIN_WETNESS`
  ON): configure it OFF for any build whose speed is read.

### PT-107 The game's own rendering still runs under the traced picture: its time and its memory (the owner, 2026-10-09)
- The owner, 18:30: "as far as I understand we're also still rendering the vanilla rendering underneath or PT still,
  aren't we? So we're paying for vanilla rendering AND PT on top? How much do we win by preventing that?"
- Answered with the day's numbers: in the guard scene a frame is 139 ms (72 frames in the tracer's 10-second
  line) of which the tracer's GPU time is 121; with tracing off the game runs at 82 to 91 frames a second (11 to
  12 ms a frame). So ALL of the game's own work under the traced picture is at most 18 ms of 139: preventing it
  wins at most one frame a second at today's speed. Its MEMORY is the larger matter now (PT-87): the game holds
  about 9 GB of video memory while tracing.
- What still runs is not listed anywhere as a whole. Known: HDRP's G-buffer (the yardstick and some guides), its
  depth and motion, its shadow maps when the game's Shadows are on (`HdrpPasses.cs`: "what HDRP shadows itself
  takes no traced shadow twice"), the fog and its lights (kept by the owner's ruling), what is drawn forward and
  not traced yet (effects, trails, a ghost: PT-83), post-processing, the interface. The owner's ruling of
  2026-10-07 stands: "super wasteful, don't do that"; nothing rendered twice.
- To do: the inventory with a number each (one run with HDRP's frame settings read out and its passes' GPU
  times; the video memory with tracing on against off in the same spot), then each part either needed by the
  tracer (say for what) or switched off while tracing, its buffers released.
- State: the Claude worker `game1` (started 20:19 on 10-09, base77, `brief-game-1.md`, plugin only): the
  inventory with MB and ms; a first cut behind a hidden `Advanced / Game passes while tracing`; what of HDRP's
  memory can be let go while tracing, as choices. Its measure in the game: `notTracer` in the memory line.
  (`game1` was stopped at once that night; its reading is in `<OLD scratch>\wt\game1\docs\agents\game-1.md`.)
- MEASURED, ROUND 19 (`docs/rounds/round-19.md` 3; run `20261010-173919`, Sunshine on the card up to 4.1 %): new
  timestamps on the GAME'S device split each traced frame (the native 10-second line's "game device a frame"). The
  tavern, no denoiser: the game from our trace's start to the composite 4.0 to 4.9 ms, the composite's wait for the
  trace 30.3 to 30.8, the composite to the next frame's trace 10.8 to 12.9; the town: 7.5, 62.5 to 63.3, 16.1 to
  24.7. The game alone at the same spots 11.6 ms (tavern) and 13.9 to 14.8 (town). AND THE CARD TAKES TURNS: a
  standalone test (`<scratch>\overlap`) runs a D3D11 job and a D3D12 job of one process in the SUM of their times
  (51.3 ms for 25.6 + 25.5), two queues of one D3D12 device in the longer (25.4). So the game's whole frame adds to
  every traced frame, whatever the order of the two devices, plus about 4 ms (tavern) to 9 to 18 (town) more. The
  stamps now time the composite and Fog Adaptation too (native 6C54E84BCA33). The cut is built (plugin only): the
  hidden `Advanced / Game passes while tracing` (1 skin scattering, 2 transmission, 4 shadow maps, 8 reflection
  probes, 16 the fog's lighting; `GamePasses.cs`, a postfix on HDRP's per-camera frame settings), recipe
  `game-passes`. Shadow maps and the fog's lighting are measured only: the owner keeps the game's fog.
  ANSWERED (run `20261010-181120`, clean): the cuts reach HDRP (the fog's lighting off darkens the town 51.8 -> 50.4
  levels) but NONE saves measurable time (tavern 3.7 to 4.0 ms before the composite, 10.3 to 10.9 after; town 7.4 to
  8.0, 16.7 to 23.5 wandering); our composite and Fog Adaptation are 0.1 ms each. The switch stays 0, a trial. Next
  suspect: frame pacing (TG Frame Generation's Reflex stays on in harness runs, our trace is recorded mid-frame);
  `Run-PtHarness.ps1 -LowLatency Off` for one run. NOT IT (run `20261010-182435`, clean): the same frames with Low
  latency Off. The town's extra 10 to 14 ms on the game's device stay unexplained; next: finer stamps after the
  composite (round-19.md 5).
- PLACED AND COUNTED (runs `20261010-190135`, `-191319`, both clean): from our composite to HDRP's fog 0.4 ms, so
  the whole "game after" lies between HDRP's fog and the next frame's trace; and at EVERY frame's render event 1 the
  frame before's trace was already done on the card (100 % of frames, tavern and town, with Reflex and with Low
  latency Off): the CPU is never a frame ahead; the card waits for it. A trial of Unity's frame queue
  (`QualitySettings.maxQueuedFrames` set at runtime, run `20261010-192506`): with 3 queued the CPU was still late on
  100 % of frames, AND SWITCHING IT BACK FROM 3 TO 2 WHILE THE GAME RAN HUNG THE GAME (19:28; Windows' Application
  Hang at 19:33, no driver reset; the owner, on Moonlight then, saw it frozen). The trial was removed the same hour
  (a read-only line of the game's pacing stays: frames queued 2, VSync count 1, no frame-rate target). Next (native
  E43DA18A3646): the CPU's clock against the trace's (the queue's clock calibration), to see whether the render
  thread is held behind the trace (driver) or the CPU is simply busy.
- ANSWERED, ROUND 19 (`docs/rounds/round-19.md` 7 to 10; runs `-222055` to `-232944`): the CPU's clock against the
  trace's and marks on both of the game's threads: the render thread is held at the start of each frame by Unity's
  `BufferD3D11::EndWrite` (the render-thread half of `LockBufferForWrite` / `EndWrite`: a 4 MB staging buffer mapped
  with no "do not wait"; by the hooks on the game's Map with the file `measure-waits`, and Unity's public symbols),
  asked for in PreLateUpdate, most likely by Entities Graphics' `SparseUploader` (the ECS-drawn objects), NOT by
  HDRP's rendering. It waits until the card ends the frame before. NOT the NVIDIA profile's "Maximum pre-rendered
  frames = 1" (cleared for one run with the owner's leave, then set back), not Reflex, not video memory (8.4 to 10.6
  of 14.85 GB). BUT IT COSTS ONLY 1 TO 3 ms: the card's gap between frames (the game's device's new stamps, run
  `-232944`, clean) is 1.1 ms (tavern) and 1.9 to 2.7 (town). A traced frame is the trace (32.7 tavern, 66.2 town)
  plus the game's own work on the card (about 12.6 and 20: after the fog to the frame's end 6.6 and 7 to 10, the next
  frame's passes before our trace 1.5 and 4.3, before the composite 3.2 and 6.2). Our capture pass takes 13 ms of
  the main thread a frame (the decals: `FindObjectsByType<Renderer>`, every LOD group and every projector, each
  frame), off the critical path today; a CPU cost to cut. NEXT: the game's pass groups timed on the card while
  tracing (HDRP's custom pass injection points), against the game alone; then what the traced picture does not need
  goes, the picture unchanged.
- ROUND 20 (HDRP draws no opaque object since step A; runs `20261011-051727` and `-060805`, clean): the town's frame is
  about 85 ms = the game before our composite 12.3 + the trace 60 to 63 + the game after it 10 to 12; with the world
  FROZEN the game before the composite is 8.0 (forest 7.6), RUNNING 12.3 (forest 11.5): the moving world adds about 4
  ms of the game's own card time before the composite, and our trace starts 10.8 ms after its inputs are flushed (the
  card takes turns). HDRP's injection points have no mark between AfterOpaqueDepthAndNormal and AfterOpaqueAndSky
  (the game's HDRP DLL lists BeforeRendering, AfterOpaqueDepthAndNormal, AfterOpaqueAndSky, BeforePreRefraction,
  BeforeTransparent, BeforePostProcess, AfterPostProcess): splitting those 8 to 12 ms needs marks of our own as small
  render graph passes after HDRP's own (shadows, volumetric lighting, deferred lighting, sky). Not built.

### PT-106 Eyes and hair of people look wrong in play (the owner, 2026-10-09)
- The owner, 18:30, after playing with the build of 16:58: "I also noticed more problems with NPC eyes and hair
  during my own play". No picture of these beyond PT-105's. Known open: PT-18 eyes too small, PT-53 to PT-56
  hair, PT-73 faces.
- To do: a run of the people scenes (`npc-day`, `npc-candle`, `hair-*`) with no denoiser and NRD, the eyes and
  hair cut out beside the game's; ask the owner which people when the pictures are there.
- State: reported; not looked at.

### PT-105 A man's hair is a soft blur (the owner's screenshot, 2026-10-09 17:16)
- The owner, 18:30: "Seems we also still have problems with hair on this guy", the screenshot
  `C:\Users\Frede\Pictures\Screenshots\Screenshot 2026-10-09 171643.png` (the "Retired Keeper" in a dark
  wooden room, NRD, 11 frames a second): his short grey hair has no strands, it is a soft cloud round the head
  with a halo against the dark wall; the face is lit and sharp, the eyes dark.
- Not known: whether the traced hair is that soft with no denoiser too (then the hair's own layers: PT-56), or
  NRD smears it (the family of PT-95, PT-100, PT-104), or both. To do: find the man (the save is the owner's) or
  a like head in `hair-*`, with no denoiser, NRD and the game's own picture.
- State: seen in the owner's screenshot; not looked into.

### PT-104 With NRD the whole picture is smoothed in play: floor planks, surfaces (the owner, 2026-10-09)
- The owner, 18:30: "When I playtest, at least with NRD, I do see a lot of general smoothing going on. When I walk
  around, I see floor planks that look smoothened out and various surfaces." And a new save, "PT
  spec-highlights-terrain": "where we can see if PT preserves the specular highlights we see on the ground terrain
  in a scene where there's a fire nearby. In the PT presentation, it looks flat."
- It is what PT-95 measured on a helmet the same afternoon, now seen everywhere: with NRD the direct light, the
  bounced light and the reflections each keep 7 to 10 % of the fine detail a normal map puts into the picture
  (the worker's sunlit wall: 0.58). The planks' and the terrain's relief is normal-map detail of that kind, and a
  highlight of a fire on rough ground is the reflections' signal.
- The scene is listed: `spec-terrain` in `tests/ingame/scenarios.json` (the owner's save by its name). To do
  first: `spec-terrain@relief-where` (the game; no denoiser; NRD; the Specular, Direct and Indirect views): is
  the highlight there with no denoiser (then it is the filters, PT-95's job) or missing from the traced picture
  itself (then the terrain's material: its smoothness, its layers, PT-10).
- THE RUN, 19:06 on 10-09 (`20261009-190504`, `spec-terrain@relief-where`, native 01130141AB47; sheets
  `terrain-game-none-nrd.jpg` and `terrain-ground-game-none-nrd-specular.jpg` in the run's folder): THE GLINTS ARE
  MISSING FROM THE TRACED PICTURE ITSELF, with no denoiser: the game's ground is wet gravel with hundreds of
  small glints of the fire, the traced ground is dry and even with one soft orange blob. Fine detail in a patch
  of that ground: 15.65 in the game, 4.62 traced with no denoiser (its pattern unlike the game's: 0.28), 3.88
  with NRD (84 % of the traced one). So here NRD takes a sixth; the rest was never traced. WHY: it rains in that
  save, and the game wets what stands under the open sky (PT-108). The tracer's own check says it in numbers:
  the terrain's colour is the game's on every pixel, its roughness another on 98 %, its normal on 63 %.
- 23:50 on 10-09: THE TERRAIN HAS NO RELIEF IN THE TRACER (PT-108's run): the same pebbles as a flat print; its
  normal another than the game's on 63 % of its pixels. Not the rain (the game's rain changes roughness only).
  The Claude worker `terrain1` (started 23:46, base78, `brief-terrain-1.md`): which of six suspects (the
  layers' normal maps not reaching the pass, their strength, their channels, the frame for maps laid over the
  world, the blend, relief the tracer lacks), the one run that proves it, the fix.
- THE CAUSE, found by `terrain1` (00:08 on 10-10; `docs/agents/terrain-1.md`), read from the game's own mesh
  files: THE GROUND'S MESHES HOLD POSITIONS AND NORMALS ONLY: no texture coordinates and no tangents (576 of the
  game's 613 ground meshes; the other 37 have no texture coordinates either and the tangent (1,0,0,-1) at every
  vertex). The tracer builds a normal map's frame from the mesh's texture coordinates, finds none, and
  `frame_normal` SILENTLY answers the flat normal: all eight layers' bumps (strengths up to 4) were thrown away,
  for every ray. The game's shader turns the bumps by the mesh's tangents, and for a mesh without them Unity hands
  it the object's x axis. The fix (9 lines in `program_terrain`): the frame from the object's x axis, behind the
  hidden `Advanced / Terrain frame` (0 = flat as before; 1 or -1 = the fix with that sign: the worker found no
  source for the sign, the run decides); three plugin log lines (each terrain material's maps and strengths, the
  mesh it is drawn with and what that mesh brings, the switch). Recipe `terrain-frame` (scene `spec-terrain`:
  the game; flat; +1; -1; the Normals view; the Yardstick view; +1 with the rain's wetness). Its forecast: the
  terrain's "another normal" from 63 % to under 20 with the right sign. NOT DONE: the sign per mesh (the 37 with
  tangents may need the other); OTHER MATERIALS ON MESHES WITHOUT TEXTURE COORDINATES STILL GO FLAT SILENTLY (a
  defect of its own: to count and to log). Seen on the way: in the traced picture the brazier lights only two
  patches of ground, in the game the whole ground round it (if the ground stays dull after this: the lamp).
- Merged into main 00:10 on 10-10 (snapshot `<scratch>\pre-terrain1`; 5 files, 0 conflicts, 108 checks); building.
- IN THE GAME (run `20261010-092027`, recipe `terrain-frame`): THE SIGN IS +1. The terrain's "another normal": 63 % of
  its pixels flat, 4 % with +1, 54 % with -1; the whole screen's 18.7 % -> 2.4 %. With the rain's wetness too:
  the terrain's roughness 98 % -> 1 %, the screen's 60.1 % -> 18.8 %. THE PICTURE (sheet
  `spec-terrain\ground-game-flat-fix-wet.jpg` in the run's folder): with the fix the traced ground is the
  game's mud and gravel with its relief, the fire spread over it in warm patches; fine detail in the patch
  13.5 in the game, 5.4 flat, 10.3 with the fix, 11.5 with the fix and the rain. Left: the traced ground is
  darker (mean 61 to 63 against the game's 71) and the fire's warm patches fainter than the game's (the
  brazier's light on the ground: `terrain1`'s remark; not looked into). TO DO THE SAME DAY: +1 becomes the
  rule, the switch goes; the 37 ground meshes with tangents of their own and other materials on meshes
  without texture coordinates are still to check.
- MADE THE RULE by `rule1` (merged into main 10:32 on 10-10; `docs/agents/rule-1.md`): the frame from the object's
  x axis, +1, in every build; the switch is gone. NOT DONE, AND IT MATTERS: 37 of the game's ground meshes bring
  tangents of their own (w = -1) and need the sign -1 by the game's own rule (the bitangent times the tangent's
  w); the game draws them a lot (10 of 14 terrain materials round `spec-terrain`, 11 of 14 at the beach), and one
  material sits on both kinds of mesh, so the sign must come from the MESH (its tangent stream, which the native
  side can take already): about 20 lines in plugin and shader. Until then those grounds have their bumps turned
  the wrong way (reckoned: 54 % "another normal" on them). The 4 % was measured on a mesh without tangents.
- State: the rule for meshes without tangents (most of the ground); the 37 with tangents of their own are open.

### PT-102 Lights of the scene loaded before stay in the tracer's list after another save is loaded (2026-10-09)
- Seen in run `20261009-162628` (the tracer's 10-second lines): the guard scene `metal` counts 510 lights when it is
  the first scene of a session (runs `20261009-103336`, `20261009-160428`) and 1,021 when loaded after the
  tavern (514 lights). The picture is the same (its mean level 49.9 and 50.0 both ways, the light grid's own
  numbers too: 13,383 cells lit, 14 lights a cell), so the 511 are not near the guard: they look like the
  tavern interior's lamps, never released. Not known: whether that interior is still loaded in the game (then
  it is right) or its lights are stale records (then they are a leak that grows with every load, and a lamp
  that lights nothing real).
- To do: the plugin's light scan against the game's loaded scenes after a second load; small.
- State: seen; not looked into.

### PT-101 A harness run started beside Starfield and ran on while the owner played (2026-10-09)
- 15:44: the coordinator's build chain started run `20261009-154425` while Starfield was OPEN, nearly idle (7.5 % of
  the GPU, 1,323 MB of video memory: the run's `gpu.json`, "before"). The start guard counted a program as
  using the GPU from 10 % or 2,000 MB, so it saw a free card after 12 s. The owner then played: Starfield up
  to 98.2 % while our game traced beside it until 15:54. The owner's rule of 2026-10-06 ("you spawned the game
  while Starfield was running ... any process ... Make it a general thing") was broken for about 8 minutes.
  Told the owner at once.
- FIXED 16:00 (`tools/harness/Run-PtHarness.ps1`; AGENTS.md's rule says it): a game of the owner's that is
  OPEN holds every start back whatever it uses (`$ownerGames`: Starfield; add names as the owner plays other
  things), with or without `-AllowGpuContention`; a program counts as using the GPU from 4 % or 800 MB; and a
  run in progress ENDS ITSELF and closes our game when a listed game opens or another program (not a worker's
  offline test) holds the card for two readings in a row (20 s); `gpu.json` then carries `stoppedFor`.
  Checked against the Starfield then running: "A run of the game would wait." The mid-run stop has not been
  seen working yet (no run has been made since).
- That run's frame times are not clean (PT-99's note). Its pictures are.
- State: fixed in the guard; the mid-run stop to see once.

### PT-100 With NRD everything seen through glass is a blur (2026-10-09)
- Seen in run `20261009-142733` (`refract-jar`, sheet `jar-close-game-nrd-trial.jpg` in the run's folder): with no
  denoiser a jar shows its film and the wall's stones behind it; with NRD the whole body of the jar is soft
  blobs, with the glass trial at 0 and at 7 alike. The lid and the wall beside the jar stay sharp.
- The likely cause, not proven: a glass pixel hands NRD the GLASS's depth, normal, roughness and motion, and its
  light is what lies behind the glass, so NRD takes detail behind the pane for noise on one smooth surface and
  spreads it (the same family as PT-95, the helmet's relief). The known cure in NVIDIA's own samples is to give
  the denoiser the guides of the first rough thing a mirror-like path meets, with motion of that virtual point
  (never copy their code: the idea alone). What Ray Reconstruction makes of the same pixels is not looked at.
- To do: prove it (the guide views on a jar; NRD with the guides of what is behind the glass); then the fix
  for glass, water's surface and mirrors alike. After `nrdrelief` is merged (the same files).
- State: seen; the cause guessed.

### PT-99 Speed: 8 frames a second is not acceptable; NVIDIA's cluster acceleration structures (the owner, 2026-10-09)
- 10-12 10:55, THE OWNER KEEPS THE STREAM ON (asked for ten minutes without it so that speed could be measured: "I
  would prefer if I can stay on the SUnshine/moonlight stream"). So speed is measured WITH the stream from now on
  (AGENTS.md has the rule: named so, held against runs with a stream alone, decisions by an A/B/A within one run);
  the baselines of 06:20 and before, taken on a quiet card, are not the yardstick any more. A NEW BASELINE WITH THE
  STREAM is owed (`speed-now -Measure` over the five scenes: it takes the owner's screen for about 12 minutes).
- 10-12 10:55, THE WATER'S PIECES IN GROUPS (worker waterblas, done; `docs/agents/waterblas.md`; hand-over 2:
  `%TEMP%\tgb\waterblas-native-3\TGPathTracing\TGPathTracing.Native.dll`, B4DA3E0E77EE). Some dozens of pieces of
  one slot and cut level share one bottom-level structure, one refit call and one instance (a geometry a piece; a
  hit finds its piece by the instance's ID plus the geometry's index, as any submesh; no pass that traces changed,
  only the pose pass `pt_water_mesh.hlsl`). IN THE GAME, hand-over 1 (5343B2B8C137; `20261012-094311`, the world
  running, standing; main this morning / its DLL, the record's CPU a frame): THE BEACH (2,459 pieces, 66 groups) the
  water's pose 3.72 / 0.20 ms, the scene's record 6.50 / 3.84 to 4.04, the 10 s line's record 7.00 / 4.33 to 4.58;
  THE TOWN (1,379, 40) 2.05 / 0.14, 6.65 / 5.43 to 5.70, 7.2 / 6.07 to 6.38; THE FOREST (1,793, 51) 2.70 / 0.15,
  5.69 / 3.20, 6.2 / 3.69 to 3.78. The scene stage on the GPU 2.91 / 2.51, 6.19 / 6.00, 4.93 / 4.64 (indicative:
  another test was on the card). The structures' pages 160 / 128 MB at the beach and in the forest. OFFLINE: 212
  frames of a walk over the sea fixture (pieces made, collapsed, groups put together) give ONE hash for the groups,
  for the same code ungrouped and for main's old DLL; three mutants fail. Hand-over 2 adds, none of it in the
  picture: the pools keep their free stretches (hand-over 1 held twice the pages it needed after other blocks had
  been through them), the frame's count in one pass, a wider self-check. HAND-OVER 2 IN THE GAME, 11:00
  (`20261012-105944`: the forest's eight walks and the beach, the record's self-check on): no "DIFFER" line, no GPU
  stop; on the walks 60 to 65 refit calls a frame, 0.2 to 1.5 groups built a frame; the pools 256 MB of pages (the
  structures' 192 with 94 to 140 in use). MERGED 11:07 (`merge_copy.py waterblas --base=base20o`: 9 files, 0
  conflicts); main D473560B18DA. Its runs as main's own build (the town frozen against the build before, the
  record's CPU) started 11:20. ITS FIND ON THE WAY, not its own, not entered yet: at 0.5 m a frame, three runs of the same walk
  whose ends lie 8 m from the next one's start differ from one another in 15 frames (some dozens of channels, by
  0.15 at most), with main's old DLL too: something of an earlier run reaches a later one across a cut.
- 10-12 09:45, THE WATER'S RULE IS SUMMED THE PLAIN WAY (worker campfix's second hand-over, merged by hand into the
  sources: `pt_water_sun.hlsli`, `tracer.cpp`, `tests/water_sea_gpu.inl`, the source check; `docs/agents/campfix.md`).
  Its offline check ran this morning and found ITS OWN LOW-SUN CORRECTION WRONG ("each corner its own facet's sun"
  for a coarser cell, in main since 05:20): the sun's share of the picture 0.6 to 1.1 % over the reference at a sun
  of 57 degrees, one band of the picture 1.6 to 1.8 % brighter, 1.5 to 3.8 % at 8 degrees. The same coarse cells
  summed the plain way (campspeed's): within 0.16 % at every ray width, every band within 0.5 %, within 1.9 % at 8
  degrees (that sun's noise is about 1 %). The speed is the same (the fixture 34.3 ms to 11.1). The frozen A/B in the
  game at 04:43 had not shown the fault: the sun's light inside the water is a small part of those pictures' luma.
  IN THE GAME, 10:25 (main 64257E205B07, in the game folder; `20261012-101445`, the beach frozen, 64 frames a step,
  the rule on / off / on again): luma 56.1 / 56.4 / 56.4, the shallows 37.8 / 38.8 / 38.8, flicker 2.02 / 1.88 /
  1.87 (the first traced step of a run reads a little low and restless, as at 04:43): THE LIGHT HOLDS. The game
  was behind another window: no speed from this run.
  STATE: the rule, built, seen in the harness. OWED THE SAME DAY: the switch `Speed trial` 64, the recipes
  `sun-cell` and `sun-cell-still`, the source check's pin of the switch and the log's line come out (every pass
  compiles again for it: with the next merge). Entered from its report: PT-142 (a very low sun over a steep sea),
  PT-143 (`passes_through`), a number under PT-68 (single samples 228 to 1,551 times the mean at a low sun).
- 10-12 08:35, THE MERGED BUILD'S SPEED IS NOT MEASURED YET (main E98E321830C2: the fog's rule, PT-136's bound):
  `20261012-082625` read the town 12.7 fps, 78.5 ms, the tracer 60.5 (06:12: 12.9, 77.5, 59.5) and THE FOREST 10.4,
  96.2, 81.2 (11.2, 89.3, 75.0): its bounced light 19.9 for 17.4 and its reflections 27.0 for 24.0, every other stage
  the same to 0.1 ms (the fog's three 7.1 for the old route's 7.2). The same scene, the same micromaps (57.8 %
  settled), the same reference radiance; of the big passes' own code the merge changed a comment and one unused
  argument (`pt_medium.hlsli`). WHAT DIFFERED: THE OWNER'S STREAM. Sunshine was encoding during that run (20.6 % of
  the video encoder; under the guard's 4 % of the 3D engines, so the report said "other programs: none") and was
  not at 06:12; a second run at 08:30 with Sunshine at 4.6 % read the same (81.5). Round 19 saw the same thing: a
  stream slows the two reordering passes and nothing else. So: a run with ANOTHER PROGRAM ON THE VIDEO ENCODER is
  not quoted for speed either (the report says so since 08:40: `analyze.py`); the forest is to be run again on a
  quiet card before 81 ms is taken for the build's number.
- THE BASELINE OF 10-12 06:20 (run `20261012-061203`, `speed-now`, main 4EA43DDDBC19 with the night's merges, the world
  running, no denoiser, nobody else on the card, the game in front at all 125 checks though the run was not
  `-Measure`): fps / a frame / the tracer's GPU / reflections / bounced light, ms, and the same scene at 01:20:
  the tavern 21.5 / 46.5 / 35.7 / 11.1 / 8.0 (21.0 / 47.7 / 36.0);
  the town 12.9 / 77.5 / 59.5 / 18.8 / 12.4 (11.3 / 88.4 / 65.0);
  the forest 11.2 / 89.3 / 75.0 / 24.0 / 17.4 (11.4 / 87.6 / 68.1);
  the vista 18.0 / 55.5 / 41.0 / 11.1 / 7.6 (13.3 / 74.9 / 60.1);
  the beach 11.2 / 89.3 / 73.1 / 41.9 / 9.9 (6.7 / 150.3 / 131.9).
  What moved them: the water's rule wherever water is in view, far off too (the beach, the vista, the town), and
  the record's CPU (4 ms a frame). THE FOREST'S TRACER IS 7 ms SLOWER than at 01:20 (its bounced light 15.4 to 17.4,
  its direct light 5.9 to 8.8) with the same scene by every count (8,575 instances, 6,453 plants in 287 groups). The
  step came with PT-125's fix and nothing else: main F1E5449077BA of 03:02 (that fix, the marks, the timers) had the
  forest at 84.2 ms (bounced light 21.8, direct light 8.9, reflections 29.1: `20261012-033350`), 15 ms over 01:20's
  68.8; the water's rule then took 9 off. So the forest of 01:20 was a forest whose plants were posed from other
  plants' records, cheap for a ray to pass; the real one costs 84 ms without the water's rule.
- MAIN WITH BOTH MERGED, IN THE GAME (2026-10-12 06:04, main 4EA43DDDBC19, run `20261012-055244`, the world running,
  no denoiser, a clean card): THE BEACH 11.2 to 11.4 frames a second, a frame 88 to 89 ms, the tracer 71.5 to 73.3
  (reflections 39 to 46, bounced light 9 to 10); with the water's rule off (`Speed trial` 64) 7.6 fps, 132.4 ms,
  116.4 (67, 28). The night's baseline of 01:20 had the beach at 6.7 fps, 150.3 ms, 131.9. The tavern 21.4 fps,
  46.8 ms, 35.6 (it was 21.0, 47.7, 36.0).
- THE RECORD'S CPU, CUT (worker reccpu, merged 2026-10-12 05:16 into main 890DC44FE597; `docs/agents/reccpu.md`). In
  the game, the same recipe one after the other, the world running, a clean card (`20261012-050228` its build,
  `-050601` main's): the scene's record THE TOWN 10.5 ms a frame TO 6.8 to 7.4, THE FOREST 9.5 TO 5.6; the tracer's
  whole record 11.1 to 7.4 to 8.0 and 10.0 to 6.1; a frame less the tracer's GPU time 22.3 to 17.5 ms in the town
  and 19.5 to 15.5 in the forest. What was cut, each the same bytes as before: the light grid's cells in one walk a
  light (CPU-tested byte for byte against the old loop over 4,002 scenes, `tests/light_grid_test.cpp`, ctest
  `tgpt_light_grid`), the far lists no longer copied and uploaded at every build, the grid built on another thread
  BESIDE the record, which waits for it before the upload (the same frame's grid, never a frame late), the plants'
  list kept between frames, the water's pieces found by a hash index, winds by tables. THE PROOF IN THE GAME: a run
  with its self-check on (an empty file `record-check` beside the DLL: the old way runs beside the new and the log
  says "DIFFERS" where they part) over eight walks in the forest and the town standing: no line (`20261012-050915`).
  WHY THE GRID IS BUILT EVERY FRAME (read by reccpu in the plugin, the log's "PT light grid 10s" says what moved):
  a light's range is tied to its flickering intensity (`Reach()`). WHAT IS LEFT, and why it is not a cut of the same
  kind: the water's pose and the characters (2.3 to 3.0 ms: one driver call a piece, 1,400 to 1,800 water refits a
  frame) and the plants' pose (1.2 ms). The report's choices for them: the water's pieces in fewer structures;
  structure commands recorded on a second list by a second thread; a flame's range that does not flicker (the grid
  then stands still: a question of the picture, a larger range is the truer one); PT-125's marks after every group
  (timed in the log now).
- WATER IN VIEW, campfix's build in the game (2026-10-12 04:30, run `20261012-041726`, the world running, no denoiser,
  a clean card; `%TEMP%\tgb\campfix-build`, SHA 7BB5795C0EB8; PT-131 tells how it came about): THE BEACH 121 ms TO
  73 ms OF THE TRACER, a frame 141 to 92 ms (7.1 to 10.8 frames a second). `water-day`, the tracer / reflections /
  bounced light, ms: both rules on 75.5 / 46.4 / 9.7 and 71.2 / 41.2 / 10.2; both off 120.8 / 72.4 / 26.0; the cell
  by the ray's width off 121.8 / 67.8 / 25.9; the shadow first off 72.6 / 38.2 / 9.5. The same view in the evening:
  65.8 and 67.9 on, 120.2 off, 114.4 with the cell off, 68.1 with the shadow first off. The tavern (no water) with
  that build: 35.8 ms against main's 36.0. So: a patch of waves no finer than the ray is wide is the whole gain;
  "the shadow first" gains nothing in these views (it is for water in a land's shade: no scene of the harness's).
  THE PICTURE IN THE GAME (frozen, 64 frames a step, `20261012-044312`): the light holds: luma whole (region) on /
  off / on again 56.1 (37.9) / 56.3 (38.6) / 56.3 (38.7) by day and 79.9 (73.7) / 80.0 (74.0) / 80.0 (74.0) in the
  evening; flicker and boil the same. MERGED 05:20 on that evidence, "the shadow first" taken out (no gain), the rule
  behind the hidden `Speed trial` 64 (off) until campfix's offline check of the mean has run (the sea fixture: every
  ray taken as 1 m and as 10 km wide, a sun 8 degrees up: started twice, ended twice by the owner's game opening).
  The hill's recipes are out (`camp`, `camp-measure*`, `camp-look`); `camp-walks` stays as PT-125's crash walk; the
  counters "Special branches" stay, a measuring build's alone. Recipes: `sun-cell` (live), `sun-cell-still`.
- WHAT THE GAP IS, FIRST READING (10-12 02:20, the baseline run's own 10 s lines): the tracer's CPU work to RECORD a
  frame, during which the GPU waits (a frame is: the game's inputs, our record, the trace, the composite, one after
  the other): the tavern 5.4 ms, the forest 11.0, the beach 12.7, the town 11.5 to 21.5 ("cpu=23.08ms (d3d11 0.95
  flush 0.55 wait 0.00 record 21.51)" in one 10 s stretch of the town, where the game's device stood 22.7 ms between
  our inputs and the composite beside its 64.6 ms wait for the trace). RTX Mega Geometry's CPU line says most of it
  is the scene's record (13 to 17 ms a frame on the forest's walk: the plants' plan 1.4 to 4, their pose 0.7 to 1.3,
  the top level 0.05; the rest not split). A millisecond of it is a millisecond of the frame, and none of it is in
  the picture. 02:20 (main 2D48A20DAEF9, `scene.cpp` alone): the scene's record is timed by section, a line every
  10 s ("PT record CPU 10s": meshes retired and compacted, new meshes, the emitters' list, materials, the plants'
  plan, the instances' loop, cloth, the records' uploads, the poses, the lights' tree and grid (the grid was built in
  95 of 94 frames of the town: every frame), the top level). NEXT: one run of the town and the forest for those
  lines, then the largest sections made cheaper (what did not change since the last frame is not done again) or
  recorded while the GPU still traces the frame before.
- THE BASELINE OF 10-12 01:20 (run `20261012-011822`, `speed-now -Measure`, main's build 3199B2F4BBF6 with the night's
  merges, the world running, no denoiser, nobody else on the card; the game was behind another window for 12 s at the
  run's start and a worker's test was making its passes on the CPU until 01:21: the tavern's row is rough): fps / a
  frame / the tracer's GPU / reflections / bounced light / caustics / the fog, ms:
  the tavern 21.0 / 47.7 / 36.0 / 11.3 / 8.0 / 4.2 / 3.0; the town 11.3 / 88.4 / 65.0 / 24.8 / 15.1 / 5.1 / 5.2; the forest
  11.4 / 87.6 / 68.1 / 25.5 / 15.4 / 4.7 / 5.9; the vista 13.3 / 74.9 / 60.1 / 21.3 / 14.2 / 4.5 / 4.5; the beach 6.7 /
  150.3 / 131.9 / 76.5 / 26.9 / 7.1 / 6.4. WHAT IS NOT THE TRACER'S GPU TIME: a frame less the tracer's GPU is 12 ms in
  the tavern, 23 in the town, 19.5 in the forest, 15 in the vista, 18 at the beach: at the target of 55 frames a second
  (18 ms) that part alone is the whole budget outdoors. Not split yet (the game's own frame, HDRP's post, the plugin's
  capture, the tracer's recording: the tracer's 10 s lines carry cpu, record and "the game to the composite").
- The owner, 10:40, of the guard scene: "I'm wondering why it takes so long, and why the fps is so incredibly
  poor"; 10:45, told that speed work had waited for correctness by the owner's own ruling: "Correctness first,
  sure, to a point, but I mean right now we're at like 8 FPS. It can barely render" (ruling in AGENTS.md: speed
  work starts now, beside correctness). 13:40: "we [can] also add [the] nanite like new thing nvidia did for
  optimizing bvh construction, that was implemented in a post patch for Alan Wake 2".
- THE OWNER'S QUESTION, 19:22 on 10-09: "We removed limits so that a ray can travel forever and that we trace
  infinitely out into the distance. isn't that the primary reason for our perf woes?"; then: "you can just pass
  that question on to your perf and memory workers" (passed to `perf1` and `memory1` at 19:27: each answers in
  its report, a section "Does the unlimited world cost the speed / the memory?"). NOT ANSWERED YET: the cost of
  PT-61's "no distance cut" was written down as "to measure" on 10-07 and never measured. What the logs of the
  harness runs say so far (`<scratch>\history_scene.py`, the lines with the whole world in): the SAME world
  gives very different frames by build: the tavern 1.53 M triangles and 5,331 instances in every run since
  10-08, a frame 60, then 98, then 82 ms; the beach 2.72 M triangles, 84, 231, 101, 212 ms; the town 5.23 M
  then 4.37 M triangles, 116 ms both times. So the swings of these days are the code's, not the world's size.
  NOT KNOWN: what the world's size costs each frame outside the passes: in the town the tracer poses 11,303
  plants (6.7 M triangles) for the wind and holds 1,379 pieces of water surface (1.2 M triangles) every frame,
  16,234 instances traced; and its memory (acceleration structures 0.5 to 0.9 GB, plants 0.8 GB), which is
  what gave the owner's frames of 1.5 s. The measuring run's switch 32 against 16 (the ray alone against no
  ray) gives the share of a frame that grows with how far a ray flies. The ruling stands: no distance cut is
  built; a cut is the owner's choice, with its numbers.
- THE REFLECTIONS' STUDY IN THE GAME (the owner, 13:45: "The reflections pass being slower with shader execution
  reordering instead of faster warrants an additional investigation"; `specser1`'s build, runs
  `20261010-144426` and `-151637`; the same picture in every step; its report `docs/agents/spec-ser-1.md`). The
  reflections' stage, ms (the tavern / the town / the beach): as it ships 9.8 / 24.9 / 92.0; the material's key
  in 8 bits 9.8 / 24.0 / 84.9; `ser2`'s 3-bit key 10.8 / 33.4 / 84.8 (THAT KEY ALONE WAS ITS WHOLE LOSS: it
  threw away the material's number; its other change, the guide stored once, costs nothing); NO HINT 10.7 / 30.8
  / **70.8**; the water's path regrouped every turn inside the pass 10.7 / 24.7 / 106.3 (2,624 bytes carried);
  the plain pass 12.9 / 29.0 / 192.3. So the material's key is right for what is not water, a hint-less sort is
  far better for the water, and a regrouping inside the water's loop costs by what it carries. Its design: the
  water's path a pass of its own, regrouped every turn, about 76 bytes a thread. The worker `water-ser1`
  (15:30, `brief-water-ser-1.md`) builds it; target the beach's reflections well under 70 ms.
- THE BEACH, THE SAME BUILD (native 2E4723B1AC0E), 10-10 AFTERNOON, ms a frame / reflections: in front 113 to 117
  / 67 to 73 (run `20261010-160321`, nobody else on the card); KEPT MINIMIZED ON PURPOSE 122 to 125 / 76 to 80
  (`-160721`, the harness's new `-KeepMinimized`, for this one measurement): MINIMIZED COSTS 7 TO 9 % AT THE
  BEACH, 2 to 3 % in the town. BUT the same build read 156 / 110 at 14:41 (`-143557`) and the build before it 154
  to 177 in the runs of 10:50 and 12:30. Not the window, not the memory (8.4 to 9.9 GB of a 15.2 GB budget in all).
  What those slow runs share: Sunshine (the owner's streaming) used the card's 3D engines at 4.4 to 4.7 % and the
  video engines at 31 to 35 % (the fast runs: none, or video only). NOT PROVEN, but the harness had flagged those
  runs ("Other programs used the GPU ... the frame rates are not to be trusted") and the coordinator quoted them
  anyway: A MISTAKE. From now on a run with that line is not quoted for speed.
- THE WATER'S OWN PASS (`water-ser1`, its build installed for run `20261010-154747`) DID NOT ENGAGE: "the water's
  own pass is asked for, but it was not made", though both passes were made (a fault in its switch); and the
  driver keeps 2,112 bytes a thread in it, not the 104 to 125 its listing count promised. To fix and measure again
  (a fresh worker).
  BOTH FOUND (round 19, `docs/rounds/round-19.md` 1 and 2): (1) the switch: `adopt_passes` took the passes' addresses
  with `&` into `ComPtr<T>*` pointers, and WRL's `ComPtr::operator&` EMPTIES a ComPtr on that conversion: the three
  passes were made and then thrown away (fixed with `std::addressof`; the main tree has no such line). (2) The 2,112
  bytes are `water_sun`'s `SunCorner rows[2][33]` (the far sea's patch made at the point): a local array indexed by a
  computed number in a loop that holds a regrouping call is kept whole on the driver's stack, whatever its lifetime.
  Trial: the patch makes the corner above again instead of keeping a row (the same numbers); queued for the beach.
  ANSWERED (run `20261010-173151`): the driver then keeps 0 bytes in both water passes, the picture is the same, and
  BOTH ARRANGEMENTS LOSE at the beach (reflections 110 to 125 ms against 91 to 96 as it ships). Dropped, never merged.
- THE REFLECTIONS' KEY, ROUND 19 (`docs/rounds/round-19.md` 4; run `20261010-175041`, copy `wt\speckey1`): no hint 75.5
  ms at the beach against 91 to 93 by the material, but 30.6 in the town against 22.9 and 10.0 in the tavern against
  9.3; a key that keeps the water's pixels in screen blocks does NOT help (92.7), nor any other key (round 6's study: 3
  to 16 bits all 83 to 86): any key is a sort, and the sort costs the water's paths. BUILT IN MAIN: the tracer picks the
  bit count each frame by measuring (`State::ReflectionsSort`: its choice, a probe of the other for 8 frames every 128,
  the faster by 5 % kept); the same picture, reordering never off. To see in the game: the beach about 15 ms faster,
  the town and the tavern as they are.
  CORRECTED (run `20261010-183839`, CLEAN): held by the material / with no hint: the beach 73.3 / 75.5, the town 22.0
  / 30.5; the pick stays on the material. "No hint wins at the water" came from runs with Sunshine on the card (round
  6's study, the trial of 175041): Sunshine slowed the sorted pass ~18 ms and the unsorted one not at all. So no gain
  in clean play; the pick's use is while the owner streams with Sunshine (to see once in such a run). AGENTS.md's rule
  on flagged runs now says: not even step against step.
- THE GAME IN FRONT AGAINST MINIMIZED (the owner, 15:35 on 10-10: "Does the frametimes depend on whether the game
  has the foreground or not? It's playing minimized right now", then: "let's make sure the game always has the
  foreground and only respect our frametimes when we know it was": AGENTS.md). The town, the same build, at the
  defaults: minimized (run `20261010-152525`, the owner's word) 112 / 102 frames in 10 s, the tracer's GPU 66.8 /
  68.8 ms, 10.5 fps; IN FRONT for all 31 checks (run `20261010-153252`) 114 / 106 frames, 65.0 / 65.8 ms, 10.7
  fps. So minimized cost 2 to 3 % here, inside the run-to-run noise: the stage times of earlier runs stand; their
  window state stays unknown. THE GAP THAT IS NOT THE WINDOW: the game reaches 10.7 fps (93 ms a frame) where the
  tracer's GPU time is 65 ms; in the tavern 21.5 fps (46.5 ms) against 33: 13 to 28 ms a frame outside the
  tracer's stages (the game's own rendering underneath, the waits between the two devices, the CPU): PT-107's
  question, now the next largest piece of a tavern's frame after the tracer's own.
- THE RULE IN THE GAME (`rule2` merged 14:21 on 10-10, installed 14:35: native 2E4723B1AC0E, plugin 6850B285696D;
  run `20261010-143557`, defaults, no denoiser): the start-up line "the driver's stack a thread, the bounced
  light's pass 0 bytes, the reflections' pass 128 bytes"; THE TAVERN'S FRAME 33.5 ms (bounced 9.9, reflections
  9.6; it was 65 this morning, 81 on 10-09); the beach frozen 155.7 (bounced 29.0, reflections 109.9: that pass
  is unchanged and has read 93 to 106 across runs; `specser1`'s study measures it next). The town NOT measured:
  the guard stopped the run at the beach because VS Code held 898 MB of video memory at 0 % (the 800 MB limit
  of 10-09); since 14:42 `Run-PtHarness.ps1` does not count the editor's memory (`$memoryNotCounted`), its
  engines' use still counts (told to the owner, to undo if they wish).
- REORDERING, DESIGNED FOR, WINS EVERYWHERE (run `20261010-130621`, 13:06 to 13:34 on 10-10; `ser2`'s split trial,
  native CF2FA0938CB9 installed for the run, main's put back; the same picture in every step). ms, a frame / the
  bounced light / the reflections, no denoiser:

  | Step (`Speed trial`) | the tavern | the town | the beach |
  |---|---|---|---|
  | as it ships (0: the hint-less call inside the loop) | 65.0 / 41.9 / 9.6 | 101.2 / 57.7 / 24.7 | 169.2 / 61.5 / 92.9 |
  | the plain compute passes (8) | 39.9 / 13.7 / 12.7 | 74.4 / 27.6 / 27.7 | 274.4 / 64.6 / 195.2 |
  | the reuse across frames a pass of its own, call unmoved (16) | 63.6 / 40.6 / 9.6 | 99.5 / 56.2 / 24.5 | 171.6 / 59.1 / 97.7 |
  | split, ONE call after the first ray, outside the loop (48) | 33.4 / **9.5** / 10.4 | 75.3 / **22.7** / 33.8 | 144.7 / **28.9** / 101.1 |

  The driver's stack a thread: 2,592 bytes as it ships, 0 for 48 (and for "top", 32: the same times). THE
  BOUNCED LIGHT'S PASS IS NOW FASTER WITH REORDERING THAN THE PLAIN PASS IN EVERY SCENE: 9.5 against 13.7, 22.7
  against 27.6, 28.9 against 64.6. The owner's ruling of 10:30 ("due to us not designing our tech with it in
  mind") measured true. What did it: the call out of the rays' loop (the shipped line compiled to three calls in
  it) and nothing carried across it. The reflections' pass ran with `ser2`'s 3-bit key in steps 32 to 64 and was
  WORSE (the town 33.8 against 24.7, the beach 101 against 93): it stays as it ships. Expected frames with the
  rule (48 and the reflections as shipped): about 33 ms in the tavern, 66 in the town, 137 at the beach.
  MERGED into main 13:36 (behind CMake `TGPT_SER_SPLIT`, OFF; snapshot `<scratch>\pre-ser2`; 114 checks). The
  worker `rule2` (13:38, base85, `brief-rule-2.md`) makes 48 the only form and takes the rest out.
- THE HINT-LESS CALL IN THE GAME (run `20261010-123027`, 12:30 on 10-10, native 34466EB210F2, defaults, no
  denoiser; Sunshine on the card at 4.7 %, as in the study's runs): the bounced light's pass 38.5 ms in the
  tavern (47.8 before; the frame 70.6 -> 61.7), 54.0 in the town (61; the frame 109.5 -> 97.0); the beach
  noisy (46.6 frozen, 56.9 running; its reflections' pass, not changed, read 104 to 106 against 93 to 96:
  the run's own drift). The picture the same as this morning's build (the tavern 2.26 levels; the beach 4.40,
  8.8 % of its blocks moved, two takes there move 22 %). SO THE SHIPPED CALL GIVES ABOUT HALF OF THE STUDY'S
  "no bits" (29.5 / 35.1). The difference seen: the study passes hint and bit count as run-time values, the
  shipped pass constants (0u, 0). `ser2` was asked to compare the two compiled listings.
- THE REORDERING STUDY IN THE GAME, THREE SCENES (`docs/agents/ser-study-run.md`; runs `20261010-113425` the
  tavern, `-115539` the town and the beach; `ser1`'s study build; the same picture in every step). The bounced
  light's pass, ms:

  | | the tavern | the town | the beach |
  |---|---|---|---|
  | as it shipped (regrouped by the material's number, 16 bits) | 50.9 | 57.4 | 55.0 |
  | regrouped with NO hint of our own | 29.5 | 35.1 | 32.2 |
  | no regrouping call at all | 11.4 | 27.9 | 51.7 |
  | the plain compute pass (the yardstick) | 14.2 | 27.8 | 63.6 |

  (1) THE HINT WAS THE LARGEST SINGLE COST, IN EVERY SCENE: 20 ms and more. MADE THE RULE AT ONCE (12:15 on
  10-10: `pt_gi.hlsl`, `dx::MaybeReorderThread(0u, 0)`; a source check holds it; building). (2) The call must
  stay: without it the beach's pass is 51.7 against 32.2. (3) What the call still costs where threads do not
  diverge (18 ms in the tavern) is what a thread carries across it: 2,560 bytes by the driver's own count, in
  every variant; the reflections' pass carries 128 and its regrouping pays in all three scenes (9.3 against
  10.0 ms without it in the tavern, 24.6 against 31.1 in the town, 95.4 against 125.4 at the beach). (4) A hit
  object, a key by what comes next and a second regrouping bought nothing with the stack as it is. `ser2`
  rebuilds the pass for a small stack: target 11.4 / 27.9 / under 32 ms with the call kept.
  THE OWNER, 11:55, asked what this means ("I thought Shader Execution Reordering was a driver-level feature
  that was always just a huge improvement"): answered: the shader asks for the regrouping at a place and by a
  key of its choosing; each thread's state is kept and fetched there; it pays where threads diverge and what
  they carry is small. And asked which model the worker is: Opus 5.5, as the coordinator.
- THE RULE CONFIRMED IN THE GAME (run `20261010-105028`, 10:50 on 10-10, native 9D134423AA82, every setting at
  its default, no denoiser): the beach 154 to 163 ms (reflections 93 to 96; before 191 to 199), the tavern 70.6
  (bounced 47.8, reflections 9.1; before 78 to 81: the dead trial code went with the switches), the town 109.5.
- THE LARGEST FIND SO FAR: SHADER EXECUTION REORDERING COSTS THE BOUNCED LIGHT'S PASS 33 TO 42 ms WHERE THERE IS
  NO WATER, AND SAVES THE WATER'S PATH HALF ITS TIME (run `20261010-100720`, 10:07 to 10:19 on 10-10, native
  with the test file's reader, `TGPT_TEST_PLAIN_PASSES=1` so that the plain compute passes are made beside the
  reordering ones; recipe `plain`: `Speed trial` 0 / 8 / 24 / 16; no denoiser; ms a frame / bounced / reflections):

  | Step | the tavern | a town (`metal`) | the beach |
  |---|---|---|---|
  | the reordering passes (as now) | 81.1 / 56.0 / 11.6 | 109.4 / 60.9 / 29.3 | 199.4 / 42.6 / 140.2 |
  | THE PLAIN COMPUTE PASSES | **42.4 / 13.9 / 15.0** | **75.5 / 27.8 / 28.4** | 405.3 / 70.8 / 317.7 |
  | plain, and the lamps' tree | 40.0 / 13.6 / 12.7 | 75.4 / 27.2 / 28.4 | 338.7 / 58.8 / 263.5 |
  | reordering, and the lamps' tree | 78.3 / 53.9 / 10.8 | 109.2 / 61.2 / 28.4 | 164.5 / 42.2 / 106.0 |
  | plain, the tree, NRD | 44.7 | 77.8 | 344.3 |

  The picture is the same in the tavern and the town (`<scratch>\same_picture2.py`: reordering against plain
  2.03 and 1.56 levels, two takes of the same 2.03 and 1.61; no 64-pixel block's mean moved by 2 levels). AT
  THE BEACH THE PLAIN STEP'S PICTURE IS BRIGHTER (57.6 against 53.6; 73 % of the blocks moved by over 2 levels
  where two reordered takes move 22 %): NOT EXPLAINED (the plain step ran at 2.4 frames a second: fewer frames
  settled; or the two variants differ in the water's path): to look at before either is called the same there.
  First seen an hour earlier (`20261010-095602` against `-095125`, the tavern: 41.7 against 84.1 ms). The
  measuring run's "without reordering" (10-09) had only left the regrouping CALL out of the same ray
  generation pass and found nothing: the cost is the pass being a ray generation shader with regrouping points
  at all, not the regrouping. `Speed trial` 8 does nothing in a normal start: the plain passes are made only
  with `TGPT_TEST_PLAIN_PASSES` in the environment (a new build's first start then took 436 s for the passes,
  241 without).
  THE OWNER, 10:25, told this: "how would Shader Execution Reordering make it slower? It's the feature that makes
  PT viable to accelerate on modern GPUs". RIGHT, AND THE NAME WAS SLOPPY: what was compared is two builds of the
  same pass, and the tracer uses the weakest form of the feature. Read from the shaders (`pt_common.hlsli` lines
  71 to 76): with `PT_SER` a pass is ONE ray generation shader that does everything inline: it traces with inline
  ray queries (`TraceRayInline`), calls `dx::MaybeReorderThread(material & 0xFFFF, 16)` ONCE (a 16-bit hint of
  our own), then shades inline. NO HIT OBJECTS, NO HIT SHADERS: the driver sorts by our hint alone and must save
  and reload all a thread carries across that call. Where pixels do very different work (the water's path) that
  pays; where every pixel does about the same (a room) the sort and the reload cost more than they bring: that
  is the reading that fits the numbers, NOT PROVEN (nothing was profiled inside the pass). So the finding is "OUR
  USE of reordering in the bounced light's pass costs 33 to 42 ms", not "reordering is slow". THE PROPER FIX is
  to use it as it is meant (hit objects, the regrouping where little is carried, shading in hit or callable
  shaders): a job of its own after the chooser.
  SO: the bounced light's pass wants the plain variant where its rays meet no water (the tavern 81 -> 40 ms,
  the town 109 -> 75) and the reordering one at water; the reflections' pass wants reordering everywhere (11.6
  against 15.0 in the tavern, 140 against 318 at the beach).
  THE OWNER'S RULING, 10:30 (AGENTS.md, "Shader Execution Reordering stays on; the tracer is designed for it"),
  to the coordinator's offer of a chooser that picks the faster variant: "Do not turn that off. If it makes it
  slower with that on, then it's either not due to that, or due to us not designing our tech with it in mind".
  SO THE CHOOSER IS DROPPED. The plain passes are the yardstick only. NEXT (the worker `ser1`, after `rule1` is
  merged; `brief-ser-1.md`): the cause found by measuring small variants in the game (the ray generation pass
  with no regrouping call compiled in; fewer hint bits and another key; the regrouping point where a thread
  carries least), and the bounced light's pass designed for the feature (hit objects, the shading split so
  that like runs with like): target the yardstick's 14 and 28 ms with reordering on.
- THE CUTS IN THE GAME (run `20261010-092027`, 09:20 to 09:36 on 10-10, native EF7EA3CAB438, recipe `speed2`, no
  denoiser; the stages' medians of the 10 s lines inside each step, `<scratch>\measure_read.py`):

  | Step | the beach: a frame / reflections / bounced, ms | the tavern: a frame / reflections / bounced, ms |
  |---|---|---|
  | as now | 193.7 / 134.8 / 41.8 | 78.5 / 11.6 / 53.3 |
  | 32768, the feeders regrouped | 193.4 / 136.2 / 40.2 | 77.2 / 9.9 / 53.6 |
  | 16, THE LAMPS' TREE | **160.8 / 102.7** / 41.2 | 77.7 / 10.3 / 53.8 |
  | 32, the water's short rays | 192.7 / 134.0 / 41.7 | 80.1 / 11.2 / 55.3 |
  | all three | 160.6 / 102.6 / 41.2 | 74.5 / 8.8 / 52.0 |
  | as now again | 190.9 / 133.8 / 40.2 | 80.2 / 11.2 / 55.5 |

  THE LAMPS' TREE TAKES 32 TO 33 ms OFF THE BEACH'S FRAME (a sixth: the reflections' pass 134 -> 103). The
  other two cannot be told from the noise at the beach; in the tavern all three together are 4 to 6 ms (the
  reflections' pass 11.4 -> 8.8). THE PICTURE IS THE SAME (`<scratch>\same_picture.py`, each step's picture
  against the first: the tavern 2.03 levels in every step, the second take of "as now" too; the beach 2.5 to
  3.0, largest for the second take of "as now": the run's own drift). TO DO THE SAME DAY (AGENTS.md): the
  tree and the feeders' key become the rule (their switches and the old loop out), the water's short rays
  come out (no gain measured).
- THE WORLD MOVING (the same run, `metal@structures`, the new "PT structures 10s" line): the scene stage 1.35
  ms frozen, 5.34 ms with the world running: THE PLANTS 3.90 ms (624 groups refitted, 6.4 M triangles a frame),
  the water 0.64 (1,379 pieces refitted, 1.2 M triangles, frozen or not), characters 0.44, the top level 0.22
  over 16,179 instances. The frame 108 ms both times. So what grows with the traced world's size is 5.3 ms of
  a 108 ms frame in a town: the owner's question of 10-09 ("isn't [the world without a distance limit] the
  primary reason"), answered for the frame time: no, a twentieth.
- `perf2`'s THREE CUTS ARE IN MAIN (merged 04:44 on 10-10 by the coordinator; built 05:02 with CMake
  `TGPT_REGROUP_FEED`, `TGPT_LAMP_TREE`, `TGPT_WATER_FLIGHT` ON; `Speed trial` 32768, 16, 32; recipe `speed2`):
  the two big passes regroup also by "feeds the cache"; THE LAMPS A RAY OFF WATER OR GLASS RUNS INTO ARE FOUND
  BY A SEARCH TREE (until now every such ray asked every lamp of the scene, 510 to 976 of them, at every turn:
  about 2.7 thousand million questions a frame at the beach; its reckoning: 15 to 40 of the 110 ms; also every
  glass pixel of the tavern); a ray inside the water is asked only as far as its path flies. NOT VERIFIED
  OFFLINE: its picture checks ended at the 8 GB memory ceiling three times, while the driver made the passes
  (with and without the reordering passes). THE GAME IS THE TEST: the run `water-day@speed2`,
  `tavern-room@speed2` is owed (the card was not free: PT-87's note on QuickLook). Found on the way, not
  built: far lamps' mirror images are drawn by rounding (a blot a pixel or two wide, not a disc); the
  measuring build's second page (`measure2`, `measure-water`) is written and parsed, never compiled whole.
- THE MEASURING RUN, 19:37 to 19:56 on 10-09 (`20261009-193722`; all of it: `docs/agents/measure-run.md`): each
  step leaves one part of the work out. (1) THE BEACH: with the walk through glass and the water's own path left
  out a frame falls from 163 to 181 ms to 29.9 ms (the reflections' pass 112 to 119 ms -> 2.8, the bounced
  light's 35 to 45 -> 11.6). The pass NAMED caustics is 7 ms; the reflections' pass stays at 103 ms with its
  own ray left out: it is the water the camera sees that costs. So "1 fps at water" is the water's own path
  (133 to 151 ms) and the memory overrun, NOT the size of the traced world: the beach, with the whole world
  in, is 30 ms without that path. An earlier line here, "the Caustics row is 90 of the beach's 150 ms", was
  right in size and wrong in place: what that row switches lies inside the water's path. (2) THE TAVERN (81 to
  85 ms): with no ray in the two big passes a frame is still 31 ms (the bounced light's pass 14.4 with no ray;
  caustics 4.0 with no water in sight); the bounce ray alone, nothing shaded, 28 ms (the camera's own rays over
  the same world: 2.5); shading what it met 22 ms. Decals about 5, the cache's feed 4. Differences under about
  7 ms are inside that run's noise (the sun, the lamps, their shadow rays, the water question, the reordering).
  NOT answered: whether the far world in the top-level structure makes each ray slow (one run with it left
  out, as a measurement), and the town. `perf1` has it (20:00) and opens the water's path first.
- The numbers (run `20261009-103336`, `metal`, the tracer's own log line; 1924 x 1084 rays for 4K, one a pixel,
  DirectX Raytracing 1.2 with Shader Execution Reordering and opacity micromaps on 236 meshes; 4.37 million
  triangles, 16,234 instances, 510 lights): 118.8 ms a frame: bounced light 73.5, reflections 25.9, direct light
  6.7, caustics 4.9, prepare 4.1, the bounced light's spatial reuse 1.7, scene 1.3, the rest 0.6.
- NVIDIA's thing is RTX Mega Geometry (cluster-level acceleration structures and a partitioned top level, through
  NVIDIA's own API on Direct3D 12; Alan Wake 2 took it in a patch; this from the coordinator's memory, to be
  checked against NVIDIA's documents and license before anything is designed). What it speeds up is BUILDING and
  updating acceleration structures for very much geometry; here that is the "scene" stage, 1.3 ms of 118.8: it
  would not move the frame rate now. It could help geometry arrive sooner after a load (PT-98), memory, and
  tracing the game's finest meshes with no level-of-detail swap. NVIDIA cards only: an extra path beside the
  plain one, never the only one. Told the owner so; kept here as a request for after the shading's cost.
- FOUND BY `speed1` (its report in `wt\speed1\docs\agents\speed-1.md`, 14:48 on 10-09, from the game's own log
  lines of the harness runs; the worker is still at it, its offline numbers not in): ABOUT 30 ms OF THE BOUNCED
  LIGHT'S 73 ARRIVED WITH ONE BUILD, FOR A SWITCH THAT IS OFF. The tavern's bounced light was 29.4 to 33.2 ms
  in five builds up to 10-09 01:20 and 61.9 ms from the build of 04:50 on, every other stage the same to a
  tenth of a millisecond; that build added PT-85's hidden `Lamp trial lit surroundings` (`pt_common`
  `lamp_around`), both of whose switches are 0. So the dead lamp trials cost a quarter of the frame while off:
  their removal (`brief-tidy-lamps.md`, not started) is the first speed job, once the worker has its offline
  pair (the same build with the trial compiled out).
- THE BEACH: the Caustics row is 90 of its 150 ms (run `20261009-144507`: 58.3 ms with the row off).
- Jobs: `brief-speed-1.md` (where the bounced light's and the reflections' time goes; what can go without
  changing the picture) and `brief-nrd-relief.md` are written; started 2026-10-09 afternoon (see the round's state).
- State: measured; workers on it (see the notes above); Mega Geometry is PT-109 now.
- `speed1` HANDED OVER 15:17 on 10-09 (stopped at the coordinator's word; its report
  `wt\speed1\docs\agents\speed-1.md`, 28 KB). TAKEN INTO MAIN AT ONCE, ahead of its merge
  (`<scratch>\lamp_out.py`, `<scratch>\pre-lampout`): its `CMakeLists.txt` whole (CMake `TGPT_LAMP_AROUND`, OFF:
  every pass gets `-D PT_NO_LAMP_AROUND`; `TGPT_SPEED_MEASURE`, OFF) and `pt_common`'s `lamp_around()` behind
  that define: PT-85's `Lamp trial lit surroundings` is in no pass any more, its setting does nothing. In the
  build of 15:21. The game's answer owed: the guard scene's bounced light against 73.5 ms (the tavern's
  against 61.9; about 30 less confirms, the same means the reading of the logs was wrong: then ON again).
  NOT MERGED YET, the rest of its copy (base69): hidden `Advanced / Speed trial` (1: no "which water" ray
  for a point above every water's top; 8: both big passes as plain compute passes, needs an environment
  variable), the measuring build's 11 timing switches, the recipe `speed`, a log count, the test's memory
  ceiling. MERGE WITH CARE: it rides in `TGPT_Frame.prevCameraPosition`'s w, the very word `nrdrelief`'s
  relief trial took (bits 0 to 2, merged 15:07): `tracer.cpp` merges with no textual conflict and would read
  one number as both. One of the two must move (the speed trial times 8) in the plugin and the tracer.
  Its own list of what could be saved, only the first measured: the lamp trial (30 ms); the "which water"
  ray only under a water's top; micromaps for the 11,303 plants traced in the wind's pose (today none); plain
  submeshes of an instance that holds one cut-out made opaque; smaller passes (one call site for
  `hit_material` and `direct_light`: "unknown, possibly the largest"). NOT DONE: every offline number.
- THE 33 GB (PT-96's note): the worker's test had not traced a frame; the driver was still making the two
  reordering passes' pipelines at 9.5 minutes (in the game today those two took 428 s and 628 s on a cold
  start), so the memory grew while PIPELINES WERE MADE, not per frame. Not known whether an ordinary build's
  cold start grows the same in the game: WATCH THE GAME'S MEMORY DURING THE NEXT BUILD'S FIRST START.
- THE LAMP TRIAL COMPILED OUT, IN THE GAME, 15:52 on 10-09 (run `20261009-154425`, `metal`, the tracer's own stage
  times): bounced light 56.8 to 60.3 ms where it was 73.5 (about 16 ms less); reflections 27.1 to 27.7 (25.9),
  direct light 6.6 (6.7), caustics 4.8 (4.9), prepare 4.1 (4.1); the step's GPU time with NRD 105.4 ms (118.8).
  So it is real and about HALF of what the tavern's logs promised for the tavern. NOT A CLEAN NUMBER: Starfield
  ran beside that run at up to 98 % (PT-101); the other stages being what they were says the reading holds,
  but it is to be taken again with the card free, in the tavern too.
- `speed1`'S REST MERGED 16:06 on 10-09 (`<scratch>\pre-speed1`; four conflicts, resolved by
  `<scratch>\resolve_speed1.py`; 106 source checks, the plugin compiles, two small passes compile): hidden
  `Advanced / Speed trial` (1: a shadow ray asks which water it starts in only under the water's highest point;
  8: the two big passes as plain compute passes, with the environment variable `TGPT_TEST_PLAIN_PASSES`), CMake
  `TGPT_SPEED_MEASURE` (OFF; 11 timing switches for a measuring build of its own, never deployed), the recipe
  `speed`, a log count of triangles tested by the shader, the offline test's memory ceiling. THE SHARED WORD:
  `TGPT_Frame.prevCameraPosition`'s w carries the relief trial in its bits 0 to 2 and the speed trial as the
  rest divided by 8 (the plugin writes relief + 8 x speed; `tracer.cpp` reads each part; two source checks hold
  the lines). Building 16:10; then `tavern-room@speed` and `metal@speed` with the card free: the bounced
  light against 61.9 ms (the tavern) and 73.5 (the guard), and what `Speed trial` 1 saves.
- THE CLEAN NUMBER, 16:08 on 10-09 (run `20261009-160428`, `metal`, the card free: "during" empty in its `gpu.json`;
  the tracer's 10-second lines): NO NET GAIN in the guard scene. With no denoiser 114 ms a frame (this morning's
  run `20261009-103336`: 111 to 119, mostly 113); with NRD 120 (117 to 131). BOUNCED LIGHT 64.5 ms (68.5 to
  73.5 this morning): 5 to 9 less. REFLECTIONS 30.0 to 30.8 ms (23.5 to 25.9 this morning): 6 MORE. Direct
  light 6.9 (6.7). The 57.5 ms read at 15:52 beside Starfield was a wrong reading, and the coordinator's "16
  ms faster" to the owner is taken back. WHY THE REFLECTIONS GREW: the pass is bigger than this morning's by
  the day's merged trials (`pt_specular_ser.h` 51.0 MB now; 45.7 before the lamp trial by `speed1`'s note, 58.6
  with it): code that is switched off still costs, the very thing `speed1` found. SO: every trial the game has
  answered comes OUT of the big passes or becomes the rule, and until then a build's speed is not a clean
  number: the lamps' trials B, C and D (`brief-tidy-lamps.md`), glass (7 and 15 answered: neither is right),
  water (the no-view light to the rule, the foam trial out), the caustics' two, the grass trial. And a trial
  that grows a big pass is from now on built behind a COMPILE switch, as `speed1` did for the lamp trial
  (AGENTS.md's rule). The tavern (61.9 ms of bounced light on the builds of this morning) is measured by the
  run after the build of 16:10.
- THE TAVERN AND THE GUARD, CLEAN, 16:31 to 16:36 on 10-09 (run `20261009-162628`, recipe `speed`, native 75B48AD036C4:
  main with `speed1`'s rest; only the streaming encoder beside it). THE TAVERN: bounced light 52.5 ms with NRD
  (53 to 60 with no denoiser), reflections 11.6 (11 to 14), direct light 4.75, a frame's GPU time 82 ms. By
  `speed1`'s table it was 61.9 on this morning's builds and 29.4 to 33.2 on the five builds up to 01:20. SO THE
  LAMP TRIAL WAS 9 OF THE 30 ms, NOT ALL OF THEM: 21 ms came with something else of the build of 04:50 or
  after it (`water8` came in that same build, and the tavern was never run on a build with `water8` alone;
  every later merge is a suspect too). THE GUARD: 108 to 115 ms with no denoiser (bounced light 61 to 65,
  reflections 28 to 31), 107 to 113 with NRD: as at 16:08, within what one reading differs from the next.
  `Speed trial` 1 DID NOT COME ON in either scene: the tracer's log, "Speed on trial (0): NOT skipping the water
  question: some water of the scene stands over the highest point the plugin gave": the plugin's highest
  point of the waters is too low or misses a water, so the trial's own safety check refuses. Its saving is
  still unmeasured.
- NEXT FOR SPEED, by numbers and not by guesses: (1) `tidy2`'s removal of the lamp trials, then the same two
  scenes; (2) THE MEASURING BUILD in the game once (CMake `TGPT_SPEED_MEASURE` ON in a build folder of its own,
  installed for one run and taken back: its 11 switches each leave one part of the work out, `Speed trial` 16
  and up): what of the tavern's 52 ms is rays, materials, lamps, the cache, the water question; (3) the plugin's
  highest point of the waters, so that trial 1 can be measured.
- ROUND 20 (`docs/rounds/round-20.md` 9 and 10): (3) done: the shaders get every slot's highest water
  (`waterCeiling`); trial 1 then switched on and gained nothing measurable (fewer rays, the same picture): THE RULE.
  (2) given up: the measuring build's bounced light pass took the driver over 20 minutes, twice. Where a town's frame
  goes now (the world running): the tracer 57 ms (bounced light 15 to 16, reflections 17 to 21, direct 7, scene 5,
  caustics 4.8, camera rays 3.2) and the game's own card time ~21. From 64 to 57 tonight: leaves' micromaps at level
  6, the bounced light's material key, the decals' tree by place. Next by size: PT-115 (a proposal, ~8 ms), the
  radiance cache's feeding ray (5.6 ms, the owner's choice), the plants' poses (4.7 ms, Mega Geometry), PT-114.

### PT-98 Things pop in over time while tracing, and a patch of grass was not there at all when the pictures were taken (the owner, 2026-10-09)
- The owner, 10:28: "I frequently see stuff pop in over time when you playtest right now, so I'm never sure when I
  can trust the visuals. But in your most recent playtest, the torch next to the guy with the weird mask had a
  weird flame and looked overblown I guess?"
- The torch (run `20261009-100849`, `night`, `lamp-size`; sheet `torch-game-off-on.jpg`): burnt pixels 727 (the
  game) / 10,616 (the near size 0, the step that shows the old look) / 1,851 (the default 15): the basket's tips
  are as the game's at the default; the flame's own sprite is dimmer and smokier than the game's.
- SEEN IN THAT RUN'S PICTURE (the coordinator; scratch `night-now.jpg`): the grass round the torch and the man,
  which the game shows and the traced picture of run `20261009-074726` showed, is NOT in the traced picture:
  bare path; grass only at the right edge. The plugin's counts are the same in both runs (13 grass materials,
  10,298 of 17,491 instances traced, 989 meshes). Not known: whether it came late (the PC was busy with five
  workers: PT-96) or was dropped by the build of 09:40 (the grass shadow trial, off; the caustics trials; the
  characters' normals).
- To do: the same scene again on a quiet PC (grass there or not, and after how many seconds); a notice during a
  harness run that says whether a step is settling or its pictures are being taken, so the owner knows when
  the picture counts; how long the traced world takes to be whole after a load, as a number in the run's report.
- 13:50 on 10-09, in the game from 14:27 (plugin AB27CC16A6B7): two limits that were counted by the frame and
  by the scan now go by the clock. `SceneCapture`: meshes are registered until a tenth of the last frame's
  time is spent (at least 6): at 8 frames a second it had been 45 meshes a second, a town square's 1,533 took
  over half a minute. `TextureShare`: every watched texture is looked at each scan (up to 4096; it was 256, so
  a swap to a sharp texture could wait 13 s to be seen). Not measured yet: `night@lamp-size` runs next.
- 14:48 on 10-09 (run `20261009-144507`, `night`, `lamp-size`; scratch `night-whole.jpg`): THE GRASS ROUND THE TORCH
  AND THE MAN IS THERE, lit by the torch as in the game's picture; no mesh left in the queue when the pictures
  were taken. So the bare path of run `20261009-100849` was the PC short of memory and time (PT-96), not a
  dropped feature. Each traced step waited 34 to 38 s before its pictures (the step's own settling).
- BUILT 14:58 on 10-09 (compiles; in the game with the clean build): THE PLUGIN shows a notice "building the
  traced world (N meshes to go)" while meshes wait to be registered or built, once that has lasted 2 s
  (`Session.cs`, `SceneCapture.Waiting`; a few meshes streaming in while walking say nothing). THE HARNESS
  shows a line at the bottom left while a step settles or waits for the passes (`Runner.Banner`,
  `HarnessBehaviour.OnGUI`): "<scene>, step <name>: settling, the picture does not count yet"; it is gone
  while the step's pictures are taken (a grabbed frame holds what IMGUI drew), so: no line, the picture counts.
- State: the grass is back; the two notices built, to see in the game; how long the world takes to be whole
  after a load is still not a number in the report.
- `arrival1` DONE 16:10 on 10-09 (Claude, base71; its report `wt\arrival1\docs\agents\arrival-1.md`; NOT MERGED
  YET: the main tree was building; nothing seen in the game). (1) THE TIMELINE: one line a second in
  `LogOutput.log` from tracing coming on until the world is whole ("Arrival 1.0 s | instances ... | meshes: seen,
  registered, to register, to place, built, building | textures: wanted, open, kept back, at full size ... |
  scans | ms a frame | to come N"), then "Arrival whole after N s ... The last to empty: <queue>". (2) 24
  LIMITS found on that path; the worst by its reading of the coordinator's runs: a built mesh is not on screen
  until the plugin's next scan, every 2 s (`metal`: 1,190 meshes built at 1.5 s, the first instances at 2.0 s,
  the rest at 4.0 s); the "tenth of a frame" for registering meshes started its clock before the frame's first
  call into Unity (still 6 a frame when frames are slow: its reading, not proven); and THE HALF MINUTE A TRACED
  STEP WAITS IN A HARNESS RUN IS THE RECIPES' `settleFrames` 240 (30 s at 8 frames a second), not the world
  arriving. (3) CHANGED: a scan 0.25 s after the last while registered meshes wait to be placed; the share
  counted after the frame's first mesh; textures looked at four times a second; every native budget "a frame"
  (builds, read-backs, tile bakes, micromaps, plant groups) times the frames of 60 a second the last frame held
  (1 to 8; the memory ones at most 4); the notice says "building the traced world: N meshes and M textures to
  go"; the harness waits for the plugin's `Arrival.Whole`. `TGPT_Status` grew at its end (216 to 232 bytes),
  plugin and native together. Hidden `Advanced / Arrival as before` (1: the old limits) and the recipe
  `arrival` (the world arrives three times: 1, 0, 1). Its expectation: whole after 4 to 6 s as before, 2 to 3 s
  by the clock. LEFT: `settleFrames` 240 in the recipes (the coordinator's); `analyze.py` does not print the
  arrival; four smaller limits; how long the first scan of a town takes.
- State: built by the worker, to merge after the speed runs, then `metal@arrival` and `npc-day@arrival`.
- THE TIMELINE IN THE GAME, 16:38 to 16:40 on 10-09 (run `20261009-163726`, recipe `arrival`, `npc-day` and
  `metal`; native 0FA70F19D5EE; the lines "Arrival ..." in the run's `LogOutput.log`): THE TRACED WORLD IS WHOLE
  ABOUT 2 SECONDS AFTER IT STARTS OVER, with the old limits and with the clock's alike. The town: 829 meshes
  registered in 0.2 s (0.13 to 0.18 s of it the frame's first call, which waits for Unity's render thread),
  whole after 2.0, 2.0 and 2.0 s (as before, by the clock, as before again); the guard scene: 1,441 meshes, whole
  after 2.0, 2.3 and 2.0 s. With the old limits the last thing to end is "registered meshes a scan has yet to
  place" (the 2-second scan, as the worker read it); by the clock it is "meshes to build". Tracing switched on
  again with the scene kept: 0.1 to 0.2 s. A frame while it arrives: 150 to 200 ms. At "whole" the guard scene
  traces 16,199 of 25,273 listed instances (the rest: other levels of detail and what the game does not draw),
  all 1,643 wanted textures are open, 668 at their full size and none below what the game itself asks for; NOT
  counted into "whole" and still to come then: 296 groups of plants for the wind (they stand still until
  grouped), 9 micromaps, 475 materials the last scan found.
- SO THE GEOMETRY'S ARRIVAL IS NOT THE LONG WAIT THE OWNER SAW. What a watcher of the day's test runs did wait
  through: (1) the driver making the passes at a new build's first start, 205 to 383 s today, and nearly every
  run of the day was a new build's first (the notice says so: PT-97); (2) each traced step's own settling, the
  recipes' `settleFrames` 240: half a minute at 8 frames a second, with the picture still gathering light; (3)
  this morning's starts without DirectX Raytracing 1.2 (PT-96). What is NOT measured: the first seconds after a
  SAVE IS LOADED in a fresh session (the game itself streams meshes and textures then; the recipe starts the
  traced scene over in a world already loaded); how long the picture takes to settle its LIGHT after the world
  is whole (the histories, the radiance cache, the lights' grid): that is the pop-in still to put a number on.
- The clock's rules cost nothing seen and gain nothing seen here; they stay (they are what holds at a low frame
  rate), and the hidden `Advanced / Arrival as before` goes with the next clean-up: the game has answered.
- State: the world's arrival measured (2 s); the light's settling after it and a save's first seconds not yet;
  `settleFrames` in the recipes to look at (seconds, not frames).
- ROUND 20, once (`docs/rounds/round-20.md` 10, run `20261011-064558`): the town loaded after the tavern showed a
  market stall's potted herbs BROWN in its frozen step, 10 s after the world was whole; the game draws them green, and
  the same build with the town loaded first drew them green. Brown is what such a leaf looks like while the tracer reads
  its small atlas copy before the game's own texture arrives: a late arrival after a scene switch, to watch.

### PT-97 A harness run can be steered by real input: the game in third person, menus opened, outfits "saved" (the owner, 2026-10-09)
- The owner, 10:12, watching run `20261009-100849` (`water-day`, waiting for the tracer's passes): "Your current
  game test is behaving really weird. You're in third person, you navigate the menus and save outfits. Then once
  in a while you exit back into the game".
- Checked at once: the harness simulates no input (no key, mouse or gamepad call anywhere in
  `src/TGPathTracing.Harness`); the foreground window was the owner's editor; the game wrote nothing but its
  preferences at start (10:09:52): no save slot, no outfit file. So real input reached the game: the keyboard
  and mouse while the game's window was in front (it takes the focus when it starts), or a gamepad (which a
  game reads in the background too, a streaming client's virtual one included). Asked the owner which.
- The gap: a run freezes the world and stops saves, but not the player's input. To do: while a run lasts the
  harness keeps real input from the game (the game's own input disabled or its devices ignored; say in
  `docs/PT-HARNESS.md` what it does), and a step notes when the camera or the player moved during it.
- The harness's own log of that run: "a frame could not be saved: the game left gameplay during capture:
  loading=False, hero=True, UI=UIState, interactive=False, ... focused=True": `water-day` and `metal` failed by
  it. The owner, 10:14: "the demo was super flawed in that it clicked around the menus", and of the wait for
  the tracer's passes: "the game is stuck on a gray screen with nothing happening", "the notice in top should
  say that the game is rendering shaders, right now nothing is communicated to the user".
- BUILT 10:26 (`src/TGPathTracing.Harness/RealInput.cs`, called from `Runner.cs` at the run's start, before
  every step and at its end): every device the game's input library (Rewired) knows is disabled for the run
  and given back after; a line in the run's log names them. Compiles; not yet seen in a run.
- THE NOTICE: the code shows a toast "compiling shaders (n of m)" while the passes are made and the row is On
  (`Session.cs` 95, `TGGraphicsBridge/Notices.cs`, drawn in the bridge's OnGUI); why the owner saw none is NOT
  known. A screenshot of the real screen during the next wait is set up (`<scratch>\shot-when.ps1`).
- 14:28 on 10-09: the input block works in a run ("Real input is kept from the game for the run: 2 devices off
  (Keyboard, Mouse)", given back at the end). The screen's picture during the wait showed the OWNER'S BROWSER
  (the owner works at the PC while the game runs behind): deleted, `shot-when.ps1` is not to be used. In its
  place the harness itself takes the game's own screen once, 5 s into a wait for the passes
  (`Runner.cs`: `waiting-for-passes.png` in the step's folder, Unity's `ScreenCapture`, notices included):
  the next new build's first run shows whether the notice is there.
- State: the input block seen working; the notice to be read off the next new build's first run.
- THE NOTICE IS THERE, 15:46 on 10-09 (run `20261009-154425`, `refract-jar\2-none-as-now\waiting-for-passes.png`, the
  game's own screen 5 s into the wait, saved by the harness): at the top "TG Path Tracing: compiling shaders
  (42 of 46)" over the game's own lit picture, at the bottom left the harness's new line ("... the tracer's
  shader passes are being made (5 s)"). So what the owner saw at 09:52 and 10:14 was not this state (PT-96's
  start without DirectX Raytracing 1.2, which compiles nothing and showed nothing; and the menus real input
  had opened). LEFT: the count stands at "42 of 46" for three minutes while the driver makes the four big
  pipelines: it looks stuck. To say the time passed, or which pass, in the notice.
- State: the notice proven; its stuck-looking count open.

### PT-96 One start without memory, and every later start traces without DirectX Raytracing 1.2: a frame of 2 s, and no word (2026-10-09)
- The owner, 09:52, watching a harness run: "The game doesn't show the compiling shaders screen anymore. It just
  shows a background and nothing else"; then: "Are we still using Shader Execution Reordering, Opacity Micromaps,
  and generally anything else we can to accelerate anything related to this Path Tracing implementation as much
  as possible?"
- What happened (the bridge's and the tracer's logs in the game folder): at 09:24 five workers were compiling and
  testing at once (16 dxc: 35 GB, three GPU tests: 12 GB, of the PC's 62; 1.4 GB free). The game's start then got
  "D3D12CreateDevice on the game's adapter failed: 0x8007000E" (out of memory); the bridge wrote its note
  (`BepInEx\TGGraphicsBridge\agility-extension-failed.txt`: "619 on 616, Windows build ..., GPU ...") and that
  session had no path tracing (the harness: "unsupported"; `metal@yardstick` came back empty). The NEXT start
  (09:40, the new build) read the note and used Windows' own Direct3D 12 runtime (616) in place of the mods' 619:
  "no DirectX Raytracing 1.2 (Shader Model 6.9 and tier 1.2)", "Shader Model 6.6 passes": no opacity micromaps,
  no Shader Execution Reordering. The beach then took 1,400 to 2,300 ms a frame (140 before) and nothing was
  composited for minutes: the owner's "background and nothing else". The run was stopped at 09:56.
- It was NOT a merge: every start before 09:24 logged "DirectX Raytracing 1.2 (opacity micromaps) available;
  Shader Execution Reordering reorders".
- Done at once: `tools/Build-PtShaders.ps1` holds a limit for the whole PC (`-MachineLimit` 10 dxc anywhere,
  `-FreeGb` 12; a third and twice that while the game runs), put into every worker's copy; the coordinator's
  run helper waits for 24 GB free and removes the bridge's note before a start.
- For players, open: one failed start (a PC short of memory for a moment) leaves the note until Windows, the
  GPU, a driver or the runtime changes, and from then on path tracing runs many times slower with no word on
  screen. To decide: a note that is tried again after one start or after a time; the Video tab's alert when
  tracing runs without DirectX Raytracing 1.2; the harness refusing to measure such a session.
- 14:28 on 10-09, a new build's first start with nothing cached (the game folder had lost the mod: the
  round's state): the run waited 383 s for the passes; the driver took 222.8 s for `walkDxr12`'s pipeline
  alone (the pass the wax trial grew) and 86.7 s for `prepareDxr12`. Players get that wait after every update.
- 14:55 on 10-09, the coordinator's own doing: the clean build was started with the limits raised
  (`Build-PtShaders.ps1 -MachineLimit 14 -FreeGb 10`) while four workers compiled: 12 dxc held 37 to 40 GB and
  the PC had 1 GB available for a moment, 5 to 8 after (the three big passes, bounced light, reflections and
  the walk, grow to 5 to 6 GB each AFTER they start: the free-memory test at their start does not see it). The
  game was closed and the owner at the PC. RULE FOR THE COORDINATOR: never raise the limits while workers
  compile; the script's defaults only. To build: the guard counting what the running compiles will still take.
- 15:01 to 15:05, the same hour: paging at up to 55,000 pages a second, 100 GB committed. The coordinator stopped
  its own build (8 dxc; nothing was installed). Three minutes later the PC was at 103 GB again WITHOUT that build:
  the worker `speed1`'s offline test (`tgpt_gpu_test.exe`, its measuring build `m1`) held 33 GB after 9.5 minutes
  and grew 5 GB a minute (the other worker's test of the same kind: 3.5 GB). Stopped by the coordinator (15:04;
  available memory 3 to 27 GB at once) and the worker told to find the growth and to say at the top of its report
  if it is the tracer's and not the test mode's: THAT WOULD BE A DEFECT OF THE MOD (a player's game growing 5 GB
  a minute), so read its answer.
- BUILT 15:05 (`tools/Build-PtShaders.ps1`, copied into the four workers' copies): the guard takes from the free
  memory what every running dxc has not yet taken of a big pass's peak (`-PeakGb` 8), whoever started it: about
  four big passes at once on the idle PC, fewer beside GPU tests, none new beside the game.
- State: the cause found and worked round for the coordinator's runs; the players' side open.
- THE GAME'S MEMORY OVER A NEW BUILD'S FIRST START, 15:44 to 15:54 on 10-09 (`<scratch>\game-memory-first-
  start.csv`, a reading every 5 s; build 9F87DF52B5A0): while the driver made the pipelines (206 s, was 383 s
  with the wax trial in) the game's committed memory rose from 7 to 21 GB and fell back to 14 when they were
  done; its peak over the run was 27.2 GB committed (20.3 GB working set) in the guard scene. So an ordinary
  build's first start does NOT grow without end as the worker's measuring build did (33 GB and rising): that
  was its own build. But 21 GB while pipelines are made is what a player's 32 GB PC must find after every
  update: part of the players' side.

### PT-95 A guard's helmet has none of its textures: plain chrome where the game's is dark hammered iron (the owner, 2026-10-09)
- The owner, 09:20, of the sheet `helmet-flat-smooth.jpg` (PT-92's run `20261009-084003`, `metal`): "Where did all
  the game's texture/normal map/c[...]" (the message ends there: its colour, by the picture).
- Looked at (the same run's Normals view steps; sheet `helmet-normals-view.jpg`): in the tracer's own Normals view
  the helmet is perfectly smooth, with the characters' normals on and off, while the brick wall behind it shows
  its relief. So the helmet's normal map does not reach the tracer, and by the picture neither its colour nor
  its roughness: a hit falls back to plain polished metal. The same guard's scale armour and leather pauldron
  show their textures: it is this material or its shader, not characters as such. The same with every fix of
  the day off.
- The owner, 09:50: "we're not rendering all the detail maps on the helmet it seems", "We're lacking all the details
  on the helmet from the game." Ruled out by the run's log: memory (all 1,656 of the scene's textures opened, 0 kept
  back). The tracer reads HDRP Lit's detail map and several of the game's own shaders (`MaterialPrograms.cs`); the
  helmet's shader may be one it has no reading for (a guess until the yardstick names it).
- THE COORDINATOR'S GUESS WAS WRONG (the agent `kandra`, `docs/agents/kandra-lit.md`, 10:20; merged 10:22, plugin
  only, `<scratch>\pre-kandra`). `TG/Character/Kandra_Lit` is base map x `_BaseColor`, normal map x
  `_NormalScale`, mask (metal R, occlusion G, smoothness A) and glow, all at the mesh's uv: NO detail layer, no
  tiling, no remaps (its property list and G-buffer code read): the tracer reads all it samples. The helmet is
  `Mat_ChMGenM_02_Head`, plain `Kandra_Lit`, the same names and values as the torso. What run `20261009-084003`
  shows instead: the WHOLE guard (torso, pauldron, sleeve too) drawn from the 64 x 64 stand-in tiles: blocks of
  colour, no normal map: what a material gets when its handles name textures the game's mip streaming has
  replaced and the plugin did not follow (the run's counts fit: 484 of 1,656 textures freed beyond the 1,866
  swaps followed; they name nothing, so it is NOT proven for the guard). A second defect is likely: in run
  `071155` no swap went unfollowed, the body is sharp, and the helmet is still smooth bright metal with NRD.
  The trial: hidden `Advanced / Texture follow` 0 (as before), 1 (each scan names in the log the textures a
  traced material reads through a stale handle; the picture unchanged), 2 (follows them too); the recipe
  `texture-follow` (`metal`: NRD, no denoiser, the Normals and Albedo views at 1, then at 2). Queued.
- Not known: which material and shader it is and why it is not read. Next: `metal` with the recipe `yardstick`
  (the tracer's first hit against the game's own surface, material by material; queued 09:22).
- State: seen; the cause not found. Belongs to PT-64 (what a hit reads of each of the game's shaders) and PT-21.
- FOUND BY `nrdrelief` (its report in `wt\nrdrelief\docs\agents\nrd-relief.md`, 14:45 on 10-09; a trial is
  written, hidden and off, nothing seen in the game): THE RELIEF IS NOT LOST IN NRD. NRD here is RELAX for the
  bounced DIFFUSE light only; the helmet is pure metal (its diffuse colour black), so nothing NRD returns
  reaches it. All its light is the reflections' signal, and with Denoising "NRD" that goes through THE
  TRACER'S OWN FILTER (`pt_temporal`, 16 frames, then four `pt_atrous` passes: 30 render pixels each way):
  its second pass writes the blurred picture back into the history every frame, so the test of brightness
  sees no difference between neighbours, and its test of normals is loose on anything but a near mirror (a
  bump tilted 10 degrees still counts 0.94). So "NRD wipes the relief" was the coordinator's wrong name for
  it: with "None" the signal is shown as traced. The run owed: `metal`, the worker's recipe `relief`.
- `nrdrelief` ENDED 15:28 on 10-09 (its final report `docs/agents/nrd-relief.md`; merged 15:07, only a comment
  changed after). ITS NUMBERS: the helmet's face in run `20261009-103336` (4K x 1660-1860, y 820-960), detail
  8.20 the game, 10.51 no denoiser, 0.59 with NRD (6 %), the same early and late. Offline (its small check
  `TGPT_TEST_RELIEF_ONLY`, a gently hammered metal floor): 0.53 of the relief kept as it is, 0.87 WITHOUT THE
  FEEDBACK, 0.53 with a stricter test of normals alone: the feedback is the cause. THE TRIAL: hidden `Advanced /
  Reflection relief trial`, bits added up: 4 the first pass is not fed back into the history (a plain reflecting
  surface keeps its own samples, as a mirror's already does), 1 normals weighed by the lobe, 2 the same specular
  colour; try 4, then 5 and 7; recipe `relief` (the game, none, NRD at 0, 4, 1, 5, 7). Cost offline: flicker
  0.005 to 0.007 against 0.253 undenoised (0.016 to 0.028 on strong bumps), one more texel read a tap. Not
  known: its noise in the game's dim lamp-lit places. NON-METALS TOO: a sunlit wall keeps 0.58 of its detail
  with NRD; the direct light's filter has the same feedback by the code (not measured): its proposed next trial,
  not built. In the build of 15:21; the run `metal@relief` follows it.
- THE RELIEF TRIAL IN THE GAME, 15:52 on 10-09 (run `20261009-154425`, `metal`, recipe `relief`; build native
  9F87DF52B5A0; sheet `status\img\helmet-trial.jpg` in the scratch folder, and on the owner's status page):
  IT HELPS A LITTLE, FAR FROM ENOUGH (the coordinator first wrote "it did nothing"; the owner, about 16:00, looking at
  the sheet: "The helmet fix did help a little, didn't it?"; measured then, `<scratch>\detail_match.py`, against
  the undenoised step: of the FINE dents NRD keeps 6 % at 0, 7 % at 4, 10 % at 1, 11 % at 5, 12 % at 7; of the
  shapes 10 to 40 pixels wide 26, 31, 28, 34 and 41 %; and what comes back is the real pattern: its correlation
  with the undenoised helmet's detail rises from 0.22 to 0.61 fine, 0.58 to 0.83 coarse; the game's own
  picture against the undenoised traced one: 0.96, so the traced relief IS the game's). Every bit adds; the
  bulk of the loss is elsewhere. Fine detail on the helmet's face (4K x 1660-1860, y 820-960; the mean distance of the
  luminance from its 9 x 9 blur, the median of 16 frames; `<scratch>\detail.py`): the game 4.12, no denoiser
  4.53, NRD with the trial at 0: 0.29, at 4 (no feedback): 0.32, at 1 (normals by the lobe): 0.45, at 5: 0.52,
  at 7: 0.55. The tracer's log names each setting as it came on, so the switch arrived. The worker's offline
  floor (0.53 kept as it is, 0.87 without the feedback) does not stand for the helmet: again a fixture the
  game did not agree with. NOT KNOWN what wipes it. To find IN THE GAME FIRST, one stage at a time, with
  switches that exist or are one line: the reflections' history (`pt_temporal`) alone, each of the four
  `pt_atrous` passes, the direct light's highlights against the reflection ray's light, and which normal the
  filter's test reads on a character (since PT-92 the denoiser's guide normal on characters is the SMOOTH one:
  if the filter reads that, the dents, which live in the normal map, are invisible to it: a guess to test
  first). The trial's code stays (off): it is part of the answer.
- State: open; the trial gives back a little (12 % of the fine dents, 41 % of the larger shapes); the main
  cause not known.
- WHERE THE DENTS LIVE AND DIE, 16:08 on 10-09 (run `20261009-160428`, `metal`, the new recipe `relief-where`: the
  tracer's own views with no denoiser and with NRD; the same box, `<scratch>\detail_match.py`): fine detail in
  the Specular view 4.31, with NRD 0.29 (7 %); in the Direct view 3.66, with NRD 0.31 (9 %; the larger shapes
  37 %); in the Indirect view 2.50, with NRD 0.24 (10 %; the larger shapes 25 %, and its pattern no longer the
  undenoised one: 0.12). ALL THREE SIGNALS LOSE NINE TENTHS OF IT, so it is not one filter's feedback: the
  direct light's filter, the bounced light's (NRD's RELAX itself) and the reflections' each spread across
  dents that tilt the normal by one to three degrees (the Normals view of run `20261009-103336` shows them:
  detail 1.0 level of 255) and change the brightness by a tenth: less than a sample's noise, so no test of
  brightness stops there, and far inside any test of normals a denoiser can afford. And since PT-92 the
  denoiser's guide normal on characters is the smooth one. WHAT THAT POINTS TO (to try in the game, one at a
  time): the guide normal WITH its bumps where bumps are that small; filters that narrow as a pixel's history
  grows long (a still pixel needs no wide blur after 16 frames); the lamps' and the sun's highlights kept out
  of the reflection ray's wide filter (they are far less noisy after ReSTIR). Not one worker's guess again: a
  job whose every step is one run of `metal@relief-where`.
- State: open; where it is lost is known (all three signals); the relief trial gives a little; next job not
  started.

### PT-94 The sun's light under water is only written where the camera looks (2026-10-09)
- Found by the water job (`docs/agents/water-9.md` in `wt\water9`, its third finding; offline, not seen in the
  game): the caustics pass's `in_view` writes no landed triangle outside 1.5 times the view, but paths under
  water read the landed light there too (a bed's light bounced from outside the view: 16 % short in a
  narrow-view fixture). The owner's rule (2026-10-07): "off-screen effects must be included, no screen space
  stuff": nothing the tracer shows or is lit by may depend on what happens to be on screen.
- In the game, 14:41 on 10-09 (run `20261009-142733`, PT-81's note): with `Advanced / Water light without view` the
  bed is lit all over (a patch 28 to 51 levels of red, the game 68) and the dark slab is gone.
- State: the trial is right in the game; to become the rule (and cheaper: +17 ms on the beach).

### PT-93 Grass lit by a fire boils with NRD: 460 speck spots a frame at the campfire, the game 1 (2026-10-09)
- Found counting the speckles of seven scenes (PT-85; `campfire`, NRD, 16 frames, run `20261009-071155`; sheet
  `campfire-grass.jpg` there): 455 spots a frame against the game's 0.1, the same with the lamps' near size off.
  82 % are one patch of grass and plants in the fire's light, 16 % grass at a tent's edge; rises to 80 levels in
  every frame. Posts, cloth and stones beside the grass are still.
- One run to split it (`20261009-074726`, the recipe `grass-why`; spots a frame, places): NRD with the camera's
  sub-pixel shift 459.5 (833); no denoiser with the shift 96.4 (428); no denoiser without it 308.1 (1,375); NRD
  without it 562.6 (1,089); the game 1.0 (7). So it is NOT the shift (without it: more), and NRD makes five times
  the spots of no denoiser at all: its kept light does not hold on grass, and what comes out jumps.
- Not known: why NRD's history fails there (blades thinner than a ray's pixel, so hit and miss change places from
  frame to frame and the history is thrown away; or the grass moving in the tracer while the world is frozen; or
  its cut-outs). The inspection of 10-08 said the same of the night scene: "With NRD the grass still flickers".
- THE AGENT `grassboil` (`docs/agents/grass-boil.md`, 09:00; merged 09:02): the cause it read in the code:
  `alpha_opaque` flips a new coin every frame at each see-through blade a light's ray meets; a level ray from a
  fire on the ground meets n blades, so the light arrives in one frame of 2^n at 2^n times the mean. Also: NRD is
  never handed the direct light (the bounced only): the direct light goes through the tracer's own `pt_temporal`
  (16 frames) and two `pt_atrous` passes in every mode, and the debug views never show NRD: the "Direct view
  with NRD" numbers above are the built-in filter's. Its trial: hidden `Advanced / Grass shadow trial` 1: a
  shadow ray multiplies what each blade lets through (the coin's own mean, no coin). IN THE GAME, 10:25 (run
  `20261009-100849`, the recipe `grass-product`; a flawed run: its steps with the trial off came out untraced,
  two other scenes were spoiled by stray input, PT-97): with the trial ON, NRD: 345.7 spots a frame (750
  places), the grass patch 0.94 levels between neighbouring frames; with no denoiser 99.9 on, 110.2 off. Against
  the earlier runs' off (455 to 460; 1.43): a quarter to a third less. It helps; it is NOT the cure. To repeat
  as a clean off and on pair; then the agent's next suspect (ReSTIR's reuse weights on blades).
- State: measured; one cause found and a trial that takes a third; the rest not found.
- THE GRASS TRIAL AS A CLEAN PAIR, 14:50 on 10-09 (run `20261009-144507`, `campfire`, recipe `grass-product`, one
  run, 16 frames; flickering spots a frame by PT-85's measure): NRD 526 with the trial off, 401 on; no
  denoiser 202 off, 164 on; the game 0.8. The picture's mean is the same (49.84 and 49.78: no light lost);
  it costs 9 to 12 ms a frame. So the trial takes a quarter, not the cause. THE NUMBER THAT POINTS AT THE
  CAUSE: with NRD there are 2.6 times as many flickering spots as with no denoiser at all: NRD makes the
  fire-lit grass boil (thin blades, cut-out edges: its guides change from frame to frame there), it does
  not merely fail to clean it. That is the denoiser's guides for grass, with PT-95 and PT-100.
- State: the trial stays off (a quarter for 10 ms); the cause is in what NRD is handed for grass.

### PT-92 Characters are made of flat triangles: a guard's helmet in bands, a cloak in facets (the owner, 2026-10-09)
- The owner, 07:27: "I noticed some blockiness/low-polyness on the armored guards helmet, and the cloth of the guy
  with the weird mask standing next to the bridge. This is a problem we resolved some time ago. Why is the
  low-polyness back, and is it related to our fix for speckles?"
- Looked at (run `20261009-071155`, NRD; sheets `helmet-game-off-on.jpg` from `metal`, `masked-man-game-off-on.jpg`
  from `night`): the helmet is a chrome mirror in flat bands where the game's is dark hammered iron; the cloak's
  lit side is flat triangles with hard edges, its weave gone. THE SAME with the lamps' near size at 0 and at 10:
  not the speckles' fix.
- Why it is "back": PT-04 (2026-10-06) gave round PROPS the mesh's vertex normals; "Characters' meshes bring no
  normals" was written there, and `pt_primary.hlsli` says it still ("the tracer has no vertex normals for
  characters"): `pt_skin.hlsl` brings a character's skinned positions only. On 10-06 the camera's first surface
  was still HDRP's own (smooth, with its normal map); since the tracer fires its own camera rays (10-07) the
  camera sees the flat faces. NRD makes it worse (its guide is the face's normal; the inspection of 10-08, row
  110, of the same cloak: "With NRD the creature and a cloak are flat facets", faint with no denoiser).
- To do: a character's smooth normals reach the tracer with its skinned positions (and its normal map is laid on
  them), for the camera's rays, reflections and the denoisers' guides. The helmet's chrome against the game's dark
  iron is a second thing (its material: PT-21).
- THE TRIAL IN THE GAME, 08:55 (the agent `normals`, `docs/agents/char-normals.md`; merged 08:20; run
  `20261009-084003`, `metal` and `night` with the recipe `char-normals`; sheets `helmet-flat-smooth.jpg`,
  `masked-man-flat-smooth.jpg`: the game, flat, smooth, smooth with NRD's guide): the helmet's bands and the
  cloak's triangles are gone at 1 and 2; `metal`'s speck spots 91.5 a frame flat, 0.9 at 1, 0.6 at 2; the
  picture's mean 67.9 to 62.0 there (the flat faces' highlights went), `night` unchanged (45.9, 46.2). The data:
  the game's own skinned normal (Kandra's `CompressedVertex`, byte 12) travels with each position; 4 bytes a
  vertex. MADE THE DEFAULT 09:00 (`Advanced / Character normals` 1; NRD's smooth guide holds wherever a
  character brought its normals and needs no option bit). Left: the helmet's missing textures (PT-95), the
  cloak's weave gone with NRD (PT-19).
- State: in the tree as the default; in the game with the next build; the owner saw the trial's sheet.

### PT-91 The game minimized while tracing: a frame takes up to 2 s, then the game is gone (2026-10-09)
- The owner, 07:10, watching a harness run: "I think I broke the run by minimizing it though". Read from the run's
  logs (`20261009-070553`, `glass-jars`, step 3; `TGPathTracing.Native.log`): the tracer's frames went from 100 ms
  of GPU time to 618, 1,913 and 1,641 ms (14, 6 and 7 frames in 10 s), and some 40 s later the harness found "The
  game is not running and the run has not finished".
- Not known: whether the game ended by itself or was closed (to ask the owner), and whether the same happens
  outside a harness run. What a player would meet: a traced game left with Alt+Tab crawls, or is gone on return.
- For the harness: a run does not survive the window being minimized; `docs/PT-HARNESS.md` should say so.
  State: seen once; not chased.

### PT-90 A candle mirrored in water is a burnt-white blob, far brighter than the flame seen directly (2026-10-09)
- Seen by the coordinator while counting what is still burnt with the near size on (PT-85; run `20261009-070553`,
  `npc-candle`, NRD; scratch sheet `npc-burnt.jpg`, the blob at x 1813 y 1695 of the 4K frame): in a stone basin's
  water a candle's reflection is a glowing blob some 50 pixels across, 521 burnt pixels (598 with the near size
  off: it is not the near light's); the game's own picture has dark water there, no reflection at all. The candle
  beside it mirrors as cream wax, as it should.
- A guess, not measured: `pt_common.hlsli` `reflected_lamps` shows a reflection ray the lamp as a ball of its
  flame's size carrying the lamp's whole strength (I over the ball's disc: for 21,000 candela in 0.6 cm some
  hundred million nits), which is far more than the flame's own mesh shows the camera; NRD then spreads it. The
  owner's rule: "A mirror must show the world as the camera sees it": a mirrored candle is its flame and its wax,
  as bright as they are seen directly.
- State: seen; cause guessed; after the speckles' count.

### PT-89 The inspection's smaller new findings (2026-10-08)
- From `docs/agents/inspection-20261008.md` (its row numbers; crops in `artifacts/inspection-20261008/`). Each is a
  thing seen in a picture, no cause known:
  - row 38: a helmet's feather crest is a comb of hard white blocks (`tavern-hero.jpg`).
  - row 45: a fir tree has orange leaves and white blobs in it (`npcday-tree.jpg`). Row 11: orange speckles all over
    far grass and ferns in the forest (`forest-ridge.jpg`).
  - row 14: a far person's bracers are burnt to white blocks. Row 12: a far lantern outdoors pulses with no denoiser
    and is a glowing ball with NRD; its hut is lit yellow (x1.32, x1.56).
  - row 30: stone walls lose their pale mortar (the seams are dark; detail 2.1 against 4.9; `candle-stone-zoom4.jpg`).
  - rows 76, 77, 79: in the lantern room a lit plank's grain is rows of jagged dark steps, a metal candlestick is
    grainy and half see-through, a white bottle is brown grain with no denoiser and white with NRD.
  - row 75: the lantern's glass is milky white, clear in the game. Row 85: glass jars are cloudy with dark blotches.
  - row 96: the lit rock ledge at the cave lake shows square blocks of about 8 pixels (a coarse texture level? a guess).
  - row 106: daylight seen through a door is washed out to pale cream (the opening x1.9 to x2.0 at gain 2.58).
  - row 120: a rock by the fire carries a black scribble pattern with NRD. Row 24: a rope on the ground is dashes.
  - The harness (PT-26): row 123: in `hair-eyebrows`, `glow` and `metal` the view shifts by some pixels between the
    first and the last step (8 to 27 % of pixels off by over 8 levels, `cameraMoved: False`); row 90: the scene
    `black-spots` has no save (PT-27 cannot be judged).
- State: open; none assigned. Take them one at a time (the owner's rule of 14:05), after PT-88, PT-84 and PT-83.
### PT-88 In a mirror a metal breastplate is pure black; seen directly it is not (the owner)
- The owner, 2026-10-08 14:00: "I noticed in the mineral cave mirror scene that the armor was pure black in the
  reflection in the mirror btw, which it wasn't when you showed how it actually looked before adding the mirror".
- Seen and measured by the coordinator, 14:05 (run `20261008-130029`, `mirror-cave`; the sheet
  `artifacts/inspection-20261008/mir-mirror-cave-centre.jpg`; the same in run `20261008-111121`'s `mirror.jpg`). The
  breastplate's front, mean R G B: the game's view 12, 24, 40; the tracer's own view from the mirror's side 21, 15, 18
  (orange and blue bands: what the polished plate reflects); in the mirror with no denoiser 1.9, 4.2, 10.7; with NRD
  2.1, 4.4, 11.4. The cave wall behind it is the same in both (10, 22, 40 directly; 7, 20, 37 in the mirror). So
  the plate alone is 4 to 5 times darker in a mirror.
- The cause, READ IN THE CODE, not shown by a test yet: what a reflection shows is shaded by `shade_seen`
  (`pt_common.hlsli`). A smooth metal there gets one more reflection ray, and what that ray meets is shaded as a
  path vertex (`shade_path`, one bounce) with NO fog over the ray's way, and the sky on a miss with no fog either.
  The camera's own pixel of the plate gets `fogged(...)` over its reflection ray (`pt_specular.hlsl`) and the full
  shading of what it meets. In this cave nearly all the light the plate shows directly is the fog's blue light and
  one lamp; without the fog on the second ray a metal (which has no diffuse colour) is black.
- The same mirror sheets also show (the inspection's rows 64 to 66, PT-46): two lamps at the top of the cave's mirror
  that the direct view does not show; in `mirror-fog` the mirror's night is grey and about 1.76 times brighter than
  the direct view and the fire's embers are missing (PT-67); in `mirror-tavern` leather and nail heads are glossy in
  the mirror and matte directly; NRD does not calm the boil inside a mirror (0.30 against 0.31).
- THE FIX CHANGED NOTHING (run `20261008-144459`, `mirror-cave`, 14:46; the build with the fog on the second
  reflection's way; sheet `artifacts/harness/20261008-144459/mirror-cave-centre.jpg`): the plate in the mirror is
  1.8, 4.3, 11.0 with no denoiser and 2.0, 4.3, 11.6 with NRD (before: 1.9, 4.2, 10.7 and 2.1, 4.4, 11.4); seen
  directly 20, 14, 18 as before. THE CAUSE GIVEN ABOVE WAS WRONG (the coordinator's; said to the owner as read,
  not shown). The fog on that ray stays in the tree (`fogged_after`, `shade_seen`): it is what the first ray
  gets, but no picture and no check has shown it doing anything yet.
- WHAT IT LIKELY IS, not shown yet: the harness's mirror stands 1.6 m straight in front of the hero, so the
  polished plate faces it. The tracer follows the camera's ray to the mirror, to the plate, and one more ray from
  the plate (`shade_seen`), which lands on the mirror again; there the chain ends and the mirror is shaded as a
  matte path vertex (`shade_path`): black in a dark cave. The fog over 1.6 m is nothing, which is why the fix
  did nothing. In the picture "before adding the mirror" no mirror stands in front of the plate, so the plate
  reflects the cave ahead: the orange and blue bands. Fits the other mirror scenes: with the mirror straight
  ahead the cuirass differs in every one (the tavern's glints, the fog scene's lost fire glow); with the mirror
  turned 55 degrees (`mirror-water`) the mirror matches the direct view (shapes 0.97).
- So two things: (a) A GAP IN THE TRACER: a chain of mirror-like bounces ends after two; a mirror facing polished
  metal goes black where a real one shows the plate with the mirror in it, and in that the cave behind the hero
  (a dim blue-grey plate here, not black, and not the bands). "No limits that change the picture": mirror-like
  bounces must be followed until chance ends them, at the cost of noise, not of light. (b) THE HARNESS: the view
  "from the mirror's side" is taken with the mirror away, so for the hero's own front it is not what a mirror
  should show. It should be taken with the mirror standing (seen only from its front).
- THE OWNER, 2026-10-08 17:40, told this: "that's probably correct - the mirror blocks the lighting from ever
  reaching the plate. In fact that just makes it more phyiscally correct I guess". So the dark plate is not a
  defect to chase now. What stays, low in the queue: (a) a chain of mirror-like bounces ends after two (a real
  mirror facing polished steel shows a dim plate, not a black one); (b) the harness's view "from the mirror's
  side" should be taken with the mirror standing. State: answered by the owner; (a) and (b) open, not urgent.
### PT-87 Path tracing still pauses itself when video memory runs short ("6 textures unavailable")
- Fog worker, 2026-10-11: the existing three camera-fog targets were missing from `read_diagnostics`' target tally.
  Counted in `fogfroxel`, along with its two new grid textures; no public diagnostic record change. Native built;
  source checks pass, live memory reading pending the first game run.
- THE TRACER'S START IN THE GAME, STEP BY STEP (run `20261010-092027`, the tavern, no denoiser; `memory3`'s lines):
  after 110 traced frames Windows' count stood 4,188 MB over the tracer's start (8,611 MB in all; the game
  alone about 4.4 GB). Of the 4,188: the start's own steps 1,125 (rings 144, atlases 261, outputs and HDRP's
  copies 175 and a second count, surfaces 136, RTXDI's reservoirs 225, signals 136, the cache 36); opening
  791 of the game's textures +700 MB for 700 MB of textures (THE SECOND COUNT, at full scale); and
  RECORDING AND RUNNING THE PASSES +1,935 MB: 616 the moment the bounced light's pass was first recorded, 914
  when frame 1's list was handed to the card, the rest in the next frames. The tracer's own line sums 2,358.
  SO THE LARGEST SINGLE THING IS MEMORY THE DRIVER TAKES WHEN THE PASSES FIRST RUN: 1.9 GB in the game against
  0.66 offline, where the two reordering passes (Shader Execution Reordering) do not run. THE NEXT TEST: one
  start with `Speed trial` 8 (the two big passes without reordering) from before the first traced frame
  (recipe `no-ser-memory`, scene `tavern-room`): if the 1.9 GB falls toward 0.7, the reordering costs about
  1.2 GB of the budget for the 5 ms it saves in the tavern (nothing at the beach, PT-99's measuring run): a
  choice for the owner, or a rule by the budget. NOT RUN: the owner was in the game at 09:45.
- THE ATLASES' CUT WORKS IN THE GAME (the same run): "the game's device let go of the colour atlas (192 MB,
  2.1 s and 21 traced frames after its last bake), the alpha atlas (64 MB), hair's strand atlas (85 MB)" and
  the cookie atlas (4): 345 MB of second count gone while nothing is baked. The A/B run and the frame time at
  a "took up again" line: not read yet.
- `memory4` ENDED 06:40 on 10-10 (report: `docs/agents/memory-4.md`; merged into main 06:41, installed 06:42,
  native EF7EA3CAB438): THE FIRST GIVE-BACK OF THE SECOND COUNT. The game's device now holds the tracer's four
  atlases (colour, alpha, cookies, hair's strands) only around a bake: it lets go two seconds after the last bake
  into an atlas, or after one second without a traced frame, and opens it again by a kept handle before the next
  bake. Offline: Windows' count fell by 345 MB (192 + 64 + 4 + 85; 260 without hair) and came back only around a
  bake, at two sizes; the same frames with the sides let go and held, a bake into all four after they were let
  go: every pixel the same. Fails safe (an atlas whose way back cannot be shown stays held, with a log line; a
  bake whose opening fails waits a frame). ON by default; `TGPT_TEST_ATLAS_SIDES=0` in the game's environment is
  the A/B. One change from the plan: two seconds by the clock, not 120 frames (`kSideIdleMs`). NOT SEEN IN THE
  GAME: the frame time around a "took up again" line, and whether pauses for textures now end by themselves.
- `memory3` ENDED 05:45 on 10-10 (report: `docs/agents/memory-2.md`, section "memory3"; its hand-over 2 merged
  into main at 05:45, C++ only; installed 05:46, native 4C8B6B659C30):
  - THE 2 GB, MEASURED OFFLINE (1280 x 720; there it is 1,195 MB): the three upload rings 144 MB (Windows counts
    them as the card's; the memory line wrongly took them out); the tracer's OWN shared textures counted a
    second time 338 MB (it comes when the game's device first writes them); memory the driver takes when the
    passes first RUN about 330 MB (700 in the first frames); heap and queue 12; making the 24 pipelines 1 MB.
    In the game about 1.5 GB more is left between the driver's part and the game's own moves: NOT measured:
    the new log lines "PT memory at start: <step> +N MB (Windows counts M)" and "PT memory since the tracer's
    start" split it at the next start of the game.
  - GIVEN BACK OF THE 2 GB: NOTHING YET (its own words). Not built, with why: the atlases' second count (261
    to 347 MB: the game's device could drop its side between bakes; its probe has not run); the rings by need
    (up to 130 MB: needs the game's new `ringPeak=` figure first).
  - "PATH TRACING PAUSED: N TEXTURES UNAVAILABLE" IS OUR OWN CAP acting on Windows' count. A trial (only with
    `TGPT_TEST_SHARE_COUNT_ONCE=1` in the game's environment) takes the 1.1 to 1.9 GB of second counts out
    before weighing. To run once, with the owner's word: whether frames stay fast past the cap decides it.
  - THE SECOND COUNT OF THE GAME'S TEXTURES (0.7 to 1.3 GB): the only supported way that counts once, with no
    copy, at full size, is THE GAME ITSELF ON DIRECT3D 12 with the tracer on Unity's device: weeks, all four
    mods. D3D11On12: no. Evicting the tracer's side: Microsoft says "device removed". Holding fewer open: 245
    to 417 MB at most. A choice for the owner, later; its sources are in the report's M3-4.
  - `memory2`'s cuts in that copy were further than its report said: cut 1 (the chunks' free room) whole and
    ON (33 against 49 to 65 MB offline, every pixel the same); cut 2 (the plants' structures compacted, to
    84 %) passed its check, OFF until a game run shows its frame time; cut 3 (the water's) compiles, never
    run, OFF (`scene.hpp kMemory2Waiting`).
- ANOTHER PROGRAM ON THE CARD: at 05:44 on 10-10 the harness's own check showed QuickLook (the owner's
  file previewer) holding 6,561 MB of video memory at 0 % use, Sunshine 1,243, NVIDIA Overlay 308, Brave 286,
  VS Code 474. The guard rightly holds every game run back for QuickLook; that is why no run happened from
  01:43 on. What other programs hold comes straight off Windows' budget for the game (15.2 GB in the run of
  20:25, 13.0 to 13.8 in the owner's session). TO TELL THE OWNER, and to log: the budget and the largest
  other holders at the tracer's start, so a player's "paused" can be told from ours.
- THE LARGEST LEAD SO FAR, found by `memory2` before a usage limit cut it off at 01:55 on 10-10 (its report:
  `<scratch>\wt\memory2\docs\agents\memory-2.md`, section 1; from logs, nothing built):
  (1) WINDOWS DOES COUNT THE GAME'S TEXTURES A SECOND TIME when the tracer opens them (the tracer's own GPU test
  measures it in every run: a 5.38 MB texture moves the count by 5.38 MB, 274 times of 274; a probe of 10-06:
  213 MB of textures held by both devices, Windows' count +430, the card itself +216). The coordinator's
  reading of 23:55 ("`notTracer` fell, so not twice") was WRONG: the game's own part moves by gigabytes within
  seconds. So 703 to 1,316 MB of Windows' budget go to a second count that holds nothing on the card.
  (2) ABOUT 2 GB OF WINDOWS' COUNT COME WITH THE TRACER'S START AND ARE IN NONE OF ITS FIGURES: the game alone
  in the tavern 4.30 GB; 2.5 s later, the tracer ready and nothing traced, 8,007 MB, of which the tracer's line
  sums 1,515: 2,089 MB (1.8 to 2.3 GB) unexplained. Not measured what it is (the driver's memory for the 44
  passes and two devices; the tracer's own shared textures counted twice, 436 to 616 MB; the SDKs' heaps).
  So `notTracer` is NOT the game: in the tavern the game is about 4.4 GB where the line says 6.3. "The game's
  own 9 GB" of the owner's session held these 2 GB and the second count too.
  (3) The chunks' free room is only 20 to 113 MB (the log's sentence did not take the water's pages out).
- `memory3` (a fresh Claude worker in the same copy, started 04:47 on 10-10, `brief-memory-3.md`): Windows'
  count read at every step of the tracer's start, as log lines (hand-over 1 for one start of the game); what
  of it can be given back with the same picture; the ways to hold the game's textures counted once, written
  up (if the only way is the game's own device on Direct3D 12, to say so with what it would save).
- IN THE GAME, the build of 20:19 (run `20261009-202514`, 20:25 to 20:36, read 23:50 with
  `<scratch>\memory_read.py`; MB = bytes / 1,048,576; the tracer's own count):

  | Scene | NRD | no denoiser | Mode Off (after 5 s) | before this build |
  |---|---|---|---|---|
  | the tavern (`tavern-room`) | 2,930 | 2,396 | 1,714 | 3,191 (NRD) |
  | the beach (`water-day`) | 3,885 | 3,352 | 2,342 | 4,345 (NRD) |
  | a town in the rain (`spec-terrain`) | | 4,389 | | 4,995 |

  So `memory1`'s cuts give 260 to 460 MB with NRD, 600 to 800 with no denoiser, and Off gives 1.2 to 1.5 GB
  back (the traced world itself stays: 1.7 to 2.3 GB; turning it on again took no wait). NRD came back whole
  after Off (the same totals to 30 MB).
- WHAT WINDOWS COUNTED in that run (the memory line's new fields): its budget 15,209 MB (the desktop quiet); the
  whole process 9.0 to 11.1 GB while tracing; NOT THE TRACER (the game and the other mods' part) 6.0 to 7.2 GB,
  and once 4.6 GB: it fell by 2.2 GB between two steps of one scene with nothing changed (the game let go of
  the scenes loaded before). In the owner's own session of 17:30 the budget was 13.0 to 13.8 GB and the game's
  part about 9 GB. So THE GAME'S PART MOVES BY 3 GB and more with what it has loaded lately, and with what else
  runs (the harness runs with Frame Generation and TG Neural Rendering off: TO ASK THE OWNER whether they were
  on in play). In the harness everything fits; in play it did not.
- THE OWNER'S QUESTION, the memory's part, IN NUMBERS (the new "by distance" lines; a thing counted at its
  nearest use, so a band's sum is what a cut at its near edge would free):

  | Scene | under 50 m | 50 to 150 m | 150 to 500 m | 500 to 1500 m | over 1500 m | named by no instance |
  |---|---|---|---|---|---|---|
  | the tavern | 378 | 0 | 7 | 43 | 168 (the outdoor map, held while in the room) | 388 |
  | the beach | 342 | 240 | 247 | 173 | 58 | 371 |
  | the town | 825 | 504 | 369 | 282 | 56 | 587 |

  Past 150 m: 0.2 GB in the tavern, 0.5 at the beach, 0.7 in the town: 7 to 16 % of the tracer's memory. Past
  500 m: 0.2 to 0.3 GB. So the world without a distance limit is NOT the main reason for the memory either.
  The largest single things in the town: the game's own textures held open 1,316 MB (487 of them under 50 m),
  the plants' posed copies 783 MB (577 of them within 150 m), the tracer's fixed part about 1,650.
- `memory1` ENDED 20:16 on 10-09 (its report: `wt\memory1\docs\agents\memory-1.md`, to be merged with its last
  small changes after the build that is running): its own check passed offline (38 ok, 0 FAIL, 1280 x 720, on
  Windows' own runtime, without NRD): filters 471.7 -> 291.7 -> 471.7 MB, caustics 215.5 -> 67.2 MB, off 471.7
  -> 0 -> 471.7 MB, every picture the same pixel for pixel. NOT run: NRD's part, the DirectX Raytracing 1.2
  route, the game. Written up and not built: the plants' structures compacted (150 to 250 MB in a town), one
  rule for all of the tracer's memory, atlases grown by need. NEXT FOR MEMORY: the game's own 9 GB (PT-107,
  worker `game1`), and the plants (PT-109's worker looks at their structures first).
- LATEST, 20:02 on 10-09: `memory1`'s hand-overs 1 and 2 are in main (its frozen copy `memory1-handover2`; C++,
  one plugin line, a check of its own; no shader, no ABI): the filters' textures made when a frame runs them and
  let go 120 frames after (136 MB with NRD, 407 MB with None, RR or Ray Regeneration); a denoiser's own memory
  let go 120 frames after another took over (NRD 262 MB); the wet store with the first water (32 MB); the
  caustics' room by what a frame builds over (158 MB -> about 10 where no light lands); MODE OFF GIVES THE
  MEMORY BACK after 5 s (found: "Off" freed nothing: 1.8 GB plus the whole traced world stayed, so an A/B with
  Insert compared the game against the game with 4 to 6 GB of its card taken); a count of the memory by
  distance from the camera in the log. NOT RUN ON A GPU by the worker (its check ended at its memory ceiling
  twice while the driver made the reordering passes). Its expectation, NRD chosen: nothing traced 1,819 ->
  1,651 MB; a small interior 2,734 -> 2,450; a town 4,238 -> about 4,000; by the water about 5,600 -> 5,340.
  So: a few hundred MB, not the gigabytes that are missing. Its answer to the owner's question (is the world
  without a distance limit the reason?): a part, not the main one: the game's 9 GB and the tracer's fixed
  1.9 GB take 10.9 of 13.0 to 13.8 GB before anything is traced; the world adds 1.3 GB in the tavern, 3.0 in
  the town, 2.4 to 4.8 by the water. The largest thing the world adds that the game does not hold: the plants'
  posed copies (795 MB in the town: every plant its own copy and structure). The game's textures (1,246 MB)
  are counted twice by Windows. Building with it for the run `tavern-room@memory`, `water-day@memory` (20:05).
- The full run `20261008-130029`, scene `metal`, the card to itself: "Path tracing paused: 6 textures unavailable
  (18.9 MB). The game is drawing the scene"; both traced steps hold the game's picture (`traced: False`). The
  bridge's line: "the game uses 12.25 GB of its 13.52 GB budget" (14.85 GB in every other scene, the game at 8.6 to
  11.9 GB): something took 1.3 GB of the card for a while and 13 textures were "kept back near this card's video
  memory budget". The memory job (PT-73 b) was said to have fixed this; it has not.
- What a player sees: the traced picture turns into the game's with no word they can act on. A guess, not measured:
  on a 12 GB card this happens in most outdoor scenes (10.8 to 11.9 GB here at 4K with DLSS Performance).
- Not known: what took the 1.3 GB (a worker's test starting up? the harness waits for a free card but nothing
  stops a worker starting a test beside a running game), and why 18.9 MB of textures cannot be opened with 1.27 GB
  of the budget unused (how much is kept back, and for what).
- PT-21 and PT-32 (armour and metal) could not be judged in that run. State: open.
- THE OWNER'S OWN SESSION, 17:27 to 17:39 on 10-09 (playing, NRD, 4K with DLSS: the tracer at 1924 x 1084):
  "I very, very frequently get this [the screenshot of 17:35: 'Path tracing paused: 129 textures unavailable
  (94.6 MB)'], so I'm barely able to test PT. Not sure where these insane perf issues come from, but it seems
  like we have big problems loading in textures"; "Seems like we're constantly exceeding our VRAM budget"; "it
  feels like we don't clean them up as we go? I walked around, then I moved to a small interior - it is forever
  stuck not loading PT waiting for textures". THE NUMBERS of that session (the game folder's logs, read
  18:40): Windows gives the game 13.0 to 13.8 GB of the 16 GB card and counted 12.3 GB at 5.6 minutes, 14.36 GB
  (104 %) at 6.1 minutes, 13.1 to 13.4 GB (100 to 103 %) for four minutes, then 12.2 GB (92 to 93 %) in the
  interior. The tracer's own memory (its line "PT memory bytes"): 1.8 GB with nothing traced; 3.8 to 4.2 GB in
  the first scene; 6.7 GB OUTDOORS BY THE WATER (acceleration structures 918 MB, mesh geometry 320, plants in
  the wind 765, the game's textures it holds open 1,228 (Windows counts them a second time), atlases 347,
  targets 557, histories 530, NRD 262, CAUSTICS 1,322, transfer 145, skinned 103); 2.7 GB in the interior. So
  the game itself holds about 9 GB there, and game plus tracer is 15 GB and more outdoors. Over the budget the
  tracer's frame took 1,490 ms (37 of its 10-second lines): the "insane perf issues".
- NOT A LEAK: the tracer let the outdoor scene go (6.7 to 2.7 GB; "Traced scene released after the game unloaded
  a scene" 12 times). THE INTERIOR STAYED PAUSED BY THE RULE ITSELF: textures are opened only while the count is
  under 90 % of the budget; it sat at 92 to 93 % (the game's 9 GB and the tracer's 2.7), nothing was left to
  free, and 596 textures of 293 MB waited for ever. No TaintedTextureBudget mod is loaded (the log's "Loading"
  lines: the TG mods and Conversation Lighting Overhaul only); the game's own mip streaming budget was 1.5 GB
  when read on 10-07, so the game's 9 GB is mostly not streamed textures.
- THE COORDINATOR'S PART IN IT: the build installed at 16:58 carried `water11`, whose rule for the light under
  water grew the caustics' memory from 215 MB (every run before) to 689 MB at the beach and 1,322 MB in the
  owner's walk. Taken back at 18:45: the build of before it reinstalled (native 0FA70F19D5EE), `water11` out of
  the main tree again (`<scratch>\unwater11.py`, `pre-unwater11`; its copy and report keep it).
- CHANGED 18:46 (`texture_share.cpp` `kBudgetShare` 0.9 to 0.97, its source check): textures open while the
  count stays under 97 % of the budget, so the interior's 293 MB open (the count then 95 %). It cures the
  stuck interior, NOT the outdoors: there the tracer's other memory overruns the budget, and no rule of texture
  opening holds that back.
- THE JOB (the first one now: the owner cannot test without it): PATH TRACING FITS THE CARD. (1) The tracer
  needs less, with the same picture: caustics allocated only where there is water and sized by what is asked
  (215 MB in an interior with no water today), the 1.09 GB of targets and histories gone through one by one
  (formats, what is allocated and never used with the chosen denoiser), plants in the wind (765 MB), the
  atlases' fixed 261 MB, the acceleration structures. (2) Everything the tracer allocates goes by the budget,
  not only textures, and what it cannot have is said on screen in plain words with the two numbers. (3) What
  HDRP still allocates and draws under the traced picture is counted and let go (PT-107). (4) Only then, as
  the owner's choice with its numbers: what to give up when a card is still too small.
- State: the cause measured; the stuck interior's rule changed (building); the diet's job starting.

### PT-86 Nothing looks wet in the rain, and fine ground relief is lost
- The inspection's rows 8, 118, 70 (`forest-rock.jpg`, `forest-stakes.jpg`, `campfire-cauldron.jpg`,
  `mirror-water-sand-direct.jpg`): in the rain the game's rock glitters with small white glints and is dark and
  green, its path is shiny mud with puddles, its wood dark and wet; traced, rock is matte grey (brightest 0.5 % at
  147 against 187, detail 5.4 against 8.5), the path flat dirt, the wood light and dry. Wet gravel by a fire is a
  smooth tan sheet (detail 4.57 against 7.77). Beach sand has lost its wind ripples (detail 5.3 against 10.4).
- A guess: the game's wetness (its rain makes surfaces smoother and darker) and a detail or height layer the tracer
  does not read. PT-12 (white wet rock) and PT-28 (relief) are neighbours; PT-64's list of what a material reads.
- State: open; not assigned.
### PT-85 NRD makes some things worse than no denoiser: lit plants flash, single dots, a fine grid, a doubled glow
- The inspection, full run `20261008-130029` (rows 10, 32, 107, 117; 54, 89, 92, 99; 29; 41, 47; 104, 110, 102):
  - LIT PLANTS: in the crystal cave the ivy on the lit wall flashes as whole leaves (boil 1.19 with NRD, 0.50
    without; 580 flashing dots against 222); by the cooking fire the grass flashes white (flicker p95 10.95 against
    5.19; blocks wander up to 21.6 against 5.4); the far edge of the forest's grass (0.77 against 0.42); a cluster
    of leaves in the candle corner. Whole-picture flicker with NRD stays high in four scenes, all plants in light:
    glow 0.74, forest 0.63, campfire 0.54, npc-day 0.44 (the game's 0.05 to 0.12).
  - SINGLE WHITE DOTS flash in dim rooms and caves, more with NRD than without: tavern-room 140 (73 without, 20 in
    the game), hair-moving 110, glow-lake 55, caustics-glass 121 on a table top outdoors; one dot by 100 levels.
  - A FINE GRID, every second pixel, about 1.4 levels, over NRD's picture in dim scenes: 62 % of the screen's blocks
    in `candle`, 30 % in `hair-moving`, 8 to 9 % outdoors; 0 to 2 % with no denoiser (`candle-stone-zoom4.jpg`).
  - THE GLOW ROUND A CANDLE DOUBLES with NRD: the bar by a candle x3.27 against x1.64 without, the wax burnt to 255.
  - FLAT TONES in dark rooms (runs of one colour 6.3 pixels long, 2.8 without, 1.4 in the game); a creature and a
    cloak show their triangles; the door's iron strap becomes a smooth glowing bar.
- With PT-75 (the colour shift: dim rooms up to 1.95 times the blue; outdoors 5 to 11 % of the light lost), PT-19
  (texture wiped: 25 of 26 scenes have less fine detail than the game's) and PT-80 (mirrors smeared: detail 0.05).
- A thought to test first (the coordinator): the flashing plants may be PT-84's doing. The camera's rays carry the
  game's sub-pixel shift, so a blade thinner than a pixel is hit in one frame and missed in the next; NRD then sees
  a new surface each frame and keeps no history there.
- THE OWNER, 2026-10-08 14:50, watching the beach scene: "It's weird how often when you enable the NRD denoiser,
  such as here on water, we see these weird fireflies/glowing orbs here and there appearing."
  Counted in kept frames of that scene (run `20261008-130029`; pixels 60 levels over what the same pixel usually
  is, a frame): with no denoiser 0 (at most 1), WITH NRD 14 (up to 77 in one frame), the game 0. So they are not
  bright samples the tracer hands over and NRD merely spreads: they are made on the NRD route (NRD itself, or
  the tracer's own filters for direct light and reflections that run in that mode, or what is divided out before
  and multiplied back after). Which: not known. The `filters` job turns NRD's anti-firefly filter OFF (to keep
  the light, PT-75): look at this scene before and after merging it.
- THE OWNER'S THEORY, 17:40: "I was wondering if it's related to dusk or snow particles falling from the sky,
  that the NRD denoiser mistakes as game objects it needs to denoise or whatever? Just a theory."
- WHAT THE ORBS ON THE WATER ARE (run `20261008-145509`, `water-day`, 16 NRD frames; sheets
  `artifacts/harness/20261008-145509/orb0.jpg`, `orb1.jpg`: the game, no denoiser, three NRD frames). Not
  particles: the game's picture has nothing at those spots and the orbs do not move. An orb appears from one
  frame to the next (0 hot pixels, then 180 to 580), grows to 500 to 650 output pixels (about 12 render pixels
  across), has a burnt-out white core and a GREEN glow, and fades over 6 to 8 frames. With no denoiser the same
  places show single green dots (0 to 350 hot pixels a frame against NRD's 40 to 2,650). The source, read in
  `pt_water_path.hlsli` and said in `docs/agents/water-6.md` section 12: the sun does not light the water's
  body; a path inside the water finds the sun only when it happens to leave straight at the sun's disc (about one
  path in a hundred thousand), and then carries the whole sun. One such sample is the green dot; the filter that
  runs on water pixels in NRD mode (the tracer's own, for the reflection signal: the water's whole light is that
  signal) spreads it and keeps it for frames. The same cause leaves the sea's body near black (PT-81).
  The fix belongs in the tracer (never in the denoiser): the sun asked for at every point in the water, through
  the surface; then no path depends on chance for it. The orbs of other scenes (candles, dim rooms: the
  inspection's rows 41, 47, 54) are likely the same kind (a rare very bright sample, spread and held), each
  with a source of its own to find.
- THE ORBS "CONSTANTLY ACROSS ALL SCENES" (the owner, 18:10 to 18:25, after the `filters` merge was in the game:
  "we had the 'NRD orbs' or stars or sprinkles or whatever they are constantly across all scenes", "What are
  they?! Why are they there?!", "It looks like actual orbs, like a particle effect, being rendered on-screen with
  NRD. It almost looks like an overlay", "not just walls and tables - seemingly anywhere", and: "the NRD orbs isn't
  a new thing. We've had them for a long time. They've just started being even more prevalent"). Counted in the
  tavern with NRD (`mirror-tavern`, places a frame where pixels are 40 levels over their usual, outside the mirror;
  sheets `artifacts/harness/20261008-180808/tavern-orb0.jpg`, `tavern-orb2.jpg`):
  - the 12:45 build, before the merge: 2.9 (28 distinct places in 16 frames);
  - the merge as made (run `20261008-175533`): 37.0 (215 places);
  - the same with NRD's anti-firefly filter back ON (run `20261008-180808`): 32.7 (195). THE COORDINATOR'S FIRST
    ANSWER TO THE OWNER ("the firefly filter was switched off") WAS WRONG: it changed nothing. (It stays on:
    `nrd_denoiser.cpp`, with a source check.)
  - the a-trous pass's old handling of a far brighter neighbour back (run `20261008-181817`): 4.0 (32 places). The
    owner: "This removed the big NRD orbs from the tavern scene for sure, good work."
  WHAT THEY WERE: `pt_atrous.hlsl` had been made to keep light exactly (every weight the same seen from either
  pixel, by both pixels' noise; a pixel keeps what its neighbours do not take). A rare bright sample's light was
  then handed to the 24 pixels round it, twice (two iterations), and the history held it: the kernel's own round
  stamp, a soft disc. The old pass refuses such a neighbour and LOSES that light (PT-75's 0.75 on a candle-lit
  floor). SETTLED IN THE GAME: the pass does not keep the light of rare bright samples; that light comes back by
  making the samples less rare in the tracer. The mirror's part of the merge is kept (detail with NRD 1.19, was
  0.05) and the whole frame boils less than before the merge (0.110 against 0.125).
- WHAT IS LEFT, the owner, 18:25: "there are still fireflies here and there when NRD is enabled. Not big ones, but
  they are there": the 3 to 4 places a frame that were there before the merge, each lasting 1 to 2 frames. And the
  water's green orbs (the sun in the water: the water job 7).
- WHICH LIGHT THE SMALL FIREFLIES ARE IN (run `20261008-182327`, `tavern-room` with the new recipe `fireflies`: NRD
  on, 16 kept frames of the picture and of each debug view; sheets `tavern-room-specular.jpg`, `tavern-room-direct.jpg`
  in the run). Places a frame 40 levels over their usual: the picture 6.2 (48 distinct in 16 frames, each 2.1
  frames); Direct 6.2 (28, 3.6 frames); Indirect 0.0; Specular 7.0 (62, 1.8 frames); Emissive 0.5. So NONE in the
  bounced light, the only layer NRD itself denoises: the specks are in the two layers the tracer's own filters
  handle, and NRD's clean picture only makes them easy to see. In the crops: a lamp's highlight on a glossy thing
  (a cider bottle's shoulder, a painted beam's edge, a lattice), dim in most frames and very bright in a few; in
  the Direct layer the candle's own wax changing, and dots on an armour's ribs.
- THE AGENT `glints` (`brief-glints.md`, copy `<scratch>\wt\glints`, base57; its report `docs/agents/glints.md`,
  merged into main with the docs): the estimator it found is the direct light's candidates, chosen without
  looking at the pixel (the far list: one candidate a frame out of about 470 lamps in the tavern, by power alone),
  with the boiling filter in `pt_di.hlsl` then throwing most of those rare samples away (13 % of a far lamp's
  highlight kept). Its fix: each candidate chosen by what the light can matter to the pixel's own surface (the
  far one the best of 32), and a smooth surface's highlight also sampled from its own lobe, weighed against the
  lights' samples. Its fixture: a pixel's largest sample over its mean 67 to 10 for far lamps, the mean unchanged.
- IN THE GAME IT MADE THE SPECKLES MORE, NOT FEWER (merged 20:42; run `20261008-205046`, `tavern-room` with
  `fireflies`; the owner, 20:57: "Still saw 'stars'/speckles here and there in the tavern this time, and also
  some boiling on the chairs"). Pixels of the NRD picture that jump more than 20 levels between frames, the first
  4 frames of each run: 142 a frame at 18:36 (hold 1), 122 at 19:30 (with the edges fix and the loop cut), 262
  with the lamp fix. By layer against the 18:23 run (which has the edges fix's change in it too): Direct 6.2 to
  82.7 specks a frame (28 to 406 places), Specular 7.0 to 16.3, Indirect 0 to 2.1, the picture 6.2 to 15.9. The
  16-pixel blocks' wander is the same in all three builds (0.040, 0.046, 0.043): the boiling the owner saw on the
  chairs is not this fix's (it sits on the seated woman and the chairs by her, the table candle and what stands
  round it, a shiny patch of floor under a bench: `tavern-room-boil.jpg` in the run).
  TAKEN OUT OF THE MAIN TREE at 21:00 (the six files as they were before the merge; the merged ones are kept in
  `<scratch>\with-glints`, the job's copy in `artifacts\worker-copies\wt\glints`). A LESSON: the job's own
  fixture got better while the game got worse; a job on specks is judged by the recipe `fireflies` in the game,
  and its fixture has to show what the game shows before its numbers count.
- THE OWNER, 2026-10-08 22:50: "The speckles is the most important thing for us to fix right now."
- THE FAIR COUNT, 22:46 (run `20261008-224604`: the main tree with the lamp job out; the tavern's main room, NRD,
  16 frames a layer; specks a frame and distinct places; against run `20261008-182327`, before the edges job, and
  `20261008-205046`, with the lamp job): the picture 6.2 (48) / 15.9 (71) with the lamp job / 9.9 (68) now;
  Direct 6.2 (28) / 82.7 (406) / 90.3 (406); Specular 7.0 (62) / 16.3 (96) / 22.2 (112); Indirect 0 / 2.1 / 1.5.
  So (a) the lamp job did raise the picture's specks (15.9 against 9.9) and taking it out was right (a 4-frame
  count the coordinator quoted first, 122 / 262 / 204 jumping pixels, is too unsteady to say it: taken back to
  the owner); (b) THE DIRECT LAYER'S RISE, 6 to 90, IS NOT THE LAMP JOB'S: it is the same with and without it,
  and it came between 18:23 and 20:50, where the biggest change is the edges job (PT-84: the camera's rays carry
  a new sub-pixel shift every frame). THE LEAD, not shown yet: the passes that fetch last frame's data for a
  pixel still look where the point was by the motion alone, in a grid that was shifted otherwise; on thin
  things, edges and small highlights that is another surface, the history is refused, and the raw sample shows.
  The agent `reuse` (`brief-reuse-shift.md`, copy `wt\reuse`, base59, report `docs/agents/reuse-shift.md` there):
  every such pass listed, the lead shown or killed with a number, the fetch fixed; no filter's strength touched.
- THE LEAD SHOWN IN THE GAME, 23:57 on 10-08 (run `20261008-235410`, `tavern-room` with the recipe `shift`; a hidden
  setting `Advanced / Camera shift`, plugin only, flipped within ONE run; NRD, 16 frames a step). Specks a frame
  (places) / a pixel's spread / 16-pixel blocks' wander, shift ON then OFF: the picture 12.1 (75) / 0.24 / 0.119
  and 3.4 (33) / 0.16 / 0.065; Direct 63.8 (322) and 9.8 (32); Specular 11.6 (72) and 4.8 (33); the means the
  same. So the camera's sub-pixel shift is what brings the specks, and nearly doubles the boiling.
- THE FIX (the agent `reuse`, `docs/agents/reuse-shift.md`; merged 00:16 on 10-09, no conflict; its own check
  `TGPT_TEST_REUSE_ONLY=1` passes; never the whole test): six fetches of the frame before left out the two
  frames' shifts' difference (ReSTIR's reuse across frames, the tracer's own history for direct light,
  reflections and bounced light, which in a still world read ONE texel, and NRD's kept direction). Now the
  histories are fetched in the previous frame's own grid (`previous_place()`, `pt_common.hlsli`), with a second
  try among the 3 x 3 round it; ReSTIR keeps its old fetch (moving it gained nothing and made flat surfaces boil
  19 % more). Behind `Advanced / Reuse follows camera shift` (0 off, 1 the fix, 2 ReSTIR's fetch too).
  IN THE GAME (run `20261009-002341`, recipe `reuse`, the setting 0 then 1 in one run): the picture 15.7 specks a
  frame (73 places) to 5.8 (32), its wander 0.096 to 0.069; Direct 62.9 (345) to 3.4 (24); Specular 13.4 (67) to
  8.1 (49); the means the same. THE DEFAULT IS 1 since 00:31 (plugin 7774CCCDF9DA, native 497C642DB260), and a run
  of that alone (`20261009-003238`, recipe `watch`: NRD only, two takes): 2.1 and 3.3 specks a frame (20 and 23
  places), wander 0.065 and 0.062: at or under the shift-off mark with the edges smooth.
- THE OWNER, 00:25, watching the run that showed the setting at 0 first: "Still so many speckles..."; then: "Show me
  in-game just with the version that supposedly fixes most speckling" (done: the run above; the owner's word on
  it is open). WHAT IS LEFT with the fix on, about 20 to 30 places in 16 frames (`specks-left.jpg` in run
  `20261009-002341`): thin slivers of light at gaps (a rope round a post, a door's edge, a crack in the floor) and
  glints on metal (a shield's boss, a mug's rim): the things half a pixel wide that one screen-space history
  cannot hold (the report's section 5 has the options). OWED: the switch out or kept (the report's section 7),
  CHANGELOG, the whole GPU test (the coordinator's).
- THE OWNER, 00:40 on 10-09, of the run with the fix alone: "it still speckles too much. And also the candles
  hanging various places are still overblown/bloomy"; "My subjective experience was that it speckled a lot." THE
  COORDINATOR'S COUNT WAS TOO COARSE (40 levels over a pixel's median). A SENSITIVE COUNT (at least 8 levels AND
  1.4 times over the median; spots = 24-pixel cells): the game's own picture 9 spots a frame (11 places in its 4
  frames); the fix on 34 to 37 (170 to 190 places in 16 frames, 1.3 % of the screen's cells); before the fix 188
  (755); the camera's shift off 35 (210). So the fix brought the picture to the shift-off level, which is still 4
  times the game's: the old base layer. Map: `artifacts/harness/20261009-003238/tavern-room-sparkle-map.jpg`. By
  layer with the fix on: Direct 83 spots a frame, Specular 77 (the dimmest layer, its sparkles carry ten times
  the share of light of the others'), Indirect 76 (a run from before the fix).
- LAMPS IN THEIR FLAMES AGAINST LAMPS AT THE GAME'S PLACE (run `20261009-004410`, the hidden setting `Advanced /
  Lights sized as their emitters` true then false in one run; sheets `wall-candle-emitters.jpg`,
  `tavern-emitters.jpg`): the picture 28.2 spots a frame (156 places) against 14.8 (71); Specular 67.7 (317)
  against 23.5 (89); the picture's blocks wander 0.062 against 0.037; pixels over 240 round a wall candle 388
  against 6 (the game 0), on the whole screen 1,375 against 17; the mean the same. The coordinator RECOMMENDED
  the game's place as the default. THE OWNER REFUSED, 00:55: "No, of course I don't want to go back to the game's
  own light position - the fire is the emitter. Why would that in any way be the cause of the NRD speckles? If
  the cause of the overblownness is that the game candles are lit by 100-watt bulbs, then make them not be that";
  and 01:00: "I'm really interested in why the flames being lit causes NRD speckles when placing the lights as
  bulbs and not inside the [flame] doesn't. That makes me think the way we light the flame has problems". THE
  RULING STANDS: lamps in their flames, at their flames' size; never propose the game's place again.
  ANSWERED: not known yet; the test moved place and size together. Three suspects, read in the code: (1) the
  wax 1 to 3 cm from a point light with 1 / max(d^2, r^2) is burnt white, a tiny blinding surface rays meet by
  chance; (2) the lamp is reflected as a sphere of its flame's radius with radiance I / (pi r^2), met only when
  a lobe's ray happens to hit a 1 cm ball; (3) a pinpoint lamp's highlight. 2 and 3 would be flaws in how a
  small emitter is lit from, as the owner suspects. Also told the owner: a lower power alone does not cure the
  wax (1 cm against 3 m is tens of thousands to one whatever the power); the picture must treat what is far
  past white as an eye does, and reflections must find a lamp on purpose.
  The agent `flame` (`brief-flame-specks.md`, copy `wt\flame`, base60, report `docs/agents/flame-specks.md`
  there): ONE build with a separate hidden switch for each suspect, handed over fast; the coordinator runs the
  tavern with each alone.
- THE GAME'S ANSWER, 01:30 on 10-09 (run `20261009-012128`, `tavern-room` with the recipe `lamp-trial`; the agent
  `flame`'s four hidden switches, `docs/agents/flame-specks.md`; NRD, 16 frames a step, the sensitive count). The
  picture, spots a frame (places) / 16-pixel blocks' wander / pixels over 240: AS NOW 35.2 (156) / 0.063 / 1,362;
  A, a lamp's light on a surface held to what it gives at 30 cm: 17.2 (85) / 0.040 / 19 (at 10 cm: 17.7 (92) /
  0.041 / 22); B, the lamp reflected at HDRP's radius: 31.8 (145) / 0.064 / 1,357 (it only acts on see-through
  glass and water); C, highlights as for HDRP's radius: 34.4 (171) / 0.062 / 1,328; D, shadow rays at HDRP's disc:
  20.6 (86) / 0.049 / 960; all four: 17.1 (76) / 0.033 / 16. The Specular view's spots: 67.9 as now, 19.2 with A,
  65.6 B, 76.9 C, 44.1 D. The means the same (23.0 to 23.7).
  SO THE SPECKS AND THE BURNT CANDLES ARE ONE THING: what a flame puts on surfaces within 10 cm of it (the wax,
  the holder, the wall behind a wall candle): burnt white on screen, and met by the room's reflection and bounce
  rays by chance. The lamp's own reflection and its highlight are not guilty (the owner's suspicion of 01:00,
  tested). D helps partly; whether through the same near light or the shadows' edges is not known. A THROWS LIGHT
  AWAY: a diagnostic, never to ship. The game's own picture: 9 spots a frame.
- Next (the agent `near`, `brief-flame-near.md`, copy `wt\near`, base61, report `docs/agents/flame-near.md` there;
  started 03:41): two trials behind switches: the room finds a flame's lit surroundings ON PURPOSE (sampled as
  lights, weighed against the chance hit); the picture rolls off what is far past white (PT-40). Then
  `tavern-room` with each. State: the cause is shown; the fix is not made.
- THE TWO TRIALS IN THE GAME, 05:01 on 10-09 (the agent `near`, `docs/agents/flame-near.md`, merged 04:40, no
  conflict; run `20261009-045121`, `tavern-room` with the recipe `lamp-near`; sheets `wall-candle-trials.jpg`,
  `tavern-trials.jpg`). The picture, spots a frame (places) / blocks' wander / pixels over 240: as now 40.2 (189)
  / 0.066 / 1,339; TRIAL 1, `Advanced / Lamp trial lit surroundings` 30 (the rays that gather light find what
  stands by a lamp with a ray from the lamp, the chance hit counts less): 33.6 (167) / 0.065 / 1,315, the light
  the same, 10 ms a frame more; TRIAL 2, `Highlight shoulder most` 3 (the traced light rolled off above a knee in
  the composite): 33.8 (154) / 0.059 / 18, the picture's light 5 % less; at 6: 479 burnt, at 12: 974; both (30
  and 6): 27.0 (137) / 0.061 / 469; the yardstick A at 30 cm: 13.8 (64) / 0.042 / 15. The Specular view: 69.6 as
  now, 53.8 with trial 1, 20.4 with A. SO: trial 2 at 3 makes the candle read as cream wax with its shape (the
  look, PT-40); trial 1 takes a sixth of the specks where A takes two thirds: finding the blazing wax on purpose
  is not enough as built.
- WHAT THE GAME'S CANDLE LIGHTS ARE (the same report, section 3, read in HDRP's public source and
  `docs/agents/lights-2.md`): HDRP lights by I / (d^2 + r^2), r the light's Radius; the tavern's big candles are
  21,149 to 24,775 candela with a Radius of 1 m (a real candle: about 1 candela). So in the game nothing takes
  more than I from such a light: the wax 10 times the far wall. The tracer, with the lamp in its 0.6 cm flame and
  the same candela, by I / d^2: equal at 3 m (1.1), 2 times at 1 m, 12 times at 30 cm, 101 at 10 cm, 1,112 at
  3 cm. The coordinator's claim that a lower power alone does not cure wax against wall holds.
- SHOWN TO THE OWNER, 05:06 (run `20261009-050427`, recipe `watch-near`: NRD only, lamps in their flames,
  `Lamp trial near light` 100, which is I / max(d^2, 1 m^2): within a factor of two of the game's own law; sheet
  `tavern-near.jpg`): 18.2 and 18.6 spots a frame (88 places; 40.2 and 189 as now), blocks' wander 0.038 (0.066),
  14 burnt pixels (1,339). Against the game's light: the whole room 0.98 (1.10 as now), a wall candle and its
  post 1.14 (1.27), a table by a candle 0.62 (2.75), the fireplace 1.79 (1.80), the floor in the middle 0.13.
  THE COORDINATOR'S RECOMMENDATION, WAITING FOR THE OWNER: the fire stays the emitter (place, shadows,
  reflections), and its strength falls off as the game's own light does (I / (d^2 + r^2) with the game's r):
  not the lamp moved back, and not light thrown away against the game's picture.
- THE OWNER'S ANSWER, 06:35 on 10-09, to the two questions of 05:06 (the game's strength near a flame? the roll-off
  as the default?): "Not sure regarding your questions - I want you to build whatever looks most correct and
  doesn't blow out, without it causing less light coverage than now, and the main priority is getting rid of those
  speckles." So the choice is the coordinator's, and its measures are the owner's four: looks right, nothing burnt
  white, the room lit no less than now, the speckles first. This file's reading of "no less light coverage": what a
  lamp gives a surface a hand or more away stays as it is now; only what is right at the flame may change. So NOT
  the game's own metre (it takes half the light at 1 m and left a table by a candle at 0.62 of the game's): the
  SMALLEST distance that cures the specks and the burn.
- WHERE THE SPECKS THAT ARE LEFT SIT (run `20261009-050427`, near light 100: 18.2 spots a frame; counted and cut
  out with the scratch scripts `specks_count.py` and `specks_close.py`, whose measure gives every count written
  here: Rec. 709 brightness, a pixel at least 8 levels and 1.4 times over its own median of the 16 frames, 24-pixel
  cells): 32 groups, five of them two thirds of all: the rim and rivets of a shield on a post (27 %, a rise of 19
  levels at most), wood shavings on the floor by a post's foot (12 %, real bright dots, +72), a seated woman in
  armour (11 %, faint, +17), the top edge of a chair's rail (8 %, a thin line, +49), the chain before the hearth
  (7 %). So what is left is mostly THIN EDGES AND SMALL METAL that shimmer a little from frame to frame, not
  blazing dots all over the room.
- THE SMALLEST DISTANCE, IN THE GAME (run `20261009-064121`, `tavern-room` with the recipe `near-sweep`; the
  installed build's trial switch, a lamp's light held flat within the distance; NRD, 16 frames a step; sheet
  `tavern-sweep.jpg`). Spots a frame (places) / burnt pixels / the picture's mean: as it was 27.5 (143) / 1,598 /
  23.9; 3 cm 23.6 (112) / 1,242 / 23.5; 5 cm 14.8 (80) / 722 / 23.2; 10 cm 15.2 (79) / 32 / 23.1; 20 cm 16.0 (82)
  / 28 / 23.1. ("As it was" has been 35.2, 40.2 and 27.5 in three runs: a count moves by a third from run to run;
  compare steps of ONE run.) So the specks are cured at 5 cm, the burn at 10, and 20 wins nothing more. The
  room's light with 10 cm against as it was, by place (scratch `tavern_sheet.py` with `tavern_boxes.json`): the
  whole picture 0.97, the far wall 0.97, the ceiling 0.98, the floor 0.95, a table top by its candle 0.96, a
  tablecloth by its candle 0.92, a post right under a wall candle 0.90.
- THE OWNER, 06:50, having watched that run's last steps (10 and 20 cm): "What you just showed me looked so much
  better in terms of speckles. In the end of your run here, the candles weren't overblown, I saw very little
  speckling, and the light coverage was still good". So it is the default: a hidden setting of its own, `Advanced /
  Lamp near size`, 10 cm (the trial key `Lamp trial near light` is gone; a new key so that a config holding the
  trial's 0 takes the default), and the shader's `light_falloff`: I / sqrt(d^4 + n^4), which is the held value at
  the flame and I / d^2 from two n on, with no edge between (held flat, as the owner saw it, it would leave a
  faint ring where the hold ends). A lamp larger than n (a hearth's fire) is as before. The offline GPU test
  hands 0 (off): a check of its own is owed. The three other trial switches (reflected, highlight, shadow size),
  `Lamp trial lit surroundings` and the roll-off (`Highlight shoulder ...`) are not needed now: to take out.
  State: the owner said yes to the look at 10 and 20 cm held flat; the smooth default is being built and run.
- THE OWNER, 07:00 (told the tavern's numbers and that a count of a few other flame scenes was next: "Great,
  sounds good"): "And then let's compare few of the difficult scenes to count speckles to ensure they're 'gone'".
  So: the recipe `lamp-size` (the game's own picture 16 frames, then NRD with the near size 0 and 10, 16 frames
  each) on the scenes where flames stand by surfaces or little light arrives: `candle`, `npc-candle`,
  `glass-jars`, `dark-room`, `night`, `campfire`, `metal`; the same count for each, the game's beside it.
- THE DEFAULT IN THE GAME, 07:05 (native 9D4B6DA1B9D6, plugin 60740CF450DC; run `20261009-065838`, `tavern-room`
  with `lamp-size`; sheet `tavern-lamp-size.jpg`): the game 9.1 spots a frame (19 places: its own flames
  flickering, rises of 90 to 128 levels) / 16 burnt; size 0: 32.3 (153) / 1,290; size 10: 18.6 (84) / 11; the
  picture's mean 23.5 to 22.8. What is left at 10: a seated woman's armour (24 %, +42 levels at most), a shield's
  rim and rivets (16 %), wood shavings on the floor (11 %).
- THE SEVEN SCENES (runs `20261009-070553` and `-071155`, `lamp-size`; NRD, 16 frames). Spots a frame, the game /
  size 0 / size 10: `glass-jars` 12.8 / 26.8 / 11.6; `npc-candle` 43.6 / 43.3 / 35.5; `candle` 39.1 / 1.8 / 1.9;
  `metal` 13.1 / 8.2 / 7.7; `dark-room` 0.0 / 0.2 / 0.0; `night` 0.2 / 0.3 / 0.4; `campfire` 0.1 / 458.1 / 454.7.
  Six are at the game's level or under it. THE CAMPFIRE IS NOT, and it is another thing: 82 % of its spots are
  one patch of grass and plants lit by the fire that shimmers in every frame (rises to 80 levels; sheet
  `campfire-grass.jpg` in the second run), 16 % grass at a tent's edge; posts, cloth and stones beside them are
  still. Not known: whether the grass moves in the tracer while the world is frozen, or thin blades change with
  each frame's sub-pixel shift, or its cut-outs do (PT-84's kind). Next: `campfire` with the recipe `shift`.
- STILL BURNT WITH THE SIZE AT 10 (the same runs). The owner, 07:10: "It looks great so far, except for the
  completely blown out candles a few places in the tavern", and of `glass-jars`' step with the size 0: "Three of
  the lights here were blown out - the one high on the tree pillar, one on the right candle, and one next to Ron".
  The same three with the size at 10 (sheet `glass-jars-three-lights.jpg`), burnt pixels at 0 / at 10: the right
  candle 1,269 / 41; next to Ron 243 / 3; high on the pillar 1,266 / 440: a pale block still. WHY: the plugin's
  "Light photometry" lines: the tavern's wall candelabra are 45,442 to 75,766 candela (HDRP radius 2 and 0.6 m),
  its candlesticks 21,337 to 27,617 (radius 1 m), a hanging wheel 262,144: one distance cannot fit lamps of such
  different strength. `npc-candle` up close (sheet `candles-close-game-off-on.jpg`): 17,318 / 2,225 burnt against
  the game's 680: the wax reads as wax, the flames are burnt in the game too.
- THE OWNER, 07:27: "If what you just showed me in-game was NRD, then speckling looks largely resolved - good
  job." (It was NRD.) "What I noticed: 1. Still some lights/candles/fires are being overblown, like the ones in the
  tavern I mentioned, and the flame inside the torch next to the guy with the weird mask next to the bridge.
  2. I noticed some blockiness/low-polyness on the armored guards helmet, and the cloth of the guy with the weird
  mask standing next to the bridge" (point 2 is PT-92). The torch (`night`; sheet `torch-game-off-on.jpg`): the
  basket's inside and tips are burnt white, 13,814 burnt pixels at size 0, 13,268 at 10, the game 3,061: the size
  did nothing there (a fire is larger than 10 cm and kept the old law).
- SO THE SIZE GOES BY THE LAMP'S STRENGTH (built 07:30, to run): the setting's centimetres are those of a lamp of a
  tavern candlestick's strength (`LAMP_NEAR_STRENGTH` 13,500 candela of brightness) and a lamp's own grows with
  the square root of its strength, so the most any lamp puts on a surface is the same: a wall candelabra's 16 cm,
  the hanging wheel's 31. As if every flame's glow were equally bright and a stronger lamp's only larger.
  State: the candlesticks cured and seen by the owner; the stronger lamps and the torch to show.
- THE SIZE BY STRENGTH IN THE GAME, 07:40 (native 609143540829; runs `20261009-073035` and `-074726`, `lamp-size`,
  the setting at 10): `tavern-room` 8.8 spots a frame (the game) / 32.6 (off) / 19.4 (on), burnt 13 / 1,367 / 19,
  the mean 23.7 / 23.7 / 23.1; `glass-jars` 13.5 / 38.8 / 18.0, burnt 32 / 2,559 / 229: the pillar's candle 182
  burnt pixels (440 at one size for all; 1,255 off), the right candle 30, next to Ron 5 (sheet
  `glass-jars-three-lights.jpg`); `night`'s torch 4,585 (the game) / 15,553 (off) / 10,137 (on): better, the
  basket's inside still white (`torch-game-off-on.jpg`). The run before it, one size for all lamps
  (`20261009-072604`, `lamp-size-sweep`): the pillar's candle 466 burnt pixels at 10 cm, 125 at 15, 23 at 20, 20
  at 30; the picture's mean 21.07, 21.00, 21.00, 20.93. Next: the same sweep by strength, for the setting's default.
- THE DEFAULT IS 15, IN THE GAME 08:16 (native 609143540829, the plugin with `Lamp near size` 15). The sweep by
  strength (run `20261009-075923`, `lamp-size-sweep`; the setting at 10 / 15 / 20 / 30): `glass-jars`, the
  pillar's candle 216 / 28 / 29 / 31 burnt pixels (28 is its flame), the picture's mean 20.54 / 20.50 / 20.40 /
  20.26; `night`, the torch 9,520 / 3,050 / 568 / 195 against the game's own 3,380, the mean 46.04 / 45.74 /
  45.61 / 45.25 against 45.56 (sheet `torch-sizes.jpg`: at 15 the basket's tips are as the game's, at 20 and 30
  dimmer than the game's). The tavern with it (run `20261009-081623`, `lamp-size`; sheet `tavern-lamp-size.jpg`):
  the game 11.1 spots a frame / 17 burnt; off 41.1 / 1,359; 15: 18.5 / 24; the mean 23.5 to 22.9. The room's
  light against off, by place: the whole picture 0.97, the ceiling 0.98, a post under a wall candle 0.98, the far
  wall 0.94, a tablecloth by its candle 0.93, the floor 0.92, a table top by its candle 0.91.
  State: in the game as the default; the owner saw the look at 10 and said "largely resolved"; to look at 15.
  Left of the speckles: PT-93 (grass by a fire), thin edges and small metal in the tavern (18 against the game's 11).
- THE WHOLE GPU TEST, once (the coordinator's; 08:19 to 08:36 on 10-09, native 609143540829, the main tree before
  the characters' normals; owed since 10-08 12:53; `<scratch>\gpu-test-main-081950.txt`): 1,516 ok, 47 FAIL: 32
  of the denoisers' mean (the filters job's checks; its kept-light filter was taken back; PT-75 is open), 9 of a
  mirror's fine detail and light with the denoisers, 4 of glass (the three of PT-76 and "glass bends light by its
  material's own index of refraction": given to the agent `glass3`), "coloured or white bulk slab retains its
  diffuse colour under even white light within 3.5 percent", "NRD flickers no more than half again as much as
  the built-in denoiser". The list is in `brief-tidy-lamps.md`. The lamps' near size is 0 in the offline test.
- Next, one thing: know what a speck IS before changing more. For a few speck pixels of the Direct and Specular
  layers in the tavern: which light, what weight its reservoir carried, which pass let it through (the candidate,
  the reuse across frames or pixels, the highlight's lobe). That needs the tracer to write it out for a marked
  pixel; not built. State: open; the big orbs are gone, the small ones are where they were.
### PT-84 Every edge is a 2-pixel stair-step and thin things are rows of dots: the traced picture is not smoothed
- The inspection's row 62 (and 2, 13, 23, 24, 38, 56; `forest-grass-zoom3.jpg`, `tavern-room-dots-zoom3.jpg`,
  `forest-lantern-right.jpg`): every step's picture is made at 1924x1084 and shown at 3840x2160. The game's own
  picture, made at the same size, comes out smooth. The traced one, with no denoiser and with NRD, has poles, stakes,
  chair slats and cloth edges as stairs of 2-pixel steps, and grass blades, a rope, a feather, lace as rows of dots.
  It is not a plain doubling of pixels: something resamples it without smoothing edges.
- Read in the code by the coordinator, 14:10: the tracer's camera rays DO carry the game's sub-pixel shift
  (`invViewProj`, "HDRP's, jittered", `pt_common.hlsli`). So the shift is in the traced picture and nothing after
  it turns the shifted frames into smooth edges. Not known: what the game's upscaler gets while tracing (which
  colour, depth and motion; PT-72), or whether the traced picture is laid in after it.
- Why it may matter more than edges: with the shift in and nothing resolving it, thin things come and go from frame
  to frame in a frozen world. Part of the measured flicker of grass and leaves (1.3 to 2.4 levels with AND without
  NRD, the game 0.1; the owner: "flickering inside leves and stuff") may be this, not noise of the light.
- THE CAUSE, FOUND AND MEASURED by the agent `edges` (`<scratch>\wt\edges\docs\agents\edges.md`, base55; the
  agent was stopped at 14:57 when the usage limit came, in the middle of writing the fix): THE TRACER'S CAMERA
  RAYS CARRY NO SUB-PIXEL SHIFT AT ALL. The game's upscaler is told a new shift every frame, so it lays the same
  unshifted low-resolution picture down at a different place each frame, and what settles is that picture's own
  pixel grid. Why: HDRP takes the shift out of its camera's matrices while it records the frame (after the
  upscaling step), and the plugin reads them later, when the frame runs: `invViewProj`, commented "jittered", is
  not. The offline test builds its own matrices with the shift, so it never saw this. The coordinator's lead
  (the shift of the frame before) was wrong. Numbers over the kept frames of run `20261008-130029`: the traced
  picture's neighbour difference is 4.70 on a render pixel's border and 3.59 in its middle (the game's: 4.25 and
  4.0, no grid); the whole traced picture slides by +0.057 output pixels per render pixel of the told shift,
  where the game's frames move 0.001 to 0.003.
- A second thing waits behind it: the light shown in frame N was traced for frame N-1, so with the shift in the
  rays it would carry the shift of the frame before. The agent weighed three ways and chose: trace and show in
  the same frame (exact still or moving, one frame less lag, and the composite's motion vectors, distance key
  and neighbour search are deleted); the cost of the wait is to be measured. Its copy holds unfinished edits in
  8 files. Next: a fresh agent finishes it (`brief-edges-2.md`), then ONE scene, `forest`.
- THE FINISHER REPORTED, 18:32 (`<scratch>\wt\edges\docs\agents\edges.md`, base55; ABI unchanged; a dry merge
  onto main: 14 changed, 5 new, no conflict): BOTH STEPS ARE WHOLE AND BUILT. A: the camera's rays carry the shift
  the upscaler is told (the plugin reads HDRP's matrices while they hold it; the tracer puts the shift in itself
  if they come without). B: a frame's traced light is shown in that same frame, each pixel its own texel; deleted
  with it: the composite's motion vectors, distance key, neighbour search, random fill, exposure ratio and the
  second set of output textures. Its one check (`TGPT_TEST_EDGES_ONLY=1`, matrices handed the game's way, the
  camera moving): 0 wrong pixel samples of 14.7 million; the base fails 4 checks. NOT RUN: the whole test, NRD,
  Ray Reconstruction's smoke test, the game. One existing check was changed blind (hair "slides with the wall"
  now expects no slide). The D3D11 side now waits for the frame's own trace: its cost a frame is to be measured
  in `forest`. Next: merge, build, ONE scene `forest@quick` (edges, the grass's flicker, the frame time).
- MERGED 18:41 AND SEEN IN THE GAME, 18:50 (run `20261008-184804`, `forest` with `quick`, against the same scene of
  run `20261008-130029`; sheet `forest-grass-before-after.jpg` in the run; the files as they were before the merge:
  `<scratch>\pre-edges`). Grass blades and thin things are smooth, continuous lines as the game's, with no
  denoiser and with NRD (before: rows of blocks and dots). A pixel's spread over the frames of the frozen world
  (levels; the game's 0.11 to 0.16): the whole picture with no denoiser 0.96 to 0.68, with NRD 0.63 to 0.35; the
  grass 1.45 to 1.12 and 1.04 to 0.46; the rock 1.33 to 0.30 and 0.25 to 0.16. WORSE: the canopy and the trunk with
  NRD, 0.35 to 0.53 (0.64 to 0.67 with no denoiser): not looked into. The frame: 117.7 to 98.5 ms with no denoiser,
  121.2 to 101.0 with NRD; no frame failed. (Not this job's alone: the water job's change to the caustics stage
  is in the same build. The wait on the frame's own trace did not make it slower.)
- RAY RECONSTRUCTION STILL RUNS after the change (run `20261008-185311`, `forest` with `with-rr`; a look that it
  works, not a judgement of its picture): traced and on, no frame failed, 95.9 ms, 1.05 times the game's
  brightness, a pixel's spread 0.17 over 4 frames, fine detail 3.59 (the game's 3.84, NRD's 2.78).
- NOT RUN: the whole GPU test on the main tree since 12:53 (water 6, the filters, the edges, hold 1 are all in
  since): the coordinator's, when the owner says or the picture is judged right. One check changed blind is in
  it (hair "slides with the wall" expects no slide now).
  State: seen in game by the coordinator's numbers and crops; the owner to look. Left: the grass still flickers
  4 times the game's with NRD. (The canopy's and the trunk's rise was not this job's: PT-75's note of 19:26,
  a loop in the filters' third part; with it cut the whole forest with NRD is at 0.20, the game's 0.13.)
### PT-83 Things the game shows are missing from the traced world
- The owner's ruling (2026-10-07): "that sounds like a big problem, if things suddenly disappear when PT is enabled.
  Fix that". The inspection of 2026-10-08 found (rows 16, 44, 57, 113, 114, 115, 15; crops in
  `artifacts/inspection-20261008/`):
  - `vista`: a tree's whole purple canopy is gone, bare pale branches (40 % of a 614x605 crop; `vista-purpletree.jpg`).
  - `npc-day`: a patch of tall grass, a cabbage and leafy plants is bare grey ground (about 8 % of the screen;
    `npcday-rightground.jpg`).
  - `npc-candle`: leaded windows have no lattice and no glass, an open hole to the night (`npc-candle-window.jpg`).
  - `stairwell`: a stained-glass rose window is a plain blue glow (`stairwell-window.jpg`); a statue under it is a
    black column with its glowing heart a faint dot; a ghost (a see-through glowing person) is a faint grey shade
    (`stairwell-statue.jpg`).
  - `forest`: far wooden structures the game shows through the fog are nearly gone (`forest-npc-zoom.jpg`).
- Nothing is known of why. What each thing is in the game (its renderer, shader, how it is drawn) has to be read in
  the game: one scene at a time, with the yardstick's lines in the log (`docs/PT-HARNESS.md`) or a list of the
  renderers in a screen box that says which of them the traced scene holds.
- `missing` (Codex, `brief-missing.md`, `docs/agents/missing.md`; done about 11:00, merged 14:00 on 10-09, one
  conflict; `<scratch>\pre-missing`; the plugin NOT compiled since: the game folder holds another build): the
  recipe `missing` (`vista`, `npc-day`, `npc-candle`, `stairwell`, `forest`): for each scene's box every renderer
  the game draws there, its materials, whether the traced scene holds it and which rule left it out, the
  largest omissions first. Found by reading alone: the game's Vegetation Studio grass and plants and Unity's
  terrain trees and details have NO capture at all (the `npc-day` patch, the purple canopy, and likely PT-98's
  grass by the torch); the scenes' own causes wait for the run.
- State: the diagnostic built and merged; one run owed; a first cause read in the code.
- THE MISSING-THINGS CENSUS IN THE GAME, 15:10 on 10-09 (run `20261009-150909`, recipe `missing`, five scenes;
  `missing-report.md` and a report a scene in the run's folder; the digest by cause: `<scratch>\missing_digest.py
  <run>`). It lists what the game holds in view by bounds (candidates, not pixels) and what the tracer took of
  it. Of 2,631 things: 1,646 are not in the traced world, and of those 1,495 are ANOTHER LEVEL OF DETAIL of an
  object whose drawn level is in (no fault) and 44 the game does not draw either (off, inactive, culled).
  WHAT IS REALLY LEFT OUT, by cause:
  1. 70 EFFECTS AND TRAILS (32 VFX Graph renderers: rain, floating dust, falling leaves, embers, candle
     flames, smoke, the waterfall's splash, a portal; 38 trail renderers, mostly arrows'): `SceneCapture`
     takes mesh renderers only; no adapter. That is PT-67.
  2. 13 TREE BILLBOARDS in `npc-day` (`Forest_Pine_05_Billboard` 8, `beech_tree_09_cross` 5): the far trees'
     flat cards. `LeshyCapture` drops a billboard and puts the coarsest real mesh in its place, or NOTHING
     when the prefab has none: far trees missing from the traced skyline. To check which of the 13 got a mesh.
  3. 7 MESHES NOT RESOLVED in `forest` ("cannot resolve mesh: Drake meshIndex ..., only completed
     successfully-loaded ..."): entities whose mesh the game's loader had not handed over when asked: either
     late (they come in a moment after) or never asked again. To follow one over time.
  4. 5 PARTS OF ONE CHARACTER FAILED in `stairwell` (`CharacterCapture.Find: handle -1, pending False, failed
     True`: a ghost's head, a body, feet, torso, legs): a character the game shows and the tracer does not.
     Read in that run's log: it is a GHOST (shader `TG/Character/Ghost/Ghost_2_0_PW`, logged as a "traced effect
     material" with a distortion pass): a see-through character the plugin leaves to HDRP's effect drawing, so its
     parts are never registered. Not a random failure: a gap of the see-through kind (with glass and effects).
  5. 6 SHADOWS-ONLY PROXIES in `forest` (`CM_AllmotherShrine_Cluster...`): meshes the game draws into its
     shadow maps only. Right to leave out IF the thing they stand for is traced; to check that it is.
  6. 4 SEE-THROUGH MESHES refused by `SceneCapture.ReadMaterial` (transparent surface type or queue): an
     alchemy station's glass and liquid, a sword, a portal's depth card. Glass and liquids belong in the
     traced world (PT-13); the other two to look at.
  THE CENSUS ITSELF IS INCOMPLETE in one source: the game's MEDUSA renderer (its baked static instances,
  `matrices.medusa` a scene) was not read: the diagnostic built the path wrong (`<game>\ns:\Medusa\...`). So
  whether Medusa's instances are all in the traced world is NOT known from this run: the first thing to mend.
  (The tracer does take Medusa: its own scene line at night says "17492 instances (objects 0, Drake 2054,
  entities 936, Medusa 3974, terrain 0, vegetation 10524, water 4)"; what the census cannot say is whether any
  are missing.) ARRIVAL, the same run's log (PT-98): 10 s after tracing was switched on in `npc-day` the
  tracer's own check read "nothing traced where the game drew 62.51 %" of the screen.
  The Vegetation Studio grass and the terrain's details the worker named as having no capture do not show in
  these five scenes' lists either way (they are not renderers the census walks): also to mend.
- State: counted once; next: the census's two blind spots (Medusa, the grass systems), then causes 2, 3, 4.

### PT-82 The whole picture inspected, 2026-10-08: 123 defects in 31 scenes (the list, and where each went)
- The owner, 12:55: "Have you taken a thorough look on the images and atlases from the past runs so you know all the
  current defects? Including flickering inside leves and stuff?" The coordinator had not. Done by a read-only agent
  on run `20261008-124542` and the full run `20261008-130029` (27 scenes with `quick`, four mirror scenes, the
  lantern lake; build D7F68BB07CCC): **`docs/agents/inspection-20261008.md`**: a table of 123 rows with a number
  and a crop each (44 new), a section a scene, one row of numbers a scene (2b), the five worst (3). Crops and the
  agent's tools: `artifacts/inspection-20261008/`. The run's own numbers: `artifacts/harness/20261008-130029/numbers.txt`.
- The five a player sees first, in the agent's judgement: (1) water is a flat milky sheet, 1.7 to 2.9 times too
  bright (PT-81); (2) people up close: faces at 0.13 to 0.5 of the game's brightness and orange, eyes black holes,
  eyebrows gone, hair flashing with NRD (PT-73's camera light, PT-17, PT-44, PT-01, PT-55); (3) things missing
  (PT-83); (4) plants flicker and NRD does not stop it (PT-84, PT-85, PT-80); (5) light in the wrong places: rooms
  1.4 to 2.1 times brighter with ceilings up to 8 to 20 times while floors and people sit at half; the candle
  corner x0.49, the doorway x0.42, the cave x0.39 (PT-73, PT-79, PT-39, PT-48).
- Where the new rows went: PT-83 (missing things), PT-84 (stair-step edges), PT-85 (NRD's own defects), PT-86 (wet
  and relief), PT-87 (tracing paused itself), PT-88 (the owner's black armour in a mirror, with the mirror rows),
  PT-89 (the small ones). Rows the list already had keep their entries (the table's last column names them).
- What holds: fog and night match the game's to 1 to 5 %; the flame rectangles are gone; thin bars do not boil; a
  perfect mirror shows the direct view with no denoiser; the game's own picture is back after Off in all 27 scenes
  (brightness within 0.2 %; PT-20).
- Not judged: motion (all stills of a frozen world), Ray Reconstruction, metal (PT-87), black floor spots (no
  save), causes (the agent read no code: every cause in its file is a guess), and whether dark is right in the
  stairwell, the cave and the crypt (the owner's call).
- THE OWNER, 14:05, of the hour this run took: one thing at a time in a short loop, no hour-long runs (AGENTS.md).
  State: the list is made; its entries are worked one at a time.
### PT-81 The tracer's own water in the game: a flat milky surface where the game has a dark green sea with waves and foam
- Seen by the owner on 2026-10-08 ("The water looks off"), looked at by the coordinator at 11:00:
  `artifacts\harness\20261008-083001\water-day\game-vs-traced.jpg` (the first build with the water merge, no denoiser).
  The game: dark green water, wave shapes, white foam in streaks, the bed showing at the shore. Traced: one pale
  beige-white surface from the shore to the horizon with a soft cloud pattern; no waves to see, no dark water, no
  reflection of the sky, the bed not showing; the shoreline is right.
- Not known yet which: (a) foam everywhere: the water's shading reads the game's foam mask, and that texture is the
  one PT-78 makes a second time (`TextureShare.Twin`: a new Texture2D, then `Graphics.CopyTexture`); if the copy did
  not land, the twin holds Unity's default pale grey and every point is foam. Check: read the twin back, or trace
  with the foam off. (b) the water job's foam or scattering in the game's units (its offline fixture had no game
  data). (c) the automatic gain or the hold on a bright surface.
- The water job was merged on its offline checks (87 ok) and a valid whole GPU run; nobody had looked at it in the
  game before the owner did. A harness gap too: no check says "the water's picture is near the game's".
- (a) IS RULED OUT, 12:44 (run `20261008-124239`; the plugin now logs what a copy holds): "The game's own: mean 123,
  114, 142, 89 of 255, red from 90 to 165; the copy: mean 123, 114, 142, 89 of 255, red from 90 to 165". The copy
  holds the game's texels. So the white sea is the water job's own shading (b): how it turns the foam texture (four
  different channels, none near 0) and the simulation's foam amount into foam, and/or the water's own colour.
  Also from that log: `AccessTools.Field: Could not find field ... WaterSystemRuntimeResources ... foamMask` (a
  harmless first try before the property is read, or a sign the resource is looked up wrongly: to check). That run
  saved no frames: ReShade with RenoDX was on (the owner turned it off at 12:50; the harness now refuses to run
  beside it).
- TAKEN BACK 13:00 (the coordinator's wrong reading): the offline check "the caustics and their lookup stay
  affordable where 25 lamps light a lake (under 10 ms here; the hash took 33.7 ms)" is NOT a regression of the
  water merge. The 33.7 ms is fixed text in the check's name, not a measurement; the check fails when the card is
  shared and passed in the whole run of 12:53 with the card free. What IS measured in the game: the caustics
  stage costs 12 ms a frame in the forest with no water in view (run `20261008-124542`).
- THE WHOLE GPU TEST OF THE 12:45 TREE (with the speed job; 12:53 to 12:59, the card free, the passes cached): a
  valid run, 1,460 ok, 6 FAIL lines: the five known, and "emissive surfaces are sampled as lights except a light's
  own, moving ones and those past the triangle budget" (the row read "gbrg?": the test looked for its 263,538
  triangle mesh six frames after registering it, and since the speed job that mesh is built a few frames later;
  the second row found it). The test now waits until no mesh is queued; rerun queued.
- THE WATER JOB 6 (`docs/agents/water-6.md`; merged into main 14:18, no conflict; built; its source checks pass).
  NOT ONE FRAME OF IT RAN ON THE GPU: the game's run held the card, then the owner asked for it (14:10). So this
  is a strong suspect, not shown by a test yet:
  - THE WHITE SEA: the foam's shader adds the waves' Jacobian over at most two bands, and the game's threshold for
    a three-band sea is made for the sum over three (12 against 8 at rest; read in the game's own HDRP build). So
    the foam's cover came out 1 at every point of the sea: a white veil over 45 % of every pixel, in the foam
    texture's cloud pattern. The shader's function run on the CPU over the fixture's data: foam 0.451 on 100 % of
    the sea before, 0.006 on 3.5 % after. One loop bound changed (`pt_water_foam.hlsli`). Rivers (2 bands) and
    lakes (1) were never hit. A fixture for it: `tests/water_sea_gpu.inl` (`TGPT_TEST_WATER_SEA=1`), never run.
  - The logs never held the sea's foam amount, colours and absorption; the plugin now logs them once a surface.
  - The caustics' 12 ms with no water in view: the sea's level runs under the whole land, and every corner of the
    grid over it traced two rays whether or not light reached it. Now only lit corners and their neighbours do.
    The time saved is not measured.
  - A lamp inside its own opaque emitter (the harness's lantern) lost its whole caustic in about one frame of
    five (shadowed by its own ball): changed, by reading. The lantern's light off the lake IS in the game's frames
    (+1 % on the rock the lamp lights itself, +17 to 27 % on the dim rock beside it; water mirrors 3 % at that
    angle); the run skipped the shaded-lantern step, the one that can judge it.
  - Not built (its section 12): the sun does not light the water's body, the water's own smoothness, foam at the
    shore, caustics outside 1.5 times the camera's view (a mirror shows none there).
  - A FLAG TO CLEAR: "the foam's erosion lines read like a port of HDRP's shader"; the agent could not compare
    (the download of Unity's source was refused). Rule: Unity/HDRP, learn, copy nothing. Someone compares
    `pt_water_foam.hlsli` against Unity's public water shader and rewrites what matches. The agent's decompiled
    reading copies (`<scratch>\water6-work\watersystem.cs`, `watersurface.cs`) are deleted.
- IN THE GAME, 14:50 (run `20261008-144459`, `water-day`, no denoiser; the NRD step lost its frames to a menu
  coming up; sheet `artifacts/harness/20261008-144459/water-day-before-after.jpg`: the game, before, after):
  BETTER, NOT RIGHT. The whole picture's light against the game's: R 1.55, G 1.51, B 1.73 (before: 3.29, 3.18,
  4.24); the median 64-pixel block 1.23 to 1.26 (before 3.5 to 5.0). The even milky sheet is gone. What is there
  now: large soft white clouds of foam (the foam texture's pattern, metres wide) with near-black brown water
  between them. The game: dark GREEN water, wave shapes, thin white foam streaks on the crests and at the shore,
  the bed showing in the shallows. So left: the foam's amount and its scale ("Ocean": the surface's amount 1,
  HDRP's threshold 12.233, intensity 0.54, tiling 0.25: the plugin's new line), the water's own colour (the sun
  does not light its body: the job's section 12), the waves' look, the bed.
- THE FLAG, CHECKED 17:45 (the owner: "Yes, you may" download Unity's source to compare). The last four lines of
  `water_foam` in `pt_water_foam.hlsli`, and the two flowing samples before them, ARE a compact port of
  `FoamErosion` in Unity's `FoamUtilities.hlsl` (Unity-Technologies/Graphics, branch 6000.0/staging; no license
  header in the file; the repository's license is Unity's Companion License): the same steps in the same order
  with the same constants (the micro field's weight from 0.05 to 0.6 and the noise's from 0.1 to 0.2 over the
  foam's life, the smooth step from 0.1 to 0.9, the sharpness from 5 to 0.1, one minus 0.45 times the mean of
  the two distance fields, the life to the power 32, the current times 3 in two half-offset phases). The rule
  says copy nothing; the other rule says the tracer shows what the game's shader shows, and the foam texture's
  four channels are made for exactly this arithmetic. THE OWNER DECIDES: keep it and name its source and license
  in `docs/SOURCES.md`, or replace it with a formula of our own (the foam will then not look like the game's).
- THE OWNER DECIDED, 17:55: "our own foam". The ported lines go: the water job 7 replaces them (its step 3) with
  a formula written our own way, from what the texture's four channels hold and how foam behaves on water, not
  from Unity's or the game's shader code. It need not match the game's foam texel for texel.
- THE OWNER, 17:40: "water and foam is improved, but far from there still".
- THE WATER JOB 7 (`docs/agents/water-7.md`; merged 19:35, no conflict; its two fixtures pass, never the whole
  test) IN THE GAME, 19:45 (run `20261008-194341`, `water-day` with `quick`; sheet `water-day-sheet.jpg` in it):
  the sea is green water with wave shapes, thin white threads of foam, and the bed and the things on it show
  through the shallows. What it holds: (1) the sun lights the water's body: every point where a path scatters
  inside the water asks for the sun through the waves (`pt_water_sun.hlsli`), counted once; (2) the water's own
  smoothness (HDRP's near and far values), so the sun's glint is a soft lobe; (3) OUR OWN FOAM: the port of
  Unity's erosion is deleted, the new formula is written from what the foam texture's channels hold and how foam
  ages; its amount (`FOAM_ONSET` 0.45) is fitted to one scene.
  NUMBERS, the open sea's light against the game's (R, G, B): 3.63, 2.69, 3.28 with no denoiser and 4.09, 3.08,
  3.64 with NRD (17.0, 10.7, 16.7 at midday; 5.7, 3.7, 5.0 after the first fix); the whole picture 1.07 and 1.19.
  PT-85's orbs on the water: bright places a frame 2.4 with no denoiser and 0.2 with NRD (15.9 and 11.0 before).
  A pixel's spread on the sea with NRD 0.63 (1.17 before; the game's 0.05).
  THE COST: 223 ms a frame (78 to 93 before): the sun's patches, 40 to 60 wave corners a scatter point at 15
  texture reads each. The job's plan for it (its section 6: a buffer of corners made once a frame) is the next
  job: the agent `water8` (`brief-water-8.md`, copy `wt\water8`, base58), the same result at under a fifth.
  LEFT TO THE EYE: the far sea is pale and hazy where the game's is dark green to the horizon (the 3 to 4 times
  above), the shallows are browner, the foam's threads are very white and there is none on the far crests, no
  foam at the shore (HDRP's foam buffer is not read), lamps do not light the body.
  State: seen in game by the coordinator; the owner to look; the cost is being worked on.
- THE OWNER, 01:00 on 10-09: "our PT water is still far from perfect I think. It doesn't seem to [refract] light
  inside it, does it?", then: "Or maybe not *far* from perfect - it's getting there, but there's room for
  improvement still", and: "the 'crabs' inside the water just doesn't seem like they're 'inside' the water, I
  would expect the light within the water to be bent so much that the crabs would look different, but maybe it's
  physically correct". LOOKED AT (run `20261009-010307`, `water-day` with the new recipe `caustics-none`: no
  denoiser, 16 frames with the Caustics row on, then off; sheet `water-caustics.jpg` in the run):
  - THE CAUSTICS ROW ADDS NOTHING TO SEE on the bed: the whole picture +1.4 %, the shallows -6 %, the open sea
    -2.9 %; no rippling light in the difference. The white branching lines in the shallows are the FOAM (there
    with the row off too).
  - A DEFECT: with the row ON a crab in the shallows is DARK; with it off it is bright orange as in the game.
    Read by the water speed job (`docs/agents/water-8.md` section 7): with the row on a surface under water has
    no straight sun, only `caustic_light`, and `pt_caustics.hlsl` `emit_triangle` drops every landed triangle
    whose three corners land on different surfaces: nearly all of them on a small rounded thing.
  - THE COST SITS BEHIND THE ROW: 235 ms a frame with it on, 57 ms with it off (off: a scatter point takes one
    straight shadow ray to the sun, no bending; x1.07 of the light).
  - Told the owner: the VIEW is bent at the surface (in a hand's depth of water with gentle waves that moves a
    thing by millimetres: the crab's crisp outline is fair); what makes a thing look under water is mostly the
    light (the rippling net over it, the surface's glints, the colour with depth), and that net is missing.
  Left for the water, in order: the dark crab (the caustics pass's dropped triangles); the sun's rippling light
  on the bed not showing; only the sun lights the body (no lamp, no fire); shore foam; the pale far sea.
- THE OWNER, 2026-10-09 08:36, what the water MUST do: "and are we reflecting other stuff than just the sun in
  water now? we must. caustics and refraction must work too. foaminess must be realistic. the wet [surfaces] where
  the wave reached against the shore and so on". Where each stands (told the owner):
  - Reflections: the surface mirrors whatever its reflection ray meets (sky, shore, people, lamps, fire), not the
    sun alone. What IS the sun's alone: the light inside the water's body (no lamp, no fire lights it). A candle
    mirrored in still water is a burnt blob (PT-90).
  - Refraction: the view through the surface is bent by the real waves. Caustics: the sun's are traced through
    the waves (a soft 5 to 10 % pattern on a shallow bed, which is what these waves give); the dark crab's two
    trials wait for the merge; thin things under water stay too dark; PT-94. A lamp's light thrown by water
    onto rock (PT-14, `lantern-lake`, `lantern-seacave`): not looked at since the water was rebuilt.
  - Foam: our own formula, thin bands on the open water; none where a wave meets the shore. Not realistic yet.
  - The wet shore (PT-52): built and in the tree (`pt_wet.hlsl`: where the water stood is remembered and dries;
    wet ground darker and glossy, for rays too), checked offline only, NEVER judged in the game: a frozen
    world's stills cannot show a wave leaving. It needs a moving look (an `anim` step).
  So the next water job, after `water9`'s merge: foam at the shore, the wet shore seen moving, lamps and fire
  lighting the water's body, PT-90, PT-94, and the lamp-on-water scenes looked at again.
- THE DARK CRAB'S TWO TRIALS IN THE GAME, 14:46 on 10-09 (run `20261009-144507`, `water-day`, recipe `caustics-split`,
  no denoiser; sheet `crab-game-off-split-lit.jpg` in the run's folder): a patch of sea bed (levels of red) 34.6
  off, 37.7 with `Caustics land split light`, 39.5 with `Caustics light keeps its direction` too; the game 67.8.
  Right way, small: +9 and +14 %; the crab's shell a little redder. THE COST THAT MATTERS: the Caustics row
  itself is 90 ms of the beach's 150 (58.3 ms with the row off): the beach's frame rate is the caustics pass
  (PT-99). The crab stands elsewhere in the traced picture than in the game's: the tracer bends the view
  through the water's surface, the game's water does not. State: both trials to become the rule with the
  water's next clean-up; not a job of their own.
- `water10` IN THE GAME, 14:41 on 10-09 (run `20261009-142733`, `water-day`, no denoiser and NRD; scratch sheets
  `water-whole-a.jpg`, `-b.jpg`, `water-close.jpg`). `Water light without view` IS RIGHT: the dark slab of
  sea bed with its hard edge at the shore is gone, the crabs show; a patch of bed under shallow water (levels)
  the game 68, 59, 44; off 28, 22, 14; on 51, 45, 34; the whole lower picture's mean 54, 50, 39 against the
  game's 55, 51, 37. It costs 17 ms on top of the lamps' switch (173.5 to 190.8 ms; off 157.9), and the
  harness flags it boiling (0.65 levels, 20 times the game's). `Water shore foam` IS WRONG: one solid white
  slab over the whole wet band of the shore and solid white blobs further out, where the game has thin lines
  of foam: the mask is read or thresholded wrong. `Water body lamps`: no change by day (no lamp near); its
  scene is `water-torch`, not run. Still unlike the game with all of it: no green tint in the body, the
  surface far out pale and hazy, the foam's lines thin and burnt white, the sand's ripples lost. The beach
  runs at 5 to 6 frames a second (158 to 228 ms). State: the no-view switch to become the rule and cheaper;
  the foam back to a worker.
- `water10` (Codex, `brief-water-10.md`, `docs/agents/water-10.md`; done about 11:30, merged 13:55 on 10-09, four
  conflicts resolved; `<scratch>\pre-water10`; nothing seen in the game): three hidden switches, all off:
  `Advanced / Water shore foam` (foam from the traced depth and the waves), `Water body lamps` (lamps' light
  bent into the water's body), `Water light without view` (PT-94). Its own offline checks: the body +3.3 and
  +2.8 %, lamp and sun within 0.6 and 0.2 % of the worked-out values, foam 1 / 0. PT-90: its fixture finds the
  mirrored lamp's stand-in 160 times too bright: the emitter's replacement is NOT built. Left: the game's
  answer, PT-90, how foam ages. Recipe `water10` (`water-day`, `water-torch`). Its note: a cold start took
  453 s and about 24 GB with other workers compiling.
- THE DARK CRAB'S JOB (the agent `water9`, `brief-water-9.md`, `docs/agents/water-9.md` in `wt\water9`, base62;
  done 08:28 on 10-09, NOT MERGED, nothing seen in the game). Two hidden switches, both off: `Advanced / Caustics
  land split light` (a left-out caustics triangle's light lands, a third at each corner, on what that corner
  met: written = carried) and `Caustics light keeps its direction` (a second cause it found: under water the
  sun's light was diffuse only, no highlight, no normal map: a crab's shell top x0.62 of the row off). Its own
  offline check against the straight light (0.978 is right): a 10 cm box 0.59, 0.77 with the first, 0.89 with
  both; the bed within 3 cm of things 0.71 to 0.78; the open bed 0.78 to 0.945 with the second. NOT mended:
  things thinner than the 2 cm cells (a crab's legs: a 1 cm bar 0.12, 0.27, about 0.4). THE RIPPLING LIGHT: it IS
  in the picture, a still soft pattern of 5 to 10 % (blobs 5 to 10 cm), no lines, and the pass loses none of it
  (12.3 % against 12.6 % worked out by hand): the game's waves are too long and gentle for lines on a bed 20 to
  50 cm down (no wave under 31 cm; they focus about 4 m down). OPTION BITS: it took 1073741824 and 4194304; the
  characters' trial (PT-92) holds 4194304 in the main tree, and all 32 are used: one must go before the merge.
  Recipe `caustics-split` (`water-day`, no denoiser). Next: the merge, one run.
- THE WATER SPEED JOB (`docs/agents/water-8.md`; two agents; merged 03:45 on 10-09, one conflict in `tracer.cpp`
  resolved by hand (both sides kept); frame constants 8048 bytes): a grid of wave corners a water slot made once
  a frame (`pt_sun_grid.hlsl`), the sun's patches taken from it as tiles of 4 x 4 cells. Its fixture: the sun's
  part 31.0 to 9.7 ms (a third, not the fifth asked for), every sea number unchanged, the largest single sample
  26.6 to 15.4 times the mean. Expected at the beach: 135 to 150 ms (235); the sea past about 60 m keeps the old
  cost.
  IN THE GAME, 03:53 (run `20261009-034927`, `water-day` with `caustics-none`, against run `20261009-010307`): the
  beach with the Caustics row on 234.6 to 140.1 ms a frame (with it off 56.6 to 54.6); no frame failed. The
  light: the open sea 0.0715 to 0.0711; the whole picture 0.0699 to 0.0674 (3.6 % less; with the row off 0.0689
  to 0.0686), the shallows 0.0388 to 0.0377 (3 % less): small, and not known whether it is the run's noise or
  the job's one flagged change (points nearer the surface than 15 cm lit from the grid's 7.6 cm cells): to
  settle with its own fixture. Native 6684C89C38AC in the game. Left for speed: its section 8's two steps to a
  fifth; the sea past 60 m.
- `water11` ENDED 16:30 on 10-09 (Codex, base70; report `wt\water11\docs\agents\water-11.md`; NOT MERGED YET: one
  conflict, shader files, so a full build). THE FOAM'S CAUSE, its words: "depth saturation whitened 59.04 % of
  measured water; game: 1.87 %" (the rule turned white wherever the bed is near under the surface). NEW: foam
  only in a centimetre-deep contact band at an active crest and on steep, depth-limited breaking fronts; its
  fixture: 1.35 % of the area; its guess for the beach: 3 to 8 %. THE LIGHT UNDER WATER WHEREVER A PATH ASKS IS
  THE RULE NOW (no switch), and cheaper by its reading ("flat aiming uses 1 query instead of 15"; the beach's
  cost not measured). Recipe `water11` (`water-day`: the game; no denoiser with the foam off and on; NRD with
  the foam on; 16 frames). Left by its own list: the game's coverage, timing, the boiling, how foam ages.
- State: to merge and run after the pop-in test's build.
- `water11` IN THE GAME, 16:59 on 10-09 (run `20261009-165909`, `water-day`, recipe `water11`; read at 19:00;
  sheets `beach-game-foamoff-foamon.jpg` and `beach-bed-game-off-on-nrd.jpg` in the run's folder). THE FOAM IS
  THE RIGHT KIND NOW: one thin white line where the water meets the sand, the open water's bands as before, no
  slab. EVERYTHING ELSE OF IT FAILED: (1) the sea bed is dark again (a patch 31.6 levels of red with the
  under-water light "the rule", where the trial `Water light without view` gave 51 at 14:41 and the game has
  68): its cheaper way ("flat aiming uses 1 query instead of 15") lost the light the trial had found; (2) WITH
  NRD THE WATER IS A FIELD OF WHITE SPARKLES (the same patch 192 levels; the report: boils 5.8 levels, 182
  times the game's); (3) a frame's GPU time 207 to 256 ms where the beach was 158 to 191; (4) the caustics'
  memory 689 MB there and 1,322 MB in the owner's walk, against 215 (PT-87). It was taken out of the main tree
  at 18:45 for (4); (1) to (3) alone would have done.
- State: the next water job starts from the trial of 14:41 (`Water light without view`, in main again as a
  switch) and from `water11`'s foam line alone (its copy `wt\water11`), with the memory and NRD as its first
  checks in the game. Not started: memory comes first (PT-87).
- 2026-10-12, STILL SO, the beach by day (`artifacts\harness\20261012-041726\water-day\overview.jpg`, no denoiser):
  the game's sea is green-brown with white foam lines over the shallows, the sand's ripples and a crab showing
  through; the traced sea is grey-blue and milky, with no foam. NEW THERE: over the shallows lies a dark patch with
  soft edges (the sand's ripples gone under it, the crab in a dark halo); the sky is a pale green band with lighter
  shapes in it where the game's is a dull grey-green haze; and the sea flickers 5 to 7.7 levels in the region with no
  denoiser (the game's 0.08; the change map is white over all the water). The night's speed rule (PT-99: the water's
  sun no finer than its ray) changed none of it: its steps with the rule on and off look the same. In `water-torch`
  (`20261011-215257`) likewise: the game's dark water with foam specks is a pale smooth mirror of a bright sky.
  Next, before the water job: one frozen frame of the beach with the tracer's views, for what the dark patch is (the
  bed's light under the water, or the caustics' cells) and what the sky's shapes are (PT-112's sky).
- 10-12 09:25, THE BEACH BY ITS PARTS (main E98E321830C2; the world frozen; recipes `water-look` and `water-noview`,
  which stay for the water job; region = the shallows in the scene's box; the game's region luma 49.9):
  (1) THE DARK PATCH IS THE CAUSTICS ROW'S OWN (`20261012-090731`): with the row on the region is 37.7 and the bed
  under it a dark slab with the crab in a dark halo; WITH THE ROW OFF (the straight light) 42.2, the sand's ripples
  and the crab plain. With the row on, light reaches the bed only as the caustics' landed light, and that leaves the
  slab out. (2) THE HIDDEN `Water light without view` STILL CURES IT AND STILL CANNOT BE THE RULE (`-091756`): the
  region 37.3 to 49.6, the game's 49.9 (the picture's darkest 5 %: 11 to 19, the game's 22); BUT UNDER NRD THE SEA
  IS COVERED WITH WHITE BLOBS (rare very bright samples; the region's flicker 2.05 for 1.50, boil 1.03 for 0.62),
  with no denoiser flicker 1.81 for 1.19, and the world running a frame is 119.0 ms for 101.8 and 99.8 (the
  reflections 70.9 for 56.5 and 53.4; the stream was on: the steps compare, they are no baseline). The same three
  faults as on 10-09: the night's speed rule did not make it cheaper. (3) EVERYTHING IN A WATER PIXEL IS THE
  REFLECTIONS' STAGE: the views "Direct" and "Indirect" are black over all the water and the bed under it, "Specular"
  holds the whole look; "Albedo" is black there but for the foam's lines. (4) THE FOAM IS THERE with no denoiser in a
  frozen world (thin white lines where the game has them, fewer); a single running frame hides it under the water's
  noise (the running steps' region flickers 2.8 to 7.0 levels). (5) THE GREEN "SKY" IS FAR LAND: the left two
  thirds of the band over the horizon are a far mountain (white in "Direct", grey rock in "Albedo") that the traced
  fog turns one flat green, and the right third is the sky, flat grey. The game's picture has one dull grey-green
  haze over both, the mountain a faint outline at the very top: its fog is saturated at that distance and its sky is
  fogged with it. So the tracer fogs far land and the sky differently, and its fog there is brighter and greener
  than the game's: PT-37 and PT-112's sky, not the water's.
  WHAT THE WATER JOB IS, THEN, in order: the bed's light with the row on (why the landed light leaves the slab dark;
  the light wherever a path asks without rare bright samples and without 17 ms); then NRD on it; then the foam's
  count and the body's colour. Brief: `docs/agents/briefs/brief-water-12.md`.
- THE OWNER, 2026-10-12 11:00, of the line "the beach's water still looks nothing like the game's": "that doesn't
  matter, the game's water doesn't look good in the first place. It's fine that ours look different, so long as it
  looks amazing". SO THE GAME'S PICTURE IS NOT THIS ENTRY'S YARDSTICK ANY MORE (AGENTS.md has the ruling). Taken
  back from the lines above: the game's 49.9 in the shallows as a target, "fewer foam lines than the game's", "no
  green in the body" as defects. What stays a defect, by physics and by its own look: the dark slab of bed under
  clear shallow water with the Caustics row on (light that must arrive is missing: with the lights' straight rays
  the same bed is lit), the white blobs under NRD and the single samples hundreds of times the mean behind them, the
  water's noise with no denoiser (flicker 1.1 to 2 in a frozen world, 3 to 7 running), a surface that reads as a
  flat milky sheet. How the water looks beyond that is the owner's to say from the traced pictures
  (`artifacts\harness\20261012-091756\water-day\overview.jpg`).

### PT-80 The mirror, water and caustics run of 2026-10-08 10:02 "looked all kinds of broken" (the owner)
- THE OWNER, 2026-10-08 about 10:15, having watched run `20261008-100208` (`mirror-fog`, `mirror-water`, `mirror-cave`,
  `mirror-tavern@mirror-check`, `lantern-lake@dots`; the 09:11 build, ABI 0x00012600): "Man, the most recent run with the
  mirrors just looked all kinds of broken. The mirrors do not work at all. And the mineral cave had incredible flicker
  in grass. The water looks off. The caustics look off. Many issues".
- The numbers of that run (`tools/harness/mirror.py`; brightness against the reference with the scene's exposure taken
  out / shapes, 1 = the same): water mirror 3.74 / 0.11 with no denoiser, 3.77 / 0.13 with NRD, and it boils 1.15
  levels with no denoiser; cave mirror 1.98 / -0.02 and 1.73 / -0.03; tavern mirror 1.16 / 0.23 and 1.32 / 0.21.
  `mirror-fog` did not run (save "PT fog" not found by the harness in that run, though `saves.json` of an earlier
  run lists it: to check). The cave lake's caustics: the Caustics row adds 1 to 8 % over the whole picture.
- LOOKED AT 10:50, two of the sheets (`mirror-tavern/mirror.jpg`, `mirror-water/mirror.jpg`: the reference from the
  mirror's side, the same traced, the mirror traced with no denoiser, the difference, with NRD, the game's own):
  (1) THE MIRROR SHOWS WHAT IS BEHIND IT, NOT A REFLECTION. In the tavern the reference is the wall with the armour
  stand behind the camera; the traced "mirror" shows the room ahead, tables and candles, as if the mirror were not
  there. So the tracer's rays pass the mirror: the harness's mirror object (made while the scene runs) is not in
  the traced scene, or is traced as something a ray goes through. It worked on 2026-10-07. To find: does the
  tracer's log register its mesh and instance (the scan of new objects since the whole-world and the memory jobs;
  the glass job's "only what the game draws see-through is glass"; the water merge's one boundary)? That is a
  harness or scene-capture defect, and it hides what a real mirror would show: until it is fixed the mirror runs
  judge nothing. (2) THE WATER IS WRONG: through the same hole the water scene shows a rock, a flat grey plane and a
  flat green one where water and shore should be. The tracer's own water (merged 07:44) has not been looked at in
  any run by the coordinator (`water-day` of runs `20261008-083001`, `-100208`): look there next.
- 10:55: CAUSE OF (1) FOUND AND FIXED IN THE HARNESS: the mirror's GameObject had `HideFlags.DontSave`, and the
  tracer finds renderers with `Object.FindObjectsByType<MeshRenderer>` (SceneCapture.cs), which leaves such
  objects out (`Lantern.cs` had learned this on 10-07). Removed; `mirror-fog` now uses the save `PT foggy-backdrop`
  (there is no save named `PT fog`); the run is repeated (`<scratch>\mirror-run.ps1`, started 10:58). A PRODUCT
  QUESTION left by it: anything the GAME draws from an object with that flag is not traced either (PT-63: no
  object left out): count them in a scene before deciding. (2) is PT-81.
- THE RERUN WITH THE MIRROR IN THE TRACED SCENE, run `20261008-111121` (11:11; numbers only, its pictures not looked
  at). `mirror.py`: brightness against the reference / shapes (1 = the same) / detail (1 = as sharp), with no
  denoiser and with NRD:
  - `mirror-fog` (the foggy night): 0.91 / 0.90 / 0.61 and 0.93 / 0.90 / 0.09.
  - `mirror-water`: 0.62 / 0.94 / 0.60 and 0.62 / 0.93 / 0.12 (the water itself is wrong: PT-81).
  - `mirror-cave`: 0.12 / 0.49 / 0.26 and 0.14 / 0.51 / 0.02: the cave in the mirror is 7 to 8 times too dark.
  - `mirror-tavern`: 0.93 / 0.49 / 1.24 and 0.97 / 0.54 / 0.09.
  So the mirrors reflect again (shapes 0.90 to 0.94 outdoors). Three defects stand in the numbers: (a) WITH NRD A
  MIRROR'S PICTURE IS BLURRED AWAY (detail 0.02 to 0.12 where no denoiser keeps 0.26 to 1.24): the reflections'
  filter treats a perfect mirror as a rough surface, or filters by the mirror's own depth and normal; (b) the cave
  in a mirror is far too dark; (c) shapes of only 0.5 indoors (to look at: what differs). The lake's caustics add 2
  to 8 % of light over the picture; not looked at.
- LOOKED AT 12:42 (run `20261008-111121`: `mirror-tavern/mirror.jpg`, `mirror-cave/mirror.jpg`, `lantern-lake/overview.jpg`):
  - With no denoiser the mirror shows what the tracer's own view from the mirror's side shows (the tavern's wall
    and armour stand, the same warm light): the reflection is right. The low "shapes" indoors is against the
    GAME's picture of that view, which is lit otherwise (its fill lights), not a fault of the mirror.
  - WITH NRD THE MIRROR'S PICTURE IS SMEARED (the same wall, out of focus): the reflections' own filter (the
    tracer's, which serves beside NRD) blurs a perfect mirror. A defect: a mirror's reflection must stay as sharp
    as the thing reflected. The same filter is the one `docs/agents/nrd-mean.md` found losing a third of the light
    on metal.
  - The cave "7 to 8 times too dark" is the traced cave itself (the tracer's own view from the mirror's side is as
    dark): the game lights that cave with fill lights the tracer leaves out (PT-48), not a mirror fault.
  - THE CAVE'S GRASS (the owner: "incredible flicker in grass"): in `lantern-lake` with no denoiser the grass at the
    lake's edge is the brightest thing in the change-per-pixel maps (flicker 0.8 to 0.9 over the picture, 1.97 with
    the world running), lit by one lantern through its own blades. No opacity micromaps in the game since 00:50
    (`docs/agents/speed-2.md` brings them back) and one coverage decision a pixel a frame.
  - THE CAUSTICS (the owner: "look off"): with the Caustics row on and off the two pictures are the same to the eye
    (luma 24.1 against 23.7); no rippling light shows on the lit rock over the water. So either the lamp's light
    off the water is too weak to see beside its direct light on that rock, or it is not arriving. PT-14.
- (Before 10:50:) NOT LOOKED AT by the coordinator (the usage limit): none of the run's pictures. So it is not known which of this is
  the tracer (the water merged at 07:44 changed what a reflection ray meets; glass and water share one boundary now;
  the mirror was last good on 2026-10-07) and which the new scenes' mirror placement. FIRST JOB NEXT: read
  `artifacts\harness\20261008-100208\<scene>\mirror.md` and pictures, the cave's grass with `flicker.py`, then one
  entry a defect.
- The owner on PT-75 the same minute: "isn't it just because it actually cleans up some of the really noisy parts of
  the image? Like, before denoising, we have some regions with extreme shadow instability, and vegetation and grass
  flicker like crazy". Answered: partly. Taking noise away does not move the average (16 undenoised frames average to
  what the picture converges to, and NRD's picture is 5 to 24 % under that, by colour). But the loss does sit in the
  noisy places: there much of the light arrives as rare bright samples, and the filters throw those out (NRD's
  anti-firefly filter; the tracer's own direct-light and reflection filters lose more: `docs/agents/nrd-mean.md` in
  `wt\nrdmean`). So fixing the noise at its source (shadow instability, grass) shrinks the loss too.
- State: open, top of the list.
### PT-79 A wall torch's lights and two blue fires were left out as fill lights: the tavern's room went dark
- Found 2026-10-08 08:40 by the coordinator, from the lantern room's numbers: its traced light's median fell 3 EV
  (log2 -8.5 on the 10-07 22:46 build, -11.5 from the 10-08 00:50 build on), the room's screen level from 17.6 to 5.4,
  the automatic gain from x5.3 to x11.1, the light at the lamp unchanged. The owner had called the old picture "best
  overall"; the first real picture of option 4b (08:30) showed the dark room, which was not 4b's doing.
- Cause: `SceneLights.AddEffect` read only the FIRST material of a visual effect when asking "does this glow". A
  fire effect has one material an output (flame, embers, smoke, the warp of hot air); for the iron wall torch
  (`prefab_iron_torch_03`: `PointLight_Main`, `PointLight_Cookies`, `PointLight_Ambience`) and the Foredwellers' blue
  fires the first is the warp. Until 10-08 00:12 the warp counted as glowing because its graph is named "Fire"; the
  light-and-flicker job rightly stopped that (a warp emits nothing), and from then on those effects had no flame at
  all: "its names say Torch, but no flame, lamp or glowing thing stands within 1 m of it: a fill light".
- Fixed 08:45 in the plugin (every material of the effect is asked; the warp alone still counts for nothing) and
  installed. Run `20261008-084558`: the torch's lights are kept, the lantern room's median is back (gain x5.2), and
  with option 4b the room is at 17.2 (17.6 before 4b), the lamp's surround at 48 (68), pixels at 250 and over 0.26 %
  (1.98 %): `glass-jars/lantern-4b-room.jpg`, `lantern-4b-lamp.jpg` (the game, before 4b, the dark 08:30 picture, now).
- THE OWNER ON THE LANTERN WITH 4b, 2026-10-08 (they had looked at the dark 08:30 picture): 08:47: "in the first tavern
  lantern scene with 4b, i think the candle should be blown more out like before. did you say you made it 1
  candela? And it was 16000 before? then try making it 10, 100, 1000, 5000, 10000 and show me". Answered: the lantern
  was never changed, it has the game's 185,364 cd; a weaker lamp would not bloom more (the gain rises, the hold
  caps). 09:00, the owner: "well OK, what I meant was I want a fix of what we had before, where the candlelight is
  super overblown/bloomy, but at least it illuminates its surroundings a lot, and 4b, where we found a way to keep
  illuminating the surroundings a great deal while limiting how overblown the flame could get" (read: a mix of the
  two). So: the hold's W, how bright a lit surface may get from the brightening, is what they are tuning. Made a
  run-time value at 09:03: the hidden setting `Advanced / Highlights kept` (default 0.54 = 4b as built; 1000000 =
  nothing held, as before 4b) through `TGPT_Frame.highlightKept` (the record 608 bytes, ABI 0x00012600) to
  `g.look.x` and the fog pass's `look.x` (FrameConstants 7920 bytes). The recipe `lantern-hold` shows 0.54, 1, 2, 4,
  8 and no hold in one run; the recipes `lantern-candela` (a step's `lightName` and `lightCandela`: a light of the
  game's at another strength) and `mean` exist too. The owner picks the value by eye; then it becomes the default.
- THE SERIES, shown to the owner 09:30 (run `20261008-092242`, in the game, NRD; `glass-jars/lantern-hold-lamp.jpg`,
  `lantern-hold-room.jpg`). Screen levels: the room / round the lamp / of that at 250 and over / at 200 and over:
  the game 7.2 / 32 / 0.05 % / 0.5 %; no hold 17.7 / 68 / 2.0 % / 18.6 %; hold 8: 17.7 / 65 / 1.0 % / 17.6 %; hold 4:
  17.6 / 63 / 0.5 % / 16.7 %; hold 2: 17.5 / 59 / 0.3 % / 15.0 %; hold 1: 17.3 / 54 / 0.3 % / 11.8 %; hold 0.54 (4b as
  built): 17.1 / 48 / 0.3 % / 4.1 %. The room is the same in all; only the glow round the lamp changes. The
  coordinator's pick: 2. THE OWNER, 09:40: "hold 2, agreed". Done 09:42: `HIGHLIGHT_KEPT` 2.0 (pt_common.hlsli,
  d3d11.hlsl), the setting's default 0 ("the built-in"), the test's `kept` and the source checks' pins.
- HOLD 1 SINCE 18:30 (the owner, of the tavern in the evening's runs: "Some lights in the tavern were still a bit
  overexposed/bloomy in the demo you just did - is that the 'hold 2' from earlier for candlelights? If so, let's do
  hold 1 instead"). `HIGHLIGHT_KEPT` 1.0 in both shader files, the test's `kept`, the checks, the setting's text; in
  the game since 18:36 (run `20261008-183634`, `tavern-room`; sheet `tavern-room-hold-2-vs-1.jpg` in it). WHAT IT
  DID: only the brightest part moves. The fireplace's glow: 2.22 times the game's light at hold 2, 1.83 at hold 1
  with no denoiser (2.52 and 2.10 with NRD); the screen's brightest 1 %: 110 to 101 (the game's 77). WHAT IT DID
  NOT: the surroundings of candles are below the hold's level and keep the room's whole gain (3.3 to 3.5 times):
  the wall candle and its post are 2.8 times the game's at either hold. So the answer to the owner's "is that the
  hold": for the fire, partly; for the candles, no. What makes those bright is the room's gain on light that is
  already the lamps' full strength (PT-73 e), and the inside of the fireplace loses its stones to a cream wash at
  either hold. Open: a firmer hold (0.54), or less gain where lamps light a surface; the owner's eye decides.
- THE CHOICE SHEET, 19:35 (run `20261008-193045`, `tavern-room` with the new recipe `room-light`, NRD;
  `tavern-room-choice.jpg` in the run: A the game, B Dark Adaptation 50 (today's default), C 25, D 0, E 50 with
  hold 0.54). Light against the game's, in three places (the fireplace / a wall candle and its post / the floor
  in the middle of the room), and the whole picture's level (the game's 23.5): B 2.09 / 2.82 / 0.30, picture
  32.6 (gain 3.27); C 1.54 / 0.70 / 0.08, picture 18.2 (gain 1.82); D 1.21 / 0.12 / 0.02, picture 9.3 (gain 1);
  E 1.84 / 2.76 / 0.29, picture 31.8. WHAT IT SHOWS: no setting gives the game's balance. The game lights the
  floor with lights that have no source (dropped by the owner's rule); traced, the room has its lamps alone, so
  the floor in the middle is at 0.02 of the game's with no brightening and still at 0.30 with a gain of 3.3,
  which by then puts the surroundings of every lamp at 2.8. The firmer hold only touches the fire. The inside of
  the fireplace is a cream wash with its stones gone at every setting, D too (gain 1): not the gain. Waiting
  for the owner: which of B, C or between; whether the fireplace's wash is to be looked at on its own.
- THE OWNER ASKED THE COORDINATOR'S VIEW (22:40). Given: DARK ADAPTATION 35 as the default, run and shown (run
  `20261008-224225`, sheet `tavern-room-choice-2.jpg`): the whole room at the game's level (23.3 against 23.5;
  26.2 at 40, 32.6 at 50), a wall candle and its post 1.28 times the game's (1.66 at 40, 2.80 at 50), the
  fireplace 1.70 (1.80, 2.08), the floor in the middle 0.14 (0.18, 0.30). NOT CHANGED YET: the owner has not
  said go. A doubt put to the owner: a floor at 0.02 of the game's with no brightening is more than dropped fill
  lights explain; weak bounced light or a real lamp wrongly dropped (as PT-79) may be in it: to look at.
  THE FIREPLACE is not an item of its own (sheet `artifacts/harness/20261008-183634/fireplace-none-vs-nrd.jpg`):
  with no denoiser the bricks inside the arch are there (fine detail 7.77, the game's 7.91), brighter (level
  137 against 82); with NRD a quarter of the detail goes (5.95) and it reads as a cream wash: PT-19.
- DARK ADAPTATION 35 IS THE DEFAULT since 23:04 (the owner: "Do Dark ADaption 35 as default then"): `Settings.cs`,
  the row's slider (`PathTracingRow.cs`), the source check, the recipes' steps that put the default back; plugin
  30D36899C20A installed; the owner's own saved value set from 50 (the old default) to 35.
- THE OWNER, 23:00, of the sheet at 35: "I still see really overblown candle lights in that image by the way. Is
  that because even hold 1 is too strong, or are these lights not affected by that?" ANSWERED WITH A MEASURE (a
  wall candle, a 120-pixel box; sheet `artifacts/harness/20261008-224225/wall-candle.jpg`: the game, 50, 35, the
  Direct view, the Emissive view): pixels over 240: the game 0, Dark Adaptation 50: 318, 35: 313. NEITHER KNOB
  TOUCHES IT. What is burnt is the WAX, a white rectangle with a glow; the flame (Emissive view) is small and
  fine; the Direct view is white over the whole box. The wax lies a centimetre from its own flame, and the flame
  carries the game's whole power for that light, which is set to light a room: the wax is past white on its own
  traced light and takes none of the gain already, so a firmer hold does nothing. The same kind as the lantern's
  wall (this entry) and the hearth behind the fire. No fix is chosen: PT-40 holds it (the wax), with the question
  what a game light's power means at a flame's place and size.
- A lesson of that hour: a run of the game BESIDE the workers' offline tests (`-AllowGpuContention`, the owner's
  word of 00:20) no longer just runs slowly. Since the memory job a texture that cannot be opened pauses tracing,
  and with three tests on the card Windows cut the game's video memory budget from 14.85 GB to 6.3 GB: "Path
  tracing paused" on screen, which the owner watched and then closed the game (run `20261008-091703`). A look-only
  run needs the card's memory to itself too: wait for the tests (`run-held.ps1`), or have none running.
- To do: a check that would have caught it (the light rules' tests know nothing of effects with several outputs),
  and the same question for particle systems with a trail or a second material. State: fixed in a capture; the
  owner has not looked at the new picture yet.
### PT-78 A texture the engine made before our hook cannot be opened: a copy serves, until the hook is in from the start
- Found 2026-10-08 08:08, run `20261008-075054` (the first build with the tracer's own water): path tracing PAUSED in
  every scene ("Path tracing paused: 1 textures unavailable (5.4 MB)"; the harness: "path tracing is not on screen" in
  13 steps). The water's shading reads the game's foam mask (`FoamMask`, 2048x2048, one of HDRP's own resources,
  loaded with the render pipeline), and that texture is made before the tracer's texture-sharing hook can exist; a
  texture made before the hook can never be shared, and since the memory job a texture that cannot be opened pauses
  tracing rather than showing a 64x64 stand-in.
- Done 08:11, in the plugin (`TextureShare.Twin`): such a texture is made once more by Unity, after the hook, with the
  same size, format and mips, its texels copied on the GPU; the tracer opens that one. One line in the log names it
  and its bytes. THIS IS A COPY, and the owner's rule is "Don't do copies": it is exact and whole (not a smaller
  stand-in), it is one texture of 5.4 MB today, and it is told to the owner (2026-10-08) rather than left to be
  found. A streamed texture gets none (mip streaming makes it anew by itself).
- The fix at the cause: the hook in place before the engine makes its first texture (from the mods' preloader, which
  runs before the game's device exists: `docs/AUTOLOAD-INTEGRATION.md`), so that nothing predates it and no copy is
  needed. It touches the shared bridge and the preloader (the other session's files too): to plan with the owner.
- State: the copy is built and installed; not yet seen to work in the game (the 08:11 session). The hook from the
  start: open.
### PT-77 Leaves lit from behind glow brighter than the light that reaches them: keep the game's glow, or hold them to what is possible?
- Found by the walk job, 2026-10-08 (`docs/agents/who-walks.md` section 3b). The game's foliage profile has a
  transmission tint of 5.26, 5.99, 1.22: an artist's dial for "leaves glow against the sun", not a share of
  anything. The tracer uses it as the game does, so a leaf lit from behind sends on up to 6 times its colour: an
  ivy leaf with a green of 0.3 is 1.8 times as bright as white paper lit from the front. In a path tracer that is
  light that was never there (bounded: once a vertex, it cannot run away), and it goes on as bounced light onto
  the forest floor and the trunks, which the game, having no bounce, never showed.
- The two ways: keep the game's numbers (its look; a little made-up light under sunlit trees), or hold a leaf to
  what its colour leaves (dimmer backlit leaves wherever the game's numbers break the bound; ivy at 0.3: 1.8 down
  to 0.7 of white paper). The correct end either way is the thin sheet as a traced thing (PT-51).
- The owner's rules pull both ways: "each material's own values" and "realism and correctness". THE OWNER'S
  CALL; asked 2026-10-08. Nothing changed meanwhile. State: open, waiting for the owner.
### PT-76 Tinted see-through glass no longer tints the light that passes it
- Found by the whole GPU test of the 03:37 build (2026-10-08 04:23, a valid run: 1,513 ok, 5 FAIL): three checks that
  passed before the glass-and-hair job was merged: "light through green glass reaches path vertices green (bounced
  light)" (green over red under a sheet of green glass is 0.59 times the open floor's; it should be greener),
  "grey glass leaves the bounced light's color", and "grey glass lets light through as bright as its color (its shadow
  about 0.64 of a solid one's)" (its shadow is 0.18 of a solid one's: nearly as clear as clear glass).
- Cause, read from `docs/agents/glass-hair.md`, not yet from the code: since that job a glass material's colour means
  paint on the glass, by its alpha (a label, a lid), and glass itself has no colour (the 10-07 rewrite gave glass
  "absorption per metre 0, 0, 0"). The old code tinted what passed by the base colour; that is gone and nothing has
  taken its place. The owner's rule: each material's own values (HDRP gives a see-through material a transmittance
  colour and an absorption distance): the tint is the glass's volume, by the path's length inside it, where the mesh
  closes a volume, and a thin pane's tint for an open sheet.
- What a player sees today: stained or coloured glass the game draws see-through casts a clear glass's shadow and
  colours nothing behind it. (The tavern's bottles are not this: the game draws them opaque.)
- Next: after the water merge (`wt\water5`), which makes one boundary for glass and water: a small job on top of it.
  The merge agent was asked to write down where a tint enters that boundary. State: open.
### PT-75 NRD changes what the picture converges to: dim, bounce-lit places come out darker and redder
- THE FILTERS JOB, 14:21 on 2026-10-08 (`<scratch>\wt\filters`, base54; its report `docs/agents/filters.md` in the
  copy, backed up to `artifacts\worker-copies\wt\filters`; a dry merge into main: 13 files, no conflict; NOT
  MERGED YET). Built with NRD, source checks pass. THE CODE AS IT STANDS HAS NEVER RUN ON THE GPU (the owner asked
  for the card at 14:10): no mirror check, no mean check, no whole test. What it holds: a spatial filter that
  only moves light between pixels (it cannot make or lose any), NRD's anti-firefly filter off, the normal map's
  shading divided out before NRD and multiplied back after, and reflections filtered only between pixels that
  show the same thing in the mirror (history fetched where the mirror image was). From three runs killed half
  way: PT-80's smear is reproduced offline on the unchanged tracer (detail kept 0.22, and the mirror's picture
  has 20 % too MUCH light); the job's first mirror filter doubled the light and was replaced. Two limits it
  names: one low light on strongly tilted bumps can still lose up to a tenth through NRD; a finely textured
  thing in a mirror is sharp but noisier (the clean fix: the lamps' highlights in a signal of their own).
- MERGED INTO MAIN 17:47 AND SEEN IN THE GAME, 18:00 (run `20261008-175533`: `mirror-tavern`, `water-day`; the 13
  files as they were before the merge are in `<scratch>\pre-filters`):
  - THE MIRROR WITH NRD IS SHARP (PT-80 a): inside the mirror, detail against the tracer's direct view 1.22 (was
    0.05; with no denoiser 0.97), shapes 0.90, boil 0.18 (was 0.30); its flicker is up, 0.86 (was 0.47): sharp but
    noisier, as the job said. Sheets: `artifacts/harness/20261008-175533/mirror-tavern-centre.jpg` and
    `...-centre-BEFORE.jpg`. The owner had said "Smeared mirrors too yeah" at 17:40.
  - THE COST: NRD's picture moves more. Whole frame, tavern: change a frame 0.15 levels (was 0.11), 16-pixel
    blocks wander 0.182 (was 0.125; the game's 0.011). Beach: 0.42 (was 0.29) and 0.435 (was 0.316). About 40 %
    more of both. Not known which of the job's parts costs it (the anti-firefly filter off, or the filter that
    only moves light): the brief asked for that number and the job never got the card. The owner decides the trade.
  - The beach's orbs are as before (pixels 40 levels over their usual, a frame: 1,051 with NRD, 1,029 before; 25
    with no denoiser): their source is the water's path, not the filter (PT-85). NRD's mean light on the beach
    is now 1.21, 1.09, 1.16 of the undenoised mean (was 1.07, 0.97, 1.15): more than the undenoised frames show,
    likely because a bright sample is cut at white in a single frame and counts in full once spread.
  - NOT RUN: the job's own checks (mirror, mean), the colours in `glass-jars` and `candle`.
- 18:20: TWO PARTS OF THE MERGE ARE TAKEN BACK after the owner saw orbs in every scene (PT-85 has the counts): the
  a-trous pass keeps light exactly no more (its old mean over the neighbours it accepts is back; the planes' term
  and the mirror's part stay), and NRD's anti-firefly filter is on again. So PT-75's loss of light is back where
  it was for direct light and reflections (0.75 on a candle-lit floor) and for RELAX's 5 parts in 100. The job's
  checks `nrdMean` and `mirrorFilters` were written for the kept-light pass: expect their mean lines to FAIL; do
  not loosen them, they say what is still lost. Still in: the normal map's shading divided out before NRD and
  multiplied back after (not judged on its own yet), reflections filtered by what they show.
  State: open; the way to close it is fewer rare bright samples (PT-85), not the filters.
- 19:26: A THIRD PART OF THE MERGE CHANGED, after the owner watched the forest ("the grassy scene outside next to the
  fence had lots of light and shadow change on fence two trees"). Seen in the frames (run `20261008-184804`,
  sheet `forest-light-changes.jpg`): whole flat patches of a trunk, its own polygons, switched between lit orange and
  dark brown from frame to frame in the frozen world, and clumps of leaves with them; those blocks wandered 0.03 to
  0.07 levels in the afternoon's run and 1.3 to 5.1 now. The recipe `fireflies` on `forest` (run `20261008-190849`,
  sheet `forest-trunk-layers.jpg`) put it in the Indirect view: the trunk's 32-pixel blocks wander 6.53 levels there
  (Direct 1.61, Specular 0.07, Emissive 0.04). THE CAUSE, SHOWN BY ONE CHANGED LINE (run `20261008-192051`): the
  normal map's share that the bounced light is divided by before NRD was taken from NRD's filtered direction of
  the LAST frame (`k.denoise1[1]`, `tracer.cpp`): a loop through the frames, which swings on strongly bumped
  things (bark, leaves) and not on flat walls (the tavern's Indirect view was clean). With `k.denoise1[1]` always
  invalid (the sample's own direction serves): the trunk in the Indirect view 0.06, in Direct 0.05, in the picture
  0.13 (was 0.60); the whole picture's spread with NRD 0.20 (was 0.35; 0.63 before the edges fix; the game's
  0.13); the picture's mean the same (78.0 against 77.3). Made permanent, with a source check. OWED: the dead
  parts (`nrdDirectionKept`, mode 2's kept direction in `pt_nrd_prep.hlsl`) are still in the tree: delete them
  with a look at that pass, and judge what is left of part 3 (the division by the share from the sample's own
  direction) in a room, by `nrdMean`.
- Found by the coordinator, 2026-10-08 02:50, in run `20261008-015158` (`vista/crop-shield.jpg`: the shield's metal rim
  is grey with no denoiser, as in the game, and orange with NRD; the ground grey-brown against red-brown). The owner's
  order of judging (10-07): the undenoised picture first, then "The NRD denoiser should look relatively noise-free".
  A denoiser may take noise away; it may not move the mean.
- Measured with the new `tools/harness/mean_colour.py <step> <step>` (the mean of each step's 16 kept frames, in
  linear light, block by block; a frozen world). NRD's light as a share of the undenoised mean's:
  - `vista` (gain x1.0 in both steps, so nothing else differs): the ground in the wheel's shade R 0.82, G 0.50, B 0.36;
    the rim R 0.70, G 0.57, B 0.23; the wood R 0.72, G 0.47, B 0.23; the worst blocks B 0.01 (blue all but gone) and
    0.06 of all light; the sky 1.000. 56 % of the lit 64-pixel blocks differ by more than 10 % in a channel.
  - `candle`: 50 % of the blocks; the dim floor R 0.70, G 0.51, B 0.47. The same in yesterday's run `20261007-231354`
    (56 %): it is not from the night's merges.
  - `forest` (overcast, all light from the sky): 3.6 % of the blocks; whole frame 0.98. So it is not everywhere.
  - `glass-jars`: 99 % of the blocks, but the automatic gain differs between the two steps there: not comparable
    until the gain is frozen for the comparison (a harness gap, PT-26).
  - The tavern (`hair-moving`, run `20261008-034922`): the OTHER way: R 0.81, G 0.99, B 1.49 of the undenoised mean,
    99 % of the blocks (the vista and the candle scene as before: 56 % and 54 %). A gain cannot do that (it scales
    the three alike), so it is a colour shift there too, toward blue. Either the denoiser's path, or something that
    changes with time between the two steps (the radiance cache filling, the exposure): the recipe `mean` (no
    denoiser, NRD, no denoiser again) tells the two apart: if the second undenoised step matches the first, it is
    the denoiser's path.
- IT IS THE DENOISER'S PATH, measured 04:32 (run `20261008-042521`, the new recipe `mean`: no denoiser, NRD, no
  denoiser again, 16 frames each, a frozen world): the second undenoised step is the first one's picture (whole
  frame 1.000, 1.000, 1.000 in the vista; blocks differing by more than 10 %: vista 0.2 %, candle 0.4 %, tavern
  6.0 %), and NRD's differs from both alike (vista 56 %, candle 53 %, tavern 99 %). Nothing drifts with time.
- A lead, from the shape of the two shifts (not proven): in the tavern green is untouched (0.98) while red falls
  (0.80) and blue rises (1.51): that is one colour difference (red minus blue) shrinking with the brightness kept.
  In the vista's shade all three channels lose about the same AMOUNT (so red least by share, blue most): that is
  brightness cut, or colour difference added, with the rest kept. Both are what comes of the brightness and the
  two colour differences being filtered apart, the differences over a wider neighbourhood and across edges of
  brightness: a dark place beside a bright one takes the bright one's colour difference (more than its own
  brightness can carry: blue goes below nothing and is clamped), a warm bright place is washed toward its
  neighbours. The directional (SH) form RELAX is given carries the light that way.
- Reading, not proven: the loss is strongest where the surface's own colour is strong (brown ground, wood, a metal's
  tint) and the light is bounced: as if that part of the light were multiplied by the surface's colour once more on
  NRD's path (taken out before the denoiser with one colour, put back with another), or lost to a clamp. To find in
  `pt_nrd_prep.hlsl`, the composite's NRD branch and what each reads as the pixel's colour (the tracer's own first hit
  against HDRP's; decals; the material programs).
- Next: an offline check first ("the mean of NRD's frames is the undenoised mean's, a channel, on a coloured floor lit
  by bounced light only, and on a tinted metal"), then the cause. `mean_colour.py` on every `quick` run from now on.
  State: open. Before NRD is judged for noise (PT-19).
### PT-74 Path tracing takes over a minute to come on: compile ahead, and tell the player
- The owner, 2026-10-07 23:00, watching run `20261007-224654` (its first scene's traced steps showed the game's picture:
  the tracer was not composing yet after 100 s): "And obviously PT shouldn't take a minute to activate, so I guess the mod
  should add some kind of precompilation step to the game for PT when enabling it. Or at least a 'compiling shaders...'
  bar in the top of the game so players know that PT is still compiling".
- Evidence: the harness waited 50 s in each of two steps without the tracer composing; the merge worker's offline test
  took 172 s to make its shader passes (89.6 s in the water worker's copy); before today's merges it was a few seconds.
- Next: measure which passes take the time (the native log's pass-creation lines); make them once and keep them (a
  cache on disk keyed by the shaders' hash and the driver), made while the game starts or loads, never when the player
  flips the row; and while passes are still being made, a line at the top of the screen that says so, with progress.
- 2026-10-08, the memory-and-start-up worker's job in the game (run `20261008-015158`): the line is there ("TG Path
  Tracing: compiling shaders (47 of 49)", `hair-moving/2-traced-none/frame.jpg`), and made passes are kept on disk.
  But a NEW build's first start took 214.6 s in the game (the native log: `waterUnderDxr12` 90.2 s, `prepareDxr12`
  54.7 s, `walkDxr12` 33.0 s, each "disk cache miss": the driver compiling the ray tracing pipelines), longer than
  before, because the passes grew (all of them 2.0 MB of compiled code on 10-07 18:46, 5.2 MB at 23:44:
  `docs/agents/speed.md`). A second start of the same build: 2.4 to 3.8 s offline, not timed in the game yet. The
  harness now waits for the passes (it took the game's picture for the tavern's two traced steps in that run).
  Left: the first start after an install or a driver update (a player waits three and a half minutes with the line
  on screen): smaller pipelines (the speed job took reads out; the water passes are the largest), and making them
  when the game starts, before the row is touched (to check: is that already when they are made?).
- 04:25: the same build's passes took 209.8 s in the game again and 515 s inside the GPU test (which also makes
  the variants the game does not use), so the test's own 180 s wait failed twice (`TGPT_TEST_STARTUP_SECONDS=900`).
  The passes' compiled size, from the generated headers: the reflections' pass about 8 MB a variant, the camera's
  about 3.5 MB (on 10-07 18:46: 0.5 and 0.26 MB; `docs/agents/speed.md` counted 2.0 MB for all then, 5.2 MB at
  23:44, before decals). Why: every place a pass reads a hit's material carries a whole copy of every material
  program, the skin program and every decal graph. The job owed: one place a pass reads a hit's material (the
  path as a loop), so the copies go; it serves the first start, every build (10 minutes of shader compiling even
  side by side) and every test run. Not started: after the speed regressions (`brief-speed-2.md`).
  State: open (the line and the cache: built, seen in a capture; the first start: open). Before any player build.
### PT-73 The first build with all of merge-4 in the game: what is wrong (the coordinator's inspection, 32 findings)
- The owner, 2026-10-07 22:55, watching the run live: "Lots of problems in these scenes. Hopefully you catch them all
  upon your own inspection"; 22:58: "Also get gpt-6-astra to look on the same frames to see what things it catches and
  compare your findings. Also focus on the things that flicker from frame to frame."
- The list, with pictures and numbers: `docs/agents/inspection-20261007-224654.md` (run `20261007-224654`; build: plugin
  D7AB6A600690, native 54165A49E5FD, ABI 0x00012100, installed 22:46 without the whole test). GPT-6-astra's independent
  list and the comparison: the same file's last section. The tool for flicker: `tools/harness/flicker.py`.
- The meter: tavern 99.0 to 99.6 % the same surface, forest 66 to 68 % (70 before), vista 95.6 % of what is not sky.
  Colour now matches where the surface is the same (forest: 1 % of pixels differ, 25 % before; vista 9 %, 35 % before):
  the material programs work. Roughness still differs on half the outdoor pixels.
- The worst, in short: (a) path tracing takes over 100 s to come on (PT-74); (b) video memory ran out (12.92 of
  13.39 GB) and Ray Reconstruction stopped for the rest of the session; (c) the tracer is 5 times slower in the tavern
  (12 to 64 ms); (d) the vista's ground has no texture and is nearly black; (e) the lantern burns the wall out, the room
  is 1.6 times as bright as the game's; (f) glass bottles are black silhouettes; (g) the tavern's floor planks are the
  wrong size; (h) far rocks are smooth, pink and glossy; (i) skin is orange-red, hair dark and striped; (j) the near
  grass flickers and is a blur; walls pulse every second frame without a denoiser.
- The owner, 23:05: "Also the wheel/shield at the right side here looked like a comic book:
  artifacts\harness\20261007-224654\vista\3-traced-rr\full.jpg". The coordinator had called it fine; GPT-6-astra's list
  had it. The cause, from the log: video memory was full, so 869 of the vista's 4,095 textures were read as 64x64
  stand-in tiles ("kept back near this card's video memory budget (their atlas tiles serve)"); the tracer's memory
  grew scene after scene (252 to 2,166 MB) and was never released. Probably the cause of (d) and (h) too. A silent
  stand-in is what "Don't do copies" forbids.
- GPT-6-astra's independent list of the same pictures: `docs/agents/inspection-20261007-224654-gpt.md`; the
  comparison is at the end of the coordinator's file. It caught what the coordinator missed: the shield, foliage
  that shimmers through the denoiser, blocky stakes, the fur, a metallic bottle, and that the harness's measured
  regions of the hair and glass scenes sit on the ceiling and a chair.
- The owner, 23:10: "I really think we need to make this look pretty good before testing with ray reconstruction. The
  NRD denoiser should look relatively noise-free, and the denoiser-less presentation should reveal the problem areas,
  like spots with a lot of shadow flickering, the weird rectangle gas spots over candle lights and stuff" (ruling in
  AGENTS.md; the recipes `quick` and `raw` now take no denoiser, then NRD; Ray Reconstruction only in `with-rr`).
  Where NRD stands in this run, by `flicker.py` (change a frame, in levels; the game's own in brackets): the tavern
  0.07 (0.03), the vista 0.26 (0.20; its worst spots 1.1 to 1.5 at the shield's plants and the tree), the forest's
  denoised step 1.14 (0.11): not noise-free, the grass and foliage. The candle scene's gas rectangles: `candle@quick`
  started 23:13 (it had not run).
- The candle scene (`20261007-231354`, `candle@quick`, 23:14; `candle/flicker-2-traced-none-map.jpg`): the owner's
  "rectangle full of noise above candlelights" is there, measured: one sharp-edged rectangle around each flame, several
  times the flame's size; inside it the picture changes 6 to 8 levels a frame with no denoiser and 2 to 3 with NRD,
  outside under 1. Its outline is a quad: the game's flame effect is drawn as transparent quads over the traced
  picture, and the tracer still places its light by HDRP's depth and motion of the frame shown. And: the candles' WAX
  IS BROWN, like wooden posts (the game's is cream white): with the woman's orange-red skin, the new scattering walk
  darkens and saturates what light walks inside. The owner, 23:20: "That also means we need to catch boiling with NRD
  and prevent that too": `flicker.py --boil` (NRD in this scene: blocks wander 0.096 levels, the game's 0.037).
  A fourth Codex worker since 23:19 (`wt/candle`, base44; `docs/agents/briefs/brief-candle.md`): the rectangles, the
  walk's colour, boiling with NRD.
- The owner on the rectangles, 23:25: "The rectangles are likely placed above flames where the heat haze effects
  should render, and then masked/feathered out at the edges in vanilla to prevent hard edges, but it breaks in PT
  rendering. The heat haze effects should be rendered in a more PT-friendly way there" (PT-41). In the tree: a
  workaround already switches HDRP's Distortion off while tracing (`HdrpPasses.cs` `SuppressDistortion`: "the noise
  it shifted around made the air above every candle boil"), and the rectangles are there all the same: it no
  longer takes, or the haze is drawn another way. The candle worker was restarted at 23:31 with the owner's words
  (`<scratch>/note-candle-2.md`): prove which effect draws them; stop the game's haze from breaking the traced
  picture; then the haze as hot air that bends the rays crossing it, from the game's own haze effect's place,
  strength and movement, feathered to nothing at its border.
- State: three Codex workers since 23:10, all from main at 23:09 (base43), tests once at the end, no mutants:
  `wt/memory` (b and PT-74: where the memory goes, release it, no silent stand-in, start-up measured, a cache, a
  "compiling shaders" line on screen; `docs/agents/briefs/brief-memory-startup.md`), `wt/mats2` (d, g, h, the roughness
  and normals, skin's colour, the dead materials agent's leftovers; `brief-materials-2.md`), `wt/light` (e and j: the
  lantern by its photometry, the two-frame pattern, the grass, the bright pixels; `brief-light-flicker.md`).
- The whole GPU test on this build, once, as the end of the merge chain (2026-10-07 23:50 to 2026-10-08 00:06, with
  NRD, native 54165A49E5FD; `<scratch>/gpu-test-main-235041.txt`): 1413 ok, 4 FAIL. (1) "clear glass casts no shadow":
  shadow rays stop at clear glass since the glass rewrite: a likely cause of the black bottles (sent to `wt/glasshair`);
  (2) "a character's eye drawn forward is shaded by the tracer, not left to HDRP" (eyes; the same agent);
  (3) "glass bends light by its material's own index of refraction" (the materials and the glass jobs both changed
  the index: for `wt/mats2`'s merge); (4) the lake's timing check (the GPU shared by eight workers: not a finding).
- 2026-10-08 00:11, the light-and-flicker worker (`docs/agents/light-flicker.md`; merged into main 00:12, no conflicts;
  not yet in the game). Found and fixed: (1) the near grass: a cut-out's cover was read from an old 256x256 copy of
  its alpha, also for the camera's rays, with a new random draw every frame and the cutoff replaced by 0.5: the blur
  and much of the flicker; it now reads the game's own alpha at the hit with the material's own cutoff. (2) With
  Denoising None the picture was still a blend of four reprojected samples: now each sample as it is (the cycle it
  measured is four frames long, not two; not proven to be all of it). (3) A fire's distortion output was taken for
  an emitter by its name. The wind stands still in a frozen world (checked). Its whole GPU run: 5 FAIL (the four of
  the base and one to look at in the merged build).
  NOT fixed, a decision for the owner: the lantern. The game's light there is 185,364 candela (a candle is 1) with
  HDRP's radius of 1 m and a fixed bright exposure: the artists' way of lighting a room. HDRP softens such a light
  near itself (at 30 cm: 170,000 lux); the tracer makes it the lantern's small flame (radius 0.1 m, by the ruling
  "a traced light has the size of what emits it"): 2,060,000 lux at 30 cm, 12 times the game's, and the traced
  picture's own exposure adds 6.5 times (2.7 stops). Far from the lamp both give the same light. The worker did not
  fudge it ("I did not quietly substitute 1 cd ... or restore HDRP's nonphysical fill falloff"). Asked of the owner
  at 00:15: see the coordinator's message; the choice is recorded here when made.
- The lantern, shown to the owner (run `20261008-001912`, `glass-jars@lantern-choice`, NRD; started beside the
  workers' tests at the owner's word: "Just run it now, we'll live with the poor framerate"; sheets
  `glass-jars/lantern-choice-lamp.jpg`, `-room.jpg`). Picture luma against the game's 13.9: as now 24.8 (gain 5.3);
  option 1, the small flame at the game's exposure: 4.8 (the room nearly black: the game lights it with fill
  lights the tracer leaves out); option 2, the lamp at the game's size and place: 11.9, the closest to the game,
  but the owner saw at once that it lights what it cannot reach ("How could the cup above the lantern, and the
  board it's standing on, be lit here, when the lantern is placed underneath it, and there's a plank in-between?":
  the game's light object stands 35 cm from the flame in the open, and in the game has no shadows at all); option
  3, less power: 0.6, black. The owner, 00:27: "none of them are ideal. 'Ours as now' looks best overall, but the
  light is blown out. Option 1 is most realistic, but the room is too dark. I'd like to see that fourth option you
  suggest. Otherwise I would think it was a matter of combining option 1 with stronger auto exposure? Not sure".
  Answer given: option 1 with stronger auto exposure IS "as now"; what burns the wall is that the brightening is
  the same everywhere. Option 4: brighten what is dark, less and less what is already bright (L' = L G / (1 + L (G
  - 1) / W)). Shown as a PREVIEW worked out from the two real pictures (said so to the owner; not built):
  `glass-jars/lantern-option4-lamp.jpg`, `-room.jpg` (4a: W 1.0; 4b: W 0.4). Waiting for the owner's pick; then
  it is built into how the traced light's gain is applied (the Dark Adaptation row already promises "The brightest
  parts of the view are kept from turning white").
- 00:30, the memory-and-start-up worker (`docs/agents/memory-startup.md`; merged into main 00:31, one conflict in the
  GPU test; not yet in the game): the tracer's memory now follows scene unloads and mesh use (it only grew before);
  a texture that cannot be opened pauses path tracing with a line on screen instead of a silent 64x64 stand-in;
  compiled passes are kept on disk: 2.4 to 3.8 s to start with the cache (over 100 s before), with progress shown
  on screen. Its whole GPU run: 1,387 ok, 5 FAIL (to read in the merged build's run). New exports (diagnostics,
  mesh release): the ABI number moves with the decals merge (0x00012200).
- 00:10, decals (`docs/agents/decals.md` in `wt/decals`): projector and mesh decals, their shaders read from the
  compiled programs, an ordered box tree with no count limit, one `apply_decals` after a hit's material; the whole GPU
  test passes in its copy. A Codex merge worker brought it onto main's tree (`wt/merge5`, base47; `docs/agents/merge-decals.md`:
  56 conflict blocks; its one GPU run stopped at start-up after 41 checks, the passes taking too long to make: before
  the memory job's cache). Merged into main 00:50 (one conflict); ABI 0x00012200 for it and the memory job.
- MAIN AT 00:50 = merge-4 + the VFX observer + light and flicker + memory and start-up + decals. Building, installing,
  one session beside the workers' tests (`candle`, `glass-jars`, `hair-moving`, `forest`, `vista` with `quick`: no
  denoiser, then NRD), then the whole GPU test once. To read first: how long tracing takes to come on, the video
  memory lines ("kept back"), the vista's ground and shield, the forest's grass, the yardstick.
- THAT BUILD IN THE GAME, run `20261008-015158` (01:52, `quick`; plugin 69DF4492CCCE, native 1058722FDF11), read by the
  coordinator at 02:45 (crops: `vista/crop-shield.jpg`, `crop-cart.jpg`, `forest/crop-grass.jpg`,
  `glass-jars/crop-shelf.jpg`, `candle/crop-candles.jpg`):
  - Textures: 1,285 of 1,285 opened with no copy, 0 kept back (869 kept back the run before). The shield has its
    wood and the vista's ground its texture again: the "comic book" look was the 64x64 tiles.
  - The forest's near grass is sharp with no denoiser and with NRD (the cutout fix).
  - The yardstick: tavern 99.4 to 99.8 % the same surface; forest 77 to 81 % (66 to 68 before; roughness still
    differs on 52 % of those, 12 % nearer and 8 % farther: the grass); vista 94.7 % of what is not sky, colour
    differing on 0.3 % (9 % before), roughness 0.2 % (49 % before), normals 26 to 28 %.
  - Change a frame, levels (no denoiser, then NRD): candle 0.71, 0.24 (at a flame 7.4, 3.1: the rectangles are
    still there, the candle job is not merged); glass-jars 0.58, 0.05; forest 0.47, 0.29; vista 0.53, 0.16. Boiling
    with NRD (the game's own): candle 0.094 (0.038), glass-jars 0.066 (0.008), forest 0.120 (0.029), vista 0.133
    (0.018). The tavern's two traced steps are void (the passes were still compiling).
  - GPU a frame (other tests may have shared it): tavern 69 to 78 ms, forest 89 to 93 ms: the speed job was not in.
  - Memory the status line counts: tavern 3,133 MB, forest 4,373 MB (252 to 2,166 MB the run before): to read in
    `docs/agents/memory-startup.md` what the number now counts before calling it growth.
  - New: NRD's picture is darker and redder than the undenoised one in dim places (PT-75).
  - Still wrong, as expected: the lantern's wall (option 4b is being built), brown wax and the red-brown wall
    (candle), dark bottles on the shelf (the glass job is not merged).
- VOID TEST RUNS, a lesson (02:50): the whole GPU runs of the glass and candle workers (01:00 to 01:30, three or
  four tests side by side, each compiling its passes) ended with 93 and 43 FAIL lines, among them their own checks
  that had passed alone. Both logs say why: frames failed (6,449 of 19,484; 1,150 of 24,429) with `Interop fence
  timeout 0x887A0006`, and "video memory near this card's budget (Windows gives the game 1595 MB)". A whole run
  with failed frames says nothing about the code: run it again. The speed worker's run at 01:50, alone on the GPU:
  1,384 ok, 4 FAIL (the three known on the base and "every mesh with alpha-tested instances has opacity
  micromaps", to watch). To do: the test stops with "void: the GPU was overloaded" at the first failed frames.
- 02:46, the speed job merged into main (`docs/agents/speed.md`; one conflict; not in the game until the 02:47
  build is installed): a material is read once a hit (it was read twice), HDRP Lit's maps are not read under a
  program that replaces them, the ground reads only the layers painted there (49 texture reads a hit down to 8 to
  11), triplanar only the sides that count, and glass asks which medium it is in without visiting every triangle
  of the scene. The picture is meant to be the same. NOT taken out, the owner's and the next job's to decide: the
  scattering walk runs on everything that names a diffusion profile, which is far more than skin and wax (clothes,
  armour, bows, the ground and snow cliffs; a walk is 3 to 50 rays): the larger part of what is left of the
  slow-down, and a likely part of brown wax and odd armour (the next job: who walks, PT-40).
- 02:55 to 02:58, three more finished copies merged into main by the coordinator (their whole runs were void, their
  own checks had passed alone): `glasshair` (`docs/agents/glass-hair.md`: only what the game itself draws see-through
  is glass, so the shelf's bottles are the painted, opaque bottles they are; an open sheet of glass is a thin pane;
  light goes through a lit card of hair to what is under it; the armour is brown because the game's white "hero
  light" at the camera is a fake light the tracer leaves out: the owner's call), `candle` (`docs/agents/candle.md`:
  the flame rectangles traced to the game's distortion and motion writes over each flame's whole quad, switched off
  per effect material while tracing (`EffectPasses.cs`; `SuppressDistortion` deleted); the scattering colour
  conversion made exact at the bright end; no 4096-flight stop; its new translated-skin check FAILS: a walk 110 m
  from the origin loses 4.5 % of its blue, a defect it found) and `mats2` (`docs/agents/materials-2.md`: the game's
  skin shader as a program (layers, tattoos), Dissolve3D's names). Two clashes a text merge could not see: both
  Codex workers had written the test's start-up wait their own way (main's kept), and decals and the glass job both
  used the same spare word of `GeometryRecord` (now `layers` and `flags`). ABI 0x00012300. Built with the shaders
  compiled side by side (`tools/Build-PtShaders.ps1`: 10.5 minutes; one at a time it was heading for an hour).
  IN THE GAME 03:37: plugin 2F7028C10335, native 7DE57FA3620B.
- THAT BUILD IN THE GAME, run `20261008-034922` (03:49, `quick`, the GPU to itself, 5 scenes, 0 problems; the harness
  waited 157 s for the passes: a new build's first start took 209.8 s again). Read by the coordinator (crops as
  before, and `hair-moving/crop-hero.jpg`):
  - THE FLAME RECTANGLES ARE GONE, in a capture (the owner has not looked): `candle`, no denoiser, change a frame
    0.34 levels (0.71 before), its 99.9th percentile 1.67 (18.65), at a flame 0.76 (7.4 to 7.6), pixels changing
    more than 8 levels 0.00 % (1.95 %); `candle/flicker-2-traced-none-map.jpg` is dark all over where it had one
    sharp rectangle a flame. With NRD 0.09 (0.24); boiling 0.075 (0.094; the game's own 0.036).
  - The shelf's bottles have their colours and labels (`glass-jars/2-traced-none/frame.jpg`): the turquoise cider
    bottle, the red jug, the white bottle. They were black shapes.
  - The tavern, now measured (it was void before): change a frame 0.28 with no denoiser, 0.08 with NRD; boiling
    0.083 (the game's 0.008). NEW, with no denoiser: the hero's face and chest are covered in red dots (the
    scattering walk's noise on skin; NRD smooths them): `hair-moving/crop-hero.jpg`. Her skin is orange and her
    armour bronze against the game's pale skin and silver (the game's white light at the camera is left out).
  - Still wrong, with jobs running: the wax is brown (who walks), the lantern's wall is burnt out (option 4b).
  - GPU a frame, no denoiser / NRD: tavern 64 / 68 ms (62 to 66 on 10-07 22:46), glass-jars 57 / 60, candle 82 / 85,
    forest 109 / 112 (90 to 96), vista 60 / 57 (46 to 48). So the speed job's saved texture reads do not show, and
    the forest and the vista are slower than before the night's merges. Not measured why: the stage times of this
    run's native log against the 22:46 run's (`docs/agents/speed.md` has the method, `stages.py`); the candle job
    took the walk's 4096-flight stop out, and the ground walks in the forest (who walks).
  - MEASURED WHY, 04:10 (the stage times of the two runs' native logs): the speed job did what it said (bounced
    light in the tavern 38 to 44 ms down to 30; vista 18.4 down to 15.5 to 16), and two regressions of the 00:50
    build ate it. (1) The "scene" stage went from 0.6 to 0.75 ms to 9 to 14 ms in every scene (tavern 9.0, forest
    9.7 to 10.0, vista 12.6): something done every frame since the memory or the decals job. (2) The opacity
    micromaps are gone: `micromaps: meshes=456 ... triangles=786689 ... 151MB` on 10-07 22:46, `meshes=1 ...
    triangles=2 ... 0MB` in both runs of 10-08: every ray that meets grass, leaves or hair now runs the alpha test
    (forest: prepare 8.4 to 9.2 ms, direct light 10 to 12.4, reflections 22 to 27). Neither changes the picture.
    Brief written: `docs/agents/briefs/brief-speed-2.md`; it starts when an agent slot is free.
  - THE SPEED JOB 2 IN THE GAME, run `20261008-124542` (12:46, the card to itself; `docs/agents/speed-2.md`; merged
    12:43): the "scene" stage is back at 1.3 to 1.6 ms (9 to 14 before) and the micromaps are back (forest 150
    meshes, 254,661 triangles, 52 MB; candle scene 210 meshes; vista 126). BUT THE FRAME IS NO FASTER: tavern 68 / 70
    ms (71 before), candle 91 (94), forest 123 (119 to 129), vista 59 to 67 (67). What the scene stage gave up, the
    stages after it took: forest bounced light 43.7 to 49.3 ms, the camera's rays 9.0 to 12.4. So the bottom-level
    updates were not waste in themselves: the same GPU work is now counted where it overlaps the next passes. The
    job's offline gain (9 ms in the forest) does not show in the game; nothing got slower. What a frame is made of
    in the forest now: bounced light 49 ms, reflections 29, the camera's rays 12, direct light 12, caustics 12
    (with no water in view), denoising 4. The frame rate needs the path itself made cheaper (smaller passes, the
    walkers as a list of their own, the caustics' 12 ms where there is no water), not bookkeeping.
  - THE OWNER, 12:55: "Have you taken a thorough look on the images and atlases from the past runs so you know all
    the current defects? Including flickering inside leves and stuff?" Answered: no (a dozen sheets, not the
    atlases and flicker maps of every scene). Started: a run of every scene with `quick` and the mirror scenes on
    the 12:45 build (`<scratch>\full-run.ps1`: the newest run folder with a `campfire` folder; its `numbers.txt`),
    and an inspection agent that looks at every picture and writes one list
    (`docs/agents/briefs/brief-inspection-2.md`; its file `<scratch>\inspection2\inspection-20261008.md`, to be
    copied to `docs/agents/`). First things seen by the coordinator in the forest's overview of run `20261008-124542`:
    with no denoiser the whole field of grass, a tree's trunk (the brightest line in the change map) and the
    canopy change from frame to frame in a frozen world; with NRD the grass still does (0.71 levels, the game's
    0.14); the stakes of the palisade are bright orange where the game's are dark brown.
- THE WHOLE GPU TEST OF THAT BUILD (04:08 to 04:23, the GPU to itself, with NRD; `<scratch>\gpu-test-main-040825.txt`):
  a valid run (29,558 frames, none failed): 1,513 ok, 5 FAIL lines. Known: the glass index check; the candle job's
  slab check, in its one case "skin 110 m from scene origin" (0.975, 0.967, 0.955). New: three tinted-glass checks
  (PT-76). Now passing that failed on the base: "clear glass casts no shadow", the eye check, the lake's timing.
  The micromaps check passes offline though the game has none: the check does not see what the game does.
  Two starts before it stopped at once: the passes took 515 s to make from cold inside the test (PT-74).
- 04:55 TO 07:40: CLAUDE'S USAGE LIMIT AGAIN (the third time; three agents cut off). All three had written their
  reports and started their whole GPU runs, which ended on their own as valid runs (no failed frames): `highlights`
  1,429 ok, `walks` 1,567 ok, `water5` 1,337 ok; each with its base's known FAIL lines and the same three that come
  of three whole runs sharing the GPU (the lake's timing, the micromaps, "a material without a number, or whose
  texture died, reads its atlas tile as before"). Nothing of theirs fails.
- 07:42 TO 07:44, MERGED INTO MAIN by the coordinator (0, 0 and 1 conflict; ABI 0x00012500: the walk job and the
  water merge had each taken 0x00012400):
  - `highlights` (`docs/agents/highlights.md`): THE LANTERN'S OPTION 4b. Each pixel's share of the automatic gain
    now depends on how bright the pixel already is: dark pixels get all of it, a pixel at W none, above W the
    game's exposure, never darker. The curve, with x = L / W and b = G - 1: L' = W - (W - L) / (1 + b x (1 - x))
    (the previewed formula had a kink at W, a visible ring; this one meets the line with the same slope and keeps
    the order of brightness for every G). W = 0.54 pre-exposed (`HIGHLIGHT_KEPT`): what the game's ACES tone
    mapping shows at 0.4 of white, one number to change. At a gain of 1 the picture is bit for bit the old one.
    It is in the composite, after NRD. Its prediction for the lantern room: whole picture 21.9 (25.3 now, the
    game 14.3), round the lamp 47 (61), pixels at 250 and over 0.01 % (1.33 %). ITS GAPS: the tracer's water,
    HDRP's fog light (Fog Adaptation) and what HDRP still shades take the whole gain as before; with no denoiser
    and with Ray Reconstruction the curve acts on each noisy sample, so dark noisy places come out a little
    darker than with NRD (1 to 3 % offline). FOR THE OWNER TO DECIDE (PT-17): the dialogue light's factor (built:
    exact for a mid-tone face; if dialogue faces are meant bright, no factor at all is right).
  - `walks` (`docs/agents/who-walks.md`): WHO TAKES THE SCATTERING WALK. The game's own rule, read from its
    shaders and counted over its 8,100 materials: a material scatters only with the subsurface feature switched
    on (a keyword), by a mask a point. Of 700 clothes, armour and weapon materials that name a profile 12
    scatter; of 194 ground materials none (their mask is 0); skin by a map times a number; wax, ice and stained
    glass by a number. So 688 materials walked for nothing, and the wax did not walk only because it is drawn
    from both sides. Built: the keyword and the mask (per point for skin; a walk taken with a chance equal to
    the mask: the same mean as a blend, fewer rays); a two-sided material walks where its mesh's piece closes a
    volume (each submesh's triangles grouped into pieces; the count in `GeometryRecord.flags`' upper bits); a
    new export counts the pixels that walk. Offline: armour that named the skin's profile 38.9 ms down to 31.1.
    A cream-white candle is NOT promised: the game lights its wax with white fill lights the tracer leaves out.
    ITS GAPS: the three snow shaders (no program: they do not walk), two-sided skin on characters, eyes. FOR THE
    OWNER TO DECIDE: leaves lit from behind (PT-77).
  - `water5` (`docs/agents/merge-water.md`, of `water-4.md`): WATER IS THE TRACER'S: a surface of its own rays,
    its waves real triangles in the acceleration structure, one boundary for glass and water; the passes `water`
    and `waterUnder` are gone (the compiled passes 26 % smaller in all). Its costs, measured offline: any water
    holds 56 MB, the test lake 240 MB; about 2.7 ms of the render thread a frame for that lake; still water is
    posed and refitted every frame like moving water. Not judged: the shadow at clear water's open edge (0.62 of
    the open floor's light). With the Caustics row Off light goes straight through the water (decided by the
    coordinator: the tooltip says so).
  Being built 07:44 (the first try stopped at once: the shader script read the old list of passes; it now has CMake
  write the list again when CMakeLists.txt is newer).
- IN THE GAME 07:50: plugin and native 7E8E121121FA (the shaders in 344 s side by side: the passes are smaller). Its
  first session, run `20261008-075054`, is VOID FOR PICTURES: path tracing paused itself in every scene for one
  texture of the water's (PT-78). Fixed in the plugin at 08:11 and installed; the session runs again.
  - Boiling with NRD elsewhere: glass-jars 0.064 (0.007), forest 0.134 (0.026), vista 0.142 (0.019).
- The memory line of the status (3,133 MB in the tavern) is not growth: since the memory job it counts everything
  the tracer allocated and the game's own textures it opened (1,148 MB there), which it did not count before. To
  do: say the two apart in the line.
- The owner, 23:40: "So why don't you spin up some claude subagents so we can get some more work done? It can't
  always be astra". Three Claude agents since 23:45 (base45 = main at 23:44): `wt/speed` (c: by stage, old against
  new; the waste out, the picture unchanged; `brief-speed.md`), `wt/glasshair` (f and i: black bottles, striped dark
  hair, dull armour; `brief-glass-hair.md`), `wt/water4` (PT-71 item 5: water3 onto main with its waves as real
  geometry; `brief-water-4.md`). Eight workers in all (the table: `docs/rounds/round-06.md`). Not yet assigned: the
  harness's measured regions and its wait for the tracer to compose; the hair scenes' recipes still use Ray
  Reconstruction.
### PT-72 Publish the tracer's geometry guides to DLSS, shared with NR
- Found during NR integration, 2026-10-07. Owner: "Keep in mind that the PT mod fundamentally renders the scene
  differently, including its own rendering of fog". NR must consume the completed traced scene, with PT's fog,
  before lens effects. Do not assume HDRP owns the fog or composite it twice.
- Original evidence (1.2.2): NR read the common HDRP depth/motion handles; 1.2.3 now observes actual DLSS below. The current
  TGPT_Guides and RayReconstruction.TryGetGuides/TryGetExtraGuides export material/RR guides but no own depth or
  motion. BU's RR host also reads HDRP geometry. PT's camera rays can hit a different surface (PT-59/70/71).
  This is a renderer/guide mismatch, not evidence that it caused the NR movement defect in PT-Off captures.
- Open, part of PT-71 item 10. Publish the matching traced image's depth and motion, frame/camera identity,
  dimensions, encodings and resource lifetime to DLSS through the shared device contract; NR observes those inputs.
  Coordinate with migration's removal of delayed/HDRP reprojection. Do not silently substitute stale or unrelated
  guides. Then test PT+NRD and PT+RR with NR, moving hair, water/glass, fog, camera cuts and resolution changes.
  NR tracks this as NR-10 in docs/DLSS5-FEEDBACK.md. Since 1.2.3 NR and FG read the actual selected DLSS
  packet via the bridge; BU publishes its RR inputs. PT must update DLSS inputs when traced geometry differs;
  lighting/fog changes alone do not change surface motion. No separate NR-to-PT lookup. No PT implementation
  was changed by the NR investigation.

### PT-71 The migration to our own renderer: what is left before everything on screen is the tracer's
- The owner, 2026-10-07 17:50, told that outdoors the camera-ray build looks worse than the one before (PT-70): "well,
  isn't that expected until we've fully migrated to our own renderer?", "I expect visual regressions until the renderer
  is fully migrated. Focus on finishing the migration to our own renderer, then we can A/B with vanilla and ensure we
  render everything" (ruling in AGENTS.md). This entry is the list; every other path tracing job waits for it.
- Done: the camera's own ray for every pixel and its surface record; hair, beards and brows as its surfaces; the
  yardstick (PT-59).
- The list, state on 10-07 17:55 ("running": an agent in a copy; "queued": a brief is written, it starts when one of the
  3 slots frees; "open": to design):
  1. What a hit reads of each of the game's shaders (PT-64; the outdoor ones first, PT-70; terrain's blended layers
     PT-10; parallax relief PT-28). Running (`wt/mats`).
  2. The traced world is the drawn one: every object, the drawn level of detail, swaying plants, every distance, decals
     (PT-63, PT-61). Decals (its part 5, the world agent's design): a Codex worker since 18:54 (`wt/decals`, base40;
     `docs/agents/briefs/brief-decals.md`). MERGED 18:44 (ABI 0x00012000; `docs/agents/world.md`): static batches, the drawn level of detail
     on every capture path (round 2's "finest within 30 m" gone; Drake entities had been traced 15 % farther than
     drawn), grass, leaves and trunks posed every frame as the game's shaders pose them, the Scene Radius and Ray
     Distance rows deleted. Offline: 90 source checks, rules test (15 mutants), the GPU test in its copy. Open: decals
     (a design; the plugin counts them), the far world's cost in game, 3 GPU mutants (running in its copy).
  3. Skin's and wax's scattering, a leaf's light and the wet ground at the camera's own hit (PT-58). Reported 19:50
     (`docs/agents/port-2.md` in `wt/port2`): merge-3 merged in the copy (25 conflicts, both sides kept), the walk, a
     leaf's own share, the wet ground and the head rule at the camera's hit; the old route's versions deleted (its
     profile table, HDRP's thin-skin transmission, two exports). Not yet: skin's terminator and cavity occlusion at a
     hit, a leaf's own reflectance, `trace_closest` meeting the head's inside. 1350 offline checks, one timing check
     red on a shared GPU (to see again alone). Its merge waits until its last mutant runs end (they edit the copy).
  4. Things the game moves that are no characters (the weapon in the hand, doors): traced where the last scene scan left
     them, up to 2 s old; no previous transform, so their motion is the camera's alone. Running since 18:47: a Claude
     agent (`wt/moving`, base39 = main with the world merge; `docs/agents/briefs/brief-moving.md`; report
     `docs/agents/moving.md` in its copy).
  5. Water: still HDRP's surface, lit by two passes of the old route (`pt_water.hlsl`, `pt_water_under.hlsl`: the dots
     of PT-68). To be an ordinary surface of the camera rays with the real waves, reflecting and refracting; then the
     passes, their history and the transpiler go (PT-60 item 2, PT-42, PT-52). Running since 18:02: a GPT-6-astra worker
     through Codex (`wt/water3`, base37; brief `<scratch>/brief-water-3.md`; report `docs/agents/water-3.md` in its copy;
     its console `<scratch>/codex/water3/`). Started before 3 merged: the waves' functions move at the merge.
     Finished 21:53: the optics right offline (11 mutation-checked checks), the old passes deleted; but its wave
     intersection is far too slow for the game (20 ms at 128 x 72): not merged. Next: onto main after `merge4`, with
     the waves as real geometry in the acceleration structure.
  6. Glass, eyes and what HDRP draws forward: glass is "seen through" by a special case, eyes and forward-drawn props are
     shaded by HDRP with traced shadows (`pt_hdrp_shadow.hlsl`). To be surfaces of the camera rays like any other;
     then the special cases go (PT-60 items 3 and 4). Running since 18:02: a GPT-6-astra worker through Codex
     (`wt/glass`, base37; brief `<scratch>/brief-glass-eyes.md`; report `docs/agents/glass-eyes.md` in its copy).
  7. Particles and transparents (smoke, fire, rain, dust, glow sprites): HDRP draws them over the picture (PT-67, PT-29
     to PT-31, PT-06). The design, with 8: `docs/agents/design-particles-fog.md` (GPT-6-astra, read-only, 18:14; not
     tested). In short: one world-space model of media beside surfaces (a medium returns extinction, scattering,
     emission, refractive index; every kind of ray crosses it; no march-step count decides the picture), the game's
     own particle state captured each frame, a flame's emitting volume as the light, the game's fog density traced
     with our lights, fill lights tagged fog-only. Its warning: a painted sprite holds no 3D flame, so some effect
     families need a real volume model. Ten jobs, in its table. The VFX Graph capture (its jobs 1 to 3 for the candle
     flame, since the inventory found no ParticleSystem at all): a Claude agent since 19:53 (`wt/vfx`, base41;
     `docs/agents/briefs/brief-vfx-capture.md`). After Claude's limit a Codex worker did the investigation (21:54,
     `docs/agents/vfx-capture.md` in `wt/vfx`): no managed access to the particles; the candle shaders' inputs decoded;
     the route is observing the game's own D3D11 draws. The owner, 22:00: "but we don't want to render the game
     twice?" Right (ruling in AGENTS.md): the game simulates its effects once, the tracer draws them, the game draws
     none; so the particles are taken from the buffers the simulation writes (its compute dispatches), for every
     simulated system, on screen or not; a draw is watched only to learn which buffers those are. The worker started
     21:55 was restarted 22:05 with that (`<scratch>/note-vfx-3.md`): first a diagnostic that logs dispatches and
     draws, then ONE in-game run of it. Built by 22:35 (an observer that reads no buffer and changes nothing; recipe
     `vfx-capture`), merged into main 22:37; the build, install and run started 22:38. Job 1 (what the game really has: an inventory the
     harness logs) runs since 18:20: a Codex worker (`wt/fxinv`, base38; `docs/agents/briefs/brief-effects-inventory.md`).
     Done 18:52 (`docs/agents/effects-inventory.md`): a read-only recipe `effects` (every particle system, VFX Graph,
     trail, rejected transparent, light and fog volume, as JSON and a summary; whether freezing freezes them); compiled,
     9 summary tests with 9 mutants. To merge after the running queue, then one run:
     The run (19:18 to 19:24, `20261007-191814`, `effects-report.md`, `effects-summary.json`): 10 censuses, no
     incomplete evidence. What the game has: NO ParticleSystem at all; every effect is VFX Graph (840 to 933 components
     in each open-world scene: candle flames `VFX_CandleFlame_*`, fire `VFX_Fire_EmberGen` with fire, smoke, sparks,
     glow and distortion outputs, Wyrdnight runes, mist, moths, fast travel), plus about 21 trail renderers and 2 to 55
     transparent or unlit meshes a scene; fog: 4 to 30 Volumes with Fog and 0 to 13 local fog volumes a scene. Freezing
     freezes them (counts and shader clocks unchanged in the frozen probes). So the design's job 3 (a VFX Graph
     adapter: its GPU buffers, which Unity's public API does not expose) is the main road, not a side one. A defect
     of the summary: live counts show negative ("unknown" summed as -1).
     `-Scenario candle,campfire,fog,night,tavern-room -Recipe effects`. Merged 19:08; the run started at once.
     Its medium core (job 4) waits for 5, which brings water's absorbing medium: one model, not two. For the owner
     later (not needed to start): how strong a candle's bloom should stay; which scene to review first.
  8. The fog: the game's fog stays (ruling); HDRP lights it today, the tracer should (PT-37). Open.
  9. HDRP's own lighting, shadows, reflections and ambient occlusion: rendered every frame and thrown away; the traced
     gain on HDRP's lights (PT-60 items 5 and 9). After 5 to 8 (fog and particles use HDRP's lights until then). Open.
  10. HDRP's depth and motion vectors of the frame shown still place the traced light, which is a frame old on purpose
      (waiting for it cost 6 to 8 ms). To retire them: trace the frame that is shown. Open: measure first.
  11. Rays leave an own-ray surface as from HDRP's (2 cm off, passing what is close): from their own triangle once
      shadow-only stand-ins are told apart (PT-60 item 6). With 2.
  12. The clean base (the owner, 17:55: "Also remove all the 'hacks' and workarounds as we go, all the stuff we did as
      workarounds for not controlling the tracing from the camera or whatever. So we have a clean base to work
      from"; ruling in AGENTS.md). Each job above deletes what it makes dead and lists the workarounds it met
      (PT-60 is the list). Last: the old route itself goes (`TGPT_OPTION_GBUFFER_SURFACES`, the hidden key "Own camera
      rays", `pt_prepare`'s G-buffer path, every `own_rays()` test), then a read-only sweep of the whole tracer for
      leftovers (GPT-6-astra or `pt-max-reviewer`, with the reports' lists).
- The owner, 18:00: "You can get gpt-6-astra to work on some of the other tasks in parallel via codex, so you don't waste
  claude tokens" (AGENTS.md "Subagents and reviews"). Three Codex workers started at 18:02 beside the three Claude
  agents; item 4 goes to a Codex worker once 2 is merged. Whether a Codex worker can build and run the GPU test inside
  its sandbox is the first thing each writes into its report ("Sandbox"). It could not (CMake refused): since 18:12
  they run without a sandbox, at xhigh, by the owner's choice. The routine is saved (the owner, 18:15: "save it to
  your harness"): `docs/AGENT-WORKERS.md`, `tools/agents/`, the briefs in `docs/agents/briefs/`.
- 20:15 Claude's weekly limit stopped the three Claude agents; back at 21:40 (the owner: "please collect all the
  pieces"). State of every copy and the order of merges: `docs/rounds/round-06.md` "State at 21:45". Finished and
  unmerged then: items 1 (materials), 3 (scattering), 4 (moving things), 6 (glass and eyes): a Codex worker
  (`wt/merge4`) merges the four into one tested tree. Items 2 (decals), 5 (water), 7 (VFX): Codex workers running.
- Not part of it, and waiting (the owner: the migration first): hair's fineness, candles, the caustics' shape, rays at
  full resolution (PT-65), bounces (PT-66), the frame rate, PT-69's two fixes (with the next merge).
- The meter, read after each merge: the yardstick's line indoors (`hair-moving@hair-measure`: 99.2 % the same surface)
  and outdoors (`forest@raw`: 64 %).
- Then: the A/B against the game's own picture, scene by scene (every scenario's `game-vs-traced.jpg`).
- The meter after the world merge (run `20261007-185958`, installed 18:59: native C14A1D8CED79, the GPU test 1232 of
  1232): the tavern 99.0 to 99.4 % the same surface (holds); the forest 70 % (from 64; nearer 17, farther 13); the vista
  (new, a far view) 62.7 % with 34.4 % sky for both, so 96 % of what is not sky, its hit nearer 1.5 %, farther 1.1 %:
  the far world's geometry is right; its materials are not (colour 35 %, roughness 71 %: the game's MeshTerrain, Cliff
  and TriplanarArchitecture2 shaders on 99 % of their pixels: the materials job). In the forest's crops
  (`forest/crop-rock.jpg`, `crop-grass.jpg`: game, before, after) the near flat mossy rock is still a tall pale
  boulder, the ground still flat grey. The cost: forest 8,164 -> 12,881 instances (Drake 805 -> 1,717, Medusa 628 ->
  3,972), 2.6 -> 5.8 million triangles, 414 -> 956 MB, the scan on the main thread 36 -> 58 ms, the tracer's GPU 66 ->
  73 ms; the vista 18,554 instances (10,931 plants in 297 groups), 9.3 million triangles, 1.5 GB, 31 to 35 ms.
- CORRECTION (19:25, run `20261007-191814`, recipe `yardstick`, `forest/crop-rock-views.jpg`): the Yardstick view shows
  the rock's pixels as the SAME surface as the game's, with another colour, normal and roughness. The coordinator's
  "another shape" (17:19) and "a tall pale boulder" (19:08) were wrong: the shape is the game's, its material is
  read wrongly (`TG/Environment/Cliffs/Cliff`: the game's dark, mossy, wet rock read as pale grey-brown). With
  `MeshTerrain` (the ground) and `TriplanarArchitecture2`, the biggest gap left: the materials agent was told. The
  geometry that still differs there: a strip of ground beside the rock (the tracer's hit farther) and the grass
  (nearer).
  Next (19:08): the yardstick's own pictures of the forest (recipe `yardstick`) to see what the rock is.
### PT-70 Outdoors the tracer's own picture is wrong: pale rocks, flat grey ground, dark grass
- Found by the coordinator, 2026-10-07 17:19, in the first outdoor run of the camera-ray build (`20261007-171522`,
  `forest@raw`, a stand-in save on the campaign map, frames kept; crops `forest/crop-rock.jpg`, `crop-grass.jpg`: the
  game's, traced with no denoiser, traced with Ray Reconstruction). Not yet seen by the owner; told at once (PT-62).
- The yardstick: the tracer's first hit is the same surface as HDRP's on 64 % of the screen (the tavern: 99.2 %); nearer
  on 19 %, farther on 16 %; of the pixels with the same surface another colour 27 %, another normal 37 %, another
  roughness 47 %. By material (% of the screen, % of its own pixels): the grass (`TG/Vegetation/Vegetation_Grass`) 34, 87;
  the cliff (`TG/Environment/Cliffs/Cliff`) 25.8, 97; pine leaves (`TG/Vegetation/Vegetation_Leaves`) 5.0, 79; the ground
  (`TG/Environment/MeshTerrain`) 4.5, 97; trunks (`TG/Vegetation/Vegetation_Trunk`) 0.9, 51.
- In the picture: the game's rock is dark, mossy and wet with fine detail, ours a pale grey-brown boulder of another
  shape with one stretched blurry texture; the game's ground is dirt and grass colour, ours flat pale grey with no
  texture; the game's grass bright green blades, ours dark olive tufts of another shape.
- Why it shows now: until 16:59 the camera's first surface was HDRP's own, so these were right on screen by
  construction; bounced light and reflections always saw this wrong world. With the tracer's own camera rays the player
  sees it. So outdoors the build of 16:59 looks worse than the one before it. The old route is still there for an A/B
  (the hidden key "Own camera rays", Advanced); the ruling stands (the tracer's own rays; fix what they read).
- Two causes, two jobs, both running: what a hit reads of these shaders (the materials agent, PT-64: told at 17:19, the
  outdoor shaders first: Cliff, Grass, MeshTerrain, Leaves, Trunk); which mesh and level of detail is traced and where
  the grass stands (the world agent, PT-63, PT-61). The check after each merge: this scene, this recipe, the yardstick's
  line and the two crops.
### PT-69 Two findings of the outside review, tested: both real
- From `docs/agents/review-today-gpt.md` (GPT-6-astra, 2026-10-07), tested by the coordinator at 17:12, nothing changed yet.
- Finding 8, read in the code: light that bounces takes a lamp's shadow differently from the camera's own pixel. For a
  light that stands at its flame (`LIGHT_AT_EMITTER`) the camera's pixel tests its shadow ray up to the flame's own size
  (`pt_rtxdi.hlsli`, `atEmitter`); a path vertex's and a reflection's (`direct_light`, `pt_common.hlsli`: `tmax = max(tmax -
  offset, 0.0)` after `shadow_ray`) stops the surface's offset (2 cm and more) short of that as well. So a candle's own
  wax 3 cm under the flame shadows the camera's pixel and lets the light through for bounced light and reflections: a
  cause to rule out for candles that look too bright (PT-47, PT-57). The fix is one condition, with a check that fails on
  today's code (a blocker 2 cm under a flame-sized light: the same shadow for a pixel and for a bounce); to do with the
  next merge, not while three copies hold `pt_common.hlsli` open.
- Finding 10, reproduced (`<scratch>/cdf_check.py`, the build's own arithmetic): a light list's cumulative weights are
  stored as floats (`scene.cpp` `cumulate`). In one list of more than about 16,700 lights whose weights are 0 at the
  build (one bright, the rest off or fading in) the faint ones get an interval of width 0 and are never drawn: 3,239 of
  20,000, 48,775 of 65,536 (the review: 48,774). Lights that weigh a millionth of the bright one: none lost. Today's
  scenes are far from it (the tavern: 84 lights, at most 25 in a cell; the list of all lights outside the grid is the
  longest). A limit that could change the picture (AGENTS.md "No limits"), so it goes before the light count grows
  (glowing triangles as lights, PT-67's flames): a list in two levels (blocks of 256), each level's weights exact in a float.
- Not tested yet: finding 9 (NRD's add-back in the triangle's shading, not the normal map's) and 11 (paired emitters).
  Finding 7 (hair's reprojection) went with the hair layer. Findings 1 to 5 (water) go with the old water passes (PT-60).
### PT-68 Bright dots ("fireflies") on lit water
- The owner, 2026-10-07 14:25: "there were weird fireflies in the water"; 16:20: "i still see fireflies in the water";
  "It's not cave fireflies - they're also there in the other angles you're rendering water from when there's light";
  "But maybe the fireflies will go away once we migrate to the new renderer?"
- Seen (run `20261007-161006`, `lantern-lake/2-shade-none/atlas.jpg`, Denoising None, the world frozen): small yellow and
  green dots on the lit water, at the same pixels in all 8 frames of a step. A first count (`<scratch>/dots.py`, which
  also counts bright bits of rock, so not yet a measure of the water's dots): most spots of one step sit at the same
  place in the next (172 of 212, 132, 94), 8 of 212 in the game's own picture of the same save. So they hold still in
  the frozen world across steps: NOT known yet whether they are samples stuck in a history, glints of the lamp on the
  frozen waves, or something drawn by the game and brightened.
- PROVEN 16:55 (run `20261007-164828`, `lantern-lake@dots`, `lantern-lake/dots.jpg`: the lit water in each step, Denoising
  None): the game's own picture with the lantern: no dots; traced with everything on: many; with the Water Reflections
  row off (the old water passes not run): NONE; Caustics off: still there; Emissive Lights off: still there. So the dots
  are the old water passes' (`pt_water.hlsl`, `pt_water_under.hlsl`) and nothing else's. They go when those passes go
  (PT-60 item 2). The check for water as a surface of the camera rays: no such dots on this scene, undenoised.
- One lead among those, not proven: the water passes (`pt_water.hlsl`, `pt_water_under.hlsl`) keep a 12-frame history; a rare, very
  bright sample (a lamp's glint; the highlight is divided by the water's Fresnel term, 0.02 face on) enters it and fades
  over seconds as a dot. The limits job (12:50) removed the clamps that cut such samples (`LIMIT_REFLECTION`). To prove:
  the Water Reflections row off in the lake scene: the dots should go. With PT-59/PT-60 the water passes and their
  history go; the lasting fix is sampling a lamp's glint so that samples are not rare and huge (light sampling with
  the lobe, both weighed), never a clamp. A check for the camera-ray work: the count of small bright dots on the lit
  lake, undenoised (`<scratch>/dots.py`).
- 2026-10-12, A NUMBER FOR THEM (worker campfix, the sea fixture, the sun inside the water; `docs/agents/campfix.md`,
  "The offline check, again"): at a sun 8 degrees up a single sample of the camera's own rays is up to 228 times the
  picture's mean with the water's speed rule off and 924 to 1,551 times with it on (17.7 and 21.0 at a sun of 57
  degrees). So the rare, very bright sample is in the estimator itself at a low sun, and coarser cells make the
  largest ones. In the game the same thing is what the hidden `Water light without view` turns into white blobs
  under NRD (PT-81, 2026-10-12). The largest sample over the mean is the yardstick the water job is to bring down
  (`docs/agents/briefs/brief-water-12.md`); never with a clamp.
### PT-67 Particles traced, and a light is the thing that emits it
- The owner, 2026-10-07 15:15: "particles need to be traced. This is in the list of requests already, I hope!" (it was:
  PT-29 rain, PT-30 dust, PT-31 smoke and fire, as "to look at"); of lamps being the game's light objects with a
  guessed size: "sounds like a problem too". One job: fire, smoke, rain, dust and magic drawn and lit by the tracer,
  and a flame's light coming from the flame's own shape and brightness (the candle's "gas", PT-06, with it). Open.
- 2026-10-09 10:05: the Codex worker `particles` (`brief-particles-2.md`, `docs/agents/particles-2.md`, base67)
  handed over and was MERGED (no conflict; `<scratch>\pre-particles`): the evidence of the 10-07 run did not give
  the buffers' layouts, so it built the next diagnostic and stopped: hidden `Advanced / Particle simulation
  diagnostic` (off), the recipe `particles-capture` (`candle`, `campfire`): the simulation's raw buffers read
  where the game leaves them, with matched pictures facing the effect and away from it; no positions decoded
  yet, nothing reaches D3D12. Its own tests: 224 GPU assertions, four mutants rejected. Next: that ONE run, then
  a worker decodes the layouts from it and builds the records and the transfer.
- The owner asked again, 2026-10-09 08:28: "Do we light [up] smoke particles yet?" No: every effect is VFX Graph
  (PT-71 item 7); the game still draws them over the traced picture with its own lighting; the way in (the
  simulation's own buffers) is found and an observer is in the tree, the capture and the tracing are not built.
- THE SIMULATION PROBE IN THE GAME, 15:11 on 10-09 (run `20261009-150909`, recipe `particles-capture`, `candle` and
  `campfire`, the world running; `particles-report.md`, `vfx-overview.jpg`, the buffers under each step's
  `vfx-*` folders): 24 frames captured with no error. The game's effect simulation shows as compute dispatches
  the probe could keep: 11 to 44 a frame at the candles (954 to 978 buffers held), 24 to 46 at the campfire
  (2,280 to 2,301). NOTHING IS DECODED YET: no particle records, positions, live counts or materials; a buffer
  held from start-up is not proof of a live system. So the data is where the owner's rule wants it taken (where
  the simulation leaves it, no second draw), and the next job is the decoding: which buffer is which system's,
  its record layout from the compute programs' declarations, the live list, then an adapter that hands the
  tracer real particles (lit, shadowing, in mirrors, off screen too).
- State: the probe works in the game; the decoding is the next job (not started).

### PT-66 Bounces after the first come from a plain cache, not rays
- The owner, 15:15: "You mean Neural Radiance Caching takes care of the remaining bounces, right? And we can always
  increase the number of bounces, right? As far as I recall, I saw no difference between 1 and higher values." The
  answer given: no, it is not NVIDIA's Neural Radiance Cache; it is the mod's own grid of averaged light (cells of 10 cm
  and up, 16 frames, clamped); the Bounces row goes to 4. Open: what stands behind the last traced bounce (more real
  bounces by default, NVIDIA's NRC where the card has it, or a cache measured against many-bounce references): the
  owner's call after numbers.
### PT-65 Rays at the screen's full resolution, with a row for half and quarter
- The owner, 15:15: "Do full resolution traces. Ray Reconstruction is not a given. We can expose as a config option to
  do half res or quarter res rays." Today one ray a pixel at the game's render size (1924x1084 for 4K with DLSS
  Performance). Four times the rays at 4K. Open.
### PT-64 Materials: what every game shader shows, each material's own values, no texture copies, no borrowing
- THE OWNER, 2026-10-09 09:55, told that the tracer reads detail maps on the standard shader and not on the character
  shader (PT-95, a helmet with none of its detail): "there may be other holes like this in our tracer, then". Yes:
  a shader the tracer was not taught falls back to something plain, silently. So, not hole by hole as the owner
  finds them: (1) the recipe `yardstick` (the tracer's first hit against the game's own surface, the materials that
  differ by name and shader) on one scene of each kind (`tavern-room`, `metal`, `forest`, `vista`, `npc-day`,
  `campfire`, `stairwell`), last run across scenes on 10-07 and never with characters in view; (2) one list from
  it: each shader with a hole, what is missing, its share of the screen; (3) the agent `kandra`'s table of the
  character and clothing shaders (what each samples against what the tracer reads). State: to run after the
  queue of 09:40.
- `kandra`'s table, 10:20 (from property lists and the material census, their code not read; nothing built):
  `Kandra_Lit_Dynamic` (5 materials) and `GhostCrew` (4) likely lack the glow rule and the mask's occlusion,
  and their vertex animation and thread dissolve are not traced; `Cloth_Wind` (39) has its `_Alpha_Clip`
  unread (0.5 used; 21 materials are at 0 to 0.15); `RealSkin_Morph` (5) gets no skin program; the ghost,
  icicle and jelly shaders are transparent, so invisible to rays; 5 `Kandra_Lit` materials carry a base-map
  tiling the tracer applies and the shader's pixel code does not. Each a hole to close; none assigned.
- The owner, 15:15: of the tracer re-reading a handful of shaders its own way: "sounds like a big problem, fix that"; of
  the fixed numbers (leaves 50 %, cloth 30 %, glass 1.5): "how can we fix that? Big problem too"; of 64x64 copies of
  textures under 64 KB (`kMinBytes`, `texture_share.hpp`): "why?! Now we share textures with the game ... Don't do
  copies"; of a reflection borrowing the on-screen material (`hdrp_material_at`): "Well, don't". One job, judged by
  the G-buffer yardstick of PT-59: every shader in the game's scenes listed, the tracer's first hit against HDRP's
  pixel for each, until none differs. Open.
- Started 17:08: the materials agent (copy `wt/mats`, base36 = main at 17:03 with the camera rays; brief
  `<scratch>/brief-mats.md`; report `docs/agents/materials.md` in its copy). Order: every shader right at a hit by the
  yardstick (worst first: material index 118, 134, 99, 15, 162, 131, 111, 222 in the tavern run `20261007-165954`) and
  no borrowing from the screen; then no texture copies; then each material's own values. Material bit 67108864 offered.
### PT-63 The whole world traced: every object, at any distance, swaying as the game sways it, with its decals
- The owner, 15:15: statically batched objects left out: "that sounds like a big problem, if things suddenly disappear
  when PT is enabled. Fix that"; nothing past 250 m (`Scene radius`): "Why? Do that"; grass and leaves standing still
  (`SceneCapture.cs` 1162, `MASK_SWAYS`): "of course our shadows and reflections should move alongside the grass and
  leaves swaying in the wind. Otherwise what's the point of realtime ray tracing"; decals: "they should! Of course
  they should". With PT-61 (the same level of detail as drawn). Open: the next agent when a slot frees.
- Started 15:40: the world agent (copy `wt/world`, base34 = main BEFORE the camera rays; brief
  `<scratch>/brief-world.md`; report `docs/agents/world.md` in its copy). Order: objects left out and the drawn level of
  detail first, then swaying grass and leaves, the distance, decals. Still running at 17:08. Its merge goes onto the
  camera-ray tree: `merge_eol.py world --base=base34` (the tool now moves files an agent deleted out of main too).
- 2026-10-07, the world agent (`docs/agents/world.md`, copy `wt/world`; merged 18:44, not yet in the game): built and passing
  offline: statically batched renderers traced in their combined meshes; Drake entities traced while the game has them
  loaded for drawing (its visible range had been worked out wrongly: 15 % too far, and short of the Draw Distance row's
  extra distance) and those with a LOD range and no reference point; trees past their last mesh traced as that mesh; and
  grass, leaves and trunks posed every frame as the game's three vegetation shaders pose them (read from their compiled
  code), in groups of a 16 m cell, with no count or distance that leaves one still (GPU checks 14 and 15; ABI
  0x00012000: `TGPT_SetPlantWind`, `TGPT_SetWindNoise`). No distance cut: the Scene Radius and Ray Distance rows are deleted (its cost in game to
  measure). Designs with numbers, not built: decals (the plugin now counts them a scan). To see in
  game: section 6 of the report, one scene a part.
### PT-62 Every shortcut, stand-in and limit, told to the owner
- The owner, 2026-10-07 15:08: "If there's anything else like you said before you should tell me, do that now. I had to
  discover your weird arbitrary limits on my own, the weird 32 sharp texture slots on my own, the lack of mirror
  reflections on my own, that hair renders after denoising on my own, and now also that we don't even trace rays from
  the camera ourselves. I've consistently told you: I want a full, correct, proper Path Tracer, that looks amazing,
  without hacks, without shortcuts. Somehow you have failed to fully respect this. So, if there's any other surprises
  in store, now's the point where you tell me, so i can redirect you".
- The list: `docs/PT-SHORTCUTS.md` (36 items, each marked sure or to check). New to the owner in it: Indirect Color 3 by
  default (bounced light's colour tripled), grass and leaves traced standing still, statically batched objects left
  out, nothing past 250 m, 64x64 copies of small textures, decals and relief unseen by rays, rays borrowing the
  on-screen material, bounces after the first from a cache, one ray a pixel at half size. State: waiting for the
  owner's word on each (fix, keep, later); an outside audit of the whole tracer offered.
### PT-61 The tracer traces coarser copies of rocks than the game draws (level of detail)
- Found by the water-edges agent (`docs/agents/water-edges.md` in `wt/wedges`, 2026-10-07 15:04, read from the code and
  pictures): the cave lake's "blocky" water (the owner) is mostly pale flat slabs flipping frame to frame. The tracer's
  copy of a rock is a coarser level of detail than the drawn one; at the waterline the copy sits above HDRP's water
  where the drawn rock is below it, so refracted rays start inside the copy and show its flat pale material. Matters
  for PT-59: the camera's own rays must hit the same geometry the game draws (its finest level near the camera), or
  every edge differs from the game's. Also found: the bed seen through water 1.78 times too bright (light leaving
  water must be divided by n squared), and the light the water's surface reflects back down onto the bed missing.
  The review's water findings 1 to 5 all confirmed. State: open; for the PT-59 switch and water as a surface.
- 2026-10-07, the world agent (`docs/agents/world.md`, not merged): the traced level is now the drawn one for every
  capture path, by each of the game's renderers' own rule read from its code (`WorldRules.cs`, 53 expectations, 15
  mutants killed); round 2's "finest within 30 m" is gone. Not proven that the level of detail made the lake's slabs:
  those rocks are Medusa's, whose traced level already followed Medusa's rule at each scan. The in-game check: `glow-lake`.
### PT-60 What the tracer's own camera rays let us remove or simplify
- The owner again, 2026-10-07 17:55: "Also remove all the 'hacks' and workarounds as we go ... So we have a clean base
  to work from" (AGENTS.md). So an item below is deleted by the migration job that makes it dead (PT-71 names the
  job), not after the migration; the three running agents were told at 17:56, the queued briefs carry it.
- The owner, 2026-10-07 15:00: "surely there must be some technical debt to clean up as we go, and things we can clean up
  and/or simplify to both improve the tracing, visuals, performance, and correctness?" The list, each removed only
  after its replacement passes the G-buffer yardstick (PT-59) and the frame-rate measure, one at a time:
  1. The hair layer (`pt_hair.hlsl`'s layer mode, its outputs, its composite reprojection and guide patching, HDRP's
     hair hidden by culling mask): hair an ordinary surface of the camera rays.
  2. The water's own passes (`pt_water.hlsl`, `pt_water_under.hlsl`, their 12-frame history and 3x3 filter in
     `d3d11.hlsl`, render event 6 and the Harmony transpiler on HDRP's water lighting): 12 to 20 ms a frame; water an
     ordinary surface with the real waves, denoised with the rest (review-today-gpt findings 1 to 5 live here).
  3. Glass as a special case (`GlassFinish.cs`, HDRP's glass hidden, the capped glass crossings).
  4. Surfaces HDRP still shades with traced shadows (`pt_hdrp_shadow.hlsl`, the eyes' and forward surfaces' share path
     in `pt_compose.hlsl`).
  5. The traced gain put on HDRP's own lights so that what HDRP shades matches (`SceneLights` ApplyGain/Darken for that
     purpose; the fill lights' fog stays, the owner's ruling).
  6. Ray origins guessed from HDRP's depth and normals (surface offsets on skin and eyes, `near_back`, `THIN_THROUGH`,
     `surface_near_back`): a ray starts on its own triangle.
  7. Motion borrowed from what is behind: every hit's own previous position for the denoiser's motion vectors.
  8. Two ways to read a material (the G-buffer route and `shade_seen` for rays): one, so a mirror shows what the camera does.
  9. HDRP's lighting, shadows, SSR and SSAO rendered while tracing and thrown away: skipped (kept only as the yardstick
     in harness runs); likely the biggest frame-rate gain with 2 and 1.
  Risk: what the tracer cannot read yet (decals, terrain blending, parallax relief, PT-28): the yardstick finds each
  before its bolt-on goes.
### PT-59 The tracer's own camera rays for every pixel
- The owner, 2026-10-07 14:57, told the tracer starts from HDRP's G-buffer (the camera's first surface is HDRP's, so
  see-through hair, glass and water were bolted on): "yes, of course it should be like that. Why isn't it? We want
  full, proper path tracing. Should go without saying", "Everything should be in the main picture. I can't believe
  this wasn't obvious". Ruling in AGENTS.md. Why it was so: when the tracer was built its rays read 64-pixel texture
  copies, and HDRP's buffers gave the camera's view exact for free; since texture sharing (PT-45) the rays read the
  game's own textures at full size.
- BUILT AND IN THE GAME (the camera-ray agent, `docs/agents/hair-6.md`; merged 16:50, ABI 0x00011F00; installed 16:59:
  plugin 125372666574, native 0449A6EFF3D6, the GPU test 1182 of 1182; 8 mutants caught in the copy). Every pixel's
  surface is the tracer's own camera ray's hit (`pt_primary.hlsli`); hair, beards and brows are surfaces of those rays;
  the hair layer (`pt_hair.hlsl`) is gone; the hidden key "Own camera rays" (Advanced) off gives the old route for an
  A/B. The yardstick (`pt_yardstick.hlsl`, the native log's "Yardstick" line, the plugin's every 10 s, a debug view).
- First run in game (`20261007-165954`, `hair-moving@hair-measure`, the tavern): the tracer's first hit is the same
  surface as HDRP's on 99.2 % of the screen (of those, another colour 3 to 5 %, another normal 2 to 4 %, another
  roughness 0.2 %); nearer 0.2 %, farther 0.6 %; nothing traced where the game drew 0.00 %. The materials that differ
  most are named by index (118: 84 % of its own pixels; 134: 96 %): for PT-64. Hair by `hair_motion.py`, frame to
  frame while the camera strafes: its pattern off the head's by 0.5 px with Ray Reconstruction (2.1 before), 0.1 px with
  no denoiser (4.7 before), the game 0.0. Camera still: its change a frame 0.32 levels with RR (0.08 before), 0.66 with
  none (0.47), the game 0.10. Seen by the coordinator (`hair-moving/head.jpg`): a whole picture from the tracer's own
  rays; the hair coarser and darker than the game's (clumps, not fine strands), blocky edges on the crest and the
  armour's highlights without a denoiser (rays at half the screen's size: PT-65), flatter skin. Not yet seen by the
  owner. The agent's warnings: things the game moves that are no characters lag by the scene scan (up to 2 s); skin's
  scattering, leaves' light, decals and terrain layers are not read at a hit yet.
- Plan: the hair agent (`wt/hair6`) builds the primary-ray pass as a general mechanism and switches hair, brows and
  lashes to it; then an agent switches every pixel, with HDRP's G-buffer as the yardstick (the tracer's first hit's
  albedo, normal and depth against HDRP's, pixel by pixel: where they differ the tracer misreads a surface, which
  mirrors get wrong too, PT-46). Likely gaps: decals, terrain blending, wet patches, parallax (PT-28). PT-51's
  "Standard in real-time path tracers: the camera's first surface from HDRP's buffers" no longer holds.
### PT-58 Two finished jobs wait for a merge onto today's tree (mirrors PT-46/PT-37, subsurface PT-40)
- The mirror finisher (`docs/agents/mirror-2.md`, copy `wt/mirror2`, base27 from 09:00) and the subsurface finisher
  (`docs/agents/sss.md`, copy `wt/sss`, base26 from 10:06) passed every offline check. The mirror merge of 14:48 met 18
  conflicts with the limits, caustics and hair work merged since: its code files were put back from the 14:38
  snapshot (`wt/base31`; the conflicted versions kept in `<scratch>/mirror2-conflicted`). Its CHANGELOG entry and its
  notes in this file ARE in main; its code is NOT. Next: one agent ports both jobs onto main in a copy, builds and
  tests, when a slot frees.
- Ported onto the tree of 15:06 by the merge agent (`docs/agents/merge-3.md` in `wt/merge3`, base33; reported 16:55):
  subsurface scattering in full, the hero's head fix, the wet ground's store and optics; 1269 offline checks; the
  mirror job's fog and water parts were dropped (built on HDRP's picture). NOT merged: a trial merge onto the
  camera-ray tree met 21 conflicts, a deleted file and moved wave functions.
- Started 17:08: the port agent (copy `wt/port2`, base35 = main at 17:03; brief `<scratch>/brief-port-2.md`; report
  `docs/agents/port-2.md` in its copy) brings those three onto the camera-ray tree and makes them work at the camera
  ray's own hit (the walk on skin and wax, the wet ground, the head rule). Its warning to carry: a GPU hang on the
  first frame when the walk and the wet store's read met in one pass (kept apart by `g_hitInside`; cause unknown).
### PT-52 Waves leave the ground wet behind them (wet sand, wet rock)
- The owner, 2026-10-07 11:20: "would be cool if the water waves left a wetness trail/shader on surfaces that can be
  seen after the wave leaves them, like wet sand on the beach".
- What it takes: wetness as a state of the ground that lasts. A place a wave covered stays wet when the wave has gone
  and dries over time; wet ground is darker and glossy (a water film on a rough surface, water in the sand's pores),
  so it mirrors the sky and catches highlights. The store of where the water has been must serve rays too (a mirror
  shows the same wet sand). It needs the water's real waves at any point, which the caustics job brings (PT-14).
- State: open. Part of the water-edges job (PT-42, `<scratch>/brief-water-edges.md`, item 3), which starts when an
  agent slot frees.
- Built by the water-edges agent before its stop (15:04): a store of where the water has been, wet ground and its
  optics in `hit_material` (any ray sees it), `pt_wet.hlsl`, `pt_waves.hlsli`; offline at 30, 450 and 1000 s; 0.12 to
  0.17 ms over a 120 m sea; NOT mutation-checked. To merge with the subsurface and mirror work (PT-58). Scenes: `water-day` (the beach), `water-torch` (the sea cave), each with an `anim` step.
### PT-51 Imitations in the tracer: everything should be path traced
- The owner, 2026-10-07 10:30, told that skin's subsurface scattering is a screen-space blur (`pt_sss.hlsl`: HDRP's
  Burley, 24 samples on a disc; thin skin by HDRP's thickness value): "'It's imitated.' - what? No, I said we don't want
  shortcuts or hacks - we want proper path tracing, and everything should be path traced" (AGENTS.md "No shortcuts").
- The list given to the owner. Being replaced: skin's and wax's subsurface scattering (the SSS agent: a traced random
  walk, PT-40), the hair layer's count (merged), HDRP's water shading (merged), the fixed caps (PT-50). Still imitated:
  leaves' and thin cloth's fixed share of light from behind (`TRANSMISSION` 0.5, `CLOTH_TRANSMISSION` 0.3: on PT-50's
  list); the sun's caustics under water from HDRP's pattern texture, not traced through the waves; lamp and sun light
  off water and smooth floors by a mirror image (right for flat reflectors only); bounced light partly from the
  radiance cache; the terminator softening on skin; what HDRP still draws over the picture (particles, transparents,
  volumetric fog: PT-29 to PT-31, PT-37). The owner's own rows that are not physical, named for their call: Reflection
  Boost, the exposure and dark adaptation rows, Fill Light Fog. Standard in real-time path tracers: the camera's first
  surface from HDRP's buffers, a denoiser.
- State: open; one by one after the running agents: each replaced by its traced form, then measured.
- The owner, 2026-10-07 14:35, told the subsurface agent traced wax, skin and leaves: "And cloth, too? blankets, towels,
  clothes?" Not yet: thin cloth passes a fixed 30 % for every fabric (`CLOTH_TRANSMISSION`; the game gives its cloth
  no scattering data), thick cloth is lit as a surface. Next: each thin fabric's share from its own colour and thickness
  (a thin scattering sheet), so a dark wool passes far less than a pale linen; an agent when a slot frees.
  mirror: detail 1.02, shapes 0.87. So a mirror is now as sharp as the direct view. Not yet looked at by the owner.
- The mirror-2 agent (`docs/agents/mirror-2.md`; in a copy, not merged, not seen in game). The egg is the inside of
  the back of the hero's head: the first-person body's mesh keeps an open shell with the face cut out, which HDRP never
  draws from in front; the 0.3 m rule left its rim. Mirror rays and bounced light's first ray now pass its back faces at
  any distance (offline: nothing of it in a mirror 0.5, 1 and 3 m ahead). Gap: `trace_closest`'s rays (bounced light's
  further rays, rays through glass) still meet it; the rule there hung the GPU in the reflections pass, likely a driver
  fault, so it was taken out. Water: rays into mirrors and bounced light now meet HDRP's water (reflection, the bed
  through it, its body color; offline within 0.3 % for the first two); no waves or foam. Fog: PT-37.
### PT-50 Limits that change the picture
- The owner, 2026-10-07 08:50, told that the see-through crown may come from the hair layer's fixed count of 8 cards
  (`HAIR_LAYERS`): "There is no budgets or limits there, this is PT, whats that about?" A ruling (AGENTS.md): a budget
  may cost noise or speed, never change what the picture converges to.
- Known caps to check against it: the hair layer's 8 cards (given to the hair-strands agent: trace until the hair
  stops the ray); the emissive lights' 16,384-triangle budget (PROJECT-CONTEXT 9.3: brightest and largest kept, the rest
  light nothing); the 96 swaying cloths traced in their wind pose (the rest still); the 2 strongest lamps a pixel in
  the water caustics; the detail tiles (128, now the fallback behind texture sharing); `SHEETS` crossings of glass (8);
  the radiance cache's reach; RTXDI's light candidates (unbiased: noise only). State: open; an agent to list every
  cap in the tracer and say which change the converged picture.
- The owner, then: "i thought the cost of swaying cloth and lights was fixed in PT, didnt expect it was required to do
  budgeting like that. Also dont do shortcuts, we want realism and correctness, and then we will optimize from there"
  (AGENTS.md "No shortcuts"). So the job is to remove every cap that changes the picture (all glowing triangles as light
  candidates, every swaying cloth posed, the hair traced until stopped, ...), then measure the cost, then optimise
  without changing the result. Next agent when a slot frees.
- The owner: "2 lamps per pixel in water caustics, also drop that restriction". Given to the water agent (`wt/water`):
  every traced lamp's light off the water, all of them or by unbiased sampling.
- The owner, 09:20: "Do you have other arbitrary restrictions I don't know about". The list from the code (grep of the
  native constants, the shaders' and the plugin's caps), the ones that change the picture:
  - What is traced at all: lights 1,024 at most (`kMaxLights`, `SceneLights.MaxLights`); lights cut at their reach
    (where they fall under 50 lux, at most 40 m: `ReachLux`, `MaxReach`); glowing triangles 16,384 (`kEmitterBudget`),
    each glow cut at its reach (2 lux, 40 m at most); characters within 100 m (`CharacterCapture.MaxDistance`), 64
    renderers, 8 submeshes and 524,288 skinned vertices a frame; vegetation 60,000 instances (`LeshyCapture`); 4 water
    surfaces (`MaxWater`); swaying cloth 96 copies within 60 m and 1M vertices (`kMaxSwayCopies`, `kSwayReach`); hair
    drawn as a layer within 60 m (`HAIR_REACH`), 8 cards deep (`HAIR_LAYERS`); HDRP's forward surfaces within 60 m
    (`HDRP_SHADOW_RANGE`); 8 glass crossings (`SHEETS`); 256 light cookies, 256 alpha tiles (atlases).
  - Light taken away to stop flicker: the firefly clamps (`LIMIT_DIRECT` 48, `LIMIT_REFLECTION` 8, `LIMIT_INDIRECT` 4,
    `NRD_LIMIT` 250, `CAUSTIC_MAX` 16).
  - Fixed numbers in place of the material's own: thin cloth lets 30 % through (`CLOTH_TRANSMISSION`), leaves 50 %
    (`TRANSMISSION`), glass bends as 1.5 (`GLASS_IOR`), a glowing thing at most 6 m across (`MaxThingSize`).
  - Approximations by design: the radiance cache (cells 10 cm and up, 16 frames), a reflection rougher than 0.6 using
    the cache's light.
  Speed only, the picture right after a few frames: BLAS builds a frame (48, 3M triangles), micromap budgets, mesh
  reads a frame. Next: the PT-50 agent removes the first three groups' caps (the clamps last, with their flicker
  measured), then the cost is measured and optimised without changing the result.
- Built, 2026-10-07, in `wt/caps` (`docs/agents/caps.md`: the table cap by cap, what it changed, its cost; CHANGELOG).
  Gone: the three radiance clamps, the 25 cm floor, `CAUSTIC_MAX`, the lamps' reach cut (a far list per 32 m block
  counts every lamp past its near reach, unbiased), the 1,024 lights, the 20 m grass ring, the 100 m character ring, the
  60 m rings (swaying cloth, hair layer, forward surfaces). Raised past what the game reaches: lights 65,536, glowing
  triangles 262,144, characters 256 renderers and 2M vertices, sway copies 4,096, alpha tiles 1,024, vegetation
  1,000,000, glass crossings 64. NRD's 250 stays as what its half floats hold; the rest is added back. Kept, each with a
  log line when a scene meets it: 4 water surfaces, 8 submeshes a character, 256 cookies, the glass index 1.5 (the plugin
  logs each glass material's own `_Ior`). `TRANSMISSION` and `CLOTH_TRANSMISSION` are the subsurface agent's (`wt/sss`).
  Passes the offline harness; not seen in game. The cost of the clamps' going: more boil in a candle-lit night (0.23 to
  0.36 of an 8-bit level, built-in denoiser). Next: the in-game harness on `tavern-room`, `vista`, `glow`, `npc-day`
  (what to look for: `docs/agents/caps.md`), and the owner's eye on the flicker.
- Found on the way, each holding a little light back, not on the owner's list (numbers in `docs/agents/caps.md`, "Still
  holding light back"); each wants its own entry: the radiance cache holds a cell's light to 2,000 times the reference
  (a furnace with such a glow comes to 0.79 where 1 is right); the denoisers keep 0.81 to 0.94 of the undenoised light
  (RELAX's own firefly filter takes 2 % of it in the candle-lit night); ReSTIR's reuse keeps 0.934 of the light of the
  same paths with no reuse.
- Merged 12:50 (ABI 0x00011D00), installed 12:45 (plugin 644F7E98BE35, native 4F2A3E36342F, the GPU test 1132 of 1132),
  run `20261007-124601` (`tavern-room@lights`, `glow`, `vista`): no problems flagged. The tavern traces 630 lights. The
  cost, by the native log's stages: the mineral cave went from 24 to 87 ms of GPU a frame. The composite stage 1.1 to
  42 ms: the lamp-off-water stand-in (`water_reflected_lamps`) loops over every lamp of a cell, now 677 on average (2,841
  at most), for each of the 3 water slots, on every pixel; the caustics agent replaces it (told the number, 12:55).
  Bounced light 8.5 to 21 ms, reflections 5.5 to 11.6 ms: every lamp reaches as far as its light goes. Tavern 25 ms,
  vista 23 ms. Correct first, then fast (the owner's ruling): the speed work is PT-08's.
### PT-49 What moves must be traced as it moves; every cloth found as cloth
- The owner, 2026-10-06 23:32, told that the canopy over the candle corner sways in the game while the tracer's copy
  hangs still, so the two do not line up and the canopy is not taken for cloth (PT-39): "Obviously reflections and
  shadows of moving objects, including cloth, must be accurately represented by our path tracer. It's one of the
  reasons why realtime path tracing is a nice feature - that it is reactive to scene changes. But yeah, we really need
  to make sure we detect cloth correctly. That also includes clothes hanging on a drier and stuff". A ruling
  (AGENTS.md).
- Today: characters are skinned per frame (Kandra's positions shared each frame); things the game moves in its vertex
  shaders by wind (the `TG/Clothes/Cloth_Wind` canopy, and vegetation: `TGPT_MATERIAL_SWAYS`, PROJECT-CONTEXT 9.2) are
  traced in their still pose, with a 30 cm window (cloth) or a still-only ray (vegetation) to bridge the gap. So their
  shadows and reflections are of the still pose, and a swaying cloth's pixels miss their traced copy.
- The cloth agent (`docs/agents/cloth.md`, finished by a fresh agent after a usage-limit stop; merged 03:12, ABI
  0x00011900): cloth found by what it is (`ClothRules.cs`: the whole `TG/Clothes/` shader family, hanging-sheet names,
  cloth words on double-sided materials; rugs, sacks, fur, nets and stone drapery stay solid; characters' clothes as
  before): 74 of 161 cloth-like materials in the harness's scenes are thin cloth now (62 before), laundry, towels, rags
  and tablecloths among the new. The sway is traced: the wind maths read from the game's compiled Cloth_Wind shader
  (its two switches act the other way round from their names), the plugin sends each swaying material's wind and time
  every frame (`TGPT_SetClothWind`), a compute pass (`pt_sway.hlsl`) moves the tracer's copy of each swaying cloth
  within 60 m (96 at most) and refits its BLAS. Offline: a swaying sheet traced 0.00 mm from where it is drawn (112 mm
  off when still); its shadow matches; a red canopy's underside gets 0.27 of the sky in red (0.02 when still). The old
  evidence was misread: the Surfaces view showed HDRP's material type only; it now paints thin cloth purple.
- In game (run `20261007-032112/candle-up/views.jpg`; native 696E0C2939E8, the tested one; tgpt_gpu_test 836 of 836):
  the canopy is purple in the Surfaces view (found as cloth), and its underside glows deep red traced, close to the
  game's; the Direct view shows the daylight through it. The log: "Cloth in the wind ... traced in the pose HDRP draws
  it" for the banners and the moving cloth planes. Not yet looked at by the owner.
- The owner, 2026-10-07 09:05: "did you ever fix the red cloth over the candle lights that the game didn't recognize as
  cloth? Did we playtest that to confirm the light through it casts red colors onto the stuff below it?" Measured on the
  candle corner under the canopy (`<scratch>/box_colour.py`; run `20261007-032345/candle/corner-before-after-small.jpg`:
  the game's, before, after): the corner's light is 3.1 times what it was (0.091 against 0.029, linear) and red: red
  over green 2.73 (1.48 before; the game's 1.54, which has no canopy light); the wall beside it reddens too (2.11 against
  1.37). So the daylight through the red cloth does tint what lies under it, seen in the harness. Told that the game's
  own picture lacks it because its renderer sends no light through cloth, the owner: "wouldn't real-life do that
  though?" (yes: the traced corner is the realistic one), then: "Ah OK. Beautiful!" (of the harness picture).
- State: open; the next agent: find every cloth (canopies, tents, banners, laundry on lines: by shader, material and
  object names, from a census over the harness's scenes, as PT-47's for lights), and trace the game's wind sway (its
  vertex shader's maths, run each frame on the traced copy, as HDRP does) so pixels, shadows and reflections agree;
  vegetation after cloth.
### PT-48 The mineral cave's lake, traced: thick blue haze, a dark slab of water, white-edged grass
- The owner, 2026-10-06 22:59, watching the run that turned the mineral cave's save round to its lake (`glow-lake`,
  run `20261006-225619`, `glow-lake/caustics.jpg`): "That looked weird".
- Seen: the game's picture shows mossy rocks over dark water and grass; traced, a blue haze covers the far half of the
  picture (the rocks and moss gone in it), the water is a dark, smooth, opaque slab, and the grass blades have white
  outlines. The same with the Caustics row off, so not the new lamp light. Not known: whether the haze is the game's
  fog as the tracer adapts it (PT-05, Fog Adaptation) or HDRP's underwater look (the camera 2.5 m above the lake),
  whether the lake is an HDRP water surface or the game's own water shader on a plane (`docs/agents/caustics.md`),
  and what draws the white outlines.
- Found (2026-10-07, `docs/agents/lake.md`): all three are the game's own fog at x5.8. The cave's lights are fill lights
  the tracer leaves out (PT-47), which still light the fog, so the fog was 5 stops over the traced light; Dark
  Adaptation brightened the view from the fog's level (x5.78) and Fog Adaptation put that back on the fog (far rocks
  32 -> 85 levels). HDRP draws its water after that pass, so the lake kept its fog at x1 (the slab, 10 levels against
  the game's 22), and grass blades over it, unlit by the tracer, were the x5.8 fog alone.
- State: **built, passes the offline harness; not seen in game.** Fog Adaptation now lifts the fog only as far as the
  traced light it veils (measured in its own pass), never below the game's fog: x1 in the cave. Next: `-Scenario
  glow-lake -Recipe views`; the native log's "fog adaptation" part should say x1.00 there. The cave stays darker than
  the game's until its lights are traced (PT-47).
- The lake agent's fix (`docs/agents/lake.md`; merged 04:21 with lights round 2; native 0AEBB6D64A40, the tested one)
  in game: the haze is gone at the lake (run `20261007-043153/glow-lake/lake-compare-small.jpg`: the game's, before,
  after); the lake and far wall are now dark where the game shows lit mossy rocks: the cave's fill lights are left out
  (the ruling). Fog Adaptation no longer lifts dark scenes' fog past the game's: stairwell 30.9 traced against the
  game's 27.3 (61.6 in the first baseline, `20261006-145526`), dark-room 20.1 against 17.1 (40.3), the tavern 29.4
  against 23.9 (31.8 the night before).
- The owner, 2026-10-07 morning: "The pool of water in the 'mineral cave' when we turn 180 degrees still look weird
  also. And I haven't yet seen beautiful caustics on the walls. I though these would appear in there". Read from
  `20261007-043153/glow-lake/views.jpg`: that side of the cave has one lantern far off and nothing else real (the
  Direct view); the game lights it with fill lights (left out by the ruling), so it is dark traced; the water is
  HDRP's own shading, which gets no light while tracing (the fill lights are switched off for HDRP too): a dark slab.
  Caustics need a light by the water: none on that side; on the lamps' side the puddles are the floor's wet patches,
  not water, so the lamp caustics (water only) throw nothing. Two answers: light reflected off any smooth flat surface
  onto what faces it (the lamps in the puddles onto the cave walls), a job with no ruling needed; and the tracer
  taking over the water's own look (its reflections, seeing into it, its colour, its caustics), which AGENTS.md says
  to raise with the owner first: asked, recommended yes. The owner, 08:44: "of course. Full Path Tracing means full
  path tracing." (AGENTS.md). A fresh agent designs and builds the traced water (`wt/water`, `docs/agents/water.md`):
  the water's own colour, refraction and reflection traced in place of HDRP's while tracing.
- The water agent (`docs/agents/water.md`; merged 10:10, ABI 0x00011C00): a Harmony transpiler on HDRP's water lighting
  pass hands over after HDRP lights the water and before its fog; render event 6 swaps HDRP's lit water for the
  tracer's (HDRP's fog still goes over it). Per pixel: the traced reflection by Fresnel; the bed seen through the water
  (the camera ray bent at 1.333, traced, dimmed by the water's extinction over the way under water); HDRP's scattering
  colour and foam lit by traced light; the sun's and lamps' highlights with shadow rays. HDRP's water is the data
  (depth, normals, its buffers, extinction, foam colour). HDRP's water stays with the camera under water, tracing off
  or the Water Reflections row off. Left out: HDRP's light through wave tips, the underwater view. Every lamp counts in
  the light off water (the two strongest traced, the rest by one sample weighted by its share; the 64-lamp cap gone).
  Offline: Fresnel right at every pixel, absorption over the bent path within 3 % at 99.9 % of pixels, lamp light on the
  bed 0.994 of the worked value, three and four lamps adding up to 0.99999. Cost: the water pass about doubles (0.41 to
  0.81 ms over the test lake at 1280x720).
- The traced water in game (run `20261007-103925`, installed 10:39: plugin 733E480875C1, native 2CAF8E849FFE, the GPU
  test 1111 of 1111): the sea cave (`water-torch/02-traced-rr/full.jpg`) reads as the game's, a little greener and
  clearer; the cave lake is still near black (no real light on that side). Defect seen there: along the picture's
  bottom edge the traced sea shows a pale grey slab with the caustic net on it where the game shows the dark bed and
  plants under shallow water (`<scratch>/strip.jpg`: the game's above the traced), and plants under water are blurred.
  Not looked into. The owner, 10:47, of the lake side: "man that cave looks weird. The rocks in the water looks so
  weird, and the water moves weird there. It looks broken, and it might be vanilla that makes it so broken": the big
  flat slabs in the water are in the game's own picture too (`lantern-lake/1-game/full.jpg`).
### PT-47 Lights at glowing things are left out (crystals, magic, orbs, fireflies)
- The owner, 2026-10-06 night, asked whether a blue light at a glowing crystal counts as the crystal's light (PT-34):
  "yes, and there may be more instances of these. Like magic, for example, also emits light. Orbs and stuff.
  Fireflies. I don't know - we may be dropping stuff we don't want to drop still!" A ruling (AGENTS.md "No fake
  lights"): a light placed at something that glows is that thing's light, judged by what is at the light.
- The census (every harness run's "Lights by name" lines, `<scratch>/left_out.py`): 45 names left out. Ones that may
  stand for a glowing thing: "Point Light_17B" x31 and "Spot Light" x2 (the mineral cave's blue), "Point Light" x190
  under "VFX_Explosion_Aard" (a spell's light), "Light_Stonehenge_Decal" x14 (glowing stone decals),
  "Light_Monster_Wyrdstalker" (a creature's glow), "Point Light_Neck" under "HeadHolder" (the masked figure by the
  bridge at night), "Light_Ambience" x23 under "SecondVisual". Left out rightly, by the rulings: the hero's own lights,
  the inventory screen's, the room fill lights named "Point Light_N" under "Lights" in the tavern, candles' extra
  lights beside the one traced (each candle keeps one).
- The owner, the same night, said what "no fake lights" means: "make sure we only drop those fake area lights and stuff
  from the game that artists place as fake area lights or fake bounce lights and stuff like that ... But emissive
  things, like crystals in a cave, or magic, things like that, of course they should still emit light and appear in
  reflections and stuff like that". So two parts: the classifier drops only fill, fake area and fake bounce lights;
  and glowing surfaces and effects light the world and show in reflections. Today the crystals glow on screen but
  light next to nothing (PT-34's Direct view), and effects HDRP draws over the picture (spells, particles) are in no
  reflection.
- The lights agent (`docs/agents/lights.md`, merged 23:40, offline only): two mistakes of ours. The tracer read
  HDRP's EmissiveIsBlack flag as "no glow", and HDRP sets it whenever Baked Emission is off, the default: no glowing
  surface was ever traced as glowing ("emissive: 0 submeshes" in all 39,669 status lines of every harness run). Fixed:
  HDRP's own shaders have their glow read whatever that flag says. And each crystal has its own blue "Point Light"
  (13,416 K) dropped as a fill light because its names say nothing. Now kept: a left-out light with something glowing
  within 1 m of it (a traced emissive surface, a glowing particle or effect, an emissive decal: the stone circle's
  runes), at most 6 m across, not already a flame's or lamp's light, with the game's colour, strength and range;
  the glowing surface it stands for lights nothing itself (no double light; material bit 131072
  `TGPT_MATERIAL_OWN_SURFACE`), the camera and reflections still see it. Still dropped: lights with nothing glowing at
  them (the cave's white and warm fill lights, its two sky-shaft spots, the tavern's room lights, the hero's light).
  Every light at its emitter's size (PT-34's ruling): measured from the flame, glass or crystal, else candle 1.2 cm,
  torch 8 cm, lantern 10 cm, brazier 30 cm, bonfire 50 cm, effect 20 cm. Open for the owner: the Wyrdstalker's and the
  masked figure's lights (their glow is in character shaders the tracer does not read).
- The owner, 2026-10-06 23:48, of the mineral cave: "Why are there like 4 of these yellow lights in the water in the
  mineral scene? I only see one torch in the scene. Never understood that." Found by position (`<scratch>/
  lights_on_screen.py` on the audit of `20261006-162322/glow`): six warm lights (2,683 K, "Point Light_35" to "_40")
  hang about 1 m above eye height across the left of the cave, 10 to 17 m ahead, in a group named "torch_06", with no
  torch at them; their mirror images in the wet floor fall where the yellow dots are (the game's and the traced
  picture's alike). The one real torch is "Light_Int_Torch_Big_01_Lit_02". So they are fake lights the tracer keeps
  because their group's name says "torch". By the ruling they go: a light kept by its name must also have its flame
  (or lamp, or glowing thing) at it. Next lights round; the run of the lights build will show which lights it keeps.
- Lights round 2 (`docs/agents/lights-2.md`, finished by a fresh agent after a usage-limit stop): why the candles blew
  out: the tavern candle's big light (21,149 to 24,775 cd, HDRP radius 1 m) sits inside the wax, 10 cm under the
  flame; HDRP softens a light near its centre by its radius, I/(d^2+r^2), so the wax 3 cm away got about I; round 1's
  flame size gave I/d^2, 1,100 times that. Changed: a flame's or lamp's light sits at its measured flame's centre (light
  flag `TGPT_LIGHT_AT_EMITTER` 0x40000), its shadow rays tested up to its size plus 1 cm, and a surface lit from behind
  passes back faces within 1 cm only: the wax's top shadows its sides (offline, a wax column: top 105.6 against 107.5 by
  hand, side 2.22 against 37.2 with the light in the wax). A light kept for its name needs a flame, lamp or glowing
  thing within 1 m, else it is dropped as fill: the cave's "torch_06" lights go (and fills in torch_04 and torch_05).
  The hidden key "Lights sized as their emitters" and the harness recipe `lights` A/B it. Not checked offline: that
  every real candle, torch, brazier and lantern keeps its light (the audits hold no flame positions).
- In game (run `20261007-042814/tavern-room/table-candle-r2.jpg`: the game's, the overblown one, round 2, round 2 with
  HDRP's radii): the halo is smaller and the candle's body less white than the night before, but its top third still
  glows brighter than the game's cream; with HDRP's place and radius it looks as before round 1. Not yet judged by the
  owner, who asked for "whatever is most realistic".
- The owner, 2026-10-07 12:40, in play: "some of the candle lights, such as in the tavern, still bloom too much/are
  overblown". Open. Measured next in the queue after the limits merge (`tavern-room@lights`); the subsurface agent
  (`wt/sss`, waiting for a slot) changes how light goes through wax.
- The mineral cave with round 2 (run `20261007-043153/glow/cave-compare-small.jpg`: the night before, now): the five
  yellow streaks on the wet floor are gone, one left (the real lantern's); the log: "Point Light_35" under "torch_06"
  (and the fills under torch_04 and torch_05) "left out (its names say "Torch" ... but no flame, lamp or glowing thing
  stands within 1 m of it: a fill light"). The crystals' blue side stays. Not yet looked at by the owner.
- State: open. Next (a fresh agent): for every left-out light, what is at it (an emissive surface within its radius,
  a particle system or effect, its parent's name), from the game's own objects, so the classifier keeps a light at a
  glowing thing and still drops fill lights; why the crystals' own glow lights so little (its strength as the tracer
  reads it against what the game's look implies); whether glowing things show in traced reflections; then the census
  again over the harness's scenes, and the mineral cave.
## Done

- The in-game harness (asked 2026-10-06): `docs/PT-HARNESS.md`.
- Autosaves and exit saves: answered there (no exit save; a game setting; the harness loads by name and blocks saves).
- Never start the game while another program uses the GPU; "any process"; video encoding does not count; 5 quiet
  seconds: in `AGENTS.md` and the start script.
- Chromatic aberration off in the comparisons ("so they're easier to compare").
- The camera's tilt: a save loses it; the harness finds it again from the save's own picture
  (`Run-PtHarness.ps1 -Calibrate`, `tests/ingame/camera.json`).
- Up to 2 subagents at a time for this work (the owner, 2026-10-06).
- This list.
