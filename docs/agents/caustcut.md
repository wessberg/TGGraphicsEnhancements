# caustcut — PT-114

Started 2026-10-11 19:00 CEST, private copy `caustcut`, base `base20pc`.
No game start, deployment, main-tree write, download, installation or git mutation is permitted.

## Plan (first checkpoint)

1. Read the rules, PT-114, the caustics producer and every consumer. Done.
2. Split the stage using the existing measuring switches (32770 grid only, 32772 shadow rays only), before claiming a cost. The first main-tree `-GpuCheck` returned 1: the game is running. Thus the reported 3.9 ms is existing game evidence; its breakdown is not measured by this worker. Do not delay the requested trial waiting for a permitted GPU slot.
3. Build the light-side alternative allowed by the brief: a tile shares a candidate **solid blocker triangle**. Each corner independently proves that its actual shadow segment crosses the interior of that triangle. Only that proved-zero shadow query is skipped. A candidate hit alone is never proof for another corner. This preserves both branches, neighbour landing rays, split triangles and off-screen readers. It avoids the difficult receiver-completeness problem below. Near edges, nearly parallel rays, uncertain materials or unrecognized geometry use the original query.
4. Hidden Advanced boolean `Caustics receivers bound (trial)`, default false; frame bit 23, native `TGPT_FRAME_CAUSTICS_RECEIVERS_BOUND`. The CPU selects a separate caustics PSO; it does not reuse packed shader frame flags (those already carry other trials). No ABI layout, export, record or FrameConstants row is added. The requested name denotes bounding the paths that can reach any receiver; this first trial proves their light blocked before it reaches the water.
5. Recipe `caustics-bound`: off/on without a denoiser, 16 frames and perfSeconds for both. Coordinator runs `tavern-room` first for speed, then `water-torch` and `water-day` for unchanged light. Native and plugin checkpoint within 90 minutes, or a written blocker. Run source checks before handoff.
6. Only a small caustics fixture mode may run, and only with the game closed and the main GPU guard returning 0. Compare off/on light and caustic counters. No full suite; no mutation run before in-game acceptance.

## Readers and exactness

`pt_compose.hlsl:caustics_at` and `pt_nrd_prep.hlsl` read caustics at camera surfaces. `pt_water_path.hlsli` reads `caustic_light`/`caustic_lit` at solid underwater vertices (unless the independent water-light trial supplies them). Ordinary dry reflected and bounced vertices do not read this light. Water paths themselves can be entered by reflected and bounced paths, so they must not be treated as camera pixels alone.

The current warped-mesh producer also rejects triangles outside its enlarged camera frustum. A correct mirror/off-screen solution must remove that producer dependence and supply caustic lighting at every secondary vertex, including off-screen geometry. A receiver bound would have to include those vertices or a conservative world-space domain, never only the camera's current box. No such feature is built here.

Found while tracing the readers: GI and specular dispatch **before** the caustics rebuild in `tracer.cpp`, so their water paths currently consult the previous build of the landed mesh. A camera-only current-frame bound would therefore also risk the next frame's water reads. This ordering gap will be recorded separately; fixing it is outside PT-114. The blocker proof preserves all emitted light and does not depend on the reader set or frame.

## State when stopped / limits

20:08 CEST: finished. First trial build was ready at 20:03 (63 minutes after starting). All 59 shaders, native DLL, managed net472 plugin and the small test executable built. All 116 source checks pass. Build, stage and actually loaded runtime native hashes match. The guarded small GPU run exited 0 after 48 seconds, with no recorded contention or GPU errors. All six comparisons passed with byte-identical triangle/light records and zero undenoised image difference. No offline speed gain: the enclosed room was 2.20 ms off, 2.23 ms on. Keep default off. The game's 3.9 ms breakdown, tavern speed and water pictures are still for the coordinator; this worker may not launch or deploy. No worker process or queued run remains.

## Implementation checkpoint

- Two added compiled shaders, `pt_caustics_bound` for SM 6.6 and 6.9. They include `pt_caustics.hlsl` with `PT_CAUSTICS_BOUND`. The ordinary shaders compile out every added operation, shared array and counter.
- Each tile/light proposes one triangle with a raw hardware query. Only a material without `MATERIAL_ALPHA_TEST` is reusable: `alpha_opaque` returns true immediately for exactly that class. Wind, foliage and water masks are excluded from the proposal. Proposal misses and uncertain material hits merely lose the optimization.
- Each corner retains its original sampled emitter point, shadow origin, direction and emitter-exclusion endpoint. Its segment must meet the candidate plane strictly inside that range and inside all three inward triangle edges. Near-parallel rays, ill-conditioned triangles and floating-point edge/endpoint uncertainty fall back to `transmittance`.
- A proved-dark corner still supplies its outgoing reflected/refracted directions and landing rays when a neighbour needs them. Light selection, waves, triangle emission, split-light mode, storage capacity and receiver filtering are unchanged.
- Counter words 2/3 were unused: the trial records attempted/proved-blocked shadow queries, aggregated to two global atomics per tile. Native status text reports their per-frame means. No buffer size change.
- `tools/Build-PtShaders.ps1` accepts optional `-CoordinationTemp`: private compiler TEMP/TMP no longer hide the coordinator's existing GAME-WANTED/BUILD-FIRST markers. Defaults preserve normal behaviour. This build uses the standard shared limits, never `-First`.
- NRD links main's existing matching static libraries read-only through `TGPT_NRD_PREBUILT_LIBRARY`/`TGPT_NRD_PREBUILT_DEPS`; no SDK source configure or copy. All native/compiler/dotnet temporary writes use `caustcut-build`. Managed restore uses only copied existing reference-package archives and is restricted to net472 (the first restore also tried net6 references, failed offline, and was corrected).

## Correctness argument and validation scope

A reused triangle belongs to the same current acceleration structure as the original shadow query. Its material satisfies the unconditional-solid branch of `alpha_opaque`, and its mask is a subset of the original caustic shadow mask. Each corner checks its own original shadow segment, including its own emitter exclusion distance. If that segment crosses the triangle, the original visibility query returns blocked and `transmittance` returns zero, regardless of anything else on the segment. Substituting zero therefore preserves that corner's carried light. Other corners use their complete original query. Dark corners retain their direction and the neighbour logic, so all subsequent triangle geometry and light are unchanged. The proof is independent of the receiver, view, light distance, caustic split setting and later reflection support.

Arithmetic close to a triangle's edges/endpoints is deliberately not trusted. The inward roundoff allowance uses the transformed vertex coordinate magnitudes, ray origin/distance and triangle conditioning; near-parallel or degenerate cases fall back. These restrictions limit which work can be saved, never which light is traced. The one proposal per tile similarly limits acceleration only; it is not a light/sample budget.

The default shader, after preprocessing out `PT_CAUSTICS_BOUND` and ignoring comments/whitespace, was compared against pristine `base20pc`: executable source identical. The old PSO therefore carries none of the trial's extra code or shared storage.

`TGPT_TEST_CAUSTICS_BOUND_ONLY=1` selects only the small fixture after the ordinary eight-frame renderer warm-up. It skips shared-texture checks and returns before all other check families. It reuses the existing fixture's sine waves and plane/cube meshes: partial opaque shadow, alpha-cutout fallback (both viewed from below the waves at the receiving floor), and a closed room whose floor is 470 m over the sea; split caustics both off and on. Six off/on pairs compare 16 matched frames each: triangle counts, maximum lookup tolerance, all 80 existing integer sun/wave sums, and undenoised RGB. Each variant also reads back the complete emitted triangle set for frame 7032: all 12 corner words and all 11 written light words. The fixture sorts these 92-byte records (append order is arbitrary) and compares them byte for byte. A missing or incomplete snapshot fails; no rounding or tolerance is used for geometry/light records. The trial must actually save queries in the opaque and elevated-room cases. Each variant also spends three seconds collecting its own stage timing window, so the previous variant cannot contaminate its timestamp average.

Test instrumentation adds private native state (three delayed frame indices and initially empty snapshot slots), the opt-in `TGPT_LOG_CAUSTICS_COUNTERS` log, and `TGPT_CAUSTICS_SNAPSHOT` (a test-owned file path). Only the requested fixture frame 7032 allocates readback buffers, copies the mesh and writes sorted records; normal rendering allocates none. The copied mesh includes unused capacity, but the file includes every actually written triangle and no padding. There are no new exports, public records, options or FrameConstants rows. Frame bit 23 and the hidden boolean are the only control additions; counters 2/3 and two PSO blobs are the only shader additions. The separate `caustcut-testbuild` builds only `tgpt_gpu_test` while the native tree's shader runner works, avoiding concurrent NMake/shader work in one build tree.

Workarounds met: the existing camera-frustum emission filter (PT-94), missing ordinary secondary caustic readers, and previous-frame water reads (PT-130). None is removed by this performance trial. No new distance, layer, light-count or image-resolution limit is introduced.

## Coordinator handoff (trial ready at 20:03 CEST)

First run is the game: `tavern-room@caustics-bound -Measure`, then the same recipe at `water-torch` and `water-day`. The recipe repeats off after on, with no denoiser, 16 frames and eight seconds of performance sampling per variant. Check total caustics time and the new proved-blocked count together; compare exposure/gain as well as image brightness. A high count without a faster stage means this implementation's extra query/synchronization/arithmetic costs more than the rays it replaces; keep it off and report that. No speed win is claimed from its construction.

No source from RTXDI/NRD was copied. The private runtime stage contains the existing bridge, Agility DLL and existing compiled shader-cache objects copied read-only from main, plus this worker's native DLL when built. Cache hits are validated by the existing cache keys. The test guard checks the main `-GpuCheck`, then monitors owner games and other GPU processes; it can stop only its own test. It never queues a game or launches one.

## Merge scope and local reproduction

Merge this delta against `base20pc`; do not replace main's shared files wholesale. No files under the ignored SDK roots belong in the merge.

- Runtime: `native/pathtracing/shaders/pt_caustics.hlsl`, new `pt_caustics_bound.hlsl`, `pt_shaders.hpp.in`, `native/pathtracing/src/tracer.cpp`, `native/pathtracing/include/tgpt_api.h`, `CMakeLists.txt`.
- Plugin: `src/TGPathTracing/NativeTracer.cs`, `Settings.cs`, `HdrpPasses.cs`.
- Checks/recipe: `tests/pathtracing_gpu_test.cpp`, new `tests/pathtracing_caustics_bound_checks.inl`, `tests/source_checks.py` (allow this exact solid-only proposal mask), `tests/ingame/scenarios.json`.
- Build helper: `tools/Build-PtShaders.ps1` (`CoordinationTemp` only).
- Shared notes: `BACKLOG.md`, `CHANGELOG.md`, `ROUNDS.md`, `docs/PROJECT-CONTEXT.md`, `docs/PT-REQUESTS.md`, `docs/rounds/round-20.md`; this report. PT-130 was free in main when recorded; renumber if another worker has since used it.

Build commands and logs live under `C:/Users/Frede/AppData/Local/Temp/tgb/caustcut-build`: `configure.cmd`, `build-native.cmd`, `configure.log`, `shaders.log`, `shaders-retry.log`, `source-checks.log`. The small test has its own `caustcut-testbuild/build.cmd`, `build.log`, and `run-guarded.ps1`; the latter is the only worker launch path. `caustcut-stage` is its input stage, `caustcut-testbuild/caustcut-runtime` its disposable runtime. All these paths are inside the worker's authorized folders.

Managed DLL: `src/TGPathTracing/bin/Release/net472/TGPathTracing.dll`, SHA-256 `77950F2AF7022194088FE2901D0CD4D2647BAC12E9E1E8B7EB866A6786664F99`.

Native DLL READY: `C:/Users/Frede/AppData/Local/Temp/tgb/caustcut-build/TGPathTracing.Native.dll`, SHA-256 `377AE52623FD52ED3167C6778C78E7654A08D5F5B5FF4114E76430FD292565D0`. The copy in `caustcut-stage/TGPathTracing/TGPathTracing.Native.dll` has the same hash. No bridge rebuild or deployment. Pair it with the managed DLL above (or merge the flag and hidden setting into main's newer plugin before building).

## Offline result (20:04 CEST)

The main GPU guard returned 0; FoA and Starfield were closed. `run-guarded.ps1` monitored other GPU use throughout and reported `Test exit 0, 48 seconds, invalid=False`. Only `TGPT_TEST_CAUSTICS_BOUND_ONLY` ran. Logs: `caustcut-testbuild/guard.log`, `gpu.log`, `gpu.err.log`; native records and twelve complete snapshots: `caustcut-testbuild/caustcut-runtime/TGPathTracing`.

| Fixture | Split | Caustics off/on (ms) | Complete snapshot triangles, off = on | Shadow queries proved blocked over 16 frames |
|---|---:|---:|---:|---:|
| Partial opaque shadow | Off | 0.54 / 0.55 | 39,942 | 327,398 / 2,917,264 |
| Partial opaque shadow | On | 0.56 / 0.56 | 40,476 | 327,398 / 2,917,264 |
| Alpha-cutout fallback | Off | 0.56 / 0.57 | 49,913 | 0 / 2,917,264 |
| Alpha-cutout fallback | On | 0.56 / 0.57 | 49,916 | 0 / 2,917,264 |
| Enclosed room, floor 470 m up | Off | 2.20 / 2.23 | 0 | 26,971,714 / 30,958,720 |
| Enclosed room, floor 470 m up | On | 2.20 / 2.23 | 0 | 26,971,714 / 30,958,720 |

Every pair has 16/16 matching counter records, every complete triangle/light byte equal after sorting, and max/mean RGB difference exactly zero. Exposure and traced gain are fixed at 1 in this fixture. The room avoids 87.1% of its original shadow queries (about 1.69 million a frame), yet the whole stage is not faster in this simple scene. This is a negative performance result, not a proven optimization. Its small acceleration structure is unlike the game's world; only the tavern test can decide whether avoiding that world's shadow queries repays the proposal, barriers and intersection arithmetic. These timings do not separately measure the wave/grid work or establish the cause of the game's 3.9 ms.

The brief specifically requested this off/on offline fixture. No mutation check was run: AGENTS.md's short-loop rule defers mutants until the owner accepts the picture. No full GPU test, ctest, RR run, game launch or deployment. Do not mark PT-114 fixed. First coordinator action remains the one tavern comparison, followed by the two water scenes if speed is worth pursuing. If the tavern does not improve, reject this direction before building more tests or making it default.

The GPU check used the RTX 5080, with DXR 1.2 available and Shader Execution Reordering active. Both shader models compile; another GPU/runtime has not been tested. Final source check at 20:07: 116/116 pass.

## The game's answer (the coordinator, 2026-10-11 21:33)

Run `20261011-213004` (the main tree's `artifacts/harness`), `tavern-room@caustics-bound -Measure`, this worker's build deployed from its copy (native 377AE52623FD, plugin 77950F2AF702), nobody else on the card: the caustics' stage 4.20 / 4.30 / 4.05 ms (off, on, off), a frame 49.03 / 49.45 / 48.98 ms, the same picture. No gain: the shadow rays are not where the stage's time goes. The trial is NOT merged; PT-114 says what comes next, PT-130 is this report's other find. The `-CoordinationTemp` option of `Build-PtShaders.ps1` was not taken either.
