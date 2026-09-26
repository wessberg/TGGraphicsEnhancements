// SPDX-License-Identifier: MIT
// HDRP's GBuffer as the tracer uses it: world position from depth, the normal and roughness from the
// normal buffer, the diffuse color and specular color from GBuffers 0 and 2 (Lit's encoding).
#include "pt_common.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    Texture2D<float4> gb0 = ResourceDescriptorHeap[g.inputs0.x];
    Texture2D<float4> nb = ResourceDescriptorHeap[g.inputs0.y];
    Texture2D<float4> gb2 = ResourceDescriptorHeap[g.inputs0.z];
    Texture2D<float> depthTex = ResourceDescriptorHeap[g.inputs1.x];
    RWTexture2D<float4> surf = ResourceDescriptorHeap[g.surface.x];
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
    RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];

    float depth = depthTex.Load(int3(p, 0));
    if (!(depth > 0.0)) { surf[p] = 0.0; nrm[p] = uint2(pack_normal(float3(0, 1, 0)), 0); mat[p] = 0; return; }
    float3 rel = camera_relative_position(p, depth);
    float4 a = gb0.Load(int3(p, 0));
    float4 n = nb.Load(int3(p, 0));
    float4 s = gb2.Load(int3(p, 0));
    float3 normal = decode_hdrp_normal(n);
    bool deferred = a.a > 0.5;
    uint feature = uint(s.a * 255.0 + 0.5) & 7u;
    // Standard Lit stores the diffuse color in GBuffer0 and the specular color (fast sRGB) in GBuffer2;
    // the other features keep the base color and imply the default dielectric response.
    float3 f0 = feature == 0 ? fast_srgb_to_linear(s.rgb) : 0.04;
    float roughness = saturate(n.a);
    // Reflection Boost, a debugging aid: every surface glossier and more reflective than the game made it, so the
    // traced reflections show everywhere and can be judged (1: nearly a mirror).
    float boost = g.cameraPos.w;
    roughness = lerp(roughness, 0.05, boost);
    f0 = lerp(f0, 0.6, 0.7 * boost);
    // The geometric normal from the positions of a neighbour along each axis, facing the camera; the
    // shading normal where they are sky. Device depth is affine across a plane in screen space, so the
    // side whose next two texels continue this one's depth in a straight line lies on this pixel's plane
    // (at a crease the nearer-depth side can belong to the other plane).
    int2 last = int2(g.size.xy) - 1;
    int2 l = clamp(int2(p) - int2(1, 0), 0, last), r = clamp(int2(p) + int2(1, 0), 0, last);
    int2 u = clamp(int2(p) - int2(0, 1), 0, last), d = clamp(int2(p) + int2(0, 1), 0, last);
    float dl = depthTex.Load(int3(l, 0)), dr = depthTex.Load(int3(r, 0)), du = depthTex.Load(int3(u, 0)), dd = depthTex.Load(int3(d, 0));
    float dl2 = depthTex.Load(int3(clamp(int2(p) - int2(2, 0), 0, last), 0)), dr2 = depthTex.Load(int3(clamp(int2(p) + int2(2, 0), 0, last), 0));
    float du2 = depthTex.Load(int3(clamp(int2(p) - int2(0, 2), 0, last), 0)), dd2 = depthTex.Load(int3(clamp(int2(p) + int2(0, 2), 0, last), 0));
    float3 geo = normal;
    bool right = abs(2.0 * dr - dr2 - depth) < abs(2.0 * dl - dl2 - depth), down = abs(2.0 * dd - dd2 - depth) < abs(2.0 * du - du2 - depth);
    float dx = right ? dr : dl, dy = down ? dd : du;
    if (dx > 0.0 && dy > 0.0) { // at the image's edge a neighbour is the pixel itself: no cross product, the shading normal
        float3 px = camera_relative_position(uint2(right ? r : l), dx) - rel;
        float3 py = camera_relative_position(uint2(down ? d : u), dy) - rel;
        float3 c = cross(px, py);
        if (dot(c, c) > 1e-12) {
            c = normalize(c);
            geo = dot(c, rel) > 0.0 ? -c : c;
        }
    }
    // HDRP Lit's transmission features (2, and 3 with subsurface scattering): grass blades and leaves, lit through
    // from behind as well.
    bool translucent = feature == 2u || feature == 3u;
    surf[p] = float4(rel + g.cameraPos.xyz, length(rel));
    nrm[p] = uint2(pack_normal(normal), (uint(roughness * 255.0 + 0.5) & 0xFF) | (deferred ? 0x100u : 0u) | (translucent ? 0x200u : 0u) | (pack_normal22(geo) << 10));
    mat[p] = uint2(pack_rgba8(float4(a.rgb, 0.0)), pack_rgba8(float4(linear_to_srgb(saturate(f0)), 0.0)));
}
