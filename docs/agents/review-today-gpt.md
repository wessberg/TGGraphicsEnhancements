# Review by GPT-6-astra (xhigh), 2026-10-07 14:40: today's merges (traced water, the limits, hair by its own place, lamp size)

Asked by the owner ("Please get gpt-6-astra xhigh to review the caustics work (and maybe the other stuff too)"). Prompt: `<scratch>/review/prompt-today.md`. Read-only; nothing in it is tested yet (AGENTS.md: test a finding before you accept it).

The far-list design is mathematically sound, but the implementation has remaining bias. The hair change fixes camera parallax only partly. The water and lamp changes still contain approximations that should not be called physically complete.

I reviewed the current code, all four reports, and the sea-cave capture. Its native build is `2CAF8E849FFE`, from 10:39—before the later limits and hair merges. I excluded the new caustics implementation.

| Change | Verdict |
|---|---|
| Water | Real rays, but incorrect filtering and incomplete light transport. |
| Limits | Correct near/far partition; numerical and other limits still change the picture. |
| Hair positioning | Correct direction; missing movement, visibility and reconstruction data. |
| Lamp position/size | Moving lights to flames helps. Their finite shape and shadow treatment remain inconsistent. |

Findings below are ranked by likely impact on the player. “Certain” means demonstrated by code or arithmetic, not reproduced in a new game run.

1. P1 — Water changes material source at the screen boundary. Certain code behaviour; likely explanation for the pale slab.

   [pt_water_under.hlsl:95](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_water_under.hlsl:95) reads the traced material, then replaces its colour and normal with HDRP’s when the hit projects onto a matching screen pixel. [pt_common.hlsli:2765](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_common.hlsli:2765) abruptly stops that replacement outside the screen or when depth disagrees.

   A refracted bed point can project below the picture even though the water above it remains visible. If its captured material differs from HDRP’s, the bed changes appearance exactly there. The broad, nearly horizontal transition in the attached image fits this explanation better than an error affecting only the last pixel row.

   This is my strongest lead, not a proven identification of that surface. Record the hit’s material ID, position and whether `hdrp_material_at` succeeded across the boundary. Correct the captured material; fading between these two appearances would conceal the defect.

2. P1 — Refraction filtering mixes unrelated surfaces and applies absorption in the wrong order. Certain.

   [pt_water_under.hlsl:125](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_water_under.hlsl:125) averages bed radiance and travel distance separately. Its history test checks the water’s depth, not the refracted hit, normal or identity. [d3d11.hlsl:216](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:216) then averages nine neighbours before applying extinction.

   These operations are not equivalent:

   `average(L × exp(-σd)) ≠ average(L) × exp(-σ × average(d))`

   Misses carry a distance of **1,000 m** and enter this average. Using the code’s absorption formula, an equal mixture of a lit 1 m hit and a dark miss should contribute **0.335** in a simple numerical case. The current calculation gives approximately **6 × 10⁻⁸⁸**.

   This explains lost bed detail, smearing and false bands. It can leave the water’s body colour dominating where the bed should show. Apply attenuation to each path before accumulation, and validate history against the refracted surface.

3. P1 — The shore correction invents a surface instead of resolving the intersection. Certain; contribution to the reported blockiness is likely.

   [pt_water_under.hlsl:92](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_water_under.hlsl:92) moves any back-face hit within 10 cm onto the water surface and replaces its normals. It retains the hit’s material.

   Thus nearly identical rays on opposite sides of that threshold can see different geometry and absorption distances. Rock intersections and thin submerged objects can visibly snap between cases.

   The waterline also uses a binary stencil decision, with HDRP shading retained when traced history is unavailable: [d3d11.hlsl:201](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:201). That can switch shading models at moving edges.

   Fix the geometry agreement and sample actual pixel coverage. An arbitrary shore fade is not the physical solution. Some large flat rock shapes also appear in the game’s reference, so their existence alone is not evidence against this change.

4. P1 — Water still contains stand-ins for physical transport. Certain.

   [pt_water_under.hlsl:105](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_water_under.hlsl:105) constructs a white diffuse surface and uses its light to illuminate HDRP’s precomputed scattering colour. This is a surface approximation for light scattered through a volume.

   [d3d11.hlsl:234](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:234) also assumes the light’s incoming underwater distance equals the viewing ray’s distance by doubling the latter. That is not generally true. Extinction belongs to each actual travelled segment. [PBRT’s transmittance description](https://pbr-book.org/4ed/Volume_Scattering/Transmittance) supports that distinction.

   Other gaps remain: underwater views retain HDRP shading; ordinary reflection/refraction queries exclude water geometry, so they cannot follow another water crossing or reflection between waves. These need to remain named gaps.

5. P1 — Water reflections use incomplete shading and an incorrect rough-reflection estimator. Certain.

   [pt_water.hlsl:95](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_water.hlsl:95) samples a rough reflection, but accumulates incoming radiance without its per-sample Fresnel and masking weight. The composite later multiplies by Fresnel at the surface normal. Those are different quantities for rough water; increasing samples will not correct this.

   The hit is also shaded with diffuse `shade_path`, rather than the fuller `shade_seen` used by ordinary reflections. Reflected metal, eyes and other glossy surfaces therefore cannot match their camera appearance.

   Use one consistent reflection/refraction model, including its sampling probability. The required relationship between sampling, Fresnel and masking is described in [PBRT’s rough dielectric model](https://pbr-book.org/4ed/Reflection_Models/Rough_Dielectric_BSDF).

6. P1 — Reprojected hair is not checked against current foreground geometry. Certain.

   [d3d11.hlsl:848](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:848) finds old hair and overlays it without comparing its reconstructed depth against the current visible surface.

   When a camera move brings a nearby object in front of previously visible hair, the old hair can be painted over that object. Reading current depth to estimate movement does not perform this visibility test.

   Validate the final sampled hair point against current depth before compositing, including samples used by bilinear filtering.

7. P1 — Hair still borrows object movement from the background, and RR receives mismatched data. Certain.

   [d3d11.hlsl:860](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:860) explicitly adds the background surface’s object movement. A walking character’s hair against a stationary wall consequently gets no character movement; an independently moving background can supply unrelated movement.

   [hair_guides:989](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:989) changes colour and normal guides, but leaves depth and motion describing the surface behind the hair. RR trails therefore remain an integration defect.

   The twelve-depth search also has deterministic holes: [d3d11.hlsl:869](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:869). At the capture’s field of view, with a 10 cm strafe, hair 3 m away and a wall 12 m away, the nearest search seed can miss by about **3.3 pixels**. Thin strands can disappear before refinement starts.

   Previous skinned positions, correct motion/depth guides and visibility-aware reprojection are needed. The merged code correctly leaves the unproven hair-brightness change out.

8. P1 — The lamp shadow-end fix is missing from bounced light and hair. Certain.

   The camera path correctly avoids subtracting the extra surface offset for a light placed at its emitter: [pt_rtxdi.hlsli:165](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_rtxdi.hlsli:165).

   However, [pt_common.hlsli:2509](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_common.hlsli:2509) and [pt_hair.hlsl:320](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_hair.hlsl:320) still subtract it unconditionally after `shadow_ray` has already shortened the ray.

   Blockers close to a flame can therefore shadow its direct camera appearance while leaking light into bounced illumination or hair. Share the corrected endpoint rule across these paths.

9. P2 — NRD’s excess-light add-back uses the wrong shading representation. Certain.

   NRD receives bounced light evaluated using the geometric normal. Its output gets the normal-map shading restored in [pt_nrd_prep.hlsl:50](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_nrd_prep.hlsl:50).

   The excess added in [pt_compose.hlsl:195](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_compose.hlsl:195) remains in the geometric-normal representation.

   Even with an otherwise unchanged denoiser: raw light **1,000**, limit **250**, shading factor **0.5** should produce **500**. This code produces **125 + 750 = 875**. Bright excess loses the normal map’s shading. Reconstruct the excess using its own direction before adding it.

10. P2 — The enlarged light capacity can give lights zero probability. Certain numerical counterexample.

    [scene.cpp:1514](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/scene.cpp:1514) builds cumulative probabilities in double precision but stores them as floats. At 65,536 entries, its minimum probability is smaller than float spacing near one.

    Reproducing this calculation with one dominant light and 65,535 faint positive lights produced **48,774 zero-width probability intervals**. Those lights contribute nothing through this sampler. Cached weights make subsequent brightening especially problematic.

    Use a probability representation that preserves support, such as a hierarchy or a suitable alias sampler. A theoretical positive floor is insufficient after rounding.

11. P2 — The emissive budget is not always “noise only.” Certain.

    Ordinary unsampled emission has a sensible complementary path contribution. But paired emitters are treated differently.

    [emission_weight:2645](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_common.hlsli:2645) returns full emission when no emitter record exists, and zero gathered emission when that record marks the surface as paired with a lamp. [scene.cpp:1281](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/scene.cpp:1281) makes record existence depend on the triangle budget.

    Moving a paired surface into or out of that budget changes its contribution. Pairing must be independent of whether the surface has an explicit light-sampling record.

12. P2 — “Measured lamp size” remains a capped proxy. Certain.

    [LightRules.cs:140](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGPathTracing/LightRules.cs:140) takes the middle bounds extent and clamps it by category—for example, candle radius cannot exceed 1.5 cm.

    [pt_rtxdi.hlsli:118](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_rtxdi.hlsli:118) then combines centre-distance illumination with a sampled receiver-facing disc. The generic path uses the centre direction for shading and the disc direction for visibility. Neither is a consistent integral over the measured emitter.

    Moving the light to its flame is useful. Calling the resulting size and lighting fully physical is premature. Use the same finite emitter and probability model for illumination, shadows and reflections. [PBRT’s area-light model](https://pbr-book.org/4ed/Light_Sources/Area_Lights) describes that consistency.

The near/far accounting itself is correct in exact arithmetic. The current CPU implementation places **every eligible no-range light** in each coarse far list. Near entries are sorted, and the binary membership test rejects duplicates. With `M` total candidates and `n` candidates from a list, multiplying by `M/n` and later dividing by `M` supplies the correct list weight. Stale importance weights change noise, provided every contributing light retains positive probability. Finding 10 breaks that proviso.

Other limits were raised, not removed: [SceneLights.cs:635](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGPathTracing/SceneLights.cs:635) still drops excess lamps outright. Glass crossings remain capped. The cache still clamps samples. Water packing clamps to 60,000, and raw light buffers remain FP16 despite removal of radiance clamps: [tracer.cpp:1529](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/tracer.cpp:1529). NRD’s add-back cannot recover light already lost or overflowed upstream.

For performance:

- Water traces a reflection, a refracted bed path, and another path for the artificial white surface, plus highlights. Several invoke multi-vertex shading independently. This is substantial duplicated work. The “water reflections” timer includes both water passes, but excludes their D3D11 filtering/compositing.
- **677 entries per cell does not mean 677 shadow rays per bounce.** Direct-light sampling uses at most eight near candidates and one far candidate. The cave log also contains **3,141 emissive clusters**; these count as grid entries alongside lamps.
- A remaining linear scan exists in glass’s [reflected_lamps:21](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_specular.hlsl:21). A light-intersection acceleration structure can preserve the result while avoiding a scan of all near and far entries.
- Safe optimizations include exact light hierarchies, separately cached far-list uploads, skipping zero-weight duplicate candidates before material evaluation, restricting underwater work to water pixels, and sampling reflection/transmission branches with correct probability compensation.

At **55 fps, the entire frame has 18.2 ms**. Water alone at 12–20 ms cannot fit that target. A few milliseconds is a reasonable engineering budget, not a measured promise. The author’s simple-lake timing does not establish what this scene should cost. Split reflection, refraction, surface-light and D3D11 timings before assigning the regression to distant lights.

The pale slab’s exact hit remains unproven. Missing or coarse bed geometry is another possibility, currently a guess. The screen-dependent material switch is the first thing I would isolate.

Validation: **89 source checks passed**. I ran CPU arithmetic counterexamples, but no build, GPU test, mutation or game session. No files were changed.