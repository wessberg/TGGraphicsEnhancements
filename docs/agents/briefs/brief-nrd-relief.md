# Brief: NRD wipes a metal helmet's hammered relief smooth: the picture with NRD keeps what the frame with no denoiser has (PT-95, PT-19) - one agent, a trial first

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\nrdrelief`
(pristine base: `...\scratchpad\wt\base69`). Follow `AGENTS.md` ("The game is the first test, not the last") and
`docs/AGENT-WORKERS.md` there. Report: `docs/agents/nrd-relief.md`; write it early and keep it current. Read
`docs/PT-REQUESTS.md` PT-95 first (grep the heading, read that entry only), then `docs/agents/filters.md` and
`docs/agents/char-normals.md` only where they speak of what NRD is handed.

## What the game showed (the MAIN tree's `artifacts\harness\20261009-103336`, read only; scene `metal`, 16 frames a step)

`helmet-now.jpg` (the 4K frame's crop x 1540 y 440, 460 x 640): the game's helmet is dark hammered iron; the traced
frame with NO denoiser (`metal\3-look-none`) has the same hammered relief, a little brighter; the tracer's Normals
view (`4-look-normals-view`) shows the normal map's relief; with NRD (`2-look-nrd`, `9-follow-nrd`) the helmet is
smooth bright metal: the relief is gone. Raw frames are in those folders (3840 x 2160, 4 bytes a pixel, bottom-up,
`step.json` beside them). The owner, 2026-10-09: "you see all the imperfections in the helmet not appearing in
either of ours", "We're lacking all the details on the helmet from the game."

The inspection of 2026-10-08 says the same of other things (PT-19): "With NRD the lit plank and wall lose all
texture", a door strap's hammered surface gone, a cloak's weave gone.

## What is known (check it)

- The direct light does not go through NRD (our own `pt_temporal` and `pt_atrous` passes take it); NRD (RELAX) takes
  the bounced light and the reflections (`native/pathtracing/shaders/pt_nrd_prep.hlsl`, `src/nrd_denoiser.cpp`,
  `pt_compose.hlsl`).
- NRD's guide normal is the surface's plane WITHOUT the normal map (the triangle's, or a character's smooth one:
  `guide_plane`), by a measured choice: "per-pixel bumps made RELAX's history fail"; the normal map's share of the
  DIFFUSE light is taken out before and put back after (`bump_share`). For reflections on metal nothing like that
  can be right as it stands: a bump changes WHERE a reflection comes from, so smoothing reflections over a bumpy
  metal with a flat guide erases the bumps.
- The owner's rules: Denoising lists None, NRD, Ray Regeneration and Ray Reconstruction; NRD "should look relatively
  noise-free" and "make NRD look great too". Never tune a denoiser to HIDE a defect of the tracer; here the frame
  with no denoiser is right and the denoiser loses it: the denoiser's inputs and settings are the job.

## What to do

1. Say, from the code and from the kept frames, where the helmet's relief is lost: which signal (reflections,
   bounced light, or the direct light's own filters, which also run), at which stage (what NRD is handed as normal
   and roughness, its own blur radii for a polished metal, our passes around it), with numbers from the frames
   (detail in a patch of the helmet: the game, no denoiser, NRD).
2. **The trial**: the smallest change that keeps the relief with NRD without bringing the noise or the boiling back,
   behind a hidden switch of the plugin (`Advanced`, default off: the picture as before; a spare or reserved field,
   never an option bit: all 32 are used). Candidates, for you to judge: the normal map in what NRD is handed for
   the REFLECTIONS (with the measured failure of its history looked at again: was it the diffuse signal's?); the
   reflection's detail split off before NRD and put back after, as the diffuse light's is; a roughness-true lobe so
   a rough hammered metal is not treated as a mirror. Whatever you choose must keep the frame's mean light.
   Hand it over as soon as it compiles and the source checks pass (aim: within 45 minutes): the switch's name, the
   files changed, a recipe for `tests/ingame/scenarios.json` (`metal`: the game, no denoiser, NRD with the switch
   off, NRD with it on, 16 frames each; the pattern: the recipe `texture-follow`). Tell the coordinator in a short
   message, then go on with 3.
3. The same for a non-metal the inspection names (a lit plank's grain, a cloak's weave): is it the same loss or
   another (PT-19, PT-75)? One line each in the report; build nothing more without the game's answer.

## Rules

- Trial first: no long study before the hand-over.
- Never the whole GPU test or ctest as a whole ("full test suite never in subagents"): your own small check only,
  through the main tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` gate, never while the game runs
  (`artifacts\harness\RUNNING` in the main tree held by a live process: wait). At most ONE GPU test at a time.
- Never the game, a deploy, a commit, the main tree or the game folder. Build in `%TEMP%\tgb\nrdrelief`, WITH NRD if
  the copy's configure allows (the main tree's recipe: `docs/PROJECT-CONTEXT.md` "Build recipes"); if not, say so
  and compile `pt_nrd_prep.hlsl` by hand with the build's own DXC line. `tools\Build-PtShaders.ps1 -BuildDir <it>
  -Throttle 8` before `cmake --build`: it waits for room on the whole PC (other compiles, memory, a game run):
  let it wait, never work round it.
- RTXDI and NRD sources never copied or quoted into the tree or the report: read them to understand, describe in
  your own words.
- Never read `pt_common.hlsli`, `tracer.cpp`, `nrd_denoiser.cpp`, `tests/pathtracing_gpu_test.cpp` or
  `tests/source_checks.py` whole: grep, then a range. Sources are CRLF: keep them so.
- Final message: six lines at most: where the relief is lost (with its numbers), the switch (name, values), what it
  does in one line, what it costs, that it compiles and the source checks pass, the report's path.
