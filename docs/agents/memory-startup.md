# Memory and shader start-up (PT-73 b, PT-74)

Worker copy: `wt/memory`, base43. Native and managed builds and focused checks pass. The final full GPU suite completed: 1,387 checks passed, five failed. No game run, deploy, or main-tree writes.

Final-binary warm starts took 2.412 and 3.840 seconds, both with 47 disk-cache hits. Scene clear returns the fixture's mesh/BLAS/record/scratch storage. Refused textures pause PT with a top-screen explanation. This is not a claim that the remaining atlas migration or in-game memory defect is finished.

## Decisions and evidence

- Read BACKLOG, the path-tracing context and DEAD ENDS, newest round and the PT round, and inspection-20261007-224654.
- Geometry and acceleration structures used append-only arenas. Lifetime now follows scene unloads and actual mesh use, without changing LOD, range, coverage, or shading.
- Shader creation already started at device-ready on background threads. Added per-pass timing, persistent driver-specific pipeline blobs and D3D11 bytecode, and visible progress through the existing bridge panel.
- A texture refusal must become a visible failure with texture identity/size in the log. No new texture copies or picture-changing limits.

## Validation

Native and managed builds pass. Final focused memory and warm-cache runs exit 0; production TextureShare fixture passes all eight assertions; source checks pass 94/94. The full GPU suite returned 1: 1,387 passing checks, five failing checks, 25,372 traced frames and zero frame failures. Every FAIL line is below. No mutation checks at this stage.

## Workarounds and handoff

Removed and remaining workarounds are listed below, alongside exact ABI changes. The coordinator assigns the ABI number.

## Measurements

- First process with empty application cache: 186.765 s, 51 passes. Per-pass driver compile/link + pipeline creation: specularDxr12 186.760 s, specularSer 113.156 s, giSer 77.610 s, giDxr12 35.452 s, waterUnderDxr12 32.337 s, prepareDxr12 15.770 s, walkDxr12 12.129 s, waterDxr12 10.895 s, diSpatialDxr12 10.526 s. These overlap, so do not sum them. D3D11 compilation total 1.2 s. DXIL was compiled by DXC at build time; D3D12 does not expose driver compile/link separately.
- Second process: 2.352 s, 49 disk-cache hits / 51 passes; under-three-second assertion passed. Two state objects use the driver's automatic cache: D3D12 exposes no GetCachedBlob for them. Compute pipelines use explicit cached PSO blobs; D3D11 uses persistent compiled bytecode. Cache keys include shader/root content plus vendor/device/subsystem/revision/driver. Tests use memory-cache under the worker build root.
- Subsequent code removes unused compute GI/reflections when both SER state objects succeed; identical compute fallbacks are built during loading only if SER creation fails. Ready passes remain retained across frame-resource recreation; no full-pass recompilation fallback on the render thread. Previously unseen skinning layouts can still compile their small D3D11 variant on first use.
- Confirmed faults: no managed scene-unload reset; append-only 256 MB arenas; clear missed plant record buffers, plant/cloth scratch and light-grid buffers; mesh status counted its base BLAS twice. Clear also discarded new material-program rows after intake had read them.
- Changes now built: 16 MB recyclable arenas; mesh retirement after scans stop using it and GPU fence passes; unload reset; missed buffers released; static BLAS compaction with size readback; shared scratch for sequential BLAS builds. Static meshes have only one position buffer; posed meshes require their source positions plus posed positions (no picture-preserving removal of that source).
- Existing top-screen Notices.Show panel displays compilation count until ready. Texture refusal logs texture name/ID/dimensions/format/requested bytes and pauses presentation with a persistent top-screen explanation. Maintenance event 7 keeps scene retirement running while tracing is off/paused. This fail-closed policy is a proposed safe default for owner review: no silently degraded PT picture.

Focused checks are complete. Final limitations and the in-game checks still needed are below.

## Final implementation details / merge boundaries

New native exports: `TGPT_ReadDiagnostics` and `TGPT_ReleaseMesh`. New `TGPT_Diagnostics` structure is 176 bytes; existing TGPT_Status remains 216 bytes. Render event 7 is maintenance (no rendering or GPU submission). No option bits. **Coordinator must assign the ABI bump**; this copy deliberately keeps 0x00012100. `TGPT_SharedTexture.bytes` now also returns the requested allocation estimate when sharing is refused.

The managed plugin uses the existing bridge `Notices.Show`/`Hide` top-screen panel; bridge files are unchanged. It refreshes compilation progress every 250 ms only while the row is On, and hides the panel on completion/Off. Texture refusals retain On but pause traced presentation, restore the game's picture, keep scans/retries and render-thread retirement running, and show the reason. This is the safe proposed behavior for owner review; reducing texture quality is never chosen.

The performance overlay also shows the actual shader count, replacing its stale half-minute estimate. Cache files default to `ShaderCache` beside the native DLL. `TGPT_SHADER_CACHE` overrides that path for tests. Rejected compute blobs rebuild the exact same shader and replace the entry; writes replace files atomically.

Incremental mesh retirement covers SceneCapture's mesh registrations once two completed scans no longer request them. Native retirement additionally checks current static, moving and skinned instance lists and the GPU fence. Other producers' CPU/skinned registrations are released by the scene-unload reset; their separate managed caches do not yet issue individual ReleaseMesh calls. Scene events coalesce until the next plugin tick. This resets the whole traced scene on an additive scene unload as well (correct contents, possible recapture cost; measure actual streaming behavior in game).

### Workarounds removed

- Append-only 256 MB arenas; now 16 MB chunks with coalesced free ranges and fence-delayed reuse, empty chunks returned to the driver.
- Periodic 2.5 GB scene restart while travelling; lifetime now follows unloads and actual mesh use, without a distance or geometry cap.
- Missing releases for light-grid buffers, plant group/member/range/wind/noise buffers and plant/cloth scratch.
- Static BLAS never compacted; now post-build compact-size queries, asynchronous readback and COMPACT copies. The original allocations retire after the copying frame. In the focused fixture, four static meshes shrink from 41,472 to 16,128 bytes (25,344 bytes saved).
- Per-mesh simultaneous scratch regions; static, skinned, cloth and plant builds reuse one region with ordering barriers. Static build scratch is dropped when not building. Dynamic scratch shrinks after the required size falls.
- Counting base BLAS bytes both as geometry and acceleration in the old status.
- Native texture death waiting for a future traced frame even while Off; retirement now uses the last frame that could read it.
- Eager water-caustic triangle storage and strand atlas even in a scene containing neither; these are allocated on first actual use and released on scene clear. The fixture saves 255,524,864 bytes (243.7 MiB).
- New material-program rows being read and then discarded by scene clear in the same intake.
- First traced frame recompiling shaders after warm-up or frame-resource recreation. Warm passes stay alive and a startup failure leaves tracing off with its reason.
- Unused compute GI/reflection pipelines compiled alongside their SER equivalents; compute fallback is prepared during loading only if SER creation fails.
- Silent texture-budget fallback presented as normal PT. The displayed picture now fails closed with names/bytes/reasons logged and a top-screen notice. The old 64-per-retry limit is gone.

### Workarounds still live / gaps for the coordinator

- The base/detail, alpha and strand atlases still have live users: hair/eye material records and bakes, coverage/micromaps, old G-buffer paths and legacy shader reads. This job removes **silent presentation** of a refused shared texture, not that whole material architecture. They are not dead code, so deleting them here would change the picture and overlap the materials worker. Base/detail + alpha account for 256 MiB before cookies/sky. These copies and their limits remain PT-45/PT-60 gaps. Shader-side fallback branches still exist for legacy native callers; managed texture refusal no longer lets that frame be presented. A mip-streamed texture dying between managed scans remains a separate native-handle validation gap; the managed refresh repairs its handle on the next scan.
- The 90%-of-Windows-budget sharing guard and quarter-of-headroom check remain as allocation safeguards, now surfaced as paused PT, not smaller textures. Whether to retain these exact margins needs the coordinator's five-scene memory measurements. This worker does not claim `0 kept back` in game.
- Caustic triangle capacity growth still discards overflow light for a frame; the hard cap and missing unbiased overflow handling are pre-existing PT defects. This job only changes when unused storage is allocated/released. The wet store remains 32 MiB, and the fixed frame/history/radiance-cache allocations stay reusable between scenes. The existing cloth count/vertex caps and plant build scheduling (temporarily still pending groups) also remain; this worker did not add or tune them. Keep these in PT-60/PT-71 where not already tracked.
- D3D12 exposes no portable serialized state-object blob; the two SER state objects rely on the driver cache. An application build change can trigger their recompilation even with all 47 explicit cache entries present: one new-binary run took 86.673 s in this state. Final warm timing has varied with concurrent work (see validation); no shader or image-quality compromise was added.
- The memory line accounts allocated resource bytes, includes upload/readback system-memory allocations, and counts opened shared textures once. It is not physical residency: Windows can count shared allocations again; driver-private pipeline/descriptor heaps and opaque Ray Regeneration/NGX allocations are not visible. NRD pool resources are counted when built with NRD; this worker build uses NRD OFF as required.

A review before the first full run completed found that maintenance could consume a clear before TRACE saw it, leaving old atlas contents. Fixed with a persistent sceneResourcesDirty flag, consumed only after the atlases are cleared on D3D11; old completed-frame/water results are invalidated too. The unfinished full run was stopped (only this worker's PID 27612) and the final run restarted on the corrected binary. No completed whole suite was repeated.

## Validation state

- Native targets TGPathTracing.Native and tgpt_gpu_test build with NRD OFF. Managed net472 builds with zero warnings/errors. Source checks: 94/94.
- Production TextureShare.cs exercised with stubbed Unity/native edges: eight assertions pass (budget refusal supplies no substitute, blocks presentation, shows missing bytes, logs identity/dimensions, retries and repairs the real material, driver refusal is visible, scene reset clears stale refusal, unsupported texture types cannot silently become constants/tiles). Unknown allocation sizes are stated as unknown, not presented as zero missing memory. Source: tests/texture_share_lifetime_test.cs; runnable generated project: %TEMP%/tgb/memory-build/TextureShareTest.csproj.
- Focused GPU memory test after lazy allocation: accounted total 1,050,738,688 -> 1,013,645,312 bytes on scene clear; geometry 16,777,216 -> 0; acceleration 17,825,792 -> 0; records 1,441,792 -> 0; scratch 1,048,576 -> 0. No fixture scene-owned geometry/BLAS/record/scratch allocation remains after the fence. Plants and cloth were zero before and after; populated plant/cloth unloads still need the coordinator's game scenes. The fixed frame/history/atlas stores account for the remaining total and are itemized.
- Warm-cache timings so far: original 51-pass build 2.352 s (49 explicit hits); reduced 49-pass build 3.492 s (47 explicit hits), while other GPU tests and Starfield ran. The strict under-3-second check failed on the latter; record this as a timing failure, not a cache miss. Both are seconds rather than the measured first build's 186.765 s. Final focused unchanged-binary run passed in 2.412 s with 47 hits / 49 passes. The full suite then warmed in 3.840 s with the same 47 hits / 49 passes. A strict under-three-second guarantee is not established under contention.
- The corrected native binary's final focused/full chain completed from one background script (%TEMP%/tgb/memory-build/run-final.ps1). It checks the live game process, main harness RUNNING holder and MAIN Run-PtHarness.ps1 -GpuCheck before each process. It waited for another session's game run (opened 23:42:45, harness 20261007-234234, holder 31764) to close. This worker neither launched nor stopped it. GPU timings during concurrent work are not performance measurements.
- No mutation checks, as instructed. No main-tree/game-folder writes, deployment, commit, push, SDK download, or SDK source copying.

The single completed full run and its five failures are detailed below. No full rerun or mutation check followed it.

## Coordinator's single in-game session

Use the five scenes from 20261007-224654, undenoised first, then RR/NRD. This worker has not run or judged the game.

1. At loading: native `Shader cache: <path>` then one `Shader pass <name>: <stage> <seconds>; disk cache hit/miss` per pass. The two SER state-object lines say driver-managed cache; their displayed miss is not proof the driver missed its own cache. `Path tracing passes made in ...` and managed `Path tracing's passes are ready...` must precede judged traced frames. On first compilation with the row On, inspect the existing top-screen panel `TG Path Tracing` / `compiling shaders (n of m)`, and confirm it disappears when ready. A subsequent unchanged-binary start should have 47 explicit hits on this SER card (49 total passes; other cards select their supported set).
2. Per scene, keep every `PT memory bytes: scene=... meshGeometry=... acceleration=... scratch=... plants=... cloth=... skinned=... records=... sharedTextures=... atlases=... targets=... histories=... caustics=... wetStore=... yardstick=... radianceCache=... transfer=... denoiser=... retired=... total=...` line. The components sum exactly to total; the plugin's MB is total shifted by 20. Compare the per-scene allocations after unloading against the same scene on return, allowing in-flight retired bytes to drain. Fixed frame allocations do not go to zero.
3. Expect `Traced scene released after the game unloaded a scene` and native `The plugin cleared the scene`; static BLAS compaction lines show original/compact bytes and cumulative saved bytes. Watch whether additive unloads cause frequent full recapture; there is no fallback distance cap.
4. The shared-texture report explicitly includes `0 kept back` when healthy. Also inspect other refusal categories: zero budget refusals does not prove every texture opened. Open/freed native counts should respond to unloads. Any refusal names the texture, ID, size, format and requested bytes (unknown where the API cannot measure it), and must show the top-screen paused message with the game's rendering restored. The owner should confirm this proposed failure behavior. Do not judge a paused/vanilla capture as PT.
5. Confirm RR is actually active for its captures. The worker neither changes TGBetterUpscaling's memory-stop logic nor claims it cannot still run out. Use the new category totals and refusal names to separate remaining texture/atlas/SDK costs. No texture-size reduction, LOD/range change or new picture budget was introduced.

Build/test outputs live under %TEMP%/tgb/memory-build; staged DLL is %TEMP%/tgb/memory-stage/TGPathTracing/TGPathTracing.Native.dll. GPU test logs: test-memory.log, test-startup-warm.log, test-full.log. Native per-pass/memory details: memory-build/memory-runtime/TGPathTracing/TGPathTracing.Native.log. The generated run-test.ps1 is the guarded launch recipe; -Mode memory / startup-warm / full selects focused or whole checks. Do not merge these build products as source.

Final-binary focused tests have now completed after the game closed: memory exit 0; warm startup exit 0, **2.412 s**, 47 explicit disk-cache hits / 49 completed passes. The earlier 3.492 s timing failure remains recorded above rather than discarded. The full GPU suite completed on native SHA256 404A8162C27735E4BC1DAD416956118D19CE6CE1D16FDF6E1C84902445091DD7; build, staged and test-runtime DLL hashes match. Managed source rebuilt after the unsupported-texture guard and overlay correction: zero warnings/errors. All eight refusal/retry/reset checks pass; source checks remain 94/94.

## Changed files

- `native/pathtracing/include/tgpt_api.h`
- `native/pathtracing/src/{extension.cpp,gpu.hpp,nrd_denoiser.hpp,pass_cache.hpp,runtime.hpp,scene.cpp,scene.hpp,texture_share.cpp,texture_share.hpp,tracer.cpp}`
- `src/TGPathTracing/{NativeTracer.cs,SceneCapture.cs,Session.cs,TextureShare.cs,TracingOverlay.cs}`
- `tests/{pathtracing_gpu_test.cpp,source_checks.py,texture_share_lifetime_test.cs}`
- This report.

`pass_cache.hpp` and `texture_share_lifetime_test.cs` are new. Native and managed changes must merge together; old DLLs lack the new required exports. No HLSL, material-program, lights, temporal passes, bridge, framegen, upscaling, neural rendering, CMake, SDK source or coordinator-owned memory file changed. `nrd_denoiser.hpp` is our pool-accounting wrapper, not NVIDIA source. Merge source-check additions carefully alongside other workers' changes.

## Full-run failure evidence (no thresholds changed)

- Glass: IOR 1.2 differs from 1.5 by 1.784 (shape difference passes), but its transmitted-light ratio is 1.63 against the required 0.5 to 1.1. Default-vs-explicit 1.5 and straight-through IOR 1 checks pass.
- Opacity micromaps: two meshes reported, zero waiting; the assertion expects three. The following micromaps-on/off image comparison is exactly zero difference and the two alpha instances use micromaps. Cause not established; no micromap build exception appeared in the native log.
- Legacy dead-texture fallback: image differences are 1.15e-3 for a missing handle and 1.11e-3 for a dead handle, against tolerance 1e-3. The failure also occurs without the death/lifetime path. Sharp shared-texture correlation is 0.871 versus 0.001 for the atlas; that check passes. This does not exercise the new managed refusal gate, whose separate eight assertions pass. Legacy shader fallback still exists and is not claimed removed.

The micromap builder still retains its 16 MiB minimum scratch allocation and uses its existing batch layout; the shared-largest-scratch change applies to static/skinned/cloth/plant BLAS builds. Micromap scratch is included in the totals and released on clear. This is a remaining small allocation to consider after the real-scene measurements.

- Clear-glass shadow: measured 55.1 against a threshold about 26.6 (noise 15.1, solid shadow 131.8). No glass transport was altered by this worker.
- Forward-drawn eye: plain material 0.0806 (required below 0.005), eye 0.0911 (required above 0.02). The eye brightness passes; the plain-material criterion fails. Review that expectation against the camera-ray migration rather than assuming this worker broke eye shading. No threshold was changed here.

The prior `merge-4.md` already records older glass-IOR failures in both clean and mutant runs. That is historical context, not a controlled baseline comparison for this binary. The causes of the five current failures are not established by this job.

## Full GPU run (final binary)

Completed with exit 1: **1,387 checks pass, five fail**. 25,372 frames traced, zero frame failures, last error 0, disabled 0; 25,371 composites applied. All stages reported GPU timings. NRD was OFF and RR is never run by this offline harness. No assertion was weakened and no completed full suite was rerun.

FAIL lines, verbatim:

```text
FAIL: glass bends light by its material's own index of refraction (another index lays the light elsewhere)
FAIL: every mesh with alpha-tested instances has opacity micromaps
FAIL: a material without a number, or whose texture died, reads its atlas tile as before
FAIL: clear glass casts no shadow
FAIL: a character's eye drawn forward is shaded by the tracer, not left to HDRP
```

The guarded queue has exited and no test from `memory-build` remains running. Final source checks are 94/94. The final native header edit only corrected the stale half-minute startup comment; executable code was unchanged after the tested binary was built.

## Accounting cross-check

All 211 complete `PT memory bytes:` lines in the worker's native log sum exactly to their reported total. The final full-test snapshot below is allocation accounting, not residency or the game's memory usage. It includes fixtures deliberately retained by the native test until process exit. The separate scene-clear fixture above tests release.

| Category | Bytes |
|---|---:|
| Mesh geometry | 33,554,432 |
| Acceleration | 92,930,048 |
| Scratch | 41,025,536 |
| Plants | 0 |
| Cloth | 0 |
| Skinned data | 84,148,224 |
| Scene records | 18,087,936 |
| Shared textures | 0 |
| Atlases | 363,462,656 |
| Frame targets | 363,855,872 |
| Histories | 291,962,880 |
| Caustics | 167,182,336 |
| Wet store | 33,554,432 |
| Yardstick | 4,390,912 |
| Radiance cache | 37,748,736 |
| Transfer | 151,584,768 |
| Denoiser | 0 |
| Retired | 0 |
| Total | 1,683,488,768 |

Earlier full-test snapshots included 55,574,528 bytes of plants and 131,072 bytes of cloth; both return to zero in the last snapshot as those instances leave. Skinned test registrations remain explicitly registered, as documented in the incremental-retirement limitation. The larger fixed targets/histories reflect the later full-test cases. The full run had no frame or device failure while exercising the changed storage paths.
