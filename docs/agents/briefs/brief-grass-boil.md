# Brief: grass lit by a fire boils with NRD: the direct light on grass jumps from frame to frame (PT-93) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\grassboil`
(pristine base: `...\scratchpad\wt\base64`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/grass-boil.md`; write it early and keep it current. Read
`docs/PT-REQUESTS.md` PT-93 first (grep the heading, read that entry only).

## What the game showed (the MAIN tree's `artifacts\harness`, read only; scene `campfire`: rainy daylight, a fire, grass around it; the world frozen; 16 frames a step)

Speck spots a frame (a pixel at least 8 levels and 1.4 times over its own median of the 16 frames; 24-pixel cells):

| run, recipe | step | spots a frame |
|---|---|---|
| `20261009-074726`, `grass-why` | the game's own picture | 1.0 |
| | NRD, the camera's sub-pixel shift on (as shipped) | 459.5 |
| | no denoiser, shift on | 96.4 |
| | no denoiser, shift off | 308.1 |
| | NRD, shift off | 562.6 |
| `20261009-081146`, `grass-views` | Albedo view, no denoiser, shift off | 8.6 |
| | Direct view, NRD | 1,180.9 |
| | Indirect view, NRD | 587.9 |
| | Specular view, NRD | 62.2 |

The patch of grass by the fire (the 4K frame's box x 3060 y 1532, 400 x 400, averaged over 4 x 4 pixels; the mean
absolute difference between neighbouring frames, in levels), and beside it a post and stones with no grass (box x
1000 y 1760): Albedo view 0.06 (post 0.05): THE GRASS DOES NOT MOVE in the frozen world; Direct with NRD 6.81 (post
0.15); Indirect with NRD 2.11 (post 0.12); Specular with NRD 0.08 (post 0.05). The picture with NRD 1.43, with no
denoiser 0.39.

So: it is the DIRECT light on grass (then the bounced), not the grass's shape, not the sub-pixel shift, not the
reflections; and NRD's picture jumps three to four times as much as the frame with no denoiser at all. 82 % of the
picture's spots are that one patch; solid things beside it are still. Sheet: `20261009-071155\campfire-grass.jpg`.
The inspection of 2026-10-08 said the same of `night`: "With NRD the grass still flickers".

## The owner's rulings that bind this job

- "The speckles is the most important thing for us to fix right now."
- "We want grass shadows, but not those horrible looking ones": grass casts shadows, soft and partial; never none,
  never hard black.
- No limits that change the picture, no fakes: a change may cost noise or speed, never change what the picture
  converges to. A defect seen with no denoiser is the tracer's: never tune a denoiser to hide it. (If a setting or a
  guide we hand NRD is WRONG for grass, that is a finding: say it with the evidence.)

## What to find out, with numbers where you can

1. What makes the direct light on a grass pixel differ from frame to frame in a still world. Suspects: how a shadow
   ray passes other blades (a coin flipped for each blade it meets, where the answer could be the product of what
   each blade lets through: the same mean, no coin); the fire as a light (its lamp and its size, its flames'
   emissive triangles, how many candidates a pixel draws); ReSTIR's reuse on blades thinner than a ray's pixel
   (rays are traced at half the output's resolution: a neighbour or last frame's pixel is another blade or the
   ground behind).
2. Why NRD does not hold it: what our passes hand NRD for a blade's pixel (view depth, normal, roughness, motion,
   hit distance) and whether any of it flips between frames on a still blade (a two-sided blade's normal turned to
   the viewer or the light, a cut-out's edge decided by a random number, a depth that is the blade's in one frame
   and the ground's in the next); how long a history such a pixel can get.

## What to do

- **The trial**: the smallest change that takes the largest cause away without changing what the picture converges
  to, behind a hidden switch of the plugin (`Advanced`, default off: the picture as before; a plugin setting packed
  where a free bit or reserved field is; ask the coordinator for an option bit if you need one: 1073741824 and
  4194304 are taken). Hand it over as soon as it compiles and the source checks pass (aim: within 45 minutes): the
  switch's name and values, the files changed, a recipe for `tests/ingame/scenarios.json` in the style of
  `grass-views` (`campfire`: the picture with NRD and the Direct view with NRD, each with the switch off and on, 16
  frames). Tell the coordinator in a short message, then go on.
- If two causes of about the same weight show, two switches in one build are welcome: one run shows both.
- After the game has said yes: one small offline check of your own (thin cut-out blades by a light) in its own
  `..._ONLY` mode, mutation-checked.

## Rules

- Trial first: no offline fixture and no long study before the hand-over.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small check only,
  through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs
  (`artifacts\harness\RUNNING` in the main tree held by a live process: wait).
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\grassboil`. NRD is
  part of this job: build WITH it if the copy's configure allows (the main tree's recipe: `docs/PROJECT-CONTEXT.md`
  "Build recipes"); if not, say so and compile `pt_nrd_prep.hlsl` by hand with the build's own DXC line.
  `tools\Build-PtShaders.ps1 -BuildDir <it> -Throttle 8` before `cmake --build`. Build the plugin too.
- RTXDI and NRD sources never copied or quoted into the tree or the report. Read them to understand; describe in
  your own words.
- Never read `pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `nrd_denoiser.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so.
- Final message: six lines at most: the switch or switches (names, values), what each does in one line, what you
  found for questions 1 and 2, the files changed, that it compiles and the source checks pass, the report's path.
