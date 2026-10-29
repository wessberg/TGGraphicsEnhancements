# merge-3: three finished jobs ported onto today's tree (PT-58)

Agent report, 2026-10-07. Copy `<scratch>/wt/merge3`, base `<scratch>/wt/base33` (the main tree at 15:06). Brief:
`<scratch>/brief-merge-3.md`. Nothing ran in the game, nothing was deployed or committed. Porting only: nothing built that
the three jobs did not build.

State: DONE OFFLINE. Built; the final build passes every check of `tgpt_gpu_test` (1269 ok lines, no FAIL); source checks
pass except the three that fail in main itself; the plugin compiles. Not seen in game. Waits for the merge into main.

## In short

- **Subsurface scattering: ported in full.** Light inside wax and skin is traced (the random walk), `pt_sss.hlsl` is gone.
  Its numbers are the job's own to the last digit.
- **The mirror job: only the hero's head fix is ported.** Not its water in reflections (the brief) and not its fog in
  reflections (the coordinator's cut at 16:30: the owner ruled out reading HDRP's fog buffer). The fog had been merged in
  first and was taken out again; nothing of it is left. `trace_closest` is byte for byte the base's.
- **The wet ground: ported** (the store of where the water has been, HDRP's waves as a header, the wet optics at every
  ray's hit and on the camera's surfaces, the hidden row). Not its changes to the water passes. Each of its seven checks
  is now mutation-checked: seven mutants, each caught by the check meant for it.
- **The GPU hang came back with all three together, and is worked around, not understood.** First frame, reflections
  stage, the same words as the mirror job's hang, with `trace_closest` untouched. Seven builds narrowed it: it needs the
  subsurface walk and the wet store's read in the same pass. One flag keeps the wet read out of the walk's loop (which is
  also right: nothing inside a solid thing is wet); with it the whole test passes. It is the only new code in this port.
  The reflections pass stays fragile on this card: the next merges into `pt_common.hlsli` need a GPU test run first.
- **Not this port's:** three source checks and one test program (`tests/neural_gpu_smoke.cpp`) fail from the neural
  rendering work in main; left alone.
- **For the merge:** 18 files changed, 6 new, `pt_sss.hlsl` deleted, against `wt/base33` (list in "Final runs"). The ABI
  number is the coordinator's to set; three new bits, all free on main. `CMakeLists.txt`: three lines.

## Tools

- `<scratch>/m3/merge_into.py <agent> <base> [--dry] [--only=] [--skip=]`: per-file three-way merge (`git merge-file`) of a
  job's copy against its base into `wt/merge3`.
- `<scratch>/m3/port.py`: lists a job's change to a file as hunks and builds the file with only the chosen ones
  (`filter_mirror2.py`, `filter_wedges.py` hold the choices), then merges that; `resolve_wedges.py`, `resolve_both.py`.
- `<scratch>/m3/build.cmd <tag>`: builds `%TEMP%\tgb\merge3-build`, installs to `%TEMP%\tgb\merge3-inst-<tag>`, test exe in
  `%TEMP%\tgb\merge3-exe-<tag>`. `<scratch>/m3/gputest.ps1 -Tags <tag>`: waits for the game, the harness lock and the GPU,
  runs `tgpt_gpu_test`, output `%TEMP%\tgb\merge3-test-<tag>.txt`.
- `<scratch>/m3/mutate.py <group> apply|revert`, `mutants.ps1` (one mutant at a time, up to the end of the caustics block),
  `experiments.ps1` and `quick.ps1` (the hang).

## Job 1: subsurface scattering (`wt/sss` against `wt/base26`)

State: PORTED, all of it. Built (tag `s1`); `tgpt_gpu_test` exit 0, "all path tracing checks passed", 1191 ok lines (the
untouched copy: 1145), no FAIL (`%TEMP%\tgb\merge3-test-s1.txt`).

What was ported: every file of the job (its report's list): `pt_walk.hlsl` new, `pt_sss.hlsl` deleted, `pt_common.hlsli`,
`tracer.cpp`, `extension.cpp`, `tgpt_api.h`, `CMakeLists.txt`, `pt_shaders.hpp.in`, `HdrpPasses.cs`, `SceneCapture.cs`,
`NativeTracer.cs`, `tests/pathtracing_gpu_test.cpp` (block 2c), `tests/source_checks.py`, the CHANGELOG entry (on top of
the TGPT list), the PT-REQUESTS notes (PT-40, PT-43, PT-50), `docs/agents/sss.md`.

Left out: nothing of the job. Two things of its code no longer exist on today's tree and went with the port:
- `g_ownLight` (the job's "no 25 cm floor for the camera pixel's own light at the walk's exit"): main took that floor out
  for every path vertex (PT-50), so the flag had nothing left to switch. Its declaration and its two sets in `pt_walk.hlsl`
  are not ported.
- `clamp_radiance(x, LIMIT_DIRECT / LIMIT_INDIRECT)` in `pt_walk.hlsl` (three places): main replaced the clamps by
  `finite_radiance(x)` (PT-50); `pt_walk.hlsl` uses that now, as `pt_gi.hlsl` does.

Conflicts (12, in 6 files), each resolved by keeping both sides' intent:
| File | Conflict | Resolution |
|---|---|---|
| `CMakeLists.txt`, `pt_shaders.hpp.in`, `source_checks.py` (pass list pin) | main added `caustics:pt_caustics`, the job `walk:pt_walk`, at the same list end | both: `... hair:pt_hair caustics:pt_caustics walk:pt_walk)` |
| `tracer.cpp` x4 (`Passes`, the two `TGPT_PASS` sets, `adopt_passes`) | the same two passes side by side; `sss` renamed `walk` | both passes; `walk` in place of `sss` |
| `tracer.cpp` (after the composite) | the job deleted the two `sss` dispatches; main rewrote the hair layer's dispatch beside them | main's hair lines, the `sss` dispatches gone |
| `pt_common.hlsli` `direct_light` | the job added `&& !g_ownLight` to the 25 cm floor; main deleted the floor | main's (no floor); `g_ownLight` dropped (above) |
| `pt_common.hlsli` `shade_path` | the job: `emitted`, `subsurface_vertex`, `m.tint`; main: `g_firstDirect` replaced by `+ caustic_light` at the first vertex | both: the job's three lines with main's caustic term (the source check's pin the same) |
| `CHANGELOG.md` | both added entries on top | both, the job's first |

Checked after: option bit 268435456 and material bit 2097152 are used by nothing else in `tgpt_api.h`.

Numbers of run `s1` (the job's own run 5 in brackets): wax cylinder's far side 0.995 0.997 1.003 of the reference walk
(the same); with a piece of the same wax inside 0.996 1.000 1.007 (the same); slabs 3 mm 0.995, 10 mm 0.996 0.995 0.994
(0.995); thick flat piece 0.985 0.997 1.000 of the painted one, 22.7 flights a walk (the same); the fold 0.0117 of the lit
wall (the same); the floor behind a wax slab 0.920 with the walk at path vertices, 0.0023 without (the same); the leaf's
share 1.992 0.999 0.341 (the same); skin's shadow edges 0.51, its light 1.012 (the same).

## Job 2: the mirror job (`wt/mirror2` against `wt/base27`): the hero's head only

State: the head fix PORTED. Built (tag `m1`); `tgpt_gpu_test` exit 0, "all path tracing checks passed", 1205 ok lines, no
FAIL (`%TEMP%\tgb\merge3-test-m1.txt`).

**The fog in reflections was ported first, then taken out again.** The brief asked for it; the coordinator cut it at 16:30
(the owner ruled out reading HDRP's own fog buffer, which holds only what the camera sees: the tracer is to light the fog
itself, PT-37). At that moment the fog was merged into this copy with its conflicts half resolved; nothing of it had been
built or tested here. It was removed by putting every file back to its state after job 1 (`<scratch>/m3/undo_fog.py`:
files job 1 merged cleanly from the job-1 merge's own output, untouched files from `base33`, the three with hand-resolved
job-1 conflicts by merging the fog change out again and comparing with the job-1 merge: only job 1's resolutions differ).
A search of the copy finds no `TGPT_SetFogVolume`, `fog_volume_at`, `fog_along`, `TGPT_EVENT_FOG_VOLUME` or
`FogVolumePass` left.

What was ported (the mirror report's section 3, `<scratch>/m3/filter_mirror2.py` picks the hunks):
- `pt_common.hlsli`: `first_person_inside` in place of `first_person_head` (a `TGPT_MATERIAL_FIRST_PERSON` material's back
  faces are passed by `trace_from_surface`'s rays at any distance; a two-sided one is met); `FIRST_PERSON_HEAD` and
  `g_reflectionRay` gone (`shade_seen` no longer sets it); the note above `trace_closest` that names the gap.
  **`trace_closest` is byte for byte `base33`'s** (and the mirror copy's): checked by script, and a source check holds it
  (one `TraceRayInline`, no `first_person` in it).
- `pt_specular.hlsl`: `g_reflectionRay` gone, the comment. `tgpt_api.h`: the flag's text, `TGPT_FIRST_PERSON_HEAD` gone.
- `SceneCapture.cs`: the comment and the log line (one-sided: "rays pass its back faces", or two-sided).
- `tests/pathtracing_gpu_test.cpp`: the sheet 35 cm behind the camera, mirrors at 0.5, 1 and 3 m, another material, from in
  front, two-sided. `tests/source_checks.py`: the first-person pins.

Left out, and why:
- **Water in reflections** (its section 2; the brief): `TGPT_SetWaterLook`, `water_crossing` / `water_pick` / `water_leg` /
  `water_arrive`, `g_waterBody`, `trace_bounce`'s new signature, the loops in `pt_specular.hlsl`, `pt_gi.hlsl` and
  `shade_seen`, `WaterCaustics.cs`, its test block and pins. Built on the water passes PT-60 removes.
- **Fog in reflections** (its section 1; the coordinator's cut): `TGPT_SetFogVolume`, `TGPT_FogVolume`,
  `TGPT_EVENT_FOG_VOLUME`, `fog_volume_*`, `fog_along`, `fog_over`, the `FrameConstants` rows, `TGPT_FOG_VOLUME` in
  `d3d11.hlsl`, the copy and atlas in `tracer.cpp`, `HdrpPasses.cs`'s hook, the F8 line, its test block and pins.
  `fogged` is `base33`'s (the height fog and the distant fog's color: the stand-in, a gap).
- Both stay in the copy `wt/mirror2`, with its report.

Conflicts: one (`tgpt_api.h`: the job deleted `TGPT_FIRST_PERSON_HEAD` on the line job 1 added `TGPT_MATERIAL_SUBSURFACE`
after; both kept). The flag's define line in `pt_common.hlsli` conflicted the same way in the first (fog) merge and kept
its resolution. The 18 conflicts of the first attempt on main were nearly all the water's and the fog's.

The GPU hang: not seen with jobs 1 and 2 (it came with job 3: see there). Run `m1` traced 25,745 frames, none failed;
the reflections stage 0.17 ms in the test's last scene against 0.16 ms in the untouched copy.

Numbers of run `m1` (the mirror job's own in brackets): the inside of the body behind the camera off by 0.004, 0.005,
0.011 from no body in a mirror 0.5, 1 and 3 m ahead (the same); another material, the body from in front, the body drawn
two-sided off by 0.686, 0.672, 0.654 (0.65 to 0.69).

Mutation: the mirror job ran both of its first-person mutants in its own copy (its sets mA and mB: the inside met again;
the body passed from in front too), both caught. Not run again here: the brief asked for the fog checks' mutants, and the
fog is not ported.

## Job 3: the wet ground's memory and optics (`wt/wedges` against `wt/base30`)

State: PORTED, with one flag of new code to stop a GPU hang (below). Built; `tgpt_gpu_test` whole: exit 0, "all path
tracing checks passed", 1269 ok lines, no FAIL (runs `x5` and `final`: "Final runs"; 26,488 frames traced, none failed).

What was ported (`<scratch>/m3/filter_wedges.py` picks the hunks):
- New: `pt_wet.hlsl` (the store's pass), `pt_waves.hlsli` (HDRP's waves at any point: `bicubic`, `water_base`,
  `water_displace`, `water_point`, moved out of `pt_caustics.hlsl`, and `water_height`), `docs/agents/water-edges.md`.
- `pt_caustics.hlsl`: includes `pt_waves.hlsli`, its own copies of those functions gone (the job's move, unchanged in what
  they do). **For the caustics agent's merge (`wt/caus3`): an edit to those functions in `pt_caustics.hlsl` must move to
  `pt_waves.hlsli`.**
- `pt_common.hlsli`: `FrameConstants` `wet`, `wetGrid`, `wetLevels[8]`; `wet_on`, `wet_now`, `wet_offset`, `wet_cell`,
  `wet_dry`, `wet_state`, `wet_albedo`, `wet_material` and their constants; `hit_material` reads them (every ray's hit).
- `pt_prepare.hlsl`: the same on the camera's surfaces (HDRP's G-buffer values, before anything lights them). Not in the
  brief's list by name, but it is the wet ground's and not a water pass; without it the picture's own pixels stay dry
  until the tracer's camera rays replace the G-buffer (then `hit_material` alone serves, and this call goes with it).
- `tracer.cpp`: the store (32 MB), its levels' constants, the clock, the ceiling, the pass before `prepare`, the pipelines
  (`FrameConstants` 7040 to 7200 B). `tgpt_api.h`: `TGPT_OPTION_DRY_GROUND`. `CMakeLists.txt`, `pt_shaders.hpp.in`: the
  `wet:pt_wet` pass, `pt_waves.hlsli` among the shaders' dependencies.
- Plugin: `NativeTracer.cs` (`OptionDryGround`), `Settings.cs` (hidden "Wet ground", on), `WaterCaustics.cs` (the F8 water
  line names each surface's foam settings: four lines, no water pass in them; the job's section 0.2 lists them as staying).
- Tests: the wet ground's block and the store's cost over the sea in `tests/pathtracing_gpu_test.cpp`;
  `test_path_tracing_wet_ground` and the constants' pins in `tests/source_checks.py`.

Left out, and why (the brief: the water passes are being replaced, PT-60):
- `pt_water_under.hlsl` (the bed from HDRP where the traced copy disagrees, the radiance law, the trapped light, absorption
  per path), `pt_water.hlsl` (the rough reflection's weight, `shade_seen`), `d3d11.hlsl` (the shade's per-texel
  absorption): not ported, as told.
- In `pt_common.hlsli`: `hdrp_material_near`, `water_window`, `g_firstUnder*` and the three lines in `shade_path`'s first
  bounce. Only `pt_water_under.hlsl` sets or calls them; without its changes they would be dead code. The job's section
  0.2 keeps the under-water transport for "any vertex under water": it is in `wt/wedges` for when water is a traced surface.
- Their checks (the lamp on the bed with the radiance law, the traced rock over HDRP's lower bed) and
  `test_path_tracing_water_bed_and_radiance`.

Conflicts (10, in 7 files): every one the `wet` pass or `TGPT_OPTION_DRY_GROUND` beside job 1's `walk` pass or
`TGPT_OPTION_NO_SUBSURFACE` on neighbouring lines (the pass lists in `CMakeLists.txt`, `pt_shaders.hpp.in`, `tracer.cpp` x4,
the source check's pin; the two option bits in `tgpt_api.h` and `NativeTracer.cs`), and in `pt_common.hlsli` the wet
functions added right after `SurfaceMaterial`, which job 1 gave a `tint`. Both kept each time
(`<scratch>/m3/resolve_wedges.py`).

Numbers of run `x5` (the job's own runs w5, w6 in brackets; all the same): rock at 30, 450, 1,000 s 11,575 of 11,575 band
pixels on the darkening (mean ratio 0.9967, 1.0033, 0.9913); sand 23,891 of 23,891 (1.0057, 0.9993, 0.9985); rays see the
same rock 11,575 of 11,575 at each age; above the highest water 6,006 of 6,006 and under the water now 1,392 of 1,392
unchanged; the film's mirror 0.00323 with it against 0.00395 dry, 3,000 s on 0.00389 against 0.00396; under HDRP's waves 806
of 806 wet, 7,623 of 7,623 dry above; a full record 462 of 462.

### The wet ground's checks, mutation-checked (none had been)

One mutant at a time (`<scratch>/m3/mutate.py`, the job's own seven, never run there), each built whole and run to the end
of the caustics block, which holds these checks (`%TEMP%\tgb\merge3-test-mW<n>.txt`). Before and after: the same build
passes them all. After the series the shaders hash as before it.

| Mutant | What it breaks | FAIL lines (and only these) |
|---|---|---|
| mW1 | a tint (0.6 A) in place of the water film's optics | "wet rock is darker by a water film's internal reflection and dries in its time" x3 ages, "wet sand ..." x3, "rays meet the same wet ground the camera sees" x3 |
| mW2 | rays don't read the store (`hit_material`) | "rays meet the same wet ground the camera sees" x2 (30 s and 450 s; at 1,000 s the rock is dry either way) |
| mW3 | nothing evaporates | "wet rock ..." x2 (450 s, 1,000 s), "wet sand ..." x2, "rays meet ..." x2, "the rock is wet as high as HDRP's waves reached, and dry above" |
| mW4 | ground under the water now is made wet too | "ground above the highest water and under the water now is not made wet" x3 |
| mW5 | a full record drops its oldest point | "a cell's full record keeps the highest the water reached" |
| mW6 | the store reads the level surface, not HDRP's waves | "the rock is wet as high as HDRP's waves reached, and dry above" |
| mW7 | the film is never chosen | "a film of water just left on the rock is a mirror of its own, gone once it has run off" |

Every check of the wet ground failed under at least one mutant meant for it; no mutant passed. Weak spot, as the job
said: sand is told from rock by slope alone, which nothing here can test against the game.

### The GPU hang came back, and what stopped it

- With all three jobs in (build `w1`), the test's first frame hung the GPU, twice the same: "the path tracer's D3D12
  device hung (its own work took too long) 0x887A0006 ... finished stage hair and eyes of frame 0, and stopped in stage
  reflections" (`%TEMP%\tgb\merge3-native-w1-first.log`). The same words as the mirror job's hang, with **`trace_closest`
  untouched** and none of the mirror job's water or fog code in the tree. On that frame no wax and no water exist: neither
  the walk nor the wet store's read runs.
- Experiments (each one change to `w1`, built, run until the first FAIL line):
  | Build | Hangs |
  |---|---|
  | jobs 1 and 2 (`m1`: the walk, the head rule) | no (whole test passes) |
  | job 3 alone (the job's own copy, runs w5, w6) | no |
  | all three (`w1`) | **yes**, twice |
  | `w1`, the wet read compiled out of `hit_material` (`mW2`) | no |
  | `w1`, the head rule compiled out (`mH1`) | yes |
  | `w1`, the walk compiled out of path vertices (`xS`) | no |
  | `w1`, the store's descriptor index always a real one, never INVALID, while the wet ground is off (`x4`) | yes |
  | `w1`, the wet read left out for what a walk meets inside the mesh (`x5`) | **no** (whole test passes) |
- Read together: it needs the subsurface walk and the wet store's read in the same reflections pass; the head rule plays
  no part; an out-of-range descriptor index read early is not it. `subsurface_walk`'s loop (up to 4096 steps) calls
  `hit_material` for a thing met inside the mesh (a wick, a holder's spike), and `hit_material` now holds the wet read
  (two more loops and a buffer read): with that read inside the walk's loop the pass hangs, without it the pass runs.
  Why is not known. Every loop is bounded and none of this code runs on that frame, so, as the mirror job concluded, the
  likeliest cause is how the driver (RTX 5080, 32.0.16.1692) or DXC compiles this pass. Not proven.
- **The one piece of new code in this port** (`pt_common.hlsli`): `static bool g_hitInside`, set by `subsurface_walk`
  around its `hit_material` call; `hit_material` skips the wet read while it is set. It is also what is right: nothing
  inside a solid thing has a film of water on it, so no picture changes. A source check holds it
  (`test_path_tracing_wet_ground`: the flag in `hit_material`, set once, in the walk).
- **The risk stays.** The reflections pass sits next to this fault: the mirror job met it through `trace_closest`, this
  merge through `hit_material`. The camera-ray switch (PT-59) and the hair and caustics agents' merges add code to the
  same pass. Every merge into `pt_common.hlsli` needs a `tgpt_gpu_test` run first; the fault shows in the first frame
  (`quick.ps1` ends there). Worth doing before PT-59 grows the pass: Device Removed Extended Data and the D3D12 debug
  layer's GPU-based validation in the test (`TGPT_TEST_DEBUG_LAYER`, `TGPT_TEST_GPU_VALIDATION` exist), to learn whether
  it is a page fault or a shader that never ends. Other cards and drivers were never tried.

## ABI, structs and bits in total

`TGPT_ABI` is untouched here: 0x00011E00, main's. The coordinator sets the number. What a plugin and a DLL must agree on
after this merge:

| What | Change | Job |
|---|---|---|
| `TGPT_Material.subsurface[3]` (floats) | was `textureReserved[3]` (uint32, zeros); same place, the struct still 160 B. With `TGPT_MATERIAL_SUBSURFACE`: Burley's shape per channel per metre; with `TGPT_MATERIAL_TRANSLUCENT`: the share of light from behind per channel (zeros: not known). Shader side `MaterialRecord.subsurface` | 1 |
| `TGPT_MATERIAL_SUBSURFACE` = 2097152 (0x200000) | new material bit; free on main (checked) | 1 |
| `TGPT_OPTION_NO_SUBSURFACE` = 268435456 (0x10000000) | new option bit, for testing; free on main; no row sets it | 1 |
| `TGPT_SetDiffusionProfiles` | same call, new meaning: every profile's shape (thin ones too); the fourth float is unused | 1 |
| `TGPT_FIRST_PERSON_HEAD` | the `#define` is gone; `TGPT_MATERIAL_FIRST_PERSON` (1048576) stays with a new meaning (back faces passed at any distance) | 2 |
| `TGPT_OPTION_DRY_GROUND` = 536870912 (0x20000000) | new option bit; free on main; the hidden "Wet ground" row sets it when off | 3 |
| `FrameConstants` (shader constants, not the plugin's ABI) | 7040 to 7200 B: `wet`, `wetGrid`, `wetLevels[8]` at its end | 3 |
| Passes | `sss` gone (and `pt_sss.hlsl`); `walk` (`pt_walk.hlsl`) and `wet` (`pt_wet.hlsl`) new, both in the 6.6 and the 6.9 set | 1, 3 |

No new export, no exported struct's size changed, no instance bit, no render event. **Not** added (left in `wt/mirror2`):
`TGPT_SetWaterLook`, `TGPT_WaterLook`, `TGPT_SetFogVolume`, `TGPT_FogVolume`, `TGPT_EVENT_FOG_VOLUME`; `FrameConstants`
has neither `waterLook` nor the `fogVolume*` rows. The size pins (`tracer.cpp`'s `static_assert`, two in
`source_checks.py`) say 7200.

`CMakeLists.txt` is one of the files the neural rendering session edits in main (the coordinator, 16:25). This port
changes three lines of it, all in the path tracing shader block: `pt_waves.hlsli` among `PT_SHADER_DEPENDS`; the pass
list's end `... caustics:pt_caustics walk:pt_walk wet:pt_wet)`; `sss:pt_sss` out of the list of passes that trace no
rays. Nothing else in it differs from `base33`, byte for byte (the file has mixed line endings: 20 lines of the neural
block end in LF; one of this port's tools had rewritten them as CRLF, `<scratch>/m3/fix_eol.py` put them back and found
no other file touched that way).

## Cost of each job's stages (offline, `tgpt_gpu_test` at 1280 x 720; other agents' GPU tests may run beside)

| Job | Stage | Measured |
|---|---|---|
| 1 | the direct light's stage with wax over the whole view (the walk on every pixel) | 3.82 ms against 0.97 ms without the walk (run `final`; `s1`: 3.84 against 1.09; the job's own: 3.90 against 0.83, 4.61 against 0.92). About 3 ms a full 720p screen of wax; in game wax and skin cover a small part of it. One walk more (about 23 rays) at every path vertex, cache sample and reflection that meets wax or skin: not measured apart |
| 2 | reflections, the test's last scene | 0.17 ms (`m1`, `final`) against 0.16 ms in the untouched copy: one material lookup per back face met |
| 3 | the store's pass (in the prepare stage) over a 120 m sea with waves | 0.32 ms with it, 0.26 ms without (run `final`: 0.06 ms); 0.77 ms with, 0.08 ms without in run `x5` an hour earlier, the same code (0.69 ms); the job's own runs 0.45 to 0.46 with, 0.28 to 0.34 without (0.12 to 0.17 ms). **The three disagree by ten times**, with the test's "without" moving as much as its "with": this number is not known offline, read "prepare" in the harness by the sea. Over the pool: 0.18 against 0.18 ms |
| 3 | the wet read at a hit | four cells of 64 B at every hit below the highest water; none where no water is near |

The game's numbers are the harness's to measure (the native log's stages: "direct light", "prepare", "reflections").

## One in-game scene and recipe per job

- **Subsurface:** scenario `candle`, the default recipe. Look at the melted wax at the candles' feet: its folds and
  pockets no longer black, the wax glowing softly near the flame. In `BepInEx/LogOutput.log`: "Subsurface material
  \"MI_Candle_01\" ..." (missing: the wax names no profile or is drawn two-sided, and nothing is walked). Read the
  exposure and gain line: the wax's light moved.
- **The head:** scenario `mirror-day`, recipe `mirror`. The bright orange bulb on the hero's shoulders (77 px wide at the
  picture's centre) is gone; the armour's collar stays, as in the game's view from the mirror's side. The first frames
  trace (no hang in the reflections stage). No change to the mirror's fog or water is expected: neither was ported.
- **Wet ground:** scenario `water-day`, recipe `caustics` (it has the `anim` steps: the store's clock stands still while
  the world is frozen). Behind each wave a glossy film for about 5 s, then dark wet sand up to the highest reach since the
  save loaded. For the owner's comparison: the hidden "Wet ground" row off. The native log's "prepare" stage: how much the
  store costs by the sea.

## Final runs

- Build `final` (`%TEMP%\tgb\merge3-inst-final`, the copy as it stands, built after the last mutant was reverted):
  `tgpt_gpu_test` whole, exit 0, **"all path tracing checks passed"**, 1269 ok lines, no FAIL line
  (`%TEMP%\tgb\merge3-test-final.txt`). The untouched copy: 1145; after job 1: 1191; after job 2: 1205; after job 3: 1269.
- `python tests/source_checks.py`: 90 tests, **passes except the three** that fail in `base33` and in main alike
  (`test_cmake_inputs_exist_and_shader_test_is_windows_only`, `test_optional_hudless_is_not_fabricated`,
  `test_sliders_pass_a_step_the_interop_accepts`: the neural rendering work in progress).
- The plugin compiles: `dotnet build src\TGPathTracing\TGPathTracing.csproj -c Release -f net472
  -p:TargetFrameworks=net472 -p:DeployToGame=false`, 0 warnings, 0 errors (into the copy's own `bin/`).
- Not built here: `tgnr_gpu_smoke` (`tests/neural_gpu_smoke.cpp`, main's, does not compile under this toolchain).
- Never run offline by any of this: Ray Reconstruction (the harness's to do), the plugin's reading of materials
  (`ProfileOf`, `ReadSubsurface`), the hidden "Wet ground" row.
- Against `wt/base33`, 18 files changed: `CHANGELOG.md`, `CMakeLists.txt`, `docs/PT-REQUESTS.md`,
  `native/pathtracing/include/tgpt_api.h`, `native/pathtracing/shaders/` `pt_caustics.hlsl`, `pt_common.hlsli`,
  `pt_prepare.hlsl`, `pt_shaders.hpp.in`, `pt_specular.hlsl`, `native/pathtracing/src/` `extension.cpp`, `tracer.cpp`,
  `src/TGPathTracing/` `HdrpPasses.cs`, `NativeTracer.cs`, `SceneCapture.cs`, `Settings.cs`, `WaterCaustics.cs`,
  `tests/pathtracing_gpu_test.cpp`, `tests/source_checks.py`. New: `pt_walk.hlsl`, `pt_waves.hlsli`, `pt_wet.hlsl`,
  `docs/agents/sss.md`, `docs/agents/water-edges.md`, this file. Deleted: `pt_sss.hlsl`. Unchanged, though the mirror job
  changed them: `pt_gi.hlsl`, `d3d11.hlsl`, `runtime.hpp`, `DebugCapture.cs`.
- Notes: the mirror job's CHANGELOG entry and its notes under PT-37 and PT-46 now say what was ported (the head) and what
  was not; the subsurface entry and notes are added from its copy; the wet ground's entry and notes are added, saying
  which of that job's work stays in `wt/wedges`; PT-58 has a line for this port.

For the coordinator's merge into main: the hair agent (`wt/hair6`) and the caustics agent (`wt/caus3`) change
`pt_common.hlsli`, `tracer.cpp` and likely `pt_caustics.hlsl` too. After merging them with this copy: build, and run
`tgpt_gpu_test` before anything else (the hang shows in its first frame). The wave functions now live in
`pt_waves.hlsli`.

## Log

- Start: `merge3` equals `base33` (0 files differ). Files each job changed against its base: sss 13 changed, 2 new
  (`pt_walk.hlsl`, its report), 1 deleted (`pt_sss.hlsl`); mirror2 17 changed, 1 new (its report, already in main); wedges
  17 changed, 3 new (`pt_waves.hlsli`, `pt_wet.hlsl`, its report).
- Baseline build of the untouched copy (tag `base`): `tgpt_gpu_test` exit 0, 1145 ok lines, no FAIL.
  `tests/neural_gpu_smoke.cpp` (new in `base33`, the neural rendering work in progress, not this job's) does not compile
  under this toolchain (`std::stoul` without `<string>`): `build.cmd` keeps going past it and fails on anything else.
  Three source checks fail in `base33` and in the main tree alike, from the same half-done work (the coordinator, 16:25):
  `test_cmake_inputs_exist_and_shader_test_is_windows_only`, `test_optional_hudless_is_not_fabricated`,
  `test_sliders_pass_a_step_the_interop_accepts`. Not touched.
- Job 1 merged, built (`s1`), tested: passes.
- Job 2: fog and head merged (conflicts half resolved), then the coordinator's cut (16:30): fog taken out, head only;
  built (`m1`), tested: passes.
- Job 3 merged (10 conflicts, all the wet pass beside the walk pass), built (`w1`): **the GPU hangs on the first frame, in
  the reflections stage** (twice, the same). See "The GPU hang came back".
- Experiments `mW2`, `mH1`, `xS`, `x4`, `x5`; `x5` (the wet read kept out of the walk's loop) runs, whole test passes.
- The plugin compiles. Notes in CHANGELOG and PT-REQUESTS written.
- The seven wet-ground mutants, one at a time (paused twenty minutes while the coordinator's harness had the game open):
  all caught. Shaders hash as before. Build `final`: whole test passes.
- `CMakeLists.txt`'s 20 LF lines, which a tool of mine had rewritten as CRLF, put back; no other file was touched so.
