# Brief: bring the finished water job onto today's tree (PT-71 item 5) - one agent, a merge and nothing else

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`: the main tree of 2026-10-08 04:10). Follow `AGENTS.md` there; read
`docs/AGENT-WORKERS.md` (how copies and merges work here, and "What went wrong"). Report: `docs/agents/merge-water.md`;
short, written as you go.

## The job

Two agents finished water as surfaces of the tracer's own rays, its waves as real triangles in the acceleration
structure, in the copy `...\scratchpad\wt\water4` (its base: `wt\base45`, the tree of 2026-10-07 23:44). Its report:
`docs/agents/water-4.md` in that copy (start at "Read this first", then sections 5 to 7: what is left, the shared
files, structs and ABI, the tests). Its whole GPU test was a valid run there: 1,239 ok, 4 FAIL (three known on the
base, and "every mesh with alpha-tested instances has opacity micromaps", which is the base's: another agent is on it).

Since its base the main tree (your copy) got: light and flicker, memory and start-up (recyclable arenas, meshes
retired, a pass cache on disk with progress: every pass must be in it), decals (records, a box tree, mesh decals),
the speed job (a material read once a hit, `program_whole`), glass and hair (`docs/agents/glass-hair.md`: glass has an
inside only where its submesh closes a volume, an open sheet is a thin pane, paint on see-through glass; edits in
`pt_dielectric.hlsli`), the candle job (`EffectPasses.cs`, the scattering colour), materials 2 (a skin program,
`TGPT_PROGRAM_COUNT` 10). `GeometryRecord`'s third and fourth words are `layers` and `flags` (bit 1 closed, bit 2 own
layers). ABI 0x00012300.

1. Merge it into your copy with your copy's own tool: `python tools/agents/merge_copy.py water4 --base=base45
   --root=<the folder above wt>` (first with `--dry`: 32 files changed, 14 new, 3 deleted, 27 conflicts in
   `CMakeLists.txt` (1), `pt_common.hlsli` (3), `pt_dielectric.hlsli` (5), `pt_specular.hlsl` (1), `scene.cpp` (2),
   `scene.hpp` (4), `tracer.cpp` (4), `NativeTracer.cs` (1), `pathtracing_gpu_test.cpp` (2), `source_checks.py` (4)).
   The tool writes into the tree it lives in: run YOUR COPY's, never the main tree's.
   Resolve every conflict so that BOTH sides' work stays. Where they meet, and what must hold afterwards:
   - `pt_dielectric.hlsli`: the water job made one dielectric boundary for glass and water (`pt_interface.hlsli`); the
     glass job changed the same code (closed against open, paint by alpha, a cornea's thin shell). Glass keeps all of
     the glass job's rules, through the one shared boundary. Its four checks (`tests/pathtracing_glass_checks.inl`)
     and the water fixture must both pass.
   - `scene.cpp`, `scene.hpp`, `tracer.cpp`: the water's buffers, meshes and read-backs follow the memory job's rules
     (released when the scene is cleared, counted in the memory line), its passes are in the pass cache and the
     progress count, and nothing of it is built or uploaded every frame that does not change every frame (say what
     the water does a frame, in ms and bytes).
   - The records: every size agreed between `tgpt_api.h`, the plugin's `Validate`, the shaders and the source checks.
   - A file the water job deleted that main has since changed: say which, and what of main's change had to move where.
2. The coordinator's files stay as your copy has them (`BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`,
   `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`, `AGENTS.md`, `docs/AGENT-WORKERS.md`): restore them from
   `wt\BASENAME` after the merge and list in your report what the water job had added to them.
3. One ABI number: `TGPT_ABI` 0x00012400 in `native/pathtracing/include/tgpt_api.h`,
   `src/TGPathTracing/NativeTracer.cs` and `tests/source_checks.py`.
4. The water finisher's one open choice is decided: with the Caustics row Off, light goes straight through the water
   (the tooltip, the README and the GPU check say so); keep it as the finisher left it, and keep its note that this
   is what "Off" means, not a feature.
5. Build the native targets (`TGPathTracing.Native`, `tgpt_gpu_test`) and the plugin; `python tests/source_checks.py`.
   Then the water fixture and the glass checks alone, then the whole GPU test ONCE; write down its ok count and every
   FAIL line. Known on the base, not yours to chase: "clear glass casts no shadow" and the eye and index checks if the
   glass job did not already fix them (say which still fail), the lake's timing, the micromaps check, the candle
   job's translated-skin check. Fix only what the merge broke. No mutation checks.
   Set `TGPT_TEST_STARTUP_SECONDS=900` for the test: a new build's passes take over 200 s to make from cold.
   A whole run with failed frames or `last error 0x887A0006` is void (GPU overloaded): run it again.
6. Report: each conflict and how it was resolved; the record sizes; the tests' results; the pass sizes and the
   start-up time with the water passes in (the passes have grown: say what the water adds); what the water job leaves
   undone (from its report: the guides at a water pixel, the waterline, the shadow at clear water's edge that is "NOT
   JUDGED", the test lake's 240 MB); the in-game scenes and what their pictures and log must show.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree, to `wt\water4` or to the game
  folder. GPU tests only while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck`
  exits 0. Wait with one background script, never by polling; never give up the test for want of a slot; leave no
  waiting shell behind. Build into `%TEMP%\tgb\COPYNAME-...` with `-DTGPT_WITH_NRD=OFF`.
- Compile the shader passes side by side before `cmake --build`:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\Build-PtShaders.ps1 -BuildDir <your build folder> -Throttle 8`
  (or a Ninja tree, as the water agents used).
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files; in
  `CMakeLists.txt` add only what the water's passes need.
- Keep tokens down: grep and read ranges of big files, never whole. Update the report after every step: a usage
  limit can cut you off without warning.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
