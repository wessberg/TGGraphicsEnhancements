# ser2: the bounced light's pass rebuilt for Shader Execution Reordering, steps 1 and 2 (PT-99, PT-110)

> Answered on 2026-10-10 (run 20261010-130621; docs/agents/rule-2.md): 'ahead' (48) is the rule in every build, the
> compute passes too; the trial's build option, pt_ser_split.hlsli, 16, 32, 64, the reflections' 3-bit key and the
> recipe ser-split are gone. Names below that no longer exist: gi_pixel_split is gi_pixel, split_trace is
> query_from_surface (pt_gi.hlsl), split_store_own and split_load_own are own_sample_store and own_sample_load
> (pt_gi_own.hlsli).

Worker `ser2`, copy `...\scratchpad\wt\ser2`, base `base84`, brief `docs/agents/briefs/brief-ser-2.md`. 2026-10-10.
Nothing here ran in the game, nothing was deployed or committed, nothing was written into the main tree or the game
folder. No GPU test ran with a new reordering pass (the brief: it cannot be made under the test's ceiling).

## State when stopped

(kept current after every step; the newest line is the truth)

- 12:52. **THE HAND-OVER IS READY (section 7). STOPPED HERE. BUILT, NEVER RUN with a reordering pass: the game is
  the test.**
- The trial build: `%TEMP%\tgb\ser2\TGPathTracing.Native.dll`, 43,532,800 bytes, built 12:47, SHA-256
  `CF2FA0938CB9517812A155521C0EDB36739C2C1C7BE226FC85AF32C3107B754D`; configured `-DTGPT_WITH_NRD=OFF
  -DTGPT_RAIN_WETNESS=ON -DTGPT_SER_SPLIT=ON` (no NRD in it: the run uses no denoiser). 0 warnings. Its pass that
  ships (`pt_gi_ser.h`) is BYTE FOR BYTE the main tree's hint-less pass of 12:19; its reflections' pass the main
  tree's own bytes. The DLL of 12:01 (`FAC25E07B43B...`) is an older set: never deploy it.
- One switch, `TGPT_SER_SPLIT`, OFF by default. One line outside it, as the coordinator asked at 12:45: every build
  now logs the driver's stack of the two passes that ship, once a start (section 14).
- Checks: source checks 113 of 113 in the copy (the new check `test_path_tracing_ser_split_on_trial_default_off`, the
  main tree's check of the hint-less line carried over word for word); the plugin builds (0 warnings, 0 errors; not
  changed); the passes that ship get the same source text as the MAIN tree's (6 of 6 cases); the C++ builds clean.
- THE OFFLINE CHECK OF STEP 1 PASSED (section 11): the split pass and the reuse pass give the same picture as the
  pass of every day, four views, and the tracer's log shows they ran.
- Counted in the compiled listings (section 13): the pass that ships, 3 regrouping calls (266 / 547 / 535 bytes of
  values alive across them); "top" ONE call, 24 bytes; "ahead" and "gain" ONE call, 146 and 145 bytes.
- The coordinator's question of 12:45 (why the shipped hint-less call gives half of the study's gain): answered in
  section 14 from the two listings side by side: the shipped pass has THREE reorder points, the study's ONE.
- NOT DONE, and not mine: the run (section 7). What becomes the rule and what goes: section 8.
## 1. What is built, in plain words

One compile switch, CMake's `TGPT_SER_SPLIT`, OFF by default. OFF: no pass is compiled with `PT_SER_SPLIT`, the tracer
holds no line of it: the build of every day. ON: beside the two reordering passes that ship (compiled as always) the
build has the passes below, and each runs only in a frame whose hidden `Speed trial` asks for it. Nothing switches
reordering off: every arrangement regroups. The plain compute passes are the yardstick (`Speed trial` 8), made in
this build with no environment variable.

| `Speed trial` | Name | The bounced light's pass | The reflections' pass |
|---|---|---|---|
| 0 | as it ships | today's: one hint-less call inside the loop over the pixel's rays, then the reuse across frames | today's |
| 8 | yardstick | today's, as a plain compute pass | today's, plain |
| 16 | split | STEP 1: the pass ends after the shading and the store of the frame's own sample; the reuse across frames is a plain compute pass that runs next (`pt_gi_reuse`). The call is today's (hint-less, inside the rays' loop). | today's |
| 32 | top | STEP 1 + the call at the TOP of the pass, once, outside the rays' loop, no hint: nothing of ours is carried across it (the pixel and its surface are read again after it) | 3-bit key, the guide stored once |
| 48 | ahead | STEP 1 + the call after the pixel's first ray, before its hit is fetched, once, outside the loop, no hint: the ray and its hit are carried (146 bytes by the listing) | as 32 |
| 64 | gain | as 48, and only threads whose ray met water or glass reach the call; the others stay where they are | as 32 |
| 24 | split, plain | the split pass as a plain compute pass, then the reuse pass: the split's own yardstick | today's, plain |

## 2. Files (all in the copy)

- `native/pathtracing/shaders/pt_ser_split.hlsli` (new): `split_store_own` and `split_load_own` (the frame's own
  sample on its way to the reuse pass), `split_trace` (a copy of `trace_from_surface` up to the fetch: keep side by
  side), `gi_pixel_split` (a copy of `gi_pixel` arranged for the call outside the loop: what differs is marked
  `DIFFERS`), `split_regroup_seen` (the reflections' 3-bit call).
- `native/pathtracing/shaders/pt_gi_reuse.hlsl` (new): the reuse across frames as a compute pass.
- `pt_gi.hlsl`: the main tree's hint-less line (carried over); the include; under `#ifdef PT_SER_SPLIT` the pass ends
  after the store; the entry picks `gi_pixel_split` for 2 to 4. `pt_specular.hlsl`: under `PT_SEEN_ONCE` (defined with
  `PT_SER_SPLIT >= 2` in the ray generation pass alone) the guide's one store and the 3-bit call.
  `pt_shaders.hpp.in`: nine headers under `#ifdef TGPT_SER_SPLIT`. `pt_common.hlsli`: unchanged.
- `CMakeLists.txt`: the option, three `dxc` commands (five ray generation passes, the split pass and the reuse pass
  in both shader models), the tracer's definition. Refuses `TGPT_SPEED_MEASURE`, `TGPT_REGROUP_WATER`,
  `TGPT_LAMP_AROUND` and `TGPT_SER_STUDY` beside it.
- `native/pathtracing/src/tracer.cpp`: one log line in every build (the driver's stack of the two passes that ship,
  section 14); the rest under `TGPT_SER_SPLIT`: the passes made (the five ray generation ones after
  the others, two at a time, each in its own try, each logging the driver's stack), the shader table's 7 records,
  `dispatch_pass` picks, the reuse pass dispatched after a split pass inside the bounced light's own stage, the log.
- `tests/ingame/scenarios.json`: recipe `ser-split`. `tests/source_checks.py`: the new check; the main tree's check of
  the hint-less line; two counts of CMake lines moved (`${PT_SPEED_DEFINES}` 4 to 7, RTXDI's include path 6 to 9).

## 3. What the study runs said, and how the build followed them

The study (`docs/agents/ser-study-run.md`, main tree), the bounced light's pass, ms (tavern / town / beach): with the
material's 16-bit hint 50.9 / 57.4 / 55.0; with NO hint 29.5 / 35.1 / 32.2 (the rule in the main tree since 12:15);
with no call at all 11.4 / 27.9 / 51.7; plain compute 14.2 / 27.8 / 63.6. The driver keeps **2,560 bytes a thread**
across the bounced light's call in EVERY variant, also those that carry 205 bytes by the listing; the reflections'
pass 128, and its regrouping pays in all three scenes. A hit object, a key by what comes next and a second
regrouping bought nothing anywhere while 2,560 bytes are carried.

So the call stays (at the beach it is worth 20 ms), and the work is the stack. What I read, and counted:
1. The listing's count of live values is not what the driver keeps: the reflections' pass has 427 bytes of live
   values and 109 arrays without lifetime marks (1,744 bytes) at its call, and the driver keeps 128.
2. What the two passes differ in: the reflections' call stands outside any loop; the bounced light's stands INSIDE
   the loop over the pixel's rays, in the pass that ships and in every study variant. A GUESS that fits: with the
   call inside a loop, what the loop's body uses is alive around the loop's back edge, so the driver keeps it.
3. So step 2 is built as three places for ONE hint-less call OUTSIDE that loop: "top" (32: nothing of ours carried),
   "ahead" (48: the first ray and its hit carried) and "gain" (64: as 48, only where the ray met water or glass;
   Microsoft's specification, "Divergence behavior": "Threads that do not arrive at a reorder point are guaranteed to
   not migrate, even if neighboring threads in the same wave do migrate"). No hit object, no hint: the coordinator's
   word, and the study's numbers.
4. The driver's stack line for "top" is the test of the guess: it carries nothing of ours. If it still reads near
   2,560, the driver keeps something that is not our values (the unmarked arrays, ray query state, the pass's size),
   and that is the next job, not the place of the call.
5. Before the message (11:46 to 12:15) step 2 was built with a hit object and a 3-bit hint ahead of the loop; that
   set was compiled (its DLL `FAC25E07B43B...`) and replaced. Its listing count stays in section 5: one call, 146 bytes.

## 4. Step 1 in detail: the same numbers

- `pt_gi` (compiled with `PT_SER_SPLIT`) ends after the shading: it stores the rays' mean and the frame's own sample.
- `pt_gi_reuse` loads that sample, runs RTXDI's reuse across frames with the same random stream (the pixel, the
  frame, 5) and the same parameters, drops a reservoir that is not finite (the same test), and stores the result.
- The sample travels WHOLE, in the reservoir array that is there (no new memory): a stored reservoir holds its
  position and weight whole and its light packed; a frame's own sample counts 1 and has age 0, so the record's
  count word, its packed-light word and its spare word say nothing yet, and the sample's light goes into those
  three words as it is (`split_store_own`, `split_load_own`). The reuse pass always stores a record of RTXDI's own
  form in its place before any other pass reads the array (a split pass is never dispatched without it:
  `serSplitRuns` ties the two). So RTXDI resamples the same numbers as in the pass of every day. The frame's own
  sample's normal goes through RTXDI's packing once more (the reuse does not read it, it only stores it again).
- The reuse pass sets the ray cone that `pt_gi`'s rays leave behind, for the ray that checks a sample is seen (it
  runs only with the hidden GI visibility option).
- Its time is counted inside the bounced light's own stage, so "bounced light" in the tracer's lines is the sorted
  pass plus the reuse pass: comparable with today's and the yardstick's.
- Out of the big pass with it: RTXDI's ray-traced bias correction, compiled in and never run by default
  (`compat-1.md` R2): the split pass has 113 ray queries where today's has 116.

## 5. Step 2 in detail (`gi_pixel_split`, `Speed trial` 32, 48, 64)

The order of the pass:
1. Pixels with nothing to gather leave (they store the empty reservoir, as today). In "top" the call comes here;
   after it the pixel's index is asked again and its surface read again (4 texture reads; the same numbers).
2. The pixel's first ray is drawn (the same random numbers in the order the loop's first turn draws them in
   `gi_pixel`) and traced by `split_trace`: `trace_from_surface` up to where it would fetch the hit. In "ahead" and
   "gain" the call comes here (in "gain" only where the ray met water or glass), and the surface is read again.
3. The first ray's hit is fetched; the sums start at 0; the loop over the rays shades the first ray and draws,
   traces and fetches any later one (Rays Per Pixel above 1) as `gi_pixel` does. No call inside the loop.
   `shade_path` is in the pass once.
4. The rays' mean and the frame's own sample are stored (step 1); the pass ends.

COUNTED in the compiled listings (`ser1-tools\ir_stats.py`, `ir_live.py`: values defined before a regrouping call
and used after it; an upper bound of what DXIL says must survive, NOT what the driver keeps):

| Pass | instructions | ray queries | calls | bytes of values alive across a call | unmarked arrays |
|---|---|---|---|---|---|
| bounced light, 16-bit key (`pt_gi_ser.h` until 12:15; bytes from `ser-1.md`) | 307,593 | 116 | **3** | 266 / 542 / 547 | 106 (1,696 bytes) |
| 16 split (built 11:51, before the hint-less line) | 304,324 | 113 | 3 | 217 / 493 / 498 | 106 (1,696) |
| "ahead" with a hit object and 3 bits (built 11:59, replaced) | 306,914 | 114 | **1** | **146** | 109 (1,744) |
| reflections, as it ships (`pt_specular_ser.h`) | 420,169 | 152 | 1 (16 bits) | 427 | 109 (1,744) |
| reflections, 32 to 64 (`pt_specular_ser_p2.h`, 11:59) | 420,178 | 152 | 1 (3 bits) | 428 | 109 (1,744) |

The new set's counts: section 13 (when its compile has ended).

## 6. The reflections' pass, and what was left

Built (it runs with 32 to 64): its key in 3 bits (what the pixel shades after the call: plain, water, glass,
nothing; and whether it feeds the cache) in place of 16 bits of a material's number; its guide stored once, where
the pixel ends (`compat-1.md` S4: the pass that ships stores "nothing" before its regrouping and the real word
after it). The study says this pass is fine as it ships (128 bytes kept; its regrouping pays everywhere), so this is
the open question "fewer hint bits for the reflections?" answered on the side: read "reflections" in steps 32 to 64
against step 0. Worse: today's key stays, and only the guide's one store is worth keeping.

Left, and why:
- No hit object anywhere: the study found none worth anything while the stack is large; for the reflections no
  single ray query stands for what the pass shades (the end of a walk through glass, the water under the camera's
  ray, its own ray's hit).
- `specular[p]` is still read and written after the reflections' call (one read, one write a pixel).
- The water's path's own regrouping at every turn (`compat-1.md` S5, `TGPT_REGROUP_WATER`): not touched.
- The recursion depth (S7): still 1. One character when a pass becomes the rule.
- The boiling filter (`pt_gi_boil`) is still a pass of its own after the reuse pass; it could be folded into
  `pt_gi_reuse` (a compute pass can synchronize a thread group).

## 7. HAND-OVER: what to build, the run, what to read

### Build
- Deploy the DLL of this copy's build as it is, once job `step2b` has ended with exit code 0:
  `%TEMP%\tgb\ser2\TGPathTracing.Native.dll` (configured `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON
  -DTGPT_SER_SPLIT=ON`; no NRD in it, the run uses no denoiser; its hash: the job's log, last line). The plugin is
  unchanged. Its pass that ships is the hint-less one, the main tree's text.
- Or merge the copy and configure the main build with `-DTGPT_SER_SPLIT=ON`. Nine headers compile beside the build
  of every day: five ray generation passes (5 to 8 minutes of a core each), the split and the reuse pass in both
  shader models. The copy's `pt_gi.hlsl` and `tests/source_checks.py` carry the main tree's lines of 12:12 as they
  are: they merge as unchanged there.
- THE FIRST START makes five large ray generation passes the driver has not seen, after the others, two at a time,
  and the split pass as a compute pass. Start once to fill the driver's cache, look at free memory first (PT-96).

### The run
`.\tools\harness\Run-PtHarness.ps1 -Scenario tavern-room -Recipe ser-split`, then `metal`, then `water-day` (no
denoiser, 16 frames, 25 s a step; 8 traced steps: about 5 minutes a scene).

| Step | `Speed trial` | What it is |
|---|---|---|
| 2-as-it-ships | 0 | today's passes (the hint-less call) |
| 3-yardstick-plain-passes | 8 | the plain compute passes |
| 4-split-reuse-a-pass-of-its-own | 16 | step 1 alone |
| 5-top-regrouped-once-nothing-carried | 32 | step 1, the call at the top of the pass |
| 6-ahead-regrouped-after-the-first-ray | 48 | step 1, the call after the first ray |
| 7-gain-only-water-and-glass-regrouped | 64 | as 48, for threads that met water or glass alone |
| 8-split-as-plain-passes | 24 | the split pass and the reuse pass, nothing regrouped |
| 9-as-it-ships-again | 0 | the run's drift |

### What to read
1. AT THE START, in the tracer's log, one line a pass: `Split trial: <pass>: made in N s; the driver's stack for its
   ray generation shader X bytes, for the pipeline Y bytes a thread` (the two that ship are logged too). THE NUMBER
   THAT DECIDES: X for 32, 48, 64 against 2,560 (today's) and 128 (the reflections'). `... could not be made`: that
   step is void (the pass that ships runs in its place).
2. When the number changes: `Speed on trial (N): the split trial: the bounced light's pass <name> as a ray
   generation pass, then the reuse pass; ...`.
3. In each step: the tracer's 10 s lines, "bounced light" (the sorted pass and the reuse pass together) and
   "reflections" (`measure_read.py`).
4. The pictures: every traced step against step 2 (`same_picture.py`): the same.

### What each step must read (the bounced light's stage, ms), written BEFORE any run
Known (tavern / town / beach): as it ships 29.5 / 35.1 / 32.2; no call at all 11.4 / 27.9 / 51.7; plain 14.2 / 27.8 /
63.6. TARGET (the coordinator's, 12:15): toward 11.4 / 27.9, and under 32 at the beach, with reordering on.

| Step | My guess (tavern / town / beach) | What it decides |
|---|---|---|
| 16 split | 25 to 29 / 31 to 35 / 30 to 32 | What the reuse in sorted order cost on top. Its stack line: still about 2,560 (the call is still in the loop). |
| 32 top | if its stack is small: 12 to 15 / 28 to 30 / 33 to 45 | Nothing carried. A small stack and a fast room: the loop was the cause. At the beach it may lose part of the gain: the threads are regrouped before anyone knows where its ray goes (by the driver's own order alone). |
| 48 ahead | if its stack is small: 12 to 16 / 28 to 31 / 30 to 33 | The ray's hit carried; the threads regrouped after their first ray is known (the beach's gain should be kept). THE MAIN CANDIDATE for the rule. |
| 64 gain | 11.5 to 13 / 28 / 30 to 34 | In a room hardly any thread calls. If it is as fast as 32 or 48 indoors and as good as 48 at the beach, it is the rule. Above 20 in the tavern with a large stack line: the driver charges every thread for a call some take. |
| 24 split, plain | 14 / 28 / 64 | The split costs nothing in the screen's order (one more dispatch). |

If the stack line of 32 still reads about 2,560: the guess of section 3 is wrong; 32 to 64 will then be near 29.5 in
the tavern, and the next job is what the driver keeps that is not our values (Nsight Graphics' live-state view, or
the unmarked arrays: `ser1-tools\ir_alloca_uses.py` finds them).
Reflections (9.3 / 24.6 / 95.4 as they ship): steps 32 to 64 within a millisecond of step 0, or better.

## 8. When the game has answered: what goes (the same day; not this job)

If 48 or 64 (or 32) is the rule:
- `pt_gi.hlsl`: `gi_pixel`'s tail (RTXDI's temporal resampling and the finite check) in every build: the split is
  then the only form, also for the compute passes of Shader Model 6.6 and of cards that do not reorder
  (`pt_gi_split*.h` become `pt_gi*.h`); `pt_gi_reuse` becomes a pass of every build, in both shader models.
- The hint-less call inside the loop in `gi_pixel` (the line of 12:12), and the losing arrangements.
- `gi_pixel` and `gi_pixel_split` are then two copies of one computation: one function, the call under `PT_SER`.
  `split_trace` and `trace_from_surface` likewise (one function that hands the query's hit back, and its fetch).
- The `Speed trial` numbers 16 to 64 and the recipe `ser-split`; the switch `TGPT_SER_SPLIT` and every `#ifdef` of
  it; the losers' lines of the check.
- The reflections' 3-bit key if it lost (then `split_regroup_seen` goes; the guide's one store can stay either way).
- The study (`TGPT_SER_STUDY`, `pt_ser_study.hlsli`, its recipes and check lines): `ser-1.md` lists it.
- `PROJECT-CONTEXT` 9.13 ("reordering was within noise"), and the rule to write down if the guess holds: a
  regrouping call never stands inside a loop of a big pass, and never before work in the screen's order.

## 9. Workarounds and loose ends met

- `trace_from_surface`'s up to five queries a ray (what the wind moves, a near back face, the hero's body): a
  workaround in the tracing itself; `split_trace` copies it line for line (two copies to keep side by side).
- The static variables that carry a ray's kind and counts between helpers (`g_bounceRay`, `g_clothMet`, the ray
  cone, the ray's seed): the split pass sets them again after the call by hand. A static set before the call and
  read after it would be carried silently: the driver's stack line is the watch for that.
- The unmarked 16-byte arrays (`v[i]` on a 4-vector) are still in every pass: the next suspect if 32's stack stays
  large.
- More rays a pixel than 1 are regrouped by the first ray alone in 48 and 64, and not at all after the top in 32.
- Nothing here was measured on a GPU with a reordering pass. The numbers of section 7 are guesses, named as such.
- The offline test's start needed `TGPT_TEST_STARTUP_SECONDS` (the Shader Model 6.6 passes were cold in the driver's
  cache: two starts ran out of the 180 s). Not a code fault.

## 10. Tools (`scratchpad\ser2-tools`, not in the copy)

`build.ps1` (stages: configure with the main tree's generated headers copied in, shaders with
`Build-PtShaders.ps1 -Throttle 3`, the C++ syntax check, the DLL), `job.ps1` (one detached job of those stages, with
a log and a done-marker), `frontend-check.ps1`, `same-source.ps1`, `compare-shaders.ps1`, `changed.ps1`, `eol.ps1`,
`offline.ps1` (section 11). The counts of what a compiled pass carries: `scratchpad\ser1-tools\ir_stats.py`,
`ir_live.py`.

## 11. The one offline check: step 1 gives the same picture

What it is: the test's own speed-1 mode (`tests/pathtracing_speed1_checks.inl`: a town of the test's making at 1920 x
1080; four views, 16 frames from a reset each: the switch off, off again, on; "no pixel differs by more than two runs
without it do"; and its proof that it sees a changed picture, Indirect Color 0.9), with the split's number as the
switch, on Windows' own runtime (`TGPT_TEST_SPEED1_ONLY=1`, `TGPT_TEST_NO_AGILITY=1`, `TGPT_TEST_SPEED1_SWITCHES=16`).
No reordering pass is made there (Shader Model 6.6), so `Speed trial` 16 runs the split pass as a plain compute pass
and the reuse pass after it, against the plain pass of every day. It checks step 1's hand-over of the frame's own
sample and the tracer's dispatching; it says nothing about a reordering pass.

RESULT (12:19 to 12:24, the DLL of 12:01, whose step-1 compute passes have the same source text as now, but for one
empty `;` line): **speed-1 checks passed**, no failed frame. Pixels that differ, switch on against off / two runs
off: the town 443 / 671 (largest 0.041 / 0.041), the shore 3,103 / 3,103, under the water 0 / 0, the town over the
water 50,945 / 50,945 (means 0.099529 / 0.099532). Indirect Color 0.9: 1,499,148 pixels differ (the check sees a
change). The tracer's log: "Speed on trial (16): the split trial: the bounced light's pass split (its reuse across
frames a pass of its own) as a plain compute pass, then the reuse pass" each time the switch was on. Times (not
judged; plain passes, Windows' runtime): the town's bounced light 12.24 ms with it, 12.56 and 13.35 without.
Output: `%TEMP%\tgb\ser2-offline-test.txt`, `%TEMP%\tgb\ser2-offline.log`.

## 13. The new set's compiled counts (12:47)

`ser1-tools\ir_stats.py` and `ir_live.py` on the headers in `%TEMP%\tgb\ser2\generated\pathtracing`: the values defined
before a regrouping call and used after it (an upper bound of what DXIL says must survive, NOT what the driver keeps).

| Pass | instructions | ray queries | calls | bytes alive across each call | unmarked arrays |
|---|---|---|---|---|---|
| as it ships, hint-less (`pt_gi_ser.h`, = the main tree's bytes) | 307,586 | 116 | **3** | 266 / 547 / 535 | 106 (1,696 bytes) |
| 16 split (`pt_gi_ser_p1.h`) | 304,317 | 113 | 3 | 217 / 498 / 486 | 106 (1,696) |
| 32 top (`pt_gi_ser_p2.h`) | 306,696 | 114 | **1** | **24** (the pixel's index, the dispatch's size) | 109 (1,744) |
| 48 ahead (`pt_gi_ser_p3.h`) | 306,865 | 114 | **1** | 146 | 109 (1,744) |
| 64 gain (`pt_gi_ser_p4.h`) | 306,878 | 114 | **1** (under a condition) | 145 | 109 (1,744) |
| reflections, 32 to 64 (`pt_specular_ser_p2.h`) | 420,178 | 152 | 1 (3 bits) | 428 | 109 (1,744) |
| (the study's "keys", 32: `ser1`'s `pt_gi_ser_s2.h`) | 307,749 | 116 | 1 | 540 | 106 (1,696) |

Every call is the same operation (`dx.op.maybeReorderThread`, opcode 268, with an empty hit object). The unmarked
arrays are in every pass, the reflections' too (whose driver stack is 128): by themselves they do not make 2,560.

## 14. The coordinator's question (12:45): the shipped hint-less call gives about half of the study's gain

The game (run `20261010-123027`, main build): the bounced light's pass with `dx::MaybeReorderThread(0u, 0)` 38.5 ms
in the tavern, 54.0 in the town; the study's "same place, no bits" (`Speed trial` 32 + mode 4 x 256) 29.5 and 35.1.

The two listings side by side (main's `build\native\generated\pathtracing\pt_gi_ser.h` of 12:19, read only; `ser1`'s
`pt_gi_ser_s2.h`):
- The operation is the same: `dx.op.maybeReorderThread(i32 268, %dx.types.HitObject <HitObject_MakeNop>, hint,
  bits)`. The shipped pass passes `i32 0, i32 0`; the study's passes two values computed at run time (`%3824`,
  `%3779`), which were 0 in mode 4.
- **The shipped pass has THREE calls; the study's has ONE.** Main's listing lines 1294, 4404 and 4408: one on the
  path where the pixel's ray was not traced (it points under the surface: 266 bytes alive), and two after the ray,
  one for each of its two other outcomes (547 and 535 bytes). With constant operands the call is the same on every
  path, so the compiler copied the code after the ray into each path and the call with it (`ser-1.md` saw the same
  of the 16-bit key, whose key was a constant on two of the paths). With operands the compiler cannot know, the
  paths join first and there is one call. So the shipped pass has three places a thread can be moved at, the study's
  one. NOT PROVEN that this is the 9 to 19 ms; it is the one difference in the code. (The study's own pair for the
  16-bit key, one call against three, was not faster: 54.8 against 50.9; so the call count alone may not be all.
  Conditions of the two runs also differed: another build, Sunshine on the card in both.)
- What my split pass uses, and why: the constant form, `dx::MaybeReorderThread(0u, 0)`, in "top", "ahead" and "gain".
  There the call stands OUTSIDE the rays' loop and before the paths part, and the compiled listing has ONE call in
  each (counted, above). The constant form says what is meant (no hint) and needs no value of the frame. "split"
  (16) keeps the shipped line on purpose (three calls, as the pass that ships), so that 16 against 0 measures the
  tail alone.
- For the pass that ships, if it is to stay until the rebuild is the rule: to have one call, the hint-less line
  needs operands the compiler cannot fold (as the study's), or the call moved before the paths part. Either needs a
  compile and a look at the listing (`grep -c "dx.op.maybeReorderThread(i32 268"` in the header must say 1). NOT
  built here: "ahead" and "gain" are that change and more.
- The driver's stack line in every build (the coordinator's ask): done in `tracer.cpp` `adopt_passes`, outside the
  switch, after the two passes that ship are adopted: `Shader Execution Reordering: the driver's stack a thread, the
  bounced light's pass X bytes, the reflections' pass Y bytes`, once a start (`GetShaderStackSize`, no shader
  compiled for it). The split build logs the same for each of its own passes (`Split trial: ...`). A source check
  holds the line. With it the main build after a merge says what today's three-call pass keeps.