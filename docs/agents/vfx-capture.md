# Candle VFX capture investigation (PT-71 item 7)

2026-10-07. Worker in `wt/vfx`, pristine base `wt/base41`.

**Result: a built, metadata-only D3D11 observer and runnable `vfx-capture` diagnostic. No particle capture or D3D12 particle transfer.** The owner corrected the route at 22:00: simulation runs once; tracing replaces drawing. The observer finds the simulation allocations from dispatch outputs and cross-checks them against candle draws. It never replays a draw, changes a binding, copies/reads a GPU buffer, or suppresses an effect.

The native observer's 41 hardware checks, ten analyzer tests, and all 90 source checks pass. Three GPU mutations and six analyzer mutations were rejected. The final-build `tgpt_gpu_test` passed in 322.02 s (NRD OFF). No game, deployment, commit, download, shared bridge edit, production struct change or option bit. PT-71 item 7 stays open. **Stop here for the coordinator's one game run.** The next worker needs its compute/buffer/timing evidence before building actual records.

Coordinator, after merging and rebuilding/deploying TGPT through your normal route (this worker's native build has NRD OFF):

```powershell
.\tools\harness\Run-PtHarness.ps1 -Scenario candle -Recipe vfx-capture
```

The launcher builds/installs the harness, enables the native observer before shader creation, restores its marker/configs, and runs the analyzer. Read `vfx-report.md`, `vfx-summary.json`, and `vfx-overview.jpg` in the run folder. Exact evidence and limitations are in the final section of this report. `campfire` is optional; it is not needed for the one candle diagnostic and may have no live candles.

## Earlier investigation (retained evidence; implementation status above supersedes it)

## Recovery and scope

- The previous worker left **zero changed/new/deleted tracked files** against base41. Verified with the coordinator's `merge_copy.py vfx --base=base41 --root=<scratchpad> --dry` from the main tree. No merge performed.
- Its scratch files were `<scratchpad>/vfx/dis_all.py`, `%TEMP%/tgb/vfx-ilspy/` (the two decompiled managed assemblies), and `%TEMP%/tgb/vfx-shader/single/` (one output shader, 12 programs/disassemblies). These are useful research, not an implementation. Inspected them; freshly decompiled VisualEffect and freshly extracted all candle shaders as cross-checks.
- Read BACKLOG, PROJECT-CONTEXT path tracing / dead ends / build recipes, latest round and the path tracing round, the complete `design-particles-fog.md`, and effects inventory notes. Coordinator-owned memory files remain untouched, as this brief requires.
- This job is particle capture only. No flame medium, traced emitter, guessed particles, replacement lighting, game launch, deployment, commit, downloads, or shared bridge edits.

## Evidence before implementation

- `artifacts/harness/20261007-191814/effects-report.md` (main tree, read-only) names the candle output shader families: Single, Set_Candles_01, Set_Candles_02, **Set_Candles_Straight**, **Set_Candles_Straight_Short**. The last two must not be silently omitted.
- The same inventory contains negative reported live counts (for example Fire_EmberGen totals -2208). These are not usable exact particle counts; interpretation must be checked against shipped runtime API/records. This is a diagnostic gap for the coordinator to track; do not interpret a negative number as zero particles.
- Public system counts and renderer bounds do not establish particle positions or identity. The observations below were recorded before choosing the fallback; no guessed particle positions were built.

### Managed access: checked in the actual shipped assemblies

Read-only inputs: `Fall of Avalon_Data/Managed/UnityEngine.VFXModule.dll` and `Unity.VisualEffectGraph.Runtime.dll`. Research output remains ignored under `build/vfx/research/` and the previous worker's `%TEMP%/tgb/vfx-ilspy/`. Do not commit decompiled Unity code.

Input SHA-256: VFXModule `482E007E47843ED7E0CDBE84A5508D583A639CC67244FD805ABBB128C072C62B`; VFX Runtime `2EA59947081AA265527719A064F00010D945C51F033644DCC088135B194F94D4`; `resources.assets` `FF2180BC642A5BC32AB19856CF3BB59FD088920CEA17BA8969694659AA7C4387`.

| Entry | Actual access |
|---|---|
| `VisualEffect.GetParticleSystemInfo` | Native call returning aliveCount, capacity, sleeping, bounds. No resource pointers or attributes. |
| `VisualEffect.GetGraphicsBuffer(int)` | Internal, but reflection does not solve this: its native binding is `GetValueFromScript<GraphicsBuffer*>`. It reads a graph's script property, not its internal attribute/indirect buffers. None of the five inventoried candle families exposes a GraphicsBuffer property. |
| `VisualEffectAsset` | Exposed-property names/types, systems, events, texture dimensions and exposed coordinate space. No compiled task descriptors or GPU-buffer getter in the shipped class. |
| `VFXRenderer` | Empty managed subclass of Renderer apart from its constructor. No BakeMesh, particle stream, or vertex-buffer API. Ordinary ParticleSystemRenderer.BakeMesh is not applicable. |
| `VFXManager.GetComponents` | Can enumerate VisualEffect components. `GetBatchedEffectInfo` / internal `GetBatchInfo` describe batching, not shader resources. |
| `VFXManager.PrepareCamera` / `ProcessCameraCommand` | Native camera/output preparation. Overloads take CullingResults; legacy overloads omit it. None returns buffers, and omitting culling results is **not proof** that all loaded/disabled effects simulate or submit output. |
| `VFXManager.SetRayTracingEnabled` / `RequestRtasAabbConstruction` | Present, but not a data export. No evidence they can supply geometry to our D3D12 device while Unity renders D3D11. Do not re-enter the stripped-HDRP-DXR dead end. |
| `VFXRuntimeResources` | SDF baker shaders, not the live graph simulations. Runtime utility/binder classes operate on exposed properties/events. |

Unity's [VisualEffect API](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/VFX.VisualEffect.html) and [6000.0 binding declarations](https://github.com/Unity-Technologies/UnityCsReference/blob/6000.0/Modules/VFX/Public/ScriptBindings/VisualEffect.bindings.cs) agree with this boundary. A buffer with the right property name cannot be assumed to be the game's internal allocation. `aliveParticleCount` can also be delayed by GPU readback ([Unity documentation](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/VFX.VisualEffect-aliveParticleCount.html)).

### The shader assets are readable; the graph simulation descriptors are not decoded

`tools/research/unity_shader_blob.py` already decodes the LZ4 D3D11 program chunks of Shader Graph outputs in `resources.assets`. It does not decode VisualEffectAsset/VisualEffectResource, graph compute tasks, their serialized buffer descriptors, or arbitrary asset bundles. No general asset-bundle reader was found in this tree; UnityPy is not installed. Nothing was installed or downloaded.

Added `tools/research/vfx_shader_audit.py`: reads `resources.assets` with a read-only mapping, validates each full serialized shader name, extracts each output program in memory, and writes **hashes, binding descriptors and input/output signatures only**. Optional disassembly goes to scratch for local investigation, never into the change set. This avoids confusing `Single` with `Single_UnscaledTime` and gives a later native hook exact program identities. It is not a GPU capture or a graph-layout decoder.

Reproduce from this copy (no game/GPU needed):

```powershell
python tools/research/vfx_shader_audit.py --out build/vfx/shader-audit.json --disassembly C:\Users\Frede\AppData\Local\Temp\tgb\vfx-shader\audit
```

Result: **9 shaders, 120 programs (54 vertex, 66 pixel), no compute programs**. No RDEF reflection in these programs; Unity stores some parameter names outside DXBC. The output-shader inputs below are recovered from actual compiled address calculations, not assumed from Unity's current editor package.

| Candle suffix | Programs | Attribute instance stride read by output | Graph-value instance stride | First vertex SHA-256 prefix |
|---|---:|---:|---:|---|
| Single | 12 | 144 B | 176 B | dc3deb8085ce02bf |
| Single_UnscaledTime | 14 | 144 B | 176 B | dc3deb8085ce02bf |
| Set_Candles_01 | 12 | 256 B | 192 B | c697bcb6243b2e72 |
| Set_Candles_02 | 12 | 128 B | 192 B | 343c89e0e2822c93 |
| Set_Candles_Corner | 14 | 2048 B | 192 B | 7f83648e20a0d8ae |
| Set_Candles_GallahadCrypt | 14 | 384 B | 192 B | 8f4b3485d5cc3161 |
| Set_Candles_Rounded | 14 | 2048 B | 192 B | 03ac448b469baa47 |
| Set_Candles_Straight | 14 | 1664 B | 192 B | 4978ad1b695b48f2 |
| Set_Candles_Straight_Short | 14 | 1024 B | 192 B | ed896121919307d3 |

All names start `Hidden/VFX/VFX_CandleFlame_` and end `/Flame/Output Particle|Shader Graph Quad - Unlit`. Nine are shipped, five appeared in the supplied inventory. The equal Single/Single_UnscaledTime vertex hashes mean **vertex hash alone cannot identify the graph family/time source**. Full hashes and all variants are in `build/vfx/shader-audit.json`.

### What the candle vertex shader actually reads

For the non-instanced Single variant at chunk 0 offset `0x4f6` (the old worker's `00-vs-chunk0-4f6.txt`):

- VS `t0`: raw attribute buffer. Let `I` be `b3[0].x`, `A` be `b3[0].y`, and `P` the particle index read from `t1`. Its position is three floats at byte `144*I + 32*P`; pivot along the vertical quad axis is at `+12`; horizontal scale at `+16`. The uint used with the system seed is at `144*I + 128 + 4*P`. Its role as a seed/identity input is verified; a birth-generation guarantee is **not**.
- VS `t1`: uint structured buffer, stride 4. The draw ordinal is `vertexID/4 + instanceID*2048`. The shader first checks it against the uint at `t2[176*A]`, then fetches `P` at element `b2[0].z + A + ordinal`. This is the **output's compact index list**, not proof of the full simulation alive list. No simulation dead-list pointer was recovered.
- VS `t2`: graph values, raw. The count is at `176*A`; scalar size at `+144`; system seed at `+156`. The pixel stage also reads this allocation for the material parameters.
- VS `b0`: view/projection and camera-relative coordinates; `b1`: object matrices; `b2`: output parameters; `b3`: instance indices. The quad axes are made from the view/object matrices, normalized and scaled, then shifted by pivot. Position alone and `transform.lossyScale` cannot reproduce its corners.
- The quad corners come from vertex ID, with UVs 0..1. No conventional texIndex/flipbook-frame attribute is read by this output. No colour, alpha, angle, age, lifetime or velocity attribute is read here either. This does **not** prove that none is stored in the simulation: those compute layouts remain undecoded. Mark unavailable fields unknown; do not invent them.

For Set_01/Set_02, position/pivot/scale use a 32-byte per-particle block inside their respective instance strides; seed is at byte 20 and the identity/hash input at byte 24. Graph values are 192 B per active instance. The output reads size endpoints at `+152/+156` and random-count inputs at `+160/+164/+168`. It then rejects some particles **in the vertex shader** according to those inputs. Thus public alive count, compact-list count, and non-degenerate rendered-quad count are different quantities even in a frozen frame.

The instanced variants also pack multiple systems into a draw, using dynamically indexed b1/b3 arrays and b2 batch offsets. Single packs output ordinals in groups of four despite its inventoried capacity of one; GallahadCrypt uses a padded output grouping of twelve but a list stride of nine. These are compiler packing details, not limits to impose on our scene. Hard-coding one system per draw or treating SV_InstanceID as a VisualEffect ID is wrong.

Unity's [particle-output template](https://raw.githubusercontent.com/Unity-Technologies/Graphics/6000.0/staging/Packages/com.unity.visualeffectgraph/Shaders/VFXParticleCommon.template) corroborates separate indirect indices, output attributes and optional flipbook calculations. It is a conceptual reference, not proof of this game's offsets. No Unity implementation was copied into project code.

### Colour and alpha are a material evaluation, not one particle colour

The simple Single pixel variant has flame/noise textures at PS t0/t1 and graph values at t2. A fog-enabled variant moves them to t2/t3 and graph values to t12, alongside HDRP fog inputs. Read **actual bindings for the selected variant**, not a universal slot number.

The compiled material samples animated noise and uses it to distort flame-texture UVs; brightness multiplies the sampled colour. The animation depends on shader time, particle position/random value and graph parameters. The fog-enabled variant includes a radial alpha expression and HDRP fog/pre-exposure. These are not a texIndex-driven flipbook or a uniform colour/alpha per quad. The future capture contract must retain the texture handles, shader/adapter identity, time and graph parameters. It must distinguish raw particle modulation from final appearance. Do not bake the appearance into a copied texture or trace HDRP's fog result as flame emission.

The inventory confirms why Material.GetTexture is insufficient: `_FlameTex` and `_NoiseTex` are `none` on the output material. Single's exposed `FlameTexture` instead names `Tex_Volumetric_CandleFlame`, a **512x512 Texture2D, DXT5 sRGB, 10 mips**. The name “Volumetric” is not a 3D density asset. Set families expose no texture property. The native draw bindings are needed for those actual texture resources; use the existing shared-texture registry if sharing succeeds, otherwise report a gap.

### Counts, culling and freeze: corrections to the initial interpretation

In `candle/1-game-effects/effects.json`, Single has 311 components and eight reported live particles. Those eight are active, enabled, visible components. Set_01 reports 56 particles across seven components; Set_02 60 across fifteen; Straight 52 in one. **Those set renderers are disabled or their GameObject inactive**, and are outside the camera. They are not proof that 168 off-camera flames are currently rendered/simulating. `campfire` has the same set counts but zero live Single particles. Do not use a census total as the expected exact capture count.

The frozen VFX probes in the existing recipe record public counts, pause/playRate and culling, not attributes or the per-instance graph-value buffer. Still counts plus still global clocks do not prove the particles and all custom shader clocks are frozen. Single_UnscaledTime is also a shipped family even though not seen in these captures. The next diagnostic needs frame-stamped GPU state and bound clocks.

## Simulation-buffer route (the owner's correction supersedes the earlier replay proposal)

Draws identify what VS t0/t1/t2 mean, but cannot be the data source for the final renderer. Once HDRP effect draws are removed they disappear, and an off-screen flame may never draw. Do not replay the output vertex shader, submit an extra renderer list or manufacture particles from bounds/names. Keep the game's one simulation and read its real buffers at the boundary established by this diagnostic.

After the game run: decode the linked compute kernels and their buffer/task layout; establish which outputs are written at initialization versus each update, and how batched system identities/live lists are addressed. If turning away culls simulation, close that scheduling gap without another simulation tick. Inactive gameplay effects and disabled renderers with stale public counts must not be reactivated indiscriminately.

Then decode/repack on D3D11 and transfer data through the existing shared device. `tracer.cpp::copy_skinned` demonstrates a buffer SRV -> `tgb::SharedTexture` -> D3D12 data path with no bridge API change. Particles must use growing storage/pages without dropping records. Original game textures stay original/shared, not copied into a replacement atlas. Keep one signalling queue per fence and frame ownership through D3D12 consumption.

The later capture test must feed known D3D11 data with nonzero batch offsets, a shuffled live list, dead entries, multiple systems, distinct positions/colours, changing frame IDs and resource replacement. Run the actual shared transfer and read D3D12 back; test growth and an empty frame after a nonempty one. Mutate value, count, index mapping and frame ownership separately. This test belongs **after** the diagnostic answers the access/layout questions, as the correction explicitly orders. This change cannot satisfy it and does not pretend to.

The later in-game proof is the same-frame captured particle positions/corners against flame pixels, exact transferred IDs/counts, no stale or omitted off-camera active systems, and readable original textures. No positional overlay is drawn by this metadata diagnostic: there are no decoded positions to overlay honestly.

## Earlier investigation validation (before the diagnostic)


- `python tests/source_checks.py`: **90/90 pass**, `build/vfx/source-checks.log`.
- `dotnet build src/TGPathTracing/TGPathTracing.csproj -c Release -f net472`: **passes, zero warnings/errors**, `build/vfx/managed-build.log`. Used an offline feed made from the already cached .NET 4.7.2 reference packages; NuGet audit disabled; all package/temp/dotnet output redirected inside this copy; DeployToGame=false. This validates the unchanged plugin, not a new capture path.
- The new shader audit runs against the actual shipped `resources.assets`: 9/120 as above. Full generated metadata is in `build/vfx/shader-audit.json`, disassembly under `%TEMP%/tgb/vfx-shader/audit/`. No Unity source, game shader bytecode or restricted NVIDIA source belongs in the change set.
- Audit reader checks: rejected a deliberately incorrect serialized name length; decoded an independently constructed signature with register 5/mask 7 exactly; changing that fixture's register to 6 changed the result and failed the register-5 expectation. These are metadata-reader checks only, not GPU/capture mutation proof.
- No native build, `tgpt_gpu_test`, transfer mutation check or in-game capture proof: no native capture/transfer was implemented, so an unrelated passing renderer test would not establish it. No GPU/game process was started.
- No workarounds are made dead by this investigation. No light/exposure behaviour changed; no new exposure/gain result is claimed.

## Workarounds encountered

None removed: capture alone does not replace their rendering users.

- `SceneLights.cs`: GatherGlows, EffectGlows, AddEffect, Classify; `LightRules.cs`: GlowAt, EmitterAt, Claim. Bounds/name/material/proximity emitter guesses remain until traced emission replaces them.
- `SceneLights.cs`: Size and the pairing loop assigning `SceneCapture.OwnSurfaces`; `LightRules.cs`: DefaultRadius, MeasuredRadius, FlameCentre. Proxy light size/position and proximity ownership remain. `SceneCapture.OwnSurfaceKey` and `pt_emissive.hlsl`'s EMITTER_PAIRED handling still suppress nearby claimed emission.
- `SceneCapture.cs`: ReadMaterial/MaterialIndex transparent/unlit rejection. VFX output geometry remains absent.
- `HdrpPasses.cs`: SuppressDistortion, fog/forward ambient and gain patches; `SceneLights.cs`: Darken/ApplyGain. Remaining HDRP effects users still need these until migration.

Coordinator follow-up: record the unresolved buffer/output-access dependency under PT-71 item 7; the negative census counts, dormant-count interpretation and freeze-evidence limits under the harness gaps. Keep the actual capture and its GPU/in-game proof open. The later diagnostic change set is listed below.

## Implemented diagnostic

The unfinished code was built first, before edits: native TGPT and GPU-test targets and managed harness all compiled. Against base41 its five edits were `extension.cpp`, `Runner.cs`, `Request.cs`, `scenarios.json`, `Run-PtHarness.ps1`; its unfinished additions were `vfx_observer.hpp`, `vfx_programs.hpp`, `VfxCapture.cs`. Kept shader hashing/identity tags/hook forwarding and the opt-in launcher integration. Removed its buffer snapshots, staging allocations, CopyResource/Map/readback and `.bin` files. No production workaround became dead.

- `native/pathtracing/src/vfx_observer.hpp`: existing `Host::add_hook` only. SHA-256 at VS, PS and CS creation; the generated header identifies 120 shipped variants as 68 unique candle VS/PS program hashes. CS hashes are recorded even before their family is known. `ShaderTag`/JSON are diagnostic implementation details, not a particle ABI.
- Every sampled Dispatch/DispatchIndirect and all seven Draw forms are recorded, including non-candle shaders, so same-buffer readers and no-draw allocations can be found later. VS/PS/CS SRVs, constant-buffer ranges, CS UAVs, IA buffers, indirect allocation/offset, resource address/byte size/structure stride, shader hash, predication, context and command-list ID are retained. Raw-buffer stride 0 is the API descriptor, not a claim that particle attributes have zero stride. The earlier shader audit supplies the actual candle layout.
- All sizeable storage is heap allocated. No app pipeline setter or draw/dispatch is issued by the observer. No GPU bytes or textures are copied. Lifetime tags are stored as private metadata on D3D11 objects; diagnostic records hold no extra COM resource references. Shader hashes are not stable system/particle IDs, nor are buffer addresses.
- Immediate contexts use actual getters. Deferred contexts track VS/PS/CS buffer setters and constant-buffer subranges, initialization/ClearState, FinishCommandList restore rules, buffer read/write hazards and context-state swaps. Metadata enters the global sequence at ExecuteCommandList, not at worker-thread recording time. Reused lists enter once per actual execution. A list recorded outside the capture interval is an explicit coverage failure; a truly empty list is not. Deferred texture subresource hazards and GS/HS/DS input bindings are outside this buffer diagnostic's stated scope.
- Hook refusal (including TGFG's optional pixel-shader hook), changed implementations, exceptions, device generation change, unknown/stale lists, dropped boundary observations, predication and incomplete relevant bindings become evidence errors. No competing hook is replaced. Camera sample primers are excluded; accepted sample boundaries must be consecutive. Errors never become zero-particle success.
- `extension.cpp` adds three diagnostic exports: `TGPT_VfxDiagnosticConfigure(const wchar_t*)`, `TGPT_VfxDiagnosticGetRenderEvent()` (stdcall callback taking a Unity frame ID), and `TGPT_VfxDiagnosticCompletedFrame()`. `TGPT_GetRenderEvent` forwards through a wrapper that records `traceBegin` just before native event 1. No public production struct, ABI version, material/option bit or bridge source changed. The coordinator owns the ABI decision on merge.
- No `.enable` marker at device creation means no observer hooks. With it present, shader creation is tracked from the earliest game-device callback; dispatch/draw event accumulation and JSON/log output start only on Configure. Deferred binding metadata is retained while installed so a later capture has known state. This costs CPU time in a diagnostic run; it is not a performance benchmark. The marker is restored by the launcher and does not enable capture on subsequent ordinary launches.

D3D11 references: [deferred rendering](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-render), [FinishCommandList](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-finishcommandlist), [ExecuteCommandList](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-executecommandlist). Learned from the API contracts; no external implementation copied.

## What the ONE candle run provides

`1-vfx-off` and `2-vfx-traced` (Denoising None) each record four live facing frames, eight live turned-away frames, four live returned frames, then four frozen facing frames. Each phase also has an excluded primer. The camera pose is applied before HDRP culling and restored in finally. Time/freeze state is restored after each diagnostic step. `3-off` restores PT Off. No extra VFX simulation, pause toggle on a system, forced culling override, or effect draw is issued.

Each phase folder contains:

- `frame-N.json`: schema 2, program hashes and ordered dispatch/draw/trace events. `VS.srvs` slot 0 of a candle draw identifies its attribute allocation; t1 is the output list, t2 the graph values. A compute `CS.uavs` entry with that same lifetime ID is a potential writer. Binding alone does not prove which byte ranges its instructions write.
- `frame-N-image.json`: screenshot/native acknowledgement, camera matrices, world and unscaled clocks, HDRP CPU shader-clock mirror, exposure/gain/tracer state, and **all loaded VFX components**, including inactive/off-camera components. Public culling, renderer visibility/enabled state, per-system sleeping/counts/capacity and asset identity are preserved. Negative counts stay negative. Compiled graph culling policy is unavailable; buffer-to-component mapping stays explicitly unknown. Neither the CPU clocks nor the frozen public counts prove GPU attributes/custom clocks froze.
- `frame-N.raw` and analyzer-produced full-size `frame-N.jpg`. `vfx-overview.jpg` gives the first accepted view from every phase. Read it to verify the camera turn actually hid the known candles; no fabricated position overlay is supplied.

`vfx-summary.json` has `resources` (address/size/stride/roles/writer hashes) and `frames[].buffers` (every writer and draw reader's sequence). Compare **the same lifetime ID** through facing/away/returned; do not join by pointer address or size. `submittedWithoutDraw` in the live-away phase establishes that a UAV-bound dispatch was submitted for a known allocation despite no draw reading it. With direct positive group counts and no predication that is useful off-view submission evidence. With indirect groups it remains unresolved until arguments are read: zero groups are possible. A batched allocation does not prove every contained system was updated. The public culling inventory makes the remaining mapping question visible instead of guessing it away.

On samples have `traceSequences`; each write's `beforeTrace` compares submission order to the actual event-1 callback. It is CPU/D3D11 submission order, not GPU timestamps or proof the D3D12 trace succeeded. Off samples must have no trace marker. If writes are after the marker, the future capture needs an earlier simulation boundary or a moved trace schedule, not stale previous-view particles.

`unmappedWriterAllocations` lists other buffers bound to the same CS program as a known attribute writer. These can reveal never-drawn candidates, but are not silently classified as candles. No known candle attribute allocation, no linked writer, refused hooks, unmatched screenshots, missing phases, stale/incomplete observations or failed PT activation makes analysis exit nonzero. A missing writer can mean initialization-only data, CPU updates/copies, changed output bytecode or incomplete observation; it is not evidence that GPU capture is impossible.

This run selects the next buffer/compute boundary. It cannot certify all-loaded-system capture, shader-write coverage, indirect execution counts or particle identity. Those need the next worker's compute decode/readback and real transfer test. Keep PT-71 item 7 open.

## Validation (diagnostic)

- Native `TGPathTracing.Native` and `tgpt_gpu_test` build successfully in `%TEMP%/tgb/vfx-build`, SDK roots read from the main tree, `TGPT_WITH_NRD=OFF`. Log: `build/vfx/native-final-build.log`. The only compiler warning in the full GPU-test target is the pre-existing double-to-float conversion; observer compilation is clean. The vcvars helper prints that vswhere is not on PATH, then successfully configures the installed compiler.
- Installed only the built TGPT DLL using the TGPT `file(INSTALL ...)` instruction from CMake into `%TEMP%/tgb/vfx-install`; CMakeLists is unchanged. Other required **object-code** runtimes are staged from existing local artifacts. No restricted SDK source copied. An initial ctest attempt skipped because that scratch stage was absent; the skip was NOT counted as a pass. Correctly staged full tests then passed: 234.94 s, and **322.02 s on the final native build**. Logs: `build/vfx/gpu-regression.log`, `build/vfx/gpu-regression-final.log`, `%TEMP%/tgb/vfx-build/Testing/Temporary/LastTest.log`. Concurrent workers mean these are not performance measurements. Offline tests do not run RR; this NRD-OFF build is for worker validation, not the coordinator's normal deployment.
- Final native SHA-256 (build, scratch test install and this copy's artifact agree): `9D5BDAFC1E444BE8EE17FA30EF6CD5A57304D5B94631A69B23AD96EA56247D11`. No game-folder hash or deployment claim.
- `tests/vfx_observer_test.cpp`: **41 real D3D11/MinHook checks pass**, `build/vfx/observer-test-final.log`. Includes SHA-256 known answer, VS/PS/CS creation tags, off capture, immediate and deferred buffer links, a dispatch without a draw, a GPU pipeline-statistics query proving exactly three forwarded vertices, unchanged UAV binding, additive GPU values proving no dispatch replay, reused lists, constant-buffer ranges, FinishCommandList(FALSE), empty lists, and explicitly rejected lists recorded outside capture. JSON is in `%TEMP%/tgb/vfx-build/observer-evidence`; it uses an explicitly tagged **synthetic candle shader**, not shipped candle particles. `build/vfx/native-json-analysis.json` independently parses its actual native output through the production analyzer and checks resource linkage, trace ordering, indirect unknowns and separate undrawn candidates.
- Three separately compiled GPU mutants fail as intended: omit dispatch observation (`observer-mutant.log`); omit deferred UAV tracking (`observer-mutant-uav.log`); forward zero draw vertices (`observer-mutant-draw.log`). Failures name the missing event, missing resource identity and incorrect GPU vertex count. Mutants live only in the private build folder; production files were never overwritten. They stop at the first affected assertion, without rerunning full regression suites.
- `python tests/harness_vfx_test.py`: **10/10**. Exact independent lifetime IDs and reused addresses, writer/read links, off-view submission, trace order, indirect unknowns, missing/stale/error observations, primers, CB-only false matches, and screenshot row/channel orientation. Six analyzer mutants fail: missing links, reversed order, invented indirect certainty, ignored stale lists, misclassified constant buffers, inverted screenshot rows. `build/vfx/analyzer-mutations.log` and per-mutant logs. Original module untouched.
- `python tests/source_checks.py`: **90/90**, `build/vfx/source-checks-final.log`. PowerShell launcher parses without errors.
- Harness and Mono TGPT managed builds: **zero warnings/errors**, `build/vfx/harness-final-build.log`, `build/vfx/managed-final-build.log`. No package downloaded: the existing offline feed/reference cache was used, with dotnet/NuGet/temp paths inside the copy. The plugin's first restore tried the multi-target dependencies and reported missing offline .NET runtime references; `-p:TargetFrameworks=net472` fixed that by restoring only the intended Mono target. `DeployToGame=false`. The unchanged plugin is a compile check; the new managed implementation is in the harness.

Reproduce the standalone hook test in the VS x64 shell (repo root; the main tree's existing MinHook include/library are read-only):

```bat
cl /nologo /std:c++17 /EHsc /O2 /MT /W4 /I native\bridge\include /I C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\build\native\_deps\minhook-src\include tests\vfx_observer_test.cpp /FoC:\Users\Frede\AppData\Local\Temp\tgb\vfx-build\vfx_observer_test.obj /FeC:\Users\Frede\AppData\Local\Temp\tgb\vfx-build\vfx_observer_test.exe /link d3d11.lib dxgi.lib d3dcompiler.lib C:\Users\Frede\Developer\TGFOA\TGFrameGeneration\build\native\_deps\minhook-build\minhook.x64.lib
```

After the usual game/RUNNING/GpuCheck gate, run `vfx_observer_test.exe <private-output-folder>`. Existing worker scripts `build/vfx/run-observer-test.ps1` and `run-regression.ps1` implement that gate and wait in one process. They are scratch evidence, not files to merge.

Changed/new source files against base41 (13 total): `extension.cpp`, `vfx_observer.hpp`, `vfx_programs.hpp`; harness `Request.cs`, `Runner.cs`, `VfxCapture.cs`; `Run-PtHarness.ps1`, `vfx_capture.py`, `vfx_shader_audit.py`; `scenarios.json`, `harness_vfx_test.py`, `vfx_observer_test.cpp`; this report. The audit/header are hash metadata only. `build/vfx/changed-files.txt` has full relative paths. No game shader bytecode, disassembly, decompiled Unity code, binary or SDK source is part of the merge.

No game run was performed. All GPU starts used the main harness's **GpuCheck only**, after checking FoA was closed and the RUNNING lock absent/stale. Other workers' tests were not stopped. Outputs are confined to this tree and `%TEMP%/tgb/vfx-*`. Coordinator-owned memory, bridge and other-mod source files are unchanged.
