# PSR and coherent denoiser guides — PT-127

Worker `psr`, pristine base `base20pc`, started 2026-10-11 19:00 local. All writes stay in this copy or `C:/Users/Frede/AppData/Local/Temp/tgb/psr-*`. No game, deployment, installation, download, or Git mutation. The main SDKs are read-only.

## Design, before code (19:05)

NRD's documented PSR follows pure-mirror (delta) events. General glass and water need separate signal and guide layers; removing the exclusion alone would not supply those inputs.

Read: the complete local NRD 4.17.3 README; RR guide sections 3.4.1–3.4.9 and 3.5; review findings 26, 3, 20; existing transport, guide producers and filters. Also read NVIDIA's [PSR article](https://developer.nvidia.com/blog/rendering-perfect-reflections-and-refractions-in-path-traced-games/) and its [linked opaque sample](https://github.com/NVIDIA-RTX/NRD-Sample/blob/0e4242ef553ac66c179d975322c7d18aaa14e3b5/Shaders/TraceOpaque.cs.hlsl). No vendor source is copied into this repository. The linked sample's `IsDelta` definition was not retrievable; no numerical threshold is attributed to NVIDIA without seeing it.

### Eligibility and limits

- NRD's rule is a delta event: zero roughness and no diffuse component, followed to the first non-delta surface. A merely shiny metal is not a pure mirror. The harness mirror has metallic 1 and smoothness 1 and therefore qualifies at the material level. For this trial the numerical predicate is exactly encoded roughness 0 and diffuse RGB 0, not an invented 0.1/0.15 cutoff. The existing transport still has a 0.01 perceptual roughness floor; changing that settled reflection sampler is outside an input-only trial. Its small departure from a delta is disclosed, not called exact PSR.
- Smooth glass has two delta branches, reflection and transmission. Their endpoints need separate guide/signal layers. The article uses separate checkerboard fields; it explicitly warns that randomly choosing branches breaks guide continuity. Its threshold approximation for later glass branches would cut light and is forbidden here. RR also discourages checkerboarding. A compliant complete solution needs separate full-resolution layers, each denoised with its own guides, and unbiased treatment of further branching. It is a renderer change, not an exclusion-bit fix.
- Water's current path contains nonzero roughness, foam, medium scattering, glints, multiple interfaces and surfaces under water. NRD does not natively denoise that combined volume/surface signal. Calling its first random scattering event a surface is incorrect. Water requires that same split plus a separate volume signal. Rough water must remain rough; no smooth-water stand-in.
- The first game checkpoint will consequently cover the mirror's opaque endpoint and RR's coherent primary guides. Glass/water remain explicit open work. Their own filter must be removed in the same change that gives their **whole** signal valid replacement layers; removing it before then would only exchange a known workaround for unfiltered noise. No claim that finding 26 is finished.

### Record and light contract

The mirror checkpoint preserves the existing traced radiance and random sequence. Capture its existing endpoint's diffuse/specular split, before the mirror reflectance is applied. Keep emission and any transport not represented by that split as an exact residual. Eligible endpoints must be ordinary opaque surfaces; a further mirror, glass, water, subsurface walk, hair, or untracked moving mirror uses the existing path. This eligibility is a denoising boundary, not a ray-depth or light limit.

NRD sees one virtual surface: reflect the endpoint position and its previous position in the mirror plane; reflect its shading normal; take its roughness and material class. Depth and 2.5D motion are projected from those virtual positions. Material demodulation uses `NRD_MaterialFactors` at the endpoint and incoming ray direction. Multiply each factor by the mirror path's RGB throughput before dividing the submitted signal, and multiply back by that identical factor after denoising. Add the untouched residual. No light is clamped away. None continues to return the original traced radiance.

Diffuse and specular hit distances start at the replacement endpoint. They are first-bounce, in-lobe distances; never the camera segment or the sum through mirrors/glass. The current secondary shader often reads the radiance cache instead of tracing either lobe, so independent cosine/VNDF guide probes are needed for this checkpoint. They use their own RNG and cannot perturb lighting samples. Multi-bounce PSR needs the README's curvature/lobe treatment and a reverse chain transform; it is not obtained by adding segment lengths.

RR keeps the actual primary surface for this checkpoint. Every hair pixel takes the sampled surface's normal, depth, motion, roughness, RGB reflectance and hit distance together. Remove the first-card/coverage selection within the trial. Sky clears those guides. For glass, RR's reflection distance must be a primary-origin-to-reflection-hit Euclidean distance, not the length of a refracted random walk. Trace an independent reflection-guide ray and keep glass's depth/normal/motion together. This does not provide dense reflection motion for animated reflected objects or solve refracted-background motion; those are explicitly different inputs.

### Switch, build and acceptance

Requested hidden Advanced boolean `Primary surface replacement (trial)`, default false. Public frame bit 22: `TGPT_FRAME_PRIMARY_SURFACE_REPLACEMENT`; managed `FramePrimarySurfaceReplacement`. NRD kinds 1/2 and None share the trial; RR uses only its coherent guide fixes. No public record size/export changes planned. Internal storage/layout changes will be listed with the implementation.

Recipe `psr`: game picture, undenoised reference, live NRD off/on/off. Confirmed scene keys: `mirror-tavern@psr-mirror`, `glass-jars@psr`, `water-day@psr`. The mirror variant supplies the harness's stand/away actions. Glass and water are controls for preserved rendering and disclosed remaining coverage, not an assertion that they now use NRD. Coordinator must read exposure/gain as well as pictures.

The first build is due before 21:00 local. GPU fixture work waits for the coordinator's game answer, as instructed. RR execution requires coordinator `tgup_gpu_smoke_with_pt` and the game; the offline TGPT fixture cannot execute RR. No whole GPU suite will run here.

## State when stopped

20:04: first native checkpoint built at 20:03, within the two-hour deadline. All 57 shader variants are current, native build exits 0, and all 117 source checks pass. No owned compiler remains. Stopped for the coordinator's first game answer: no game, deployment, GPU test, or RR execution here. The managed plugin is **not built**, because its references are in the forbidden game directory. No new small GPU mode was added before the required game answer. Full glass/water PSR, chains and moving/curved mirrors remain open; this is not completion of the brief.

## Handoff and validation

Native trial: `C:/Users/Frede/AppData/Local/Temp/tgb/psr-build/TGPathTracing.Native.dll` (32,955,392 bytes).

SHA-256: `7DD2E5E3480D75EF8E880B31FD4512E63C8CDE27576C8FA782875946F896A68A`.

Review delta against pristine `base20pc`: `C:/Users/Frede/AppData/Local/Temp/tgb/psr-build/psr-checkpoint.patch`.
The adjacent `psr-source-manifest.json` gives hashes for all 33 changed files. It contains only our source/doc delta,
not vendor sources. Reproducible worker commands are `tools/psr/configure.cmd` and `tools/psr/build.cmd`, with the guarded
`Build-PtShaders.ps1 -BuildDir C:/Users/Frede/AppData/Local/Temp/tgb/psr-build -MachineTemp C:/Users/Frede/AppData/Local/Temp`
before the native build. TEMP/TMP must be `psr-build/tmp`, as in those command files.

Coordinator must build the matching managed plugin first: the changed files are `Settings.cs`, `NativeTracer.cs`,
and `HdrpPasses.cs` under `src/TGPathTracing`. An old plugin cannot enable frame bit 22, so it would not exercise this
DLL's trial. No bridge change or release package is part of this handoff.

| Check | Result |
|---|---|
| Guarded shader build | 57/57 current; final pass 4 compiled, 0 failed; subsequent guard reports 0 to compile |
| Native target | `TGPathTracing.Native`, exit 0; `native-build-final.log` |
| Existing GPU fixture target | `tgpt_gpu_test.exe` built, **not run**; no PSR fixture case claimed |
| D3D11 composite | FXC `/O3` CPU compile passes; same X4000 warning in pristine base with `/WX` |
| Source checks | 117 pass; `source-final.log` |
| Mutation | Managed bit 22 changed to 23 makes the new wiring test fail; restored byte-for-byte, passes |
| Managed / game / RR / GPU | Not run; required reference access and game answer are the coordinator's |

Build log details: two obsolete owned reflection compilers were stopped after review corrections, and an earlier
completed obsolete header was removed. The final guarded pass rebuilt every such variant. One C++ compile error
placed the new buffer's barrier before its local `Barriers` object; moving it into the existing barrier block fixed
the build without a shader change. The fixture executable hash is
`95A8383E12420259AA82137C553D61426114770599A5C471D9294B656D468220`.

## First checkpoint implementation

- Public flag bit 22 only; no export, option bit, ABI version, or public record size changes. Managed setting and flag wiring are in the copy.
- Changing the hidden setting resets native histories and HDRP's post-processing history, so the existing TGBU RR host sees the guide change as a reset too. The managed reset uses the same HDRP field its existing host uses; managed compilation/runtime confirmation is still owed.
- Private D3D11 `CompositeConstants.extras.x` gains bit 2 for the trial sky tag; no row or buffer-size change. The tag restores RR's neutral sky albedo and zero normal/specular guides, including a ray that passed hair in front of the sky. Such a ray also writes far-plane depth, keeping its already traced sky motion.
- Internal `FrameConstants.psr` is one uint4: record UAV, trial boolean, two reserved zero words. FrameConstants is 9024 bytes (was 9008). `PsrRecord` is 112 bytes: seven float4 rows. Its native resource wrapper has a matching static size assertion. `NrdFrame.primarySurfaceReplacement` is private, enabling material comparison in RELAX/REBLUR during the trial.
- NRD's material classes are consistent at actual and virtual surfaces while enabled: ordinary opaque 0, hair 1, zero-diffuse metal 2. These are filter classes, not truncated material indices; the material properties still come from the surface itself. The original class behavior remains with the switch off.
- Record allocated only for the enabled mirror checkpoint under NRD/None, released on Off/RR/resize; counted in memory reporting. About 223 MiB at 1924×1084 before allocation alignment. It is a correctness checkpoint, not a memory optimization. RR's guide fixes need no new per-pixel allocation.
- Only the specular shader defines `PT_PSR_CAPTURE`. Other big passes do not compile its lighting-capture additions. This copy still contains a larger specular shader even with the frame switch off: do not claim production speed from it; split into compile variants or remove the trial after the game answer.
- The mirror record carries the actual shading split and the same sampled fog transmittance as the original path. Fog scattering, emission, the mirror's own highlight, and NRD's retained bright excess form an algebraic residual. Nothing is removed from the raw path. The lighting RNG is not used by the two added distance probes.
- Virtual position is elongated along the original camera ray; previous position adds the reflected endpoint displacement. Eligibility additionally requires shading/geometric normals aligned within `1e-5` in cosine, so this checkpoint does not pretend to handle normal-mapped mirror curvature. This tolerance is ours, not a vendor roughness rule. Zero **encoded** roughness has the existing 8-bit quantization ambiguity; a full delta classifier should carry an exact material flag from the camera hit. These are trial limitations.
- The camera pass clears the whole record, then marks mirrors whose current and previous **full transforms** are equal, including followed GameObjects. The harness mirror is followed every frame despite standing still, so using `YARD_KIND_STILL` would have excluded the requested test. Deforming meshes remain excluded. `motionDepth.x` is the producer-only stationary tag until the reflection pass replaces that row with the coherent virtual motion/depth. The trial also skips the obsolete first-card material-guide fetch in the camera pass.
- Distance probes condition the existing cosine/VNDF sampler on directions above both normals, without a retry cap. An inward sample is not reported as a sky miss. This is our rejection-sampling construction, not a claim to have integrated NVIDIA's linked VNDF-v3 implementation. Degenerate/inward-facing endpoint normals keep the original path.
- If the separate NRD confidence trial is also enabled, replaced pixels get neutral confidence 1. Its current lighting-gradient pass measured the primary mirror, not the replacement; NRD's own disocclusion logic still runs. The `psr` recipes disable that separate trial to isolate this comparison.
- New `psr` recipe and `psr-mirror` variant. First coordinator checks: `mirror-tavern@psr-mirror`, then `glass-jars@psr`, then `water-day@psr`. All comparison takes are live. The latter two remain controls; they are not new NRD glass/water coverage. `NRD trial` remains whatever the coordinator selected, so the same recipes can check kinds 1 and 2.
- `Build-PtShaders.ps1` gains optional `-MachineTemp`: compiler TEMP/TMP can stay inside the allowed worker build while the guard still reads the real machine's BUILD-FIRST/GAME-WANTED markers. Compiler children use hidden windows. No worker priority override.

## Build containment

The remaining RR guide sections were also read during final review, completing the local August 2026 document. This did not change RR's host or preset. The edited D3D11 composite compiles with FXC using the runtime's `/O3` flags. `/WX` reports X4000 at line 412 in both pristine `base20pc` and this copy; the runtime does not use `/WX`. Logs are in the private build folder. This is CPU shader compilation, not RR execution.

Native configure uses the main tree's matching NRD/ShaderMakeBlob libraries read-only via `TGPT_NRD_PREBUILT_LIBRARY` / `TGPT_NRD_PREBUILT_DEPS`; the SDK source configure is never invoked. All native temporary files go to `psr-build/tmp`. No vendor sources were copied. Configure completed; its environment emitted an existing `vswhere.exe` lookup warning, but found MSVC 19.44 and generated the build.

The supplied default managed build resolves references under the forbidden game directory. I have not invoked it or read that directory. The permitted trees contain no Mono Unity/game assembly set (the Desktop set is IL2CPP interop and cannot stand in). Consequently the first handoff can contain the native DLL plus managed source, but the coordinator must compile the plugin in its authorized tree. This is the reason a complete native/plugin pair cannot be supplied under these file-access constraints; no binary weaving or dummy-reference build substitutes for it.

## Workarounds and pending acceptance

Within the switch, RR's first-card/coverage guide selection and glass's accumulated bent-path reflection distance are replaced. Outside it they remain for the required comparison. The glass/water reflection filter remains because its replacement is not implemented, not because it is accepted as correct. No texture copy, fake light, ray-depth cap, layer cap, or SER-off path was added.

This is a first game checkpoint, not completion of the brief. Besides layered glass/water transport, still open: chains of delta bounces, moving/curved mirror transforms, the exact delta classification/sampling noted above, and temporal stability of sampled hair guides. The mirror's secondary lighting still uses the existing radiance cache; this change neither replaces that estimator nor establishes its correctness.

After the coordinator's first game answer, add one small offline mirror/glass mode: independently intersect the fixture planes to check virtual depth/normal and motion, verify endpoint rather than camera/path hit distances, and compare two undenoised still-scene means with the switch off/on. Mutate the virtual normal or depth to make the guide check fail. Glass replacement assertions must remain pending until its layers exist; do not write a test that blesses the current fallback. RR additionally requires coordinator `tgup_gpu_smoke_with_pt` and actual RR game captures; `tgpt_gpu_test` cannot establish RR execution.
