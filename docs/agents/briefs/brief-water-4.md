# Brief: water onto today's tree, with its waves as real geometry (PT-71 item 5) - one agent

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`: the main tree of 2026-10-07 23:40). Follow `AGENTS.md` there; read "The
owner's rulings" first ("The tracer fires its own camera rays, for every pixel", "Water is the tracer's", "Do. not.
fake. stuff.", "Remove the workarounds as we go", "test what catches defects and nothing more", "The order of judging,
and Ray Reconstruction last"). Report: `docs/agents/water-4.md`; write it early, keep it current.

## Where things stand

A GPT-6-astra worker made water a surface every ray meets, in the copy `...\scratchpad\wt\water3` (its base:
`wt\base37`, the tree of 18:00). Its report: `docs/agents/water-3.md` in that copy (read it whole; its numbers are in
`water-3-validation.txt` beside it). What it did right, checked offline with no denoiser (11 checks, each failing
under its mutant): the camera's and every other ray meets the displaced water surface, reflects or refracts by
Fresnel by chance, the water absorbs with depth, the camera can be under water (the bed through water 0.5510 against
Fresnel over n squared 0.5511; absorption over 0.4 m 0.6692 against 0.6703); the old passes (`pt_water.hlsl`,
`pt_water_under.hlsl`), their histories, events, exports and the transpiler are deleted. What makes it unusable: it
finds where a ray meets a wave by walking the height field for every ray: 20 ms for the prepare stage at 128 x 72
pixels, and its shaders took 90 s to start.

Since its base the main tree got: the whole-world merge (plants posed by the game's wind in groups, one BLAS a 16 m
cell refitted a frame: `docs/agents/world.md`, `scene.cpp` "groups", `pt_sway.hlsl`), scattering, material programs,
moved things each frame, glass and eyes as surfaces (`docs/agents/merge-4.md`: `pt_waves.hlsli` holds the wave
functions now, `pt_wet.hlsl` the wet ground's store, `pt_dielectric.hlsli` the glass's optics).

## What to do

1. **Bring water3's work onto your copy** (`python tools/agents/merge_copy.py water3 --base=base37 --root=<the folder
   above wt>` run from YOUR copy merges into your copy; first `--dry`). Conflicts: both sides stay; the wave functions
   exist in both under the same file name (it was told to use the port's names); its dielectric maths and glass's
   (`pt_dielectric.hlsli`) should be ONE implementation, not two.
2. **The waves as real geometry in the acceleration structure.** In place of the per-ray walk of the height field: a
   grid of triangles over each water surface, displaced by the game's own wave data on the GPU each frame and
   refitted, as the plants' groups are posed (the same machinery where it fits). Fine near the camera, coarser far
   away, in a way that leaves no visible step and is the SAME surface for every kind of ray (camera, mirror, bounce,
   shadow), so nothing depends on the screen; say how the grid's fineness is chosen and what error against the game's
   wave data it leaves (in millimetres, near and far). Then a ray meets water as it meets any triangle, and the
   walk and its 20 ms go. Keep the optics exactly as tested.
3. **Its open points, as far as one job goes**: the guides for the denoisers at a water pixel (depth, motion, normal,
   roughness, albedo from the water hit: NRD first, AGENTS.md "The order of judging"); foam and the waterline where
   waves meet rock and sand ("smooth intersections between waves and surfaces/cliffs/rocks/shore", the owner); the
   wet ground's store reading the new surface. Say what is left (decal foam, water masks, deformers).
4. **Cost and start-up**: the stage's time with a lake in view against today's main without water (offline, both
   builds run back to back); shader passes must not take longer to make than main's do.

Judged (AGENTS.md "test what catches defects and nothing more"): build; water3's 11 optical checks still pass on the new
geometry; your new checks (the surface a camera ray meets is the game's wave surface within your stated error; the
same surface for a shadow ray); the whole GPU test ONCE at the end with its FAIL lines written down; no mutation
checks now. In game the coordinator runs `lantern-lake@dots` (no dots, no denoiser) and `water-day@quick` (the bed, the
far shore's reflection, the waterline; no denoiser, then NRD): say what each picture, `flicker.py`'s numbers and the
log must show.

## Clean as you go

What your work makes dead is deleted in this job (the height-field walk, anything of the old water route water3 left);
your report lists every workaround you met, removed or not.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or to `wt\water3`. GPU tests only
  while the game is closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its
  process gone, and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side).
  Wait with one background script, never by polling; never give up a test for want of a slot. When you end, leave no
  waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...`; NRD's configure writes into the shared SDK folder, so
  build with `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- The coordinator's own files are not yours: `BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`. Everything you learned goes into your report.
- Eight workers run at once tonight (memory and start-up, materials, lights and flicker, the candle scene and heat
  haze, decals, speed, glass bottles and hair): keep to water's files where you can, and say in your report which
  shared files you changed (`tracer.cpp`, `scene.cpp`, `pt_common.hlsli`, `pt_primary.hlsli`).
- Keep tokens down: grep and read ranges of big files, never whole; write your report early so nothing is lost if you
  are cut off. Stop when done; ideas go in the report.
- Say if an export or struct changes (the coordinator sets the ABI number; main is 0x00012100). Option bits: none.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
