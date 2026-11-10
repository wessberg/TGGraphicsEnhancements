# Renderer correctness gaps

Requirements are in [PT-DESIGN.md](PT-DESIGN.md); this page tracks the categories to audit, not permission to keep
approximations. Detailed historical investigations remain in the [archive](DEVELOPMENT.md).

| Category | Requirement | Tracking |
|---|---|---|
| Camera first surface | Own camera ray for every pixel | PT-59/PT-71 |
| Complete world | All renderer families, no distance/coverage caps, correct game LOD | PT-50/PT-61/PT-63/PT-83 |
| Motion | Live cloth, skinning, wind in reflections/shadows | PT-49/PT-71 |
| Materials/textures | Game shader behaviour, original textures/resolution, no fixed atlas capacity | PT-45/PT-51/PT-64 |
| Mirrors | Camera-quality geometry/materials including transparent surfaces | PT-46/PT-127/PT-139 |
| Scattering/caustics | Physical transport, real waves, no pattern or screen-space stand-in | PT-14/PT-37/PT-51/PT-81 |
| Effects | Simulate once, trace off-screen particles/volumes/emitters | PT-29/PT-30/PT-31/PT-41/PT-67/PT-112 |
| Main-frame integration | Hair/eyes/glass/water supply their own guides; no post-denoiser surface layer | PT-53/PT-71/PT-127 |
| Duplicate rendering | HDRP retains only post/upscaling/UI | PT-60/PT-112 |
| Acceleration | SER/Mega Geometry on where supported; conservative micromaps | PT-109/PT-110/PT-149 |
| Vendor inputs | Full documented signals/settings; departures explicit | PT-127 |
| Limits | Budgets preserve convergence; remove picture-changing capacities/clamps | PT-50/PT-116/PT-137 |

Already replaced: G-buffer-only camera starting point in the main path, hair overlay, screen-space skin blur,
flat/patterned water caustic stand-ins and bounced-light-only NRD inputs. Some legacy routes/readers remain;
these statements do not close PT-60/PT-71 or establish a complete mirror/material migration.
