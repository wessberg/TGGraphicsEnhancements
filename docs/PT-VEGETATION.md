# Vegetation and cloth integration

Current baseline: [PROJECT-CONTEXT.md](PROJECT-CONTEXT.md). The original detailed shader/deformation studies
and image measurements are preserved under this path in the [local archive](DEVELOPMENT.md).

Capture Leshy and Vegetation Studio Pro alongside ECS/GameObject geometry. Follow each renderer's LOD and the
game's live deformation. Plants use 16m wind cells; cloth poses per instance. Game wind includes view-dependent
fades; generic sway cannot replace it. Trace moved geometry in shadows/reflections, not just camera colour.

Grass shadows use soft partial transmission. Material properties and authored diffusion profiles supply
transmission; remaining fixed leaf 0.5 / cloth 0.3 terms are fidelity gaps. Do not add a glow that violates energy.

Plant opacity micromaps classify only proven coverage. Grass/leaves use level 6; covered pieces are opaque to
ordinary rays while separate light-ray traversal retains stochastic transmission. Cloth/hair can require unknown
coverage. Top-mip alpha is not proof for anisotropic or changing-mip readers.

Keep stable plant identity across frame-buffer rebuilds. PT-125 fixed retained ranges indexing another frame's
plant data and survived 40 walks. PT-148 still affects retained planning when leaving Mega Geometry trials 4/5/6.
Grouped-plant acceleration work is unmerged and its final worker copy is unavailable; see PT-STATE.

Do not discard shadow-only meshes or unresolved cluster stand-ins: earlier exclusions removed roofs/tunnels.
Do not remove visible grass coverage or cap cloth counts to meet frame time. Optimize sampling/acceleration
without changing convergence and expose remaining capacity gaps in the issue register.
