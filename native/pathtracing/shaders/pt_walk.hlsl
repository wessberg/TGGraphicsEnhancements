// SPDX-License-Identifier: MIT
// Light inside subsurface materials, for the pixels the camera sees (PT-40, PT-43): wax, skin, teeth. Such a pixel's diffuse
// light is not the light that falls on it: it is the light that went into the thing somewhere, was scattered and absorbed
// inside, and comes out here. By reciprocity that is one random walk from the pixel into the traced mesh (pt_common
// subsurface_walk, the medium from the material's diffusion profile and the pixel's color) and the light gathered where the
// walk leaves the mesh: the sun's and the lamps' with shadow rays (next-event estimation, as a path vertex takes them), and one
// cosine-distributed ray for the sky's and the bounced light (a path as pt_gi's, ending on the radiance cache). A candle's
// pockets and folds, which nothing lights from outside, glow with what went in at its top; a nostril or an ear with a lamp
// behind it glows red, because the walk comes out on the lamp's side and the blue was absorbed on the way.
//   It is the tracer's own in place of HDRP's screen-space scattering (a Burley blur of the light on screen, which only knows
//   what the camera sees) and HDRP's thin-object transmission (a thickness map and one exponential): the owner, 2026-10-07,
//   "everything should be path traced". The pixel's diffuse signals (pt_di_spatial's direct light, pt_gi_spatial's bounced
//   light, both divided by the diffuse color) are replaced by the walk's: what it carries over the pixel's color (the
//   composite multiplies the color back in, so a texture's detail stays sharp) times the light at the exit. The pixel's
//   highlight stays where the camera sees it (pt_di_spatial's specular, pt_specular).
//   Which pixels: those whose own camera ray (PT-59, pt_primary) stopped at a material light walks inside (pt_common
//   subsurface_material: one the game's own shader scatters light under the surface of, TGPT_MATERIAL_SUBSURFACE and its
//   profile's shape; not a leaf or cloth, which pass light as thin sheets), on a piece of the mesh that has an inside (a
//   material drawn from both sides: only where it closes a volume), as often as the game's subsurface mask at the point
//   says (own_walks: decided at the hit, pt_primary write_traced), exactly as a path's vertex on it is told
//   (subsurface_vertex; pt_common "who walks"). The walk's entry is that hit itself. HDRP's G-buffer is not asked: until PT-58
//   the pixels were HDRP's (its subsurface features and its profile by G-buffer index, a traced mesh looked for under HDRP's
//   surface); on that route (TGPT_OPTION_GBUFFER_SURFACES) nothing walks now. Elsewhere the pixel keeps its light as a plain
//   surface's.
//   Unbiased, one walk a pixel a frame; its noise is the denoisers' to take out, as the bounced light's is. ReSTIR's
//   samples for these pixels are not used (they are the light at the pixel, not at the exit): reusing exits between
//   pixels and frames is the next step (Werner, Schuessler and Dachsbacher 2024, "ReSTIR Subsurface Scattering").
#include "pt_common.hlsli"

// The walk's entry: the triangle the pixel's own camera ray stopped at (pt_primary write_traced), where that triangle's
// material is one light walks inside (subsurface_material). Its point and its triangle's own orientation are the surface
// record's (the orientation at 22 bits, a twentieth of a degree), its material the one the ray met (own_material); a camera
// ray stops at front faces alone where a material is drawn one-sided, and one drawn two-sided is walked only from its front
// (subsurface_hit). pixel: the ray cone's width there. False: no such pixel (and every pixel of the route before the
// tracer's own rays).
bool walk_own(uint2 p, Surface s, float pixel, out Hit entry, out float3 shape) {
    entry = (Hit)0; shape = 0.0;
    if (!subsurface_on() || !s.valid || !s.deferred || s.glass || s.strands || s.hair) return false;
    uint material;
    if (!own_material(p, material) || !subsurface_material(material, shape)) return false;
    // By the mesh and the point too (pt_common "who walks"): the ray's own hit said so this frame (YARD_WALKS, pt_primary
    // write_traced). In the frames it did not, the pixel keeps its light as a plain surface's (pt_di's, pt_gi's).
    if (!own_walks(p)) return false;
    entry.position = s.position; entry.normal = s.geoNormal; entry.rounded = s.geoNormal; entry.color = 1.0;
    entry.material = material; entry.front = true; entry.footprint = pixel;
    return true;
}

// The normal map's normal where a walk's exit is the very surface the camera sees at its place on screen: the tracer's own
// record there (the traced material's normal map, pt_primary write_traced), within 1 mm and 0.2 % of the distance, and
// facing as the traced surface does there (within 60 degrees: a normal map tilts it less). A candle is a few centimetres
// thick: an exit on its far side must not take the normal of the side the camera sees (taken within 3 cm, the light on a
// cylinder's flanks was lit by the wrong face: 0.84 of a reference walk's light in tgpt_gpu_test).
bool exit_normal(float3 position, float3 geometric, out float3 normal) {
    normal = geometric;
    float2 uv;
    const float3 rel = position - g.cameraPos.xyz;
    if (!project(rel, uv)) return false;
    const uint2 q = min(uint2(uv * float2(g.size.xy)), g.size.xy - 1);
    const Surface seen = load_surface(q, g.surface.x, g.surface.z, g.material.x);
    if (!seen.valid || !seen.deferred || seen.strands || seen.hair) return false;
    if (length(seen.position - position) > 0.001 + 0.002 * length(rel)) return false;
    if (!(dot(seen.normal, geometric) > 0.5)) return false;
    normal = seen.normal;
    return true;
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    const Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    float3 shape;
    Hit entry;
    const float pixel = pixel_angle() * s.distance;
    ray_cone(pixel, 0.0);
    if (!walk_own(p, s, pixel, entry, shape)) return;
    Rng rng = make_rng(p, g.size.z, 21);
    RWTexture2D<float4> sun = ResourceDescriptorHeap[g.signals.z];
    RWTexture2D<float4> diffuse = ResourceDescriptorHeap[g.signals.y];
    float3 direct = 0.0, bounced = 0.0;
    WalkExit e;
    if (subsurface_walk(entry, s.albedo, shape, rng, e)) {
        // What the walk carries, over the pixel's color (the composite multiplies it back in). A channel the color has none
        // of carries nothing.
        const float3 gain = e.throughput / max(s.albedo, 1e-3);
        const float offset = 0.001 + length(e.position - g.cameraPos.xyz) * 0.0002;
        // The exit as the camera sees it where it does (its normal map), else the traced mesh's smooth normal.
        float3 shading = e.shading;
        float3 drawn;
        if (exit_normal(e.position, e.normal, drawn)) shading = drawn;
        // The sun and the lamps at the exit, as a white diffuse surface there takes them (irradiance over pi).
        direct = finite_radiance(gain * direct_diffuse(e.position, e.normal, shading, float3(1.0, 1.0, 1.0), offset, rng, 0.0));
        // The sky's and the bounced light there: one cosine-distributed ray (its light is the estimate, as pt_gi's rays').
        const float3 dir = cosine_sample(shading, rng.next2());
        if (dot(dir, e.normal) > 0.0) {
            g_pathFrom = e.position; g_pathGathers = true;
            Hit hit;
            float3 tint;
            ray_cone(pixel, DIFFUSE_SPREAD);
            float3 arriving;
            if (trace_bounce(e.position + e.normal * offset, dir, g.params.y, hit, tint)) arriving = finite_radiance(shade_path(hit, dir, g.settings.z, rng, false));
            else arriving = finite_radiance(sky_radiance(dir));
            bounced = indirect_color(gain * tint * arriving);
        }
    }
    // (Alpha: the hit distance pt_gi_spatial left, which the denoisers read.)
    sun[p] = float4(to_storage(max(direct, 0.0)), 1.0);
    diffuse[p] = float4(to_storage(max(bounced, 0.0)), diffuse[p].a);
}
