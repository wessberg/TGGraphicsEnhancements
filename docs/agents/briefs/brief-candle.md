# Brief: the candle scene: rectangles of noise at the flames, and wax that comes out brown (PT-73, PT-06) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`). Follow `AGENTS.md` there; read "The owner's rulings" first ("Judge the
picture without a denoiser first" with "The order of judging, and Ray Reconstruction last", "Do. not. fake. stuff.",
"Everything on screen is part of the main traced picture", "test what catches defects and nothing more"). Report:
`docs/agents/candle.md`; write it early, keep it current.

## Why

The owner, 2026-10-07 23:10: "we need to make this look pretty good before testing with ray reconstruction. The NRD
denoiser should look relatively noise-free, and the denoiser-less presentation should reveal the problem areas, like
spots with a lot of shadow flickering, the weird rectangle gas spots over candle lights and stuff". And earlier of the
same thing: "It's a rectangle full of noise above candlelights". The scene that shows it was run at 23:14 with no
denoiser and with NRD: `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\20261007-231354\candle`
(a group of candles before a stone wall, the world frozen). Look at these first:
`flicker-2-traced-none-map.jpg` (the whole screen's change between consecutive frames: white is 16 levels),
`flicker-2-traced-none.jpg` and `flicker-3-traced-nrd.jpg` (the six spots that change most: the game's picture, four
consecutive frames, the change; made by `tools/harness/flicker.py`), `game-vs-traced.jpg`.

### 1. The rectangles

In the change map there is one sharp-edged bright RECTANGLE around each candle flame, several times the flame's
size. Inside a rectangle the picture changes 6 to 8 levels a frame with no denoiser (2 to 3 with NRD); a pixel
outside it, on the same wall in the same light, changes under 1. The flames themselves are steady. So something whose
outline is a screen-aligned or camera-facing quad makes the traced picture behind it noisy. The game draws each
flame as a VFX Graph quad (`Hidden/VFX/VFX_CandleFlame_*`, output "Shader Graph Quad - Unlit"; fire has "Distortion"
and "Glow" outputs too: `docs/agents/effects-inventory.md`, `docs/agents/vfx-capture.md` say what these shaders read),
transparent, far bigger than the visible flame. The tracer's own camera rays do not see those quads; HDRP still draws
them over the traced picture, and the tracer still takes HDRP's depth and motion vectors of the frame shown to place
its light (`docs/agents/hair-6.md` "What still comes from HDRP").
Find which of the tracer's inputs or passes is different inside a rectangle (HDRP's depth, its motion vectors, a
transparent or distortion pass sampling the picture, the composite's match by distance, the temporal reuse or the
denoisers' history being thrown away there, the exposure meter) by reading the code and with an offline test that puts
a transparent quad that writes what the game's flame quad writes in front of a lit wall. Then fix it at the cause: a
transparent thing the game draws in front must not make the traced picture behind it noisier. Say what remains for
the end state (the flame traced by the tracer and not drawn by the game: PT-71 item 7, another worker's).

### 2. The wax is brown, skin is red

`game-vs-traced.jpg`: the game's candles are cream-white wax; traced they are dark brown, like wooden posts. In another
run a woman's skin is orange-red and dark where the game's is pale
(`...\artifacts\harness\20261007-224654\hair-moving\crop-head-frames.jpg`: the game, then traced). Both are materials light
walks inside since today (`native/pathtracing/shaders/pt_walk.hlsl`, `TGPT_MATERIAL_SUBSURFACE`,
`TGPT_Material.subsurface`; the port's report `docs/agents/port-2.md`, the first job's `docs/agents/sss.md`). A walk that
loses energy or takes the surface colour for the wrong quantity gives exactly this: darker and more saturated.
Check, with numbers: how the material's colour becomes the medium's single-scattering albedo (the surface colour is the
colour after MANY scatterings: it must be inverted, not used as it is), the mean free path's units and scale against
the game's diffusion profile, whether colour is applied both at the surface and inside, whether paths that are cut
short are weighted (no energy lost: a white slab under even light must come out white, a cream one its own cream), and
what the plugin sends for wax and for skin (the log's `Subsurface material "..."` lines in the runs' `LogOutput.log`).
Fix what is wrong. The check to add: a slab of the wax's colour and of a skin's colour under even white light comes
out its own colour within a few percent, with no denoiser.

### 3. Boiling with NRD

The owner, 23:20: "That also means we need to catch boiling with NRD and prevent that too." Boiling is what a denoiser
leaves of an unsteady input: blotches whose brightness wanders slowly. `python tools/harness/flicker.py <step> --game
<game step> --boil` measures it (how far the mean of each 16-pixel block wanders over the step's 16 frames) and cuts
out the worst spots. This scene's NRD step: blocks wander 0.096 levels on average (the game's own picture 0.037),
0.69 % of the blocks more than 1 level; the worst spots are leaves at the picture's top left (0.75) and the wall
around the flames (0.5 to 0.6): `boil-3-traced-nrd.jpg` and its map in the run's folder. For each of those: is the
input NRD gets unsteady there (the rectangles of item 1; a light sampled by too few rays; history thrown away), or is
NRD fed wrongly (motion vectors, depth, normals and roughness at that pixel; its settings)? Fix the cause in the
tracer's input first; never hide it by blurring more.

Not yours: the lantern's brightness and the two-frame flicker (`wt/light`), materials' programs (`wt/mats2`), memory
and start-up (`wt/memory`), capturing the flames' particles (`wt/vfx`).

Judged (AGENTS.md "test what catches defects and nothing more"): build; your own new checks; the whole GPU test ONCE at
the end with its FAIL lines written down; no mutation checks now. In game the coordinator runs `candle@quick` again
(no denoiser, then NRD): say what `flicker.py`'s map and numbers (with and without `--boil`) should become, and what
colour the wax should be.

## Clean as you go

What your work makes dead is deleted in this job; your report lists every workaround you met, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its process gone, and
  the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side). Wait with one
  background script, never by polling; never give up a test for want of a slot. Build into `%TEMP%\tgb\COPYNAME-...`;
  NRD's configure writes into the shared SDK folder: for the offline test build with `-DTGPT_WITH_NRD=OFF`; what you
  change for NRD is tested by the coordinator's build.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Keep tokens down: grep and read ranges of big files, never whole. Stop when done; ideas go in the report.
- Say if an export or struct changes (the coordinator sets the ABI number; main is 0x00012100). Option bits: none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
