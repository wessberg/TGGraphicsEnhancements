// SPDX-License-Identifier: MIT
// ReSTIR GI spatial reuse and shading. A few neighbors' temporal reservoirs, on similar surfaces,
// compete with the pixel's own; the survivor's path vertex is checked for visibility from this pixel's
// surface (no light leaks through walls between neighbors), and its radiance, cosine and contribution
// weight give the pixel's indirect diffuse light, divided by the diffuse color (the composite
// multiplies HDRP's full-resolution one back in).
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWStructuredBuffer<GIReservoir> cur = ResourceDescriptorHeap[g.material.y];
    RWTexture2D<float4> diffuse = ResourceDescriptorHeap[g.signals.y];
    uint index = p.y * g.size.x + p.x;
    Surface s = load_surface(p, g.surface.x, g.surface.z, INVALID);
    // Alpha: the length of the ray the light came along, for AMD's Ray Regeneration (negative: no surface).
    if (!s.valid || !s.deferred) { diffuse[p] = float4(0.0, 0.0, 0.0, -1.0); return; }

    Rng rng = make_rng(p, g.size.z, 2);
    GIReservoir r = cur[index];
    float target = 0.0, jacobian = 1.0;
    if (gi_valid(r)) gi_target(r, s.position, s.normal, s.position, target, jacobian);
    float wsum = target * r.W * r.M;
    float m = r.M;
    float selectedTarget = target;

    uint samples = (g.settings2.y & OPTION_NO_SPATIAL) ? 0 : g.settings.w;
    float radius = g.params.w;
    for (uint k = 0; k < samples; ++k) {
        int2 q = int2(p) + int2(round(disk_sample(rng.next2()) * radius));
        if (any(q < 0) || any(q >= int2(g.size.xy)) || all(q == int2(p))) continue;
        Surface sq = load_surface(uint2(q), g.surface.x, g.surface.z, INVALID);
        if (!similar(s, sq)) continue;
        GIReservoir rq = cur[q.y * g.size.x + q.x];
        if (!gi_valid(rq) || rq.M <= 0.0 || rq.W <= 0.0) continue;
        float t, j;
        gi_target(rq, s.position, s.normal, sq.position, t, j);
        float w = t * j * rq.W * rq.M;
        m += rq.M;
        wsum += w;
        if (w > 0.0 && rng.next() * wsum < w) { r = rq; selectedTarget = t; }
    }
    float W = selectedTarget > 0.0 ? wsum / (m * selectedTarget) : 0.0;
    if (!(W < 1e20)) W = 0.0;

    float3 result = 0.0;
    float hitDistance = HIT_DISTANCE_MISS;
    if (W > 0.0 && gi_valid(r)) {
        float offset = ray_offset(s.distance);
        float3 origin = s.position + s.geoNormal * offset;
        float3 dir = gi_direction(r, origin);
        if (!gi_sky(r)) hitDistance = length(r.pos - origin);
        bool blocked;
        if (gi_sky(r)) blocked = occluded_from_surface(origin, dir, 0.0, g.params.y, near_back(s.distance));
        else blocked = occluded_from_surface(origin, dir, 0.0, max(length(r.pos - origin) - 0.02, 0.0), near_back(s.distance));
        if (!blocked) result = unpack_half3(r.radiance) * saturate(dot(s.normal, dir)) / PI * W;
    }
    // Indirect Color: the bounced light's color scaled around its brightness (1 as traced, 0 grey), which it keeps.
    float l = luminance(result);
    result = l + (result - l) * g.prevOriginDelta.w;
    diffuse[p] = float4(max(result, 0.0), hitDistance);
}
