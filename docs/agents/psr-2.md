# psr-2: primary surface replacement becomes the rule for mirrors under NRD (PT-127)

Worker `psr-2`, a fresh agent in the worker `psr`'s copy (`%TEMP%\tg-agents\wt\psr`, pristine base `base20pc`), started
2026-10-11 21:48. Brief: `docs/agents/briefs/brief-psr-2.md` (main tree). The first worker's report: `docs/agents/psr.md`.
All writes stay in this copy and in `%TEMP%\tgb\psr-build`. No game, no deploy, no commit, nothing written to the main tree.

## State when stopped

Stopped by myself on 2026-10-12 at 00:06, the work handed over, no limit hit. Nothing of mine is left running (no test,
no compile, no waiting script).

Done: the rule in the sources, the build, the offline check (it passes: `check-C.log`, 23:32), 117 source checks, the
design for glass and water, the list for Ray Reconstruction, the files to merge.

**Two runs I could not get**, both built and one command away (`tools\psr\run-check.ps1 -Runs B:0,M`, about ten minutes
each with the card free; the runner waits for the game to close and stops its test when the game opens):

1. **The cost's base** (`B`: the base's reflections' pass, nothing of the capture compiled in). Each start of the test
   needs eight minutes for the driver to make the two ray tracing pipelines again, and the coordinator's game opened
   inside those minutes five times between 22:45 and 00:03. So the table under "What it costs" has this build against
   the first worker's build, not against the base. Read `check-B-old0.log`, its "mirrors' cost" lines.
2. **The mutant** (`M`: the normal handed to NRD unmirrored; `variants\M`, built by `tools\psr\mutation_check.py build`).
   The check must FAIL at "the normal NRD is told is the panel's mirrored in the mirror's plane". Until it has, the
   check is a passing test that has not been shown to fail. (It did fail once for a real difference: its first run
   caught its own wrong ray offset in the depth, by 1 to 2 cm.)

## In short

- **The rule.** Where NRD denoises and the tracer fires its own camera rays, a pixel that shows a still pure mirror is
  handed to NRD as the surface the mirror shows. No setting, no frame flag: bit 22 is free again, the plugin's three
  files are the base's bytes again.
- **Ray Reconstruction's two guide changes are NOT in this build.** They were behind the same switch in the first
  worker's build. One of them undoes a thing the tree does on purpose (hair's guides held steady from frame to frame).
  The game has to answer first, and it can today, with the first worker's build. See (c).
- **What changed against the first worker's build**, for cost and for the picture: the record is 64 bytes a pixel (was
  112); nothing is cleared or written per pixel per frame except at mirror pixels; the reflections' thread carries one
  word across its rays (was the whole record and the ray's answer); a glossy metal seen in the mirror is replaced too
  (it stayed blurred).
- **What it costs**: see "What it costs". In short: the first worker's build paid for the trial at every pixel of every
  frame (in the game +3.1 ms of 39.3 in the mirror scene; in the fixture, 1280 x 720, +0.2 ms in the camera pass with
  its switch on, and 0.36 ms in the reflections' pass on a floor that is no mirror even with its switch off). This
  build does not pay those two (camera pass 0.36 ms against 0.56; reflections on a floor with no mirror 1.67 ms
  against 2.03) and takes 127 MB of video memory at 1924 x 1084 whenever NRD runs. What is left against the base's
  pass with nothing of it compiled in, I could not measure (see "State when stopped").
- **The flicker**: not NRD's motion or hit distances. It is the mirror's own highlights of the lamps, which replacement
  leaves as traced. See "The flicker".

## (a) Mirrors: what is built

### Which pixels

A pure mirror: the camera's surface has no roughness and no diffuse colour as its surface record holds them (NRD's
README calls that a delta event; a merely shiny metal is not one), it is not glass, water, skin, an eye or hair, its
shading normal is its triangle's (no bump), and it stands still this frame (its transform is the frame before's; no
character, nothing the wind sways). The harness's mirror is one. In the game's own content almost nothing is: a metal
with a smoothness map below 1 has some roughness. So today this serves the mirror scenes and the owner's rule that a
mirror shows the world as the camera sees it. The surfaces of the game that NVIDIA's rule would reach are smooth glass
and calm water, which are (b).

What the mirror shows must be an ordinary surface: not behind glass, no skin or wax that a walk lights, no hair, nothing
light passes through, and not a pure mirror again. Otherwise the pixel stays the mirror's own, as before. New against the
first worker's build: a glossy metal in the mirror (a tankard, armour) is replaced too. Its own reflection ray's light is
its specular part and that ray's length its specular hit distance, which is what NRD's README asks of the first bounce
after a replaced surface.

### What NRD is told of such a pixel (NRD README, "INTERACTION WITH PRIMARY SURFACE REPLACEMENTS")

| Input | What it holds | Where |
|---|---|---|
| View depth | of the virtual point: on the pixel's own view ray, the reflection ray's length behind the mirror | `pt_psr_capture.hlsli` `psr_begin` |
| Motion | of that virtual point between this frame's and the last frame's camera; the surface's own movement mirrored in the mirror's plane | `psr_begin` |
| Normal | the surface's shading normal, mirrored in the mirror's plane | `psr_finish` |
| Roughness, material class | the surface's own (class 2: no diffuse colour; 0: any other; hair is 1 at camera pixels) | `pt_common.hlsli` `psr_capture` |
| Diffuse light, specular light | the surface's two parts apart, times what the mirror passes, each over NRD's material factor of that surface seen along the mirrored view | `psr_capture`, `pt_nrd_prep.hlsl` |
| Hit distances | from the surface along one direction of its diffuse lobe and one of its specular lobe (rays of their own, random numbers of their own); a glossy metal's own reflection ray's length | `psr_finish` |

Nothing of the traced light changes. The record's two parts and the rest of the pixel's light (what the surface emits,
the fog's own light on the way, the mirror's own highlights, a part NRD's half floats cannot hold) add up to the pixel's
traced light by construction: the rest comes back as traced.

With the records NRD also compares material classes on the whole frame (README: not to mix a metal's diffuse light
with a non-metal's). That was part of what the game saw with the switch on. It is the one thing of the rule that
reaches pixels that are no mirror.

### How it is stored (new)

- `PsrRecord`, 64 bytes a pixel: one row of floats (motion and depth), three rows of packed words (light as half
  floats, normal and state, material). At 1924 x 1084: 127 MB (the first worker's: 223 MB).
- A record counts only in the frame that stamped it (`g.psr.w`, a serial the tracer counts up). So nothing is cleared:
  the camera pass writes one row at mirror pixels and nothing elsewhere (the first worker's build wrote 112 bytes at every
  pixel every frame).
- `pt_nrd_prep` and the confidence filter read one row a pixel to ask "replaced?" and the rest only where it is.
- The reflections' pass (`pt_specular`, the only pass that compiles the capture: `PT_PSR_CAPTURE`) carries one word
  across its rays, the record's slot (`g_psrSlot`), and one float, the fog's transmittance over the reflection ray. The
  depth and motion are written before the surface is shaded; `shade_seen` writes the light into the record as it adds it
  up; the two distance rays run right after the fog, in the mirror pixel alone. (The first worker's build held the
  112-byte record in a global and the reflection ray's answer, 18 words, alive across every ray of the pass.)

### What it does not do yet (each a gap, none hidden)

- A mirror seen in a mirror: the chain of mirrors is not followed. Such a pixel keeps the first mirror's own surface.
- A mirror that moves or is curved (a bumped mirror): not replaced. NRD's README says replacement is exact for flat
  mirrors; a moving mirror needs its previous plane for the motion.
- Hair, skin, see-through things seen in a mirror: not replaced (their light is not split into the two parts).
- The mirror's own highlights of lamps are not NRD's: they come back as traced, and they are the flicker (below).
- Under no denoiser and under Ray Reconstruction there are no records (nothing reads them).
- The record is made whenever NRD runs, mirror on screen or not. See "What I could not do".
- A surface that moves, seen in the mirror: its motion is built (the point's own displacement, mirrored) but no check
  moves a thing in a mirror. The fixture's check moves the camera only.

### The offline check: `TGPT_TEST_PSR_ONLY=1`

`tests/pathtracing_psr_checks.inl`, alone and right after the test's warm-up, run with
`tools\psr\run-check.ps1 -Runs C`. PT-80's mirror floor with the tracer's own camera rays; the panel leans back by 0.25
rad so that a normal that was not mirrored shows. It works out on the CPU, from the two planes, what each mirror pixel
must be handed as, and reads what the tracer handed through a view only this test can ask for (`TGPT_TEST_PSR_VIEW`,
read by `tracer.cpp`; `pt_compose` shows the records, `pt_nrd_prep`'s back end shows the parts of the light).

The new build's runs (`check-C-first.log` at 23:13 and, with the check corrected, `check-C.log` at 23:32: it passes;
1280 x 720, 46,902 and then 48,180 mirror pixels showing the panel's middle; the numbers are the corrected run's where
the two differ):

| Check | Result |
|---|---|
| Replaced | every one; a floor that is no mirror: 0 pixels |
| View depth | the virtual point's, within 6 mm and 0.15 % at all but 57 pixels (a rare far sample of the mirror's lobe moves a pixel's end) |
| Roughness, material class | the panel's, exact |
| Normal | the panel's mirrored in the floor, off by at most 0.0002 |
| Motion (camera 1 cm sideways and 5 mm forward a frame) | under 0.05 pixels off at all but 2 pixels (those by up to 0.45); the virtual point moves 2.6 pixels a frame, the mirror's own surface about 3.9 |
| View depth's change | off by at most 0.03 mm |
| The mirror's throughput | 1, off by at most 0.0005 |
| Hit distances | every pixel has both; 5 % happen to equal a path length (the check allows 10 %) |
| Glossy metal panel | every pixel replaced, all with roughness 0.3 and the class of a surface without diffuse colour, all with a specular hit distance |
| Replaced in each of 8 frames | every pixel; the depth of 200 of 48,180 spreads by more than 5 mm over the frames |

The first run's four FAIL lines were the check's own faults, not the tracer's:

1. View depth: the check started the reflection ray 1.3 mm off the mirror; the tracer starts it 2 to 2.3 cm off
   (`ray_offset`, which a surface that is no character's takes). That moves the virtual point by 1 to 2 cm, more at
   flat angles. Corrected in the check. (The offset itself is a leftover of starting from HDRP's surfaces: see
   "Workarounds met".)
2. "The same depth in every frame" judged the worst pixel: one pixel's depth spread by 11 cm, a rare far sample of the
   mirror's lobe. Now judged by how many pixels spread by more than 5 mm.
3. The glossy metal panel's light with NRD, 0.82 of the undenoised mean: NRD's own loss on a light made of rare bright
   samples (its firefly suppression, on purpose: `nrd_denoiser.cpp`). The first worker's build, which did not replace
   metal: 0.78 with its switch on, 0.79 off. Now judged against that.
4. The sliding camera's detail in its one last frame: with no denoiser that measure came out 0.32 in one run and 1.96
   in another. One frame's bright specks decide it. Printed, no longer judged; the motion is judged directly (above).

The mutant (the normal handed unmirrored: `tools\psr\mutation_check.py build`, its DLL is `variants\M`): built, NOT
RUN (see "State when stopped"). The check must fail at "the normal NRD is told is the panel's mirrored in the
mirror's plane".

### The light: is it the same with and without?

By construction the record's parts and the rest add up to the traced light. What NRD does to the parts is NRD's.
Fixture, the panel's middle in the mirror, means of 32 frames after 64, the scene still:

| | light with NRD, of the undenoised mean | whole frame | detail kept | flicker (undenoised 0.77 to 0.79) |
|---|---|---|---|---|
| First worker's build, switch off | 0.947 | 0.972 | 0.69 | 0.40 |
| First worker's build, switch on | 0.935 | 0.963 | 0.92 | 0.67 |
| This build (the rule), the same pixels | 0.923 | 0.966 | 0.85 | 0.67 |
| This build, the corrected check's pixels | 0.934 | 0.971 | 0.89 | 0.58 (undenoised 0.68) |

So NRD itself takes about 5 % of this light with or without replacement (the same loss the whole test's "NRD mean" check
shows on surfaces seen directly: 0.89 to 0.95; PT-127's open item), and replacement adds 1 to 2 %. In the game the
mirror's region read 11.2 with the switch on against 11.5 and 11.4 off (2 %), the whole frame 15.0 against 15.1.
Where it goes: NRD returned 0.76 of the diffuse part it was handed 0.81 of (in shares of the pixel's light): 5 to 6 % of
that part, its firefly suppression on a light sampled one lamp at a time.

### What it costs

**Video memory**: 64 bytes a pixel whenever NRD denoises with the tracer's own camera rays: 127 MB at 1924 x 1084, 506 MB
at 3840 x 2160. (The first worker's: 223 MB and 887 MB.)

**Time, in the game** (the first worker's build, run 20261011-213531, the tavern's mirror, 1924 x 1084, the harness's
`gpuMs` per step; a look run, not a `-Measure` run: the window was in front throughout and no other program was on the
card): no denoiser 36.4 ms; NRD with the switch off 39.5 and 39.1; **NRD with the switch on 42.4: +3.1 ms**.

**Time, in the fixture** (1280 x 720, NRD, 600 frames each, twice; the reflections' stage and the whole GPU frame):

| | first worker's build, switch off | first worker's build, switch on | this build | the capture not compiled in (base's `pt_specular`) |
|---|---|---|---|---|
| Mirror floor: reflections | 1.92 ms | 2.17 | 1.89 | not run |
| Floor that is no mirror: reflections | 2.03 | 2.03 | 1.67 | not run |
| Glossy metal floor: reflections | 2.20 | 2.18 | 1.93 | not run |
| Camera pass (`prepare`), every scene | 0.35 | 0.56 | 0.36 | not run |
| Whole frame, mirror floor | 6.94 | 7.66 | 7.12 | not run |
| Whole frame, floor that is no mirror | 7.15 | 7.60 | 6.90 | not run |

Read with care: the three runs were half an hour apart (22:38, 22:40, 23:13; `check-A-old4194304.log`,
`check-A-old0.log`, `check-C-first.log`), and other workers' GPU tests were on the card at times that evening. The
runner writes into a log when it saw one beside its run; it learned to at 23:20, so those three logs cannot say. The
corrected run of 23:32 did have one beside it and its first round shows it (reflections 10.6 ms); its second round
read 1.96, 1.73 and 2.00 ms, like the table. What stands out and is larger than the spread between a run's two
rounds (0.02 to 0.03 ms):

- The first worker's switch cost the camera pass 0.2 ms at 1280 x 720 in every scene, mirror or not (it wrote 112 bytes
  at every pixel). That is gone: 0.36 ms against 0.56.
- The first worker's capture, compiled in, cost the reflections' pass about 0.35 ms on a floor that is no mirror even
  with its switch off (2.03 against this build's 1.67). "Code that is switched off still costs frame time", measured.
- In this build the mirror floor costs the reflections' pass about 0.2 ms more than the floor that is no mirror (1.89
  against 1.67), where in the first worker's build with its switch off the mirror floor was the cheaper of the two by
  0.1 ms. So replacement itself costs about 0.3 ms in this view, in which the mirror fills half the picture: two more
  rays and the record in each mirror pixel. (The first worker's switch: 0.25 ms by the same reckoning.)

The column on the right, the base's reflections' pass with nothing of the capture in it, is the number the brief asks
for, and it is the one I do not have: its DLL is built (`variants\B`, FB4FE8CA...), its run was stopped five times by
the game opening (see "State when stopped"). What the table does say: this build's reflections' pass is faster than
the first worker's with its switch off in all three views, by 0.03, 0.36 and 0.27 ms.

## The flicker (0.83 with it on, 0.61 off, 1.10 with no denoiser)

**It is not the replaced surface's motion or hit distances. It is the mirror's own highlights of the lamps**, which
replacement does not hand to NRD: they come back as traced.

How I know, in the fixture (the scene and the camera still, so nothing moves and every guide is constant): the check
shows each part of a replaced pixel's light alone, 32 frames each (`check-C.log`):

| Part of the mirror pixel's light | share of the light | flicker | flicker of 8-pixel blocks |
|---|---|---|---|
| The surface's diffuse light, as handed to NRD | 0.81 | 0.23 | 0.03 |
| The same, as NRD returned it | 0.76 | **0.09** | 0.01 |
| The surface's specular light, as NRD returned it | 0.03 | 0.01 | 0.00 |
| The rest, back as traced | 0.22 | **2.42** | 0.44 |
| The whole pixel with NRD | 1 | 0.58 | 0.08 |
| The whole pixel, no denoiser | | 0.68 | 0.09 |

NRD does its work on what it is handed: the replaced surface's light flickers 0.23 going in and 0.09 coming out. A
fifth of the light is "the rest", and it flickers 2.4: 0.22 x 2.42 is 0.53 of the pixel's 0.58. The pictures show
what the rest is (`images-C\95-psr-part-rest.png`, the rest alone: the replaced pixels are black but for three soft
glows on the mirror floor; `97-psr-nrd.png`, the whole picture): the lamps' highlights.
The fixture's lamps are light records with no surface of their own, so a mirror shows them only through the direct
light's highlight on the mirror itself, which the direct light's samples find through a lobe a pure mirror does not
have (as I read `pt_common`, a highlight keeps `roughness_alpha`'s floor): a soft glow with heavy noise in place of the
lamp's sharp image.

With the switch off that noise went through NRD's specular filter together with the reflection, both blurred: less
flicker (0.40 here, 0.61 in the game), less detail, and more boil (in the game 0.45 against 0.27 with it on). With
replacement the reflection is sharp and clean and the highlights' noise is what is left.

Against the same panel seen directly, NRD takes its flicker from 0.56 to 0.01: its light is sampled by ReSTIR with
reuse. The mirror's picture is lit one lamp at a time at the reflection's end (0.23 before NRD), which is why it stays
noisier than the room around it even without the highlights.

Not shown in the game: I could not run it. Whether the tavern's 0.83 is the same thing can be seen in one run:
start the game with the environment variable `TGPT_TEST_PSR_VIEW=8` and the mirror shows the rest alone (6: the
diffuse part as NRD returned it; 9: as handed). It is read by `tracer.cpp` every frame and is for looking only.

What would take the flicker away is in the tracer, not in a denoiser: a pure mirror has no lobe to sample a lamp
through, so the lamp belongs to what the reflection ray sees. A lamp with its own surface (a flame's card, "a light is
the thing that emits it") is seen by that ray, sharp and without noise, and the highlight on a pure mirror then counts
it twice or should not be there. That is a new entry for `docs/PT-REQUESTS.md` (a defect found: "a pure mirror's
highlights of lamps are sampled through a lobe it does not have: a noisy glow, and the flicker left in a mirror under
NRD"); it is noted under PT-127 in the copy's `docs/PT-REQUESTS.md` with two more found on the way,
for the coordinator to number.

## (b) Glass and water: the design for the owner

Nothing of this is built. Our own reflection filter stays on glass and water until the owner has ruled.

### Why glass and water are not one more case of the mirror rule

At a mirror a camera ray has one way on. At glass and at water it has two: part of the light is what the surface
reflects, part is what is seen through it. NRD takes one surface a pixel (one depth, one normal, one motion). Today the
tracer picks one of the two ways by chance each frame. Then the surface "behind" a glass pixel changes from frame to
frame, and a denoiser needs the same surface every frame. So each way needs a picture of its own, with guides of its own.

Water adds three things glass does not have: waves (the surface is not flat, so a replaced surface's history is thrown
away more often; NRD's README names a per-pixel input to loosen that), foam (where foam lies the water is a rough
surface, no mirror), and light scattered inside the water (no surface at all).

### What NVIDIA does: two documents, two answers

1. **The article NRD's README links** ("Rendering Perfect Reflections and Refractions in Path-Traced Games"), as I read
   it. It splits the path ONCE, at the first glass or water surface: one half of the pixels, in a checkerboard, follows
   the reflection, the other half what is seen through. Each half is denoised with its own replaced surface, at half
   resolution, and the two are woven together. After that first split it follows one way only, the stronger one. Its
   own list of limits: half resolution, a stipple pattern that upscalers make worse, refraction a little blurry, no
   good answer for several glass surfaces in one pixel, motion right only for flat mirrors that stand still. It gives
   no number for a threshold.
2. **NRD's README itself** (its overview): NRD is not made for transparency, and NVIDIA's own NRD sample draws glass
   WITHOUT a denoiser. The glass ray is followed to the first solid surface; that surface's light is looked up in the
   frame NRD has just denoised (on screen) or in a radiance cache (off screen); reflection or refraction is picked by
   dithering; TAA or the upscaler smooths the rest.

### How they sit with the owner's rulings

- The article's checkerboard is half-resolution rays ("Do full resolution traces"), and following only the stronger way
  after the first split cuts light ("No limits that change the picture").
- The sample's lookup in the denoised frame makes what glass shows depend on what is on screen ("no screen space
  stuff", 2026-10-07): a thing behind the camera would come from the cache, softer, and change when it enters the screen.

So neither can be followed to the letter without the owner changing a ruling. That is the question for the owner.

### Three ways to go, with what each costs

Estimates, at 1924 x 1084 on this RTX 5080. The basis: in the tavern NRD with its prep and our filter costs 2.8 ms a
frame (36.4 ms with no denoiser, 39.3 with NRD: run 20261011-213531); NRD's pools hold 171 MB, its seven textures 102 MB,
our own filters' textures 272 MB (that run's log); a layer of records is 127 MB.

| | A. Two layers, both through NRD | B. One layer through NRD, the other as traced | C. As NVIDIA's sample: no denoiser on glass |
|---|---|---|---|
| What the player gets | Reflection and view-through both clean, at full resolution | The view through clean (most of glass's light); the reflection on it as noisy as today, or with our filter kept for that part | Clean and cheap, but what glass shows comes from the picture on screen where it can |
| Rays | Both ways traced at every glass and water pixel every frame: the reflections' pass about twice as long for those pixels | The same | One way a frame, by dithering: as today |
| NRD | A second NRD run for the second layer: about +2.5 ms whenever glass or water is on screen (NRD's time goes with the resolution, not with how many pixels are glass) | No second run | None for glass |
| Memory | +171 +102 +127 MB for the second layer; our own filters' textures (up to 272 MB) can go under NRD | +0 (the mirrors' records serve) | Less than today: our filter goes |
| Passes | No new kind: `pt_specular` traces both ways, `pt_nrd_prep` runs twice, the composite adds the two | `pt_specular` traces both ways | The glass pass reads the denoised frame: it must run after NRD |
| Follows NVIDIA | The article's idea without its two shortcuts (full resolution; later splits by chance, nothing cut) | NRD README's replacement for the stronger way; the weaker way is our own | The README's own sample, to the letter |
| Against a ruling | None | The weaker way stays undenoised or keeps a filter of ours: a workaround left in | "No screen space stuff" |
| Workaround removed | Our reflection filter on glass and water | Half of it | All of it |

Later splits (the back wall of a jar, a second pane, the water's surface seen from under it) are followed by chance
inside a layer in A and B: no light is cut, the price is noise there, and the replaced surface can change from frame
to frame at those pixels. More layers would fix that and cost a run of NRD each.

Rough glass and foam are no mirror: they stay NRD's ordinary rough surfaces. Light scattered inside water has no
surface: it stays as traced (it is smooth already) in every option.

One thing the mirrors taught that glass will meet too: the surface's own highlights of lamps are not the replaced
surface's light. On glass and water they are the sun's and the lamps' glints. They stay as traced in A and B, and they
are where the flicker will be (see "The flicker").

### What I would do, in this order

1. **Glass first, way B**, as the next small step: glass covers few pixels (jars, bottles, windows), so tracing both
   ways there costs little, and nothing new is needed beside what the mirrors now have. The game then shows whether the
   view through glass comes out right under NRD (its depth and motion through a refraction are the part NVIDIA's
   article gives no formula for: that is the risk, and a jar is where to see it).
2. **Then way A for glass** if its reflections look too noisy: the second NRD run.
3. **Water last, way A**, after the speed work: water fills half the screen at a lake, the reflections' pass is the
   largest pass of such a frame, and tracing both ways there makes it up to twice as long. It needs the water worker's
   knowledge of the waves and the foam (foam as a rough surface a pixel keeps, not a coin flipped each frame).

What I would not do: the checkerboard (half the rays, against a ruling, and NVIDIA's own article lists its faults), and
turning the two ways into one by a threshold (it cuts light).

## (c) Ray Reconstruction's guides: what the coordinator runs

The first worker put two changes to Ray Reconstruction's guides behind the same hidden switch as the mirrors:

- **Review 3**: a hair pixel's guides all come from the surface its ray stopped at this frame (the camera pass no longer
  writes the first hair card's guides; `pt_compose` writes colour, normal and roughness of the stop surface; the sky
  behind hair gets the sky's neutral guides and far depth).
- **Review 20**: glass's reflection distance comes from a reflection ray of its own from the glass (NVIDIA's guide
  3.4.9), where today it is the length of the whole bent path.

**They are not in this build.** Reasons:

1. The game has not seen them, and the offline tests cannot run Ray Reconstruction.
2. Review 3's change undoes something the tree does on purpose. `pt_primary.hlsli` `write_guides` holds the first
   card's guides "the same every frame, whether this frame's ray stopped at that card or passed it", by NVIDIA's guide
   3.5 (steady guides), and `pt_compose.hlsl` notes that guides laid on whole "flipped between the hair's and the blend
   from frame to frame". With the change a pixel a card covers partly gets hair's guides in the frames the ray stops at
   it and the skin's in the others: coherent, but no longer steady. Hair under Ray Reconstruction is the owner's most
   looked-at thing. Review 3 can also be answered the other way round (depth, motion and reflection distance from the
   first card too, weighed by its cover), which keeps the guides steady.
3. Review 20's extra ray is compiled into the reflections' pass for every denoiser.

So bit 22 is free and Ray Reconstruction is handed exactly what it was handed before.

### What to run, in this order

**1. That this build left Ray Reconstruction alone** (after the merge, in the main tree):

```
ctest --test-dir build\native -C Release -R tgup_gpu_smoke_with_pt --output-on-failure
.\tools\harness\Run-PtHarness.ps1 -Scenario hair-eyebrows@with-rr
```

Look at: the smoke test passes as before. In the game run's `TGPathTracing.Native.log`, the line "NRD is told of what a
still pure mirror shows" must appear only once NRD is the denoiser (step `4-traced-nrd`), never in step `3-traced-rr`.
Step `3-traced-rr`'s luma, flicker and detail should match the last `with-rr` run of the same scene: nothing it is
handed changed.

**2. The game's answer on the two guide changes**, with the FIRST worker's build, which has the switch. It is
the build of run 20261011-213531; if the game folder has been redeployed since, install with `Deploy-PtBuild.ps1`:
`%TEMP%\tgb\psr-build\variants\A\TGPathTracing.Native.dll` (SHA-256 7DD2E5E3...A68A) and `TGPathTracing.dll` beside it
(578A2085...). The recipe is in `%TEMP%\tgb\psr-build\rr-guides-first-build.recipe.json`: paste its `rr-guides` under
`recipes` for the run and take it out again (its setting exists only in that build). Ray Reconstruction with the guides as
now, the changed guides, as now again, the world running, 16 frames each.

```
.\tools\harness\Run-PtHarness.ps1 -Scenario eyebrows-flickering@rr-guides,hair-eyebrows@rr-guides,npc-day@rr-guides,glass-jars@rr-guides
```

Look at:

- **Hair** (`eyebrows-flickering`, `hair-eyebrows`): `python tools\harness\flicker.py <run>\<scene>\2-rr-guides-one-surface --boil`
  against steps 1 and 3. Do brows, lashes and thin strands flicker or crawl more with the changed guides? Is hair's
  detail number higher or lower? If flicker rises, the change is the wrong way round (see reason 2).
- **The sky behind hair** (`npc-day`): a bright or dark fringe around hair against the sky, step 2 against 1 and 3.
- **Glass** (`glass-jars`): with a still camera the reflection distance does little. It shows when the camera moves:
  the owner walking sideways past the jars with Ray Reconstruction, the switch off and on (`[Advanced]`
  `Primary surface replacement (trial)` in the plugin's config). Do the reflections on the glass trail or swim less?
- **The light**: whole-frame luma and the exposure and gain log line must be the same in steps 1, 2 and 3. Guides
  carry no light.

**3. If the game says yes to one of them**, it becomes the rule in a job of its own (a shader build of the camera pass,
the composite and the reflections' pass). The code is the first worker's: `%TEMP%\tgb\psr-build\psr-checkpoint.patch`,
its hunks of `pt_primary.hlsli` (`if (w.met && !psr_trial())`), `pt_compose.hlsl` (`traced_guides`, the depth), `d3d11.hlsl`
(the sky's tag), `pt_specular.hlsl` and `pt_psr_capture.hlsli` (`psr_rr_glass_distance`), `tracer.cpp` (the composite's
`extras` bit 2). Then `tgup_gpu_smoke_with_pt` again and a `with-rr` run.

## (d) The build and the files to merge

### The build

| File | Bytes | SHA-256 |
|---|---|---|
| `%TEMP%\tgb\psr-build\variants\C\TGPathTracing.Native.dll` | 32,966,656 | `A1A5AE40AFC3E438D4DBE78EC405FEA6FFCB0CB9DFF705621BAF9A7BC95EC804` |
| `%TEMP%\tgb\psr-build\variants\C\TGPathTracing.dll` (the plugin) | 586,752 | `CAFEF9411C30EFBAC5CC18EDD66848944238B938E03364BC0BB9165D6FF1FE7C` |

The plugin's sources are `base20pc`'s byte for byte (the trial's setting and flag are gone), so any plugin of that base
works with this DLL, the one in the game folder too: the native side ignores bit 22. No bridge change, no export, no
change of a public record.

**How the passes were compiled, said plainly.** A change to `pt_common.hlsli` marks all 57 passes out of date, and
while the game is open the shader script may run one compile at a time: hours. The code this job changed is called in
five passes only (`pt_specular` in its three builds, `pt_prepare` in two, `pt_compose` in two, `pt_nrd_prep`,
`pt_nrd_confidence_filter`). Those nine headers were compiled from the final sources. The other 48 are the first
worker's compiles of 19:15 to 20:01. Whether they are what the final sources give was checked, not assumed:
`tools\psr\verify-headers.py` compiled each again from the final sources, one at a time, and compared the bytes with the
tree's header (`%TEMP%\tgb\psr-build\verify-rest-*.log`):

- 46 are byte for byte the same (the 40 MB `pt_gi_dxr12`, `pt_gi` and both `pt_walk` among them);
- `pt_gi_reuse` was compiled from the final sources outright;
- **`pt_gi_ser` came out different**: the same size, another shader hash. It is a library pass (the bounced light's
  ray generation shader with Shader Execution Reordering), and a library holds every function of the headers it
  includes, called or not: the record's accessors and `shade_seen` are in it though the bounced light calls none of
  them (its two plain builds, the same code as compute passes, are byte for byte the same). So the first worker's
  `pt_gi_ser` does what the new one does and carries the old record's accessors as dead weight.

The build named above (`variants\C`) is the one the check ran on, with that first worker's `pt_gi_ser`. A second DLL,
`variants\C2\TGPathTracing.Native.dll` (`A6FD7A02267A5AE3E656328162A2C773328777493A7ABD9FB0BFE1A68E02FC30`), has the
final sources' `pt_gi_ser` and is otherwise the same passes: in it every one of the 57 is the final sources' compile.
The check has not run on it (the game was open). The coordinator's merge compiles everything anyway.

A learning for `docs/PROJECT-CONTEXT.md` (not added there: the coordinator's file): a function added to `pt_common.hlsli`
changes the two library passes' code (`pt_gi_ser`, `pt_specular_ser`) whether they call it or not, and nothing in the
compute passes that do not call it; `verify-headers.py` shows which headers a change really moves, in seconds for
most passes.

### Flags, records, rows, exports: what was added and what was freed

- **Freed**: frame flag bit 22 (`TGPT_FRAME_PRIMARY_SURFACE_REPLACEMENT`, managed `FramePrimarySurfaceReplacement`), the
  hidden setting `Primary surface replacement (trial)`, the D3D11 composite's private `extras.x` bit 2.
- **Kept from the first worker**: the row `psr` of `FrameConstants` (9024 bytes), now: x the records' UAV, y none, z a
  test's view (0 in the game), w the frame's serial. `NrdFrame.primarySurfaceReplacement` (private).
- **Changed**: `PsrRecord` is 64 bytes (was 112); `PrimarySurfaceReplacementResources::RecordLayout` with it.
- **Added**: nothing public. No export, no option bit, no material bit, no ABI change. Two environment variables for
  tests: `TGPT_TEST_PSR_VIEW` (read by the tracer each frame) and `TGPT_TEST_PSR_ONLY`, `TGPT_TEST_PSR_OLD` (the test's).
- `tools\Build-PtShaders.ps1` keeps the first worker's `-MachineTemp` and hidden compiler windows.

### The files to merge (`python tools\psr\make_handoff.py` lists them by comparing the copy with `base20pc`: 36 files)

Review patch: `%TEMP%\tgb\psr-build\psr2.patch`; sizes and hashes: `psr2-source-manifest.json`.

| File | What | In the main tree since `base20pc` (compared 23:40) |
|---|---|---|
| `native/pathtracing/shaders/pt_psr_record.hlsli` | new: the record and its accessors | new |
| `native/pathtracing/shaders/pt_psr_capture.hlsli` | new: the reflections' pass's part | new |
| `native/pathtracing/src/primary_surface_replacement.hpp` | new: the records' buffer | new |
| `tests/pathtracing_psr_checks.inl` | new: the small mode | new |
| `native/pathtracing/shaders/pt_primary.hlsli` | `psr_primary`, called after `write_traced` | unmoved: take the copy's file |
| `native/pathtracing/shaders/pt_specular.hlsl` | `psr_begin`, `psr_finish`, the define | unmoved: take the copy's file |
| `native/pathtracing/shaders/pt_medium.hlsli` | the fog's transmittance kept for the mirror (`fogged`) | unmoved: take the copy's file |
| `native/pathtracing/shaders/pt_nrd_prep.hlsl` | replaced pixels in the front and the back end; the material class | unmoved: take the copy's file |
| `native/pathtracing/shaders/pt_nrd_confidence_filter.hlsl` | a replaced pixel takes neutral confidence | unmoved: take the copy's file |
| `native/pathtracing/shaders/pt_compose.hlsl` | the test's view only | unmoved: take the copy's file |
| `native/pathtracing/src/nrd_denoiser.cpp`, `.hpp` | NRD compares material classes with the records | unmoved: take the copy's files |
| `CMakeLists.txt` | the two new headers in `PT_SHADER_DEPENDS` | unmoved: take the copy's file |
| `tools/Build-PtShaders.ps1` | `-MachineTemp`, hidden compiler windows (the first worker's) | unmoved: take the copy's file |
| `native/pathtracing/shaders/pt_common.hlsli` | the row `psr` and the include (2 lines); under `PT_PSR_CAPTURE`: the globals, `psr_capture`, `psr_capture_reflection`, their calls in `shade_seen`, `direct_light`'s two parts | MOVED: three-way |
| `native/pathtracing/src/tracer.cpp` | the include, the row and its size (9024), the records' life, the serial, the memory counts, the barrier, `n.primarySurfaceReplacement` (8 small hunks) | MOVED: three-way |
| `tests/pathtracing_gpu_test.cpp` | the mode's include, right after the warm-up (one hunk) | MOVED: three-way |
| `tests/source_checks.py` | `test_path_tracing_mirrors_are_replaced_for_nrd` (new, at the class's top); 9008 -> 9024 in five places; the row count 51 -> 52; the material class line | MOVED: three-way |
| `tests/ingame/scenarios.json` | recipes `psr`, `psr-mirror`: plain looks (the first 12 lines of `recipes`) | MOVED: three-way |
| `BACKLOG.md`, `CHANGELOG.md`, `docs/PROJECT-CONTEXT.md`, `docs/PT-REQUESTS.md`, `docs/rounds/round-20.md` | the first worker's lines rewritten for the rule (one hunk each); the coordinator's own words win | MOVED: by hand |
| `ROUNDS.md` | the workers' four lines under round 20 | unmoved |
| `docs/agents/psr.md`, `docs/agents/psr-2.md` | the two reports | new |
| `tools/psr/*` | the worker's own scripts (build, check runner, mutant, header verification); take or leave | new |

NOT to merge, they are the base's bytes again: `src/TGPathTracing/NativeTracer.cs`, `Settings.cs`, `HdrpPasses.cs`,
`native/pathtracing/include/tgpt_api.h`, `native/pathtracing/shaders/d3d11.hlsl`.

After the merge: a full shader build (the camera pass, the reflections' pass and the composite change; `pt_common`
marks all), `python tests\source_checks.py`, `tools\psr\run-check.ps1` adapted to the main tree or the mode by hand
(`TGPT_TEST_PSR_ONLY=1`), then `mirror-tavern@psr-mirror` (a plain look now: game, no denoiser, NRD).

## Workarounds met (the brief's ruling: every job lists them)

- **Removed**: none of the tree's. The first worker's trial switch, flag and per-pixel clear are gone.
- **Left, and why**: our reflection filter on glass and water under NRD (the brief: it stays until the owner rules on b).
- **Met, not mine to remove here**: the reflection ray of a surface that is no character's starts 2 cm and a fifth of a
  percent of the camera's distance off the surface (`ray_offset`, made for HDRP's surfaces whose depth could differ
  from the traced mesh). With the tracer's own camera rays the surface is on the mesh and needs no such offset. It
  moves what a mirror shows by 1 to 2 cm in depth. A line for PT-60's list.
- **Met**: a pure mirror's highlights of lamps through a lobe it does not have (`roughness_alpha`'s floor): a stand-in
  for the lamp's image in the mirror, and the flicker left under NRD. A defect to enter (see "The flicker").

## What I could not do

- **The game.** Nothing here was seen in the game. The game's numbers above are the first worker's build's.
- **The cost's base and the mutant's run**: built, not run; see "State when stopped".
- **Ray Reconstruction.** Not run (it cannot run offline). Its guides are untouched in this build; (c) says what to run.
- **Records only while a mirror is on screen.** They are made whenever NRD runs: 127 MB at 1924 x 1084 for scenes that
  have no mirror at all. Making them on demand needs a count of mirror pixels read back from the GPU each frame (the
  yardstick's counters run twice a second and not in every mode), and a first frame without replacement when a mirror
  comes into view. Not built. If video memory is the tighter need, this is the first follow-up.
- **A full compile of all 57 passes from the final sources in one go**: see "How the passes were compiled" (nine
  compiled, 46 shown the same byte for byte, two compiled again one by one; the DLL with all of them is `variants\C2`,
  on which the check has not run).
- **A thing moving in the mirror**, a mirror in a mirror, a moving or curved mirror: not checked or not built (above).
  (Things moved in the tavern's mirror in the game's run of the first worker's build; that code is unchanged.)
- **REBLUR** (the hidden `NRD trial` 2): the replaced pixels' path for it is the first worker's, kept; no run of mine
  used it. The fixture and the game's run used RELAX.
- **A mirror the game follows every frame** (the harness's mirror: its two transforms are compared): the fixture's
  mirror is a plain static instance, so that branch of `psr_primary` ran only in the game, in the first worker's build;
  it is kept word for word.
- **My first run of the small mode ran more than the small mode.** It stood after the mirror filter's mode in the
  file, and a mode there runs every check above it first: the cut-outs, the edges and part of the reuse check ran for
  about a minute before I saw it and stopped it. The mode now stands right after the warm-up. Two of my test starts
  also began in the minutes before a game run of the coordinator's and were still making their passes when it
  started; the runner now stops its test the moment the game opens.
