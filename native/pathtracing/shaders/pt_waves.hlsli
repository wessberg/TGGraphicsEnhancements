// SPDX-License-Identifier: MIT
// HDRP's waves as HDRP's water shader moves its surface (the caustics' slots, TGPT_SetWaterCaustics): one reading of them for
// every pass that needs the water where it really is this frame (pt_caustics: the light through it; pt_wet: where it reaches
// on the shore). Included after pt_common.hlsli.
#ifndef PT_WAVES_HLSLI
#define PT_WAVES_HLSLI

static const float CHOPPINESS = 2.25;   // HDRP's WATER_SYSTEM_CHOPPINESS

// HDRP's waves read as HDRP's water shader reads its surface gradient: a cubic B-spline over 4 x 4 texels, as four bilinear reads.
float4 bicubic(Texture2DArray<float4> t, float2 uv, float slice, float size) {
    const float2 st = uv * size - 0.5;
    const float2 i = floor(st), f = st - i, f2 = f * f, f3 = f2 * f;
    const float2 w0 = (1.0 - 3.0 * f + 3.0 * f2 - f3) / 6.0, w1 = (4.0 - 6.0 * f2 + 3.0 * f3) / 6.0;
    const float2 w2 = (1.0 + 3.0 * f + 3.0 * f2 - 3.0 * f3) / 6.0, w3 = f3 / 6.0;
    const float2 g0 = w0 + w1, g1 = w2 + w3;
    const float2 h0 = (i - 0.5 + w1 / g0) / size, h1 = (i + 1.5 + w3 / g1) / size;
    return g0.y * (g0.x * t.SampleLevel(s_linearRepeat, float3(h0.x, h0.y, slice), 0) + g1.x * t.SampleLevel(s_linearRepeat, float3(h1.x, h0.y, slice), 0)) +
           g1.y * (g0.x * t.SampleLevel(s_linearRepeat, float3(h0.x, h1.y, slice), 0) + g1.x * t.SampleLevel(s_linearRepeat, float3(h1.x, h1.y, slice), 0));
}

// The level surface of a slot's water under a point (x, z relative to the scene origin): the slot's own water instance, under
// any other slot's, met by a ray straight down; its point and its normal. False where the slot has none there, or where it
// stands steep (a waterfall's sheet). slot ~0u: the first slot's water met, whichever (its slot given back).
bool water_base(inout uint slot, float2 xz, out float3 base, out float3 baseNormal) {
    base = float3(xz.x, 0.0, xz.y); baseNormal = float3(0.0, 1.0, 0.0);
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    const bool any = slot == ~0u;
    const float top = any ? max(max(g.waterTop.x, g.waterTop.y), max(g.waterTop.z, g.waterTop.w)) : g.waterTop[slot];
    const float bottom = any ? min(min(g.waterBottom.x, g.waterBottom.y), min(g.waterBottom.z, g.waterBottom.w)) : g.waterBottom[slot];
    if (!(top >= bottom)) return false;
    RayDesc ray; ray.Origin = float3(xz.x, top + 0.05, xz.y); ray.Direction = float3(0.0, -1.0, 0.0); ray.TMin = 0.0; ray.TMax = top - bottom + 0.1;
    for (uint pass = 0; pass < 4; ++pass) {
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_WATER, ray);
        q.Proceed();
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
        const float t = q.CommittedRayT();
        const uint owner = (instances[q.CommittedInstanceID()].flags >> 8) & 3u;
        if (!any && owner != slot) { ray.TMin = t + 1e-3; continue; }
        base = ray.Origin + ray.Direction * t;
        baseNormal = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                               q.CommittedObjectToWorld3x4(), ray.Direction, base, ray.Origin).normal;
        slot = owner;
        return baseNormal.y >= 0.5 && water_slot(owner);
    }
    return false;
}

// That level surface moved as HDRP's waves move it at its undisplaced point (base): each band's displacement (its height, and
// its horizontal move times HDRP's choppiness) and surface gradient (read bicubic, as HDRP reads it), times its amplitude and
// its fade with the distance from the camera. HDRP samples its waves in the surface's own space (rotated as the surface is;
// the world itself for a custom mesh's). lift: how far the waves moved it up or down (in the surface's own space); steep: its
// slope there.
void water_displace(uint slot, float3 base, float3 baseNormal, out float3 position, out float3 normal, out float lift, out float steep) {
    const float3 r0 = water_row(slot, 9).xyz, r1 = water_row(slot, 10).xyz, r2 = water_row(slot, 11).xyz;
    float3 n = float3(dot(r0, baseNormal), dot(r1, baseNormal), dot(r2, baseNormal));
    float3 move = 0.0;
    const float4 info = water_row(slot, 12);
    const uint bands = uint(info.x);
    if (bands > 0u && g.waterTextures[slot] != INVALID && g.waterNormals[slot] != INVALID) {
        Texture2DArray<float4> displacement = ResourceDescriptorHeap[NonUniformResourceIndex(g.waterTextures[slot])];
        Texture2DArray<float4> gradient = ResourceDescriptorHeap[NonUniformResourceIndex(g.waterNormals[slot])];
        const float distance = length(base - g.cameraPos.xyz);
        float2 slope = 0.0;
        for (uint b = 0; b < bands; ++b) {
            const float4 rx = water_row(slot, b * 3), rz = water_row(slot, b * 3 + 1), e = water_row(slot, b * 3 + 2);
            const float2 uv = float2(dot(rx.xyz, base) + rx.w, dot(rz.xyz, base) + rz.w);
            // HDRP's fade with the distance from the camera: saturated, squared, then smoothed.
            float fade = saturate(distance * e.y + e.z); fade *= fade; fade = fade * fade * (3.0 - 2.0 * fade);
            if (!(fade > 0.0)) continue;
            const float3 raw = displacement.SampleLevel(s_linearRepeat, float3(uv, float(b)), 0).xyz * (e.x * fade);
            move += float3(-CHOPPINESS * raw.y, raw.x, -CHOPPINESS * raw.z);
            slope += bicubic(gradient, uv, float(b), info.y).xy * fade;
        }
        n = n - float3(slope.x, 0.0, slope.y);
    }
    n = normalize(n);
    lift = move.y; steep = length(n.xz) / max(n.y, 1e-6);
    position = base + r0 * move.x + r1 * move.y + r2 * move.z;
    normal = normalize(r0 * n.x + r1 * n.y + r2 * n.z);
}

// The water of a slot at a point of its level surface (x, z relative to the scene origin), moved as HDRP's waves move it there,
// and its normal. False where the slot has no water. lift, steep: water_displace's.
bool water_point(uint slot, float2 xz, out float3 position, out float3 normal, out float lift, out float steep) {
    float3 base, baseNormal;
    position = float3(xz.x, 0.0, xz.y); normal = float3(0.0, 1.0, 0.0); lift = 0.0; steep = 0.0;
    if (!water_base(slot, xz, base, baseNormal)) return false;
    water_displace(slot, base, baseNormal, position, normal, lift, steep);
    return all(isfinite(position)) && all(isfinite(normal));
}

#endif
