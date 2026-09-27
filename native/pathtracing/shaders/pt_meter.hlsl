// SPDX-License-Identifier: MIT
// How bright the traced light comes out on screen, for its own exposure: HDRP keeps the game's exposure for what it
// draws itself (flames, fire, glows, fog, the sky, transparent hair: all made for it), and the traced light is scaled by
// a gain the managed side adapts from this (HdrpPasses.UpdateTracedGain). A centre-weighted histogram of every other
// pixel's traced light as the composite shows it (its diffuse light times its color, plus its reflections), at HDRP's
// exposure without that gain: log2 of its luminance from -16 to +8 in 64 bins. tracer.cpp reads it back a few frames
// later as its median and 98th percentile. Pass a.x: the histogram (UAV); a.y: 0 clears it (one group), 1 fills it.
#include "pt_common.hlsli"

groupshared uint s_bins[64];

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID, uint t : SV_GroupIndex) {
    RWStructuredBuffer<uint> bins = ResourceDescriptorHeap[pass.a.x];
    if (pass.a.y == 0u) { bins[t] = 0u; return; }
    s_bins[t] = 0u;
    GroupMemoryBarrierWithGroupSync();
    uint2 p = id * 2u;
    if (all(p < g.size.xy)) {
        Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
        // Only what the tracer lights: not the sky, nor a surface HDRP still shades itself.
        if (s.valid && s.deferred) {
            RWTexture2D<float4> outA = ResourceDescriptorHeap[g.scene1.w];
            RWTexture2D<float4> outB = ResourceDescriptorHeap[g.denoise2.w];
            float4 b = outB[p];
            // B's alpha: a surface drawn forward, whose color compose already multiplied in.
            float3 light = (b.a > 0.5 ? outA[p].rgb : outA[p].rgb * s.albedo) + b.rgb;
            float v = luminance(light) / traced_gain_at(s.position);
            float bin = v > 0.0 && v < 1e30 ? clamp((log2(v) + 16.0) * (64.0 / 24.0), 0.0, 63.0) : 0.0;
            float2 c = (float2(p) + 0.5) / float2(g.size.xy) - 0.5;
            uint weight = 1u + uint(3.0 * saturate(1.0 - 2.0 * length(c * float2(1.0, 0.75))));
            InterlockedAdd(s_bins[uint(bin)], weight);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_bins[t] != 0u) InterlockedAdd(bins[t], s_bins[t]);
}
