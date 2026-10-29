# Review by GPT-6-astra (xhigh), 2026-10-07 14:50: the traced water caustics (PT-14)

Asked by the owner after the in-game run 20261007-140305 ("was that supposed to be caustics? that was all kinds of wrong"). Prompt: `<scratch>/review/prompt-caustics.md`, with the run's pictures. Read-only; nothing in it is tested yet (AGENTS.md: test a finding before you accept it).

The shader does trace light, reflect/refract it, and estimate its density where it lands. But the current integration has a decisive bug: **HDRP’s wave buffers never reach that shader. It silently traces still water.**

There are also separate problems in how the arriving light is placed on rocks, how it reaches reflections and submerged surfaces, and how much work each pixel does. Fixing the buffer handoff is necessary, but it will not fix everything shown here.

“Certain” below means established from the code and data types. “Likely” means the code explains the picture, but I have not isolated it in a new capture. I inspected the existing evidence; I did not run or modify the game.

The findings, ranked by their effect on the picture:

1. Certain: the wave-normal buffer is read using the wrong type.

   [WaterCaustics.cs:155](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGPathTracing/WaterCaustics.cs:155) gets `additionalDataBuffer` through reflection and applies `as Texture`.

   That field contains an `RTHandle`, which wraps a texture. It does not inherit from `Texture`. I checked the existing decompilation of the game’s HDRP assembly: the field is explicitly `RTHandle`, and HDRP unwraps it before use. Unity’s [RTHandle documentation](https://docs.unity3d.com/Packages/com.unity.render-pipelines.core@17.0/api/UnityEngine.Rendering.RTHandle.html) confirms the class and its texture accessors.

   `as Texture` therefore returns null. It does not invoke the handle’s implicit conversion.

   The consequence is larger than losing normals: [line 188](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGPathTracing/WaterCaustics.cs:188) requires both buffers before sending either one. Neither buffer nor the band count is sent. Native code accepts this as still water, and [pt_caustics.hlsl:87](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_caustics.hlsl:87) skips all wave sampling.

   This directly explains smooth reflected illumination without wave focusing. The repair is to unwrap the `RTHandle` and verify the actual texture, band count and samples at the native end.

2. Certain: landed triangles are allowed to light surfaces they never hit.

   [pt_common.hlsli:2703](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_common.hlsli:2703) accepts a receiving point up to:

   `5 mm + 10% of the triangle’s longest edge`

   away from the triangle’s plane. It then projects that point onto the triangle.

   No receiving object or primitive identity is stored. No visibility test connects this projected point to the original light path. A nearby, separate surface can consequently receive the same light. Conversely, a curved part of the intended rock can fall outside the tolerance and receive none.

   For a triangle with a one-metre longest edge, this permits a 10.5 cm displacement from the receiving plane. That is large enough to cross small ledges and gaps.

   The density also belongs to the triangle’s area, not the actual curved or tilted surface onto which it is projected. The loose normal test does not correct that difference.

   This is a strong explanation for light patches bounded by straight polygon edges.

3. Certain: the mesh loses light at precisely the receiver discontinuities visible in the cave.

   [pt_caustics.hlsl:270](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_caustics.hlsl:270) discards an entire triangle if any corner misses. Lines 286 and 292 discard further triangles when their hit normals or placement disagree.

   Three rays landing on different sides of a ledge do not justify dropping all the light between them. Some of that water area still illuminates the rock. Other accepted triangles bridge geometry that their interiors were never traced against.

   These cases need subdivision, properly clipped footprints, or a different estimator. Accepting some large triangles and deleting their neighbours produces hard boundaries and missing strips.

4. Certain: the fixed mesh does not converge to the full wave solution merely by moving it each frame.

   The finest cells are 2 cm wide; successive levels double their size. Their resolution follows distance from the camera, rather than wave curvature or how greatly the light footprint stretches on a receiver.

   [tracer.cpp:1967](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/tracer.cpp:1967) claims that randomly translating this grid converges to what a finer grid gives. That is not generally true. Averaging different coarse, linear approximations leaves a reconstruction error. More frames do not shrink the cells or resolve an unrepresented fold in the light paths.

   Even after the buffer fix, short waves and strongly stretched footprints can therefore lose detail. Random movement may hide the grid while leaving the wrong average image.

5. Certain: caustics are missing from reflections, later bounces and non-diffuse receiver responses.

   [pt_common.hlsli:2801](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_common.hlsli:2801) includes caustics only at the first vertex when `g_firstCaustic` is set. The underwater pass sets that flag; ordinary reflection paths do not.

   Thus, a caustic visible directly on a rock need not appear in its reflection. Light from that caustic also does not continue through later diffuse bounces.

   Furthermore, the stored quantity is diffuse irradiance divided by π. The incoming direction is discarded. It cannot reproduce the correct response of a glossy, metallic or other directional material.

   There is a particularly bad interaction here: ordinary shadow rays are blocked by water while caustics are enabled, but many secondary vertices never receive the replacement refracted illumination.

6. Certain: water is only handled at the initial constructed interaction.

   [pt_caustics.hlsl:227](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_caustics.hlsl:227) makes incoming visibility rays ignore water. Outgoing rays trace ordinary scene geometry without following further water interfaces.

   This omits wave self-shadowing, another wave intercepting reflected light, and a refracted ray leaving the water before reaching its receiver. The transmitted branch assumes its entire remaining distance is underwater.

   Additional exclusions change valid paths:

   - Line 233 drops every reflected direction with negative world-space Y, although a sloped wave can validly reflect that way.
   - Line 131 excludes lights below the water’s lower bound, so underwater emitters cannot contribute through an exit interface.
   - [pt_common.hlsli:1745](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_common.hlsli:1745) tests straight-ray crossings against the undisplaced water mesh. Its height shortcut also uses that mesh’s height.
   - Steep water surfaces are explicitly excluded.

   These are incomplete transport paths, not sampling noise.

7. Certain: submerged caustics receive excess absorption.

   [pt_caustics.hlsl:240](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_caustics.hlsl:240) already attenuates the light from its water entry point to the bed.

   [d3d11.hlsl:234](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/d3d11.hlsl:234) then multiplies the bed’s radiance by absorption over **twice** the bed-to-surface viewing distance.

   For this contribution, the water distances should be the incoming leg plus the outgoing leg, each counted once. The current result counts the outgoing leg twice. This darkens submerged caustics and changes their colour.

8. Certain: the lamp’s sampled shape differs from its emitting surface; some emitters are absent entirely.

   [pt_caustics.hlsl:111](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_caustics.hlsl:111) samples a point uniformly inside a ball. The harness lantern is a glowing spherical surface.

   Those distributions have different apparent brightness across the source and produce different softening of caustics. Sampling the volume without the appropriate emission model does not reproduce the surface emitter.

   One source point is shared across the entire frame. That preserves correlation between neighbouring rays, which a mesh needs, but also makes source noise move large regions together.

   The light picker enumerates the sun and point/spot light records. It does not include the tracer’s emissive-triangle sources. Glowing geometry without a matching point light therefore casts no caustics. Its “never zero where it lights any” weighting claim is also too strong: the range bound does not include the full horizontally displaced surface and emitter extent.

9. Certain: several hard limits discard light or water coverage.

   These conflict with the owner’s rule that a budget may increase noise or cost, but must not change the converged picture:

   | Limit | Where | Consequence |
   |---|---|---|
   | Four nearest water surfaces | [WaterCaustics.cs:113](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGPathTracing/WaterCaustics.cs:113) | Other surfaces disappear from this transport. |
   | Four source-water intersections; three crossing attempts | `pt_caustics.hlsl:68`, `pt_common.hlsli:1752` | Valid layers can be skipped. |
   | 200 m transmitted reach | `pt_caustics.hlsl:238` | More distant receivers get no contribution. |
   | Camera-frustum rejection | `pt_caustics.hlsl:293` | Offscreen receivers needed in reflections or indirect light are removed. |
   | Record overflow | `pt_caustics.hlsl:307` | Overflowing contributions vanish, including before next-frame growth. |
   | 4,096 records per lookup | [pt_common.hlsli:2741](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_common.hlsli:2741) | The remaining light is discarded without compensation. |
   | Half-float brightness clamp | `pt_caustics.hlsl:316` | Strong focusing loses energy. |

   The logged triangle totals are below the allocated capacity, so I would not blame buffer overflow for these captures. The lookup limit has no corresponding evidence counter.

   By contrast, selecting the two strongest lights plus one probability-weighted sample of the remainder is a legitimate sampling budget, provided every contributing light has nonzero probability.

10. Certain: the water’s own shading still contains approximations separate from the new caustics.

    [pt_water_under.hlsl:105](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_water_under.hlsl:105) creates a fictitious white diffuse surface at the water surface. It traces lighting for that surface, then multiplies it by HDRP’s scattering colour and foam data. This is not light transport through the water volume.

    Other definite problems in this path are:

    - Line 92 moves a nearby backface hit to the water surface and replaces its normal. That invents a receiver instead of resolving the geometry mismatch.
    - Line 82 changes a refracted direction to force it below the geometric surface.
    - [pt_water.hlsl:95](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_water.hlsl:95) samples a rough reflection, then folds directions across the geometric plane. The sampled radiance does not receive the corresponding full rough-surface scattering/PDF weight; the composite applies a view-angle Fresnel factor instead.
    - Underwater history averages radiance and distance separately, then applies exponential absorption afterward. In general, attenuating averaged radiance over averaged distance is not the same as averaging the correctly attenuated samples.
    - History and the final 3×3 filter follow the water surface without adequately distinguishing different refracted receivers. That can blend across bed and shoreline boundaries.
    - Underwater views and certain failed handoffs retain HDRP shading.

    These are relevant to picture 6, although that older screenshot does not establish which one caused its slab.

The basic optical arithmetic does contain correct work. [pt_caustics.hlsl:229](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/shaders/pt_caustics.hlsl:229) uses dielectric Fresnel and Snell refraction with index 1.3333. The sun’s projected source area and a point light’s triangle solid angle are appropriate flux measures. Dividing that flux by the landed area is valid for the corresponding flat, correctly mapped footprint.

There is no general “missing inverse-square falloff” bug in that area conversion. Nor should an extra refractive-index-squared factor simply be inserted into a flux estimate. The failures arise around those formulas: the wrong input surface, coarse reconstruction, wrong receivers, dropped paths and incomplete integration.

The intended wave decoding is mostly sensible once the handoff works. It uses the displacement array and additional-data gradients, sums active bands, applies the band scale and offsets, multiplies displacement by amplitude, and uses the horizontal choppiness factor. The gradients already contain amplitude, so not multiplying them by amplitude again is correct. The native copy preserves the source texture resolution; I found no deliberate low-resolution wave copy. The per-band constants match [Unity’s HDRP water setup](https://raw.githubusercontent.com/Unity-Technologies/Graphics/6000.0/staging/Packages/com.unity.render-pipelines.high-definition/Runtime/Water/HDRenderPipeline.WaterSystem.cs).

That still does not establish full parity with HDRP:

- Current maps, water masks and deformation are explicitly omitted.
- Constants from the previous frame are accepted alongside live buffers. Their time alignment needs measurement.
- Normal fading uses the undisplaced position’s camera distance; HDRP’s shading calculation uses the displaced position.
- HDRP’s stored shading gradients are not automatically the exact geometric derivatives of the complete horizontally displaced, multi-band surface. A physically consistent optical interface needs matching positions and derivatives.

So there are two different resolution issues: the current integration supplies no waves at all, and the repaired integration would still trace them through a finite, sometimes coarse light mesh.

For the pictures, my attribution is:

| Observation | Explanation | Confidence |
|---|---|---|
| Large smooth patches on the ledge | Still water reflects the lamp into a broad, smooth footprint. Faulty receiver projection and triangle rejection impose polygon boundaries. | Missing waves: certain. Exact boundary attribution: likely. |
| No fine bright lines | Wave focusing is absent because the buffers are not supplied. Coarse reconstruction remains a problem after that is fixed. | Certain for the current code. |
| Bright Direct view, dim final rock | Direct view shows illumination before the rock’s colour absorbs most of it. Final shading multiplies by material colour. The lake steps also have essentially the same exposure/gain, so this is not simply an exposure change. | Certain mechanism; its exact share of the visual difference is unmeasured. |
| Grainy final light | Random grid/source samples and coverage changes produce noise. Caustics are added after native NRD processing; RR still receives the composite. | Likely contributors. Blaming RR specifically would be a guess. |
| Sun caustics change average brightness by only about 1% | Missing waves remove concentration, not necessarily much total energy. Caustics redistribute sunlight. Also, disabling this option restores straight light through water, so the subtraction is not a clean isolated caustic contribution. | Certain measurement limitation. |
| Earlier pale slab and net along the bottom edge | The refraction/material/receiver path and its history/filtering are suspect. The new caustic stage cannot explain an older defect by itself. | Exact cause unresolved. |

A finite lantern also legitimately softens caustics. With the harness’s 4 cm radius and 35 cm height, its angular radius directly below the source is about 6.6°. A correct reference must use that emitting shape; razor-thin lines everywhere are not a valid acceptance requirement by themselves.

For the method, a warped light mesh is a real caustic technique. It is not inherently a painted substitute. NVIDIA’s published water-caustic implementation describes both procedural caustic meshes and individual photon footprints, including the mesh’s known artifacts at geometry discontinuities. That supports the general approach, while also matching the failure class here. [NVIDIA’s water-caustics article](https://developer.nvidia.com/blog/generating-ray-traced-caustic-effects-in-unreal-engine-4-part-2/).

But this fixed mesh, loose world-space lookup and diffuse-only composite do not meet this project’s correctness requirements.

My recommendation for this engine is:

1. First repair and validate the shared water surface: actual buffers, matching frame, position, normal and ray intersection. Every camera, reflection, shadow and caustic path must encounter the same water.
2. Put caustic transport into the lighting calculation at path vertices, including reflected and refracted views.
3. For the final estimator, investigate specular manifold sampling: starting at the receiver and solving for water points that connect it to the sampled light while obeying reflection or refraction. It avoids spreading a landed triangle across unrelated rocks.
4. Use light-traced samples and previous solutions to guide those searches. Keep a sampling route with coverage of every valid solution, and account for selection probabilities. Finding one convenient solution is insufficient.
5. Reuse samples across pixels and frames with correct weights, then denoise the resulting lighting with appropriate guides.

[Specular Manifold Sampling](https://rgl.epfl.ch/publications/Zeltner2020Specular) supports high-frequency normal/displacement surfaces and has both biased and unbiased formulations. The newer [sample-space partitioning and ReSTIR work](https://research.nvidia.com/labs/rtr/publication/hong2025partition/) specifically targets its interactive cost. This is a concrete research direction, not a claim that it will drop into this renderer cheaply.

Photon density estimation is a useful alternative and reference implementation. It traces genuine light paths, but a fixed gathering radius blurs the solution permanently. Progressive photon mapping reduces that radius as evidence grows; simply accumulating more frames at the same radius does not remove the bias. [PBRT’s treatment](https://pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Stochastic_Progressive_Photon_Mapping).

Ordinary directional path guiding alone also does not solve the exact connection to a point light through a perfectly specular interface. It is useful for proposing manifold solutions or sampling finite rough lobes.

The offline checks test useful pieces, but they would miss the main failure here.

[The test setup](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/tests/pathtracing_gpu_test.cpp:2842) constructs native textures directly and passes them to `TGPT_SetWaterCaustics`. It never executes the broken managed `RTHandle` extraction. Its main wave case is one sinusoid, one band, identity mapping, no horizontal displacement and a simple receiver.

The CPU comparison checks mean energy, peak location and peak contrast. Those are worthwhile. However, the reflected-lamp wave assertion at [line 3297](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/tests/pathtracing_gpu_test.cpp:3297) only requires enough bright and dark pixels. Large incorrect patches can satisfy that. Edge exclusions also avoid some of the geometry where this implementation fails.

The missing checks, in priority order, are:

- Production handoff: obtain HDRP’s real handles through `WaterCaustics.cs`, then compare GPU-sampled positions and normals on both sides of the bridge.
- The shaded lantern over the actual rocky ledge, compared with an undenoised converged reference using the correct emitting surface.
- Receiver discontinuities: a step, curved rock, partial miss and two nearby disconnected surfaces. Measure missing and leaked energy.
- The same caustic viewed directly, in a mirror, through water and after a diffuse bounce.
- Absorption with separately known incoming and outgoing water distances.
- Multi-band animation, rotation, currents, masks and deformation.
- Overloaded lookup/storage cases: reducing a work budget may raise noise, but must not lower the mean.
- Frame-time checks on these scenes, including application of the caustics and the water passes.

Finally, the performance problem is larger than the caustics stage’s timer suggests.

The quoted 444,364-triangle sample belongs to the sea-cave portion of the log. A lake sample is substantially worse:

| Logged sample | Main tracer | Caustic generation | Main composite | Separately timed water |
|---|---:|---:|---:|---:|
| Lake | 94.47 ms | 4.07 ms | 50.40 ms | 81.05 ms |
| Sea cave | 35.77 ms | 5.95 ms | 6.15 ms | 15.86 ms |

Sources: [lake log sample](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/artifacts/harness/20261007-140305/TGPathTracing.Native.log:4547), [sea-cave sample](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/artifacts/harness/20261007-140305/TGPathTracing.Native.log:4560).

The main `gpu` measurement excludes water. These are averaged timings, so adding them is approximate, but the lake is around 175 ms of tracer GPU work—not 30–38 ms.

The expensive design choices are identifiable:

- Every shaded point searches multiple hash levels and follows scattered linked lists of triangles. The lake’s 50 ms composite is strong evidence that applying the caustics is the dominant problem.
- Underwater shading repeats that lookup, helping explain the water-pass increase.
- Each water pixel can run full lighting paths for a reflection, a refracted receiver, and the fictitious diffuse water surface.
- Light selection scans the light list repeatedly for water tiles.
- “Water reflections” times both `pt_water.hlsl` and `pt_water_under.hlsl`; [tracer.cpp:2987](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/native/pathtracing/src/tracer.cpp:2987) confirms that scope.

The next profiling change should split reflection, refraction, surface/body lighting and caustic lookup. Record lookup lengths and rejected/truncated contributions. A triangle count alone cannot explain or control this cost.

For comparison, NVIDIA reports roughly 0.5–2 ms for its water-caustic approach in common single-light cases. That is a useful scale, not a promise for this scene or a general manifold solver. The present 50 ms application pass is plainly not an acceptable real-time design.

Fixing the handle extraction should come first. The next acceptance gate should be the shaded lantern on the real ledge, with verified waves and a physical reference. Increasing brightness, changing denoiser settings or shrinking arbitrary limits would obscure the remaining errors.