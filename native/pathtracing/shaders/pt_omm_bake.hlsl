// SPDX-License-Identifier: MIT
// Opacity micromaps (DirectX Raytracing 1.2) for one alpha-tested submesh: a group per triangle writes
// its 4-state micromap of 4^OMM_LEVEL micro-triangles (2 bits each) and its entry in the micromap index
// buffer. A micromap only answers early what the traversal's alpha test (pt_common alpha_opaque) would
// answer: a micro-triangle is opaque or transparent only when every texel the test can read anywhere in it
// agrees; otherwise, or when it reaches more texels than are worth reading, it stays unknown and the alpha
// test decides as before. A triangle whose micro-triangles all agree takes one of DXR's special indices
// instead of its micromap.
// It is baked from what the test reads: the game's own texture where the material's alpha test reads that
// (shared_cutout: its top mip, the base color's alpha, the material's own cutoff), else the alpha tile where
// the material has one, else its albedo tile.
//   pass.a: the mesh (its record: indices and UVs), the texture's alpha bounds (raw UAV, or none), material,
//           first micromap of the submesh
//   pass.b: micromap data (raw UAV), micromap indices (raw UAV), triangle count (its top 4 bits: the subdivision level, 4,
//           or on trial a leaf's 5 or 6: scene.cpp omm_level), first index of the submesh
// The same pass makes a texture's alpha bounds first (pass.a.z names the step instead of a material): the least
// and the most alpha of every 4 x 4 texels of its top mip, then of every 2 x 2 of those, up to one cell for
// the whole texture; one after another in one buffer. With them a micro-triangle that covers hundreds of
// texels (a leaf card on a 2048 x 2048 texture) is settled by a few cells, where texel by texel it would be
// left unknown. They are made for a bake and dropped after it.
//   OMM_BOUNDS_FIRST: pass.a: the texture's number, the bounds (raw UAV), the step, -; pass.b: cells across, down
//   OMM_BOUNDS_NEXT:  pass.a: the finer level's first cell, the bounds, the step, this level's first cell;
//                     pass.b: cells across, down, the finer level's across, down
#include "pt_common.hlsli"

// The subdivision level is the submesh's (pass.b.z's top 4 bits): 4^level micro-triangles a triangle, 2 bits each.
static const uint OMM_LEVEL_MOST = 6u;                         // scene.hpp kOmmLevelMost
static const uint OMM_WORDS_MOST = (1u << (2u * OMM_LEVEL_MOST)) / 16u;
uint omm_level() { return pass.b.z >> 28; }
uint omm_count(uint level) { return 1u << (2u * level); }      // micro-triangles per triangle
uint omm_bytes(uint level) { return omm_count(level) / 4u; }   // 2 bits each
static const uint OMM_TRANSPARENT = 0, OMM_OPAQUE = 1, OMM_UNKNOWN_OPAQUE = 3;
static const int OMM_SPECIAL_FULLY_TRANSPARENT = -1, OMM_SPECIAL_FULLY_OPAQUE = -2;
static const uint OMM_MAX_TEXELS = 64;
static const uint OMM_BOUNDS_FIRST = 0xFFFFFFFEu, OMM_BOUNDS_NEXT = 0xFFFFFFFDu;
static const uint OMM_BOUNDS_LEVEL = 2u;                       // the finest cells: 4 x 4 texels (scene.cpp kOmmBoundsLevel)
// A cell: its least alpha in the low 16 bits (rounded down), its most in the high (rounded up). A cell with a texel
// whose alpha is no number in [0, 1] has a least above its most: nothing is known of it.
static const uint OMM_BOUNDS_UNKNOWN = 0x0000FFFFu;
// A texel this close to the cutoff is left to the test (the sampler's rounding of its weights), and a hit may read
// texels this far outside where its position says (the sampler's rounding of a position to 1/256 of a texel).
static const float OMM_MARGIN = 1.0 / 1024.0, OMM_GUARD = 1.0 / 64.0;

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
void micro_triangle(uint index, uint level, out float2 c0, out float2 c1, out float2 c2) {
    uint b0 = even_bits(index), b1 = even_bits(index >> 1);
    uint fx = prefix_xor(b0), fy = prefix_xor(b0 & ~b1);
    uint t = fy ^ b1;
    uint mask = (1u << level) - 1u;
    uint u = ((fx & ~t) | (b0 & ~t) | (~b0 & ~fx & t)) & mask;
    uint v = (fy ^ b0) & mask;
    uint w = ((~fx & ~t) | (b0 & ~t) | (~b0 & fx & t)) & mask;
    bool upright = ((u ^ v ^ w) & 1u) != 0;
    if (!upright) { u += 1u; v += 1u; }
    float step = 1.0 / float(1u << level);
    float d = upright ? step : -step;
    c0 = float2(u, v) * step; c1 = c0 + float2(d, 0); c2 = c0 + float2(0, d);
}

// One cell of a texture's alpha bounds (a thread each).
void bounds_step(uint3 group, uint thread) {
    const uint cell = (group.y * 32768u + group.x) * 64u + thread;
    const uint2 cells = pass.b.xy;
    if (cell >= cells.x * cells.y) return;
    const uint2 at = uint2(cell % cells.x, cell / cells.x);
    RWByteAddressBuffer bounds = ResourceDescriptorHeap[pass.a.y];
    uint least = 65535u, most = 0u;
    bool known = true;
    if (pass.a.z == OMM_BOUNDS_FIRST) {
        Texture2D<float4> t = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + pass.a.x)];
        uint w, h, levels;
        t.GetDimensions(0, w, h, levels);
        const uint side = 1u << OMM_BOUNDS_LEVEL;
        for (uint j = 0; j < side; ++j) {
            for (uint i = 0; i < side; ++i) {
                const uint2 p = at * side + uint2(i, j);
                if (p.x < w && p.y < h) {   // a texture narrower than a cell has fewer
                    const float a = t.Load(int3(p, 0)).a;
                    if (!(a >= 0.0 && a <= 1.0)) known = false;
                    else { least = min(least, uint(floor(a * 65535.0))); most = max(most, uint(ceil(a * 65535.0))); }
                }
            }
        }
        bounds.Store(cell * 4u, known ? (least | (most << 16)) : OMM_BOUNDS_UNKNOWN);
    } else {
        const uint2 finer = pass.b.zw;
        for (uint j = 0; j < 2u; ++j) {
            for (uint i = 0; i < 2u; ++i) {
                const uint2 p = at * 2u + uint2(i, j);
                if (p.x < finer.x && p.y < finer.y) {
                    const uint v = bounds.Load((pass.a.x + p.y * finer.x + p.x) * 4u);
                    if ((v & 0xFFFFu) > (v >> 16)) known = false;
                    least = min(least, v & 0xFFFFu); most = max(most, v >> 16);
                }
            }
        }
        bounds.Store((pass.a.w + cell) * 4u, known ? (least | (most << 16)) : OMM_BOUNDS_UNKNOWN);
    }
}

// Whether the triangle a, b, c meets the box [lo, hi] (texels, the plane as the triangle's corners name it): the two axes
// of the box and the triangle's three edge normals (the separating axes in 2D). A degenerate edge separates nothing.
bool triangle_meets_box(float2 a, float2 b, float2 c, float2 lo, float2 hi) {
    if (max(a.x, max(b.x, c.x)) < lo.x || min(a.x, min(b.x, c.x)) > hi.x) return false;
    if (max(a.y, max(b.y, c.y)) < lo.y || min(a.y, min(b.y, c.y)) > hi.y) return false;
    const float2 center = 0.5 * (lo + hi), half = 0.5 * (hi - lo);
    const float2 edges[3] = { b - a, c - b, a - c };
    [unroll] for (uint i = 0; i < 3u; ++i) {
        const float2 n = float2(-edges[i].y, edges[i].x);
        const float pa = dot(n, a), pb = dot(n, b), pc = dot(n, c);
        const float r = abs(n.x) * half.x + abs(n.y) * half.y, at = dot(n, center);
        if (at + r < min(pa, min(pb, pc)) || at - r > max(pa, max(pb, pc))) return false;
    }
    return true;
}

// A micro-triangle (its corners in texels of the texture's top mip, the tiling applied) of a cut-out whose alpha test reads
// the game's own texture: pt_common material_alpha takes the top mip filtered between the four texels around the hit
// (SampleGrad with no footprint, wrapping), times the base color's alpha, against the material's cutoff. A filtered value
// lies between the least and the most of its four texels. So the micro-triangle is opaque when every texel a hit in it can
// reach is above the cutoff, transparent when every one is below, and unknown otherwise: also where a texel is within
// OMM_MARGIN of the cutoff. "Can reach" is the texels around every point of the micro-triangle itself, and OMM_GUARD more
// (and the float's own rounding of a position far out on the texture plane): a texel t is read by the points of
// [t - 0.5, t + 1.5) each way, so it counts where that square, grown by the guard, meets the triangle. Until 2026-10-11 every
// texel of the triangle's box counted (docs/agents/review-perf-astra.md 4, NVIDIA's OMM guide: the alpha contour against
// the micro-triangle): a thin, slanted micro-triangle's box holds texels it never reaches, and a mixed one there left it
// unknown, a shader's alpha test for every ray through it. The same states wherever both agree; never a wrong one.
uint own_state(Texture2D<float4> t, uint2 size, uint boundsIndex, MaterialRecord m, float2 p0, float2 p1, float2 p2) {
    const float2 pmin = min(p0, min(p1, p2)), pmax = max(p0, max(p1, p2));
    const float reach = max(max(abs(pmin.x), abs(pmin.y)), max(abs(pmax.x), abs(pmax.y)));
    // Far out a float no longer tells texels apart (from 2^24 on): the alpha test decides per hit there.
    if (!(reach <= 1048576.0) || size.x == 0u || size.y == 0u) return OMM_UNKNOWN_OPAQUE;
    const float guard = OMM_GUARD + reach * 2e-6;
    const float2 lo = floor(pmin - 0.5 - guard), hi = floor(pmax - 0.5 + guard) + 1.0;
    const float2 extent = hi - lo + 1.0;
    const float covered = m.alphaCutoff + OMM_MARGIN, bare = m.alphaCutoff - OMM_MARGIN;
    bool anyOpaque = false, anyTransparent = false;
    if (extent.x * extent.y <= float(OMM_MAX_TEXELS)) {
        // Few texels: each one read, wrapped as the sampler wraps.
        const float2 fsize = float2(size);
        const uint2 count = uint2(extent);
        for (uint j = 0; j < count.y && !(anyOpaque && anyTransparent); ++j) {
            for (uint k = 0; k < count.x; ++k) {
                const float2 xy = float2(lo.x + float(k), lo.y + float(j));
                if (!triangle_meets_box(p0, p1, p2, xy - 0.5 - guard, xy + 1.5 + guard)) continue;
                const uint2 texel = min(uint2(max(xy - floor(xy / fsize) * fsize, 0.0)), size - 1u);
                const float a = m.baseColor.a * t.Load(int3(texel, 0)).a;
                if (a >= covered) anyOpaque = true;
                else if (a < bare) anyTransparent = true;
                else { anyOpaque = true; anyTransparent = true; }
            }
        }
    } else {
        // Many: the texture's alpha bounds (a texture whose sides are powers of two has them), at the level whose cells
        // the box spans at most 8 of each way: the least and the most of a few more texels than the box holds.
        if (boundsIndex == INVALID || (size.x & (size.x - 1u)) != 0u || (size.y & (size.y - 1u)) != 0u) return OMM_UNKNOWN_OPAQUE;
        RWByteAddressBuffer bounds = ResourceDescriptorHeap[boundsIndex];
        const int2 ilo = int2(lo), ihi = int2(hi);
        const uint longest = max(size.x, size.y);
        uint level = OMM_BOUNDS_LEVEL, first = 0u;
        uint2 cells = max(size >> level, uint2(1u, 1u));
        while ((1u << level) < longest && (((ihi.x >> level) - (ilo.x >> level) + 1) > 8 || ((ihi.y >> level) - (ilo.y >> level) + 1) > 8)) {
            first += cells.x * cells.y;
            ++level;
            cells = max(size >> level, uint2(1u, 1u));
        }
        // A box wider than the texture meets every cell of the level that way (they repeat).
        const int2 from = int2(ilo.x >> level, ilo.y >> level);
        const uint2 span = min(uint2(int2(ihi.x >> level, ihi.y >> level) - from + 1), cells);
        for (uint j = 0; j < span.y && !(anyOpaque && anyTransparent); ++j) {
            for (uint k = 0; k < span.x; ++k) {
                // The cell's texels on the plane (before the wrap), and the square their reads come from.
                const int2 cellAt = from + int2(k, j);
                const float2 cellLo = float2(cellAt << level), cellHi = float2((cellAt + 1) << level) - 1.0;
                if (span.x < cells.x && span.y < cells.y && !triangle_meets_box(p0, p1, p2, cellLo - 0.5 - guard, cellHi + 1.5 + guard)) continue;
                const uint2 c = uint2(cellAt) & (cells - 1u);
                const uint v = bounds.Load((first + c.y * cells.x + c.x) * 4u);
                const float least = m.baseColor.a * float(v & 0xFFFFu) / 65535.0, most = m.baseColor.a * float(v >> 16) / 65535.0;
                if ((v & 0xFFFFu) > (v >> 16)) { anyOpaque = true; anyTransparent = true; }
                else if (min(least, most) >= covered) anyOpaque = true;
                else if (max(least, most) < bare) anyTransparent = true;
                else { anyOpaque = true; anyTransparent = true; }
            }
        }
    }
    return anyOpaque && anyTransparent ? OMM_UNKNOWN_OPAQUE : anyOpaque ? OMM_OPAQUE : OMM_TRANSPARENT;
}

groupshared uint gs_words[OMM_WORDS_MOST];
groupshared uint gs_seen;   // bit per state found

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 group : SV_GroupID, uint thread : SV_GroupIndex) {
    if (pass.a.z >= OMM_BOUNDS_NEXT) { bounds_step(group, thread); return; }   // the same for every thread of the pass
    uint triangleIndex = group.y * 32768u + group.x;
    const uint level = omm_level(), words = omm_bytes(level) / 4u;
    for (uint w = thread; w < words; w += 64u) gs_words[w] = 0;
    if (thread == 0) gs_seen = 0;
    GroupMemoryBarrierWithGroupSync();
    if (triangleIndex >= (pass.b.z & 0x0FFFFFFFu)) return;   // uniform across the group

    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    Texture2D<float4> atlas = ResourceDescriptorHeap[g.scene1.z];
    Texture2D<float> alphas = ResourceDescriptorHeap[g.extra.x];
    const MeshRecord mesh = meshes[pass.a.x];
    ByteAddressBuffer indices = ResourceDescriptorHeap[mesh.indices];
    MaterialRecord m = materials[pass.a.z];
    uint3 tri = indices.Load3((pass.b.w + triangleIndex * 3u) * 4u);
    float2 uv0 = 0, uv1 = 0, uv2 = 0;
    if (mesh.uvs != INVALID) {
        ByteAddressBuffer uvs = ResourceDescriptorHeap[mesh.uvs];
        uv0 = asfloat(uvs.Load2(tri.x * 8u)); uv1 = asfloat(uvs.Load2(tri.y * 8u)); uv2 = asfloat(uvs.Load2(tri.z * 8u));
    }
    // As the alpha test reads them: the material's tiling applied, in texels of what it reads (the game's own texture's
    // top mip; else the alpha tile at 256x256 when the material has one, else the albedo tile at 64x64).
    const bool own = shared_cutout(m);
    Texture2D<float4> ownTexture = ResourceDescriptorHeap[NonUniformResourceIndex(own ? g.textures.x + uint(m.colorTexture) : g.scene1.z)];
    uint2 ownSize = uint2(0u, 0u);
    if (own) { uint w, h, levels; ownTexture.GetDimensions(0, w, h, levels); ownSize = uint2(w, h); }
    const bool alphaTile = (m.flags & MATERIAL_ALPHA_TILE) != 0;
    const uint tileSize = alphaTile ? ALPHA_TILE_SIZE : TILE_SIZE;
    const float2 texels = own ? float2(ownSize) : float2(tileSize, tileSize);
    uv0 = (uv0 * m.uvST.xy + m.uvST.zw) * texels;
    uv1 = (uv1 * m.uvST.xy + m.uvST.zw) * texels;
    uv2 = (uv2 * m.uvST.xy + m.uvST.zw) * texels;
    uint2 origin = alphaTile ? uint2(uint(m.alphaTile) % ALPHA_TILES_PER_ROW, uint(m.alphaTile) / ALPHA_TILES_PER_ROW) * ALPHA_TILE_SIZE
                             : uint2(uint(m.albedoTile) % TILES_PER_ROW, uint(m.albedoTile) / TILES_PER_ROW) * TILE_SIZE;

    for (uint i = thread; i < omm_count(level); i += 64u) {
        uint state;
        if (own) {
            float2 c0, c1, c2;
            micro_triangle(i, level, c0, c1, c2);
            state = own_state(ownTexture, ownSize, pass.a.y, m,
                              uv0 * (1.0 - c0.x - c0.y) + uv1 * c0.x + uv2 * c0.y,
                              uv0 * (1.0 - c1.x - c1.y) + uv1 * c1.x + uv2 * c1.y,
                              uv0 * (1.0 - c2.x - c2.y) + uv1 * c2.x + uv2 * c2.y);
        } else if (m.albedoTile < 0) {
            state = m.baseColor.a >= m.alphaCutoff ? OMM_OPAQUE : OMM_TRANSPARENT;
        } else {
            float2 c0, c1, c2;
            micro_triangle(i, level, c0, c1, c2);
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
        // Thin cloth lets a light's shadow ray and a bounce ray through its weave by chance where it covers (pt_common
        // alpha_opaque): its covered micro-triangles are left to that test; its bare ones are still settled. A leaf's and a
        // blade of grass's covered ones are opaque (round 20): every ray but a light's stops there, and a light's ray meets
        // leaves and grass in a pass of its own that hands their covered pieces to the test (pt_common foliage_blocks,
        // MASK_FOLIAGE). Until round 20 they were unknown to every ray, a shader's test at every leaf a bounce or a
        // reflection crossed.
        if ((m.flags & MATERIAL_THIN_CLOTH) != 0 && state == OMM_OPAQUE) state = OMM_UNKNOWN_OPAQUE;
        InterlockedOr(gs_words[i >> 4], state << ((i & 15u) * 2u));
        InterlockedOr(gs_seen, 1u << state);
    }
    GroupMemoryBarrierWithGroupSync();

    RWByteAddressBuffer data = ResourceDescriptorHeap[pass.b.x];
    RWByteAddressBuffer ommIndices = ResourceDescriptorHeap[pass.b.y];
    uint omm = pass.a.w + triangleIndex;
    for (uint w = thread; w < words; w += 64u) data.Store(omm * omm_bytes(level) + w * 4u, gs_words[w]);
    if (thread == 0) {
        int index = gs_seen == (1u << OMM_OPAQUE) ? OMM_SPECIAL_FULLY_OPAQUE
                  : gs_seen == (1u << OMM_TRANSPARENT) ? OMM_SPECIAL_FULLY_TRANSPARENT : int(omm);
        ommIndices.Store(omm * 4u, asuint(index));
    }
}
