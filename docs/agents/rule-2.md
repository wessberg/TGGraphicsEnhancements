# The bounced light's split pass, regrouped once after the first ray, becomes the rule (PT-99, PT-110): worker "rule-2", 2026-10-10

Copy `scratchpad\wt\rule2` (base `wt\base85`). Brief: `docs/agents/briefs/brief-rule-2.md`. Scripts:
`scratchpad\rule2-work\`. Build tree: `%TEMP%\tgb\rule2`. Nothing here ran in the game, nothing was deployed,
nothing committed, nothing written to the main tree or the game folder. No GPU test was run.

The four claims, kept apart: **built**: see "State when stopped"; **passes the harness**: no run (the
coordinator's); **seen in game**: the arrangement was (run `20261010-130621`, `Speed trial` 48 of `ser2`'s build), the
rule as built here was not; **the owner says it's fixed**: no.

## State when stopped

- (kept current; the LAST line is the state)
- Started. Read the brief, `ser-2.md` (1, 5 to 8, 13, 14), `rule-1.md`, AGENTS.md's rules, `AGENT-WORKERS.md`.
- Found: the copy's shader sources are byte for byte the `ser2` copy's, so `%TEMP%\tgb\ser2\generated\pathtracing`
  holds the base's compiled passes (its `pt_gi_ser_p3.h` is "ahead", 48). The plan for "no new behaviour": compile
  everything here and compare the compiled bytes with those, pass by pass.
- All edits done (list below). Source checks 114 of 114. The plugin builds: 0 warnings, 0 errors
  (`%TEMP%\tgb\rule2-plugin`; the plugin is not changed). The text the compiler sees: checked (section "How I know").
  DXC's front end on the variants the build does not make (lamp trial, measuring build, water regrouping): 9 of 9, no
  error.
- 14:00 to 14:20, ONE background job (`rule2-work\build.ps1`, log `%TEMP%\tgb\rule2-job.log`): configured with
  `-DTGPT_WITH_NRD=OFF -DTGPT_RAIN_WETNESS=ON`; **all 42 shader headers compiled, 0 failed** (`Build-PtShaders.ps1
  -Throttle 3`, 1,099 s; with NRD on there is one more, `pt_nrd_prep`, not touched here and not compiled); **the DLL
  links, 0 warnings** (`%TEMP%\tgb\rule2\TGPathTracing.Native.dll`, 26,203,648 bytes, SHA-256
  `E60E91EC16E4546A2DDCC0F0475614C3B5541A3B2EF8F626AF0159A91CA9B05A`, never deployed; built without NRD, so not the
  DLL to deploy: merge and build the main tree).
- **40 of the 42 compiled passes are byte for byte the base's** (`compare.py`, section "How I know"), among them the
  bounced light's reordering pass = the base's "ahead" (48) and the reflections' three passes as they ship. The two
  others are the bounced light's compute passes, the new arrangement on purpose.
- A dry merge against the main tree (14:20): 14 changed, 2 new, 2 deleted, **0 conflicts**.
- **DONE, STOPPED.** Source checks 114 of 114; the plugin 0 warnings, 0 errors; every pass compiles; the DLL links.
  Not done by design: no GPU test, no game, no deploy, no commit. NOT verified here: the NRD path (built with NRD off)
  and anything on a GPU: the coordinator's merged build and its three scenes are the test.

## What is now the rule, and what is out

| What | Was | Now |
|---|---|---|
| the bounced light's pass | as it shipped: ONE hint-less call INSIDE the loop over the pixel's rays (three calls in the compiled pass), RTXDI's reuse across frames at the end of the pass | THE RULE, in every build: `ser2`'s "ahead" (48). `pt_gi.hlsl` `gi_pixel` traces the pixel's first ray, regroups ONCE (`dx::MaybeReorderThread(0u, 0)`, under `#ifdef PT_SER` alone), reads the pixel and its surface again, fetches the hit, runs the rays' loop with no call in it, and stores the frame's own sample whole |
| the compute passes of it (Shader Model 6.6 and 6.9: a GPU that does not reorder, the fallback, the yardstick 8) | the pass as it shipped, without the call | the same one function, without the call and the six lines after it: one computation written once |
| ReSTIR GI's reuse across frames | the end of `pt_gi`; in the split trial a pass of its own (`pt_gi_reuse`) | `pt_gi_reuse.hlsl`, a compute pass in both shader models, made in every build and dispatched right after `pt_gi` in every frame, inside the bounced light's stage. The hand-over of the frame's own sample: `pt_gi_own.hlsli` (`own_sample_store`, `own_sample_load`; were `split_store_own`, `split_load_own`) |
| the reflections' pass | as it shipped; in steps 32 to 64 the trial's 3-bit key and its guide stored once | AS IT SHIPPED, to the byte: `pt_specular.hlsl` is base84's file (sha 5E28D273...) |
| "top" (32), "gain" (64), "split" (16), "split as plain" (24), the hint-less call inside the loop | trial passes | OUT |
| the reordering study | CMake `TGPT_SER_STUDY`, `pt_ser_study.hlsli`, recipes `ser-study`, `ser-study-keys`, its check lines, its `PT_REORDER` switch in `pt_common` | OUT |
| the split trial's switch | CMake `TGPT_SER_SPLIT`, `pt_ser_split.hlsli`, nine headers, the tracer's trial passes, its shader table records 2 to 7, `Speed trial` 16 to 64, recipe `ser-split` | OUT (16 to 80 do nothing in a build for players, as every number from 16 up) |
| the start-up line with the driver's stack of the two reordering passes | every build | STAYS |

## What the coordinator's run must read

Merge, build (every pass compiles again: `pt_common.hlsli` changed, comments and the study's `#if` alone), deploy. No
`Speed trial`, no recipe of the trials: `tavern-room`, `metal`, `water-day`, recipe `structures`, defaults.

1. The start-up log: `Shader Execution Reordering: the driver's stack a thread, the bounced light's pass 0 bytes, the
   reflections' pass 128 bytes` (`ser2`'s 48 read 0; the pass as it shipped 2,592).
2. The tracer's 10 s lines, "bounced light" (the two passes, `pt_gi` and `pt_gi_reuse`) and "reflections", no
   denoiser, against run `20261010-130621` (its step 48 for the bounced light, its step 0 for the reflections):

   | scene | bounced light | reflections | the frame (48's, less its slower reflections) |
   |---|---|---|---|
   | `tavern-room` | about 9.5 | about 9.6 | about 32.6 |
   | `metal` | about 22.7 | about 24.7 | about 66 |
   | `water-day` | about 28.9 | about 93 | about 137 |

3. The picture: the same as the build of every day (step 0 of that run), as 48's was.
4. If the bounced light reads 40 and more: the old pass runs (the DLL or the shaders are not this build's). If the
   reflections read 33.8 in the town or 101 at the beach: the trial's reflections' pass is still in.
5. `Speed trial` 8 (the plain passes) needs `TGPT_TEST_PLAIN_PASSES` at start again, as before the trials (their build
   made the plain passes always).

## How I know nothing else changed

1. The text the compiler sees (`rule2-work\textdiff.py`: dxc `-P` on the base's file with the trial's defines and on
   the copy's without them; `#line` marks and blank lines out; the copy's four new names put back):
   - the bounced light's reordering pass (`-T lib_6_9 -D PT_DXR12 -D PT_SER`, base with `-D PT_SER_SPLIT=3`): the
     function `gi_pixel` is `gi_pixel_split` line for line. What differs: the struct's field `what` and its three
     stores (read by "gain" alone), the helpers no pass of 48 called (`split_class`, `split_hint`,
     `split_regroup_seen`), and the old `gi_pixel`, which 48 compiled but never called; the entry calls `gi_pixel`.
   - the reuse pass (cs_6_6 and cs_6_9, base with `-D PT_SER_SPLIT=1`): the same text but the unused helpers.
   - the reflections' pass (lib_6_9 with SER, and cs_6_9): THE SAME TEXT.
   - the compute bounced light (cs_6_9) against base's 48: 48 less the call and the six lines that read the pixel
     and its surface again (and the entry). It runs the computation 48 runs; 48 gave the picture of every day in the
     game.
2. The compiled bytes (`rule2-work\compare.py`): this copy's shader sources were byte for byte the `ser2` copy's, so
   `%TEMP%\tgb\ser2\generated\pathtracing` (the same DXC, NRD off, the rain's wetness on) holds the base's compiled
   passes. Each pass of this build against its DXIL container (the C array) and DXC's shader hash:
   - **the same bytes, 40 of 42**: every pass but two, among them `pt_gi_ser.h` = `ser2`'s `pt_gi_ser_p3.h` (the
     "ahead" pass, 48, that ran in the game: hash `ac1dfb40045fe0acef62ba9baf57c28e`, 2,455,068 bytes), the
     reflections' `pt_specular_ser.h`, `pt_specular.h`, `pt_specular_dxr12.h` (as they ship), the reuse pass in both
     shader models, the walk, the prepare pass and every other.
   - **other bytes, 2**: `pt_gi.h` and `pt_gi_dxr12.h`, the bounced light's compute passes (a GPU that does not
     reorder, the fallback, "Speed trial" 8). They are "ahead" without the call (1 above); no build ever had them, so
     nothing to hold them against. NOT run anywhere: the offline GPU test runs them (Shader Model 6.6), the
     coordinator's.
   So a default build runs, on a GPU that reorders, exactly the passes of the base's build with "Speed trial" 48,
   with the reflections' pass as it ships.

## Files changed (all in the copy)

| file | what |
|---|---|
| `native/pathtracing/shaders/pt_gi.hlsl` | rewritten: one `gi_pixel` ("ahead"), `QueryHit` and `query_from_surface` (were `SplitHit`, `split_trace`); the lamp trial's lines under `#ifndef PT_NO_LAMP_AROUND`, drawn before the first ray (as before the loop); the measuring build's lines under `PT_SPEED_MEASURE` (its `SPEED_M_ONE_QUERY` in `query_from_surface` too); no reuse across frames |
| `native/pathtracing/shaders/pt_gi_reuse.hlsl` | no longer a trial: includes `pt_gi_own.hlsli`; the measuring build's "no reuse" switch |
| `native/pathtracing/shaders/pt_gi_own.hlsli` | new: the hand-over of the frame's own sample |
| `native/pathtracing/shaders/pt_ser_split.hlsli`, `pt_ser_study.hlsli` | deleted |
| `native/pathtracing/shaders/pt_specular.hlsl` | base84's file, byte for byte |
| `native/pathtracing/shaders/pt_common.hlsli` | the study's `PT_REORDER` switch out (base83's line); comments: which pass regroups by what; a line above `trace_from_surface` naming its copy |
| `native/pathtracing/shaders/pt_shaders.hpp.in` | the trials' headers out; `pt_gi_reuse.h`, `pt_gi_reuse_dxr12.h` in |
| `CMakeLists.txt` | both options, their commands and definitions out; `giReuse:pt_gi_reuse` in the passes' list; `pt_gi_own.hlsli` in `PT_SHADER_DEPENDS` |
| `native/pathtracing/src/tracer.cpp` | `giReuse` a pass of every build (both sets), adopted, released, dispatched after `pt_gi`; the trials' state, passes, logs, table records and `Speed trial` texts out; `plainToo` as before the trials; the shader table 2 records |
| `tests/source_checks.py` | `test_path_tracing_ser_split_on_trial_default_off` became `test_path_tracing_ser_split_is_the_rule`; the study's lines out; counts back (`-I` 5, `${PT_SPEED_DEFINES}` 3); three checks follow the reuse into `pt_gi_reuse.hlsl` |
| `tests/ingame/scenarios.json` | recipes `ser-split`, `ser-study`, `ser-study-keys` deleted |
| `CHANGELOG.md` | one entry, with the table |
| `docs/PROJECT-CONTEXT.md` | 9.13: "SER: within noise" replaced by the learning (four lines); the passes' list names `pt_gi_reuse` |
| `docs/agents/ser-1.md`, `ser-2.md`, `ser-study-run.md` | a line at their top |

No struct, export or ABI change. The plugin is not changed.

## Left, and why

1. **`query_from_surface` (pt_gi) and `trace_from_surface` (pt_common) are two copies of one loop** (`ser-2.md`
   section 8 asked for one function). Not done: one function for both changes the code of every pass that traces
   from a surface, the reflections' pass among them, which the brief keeps as it shipped. Both carry a "KEEP THE TWO
   SIDE BY SIDE" line, and the new check holds their lines equal. For a job of its own, with a byte check of the
   reflections' compiled pass.
2. `README.md` (line 181) still says the GI and reflection passes regroup by the material their rays hit; true of
   the reflections' pass alone since 12:12. Not mine to word (player text); for the coordinator. Likewise a comment
   in the plugin's `Settings.cs` ("the two big passes regroup by whether a pixel feeds the radiance cache"): the
   plugin is left unchanged.
3. The lamp trial (CMake `TGPT_LAMP_AROUND`, OFF) and the measuring build (`TGPT_SPEED_MEASURE`) were refused by the
   split trial's build; in the one function they keep their lines (DXC's front end takes both), never compiled
   whole here and never run. The water's regrouping trial (`TGPT_REGROUP_WATER`) compiles into the new pass as before.
4. A measuring build's "no ray" (16) and "one query" (4) now act on the first ray through `query_from_surface`, and
   its "no reuse" on `pt_gi_reuse`: the same switches, in the passes' new places.

## Workarounds met (the owner, 2026-10-07: "remove all the hacks as we go")

| where | what | removed? |
|---|---|---|
| `pt_gi.hlsl` `query_from_surface` | a copy of `trace_from_surface`'s loop (up to five queries a ray for the wind, a near back face, the hero's body) | not removed: left 1 |
| `pt_gi.hlsl` after the call | the pixel and its surface read again (4 texture reads) so that nothing of the surface is carried across the regrouping | not a workaround: the design (0 bytes carried) |
| statics carried across the call (`g_clothMet` as `firstCloth`, the ray seed set again with `ray_seed`) | set again by hand after the call | kept: the design of 48 |
