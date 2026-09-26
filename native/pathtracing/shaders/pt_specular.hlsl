// SPDX-License-Identifier: MIT
// Path-traced reflections: one GGX visible-normal sample per pixel from HDRP's normal,
// roughness and specular color; the path vertex it reaches is shaded like a GI vertex. Added to the
// specular signal the DI pass wrote.
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

void specular_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> specular = ResourceDescriptorHeap[g.signals.w];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!s.valid || !s.deferred) return;
    Rng rng = make_rng(p, g.size.z, 5);
    float3 v = normalize(g.cameraPos.xyz - s.position);
    float alpha = roughness_alpha(s.roughness);
    float3 l = ggx_sample(s.normal, v, alpha, rng.next2());
    float nl = dot(s.normal, l), nv = saturate(dot(s.normal, v));
    if (nl <= 0.0 || nv <= 0.0) return;
    float3 h = normalize(v + l);
    // Visible-normal sampling leaves F * G1(l) as the estimator's weight.
    float3 weight = fresnel_schlick(s.f0, saturate(dot(v, h))) * smith_g1(nl, alpha * alpha);
    float offset = ray_offset(s.distance);
    float3 origin = s.position + s.geoNormal * offset;
    Hit hit;
    float3 radiance;
    // One vertex (its emission and direct light) keeps reflections to three rays a pixel.
    const bool found = trace_from_surface(origin, l, 0.0, g.params.y, near_back(s.distance), hit);
    PT_REORDER(found ? hit.material : 0xFFFFu); // with SER: the reflected surface's shading, grouped by material
    if (found) radiance = shade_path(hit, l, 1u, rng);
    else radiance = sky_radiance(l);
    specular[p] = float4(specular[p].rgb + clamp_radiance(weight * radiance, LIMIT_REFLECTION), found ? length(hit.position - origin) : HIT_DISTANCE_MISS);
}
PT_ENTRY(specular_pixel, specular)
