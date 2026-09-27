// SPDX-License-Identifier: MIT
// NVIDIA NRD's inputs for RELAX's diffuse denoiser from this frame's surfaces and bounced light, as its NRD.hlsli packs
// them: the view depth along the camera's axis (positive; the sky's texels beyond the denoising range, which NRD leaves
// alone), motion to the previous frame (its texture coordinate minus this one's: NRD's 2D motion, from which RELAX
// estimates the previous depth itself as for a still surface; this pass knows no more, having no previous position for a
// moving one), the surface's geometric normal with linear roughness, and the bounced light (divided by the diffuse color)
// with its hit distance, in NRD's own units (pass.b.x: the tracer's storage units into them, tracer.cpp).
// NRD takes the bounced light only. The direct light's shadows and a normal map's shading in it are sharp detail RELAX
// blurred and reset from frame to frame, and the reflections, sampled around a normal map's normals, flickered under it
// (in the harness: the sun's shadows on the floor at three times the built-in denoiser's error, a bump-mapped floor
// flickering three to five times as much), so the tracer's own filters for those serve with NRD too. The geometric
// normal, not the shading normal: RELAX tests its history against the surface's plane, which a normal map's per-pixel
// normals made fail (three times the flicker again).
//   pass.a: UAVs view depth, motion, normal and roughness, diffuse; pass.b.x: float, storage units to NRD's
#include "pt_common.hlsli"
#include "NRD.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float> viewZ = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> motion = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float4> normalRoughness = ResourceDescriptorHeap[pass.a.z];
    RWTexture2D<float4> diffuse = ResourceDescriptorHeap[pass.a.w];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    // See-through glass has no diffuse light to denoise (its light is all in the reflections: pt_specular, pt_compose), so
    // RELAX leaves it alone as it does the sky, and its bounced light, which nothing shows, isn't mixed into its neighbours'.
    if (!s.valid || !s.deferred || s.glass) {
        viewZ[p] = 1e6;
        motion[p] = 0.0;
        normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0, 0.0, 1.0), 1.0, 0.0);
        diffuse[p] = 0.0;
        return;
    }
    // Clip-space w is the distance along the view direction (Unity's view space looks down -z).
    float3 rel = s.position - g.cameraPos.xyz;
    float z = mul(g.viewProj, float4(rel, 1.0)).w;
    Texture2D<float2> mv = ResourceDescriptorHeap[g.inputs1.y];
    viewZ[p] = z;
    motion[p] = float4(-mv.Load(int3(p, 0)), 0.0, 0.0);
    normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(s.geoNormal, s.roughness, 0.0);
    RWTexture2D<float4> bounced = ResourceDescriptorHeap[g.signals.y];
    float4 b = bounced[p];
    diffuse[p] = RELAX_FrontEnd_PackRadianceAndHitDist(b.rgb * asfloat(pass.b.x), b.a, true);
}
