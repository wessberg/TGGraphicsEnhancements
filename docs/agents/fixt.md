# fixt: offline checks for the plants' stale range number (PT-125) and the mesh decal's stale address (PT-134)

Worker "fixt", 2026-10-12 04:25 to 07:05. Copy: `%TEMP%\tg-agents\wt\fixt` (base `base20n`). Brief:
`docs/agents/briefs/brief-fixt.md`.

## The answer in five lines

- `TGPT_TEST_PLANT_SHIFT_ONLY=1` (PT-125): PASSES on the fix (0 pixels of 921,600 differ, 9 of 9 plants stand) and
  FAILS on its mutant, `pt_sway.hlsl`'s old number (24,010 pixels differ, 1 of 9 plants stands; the device stands).
- `TGPT_TEST_DECAL_MOVE_ONLY=1` (PT-134): PASSES on the fix (the decal still painted after the old memory went back
  to the driver, 191 frames, none failed) and FAILS on its mutant, `upload_decals`' old early way out: cleanly, on
  the CPU side, in the frame of the compaction, with NO reset and no read of freed memory.
- Each run takes 8 to 16 s. Run on 2026-10-12 06:28:39 to 06:29:33 with our game closed and no game of the owner's
  open (`-GpuCheck` exit 0 before each start). The cross runs pass too: the sway mutant passes the decal mode
  (06:48), the decal mutant the plant mode (06:59): each mode fails for its own fault alone.
- Changed: two new files of checks, ten lines in the test, four hunks in `scene.cpp` (one test switch that only
  writes two log lines), twenty lines in one source check. No shader, no export, no struct, no ABI number.
- Not run: the decal mutant "through" to the reset itself (not needed, and not wanted; see "Not done").

## State when stopped

(the newest line first)
- 07:05 DONE. The last code's four runs, the two cross runs and the close-ups are in "Results". `python
  tests/source_checks.py`: 118 tests OK. No mutant is applied in the copy (`mutant.py status`: clean;
  `pt_sway.hlsl` is the base's byte for byte). No process of mine is left: the last chain ended by itself at
  07:00:10 ("chain done").
- 06:29 the four runs on the last code: both modes pass on the fix and fail on their mutants.
- 06:02 the first full round (05:26 to 05:28) had already shown the same; after it the plant mode's materials became
  two-sided and its close-ups were added, the decal mode's summary line was tidied, and all four were run again.
- 05:02 two starts of the plant mode (04:50, 04:54) were ended by my own script when Starfield opened (04:51:30,
  04:59:57), both while the test was still MAKING ITS PASSES: this exe's first start had the driver make the
  reordering passes (`giSer`: 250 s by the tracer's log). So a "warm" job (`TGPT_TEST_STARTUP_ONLY`: the passes are
  made, no frame is traced; started only when `-GpuCheck` says so, below normal priority) went first. It is the
  one kind of job my script does not end when a game opens (it would lose the driver's minutes again); the three
  that ran (05:15:36, 05:27:18, 05:27:53; 6 to 8 s each) ran with no game open. Every run that traces frames is
  ended at once when a game of the owner's opens, and run again later.
- 04:45 written and built; our game was running (the coordinator's runs) from my first look at 04:25 on.
- 04:27 the plan.

## What was added

| File | What |
|---|---|
| `tests/pathtracing_plant_shift_checks.inl` (new) | the mode `TGPT_TEST_PLANT_SHIFT_ONLY=1` (PT-125) |
| `tests/pathtracing_decal_move_checks.inl` (new) | the mode `TGPT_TEST_DECAL_MOVE_ONLY=1` (PT-134) |
| `tests/pathtracing_gpu_test.cpp` | the two includes, right after the warm-up (after the PSR mode's include): nothing of the whole test runs before them, and the whole test takes neither |
| `native/pathtracing/src/scene.cpp` | ONE test switch, `TGPT_TEST_SCENE_ORDER` (read once a frame in `record`, as `TGPT_TEST_MEMORY2_OFF` is): with it the scene writes two log lines, each only when it changes. Four hunks: the switch's variable after `cutsTest`; its read in `record`; the decals' line after the call of `upload_decals`; the plants' line in `pose_plants` after the ranges' upload |
| `tests/source_checks.py` | twenty lines at the end of `test_kept_numbers_cannot_go_stale`: the two modes are wired under their switches before anything the whole test runs, each "ends here" at its first failed stage, and the scene's switch is read once and its two blocks only say (no `list->`, `upload_`, `heap.`, `release.`, `barriers.`, `Dirty_`, `=true;`) |

No export, no struct, no ABI number, no shader, no option or material bit changed. Without the switch nothing is
said; with it nothing that is recorded or traced changes (two `log` calls). No test-only export was needed: the
tracer's log is how the memory-2 and megaplants modes read what no export has, and these two do the same.

The two lines (the checks parse them):
- `PT test, the plants' groups in this frame's order: 2 | x 0 z -16 serial 4 members 0+2 ranges 0+3 | x 0 z 0 serial 3 members 2+9 ranges 3+12`
  (each group in use: its cell's corner, its serial, where its member and range records start in this frame's
  buffers and how many it has).
- `PT test, mesh decals' top levels: 1 named by a record, 0 built with an address that is not their mesh's now | decal 0 mesh 8 at 0x375604e00 built with 0x375604e00`
  (each mesh decal's top level a record names: its mesh's bottom level's address now, and the address the top level
  was built with; read on the CPU side after `upload_decals`, whichever way that took out).

## 1. `TGPT_TEST_PLANT_SHIFT_ONLY=1` (PT-125)

- The scene: a floor; group B, six tufts of grass and three bushes (a stem and a crown, two ranges each: twelve
  ranges, nine members) in the cell of x and z 0 to 16, in front of the camera; group A, a bush and a tuft (three
  ranges) in the cell BEFORE it in the frame's order (z -16 to 0; the groups go by `(cellX, cellZ)`), behind the
  camera, where no camera ray meets it. Plain two-sided materials (no cut-out: no micromaps, no waiting for them).
- THE WIND'S CLOCK RUNS, and that is what makes the check see the fault: `pose_plants` poses a group only when the
  wind's time, the camera or the winds changed. With the wind held still B's pose is KEPT, the old number is never
  read, and the mutant passes. So every run uses the same frame numbers and the same wind time a frame, and
  pictures are compared at the same frame.
- Four runs of the same 48 frames, each from groups made anew:
  - the yardstick: B alone the whole time; and once more (what two runs of the same frames differ by);
  - the shift: B alone 16 frames (B made, its numbers written), A there 16 frames, A gone 16 frames;
  - the walk: A and B there from frame 0 (B made BEHIND A), then A gone for good: the order of the game's resets.
- Judged, with the tracer's own camera rays and no denoiser, on geometry alone: the first hits' colour
  (`TGPT_VIEW_ALBEDO`) at frames 30 and 46, pixel by pixel against the yardstick at the same frame (no more pixels
  off than two runs of the yardstick, plus a hundredth of a percent of the picture). And a bound that needs no
  yardstick: each of B's nine plants shows where the scene put it (at least ten pixels that are not the bare
  floor's within 16 pixels of its middle on the screen; the plants stand clear of each other there).
- What the tracer says is checked too: A's group FIRST in the frame's order, B's ranges at 0, then 3, then 0, and
  B the same group throughout (its serial: a group made anew would write its numbers anew and show nothing).
- Controls: the yardstick's two shots differ (the wind moved B between them: the pose is made every frame); one
  bush 0.3 m off differs.
- THE MUTANT: `pt_sway.hlsl` with the old number (step 4 stores `grp.rangeFirst + lo`, step 5 reads it as a place
  and takes anything but INVALID). `mutant_check.py`: with comments left out it is the pristine `base20i`'s file
  line for line (219 lines of code). With A before it, B's vertices read A's ranges and B's own one off.
- The mode ENDS AT ITS FIRST FAILED STAGE. For the mutant that is "A appeared", where every record it reads is a
  live one of this frame (A's three ranges, then B's own). It never reaches "A went" after a group made behind A,
  where the old number points PAST the frame's records: the read that reset the GPU in the game. So the walk
  stage runs on the fix only, on purpose; the mutant shows that the same comparison fails on the fault.
- `TGPT_TEST_PLANT_SHIFT_LOOK=1` with it: close-ups of B's nine plants, saved, never judged.

## 2. `TGPT_TEST_DECAL_MOVE_ONLY=1` (PT-134)

- The scene: the decals mode's floor and ONE mesh decal on it, whose mesh is registered in this mode (so its bottom
  level is new: built at full size in the builds' own chunk, compacted a frame later here, two or three in the
  game). The decal is handed over BEFORE its mesh is converted, as a scene arrives in the game; from then on
  nothing is registered and the decals are not touched: every frame is quiet.
- THE STALE ADDRESS IS SHOWN ON THE CPU SIDE, WITHOUT A READ OF FREED MEMORY (the brief's "first look whether"):
  after every frame the mode reads the tracer's line. The address the top level was built with must never differ
  from its mesh's.
  - The fix: in the frame of the compaction the line shows the mesh at a new address AND the top level built with
    it (built again in that same frame).
  - THE MUTANT (`upload_decals`' early way out without `!moved`: the line as `base20i` has it): the line shows the
    mesh at the new address and the top level still built with the old one. The mode fails there AND ENDS. The old
    piece is given back at that very frame's fence and its chunk 120 collects later, so the frame just traced read
    memory that was whole, and no later frame is traced. No reset is needed to show the fault, and none happened.
- Then, on the fix only: frames until the tracer's own arenas line says the full-size builds' chunks went back to
  the driver (`TGPT_TEST_MEMORY2_OFF=0`: nothing switched off, the line said at every change), thirty more, and
  the picture: the floor inside the decal is the decal's red; the same as with the decal handed over anew (its
  top level built now); and, the control, bare once the decal is taken away.
- After EVERY frame the status is read: a failed frame, an error or "disabled" ends the mode at once with "the
  device was lost" (the tracer's own fence wait ends a hung frame, so the test does not hang).
- The mode also fails when the order of events it needs did not happen (the mesh never moved, the chunk never went
  back): a pass means the stale address would have been there.

## Results

### The last code, 2026-10-12 06:28:39 to 06:29:33

The card: our game closed (its run ended about 06:28), Starfield closed (it opened again at 06:29:35),
`-GpuCheck` exit 0 right before each start. Logs: `%TEMP%\tgb\fixt-runs\<job>.log`, pictures
`%TEMP%\tgb\fixt-runs\images-<job>\`; the chain's own log `chain.log`.

| Run | DLL | Exit | What it said |
|---|---|---|---|
| fix, plant mode | 2B71E69A2DC8 | 0 | 18,807 plant pixels; 9 of 9 plants stand (604 plant pixels about the least); two runs of the yardstick differ in 0 pixels; the wind moved 1,284 pixels between the shots. With A before B (the tracer: `2 \| x 0 z -16 serial 4 members 0+2 ranges 0+3 \| x 0 z 0 serial 3 members 2+9 ranges 3+12`): 0 pixels differ, 9 of 9 stand. A gone (`1 \| x 0 z 0 serial 3 ... ranges 0+12`): 0 pixels. The walk (B made at ranges 3, serial 6, then at 0): 0 and 0 pixels, 9 and 9 stand. The control: 2,497 pixels. 258 frames, none failed. "plant shift checks passed" |
| SWAY MUTANT, plant mode | C9F372B2A18F | 1 | the yardstick as above (the old number is right while B is alone); with A before B, the tracer's line the same as on the fix: 24,010 pixels differ, 1 of 9 plants stands where it should, 5,436 plant pixels of 18,807. Two FAIL lines, "PT-125's fault", the run ends there; the device stands |
| fix, decal mode | 2B71E69A2DC8 | 0 | the mesh converted in frame 3, the top level built with 0x374600000 (the full-size build in the builds' chunk); frame 4: the mesh at 0x375604e00 AND the top level built with it; the chunk (16 MB) back to the driver in frame 125; frame 155 on: the floor inside the decal red 0.799, green 0.040 (bare 0.502 and 0.502); against the decal handed over anew 0.000000; the control bare. 191 frames, none failed. "decal move checks passed" |
| DECAL MUTANT, decal mode | 99DB0E0EBEA9 | 1 | frame 3 as above; frame 4: `1 named by a record, 1 built with an address that is not their mesh's now \| decal 0 mesh 8 at 0x375604e00 built with 0x374600000`. One FAIL line, "PT-134's fault", the run ends after 4 frames: no frame traced through memory that was gone, no reset |

The extras, each a run of its own after `-GpuCheck` said yes:

| Run | DLL | When | Exit | What it said |
|---|---|---|---|---|
| sway mutant, DECAL mode | C9F372B2A18F | 06:48:44 | 0 | "decal move checks passed", 191 frames, none failed |
| decal mutant, PLANT mode | 99DB0E0EBEA9 | 06:59:39 | 0 | "plant shift checks passed", 258 frames, none failed |
| fix, plant mode with `TGPT_TEST_PLANT_SHIFT_LOOK=1` | 2B71E69A2DC8 | 06:59:58 | 0 | passed; 18 close-ups in `images-fix-plant+look\` (330 frames) |

(A first try of the decal mutant on the plant mode, 06:48:58, ended with -1 at 06:49:53 while still making its
passes: Starfield had opened at 06:49:51 and the test was ended two seconds later, by my script's check or by
another's. No result; it was run again at 06:59:39.)

The pictures were looked at: `images-fix-plant\125-shift-a-before-first-hits.png` shows the nine plants in their
places; the mutant's (`images-sway-plant\`, the same name) one bush and one tuft, elsewhere, and nothing more;
`images-fix-decal\134-decal-move-after.png` the red square on the floor; `images-fix-plant+look\125-look-5-lit.png`
the tuft that was a blade in the first round: whole, leaning in the wind, with its shadow.

### The first full round, 05:26:37 to 05:28:18 (the plant mode's materials still one-sided)

The same four verdicts: fix 0 / sway mutant 1 (21,829 pixels, 1 of 9) / fix 0 / decal mutant 1 (frame 4). Logs in
`%TEMP%\tgb\fixt-runs\round1\`. Its pictures showed one tuft as a blade three pixels wide in EVERY run, the
yardstick too. `pose_sim.py` (the pose redone on the CPU) said 21 pixels. The cause was mine, not the tracer's:
the cards' material was one-sided, that tuft is turned away from the camera, and a camera ray does not meet a
one-sided card from behind (`pt_primary.hlsli`), while its shadow was whole. The megaplants mode's pictures show
thin plants too (`%TEMP%\tgb\megageo6-images\megaplants-merged-1.png`); its materials are one-sided as well, so
likely for the same reason: by reading, not checked by a run. With two-sided materials (as the game's plants have) all nine are whole.

## Builds (all in `%TEMP%\tgb`, nothing in the main tree)

- `fixt-build`: the copy's sources; the 57 shader headers are the main tree's (`build\native\generated\pathtracing`,
  copied 04:26: the copy's 62 shader sources are main's, checked file by file), so no pass was compiled except
  `pt_sway.h` once after the mutant was taken out again (the same bytes as main's: hash 8B2D181A1036).
- `fixt-mutant-build`: the second build folder, for the two mutants; `mutants.cmd sway|decal` puts ONE old piece of
  code back in the copy (`mutant.py apply`, the file's bytes kept), compiles (`pt_sway.h` alone, 1 s; then the DLL),
  stages the DLL and writes the kept bytes back (`mutant.py restore`, a new write time; checked).
- The three DLLs (SHA-256's first 12): the fix `fixt-native` 2B71E69A2DC8; the sway mutant `fixt-native-sway`
  C9F372B2A18F (mutant sway, fixed decals); the decal mutant `fixt-native-decal` 99DB0E0EBEA9 (fixed sway, mutant
  decals). Each stage holds the bridge and the Agility runtime of main's `artifacts\Native` (every run's log has
  "DirectX Raytracing 1.2 (opacity micromaps) available"); `gpu.ps1` checks they are there before every start.
- The pass cache for all runs: `%TEMP%\tgb\fixt-cache` (`TGPT_SHADER_CACHE`), seeded with a copy of the game
  folder's and the main test stage's cache files (keyed by card, driver and each pass's own hash: 55 of 57 passes
  were hits; the two reordering passes are the driver's own cache).
- Scripts, `%TEMP%\tgb\fixt-scripts\`: `build.cmd`, `mutants.cmd`, `mutant.py`, `mutant_check.py`, `gpu.ps1` (the
  one waiting-and-running script), `add_check.py` and `check_mutants.py` (the source check's lines and their 8
  mutants, all caught, in a scratch copy), `project.py` and `pose_sim.py` (where the plants land on the screen).

## How to run them (the coordinator, in the main tree after a merge)

    $env:TGPT_TEST_PLANT_SHIFT_ONLY='1'; build\native\tgpt_gpu_test.exe artifacts\Native build\native\tgpt-images
    $env:TGPT_TEST_DECAL_MOVE_ONLY='1';  (the same, the other switch unset)

Each ends with 0 and "plant shift checks passed" / "decal move checks passed", or with 1 and the failed stage.

The mutants, word for word (`mutant.py` applies and restores them; `%TEMP%` is swept, so here they are):
- sway, `pt_sway.hlsl` `plant`, four places, the code as `base20i` has it:
  - step 4: `const PlantRange r = ranges[grp.rangeFirst + lo];` becomes
    `const uint rangeIndex = grp.rangeFirst + lo;` and `const PlantRange r = ranges[rangeIndex];`
  - step 4: `data.Store(grp.rangeAt + (m.vertexBase + v) * 4u, lo);` becomes `... * 4u, rangeIndex);`
  - step 5: `const uint rangeLocal = data.Load(grp.rangeAt + thread * 4u);` becomes `const uint rangeIndex = ...`
  - step 5: `if (rangeLocal < grp.rangeCount) {` and `ranges[grp.rangeFirst + rangeLocal]` become
    `if (rangeIndex != INVALID) {` and `ranges[rangeIndex]`
- decal, `scene.cpp` `upload_decals`: `if(!decalsDirty_ && !moved && generation==decalShareGeneration_ && ...) return;`
  without `!moved && ` (and `(void)moved;` before it, for the compiler's warning).
A mutant's own mode ends before the stage where the fault would read what is not its own (that is what "ends at
its first failed stage" is for); the other mode never meets the fault (no plants in the decal mode, no mesh decal
in the plant mode). None of the six mutant and cross runs here reset anything or failed a frame.

## Not done, and why

- The decal mutant was NOT run "through" (frames traced until the chunk is gone, to see the reset itself). The
  CPU-side line shows the fault without it, and a reset is what the rule of 2026-10-12 00:20 is there to prevent.
  The mode has no switch for it. So that the stale top level really does hang the GPU is still by reading
  (`docs/agents/stale.md` F1), not by test.
- The plant mode's "walk" stage has no mutant run of its own, on purpose (above).
- SEEN, NOT MINE: `%TEMP%\tgb\reccpu-build\tgpt_gpu_test.exe` (started 04:49:55, before Starfield opened) ran on
  beside Starfield after 04:51:30. I did not touch it. A runner that only asks `-GpuCheck` at the start does not
  see a game that opens during the test; mine ends its test then (`gpu.ps1`: three lines) and runs it again later.

## Learned on the way (for PROJECT-CONTEXT, the coordinator's to file)

- A check of a kept number must make the thing that READS the number run. The plants' pose is skipped while the
  wind's time and the camera stand still: a plant check with a still wind passes on PT-125's fault.
- A new test exe's FIRST start has the driver make the two reordering passes for minutes (`giSer` 250 s here),
  even with every other pass a disk cache hit, and a test ended in that time has to make them again. A first start
  with `TGPT_TEST_STARTUP_ONLY=1` (no frame traced) does it once; later starts take 4 to 8 s, and a whole run of
  one of these modes 8 to 16 s. A pass cache can be seeded from another build's of the same shaders
  (`TGPT_SHADER_CACHE`, files keyed by card, driver and pass).
- One-sided cards seen from behind are not met by camera rays but cast their shadow: a fixture's plants need
  `TGPT_MATERIAL_DOUBLE_SIDED`, or a plant turned away looks like a fault that is not there (the megaplants
  mode's pictures have thin plants, and its materials are one-sided: likely this, by reading).
- In the offline test a still mesh is compacted ONE frame after its build (each frame waits for the GPU).

## For the merge

- Six files against `base20n`: new `docs/agents/fixt.md`, `tests/pathtracing_plant_shift_checks.inl`,
  `tests/pathtracing_decal_move_checks.inl`; changed `tests/pathtracing_gpu_test.cpp` (one hunk of ten lines after
  `#include "pathtracing_psr_checks.inl"`, base line 1478), `native/pathtracing/src/scene.cpp` (four hunks, all
  additions, at base lines 388, 1589, 1926 and 2034; none touches a line the source check quotes),
  `tests/source_checks.py` (one hunk of twenty lines before `def test_ingame_harness`, at the end of
  `test_kept_numbers_cannot_go_stale`). Line ends kept: the two sources and the two new `.inl` LF, the checks CRLF.
- The source check quotes the test's two wiring lines and `orderTest`'s read word for word, and wants exactly two
  `if(orderTest) {` blocks in `scene.cpp`: a merge that moves them must carry that.
- `AGENTS.md`, `BACKLOG.md`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md` are not edited here. Proposed: PT-125
  and PT-134 each "offline check: `TGPT_TEST_PLANT_SHIFT_ONLY` / `TGPT_TEST_DECAL_MOVE_ONLY`, passes on the fix and
  fails on the mutant (`docs/agents/fixt.md`)"; PT-134's "an offline check owed" is paid, "not seen in a scene with
  mesh decals in the game" stays.
