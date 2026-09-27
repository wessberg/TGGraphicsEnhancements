// SPDX-License-Identifier: MIT
// TG Path Tracing's work on the game's D3D11 device: HDRP's textures into the textures shared with
// the D3D12 tracer, materials' textures into the shared atlas, the sky into a latitude-longitude map,
// and the tracer's result into HDRP's color buffer. Compiled at run time, one entry point per macro.
cbuffer Constants : register(b0) { uint4 c0; float4 c1; };

#if defined(TGPT_COMPOSITE) || defined(TGPT_GUIDES)
// Ray Reconstruction's guides, which the guides pass writes and the composite corrects on see-through glass.
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
#endif

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
#elif defined(TGPT_COPY_WATER_DEPTH)
// HDRP's depth where its transparent SSR step gives a surface reflections (the stencil bit in c0.z, HDRP's TraceReflectionRay:
// its water, and transparent surfaces that take them), 0 everywhere else.
Texture2D<float> source : register(t0);
Texture2D<uint2> stencil : register(t1);
RWTexture2D<float> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= c0.xy)) return;
    target[p] = (stencil.Load(int3(p, 0)).g & c0.z) != 0u ? source.Load(int3(p, 0)) : 0.0;
}
#elif defined(TGPT_WATER)
// The previous frame's traced reflections on water in place of HDRP's reflections for its water and transparent surfaces:
// HDRP's shading takes its reflection from its SSR texture (what its SSR found in the last frame's image, premultiplied by
// its weight in alpha) and leaves the rest of the weight to reflection probes and the sky. The traced reflection replaces
// all of it, SSR's own hits too (they miss what is off screen or hidden, fade out at the screen's edges and on waves, and
// show last frame's image), with the weight 1, so HDRP's shading takes the reflection as it stands (its own Fresnel term,
// foam, refraction and fog stay the game's); the SSR texture is only where HDRP's water reads it from. Each pixel takes the traced frame's light around where its own surface was then: 5x5 texels
// weighted as a Gaussian of 1.5 texels, each only if its water lay where this pixel's does and its reflection met
// something as far away as the nearest texel's did (no denoiser sees HDRP's water, and accumulated over frames alone a
// reflection of what bounced light lights kept a fifth of a frame's grain; the edges between near and far things in it
// stay sharp). With none (the traced frame saw no water there: the frame's delay uncovering it), HDRP's stays. The traced light was pre-exposed with the traced frame's
// exposure, which is the one HDRP takes its SSR texture back out of (its previous frame's), and the whole is divided by
// HDRP's reflection lighting multiplier, which it applies to a reflection's every part.
cbuffer Water : register(b1) {
    float4x4 invViewProj;       // this frame's, camera-relative (Unity's column-major memory order)
    float4x4 tracedViewProj;    // the traced frame's, relative to its camera
    float4x4 tracedInvViewProj; // the traced frame's inverse
    float4 cameraDelta;         // xyz: this camera minus the traced frame's; w: 1 / HDRP's reflection lighting multiplier
    uint4 frameSize;            // width, height
};
Texture2D<float> waterDepth : register(t0);   // this frame: HDRP's depth where it takes reflections, 0 elsewhere
Texture2D<float4> traced : register(t2);      // the traced frame: reflected light pre-exposed, a its hit distance (0: none)
Texture2D<float> tracedDepth : register(t3);  // the traced frame's water depth
RWTexture2D<float4> target : register(u0);    // HDRP's reflections
float3 camera_relative(float4x4 inverse, float2 uv, float d) { float4 w = mul(inverse, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0)); return w.xyz / w.w; }
// The traced frame's texel c, if its water lay where this pixel's does (dist from the traced frame's camera) and it has a
// reflection: its light and hit distance.
bool traced_texel(int2 c, float2 size, float dist, float tolerance, out float4 t) {
    t = 0.0;
    if (any(c < 0) || any(c >= int2(frameSize.xy))) return false;
    t = traced.Load(int3(c, 0));
    const float td = tracedDepth.Load(int3(c, 0));
    if (!(t.a > 0.0) || !(td > 0.0)) return false;
    return abs(length(camera_relative(tracedInvViewProj, (float2(c) + 0.5) / size, td)) - dist) <= tolerance;
}
// Whether two reflections met things about as far away (the sky, 65504, counts as one distance).
bool same_reach(float a, float b) {
    const bool skyA = a >= 32752.0, skyB = b >= 32752.0;
    return skyA == skyB && (skyA || abs(a - b) <= 0.15 * b + 0.2);
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= frameSize.xy)) return;
    const float d = waterDepth.Load(int3(p, 0));
    if (!(d > 0.0)) return;
    const float2 size = float2(frameSize.xy);
    const float3 rel = camera_relative(invViewProj, (float2(p) + 0.5) / size, d) + cameraDelta.xyz; // from the traced frame's camera
    const float dist = length(rel);
    const float4 clip = mul(tracedViewProj, float4(rel, 1.0));
    if (clip.w <= 1e-4) return;
    const float2 texel = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5) * size - 0.5;
    const int2 center = int2(round(texel));
    const float tolerance = 0.03 * dist + 0.05;
    // The nearest texel with this pixel's surface: the others' reflections must have met something as far away as its did,
    // and about as bright (a Gaussian of 0.5 in log luminance: the texels hold light accumulated over frames, whose grain is
    // a few tenths of a frame's, so a ratio of several is an edge, a lamp beside leaves as far away, not noise).
    float reach = -1.0, nearest = 1e30, level = 0.0;
    [unroll] for (int k = 0; k < 9; ++k) {
        const int2 c = center + int2(k % 3 - 1, k / 3 - 1);
        float4 t;
        if (!traced_texel(c, size, dist, tolerance, t)) continue;
        const float2 o = float2(c) - texel;
        if (dot(o, o) < nearest) { nearest = dot(o, o); reach = t.a; level = dot(t.rgb, float3(0.2126, 0.7152, 0.0722)); }
    }
    if (reach < 0.0) return;
    float3 fill = 0.0; float weights = 0.0;
    [unroll] for (int i = 0; i < 25; ++i) {
        const int2 c = center + int2(i % 5 - 2, i / 5 - 2);
        float4 t;
        if (!traced_texel(c, size, dist, tolerance, t) || !same_reach(t.a, reach)) continue;
        const float2 o = float2(c) - texel;
        const float l = dot(t.rgb, float3(0.2126, 0.7152, 0.0722)), least = 1e-3 * max(l, level) + 1e-7;
        const float ratio = log((l + least) / (level + least));
        const float w = exp(-dot(o, o) * (1.0 / (2.0 * 1.5 * 1.5)) - ratio * ratio * (1.0 / (2.0 * 0.5 * 0.5)));
        fill += t.rgb * w; weights += w;
    }
    if (weights < 1e-4) return;
    fill /= weights;
    target[p] = float4(fill * cameraDelta.w, 1.0);
}
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
    // A mask map (c0.z bit 2, its bytes as they are): metallic and smoothness from the channels c0.w names (bits 0-1, 2-3), in
    // red and alpha, each inverted, so that a tile not baked yet (white) reads as their low ends (pt_common.hlsli
    // metallic_smoothness), with the usual remaps a dielectric at its roughest, not a mirror of metal. Green and blue stay
    // the texture's.
    if (c0.z & 2) {
        const float4 raw = float4(rgb, saturate(sum.a));
        const uint m = c0.w & 3u, s = (c0.w >> 2) & 3u;
        target[c0.xy + p] = float4(1.0 - (m == 0u ? raw.r : m == 1u ? raw.g : m == 2u ? raw.b : raw.a), raw.g, raw.b,
                                   1.0 - (s == 0u ? raw.r : s == 1u ? raw.g : s == 2u ? raw.b : raw.a));
        return;
    }
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
#elif defined(TGPT_COOKIE_CUBE) || defined(TGPT_COOKIE_2D)
// A light's cookie (HDRP's mask on its intensity by direction) into its 64x64 tile of the cookie atlas: a point light's
// cubemap as an octahedral map over the light's own directions (x right, y up, z forward, as HDRP samples it), a spot's
// texture as it is. c1.y: which channel holds the mask (0 its colour, 1 alpha, 2 red). 2x2 samples a texel.
#if defined(TGPT_COOKIE_CUBE)
TextureCube<float4> source : register(t0);
#else
Texture2D<float4> source : register(t0);
#endif
SamplerState clampSampler : register(s0);
RWTexture2D<unorm float4> target : register(u0);
float3 oct_decode(float2 e) {
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= 64)) return;
    float3 sum = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        float2 uv = (float2(p) + float2(0.25 + 0.5 * (i & 1), 0.25 + 0.5 * (i >> 1))) / 64.0;
#if defined(TGPT_COOKIE_CUBE)
        float4 c = source.SampleLevel(clampSampler, oct_decode(uv * 2.0 - 1.0), c1.x);
#else
        float4 c = source.SampleLevel(clampSampler, uv, c1.x);
#endif
        sum += c1.y > 1.5 ? c.rrr : c1.y > 0.5 ? c.aaa : c.rgb;
    }
    target[c0.xy + p] = float4(saturate(sum * 0.25), 1.0);
}
#elif defined(TGPT_ALPHA)
// An alpha-tested material's texture into its 256x256 alpha tile (R8), the alpha test's own resolution: the
// 64x64 color tile averaged straw, rope nets and leaves into opaque blocks. Each texel holds its coverage: how many of
// 4x4 samples pass the material's cutoff (c1.y). Averaged alpha thinned strands and a rug's fringe threads below the
// cutoff (they came and went as the shading points moved); coverage keeps them as the texture has them.
Texture2D<float4> source : register(t0);
SamplerState wrapSampler : register(s0);
RWTexture2D<unorm float> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= 256)) return;
    float covered = 0.0;
    [unroll] for (int i = 0; i < 16; ++i) {
        float2 uv = (float2(p) + float2(0.125 + 0.25 * (i & 3), 0.125 + 0.25 * (i >> 2))) / 256.0;
        covered += source.SampleLevel(wrapSampler, uv, c1.x).a >= c1.y ? 1.0 : 0.0;
    }
    target[c0.xy + p] = covered / 16.0;
}
#elif defined(TGPT_COMPOSITE) || defined(TGPT_COMPOSITE_RASTER)
// The previous frame's traced light applied to this frame: each pixel finds its previous position
// through HDRP's motion vectors and blends the four texels around it bilinearly, each only if its
// surface was this one (its distance from the previous camera matches); with none matching, the best
// of the eight neighbours of the nearest texel. The light is multiplied by this frame's diffuse color.
// Returns (rgb, a): the pixel becomes color * a + rgb.
cbuffer Composite : register(b1) {
    float4x4 invViewProj;   // this frame's, camera-relative (Unity's column-major memory order)
    float4 cameraDelta;     // xyz: this camera minus the previous one; w: the frame index (a random seed)
    uint4 frameSize;        // width, height, 1 for the nearest texel (Ray Reconstruction), debug view
    uint4 extras;           // x: 1 writes Ray Reconstruction's hit distance and color before transparency (u1, u2)
};
Texture2D<float4> albedo : register(t0);   // this frame: sRGB diffuse color, a: deferred
Texture2D<float4> emission : register(t1); // this frame: GBuffer3, pre-exposed
Texture2D<float> depth : register(t2);     // this frame
Texture2D<float2> motion : register(t3);   // this frame
Texture2D<float4> tracedA : register(t4);  // previous frame: diffuse light, distance
Texture2D<float4> tracedB : register(t5);  // previous frame: specular light
Texture2D<float> exposureNow : register(t6);    // HDRP's exposure this frame, which its color is pre-exposed with
Texture2D<float> exposureTraced : register(t7); // the one the traced light was pre-exposed with, a frame earlier
Texture2D<float> tracedC : register(t8);  // previous frame: the traced reflections' hit distance
Texture2D<float4> normalsIn : register(t9); // this frame: HDRP's normal buffer
static float g_hitDistance = 65504.0;     // the hit distance of the texels composite() takes (a miss: none)
static bool g_glass = false;              // and whether they are see-through glass (B's alpha -1: pt_compose)
float3 decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((abs(c) + 0.055) / 1.055, 2.4); }
// A traced texel's kind from B's alpha (pt_compose): 0 see-through glass (-1), 1 deferred (0), 2 forward (1), 3 a share of
// HDRP's shading (2).
int texel_kind(float k) { return k < -0.5 ? 0 : k < 0.5 ? 1 : k < 1.5 ? 2 : 3; }
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
    if (!(d > 0.0)) return float4(0, 0, 0, 1);
    // A surface HDRP draws forward (hair, some props) takes traced light only from texels where the tracer found it along
    // the camera ray (B's alpha: light already multiplied by its color); a deferred one only from the others. One HDRP shades
    // itself (B's alpha 2) keeps HDRP's color, times the share of its light the traced shadows leave (A).
    const bool forward = a.a < 0.5;
    float4 w = mul(invViewProj, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0));
    float dist = length(w.xyz / w.w + cameraDelta.xyz);
    float tolerance = 0.03 * dist + 0.05;
    float3 diffuse = 0.0, specular = 0.0, shadeAdd = 0.0; float weights = 0.0, shade = 0.0, shadeWeights = 0.0;
    float2 texel = prevUv * size - 0.5;
    int2 corner = int2(floor(texel));
    float2 f = texel - float2(corner);
    // The texels around the position of one kind only (B's alpha: -1 see-through glass, 0 a deferred surface, 1 a forward one
    // the tracer lit, its light already times its color, 2 a share of HDRP's own shading), the kind holding the most of the
    // weight among those the surface takes (a deferred one glass or deferred, a forward one the others). Mixed, glass's light
    // (all specular) was filtered with the wall's beside it under a guide of one or the other, and a pixel HDRP shades itself
    // took a forward neighbour's traced light in place of HDRP's whenever one such texel was among the four.
    float kinds[4] = { 0.0, 0.0, 0.0, 0.0 };
    [unroll] for (int i = 0; i < 4 && !nearest; ++i) {
        int2 o = int2(i & 1, i >> 1), c = corner + o;
        if (any(c < 0) || any(c >= int2(frameSize.xy))) continue;
        float stored = tracedA.Load(int3(c, 0)).a;
        float wgt = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
        const int kind = texel_kind(tracedB.Load(int3(c, 0)).a);
        if (!(stored > 0.0) || abs(stored - dist) > tolerance || wgt <= 0.0 || (kind >= 2) != forward) continue;
        kinds[kind] += wgt;
    }
    const int want = forward ? (kinds[3] > kinds[2] ? 3 : 2) : (kinds[0] > kinds[1] ? 0 : 1);
    float heaviest = 0.0;
    [unroll] for (int j = 0; j < 4 && !nearest; ++j) {
        int2 o = int2(j & 1, j >> 1), c = corner + o;
        if (any(c < 0) || any(c >= int2(frameSize.xy))) continue;
        float4 t = tracedA.Load(int3(c, 0));
        float wgt = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
        if (!(t.a > 0.0) || abs(t.a - dist) > tolerance || wgt <= 0.0 || texel_kind(tracedB.Load(int3(c, 0)).a) != want) continue;
        if (want == 3) { shade += t.r * wgt; shadeAdd += tracedB.Load(int3(c, 0)).rgb * wgt; shadeWeights += wgt; continue; }
        diffuse += t.rgb * wgt; specular += tracedB.Load(int3(c, 0)).rgb * wgt; weights += wgt;
        if (wgt > heaviest) { heaviest = wgt; g_hitDistance = tracedC.Load(int3(c, 0)); g_glass = want == 0; }
    }
    // HDRP's own shading of it, times the share its traced shadows leave, and the traced bounced light it gains (pt_compose),
    // pre-exposed a frame earlier.
    float en0 = exposureNow.Load(int3(0, 0, 0)), et0 = exposureTraced.Load(int3(0, 0, 0));
    const float shareRatio = (en0 > 0.0 && et0 > 0.0 && en0 < 1e20 && et0 < 1e20) ? en0 / et0 : 1.0;
    if (weights <= 1e-3 && shadeWeights > 1e-3) return float4(max(shadeAdd / shadeWeights * shareRatio, 0.0), saturate(shade / shadeWeights));
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
            if (stored > 0.0 && error < bestError && (tracedB.Load(int3(c, 0)).a > 0.5) == forward) { bestError = error; best = c; }
        }
        bestError = tolerance * 1.5;
        [unroll] for (int ring = 2; ring <= 4 && best.x < 0; ring *= 2) {
            [unroll] for (int i = 1; i < 9; ++i) {
                int2 c = q + offsets[i] * ring;
                if (any(c < 0) || any(c >= int2(frameSize.xy))) continue;
                float stored = tracedA.Load(int3(c, 0)).a;
                float error = abs(stored - dist);
                if (stored > 0.0 && error < bestError && (tracedB.Load(int3(c, 0)).a > 0.5) == forward) { bestError = error; best = c; }
            }
        }
        // Still nothing (a disocclusion, or the band a turning camera just brought into view): the light of a traced texel
        // nearby, picked at random for each pixel and frame among eight within about 24 pixels (a disc kept on screen),
        // the closest in distance from the camera. One texel for a whole band (the nearest to a position off screen, the
        // same for every pixel of a column) drew streaks along the edge a quick turn revealed, which Ray Reconstruction,
        // given single noisy texels, kept; a random pick is noise of the kind it removes. HDRP's own lighting, which was
        // left there before, draws the fill lights the tracer leaves out (new parts of the view went from bright to dark).
        if (best.x < 0 && !forward) {
            const float radius = max(8.0, 24.0 * size.x / 1920.0);
            const int2 margin = int2(min(radius, size * 0.5 - 1.0));
            int2 c = clamp(q, margin, int2(frameSize.xy) - 1 - margin);
            uint h = (p.x * 1973u + p.y * 9277u + asuint(cameraDelta.w) * 26699u) | 1u;
            float closest = 1e30;
            [unroll] for (int k = 0; k < 8; ++k) {
                h ^= h << 13; h ^= h >> 17; h ^= h << 5;
                float a = float(h & 0xFFFFu) * (6.28318531 / 65536.0), r = radius * sqrt(float(h >> 16) / 65536.0);
                int2 n = c + int2(round(float2(cos(a), sin(a)) * r));
                if (any(n < 0) || any(n >= int2(frameSize.xy))) continue;
                float stored = tracedA.Load(int3(n, 0)).a;
                float error = abs(stored - dist);
                // A forward texel's light is already times its own color, and one HDRP shades itself holds none.
                if (tracedB.Load(int3(n, 0)).a > 0.5) continue;
                if (stored > 0.0 && error < closest) { closest = error; best = n; }
            }
            if (best.x < 0) return float4(0, 0, 0, 1);
        }
        if (best.x < 0) return float4(0, 0, 0, 1); // a surface drawn forward the tracer has no light for: HDRP's shading stays
        if (tracedB.Load(int3(best, 0)).a > 1.5) return float4(max(tracedB.Load(int3(best, 0)).rgb * shareRatio, 0.0), saturate(tracedA.Load(int3(best, 0)).r));
        diffuse = tracedA.Load(int3(best, 0)).rgb; specular = tracedB.Load(int3(best, 0)).rgb;
        g_hitDistance = tracedC.Load(int3(best, 0)); g_glass = tracedB.Load(int3(best, 0)).a < -0.5;
    }
    float3 base = forward ? 1.0 : decode(a.rgb);
    // The traced light was pre-exposed with the previous frame's exposure: into this frame's, as auto exposure adapts.
    float en = exposureNow.Load(int3(0, 0, 0)), et = exposureTraced.Load(int3(0, 0, 0));
    float ratio = (en > 0.0 && et > 0.0 && en < 1e20 && et < 1e20) ? en / et : 1.0;
    diffuse *= ratio; specular *= ratio;
    float3 e = forward ? 0.0 : emission.Load(int3(p, 0)).rgb; // GBuffer3 holds nothing of a surface drawn forward
    // HDRP's tag for "no emission, ambient occlusion in green" (Lit.hlsl AO_IN_GBUFFER3_TAG).
    if (abs(e.r - 2048.0) < 1.0 && abs(e.b - 1024.0) < 1.0) e = 0.0;
    return float4(max(e + base * diffuse + specular, 0.0), 0.0);
}
#if defined(TGPT_COMPOSITE)
RWTexture2D<float4> target : register(u0);
RWTexture2D<float> hitOut : register(u1);     // Ray Reconstruction's specular hit distance (extras.x)
RWTexture2D<float4> beforeOut : register(u2); // and its color before transparency: the pixel as the composite leaves it
// And its guides (the guides pass wrote them from the GBuffer), corrected where the light is see-through glass's.
RWTexture2D<unorm float4> diffuseGuide : register(u3);
RWTexture2D<unorm float4> specularGuide : register(u4);
RWTexture2D<float4> normalGuide : register(u5);
// See-through glass's light is all specular (pt_specular refracts what lies behind it into the reflections), so Ray
// Reconstruction takes it as the near-mirror the tracer made of it (pt_prepare: roughness 0.03, a dielectric's reflectance):
// no diffuse albedo, and a specular albedo that covers the light the glass lets through (1 - F of its color, as pt_specular
// weights what lies behind) as well as what it reflects. Its hit distance is already the distance to what is seen behind.
void glass_guides(uint2 p) {
    float2 uv = (float2(p) + 0.5) / float2(frameSize.xy);
    float4 w = mul(invViewProj, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth.Load(int3(p, 0)), 1.0));
    float3 v = -normalize(w.xyz / w.w);
    float3 n = decode_normal(normalsIn.Load(int3(p, 0)));
    float nv = saturate(dot(n, v));
    float fresnel = 0.04 + 0.96 * pow(1.0 - nv, 5.0);
    float3 spec = saturate(env_brdf_approx2(0.04, 0.03 * 0.03, nv) + (1.0 - fresnel) * decode(albedo.Load(int3(p, 0)).rgb));
    const float diffuse = dot(spec, 1.0 / 3.0) < 0.05 ? 0.05 : 0.0; // never both near 0, as the guides pass keeps them
    diffuseGuide[p] = float4(diffuse, diffuse, diffuse, 1.0);
    specularGuide[p] = float4(spec, 1.0);
    normalGuide[p] = float4(n, 0.03);
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= frameSize.xy)) return;
    float4 o = composite(p);
    float4 c = target[p];
    if (!(o.a == 1.0 && all(o.rgb == 0.0))) { c.rgb = c.rgb * o.a + o.rgb; target[p] = c; }
    if (extras.x != 0) {
        hitOut[p] = g_hitDistance; beforeOut[p] = float4(c.rgb, 1.0);
        if (g_glass) glass_guides(p);
    }
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
// linear roughness (HDRP's perceptual roughness, whose square is GGX's alpha) in alpha. Surfaces HDRP draws forward
// keep their normals and get a gray albedo. Where a pixel emits light (GBuffer3) or is sky, the diffuse guide leans
// towards that light's color, as NVIDIA's RTXPT mixes its stable radiance in ("stable radiance comes from emitters
// including sky; it is good to have it as guidance"): the square root of its Reinhard-mapped, pre-exposed color.
cbuffer Composite : register(b1) {
    float4x4 invViewProj;   // this frame's, camera-relative
    float4 cameraDelta;     // w: Reflection Boost
    uint4 frameSize;        // width, height
    uint4 extras;           // y: the diffusion profiles that transmit as thick objects (TGPT_Frame::thickProfiles)
};
Texture2D<float4> albedo : register(t0);    // sRGB diffuse color, a: deferred
Texture2D<float4> normals : register(t1);   // HDRP's normal buffer: octahedral normal 12:12 in RGB, roughness in A
Texture2D<float4> specular : register(t2);  // GBuffer2: specular color (fast sRGB), feature in A's low bits
Texture2D<float> depth : register(t3);
Texture2D<float4> emissionIn : register(t4); // GBuffer3: emission, pre-exposed (HDRP's tag where there is none)
Texture2D<float4> skyIn : register(t5);      // the sky rays see (latitude-longitude, HDRP's units)
Texture2D<float> exposureIn : register(t6);  // HDRP's exposure, which its color is pre-exposed with
RWTexture2D<unorm float4> diffuseOut : register(u0);
RWTexture2D<unorm float4> specularOut : register(u1);
RWTexture2D<float4> normalOut : register(u2);
float3 decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((abs(c) + 0.055) / 1.055, 2.4); }
float3 fast_srgb_to_linear(float3 c) { return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878); }
float3 stable_mix(float3 albedo, float3 radiance) {
    radiance = max(radiance, 0.0);
    float3 stable = sqrt(radiance / (1.0 + max(radiance.r, max(radiance.g, radiance.b))));
    float s = dot(stable, 1.0 / 3.0);
    if (!(s > 0.0)) return albedo;
    return lerp(albedo, lerp(stable, 0.5, 0.2), saturate(s / (dot(albedo, 1.0 / 3.0) + sqrt(s) + 1e-7)));
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= frameSize.xy)) return;
    float d = depth.Load(int3(p, 0));
    float e0 = exposureIn.Load(int3(0, 0, 0));
    float exposure = (e0 > 0.0 && e0 < 1e20) ? e0 : 1.0;
    if (!(d > 0.0)) {
        // The sky: its color along the view ray, as HDRP's color holds it (pre-exposed).
        float2 suv = (float2(p) + 0.5) / float2(frameSize.xy);
        float4 far = mul(invViewProj, float4(suv.x * 2.0 - 1.0, 1.0 - suv.y * 2.0, 1e-7, 1.0));
        float3 dir = normalize(far.xyz / far.w);
        uint sw, sh; skyIn.GetDimensions(sw, sh);
        float u = atan2(dir.x, dir.z) * (0.5 / 3.14159265) + 0.5, v = acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
        float3 sky = skyIn.Load(int3(min(uint2(uint(u * sw), uint(v * sh)), uint2(sw - 1, sh - 1)), 0)).rgb * exposure;
        float3 skyGuide = stable_mix(0.0, sky);
        if (dot(skyGuide, 1.0 / 3.0) < 0.05) skyGuide += 0.05;
        diffuseOut[p] = float4(skyGuide, 1.0); specularOut[p] = 0.0; normalOut[p] = 0.0; return;
    }
    float4 a = albedo.Load(int3(p, 0));
    float4 nb = normals.Load(int3(p, 0));
    float3 n = decode_normal(nb);
    bool deferred = a.a > 0.5;
    const float4 gb2 = specular.Load(int3(p, 0));
    // Reflection Boost, as the tracer applies it (pt_prepare): on what the GBuffer describes, but not on skin (HDRP's
    // transmission through a thick profile, as pt_common's solid_transmission reads it). Hair and eyes keep theirs.
    const uint features = uint(gb2.a * 255.0 + 0.5);
    const bool skin = (features & 7u) >= 2u && (features & 7u) <= 3u && (features & 8u) == 0u &&
                      ((extras.y >> (uint(gb2.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u)) & 1u) != 0u;
    const float boost = deferred && !skin ? cameraDelta.w : 0.0;
    float roughness = lerp(saturate(nb.a), 0.05, boost);
    float2 uv = (float2(p) + 0.5) / float2(frameSize.xy);
    float4 w = mul(invViewProj, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0));
    float3 v = -normalize(w.xyz / w.w);
    float3 f0 = 0.04;
    if (deferred) {
        if ((features & 7u) == 0u) f0 = fast_srgb_to_linear(gb2.rgb);
        f0 = lerp(f0, 0.6, 0.7 * boost);
    }
    float3 diffuse = deferred ? decode(a.rgb) : 0.5;
    if (deferred) {
        float3 e = emissionIn.Load(int3(p, 0)).rgb;
        if (!(abs(e.r - 2048.0) < 1.0 && abs(e.b - 1024.0) < 1.0)) diffuse = stable_mix(diffuse, e);
    }
    float3 spec = saturate(env_brdf_approx2(f0, roughness * roughness, dot(n, v)));
    // Never both near 0 (a black cloth, soot): Ray Reconstruction divides the light by them, as NVIDIA's RTXPT keeps
    // their average above 0.05.
    if (dot(diffuse + spec, 1.0 / 3.0) < 0.05) diffuse += 0.05;
    diffuseOut[p] = float4(diffuse, 1.0);
    specularOut[p] = float4(spec, 1.0);
    normalOut[p] = float4(n, roughness);
}
#elif defined(TGPT_SHADOW_DETAIL) || defined(TGPT_SHADOW_DETAIL_RASTER)
// Shadow Detail: HDRP's finished HDR image (denoised, upscaled, pre-exposed: before its post-processing and tonemapper)
// with its dark parts lifted by a bounded curve below a mid tone, log2 0.12, where the traced light's exposure aims its
// median; everything brighter as it is. Below it the lift grows smoothly (quadratically for the first half stop) and then
// by 0.4 s EV per stop, so the log-luminance slope there is 1 - 0.4 s, up to 3 s EV at most; one multiplier for all three
// channels (their ratios kept), black kept black. After every denoiser: a curve before Ray Reconstruction biased it (the
// average of a lifted noisy pixel is darker than the lifted average). c0.xy: size; c1.x: s, 0 to 1.
float3 shadow_detail(float3 c) {
    float y = dot(c, float3(0.2126, 0.7152, 0.0722));
    if (!(y > 0.0) || !(c1.x > 0.0)) return c;
    float d = max(0.0, -3.06 - log2(max(y, 1e-12)));
    float h = d < 0.5 ? d * d : d - 0.25;
    return c * exp2(c1.x * min(3.0, 0.4 * h));
}
#if defined(TGPT_SHADOW_DETAIL)
RWTexture2D<float4> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= c0.xy)) return;
    float4 c = target[p];
    target[p] = float4(shadow_detail(c.rgb), c.a);
}
#else
Texture2D<float4> source : register(t0); // a copy of the image
float4 vsMain(uint id : SV_VertexID) : SV_Position { float2 uv = float2((id << 1) & 2, id & 2); return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }
float4 psMain(float4 position : SV_Position) : SV_Target { float4 c = source.Load(int3(position.xy, 0)); return float4(shadow_detail(c.rgb), c.a); }
#endif
#endif
