# Brief: sky-2 - the tracer's own sky: atmosphere, clouds and the sky's pixels in D3D12, HDRP's sky passes off (PT-112, the sky) - one agent, one job

Copy: `C:\Users\Frede\AppData\Local\Temp\tg-agents\wt\sky2` (pristine base: `...\wt\base20sky`). Follow `AGENTS.md` there;
read "The owner's rulings" first (no shortcuts or stand-ins; no limits that change the picture; everything on screen is the
tracer's; textures are the game's own, never copies). Report: `docs/agents/sky-2.md` IN YOUR COPY; write the plan into it
within 20 minutes and keep it current ("State when stopped" at once if a limit shows). You are the ONE subagent the owner
allows at a time (2026-10-11).

## Why

The owner, 2026-10-11, of HDRP work that runs in sequence with the trace: "Basically we want to move over anything that
could have run sequentially or block the GPU before, so we know there's no HDRP bottlenecks and any GPU/CPU waits will
come from our own rendering"; of the sky: "definitely bring that over too". The design is done: `docs/agents/sky-1.md`
(read its summary and sections 2, 3 and 4 whole; the rest by grep). Today HDRP renders the sky's cube map every frame,
runs the sky graph on every screen pixel (`RenderSky`) and the clouds (`RenderClouds`), and the tracer resamples HDRP's
cube map in D3D11 every 30 frames (`tracer.cpp update_sky`), which can make the frame wait.

## What to do (sky-1's order of work, steps 1 and 3; each behind a hidden switch first)

1. The measuring switch (sky-1 step 1): a hidden Advanced setting in `Settings.cs` (a key without `= \n \t \ " ' [ ]`)
   that, for the gameplay camera while tracing, sets the sky's update mode to OnDemand through the plugin's own override
   volume (only `updateMode`; never `Add<T>(true)`: a source check forbids it), skips `HDRenderPipeline.RenderSky` and
   `RenderClouds` by Harmony prefixes (`GamePasses.cs` `BeforeOpaqueFog` is the pattern), and draws every pixel whose
   camera ray misses from the tracer's current sky map (pt_compose: today it keeps HDRP's colour there). Plus a log line
   that counts HDRP's `RenderSkyToCubemap` calls per 10 s. This is what the coordinator's first game run measures.
2. The tracer's atmosphere (sky-1 step 3), written anew from the published model HDRP implements (Hillaire 2020,
   Bruneton; HDRP 17's documented parameters and its parameterisation as sky-1 section 3 describes them): READ HDRP's
   sources, COPY NO CODE TEXT (the owner's call on Unity's licence is pending; the water was done this way, `docs/SOURCES.md`).
   The plugin hands the values over each frame (one new record, sky-1 "What the plugin hands over"): the
   PhysicallyBasedSky and CloudLayer values of the gameplay camera's blended volume stack, the two celestial lights
   (`interactsWithSky`: today Sun and Moon), the clouds' wind offset (the plugin adds it itself), HDRP's pre-exposure.
   The tracer's passes: the atmosphere tables when the atmosphere's values change; the cloud bake when HDRP would; the
   lat-long sky map (1024 x 512 RGBA16F with mips) EVERY frame, replacing `st.sky` and `st.fogSky` for every
   `sky_radiance` and the fog's sky light; camera rays that miss drawn by the tracer per pixel (atmosphere, clouds, the
   sun's and moon's discs). The game's star layer (its own shader graph `TG/Skybox/PhysicallyBasedSky_Wyrdness`: stars,
   nebula, night cube map, Wyrdness) is sky-1 step 4: NOT this job; at night keep HDRP's `RenderSky` for the space layer
   behind the switch's "day only" state and say so in the report, or, if you reach it, translate it the way
   `tools/research/lift_decal_graphs.py` translated the decals.
3. Not yours: the haze on far objects (sky-1 step 6; the coordinator does it inside the traced fog), the questions Q1-Q3
   (the owner's), HDRI skies and cube-map sharing (step 5), the fog (`pt_medium.hlsli`, `pt_fog.hlsl`, `FogBoxes.cs`:
   the coordinator edits them now), particles.

## How it is judged

In the game, by the coordinator: scene `vista` by day and `night`, recipe with steps today's sky / the switch / the
tracer's sky, 16 frames, no denoiser first: the sky's pixels, the light on the land and the frame time. Your own check: a
small offline fixture or `..._ONLY` mode that renders the sky map for two sun heights and writes PNGs (minutes), compared
by eye and by numbers with HDRP's cube map where you can get one (the harness's captures under
`artifacts/harness/*/vista*/` and `night*`). NEVER the whole `tgpt_gpu_test` or ctest as a whole (the owner: "full test
suite never in subagents"). No mutation checks.

## Clean as you go

When the tracer's sky serves every reader, delete what only HDRP's copy needed (sky-1 section 3, the PT-60 list: `update_sky`
and its resample and wait, the fog-sky tiles, `TGPT_SetSky`, `HdrpPasses.SkyTexture`, `Session.cs`'s sky pointer, the
composite's HDRP-sky mixes, the `overSky` test). If it cannot all go yet, list in the report what stays and why.

## Working rules
- Never start the game, never deploy, never commit, never write to the main tree. GPU tests only while the game is
  closed, `<main tree>\artifacts\harness\RUNNING` is absent or its process gone, and the MAIN tree's
  `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0. Wait with one background script, never by polling. Build into
  `%TEMP%\tgb\sky2-...` (configure it like the main tree: PROJECT-CONTEXT "Build recipes"); run
  `tools\Build-PtShaders.ps1 -BuildDir <your tree>` before `cmake --build` (never `-First`).
- The coordinator's files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`, `CHANGELOG.md`, and the fog files above. Everything you learned goes into your report.
- Keep tokens down: grep and read ranges of the big files (`pt_common.hlsli`, `tracer.cpp`, `scene.cpp`, `HdrpPasses.cs`,
  `tests/source_checks.py`), never whole. Stop when steps 1 and 2 are built and your check passes; ideas go in the report.
- Numbers: a new FrameConstants row is yours to add at the END of the struct (the coordinator's last is `fogBoxes`; say
  its size in the report, and update the two source checks that pin the size and the row count); a new export and
  record in `tgpt_api.h` (say their names and sizes). Option bits: none without saying which in the report.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
