// SPDX-License-Identifier: MIT
// Light scattered under skin, with HDRP's diffusion profiles and the Burley normalized diffusion its own subsurface pass
// uses. HDRP spreads the diffuse light of skin over it after its lighting, and the traced light replaces that lighting: skin
// came out as hard as plaster, its shadows' edges sharp across a face. Here the traced diffuse light of a surface with a
// solid diffusion profile (skin, teeth, the whites of the eyes: the ones the tracer lights as solid) is spread over it per
// color channel as its profile sets, before the composite multiplies the albedo back in (HDRP's post-scatter texturing, so
// the texture's detail stays sharp). Output A (light divided by the albedo, and the distance) is read at 24 points of a disc
// as wide as the profile's filter radius in the surface's own plane (projected to the screen: a disc on the screen stood for
// more of a surface seen at a slant than its samples were weighted for, and the pixel's own light kept too much), rotated per
// pixel and frame, each weighted per channel by the profile at the distance of the surface found there from the pixel's own
// (across a depth edge the weight vanishes) and by the share of the disc it stands for; the pixel itself by the profile's
// energy within its own footprint. The weights are normalized: the light is spread, never gained or lost.
//   pass.a: output A, scratch, 0 to scatter into the scratch, 1 to write the scratch back
#include "pt_common.hlsli"

// The profile a pixel's light scatters by, or INVALID: a deferred surface whose GBuffer holds transmission through a solid
// diffusion profile that has a filter radius (the managed side's table, TGPT_SetDiffusionProfiles).
uint scattering_profile(uint2 p, Surface s) {
    if (!s.valid || !s.deferred || s.forward) return INVALID;
    Texture2D<float4> gb2 = ResourceDescriptorHeap[g.inputs0.z];
    const float4 b = gb2.Load(int3(p, 0));
    const uint feature = uint(b.a * 255.0 + 0.5) & 7u;
    if (!solid_transmission(b, feature)) return INVALID;
    const uint profile = uint(b.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u;
    return g.sssProfiles[profile].w > 0.0 ? profile : INVALID;
}
// Burley's profile R(r) = s (e^-sr + e^-sr/3) / (8 pi r) per channel, and its share of the energy within r.
float3 burley(float r, float3 shape) {
    r = max(r, 1e-6);
    return shape * (exp(-shape * r) + exp(-shape * r / 3.0)) / (8.0 * PI * r);
}
float3 burley_energy(float r, float3 shape) { return 1.0 - 0.25 * exp(-shape * r) - 0.75 * exp(-shape * r / 3.0); }

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> light = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> scratch = ResourceDescriptorHeap[pass.a.y];
    const Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    const uint profile = scattering_profile(p, s);
    if (profile == INVALID) return;
    if (pass.a.z == 1u) { light[p] = scratch[p]; return; }
    const float4 center = light[p];
    const float4 prof = g.sssProfiles[profile];
    const float3 shape = prof.xyz;
    const float radius = prof.w;
    // The pixel's footprint where it lies: the distance to its neighbour's direction at its own depth.
    Texture2D<float> depthTex = ResourceDescriptorHeap[g.inputs1.x];
    const float depth = depthTex.Load(int3(p, 0));
    const uint2 next = uint2(p.x + 1u < g.size.x ? p.x + 1u : p.x - 1u, p.y);
    const float pixel = max(length(camera_relative_position(next, depth) - camera_relative_position(p, depth)), 1e-6);
    const float inner = 0.5 * pixel; // the pixel itself stands for the disc within half its footprint
    if (radius <= inner * 1.5) { scratch[p] = center; return; } // scattering within the pixel: nothing to spread
    const float3 selfWeight = burley_energy(inner, shape);
    float3 sum = selfWeight * center.rgb, weights = selfWeight;
    const uint N = 24;
    const float share = PI * (radius * radius - inner * inner) / float(N);
    const float turn = float(pcg(p.x + pcg(p.y + pcg(g.size.z * 7919u + 0x5353u))) >> 8) * (6.2831853 / 16777216.0);
    // The disc in the surface's own plane.
    const float3 n = s.geoNormal;
    const float3 t = normalize(cross(n, abs(n.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0)));
    const float3 bt = cross(n, t);
    for (uint i = 0; i < N; ++i) {
        const float r = sqrt(inner * inner + (radius * radius - inner * inner) * (float(i) + 0.5) / float(N));
        const float phi = turn + float(i) * 2.39996323;
        float2 uv;
        if (!project(s.position + (t * cos(phi) + bt * sin(phi)) * r - g.cameraPos.xyz, uv)) continue;
        const int2 q = min(int2(uv * float2(g.size.xy)), int2(g.size.xy) - 1);
        if (all(q == int2(p))) continue;
        const Surface sq = load_surface(uint2(q), g.surface.x, g.surface.z, g.material.x);
        if (scattering_profile(uint2(q), sq) != profile) continue;
        const float3 w = burley(length(sq.position - s.position), shape) * share;
        sum += w * light[q].rgb; weights += w;
    }
    scratch[p] = float4(sum / max(weights, 1e-12), center.a);
}
