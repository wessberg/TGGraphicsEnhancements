# fog-froxel: PT-118 / PT-37 / PT-112

Worker started 2026-10-11 15:34 CEST in `wt/fogfroxel`, base `base20fog`.
Only this copy and `C:/Users/Frede/AppData/Local/Temp/tgb/fogfroxel-*` may be written.
No deployment, game start, git mutation, download, or install. No GPU fixture before the game's first answer.

## Current handoff: continuation built 17:29 CEST

The integration cost is down in the guarded small GPU check: **9.73 -> 0.55 ms**, same raw frame mean.
With a box intersecting camera rays: **9.76 -> 0.74 ms**, raw mean difference 1.25e-7. The game's clean baseline
is 9.84-10.54 ms; a new tavern measurement is still owed. No game launch or deployment by this worker.

The complete requested continuation is now built: exact box-miss culling and shared grid reads, 1-6 stability
samples, adaptive Gaussian/history filter, and a separate history that retains the firefly excess.
No new export, public record, FrameConstants row, option bit, frame flag or player setting. Existing flag 19.
`pt_fog_filter` is a new isolated pass; its `fog filter` timestamp sits between `fog cells` and `fog integrate`.
Three additional RGBA32F volumes (two excess histories, one filtered field), **18.08 MiB at 1924x1084**,
are accounted and released on disable/resize/teardown. The eight existing root constants carry their indices.
No large surface shader changed. Workarounds removed: none; the old route stays until the owner's acceptance.

| Build in `C:/Users/Frede/AppData/Local/Temp/tgb/fogfroxel-build/` | SHA-256 |
| --- | --- |
| `TGPathTracing.Native.dll` ? all requested continuation pieces | 7C3CFA54CF5124B5523F0044D386F35866FB2A2C247CCD9F10AAA446FB9DA79D |
| `integration-tile.dll` ? measured integration fix alone | 6222F8DD8AC1F6F54ECA254C6CC0E603B5D4881AC17B484ABA6342CCF52B2954 |

Keep the coordinator's current managed plugin. Use **`tavern-room@fog-froxel-b`**, one clean `-Measure` run.
Main has advanced: merge the source delta and rebuild main before installing if its other new work is needed;
these binaries are from the worker's older base. No packaging/bridge replacement is involved.

Merge files: `worker.patch` = `continuation-from-1638.patch`, with `continuation-changed-files.txt`.
These are **deltas against the merged 16:38 part (b) source**, saved byte-for-byte in `checkpoint-1638`, not
against `base20fog`. `integration-from-1638.patch` is the one-shader integration fix alone. Old patches named
`part-b-from-base20fog.patch` are historical; do not apply them as this continuation. Shared docs need a three-way
merge of worker lines. New file in this continuation: `native/pathtracing/shaders/pt_fog_filter.hlsl`.

Build logs: `shaders-filter.log` (five objects, zero failures; 16 minutes waiting for the priority guard),
`native-filter.log` (both native targets built; no new warning). **The complete continuation passed its fog-only
GPU check at 17:33**, after the main game closed; no recorded GPU contention. `fog-final-b.log`,
`usage-final-b.jsonl`, `hash-final-b.txt`, `images-final-b/` and `final-comparison.json` are the evidence.
All 116 source checks pass (`source-checks-final.log`); the 6,000-case temporal proposal oracle still passes
(worst relative error 8.65e-16). No whole GPU suite, ctest, game launch, deployment or additional mutation run.
The new Gaussian filter/excess path is executed and finite; its visual quality and moving-light response still
need the game. The retained-excess identity is justified algebraically, not claimed as a separate GPU oracle.

| Fog piece, same 1920x1080 intersecting-box fixture | Merged 16:38 | Integration fix only | Full continuation |
| --- | ---: | ---: | ---: |
| Cells, including reuse | 0.16 ms | 0.16 ms | 0.18 ms |
| Adaptive filter | absent | absent | 0.08 ms |
| Camera integration | 9.76 ms | 0.74 ms | 0.76 ms |
| Distant tail | 0.05 ms | 0.05 ms | 0.05 ms |
| Sum | 9.97 ms | 0.95 ms | 1.07 ms |

These are steady-history times. The first cell frame uses six reservoir samples and settles to one by age 64;
its transient cost has not been separately measured. No quality-reducing sample/grid change bought the integration
speedup. The full raw mean is 0.0755004233 -> 0.07550052; compare `final-comparison.json` for all digits and pixel
errors. This is an undenoised synthetic check, **not** a new in-game measurement or an owner acceptance.

The table below in the historical 16:35 handoff is superseded. Remaining overall gaps: directional radiance
history, the game's motion/mirror/shaft verdict, and part (c) cleanup after acceptance. Current-main Remix bodies
remain unpublished; historical full bodies and the requested settings were read. Documented deviations below.

## Cost-fix checkpoint, 17:07 CEST (built; next game run owed)

The seven-box synthetic scene reproduced the integration defect: **9.87 -> 1.09 ms** after testing box
intersections once per camera ray. Cells stayed 0.16 ms; tail stayed 0.05 ms. The 16-frame undenoised mean is
0.07550970 before and after (whole composed linear frame). Both fog-only runs passed. They passed the initial
main GPU check and recorded no other game/test opening; the inherited guard did not continuously sample other
applications, so these are diagnostic timings to confirm in the game's clean run, not a claimed game speedup.
The guard now also samples all processes' GPU counters, using the main harness's thresholds.

Smallest cost-fix DLL: `fogfroxel-build/integration-cull.dll` (SHA256 8D7C341B6E716230A33218368868581FB2DB9844A9750F25F5F2A6F671D74FF5).
Keep main's plugin and recipe `tavern-room@fog-froxel-b`. This worker cannot deploy or start the game.
An additional shared-memory texture-read trial is built and under its own small check; use the final handoff
below when available. All camera samples, density maths and tail/secondary estimators remain present.
The table at the beginning of the old report is superseded by this continuation's final table.

## Continuation source audit and decisions

Read the complete historical Remix 0.4 integrator (313 lines), filter (121 lines), preintegrator (152 lines),
and preintegrator dispatch (75 lines), pinned at `74cadc6febf55b6737e5a860c64174795840bbee`. Each file has the MIT
license header; no code was copied. This closes the historical filter/integrator body gap, not the missing
current-main source gap. References:

- [light integration, 221-313](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/74cadc6febf55b6737e5a860c64174795840bbee/src/dxvk/shaders/rtx/algorithm/volume_integrator.slangh#L221-L313):
  stability-driven reservoir count, visibility, history and clamp.
- [adaptive filter, 45-120](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/74cadc6febf55b6737e5a860c64174795840bbee/src/dxvk/shaders/rtx/pass/volumetrics/volume_filter.comp.slang#L45-L120):
  radius in XY, one neighbouring slice each way in Z, Gaussian weights and relative/absolute history weights.
- [preintegration, 109-150](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/74cadc6febf55b6737e5a860c64174795840bbee/src/dxvk/shaders/rtx/pass/volumetrics/volume_preintegrate.comp.slang#L109-L150)
  and [2D dispatch, 56-73](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/74cadc6febf55b6737e5a860c64174795840bbee/src/dxvk/rtx_render/rtx_volume_preintegrate.cpp#L56-L73).

`volume_integrate` is light integration at a cell. Historical `volume_preintegrate` separately marches each grid
column and stores cumulative radiance. It explicitly assumes homogeneous media. A direct translation here would
interpolate *integrated medium* across neighbouring rays, changing the picture at thin masked boxes, height
changes and geometry edges. Exact transmittance alone would not fix its altered scattering. Under the explicit
"without changing what the picture converges to" instruction, I did **not** replace our per-ray medium by that
coarse prefix. The measured equivalent-work fix instead eliminates repeated box misses and shares the same grid
texels across an 8x8 pixel group. It keeps all 48 slices and scattering samples. Shared interpolation evaluates
the same continuous trilinear field in float arithmetic (hardware interpolation can differ in subtexel rounding).
There is a texture fallback for projected positions outside the group's conservative footprint; no cell is omitted.

The decisive monitored repeat of the old shader is `b1638-monitored`: integration **9.73 ms**, cells 0.16,
tail 0.06. The shared-read build `tile-boxes` gives integration **0.55 ms**, cells 0.16, tail 0.05. Both have the
same raw frame mean 0.07550970. Neither monitored run recorded external GPU contention. These are the small
1920x1080 synthetic case, not new tavern measurements. The original tavern baseline remains 9.84-10.54 ms.

The remaining part (b) implementation uses the requested 1.4 settings and the historical filter's formulas:
1-6 independent corrected reservoir estimates per cell (eight fresh proposals each), selected-light visibility,
history 1-64 with power 2; Gaussian sigma 1.2, XY radius 4 down to 2 and Z radius 1, with history weighting.
Visibility-weighted reservoir selection guides the next frame without changing the current average. Existing
MIS correction is retained, instead of importing Remix's documented biased temporal weights.

Fireflies: split each physical, unexposed, phase-weighted cell estimate into a luminance-bounded part (1000) and
its exact nonnegative excess. Accumulate both with the same age/reprojection/reference-scale conversion. Filter
only the bounded history, then add the excess at its original cell. Algebraically bounded + excess = original;
no energy is discarded. Spatial filtering itself redistributes the bounded component, as explicitly requested;
it is not claimed to preserve every pixel of the unfiltered field. Histories remain linear, rather than Remix's
historical gamma-space interpolation, to avoid a nonlinear mean shift. Those are documented departures under
PT-127, justified by the owner's retained-light rule. No claim of byte-for-byte NVIDIA shader parity.

17:10: the new cells/filter shaders are queued behind the coordinator's `BUILD-FIRST` guard. No override used.
The integration-only DLL is already built and checked: `fogfroxel-build/integration-tile.dll`, SHA256
`6222F8DD8AC1F6F54ECA254C6CC0E603B5D4881AC17B484ABA6342CCF52B2954`. Keep main's current plugin; flag 19 and the existing recipe are sufficient.

The intersecting-box regression also passed (`b1638-intersect2` / `tile-intersect`, guarded and monitored):
integration 9.76 -> 0.74 ms. Both keep one coloured, faded box crossing the camera rays. Raw 16-frame linear
means 0.0755004233 -> 0.0755005481 (difference 1.25e-7); average absolute difference 7.57e-6, p99 5.72e-6.
Maximum difference 0.0161 is at a stochastic sample, not a bound on each individual pixel. No brightness or
per-pixel identity guarantee is inferred from the whole-frame mean. Both raw `.f32` means are saved beside
these runs' PNGs. No denoiser was used. One earlier intersecting-box attempt stopped its own test immediately
when Starfield appeared and rendered no frames; its timing is excluded.

17:21: all 116 source checks and the proposal oracle pass. The complete part (b) shader build remains queued
behind main's priority build. The checked integration-only build remains ready; there is no worker GPU test running.

## Continuation measurements / limits

17:02: the first fog-only check finally rendered after cold driver startup and passed finite/nonnegative/frame
completion checks. **All its timings are invalid**: the inherited guard recorded Starfield opening during
startup but did not stop our test. Fixed that worker-local guard to stop only its own test when any owner game
or other worker GPU test opens. No process of the owner was stopped. Do not use `fog-b1638.log` for speed.
No further GPU work starts until the main guard permits. The build continues on the CPU.

## Continuation plan, 2026-10-11 ~17:00

The clean game run `20261011-164811` reports no contention or lost foreground. Its final native stage lines
confirm: old fog integrate 3.00-3.03 ms; grid cells 0.61-0.68, integrate 9.84-10.54, tail 0.06 ms.
The NRD improvement (flicker 0.39 -> 0.21, boil 0.23 -> 0.12) and unchanged brightness are kept.

1. Preserve the merged 16:38 source checkpoint and binary in the worker build; all new patches use that base.
2. Read Remix's actual integration/lookup code. Verify the proposed pre-integrated volume; do not assume that
   a pass called integrate integrates camera depth. Inspect our density/box workload and texture accesses.
3. Replace repeated camera work with shared grid work where mathematically equivalent. Preserve exact
   transmittance, the continuous cache reconstruction, and all distant/secondary fog. Build the smallest
   cost fix for the coordinator's next `fog-froxel-b` game run; record separate stage costs if GPU guard allows.
4. Then complete the requested stability samples/filter and firefly handling with retained excess. Document
   any difference from NVIDIA and the no-light-loss proof. Do not remove the old route before acceptance.

The previous checkpoint is saved in `fogfroxel-build/checkpoint-1638`; no main-tree files were written.

## Part (b), first game checkpoint: built 16:35 CEST

Use `tavern-room@fog-froxel-b`: switch off/on with NRD first, then None, 16 frames per traced take.
Native DLL: `C:/Users/Frede/AppData/Local/Temp/tgb/fogfroxel-build/TGPathTracing.Native.dll`.
SHA-256: `72436C931A52745F4B1321E00D04CDB10671DA2B23839E47BA138D2BACF40D68`.
The part (a) plugin switch is unchanged; **keep the coordinator's current plugin** (it already knows flag 19).
Do not replace it with this copy's older plugin just for this native change. No deployment by this worker.

Built: separate tail pass, cheaper homogeneous-medium integration, corrected temporal reservoir proposals,
and separate GPU stage times. **Not measured on the GPU or accepted in game.** The small fog check could not
get past cold driver startup before the game opened; its guard stopped it. No full GPU suite was run.
All 116 source checks pass. The 6,000-case numerical proposal test preserves the exact light sum (worst relative
error `8.65e-16`); that checks the weights, not executed shader code or the game's picture.
Removing the fresh-proposal correction in memory makes that oracle fail on its first nonzero-history case;
the source file was never changed for the mutation.

Handoff files in `fogfroxel-build`: `part-b-from-base20fog.patch` and `part-b-changed-files.txt` (25 files,
**including the already merged part (a)**; do a three-way merge against `base20fog`, never overwrite main's
shared files). Logs: `shaders-split.log`, `shaders-reuse.log`, `shaders-fast.log`, `native-b.log`,
`source-checks-b.log`, `fog-math.log`, `fog-math-mutation.log`. GPU startup attempts: `fog-baseline.log`,
`fog-a2.log`, `fog-a3.log` and their staged native logs; none contains rendered fog measurements.
The fog-only fixture is `TGPT_TEST_FOG_FROXEL_ONLY=1` (1920x1080, 160 frames per route, 16-frame raw mean;
returns immediately after its own checks). It is built, not passed. Its executable SHA-256 is
`6EB32CD8EAD1413172775482766AE6CDDEA679E0C93ADBA2799171376A8274CB`.

| Piece | What changed / cost evidence | GPU cost |
| --- | --- | --- |
| Cell lighting + reuse | Same /16 x 48 grid and eight fresh candidates, one reused proposal; still a fresh selected-light shadow ray. Two uint4 history volumes add 12.05 MiB at 1924x1084. | `fog cells`; unmeasured |
| Camera integration | Same samples/slices and medium, UV hoisted, homogeneous branch simplified. Compiled SM 6.9 IR: 21,275 -> 2,099 instruction lines; ray-query calls 285 -> 0, allocas 21 -> 0. | `fog integrate`; unmeasured |
| Beyond 20 m | Original free-flight + equiangular estimators, moved to `pt_fog_tail`. Pixels ending inside the grid return immediately. | `fog tail`; unmeasured |
| Old route | Preserved for the owner's comparison; its trace/filter time appears under `fog integrate`, other fog stages empty. | Also needs the clean game run |

The compile-size reduction is **not** a claimed timing reduction. The old timing was combined with composite;
compare the sum of the three new fog stages plus composite against the previous composite total. Reading only
the new `composite` figure would falsely report a speedup. For timing, the coordinator must use `-Measure` and a
run without competing GPU use. Check brightness/exposure/gain, the shafts, a camera strafe and disocclusion.

The grid still ends at **20 m of camera view depth**, not radial distance. After that plane the full remaining
medium is traced to the real hit (or the game's own maximum fog distance for height fog over sky; fog boxes
keep their existing separate reach). No transmittance threshold ends a ray. Mirrors and refraction still use
`traced_fog_on_way` along their own paths, including off-screen fog; this camera grid is never sampled as a
substitute for their light. Their existing radiance-cache approximation is unchanged. The split adds an
intermediate half-float write before distant fog; this can change last-bit rounding, not the transport estimator.

No new exported function, public ABI record, FrameConstants row, option bit, native frame bit or player setting.
Flag 19 and the existing hidden boolean still select the whole trial. Internal uint4 reservoir: top-level light
index, identity, confidence sample count, reserved zero (16 bytes). Eight root constants now pass the two private
reservoir descriptors too, with no root-signature change. New pass: `pt_fog_tail`; new include: `pt_fog_reservoir`.
Memory release/accounting covers all four grids, including resize/switch-off. Scene-clear resets now use the scene's
actual generation even when the world radiance cache is off. No large surface shader or secondary-ray code changed.

Remaining (b), deliberately after this requested checkpoint: 1-6 stability-driven reservoir samples, adaptive
Gaussian filter, directional radiance history, and firefly handling that retains rather than discards the excess.
Part (c) still requires the owner's acceptance; no old-route workaround was removed in this checkpoint.

Other finding: **PT-128**, the existing reflection-sort chooser measures `t[6]-t[5]` (direct lighting) while
reflection work is between marks 6 and 7. Verified in main read-only too; not caused or fixed by this patch.

## Part (b) plan / active checkpoint (2026-10-11, resumed after the tavern run)

The coordinator supplied `artifacts/harness/20261011-161034` (main tree, read only).
I read its report and overview. NRD flicker 0.38 -> 0.21, boil 0.23 -> 0.12, luma 16.5 -> 16.3;
None luma 18.1 -> 18.1. Its timing cannot establish a speed result (window behind Code; coordinator also
reports a concurrent GPU test). The reported composite increase, about 4 -> 12 ms, is a defect under PT-118.

1. Split GPU timestamps around the cell pass, reconstruction and distant tail. Preserve a baseline binary.
2. Audit current Remix reservoir/integration/filter sources and defaults; no downloads or source copying.
3. Remove repeated work while preserving the existing cache's expected image. In particular inspect the
   per-pixel 48-segment medium walk; do not silently coarsen the grid, cut rays off, or discard faint light.
4. Add temporal cell reservoirs with current visibility and light-identity validation as the first (b) piece.
   Inspect stability-driven sampling/filtering and firefly handling; document any conflict with the no-light-loss
   rule and preserve any clamped residual. Mirrors/refraction keep tracing the real medium beyond the cache.
5. Build a native trial for one coordinator tavern comparison (old/new NRD, then None), with isolated cost
   counters. Run only a small fog check if the main GPU guard permits; never start/deploy the game.

No change to the /16, 48-slice, 20 m cache is assumed. This is a continuation of part (a); the old route stays
until the owner accepts the grid. Write and build paths remain strictly this copy and `tgb/fogfroxel-*`.

16:27 checkpoint: split the ray-query tail out of the per-pixel slice loop, hoist the ray's grid UV, and add
three GPU timing labels (`fog cells`, `fog integrate`, `fog tail`). The first baseline test timed out during
cold driver pipeline creation at 180 s, before a frame ran. A retry uses copied **object-code** PSO caches
from main's offline build in the worker build (main is read only); any concurrent-test timings are invalid.

The first reuse piece is coded and built: every cell retains its selected top-level lamp/emitter cluster and
a count capped at 200; clamped reprojection halves that count. An old selection is an extra proposal balanced
against eight new light-list candidates, with a fresh area point and current shadow ray. Its MIS denominator
uses the actual near/far-list probability. This keeps the mean for ANY previous selection, including after
lights or blockers move. It intentionally does **not** import biased history weights or old visibility.
This is a documented deviation from Remix's uncorrected temporal resampling, required by the owner's
no-lost-light rule; do not describe it as the exact Remix shader implementation. Full Remix filter/clamp
parity is still pending; this is the first (b) piece for the requested next game checkpoint.

Source audit correction: at live main commit `3a75b814600bdf7ef7b0d3a0bae55b9a8c81f34b`, GitHub's complete tree
contains `volume_integrator.h` (a 28-line placeholder), but neither `volume_integrator.slangh` nor
`volume_restir.slangh`, although the public passes include them. The initial claim that a rename solved the
access issue was wrong. The files are absent, not merely a browser failure. The public dispatch, bindings,
helpers and full settings page were read in memory; no source was saved or copied. Runtime cpp lines 679-741
dispatch the reservoir grid's initial, visibility, temporal and spatial passes and then all radiance cells
each frame: it does not visibly skip a fixed fraction each frame. Header lines 83-123 give /8 x 64 radiance,
another /4 x 128 reservoir grid, 32 fresh candidates and history cap 2. We keep the requested 1.4 grid/counts
for the comparison rather than silently changing spatial resolution. These missing internals remain an audit
gap under PT-127, not a claim that the independent implementation follows unread code.

Pinned public references: [runtime defaults, lines 81-123](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/3a75b814600bdf7ef7b0d3a0bae55b9a8c81f34b/src/dxvk/rtx_render/rtx_global_volumetrics.h#L81-L123),
[per-frame dispatch, lines 679-741](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/3a75b814600bdf7ef7b0d3a0bae55b9a8c81f34b/src/dxvk/rtx_render/rtx_global_volumetrics.cpp#L679-L741),
[reservoir bindings, lines 28-35](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/3a75b814600bdf7ef7b0d3a0bae55b9a8c81f34b/src/dxvk/shaders/rtx/pass/volumetrics/volume_restir_bindings.slangh#L28-L35),
[the placeholder integrator header](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/3a75b814600bdf7ef7b0d3a0bae55b9a8c81f34b/src/dxvk/shaders/rtx/algorithm/volume_integrator.h),
and [the wrapper that includes the missing body, line 29](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/3a75b814600bdf7ef7b0d3a0bae55b9a8c81f34b/src/dxvk/shaders/rtx/pass/volumetrics/volume_integrate.slangh#L29).

## Ready for the coordinator's first game run

Part (a) built at **16:04 CEST**, 30 minutes after the worker started. Default remains off. Run
`tavern-room@fog-froxel` using this copy's managed and native DLL together and its new scenario recipe.
The recipe compares old/new fog with None first, then NRD, and returns the switch to off. Check the route log,
the 16-frame means, exposure/gain, thin light shafts, and movement after the frozen comparison. This worker
has not deployed anything or run the game; there is no visual or speed verdict yet. Parts (b)/(c) await that verdict.

| Built file | SHA-256 |
| --- | --- |
| `C:/Users/Frede/AppData/Local/Temp/tgb/fogfroxel-build/TGPathTracing.Native.dll` | `886433EBD655BAAF2B970A9B45FE7E0DE9C1F1463D3D8040D31AC453024D3A9D` |
| `C:/Users/Frede/AppData/Local/Temp/tg-agents/wt/fogfroxel/src/TGPathTracing/bin/Release/net472/TGPathTracing.dll` | `E36DE60E8048281EF6C4564B17721A497535B6994DBF48C89CCB90E61E3B79E6` |
| `C:/Users/Frede/AppData/Local/Temp/tgb/fogfroxel-build/tgpt_gpu_test.exe` (built, not run) | `B9E8FF19673CF378AC2E9AC05EACE8B6271848C8C0067FA671FCFB88469F3194` |

Build and validation logs are in `C:/Users/Frede/AppData/Local/Temp/tgb/fogfroxel-build/`:

- `changed-files.txt` and `worker.patch`: 19 changed/new files against `base20fog`, for review and transfer.
  The large surface shaders (`pt_gi`, `pt_specular`, `pt_prepare`, `pt_walk`), `pt_common`, `pt_medium`, old `pt_fog`
  and the GPU fixture are byte-identical to the base. Merge only this worker's lines in shared harness documents.
- `configure.log` / `configure.cmd`: own build folder, main SDKs read-only, prebuilt matching NRD and ShaderMakeBlob.
- `shaders-new.log`: the four new SM 6.6/6.9 shader objects compiled, no failures. The helper ran without `-First`.
  `shader-reuse-audit.txt` and `shader-reuse-hashes.txt` document reuse of 46 unchanged object-code headers after
  checking compiler commands, all input contents and freshness. Nothing was installed or fetched from the network.
- `native.log` / `build-native.cmd`: TGPathTracing.Native and tgpt_gpu_test built. The DLL has no compiler warnings.
  The unchanged GPU fixture has four C4244 double-to-float warnings (source lines 2742 and 5889).
- `managed.log`: net472 plugin built, zero warnings/errors. Restore used two already-cached framework-package
  archives copied to the worker's local feed; no download. TEMP, CLI home and HTTP cache stayed inside the own build.
- `source-checks.log`: **116/116 passed**. `recipe-mutation.log`: changing the new recipe's initial Mode from Off to
  On made its source check fail specifically for `fog-froxel`; the original JSON was then restored byte for byte.
  This checks recipe wiring, not shader execution or the picture. No GPU fixture, full suite or ctest run.

## Plan (first checkpoint)

1. Read the owner rulings, handoff, fog implementation and NVIDIA's volumetrics settings and runtime sources.
2. Build the smallest switchable camera fog cache: render size / 16, 48 frustum depth slices, squared spacing;
   traced sun, sky and lamp/emitter RIS per cell, world-point reprojection and up to 254 accumulated frames.
   Keep the existing height fog integral and fog-box ratio tracking. Integrate light along each camera ray's cells.
   Continue tracing beyond the cache; its reach must never remove distant fog. Secondary rays stay in pt_medium.
3. Native frame flag bit 19 (`TGPT_FRAME_FOG_FROXELS`), a hidden Advanced boolean, a tavern-room A/B recipe
   (old/new without a denoiser first, then old/new NRD). No exported function, ABI record or FrameConstants change
   is intended. Put new shader work in separate passes, never the large surface passes.
4. Build native + managed for one coordinator run within the first hour, or record the exact blocker here.
5. After the coordinator's game evidence: reservoir reuse and the documented stability filter/clamp; then remove
   the old route only once the owner accepts the result. This worker cannot supply that game evidence itself.

## Decisions / source audit

- The requested 1.4 documentation and current Remix `main` differ: current runtime defaults include a separate
  reservoir grid. The first prototype follows the specifically requested documented grid; differences will be listed.
- Read license first: repository LICENSE is zlib; `rtx_global_volumetrics.cpp` and `.h` carry MIT notices.
  No Remix code copied. No RTXDI/NRD source copied or quoted.
- Temporal radiance accumulation is part (a); temporal *reservoir* reuse, adaptive filtering and a firefly clamp
  are part (b), deliberately after the game judges the direction. Coarse cache reconstruction is an approximation
  explicitly requested for this trial, not a claim of exact per-ray radiance.

### NVIDIA references (read online; nothing downloaded into the tree)

- [Remix 1.4 Volumetrics settings](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.4.0-0/docs/runtimeinterface/renderingtab/remix-runtimeinterface-rendering-volumetrics.html),
  all settings read: /16, 48 slices, exponent 2, 8 RIS candidates, initial visibility, 254-frame accumulation.
  Part (b): temporal reservoirs capped at 200 samples, clamped-reprojection confidence 0.5, 1-6 reservoir samples,
  Gaussian sigma 1.2, radius 4 down to 2 over history 1-64 with power 2, firefly threshold 1000. The documentation
  itself calls out temporal reuse bias. These are a reference for the next stage, not features claimed in part (a).
- [Runtime header, lines 75-121](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/rtx_render/rtx_global_volumetrics.h#L75-L121):
  current main differs from that documentation: /8, 64 slices, 128 accumulated frames, 20 **metres** of reach,
  32 initial candidates, a separate reservoir grid and a temporal sample cap of 2. The requested 1.4 defaults win
  for this trial's counts. Reach uses the runtime's explicit metric value, not the documentation's unscaled
  2000 world units; TG's world is in metres. The old estimator handles everything beyond it.
- [Runtime dispatch, lines 593-665](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/rtx_render/rtx_global_volumetrics.cpp#L593-L665)
  separates reservoir initialization, visibility, temporal/spatial reuse, and radiance integration. Allocation at
  lines 714-749 uses ping-pong 3D textures and directional radiance coefficients. Our first stage has two own
  compute passes; it does not change the existing SER surface passes.
- [Integration pass, lines 60-126](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/shaders/rtx/pass/volumetrics/volume_integrate.slangh#L60-L126):
  reproject the cell centre through unjittered cameras, then jitter the current lighting point within the cell.
  The wrapper [lines 21-48](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/shaders/rtx/pass/volumetrics/volume_integrate.comp.slang#L21-L48)
  is a separate compute pass.
- [Frustum mapping and reprojection](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/src/dxvk/shaders/rtx/utility/froxel.slangh#L1-L256),
  `depthSliceToViewZ`, `froxelCoordinateToTranslatedWorldPosition`, `froxelCoordinateToPrev`: use camera-relative
  positions, nonlinear view depth, cell centres and bounds checking. Our existing prevViewProj already includes
  the camera translation. Our cache starts at the eye instead of the near plane, retaining fog before that plane.
- License check: [repository LICENSE](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/LICENSE) is zlib;
  the source files above each have an MIT header (lines 1-20). No code was copied. Our light sampling is an
  independent adaptation of **our own** `fog_light`, `light_lists`, `lists_candidate` and `load_light`.

Source-access limitation: repeated online reads of the included `rtx/algorithm/volume_integrator.slangh` and the
reservoir pass bodies returned 404/cache failures. The available pass and runtime were read, but the complete
reservoir/filter internals were **not** inspected. Finish that source audit before implementing part (b); this
report does not claim a complete Remix integration or complete vendor audit.

### What the first build does

`pt_fog_froxel` traces sun/sky visibility and chooses a lamp or emissive triangle from 8 RIS proposals. A random
near/far-list stratum is drawn for each proposal with its existing inverse probability; far lights are never
omitted to fit the count. It samples the existing multiple-scattering fog cache and feeds it through free flight.
The radiance history is trilinearly reprojected by the cell's world point. It resets for cuts, frame gaps, scene
clear, resize, switching routes, and switching scattering order; stored light follows reference-radiance changes.

`pt_fog_integrate` walks every intersected depth slice up to the hit. Each segment uses `fog_scatter_point` for
its medium-weighted light sample. Height extinction stays analytic; local boxes retain ratio tracking and their
actual 3D masks. Independent segment transmittances multiply to the same expected whole-ray transmittance.
After the final cache plane, the original free-flight plus equiangular-lamp estimators continue over the remaining
ray. No early exit drops light because transmittance is small. Mirrors/refraction stay in `traced_fog_on_way`.

The cache holds light before extinction/albedo, so its cell size never voxelizes away a thin fog box. It is still
a spatial approximation of lighting: shafts narrower than a cell may blur or leak. The first cache stores the
camera-directed value, not Remix's directional coefficients; direction changes can retain stale phase response.
Light/occluder changes may lag with a long history. These are explicit trial limitations to judge, not fixed defects.
The multiple-scattering cache keeps its existing approximation and bounds; this job does not make it exact.

At 1924 x 1084 the grid is 121 x 68 x 48: two RGBA32F volumes use 12.05 MiB before allocation alignment.
The old camera targets remain for the A/B. Grid resources retire safely when the switch is off and on resize;
both the grid and the previously uncounted camera fog targets now enter diagnostics (PT-87).

### Interfaces and files

- `TGPT_Frame.flags`: bit 19, `TGPT_FRAME_FOG_FROXELS` / managed `FrameFogFroxels`. No option bit, export,
  public record, ABI version/size, FrameConstants row, shader global flag or root-signature change.
- Hidden Advanced boolean `Traced fog: froxel cache`, default false. No player row.
- New shaders `pt_froxel.hlsli`, `pt_fog_froxel.hlsl`, `pt_fog_integrate.hlsl`; included by no large surface pass.
  New native allocation/dispatch helper `fog_froxel.inl`; wiring in `tracer.cpp`, CMake and `pt_shaders.hpp.in`.
- New recipe `fog-froxel`: game's frame, old/new None, old/new NRD, Off. Four 16-frame traced takes after at least
  300 settle frames. It uses the base tree's current NRD default (trial 0), not an undocumented selector value.
- Build-only CMake option `TGPT_NRD_PREBUILT_LIBRARY` plus `TGPT_NRD_PREBUILT_DEPS`: link main's existing static
  NRD and ShaderMakeBlob libraries read-only. This avoids NRD's configure writing `Shaders/NRDConfig.hlsli` through
  the SDK junction. The normal source-build path is unchanged. Do not use this worker configuration to package.

### What remains after the first game answer

Part (b) is intentionally not coded before the first scene answers: cell reservoirs/reprojection confidence,
stability-driven reservoir count, adaptive Gaussian filter, firefly handling, and directional history. A fixed
clamp discards energy; reconcile the requested Remix threshold with the owner's no-light-loss rule explicitly
(a separately integrated residual is one candidate), rather than silently cutting bright light. Compare exposure
and gain as well as the images. The camera's coarse cache and the secondary rays' estimator need a mirror check.

Part (c) removes `pt_fog` and its old history/filter targets, switches, and tests after the owner accepts the new
route. Nothing was removed just to force the comparison to use the trial. No HDRP fog or new picture-space patch
was introduced. Workarounds removed now: none; the old route is required for the requested first A/B.

## State when stopped

16:35 CEST: the remaining shader and native relink finished. The final build/hash and honest validation boundary
are at the top of this report. The game is still open, so no further worker GPU test is started or queued.

Part (b), 16:33 CEST: offline timing is blocked by the game's run (PID 23628, observed read only).
The worker's guard killed only its own `tgpt_gpu_test` immediately when the game opened, before that test
rendered a frame. Two earlier attempts timed out during the unchanged large reflection pass's cold driver
startup. There is no successful GPU timing or visual check of (b), and no speed reduction is claimed yet.
The final SM 6.6 integration shader waits under the shared compiler/memory guard; its SM 6.9 twin compiled.
The earlier split/reuse native build exists; the final homogeneous-medium arithmetic optimisation is awaiting
that last shader and relink. No game launch, deployment, main-tree write or interference with another process.

The requested game checkpoint is the first (b) piece: corrected temporal reservoir proposals, split tail,
cheaper constant-density integration and timings. The adaptive Gaussian filter, 1-6 stability-driven samples,
directional radiance history and firefly clamp with retained excess are **not** implemented. They must not be
claimed as complete. The clamp cannot discard its excess under the owner's rule. The old per-pixel route
stays, as explicitly requested, until the owner accepts the grid.

15:45 CEST: part (a) is written and configured. Shader build started at 15:44:30, but the machine-wide
`BUILD-FIRST` guard is holding every worker compile for the coordinator's main build. No override used.
The first-hour build handoff depends on that build releasing the compiler. No game or GPU test started.

The prototype uses two RGBA32F 3D textures, /16 x /16 x 48, squared view-depth spacing, 20 metres of cache
(the current Remix runtime's value in metres). The rest of the ray is still sampled through the existing medium.
It adds no public export, record, option bit or FrameConstants row; only native frame flag bit 19 and one hidden
Advanced boolean, `Traced fog: froxel cache` (false). Recipe: `tavern-room@fog-froxel`.

15:54 CEST: managed build passed, zero warnings/errors; source checks passed 116/116. Still waiting for the shared
compiler/memory gate. To avoid rebuilding unchanged passes, verified all dependency contents, compiler commands and
object timestamps against main, then copied **object-code headers only** for 46 unchanged shaders into this build.
Rechecked inputs and hashes at the copy. `fogfroxel-build/shader-reuse-hashes.txt` records every SHA-256.
Only four new fog shader objects remain. No SDK source was copied; the two own idle shader runners were stopped
after checking their process identities and that neither had a compiler child. No other process was stopped.

16:04 CEST: both native targets and the managed plugin have built. All four new shaders compiled after the shared
guard released them. Binary paths and hashes are above; the coordinator can now make the first game comparison.
No game, GPU test, deploy, install, commit, or write outside the permitted worker paths. The remaining limit is the
required game evidence: do not proceed to reservoir/filter work or remove the old route before that answer.

## State when stopped ? continuation, 17:33 CEST

All requested continuation code is built. The final DLL is `7C3CFA54CF5124B5523F0044D386F35866FB2A2C247CCD9F10AAA446FB9DA79D`.
Both targets built; 116 source checks, the proposal oracle and the guarded fog-only GPU checks pass. The final
intersecting-box case records no other GPU user. No process remains running from this worker's tests/builds.
The integration-only and full-continuation binaries and separate patches are in the handoff table above.

Next is the coordinator's one clean tavern run, `fog-froxel-b`, using main's current plugin and a three-way
merge of this delta against its saved 16:38 state. Compare all four fog timings and exposure/gain, then shafts,
movement and mirrors. The game's clean 9.84-10.54 ms integration baseline is the comparison, not the earlier
contended run. Directional history and current-main source audit remain gaps; the old per-pixel route stays
until the owner accepts the grid. No pre-integrated coarse *medium* was substituted for the exact per-ray
medium: reasons and the equivalent-work performance fix are documented above. No main/game write, deployment,
install, download to disk, git mutation or interference with someone else's process.
