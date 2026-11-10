# Surface transport notes

Camera and secondary rays share material/hit code. Current status is in [PT-STATE.md](PT-STATE.md).
The full shader investigation and measurements are retained in the [archive](DEVELOPMENT.md).

Glass uses Fresnel/GGX, Snell refraction, total internal reflection, material IOR and real-segment absorption.
Do not restore guessed thickness, 15m eligibility or 1m exit searches. Mixed lids/labels and open meshes need
correct material/volume classification. NRD glass/water branches remain separate unresolved work.

Skin/solid subsurface transport uses traced walks with the authored diffusion profile and mask, not screen-space
Burley filtering. Human skin is authored Thin; do not assume all scattering profiles are thick solids.
Shading normals go to NRD; shadow origins and geometric normals have separate jobs.

Camera rays hit hair, brows and lashes directly, using remapped stochastic coverage and their own motion.
Every opacity reader may use different mip/anisotropic footprints; micromap coverage must conservatively bound
them all. Eye and cornea shaders need separate texture and refraction handling, including the layers over the sclera.

Original textures/material programs are the main route. Atlas readers, fallback leaf/cloth terms, terrain blending
and original-resolution reflection fidelity remain open. Do not infer a complete material migration from a matching
camera image. Real emitter shape, original material values and reflection guides must hold in mirrors too.

Reflection Boost includes armour/metal and exempts skin/foliage; metal gets more mirror response than stone.
GGX denominator guards must not flatten sharp peaks. HDRP Unlit uses f0=0, roughness1 and correct Schlick f90.

Water uses displaced game-wave geometry for both camera/secondary paths. Caustics use actual traced waves;
the game's ready-made ripple pattern and flat reflection glow were rejected substitutes. Remaining seabed,
foam/seam, low-sun and rare-bright-sample defects are tracked separately.
