// SPDX-License-Identifier: MIT
// The water's wave corners, kept once a frame for the sun's question inside the water (PT-81; pt_water_sun.hlsli water_sun,
// docs/agents/water-8.md). A scatter point asks for the sun through the waves' corners round the place its light comes in
// by, each the game's own wave there (pt_waves water_displace: 15 texture reads). A corner depends on its slot, its place
// on the slot's level plane and the camera alone, so a slot's corners round the camera are made here, once, and the
// point reads them (two loads a corner). What a corner holds is water_displace's own answer at its place, nothing worked
// on: the wave's point and its normal.
//   And the corners' tiles (SUN_TILE cells a side): a record a tile of what its corners can do (SunRecord: the box of
// their moves off their grid places and the box of the rays still water's facets bend there, in sun_frame's frame, and
// the least cosine a normal makes with the sun), by which a point leaves out the tiles no ray can reach it through
// without reading their corners; then a record a row of tiles, and one for the whole grid (the block of tiles a point
// asks is laid by it).
//   One dispatch a slot that has a grid (g.sunGrid: its two steps, its first corner, the plane's height, its first byte in
// the buffer) and a stage. pass.a.x: the slot. pass.a.y: the stage.
//   0 (or none): a thread a corner, (x, y) its count along the grid's two steps;
//   1: a thread a tile, from its corners;
//   2: a thread (x) a row of tiles, from its tiles' records;
//   3: one thread, the whole grid's record from the rows'.
#include "pt_common.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const uint slot = pass.a.x, stage = pass.a.y == INVALID ? 0u : pass.a.y;
    if (slot >= 4u || !sun_grid_slot(slot)) return;
    const float4 spans = g.sunGrid[slot * 2u], first = g.sunGrid[slot * 2u + 1u];
    RWByteAddressBuffer corners = ResourceDescriptorHeap[g.waterGeometry.z];
    const uint slotAt = asuint(first.w);
    if (stage == 0u) {
        if (id.x >= SUN_CORNERS || id.y >= SUN_CORNERS) return;
        const float2 xz = first.xy + spans.xy * float(id.x) + spans.zw * float(id.y);
        float3 position, normal; float lift, steep;
        water_displace(slot, float3(xz.x, first.z, xz.y), float3(0.0, 1.0, 0.0), position, normal, lift, steep);
        const uint address = slotAt + (id.y * SUN_CORNERS + id.x) * SUN_CORNER_BYTES;
        corners.Store4(address, asuint(float4(position, normal.x)));
        corners.Store2(address + 16u, asuint(normal.yz));
    } else if (stage == 1u) {
        // A tile's record from its corners (the far ones are its neighbours' first ones: the last tile of a row has none).
        if (id.x >= SUN_TILES || id.y >= SUN_TILES) return;
        SunRecord record = sun_record_none();
        if (id.x + 1u < SUN_TILES && id.y + 1u < SUN_TILES) {
            const float3 l = g.sunDir.xyz;
            float3 a0, b0, t00; sun_frame(a0, b0, t00);
            for (uint j = 0; j <= SUN_TILE; ++j) for (uint i = 0; i <= SUN_TILE; ++i) {
                const uint2 at = id.xy * SUN_TILE + uint2(i, j);
                const uint address = slotAt + (at.y * SUN_CORNERS + at.x) * SUN_CORNER_BYTES;
                const float4 head = asfloat(corners.Load4(address));
                const float3 position = head.xyz, normal = float3(head.w, asfloat(corners.Load2(address + 16u)));
                const float2 xz = first.xy + spans.xy * float(at.x) + spans.zw * float(at.y);
                const float3 move = position - float3(xz.x, first.z, xz.y);
                // The ray still water's facet bends here: the wave's own normal the facet (pt_water_sun sun_corner, no tilt).
                const float3 ray = refract(-l, normal, 1.0 / WATER_IOR);
                if (!all(isfinite(position)) || !all(isfinite(normal)) || !all(isfinite(ray))) continue; // (no corner of a patch either)
                SunRecord one;
                one.moveLo = float3(dot(move, a0), dot(move, b0), dot(move, t00)); one.moveHi = one.moveLo;
                one.rayLo = float3(dot(ray, a0), dot(ray, b0), dot(ray, t00)); one.rayHi = one.rayLo;
                one.least = dot(normal, l);
                record = sun_record_join(record, one);
            }
        }
        sun_record_store(corners, slotAt + SUN_TILES_AT + (id.y * SUN_TILES + id.x) * SUN_RECORD_BYTES, record);
    } else if (stage == 2u) {
        if (id.x >= SUN_TILES || id.y != 0u) return;
        SunRecord record = sun_record_none();
        for (uint i = 0; i < SUN_TILES; ++i) record = sun_record_join(record, sun_record_load(corners, slotAt + SUN_TILES_AT + (id.x * SUN_TILES + i) * SUN_RECORD_BYTES));
        sun_record_store(corners, slotAt + SUN_ROWS_AT + id.x * SUN_RECORD_BYTES, record);
    } else if (stage == 3u) {
        if (id.x != 0u || id.y != 0u) return;
        SunRecord record = sun_record_none();
        for (uint i = 0; i < SUN_TILES; ++i) record = sun_record_join(record, sun_record_load(corners, slotAt + SUN_ROWS_AT + i * SUN_RECORD_BYTES));
        sun_record_store(corners, slotAt + SUN_WHOLE_AT, record);
    }
}
