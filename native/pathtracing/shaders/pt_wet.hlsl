// SPDX-License-Identifier: MIT Where the water has been (PT-52). Each frame, before the camera's surfaces
// are made (pt_prepare), every cell of the store round the camera asks where HDRP's water stands over its
// middle this frame (pt_water_surface water_over: the tracer's own water surface, the waves as every ray
// meets them) and remembers it. The store's cells are the world's (levels of WET_GRID x WET_GRID round the
// camera, the finest g.wetGrid.x, each next twice as coarse; a cell keeps its place in the world as the
// camera moves, its record found by its world index modulo the grid, its key the index itself), so what a
// wave wetted stays wet behind the camera and when it comes back. A cell's record: how long ago the water
// last stood at or above each height, as points (height in the world, time) down a falling line, the oldest
// highest; between two points the water fell from the one to the other. A new reading (W, now) drops every
// point below W (the water stands over them now), keeping where the remembered fall crossed W, and joins the
// line; a level held since it came (two points at it) moves its newer point on to now; a point that lies on
// the line from the one before it to the new one (the water fell steadily) is dropped. With all WET_POINTS
// kept, the point between two others whose line misses it by the least time goes. Points older than twice
// the slowest drying (sand's) are forgotten. One thread a cell: (x, y) = (cell, level x WET_GRID + row).
// pass.a.x: 1 empties every level's cells, 0 reads the water.
#include "pt_common.hlsli"
#include "pt_waves.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (g.wet.x == INVALID) return;
    if (pass.a.x == 1u) {
        RWByteAddressBuffer all = ResourceDescriptorHeap[g.wet.x];
        const uint offset = (id.y * WET_GRID + id.x) * WET_CELL_BYTES;
        [unroll] for (uint k = 0; k < WET_CELL_BYTES; k += 16) all.Store4(offset + k, uint4(0u, 0u, 0u, 0u));
        return;
    }
    if (!wet_on()) return;
    const uint level = id.y / WET_GRID;
    if (level >= g.wet.y || id.x >= WET_GRID) return;
    const int2 local = int2(id.x, id.y % WET_GRID);
    uint key;
    const uint at = wet_offset(level, local, key);
    RWByteAddressBuffer store = ResourceDescriptorHeap[g.wet.x];
    const uint4 head = store.Load4(at);
    const uint4 q0 = store.Load4(at + 16), q1 = store.Load4(at + 32), q2 = store.Load4(at + 48);
    float h[6] = { asfloat(q0.x), asfloat(q0.z), asfloat(q1.x), asfloat(q1.z), asfloat(q2.x), asfloat(q2.z) };
    float t[6] = { asfloat(q0.y), asfloat(q0.w), asfloat(q1.y), asfloat(q1.w), asfloat(q2.y), asfloat(q2.w) };
    uint n = head.x == key ? min(head.y, WET_POINTS) : 0u;
    const float clock = wet_now();
    // A clock that went back (the game's time started over): forgotten.
    if (n > 0u && !(asfloat(head.w) <= clock + 1.0)) n = 0u;
    // What dried long ago: forgotten.
    const float keep = 2.0 * WET_SAND_HOLDS / WET_EVAPORATION;
    uint gone = 0u;
    [unroll] for (uint i = 0; i < WET_POINTS; ++i) if (i < n && clock - t[i] > keep) gone = i + 1u;
    if (gone > 0u) {
        [unroll] for (uint i = 0; i < WET_POINTS; ++i) { const uint from = min(i + gone, WET_POINTS - 1u); h[i] = h[from]; t[i] = t[from]; }
        n -= gone;
    }
    // The water over the cell's middle now.
    const float cell = g.wetGrid.x * exp2(float(level));
    const float2 xz = g.wetLevels[level].xy + (float2(local) + 0.5) * cell;
    uint slot = 0u; float height = 0.0;
    if (water_over(xz, slot, height)) {
        const float y = height + g.wetGrid.y;
        static const float SAME = 1e-4; // heights within 0.1 mm: one level
        // Risen over the newest points: they are under water now. Where the remembered fall crossed this height (between the
        // last point kept above it and the first one dropped) stays: the water last stood at that height then, and above.
        if (n > 0u && h[n - 1u] < y - SAME) {
            uint kept = 0u;
            [unroll] for (uint i = 0; i < WET_POINTS; ++i) if (i < n && h[i] >= y - SAME) kept = i + 1u;
            float crossed = clock;
            [unroll] for (uint i = 0; i + 1u < WET_POINTS; ++i)
                if (kept > 0u && i == kept - 1u) crossed = t[i] + (h[i] - y) / max(h[i] - h[i + 1u], 1e-6) * (t[i + 1u] - t[i]);
            n = kept;
            if (n > 0u && h[n - 1u] > y + SAME) {
                [unroll] for (uint i = 0; i < WET_POINTS; ++i) if (i == n) { h[i] = y; t[i] = crossed; }
                n = min(n + 1u, WET_POINTS);
            }
        }
        // Steady at the newest point's level after it came there: the newer of the two at that level moves on to now.
        if (n >= 2u && abs(h[n - 1u] - y) <= SAME && abs(h[n - 2u] - y) <= SAME) --n;
        // Still falling the way it fell to the newest point: that point lies on the line to the new one and goes. Within a quarter
        // second and a tenth of its age: the drying's own times are seconds (a film) to hours (sand), so no drying reads the
        // difference.
        if (n >= 2u && h[n - 1u] > y + SAME) {
            const float span = h[n - 2u] - y;
            const float when = t[n - 2u] + (h[n - 2u] - h[n - 1u]) / max(span, 1e-6) * (clock - t[n - 2u]);
            if (abs(when - t[n - 1u]) <= 0.25 + 0.1 * max(clock - t[n - 1u], 0.0)) --n;
        }
        // Full: the point between two others that their line misses by the least time goes (the oldest and highest, and the
        // newest, always stay: a wave's crest is not lost to the many points of its fall).
        if (n == WET_POINTS) {
            uint drop = 1u; float least = 1e30;
            [unroll] for (uint i = 1; i + 1u < WET_POINTS; ++i) {
                const float when = t[i - 1u] + (h[i - 1u] - h[i]) / max(h[i - 1u] - h[i + 1u], 1e-6) * (t[i + 1u] - t[i - 1u]);
                const float miss = abs(when - t[i]);
                if (miss < least) { least = miss; drop = i; }
            }
            [unroll] for (uint i = 1; i + 1u < WET_POINTS; ++i) if (i >= drop) { h[i] = h[i + 1u]; t[i] = t[i + 1u]; }
            --n;
        }
        [unroll] for (uint i = 0; i < WET_POINTS; ++i) if (i == n) { h[i] = y; t[i] = clock; }
        ++n;
    } else slot = 0xFFu;
    store.Store4(at, uint4(key, n, slot, asuint(clock)));
    store.Store4(at + 16, uint4(asuint(h[0]), asuint(t[0]), asuint(h[1]), asuint(t[1])));
    store.Store4(at + 32, uint4(asuint(h[2]), asuint(t[2]), asuint(h[3]), asuint(t[3])));
    store.Store4(at + 48, uint4(asuint(h[4]), asuint(t[4]), asuint(h[5]), asuint(t[5])));
}
