// SPDX-License-Identifier: MIT
// AMD FSR Ray Regeneration's guides from this frame's surfaces, as AMD's own sample writes them: the signed
// linear view depth (Unity's view space looks down -z), motion to the previous frame (its texture
// coordinate minus this one's, and the change of the view depth), the world shading normal (octahedral)
// with perceptual roughness, and both albedos sqrt-encoded. The signals it denoises are to be divided by
// their albedo: the diffuse ones already are; reflections use one factor in both directions here.
// The sky's texels get a depth beyond the denoiser's bounds, so they pass through.
// pass.a: UAVs linear depth, motion, normals, diffuse albedo; pass.b.x: UAV specular albedo.
// pass.b.y: storage-to-stable radiance scale (float bits); pass.b.z: 0 prepare, 1 restore after AMD dispatch.
#include "pt_common.hlsli"

float2 octahedral(float3 n) {
    n /= abs(n.x) + abs(n.y) + abs(n.z);
    if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
    return n.xy * 0.5 + 0.5;
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    float3 spec = s.valid && s.deferred ? specular_guide_albedo(s, saturate(dot(s.normal, normalize(g.cameraPos.xyz - s.position)))) : 1.0;
    const float3 specularFactor = max(spec, 1e-3);
    const float scale = asfloat(pass.b.y);
    RWTexture2D<float4> indirect = ResourceDescriptorHeap[g.signals.y];
    RWTexture2D<float4> direct = ResourceDescriptorHeap[g.signals.z];
    RWTexture2D<float4> specular = ResourceDescriptorHeap[g.signals.w];
    float4 gi = indirect[p], di = direct[p], reflection = specular[p];
    const bool restore = pass.b.z != 0u;
    indirect[p] = float4(restore ? gi.rgb / scale : gi.rgb * scale, gi.a);
    direct[p] = float4(restore ? di.rgb / scale : di.rgb * scale, di.a);
    specular[p] = float4(restore ? reflection.rgb * specularFactor / scale : reflection.rgb * scale / specularFactor, reflection.a);
    if (restore) return;
    RWTexture2D<float> depth = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> motion = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float4> normals = ResourceDescriptorHeap[pass.a.z];
    RWTexture2D<float4> diffuseAlbedo = ResourceDescriptorHeap[pass.a.w];
    RWTexture2D<float4> specularAlbedo = ResourceDescriptorHeap[pass.b.x];
    if (!s.valid || !s.deferred) {
        depth[p] = -1e6; motion[p] = 0.0; normals[p] = 0.0; diffuseAlbedo[p] = 0.0; specularAlbedo[p] = 0.0;
        return;
    }
    // Clip-space w is the distance along the view direction, which Unity's view space counts as -z.
    float3 rel = s.position - g.cameraPos.xyz;
    float z = -mul(g.viewProj, float4(rel, 1.0)).w;
    // HDRP's previous view-projection takes positions relative to this frame's camera (it moves the previous camera's
    // matrix by the camera's move: HDCamera, camera-relative rendering).
    // (The change of that signed depth, the previous frame's minus this one's, of the point's own previous position: a moving
    // thing's too since 2026-10-11, PT-127; the signs: Unity's view looks down -z.)
    depth[p] = z;
    motion[p] = float4(-motion_at(p), -motion_depth_change(p, s.position), 0.0);
    // See-through glass is specular only (pt_specular refracts what lies behind it into the reflections): no diffuse albedo,
    // a specular albedo that covers the light it lets through, the tracer's near-mirror roughness (pt_prepare), and a
    // material type of its own (AMD's normals' alpha, in thirds), which keeps its light and its neighbours' apart.
    normals[p] = float4(octahedral(s.normal), s.roughness, s.glass ? 1.0 / 3.0 : 0.0);
    diffuseAlbedo[p] = float4(s.glass ? 0.0 : sqrt(saturate(s.albedo)), 1.0);
    specularAlbedo[p] = float4(sqrt(saturate(spec)), 1.0);
}
