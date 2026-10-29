# Overnight report: 2026-10-12, 01:12 (you went to bed) to 08:50

For the owner. Plain words. The long version with every number is `docs/rounds/round-20.md` and `docs/PT-REQUESTS.md`.

## The short answer

- The forest crash is fixed, and the same bug had been hiding grass and trees from the path tracer.
- The fog is the new one now (RTX Remix's way, in cells). Same light, half to a tenth of the flicker outdoors.
- Water scenes are much faster. The rest is about the same. It is still far too slow to play: 11 to 21 frames a
  second.
- Nothing is committed. The newest build is in the game folder and has been run in the harness. You have not
  looked at any of it yet.

## Then and now

Frames a second, the world running, no denoiser, 1924x1084. "Bedtime" is the run of 01:20, "morning" the run of
06:20.

| Scene | Bedtime | Morning | Why |
|---|---|---|---|
| Tavern | 21.0 | 21.5 | no water there |
| Town | 11.3 | 12.9 | water far off, and 4 ms less CPU work a frame |
| Forest | 11.4 | 11.2 | see below |
| Vista | 13.3 | 18.0 | water in view |
| Beach | 6.7 | 11.2 | water in view |

The forest did not get faster because the old number was of a broken forest: its plants were posed wrongly, which
made them cheap for rays. The real forest costs more. The water speed-up took most of that back.

The fog change came after 06:20. Its own stages cost the same as the old fog outdoors. I could not measure the
whole frame again: your stream started about 08:26, and a running stream slows the two biggest passes by 2 to 3 ms
each. Pictures can be tested while you stream. Frame rates cannot.

| Thing | When you went to bed | Now |
|---|---|---|
| GPU resets in the forest | happening, cause unknown | fixed; 40 test walks, no reset |
| Grass and trees after a walk | gone from the traced picture (nobody had noticed) | back |
| Fog | the old per-pixel fog, "super noisy" (your words); the Remix grid was only a trial and wrong outdoors | the cells are the rule, over the whole distance; the old fog is deleted from the code |
| Dark parts of the vista | crushed (darkest parts 29 of 255, the game's 38) | 38, same as the game |
| "The camp at 3 fps" | open, looked like a real slow spot | it was my test camera flying into a hill; no such place for a player |

## What was done, in order of importance

1. **The crash (PT-125).** After a walk the tracer posed every plant from other plants' data. That reset the GPU
   now and then. It also took grass and trees out of the traced picture. Found with numbered progress marks on the
   GPU, which stay in the code. An offline test guards it now.
2. **Two more bugs of the same kind**, found by reading, not by crashing: mesh decals (a GPU reset, PT-134) and a
   glow's bounced light counted twice or lost (PT-135). Four loops that could run for ever are guarded. None of
   these is proven to be your crash of 10-11.
3. **Fog in cells (PT-118).** Flicker against the old fog, no denoiser: tavern about the same (0.40 to 0.36),
   vista and thick fog half, night a tenth (1.85 to 0.20). Under NRD: tavern and thick fog half, vista a third,
   night a twelfth. The light is the same to 1 %.
4. **One lamp ruined the dark parts (PT-136).** Light is stored relative to the brightest lamp. At the vista one
   lamp is 3,500 times brighter than the next: the open portal at Sagremor's gate, 2 billion candela. Everything
   else sat at the bottom of the number range and dim light got lost. Now no lamp counts for more than 64 times the
   median lamp.
5. **Water speed (PT-99).** The sun through the waves was worked out far finer than the ray asking for it. Now it
   is as fine as the ray is wide. A worker found this morning that its own extra correction makes the water's
   sunlight about 1 % too bright in bands. It is taking the correction out now.
6. **CPU work per frame** cut from 10 to 6 or 7 ms (the GPU waits during it). The picture is byte-for-byte the same.
7. **Test tools.** Runs queue up and wait for the card. A watchdog ends my offline tests the moment Starfield
   opens. A run ends by itself when the GPU stops. The test walk follows the ground.

## Since you got up (08:50 to 09:05)

- **Rooms are 12 % faster.** Inside the tavern the tracer was posing 762 pieces of the sea 470 m below every frame
  and laying caustics on it. Nothing in the room can see that sea. It is left out now while you are in an interior.
  The tavern's frame went from 49.6 to 43.7 ms (20.2 to 22.9 fps) in one run, with the same picture to the decimal.
  Plugin only. In the game folder.

## What is still wrong

- **Speed.** 11 to 21 frames a second. The target is 55. Reflections are the biggest cost in every scene (11 to
  42 ms), then bounced light (8 to 20 ms).
- **The beach's water still looks nothing like the game's** (PT-81): milky and noisy, a dark slab on the
  shallows. I took it apart this morning: the dark slab comes from the Caustics row itself. A hidden switch
  cures it (the shallows then match the game's brightness: 49.6 against 49.9) but covers the sea in white blobs
  under NRD and costs 17 ms. So it needs a real job, not a switch. Also: the green "sky" there is a far mountain
  that our fog paints flat green next to a grey sky; the game hides both in one haze.
- **Traced fog is 30 to 40 % brighter than the game's** in thick fog and at night (PT-37). In thick fog that is
  light bouncing more than once inside the fog, which the game does not have. At night it is something else.
- **Night by a torch looks very different from the game.** Traced: the torch's orange glow fills the mist. The
  game: a cool blue night with a small warm light. I found why: outdoors the game's own fog setting says "lit by
  directional lights only", so in the game no torch ever lights the mist, only the sun or moon. We light it with
  every lamp. The night fog is also very dense (you see 9 m into it) and made dark for that blue moon look. Yours
  to judge (question 11): `artifacts/harness/20261012-081117/night/overview.jpg`.
- **The night scene's darkest parts are too dark**: 9 of 255, the game's 18. Not looked into yet.
- **RTX Mega Geometry** no longer hangs but is still 6 % slower than the plain way. In the code, switched off.
- **The whole GPU test has not been run** on tonight's code. By your rule it waits until the picture looks right.
- Three small new entries, none looked into: PT-139 (a mirror shows lamps as a noisy glow), PT-140 (a log line that
  adds up wrongly), PT-141 (a flickering line at the water's edge with Caustics on).

## Things that went wrong on the PC

- **The disk was nearly full**: 8 GB free at 08:45. Windows' page file grew to 114 GB while several builds ran at
  once, and 145 GB of old worker build folders sat in the temp folder. You said: delete the old ones, and make the
  clean-up script do it. Done: 131 GB deleted, 134 GB free now, and `Clear-PtHarnessRuns.ps1` does it from now
  on. You said: leave the page file. Left.
- **Windows deleted old temp files at 03:28** because the disk was full. It broke the workers' tree copies. They
  were repaired and are guarded now.
- **The GPU reset at 00:20** was my offline test running beside Starfield. The rule since: no offline GPU test
  while a game of yours is open, and the watchdog enforces it.
- **The Ray Reconstruction preset change of 10-11 never compiled** (a missing quote in TG Better Upscaling's native
  code). Fixed in the source. Not built into a release, not deployed.

## What is in the game folder right now

- Native `64257E205B07`, plugin `02C007EB5DF3` (since 10:14). Vortex will call them changed. Do not let it
  revert them.
- Worth a look, in this order: walk through a forest; the fog in the tavern, at the vista and at night; the vista's
  dark parts by day and night; a mirror with Denoising on NRD.

## Running now

- A worker is grouping the water's 1,400 to 1,800 pieces into a few dozen structures (about 2.5 ms of CPU a frame).
- A worker is taking its 1 % water correction out.
- One worker slot is free.

## What I need from you

Each is one line to answer. None blocks tonight's fixes.

1. Glass and water under NRD: way A, B or C (`docs/agents/psr-2.md`, section (b)).
2. NRD's filter: RELAX or REBLUR.
3. ANSWERED 10:55 (you stay on the stream): speed is measured with the stream on from now on.
4. May offline tests ever run beside Starfield again? Today: never.
5. The sky's three questions (PT-112) and the RTXDI settings that differ from NVIDIA's sample.
6. A save named "PT walk" at the start of a clear, straight 150 m path in the forest, and one in the town. From the
   current forest save a straight walk ends inside rock, so I cannot measure walking speed honestly.
7. Show the traced light one frame late? The CPU work would then overlap the GPU's: a town frame of 77 ms would be
   about 60. Not built. It adds a frame of lag to the lighting.
8. A flame's reach that does not flicker with its light. The light grid is rebuilt every frame because of it.
9. The portal at Sagremor's gate: the game shows its 2-billion-candela light only within 64 m of the camera. We
   trace it from any distance. Follow the game's fade, or keep tracing it?
10. WITHDRAWN (you stay on the stream). What I need instead: your screen for about 12 minutes for a new speed
    baseline (the game must be in front), and about 35 minutes once for a cost breakdown of the three big passes.
    Say when, or say "at night" and I do both while you sleep.
11. Outdoors, should lamps light the mist? The game says no there (sun and moon only). Today we say yes, which
    makes the night orange. On 10-06 you said yes to "shadows and light shafts in the mist". The switch is built
    (hidden: `Traced fog: lamps light the mist outdoors`). The three nights side by side:
    `artifacts/harness/20261012-093855/night/overview.jpg`. Neither looks like the game yet: with the lamps out
    of the mist the orange is gone but the night is half as bright as the game's, because the blue moon haze is
    missing from our fog. That is a bug to find first; a worker is being started on it.
12. Should fog light bounce more than once inside the fog? Physically it does, and in thick white fog that makes
    our picture 30 % brighter than the game's. The game's look was made with one bounce. Same offer: a switch.
