// SPDX-License-Identifier: MIT
// ReSTIR DI spatial reuse and direct lighting. Neighbors' lights compete with the
// pixel's; the survivor gets a shadow ray. The sun is always sampled on its own, one shadow ray into a
// cone the size of its disc. Writes the direct diffuse light of the sun and the lamps (divided by the
// diffuse color) as a signal of its own, apart from the bounced light: its shadows are sharp and its noise
// low, and denoised together with the bounced light's they were blurred away. And the specular light of both.
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWStructuredBuffer<DIReservoir> cur = ResourceDescriptorHeap[g.material.w];
    RWTexture2D<float4> sun = ResourceDescriptorHeap[g.signals.z];
    RWTexture2D<float4> specular = ResourceDescriptorHeap[g.signals.w];
    uint index = p.y * g.size.x + p.x;
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!s.valid || !s.deferred) { sun[p] = float4(0.0, 0.0, 0.0, -1.0); specular[p] = float4(0.0, 0.0, 0.0, -1.0); return; }

    Rng rng = make_rng(p, g.size.z, 4);
    float3 v = normalize(g.cameraPos.xyz - s.position);
    float alpha = roughness_alpha(s.roughness);
    float offset = ray_offset(s.distance);
    float3 origin = s.position + s.geoNormal * offset;
    float nearBack = near_back(s.distance);
    uint count = g.settings2.z;
    float3 localDiffuse = 0.0, spec = 0.0;

    if (count > 0) {
        DIReservoir r = cur[index];
        float3 d, sp, l; float dist;
        float selectedTarget = r.light < count ? di_target(r.light, s, v, alpha, d, sp, l, dist) : 0.0;
        float wsum = selectedTarget * r.W * r.M;
        float m = r.M;
        uint samples = (g.settings2.y & OPTION_NO_SPATIAL) ? 0 : g.settings.w;
        for (uint k = 0; k < samples; ++k) {
            int2 q = int2(p) + int2(round(disk_sample(rng.next2()) * g.params.w));
            if (any(q < 0) || any(q >= int2(g.size.xy)) || all(q == int2(p))) continue;
            Surface sq = load_surface(uint2(q), g.surface.x, g.surface.z, INVALID);
            if (!similar(s, sq)) continue;
            DIReservoir rq = cur[q.y * g.size.x + q.x];
            if (rq.light >= count || rq.W <= 0.0 || rq.M <= 0.0) continue;
            float t = di_target(rq.light, s, v, alpha, d, sp, l, dist);
            float w = t * rq.W * rq.M;
            m += rq.M; wsum += w;
            if (w > 0.0 && rng.next() * wsum < w) { r.light = rq.light; selectedTarget = t; }
        }
        float W = selectedTarget > 0.0 ? wsum / (m * selectedTarget) : 0.0;
        if (W > 0.0 && W < 1e20 && r.light < count) {
            di_target(r.light, s, v, alpha, d, sp, l, dist);
            LightRecord light = load_light(r.light);
            float3 lightOrigin = shadow_origin(s.position, s.geoNormal, light.position - s.position, offset, s.translucent);
            float tmax;
            float3 dir = shadow_ray(light, lightOrigin, rng.next2(), tmax);
            if (!occluded_from_surface(lightOrigin, dir, 0.0, max(tmax - offset, 0.0), nearBack)) { localDiffuse = d * W; spec = sp * W; }
        }
    }
    float3 sunDiffuse = 0.0;
    if (g.sunColor.w > 0.0) {
        float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
        float nl = translucent_cos(s.normal, l, s.translucent); // a grass blade or leaf is lit through from behind too
        float3 sunOrigin = shadow_origin(s.position, s.geoNormal, l, offset, s.translucent);
        if (nl > 0.0 && !occluded_from_surface(sunOrigin, l, 0.0, g.params.y, nearBack)) {
            sunDiffuse = g.sunColor.rgb * nl * terminator_softening(s, l) / PI;
            if (dot(s.normal, l) > 0.0) spec += g.sunColor.rgb * ggx_specular(s.normal, v, l, s.f0, alpha);
        }
    }
    sun[p] = float4(sunDiffuse + clamp_radiance(localDiffuse, LIMIT_DIRECT), 1.0);
    specular[p] = float4(clamp_radiance(spec, LIMIT_DIRECT), HIT_DISTANCE_MISS); // the reflection pass sets its own
}
