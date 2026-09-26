// SPDX-License-Identifier: MIT
// ReSTIR DI, candidates and temporal reuse (Bitterli et al. 2020). The lights whose range
// reaches the pixel (its light grid cell) compete by their unshadowed contribution at the pixel: all of them
// when there are no more than eight, otherwise eight spread over the cell without repeats (cell_candidate);
// every light that can light the pixel is in the cell. The previous frame's choice at the
// reprojected pixel competes with the winner. The plugin keeps every light in a stable slot, so a
// light index means the same light from one frame to the next.
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWStructuredBuffer<DIReservoir> cur = ResourceDescriptorHeap[g.material.w];
    RWStructuredBuffer<DIReservoir> prev = ResourceDescriptorHeap[g.signals.x];
    uint index = p.y * g.size.x + p.x;
    DIReservoir r; r.light = INVALID; r.W = 0.0; r.M = 0.0; r.pad = 0.0;
    uint count = g.settings2.z;
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!s.valid || !s.deferred || count == 0) { cur[index] = r; return; }

    Rng rng = make_rng(p, g.size.z, 3);
    float3 v = normalize(g.cameraPos.xyz - s.position);
    float alpha = roughness_alpha(s.roughness);
    float wsum = 0.0, selectedTarget = 0.0;
    uint first, cellCount;
    bool lit = light_cell(s.position, first, cellCount);
    const uint candidates = lit ? min(cellCount, 8u) : 0u;
    const uint start = lit ? min(uint(rng.next() * cellCount), cellCount - 1) : 0u;
    for (uint c = 0; c < candidates; ++c) {
        uint i = cell_candidate(first, cellCount, candidates, c, start);
        float3 d, sp, l; float dist;
        float t = di_target(i, s, v, alpha, d, sp, l, dist);
        float w = t * float(cellCount); // target / (1 / cellCount)
        wsum += w;
        if (w > 0.0 && rng.next() * wsum < w) { r.light = i; selectedTarget = t; }
    }
    float m = float(candidates);
    if ((g.size.w & FLAG_RESET) == 0 && (g.settings2.y & OPTION_NO_TEMPORAL) == 0) {
        int2 q; Surface spv;
        if (reproject(p, s, q, spv)) {
            DIReservoir rp = prev[q.y * g.size.x + q.x];
            if (rp.light < count && rp.W > 0.0 && rp.M > 0.0) {
                rp.M = min(rp.M, float(g.settings2.x) * float(max(candidates, 1u)));
                float3 d, sp, l; float dist;
                float t = di_target(rp.light, s, v, alpha, d, sp, l, dist);
                float w = t * rp.W * rp.M;
                wsum += w; m += rp.M;
                if (w > 0.0 && rng.next() * wsum < w) { r.light = rp.light; selectedTarget = t; }
            }
        }
    }
    r.M = m;
    r.W = selectedTarget > 0.0 ? wsum / (m * selectedTarget) : 0.0;
    if (!(r.W < 1e20)) r.W = 0.0;
    // Visibility reuse (Bitterli et al. 2020, section 4): the choice is made by unshadowed contribution, so in a
    // chair's shadow the pixel keeps choosing the lamp the chair hides. One shadow ray now; a light this pixel
    // cannot see leaves the reuse, and neighbours and the next frame share only lights that reach them. Without
    // it the lit and the shadowed frames alternated in every shadow, noise the denoiser could only blur away.
    if (r.W > 0.0 && r.light < count) {
        float offset = ray_offset(s.distance);
        LightRecord light = load_light(r.light);
        float3 origin = shadow_origin(s.position, s.geoNormal, light.position - s.position, offset, s.translucent);
        float tmax;
        float3 dir = shadow_ray(light, origin, rng.next2(), tmax);
        if (occluded_from_surface(origin, dir, 0.0, max(tmax - offset, 0.0), near_back(s.distance))) r.W = 0.0;
    }
    cur[index] = r;
}
