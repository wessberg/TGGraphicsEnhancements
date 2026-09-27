// SPDX-License-Identifier: MIT
// Opacity micromaps (DirectX Raytracing 1.2) for one alpha-tested submesh: a group per triangle writes
// its 4-state micromap of 4^OMM_LEVEL micro-triangles (2 bits each) and its entry in the micromap index
// buffer. The traversal's alpha test reads one atlas texel (point-sampled), so a micro-triangle is
// opaque or transparent only when every texel its UVs can reach agrees; otherwise, or when it reaches
// more texels than are worth reading, it stays unknown and the alpha test decides as before. A triangle
// whose micro-triangles all agree takes one of DXR's special indices instead of its micromap.
//   pass.a: mesh indices (raw SRV), mesh UVs (raw SRV, or none), material, first micromap of the submesh
//   pass.b: micromap data (raw UAV), micromap indices (raw UAV), triangle count, first index of the submesh
#include "pt_common.hlsli"

#define OMM_LEVEL 4u
static const uint OMM_COUNT = 1u << (2u * OMM_LEVEL);         // micro-triangles per triangle
static const uint OMM_BYTES = OMM_COUNT / 4u;                  // 2 bits each
static const uint OMM_TRANSPARENT = 0, OMM_OPAQUE = 1, OMM_UNKNOWN_OPAQUE = 3;
static const int OMM_SPECIAL_FULLY_TRANSPARENT = -1, OMM_SPECIAL_FULLY_OPAQUE = -2;
static const uint OMM_MAX_TEXELS = 64;

// The micromap's order (the DXR spec's figure; Vulkan's VK_EXT_opacity_micromap uses the same curve):
// micro-triangles follow a space-filling curve over the triangle's 2^level x 2^level barycentric grid.
// A curve position's even bits and odd bits give its grid cell's discrete barycentrics through prefix
// XORs; cells alternate between upright and flipped triangles.
uint even_bits(uint x) {
    x &= 0x55555555u;
    x = (x | (x >> 1)) & 0x33333333u;
    x = (x | (x >> 2)) & 0x0f0f0f0fu;
    x = (x | (x >> 4)) & 0x00ff00ffu;
    return (x | (x >> 8)) & 0x0000ffffu;
}
uint prefix_xor(uint x) { x ^= x >> 1; x ^= x >> 2; x ^= x >> 4; return x ^ (x >> 8); }
// The corners of the micro-triangle at a curve position, as barycentrics (the weights of the triangle's
// second and third vertices, as DXR reports a hit's).
void micro_triangle(uint index, out float2 c0, out float2 c1, out float2 c2) {
    uint b0 = even_bits(index), b1 = even_bits(index >> 1);
    uint fx = prefix_xor(b0), fy = prefix_xor(b0 & ~b1);
    uint t = fy ^ b1;
    uint mask = (1u << OMM_LEVEL) - 1u;
    uint u = ((fx & ~t) | (b0 & ~t) | (~b0 & ~fx & t)) & mask;
    uint v = (fy ^ b0) & mask;
    uint w = ((~fx & ~t) | (b0 & ~t) | (~b0 & fx & t)) & mask;
    bool upright = ((u ^ v ^ w) & 1u) != 0;
    if (!upright) { u += 1u; v += 1u; }
    float step = 1.0 / float(1u << OMM_LEVEL);
    float d = upright ? step : -step;
    c0 = float2(u, v) * step; c1 = c0 + float2(d, 0); c2 = c0 + float2(0, d);
}

groupshared uint gs_words[OMM_BYTES / 4u];
groupshared uint gs_seen;   // bit per state found

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
    uint triangleIndex = group.y * 32768u + group.x;
    if (thread < OMM_BYTES / 4u) gs_words[thread] = 0;
    if (thread == 0) gs_seen = 0;
    GroupMemoryBarrierWithGroupSync();
    if (triangleIndex >= pass.b.z) return;   // uniform across the group

    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    Texture2D<float4> atlas = ResourceDescriptorHeap[g.scene1.z];
    Texture2D<float> alphas = ResourceDescriptorHeap[g.extra.x];
    ByteAddressBuffer indices = ResourceDescriptorHeap[pass.a.x];
    MaterialRecord m = materials[pass.a.z];
    uint3 tri = indices.Load3((pass.b.w + triangleIndex * 3u) * 4u);
    float2 uv0 = 0, uv1 = 0, uv2 = 0;
    if (pass.a.y != INVALID) {
        ByteAddressBuffer uvs = ResourceDescriptorHeap[pass.a.y];
        uv0 = asfloat(uvs.Load2(tri.x * 8u)); uv1 = asfloat(uvs.Load2(tri.y * 8u)); uv2 = asfloat(uvs.Load2(tri.z * 8u));
    }
    // As the alpha test reads them: the material's tiling applied, in texels of the tile it reads (the alpha
    // tile at 256x256 when the material has one, else the albedo tile at 64x64).
    const bool alphaTile = (m.flags & MATERIAL_ALPHA_TILE) != 0;
    const uint tileSize = alphaTile ? ALPHA_TILE_SIZE : TILE_SIZE;
    uv0 = (uv0 * m.uvST.xy + m.uvST.zw) * tileSize;
    uv1 = (uv1 * m.uvST.xy + m.uvST.zw) * tileSize;
    uv2 = (uv2 * m.uvST.xy + m.uvST.zw) * tileSize;
    uint2 origin = alphaTile ? uint2(uint(m.alphaTile) % ALPHA_TILES_PER_ROW, uint(m.alphaTile) / ALPHA_TILES_PER_ROW) * ALPHA_TILE_SIZE
                             : uint2(uint(m.albedoTile) % TILES_PER_ROW, uint(m.albedoTile) / TILES_PER_ROW) * TILE_SIZE;

    for (uint i = thread; i < OMM_COUNT; i += 64u) {
        uint state;
        if (m.albedoTile < 0) {
            state = m.baseColor.a >= m.alphaCutoff ? OMM_OPAQUE : OMM_TRANSPARENT;
        } else {
            float2 c0, c1, c2;
            micro_triangle(i, c0, c1, c2);
            float2 p0 = uv0 * (1.0 - c0.x - c0.y) + uv1 * c0.x + uv2 * c0.y;
            float2 p1 = uv0 * (1.0 - c1.x - c1.y) + uv1 * c1.x + uv2 * c1.y;
            float2 p2 = uv0 * (1.0 - c2.x - c2.y) + uv1 * c2.x + uv2 * c2.y;
            // The texels the micro-triangle's box covers, its far edges open: an edge that lies exactly on a
            // texel boundary (UVs laid out on the texel grid) would otherwise pull in the next texel and leave
            // every micro-triangle there unknown. Only hits within rounding of such an edge may then read
            // the next texel where the micromap has already decided. The alpha tile is filtered bilinearly
            // (alpha_sample): every texel a point's 2x2 footprint reads counts, so a micro-triangle is opaque or
            // transparent only when the filtered alpha is everywhere in it.
            float2 lo = floor(min(p0, min(p1, p2))), hi = ceil(max(p0, max(p1, p2))) - 1.0;
            if (alphaTile) { lo = floor(min(p0, min(p1, p2)) - 0.5); hi = floor(max(p0, max(p1, p2)) - 0.5) + 1.0; }
            hi = max(hi, lo);
            float2 extent = hi - lo + 1.0;
            // Far out on the texture plane a float no longer tells texels apart (from 2^24 on, and the wrap below
            // loses them well before): the alpha test decides per hit there.
            if (!all(isfinite(extent)) || !all(isfinite(lo)) || any(abs(lo) > 1048576.0) || extent.x * extent.y > float(OMM_MAX_TEXELS)) {
                state = OMM_UNKNOWN_OPAQUE;
            } else {
                bool anyOpaque = false, anyTransparent = false;
                const uint2 count = uint2(extent);
                for (uint j = 0; j < count.y; ++j) {
                    for (uint k = 0; k < count.x; ++k) {
                        const float x = lo.x + float(k), y = lo.y + float(j);
                        // The tile repeats: texel coordinates wrap as frac() does for the alpha test.
                        uint2 texel = uint2(int2(float2(x, y) - floor(float2(x, y) / tileSize) * tileSize)) & (tileSize - 1u);
                        // An alpha tile holds coverage for the material's cutoff, its base alpha included (alpha_opaque), which
                        // stops the rays other than the camera's by chance: known only where a texel is fully covered or bare.
                        float a = alphaTile ? alphas.Load(int3(origin + texel, 0)) : m.baseColor.a * atlas.Load(int3(origin + texel, 0)).a;
                        if (alphaTile) { if (a > 0.001) anyOpaque = true; if (a < 0.999) anyTransparent = true; }
                        else if (a >= m.alphaCutoff) anyOpaque = true; else anyTransparent = true;
                    }
                }
                state = anyOpaque && anyTransparent ? OMM_UNKNOWN_OPAQUE : anyOpaque ? OMM_OPAQUE : OMM_TRANSPARENT;
            }
        }
        InterlockedOr(gs_words[i >> 4], state << ((i & 15u) * 2u));
        InterlockedOr(gs_seen, 1u << state);
    }
    GroupMemoryBarrierWithGroupSync();

    RWByteAddressBuffer data = ResourceDescriptorHeap[pass.b.x];
    RWByteAddressBuffer ommIndices = ResourceDescriptorHeap[pass.b.y];
    uint omm = pass.a.w + triangleIndex;
    if (thread < OMM_BYTES / 4u) data.Store(omm * OMM_BYTES + thread * 4u, gs_words[thread]);
    if (thread == 0) {
        int index = gs_seen == (1u << OMM_OPAQUE) ? OMM_SPECIAL_FULLY_OPAQUE
                  : gs_seen == (1u << OMM_TRANSPARENT) ? OMM_SPECIAL_FULLY_TRANSPARENT : int(omm);
        ommIndices.Store(omm * 4u, asuint(index));
    }
}
