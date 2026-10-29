# Brief: what the game has answered becomes the rule (PT-99, PT-104) - one worker, a tidy job, no new behaviour

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/rule-1.md`; write it early, with "State when stopped" at its top after every step.

## Why

AGENTS.md, "Code that is switched off still costs frame time in the big passes": "once the game has answered, the
trial comes out or becomes the rule THE SAME DAY". The game answered on 2026-10-10 09:36 (run `20261010-092027`;
read in your copy: `docs/PT-REQUESTS.md` PT-99's and PT-104's newest notes: grep the headings):

| Trial | Switches today | The game's answer | What it becomes |
|---|---|---|---|
| the lamps a ray off water or glass runs into, found by a search tree | CMake `TGPT_LAMP_TREE`, `Speed trial` 16 | the beach's frame 192 -> 161 ms, the same picture | THE RULE |
| the two big passes' regrouping also by "feeds the radiance cache" | CMake `TGPT_REGROUP_FEED`, `Speed trial` 32768 | the tavern's reflections 11.4 -> 9.9 ms, the same picture, no regrouping added | THE RULE |
| a ray inside the water asked only as far as its path flies | CMake `TGPT_WATER_FLIGHT`, `Speed trial` 32 | no gain that can be told from the noise | OUT |
| the terrain's frame from the object's x axis | hidden `Advanced / Terrain frame` 0, 1, -1 | +1: the terrain's "another normal" 63 % -> 4 % of its pixels; -1: 54 % | +1 THE RULE |

Not yours, they stay exactly as they are: `TGPT_REGROUP_WATER` (`Speed trial` 2 and 4: not run yet), the measuring
build (`TGPT_SPEED_MEASURE` and its second page), `Speed trial` 1 and 8, the rain's wetness (`TGPT_RAIN_WETNESS`,
`Advanced / Rain wetness`: it waits for its shelter), Mega Geometry (parked), the relief trial, the lamp trial that
is compiled out, the memory work.

## The job

1. **The lamps' tree the rule**: the tree is built and used in every build; `reflected_lamps`' old loop over every
   lamp and its `#ifdef` go; CMake's `TGPT_LAMP_TREE` and the number 16's meaning as a speed trial go (in a
   measuring build 16 keeps its measuring meaning: check the report `docs/agents/perf-1.md`, "perf2.2" and
   "perf2.5", for what else read that number); the tracer's log line that said "compiled out" goes.
2. **The feeders' key the rule**: the same for `TGPT_REGROUP_FEED` and 32768 (the regrouping's key carries the
   feed bit always, in the two reordering passes).
3. **The water's short rays out**: `TGPT_WATER_FLIGHT`, `water_flight_reach`, 32 as a speed trial, their text.
4. **The terrain's frame +1 the rule**: `program_terrain` builds its frame from the object's x axis with the sign
   +1, always; the hidden `Advanced / Terrain frame`, its record and its log line of the switch go; the plugin's
   two log lines about the terrain's meshes stay if they are cheap (once a scene). And two things the worker
   before you left open (`docs/agents/terrain-1.md`, "Not done"): (a) the 37 ground meshes that bring tangents of
   their own (all (1,0,0,-1)): say what sign they need by the same rule the game's shader applies, and build it if
   it is a line; (b) OTHER material programs on meshes without texture coordinates still get the flat normal
   silently from `frame_normal`: do not change them, but make the tracer SAY it: a count in the log once a scene
   ("N instances of M materials have a normal map and a mesh without texture coordinates: drawn flat: ...names").
5. Everything that names the removed switches follows: `CMakeLists.txt`, `Settings.cs` (the Speed trial's text),
   `tests/ingame/scenarios.json` (recipes `speed2`, `regroup`, `terrain-frame`: drop them or cut them down to
   what still means something), `tests/source_checks.py` (its checks of these trials become checks of the rule:
   the old loop is gone, the tree is always laid, the terrain's frame has no switch), the test files, the docs in
   `docs/agents/` only by a line at their top ("made the rule on 2026-10-10: see rule-1.md"). `CHANGELOG.md`: one
   entry each, with the game's numbers from the table above.
6. Verify: `python tests/source_checks.py`; the plugin builds (0 errors, 0 warnings); every pass compiles
   (`tools\Build-PtShaders.ps1 -BuildDir %TEMP%\tgb\COPYNAME -Throttle 3`, configured with `-DTGPT_WITH_NRD=OFF
   -DTGPT_RAIN_WETNESS=ON`; never `-First`, never a raised limit); the native DLL links; ctest's `tgpt_lamp_tree`
   passes. No GPU test: the coordinator's run of `water-day` and `spec-terrain` is the test (say what the stages
   and the Yardstick's shares must read: the table's right-hand numbers, by default).
7. STOP there.

## Rules

- Never the game, a deploy, a commit, the main tree or the game folder.
- NO NEW BEHAVIOUR: with the job done, a default build does what today's build does with `Speed trial` 32784 and
  `Terrain frame` 1, and nothing else differs. If you find something that wants changing, write it down.
- Never read `pt_common.hlsli`, `pt_materials.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp`
  or `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so. After a scripted edit,
  check every hunk landed.
- RTXDI and NRD sources never copied or quoted.
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Never describe anything with the word the source checks ban (`test_testing_limits_disclosed`): say "prototype" or
  "on trial".
- Final message: six lines at most: what is now the rule, what is out, what the 37 meshes need, the flat-normal
  count's line, that every pass compiles and the checks pass, the report's path.
