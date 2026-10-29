# Brief: fog-froxel - the traced fog as NVIDIA's RTX Remix builds it: a froxel radiance cache (PT-118, PT-37, PT-112)

Copy: COPYNAME (pristine base: BASENAME). Follow `AGENTS.md` in your copy; read first its rulings "Follow NVIDIA's and
AMD's guides to the letter" (2026-10-11), "No limits that change the picture", "No shortcuts", "The game is the first
test" and "Code that is switched off still costs frame time". Report: `docs/agents/fog-froxel.md` IN YOUR COPY; the plan
within 20 minutes, kept current; "State when stopped" at once if a limit shows.

## Why

The owner, 2026-10-11 ~15:25, with NRD now set up as NVIDIA documents it ("NRD is a million times better now"): "now of
course we see boiling/artifacts from our path traced fog ... which is still super noisy". Asked how to fix it, the owner
chose (~15:35) "NVIDIA's Remix way": build the fog as NVIDIA's own path tracer for games, RTX Remix, does. Read
`docs/PT-REQUESTS.md` entries PT-118 (grep "### PT-118": the owner's words, NVIDIA's documented settings) and PT-37, and
`docs/agents/fog-2.md` (how today's traced fog came to be).

Today (`native/pathtracing/shaders/pt_fog.hlsl`, `pt_medium.hlsli`): one point a pixel a frame on the camera ray (the height
fog's free flight or a fog box's), its light from the sun, the sky, one lamp by RIS, an equiangular lamp sample along the
ray, the mist's multiple scattering from the radiance cache's cells; a 10 % blend with the last frame, a 5 x 5 filter;
composed over the picture after the denoisers (pt_compose). NRD is not made for media (its README), so nothing else
cleans it.

## NVIDIA's way (the yardstick; read it in full before designing)

- RTX Remix's documented volumetrics settings (docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.4.0-0/docs/runtimeinterface/
  renderingtab/remix-runtimeinterface-rendering-volumetrics.html): a froxel grid of the render size / 16 across and 48
  depth slices, spaced by an exponent of 2, to a max distance; per cell an initial RIS over the lights (8 candidates) with
  a visibility ray, temporal reservoir reuse (up to 200 samples, a confidence penalty where reprojection is clamped),
  1 to 6 reservoir samples a cell by its stability, accumulation over up to 254 frames, a Gaussian filter (sigma 1.2) whose
  kernel shrinks from radius 4 to 2 as the history grows (power 2 over 1 to 64 frames), a firefly clamp.
- RTX Remix's runtime source (github.com/NVIDIAGameWorks/dxvk-remix: its volumetrics passes; check its license before
  reading it closely and cite files and lines; copy no code unless the license allows it and the report says so).
- NVIDIA's ReSTIR as we already use it (`pt_rtxdi.hlsli`, the light lists `lists_candidate`, `fog_light`, `load_light`).

## What to build, in this order

(a) The SMALLEST version that shows the direction in the game, behind a hidden switch (a new value of an existing hidden
    setting or a frame flag bit; say which: the option bits are all taken, TGPT_Frame.flags bits 19 and up are free): the
    froxel grid (camera-frustum aligned, as Remix), each cell's in-scattered light (the sun, the sky, the lamps and glowing
    surfaces by RIS with a visibility ray, the mist's multiple scattering from the radiance cache as today), accumulated
    over frames with reprojection (a cell's world point into last frame's grid), and the camera rays' fog read from it:
    the medium's transmittance exact as today (height fog closed form, the boxes), the light integrated through the
    cells along the ray. Hand the coordinator a build for ONE run (tavern-room, recipe traced-fog or a new one beside
    it: the switch off and on, no denoiser and NRD) within your first hour, or say why not.
(b) Then, as the game answers: temporal reservoir reuse per cell, the stability-driven filter, the firefly clamp, as
    Remix documents them; the grid's reach (no distance past which the fog stops: where the grid ends, say what takes
    over and why it is not a limit on the picture); mirrors and refraction (secondary rays keep the traced medium,
    pt_medium traced_fog_on_way, unless Remix does otherwise: say).
(c) The clean-up when the owner says it looks right: the old per-pixel route out the same day (AGENTS.md).

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests: only your own small
  check (a `..._ONLY` mode of `tgpt_gpu_test` or a fixture of your own), only while the game is closed and the MAIN
  tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0; NEVER the whole `tgpt_gpu_test` or ctest as a whole.
- Build: `tools\Build-PtShaders.ps1 -BuildDir <your build>` (never `-First`) then the native build into
  `%TEMP%\tgb\fog-froxel-...`. Keep big-pass code out of `pt_gi`, `pt_specular`, `pt_prepare` and the walk (a pass of its
  own for the grid). Say every export, record, FrameConstants row, option or flag bit you add.
- RTXDI and NRD sources are never copied or quoted. Grep and read ranges of the big files, never whole.
- Final message: five lines at most, pointing at the report.
