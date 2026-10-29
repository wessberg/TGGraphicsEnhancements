// SPDX-License-Identifier: MIT
// The sun at a point inside the water, through the waves (PT-81, PT-85: until it was asked for here, a path in the water got
// the sun only when it happened to leave straight at the sun's disc, about one path in a hundred thousand: single green dots
// that a filter turns into glowing balls, and a near-black sea in every other frame).
//   The light at the point comes through the places of the surface whose tilt bends the sun's ray onto it: one under gentle
// waves, several deep under ripples. They are found, and their light measured, with the method the caustics pass uses for the
// bed ("Real-Time Ray Traced Caustics", Ray Tracing Gems II, chapter 30: a grid of rays over the water, each cell's light
// spread over the area its corner rays land round), asked from the point:
//   - a ray from the point up along the sun's direction as still water bends it meets the waves: the middle of a patch;
//   - a grid of the surface's points round it (cells of half a texel of the finest wave band there); at each corner the game's
//     own wave (water_displace) and the sun's ray bent at it (exact Snell and Fresnel);
//   - each corner's bent ray is followed down to the plane through the point across the light. A cell's light (the sun through
//     its piece of the surface, less what the surface mirrors) lies on the triangle its corners land on, and the point takes it
//     where it lies in that triangle: rays that bunch make a small triangle and much light, rays that spread little. Several
//     places that light the point are several triangles over it; all count. The sum of the light is exact for any cell size
//     (every cell's light lands somewhere, once); the cell is how fine the light's pattern inside the water is.
//   - the sun is a disc: the point takes a triangle's light by how much of the disc's footprint round the point the triangle
//     covers (the disc through the surface: an ellipse; the overlap of a triangle and a circle is exact). That bounds what a
//     fold of the light can send: no sample is brighter than the real sun makes it.
//   - the patch grows until it holds all that its own corners say can reach the point (how far any corner's ray strays from
//     the middle's, and a margin); nothing of the surface's is assumed, and a wider patch costs time alone. Past SUN_GRID
//     cells a side the cells grow instead.
//   - one landed cell, chosen by its light, sends the shadow rays for all: the point to its place on the surface, and from
//     there to the sun; and it says how much of the surface there is foam (foam lets no sun through).
//   What it costs, and the one thing that is drawn by chance: a patch asks 15 texture reads a corner, and a point 2 m under
//   steep ripples needs some hundred corners. A patch of more than SUN_BUDGET corners is made with the chance SUN_BUDGET over
//   its corners and counts that much more (the mean is the same, the noise is the price).
//   - the water's own smoothness (water_smoothness): the surface's small facets tilt the bent ray a little more. One tilt
//     is drawn for the whole patch from the facets the sun can see (GGX's visible normals, about each corner's own wave
//     normal), every corner's ray is bent at its facet so tilted, and the patch is laid round the place that tilt points
//     to; many patches make the lobe. A smooth surface (0.95: a tilt of a sixth of a degree) lights as a clear one does.
// With the Caustics row off the row's "off" holds here too: the sun's straight ray through the surface, the water taking its
// share over the ray's way under water (water_straight); no patch.
//
//   THE CELL BY THE RAY'S WIDTH (PT-131, docs/agents/campfix.md; the rule since 2026-10-12, for the patch made at a point).
// The patch's sum is the light whatever its cell, and the cell is how fine the light's pattern is. How fine a pattern a
// path can show is the width of its own ray at the point (the ray cone the tracer reads its textures by, PT-46: a pixel's
// width seen straight or in a mirror; metres after a rough reflection, a bounce or a scatter point). So a patch's cell is
// no finer than half that width (and as before never more than half the way to the surface): a texture's mip, for the
// waves. A bounce ray's scatter point 100 m off took a patch of up to 33 x 33 corners, made anew at every turn of its
// growing (the camp of PT-131: 95 million corners a frame for light nobody can see a pattern in); it takes 9 to 25.
//   A cell coarser than the waves is summed AS EVERY OTHER: a triangle takes the sun by the area of its three moved
// corners across the light. That area is exactly the sun through the piece of water between those corners, whatever the
// waves do inside it (their slopes, and how their sideways move squeezes the water at a crest), so each triangle keeps
// its own light and a coarser cell only blurs the pattern: on the GPU the coarsest cells there are (half the way to the
// surface) gave the sun's share of the sea fixture's picture within 0.05 % of the fine cells', whole and band by band
// (docs/agents/campfix.md, "The offline check").
//   (TRIED AND TAKEN OUT, 2026-10-12: "each corner its own facet's sun". For a facet turned from a low sun, a coarser
// cell's corner carried the sun its own facet takes, and the triangle its level area. It left out how the sideways move
// squeezes the water, moved the sun's share by 0.6 to 1.1 % in that fixture and changed the pattern; on a steep sea its
// factor has no bound. What it was for is still open: a coarse triangle with a corner turned from the sun is left out
// (below: `landed.w < 0`), so a very low sun over a steep sea comes out darker with coarse cells than with fine ones.
// The cure that fits is to keep such a triangle with its own area across the light; not built, campfix.md.)
//
//   (TRIED AND TAKEN OUT THE SAME DAY, 2026-10-12: "the shadow first". The patch's two shadow rays come last, so water
// the sun cannot reach pays for its patch and then gets nothing. A point that made its own patch first sent one small ray
// from the place over it to the sun against the ground alone, and where the ground was in the way made its patch one
// time in four, counted four times: the same mean. In the game's two views of water it gained nothing (run
// 20261012-041726: the tracer 72.6 ms without it, 71.2 to 75.5 with it), and what gains nothing in the game comes out.
// It is worth it only for water seen in a land's shade from more than the kept corners' reach: docs/agents/campfix.md
// has the lines and the numbers.)
//
//   THE CORNERS KEPT, IN TILES (docs/agents/water-8.md; the way the sea, a lake and a pool are asked). Why: a GPU runs 32
// pixels as one group that loops until its slowest pixel is done, and every turn of the loop costs the group what its
// costliest pixel does in it. The patch that grows cost two thirds of its time in the patches over the budget that one
// pixel in a few goes on to make. So here every pixel does the same bounded work, none of it found by growing:
//   - A corner's wave point and normal depend on the slot, the place on the level surface and the camera alone, not on
//     the point that asks, and the light's sum is exact for any cutting of the surface into triangles. A slot's corners
//     are made once a frame (pt_sun_grid.hlsl: SUN_CORNERS a side, on the slot's level plane round the camera, laid along
//     the sun's bearing and across it in the cell a patch wants, shifted by a random part of a cell every frame, as the
//     caustics' grid is: the frames' mean is the mean over every cutting). Two loads a corner in place of 15 texture reads.
//   - The corners stand in tiles of SUN_TILE cells a side, and each tile has a record, made once a frame too: the box of
//     its corners' moves off their grid places, and the box of the rays still water's facets would bend there, both in
//     the frame of still water's own bent ray (sun_frame), with the least cosine any wave's normal makes with the sun.
//     The tiles' records are summed into one for the whole grid.
//   - A point asks the whole grid's record which tiles any ray could reach it through (a block of tiles: every corner's
//     landing place on the point's plane is its own place plus its ray's reach times the ray's slant, and the record
//     bounds all three), then asks each tile of the block the same of its own record. A tile whose corners' landing
//     places all lie to one side of the sun's footprint round the point holds no triangle that covers it. The test only
//     ever keeps too much: the tilt the patch's facets add to each ray is bounded for every normal (sun_tilt_slack: a
//     facet stands off its wave's normal by at most an angle, and a ray bent at it turns by a known share of that).
//   - The tiles that are left are summed whole: every triangle of every cell, as before. More than SUN_PICK of them: that
//     many are drawn evenly by chance and count for the rest (the mean is the same; where a patch of 400 corners was made
//     by 1 chance in 8 and counted 8 times, 4 tiles of 8 are, counted twice). A block of more than SUN_TESTS tiles: every
//     2nd, 3rd, ... is asked, counted for the others. Nothing grows: what the growing patch found by how far its own
//     corners' rays strayed, the records say for every corner there is.
//   - A corner that stands behind the point's plane (the plane lies across the bent ray, 35 degrees off level under a
//     sun 40 degrees up: a corner one cell down the light's bearing stands 5 cm deeper along the ray) is followed BACK to
//     the plane along its ray's own line, its reach under 0: that is where the triangle's light lies on that plane. The
//     patch made at the point gives such a corner up, and so needs cells of half the point's way to the surface; the
//     tiles serve a point 2 mm under the waves as one 2 m under them, from the same cells (the waves' own: half a texel
//     of their finest band).
//   - Made at the point as before (the patch above: the same light): a point whose block leaves the grid, a tilt too
//     wide to bound, a surface that is not one level plane (a river's bent mesh), a slot without a grid.
//   The patch made at the point is the code it was (not a line of it changed for the tiles); a tile's corner is the
//   same corner with less work (sun_bend: the facet straight from GGX's half vector, what the surface lets through
//   worked out for the triangles that land round the point alone).
#ifndef PT_WATER_SUN_HLSLI
#define PT_WATER_SUN_HLSLI

static const uint SUN_GRID = 32;          // cells a patch's side at most (then the cells grow)
static const float SUN_BUDGET = 48.0;     // corners a patch is always made with; a larger one by chance, counted for more
static const uint SUN_RECENTRE = 2;       // steps the patch's middle takes towards a place whose ray lands on the point
static const float SUN_MARGIN = 1.15;     // the patch holds this much more than its corners' rays were seen to stray
static const uint SUN_CORNERS = 1024;     // corners a side of a slot's grid (tracer.cpp kSunCorners)
static const uint SUN_CORNER_BYTES = 24;  // a kept corner: the wave's point and its normal (tracer.cpp kSunCornerBytes)
static const uint SUN_TILE = 4;           // cells a side of a tile (its corners: SUN_TILE + 1 a side, the far ones its neighbours' too)
static const uint SUN_TILES = SUN_CORNERS / SUN_TILE; // tiles a side kept (the last has no far corners: never asked)
static const uint SUN_RECORD_BYTES = 64;  // a tile's record (SunRecord; tracer.cpp kSunRecordBytes)
static const uint SUN_PICK = 4;           // tiles summed at most; more are left: this many by chance, counted for the rest
static const uint SUN_PICK_MOST = 8;      // (the most a test may ask for: TGPT_TEST_SUN_PICK)
static const uint SUN_TESTS = 256;        // tiles a point asks at most; a wider block: every 2nd, 3rd, ... counted for the others
// A slot's part of the buffer (g.waterGeometry.z, from the slot's first byte): its corners, its tiles' records, a record a
// row of tiles, the whole grid's record (tracer.cpp kSunSlotBytes).
static const uint SUN_TILES_AT = SUN_CORNERS * SUN_CORNERS * SUN_CORNER_BYTES;
static const uint SUN_ROWS_AT = SUN_TILES_AT + SUN_TILES * SUN_TILES * SUN_RECORD_BYTES;
static const uint SUN_WHOLE_AT = SUN_ROWS_AT + SUN_TILES * SUN_RECORD_BYTES;
// A slot's kept corners: g.sunGrid[2 slot] its two steps on the level plane (x, z of each; all 0: the slot has none),
// g.sunGrid[2 slot + 1] corner (0, 0)'s x and z, the plane's height, and the slot's first byte in the buffer (a uint's bits).
// g.waterGeometry.z: the buffer (raw UAV; INVALID: none this frame); w: a test's two numbers: in its low byte the count
// of tiles to sum (0: SUN_PICK), in bits 8 to 30 the width every path's ray is taken to have, centimetres (0: the ray's
// own).
bool sun_grid_slot(uint slot) { return g.waterGeometry.z != INVALID && any(g.sunGrid[slot * 2u] != 0.0); }
// The cell by the ray's width (the top of this file) is the rule in every build for players, with no switch (the game:
// the beach's tracer 121 ms without it, 73 with it, run 20261012-041726; the same light, 20261012-101445; the mean
// checked offline, docs/agents/campfix.md). Only a measuring build can turn it off (its third page's 8192): a patch's
// cell by the waves alone, as before 2026-10-12. (Until 2026-10-12 10:30 the hidden "Speed trial" 64 did so too.)
#ifdef PT_SPEED_MEASURE
bool sun_cell_by_width() { return !speed_measure3(SPEED_M3_SUN_FOOTPRINT); }
#else
bool sun_cell_by_width() { return true; }
#endif

// The frame of still water's bent ray over a level surface: t00 the ray, a0 across it in the plane the sun's ray bends in,
// b0 across both. The tiles' records are kept in it (pt_sun_grid.hlsl) and read in it (water_sun).
void sun_frame(out float3 a0, out float3 b0, out float3 t00) {
    const float3 l = g.sunDir.xyz;
    t00 = normalize(refract(-l, float3(0.0, 1.0, 0.0), 1.0 / WATER_IOR));
    a0 = l - t00 * dot(l, t00);
    const float aLength = length(a0);
    if (aLength > 1e-4) a0 /= aLength; else { float3 other; basis(t00, a0, other); }
    b0 = cross(t00, a0);
}
// A tile's record, in sun_frame's frame (a0, b0, t00): the box of its corners' moves off their grid places (the waves'
// own move), the box of the rays still water's facets bend there (unit vectors), and the least cosine a corner's normal
// makes with the sun. Of no corner: the boxes inside out (it passes no test and widens no sum).
struct SunRecord { float3 moveLo; float least; float3 moveHi; float3 rayLo; float3 rayHi; };
SunRecord sun_record_none() {
    SunRecord r; r.moveLo = 1e30; r.moveHi = -1e30; r.rayLo = 1e30; r.rayHi = -1e30; r.least = 1e30;
    return r;
}
SunRecord sun_record_load(RWByteAddressBuffer kept, uint address) {
    SunRecord r;
    const float4 q0 = asfloat(kept.Load4(address)), q1 = asfloat(kept.Load4(address + 16u));
    const float4 q2 = asfloat(kept.Load4(address + 32u)), q3 = asfloat(kept.Load4(address + 48u));
    r.moveLo = q0.xyz; r.least = q0.w; r.moveHi = q1.xyz; r.rayLo = q2.xyz; r.rayHi = q3.xyz;
    return r;
}
void sun_record_store(RWByteAddressBuffer kept, uint address, SunRecord r) {
    kept.Store4(address, asuint(float4(r.moveLo, r.least))); kept.Store4(address + 16u, asuint(float4(r.moveHi, 0.0)));
    kept.Store4(address + 32u, asuint(float4(r.rayLo, 0.0))); kept.Store4(address + 48u, asuint(float4(r.rayHi, 0.0)));
}
SunRecord sun_record_join(SunRecord x, SunRecord y) {
    SunRecord r;
    r.moveLo = min(x.moveLo, y.moveLo); r.moveHi = max(x.moveHi, y.moveHi);
    r.rayLo = min(x.rayLo, y.rayLo); r.rayHi = max(x.rayHi, y.rayHi); r.least = min(x.least, y.least);
    return r;
}
// The least and the most of dot(x, v) over the box lo..hi.
void sun_span(float3 lo, float3 hi, float3 v, out float least, out float most) {
    const float3 x = lo * v, y = hi * v;
    least = dot(min(x, y), 1.0); most = dot(max(x, y), 1.0);
}
// How far at most the ray bent at the facet a patch's tilt gives a wave's normal stands off the ray still water's facet
// (the normal itself) bends there, for every normal the sun meets at a cosine of `least` or more. Two steps:
//   - the facet stands off its normal by an angle whose tangent is at most alpha sqrt(1 - z z) / z (ggx_sample's own
//     steps, bounded: the stretched view stands at a cosine of viewCos or more over the normal, the half vector no lower
//     than z, and unstretched its slant is alpha times that one's);
//   - a ray bent into the denser side turns by rate(c) times what its facet turns, c the sun's cosine with the facet:
//     sqrt((e - e e c / ct)^2 (1 - c c) + (ct - e c)^2), e the indices' ratio, ct the bent ray's cosine; it falls as c
//     rises (a quarter head on, 1 grazing), so the least cosine any facet can have bounds it.
// 1e9: no bound (the patch is made at its point). Tried against 4,000 suns, tilts and sets of normals on the CPU
// (docs/agents/water-8.md): the ray's turn is 0.9998 of this at most.
float sun_tilt_slack(float alpha, float2 u, float least) {
    const float c = clamp(least, 1e-4, 1.0);
    const float viewCos = c / sqrt(alpha * alpha * (1.0 - c * c) + c * c), viewSin = sqrt(max(0.0, 1.0 - viewCos * viewCos));
    const float r = sqrt(u.x), phi = 2.0 * PI * u.y;
    const float p1 = r * cos(phi), p2 = r * sin(phi);
    const float mixLeast = 0.5 * (1.0 + viewCos);
    const float q = (1.0 - mixLeast) * sqrt(max(0.0, 1.0 - p1 * p1)) + mixLeast * p2;
    const float lo = min(p2, q), hi = max(p2, q);
    const float p3 = sqrt(max(0.0, 1.0 - p1 * p1 - max(lo * lo, hi * hi)));
    const float z = p3 * viewCos - max(0.0, -lo) * viewSin;
    if (!(z > 1e-3)) return 1e9;
    const float facet = alpha * sqrt(max(0.0, 1.0 - z * z)) / z;
    const float cosLow = max(0.0, c * (1.0 - 0.5 * facet * facet) - sqrt(max(0.0, 1.0 - c * c)) * facet);
    const float e = 1.0 / WATER_IOR, bent = sqrt(1.0 - e * e * (1.0 - cosLow * cosLow));
    const float rate = sqrt((e - e * e * cosLow / bent) * (e - e * e * cosLow / bent) * (1.0 - cosLow * cosLow) + (bent - e * cosLow) * (bent - e * cosLow));
    return rate * facet;
}

// The wave surface over a point inside the water along a direction: its slot, the undisplaced point there, the level
// surface's normal (outward) and the distance. False where the ray meets no water from below.
bool water_above(float3 origin, float3 dir, out uint slot, out float3 base, out float3 level, out float t) {
    slot = 0u; base = origin; level = float3(0.0, 1.0, 0.0); t = 0.0;
    if (g.waterGeometry.y == 0u) return false;
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = 0.0; ray.TMax = 1e30;
    PT_RAYQUERY_WAVES(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_WAVES, ray);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT || q.CommittedTriangleFrontFace()) return false;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const InstanceRecord inst = instances[q.CommittedInstanceID()];
    const MeshRecord mesh = meshes[inst.mesh];
    const uint3 idx = triangle_indices(mesh, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex());
    ByteAddressBuffer vb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    const uint baseAt = mesh.vertexCount * 12u;
    const float2 bary = q.CommittedTriangleBarycentrics();
    const float3 b0 = asfloat(vb.Load3(baseAt + idx.x * 12)), b1 = asfloat(vb.Load3(baseAt + idx.y * 12)), b2 = asfloat(vb.Load3(baseAt + idx.z * 12));
    slot = (inst.flags >> 8) & 3u;
    base = b0 * (1.0 - bary.x - bary.y) + b1 * bary.x + b2 * bary.y;
    const float3 n = cross(b1 - b0, b2 - b0);
    const float len = length(n);
    level = len > 1e-20 ? n / len : water_row(slot, 10).xyz;
    t = q.CommittedRayT();
    return true;
}

// The area of a triangle inside the unit circle round the origin, exact: edge by edge, the part of the circle's sector the
// edge cuts off (a chord's triangle between its two crossings of the circle, the sector's slices outside them).
float sun_sector(float2 u, float2 v) {
    const float s = u.x * v.y - u.y * v.x, c = dot(u, v);
    return s == 0.0 && !(c > 0.0) ? 0.0 : 0.5 * atan2(s, c);
}
float sun_edge(float2 a, float2 b) {
    const float2 d = b - a;
    const float along = dot(d, d);
    if (!(along > 1e-30)) return 0.0;
    // The line's nearest point to the middle and half its chord, in the edge's own measure (0 at a, 1 at b).
    const float away = a.x * d.y - a.y * d.x;
    const float chord = along - away * away;
    if (!(chord > 0.0)) return sun_sector(a, b);
    const float middle = -dot(a, d) / along, reach = sqrt(chord) / along;
    const float t0 = middle - reach, t1 = middle + reach;
    if (!(t1 > 0.0) || !(t0 < 1.0)) return sun_sector(a, b);
    const float2 pa = a + d * saturate(t0), pb = a + d * saturate(t1);
    float area = 0.5 * (pa.x * pb.y - pa.y * pb.x);
    if (t0 > 0.0) area += sun_sector(a, pa);
    if (t1 < 1.0) area += sun_sector(pb, b);
    return area;
}
float disc_triangle(float2 a, float2 b, float2 c) { return abs(sun_edge(a, b) + sun_edge(b, c) + sun_edge(c, a)); }

// The shadow rays of the sun's question: one (the Caustics row off: the straight ray from the point, the water taking its share
// under way: inside is the point's slot) or two (the point to its place on the surface, then that place to the sun; such rays
// do not stop at the waves they start or end on, the caustics' own convention, and the water's share is the patch's to take:
// inside -1). What they let through.
float3 sun_rays(float3 from0, float3 dir0, float min0, float max0, float3 from1, float3 dir1, float max1, uint rays, bool pass, int inside) {
    const bool savedKnown = g_waterMediumKnown, savedPass = g_passWater; const int savedMedium = g_waterMedium;
    g_passWater = pass; g_waterMediumKnown = true; g_waterMedium = inside;
    float3 through = 1.0;
    for (uint k = 0; k < rays && any(through > 0.0); ++k) {
        const float end = k == 0u ? max0 : max1;
        if (end > 0.0) through *= transmittance(k == 0u ? from0 : from1, k == 0u ? dir0 : dir1, k == 0u ? min0 : 0.0, end);
    }
    g_passWater = savedPass; g_waterMediumKnown = savedKnown; g_waterMedium = savedMedium;
    return through;
}

// A corner of the patch: its point on the waves and how far its bent ray goes to the point's plane (xyz, w), where that ray
// lands on the plane (xy, the point at 0), the ray's share along the plane's own direction (z) and what the surface lets
// through there (w; under 0: no ray of the sun's reaches the plane from this corner).
struct SunCorner { float4 place; float4 landed; };

// A tile's corner (the patch made at the point has SunCorner above: the same corner, the same numbers): its point on
// the waves and how far its bent ray goes to the point's plane (xyz, w), where that ray lands on the plane (xy, the point
// at 0), the ray's share along the plane's own direction (z; w under 0: no ray of the sun's reaches the plane from this
// corner), and the two cosines that say what the surface lets through there (sun_through): the sun's ray against the
// facet it is bent at, the wave's normal against the bent ray.
struct SunBent { float4 place; float4 landed; float2 cosines; };
// What a point asks of every corner of its tiles alike: itself, the sun, its plane (its own bent ray t0, a and b across
// it) and the tilt of the surface's facets: GGX's alpha and where in its lobe (tiltAt: the pair ggx_sample turns its
// two numbers into, a point of the unit disc).
struct SunAsk { float3 p; float3 l; float3 t0; float3 a; float3 b; float alpha; float2 tiltAt; };
// The facet the tilt gives a wave's normal: ggx_sample's own half vector (GGX's visible normals, Heitz 2018), which is
// normalize(v + ggx_sample(n, v, alpha, u)) without the way there and back.
float3 sun_facet(float3 n, float3 v, float alpha, float2 at) {
    float3 t, b; basis(n, t, b);
    const float3 vl = float3(dot(v, t), dot(v, b), dot(v, n));
    const float3 vh = normalize(float3(alpha * vl.x, alpha * vl.y, vl.z));
    const float lensq = vh.x * vh.x + vh.y * vh.y;
    const float3 t1 = lensq > 0.0 ? float3(-vh.y, vh.x, 0.0) * rsqrt(lensq) : float3(1.0, 0.0, 0.0);
    const float3 t2 = cross(vh, t1);
    const float s = 0.5 * (1.0 + vh.z);
    const float p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - at.x * at.x)) + s * at.y;
    const float3 nh = at.x * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - at.x * at.x - p2 * p2)) * vh;
    const float3 hl = normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));
    return hl.x * t + hl.y * b + hl.z * n;
}
// The corner at a wave's point with the wave's normal there: the sun's ray bent at the facet the point's tilt gives that
// normal (Snell's law, as pt_interface's boundary bends it), followed to the point's plane: down to it, or, from a
// corner that stands behind that plane, back to it (the reach under 0).
SunBent sun_bend(SunAsk q, float3 position, float3 normal) {
    SunBent c; c.place = 0.0; c.landed = float4(0.0, 0.0, 0.0, -1.0); c.cosines = 0.0;
    const float3 facet = sun_facet(normal, q.l, q.alpha, q.tiltAt);
    const float cosIn = dot(facet, q.l);
    const float3 t = refract(-q.l, facet, 1.0 / WATER_IOR);
    const float3 flat = position - q.p;
    const float along = dot(t, q.t0);
    const float reach = -dot(flat, q.t0) / max(along, 0.05);
    if (dot(normal, q.l) > 1e-4 && cosIn > 0.0 && along > 0.05 && all(isfinite(position)) && all(isfinite(t))) {
        const float3 land = flat + t * reach;
        c.place = float4(position, reach);
        c.landed = float4(dot(land, q.a), dot(land, q.b), along, 1.0);
        c.cosines = float2(cosIn, abs(dot(normal, t)));
    }
    return c;
}
// What the surface lets through at a corner: what the facet does not mirror (exact Fresnel, as pt_interface's boundary),
// and what the facets on the way in do not hide of the bent ray (Smith's).
float sun_through(float2 cosines, float alpha2) { return (1.0 - fresnel_dielectric(cosines.x, WATER_IOR)) * smith_g1(cosines.y, alpha2); }
// The sun's light a tile's triangle sends the point (per unit of the sun's, before the water takes its share over
// `reach`, the corners' mean way down): the sun through the surface's triangle, spread over the triangle its corners'
// rays land on, by how much of the sun's footprint round the point that triangle covers. The patch's own sums, line for
// line (a mean reach under 0, the point a hair over the triangle: the footprint's least size, and the water takes
// nothing). 0: none.
float sun_triangle(SunBent c00, SunBent cb, SunBent cc, float2 footprint, float3 l, float alpha2, out float reach) {
    reach = 0.0;
    if (c00.landed.w < 0.0 || cb.landed.w < 0.0 || cc.landed.w < 0.0) return 0.0;
    reach = (c00.place.w + cb.place.w + cc.place.w) / 3.0;
    const float2 size = max(footprint * reach, 1e-4);
    // (The patch asks this of the landing places in the footprint's own measure; here before they are put into it: the
    // same test, and most triangles end at it.)
    const float2 lo = min(c00.landed.xy, min(cb.landed.xy, cc.landed.xy)), hi = max(c00.landed.xy, max(cb.landed.xy, cc.landed.xy));
    if (lo.x > size.x || lo.y > size.y || hi.x < -size.x || hi.y < -size.y) return 0.0;
    const float2 ya = c00.landed.xy / size, yb = cb.landed.xy / size, yc = cc.landed.xy / size;
    const float area = 0.5 * abs((yb.x - ya.x) * (yc.y - ya.y) - (yb.y - ya.y) * (yc.x - ya.x));
    if (!(area > 1e-12)) return 0.0;
    const float covered = min(disc_triangle(ya, yb, yc), area);
    if (!(covered > 0.0)) return 0.0;
    // The sun through the surface's triangle: its area across the light (the grid's two steps turn so that this cross
    // product points down into the water).
    const float across = max(0.0, -0.5 * dot(cross(cb.place.xyz - c00.place.xyz, cc.place.xyz - c00.place.xyz), l));
    const float through = (sun_through(c00.cosines, alpha2) + sun_through(cb.cosines, alpha2) + sun_through(cc.cosines, alpha2)) / 3.0;
    const float straight = (c00.landed.z + cb.landed.z + cc.landed.z) / 3.0;
    const float light = across * through * (covered / area) / (PI * size.x * size.y * straight);
    return light > 0.0 && isfinite(light) ? light : 0.0;
}

// The sun's light inside the water at p (relative to the scene origin), in the slot `medium`'s water: what a patch of
// surface across its direction takes there (the sum over every way through the waves), times what the water, the surface,
// the foam and whatever stands in the way take. Per unit of g.sunColor it is 1 in the open air.
// width: how wide the path's own ray is at p, metres (pt_water_path.hlsli carries it): the cell by the ray's width.
float3 water_sun(uint medium, float3 p, float width, inout Rng rng) {
    if (!(g.sunColor.w > 0.0)) return 0.0;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure2): no sun in the water's body
    if (speed_measure2(SPEED_M2_NO_SUN) || speed_measure3(SPEED_M3_NO_SUN)) return 0.0;
    if (water_passes_light()) ++g_scSunStraight; else ++g_scSunAsked; // (the counters: pt_common speed_count_flush)
#endif
    const float3 l = g.sunDir.xyz;
    if (water_passes_light()) return g.sunColor.rgb * sun_rays(p, cone_sample(l, g.sunDir.w, rng.next2()), water_epsilon(p), g.params.y, p, l, 0.0, 1u, false, int(medium)) * sun_fog(p);
    const float3 sigma = max(water_row(medium, 14).xyz, 0.0);
    // The middle of the patch: where still water's bent ray from the point meets the waves.
    float3 up = water_row(medium, 10).xyz;
    if (!(dot(up, l) > 1e-3)) return 0.0;
    // The tilt of the surface's small facets, for the whole patch (two numbers: where in GGX's lobe).
    const float alpha = reflection_alpha(1.0 - water_smoothness(medium, p));
    const float2 tilt = rng.next2();
    float3 t0 = normalize(refract(-l, normalize(l + ggx_sample(up, l, alpha, tilt)), 1.0 / WATER_IOR));
    if (!(dot(t0, up) < -0.05)) return 0.0; // a facet tilted so far that its ray does not go down: nothing of it reaches the point
    uint slot; float3 base0, level; float s0;
    if (!water_above(p, -t0, slot, base0, level, s0) || !(s0 > 0.0)) return 0.0;
    up = level;
    if (!(dot(up, l) > 1e-3)) return 0.0;
    const float3 facet0 = normalize(l + ggx_sample(up, l, alpha, tilt));
    const float cosIn = dot(facet0, l);
    t0 = normalize(refract(-l, facet0, 1.0 / WATER_IOR));
    const float cosOut = -dot(facet0, t0);
    if (!(dot(t0, up) < -0.05) || !(cosIn > 1e-3) || !(cosOut > 1e-3)) return 0.0;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): the points that go on to the tiles or a patch
    ++g_scSunAbove; g_scSunWay += uint(min(s0, 1000.0));
    // Why a patch is made at the point: 1 the slot keeps no corners, 2 the surface here is not their level plane, 3 their
    // cell is coarser than the waves want, 4 the tilt has no bound, 5 the block's size has no solution, 6 the block leaves
    // the kept corners, 7 it holds too many tiles.
    uint speedWhy = 1u, speedCorners = 0u;
#endif
    // The plane through the point across the light: a in the plane the sun's ray bends in, b across it. A step on that
    // plane is a step on the level surface, slid along t0 (step_x, step_y).
    float3 a = l - t0 * dot(l, t0);
    const float aLength = length(a);
    if (aLength > 1e-4) a /= aLength; else { float3 other; basis(t0, a, other); }
    const float3 b = cross(t0, a);
    const float3 stepX = a - t0 * (dot(a, up) / dot(t0, up)), stepY = b - t0 * (dot(b, up) / dot(t0, up));
    // The sun's disc through the surface, per metre of the bent ray: its footprint's two half-axes on the plane.
    const float sunSine = sqrt(max(1.0 - g.sunDir.w * g.sunDir.w, 0.0));
    const float2 footprint = float2(sunSine * cosIn / (WATER_IOR * cosOut), sunSine / WATER_IOR);
    // The cell wanted: half a texel of the finest wave band that moves the surface here (HDRP reads its gradient smoothly
    // over a texel's neighbours: waves of four texels and more are followed within a tenth), and never more than half the
    // way to the surface (a point just under it has a patch of its own size).
    float cellWanted = 1e30;
    {
        const float4 info = water_row(slot, 12);
        const float distance = length(base0 - g.cameraPos.xyz);
        if (g.waterTextures[slot] != INVALID && g.waterNormals[slot] != INVALID) {
            for (uint band = 0; band < uint(info.x); ++band) {
                const float4 e = water_row(slot, band * 3 + 2);
                const float scale = length(water_row(slot, band * 3).xyz);
                if (saturate(distance * e.y + e.z) > 0.0 && scale > 0.0 && info.y > 0.0) cellWanted = min(cellWanted, 0.5 / (scale * info.y));
            }
        }
    }
    const float cellOfWaves = cellWanted; // (the tiles are asked by this one: a corner behind the point's plane is no loss to them)
    cellWanted = min(cellWanted, max(0.5 * s0, 1e-4));
    // The cell by the ray's width (the top of this file), for a patch made at the point: no finer than half the width of
    // the path's own ray here, and as before no more than half the way to the surface. coarse: the cell is then coarser
    // than the waves' own (a measuring build counts those). The kept tiles are asked as before.
    // (A test's: every path's ray taken as so many centimetres wide, TGPT_TEST_SUN_WIDTH: tracer.cpp.)
    bool coarse = false;
    if (sun_cell_by_width()) {
        const uint testWidth = (g.waterGeometry.w >> 8) & 0x7FFFFFu;
        const float wide = testWidth != 0u ? float(testWidth) * 0.01 : width;
        const float byWidth = min(0.5 * wide, max(0.5 * s0, 1e-4));
        if (byWidth > cellWanted) { cellWanted = byWidth; coarse = true; }
    }
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): the ray's width at the points that ask
    g_scSunWidth += uint(min(width * 10.0, 10000.0));
    if (width < 0.1) ++g_scSunNarrow;
#endif
    const float2 jitter = rng.next2();
    const float chance = rng.next();
    float kept = 1.0;          // the chance this patch is made with (the tiles: the share of the tiles left that is summed)
    float3 sum = 0.0; float weightSum = 0.0; float3 chosenPlace = p, chosenBase = base0;
    bool made = false;

    // ---- From the slot's kept corners, in tiles (the top of this file).
    if (sun_grid_slot(slot)) {
        SunAsk ask; ask.p = p; ask.l = l; ask.t0 = t0; ask.a = a; ask.b = b; ask.alpha = alpha;
        ask.tiltAt = sqrt(tilt.x) * float2(cos(2.0 * PI * tilt.y), sin(2.0 * PI * tilt.y));
        const float4 spans = g.sunGrid[slot * 2u], first = g.sunGrid[slot * 2u + 1u];
        const float lift = base0.y - first.z; // the surface's height here over the grid's own (a level plane: none)
        const float3 stepU = float3(spans.x, 0.0, spans.y), stepV = float3(spans.z, 0.0, spans.w);
        const float gridCell = max(length(float2(dot(stepU, a), dot(stepU, b))), length(float2(dot(stepV, a), dot(stepV, b))));
        RWByteAddressBuffer keptCorners = ResourceDescriptorHeap[g.waterGeometry.z];
        const uint slotAt = asuint(first.w);
        float3 a0, b0, t00; sun_frame(a0, b0, t00);
        const SunRecord whole = sun_record_load(keptCorners, slotAt + SUN_WHOLE_AT);
        // How far the patch's tilt turns any ray from still water's ray at the same corner (each of its three numbers).
        const float slack = sun_tilt_slack(alpha, tilt, whole.least);
        // The patch in the records' frame, the point at its origin: the point's own bent ray, corner (0, 0)'s place and
        // the grid's two steps, where the ray from the point meets the surface, and how the level surface lies in it.
        const float3 rayOwn = float3(dot(t0, a0), dot(t0, b0), dot(t0, t00));
        const float3 from = float3(first.x, base0.y, first.y) - p;
        const float3 firstAt = float3(dot(from, a0), dot(from, b0), dot(from, t00));
        const float3 stepI = float3(dot(stepU, a0), dot(stepU, b0), dot(stepU, t00)), stepJ = float3(dot(stepV, a0), dot(stepV, b0), dot(stepV, t00));
        const float3 over = base0 - p;
        const float2 overAt = float2(dot(over, a0), dot(over, b0));
        const float3 upAt = float3(dot(up, a0), dot(up, b0), dot(up, t00));
        const float turned = stepI.x * stepJ.y - stepI.y * stepJ.x;
        const float fattest = max(footprint.x, footprint.y);
        float alongLo, alongHi; sun_span(whole.rayLo - slack, whole.rayHi + slack, rayOwn, alongLo, alongHi);
        alongLo = max(alongLo, 0.05);
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): which of the tests below sends the point to its own patch
        speedWhy = !(up.y > 0.99999 && abs(lift) < 2e-3) ? 2u : !(gridCell <= cellOfWaves) ? 3u : 4u;
#endif
        if (up.y > 0.99999 && abs(lift) < 2e-3 && gridCell <= cellOfWaves && slack < 0.1 && alongHi > 0.05 && abs(turned) > 1e-12 && abs(upAt.z) > 0.05) {
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
            speedWhy = 5u;
#endif
            // The block of tiles to ask. A corner's ray reaches the point's plane after h / along, h how far the corner
            // stands over that plane (under 0: behind it): known at base0 (the level surface's point under the wave the
            // point's own ray met), and from there it changes with the corner's place on the level surface (tiltA, tiltB:
            // a step along a0 and b0) and with the waves' move. The block's own size is in that reach, so the reach's
            // bound is solved for (closed): reachMost, the most a reach can be either way; reachLeast, the least (0, or
            // under 0 where a corner of the block can stand behind the plane).
            float moveLo, moveHi; sun_span(whole.moveLo, whole.moveHi, rayOwn, moveLo, moveHi);
            const float tiltA = abs(rayOwn.x - rayOwn.z * upAt.x / upAt.z), tiltB = abs(rayOwn.y - rayOwn.z * upAt.y / upAt.z);
            const float3 rayLo = whole.rayLo - slack, rayHi = whole.rayHi + slack;
            const float2 slant = max(max(rayHi.xy, -rayLo.xy), 0.0) + fattest;
            const float2 fixedPart = 2e-3 + max(abs(whole.moveLo.xy), abs(whole.moveHi.xy)) + abs(overAt);
            const float grows = (tiltA * slant.x + tiltB * slant.y) / alongLo;
            if (grows < 0.5) {
                const float overOwn = -dot(over, t0);
                const float reachMost = (abs(overOwn) + max(abs(moveLo), abs(moveHi)) + tiltA * fixedPart.x + tiltB * fixedPart.y) / ((1.0 - grows) * alongLo);
                const float wide = fattest * reachMost + 2e-3;
                const float overLeast = overOwn - moveHi - tiltA * (reachMost * slant.x + fixedPart.x) - tiltB * (reachMost * slant.y + fixedPart.y);
                const float reachLeast = min(overLeast / alongLo, 0.0);
                // The grid places (in the frame, the point at 0) a corner that reaches the point can have: its landing
                // place less its ray's slant times its reach, less its own move.
                const float2 slantLo = min(min(reachLeast * rayLo.xy, reachLeast * rayHi.xy), min(reachMost * rayLo.xy, reachMost * rayHi.xy));
                const float2 slantHi = max(max(reachLeast * rayLo.xy, reachLeast * rayHi.xy), max(reachMost * rayLo.xy, reachMost * rayHi.xy));
                const float2 placeLo = -slantHi - wide - whole.moveHi.xy - firstAt.xy;
                const float2 placeHi = -slantLo + wide - whole.moveLo.xy - firstAt.xy;
                // ... as counts of corners along the grid's two steps, and the tiles that hold any of those corners.
                const float2 rowI = float2(stepJ.y, -stepJ.x) / turned, rowJ = float2(-stepI.y, stepI.x) / turned;
                const float2 iEnds = float2(dot(min(placeLo * rowI, placeHi * rowI), 1.0), dot(max(placeLo * rowI, placeHi * rowI), 1.0));
                const float2 jEnds = float2(dot(min(placeLo * rowJ, placeHi * rowJ), 1.0), dot(max(placeLo * rowJ, placeHi * rowJ), 1.0));
                const float2 tileLo = floor((floor(float2(iEnds.x, jEnds.x) - 1e-3) - 1.0) / float(SUN_TILE));
                const float2 tileHi = floor(ceil(float2(iEnds.y, jEnds.y) + 1e-3) / float(SUN_TILE));
                const float2 tilesWide = tileHi - tileLo + 1.0;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
                speedWhy = all(tileLo >= 0.0) && all(tileHi <= float(SUN_TILES - 2u)) ? 7u : 6u;
#endif
                if (all(tileLo >= 0.0) && all(tileHi <= float(SUN_TILES - 2u)) && tilesWide.x * tilesWide.y <= float(16u * SUN_TESTS)) {
                    // Each tile of the block against its own record: its corners' places in the frame (its grid places'
                    // box and its moves' box), how far over (or behind) the point's plane they stand, their rays' reach,
                    // and where they land along a0 and along b0: all to one side of the sun's footprint round the point,
                    // and no triangle of the tile covers it.
                    const uint wideI = uint(tilesWide.x), count = wideI * uint(tilesWide.y);
                    const uint tileI0 = uint(tileLo.x), tileJ0 = uint(tileLo.y);
                    const uint pick = (g.waterGeometry.w & 0xFFu) != 0u ? min(g.waterGeometry.w & 0xFFu, SUN_PICK_MOST) : SUN_PICK;
                    // A block of more than SUN_TESTS tiles: every `every`th is asked, from a first one drawn by chance.
                    const uint every = (count + SUN_TESTS - 1u) / SUN_TESTS;
                    const uint firstAsked = every > 1u ? min(uint(rng.next() * float(every)), every - 1u) : 0u;
                    // A tile's corners' grid places: its first corner's, and the box its other corners add (cornerLo, cornerHi).
                    const float3 tileI = stepI * float(SUN_TILE), tileJ = stepJ * float(SUN_TILE);
                    const float3 cornerLo = min(tileI, 0.0) + min(tileJ, 0.0), cornerHi = max(tileI, 0.0) + max(tileJ, 0.0);
                    uint picked[SUN_PICK_MOST];
                    { for (uint z = 0; z < SUN_PICK_MOST; ++z) picked[z] = 0u; }
                    uint left = 0u;
                    [loop] for (uint k = firstAsked; k < count; k += every) {
                        const uint tI = tileI0 + k % wideI, tJ = tileJ0 + k / wideI;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
                        ++g_scSunTileTests;
#endif
                        const SunRecord r = sun_record_load(keptCorners, slotAt + SUN_TILES_AT + (tJ * SUN_TILES + tI) * SUN_RECORD_BYTES);
                        const float3 tileAt = firstAt + tileI * float(tI) + tileJ * float(tJ);
                        const float3 placesLo = tileAt + cornerLo + r.moveLo, placesHi = tileAt + cornerHi + r.moveHi;
                        float overLo, overHi; sun_span(placesLo, placesHi, -rayOwn, overLo, overHi);
                        const float3 raysLo = r.rayLo - slack, raysHi = r.rayHi + slack;
                        float tileAlongLo, tileAlongHi; sun_span(raysLo, raysHi, rayOwn, tileAlongLo, tileAlongHi);
                        if (!(tileAlongHi > 0.05)) continue;
                        // (The reach h / along over the two boxes: h may be under 0, along is over 0.)
                        const float alongLeast = max(tileAlongLo, 0.05);
                        const float reachLo = overLo >= 0.0 ? overLo / tileAlongHi : overLo / alongLeast;
                        const float reachHi = overHi >= 0.0 ? overHi / alongLeast : overHi / tileAlongHi;
                        const float2 landLo = placesLo.xy + min(min(reachLo * raysLo.xy, reachLo * raysHi.xy), min(reachHi * raysLo.xy, reachHi * raysHi.xy));
                        const float2 landHi = placesHi.xy + max(max(reachLo * raysLo.xy, reachLo * raysHi.xy), max(reachHi * raysLo.xy, reachHi * raysHi.xy));
                        const float margin = fattest * max(reachHi, 0.0) + 2e-3;
                        if (any(landLo > margin) || any(landHi < -margin)) continue;
                        // One of the tiles left: among `pick` of them, each as likely as any other.
                        const uint key = tI | (tJ << 16);
                        if (left < pick) picked[left] = key;
                        else { const uint at = min(uint(rng.next() * float(left + 1u)), left); if (at < pick) picked[at] = key; }
                        ++left;
                    }
                    // The tiles picked, summed whole: corner by corner along a row, a cell's two triangles as its last
                    // corner is made (the corner before it, and the two under them from the row before).
#ifdef PT_SPEED_MEASURE // the measuring build alone: the tiles asked, none summed
                    const uint tiles = speed_measure2(SPEED_M2_NO_SUM) ? 0u : min(left, pick);
#else
                    const uint tiles = min(left, pick);
#endif
                    kept = float(max(tiles, 1u)) / (float(max(left, 1u)) * float(every));
                    const uint side1 = SUN_TILE + 1u, corners = side1 * side1;
                    const float3 firstPlace = float3(first.x, base0.y, first.y);
                    SunBent tileRows[2][SUN_TILE + 1];
                    [loop] for (uint q = 0; q < tiles * corners; ++q) {
                        const uint tile = picked[q / corners], within = q % corners, i = within % side1, j = within / side1;
                        const uint cornerI = (tile & 0xFFFFu) * SUN_TILE + i, cornerJ = (tile >> 16) * SUN_TILE + j;
                        const uint address = slotAt + (cornerJ * SUN_CORNERS + cornerI) * SUN_CORNER_BYTES;
                        const float4 head = asfloat(keptCorners.Load4(address));
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
                        ++g_scSunTileCorners;
#endif
                        const SunBent c = sun_bend(ask, head.xyz + float3(0.0, lift, 0.0), float3(head.w, asfloat(keptCorners.Load2(address + 16u))));
                        tileRows[j & 1u][i] = c;
                        if (i == 0u || j == 0u) continue;
                        // The cell this corner closes: two triangles of the surface, each one's light where its corners landed.
                        const SunBent c00 = tileRows[(j - 1u) & 1u][i - 1u], c10 = tileRows[(j - 1u) & 1u][i], c01 = tileRows[j & 1u][i - 1u];
                        [loop] for (uint side = 0; side < 2u; ++side) {
                            SunBent cb, cc;
                            if (side == 0u) { cb = c10; cc = c; } else { cb = c; cc = c01; }
                            float way;
                            const float light = sun_triangle(c00, cb, cc, footprint, l, alpha * alpha, way);
                            if (!(light > 0.0)) continue;
                            const float3 arriving = light * segment_transmittance(sigma, way);
                            sum += arriving;
                            const float w = luminance(arriving);
                            weightSum += w;
                            if (w > 0.0 && rng.next() * weightSum < w) {
                                chosenPlace = (c00.place.xyz + cb.place.xyz + cc.place.xyz) / 3.0;
                                // The triangle's middle on the level surface: its three corners' grid places' mean.
                                const float2 centre = float2(float(cornerI), float(cornerJ)) - (side == 0u ? float2(1.0, 2.0) : float2(2.0, 1.0)) / 3.0;
                                chosenBase = firstPlace + stepU * centre.x + stepV * centre.y;
                            }
                        }
                    }
                    made = true;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
                    ++g_scSunTiles;
#endif
                }
            }
        }
    }

    // ---- Made at the point (the top of this file; the code as it was): what the tiles above could not serve.
    float2 middle = 0.0;       // the patch's middle on the plane's coordinates (0: under base0)
    float2 middleStray = 0.0;  // how far the middle's own ray strays from its place on the surface, and that place from its
    float2 middleMoved = 0.0;  // undisplaced point (the waves' sideways move): the patch is measured against both
    float middleMiss = 0.0;    // how far the middle's ray lands from the point
    float halfSide = 0.0;      // 0: the steps that find the middle (one corner each)
    uint steps = 0;
    SunCorner rows[2][SUN_GRID + 1];
#ifdef PT_SPEED_MEASURE // the measuring build alone: no patch is made at the point (its light is left out)
    if (!made && (speed_measure2(SPEED_M2_NO_SUM) || speed_measure2(SPEED_M2_NO_PATCH) || speed_measure3(SPEED_M3_NO_PATCH))) return 0.0;
    if (!made) {
        ++g_scSunPatches;
        if (coarse) ++g_scSunCoarser;
        if (speedWhy == 1u) ++g_scSunNoGrid; else if (speedWhy == 2u) ++g_scSunNotLevel; else if (speedWhy == 3u) ++g_scSunCoarse;
        else if (speedWhy == 4u) ++g_scSunTilt; else if (speedWhy == 5u) ++g_scSunGrows; else if (speedWhy == 6u) ++g_scSunOffGrid; else ++g_scSunWide;
    }
#endif
#ifdef PT_REGROUPS
    // ON TRIAL (pt_water_path.hlsli water_regroup; "Speed trial" 4): the points that make their own patch are regrouped
    // before they make it, so that the threads the tiles served do not wait for them (one point in a thousand near the
    // camera, every point of the sea past the kept corners' reach).
    if (!made) water_regroup(SPEED_WATER_REGROUP_PATCH, 0x8000u);
#endif
    [loop] for (uint turn = made ? 40u : 0u; turn < 40u; ++turn) {
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
        ++g_scSunPatchTurns;
#endif
        const uint n = halfSide > 0.0 ? uint(clamp(ceil(2.0 * halfSide / cellWanted), 2.0, float(SUN_GRID))) : 0u;
        const float cell = n > 0u ? 2.0 * halfSide / float(n) : 0.0;
        if (n > 0u) {
            // A patch of more corners than the budget is made by chance (the one number drawn for it stays: a patch that
            // grows is given up as soon as it would not have been made at its new size).
            const float corners = 0.785 * float((n + 1u) * (n + 1u));
            kept = min(kept, SUN_BUDGET / corners);
            if (!(chance < kept)) { sum = 0.0; weightSum = 0.0; made = true; break; }
        }
        sum = 0.0; weightSum = 0.0;
        float stray = 0.0, wander = 0.0;
        [loop] for (uint j = 0; j <= n; ++j) {
            [loop] for (uint i = 0; i <= n; ++i) {
                const float2 offset = n > 0u ? (float2(float(i), float(j)) + jitter - 0.5) * cell - halfSide : float2(0.0, 0.0);
                SunCorner c; c.place = 0.0; c.landed = float4(0.0, 0.0, 0.0, -1.0);
                // The patch is a disc: the square's own corners are not asked.
                const bool used = n == 0u || dot(offset, offset) <= (halfSide + 1.5 * cell) * (halfSide + 1.5 * cell);
                if (used) {
                    const float2 at = middle + offset;
                    const float3 base = base0 + stepX * at.x + stepY * at.y;
                    float3 position, normal; float lift, steep;
                    water_displace(slot, base, up, position, normal, lift, steep);
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
                    ++g_scSunPatchCorners; ++speedCorners;
#endif
                    const float3 flat = position - p;
                    const float2 under = float2(dot(flat, a), dot(flat, b));
                    wander = max(wander, length(under - at - middleMoved));
                    const float facing = dot(normal, l);
                    const Interface boundary = dielectric_interface(-l, normalize(l + ggx_sample(normal, l, alpha, tilt)), 1.0, WATER_IOR);
                    const float3 t = normalize(boundary.transmitted);
                    const float along = dot(t, t0);
                    const float reach = -dot(flat, t0) / max(along, 0.05);
                    if (facing > 1e-4 && along > 0.05 && reach > 0.0 && all(isfinite(position)) && all(isfinite(t))) {
                        const float3 land = flat + t * reach;
                        c.place = float4(position, reach);
                        // What the facet lets through, and what the facets on the way in hide of the bent ray (Smith's).
                        c.landed = float4(dot(land, a), dot(land, b), along, (1.0 - boundary.reflectance) * smith_g1(abs(dot(normal, t)), alpha * alpha));
                        stray = max(stray, length(c.landed.xy - under - middleStray));
                        if (n == 0u) { middleStray = c.landed.xy - under; middleMoved = under - at; middleMiss = length(c.landed.xy); }
                    }
                    if (n == 0u && steps < SUN_RECENTRE && c.landed.w >= 0.0) middle -= c.landed.xy;
                }
                rows[j & 1u][i] = c;
                if (i == 0u || j == 0u) continue;
                // The cell this corner closes: two triangles of the surface, each one's light where its corners landed.
                const SunCorner c00 = rows[(j - 1u) & 1u][i - 1u], c10 = rows[(j - 1u) & 1u][i], c01 = rows[j & 1u][i - 1u];
                [loop] for (uint side = 0; side < 2u; ++side) {
                    SunCorner cb, cc;
                    if (side == 0u) { cb = c10; cc = c; } else { cb = c; cc = c01; }
                    if (c00.landed.w < 0.0 || cb.landed.w < 0.0 || cc.landed.w < 0.0) continue;
                    const float reach = (c00.place.w + cb.place.w + cc.place.w) / 3.0;
                    const float2 size = max(footprint * reach, 1e-4);
                    const float2 ya = c00.landed.xy / size, yb = cb.landed.xy / size, yc = cc.landed.xy / size;
                    const float2 lo = min(ya, min(yb, yc)), hi = max(ya, max(yb, yc));
                    if (lo.x > 1.0 || lo.y > 1.0 || hi.x < -1.0 || hi.y < -1.0) continue;
                    const float area = 0.5 * abs((yb.x - ya.x) * (yc.y - ya.y) - (yb.y - ya.y) * (yc.x - ya.x));
                    if (!(area > 1e-12)) continue;
                    const float covered = min(disc_triangle(ya, yb, yc), area);
                    if (!(covered > 0.0)) continue;
                    // The sun through the surface's triangle: its area across the light (the grid's two steps turn so that
                    // this cross product points down into the water).
                    const float across = max(0.0, -0.5 * dot(cross(cb.place.xyz - c00.place.xyz, cc.place.xyz - c00.place.xyz), l));
                    const float through = (c00.landed.w + cb.landed.w + cc.landed.w) / 3.0;
                    const float straight = (c00.landed.z + cb.landed.z + cc.landed.z) / 3.0;
                    const float light = across * through * (covered / area) / (PI * size.x * size.y * straight);
                    if (!(light > 0.0) || !isfinite(light)) continue;
                    const float3 arriving = light * segment_transmittance(sigma, reach);
                    sum += arriving;
                    const float w = luminance(arriving);
                    weightSum += w;
                    if (w > 0.0 && rng.next() * weightSum < w) {
                        chosenPlace = (c00.place.xyz + cb.place.xyz + cc.place.xyz) / 3.0;
                        const float2 at = middle + (float2(float(i), float(j)) + jitter - 0.5 - (side == 0u ? float2(1.0, 2.0) : float2(2.0, 1.0)) / 3.0) * cell - halfSide;
                        chosenBase = base0 + stepX * at.x + stepY * at.y;
                    }
                }
            }
        }
        if (n == 0u) {
            // The middle is found: the first patch is one cell round it.
            if (++steps > SUN_RECENTRE) halfSide = cellWanted;
            continue;
        }
        // The patch holds what can reach the point when it reaches past the farthest any of its corners' rays strays from the
        // middle's (and the middle's own miss, the sun's footprint and half a cell: the grid is laid half a cell off by
        // chance), less how far the waves' sideways move shifts its corners against the middle.
        const float needed = SUN_MARGIN * stray + middleMiss + 2.0 * max(footprint.x, footprint.y) * s0 + 0.5 * cell;
        if (halfSide - wander >= needed) { made = true; break; }
        halfSide = max(1.35 * halfSide, 1.05 * (needed + wander));
    }
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
    if (speedCorners >= 256u) ++g_scSunPatchBig;
    if (!made || !(weightSum > 0.0) || !(kept > 0.0)) ++g_scSunNone;
#endif
    if (!made || !(weightSum > 0.0) || !(kept > 0.0)) return 0.0;
    float3 light = sum / kept;
    // The chosen cell speaks for all: its foam, and its two shadow rays (the caustics' own convention: such a ray does not
    // stop at the waves it starts on).
    light *= saturate(1.0 - water_foam(slot, chosenBase));
    if (!any(light > 0.0)) return 0.0;
    const float3 toPlace = chosenPlace - p;
    const float away = length(toPlace);
    const bool there = !(away > 2.0 * water_epsilon(p)); // the point lies on the surface: no way between them to ask about
    light *= sun_rays(p, there ? l : toPlace / away, water_epsilon(p), there ? 0.0 : away, chosenPlace + l * 0.005, l, g.params.y, 2u, true, -1) * sun_fog(chosenPlace);
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): whether the work brought any light
    if (any(light > 0.0)) ++g_scSunLit; else ++g_scSunBlocked;
#endif
    return g.sunColor.rgb * light;
}
#endif
