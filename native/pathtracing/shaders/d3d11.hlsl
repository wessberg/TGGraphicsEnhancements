// SPDX-License-Identifier: MIT
// TG Path Tracing's work on the game's D3D11 device: HDRP's textures into the textures shared with
// the D3D12 tracer, materials' textures into the shared atlas, the sky into a latitude-longitude map,
// and the tracer's result into HDRP's color buffer. Compiled at run time, one entry point per macro.
cbuffer Constants : register(b0) { uint4 c0; float4 c1; };

#if defined(TGPT_COPY_ALBEDO)
// GBuffer0 keeps its sRGB encoding (a UNORM view copies the bytes; an sRGB-only view is re-encoded);
// alpha becomes HDRP's "deferred lighting" stencil bit, which marks the pixels the GBuffer describes.
Texture2D<float4> source : register(t0);
Texture2D<uint2> stencil : register(t1);
RWTexture2D<unorm float4> target : register(u0);
float3 encode(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055; }
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= c0.xy)) return;
    float4 v = source.Load(int3(p, 0));
    if (c0.z & 1) v.rgb = encode(v.rgb);
    uint s = (c0.z & 2) ? stencil.Load(int3(p, 0)).g : 2u;
    target[p] = float4(v.rgb, (s & 2u) ? 1.0 : 0.0);
}
#elif defined(TGPT_COPY_FLOAT4)
Texture2D<float4> source : register(t0);
RWTexture2D<float4> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) { if (all(p < c0.xy)) target[p] = source.Load(int3(p, 0)); }
#elif defined(TGPT_COPY_DEPTH)
Texture2D<float> source : register(t0);
RWTexture2D<float> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) { if (all(p < c0.xy)) target[p] = source.Load(int3(p, 0)); }
#elif defined(TGPT_COPY_MOTION)
Texture2D<float2> source : register(t0);
RWTexture2D<float2> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) { if (all(p < c0.xy)) target[p] = (c0.z & 1) ? source.Load(int3(p, 0)) : 0.0; }
#elif defined(TGPT_COPY_EXPOSURE)
Texture2D<float2> source : register(t0);
RWTexture2D<float> target : register(u0);
[numthreads(1, 1, 1)] void main() { target[uint2(0, 0)] = (c0.z & 1) ? source.Load(int3(0, 0, 0)).x : c1.x; }
#elif defined(TGPT_SKY_CUBE) || defined(TGPT_SKY_CUBEARRAY)
#if defined(TGPT_SKY_CUBE)
TextureCube<float4> source : register(t0);
#else
TextureCubeArray<float4> source : register(t0);
#endif
SamplerState linearSampler : register(s0);
RWTexture2D<float4> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= c0.xy)) return;
    float u = (p.x + 0.5) / c0.x, v = (p.y + 0.5) / c0.y;
    float phi = (u - 0.5) * 2.0 * 3.14159265, theta = v * 3.14159265;
    float3 dir = float3(sin(theta) * sin(phi), cos(theta), sin(theta) * cos(phi));
#if defined(TGPT_SKY_CUBE)
    float4 c = source.SampleLevel(linearSampler, dir, c1.x);
#else
    float4 c = source.SampleLevel(linearSampler, float4(dir, 0.0), c1.x);
#endif
    target[p] = float4(max(c.rgb, 0.0), 1.0);
}
#elif defined(TGPT_ATLAS)
// One material texture into its 64x64 tile: sampled at the mip nearest the tile's size, 2x2 each texel.
Texture2D<float4> source : register(t0);
SamplerState wrapSampler : register(s0);
RWTexture2D<unorm float4> target : register(u0);
float3 encode(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055; }
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= 64)) return;
    float4 sum = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        float2 uv = (float2(p) + float2(0.25 + 0.5 * (i & 1), 0.25 + 0.5 * (i >> 1))) / 64.0;
        sum += source.SampleLevel(wrapSampler, uv, c1.x);
    }
    sum *= 0.25;
    // Linear textures (c0.z bit 1) hold colors already in the space the atlas stores: sRGB.
    float3 rgb = (c0.z & 1) ? saturate(sum.rgb) : encode(saturate(sum.rgb));
    target[c0.xy + p] = float4(rgb, saturate(sum.a));
}
#elif defined(TGPT_SKIN)
// Characters' skinned positions from the game's skinning output into this frame's shared texture, a vertex a
// texel and 4096 a row; one dispatch a character. c0: first vertex there, count, first texel, stride in bytes.
#if defined(SKIN_RAW)
ByteAddressBuffer source : register(t0);
float3 load_position(uint i) { return asfloat(source.Load3(i * c0.w)); }
#else
struct SkinVertex {
    float3 position;
#if SKIN_REST > 0
    uint rest[SKIN_REST];
#endif
};
StructuredBuffer<SkinVertex> source : register(t0);
float3 load_position(uint i) { return source[i].position; }
#endif
RWTexture2D<float4> target : register(u0);
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= c0.y) return;
    uint t = c0.z + id.x;
    target[uint2(t % 4096u, t / 4096u)] = float4(load_position(c0.x + id.x), 1.0);
}
#elif defined(TGPT_ALPHA)
// An alpha-tested material's texture into its 256x256 alpha tile (R8), the alpha test's own resolution: the
// 64x64 color tile averaged straw, rope nets and leaves into opaque blocks. 2x2 samples a texel.
Texture2D<float4> source : register(t0);
SamplerState wrapSampler : register(s0);
RWTexture2D<unorm float> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= 256)) return;
    float sum = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        float2 uv = (float2(p) + float2(0.25 + 0.5 * (i & 1), 0.25 + 0.5 * (i >> 1))) / 256.0;
        sum += source.SampleLevel(wrapSampler, uv, c1.x).a;
    }
    target[c0.xy + p] = saturate(sum * 0.25);
}
#elif defined(TGPT_COMPOSITE) || defined(TGPT_COMPOSITE_RASTER)
// The previous frame's traced light applied to this frame: each pixel finds its previous position
// through HDRP's motion vectors and blends the four texels around it bilinearly, each only if its
// surface was this one (its distance from the previous camera matches); with none matching, the best
// of the eight neighbours of the nearest texel. The light is multiplied by this frame's diffuse color.
// Returns (rgb, a): the pixel becomes color * a + rgb.
cbuffer Composite : register(b1) {
    float4x4 invViewProj;   // this frame's, camera-relative (Unity's column-major memory order)
    float4 cameraDelta;     // xyz: this camera minus the previous one; w unused
    uint4 frameSize;        // width, height, 1 for the nearest texel (Ray Reconstruction), debug view
};
Texture2D<float4> albedo : register(t0);   // this frame: sRGB diffuse color, a: deferred
Texture2D<float4> emission : register(t1); // this frame: GBuffer3, pre-exposed
Texture2D<float> depth : register(t2);     // this frame
Texture2D<float2> motion : register(t3);   // this frame
Texture2D<float4> tracedA : register(t4);  // previous frame: diffuse light, distance
Texture2D<float4> tracedB : register(t5);  // previous frame: specular light
Texture2D<float> exposureNow : register(t6);    // HDRP's exposure this frame, which its color is pre-exposed with
Texture2D<float> exposureTraced : register(t7); // the one the traced light was pre-exposed with, a frame earlier
float3 decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((abs(c) + 0.055) / 1.055, 2.4); }
float4 composite(uint2 p) {
    float2 size = float2(frameSize.xy);
    float2 uv = (float2(p) + 0.5) / size;
    float2 prevUv = uv - motion.Load(int3(p, 0));
    int2 q = int2(floor(prevUv * size));
    bool nearest = frameSize.z != 0;
    if (frameSize.w != 0) {
        q = clamp(q, int2(0, 0), int2(frameSize.xy) - 1);
        return float4(tracedA.Load(int3(q, 0)).rgb, 0.0);
    }
    float d = depth.Load(int3(p, 0));
    float4 a = albedo.Load(int3(p, 0));
    if (!(d > 0.0) || a.a < 0.5) return float4(0, 0, 0, 1);
    float4 w = mul(invViewProj, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0));
    float dist = length(w.xyz / w.w + cameraDelta.xyz);
    float tolerance = 0.03 * dist + 0.05;
    float3 diffuse = 0.0, specular = 0.0; float weights = 0.0;
    float2 texel = prevUv * size - 0.5;
    int2 corner = int2(floor(texel));
    float2 f = texel - float2(corner);
    [unroll] for (int i = 0; i < 4 && !nearest; ++i) {
        int2 o = int2(i & 1, i >> 1), c = corner + o;
        if (any(c < 0) || any(c >= int2(frameSize.xy))) continue;
        float4 t = tracedA.Load(int3(c, 0));
        float wgt = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
        if (!(t.a > 0.0) || abs(t.a - dist) > tolerance || wgt <= 0.0) continue;
        diffuse += t.rgb * wgt; specular += tracedB.Load(int3(c, 0)).rgb * wgt; weights += wgt;
    }
    if (weights > 1e-3) { diffuse /= weights; specular /= weights; }
    else {
        // No texel around the position was this surface (it was hidden or off screen last frame): the closest
        // match near it, first among the eight neighbours, then two and four texels out in eight directions,
        // where the background a moving edge just uncovered was still visible.
        const int2 offsets[9] = { int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1), int2(-1, -1), int2(1, -1), int2(-1, 1), int2(1, 1) };
        int2 best = int2(-1, -1); float bestError = tolerance;
        [unroll] for (int i = 0; i < 9; ++i) {
            int2 c = q + offsets[i];
            if (any(c < 0) || any(c >= int2(frameSize.xy))) continue;
            float stored = tracedA.Load(int3(c, 0)).a;
            float error = abs(stored - dist) + (i == 0 ? 0.0 : tolerance * 0.25);
            if (stored > 0.0 && error < bestError) { bestError = error; best = c; }
        }
        bestError = tolerance * 1.5;
        [unroll] for (int ring = 2; ring <= 4 && best.x < 0; ring *= 2) {
            [unroll] for (int i = 1; i < 9; ++i) {
                int2 c = q + offsets[i] * ring;
                if (any(c < 0) || any(c >= int2(frameSize.xy))) continue;
                float stored = tracedA.Load(int3(c, 0)).a;
                float error = abs(stored - dist);
                if (stored > 0.0 && error < bestError) { bestError = error; best = c; }
            }
        }
        // Still nothing (a disocclusion, or the band a turning camera just brought into view): the light of the nearest
        // traced texel, whatever surface it was. HDRP's own lighting, which was left there, draws the fill lights the
        // tracer leaves out: those pixels started far brighter for a frame, and the denoiser's history then took
        // seconds to let go of it (new parts of the view went from bright to dark, like screen-space GI).
        if (best.x < 0) {
            int2 c = clamp(q, int2(0, 0), int2(frameSize.xy) - 1);
            const int rings[4] = { 0, 2, 4, 8 };
            [unroll] for (int k = 0; k < 4; ++k) {
                [unroll] for (int i = 0; i < 9; ++i) {
                    int2 n = clamp(c + offsets[i] * rings[k], int2(0, 0), int2(frameSize.xy) - 1);
                    if (best.x < 0 && tracedA.Load(int3(n, 0)).a > 0.0) best = n;
                }
            }
            if (best.x < 0) return float4(0, 0, 0, 1);
        }
        diffuse = tracedA.Load(int3(best, 0)).rgb; specular = tracedB.Load(int3(best, 0)).rgb;
    }
    float3 base = decode(a.rgb);
    // The traced light was pre-exposed with the previous frame's exposure: into this frame's, as auto exposure adapts.
    float en = exposureNow.Load(int3(0, 0, 0)), et = exposureTraced.Load(int3(0, 0, 0));
    float ratio = (en > 0.0 && et > 0.0 && en < 1e20 && et < 1e20) ? en / et : 1.0;
    diffuse *= ratio; specular *= ratio;
    float3 e = emission.Load(int3(p, 0)).rgb;
    // HDRP's tag for "no emission, ambient occlusion in green" (Lit.hlsl AO_IN_GBUFFER3_TAG).
    if (abs(e.r - 2048.0) < 1.0 && abs(e.b - 1024.0) < 1.0) e = 0.0;
    return float4(max(e + base * diffuse + specular, 0.0), 0.0);
}
#if defined(TGPT_COMPOSITE)
RWTexture2D<float4> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= frameSize.xy)) return;
    float4 o = composite(p);
    if (o.a == 1.0 && all(o.rgb == 0.0)) return;
    float4 c = target[p];
    target[p] = float4(c.rgb * o.a + o.rgb, c.a);
}
#else
float4 vsMain(uint id : SV_VertexID) : SV_Position { float2 uv = float2((id << 1) & 2, id & 2); return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }
// Blended as source + destination * source alpha, with the destination's alpha kept.
float4 psMain(float4 position : SV_Position) : SV_Target { return composite(uint2(position.xy)); }
#endif
#elif defined(TGPT_GUIDES)
// DLSS Ray Reconstruction's guide buffers for this frame, from its GBuffer copies (NVIDIA's DLSS-RR
// integration guide, section 3.4): linear diffuse albedo; specular albedo, the specular color's average
// reflectance at this view angle and roughness (the guide's EnvBRDFApprox2); world-space normals with
// linear roughness (HDRP's perceptual roughness, whose square is GGX's alpha) in alpha. Sky: albedo 0.5,
// the rest 0, as the guide asks. Surfaces HDRP draws forward keep their normals and get a gray albedo.
cbuffer Composite : register(b1) {
    float4x4 invViewProj;   // this frame's, camera-relative
    float4 cameraDelta;
    uint4 frameSize;        // width, height
};
Texture2D<float4> albedo : register(t0);    // sRGB diffuse color, a: deferred
Texture2D<float4> normals : register(t1);   // HDRP's normal buffer: octahedral normal 12:12 in RGB, roughness in A
Texture2D<float4> specular : register(t2);  // GBuffer2: specular color (fast sRGB), feature in A's low bits
Texture2D<float> depth : register(t3);
RWTexture2D<unorm float4> diffuseOut : register(u0);
RWTexture2D<unorm float4> specularOut : register(u1);
RWTexture2D<float4> normalOut : register(u2);
float3 decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((abs(c) + 0.055) / 1.055, 2.4); }
float3 fast_srgb_to_linear(float3 c) { return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878); }
float3 decode_normal(float4 v) {
    uint3 i = (uint3)(v.rgb * 255.5);
    uint hi = i.z >> 4, lo = i.z & 15;
    uint2 cb = i.xy | uint2(lo << 8, hi << 8);
    float2 f = float2(cb) / 4095.0 * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}
// NVIDIA's DLSS-RR integration guide, appendix (Ray Tracing Gems, chapter 32).
float3 env_brdf_approx2(float3 specularColor, float alpha, float nov) {
    nov = abs(nov);
    float4 x = float4(1.0, nov, nov * nov, nov * nov * nov);
    float4 y = float4(1.0, alpha, alpha * alpha, alpha * alpha * alpha);
    float2x2 m1 = float2x2(0.99044, -1.28514, 1.29678, -0.755907);
    float3x3 m2 = float3x3(1.0, 2.92338, 59.4188, 20.3225, -27.0302, 222.592, 121.563, 626.13, 316.627);
    float2x2 m3 = float2x2(0.0365463, 3.32707, 9.0632, -9.04756);
    float3x3 m4 = float3x3(1.0, 3.59685, -1.36772, 9.04401, -16.3174, 9.22949, 5.56589, 19.7886, -20.2123);
    float bias = dot(mul(m1, x.xy), y.xy) * rcp(dot(mul(m2, x.xyw), y.xyw));
    float scale = dot(mul(m3, x.xy), y.xy) * rcp(dot(mul(m4, x.xzw), y.xyw));
    bias *= saturate(specularColor.g * 50.0); // specular reflectance of 0
    return mad(specularColor, max(0.0, scale), max(0.0, bias));
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= frameSize.xy)) return;
    float d = depth.Load(int3(p, 0));
    if (!(d > 0.0)) { diffuseOut[p] = float4(0.5, 0.5, 0.5, 1.0); specularOut[p] = 0.0; normalOut[p] = 0.0; return; }
    float4 a = albedo.Load(int3(p, 0));
    float4 nb = normals.Load(int3(p, 0));
    float3 n = decode_normal(nb);
    float roughness = lerp(saturate(nb.a), 0.05, cameraDelta.w); // Reflection Boost, as the tracer applies it
    float2 uv = (float2(p) + 0.5) / float2(frameSize.xy);
    float4 w = mul(invViewProj, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0));
    float3 v = -normalize(w.xyz / w.w);
    bool deferred = a.a > 0.5;
    float3 f0 = 0.04;
    if (deferred) {
        float4 s = specular.Load(int3(p, 0));
        if ((uint(s.a * 255.0 + 0.5) & 7u) == 0u) f0 = fast_srgb_to_linear(s.rgb);
        f0 = lerp(f0, 0.6, 0.7 * cameraDelta.w);
    }
    diffuseOut[p] = float4(deferred ? decode(a.rgb) : 0.5, 1.0);
    specularOut[p] = float4(saturate(env_brdf_approx2(f0, roughness * roughness, dot(n, v))), 1.0);
    normalOut[p] = float4(n, roughness);
}
#endif
