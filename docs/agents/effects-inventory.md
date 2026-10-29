# Effects inventory (PT-71 items 7 and 8, job 1)

2026-10-07, fxinv worker. Implemented and compiled for Mono; no game run or appearance claim. No native/ABI change, deployment, GPU test, commit, download or production-renderer edit. The remaining evidence is the coordinator's one in-game run. PT-71 items 7 and 8 remain open.

## Run once after merging

```powershell
.\tools\harness\Run-PtHarness.ps1 -Scenario candle,campfire,fog,night,tavern-room -Recipe effects
```

The brief's command is correct. The launcher builds/installs the harness and obeys its GPU-start gate. This worker never ran it. Three steps: Off inventory, On inventory, Off. On is needed to observe the installed tracer's actual light slots and cached reasons. Calling its classifier during an Off-only inventory would change its caches. The inventory itself only reads; the recipe uses the existing harness freeze/config restoration and takes no screenshots.

Allow roughly 3-8 minutes for five scenes, excluding GPU wait. This is an estimate, not measured. Deliberate waits are at least 16.5 seconds per scenario: two seconds of live probes, three 1.5-second state waits, six seconds of settling, four seconds of frozen probes. Add loading, shader startup (On can wait 45 seconds), whole-world enumeration and serialization. Busy scenes can take longer. No inventory limit hides distant/inactive systems or particles; the normal run timeout still applies.

## Files and acceptance

Under `artifacts/harness/<run>/`:

| File | Must contain |
|---|---|
| `<scenario>/effects-live.json` | Three rendered-frame probes, a second apart, before freeze: frame, real/game time, timeScale, particle count/first eight particles and HDRP shader clocks. Two extra seconds only for effects requests. |
| `<scenario>/1-game-effects/effects.json` | Schema 1, actual/expected PT state, all loaded effects and rejected/unknown mesh candidates, materials, lights, fog and three frozen probes. Unknown reads include reasons. |
| `<scenario>/2-traced-effects/effects.json` | Same census while tracing, including selected light IDs, submitted values, cached source/fill/glow evidence and original HDRP state where recorded. |
| Both step folders' `effects.md` | Short renderer-by-shader family counts, known live counts and unknown counts. |
| `effects-summary.json` | Per-step/scenario and all-scenario families, Off and On separately; light associations, selected attached/nearby lights, glow recognition, capture routes, fog, freeze comparisons, unknown paths/reasons and save/fallback metadata. |
| `effects-report.md` | Readable family/light-decision/freeze tables and incomplete-evidence issues. Detailed identities/settings stay in JSON. |

The summary runs automatically. Repeat with `python tools/harness/effects.py <run>`. `-NoAnalyze` still opts out. Mixed recipes retain normal image analysis; this zero-image recipe skips it. Missing requested inventories, malformed data, bad probe spacing and PT activation mismatch make the summary exit nonzero. Expected API limitations are unknowns, not zeros.

Check both inventories for all five scenarios, requested On actually compositing, frozen probes at timeScale 0 with distinct frames at least one second apart, and readable camera fog. Inspect `particleStateChanged` and `shaderClockChanged` independently. A changing probe proves that part was not frozen. Still sampled particles do not prove the entire simulation or custom clocks stopped. VFX probes cover public counts only. Fallback saves are identified; these scenes cannot establish absence of rain, dust or effects in unloaded scenes.

## Implementation and decisions

- New `EffectsInventory.cs`, `EffectsRead.cs`, `EffectsTracer.cs`. Shared changes: one Step flag, one ParticleSystemModule reference, two one-line Runner hooks in its scenario setup/step loop. RunStep and Neural.cs are untouched. The nearby live-before-freeze and after-step points are both necessary for the design's live and per-step frozen sequences; no runner logic was moved or replaced.
- Ordinary particles: all live particles for size, colour, velocity and lifetime ranges; capacity, modules/curves/gradients, simulation/culling/time source, flipbook/sprites, custom data/streams, trails, mesh choices and particle-light template. Only diagnostic freeze positions use a declared sample of eight; this never changes rendering.
- VFX: each public system, particle-system info (alive count, capacity, sleeping, bounds), spawner state, output event, exposed graph property and observed renderer material slot. Public APIs do not enumerate render-output contexts or map slots to systems. Output events are not render outputs. GPU particle attributes, simulation space/time and output shape/flipbook choices are explicitly unknown. That is the boundary for the later VFX adapter.
- Materials: active shader passes, keywords and all shader properties, including texture identities/sizes/formats/transforms and blend/depth/cull properties. Effective compiled pass state, unsupported shader operations and arbitrary custom clocks cannot be inferred from property names. No texture pixels or shader code are copied.
- Material rejection mirrors `SceneCapture.ReadMaterial`'s early gates, using installed pure `LayerBrow`/`IsGlass` predicates and separately reading cached material indices. Gate acceptance does not prove geometry capture. Update this diagnostic when capture gates change. It never calls MaterialIndex, ReadMaterial or the mutable light classifier/scanner/size routines.
- Lights: actual `SceneLights.slots` and submitted buffer, existing caches and active/type/diffuse/intensity gates. Missing evidence remains unknown, particularly unscanned lights or light-layer/effect-reach decisions not established by those records. Off warns caches may precede the scene. Directional selection uses a separate Sun path and remains unknown in this local-light diagnostic. Particle light templates are recorded; generated particle-light identities are not public.
- Attached means same object, descendant, or light on an ancestor. Nearby means within 5 m of effect bounds, listed separately. Neither proves physical ownership. All loaded lights are inventoried independently; the proximity window does not remove lights from the census.
- Fog reads existing main-camera HDCamera.TryGet, blended Fog, local fog parameters/masks/materials/transforms/runtime bound caches, and existing/shared Volume profiles. Volume.profile and HDCamera.GetOrCreate are avoided because they can create state. Inactive local bounds caches may be stale; authored size/scale mode/transform remain present.
- Family system counts mean inventoried components (one ParticleSystem/VisualEffect/trail/line/mesh); VFX internal systems are individually recorded in the source JSON. Live totals are whole-component counts: their allocation to VFX shader outputs is unknown. Multi-shader components/counts appear in each family they use. Off/On and scenario totals are observations, not unique asset totals. Unknown counts, associations and glow classification stay separate from known zero. A missing type is an unidentified diagnostic record, never a fabricated system.

## Validation

- Mono build passes, zero warnings/errors. Output: this copy's `src/TGPathTracing.Harness/bin/Release/net472/`. Initial restore used already cached reference-assembly packages copied into `build/fxinv/packages`, an offline source and NuGet audit disabled; later builds use --no-restore. No package was downloaded.
- `python tests/harness_effects_test.py`: 9 tests. Nine deliberate mutations detected: wrong count, merged phases, hidden movement, discarded unknown, accepted bad spacing, error counted as a system, hidden missing inventory, wrong output filename and ignored failed PT activation. Source restored byte-for-byte; clean tests pass. Evidence: `build/fxinv/mutations.log`, `activation-mutation.log`.
- Mutated effects recipe to end On: `SourceConsistency.test_ingame_harness` failed naming effects. Restored (`build/fxinv/recipe-mutation.log`).
- Final `python tests/source_checks.py`: 89/89. Log: `build/fxinv/source-checks.log`. Launcher parses with no PowerShell errors; its analysis routing chooses two effects/no image steps for effects, zero effects/five image steps for quick. Build log: `build/fxinv/build.log`.
- Read installed Unity metadata with ilspy: HDCamera.TryGet signature, existing profile field, local fog fields, VFX public info, HDRP shader clocks. No game/GPU test. Runtime reflection failures become unknown records.

Tool side effect: the first dotnet first-run banner **reported** `Installed an ASP.NET Core HTTPS development certificate.` This was not requested. DOTNET_CLI_HOME/TEMP were redirected into this copy, but that banner does not establish that certificate-store effects stayed here. I did not inspect/change the certificate store because that would exceed the write boundary. Subsequent commands set `DOTNET_GENERATE_ASPNET_CERTIFICATE=false`. No command requested a game/main-tree write.

## Workarounds met; none removed by this read-only job

| Location/name | What later migration jobs must replace |
|---|---|
| `SceneCapture.cs`, ReadMaterial / MaterialIndex | Transparent/unlit and water rejection, named glass/brow exceptions, HDRP Unlit camera-only handling. |
| `SceneLights.cs`, GatherGlows / AddEffect / EffectGlows / Classify; `LightRules.cs`, GlowAt / EmitterAt / Claim | Bounds, names, material colours/blending and proximity infer emitters. Recognition is not traced particle geometry or ownership. |
| `SceneLights.cs`, Size; `LightRules.cs`, DefaultRadius / MeasuredRadius / FlameCentre | Guessed/bounded radius and relocation to apparent flame centre: proxy light instead of emitting shape. |
| `SceneLights.cs`, OwnSurfaces; `SceneCapture.cs`, OwnSurfaceKey; `pt_emissive.hlsl`, EMITTER_PAIRED | Nearby glowing surfaces claimed by a light stop emitting independently. Needs emitter identity. |
| `SceneLights.cs`, Darken / ApplyGain / originals | Surface/fog split and gain on remaining HDRP shading. Keep until those users migrate. |
| `HdrpPasses.cs`, FillFog / FogPass / EventFog / SuppressDistortion and fog/forward ambient control; `Settings.cs`, Fog Adaptation | Approximate fog state/brightness and suppressed screen distortion; HDRP still owns fog illumination and transparent shading. |
| `pt_common.hlsli`, fog_color / fogged; native fog_automatic path | Secondary-ray fog colour and gain approximations instead of traced medium transport. |

Next: merge, run the five scenes once, read family/unknown/freeze results, choose a real particle/VFX/fog family for the next job. Nothing here closes the migration itself.
