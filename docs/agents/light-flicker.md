# PT-73 e/j: lantern and frozen-frame flicker

Worker started 2026-10-07. Finished 2026-10-08. Fixes built; focused GPU checks passed; the single full GPU run completed with five assertion failures. No game launch, deployment, main-tree writes, commits or downloads.

Scope: the lantern's physical light, duplicate emission, glass and exposure; the apparent two-frame cycle; frozen plant motion; bright vista samples. Base: `base43`. ABI 0x00012100; no export, struct-layout or option-bit changes.

Evidence: main-tree `artifacts/harness/20261007-224654`, both inspection reports. The game's image is the intended room-brightness reference; an undenoised frozen mean is the tracer reference. Do not infer a fix from denoised output.

Validation performed: affected native/managed targets, focused new checks, source checks, then the whole GPU test once. No mutation tests. GPU gate: game closed, live harness lock absent, main-tree `Run-PtHarness.ps1 -GpuCheck` returns 0. Build with NRD disabled into `%TEMP%/tgb/light-build`.

Coordinator-owned tracking files are untouched by instruction. New defects, decisions, removed workarounds and remaining gaps will be recorded here.

## Findings and changes

1. **Near grass has a real coverage defect.** `alpha_opaque` always read the old 256x256 coverage tile, including the tracer's camera rays, even when its original texture was shared. It drew a fresh Bernoulli coverage decision from that blurred tile every frame. `SceneCapture` also discarded the original cutoff, replacing it with 0.5. The fix reads the original alpha at the ray's hit and applies the actual cutoff; jittered camera rays and finite-light samples supply coverage. The finest *resident* texture mip is used. No smoothing or temporal history was added. The original cutoff is retained, including values outside the old arbitrary 0.05–0.95 clamp. Fallback tiles still use their existing coverage semantics. Hair's special strand sampling stays separate.

   Atlas-derived opacity micromaps are disabled on original-texture cutouts (including a texture becoming available after a micromap was built), because their coarse classification can erase real holes or blades before the shader sees a candidate. Emissive cutouts and cloth coverage use the same original-alpha decision. This changes no export, struct layout or option bit. `TGPT_Material.alphaCutoff` now retains the source cutoff with an alpha tile; deploy managed and native together. The coordinator decides whether this semantic change warrants an ABI bump.

2. **The two-frame impression is a four-frame cycle.** All 16 screenshots have consecutive Unity frame numbers. In a fixed 224px tavern wall crop, squared spatial gradients repeat roughly **14, 15, 53, 65** every four frames (13.1–16.2 in the soft pair; 51.3–65.9 in the sharp pair). Local luma alternates roughly 11–12 / 13–13.5. Forest gradients repeat roughly **74, 96, 37, 31**. Crucially, the forest's `3-traced-raw` step, with temporal AND spatial ReSTIR reuse off, still repeats **14, 81, 110, 23**. Thus ReSTIR reuse is not required to produce it. The samples themselves do not repeat (lag-4 image difference does not fall toward zero). Measurements and crop coordinates: `light-flicker-frames.json`, reproducible with `tools/research/light_flicker_frames.py`.

   Confirmed presentation defect: Denoising None still took a bilinear blend of four reprojected samples; only RR bypassed it. Subpixel phase then changes the raw noise variance and spatial correlation, contrary to the purpose of None. None now uses the same nearest-sample handoff as RR. **Not yet established that this accounts for the entire captured four-frame cycle**: the captures are after the upscaler and post-processing. No per-frame jitter or pre-upscaler image was recorded. Do not claim this cycle fixed without the next run. The harness now records jitter, scaled time, wind speed/direction and traced/composited counts for each screenshot (fresh native counters, not the four-times-a-second Session cache), to separate jitter phase, wind and repeated/dropped traced outputs. No cache rate/history/seed tuning was done.

3. **The wind clock is scaled time.** `PlantWind.Send` passes `Time.time`; native `pose_plants` uses that supplied value and retains a pose when the clock, wind and view are unchanged. The run's frozen VFX diagnostic proves Unity `Time.time` and HDRP `_TimeParameters.x` both stay at **50.44535** across frames 15918–15921, while real time advances. This is a different view in the same session, so it does not prove the forest's wind direction/speed were constant. Its next screenshots now carry those numbers. No replacement clock or artificial plant freeze was added.

4. **The vista's white points are not demonstrated estimator bias by these screenshots.** Its fixed ground crop has stable mean luma 77.90–78.23 while individual highlights change. The gradient energy lacks the forest/tavern cycle (1,320–1,481). Direct sun uses a fresh sample of the solar disc and evaluates the GGX lobe; the reflection ray samples the GGX lobe. A narrow highlight can therefore have rare bright samples. The material audit reports a roughness mismatch on roughly half the outdoor pixels and many missing full-resolution textures; the materials/memory jobs must land before judging those ground highlights against the game's material. No firefly clamp, energy cutoff, roughness floor increase or denoiser tuning was added. **Attribution to one estimate is not proven from final RGB frames; a Specular/Direct separation is still required if it survives the material fix.**

## Lantern: measured photometry, unresolved brightness

The actual run's `glass-jars/1-game/audit-a.txt` identifies exactly one light within 1 m of the lamp:
`Room_01/Lights/Light_Int_Lantern_01_Lit/PointLight_Ambience#5195430`.

| Recorded property | Value |
|---|---|
| Position | (-6.318, 502.1, 1.847) m |
| `UnityEngine.Light.intensity` | 185,364 (HDRP's native punctual unit: candela) |
| Colour / temperature | white / 2,600 K |
| Original light dimmer | 1 |
| Range / HDRP radius | 7.92379 m / 1 m |
| HDRP lux-at-distance field | 27.34 m (the inspector unit itself was not captured) |
| Cookie / shadows | none / none |
| Diffuse / specular / volumetric | on / off / off |
| HDRP exposure | log2 -13.26, stable |
| Traced exposure gain, None | 6.540893 (+2.71 stops) |

This is not a lumen-to-candela conversion mistake in SceneLights: it takes `Light.intensity`, not an inspector-unit value. The previous audit (`20261006-234757/tavern-room/1-game/audit-a.txt`) records the **same** 185,364 value, so it did not originate in this merge. The session's aggregate size log has six Lamp lights, all traced with radius 0.1 m and moved 0.351 m to their selected effect bounds. That is aggregate evidence, not a per-light proof of this lamp's selected source. The captured VFX metadata also contains single-flame systems with a fixed 0.2m culling box: **culling bounds are not measured flame geometry**.

At a surface 30 cm away, before colour-temperature tint, incidence cosine, glass, material reflectance and exposure:

- HDRP's radius-softened term gives `185364 / (0.3² + 1²) = 170058.7 lux`.
- The tracer's small emitter gives `185364 / 0.3² = 2059600 lux`, **12.11 times** as much. With the extra 6.54 exposure, the simple comparison is **79.2 times** the game's term.
- Using the brief's 1 cd candle comparison: 11.11 lux. A diffuse wall of reflectance 0.2 would be 0.707 cd/m² (`rho E / pi`); the authored intensity instead gives 131,117 cd/m² before tint. Its isotropic white equivalent is 2.33 million lumens, versus roughly 12.6 for that candle. These are photometric reference calculations, not an assertion of this wall's unknown reflectance/distance or the exact rendered ratio.

The room's recorded luma 22.2 versus the game's 13.8 (1.61x) is display-referred, so it cannot be divided by the linear-light factor to predict a new screenshot. RR and NRD also have different recorded gains. The p98 meter ignores a sufficiently small hot patch; a stable game exposure does not mean the PT gain is one.

No second *Unity light* is colocated with this lantern in the audit. The full GPU run's native pairing fixture (a light 2 cm below a quad) measures room light **0.01124** with the quad dark, glowing with emitter sampling on, and glowing with it off; that fixture passes. This validates the paired native path, not the tavern's managed source matching. Paired emissive surfaces are excluded from sampled diffuse emission and diffuse-path emission; they remain visible to camera/reflection rays. The real glass code transports Fresnel reflection/refraction and absorption; it does not multiply all light by a fabricated glow. **The existing capture does not inventory this lamp's paired triangles, pane material or actual particle emission, so it cannot rule out duplicate emission or establish a pane-specific factor.** Its `affectSpecular=false` prevents the synthetic reflected-light sphere from being this lamp's direct glass highlight.

One proven emitter-selection bug is fixed: `EffectGlows` accepted the dedicated **Distortion** output of `VFX_Fire_EmberGen` because the graph's name contained Fire. That output warps the background and does not emit light. It no longer supplies an emitter or moves/sizes a light. New near-lamp photometry logs record original and traced RGB cd, positions, radii, the selected bounds/name and nearby claimed emissive surface keys beside the exposure text, once per source. They explicitly label effect bounds as culling bounds.

**Lantern brightness is not fixed by this worker.** I did not quietly substitute 1 cd, multiply the light by an arbitrary attenuation factor, restore HDRP's nonphysical fill falloff, or tune p98 to hide the patch. The recorded game light combines extreme intensity, a 1 m radius and fixed bright exposure. Replacing that radius with a 0.1 m source at the selected bounds changes the near field; its exact difference from the game's wall still needs a measured surface point and the game shader's attenuation. The correct remaining work is to use the real emitting flame's geometry and emission (the particle migration), replace the redundant light representation consistently, and meter that radiance. The new per-source log makes the next run's pairing and placement inspectable. This gap must remain in PT-73 e / PT-65; do not report the lantern fixed from a compile.

## Workarounds encountered

- Removed for shared ordinary cutouts: camera sampling of the small coverage copy; random geometry coverage from that copy; loss/clamping of the material's original cutoff; stale atlas micromaps deciding original-texture geometry.
- Removed for None: bilinear reprojection of raw samples. NRD/built-in filtered output retains its existing reconstruction; the larger one-frame-late HDRP depth/motion route remains PT-72.
- Removed: dedicated distortion output treated as an emitter by its parent graph's Fire name.
- Remain, disclosed: fallback texture copies where originals are unavailable; effect culling bounds used as flame size/centre; kind-based emitter radii and their clamps; the hidden HDRP-radius comparison setting; emitter pairing by proximity; non-emissive source spheres for some lamps; forward/HDRP handoff; old fixed leaf transmission probability. None was polished as a realistic replacement for missing physical data. No new light/exposure or sampling limits were added.
## Coordinator checks after merging

Run `glass-jars@quick` and `forest@raw`, keeping the raw frames and installing the updated harness DLL too. This worker never launched the game.

- Lantern: read the new `Light photometry:` line for `Light_Int_Lantern_01_Lit/PointLight_Ambience`, then the steady exposure/gain line. The new log lists nearby claimed emissive surfaces, not a proven per-light ownership map. A distortion-only output must not be the selected emitter. The wall should ultimately retain its texture, the flame remain small inside dark metal, and room luma approach the game's 13.8 rather than 22.2. Those are acceptance targets for the remaining photometry work, **not predicted results of this patch**. No evidence supports promising a new mean from this worker's small classification fix.
- Forest: verify identical `scaledTime`, `windSpeed` and `windDirection` during the frozen burst. Fresh `ptTraced`/`ptComposited` should advance with the captured frames; -1 means unavailable. Fine shared-texture cutouts must remain the same geometry for identical camera rays. Frame jitter legitimately changes pixel coverage at blade edges; lighting still has one-sample noise.
- Re-run `tools/harness/flicker.py` and the local `tools/research/light_flicker_frames.py` on the same crops. The tavern's raw gradient energy should no longer alternate between the 13-16 and 51-66 bands **if the removed bilinear filtering was the cause**. If it still does, correlate it with recorded jitter and trace/composite counters, then capture the tracer's output before the upscaler. Removing a blur can increase raw per-frame change while making blades truthful: do not judge the cutout fix solely by lowering the old 5-6-level grass number.
- Vista: no numerical improvement is claimed. If the rare white points survive the material/texture fixes, compare isolated direct and specular estimates with an undenoised accumulated reference. A display screenshot cannot establish unbiasedness.

## Files and integration

- `pt_common.hlsli`, `SceneCapture.cs`, `scene.cpp`: original cutout alpha/cutoff; bypass incompatible atlas micromaps. Leaves `pt_materials.hlsli`, `MaterialPrograms.cs` and `texture_share.*` untouched.
- `tracer.cpp`, `d3d11.hlsl`: None hands off individual samples without bilinear filtering.
- `LightRules.cs`, `SceneLights.cs`: reject distortion-only VFX as emitters; per-source photometry logging.
- Harness `Request.cs`, `Runner.cs`, `Traced.cs`: per-frame wind/time/jitter and fresh native counters. Frame JSON gains fields; native exports/structs/options are unchanged.
- `tests/pathtracing_gpu_test.cpp`, `tests/light_rules_tests.cs`, `tests/source_checks.py`: focused regression checks and existing textual contract updates.
- `tools/research/light_flicker_frames.py`, `light-flicker-frames.json`: measurements. Worker-only `build-light.cmd`, `install-light.cmake`, `test-light.ps1` record the build/stage/gate recipe; they contain this worker's absolute paths and need not be merged.

No coordinator tracking files were edited. Add the unresolved lantern photometry, effect culling bounds as source geometry, and any remaining four-frame cycle to the existing PT-73/PT-65 entries. Existing fallbacks and source-shape gaps above are not completed features.

## Validation

- Managed TGPT and harness net472 builds: passed, zero warnings/errors. Restores used empty package sources and disabled NuGet audit; nothing downloaded.
- LightRules checks: passed, including distortion-only versus actual flame outputs.
- Source checks: 93 passed.
- Native TGPT and GPU test targets built with NRD OFF. The first test compile caught and corrected the new test's misspelled NO_RADIANCE_CACHE option; the final build log ends with both targets built. Existing float-narrowing warnings in the large GPU test remain.
- Only TGPT was built and installed to `%TEMP%/tgb/light-stage`; the existing bridge and already-installed Agility/FidelityFX runtime binaries were copied read-only from main into that private stage. Main/native/game folders were not changed.
- Built/staged TGPT SHA256: `B11F18829AC15EE75E77DFD7F183242CA8359578469D93A5E095000ACFB1D05B`.
- No mutation tests, game run, deployment, commit or push.
- Focused GPU run: **passed**. Source cutout at cutoff 0.8: 0 green pixels; at cutoff 0.4: 92,719; changed frozen geometry pixels across random seeds: **0**. Its `73-original-cutout.png` was inspected: sharp green strips, red surface visible through their holes. Shader passes took 161.3 s on the first run. GPU log: `%TEMP%/tgb/light-focus.log`; image: `%TEMP%/tgb/light-focus-images/73-original-cutout.png`.
- The DLL actually loaded beside the test has the same SHA256 as build and stage. Main's GPU gate returned 0 after its harness run closed; no processes were stopped.

### Full GPU run (once): five failed assertions

`FAIL: glass bends light by its material's own index of refraction (another index lays the light elsewhere)`.
The named 1.5 case matches the unnamed case (difference 0.000); 1.2 changes the pattern by 1.784, but the recovered light measured over the opaque ball's shadow is 1.63, above the test's 1.1 bound. IOR 1 differs from straight transmission by 0.051 and passes. This is **not established as a pre-existing failure or attributed to this patch**. No threshold was relaxed. It is also not proof that the lantern's panes cause its hotspot: the fixture is a sphere under the sun, and its measure is a shadow-region ratio, not total emitted power. The coordinator should keep the failure visible in the merge result.

Full GPU exit code: **1**; **1,390 assertions passed, five failed**. It rendered **25,382 frames, zero failed frames**, last native error `0x00000000`, 25,381 composites. The new cutout checks passed again in this run. This is not a passing regression suite. NRD was disabled at configure time as required for this worker; this does not validate RR or the in-game picture.

Exact FAIL lines:

```
FAIL: glass bends light by its material's own index of refraction (another index lays the light elsewhere)
FAIL: every mesh with alpha-tested instances has opacity micromaps
FAIL: a material without a number, or whose texture died, reads its atlas tile as before
FAIL: clear glass casts no shadow
FAIL: a character's eye drawn forward is shaded by the tracer, not left to HDRP
```

Additional failure context:

- Micromaps: 2 meshes reported, 3 expected by the old assertion. The effective micromap-instance count was 0 with them disabled and 2 enabled; the on/off image difference was exactly zero. The missing third mesh has not been attributed. The original-texture bypass is intentional; the count assertion was not relaxed.
- Missing/dead shared texture fallback: relative image differences 0.00128 and 0.00107. This narrowly misses the old threshold. A raw nearest-sample handoff can affect comparison variance; that connection has not been isolated.
- Clear glass shadow and forward eye: the exact surrounding output is in `light-flicker-gpu.txt`. No glass/eye threshold or implementation was changed to make the run green.
- None of these five is claimed to be a pre-existing failure: no second full baseline suite was run, as instructed. The coordinator needs to compare them in the final merge result. Do not report PT-73 e/j fully resolved.

Logs: `%TEMP%/tgb/light-gpu.log` (full), `%TEMP%/tgb/light-gpu-gate.log` (gate and full output), `%TEMP%/tgb/light-build-final.log`, `%TEMP%/tgb/light-managed-final.log`, `%TEMP%/tgb/light-harness-build-final.log`, `%TEMP%/tgb/light-source-checks-final.log`. Failure excerpts and final totals are retained in `docs/agents/light-flicker-gpu.txt`.
