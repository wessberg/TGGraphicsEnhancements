# Path Tracing requirements

PT targets a complete traced renderer; it is tabled and incomplete. [PT-STATE.md](PT-STATE.md) describes implementation, not these requirements.

Own camera rays for every pixel. Camera/secondary rays see matching geometry, original-resolution game textures, materials, decals, deformation and emitters. No distance/layer/object/light cap may change convergence. Existing caps remain defects. Budgets may affect variance or speed, not the final picture.

Hair, eyes, glass, water and particles belong in the main picture with their own depth/motion/normals/material guides, not a denoiser-blind layer. Finish migration before old-route comparisons; remove obsolete workarounds/settings/tests as replacements land. HDRP keeps post, upscaling and UI. Simulate once, draw once. Off-screen effects must illuminate and reflect.

Trace physical scattering, reflections, refraction, haze and caustics. Use real waves/material properties, not screen-space substitutes or ready-made patterns. Real emitters include crystals, magic, orbs and fireflies. Reject artist fill/bounce light from surfaces, not unnamed physical sources. Classify by attachment/content and use real emitter size.

Keep authored fog density/colour/volumes. Every emitter lights mist indoors/outdoors, with all scattering orders. Preserve accepted lamp glow; show comparisons before changing it. Use visible sun/moon direction, physical distant haze and moving cloud shadows. Undimmed ambient-sky fog is not a physical reference.

Water must be physically consistent and visually convincing; rasterized colour parity is not required. Missing shallow illumination, fireflies, blotches, missing waves and seams are defects. Mirrors reproduce camera-quality world/material/character detail. Moving cloth/foliage changes reflections/shadows; grass shadows are soft/partial.

Keep SER and Mega Geometry on where supported. Diagnose slower accelerated integrations; plain paths are hardware fallbacks or measuring references. Micromaps conservatively bound every alpha reader. Follow vendor documentation/samples and audit departures.

NRD takes full demodulated light, real distances, shading normals and correct motion. RELAX selected; REBLUR motion comparison pending. Glass/water starts with recorded way B, then A: recover precise meanings from archived `docs/agents/psr-2.md` before implementation. SH detail recovery/RTXDI default comparisons pending.

Denoisers: None/NRD/RRG/RR; prefer RR then RRG then NRD where available. RR works alongside NR. RR uses NVIDIA Default when F, otherwise F; recheck after updates. No extra temporal filter over Neural Rendering.

First make speed/VRAM practical alongside correctness, then close rendering gaps. Optimize without changing the picture first. Sampling/path-length/quality/responsiveness changes need images and measured costs. 55 FPS is a target, not a result. Default full-resolution rays; explicit half/quarter choices. Indirect Color 1, Reflection Boost 0.1; metal/armour get more than stone, skin/foliage exempt.

One scene/defect per short iteration. Compare undenoised/NRD before RR. Motion blur off, native-resolution pictures. Hold exposure, load order, streaming and foreground state fixed. Frozen fixtures cannot establish motion; synthetic checks cannot establish accepted appearance or untested hardware support.
