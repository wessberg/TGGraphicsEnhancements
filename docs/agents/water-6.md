# Water 6 - the tracer's water in the game is a flat milky sheet (PT-81, PT-80, PT-14)

Copy: `wt\water6` (base `wt\base54`, ABI 0x00012600, not changed). Worker: Claude. 2026-10-08, about 12:35 to 14:25.
Nothing was written to the main tree or the game folder; the game was never started; nothing deployed or committed.
Builds: `%TEMP%\tgb\water6-*`. Scripts: the session's `scratchpad\water6-work\` (`build.ps1`, `run.ps1`, `chain.ps1`,
`foam_cpu.py`).

## Read this first

**Built, compiled, checked by the source checks and by arithmetic on the CPU. NOT RUN ON THE GPU: not one frame.**
The coordinator's game run held the card until 13:47; my first two runs of the fixture (14:04, 14:05) were ended from
outside 7 and 8 seconds after they started (exit -1, their logs stop in the start-up wait, Windows' log has no crash of
them: void, not failures); at 14:10 the coordinator stopped all GPU tests for the owner. So by the brief's own rule the
cause below is a **suspect that the fixture has not yet shown**. What stands behind it is in section 3. The tests that
are owed, with their command lines, are in section 11.

1. **The white sea (suspect, strong).** The foam's shader adds the waves' "Jacobian" over at most two bands. The game's
   own threshold for a three-band sea is made for the sum over three. So the foam's cover came out as 1 at every point of
   the sea, at rest, to the horizon: a white veil over 45 % of every pixel, in the foam texture's cloud pattern.
   One loop bound is changed. Rivers (2 bands) and lakes (1) were never hit; the cave lake has 3 bands but its foam is off.
2. **The lantern's light off the lake** is there in the game's frames, and small where the lantern itself lights the
   rock: +1 % on the lit face, +17 to 27 % on the dim rock beside it. Water mirrors 3 % at that angle. The run did not
   take the scenario's shaded-lantern step, which is the one that can judge it. One defect found by reading and changed:
   a lamp inside its own opaque emitter (the harness's lantern is one) lost its whole caustic in about one frame of five.
3. **The caustics stage where no water is in view** (12 ms in the forest): every corner of its grid over the open sea's
   level, which runs under the whole land, traced two full rays whether or not any light reached it. Now only lit corners
   and their neighbours do. The light that lands is the same. How much time it saves is not measured yet.
4. **A mirror's water is the camera's** by the code and by the offline checks. `mirror-water`'s 0.62 is two other things
   (section 9).

## 1. What the logs hold, and what they do not

The brief says the run's `Water:` lines carry the game's foam amount, intensity and colours. They do not. All the
plugin logged of a surface was one line: `Water: "Ocean" has its waves in the tracer (3 bands, 64x64, the waves move up
to 0.665 m)`. So from run `20261008-083001` the game's own numbers for the sea are:

| What | Value | Where |
|---|---|---|
| bands of "Ocean" | 3 | `LogOutput.log` line 609 |
| each band's wave texture | 64 x 64 | same |
| the waves' largest move | 0.665 m, so their height 0.2956 m (HDRP: the move is 2.25 x the height) | same |
| rivers | 2 bands, 5.968 m (4.559 m in Cuanacht) | lines 611, 613 |
| lakes ("BigLake", "BloodLake") | 1 band, 0.185 m | line 612 |
| the cave lake ("WaterSurfaces_Dungeon_SmugglersBay_01") | 3 bands, 0.032 m | run `20261008-111121`, line 1854 |
| the foam texture | 2048 x 2048, format 77; mean 123, 114, 142, 89 of 255; red 90 to 165 | run `20261008-124239` |
| exposure, gain | log2 -12.66; "traced light x1.0 ... (0.0 EV)" | `water-day/2-traced-none/step.json` |

Not in any log: the sea's foam amount and intensity, its foam colour and tiling, its scattering and refraction colours,
its absorption distance, its bands' sizes and fades. **The plugin now logs them, once a surface (section 6): the next
game run has them.** Until then the fixture uses HDRP's defaults for those, read from the game's own HDRP build, and
says so in its header.

The evidence picture itself, measured (`game-vs-traced.jpg`, display values made linear; the game's tone mapping is in
them, so they are rough): the game's sea is 0.39 times as bright as the dry sand in the middle of the picture and 0.73
times far out; the traced sea 4.1 and 3.3 times. Sand and sky are the same in both halves (0.064 and 0.068, 0.118 and
0.119). So the traced sea is about ten times too bright, and 2.3 times as bright as the sky over it: it is not a
reflection of anything, it is something white lit by the sun.

## 2. How the game makes its foam number (the constants the plugin reads)

Read from the game's own `Unity.RenderPipelines.HighDefinition.Runtime.dll` (ilspycmd, into the session's scratch only:
`water6-work\watersystem.cs` and `watersurface.cs`, two classes; nothing of them is in the tree; delete them when done).
I also tried to fetch Unity's public HDRP shader sources; the permission system refused the download, so I did not read
them. What the water SHADER does with these constants is taken from the constants themselves and from what the earlier
water jobs wrote down from that source (`docs/agents/caustics-2.md`, `water.md`).

- `_SimulationFoamAmount` (the plugin's `FoamAmount`, the shader's foam threshold) depends on the surface's band count:
  **12 x (0.8 + 0.28 a)^0.25 with three bands, 8 x (0.72 + 0.28 a)^0.25 with two, 4 x (0.72 + 0.28 a)^0.25 with one**
  (a: the surface's foam amount, 0 to 1; HDRP's default 0.3). So 11.35 to 12.23, 7.37 to 8, 3.68 to 4.
- That is 4 a band: the simulation's Jacobian (the wave buffer's third channel) is 4 where the water is at rest, and the
  threshold is made for its sum over ALL of a surface's bands. (The tracer's own stand-in for a band faded out by
  distance is 4, which agrees.)
- `_SimulationFoamIntensity`: the surface's wind curve at (its wind speed / 69.4), clamped to 0..1; 0 for a pool-type
  surface or with foam off.
- `_WaterAlbedo` is the surface's scattering colour as authored (no conversion); `_WaterExtinction` is
  -ln(0.02) / absorption distance x max(1 - refraction colour, 0.01): the plugin's own formula is the same.
- HDRP's defaults (the game's build): scattering (0, 0.27, 0.23), refraction (0, 0.45, 0.65), absorption distance 5 m,
  foam colour white, foam tiling 0.2, foam smoothness 0.3, foam amount 0.3; the sea's bands are 500 m (its repetition
  size), 500 / 7.37 m and 10 m wide and fade from 1500 m over 3000 m, from 300 m over 800 m, the ripples from 50 m over
  200 m; smoothness 0.95 near, 0.85 far.

## 3. The white sea: what stands behind the suspect

`pt_water_foam.hlsli` added the Jacobian over `min(bands, 2)` bands ("HDRP's simulation foam uses its one or two swell
bands"). The game's threshold for the sea counts three. So for "Ocean":
cover = saturate(11.35..12.23 - (4 + 4)) = 1 at every point, at rest, to the horizon.

**The shader's foam function worked on the CPU** (`water6-work\foam_cpu.py`: its arithmetic ported line by line, run over
the fixture's own waves, Jacobians and foam texture; 400,000 points of the sea each):

| The sea | Foam amount (threshold) | Summed over 2 bands (before) | Summed over 3 (now) |
|---|---|---|---|
| 6 to 60 m out | 0.0 (11.35) | cover 1.000; foam 0.451 on average, at 100 % of the points | cover 0; no foam |
| 6 to 60 m out | 0.3 (11.64) | cover 1.000; foam 0.451, at 100 % | cover 0.004; foam 0.006, at 3.5 % of the points, up to 0.41 |
| 6 to 60 m out | 1.0 (12.23) | cover 1.000; foam 0.451, at 100 % | cover 0.256; foam 0.241, at 82 % |
| 150 to 900 m out | 0.3 (11.64) | cover 1.000; foam 0.450, at 100 % | cover 0.001; foam 0.001, at 0.5 % |

So before: 45 % foam everywhere, whatever the surface's foam amount, near and far, following the foam texture's blue
channel (a soft cloud pattern). That is the picture. Now: foam on the crests that compress.
**Note the last-but-one line.** With the surface's foam amount near 1 the game's own threshold (12.23) stands over the
sum at rest (12): a thin veil everywhere by HDRP's own constants. Whether the game's sea has such an amount, the next
run's log line says ("HDRP's threshold": over 12 means a veil at rest). If it has, and the game still shows none, the
next suspect is the erosion maths (the last jobs', not checked here).

**The change:** the sum runs over every band (`for (uint band = 0; band < uint(info.x); ++band)`). Nothing else in the
foam was touched.

What was ruled out, with the number that rules it out:
- (c) the automatic gain and its hold: that run's gain was x1.0 (step.json), and `highlight_hold` returns 1 unless the
  automatic part is over 1 (`if (!(automatic > 1.0)) return 1.0;`). It did nothing there.
- The sky's reflection: the traced sea is 2.3 times the sky's own brightness; a reflection cannot be.
- The water's colour: its scattering colour and absorption reach the tracer as HDRP holds them (section 2); a body that
  scatters 0 to 27 % of the light cannot be pale.

**A thing to look at, not mine to judge:** the erosion lines of `pt_water_foam.hlsli` (the four at its end, with their
constants 0.05, 0.55, 0.1, 0.9, 5, 0.1, 0.225, 32) read like a port of HDRP's own foam erosion. I could not compare
(no source read). The rules say nothing is copied from HDRP's shaders: someone who may read that source should compare
and rewrite it if so. Likewise the fixture holds HDRP's threshold as one line of arithmetic (12 x (0.8 + 0.28 a)^0.25),
the number the plugin reads; if that is too close, replace it by its three values (11.35, 11.64, 12.23).

## 4. Coordinator's messages

- The first: the "33.7 ms" in the lake check's name is fixed text; the check passes on a free card. Not worked on. Instead:
  why the caustics stage costs 12 ms a frame in a forest with no water in view (section 8).
- 14:10: no GPU test until told. Obeyed: my waiting shell and its queue were stopped at once; no test of mine was
  running; no shell of mine waits. Three tests of the filters worker were running and were not touched.

## 5. The fixture (`tests/water_sea_gpu.inl`; `TGPT_TEST_WATER_SURFACE=1` with `TGPT_TEST_WATER_SEA=1`, 640 x 360)

**It compiles and links. It has never run.** A first run may show a mistake of its own (the Albedo view's reading of a
water pixel, a region's bounds): fix the fixture, not the tracer, if its numbers on the untouched tracer are not the
CPU's (cover 0.45).

A beach sloping into the sea at 8 in 100, two kilometres of sea to the horizon under the test's sky and sun, the camera
2.6 m over the sand looking out and 15 degrees down. The sea as the plugin sends "Ocean": three bands of 64 x 64, moving
up to 0.665 m, the foam texture with the game's channel means (127, 114, 142, 89 here), the threshold for three bands.
Its waves are the fixture's own (a swell of 70 to 160 m, wind waves of 4 to 7 m, ripples), with each band's Jacobian
worked out from the waves' own sideways move in HDRP's convention (4 at rest; here 3.24 to 4.83 in the second band).

| Part | What it prints or checks |
|---|---|
| 1 as sent | foam's cover over the sea within 60 m (the Albedo view), the sea's brightness against the dry sand's and the horizon sky's; checks: cover under a tenth; some pixels with foam, most without; the near sea darker than the sunlit sand |
| 2 foam amount 0 and 1 | check: more amount is more foam |
| 3 foam intensity 0 | check: no foam; prints the bare sea's colour over the sand's light |
| 4 the Caustics row off | printed only |
| 5 cost | each stage's GPU time with the sea open to the sun and four lamps, and with the same sea under ground |
| 6 a lamp in its own emitter | check: the bed's light under a lamp inside an opaque box is over 0.95 of the bare lamp's, the same frames |

Pictures: `water-sea-<name>.png` and `-foam.png`, `water-sea-lamp-*.png`.
Expected on the untouched tracer (`stage-base`): part 1 FAILS with cover about 0.45 and the sea brighter than the sand
(that is the white sea shown); part 6 FAILS at about 0.74. On the changed tracer (`stage`): all pass, cover about 0.006.

## 6. What changed (all in the copy)

| File | Change |
|---|---|
| `native/pathtracing/shaders/pt_water_foam.hlsli` | the Jacobian summed over every band (was: at most two) |
| `native/pathtracing/shaders/pt_caustics.hlsl` | (a) a corner's two rays on from the wave only where it, or a corner it shares a triangle with, is lit (section 8): `trace_corner` is now `corner_light` and `corner_rays` with the group's barrier between, and one more group-shared array (256 uints); (b) a lamp's shadow ray ends where it enters the lamp's own emitter ball (section 7) |
| `src/TGPathTracing/WaterCaustics.cs` | one log line a surface with its numbers: type, bands' sizes, amplitudes and fades, foam (on/off, amount, threshold, intensity, tiling, smoothness, colour, textures), scattering and refraction colours, absorption distance and what it takes a metre, smoothness, HDRP's own scattering terms. And the foam texture is asked as a property first (PT-81's "Could not find field ... foamMask" was the field asked first: harmless, now silent) |
| `tests/water_sea_gpu.inl` | new: the sea fixture |
| `tests/water_surface_gpu.inl`, `tests/pathtracing_gpu_test.cpp` | the fixture's include and its switch (`TGPT_TEST_WATER_SEA`, 640 x 360) |
| `tests/source_checks.py` | the water check names the foam's loop, the fixture and its checks, the plugin's log, the caustics' two changes |

No struct, export, option bit or ABI number changed. No constant buffer changed.
Built: `TGPathTracing.Native.dll` and `tgpt_gpu_test.exe` (Ninja, NRD off), exit 0, staged in `%TEMP%\tgb\water6-stage`
(DLL sha256 4F8F0096...); the untouched tracer with the same test program in `%TEMP%\tgb\water6-stage-base` (DLL
D3B9F769...). The Mono plugin: 0 warnings, 0 errors (`-p:DeployToGame=false`, into the copy's `bin`). Source checks: 95
of 95.

## 7. The lantern over the cave lake (PT-80, PT-14)

Measured from that run's own frames (`20261008-111121\lantern-lake`, steps `1-all-on` and `3-caustics-off`, the mean of
8 frames each, row on over row off):
- the whole picture: 1.046;
- the rock face the lantern lights directly (the brightest blocks): **1.01**;
- the dim rock beside and behind it (flat shelf tops, the far rocks, where the lantern's own light is weak): 1.17 to
  1.27. A map of it: `%TEMP%\tgb\water6-lantern-ratio.png` (red where the row adds light: along the shelves).

That is what water does. The lantern hangs 0.35 m over the water and the lit face stands close behind it and higher:
for a rock point 0.5 m up and 1 m away the light meets the water at about 50 degrees, where water mirrors 3.4 % of it,
over a path 1.3 times as long: about +2 % of the lantern's own light there. Water mirrors much only at a flat angle
(13 % at 70 degrees, 35 % at 80). So with a bare lantern the rock that faces it cannot show rippling light: the lantern's
own light is fifty times stronger there. The scenario's text names a second set-up, "under a shade that keeps the lamp's
own light off the rock"; this run's recipe (`@dots`) did not take that step (its step 2 is missing). **That step is the
one that can judge the caustics**: with the shade the rock holds only the light off the water.

**One defect, found by reading, changed, not yet shown on the GPU.** The caustics pass draws one point inside a lamp's
emitter ball a frame and sends each water corner's shadow ray at it, ended 5 cm short of the point. The harness's
lantern has a glass ball the game draws opaque, 4 cm round a 4 cm light. For a point on the ball's far side the ray is
still outside the ball where it ends, so it meets the ball's own shell. Worked out on the CPU: 19 to 21 % of the points
(18.5 % seen from far). As one point serves the whole frame, the lantern's whole caustic was gone in about one frame of
five: a fifth of its light, and a flicker of all of it. Now the ray ends where it enters the ball (what lies inside is
the emitter). A glowing crystal with the light in its body is the same case in the game itself. The fixture's part 6
shows it or not.

Not ruled out, because nothing here can show it: light lost on rough rock (a landed triangle lights only points within
0.5 to 5 cm of its plane, and one whose corners land on different faces is left out; the pass says so itself).

## 8. The caustics stage where no water can throw light (the forest's 12 ms)

Run `20261008-124542`, the native log's status lines: the candle room 4.2 ms a frame with 0 triangles landed, the forest
12.3 ms with 21,000, the vista 6.7 ms with 47,000, the moving-hair scene 8.5 ms with 247,000. The cost does not follow
what lands.

Why it runs: the open sea is "everywhere" (the plugin gives the nearest four surfaces slots, and the sea's distance is
0), and its level quad reaches as far as the camera draws, under the whole land. The pass lays its grid round the camera
on every slot's water by the slot's extent alone: 8 levels of 512 x 512 cells, about 7,800 tiles of 256 corners for the
sea. Every corner sent a ray down to find the level surface, a shadow ray to each of the tile's three lights, and then
**two more rays (the reflection and the refraction, each a full nearest-hit with its material) whether or not any light
had reached it**. Under a forest's ground none does. And every tile picks three lights from all of the scene's (994
there), far lamps too: their weight is never 0.

What is done here: the two rays are traced only for a corner that is lit, or that shares a triangle with a lit corner
(the six round it). Every triangle that was written before is written still, with the same corners: a triangle needs
one lit corner to be written, and then all three of its corners are lit or beside a lit one. The light that lands is
the same. What is left of the cost under the ground: the ray down, the shadow rays, the lights' choice. **Not measured**
(the fixture's part 5 prints it, before and after). What would take the rest away without changing the picture, not
built:
- the sea's level ray is not needed (a quad's level surface is a plane inside its extent: one test, no ray);
- a slot whose water no light reaches still takes its tiles: "was any corner of this tile lit a frame ago" would skip
  them, one frame late when a light appears. That is a trade the owner should rule on, not a worker.

## 9. A mirror and water (PT-80's 0.62)

By the code, the water a reflection ray meets IS the camera ray's: the same wave triangles in the same structure, and
one function for its light (`water_path`, called once, from `shade_path`'s end, for a camera pixel's water, a mirror's
and a bounce's alike). The offline checks agree ("a mirror sees the same water boundary and transmitted bed as the
camera", "... the same displaced waves and transport": ratios within 2.5 %). What a mirror must show of water is what
the camera shows: its waves, foam, colour, the bed through it, its caustics.

What `mirror-water` shows is mostly the beach behind the camera, with a sliver of sea at its left edge. Measured on its
sheet (the mirror's picture over the tracer's own view from the mirror's side): the whole 0.92, sunlit sand 1.04, the
tree's shaded trunk 0.64, the cliffs 0.75, the sea sliver 0.63. So two things, neither the water's surface:
1. **Shade is too dark in any reflection** (0.64 to 0.75): a reflection's hit takes its bounced and sky light from the
   radiance cache (`shade_seen`), a camera pixel from its own traced bounce. Not water; for the mirror's owner.
2. **The sea in the mirror lacks the sun under the water.** With the Caustics row on, the sun reaches anything under
   water only as landed caustic triangles, and the caustics pass throws away every triangle that lands outside 1.5 times
   the camera's own view (`in_view` in `pt_caustics.hlsl`: "nothing reads it there yet: mirrors and bounced light don't
   ask for caustics"). They do ask now (`water_path` reads `caustic_light` at whatever it meets under water). So the bed
   a mirror shows, or a bounce meets, behind the camera has no sun. In shallow water under a 45 % veil that would fit
   the 0.63 (not measured apart). This is the screen deciding what is lit, which the owner ruled out. Removing `in_view` is one line, but it
   multiplies the triangles written, built and kept, perhaps by four (the view is a quarter of the round): the sea
   scene already wanted 1.5 million a frame (316 MB of room). **Not changed: it needs a count first and the owner's
   word on the memory.**

## 10. For the coordinator's game run

| Picture | Should show after this change | Will still differ from the game, and why |
|---|---|---|
| `water-day@quick` | a dark sea that mirrors the sky, more towards the horizon; wave shapes; no pale veil and no cloud pattern; white foam only on crests; the bed in the shallows. Its sea against its sand near the game's 0.4 to 0.7, not 3 to 4. The log: one `Water: "Ocean" numbers:` line | no foam at the shoreline (the level's foam generators and water decals draw into a buffer the tracer does not have); the water's body darker and less green in sun (the sun does not light the body, section 12); sun glints needle-sharp (the water's own smoothness is not used); if the log's threshold is over 12, a thin veil by HDRP's own numbers (section 3) |
| `lantern-lake@dots` | as before to the eye: the rock the lantern faces gains 1 to 2 %. The lantern's caustic no longer drops out in a frame of five (if section 7 holds): a little more light on the dim shelves, less flicker there | no rippling light on the lit face: physics. Take the shaded-lantern step to judge |
| `mirror-water` | the sea sliver dark like the camera's sea, not milky | shade in the mirror too dark (the cache); no sun on the bed in the mirror (`in_view`) |
| any scene | the status line's "caustics" stage lower where the sea's level lies under ground (forest: was 12.3 ms) | not 0: section 8 |

## 11. Tests owed (nothing here has run on the GPU)

From PowerShell, the game closed and the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` at 0. `$w` is
`<session scratch>\water6-work`. `run.ps1` waits for the gate itself, sets `TGPT_TEST_STARTUP_SECONDS=900`, writes
`%TEMP%\tgb\water6-test-<Name>.log` and pictures to `%TEMP%\tgb\water6-images-<Name>`.

1. The white sea shown on the untouched tracer (expect the FAIL lines of section 5):
   `& "$w\run.ps1" -Name sea-base -Water -StageName stage-base -Env @{TGPT_TEST_WATER_SEA='1'} -Grep 'SEA|FAIL|PASS'`
2. The same on the changed tracer (expect PASS):
   `& "$w\run.ps1" -Name sea-fix -Water -StageName stage -Env @{TGPT_TEST_WATER_SEA='1'} -Grep 'SEA|FAIL|PASS'`
3. The water's own fixture on the changed tracer (expect `PASS water surface checks`):
   `& "$w\run.ps1" -Name water-fix -Water -StageName stage -Grep 'WATER|FAIL|PASS'`
4. The whole GPU test once on the changed tracer; its FAIL lines against the base's known ones (the candle job's skin
   110 m from the scene origin, the glass index, three tinted-glass checks; the lake's 33.7 ms on a shared card):
   `& "$w\run.ps1" -Name whole -StageName stage -Grep 'FAIL|FAILED|passed|last error|failed frames'`
   The caustics block's checks (the lamps' reflected light adds up, no light where something stands between) are the
   ones that judge `pt_caustics.hlsl`'s two changes beside the fixture's parts 5 and 6.
Without the scripts: `$env:TGPT_TEST_WATER_SURFACE='1'; $env:TGPT_TEST_WATER_SEA='1'; $env:TGPT_TEST_STARTUP_SECONDS='900';
& "$env:TEMP\tgb\water6-stage\tgpt_gpu_test.exe" "$env:TEMP\tgb\water6-stage" "$env:TEMP\tgb\water6-images-sea-fix"`.
No mutation checks were run (the brief's rule). If a run of 1 or 2 does not give the CPU's numbers, the fixture is the
first thing to doubt (section 5).

## 12. What is still not the game's water after this change (known, not built)

- **The sun does not light the water's body.** Inside the water a path scatters and leaves by chance; it finds the sun
  only if it happens to leave straight at its disc (about one path in a hundred thousand), which the clamps then cut.
  So the sea's own colour is the sky's light alone. The right way is the sun's refracted beams as volumes a path can
  ask (the caustics pass already traces them down to the bed); a job of its own. With the Caustics row off the same
  holds: the straight light is given to surfaces under water, not to the water itself.
- **The water's own smoothness is not used** (0.95 near to 0.85 far in HDRP's defaults): the tracer's water is a clear
  mirror facet at the wave normal. The sun's glints on it are single bright pixels where the game's are soft, more so
  far out. Its numbers need four floats a slot (two are free in the struct).
- **Foam at the shore** (the level's foam generators and water decals) is still not shared (the water-4 report).
- **`in_view`**: caustics exist only in 1.5 times the camera's view (section 9).
- A surface's foam mask is handed over whenever the surface holds one, and its scale and offset are read from HDRP's
  constants; with HDRP's decal workflow on those constants mean something else. The new log line says per surface
  whether a mask is read: check it against the game on the first run.
- The sea's bands' real sizes, fades and foam numbers: unknown until the next run's log line.

## 13. Workarounds met (AGENTS.md: listed, removed or not)

| Workaround | Where | Removed? |
|---|---|---|
| "at most two bands make foam" | `pt_water_foam.hlsli` | yes: it was the suspect |
| caustics kept only in the camera's view | `pt_caustics.hlsl in_view` | no (section 9: memory, the owner's word) |
| the Caustics row's "off" as straight, unbent light through the water | `pt_water_surface.hlsli water_passes_light` | no (water 4's decision, under "off" only) |
| a band faded out counts as water at rest (Jacobian 4) | `pt_water_foam.hlsli` | no: it is what the fade means |
