# memory-1: path tracing fits the card's video memory (PT-87)

Worker report. Copy `memory1`, base `base73`. Brief: `docs/agents/briefs/brief-memory-1.md`.
Kept current while the job runs. "MB" here is the log's own unit: bytes / 1,048,576 (the brief's table is in it too).
`<scratch>` is the session's scratchpad folder (the one that holds `wt\memory1`).

## State (2026-10-09 20:16): done; the worker has stopped

- HAND-OVER 1 (19:11, section 1) and HAND-OVER 2 (19:33, section 2; the copy as it stands, frozen in
  `<scratch>\wt\memory1-handover2`): built (native and plugin: 0 errors, 0 warnings of its own), source checks
  pass (107).
- **Its own GPU check passes** (20:14: 38 lines ok, 0 FAIL, 90 s; section 2, "The check"): the memory goes
  and comes back as said, and the picture is the same PIXEL FOR PIXEL in every case. Run on Windows' own
  Direct3D 12 runtime (the test's `TGPT_TEST_NO_AGILITY` mode: no micromaps, no reordering passes), at 1280 x 720,
  in a build without NRD. So NOT run: NRD's part (its lines are in the check and run where the build has NRD),
  the DirectX Raytracing 1.2 route, the game.
- Not seen in the game. The four rows' numbers are expected, not measured there.
- The coordinator's question "Does the unlimited world cost the memory?": section 5.
- Not built: the plants' acceleration structures compacted (written up), the one rule (designed in section 7),
  atlases grown on demand (written up).
- No process of this worker's is left running.

## 1. HAND-OVER 1 (19:11): what a setting or a scene does not need is no longer held

Frozen copy of the five changed files, as they were when it built (the copy itself moved on):
`<scratch>\wt\memory1-handover1\native\pathtracing\src\` (`tracer.cpp` 617DA1A4D22F, `texture_share.cpp`
176B0AD81410, `texture_share.hpp` E9755869414D, `nrd_denoiser.hpp` 8B78FE11F36D, `ray_regeneration.hpp`
0B2617E218F1: SHA-256, first 12). C++ only: no shader, no ABI, no option bit, no plugin change.
Built: `%TEMP%\tgb\memory1` (NMake, Release, no NRD, the main tree's shader headers of 18:47 to 19:00 copied in,
"0 to compile"), `TGPathTracing.Native.dll` links, 0 errors, 0 warnings. `python tests/source_checks.py`: 107 pass.
NRD's path is in the code but not compiled here (`-DTGPT_WITH_NRD=OFF`): the coordinator's build compiles it.

What it changes:

1. **The built-in filters' textures are made when a frame runs them, and let go 120 traced frames after the last
   one that did** (`ensure_filter_textures`, `release_idle_filters`; before: all 18 made with the render size,
   whatever the Denoising row said).
   - The bounced light's (history x2, moments x2, ping, pong): 135.75 MB. Used by the built-in denoiser alone
     (it serves in a debug view and when NRD or Ray Regeneration fail). Never with None, NRD, Ray Reconstruction,
     Ray Regeneration.
   - The direct light's and the reflections' (history x2 and moments x2 each, four scratch): 271.5 MB. Used by
     the built-in denoiser and with NRD. Never with None, Ray Reconstruction, Ray Regeneration.
   - Why the picture is the same: no shader reads them through the frame constants (`g.denoise0`,
     `g.denoise1.w`, `g.denoise2.xy` are read by no pass: grep); they are named only in `pt_temporal`'s and
     `pt_atrous`'s dispatch constants, in frames that run those passes. A group is only ever made in a frame
     that already tells `pt_temporal` to read no history (`FLAG_DENOISER_RESET`, the existing "the filters
     resume" rule); the frame sets that flag for a new group too, so it does not rest on that.
   - They are made BEFORE the frame's command list is opened (by the same rule the frame applies to choose its
     denoiser), so a card with no room fails that frame cleanly and the next one asks again; a failure while a
     list is being recorded turns path tracing off for the session.
2. **A denoiser's own memory is let go 120 traced frames after another one took over** (`release_idle_denoisers`):
   NRD's seven textures and its 14 pool textures (261.9 MB), Ray Regeneration's five guides (51.25 MB) and its
   context (AMD's own, not counted). Before: kept until the render size changed. Made again when it next runs;
   its history starts over then, as it already did whenever it resumed.
3. **The wet ground's store (32 MB) is made by the first frame that has water** and goes with the scene
   (`release_scene_extras`), as the caustics' room does. Before: made at start-up. It is emptied by its first
   pass, as before (`wetRan`).
4. **The memory line names what Windows gives and counts** (job item 5), at its end before the bracket:
   `windowsBudget=<bytes> windowsCounts=<bytes> notTracer=<bytes> ownShared=<bytes>`.
   `notTracer` = what Windows counts less the tracer's own on the card (its total without `transfer`, which is
   the PC's memory): the game itself and the other mods. `ownShared` = the tracer's own textures that both
   devices hold open (atlases, HDRP's frame copies, the outputs, characters' and waves' copies): if Windows
   counts those twice as it does the game's textures, that much of `notTracer` is the tracer's too. Not measured:
   the first log line with these fields shows it (with nothing traced `ownShared` is about 436 MB: if `notTracer`
   then drops by that much when path tracing's textures are let go, they are counted twice).
   Added at 19:44 (after hand-over 2's frozen copy; in the copy itself): `colorTiles=<used>/4096
   detailTiles=<held>/128 alphaTiles=<used>/1024`: how much of the atlases that are made whole is in use (the
   town run had 918 colour tiles registered: 22 % of that part's 64 MB), for choice 5 in section 8.
5. The "Render size" log line now says what was really made (the device's own count), and each let-go has a line
   ("... are let go: no frame has run ... for 120 frames").
6. `TGPT_TEST_KEEP_IDLE=1` (a test's switch, read by the native side only): nothing is let go, as before this
   job. For the check "the same frames give the same picture with and without it".

### What to expect in the four rows (NRD chosen, 1924 x 1084)

| Where | total before | hand-over 1 | with hand-over 2 | from |
|---|---|---|---|---|
| nothing traced yet | 1,819 | 1,651 | 1,651 | bounced light's filter 136, wet store 32 |
| outdoors, a town | 4,238 | 4,102 | 3,954 to 4,102 | filter 136; the caustics' room up to 148 less (by the light that lands) |
| outdoors by the water | 6,702 | 6,566 | about 6,450 | filter 136; the room 118 less at 135,000 landed triangles a frame |
| a small interior | 2,734 | 2,598 | 2,450 | filter 136; the room 148 less (no light lands there) |

In the line: `histories` 530 -> 428 (-101.75), `targets` 540/557 -> 506/523 (-34), `wetStore` 32 -> 0 until
a frame has water, `caustics` 216 -> about 67 where no light lands (the waves' copies stay).
With Denoising None, Ray Reconstruction or Ray Regeneration instead: 407 MB less than before in every row
(`histories` 530 -> 225, `targets` -102), and NRD's 262 gone 120 frames after the switch.
With path tracing OFF (hand-over 2): the render size's textures, the filters', the denoiser's and the caustics'
room go after 5 seconds: about 1,100 to 1,600 MB less than today's "off", in every scene.
(By the water the brief's 6,702 holds 1,322 MB of caustics from a change that is out of the base again; with the
base's 215 the row was about 5,595, and becomes about 5,340.)
What to read in the game's `TGPathTracing.Native.log`: the lines "PT memory bytes", "PT memory by distance",
"Render size ... recreated (N MB; ...)", "... are let go", "The caustics' room: N triangles ...", "Path tracing
is off: what only a traced frame needs is let go (N MB)".

### What could go wrong (hand-over 1)

- Switching Denoising or entering a debug view makes or frees 8 to 14 textures in that frame: a hitch of a few
  milliseconds once. Flipping between two denoisers faster than every 120 traced frames keeps both.
- If a denoiser fails to start in the very frame it was to run, the built-in one's textures are made while the
  list is being recorded (the one place left where "no room" would end the session). Before this job there was
  no such place for these textures; it needs two failures in one frame.

## 2. HAND-OVER 2 (19:33): the caustics' room, off gives the memory back, the count by distance

Frozen copy of the eleven changed files: `<scratch>\wt\memory1-handover2\` (same relative paths; equal to the
copy at 19:45: `tracer.cpp` F30C2595DA86, `scene.cpp` 449591F69BA4, `scene.hpp` 181204E5891F,
`scene_water.cpp` F5197C90AEAE, `texture_share.cpp` 6FC26E038586, `Session.cs` 773D0679E80C; the tracer DLL
built from them: 00097D1AF243 in `%TEMP%\tgb\memory1`; the GPU check's runs used the build before it,
07FA4E5F76EE, which differs by three numbers in the memory line). C++, one line of the plugin, a test file. No
shader, no ABI, no option bit.

1. **The caustics' room is what a frame builds over** (`ensure_caustic_room`; `make_caustic_triangles` now only
   sets the limit, a number). Before: room for the limit, 1,048,576 triangles = 158 MB, from the first frame a
   water slot was live, while each frame writes and builds over `causticBuilt` (65,536 at least, half as many
   again as the last frames landed). Measured in the logs: the tavern, a dungeon and every scene with a far sea
   land 0 triangles and build over 65,536; by the sea 135,000 land and 262,144 are built over.
   - The room follows that count: 65,536 triangles = about 10 MB, 262,144 = about 40 MB. It grows in the frame the
     count passes it, and shrinks once 600 frames in a row built over half of it or less.
   - Why the picture is the same: the count a frame may write (`causticBuilt`) is worked out exactly as before
     (the limit stays 1,048,576 and grows as before). A frame's passes read the light of the frame before until
     its own caustics pass has run; a new room takes the old one's corners and light along (a copy in the same
     command list, before any pass), and the old structure lives through that frame.
   - One difference, for the better: before, when the limit grew (over a million triangles) the frame after read
     an empty light buffer (one dark frame of caustics). Now nothing is lost in that frame.
   - A room made from nothing empties the caustics' counters first (the pass's own mode 0), so no pass asks a
     structure that was never built (before, the first frame of a new scene with water did; it found nothing).
   - With no caustics pass for 120 frames (the Caustics row off, no water slot) the room is let go.
2. **Off gives the memory back** (`release_frame_memory`, `Session.cs` one line). The plugin's maintenance event
   (render event 7, four times a second) now says whether tracing is OFF (the Mode row, or it cannot run this
   session) or merely paused for textures. Off for 5 seconds: the render size's textures and reservoirs, the
   filters', the denoiser's own and the caustics' room are let go: 1,100 to 1,600 MB at 1924 x 1084. The next
   traced frame makes them again (milliseconds: the same code a change of render size runs). The traced world
   itself stays (so turning it back on shows the picture at once).
   - FOUND: today "Off" frees nothing. The plugin stops handing frames over and the native side keeps
     everything: 1.8 GB of its own plus the whole traced world (3 to 5 GB outdoors) until the game unloads a
     scene. So an A/B with Insert compares the game against the game with 4 to 6 GB of its card taken.
   - NOT done on a pause for textures, on purpose: freeing 1.3 GB there would let the textures open, tracing
     would resume, take the 1.3 GB again and stand over the budget. That needs the one rule (section 7).
   - An older plugin sends 0 (as before): nothing changes with it. An older native side ignores the 1.
3. **The count by distance** (`GpuScene::distance_bands`, the log's "PT memory by distance from the camera"
   line, every 10 s after the memory line): section 5. It cuts nothing. It only counts.
4. **Its own GPU check**: `tests/pathtracing_memory1_checks.inl`, alone with `TGPT_TEST_MEMORY1_ONLY=1` (included
   right after the test's warm-up; the whole run does not take it). It runs the same frames twice, once with
   `TGPT_TEST_KEEP_IDLE=1` (everything kept, the caustics with the limit's room: as before this job) and once
   without, and compares: the memory must differ, every pixel must be the same.
   - built-in denoiser -> 140 frames of no denoiser -> built-in again: 18 textures gone, then back; the picture
     from textures made again equals the picture from textures kept;
   - with NRD, where the build has it (not this worker's build): the bounced light's six go, the other twelve
     stay; NRD's own go 140 frames after it stops;
   - a pool of water with waves: the wet store appears with the water; the caustics' room is over 100 MB
     smaller than the limit's; the picture is the same with either room; without water the room goes.

### The check: what happened

- First try, 19:22: it never reached its own code. The pass cache beside the test's DLL was empty, and the
  driver's first making of the passes took the test past its memory ceiling (10,000 MB, set as the brief says)
  after 90 seconds: the test ended itself (exit 4). Not a finding about this job's code.
- Second try, 19:34: the tracer's passes are stored by their own bytes, this build embeds the main tree's shader
  headers, and the game had run them, so the game folder's 111 cache files were COPIED (a read there) into this
  worker's own folder (`%TEMP%\tgb\memory1\tgpt-native\TGPathTracing\ShaderCache`). All 44 passes then loaded
  from it in 5 seconds. But the two reordering passes (`giSer`, `specularSer`) are not in that cache: the
  driver keeps them itself, for each program apart ("D3D12 state object (driver-managed cache)"; in the game 3
  seconds each, because the driver has made them for the game). For this test program it made them from
  nothing, and the test passed its ceiling again after 4 minutes (exit 4). No frame was traced.
- THE CEILING WAS NOT RAISED, and that is this worker's decision to report, not to change: the first making of
  those two passes is on record at 33 GB and more (`tests/pathtracing_gpu_test.cpp`, the note at
  `memoryCeilingMb`), another tree was compiling shaders, and the coordinator's runs of the game need the PC's
  memory. A run without a ceiling is the coordinator's to allow, with the PC to itself.
- Third try, 19:57 (after the coordinator's run of the game had ended): the test's own mode
  `TGPT_TEST_NO_AGILITY=1` (Windows' own Direct3D 12 runtime: no reordering passes, no micromaps), same ceiling.
  The plain passes took 8 minutes to make the first time (1.8 GB, far under the ceiling) and seconds after.
  It passed. Three more runs followed as the check grew (each 45 to 100 s): all passed, the last at 20:14.
- For the DirectX Raytracing 1.2 route the coordinator needs ONE start without a ceiling, with the PC free:
  `build\native\tgpt_gpu_test.exe` with `TGPT_TEST_MEMORY1_ONLY=1` lets the driver make the two passes for that
  program once (ten minutes or more, tens of GB); every later start takes seconds. Or see it in the game,
  which has them: the lines to read are in section 1.

### The check: what it measured (run 6, 20:14; `%TEMP%\tgb\memory1\memory1-check-run6.out.txt`)

1280 x 720, the test's room. "The same" below means: the largest difference of any pixel's channel is 0.

| Case | Memory | Picture |
|---|---|---|
| Built-in denoiser, then 140 frames of no denoiser, then built-in again | histories + targets 471.7 -> 291.7 -> 471.7 MB (kept by the test's switch: 471.7 throughout) | kept against let go: the same before, during and after; the same run twice: the same |
| A pool with waves (25,000 triangles of light land) | caustics 67.2 MB with the room a frame builds over, 215.5 MB with the limit's room (as before this job); 57.2 MB 130 frames after the water went; the wet store 0 before any water, 32 with it | the same with either room |
| A pool of twice the size (45,000 land: the room grows from 65,536 to 131,072 in the run, with the copy) | 77.1 MB grown, 215.5 MB the limit's | the sum of all 40 frames the same with the grown room as with the limit's |
| Off for five seconds (render event 7 with data 1), then tracing again | targets + histories + denoiser 471.7 -> 0.0 -> 471.7 MB (the log: "let go (475 MB)") | the same twelve frames before and after: the same |
| A pause that is not "off" (data 0) | nothing goes | |

What it also showed:
- **A lake over the whole floor is not the same from run to run, with either room** (run 5: the sum of 40 frames
  differed by up to 91.8 in a channel between two runs of the SAME build and room, mean 0.010; grown room
  against the limit's: mean 0.008). In its first three frames far more triangles want to land than the frame may
  write (65,536 until the count is read back), and which ones fit is the order the GPU's threads come in. That
  is so before this job too. It is a real, short loss of caustic light whenever the landed count jumps (the
  first frames by a large water): a defect of its own, not this job's.
- **The line by distance works** (the pool: 33 pieces of water, 32,770 triangles, 5 MB, all under 50 m).
- **Windows counts far more than the tracer's line sums**: in the test, with nothing else in the process, the
  line says `windowsCounts` 2,181 MB where the tracer's own figures on the card sum to 933 MB. 1,248 MB the
  line does not see: its own shared textures a second time (`ownShared` 339 MB, if they are counted twice) and
  the rest the driver's own (the passes, two devices). So of "the game's own 9 GB", about 1 GB may be the
  tracer's that its line cannot count. The game's log will say (`notTracer`, with path tracing on and off).
- Not run by it: NRD's part (not in this build), the room's shrinking (600 frames), the limit growing past a
  million triangles.
- To run it again: `<scratch>\memory1-run-check.ps1 -Tag <name> [-NoAgility]` (it stages the main tree's
  installed bridge and tracer folders with this build's tracer DLL over them, waits for the main tree's
  `-GpuCheck`, runs once; logs `%TEMP%\tgb\memory1\memory1-check-<name>.log` and `.out.txt`).

## 3. The measured sizes (the device's own count at 1924 x 1084)

`<scratch>\memory1-measure\measure.exe 1924 1084` asks `ID3D12Device::GetResourceAllocationInfo` (the call the
memory line uses); nothing is allocated. A texture counts about 7 % more than its pixels times its bytes:

| Format | bytes a pixel | MB |
|---|---|---|
| R16G16B16A16_FLOAT, R32G32_UINT | 8 | 17.000 |
| R32G32B32A32_FLOAT | 16 | 33.875 |
| R32_FLOAT, R32_UINT, R16G16_FLOAT, R8G8B8A8_UNORM, R10G10B10A2_UNORM | 4 | 8.5625 |
| R16_FLOAT | 2 | 4.3125 |

Checked against the log: with these, `targets` sums to 566,624,256 bytes and `histories` to 556,007,424: both
are the log's own figures to the byte (run `20261009-165909`, first line).

## 4. Every fixed allocation (job item 1)

"Needed with": B the built-in denoiser, N NRD, RR Ray Reconstruction, RRG Ray Regeneration, 0 None.
"Now": what this job did. MB at 1924 x 1084.

| What (the line's figure) | Format | MB | Who reads or writes it | Needed with | Now |
|---|---|---|---|---|---|
| HDRP's frame, two sets by frame parity: G-buffer 0, normals, G-buffer 2 (RGBA8), G-buffer 3 (RGBA16F), depth (R32F), motion (RG16F), exposure 1x1 (`targets`) | shared with D3D11 | 2 x 59.9 = 119.75 | D3D11 copies them every frame; `pt_prepare`, `pt_primary`, `pt_compose`, `pt_probe`, every pass's reprojection | all | kept (see section 8, choice 6) |
| Outputs a, b (RGBA16F), c (R16F), g0, g1 (RGBA8) (`targets`) | shared | 55.4 | `pt_compose` writes, the D3D11 composite reads; g0 and g1 are the tracer's own guides for Ray Reconstruction | all (g0, g1: RR alone, 17 MB, but two passes write them without asking) | kept |
| Surface x2 (`targets`) | RGBA32F | 67.75 | `pt_prepare` writes; every pass reads this frame's and the last | all | kept |
| Normal x2, material x2 (`targets`) | RG32UI | 68 | the same | all | kept |
| The three signals: bounced, direct, reflections (`targets`) | RGBA16F | 51 | the light passes write, filters and compose read | all | kept |
| Hair's own, the reflections' guide (`targets`) | RG32UI | 34 | `pt_prepare`, `pt_di_spatial`; `pt_specular`, the reflections' filter | all | kept |
| The camera rays' own motion and emission (`targets`) | RG16F, RGBA32F | 42.4 | `pt_primary` writes, reprojection and compose read | all (not on the old G-buffer route, a test's) | kept |
| Built-in filter, bounced light: ping, pong (`targets`) | RGBA16F | 34 | `pt_atrous` | B | made on need |
| Built-in filter, direct light and reflections: four scratch (`targets`) | RGBA16F | 68 | `pt_atrous`, compose | B, N | made on need |
| Fog Adaptation's colour before the fog (`targets`) | RGBA16F, D3D11 | 17 | the composite, the fog pass | with the Fog Adaptation row | kept (made at its first frame, as before) |
| Ray Reconstruction's guides, five D3D11 textures (`targets`) | | not measured | `write_guides`, TG Better Upscaling | RR | kept: their pointers are handed to another mod |
| ReSTIR's reservoirs: direct 2 x 24 bytes, bounced 2 x 32 bytes a pixel (`histories`) | buffers | 96.4 + 128.6 | the ReSTIR passes | all | kept |
| Built-in filter, bounced light: history x2, moments x2 (`histories`) | RGBA16F, RGBA32F | 101.75 | `pt_temporal`, `pt_atrous` | B | made on need |
| Built-in filter, direct light: the same (`histories`) | | 101.75 | the same | B, N | made on need |
| Built-in filter, reflections: the same (`histories`) | | 101.75 | the same | B, N | made on need |
| NRD's inputs and results, seven (`denoiser`) | R32F, RGBA16F, R10G10B10A2 | 102.1 | `pt_nrd_prep`, NRD | N | let go when idle |
| NRD's own pools, 14 textures (`denoiser`) | NRD's | 159.75 | NRD | N | let go when idle |
| Ray Regeneration's guides, five; its context (`denoiser`) | | 51.25 + AMD's | AMD's denoiser | RRG | let go when idle |
| The wet ground's store (`wetStore`) | buffer, 8 levels x 256 x 256 cells x 64 bytes | 32 | `pt_wet`, the surfaces' shading | a live water slot, the wet ground on | made with the first water |
| The radiance cache (`radianceCache`) | buffers, 1,048,576 cells x 36 bytes | 36 | the bounced light's passes | unless its row is off | kept |
| The yardstick (`yardstick`) | R32UI + counters | 9 | `pt_prepare` every 30th frame and in its view; the harness reads its numbers | a diagnostic | kept (choice 7) |
| The caustics' room (`caustics`) | buffers | 158 for a million triangles | `pt_caustics`, `caustic_light` | a live water slot, the Caustics row on | follows what a frame builds over: 10 to 40 MB mostly |
| The waves' copies: displacement and gradients a slot and parity (`caustics`) | shared | 57 in the logs | the water's surface, the caustics | a water slot with waves | kept |
| The upload rings (`transfer`) | 3 x 48 MB | 144 | every frame's constants and scene uploads | all | kept: the PC's memory, not the card's |
| Colour atlas: 4,096 tiles of 64 x 64 and 128 detail tiles of 512 x 512 (`atlases`) | RGBA8 4096 x 12288, shared | 192 | materials not read from the game's own texture; hair's, eyes', glass's detail tiles | all | kept (choice 5) |
| Alpha atlas: 1,024 tiles of 256 x 256 (`atlases`) | R8 4096 x 16384, shared | 64 | alpha tests and micromap bakes where the game's own texture is not open | all | kept (choice 5) |
| Cookies, sky, the fog's sky (`atlases`) | | 5.25 | lights' cookies, the sky | all | kept |
| Hair's strands (`atlases`) | R8 8192 x 8192 with mips, shared | 85.4 | hair's alpha | a scene with hair | already made by the first hair bake, let go with the scene |
| Characters' positions on their way, two (`skinned`) | RGBA32F 4096 x 512, shared | 64 | the skin copy | a character on screen | already made by the first character |

## 5. Does the unlimited world cost the memory? (the owner, 2026-10-09 19:40)

**The verdict, in one sentence: it is a part of it, not the main reason: the game's own 9 GB and the 1.9 GB the
tracer holds before it has traced anything already take 10.9 of the 13.0 to 13.8 GB, the traced world then adds
1.3 GB in the tavern, 3.0 GB in the town and 2.4 to 4.8 GB by the water, and how much of that is FAR from the
camera no log has said until now.** (The line that says it is added: below.)

Two things the question mixes:
- "A ray can travel forever": costs no memory. A ray's reach is time only, and only where there is something
  to hit.
- "We trace infinitely out into the distance": the traced world is what the game has LOADED and draws from the
  camera: one level of detail a thing, the one the game draws, and nothing where the game draws nothing
  (`SceneCapture.cs`: "LOD as drawn"). It is as large as the game's own streaming makes it, not endless.

### What grows with the world, measured (MB; runs `20261009-162628`, `-165909`, the brief's row for the walk)

| | tavern room | town (`metal`) | by the water (`water-day`) | the owner's walk by the water |
|---|---|---|---|---|
| instances traced | 5,331 | 16,234 (of 25,273) | 10,275 | |
| triangles | 1.5 M | 4.4 M | 2.7 M | |
| plants in the wind | 0 | 11,303 in 636 groups, 6.7 M triangles | 2,555 in 286 groups, 1.1 M | |
| water's pieces | 762, 0.69 M triangles | 1,379, 1.21 M | 2,459, 2.05 M | |
| the game's textures held open | 703 | 1,246 | 776 | 1,228 |
| plants' posed copies | 0 | 795 | 131 | 765 |
| acceleration structures | 210 | 411 | 485 | 918 |
| geometry | 80 | 208 | 176 | 320 |
| characters | 93 | 99 | 77 | 103 |
| caustics (room and waves) | 159 | 215 | 689 (not the base's) | 1,322 (not the base's) |
| scratch, records, cloth | 28 | 56 to 90 | 93 | |
| **the world's part** | **1,273** | **3,030 to 3,064** | **2,427** | **about 4,800** |
| of it, the water's surface as triangles | 137 | 218 | 386 | |
| the fixed part (targets, histories, atlases, NRD, rings, cache) | 1,918 | 1,918 | 1,918 | about 1,900 |
| **total** | **3,191** | **4,948 to 4,981** | **4,345** | **6,702** |

What stands out:
- **The plants are the largest thing the world adds that the game itself does not hold**: 795 MB in the town.
  The game instances a plant (one mesh, drawn many times, posed in the vertex shader). The tracer holds every
  plant as its own posed copy: 32 bytes a vertex, 12 a triangle, and an acceleration structure of about 67
  bytes a triangle, none of it shared between two plants of the same mesh. 6.7 M triangles of plants against
  4.4 M of everything else.
- **The game's textures (1,246 MB) are the largest single figure**, and Windows counts each a second time. Which
  of them are needed only by far things is what the new line says.
- **Water costs in rooms that have none in sight**: 137 MB of water triangles and a 158 MB caustics room in the
  tavern; 217 MB of water in a castle dungeon (run `20261009-150909`). The sea is in the loaded world, so it is
  traced. Its triangles grow with the distance only slowly (an edge is a 64th of its distance: every doubling of
  the distance adds about the same count), so the FAR sea is roughly 4 of its 10 doublings: about 40 % of it.
- **The tavern is the clearest case of "far"**: the game keeps the outdoor map loaded while the player is in the
  room (the harness's line: "in ApplicationScene, CampaignMap_HOS, ..., Interior_Tavern_02", the camera at
  height 502, away from the map). So the tavern's 1,273 MB holds the room AND whatever of the outdoors the game
  still has loaded: 5,331 instances, 0.69 M triangles of sea. In that scene the new line's far places are the
  outdoors, and its near places the room: the cleanest reading of what "far" costs.
- The world's part by distance (near, middle, far) cannot be read from any log so far: the native side never
  logged where things are.

### The line that tells (added, cheap: a count every 10 seconds, nothing is cut)

`PT memory by distance from the camera, bytes (...)`, right after "PT memory bytes", in six places: under 50 m,
50 to 150 m, 150 to 500 m, 500 to 1500 m, over 1500 m, and "named by no instance" (characters' meshes; textures
only hair, decals or a material program read). For each: `instances`, `instanceTriangles`, `meshes`, `geometry`,
`acceleration`, `plantGroups`, `plantTriangles`, `plants`, `waterPieces`, `waterTriangles`, `water`, `textures`,
`textureBytes`. Counted so that a band's figure is what a cut at its near edge would free: a mesh and a texture
at the NEAREST instance that uses them (they stay for that one whatever goes beyond), a group of plants at its
16 m cell, a piece of water at its nearest point. At its end: how much of the arenas' chunks the meshes really
use (the rest is free room inside chunks held whole: see section 6).
One run of the three scenes with this build answers the question in MB.

### What could be saved WITHOUT a limit that changes the picture

| What | Where it saves | MB (town) | State |
|---|---|---|---|
| The plants' acceleration structures compacted (as the static meshes' are: those shrink to 38 % on this card) | `plants` | 150 to 250 of 795 | written up (section 6); C++ only, same triangles |
| A plant's texture coordinates, colours and normals read from its mesh instead of copied into every plant's posed copy (16 of 32 bytes a vertex) | `plants` | about 145 | written up; needs a shader change in what a hit reads |
| The water's pieces' acceleration structures compacted, and their pools' pages given back | `acceleration` | 60 to 120 of 218 | written up |
| The caustics' room by what lands | `caustics` | up to 148 | BUILT (hand-over 2) |
| The arenas' free room given back (chunks held whole for a few live pieces after compaction) | `acceleration`, `meshGeometry` | not known: the new line says | written up |
| The game's coarser level for far things | | 0 | already so: the plugin takes the level the game draws, and nothing where the game culls |

### What WOULD change the picture: choices, not built (the ruling stands)

| Choice | What the player would see | MB |
|---|---|---|
| Plants beyond a distance traced in their still pose, as instances of their one mesh (no posed copy) | far grass and leaves and their shadows stand still in the tracer; near ones sway as now. At 200 m a pixel is 11 cm wide at this size, so the sway there is about a pixel | the `plants` figure of the bands beyond the distance (the whole is 795 in the town) |
| One posed copy shared by all plants of the same mesh in a cell | they sway in step (the game's wind differs by place) | up to most of `plants` |
| The sea's far pieces as flat water (no wave triangles beyond a distance) | far waves flat in reflections and at the horizon | about 40 % of the water's 137 to 386 |
| No traced world beyond a distance (the deleted Scene Radius) | far things missing from reflections and casting no shadow; the far picture is the tracer's own camera rays, so it would show holes | the bands' sum beyond the distance: the new line |
| Textures of far things not held open (their 64 x 64 atlas tile serves) | far things blurred in the traced picture | the `textureBytes` of the far bands |

## 6. The parts that grow with the scene (job item 3)

**Plants** (`scene.cpp` `plan_plants`, `pose_plants`; one group a 16 m cell of the world). A group is ONE buffer
(positions 12 bytes a vertex, texture coordinates 8, colours 4, normals 4, each vertex's range 4, then 32-bit
indices) and ONE acceleration structure built `PREFER_FAST_TRACE | ALLOW_UPDATE`, refitted every frame, never
compacted. Town: 9,513,841 vertices x 32 = 290 MB, 6,672,229 triangles x 12 = 76 MB, structures about 428 MB
(67 bytes a triangle): 795 MB. A group is made anew when a member changes its level of detail.
- Sure, C++ only, NOT built (time): **compact each group's structure** after its first build (ask for the
  compacted size with the build, copy compacted into a block of that size, refit that one from then on:
  `ALLOW_COMPACTION` may be combined with `ALLOW_UPDATE`). The static meshes' compaction is the pattern
  (`compact_meshes`): they go to 38 % here (3,416 -> 1,301 MB over 400 batches in one log). A structure that
  allows updates compacts less; 40 to 60 % of 428 MB is a fair guess: 170 to 250 MB in the town. Risk: for the
  frames between a group's build and its compaction both copies exist (a short peak when many groups are made
  at once, as after a load), and groups are remade whenever a level of detail changes while walking.
- Needs a shader change, not built: texture coordinates, colours and normals read through the plant's member
  record from its own mesh (they are the same for every plant of that mesh): 16 of the 32 bytes a vertex,
  about 145 MB in the town. The positions (posed) and the indices (the structure's) stay.
- A choice (changes the picture): section 5's table.

**Acceleration structures** (`acceleration`): static meshes are built `PREFER_FAST_TRACE | ALLOW_COMPACTION` and
compacted a few frames later (`compact_meshes`; 38 % of the built size on this card). NOT compacted: characters
(`PREFER_FAST_BUILD | ALLOW_UPDATE`, rebuilt from skinned positions), swaying cloth's copies, the plants' groups
(above; counted under `plants`), the water's pieces (`PREFER_FAST_TRACE | ALLOW_UPDATE` in pooled blocks of
power-of-two sizes, 32 MB pages), the micromap structures, the top level.
- The arena (`scene.cpp` `Arena`): 16 MB chunks, first fit, a chunk goes back only when nothing in it is live.
  A mesh is built at full size and its compacted copy placed elsewhere; the full-size hole is reused by later
  builds, but a chunk with one small live piece stays whole. In the last town log the static meshes' compacted
  structures summed to about 54 MB while `acceleration` stood at 411 to 508 MB (with the water's 32 MB pages,
  characters, micromaps and the holes). How much is holes: the new line's last part says
  ("the meshes use X of ... chunks"). If it is large: build new structures in chunks of their own and compact
  into the old ones (C++ only, same triangles). Not built: not measured yet.
- The water's pieces: 0.69 to 2.05 M triangles, 137 to 386 MB, about 190 bytes a triangle with its vertices
  (36 bytes each), an uncompacted updatable structure in a block rounded up to a power of two, and pages that
  are never given back while the scene lives. Compaction and blocks that fit would take a third to a half of
  it. Not built.

**Geometry** (`meshGeometry`): what a ray reads at a hit, as floats: positions 12 bytes a vertex, texture
coordinates 8 (a second set 8 where there is one), colours 4, normals 4, tangents, decal colours, 32-bit
indices. 16-bit indices and packed coordinates would halve some of it, but every shader that reads a hit would
have to change: not sure, not built.

**Atlases beyond the fixed part**: only hair's strand atlas (85 MB), already made on need. The fixed 261 MB:
choice 5 in section 8.

## 7. One rule for all of the tracer's memory (job item 4): the design, not built

Today the only rule is on opening the game's textures (`texture_share.cpp` `kBudgetShare`, 97 % of what Windows
gives). Everything else the tracer allocates asks nobody. So: textures stop at 97 %, the tracer's own memory
runs on to 104 %, Windows moves memory out of the card, and a frame takes 1,490 ms. Or the textures wait for
ever and the notice counts textures, which the player can do nothing about.

The rule, in the order things would be asked:
1. **One budget**: what Windows gives the game (`windowsBudget`), less a margin (3 %). One question before every
   allocation the tracer makes for the world: "does this fit under it, by what Windows counts now?"
2. **Never refused** (without them there is no picture at all): the render size's textures and reservoirs, the
   chosen denoiser's own, the frame's uploads. If THESE do not fit, path tracing does not start, and says so.
3. **Refused first, and it costs only time**: growing a scratch buffer, a second copy during compaction
   (compact later, one at a time), a new group of plants while another is being compacted.
4. **Then the world, the farthest first**: a mesh, a group of plants, a texture are asked for nearest first
   (the plugin already registers by the camera). When the next one does not fit, the tracer stops taking more
   and PAUSES: it does not trace a world with holes in it (the ruling: nothing left out).
5. **Paused means everything of the tracer's that a frame needs is given back** (hand-over 2's
   `release_frame_memory`, which then is safe on a pause too: the rule knows the frame's memory must fit back
   in before it resumes). The traced world stays, so it resumes at once when it fits.
6. **It resumes by itself** when the count has room for the frame's memory plus what was refused, with some
   slack so that it does not flip back and forth (resume under 90 %, pause over 97 %).
7. **The player's line** (one notice, on the Path Tracing row's amber icon and on screen):
   "Path tracing paused: this place needs 14.9 GB of video memory with path tracing, and this card gives the
   game 13.2 GB. It comes back by itself when it fits." The two numbers: what Windows counts plus what was
   refused plus what was given back; and the budget.
What is small and sure of it is built: the two numbers are in the log line, and "off" gives back. The rest is a
job of its own: it touches every allocation in `scene.cpp`, and the wrong order pauses a scene that would fit.

## 8. Choices for the owner (nothing here is built)

1. **How long "Off" keeps the traced world.** Now: for ever (3 to 5 GB outdoors while path tracing is off). Let it
   go after N seconds off: the game gets its card back; turning path tracing on again then takes the arrival
   time of a load. (The frame's own 1.1 to 1.6 GB already goes after 5 s with hand-over 2.)
2. **Far plants still** (section 5): up to several hundred MB outdoors.
3. **Far sea flat**: 50 to 150 MB.
4. **A traced distance**: the bands line gives its MB. The ruling says no.
5. **The atlases' fixed 256 MB** (colour 192, alpha 64) grown by need instead of made whole: sure for the picture,
   but both are shared between the two devices and bound in every pass; growing means a new texture and a copy
   at a moment both devices agree on. Saves up to 200 MB in scenes that use few tiles (how many are used is not
   logged: one number to add first).
6. **HDRP's frame copies (120 MB) and the D3D11 copy passes behind them** go when the tracer's own camera rays
   need nothing of HDRP's G-buffer any more (the migration, PT-71): today four passes read them.
7. **The yardstick** (9 MB, and a pass every 30th frame): a diagnostic. Off by default with a hidden switch
   would free it; the harness reads its numbers, so the harness would turn it on.
8. **The radiance cache's 36 MB** when its row is off: trivial, not built (the row is on by default).

## 9. Files changed (against `base73`)

- `native/pathtracing/src/tracer.cpp`: everything above on the native side.
- `native/pathtracing/src/texture_share.cpp`, `.hpp`: `video_memory`, `open_bytes`; `within_budget` asks through
  `query_video_memory` (the same call as before).
- `native/pathtracing/src/scene.hpp`, `scene.cpp`, `scene_water.cpp`: `DistanceBands`, `distance_bands`,
  `water_bands`; a water piece remembers how near the camera was (`WaterUnit::nearest`, for the count alone).
- `native/pathtracing/src/nrd_denoiser.hpp`, `ray_regeneration.hpp`: `has_pools`, `has_context`.
- `src/TGPathTracing/Session.cs`: the maintenance event's data (1: off).
- `tests/pathtracing_gpu_test.cpp` (one include), `tests/pathtracing_memory1_checks.inl` (new).
- `docs/agents/memory-1.md` (this file).
No ABI change: `TGPT_Frame`, `TGPT_Status`, `TGPT_Diagnostics` as they were. No option bit. No shader.

## 10. Workarounds and oddities met (none removed: not this job's)

- "Off" freed nothing (section 2): fixed for the frame's memory; the world is choice 1.
- Ray Reconstruction's five guide textures and Fog Adaptation's copy are never let go once made.
- `VegetationCapture.cs` traces a Vegetation Studio instance past its last level of detail at its coarsest
  level ("past the last LOD the coarsest still shadows"): more than the game draws there. Whether that path
  still feeds the scene, and how many instances, was not checked.
- The plugin's log says "11 water surfaces within the traced scene; the nearest 4 are traced as water": four
  slots (`TGPT_MAX_WATER`). A count that leaves water out: a limit of the kind the rulings name. Not this job's.
- `tracer.cpp`'s comment on `ensure_frame_textures` counted bytes for textures that no longer exist ("HDRP's
  light's shadowed shares"): replaced by the device's own count.
