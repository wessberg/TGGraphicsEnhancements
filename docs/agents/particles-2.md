# Particle simulation capture, stage 2 (PT-67, PT-71 item 7)

2026-10-09. Worker `particles`, base `base67`. Finished at brief step 1's conditional diagnostic hand-over; no game or deploy.

Result: native/managed builds and focused checks pass. Raw compute-buffer snapshots are ready for one game run. There are no decoded game particles, D3D12 particle records or positional dots yet; the supplied metadata could not establish that layout. PT-67/PT-71 item 7 remain open.

## Decision before code

The supplied candle observer run does not establish a simulation attribute layout. Its two attribute allocation IDs are 2190 and 2206; the only linked compute hash is `1f1e2c1981ef2e0070feb2594064c6f321f99dfdf0498229fe312856fdb34db8`. The summary reports a missing compute shader in every sample; raw-record inspection below shows those warnings concern shared camera constants, not this candle writer. No buffer contents, task descriptors, component-to-batch mapping, initialization trace or campfire sample exist in that report. Binding a UAV alone does not prove writes to its particle range.

The brief explicitly says to build ONE further diagnostic and stop if this evidence cannot answer the layout questions. That condition applies. I will first inspect the raw records to distinguish analyzer mistakes from missing capture, then add the diagnostic needed to decode actual simulation data. I will not invent positions, colours, lifetime, alive state, material identity, or a game adapter from bounds/names/output draws. A synthetic transfer can prove transport only; it cannot validate an unknown game layout.

## Scope and safety

Only this copy and `%TEMP%/tgb/particles-*` are writable. No main-tree writes, game-folder access, game launch, deployment, installs, downloads or mutating git commands. No whole GPU suite. Coordinator-owned memory files stay untouched. The single next game run belongs to the coordinator.

## Progress

- Read BACKLOG, path tracing context, DEAD ENDS, build recipes, latest round, worker rules, prior VFX capture report, effects inventory and medium design.
- Read the supplied vfx-report: 40 sampled frames, candle only, facing/away/returned/frozen with Off and On. No decoded particle capture exists.
- Inspecting the observer implementation and raw summary next.

## Checks

Pending. No build, focused GPU check, source checks or visual proof claimed yet.

## Workarounds

None removed: HDRP particles, proxy lights, transparent exclusions and distortion suppression still have rendering users. Capture diagnostics must leave the picture and simulation unchanged when off.
## Evidence correction and implementation checkpoint

The raw records correct the first report's broad error text. The linked candle writer WAS seen at creation (2,344-byte CS, three creations). The missing-CS events bind shared camera constants and belong to unrelated large structured-buffer work. Only frame 15906, dispatch sequence 18, binds candle allocation 2206 as UAV 0, with groups [1,1,1], before trace. Allocation 2190 has no observed writer in the sample. This is consistent with initialization-only data, but is not proof of the kernel's meaning. Both allocations are 18,432 bytes (128 times the output shader's 144-byte Single instance stride). No compute bytes or attributes were retained by the old run.

Read vfx-overview.jpg: the camera actually faces the candles, turns toward the stall, and returns. Campfire is absent. No same-frame particle position check is possible from these records.

Implementation now adds an explicitly gated raw-UAV compute diagnostic. It saves declared buffer inputs BEFORE each candidate dispatch and outputs AFTER the same dispatch, including deferred command-list execution; no dispatch or draw is replayed. It retains the latest task snapshots from startup so initialization-only candidates survive until screenshot capture. All compute bytecode is retained as local runtime evidence, never source to merge. Unknown programs, retained snapshots and candidate membership remain distinct from actual particles. Storage grows with the observed evidence; no particle-count truncation.

The native bridge/API structs stay unchanged. A diagnostic-only enable export operates while PT is Off; using TGPT_Frame spare fields would require tracing to run and miss startup. No option bit is consumed. Hidden Advanced setting: Particle simulation diagnostic (false). The startup marker and setting are both required for initial capture; the launcher restores both. No material, lighting, exposure or normal rendering change.

Per brief step 1's stop condition, the game-layout-dependent repacker, D3D12 particle transfer, per-system alive counts and positional dots are deferred to the next worker. Drawing dots from unproven floats would be guessed particles. The focused test is for exact raw snapshot values/ranges/ownership, not a claimed substitute for the later D3D12 record test.
## Validation checkpoint

- Plugin and harness compile: zero warnings, zero errors (local offline NuGet feed from existing cached reference-assembly packages; no downloads).
- Source checks: 101/101 pass at the first checkpoint.
- New analyzer fixtures: four tests pass, including nine corruptions (value hash, byte count, submission count, frame/ownership, indirect range, program identity, replay and bindings). A texture-copy record and missing buffer fail.
- Native shader compilation remains running in particles-build, NRD OFF. Separate particles-check builds the focused raw-snapshot test successfully. Its single gate script is waiting for the coordinator's live game harness (RUNNING PID 53064); no GPU work has been started by this worker.
- The diagnostic's declared scope is raw-UAV compute programs, not all GPU work. It retains all shader bytecode to decode initializer/update/output programs from the same game. Structured-only particle writers, unseen shader creations and buffer-to-component mapping remain explicit unknowns.
## Hand-over contract (the coordinator runs this once)

After merging the source and rebuilding TGPT normally (with NRD enabled in the main tree), run:

```powershell
.\tools\harness\Run-PtHarness.ps1 -Scenario candle,campfire -Recipe particles-capture
```

This is the additional layout diagnostic authorized by brief step 1, not the later capture acceptance run. PT stays Off, so the reference screenshots are the game's own picture. Each scene gets two matched frames facing, turned 180 degrees away, returned, and frozen; each phase has a discarded primer. Do not judge frame rate: synchronous staging reads intentionally invalidate timing.

Read `particles-report.md`, `particles-summary.json`, and `vfx-overview.jpg` in the run folder. Each phase's `frame-N-simulation.json` links exact input/output buffer bytes to shader hashes, resource lifetime IDs, view/batch ranges, indirect arguments and frame ownership. Each `frame-N-image.json` also includes all loaded graph/system public state, transforms, exposed properties and output materials. Shader bytecode lives only in runtime evidence under `simulation/programs/`; never merge or publish those files as project source.

Expected evidence: candle initializer/update code with the actual constants and attribute values; fire/smoke/spark candidate compute inputs and outputs in the campfire scene; and which allocations continue receiving dispatches after turning away. A retained startup snapshot is labelled retained, never counted as current live particles or proof of off-camera updates. The raw-UAV filter identifies candidates, not families. Structured-only writers and shaders created before hook installation remain possible gaps. Hidden append/consume UAV counters are not separately captured; explicit count/argument buffers are. The next worker must inspect the program and recorded resources before assigning alive/count meanings.

Known candle OUTPUT layout from the prior shader audit (not a decoded simulation contract): Single attribute address `144*I + 32*P` reads position, with pivot at +12 and horizontal scale at +16; the identity/seed input is at `144*I + 128 + 4*P`. Graph values have 176-byte instance stride, output count at +0, scalar size at +144 and seed at +156. The output index list is addressed using its batch constant, active-instance index and output ordinal. This is not proof of the simulation's complete alive list, world-space transform, birth generation, colour, age or lifetime. Other candle families have different strides and output rejection. The supplied run has no fire layout evidence.

Stop if required phases/files are missing, hashes/byte counts/ownership fail, command lists were reused, predication is active, or the native observer reports missing coverage. Zero integrity errors means the raw diagnostic was recorded, not that particle capture is complete. No dots are drawn; a positional overlay needs decoded world positions. Do not call arbitrary floats positions, map a component by bounds/name, treat old public counts as alive records, or reactivate disabled gameplay effects.

The next worker decodes this evidence, implements the family adapters and growing D3D11-to-D3D12 records, then runs the specified D3D12 round-trip test and positional overlay. Emission/scattering/absorption and heat haze remain subsequent jobs. No HDRP particle draw, light proxy, distortion workaround or material exclusion became dead in this job.

## Build and ABI checkpoint

- All 40 shader headers compiled, zero failures (37.6 minutes with other workers compiling). TGPathTracing.Native, tgpt_gpu_test and tgpt_vfx_simulation_test build. Native DLL/new focused target: no warnings. The unchanged full GPU test source has four existing C4244 conversion warnings; it was compiled, never run.
- Plugin and harness: zero warnings/errors. Four new analyzer tests and ten prior observer analyzer tests pass. Source checks: 101 pass.
- Native GPU test still awaiting the live game gate at this checkpoint. The initial run correctly failed frame ownership after a long-path file-write failure; the output writer now uses extended Windows paths and logs failure details. Clean rebuilds of the short-path scratch test avoid stale header dependencies in its first long object path. Final outcomes will be added below.
- Production ABI unchanged: 0x00012600; TGPT_Frame 608 B, TGPT_Status 216 B, TGPT_Material 204 B. No public struct or option bit changed. Diagnostic control export: `TGPT_VfxSimulationSetEnabled(uint32_t) -> int32_t`, mirrored in ParticleDiagnostic.cs. It is separate because capture must start before the first traced frame and while Mode is Off; TGPT_Frame's reserved fields cannot deliver that control.
- Latest startup state is retained per compute writer/output allocation, not per transient constant-buffer view. Every dispatch in a sampled interval is retained independently. Copies relinquish their source COM references once queued; the diagnostic does not keep dead game allocations alive merely to read its own staging data.
- This worker changed 19 source/report files (listed below); coordinator memory, other mods, bridge source and game files are unchanged by this worker's commands. No deployment, installation, download, git mutation or game launch.

## Final results, 2026-10-09 09:59

- Clean focused hardware run passes: 224 assertions, including startup initialization before sampling, direct and deferred dispatches, nonzero constant-buffer batch ranges, shuffled indices/dead entries, three systems per frame, 4/128/2048-entry growth, changing frame IDs, resource replacement, indirect arguments at byte offset 8, immutable input snapshots and an empty frame after a full frame. No texture is copied and no pipeline binding/draw/simulation is replayed. The switch-Off case captures nothing.
- Four real compute-shader mutations (`value`, `count`, `index`, `frame`) each exit 1 at the independent exact-output check; clean exits 0. These validate the raw snapshot fixture's oracle, not the unimplemented D3D12 particle transfer. Logs: `build/particles/focused-current.log`, `gpu-clean.log`, `gpu-{value,count,index,frame}.log`.
- The Python reader also validates all five clean GPU-produced documents, with zero errors. Submitted dispatch counts are 0, 3, 3, 3, 0. The last frame retains earlier diagnostic snapshots but submits zero current records; it is not labelled a live-particle frame.
- Native final build passes (`build/particles/native-final.log`); plugin/harness final builds have 0 warnings and 0 errors (`managed-final.log`). Source checks 101/101 (`source-checks-final.log`); new reader tests 4/4 (`analyzer-final.log`), prior observer reader tests 10/10 (`observer-analyzer-test.log`). No full GPU test, ctest, D3D12 round-trip, game run or visual acceptance is claimed.
- All GPU attempts used the main tree's GpuCheck-only command and the live RUNNING/game gate. The waiter has completed. Earlier waiters owned by this worker were stopped before rebuilding; no other process was stopped. The native shader/build queue has also finished.

Merge files (19): CMakeLists.txt; this report; native/pathtracing/src/{extension.cpp,vfx_observer.hpp,vfx_simulation.hpp}; src/TGPathTracing/{Settings.cs,Plugin.cs,ParticleDiagnostic.cs}; src/TGPathTracing.Harness/{EffectsInventory.cs,Request.cs,Runner.cs,VfxCapture.cs}; tools/harness/{Run-PtHarness.ps1,vfx_simulation.py}; tests/ingame/scenarios.json; tests/{vfx_observer_test.cpp,vfx_test_support.hpp,vfx_simulation_test.cpp,harness_vfx_simulation_test.py}. The old observer test's fixture helpers moved to vfx_test_support.hpp for reuse; its checks are unchanged.

Focused test after a normal configure/build, through the same game/RUNNING/GpuCheck gate:

```powershell
$env:TGPT_VFX_SIMULATION_ONLY = '1'
& <private-build>/tgpt_vfx_simulation_test.exe <private-output>
```

This worker ran the same test source in the separate short-path `particles-check` build (`particles_snapshot_test.exe`) while the production shaders compiled, then built the registered `tgpt_vfx_simulation_test` target too. Both use the actual observer/snapshot implementation. No test-only production branch was added. Set TGPT_VFX_MUTATION to value/count/index/frame for the four shader corruptions; clear it for the clean run.


Concurrent edit at hand-over: tools/Build-PtShaders.ps1 changed at 09:59:35 while this worker was finishing (new MachineLimit/FreeGb cross-process compile gate). This worker did not edit that file. It is preserved and excluded from the 19-file merge list above; coordinate that external change separately. No worker code was reverted.
