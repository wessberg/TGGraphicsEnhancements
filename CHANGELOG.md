# 1.2.4 (in testing, not on Nexus)

For the game's 1.31 on Unity 6000.3.21f1. Every number below: `docs/rounds/round-21.md`.

- All mods: FSR and XeSS upscaling and Neural Rendering work again. HDRP's `CustomPass.ExecuteInternal` takes a render
  context now, so every 1.2.3 FSR and XeSS frame fell back to STP and Neural Rendering recorded nothing.
- TG Frame Generation: DLSS 310.9.16 and Streamline 2.14.16; FSR FG 4.0.2 and SDK 2.4.0. A new row, Frame Generation Quality,
  DLSS only: Quality (NVIDIA's default) or Performance (NVIDIA's Preset C; 76 against 73 rendered fps at 4K, 2x). The real render size of DLSS in the log.
- TG Better Upscaling: DLSS 310.9.16, FSR upscaler 4.1.2 (FSR 4 on RX 6000 and newer and some Ryzen chips, AMD's list). DLAA is
  held at 100 % where the query now answers 33.3 %.
- TG Better Upscaling: DLSS runs at full speed from launch. HDRP created it at the size of the first frame, before the render scale took effect, and kept that for the session (76 against 104 fps at 4K, DLSS Performance). The mod now has it made anew at the real size, half a second after start.
- TG Neural Rendering: takes the signed 310.9.16.0 DLL as well as 310.8.0.0 and asks for NVIDIA's lightweight network, Preset B:
  6.4 ms of GPU time at 4K against 11.3 to 11.6 ms on 310.8 (another scene).
- TG Neural Rendering: the download now includes NVIDIA's signed `nvngx_dlssnr.dll` 310.9.16.0, unmodified, from NVIDIA's RTX Remix release, under NVIDIA's public permission for modders. A patched DLL is copied over it after each update.
- Build: `Build.ps1 -Skip <mod>`; TG Path Tracing does not compile against Unity 6000.3 and is tabled. Harness: a step's
  `upscaler`, recipes `release-check` and `dlss-reapply`; NVIDIA Overlay's memory counter no longer holds a run back.

# Unreleased

- Harness (2026-10-12 12:15): a step's `"disableMotionBlur": true` works on any step, not only on Neural
  Rendering's (`Runner.cs`): frames grabbed while the camera moves are judged without the game's motion blur (the
  owner). The recipe `nrd-motion` (no denoiser, RELAX, REBLUR with the camera turning and swinging) carries it; its
  first run, without it, was smeared in every frame and is thrown away. `Queue-PtRun.ps1 -KeepFrames` keeps a run's
  raw frames for moving pictures; `tools/harness/motion_clips.py` makes them (two steps side by side at the game's
  own pixels, lined up by where the camera looks; it refuses frames that do not say the blur was off).
  `tools/harness/compare_lights.py` lays the game's lights of two audits side by side (PT-147: the town's lamps
  read 0.125 or 524,288 by what was loaded before in the session).
- PT-114 (2026-10-12 12:10): THE CAUSTICS' GRID, THE SPEED-UP IS THE DEFAULT. Worker caustgrid's three steps (the
  lights' pick in a pass of its own; only tiles with water under them and only the corners a cell needs; no light
  traced whose rays cannot land in view; `docs/agents/caustgrid.md`) were merged at 11:42 behind the hidden
  `Caustics grid trial`, 0. The game answered (run 20261012-114808, the owner's stream on, 0 / 1 / 2 / 3 / 0 in one
  run): the caustics' stage 5.25 / 3.28 / 3.12 / 3.13 / 5.41 ms in the town and 5.34 / 3.42 / 3.12 / 2.75 / 5.27 in
  the forest; the beach's frozen picture reads 56.0 / 56.2 / 56.3 with 0 / 3 / 0 (offline the written triangles are
  the same, checksum for checksum). So the setting's default is 3 now, and the recipes leave 3 behind. Owed: the old
  way taken out of `pt_caustics.hlsl` and the tracer (0 stays as the tests' yardstick until then).
- PT-138, PT-37 (2026-10-12, worker fog-3; `docs/agents/fog-3.md`): THE FOG CHECK'S GROUND, AND WHY THE NIGHT'S HAZE
  IS GONE. (A) The fog check's first run read the ground under fog x1.24 to x3.43 of its exact value. The tracer was
  right: the check's black ground is black by its base colour only and still reflects 4 %, and its highlight of the
  sun and of the lamp was not in the exact value. Added by hand (`groundShine`), every region is within 1.6 %; with
  a ground that reflects nothing (`TGPT_TEST_FOG_ABSORBER=1`) the tracer reads the old value. The check's pins are
  filled (PT-138 (6): the per-pixel route's numbers from the last build that had both routes; the cells read x0.995
  to x1.016 of them), so its room cases and the two daylight fogs are judged now. The check has the night's fog too
  (a fog that thins with height, known exactly by quadrature, HDRP's fog by hand printed beside it): seventeen
  cases. (B) The game's blue night haze is the SKY's light, not the moon's: HDRP adds its ambient probe to every
  point of its fog at full strength (no fog in its way, no shadow), x8.5 to x19 of the traced haze; the tracer dims
  the sky's light by the fog above the point, which in the night's fog (a mean free path of 9 m) leaves a level ray
  a 28th of it. The moon's share is the same in both but for its volumetric dimmer (x1.23). NOT MERGED: the
  worker's trial that leaves the sky's light undimmed (frame flag bit 27, a hidden setting, the recipe `fog-sky`):
  it is HDRP's shortcut, and the owner's rule of the day is "path traced, always". Merged by hand at 12:20, the
  trial's lines taken out of the check; passed in the worker's copy with them (18 cases).

- PT-99 (2026-10-12, worker waterblas; `docs/agents/waterblas.md`): THE WATER'S PIECES ARE TRACED IN GROUPS. The
  water's surface was 1,400 to 2,500 bottom-level structures, each refitted by a driver call of its own every frame
  (2 to 3.7 ms of CPU a frame while the GPU waits). Now some dozens of pieces of one slot and cut level share one
  structure, one refit call and one instance (a geometry a piece; a hit finds its piece by the instance's ID plus
  the geometry's index, as any submesh); a group whose pieces changed is built anew (under 1.5 groups a frame on a
  walk); the pools keep their free stretches. No pass that traces changed, only the pose pass. In the game (its
  first DLL, `20261012-094311`; standing, the world running): the record's CPU a frame THE BEACH 7.00 to 4.3-4.6 ms
  (the water's pose 3.72 to 0.20), THE FOREST 6.2 to 3.7 (2.70 to 0.15), THE TOWN 7.2 to 6.1-6.4 (2.05 to 0.14); the
  structures' pages 160 to 128 MB. Its last DLL with the record's self-check on and eight walks in the forest
  (`-105944`): no difference found, no GPU stop. Offline 212 frames of a walk over the sea give one hash for the
  groups, the same code ungrouped and the old DLL; three mutants fail.
- PT-99 (2026-10-12): THE SUN INSIDE THE WATER IS ASKED NO FINER THAN THE RAY THAT ASKS IS WIDE, the rule with no
  switch. A point in the water's body makes a patch of waves to ask how much sun arrives there; its cells were as
  fine as the waves (2 cm) whoever asked, and a path's ray after its first scatter is metres wide. Now a patch's
  cell is no finer than half the width of the path's own ray (the water's path carries the width), and a coarser
  cell is summed as every other: a triangle's sun is the area of its three moved corners across the light. The
  beach's tracer 121 ms to 73 (`20261012-041726`), the same light in a frozen A/B of the final build (`-101445`:
  56.1 / 56.4 / 56.4 with the rule on, off, on), offline the sun's share within 0.16 % at every ray width and every
  band of the picture within 0.5 % (`docs/agents/campspeed.md`, `campfix.md`). Tried the same day and taken out: a
  coarser cell's corner carrying its own facet's sun (it moved the sun's share by 0.6 to 1.1 % and changed the
  pattern: found by the worker's own offline check), and one small ray against the ground before a patch is made
  (no gain in the game). The hidden `Speed trial` 64 that turned the rule off for the comparison is gone, with the
  recipes `sun-cell` and `sun-cell-still`.
- PT-114 (2026-10-12): IN AN INTERIOR THE MAP'S WATER IS NOT TRACED. The game's interiors float above the map they
  belong to (a tavern 470 m over the sea) and the map stays loaded: from inside, the tracer's water slots went to
  the map's sea, lake and rivers, 762 pieces of sea were posed every frame and the caustics' grid was laid over
  them, for water no ray from the room can meet. While an "Interior" scene is loaded a water surface of another
  scene now gets no slot and HDRP does not draw it (`WaterCaustics.LeftOutside`, `WaterSurfaceRendering.Surface`,
  `SceneLights.InteriorLoaded`); an interior's own water is traced as before. The tavern, the same run, with and
  without (`20261012-085406`): a frame 49.6 to 43.7 ms (20.2 to 22.9 fps), the tracer 37.7 to 33.0, the caustics
  4.2 to 0; the picture the same (luma 17.9 / 17.9, flicker 0.36 / 0.36, detail 5.44 / 5.45). The plugin alone. A
  source check pins the rule.
- Tools (2026-10-12): `tools/harness/Clear-PtHarnessRuns.ps1` also deletes the workers' old builds in `%TEMP%\tgb`
  (the owner, with 8 GB left on the disk and 145 GB of them there: "delete all the old ones, and make sure our
  Clear-PtHarnessRuns.ps1 tool also does that"): a folder or loose file in which nothing was written for
  `-WorkerDays` days (2; 0: never), a link inside one unlinked and never followed, the tree copies untouched. Its
  test has a sixth case for it on made-up folders and names a workers' folder of its own in every call; four
  mutants fail it (the days ignored, `-Keep` ignored, a folder's own making counted as a write, a deletion that
  goes through a link). That day it freed 130.8 GB (3,266 folders and files; 134 GB free after).
  `tools/harness/analyze.py`: a run during which a program encoded a stream says so in its report ("A STREAM WAS
  BEING ENCODED"), because such a run's speed is not to be quoted: with Sunshine at 20 % of the video encoder and
  under the guard's 4 % of the 3D engines the forest's bounced light read 19.9 ms for 17.4 and its reflections
  27.0 for 24.0, nothing else moved (`20261012-082625` against `-061203`).
- PT-118 (2026-10-12, worker fogrule2 after fogrule; `docs/agents/fogrule2.md`): THE CAMERA'S FOG IS TRACED IN CELLS
  OVER ITS WHOLE WAY, and the per-pixel camera route is gone. The cells follow the camera as RTX Remix's do (render
  size / 16 across; 48 slices to 20 m by a square) and go on where Remix's end: 48 more slices to the far distance,
  each a quarter deeper than the one before. A cell takes one sample a frame at a point the camera's rays read (not
  behind what its pixel shows), keeps up to 254 frames, and holds the sun's and the sky's light, the mist's cache
  and of each lamp the share that is smooth at a cell's width; each pixel walks the cells with one point of the
  medium a slice (`pt_fog_integrate`) and adds a lamp's light right by it (`pt_fog_tail`). Why the whole way: with
  cells to 20 m and one raw point a pixel past them, the game's foggy valley flickered 3.6 times the per-pixel
  route's (1.17 against 0.33), because 72 % of that fog's light comes from past 20 m; with the cells over the whole
  way 0.17. The game, frozen, old route / cells, flicker with no denoiser: tavern-room 0.40 / 0.36, vista 0.99 /
  0.46, night 1.85 / 0.20, fog 0.33 / 0.17; under NRD 0.40 / 0.18, 0.94 / 0.27, 1.92 / 0.16, 0.32 / 0.16 (runs
  `20261012-051553`, `-053455`). The light (run `-065226`): 17.96 / 17.92, 133.51 / 132.61, 54.50 / 53.83, 91.42 /
  91.31.
  The fog's target is RGBA32F (it was 16-bit): in the vista the reference radiance was 5.7e8 and the fog over a
  near rock 0 to 8 of a 16-bit float's smallest steps, each half a percent of white: the cells showed it in steps
  and the per-pixel route's blend rounded it to nothing (PT-136; the vista's old route came up from 130 to 133.5
  with 32-bit targets).
  OUT with the old route: `pt_fog.hlsl`, `pt_fog_filter.hlsl`, `pt_fog_reservoir.hlsli`, the hidden `Traced fog:
  froxel cache`, `Traced fog: froxel cache trial` and `Traced fog: froxel cache leave out`, the grid's read-back
  and its log line, frame flag bits 19 and 24 to 29 (free), the stage "fog filter" (the stages are "fog cells",
  "fog walk", "fog lamps": 14, were 15), two of the fog's three targets, `fog_lamps_along`'s out-parameter for the
  old route's history, the recipe `fog-froxel-b`. ADDED: nothing in the API; the recipe `fog-turn` and the
  scenarios `fog-edge` and `vista-night`; a source check. The check (`TGPT_TEST_FOG_FROXEL_ONLY`) holds the one
  route against what is known exactly outdoors and against pins in the room (the pins are still empty: PT-138).
  Tried on the way and taken out: the cells' light weighed by the medium's scattering (no change in four scenes,
  8 to 20 % more of the cells' pass). Open: PT-137, PT-138. Merged into main 2026-10-12 08:00.
- PT-125, THE GPU RESETS IN THE FOREST: FIXED (2026-10-12 03:00; seen in the game: 24 walks without a reset where 4
  of 4 runs had reset within five walks). `pt_sway.hlsl`: each vertex of a group of plants keeps the number of its
  range (which submesh of which member mesh it is of), written once at the group's making and read by every later
  frame's pose. It was written as the range's PLACE IN THAT FRAME'S BUFFER, and that buffer is laid out anew every
  frame, the groups in use one after another: when a group before it came or went (a walk), a group's vertices read
  other groups' ranges and members, at times records left past the buffer's end whose meshes had been let go, and the
  read through such a mesh's view reset the GPU. Short of a reset it posed every plant from other plants' meshes:
  after a walk the traced picture had NO GRASS AND NO TREES (`20261012-025731\forest\4-walk-3\frame.jpg` against
  `-030621`'s). Now the number is the range's number within its group, added to the group's place when read, and a
  number past the group's ranges counts as none. HOW IT WAS FOUND, kept in the tree: the scene stage's hang marker
  names the stamp and the kind of work that did not end (GPU progress marker slot 3), the plants' work is numbered
  step by step (each compacting copy, each pose dispatch, each build and refit: slots 4 and 5,
  `GpuScene::plant_step`, `describe_plant_steps`), and the log's "The GPU stopped" line names the group. A test
  switch for the plants' compaction (the hang came without it too) was in for one run and is out.
- PT-129's leads, guards against four loops with no end (worker nosun, by reading; `docs/agents/nosun.md`): a ray
  that is no number is inside no fog box (`pt_medium.hlsli`; it counted as inside every box up to the ray's end of
  1e6 m); the two box-tracking loops end after 65,536 steps (40 are usual; for a bound so large that a step no longer
  moves the ray: no real fog reaches it); `psr_finish` (`pt_psr_capture.hlsli`, mirrors under NRD) draws a direction
  64 times at most and then keeps it (on a smooth, normal-mapped surface seen low every draw could be refused);
  `fetch_hit` takes a normal whose length is past what a float holds as none. The mirror under NRD on that build:
  detail 2.42, boil 0.25 (`20261012-034258`; 2.53 and 0.24 before). Also from nosun, for tests: `TGPT_TEST_SM66`,
  `TGPT_TEST_NOSUN`, `TGPT_TEST_TRACE`, the mode `TGPT_TEST_NOSUN_ONLY` (never run).
- PT-99, the CPU's time to record a frame's scene is in the log by section ("PT record CPU 10s", `RecordCpu` in
  `scene.cpp`): the town frozen 5.8 ms a frame, the forest running 9.5 to 9.8 (its lights' grid built every frame:
  3.4), 13 to 26 on a walk. The GPU waits for all of it. Worker reccpu cuts it.
- PT-131 was no place: the harness's walking camera flew level into a hill (worker campfix). The harness
  (`src/TGPathTracing.Harness`): a walk at a speed keeps the camera at its height over the ground (the solid top
  nearest its feet: rises are climbed, a roof overhead is not the ground), writes where it ended and takes
  `walk-end.png` there; a run ends by itself when the tracer's log says the GPU stopped, and waits for a reset game to
  close before the owner's configs come back; `tools\harness\Queue-PtRun.ps1` queues a run behind a busy card
  (it holds the lock while it waits, and `-GpuCheck` now says no to a new offline test meanwhile; a trial build goes
  into the game folder only once the card is free and main's comes back after). `Build-PtShaders.ps1` starts its
  compilers below normal priority.
- Worker copies and Windows' Storage Sense (2026-10-12 03:28): with the disk 98 % full Windows deleted every file
  under the temp folder not written for about six days: every copy and base lost 138 files, its SDK links and its
  runtime DLLs. `New-TreeCopies.ps1` now gives a copy's old files a write time of a day ago;
  `tools\agents\repair_copies.py` puts back what a sweep took (`docs/AGENT-WORKERS.md`).
- PT-109 worker megageo-6: cluster BLAS batches now size explicit destinations against their declared size class,
  validate bounds before submission and check completed driver sizes. Stable plant groups reuse their GPU build
  arguments; immutable allocation sizes replace repeated per-frame driver queries made only for logs. Added native
  CPU phase timings and a focused grow/shrink/replacement regression case. The forest TDR still needs the game run;
  no game-speed claim. See `docs/agents/megageo-6.md`.
- PT-110 M3 (2026-10-11, the rule): a character's solid submeshes keep their OPAQUE marks when the character also
  has a glass submesh. One glass part used to take the marks from the whole character: 109,800 triangles of a
  tavern's frame went to the shader's alpha test for nothing. A solved caustic's query asks for candidates itself
  and still skips its whole source instance (`caustic_clear`). `docs/agents/omm-chars.md`.
- PT-110 M4, opacity micromaps for the characters themselves: tried and TAKEN OUT the same day (worker ommout,
  `docs/agents/ommout.md`). The hidden `Character micromaps (trial)` (frame flag bit 20, free again) gave the
  tavern's 421,395 character cut-out triangles maps (15 % bare, 32 % covered, 52 % unknown). Two runs, the game in
  front and alone on the card, off / on / off: the tavern's tracer 36.20 / 36.25 / 36.20 ms (`20261011-172725`);
  the town's frame 87.69 / 87.23 / 86.97 ms, its tracer 67.81 / 68.17 / 67.40 (`20261011-185528`). No gain, so by
  the rule for trials it is out: its code in `scene.cpp` and `pt_omm_bake.hlsl` (the bake is the text from before
  it), the setting, the flag, the recipe `micromaps-characters`, its source checks. A skinned mesh is never queued
  for a bake again. The "Micromaps trial" 16 stays: M3's offline check (`TGPT_TEST_OMM_ONLY`) compares against it.
  Why it gave nothing: half of those triangles are hair (212,962), and hair's pieces stayed mostly unknown. The
  camera reads the strands through changing mips with an anisotropic footprint, the other rays read the alpha
  tile, a piece is settled only where every such reading agrees, and covered hair must stay unknown while the
  probe's camera lookup passes hair layers. So the alpha test's cost on hair was never removed.
  A second try needs, in this order: the bound first (the "Micromaps trial" 2, the characters' cut-outs solid for
  a step: recipe `micromaps`, step 6, in a clean `-Measure` run; if the frame is no faster with them solid, no map
  can help); then a proven bound on the camera's footprint over hair, or a map for each reader, or one reader
  for hair's alpha; and the probe's lookup made to agree with it.
- PT-127, primary surface replacement for mirrors: the rule under NRD (2026-10-11, workers `psr` and `psr-2`; no
  setting, no frame flag: bit 22 is free). Where the camera sees a still pure mirror and NRD denoises with the
  tracer's own camera rays, NRD is told of the surface the mirror shows: its depth and motion in the mirror's virtual
  world, its normal mirrored, its roughness and material, its diffuse and specular light apart over its own material
  factors, hit distances that start at it (NRD README's section on it). Why: in the game (run 20261011-213531, a
  tavern's mirror, the world running) the mirror's picture keeps 2.41 of detail under NRD against 0.47 without (no
  denoiser: 3.78), the light the same (luma 15.0 against 15.1). The record is 64 bytes a pixel (127 MB at 1924 x 1084),
  written only at mirror pixels; the reflections' thread carries one word of it across its rays. A glossy metal seen in
  the mirror is replaced too. Not yet: glass and water (a design for the owner), a mirror in a mirror, a mirror that
  moves or is curved. Ray Reconstruction's two guide changes of the trial (a hair pixel's guides from the surface its
  ray stopped at; glass's reflection distance from a ray of its own) are NOT in: the game has not seen them, and the
  first undoes the steady hair guides the tree holds on purpose. The offline check: `TGPT_TEST_PSR_ONLY=1`.
  `docs/agents/psr-2.md` has the numbers, the design and what to run.
- PT-110 M3/M4 worker (2026-10-11, trial awaiting the game): solid character submeshes no longer lose their
  OPAQUE marks merely because the character has glass. The caustic query still skips its full source instance.
  Hidden `Character micromaps (trial)` links per-mesh/material maps through every pose; hair uses conservative
  bounds from both alpha readers, including strand mips. Tile replacement invalidates maps. No speed or pixel
  equivalence claim yet; `docs/agents/omm-chars.md`, recipe `micromaps-characters`.
- PT-127 RR/RRG worker (2026-10-11, untested in game): preserve RGB specular guides, keep reflection distance
  independent of optional transparency, remove illumination from diffuse guides, and use RR's own optimal size.
  Ray Regeneration gains stable radiance units, matching specular factors, history reset after gaps, background
  context creation and hidden AMD validation/debug flags. Preset F unchanged. See `docs/agents/vendor-rr.md`.
- PT-118, worker `fogfroxel`: a hidden camera-fog cache trial in its own passes. The grid stores traced lighting,
  reprojects world points and integrates through the unchanged height/box medium; the rest of each ray and secondary
  rays remain traced. `TGPT_Frame.flags` bit 19, no ABI size change. Default off, recipe `tavern-room@fog-froxel`.
  Part (a)'s tavern run reduces fog noise at near-identical brightness. Part (b)'s first trial separates distant
  ray queries from integration, simplifies constant-density maths, and reuses reservoir light proposals with
  corrected weights and fresh visibility. The clean tavern run exposed repeated box tests during integration;
  the continuation culls box misses once per ray and shares grid reads without reducing slices or samples.
  Guarded small-case integration 9.73 -> 0.55 ms; new game measurement owed. Part (b): 1-6 stability samples,
  adaptive Gaussian filter, bounded radiance plus separately retained bright excess; separate filter timing.
  Directional history and owner acceptance remain open; recipe `fog-froxel-b`.
  PT-87: camera fog targets and the new grid are included in the memory tally. Report: `docs/agents/fog-froxel.md`.

On Nexus as 1.2.3 since 2026-10-09 (the owner: "mark these as shipped"): TG Frame Generation, TG Better Upscaling
and TG Neural Rendering, each for the normal and the Mono version

- What shipped: the six zips of 2026-10-09, 13:40 to 13:49, in `artifacts/Nexus` (their SHA-256 are round 17's;
  copies in `artifacts/Nexus-1.2.3-backup`). The version on Nexus before them was 1.2.0: 1.2.1 and 1.2.2 were built
  and never uploaded. TG Neural Rendering had no release before this one. TG Path Tracing is not released: its
  entries stay unreleased wherever they stand in this file.
- The entries these releases hold: NR-36 to NR-38 and R17 right below, "TG Neural Rendering / shared bridge 1.2.3",
  "TG Neural Rendering 1.2.2", "TG Better Upscaling and the game's update of 2026-10-06" and "Fixes from the first
  troubleshooting reports" (their headings say so), and TG Frame Generation's entry about the cursor in a menu
  under "TG Path Tracing (new, prototype, Mono version)".
- The notes written for TG Frame Generation's page: works together with TG Neural Rendering; a 45 entry in the
  game's Framerate Limit row (90 FPS on screen with 2x); no warning for the NVIDIA driver's forced VSync or Max
  Frame Rate (VSync at 1/2, 1/3 or 1/4 of the refresh rate still warns); no more switching on and off with hitches
  while FoA Mod Manager is open in game; normal version: the alert icons show on the Video rows, and a crash that
  could happen when opening Options is fixed; troubleshooting report: an autosave no longer interrupts the test and
  each step has its latency numbers again; checked against the game's update of 2026-10-06.
- The notes written for TG Better Upscaling's page: fixed for the game's update of 2026-10-06, which added the
  game's own FSR 2 and broke 1.2.0 (XeSS, Quality Mode and FSR Sharpness were gone); the Upscaling row is None,
  STP, DLSS, FSR, XeSS again, this mod's FSR 3.1 / 4 in the place of the game's FSR 2, a saved choice carried over;
  where FSR 3.1 / 4 can't run, the game's FSR 2 stays with its slider; the FSR entry names the version the GPU
  runs; works together with TG Neural Rendering; normal version: the alert icons and the Options crash, as above.
- The notes written for TG Neural Rendering's page: first release; NVIDIA DLSS 5 Neural Rendering as a row in the
  game's Video settings, for both game versions; Placement Quality (after DLSS upscaling) or Performance (before
  it); Strength, and under Advanced Local Tone, Style, Passes, Detail, Automatic Mask and Skin Detail; NVIDIA's
  official `nvngx_dlssnr.dll` or a compatible community one, the loading path chosen by the mod, the DLL not in the
  download; alone or with TG Frame Generation, TG Better Upscaling, ReShade and RenoDX HDR.
- Shipped without having been seen in game as a player sees them: an alert icon itself on the normal version (the
  log says the rows are listed; no problem was active in that run), and the Options crash with TG Frame Generation
  or TG Better Upscaling alone (seen and fixed with TG Neural Rendering installed; the fix is in the bridge all
  three carry).

- NR-38 (2026-10-09): move Local Tone under Advanced in Video and FoA Mod Manager. Saved values and defaults
  stay the same. README and Nexus draft match the new order.
- NR-37 (2026-10-09): retain the normal-version expandable rows' native child arrays for their owner's
  lifetime, including after changing a selector. The Options diagnostic reproduced recycled memory being
  read as a row. Built/deployed;20 actual gameplay pause/options/apply/resume cycles pass with active NR,
  forced GC and no errors. Owner says it works great.

- NR-36 (2026-10-09): fix normal-version startup crash in the rendering hook. BepInEx's generated hook
  misread HDRP's unboxed PrepassOutput as an object. The NR-only IL2CPP adapter forwards it unchanged and
  preserves camera-scoped scheduling; Mono's hook and shared bridge remain unchanged. Official runtime
  selected for this test. Startup and4800 NR evaluations observed; visual verdict and FG/BU coexistence pending.
  Missing-model smoke passes with a clear unavailable reason and a working original swapchain. Harness gains
  -LaunchOnly for GPU-guarded startup checks on either runtime, without installing the Mono capture runner.

- TG Neural Rendering, R17 (2026-10-09): automatically selects an isolated compatibility adapter for community
  runtimes. Signed NVIDIA DLLs retain the normal SDK route. No community hash/version allowlist: matching direct
  interface and runtime support decide compatibility. The helper contains the caller-name exception, uses the same bridge device
  and reads hardware support from the runtime. No new setting or bundled model. Real rendering, neutral SDR/HDR,
  Off/resize and shared Ray Reconstruction checks pass on RTX 5080. Older RTX cards still need hardware testing.
  See `docs/DLSS5-COMPATIBILITY.md`. Later NR-35/36 deployments were explicitly requested for IL2CPP validation.

- TG Path Tracing, 2026-10-12 ~01:00 (PT-110, PT-118, PT-127, PT-130, PT-131, PT-132, PT-133; deployed 00:45, native
  3199B2F4BBF6, plugin A2311F0DA2C5; seen in the game by the harness: `20261012-004519`, `-005626`). (1) MIRRORS BY PRIMARY
  SURFACE REPLACEMENT ARE THE RULE UNDER NRD (workers psr and psr-2, NRD's documented way): a pixel that shows a pure
  mirror is handed to NRD as the surface the mirror shows (depth, normal, motion, roughness, hit distances of that
  surface; 64 bytes a pixel of record). The tavern's mirror under NRD: detail 2.53 against 0.47. No setting. Glass and
  water are not covered: a design for the owner (`docs/agents/psr-2.md`). (2) THE FOG GRID'S WHITE TAVERN WAS A FRAME WITH
  NO SUN: the cells' Shader Model 6.9 pass returned garbage for a sum that is zero on the branch that skips the sun
  (PT-133; worker fogfix); cured by the pass's shape, with four no-sun cases in its check; and a lamp's light near the
  lamp is split (the smooth part in the cells, the rest per pixel). In the tavern the grid has the old route's light
  and half its flicker and boil under NRD, for 0.7 ms more. Still behind its hidden switch; a number read-back (TEMP
  DIAG) is in the tree until it becomes the rule. (3) THE CHARACTERS' MICROMAPS TRIAL IS OUT (worker ommout; no gain in
  two clean runs); its fix for solid parts beside glass stays. (4) `water_medium` counts water overhead only with no
  ground between (a second ray against ground, rock and buildings, MASK_GROUND): built on a theory of PT-131 the game
  did not confirm; it costs nothing measurable in the tavern (tracer 36.45 ms before and after) and the water scene's
  light is the same. (5) THE HARNESS: a running step keeps the world frozen until the passes are made (the game's clock
  had run from the afternoon into the night while a new build's passes compiled); Sunshine alone no longer ends a run;
  NO OFFLINE GPU TEST WHILE A GAME OF THE OWNER'S IS OPEN (`-GpuCheck` exits 1: a worker's test hung the card beside
  Starfield at 00:20 and the driver's reset took the game down). (6) MEASURED: a camp in the forest at 3 frames a
  second is the glass's or the water's own path with the Caustics row on (31 ms without those paths, 346 with: PT-131;
  worker campspeed); RTX Mega Geometry's NVIDIA layout is 6 % slower than plain in the town's walk.
- TG Path Tracing, 2026-10-11 ~22:00 (PT-109, PT-110, PT-114, PT-118, PT-127, PT-130, PT-131; deployed 21:52, native
  DF0395575260, plugin 3401A307FFF1; looked at in the game: `20261011-215257`, the tavern and a water scene). (1) NRD's
  HISTORY CONFIDENCE IS THE RULE whenever NRD runs (NVIDIA's README: "optional, but highly recommended"): the hidden
  `NRD lighting confidence` and frame flag bit 21 are gone. The tavern: the same picture, denoising 2.83 -> 3.29 ms; the
  world frozen, no boil of its own (0.23 against 0.24, `-214719`); the candles running, NRD follows them sooner (boil 0.29
  against 0.25, `-190338`). (2) THE CAUSTICS ARE BUILT BEFORE THE PASSES THAT READ THEM (PT-130): the bounced light's and
  the reflections' paths read the caustics at solid points under water and ran before them, so a ray saw the frame
  before's; the stage is now the third (`kStageNames`: scene, sky, caustics, prepare, ...). (3) RTX Mega Geometry: the
  worker megageo-6's fix merged (the forest's GPU hang: a cluster operation's room for its results sized per object where
  NVAPI asks for the explicit destinations' bounds; arguments kept between frames; the driver's size questions once).
  The forest walk ran four times without a hang (`-212136`); NVIDIA's layout is still no faster than plain. (4) THE
  DESCRIPTOR HEAP KEEPS EVERY SLOT'S STATE (`gpu.hpp`): a slot given back twice is refused and a view written into a slot
  nobody holds is noted, each with the line that did it, in the log ("DESCRIPTOR SLOT MISUSE"); none seen so far. Built
  while hunting (5). (5) NOT FIXED, FOUND: the fog grid with its continuation is white in the tavern: it over-counts a
  lamp's light near the lamp (offline, a lamp 2 m / 0.3 m / 0.02 m before the camera: 1.5x / 2.1x / 2.4x the old route's
  light in the sky strip; new test switches `TGPT_TEST_FOG_LAMP`, `_CACHE`, `_SCALE`, `_DIAG`). A temporary hidden
  `Fog grid diagnosis` (frame flags bits 24 to 27) is in the tree until the worker fogfix removes it. (6) MEASURED AND
  DROPPED: the characters' micromaps (no gain in the town or the tavern: coming out, worker ommout) and caustcut's exact
  cut of the caustics' shadow rays (the stage 4.20 / 4.30 / 4.05 ms off, on, off in the tavern: never merged). (7) SEEN
  IN THE GAME, NOT MERGED YET: primary surface replacement for mirrors (NRD's detail in the mirror 0.47 -> 2.41,
  `-213531`; worker psr-2). (8) FOUND: a camp in the forest where the reflections take 213 to 235 ms a frame (PT-131).
- TG Path Tracing, 2026-10-11 ~16:10 (PT-117, PT-118, PT-127; deployed 16:10, native B5F7F8C4F141, plugin 5B3E2CBC568C):
  (1) The bounced light's spatial pass no longer traces the visibility ray of a pixel the scattering walk takes over
  (pt_walk replaces its light: review-perf-astra 8). (2) The micromap baker counts a texel only where the texels a hit
  can read meet the micro-triangle itself, not its box (review-perf-astra 4, NVIDIA's OMM guide): fewer unknown
  micro-triangles, the same states where both agree (`TGPT_TEST_MICROMAPS_ONLY` passes: every pixel as the alpha test alone
  makes it, largest difference 0.00000; in the tavern the share settled 16.5 -> 16.6 %: its cut-outs are characters'). (3) Merged: the
  Codex worker vendor-rr's Ray Reconstruction and Ray Regeneration fixes (PT-127; TG Better Upscaling's part not yet
  deployed) and fog-froxel's part (a): the fog as NVIDIA's RTX Remix builds it, a froxel cache, behind the hidden
  `Traced fog: froxel cache` (frame flag bit 19, off). Run 20261011-161034 (rough times): with NRD the fog's flicker 0.38
  -> 0.21, boil 0.23 -> 0.12, the brightness the same; the composite stage 3.9 -> ~12 ms: part (b) works on both.
  (4) Built after, not deployed: with NRD the reflections' own filter (pt_atrous's four passes) filters glass and water
  alone, the rest passing through (review-vendor-astra 10: NRD's result replaces them); the denoising stage was ~4.4 ms.

- TG Path Tracing, 2026-10-11 ~15:40 (PT-127; built, not yet seen in the game): NRD is only as NVIDIA's README sets it up
  (the owner, watching run 20261011-151710: "NRD is a million times better now"). The old route is gone: RELAX's
  directional form on the bounced light alone, the normal map's share (bump_share), the triangle's or the character's
  smooth normal as the guide (guide_plane, SMOOTH_PLANE in the hair texture), the direction code in the bounced light's
  alpha, the tracer's own direct-light filter under NRD. NRD takes the diffuse light (the direct and the bounced light and
  the caustics, traced before the denoisers now) times the diffuse color over NRD_MaterialFactors' diffuse factor, the
  specular light over its specular factor, the shading normal (hair as NRD's strands, material ID 1), 2.5D motion (the
  camera rays keep each point's view depth change, a moving thing's too: the own-motion texture is RGBA16F), the
  bounce's hit distance as the README asks for ReSTIR (the sample's kept by the diffuse lobe's odds, else the pixel's own
  cosine ray's, which pt_gi now leaves in its alpha), what a channel cannot hold handed back as traced (RELAX: 250, REBLUR:
  a half float's), its history in seconds (RELAX's own maximum). RELAX by default; REBLUR on the hidden `NRD trial` = 2.
  AMD Ray Regeneration's motion takes the same depth change. And the traced light's automatic gain is metered on 4 x 4
  blocks' means (pt_meter): metered pixel by pixel, noise put the median low and the undenoised picture got more gain
  (the tavern x2.7 against x2.2 with NRD; after: x2.2 both, run 20261011-151710).

- TG Path Tracing, 2026-10-11 ~15:00 (PT-127, PT-112, PT-109; built and deployed 15:01, native 813BC4C511E0, plugin
  28B216AD6B01; not yet seen in the game). (1) NRD as NVIDIA's README sets it up, ON TRIAL behind the hidden `NRD trial`
  (frame flags bits 17-18; the owner: "make sure NRD is set up to perfection", then "Obviously we want to 100% follow
  NVIDIA's and AMD's implementation guidelines"): 1 RELAX and 2 REBLUR, DIFFUSE_SPECULAR, take ALL the light a surface
  gives (the direct and the bounced light together, divided by the diffuse color; the reflections and the lights'
  highlights divided by NRD_MaterialFactors' specular factor), with the bounce's and the reflection's real hit
  distances (REBLUR's normalized), the shading normal as the guide, no diffuse pre-pass, the firefly suppression on, and
  the history set in seconds at the real frame rate (NRD's half a second: 5 frames at 10 a second). Glass and water keep
  the reflections' own filter. 0 is the route since round 2 (RELAX on the bounced light alone; the direct light and the
  reflections through the tracer's own filter, which the tavern run of 14:12 showed 16 % dark and blurred: wood grain,
  small shadows). pt_nrd_prep modes 3 to 6, nrd_denoiser.cpp (nrd_use makes the instance per kind), tracer.cpp,
  pt_compose (pass.a.x's 2). The hidden "NRD keeps detail (trial)" is gone (it changed nothing, run 20261011-141211);
  its option bit 8388608 is free. Recipe `nrd-book`; `nrd-dark` starts with the game's picture. (2) The sky of sky-2 and
  sky-3 merged and built (the hidden `Sky trial`, 0 by default; TGPT_SetSkyModel, FrameConstants 9008 bytes). (3) The
  traced fog's distance for an optical depth in closed form (review-perf-astra 5), in place of 22 halvings. (4) RTX Mega
  Geometry's plants as clusters, measured offline (`docs/agents/megageo-2.md`, `tests/megageo_cluster_bench.cpp`):
  rays through clusters 40 to 130 % slower here against NVIDIA's 5 %: not taken; a fresh worker compares our setup with
  NVIDIA's own (brief-megageo-3.md).

- TG Path Tracing, 2026-10-11 afternoon (PT-117, PT-118, PT-124, PT-125; the owner's session of ~11:10 and GPT-6-astra's
  review, `docs/agents/review-perf-astra.md`). (1) The GPU hung in the scene stage while the owner walked into new ground
  (TDR 0x887A0006, the game crashed): a mesh whose micromap bake passes one frame's pieces goes a level coarser (one bake
  is one dispatch; the first mesh of a frame went whatever its size), and no frame bakes more because frames are slow;
  progress markers now name the scene stage's step a hang stopped in. (2) The memory cuts that waited since round 19 are
  on: the water's blocks exact and its idle pages given back (the beach 160 MB of pages for 137 in use, where the owner's
  session held 848 for 196), the plants' structures compacted. (3) The reflections fetch the hit their ray met AFTER the
  regrouping (Shader Execution Reordering), as the bounced light's first ray does: the query's answer crosses the call,
  not the whole hit (`pt_query.hlsli`, moved out of pt_gi). (4) The fog's history follows where this frame's light comes
  from along the way, a point still in the world, not the surface behind (the lamps' glow trailed behind them as the camera
  moved). (5) The reflections' hint probe backs off while one choice keeps winning (128 frames, then 256, 512, 1024). Then
  (in the next build): water-only rays without micromaps (they can meet none), light candidates of weight 0 not loaded or
  evaluated (their count kept for RTXDI), the water grid's stages run for every slot at once with one barrier a stage.
  Run `20261011-133953` (another program at 4.8 %: rough): the forest 11.5 fps (the tracer 67 ms; ~74 last night), the
  beach 7.9 fps (108 ms).
- TG Path Tracing, 2026-10-11 ~11:00 (PT-112 B, PT-37, PT-60; built, NOT yet seen in the game): (1) The lamps' and
  glowing surfaces' light in the fog is drawn where it is: per pixel one lamp chosen by the light it can send into the
  whole view ray (its light at the ray's nearest point, times the angle the ray spans from it), and one point of the ray
  drawn in proportion to 1/r^2 from it (equiangular sampling, Kulla and Fajardo 2012), its traced shadow ray and the fog
  boxes on its way; the sun, the sky and directional lights stay at the fog's own scattering point (`pt_medium.hlsli`
  `fog_lamps_along`). Exact on average; meant for the torch's grainy mist at night (flicker 2.9, run `20261011-102244`).
  Mirrors and glass take it too, paid for with the chance the medium has to scatter at all. (2) HDRP's fog under the
  traced picture is gone, as the owner's rule asks of workarounds ("remove all the 'hacks' and workarounds as we go"):
  the Fog Adaptation row (`TGPT_OPTION_FOG_ADAPTS`, render event 5, `TGPT_SetFog`, d3d11.hlsl `TGPT_FOG`, its histogram
  and gain, `HdrpPasses.FogPass`), the hidden `HDRP fog in place of the traced fog`, HDRP's formula over a reflected way
  (`fogged_after`, `fog_color`, `fog_sky`) and the traced light's gain fading behind HDRP's fog (`traced_gain_through`,
  `traced_gain_at`, `fog_gain`); a debug view shows no fog. The harness recipes `traced-fog`, `mirror-fog-traced` and
  `why-bright` lost their HDRP-fog steps (the game's own picture is the reference); the GPU test lost Fog Adaptation's
  checks and the mirror-in-HDRP-fog check (a check of the traced medium in a mirror is owed). The fill lights' fog part
  (the Fill Light Fog row) lit only HDRP's fog: inert until fog-only lights are traced (open).
- TG Path Tracing, 2026-10-11 ~10:20 (PT-112; the owner: "ambient occlusion, contact shadows, SSR, shadow maps, deferred
  lighting, subsurface scattering - NONE of these are relevant while Path Tracing is on. These must not render at all";
  built, not yet seen in the game): while the tracer draws the picture, HDRP renders none of its own lighting for the
  gameplay camera. Left out of its frame settings with the decals: ambient occlusion, contact shadows, screen-space
  reflections (opaque and transparent), indirect diffuse, screen-space shadows, subsurface scattering, transmission,
  shadow maps and the volumetric fog grid; its deferred lighting and its camera motion vectors are skipped (Harmony
  prefixes). They lit or fed only HDRP's own picture, which the traced one replaces; they should have gone with step A.
  What HDRP still draws over the traced picture (its transparent effects) takes HDRP's plain fog and no shadow-map
  shadows until it is traced. The fog's albedo follows the game's own volumetric setting (`GamePasses.GameVolumetrics`).
  For one measuring run: the hidden `HDRP transparents not drawn (trial)` (does the game still simulate its effects
  when HDRP draws none of them; recipe `particles-no-transparents`). PT-116 found: at most 4 water surfaces traced.
- TG Path Tracing, 2026-10-11 morning (PT-112 step B, PT-37; `docs/rounds/round-20.md` 11; built, the mirror seen in the
  harness, the fog boxes NOT yet seen in the game): the game's fog is the tracer's, the rule. (1) The hidden `Traced fog
  (trial)` and its walk are gone; HDRP's fog comes back only behind the hidden `HDRP fog in place of the traced fog`
  (off; for comparing), and `Traced fog: light scattered once only` (off) is for tests. (2) The light the mist scatters
  more than once comes from the mist's own cells in the radiance cache (a seventh kind of cell, four times a surface's
  size), fed by a quarter of the screen's blocks a frame by one free flight from the fog point: the Broc Meala mist 91
  (HDRP 69, the first order alone 61), smooth, flicker 0.30 (run `20261011-091933`); the night's torch glow and shadow in
  the mist, its moonlit blue haze gone (the owner to judge). (3) Reflected and refracted rays cross the traced medium
  (`pt_medium.hlsli` `traced_fog_on_way`: the transmittance over the way and one scattering point with the light it
  gets, by the chance the fog has to scatter at all), where they took HDRP's formula, the sky's colour times a tint
  (FrameConstants 8080 -> 8096: `fogMedium`): the mirror in the night mist shows a faint warm haze and dims what it
  shows (run `20261011-094755`). (4) HDRP's screen-space "multiple scattering" blur of the picture by the fog's opacity
  (`ScreenSpaceFogMultipleScattering`) is skipped while the fog is traced. (5) The game's local fog boxes (HDRP's Local
  Volumetric Fog: mist patches, a blizzard's) are part of the medium: the plugin sends every enabled one each frame
  (`FogBoxes.cs`, `TGPT_SetFogBoxes`, `TGPT_FogBox` 144 bytes; FrameConstants 8096 -> 8112: `fogBoxes`), their 3D noise
  masks opened with no copy (texture sharing now hooks `CreateTexture3D` and `CreateTexture3D1` and its start-up
  self-test proves nine 3D cases apart from the 2D ones), their density as HDRP voxelizes them (the default Fog Volume
  graph: the mask scrolled through the box, the faces' and the camera distance's fades, added to the height fog), with no
  distance cut (HDRP leaves out the far ones); rays cross them by ratio tracking (the light through) and delta tracking
  (where they scatter): exact on average, no step size. Not traced, named once in the log: a box with a fog material of its
  own (rain's), a blend other than Additive. Not yet: the sky's haze on far land (HDRP's fog pass added it outdoors and
  is skipped: `docs/agents/sky-1.md`), the boxes' shadow on surfaces, the light surfaces send into the fog.
- TG Path Tracing, 2026-10-11 (PT-110; `docs/rounds/round-20.md` 9; the harness, the world running, the frozen pictures
  the same): the micromaps of leaves and grass are baked at level 6 (4,096 pieces a triangle; the rest stay at 4). At
  level 4 59 % of the plants' pieces were left to the shader's alpha test, which cost the forest 19 ms of its 83 (run
  `20261011-053411`); at 6 35 %. Run `20261011-055446`: the forest 82.8 -> 75.5 ms of the tracer, the town 64.5 -> 62.2;
  the micromaps 32 -> 163 MB (forest) and 44 -> 200 (town). A mesh baked at another level is baked again; the scene's
  budget of micromap bytes is 1,024 MB (was 256); the hidden `Speed trial`'s 128 and 256 bake them at 4 or 5 again, for
  measuring. The log's piece shares are now shares of a triangle's area, whatever the level.
- TG Path Tracing, 2026-10-11 (PT-99, PT-110; `docs/rounds/round-20.md` 9 and 10; the harness, the world running, the
  frozen pictures the same): three speed changes that leave the picture as it is. (1) The bounced light's pass regroups
  its threads by the material its first ray met and whether the pixel feeds the radiance cache (the reflections' key,
  16 bits), where it had regrouped them with no key at all since 10-10: the tavern 32.2 -> 30.6 ms of the tracer, the
  forest 82.3 -> 78.1 (run `20261011-060805`); a key had cost 20 ms on 10-10 only because the pass then carried 2,560
  bytes a thread across the call (it carries 0). (2) The decals' lookup tree is split by place (the median of the boxes'
  centres), each node holding its least and most draw-order index, and a hit takes its decals in draw order one at a
  time: the same decals in the same order; the tree split by draw order had boxes spanning the whole scene. (3) A point
  over the scene's highest water asks no "which water am I in" question (a ray straight up after every shadow ray
  nothing stopped): round 19's trial, which never switched on because a river 30 m over the beach's sea lay in a slot
  the plugin gives no top; the shaders now get every slot's highest water (`waterCeiling`, FrameConstants 8064 -> 8080),
  each live slot's top is at least its own highest water, and the skip is the rule (no measurable change of speed,
  fewer rays). All of tonight together (run `20261011-064558`): the tracer in the tavern ~32 -> 30 ms, the town ~64 ->
  57, the forest ~83 -> 74.
- TG Path Tracing, 2026-10-11 (PT-110; `docs/rounds/round-20.md` 8; the harness, the town with the world running, the
  picture the same): leaves and grass no longer cost a shader's alpha test on every ray that crosses them. Their covered
  micromap pieces were left "unknown" so that a light's shadow ray could pass a leaf by chance where it covers; now they
  are opaque, every still instance holding leaves or grass carries the mask bit 0x40, and a light's ray meets those
  instances in a pass of its own with every triangle forced to the test (the same chance at each crossing). Thin cloth
  keeps its unknown pieces (a bounce ray passes its weave too). The town's reflections 22.7 -> 18.2 to 18.8 ms, the
  tracer 64.6 -> 61.3 to 63.7. And the plants in the wind's pose trace with their meshes' micromaps by default (M1, a
  trial since 10-10: the tracer -0.6 ms in the town, the forest 121 -> 115 frozen; the `Micromaps trial`'s 32 now leaves
  them out, for measuring).
- TG Path Tracing, 2026-10-11 (PT-112 step B, PT-37, ON TRIAL behind the hidden `Traced fog (trial)`, off;
  `docs/agents/fog-2.md`): the game's height fog traced along each camera ray by a new pass (`pt_fog`): its thickness
  exact from the Fog volume's own numbers, its albedo and anisotropy (ABI 0x00012700: `TGPT_Frame.fogMedium`), lit by
  the sun, the sky and one lamp or glowing surface with traced shadows and HDRP's phase function, reused over frames and
  filtered; the composite lays it over every pixel and HDRP's opaque fog pass is skipped. Single scattering by default
  (the day's mist 61 against HDRP's 69, a torch's glow and shadows in the night mist); the light scattered more than once
  behind a second hidden switch (brighter, too grainy for now). Not yet: local fog volumes, the fog on reflections.
- TG Path Tracing, 2026-10-11 (PT-112 step A; `docs/rounds/round-20.md`; seen in the harness's frozen tavern and town,
  NOT yet looked at by the owner in play): HDRP draws no opaque object and no decal under the traced picture while the
  traced light is on screen; the tracer gives the frame its depth, motion vectors and depth pyramid. Why: the owner,
  2026-10-10: "Let's prevent HDRP rendering, yes", and round 19 measured HDRP's opaque drawing at 6 to 9 ms of the
  town's frame. Its opaque draw lists are left empty (never the frame setting OpaqueObjects: it woke the game's ECS
  uploads, 800 ms frames); HDRP draws as before only for the Yardstick view, the old route and the hidden `HDRP draws
  under the traced picture` (tests). The trial switch `Game passes while tracing` of round 19 is gone. The traced decals
  follow the game's own decal setting, not HDRP's. HDRP no longer draws hair's depth while tracing (`HoldLayerDepth`).
  Two faults found on the way, both in the motion the tracer now hands DLSS: (1) a plant group's mesh record left
  `previous` at 0, not "none", so the camera rays read heap slot 0 as the plants' last positions (round 19's foliage
  differences); plants and swaying cloth now carry their own sway motion (the plants as each vertex's move in three
  halves, 76 MB in the town). (2) A still character "moved" a few hundredths of a pixel: its positions are the world's,
  kilometres from the origin, and its previous point was placed anew against the ray's hit; each corner's move is now a
  difference of two nearby floats, exactly nothing for what stands still. Town, frozen, no denoiser: flicker 0.22 with
  HDRP drawing and 0.22 with the rule (0.40 before the fixes, 0.30 with the first); tavern 0.40 and 0.41.

- TG Path Tracing, 2026-10-10 (PT-99, PT-110; worker `rule2`, `docs/agents/rule-2.md`; the arrangement was seen in the
  game, the rule as built here compiles and is NOT yet seen in the game): the bounced light's pass is split in two and
  regroups its threads once, after the pixel's first ray, outside the loop over its rays, in every build and with no
  switch. Why: Shader Execution Reordering stays on (the owner: "Do not turn that off. If it makes it slower with that
  on, then it's either not due to that, or due to us not designing our tech with it in mind"), and what a thread
  carries across the regrouping is its cost: the call inside the rays' loop kept 2,592 bytes a thread by the driver's
  own count, this place 0. The game, run `20261010-130621`, the same picture in every step, no denoiser (ms: a frame /
  the bounced light's stage / the reflections'):

  | Step ("Speed trial") | the tavern | the town (`metal`) | the beach |
  |---|---|---|---|
  | as it shipped (0) | 65.0 / 41.9 / 9.6 | 101.2 / 57.7 / 24.7 | 169.2 / 61.5 / 92.9 |
  | the plain compute passes (8) | 39.9 / 13.7 / 12.7 | 74.4 / 27.6 / 27.7 | 274.4 / 64.6 / 195.2 |
  | split, regrouped at the top (32) | 33.4 / 9.6 / 10.4 | 75.5 / 22.9 / 33.7 | 145.0 / 29.5 / 100.9 |
  | **split, regrouped after the first ray (48): the rule** | **33.4 / 9.5** / 10.4 | **75.3 / 22.7** / 33.8 | **144.7 / 28.9** / 101.1 |
  | split, only water and glass regrouped (64) | 36.9 / 13.1 / 10.3 | 83.9 / 32.3 / 32.8 | 146.1 / 31.8 / 99.6 |

  What changed: `pt_gi.hlsl` is one function for the reordering pass and the compute passes (Shader Model 6.6 and
  6.9), its one call under `PT_SER` alone; ReSTIR GI's reuse across frames is `pt_gi_reuse.hlsl`, a compute pass the
  tracer runs right after it in every frame, inside the bounced light's stage (the frame's own sample travels whole
  between the two: `pt_gi_own.hlsli`). The reflections' pass is as it shipped: in steps 32 to 64 it ran with the
  trial's 3-bit key and its guide stored once, and was slower (the town 33.8 against 24.7 ms, the beach 101 against
  93). Out: CMake's two options of the reordering study and the split trial, `pt_ser_split.hlsli`,
  `pt_ser_study.hlsli`, the "Speed trial" numbers 16 to 80 as their arrangements (they do nothing now), the recipes
  `ser-split`, `ser-study` and `ser-study-keys`. Stays in every build: the start-up line with the driver's stack of the
  two reordering passes.
- TG Path Tracing, 2026-10-10 (PT-99; worker `rule1`, `docs/agents/rule-1.md`; the trial was seen in the game, the
  rule as built here compiles and is NOT yet seen in the game): the lamps a ray off water or see-through glass runs
  into are found by a search tree, in every build and with no switch. Why: such a ray asked every lamp of the scene
  (510 to 976 of them) whether it runs into its ball, at every turn of every water pixel's path and at every face of
  glass. The game, run `20261010-092027`, the trial on and off in one build: the beach's frame 192 -> 161 ms (its
  reflections' pass 134 -> 103), the same picture. What changed: `reflected_lamps` is the walk through the tree
  (`pt_common.hlsli`; `native/pathtracing/src/lamp_tree.hpp` builds the tree each frame, `scene.cpp` lays it after
  the frame's lights); the old loop, CMake's `TGPT_LAMP_TREE` and the hidden "Speed trial" 16 are gone; the tracer's
  10 s line names the tree's nodes beside the lights; ctest's `tgpt_lamp_tree` (the walk against the old loop, on the
  CPU) is built in every tree. Not changed: the tree still lies in the lights' own buffer, and a far lamp's mirror
  image is still decided by rounding, as the loop's was (`docs/agents/perf-1.md`, perf2.2).
- TG Path Tracing, 2026-10-10 (PT-99; the same worker and report; compiles, NOT yet seen in the game as the rule):
  the two big passes regroup their threads also by whether a pixel feeds the radiance cache, always. Why: a quarter
  of the screen's 8 x 8 blocks feed the cache each frame; grouped by material alone, every group ran the cache's
  ray for its few feeders. The game, the same run: the tavern's reflections' pass 11.4 -> 9.9 ms, the same picture,
  no regrouping added. What changed: `regroup_key` (`pt_common.hlsli`) carries the feed bit in every build; CMake's
  `TGPT_REGROUP_FEED` and "Speed trial" 32768 are gone (in a measuring build 32768 is its second page, as before).
  `TGPT_REGROUP_WATER` ("Speed trial" 2 and 4) is as it was: on trial, not run yet.
- TG Path Tracing, 2026-10-10 (PT-99; the same worker and report): the water's short rays are taken out. A ray
  inside the water asked only as far as its path flies before it scatters (CMake's `TGPT_WATER_FLIGHT`, "Speed
  trial" 32) gave no gain in the game that could be told from the noise (the same run: the beach's frame 192.7 ms
  with it, 193.7 and 190.9 without). `water_flight_reach`, its define and its option are deleted; the turn's ray
  asks for what is nearest however far, as before the trial. The recipe `speed2` is deleted with the three trials;
  `regroup` keeps the water's two steps.
- TG Path Tracing, 2026-10-10 (PT-104; the same worker and report; `docs/agents/terrain-1.md`; compiles, NOT yet
  seen in the game as the rule): the ground has its relief. The terrain's program turns its layers' bumps by the
  object's x axis with the sign +1, always, as the game's shader does for a mesh without tangents. Why: the game's
  ground meshes bring no texture coordinates, the tracer made a normal map's frame of those, had none and answered
  the flat normal: every bump of the ground's eight layers was thrown away, for every ray. The game, run
  `20261010-092027`: another normal than the game's on 63 % of the terrain's pixels flat, on 4 % with +1, on 54 %
  with -1 (the whole screen: 18.7 % -> 2.4 %). What changed: `program_terrain` (`pt_materials.hlsli`) always makes
  that frame; the hidden `Advanced / Terrain frame`, its word in the terrain's record and its log line are gone;
  the recipe `terrain-frame` shows the rule (the picture, the normals, the yardstick, the rain). STILL WRONG, and
  said in the log: (a) the 37 ground meshes that bring tangents of their own hold (1, 0, 0, -1), by the game's rule
  the other sign, and the game draws them (10 of the 14 terrain materials round the test scene were on such a
  mesh): their bumps are turned with +1 too, the sign by the mesh is not built; (b) every other material with a
  normal map on a mesh without texture coordinates is still drawn flat: the plugin now writes one line, "N
  instances of M materials have a normal map and a mesh without texture coordinates: drawn flat", with their names.

- TG Path Tracing, 2026-10-09 late afternoon (PT-98; worker `arrival1`, `docs/agents/arrival-1.md`; compiles, NOT
  yet seen in the game): the game writes its own timeline of the traced world's arrival, and every limit on that
  path that was counted by the frame or by the scan goes by the clock. Why: the owner saw "stuff pop in over time"
  and "it took a LONG time before it kicked in"; run `20261009-150909` took a town's pictures with nothing traced
  on 62 % of the screen while the harness believed the scene was settled.
  - `Arrival.cs` (new): a log line a second from the moment tracing captures until the world is whole
    (`Arrival 3.0 s | instances ... | meshes ... | textures ... | scans ... | N ms a frame | to come N`), then
    `Arrival whole after N s`, with the queue that emptied last; then silence.
  - Meshes are registered for a tenth of the frame's time as before, and that share is now counted AFTER the
    frame's first mesh: the first call into Unity waits for the render thread, which at 8 frames a second eats the
    whole share, so the rule of 13:50 is still 6 meshes a frame once frames are slow (read from the code; the
    timeline's line shows it: "+N in X ms, Y ms of it the frame's first call"). Cost: a frame is up to a tenth
    longer while meshes wait, about 1,000 meshes a second at any frame rate.
  - A scan comes 0.25 s after the last (and no sooner than four times what it took) while registered meshes wait
    to be placed. It was every 2 s whatever was waiting: a mesh was built and still not on screen for up to 2 s.
  - The textures are looked at four times a second, not once a scan (a swap to a sharp texture was seen up to 2 s
    late, its material read from 64 x 64 tiles meanwhile).
  - Native: every "a frame" budget is multiplied by the frames of 60 a second the last frame's time holds (1 to
    8): tile bakes (24, 8, 8, 4, 8, 8 a frame), BLAS builds (48), mesh read-backs (64 MB), micromap bakes, plant
    groups. Budgets that are memory in flight stop at 4 times. A mesh's read-back is mapped 33 ms after its copy
    (it was two frames: a quarter second at 8 frames a second). The same work a second at any frame rate, in
    fewer and larger pieces. Nothing is left out or made coarser.
  - `TGPT_Status` grew at its end, 216 to 232 bytes, `TGPT_ABI` as it was: tiles to bake, listed instances whose
    mesh is not built, plants still to group. Either side works with the other's older build.
  - The notice says all of it: "building the traced world: 412 meshes and 96 textures to go" (it counted meshes
    only), gone when the world is whole. The harness takes a traced step's pictures when the plugin says the world
    is whole (it waited for the native queue of meshes alone) and writes how long that took into the step's result.
  - For measuring: the hidden `Advanced / Arrival as before` (0; 1 puts the plugin's three rules back, a change
    starts the traced scene over) and the recipe `arrival`, which shows one scene's world arrive both ways in one
    run. The switch goes when the game has answered.

- TG Path Tracing, 2026-10-09 afternoon (PT-98, PT-97, PT-96, PT-40): the traced world arrives BY THE CLOCK, not by
  the frame: meshes are registered until a tenth of the last frame's time is spent (at least 6; `SceneCapture`
  `MeshShare`), and every watched texture is looked at each scan (4096, was 256). Why: at the 8 frames a second
  the tracer ran at, 6 a frame were 45 meshes a second and a town square's 1,533 took over half a minute; a guard
  stood in 64 x 64 stand-ins up to 13 s after the game had his sharp textures. A notice "building the traced
  world (N meshes to go)" shows while that lasts more than 2 s. The harness says on screen when a step's picture
  does not count yet and saves the game's own screen during a wait for the passes. `tools/Build-PtShaders.ps1`
  takes from the free memory what running compiles will still take (`-PeakGb` 8): twelve compiles that each
  passed the test at their start committed 100 GB on a 62 GB PC. The wax trial `Advanced / Subsurface reading`
  (a diffusion profile read as the game's shader reads it) was merged, run once and TAKEN OUT: brown wax at 0.55
  times the brightness where today's wax already has the game's (run `20261009-142733`), 47 ms a frame, and 223 s
  for the driver to make its pass's pipeline. On trial, off by default, each run once in the game (PT-13, PT-81,
  PT-93, PT-95 have the numbers): `Advanced / Glass trial` (7: jars see-through as the game's, the view through
  them warped: not ready), `Water light without view` (right: the sea bed 28 to 51 levels, the game 68),
  `Water shore foam` (wrong: one white slab), `Water body lamps`, the two caustics switches (+9 and +14 %),
  `Grass shadow trial` (a quarter of a campfire's flicker for 10 ms), `Reflection relief trial` (not run yet).

- TG Path Tracing, 2026-10-09 (PT-85, PT-40): a lamp's light stops growing within its NEAR SIZE (hidden `Advanced /
  Lamp near size`, 15 cm for a lamp of a tavern candlestick's strength, wider with the square root of a stronger
  lamp's; `pt_common.hlsli` `light_falloff`: I / sqrt(d^4 + n^4)). Why: the game's lamps carry strengths set for
  HDRP's I / (d^2 + r^2) with r up to 2 m (candlesticks 21,000 to 28,000 candela, wall candelabra to 76,000); in
  its flame and left to I / d^2 a lamp burnt its own wax white, and the room's rays that met that wax by chance
  were the picture's specks. In the game (NRD, 16 frames): the tavern 41.1 speck spots a frame to 18.5 (the game's
  own 11.1), burnt pixels 1,359 to 24 (17), the picture's mean 23.5 to 22.9; six of seven flame scenes at the
  game's speck level or under it. The owner chose it in the game ("the candles weren't overblown, I saw very
  little speckling, and the light coverage was still good"). A lamp stays in its flame, at its flame's size, for
  its shadows and its reflection. The trial key `Lamp trial near light` is gone.
- R16 HDR compatibility check: with the owner's ReShade, RenoDX and HDR enabled and no RenoDX DLSS5 add-on,
  a temporary capture probe verified the final FP16 scRGB presentation after ReShade. Off, neutral, full and
  restored-Off captures were valid with no fallback or invalid pixels. This confirms the HDR wiring but does not
  fix NR-14. The probe is harness-only and was removed after the run.
- R16 Nexus guidance linked the RenoDX Discord as a community discovery source for `nvngx_dlssnr.dll` while
  explaining that TG Neural Rendering accepts only the unmodified NVIDIA-signed 310.8.0.0 runtime. The patched
  RTX 2000/3000/4000 compatibility DLLs that RenoDX accepts are rejected by our current signature policy.
- The supplied RenoDX RTX 2000/3000/4000 compatibility DLL was checked in a scratch TGNR probe. It reports
  310.8.0.0 but fails Authenticode hash verification, so TG Neural Rendering rejects it before NGX initialization.
  The game folder was not changed.
- R16's scratch-only signature-bypass probe reported `DLSSNR.Available=0` on RTX 5080. R17 corrects the earlier
  interpretation: NGX's own signature check rejected the file before loading it. It was not a hardware rejection.

TG Neural Rendering / shared bridge 1.2.3 (2026-10-07; Mono deployed for testing; shipped in 1.2.3, 2026-10-09):

- R15 validation: repaired stale diagnostic mutation checks, extended native smoke coverage to all four
  styles, added real FoA menu and passive FG/memory evidence to optional NR harness takes. Five live manager
  states pass after correcting the inspector for omitted hidden entries. GPU arithmetic/fallback/shared
  device and multipass checks pass. Tested NR pair installed19:49, no rendering or default change.
  Standalone, FG cooperation, native Video greying, resize/cut/reload checks pass. Latest source96 passes.
  Added opt-in lifecycle audit and capture checks with deliberate corruptions. Quality cost measured at4K.
  DLAA and FSR/XeSS upscaler gates pass. New finding NR-29: FSR FG is slow even with NR disabled,
  reproduced in a separate Off/On/Off run. Cause open, no frame-generation code changed.
  Remaining validation and limitations: docs/DLSS5-VALIDATION.md. NR-14 remains open.

- R13/R14: diagnostic-only exact post-SR input/result captures and alignment analysis. One tavern walk
  at1.2m/s supports current-frame guide alignment, with0.14..0.15 render pixel median tracking error.
  Wrong sign/scale/timing hypotheses are much worse, local outlier crops inspected. NR-only pair
  deployed18:26, Source95 passes. No rendering/default change or lighting stability fix. Round-14.

- R11: corrected advanced Style labels to NVIDIA Default/A/B/C=0/1/2/3, preserving the old saved
  numeric selection. Default is the actual vendor Default. No unverified second preset row. Plain
  tooltip sentences replace semicolons. Both managed builds compile, Mono NR pair installed16:10.
  Added exact per-pixel guide captures and fixed-speed harness walking. Six moving pairs at1.2m/s
  agree with independent camera/depth motion within0.008..0.016 guide pixels median, with aligned
  stair/wall depth outlines. Wrong sign/scale hypotheses give6.59..25.13pixels. No normal guide change.
  Harness-only actual game-grading comparison passes neutral/order checks but halves the measured NR
  effect as it reduces changes. No stability fix or default change claimed. Details: round-11.

- NR Quality follow-up10-08: faster chair runs141444/142304 capture eight valid takes without contention.
  Added actual pixel range and HDR-fallback counts, separate from averaged stage colours; deployed14:23.
  No fallback pixels in the full-strength chair take, steady exposure, exact neutral; instability remains.
  Harness-only per-channel Reinhard comparison added with matched initial history resets; deployed14:41.
  Run144131 finds no meaningful stability win against repeated current baseline (moving p95
  1.965/1.977/1.983 RGB levels). Player defaults unchanged; no stability fix claimed. R10.

- Harness adds a straight forward walk with an endpoint hold for the owner's chair reproduction.
  Quality-only plan compares Off/Strength0/Strength1/Off with full A, one pass. Synthetic camera validation
  rejects six faults; game run122849 saved0 frames (null Unity screenshot, NR-24). No stability verdict or
  NR rendering change. HDR input/output support remains unvalidated; see DLSS5-FEEDBACK.
  Follow-up adds native Unity capture error details, explicit NR-only ReShade compatibility opt-in,
  and resets NR's saved and remembered mode to Off for ordinary PT runs (configs restored afterward).
  Chair report validates actual camera paths/neutral output and draws source/model/output alongside
  screen captures. Three checks reject nine corrupt-evidence cases; source95/harness9 pass.
  ReShade-off run135437 captures960 frames: matching2m walks, exact neutral, constant held exposure;
  full-strength colour drift already at model output. Requested walks now use route validation rather
  than a false accidental-motion alert (reanalysis0 problems). Faster run140059 interrupted by owner
  because GPU contention; excluded. No queue remains; future NR runs use the GPU-free gate.

- Owner comparison 2026-10-08 12:04: RenoDX DLSS5 Upscaled is equally unstable with matching settings,
  with/without RenoDX HDR, TG Graphics Bridge and FG. Supersedes the earlier RenoDX-better impression.
  NR-23 records the slow forward route from PT eyebrows-flickering toward the lady's chairs. Shared symptom;
  root cause unresolved. No rendering change, new stability claim or defaults change from this finding.

- Advanced Passes1-4 (default1) applies NR repeatedly to the preceding result, with a separate feature/history
  for each stage on the shared device. Strength and all tuning apply to each pass, not a final opacity blend.
  Convert scene colour once before the chain and restore HDR/alpha once after its final successful result;
  zero Strength preserves exact input. Pass changes recreate/reset the chain; Off releases all stages.
  NR ABI0x00010300 (frame104/status80), native model-invocation counter and captured pass count.
  Both managed builds, source95, policy plus10 faults and stage17/faults16 pass. Real-model1-4 passes at
  Strength0.5/1 match independent exact-input calls; three broken chains rejected. NR-only deployed11:22;
  tavern7 takes valid, correct pass counts, Off restored. Owner sees worse1pass temporal colour: shortened
  sweep1.15s vs6s earlier; native old/new120-frame moving HDR output bit-identical (NR-22). No stability fix claimed.
  NR-21: owner's "2x" means repeated passes, not native intensity above1; default stays one pass.

- Owner settles both placements on A, full Strength/Local Tone/Detail/Skin Detail, automatic masking Off.
  Quality remains the default. Shared Video/FoA tooltips explain Performance's softer detail and larger
  colour/brightness changes after stopping, and how Local Tone/Strength keep more of the original look.
  Advanced Off/On groups A/B/C, Detail, Automatic Mask and Skin Detail; hiding them retains values/history.
  Both managed builds and Source95 pass; NR-only menu update deployed/hash-checked 2026-10-08 10:31.
  Isolated native intensity1.5/2 produces identical pixels to1; lower values work. Strength remains0..1;
  the owner subsequently clarifies that multipliers mean repeated passes (above). NR-20/21, round-10.

- Adds an opt-in harness diagnostic that resets NR history on every retained-input frame, compared with
  resetting once. NR-only deployment 2026-10-08 09:44; native probe and Source95 pass, stage tests16/faults15.
  Its first game run stopped when the UI became non-interactive; no comparison verdict. No normal-play
  rendering or default change. Recent model testing has not fixed the remaining colour drift (NR-14).

- Adds signed motion readbacks and an independent image-tracking check. At both placements the supplied NR
  vectors match captured tavern movement within 0.11 capture pixel median error; wrong signs/scales fare worse.
  Native diagnostic update deployed 2026-10-08 07:20, with no normal-play rendering change. A/B/C and masking
  replays total 32 takes: C has gentler changes, masking is not a consistent stability fix. Model defaults stay.
  Harness now verifies actual native controls every frame and reports local late drift. NR-14 remains open.
  Live forest A/C favours After for steadier colour; twelve face/tavern takes rejected outside gameplay.
  The harness now stops such scenes before further takes, checking gameplay before freezing and every frame
  (NR-18/PT-26). The retry passes all 12 face/tavern takes. Fifteen stage checks reject fourteen deliberate faults.
  Adds tracked-feature colour measurements (three tests/two faults): Before varies less between moving frames
  here, but settles more after stopping. No claim that one placement is always steadier; NR-14 remains open.

- Fixes HDR glare caused by inverting NR's new peak near white. Restores NR's colour gain using the original
  input shoulder instead. Diagnostic input multipliers 0.25/1/4 exposed the defect; all 12 full-strength
  face/tavern/placement cases pass the new HDR bound, rejecting the older capture. At ordinary Before settings,
  the tavern final maximum cell average falls from 16.20 to 3.16 HDR units (source 3.36/3.29 respectively).
  NR-only deployment 2026-10-08 00:44; 32 takes this round. No reduced Local Tone or new player setting.
  Model-output temporal colour variation remains unresolved. See DLSS5-TUNING and round-09.

- At zero Strength, retains the original scene pixels while continuing NR evaluation. The previous conversion
  round trip changed neutral scene colour by about 0.23% on average. The exact-pixel GPU regression rejects the
  old build and passes the fix; both placements preserve exact scene grids in run 000447. NR-only update deployed
  2026-10-08 00:04. This fixes neutral colour preservation, not the remaining full-strength temporal variation.

- Adds harness-only identical-input replay with optional one-time history reset and depth readbacks, deployed
  at 23:28. Sixteen face/tavern takes confirm continuing NR colour changes with constant colour/depth and zero
  motion at both placements. Twelve evidence tests reject ten deliberate faults. No visual default changed;
  the remaining colour issue is open. The 600-frame follow-up confirms persistent smaller colour fluctuations.

- Adds opt-in same-frame NR input/output, motion and GPU exposure records to the game harness. Two 16-take
  tavern/face runs show constant exposure but local colour changes already at NR output. Corrects Unity SR's
  no-motion marker: held face motion falls from 7680 model pixels to zero; generic RR vectors stay unchanged.
  Installed NR pair at 22:57. This fixes an input defect, not the remaining broad colour instability. Full
  tone/detail/strength stay 1. Stage tests reject five deliberate faults; GPU readbacks reject a wrong row flip.

- Moves FG's actual NGX input observation into the shared bridge so NR works independently of FG and reads the
  selected DLSS resources, regions, scale, jitter, reset and depth convention. BU publishes its RR handoff too.
  No substitute HDRP input lookup. RR+NR GPU coexistence passes; wrong-camera mutation is rejected. Eight real
  tavern motion takes pass; contention prevents timing claims. Both game versions compile.
- Labels NR placement Quality (After Upscaling) / Performance (Before Upscaling) in Video and FoA, retaining
  saved values. Adds a real close-up face harness with A/B/C, mask, skin detail and matched camera motion.

TG Path Tracing, PT-75 and PT-80 a, the filters keep the light and a mirror's picture (2026-10-08, isolated worker;
built with NRD, source checks pass; NOT yet run on the GPU with the fixes, not deployed, not seen in game):
- Why: with Denoising NRD (and the built-in denoiser) the picture held less light than the same picture with no
  denoiser, not the same in each colour (PT-75: 5 to 24 % in the harness's closed room), and a mirror's picture
  came out smeared (PT-80: 0.09 of the reference's detail in game with NRD against 1.24 with none). Details,
  numbers and what is still owed: `docs/agents/filters.md`.
- The tracer's own spatial filter (`pt_atrous.hlsl`: the built-in denoiser's bounced light, and with every denoiser
  but Ray Reconstruction the direct light and the reflections) moves light between pixels and neither makes nor
  loses any. Every weight is the same seen from either of two pixels (the brightness weight takes both pixels'
  noise; the 3x3 smoothing of one pixel's variance is gone), and a pixel keeps what its neighbours do not take
  instead of dividing by the weights it found. Before, a bright (so noisy, so tolerant) pixel took its dim
  neighbours' light while they refused its own: a candle-lit floor's direct light kept 0.75 of its mean, a metal's
  reflections 0.68 (the earlier agent's settled runs), and a mirror with lamps' highlights came out 20 % too
  bright. A model of it on the CPU (`docs/agents/filters.md`): light that arrives once in 50 frames kept 0.76
  before, 0.90 with the weight alone made the same both ways, 1.00 now, for about a tenth more noise.
- NRD: RELAX's firefly suppression is off (`relax.enableAntiFirefly=false`). It cut 5 of 100 parts of the closed
  room's bounced light (6 to 10 where the sky comes through small holes, blue most). Cost in the noise study (the
  earlier agent's runs): 2 to 7 % more boil, and 7 to 13 % closer to the undenoised reference.
- NRD and normal maps (`pt_gi_spatial.hlsl`, `pt_nrd_prep.hlsl`, `pt_compose.hlsl`): RELAX is handed the bounced
  light as the shading normal takes it, divided by the normal map's share from the last frame's filtered direction;
  this frame's share is multiplied back after. Before, it was handed the light as the geometric normal takes it and
  the share was multiplied on after, which lost 4 to 5 % on every normal-mapped surface (the share's mean over a
  normal map is not 1). The share itself is now a single direction plus an even half-space (exact for either),
  where it was NRD's linear SH ratio held to [0, 2]. `resultFlat` is gone. What is brighter than NRD's limit comes
  back as traced by the share NRD was handed (the traced light's alpha).
- Reflections are filtered by what they show (`pt_specular.hlsl`, `pt_atrous.hlsl`, `pt_temporal.hlsl`, a new texture
  for each pixel's shown surface: kind, normal, reflectance). On a surface as smooth as a mirror (roughness 0.15 and
  below, blending out by 0.35) a neighbour counts only if it shows the same surface (normal, plane, reflectance to
  within a few hundredths), the first iteration is no longer fed back into the history, and the history is fetched
  where the shown point was last frame, not where the mirror's face was. A mirror in the mirror, and water in it,
  are left to their history alone. The water's own path keeps its filter, with water alone.
- Tried and taken out again, do not repeat: dividing a mirror's light by the shown surface's reflectance to filter
  it. The surface's own lamp highlights are in the same signal, and so filtered they doubled the mirror's light.
- New offline checks (`tests/pathtracing_gpu_test.cpp`): `nrdMean` (a denoiser's picture converges to the
  undenoised picture's: NRD, the built-in denoiser and what Ray Reconstruction is handed; within 3 %) and
  `mirrorFilters` (a mirror keeps its detail within 10 % and its light, camera still and moving). On the unchanged
  tracer the mirror check reproduces the defect (detail kept 0.22 with NRD and with the built-in denoiser).
- No export, struct, option bit or ABI change: two frame-constant slots that held INVALID are used
  (`material.z`, `denoise1.y`).
TG Path Tracing, PT-73 e, the lantern's wall (2026-10-08, isolated worker; not deployed, not seen in game):
- The traced light's automatic brightening (Dark Adaptation) acts by each pixel's own brightness. One gain for the
  whole picture lifted the wall beside a lantern as much as the dark room (x5.3 in a tavern) and burned it out to
  white. Now a dark pixel takes the whole gain, a pixel at W takes none, and from W up the picture keeps the game's
  exposure, never darker: L' = W - (W - L) / (1 + (G - 1) x (1 - x)), x = L / W. It is the owner's pick ("4b",
  L G / (1 + L (G - 1) / W)) with x (1 - x) for x in the denominator, which arrives at W with the slope of L' = L (the
  pick's own curve meets it with a kink: a ring on a wall). W = 0.54 pre-exposed: what the game's tone mapping (HDRP's
  ACES) shows at 0.4 of white, screen level 168. Luminance only, the colour scaled alike. Nothing changes where the
  automatic gain is 1 (daylight). No row, no option, no export; `pt_common.hlsli` `highlight_hold`, applied in
  `pt_compose.hlsl` after NRD.
- The meter and Fog Adaptation's histogram take the gain back out with the same curve solved backwards
  (`traced_ungained`), so the gain itself and the fog's gain are what they were. The dialogue light's factor (PT-17)
  is that inverse at a mid tone instead of 1 / gain, which would now leave a lit face too dark.
- Not held: the tracer's water, HDRP's fog light, what HDRP still shades. None and Ray Reconstruction get the curve
  on raw samples. `docs/agents/highlights.md` has the numbers and a prediction of the lantern scene (picture level
  about 22 for 24.8 now, the game's 13.9; round the lamp 0.01 % of pixels at 250 and over for 1.3 % now).

TG Path Tracing, PT-71 item 6 (2026-10-07, isolated worker; not deployed):
- Glass/cornea are camera-ray surfaces with the same dielectric transport for reflection and
  bounce rays: material IOR/roughness, Fresnel reflection, refraction and absorption over real
  walls. Compensated roulette replaces fixed crossing counts. Eyes and opaque Unlit are visible
  to every ray. Unlit emits its shader colour without HDRP lighting.
- Removed the glass prepass, guessed shell thickness/exit distance, HDRP background/material
  borrowing, HDRP shading-share pass/history, Shadow Detail pass/event/export/row, and the glass
  guide patch that forced roughness 0.03. Details and remaining material/caustic gaps are in
  `docs/agents/glass-eyes.md`. Material size 160 -> 192 bytes; coordinator assigns the ABI bump.
- Zero optical absorption now stays clear even when the diffuse base is coloured. Removed
  that per-interface colour filter and its tint in the glass specular guide (0.99216 now,
  0.51373 with the old guide filter restored by a mutant). Missing absorption data is logged.
- Final raw GPU evidence (Denoising None): eighty interfaces / bare 1.069; absorption ratios
  0.3669/0.8174/0.9591 against 0.3679/0.8187/0.9608; IOR 1.5 pane 0.9252 against 0.9231;
  jar centre/rim red 0.536/0.133; eye in mirror/direct green 0.994. A slanted pane hits the
  target at Snell's predicted 0.26331 m displacement. Ten raw checks pass and all twelve mutants
  stop at their intended failures. Source checks: 89/89. Initial absorption
  for rays born inside glass remains open (new request ID pending; PT-72 is already used). No in-game appearance verdict is claimed.

TG Neural Rendering 1.2.2 (2026-10-07; Mono deployed for testing, no release package; shipped in 1.2.3, 2026-10-09):

- Adds an independent Mono/normal-version mod through public NGX on the shared bridge device. A single shared
  scene pass originally recorded NR after tone mapping; the placement correction below moves it before lens effects. FG and RR share its NGX lock
  and initial model search path. The bridge and all source versions move to 1.2.2.
- Adds A/B/C, before/after upscaling, Local Tone, Detail, Automatic Mask and Skin Detail to Video/FoA. Earlier
  placement adapts HDR colour for the LDR model and restores HDR; RR requires After Upscaling. Neutral HDR probe
  error is at most 0.144%; live intensity preserves history. These are functional results, not a chosen look.
  The Mono update was deployed/hash-verified at 18:21. Tavern, castle and forest comparisons now run with
  matched 65 cm / 25-degree camera movement. [Evidence](docs/DLSS5-TUNING.md).
- Lowering Local Tone reduces measured motion-dependent brightness changes, but does not establish the cause.
  The owner wants NVIDIA's lighting/colour changes and reports that they disappear while moving, then return
  when still. Defaults remain at full strength/tone/detail; investigate motion inputs, skips and history first.
  The proposed tone-zero default was withdrawn before deployment. [Evidence](docs/DLSS5-TUNING.md).
- Corrects both NR placements to run immediately before/after HDRP upscaling, ahead of DoF, motion blur,
  chromatic aberration, vignette and tone mapping. Both use the reversible HDR adapter. The native FG capture
  remains downstream. BU and NR share their CustomPass recorder, including the IL2CPP unboxing fix. NR now
  requests the bridge motion fixes itself. Both game versions compile; Mono deployed/hash-verified 20:44.
  Eight tavern move/hold takes pass: NR evaluates every frame before lens effects; the stop jump falls from
  1.130 to -0.163 display levels with Local Tone still 1. Off restoration passes in that run; this is not a
  general appearance or HDR-compatibility verdict. Fog stays in the incoming scene. See tuning evidence.
- Static NR restoration checks reject different camera poses, as moving comparisons already did. A 0.373-degree
  shift invalidated the castle's final static Off comparison; it did not invalidate its matched movement takes.
- Adds Neural Rendering (Off / DLSS, renamed from Off / On after the owner's first test) and Strength in Video/FoA.
  The saved On value is retained and labelled DLSS, so existing preferences survive. Starts Off; greys out without DLSS selected (the owner's
  explicit ruling), SDR, a supported runtime, or denoised PT input. PT debug views bypass it. Missing/rejected files
  affect NR alone. The player's model stays outside packages; signed 310.8.0.0 is the first validated input profile.
- Real bridge-device checks: 20 NR evaluations, neutral strength, resize, release/restart and sRGB/linear transfers;
  neutral sRGB error zero byte levels. With Streamline active, 32 RR and 30 following NR evaluations pass without
  RR fallback. Missing/rejected runtime and invalid-viewport fallback pass. Deliberately black output and ignored
  strength fail the GPU probe; frame/camera/depth/history mutations fail policy checks.
- Extends the saved-scene harness with NR settings, actual evaluation counts, GPU timestamps, Off/neutral image
  comparisons and visible-HUD captures. [Evidence and remaining acceptance](docs/DLSS5-IMPLEMENTATION.md): actual
  game integration, ReShade, appearance/defaults and performance at game resolution still await coordinated testing.

From 1.1.0 on, the version numbers are the ones the mods have on Nexus Mods, and TG Frame Generation,
TG Better Upscaling and the bridge they share all carry the same one. Earlier entries use the repository's
own numbers: v0.5.0 is the release published on Nexus as Frame Generation 1.0.2.

TG Path Tracing: what the in-game harness found first (2026-10-06; in the tree, state per entry in `docs/PT-REQUESTS.md`)

- **The in-game harness takes an inventory of the game's effects, lights and fog** (PT-71 items 7 and 8, the first job
  of `docs/agents/design-particles-fog.md`; `docs/agents/effects-inventory.md`). Why: before particles, transparents and
  fog can be traced, what the game really has must be known ("the scanner alone proves none of them"). A recipe
  `effects` (Off, On, Off; no pictures) writes, per scenario, every loaded ParticleSystem, VisualEffect, trail, line and
  transparent or unlit mesh the capture rejects (with why), their materials, the lights attached or near (and what the
  tracer did with each), the blended fog and every local fog volume, as JSON and a short table; three probes a second
  apart before and inside the freeze say whether particles and shader clocks really stop. `tools/harness/effects.py`
  sums the families (`tests/harness_effects_test.py`, 9 checks, 9 mutants). Read-only: nothing it does changes the
  picture. Built by a GPT-6-astra worker through Codex; not yet run in game.
- **Light inside wax and skin, the wet ground and the hero's head, at the tracer's own camera ray's hit; their old
  route removed** (PT-58; `docs/agents/port-2.md`). The three jobs below (merged in `docs/agents/merge-3.md` onto the
  tree of 15:06) brought onto the tree whose pixels are the tracer's own camera rays' hits (PT-59), and made to work
  there; then, by the owner's rule of 17:55 ("remove all the 'hacks' and workarounds as we go, all the stuff we did as
  workarounds for not controlling the tracing from the camera ... So we have a clean base to work from"), their
  versions for pixels whose surface was HDRP's G-buffer taken out, with their tests and docs. What a pixel's own ray
  stopped at now says it all: a pixel walks inside the material its ray met (`TGPT_MATERIAL_SUBSURFACE` and its
  shape, the walk's entry the hit itself, `pt_walk`); a leaf the ray met lets through its material's own share
  (`TGPT_Material.subsurface`, as where a path meets it); the hit is made wet in `hit_material`, the one place every
  ray's hit is (not skin, wax, eyes, glass or hair); the camera's ray passes the inside of the hero's first-person body
  at any distance, as every ray that shades what it meets does (the 30 cm rule is gone from the camera ray too).
  Removed: the walk on HDRP's subsurface pixels (`walk_pixel`, `walk_entry`, the G-buffer half of `exit_normal`) and
  the table of profiles by G-buffer index it read (`TGPT_SetDiffusionProfiles`, `FrameConstants.sssProfiles`); HDRP's
  thin-object transmission through skin by its thickness map (`skin_transmission`, `through_skin`,
  `transmitted_unoccluded`, `skin_transmittance`, `Surface.transmittance`, thin skin's bit and bytes in the surface
  record, `TGPT_SetSkinTransmission`, `FrameConstants.transmission`): an ear lit from behind is the walk's; a leaf's
  transmittance read from GBuffer2; the wet ground made on HDRP's G-buffer in `pt_prepare`. On the route before the
  tracer's own rays (`TGPT_OPTION_GBUFFER_SURFACES`, the hidden key) nothing walks and leaves pass the grey half.
  FrameConstants 7280 to 6512 B. Two exports gone (the ABI number is the coordinator's to set). Offline, all with
  Denoising None: `tgpt_gpu_test` passes (`docs/agents/port-2.md` "Final runs"): the wax checks against the reference walk
  on the tracer's own rays (the cylinder's far side 0.995 0.997 1.003 of it, slabs 0.991 to 0.995, the thick piece 0.985
  to 1.000), a floor that scatters 3 cm (shadow edges 0.51, light 1.006), the wet ground's checks at the tracer's own
  hit (the same counts as before, every band pixel), what is not ground stays dry (11,575 of 11,575), the hero's body
  in a mirror whose own pixels are the tracer's hits and seen by the camera's own ray, a leaf the camera's ray meets
  passing 2.000 1.000 0.333 of the grey share's light for a share of 0.6 0.3 0.1. Not seen in game.

- **Wet ground where the waves have been** (PT-52; `docs/agents/water-edges.md`, ported by `docs/agents/merge-3.md`).
  The owner, 2026-10-07: "would be cool if the water waves left a wetness trail ... like wet sand on the beach".
  A store of the world's cells round the camera (`pt_wet.hlsl`, 32 MB, 2 cm cells near the camera) remembers each frame
  where HDRP's own waves stand (the caustics' reading, now in `pt_waves.hlsli`), as when the water last stood at each
  height. Every ray's hit reads it (`hit_material`; the camera's own ray's too since the port, PT-58): a mirror shows the
  same wet sand. Wet ground is darker by the water's internal reflection (Lekner and Dorf 1988: A' = (1 - 0.0665)(1 - 0.4749) A /
  (1 - 0.4749 A); wet rock of albedo 0.3 is 0.57 of dry, 0.7 is 0.73), one mirror (water's f0 and roughness) while a film
  covers it; the film runs off as a draining film does (Jeffreys) or soaks into sand (5 s), then the rest evaporates
  (0.2 mm an hour: a rock face about 15 minutes, sand about 2 hours; sand told from rock by the slope, no steeper than
  its angle of repose). All on the game's clock. Hidden "Wet ground" row (on), `TGPT_OPTION_DRY_GROUND` (536870912) for
  comparisons. The F8 report's water line names each surface's foam settings. Offline: `tgpt_gpu_test`'s wet ground
  checks (two ramps into the pool, 30 s, 450 s and 1,000 s after the water left, the rays' Materials view, HDRP's waves'
  reach, a full record), each mutation-checked in the merge (`docs/agents/merge-3.md`). Not seen in game.
  What a walk inside wax or skin meets in there (a wick, a holder's spike) does not read the store (`g_hitInside`):
  nothing inside a solid thing is wet, and with that read inside the walk's loop the reflections pass hung the GPU on
  its first frame (RTX 5080, driver 32.0.16.1692; the mirror job's hang again, by another way: the merge's report).
  **Not merged from the same job** (built and tested offline in its copy `wt/wedges` only, in the water passes that go
  with PT-60): the bed seen through the water taken from HDRP where the traced copy of a rock disagrees (the cave lake's
  pale slabs), the radiance law for light out of the water (over n^2 = 1.78) and the bed's trapped light, absorption per
  path before averaging, the rough reflection's own weight. Its report's section 0.3 says what water as a surface of the
  tracer's own camera rays needs from them.
- **Light inside wax and skin is traced; leaves pass light by their own material where paths meet them** (PT-40, PT-43;
  `docs/agents/sss.md`). The owner, of the candles: "what are these black spots in the wax bases of the candlelights
  here? Looks fully black inside the wax in spots"; of the screen-space blur that stood in for skin's scattering: "we
  don't want shortcuts or hacks - we want proper path tracing, and everything should be path traced"; and "That would
  also affect vegetation like leaves and grass I hope." Until now wax was lit as a surface or a thin leaf (so a thick
  lump's folds, which no light reaches from outside, were black), skin's scattering was HDRP's own screen-space filter,
  copied (`pt_sss.hlsl`), and a path that met wax or skin saw a painted surface. Now light that meets what HDRP draws
  with subsurface scattering goes in and comes out elsewhere: a random walk inside the traced mesh (a new pass,
  `pt_walk.hlsl`, for the pixels the camera sees; `subsurface_vertex` where bounced light, the radiance cache and
  reflections meet such a material), lit where it leaves the mesh by the sun and the lamps (shadow rays) and by one
  bounced ray. The medium is the material's own, from its diffusion profile and its color by HDRP's own conversion (its
  path tracer's `RemapSubSurfaceScatteringParameters`: the scattering distance d is a mean free path of d x (1.9 - A +
  3.5 (A - 0.8)^2), the color A keeps 1 - exp(A (-5.09406 + A (2.61188 - 4.31805 A))) of the light at a scattering):
  the fit after Christensen and Burley 2015, the walk as film renderers do it (Chiang, Kutz and Burley 2016). The three
  color channels share one walk, their densities weighed together (hero-wavelength sampling), so each stays unbiased.
  No walk is cut short: it ends by leaving the mesh or by Russian roulette (HDRP's own path tracer stops at 16 steps and
  drops the light; the bound here, 4096 rays, is out of reach: one walk in 16 million of those still going after 1024
  scatterings). A piece of the same wax grown into another is walked through as one; anything else inside (a wick)
  reflects the walk by its own color. `pt_sss.hlsl` is deleted: no blur of light on screen is left. ReSTIR's samples are
  not used for these pixels (they are the light at the pixel, not where the walk comes out): one walk and one light
  sample a pixel a frame, unbiased, for the denoisers to clean; reusing exits between pixels and frames is the next
  step (Werner et al. 2024). Thin things: a leaf or a blade a path meets now passes its own share of the light from
  behind per channel (the plugin works out HDRP's transmittance of the material's profile at its `_Thickness` times its
  `_TransmissionMask`), where it passed a grey half; the camera's own pixels of it already did. What `TRANSMISSION` (0.5)
  and `CLOTH_TRANSMISSION` (0.3) still stand for is written at them in `pt_common.hlsli`: the half is the share ReSTIR
  weighs samples by, the share where a material names no profile, and how often a light's ray passes a blade (the
  owner's grass-shadow ruling of 2026-10-06, a look, left as it is); thin cloth keeps its rule because the game gives
  nothing else to go on (its cloth is HDRP's standard material: no profile, no thickness, no transmission). Named, for
  the merge to number: `TGPT_Material.subsurface[3]` (was `textureReserved`, same size and place),
  `TGPT_MATERIAL_SUBSURFACE` (2097152), `TGPT_OPTION_NO_SUBSURFACE` (268435456, for testing: subsurface materials lit
  as plain surfaces); `TGPT_SetDiffusionProfiles` now takes every profile's shape, thin ones too. Offline
  (tgpt_gpu_test 2c, the Direct view undenoised against a random walk on the CPU through the same shapes, in double
  precision, written from the method): a wax cylinder with a lamp behind it, its far side 0.995, 0.997 and 1.003 of the
  reference in red, green and blue (black lit as a plain surface), the same with a second piece of wax inside it; slabs
  3 and 10 mm thick pass 0.995 of the reference (0.81 and 0.54 of the red that entered, 0.69 and 0.39 of the blue); a
  thick flat piece under even light 0.985, 0.997, 1.000 of the same piece painted (it keeps its color) and within 1.7 %
  of the reference; the floor of a deep fold, in its walls' shadow, lit at 1.2 % of the light on the wall's lit face,
  red over blue 3.2 (0 as a plain surface); a floor behind a wax slab takes the slab's light through its paths, green
  and blue against red 0.930 and 0.851 (the reference's 0.930 and 0.852); a floor behind a leaf with the share 0.6, 0.3,
  0.1 takes 1.99, 1.00 and 0.34 of the light it takes at 0.3 grey, and 1.667 with no share given (the half). Each check
  mutation-checked (the pieces not counted, no walk at path vertices, a leaf's share not read; the light gathered at the
  entry together with the roulette's survivors not made up for; the channels' densities not weighed together). Cost: with wax over the
  whole 1280x720 view (a walk of 22.7 flights on average there) the direct light's stage took 3.9 ms against 0.8 ms
  without the walk. Not checked offline: the walk where a reflection or the radiance cache's samples meet wax or skin
  (the same call as at bounced light's vertices, which is checked). Not walked: a subsurface material drawn from both
  sides (no inside; lit as before). Not seen in game: the plugin's reading of each material's profile compiles and logs
  what it found ("Subsurface material ...", "Thin material ..."), which the next harness run shows.
- **Materials as the game's own shaders show them; no texture copies, no borrowing from the screen** (PT-64;
  `docs/agents/materials.md`). Why: with the tracer's own camera rays, what the player sees of a surface is what a hit reads of
  its material, and the yardstick's first outdoor run (20261007-171522, the forest) had the same surface as HDRP's on 65 % of
  the screen and, of those pixels, another colour on 27 %, another normal on 37 %, another roughness on 47 %. The materials it
  named are drawn by seven Shader Graphs of the game's own (`TG/Environment/Cliffs/Cliff`, `Vegetation_Grass`, `MeshTerrain`,
  `Vegetation_Leaves`, `Vegetation_Trunk`, `TG/Architecture/KF_Architecture`, `TriplanarArchitecture2`), which the tracer read as HDRP Lit: one map, one
  tint. The rock lost its close-up layer and moss (97 % of its pixels differed), the grass its two colours (87 %), the ground
  all eight layers (97 %), the furniture its tint, called `_Base_Tint`, and its normal map, `_Base_NormalMap` (82 and 95 %).
  Now each has a program (`pt_materials.hlsli`, `MaterialPrograms.cs`, `TGPT_Material.program`, `TGPT_SetMaterialPrograms`)
  that computes what the shader's GBuffer pass computes, written from the game's compiled code. Also: HDRP Lit's detail map;
  the mask map's occlusion through its own range; a normal map's scale as Unity applies it; HDRP's Double-Sided Flip mode;
  the glow of the game's own graphs (characters, grass, leaves) as their code computes it; glass's own index of refraction
  (`TGPT_Material.ior`); the emissive map read as the game's own texture; a mesh's fourth texture coordinates and vertex
  colours. Removed: `hdrp_material_at` (a reflection or a view through glass took HDRP's on-screen colour and normal) and the
  rule that left textures under 64 KB to 64x64 copies (`kMinBytes`, `TGPT_SHARE_SMALL`). The yardstick's line now says, per
  material, how its pixels differ: place, colour, normal, roughness. `TGPT_Material` and `TGPT_MeshDesc` 176 B,
  `TGPT_Yardstick` 632 B. Not in game yet; what is left (skin, `Dissolve3D`, snow, parallax, thin cloth's
  share of light, which no material holds a number for) is in the report with counts.

- **The caustics' lookup by an acceleration structure, and the proof that HDRP's waves arrive** (PT-14;
  `docs/agents/caustics-3.md`, from the review `docs/agents/review-caustics-gpt.md`). Why: in game (run 20261007-140305) the
  cave lake ran at 5 to 6 fps and showed no caustic lines. The lines were missing because no water slot ever got its waves
  (the plugin read HDRP's `RTHandle` buffers "as Texture": fixed in `WaterCaustics.cs` the same day). The frame rate went to the
  lookup: with the Caustics row on the lake's composite took 40 to 50 ms and the water passes 73 to 84 ms; with it off 0.4 ms
  and 23 to 25 ms (the same log). `caustic_light` walked a world-space hash: 8 buckets' linked lists at every level, for
  every shaded point. Now the landed triangles are the vertices of an acceleration structure of their own, built every frame
  (over what the last frames wrote and half as much again, not the whole room: a build over 16 million unused triangles took
  10 ms), and a point asks it with one short ray along its normal; each triangle's own tolerance decides as before, now
  5 cm at most (a metre-wide triangle lit everything within 10 cm of its plane). No walk limit is left (the hash stopped at
  4,096 records and dropped the rest), and the light is kept as floats (halves clamped a focused line). Offline, a lake with 25
  lamps round it at 1280 x 720: the caustics and the composite together 33.7 ms with the hash, 4.4 to 6.1 ms now, 1.9 ms
  with the row off (the GPU test holds it under 10 ms). The tracer says in its 10 s log line what the waves are where it reads them ("water as the tracer
  reads it: slot 0 waves up to 3.0 cm high, slopes to 0.471 ...": 0 cm and flat would be still water), and the GPU test hands
  over wave buffers as HDRP makes them (texture arrays of half floats with mips, three bands, a turned and moved surface, a
  distance fade) and holds the tracer's surface and normal at a grid point against HDRP's formula: 0.015 mm and 0.05 degrees
  off at most over 14 wave states; four mutants of the formula each fail it. Not changed: where the light lands (flat
  triangles, left out across edges), who asks (the composite and the bed under water); the report has the design for the
  rest (beams any ray can ask at the surface it meets) and what a first build of it measured.
- **The tracer fires its own camera rays, for every pixel; hair is a surface of the picture, no layer** (PT-59, PT-57,
  PT-53 to PT-56; `docs/agents/hair-6.md`). The owner, 2026-10-07: "make sure WE trace the world, not the game", and of
  hair laid over the picture after the denoiser: "the wrong design entirely". Built and checked offline; not yet seen in
  game.
  - Each pixel's surface is what its own camera ray finds in the traced scene (`pt_primary.hlsli`, called from
    `pt_prepare`): from the near plane, the nearest thing that stops it. A hair card stops it as often as its texel covers
    (the game's alpha, a new draw every frame), a cut-out as often as its alpha tile covers; it passes what the camera
    does not draw (shadow-only stand-ins, the back of a one-sided surface). The surface record is the one the G-buffer
    route wrote, so the light, the filters, NRD and the guides for Ray Reconstruction are unchanged after it. New beside
    it: the hit's emission, and its motion from its own previous position (the camera's move; a character by its previous
    skinned positions, which `pt_skin` now keeps), which the tracer's reuse across frames, its filters and NRD follow.
  - HDRP's G-buffer is the yardstick: Debug View "Yardstick" (`TGPT_VIEW_YARDSTICK`), a `Yardstick` line in the native
    log and one with the materials' names in the BepInEx log every 10 s, the F8 report, `TGPT_GetYardstick`. Per pixel:
    the same surface or not (nearer, farther, nothing traced, traced over sky), and on the same surface another colour,
    normal or roughness; and whether its own motion is HDRP's.
  - The hair layer is gone (`pt_hair.hlsl`, the composite's `hair_at` and `hair_guides`, four output textures). While
    tracing, HDRP draws the depth of a head of hair and a beard again, at the game's own alpha clip, and not their colour
    (`CharacterCapture.HoldLayerDepth`), so HDRP's depth and motion vectors at a hair pixel are the hair's in the frame
    shown: what the composite places the light by, and what DLSS and Ray Reconstruction accumulate along. Hidden from
    HDRP, a hair pixel had the wall's, and the hair slid over the wall as the camera moved (measured in run
    `20261007-144742`, frame to frame: 2 to 5 px a frame against the game's 0.1). Brows stay hidden from HDRP (their shader
    has no opaque build that cuts) and are found by the pixel's ray a millimetre over the skin, whose depth and motion are
    theirs. The scalp and beard masks are HDRP's again, in its G-buffer.
  - The composite: a pixel of the G-buffer takes a texel the tracer's own ray made (complete light, no G-buffer colour,
    no GBuffer3), which with own rays is every texel.
  - A hidden key, `[Advanced] Own camera rays` (default on; off: `TGPT_OPTION_GBUFFER_SURFACES`, the route before, with
    hair still by its own rays).
  - What still comes from HDRP: its depth and motion vectors of the frame shown (the traced light is a frame old then;
    only HDRP knows that frame's geometry). Known gaps with own rays, each visible in the yardstick: things the plugin
    moves between its scene scans (a held weapon, a door: traced where the last scan left them, up to 2 s old), what the
    traced scene lacks or holds coarser, grass and leaves standing still, and what is not read from a hit yet (skin's
    scattering and transmission, a leaf's own transmittance, decals, terrain layers, parallax). Colour detail is now part
    of the traced light, a frame old when shown: textures may look softer under movement.
  - API: a new export and struct (`TGPT_GetYardstick`, `TGPT_Yardstick` 376 B), a new option and view; the ABI number is
    left for the merge. FrameConstants 7120 B; `MeshRecord` 36 B.
  - Offline (`tgpt_gpu_test`, all without a denoiser): all 1173 checks pass (native `4A4B4346A69F`). Hair 3 m before a wall 12 m, the camera moved 10 cm between the traced frame and the shown one: its squares where they are in 0.894 of the pixels still and 0.885 moved (0.888 with own rays for every pixel); the same card hidden from the drawn depth 0.672 (the control: it slides). Five cards of 0.3: the hair in 0.820 of a frame's pixels and 0.841 of the mean of 16 (the game's blend 0.832), 0.93 of the pixels now hair, now wall. The yardstick over the test room: the same surface 96.99 %, own motion off the drawn vectors' 0.06 % under a moving camera; a cube only traced and a slab only drawn found, the cube's material named; a character moved 10 cm: 1688 of its 1688 pixels with motion of their own. The picture by own rays against the G-buffer route where the yardstick says the same: x0.996. Eight mutants, each caught (the report's table). `tgup_gpu_smoke --with-pt` passes.
- **What the game moves is traced where it is, every frame** (PT-71 item 4; `docs/agents/moving.md`; built in its copy, not
  in the game). Since the tracer's own camera rays draw every pixel, a thing the game moves was drawn where the last scene
  scan left it, up to 2 s old: the weapon in a hand, a door, a carried lantern, a turning windmill. The owner: "What moves
  is traced as it moves".
  - Placed every frame where HDRP draws it then, right before the frame is handed over (`TGPT_SetMoving`): every
    GameObject mesh renderer that is not statically batched, Drake's movable entities (no `Static` tag: the game's
    `LinkedTransformSystem` copies their GameObject's matrix every frame), and any entity the scan listed that is found to
    have moved since (the game turns windmills, flies birds and lifts the elevator's chains on entities it tags `Static`):
    each frame the chunks some system wrote are compared with the list, and one found moving is placed every frame from
    then on. What appears, disappears (a sheathed weapon) or changes its level does so the same frame. Each is an instance
    of its mesh's own acceleration structure in the frame's top level: nothing is rebuilt.
  - Each with where it was a frame before: its hits' motion is its own (the point taken back into its mesh's space and
    forward by the transform it had), as HDRP's motion vectors give it; the denoisers and DLSS follow it.
  - The flag `TGPT_INSTANCE_MOVING` ("moved since the last scan: its glow is not sampled") is deleted with the reason for
    it. A moving thing's glow is met by paths, not sampled as a light: the same light, more noise.
  - A frame is traced with the scene list it was submitted with (a scan between submit and trace swapped it before).
  - First person: the game draws the hands and the weapon with its one camera, field of view and near plane; the hands are
    Kandra (posed every frame already), the weapon Drake's movable entities (now placed every frame).
    ABI 0x00012100 in its copy (`TGPT_SetMoving`, `TGPT_Moving` 112 B). Checks: GPU checks of a box moved since the scan
    (traced where drawn, its scan copy not), moved 10 cm in a frame (its motion the drawn motion vectors'), hidden (traced
    nowhere); `tests/moving_rules_tests.cs` (11 mutants killed); 91 source checks.
- **The traced scene is the world the game draws** (PT-63, PT-61; `docs/agents/world.md`; merged 2026-10-07 18:44 onto the camera rays, not yet in
  the game). The owner, 2026-10-07: "make sure WE trace the world, not the game"; of objects left out: "that sounds like
  a big problem, if things suddenly disappear when PT is enabled. Fix that"; of plants standing still: "of course our
  shadows and reflections should move alongside the grass and leaves swaying in the wind". The tracer's own camera rays
  (PT-59) show only what is in the traced scene, so:
  - Objects left out are traced. Statically batched renderers: once a combined mesh, each member's materials on its own
    submeshes (they were all skipped). Drake entities with a LOD range and no reference point (they matched no query).
    Trees past their last mesh, where the game draws a card that turns to the camera: the tree's coarsest mesh.
  - The traced level of detail is the drawn one, by each of the game's renderers' own rule read from its code
    (`WorldRules.cs`; Entities Graphics' with the quality's maximum LOD level, Unity's LOD groups culled past their last
    level, Medusa's, Leshy's), with the LOD bias HDRP culls the camera with. The rule "within 30 m the finest level
    whatever the game draws" (round 2) is gone: it made the traced mesh finer than the drawn one. A Drake entity is traced
    while the game has it loaded for drawing: its visible range was compared with the plain distance to its own
    position, where the game compares the scaled distance to its LOD reference point (15 % off at a 60 degree view), and
    the Draw Distance row's extra distance was not counted. Levels are chosen again within half a second of a metre of
    camera movement (4 m before) or of another LOD scale.
  - Grass, leaves and trunks sway in the tracer as the game sways them. The three vegetation shaders' vertex maths was
    read from their compiled code (`Vegetation_Grass`: a push along the wind by a gust noise over the world;
    `Vegetation_Leaves` and `Vegetation_Trunk`: a turn about the wind's axis by noise x speed x strength degrees, fading
    with the camera's distance). Every such instance is posed every frame (`TGPT_SetPlantWind`, `TGPT_SetWindNoise`,
    `pt_sway` steps 3 to 5): the instances of one 16 m cell are one group with one BLAS refitted a frame, so no count and
    no distance leaves a plant still, and posed plants are met by every ray from where it starts: the plugin no longer
    flags them as things rays distrust (that is left for cloth past its 4,096 copies alone). Trunks were traced still and
    trusted while HDRP bent them.
    ABI 0x00012000 (two exports, `TGPT_PlantWind` 48 B, `TGPT_PlantWindFrame` 32 B; built as 0x00011F00 in its copy).
  - No distance past which nothing is traced ("Why? Do that"): the Scene Radius (250 m) and Ray Distance (500 m) rows and
    every cut by them are deleted, not set wide (the owner, 17:55: "remove all the 'hacks' and workarounds as we go").
    Each capture path traces what its renderer draws however far off; a sea without an end is traced to the camera's far
    plane. Offline, 19,881 more instances cost 0.08 ms of the GPU and 0.8 ms of the render thread; what the game's far
    world costs the scan on the main thread is to be read off the first run.
  - Still open, written up in the report: decals seen by rays (the plugin now counts them a scan, for the design's cost).
  Checks: `tests/world_rules_tests.cs` (15 mutants killed); GPU checks 14 and 15 (plants' shadows against the game's pose
  worked out on the CPU from the compiled shaders, and the floor at their feet); 90 source checks.
- **Real traced caustics** (PT-14; `docs/agents/caustics-2.md`). The owner, 2026-10-07: "Do. not. fake. stuff. Real path
  traced caustics", and "I want to see caustics ... in the next playtest". The three stand-ins are gone: the sun's and the
  lamps' mirror image in a flat water plane times HDRP's ready-made ripple picture (`water_reflected_sun`,
  `water_reflected_lamps`), and that picture multiplied onto the light under the water (`water_caustic`). In their place:
  - The tracer gets HDRP's real waves. The plugin hands over each water surface's wave buffers (HDRP's displacement and
    surface-gradient arrays, one slice a band) and the numbers HDRP set for them that frame (each band's scale, current
    offset, amplitude and distance fade, read from HDRP's own constant buffer right after HDRP fills it). The tracer copies
    the buffers every frame and moves the water's surface as HDRP's water shader does. Every water surface near the
    camera counts now, its caustics setting or not.
  - Light is traced onto those waves (`pt_caustics.hlsl`, a stage of its own before the composite). A grid over the water
    (2 cm cells round the camera, twice as coarse each ring out, moved a random fraction of a cell every frame); from each
    grid point a shadow ray to the light and two rays on, the reflection and the refraction (index 1.333, exact Fresnel),
    each to what it meets. The light through each grid cell lands on the triangle its corners' rays hit; its brightness
    there is that light over that triangle's area, so rays a wave bunches make bright lines (the warped light mesh of Ray
    Tracing Gems II, chapter 30). Every lamp counts: each 15 x 15-cell tile traces its two strongest lights and one of the
    rest chosen by its light (divided by its chance), picked over all lights by the tile's 256 threads; no lamp loop per
    pixel (the stand-ins' loop cost 42 ms a frame in the mineral cave once PT-50 let every lamp reach every cell).
  - Who sees it: every surface the camera sees (added to the direct light after its filter), and the bed seen through the
    water. A light's straight shadow ray that crosses the water stops there while the Caustics row is on: under the water
    the light comes through the waves, as the caustic.
  - Offline (`tgpt_gpu_test`): still water is the mirror's limit (the floor under it takes 0.9782 of the sun, the water's
    transmittance being 0.9779; the sun's mirror image on a wall 200,680 of 200,681 pixels within 3 %); a sine wave's lines
    land where a CPU tracer puts them, 10.1 times the mean against its 10.3; waves keep the floor's mean (0.998); the lines
    move with the waves; a spot's mirror image on a wall 253,300 of 253,352 pixels within 3 %; three and four lamps add up.
    Each new check mutation-checked. Not seen in game yet.
  - Not built yet: light off glossy floors that are not water (the cave's wet patches), caustics seen in mirrors and in
    bounced light, HDRP's current maps, water masks and deformers, emissive surfaces as caustic sources.
- **Hair stays on the head while the camera moves** (PT-53, PT-56; `docs/agents/hair-5.md`). The owner: "When the player
  character moves, the hair on NPC peoples heads move too", without any denoiser; of Ron from behind, "one layer sitting
  close to the head, which doesn't move, and then a translucent one hanging around it, which does flicker and move". The
  traced light reaches the screen a frame late and the composite moves it by HDRP's motion vectors. Hair drawn as a layer
  was written into the texel of the surface behind it, and HDRP has no hair while tracing (the mod hides it), so the hair
  moved with what was behind it: over the skull with the skin (the still layer), around the head with the far wall (the
  moving one; at the harness's slide about 17 pixels a frame). Not Kandra: it skins every vertex every frame in world
  space, and the world was frozen. Now pt_hair keeps the hair apart (its light and cover, its first card's distance, its
  guides for Ray Reconstruction: four shared outputs) and the composite finds, per pixel, the texel whose hair point lands
  there now, by HDRP's own motion-vector rule for that point, plus the motion of a character's skin behind it. Debug views
  still lay the hair in at the trace. Left as before: HDRP's motion vectors at hair pixels (what DLSS and Ray
  Reconstruction accumulate along), and the object motion of a walking character's hair over a still wall. Offline
  (`tgpt_gpu_test`, a layer card 3 m away before a wall at 12 m, the camera moved 10 cm sideways): its checker where it
  is at 0.902 of the card's pixels after the move, 0.892 with the camera still; the wall's motion would put it 15 pixels off.
- **Hair takes the room's bounced light, not the shade under it** (PT-54). "Some NPCs hair glows on top": a bowed head
  lit by candles behind it had a black oval over its crown and candle-lit strands around it. The crown faces away from
  the candles, so only bounced light reaches it, and a card's bounced light was the radiance cache's at its own point:
  the cell of the scalp a centimetre or three under it, which the scalp's rays feed and whose rays meet the hair above.
  That is the light in the hair's own shade. Each card now traces its bounced light: one ray over the whole sphere (a
  strand is lit from every side), shaded as the bounced light's rays are (emission, sun and lamps, the cache past them,
  Bounces deep). One more ray, and its shadow rays, per lit card. Offline (a whole card a centimetre before a near wall,
  lit only by the sky's bounced light): 2.43 times the wall's light by its colour. Not settled: that lobe gives up to pi
  times a matte surface's light, HDRP's ambient on hair is a matte surface's, and a cosine lobe tried instead made the
  card 31 times too bright with overflowing samples, cause not found; and the old cache term passes that check too (0.85
  of the wall): the offline scene does not reproduce the dark crown. In the copy, not to merge yet (`docs/agents/hair-5.md`).
- **Hair and eye colours (the harness's run 20261007-122527): no colour bug.** The tracer reads the character's own hair
  colours (the blond man's _DiffuseColor 0.893, 0.827, 0.714), and the traced frame is warmer everywhere by the same
  amount: red's share of the colour 0.54 to 0.64 on the grey man's hair, 0.61 to 0.71 on his white shirt. The tavern's
  traced light is its candles'. PT-55 (brows flicker) located, not fixed: streaks along the brow and lash line with Ray
  Reconstruction and with none, none with NRD.

- **The egg on the hero's head is gone from mirrors** (PT-46; `docs/agents/mirror-2.md`, ported by
  `docs/agents/merge-3.md`). The game's first-person body keeps the back of a head, an open shell with the face cut out
  for the camera, which HDRP never draws from in front; the tracer showed its inside as a bare egg in every mirror, and
  the 0.3 m rule left its rim as a bowling pin. Mirror rays and bounced light's first ray now pass its back faces at any
  distance (`first_person_inside`); its shadow keeps its head. Bounced light's further rays and rays through glass still
  meet that inside (a gap): the same rule in `trace_closest` hung the GPU on the first frame in the reflections pass
  (RTX 5080, driver 32.0.16.1692; the mirror job's 27 experiment builds narrowed it to that rule there together with the
  rest of its code, and to no loop of ours), so `trace_closest` is unchanged. Offline (`tgpt_gpu_test`): the inside of
  the body behind the camera off by 0.004 to 0.011 in a mirror 0.5 to 3 m ahead, the body seen from in front or drawn
  two-sided off by 0.65 to 0.69. `TGPT_FIRST_PERSON_HEAD` gone (the ABI number is the merge's to set). Not yet seen in
  game. **Not merged from the same job** (built and tested offline in its copy `wt/mirror2` only): its water in
  reflections (`TGPT_SetWaterLook`; built on the water passes that go with PT-60) and its fog in reflections
  (`TGPT_SetFogVolume`; it reads HDRP's own volumetric fog buffer, which holds only what the camera sees: the owner
  ruled that out, the tracer is to light the fog itself, PT-37). So a mirror still shows the height fog's stand-in and
  the lake's bed dry: both are gaps.
- **The tracer draws the water** (PT-48, PT-14, PT-42; `docs/agents/water.md`). The owner, asked whether the tracer
  should take over the water's own look: "of course. Full Path Tracing means full path tracing." Until now HDRP shaded
  its water while tracing: only the reflection was traced, the rest was lit by HDRP's lights with no ambient light, so a
  cave's lake was a dark slab. Now the plugin steps into HDRP's water lighting pass between its lighting and its fog (a
  Harmony transpiler on its render function, checked against the game's own HDRP build) and render event 6 puts the
  tracer's water in place of HDRP's lit water; HDRP's fog still goes over it. Per pixel, as HDRP's water composes it:
  the traced reflection by the water's Fresnel term (HDRP's f0 0.0204 at HDRP's normal); what lies under the surface,
  the camera's ray bent into the water by HDRP's normal (index 1.333) and traced to the bed (its direct light taking
  HDRP's caustic pattern, its bounced light, its color HDRP's GBuffer's), dimmed by HDRP's absorption exp(-2 x
  extinction x the way under water); HDRP's scattering color (the water's body color) and foam lit by the traced light
  on the surface; the sun's and lamps' highlights traced with shadow rays. HDRP's water is the data (its depth and
  normals, scattering color, foam amount, absorption distance and refraction color), the light the tracer's. A new pass
  (`pt_water_under.hlsl`) traces the light under and on the surface in the same submission as the reflections,
  accumulated over 12 frames like them. Kept HDRP's: pixels seen from under the water (and HDRP's underwater fog),
  pixels the traced frame did not cover yet, and everything with path tracing off or the Water Reflections row off. Left
  out, written down: HDRP's extra scattering of direct light through wave tips (its phase term). New:
  TGPT_SetWaterShade, TGPT_WaterShade (568 B), TGPT_EVENT_WATER_SHADE 6 (the ABI number is the merge's to set). Offline
  (tgpt_gpu_test, a lake 30 cm over a green bed under the open sky): every water pixel shaded and no other; the
  reflection's Fresnel term within 1.5 % at every pixel, over 0.3 at grazing angles; the bed dimmed by exp(-2 x
  extinction x the refracted way) within 3 % at 99.9 % of pixels (none matches the straight way); a lamp over the water
  lights the bed seen through it where the refracted rays meet it (mean 0.994 of the worked-out light); the scattering
  color lit by the surface's light (0.999); the sun's highlight where the water mirrors it. Each check mutation-checked.
  GPU cost: the water pass about doubles (0.41 to 0.81 ms over the test's lake at 1280x720, water on 59 % of the
  picture). Not seen in game.
- **Every lamp's light off the water counts** (PT-14; the owner, 2026-10-07: "2 lamps per pixel in water caustics, also
  drop that restriction"). The lamplight reflected off HDRP's water onto walls (`water_reflected_lamps`) took the two
  lamps a pixel that lit it most, and at most 64 of a cell's lamps. Now all of them count: the two strongest are traced
  as before, and of all the others one, picked in proportion to its unshadowed light, stands for them, its light divided
  by its chance: the right total on average, its noise the weak lamps' alone (none with three lamps or fewer). Offline:
  three spots over the pool give the wall 0.99999 of the sum of each alone, four spots 0.99999 (the two strongest alone
  gave 0.776); both mutation-checked. Cost: a second walk over the lamps of a cell and one more lamp's rays a pixel
  above water; with 32 lamps over the test's pool too noisy to measure while other tests shared the GPU (the composite
  0.15 to 1.31 ms with it, 0.16 to 0.37 ms without).
- **No fixed limit decides what the traced picture comes to** (PT-50; the owner: "There is no budgets or limits there,
  this is PT", "dont do shortcuts, we want realism and correctness, and then we will optimize from there", "we should
  probably loosen them up"; `docs/agents/caps.md` has the table, cap by cap, with its cost). The game reached four of
  the caps in the harness's own scenes: 985 lights of 1,024, the 16,384 glowing triangles (36 submeshes over), 64
  character renderers of 64, 96 swaying cloths of 96.
  (1) Lights: a lamp no longer stops at its reach (50 lux, 40 m at most). Every lamp the plugin keeps falls off with
  distance alone (`TGPT_LIGHT_NO_RANGE`, a new light flag); its reach only says how far the light grid lists it as a near
  light. Past that one more candidate a sample is drawn from a far list (every such light, by its power over its
  distance from a 32 m block of the grid; outside the grid, every light by its power), and counts only a light the
  point's own cell does not list, so each light is counted once wherever the point is. An effect's light keeps HDRP's
  window at its range (the portal's two lights). Every loaded light is sent, 65,536 at most (was 1,024, within Scene
  Radius plus reach). The far lists are kept from one light grid to the next and made anew only when a light came, went,
  changed its kind or moved 4 m: made with every grid (a quarter of the game's frames) they cost the render thread 9 ms
  with 985 lights.
  (2) What is traced: grass and plants as far as Leshy draws them (was 20 m), characters within Scene Radius, 256
  renderers and 2M vertices (was 100 m, 64 and 524,288), every swaying cloth in the traced scene in its wind pose (was
  96 within 60 m), the hair layer and HDRP's forward-drawn surfaces at any distance (was 60 m), 262,144 glowing triangles
  as light candidates (was 16,384), 1,024 alpha tiles (was 256, past which straw and leaves tested alpha from a 64x64
  tile), 64 glass walls along a ray (was 8).
  (3) Light that was cut to stop flicker: the three radiance clamps are gone (48, 8 and 4 times the scene's reference
  for direct light, reflections and bounced light), and so are the 25 cm floor on a lamp's distance at a path's vertex
  and the cap on a caustic's focus (16 times the straight light). What NRD's half floats cannot hold (over 500 times the
  median) is handed to NRD at its most and the rest added back in the composite, not cut. In a night lit by eight
  candles on the floor (a new scene in the test's denoiser study) the clamps and the floor took 15 % of the whole
  picture's light (0.1247 against 0.1472 undenoised); now the old option changes nothing (0.1469 and 0.1472). The cost
  there is flicker: the boil of an 8x8 block from 0.23 to 0.36 of an 8-bit level with the built-in denoiser, 0.22 to
  0.31 with NRD; in the sun's room and the torch-lit room nothing changed to the third digit. Ray Reconstruction is not
  run offline: the harness's `tavern-room` and `glow` say what it makes of it.
  Kept, each with a log line that says when a scene meets it: 4 water surfaces, 8 submeshes a character, 256 light
  cookies; and the glass index 1.5 (HDRP Lit's default; the plugin logs each glass material's own). Leaves' and thin
  cloth's fixed transmission shares are the subsurface scattering agent's job. Memory: 96 MB more up front (the skin
  texture 64 MB, was 16; the alpha atlas 64 MB, was 16), and about 5.7 MB of far lists with 985 lights.
  `TGPT_OPTION_NO_CLAMPS` does nothing now (its bit stays taken); `TGPT_MAX_SKINNED` is 256; no option or material bit.
- **Hair stays solid at the top of a head, its strands stay apart at a slant, and each strand takes the light by the
  game's normal map** (PT-01; `docs/agents/strands.md`). (1) The owner's npc-day run `20261007-081218` ("why is hair
  fogged out in the top here?"): above the skull of a head seen from the side, about 60 % of the hillside showed through
  the hair (the hair's share of a box there 0.35 with Ray Reconstruction, 0.40 with NRD; the game's solid). A camera ray
  there grazes through many thin cards, and the hair layer stopped after 8 with nothing after them; the game blends every
  card in front of its depth prepass. The owner: "There is no budgets or limits there, this is PT". Now the first 8 cards
  are lit one by one as before and the ray goes on past them, however many there are, stopping at a card by chance as
  much as its texel covers (as hair stops every other ray), so the picture averages to the game's blend; the 8 only sets
  the speed. (2) Hair's strands were read at the geometric mean of a pixel's footprint, one or two mip levels blurrier
  than the game where a card turns from the camera (the crown, a head's top seen from the side): the game forces
  anisotropic filtering on every texture (its Ultra quality setting, read from `globalgamemanagers`). The strands are read
  anisotropically now, up to 9 samples along the footprint's long side at its short side's level. (3) RealHair lights hair
  by Lambert about its normal-mapped normal (`_NormalMap` at `_NormalStrength`, read from its compiled forward pass) and
  shifts the highlights about it; the tracer lit every strand of a card alike. Now each texel's normal from the game's own
  normal map (shared, as the other materials' are) leans the strand's light the way the game's rule does, averaging to 1
  over a strand, and the highlights are shifted and lit about it. Also found: the "crown" box the last rounds measured
  (0.465, 0.36 to 0.53, 0.41) is the wall above Ron's head, not his hair. Harness: sixteen cards of 0.2 before a bright
  wall let 0.028 of it through (the game's blend 0.028; eight and no more 0.168, mutant 0.173); strands on a card
  turned 80 degrees spread 0.43 (blurred into a veil 0.12); normal-mapped stripes toward a lamp 7.2 times
  as light as those away (without the map 1.11, the lamp's falloff). No ABI change, no new option or material bit.
- **Mirrors as sharp as the camera's own view** (PT-46; the owner: "these mirrors still doesn't look like full
  resolution, are they?"; `docs/agents/sharp.md`). In the harness's tavern mirror the fine detail was 0.36 of the
  tracer's own view from the mirror's side (Ray Reconstruction; 0.23 with no denoiser), at the same render size. The cause
  was ours: a reflection ray was drawn from GGX's lobe at a roughness of at least 0.045 (`roughness_alpha`'s floor, which
  is there to bound a highlight's peak), so a perfect mirror's rays strayed by about 0.004 radians, some four pixels'
  angle at the game's 1924x1084, and the denoiser averaged the strays into a blur; and that lobe widened the ray's cone
  three times as fast as a pixel's, so the mirror read its textures at a coarser mip than the camera does. Now a reflection
  is drawn from the surface's own lobe down to a mirror's (`reflection_alpha`, floor 0.01: a tenth of a pixel), in
  `pt_specular` and for the reflection inside a reflection (`shade_seen`), and its cone is the camera pixel's carried over
  the whole way. Offline (`tgpt_gpu_test`: a checker of 8-texel squares in a perfect mirror against the camera's view of it
  from the mirror's place, no denoiser): the mip read 1.71 before against 1.01 seen directly, now 1.00; fine detail 0.35
  of the direct view's, now 0.96 (the strays alone put back: 0.40; the wide cone alone: 0.75). Surfaces rougher than
  0.045 are traced as before. Reflection Boost never roughens a
  surface any more (a mirror at the boost's 1 became 0.05 rough: mip 1.86, detail 0.27; now 1.02 and 0.96), in the tracer
  and in Ray Reconstruction's guides; its rule for stone and metal (the owner's, 2026-10-06) is unchanged. Ray
  Reconstruction's hit distance is measured from the mirror's surface, as its guide asks (3.4.9: the ray's origin "on the
  Primary Surface"), along the ray to the plane of what it met: from the ray's start 2 cm off the surface, a ceiling 6 m
  over a floor mirror came out 5.984 m. For the harness: a hidden "Reflections read the top mip" (option 134217728) and the
  recipe `mirror-sharp`, which takes the mirror with it, with no denoiser and with Reflection Boost 0; `mirror.py` adds
  those steps to its table against the traced view. Not yet seen in game.
- **Glass jars show the wall behind them, as the game blends them** (PT-35, PT-13; `docs/agents/jars.md`). On the
  owner's save "PT refract-jar" the Crypt's two jars were a flat milky brown where the game shows greenish glass with
  the stones behind it. They were taken for see-through glass and seen through all along (the run's log, HDRP's depth
  behind them, the jar mesh's four crossings), but what the view through them ended on was the tracer's own copy of the
  wall, a 64x64 tile with no normal map: no stones. Now (1) the surface a see-through view ends on is shaded with HDRP's
  own GBuffer color and normal wherever it is the surface HDRP drew at that pixel (`hdrp_material_at`), as the pixels
  around the glass are; (2) the glass is blended as HDRP blends it (the jar material's blend mode Alpha with its specular
  kept): its alpha's share of its color lit by the light gathered at it, the rest of what is behind let through
  untinted, instead of a filter that multiplied what is behind by its color (the jar's texture is alpha 0.165 to 0.18 on
  the glass and 1 on the lid, so the lid was brown glass the wall showed through, not wood); (3) the glass is crossed
  wall by wall (`see_through_shell`: into a wall at a front face, out at its back face within 3 cm, bent in and out by
  the smooth normals; each further front face lays its cover over what is behind), so a thin jar bends what is behind it
  only near its outline. Offline, a copy of the Crypt's jar (27 cm, 5 mm walls, its material and texture alpha) 5 cm in
  front of a wall HDRP draws checkered and the tracer plain: the checker comes through it column for column (0.97),
  at 0.71 of the bare wall's light; where its texture covers whole it does not (-0.08). A clear test pane now has alpha
  0 (alpha 1 is opaque color to HDRP). A log line per glass material says whether rays take it for see-through glass,
  with its alpha and how its texture is read, or why not. Not seen in game yet.
- **NRD keeps the normal maps in the bounced light, and its units follow the light it is handed** (PT-19; the owner:
  "the NRD denoiser is a LOT more noisy than Ray Reconstruction ... ideally we also make NRD look great";
  `docs/agents/nrd.md`). Measured in a new part of `tgpt_gpu_test` (NRD's look as the in-game harness measures it: boil,
  flicker, error against an undenoised reference, and how much of that reference's fine detail is kept, against two
  halves of it so their noise cancels; bump-mapped room, the game's defaults, the camera still).
  - Blur: RELAX got the bounced light with the normal map's shading in it, guided by the geometric normal (the shading
    normal made its history fail), so it filtered the shading away with the noise: under the ceiling, lit by bounced light
    alone, NRD kept 0.1 to 1 % of the reference's detail (the built-in denoiser 0.5 to 1 %; in game the shade outdoors is
    bounced sky light). Now RELAX runs in its directional (SH) form: `pt_gi_spatial` hands it the light as the geometric
    normal takes it and the direction it came from (in the signal's alpha, as a 45 x 45 octahedral cell), and
    `pt_nrd_prep` puts the normal map's shading back after, as NRD's filtered direction gives it (NRD_SH_ResolveDiffuse's
    irradiance for the shading normal over that for the geometric one, held to [0, 2]). Detail kept under the ceiling
    0.35 to 0.41, over the whole picture 0.27 -> 0.38 by day and 0.10 -> 0.25 at night. Boil about as before, and about
    the built-in denoiser's (0.17 to 0.21 levels by day, depending on how NRD's history started; 0.26 to 0.28 at
    night); error 3.99 -> 4.06 to 4.28 by day and 6.31 -> 6.34 at night (the linear SH gives bumps a little less
    contrast than the reference). Where the median sits in NRD's units (1/2, 2 or 8 were tried) changes nothing. About 0.15 ms more at 1280x720 (0.3 to 0.5 ms at a 1080p render size, estimated) and about
    100 MB more video memory at 1080p while NRD runs (RELAX's SH form: 158 MB against 91 in NRD's README; two more
    render-size textures).
  - Units: NRD was handed light divided by the scene's reference (the brightest light 1 m away, or the sun). RELAX keeps
    the light's square in half floats (NRD's README: inputs in [0, 250]), so a room lit far below that reference lost its
    variance: with a light 40,000 times the brightest far away (which changes nothing in the room), NRD's boil went 0.28
    -> 0.49 levels and its error 6.3 -> 37. NRD's 1 now stands for twice the bounced light's median, read back from a
    histogram `pt_nrd_prep` fills (the meter's buffer, bins 64 to 127): 0.30 and 19 there (the rest of that error is
    the tracer's own storage at that ratio; the built-in denoiser's is 69). NRD's history starts over when that median
    leaves 1/16 to 2 of the scale (before: when the reference moved 4x); light above 250 of it is held there.
  - Defaults: none changed. More rays a pixel bought NRD little: 4 rays cut the night's boil 8 % (0.273 -> 0.252) and
    raised the day's (0.178 -> 0.198), for 1.2 ms here and, by the harness's stage times (bounced light 8 to 16 ms a
    ray at 1 ray), some 25 to 45 ms in game; 2 rays and 8 spatial samples changed nothing measurable.
  - Checked: the inputs against NRD's README (RELAX is its pick for RTXDI; no pre-pass and no hit distance
    reconstruction is right for a signal with a sample in every pixel; RELAX uses no diffuse hit distance without its
    pre-pass; the jitter only sizes its disocclusion slack). 2D motion stays (2.5D needs previous positions of moving
    things). The new checks fail on the code before (detail under the ceiling 0.009 and 0.001; the far light's boil
    0.49 against 0.28) and with both fixes undone on purpose. One older check changed: NRD's flicker against the
    built-in denoiser's is no longer compared on the bump-mapped moving case, where NRD now shows the normal map's
    shading and the built-in denoiser does not: with the camera's jitter that shading changes from frame to frame
    as the game's own normal-mapped lighting does before its upscaler (0.030 moving, 0.038 still and jittered, the
    same at 8 rays a pixel, 0.003 without the shading put back); its error is still compared there.
- **The game's fog along the tracer's own rays: a mirror, water, a glossy surface and glass show far things in the fog**
  (PT-37 step 1, PT-46; the owner: "fog shouldn't go away just because people activate PT", "the tracer should light that
  fog too"; `docs/agents/fog.md`). HDRP fogs the camera's way to a mirror, but the way on from the mirror to what it
  reflects is the tracer's own ray, and it got no fog: in the harness's perfect mirror by day far things came out 0.14 to
  0.23 of their brightness against the game's view from the mirror's side, dark and brown where the game shows them grey
  in the mist. A reflected or refracted ray (`pt_specular`: the mirror, every glossy reflection, out of solid glass;
  `pt_water`) now gets the fog as HDRP fogs a view from the ray's origin (`pt_common` `fogged`): what it met dimmed by
  the height fog's transmittance over the ray's own way (the sky as a surface at the Max Fog Distance), and the fog's
  own light for what the fog hides: the Fog volume's constant color, or its tint times HDRP's sky in the ray's direction
  at the mip HDRP's mip fog picks (`fog_sky`: the sky cubemap's mips 3 to 6, resampled with the sky every 30 frames),
  times its albedo while its volumetric fog is on, at the game's exposure. The plugin sends the fog's color and mip fog
  (`TGPT_Frame` 568 -> 600 bytes: `fogColor`, `fogMip`; the ABI number goes up at the merge), the shaders' constants grow
  by two rows (6128 -> 6160 bytes). The fog's light along the ray is HDRP's distant fog everywhere: its volumetric
  light (the sun's glow through the mist, local fog volumes) is not on a reflected ray. Offline (tgpt_gpu_test): a
  perfect mirror showing a wall 3 m off in thick height fog gives the colour HDRP's formula gives for a view of the wall
  from the mirror's place, within 5 % per channel, for a constant color and for the Sky colour mode; without the fog
  on the ray, or without the sky in the fog's color, the check fails. Not seen in game yet.

- **A mirror shows what it reflects lit as the camera sees it; the hero's stand-in head is gone from mirrors** (PT-46; the
  owner: "we need our tracer to be able to perfectly render the world in a mirror"; `docs/agents/mirror.md`). What a
  reflection's ray met was lit as a bounce's path vertex: matte, its base color (a metal's too) as diffuse light, no
  highlight, its bounced light without Indirect Color. It is now lit as the camera's own pixel of it is (`shade_seen`):
  its diffuse color and its specular lobe (Reflection Boost as on screen) take the sun's and the lights' samples
  (`direct_light`: `direct_diffuse` with a GGX lobe), its bounced light takes Indirect Color (`indirect_color`, now shared
  with `pt_gi_spatial`), a smooth metal traces its own reflection, the rest reflect the radiance cache's light by the
  lobe's albedo. Off a mirror-smooth surface (not skin) a reflection sees eyes and unlit decals, as the camera does.
  Offline, a perfect floor mirror showing the lit red wall against the wall seen from the camera's mirror image, pixel for
  pixel: as it was, brightness 0.864, green/red 0.703, blue/red 0.610 of the wall's (too dark and too colored: the
  wall's highlights and reflections, the light's color, were missing); now 0.982, 0.989, 0.981 at Indirect Color 1; at
  3, 1.022, 0.934 and 0.863 (the camera's pixel takes Indirect Color per noisy sample, the mirror's on the cache's
  mean; as it was, 0.794, 0.825, 0.750). Each part mutation-checked. Ray Reconstruction's guides at
  the mirror a mirror's (specular albedo 0.999, roughness 0, hit distance the way to the wall within 5 % at 99.9 %). In
  the game's tavern mirror the strong colors were mostly the traced tavern's own (saturation x1.26 outside the mirror,
  x1.29 inside, against the game's picture); the harness now also takes the tracer's own view from the mirror's side
  (step `2b-mirror-reference-traced`, a second table in `mirror.py`). Not changed: HDRP fogs a mirror at its own
  distance, not along the way to what it shows (by day far things in the mirror 4 to 7 times too dark, PT-37), and its
  depth of field blurs the mirror at its own depth. The head: in first person the game loads no head (its first-person
  body, `VHeroController.fppParent`, ends in a faceless stand-in around the camera, which its camera never draws); the
  plugin marks that body's skin (`TGPT_MATERIAL_FIRST_PERSON`, material bit 1048576), and reflections pass it within
  0.3 m of the camera (`first_person_head`): the mirror shows the body without a head, as the game's own view from the
  mirror's side does; its shadow stays. Offline: a box around the camera in the mirror under it, off the no-box picture
  by 0.007 with the flag, 0.759 without. Two older checks follow the new rule: the mask map's metallic half in a
  reflection is lit as a metal (0.459 of the plain half's light, not the plain half's diffuse light), and Reflection
  Boost moves the skin floor's reflection by 2 % (what it reflects takes the boost; the check allows 4 %).
- **Texture sharing tests itself on the player's card first, hair and eye maps follow mip streaming, and a video memory
  cap** (PT-45 phase 3; the owner: texture sharing is "the number one most important thing right now";
  `docs/agents/share-3.md`). (1) No vendor promises the legacy sharing flag on compressed textures (Microsoft: it "might
  vary from driver to driver"; NVIDIA and AMD were tested bit-exact, Intel never). So the hook no longer goes in with the
  game's device: a self-test on a thread of its own first makes one texture of each format the game uses (BC1 to BC7,
  RGBA8 and BGRA8, A8, R8, RG8, R16, RG16F, RGBA16F, RGBA32F, sRGB forms; BC1 sRGB and BC3 also at 2048x1024) shareable on
  a D3D11 device of its own on the game's GPU, opens each on the shared D3D12 device by its legacy handle behind the
  tracer's fence handoff, and compares every byte of every mip copied back and every texel read through the very view
  the registry makes (`pt_share_test.hlsl`, against a texture D3D12 made from the same bytes). The hook then flags only
  textures in a format that passed; a format that failed keeps its atlas copies (`TGPT_SHARE_SELF_TEST`); none passing
  keeps the hook out. Arrays and cube maps no longer get the flag (the registry never opened them). One line in the
  native log names each format's verdict. RTX 5080: 24 of 24 cases, 22 formats, in 82 to 168 ms on its thread (most of it
  making its D3D11 device and pipeline), 0.03 to 0.06 ms on the game's; the passes still made in 0.6 s. With BC7 sRGB made
  to read one wrong byte (`TGPT_TEST_SHARE_BREAK=99`, the new ctest `tgpt_share_selftest_fails_one`): that format alone
  failed, its texture got no flag and kept its atlas tile, the hook went in for the other 21. (2) Hair's and eyes' maps
  (RealHair's colour and flow maps, the sclera and iris) are watched for mip streaming like material textures: a map made
  anew is registered and `TGPT_SwapSharedTexture` points every map record baked from the old texture, and every bake
  still waiting, at the new one; before, the record's number died with the old texture and hair and eyes fell back to
  their tiles. (3) A video memory cap: Windows counts a texture the tracer opened a second time (opening a 2048x2048 BC3
  moved the count by +5.38 MB at once), and the registry now opens a texture only while that count stays under 90 % of
  the budget Windows gives the game (`QueryVideoMemoryInfo`), and only if it takes at most a quarter of what is left:
  near the budget the largest stop first. Kept-back textures (`TGPT_SHARE_BUDGET`) keep their atlas tiles and are asked
  for again every 10 s; one native log line every 30 s at most. Offline at a limit 16 MB above the count: a 2048 BC3 kept
  back, a 256 BC3 opened; under the count: nothing opened; no limit: both opened when asked again.

- **A face's lids, brow and nose shadow its eyes and sockets; skin keeps its own ambient occlusion** (PT-43, PT-44; the
  owner, of Ron: "In vanilla, you see blushes and details on his cheek, and we see is eyelid casting a shadow or
  whatever. In our rendering there's none of that"; `docs/agents/face.md`). Every ray from the skin and the eyes
  started `ray_offset` out (2 cm + 0.2 % of the distance: 2.3 cm at Ron), past a lid 2-5 mm off the eye and a brow 1-2 cm
  off the socket, and passed every back face within 16 cm: the lids could not shadow the eye, and the sockets saw the
  whole room (Direct view: the crease lit 0.95 of the cheek, the eye 0.88). A character's skin whose own triangle a
  camera ray finds within 4 mm of HDRP's depth (Kandra's skinned positions are the ones HDRP draws that frame), and an
  eye (seen by camera rays alone), are now lit from that triangle: their rays leave 1 mm above it (+0.02 % of the
  distance) and back faces stop them (`surface_offset`, `surface_near_back`). Skin that is not thin also takes its
  material's ambient occlusion (HDRP's, GBuffer3's tag, as a leaf's) off its bounced and sky light: the game's skin
  shader darkens mouth, nostrils and lips so. Harness (an eye and a skin under a lid 1.5 cm tall, the sun over it; Direct
  view 2 to 6 mm in front of the lid over 4 to 8 cm away): eye 1.00 -> 0.00, the lid turned away (its back face) 1.00
  -> 0.00, skin 1.00 -> 0.00; skin out of the sun at occlusion 0.25 keeps 0.25 of its bounced and sky light (before:
  all of it). What the game's own picture has that this does not
  give: its sockets darker by about 0.3 of the cheek's light (0.71 against the traced 0.99) are HDRP's screen-space
  ambient occlusion, which the tavern's volume puts on direct light at 0.75, on a light the game carries with the
  player (4900 K, from the camera: no shadow the camera sees) that the tracer leaves out as a fill light; that light is
  also why the game's face is greyer (the light's blue over green 0.64 against the traced candle's 0.39).

- **Cloth the wind sways is traced in the pose the game draws it, and every cloth is found as cloth** (PT-49, PT-39; the
  owner: "reflections and shadows of moving objects, including cloth, must be accurately represented by our path tracer
  ... we really need to make sure we detect cloth correctly. That also includes clothes hanging on a drier and stuff";
  `docs/agents/cloth.md`). The game's `TG/Clothes/Cloth_Wind` (canopies, banners, laundry) moves its vertices in its
  vertex shader, and the tracer kept them still, so a canopy's pixels, shadow and reflection were of another pose. Read
  from the game's compiled shader: Shader Graph's Simple Noise of the texture coordinates (or, by the material's
  `_UseVertexPaint`, the position) scrolled by `Time.time` x `_Speed`, and the vertex moved by `_Intensity` x noise x
  (its object-space position + the world push `uv.y` x `_Wind` x `_Direction`), masked by twice its vertex color where
  `_UseWorldSpace` says (the two switches act the other way round from their names). The plugin sends each such
  material's wind and the time with every frame (`TGPT_SetClothWind`); the tracer keeps a copy of each swaying cloth
  within 60 m (96 at most, a million vertices), moves it the same way each frame (`pt_sway.hlsl`), refits its BLAS and
  traces it as a still object. Offline, against a pose made from the compiled shader's maths on the CPU: a hanging sheet's
  traced outline lies 0.001 and 0.002 pixels (0.00 mm) from its drawn one at two times, 112 mm traced still; a canopy's
  shadow is the shadow of the canopy posed on the CPU (mean difference 0.0000 against the still one's 0.2523); a red
  canopy seen from below takes 0.27 of the sky's light through it (0.30 expected; 0.02 traced still), red in the picture.
  With the wind's direction swapped the outline is 7 to 8 mm off; without the refit 25 mm at the second time. Thin cloth is now found by what it is (`ClothRules`): the whole
  `TG/Clothes/` family, a hanging sheet's name (tent, banner, curtain, canvas, laundry, ...), or a cloth word (sheet,
  towel, rag, tablecloth, ...) on a material drawn from both sides; floors, rugs, sacks, fur, nets and stone drapery stay
  solid, characters' clothes as they were. Over the census of the harness's scenes: 74 thin cloth materials, was 62
  (laundry sheets, towels, rags and tablecloths found; a statue's stone cloth, a shrouded body and the loading screen's
  canvas no longer). And the Surfaces debug view never showed the tracer's cloth (it paints HDRP's own material type, and
  the game's cloth is HDRP's standard one: the canopy was grey either way); it now paints thin cloth purple.
- **Fog Adaptation lifts the fog only as far as the traced light** (PT-48; the owner, watching the mineral cave's save
  turned round to its lake, `glow-lake` in run `20261006-225619`: "That looked weird"; `docs/agents/lake.md`). Traced, a
  blue haze hid the far half of the picture, the lake was a dark slab and the grass blades flat light shapes. All three
  were the game's own fog at 5.8 times its brightness. The cave's lights are fill lights the tracer leaves out
  (`Point Light_17B` x31, the crystals' `Point Light` x39: PT-47), and with Fill Light Fog they still light the fog, so
  the fog came out 5 stops over the traced light under it (the view's median log2 -8.1 at the game's exposure, the
  traced light's -13.2). Dark Adaptation brightened the view from the fog's level (x5.78), and Fog Adaptation put that
  gain back on the fog: the far rocks' region 32 levels in the game, 85 traced. HDRP draws its water after Fog
  Adaptation's pass (`RenderTransparency`: the BeforePreRefraction custom passes, then `RenderWaterLighting`), so the fog
  in front of the lake stayed at x1 (10 levels, the game's 22) inside a haze of 40 to 85, and grass blades standing out
  of it, with almost no traced light, were the x5.8 fog alone (40 to 50 levels). Now Fog Adaptation's pass also measures
  the fog's light over the traced light it veils (a histogram of the stops per surface pixel, before the gain; read back
  without waiting, its median eased over half a second), and the fog takes the traced light's automatic gain shrunk by
  those stops: never more than lifts it to the brightened surfaces, never less than the game's fog. The traced light's
  gain fades behind fog to the fog's (`traced_gain_at`), as it fades to 1 with Fog Adaptation off. Where the traced
  light makes up the view (a lit room, any day) nothing changes. In the cave the fog stays the game's, and Dark
  Adaptation, now seeing that fog at x1, brightens the traced light further (to about where it meets the fog). No ABI
  change; FrameConstants `textures.z` (unused) carries the fog's gain. Offline harness: a fog 50 times as bright as the
  dim one keeps the game's brightness (x1.000 on all 921,600 pixels after 4 frames, and still after more), a faint fog
  over the lit scene then takes the whole x2 again (x2.000 after 13 frames, eased); the old checks (x2 over surfaces
  and, with HDRP's maximum fog distance, over the sky) pass as before; 778 checks pass. Broken on purpose: the
  measurement ignored, the bright fog stays x1.999 after 90 frames and its check fails; the reading never easing back,
  the faint fog stays x1.000 and its check fails. Not seen in game yet.

- **Rays read the game's own textures: color, normal and mask maps, hair's and eyes' maps** (PT-45 phase 2, PT-46; the
  owner: "if we can share textures, we immediately improve hair and reflections"; the rulings: "a mirror must show the
  world as the camera sees it", textures "at the same res as the res the game renders at"; this step:
  `docs/agents/share-2.md`). The mirror harness's first run had the traced mirror's textures blocky and blurred (fine
  detail 0.37 of the game's own picture), with no normal maps. (1) `TGPT_Material` names its color, normal and mask
  maps by handle (ABI 1.18, 160 bytes); the scene turns the handles into the textures' numbers before every upload, so
  a texture that died, or a number given to another since, reads as before: its atlas tile. A dead texture is freed
  only after the first frame that no longer names it (phase 1 freed it while a later frame could still read it). (2)
  Ray cones: each ray carries its width (a camera ray one pixel's, a reflection's the pixel's at the mirror growing on,
  a diffuse bounce's wide), and at a hit the texture is read over that footprint with an anisotropic sampler, at the
  upscaler's mip bias, as the game samples it. (3) The color through the same maths as the atlas (base color, vertex
  color, HDRP Lit's metallic split); the mask map texel by texel (its green, HDRP Lit's ambient occlusion, taken off
  the bounced light alone, as HDRP does); the normal map (Unity's packing, its strength) in the tangent frame of the
  mesh's smooth normal and the triangle's UV directions, lighting path vertices and what mirrors show. (4) Hair's
  RealHair color, its strands' flow, and the eye's sclera and iris from their own maps at full size (the bakes' maths,
  per hit), the 512 detail tiles and lobe tiles behind. (5) From phase 1's runs in game: the hook goes in with the
  game's device whatever the Mode row says (485 textures were "made before the hook" when tracing was turned on after
  loading); only textures of 64 KB or more with their mips are shared (a smaller one rounds up to 64 KB when shared);
  and a texture mip streaming makes anew (12 of 12 in the harness) is registered again and its materials sent again.
  Offline (tgpt_gpu_test, a BC7 panel of 128 by 64 cells in a perfect mirror): the mirror agrees with the cells 0.713
  (the 64x64 atlas tile: 0.001, grey) at the same mean light; tiled 16 times, no aliasing (spread 0.190, the grey
  tile's 0.190); a normal map tilted toward a lamp 0.2506, flat 0.1435, away 0.0575; no number or a dead texture reads
  the tile (off by 1.6e-4). Each new check mutation-checked (`share-2.md`). Source check
  `test_path_tracing_shares_the_games_textures` extended. Not yet seen in game.
- **A candle's light is traced at its flame, and a "torch" light with no torch is left out** (PT-47, PT-34; the owner,
  watching the tavern: "it looks like the candle lights glow too strongly/overblown now, some of them", and of the
  mineral cave: "Why are there like 4 of these yellow lights in the water ... I only see one torch in the scene";
  `docs/agents/lights-2.md`). The tavern's candles have their big light (21,149 to 24,775 cd, HDRP radius 1 m) inside the
  wax, 10 cm under the flame (projected into the harness frames: the flame's brightest rows 9 to 11 cm above it). HDRP
  softens a light near it by its radius, I / (d^2 + r^2) (`ModifyDistancesForFillLighting`; its 1 cm clamp,
  `PUNCTUAL_LIGHT_THRESHOLD`, never mattered here), so the wax 3 cm away got about I. Traced at the flame's 1.2 to 3 cm
  since round 1 it got I / d^2, 1,100 times that (100 times what the tracer's 30 cm ball gave it before), through the
  wax's thin diffusion profile from behind, and nothing in the wax blocked it (a surface's rays pass its own mesh's back
  faces within 15 cm, and stop 5 cm and the surface's 2 cm offset short of a light): the whole candle white-hot, up to
  the direct-light clamp. Now a flame's or a lamp's light (candle, torch, lantern, brazier, fire, ember,
  a flame by its look) stands at the centre of its measured flame effect or glass where that is compact (no side more than
  25 cm from its centre, 50 cm for fires), in the light's own space, and carries `TGPT_LIGHT_AT_EMITTER`: its shadow rays
  are tested up to its own size and 1 cm (`light_own_reach`), without the surface's 2 cm ray offset taken off the end as
  well. And a surface lit through from behind passes back faces on the way to a lamp within 1 cm only
  (`near_back_towards`), not the 15 cm every surface's rays pass its own mesh by: a thin thing's thickness, not a
  candle's. So a candle's top shadows its sides; only its top centimetre still takes the flame's light, as a real
  candle's top glows. Effects, glowing things and lava keep the game's place. A hidden key, "Lights sized as their
  emitters" (on), traces every light at HDRP's place and radius when off, to compare (recipe `lights`). And a light kept
  by a flame's or a lamp's name needs its flame, lamp or glowing thing within 1 m (`LightRules.EmitterAt`), else it is a
  fill light: the mineral cave's torch groups hold one torch each and 3 to 6 warm "Point Light_NN" 3 to 8 m from it, whose
  reflections were the yellow dots; each such light is
  logged with what is nearest it. Offline (`tgpt_gpu_test` 2b, a 2 cd light 3 cm over a wax column 5 x 12 x 5 cm drawn
  with transmission and traced as a closed box, Direct view): the top of the wax takes 105.6 against 107.5 worked by
  hand (the solid angle it fills over its area); the side 2.22 where the light in the wax gave 37.2 and the light over
  the top without the flag 5.20 (5.24 by hand), and below its top 2 cm 0.43 against 2.78. With either rule undone or the
  offset taken off again, the side check fails. `LightRules.Tests`: the torch group, the flame centre, smoke and effects;
  each of 4 mutations fails it. Not seen in game yet.

- **Ray Reconstruction is told what hair, brows and eyes are** (PT-01, focus 2; the owner, of Ron: "it still seems
  blurry/low-res and not as finely detailed as the vanilla game, including his brows and eyes"; `docs/agents/ron.md`).
  Its guides (the pictures of each pixel's colour, mirror share, facing and roughness that it denoises by) came from
  HDRP's GBuffer alone. Under hair drawn as a layer the GBuffer holds the skin or the wall behind it (the plugin hides
  HDRP's own hair passes), and an eye, which HDRP draws forward, had a grey albedo of 0.5 there. Ray Reconstruction took
  the strands' light for noise on the skin and the iris for noise on grey, and smoothed both: on Ron's crown it kept a
  fine-detail measure of 0.36 against the game's picture where the undenoised one has 0.63 (eyes 0.68 and 0.94, cheek
  0.74 and 0.92). Now the tracer writes its own guides where it knows better (two new 8-bit outputs a frame, about 33 MB
  at 1924x1084): the hair layer its colour, specular albedo (hair's reflectance at its roughness, times its specular
  map's texel), roughness and the strand's normal facing the camera, with its cover (`pt_hair` `layer_guides`); a
  surface drawn forward that the tracer shades (an eye, a held brow card, a prop) its traced colour and finish
  (`pt_compose` `traced_guides`). The composite lays them over the GBuffer's from the texel whose light the pixel takes:
  albedos and roughness blended by the cover, the normal switched where the hair covers more than half (NVIDIA's DLSS-RR
  guide 3.4.1 and 3.4.3: with a hit distance and no specular motion vectors, normals are never blended). Offline
  (`tgpt_gpu_test`, which never runs Ray Reconstruction itself: the guide textures are read back): over a red layer card
  in front of a grey wall the diffuse albedo guide is the card's red where its strands cover the pixel (0.800 0.051
  0.039, was the wall's grey) and differs from the albedo view's blend of the two by 0.001 on average; its roughness
  guide 0.10 there, the bare wall's kept; on a blue eye drawn forward the guide is its own 0.098 0.302 0.702 (was 0.5
  grey) and its roughness 0.10 (was the box's 0.6). With the composite not laying them over, or the tracer writing none,
  all five of these checks fail. Not seen in game yet: the next run of `-Scenario hair-eyebrows -Recipe views` should
  raise the three numbers with Ray Reconstruction toward the undenoised ones.
- **Glowing surfaces glow in the traced light at last, and a light the game placed at a glowing thing is traced as that
  thing's light** (PT-47, PT-34; the owner: "make sure we only drop those fake area lights ... But emissive things, like
  crystals in a cave, or magic ... of course they should still emit light and appear in reflections";
  `docs/agents/lights.md`). Two mistakes of ours made the mineral cave orange. First, no glowing surface of the game was
  ever read: the scene capture took HDRP's lightmap flag EmissiveIsBlack for "no glow", but HDRP writes it whenever a
  material's Baked Emission toggle is off (its default) and adds `_EmissiveColor` all the same; every harness run's
  native log says "emissive: 0 submeshes" (39,669 lines, every scene), and no "Emissive material" line was ever logged.
  HDRP's own shaders are now read whatever that flag says (others keep its word), and the log says "Baked Emission off
  (read all the same)", or names a material with a glow map but no glow color. Second, the classifier dropped every
  light whose names say no flame, lamp or effect, so the crystals' own blue lights (a "Point Light" in each
  `Prefab_Prop_ForeCrystalMold_Lowpoly_01`, 13,416 K, 185,364 cd) went with the cave's fill lights. Now a light the names
  leave out is kept where a glowing thing stands within 1 m of it (`LightRules.GlowAt`): a traced emissive surface, an
  effect drawn glowing (particles or a visual effect whose material adds its light, glows by its own colors or is named
  for fire or light) or an emissive decal (the stone circles' red runes), at most 6 m across (larger is a wall or a volume
  of air), and not one a flame's or a lamp's own light already stands at (a candle's extra lights stay out). It keeps
  HDRP's color, strength and range, as an effect's light does; once kept it stays kept while its parent is the same
  (particles come and go). Its glowing surface lights nothing itself (`TGPT_MATERIAL_OWN_SURFACE` 131072 on that
  instance's emissive materials: pt_emissive marks it EMITTER_PAIRED, scene.cpp never samples it), nor does a glowing
  surface within 1 m of a flame's or a lamp's light (a lantern's glass, a brazier's embers), so no light is counted twice;
  reflections and the camera still see them glow. Lights with nothing glowing at them (the cave's white and warm "Point
  Light_17B" fills, the tavern's "Point Light_N", the sky-shaft "Spot Light"s, HeroLight, Volumetric) stay out. The log
  names each left-out and each kept-for-a-glow light once per name with what is at it, and the inventory marks the kept
  ones "+"; the harness's material audit adds each `_EmissiveColor` and lightmap flag. Checks: `tests/light_rules_tests.cs`
  (a light 0.4 m from a crystal kept, a lone fill light and one 4 m off dropped, a 20 m glowing wall and a candle's
  claimed flame keep none; each check fails with its rule undone, 6 mutations); GPU, a glowing quad marked as a light's
  own surface 1.5 m from that light adds no light, row on or off (the room's light 0.01017 as with the quad dark), and
  unmarked it does (0.02989). No new export or ABI.
- **Separately: every traced light is the size of what emits it, for its highlights and its shadows** (the owner, asked
  whether lamps get their real size for highlights only or shadows too: "whatever is most realistic"). The tracer drew
  each light as a ball of HDRP's light radius, an artist's value for soft shadows (0.25 to 1.5 m for the cave's lamps),
  so a lamp's highlight in a puddle was a long soft smear where HDRP draws a point (`docs/agents/caustics.md`). Now the
  size comes from the emitting thing at the light (the flame's particles, a lantern's glowing glass, the crystal it was
  kept for: the middle of its bounds' half extents, held to its kind's range) or, where none is found, its kind: a
  candle's flame 1.2 cm across, a torch's 8 cm, a lantern's glass 10 cm, a brazier 30 cm, a bonfire 50 cm, lava 1 m, an
  effect or a glowing thing 20 cm (`LightRules.DefaultRadius`). HDRP's radius is kept only for a mod's own light (the
  dialogue light). Shadows of candles and torches get sharp, highlights small; the light a kept light's own surface is
  found by (its radius and 25 cm) shrinks with it, which the 1 m claim above covers. The log names the traced sizes by
  kind once a session. Judge it on its own: in `SceneLights.Size` alone.
- **The tracer opens the game's own textures, with no copy** (PT-45 phase 1; the owner: "the number one most important
  thing right now, because if we can share textures, we immediately improve hair and reflections"; the probe:
  `docs/agents/texture-share.md`; this step: `docs/agents/share-1.md`). Nothing draws with them yet: rays still read
  the 64x64 atlas tiles, and phase 2 moves the shaders over. (1) A hook on the game's device (CreateTexture2D, vtable
  slot 5, and CreateTexture2D1, slot 54, which bypasses it) adds the old sharing flag, `D3D11_RESOURCE_MISC_SHARED`,
  to every texture made only for shaders to read and larger than 64x64 (the NT-handle flag refuses BC4 to BC7 and
  cube maps). If the driver refuses the flag, the texture is made again exactly as the game asked: the game never
  sees a failure. It goes in with the game's device unless Mode was Off at launch (read from the config file at the
  bridge's bootstrap); then it waits for path tracing's first texture, so it costs nothing while path tracing is
  unused. It is never taken out (a loading thread could be inside it). (2) `TGPT_RegisterSharedTexture` (ABI 1.17)
  opens such a texture on the shared D3D12 device by its handle, writes a view over all its mips and gives it a
  number (16384 of them, a range of the tracer's descriptor heap). A small object in the texture's private data tells
  the registry when the game frees it, and the D3D12 side goes once the GPU is done (without it, D3D12 keeps the
  memory alive); a scene clear lets go of them all. (3) The plugin registers each traced material's colour, normal
  and mask textures and characters' maps beside their atlas tiles, and logs per traced scene how many were opened,
  their video memory, and why the others were not. The harness's mirror recipe gets a `mipStreaming` step: whether
  Unity makes a new D3D11 texture when mips load or drop (`mipstreaming.txt`). Offline (tgpt_gpu_test): BC1, BC3
  sRGB, BC5 and BC7 sRGB (through CreateTexture2D1) with all their mips, made through the hook, read through the
  tracer's view bit-exact against D3D11's own reads (1.59 million texels, 0 differ); a freed texture's D3D12 side
  let go of; a 64x64 texture, a render target and an array get no number and keep their atlas tile. Source check
  `test_path_tracing_shares_the_games_textures`.

- **Lamps and torches reflected off water light what faces the water** (PT-14; the owner, of the mineral cave: "build
  caustics from lamps and torches"; `docs/agents/caustics.md`). The walls and roof over a pool, a bridge's underside by
  a torch: the lamp's mirror image in HDRP's water lights them, rippling with HDRP's pattern as the sun's reflection
  does (round 3), which the game never had and the tracer's paths can't find (they would have to meet a point light's
  mirror image exactly). Over level water at height h, a point takes from a lamp what the lamp sends to the point's
  mirror image under the water along a straight line (its range, cone and cookie there; HDRP's fog along the real
  path), times the water's Fresnel reflectance at the lamp's angle on it (2 % face on) and the point's cosine towards
  the reflection point (`pt_compose.hlsl` `water_reflected_lamps`). The light grid's cell at the mirror image lists the
  lamps whose reflected light reaches the point; the two that light it most are traced: a water-only ray finds that
  slot's water at the reflection point, and two shadow rays clear the path (the point to the water, the water to the
  lamp). No noise (the lamp's centre), added after the direct light's filter as the sun's is; with the Caustics row
  off, none. No new option, material bit, ABI or frame constant. Not seen in the mineral cave's view: its puddles are
  the floor's own wet look in HDRP's G-buffer, not water the tracer has (the cave's HDRP lake lies behind the save's
  camera); the long soft streaks in them are the lamps' highlights drawn as balls of HDRP's light radius (0.25 to 1.5 m
  there), which HDRP draws as points. Both are written down in the agent's file; nothing was changed for them. Offline: a
  spot 0.6 m over a pool beside a wall: 303294 of 303294 wall pixels within 3 % of the light worked out by hand (the
  reflection point's Fresnel term: taken where the offset shadow ray crossed the water instead, 26 % of them were up to
  5 % off); none with the row off, where the mirrored path misses the pool, behind a card standing in the pool, under a
  card below the lamp, or from the pool moved behind the wall; HDRP's stripes split it into bands. The composite's GPU
  time with 32 lamps over the pool at 1280x720 stays within the stage timer's noise (0.11 ms with it, 0.19 ms without).
  Each check mutation-checked. Compiled and checked offline; not yet in the game.
- **The sharp texture squares no longer run out, and a jar is see-through** (PT-35, PT-13; the harness's `refract-jar`:
  "beige and nearly opaque, covered in big square blocks ... nothing is seen through them"; `hair-eyebrows` loaded
  after another scene: hair "solid but blocky with stair steps"; `docs/agents/surfaces-2.md`). Two causes. (1) What the
  camera sees from the tracer's own atlas (hair's color, the eyes, see-through glass) needs a detail tile, 512x512,
  and there were 32. Hair took one for every material, the game clones a hair material for each character, and none
  ever came back: a dozen characters took them all (29 of 53 hair materials in one run fell back to a 64x64 tile, which
  takes hair off the tracer's hair layer; the jars after them read their texture from 64x64: the squares). Now they
  are a pool of 128 (`runtime.hpp` `kDetailRows` 16, `take_detail_tile`): hair of the same maps and values shares one
  tile; a hair material gives its tile back when it is gone (new `TGPT_ReleaseHairColor`; the plugin asks for that only
  when hair or glass finds no tile: materials destroyed, or not traced for 600 frames) or is colored anew (a tile it
  shares stays as it was for the others, and `TGPT_RegisterHairColor` returns the tile with the new colors, which the
  plugin puts into the material); and hair never takes the last 16, which stay for glass and the eyes. A tile given
  back has its strand row cleared and its waiting bake dropped before the next hair's is written. (2) Glass HDRP draws
  as a transparent surface (a jar's wall, a pane) was traced as the near side of a block of glass, bent in at an index
  of 1.5 and out at whatever glass the ray met within a metre: the jar's far wall. That made a jar a lens with the wall
  behind it near its focus: one patch of the wall spread over all of it. Now such glass is a sheet (`pt_specular.hlsl`
  `behind_sheets`): seen straight through, tinted by each sheet on the way as the game's blending tints it, up to 4
  further sheets passed. It is told from the solid glass HDRP draws into its GBuffer (bottles: bent in and out as
  before) by lying in front of HDRP's surface by more than half the window `pt_prepare` finds it in. Costs: the atlas
  grows from 4096x6144 to 4096x12288 (RGBA8: 96 to 192 MiB of video memory); the frame constants from 3040 to 6112 B
  (two strand rows a tile: 64 to 256), the water pass's copy of them from 4096 to 8192 B a slot. ABI 1.16 (0x00011600:
  the new export, and a known key may get another tile). Offline: 200 clones of a hair material read its one tile;
  109 hair colors of their own took a tile each, the highest number 111 of 0 to 127, and the next a 64x64 tile; glass
  and an eye registered after them still got detail tiles; the 109 given back, 54 of 54 new hair colors took one again;
  a clone colored red left the tile it shared green (5408 of 5408 texels) and read its own red one; the layer's card
  reads a tile past the first 32 and its strand row. A checker seen through a 90 cm shell of see-through glass follows
  the bare checker's columns by 0.99 (0.06 before: the lens), with 0.97 of its contrast. Each mutation-checked.
  Compiled and checked offline; not yet in the game.
- **Hair drawn as a layer covers a pixel as the game's hair shader does** (PT-01; the owner: "completely broken hair and
  eyebrows"). Read from RealHair's compiled shader (its forward pass and its depth passes alike): the game takes its
  texture's alpha as pow(saturate(alpha / `_AlphaRemap`), `_AlphaPower`), cuts it below `_AlphaClip`, and where it is at
  least `_DepthPrepass` (0.5 on hair and beards, 0.95 on brows) its forward pass outputs 1: the texel covers the pixel
  whole. The layer (`pt_hair.hlsl`) took the texture's alpha as it is and covered only that share of such a texel, then
  stopped: the rest of the pixel showed the surface HDRP drew behind the hair, the scalp in the hair's shade, black. On
  Ron's hair (`Tex_H_m001`, power 1, read from the game's bundle): of its hair texels at the mip a head a metre or two
  away reads (512 to 256 across), 41 to 50 % are at least the prepass, and those left 24 to 28 % of their pixel
  uncovered. Other hair has `_AlphaPower` 0.5 (`Tex_H_m008`) or 0.25 (`Tex_H_f001`): a texel of 0.25 is 0.5 or 0.71 of the
  pixel in the game, 0.25 traced. Now the layer remaps the alpha and covers the pixel whole from the prepass on
  (`hair_game_alpha`, `strand_cover`). The power and remap reach it in `TGPT_Material`'s last field, a reserved float
  until now, as two halves (`hairAlpha`; same size and place, 0 reads as 1: no ABI change).
  Offline (`tgpt_gpu_test`'s layer card, failing since the layer was written): the card was the unit quad, whose texture
  runs 0 to 4, so 32 checker squares lay across 52 pixels, finer than a pixel, and the layer rightly averaged them to pink
  (438 red and 187 grey of 1764 pixels). Now the card has its texture once across it (8 squares of about 6.5 pixels) and
  its squares are 0.3 with `_AlphaPower` 0.5 (0.55 as the game takes it): 643 of the 1764 pixels are the card's own red
  and 1121 the wall; all 557 checks pass. Broken on purpose, each gives 0 red pixels and fails it: without the remap,
  without the whole pixel at the prepass, and the layer as it was. The source check `test_path_tracing_no_placed_lights`
  held nine lines older than the layer (the strands' cut at the camera, replaced by `strand_coverage` and `behindShown`;
  the instance masks, which gained 0x10 for layer hair); they now name the code that replaced them: 76 of 76 pass.
- In game this is not yet what shows (the harness's Ron, `20261006-172608/hair-eyebrows`, the tree's build): HDRP still
  writes the hair's depth and normals while tracing, though the plugin logs its passes switched off and its cut raised to
  1.1. The Surfaces view has 62 % of the head's forward pixels magenta (no surface the tracer shades, no layer over it)
  and the rest only partly covered; the Albedo view shows Ron bald; the Coverage view is darkened along the hair's
  outline (HDRP's surface in front of the traced scene). The layer looks for cards only in front of HDRP's depth, so it
  finds just the faint texels in front of the hair HDRP drew. The plugin now logs it when a hidden hair material is found
  drawn by HDRP again (`Characters: layer hair "..." was HDRP's again (...)`), and keeps RealHair's shadow pass, named
  SHADOWCASTER, as the code meant to (compared as "ShadowCaster", it was switched off with the rest).
- **Hair and beards the tracer draws are taken out of Kandra's culling while tracing** (PT-01, round 2; the owner: still
  "ridiculous"). In run `20261006-182908` the "was HDRP's again" line never came: the materials the plugin holds kept
  their passes off and their cut at 1.1, and HDRP still drew Ron's hair and beard (magenta in the Surfaces view, their
  normals in HDRP's buffer, the layer over them only in patches), while the same switches did hide his brows. Kandra,
  the game's character renderer, hands HDRP its draws through a batch renderer group of its own, culled per renderer by
  a layer mask (`VisibilityCullingManager.layerMasks`). A renderer whose every material is hair the tracer draws as a
  layer, or a scalp or beard mask under it, now gets mask 0 while tracing: no camera and no light of HDRP draws it, and
  Kandra still skins it for the tracer. The game's mask comes back when tracing stops (only into the slot it came from)
  and after 30 frames without the renderer among the traced characters. Brows (on the head's renderer) keep the material
  switches, which work for them. While hidden, that hair casts no HDRP shadow, and a beard keeps its last mouth shape in
  dialogue (the game copies the face's blend shapes to it only while Kandra calls it visible). To prove what draws, the
  log now names, twice a tracing session, every Kandra renderer of the two characters nearest the camera with its
  culling mask and, per submesh, the material Kandra registered for HDRP beside the one the plugin switches
  (`Characters: what Kandra hands HDRP ...`). Compiled; not yet in the game.
- **Brows stay transparent for HDRP, and hair hides the emission of the skin under it** (PT-01, round 4; in game after
  round 3, run `20261006-203513`, the crown was still a pale veil and the brows black bars).
  The brows: held opaque while tracing, HDRP drew their whole cards into its depth and normals before the tracer took them
  (RealHair has no opaque build that cuts them, as pt_prepare's note of 2026-09-28 says): the Surfaces view magenta over
  the brows in card-shaped slabs, and nothing lit them. Brows the tracer draws as a layer now keep the game's transparency
  (`CharacterCapture.Brows`; traced while transparent: `SceneCapture.LayerBrow`). HDRP draws a transparent surface only
  after the tracer has taken its depth and GBuffer, and its transparent passes cut by the cuts raised past 1. Brows
  without a detail tile are held opaque as before.
  The crown: the albedo, normals and materials views show the layer covering 98 to 99 % of each crown pixel in the hair's
  color, and the lit picture still jumps to pale pink exactly at the skull's outline. The one term the hair did not
  cover is the GBuffer3 of the surface behind (its emission and what HDRP bakes there), which the composite adds to a
  deferred surface's traced light. Now the hair's cover rides in B's alpha (0.4 times it on the kinds 0 and -1) and the
  composite leaves out that share of GBuffer3. The emissive view adds HDRP's GBuffer3 before any hair covers it, to show
  the skin's in the next run. GPU test: the wall behind a layer card drawn with light in its GBuffer3; the strands over it
  gain next to none of it (the old composite: all of it).
- **The scalp and beard masks are the tracer's, and HDRP writes no brow depth** (PT-01, round 3; in game after round 2 the
  crown was "a thin pink veil" and the brows thick black bars).
  The masks: under its cards the game paints the skin in the hair's color (`FirstPass_ScalpMask_*`, Kandra_Lit: a grey mask
  texture times `_BaseColor`, the hair config's scalp color, blended by its alpha). They sit in the hair's renderer, which
  round 2 hides from HDRP, and the tracer left them out: bare skin under thin strands. Now the tracer draws them as the
  hair layer's lowest layer: the mask's texture through the hair color bake (`TGPT_RegisterHairColor`, one tile a texture,
  shared by every character's clone; no new call), its color the traced material's, seen by the layer alone (camera-only:
  it shadows nothing), lit as a surface. A mask lies on the skin HDRP drew, so the layer takes it up to 1 mm (and 0.05 %
  of the distance) past HDRP's depth, where a card must be in front of it; and a ray takes 8 cards now, not 4 (a crown
  stacks thin strands: at 4 the ray gave out before the mask).
  The brows: they sit on the head's renderer, which cannot be hidden whole. The log of round 2 shows their material as
  the plugin left it (every pass off, `_AlphaClip` 1.1) and Kandra drawing that same material, yet the Surfaces view has
  a magenta strip there: Kandra's batches draw a material's passes whether they are switched off or not, and RealHair's
  depth variants cut at `_DepthPrepass` (0.95 on brows, 0.5 on hair: the cut round 1 saw), not at `_AlphaClip`. So HDRP
  wrote the brows' depth and normals, its forward pass discarded every texel, nothing lit them, and the layer found no
  card in front of that depth: black bars. Now `_DepthPrepass` and `_DepthPostpass` are raised to 1.1 with `_AlphaClip`
  and given back with it; the tracer keeps the game's values (`CharacterCapture.GameCut`).
  Offline (`tgpt_gpu_test`, a new check): a blue shell lying on the wall HDRP draws, behind five thin red cards (0.3 of the
  pixel each). Of 1764 pixels, 754 are the cards with the shell behind them (none with the wall), 746 the shell alone, 0
  the wall; all 634 checks pass. Broken on purpose: with 4 cards a ray, 0 of 687 have the shell behind; with the shell
  taken only in front of HDRP's depth, 0 shell pixels and 747 wall. The masks and the brows' cuts are the plugin's work
  (compiled, source checks 78 of 78); not yet in the game.
- Far rocks and hills the fog hides stood out lighter than the sky behind them, as if lit (the owner: "light gray cliffs
  against a darker gray sky, it looked like a bug"; PT-33). Fog Adaptation gave the fog's own light over a surface the
  traced light's gain and left the fog in front of the sky as it was ("the sky's stays", by design then): at a gain of
  1.3 (a foggy day at Broc Meala) and 1.4 (a bridge at night), a rock the fog hides entirely was the fog's color times
  that, against the same fog at 1 beside it. Measured in the harness's pictures: the sky the same in the game's and the
  traced picture (76.2 levels), the far rock 79 in the game's and 90 traced. Now the fog in front of the sky takes the
  gain too. HDRP lays its fog over the sky as over a surface at its Fog volume's Max Fog Distance along the ray; the
  plugin reads that distance each frame (`TGPT_Frame.forward`'s z, unused until now, so no ABI change: 0 keeps the sky
  as before) and the pass (`d3d11.hlsl` TGPT_FOG) uses the same height-fog transmittance for the sky as for a surface.
  The sky's own light (stars, the moon) keeps the game's exposure. Fog Adaptation stays On, the owner's choice in R3.
  Offline harness: without the distance the sky's pixels keep the fog's light, with it they get twice at a gain of 2.
- In game after the fix (the harness, 16:39): the light shapes are gone at the bridge by night and at Broc Meala; the far
  rocks sink into the fog as in the game's picture (`artifacts/harness/20261006-163130/*/cliffs-before-after.jpg`).
- **The game's fog dims the traced sun, moon and lamps on their way to a surface, as it dims the game's own** (PT-05
  "bloomey", PT-38 a night lit like a day, PT-12 white glints on wet rock and grass; found by two review agents on their
  own, `docs/PT-SURFACES.md` B and `docs/PT-VEGETATION.md` 1). HDRP multiplies every light by its height fog's
  transmittance before the light reaches a surface (the sun: from the surface up along its direction; a lamp: over the
  way between the two), and the game's day and night system keeps the fog's base at the hero's height, so its lights'
  strengths were set with that fog in front of them. By the fog values the harness recorded and HDRP's formula, of the
  sun arrives: 19 to 44 % by day at Horns of the South (a mean free path of 144 m), 31 to 56 % at the beach, 75 to 86 %
  at Cuanacht, and of the night's 60,000 lux "moon" 2 to 15 % (9 m). The tracer lit with all of it: sunlit ground
  measured x2 to x2.9 the game's where the fog is thick (and x1.03 in deep shade under an awning), a rainy afternoon
  came out sunny with hard shadows, and a torch-lit night cold and bright. Now `directional_fog` (the sun, and a placed
  directional light) and the fog's depth between a lamp and the surface dim them (`pt_common.hlsli`); the pixel's own
  sun, a path's, hair's, the sun's disc in glass, sunlight off water and the share of HDRP's own light all take
  `sun_fog`. The fog's numbers already reached the shaders (for the gain). Emissive surfaces are not dimmed (HDRP has
  no light for them). It removes light: the automatic gain will answer where the picture gets dark (nights).
  Offline check: a sunlit floor under a fog of known depth gets HDRP's share of the sun. Compiled; not yet in the game.
- Reflection Boost leaves grass and leaves as the game made them (PT-03; `docs/PT-VEGETATION.md` Change 2): boosted, a
  dry blade's reflectance doubled and its neighbour whose mirror ray met the ground stayed dark beside it (the white
  and dark blades). The row is for stone and brick, armour and metal. Ray Reconstruction's guides say the same.
  Compiled; not yet in the game.
- **Grass and leaves shaded as HDRP shades them** (PT-03; the owner: "the grass shading can still look a bit weird. Some
  dark patches, other white overblown ones", "Grass tends to loose a lot of its green color", "make grass/vegetation more
  'PBR'"; `docs/PT-VEGETATION.md`, `docs/agents/grass.md`). Measured in the harness's pictures (the grass's pixels, the
  sRGB curve undone): the traced grass had 0.33 to 0.41 of chroma where the game's has 0.48 to 0.69, and its blue was 0.67
  to 0.72 of its green where the game's is 0.47 (npc-day, forest, metal, night). In the Specular view a quarter of the
  grass's light at Horns of the South was colourless reflection. Six changes, each with its offline check:
  - A blade's mirror reflectance is its diffusion profile's, as HDRP's (Lit's FillMaterialTransmission): the game's foliage
    profile has an index of refraction of 1.1, a reflectance of 0.0023 (88 of its 128 grass and leaf materials), where the
    tracer used a dielectric's 0.04 and every wet blade became a small mirror of the sky. The plugin sends every profile's
    Fresnel0 (`TGPT_SetProfileReflectance`, ABI 1.15; logged once as "HDRP diffusion profiles' mirror reflectance");
    `pt_prepare` and Ray Reconstruction's specular guide (`d3d11.hlsl`) use it for the pixels of HDRP's transmission
    features through a thin profile. Skin keeps its 0.04. Offline: a wet blade reflects 0.06 of what it did; its guide's
    specular albedo 0.004 against 0.039.
  - The light through a blade is its profile's transmittance of the pixel's own thickness, tinted, as HDRP's (the game's
    foliage lets 1.4 to 6 times the light through, yellow-green; its grass profile 0.14 to 0.66, little blue), where the
    tracer let a grey half through. The plugin now sends every profile's transmission constants, not only the thick
    ones' (`pt_common` `leaf_tint`, in `pt_di_spatial` for the sun and lamps). Offline: 7.79, 9.00, 1.64 times the grey
    half's light through a 4.8 mm leaf of the foliage profile, as expected exactly.
  - The sky's and bounced light from behind a leaf, as HDRP adds the far side's light times the transmittance: a third of
    a leaf's rays (its grey share against the front's 1) leave from its far side (`pt_gi`), ReSTIR GI weighs and checks
    them from that side (`pt_rtxdi`), and the final shading tints them (`pt_gi_spatial`). Under an overcast sky, where all
    light is the sky's, the undersides of trees and the backs of blades got almost none. Offline: a leaf facing down under
    the open sky gets 0.49 to 0.52 of the sky's light from behind it (0.5 expected), 3.76, 4.43, 0.82 with the foliage
    profile (3.89, 4.50, 0.82 expected).
  - The material's own occlusion on grass and leaves: HDRP's ambient occlusion (GBuffer3's green behind HDRP's tag) takes
    their bounced and sky light down (`pt_compose`), the specular occlusion (GBuffer2's red) their reflections
    (`pt_specular`), as HDRP's do; the sun and lamps keep theirs (pt_prepare's flag 128). Blades were lit evenly from root
    to tip: 17 % of the game's grass pixels are dark there, 0.4 % traced. Offline: both exactly linear (0.30 and 0.50).
  - A blade's rays leave from its own face: where the plane through its neighbouring pixels is more than 60 degrees off
    HDRP's normal (a blade a pixel or two wide, or a back face the game leaves unflipped: 14 of its materials), HDRP's
    normal serves. Offline: a blade seen from its unflipped back gathered nothing, now 0.85 of the same blade facing
    the camera.
  - What a ray meets of grass and leaves is their texture times the mean of `_HealthyColor` and `_DryColor`, as the
    game's vegetation shader tints it, not times white (the bounced light off grass was 1.7 to 2.1 times HDRP's colour).
- **Grass casts soft, partial shadows** (PT-03; the owner chose it, 2026-10-06: "We want grass shadows, but not those
  horrible looking ones. Also sometimes they are pure white"). The game's own vegetation settings give grass, plants and
  ivy no shadows at all; traced, every tuft was a full blocker and threw hard black shadows on the gravel and on the grass
  behind it. Now a leaf or a blade (`TGPT_MATERIAL_TRANSLUCENT`) lets a light's shadow ray through as often as the tracer
  lights it through from behind, half the time (`alpha_opaque`, for the final shading's rays and ReSTIR's visibility
  test): one blade's shadow is half one, it deepens with every blade behind it, and a tree's crown, many leaves deep, still
  shades as a whole. Opacity micromaps leave a translucent material's covered texels to that test (`pt_omm_bake`).
  Offline: the floor in a translucent card's shadow gets 0.50 of the sun the opaque card's takes.
- **Thin cloth lets light through its weave, tinted** (PT-39: the owner's save "PT candle-boil", a candle corner under a
  red cloth canopy with holes, came out 32 of 255 traced against the game's 92). Thin cloth (`TGPT_MATERIAL_THIN_CLOTH`:
  canopies, banners, laundry) stopped every shadow ray like a wall. Now a light's shadow ray passes it as often as the tracer
  lights it through from behind (`CLOTH_TRANSMISSION`, 0.3), tinted by its colour at full brightness (`cloth_tint`, through
  `glass_walk`'s crossings as glass tints its light); its holes let the light through as before. The plugin makes every
  thin cloth alpha-tested (cut-off 0 where it has none) so that rays test it, and micromaps leave its covered texels to
  that test. And cloth the wind moves (the game's `Cloth_Wind`) is traced where it stands still while HDRP draws it
  swayed: near the camera that is more than the camera ray's window (6 cm for a canopy 3 m overhead), so its pixels were
  not taken for cloth and were never lit from behind. Where the traced scene has nothing within the window, a swaying thin
  cloth within 30 cm of HDRP's depth now counts (`pt_prepare`). Not seen in game; whether the canopy at the candle corner
  was that case or its own still copy shading it is for an F8 there (`docs/agents/grass.md`). Offline: the floor under a
  red cloth card gets 0.30 of the sun in red, 0.107 of that in green (its colour); a cloth drawn 25 cm from its traced
  place is lit through from behind again (0.73 of the cloth in place, its still copy now shading the lamp in front;
  before, not at all).
- **Thin cloth lets the sky's and bounced light through too** (PT-39, in game after the entry above: the candle corner
  still 32.7 of 255 against the game's 92.4, the canopy's underside still black). The corner stands in a building's
  shade at 10:47: no sun reaches the canopy, so the lights' partial pass had nothing to let through; its light is the
  sky's, whose rays still stopped at cloth, and a cloth pixel gathered no light from behind. Now:
  - a cloth pixel gathers the sky's and bounced light from behind it as a leaf does, by the share it lets through
    (`gathers_behind`, `CLOTH_TRANSMISSION` 0.3; its colour is the composite's own): a canopy's underside glows in its
    colour under an open sky. Not hair.
  - the rays that gather light for a diffuse surface (pt_gi's from HDRP's surfaces, a path's next ray, the radiance
    cache's: `trace_bounce`) pass thin cloth by the same chance as a light's ray, tinted by its colour (`cloth_walk`):
    what lies under a canopy gets the tinted share of the sky. A bounced-light sample keeps its light over the chance
    its ray had of getting there (1 / 0.3 for a cloth passed, 1 / 0.7 where cloth stopped it; applied after the firefly
    clamp, which cut the sky through a canopy to 0.21 of 0.30 when it came first), and the ray that checks it is seen
    (`pt_gi_spatial`) passes cloth by that chance and takes the tint, so ReSTIR's reuse across a canopy's edge dims a
    neighbour's sample as the pixel's own. That ray takes a chance of its own at the cloth (`g_raySalt`): its direction
    differs from the sample's own ray's in the last bits alone, and deciding as that ray did, it let 1.2 times the light
    through. (With cloth simply left open to that ray, the harness's canopy leaked a quarter more light, untinted: 0.37
    through, green 0.34 of red.) Reflection rays still see cloth as solid.
  - a character's clothes are no canopy: a material named like cloth on a character stops lights and rays whole again.
  What it does to other cloth: a tent's wall glows from outside and its inside takes the tinted share of the day; an
  awning's goods, laundry on a line and a room behind a curtain take 0.3 of what was behind the cloth; a banner on a wall
  barely changes (behind it is the wall in its own shade). It will not bring the candle corner to the game's 92: a red
  canopy passes 0.3 of the sky in red, about a tenth of its brightness, and the game's corner is lit by an ambient light
  that nothing shades.
  Offline (tgpt_gpu_test): a cloth sheet seen from below under the open sky takes 0.33 0.29 0.29 of the sky's light from
  behind it (0.30 expected); the floor under a red cloth canopy, the sky alone and without ReSTIR's reuse, takes 0.30 of the
  sky's light the solid canopy takes from it, in red, and green 0.107 of red's share (0.30 and 0.107 expected). With the
  reuse 0.33 and 0.23 (0.32 and 0.15 deep under the canopy): the reuse's Basic bias correction darkens the floor near the
  solid canopy's edge by 16 % (no canopy 6 %), which the cloth's light through hides, so cloth minus solid holds that
  untinted loss too; with Crevice Light's ray-traced correction 0.34 and 0.11. Broken on purpose, each fails its check:
  bounce rays stopping at cloth again 0.00 through; `CLOTH_TRANSMISSION` 0.01 0.01 (at 0 the shaders don't compile); the
  check ray deciding as the sample's ray 0.37; the clamp after `held` with no tint on the check ray 0.17, green 1.31 of
  red; cloth not gathering from behind 0.01 of the sky's.
- **A leaf's light from behind is wrapped as HDRP wraps it** (PT-03, the bright yellow-green blades in the harness's `forest` scene):
  HDRP's thin-object transmission lights a leaf from behind by (w - N.L) / (1 + w)^2 with w for 15 degrees, 0.795
  straight from behind and some light for a blade edge-on to the light; the tracer's plain cosine gave 1 and 0. Over all
  directions the two give the same light, so the grass is no darker for it; the blades with the sun right behind them are
  a fifth dimmer (`leaf_cos`, for the sun and lamps where the profile's transmittance is known). Offline: a blade edge-on
  to the sun (its front 0.100 towards it) takes 4.89 5.50 1.82 times its front's light alone (4.89 5.50 1.82 expected; 1.00
  with the plain cosine, which fails the check); a blade lit from behind by the test's sun, nearly straight behind it,
  7.68 8.88 1.62 of the grey half (7.67 8.87 1.62 expected; 7.79 9.00 1.64 with the plain cosine).
- **Reflection Boost's mirror share goes to metal** (PT-32; the owner, 2026-10-06: "we want metal to reflect more than
  stone and brick"). The boost raised every surface's mirror share toward 0.6 by the same step: at the default 0.1 that
  doubled a non-metal's (4 % to 7.9 %: stone, wood, soil; 46 to 63 % of the shadows' lift at Cuanacht and 18 to 26 % at
  Horns of the South, `docs/PT-SURFACES.md` B.3) and left a metal at 70 % where it was. Now everything gets glossier by the
  row's value (a tenth of the way to a mirror's finish at 0.1), a metal by twice that, and a metal's mirror share rises
  toward the brightest its own color goes (a gold stays gold); a non-metal's stays. How much of a metal a surface is comes
  from its specular color (`pt_common.hlsli` `reflection_boost`; the same in `d3d11.hlsl` for Ray Reconstruction's
  guides). This revises the ruling of 2026-09-28 for stone and brick (they keep the gloss, not the doubled mirror).
  Offline check: a grey metal floor gains more from the boost than the same floor as a non-metal. Compiled; in test.
- **Torches and candles out on the campaign map light again** (PT-38; the owner: "In vanilla, I see nearby flames from
  torches cast some orange-brown hues on the top of the adjacent grass, and I just don't see that at all in the PT
  presentation"). Out there the game keeps its lights at the top of the scene, under no prop, with names that say
  nothing (at Horns of the South 127 "PointLight_Cookies", 184 "PointLight_Ambience", 25 "PointLight_Shadows"); only a
  light whose own or a parent's name says flame, torch or candle was kept, so every one of them was left out and a torch
  lit nothing. A light under no prop is now a flame by its look: warm (3500 K at most) with a cookie, the flicker pattern
  the game gives its flames, or warm within 1 m of such a light (a torch's ambience light beside its flickering one). A
  warm light alone stays out: a placed fill light looks the same (no fake lights). Harness: traced lights at the bridge
  by night 50 -> 86, the man and the grass beside the torch lit orange as in the game's picture.
- Sky Intensity 0 means no sky light (it was sent as 0, which the native side reads as "not set": 1).
- After path tracing was turned off, hair kept another render queue than the game gave it (2750 "before refraction" came
  back as 3000, a scalp's 2749 as 2999), in every scene with a head in view (7 of 19 in the baseline run; PT-02): HDRP
  then draws that hair in another pass. The game's `HairController.Refresh` runs HDRP's `ValidateMaterial`, which derives
  a queue from the queue it finds: opaque and back loses "before refraction". The hold on opaque hair now remembers each
  material's queue and gives it back (`CharacterCapture.RememberQueues`, `GiveQueueBack`), for the brows and lashes too.
  In game with the fix (the harness, 15:54): 0 of 32,000 to 37,500 values differ after Off in seven scenes.
- **Round things are shaded as round** (PT-04; the owner, 2026-10-06: "cloth, jars, and other things can look a little
  'stepped'/low-poly ... around corners, shoulders, cloth folds"; `docs/PT-SURFACES.md` A, `docs/agents/surfaces.md`).
  Four places took a mesh's flat faces where the game shades it smoothly. (1) What the tracer draws from its own mesh,
  see-through glass (the jars, windows) and props HDRP draws forward, was shaded by each triangle's normal: a round jar
  was a prism of flat mirrors (11 jumps in its normal across the jar at `refract-jar`, none across the jug HDRP draws; a
  lamp's reflection a block one face wide), and a bottle's far side bent what it shows panel by panel. Now the mesh's own
  vertex normals (on the GPU since the caustics; `Hit.rounded`), turned to face the viewer at the outline; rays still
  leave from the triangle. Characters' meshes bring none. (2) The direct light's fade towards each face's terminator
  (Chiang's softening) ran on every opaque surface and left each face near the shadow line its own flat light: a step
  at every face's edge and a shadow line straight along the mesh. Now skin alone keeps it (marked in the material's
  color alpha, 0.25), as the owner judged faces with it; shadow rays start 2 cm outside, which clears a convex face's own
  mesh. (3) The built-in denoiser (and the direct light's and reflections' filters beside NRD) stopped at every face's
  edge by its orientation from depth: each face of the jug at Horns of the South was filtered on its own into one tone,
  the Direct, Indirect and Specular views' faces (the debug views run it; Ray Reconstruction doesn't, and the jug's Ray
  Reconstruction picture was smooth). Now it stops only where the smooth normal turns too. NRD's own guide stays (a
  measured choice). (4) A translucent surface (HDRP's transmission through a thin profile: leaves, and the candle wax of
  PT-40) started its shadow rays on the light's side of every face that turned from the light; on a closed alpha-tested
  mesh that is inside it, and the face before the shadow line went black. Now only where its shading normal turns away
  too. A hidden "Smooth round surfaces" (on; off is the old look, option 33554432) for an A/B in the harness. Offline, a
  12-sided cylinder, the largest step between neighbouring columns before and after: see-through glass's normal 0.518
  and 0.001, a prop drawn forward the same, its direct light lit by a lamp 0.141 and 0.020 of its brightest (skin keeps
  0.141), closed translucent 0.121 and 0.016, the denoised bounced light 0.037 and 0.001; mutation-checked. The rest of
  the offline test as before, but normal-mapped walls: their error against the undenoised picture rose 3.5 to 4.6 %, as
  they keep their full bump shading at grazing light (measured: not the filter). Compiled and checked offline; not yet in
  the game.
- Candle wax (PT-40) is not taken for skin: red in the Surfaces view is HDRP's subsurface scattering with transmission
  through a thin profile, which the tracer lights as a leaf (orange is skin). Its faces are (3) and (4) above. Its dark
  sides (each flame lights its own candle's top; the game's cream needs light the tracer leaves out and HDRP's subsurface
  blur, which the tracer runs for solid profiles only) and white wicks are left open (`docs/agents/surfaces.md`).

TG Path Tracing: an in-game test harness (2026-10-06; a developer's tool, in no release zip, no mod changed)

- Asked for by the owner before any more hair work: an agent should test path tracing in the real game by itself.
  `tools/harness/Run-PtHarness.ps1` starts the game, a runner plugin (`src/TGPathTracing.Harness`) loads saves by the
  name the owner gave them, freezes the world, and takes 8 consecutive frames of the game's own picture, of the traced one
  with Ray Reconstruction and with NRD, and of the game's again after tracing was turned off (and after four more
  toggles), then the same three with the world running, with frame rates. `tools/harness/analyze.py` writes pictures and a
  report. All of it: `docs/PT-HARNESS.md`.
- Why a frozen world: every step renders the same instant, so what differs between two steps is the renderer's doing.
  Why also a running world: frozen, the picture is so still that a denoiser has it easy (the first run measured 0.05
  levels of change per pixel with Ray Reconstruction); what boils in play needs the camera's idle sway and the flames.
- Why the game's state is written out and compared, not only its picture: the first run's picture came back the same,
  and nine hair materials had not (their render queue 2750 -> 3000, the scalp's 2749 -> 2999).
- Why it reads Unity and HDRP and not the mod: the runner references no build of the mod, so it tests whatever is
  installed, and a value the mod forgot to put back cannot hide in the mod's own records.
- Nothing is saved during a run (the game's own save blocker, a stop on `LoadSave.Save`, and the save files compared
  before and after), and the game is never started while another program uses the GPU's 3D or compute engines (the
  owner's rule of that day).
- "Is the game's picture back after Off" is judged block by block against what that block changes by itself (the two
  takes before tracing, the two after): judged by the picture's worst block, one flame excused everything, and judged
  by a fixed level, a brazier and foam on water read as leftovers. A recipe `control` runs the same steps with path
  tracing never on, for what the game's picture does by itself in that time.
- A save comes back with the camera level: the view a save was made with is found again from the picture the game
  stores with it (`tools/harness/calibrate.py`, `tests/ingame/camera.json`).

TG Better Upscaling and the game's update of 2026-10-06 (1.2.1; Steam build 25629220; shipped in 1.2.3, 2026-10-09)

- The game's update gave its Upscaling row an FSR entry of its own: Unity's built-in FSR 2 (HDRP's FSR2Pass through the
  new `AMDUnityPlugin.dll`), at index 3 under the preference ID `UpScalingType_FSR`, the very index and ID this mod's FSR
  entry has. 1.2.0 found a row of 4 entries where it knew 3, left it alone (no XeSS, no Quality Mode rows, no FSR
  Sharpness) and still set DLSS and FSR up from its config in the game's controller, so the game's slider did nothing and
  "FSR" ran this mod's FSR at the config's mode. Read from the game's code; the game has not been started since the update.
- Now the row is None, STP, DLSS, FSR, XeSS again on both game builds, with one FSR entry: this mod's FSR 3.1 or 4 takes
  the game's entry where AMD's runtime reports it (the same index, so a saved choice carries over), and where it doesn't
  (no D3D12 device, the bridge not started) the game's own FSR 2 keeps its entry and its slider, as without the mod. The
  game's entry counts as offered when the game itself does not forbid it.
- The controller hook leaves index 3 to the game unless AMD's runtime reported FSR, and leaves DLSS to the game's slider
  when the row could not be extended (Ray Reconstruction keeps its own path). Wherever this mod sets the camera up it
  switches HDRP's FSR 2 off for it, as the game does: HDRP runs one upscaler for a camera, the first in its asset's list
  that the camera allows, and FSR 2 left on could take STP's place and this mod's pass with it. The load line now logs
  that list. The overlay, the report and the mod managers name the game's FSR 2 where it runs.
- Nothing only the updated game has is used: the same build compiles against the updated Mono game and against the
  previous build's IL2CPP interop. Deployed to the Mono game on 2026-10-06; not yet seen in game.
- On the normal version the alert icon never showed on the Frame Generation and Upscaling rows, since 1.2.0. The
  bridge listed the settings screen's rows by walking the game's own set of views through the interop's enumerator,
  which threw "Collection was modified" the first time the settings opened (today's log and both normal-version
  troubleshooting reports of 2026-09-25), and RowAlerts then gave up for the session. Unity lists the row views now
  (`Object.FindObjectsByType`); a failure costs one try and one log line, not the session. Normal-version code only:
  the Mono build compiles to the same instructions as before, so its zips of 11:25 stand. Built against the game's
  fresh interop and deployed 12:03; not yet seen in game (the log should say "rows are listed").
- Version 1.2.1 for TG Frame Generation, TG Better Upscaling and the bridge (their only native change is the version in
  their logs). Both are built from the tree as it stands, so whatever changed in them since the 1.2.0 zips on Nexus
  were made comes along (the entries below). 15 native tests pass on the staged DLLs, with TG Path Tracing's 1.2.0
  native on the 1.2.1 bridge.
- TG Frame Generation: nothing found broken. All three mods compile against the updated game, and all 67 private game
  and HDRP members they reach by name are still there. Its inputs don't depend on the upscaler row (DLSS's evaluation,
  else a custom pass with HDRP's depth and motion vectors, which is what the game's FSR 2 reads too).

TG Path Tracing (new, prototype, Mono version)

- Hair has its highlights: the shiny bands that run across a head of hair, from the sun and every lamp, as the game's
  own hair shader draws them. The game's RealHair is HDRP's Kajiya-Kay hair: two highlights around each strand's
  direction, shifted along it toward the root or the tip, tinted by the hair's specular map and color. The tracer now
  evaluates the same for the sun and for every lamp sample, from each hair material's own values (shifts, smoothness,
  tints and strengths, read from its compiled shader) and textures: the strands' direction from its flow map (in the
  mesh's texture frame, worked out from the triangle), its smoothness and its specular map, baked into a small tile per
  material. Hair reflects its surroundings as HDRP's does, barely (in its second highlight's tint). The sharpest
  highlights HDRP makes (a line from a glossy strand, too thin for rays to find) are widened to about 1.3 degrees, which
  keeps their light. The game's recoloring of hair (its specular tint too) is followed. Harness: a sunlit quad of hair
  turned 25 degrees off the sun's mirror angle gives HDRP's formula pixel by pixel with its strands across the turn
  (0.01787 against 0.01788), and 18.8 times less along it; a round highlight gave both the same, and the strands' frame
  turned gave them back to front. ABI 0x00011300 (TGPT_Material 128 bytes with the hair fields, TGPT_RegisterHairLobes).
- The game no longer freezes at its start while the tracer gets ready. The driver compiles every one of the tracer's
  passes, which after a new version or GPU driver took 13 to 22 s (the owner's log, 2026-09-28: 21.8 s), and the first
  traced frame waited for it on the game's render thread. The passes are now made on threads of their own from the
  moment the game creates its GPU device (about 30 s before the main menu), several at once, and the D3D11 ones are
  compiled there too; until they are made the game draws its own image and the Video tab's row stays as it is. Harness:
  the passes made in 0.5 s with the driver's cache warm (7.4 s cold), the render thread's own start 0.09 s. ABI 0x00011200
  (TGPT_Status.starting and startSeconds).
- When the GPU stops (a driver reset: the game froze at 09:16 on 2026-09-28 with nothing in the logs), the native log now
  says why each device was lost (hung, reset, removed) and how far the tracer's own GPU work got: the compute queue writes
  a marker as each stage completes, into memory the CPU still reads after the reset, so a stage that hung names itself
  ("stopped in stage bounced light"), and a frame that finished says the hang was elsewhere.
- Hair, brows and lashes are no longer LEGO bricks and black bars. While tracing they are held opaque, and HDRP then cut
  them at the alpha they blend from as transparent layers: 0.1 for the hair (RealHair's _AlphaClip, read from its compiled
  depth pass), 0.067 for the lashes and 0.05 for the scalp masks, so the faint fringe of every clump and card turned solid
  (a brow kept 78 % of its soft area, the lashes 89 %, a head of hair 97 %; the lashes are black, and a scalp mask's faint
  edge is near black). Held, they are now cut where they are half opaque, as the game's own depth prepass cuts its hair
  (_DepthPrepass 0.5): the brow keeps its dense 27 %, the lashes 48 %, the hair 73 %. The game's values come back when
  tracing stops.
- Hair's color is baked into a 512x512 tile where it was 64x64, so its strands and clumps stay sharp up close (the camera
  sees hair's color from its tile; at 64x64 every strand blurred into flat clumps). The detail tiles are 32 now (16): hair
  shares them with the eyes and glass, and takes a 64x64 tile once they are all used. Harness: the baked hair quad keeps
  its gradient and highlight band, colored again it is baked again, and hair past the last detail tile takes a color tile.
- The eyes next to the lashes are the eyes' again: the camera ray for a pixel HDRP drew forward stopped at the first hair
  card it met, and a lash card a few millimetres in front of the eye made the eye's pixels the lash's black. A hair card
  well in front of HDRP's depth there (3 mm and a tenth of a percent of the distance) is one HDRP cut away at that pixel,
  and the ray passes it. Harness: an eye behind a black lash card 4 cm in front of it reads 0.0894 against 0.0895 alone
  (0.0000 with the rule taken out).
- Hardening: the emitter pass reads at most a cluster's 32 triangles from a record, so a wrong one can't hold the GPU.

- Sunlight reflected off water, in the Caustics row: what faces the water above it (a bridge's underside, a hull, a
  bank, the rock over a pool) takes the sun's mirror image in the water, weighed by water's reflectance at the sun's
  angle (2 % face on, far more at sunset) and moved about by the water's caustic pattern as the waves move it. The
  game never had it, and the tracer's own paths miss it (its rays pass through water). Harness: a wall at a pool's
  edge gets exactly the plain mirror's light where its mirrored path reaches the lit pool, bands of it with a striped
  pattern, and none where the path misses the water or the sun doesn't reach it.
- See-through glass in front of the sky (a window, a bottle held up against it) is there: the tracer drew it only in
  front of something HDRP drew, and HDRP's own passes for it are hidden while tracing, so against the sky it vanished.
  Now its reflection is traced and the game's own sky is laid under it through its color. Harness: a green pane against
  the sky turns the sky behind it green (its green against its red x1.61, x1.00 before).
- Caustics under water, in the Caustics row: sunlight reaching a lake bed or a pool's floor through the water ripples with
  the water's own caustic pattern, and only where the light really passes through the water. The pattern is HDRP's (the
  texture its water renders every frame from its waves), read as HDRP maps it; the light is the tracer's. HDRP's water is
  in the traced scene now under a ray mask of its own, which only a water query from the lit point towards the sun (or
  straight up, in a cave) meets: every other ray leaves it out. The pattern multiplies the direct light alone, after its
  filter, so the shade of a bridge and the night stay plain, where HDRP's own multiplied all the light seen through the
  water. HDRP's own is off while tracing; its light shafts under water keep theirs (the game's fog). Harness: a water quad
  over the sunlit floor with a striped pattern; the pattern and its copy moved half a repeat add up exactly on the floor
  under it, nothing changes elsewhere or in the bounced light, and the water stops no ray.
- Caustics row (On): the sun and lamps shining through a glass bottle, flask or potion are bent and focused by it, a
  bright spot or line in its shadow. Manifold next event estimation (Hanika et al. 2015, solved as Blender Cycles'
  shadow caustics are): from the straight shadow ray's two crossings of the glass, Newton steps along its smooth surface
  (meshes now bring their vertex normals) find the path Snell's law allows, weighted by how strongly the glass focuses
  it. Glass that isn't one closed object crossed twice keeps the straight tint. Harness: a clear glass ball in the sun
  focuses it into a spot brighter than the open sun with a darker ring, and most of its light still reaches the floor.
- No Hair Shadows row: it put the traced shadows on hair and eyes that HDRP shaded itself, and the tracer shades both
  now. What HDRP still shades itself (a character's forward surface the traced scene lacks) always takes them.
- The eyes are shaded by the tracer, no longer by HDRP: the game's eye shader (read from its compiled code) lays the iris
  (its pupil, and a limbal ring darkening its edge) over the sclera, and that is baked texel by texel into a 512x512 tile
  per kind of eye, lit as a wet surface (smoothness 0.9) with HDRP's own smooth normal. HDRP's cornea, a transparent
  layer over the eyes, is hidden while tracing; the shader's fake cubemap reflection is left out (the tracer reflects the
  room). Harness: a white sclera, a green iris and its darker limbal ring come out of the bake.
- Fog Adaptation row (On by default, for comparing): the game's fog brightens with the traced light where Dark Adaptation
  brightens a dark place (x5 in the tavern), so its haze looks as thick against the room as in the game. HDRP lays its
  fog over the traced light at the game's exposure; a pass right after it gives the fog's own light the same gain (the
  color after the fog, less the color before it through the fog's transmittance). Off is the look before: the fog at the
  game's brightness, and the brightening fading behind it. Harness: fog light added over the image comes out twice as
  bright at a gain of 2, and the sky's stays.
- Hair, eyebrows and lashes stay opaque while tracing, alpha-tested as the game draws hair far away, so the tracer shades
  them at every distance. Up close the game makes them transparent, which the tracer couldn't find in front of HDRP's
  surface, so HDRP drew them over the traced image with its own light. Hair goes through the game's own switch
  (HairController.Refresh), and what the game asked for comes back when path tracing stops.
- Hair takes its shader's colors: the game's RealHair shader (read from its compiled code) colors its diffuse map toward a
  root-to-end gradient, adds two highlight bands and occlusion, and only then multiplies by _DiffuseColor, which the tracer
  used alone. A far bartender whose color lives in the gradient (_DiffuseColor 0.70 grey, auburn root and end) showed grey
  hair. The whole formula is baked texel by texel into each hair material's own tile, and baked again when the game
  recolors it. Harness: a red-to-blue gradient with a green highlight band comes out as such, and green once recolored.
- See-through glass shows its texture sharp: a 512x512 tile of its own, filtered, where the 64x64 tile every texture has
  for rays showed a jar's stains as blocks. Harness: a checker of 4-texel squares keeps its contrast from the new tile
  and averages away in the old one.
- Glass the game draws as transparent (the alchemist's jars, windows, potions: by name, as the bottles are) is drawn by
  the tracer: seen through and refracted, tinted by its color as far as it covers, shadowing and in reflections. HDRP's
  own passes for it are hidden while tracing. Before, it was invisible to rays and HDRP drew it on top with its own
  light. Not yet where the sky is behind it (a window seen against the sky keeps HDRP's). Harness: a green pane half
  covering makes the wall behind it 1.6 times as green against its red.
- A far character's hair the traced scene doesn't hold (no renderer the tracer captures draws it there) keeps HDRP's
  shading, with the traced shadows, until the tracer has it: its camera ray met the head under it, and the traced scalp
  showed as grey hair. The F8 report lists every renderer of the characters along the crosshair, to find what draws it.
- A character's hair takes the color the game gives it later too: the game colors hair on each character's own copy of the
  material, sometimes after the character first shows, and the tracer, which read it once, kept a far character's hair
  grey (the material's default) until it came near. Hair's color is read again every frame now.
- Hair is shaded by the tracer, not by HDRP: its color is its shader's (RealHair's diffuse map times _DiffuseColor and
  _DiffuseStrength; read as the texture alone it came out grey), camera rays stop at its strands wherever they meet them,
  it is lit through from behind as thin cloth is, and Reflection Boost leaves it alone. The eyes still keep HDRP's shading
  (darkened by the traced shadows) until the tracer reads their textures: the log now lists the eye, eyebrow and lash
  shaders' textures and values for that.
- The direct light's reuse keeps a light choice for 4 frames, not RTXDI's 20 (NVIDIA RTX Remix's choice with Ray
  Reconstruction): a hanging cage's thin bars boiled, where the temporal reuse can't be permuted. The F8 capture's
  frame-to-frame change on them: 0.40 at 20, 0.14 at 4, 0.11 without the reuse at all.
- The F8 capture's boiling comparison adds the bounced light's reuse over 4 frames instead of 8, and the image as traced
  again once settled: the first pair follows the report straight away, while the HUD and the image can still change.
- Fill Light Fog (a new Video row, On by default): the game's fill lights, which path tracing replaces, light no surface but
  still light the game's fog as in the game, so interiors keep their haze (the tavern's came from its 24 fill lights, and
  was gone). Off: only the traced lights light the fog, to compare.
- Reflection Boost defaults to 0.1 again (it was 0.2). A value saved in the config stays as it is.
- Bounces defaults to 1 (it was 2): no difference showed in game between 1 and 4, since the radiance cache carries the
  light of every bounce after a path's last, and the bounced light was most of a frame's GPU time (28 of 35 ms in the
  tavern at 1924x1084 on an RTX 5080). A value saved in the config stays as it is.
- The F8 capture measures what boils: how much the middle of the image changes from frame to frame, in 8x8-pixel blocks, as
  traced and again with the bounced light from each pixel's own 16 rays alone (ReSTIR GI's reuse left out), with the direct
  light without ReSTIR DI's reuse, and with neither, each with its difference image. The report's Boiling line gives the
  four, and the change around the crosshair. It takes about 16 seconds now.
- The F8 report gives the GPU time of each stage of a frame (the scene, preparing, bounced light and its spatial reuse,
  direct light, hair and eyes, reflections, the radiance cache, denoising, the composite, and the water's reflections),
  averaged over the last second; BepInEx's log has the same in its summary every ten seconds.
- Spatial Samples goes to 16 (it stopped at 8): more of them smooth the bounced light's boiling further, at a cost.
- Eyes take the traced bounced light where HDRP's own ambient light is taken from them: HDRP scales it by the sky's light at
  the camera, a hundredth of it indoors, and the whites of the eyes went dark between the lids (eyes too small in every
  candle-lit face). The share HDRP no longer gives is the radiance cache's light where the eye is, times the eye's color.
  Not hair: its traced color lacks the tint HDRP's hair shader gives it (blonde hair under a roof glowed).
- Reflection Boost leaves skin as the game made it: the game's skin is glossy already (a roughness of 0.18), and boosted
  its reflectance doubled, so a neck mirrored the dark chin and collar above it in sharp, reddish patches.
- The dialogue light of Conversation Lighting Overhaul lights a face as brightly as the mod sets it at the game's
  exposure: the traced light's automatic brightening of dark places is taken back out of it (three times as bright in a
  candle-lit room). The Exposure row still applies to it.
- A light whose HDRP data has Affect Specular off makes no highlight where the tracer lights with it either, as in the game
  (the F8 report's lights say "no specular" for those).
- A reflection direction drawn into the surface's own triangle (a smooth, normal-mapped curve over flat triangles: a metal
  strap across a door) is mirrored back out, as the bounced light's and the water's are.
- HDRP Unlit surfaces reflect nothing, as in the game (a portal's black core reflected the two bright lights inside it: a
  glowing white orb), and a reflectance below 2 % no longer brightens toward grazing angles (Unreal's and Frostbite's
  Schlick).
- Hair and eyes (what HDRP shades itself) no longer go dark in daylight: where the traced shadow of a lamp HDRP doesn't
  shadow fell on them, the whole pixel was multiplied by the share of that lamp's light left, the sun's light and the
  ambient's with it. The light of what HDRP shadows itself (the sun, as far as the traced sun reaches the pixel, and a
  lamp with a shadow map) now stays in the share. Emissive surfaces no longer count as lamps there, and every such pixel
  is passed on, a share of 1 too (left out, its shadowed neighbours' shares spread over it). Harness: a sunlit table
  keeps 0.65 of its light with a lamp beside it blocked, against 0.47 without the sun.
- Straw on a floor, a fringe, grass tips: what is finer than a material's alpha tile lets the light between its parts
  through. Every alpha tile's coverage stops rays other than the camera's by chance, as hair's did; cut out at half a
  texel's coverage, the tavern's straw was a solid mat to rays, and its shadow blacked out the floor seen between the
  strands. Micromaps settle only fully covered and bare texels now. Harness: a card of half-covered texels casts 0.49
  of a solid card's shadow.
- Grass, leaves, banners and laundry move in the wind as HDRP draws them, and are traced where they stand still: rays
  from the drawn surface pass what the wind moves within 20 cm of it (its own instance mask), where they met its still
  copy (the inner side of laundry black, black spots on the ground swaying with the grass). Harness: 45 floor pixels
  darkened under such a quad 12 cm up, 3104 under an ordinary one; 60 cm up it shadows as an ordinary one does.
- Grass and leaves are lit through from behind where paths meet them, as the camera's translucent pixels are: the
  underside of a sunlit blade was black to the rays that met it, and the ground under grass took no light through it.
  Harness: the floor in a sunlit canopy's shade 1.33 times as bright under a translucent one.
- HDRP's fog keeps what it hides hidden: the traced light's automatic brightening of dark places fades as the game's
  height fog hides a surface (the player's Exposure stays). Through the castle's night mist the brightening lifted what
  the mist hides.
- HDRP Unlit materials take their color from _UnlitColor and _UnlitColorMap (their _Color and _MainTex are there for
  Unity's lightmapper, and white).
- Rays Per Pixel goes to 16, and every ray counts: at 1 the bounced light is ReSTIR's, at more a share of it (30 % at 8,
  48 % at 16) is the pixel's own rays' mean. Before, the rays only competed for the one sample ReSTIR shades with, which
  left the boiling as it was. Harness: the share of a frame's blotchy error the next frame keeps, 0.34 at 1 ray, 0.17 at
  8, 0.08 at 16; a frame's error 0.108, 0.094, 0.091.
- From a review of the day's changes: the radiance cache's per-frame sums could wrap past 32 bits for a saturated color
  (each channel is bounded now, and a cell takes 512 samples a frame); lamp light comes through thin skin (ears, fingers)
  as the sun's does; shadow rays to glowing surfaces end at them, not 5 cm and more short (a leak 17 cm deep 50 m away);
  hair's layers decide independently whether they stop a ray; the composite blends one kind of traced texel at a time
  (glass with glass, HDRP's shares with shares); glass with the Reflections row off is lit as HDRP drew it (it was black);
  the pixels' first bounced-light rays feed the radiance cache counting those below the surface as no light.
- Metal is metal where rays meet it: a traced surface's metallic and smoothness come from its mask map, texel by texel
  (HDRP Lit's, remapped as HDRP remaps them, and those of the game's own shaders as their GBuffer passes read them:
  characters' and their armour's, the castle's architecture, cloth). A surface HDRP draws forward takes its roughness
  from the mask map, and a metal there has no diffuse color and its base color as its specular color (it had a
  dielectric's), as in HDRP's metallic workflow. Where a path meets a metal (bounced light, a reflection, what glass
  shows), it is shaded diffusely and can't follow the metal's own reflection, so the metal sends the light it reflects
  on as diffuse light of the same energy, its base color, as it did (one with a constant metallic kept only a fifth of
  it): with its diffuse color alone, metal armour, pots and weapons bounced no light and were black in reflections. Mask
  maps share the color textures' atlas, a 64x64 tile each, up to three quarters of it (the rest stays for color);
  BepInEx's scene line counts them. The game's Shader Graphs that tile their textures through properties of their own
  (the castle's architecture, cloth) are traced with that tiling: a wall's stone was traced as many times too coarse as
  the wall tiles it.
- Highlights on smooth surfaces: a 1e-7 in the GGX distribution's denominator capped the peak of any surface smoother
  than a roughness of about 0.12, so a glossy floor's, a puddle's, polished metal's and Reflection Boost's highlights of
  the sun and the lamps kept as little as 0.1 % of their light (and glass 2.5 %). The peak isn't capped now; the direct
  light's clamp still bounds a highlight. Harness: a lamp's highlight on a smooth floor carries as much light as on a
  rough one.
- Water Reflections (a new Video row, On by default): water reflects the traced scene. The game's lakes, rivers and sea
  are HDRP's water system, drawn after the traced light is applied, and take their reflections from HDRP's screen-space
  reflections, which only find what is on screen; the rest of a reflection came from reflection probes and the sky (and
  while tracing, dimmed by the light at the camera). Right after HDRP's screen-space reflections for water, the tracer
  fills that rest from the traced scene: the canopy overhead, the bank behind you, grass within 20 m, a lamp, whatever is
  off screen or hidden behind something. The traced reflection replaces HDRP's screen-space one entirely, its hits
  too (they miss what is off screen or hidden, fade at the screen's edges and on waves, and show the last frame's image):
  HDRP's texture is only where its water shader reads its reflection from. The water's own shading stays the game's
  (its Fresnel, colour, foam, refraction and fog). The reflections are traced from HDRP's water depth and wave normals and
  applied a frame late, as the traced light is; since no denoiser sees HDRP's water, they are averaged over up to 12
  frames and filtered over 5x5 pixels that stop at edges between near and far or bright and dark things (on reflected
  walls lit by bounced light, a grain of 0.05, against 0.17 averaged over frames alone and 0.59 in a single frame). Leaves
  and grass in them are lit through from behind, as on screen (a sunlit card seen from under it, 3.4 times brighter).
  Liquids and other see-through surfaces that take HDRP's screen-space reflections get the same. HDRP applies its
  reflection lighting multiplier to a whole reflection, so the traced part is divided by it, and it is pre-exposed with
  the previous frame's exposure, as HDRP reads it. Harness: a flat water plane reflects a light panel only the traced scene
  has, the red and the green wall and the sky through the ceiling's hole, each where the mirror sends each pixel's ray;
  every check fails with its piece undone. F8 adds a shot with HDRP's own reflections to compare, and its report lists
  HDRP's water surfaces. Water itself is never traced as a solid surface: a mesh renderer carrying a water material
  (which HDRP draws with passes of its own) is invisible to rays, so it can't shadow the lake bed.
- Emissive Lights (a new row, on by default): glowing surfaces (embers, lit windows, magic crystals) are sampled as
  lights by ReSTIR DI, triangle by triangle, instead of being found only by the bounced light's rays that happen to
  meet them. Each point of their light is counted once: the light sampling takes it as far as its reach (where it
  still gives 2 lux), a ray that meets it counts the rest, and a reflection counts it whole (ReSTIR DI gives it no
  specular light). Harness: the floor under a glowing panel converges to the same light on and off (within 0.1 %), a
  frame's error around its foot is 1.7 times lower with ReSTIR's reuse and 5 times with its temporal reuse alone, its
  direct light matches a Lambertian square's to 0.1 %, and its reflection is as bright as with the row off. The
  radiance clamps keep its light too: off, they cut it to 22 % as rare bright hits, on 98 % stays, so rooms lit by
  glowing surfaces come out brighter than before.
- A glowing surface right around a kept light (a lantern's glass round its candle, within the light's radius and
  25 cm more) is that light's surface and lights nothing itself, row on or off: the light has its light, and the
  lantern's surroundings got it twice. Reflections and glass still show it glowing.
- Emissive surfaces the scene capture saw move since its last scan (a carried lantern, a swinging door), characters,
  stand-ins and the ones past a budget of 16,384 triangles (the brightest and largest are kept) are left to the rays
  that meet them. The Emissive debug view shows which glowing surface is which; the native log's 10 s line counts
  them, and BepInEx logs the first 40 emissive materials with their nits and names shaders whose glow the tracer
  doesn't read.
- Traced emission is what the camera sees: HDRP's exposure weight is applied (at 0 a glow looks equally bright at any
  exposure, so the light it stands for is its color over the exposure, about 10,000 times it at EV 13), and so is
  Albedo Affects Emissive; a single-sided surface glows from its front only (rays from behind it saw it glow too).

- Path tracing on or off: the Global Illumination mode (HDRP's direct light and shadows with the traced bounced light
  added) is gone, and with it Indirect Intensity. The Path Tracing row offers Off and On, On by default; a config set
  to Global Illumination or Full becomes On. Insert turns path tracing on and off.
- Direct light's ReSTIR reuse is NVIDIA's RTXDI library (3.1, compiled in): the tracer's own candidates (the lights
  whose range reaches a pixel, from its light grid) go into RTXDI's reservoirs, and RTXDI's temporal reuse, boiling
  filter and pairwise-MIS spatial reuse take them on, with the decorrelation NVIDIA and AMD ask for with their
  denoisers (permutation sampling with a new random number every frame). A light choice is kept for up to 20 frames
  (RTXDI's default), the final shadow ray is always traced, and spatial results never feed the next frame. The
  config's hidden "Direct light history" sets another length for testing.
- Bounced light's ReSTIR reuse is RTXDI's too: the tracer's rays find this frame's path, and RTXDI's temporal reuse
  (permutation sampling every frame, a search around the reprojected pixel, the reconnection Jacobian), boiling
  filter and spatial reuse take it on. A light path is kept for up to 8 frames (RTXDI's default; before, the
  tracer's own reuse was least grainy keeping one for a single frame, because it decorrelated its samples too
  little), and a path no older than 30 frames. The config's hidden "Bounced light history" sets another length
  (1 comes closest to the old behaviour).
- Lighting that changed with the camera's direction and caught up over a few seconds: the tracer's clamp on bright
  bounced light, direct light and reflections was measured on screen, against HDRP's auto exposure. A hall lit by a
  sunlit courtyard's bounce was mostly the clamp itself, about 80 times over it, so its light rose and fell with the
  exposure as the camera turned between bright and dark (and with a ten-second average of it, tried first, it rose
  and then sank over several seconds). The clamp is measured against the scene now: four times a white surface in
  full sun, or lit by the brightest light 1 m away, so sunlit bounce is never cut and nothing depends on where the
  camera looks. Traced light is stored relative to that reference, which keeps HDRP's physical units within half
  precision's range. A new harness check renders the same frames at two exposures; the old clamp failed it by 14%.
  The one-frame-old traced light is also put into the current frame's exposure before it is applied, and DLSS Ray
  Reconstruction gets HDRP's pre-exposure as HDRP's own DLSS pass hands it over, clamped to 0.2..2 as HDRP clamps it:
  at this game's exposures (about 0.0001 to 0.006) that is always 0.2, and NVIDIA's guide says Ray Reconstruction
  doesn't use exposure inputs anyway, so changes of exposure reach its history as changes of brightness.
- Lights are chosen by what they belong to, not what they are called: every light inside a flame, candle, lamp or
  torch prop is traced, including the ones this game names PointLight_Ambience (the room light of its candlesticks,
  wall candelabras and torches). A candlestick whose only light has that name lit nothing, while its neighbour's
  PointLight_Cookies_01 lit the whole room. Fill lights outside any source prop stay out, as do the player's own light
  and lights for fog.
- Lights fall off as physical lights do: traced as far as they still give 50 lux (inverse square, up to 40 m), not to
  HDRP's range, an artist's cut-off that made a chandelier's light stop 2.7 m below it (above everyone's heads) and a
  candle's 0.5 m from its flame. HDRP's fade with the camera's distance isn't applied to traced lights either: a lamp
  past it went dark and cast no shadow, then came in as you walked up.
- Conversation Lighting Overhaul's dialogue light is traced: it hides its light from the lookup the tracer used
  (HideFlags.DontSave), so faces in dialogue got none of it while HDRP's own shading of hair did (white hair).
- Rays see no sky indoors: interiors float in the sky above the world (the tavern at 500 m), and a ray slipping through
  a seam between walls and floor found the open sky, about 100 times brighter than the room: thin blue-white lines along
  wall and floor edges, and sparkles.
- The lights the tracer traces light what HDRP still shades itself (hair up close, glass) with the traced light's gain
  too, so those come out as bright as the traced surfaces around them.
- Indirect Color no longer clips: it scaled the bounced light's color around its brightness and clamped channels below
  0, which turned a candle's warm bounce off brown wood pure red at 3 (green and blue at 1 to 5 of 255 across the
  tavern). It stops where a channel would reach 0, keeping the brightness.
- The traced light's gain keeps its brightest fiftieth at white (was the brightest tenth: in a tavern lit by candles
  most of the frame is dark, and a candle-lit table past that point went white).
- Direct light: the boiling filter weighs a sample by the light it gives (RTXDI's weighs its inverse probability, and
  the stricter filter dropped weak lights' light instead of fireflies); the shadow ray goes to a new point of the
  light's disc every frame (a penumbra's noise held for up to 20 frames, then jumped: fringe shadows flickered); and
  RTXDI's spatial reuse no longer discounts neighbours it skips (a brief darkening after disocclusions).
- The F8 capture holds the camera light and the gain still while it compares images, and its light table lists each
  light's position, traced reach, cookie and fade distance. It also saves the image twice a quarter of a second apart
  with the difference between them (what flickers or boils stands out), a new Specular debug view (the traced
  reflections alone), and names what the traced scene has under the crosshair (its mesh and material).
- NRD, a new Denoising choice: NVIDIA's Real-Time Denoisers (RELAX), the fallback DOOM: The Dark Ages uses when Ray
  Reconstruction is off, for GPUs that run neither Ray Reconstruction nor AMD's Ray Regeneration (a Radeon RX 7900
  XTX, for one). It denoises the bounced light, which the built-in denoiser smoothed and let boil; the direct light and
  the reflections keep the built-in denoiser's own filters, which hold the shadows sharp and stay still on
  normal-mapped surfaces (given to RELAX as well, the sun's shadows blurred to three times the error and a bump-mapped
  floor flickered three to five times as much). In the harness the error where the light is all bounced (under the
  ceiling) is 10 to 18 % below the built-in denoiser's, with about the same flicker. Denoising starts on Ray
  Reconstruction, then Ray Regeneration, then NRD, then Built-in, whichever this PC runs, and a pick of Ray
  Regeneration or NRD now stays while the game starts (whether they run was known only once a frame was traced, and
  Denoising followed the best one until then).
- A radiance cache (the Radiance Cache row, on by default): the bounced light reaching each part of the world is kept
  from frame to frame, and every path ends on it where it used to stop, so light keeps bouncing past the Bounces
  setting's last bounce, and a reflection or glass shows a surface lit by bounced light as bright as it is instead of
  by its direct light alone (a reflected wall in shadow was black). The paths fill it themselves: a quarter of the
  screen's tiles trace one more ray each frame from their paths' last surfaces, and the bounced light's first rays count
  for the surfaces they leave. Its cells are the world's, finer near the camera, one for each side of a surface; a
  camera cut keeps them, a new scene starts them over. In the harness's furnace (a closed box whose walls glow and
  reflect half the light that reaches them) one bounce ending on the cache comes to 0.994 of the walls' exact
  brightness, where paths alone come to 0.75 with one bounce and 0.97 with four, and a wall seen in another is twice
  as bright with it, as it should be; under the harness room's ceiling one bounce with it is as bright as four without.
  It takes 36 MB of video memory.
- See-through glass: the glossy texels of glass (HDRP's roughness below 0.5 there; a label or a cork in the same texture
  is rough and stays solid) have a glass's mirror finish and show what lies behind them, within 15 m; the game's
  materials are left as they are (raising a mask-mapped one's smoothness did nothing: its upper end is already 1, so
  stage 1's finish never reached the wine bottles). The ray is refracted into the glass
  (an index of refraction of 1.5) and out where it meets the glass again from inside, so a full bottle bends what is
  behind it as a lens; what it finds is shaded as a reflection's surface is, tinted by the glass's color and weighted
  by the light the Fresnel term lets in, beside the reflection, and the glass has no diffuse light of its own.
- Light scatters under skin: the traced light replaces HDRP's lighting, which HDRP spreads under skin after it lights
  it, so skin came out as hard as plaster, its shadows' edges sharp across a face. Surfaces with a solid diffusion
  profile (skin, teeth, the whites of the eyes) now have their traced diffuse light spread over them as their HDRP
  profile sets, per color channel (red farthest, as in skin), with the Burley diffusion and the filter radius HDRP's
  own subsurface pass uses, before the albedo is multiplied back in (so the skin's texture stays sharp). It keeps the
  light's total, and fades with distance as a profile's millimetres shrink below a pixel. BepInEx's "HDRP diffusion
  profiles" line gives each thick profile's radius.
- Glass (bottles, flasks, vials, potions, jars, glasses: known by their materials' names, since the game draws them
  opaque with the label and the cork in the same texture): rays pass through it by chance, as bright as its color is,
  whether they carry a shadow, the sky's or bounced light, so a bottle's shadow is as dark as the light it stops
  (clear glass casts none, a bottle of dark wine a dim one) instead of a hard black sliver. BepInEx logs each glass
  material once.
- Coloured glass colours the light through it: a candle's or the sun's light through a green bottle reaches the table
  green, not just dimmer. The direct light's final shadow rays (the light RTXDI chose, and the sun) and those at the
  paths' bounces take each crossing's color, channel by channel (a closed bottle's two crossings let through its
  color); RTXDI's reuse and the rays of the sky's and bounced light still pass glass by chance, as bright as its color.
  Only rays that meet glass pay for it. In the harness a cube of green glass lets through exactly its color of the
  light an opaque one stops, from the sun and from a lamp, grey glass 0.36 of it and clear glass all of it, and the
  bounced light under a sheet of green glass turns 1.6 times greener.
- The denoisers take see-through glass as the near-mirror it is, its light all in the reflections: DLSS Ray
  Reconstruction gets no diffuse albedo there, a specular albedo that covers the light the glass lets through as well
  as what it reflects, the tracer's roughness 0.03, and the distance to what is seen behind it as the hit distance, so
  the image seen through a bottle moves with what it shows; AMD's Ray Regeneration gets the same, with a material type
  of its own; NRD leaves glass alone, as it does the sky.
- Bottles reflect the candles and lamps around them, and the sun. Glass is nearly a mirror, and its highlights came
  from the direct light's samples, a point of each lamp's disc or of the sun's with the specular lobe evaluated there,
  whose peak is capped for a surface that smooth: in the harness 2 % of a lamp's mirror image reached a glass cube, and
  2.5 % of the sun's a glass pane. The glass's reflection ray now meets the lamps' spheres (the size the game gives each
  light, as its shadows soften by it) and the sun's disc itself, and shows their light where it does: 98 % of the lamp's
  and 94 % of the sun's.
- Dark Adaptation measures the view as it is seen, HDRP's fog included (a small copy of the image before
  post-processing, read back every fourth frame): a view the mist fills is not a dark one. Measured on the traced
  light alone, Sagremor Castle's night mist counted as darkness, the traced rocks and walls got four times the light
  against the fog, which HDRP draws as the game has it, and they showed through what the game's fog hides. Where
  nothing HDRP draws is brighter than the traced light (the tavern), nothing changes. The F8 report's exposure line
  gives the seen median.
- An effect's lights (a portal, a spell: kept for "VFX", "Magic", "Portal" and the like, with no flame or lamp in their
  names) light only as far as their HDRP range, as the game draws them: a portal's two 131,072 cd lights, a glow within
  0.6 m of it in the game, were traced 40 m far and lit the whole alcove around it white. Flames and lamps keep their
  physical reach: in the tavern and the exteriors captured so far, nothing else changes.
- Fill lights left out while tracing stop lighting the fog by HDRP's switch for that (affectsVolumetric) instead of a
  fog dimmer of 0: the game's light overrides (time of day, triggers) remember a light's fog dimmer when they start and
  write it back when they end, so an override that started while path tracing had it at 0 left that light's fog dark
  after path tracing was turned off. The game's own dimmers are no longer touched for them.
- Hair far away keeps its color: there a character has only its first hair pass, which the tracer shaded with the hair
  texture (the game colors hair in its shader), so it came out grey; camera rays leave hair to HDRP's hair shader now,
  as HDRP's transparent hair layer is up close. The hair still shadows the face. Camera rays stop at hair whatever the
  tracer's alpha test says (HDRP's own decided which pixels are hair): by the tracer's, a ray slipped between strands
  to the scalp behind them, and the far hair flickered between HDRP's color and the traced grey. Nor does a camera ray
  that misses a far character's few hair cards trace the head behind them: every pixel HDRP draws forward on a
  character (hair, brows, eyes) stays HDRP's, since its skin and clothes are drawn deferred.
- Hair's shadow lets light through between its strands: a ray stops at hair by chance, as much of a texel as the strands
  cover (its 256x256 alpha tile holds that coverage), so a layer of hair lets part of the light through. Cut out where a
  fifth of a texel was covered, every hair card cast a full, hard-edged shadow on the face and neck, as dark as a solid
  sheet. In the harness a card whose strands cover half of every texel casts 0.50 of the shadow the old rule gave. Hair
  gets no opacity micromaps, which would decide it by the cutoff again.
- Light through thin skin: ears, nostrils and fingers lit from behind glow red as HDRP lights them (the thin-object
  transmission the game's skin profile uses), where lit as a solid surface a backlit ear was as dark as the back of a
  head. The light is HDRP's own: its transmittance of the skin's thickness (the GBuffer's, remapped as the diffusion
  profile sets) through the profile's Burley scattering, tinted by its transmission tint, with HDRP's cosine wrapped 15
  degrees past the terminator; the subsurface scattering spreads it with the rest. The traced geometry decides whether any
  of it gets there: from just under the skin a ray towards the light must leave it within 3 cm (an ear's far side, a
  finger's) and reach the light unblocked, so a head behind a face lets none through, nor does a character the traced
  scene lacks. In the harness a backlit skin quad over a 4 mm slab gets 0.0622 where HDRP's formula gives 0.0623, three
  times as much red as green as its profile tints it, and over a 30 cm block nothing; lit from the front it is lit as
  before. BepInEx's "HDRP diffusion profiles" line gives each thick profile's tint and thickness range.
- Traced shadows on hair and eyes (the Hair Shadows row, on by default): HDRP shades them itself, with the lights the
  tracer traces but shadows only from the few it has shadow maps for (the sun's cascades, a fire or two), so a candle
  behind a wall or a head lit an NPC's hair while the traced face beside it was dark. Their color is now darkened to the
  share of their light the traced shadows leave: the direct light of the lights HDRP doesn't shadow, as much of it as
  reaches them, against the bounced light arriving there from the radiance cache. Never brightened, and nothing is
  shadowed twice: the lights HDRP shadows and the sun while it casts HDRP's shadows are left out. One shadow ray a pixel a
  frame, averaged over its neighbours and over time. Without the radiance cache, or with the row off, they keep HDRP's
  light, and so does anything drawn forward that the traced scene lacks (it may be unlit: a glow card, an impostor).
  Eyebrows are drawn after the traced light and keep HDRP's. In the harness a table HDRP shades is darkened to 0.46 of
  HDRP's light under a lamp a block shadows, and stays at 1.00 without the block, when HDRP shadows the lamp, and under a
  sun HDRP shadows.
- Characters are traced to 100 m (45 before: past it they floated in the sun, where HDRP's cascades used to shadow them),
  the nearest first as before, at most 64 renderers. Their acceleration structures are refitted to each frame's pose and
  rebuilt from it every fourth frame, each character on its own frame: 64 characters of 8,192 triangles cost about
  0.8 ms of GPU time a frame rebuilt every frame in the harness, and about 0.3 ms this way (the fastest frames of a busy,
  shared GPU).
- BepInEx logs a hair shader's colors and values once, for matching the color rays see on hair (grey now: the game colors
  hair in its shader) to the game's.
- The Denoising tooltip is one short line per denoiser.
- Denoising offers None, NRD, Ray Regeneration and Ray Reconstruction, whichever this PC runs: Built-in (the tracer's
  own filters) is no longer a choice, and NRD takes its place wherever the tracer denoises the image itself, also
  while Ray Reconstruction waits for DLSS upscaling or Ray Regeneration couldn't start. A config set to Built-in
  becomes NRD. Denoising starts on Ray Reconstruction, then Ray Regeneration, then NRD.
- The game's fog stays while tracing. The volume that turns HDRP's own indirect light, ambient occlusion, contact
  shadows and screen-space reflections off while tracing overrode every parameter of each of those, with HDRP's
  defaults, and its fog component (there to dim the fog's ambient light) turned the fog off: HDRP's default. The
  night mist over Sagremor Castle was gone and the void below it black. Each override now sets only the parameter it
  names, so transparent surfaces keep the game's screen-space reflections and reflection probes their intensity too;
  the fog isn't touched at all (the dimming answered a haze that HDRP's auto exposure made, which the mod no longer
  uses), and the reflections of what HDRP still shades itself are the game's own, times the light at the camera.
- For GPUs without NVIDIA's extras (a Radeon RX 7900 XTX), from a review of what the tracer assumed of the driver:
  - At start the tracer checks that the driver loads its textures through typed UAVs (the half and single precision
    formats, and R32G32_UINT for the surfaces), and says so when it can't instead of drawing garbage.
  - The D3D11 composite and Shadow Detail write HDRP's image in place only where its format is a floating point one
    that loads through a UAV; anywhere else they draw over it from a copy.
  - The built-in denoiser keeps its moments in 32-bit floats and carries the standard deviation instead of the
    variance: in half precision the variance of a bright spot (a lamp's surround) passed the largest value half
    precision holds, and a dim wall's fell below its normal range.
  - A ray retried past a surface it passes through never starts past its end (undefined in DirectX Raytracing).
  - The boiling filter sums its thread group once, not once per thread; the micromap bake counts texels in integers.
  - The Ray Reconstruction hit-distance texture is released when the resolution changes, like the others.
  - A runtime that failed to create a device is tried again on another GPU or driver, not only after a Windows update
    (read from the registry: loading dxgi.dll that early would bypass a ReShade proxy).
  - The GPU test expects DirectX Raytracing 1.2 exactly where the driver reports it, and runs on any GPU by vendor
    (TGPT_TEST_ADAPTER=1002 for AMD).
- Path tracing turned off gives every light it changed exactly the dimmers the game gave it: one record per light, taken
  the first time either change touched it (darkened as a placed light, or carrying the traced light's gain). With a
  list for each, a light that went from one to the other could be put back at the wrong value, 0 among them, and an
  A/B against the game's own lighting wasn't clean. The log says how many lights got theirs back.
- A Boiling Filter row (on by default, as before), for comparing: off, the bounced light keeps samples far brighter than
  their neighbours' that NVIDIA RTXDI's boiling filter drops. Against an honest reference (the plain path tracer without
  reuse, clamps or filters, 2,048 samples a pixel), ReSTIR's bounced light was 21 % short next to a light 10 cm from a
  wall, as a candle's by a wall is, and within 1.3 % of it without that filter; among 24 lights it matters about 1 %.
  (The first measurement, 1.6 times too bright, compared with a "no reuse" run that still had the clamps and the filter,
  which left it at half the true light.) Test switches for that measurement undo each of the tracer's changes to what
  it estimates: the boiling filters, the radiance clamps, the Jacobian's limits, RTXDI's surface position.
- A Crevice Light row (off by default), for comparing: the gap under a beached boat's canvas lining looked black in
  daylight. Measured in the harness on sand under a canvas 40 cm over it, against a hull and with a 12 cm slit at its
  front, against an honest reference: ReSTIR's bounced light is 14 % short at the gap's mouth, where NVIDIA RTXDI's
  normalization counts the pixels further in for light they can't see through the slit, and 9 % too bright deeper in,
  where a light path's history dropped at 30 frames restarts dark and then counts the next lit path up to three times.
  On, a pixel counts only for light it sees, histories are kept whatever their age, and the boiling filter brings a
  sample far brighter than its neighbours down to their level instead of dropping it (dropped, a quarter of the light
  deep in the gap went once histories last): the mouth comes within 1 % and deeper in 3 to 9 % short. It is noisier
  (15 to 30 % more error after denoising in the harness's room), so it stays off until it's seen in game. No light is
  added.
  Most of the gap's darkness is real: deep under the canvas the sand gets about 1/150 of the sunlit sand's light (7 to
  7.5 stops below it) followed to every bounce, and the 2 bounces traced by default hold 53 to 72 % of that (4: 90 to
  96 %; the mouth, 86 to 89 %). The rest is what closing paths with a radiance cache would add. Shadow Detail and Dark
  Adaptation leave a sunlit view alone by design; the game's own look lights the gap with its ambient probes, which
  don't see the canvas.
- A Shadow Detail row (0 to 100 %, 50 % by default): the dark parts of a dark, uneven view (a room lit by one candle,
  where 42 % of a frame was all but black and the median pixel sat 7 stops under the brightest) are lifted by a bounded
  curve below a mid tone, and everything brighter stays as it is. On its own it acts only where the traced light's
  exposure brightens a view and its brightest fiftieth stands 4.5 to 6.5 stops and more above its median, so a lit hall
  and a sunny street are left alone; it eases in over a second, while the row's value applies at once (0 against 50 %
  compares the two). It works on HDRP's finished image after every denoiser and the upscaler, before its
  post-processing and tonemapper: a curve before Ray Reconstruction would have biased it (the average of a lifted noisy
  pixel is darker than the lifted average). No light is added. The F8 report now also names the game's tonemapper and
  color adjustments.
- NRD, after a review: its motion is NRD's 2D motion (the view depth's change it computed took the camera's move twice:
  HDRP's previous view-projection already takes positions relative to this frame's camera, which AMD Ray Regeneration's
  guides now use too); its bounced light is in units of its own that hold until its history starts over (the tracer's
  units follow the scene's brightest light, and NRD's history mixed them as lights came and went); a frame NRD can't
  denoise records nothing of it and shows the bounced light as traced; and a saved pick of NRD or Ray Regeneration
  stays while the game starts (the GPU's denoisers are known before the device counts as ready). A camera cut starts
  every history over, for every denoiser: another camera, HDRP's own reset, a jump of 20 m or a turn of more than 60
  degrees in one frame (a dialogue cutting between speakers). The package carries the notices of what the DLL compiles
  in (NVIDIA's RTX SDKs license for RTXDI and NRD, MIT for NRD's MathLib and ShaderMake reader), and Ray
  Reconstruction's on-screen warning stays quiet while FSR or XeSS upscales (the built-in denoiser serving is the
  Denoising row's own fallback then).
- The F8 capture's images look as the screen does: scaled down from a screen wider than 1920 pixels (through an sRGB
  render texture), they went through the sRGB curve a second time, which lifted every shadow and washed them out.
- DLSS Ray Reconstruction works again: the reflections' hit distance handed to it last night is one channel, which
  TG Better Upscaling read through a color view that has none, so it refused every frame until Ray Reconstruction
  turned itself off for the session, and the built-in denoiser took over (smeared, blurred, with a trail behind moving
  heads). It is read as a single float channel now, trouble with those optional inputs drops them for the frame
  (never the frame), and should NGX refuse them, Ray Reconstruction runs on without them. The upscaling smoke test
  now hands them to Ray Reconstruction with every frame, and TG Path Tracing says on screen when Ray Reconstruction
  is its Denoising setting but hasn't denoised for 5 seconds of traced frames (not while the game loads: it first
  said so at the main menu, before anything was traced).
- Less boiling with many lights: the lights a pixel's candidates come from are drawn by how much each can matter there
  (its luminance over its squared distance from the light grid's cell), where they were drawn evenly. With lights
  reaching as far as physical lights do, a cell lists dozens (up to 43 in the tavern, 11 on average), and a room's
  chandelier came up as rarely as the faintest candle glow: its light was noise that boiled and that Ray Reconstruction
  smoothed away. In the harness's night scene the direct light's error is 8 % lower (27 % without spatial reuse) and
  the bounced light's 6 %.
- Exposure and Dark Adaptation rows in the Video tab (and FoA Mod Manager). The traced light's exposure starts from the
  game's own and never goes darker: metering sunny exteriors down to a mid tone left them underexposed (at half to four
  fifths of the game's exposure). Dark views are brightened part of the way to a mid tone, 50 % by default (Dark
  Adaptation: fully adapted, a candle-lit tavern looked nearly as bright as day, and at 70 % still a little too
  bright), with the brightest fiftieth of the view at most 1.4 times white (at white, a sunlit floor in view left a
  shaded stall black). Exposure adds up to 3 stops either way; the config's hidden "Auto exposure compensation"
  carries over to it.
- The eyes are HDRP's again, irises included: traced, they took the sclera's texture over the whole eye (the iris is
  its own texture, refracted under the cornea) and came out white; left out of the traced scene, the camera ray
  looking for them went on to the eye socket just behind, and the tracer lit its skin in their place, white again.
  Camera rays stop at the eyes now and leave them to HDRP (a new material flag), which lights them with the lights
  the tracer traces, with its gain, and no longer with the placed lights and the sky's ambient light that made them
  glow.
- Lights' cookies are traced: HDRP's mask on a light's intensity by direction, which the game's candle and torch lights
  carry (PointLight_Cookies), is baked into a cookie atlas (a point light's cubemap as an octahedral map, a spot's
  texture as it is) and shapes the traced light as HDRP's. And a flame's light is traced at most 30 cm across (a fire's
  1 m): HDRP uses a light's radius only for highlights and soft shadows, where the tracer lit surfaces from a sphere of
  that size, and the game gives candles up to a metre and more.
- Hair up close is lit by the tracer: HDRP draws it as an alpha-tested first pass, which the tracer finds and lights,
  and a transparent layer over it, which the tracer can't find, so HDRP lit that with its own lights alone (black
  outside dialogue, white in a dialogue light). While tracing, hair that has both loses the transparent layer (its
  first pass shows); eyebrows, only a transparent layer, keep theirs.
- DLSS Ray Reconstruction gets the traced reflections' hit distance (for reflections as the camera moves) and the color
  before transparent surfaces and particles were drawn (for candle flames, sparks and glass, which carry no traced
  light), both written by the composite as it applies the traced light; TG Better Upscaling passes them on (looked up
  by name, so either mod works with an older copy of the other). Its diffuse guide leans towards the light's color
  where a pixel emits light or is sky, as NVIDIA's RTXPT guides it.
- What HDRP still lights with the sky (transparent surfaces, fog) is scaled by how much sky the camera sees, measured
  alone: the lamps' light the probe's paths found counted too, as if the sky were there.
- Fewer sparkles: a path vertex takes a light's light as if at least 25 cm away (a candle's own holder got I / d^2 from
  centimetres, rare very bright samples among dark neighbours), and what Ray Reconstruction takes is kept below 256
  times white, as NVIDIA's RTXPT clamps its input. ReSTIR's reuse isn't permuted on thin or finely detailed surfaces
  (a neighbour's normal turning more than 35 degrees: strands, slats, blades), as NVIDIA's RTXDI sample does.
- Opt-in, for testing (hidden settings): "Direct light visibility reuse" keeps whether a pixel sees its light in
  ReSTIR DI's reservoirs (RTXDI's initial visibility with its ray-traced bias correction): 7% less error at night in the
  harness, but a 2 cm lamp's shadow edge 4 pixels wide instead of 0.8. "Spatial reuse feeds the next frame", as
  RTXDI's sample does: 6 to 10% more error in the harness, flicker about the same.
- Characters are traced to 45 m (30 before: their shadows popped in as you came closer).
- Alpha-tested textures keep their coverage: each texel of the 256x256 alpha tile holds how much of it passes the
  material's cutoff (16 samples of the finer mip), where averaged alpha thinned strands and a rug's fringe threads
  below the cutoff, so they came and went as the shading points moved. Hair is still cut out where a fifth of a texel
  is covered, now measured as coverage.
- The traced light has its own exposure: the game's exposure (a fixed EV 13 to 14 in a tavern, a physical camera
  outdoors) is set for its own fill lights and baked light, which the tracer leaves out, and with only the real sources
  lit its interiors were nearly black. HDRP's auto exposure on the traced image brightened them (to EV 7 at the least),
  but everything HDRP draws itself is made for the game's exposure and blew out with it: flames, fire, glows, fog lit by
  lamps, hair in a dialogue light, the sky through a door. HDRP keeps the game's exposure now, and only the traced light
  is brightened, adapting as an eye does: its median pixel towards a mid tone, but never so far that its brightest tenth
  passes white, and at most down to the darkest scene EV the config sets (7) from the game's exposure. The config's
  hidden "Auto exposure compensation" and "Auto exposure darkest scene EV" tune it; the 10-second log line and the F8
  report show the gain and what it was metered on.
- Lights left out as placed to fake light no longer light anything HDRP shades itself (hair, eyes, transparent
  surfaces) or its fog: in a tavern lit by them, hair and eyes shone in the dark, and its fill lights (reaching 46 m)
  filled the room with a red haze.
- The indoor clamp on bright light is measured against the brightest light 25 cm away instead of 1 m, so the table
  under a candle and a fireplace's walls feed the bounced light again.
- Streaks along the screen edge on a quick turn: pixels a turn revealed took their light from one traced texel per
  column. They take it from a few picked at random nearby now, which Ray Reconstruction removes as noise.
- Blotchy shadows across faces: characters' scalp and beard masks (stubble and hairlines drawn as a shell on the
  skin), cut out coarsely for rays, shadowed the skin under them. They are left out of the traced scene, as lashes
  and eyebrows are.
- Skin lit like a leaf: HDRP's transmission with subsurface scattering, which the tracer lit through from behind as
  it does grass and leaves, is also what characters' skin uses (for light through the ears). Lit that way, faces had
  hard, dark patches across the cheeks and came out pale. A diffusion profile HDRP transmits through as a thick object,
  or one named as skin, is now lit as a solid surface (the game sets its human skin profile to Thin Object, as it does
  its foliage, so HDRP's mode alone left skin lit like a leaf). The log lists the game's profiles and which are solid,
  and the Surfaces debug view shows such pixels in orange. The harness's check that leaves are lit through had put its
  quad in the back wall's shadow and passed on noise; it's in the sun now, with a check for solid profiles.
- Sparkling light ("Christmas lights") in dark rooms: the boiling filter, which drops a light sample far brighter than
  its neighbours' before ReSTIR spreads it, counted the sample itself in the average it was compared with (as NVIDIA
  RTXDI's own filter does). In a group of 64 pixels with 41 or fewer lit ones (a candle-lit room, bounce rays that
  mostly find darkness) it could never drop anything. It compares a sample with the others now, once at least 4 of them
  are lit.
- Bounced light reused from a neighbouring pixel is weighted as NVIDIA's RTXDI sample weights it: a sample from very
  different geometry is dropped (beyond 10 times either way) and the rest limited to 3 times either way, where up to 8
  times before made sparkles near corners.
- Shadows of alpha-tested fringes and leaves flickered: the alpha test read its 256x256 alpha point by point, so an
  edge stepped from texel to texel as the camera's jitter moved the shading points. It is filtered between texels now,
  and the opacity micromaps are baked from the same filtered alpha (the harness still finds them identical to the
  alpha test).
- DLSS Ray Reconstruction's albedo guides are never both near black (NVIDIA's RTXPT keeps their average above 0.05):
  it divides the light by them.
- The probe key (F8) now captures what the tracer sees: hold still for about 8 seconds while it saves the image, each
  debug view, the image with the sky's light off and with rays not passing through near back faces (the two ways light
  can leak along a seam), and the game's own rendering with path tracing off, with a report of the exposure, every
  light within 40 m (traced or left out, and why) and what the game has under the crosshair. One folder per press in
  BepInEx/TGPathTracing/Captures.
- The dialogue light holds its traced slot while it fades in, so its traced light follows HDRP's from the start.
- Debug view "Surfaces": how HDRP draws each pixel (its material type; for a surface HDRP draws forward, cyan where
  the tracer found it along the camera ray and lights it, magenta where HDRP's own shading stays).
- Hair, beards and other surfaces HDRP draws forward are path traced too: the tracer finds them along the camera ray in
  its own scene and lights them like the rest, and the composite replaces HDRP's own shading there (which gave them
  the sky's ambient light: grey-teal hair and white beards in a candle-lit tavern).
- HDRP's fog is lit only by lights while path tracing: its ambient term (the sky's light, with no walls in between)
  filled the tavern with a bright teal haze once auto exposure adapted.
- Shadows no longer change as you walk up to a statue or a well: within 30 m the tracer traces an object's finest LOD
  whatever the game draws, registers it from 40 m on, and keeps a LOD it has while the one it wants is not ready (the
  object dropped out of the traced scene for a moment at each switch, its shadow vanishing and coming back).
- Indirect Color defaults to 3 and Reflection Boost to 0.2.
- Eyes are path traced: they are in the traced scene for camera rays only (every other ray passes through them, as
  solid surfaces they stopped the rays leaving the face around them), and lit like the face instead of glowing with
  HDRP's own light. So are HDRP's opaque Unlit decals within 60 m (debris on floors shown at their texture's
  brightness whatever the light: they glowed in a dark room).
- What HDRP still lights itself with the sky (transparent surfaces such as glass, smoke and water, and fog) gets as much
  of the sky's light as reaches the camera: the tracer measures the light arriving there against the open sky's along
  the same 256 rays each frame (1 in the open, low indoors) and scales HDRP's ambient light, reflections and fog
  ambient by it, eased over half a second. Glass on a tavern floor glowed with the sky.
- The air above candles and fires no longer boils: HDRP's distortion (their heat shimmer) is off while path tracing is
  on screen. It resampled the traced light, which Ray Reconstruction takes undenoised, and shifting its noise around is
  what NVIDIA's guide warns breaks the denoiser. The camera's own settings come back when path tracing stops.
- Found in two independent reviews of the RTXDI move (Claude Opus and GPT-6-astra), each checked against RTXDI's source
  and the harness:
  - Direct light could go dark near the edge of a lamp's range and blotchy wherever a pixel kept its own light choice:
    RTXDI's pairwise spatial reuse counted empty neighbours (out of every lamp's range) against the pixel's own
    light, and its Basic reuse (RTXDI's default, used now) returns a light only when a neighbour's wins, which the
    shading pass didn't allow for.
  - Temporal reuse weighs a light as it was in the previous frame (a torch that flickered, a light that just came
    into range), and a light slot another light took over is no longer taken for the same light.
  - The boiling filters are the tracer's own now: RTXDI's assume waves of at least 32 lanes and read and write past
    their shared memory on GPUs that run 8 or 16 (Intel's).
  - A bounced-light sample that became infinite or NaN is dropped instead of staying forever and spreading.
  - History starts over after frames that went untraced (path tracing turned off a while, a failed frame).
  - Bounced light no longer borrows samples from whatever surface was at a pixel before when a turn of the camera
    reveals a new one (RTXDI's fallback, which it describes as biased, usually darker).
  - Bounce rays a normal map points into the surface count as finding no light instead of being mirrored out, which
    counted their mirrored directions twice (up to 50% too bright on steep normal maps).
  - "Direct light history" 1 no longer turns off direct light's spatial reuse.
  - Direct light's visibility reuse (dropping a light a pixel doesn't see) stays off: measured with RTXDI's spatial
    reuse, it widened a lamp's shadow edge from under a pixel to 14 pixels, for 13% less noise at night.
- Hard, jagged shadow edges across faces where they turn from the sun: an opaque surface's shadow ray started some
  4 cm inside it wherever its own flat triangle faced away from the light (a rule meant for leaves lit from behind),
  and the jump drew the edge. Opaque surfaces start outside now, and the light fades out towards that edge (Chiang,
  Li and Burley 2019, "Taming the Shadow Terminator"). Shadow rays near a surface also could skip a real blocker
  nearer than something they passed through; there the nearest hit decides now.
- The castle's walls, arches and battlements are traced as the game draws them: they are Entities Graphics
  entities without Drake, whose meshes and materials Entities Graphics registers at runtime, and the tracer now
  reads them from its registry. The shadow-only meshes that stood in for them (coarser copies the game casts
  shadows with) are left out: they cast shadows the drawn walls don't (a battlement's shadow on the walkway with
  blocks that aren't there) and let sunlight leak into corridors where rays passed through them.
- Hard-edged shadows across faces: rays leaving a face into its own flatter triangles where it turns away from
  the sun were stopped by the inside of the head, because the head's object also had alpha-tested hair. Whether
  a back face is a real hit now depends on its material (alpha-tested cards), not on the object. Harness: a slab
  in an object with an alpha-tested material turned 3052 floor pixels black before, 1 now.
- A third mod built on TG Graphics Bridge: ReSTIR path traced lighting on the bridge's Direct3D 12
  device with inline ray tracing (DXR 1.1, Shader Model 6.6). Deferred surfaces are lit by the tracer:
  ReSTIR DI over the lights and the sun with traced shadows, traced bounced light (ReSTIR GI) in place of
  HDRP's indirect diffuse light, and traced reflections. An SVGF-style denoiser, and a composite one frame later through the next frame's motion vectors, so
  Direct3D 11 never waits for Direct3D 12 work of its own frame.
- The traced scene is captured from mesh renderers, Awaken's Drake ECS world, Vegetation Studio Pro's
  trees and large objects, and Unity terrains near the camera; meshes are read back from the game's GPU buffers once and built into acceleration
  structures on the render thread, materials reduced to base color, texture (a shared atlas),
  emission and alpha testing.
- Config section Path Tracing (on or off, bounces, ReSTIR reuse, ray distance, scene radius, debug views);
  Insert turns it on and off, Shift+Insert cycles the debug views; a performance overlay section.
- `tests/pathtracing_gpu_test.cpp` runs the whole path on this machine's GPU with a synthetic HDRP-style
  frame.
- DirectX Raytracing 1.2 where the GPU has it (Shader Model 6.9, raytracing tier 1.2): opacity micromaps
  for alpha-tested meshes, made on the GPU from the same atlas texels the alpha test reads, and Shader
  Execution Reordering for the GI and reflection passes on GPUs that reorder. TG Path Tracing ships
  Microsoft's Agility SDK runtime (D3D12Core.dll 1.619), which TG Graphics Bridge points the game's
  Direct3D 12 at when Windows' own is older; the bridge only takes one from inside the game folder,
  Microsoft-signed, before anything has loaded Direct3D 12.
- Denoisers: DLSS Ray Reconstruction through TG Better Upscaling on NVIDIA GPUs (the tracer writes its
  guides and hands its light over undenoised, with ReSTIR's temporal reuse permuted), AMD FSR Ray
  Regeneration on a Radeon RX 9000 (AMD's signed DLLs ship in `Native/FidelityFX`), the built-in
  SVGF-style denoiser elsewhere. Ray Regeneration is untested: no RX 9000 was available.
- The mods together: TG Better Upscaling starts NGX for Ray Reconstruction only when TG Path Tracing asks
  for it (tracing, Denoising on Ray Reconstruction); RenoDX's DLSS 5 works alongside Ray Reconstruction. A refused Ray
  Reconstruction frame gives TG Path Tracing its own denoiser back (with fresh history), a feature created in
  a list that never ran is created again, and refusals count against successes. With TG Frame Generation, NGX runs as Streamline's session on the shared device, which never
  looked in TG Better Upscaling's folder, so Ray Reconstruction could not start (0xBAD0000B); TG Frame
  Generation now lists that folder after its own when TG Path Tracing is installed. The bridge stops using
  an extension's Agility SDK runtime on the next start if Direct3D 12 could not create a device with it.
- TG Path Tracing's options are Video settings rows (Path Tracing, with its options nested under it), mirrored
  exactly in FoA Mod Manager. Denoising offers None, Built-in, Ray Regeneration and Ray Reconstruction, each
  only where this PC runs it, and starts on the best (Ray Reconstruction where it runs, RenoDX's DLSS 5 loaded
  or not). It replaces the Denoiser setting (Auto, None, Built-in): a pick carries over, Auto becomes the best.
  Indirect Color scales how much color the bounced light keeps, at the same brightness (1 as traced, 2 by default).
- TG Path Tracing's built-in denoiser keeps sharp shadows and leaves less grain, measured outside the game against
  an undenoised converged frame (48 frames of 8 rays a pixel, averaged): the sunlit floor's error fell from 10.6% to
  4.4%, and the box's and the checkered leaf's shadows are sharp again. The direct light (the sun and every lamp)
  is a signal of its own with a short filter, apart from the bounced light, whose heavy filter blurred a chair's or a
  rope net's shadow away; the filters are steered by the noise of the accumulated light, not of a single frame, and
  no longer by a 3x3 spread that took every shadow edge for noise; ReSTIR DI reuses only lights a pixel sees
  (visibility reuse). ReSTIR's temporal neighbour is permuted for the built-in denoiser too, and reflections are
  filtered across normal-map detail by their roughness. The Temporal History option is gone: every denoiser
  averages over time and needs samples that change from frame to frame, and its old default of 20 left two to four
  times the noise of 1 on normal-mapped surfaces. ReSTIR now always reuses a sample for about a frame.
- Characters are in the traced scene: the game's GPU-skinned characters (Kandra) near the camera are posed
  in it every frame, from the game's own skinning output, so they shadow themselves, cast traced shadows and
  contact shadows, and darken the bounced light around them. Tested outside the game with a skinned box in a
  buffer laid out like the game's: it casts a shadow that follows its vertices frame by frame.
- Black patches and stripes on arches, vaults and battlements: the game casts its castle's shadows with
  shadow-only meshes that are mostly the drawn walls' own geometry but coarser on curves, and in places all the
  tracer has of a building. They are traced as stand-ins: a ray leaving a drawn surface passes through a stand-in
  within about half a metre, whichever way it faces, and farther away it shadows and bounces light as before.
  Large static sets drawn by Awaken's Medusa renderer are traced too, from its own instance data; meshes on
  layers the camera does not draw are left out.
- Grass blades and leaves facing away from the sun were black: HDRP lights translucent surfaces through from
  behind. Pixels with HDRP's transmission feature now take half the light arriving from behind them.
- Grass, plants, bushes, ivy and trees drawn by the game's own vegetation renderer (Leshy, which replaced
  Vegetation Studio) are traced: its loaded cells near the camera, at the LOD it draws each instance with
  (grass and plants within 20 m, at most 60,000 instances, nearest cells first; billboards left out), so grass
  shadows itself and the ground under it.
- Only lights that stand for something that emits light are traced: a flame, a candle, a brazier, a lamp, a
  lantern, a magic effect (the light or one of its parents names one). The lights placed to fake the light raster
  rendering lacks are left out, since the bounced light is traced and they lit rooms a second time: fill lights
  (PointLight_Ambience, groups of plain "Point Light"s, unnamed lights up to 25 m across), the light that follows
  the player and light meant for volumetric fog. A fill light inside a lamp's prefab is left out too; Conversation
  Lighting Overhaul's portrait light stays. HDRP still lights fog, transparents and hair with every light.
  The 10 s log line counts them, and F8 says of each light reaching a point whether it is traced.
  The log lists the scene's lights by name and its light-effect meshes once a session.
- Forward-drawn surfaces (eyes, hair: HDRP's Eye and Hair shaders) got no ambient light and were black in
  shade: they take the game's own ambient light again, after the GBuffer is written.
- Hair casts traced shadows: strands a few texels wide averaged out in the 256x256 alpha tile and let light
  through; hair is cut out at a fifth of a texel covered.
- F8 writes to the log what the game has under the crosshair and the lights reaching it.
- Characters' eyes were black: the eye the game draws lies just inside the traced eyeball, so every ray from it
  started inside. Eyes are left out of the traced scene, as is the game's invisible material (TG/NoRender, which
  hides body parts under armour and was traced as a solid surface).
- Characters are traced with their textures: their texture coordinates are read with their triangles, so bounced
  light takes their colors and hair and torn cloth are cut out texel by texel (skin never is).
- Black patches in the shape of the traced triangles on arches, battlements and walls: where the game draws a
  surface a few centimetres inside the mesh the tracer has (an arch drawn as a smoother curve than its triangles),
  every ray from it started inside the traced mesh and found no light. A ray from the drawn surface that first
  meets the inside of a mesh close by now passes out through it. Tested with a traced-only slab around part of
  the floor: none of its pixels turn black (3075 did without the change).
- Lamp shadows are crisp: lights keep the size the game gives them (HDRP's Radius, normally 2.5 cm) instead of
  at least 10 cm, and a pixel weighs every lamp near it once (or eight spread evenly) instead of eight drawn at
  random with repeats. Harness: a 2 cm lamp's shadow edge is 0.8 px wide against 4 px for a 10 cm one.
- Black, flickering patches in the shape of walls, arches and floors: the scene is gathered again half a second
  after the camera has moved 4 m (it was every 2 seconds), so a coarser level of detail no longer stands in for
  the one on screen while walking up to it.
- Characters' hair and transparent parts are left out of the traced scene; they were traced as black, solid
  cards (shadows on faces, dark blotches on clothing).
- Alpha-tested materials (straw, rope nets, leaves) are tested against a 256x256 copy of their texture's alpha
  instead of the 64x64 color tile, which made coarse opaque blocks of them: black, flickering patches under
  straw, and no rope-net shadows. Opacity micromaps are baked from the same texels.
- Reflection Boost: makes every surface glossier and more reflective; 0.15 by default, 1 nearly a
  mirror to judge the traced reflections.
- DLSS Ray Reconstruction: TG Better Upscaling never told TG Path Tracing that it ran (its "no failure yet" start
  overflowed), so TG Path Tracing kept denoising its light itself and Ray Reconstruction denoised it a second time.
  It asks for preset F, the SDK's newest model ("Default model RR2"), in every quality mode.
- While path tracing is on, the game's Video rows it replaces are greyed out with the reason as their
  tooltip: Shadows and their distance, Contact Shadows, Ambient Occlusion, Reflections and Subsurface Scattering.
  They follow the Path Tracing row before Apply, keep the game's own conditions, and apply again once path tracing
  is off; HDRP's ambient occlusion, contact shadows and screen-space reflections are off
  meanwhile, and its shadow maps still cast light shafts in fog. Characters and the player's hands are deferred
  (the game's character shaders draw into the GBuffer): the traced light shades them, though they are not in the
  traced scene.
- TG Frame Generation (shipped in 1.2.3): with mods fighting over the cursor in a menu (FoA Mod Manager open in game),
  frame generation resumes only once the cursor has stayed locked for half a second, not for single frames, which
  switched it on and off with hitches.
- Lighting fixes from the first test builds in the game: the capture no longer stops at HDRP's
  transparent SSR call, the composite recognises the game's device behind ReShade, a light grid with
  resampled light choice replaces a uniform pick among all lights (the fireflies), geometric normals and
  a spatial variance estimate remove the grain a longer temporal history added, area-light shadow rays
  replace hard point-light shadows, and Rays Per Pixel (1-8) trades GPU time for less noise.

Fixes from the first troubleshooting reports (shipped in 1.2.3, 2026-10-09)

- TG Better Upscaling's Upscaling row names FSR by the version that runs on this GPU, "FSR 4" on a Radeon RX 9000
  and "FSR 3.1" elsewhere (the major and minor number of AMD's version name), in the Video tab and in FoA Mod
  Manager alike, with the full version (3.1.5, say) in the tooltips of FSR's Quality Mode and Sharpness rows and in
  the log. The config still stores "FSR", so a saved choice stays valid on a GPU that runs another version.
- The game's own Framerate Limit row offers 45, between 40 and 50: 90 frames on screen with 2x frame
  generation, which the player on the RTX 4060 laptop asked for. It is an entry of the game's row, applied
  as the game applies its own limits, and like them it is unavailable while VSync is on; it is also
  unavailable in Exclusive Fullscreen, where the game would ask the display for 45 Hz. The game saves
  the row's choice as a position in its own list, which it builds without 45 at every start, so the
  mod keeps that position as the game counts it and chooses 45 again when the player left it there.
  Without TG Frame Generation, the row is the game's again and falls back to its default.
- The NVIDIA driver's Vertical sync set to On no longer gets an alert telling the player to use the
  3D application setting, and Max Frame Rate no longer gets one telling them to turn it off. A player
  on an RTX 4060 laptop found the game ran worse with the 3D application setting. NVIDIA's DLSS-G
  guide supports VSync from the driver (it overrides the game's), and with G-SYNC that is NVIDIA's own
  advice for frame generation. VSync with frame generation raises latency only where Windows composes
  the frames instead of flipping them straight to the screen, which on a laptop means a screen on the
  integrated GPU, and the "High latency" alert already covers that. A driver frame cap counts generated
  frames too, so one above what frame generation reaches does nothing to it. Vertical sync at 1/2, 1/3
  or 1/4 of the refresh rate still gets an alert, and the framerate check names the driver's VSync or
  Max Frame Rate as the cause only when the frames on screen are at them.
- The troubleshooting report has each test step's Pipeline line from the native log again, with
  Reflex's latency numbers. The filter meant to keep it dropped it every time.
- A moment out of play without a menu, such as an autosave taking its picture with a camera of its
  own, no longer pauses the test run: the step's measurement starts over, and the run pauses only if
  it lasts a second. Pausing for an autosave put the player's settings back and the step on again a
  third of a second later, rebuilding frame generation's swapchain twice.
- The Pipeline line's `d3d11Waits buffer=` count equals the presents in a D3D11 game, which renders
  every frame into one back buffer, not rarely as the 1.2.0 notes below say. The `inputs=` count is
  the one that shows D3D12 falling behind.

Troubleshooting (1.2.0)

- **Create Troubleshooting Report**'s tooltip and start message say what the test tries with the mods
  installed, and for about how long: each upscaler with TG Better Upscaling alone, each frame
  generation and low latency setting with TG Frame Generation alone, both with both. The report names
  the mods that took part.
- **Create Troubleshooting Report**, a button nested under Performance Overlay at the bottom of the
  Video settings, there with the overlay on or off (and a Video entry in FoA Mod Manager). Back in the game it runs a test of about two minutes:
  the player's own settings first, then each upscaler the game's Upscaling row offers with frame
  generation off, then frame generation off with each low latency technology, then each frame
  generation technology with each low latency technology it can run with (Off where that is allowed)
  at 2x, and once more at the highest multiplier where there is one. Each step is applied, left to
  settle and measured for a few seconds: the rendered frame rate and its 1% low, CPU, GPU and video
  memory use, frames on screen, the presenting and latency technology, and where the presentation
  bridge spends its time per frame. Then the player's settings come back and the report is saved as
  one text file in the BepInEx folder, beside LogOutput.log (`TG Troubleshooting Report <date>.txt`,
  about 25 KB), to paste into a bug report on Nexus. It has a summary (system, mods, problems found,
  the test results, what the native side knows), then this session's part of each log, filtered to
  what explains a problem: from BepInEx's log the mods' lines, warnings and errors with their stack
  traces and plugin loading; from the preloader's and native logs this session's lines (found by the
  process ID), with frame generation's 5 s pipeline lines thinned to the first and last few and one
  per test step; errors and warnings from Streamline's and the game's logs; the mods' config values;
  and where each of the mods' files is, with its size, date, version and whether it is a Vortex link.
  Repeated lines are counted, and the Windows user folder is replaced by `%USERPROFILE%`. A test step never writes the
  player's settings: frame generation's steps override what it requests, upscaling's steps select
  the row in the game but leave the mod's config alone, and when the game pauses the player's
  settings come back until play resumes, so the settings screen never shows a test value. If the game
  closes during a test, the next start puts upscaling back from the config. Selecting the button
  again stops the test and saves what was measured; before the test starts, it saves the logs alone.
- The mods' problems are shown on the Video rows they concern, not as rows of their own: the Frame
  Generation row for TG Frame Generation and whatever interferes with frame generation, the game's
  Dynamic Resolution row (the upscaler, which TG Better Upscaling extends) for TG Better Upscaling
  and whatever interferes with upscaling. Such a row gets an amber alert icon after its name and the
  most serious problem as its tooltip, as "Source: Label. What to do" in a sentence or two ("TG Better
  Upscaling: Wrong folder. Its files are in ..."). The full explanation goes in the report. The top of
  the performance overlay lists all of them the same short way, wrapped to under half the screen.
  Each mod tells the bridge when it has started; one that BepInEx loaded but that never started (an
  error while starting, before it could log anything, which this game's missing Unity log hides) gets
  a "Didn't start" alert on its row. When TG Frame Generation has a problem and no row of its own, a
  stand-in Frame Generation row reads Off with the icon and the reason. Tooltips name the game's two versions as the Nexus files do (the
  normal version, and the Mono one whose files end in "mono"), never by runtime names. They cover:
  - where a mod is installed wrong: its archive's BepInEx folder unpacked into BepInEx, into
    BepInEx/plugins (what a mod manager does with a plugin-type mod; with Vortex the fix is the
    Engine Injector type), into a folder of its own, or its contents straight into the game folder;
    a missing preloader or other files; the build for the other game branch; a plugin BepInEx found
    and didn't load; TG Frame Generation 1.0 still installed; a `disable-autoload.txt`;
  - other software that conflicts or limits: OptiScaler (running, or files left over), DLSS Enabler,
    the DLSSG-to-FSR3 mod, FSR 3 mods, Special K, RivaTuner Statistics Server with a frame rate limit
    (read from its profiles), Lossless Scaling running, and from the NVIDIA driver's profile for the
    game: Max Frame Rate, forced VSync, Smooth Motion (ID from NVIDIA Profile Inspector, not yet
    checked against a driver with it on) and a DLSS Frame Generation override that forces it off.
- When frame generation turns on and the framerate on screen doesn't rise by 15% over the rate just
  before, a message says so during play and a row in the Video settings explains it with what this
  session shows: the game's Framerate Limit, VSync (at the display's refresh rate or every second
  refresh), a limit outside the game, video memory nearly full, rendering that fell much more than
  frame generation costs (with Anti-Lag 2 on, hybrid graphics, or AMD Fluid Motion Frames as things
  to try), or nothing known, with the troubleshooting report as the next step. Dynamic mode that
  reaches its target counts as a gain.
- DLSS Frame Generation with VSync on, while the display is connected to another GPU than the one
  the game renders on (a laptop's panel on its integrated GPU), shows a "High latency" alert: NVIDIA's
  DLSS-G guide says high latency is expected with VSync on such a system. The fix it gives is VSync
  off, or the laptop's setting that makes the dedicated GPU drive the display.
- Frame generation's row reads Off, greyed out, with the alert icon when it can't run, and its tooltip gives the first thing
  that went wrong with what to do: the install problem, OptiScaler, a preloader that didn't run,
  another Streamline, and for a Direct3D 12 device Windows refused, why. 0x887E0003 names the Agility
  SDK versions, the Windows build, and whether the bridge corrected the game's request or something
  loaded Direct3D 12 before it could, but only when the bridge saw that mismatch. Otherwise it says
  the game's D3D12Core.dll is missing or doesn't match, and to verify the game's files. The row's
  own value stays one word, and the tooltip is the short form. When DLSS Frame Generation isn't offered on an NVIDIA GPU, the
  tooltip says why from Streamline's own answer: Hardware-accelerated GPU scheduling off, an old
  driver or Windows, or a GPU before the RTX 40 series.
- The native logs have more to go on: the Windows build, the game's adapter, driver version and video
  memory, which GPU drives the display (a laptop's hybrid graphics shows as the display on the other
  GPU), the NVIDIA driver settings above, and in frame generation's 5 s pipeline line, where the
  presenting thread's time goes per frame (waiting for the GPU, the SDK's Present, handing the frame
  back to D3D11) and the frame interval with the frames over 50 ms. New optional exports carry these
  to the plugins: `TGGraphicsBridge_ReadDiagnostics`, `TGGraphicsBridge_LogNote`,
  `TGFG_ReadDiagnostics`, `TGFG_ReadTimings`, `TGFG_LogNote`. The ABIs are unchanged.
- The bridge is 1.2.0, so a copy bundled with a 1.1.0 mod stays idle beside it.

Frame generation and compatibility fixes (1.2.0), from a review of hardware the mods haven't been
tested on (only an RTX 5080 on Windows 11 has been)

- The game's next frame no longer waits for frame generation to finish the current one. After
  every Present, D3D11 used to wait on the GPU until D3D12 had finished the whole present, frame
  generation included, before any of the next frame's work could run. With DLSS-G (which blocks the
  presenting queue until its work is done) and FSR (whose generation runs on that queue) the game's
  rendering and frame generation took turns instead of overlapping. That fits the reports of frame
  generation lowering the framerate on slower GPUs. D3D11 now waits only for what it is about to overwrite: a
  back buffer until D3D12 copied it, and an input texture until the present that used it three
  frames earlier retired. Usually neither has to wait; the pipeline line counts the times one did.
  Each direction has its own shared fence now (D3D11 signals the producer fence, only the D3D12 queue
  the consumer fence): a fence Signal sets its value, and with D3D11 no longer waiting for every
  retirement, a single fence signalled by both could have moved backwards.
- Reflex, XeLL and Anti-Lag 2 now wait in Unity's PlayerLoop right after
  `TimeUpdate.WaitForLastPresentationAndUpdateTime`: after Unity's own wait for the previous frame's
  presentation (Intel's XeLL guide wants other waits before XeLL's) and before any input is read,
  Unity's or Rewired's. Their SDKs ask for the wait just before input is sampled; it ran in a
  MonoBehaviour's Update, after the input it was meant to precede, which made the input each frame
  used older by the wait. If another mod replaces
  the PlayerLoop, frames begin in Update as before. The native frame ID is now a counter that rises
  once per frame instead of `Time.frameCount`, which can still hold the previous frame's number there.
- VSync is turned off for DLSS Frame Generation when the screen is connected to another GPU than the
  one the game renders on: a laptop's screen on its integrated GPU (the default hybrid mode of most
  gaming laptops), or a monitor in the motherboard's port. NVIDIA's DLSS-G guide expects high input
  delay with VSync and frame generation where Windows composes the frames instead of flipping them
  straight to the screen, and says it is better not to enable VSync there. Frames copied to another
  GPU's screen are usually composed, but not always: hybrid laptops can flip them straight to the screen
  too (the two-copy hybrid path, Windows 11's cross-adapter scan-out), where VSync off would tear. So
  while VSync is held off, the bridge presents without the tearing flag (`TGFG_SetNoTearing`, a new
  optional export): the newest frame shows at the next refresh, with no VSync wait and no tear either
  way. The mod turns the game's own
  VSync toggle off once, as a player would, and sets Target FPS to Unlimited: the game shares that row
  between VSync rates and frame limits and would otherwise move it to the nearest limit, a cap the
  player never chose. It greys the toggle out with a tooltip saying why and shows a short message; a
  limit the player picks afterwards stays. Choosing FSR or XeSS, or a screen on the rendering GPU, offers the toggle again; VSync
  stays off until the player turns it on. VSync the NVIDIA driver forces on can't be changed from the
  game: the "High latency" alert then says where to change it in the NVIDIA App. An advanced setting
  turns this off. Which GPU the screen is connected to is checked again every 5 s, so moving the
  window or a laptop switching its screen between GPUs is noticed, and Streamline's measured copy time
  between the GPUs is in the pipeline line (`crossAdapterCopy`).
- Alerts for memory while frame generation runs: "VRAM full" when the game stays over the video memory
  Windows gives it (with how much frame generation added), and "RAM full" when less than 1 GB of system
  memory is free or 95% is in use. Both make the game stutter as memory moves out and back. System
  memory also counts as a cause when frame generation gives no framerate gain, and the report lists
  free memory.
- DLSS-G's UI recomposition is off. It was on whenever a HUD-less image was supplied, but NVIDIA's
  guide allows it only when a UI buffer is tagged as well, and the bridge tags none. DLSS-G still
  gets the verified HUD-less image, and it no longer spends recomposition's memory and GPU time.
- DLSS-G below its minimum output size (`minWidthOrHeight`) is off until the resolution changes,
  as NVIDIA's guide requires, with the reason "resolution too low". The runtime's
  resolution-too-low status used to end frame generation for the session.
- NVIDIA Reflex is offered in Low Latency only when Streamline reports it available
  (`lowLatencyAvailable`: an NVIDIA GPU from Maxwell on with driver 456 or later). NVIDIA's guide
  requires this for the Reflex choice; DLSS-G still runs Reflex whatever the choice. A query that
  fails is asked again before the plugin reads the capabilities, and without an answer Reflex isn't
  offered.
- XeSS Frame Generation generates only from frames XeLL slept for, and only while XeLL's low latency
  mode is on. Intel requires XeLL with every XeSS-FG frame; the first frame after switching to XeSS
  began before XeLL existed. A sleep mode XeLL refused is not asked for again every frame (each
  attempt costs a GPU drain).
- The Base FPS limit with XeSS Frame Generation capped the frames on screen, not the rendered ones:
  XeLL's limit counts every Present. XeLL now gets it multiplied by the presents per rendered frame.
- XeSS Frame Generation and XeLL are loaded only where Intel supports them: Windows 10 21H1 (build
  19043) or later and the Visual C++ runtime 14.40.33810 or later, checked on the copy they would
  bind to (one already loaded, one next to them or the game, or System32's). Elsewhere the Frame
  Generation row's tooltip says which one is missing.
- The three native DLLs link the C and C++ runtimes statically. The game ships no `msvcp140.dll`, so
  they used whatever System32 had. Without the Visual C++ Redistributable they didn't load at all,
  and with one older than 14.40 the first `std::mutex` lock crashed the game: MSVC 14.40 and later
  construct `std::mutex` in a way older runtimes can't lock. They no longer call
  `DisableThreadLibraryCalls`, which Microsoft forbids with the static runtime, and their extension
  factories catch everything before it reaches the bridge's runtime. NVIDIA's, AMD's and Intel's DLLs
  still need the redistributable, so the bridge records the copy they would use (`windows.vcruntime`,
  in the report), a "Visual C++ runtime: Missing" alert appears when there is none in Windows or the game
  folder, and a Streamline that failed to load with Windows error 126 or 127 says which it was.
- The troubleshooting report is written whole and then renamed, so a full disk or the game closing
  leaves no half report. A log longer than the 8 MB the report reads says the session may have started
  before them, instead of that its native part didn't start.

Error handling (1.2.0), from a final review of every failure path

- No C++ exception leaves a native entry point: Unity's render-thread callbacks, the exports the
  plugins call every frame, the performance sampler's thread (which now starts over after an error)
  and the bridge's exports catch everything. The three native logs never throw, since they are called
  from catch handlers, some in functions that must not throw.
- A removed device signals its fences to their maximum, which also woke the CPU fence waits: they
  now check again after waking, so device removal is reported as such instead of passing as a
  finished frame. A bridge whose GPU work was never confirmed finished keeps its GPU objects for the
  rest of the session instead of freeing them under running work; the upscaler does the same on a new
  game device.
- A settings file that can't be written (read-only, locked by an antivirus scan, a full disk) no longer
  stops a mod: settings bind with BepInEx's saving off and every save goes through one place, which
  keeps the values for the session and shows a "Can't save" alert on the mod's row. Each plugin's
  start is guarded and logs why it failed, and the preloader starts the bridge even if it can't create
  its log folder.
- The troubleshooting test run starts only once its recovery file is written, keeps that file until
  TG Better Upscaling has put the upscaler back, reports a step that couldn't be applied as such
  instead of measuring it, says which mod couldn't put your settings back, and puts them back if the
  report's own code fails mid-run.
- After FSR or XeSS stops with an error, STP really takes over at the chosen render scale. The game's
  upscaling controller knows no FSR or XeSS, so handing it that choice turned upscaling off and
  rendered at full resolution. Video memory has to stay over 95% of the budget on two 10 s checks in
  a row before FSR and XeSS give way, not on one sample over 90%.
- TG Better Upscaling extends the upscaling row for DLSS's quality modes even where FSR and XeSS are
  unavailable. Its "Add upscalers to Video settings" setting says that the game applies its upscaler
  through that row.
- The game builds its display and VSync rows again when the window moves to another monitor; the
  Exclusive Fullscreen restriction and the VSync hold follow the new rows. An Apply press in the Video
  tab counts once, so a later Restore Defaults can't apply display rows the player is still editing.
- The display check follows the game's window when the swapchain is replaced on the same device.
  Anti-Lag 2's and XeLL's per-frame errors are counted and logged. GPU usage is the busiest engine of
  the game's GPU, as Task Manager counts it, not engines of one type added together.
- A disable-autoload.txt in the bridge's folder shows on both mods' rows. RivaTuner's frame limit
  and Lossless Scaling are checked again every 30 s, so their alerts clear once they are changed or
  closed.

Two mods and a shared bridge (1.1.0)

- The repository now builds two mods on a shared part. **TG Graphics Bridge** is the DX11-to-DX12
  bridge and its BepInEx preloader, taken out of TG Frame Generation: it owns the graphics hooks,
  observes the game's device, creates one D3D12 device inside the game's swapchain creation, and
  loads the native part of each mod built on it (`BepInEx/<mod>/graphics-bridge-extension.json`).
  **TG Frame Generation** is now one of those extensions. **TG Better Upscaling** is new.
- The bridge is bundled with each mod, not a download of its own, under the mod's own folders
  (`BepInEx/patchers/<mod>.Autoload.dll`, `BepInEx/plugins/<mod>/TGGraphicsBridge.dll`,
  `BepInEx/<mod>/Native/TGGraphicsBridge.Native.dll`), so installing both deploys no file twice and
  Vortex reports no conflict. With both installed, the preloader bundled with the newest bridge
  starts it once for both, and BepInEx loads the newest copy of its plugin; BepInEx logs that it
  skips the other copy.
- TG Better Upscaling adds AMD FSR (FSR 4 on Radeon RX 9000, FSR 3.1 elsewhere) and Intel XeSS to the
  game's own Upscaling row, after None, STP and DLSS, with the row's quality slider as the render
  scale, and an FSR Sharpness row under FSR. They run where HDRP runs STP, on HDRP's
  own color, depth and motion vectors, on the bridge's D3D12 device, and return the upscaled image in
  STP's output texture, so post-processing, RenoDX, ReShade and frame generation see an upscaled frame
  as they would after STP. A refused frame is enlarged instead of left black; five in a row turn that
  technology off for the session and STP takes over.
- FSR has no version row: it runs AMD's default for the GPU, which is FSR 4 on a Radeon RX 9000 and
  the newest FSR 3.1 elsewhere (3.1.5 on an RTX 5080, checked). AMD asks that a version is either
  left to that default or chosen by the player, never picked by the application; the native log names
  the version AMD chose and the newest it offers.
- A performance overlay, switched on in the last row of the Video settings (Performance Overlay, with
  Overlay Position and Overlay Size below it), FoA Mod Manager or TG Graphics Bridge's config: frame
  rate and frame times, render and output resolution, VSync and the frame cap, CPU, GPU and video
  memory use, and a section from each mod (the upscaler, its mode and version, the DLSS model and DLL;
  frame generation's technology, mode, multiplier and frame rate on screen). The DLSS model comes from
  the NVIDIA driver's profile for the game when the NVIDIA App overrides it (preset, performance mode,
  render scale, DLAA), and is the DLL's default for the mode otherwise.
- DLSS, FSR and XeSS get their own quality modes in the Video tab (Quality Mode) instead of the game's
  render-scale slider, which STP keeps. Only the modes each runtime reports are offered, at the ratio
  it reports: NGX's recommended size per DLSS mode through Unity's NVIDIA module (DLAA only when NGX
  sizes it), FSR's per-mode ratio from AMD's runtime, XeSS's seven presets from Intel's. DLSS gets the
  mode as its NGX quality level, so NVIDIA's driver picks that mode's model; the game had given DLSS a
  level derived from the slider's position. XeSS initializes with the chosen preset. A config without
  a mode starts each upscaler on the highest-quality mode at or below the game's render scale (never
  more pixels than before), and the game's graphics presets set the modes by the same rule. The
  upscaling ABI is 1.2 (`TGUP_GetModes`, `TGUP_Frame.mode`).
- FSR gets HDRP's exposure, as HDRP's own DLSS pass takes it (a readback requested every frame,
  limited to 0.2..2): as its pre-exposure, which it divides out so that its history holds while the
  game's exposure adapts, and as its exposure texture, so it still works on the color HDRP tonemaps.
  It had a fixed pre-exposure of 1, so every exposure change looked like a change in brightness to
  its history. XeSS, which has no pre-exposure input, keeps working on the pre-exposed color.
- With FSR, textures get AMD's recommended mip bias, `log2(render / output width) - 1`: HDRP applies
  `log2(render / output width)` wherever STP runs (Intel's guidance for XeSS), and FSR adds the
  extra -1 through the camera's material mip bias while it is selected.
- TG Better Upscaling brings NVIDIA's DLSS 310.9.1 DLL (`Native/DLSS/nvngx_dlss.dll`, from the DLSS
  SDK) for the game's own DLSS, which ships 3.8.10. When NVIDIA's runtime loads an older
  `nvngx_dlss.dll`, it gets the bundled one; an equal or newer file, such as a DLSS mod's, is kept.
  Nothing in the game folder is replaced. A DLSS mod's own set of DLLs is kept whole as well: with
  DLSS neural rendering (`nvngx_dlssnr.dll`, DLSS 5) or frame generation or ray reconstruction of the
  same release beside the game folder's `nvngx_dlss.dll`, that file stays, since the RenoDX DLSS 5
  add-on's frame rate stopped following the render resolution with ours in its place.
  Stage-Runtime.ps1 takes `-DlssRoot`.
- Only FSR gets a sharpness row, at 50% by default. DLSS has no sharpening any more (NVIDIA's headers mark it "not
  supported" and the game's DLSS 310 ignores it) and XeSS never had one.
- The bridge hooks the swapchain methods of DXGI factories up front, on a factory of its own created
  past any hook already on `dxgi.dll`'s exports, before any extension loads. Up to 1.0.x frame
  generation saw the game's swapchain only because something else created a DXGI factory where its
  hooks saw it, which happened with NVIDIA's runtimes loaded; with ReShade installed as `dxgi.dll`,
  which forwards the game's factory calls past later hooks, TG Better Upscaling alone never saw it, and
  frame generation on AMD and Intel GPUs most likely didn't either. Installed first, the bridge's
  hook also runs inside ReShade's, so ReShade leaves the bridge's D3D12 device and frame generation's
  swapchain alone, and the RenoDX DLSS add-on still gets a device of its own.
- When frame generation's native side reports nothing (it didn't load or start, another mod already
  loaded NVIDIA Streamline, or it couldn't take over the game's presentation), the Frame Generation row
  offers Off only, greyed out, and its tooltip says why. Up to 1.0.x it showed DLSS Frame Generation
  with multi-frame choices on any GPU in that case, which did nothing. A report that arrives later
  replaces those rows.
- With TG Frame Generation 1.0.x still installed, which runs a graphics bridge of its own, the bridge
  does not start and says to update Frame Generation to 1.1.0; two bridges cannot share the game's
  device.
- Frame generation and upscaling work on Windows 10 and older Windows 11 builds. The game's exe asks
  Windows for Direct3D 12 Agility SDK 610 from its `D3D12` folder, which holds D3D12Core.dll 611.
  Where Windows' own runtime is newer (Windows 11 24H2 and later), Windows uses its own and never looks
  there; where it is older, Windows loads the game's file and refuses every D3D12 device over the
  mismatch (0x887E0003, D3D12_ERROR_INVALID_REDIST), so up to 1.0.x frame generation never started
  there. Before any runtime loads `d3d12.dll`, which reads the request as it loads, the bridge sets the
  request to the version the game ships, and logs the three versions it found. Reproduced and checked
  on Windows 11 with an exe asking for 618 beside a D3D12Core.dll 619: 0x887E0003 before, a device
  from that file after.

Frame generation quality

- DLSS Frame Generation and Intel XeSS-FG receive depth, motion vectors and a camera in the same
  orientation as the frame they interpolate. HDRP renders Y-inverted, so both were reprojecting from
  mirrored buffers and visibly warping during camera movement; all three backends now look alike.
- The HUD-less image is HDRP's colour at `AfterPostProcess`, taken in the frame it belongs to, instead
  of a swapchain capture that was one frame late.
- First-person hands no longer flicker: Kandra's previous skinned positions survive the extra
  camera-rendering contexts that used to overwrite them within a frame.
- The HUD stays out of frame generation with ReShade and RenoDX, by replaying RenoDX's recognised
  final-output shader on the capture, and by accepting ReShade's wrapped D3D11 device.
- FSR is given the resolution the game renders at rather than the display resolution, and near and far
  swapped for inverted depth, as AMD's own integrations do.
- FSR resets its history when the HUD-less source changes, and rebuilds its context when input flags do.
- DLSS-G is told to allocate its UI recomposition path only while a verified HUD-less image is supplied.
- A still image - a menu, a pause, a loading screen - no longer counts against the HUD-less capture.

Display settings

- Turning frame generation on moves the game's own display setting from Exclusive Fullscreen to
  Borderless Fullscreen, and stops offering Exclusive while it is on. Frame generation cannot run in
  Exclusive Fullscreen: DXGI will not give its swapchain a window that already holds the display.
  `Use borderless for frame generation` in the advanced settings turns this off.
- Low Latency can be Off while FSR frame generation runs; AMD pairs FSR with Anti-Lag 2, which NVIDIA
  hardware does not have.

Stability

- Frame generation survives a display-mode change. Unity replaces the swapchain rather than resizing
  it, and the replacement was refused because a bridge already existed, which left the game presenting
  through its own swapchain for the rest of the session.
- Fixed a crash when the game kept using a bridge it had replaced. Twenty-two swapchain methods
  forwarded to a presentation swapchain that was no longer there, three of them as tail calls through
  its vtable.
- Frame generation starts on an sRGB back buffer instead of failing to create the bridge. The view that
  failed exists only to verify the HUD-less capture, so the bridge now continues without it.
- The HUD-less capture has to earn its verification again whenever the buffers change; the previous
  result outlived the buffers it was measured on.
- A `ResizeBuffers` asking for the configuration already in effect is skipped. FoA issues seven in a
  minute of Options use, each costing a GPU drain, a buffer rebuild and a frame over 100 ms.
- Fixed the IL2CPP crash when opening Options with upscaling active.

DLSS-G correctness

- `slDLSSGGetState` is no longer given an options pointer, which it applies rather than reads.
- The capabilities query stays at startup; deferring it to the present thread broke the Video tab twice.
- The pipeline line reports DLSS-G's own presented-frame count, and prints `unreported` for FSR rather
  than a zero that read as failure.

Diagnostics

- Reflex and XeLL are measured on one comparable figure, `simToPresent`, alongside each technology's
  own. Every average divides by the frames behind it, which `renderLatency` did not.
- A refused bridge says what the game asked for - size, format, buffers, swap effect, flags and whether
  the request was windowed or exclusive fullscreen - on all three creation paths.
- The log records whether the game protects its immediate context, and says so once if the bridge ever
  uses that context from a second thread.
- FSR debug flag 32 saves three source colour, depth and motion frames with their metadata, once.
- Added a D3D11 WARP regression test for the HUD-less colour conversion shader, and portable tests for
  the clip-space orientation change.
- New optional exports `TGFG_SetHudlessCapture`, `TGFG_SetHudless` and `TGFG_SetDebugFlags`.

Packaging

- The Nexus downloads carry `vortex_override_instructions.json`, which sets the mod type Vortex calls
  "Engine Injector". Vortex was installing the BepInEx folder under its own mod directory instead of
  the game root, so nothing loaded. Manual installs and other mod managers are unaffected.

# v0.5.0 - AMD FSR and Intel XeSS frame generation, one Low Latency selector

- The plugin is now "TG Frame Generation" with the ID com.wessberg.tgframegeneration. Settings
  live in BepInEx/config/com.wessberg.tgframegeneration.cfg; the old
  tgfoa.experimental.dlss-frame-generation.cfg and BepInEx/TGFrameGeneration/VideoSettings.cfg
  are migrated on first start and renamed to *.migrated.
- FoA Mod Manager lists exactly the Video tab's six rows, with the same names, choices, labels,
  order and visibility rules. Choices follow what this GPU reports, and a change made there
  shows up in the Video tab once the Options screen is closed. The other options sit in a hidden
  [Advanced] section.
- Avalon Mod Manager's MODS tab no longer lists TGFG: TGFG adds itself to that manager's
  blacklist for the session, without saving its config. Its Apply writes back the values staged
  when the Options screen opened, which undid Video-tab changes.
- Low Latency can be Off only while frame generation is Off; FSR now runs with Reflex or Anti-Lag
  2. The row is greyed out when only one choice remains (XeSS with XeLL).
- Frame generation no longer needs DLSS upscaling. Without DLSS, a custom pass before
  post-processing hands the native side HDRP's own depth and motion vectors (TGFG_SetHdrpInputs,
  render event 3), with HDRP's TAA jitter and the motion vector scale HDRP gives DLSS. DLSS's
  inputs take precedence while it runs. FSR and XeSS frame generation therefore work on AMD and
  Intel GPUs. If the custom pass can't be installed, the rows are greyed out unless DLSS is the
  selected upscaler.
- The standard version (IL2CPP, BepInEx 6) has the same Video rows and HDRP input path as the Mono
  build, from the same sources. TGFG's subclasses of the game's Setting and DependentOption and of
  HDRP's CustomPass are registered with Il2CppInterop, which routes the base class's virtual calls
  to them. Il2CppInterop allocates those objects without running the IL2CPP base constructor, so
  TGFG runs it (Il2CppTypes.Construct).
- Packages include NVIDIA's license notices next to the Streamline runtime
  (LICENSE-Streamline.txt, LICENSE-NVIDIA-RTX-SDKs.txt, LICENSE-NVIDIA-Reflex.txt), staged from
  the SDK release by Stage-Runtime.ps1.
- Packaging writes zip entries with forward slashes (Compress-Archive in Windows PowerShell used
  backslashes) and copies each package to artifacts/Nexus as "TGFrameGeneration - mono.zip" or
  "TGFrameGeneration.zip". Build paths are mapped, so the DLLs no longer name local folders.
  A package holds the BepInEx folder and TGFG-Package-manifest.json; TGFG-PLAYER-INSTALL.txt is
  no longer included.
- Removed leftovers of the 0.3 patch workflow: APPLY.md, integrate-autoload.patch,
  source-manifest.json, the validation logs, docs/VALIDATION.md, the retired Launch-Game.ps1 and
  the 0.3 example config.
- Video tab: Frame Generation [Off | DLSS | FSR | XeSS]. A technology is listed only when its
  runtime is installed and reports support on the GPU. FSR runs through AMD's FSR API (FSR SDK
  2.3.0: FSR 4 ML frame generation on Radeon RX 9000, FSR 3.1 analytic frame generation elsewhere)
  and AMD's frame generation swapchain. XeSS runs through Intel's XeSS-FG swapchain (XeSS SDK
  3.0.2). All three use the same inputs (see the HDRP input path above).
- Frame multipliers come from the runtimes: DLSS from Streamline's numFramesToGenerateMax, XeSS
  from xefgSwapChainGetProperties (up to 4x on Intel Arc, 2x elsewhere). The FSR API has no
  multi-frame control, so FSR is 2x. Frame Multiplier offers up to the highest reported maximum,
  blocks values above the selected technology's maximum and clamps the value when switching
  technology; the native side clamps the request as well. Neither FSR nor XeSS has dynamic frame
  generation, so Dynamic stays DLSS-only. The 6x cap is gone (the ABI bound is 16).
- Switching technology recreates only the private presentation swapchain, between two frames: the
  Streamline swapchain for Off/DLSS, the SDK's own swapchain for FSR/XeSS. The game's D3D11
  buffers are unchanged. An SDK that fails is disabled for the session and presentation falls
  back to the standard swapchain.
- Low Latency [Off | NVIDIA Reflex | NVIDIA Reflex + Boost | Intel XeLL | AMD Anti-Lag 2] replaces
  the NVIDIA Reflex row and lists what the GPU supports. DLSS Frame Generation requires Reflex and
  XeSS Frame Generation runs with XeLL only (Intel's requirement), so the row forbids the other
  choices for those. XeLL on its own needs an Intel GPU; Anti-Lag 2 needs an AMD GPU. The 0.4.0
  "NVIDIA Reflex" value migrates to "Low latency"; the advanced base FPS limit feeds whichever
  technology is active.
- The bridge no longer requires DLSS-G: Streamline binds when DLSS-G or Reflex is supported (RTX
  20/30 get FSR/XeSS with Reflex), other GPUs get a plain D3D12 device, and a Streamline start
  failure no longer stops the other technologies.
- A game waiting on the swapchain's frame-latency object gets a bridge-owned semaphore that
  survives swapchain recreation; the private swapchain no longer carries that flag. HDR metadata
  is re-applied to a recreated swapchain.
- Native ABI v5 (0x00050000): the request gains backend and latencyMode (was reflexMode) and
  baseFpsLimit (was reflexBaseFpsLimit); the status reports available backends, per-backend
  maxima, latency technologies, the active output and latency technology, the GPU vendor and
  backend failures; the camera packet carries the world-to-view matrix XeSS needs.
- Packages add Native/FidelityFX (AMD-signed loader and frame generation DLLs, license notice)
  and Native/XeSS (Intel-signed libxess_fg.dll and libxell.dll, license notice).
  Stage-Runtime.ps1 takes -FidelityFXRoot and -XeSSRoot; Build-Native.ps1 takes the FSR, XeSS and
  Anti-Lag 2 SDK roots, defaulting to folders next to the script.
- Managed and native versions are 0.5.0.

# v0.4.0 - native Video settings rows, Reflex modes, startup crash fix

- Video tab: Frame Generation [Off | DLSS], Frame Generation Mode [Fixed | Dynamic] (shown only
  when the runtime reports Multi Frame Generation and Dynamic support), Frame Multiplier
  [2x .. max reported] (Fixed), Max Frame Multiplier [Up to 2x .. max] and Target Framerate
  [Auto | 60 .. display maximum] (Dynamic; the max is sent as numFramesToGenerate, which the
  Streamline 2.14.1 guide documents as ignored in Dynamic mode, so it is best-effort) and
  NVIDIA Reflex [Off | On | On + Boost] are real game Setting/PrefOption rows registered through
  SettingsMaster.AddGraphicSetting, placed after Dynamic Resolution, with the game's Apply
  Changes / Undo / Restore Defaults. Reflex Off is forbidden while Frame Generation is on.
- Video settings persist in BepInEx/TGFrameGeneration/VideoSettings.cfg, outside the plugin's
  main config, so generic settings managers no longer re-apply stale copies over them. The
  old UGUI adapter and its Mode/Fixed multiplier/target entries in the main config are gone.
- Native ABI v4 (0x00040000): TGFG_Request gains reflexMode (Off/On/On + Boost). Reflex
  follows the user setting and is forced to at least On while FG is requested.
- Managed and native versions are 0.4.0.
- DLSS 5 (RenoDX DLSS addon) compatibility: NVIDIA's sl.interposer.dll is staged and installed,
  byte-identical and still NVIDIA-signed, as `Native/Streamline/TGFG.Streamline.dll`. The addon
  looks Streamline up by module name, hooks the interposer's exports and DXGI factories, and
  releases (E_UNEXPECTED) every swapchain ReShade did not observe, including TGFG's private
  output. Deployment backs up and removes the old `Native/Streamline/sl.interposer.dll`. The
  native log reports another mod's Streamline if one is loaded later.
- ReShade compatibility: NVIDIA's Unity plugin evaluates DLSS on ReShade's proxy context, not the
  real immediate context. The bridge tags the real context with private data (which ReShade
  forwards) and matches on it, so depth/motion capture works under ReShade + RenoDX.
- Video tab: Frame Multiplier is shown only in Fixed mode; Dynamic mode shows Max Frame Multiplier
  and Target Framerate instead. The pipeline log line includes Reflex's own latency reports.
- Fixes found in the first in-game FG runs: the end-of-camera marker never executed under HDRP
  (endCameraRendering fires after the last Submit); FoA's camera rect is not bit-exact full screen;
  the R32G32_FLOAT motion copy is not NT-handle shareable on NVIDIA D3D11 (now R16G16_FLOAT).
  A "Pipeline 5s:" line in TGFG.Native.log shows where frames drop out.

- Native: removed 64 KB stack buffers (GetModuleFileNameW/GetSystemDirectoryW) from the
  GetProcAddress hook and bootstrap paths. The hook runs on every thread in the process,
  including NVIDIA driver worker threads with small stacks, and the 64 KB frame probe caused
  a STATUS_STACK_OVERFLOW at the first Streamline Present. Paths are now heap-grown.
- Build-Native.ps1: passes -DCMAKE_BUILD_TYPE=Release for single-config generators; the
  NMake build previously linked the debug CRT.

# v0.3.1 - repository-integrated Steam autoload

- Adds src/TGFG.Autoload in the existing solution and gameplay build graph.
- Same root Build.Local.props, two runtime-specific managed pairs.
- Build.ps1 -Deploy stages/installs complete plugin/preloader/native/runtime package.
- Package.ps1 produces a launcher-free player ZIP from this repo's artifacts.
- Test-Setup.ps1 checks the installed Steam-autoload layout, not developer launcher files.
- Replaces stale launcher advice and adds an autoload log tail to gameplay diagnostics.
- Retains all native v0.3.0 source, SDK build scripts and ABI unchanged.
- No Windows compilation, PowerShell execution or in-game validation claimed.

# Changelog

- NR-40 (2026-10-09): Placement choices now read Quality / Performance in Video and FoA Mod Manager.
  The tooltip explains after/before DLSS upscaling and the detail/GPU-cost tradeoff. Saved values unchanged.

## 0.3.0 — DX11 rendering / private DX12 Streamline presentation

Replaced the D3D12-source backend with a direct D3D11-to-D3D12 bridge. Added a
D3D11-facing swapchain, shared textures/fences, typed D3D11 NGX capture, protected
input conversion, HDR output conversion, expanded diagnostics and local setup
checks. Kept the Mono/IL2CPP managed targets and existing in-game menu adapter.

Changed the early launcher to require `-force-d3d11` and rendezvous after Windows
loader initialization at the restored PE entrypoint. ABI changed from v2 to v3.
Added Windows HLSL compilation tests and portable interop policy checks. No
OptiScaler/FidelityFX rendering backend is included. Windows build/game testing
has not been performed in the preparation environment.

The HUD-less capture export is implemented, but no verified FoA/RenoDX HUD-less
stage is hooked. Indexed source buffers, nonblocking presentation, full device
recovery and zero-overhead Off are not claimed.

## 0.2.0

Earlier direct Streamline prototype, requiring actual D3D12 game rendering.
Not compatible with this version's native ABI.

## 0.1.0

Earlier architecture was superseded. It is not a dependency of this package.
