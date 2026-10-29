// SPDX-License-Identifier: MIT
// How bright the traced light comes out on screen, for its own exposure: HDRP keeps the game's exposure for what it
// draws itself (flames, fire, glows, fog, the sky, transparent hair: all made for it), and the traced light is scaled by
// a gain the managed side adapts from this (HdrpPasses.UpdateTracedGain). A centre-weighted histogram of every other
// pixel's traced light as the composite shows it (its diffuse light times its color, plus its reflections), at HDRP's
// exposure without that gain (and without its hold on what is already bright: pt_common traced_ungained takes both back
// out): log2 of its luminance from -16 to +8 in 64 bins. tracer.cpp reads it back a few frames
// later as its median and 98th percentile. Pass a.x: the histogram (UAV); a.y: 0 clears it (one group), 1 fills it.
// Each entry is a 4 x 4 block's mean (since 2026-10-11, PT-127), so that the gain is the same whichever denoiser runs:
// metered pixel by pixel (every other one), the noise of an undenoised frame put the median below the light's own (a few
// bright samples among many dark ones) and the gain came out higher, the undenoised picture brighter for its noise alone
// (the tavern: x2.7 with no denoiser, x2.2 with NRD, run 20261011-150230). A block's mean keeps the light's level; its
// noise is a quarter of a pixel's.
#include "pt_common.hlsli"

groupshared uint s_bins[64];

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 id : SV_DispatchThreadID, uint t : SV_GroupIndex) {
    RWStructuredBuffer<uint> bins = ResourceDescriptorHeap[pass.a.x];
    if (pass.a.y == 0u) { bins[t] = 0u; return; }
    s_bins[t] = 0u;
    GroupMemoryBarrierWithGroupSync();
    const uint2 block = id * 4u;
    if (all(block < g.size.xy)) {
        RWTexture2D<float4> outA = ResourceDescriptorHeap[g.scene1.w];
        RWTexture2D<float4> outB = ResourceDescriptorHeap[g.denoise2.w];
        float sum = 0.0;
        uint n = 0u;
        for (uint j = 0u; j < 16u; ++j) {
            const uint2 p = block + uint2(j & 3u, j >> 2);
            if (any(p >= g.size.xy)) continue;
            Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
            // Only what the tracer lights: not the sky, nor a surface HDRP still shades itself.
            if (!s.valid || !s.deferred) continue;
            float4 b = outB[p];
            // B's alpha: a surface drawn forward, whose color compose already multiplied in; -2 see-through glass in front of the
            // sky, whose A is the share of HDRP's sky it lets through, not light (the sky is HDRP's: left out, as the sky is).
            if (b.a <= -1.5) continue;
            // (Hair is a surface like the rest since 2026-10-07: pt_primary. As a layer it was added in here.)
            float3 light = (b.a > 0.5 ? outA[p].rgb : outA[p].rgb * s.albedo) + b.rgb;
            // Without the gain, and without what the gain holds back where the pixel is bright (pt_compose).
            float v = traced_ungained(luminance(light), traced_gain());
            if (!(v >= 0.0 && v < 1e30)) continue;
            sum += v;
            ++n;
        }
        if (n > 0u) {
            const float v = sum / float(n);
            float bin = v > 0.0 ? clamp((log2(v) + 16.0) * (64.0 / 24.0), 0.0, 63.0) : 0.0;
            float2 c = (float2(block) + 2.0) / float2(g.size.xy) - 0.5;
            // The block counts for the pixels it holds (as every other pixel counted before), the centre weighted as before.
            uint weight = (1u + uint(3.0 * saturate(1.0 - 2.0 * length(c * float2(1.0, 0.75))))) * n;
            InterlockedAdd(s_bins[uint(bin)], weight);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_bins[t] != 0u) InterlockedAdd(bins[t], s_bins[t]);
}
