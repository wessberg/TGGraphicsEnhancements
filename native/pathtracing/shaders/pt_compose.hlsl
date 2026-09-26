// SPDX-License-Identifier: MIT
// The traced light for the D3D11 composite, which applies it one frame later (reprojected through the
// next frame's motion vectors, so D3D11 never waits for D3D12 work of its own frame). Light is stored
// divided by the diffuse color and pre-exposed: output A holds the diffuse light (rgb) and the surface's
// distance from the camera (a, 0 where nothing was traced), output B the specular light.
// The composite multiplies the next frame's own albedo back in and adds its emission.
// Debug views store their colors in A instead. pass.a.x: 1 when AMD's Ray Regeneration denoised the
// reflections divided by their albedo (pt_rrg_guides), which is multiplied back in here.
#include "pt_common.hlsli"

float3 hash_color(uint v) { uint h = pcg(v * 2654435761u + 1u); return float3(h & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF) / 255.0 * 0.8 + 0.2; }

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> outA = ResourceDescriptorHeap[g.scene1.w];
    RWTexture2D<float4> outB = ResourceDescriptorHeap[g.denoise2.w];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    uint view = g.settings.y;
    float ex = exposure();
    RWTexture2D<float4> diffuseFinal = ResourceDescriptorHeap[g.denoise2.z];
    float distance = s.valid ? s.distance : 0.0;

    if (view == VIEW_COVERAGE || view == VIEW_MATERIALS) {
        float3 rel = s.valid ? s.position - g.cameraPos.xyz : camera_relative_position(p, 1e-7);
        float3 dir = normalize(rel);
        float3 c = 0.0;
        RayDesc ray; ray.Origin = g.cameraPos.xyz; ray.Direction = dir; ray.TMin = 0.0; ray.TMax = g.params.y;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            if (view == VIEW_COVERAGE) {
                c = hash_color(q.CommittedInstanceID());
                // Where the traced scene lies behind HDRP's surface, HDRP draws something the tracer lacks: darker.
                if (s.valid && q.CommittedRayT() > s.distance * 1.02 + 0.1) c *= 0.25;
            } else {
                Hit hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                                    q.CommittedObjectToWorld3x4(), dir, g.cameraPos.xyz + dir * q.CommittedRayT());
                SurfaceMaterial m = hit_material(hit);
                c = m.albedo * (0.35 + 0.65 * saturate(dot(hit.normal, -dir))) + m.emissive * ex;
            }
        }
        outA[p] = float4(c, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (view == VIEW_ALBEDO) { outA[p] = float4(s.valid ? s.albedo : 0.0, max(distance, 1e-3)); outB[p] = 0.0; return; }
    if (view == VIEW_NORMALS) { outA[p] = float4(s.valid ? s.normal * 0.5 + 0.5 : 0.0, max(distance, 1e-3)); outB[p] = 0.0; return; }
    if (view == VIEW_INDIRECT || view == VIEW_INDIRECT_RAW) {
        RWTexture2D<float4> raw = ResourceDescriptorHeap[g.signals.y];
        float3 d = s.valid && s.deferred ? (view == VIEW_INDIRECT ? diffuseFinal[p].rgb : raw[p].rgb) : 0.0;
        outA[p] = float4(d * ex, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (view == VIEW_DIRECT) {
        RWTexture2D<float4> sun = ResourceDescriptorHeap[g.denoise1.x];
        float3 d = s.valid && s.deferred ? sun[p].rgb : 0.0;
        outA[p] = float4(d * ex, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (!s.valid || !s.deferred) { outA[p] = 0.0; outB[p] = 0.0; return; }
    RWTexture2D<float4> sun = ResourceDescriptorHeap[g.denoise1.x];
    RWTexture2D<float4> spec = ResourceDescriptorHeap[g.denoise1.z];
    float3 diffuse = diffuseFinal[p].rgb + sun[p].rgb; // the direct light, denoised on its own
    float3 specular = spec[p].rgb;
    if (pass.a.x == 1u) specular *= specular_albedo(s.f0, s.roughness, saturate(dot(s.normal, normalize(g.cameraPos.xyz - s.position))));
    outA[p] = float4(max(diffuse * ex, 0.0), distance);
    outB[p] = float4(max(specular * ex, 0.0), 0.0);
}
