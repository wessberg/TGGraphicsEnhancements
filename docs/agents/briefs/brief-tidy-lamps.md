# Brief: take out what the game said no to: the lamp trials, the roll-off, the dead loop, the glints' leftovers (PT-85, PT-40) - one worker

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\tidy`
(pristine base: `...\scratchpad\wt\base62`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/tidy-lamps.md`; write it early and keep it current. Background, only as far as you need it:
`docs/agents/flame-specks.md` (the four trial switches), `docs/agents/flame-near.md` (the two after), `docs/PT-REQUESTS.md`
PT-85's last notes (search "THE OWNER, 06:50").

## What was decided in the game (2026-10-09)

The tavern's specks and its burnt-white candles were one thing: what a flame puts on surfaces within 10 cm of it.
The cure the owner chose in the game is **the near size**: within 10 cm of a lamp its light stops growing
(`pt_common.hlsli` `light_falloff`, the plugin's hidden `Advanced / Lamp near size`, default 10, bits 0-7 of the
number that travels in `TGPT_Frame.jitter`'s w and `FrameConstants.wetGrid`'s w). **That stays, exactly as it is.**
Everything else that was built to find or cure the same thing is dead and goes, with its settings, checks, recipes
and docs (the owner, 2026-10-07: "remove all the 'hacks' and workarounds as we go ... So we have a clean base").

## What to take out (each with its settings, shader code, source checks, recipes and doc lines)

1. **Trial switches B, C, D**: `Advanced / Lamp trial reflected size`, `Lamp trial highlight size`, `Lamp trial
   shadow size` (bits 8-13): `lamp_trial_mode`, `lamp_trial_radius`, `lamp_disc_dir`, `LAMP_TRIAL_*`, their uses in
   `pt_common.hlsli` and `pt_rtxdi.hlsli` (`highlightDir` and the two blocks), HDRP's radius in a light's flags' top
   12 bits (`TGPT_LIGHT_HDRP_RADIUS_SHIFT`, `hdrpRadiusAsked` in `SceneLights.cs`). `LightRules.HdrpRadius` itself
   stays if `Lights sized as their emitters` off still uses it (check).
2. **`Advanced / Lamp trial lit surroundings`** (bits 14-21): `lamp_around`, `lamp_around_light`, `near_split`,
   `near_first`, `g_nearArmed` and their call sites in `pt_common.hlsli`, `pt_gi.hlsl`, `pt_specular.hlsl`.
3. **The roll-off**: `Advanced / Highlight shoulder knee` and `Highlight shoulder most`: `HdrpPasses.HighlightShoulder`,
   `TGPT_Frame.frameReserved`'s use for it, `k.radiance[2]`, `constants.look[1..2]` in `tracer.cpp`,
   `highlight_rolled` / `highlight_unrolled` in `pt_common.hlsli`, `pt_compose.hlsl` and `d3d11.hlsl`'s copy. The
   fields themselves go back to what they were before (reserved, 0), sizes and ABI unchanged.
4. **Then name what is left for what it is**: the number in `jitter`'s w is only the near size in centimetres now.
   Rename `lamp_trial()` and `SceneLights.LampTrial` accordingly, rewrite the comments in `tgpt_api.h`, `tracer.cpp`,
   `pt_common.hlsli`, `Settings.cs`, `SceneLights.cs`, `HdrpPasses.cs` so none says "on trial" of it, and replace
   the two source checks (`test_path_tracing_small_lamps_on_trial_default_off`,
   `test_path_tracing_flame_surroundings_on_trial_default_off`) with ONE that pins the near size: the setting and
   its default 10, the packing, `light_falloff`'s line, `reflected_lamps`' line, and that the removed names are gone.
5. **The filters' dead loop code**: the direction feedback loop was cut on 2026-10-08 (`k.denoise1[1]=kInvalid` in
   `tracer.cpp`); what only served it (`nrdDirectionKept` and what feeds it) goes. Grep first; if something else
   reads it, leave it and say so.
6. **The glints' leftovers**: `glints` (lamp candidates) was merged and taken out again on 2026-10-08; lines about it
   remain in `docs/TESTING.md` and perhaps elsewhere (`git grep -n -i glints -- docs tests src native`): remove
   what describes code that is not there. `docs/agents/glints.md` stays (a report of a job).
7. **Recipes** in `tests/ingame/scenarios.json`: remove `lamp-trial`, `lamp-near`, `watch-near`, `near-sweep` (they
   set removed keys); keep `lamp-size` and the others.
8. **The offline GPU test's checks that fail by design** since the take-backs of 2026-10-08: the coordinator's list
   of FAIL lines of the whole test is at the end of this brief. For each: if it tests code that was taken out, the
   check goes with it; if it tests something that should hold, say so in the report and leave it failing (do not
   bend a check to pass).
9. **One small offline check for the near size**, in a fixture of its own with its own `..._ONLY` mode (the
   pattern: `tests/pathtracing_edges_checks.inl`, `TGPT_TEST_EDGES_ONLY`): a lamp over a wall, the frame's jitter w
   at 0 and at 10: with 10 a point 3 cm from the lamp takes I / sqrt(d^4 + n^4) (about a ninth of the 0 case's
   I / d^2... work the numbers out), one 20 cm away 3 % less than at 0, one a metre away the same within a
   thousandth; a lamp of radius 15 cm is unchanged by it. Mutation-check it (break `light_falloff`, watch it fail).
10. **Docs**: a `CHANGELOG.md` entry (why the near size exists, the sweep's numbers from PT-85, what was removed);
    `docs/PROJECT-CONTEXT.md` section 9: the learning in five lines at most (the game's lamp strengths were set for
    HDRP's I / (d^2 + r^2) with r up to a metre; in its flame a lamp needs a near size; what did NOT work and is a
    dead end: sampling the lit surroundings on purpose (-16 %), a roll-off of the picture (cures the burn, not the
    specks), the lamp's reflected, highlight and shadow sizes); `docs/TESTING.md`. Keep PROJECT-CONTEXT within its
    budget: condense in the same edit.

## Rules

- **The picture must not change.** With every removed switch at its default the shaders did nothing: after your
  removal the tavern must look the same. You cannot run the game; the coordinator will. So remove, do not rewrite:
  no "while I am here" improvements, no reordering of live code.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own new check and the
  `..._ONLY` modes of fixtures whose code you touched (minutes each), through the main tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs.
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\tidy` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the
  plugin too (0 errors, 0 warnings). `python tests/source_checks.py` and `python tests/harness_analyze_test.py` pass.
- RTXDI and NRD sources never copied or quoted.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so (no `sed -i` from Git Bash).
- After a scripted edit, check every hunk landed.
- Final message: eight lines at most: what went (by item), what stayed and why, the new check's name and its
  mutation result, that everything compiles and the source checks pass, the report's path.

## The coordinator's list: FAIL lines of the whole GPU test in the main tree

The run of 2026-10-09 08:19 to 08:36 (native 609143540829: the main tree before the characters' normals were
built in; the lamps' near size is 0 in the offline test): **1,516 ok, 47 FAIL**. The output:
`<scratch>\gpu-test-main-081950.txt` (the coordinator copies it beside this brief's copy if you ask). By family:

- 32: "NRD's mean is the undenoised mean's, a channel: ..." and "the built-in denoiser's mean is the undenoised
  mean's, a channel: ..." (the sky through a slot, the sky through a grille, candles at night; with and without
  reflections; the metal box, the brown floor, the floor's dim end, the walls). These came with the filters job;
  its kept-light filter was taken back on 2026-10-08. PT-75 (NRD changes what the picture converges to) is a
  real open defect: the checks ask for something that should hold. Say which of them test code that is gone.
- 9: the mirror family: "with NRD a perfect mirror keeps the fine detail of the checker it shows, within 10
  percent", the same with the built-in denoiser, "Ray Reconstruction is handed a perfect mirror's picture of the
  checker with its fine detail", "a perfect mirror's picture of the checker holds the undenoised picture's light
  with every denoiser", the two of the colored cells, "a moving camera's undenoised frame shows the still
  camera's picture of the mirror (the check's own measure)", "a mirror's picture keeps its fine detail while the
  camera moves, with NRD and the built-in denoiser", "a glossy floor's reflection holds the undenoised picture's
  light with NRD and the built-in denoiser".
- 4: glass (NOT yours: the agent `glass3` has them): "glass bends light by its material's own index of
  refraction (another index lays the light elsewhere)", "light through green glass reaches path vertices green
  (bounced light)", "grey glass leaves the bounced light's color", "grey glass lets light through as bright as its
  color (its shadow about 0.64 of a solid one's)".
- 1: "coloured or white bulk slab retains its diffuse colour under even white light within 3.5 percent".
- 1: "NRD flickers no more than half again as much as the built-in denoiser".
For the two single ones: find what they test and since when they can fail (the report), change nothing.
