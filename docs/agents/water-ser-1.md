# water-ser-1: the water's path as a pass of its own, built for Shader Execution Reordering (PT-99, PT-110)

Worker `waterser1`, copy `...\scratchpad\wt\waterser1`, base `base87`, brief `docs/agents/briefs/brief-water-ser-1.md`.
2026-10-10. Nothing here ran in the game, nothing was deployed or committed, nothing was written into the main tree or
the game folder. No GPU test was run (the brief: a worker's test cannot make a new reordering pass).

The four claims, kept apart: **built**: see below; **passes the harness**: no run (the coordinator's); **seen in game**:
no; **the owner says it's fixed**: no.

## State when stopped

(kept current after every step; the newest line is the truth)

- 15:25 Started. Read the brief, `spec-ser-1.md`, `ser-2.md`, `rule-2.md`, AGENTS.md's rules.
- 15:33 Configured `%TEMP%\tgb\waterser1` (`-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON -DTGPT_WATER_PASS=ON`), the main
  tree's 48 generated headers copied in. Shader job started (`Build-PtShaders.ps1 -Throttle 3`).
- 15:35 The two water passes compiled (50 s each). Counted: ONE regrouping call each; values alive across it 104 bytes
  (arrangement 1) and 125 bytes plus the hit object (arrangement 2), by DXIL: the path's state and nothing else (3.2).
- 15:40 Source checks 115 of 115 (one new: `test_path_tracing_water_pass_on_trial_default_off`; three counts and one
  check of the water's turn follow the trial). The tracer's C++ compiles with the switch (`cl /Zs`, 0 errors). A build
  WITHOUT the switch: every pass that ships gets base87's source text (12 of 12 cases, `waterser1-tools\same-source.ps1`)
  and `tracer.cpp` base87's text (`same-tracer.ps1`, 2,208,019 lines the same).
- 15:42 The hand-off pass compiled (421 s): ONE call, 8 bits, 426 bytes alive by DXIL (the shipping pass: 427).
- 15:43 **THE TRIAL BUILD IS READY. HAND-OVER: section 4. STOPPED HERE. BUILT, NEVER RUN: the game is the test, and the
  coordinator's.** DLL `%TEMP%\tgb\waterser1\TGPathTracing.Native.dll`, 31,224,320 bytes, built 15:43:27, SHA-256
  `132633A03519F83B1711F22E5EFF376088B4EA13F73462ED4452BD66A78DA183`; configured `-DTGPT_WITH_NRD=OFF
  -DTGPT_RAIN_WETNESS=ON -DTGPT_WATER_PASS=ON` (no NRD, no Mega Geometry: the run uses no denoiser). 0 errors, 0
  warnings of ours. The plugin is not changed. Its passes that ship are the main tree's own compiled headers (copied
  from `build\native\generated\pathtracing`; the same source text, checked above).
- Checked again after the last shader edit (15:38, a macro named for the hand-off's call, token for token the same
  call): source checks 115 of 115; every pass that ships the same text as base87 (12 of 12); line endings as base87's.

## 1. What is built, in plain words

One compile switch, CMake's `TGPT_WATER_PASS`, OFF by default. OFF: no pass is compiled with it, the tracer holds no line
of it: the build of every day (checked, above). ON: three more ray generation passes beside the two that ship (compiled
as always), and they run only in a frame whose hidden `Speed trial` asks for them. Reordering is never switched off by
any of it: it adds regrouping.

| `Speed trial` | What runs in the reflections' stage |
|---|---|
| 0 | the passes that ship (the reflections' pass, the water's path inside it) |
| 16 | the hand-off pass, then the water's pass, ARRANGEMENT 1: one regrouping call at the TOP of every turn of the water's path, before the turn's ray, no hint, no hit object; a thread carries the path's state alone |
| 32 | the hand-off pass, then the water's pass, ARRANGEMENT 2: one regrouping call right AFTER every turn's ray, with a hit object made from the ray's query (the driver orders threads by where their hits lie) and a 2-bit hint by what the turn does (0 a scatter point in the water's body, 1 the water's surface, 2 anything else met, 3 nothing met); the hit fetched after the call, from the hit object |
| 96 (32 + 64) | as 32, with no hint (0 bits): the hit object alone |
| with 8 | the plain compute passes, as before (the trial does not run) |

- **The hand-off pass** (`pt_specular.hlsl` built with `PT_WATER_HANDOFF`): the reflections' pass as it ships, but a
  pixel the camera sees as water leaves at its top (before any store), and its one call regroups by today's key in 8
  bits ("feeds" in bit 7, the material's number's 7 low bits: the study's mode 3, as fast as 16 bits in its run).
- **The water's pass** (`pt_water_pass.hlsl`, entry `water`): launched over the whole screen; a pixel that is not
  camera water leaves at the top, before any regrouping (a thread that never reaches a call takes no part in it). So
  it is a SCREEN MASK, not a list: why: no new buffer, no change to `pt_prepare` (a big pass: code switched off still
  costs there), and an empty launch costs one surface read a pixel. If the run shows that launch in the tavern, a list
  written by `pt_prepare` and an indirect dispatch is the next step.
- **The same computation**: what the water's pass computes for a pixel is `pt_specular`'s, number for number: the same
  random stream (the pixel, the frame, 5) with the two numbers `pt_specular` draws for every pixel's reflection direction
  before it knows the pixel is water, the same ray to the water's surface (`trace_water`), the same `water_path` (the one
  function, its hooks under `#if PT_WATER_PASS`), the same two stores (`specular[p]` the whole light and the guide's
  distance, `seenGuide[p]` `SEEN_WATER`; with `OPTION_NO_SPECULAR` the guide's 0 alone). `shade_seen` and `shade_path`
  hand a water hit to `water_path` with weights of exactly 1, so nothing of them is lost. Arrangement 2 draws the turn's
  free flight before the ray (the same two random numbers in the same order: the ray draws none), as `TGPT_REGROUP_WATER`
  does; arrangement 1 draws it where it always was.
- **Statics across the call** (`ser-2.md` 9: a static set before a call and read after it is carried silently): in
  arrangement 2 the turn's ray number and start (`ray_seed`) are set again after the call (a pure function of the
  path's origin and direction), and the hit's transform is read from the hit object. The listing shows no static
  carried in either arrangement (3.2).
- **Not in this build** (the design's step 8): a reflection ray of another surface that meets water, the walk through
  glass that ends on water, and the bounced light's rays that meet water keep today's inline path (in the hand-off pass,
  `pt_gi`, `pt_walk`). And the patch of waves a scatter point makes for itself (`water_sun`, up to 40 steps) has no call
  of its own: one call a turn.

## 2. Files (all in the copy)

- `native/pathtracing/shaders/pt_water_pass.hlsl` (new): the water's pass, its entry `water`. Refuses to compile without
  `PT_SER` and `PT_WATER_PASS`.
- `native/pathtracing/shaders/pt_water_path.hlsli` (LF, as it was): under `#ifdef PT_WATER_HANDOFF` the macro
  `PT_REORDER_8`; under `#ifdef PT_WATER_PASS` `water_pass_trace` (arrangement 2: `trace_closest`'s query, the call by a
  hit object, the fetch from it); in `water_path`'s loop four hooks under `#if PT_WATER_PASS == 1` / `== 2` (the top
  call; the free flight drawn first, which `TGPT_REGROUP_WATER` already had; the ray; the row read again after the call).
- `native/pathtracing/shaders/pt_specular.hlsl`: three `#ifdef PT_WATER_HANDOFF` blocks (the early return, `water`
  false, the 8-bit call).
- `CMakeLists.txt`: the option (refused beside `TGPT_SPEED_MEASURE` and `TGPT_REGROUP_WATER`), three `dxc` commands
  (after the two reordering passes' loop): `pt_water_pass_1.h` (`waterSerT`), `pt_water_pass_2.h` (`waterSer`),
  `pt_specular_ser_handoff.h` (`specularSerHandoff`); the tracer's definition `TGPT_WATER_PASS=1`.
- `native/pathtracing/src/tracer.cpp`, every line under `#ifdef TGPT_WATER_PASS`: the three headers included (into
  `tgpt::shaders`, beside `pt_shaders.hpp`, which is unchanged: a source check holds it free of `#if`); the state and
  `Passes` fields; `release_all`; `make_water_trial` (the three passes made after every other, ONE AT A TIME, each in its
  own try, each logging the driver's stack); its call at the end of `build_passes`; `adopt_passes` (a shader table of its
  own, 3 records, so the shipping table is not touched); the frame's pick (`st.waterTrial`, after `st.plainPasses`); the
  dispatch in the reflections' stage (both passes inside it, so "reflections" is their sum); two log lines of the
  "Speed on trial" chain.
- `tests/source_checks.py`: the new check at the end of the class; `-I "${TG_RTXDI}/Include"` 5 -> 7 and
  `${PT_SPEED_DEFINES}` 3 -> 5 (the two new command lines); `test_path_tracing_lamp_tree_is_the_rule`'s two lines about
  the water's turn follow the hooks (the turn's one `trace_closest` line, as far as before: 1e30).
- `tests/ingame/scenarios.json`: recipe `water-pass` (after `spec-study-water`).
- The plugin is not changed (the hidden `Speed trial` takes any number).

## 3. Counted

### 3.1 The compiled passes (`ser1-tools\ir_stats.py`; calls: `grep -c "dx.op.maybeReorderThread(i32 268"`)

| Pass | instructions | ray queries | regrouping calls | bytes alive across the call (DXIL) |
|---|---|---|---|---|
| reflections, as it ships (`pt_specular_ser.h`, main's; `spec-ser-1.md`) | 420,028 | 152 | 1 | 427 (the driver keeps 128) |
| the water's pass, arrangement 1 (`pt_water_pass_1.h`) | 104,605 | 72 | **1** (NOP hit object, 0, 0) | **104** |
| the water's pass, arrangement 2 (`pt_water_pass_2.h`) | 104,654 | 72 | **1** (hit object from the query, computed hint and bits) | **125** + the hit object |
| the hand-off pass (`pt_specular_ser_handoff.h`) | 418,672 | 151 | **1** (8 bits, computed hint) | 426 |
| (the water's regrouping inside today's pass, `spec-ser-1.md` step 12: 2,624 bytes kept by the driver) | | | 3 | 443 / 701 / 874 |

### 3.2 What the water's passes carry across their call (`waterser1-tools\ir_live_show.py`)

Arrangement 1, 104 bytes: the pixel (2 words), the path's origin, direction, weight and light (4 x float3), the
medium, the random state, the guide's distance and one more float, and the six counts and flags (`delta`, `primary`,
`asked`, `crossed`, `glinted`, `lampsAsked`, crossings, vertices). Arrangement 2 adds the drawn channel and free flight,
"in the water" (1 bit) and the hit object. No static (`g_rayOrigin`, `g_raySeed`, `g_hitToWorld`, `g_candidateT`, the ray
cone, the water medium's) and no part of the surface is carried. The 85 small arrays "kept by lifetime marks" (1,360
bytes) are the same in every pass of the tracer, the shipping reflections' pass among them, whose driver keeps 128.

## 4. HAND-OVER: what to build, the run, what to read

### 4.1 Build
- Deploy this copy's DLL as it is: `%TEMP%\tgb\waterser1\TGPathTracing.Native.dll` (SHA-256 `132633A0...DA183`, the top
  of this file). No NRD and no Mega Geometry in it (the run uses no denoiser; the last step only sets the row back).
  The plugin is unchanged.
- Or merge the copy and configure the main build with `-DTGPT_WATER_PASS=ON`: three more headers compile (the hand-off
  pass about 7 minutes of a core, each water pass about a minute). Without the switch the merged tree builds today's
  passes and tracer to the text (section "State").
- THE FIRST START makes three ray generation passes the driver has not seen, after every other pass and ONE AT A TIME:
  the hand-off pass is as large as the reflections' pass that ships (4 to 9 minutes for the driver), the water passes a
  quarter of it. Look at free memory first (PT-96).

### 4.2 The run (no denoiser, 16 frames, 25 s a step; as it ships first and last)
`.\tools\harness\Run-PtHarness.ps1 -Scenario water-day -Recipe water-pass`, then `-Scenario metal`, then
`-Scenario tavern-room` (5 traced steps, about 4 minutes a scene). The beach first: it is the question.

| Step | `Speed trial` | What it is |
|---|---|---|
| 2-as-it-ships | 0 | the passes that ship |
| 3-water-pass-regrouped-at-each-turns-top | 16 | the hand-off pass, then the water's pass, arrangement 1 |
| 4-water-pass-hit-object-and-kind | 32 | the hand-off pass, then the water's pass, arrangement 2 (hit object, 2-bit hint) |
| 5-water-pass-hit-object-no-hint | 96 | as 4, no hint |
| 6-as-it-ships-again | 0 | the run's drift |

### 4.3 What to read
1. AT THE START, the tracer's log: three lines `Water pass trial: <pass>: made in N s; the driver's stack for its ray
   generation shader X bytes` (beside every build's `Shader Execution Reordering: the driver's stack a thread, ...`).
   THE NUMBERS THAT DECIDE: X of the two water passes against the 2,624 bytes of the water's regrouping inside today's
   pass (`spec-ser-1.md` step 12) and the 128 of the reflections' pass. By the listing (3.2) a thread carries 104 and 125
   bytes: I expect 100 to 250. The hand-off pass: about 128, as the pass that ships. `... could not be made`: that
   arrangement's steps run the passes that ship (void; arrangement 2 is the first pass of ours with a hit object made
   from a ray query, never run on this driver). (Three more "Shader pass specularSer: D3D12 state object" lines are the
   trial's passes too: `make_raygen` names every pass that is not `gi` so.)
2. When the number changes: `Speed on trial (16): the water's own pass, regrouped at the top of each turn ...`, `(32):
   ... by a hit object, the turn's kind in 2 bits ...`, `(96): ... no hint ...`. "... was not made or the reordering
   passes do not run: nothing changes" makes the step void.
3. In each step, the tracer's 10 s lines, "reflections" (`measure_read.py`): the hand-off pass and the water's pass
   together. The bounced light is the same pass in every step: its numbers are the run's drift.
4. The pictures: every traced step against step 2 (`same_picture.py`): the same.

### 4.4 What each step should read, written BEFORE the run (the reflections' stage, ms: tavern / town / beach)
Known (runs `20261010-144426`, `-151637`): as it ships 9.8 / 24.9 / 92.0 (85 to 95 at the beach); the key in 8 bits
inside today's pass 9.8 / 24.0 / 84.9; no hint inside today's pass 10.7 / 30.8 / 70.8; the water regrouped at every turn
inside today's pass 10.7 / 24.7 / 106.3. TARGET: the beach well under 70; the town and the tavern no slower than today.

| Step | My guess (GUESSES) | If it reads otherwise |
|---|---|---|
| 3 (16) | 9.8 to 10.2 / 24.0 to 24.6 / 45 to 65 | tavern or town +0.5 or more: the water's pass's empty launch costs (every pixel's surface read): a list of the water's pixels from `pt_prepare` and an indirect dispatch. Beach 85 or more: the per-turn call costs what it gains; read X (4.3.1): far over 250 means something else is carried |
| 4 (32) | as 3 in the tavern and the town / 40 to 65 | faster than 3 at the beach: ordering by where the hits lie (and by the turn's kind) pays: the rule's arrangement. Slower than 3: the call after the ray holds the turn's state, or the hint costs (read 5) |
| 5 (96) | between 3 and 4 at the beach | faster than 4: the hint costs, as it did in the bounced light's pass |
| 6 (0) | = 2 | the run's drift |

## 5. When the game has answered: what goes (the same day; not this job)

If an arrangement reads well under 70 at the beach and the town and the tavern are no slower than steps 2 and 6:
- **It becomes the rule**: `pt_water_pass.hlsl` with that arrangement, a third ray generation pass of every build that
  reorders, dispatched after the reflections' pass inside its stage, its record in the one shader table (3 records).
- **Out of `pt_specular.hlsl`**: the camera's water pixels' branch (the `water` flag, its `trace_water` block, its
  `specular[p]` / `seenGuide[p]` store block): the water's pass owns those pixels; the hand-off's early return becomes the
  pass's, the key 8 bits or 16 by the town's numbers. The compute passes (a GPU that does not reorder, the fallback, the
  yardstick 8) keep the water inside the reflections' pass, or get the water's pass as a compute pass: a decision then.
- **Out of `pt_water_path.hlsli`**: the losing arrangement's hooks, `PT_WATER_HANDOFF`'s macro becomes `PT_REORDER`'s
  8-bit form, and `TGPT_REGROUP_WATER` (perf2's calls inside today's pass: 106 ms at the beach in the study) with its
  `PT_REGROUPS` lines, its `Speed trial` numbers 2 and 4, its CMake option and checks.
- **Out of the build**: the switch `TGPT_WATER_PASS`, `Speed trial` 16, 32 and 64, the recipe `water-pass`, the check
  `test_path_tracing_water_pass_on_trial_default_off` (rewritten as "is the rule"); the counts it moved stay.
- **Next** (the design's step 8, `spec-ser-1.md` 3.1): the other paths that meet water (a reflection ray, a walk through
  glass, the bounced light's rays) hand their ray (origin, direction, weight, pixel) to the water's pass through a queue,
  and the inline water path leaves `pt_specular`, `pt_gi` and `pt_walk` (less code in each: `compat-1.md` S6).
If none wins: everything above under the switch comes out the same day; the driver's stack lines (4.3.1) and the
listing's counts (3) say where the next look goes.

## 6. Workarounds and loose ends met

- The water's path still has two homes after this trial: its inline call in `shade_path` (for reflection rays, glass
  and the bounced light that meet water) and the water's pass. If the pass wins, the design's step 8 moves the others to
  it through a queue, and the inline call leaves `pt_specular`, `pt_gi`, `pt_walk`.
- `make_raygen` logs every ray generation pass that is not `gi` as "Shader pass specularSer: D3D12 state object": the
  three trial passes add three such lines; the trial's own "Water pass trial: ..." lines name them.
- `TGPT_REGROUP_WATER` (perf2's calls inside today's pass) lost in the study (106 ms at the beach); it still stands, and
  this trial uses its "free flight drawn first" lines. For the clean-up when this trial is decided.
- Nothing of the tracer's workarounds was removed: this is a trial.

## 7. Tools (`scratchpad\waterser1-tools`, not in the copy)

`build.ps1` (stages configure / shaders / syntax / dll / restamp, one a call), `same-source.ps1` (the passes that ship,
base87 against the copy, preprocessed, 12 cases), `same-tracer.ps1` (`tracer.cpp` without the switch, preprocessed),
`ir_live_show.py` (ser1's `ir_live.py` that also names each live value), `edit_checks.py`, `fix_check.py`.
