// SPDX-License-Identifier: MIT
// AMD FSR Ray Regeneration's guides from this frame's surfaces, as AMD's own sample writes them: the signed
// linear view depth (Unity's view space looks down -z), motion to the previous frame (its texture
// coordinate minus this one's, and the change of the view depth), the world shading normal (octahedral)
// with perceptual roughness, and both albedos sqrt-encoded. The signals it denoises are to be divided by
// their albedo: the diffuse ones already are; the reflections are divided here and multiplied back in by
// pt_compose. The sky's texels get a depth beyond the denoiser's bounds, so they pass through.
//   pass.a: UAVs linear depth, motion, normals, diffuse albedo; pass.b.x: UAV specular albedo
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
    RWTexture2D<float> depth = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> motion = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float4> normals = ResourceDescriptorHeap[pass.a.z];
    RWTexture2D<float4> diffuseAlbedo = ResourceDescriptorHeap[pass.a.w];
    RWTexture2D<float4> specularAlbedo = ResourceDescriptorHeap[pass.b.x];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!s.valid || !s.deferred) {
        depth[p] = -1e6; motion[p] = 0.0; normals[p] = 0.0; diffuseAlbedo[p] = 0.0; specularAlbedo[p] = 0.0;
        return;
    }
    // Clip-space w is the distance along the view direction, which Unity's view space counts as -z.
    float3 rel = s.position - g.cameraPos.xyz;
    float z = -mul(g.viewProj, float4(rel, 1.0)).w;
    float prevZ = -mul(g.prevViewProj, float4(s.position - g.prevCameraPos.xyz, 1.0)).w;
    Texture2D<float2> mv = ResourceDescriptorHeap[g.inputs1.y];
    depth[p] = z;
    motion[p] = float4(-mv.Load(int3(p, 0)), prevZ - z, 0.0);
    normals[p] = float4(octahedral(s.normal), s.roughness, 0.0);
    float3 spec = specular_albedo(s.f0, s.roughness, saturate(dot(s.normal, normalize(-rel))));
    diffuseAlbedo[p] = float4(sqrt(saturate(s.albedo)), 1.0);
    specularAlbedo[p] = float4(sqrt(saturate(spec)), 1.0);
    RWTexture2D<float4> specular = ResourceDescriptorHeap[g.signals.w];
    float4 light = specular[p];
    specular[p] = float4(light.rgb / max(spec, 1e-3), light.a);
}
