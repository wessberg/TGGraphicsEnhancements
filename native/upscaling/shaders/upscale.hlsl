// SPDX-License-Identifier: MIT
// This shader does not upscale. It moves HDRP's render textures into the textures shared with the
// D3D12 device, writes the upscaled image back into HDRP's destination, and, only when an SDK
// refused a frame, fills the destination with a bilinear enlargement so the frame is never black.
cbuffer Parameters : register(b0) {
    uint2 origin; uint2 extent;   // copies: source offset and size; fallback: output size in extent
    float2 sourceScale;           // fallback: render size divided by the source allocation
    float2 halfTexel;             // fallback: half a source texel in UV, to keep filtering inside the render region
};
Texture2D<float4> sourceTexture : register(t0);
#if defined(TGUP_COPY)
// Color, depth (x) and motion vectors (xy) alike: every typed store writes the components its format
// has. HDRP's textures are valid from (0,0); the shared textures are exactly that size.
RWTexture2D<float4> targetTexture : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=extent)) return;
    targetTexture[id.xy]=sourceTexture.Load(int3(origin+id.xy,0));
}
#elif defined(TGUP_FALLBACK)
SamplerState linearClamp : register(s0);
RWTexture2D<float4> targetTexture : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=extent)) return;
    // Clamped half a texel inside the render region: HDRP's allocation is larger, and bilinear
    // filtering at its edge would otherwise blend in pixels this frame never rendered.
    const float2 uv=clamp((float2(id.xy)+0.5)/float2(extent)*sourceScale,halfTexel,sourceScale-halfTexel);
    targetTexture[id.xy]=sourceTexture.SampleLevel(linearClamp,uv,0);
}
#elif defined(TGUP_RASTER)
// For a destination that has no unordered-access view: the same copy through the rasterizer, or the
// bilinear enlargement when origin.x is 1.
SamplerState linearClamp : register(s0);
struct VSOut { float4 position:SV_Position; };
VSOut vsMain(uint vertex:SV_VertexID) {
    VSOut v; v.position=float4(vertex==2?3:-1,vertex==1?3:-1,0,1); return v;
}
float4 psMain(VSOut input):SV_Target {
    if(origin.x==1) return sourceTexture.SampleLevel(linearClamp,clamp(input.position.xy/float2(extent)*sourceScale,halfTexel,sourceScale-halfTexel),0);
    return sourceTexture.Load(int3(uint2(input.position.xy),0));
}
#endif
