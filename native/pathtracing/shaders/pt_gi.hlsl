// SPDX-License-Identifier: MIT
// ReSTIR GI, initial samples and temporal reuse (Ouyang et al. 2021). Each deferred pixel traces
// cosine-distributed rays from HDRP's surface (one to eight, the plugin's rays per pixel); the path
// vertex each finds is shaded (sun, a light that reaches it, more diffuse bounces) and the candidates
// compete by their target function. The previous frame's reservoir at the pixel's reprojected position
// competes with the winner, weighted by the target function at this pixel and the reconnection
// Jacobian, and the survivor is stored for the spatial pass and the next frame.
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

void gi_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    RWStructuredBuffer<GIReservoir> cur = ResourceDescriptorHeap[g.material.y];
    RWStructuredBuffer<GIReservoir> prev = ResourceDescriptorHeap[g.material.z];
    uint index = p.y * g.size.x + p.x;
    Surface s = load_surface(p, g.surface.x, g.surface.z, INVALID);
    if (!s.valid || !s.deferred) { cur[index] = (GIReservoir)0; return; }

    Rng rng = make_rng(p, g.size.z, 1);
    float offset = ray_offset(s.distance);
    float3 origin = s.position + s.geoNormal * offset;
    const uint rays = clamp(g.grid.w, 1u, 8u);
    GIReservoir r = (GIReservoir)0;
    float wsum = 0.0, selectedTarget = 0.0;
    for (uint k = 0; k < rays; ++k) {
        float3 dir = above_surface(cosine_sample(s.normal, rng.next2()), s.geoNormal);
        GIReservoir c = (GIReservoir)0;
        Hit hit;
        float3 radiance;
        const bool found = trace_from_surface(origin, dir, 0.0, g.params.y, near_back(s.distance), hit);
        PT_REORDER(found ? hit.material : 0xFFFFu); // with SER: the path's shading, grouped by material
        if (found) {
            radiance = clamp_radiance(shade_path(hit, dir, g.settings.z, rng), LIMIT_INDIRECT);
            c.pos = hit.position; c.normal = pack_normal(hit.normal);
            c.radiance = pack_half3(radiance, GI_VALID);
        } else {
            radiance = clamp_radiance(sky_radiance(dir), LIMIT_INDIRECT);
            c.pos = dir; c.normal = 0;
            c.radiance = pack_half3(radiance, GI_VALID | GI_SKY);
        }
        float cosTheta = saturate(dot(s.normal, dir));
        float target = luminance(radiance) * cosTheta;
        // Source density cos/pi: the candidate's weight is target / density.
        float w = cosTheta > 0.0 ? target * PI / cosTheta : 0.0;
        wsum += w;
        if (k == 0 || (w > 0.0 && rng.next() * wsum < w)) { r = c; selectedTarget = target; }
    }
    float m = float(rays);

    if ((g.size.w & FLAG_RESET) == 0 && (g.settings2.y & OPTION_NO_TEMPORAL) == 0) {
        int2 q;
        Surface sp;
        if (reproject(p, s, q, sp)) {
            GIReservoir rp = prev[q.y * g.size.x + q.x];
            // Its path vertex is relative to last frame's scene origin (a sky sample holds a direction).
            if (gi_valid(rp) && !gi_sky(rp)) rp.pos += g.prevOriginDelta.xyz;
            if (rp.M > 0.0 && rp.W > 0.0 && gi_valid(rp)) {
                rp.M = min(rp.M, float(g.settings2.x) * float(rays));
                float target; float jacobian;
                gi_target(rp, s.position, s.normal, sp.position, target, jacobian);
                float w = target * jacobian * rp.W * rp.M;
                wsum += w; m += rp.M;
                if (w > 0.0 && rng.next() * wsum < w) { r = rp; selectedTarget = target; }
            }
        }
    }
    r.M = m;
    r.W = selectedTarget > 0.0 ? wsum / (m * selectedTarget) : 0.0;
    if (!(r.W < 1e20)) r.W = 0.0;
    cur[index] = r;
}
PT_ENTRY(gi_pixel, gi)
