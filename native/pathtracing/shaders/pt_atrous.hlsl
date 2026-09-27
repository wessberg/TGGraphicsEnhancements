// SPDX-License-Identifier: MIT
// One edge-aware a-trous wavelet iteration (SVGF's spatial stage, Schied et al. 2017): a 5x5 B3-spline
// kernel spread by the step size, weighted down across surfaces, normals and, by the variance around
// the pixel (a 3x3 Gaussian of it, so one noisy estimate does not tear the image into blocks), across
// luminance. The variance is filtered along so each iteration trusts the image a little more; alpha carries its square
// root, the standard deviation (in half precision the variance of a hot spot overflowed).
// Specular signals also stay within similar roughness, and mirror-like surfaces get the first, finest
// iteration only.
// The second iteration feeds the first one's result back into the temporal history (SVGF's feedback),
// which is read at the pixel only: the first iteration is finished by then.
//   pass.a: input, output, step size, 1 for a specular signal   pass.b: history to feed, or INVALID
#include "pt_common.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> input = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass.a.y];
    int step = int(pass.a.z);
    bool specular = pass.a.w == 1;
    float4 center = input[p];
    if (pass.b.x != INVALID) {
        RWTexture2D<float4> history = ResourceDescriptorHeap[pass.b.x];
        history[p] = float4(center.rgb, history[p].a);
    }
    Surface s = load_surface(p, g.surface.x, g.surface.z, INVALID);
    if (!s.valid || !s.deferred || (specular && s.roughness < 0.15 && step > 1)) { output[p] = center; return; }
    const float kernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };
    // The variance the luminance weight trusts: a 3x3 Gaussian around the pixel.
    float variance3 = 0.0, gaussian = 0.0;
    for (int vy = -1; vy <= 1; ++vy) {
        for (int vx = -1; vx <= 1; ++vx) {
            int2 q = clamp(int2(p) + int2(vx, vy), int2(0, 0), int2(g.size.xy) - 1);
            float w = (vx == 0 ? 0.5 : 0.25) * (vy == 0 ? 0.5 : 0.25);
            float deviation = input[q].a;
            variance3 += deviation * deviation * w; gaussian += w;
        }
    }
    variance3 /= gaussian;
    float lc = luminance(center.rgb);
    float sigma = 4.0 * sqrt(max(variance3, 1e-10)) + 1e-4;
    float planeScale = 1.0 / ((0.02 + 0.01 * s.distance) * float(step));
    // Normal-map detail: diffuse light barely changes with it, and a reflection only as much as its lobe is
    // narrow; glossy surfaces keep their normals' detail, rough ones are filtered across it.
    float normalPower = specular ? lerp(32.0, 4.0, saturate(s.roughness * 2.5)) : 4.0;
    float3 sum = center.rgb; float variance = center.a * center.a; float weights = 1.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) continue;
            int2 q = int2(p) + int2(x, y) * step;
            if (any(q < 0) || any(q >= int2(g.size.xy))) continue;
            Surface sq = load_surface(uint2(q), g.surface.x, g.surface.z, INVALID);
            if (!sq.valid || !sq.deferred) continue;
            float4 c = input[q];
            // Edges by the surfaces' own orientation; normal-map detail only softly, or it keeps the grain.
            float wn = pow(saturate(dot(s.geoNormal, sq.geoNormal)), 64.0) * pow(saturate(dot(s.normal, sq.normal)), normalPower);
            float wp = exp(-abs(dot(s.geoNormal, sq.position - s.position)) * planeScale);
            float wl = exp(-abs(luminance(c.rgb) - lc) / sigma);
            float wr = specular ? exp(-abs(sq.roughness - s.roughness) * 12.0) : 1.0;
            float w = kernel[abs(x)] * kernel[abs(y)] / (kernel[0] * kernel[0]) * wn * wp * wl * wr;
            sum += c.rgb * w; variance += c.a * c.a * w * w; weights += w;
        }
    }
    output[p] = float4(sum / weights, sqrt(variance) / weights);
}
