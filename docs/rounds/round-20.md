# Round 20 - TG Path Tracing: HDRP draws nothing opaque under the traced picture, from 2026-10-11 01:20

Started from round 19's hand-off ("▶ Starter prompt (R20)" at the end of `docs/rounds/round-19.md`), straight after it,
with the owner at the PC ("Get going on the next round"). The order (PT-112): (1) make step A the rule, (2) step B, the
game's fog traced, (3) step C, the transparent effects traced. Nothing committed since d31dff9; the owner commits.

The state at the start (hash-checked 01:22): plugin 330557A98A6E and native C6515C716BD5 in the game folder, native the
same in `artifacts\Native`. No game, no worker, no dxc running; only the Codex app's daemon (Codex is out until 10-16).

## 1. The plants' and cloths' own motion (step A's first item)

What the tracer writes into HDRP's motion vectors under step A is its camera rays' own motion (`pt_primary` hit_motion):
a character's by its previous skinned positions, a moved thing's by its previous transform, everything else as still.
Plants in the wind are posed every frame by the sway pass (`pt_sway` step 5, one merged mesh a 16 m cell), so their sway
was missing from that motion. Worse, a FAULT: a plant group's mesh record was made with eight values for nine fields
(`scene.cpp`, `MeshRecordGpu{...}`), so its `previous` was 0, not "none", and hit_motion read heap slot 0 as the plants'
positions a frame ago: whatever that slot holds. The foliage differences of round 19's town run (`20261011-011046`, the
world frozen, so the plants were still) fit that: with a still world and a still camera the motion should be zero there.

Built (native, plugin not yet):
- A group's buffer has one more part: each vertex's move since the frame before, three halves in 8 bytes (where it was
  less where it is). Step 5 writes it from the pose already in the buffer (flags bit 1: the group was posed or kept last
  frame), else 0; a new step 6 writes 0 where the pose is kept (the game paused, a frozen world). The record's
  `previous` is that part's view with the bit `kMeshPreviousDelta` (0x40000000), and hit_motion adds the interpolated
  move to the hit point. 8 bytes and not the full 12-byte positions: 9.5 million plant vertices in the town (run
  `20261011-011046`), so 76 MB more video memory there instead of 114.
- A swaying cloth's copy keeps its previous pose (12 bytes a vertex, a few tens of thousands of vertices): sway modes 3
  (before the pose, the pose of the frame before) and 4 (after it, where there was none before, or the pose was kept);
  its record's `previous` is that pose, read as a character's is.
- Only `pt_primary.hlsli` (the camera rays' pass alone includes it) and `pt_sway.hlsl` changed among the shaders, so a
  player's driver remakes two passes at the first start. (CMake makes every pass depend on every shared file, so the
  build saw all 43 as stale: the 40 others were marked current by hand, their sources unchanged; `pt_common.hlsli`, edited
  and put back, got its old time back.)

## 2. Step A the rule

- `GamePasses.cs` rewritten: while the traced light is on screen HDRP's opaque draw lists are empty and its decals are
  left out of the gameplay camera's frame settings; the frame carries `TGPT_FRAME_OWN_DEPTH`. HDRP draws as the game does
  only when its G-buffer is looked at: the Yardstick view, the old route (Own camera rays off), and the hidden
  `Advanced / HDRP draws under the traced picture` (off; for tests). The trial switch `Game passes while tracing` and its
  bits (1 to 128, 512) are gone; its recipe `game-passes` too. Recipe `no-hdrp`: HDRP drawing against the rule, twice.
- The traced decals follow the game's own decal settings (recorded before the cut), not HDRP's of the frame: in round 19
  cutting HDRP's decals cut the traced ones too.
- Hair: HDRP no longer draws a head of hair's or a beard's depth while tracing (`HoldLayerDepth`, PT-57, gone): it is
  hidden from HDRP as brows are; the tracer's depth and motion at a hair pixel are its camera ray's.
- The 10-second yardstick line is not written while HDRP draws nothing (an empty G-buffer). The per-pixel yardstick word
  stays: the scattering walk reads which pixels walk from it.
- Source checks: 115 pass (a new one for this rule).

## 3. In the game: the tavern matches, the town's soldier flickered, the cause, the fix

Run `20261011-014541` (`tavern-room@no-hdrp`, `metal@no-hdrp`; frozen world, no denoiser; Sunshine up to 4.9 %: no speed
numbers). TAVERN: our depth the same as HDRP drawing (flicker 0.41 and 0.40, boil x9.4 both). TOWN: much better than
round 19 (flicker 0.40 -> 0.31, boil x4.6 -> x3.0 of the game's) but not at HDRP drawing's 0.22. The single frames differed
little (1.83 levels against 1.71 between two HDRP-drawing steps); the extra sat on the soldier in front of the camera.
`tools/harness/flicker.py` on a run that kept its frames (`20261011-015412`): every top spot on the soldier and his spear
(the mail's scale edges, the stitches, the helmet's rim), none with HDRP drawing. Measured per crop (`<scratch>\shifts.py`):
no shift between frames at all, but 0.6 levels of change a frame on him against 0.11 with HDRP drawing (the floor beside
him 0.17 in both): DLSS kept no history on him.

Two causes tried:
1. Camera rays stop at a cut-out texel by chance, frame by frame, so the depth at a fringe flips. A finding switch (Speed
   trial 32768: cut at the cutoff) changed nothing (run `20261011-020452`: 0.30 and 0.30). Not it; the switch came out.
2. A finding switch that painted the motion and depth handed to DLSS (Speed trial 16384, run `20261011-021316`): red over
   the whole soldier and his spear (the floor and walls none), in a moire, the same with HDRP drawing (where DLSS used
   HDRP's motion, so it never showed). The game's own frames: the soldier still (no shift, 0.08 levels). So the tracer's
   motion for a still character was not zero. THE CAUSE: Kandra's skinned positions are the world's (4 km from the origin
   here, where a float resolves half a millimetre); hit_motion placed the previous point through the instance's transform
   and compared it with the ray's own hit: two roundings, up to half a millimetre apart, a few hundredths of a pixel at
   3 m, in a moire. The fix (`pt_primary.hlsli` hit_motion): each corner's move, previous less current position, a
   difference of two nearby floats (exact, and nothing for what stands still), added to the hit; a moved thing's the same
   way (its previous transform less its current one on the same point). The same motion feeds the tracer's own reuse
   across frames and the denoisers, so it was a little off on characters before today too.

Run `20261011-022100` (the fix): town flicker 0.22 with the rule against 0.22 with HDRP drawing (boil x2.0 against x1.9 to
2.1), the soldier's change 0.11 a frame in both. The detail measure is a little higher with the rule (11.1 against 10.8).
The finding switches came out the same day; installed: plugin 65955D57DF84, native 5D9A826A1BCC (hashes match).

## 4. Step B on trial: the fog traced (pt_fog.hlsl)

The owner, 02:55 going to bed: iterate unattended (motion vectors, then fog and transparents into the tracer, then frame
times, "absolutely certain" about RTXDI, reordering, micromaps, Mega Geometry); 02:57: NO SUBAGENTS until the owner says
so (an audit agent started at 02:56 was stopped at once; it wrote nothing). The fog design: `docs/agents/fog-2.md` (one
research agent, before that ruling). Its questions decided by the standing rulings, for the owner to review: every traced
light lights the mist at real strength with its shadow; the fill lights keep their fog; the far haze's tint: traced fog
uses the Fog volume's albedo alone (no tint), to show the owner side by side.

Trial 1 (installed, run `20261011-025814`, `fog@traced-fog`, Broc Meala's thick mist): a new pass `pt_fog` (sun + sky with
traced shadows, isotropic, the scattered share from fogColor), applied by `pt_compose` (light x transmittance + fog; HDRP's
sky through it by the glass-over-sky path), HDRP's `RenderOpaqueFog` skipped by a prefix, Fog Adaptation off, the traced
gain unfaded; hidden `Advanced / Traced fog (trial)`, frame flag `TGPT_FRAME_TRACED_FOG` (1 << 14 + 1). Picture: the same
grey veil and far shapes as HDRP's and the game's, a little thicker over the near rocks; luma 66.7 against 69.4 (HDRP's
fog) and 70.4 (the game); grainy (flicker 1.23 against 0.09: one sample a pixel, no reuse yet).

Trial 2 WRITTEN, NOT COMPILED: ABI 0x00012700 (`TGPT_Frame.fogMedium[4]`: the Fog volume's albedo, its anisotropy; 624 B;
plugin `FogMedium`, FillFog), `pt_fog.hlsl` with HDRP's Cornette-Shanks phase, the albedo from the pass's root constants,
and one lamp or glowing surface chosen as the direct light chooses (light_lists). Source checks updated for the ABI.

## 6. The fog, trials 2 to 5 (after the usage limit reset, 03:10 to 03:45)

- Trial 2 (ABI 0x00012700: `TGPT_Frame.fogMedium`, the Fog volume's albedo and anisotropy; HDRP's Cornette-Shanks phase;
  one lamp or glowing surface chosen as the direct light chooses): run `20261011-031242`. Day mist 59.8 against HDRP's
  69.3 (its phase scatters less sideways than an even one); at night the torch lights the 9 m mist with a warm glow and its
  shadows (outdoors HDRP lights fog with the moon only), and the moonlit blue haze is gone: HDRP's comes from its sky
  light, which it never dims through the fog (a stand-in for light scattered more than once).
- Trial 3 (the walk: light scattered more than once, Russian roulette, no fixed count; reuse over frames): day mist 1.45x
  HDRP's and grainy. Trial 4 (a gentler roulette, a 5 x 5 filter by transmittance): sparkles smeared into blotches (rare
  walks that climb toward the sun): correct on average, far too noisy at one walk a pixel. The fix is a world-space cache
  of the mist's light (the walk stops into it); not built.
- Trial 5 (installed: plugin 80C9B38B2F8E, native A77B24D179C2): single scattering the trial's default, the walk behind
  `Advanced / Traced fog: light scattered more than once (trial)`; recipe `traced-fog` takes both. Run `20261011-033715`:
  DAY single 61.0 (x0.87 of HDRP's fog), flicker 0.23 (HDRP's 0.11), boil 0.13; walk 108.3, flicker 4.3. NIGHT single
  52.1 (the torch's glow), flicker 1.9 (grain near the torch); walk 54.6. Cost: the fog pass about +0.3 ms by day,
  +2 ms at night (composite stage). Single scattering leaves the higher orders out: a gap, for the owner to see with both.

## 7. Step C, its first step: what the game's transparent effects are (run `20261011-034434`, recipe `effects`)

The effects inventory (built 2026-10-07 by a worker, never run in game until now) on tavern-room, candle, metal, night,
campfire: `artifacts/harness/20261011-034434/effects-report.md` and each scene's `effects.json` (935 effects loaded in the
campfire scene alone). Nearly all are VFX Graph systems (VFXRenderer): candle flames (1,606 component records over the five
scenes), the EmberGen fires (each: bottom glow, debris, heat-shimmer distortion, fire, smoke, sparks), Wyrd-night strips
and runes, the waterfalls' fog and droplets, cobwebs, moths, the fast-travel effect; then weapon trails (TrailRenderer) and
the Wyrd repeller bonfires (MeshRenderer). Their live particle counts come back unknown: VFX Graph keeps its particles in
its own GPU buffers, which no public API exposes. So step C's first real piece is a capture of those buffers (where each
system's particles are, their size, colour, age, flipbook frame), then a physical form per family (design-particles-fog.md:
fire and smoke as volumes, sparks as small emitters, distortion as refraction). Research before any build; not tonight.

## 8. Speed (03:50 on): plants' micromaps the rule, the leaves' shader tests, a measuring run

- The plants' micromaps (PT-110's M1, a trial since 10-10): run `20261011-035325` (the town, the world running, clean):
  the tracer 65.2 -> 64.6 ms (bounced light 21.7 -> 20.8), the scene stage +0.9 ms (the maps' structures), the same
  picture. With the forest's earlier 121 -> 115 frozen: THE RULE (the `Micromaps trial`'s 32 now leaves them out, for
  measuring). Installed: plugin 728BD9FEA32B, native 89B662465C4D.
- Learned on the way: with the world running the scene stage is 5.3 ms against 1.3 frozen: the plants' wind poses and
  refits, 3.8 to 4.8 ms a frame (624 groups, 6.4 M triangles): RTX Mega Geometry's target (PT-109), worth more than
  reckoned before.
- THE LEAVES' SHADER TESTS (PT-110's M2, the picture kept): the bake left every covered piece of a leaf or a blade
  "unknown", so every ray that crossed a leaf ran the shader's alpha test, because a LIGHT's ray passes a leaf by chance
  where it covers. Now: covered leaf pieces are opaque in the micromap (thin cloth's stay unknown: a bounce ray passes its
  weave too); every still instance holding leaves or grass carries the mask bit 0x40 (`kMaskFoliage`; the rest 0x21:
  every ray mask holds both, so no other ray changes), and a light's ray meets those instances in a pass of its own
  (`foliage_blocks`), every triangle forced to the test: the same chance at each crossing as before, one ray more where
  the first finds nothing. The near stretch of a ray from a surface forces its triangles to the test for a light's ray.
  Source checks: one new (`test_path_tracing_foliage_opaque_but_to_light_rays`). A first compile failed (MASK_SWAYS was
  defined below its new use: moved up).
- THE MEASURING RUN (the measuring build of the tree before the leaves' change, native EB2A3D00CCA6, installed for the
  run only): `metal@measure`, `tavern-room@measure`: each step leaves one part out (the pass's ray, the shading, the
  materials, decals, sun, lamps, the lamps' rays, the cache, the water question, the glass and water walk).
  It never ran: the driver took more than 20 minutes to make the measuring build's bounced light pass, twice. Given up.
- The leaves' change in the game (run `20261011-050806`, the town, the world running, clean): the reflections 22.7 ->
  18.2 to 18.8 ms, the tracer 64.6 -> 61.3 to 63.7, the frozen picture the same (51.0). Installed: native E04E54B9ACDD.

## 9. Where a traced frame goes (05:20 on), and two trials

THE FEATURES RUN (`20261011-051727`, recipe `features`, `metal` and `tavern-room`, the world running, no denoiser,
clean, the window in front throughout). Each step switches one feature off; ms of the tracer's stages (10 s lines):

| | town (`metal`) | tavern |
|---|---|---|
| the tracer, everything on | 63 | 31 |
| the scene stage (plants' poses and refits 4.7 of it) | 6.2 | 1.0 |
| camera rays (prepare) | 3.1 | 2.1 |
| bounced light / its reuse | 20.6 / 1.7 | 9.6 / 1.3 |
| direct light | 6.4 | 4.6 |
| reflections | 20.5 | 8.7 |
| caustics | 4.8 | 3.9 |
| reflections on glossy surfaces only: saves | 8 | 4.6 |
| no glowing surfaces as lights: saves | 0 | 0 |
| no radiance cache: saves (all of it in the bounced light: its feeding ray) | 5.6 | 2.9 |
| no spatial reuse: saves | 3.3 | 2.6 |

- The caustics cost the tavern 3.9 ms a frame and make NOTHING there (0 triangles a frame): the interior floats 470 m
  above the sea of the map around it, and every tile of that sea traces its corners' rays and their lights' shadow rays
  (the tavern's lamps, 470 m up, behind its floor). A cut that cannot change the picture is not simple (light reflected
  off the sea may reach anything above it), so it stays a question: PT-114.
- The frame is the tracer plus the game's own work on the card (the card takes turns, round 19): the town 85 ms = 0.3 +
  12.5 (the game before our composite) + 60 (the trace) + 11 (after); round 19's split of the game's part: post-
  processing with DLSS 6 ms, the rest small. PT-107 holds the next step (the game's 12.5 ms before the composite).

THE MICROMAPS' PRIZE (`20261011-053411`, recipe `micromaps-left`, the world running, clean). The tracer's ms:

| | town | forest |
|---|---|---|
| as it ships | 63.9, again 62.8 | 84.2, again 82.7 |
| the plants' cut-outs solid (a measuring step) | 60.0 | 64.3 |
| every cut-out solid (the whole prize) | 51.9 | 63.5 |

The forest's 19 ms are in the bounced light (8.8), the reflections (7.2), the bounced light's reuse (1.4), the camera
rays (0.4); the direct light's pass not at all. At micromap level 4 the plants' pieces are 59 % "unknown" (the alpha
test asked there): a leaf card's triangle covers hundreds of texels a piece, so most pieces straddle an edge. Each
level finer halves that share and quadruples the bytes (level 4: 32 MB in the forest, 64 in the town).

TRIAL 1, the micromaps of leaves and grass finer (PT-110's M7): the hidden `Speed trial` 128 bakes them at level 5,
256 at level 6 (the `Micromaps trial`'s eight bits are all taken); the rest stay at 4; a mesh baked at another level
than the frame asks is baked again (`omm_stale`); a bake's per-frame budget counts a finer triangle as the pieces it
bakes; the scene's budget of micromap bytes 256 -> 1,024 MB for the trial; the log's piece shares are in level 4's
units (shares of a triangle). The same picture by construction (a piece is settled only where the alpha test would
answer the same). Recipe `omm-level`.

RUN A (`20261011-055446`, recipe `omm-level`, `metal` and `forest`, clean; native 740C823FFE87, plugin 496E68E45584;
the new bounced light pass took the driver 186 s at the first start, and it still carries 0 bytes across its
regrouping). The tracer's ms:

| | leaves at 4 | at 5 | at 6 |
|---|---|---|---|
| forest frozen | 80.5 | | 72.7 |
| forest running | 83.7, again 82.5 | 79.2 | 75.5 |
| town running | 65.0, again 63.9 | 63.3 | 62.2 |
| the plants' pieces unknown (town) | 59 % | 46 % | 35 % |
| micromaps, forest / town | 32 / 44 MB | 58 / 75 | 163 / 200 |

The frozen pictures at 4 and 6: the same (forest 64.1 / 64.1 levels, town 51.0 / 51.0, flicker and boil equal). The
forest's running steps darken as its evening comes (62 -> 26 levels), so its frozen pair is the clean one. MADE THE
RULE (06:10): leaves and grass at 6, the rest at 4; `Speed trial` 128 / 256 bake them at 4 / 5 again, for measuring.

TRIAL 2, the bounced light regrouped by material (PT-99, SER): `Speed trial` 64 regroups the bounced light's threads by
the material their first ray met and whether the pixel feeds the cache (the reflections' key, 16 bits); without it,
no hint, as since 10-10. The source checks' "never the material key again" came from runs when the pass carried 2,560
bytes a thread across the call; it carries 0 since rule-2, so the key is measured again. Recipe `gi-key`.

RUN B (`20261011-060805`, clean, the same build as run A). The bounced light's key, the tracer's ms, no key / key:
the tavern 32.0, 32.3 / 30.6, 30.6 (-1.6); the forest 82.9, 81.7 / 78.4, 77.8 (-4.2). The frozen pictures the same
(tavern 21.1 / 21.1 levels, forest 63.9 / 63.8, flicker equal). MADE THE RULE (06:25; the source checks' "never the key
again" is gone with the reason it rested on). The water question (`Speed trial` 1) at the beach and in the town: REFUSED
again in every frame ("the scene's water reaches y 65.545; the plugin's highest points a slot: 34.800, -1e30, ..."): a
river 30 m over the sea, in a slot the plugin gives no top, so the per-slot fix could not take it in. Fixed: a constant
of its own, `waterCeiling` (the highest corner of every slot's traced water), which the shaders' ceiling takes too
(FrameConstants 8064 -> 8080 bytes). The town's four steps that did the same work read 64.1, 65.2, 63.4, 64.7: a single
reading wanders about 1 ms.

## 5. The whole GPU test: 44 FAIL, none new

`tgpt_gpu_test` on today's build and on round 19's (C6515C716BD5): the SAME 44 FAIL lines (denoisers' means in the slot,
grille and candle rooms, mirror detail with NRD and the built-in denoiser, the glass index, the coloured slab). The last
valid whole run (10-08) had 5 known; the rest came between 10-08 and round 19. Logs: `<scratch>\gputest2.log`,
`gputest-base.log`. Entry: PT-113. (A bounced-light material key written at 04:30 was dropped then on the strength of
`docs/agents/ser-study-run.md`; measured again at 06:10 with the pass carrying 0 bytes, it won: section 9.)

## 10. All of it in one build (06:45 on), the herbs, the water question made the rule

Built at 06:44 (every pass anew: the shared header changed; 45 headers in 1,100 s, three at a time under the compile
script's memory guard with the other session's tools on the PC): leaves at level 6, the bounced light's key, the
`waterCeiling` constant, and the decals' tree split by place (PT-99: round 19 had put all traced decals at ~4 ms of a
room; `scene.cpp` upload_decals and `pt_common` apply_decals: the tree was split by draw order, so its boxes spanned the
scene and a hit walked most of its ~130 nodes; now split at the median of the boxes' centres, each node holding the
least and the most draw-order index under it, a hit taking its decals in draw order one at a time: the same decals in
the same order). Source checks: 116, with new lines for the leaves' level, the key, the ceiling and the tree.
Installed: plugin EEFB85797823, native 4EDCF1F45C53. The driver made the passes in 211 s at the first start.

RUN C (`20261011-064558`, recipe `confirm-r20`, clean), the tracer's ms, the world running, the water question asked
everywhere / only under the water's top:

| | before tonight's speed work (run `-051727`, `-053411`) | run C | with the skip |
|---|---|---|---|
| tavern | 31 to 32 | 30.2, 30.2 | 29.9, 29.8 |
| town (`metal`) | 63 to 65 | 57.7, 56.7 | 57.5, 56.6 |
| forest | 82.5 to 84 | 74.3, 74.4 | 73.3, 74.7 |

The town's stages: bounced light 15.1 to 16.4 (20.6 to 21.5 before), reflections 17.0 to 20.8 (20.4 to 21.2), the
scene stage 5.1 (6.2), direct light 7.1 (6.4), its reuse 2.1 (1.7), caustics 4.8. Frozen brightness as in the runs
before (tavern 21.0, town 50.8, forest 63.9). Difference pictures (`<scratch>\picdiff.py`: the mean frames' luma
in 16-pixel blocks) against runs A and B: the tavern noise only (0.5 levels on average; the fire and candles); the town
and forest move between runs (a soldier, a lamp, the wind's pose), and no decal-shaped difference anywhere.

THE HERBS. Run C's town showed the market stall's potted herbs BROWN; the game, and runs A and B, green. The same
build again (`20261011-070315`, recipe `herbs`, the town loaded first): green at level 6, at 4 and at 6 again. So the
build does not make them brown; run C (the town loaded after the tavern) caught a passing state: brown is what such a
leaf looks like while the tracer still reads its small atlas copy before the game's own texture arrives (PT-98, things
that arrive late). Run D (below) loads the town after the beach to see whether it comes back.

THE WATER QUESTION made the rule (07:10, C++ only): the tracer sets the `Speed trial`'s bit 1 in every frame; the
shader's test is as before. No measurable speed (tavern -0.3, town -0.1, forest within its wander), fewer rays, the
same picture. Installed: plugin A3F635C34717, native DB9225B460BF (hashes match in all three places).

RUN D (`20261011-071110`, `water-day@confirm-r20` then `metal@herbs`, the installed build, clean; started 07:14 after
Starfield closed): the beach's frozen picture as last night's (60.9 levels, its region 40.8; run B 60.8, 40.7), with
the water question now skipped above the water; its running steps 118, 117, 113, 111 ms of the tracer (run B 122, 119,
115, 114, drifting down the same way). The town loaded AFTER the beach: the herbs green at level 6, 4 and 6 again
(51.9, 51.8, 51.8 levels). So run C's brown herbs were a one-off (PT-98).

Recipes after it: `gi-key` and `water-ask` removed (their switches are gone with the rules); `omm-level` now measures
leaves at 4 and 5 against the rule's 6; `confirm-r20` (tonight's build, frozen and running), `herbs`, `micromaps-left`
(the cut-outs' prize) and `features` (each feature's cost) stay.

## 11. Morning (07:30 on, the owner awake): fog and translucents first, then everything HDRP runs in sequence

The owner's words, in order: "so is fog and trandlucents traced now? Nothing like thst left in hdrp, only hud and post
proceSing? Also please do add RTX Mega Geometry ... we are still looking at 6-20 fps framerates ... there has to ve more
large bottlenecks ... we are still tracing withojt caps/limits for distance"; then "wvy ks fog and trandljcebts untraced,
wasnt that the entirr task i gave you beforr going to bed? ... What were you working on if not that?" (the coordinator
had left the fog as an off trial at 03:50 and gone to speed: a deviation, admitted). Rulings (BACKLOG): fog and
translucents first, then the sky, then Mega Geometry; one subagent at a time (Codex is out until 10-16); and at ~09:45,
told the sky makes the trace wait for HDRP's sky copy each frame: "OK well then definitely bring that over too then",
"Basically we want to move over anything that could have run sequentially or block the GPU before, so we know there's no
HDRP bottlenecks and any GPU/CPU waits will come from our own rendering, so we can optimize it. We are far, far away from
our performance targets still". HDRP keeps post-processing, the upscaler and the HUD; everything else moves.

What was done (CHANGELOG, the entry "2026-10-11 morning"):
- The traced fog is the rule: trial switch and walk removed; `HDRP fog in place of the traced fog` (hidden, off) for
  comparing. Multiple scattering from the mist's own radiance-cache cells. Run `20261011-091933`: Broc Meala mist 91
  (HDRP 69, first order 61), smooth; night: torch glow and shadow in the mist, the moon's blue haze gone (a look for the
  owner), grainy (NRD does not denoise the fog; equiangular sampling for lamps is the next noise fix).
- Mirrors, glass and water rays cross the traced medium (`pt_medium.hlsli`, new: the medium's functions moved out of
  `pt_fog.hlsl`). Run `20261011-094755` (`mirror-fog@mirror-fog-traced`): the mirror shows a faint warm haze and dims
  what it shows. Its "reference" step films from behind the mirror and so also sees the torch-lit mist on the mirror's
  far side: brighter than any mirror can be. Use the game's own picture of the same place as the reference instead.
- HDRP's `ScreenSpaceFogMultipleScattering` blur skipped while the fog is traced.
- The game's local fog boxes in the medium (not yet seen in the game when this was written): 3D texture sharing (hook
  and self-test), `TGPT_SetFogBoxes`, the density as HDRP voxelizes it, ratio and delta tracking. The scene that shows
  them: `night` (LocalFog boxes 172-390 m away); a blizzard or the campaign map's mist would show more.
- The sky: subagent sky-1 (research, `docs/agents/sky-1.md`): the open world's sky is HDRP's physically based sky with
  the game's own graph (stars, nebula, a day ramp, the Wyrdness), two flat cloud layers; HDRP re-renders the sky cube
  every frame (its hash holds the ShadowCaster's rotation) and runs the sky graph over every pixel since step A; the
  traced fog lacks the atmosphere's haze HDRP's fog pass added to far land outdoors (aerosols 58 times HDRP's default,
  adds light only). Three questions went to the owner (Q1 the visible sun and moon instead of the 30-degree ShadowCaster,
  Q2 the haze dims or not, Q3 cloud shadows). Subagent sky-2 (code, tree copy `%TEMP%\tg-agents\wt\sky2`, base
  `base20sky`, brief `docs/agents/briefs/brief-sky-2.md`) builds the measuring switch and the tracer's own atmosphere,
  clouds and sky pixels.
- Translucents: subagent particles-3 (`docs/agents/particles-3.md`) decoded the campfire's and candles' VFX buffers
  (alive counts, ages, positions, per-system layouts); effects out of view are not simulated by the game (a conflict with
  the owner's off-screen rule, to design around). Next: one run to see whether the game still simulates them with HDRP's
  transparents off, then the hand-over (D3D11 buffers to D3D12 records) and the tracing of fire, smoke and sparks.

## 12. Afternoon (13:30 to 16:00): NRD as NVIDIA documents it; the vendors' guides to the letter

- The owner (~14:25): "I practically always prefer the non-denoised presentation over NRD ... It completely oversmoothens
  detail away and removes fine shadows", then "make sure NRD is set up to perfection", and (~14:50, told NRD had been
  handed the bounced light alone since round 2): "Obviously we want to 100% follow NVIDIA's and AMD's implementation
  guidelines for these technologies" (AGENTS.md now has it as a ruling). Who chose the old route: a Claude fork of round 2
  (an offline number taken with a hit distance of 1 and the triangle's normal), kept by a Claude agent on 10-07.
- Found: the tracer's own direct-light filter took 16 % of the tavern's direct light and its wood grain; the "NRD is
  darker" of the full picture was mostly the traced gain: pt_meter's median read noise low (x2.7 none against x2.2 NRD).
  Both fixed: NRD takes all the light (RELAX DIFFUSE_SPECULAR, REBLUR on the hidden `NRD trial` = 2), demodulated by
  NRD_MaterialFactors, real hit distances (the bounce's through the lobe's lens), the shading normal, 2.5D motion from
  each point's own previous depth (own-motion texture RGBA16F), hair as NRD's strands, history in seconds, the caustics
  traced before the denoisers and handed to NRD; pt_meter meters 4 x 4 block means. The old route deleted (bump_share,
  guide_plane, SMOOTH_PLANE, the direction code, the direct filter under NRD).
- Runs: 20261011-150230 (trial: the gravel milky with the old route, kept with RELAX/REBLUR), -151710 (fair gain),
  -154933 (the clean build C986D0E687AC: tavern none 18.0 / RELAX 16.4 / REBLUR 16.9, the gravel's detail 15.3 / 7.9 /
  8.5). The owner watching: "NRD is a million times better now", then: the traced fog "still super noisy" (PT-118).
- GPT-6-astra's review of all denoiser integrations against the guides: `docs/agents/review-vendor-astra.md` (28
  findings); split in PT-127. Codex worker `vendor-rr` (brief-vendor-rr.md) takes RR 2, 4, 18, 19 and AMD 6, 14, 15, 28.
- The fog: the owner chose "NVIDIA's Remix way" (a froxel radiance cache, RIS and temporal reservoirs per cell, long
  accumulation): Codex worker `fog-froxel` (brief-fog-froxel.md).
- RTX Mega Geometry: megageo-2 measured clusters 40 to 130 % slower a ray; the owner: "send it back ... Still want us to
  get this done"; megageo-3: our integration matches NVIDIA's own (+3 to +10 % on its sample's scene); the plants' gap is
  their grouping (16 to 18 porous copies in one structure) and the cutter. Claude worker `megageo-4` builds the small
  plants as clusters for a game trial (brief-megageo-4.md).

## State at 12:30 (10-12): PAUSED BY THE OWNER

- THE OWNER, ~11:40: "When your agents are done running, and when you've finalized the experiments you want to run,
  please pause until I give you to go-ahead to continue working, as I want to save on my tokens and GPU after your
  current batch of work". At 12:12 the owner opened Starfield and the harness refused the next run. SO NOTHING RUNS:
  no worker, no game run, no offline test until the owner says go. The two workers were told at 12:18 to write
  their state and stop; both did (12:20, 12:22). The watchdog `Stop-TestsBesideGame.ps1` and three leftover waiting
  scripts of the coordinator's were ended at 12:30 (the owner: "There is still an open subagent by the way", "and 4
  shells"): when work goes on, start the watchdog again before any worker may test, and end every waiting script
  with the job it waited for.
- THE OWNER'S ANSWERS (all 14, 11:20; the choices page, AGENTS.md "The choices of that day", each entry of
  PT-REQUESTS): recorded. And two more of the owner's words since:
  (1) the lit mist: "Loved the look of the lit fog you had", then at 12:00 "Looks amazing with the lit fog around the
  lights, also in the forest scene - so pretty": a look confirmed in game (AGENTS.md, the fog's rule);
  (2) "The images are too low-res to judge": pictures to judge closely go at the game's own pixels, with their paths.
- THE LANTERN QUESTION, ANSWERED BY MEASUREMENT (PT-147): the town's lamps are bright or nearly off BY THE GAME'S OWN
  STATE: after the beach's save in the same session "PointLight_Ambience_02" reads 524,288, with the town loaded
  first 0.125 (90 of 1,075 lights differ by more than 1.5 times; the game's own picture differs too). 19 of 19 runs
  with the beach first have the warm glow, 47 of 47 others do not. Which piece of the game sets it: not found. The
  pictures: `artifacts\harness\_notes\lantern-by-load-order-full.jpg`. The harness runs scenes in
  `scenarios.json`'s order (`water-day` before `metal`): PT-HARNESS "A scene depends on what was loaded before it";
  `tools/harness/compare_lights.py`.
- MAIN AND THE GAME FOLDER: native 70948346AD5A in `build\native`, `artifacts\Native` and the game (hashed 12:14;
  kept as `%TEMP%\tgb\main-70948346AD5A.dll`); the plugin 1A6C8C6297DA, deployed 12:12 (`Mega Geometry trial` 7 and
  `Caustics grid trial` 3 by default; the owner's own config holds `Mega Geometry trial = 7`). Changed in the
  sources after that build: tests (the fog check), the harness's runner and recipes, tools, docs: no native change.
- MERGED SINCE 11:15: caustgrid at 11:42 (its game run 20261012-114808: the caustics' stage 5.25 / 3.28 / 3.12 / 3.13
  / 5.41 ms in the town, 5.34 / 3.42 / 3.12 / 2.75 / 5.27 in the forest, the beach's frozen picture 56.0 / 56.2 /
  56.3: the default is 3 since 12:10; PT-114); fog3's part A at 12:20, by hand (`scratchpad\merge_fog3_a.py`: the
  check's exact value with the ground's 4 % highlight, the pins, the night's cases; its trial of the undimmed sky
  left out: PT-138, PT-37). The test's exe compiles; 121 source checks pass. THE FOG CHECK HAS NOT RUN IN THE MAIN
  TREE.
- RTX MEGA GEOMETRY (PT-109): standing in the town, one run (20261012-113255): a frame 77.46 (plain) / 74.59 (7) /
  78.39 (plain) ms; the default is 7. WORKER megageo7 (`wt\megageo7\docs\agents\megageo-7.md`, base `base20r`) read
  last night's walk second by second: the loss on a walk is OUR CPU, not the rays (the plants' plan 16.6 ms a frame
  against 10.8: a group of plants made anew asked the driver for two buffers of its own and twice the room, and a
  bottom level a plant put 10,686 instances into the top level). Its mend, the hidden trial's new 9 (a group ONE
  bottom level from all its plants' clusters, one instance, everything in pieces of one arena; 10, 11 the same in
  the partitioned top level; 12 the yardstick with a buffer a group): the trial DLL
  `%TEMP%\tgb\megageo7-handover\TGPathTracing.Native.dll` (8A38D375B81B..., no plugin change, no shader change;
  it does not hold caustgrid's merge). NOT SEEN IN THE GAME. Its runs: `metal@mega-group-walk -Measure` (plain / 7 /
  12 / 9 / plain, walking: takes the owner's screen, ASK) and `metal@mega-group` (standing); the recipes are in its
  copy's scenarios.json. It stopped at 12:20 on the coordinator's word: offline 9, 10 and 11 trace the plain
  groups' pictures; a clean run of 12, the arena's CPU number and its mutant's run are not done. AND IT FOUND A
  DEFECT OF MAIN'S (PT-148): leaving trial 4, 5 or 6 switches the plants' kept list off for the session; the
  standing run above had it from its trial-5 step on (all its running steps alike: the A/B/A holds, the absolute
  CPU is too high). No run mixes 4, 5 or 6 with other numbers until it is mended (`mega-plants` does).
- THE TRACED PICTURE ONE FRAME LATE (PT-145): WORKER framelate (`wt\framelate\docs\agents\framelate.md`, base
  `base20s`) built the whole-picture way behind the hidden `Advanced / Frame late`: native
  `%TEMP%\tgb\framelate-native\TGPathTracing\TGPathTracing.Native.dll` (A9752A40028D...), its plugin
  `wt\framelate\src\TGPathTracing\bin\Release\net472\TGPathTracing.dll` (D4AE8EB2C4D2...). ITS FORECAST IS SMALLER
  THAN THE OWNER WAS TOLD: the card takes turns between the two devices (round 19), so the town's 77 ms become
  about 67 to 69, not 60; the price is one frame more from the mouse to the screen (about 70 ms there); with Ray
  Reconstruction the switch is not used, with frame generation it is not right yet; what HDRP still draws itself
  (flames' cards, smoke, the interface) runs one frame ahead of the traced world in a turn. Its offline proof (the
  switch off = today, bit for bit) HAD NOT RUN when it was stopped (12:22). NOT SEEN IN THE GAME. Its recipe
  `frame-late` poses the camera by the frame's number, so frame k of every step is a pair for the page; the run to
  ask for: `spec-terrain@frame-late,metal@frame-late -Measure` (PT-145's first bullet has all of it).
- NRD'S TWO FILTERS IN MOTION (PT-127): the first run (20261012-120026) is thrown away: the game's motion blur was
  on. Mended in the harness (`"disableMotionBlur": true` on any step; the recipe carries it). The second run is
  OWED; `tools/harness/motion_clips.py` makes the page's moving pictures from it.

OWED WHEN THE OWNER SAYS GO, in this order (each is short; none was started):
1. The fog check in the main tree: `TGPT_TEST_FOG_FROXEL_ONLY=1` on `build\native\tgpt_gpu_test.exe` (ten minutes;
   the card free, no game of the owner's open: `scratchpad\chain_1210.ps1` step 3 is the pattern): seventeen cases.
2. `Queue-PtRun.ps1 -Scenario 'tavern-room@nrd-motion,spec-terrain@nrd-motion' -KeepFrames` (7 minutes), then
   `motion_clips.py <run> <page's img folder> tavern-room:2150,1100,1000,720 spec-terrain:1450,780,1000,720`, the
   clips into the NRD card of the choices page (its source: read the artifact, AGENTS.md has the link), and the
   owner told. Delete the run's raw frames afterwards (12 GB).
3. megageo7's trial in the game (ask for the screen for the walk), then its merge against `base20r` and the default;
   PT-148's two lines with it.
4. framelate: its offline proof (`%TEMP%\tgb\framelate-scripts\gpu.ps1`, `compare.py`), the run `frame-late`, the
   frames on a page for the owner with the forecast above said plainly. Its copy changes `Runner.cs` at the same
   line as main (the motion blur switch): merge by hand there.
5. The caustics' old way out of `pt_caustics.hlsl` and the tracer (PT-114's OWED).
6. PT-147: find the game's piece that sets the lamps' strengths (reading `TG.Main.dll`, no GPU).
7. The jobs the owner chose and none has begun: the beach's bed (`brief-water-12.md`), the sky (the sun and moon
   one sees, real haze, cloud shadows: PT-112), glass under NRD way B (PT-127), RTXDI's defaults beside ours
   (PT-127), NRD's SH form, a flame's steady reach with pictures (PT-146), a speed baseline WITH the stream and the
   census (ask for the screen each time), the whole GPU test (the coordinator's alone).
The owner makes the saves: at Sagremor's gate, "PT walk", "PT walk town"; a save at the town's market at night
would show PT-147's lamps as the game means them by night (offered, not asked for).

## State at 11:15 (10-12)

- THE OWNER, 11:00 TO 11:10, FOUR RULINGS (AGENTS.md has each, BACKLOG's first ruling line names them):
  (1) "I would prefer if I can stay on the SUnshine/moonlight stream": speed is measured with the stream on.
  (2) Of "the beach's water still looks nothing like the game's": "that doesn't matter, the game's water doesn't look
  good in the first place. It's fine that ours look different, so long as it looks amazing" (PT-81's newest bullet;
  `brief-water-12.md` reworded: the game's picture is no yardstick for water).
  (3) "I still want us to enable RTX Mega Geometry. Do not turn it off. I still insist there must be something we've
  done wrong if it's not making things faster. Othewise, what's the point?": it goes on by default and stays on
  (PT-109's first bullet; out of DEAD ENDS). WORKER megageo7 STARTED 11:12 (copy `wt\megageo7`, base `base20r` =
  main D473560B18DA's sources; `docs/agents/briefs/brief-megageo-7.md`). THE DEFAULT IS NOT FLIPPED YET: first the
  run `metal@mega-plants` on today's build (queued, below), then `Mega Geometry trial`'s default to 7 in
  `Settings.cs` (and the recipes that set 0 looked through).
  (4) "please present me a page with visualizations so I can easily pick": THE CHOICES PAGE,
  https://claude.ai/artifact/JbWmoPqCgAS7em2nN1aSFt (source `scratchpad\decisions\index.html` and `img\`; 14 cards;
  each pick is a document `picks/<id>` in the page's store: read them with the ArtifactData tool, action list,
  collection `picks`). PICKED SO FAR (11:06): night-mist "Lamps light the mist" with the note "Anything can light the
  mist - not just the sun and moon."; fog-bounces "All bounces". Both were today's way: the rule now; THE FOG LAMPS
  TRIAL IS OUT OF THE SOURCES (`scratchpad\fog_lamps_out.py`). READ THE STORE AGAIN before acting on any other card.
- MAIN IS D473560B18DA (`build\native`, `artifacts\Native`; kept as `%TEMP%\tgb\main-D473560B18DA.dll`): the water's
  pieces in groups (waterblas, merged 11:07), the fog lamps trial out, the water rule with no switch. THE GAME
  FOLDER STILL HOLDS 8B85361299E2 until the queued runs deploy it: `scratchpad\runs_1120.ps1` (task bd436jxgy, from
  11:20): 1 main: `metal@traced-fog`, then `forest`, `water-day`, `metal` `@speed-now`; 2 the build before
  (`main-8B85361299E2.dll`) on `metal@traced-fog` for the frozen A/B of the town; 3 `metal@mega-plants` on main.
- caustgrid: its hand-over is in `%TEMP%\tgb\caustgrid-handover-1` (native A6DA417B13F8, its plugin 43DB889189FF;
  hidden `Caustics grid trial` 0 to 3, frame flag bits 19, 25, 26; recipes `caustics-grid`, `caustics-grid-speed` in
  ITS copy's scenarios.json). It confirms its offline check first (the card was its until 11:20); then its trial in
  the game needs ITS PLUGIN beside its DLL (put the plugin in by hand, keep main's to put back), then the merge
  against `base20p`.
- fog3: told at 11:10 that the lamps trial is out and what the owner ruled; its parts A and B stand.
- THREE WORKERS: caustgrid (finishing), fog3, megageo7. `brief-water-12.md` waits for a slot and caustgrid's merge.

## State at 11:00 (10-12)

- THE OWNER, 10:55: "I would prefer if I can stay on the SUnshine/moonlight stream". THE STREAM STAYS ON AND SPEED IS
  MEASURED WITH IT (AGENTS.md, "The stream stays on"; `analyze.py` no longer calls Sunshine a problem and names the
  stream; PT-HARNESS, PROJECT-CONTEXT say so). Never ask for the stream off again. A new baseline with the stream
  is owed; the census too (both need the game in front: say so before, the owner is at the PC).
- MAIN'S BUILD WITH THE SWITCH 64 OUT: 8B85361299E2 (`build\native`, `artifacts\Native`; kept as
  `%TEMP%\tgb\main-8B85361299E2.dll`); it goes into the game folder with the next queued run.
- waterblas IS DONE. Its hand-over 2 (B4DA3E0E77EE) runs as a trial with the self-check (`record-check`) and the
  forest's walk as soon as the card is free (`scratchpad\run_waterblas2.ps1`, task b651oi78i); then
  `python tools\agents\merge_copy.py waterblas --base=base20o` (previewed: 0 conflicts), build (one pass and the
  C++), deploy, a frozen A/B of the town against `main-8B85361299E2.dll`. PT-99's second bullet has its numbers.
- caustgrid: checked offline (bit-equal, 2.2 to 3.9 ms of the stage; PT-114's newest bullet); its final build and
  mutants, then its DLL for `metal`, `forest`, `water-day` (its recipes `caustics-grid`, `caustics-grid-speed`).
- fog3: running since 09:52. A SLOT IS FREE: `brief-water-12.md` waits for waterblas's and caustgrid's merges (it
  works in the caustics' and the water's files).

## State at 10:30 (10-12)

- IN THE GAME FOLDER: native 64257E205B07 (`build\native`, `artifacts\Native`, the game: hashed 10:26; kept as
  `%TEMP%\tgb\main-64257E205B07.dll`; the one before, 18784FDDA14D, is kept only in `artifacts\harness\builds`),
  plugin 02C007EB5DF3. It holds: the fog in cells, PT-136's bound, PT-114's rule for interiors, the fog lamps trial
  (hidden, on = as before), the water's sun summed the plain way.
- THE WATER RULE'S FINAL BUILD RAN (`20261012-101445`, the beach frozen: on / off / on 56.1 / 56.4 / 56.4, the
  shallows 37.8 / 38.8 / 38.8): the light holds. THE SWITCH 64 IS OUT OF THE SOURCES SINCE 10:35
  (`scratchpad\switch64_out.py`: `pt_water_sun.hlsli`, `pt_common.hlsli`, `tracer.cpp`'s log line and mask, the
  source check's pins, the recipes `sun-cell` and `sun-cell-still`; CHANGELOG has the rule). NOT BUILT: THE SOURCES
  ARE AHEAD OF THE BUILD IN THE GAME FOLDER (64257E205B07) BY THAT; every pass compiles again, so it is built with
  the next merge (waterblas's or caustgrid's). Before a deploy for the owner: build first.
- caustgrid FOUND WHERE THE CAUSTICS' 4 ms GO (10:17, its message; `wt\caustgrid\docs\agents\caustgrid.md`): the
  lights' pick: in every tile one thread of 256 does two serial sums of 256 steps while 255 wait, 8,100 tiles a
  frame, whatever lands. Built behind frame flag bit 19: the pick in a pass of its own, only tiles over their
  slot's water, lights that cannot land in view not traced; its checks wait for the card, which is free for it
  from 10:27. Then its DLL comes for `metal`, `forest`, `water-day`.
- waterblas: its DLL ran (the block below); two more checks, then its hand-over. fog3: started 09:52.
- NO GAME RUN IS QUEUED. The census (measuring build CD000326AA43) still waits for a card without the stream.

## State at 10:00 (10-12)

- THREE WORKERS since 09:52: waterblas (two more checks, then its hand-over), caustgrid (the caustics' grid outdoors),
  fog3 (copy `wt/fog3`, base `base20q` = main's sources of 09:50 with campfix's merge, not yet built then;
  `docs/agents/briefs/brief-fog-3.md`). Waiting for a slot: `brief-water-12.md` (the beach's bed).
- THE NIGHT TRIAL RAN (`20261012-093855`; PT-37's newest lines): with the lamps out of the mist the orange glow is
  gone and the bright end is the game's, but the night is half as bright as the game's (20.5 for 38.7): the moon's
  haze is not in the traced fog. `docs/agents/briefs/brief-fog-3.md` is ready for the free slot: A the fog check's
  three failing cases (the ground too bright under fog: PT-138's newest bullet), B the moon's haze.
- waterblas'S DLL IN THE GAME (`20261012-094311`; sent to the worker): the record's CPU a frame, main / its DLL: the
  beach 7.00 / 4.3 to 4.6 ms (the water's pose 3.72 / 0.20), the town 7.2 / 6.1 to 6.4 (2.05 / 0.14), the forest 6.2
  / 3.7 (2.70 / 0.15); 66, 40 and 51 groups for 2,459, 1,379 and 1,793 pieces; the structures' pages 160 to 128 MB
  at the beach and in the forest; the pictures the same in a running world but the town's 3 levels (a frozen A/B of
  the town at the merge). The worker goes on with two checks, then hands over.
- MAIN IS BUILDING with campfix's merge (`scratchpad\build_only.ps1`; the two big passes). After it: deploy,
  `water-day@sun-cell-still`, then the switch 64 comes out. THE GAME FOLDER NOW: native 18784FDDA14D (main with the
  fog lamps trial, before campfix's merge), plugin of 09:38.

## State at 09:50 (10-12)

- campfix IS DONE (09:40) and its second hand-over is IN THE SOURCES, merged by hand (`scratchpad\merge_campfix2.py`:
  a three-way merge would have kept the correction it took out): the water's rule summed the plain way. NOT BUILT
  YET: `pt_water_sun.hlsli` changed, so the two big passes compile again (10 minutes each). After the build: one
  frozen run of the beach (`water-day@sun-cell-still`), then the switch `Speed trial` 64, the recipes `sun-cell` and
  `sun-cell-still`, the source check's pins of the switch and the log's line go (the worker's note: the mask
  `0xFF80u` in `tracer.cpp` goes with it). New entries from its report: PT-142, PT-143, a bullet under PT-68.
- waterblas HANDED A FIRST DLL (09:36): `%TEMP%\tgb\waterblas-native\TGPathTracing\TGPathTracing.Native.dll`,
  5343B2B8C137: the water's pieces in groups (about 55 refit calls a frame in the forest for 1,793), every frame of
  its offline walk bit for bit the ungrouped picture. ITS GAME RUN IS QUEUED (`scratchpad\runs_0940.ps1`: the beach,
  the town, the forest, `speed-now`; the worker wants "PT record CPU 10s", "PT water groups 10s", the scene stage).
- PT-37 ON TRIAL IN MAIN (native 18784FDDA14D, plugin with it; in the game folder since 09:38): the hidden `Traced
  fog: lamps light the mist outdoors` (on = as before). Off: where the game's Fog volume says "directional lights
  only", no lamp lights the camera's fog (frame flag bit 24, the cells' pass and the cache's feed; the lamps' pass
  not run; NOT the fog a reflected ray crosses). Its run is queued first: `night@fog-lamps`.
- THE FOG'S OFFLINE CHECK RAN FOR THE FIRST TIME (`scratchpad\fog_check.ps1`, `%TEMP%\tgb\coord-fogcheck.txt`;
  the test target had not compiled since the fog merge: `pathtracing_nosun_checks.inl` still named the removed flag,
  fixed, both workers told the same line): 108 ok, 3 CASES FAIL "the fog is what is known exactly": the sky and the
  air over the horizon are right (x0.996 to x1.000), THE GROUND IS TOO BRIGHT: near ground x1.24 (sun and sky, one
  order), x1.93 (a lamp 6 m ahead), far ground x3.43 and near x2.63 (a lamp 60 m ahead), round that far lamp x1.16.
  Either surfaces under fog get too much light or the check's exact value leaves a part out: NOT LOOKED INTO; the
  numbers are to go into PT-138. Not caused by the trial (the lamps' factor is 1 there; "finite and nonnegative" ok
  in every case, the no-sun ones too).

## State at 09:25 (10-12)

- THREE WORKERS: waterblas (08:42, the water's pieces in fewer structures), caustgrid (09:07, the caustics' grid's
  cost outdoors; copy `wt\caustgrid`, base `base20p`, `docs/agents/briefs/brief-caustgrid.md`), campfix (finishing:
  the water rule summed the plain way). The next for a free slot: `docs/agents/briefs/brief-water-12.md` (PT-81).
- THE MEASURING BUILD IS READY: `%TEMP%\tgb\measure-build\TGPathTracing.Native.dll`, CD000326AA43 (main's sources of
  08:50 with CMake's `TGPT_SPEED_MEASURE`; never deployed for players). THE CENSUS WAITS FOR A QUIET CARD (the
  owner's stream is on since 08:26): `Queue-PtRun.ps1 -Scenario tavern-room@measure -TrialDll <that DLL> -Measure`,
  then `metal@measure`, `forest@measure`; its first start takes the driver 5 to 20 minutes.
- THE BEACH, LOOKED AT BY ITS PARTS (PT-81's newest bullet; runs `20261012-090731`, `-091756`; recipes `water-look`,
  `water-noview`): the dark slab on the shallows is the Caustics row's own; the hidden `Water light without view`
  cures it (the region 37.3 to 49.6, the game's 49.9) and still brings white blobs under NRD and 17 ms; the green
  "sky" is a far mountain the traced fog turns flat green beside a grey sky (PT-37, PT-112).

## State at 09:05 (10-12)

- IN THE GAME FOLDER: native E98E321830C2 (unchanged), PLUGIN 8A3AF7BA55B9 (PT-114's rule; was 6E72149A874A).
- PT-114 FIXED FOR INTERIORS (PT-REQUESTS has it): in an interior the map's water is neither traced nor drawn. A
  trial behind a hidden switch first (`20261012-085406`: the tavern's frame 49.6 to 43.7 ms, the same picture), then
  the rule without the switch (`-085954`: 43.4 ms; the beach in that run still has its water). The plugin alone; a
  source check pins it. The beach read 102 ms a frame in that run (reflections 54.8 for 41.9 at 06:12): the owner's
  stream was on (Sunshine 24.7 % of the encoder): not a number, the report says so.
- THE NIGHT SCENE, LOOKED AT (`20261012-081117\night\overview.jpg`): the traced night is the torch's orange glow in
  the mist over everything; the game's is a cool blue night with a small warm light. That is PT-37's OPEN (5), the
  owner's look. Faint vertical bands in the dark left part are the 8-bit picture's own steps (measured: 1 level, the
  same in the game's own picture and in the old fog route: `scratchpad\stripes.py`): no defect.
- THE MEASURING BUILD of main is compiling in `%TEMP%\tgb\measure-build` (`scratchpad\build_measure.ps1`, log
  `%TEMP%\tgb\measure-build-coord.log`), for a census of the three big passes (recipes `measure`, `measure2`) when
  the card is quiet (no stream).
- WORKERS: waterblas (since 08:42), campfix (finishing). One slot free.

## State at 08:40 (10-12)

- THE FOG RULE'S MERGED BUILD IS VERIFIED IN THE HARNESS (`20261012-081117`; PT-118's first bullet has the numbers:
  the same light and flicker as the rule's own DLL, under NRD too). It is main, and it is in the game folder: native
  E98E321830C2, plugin 6E72149A874A, all three places hash-checked by the queue.
- ITS SPEED IS NOT MEASURED: THE OWNER'S STREAM STARTED ABOUT 08:26 (Sunshine encoding, 20 to 32 % of the video
  encoder, 0 to 4.6 % of the 3D engines). The forest read 81 ms of the tracer in two runs (`-082625`, `-083016`)
  against 75 at 06:12: bounced light +2.5, reflections +3.0, nothing else moved, and the merge changed nothing in
  those passes: PT-99's first bullet. THE OWNER IS AT THE PC (remotely): no `-Measure` run without saying so, and no
  speed number is quoted while the stream is on.
- campfix reported 08:30 (its message; the report `wt\campspeed\docs\agents\campfix.md`): THE WATER RULE'S MEAN MOVED
  BY ITS OWN LOW-SUN CORRECTION, not by the coarse cell: with the correction the sun's share in the water is 0.6 to
  1.1 % over the reference at a sun of 57 degrees (one band of the picture 1.6 to 1.8 % brighter), 1.5 to 3.8 % at
  8 degrees; the same coarse cells summed the plain way (campspeed's) x0.9996 to 0.9999, every band within 0.05 %.
  MAIN HAS THE CORRECTION (merged 05:20). The coordinator's answer: the plain sum made the rule in the worker's copy
  (the three uses of `ownSun` in `pt_water_sun.hlsli`), its check's expectation corrected, hand over the sources
  when the offline check has passed; the coordinator then merges and takes the switch `Speed trial` 64 out. A gap it
  names, to enter at the merge: at a very low sun over a steep sea a coarse triangle with a corner turned from the
  sun is left out (about 2 % reckoned; the cure keeps the triangle's own area across the light).
- WORKERS, 08:45: `waterblas` STARTED 08:42 (Opus, its copy `wt\waterblas`, base `base20o` = main E98E321830C2's
  sources of 08:38; `docs/agents/briefs/brief-waterblas.md`: the water's 1,400 to 1,800 pieces in fewer structures:
  2.1 ms of the forest's record is their refits' calls, 0.5 their plan). campfix is finishing (the plain sum). ONE
  OPUS SLOT IS FREE. The watchdog `Stop-TestsBesideGame.ps1` was started again at 09:25 and runs until
  22:00: start it again after that while workers may test.
- THE DISK: 8.4 GB FREE at 08:45, 134 GB AT 08:50. The owner, asked: "Yes, delete all the old ones, and make sure
  our Clear-PtHarnessRuns.ps1 tool also does that"; of the page file (114 GB): "Leave it as it is". Done:
  `Clear-PtHarnessRuns.ps1` has a section for `%TEMP%\tgb` (`-WorkerDays`, 2 by default; its test's sixth case,
  four mutants; AGENTS.md, PT-HARNESS, README, AGENT-WORKERS say so); run with `-WorkerDays 0.5 -Apply` it deleted
  3,266 folders and files, 130.8 GB, everything not written since 10-11 20:55. What stays (179, 14.8 GB): the
  night's builds and evidence (`campfix*`, `fogrule*`, `nosun-build`, `reccpu-build`, `fixt*`, `measure-build`,
  `main-*.dll`). The SDK folders and the copies were checked after it: whole.
- THE OWNER ASKED FOR AN OVERNIGHT REPORT (08:50): `docs/rounds/round-20-overnight.md`, in plain words: then and
  now, what was done, what is still wrong, what is in the game folder, ten questions. A census of the three big passes with the measuring build (`%TEMP%\tgb\measure-build`,
  recipes `measure`, `measure2`, `caustics-split`) needs a quiet card: not while the stream is on.

## State at 08:15 (10-12)

- THE FOG RULE'S MERGED BUILD IS BUILT AND IN THE GAME FOLDER: native E98E321830C2 (`build\native`, `artifacts\Native`,
  the game; kept as `%TEMP%\tgb\main-E98E321830C2.dll`), plugin 6E72149A874A (was 6C73FE0D600F). All 53 passes compiled,
  0 failed (22 minutes, two at a time: the PC's memory rule held it). ITS RUN STARTED 08:11 (`chain_runs.ps1`:
  `vista@traced-fog,fog@traced-fog,night@traced-fog,tavern-room@traced-fog`, then `metal@speed-now,forest@speed-now`);
  the numbers to meet are in the block below. Not verified until that run is read.
- THE DISK IS NEARLY FULL: 13 GB FREE OF 3,725 (83 GB at 03:28). Measured 08:13 (`scratchpad\disk_look.ps1`): Windows'
  page file is 113.8 GB (3.6 GB in use, its peak 60.4 GB): it grew while several trees compiled at once and Windows
  gives it back only at a restart. `artifacts\harness` is 19.3 GB (cleaned already: `Clear-PtHarnessRuns.ps1` would
  free 0.4 GB more). `%TEMP%\tgb` holds 142 GB of workers' builds since 10-06. NOTHING WAS DELETED: the owner's word
  is needed for the builds (choice (f)), and a restart is the owner's. Until then: no new tree copies beyond what a
  job needs, watch the free space before a run (a run of four scenes writes 1 to 3 GB of frames), and Windows may
  sweep `%TEMP%` again (`docs/AGENT-WORKERS.md`: the copies are guarded, `repair_copies.py` puts files back).
- campfix's offline check of the water rule ran 07:56 to 08:04 (`%TEMP%\tgb\campfix-runs\run-width.txt`): "a cell as
  coarse as a wide ray allows keeps the mean of the sun's light in the water (within 2 % at 1 m and at 10 km)" ok;
  "the shadow first keeps the mean ... (within 2 %)" ok; BUT "the cell by the ray's width leaves a camera's own narrow
  rays as they were (the sun's share within a hundredth)" FAILS: x1.038, x1.015, x1.023 of the sun's share in three
  frames (the share is 6 to 12 % of the picture, so 0.1 to 0.4 % of it). The worker is still on it; the rule keeps
  its switch (`Speed trial` 64) until it reports.
- ENTERED 08:10: PT-139, PT-140, PT-141; bullets under PT-81 (the beach's water and sky now) and PT-37 (the traced
  fog 30 to 40 % brighter than the game's). PT-136: the outlier lamp is the open portal's light at Sagremor's gate
  (2 to the 27th cd times a dimmer of 16, HDRP's fade distance 64 m): a question for the owner (j).

## State at 08:00 (10-12)

- PT-136 IS FIXED FOR ITS OUTLIER (PT-REQUESTS has the numbers): the first trial missed (it was a lamp, not a glowing
  mesh: the new log lines said so); the second, "no lamp counts for more than 64 times the median lamp", brought the
  vista's reference from 5.7e8 to 1.27e6 and its darkest 5 % from 29 to 38, the game's own; the town's reference is
  unchanged. It is the rule (in main since 37237BC1B620, which is IN THE GAME FOLDER, hash-checked by the queue 07:44).
- THE FOG'S RULE IS MERGED INTO THE TREE (07:50; fogrule2's copy against `base20g`; the code merged clean, the docs and
  `scenarios.json` by hand: `scratchpad\merge_fogrule.py`, `scenarios_fogrule.py`, `docs_fogrule.py`). 120 source
  checks pass. ALL 53 PASSES ARE COMPILING (`%TEMP%\tgb\coord-shaders.log`, started 07:47). NOT BUILT INTO A DLL YET:
  `build\native` is still 37237BC1B620 (the build before the merge, also kept as `%TEMP%\tgb\main-37237BC1B620.dll`).
  WHEN THE COMPILE IS DONE: `scratchpad\build_only.ps1` (or the three commands of AGENTS.md), deploy
  (`Deploy-PtBuild.ps1 -Native`: the PLUGIN changed too, it builds it), hash-check, then `vista@traced-fog,
  fog@traced-fog,night@traced-fog,tavern-room@traced-fog` and a look at `metal` and `forest`: the fog's numbers should
  be the rule's own run's (`20261012-072747`: luma 132.2 vista, 17.8 tavern, 54.7 night, 91.7 fog; flicker 0.46,
  0.35, 0.20, 0.18) with the vista a little brighter still (PT-136's bound came after that run).
  IF THE MERGED BUILD IS WRONG: the rule's own DLL is `%TEMP%\tgb\fogrule2-rule\TGPathTracing.Native.dll`
  (C70025F1C243, without the night's other merges); main before the merge is the kept DLL above; the sources before
  the merge are in no snapshot but the copies (`wt\fixt` + reccpu + campspeed's merges are in `base20n` and later).
- fixt's two offline checks are merged (PT-125, PT-134: each passes on its fix and fails on its mutant).
- WORKERS: fogrule2 (handed over; waits a while for its offline check), campfix (waits for its offline check).
  TWO OPUS SLOTS ARE FREE.
- STILL OWED BY THE COORDINATOR: the whole GPU test (never got the card); the merged build's verification.

## State at 07:05 (10-12)

- PT-136 (NEW, found by fogrule2 while hunting the vista's 1.5 %): one large glowing mesh put the reference radiance
  20,000 times over the sun's white at the vista, so light was stored under a 16-bit float's range and dim light was
  lost or shown in steps (the old fog route lost its light there). The root is in how a glowing mesh counts for the
  reference; A FIX IS ON TRIAL IN MAIN (A174548E1B9D, deployed by the queue when the card is free; the old main is
  kept as `%TEMP%\tgb\main-4EA43DDDBC19.dll`). IF THE A/B SAYS NO (the picture worse, or the reference unchanged):
  put the old line back in `scene.cpp` (`brightestEmitter_=std::max(brightestEmitter_, float(sampledPower));`),
  rebuild, deploy. Its log line and `brightest_parts` may stay.
- THE FOG: trial 5 settled the vista's 1.5 % (PT-136's proof). fogrule2 has applied THE RULE in its copy (the old
  camera route, the hidden switch, frame flag bit 19, the "fog filter" stage and the comparing recipe out; the fog's
  target one RGBA32F) and is building it; it hands a DLL for a last game run and then the sources.
- QUEUED (`%TEMP%\tgb\seq-reference.log`), waiting for Starfield to close: `vista` on the new main, `vista` on the old,
  then fogrule2's `vista@fog-rule` and `vista-night@fog-ab` with trial 5.
- THE WHOLE GPU TEST was started and stopped again (06:50): its build found `native/upscaling/src/rr_upscaler.cpp`
  broken since 10-11 18:14 (a quote missing in the log line of the RR preset change; nobody had built that target
  since: THE RR PRESET CHANGE HAS NEVER BEEN BUILT OR DEPLOYED). Fixed; every native target builds; `artifacts\Native`
  now holds fresh builds of the bridge, TGFG and TGBU natives too (not the released ones: mind `Build.ps1 -Deploy`,
  PROJECT-CONTEXT 11). The whole test itself is still owed (it gave way to PT-136's trial).
- WORKERS: fogrule2, fixt, campfix (its offline check).

## State at 06:25 (10-12)

- THE NEW BASELINE (PT-99's first bullet, `20261012-061203`, main 4EA43DDDBC19): the tavern 21.5 fps, the town 12.9, the
  forest 11.2, the vista 18.0, the beach 11.2 (at 01:20: 21.0, 11.3, 11.4, 13.3, 6.7). The forest's tracer is 7 ms
  slower than at 01:20: to look into (the plants that PT-125 had taken out?).
- THE FOG: trials 2 and 3 answered no to two suspects for the vista's +1.5 % (the cells weighed by the scattering:
  no change; the old route with 16 points a pixel: it stays lower). Trial 4 (the grid's last pass split in two
  halves, on the vista) is queued. The candidate (trial 1) stays the base.
- QUEUED: `forest@camp-walks` on main, then trial 4 (`%TEMP%\tgb\seq-trial4.log`).

## State at 06:10 (10-12)

- IN THE GAME FOLDER: main 4EA43DDDBC19 with the plugin 6C73FE0D600F (deployed 05:52, the queue hash-checks before
  every run): everything of the night merged (PT-125's fix, the loop guards, the audit's fixes, the record's CPU cut,
  the water's rule). ITS FIRST RUN: the beach 11.3 fps (it was 6.7 at 01:20), the tavern 21.4.
- RUNNING / QUEUED (each takes the lock in turn): `forest@camp-walks` on main (the crash walk with everything
  merged), fogrule2's trial 3 on the four fog scenes, a new baseline of the five scenes (`speed-now`, not `-Measure`:
  the owner may be at the PC).
- THE FOG (PT-118's first bullet): the candidate holds under NRD too; open: the vista's +1.5 % (trial 3 asks whether
  the OLD route's picture is the one that is short there), then the rule.
- WORKERS: campfix (waits for the card for its offline check of the mean; everything else of it is merged), fixt,
  fogrule2.

## State at 05:25 (10-12)

- MERGED INTO MAIN since 04:45: reccpu (the record's CPU: the town 10.5 to 7 ms a frame, the forest 9.5 to 5.6; its
  self-check in the game found no difference over eight walks) and campfix (the water's patch no finer than the ray:
  the beach 121 to 73 ms; the light held in the frozen A/B; "the shadow first" out; the hill's recipes out).
  `build\native` is 890DC44FE597 = stale + reccpu, built 05:16. CAMPFIX'S SHADERS ARE COMPILING (all 57 passes,
  started 05:20, `%TEMP%\tgb\coord-shaders.log`): when done: `scratchpad\build_only.ps1` (or `cmake --build build\native
  --target TGPathTracing.Native`, the install, `Deploy-PtBuild.ps1 -Native`), hash-check, then `water-day@sun-cell`
  and `forest@camp-walks` on it. THE GAME FOLDER HOLDS 890DC44FE597 (stale + reccpu; put there by the queue
  at 05:22, hash-checked 05:25) with the plugin 6C73FE0D600F (campfix's hidden setting's text) UNTIL THEN.
- THE FOG: fogrule2's first trial (fogrule's candidate, the cells over the whole way) is calmer than the old route in
  all four scenes with the same light (PT-118's first bullet). Owed: its cost (`fog-speed`), NRD, then the rule.
- THE WATCHDOG: `tools\harness\Stop-TestsBesideGame.ps1` runs in the background until 10:00 (it ends every offline
  test when Starfield opens: one ran on beside it at 04:51). AGENTS.md has the line.
- WORKERS: campfix (its offline check of the mean waits for the card), fixt (offline checks for PT-125 and PT-134),
  fogrule2. reccpu ended 04:58 (merged), stale 04:17 (merged).

## State at 04:45 (10-12)

- IN THE GAME FOLDER: main AC29308FC2CE (04:18; hash-checked by the queue at 04:33 and 04:43): PT-125's fix, the
  guards, the audit's three fixes (PT-134 the mesh decals' stale address, PT-135 the emitters' stale table, a leak
  of views), the marks, the record's CPU timers. 32 walks into the camp without a reset on the fixed builds.
- THE WATER'S SPEED (campfix's build, PT-99's first bullet): the beach 121 ms to 73 ms of the tracer, a frame 141 to
  92 ms. Not merged: its picture's proof runs now (`sun-cell-still` in the game; its offline check when the card is
  free). Its second rule ("the shadow first") gains nothing in the game's two views: out unless it shows a gain.
- THE FOG (PT-118's first bullet): fogrule's smallest fix makes the grid's light equal the old route's in all four
  scenes; the grid is calmer everywhere but in the fog scene (3.6 times the flicker there). A fresh worker when a
  slot is free.
- THE WALK: with the camera on the ground the forest's 150 m ahead ends in a rock massif either way (`walk-end.png`:
  under the land with the first rule, inside rock at y 77 with the second). A straight walk from that save has no
  honest end: A SAVE AT THE START OF A CLEAR PATH IS NEEDED for walking speed (asked of the owner).
- stale ENDED 04:17 (merged). WORKERS NOW: campfix (waits for the card for its offline check), reccpu (the record's
  CPU), fixt (offline checks for PT-125 and PT-134, with mutants).
- The owner plays Starfield on and off through the night (02:01, 02:08, 02:19, 03:02, 03:59 to 04:12): the runs wait
  in the queue each time.

## State at 03:55 (10-12)

- PT-125 CONFIRMED IN THE GAME: 16 walks into the camp without a reset on the fixed build (4 of 4 runs reset within
  five walks before). And the picture: after a walk the save's view had bare ground, no grass, no pines; with the fix
  they are there (`20261012-025731\forest\4-walk-3\frame.jpg` against `-030621`'s). The fault took every swaying plant
  out of the traced picture once the set of plant groups had changed.
- PT-131 IS NO PLACE (campfix; confirmed by `forest@camp-look`, `20261012-032137`): the harness's walk flew the camera
  level into a hill, and the pictures looked at were the walk's START. The harness's walk now keeps the camera over the
  ground (`CameraSlide.cs`: "the camera kept 2.24 m over the ground"), says where it ended and takes `walk-end.png`
  there. ITS FIRST GROUND-FOLLOWING RUN (`20261012-032808`) ended somewhere with next to no bounced or direct light
  (0.26 and 0.39 ms) and 65 to 73 ms of reflections with Caustics on: where that is, the next run's `walk-end.png`
  says (running now with the guards' build). The forest's walk frame times before 03:30 are of the hill.
- WINDOWS SWEPT THE TEMP FOLDER AT 03:28 (Storage Sense, the disk 98 % full; found by fogrule): every worker copy and
  base lost its files last written before 10-06, its SDK links and its runtime DLLs. Put back for the live trees
  (reccpu, fogrule, nosun and the four bases; campspeed was spared), `New-TreeCopies.ps1` and
  `tools\agents\repair_copies.py` now guard against it (`docs/AGENT-WORKERS.md`). `%TEMP%\tgb` holds 142 GB of
  finished workers' builds: NOT deleted (the owner's word).
- fogrule ENDED 03:40 BLOCKED (nothing run on the GPU; PT-118's first bullet has its builds and suspects). nosun
  ended 03:00 (merged). Workers now: campfix (water that is really in view: `water-day@sun-cell`), reccpu (the
  scene's record on the CPU; it has the town's and the forest's lines). ONE OPUS SLOT IS FREE.
- IN THE GAME FOLDER: main B64A90716586 (03:44: PT-125's fix, the guards against four endless loops, the plants'
  numbered marks; every pass made anew by the driver at its first start). Running: `mirror-tavern@psr-mirror`, then
  `forest@camp-walks` (`%TEMP%\tgb\chain-0345.log`).
- THE RECORD'S CPU, the lines (`20261012-033350`): the town frozen 5.8 ms (the water's pose and the characters 2.3,
  the plants' plan 1.75); the forest running 9.5 to 9.8 (the lights 3.4: their grid is built every frame there; the
  water's pose and the characters 2.9; the plants' pose 1.3, their plan 1.0).

## State at 03:10 (10-12)

- PT-125, THE CRASH, HAS A CAUSE AND A FIX (PT-REQUESTS has the whole story): the plants' pose pass read each vertex's
  range by a place in a buffer that is laid out anew every frame; after a walk it read other groups' records, at times
  records of meshes already let go, and that reset the GPU. Found with three steps of marks in the log (the stamp's
  number; the plants' steps numbered; a mark after every pose dispatch) and four runs of `forest@camp-walks` that all
  reset within five walks. Fixed in `pt_sway.hlsl` (the number is kept within the group). IN THE GAME FOLDER: main
  F1E5449077BA with the fix (hash-checked 03:02). ITS TWO CONFIRMING RUNS WAIT FOR THE CARD (Starfield is open again;
  `scratchpad\chain_runs.ps1` holds the lock and starts them when it closes).
- nosun ENDED 03:00 with nothing run on the GPU and four endless loops found by reading (PT-129's leads: the fog
  boxes' tracking with a ray that is no number; the mirror record's draw-until-accepted). Its copy is MERGED (guards,
  test switches, a mode never run); all 57 passes are compiling (03:05, `%TEMP%\tgb\coord-shaders.log`); THE DLL IS
  NOT LINKED YET (after the walks: `scratchpad\build_only.ps1`, then deploy and hash-check), so `build\native`'s DLL
  is still F1E5449077BA.
- PT-131: campfix has the measuring run's numbers and the question (do the rays leak through the ground).
- PT-99: the scene's record on the CPU, by section, is in the log ("PT record CPU 10s"). The forest standing: 16.7 ms a
  frame: the plants' plan 3.8, the lights (tree, grid, clusters) 3.5, the water's pose and the characters 3.4, the
  plants' pose 2.1, the emitters' list 1.8. A town run for its line is owed.
- THE HARNESS since 02:15: `Queue-PtRun.ps1`; a run ends when the tracer says the GPU stopped; the configs come back
  after a reset game has closed.
- WORKERS: fogrule (waiting on its own GPU runs), campfix. One Opus slot is free.

## State at 02:15 (10-12)

- THE OWNER IS UP: Starfield opened at 02:01 and again at 02:08 (94 % of the card), each time for two or three minutes.
  No run and no test started beside it (the harness and `-GpuCheck` held). The measuring build was taken out of the
  game folder while the run waited (a wait of unknown length is "left in the game").
- THE HARNESS: `-GpuCheck` now also exits 1 while a live process holds `artifacts/harness/RUNNING` (a run of the game
  that waits for the card: the docs said workers hold then, the check did not know it; two workers' chains of tests
  kept the camp's run waiting). A queue's script writes that file with its own process number before its wait
  (`scratchpad\run_measure_camp3.ps1` is the pattern: wait, THEN deploy, run, put main's build back). A wait ended by the
  STOP file gives the lock back. `Build-PtShaders.ps1` starts its compilers BELOW NORMAL priority (three workers'
  compilers and Starfield stood at 90 % of the CPU); workers' copies carry the old script, so
  `scratchpad\low_priority_watch.ps1` lowers their compilers until 08:00.
- PT-131: campspeed's run of 01:27 gave steps 3 and 4 and then hung (below); its numbers: the water's path not run
  30.8 ms; run without the sun through the waves 40.2 ms; everything 346 to 366. (Read at first as "700 paths a frame
  reach a far sea": WRONG, those lines were from the save's place before the walk. The second run, `20261012-021107`,
  02:11 to 02:16, clean: AT THE CAMP 825,000 pixels' reflection rays and 216,000 bounce rays a frame enter the water's
  path, 23 m under the camera on the sea's level, with no water in view; 1.2 M questions for the sun through the waves,
  nine in ten for nothing. PT-131 has the lines. Whether those rays leak through the ground is the first question now.)
  A FRESH Opus worker `campfix` (brief-campfix.md; it goes on in campspeed's copy; the
  old agent's history was 3 MB: no follow-up to it) makes the fix as a build for players with one hidden switch, and
  says why the water's path still costs 9 ms for 700 paths. The measuring recipe was reordered (`camp-measure-b`:
  steps 2, 7, 5, 6) and started 02:11; its numbers go to campfix by a short message.
- PT-125 CAME BACK TWICE (22:49 and 01:49, walks into the camp, the scene stage; PT-REQUESTS has the lines). The marker
  could not name the step; it can now (main FF1C2D52B16B, built and installed 02:11; in the game folder once the
  measuring run has put main's build back). Next: a recipe of walks to make it happen again.
- WORKERS: fogrule, nosun, campfix (Opus). Codex is out until 10-16.

## State at 01:45 (10-12)

- THE BASELINE (`20261012-011822`, PT-99's first lines): the tavern 21.0 fps (tracer 36.0 ms), the town 11.3 (65.0), the
  forest 11.4 (68.1), the vista 13.3 (60.1), the beach 6.7 (131.9). A frame less the tracer's GPU time is 12 to 23 ms:
  a speed item of its own, not split yet.
- campspeed HANDED OVER its measuring build (01:25; `%TEMP%\tgb\campspeed-measure`, SHA B386C2E7...; recipe `camp-measure`
  in main now: the water's path left out, no sun through the waves, no patch of waves made at a point, the shadow rays
  first, and a candidate fix on trial; counters "Special branches 1s" in the log). The run was started 01:40
  (`scratchpad\run_measure_camp2.ps1`: deploys it, runs `forest@camp-measure -TimeoutMinutes 45`, puts main's build back
  and hash-checks). ITS NUMBERS GO BACK TO campspeed (SendMessage: the stage times and two or three counter lines a step).
  Its reading of the code: with the Caustics row on every scatter point in the water asks for the sun through the waves;
  beyond about 60 m of the camera a patch of waves is made at the point (up to 40 rounds of 33 x 33 corners, 15 texture
  reads each), and 32 threads wait for the one that draws the big patch.
- THE HARNESS waits for a running offline test whatever GPU it uses (a test making its passes showed no GPU use: a run
  began beside one at 01:19).
- WORKERS: fogrule and nosun (Opus) run; ctxcondense (Codex: PROJECT-CONTEXT under its budget, a docs job) runs;
  campspeed waits for its numbers.

## State at 01:25 (10-12): the owner asleep, the work goes on

- THE OWNER, 01:12: "I'll go to bed now. Please keep working on all these things". So: no question waits on the owner
  tonight; choices that are the owner's are collected for the morning ("FOR THE OWNER" below); `-Measure` runs may take
  the screen; Starfield is closed (the harness and `-GpuCheck` still check every time).
- IN THE GAME (hash-checked 00:45): native 3199B2F4BBF6, plugin A2311F0DA2C5 = main (psr-2 and fogfix merged). SEEN IN
  THE GAME ON IT: the fog grid right in the tavern (the old route's light, half its flicker and boil under NRD, +0.7 ms:
  `20261012-004519`, `-005626`); the mirror under NRD (detail 2.53: `-005626`). The grid is still behind its hidden
  switch: OUTDOORS THE TWO FOG ROUTES DISAGREE ON THE LIGHT (`20261012-010243`: vista the grid 7 % under, fog the old
  route 30 % over the game's own and the grid on it, night the same and the grid four to ten times calmer).
- THREE OPUS SUBAGENTS RUN (each ends its turn with a build for the coordinator's game run, or with its report):
  campspeed (PT-131: finer measuring switches for the camp; copy `campspeed`, base `base20f`), fogrule (PT-118: which
  fog route is wrong outdoors, the fix, then the grid the rule and its cleanup; copy `fogrule`, base `base20g`), nosun
  (PT-133: the 6.6 pass set against the 6.9 set without a sun; PT-129's loops; copy `nosun`, base `base20h`).
  Merging: `python tools/agents/merge_copy.py <copy> --base=<base>`, never during a build; read tracer.cpp's hunks.
- THE COORDINATOR'S OWN LIST FOR THE NIGHT: a clean speed baseline of the merged build (`speed-now -Measure`: the tavern,
  the town, the forest, the beach); `mega-plants`; the whole GPU test once (the card is free; PT-113's 44 known
  failures are the baseline); PROJECT-CONTEXT condensed (950 lines for a budget of 700); the workers' builds as they come.
- FOR THE OWNER, in the morning: (1) glass and water under NRD: A, B or C (`docs/agents/psr-2.md` (b); A breaks no
  ruling: two layers through NRD, about +2.5 ms and +400 MB when glass or water is on screen); (2) RELAX or REBLUR;
  (3) may runs with Sunshine at 4 to 5 % be quoted for speed (an A-B-A inside one run); (4) may offline tests run
  beside Starfield again, or never (the rule now: never); (5) a save at the forest camp ("PT forest-camp") would make
  PT-131 a 3-minute loop; (6) the sky's Q1 to Q3; RTXDI's parameter departures (review 22).

## State at 00:50 (10-12)

- IN THE GAME (hash-checked 00:45): native 3199B2F4BBF6, plugin A2311F0DA2C5 (= `build\native` = `artifacts\Native`): main
  with psr-2 (mirrors by primary surface replacement, the rule under NRD) AND fogfix merged (00:36; two conflicts in the
  flag bits resolved by hand; 117 source checks pass). NEITHER MERGE HAS BEEN SEEN IN THE GAME YET on this build: the
  tavern's run of the fog grid was started 00:46 (`tavern-room@fog-froxel-numbers`, look-only), the mirror scene is next.
- THE WHITE FOG GRID'S CAUSE (PT-118, PT-133), found by fogfix, reproduced offline: the tavern's frame has no sun, and the
  cells' Shader Model 6.9 pass returned garbage for a sum that is zero on the branch that skips the sun's code; cured by
  the pass's shape ("KEEP THIS SHAPE"), four no-sun cases in the check. With it in main: the split near lamps (the smooth
  part in the cells, the rest per pixel: `fog_lamps_residual`), the trial number in flag bits 24-25 (the hidden `Traced fog:
  froxel cache trial`: 0 one sample a cell and the fast walk, 1 the first trial's walk, 2 Remix's reuse and filter), and
  TEMP DIAG: the numbers' read-back ("Fog grid numbers (TEMP DIAG)" a second in the
  log) and the hidden `Traced fog: froxel cache leave out` (bits 26-29). The TEMP DIAG parts come out once the game has
  answered (a fresh worker's small job, or the coordinator's).
- AN INCIDENT, 00:20:12: the GPU's driver was reset (LiveKernelEvent 141, nvlddmkm 153) while fogfix's offline test
  ran beside the owner's Starfield, and Starfield went down. The owner: "Yeah regarding the GPU reset, Starfield was
  playing while you were doing your test, and the GPU couldnt handle it". SINCE THEN: no offline GPU test while a game
  of the owner's is open (`Run-PtHarness.ps1 -GpuCheck` exits 1 then; AGENTS.md "Subagents and reviews"); the worker had
  taken its own wait for Starfield out: no worker may. The run that hung: fogfix's `m-many` (its report lists the
  switches: not to be repeated without care; it was 600 times slower from its first frame: a hang of ours to find).
- THE OWNER, ~00:28: "btw, the fog right now - is that the RTX Remix fog? Because it's still super noisy". Answered: no,
  the game still runs the old per-pixel fog; the grid is behind its hidden switch until it is right in the tavern.
- ONE OPUS SUBAGENT RUNS: campspeed (PT-131; told of the new rule). fogfix, psr-2 and ommout are done and merged.

## State at 00:35 (10-12)

- THE WORKER psr-2 IS MERGED INTO MAIN (00:25; four conflicts in notes and recipes resolved by hand; 117 source checks
  pass; the build of all 57 passes was started 00:27 with `scratchpad\build_deploy.ps1`, which deploys and hash-checks at
  its end): mirrors by primary surface replacement are THE RULE under NRD (no setting, no flag; bit 22 free), with the
  offline check `TGPT_TEST_PSR_ONLY`. NOT merged: Ray Reconstruction's two guide changes (psr-2's decision: review 3's
  gives up the steady hair guides; its report's section (c) says how to A/B them). Owed on that build:
  `mirror-tavern@psr-mirror` (the recipe is in main now), the tavern's speed (`tavern-room@speed-now -Measure`: 36.45 ms
  before), psr-2's mutant check (`tools\psr\run-check.ps1 -Runs B:0,M`, 20 minutes of a free card).
- FOR THE OWNER TO RULE (psr-2's report `docs/agents/psr-2.md`, section (b)): glass and water under NRD. A: two layers,
  both through NRD (reflection and view-through each with guides of their own; about +2.5 ms and +400 MB when glass or
  water is on screen; no ruling broken; our own reflection filter goes). B: one layer through NRD, the other as traced.
  C: as NVIDIA's own sample, no denoiser on glass, its light looked up in the denoised frame (breaks "no screen space
  stuff"). psr-2 would start with glass, way B, as the small next step.
- THE TOWN'S WALK for RTX Mega Geometry (`20261012-000233`, clean): NVIDIA's layout 6 % slower than plain (PT-109).
- TWO OPUS SUBAGENTS RUN: fogfix (asked for a build that logs numbers), campspeed (finer measuring switches for PT-131).

## State at 00:15 (10-12)

- IN THE GAME (hash-checked 23:54): native 49CD9C55E935, plugin E411BF1A2884 (= `build\native` = `artifacts\Native`); the
  harness plugin rebuilt 00:00 (a running step stays frozen until the passes are made). No config backup pending.
- THE OWNER, 23:50, of the harness's running steps: "Because you are keeping the game simulation active while you compile
  shaders, the time of day has also shifted to the middle of the night at the point of time where the PT shaders are
  compiled." and "That makes comparison with the vanilla frames flawed, as they're two different times of day". Right:
  16:35 became 21:38 and 01:23 in the two camp runs. Fixed in `Runner.cs` (the world is let go, and the walk started,
  only after the passes are made), checked in run `20261011-235717`; `docs/PT-HARNESS.md` says what is left (a running
  step moves the clock 25 to 45 game minutes) and that a new build's run takes `-TimeoutMinutes 30`.
- PT-131, THE CAMP: the measuring build's run (`20261011-233418`): the cost is in the two special branches of path
  shading, glass followed and the water's own path (31 ms without them, 346 with), only with the Caustics row on.
  Opus worker campspeed has it now (`brief-campspeed.md`, copy `wt\campspeed`, base `base20f`).
- PT-118, THE WHITE FOG GRID: fogfix's build is still white in the tavern (`20261011-231804`); it was told so and asked
  for numbers from the game (a readback in the log) in its next build.
- THE GROUND RAY in `water_medium`: the tavern costs the same with it (tracer 36.45 ms, `20261011-230052`, clean), a
  water scene's light is the same (luma 56.0 / 61.7 as at 18:15). It stays; PT-131 says what gap it closes and opens.
- THREE OPUS SUBAGENTS RUN: fogfix, psr-2, campspeed. Each ends its turn with a build for the coordinator's game run.
- OWED: `metal@mega-walk`, `mega-plants`; NRD's history confidence on and off in a water scene (it has no switch any
  more: a build without it for one run, or a look by the owner); the whole GPU test once things look right.

## State at 23:10 (10-11)

- IN THE GAME (hash-checked 22:59): native 49CD9C55E935, plugin E411BF1A2884 (= `build\native` = `artifacts\Native`).
  Since 22:00 in main: the worker ommout merged (the characters' micromaps trial is OUT, M3 stays; five small conflicts
  resolved by hand; flag bit 20 free), `water_medium`'s second ray against the ground (PT-131: built on a theory the game
  did not confirm; TO DECIDE, see there), the harness no longer ends a run for Sunshine alone (AGENTS.md), PT-132 entered
  (the traced fog loses ranged lamps' and glowing surfaces' light on long rays: fogfix's find). Source checks pass (116).
- PT-131, THE CAMP AT 3 FPS: the Caustics row decides it (366 ms on, 32 ms off; the same picture), the coordinator's
  "camera in the water" theory was WRONG (its fix changed nothing: `20261011-223913`). The measuring build
  (`%TEMP%\tgb\measure-build`, CMake `TGPT_SPEED_MEASURE`, `scratchpad\measure_build.cmd`) was being built at 23:00; then:
  deploy it with `Deploy-PtBuild.ps1 -Native -NativeFrom <its dll>`, run `forest@camp-measure -TimeoutMinutes 30`, put
  main's build back (`-NativeFrom build\native\TGPathTracing.Native.dll`). A MEASURING BUILD IS NEVER LEFT IN THE GAME.
- THE OWNER, 22:50, watching a harness run: "It has been stuck at the '6 meshes to go' forever now": that run stood at
  the camp at 3 frames a second and was stopped at the harness's 16 minutes; the owner's settings were put back by hand
  (`artifacts\harness\_config-backup` is gone again). A run whose build is new needs about 7 minutes for its passes
  first: give such a run `-TimeoutMinutes 30`.
- TWO OPUS SUBAGENTS RUN: fogfix (found: the offline "over-count" was the OLD route losing ranged lamps' light, PT-132;
  the grid mis-counts light concentrated in a cell by 5 to 31 %; the white tavern not reproduced offline yet) and psr-2
  (its final sources in the copy, its build waiting behind the coordinator's). ommout is done and merged.
- OWED TO THE GAME, in this order: `forest@camp-measure` (measuring build); `tavern-room@speed-now -Measure` and a water
  scene on main's build (does the ground ray cost or change anything: keep or out); fogfix's and psr-2's builds;
  `metal@mega-walk`, `mega-plants`.

## State at 22:00 (10-11)

- IN THE GAME (hash-checked 21:52): native DF0395575260, plugin 3401A307FFF1 (= `build\native` = `artifacts\Native`).
  Since 21:10 in main: megageo-6 merged, the descriptor heap's slot guard, NRD's history confidence the rule, the
  caustics before the passes that read them (PT-130), the fog grid's TEMP DIAG switches (they come out with fogfix).
  Source checks pass (116). Looked at: `20261011-215257` (the tavern NRD: flicker 0.26, boil 0.14; a water scene: right).
- THE OWNER, ~21:40: "you can up to 3 Opus 5.5 xhigh subagents at a time again, the old restriction has been lifted"
  (AGENTS.md "Subagents and reviews").
- THREE OPUS SUBAGENTS RUN (started 21:20 to 21:47; each writes `docs/agents/<name>.md` in its copy under
  `%TEMP%\tg-agents\wt`): fogfix (copy `fogfix`, base `base20e`, `brief-fogfix.md`: why the fog grid over-counts lamps,
  a fix with no clamp, the check a comparison, TEMP DIAG out), ommout (copy `ommout`, base `base20e`, `brief-ommout.md`:
  the characters' micromaps trial out, M3 stays), psr-2 (copy `psr`, base `base20pc`, `brief-psr-2.md`: mirrors' primary
  surface replacement to the rule, the design for glass and water for the owner). No Codex worker runs.
  MERGING THEM: main moved since their bases (tracer.cpp: the caustics' block moved up and the marks 3 to 9 renumbered,
  the confidence switch gone; `gpu.hpp`; Settings, HdrpPasses, NativeTracer; scenarios.json): three-way with
  `merge_copy.py <copy> --base=<base>`, read every hunk of tracer.cpp.
- WHAT THE GAME ANSWERED TONIGHT (all in PT-REQUESTS): the fog grid white, traced to lamps (PT-118); the characters'
  micromaps no gain (PT-110); caustcut's cut no gain, dropped, its report kept (`docs/agents/caustcut.md`; PT-114); mirrors
  by primary surface replacement work (PT-127); history confidence the rule; the forest's hang gone, NVIDIA's layout no
  faster (PT-109); A CAMP IN THE FOREST AT 3 FPS, reflections 213 to 235 ms (PT-131, new, the next speed job).
- NEXT, in this order: (1) PT-131: a recipe that walks to the camp and stands (the `mega-walk` walk, then frozen steps:
  Caustics off, `Micromaps trial` 8, then the measuring build `TGPT_SPEED_MEASURE` with the recipe `measure`'s switches);
  (2) the three workers' results: merge, build, one scene each (`tavern-room@fog-froxel-b`; `metal@speed-now` after ommout;
  `mirror-tavern` with psr-2's recipe); (3) `metal@mega-walk`, `mega-plants`; (4) the owner's open choices: RELAX or REBLUR,
  Sunshine in speed runs, the sky's Q1-Q3, RTXDI's parameters, and psr-2's design for glass and water when it is written.
- THE HARNESS: every walk of `20261011-212136` was flagged "Real-time walk did not start on the next rendered frame" (at
  3 to 5 fps): its frame times are rough; look at the check before the next walk run.
- A flickering bright line at the screen's lower edge with Caustics on in `water-torch` (`20261011-215257`, the change
  map): not looked into; may be old.

## State at 21:10 (10-11), after Claude's weekly limit (19:15 to ~20:40; the owner bought usage)

- THE THREE RUNS OF 19:00 (build native 6C5493C0AA1C, plugin 89E905494F92; nobody else on the card, the game in front):
  - `20261011-185528` (-Measure). The town, the characters' micromaps (PT-110 M4): a frame 87.69 / 87.23 / 86.97 ms (off,
    ON, off), the tracer 67.81 / 68.17 / 67.40: NO GAIN. With the tavern's 36.20 / 36.25 / 36.20 of 17:27: the trial comes
    out (taken out by worker ommout, `docs/agents/ommout.md`: `scene.cpp`, `pt_omm_bake.hlsl`, the setting, flag
    bit 20, the recipe; M3 stays).
    The tavern, the fog grid with its continuation (PT-118): THE WHOLE PICTURE WHITE (luma 255, 100 % near white, the
    meter's median at its top), with NRD and with no denoiser. The grid of 16:48 (parts a and b) was right.
  - `20261011-190338`, NRD's history confidence (PT-127): the same picture, denoising 2.83 -> 3.29 ms, boil 0.29 against
    0.25 / 0.26, flicker 0.49 against 0.47 / 0.45 (the candles do flicker). NOT DECIDED: NVIDIA calls it "optional, but
    highly recommended"; rule or out is owed today.
- THE WHITE GRID, what is known: not a stale build (headers newer than sources; main's fog files equal the worker's);
  `20261011-204941` (`fog-froxel-why`): white with the fog's cache on and off, with Fill Light Fog on and off; the offline
  check `TGPT_TEST_FOG_FROXEL_ONLY` gives the grid the old route's light in every case tried (plain 0.07555 / 0.07551, boxes
  crossing 0.07555 / 0.07550, the mist's cache 0.09846 / 0.09836, the sun 16,000 times brighter with the exposure as much
  smaller 0.000312 / 0.000312, both 0.000467 / 0.000470; new switches TGPT_TEST_FOG_CACHE, TGPT_TEST_FOG_SCALE). That check
  only asked for a finite picture: IT MUST COMPARE THE TWO ROUTES. The composite drops a fog value that is not finite, so
  the grid holds large finite light. Next: ONE game run that leaves the continuation's parts out one by one: a TEMPORARY
  hidden `Fog grid diagnosis` (frame flags bits 24 to 27, "TEMP DIAG" in `fog_froxel.inl`, `pt_fog_froxel/filter/integrate`,
  Settings, HdrpPasses), recipe `fog-froxel-diag` (8 the cells dark, 9 the grid not read, 10 no tail, 11 no excess added,
  2 no excess split, 3 no filter, 1 one sample, 7 no old proposal, 4 no tile, 5 no homogeneous path, 6 boxes everywhere).
  ALL OF IT COMES OUT when the cause is found.
- WORKERS: megageo-6 (Claude) stopped at the limit with its plan alone; the SAME copy and brief went to a Codex worker at
  ~20:50 (`note-megageo-6-codex.md`). psr (Codex) stopped at 20:04 for the game's answer: mirrors' primary surface
  replacement for NRD and RR's guides (hair one surface, glass's reflection distance) behind bit 22; glass and water NOT
  done (its report says why: two delta branches need two full layers); native DLL `%TEMP%\tgb\psr-build` (SHA 7DD2E5E3...),
  NO plugin built; it grows `pt_specular`, so it stays in its copy until the game answers (deploy from the copy:
  `<copy>\tools\harness\Deploy-PtBuild.ps1 -Native -NativeFrom <dll>`), runs `mirror-tavern@psr-mirror`, `glass-jars@psr`,
  `water-day@psr`. caustcut (Codex) finished 20:08: an exact cut (a tile's corners whose shadow ray is proved to cross
  one solid triangle skip the ray; a separate pass, the ordinary one unchanged), bit 23; offline NO gain (2.20 / 2.23 ms);
  DLL `%TEMP%\tgb\caustcut-build` (SHA 377AE526...), plugin 77950F2A...; the game owes `tavern-room@caustics-bound -Measure`.
  It found: the bounced light and the reflections run BEFORE the caustics are rebuilt, so the water's paths read the frame
  before's caustics (its PT-130, to enter in main).

## State at 19:00 (10-11)

- In the game (hash-checked): native 6C5493C0AA1C, plugin 89E905494F92 (= build\native = artifacts\Native). Since 17:40:
  the reflections' sort chooser gives no verdict across a scene change (PT-129's lead), the fog boxes' sizes and
  densities logged, RR asks for NVIDIA's Default preset (= F in 310.9.1; the owner's ruling ~18:10; TG Better Upscaling
  not deployed), megageo-5 merged (Mega Geometry trial 5 with micromaps in clusters, 7 NVIDIA's layout, 8; recipes
  `mega-plants`, `mega-walk`), nrdconf merged (NRD history confidence for the direct light, hidden `NRD lighting
  confidence`, frame flag bit 21, off; recipe `nrd-confidence`; its worker's .NET build ran workload advertising: future
  dotnet calls set DOTNET_CLI_WORKLOAD_UPDATE_NOTIFY_DISABLE=true and MSBuildEnableWorkloadResolver=false).
- The owner's crash (PT-129: a GPU hang in the reflections after a second load) not reproduced in four loads; the owner:
  "nevermind trying to reproduce that crash, just keep going with the other stuff".
- Run 20261011-182915 (`mega-walk`, -Measure): trial 7 walks slower than plain (the town 140 / 149 ms a frame against 127 /
  126, the tracer's GPU time the same) and HUNG THE GPU in the forest's scene stage. Claude worker megageo-6 (wt\megageo6,
  base20mg6; brief-megageo-6.md): the hang, then where the frame time goes against NVIDIA's own per-frame work.
- Queued (waits for Starfield to close): `tavern-room@fog-froxel-b,metal@micromaps-characters -Measure`, then
  `tavern-room@nrd-confidence`. The harness keeps one recipe per scene in a run (docs/PT-HARNESS.md).
- Codex workers started ~19:00 (copies `wt\psr`, `wt\caustcut`, base `base20pc`; Starfield open, so no game runs): psr
  (`brief-psr.md`: NRD's primary surface replacement for mirrors, glass and water, RR 3 and 20; frame flag bit 22,
  recipe `psr`) and caustcut (`brief-caustcut.md`: PT-114, the caustics' 3.9 ms in rooms by an exact bound; bit 23,
  recipe `caustics-bound`). With megageo-6: three workers.
- Source checks pass (116). Uncommitted since d31dff9.

## State at 17:40 (10-11), the usage limit reached

- In the game (hash-checked): native 174195A8959D, plugin D8423AD03CDA (= build\native = artifacts\Native). Holds: NRD
  as NVIDIA documents it (RELAX; REBLUR on `NRD trial` 2), the fair gain meter, the reflections' own filter on glass and
  water only under NRD (denoising 4.4 -> 2.8 ms), PT-128 fixed (the sort chooser times the reflections), the micromap
  baker's tighter test (check passes), the bounce's walk-skip, the RR/RRG fixes' TG Path Tracing half (TG Better
  Upscaling's half NOT deployed: deploy it with the RR test), fog-froxel parts (a) + (b) + continuation (hidden `Traced
  fog: froxel cache`, off; its integration 10 -> 0.55 ms in the worker's check: NOT yet run in the game), megageo-4's
  cluster trial (Mega Geometry trial 4-6), omm-chars (the solid-marks fix as rule; `Character micromaps (trial)` off).
- Next, first: one clean `-Measure` run `tavern-room@fog-froxel-b,metal@micromaps-characters` (the fog's cost and look;
  the character maps' ~2 ms in the town seen with Sunshine on the GPU, run 20261011-173231: must be clean to count).
  The owner, 17:40: "Sunshine practically always runs": the rule that a run with Sunshine's 4-5 % is not quoted for
  speed needs the owner's word on what to do instead (A-B-A steps in one run, as 173231's 70.5 / 68.6 / 70.8, may be
  the way); ask first thing.
  Then by the trial rule: the character maps become the rule or come out; the fog grid to the owner's eyes.
- Running: Claude worker megageo-5 (wt\megageo5, base base20mg5; brief-megageo-5.md): RTX Mega Geometry the NVIDIA way
  (micromaps in clusters via NVAPI's OMM arrays, a cluster BLAS per plant copy in the partitioned top level, a walking
  recipe). The owner (~17:05) wants it kept and done right; the cluster code stays.
- The owner's open choices: RELAX or REBLUR (hidden `NRD trial`); RR preset F vs NVIDIA's Default; RTXDI's departures
  from its defaults (PT-127's list).
- Queued: NRD's history confidence (gradients), primary surface replacement for mirrors and water, RR 3/17/20, AMD 7,
  RTXDI 21; the caustics' 3.9 ms in rooms (PT-114). BACKLOG is ~13 KB (trim owed); PROJECT-CONTEXT over budget.
- Source checks pass (116). The whole GPU test not run since the NRD rewrite (its PT-19/PT-75 NRD checks describe the
  old route). Uncommitted since d31dff9.

## State at 16:00 (10-11)

- In the game (hash-checked): native C986D0E687AC, plugin 0B9278457FE7 (= build\native = artifacts\Native). In it besides
  NRD: the sky merged (hidden `Sky trial`, 0), the fog's closed-form inverse, the caustics before the denoisers.
- Workers running: vendor-rr (Codex, copy wt\vendorrr, base base20rr), fog-froxel (Codex, wt\fogfroxel, base20fog),
  megageo-4 (Claude, wt\megageo4, base20mg4). Merge each with `tools\agents\merge_copy.py` against its base.
- Queued: NRD's history confidence from lighting gradients (README: "highly recommended"; RTXDI 3.1 has no helper),
  primary surface replacement for mirrors and water (PT-127), the reflections' own filter only where glass and water
  need it (~1.5 ms), the caustics' 4.8 ms in the tavern (PT-114), RR 3/17/20, AMD 5/7, RTXDI 21/22 (the owner's choices).
- Source checks pass (116). The whole GPU test not run (its NRD checks of PT-19/PT-75 describe the old route: expect
  FAILs there). Uncommitted since d31dff9.

## State at 11:05 (10-11), the usage limit reached

- In the game: plugin 048382CCF8E0, native 135C9E3502D9 (fog boxes, 3D texture sharing, HDRP's lighting off while
  tracing). Built since, NOT deployed: equiangular lamps in fog, HDRP's fog / Fog Adaptation removed, the reflections'
  hint probe backing off. Its full shader compile was running (`Build-PtShaders -First`, log in the session scratchpad
  `shaders-eq.log`, 3 of 45 at 11:03); then `cmake --build build\native --target TGPathTracing.Native`, Deploy-PtBuild
  -Native, install, hash, and the run `night@traced-fog,metal@speed-now,forest@speed-now -Measure` (say first it takes the
  owner's screen). The GPU test file was edited (Fog Adaptation's and the mirror-in-HDRP-fog checks removed): compile it.
- Next code (prepared, not applied): the reflections fetch their hit after the regrouping (review finding 1, PT-117):
  move `QueryHit` and `query_from_surface` from `pt_gi.hlsl` into a header both passes include, and in `pt_specular.hlsl`
  query, key by the met material, `MaybeReorderThread`, then `fetch_hit`. Then the review's small ones (water queries
  without the micromap flag, the fog's closed-form height inverse) and the plants' rebuild-vs-refit trial.
- sky-2 stopped at the hand-off: `%TEMP%\tg-agents\wt\sky2\docs\agents\sky-2.md` "State when stopped" (written, not
  compiled natively; its shader build log `%TEMP%\tgb\sky2-shaders.log`). A fresh agent finishes it from that report.
- Source checks pass (116). Uncommitted since d31dff9.

## State at 07:20 (10-11)

- In the game (hash-checked): plugin A3F635C34717, native DB9225B460BF (= `build\native` = `artifacts\Native`). In it:
  step A the rule, the motion fixes, fog trial 5 (off), plants' micromaps and leaves opaque but to light rays, leaves'
  micromaps at level 6, the bounced light's material key, the decals' tree by place, the water question skipped above
  every slot's water. ABI 0x00012700; FrameConstants 8,080 bytes.
- Tonight's speed, the world running, no denoiser: tavern 32 -> 30 ms of the tracer, town 64 -> 57, forest 83 -> 74.
  A frame of the town is ~78 ms (12.8 fps): the tracer 57 + the game's own card time ~21.
- The audit the owner asked for: `docs/agents/audit-4.md` (RTXDI: used right but R3; reordering: the bounced light had
  no key, now keyed; micromaps: right in form, too coarse for leaves, now level 6; Mega Geometry: not used, by the
  owner's order, its target 4.7 ms; plus the other bottlenecks found).
- New entries: PT-113 (44 GPU-test FAILs, the same on round 19's build), PT-114 (caustics in rooms), PT-115 (rough
  highlights from the bounced light's sample: a proposal, ~8 ms of a town). PT-107: the game's own time before our
  composite grows by ~4 ms with the world running.
- Source checks pass (116). No GPU test since 03:00 (the owner's rule: the game first). Uncommitted since d31dff9. No
  subagent. Starfield opened at 02:04, 03:53, 05:53 and 06:46 for a minute or two each (another session's tests, it
  seems: its tools ran at 06:25); the harness waited each time.
- PROJECT-CONTEXT is over its ~700 lines (about 915): condensing is owed.

## PT-127 worker addendum, vendor-rr, 2026-10-11

Isolated `vendorrr` copy; read-only main tree and SDKs. RR 2/4/18/19 and AMD 6/14/15/28 implemented against the
vendor guides. The per-fix screen effects, size-query hook, CPU context creation, memory cost and build evidence
are in `docs/agents/vendor-rr.md`. Preset F unchanged. Both managed/native modules, 46 shader variants and 116 source
checks pass (16:04). No GPU tests, game run, install, deployment or commit. Coordinator must integrate the
source changes with its NRD work and run RR in game; RRG needs supported AMD hardware. Excluded findings stay open.

## Worker addendum: megageo-6, 2026-10-11 evening (own copy only)

Codex resumed the stopped Claude worker in megageo6. Its report had not recorded unfinished edits to the cluster
builder: scene.cpp still called the old signature. Audited against NVAPI R615, finished the size-class bounds fix,
validated real allocation ranges/counts before submission, reused immutable GPU arguments, and removed four driver
allocation-size queries per group per frame made only for logs. No shader, public ABI, option or flag change.
Read `docs/agents/megageo-6.md` for the old walk's CPU/GPU separation, exact source delta, DLL hashes and test state.

Native and focused test build; all 116 source checks pass. The functional growth case reached 8,887 clusters and
1,566 plant BLAS, repeatedly grew/shrank/replaced groups, returned to identical camera first hits, and read back no
oversized result. The final guarded repeat exits 0, with no contention detected (21:04:53-21:05:32). Trial 7's
small-fixture median is 6.76 ms GPU/0.781 ms CPU recording; plain is 6.56/0.667 ms, not a game-speed claim. No main-tree
or game-folder writes, game start, deploy, commit, download or process stop except the worker's own processes.
Forest TDR's exact cause remains unproven until the real walk; implicit BLAS placement and GPU
instance address writes are a separate follow-up, after the smallest safe fix is tried in game.

## Older starter notes (stood under the starter prompt until 10-12 02:30; every job in them is done, merged or out)

Workers `psr` and `psr-2` addendum, 2026-10-11: read `docs/agents/psr-2.md`. PT-127's primary surface replacement
for mirrors is the rule under NRD (no setting, bit 22 free; the game's answer: run 20261011-213531). Next for it:
merge psr-2's files (its section d), then `mirror-tavern@psr-mirror` to see the merged build, then the owner's ruling
on glass and water (its section b). Ray Reconstruction's two guide changes are not in that build: run its section
(c) with the first worker's build before anything of them is merged.

(Newest first, 10-11 17:40: read "State at 17:40" above, run its "Next, first" run, read megageo-5's report in
`%TEMP%\tg-agents\wt\megageo5\docs\agents\megageo-5.md` and merge it with merge_copy.py against base20mg5. The older
prompt below is from the night.)

Worker addendum (2026-10-11, PT-118): part (a)'s tavern run `20261011-161034` reduces NRD fog flicker 0.38 -> 0.21
and boil 0.23 -> 0.12 at near-identical brightness; its timing is invalid. Part (b)'s first checkpoint built
16:35 (native `72436C931A52`): corrected temporal reservoir proposals, a separate distant-tail pass, simpler
constant-density integration and three fog timestamps. Same grid/reach/rays; no lost distant or reflected fog.
Report `docs/agents/fog-froxel.md`; new recipe `tavern-room@fog-froxel-b` (NRD then None, as requested).
116 source checks pass; 6,000 numerical proposal cases preserve mean. GPU check did not render: cold driver
startup twice timed out; a retry stopped its own process immediately when the coordinator's game opened.
No speed reduction measured or claimed. Filter/clamp/directional history still pending; old route stays until
owner acceptance. No deployment, game start or main-tree writes by the worker. Also found PT-128: the reflection
sort chooser reads the direct-light timestamp pair (unfixed). First next action: one clean tavern run of this
checkpoint; compare the SUM of fog cells/integrate/tail plus composite, since composite used to include all fog.
Keep main's current plugin; its flag 19 already selects the trial. The worker's SDK configuration links main's
prebuilt NRD read-only; do not configure the SDK's source through its junction.
Worker addendum (2026-10-11, PT-110 M3/M4): `ommchars` implements solid character submeshes even beside glass,
and the hidden `Character micromaps (trial)` (frame flag 20). The same OMM links repeat through posed builds and
refits. Hair camera alpha has changing mip/anisotropic footprints; the first safe bake may leave mixed hair
entirely unknown. No shader shortcut or camera filtering change. At 17:05 the native/plugin builds and 50 shader
passes were complete; the source check and its mutation passed. `docs/agents/omm-chars.md` has hashes, merge
functions and the pending first `tavern-room@micromaps-characters` run. No game, GPU test, deploy or commit.
GPU pixel checks wait for the coordinator's game verdict; useful hair acceleration remains unproven.
Worker addendum (2026-10-11 evening, PT-110 M4 out): worker `ommout` took the characters' micromaps trial out after
the game's two clean answers (no gain): the setting, frame flag bit 20 (free again), the recipe
`micromaps-characters`, the characters' map code in `scene.cpp`, the bake's hair and character rules. M3's fix (solid
parts beside glass) stays the rule; the "Micromaps trial" 16 stays as the yardstick of M3's offline check.
`docs/agents/ommout.md` has the list, the hashes and what a second try needs. No game, deploy or commit.

Worker addendum (2026-10-11, PT-118): the owner chose Remix's fog cache after seeing the remaining boiling with
NRD. `docs/agents/fog-froxel.md` is the worker handoff: part (a), hidden frame flag 19 and Advanced boolean,
separate grid/integration passes, exact height extinction and existing box tracking, traced tail and secondary
rays. Native/plugin builds and 116 source checks pass. `tavern-room@fog-froxel` is ready; binary hashes in the report.
The worker started no game or GPU test and changed no installed file. Judge the first comparison before part (b)
(reservoir reuse/filtering) or removing the old path. The worker's SDK configure uses the existing NRD library
read-only because NRD's source configure would write through its SDK junction.
Worker addendum (2026-10-11, PT-118, continued ~17:00): the clean tavern run `20261011-164811` confirms
NRD flicker 0.39 -> 0.21 and boil 0.23 -> 0.12 at the same brightness; old fog 3.0 ms, grid integration
9.84-10.54 ms. Found repeated tests of seven boxes at every slice. Cull their misses once per ray and share
the same light texels across each 8x8 pixel group: guarded small GPU case 9.73 -> 0.55 ms, identical raw mean.
The integration-only build is ready in the worker; no game-speed claim. Report `docs/agents/fog-froxel.md`.

Part (b)'s requested remainder is built and GPU-checked (17:33): 1-6 reservoir estimates by stability, Gaussian radius 4-2/sigma 1.2,
and a bounded history plus separate retained bright excess. Separate filter timestamp and memory accounting.
The guard cleared; five objects compiled, both native targets built. Final small-case fog total 9.97 -> 1.07 ms.
The filter costs 0.08 ms. No recorded contention; mean effectively unchanged. Hash/patch state is in the report.
All 116 source checks and the existing 6,000-case proposal oracle pass. The first cold-start GPU run's timings
are invalid (Starfield opened); the worker guard now yields to every owner game and monitors other GPU users.
Subsequent measured repeats were guarded. No deployment, game start or main-tree write by the worker.

Historical Remix 0.4's complete bodies were found and read: its *separate* depth-prefix pass assumes uniform
fog. To obey unchanged convergence, our height/box rays keep their own medium and all 48 samples. The measured
fix removes repeated work. Current-main missing bodies and directional history remain explicit gaps (PT-127).
Next coordinator action: merge the delta against the saved 16:38 checkpoint, use main's current plugin, one
clean `tavern-room@fog-froxel-b -Measure` run. Compare all fog stages, exposure/gain, shafts and motion. Only the
owner can accept the picture; the old route remains until then. PT-128 remains main's separate timing defect.

Read AGENTS.md's START HERE, then this file's sections 9 and 10 and `docs/agents/audit-4.md`. No subagents until the
owner allows them again. First tell the owner, in plain words: tonight's speed (town 64 -> 57 ms of the tracer, forest
83 -> 74), what is the rule now, and the three choices that are theirs: PT-115 (rough highlights from the bounced
light's own sample, ~8 ms a town: a look to judge), the radiance cache's feeding rate (5.6 ms of a town: noise against
speed), and the fog's open choices (tint, lights in the mist; trial 5 is in, off). Then, by the owner's order of work
(speed under control first): the camera pass that forces every triangle through the shader (`pt_primary.hlsli` 226: a
two-step query keeps the same surfaces), marks of our own between HDRP's passes to split the game's 8 to 12 ms before
our composite (PT-107), hair's micromaps (M4: tried and taken out, PT-110; only after its bound says there is
something to win), the caustics in rooms (PT-114). Watch PT-98: run C saw a stall's herbs
brown once after a scene switch (section 10). Condense PROJECT-CONTEXT.

## ▶ Starter prompt (R21)

(Written 10-12 12:30. The round is PAUSED by the owner's word; what the night found is in
`docs/rounds/round-20-overnight.md`, the owner's own report, and every choice of the page is answered.)

Read AGENTS.md's START HERE, BACKLOG's FOCUS, then "State at 12:30 (10-12): PAUSED BY THE OWNER" above (and the
blocks under it only for detail).

THE OWNER PAUSED THE WORK ("pause until I give you to go-ahead to continue working, as I want to save on my tokens
and GPU"). Until the owner says go: start no worker, no game run, no offline GPU test, no long reading. Answer what
the owner asks, shortly. When the owner says go, say in a few plain lines what waits (the OWED list of that block,
in its order) and ask nothing that the block already answers; a run that takes the screen is asked for each time.

Before any run: the hashes (`build\native`, `artifacts\Native\TGPathTracing`, the game's
`BepInEx\TGPathTracing\Native`: 70948346AD5A at 12:14), `Run-PtHarness.ps1 -GpuCheck`, and whether a game of the
owner's is open (Starfield was, from 12:12).

TRAPS OF THIS ROUND: the tracer's log runs on across starts (read from the LAST "extension bootstrap"); a step's
pictures are taken as its walk STARTS (`walk-end.png` is the end); a recipe after `@` holds for the ONE scene it is
written after (`metal@speed-now,forest@speed-now`); a running step moves the game's clock, so light is compared in
frozen steps only; a queued run takes `artifacts/harness/RUNNING` before it waits and a trial build goes in only
once the card is free (`Queue-PtRun.ps1`); never put the STOP file down while a run you want is going (it ends
that run too); the owner plays Starfield on and off at night: every run and every offline test waits, and
`tools\harness\Stop-TestsBesideGame.ps1` must be running while workers may test; a new build's two reordering
passes take the driver 4 to 10 minutes at the first start; three trees compiling at once take hours (a worker that
changes no shader copies main's headers from `build\native\generated\pathtracing`); Windows sweeps `%TEMP%`
(`docs/AGENT-WORKERS.md`); main has no snapshot but the newest worker base, so a merge is not undone: run a trial
DLL in the game first, and keep the DLL of the build before (`%TEMP%\tgb\main-<hash>.dll`) for the A/B; `cmake
--install` of the whole tree puts fresh builds of the bridge and the released mods' natives into `artifacts\Native`;
a path with backslashes written through the Bash tool loses them or turns into control characters (two docs had
to be repaired): scripts through the Write tool only; Codex is out until 10-16 09:04; three Opus agents at most.
NEW ON 10-12: a scene's lamps depend on the scenes loaded before it in the run, and scenes run in `scenarios.json`'s
order (PT-147); a moving step without `"disableMotionBlur": true` is smeared by the game's own motion blur; a
harness run's "exit 1" with "N problems" is a finished run with flagged numbers, not a failed one; when the harness
refuses a queued run (the owner's game opened) `artifacts\harness\RUNNING` stays behind with a dead process's id
(harmless: such a lock is taken over); the scratchpad of a session is gone with it: a tool worth keeping goes into
`tools/harness/`.
