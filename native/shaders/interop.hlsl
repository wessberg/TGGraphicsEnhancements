// SPDX-License-Identifier: MIT
// This shader does not interpolate frames. It prepares API-shareable buffers
// and converts scRGB output to HDR10; NVIDIA Streamline performs DLSS-G.
cbuffer CopyParameters : register(b0) {
    uint2 origin; uint2 extent;
};
Texture2D<float4> sourceTexture : register(t0);
#if defined(TGFG_DEPTH)
RWTexture2D<float> targetTexture : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=extent)) return;
    targetTexture[id.xy]=sourceTexture.Load(int3(origin+id.xy,0)).x;
}
#elif defined(TGFG_MOTION)
RWTexture2D<float2> targetTexture : register(u0);
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
    if(any(id.xy>=extent)) return;
    targetTexture[id.xy]=sourceTexture.Load(int3(origin+id.xy,0)).xy;
}
#elif defined(TGFG_HDR)
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
float4 psMain(VSOut input):SV_Target {
    float3 rgb=sourceTexture.Load(int3(uint2(input.position.xy),0)).rgb;
    float3 bt2020=float3(dot(rgb,float3(0.6274039,0.3292830,0.0433131)),
        dot(rgb,float3(0.0690973,0.9195404,0.0113623)),
        dot(rgb,float3(0.0163914,0.0880133,0.8955953)));
    return float4(encodePQ(bt2020*80.0),1.0);
}
#endif
