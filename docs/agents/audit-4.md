# audit-4: RTXDI, Shader Execution Reordering, Opacity Micromaps, RTX Mega Geometry: are we using them right? (round 20)

Done by the coordinator itself on 2026-10-11 (no subagents that night: the owner's ruling). Brief:
`docs/agents/briefs/brief-audit-4.md`. The owner, 02:55: "I especially want you to to absolutely certain we're doing
everything correctly in terms of RTXDI, Shader Execution Reordering, Opacity Micromaps and RTX Mega Geometry. Our target
FPS continues to be so much higher than the territory we're in, and I'm sure there are some clear bottlenecks here and
there." Every number below comes from a run of the harness with the world running, no denoiser, the card free and the
game in front, unless it says otherwise.

## In short

Where a traced frame went in a town at the start (run `20261011-051727`): about 85 ms = the game's own work on the
card 23.5 (it takes turns with ours) + the tracer 63: bounced light 20.6, reflections 20.5, direct light 6.4, the
scene stage 6.2 (plants' wind poses 4.7 of it), caustics 4.8, camera rays 3.1, the bounced light's reuse 1.7.

At the end (run `20261011-064558`, everything below that is marked the rule): the tracer in the tavern 32 -> 30 ms,
the town 64 -> 57 (bounced light 15 to 16, reflections 17 to 21), the forest 83 -> 74; the pictures the same.

| Technique | Used right? | What is open | Its size |
|---|---|---|---|
| Opacity micromaps | Yes in form (4-state, special indices, every cut-out mesh, plants since round 20, leaves opaque but to light rays since round 20). Not in fineness: level 4 left 59 % of the plants' pieces to the alpha test | leaves and grass at level 6: THE RULE since 06:10 (35 % unknown); hair (M4); the other cut-outs | forest 82.8 -> 75.5 ms, town 64.5 -> 62.2; +130 to 160 MB |
| Shader Execution Reordering | The reflections: yes (the material as the key; it beats no key by 6 to 13 ms in town and forest, the tracer's own probe). The bounced light: the call was there but told the driver nothing (no key, an empty hit object) | the bounced light keyed by material: THE RULE since 06:25; the water's path (S5) | tavern -1.6 ms, forest -4.2 |
| RTXDI | Its reuse passes are where NVIDIA's sample has them since round 19 (R1). The disocclusion boost can never fire (R3: target history 0), noise only | R3; the light grid stays (R7: no gap) | ~1 ms passes; R3 is noise |
| RTX Mega Geometry | Not used: its first piece is in and switched off, by the owner's order of work (later) | the plants' refits are its target | the plants' poses 4.7 ms a frame in town |

And two found on the way that are not one of the four:
- every shadow ray that nothing stopped traces one more ray straight up to ask whether its start lies in water
  (`water_medium`); round 19's skip above the water's top never switched on (its check refused): the fix and the
  measure are ON TRIAL (`Speed trial` 1);
- the caustics cost a room 3.9 ms and make nothing there (PT-114).

## Opacity micromaps

What we do (`pt_omm_bake.hlsl`, `scene.cpp` record_omm): the 4-state format; a micromap for every alpha-tested
triangle of a mesh that may have one (not glass, hair, camera-only surfaces); DirectX's special indices where a whole
triangle is bare or covered; the alpha's bounds per 4x4 texels for pieces that cover many texels; the structures
compacted; the plants in the wind's pose linked to their meshes' maps (M1, round 20); leaves' covered pieces opaque, a
light's ray meeting leaves in a pass of its own that hands their pieces to the chance of passing (M2, round 20). By
NVIDIA's and Microsoft's documents that is the right form.

What was wrong is the fineness. A leaf card's triangle covers hundreds of texels a piece at level 4, so most pieces
straddle a leaf's edge and stay "unknown": 59 % of the plants' pieces in a town, and every ray that meets such a piece
runs the shader's alpha test. What that cost (run `20261011-053411`, a measuring step with the plants' cut-outs solid):
the forest 19 ms of 83, the town 3.4 ms (and 8 more for the other cut-outs).

The trial (run `20261011-055446`, the world running, clean; recipe `omm-level`), the tracer's ms:

| | level 4 | leaves at 5 | leaves at 6 |
|---|---|---|---|
| forest (frozen) | 80.5 | | 72.7 |
| forest (running) | 83.7, again 82.5 | 79.2 | 75.5 |
| town (running) | 65.0, again 63.9 | 63.3 | 62.2 |
| the plants' pieces unknown (town) | 59 % | 46 % | 35 % |
| micromaps' memory, forest / town | 32 / 44 MB | 58 / 75 | 163 / 200 |

The frozen pictures at 4 and 6 are the same (forest 64.1 and 64.1 levels, town 51.0 and 51.0; flicker and boil
equal). MADE THE RULE the same morning: leaves and grass at level 6, the rest at 4 (`Speed trial` 128 and 256 measure
4 and 5 again).

Still open: hair has no micromaps (M4: the tavern's 437,000 hair triangles go to the test); the still meshes' other
cut-outs (fences, ivy, cloth) are 25 % unknown at level 4; a level per triangle by its texel size would put the bytes
where they pay (M7's full form; DirectX allows a level per micromap).

## Shader Execution Reordering

What NVIDIA's guidance and the HLSL specification want: regroup where threads are about to diverge (after a ray, before
its hit is shaded), give the driver something to sort by (a hit object from `HitObject::TraceRay` or `FromRayQuery`, or
an empty hit object with a coherence hint of up to 16 bits), carry little across the call, and never call it inside a
loop of a big pass.

What we do:
- The reflections (`pt_specular.hlsl` 132): one call after the ray, by the material the ray met and whether the pixel
  feeds the radiance cache (16 bits), the driver's stack 128 bytes a thread. The tracer probes the other choice (no
  hint) 8 frames in every 128 and keeps the faster: tonight in town and forest the material won every probe by 6 to 13
  ms (`reflections' sort` in the log). As the guidance wants.
- The bounced light (`pt_gi.hlsl` 126 to 150): since 10-10 one call after the first ray, outside the rays' loop, the
  driver's stack 0 bytes: right in place and in what it carries. But it was given NO key: an empty hit object and 0 hint
  bits, so the driver had nothing to sort the threads by. With the reflections' key (run `20261011-060805`, the same
  picture): the tavern 32.2 -> 30.6 ms, the forest 82.3 -> 78.1. THE RULE since 06:25; the stack stays 0 bytes.
  The study of 10-10 that found a key 20 ms dearer was made while the pass still carried 2,560 bytes a thread.
- Hit objects: we sort by a key of our own and not by a hit object, because the tracer's rays are inline queries with
  no hit shaders; a hit object made from the query (`FromRayQuery`) would name no shader to sort by. Measured on 10-10
  ("by a hit object from the ray query"): no better than the key. Fine as it is.
- Open: the water's path regrouped at every turn of its loop, in a pass of its own (S5, the beach's 115 to 120 ms; built
  behind CMake `TGPT_REGROUP_WATER`, never run in the game). The camera rays (`pt_prepare`, 3 to 5 ms) are a compute pass:
  their rays are coherent, their shading is not; a regrouping there would need the pass as a ray generation shader.

Verdict: the reflections were right; the bounced light was regrouped without a key and is now keyed. Both carry little.

## RTXDI

What we do (`pt_rtxdi.hlsli`, `pt_restir.hlsli`, `pt_di*.hlsl`, `pt_gi*.hlsl`, `tracer.cpp` 3600 to 3655):
- ReSTIR DI: initial candidates from our light grid (exact weights from every light that matters to a cell; RTXDI's own
  presampled tiles hold a random few hundred: compat-1 R7, no gap), temporal reuse with permutation sampling, the
  boiling filter (0.2), spatial reuse (4 neighbours, the Spatial Samples row), Basic bias correction by default, the
  ray-traced one with the hidden direct-light visibility. History 20 frames.
- ReSTIR GI: this frame's sample in `pt_gi` (the reordered pass), its reuse across frames in `pt_gi_reuse` (a compute
  pass of its own since 10-10, as NVIDIA's sample has it: compat-1's R1, done), the boiling filter, the spatial reuse
  with a visibility ray for a reused sample. History 8, age 30.
- Cost in the town: direct light 6.4 ms (both its passes), the bounced light's reuse 1.7, its reuse across frames
  inside the bounced light's 20.6 (not split out). The forest: 9.7 and 2.8.

Open:
- R3: `targetHistoryLength=0` (tracer.cpp 3617), so the boost to 8 neighbours for a pixel with little history (a
  disocclusion) never fires. RTXDI's noise guide gives the boost as the way such a pixel converges. Noise only, one
  number; not changed tonight (it changes what the denoisers get after a camera move: the owner's eye).
- R2 (the ray-traced bias correction compiled into every RTXDI pass, the default never running it): harmless since R1
  (its own small passes).
- R8: every camera pixel still asks "is the camera under water" with its own ray straight up (`pt_primary.hlsli` 333):
  two million identical rays a frame. Tenths of a ms; it goes with `Speed trial` 1 above the water's top.
- Not a gap: our grid instead of ReGIR, and our own sun handling.

Verdict: used as NVIDIA's sample uses it, but for R3. The time is not in RTXDI's passes (8 ms of 63 in the town); it
is in the paths the bounced light and the reflections trace and shade.

## RTX Mega Geometry

What we do: its first piece (the partitioned top level, CMake `TGPT_MEGA_GEOMETRY`) is built and switched off: it saved
0.03 ms (PT-109). The structures' work it targets, measured tonight in the town with the world running ("PT
structures" line): the plants' wind poses and refits 4.7 ms a frame (624 groups, 6.4 M triangles a frame), the water's
pieces 0.66, the characters 0.42 (23 built anew and 68 refitted a frame), cloth 0.08, the top level 0.22 over 16,171
instances. In the forest: 5.1 ms the whole scene stage.

What it would replace: the plants' refits by clusters stamped from templates (NVIDIA's sample's ratios: 2 to 3.5 ms
and about 220 MB less in the town, PT-109's plan). New tonight: NVAPI's cluster structures take micromaps
(`opacityMicromapArray` with `..._FLAG_ALLOW_OMM`, `nvapi.h` R615 21080 and 21110), so the plants would keep their
micromaps (the forest's 19 ms of alpha tests say they must).

Verdict: not used, by the owner's order of work ("a later optimization", 10-10); its target grew from 3.9 to 4.7 ms
since M1 put micromaps into the plants' refits. Today it is the third-largest picture-neutral prize in a town
after the alpha tests and the bounced light's regrouping (if that trial wins).

## Other bottlenecks found on the way (not one of the four)

| What | Where | Size (town unless said) | State |
|---|---|---|---|
| The decals' lookup tree was split by draw order, so its boxes spanned the scene and every hit walked most of its ~130 nodes | `scene.cpp` upload_decals, `pt_common` apply_decals | round 19 measured ALL traced decals at ~4 ms in a room | rebuilt by place, the same decals in the same order: THE RULE (in run C's build; its own share not split out) |
| "Which water is this point in": one more ray straight up after every shadow ray nothing stopped | `pt_water_surface` water_medium | up to 2 rays a path vertex | the skip above the water never switched on; fixed (`waterCeiling`); measured: no gain beyond a reading's wander; fewer rays, the same picture: THE RULE |
| The caustics trace the whole sea's grid under a room that floats 470 m above it | `pt_caustics` | the tavern 3.9 ms for nothing | PT-114, a design question |
| Rough reflections: a ray each, where "Glossy Surfaces" uses a stand-in | `pt_specular` | 8 ms | PT-115, a proposal (the bounced light's own sample) |
| The radiance cache's feeding ray (a quarter of the pixels trace one more bounce) | `pt_gi` cache_feeds | 5.6 ms (tavern 2.9) | fewer feeds = more noise in the cache: the owner's choice, with numbers |
| The camera pass forces every triangle a camera ray crosses through the shader (to pass back faces and stand-ins), which also leaves the micromaps unused there | `pt_primary.hlsli` 226 | part of 3 to 5 ms | a two-step query would find the same surfaces; not built |
| The game's own card time: 8 ms before our composite frozen, 12.3 with the world running, ~11 after it | PT-107 | ~23 ms | needs marks between HDRP's passes; not built |
| The plants' wind poses and refits | Mega Geometry's target | 4.7 ms | later (the owner) |
