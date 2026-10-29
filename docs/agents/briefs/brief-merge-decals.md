# Brief: bring the finished decals job onto today's tree (PT-63 part 5, PT-71 item 2) - one worker, a merge and nothing else

Copy: `C:\Users\Frede\AppData\Local\Temp\claude\C--Users-Frede-Developer-TGFOA-TGFrameGeneration\3bcc2cf1-dcc7-4e9c-ab8a-902e08fd49b8\scratchpad\wt\COPYNAME`
(pristine base: `...\scratchpad\wt\BASENAME`: the main tree of 2026-10-08 00:12). Follow `AGENTS.md` there; read
`docs/AGENT-WORKERS.md` (how copies and merges work here) and "test what catches defects and nothing more" in
`AGENTS.md`. Report: `docs/agents/merge-decals.md`; short, written as you go.

## The job

A worker finished decals seen by every ray in the copy `...\scratchpad\wt\decals` (its base: `wt\base40`, the tree of
2026-10-07 18:53). Its report: `docs/agents/decals.md` in that copy (read it whole: the design, the ABI changes, the
coverage gaps, the in-game scenes). It passed the whole GPU test there. Since its base the main tree (your copy) got a
large merge (`docs/agents/merge-4.md`: scattering by a walk, material programs, moved things each frame, glass and
eyes as surfaces; `TGPT_Material` 204 B, `TGPT_Moving`, FrameConstants 6512 B, ABI 0x00012100) and a VFX observer.

1. Merge it into your copy with your copy's own tool: `python tools/agents/merge_copy.py decals --base=base40
   --root=<the folder above wt>` (first with `--dry`). Resolve every conflict so that BOTH sides' work stays. Where they
   meet: `hit_material` in `pt_common.hlsli` (decals add ONE call, `apply_decals`, after the ordinary material is made:
   it must come after the material programs and the wet ground too; say where you put it and why), the instance and
   skinned-instance records (decals made `TGPT_Instance` 68 B and `TGPT_SkinnedInstance` 100 B for receiver layers; the
   merge-4 tree has `TGPT_Moving` and plant groups: every record that describes an instance must carry the layer),
   FrameConstants, `SceneCapture.cs`, `NativeTracer.cs`, the GPU test's export list, `tests/source_checks.py`.
2. The coordinator's files stay as your copy has them (`BACKLOG.md`, `ROUNDS.md`, `docs/rounds/`,
   `docs/PT-REQUESTS.md`, `docs/PROJECT-CONTEXT.md`): restore them from `wt\BASENAME` after the merge and list in your
   report what the decals job had added to them.
3. One ABI number: `TGPT_ABI` 0x00012200 in `native/pathtracing/include/tgpt_api.h`,
   `src/TGPathTracing/NativeTracer.cs` and `tests/source_checks.py` (one line of reason in its comment block). Every
   pinned size agreed between the header, the plugin's `Validate`, the shaders and the source checks.
4. Build the native targets (`TGPathTracing.Native`, `tgpt_gpu_test`) and the plugin; `python tests/source_checks.py`.
   Then the whole GPU test ONCE; write down its ok count and every FAIL line. Known failing checks on the base, not
   yours to chase: "clear glass casts no shadow", "a character's eye drawn forward is shaded by the tracer", "glass
   bends light by its material's own index of refraction", the lake's timing check. Fix only what the merge broke.
   No mutation checks, no reruns.
5. Report: each conflict and how it was resolved; the record sizes; the test's result; what the decals job leaves
   undone (from its report); the ONE in-game scene and what its picture and log must show.

## Working rules

- Never start the game, never deploy, never commit, never write to the main tree or to `wt\decals`. GPU tests only
  while the game is closed, `C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\artifacts\harness\RUNNING` is absent or its
  process gone, and the MAIN tree's `tools\harness\Run-PtHarness.ps1 -GpuCheck` exits 0 (4 tests may run side by side).
  Wait with one background script, never by polling; never give up the test for want of a slot. Build into
  `%TEMP%\tgb\COPYNAME-...`; NRD's configure writes into the shared SDK folder, so build with `-DTGPT_WITH_NRD=OFF`.
- Another session of the owner's builds a DLSS 5 mod in the main tree (`src/TGNeuralRendering*`, `native/neural`,
  `native/bridge`, `native/framegen`, `native/upscaling`, most of `CMakeLists.txt`): do not touch its files.
- Keep tokens down: grep and read ranges of big files, never whole. Speed matters: the coordinator installs this tree
  in the game as soon as you hand it over.
- RTXDI and NRD sources are never copied or quoted. Final message: five lines at most, pointing at the report.
