// SPDX-License-Identifier: MIT
// The water's waves as triangles (PT-71 item 5): this frame's pose of every water unit's vertices (scene.cpp), from the
// game's own wave data (pt_waves water_displace: HDRP's displacement arrays as its water shader reads them), before their
// acceleration structures are refitted. The surface they make is a function of that data and of the camera's position alone
// (docs/agents/water-4.md has the argument); every kind of ray then meets it as it meets any triangle.
//
// Each base triangle of a water mesh carries a lattice that halves again and again. A point that first appears at one
// level sits in the middle of an edge of the level before (its parent edge), and lies
//     lerp(the middle of its two parents' positions, the game's displaced position at the point, w),
//     w = clamp(the parent edge's length / h - 1, 0, 1),  h = max(the slot's cell, the edge's distance from the camera x CELL_PER_METRE)
// so it slides from the coarser surface's own line onto the waves as the camera comes near: no step in space or in time,
// and a finely cut piece beside a coarsely cut one has its extra edge points on the coarse piece's edge (no crack, no
// stitching). The length is the base edge's own for a point on an edge of the base triangle (both neighbours agree, bit
// for bit: the same products summed) and the base triangle's longest edge, halved as the level, for a point inside it.
//
// One dispatch a lattice level, the coarsest first (a point reads its parents, written by the dispatch before); a thread a
// vertex. A triangle's vertices are numbered level by level (the lattice table: scene.cpp water_lattice), so a unit cut
// into 4^m triangles uses the first (2^m + 1)(2^m + 2) / 2 of them.
//   pass.a: the triangles' records (SRV), how many of them reach this level (they are sorted by it), the level, the table (SRV)
#include "pt_common.hlsli"

// scene.cpp WaterTriGpu. node: bits 0 to 4 the node's level L in the base triangle's lattice (0: the whole base triangle),
// bit 5 it points the other way (a middle quarter), bit 6 the base triangle's second and third corner change places (its
// front becomes the water's outward side), bits 8 to 10 how often the unit cuts it (m). Bit 7 (COLLAPSE, scene.hpp
// kWaterTriCollapse): the unit is not traced this frame; index, i and j are then the bits of the one place all its vertices
// go to, and record is not read.
struct WaterTri { uint record, index, node, i, j, at, span, target; };
static const uint COLLAPSE = 128u;
static const uint LEVEL_FIRST[7] = {0u, 3u, 6u, 15u, 45u, 153u, 561u};
static const uint LATTICE = 32u;                    // a node's edge in its finest cells (2^5)
static const float CELL_PER_METRE = 1.0 / 64.0;     // scene.cpp kWaterCellPerMetre

// The same whichever end is named first: two triangles that share an edge name its ends in opposite orders.
bool ordered(float3 a, float3 b) { return a.x != b.x ? a.x < b.x : (a.y != b.y ? a.y < b.y : a.z <= b.z); }
float edge_weight(float3 a, float3 b, float len, float cell) {
    if (!ordered(a, b)) { const float3 t = a; a = b; b = t; }
    const float3 ab = b - a, ac = g.cameraPos.xyz - a;
    const float along = saturate(dot(ac, ab) / max(dot(ab, ab), 1e-30));
    const float h = max(cell, length(ac - ab * along) * CELL_PER_METRE);
    return saturate(len / h - 1.0);
}
float3 morph(float3 a, float3 b, float3 moved, float w) {
    precise float3 middle = (a + b) * 0.5;
    if (w >= 1.0) return moved;
    if (!(w > 0.0)) return middle;
    precise float3 p = middle + (moved - middle) * w;
    return p;
}
// A lattice point's undisplaced place: whole-number coordinates over 2^level, so every unit that holds the point computes
// the same bits, and so do the two triangles of a shared edge (one weight is 0 there: the same two products, summed).
float3 lattice_point(float3 c0, float3 c1, float3 c2, uint ia, uint ja, uint level) {
    const float scale = exp2(-float(level));
    const float u = float(ia) * scale, v = float(ja) * scale, w = float((1u << level) - ia - ja) * scale;
    precise float3 p = c0 * w + c1 * u + c2 * v;
    return p;
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint r = pass.a.z;
    const uint count = LEVEL_FIRST[r + 1u] - LEVEL_FIRST[r];
    const uint k = id.x / count;
    if (k >= pass.a.y) return;
    StructuredBuffer<WaterTri> tris = ResourceDescriptorHeap[pass.a.x];
    const WaterTri tri = tris[k];
    const uint m = (tri.node >> 8) & 7u;
    if (r > m) return;
    const uint v = LEVEL_FIRST[r] + id.x % count;
    // A unit its group holds but this frame does not trace (scene.hpp WaterGroup, PT-99): each of its vertices to the one
    // place the record names (index, i, j: a float3's bits; nothing else of the record is read). Its triangles then have no
    // area and no ray meets them, while its group's structure is refitted as every frame.
    if ((tri.node & COLLAPSE) != 0u) {
        RWByteAddressBuffer held = ResourceDescriptorHeap[NonUniformResourceIndex(tri.target)];
        const uint3 place = uint3(tri.index, tri.i, tri.j);
        held.Store3(tri.at + v * 12u, place);
        held.Store3(tri.at + tri.span + v * 12u, place);
        held.Store3(tri.at + 2u * tri.span + v * 12u, place);
        return;
    }
    StructuredBuffer<uint> table = ResourceDescriptorHeap[pass.a.w];
    const uint I = table[v] & 0xFFu, J = (table[v] >> 8) & 0xFFu;

    StructuredBuffer<WaterRecord> records = ResourceDescriptorHeap[g.waterGeometry.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const WaterRecord record = records[tri.record];
    const MeshRecord mesh = meshes[record.mesh];
    ByteAddressBuffer ib = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.indices)];
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    uint3 idx = ib.Load3(tri.index * 4u);
    if ((tri.node & 64u) != 0u) idx = idx.xzy;
    const float3x4 toWorld = float3x4(record.transform[0], record.transform[1], record.transform[2]);
    const float3 c0 = mul(toWorld, float4(asfloat(pb.Load3(idx.x * 12u)), 1.0));
    const float3 c1 = mul(toWorld, float4(asfloat(pb.Load3(idx.y * 12u)), 1.0));
    const float3 c2 = mul(toWorld, float4(asfloat(pb.Load3(idx.z * 12u)), 1.0));
    const uint slot = record.slot;
    // Still water (no wave data this frame) has nothing to resolve: every point stays on its parents' line.
    const float cell = record.cell > 0.0 ? record.cell : 1e30;

    const uint L = tri.node & 31u, level = L + 5u, whole = 1u << level;
    const bool flip = (tri.node & 32u) != 0u;
    // The node's own lattice coordinates (I, J: 0 to 32) in the base triangle's, at the finest level.
    #define ABSOLUTE(pi, pj) (flip ? uint2((tri.i + 1u) * LATTICE - (pi), (tri.j + 1u) * LATTICE - (pj)) : uint2(tri.i * LATTICE + (pi), tri.j * LATTICE + (pj)))
    const uint2 at = ABSOLUTE(I, J);
    const float3 base = lattice_point(c0, c1, c2, at.x, at.y, level);
    const bool onEdge = at.x == 0u || at.y == 0u || at.x + at.y == whole;

    RWByteAddressBuffer vertices = ResourceDescriptorHeap[NonUniformResourceIndex(tri.target)];
    float3 position, before;
    if (r == 0u) {
        const bool corner = (at.x == 0u || at.x == whole) && (at.y == 0u || at.y == whole);
        if (corner || !onEdge) {
            // A corner of the base triangle, or a node's corner inside it: the camera is near enough that the level before
            // this node's is wholly wanted there (scene.cpp cuts a node only then), so the point lies on the waves.
            position = water_moved(slot, base);
            before = water_moved_before(slot, base, position);
        } else {
            // A node's corner on an edge of the base triangle: down that edge's own halvings from its two ends, as the
            // triangle on its other side does.
            float3 ea, eb; uint target;
            if (at.y == 0u) { ea = c0; eb = c1; target = at.x; }
            else if (at.x == 0u) { ea = c0; eb = c2; target = at.y; }
            else { ea = c1; eb = c2; target = at.y; }
            uint lo = 0u, hi = whole;
            float3 baseLo = ea, baseHi = eb;
            float3 pLo = water_moved(slot, ea), pHi = water_moved(slot, eb);
            float3 qLo = water_moved_before(slot, ea, pLo), qHi = water_moved_before(slot, eb, pHi);
            position = pLo; before = qLo;
            const float scale = exp2(-float(level));
            [loop] for (uint step = 0u; step < level; ++step) {
                const uint mid = (lo + hi) >> 1u;
                precise float3 baseMid = ea * (float(whole - mid) * scale) + eb * (float(mid) * scale);
                const float w = edge_weight(baseLo, baseHi, length(baseHi - baseLo), cell);
                float3 moved = 0.0, movedBefore = 0.0;
                if (w > 0.0) { moved = water_moved(slot, baseMid); movedBefore = water_moved_before(slot, baseMid, moved); }
                const float3 pMid = morph(pLo, pHi, moved, w), qMid = morph(qLo, qHi, movedBefore, w);
                if (target == mid) { position = pMid; before = qMid; break; }
                if (target < mid) { hi = mid; baseHi = baseMid; pHi = pMid; qHi = qMid; }
                else { lo = mid; baseLo = baseMid; pLo = pMid; qLo = qMid; }
            }
        }
    } else {
        // Its parent edge: the two points of the level before it lies between, along one of the lattice's three directions.
        const uint s = LATTICE >> r;
        const bool oddI = ((I / s) & 1u) != 0u, oddJ = ((J / s) & 1u) != 0u;
        const uint2 pa = oddI && oddJ ? uint2(I + s, J - s) : (oddI ? uint2(I - s, J) : uint2(I, J - s));
        const uint2 pb2 = oddI && oddJ ? uint2(I - s, J + s) : (oddI ? uint2(I + s, J) : uint2(I, J + s));
        const uint va = table[561u + pa.y * 33u + pa.x], vb = table[561u + pb2.y * 33u + pb2.x];
        const uint2 aa = ABSOLUTE(pa.x, pa.y), ab = ABSOLUTE(pb2.x, pb2.y);
        const float3 baseA = lattice_point(c0, c1, c2, aa.x, aa.y, level), baseB = lattice_point(c0, c1, c2, ab.x, ab.y, level);
        const float longest = max(length(c1 - c0), max(length(c2 - c0), length(c2 - c1)));
        const float len = onEdge ? length(baseB - baseA) : longest * (float(2u * s) * exp2(-float(level)));
        const float w = edge_weight(baseA, baseB, len, cell);
        const float3 pA = asfloat(vertices.Load3(tri.at + va * 12u)), pB = asfloat(vertices.Load3(tri.at + vb * 12u));
        const float3 qA = asfloat(vertices.Load3(tri.at + 2u * tri.span + va * 12u)), qB = asfloat(vertices.Load3(tri.at + 2u * tri.span + vb * 12u));
        float3 moved = 0.0, movedBefore = 0.0;
        if (w > 0.0) { moved = water_moved(slot, base); movedBefore = water_moved_before(slot, base, moved); }
        position = morph(pA, pB, moved, w);
        before = morph(qA, qB, movedBefore, w);
    }
    vertices.Store3(tri.at + v * 12u, asuint(position));
    vertices.Store3(tri.at + tri.span + v * 12u, asuint(base));
    vertices.Store3(tri.at + 2u * tri.span + v * 12u, asuint(before));
}
