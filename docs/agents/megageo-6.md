# megageo-6: RTX Mega Geometry's NVIDIA layout: the forest's GPU hang, and where its frame time goes (PT-109)

Worker `megageo-6` (Claude initially; Codex takeover), copy `...\tg-agents\wt\megageo6`, base `base20mg6`. Brief:
`docs/agents/briefs/brief-megageo-6.md` (main tree). 2026-10-11, from about 19:00.
Never the game, a deploy, a commit, the main tree; no `tgpt_gpu_test` as a whole, no ctest. GPU runs: my own small
check only, the game closed and the main tree's `Run-PtHarness.ps1 -GpuCheck` at exit 0.

## State (kept current)

- 21:06: READY FOR THE COORDINATOR. Final native hash `C914B898A4153C1F0A93FE255709095AA5991A14EAD6F16CED99D6B9B08B07D5`;
  build, own stage and tested DLL all match. Final guarded focused check exits 0 (21:04:53-21:05:32), all 116 source
  checks pass. Forest/town game confirmation remains owed. No worker build, GPU test or waiting launcher remains.
- 20:45 Codex takeover: the handoff's "no code was changed" is stale. `mega_geometry.hpp/.cpp` have unfinished
  edits dated 19:09/19:10: size-classed bottom-level builds, cached arguments, and size readback. `scene.cpp` still
  calls their old signature, so this is not a buildable checkpoint. I will audit and finish those edits, not discard
  them. The existing build also configures NRD from its junction; I will switch to the main tree's prebuilt NRD
  libraries before any build, to keep the main tree read-only. No other worker is running in this copy.
- Takeover plan: audit the full NVAPI cluster contract and NVIDIA sample; confirm the suspected per-object room
  mismatch against the pristine base; finish bounds/cached-argument handling and CPU measurements; extend the small
  plant check with growth/shrink/replacement frames; build and hand off one native DLL. A single guarded background
  runner will wait for the coordinator's game and GPU work before the focused check. No questions or new agents.
- 20:54: first native checkpoint BUILT (no native warnings), own `megageo6-build/TGPathTracing.Native.dll` and
  `megageo6-native/TGPathTracing/TGPathTracing.Native.dll`. No exports, public records, FrameConstants rows, settings,
  flags or shaders added/changed. Keep the coordinator's current plugin. The focused GPU check is next; no game
  claim. Run `forest@mega-walk`, then `metal@mega-walk`, then `mega-plants` after merging/rebuilding with main's
  concurrent changes. The new `Mega Geometry CPU trial` lines report CPU time each second; existing structure
  timestamps remain the GPU figures. See findings below before interpreting the old hang as proven fixed.
- 19:05: read the brief, AGENTS.md's rulings, megageo-5's report, PT-109, the run `20261011-182915`'s native log (this
  run's session starts at its line 35300; the two earlier hangs in that file are older sessions').

## Plan

1. (a) The hang: read `mega_geometry.cpp` and the scene's `pose_plants`/`record` for trial 7 against NVAPI R615's rules
   for cluster operations (argument counts, indirect counts, scratch and destination room, barriers between the
   operations, micromap arrays). Find every way a frame can ask the GPU for more than was reserved or read memory not
   yet written. Make that condition a case in `TGPT_TEST_MEGAPLANTS_ONLY`; fix it.
2. (b) Where the frame time goes with trial 7: time the CPU side of `pose_plants` (per plant bottom level, per call),
   compare with `vk_animated_clusters`' per-frame work (one indirect call for all, arguments written on the GPU);
   bring ours to it where it differs.
3. (c) One build for `forest@mega-walk` and `metal@mega-walk`.

## Evidence from the run `20261011-182915`

- Forest, trial 7's second walk: "the last frame recorded 4003; the GPU finished the start of frame 4003, and stopped
  in stage scene (after its structures' the rest of the scene stage; the next kind's work did not end)".
- The last frames before it: 134 to 146 groups stamped from clusters a frame, 15,693 to 24,640 clusters, 860 to 1,504
  bottom levels (one a plant), the clusters' and bottom levels' room 82.7 to 124.2 MB; the count of groups and plants
  stamped changes from frame to frame (groups that wait, plain, one frame).
- The walk's `step.json` files already separate the tracer's render-thread time from its GPU time. Town means:

  | Walk | Frame ms | Tracer GPU ms | Tracer render-thread ms |
  |---|---:|---:|---:|
  | Plain 1 | 127.13 | 72.37 | 46.79 |
  | Trial 7, 1 | 140.28 | 69.10 | 68.23 |
  | Plain 2 | 126.47 | 69.02 | 47.60 |
  | Trial 7, 2 | 149.42 | 70.74 | 71.61 |

  The extra **21.44 / 24.01 ms is inside the tracer's CPU event**, not an unexplained extra GPU pass. D3D11 input
  work is 0.11-0.14 ms; the native summaries show essentially zero fence wait and ~0.2 ms flush, leaving recording.
  The plugin's capture snapshots are 11.05 / 10.92 / 17.63 / 10.58 ms (snapshots, not walk means: no plugin regression
  can be inferred). Unity CPU/GPU fields are zero/unavailable. Town GPU plants go 2.77/2.87 -> 1.48/1.48 ms;
  GPU top level 0.20 -> 0.52/0.53 ms. The town builds ~9,906 plant bottom levels, not ~1,000 as the brief summarized.
  The foreground gaps in `gpu.json` end before these steps; no competing GPU program was recorded. The walk still
  has the known one-frame-start validation failure. These numbers identify the old bottleneck, not a new speedup.

## Build and focused-check state

- First checkpoint hash: `483BCCD8BDCB6209045C448A48A7CE03CD614F396185620A45F1EDBCB728E4A7` (native build and
  own staged copy matched). Memory-accounting/dead-helper cleanup after this checkpoint requires the final hash below.
- `megageo6-build/configure-worker.cmd` pins main's read-only SDKs, NRD/ShaderMakeBlob static libraries, existing
  meshoptimizer source and disconnected FetchContent. No install/download. A previous shader run's output was absent;
  52 headers were reused from megageo5 only after matching the compiler bytes, normalized command and every listed
  dependency byte-for-byte (`megageo6-build/shader-reuse.txt`). Build-PtShaders compiled ten missing/stale headers,
  13 seconds; sources unchanged by this worker. No native warnings; the test has existing unrelated C4244 warnings.
- All 116 source checks passed. Own `TGPT_TEST_MEGAPLANTS_ONLY`, startup allowance 900 seconds, guarded by main's
  `-GpuCheck -TestsYield -TestsAtOnce 1` before start; while running, the guard allows only its own process and stops it
  if the game or competing work arrives. First attempt started 20:56:50, stopped its OWN PID 57452 at 20:57:03 when
  the coordinator's game opened. It was still creating passes. No pixels or timing result; no other process stopped.
- Attempt 2 completed every functional assertion and printed success, but the guard saw Sunshine at 4% at its end
  and rejected timing. Correction to the progress commentary: this second interruption was Sunshine, not the game.
  Attempt 3 started 21:04:53 and finished normally at 21:05:32, exit 0, with no guard rejection. Its outputs:
  `C:\Users\Frede\AppData\Local\Temp\tgb\megageo6-check3.log`, `megageo6-guard3.log`, and
  `megageo6-build\check3-native.log`. In-run GPU checks allowed only the worker's one test (the tool's generic
  "one more may start" text counts our process); a second test would reject the sample.
- Functional result: both growth cycles reached **8,887 clusters and 1,566 plant BLAS**, beyond the initial
  argument, destination, result-size and scratch capacities. No device failure, no failed cluster call, no result
  past its reserved size. The first-hit colour and direct-light pictures match exactly; two lit pixels differ
  (of 921,600, within the existing coplanar-edge tolerance). The moved-bush control changes 46,945 lit pixels.
  The post-growth first-hit check passes. Viewed the plant first-hit capture; it is the expected room and plants.
- Final small-scene medians (1,536 added plants, no denoiser, 160 measured frames per variant, two repetitions):

  | Trial | GPU ms | CPU recording ms | CPU p90 ms |
  |---|---:|---:|---:|
  | 0 plain | 6.56 | 0.667 | 1.018 |
  | 5 small-plant clusters | 6.80 | 0.652 | 1.170 |
  | 7 one BLAS/plant with maps | 6.76 | 0.781 | 1.158 |
  | 8 one BLAS/plant without maps | 9.40 | 0.788 | 1.188 |

  This is a correctness/stress fixture, not the forest or town and not a before/after CPU measurement of the old
  binary. It does not establish an in-game speedup. Stable topology reuses GPU arguments; the grow/shrink seconds
  rewrite them in 1.5-2.3% of frames. Three BLAS size classes serve the enlarged per-plant layout. Full suite and
  destructive GPU mutants were not run; AGENTS.md defers those until the game accepts the direction. The moved-bush
  control still proves the image comparison sees a changed plant.

## Source delta and interface inventory

Against `base20mg6`: `mega_geometry.hpp/.cpp`, `scene.hpp/.cpp`, `tests/pathtracing_megaplants_checks.inl`, plus this
report and the harness notes (BACKLOG, PT-REQUESTS, PROJECT-CONTEXT, CHANGELOG, ROUNDS, round-20). A read-only diff was
written to `megageo6-build\source-delta.patch`. No git operation changed anything.

- No exported function, public ABI record, FrameConstants row, option, flag bit, CMake option or shader change.
- Internal records: `Bottom.bytes`; new `Part` (group lifetime serial, stamp/bottom pointers and counts, BLAS/CLAS
  addresses and sizes); size-class metadata; pending result-size readbacks and their fence/room metadata.
- Internal state: persistent GPU argument buffer and exact part identities; cached requirements/scratch bound;
  `PlantGroup.megaRoom`; eight CPU counters for plan, pose, clusters, top, whole scene, rewrites, BLAS count and calls.
- Logs: `Mega Geometry CPU trial N` each second; `Mega Geometry size check` after each completed sampled readback;
  failures name the violated bound. Readback memory is counted in `transfer`, argument memory in `scratch`.
- Removed workaround/work: the unsafe mixed-max explicit batch; repeated argument construction/upload for unchanged
  groups; the now-unused `addresses_into` helper; per-frame allocation-size queries for the cluster log. No HDRP
  rendering workaround encountered. Plain structures remain for unsupported hardware and the existing trials/fallback.

## State when stopped

2026-10-11 21:06, normal handoff, no limit reached. One build is ready:
`C:\Users\Frede\AppData\Local\Temp\tgb\megageo6-build\TGPathTracing.Native.dll`
(same in `megageo6-native\TGPathTracing\TGPathTracing.Native.dll` and the tested `megageo6-build\megageo6-stage`).
SHA256 `C914B898A4153C1F0A93FE255709095AA5991A14EAD6F16CED99D6B9B08B07D5`.

Build and focused GPU check pass. No deployment, game start, main-tree write, installation, download, commit or push.
All worker guards/tests have exited. Do not replace main's concurrent shader/NRD work with this older whole-tree DLL
without accounting for it: merge the five source/test files against base20mg6, rebuild main, keep its current plugin.
Then `forest@mega-walk`, `metal@mega-walk`, and `mega-plants`. Read per-second CPU lines beside existing GPU structures;
the NVAPI sizing defect is corrected, but only the game can establish that the forest TDR is gone. Windows logged
three generic nvlddmkm 153 events at 18:44:08/10/18 with GPUID 100, no fault address to identify a unique allocation.

### ▶ Starter prompt (coordinator / R21)

Merge this bounded native fix and focused test. Run the forest walk first with trial 0/7 and check for the former
frame-4003 scene-stage hang; then town walking CPU/GPU cost and the standing recipe. Do not call Mega Geometry
finished or turn it on by default from this fixture. If safe but still CPU-bound, the new phase timings identify
which part to move next. Implicit BLAS allocation plus GPU-written instance addresses is still separate work; the
current explicit size-class path follows the NVAPI contract and is the smallest change that the game should judge.

## Confirmed code defects and changes (Codex takeover)

- NVAPI R615, `nvapi.h` 20995-21001, 21152-21165: an explicit destination must fit the per-object result size of
  the call's declared inputs (or the result of GET_SIZES). The old code queried each plant's own cluster count
  for its room, then put every plant into a call declaring the largest count. No GET_SIZES had been run. That
  violates the documented bound. The inherited size-class fix now builds: one call per power-of-two cluster
  count, room queried with that class, and the actual batch's reported bound checked before executing. All plants
  and triangles stay. This is a credible corruption cause, not yet proof of the forest TDR's sole cause.
- Each group's real buffer bounds, each bottom level's reserved bytes, counts, alignment, and cluster spans are
  checked before submission. Every 16 builds, result sizes are copied back after their fence and compared with the
  actual rooms. No synchronous GPU wait. The readback is diagnostic, not what makes the writes safe.
- Per-frame stamp/bottom/address vectors and uploads are gone while the same groups are posed. The builder compares
  exact lifetime serials, order, pointer/count and allocation identity (no hash collision), reuses GPU arguments,
  and queries requirements again only when those inputs change. Growth reallocates all needed buffers and rewrites
  bottom-level references to the new destination array. Retired buffers stay alive through the frame fence.
- The old logging path called `allocation_bytes` FOUR times per cluster group each frame, though it printed only
  sometimes. Each call asks the D3D12 device for allocation information. The allocation sizes are now cached once
  at group creation. This is removed work, not yet a measured game-speed claim.
- `TGPT_TEST_MEGAPLANTS_ONLY` adds two grow/shrink/replace cycles (0, 96, 1536, 192, 1536, 0 extra plants), mixing
  small tufts and a denser mesh, then checks the camera hits against the plain scene. Timing repetitions reduced
  from eight to two on the enlarged scene; it prints CPU record times separately. No new test environment switch.

## NVIDIA sample comparison and deliberate scope

Read local `vk_animated_clusters/src/renderer_raytrace_clusters.cpp` (the update and initialization paths),
its instance-update shader, and NVAPI R615's full cluster-operation declarations. Its CPU creates/uploads the CLAS
and BLAS argument lists at initialization; they are NOT regenerated on the GPU every frame. A GPU pass installs
the driver-produced BLAS addresses into TLAS instances. It uses one stamp call, one implicit-destination BLAS call,
and barriers between animation, stamps, BLAS, instance writes and TLAS. Our stamps now reuse their GPU arguments too.

The first checkpoint retains explicit destinations in size-class batches. Main's instance records are written on
the CPU and need stable addresses. Converting them to implicit destinations also requires a GPU instance-write
stage and lifetime changes for frozen/unposed groups. That is not required to make the documented explicit route
safe, and would mix a second structural change into the forest-hang test. No claim that the layout migration is
finished: implicit BLAS allocation and GPU instance writes remain a separate measured follow-up after this run.
