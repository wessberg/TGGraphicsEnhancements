# Brief: candle wax: bluish white where the game's is cream, and the wax at the candles' foot still brown (PT-40) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\wax3`
(pristine base: `...\scratchpad\wt\base66`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/wax-3.md`; write it early and keep it current.

## The owner (twice)

- 2026-10-08 08:55: "Good work on the candle lights, they look white now. Although, they're inside that red cloth
  above them, so would they really be SO white under that darkened, reddish overcast cloth? Also, the base of the
  candle lights, you know, the wax grown into the rock underneath them, still looked brown".
- 2026-10-09 08:40: "also the wax base of the [candles] on the pillar with candles under the red blanket is still
  brown while the candles themselves are white and scatters light".

## What the game shows (the MAIN tree's `artifacts\harness\20261009-071155`, read only; scene `candle`, NRD)

`candle-corner-game-traced.jpg`: in the game's picture the candles AND the melted wax at their foot are one cream;
traced, the candles are bluish white and the foot is brown and tan, the look of what lies under it. Raw frames:
`candle\1-game`, `candle\3-size-10` (3840 x 2160, 4 bytes a pixel, stored bottom-up, `step.json` beside them).

## What the job before found, and left (read it first: `...\scratchpad\wt\wax2\docs\agents\wax-2.md`, its "In short" and sections 2, 4, 5; its code is in that copy, base53, NEVER built whole or run)

- The candles' whiteness is real light as the tracer is given it: "The game's wax material names the SNOW's
  scattering profile: light flies 3 to 7 cm in it before it scatters, and a candle is 8 cm thick. So this wax is
  nearly clear, like thin ice." Daylight on a candle's far side comes out on the near side, hardly tinted.
- The foot is an open shell over rock ("0 of its 1147 triangles are in pieces that close a volume: a sheet, no
  walk"), lit as a plain surface. Its part 2 (light walks between the shell and what it lies on) is written there,
  unbuilt, with the warning that with wax this clear the foot would come out DARKER and stone-coloured.

## What to do, in this order

1. **The units, first and fast.** A candle that is nearly clear is not what the game shows and not what wax is. Read
   in HDRP's public source (the diffusion profile's settings and how its shaders use them; learn, copy nothing) what
   a profile's scattering distance and its world scale MEAN and in which unit (metres, millimetres, a blur radius on
   screen), and in the tracer how they become the walk's free path (`docs/agents/who-walks.md`,
   `docs/agents/candle.md`, `subsurface_*` in `pt_common.hlsli`, the plugin's profile capture). Say, for the
   candles' profile, what free path in millimetres the game's values stand for and what the tracer uses. If the
   tracer's is too long by a factor (ten, a hundred, a missed world scale), that is the cause of both complaints,
   and of every scattering material's look (skin shares the walk: faces up close are too dark and orange, PT-73).
2. **The trial**: the correct conversion behind a hidden switch of the plugin (`Advanced`, default off: the picture
   as before; a spare or reserved field, never an option bit: all 32 are used), so ONE run shows the candle corner
   and a face with it off and on. Hand it over as soon as it compiles and the source checks pass (aim: within 45
   minutes): the switch's name, the files changed, a recipe for `tests/ingame/scenarios.json` (`candle` and
   `npc-candle`, NRD, 16 frames, the switch off then on; the pattern: the recipe `lamp-size`). Tell the
   coordinator in a short message, then go on.
   If the units are RIGHT, say so with the numbers, and say what else makes the game's wax cream where ours is
   clear (the game draws the material's own colour and only blurs light across it: then what is the truthful
   traced reading of such a material, with no tint or dial that copies the game's picture?).
3. **The foot.** With the walk's free path right, decide again what the open shell over rock needs (the job
   before's part 2: a flight that leaves through the open side meets what the shell lies on; a real open sheet in
   the air stays a thin sheet). Build the smallest correct form, behind the same or a second switch, with one small
   offline check of your own (`..._ONLY` mode): a wax shell on a stone block against the same wax as a closed lump.
4. Note, do not build: the red canopy over the corner (PT-49), the corner's darkness (PT-39: the owner ruled dark is
   right there).

## Rules

- Trial first: no long study before the hand-over of step 2.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents": the job before's brief asked
  for one; that is no longer allowed): your own small checks only (`TGPT_TEST_..._ONLY` modes: the walk's, the
  candle's, your own), through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the
  game runs (`artifacts\harness\RUNNING` in the main tree held by a live process: wait).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\wax3` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the
  plugin too.
- The owner's rules: everything path traced, each material's own values, no limits that change the picture, no
  stand-ins, nothing made to look like the game's picture by a tint or a dial.
- RTXDI and NRD sources never copied or quoted. Unity's, HDRP's and the game's shaders: learn, copy nothing.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so.
- Other workers are in `pt_common.hlsli` too (glass, grass): change what you must, move nothing else.
- Final message: six lines at most: the units' answer with its numbers, the switch or switches (names, values), what
  each does in one line, the foot's state, that it compiles and the source checks pass, the report's path.
