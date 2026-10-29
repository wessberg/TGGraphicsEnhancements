# Brief: finish water 4 (the last agent was cut off mid-edit by a usage limit) - one agent

Copy (continue IN it): `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\water4`
(its pristine base: `...\scratchpad\wt\base45`). A backup of the copy as it stood at 00:55 is in the main tree's
`artifacts\worker-copies\wt\water4` (read only). Follow `AGENTS.md` and `docs/AGENT-WORKERS.md` in the copy.

## Where it stands

Read `docs/agents/water-4.md` in the copy first (section 1 is the state, section 5 what is left). The job (PT-71 item
5, the owner: "Water is the tracer's ... Full Path Tracing means full path tracing"; "Real path traced caustics"):
water as surfaces of the tracer's own rays, its waves as real triangles in the acceleration structure, one dielectric
boundary for glass and water. The last agent had merged the older water work onto this base (63 conflicts, builds),
built the wave triangles and was measuring. It was cut off at about 01:17 on 2026-10-08 while writing: "Now add the
read-back path: a member in scene.cpp (where the arena is) and its use in plan_water". The files it touched last:
`native/pathtracing/src/scene_water.cpp` (01:12), `shaders/pt_common.hlsli` (01:10), `tests/water_surface_gpu.inl`
(01:14), `tests/pathtracing_gpu_test.cpp` (01:07), `shaders/pt_water_surface.hlsli` (01:06). An edit may be half done.

## What to do

1. Build the copy as it is (native with `-DTGPT_WITH_NRD=OFF` into `%TEMP%\tgb\water4b-*`, the plugin, the source
   checks). If it does not build, find the half-done edit (diff the last-touched files against the 00:55 backup) and
   either finish it or take it back out: say which and why.
2. Decide the read-back path the last agent was starting: finish it if the report says what it is for and it is
   needed for a correct picture or for the measurements; otherwise leave it out and say so.
3. Run the water checks (`tests/water_surface_gpu.inl`, the fixture named in the report), fix what they show.
4. Fill the report's open parts: section 4's numbers (the stage's cost before and after, start-up), section 5 (what
   is left: the denoisers' guides at a water pixel, the waterline, the gaps), section 6 (shared files, structs, ABI).
5. The whole GPU test ONCE at the end, FAIL lines written down. Known on the base, not yours: "clear glass casts no
   shadow", "a character's eye drawn forward...", "glass bends light by its material's own index of refraction", the
   lake's timing, and "the passes were made" / "all startup passes report completion" on a cold pass cache (run once
   more: the second run uses the cache).
   **A whole run is VOID if its log ends with failed frames or `last error 0x887A0006` ("Interop fence timeout"):**
   that is the GPU overloaded by several tests at once (it voided two workers' runs last night). Run it once more.
6. Do NOT bring the copy onto a newer tree: main has moved on (decals, memory, the speed job, glass). A separate merge
   job does that from your finished copy. So: small and local edits, and list in the report every shared file you
   changed and every struct, export and option bit (the next number free for a new ABI is 0x00012300).

## Rules

- Never start the game, never deploy, never commit, never write to the main tree or the game folder. GPU tests only
  while the game is closed and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0; wait with one
  background script, never by polling; leave no waiting shell behind when you end.
- The coordinator's files are not yours (`BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`, `docs/PT-REQUESTS.md`,
  `docs/PROJECT-CONTEXT.md`), nor the other session's (`src/TGNeuralRendering*`, `native/neural`, `native/bridge`,
  `native/framegen`, `native/upscaling`).
- Keep tokens down: grep and read ranges; never read `tests/pathtracing_gpu_test.cpp`, `pt_common.hlsli`, `scene.cpp`,
  `tracer.cpp` or `tests/source_checks.py` whole. The Bash tool eats backslashes: write files with Write/Edit.
- **Update the report after every step**: a usage limit can cut you off without warning, and the report plus your
  edits are all that survives. No mutation checks. No stand-ins: what is not traced is named as a gap.
- RTXDI and NRD sources are never copied or quoted; nothing copied from Unity/HDRP or the game's shaders.
- Final message: five lines at most, plain words, pointing at the report: what is built, what passed, what is left.
