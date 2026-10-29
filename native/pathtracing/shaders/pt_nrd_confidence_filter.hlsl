// SPDX-License-Identifier: MIT
// PT-127: spatial lighting-gradient filtering followed by surface-aware reprojection.
// a: source gradient, destination gradient/confidence, previous positions, previous normals (UAVs).
// b: stride (1..5 blur, 0 resolve), NRD kind (1 RELAX, 2 REBLUR).
// No temporal accumulation here: it would delay the change NRD needs to react to.
#include "pt_common.hlsli"

float guide_weight(float4 center, float3 normal, float3 geometry, float4 other, float3 otherNormal) {
    if (center.w <= 0.0 || other.w <= 0.0) return 0.0;
    const float distanceScale = max(center.w * pixel_angle() * float(g.size.x) * 0.02, 1e-3);
    const float plane = saturate(1.0 - abs(dot(other.xyz - center.xyz, geometry)) / distanceScale);
    const float aligned = saturate(dot(normal, otherNormal));
    return plane * plane * (3.0 - 2.0 * plane) * aligned * aligned;
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    const uint2 grid = (g.size.xy + 4u) / 5u;
    RWTexture2D<float2> source = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> positions = ResourceDescriptorHeap[pass.a.z];
    RWTexture2D<uint2> normals = ResourceDescriptorHeap[pass.a.w];
    if (pass.b.x != 0u) {
        if (any(p >= grid)) return;
        RWTexture2D<float2> destination = ResourceDescriptorHeap[pass.a.y];
        const float4 center = positions[p];
        const uint2 n = normals[p];
        const float3 shading = unpack_normal(n.x), geometry = unpack_normal(n.y);
        float2 sum = source[p]; float weights = 1.0;
        [unroll] for (int y = -2; y <= 2; ++y) {
            [unroll] for (int x = -2; x <= 2; ++x) {
                if (x == 0 && y == 0) continue;
                int2 q = int2(p) + int2(x, y) * int(pass.b.x);
                if (any(q < 0) || any(q >= int2(grid))) continue;
                const float w = exp(-0.5 * float(x*x + y*y)) * guide_weight(center, shading, geometry, positions[q], unpack_normal(normals[q].x));
                sum += source[q] * w; weights += w;
            }
        }
        destination[p] = sum / weights;
        return;
    }
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float> confidence = ResourceDescriptorHeap[pass.a.y];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    confidence[p] = 1.0;
    // This optional confidence pass measured light at the mirror, not at its virtual replacement.
    // A neutral value lets NRD's own disocclusion logic handle that endpoint.
    if (psr_replaced(p)) return;
    if (!s.valid || !s.deferred || s.glass || s.water || s.underwater) return;
    const float2 oldPixel = previous_place(p, motion_at(p));
    if (any(oldPixel < 0.0) || any(oldPixel >= float2(g.size.xy))) return;
    const float2 cell = (oldPixel - 2.5) / 5.0;
    const int2 base = int2(floor(cell));
    const float2 f = frac(cell);
    float2 sum = 0.0; float weights = 0.0;
    const float4 center = float4(s.position, s.distance);
    [unroll] for (int y = 0; y < 2; ++y) {
        [unroll] for (int x = 0; x < 2; ++x) {
            const int2 q = clamp(base + int2(x, y), int2(0, 0), int2(grid) - 1);
            const float4 oldPosition = positions[q];
            float w = (x == 0 ? 1.0-f.x : f.x) * (y == 0 ? 1.0-f.y : f.y);
            w *= guide_weight(center, s.normal, s.geoNormal, oldPosition, unpack_normal(normals[q].x));
            // A moving receiver can leave its old surface. Relax such comparisons as the
            // vendor recommends, leaving primary disocclusions to NRD itself.
            const float relativeMove = abs(dot(oldPosition.xyz - s.position, s.geoNormal)) / max(s.distance, 1e-3);
            const float relaxed = 1.0 - smoothstep(0.05, 0.25, relativeMove);
            // Relax the gradient itself, not both numerator and denominator: multiplying
            // both by the same factor would cancel in the relative-change conversion.
            sum += source[q] * float2(relaxed, 1.0) * w; weights += w;
        }
    }
    if (weights <= 1e-6) return;
    const float relativeChange = sum.y > 0.0 ? saturate(sum.x / sum.y) : 0.0;
    // A relative-light mapping is independent of exposure and keeps exact endpoints.
    // RELAX caps only this frame's temporal history, so use its stronger response.
    float c = 1.0 - sqrt(relativeChange);
    if (pass.b.y == 1u) c *= c;
    confidence[p] = saturate(c);
}
