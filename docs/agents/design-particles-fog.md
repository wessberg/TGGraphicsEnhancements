<!-- GPT-6-astra (xhigh) through Codex, read-only, 2026-10-07 18:02 to 18:14; the brief: docs/agents/briefs/brief-design-vfx.md.
     A design, nothing built or run. Filed by the coordinator; findings to test before they are acted on. -->

Particles, transparents and fog in TG Path Tracing  
Design for PT-71 items 7 and 8 — 7 October 2026

Use one world-space representation of surfaces and participating media: matter that absorbs, emits or scatters light. Camera rays, reflections, refractions, shadows and bounced light must all query it. Keep the game’s fog density and its simulation of effects. Replace their lighting and their rendering.

This is a design, not an implementation or an in-game result. “Verified” below means checked in this tree. “Recorded” means reported in its notes. Public Unity capabilities do not prove that FoA uses them. Those game-specific possibilities are marked as assumptions. No files were changed, no game or tests were run, and neither restricted NVIDIA source directory was read.

The main implementation risk is missing scene data. A painted flame sprite does not contain a unique 3D flame. A screen distortion map does not contain a temperature field. Correct ray tracing cannot recover information that was never supplied. Finding or building those missing fields is part of migration.

---

The tree already identifies some effects, but it does not capture their particles.

| Evidence | What it establishes |
|---|---|
| Verified: `SceneLights.GatherGlows`, `AddEffect`, `EffectGlows` | Scans `ParticleSystemRenderer` and `VFXRenderer`. Uses bounds, material colours, blend properties and names to identify likely glowing things. This does not establish which renderer families occur in a running scene. |
| Verified: `SceneLights.Size`, `LightRules` | Places some lights at an effect’s centre. Reduces its bounds to a radius, with kind-specific defaults and bounds. This is still a light proxy, not the flame’s emitting shape. |
| Verified: `SceneCapture.MaterialIndex` | Rejects many transparent and unlit materials, with specific exceptions. There is no general particle-state capture or local fog density capture. |
| Verified: `HdrpPasses.FillFog` | Reads the camera’s blended `Fog`: mean free path, heights, colour/albedo and distant-fog settings. It does not provide the full local density field or scattering model. |
| Verified: `FogPass`, `EventFog`, `TGPT_FOG` | Adjust the brightness of fog HDRP has already rendered. `fogged` adds an approximate fog colour along secondary rays. Neither traces fog lighting. |
| Recorded: PT-06 | The candle patch changes even when the harness freezes the world. The report attributes it to the game’s glow/haze sprites. The exact contributing shader passes still need identification. |

These findings come from [SceneLights.cs](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGPathTracing/SceneLights.cs), [HdrpPasses.cs](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/src/TGPathTracing/HdrpPasses.cs), and the requested [PT entries](C:/Users/Frede/Developer/TGFOA/TGFrameGeneration/docs/PT-REQUESTS.md).

Public Unity capabilities to inventory are ordinary, stretched, horizontal and vertical billboards; mesh particles; trails; and VFX Graph primitive, mesh and strip outputs. Outputs can be lit, unlit, alpha blended, premultiplied, additive or cut out. Soft particles fade near depth intersections. Distortion outputs can encode screen offsets or derive distortion from normals. **Assumption:** FoA uses some combination of these; the scanner alone proves none of them. [Unity particle renderer](https://docs.unity3d.com/6000.0/Documentation/Manual/PartSysRendererModule.html), [VFX output settings](https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/Context-OutputSharedSettings.html), [distortion outputs](https://docs.unity3d.com/Packages/com.unity.visualeffectgraph@17.0/manual/Context-OutputDistortion.html).

HDRP computes volumetric fog on a camera-aligned 3D grid. Each “froxel” covers a screen tile and a depth interval. Density and scattering properties enter this grid; lights, shadow maps and ambient lighting produce its scattered light. HDRP integrates that light towards the camera and filters it. Local fog can use a 3D texture or a procedural material, with ordered blending. **Assumption:** FoA uses these local mask types; verify their actual presence. [HDRP fog controls](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/fog-volume-override-reference.html), [local fog controls](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/local-volumetric-fog-volume-reference.html).

Recorded in `docs/agents/fog.md`: one outdoor audit had a 682 m mean free path and 128 m volumetric depth extent; a swamp stack had 32 m and 64 m. These are scene observations, not constants. A local Volume overriding the global `Fog` component must also be distinguished from a `LocalVolumetricFog` object.

Extend the existing harness audit for one run of `candle,campfire,fog,night,tavern-room`. Record:

- Every effect’s stable identity, scene/hierarchy, active state, renderer/output type, shader and material identities, passes, blend/depth/cull settings, textures and dimensions.
- Live count, capacity, bounds, simulation space, culling mode, time source, current size/colour ranges, velocity, lifetimes, flipbook settings, custom streams, trails and mesh choices. For VFX, include each system/output separately.
- Attached lights and particle light modules; nearby lights separately. Log identities, units, intensity, colour, cookies and volumetric settings. Proximity is evidence, not ownership.
- The blended global fog state and every local fog object: transform, bounds, mask/material, density, albedo, priority, blend operation, fades and animation.
- Capture route, captured count, missing attributes and unsupported shader operations. Log `unknown`, never a plausible default presented as measured.

Include off-camera systems in loaded scenes. Capture a short live sequence before freezing, then verify that particle state **and shader animation time** stop changing. Inventory results decide the shader adapters; they do not ask the owner to diagnose engine details.

---

Each effect needs a physical representation independent of the viewing camera.

| Effect | Representation used by every ray |
|---|---|
| Smoke and fine dust | A density field with absorption, scattering colour and an angular scattering distribution. It can shadow itself and other objects. Ordinary cold smoke/dust has no emission. |
| Fire | A volume with spatially varying emission, absorption and scattering. Its emitting regions light surfaces and fog, and remain visible in mirrors. |
| Sparks and embers | Small emitting surfaces or solids, using actual particle shape and motion. A motion-blurred streak is integrated movement during the shutter interval, not an arbitrarily enlarged lamp. |
| Rain | Moving dielectric drops, with reflection, refraction and absorption. A stretched sprite’s length is not evidence of a drop’s physical length or radius. |
| Heat haze | A spatial refractive-index field. Rays bend through it before finding the background. |
| Other transparent meshes, ribbons and magic | Actual geometry and a material adapter: coverage, transmission, scattering and emission as appropriate. “Unlit” or “additive” alone does not prove physical emission. |
| Candle halos | Classify their purpose first. Real scattering belongs in a medium. Optical bloom belongs to the camera response. A decorative halo must not become a room-lighting surface merely because its shader adds colour. |

For smoke, soft intersections come from integrating density only up to the actual surface. Do not reuse the gameplay camera’s depth fade for mirror or shadow rays. For true cutout surfaces, retain stochastic coverage. Never turn a smoke billboard into a solid rectangle or make it face each querying ray.

**Assumption requiring evidence:** suitable 3D density, emission or temperature data may exist in an effect’s assets or shader inputs. If only a painted flipbook exists, its alpha constrains opacity along one view; it does not specify thickness, scattering colour or emission power. A prelit smoke texture cannot simply become scattering albedo.

For each such family, add a real volume asset or physical field model tied to the game’s particle positions, sizes, lifetimes and motion. Record the missing parameters and modelling assumptions. Validate from several views. Generic sprite extrusion is not sufficient evidence that the original effect has been reproduced. These families remain open migration items until their representation is justified.

For ordinary ParticleSystems, capture after simulation and before tracing. Use a reusable, sufficiently large `GetParticles` buffer. Read current size and colour, rotation, velocity, lifetime, simulation transforms and custom data. Reproduce the renderer’s output calculations, including flipbook interpolation and vertex displacement. Use `BakeMesh`/trail snapshots as geometry references where appropriate, not as a way to turn smoke into permanent cards. Unity exposes these ordinary-particle APIs. [Particle capture](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/ParticleSystem.GetParticles.html), [current size](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/ParticleSystem.Particle.GetCurrentSize3D.html), [current colour](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/ParticleSystem.Particle.GetCurrentColor.html), [mesh snapshot](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/ParticleSystemRenderer.BakeMesh.html).

VFX Graph needs a separate capture job. Unity’s public `VisualEffect` API exposes system information and property bindings, not a general read of every simulated particle attribute. Do not assume an exposed GraphicsBuffer is the graph’s internal simulation buffer. [VisualEffect API](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/VFX.VisualEffect.html).

For each shipped graph family, identify its GPU attribute buffers, live-index list, output expressions and completion point. A D3D11 compute adapter should translate those buffers into tracer records after simulation completes. Read back only small diagnostic samples. Prove positions and final rendered sizes/colours against the game before relying on the adapter. If internal buffers cannot be identified, capture the generated output geometry where available; this still does not solve missing volume data.

A proposed per-frame record contains:

`system ID, particle ID/generation, frame sequence, current/previous pose, velocity, lifetime, material adapter, texture references, geometry/medium parameters, emitter identity`.

All particles, lights and density fields must describe the same logical frame. Do not pair fresh flames with last frame’s emission distribution. Do not use array positions as stable particle identities across births and deaths. When identity cannot be recovered reliably, invalidate motion history.

Keep simulation running when suppressing HDRP colour passes. Disabling an entire renderer or VFX component may stop needed output work; that behaviour must be measured. Off-camera simulation and bounds also need testing.

---

Add one medium traversal contract beside the tracer’s surface traversal.

At a point, it returns extinction `σt`, scattering `σs`, emission `j`, scattering direction distribution, refractive index and motion. Between two events, transmission is:

`T = exp(-∫ σt ds)`

A camera or bounce path finds the next surface, then samples possible medium interactions before it. It accumulates emission, samples scattering and continues. A shadow connection tests geometry and transmission through every intervening medium. Mirror and glass/water paths use the same operation on each successive segment. Surface interiors select their own medium, so outdoor fog is not accidentally integrated inside solid glass.

Use null-collision sampling for varying density: sample against a conservative density upper bound, then reject fictitious collisions. Use an unbiased transmission estimator for shadow connections. This avoids making a fixed number of march steps determine the final picture. Homogeneous and analytic height-fog segments can use exact integrals. [Volume transport](https://www.pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/The_Equation_of_Transfer), [volume sampling methods](https://www.pbr-book.org/4ed/Light_Transport_II_Volume_Rendering/Volume_Scattering_Integrators).

Acceleration structures may skip proven empty space. They must not average away density detail. Overlapping physical media add their coefficients; HDRP local-fog blend operations are evaluated separately, in their authored order. An invalid density upper bound is a correctness failure, not permission to clamp density.

There is no fixed particle, layer, light or scattering-event cutoff. Queue overflow spills into another batch. Russian roulette may terminate paths with compensating weights. Every contributing emitter retains a nonzero sampling probability. The existing finite bounce policy is a dependency on PT-66; do not introduce a new volume-depth limit.

A flame’s light sampler draws positions from its actual emitting volume. Camera visibility, reflection visibility and room illumination evaluate the same emission function. Combine explicit emitter sampling and path encounters with correct probability weights so energy is counted once. Start with an ordinary path-tracing reference; extend ReSTIR reuse only after this reference passes.

Pair each migrated flame with its source light by identity. Remove that light’s independent surface and fog contribution when the flame takes over. Its authored power may calibrate the flame if no calibrated emission exists, but record that assumption and apply it to the visible flame too. Do not keep a bright sprite and a separately tuned invisible lamp. Do not suppress neighbouring emissive objects because they are nearby.

Heat haze requires tracing curved paths through the refractive-index field. Use adaptive integration with a numerical error tolerance. Light connections through it need a valid bent-path solver and its weighting; a straight shadow ray plus a bent camera ray is incomplete. Until that connector exists, retain a correct path-continuation estimator for the affected transport. Screen-colour offsets and copied normal-map ripples do not qualify.

The denoising input must contain the complete traced scene. For surface particles, write the actual hit’s depth, normals, albedo, roughness and motion. For volumes, retain separate renderer-owned records for transmission, scattering depth/distribution, velocity and history confidence. Smoke in front of a wall has no single surface normal or motion vector that describes both.

RR receives the complete colour input. Its conventional surface guides remain truthful surface guides; use additional confidence/reactivity inputs only where supported. NRD/RRG adapters must preserve medium contributions even where their documented filters cannot handle them. Unsupported volume filtering is an explicit gap, not a fabricated wall normal. All terms enter the denoising stage together; no later particle or fog draw may add missing scene content.

The frame-age problem in PT-71 item 10 remains relevant. HDRP’s opaque depth and motion cannot correctly place independently moving transparent effects. Carry the tracer’s effect motion through composition and measure the result; do not declare moving particles complete while they slide with the wall.

---

The recommended fog implementation evaluates the game’s density in world space and traces its lighting along rays.

Read the gameplay camera’s blended `Fog` state once per frame. Enumerate local fog objects directly, including those outside the camera frustum. Preserve transforms, masks, animation, scattering colour and ordered blending. Use the same world state for a mirror; do not substitute a different Volume-stack evaluation at an imaginary reflected camera.

Validate density independently of lighting by comparing optical depth along known segments. Use shipped HDRP constants and evaluations as the reference. For example, the current plugin uses a height scale of `0.144765 × layer height`; verify that against the running version rather than changing it from a documentation description.

The colour HDRP adds as distant fog is already a lighting approximation. Its tint/mip controls are not evidence of volume emission. Preserve the authored density and scattering properties; replace ready-made distant-fog colour, ambient-probe fill and multiple-scattering brightening with traced illumination. Exact final pixel equality with HDRP’s lighting is neither promised nor a reason to keep that lighting.

| Approach | Requirements and cost | Decision |
|---|---|---|
| Trace density along every path | Global/local coefficient evaluator; medium sampling; traced visibility; emitter and sky sampling. Cost grows with density evaluations and path events. | Build this. It covers mirrors, hidden space, glass and water. |
| Feed traced visibility into HDRP fog lighting | Hooks before integration; precise froxel coordinates, light identities, exposure and ownership; D3D12/D3D11 synchronization. | Insufficient as the final design. A camera grid lacks general off-camera coverage and direction-dependent scattering. HDRP still owns transport and filtering. |

The earlier `fog.md` estimate of roughly 1–2 ms and 60 MB applies to a proposed camera-grid method. It is not a measured budget for this design. Quarter-grid updates and an off-camera colour fallback must not become its foundation.

Texture access is a separate prerequisite. Verified: `texture_share.cpp` currently opens plain 2D textures, excluding arrays, cubes and 3D resources. The previous mirror-fog report flattened a copied 3D lighting buffer. That does not satisfy the no-texture-copies ruling.

For unshareable density textures, the correctness-first D3D11 route is a batched coefficient evaluator. D3D12 submits sample positions; D3D11 samples the game’s original textures/material inputs and returns evaluated coefficients in shared query/result resources. This transfers query results, not a replacement density texture. Procedural materials need an adapter for their actual density calculation.

Prove this route with a 3D-mask test before building around it. It adds queue round trips and may be costly. Keep resources unchanged until their frame’s queries finish; use one signalling queue per fence. If practical D3D11 access fails, document that result for the already conditional D3D12 migration decision. Do not quietly introduce a copied atlas.

Sun, sky and real emitters illuminate the medium with traced visibility and medium transmission. Replace existing height-fog attenuation at light evaluation with the common transmission calculation so it is applied once. No 64/128 m volumetric cutoff may terminate secondary-ray fog.

“Fill Light Fog” stays On by default. It is the owner’s explicit art-directed exception to physical lighting. Give these lights a `FogOnly` transport tag. Their authored fog contribution is evaluated at volume interactions, with traced visibility. It may be seen through reflection/refraction, but must not enter diffuse surface lighting, surface GI caches or caustic deposits. Track that contribution separately to prevent indirect leakage. A test must show visible fog and its mirror image while a diffuse wall receives no fill-light illumination.

---

Build this in small jobs. Each numbered row is one agent assignment; repeated shader families get separate follow-up assignments.

| Job | Offline check in `pathtracing_gpu_test.cpp`, without denoising | In-game proof and removal |
|---|---|---|
| 1. Inventory and capture contract | Round-trip known particle records, identities and frame sequences; detect a missing record. | `candle`: identify every contributing renderer/pass. Remove nothing yet. |
| 2. Ordinary particle capture | Birth/death, local transforms, animated size/colour and motion match a known sequence. | `campfire`: captured counts and positions match. Retire HDRP colour only for fully represented outputs. |
| 3. One VFX family adapter | Compare GPU-decoded attributes/output against independently generated known values; include off-camera particles. | Use an inventoried effect in `campfire` or `candle`. **Assumption:** either contains VFX Graph; otherwise no proof is claimed. |
| 4. Medium transport core | Homogeneous transmission against `exp(-σt d)`; overlapping volumes; emission integral; multiple scattering against an independent reference. | `fog`: debug density/transmission only until lighting is complete. |
| 5. Global and local fog capture | Compare density at points and along rays, including ordered overlaps and animated 3D masks. | `fog`: same density at rest, including off-camera volumes. |
| 6. Fog lighting and fill provenance | Occluder cuts a beam; medium behind a mirror remains present; fill-only fog leaves a diffuse wall dark. | `tavern-room`, then `night`: preserve haze and far silhouettes. Remove HDRP fog lighting/composition and adaptation. |
| 7. One flame/smoke family | Flame visibility and sampled power agree; smoke shadows receivers; no doubled light after pairing. | `candle`, then `campfire`: remove migrated source-light proxies and glow colour passes. |
| 8. Rain, dust and remaining transparent families | Drop refraction/reflection, dust scattering, mesh transmission and moving shadows; one family per job. | PT-29 records rain in `npc-day`. No dust scene is confirmed. The five requested scenes cannot honestly prove absent effects. |
| 9. Heat haze | Known index field bends rays correctly; reversed paths agree; refinement converges; off-screen background remains visible. | `candle`: remove its screen-distortion dependence. |
| 10. Guides, composition and final retirement | Moving foreground smoke/sparks over a static wall; cuts and resource replacement; no missing or duplicate terms. | `candle` and `tavern-room`: None first, then RR/NRD; repeated Off/On restoration. |

For numerical checks, use independent analytic answers or sufficiently converged undenoised references. A proposed starting tolerance is 1% regional mean error with reported sampling uncertainty; increase samples when uncertainty is larger. Mutation-check each test by breaking its feature. Counts, identities and frame ownership require exact checks.

The harness’s 16 frozen undenoised frames are an initial diagnostic, not automatic convergence proof. First verify the effect is actually frozen. Use live sequences for motion. Read the pictures, exposure and gain logs. RR changes also require `tgup_gpu_smoke_with_pt`; offline TGPT tests do not establish RR correctness.

Retirement must happen with the replacement:

- Fog migration deletes `FogPass`, its host and pending state, `EventFog`/`SetFog` implementation, `TGPT_FOG`, pre-fog copies, histograms and `fog_automatic`. Remove the Fog Adaptation row, option and obsolete harness recipe. Keep Fill Light Fog, backed by `FogOnly`.
- Replace `fogged`, `fog_color`, `fog_sky` and split fog/surface gain compensation. Retain useful analytic density integrals. Remove copied lit-fog machinery if the pending mirror work introduces it before this job lands.
- Emitter migration removes migrated uses of `DefaultRadius`, bounded `MeasuredRadius`, centre relocation and shortened “own light” shadow reach. Replace proximity-based `OwnSurfaces`/`EMITTER_PAIRED` suppression with emitter identity.
- Transparent migration removes its HDRP colour, distortion and per-light gain patches. Retire `SuppressDistortion`’s workaround implementation when the replacement render scheduling excludes HDRP distortion. Keep simulation and restoration intact.
- `pt_hdrp_shadow`, water-specific passes and global HDRP-lighting suppression have other users under PT-71. Delete them when the last user migrates, not prematurely. Update source checks and ABI bindings with every retirement.

Record remaining access, material and guide gaps under PT-67/PT-37/PT-41 and PT-71. A family is migrated only when every ray sees it, its energy is counted once, its HDRP shading is gone, and turning PT Off restores the game.

Only two later decisions belong to the owner:

- After migration, should the candle’s optical bloom stay as strong as the game’s, or be subtler? Default: preserve its apparent strength while keeping bloom separate from emitted light.
- Which finished scene should receive the first appearance review: candle, campfire or fog? Default: candle, because it covers the reported noisy rectangle, flame emission and heat haze.

Neither answer is needed to start the inventory, capture or transport work.