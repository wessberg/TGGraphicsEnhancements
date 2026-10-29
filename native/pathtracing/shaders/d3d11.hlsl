// SPDX-License-Identifier: MIT
// TG Path Tracing's work on the game's D3D11 device: HDRP's textures into the textures shared with
// the D3D12 tracer, materials' textures into the shared atlas, the sky into a latitude-longitude map,
// and the tracer's result into HDRP's color buffer. Compiled at run time, one entry point per macro.
cbuffer Constants : register(b0) { uint4 c0; float4 c1; };

#if defined(TGPT_COMPOSITE) || defined(TGPT_GUIDES)
// Ray Reconstruction's guides, which the guides pass writes and the composite corrects on see-through glass; HDRP's normals
// (its normal buffer's and its water GBuffer's, PackNormalOctQuadEncode into 8:8:8).
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
    // c0.z: the first row written (the fog's sky: a tile per mip, stacked; 0 for the sky itself).
    target[p + uint2(0, c0.z)] = float4(max(c.rgb, 0.0), 1.0);
}
#elif defined(TGPT_ATLAS)
// One material texture into its tile (64x64, or c1.y texels a side: a detail tile's 512): sampled at the mip nearest twice
// the tile's size, 2x2 each texel.
Texture2D<float4> source : register(t0);
SamplerState wrapSampler : register(s0);
RWTexture2D<unorm float4> target : register(u0);
float3 encode(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055; }
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    const uint size = c1.y > 0.0 ? uint(c1.y) : 64u;
    if (any(p >= size)) return;
    float4 sum = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        float2 uv = (float2(p) + float2(0.25 + 0.5 * (i & 1), 0.25 + 0.5 * (i >> 1))) / float(size);
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
#elif defined(TGPT_EYE)
// An eye's color into its tile (TGPT_RegisterEyeColor; 64x64, or c1.y texels a side: a detail tile's 512), texel by texel
// and 2x2 each, as the game's eye shader computes it (read from its compiled code): the sclera map tiled about the centre
// times its tint; over it by the iris map's alpha, the iris map, magnified toward the centre by the pupil, times its tint,
// through the shader's overlay with its limbal ring. c0.z: which maps it has (bit 0 sclera, 1 iris). Its fake reflection
// is left out.
Texture2D<float4> scleraMap : register(t0);
Texture2D<float4> irisMap : register(t1);
SamplerState clampSampler : register(s0); // clamped: the shader's one sampler for both maps (a repeating iris would tile the sclera)
RWTexture2D<unorm float4> target : register(u0);
cbuffer Eye : register(b1) {
    float4 scleraTint, irisTint;
    float4 params;          // sclera tiling, iris tiling, pupil size, limbal ring size
    float4 params2;         // limbal ring intensity, iris emission intensity
};
float3 encode(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055; }
// The mip whose texels the tile's 2x2 samples cover, for a map tiled this many times across the eye.
float lod_for(Texture2D<float4> t, float tiling, float size) {
    uint w, h, levels; t.GetDimensions(0, w, h, levels);
    return clamp(log2(max(abs(tiling) * float(max(w, h)) / (2.0 * size), 1e-6)), 0.0, max(float(levels) - 1.0, 0.0));
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    const uint size = c1.y > 0.0 ? uint(c1.y) : 64u;
    if (any(p >= size)) return;
    const float scleraLod = (c0.z & 1u) != 0 ? lod_for(scleraMap, params.x, float(size)) : 0.0;
    const float irisLod = (c0.z & 2u) != 0 ? lod_for(irisMap, params.y, float(size)) : 0.0;
    float3 sum = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        const float2 uv = (float2(p) + float2(0.25 + 0.5 * (i & 1), 0.25 + 0.5 * (i >> 1))) / float(size);
        const float2 d = uv - 0.5;
        const float3 sclera = ((c0.z & 1u) != 0 ? scleraMap.SampleLevel(clampSampler, d * params.x + 0.5, scleraLod).rgb : 1.0) * scleraTint.rgb;
        // The pupil: the iris magnified toward the centre (within an eighth of the eye's coordinates).
        const float w = 1.0 - min(8.0 * length(d), 1.0);
        const float2 irisUv = lerp(d * params.y + 0.5, d * (params.y + 8.0 * params.z - 6.0) + 0.5, w);
        const float4 iris = (c0.z & 2u) != 0 ? irisMap.SampleLevel(clampSampler, irisUv, irisLod) : float4(1.0, 1.0, 1.0, 0.0);
        const float3 tint = lerp(irisTint.rgb, float3(0.093520, 0.118404, 0.169881), saturate((params2.y - 1.0) / 9.0));
        float3 b = lerp(sclera, iris.rgb * tint, iris.a);
        // The limbal ring: the iris's outer params.w darkens toward its edge; 1 elsewhere, through the same overlay.
        const float r = length(2.0 * (irisUv - 0.5));
        const float ring = r > 1.0 - params.w ? pow(0.1 + 0.9 * max(1.0 - r, 0.0) / max(params.w, 1e-4), params2.x) : 1.0;
        const float3 over = b <= 0.5 ? 2.0 * b * ring : 1.0 - 2.0 * (1.0 - b) * (1.0 - ring);
        sum += lerp(b, over, iris.a);
    }
    target[c0.xy + p] = float4(encode(saturate(sum * 0.25)), 1.0);
}
#elif defined(TGPT_HAIR)
// RealHair's color into its tile (TGPT_RegisterHairColor; 64x64, or c1.y texels a side: a detail tile's 512), texel by texel
// and 2x2 each, as the game's shader computes it (read from its compiled code): its diffuse map (at the Shader Graph's tiling)
// toward the root-to-end gradient along the root map, two highlight bands by the ID map, times its strength, the blend map and
// the mask map's occlusion, and its color. Maps at the mip nearest twice the tile's size; one c0.z leaves out (bit 0 diffuse,
// 1 root, 2 ID, 3 mask, 4 blend) reads as white.
Texture2D<float4> diffuseMap : register(t0);
Texture2D<float4> rootMap : register(t1);
Texture2D<float4> idMap : register(t2);
Texture2D<float4> maskMap : register(t3);
Texture2D<float4> blendMap : register(t4);
SamplerState wrapSampler : register(s0);
RWTexture2D<unorm float4> target : register(u0);
cbuffer Hair : register(b1) {
    float4 tilingOffset;                                        // the diffuse map's tiling (xy) and offset (zw)
    float4 diffuseColor, rootColor, endColor, highlightAColor, highlightBColor;
    float4 highlightA, highlightB;                              // a band's start, peak and end on the ID map; w: its fade toward the end
    float4 strengths0;                                          // base color, global, root color, end color strength
    float4 strengths1;                                          // root map inverted; highlight blend; highlight A, B strength
    float4 strengths2;                                          // highlight A, B overlap inverted; diffuse strength; blend strength
    float4 strengths3;                                          // occlusion strength, occlude all
};
float3 encode(float3 c) { return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(abs(c), 1.0 / 2.4) - 0.055; }
float tile_size() { return c1.y > 0.0 ? c1.y : 64.0; }
float lod_of(Texture2D<float4> t) {
    uint w, h, levels; t.GetDimensions(0, w, h, levels);
    return clamp(log2(max(float(max(w, h)), 1.0) / (2.0 * tile_size())), 0.0, max(float(levels) - 1.0, 0.0));
}
float4 read_map(Texture2D<float4> t, uint bit, float2 uv) { return (c0.z & bit) != 0 ? t.SampleLevel(wrapSampler, uv, lod_of(t)) : 1.0; }
// A highlight band: rising from d.x to its peak at d.y, falling to d.z (smoothsteps, as the shader's).
float band(float id, float4 d) {
    const float rise = saturate((id - d.x) / max(d.y - d.x, 1e-5)), fall = saturate((id - d.y) / max(d.z - d.y, 1e-5));
    return id >= d.y ? 1.0 - fall * fall * (3.0 - 2.0 * fall) : rise * rise * (3.0 - 2.0 * rise);
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    const uint size = uint(tile_size());
    if (any(p >= size)) return;
    float4 sum = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        const float2 uv = (float2(p) + float2(0.25 + 0.5 * (i & 1), 0.25 + 0.5 * (i >> 1))) / float(size);
        const float4 diffuse = read_map(diffuseMap, 1u, uv * tilingOffset.xy + tilingOffset.zw);
        const float root = read_map(rootMap, 2u, uv).g, id = read_map(idMap, 4u, uv).g;
        const float along = lerp(root, 1.0 - root, strengths1.x);
        float3 c = lerp(1.0, diffuse.rgb, strengths0.x);
        c = lerp(c, lerp(rootColor.rgb, endColor.rgb, along), lerp(strengths0.z, strengths0.w, along) * strengths0.y);
        const float a = saturate(band(id, highlightA) * strengths1.z * (1.0 - highlightA.w * lerp(root, 1.0 - root, strengths2.x)) * strengths1.y);
        c = lerp(c, highlightAColor.rgb, a);
        const float b = saturate(band(id, highlightB) * strengths1.w * (1.0 - highlightB.w * lerp(root, 1.0 - root, strengths2.y)) * strengths1.y);
        c = lerp(c, highlightBColor.rgb, b);
        c *= strengths2.z;
        c = lerp(c, c * read_map(blendMap, 16u, uv).g, strengths2.w);
        c = lerp(c, c * lerp(1.0, read_map(maskMap, 8u, uv).g, strengths3.x), strengths3.y);
        sum += float4(c * diffuseColor.rgb, diffuse.a);
    }
    sum *= 0.25;
    target[c0.xy + p] = float4(encode(saturate(sum.rgb)), saturate(sum.a));
}
#elif defined(TGPT_HAIR_LOBES)
// RealHair's strands into a 64x64 tile (TGPT_RegisterHairLobes), texel by texel and 2x2 each, as the game's shader reads them
// for its highlights, at its mesh's own coordinates: red and green the flow map's direction in the mesh's tangent frame (red
// times alpha, and green, each to -1..1, green flipped as far as c1.z as the shader does it; stored as 0.5 + 0.5 x), blue its
// mask map's alpha to the power c1.x (how far the primary lobe is from its lowest smoothness to its highest), alpha its
// specular map's luminance. c0.z: the maps it has (bit 0 flow, 1 mask, 2 specular); a missing flow runs along -v (root to tip,
// as the game's hair cards lie), a missing mask is full, a missing specular map white. Maps at the mip nearest twice the tile's
// size. Stored as they are: read back texel by texel (pt_common hair_lobe), not as colors.
Texture2D<float4> flowMap : register(t0);
Texture2D<float4> maskMap : register(t1);
Texture2D<float4> specularMap : register(t2);
SamplerState wrapSampler : register(s0);
RWTexture2D<unorm float4> target : register(u0);
float lod_of(Texture2D<float4> t) {
    uint w, h, levels; t.GetDimensions(0, w, h, levels);
    return clamp(log2(max(float(max(w, h)), 1.0) / 128.0), 0.0, max(float(levels) - 1.0, 0.0));
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= 64)) return;
    float4 sum = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        const float2 uv = (float2(p) + float2(0.25 + 0.5 * (i & 1), 0.25 + 0.5 * (i >> 1))) / 64.0;
        float2 flow = float2(0.0, -1.0);
        if ((c0.z & 1u) != 0) {
            const float4 f = flowMap.SampleLevel(wrapSampler, uv, lod_of(flowMap));
            flow = float2(f.r * f.a, f.g) * 2.0 - 1.0;
            flow.y += c1.z * (1.0 - 2.0 * flow.y);
        }
        const float mask = (c0.z & 2u) != 0 ? maskMap.SampleLevel(wrapSampler, uv, lod_of(maskMap)).a : 1.0;
        const float3 spec = (c0.z & 4u) != 0 ? specularMap.SampleLevel(wrapSampler, uv, lod_of(specularMap)).rgb : 1.0;
        sum += float4(flow * 0.5 + 0.5, pow(saturate(mask), max(c1.x, 0.0)), dot(saturate(spec), float3(0.2126, 0.7152, 0.0722)));
    }
    target[c0.xy + p] = saturate(sum * 0.25);
}
#elif defined(TGPT_STRAND_ALPHA)
// A hair material's diffuse map's alpha into its block of the strand atlas (tracer.cpp bake_hair), texel for texel at the
// map's mip whose size is the block's (c1.x): the strands as sharp as the game's texture has them. c0.xy: the block's corner,
// c0.z its size.
Texture2D<float4> source : register(t0);
SamplerState wrapSampler : register(s0);
RWTexture2D<unorm float> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= c0.z)) return;
    target[c0.xy + p] = source.SampleLevel(wrapSampler, (float2(p) + 0.5) / float(c0.z), c1.x).a;
}
#elif defined(TGPT_STRAND_MIP)
// A block of the strand atlas one level down from the level above it: each texel the mean of the four it covers there (the
// share of it the strands cover). c0.xy: the block's corner at this level, c0.z its size there.
Texture2D<float> above : register(t0);
RWTexture2D<unorm float> target : register(u0);
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= c0.z)) return;
    const uint2 q = (c0.xy + p) * 2u;
    target[c0.xy + p] = 0.25 * (above.Load(int3(q, 0)) + above.Load(int3(q + uint2(1, 0), 0)) + above.Load(int3(q + uint2(0, 1), 0)) +
                                above.Load(int3(q + uint2(1, 1), 0)));
}
#elif defined(TGPT_SKIN)
// Characters' skinned positions from the game's skinning output into this frame's shared texture, a vertex a
// texel and 4096 a row; one dispatch a character. c0: first vertex there, count, first texel, stride in bytes.
// ON TRIAL (PT-92; SKIN_NORMAL: the 32-bit word after the position's three that holds it, 0 the first): the game's own
// skinned normal of the vertex goes with its position, in the texel's alpha, as the game packed it (TGPT_SetSkinned) with
// bit 30 set: no valid one has that bit, and with it every one is an ordinary float (pt_skin.hlsl SKIN_NORMAL_MARK takes
// it off again). Without SKIN_NORMAL the alpha is 1 as before, and nothing reads it.
#if defined(SKIN_RAW)
ByteAddressBuffer source : register(t0);
float3 load_position(uint i) { return asfloat(source.Load3(i * c0.w)); }
#if defined(SKIN_NORMAL)
uint load_normal(uint i) { return source.Load(i * c0.w + 12u + 4u * SKIN_NORMAL); }
#endif
#else
struct SkinVertex {
    float3 position;
#if SKIN_REST > 0
    uint rest[SKIN_REST];
#endif
};
StructuredBuffer<SkinVertex> source : register(t0);
float3 load_position(uint i) { return source[i].position; }
#if defined(SKIN_NORMAL)
uint load_normal(uint i) { return source[i].rest[SKIN_NORMAL]; }
#endif
#endif
RWTexture2D<float4> target : register(u0);
[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= c0.y) return;
    uint t = c0.z + id.x;
#if defined(SKIN_NORMAL)
    target[uint2(t % 4096u, t / 4096u)] = float4(load_position(c0.x + id.x), asfloat(load_normal(c0.x + id.x) | 0x40000000u));
#else
    target[uint2(t % 4096u, t / 4096u)] = float4(load_position(c0.x + id.x), 1.0);
#endif
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
// The frame's own traced light laid into HDRP's color buffer: each pixel takes its own texel of the light the tracer made
// for this very frame (tracer.cpp composite_event waits for it on the GPU). So the picture the upscaler gets was traced
// through the points HDRP drew this frame's depth at, under the sub-pixel shift the upscaler is told, still or moving (PT-84).
// (Until 2026-10-08 this pass showed the light of the frame before: each pixel looked for it through HDRP's motion vectors,
// took the texel nearest that place, or four around it, matched them by HDRP's depth, and where none matched a neighbour's
// or a random one's nearby. The picture was a frame late, and its samples lay under another sub-pixel shift than the one the
// upscaler was told: after it every edge was a stair of render pixels and thin things were rows of dots.)
// Returns (rgb, a): the pixel becomes color * a + rgb.
cbuffer Composite : register(b1) {
    float4x4 guidesMatrix;  // the guides pass's (one buffer serves both: tracer.cpp CompositeConstants); not read here
    float4 guidesVector;    // the guides pass's; not read here
    uint4 frameSize;        // width, height, unused, debug view
    uint4 extras;           // x: 1 writes Ray Reconstruction's hit distance and color before transparency (u1, u2); z: unused (Fog Adaptation's copy until 2026-10-11)
};
Texture2D<float4> albedo : register(t0);   // sRGB diffuse color, a: deferred
Texture2D<float4> emission : register(t1); // GBuffer3, pre-exposed
Texture2D<float> depth : register(t2);
Texture2D<float4> tracedA : register(t3);  // the traced light (pt_compose's output A); a: the surface's distance, 0 where nothing was traced
Texture2D<float4> tracedB : register(t4);  // its specular part; a: what the texel holds (below)
Texture2D<float4> tracedC : register(t5); // RGB specular albedo, A reflections' hit distance
// Ray Reconstruction's guides where the tracer knows the pixel better than the GBuffer (pt_compose traced_guides, pt_primary
// write_guides): color (sRGB) and how much of the pixel they describe; normal (octahedral) and roughness.
// RGB specular reflectance travels in tracedC.
Texture2D<float4> tracedG0 : register(t6);
Texture2D<float4> tracedG1 : register(t7);
static int2 g_guideTexel = int2(-1, -1);   // the pixel's own texel where it took traced light (none: -1)
static float g_hitDistance = 65504.0;     // its reflection's hit distance (a miss: none)
static float3 g_through = -1.0;           // glass in front of the sky: the share of HDRP's sky it lets through
float3 decode(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((abs(c) + 0.055) / 1.055, 2.4); }
// HDRP's GBuffer3 at a pixel as the composite adds it: emission (and what HDRP bakes in), pre-exposed; its tag for "no
// emission, ambient occlusion in green" (Lit.hlsl AO_IN_GBUFFER3_TAG) none.
float3 gbuffer3_emission(uint2 p) {
    float3 e = emission.Load(int3(p, 0)).rgb;
    return abs(e.r - 2048.0) < 1.0 && abs(e.b - 1024.0) < 1.0 ? 0.0 : e;
}
// What a texel holds, by B's alpha (pt_compose):
//   1  the pixel's whole light: the surface the tracer's own camera ray stopped at, its color and emission in it (A + B);
//  -2  glass in front of the sky: A the share of HDRP's sky it lets through, B its own light;
//   0  the route before the tracer's own camera rays (TGPT_OPTION_GBUFFER_SURFACES): a surface of HDRP's G-buffer, A its
//      diffuse light divided by its color, which this frame's G-buffer multiplies back in, with its GBuffer3; -1 that
//      route's see-through glass (all of it in B).
// A's alpha 0: nothing was traced for the pixel (the sky, or a surface the tracer has no light for), and HDRP's own stays.
float4 composite(uint2 p) {
    const float4 ta = tracedA.Load(int3(p, 0));
    if (frameSize.w != 0) {
        // A debug view's colors are in A. The emissive view (TGPT_VIEW_EMISSIVE) adds HDRP's GBuffer3 where it is drawn, as
        // the lit composite adds it to a surface of the G-buffer: where it lights something HDRP's emission is not (skin
        // under a crown of hair, 2026-10-06), the view shows it.
        const float3 hdrpEmission = frameSize.w == 10u && depth.Load(int3(p, 0)) > 0.0 && albedo.Load(int3(p, 0)).a >= 0.5 ? gbuffer3_emission(p) : 0.0;
        return float4(ta.rgb + hdrpEmission, 0.0);
    }
    if (!(ta.a > 0.0)) return float4(0, 0, 0, 1);
    const float4 tb = tracedB.Load(int3(p, 0));
    g_guideTexel = int2(p);
    g_hitDistance = tracedC.Load(int3(p, 0)).a;
    if (tb.a > 0.5) return float4(max(ta.rgb + tb.rgb, 0.0), 0.0);
    if (tb.a < -1.5) { g_through = saturate(ta.rgb); return float4(max(tb.rgb, 0.0), dot(g_through, 1.0 / 3.0)); }
    const float4 a = albedo.Load(int3(p, 0));
    // The route before: only a surface of the G-buffer has a color to multiply back in (one HDRP drew forward keeps HDRP's).
    if (!(depth.Load(int3(p, 0)) > 0.0) || a.a < 0.5) { g_guideTexel = int2(-1, -1); g_hitDistance = 65504.0; return float4(0, 0, 0, 1); }
    return float4(max(gbuffer3_emission(p) + decode(a.rgb) * ta.rgb + tb.rgb, 0.0), 0.0);
}
#if defined(TGPT_COMPOSITE)
RWTexture2D<float4> target : register(u0);
RWTexture2D<float> hitOut : register(u1);     // Ray Reconstruction's specular hit distance (extras.x)
RWTexture2D<float4> beforeOut : register(u2); // and its color before transparency: the pixel as the composite leaves it
// The tracer's first surface supplies the guides below, glass included.
RWTexture2D<unorm float4> diffuseGuide : register(u3);
RWTexture2D<unorm float4> specularGuide : register(u4);
RWTexture2D<float4> normalGuide : register(u5);
float3 guide_normal(float2 e) {
    float3 n = float3(e * 2.0 - 1.0, 0.0);
    n.z = 1.0 - abs(n.x) - abs(n.y);
    const float t = saturate(-n.z);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}
// Where the tracer knows what the pixel shows better than HDRP's GBuffer (its guide outputs, from the texel whose light the
// pixel took: pt_compose traced_guides, pt_primary write_guides), its guides over the guides pass's, by the share of the pixel
// they describe: all of a surface HDRP draws forward that the tracer shades (an eye: its own color and wet finish, not the
// grey albedo and dielectric the guides pass gives what the GBuffer lacks; hair), the cover of the hair a pixel's ray crossed
// in front of a surface of the G-buffer (a brow's color, roughness and specular albedo over the skin's, which the GBuffer
// holds there). Albedos and roughness blend; the normal is the one that covers more of the pixel, unblended (NVIDIA's RR guide
// 3.4.1 and 3.4.3: Ray Reconstruction computes the reflections' motion from it and from the hit distance). Without them Ray
// Reconstruction took the strands' light for noise on the skin and smoothed it.
void traced_guides(uint2 p) {
    if (g_guideTexel.x < 0) return;
    const float4 a = tracedG0.Load(int3(g_guideTexel, 0));
    const float w = saturate(a.a);
    if (!(w > 0.0)) return;
    const float4 b = tracedG1.Load(int3(g_guideTexel, 0));
    const float4 nr = normalGuide[p];
    float3 diffuse = lerp(diffuseGuide[p].rgb, decode(a.rgb), w);
    const float3 spec = lerp(specularGuide[p].rgb, tracedC.Load(int3(g_guideTexel, 0)).rgb, w);
    diffuseGuide[p] = float4(saturate(diffuse), 1.0);
    specularGuide[p] = float4(saturate(spec), 1.0);
    normalGuide[p] = float4(w > 0.5 ? guide_normal(b.rg) : nr.xyz, lerp(nr.a, b.b, w));
}
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= frameSize.xy)) return;
    float4 o = composite(p);
    float4 c = target[p];
    if (g_through.x >= 0.0) { c.rgb = c.rgb * g_through + o.rgb; target[p] = c; } // the sky behind see-through glass
    else if (!(o.a == 1.0 && all(o.rgb == 0.0))) { c.rgb = c.rgb * o.a + o.rgb; target[p] = c; }
    if ((extras.x & 1u) != 0u) {
        hitOut[p] = g_hitDistance;
        if ((extras.x & 2u) != 0u) beforeOut[p] = float4(c.rgb, 1.0);
        traced_guides(p);
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
// keep their normals and get a gray albedo until the traced guides overwrite it. Albedo is material reflectance,
// independent of illumination or emission (DLSS-RR guide 3.4.1). Sky defaults to 0.5 as that section recommends.
cbuffer Composite : register(b1) {
    float4x4 invViewProj;   // this frame's, camera-relative
    float4 cameraDelta;     // w: Reflection Boost
    uint4 frameSize;        // width, height
    uint4 extras;           // y: the diffusion profiles that transmit as thick objects (TGPT_Frame::thickProfiles)
    float4 profileF0[4];    // per diffusion profile, four to a row: its mirror reflectance (TGPT_SetProfileReflectance), 0 none
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
[numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= frameSize.xy)) return;
    float d = depth.Load(int3(p, 0));
    if (!(d > 0.0)) {
        diffuseOut[p] = float4(0.5, 0.5, 0.5, 1.0); specularOut[p] = 0.0; normalOut[p] = 0.0; return;
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
    const bool transmits = (features & 7u) >= 2u && (features & 7u) <= 3u; // grass and leaves too (pt_prepare)
    const float boost = deferred && !skin && !transmits ? cameraDelta.w : 0.0;
    float roughness = saturate(nb.a);
    float2 uv = (float2(p) + 0.5) / float2(frameSize.xy);
    float4 w = mul(invViewProj, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0));
    float3 v = -normalize(w.xyz / w.w);
    float3 f0 = 0.04;
    if (deferred) {
        if ((features & 7u) == 0u) f0 = fast_srgb_to_linear(gb2.rgb);
        // A leaf's or a blade's, its diffusion profile's (pt_prepare): the game's foliage mirrors 0.0023, not 0.04.
        else if (transmits && !skin && (features & 8u) == 0u) {
            const uint profile = uint(gb2.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u;
            const float leafF0 = profileF0[profile >> 2u][profile & 3u];
            if (leafF0 > 0.0) f0 = leafF0;
        }
    }
    {
        // As pt_common's reflection_boost: everything a little glossier, a metal twice that and mirroring more; never rougher
        // (a mirror stays one).
        const float top = max(f0.r, max(f0.g, f0.b));
        const float metal = deferred ? saturate((top - 0.08) / 0.4) : 0.0;
        const float unboosted = roughness;
        roughness = lerp(roughness, 0.05, saturate(boost * (1.0 + metal)));
        roughness = min(roughness, unboosted);
        f0 = lerp(f0, f0 / max(top, 1e-3), 0.7 * boost * metal);
    }
    float3 diffuse = deferred ? decode(a.rgb) : 0.5;
    float3 spec = saturate(env_brdf_approx2(f0, roughness * roughness, dot(n, v)));
    diffuseOut[p] = float4(diffuse, 1.0);
    specularOut[p] = float4(spec, 1.0);
    normalOut[p] = float4(n, roughness);
}
#elif defined(TGPT_DEPTH_MOTION)
// PT-112 (A), on trial: the traced frame's own depth and motion vectors into HDRP's depth buffer and motion vectors
// (tracer.cpp write_depth_motion), from pt_compose's dm and mv: the device depth (reversed-Z, HDRP's projection with the
// frame's sub-pixel shift; 0 where the camera's ray met nothing) and the motion (HDRP's kind).
Texture2D<float> depthIn : register(t0);
Texture2D<float2> motionIn : register(t1);
struct DepthMotionOut { float2 motion : SV_Target0; float depth : SV_Depth; };
float4 vsMain(uint id : SV_VertexID) : SV_Position { float2 uv = float2((id << 1) & 2, id & 2); return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1); }
DepthMotionOut psMain(float4 position : SV_Position) {
    const int3 p = int3(uint2(position.xy), 0);
    DepthMotionOut o;
    o.depth = saturate(depthIn.Load(p));
    o.motion = motionIn.Load(p);
    return o;
}
// HDRP's depth pyramid's first level, in a draw of its own: the pyramid is a larger texture than the depth buffer and the
// motion vectors, and D3D11 binds no targets of different sizes together.
float psPyramid(float4 position : SV_Position) : SV_Target0 { return saturate(depthIn.Load(int3(uint2(position.xy), 0))); }
#endif
