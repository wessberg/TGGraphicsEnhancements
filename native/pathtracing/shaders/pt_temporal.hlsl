// SPDX-License-Identifier: MIT
// Temporal accumulation of one signal (SVGF's first stage, Schied et al. 2017). The previous frame's
// history is fetched at the pixel's reprojected position through HDRP's motion vectors, from the four
// nearest texels whose surfaces match this pixel's, and blended with this frame's value by the number
// of frames accumulated. With moments, the luminance's first two moments give the variance the
// a-trous passes steer by; without, the history's alpha holds the accumulated length.
// Reflections are filtered as they are, not divided by their directional albedo: measured in the harness,
// dividing gave normal-mapped surfaces more sparkle and flicker (small lights' highlights), not less.
//   pass.a: raw, history out, history in, moments out   pass.b: moments in, maximum length, flags
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> raw = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> historyOut = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float4> historyIn = ResourceDescriptorHeap[pass.a.z];
    bool moments = pass.a.w != INVALID;
    float maxLength = float(pass.b.y);
    float3 current = raw[p].rgb;
    Surface s = load_surface(p, g.surface.x, g.surface.z, INVALID);
    if (!s.valid || !s.deferred) {
        historyOut[p] = float4(current, 0.0);
        if (moments) { RWTexture2D<float4> mo = ResourceDescriptorHeap[pass.a.w]; mo[p] = 0.0; }
        return;
    }
    float3 history = 0.0; float4 historyMoments = 0.0; float weightSum = 0.0; float historyLength = 0.0;
    if ((g.size.w & (FLAG_RESET | FLAG_DENOISER_RESET)) == 0) {
        Texture2D<float2> motion = ResourceDescriptorHeap[g.inputs1.y];
        float2 mv = motion.Load(int3(p, 0));
        float2 prevPixel = ((float2(p) + 0.5) / float2(g.size.xy) - mv) * float2(g.size.xy) - 0.5;
        int2 base = int2(floor(prevPixel));
        float2 f = prevPixel - float2(base);
        RWTexture2D<float4> momentsIn = ResourceDescriptorHeap[moments ? pass.b.x : pass.a.z];
        [unroll] for (int i = 0; i < 4; ++i) {
            int2 o = int2(i & 1, i >> 1);
            int2 q = base + o;
            if (any(q < 0) || any(q >= int2(g.size.xy))) continue;
            Surface sp = load_surface(uint2(q), g.surface.y, g.surface.w, INVALID);
            sp.position += g.prevOriginDelta.xyz;
            if (!sp.valid || !sp.deferred || dot(sp.geoNormal, s.geoNormal) < 0.85) continue;
            if (abs(dot(s.geoNormal, sp.position - s.position)) > 0.05 + 0.02 * s.distance) continue;
            float w = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
            if (w <= 0.0) continue;
            float4 h = historyIn[q];
            history += h.rgb * w;
            if (moments) historyMoments += momentsIn[q] * w;
            else historyLength += h.a * w;
            weightSum += w;
        }
    }
    float len;
    if (weightSum > 0.01) {
        history /= weightSum;
        if (moments) { historyMoments /= weightSum; len = historyMoments.z; } else len = historyLength / weightSum;
    } else { len = 0.0; }
    len = min(len + 1.0, maxLength);
    float alpha = len > 1.0 ? max(1.0 / len, 1.0 / maxLength) : 1.0;
    float3 color = lerp(history, current, alpha);
    if (!all(isfinite(color))) color = current;
    if (moments) {
        RWTexture2D<float4> momentsOut = ResourceDescriptorHeap[pass.a.w];
        float l = luminance(current);
        float2 m = len > 1.0 ? lerp(historyMoments.xy, float2(l, l * l), alpha) : float2(l, l * l);
        // The spread of single frames over time. ReSTIR reuses a sample for about a frame and its temporal
        // neighbour is permuted, so the frames differ and this is a fair measure (a 3x3 spread of the raw
        // signal used to stand in for it, which took every sharp shadow edge for noise).
        float variance = max(0.0, m.y - m.x * m.x);
        // The a-trous passes filter the accumulated value, whose noise is that spread over the frames it holds.
        // Steered by the single frames' spread, they took every shadow smaller than the noise of one frame for
        // noise: a chair's or a rope net's shadow was blurred away.
        variance /= len;
        // Few frames of history: their variance says little, so filter hard until it settles.
        if (len < 4.0) variance = max(variance, l * l * (4.0 - len));
        momentsOut[p] = float4(m, len, 0.0);
        historyOut[p] = float4(color, variance);
    } else {
        historyOut[p] = float4(color, len);
    }
}
