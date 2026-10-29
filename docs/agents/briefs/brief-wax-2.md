# Brief: candle wax comes out too white, and the wax at the candles' foot is still brown (PT-40) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` there. Report:
`docs/agents/wax-2.md`; write it early and after every step.

## The owner, 2026-10-08 08:55, looking at the candle scene of the first build where wax scatters light

"Good work on the candle lights, they look white now. Although, they're inside that red cloth above them, so would
they really be SO white under that darkened, reddish overcast cloth? Also, the base of the candle lights, you know,
the wax grown into the rock underneath them, still looked brown".

## The evidence (the MAIN tree, read only: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261008-084558\candle`)

`crop-candles.jpg` (the game, no denoiser, NRD). `tools/harness/mean_colour.py` on `1-game` and `3-traced-nrd` with
`--region tallcandle=0.495,0.56,0.51,0.72 --region base=0.36,0.93,0.46,0.99 --region wall=0.38,0.32,0.50,0.40`:

| screen level, R G B | the game | traced, NRD | the same as shares of red, in linear light |
|---|---|---|---|
| the tall candle's wax | 208, 183, 152 | 196, 164, 154 | game 1 : 0.75 : 0.50, traced 1 : 0.68 : 0.59 |
| the wall behind the candles | 145, 118, 95 | 85, 50, 30 | game 1 : 0.63 : 0.40, traced 1 : 0.31 : 0.10 |
| the wax at the candles' foot | 118, 93, 69 | 94, 67, 48 | |

So the traced wall is lit strongly orange (candle flames, and what the red canopy over them lets through and throws
back), and the traced wax beside it is nearly neutral white, a little bluer than the game's cream. Cream wax (about
0.9, 0.85, 0.7) under the wall's light would be about 1 : 0.29 : 0.08. With no denoiser the wax is as white (and
grainy: one walk a pixel), so it is not NRD (PT-75 is another agent's). The scene is outdoors by day, under a red
cloth canopy, at a wall; the automatic gain is x1.3.

The job that made wax scatter: `docs/agents/who-walks.md` (in your copy; its section 6 predicted "its colour is still
the candle light's on cream wax (orange 0.70, 0.25, 0.05)"). The candle job before it: `docs/agents/candle.md` (the
scattering colour conversion; its checks hold a slab's colour under even WHITE light only).

## What to find and fix

1. **Why the wax is white where the wall is orange.** One of these, or something else; decide by numbers, not by
   reading alone:
   - the wax really takes light the wall does not (daylight from the open side or through the cloth, carried through
     the wax by scattering to the side the camera sees): then say how much of the wax's light is the sky's and how
     much the flames', and it is right;
   - a walk's exit is lit by light that cannot reach it (the sky or the radiance cache taken without the shadow or
     the cloth in the way; a light's colour dropped; the cloth's own tint not applied to what passes it);
   - the conversion from the wax's colour to what a collision keeps is right under white light and wrong under
     coloured light, or the surface's colour is applied in a way that whitens;
   - the camera pixel's walk and what the denoiser's guides say of it disagree.
   Offline first (`tests/pathtracing_walk_checks.inl`, `pathtracing_candle_checks.inl`): a closed wax cylinder beside an
   opaque cylinder of the wax's own colour and a wall, lit by an orange lamp alone (the two must agree in hue: the
   wax's is the lamp's colour times what a deep slab of its colour sends back); the same under a red sheet with a
   white sky beyond it; the same with the sky open on one side. Then fix what is wrong at its cause.
2. **The wax at the candles' foot.** The tracer's log of that run says of it: "Subsurface material 202 (mask 1.00),
   drawn from both sides, on mesh 1326 ... 0 of its 1147 triangles are in pieces that close a volume: a sheet, no walk
   (lit as a plain surface)". It is wax run over rock: a shell with no underside, because the rock is its underside.
   Lit as a plain surface it is brown; the candles above it now scatter. What is right: light walks in it as in the
   candles, and a flight that leaves through the open side meets what the shell lies on (the rock), which takes or
   throws back the light by its own material; where nothing lies under it (a real open sheet in the air: a leaf, a
   flap) it stays the thin sheet it is today. Design it so that no light passes where none can and none is made, say
   how an open shell on a surface is told from a sheet in the air (what a flight meets when it leaves, not a name or
   a guess), build it, and check it offline: a wax shell on a stone block against the same wax as a closed lump on it.
3. Tell the coordinator what the candle scene should show after 1 and 2 (the wax's hue against the wall's, the foot
   against the candles), and what stays different from the game and why (the game lights this scene with white fill
   lights the tracer leaves out by the owner's ruling: say so with a number if you can read it from the run's audit,
   `candle/1-game/audit-a.txt`).

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or the game folder. GPU tests only
  while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0; wait with one
  background script, never by polling; leave no waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...` with
  `-DTGPT_WITH_NRD=OFF`; `tools\Build-PtShaders.ps1 -BuildDir <folder> -Throttle 8` before `cmake --build`. Set
  `TGPT_TEST_STARTUP_SECONDS=900` for every GPU run.
- Two other agents work beside you: the speed regressions (`scene.cpp`, `tracer.cpp`: the scene stage, micromaps) and
  NRD's colour (`pt_nrd_prep.hlsl`, `pt_compose.hlsl`). Yours: the walk (`pt_walk.hlsl`, `subsurface_*` in
  `pt_common.hlsli`), `mark_closed` and the pieces in `scene.cpp` (small, local edits there).
- Not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`;
  `src/TGNeuralRendering*`, `native/neural`, `native/bridge`, `native/framegen`, `native/upscaling`.
- Keep tokens down: grep and read ranges; never read `pt_common.hlsli`, `scene.cpp`, `tracer.cpp`,
  `tests/pathtracing_gpu_test.cpp` or `tests/source_checks.py` whole. The Bash tool eats backslashes: write files with
  Write/Edit.
- The owner's rules: everything path traced, each material's own values, no limits or thresholds that change the
  picture, no stand-ins, nothing made to look like the game's picture by a tint or a dial. A struct field, export or
  option bit: say so with sizes (the next free ABI number is 0x00012600).
- No mutation checks. The whole GPU test ONCE at the end, FAIL lines written down (known on the base: the candle
  job's "skin 110 m from scene origin" case, the glass index check, the tinted-glass checks; a run with failed
  frames or `last error 0x887A0006` is void: run it again). Start it as soon as the code is final.
- RTXDI and NRD sources are never copied or quoted; nothing copied from Unity/HDRP or the game's shaders.
- Final message: five lines at most, plain words, pointing at the report: what is built, what passed, what is left.
