// SPDX-License-Identifier: MIT
// ReSTIR helpers shared by the GI and DI passes: reprojection with HDRP's motion vectors, neighbor
// similarity, target functions and the reconnection Jacobian.
#pragma once

static const uint GI_VALID = 1u;
static const uint GI_SKY = 2u;

uint gi_flags(GIReservoir r) { return r.radiance.y >> 16; }
bool gi_valid(GIReservoir r) { return (gi_flags(r) & GI_VALID) != 0; }
bool gi_sky(GIReservoir r) { return (gi_flags(r) & GI_SKY) != 0; }

// Bounded in pre-exposed units, where the image is displayed (1 is about white): fireflies from rare
// bright paths would otherwise hold ReSTIR's reservoirs and the temporal accumulation for many frames. Measured against
// HDRP's exposure averaged over about ten seconds (params.x), not its current one: auto exposure follows the view
// within a second, and in a hall lit by a sunlit courtyard's bounce, looking at a dark wall raised the exposure and
// the clamp then cut the bounced light it had let through while looking at the courtyard (lighting that changed with
// the camera's direction, catching up as the exposure adapted).
static const float LIMIT_INDIRECT = 4.0;   // light arriving from a path vertex
static const float LIMIT_REFLECTION = 8.0; // a traced reflection
static const float LIMIT_DIRECT = 48.0;    // ReSTIR DI's direct light, sampled well enough to stay bright
float3 clamp_radiance(float3 radiance, float limit) {
    float l = luminance(radiance) * (g.params.x > 0.0 ? g.params.x : exposure());
    return (l > limit) ? radiance * (limit / l) : max(radiance, 0.0);
}

bool similar(Surface a, Surface b) {
    if (!b.valid || !b.deferred) return false;
    if (dot(a.geoNormal, b.geoNormal) < 0.85) return false;
    float plane = abs(dot(a.geoNormal, b.position - a.position));
    return plane < 0.05 + 0.02 * a.distance;
}
// The previous frame's pixel for this one, through HDRP's motion vectors, if its surface matches.
bool reproject(uint2 p, Surface s, out int2 q, out Surface sp) {
    Texture2D<float2> motion = ResourceDescriptorHeap[g.inputs1.y];
    float2 mv = motion.Load(int3(p, 0));
    float2 prevUv = (float2(p) + 0.5) / float2(g.size.xy) - mv;
    q = int2(floor(prevUv * float2(g.size.xy)));
    // The temporal neighbour is permuted within a 2x2 texel block, differently each frame (RTXDI's permutation
    // sampling). Without it a sample ReSTIR keeps stays on its pixel for as long as the temporal history lets it:
    // noise that stands still, which no denoiser averages over time (the built-in one keeps it as grain, and DLSS
    // Ray Reconstruction and AMD's Ray Regeneration both assume uncorrelated samples).
    { uint h = pcg(g.size.z * 747796405u + 12345u); q ^= int2(h & 1u, (h >> 1) & 1u); }
    sp = (Surface)0;
    if (any(q < 0) || any(q >= int2(g.size.xy))) return false;
    sp = load_surface(uint2(q), g.surface.y, g.surface.w, INVALID);
    sp.position += g.prevOriginDelta.xyz;
    if (!sp.valid || !sp.deferred) return false;
    if (dot(sp.geoNormal, s.geoNormal) < 0.8) return false;
    return length(sp.position - s.position) < 0.05 + 0.03 * s.distance;
}
// Target function of a GI sample at a primary surface (radiance luminance times cosine), and the
// Jacobian of reconnecting the sample's path vertex to this surface instead of the one it came from:
// the ratio of the solid angles the vertex's surface subtends at the two points.
void gi_target(GIReservoir r, float3 position, float3 normal, float3 fromPosition, out float target, out float jacobian) {
    float3 radiance = unpack_half3(r.radiance);
    if (gi_sky(r)) {
        target = luminance(radiance) * saturate(dot(normal, r.pos));
        jacobian = 1.0;
        return;
    }
    float3 toNew = position - r.pos, toOld = fromPosition - r.pos;
    float dNew = dot(toNew, toNew), dOld = dot(toOld, toOld);
    float3 n2 = unpack_normal(r.normal);
    float cosNew = abs(dot(n2, toNew * rsqrt(max(dNew, 1e-8)))), cosOld = abs(dot(n2, toOld * rsqrt(max(dOld, 1e-8))));
    jacobian = (cosOld > 1e-4 && dNew > 1e-8) ? (cosNew * dOld) / (cosOld * dNew) : 0.0;
    // Large Jacobians come from very different geometry: the sample would be a poor fit here.
    if (jacobian > 8.0 || jacobian < 0.125) jacobian = 0.0;
    float3 dir = -toNew * rsqrt(max(dNew, 1e-8));
    target = luminance(radiance) * saturate(dot(normal, dir));
}
float3 gi_direction(GIReservoir r, float3 position) { return gi_sky(r) ? r.pos : normalize(r.pos - position); }

// DI target: unshadowed diffuse plus specular reflection of one light at the surface.
float di_target(uint lightIndex, Surface s, float3 v, float alpha, out float3 diffuse, out float3 specular, out float3 l, out float dist) {
    LightRecord light = load_light(lightIndex);
    float3 e = light_incident(light, s.position, l, dist);
    float nl = translucent_cos(s.normal, l, s.translucent);
    diffuse = 0.0; specular = 0.0;
    if (nl <= 0.0 || !any(e > 0.0)) return 0.0;
    diffuse = e * nl * terminator_softening(s, l) / PI;
    if (dot(s.normal, l) > 0.0) specular = e * ggx_specular(s.normal, v, l, s.f0, alpha); // no highlight from behind
    return luminance(s.albedo * diffuse + specular);
}
