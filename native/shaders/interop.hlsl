// SPDX-License-Identifier: MIT
// This shader does not interpolate frames. It prepares API-shareable buffers
// and converts scRGB output to HDR10; NVIDIA Streamline performs DLSS-G.
cbuffer CopyParameters : register(b0) {
    uint2 origin; uint2 extent;
};
Texture2D<float4> sourceTexture : register(t0);
#if defined(TGFG_DEPTH) || defined(TGFG_DEPTH_FLIP)
RWTexture2D<float> targetTexture : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=extent)) return;
    uint2 p=id.xy;
#if defined(TGFG_DEPTH_FLIP)
    p.y=extent.y-1-p.y;
#endif
    targetTexture[id.xy]=sourceTexture.Load(int3(origin+p,0)).x;
}
#elif defined(TGFG_MOTION) || defined(TGFG_MOTION_FLIP)
RWTexture2D<float2> targetTexture : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=extent)) return;
    uint2 p=id.xy;
#if defined(TGFG_MOTION_FLIP)
    p.y=extent.y-1-p.y;
#endif
    // Reorder rows here; the matching Y-vector and jitter signs are changed in the constants.
    targetTexture[id.xy]=sourceTexture.Load(int3(origin+p,0)).xy;
}
#elif defined(TGFG_HDR) || defined(TGFG_HUDCOLOR)
// scRGB uses linear BT.709 primaries, 1.0 = 80 nits. Convert primaries BEFORE
// clipping negative channels, preserving representable wide-gamut colors.
float3 encodePQ(float3 nits) {
    float3 y=pow(saturate(nits/10000.0),2610.0/16384.0);
    return pow((3424.0/4096.0+(2413.0/128.0)*y)/(1.0+(2392.0/128.0)*y),2523.0/32.0);
}
struct VSOut { float4 position:SV_Position; };
VSOut vsMain(uint vertex:SV_VertexID) {
    VSOut v; v.position=float4(vertex==2?3:-1,vertex==1?3:-1,0,1); return v;
}
// Match the installed RenoDX final pixel shader's signature register order.
struct RenoVSOut { float2 uv:TEXCOORD0; float4 position:SV_Position; };
RenoVSOut vsReno(uint vertex:SV_VertexID) {
    RenoVSOut v;v.position=float4(vertex==2?3:-1,vertex==1?3:-1,0,1);
    v.uv=float2(v.position.x*0.5+0.5,0.5-v.position.y*0.5);return v;
}
float4 psMain(VSOut input):SV_Target {
    uint2 p=uint2(input.position.xy);
#if defined(TGFG_HUDCOLOR)
    // origin: flip Y, color mode. extent is the valid viewport, not the allocation.
    if(origin.x!=0) p.y=extent.y-1-p.y;
#endif
    float3 rgb=sourceTexture.Load(int3(p,0)).rgb;
#if defined(TGFG_HUDCOLOR)
    if(origin.y==4) return float4(rgb,1); // preserve linear scene for RenoDX's live final pass
    if(origin.y==0) { // HDRP's SDR color is linear (including an sRGB SRV's automatic decode).
        rgb=max(rgb,0.0);
        float3 high=1.055*pow(rgb,1.0/2.4)-0.055;
        return float4(rgb.r<=0.0031308?12.92*rgb.r:high.r,
                      rgb.g<=0.0031308?12.92*rgb.g:high.g,
                      rgb.b<=0.0031308?12.92*rgb.b:high.b,1);
    }
    if(origin.y==3) return float4(encodePQ(rgb),1); // HDRP Rec2020 nits, before final OETF
#endif
    float3 bt2020=float3(dot(rgb,float3(0.6274039,0.3292830,0.0433131)),
        dot(rgb,float3(0.0690973,0.9195404,0.0113623)),
        dot(rgb,float3(0.0163914,0.0880133,0.8955953)));
#if defined(TGFG_HUDCOLOR)
    return float4(encodePQ(bt2020*(origin.y==2?1.0:80.0)),1.0);
#else
    return float4(encodePQ(bt2020*80.0),1.0);
#endif
}
#elif defined(TGFG_HUDCHECK)
// Three counts over a sample grid (origin.x: stride in pixels, extent: grid size). [0]: points where
// the frame about to be presented differs from the copy taken before the overlay UI was drawn; a HUD
// covers a few percent, something that redraws the frame after the capture differs everywhere.
// [1]: points where that copy differs from the previously presented frame. [2]: points where the
// presented frame itself differs from the previously presented one. A still image (menu, pause,
// loading) leaves [1] and [2] at zero and proves nothing; [2] without [1] means the copy is stale,
// i.e. the game renders into another buffer (a swapchain proxy) and the bridge's back buffer is
// only written at Present.
Texture2D<float4> hudlessTexture : register(t1);
Texture2D<float4> previousTexture : register(t2);
RWStructuredBuffer<uint> differing : register(u0);
groupshared uint groupDiffering, groupChanged, groupMoving;
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID,uint index:SV_GroupIndex) {
    if(index==0) { groupDiffering=0; groupChanged=0; groupMoving=0; }
    GroupMemoryBarrierWithGroupSync();
    if(all(id.xy<extent)) {
        int3 p=int3(id.xy*origin.x+origin.x/2,0);
        float3 a=sourceTexture.Load(p).rgb, b=hudlessTexture.Load(p).rgb, c=previousTexture.Load(p).rgb;
        if(any(abs(a-b)>0.02)) InterlockedAdd(groupDiffering,1u);
        if(any(abs(b-c)>0.002)) InterlockedAdd(groupChanged,1u);
        if(any(abs(a-c)>0.002)) InterlockedAdd(groupMoving,1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if(index==0) {
        if(groupDiffering) InterlockedAdd(differing[0],groupDiffering);
        if(groupChanged) InterlockedAdd(differing[1],groupChanged);
        if(groupMoving) InterlockedAdd(differing[2],groupMoving);
    }
}
#endif
