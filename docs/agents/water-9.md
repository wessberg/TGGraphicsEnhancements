# Water 9 - the light on what lies under the water: the dark crab, and the rippling light that does not show (PT-81)

Copy: `wt\water9` (base `wt\base62`). One Claude worker, 2026-10-09 from about 06:30 to 08:30.
Nothing is written to the main tree or the game folder; the game is never started; nothing deployed or committed.
Build: `%TEMP%\tgb\water9` (Ninja, 8 jobs, so the shader passes compile 8 at a time; NRD off). Scripts, crops and logs'
readers: the session's `scratchpad\water9-work\`.

## Read this first

**State (08:30): the job is at its end. Two trials are built, each behind its own hidden switch, both off by default
(the picture as before). The DLL, the GPU test and the plugin build; source checks 99 of 99; the worker's own offline
check ran (section 7). Nothing has been seen in the game: the ONE run is the coordinator's (section 4).**

| | Trial 1: the left-out light lands | Trial 2: the light keeps its direction |
|---|---|---|
| The switch | `Advanced` / `Caustics land split light` (bool, default `false`) | `Advanced` / `Caustics light keeps its direction` (bool, default `false`) |
| How it travels | option bit `TGPT_OPTION_CAUSTICS_SPLIT` 1073741824 (the one free bit) | option bit `TGPT_OPTION_CAUSTICS_LIT` 4194304 (the bit HDRP's shading shares had until they were retired; nothing set it since) |
| What it does | A grid triangle of the water whose three corner rays met different surfaces was left out with its light. On: each corner's third of that light lands on the plane of what that corner met. | Under water the sun's light was read as flat, diffuse light. On: it comes from the direction it came through the waves: the surface's own normal (its normal map) shades it, and it gets its highlight. |
| The sum | What is written is exactly what the triangles carry (section 2; the log counts it: 99.2 % landed before, 100.0 % with it, in the offline scene). | The light across the beam is the landed light over the cosine it landed at: on a surface whose normal is its triangle's, the diffuse light is the same as before. |
| Offline (section 7; against the straight light, which should read 0.978) | a 10 cm box x0.59 -> x0.77; the bed within 3 cm of things x0.71 -> x0.78, which is the open bed's own number there: the dark ring is gone | the open bed x0.78 -> x0.945; the box x0.89; the highlight back |
| Cost offline | 2.5 % more triangles; the caustics stage 1.30 -> 1.31 ms | the frame 2.22 -> 2.23 ms |
| Touches | `pt_caustics.hlsl` only (one pass) | `pt_common.hlsli`, `pt_water_path.hlsli` (every pass compiles anew), 4 bytes more written per landed triangle (spare bytes of its record) |
| No ABI change | no export, no struct, no size | the same |

- **The recipe for the ONE run: `caustics-split`** (in the copy's `tests/ingame/scenarios.json`; section 4): `water-day`,
  no denoiser, 16 frames a step: the row on, trial 1 on, trials 1 and 2 on, the row off.
- **The log says the sums** (section 3), in every step: how much of the sun's light under the water is on left-out
  triangles, how much landed, and for step 3 of the brief the waves' slopes, the bed's depth and the landed light's
  ups and downs, per level. The offline check holds that measure against waves worked out by hand: 12.3 % read,
  12.6 % worked out.
- **What the game's frames already say** (run `20261009-010307`, sections 1 and 5):
  1. With the row on the top of the crab's shell IS lit. Dark are its legs, its rim, and a ring of bed 2 cm wide all
     round it: the left-out triangles (trial 1).
  2. The top of its shell, where nothing is left out, is still only x0.62 of the row off, and its highlight is gone:
     the light under water is diffuse only (trial 2).
  3. **The rippling light IS in the picture: a still, soft pattern, the light going up and down by 5 to 10 %**
     (standard deviation), blobs of 5 to 10 cm in the shallows, the beginnings of a net of bright bands further out
     where the water is deeper. No lines in the shallows. The game's waves are too long and too gentle to draw lines
     on a bed 20 to 50 cm down (section 5). The pass loses none of it on the way (section 7).
- **What the trials do NOT mend, with numbers** (section 8):
  1. **What is thinner than a cell** (a crab's legs; the cells are 2 cm near the camera): a bar 1 cm thick gets 0.12
     of its light today, 0.27 with trial 1, 0.39 to 0.46 with both; a bar 2 cm thick 0.48, 0.58, 0.79. It is the
     grid's fineness, not the left-out triangles: where no ray of the grid lands on a thing, nothing knows it is there.
  2. **A third finding: nothing lands outside the camera's view** (times 1.5), and a path under water does ask there:
     the light the bed throws up and the surface mirrors back down from outside the view is missing. In the offline
     scene (a narrow view) the bed's light was 16 % short for it. It is light that depends on what is on screen.

## 1. What the game's frames show of the crab (run `20261009-010307`, read from its raw frames; crops in `scratchpad\water9-work\crab2.png`)

The mean of the 16 frames, the crab near the screen's middle (about 60 pixels wide at 3840: a body of some 10 cm):

- **Row on:** the top of its shell IS lit. Dark are its legs and claws, the rim of its shell, and **a ring of the sea bed
  all round it, some 2 cm wide**. The crab's own shadow cannot be told from that ring.
- **Row off:** the whole crab is lit, with a highlight on its shell, and it throws one shadow, away from the sun.

That is the picture of the left-out triangles: every grid triangle with one corner on the crab and one on the bed is
dropped, on both sides of the crab's outline, one cell (2 cm at the finest level) wide. The legs are thinner than a
cell, so every triangle that touches one is dropped. So water-8's reading of the code holds, with one correction: it
is the outline and what is thin that go dark, not the whole body. The offline scene shows the same ring round a box
(section 7, `landed-still.png`).

Boxes of the 16-frame means, linear light (`boxes.py`):

| Box | row on / row off | brightest 5 % of the box, on / off |
|---|---|---|
| the top of the crab's shell (no triangle left out there) | x0.62 | 0.011 / 0.027: the highlight is gone |
| the bed 40 cm left of the crab | x0.78 | |
| the bed below the crab (foam in it) | x0.93 | |
| the bed further out | x1.07 | |

(The bed's numbers differ by place because the row off takes the cosine with the sun's straight direction and the row
on with the bent one, on a bed that slopes: the row on is the right one there, and the difference is no loss. The
shell's highlight is a loss.)

## 2. Trial 1: what it does, and the sum

`pt_caustics.hlsl`, `emit_triangle`: a triangle is "split" when its corners' smooth normals disagree by more than 60
degrees, or its landed triangle does not lie along the surface (a step), or a corner of it landed nowhere while
another did. Before, and with the switch off, a split triangle writes nothing.

With the switch on (`emit_part`, once for each of its three corners that landed and carries light):
- The light through a water triangle is its share (the sun's irradiance times its area across the light) times the
  mean of what its three corners carry. So a corner's part is a third of the share times what that corner carries.
- That part lands on the plane of what the corner met (through its landing point, by the smooth normal there), on the
  triangle the three corners' rays cut out of that plane (the beam's cut there: rays that bunch make it small and
  bright, exactly as on a whole triangle). It is full at the corner's own point and falls to nothing at the other two.
- Read by the barycentric weights (`caustic_light`, not changed), its mean is a third of the corner's light per area:
  the part's sum is the corner's third exactly. Three parts: the triangle's light exactly.
- Where the three corners did meet one plane, the three parts add up to the very triangle that was written before.
- On one curved thing (a round stone whose normals turn more than 60 degrees in a cell), and wherever two parts are
  read at one point, the parts add up to 1: nothing can be counted twice.

**The sum, before and after:**

| | landed light against what the triangles carry |
|---|---|
| Before (switch off) | carried, less ALL the light of every split triangle. In the offline scene 0.8 % of the sun's light in view (a few small things on a wide bed); at the beach the run's log says it (section 3). |
| After (switch on): what is WRITTEN | carried, exactly. The log counts it: 100.0 % in the offline scene (it would fall short only by parts that are degenerate, out of view or past the buffer's room). Nothing is made: no part is written twice. |
| After: what a surface READS of it | all of it where the plane's cut lies on the thing the corner met. A part's cut reaches past the edge of that thing by up to a cell, and what lies past the edge is read by nothing. Offline (section 7): a 10 cm box reads 0.79 of what it should (0.61 before), with trial 2 as well 0.91; the bed within 3 cm of things reads all of its own (0.996 of the open bed's number; 0.91 before), as a soft ramp where a thing's shadow starts. |
| After: what is thinner than a cell | only what the grid's rays happen to land on it: a 2 cm bar 0.58 of its light (0.48 before), a 1 cm bar 0.27 (0.12 before), in patches that move from frame to frame. Section 8. |

No light is invented: a part is only ever a third of its own triangle's light, on the plane of what its own ray met.

**Tried and taken out (it is in `scratchpad\water9-work\pt_caustics.with-ext.hlsl`):** a part kept full, not falling
off, towards a corner whose ray passed its plane and landed on another thing behind it (a shell lit to its very rim).
Offline it lit the box 5 % better (x0.927 against x0.888 with both trials), did nothing for the bars, and invented
nothing there. But where two things meet at a slope (a rock in the sand) the bed at the rock's foot lies within reach
of the rock's plane and would read both: light counted twice. The parts as built cannot do that.

One thing changed for both states, with the same result as before: a corner that is not on the slot's water (off a
pond's edge) leaves its triangles out by a flag of its own (`gs_on`); before, its normal of 0 did that.

### What trial 1 costs
Up to three triangles written for each split one, where none was. Offline: 17,739 triangles a frame become 18,176
(2.5 % more); the caustics stage 1.30 ms and 1.31 ms; the frame 2.22 ms both. `water-day` writes 307,000 triangles a
frame now (built over 524,288, room for 1,048,576: the log of run `20261009-010307`); the count with the switch on is
in the same log line, and the room grows by itself when a frame wants more. The game's frame time is for the run to
say (the "caustics" and "water reflections" stages).

## 2b. Trial 2: the light under water keeps its direction

`pt_water_path.hlsli` line 147 was, and with the switch off is:
`radiance += weight * m.albedo * caustic_light(hit.position, hit.normal)`. The landed light is read as light per area of
the landed triangle and multiplied by the albedo: the surface's own normal (its normal map) plays no part and there is
no highlight. With the row off the same surface is lit by `direct_light`, which takes the cosine with the
normal-mapped normal and adds the GGX highlight. So a glossy shell is dull with the row on, and a bumpy bed flat.

With the switch on:
- `pt_caustics.hlsl` `store_triangle` keeps, for every landed triangle, the direction its light came from (the mean
  of its corners' rays, turned round; for a part of trial 1 its own corner's ray), packed into bytes 40 to 43 of its
  48-byte light record, which nothing used. It is written in both states; only the trial reads it.
- `pt_common.hlsli` `caustic_lit` (new, beside `caustic_light`, which is not changed): the same search; for each
  landed triangle at the point the light across the beam is the landed light over the cosine it landed at (a twentieth
  at least), and the surface answers it as it answers any light: the albedo times the cosine with its shading normal
  (a thin sheet's light from behind as `direct_light` takes it), and `ggx_specular` seen from the path's direction.
- `pt_water_path.hlsli`: the path's vertex under water calls it in place of the line above.

No light is made: on a surface whose shading normal is its triangle's, the diffuse light is what it was. A normal
turned to the light gets more, one turned away less, as above water. The highlight is new light in the picture: it is
the light the surface mirrors, which the diffuse-only reading left out. Offline, on a dark bed (albedo 0.05, where the
highlight is a fifth of the bed's light): the open bed reads x0.780 of the straight light without it and x0.945 with
it, where 0.978 is right.

Not done by it: the camera pixels' own caustics (`pt_compose` `caustics_at`: light mirrored up by the water onto a
wall) stay diffuse. A path under water is where the crab and the bed are read.

## 3. New in the native log (every 10 s, in the status line, after "caustics: N triangles a frame ...")

`; the sun's light under the water in view: A % on triangles whose corners met different surfaces, B % landed (per level, split / landed %: 0: a / b, 1: a / b, ...); its rippling where it landed (per level: 0: slopes S, D cm down, ups and downs C %, ...)`

- Counted for the sun's refracted light, the triangles in view, per level (0: the 2 cm cells round the camera, each
  next twice as coarse).
- **Trial 1 off: B = 100 - A.** A is the light that is lost today.
- **Trial 1 on: B should read 100** (within a few tenths): written equals carried.
- It is counted in both states, so one run gives "before" and "after". Read each step's LAST status line (a line
  covers 10 s, and the first one of a step can hold frames of the step before).
- **The rippling light, for step 3** (the same line, counted in every state, of the triangles that landed on one
  surface): per level, S the waves' slopes there (root mean square over the level's corners), D how far under the
  surface the light landed (the mean ray), and C how much the landed light's brightness goes up and down over what it
  landed on (its standard deviation over its mean, across the beam: the waves' focusing alone, no shadow or absorption
  in it). C is what the pass delivers; section 5 holds it against what the picture shows and what the waves can give.

The counters: `pt_caustics.hlsl` counters [48..127] (ten sums a level, `SUM_*`; a thread adds to its group's, the
group's first thread to the buffer: ten atomic adds a tile); `tracer.cpp` `kCausticCounters` 48 -> 128 and
`split_text()`. A source check pinned the 48: changed.

## 4. The recipe for the ONE run (`water-day`)

`.\tools\harness\Run-PtHarness.ps1 -Scenario water-day -Recipe caustics-split` (as `caustics-none` was run). In the
copy's `tests/ingame/scenarios.json`, after `caustics-none`:

```json
"caustics-split": [
  { "name": "1-game", "set": [ { "key": "Mode", "value": "Off" }, { "key": "Debug view", "value": "None" }, { "key": "Caustics", "value": "true" }, { "section": "Advanced", "key": "Caustics land split light", "value": "false" }, { "section": "Advanced", "key": "Caustics light keeps its direction", "value": "false" } ], "traced": false, "settle": 3, "frames": 4 },
  { "name": "2-row-on-split-off", "set": [ { "key": "Mode", "value": "On" }, { "key": "Denoising", "value": "None" } ], "traced": true, "settle": 6, "settleFrames": 120, "frames": 16 },
  { "name": "3-row-on-split-on", "set": [ { "section": "Advanced", "key": "Caustics land split light", "value": "true" } ], "traced": true, "settle": 6, "settleFrames": 120, "frames": 16 },
  { "name": "4-row-on-split-on-lit-on", "set": [ { "section": "Advanced", "key": "Caustics light keeps its direction", "value": "true" } ], "traced": true, "settle": 6, "settleFrames": 120, "frames": 16 },
  { "name": "5-row-off", "set": [ { "key": "Caustics", "value": "false" }, { "section": "Advanced", "key": "Caustics land split light", "value": "false" }, { "section": "Advanced", "key": "Caustics light keeps its direction", "value": "false" } ], "traced": true, "settle": 4, "settleFrames": 120, "frames": 16 },
  { "name": "6-off", "set": [ { "key": "Mode", "value": "Off" }, { "key": "Caustics", "value": "true" }, { "key": "Denoising", "value": "Ray Reconstruction" } ], "traced": false, "settle": 1, "frames": 0 }
]
```

What to look at (the crab near the screen's middle, and the two smaller ones to its right):
1. Step 3 against step 2 (trial 1): the dark ring round the crab gone; its rim no longer black; its shadow readable
   as one shadow away from the sun, shorter than in step 5 (the bent light comes down steeper: that is right). Its
   legs lighter than in step 2 but still too dark, and a little patchy from frame to frame (section 8).
2. Step 4 against step 3 (trial 2): the crab's shell with its highlight and its form, as in step 5 and in the game's
   own picture; the bed's grain shaded as in step 5.
3. Step 4 against step 5 (the row off): the crab's body about as bright. The bed's light differs by place (the bent
   light against the straight one on a sloping bed); it must not be darker all over by more than what the surface
   mirrors (about 6 %).
4. No new bright specks on the bed or on rocks (a part's cut is small where rays bunch, as a whole triangle's is; a
   highlight under water is one light direction a triangle, so a very glossy thing can glitter).
5. The log: A (step 2's last status line) is today's loss; B in step 3 near 100; S, D, C at level 0 for section 5.
6. The frame time of steps 3 and 4 against step 2.

## 5. The rippling light on the bed (step 3 of the brief)

### From the game's frames (run `20261009-010307`; `scratchpad\water9-work\pattern.py`, `pattern2.py`, `pattern2-a.png`, `pattern-mid.png`)

The row on over the row off, pixel by pixel, in linear light, each blurred over 17 pixels (3 cm) and divided by its own
trend over 161 pixels: what the row adds as a pattern, with the bed's own texture cancelled.

| Where (upright picture, 3840 wide) | the light goes up and down by (standard deviation) | the noise floor of the same measure (8 frames against the other 8) |
|---|---|---|
| by the shore (x 1000-1600, y 1500-1900) | 5.2 to 6.9 % | 0.2 % |
| round the crab (x 1300-2500, y 1150-1750) | 7.6 to 8.1 % | 0.4 to 0.6 % |
| further out (x 1700-2500, y 1150-1500) | 10.3 to 10.8 % | 0.6 to 0.9 % |
| deeper still (x 1800-2800, y 800-1150; foam left out) | 8.3 to 8.5 % | 1.1 to 1.5 % |
| the open sea (x 2000-3200, y 500-800: the surface's own light there, the bed is not seen) | 2.5 % | 1.3 to 1.6 % |

- **It does not move:** the first 8 frames' pattern and the last 8's correlate 0.93; single frames 0.86 to 0.93 (their
  own noise is 2.2 %, which allows 0.90). The world is frozen, and so are the waves.
- **It does not sit on the bed's own shading:** correlation -0.06.
- **Its shape:** soft blobs 5 to 10 cm across in the shallows. Further out, where the water is deeper, bright bands
  round dark cells: the beginnings of the net that rippling light is (`pattern-mid.png`, upper right).

So **the rippling light IS in the picture.** The eye does not find +-7 % in a 16-frame picture with no denoiser beside
the sand's own texture and the noise, and the sheet's "what the row adds (x2)" shows a difference, where the pattern
is a ratio of a few percent.

### Does the pass deliver what the waves give? Yes (the offline check, section 7)

Two crossing waves of 1 m, 9 mm up and down each (slopes 0.058), over a flat bed 1 m down:

| | the waves' slopes | the bed along the ray | the landed light's ups and downs |
|---|---|---|---|
| worked out on the CPU, ray by ray, from the same waves | 0.058 | 110 cm | 12.6 % |
| the native log's status line (the pass's own sums) | 0.056 | 110 cm | 12.3 % |
| the picture (the row on over the row off, seen through the waves, blocks of 5 x 5 pixels) | | | 11.2 % |

Nothing is lost between the waves and the landed light, nor between the landed light and the picture.

### Should it be stronger at that beach? The reckoning

A wave of length L and slope s brings light to a line at a depth of about L / (2 pi s x 0.25) (water bends a ray by a
quarter of the surface's tilt). On a bed far above that depth the light only goes up and down, by about
0.25 x depth x (the surface's curvature), for one wave 0.25 x d x 2 pi s / L.
- The game's "Ocean": three bands of 250, 50 and 10 m over 64 texels each (LogOutput.log of the run). **No wave
  shorter than 31 cm exists in it** (two texels of the finest band), and a wave of four texels, 62 cm, keeps about two
  thirds of its height through the smooth (bicubic) reading, if that is the usual cubic B-spline.
- For +-7 % at 35 cm of water (a ray some 42 cm long): slopes of about 0.06 (root mean square) at 60 cm length, or
  0.1 at 1 m. Such waves draw their lines about 4 m down, not at 35 cm.
- The lines of real shallows come from ripples 2 to 10 cm long with slopes of 0.1 to 0.3 (their lines form 5 to 30 cm
  down). HDRP's water simulation has no such ripples; the game's own picture gets its lines from HDRP's ready-made
  caustics texture, which is not light traced through anything.

**So the answer, plainly: at that beach the game's waves are too long and too gentle to draw lines on a bed 20 to
50 cm down. What they can give is there: a soft pattern of 5 to 10 %.** What the ONE run's log adds (section 3: S, D,
C at level 0, the 10 m round the camera): C should read about the picture's 7 %, with slopes S of 0.05 to 0.1. If C
is far above the picture's number, I am wrong and the loss is between the landing and the picture.

### Where it could still be lost, checked

| Candidate | Finding |
|---|---|
| The landed cells' size (2 cm, doubling with each level out) | Not it in the shallows near the camera: the pattern's blobs are 5 to 10 cm, the cells 2 cm. From 5 m out the cells are 4 cm, from 10 m 8 cm: blobs of 5 to 10 cm are then blurred. A cost of distance, as the brief says. |
| What is kept over frames | With no denoiser: nothing is kept, a frame reads the landed triangles of the frame before. In play WITH a denoiser the bed's light is gathered over many frames while the waves move: a pattern of a few percent that moves is averaged away. Not measured here (the harness's world is frozen); it is where I would look next if the pattern is wanted in play. |
| The left-out triangles | On a flat bed none are left out; at stones and things they are (trial 1). |
| Diffuse only | The pattern lost nothing by it, but the bed's grain did (trial 2). |

## 6. Files changed (all in the copy; against base62)

| File | Change |
|---|---|
| `native/pathtracing/shaders/pt_caustics.hlsl` | `store_triangle` (the writing, moved out of `emit_triangle` word for word; plus the light's direction into the record's bytes 40-43), `part_corner` and `emit_part` (trial 1), `emit_triangle` (the split test as one flag; the counters; the trial's call; with the switch off it writes what it wrote before), `gs_on` (a corner off the water), `flux_units`, the thread's and the group's ten sums, `COUNTERS` 128, the header's comment |
| `native/pathtracing/shaders/pt_common.hlsli` | `#define OPTION_CAUSTICS_LIT 4194304`; `caustic_lit` (new, after `caustic_light`, which is untouched) |
| `native/pathtracing/shaders/pt_water_path.hlsli` | the call of `caustic_lit` with trial 2 on; the old line stays, word for word, for off |
| `native/pathtracing/src/tracer.cpp` | `kCausticCounters` 128; ten sums a level kept between status lines; `split_text()` in the status line |
| `native/pathtracing/include/tgpt_api.h` | `TGPT_OPTION_CAUSTICS_SPLIT 1073741824u`, `TGPT_OPTION_CAUSTICS_LIT 4194304u` (comments and defines; nothing else) |
| `src/TGPathTracing/NativeTracer.cs` | `OptionCausticsSplit`, `OptionCausticsLit` |
| `src/TGPathTracing/Settings.cs` | the two hidden `Advanced` settings and their bits in `Options` |
| `tests/source_checks.py` | the pinned `kCausticCounters=48` -> 128 |
| `tests/ingame/scenarios.json` | the recipe `caustics-split` |
| `tests/water_landed_gpu.inl` (new), `tests/water_surface_gpu.inl` (its include), `tests/pathtracing_gpu_test.cpp` (`waterLanded`: three lines in `main`) | the worker's own check (section 7): `TGPT_TEST_WATER_SURFACE=1` with `TGPT_TEST_WATER_LANDED=1` |

Not touched: `caustic_light` itself, the frame constants, any struct or export.

Merge notes:
- `TGPT_OPTION_CAUSTICS_LIT` takes the retired bit 4194304, `TGPT_OPTION_CAUSTICS_SPLIT` the last free one. If
  another job took either since base62, one needs another home: `TGPT_Frame.options` is full with these two.
- The build that was tested last: `%TEMP%\tgb\water9-stage`, DLL sha256 D5E456FF... (NRD off; the NRD path is the
  coordinator's merged build's to test).

## 7. The worker's own offline check (`tests/water_landed_gpu.inl`; built after the hand-over, as the brief says)

`TGPT_TEST_WATER_SURFACE=1` with `TGPT_TEST_WATER_LANDED=1`, at 640 x 360, alone, 2.5 minutes;
`scratchpad\water9-work\run.ps1 -Name x` (it waits for the game, the harness's marker and the main tree's `-GpuCheck`),
then `states.py <test log> <native log>` joins each state with its status line. It prints numbers and judges only that
no frame failed. Knobs: `TGPT_TEST_LANDED_BED` (the bed's albedo), `TGPT_TEST_LANDED_SECONDS` (a state's length; 23 for
a whole status line), `TGPT_TEST_LANDED_PART` (`still` or `ripples`).

### Part 1: small things under 40 cm of still water, seen from straight above, the sun 57 degrees up

A box of 10 cm, a bar 2 cm thick lying on the bed, a bar 1 cm thick 3 cm over the bed, a glossy box. Each part's
light against the same picture with the row off (the straight sun), where still water should read 0.978 (Fresnel).
The pictures side by side: `scratchpad\water9-work\landed-still.png` (the row off; the row on, with the dark ring
round the box and the thin bar black; trial 1; trials 1 and 2).

On a dark bed (albedo 0.05: the light the bed throws up and the surface mirrors back is small there; runs
`final-dark`, `final-dark2`):

| Part | the row on | trial 1 | trials 1 and 2 | should read |
|---|---|---|---|---|
| the open sunlit bed | 0.780 | 0.780 | 0.945 | 0.978 with both (without trial 2 about 0.80: a fifth of the dark bed's straight light is its highlight) |
| the box, 10 cm | 0.59 | 0.77 | 0.89 | 0.978 |
| the glossy box | 0.61 | 0.80 | 0.92 | 0.978 |
| the bar 2 cm thick on the bed | 0.48 | 0.58 to 0.59 | 0.79 | 0.978 |
| the bar 1 cm thick over the bed | 0.12 | 0.26 to 0.27 | 0.39 to 0.46 | 0.978 |
| the sunlit bed within 3 cm of a thing | 0.69 | 0.74 | 0.92 | the open bed's number |
| all the bed within 3 cm of a thing (sunlit and shadow) | 0.71 | 0.78 | 0.975 | the open bed's number, a little over (the bent light's shadows are shorter) |

On the bed as first run (albedo 0.4; run `final`), the log's sums and the cost:

| State | triangles a frame | on split triangles / landed | the caustics stage | the frame |
|---|---|---|---|---|
| the row off | none | | 0 | 0.88 ms |
| the row on | 17,739 | 0.8 % / 99.2 % | 1.30 ms | 2.22 ms |
| trial 1 | 18,176 | 0.8 % / 100.0 % | 1.31 ms | 2.22 ms |
| trials 1 and 2 | 18,175 | 0.8 % / 100.0 % | 1.31 ms | 2.23 ms |

**What the bright bed showed besides (the third finding):** there the open bed read only x0.798 with the row on,
x0.820 with both trials, where the dark bed reads x0.945. The difference is the light the bed throws up and the
water's underside mirrors back down (total reflection; with a bed of albedo 0.4 it adds a quarter). With the row off
that light is there; with the row on only 15 % of it, because a path's second point on the bed lies 1 to 3 m from the
first, mostly outside this scene's narrow view, and **no landed triangle is written outside the camera's view (times
1.5)** (`in_view`: "nothing reads it there yet"). A path under water does read there. In the game's wide view the
loss is smaller and sits towards the screen's edges and the camera's feet; it was not measured there.

### Part 2: ripples over a flat bed: section 5's table.

### What ran

| Run (log `%TEMP%\tgb\water9-test-<name>.log`) | Build | Result |
|---|---|---|
| `landed1` (07:39; the first start made the passes for 5 minutes) | trials 1 and 2, before the bars were told apart | all frames complete; the first numbers |
| `bed005`, `bed005b` (dark bed, the still part) | the same | the bright bed's shortfall found to be the light from outside the view |
| `ext005` | with the part kept full towards another thing (taken out again) | the box 5 % better, the bars the same |
| `final-dark` (08:17), `final` (08:18, the whole check) | the copy, DLL 8AD371D2... | section 7's and section 5's tables; "landed test frames complete without GPU errors" |
| `final-dark2` (08:25) | the copy as it is, DLL D5E456FF... (a comment more) | the same numbers |
| `python tests/source_checks.py` | the copy | 99 of 99 |
| `python tests/harness_analyze_test.py` | the copy | OK |
| the plugin: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472 -p:TargetFrameworks=net472 -o %TEMP%\tgb\water9-plugin` | the copy | 0 warnings, 0 errors |

**NOT run, and whose it is:** the whole GPU test and ctest (never in a worker); the water's own fixture and the sea's
(their code paths are touched only by the counters and, with the switches off, by nothing: the coordinator's whole run
covers them); the NRD build; mutation checks; anything in the game.

## 8. What the trials do not do, and what would (plans; none built)

| Left | Why | What would do it |
|---|---|---|
| **What is thinner than a cell** (crab legs): 0.27 to 0.58 of its light with trial 1 | Where no ray of the grid lands on a thing, the triangle is whole, lands on the bed behind the thing, lights the bed in the thing's shadow and never lights the thing. No handling of split triangles can mend that: nothing is split. | **The point asks the sun itself.** Where a point under water finds no whole landed triangle (a thin thing, a rim, a shadow's edge), it asks for the sun through the waves directly, as a point inside the water's body already does (`water_sun`, water-7 and water-8: every way through the waves summed, its own shadow rays). Exact for any thinness, and the landed triangles stay the cheap answer for the open bed. It needs `water_sun` to give the light's direction too (for the cosine and the highlight), a mark on each landed triangle (whole or part, the sun's or a lamp's; the record has 4 spare bytes left), and it costs `water_sun`'s time at those points only. Lamps would still come from the landed parts. |
| The same, by brute force | | Finer cells near the camera (1 cm or 0.5 cm: 4 or 16 times the rays of the finest level). A budget, never a limit; but it only moves the size below which things go wrong. |
| A rim short of light: a 10 cm box at 0.91 with both trials | A part's cut hangs over the thing's edge, and what hangs over is read by nothing. | The same asking of the sun at those points. (Keeping the part full towards the edge was tried and taken out: section 2.) |
| **The bed's light from outside the view** (section 7): the light thrown up and mirrored back down | `in_view` leaves out every landed triangle outside 1.5 times the view. | Write them. The cost is triangles: the levels already cover the water all round the camera, so about four times as many near the camera. Or write them within the reach a path under water has (a few times the depth). Either way a path must not find the light gone because the camera looks elsewhere. |
| Lines of light in the shallows | The game's water has no ripples short enough (section 5). | Nothing that is not a fake, unless the game's water gets a finer band of waves: the owner's call. |
| The rippling light in play with a denoiser | Not measured: a pattern of a few percent that moves is likely averaged away over the frames a denoiser gathers. | A look in the game with NRD in motion, once the pattern is wanted. |

Noted, not built (the brief's step 4), where I met their cause:
- Only the sun lights the water's body: a point where a path scatters inside the water asks the sun alone
  (`water_sun`); a lamp reaches it only when the scattered ray happens to meet it. The bed does get the lamps (the
  caustics pass takes the two strongest lights of a tile and one of the rest).
- No foam at the shore, the far sea pale: their causes were not on my way.

## 9. Workarounds and stand-ins met (AGENTS.md: listed, removed or not)

| What | Where | Removed? |
|---|---|---|
| A triangle whose corners met different surfaces is left out with its light | `pt_caustics.hlsl` `emit_triangle` | ON TRIAL: landed by trial 1. To be removed (the switch too) once the game says the direction is right. |
| A triangle with a corner that landed nowhere is left out, with the light of its corners that did land | the same | The same switch lands it. |
| Under water the sun's and the lamps' light is diffuse only, by the landed triangle's plane | `pt_water_path.hlsli`, `caustic_light` | ON TRIAL: trial 2. The same for the camera pixels' caustics in `pt_compose`: NO, not touched. |
| **A landed triangle out of the camera's view (times 1.5) is not written** | `in_view` | NO. Its comment says nothing reads there; paths under water do (section 7): light that depends on what is on screen. An entry for PT-REQUESTS (the coordinator files it). |
| Caustics are read only by camera pixels and by paths under water | `pt_compose`, `pt_water_path` | NO: a mirror or a bounce ray that meets a wall above the water sees no light mirrored up by the water. Not this job's. |
| The grid's cells (2 cm, doubling outwards) decide what small thing gets light | `tracer.cpp kCausticCell` | NO: section 8's first row. |
