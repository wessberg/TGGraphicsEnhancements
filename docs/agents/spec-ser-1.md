# spec-ser-1: the reflections' pass and Shader Execution Reordering (PT-99, PT-110)

Worker `specser1`, copy `...\scratchpad\wt\specser1`, base `base86`, brief `docs/agents/briefs/brief-spec-ser-1.md`.
2026-10-10. Nothing here ran in the game, nothing was deployed or committed, nothing was written into the main tree or
the game folder. No GPU test was run (the brief: a worker's test cannot make a new reordering pass).

## State when stopped

(kept current after every step; the newest line is the truth)

- 14:27. **HAND-OVER 1 IS READY (section 2). THE DESIGN IS WRITTEN (section 3). STOPPED HERE. BUILT, NEVER RUN: the
  game is the test, and the coordinator's.**
- The study build: `%TEMP%\tgb\specser1\TGPathTracing.Native.dll`, 32,882,176 bytes, built 14:25:54, SHA-256
  `B8846ED053BD4AFE84EC944DB6596DB5D1B3F42BFD42112DCFC0F3EBF97E88B2`; configured `-DTGPT_WITH_NRD=OFF
  -DTGPT_RAIN_WETNESS=ON -DTGPT_SPEC_STUDY=ON`. The plugin is not changed. (A DLL of 14:16, `68188F49105A...`, had a
  "keys" pass with three calls: never deploy it.)
- Checks: source checks 115 of 115 in the copy (one new: `test_path_tracing_spec_study_on_trial_default_off`); the
  tracer's C++ compiles with the switch and without it (`cl /Zs`, 0 errors, 0 warnings); a build without the switch
  gives every pass that ships the same source text as base86 (9 of 9 cases); the study's copy of the pixel differs
  from the pass that ships in its `STUDY` lines alone. Counted in the listings: "keys" ONE call (423 bytes alive by
  DXIL, the pass that ships 427), "water turns" three (the pass's 443, the turn's 701, the patch's 874).
- 14:16. The first "keys" pass had THREE calls (DXC copied the call into the paths whose hint was a different
  constant): rewritten with `select()`, compiled again, one call (section 3.4).
- 14:14. "Water turns" compiled (509 s). The design written (section 3).
- 14:10. Reading done (section 1); the study build written (section 2).
- NOT DONE, and not mine: the run (section 2.4), then the design's numbers (section 3.3).

## 1. What reading says

### 1.1 The pass as it ships

`pt_specular.hlsl` `specular_pixel`, compiled as ONE ray generation shader with everything inline (`pt_specular_ser.h`,
53.8 MB of header, 420,028 DXIL instructions, 152 ray queries).

What runs, in order, for each pixel:

1. The pixel's surface read (`load_surface`: the G-buffer, in the screen's order). Sky and forward-drawn pixels leave.
2. BEFORE the call, by kind of pixel:
   - glass seen by the camera: the whole walk through the glass (`dielectric_follow`: a loop of rays and interfaces);
   - water seen by the camera: one ray to the water's surface (`trace_water`);
   - anything else: the reflection ray (`trace_from_surface`, up to five ray queries) and its hit fetched whole
     (`fetch_hit`: positions, normals, texture coordinates, 68 numbers).
3. THE CALL, line 142: `PT_REORDER(regroup_key(shade ? seen.material : 0xFFFF, cache_feeds(p, 2u)))`, that is
   `dx::MaybeReorderThread(key & 0xFFFF, 16)`: the material's number in bits 0 to 14, "this pixel feeds the radiance
   cache" in bit 15, `0xFFFF` for nothing to shade. It stands in no loop. Every valid pixel reaches it.
4. AFTER the call: `shade_seen` (the material, the sun and the lamps with their shadow rays, the subsurface walk, the
   cache's light, the cache's feed ray for one block in four, a smooth metal's second reflection ray and its shading,
   a walk through glass the reflection met) and, for water, `shade_path` -> `water_path`: the water's whole path, a
   loop with no fixed end (`pt_water_path.hlsli`; roulette after 3 vertices). Then `specular[p]` is read and written
   and `seenGuide[p]` written, in the order the threads now run in (scattered).

COUNTED (`ser1-tools\ir_live.py` on the header):
- ONE regrouping call in the compiled pass (`dx.op.maybeReorderThread(i32 268, NOP hit object, %hint, i32 16)`), at
  instruction 27,940 of 420,028: 7 % of the pass's code lies before it in program order, 93 % after.
- Values DXIL says live across it: 284 values, 427 bytes as data (16 `float3`, 14 `uint`, 8 `float`, ...), plus 109
  arrays without lifetime marks (1,744 bytes).
- The driver's own count (the game's log, the study runs): **128 bytes a thread**. The driver keeps far less than the
  listing names (it recomputes or reloads the rest). The bounced light's pass as it shipped kept 2,560 and lost by
  it; this pass keeps 128 and gains by the call everywhere: 9.3 against 10.0 ms without a call (the tavern), 24.6
  against 31.1 (the town), 95.4 against 125.4 (the beach).

### 1.2 What `ser2`'s two changes change (`pt_ser_split.hlsli` `split_regroup_seen`, `pt_specular.hlsl` `PT_SEEN_ONCE`)

- **The 3-bit key**: the hint is "what the pixel shades next" (0 plain, 1 water, 2 glass, 3 nothing) in bits 1 and 2
  and "feeds" in bit 0, 3 bits, in place of the material's number and "feeds" in 16. Adds one read of the material
  record (`materials[seen.material].flags`) before the call. Changes nothing a thread carries (the driver: 128 bytes
  in both). What it takes away: the driver no longer sees WHICH material a pixel shades, only its class. All plain
  surfaces of a town fall into one class, so after the call a group of 32 threads shades many materials (different
  textures, different material programs, hits far apart in the scene).
- **The guide stored once**: the pass that ships writes "nothing" into `seenGuide[p]` BEFORE the call (in the screen's
  order: one coalesced write) and the real word AFTER it; the trial writes once, after the call (scattered), also for
  the pixels that end with nothing. Carries nothing more (the guide's descriptor index is the frame's constant). Moves
  one 8-byte store of the "nothing" pixels from the coalesced side of the call to the scattered side. A guess: a few
  tenths of a millisecond at most.
- So by reading, the 9 ms of the town are the key's: the material's number is what the regrouping pays with, and a
  3-bit class gives the driver a reason to move threads without a reason to put like with like. The run tells
  (steps 5 and 6 of the recipe below).

### 1.3 What the documents say about the hint's bits

Microsoft, HLSL proposal 0027 (`<scratch>\compat1-refs\hlsl\0027-ser.md`), "MaybeReorderThread with coherence hint":
- "More significant bits of the hint value are more important than less significant bits."
- "The resolution of the hint is implementation-specific. If an implementation cannot resolve all values of
  CoherenceHint, it is free to ignore an arbitrary number of least significant bits."
- The bit count: "the lowest value required to represent all possible values of CoherenceHint (at the given
  MaybeReorderThread call site)"; the same value from all threads at a call site.
- With a hit object: the hit group counts before the hint, and "the number of coherence hint bits that the
  implementation actually honors can be smaller in this overload".
- In DXIL there is ONE operation (268), always with a hit object: the hint-only call is compiled into a NOP hit object
  and that operation (counted in our listings).

NVIDIA, the SER whitepaper (`<scratch>\compat1-refs\ser\ser-whitepaper.txt`), "Using coherence hint bits judiciously" and
the API reference:
- The hint-only form: "how many of the least significant bits of CoherenceHint should be considered during
  reordering (max: 16)"; "Where possible, reordering will also attempt to retain locality in the thread's launch
  indices".
- With a hit object: "The maximum number of coherence hint bits in this variant of NvReorderThread is 8"; the order of
  priority: 1. the hit object's shader, 2. the hint, "with the most significant hint bit having highest priority", 3.
  "spatial information stored in the HitObject" (where the hit lies).
- "using more coherence hint bits also reduces the resolution at which the hit object can be taken into account":
  no more bits than needed.
- NOT DOCUMENTED: which limit NVIDIA's driver applies to a DXR 1.2 call with a NOP hit object (our call): the generic
  16 or the hit object's 8. If 8, and it keeps the HIGHEST 8 of our 16, then today's key reaches the driver as "feeds"
  plus bits 8 to 14 of the material's number (nearly always 0): a sort by "feeds" alone. Step 7 of the recipe (the
  material's number moved into the high bits) answers it.

### 1.4 The water's path (`compat-1.md` S5) and `TGPT_REGROUP_WATER`

- At the beach the pass is 93 ms of a 145 to 169 ms frame; the measuring run of 10-09 (`measure-run.md`) put nearly
  all of it in the water's path of the pixels the camera sees as water.
- The path regroups once, at the pass's call, before the loop. Inside the loop each turn is one of four very
  different pieces of code: a scatter point in the body (`water_sun`: the sun through the waves, with a patch of waves
  of its own when the point is outside the kept corners, a loop of up to 40 steps), the surface (the sun's glint with a
  shadow ray, a facet drawn, crossing or mirroring), something solid (the bed's material, the sun and lamps with shadow
  rays, the caustics' landed light), or the sky. A group of 32 threads runs every kind any of its threads takes, and
  waits for its longest path.
- `perf2`'s `TGPT_REGROUP_WATER` (never run in the game): a call at every turn after the first, right after the turn's
  ray, keyed 0 scatter / 0x2000 surface / 0x4000 + the material's 13 low bits solid / 0x6000 sky, 16 bits; and one before
  a point makes its own patch (key 0x8000). The turn's free flight is drawn before the ray (the same two random numbers
  in the same order: the ray draws none), so the picture is the same.
- What a thread carries there, by reading: the path's own state (17 numbers) and the turn's FETCHED hit (68 numbers),
  and everything `shade_path`, `shade_seen` and `specular_pixel` still need after the loop (the glass branch's weights,
  the reflection's weight, origin and direction, the surface's position, ...), because the compiler cannot know a
  thread in the water's path is a water pixel. The listing and the driver's line of the study's pass count it
  (section 2.6, filled when the compile has ended).

## 2. Hand-over 1: the study build

### 2.1 What it is

One compile switch, CMake's `TGPT_SPEC_STUDY`, OFF by default. OFF: no pass is compiled with `PT_SPEC_STUDY`, the
tracer holds no line of it, every pass gets the same source text as base86's (checked: `specser1-tools\same-source.ps1`,
9 cases, among them the reflections' and the bounced light's reordering passes, the plain passes, `pt_prepare`,
`pt_walk`). ON: two more ray generation passes of the reflections, beside the two that ship (compiled as always):

| `Speed trial` | Pass | What it is |
|---|---|---|
| 0 | as it ships | the pass that ships |
| 8 | the yardstick | the plain compute passes (made in this build without an environment variable) |
| 16 + 256 x mode (+ 4096) | "keys" (`pt_specular_ser_k.h`) | `spec_study_pixel`: the pass as it ships, its one call given a hint and a bit count by the mode; 4096: the guide stored once |
| 32 (+ 2, + 4, + 256 x mode) | "water turns" (`pt_specular_ser_w.h`) | the pass as it ships, with `TGPT_REGROUP_WATER`'s calls in the water's path (2: every turn; 4: before a patch); the turn's hint by the mode |

The modes of "keys" (bits 8 to 11): 0 today's key (the control: the same numbers, given as values the compiler cannot
fold), 1 `ser2`'s 3-bit key, 2 today's key with the material's number in the HIGH bits (its 15 bits reversed, "feeds"
still on top), 3 today's key in 8 bits ("feeds" in bit 7, the material's 7 low bits), 4 in 4 bits ("feeds" in bit 3, its
3 low bits), 5 no hint (0 bits), 6 the material's number first (reversed) and "feeds" last, 16 bits.
The modes of "water turns": 0 `perf2`'s 16-bit key, 1 the turn's kind alone (3 bits), 2 no hint.
Every variant is the same computation: the same rays, the same random numbers in the same order, the same shading.

### 2.2 Files (all in the copy)

- `native/pathtracing/shaders/pt_spec_study.hlsli` (new): the modes, `spec_study_regroup` (the one call),
  `spec_study_pixel` (a copy of `specular_pixel` as it ships, its lines for a build without `PT_SEEN_ONCE` and
  `PT_SPEED_MEASURE`; the lines that differ are marked `STUDY`). Refuses `PT_SPEED_MEASURE`, `PT_SER_STUDY`,
  `PT_SER_SPLIT`.
- `pt_specular.hlsl`: 8 lines at its end: the include and the entry, under `PT_SPEC_STUDY` (nothing of `rule2`'s lines).
- `pt_water_path.hlsli`: inside `TGPT_REGROUP_WATER`'s own block, `water_regroup` by the mode under
  `PT_SPEC_STUDY == 2`.
- `pt_shaders.hpp.in`: two headers under `#ifdef TGPT_SPEC_STUDY`.
- `CMakeLists.txt`: the option and its two `dxc` commands (after NRD's, before `add_custom_target`), the tracer's
  definition (after `TGPT_REGROUP_WATER`'s). Refuses `TGPT_SPEED_MEASURE`, `TGPT_REGROUP_WATER`, `TGPT_SER_STUDY`,
  `TGPT_SER_SPLIT` beside it.
- `native/pathtracing/src/tracer.cpp`, 8 blocks under `#ifdef TGPT_SPEC_STUDY` and two lines of the speed trial's log
  chain: the state; `Passes`; `release_all`; `make_spec_study` (the two passes made after the others, ONE AT A TIME,
  each in its own try, each logging the driver's stack; the plain passes made if missing) and `spec_study_text` (the
  log's words); its call after `run(0)` in `build_passes`; `adopt_passes` (a shader table of its own, 2 records, so
  that `kSerRecords` is not touched); `dispatch_pass` (its own dispatch for record 1, after the plain passes' line);
  the pick from the Speed trial (bits 4 to 6). The log chain: `#if defined(TGPT_SPEC_STUDY)` in front of
  `#ifdef TGPT_SPEED_MEASURE` (one line changed into two), and an empty `#elif defined(TGPT_SPEC_STUDY)` before
  `#elif !defined(TGPT_SPEED_MEASURE)`.
- `tests/ingame/scenarios.json`: recipes `spec-study` and `spec-study-water` (after `regroup`).
- `tests/source_checks.py`: the new check `test_path_tracing_spec_study_on_trial_default_off` (at the end of the class);
  two counts of CMake lines moved (`${PT_SPEED_DEFINES}` 7 to 9, RTXDI's include path 9 to 11).

FOR THE MERGE after `rule2`: every block above sits between lines `rule2` should not touch (chosen so). The two count
lines in `source_checks.py` and the CMake count are the ones `rule2` changes too: there add 2 to whatever `rule2` left.
The log chain's `#if defined(TGPT_SPEC_STUDY)` stands in front of the `TGPT_SPEED_MEASURE` branch, which `rule2` keeps.

### 2.3 Build and deploy

- The DLL: `%TEMP%\tgb\specser1\TGPathTracing.Native.dll` (SHA-256 `B8846ED053BD...`, the top of this file), configured
  `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON -DTGPT_SPEC_STUDY=ON` (no NRD in it: the run uses no denoiser; no Mega
  Geometry either, as `ser2`'s build: that trial is off by default). The plugin is not changed. The passes that ship are
  main's own compiled headers (copied from `build\native\generated\pathtracing`, the same source text: 2.1).
- Or merge the copy and configure the main build with `-DTGPT_SPEC_STUDY=ON` (two more headers compile, 8 to 9 minutes
  of a core each).
- THE FIRST START makes two large ray generation passes the driver has not seen, AFTER the others and ONE AT A TIME
  (each 4 to 9 minutes); look at free memory first (PT-96). The tracer's log then has three lines `Reflections study:
  ...: the driver's stack for its ray generation shader X bytes` (the pass as it ships, 16 keys, 32 water turns).

### 2.4 The run (no denoiser, 16 frames, 25 s a step, "as it ships" first and last)

`.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room -Recipe spec-study`, then `metal`, then `water-day` (12 traced
steps, about 8 minutes a scene); at the beach also `-Recipe spec-study-water` (6 traced steps, about 4 minutes).

`spec-study`:

| Step | `Speed trial` | What it is | The question |
|---|---|---|---|
| 2-as-it-ships | 0 | the pass that ships | |
| 3-yardstick-plain-passes | 8 | the plain compute passes | the yardstick |
| 4-keys-todays-key | 16 | "keys", mode 0: today's key | the control: does the study's call cost anything by itself? |
| 5-keys-3-bit-key-alone | 272 | mode 1: `ser2`'s 3-bit key, the guide as it ships | `ser2`'s first change alone (b) |
| 6-keys-todays-key-guide-once | 4112 | mode 0, the guide stored once | `ser2`'s second change alone (a) |
| 7-keys-material-in-high-bits | 528 | mode 2 | does the driver drop our low bits? (c) |
| 8-keys-8-bits | 784 | mode 3 | fewer bits (d) |
| 9-keys-4-bits | 1040 | mode 4 | fewer bits (d) |
| 10-keys-no-hint | 1296 | mode 5 | no hint (d) |
| 11-keys-material-first-feed-last | 1552 | mode 6 | does "feeds" on top matter? |
| 12-water-regrouped-every-turn | 34 | "water turns", every turn, `perf2`'s key | (e) |
| 13-as-it-ships-again | 0 | the run's drift | |

`spec-study-water` (the beach): 0; 32 (the water's calls compiled in, none taken: what the code alone costs); 34
(every turn, 16-bit key); 290 (every turn, the turn's kind in 3 bits); 546 (every turn, no hint); 0.

(Not in a recipe, for a second look if step 5 does not explain `ser2`'s numbers: 4368, `ser2`'s two changes together.)

### 2.5 What to read

1. AT THE START, in the tracer's log: `Reflections study: the reflections' pass as it ships: the driver's stack ... X
   bytes` (expected 128), then one line for 16 and one for 32 (`made in N s`). THE NUMBER FOR (e): the stack of 32
   against 128. `... could not be made`: that number runs the pass that ships (the step is void).
2. When the number changes: `Speed on trial (N): the reflections study: keys, mode M (...)` or `water turns, ...`.
3. In each step, the tracer's 10 s lines: "reflections" (`measure_read.py`). The bounced light's pass is the same in
   every step: its numbers are the run's drift.
4. The pictures: every traced step against step 2 (`same_picture.py`): the same.

### 2.6 What each step should read, written BEFORE the run (the reflections' pass, ms: tavern / town / beach)

Known: as it ships 9.3 to 9.6 / 24.0 to 24.7 / 92.9 to 95.4; no call at all 10.0 / 31.1 / 125.4; plain 12.6 / 27.7 to
28.4 / 194; `ser2`'s two changes 10.3 to 10.4 / 32.8 to 33.8 / 99.6 to 101.1. The beach's noise between steps of an
unchanged pass: 3 to 5 ms (the study's run: 95.0 to 100.8).

| Step | My guess | If it reads otherwise |
|---|---|---|
| 4 keys, today's key | = as it ships (within 0.3 / 0.7 / 3) | the run-time bit count costs: read 5 to 11 against 4, not 2 |
| 5 3-bit key alone | 10.3 / 33 / 100: ALL of `ser2`'s loss | if = 4: the guide (6) or the two together are the cost |
| 6 guide once | = 4 | the scattered stores of "nothing" cost: a store before a call is not free to move |
| 7 high bits | = 4 (the driver reads our low bits: the bounced light's study, where more bits cost more) | faster than 4 in the town: the driver keeps only the high bits, and today's key has been sorting by "feeds" alone |
| 8 8 bits | town +1 to +3 over 4; the tavern and the beach within noise | = 4: 128 material groups are enough |
| 9 4 bits | town +4 to +8 over 4, towards 5 | |
| 10 no hint | between 5 and "no call": 9.8 to 10.3 / 28 to 33 / 95 to 125 | near 4 at the beach: the hint is not what the water gains by, the regrouping itself is (threads that left at the top no longer hold groups half empty) |
| 11 material first | = 4 in the town; the tavern up to +1.5 (rule-1: "feeds" on top took the tavern from 11.4 to 9.9) | |
| 12 water every turn | tavern and town = 4 (no water: the calls are never reached); the beach 80 to 100: the call is in a loop and carries what the whole pass still needs after it (listing: 701 bytes at the turn's call, 874 at the patch's, 443 at the pass's own) | much faster at the beach: the turn's divergence was the cost and the stack is small; then the water pass of section 3 is the next build, sooner |
| 13 as it ships again | = 2 | |
| water 32 (calls compiled, none taken) | = 2 within noise | slower: the code in the pass costs (AGENTS.md "Code that is switched off still costs") |
| water 290 / 546 | within 3 of 34 | faster than 34: the hint costs, as in the bounced light's pass |

COUNTED in the compiled listings (`ser1-tools\ir_live.py`, `ir_stats.py`; values defined before a call and used after
it: an upper bound of what DXIL says must survive, NOT what the driver keeps):

| Pass | instructions | ray queries | calls | bytes alive across each call |
|---|---|---|---|---|
| as it ships (`pt_specular_ser.h`, main's) | 420,028 | 152 | 1 | 427 |
| 32 water turns (`pt_specular_ser_w.h`) | 420,239 | 152 | 3: the pass's, the turn's, the patch's | 443 / 701 / 874 |
| 16 keys (`pt_specular_ser_k.h`, 14:25, with `select()`) | 420,091 | 152 | **1** | 423 |
| (16 keys as first written with `if`s, 14:15, replaced) | 420,118 | 152 | 3 | 431 / 187 / 431 |

## 3. The design (by reading now; finished when the run is in)

Target (the brief): under today's 9.3 / 24.0 / 93 ms, the beach by tens of ms. The model is what the bounced light's
pass taught today (`ser-2.md`, run `20261010-130621`): ONE call, outside every loop, nothing of the pass's own carried
across it, the screen-order work out of the sorted part: 42 to 9.5 ms in the tavern, 62 to 29 at the beach.

### 3.1 The water's path: a pass of its own, regrouped at every turn

What is wrong with it today, by reading: the water's whole path (a loop with no fixed end, four very different kinds
of turn) runs AFTER the reflections pass's one call, inside a shader that also holds every other kind of pixel. A
group of 32 threads runs every kind of turn any of its threads takes and waits for its longest path. A call inside
that loop (the study's step 12) must also keep everything the big pass still needs after the loop, for every thread
(the listing: 701 bytes at the turn's call against 443 at the pass's own), because the compiler cannot know that a
thread in the loop is a water pixel. That is the bounced light's old fault (a call inside a loop, a big state) in
another place.

The pass, built for reordering (ray generation, Shader Model 6.9, as the two that ship):

1. **Its threads**: the pixels whose camera ray meets water (`s.water || s.underwater`, not glass). First build: the
   whole screen, the other pixels leave at the top (a thread that never reaches a call does not take part in it). Later,
   if the launch costs: a list of the water pixels written by `pt_prepare`, dispatched indirectly.
2. **Before the loop, in the screen's order**: the surface read, the camera ray to the water's surface
   (`trace_water`), the medium. No call here.
3. **What a thread carries**: the path's own state and nothing else: origin, direction, weight, light (12 numbers), the
   medium, the counts and flags (`delta`, `primary`, `asked`, `crossed`, `glinted`, `lampsAsked`, crossings, vertices:
   two words), the guide's distance, the random state, the pixel: about 19 words, 76 bytes. (The study's step 12 carries
   that and the whole reflections pass.) The statics the path sets and reads (`g_waterMediumKnown`, `g_waterMedium`, the
   ray cone) are set again after each call from the state, by hand: a static set before a call and read after it is
   carried silently (`ser-2.md` section 9).
4. **The turn**: the free flight drawn before the ray (as `perf2`'s trial does: the same numbers in the same order),
   the ray traced with a ray query as today (every rule of the traversal stays: the alpha test, the water's mask), the
   hit NOT fetched: a hit object from the query (`HitObject::FromRayQuery`), and the call at once:
   `dx::MaybeReorderThread(hit, key, bits)`; the hit fetched AFTER it (`fetch_hit` from the hit object's instance,
   primitive, barycentrics, distance and transform), among threads of one kind whose hits lie together.
5. **The key** (the highest bits first; a hit object's hint holds 8 at most, NVIDIA): what the turn does, 2 bits (a
   scatter point in the body: `water_sun`, the heaviest; the surface: the glint and a facet; something solid: the bed's
   material and lights; the sky: the shortest), then 1 bit "the path likely ends here" (an estimate is allowed: the
   roulette's survival chance from the weight, known before the turn; NVIDIA: "an approximate coherence hint is often
   better than none"), then the solid's material program in the low bits. "Surface or solid" needs the hit's instance
   flag before the fetch: one read of the instance record. The driver then orders, within a key, by where the hits lie.
   How many bits pay, and whether a hint pays at all, the study's `spec-study-water` steps say first (the bounced
   light's study: every hint cost there while the state was large).
6. **The patch**: a scatter point outside the kept corners makes its own patch of waves (a loop of up to 40 steps):
   a second call before it (`perf2`'s bit 4), keyed "makes a patch", carrying the same 76 bytes.
7. **Its output**: `specular[p]` and `seenGuide[p]` as the reflections pass writes them for water pixels today (the
   whole signal, `SEEN_WATER`, `g_waterGuide`). The reflections pass then lets water pixels leave at its top, before
   its own call.
8. **Not in the first build**: a reflection ray of another surface that meets water, and the bounced light's rays that
   meet water (`shade_path`'s water part in `pt_gi`, `pt_walk`): they keep today's inline path. Second step, if the
   first pays: they hand the water pass a ray (origin, direction, weight, pixel) through a queue, and the inline path
   leaves those passes (less code in each: `compat-1.md` S6).

What it should give: the beach's reflections stage is today about 93 ms, the water the camera sees nearly all of it.
A GUESS until step 12 of the study is in: the turn's divergence is the larger part of the water's time (perf-1: "a
third or more if the waiting is the cost"), so 93 to 55 to 65 ms. Step 12 bounds it from below: whatever the
per-turn call gains there with 700 bytes carried, the pass of its own gains more with 76.

The first trial (the game first, `AGENTS.md`): the pass with the call at every turn, NO hit object and NO hint (the
smallest change that shows the direction), behind a compile switch, one scene (`water-day`), one recipe of three steps
(as it ships / the water pass / as it ships). Then the hit object and the key, one step each.

### 3.2 The reflections' pass for everything that is not water

- **Its call stays where it is** (no loop, 128 bytes, it pays in every scene).
- **Its key**: by the run. By reading: the material's number is what the call pays with (a class of 3 bits throws it
  away). If step 7 is faster than step 4, the key moves its varying bits to the top (mode 2) at once; if 8 is as fast
  as 4, 8 bits (room for a hit object, below).
- **A hit object for its ray** (second trial): `trace_from_surface`'s last query as a hit object, the call by it and an
  8-bit hint ("feeds" on top, then the kind: plain / smooth metal that traces again / glass, then the material's low
  bits), the hit fetched after the call. What it buys: the driver orders by where the hits lie within a key (the
  textures, the shadow rays' way through the scene), and the fetched hit (68 numbers) no longer crosses the call. The
  bounced light's study found a hit object worth nothing while 2,560 bytes were carried; this pass carries 128.
  `trace_from_surface` asks up to five queries (what the wind moves, a near back face, the hero's body): the hit object
  is made from the query whose hit is kept.
- **The smooth metal's second ray** (`shade_seen`: a reflection in a reflection, the town's metal): after its ray, a
  second call keyed by that hit's material, carrying the first vertex's light (6 numbers). A third trial, the town only.
- **After the call** the pass reads `specular[p]` and writes it and `seenGuide[p]` in the order the threads run in
  (scattered). Small; the guide's "nothing" stays before the call if step 6 says it costs.

### 3.3 What the run must say before any of this is built

The steps of section 2.6, each against step 4 (the control). The design above is finished with their numbers: which
key, whether the low bits reach the driver, and what the per-turn call is worth with 700 bytes carried.

### 3.4 A rule for every reordering pass, learned twice today

DXC copies a regrouping call into each path whose hint or bit count is a different CONSTANT, and every copy is a reorder
point of its own: the bounced light's one hint-less line was three calls (`ser-2.md` section 14, the game: about half
of the gain lost), and this study's first "keys" pass, written with `if`s, was three calls too (14:16, counted). The
pass that ships has one because its hint is one value on every path. So: the hint and the bit count are computed
with `select()` (HLSL 2021's `?:` on a scalar is a branch) and the call stands once, after them; and every reordering
pass's compiled listing is counted before it goes into the game (`grep -c "dx.op.maybeReorderThread(i32 268"` on
its header must say what the source says). A source check cannot see this; the build could (a CMake step that counts).

## 4. Workarounds and loose ends met

- `spec_study_pixel` is a second copy of `specular_pixel` while the study stands (checked equal but for the `STUDY`
  lines: `specser1-tools\copy_diff.py`). It goes with the study.
- The "water turns" pass reuses `TGPT_REGROUP_WATER`'s code as it is (its free flight drawn before the ray); only the
  hint's mode is mine.
- The yardstick's plain passes are made by `make_spec_study` when missing, not by the `#if` line that `rule2` edits.
- Nothing of the tracer's workarounds was removed: this is a study.

## 5. Tools (`scratchpad\specser1-tools`, not in the copy)

`build.ps1` (stages configure / shaders / syntax / dll, as `ser2`'s, for this copy; pass ONE stage a call: `-Stage
shaders,dll` through `-File` is one string and runs nothing), `same-source.ps1` (the passes that ship, base86 against
the copy, preprocessed), `copy_diff.py` (the study's copy of the pixel against the pass that ships). The listing counts:
`scratchpad\ser1-tools\ir_live.py`, `ir_stats.py`.
