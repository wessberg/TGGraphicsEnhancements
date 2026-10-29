# memory-2: the tracer's video memory, the cuts that keep the picture (PT-87)

Worker `memory2`, copy `<scratch>\wt\memory2` (base `base80`), build folder `%TEMP%\tgb\memory2`
(`-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON`, the main tree's shader headers copied in: C++ only).
Brief: `docs/agents/briefs/brief-memory-2.md`. Started 2026-10-10 01:10.

**The newest state is in the section "memory3" further down (a second worker, from 04:50): start there. What
follows is the first worker's report as it stood when that worker was stopped; its cuts 1 and 2 turned out to be
finished in the copy (M3-0).**

## State (2026-10-10 01:50): the double count settled; cut 1 being built

| Step | State |
|---|---|
| 0. Does Windows count the game's textures twice? | SETTLED, no build: yes (section 1). And a new finding: about 2 GB of Windows' count come with the tracer and are in none of its figures |
| 1. The chunks' free room | being built (section 2) |
| 2. The plants' structures compacted | not started |
| 3. The water's pieces | not started |
| 4. The plants' shared vertex data, written up | not started |

## 1. The double count (settled from logs on the disk; nothing built)

**The answer: yes. Windows counts a texture of the game's a second time when the tracer opens it. The card
itself holds it once.** The memory line's wording was right on that point. What misleads in it is `notTracer`:
it is not the game (below).

### What proves it (two measurements on this card and driver, each with nothing else moving)

1. The tracer's own GPU test measures it in every whole run (`tests/pathtracing_gpu_test.cpp`, "Phase 3, the
   video memory cap"): a 2048 x 2048 BC3 texture that the game's device ALREADY holds (made, filled, flushed)
   is opened on D3D12, and Windows' count for the process is read right before and right after. Its line in 274
   of 274 outputs kept under `%TEMP%\tgb\*.txt` (2026-10-07 to 10-09): "opening the 2048 BC3 (5.38 MB) moved
   Windows' count by +5.38 MB at once". The texture's whole size, every time.
2. The probe of 2026-10-06 (`docs/agents/texture-share.md`, "Real memory against counted memory"): 40 textures,
   213 MB. Both devices hold them: Windows' count +430 MB, the whole card +216 MB. After D3D11 let go: +215.
   After D3D12 let go too: 0.

### What the run's lines prove (`20261009-202514`), and what they do not

They do not overturn it. The brief's reading ("`notTracer` fell by 1,190 MB when the scene loaded, so the
textures are not counted twice") takes the game's own part for steady. It is not:

- The game's part moves by gigabytes within seconds by itself. Between two lines of the town, nothing changed
  in the tracer (4,389 -> 4,349 MB), Windows' count fell by 2,286 MB. At the beach one reading dipped by
  3,656 MB and was back 10 s later. The bridge's own lines (a line each time the count moved 256 MB, read every
  5 s) show the same: 9.04 -> 5.42 -> 8.14 GB in 500 frames.
- The beach's fall of 1,190 came 10 s after a load, while the game was still letting go of the tavern.
- The cleanest moments in the run are the two where the tracer let a whole scene go at once (703 MB and 776 MB
  of the game's textures closed in one step). Windows' count fell by the tracer's WHOLE figure, those textures
  included: -2,414 against -2,439 MB (the tavern), -3,239 against -3,287 MB (the beach). So with the textures
  counted twice the game's own part stood still in those seconds (+26, +48 MB). (The other reading, "counted
  once, and the game freed exactly those textures in the same 4 seconds", fits the lines too: the lines cannot
  tell the two apart. The two measurements above can.)
- Where only the tracer's private memory changed (NRD -> no denoiser, the tavern): the tracer's figure fell by
  534 MB and Windows' count by 532. So the line's own figures are right to the MB for what they count.

### What the lines DO show, and nobody had read: about 2 GB of Windows' count come with the tracer and are in none of its figures

- The game ALONE, in the tavern, before the tracer existed: 4.30 GB (the bridge's line at frame 16884, during
  the 221 s the passes were being made; it writes a line whenever the count has moved 256 MB, and wrote none
  until the tracer was there: so 4.15 to 4.66 GB all that time).
- 2.5 s later, the tracer ready and NOTHING traced: Windows counts 8,007 MB. The tracer's line sums 1,515 MB on
  the card. 8,007 - 4,403 - 1,515 = **2,089 MB (1.8 to 2.3 GB) that came with the tracer's start and are in no
  figure of its line.**
- So `notTracer` (6.0 to 7.2 GB in that run; "the game's own 9 GB" of the owner's session) is NOT the game. In
  the tavern the game is about 4.4 GB; `notTracer` says 6.25 to 6.49.
- The worker before saw the same offline and left it as a note (memory-1, "What it also showed"): 2,181 MB
  counted with nothing else in the process, 933 MB in the line: 1,248 MB not in it.
- What it is: NOT MEASURED YET. The candidates, largest guess first: the driver's own memory for the 44 passes
  and the two devices; the tracer's own shared textures counted a second time (436 MB with nothing traced, 616
  while tracing: they are made on the game's device and opened on D3D12 exactly as the probe's were); the
  D3D12 heaps the SDKs make. The Off and On steps of the run fit either reading of the shared ones (the count
  followed the tracer's figure to 20 MB, which says "once", unless HDRP's own lighting targets came and went
  by the same 190 MB in the same steps, which they may: the plugin stops HDRP's lighting while tracing).
- THE NEXT STEP FOR IT (cheap, in this job's check and in the game's log): Windows' count read at three
  moments of the tracer's start, so the log says in MB what the passes and the D3D12 side take by themselves.

### The line's wording (to change with cut 1's log line)

- `notTracer` -> `notInTheLine`, and the note says what is in it: the game, the other mods, the second count of
  the tracer's own shared textures, and the driver's memory for the tracer's passes.
- `sharedTextures` stays "the game's textures the tracer holds open: their second count in Windows' budget; no
  memory of the card's".

## 2. Cut 1: the chunks' free room (being built)

What the run's own lines say (MB; the water's pages taken out by their count: pages of 16 MB of vertices and of
32 MB of structures, from "The water's surface: ... MB of pages"):

| | tavern | beach | town |
|---|---|---|---|
| `acceleration` in the line | 226 | 485 | 504 |
| of it the water's pages | 96 | 320 | 192 |
| the arena's chunks (the rest, less the top level) | about 129 | about 163 | about 309 |
| what the meshes' structures take of them | 82 to 109 | 115 to 120 | 196 |
| **free room inside chunks held whole** | **20 to 47** | **about 45** | **about 113** |
| `meshGeometry` in the line | 80 | 176 | 208 |
| of it the water's vertex pages | 32 | 48 | 48 |
| the geometry arena's chunks | 48 | 128 | 160 |
| free room there | under 16 | under 40 | under 20 |

So the brief's "the chunks hold far more than the meshes use" is mostly the WATER'S pages, which that sentence
of the log does not take out (cut 3). The arenas' own free room is 20 to 113 MB, nearly all of it in the
acceleration structures' arena. The geometry arena is tight already.

---

# memory3: the 2 GB that come with the tracer (a fresh worker in the same copy, from 2026-10-10 04:50)

Brief: `docs/agents/briefs/brief-memory-3.md`. Scripts: `<scratch>\m3\`. Build folder: `%TEMP%\tgb\memory2` (the
stopped worker's, kept).

## State when stopped

(Rewritten after every step. The newest is the truth.)

- 04:50: the copy checked against the base (section M3-0). Nothing built yet by me.
- 05:17: **HAND-OVER 1 is ready** (section M3-1): step 1's lines are written and built (C++ only, 0 errors, the same
  4 warnings as before), the source checks pass (109 of 109), the changed files are frozen in
  `<scratch>\wt\memory3-handover1\`.
- 05:20: the check ran once offline (`m3b`, the gate was open for a minute): **the 2 GB is measured, section M3-2**.
  Its printed output is lost (I stopped its script to rebuild as it started), its native log is whole.
- 05:31: **HAND-OVER 2 is ready** (section M3-5; it holds hand-over 1 and replaces it): frozen in
  `<scratch>\wt\memory3-handover2\`, built (0 errors, the same 4 warnings), source checks 109 of 109. Steps 2 and 3
  are written (M3-3, M3-4). NOT RUN OFFLINE YET, for what came after hand-over 1: run `m3d` (720p, then 1080p) has
  been waiting since 05:30 behind the coordinator's held run (`artifacts\harness\RUNNING`, process 52900, itself
  waiting for the card: QuickLook holds 6.5 GB of video memory). The script (`<scratch>\m3\run-check.ps1`) waits by
  itself, up to four hours, and writes `%TEMP%\tgb\memory2\memory2-check-m3d.log`, `...-m3d-1080.log` and their
  `.out.txt` and `.native.log`. What that run still has to say: the probe (case 9), the cap's trial (case 10), off
  and on in Windows' count (case 8), the first frames by stage, and whether the driver's part grows with the size.
- 05:43, **STOPPED HERE, the job's steps 1 to 3 handed over or written.** Run `m3d` is still waiting: the held run
  has kept the marker since 05:25 without a break (I waited nine more minutes). The script is PowerShell process
  54808 and ends by itself, at 09:30 at the latest; to end it sooner: `Stop-Process -Id 54808` (only while no
  `tgpt_gpu_test.exe` is running, or that run's last lines are lost).

- 05:48, AFTER the hand-back: the gate opened at 05:45 and **run `m3d` ran, at both sizes: every check passed**
  ("memory-2 checks passed", exit 0, twice; the build is hand-over 2's, tracer 871E2A6F9A78). Its results are in
  **M3-6** at the end, and they settle three open points: the atlases' second count CAN be given back (the probe
  said yes three times); the driver's part is a fixed 662 MB that comes when the first list is handed to the GPU,
  the same at 1280 x 720 and 1920 x 1080; the cap's trial does what it says. No source was touched after the
  hand-back: the copy still equals `memory3-handover2`. Only this report changed. No script of mine is left running.

### What is left, in the order I would take it

(Point 2 below is done: M3-6 has its answers. The atlases' cut it clears is now the first thing to BUILD.)

1. **The game run of hand-over 2** (M3-5's recipes): it says what the 1.5 GB beyond the rings and the second count
   are in the game, and whether the cap's trial ends the pauses.
2. **Read run `m3d` when it has run** (`Get-Content $env:TEMP\tgb\memory2\memory2-check-m3d.out.txt | Select-String
   'A, the|B, the|B0,|C, the|D, one|the cap|off for five|Windows counts|traced frame'`):
   - case 9, way A: does the count fall by 64 when "the game's device lets go (the tracer's still holds it)", does
     it open again, is the texel still there? Three times yes: the atlases' second count (261 to 347 MB) can go,
     by the change M3-3 describes.
   - case 9, way B: what `Evict` does to the count (for M3-4's way 3; it stays "not a way" whatever it says).
   - case 9, way D: the upload buffer's 64 MB in the card's count once more (M3-2 found the rings there).
   - `m3d-1080` against `m3d`: "the rest of recording and running the passes" at 10 frames. About 2.25 times
     larger at 1920 x 1080: the driver's part grows with the pixels (memory for every thread of a pass that runs),
     and only smaller passes or fewer pixels shrink it. The same: it is a fixed cost of the passes.
   - If case 10 or the "expect" of the start's lines fails there, that is my code's first run: read it as such.
3. **The upload rings by need**, once the game's `ringPeak` is known (M3-3).
4. **Who reads the textures held open** (M3-4 way 4): a count by reader in `GpuScene::distance_bands` (still
   instances' four maps are counted today; add moving things, characters, material programs' rows, decals).
5. Cut 2 (the plants' structures compacted, 84 % offline) needs one game run for its frame time; cut 3 (the water's
   blocks and pages) needs its check run once (`run-check.ps1 -Water`). Both are off (`scene.hpp kMemory2Waiting`).

## M3-0. What the stopped worker left in the copy (checked before any build)

The report above stops at "cut 1 being built". The copy holds far more. File by file, against `base80`:

| Change | Files | Whole or half-made | Proof |
|---|---|---|---|
| **Cut 1**, the full-size builds in chunks of their own (`buildArena`) | `scene.cpp`, `scene.hpp` | WHOLE | built 01:38, its check passed 01:47 (`memory2-check-c1a`): 33.0 MB against 49.0 MB with one arena, every pixel the same |
| **Cut 2**, the plants' structures compacted (`compact_plants`) | `scene.cpp`, `scene.hpp` | WHOLE as code, NOT seen in the game | built 01:48; its check ran by itself at 04:43 after the worker was gone (`memory2-check-c12a`) and passed: 27.9 MB of structures became 23.5 MB (84 %), every pixel the same over 60 frames of wind. What a refit of a compacted structure costs each frame is not measured |
| **Cut 3**, the water's blocks that fit and its pages given back | `scene_water.cpp`, `scene.hpp` | HALF-MADE | the native side compiles (object file 01:48:29, after the last edit 01:47:50) and reads complete, but NO check ever ran it with water. The script that adds its check (`<scratch>\m2\edit_water2.py`, written 01:52) was never run: this is where the worker was cut off |
| The memory line's wording (`notTracer` -> `notInThisLine`), the arenas' own figures | `tracer.cpp` | WHOLE | in the build of 01:48 |
| First lines of Windows' count at the start (before and after the passes, the five atlases, the render size's textures) | `tracer.cpp`, `texture_share.cpp/.hpp` | WHOLE, and the start of this job's step 1 | in the build of 01:48; what they said offline is in M3-1 |
| The check: cases 5 (sheets) and 6 (grass) | `tests/pathtracing_memory2_checks.inl` (new), `tests/pathtracing_memory1_checks.inl` | WHOLE | both ran and passed |

No file was left broken in the middle of a text edit: every changed file compiled at 01:48 and none was written after.

What I do with it:

- Cut 1 stays, on. It cannot change the picture: the same structures are built and compacted as before, only the
  address they lie at differs, and the check compares every pixel.
- Cuts 2 and 3 wait (the brief). Their code stays in the copy but is OFF in a build without the test's switch
  (`scene.hpp`, `kMemory2Waiting`): the game then runs them exactly as the base does. One constant turns each on.
  Cut 2 needs one game run for its frame time before it is turned on. Cut 3 needs its check first.
- The native sources in this copy and in the main tree end their lines with LF, the tests' `.inl` files and the
  docs with CRLF. I keep each file as it is.
- One thing of cut 2 had to change for the source checks: `compact_plants` called `mega_fresh` (the parked Mega
  Geometry path's), a fourth call where `test_path_tracing_mega_geometry_on_trial_beside_the_plain_path` counts
  three. It is not needed: a plant group's instance is written again every frame by that path (it comes after
  `megaStill`). Taken out. The checks pass again (109 of 109, as the base).

## M3-1. HAND-OVER 1 (05:17): the tracer's start, step by step, in what Windows counts

Frozen copy of the eight changed files: `<scratch>\wt\memory3-handover1\` (same relative paths; `tracer.cpp`
E7175F7E6A37, `texture_share.cpp` 5EF553D5C90E, `texture_share.hpp` 8DE7B5E352CF, `scene.cpp` 4CF4F8230FC7,
`scene.hpp` F792BD688162, `scene_water.cpp` F47B9D1BB3F0, `tests/pathtracing_memory1_checks.inl` AE4C500FFBD2,
`tests/pathtracing_memory2_checks.inl` 304CE30E232B; SHA-256, first 12). C++ only: no shader, no plugin, no
export and no struct changed. Merge: `python tools/agents/merge_copy.py memory2 --base=base80 --root=<scratch>`.

**What it is for.** Windows' count for the process is one number. The memory line can only subtract its own sum
from it. These lines read the count before and after every step of the tracer's start and write the difference:
then the log itself says which step brought what, and how much came while no code of the tracer's was running.

### The lines (all in `TGPathTracing.Native.log`; grep `PT memory at start` and `PT memory since`)

| When | Line | Thread |
|---|---|---|
| The shared device arrives | `PT memory at start: the shared device is here, nothing of the tracer's made on it yet (Windows counts M MB; ...)` | the game's |
| After each probe | `... Ray Regeneration's probe ... +N MB`, `... NRD's probe ... +N MB` | the game's |
| The passes are made | `... the K compute pipelines, made in S s ... +N MB` and, where the card reorders, `... the two reordering passes ... +N MB` | the passes' own |
| The first traced frame begins (`ensure_d3d12`) | one line each: before the first object; the queue and the two fences; the three allocators and lists; the three upload rings; the descriptor heap and the samplers; the passes taken over; timestamps, probe, meter, yardstick; the radiance cache; the caustics' counters; the colour atlas; the alpha atlas; cookie atlas, sky and fog's sky; then `the tracer's own start, all the steps above together, +N MB` | render |
| The render size's textures (`ensure_frame_textures`; again after Off, and at another size) | the five outputs; the 14 copies of HDRP's frame; surfaces, normals, materials; RTXDI's reservoirs; the raw signals and the camera rays' own | render |
| A filter or a denoiser is first made | the built-in denoiser's textures for the bounced light; the direct light's and the reflections' filters; NRD's own pools; NRD's inputs; Ray Regeneration's context; its guides | render |
| After the first traced frame | `PT memory at start: the first traced frame, apart from the steps above, +N MB (Windows counts M MB): its D3D11 side +a, the flush and the wait for it +b, recording and running the passes +c` | render |
| After frames 1, 2 and 10, then with every memory line (10 s) for ten minutes | `PT memory since the tracer's start: ...` (below) | render |

Every step line has the same shape: `PT memory at start: <step> +N MB (Windows counts M MB; the PC's own memory
beside it +n MB, now m MB[; the tracer's own figure for it F MB])`. N is the move of the card's count (the one the
budget is for). "The PC's own memory beside it" is Windows' second count (the non-local group): where upload
rings should show. F is what the memory line will hold for that step. N near F: counted once. N near 2 F: twice.
N far above F: memory the line does not see.

`PT memory since the tracer's start` splits everything that came after the start began:

- while a call of the tracer's ran on the render thread, by the call's part (the start's steps; the rest of the
  frames' D3D11 side; the flush and the wait; the rest of recording and running the passes; the end of the call;
  maintenance, composite and fog);
- **opening the game's textures**: how many were opened, their MB as the tracer counts them, and what Windows'
  count did across those very calls (read right before and after each `OpenSharedHandle`). This is the double
  count measured in the game itself, at full scale: "moved by" near their MB means counted twice;
- **with no call of the tracer's running**: what is left. That part is the game's own (loading, streaming), other
  mods', or what Windows counts late.

So the 2 GB of the run of 2026-10-09 fall into: the steps (each named), the first frames' own part, the opens,
and "from elsewhere". Whichever is large is the next thing to open.

### The recipe (yours: the game)

Any scene's first start after a fresh launch of the game, path tracing on from the first frame:
`.\tools\harness\Run-PtHarness.ps1 -Scenario <one scene> -Recipe memory` (NRD, none, off, NRD again: it also shows
NRD's own step, what Off gives back in Windows' count, and the render size's textures made again). One scene is
enough; a town (`spec-terrain`) has the most textures to open. Then:
`Select-String -Path <run>\TGPathTracing.Native.log -Pattern 'PT memory at start|PT memory since'`.

What I need back, if it is to be read by me: nothing. The lines are the result. Read them like this:

1. Sum the step lines' N. Compare each with its F.
2. The line after the first frame: is "recording and running the passes" far above the filters' own figures? Then
   the driver takes memory when a pass first runs.
3. `PT memory since ...` at 10 frames and at 10 s: "Opening K of the game's textures (X MB) moved the count by
   +Y MB": Y against X is the double count. "came from elsewhere": the game's own part.
4. Read the lines of frames 1, 2 and 10 first. Later ones carry churn: offline, "their flush and the wait for it"
   rose by about 2 MB every frame and "came from elsewhere" fell by as much (after 1,070 frames: +2,431 and
   -2,121). That is memory the game's device takes for a moment at its flush and gives back between the tracer's
   calls. Add the two: the sum (+310) is what stayed.

### What my offline check leaves out (it runs under `TGPT_TEST_NO_AGILITY=1`, 10 GB ceiling, 1280 x 720)

- The Shader Model 6.9 passes and the two reordering passes: Windows' own runtime gives Shader Model 6.6 and no
  reordering. So "the two reordering passes" line can only come from the game.
- Opacity micromaps (none on that runtime).
- NRD (this build is `TGPT_WITH_NRD=OFF`), Ray Regeneration (not this card's) and Ray Reconstruction (TG Better
  Upscaling's, another mod).
- The game: its textures by the thousand, its meshes, mip streaming, the other mods, the game's own loading.
- The game's render size (the render size's steps scale with it).

### What could go wrong

- The lines only read. They make nothing and change no order. Cost: one or two `QueryVideoMemoryInfo` calls a
  step; five a frame for ten minutes from a start, then none. An open of a texture reads the count twice.
- The passes' two lines split one thread's move at the moment the first reordering pass is begun; a few compute
  pipelines are still being made then (the line says how many). And the game loads beside them.
- If the game's textures are opened by the plugin's thread while a frame is traced, the summary takes that part
  out of the frame's ("of it +N MB while the game's textures were being opened on another thread").

## M3-2. What the 2 GB is made of: measured offline (run `m3b`, 05:19, 1280 x 720, nothing else in the process)

Source: `%TEMP%\tgb\memory2\memory2-check-m3b.native.log`, the lines `PT memory at start` and `PT memory since`.
(That run's own printed output is lost: I stopped its script to rebuild just as the gate opened. The native log
is whole. Run `m3d` repeats it, with a second run at 1920 x 1080.)

**Ten frames after the start began, Windows counted 2,020 MB more than before it. The memory line summed 969 MB.**
The 1,051 MB between them, and the 144 MB the line wrongly took out, are these four things:

| Part | MB at 1280 x 720 | How it was measured | In the memory line? |
|---|---|---|---|
| **The three upload rings** | 144 | their own step: the card's count +144, the PC's memory +0 | yes, as `transfer`, but `notInThisLine` took them for the PC's memory. On this card and driver Windows counts a D3D12 upload heap against the card's budget |
| **The tracer's own shared textures, a second time** | 338 | the first frame's "flush and the wait for it" +312 = the atlases (261) and the copies of HDRP's frame (53), the moment the game's device first writes them; the outputs (24) follow when the game's device first reads them | no (it is `ownShared`'s second count) |
| **Memory the driver takes when the passes first run** | about 700 in the first frames, about 330 once it has settled (300 to 400 over a minute) | the first frame's "recording and running the passes" +705 beyond its own steps, of which the line grew by 37 | no |
| The descriptor heap, the queue, the pipelines themselves | 8 + 3 + 1 | their own steps | no |

Every step, as the log has it (N = Windows' count, F = the tracer's own figure):

| Step | N | F |
|---|---|---|
| The 24 compute pipelines made (this runtime's set, no reordering passes) | +1 | - |
| The compute queue and the two shared fences | +3 | - |
| Three command allocators and lists | 0 | - |
| **The three upload rings** | **+144** | 144 |
| The descriptor heap (262,144 views) and the samplers | +8 | - |
| The passes taken over; timestamps, probe, meter; the caustics' counters | 0 | - |
| The radiance cache | +36 | 36 |
| The colour atlas / the alpha atlas / cookie, sky, fog's sky | +192 / +64 / +5 | 192 / 64 / 5 |
| *The tracer's own start as a whole* | *+453* | *441* |
| The five outputs / the 14 copies of HDRP's frame | +24 / +53 | 24 / 53 |
| Surfaces, normals, materials / RTXDI's reservoirs / signals and the camera rays' own | +60 / +98 / +60 | 60 / 98 / 60 |
| The built-in denoiser's textures / the direct light's and the reflections' filters | +60 / +120 | 60 / 120 |
| **The first frame's flush and the wait for it** | **+312** | 0 (the second count of 314 MB of shared textures) |
| **The first frame recorded and run, apart from the filters** | **+705** | 37 |

So:

1. **Making a thing on the tracer's device is counted once, at once, at its own size.** No surprise there.
2. **Making the pipelines costs nothing in Windows' count** (+1 MB for 24). Their cost comes when they first RUN.
3. **A texture both devices hold is counted a second time, but only when the SECOND device first uses it.** That is
   why the stopped worker's line at the atlases said "once" (+261 for 261): the game's device had not written
   them yet. The second 312 MB came 20 ms later, when it flushed.
4. **The upload rings are in the card's count.** The memory line's `notInThisLine` (and the old `notTracer`)
   subtracted `transfer` as if it were the PC's memory, so they showed up as unexplained.
5. **About 330 MB (700 at first) arrive when the first frame is recorded and run and are no object of the
   tracer's.** This is the driver's own memory for running the passes. **Answered since by run `m3d` (M3-6): it is
   662 MB, all of it at the moment the first list is handed to the GPU, none while the passes are recorded, and the
   same at 1280 x 720 and 1920 x 1080.** In that run it stayed at 690 to 770 MB; here it fell to about 350.

The worker before saw 1,248 MB not in the line offline (memory-1). That number is now whole: 58 MB before the
tracer started (the test and the device), 144 the rings, 338 the second count, 12 heap and queue, about 700 the
driver's at the first frames.

**For the game** (the 2,089 MB of 2026-10-09, at 1924 x 1084, with Shader Model 6.9 and the reordering passes):
the rings are 144 MB there too; `ownShared` was 436 MB with nothing traced, so its second count is about 436; that
leaves about 1.5 GB for the driver's memory when the passes first run, and for the game's own moves in those
2.5 seconds. Hand-over 1's lines split those two in the game's own log.

## M3-3. Step 2: what can be given back with the same picture

| What | MB of Windows' count | State |
|---|---|---|
| **The cap weighs what the card really holds** (way 5 of M3-4) | the pause comes 1.1 to 1.9 GB later | BUILT as a trial, off unless `TGPT_TEST_SHARE_COUNT_ONCE` is set when the game starts. It is the one thing here that goes at the owner's own symptom ("Path tracing paused: N textures unavailable"). Not a cut of memory: a change of when the tracer pauses itself. Yours and the owner's to decide after one game run |
| The upload rings, 3 x 48 MB | up to about 130 if a frame's need is small | MEASURED, NOT CUT. The memory line now counts them as the card's (`notInThisLine` no longer takes `transfer` out) and says `ringPeak=<most a frame took>/<ring>` and `ringFull=<times a frame found one full>`. Why not cut: most users of a ring fall back to a buffer of their own when it is full, but two do not (the caustics' instance throws, `tracer.cpp` "found no room in the frame's ring"; NRD's constants fail its dispatch), and a frame that fails while it is recorded turns path tracing off for the session. Size them by need once the game's `ringPeak` is known, with a ring that is made larger before its frame is recorded |
| The atlases' second count: the game's device lets go of its side between bakes and takes it up again for the next | 261 offline, 347 in the game with hair | NOT BUILT, and now CLEARED TO BUILD: the check's probe (case 9, way A) ran after the hand-back and said yes to all three (M3-6): the count falls when the game's device lets go, it opens the texture again by a kept handle, the texels are still there. A contained C++ change (the places that bake into an atlas ask for the game's side first; 120 frames without a bake let it go): M3-6 says exactly what |
| The copies of HDRP's frame and the outputs, a second time | 77 offline; about 170 at 1924 x 1084 | cannot be given back: both devices use them every frame. The copies go for good when the yardstick against HDRP's G-buffer goes (the migration) |
| Pipelines made only when their pass is used | 0 | does not pay in Windows' count: making 24 of them moved it by 1 MB. (It pays in the PC's memory and in start-up time: not this job) |
| The driver's memory when the passes first run | 350 to 770; 662 at the first list (M3-6) | not an object of the tracer's, so nothing in C++ gives it back. It does NOT grow with the render size, and recording the passes takes none of it: it all comes when the first list is handed to the GPU. To know which pass brings it, the first frame's list would have to be handed over stage by stage (M3-6, not built) |
| The descriptor heap of 262,144 views | 8 | leave |

## M3-4. Step 3: the game's textures counted twice (0.7 to 1.3 GB). The ways, with their cost

**How it is held today** (`texture_share.cpp`): a hook adds the legacy sharing flag when the game makes a texture
for shaders to read; the tracer's device opens it by its legacy handle. One texture on the card, two devices of
one process holding it.

**Why Windows counts it twice** (Microsoft's own documents, read 2026-10-10):

- Residency is per DEVICE: "residency is moved to an explicit list on the device ... The video memory manager
  ensures that all allocations on a particular device residency requirement list are resident before any contexts
  belonging to that device are scheduled" (learn.microsoft.com, windows-hardware/drivers/display,
  "Driver Residency in WDDM 2.0"). Two devices, two lists, the same texture on both. Nowhere do the documents say
  how a texture on two lists of one process is counted: that it is counted once for each is measured here, not
  documented (5.38 MB for a 5.38 MB texture, 274 of 274; and M3-2 for the tracer's own).
- What the count is weighed against: "processes are assigned budgets for how much memory they can keep resident.
  This budget can change over time, but generally will only be imposed when the system is under memory pressure.
  Prior to Direct3D 12, the budget is handled by the user mode driver in the form of Trim notifications ... For
  Direct3D 12 applications, the budget is handled completely by the application" ("Process Residency Budgets").
- What over the budget costs: "If CurrentUsage is greater than Budget, the application may incur stuttering or
  performance penalties due to background activity by the OS" (`DXGI_QUERY_VIDEO_MEMORY_INFO`); "the process will
  be intermittently frozen to allow other applications to run and/or the creation APIs will return failure"
  (Direct3D 12 "Residency"). That is the frame of 1.5 seconds.
- "Setting a reservation won't ever give an application a higher budget than it would normally receive" (the
  same page): nothing an application asks for makes the budget larger.

**The ways:**

| # | Way | Counted | Saves | Cost, and what speaks against it |
|---|---|---|---|---|
| 1 | **One device for both: the game itself on Direct3D 12**, the tracer on Unity's own device | once | all of it: 0.7 to 1.3 GB, and the tracer's own second count (0.44 to 0.62 GB), the hook, the 64 KB pages of 629 small textures | The game's renderer changes for every player (Unity's HDRP has a Direct3D 12 path; nobody has run this game on it). Every mod here is built on "the game on D3D11, the mods on one shared D3D12 device": the bridge, frame generation's presentation, the upscalers. Weeks, for all four mods. **This is the only supported way I found of holding the game's textures that Windows counts once, with no copy and at the game's own size** |
| 2 | The game's D3D11 calls run on Microsoft's D3D11On12 layer over the shared device | once | the same | The whole game then draws through a translation layer: slower, another driver path, not what that layer is for. Not a way I would take |
| 3 | The tracer evicts its side of each texture (`ID3D12Device::Evict`) and reads what the game's device keeps resident | once (measured: the count fell by the texture's 64 MB, M3-6 way B) | the same 0.7 to 1.3 GB | Microsoft: "GPU access to allocations that aren't resident is illegal and will result in the device removed". It would work only while the game's device happens to keep every texture resident, and that device lets textures go exactly when memory is short. **Not a way** |
| 4 | Hold fewer textures open: let go of those no traced thing reads | twice, of less | at most 245 MB (tavern), 257 (beach), 417 (town): what the log calls "named by no instance" | That figure counts only the four main maps of still instances. Characters', moving things', material programs' and decals' maps are in it too. So first a count by who reads each texture (C++ only, a log line), then the cut. The plugin registers a texture once and never again: it would have to ask again when a material returns |
| 5 | **Leave the holding as it is; stop OUR OWN cap from counting what is not there** | twice, but the pause no longer acts on it | the pause comes 1.1 to 1.9 GB later (the game's textures' second count 0.7 to 1.3 GB, the tracer's own 0.44 to 0.62 GB) | BUILT as a trial (`TGPT_TEST_SHARE_COUNT_ONCE`, M3-3). The owner's "Path tracing paused: N textures unavailable" is not Windows: it is `within_budget`, which stops opening textures at 97 % of the budget BY WINDOWS' COUNT. In the owner's session of 2026-10-09 the count stood at 12.2 of 13.2 GB with path tracing paused for good: about 1.2 GB of those 12.2 were second counts, so the card held about 11.0. What it does not cure: when the card is really full, Windows' own trimming still reckons with the inflated count |
| 6 | A copy on the tracer's device instead of sharing | once each, but the copy is real memory | nothing: it costs the card what sharing only costs the count | The owner: "Don't do copies". No |

Beside the ways: **other programs' video memory is taken out of the game's budget before anything of ours.** At
05:12 tonight the harness's own check listed QuickLook with 6,561 MB, Sunshine 1,243, Code 508, NVIDIA Overlay 307,
Brave 286, Steam 258: 9.2 GB of a 16 GB card held by other programs. QuickLook's 6.5 GB alone is more than
everything in this report. It is also what holds your game run back right now.

## M3-5. HAND-OVER 2 (05:31): hand-over 1, and what the first offline run asked for

Frozen copy of the ten changed files: `<scratch>\wt\memory3-handover2\` (`tracer.cpp` A3E48FC2C2BC,
`texture_share.cpp` F76C4D70B1C9, `texture_share.hpp` 77CE70FBB2D3, `gpu.hpp` 861709CAE9DC, `scene.cpp`
4CF4F8230FC7, `scene.hpp` F792BD688162, `scene_water.cpp` F47B9D1BB3F0, `tests/pathtracing_gpu_test.cpp`
223586448775, `tests/pathtracing_memory1_checks.inl` AE4C500FFBD2, `tests/pathtracing_memory2_checks.inl`
98691BB2505D). The copy itself is the same. C++ only; no shader, no plugin, no export, no struct. Take this one, not
hand-over 1.

Added to hand-over 1:

1. **The first two traced frames of a start, stage by stage**: `PT memory at start: traced frame 1's list in
   Windows' count, as each stage's commands were recorded (MB): scene +a, prepare +b, bounced light +c, ...;
   closing it +k; handing it to the GPU +x`. If the driver takes its memory as a pass is recorded, this names the
   pass. If it all comes at "handing it to the GPU", it does not, and the next step is another one.
2. **The memory line is right about the rings**: `notInThisLine` is Windows' count less the line's WHOLE sum
   (`transfer` was taken out as the PC's memory; it is in the card's count). New in it: `ringPeak=<bytes>/<ring>`
   and `ringFull=<times>`.
3. **The cap's trial** (M3-4 way 5): with `TGPT_TEST_SHARE_COUNT_ONCE` set in the game's environment when it
   starts, `within_budget` weighs Windows' count less the second counts (the opened textures' and `ownShared`'s).
   Its log line then says "on trial: the second counts are taken out of the count before it is weighed". Without
   the variable nothing changes.
4. The check: case 8 (off and on in Windows' count), case 9 (the count's probe: four ways of making and holding
   a 64 MB texture, each step's move), case 10 (the cap's trial), and `TGPT_TEST_MEMORY2_1080=1` for the same
   cases at 1920 x 1080.

### The recipes (yours: the game)

- **The 2 GB, in the game's own log**: as in M3-1. Now also look for `traced frame 1's list`.
- **The pause** (the cap's trial): the scene and the walk that give "Path tracing paused: N textures unavailable"
  today, once as it is and once with `TGPT_TEST_SHARE_COUNT_ONCE=1` in the environment the game starts in. What
  to look at: does it still pause; `Texture sharing: video memory near this card's budget` in the native log (its
  two numbers and "on trial"); and the frame times, for frames of a second or more that were not there before.
  If the pauses go and no long frames come, it is a rule to put to the owner. If long frames come, it is not.

## M3-6. Run `m3d` (05:45 to 05:47, after the hand-back): hand-over 2's build, at 1280 x 720 and 1920 x 1080

Files: `%TEMP%\tgb\memory2\memory2-check-m3d.out.txt`, `...-m3d-1080.out.txt`, their `.native.log`. Both runs:
"memory-2 checks passed", exit 0. So hand-over 2 has now run offline, all of it but the reordering passes, NRD
and the game (M3-1 says what this mode leaves out).

### 1. The driver's part: 662 MB, all at once, when the first list is handed to the GPU. Not by the pixels.

`PT memory at start: traced frame 1's list in Windows' count, as each stage's commands were recorded (MB): scene
+43, prepare +0, bounced light +0, bounced light spatial reuse +0, direct light +0, reflections +0, radiance cache
+0, denoising +0, caustics +0, composite +0; closing it +0; handing it to the GPU +662`. Frame 2: `handing it to
the GPU +0`.

| | 1280 x 720 | 1920 x 1080 |
|---|---|---|
| Recording the passes' commands | 0 | 0 |
| Recording the scene (the arenas' first chunks: in the memory line) | +43 | +43 |
| **Handing the first list to the GPU** | **+662** | **+662** |
| The first frame's flush (the second count: atlases 261, and HDRP's copies 53 or 118) | +312 | +385 |
| Not in the line after ten frames, less what was there before the start, the second counts and the heap | 692 | 731 |
| The same after the next case | 733 | 772 |

So it is NOT memory for every pixel a pass runs on (it would have been 2.25 times larger at 1920 x 1080). It is a
fixed cost of running these passes for the first time. Recording them takes nothing, so the line by stage cannot
name the pass: the driver takes it for the whole list at once. In this run it stayed (690 to 770 MB). In the two
runs before, it fell to about 350 MB some seconds later (M3-2). So: **350 to 770 MB on Windows' own runtime with
24 pipelines.** The game has more and larger passes (Shader Model 6.9, the two reordering passes): its own line
`handing it to the GPU +N` is the number to read.

To name the pass (not built): for the first frame after a start only, hand the list to the GPU after each stage
instead of once at the end, and read the count after each. The picture is the same; a frame's order is not, so
it belongs behind a test's switch.

### 2. The count's probe (case 9): 64 MB textures, each step's move in Windows' count, the same at both sizes

| Way | Steps |
|---|---|
| **A, as the tracer makes its own** (the game's device makes it empty, with an NT handle) | made +0; the tracer's device opens it +64; the game's device writes it **+64**; the tracer evicts its side -64, resident again +64; **the game's device lets go while the tracer's holds it -64**; the game's device opens it again by a kept handle +0; **reads back the texel it wrote before: still there**, +64; writes again +0; lets go again -64; the tracer lets go -64 |
| **B, as the game's textures are held** (made full, the legacy flag) | made +64; the tracer's device opens it **+64**; the tracer evicts its side -64, resident again +64; the tracer lets go -64; the game lets go -64 |
| B0, the same texture with no flag | made +64; let go -64 (the flag costs nothing at this size) |
| C, the other way round (the tracer's device makes it in a heap that may be shared) | made +64; **the game's device could not open it** |
| D, one of the tracer's own alone; then an upload buffer | made +64; evicted -64; resident +64; let go -64. **The 64 MB upload buffer: +64 in the card's count, +0 in the PC's own** |

What it settles:

- **The atlases' second count can be given back** (M3-3's third row; 261 MB, 347 with hair's strands). All three
  things it needs hold: the count falls by the texture's size when the game's device lets go; the game's device
  opens it again by a handle kept meanwhile; what it wrote before is still there. And the second count is back only
  when the game's device USES the texture again (+64 at the read, +0 at the open). So an atlas costs its second
  count only around a bake. This is now a contained C++ change with nothing unknown in it; it is NOT built (the
  hand-back was out, and the copy must not change under your merge). What to build: `Shared` keeps a handle
  (`IDXGIResource1::CreateSharedHandle`) for the colour, alpha, cookie and strand atlases; the eight places that
  bake into one (`tracer.cpp`: `bake_textures`, `bake_strands`, `bake_hair`, `bake_hair_lobes`, `bake_eyes`,
  `bake_cookies`, `bake_alpha_textures`, and the two clears of a cleared scene) first open the game's side again
  (`ID3D11Device1::OpenSharedResource1`, then the views); after 120 frames without a bake the game's side and its
  views are let go; `release_all` closes the handles. Its check: the same frames with and without, a texture
  registered in between so that a bake comes after the side was let go; every pixel the same, and the count 256 MB
  lower between bakes.
- **A texture the game's device made empty is not counted until that device uses it.** That is why a start's
  steps say "once" and the first flush brings the second count.
- **Evicting the tracer's side does take the second count away** (way B: -64). It stays "not a way" (M3-4 way 3):
  Microsoft calls GPU access to it illegal, with the device removed.
- **The other way round is closed** (way C): the game's device did not open a texture the tracer's device made.
  So the tracer's own shared textures stay made on the game's device, as they are.
- **Upload buffers are in the card's count on this card** (way D), as the rings' step said.

### 3. The cap's trial (case 10) does what it says

One 2048 BC3 open; at a limit 18 MB above Windows' count a second one: kept back (-28) as the count stands; a
third: opened with `TGPT_TEST_SHARE_COUNT_ONCE` set; the second again without it: kept back; with no limit:
opened. The rule is unchanged without the variable.

### 4. Off and on in Windows' count (case 8)

1280 x 720: the line fell by 295 MB and the count by 295; both came back by 295. 1920 x 1080: 666 and 666; back by
666 and 596 (the rest came a moment later: the next lines show the count at its old 2,449). The count did not fall
by the shared textures' second count as well (77 or 173 MB), because the game's device frees its side only at
its next flush, which is the first frame after. So Off gives back what the line says, and the second count of
what it let go a frame later.

### 5. Cuts 1 and 2 again, at both sizes

Cut 1: 33.0 MB against 49.0 MB with one arena, every pixel the same. Cut 2: 27.9 MB of structures became 23.5 MB
(84 %), every pixel the same over 60 frames of wind. As in the stopped worker's own runs.
