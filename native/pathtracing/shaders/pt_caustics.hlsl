// SPDX-License-Identifier: MIT
// Caustics (the Caustics row, PT-14): the sun's and the lamps' light traced onto HDRP's water as its waves shape it, reflected
// and refracted there and traced on to what it meets; its light per area where it lands is the caustic (caustic_light in
// pt_common.hlsli reads it). The method is the warped light mesh of "Real-Time Ray Traced Caustics" (Ray Tracing Gems II,
// chapter 30, its water caustics): a grid of rays over the water, each cell's light carried by the cell's corner rays, its
// brightness where it lands the light through the cell over the area the corners land round. Rays that bunch (a wave focusing
// the light) make a small area and a bright line, rays that spread a dim one; no blur and no noise come from the method.
//   - The grid: levels of 512 x 512 cells round the camera (tracer.cpp: 2 cm the finest, each next twice as coarse, holding the
//     finer one's square in its middle), moved by a random fraction of a cell every frame. A group traces a tile of 15 x 15
//     cells (its 16 x 16 corners) of one level over one slot's water.
//   - A corner: the water's level surface under it (a ray down onto the slot's water instance), then HDRP's waves there as HDRP's
//     water shader moves its surface (water_point): each band's displacement (its height, and its horizontal move times HDRP's
//     choppiness of 2.25) and surface gradient (read bicubic, as HDRP reads it), times its amplitude and its fade with the
//     distance from the camera.
//   - The lights of a tile: the two that send its water the most light, and one of all the others, chosen in proportion to its
//     light and divided by its chance (every lamp counts; its noise is the weak lamps' alone). A lamp's light leaves a random
//     point of its emitter's ball (the same for every tile of the frame), the sun's a random direction within its disc.
//   - Each corner's light: a shadow ray to the light (glass tints it), and two rays on from the wave: the reflection and the
//     refraction (an index of 1.333, the exact Fresnel split between them), each to the first thing it meets. The reflected
//     light takes HDRP's fog on its way, the refracted the water's absorption.
//   - Each cell, two triangles: the light through the water's triangle (a lamp's intensity times the solid angle the triangle
//     fills seen from it; the sun's irradiance times the triangle's area across its light), as the corners carry it, lands on
//     the triangle their rays hit; its irradiance there is that light over that triangle's area. A triangle whose corners land
//     on different surfaces (across an edge) is left out, as one that lands outside the camera's view (nothing reads it there
//     yet: mirrors and bounced light don't ask for caustics).
//   The landed triangles go into buffers an acceleration structure is built over every frame (tracer.cpp), which caustic_light
//   (pt_common.hlsli) asks with a short ray at a point.
//   pass.a.x: 0 empties the counters, 1 traces (SV_GroupID.z = level x 4 + slot), 2 makes the room past this frame's triangles
//   inactive (a thread a triangle)
//   ON TRIAL (PT-114, docs/agents/caustgrid.md; TGPT_FRAME_CAUSTICS_GRID and its two further bits, the plugin's hidden
//   "Caustics grid trial" 1 to 3; off: everything as before). What it leaves out is work that cannot end in a written
//   triangle; what it does another way gives the same numbers. The triangles written are the same, each with the same
//   numbers; of the counters, the tiles' and the waves' (the corners counted, a slot's sample) count fewer tiles and
//   corners, and a light that is not traced marks no tile as lit.
//   1. THE TILES ARE LISTED AND THE PICK HAS A PASS OF ITS OWN. pass.a.z is the view of a list of tiles (tracer.cpp
//      caustic_tile_list), and a group runs for each listed tile (SV_GroupID.x its place in the list), where a group ran
//      for every tile of every level and slot. Before it, pass.a.x 3 (pick_tile; pass.b.y: the view of the listed tiles'
//      picks): a thread a listed tile works out that tile's three lights as the tile's 256 threads did (pick_lights):
//      share by share in the threads' order (a share: every 256th light from the thread's number, weighed in order,
//      drawn with that thread's random numbers) and joined in that order with the first share's numbers. The same sums
//      and draws in the same order: the same lights and the same number for the third. The tiles' pass reads its three
//      lights: none of its threads waits for a pick. (Measured, the stage's cost was that waiting: in pick_lights the
//      group's first thread joins the shares alone, twice, in loops of 256 steps, while the other 255 wait.)
//   2. ONLY TILES WITH WATER, ONLY CORNERS A CELL NEEDS. The list leaves out the tiles with no triangle of their slot's
//      level water under them (tracer.cpp). And a corner that no cell of its tile needs (every cell round it lies in the
//      finer level's square, or past the level's last cell) does nothing: no triangle is made from it.
//   3. A LIGHT WHOSE RAYS CANNOT LAND IN VIEW IS NOT TRACED (way_out_of_view, main): a landed triangle is written only
//      where in_view passes it. For each of the tile's lights and each branch (reflected, refracted), every corner's ray
//      on is held against the regions in_view turns a triangle away by; where all the tile's corners' rays lie wholly in
//      one of them, no triangle of that branch can be written, wherever the rays land: the branch is not traced, and with
//      both branches so, the light's shadow rays are not traced either. (Not with TGPT_OPTION_CAUSTICS_SPLIT: its parts
//      land off the corners' rays.)
//   pass.a.w (the steps not taken, and TGPT_TEST_CAUSTIC_CUTS, a test's): 1 every light is traced (no step 3); 2 every
//   corner is worked out (no step 2); 16 and 32 are tracer.cpp's (no pass for the pick; every tile listed).
//   The counters (COUNTERS uints, read back by tracer.cpp): [0] the triangles written, [1] their largest tolerance (a float's
//   bits: caustic_light's reach), and what the waves are where the tracer reads them, for the log (the proof that HDRP's waves
//   arrive): per slot s, [4 + s] 1 once its sample is taken, [8 + s] the highest a wave moved the surface (0.1 mm), [12 + s]
//   the steepest slope (x 10000), [16 + 8 s ..] the sample: a grid point (x, z), the surface there (x, y, z), its normal.
//   And the sun's light through the water as it lands (the refracted branch, the triangles in view), per level l, sum i at
//   [48 + 8 i + l] (SUM_*; tracer.cpp says them in the log's status line). In 4096ths of the sun's light through one of the
//   level's cells: 0 what the triangles' corners carry to what they met, 1 the part of it on triangles whose corners met
//   different surfaces (left out, unless the trial below lands it), 2 what the trial landed of that part. The rippling
//   light, of the triangles that landed on one surface: 3 their landed area across the beam, 4 the light through them and 5
//   that light times its own brightness (its light over its landed area across the beam), all in the level's cells times
//   4096: 4 over 3 is the mean brightness on what they landed on, 5 over 3 its mean square, so the waves' focusing shows as
//   the spread between them; 6 their rays' lengths (cm) and 7 their count. And the waves: 8 the squares of the slopes at the
//   level's corners (times 4096), 9 the corners.
//   And the tiles (PT-114, docs/agents/caustgrid.md; tracer.cpp says them in the log's status line), per level l and slot s
//   at [128 + 32 k + 4 l + s]: k 0 the tiles that ran (a group that got past its first tests: its level, its slot's water,
//   the finer level's square, the slot's extent), 1 those of them with a corner on the slot's water, 2 those in which a
//   light reached a corner (a shadow ray came through), 3 those that wrote a triangle. A tile that ran and wrote none did
//   its corners' work for nothing in view.
//   [2], [3]: a sum over every triangle written of a hash of its record (its corners, its light, its tolerance and the
//   direction its light came from), the same whatever order the threads wrote them in: two frames wrote the same triangles
//   when the count and these two words agree (the check of a cut that must leave them as they are).
//   ON TRIAL (PT-81, docs/agents/water-9.md; TGPT_OPTION_CAUSTICS_SPLIT, the plugin's hidden "Caustics land split light";
//   off: the picture as before): a triangle whose corners met different surfaces is not left out. Each corner's third of
//   its light lands on the plane of what that corner met (emit_part).
#include "pt_common.hlsli"
#include "pt_waves.hlsli"

static const uint TILE = 16;            // corners a tile's side
static const uint CELLS = 15;           // cells a tile's side
static const uint GRID = 512;           // cells a level's side (tracer.cpp kCausticGrid)
static const uint SLOTS = 4;            // TGPT_MAX_WATER
static const float UNDER_REACH = 200.0; // metres a refracted ray looks (pt_water_under's)
static const uint SUN = 0xFFFFFFFEu;
static const uint PICKS = 3;
static const uint COUNTERS = 256;       // tracer.cpp kCausticCounters
static const uint SUMS = 10;            // the sums of the sun's light as it landed: the counters' [48 + 8 i + level]
static const uint SUM_CARRIED = 0, SUM_SPLIT = 1, SUM_LANDED = 2, SUM_ACROSS = 3, SUM_SHARE = 4, SUM_SQUARE = 5, SUM_DEPTH = 6, SUM_COUNT = 7, SUM_STEEP = 8, SUM_CORNERS = 9;
static const uint OPTION_SPLIT = 1073741824u; // TGPT_OPTION_CAUSTICS_SPLIT (tgpt_api.h): ON TRIAL
static const float FLUX_UNIT = 4096.0;  // the counters' fixed point: the sun's light through one cell of the level
static const uint TILE_COUNTS = 128;    // the counters' tiles: [TILE_COUNTS + 32 k + 4 level + slot] (k: TILE_RAN to TILE_WROTE)
static const uint TILE_RAN = 0, TILE_WATER = 1, TILE_LIT = 2, TILE_WROTE = 3;
static const uint CHECKSUM = 2;         // the counters' two words of the written triangles' checksum

// A level's cell (the finest, doubled per level).
float caustic_cell(uint level) { return g.causticGrid.x * exp2(float(level)); }
// A thread's sums for the counters (SUM_*), and its group's: main adds a thread's to the group's at its end, and the
// group's first thread theirs to the counters (ten atomic adds a tile, not a thread).
static uint s_sum[SUMS];
groupshared uint gs_sum[SUMS];
// A thread's part of the tile's marks (bit TILE_WATER: its corner is on the slot's water; bit TILE_LIT: a light reached it;
// bit TILE_WROTE: it wrote a triangle) and of the written triangles' checksum, and its group's: joined as the sums are.
static uint s_marks = 0u;
static uint2 s_check = 0u;
groupshared uint gs_marks;
groupshared uint gs_check[2];
// ON TRIAL (PT-114): per light of the tile, the regions out of view that every corner's ray on lies wholly in (way_out_of_view's
// bits; the reflected ray's in bits 0 to 4, the refracted ray's in bits 8 to 12): each corner takes away what its own does not.
groupshared uint gs_view[PICKS];
// The sun's light through a water triangle in the counters' units (flux: the triangle's share times what a corner carries).
uint flux_units(float share, float3 carried, float cell) {
    const float sun = luminance(g.sunColor.rgb);
    if (!(sun > 0.0)) return 0u;
    return uint(clamp(share * luminance(carried) / (sun * cell * cell) * FLUX_UNIT + 0.5, 0.0, 65535.0));
}

groupshared uint gs_light[PICKS];        // the tile's lights (SUN, a light's index, CAUSTIC_NONE)
groupshared float gs_scale[PICKS];       // and what each one's light counts for
groupshared float3 gs_water[TILE * TILE];    // each corner's point on the waves (relative to the scene origin)
groupshared float3 gs_land[2][TILE * TILE];  // where its reflected [0] and refracted [1] rays landed
groupshared float3 gs_normal[2][TILE * TILE];// the surface they landed on, its smooth normal (0: they landed nowhere)
groupshared float3 gs_carried[2][TILE * TILE]; // the light each carries: the light's intensity (a lamp's, per steradian) or
                                               // irradiance (the sun's) at the corner, times its share and what its way takes
groupshared uint gs_lit[TILE * TILE];          // whether the tile's light in turn reaches the corner at all (its shadow ray)
groupshared uint gs_on[TILE * TILE];           // whether the corner is a point of the slot's water at all (water_point)
static const float CORNER_OFFSET = 0.005;      // metres a corner's rays start along their own way

// A point of a lamp's emitter for this frame (the same for every tile): uniform in its ball.
float3 light_point(LightRecord l, uint index) {
    Rng r; r.state = pcg(asuint(g.causticGrid.w) ^ pcg(index + 0x632BE5ABu));
    const float z = 1.0 - 2.0 * r.next(), phi = 2.0 * PI * r.next();
    const float s = sqrt(max(0.0, 1.0 - z * z)), radius = l.radius * pow(r.next(), 1.0 / 3.0);
    return l.position + float3(s * cos(phi), z, s * sin(phi)) * radius;
}
// The sun's direction for this frame: a point of its disc.
float3 sun_direction() {
    Rng r; r.state = pcg(asuint(g.causticGrid.w) ^ 0xA511E9B3u);
    return cone_sample(g.sunDir.xyz, g.sunDir.w, r.next2());
}

// How much light a light sends onto a tile's water at most (its box: the tile's square, the water's heights with the waves'
// largest move): the sun's irradiance times the square's area across it; a lamp's intensity times the square's area over the
// nearest distance squared (at most a hemisphere), none under the water nor out of the range of a light that keeps one. Never 0
// where it lights any.
float light_weight(uint li, float2 lo, float2 hi, float yLo, float yHi) {
    const float area = (hi.x - lo.x) * (hi.y - lo.y);
    if (li == SUN) return g.sunColor.w > 0.0 && g.sunDir.y > 0.0 ? luminance(g.sunColor.rgb) * area * g.sunDir.y : 0.0;
    const LightRecord l = load_light(li);
    if (l.type > 1u || l.position.y < yLo) return 0.0;
    const float3 c = clamp(l.position, float3(lo.x, yLo, lo.y), float3(hi.x, yHi, hi.y));
    const float d2 = dot(l.position - c, l.position - c);
    // A light that falls off with distance alone (LIGHT_NO_RANGE, PT-50: the plugin's lights) reaches the water however far it is;
    // its range is only how far the light grid lists it. One that keeps HDRP's window stops at its range.
    if ((l.flags & LIGHT_NO_RANGE) == 0u && !(d2 <= l.range * l.range)) return 0.0;
    return luminance(l.color) * min(area / max(d2, 1e-4), 2.0 * PI);
}
// The tile's lights, all 256 threads over all the lights (the sun and every lamp; a scene holds a thousand near the camera): the
// two strongest, and one of the rest in proportion to its weight, which then counts for all of the rest (their summed weight over
// its own). Each thread keeps its own share's two strongest and sum, then its share's pick of the rest (a weighted reservoir);
// one thread joins them (a share's pick taken by the share's sum: a light's chance is its weight over the rest's). The corners'
// light arrays (gs_carried) hold the shares meanwhile.
// stop: the measuring build's (TGPT_TEST_CAUSTIC_PARTS 7, 8, 9: the pass's threads end after the pick's first, second
// and third step; 0 in a build for players); true when they end here.
bool pick_lights(uint index, uint slot, float2 lo, float2 hi, uint tile, uint stop) {
    const float margin = water_row(slot, 12).z;
    const float yLo = g.waterBottom[slot] - margin, yHi = g.waterTop[slot] + margin;
    const uint count = g.settings2.z;
    Rng r; r.state = pcg(asuint(g.causticGrid.w) ^ pcg(tile * 0x9E3779B9u + index * 0x85EBCA6Bu + 0x7F4A7C15u));
    uint i0 = CAUSTIC_NONE, i1 = CAUSTIC_NONE; float w0 = 0.0, w1 = 0.0, total = 0.0;
    for (uint k = index; k <= count; k += TILE * TILE) {
        const uint li = k == count ? SUN : k;
        const float w = light_weight(li, lo, hi, yLo, yHi);
        if (!(w > 0.0)) continue;
        total += w;
        if (w > w0) { i1 = i0; w1 = w0; i0 = li; w0 = w; } else if (w > w1) { i1 = li; w1 = w; }
    }
    gs_carried[0][index] = float3(w0, w1, total);
    gs_carried[1][index] = float3(asfloat(i0), asfloat(i1), 0.0);
    GroupMemoryBarrierWithGroupSync();
    if (stop == 7u) return true;
    if (index == 0u) {
        uint a = CAUSTIC_NONE, b = CAUSTIC_NONE; float wa = 0.0, wb = 0.0, sum = 0.0;
        for (uint t = 0; t < TILE * TILE; ++t) {
            const float3 w = gs_carried[0][t];
            const uint2 ids = asuint(gs_carried[1][t].xy);
            sum += w.z;
            [unroll] for (uint j = 0; j < 2; ++j) {
                const float wj = j == 0 ? w.x : w.y; const uint ij = j == 0 ? ids.x : ids.y;
                if (!(wj > 0.0)) continue;
                if (wj > wa) { b = a; wb = wa; a = ij; wa = wj; } else if (wj > wb) { b = ij; wb = wj; }
            }
        }
        gs_light[0] = a; gs_scale[0] = 1.0;
        gs_light[1] = b; gs_scale[1] = 1.0;
        gs_light[2] = CAUSTIC_NONE; gs_scale[2] = sum - wa - wb; // the rest's weight, until its pick is made
    }
    GroupMemoryBarrierWithGroupSync();
    if (stop == 8u) return true;
    const uint a = gs_light[0], b = gs_light[1];
    float rest = 0.0, pickWeight = 0.0; uint pick = CAUSTIC_NONE;
    for (uint k2 = index; k2 <= count; k2 += TILE * TILE) {
        const uint li = k2 == count ? SUN : k2;
        if (li == a || li == b) continue;
        const float w = light_weight(li, lo, hi, yLo, yHi);
        if (!(w > 0.0)) continue;
        rest += w;
        if (r.next() * rest < w) { pick = li; pickWeight = w; }
    }
    gs_carried[0][index] = float3(rest, pickWeight, asfloat(pick));
    GroupMemoryBarrierWithGroupSync();
    if (stop == 9u) return true;
    if (index == 0u) {
        const float all = gs_scale[2];
        float sum = 0.0, chosenWeight = 0.0; uint chosen = CAUSTIC_NONE;
        for (uint t = 0; t < TILE * TILE; ++t) {
            const float3 s = gs_carried[0][t];
            if (!(s.x > 0.0)) continue;
            sum += s.x;
            if (r.next() * sum < s.x) { chosen = asuint(s.z); chosenWeight = s.y; }
        }
        if (chosen != CAUSTIC_NONE && sum > 1e-6 * (sum + all) && chosenWeight > 0.0) { gs_light[2] = chosen; gs_scale[2] = sum / chosenWeight; }
        else { gs_light[2] = CAUSTIC_NONE; gs_scale[2] = 0.0; }
    }
    GroupMemoryBarrierWithGroupSync();
    return false;
}

// ON TRIAL (PT-114): a tile's pick by ONE thread (the pick's own pass, a thread a tile), as pick_lights above makes it with
// the group's 256 threads: the shares in the threads' order (share t: the lights t, t + 256, ... and the sun as the light
// after the last, weighed in that order; its own two strongest and sum, then its own draw of one of the rest with the
// random numbers of the thread that had it), joined share by share in that order as the first thread joined them, the
// draws of the join with the first share's numbers going on. Every sum and every draw in the same order: the same result.
struct TilePick { uint light[PICKS]; float third; }; // (the first two lights count for themselves: 1)
TilePick pick_tile(uint slot, float2 lo, float2 hi, uint tile) {
    const float margin = water_row(slot, 12).z;
    const float yLo = g.waterBottom[slot] - margin, yHi = g.waterTop[slot] + margin;
    const uint count = g.settings2.z;
    const uint shares = min(TILE * TILE, count + 1u); // (a thread past the last light had none: its share was nothing)
    uint a = CAUSTIC_NONE, b = CAUSTIC_NONE; float wa = 0.0, wb = 0.0, sum = 0.0;
    for (uint t = 0; t < shares; ++t) {
        uint i0 = CAUSTIC_NONE, i1 = CAUSTIC_NONE; float w0 = 0.0, w1 = 0.0, total = 0.0;
        for (uint k = t; k <= count; k += TILE * TILE) {
            const uint li = k == count ? SUN : k;
            const float w = light_weight(li, lo, hi, yLo, yHi);
            if (!(w > 0.0)) continue;
            total += w;
            if (w > w0) { i1 = i0; w1 = w0; i0 = li; w0 = w; } else if (w > w1) { i1 = li; w1 = w; }
        }
        sum += total;
        if (w0 > 0.0) { if (w0 > wa) { b = a; wb = wa; a = i0; wa = w0; } else if (w0 > wb) { b = i0; wb = w0; } }
        if (w1 > 0.0) { if (w1 > wa) { b = a; wb = wa; a = i1; wa = w1; } else if (w1 > wb) { b = i1; wb = w1; } }
    }
    const float all = sum - wa - wb; // the rest's weight
    Rng first; first.state = 0u;
    float joined = 0.0, chosenWeight = 0.0; uint chosen = CAUSTIC_NONE;
    for (uint t2 = 0; t2 < shares; ++t2) {
        Rng r; r.state = pcg(asuint(g.causticGrid.w) ^ pcg(tile * 0x9E3779B9u + t2 * 0x85EBCA6Bu + 0x7F4A7C15u));
        float rest = 0.0, pickWeight = 0.0; uint pick = CAUSTIC_NONE;
        for (uint k2 = t2; k2 <= count; k2 += TILE * TILE) {
            const uint li = k2 == count ? SUN : k2;
            if (li == a || li == b) continue;
            const float w = light_weight(li, lo, hi, yLo, yHi);
            if (!(w > 0.0)) continue;
            rest += w;
            if (r.next() * rest < w) { pick = li; pickWeight = w; }
        }
        if (t2 == 0u) first = r; // (the first thread's numbers go on in the join)
        if (!(rest > 0.0)) continue;
        joined += rest;
        if (first.next() * joined < rest) { chosen = pick; chosenWeight = pickWeight; }
    }
    TilePick p;
    p.light[0] = a; p.light[1] = b;
    if (chosen != CAUSTIC_NONE && joined > 1e-6 * (joined + all) && chosenWeight > 0.0) { p.light[2] = chosen; p.third = joined / chosenWeight; }
    else { p.light[2] = CAUSTIC_NONE; p.third = 0.0; }
    return p;
}
// One corner's light for one of the tile's lights, in two steps with the group's barrier between them (main).
// First what the light sends it: its shadow ray. gs_lit says whether any arrives.
struct CornerLight { float3 toLight; float3 carried; bool faces; };
CornerLight corner_light(uint index, uint li, float scale, bool ok, float3 x, float3 n) {
    CornerLight c; c.toLight = 0.0; c.carried = 0.0; c.faces = false;
    gs_lit[index] = 0u;
    if (!ok) return c;
    float3 toLight, intensity; float tmax;
    if (li == SUN) {
        toLight = sun_direction(); tmax = g.params.y;
        intensity = g.sunColor.rgb * sun_fog(x);
    } else {
        LightRecord l = load_light(li);
        const float3 centre = l.position;
        l.position = light_point(l, li);
        float dist;
        intensity = light_incident(l, x, toLight, dist);
        intensity *= dist * dist; // per steradian: the solid angle the water's triangle fills multiplies it (emit_triangle)
        tmax = max(dist - light_own_reach(l), 0.0);
        // The ray aims at a point INSIDE the emitter's ball, so it ends where it enters that ball (and a centimetre): what
        // lies inside is the emitter itself (a lantern's glass, a crystal's body). Ending light_own_reach short of the point
        // alone, a point on the ball's far side had the emitter's own shell in the way: for a 4 cm ball 18.5 % of the
        // points, and as a frame has one point a light, that light's whole caustic was gone in as many frames.
        const float along = dot(centre - x, toLight);
        const float3 aside = centre - x - toLight * along;
        tmax = min(tmax, max(along - sqrt(max(l.radius * l.radius - dot(aside, aside), 0.0)) - 0.01, 0.0));
    }
    const float cosIn = dot(n, toLight);
    if (!(cosIn > 1e-4)) return c; // the light meets the wave's back: no ray leaves it here
    g_passWater = true; // the caustics' own rays: the level water below the waves is none of theirs
    c.carried = intensity * scale * transmittance(x + toLight * CORNER_OFFSET, toLight, 0.0, tmax);
    g_passWater = false;
    c.toLight = toLight; c.faces = true;
    gs_lit[index] = any(c.carried > 0.0) ? 1u : 0u;
    return c;
}
// Then its two rays on from the wave, where they landed and what they carry (gs_*[*][index]). A corner no light reaches
// carries none, but a triangle it shares with a lit corner still needs to know where its ray lands: it traces when it or
// any corner it shares a triangle with is lit (the six round it: main's two triangles a cell). Where no corner of a
// triangle is lit the triangle was never written (emit_triangle), so nothing changes in the light that lands; what goes is
// the two rays of every corner of water no light reaches: water under the ground (the open sea's level runs under the
// whole land), in a closed room, behind a hill. In a forest with no water in view the stage cost 12 ms a frame for them
// (run 20261008-124542).
// branches (ON TRIAL, PT-114; 3 otherwise): bit 0 the reflected ray is traced, bit 1 the refracted.
void corner_rays(uint index, uint2 at, uint slot, CornerLight c, float3 x, float3 n, uint branches) {
    gs_normal[0][index] = 0.0; gs_normal[1][index] = 0.0;
    gs_carried[0][index] = 0.0; gs_carried[1][index] = 0.0;
    gs_land[0][index] = x; gs_land[1][index] = x;
    if (!c.faces) return;
    bool wanted = gs_lit[index] != 0u;
    if (!wanted) {
        static const int2 beside[6] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1), int2(1, 1), int2(-1, -1) };
        for (uint k = 0; k < 6u && !wanted; ++k) {
            const int2 other = int2(at) + beside[k];
            if (any(other < 0) || any(other >= int(TILE))) continue;
            wanted = gs_lit[uint(other.y) * TILE + uint(other.x)] != 0u;
        }
    }
    if (!wanted) return;
    const float3 toLight = c.toLight, carried = c.carried;
    // Each ray leaves the wave a few millimetres along its own way: off its normal, the reflected light landed that much aside
    // (5 mm up a wall, a lamp's light 7 % short at its cone's edge, where it falls off steeply).
    const float offset = CORNER_OFFSET;
    g_passWater = true;
    const Interface boundary = dielectric_interface(-toLight, n, 1.0, WATER_IOR);
    const float reflected = boundary.reflectance;
    Hit hit; float3 tint;
    ray_cone(0.0, 0.0);
    const float3 r = boundary.reflected;
    if ((branches & 1u) != 0u && r.y > 0.0 && trace_bounce(x + r * offset, r, g.params.y, hit, tint)) {
        gs_land[0][index] = hit.position; gs_normal[0][index] = hit.rounded;
        gs_carried[0][index] = carried * reflected * tint * exp(-height_fog_depth(x, hit.position));
    }
    const float3 t = boundary.transmitted;
    if ((branches & 2u) != 0u && dot(t, t) > 1e-6 && trace_bounce(x + t * offset, t, min(g.params.y, UNDER_REACH), hit, tint)) {
        gs_land[1][index] = hit.position; gs_normal[1][index] = hit.rounded;
        gs_carried[1][index] = carried * (1.0 - reflected) * tint * segment_transmittance(water_row(slot, 14).xyz, length(hit.position - x));
    }
    g_passWater = false;
}

// The solid angle a triangle fills seen from the origin (Van Oosterom and Strackee, 1983).
float solid_angle(float3 a, float3 b, float3 c) {
    const float la = length(a), lb = length(b), lc = length(c);
    const float volume = abs(dot(a, cross(b - a, c - a)));
    return 2.0 * atan2(volume, la * lb * lc + dot(a, b) * lc + dot(a, c) * lb + dot(b, c) * la);
}

// Whether a landed triangle can be seen: not wholly behind the camera, nor wholly beyond one side of its view (half a view's
// width round it kept: the bed seen through the water lies a little outside the straight view).
bool in_view(float3 a, float3 b, float3 c) {
    const float4 ca = mul(g.viewProj, float4(a - g.cameraPos.xyz, 1.0)), cb = mul(g.viewProj, float4(b - g.cameraPos.xyz, 1.0)), cc = mul(g.viewProj, float4(c - g.cameraPos.xyz, 1.0));
    if (ca.w <= 0.0 && cb.w <= 0.0 && cc.w <= 0.0) return false;
    if (ca.w <= 0.0 || cb.w <= 0.0 || cc.w <= 0.0) return true;
    const float k = 1.5;
    if (ca.x > k * ca.w && cb.x > k * cb.w && cc.x > k * cc.w) return false;
    if (ca.x < -k * ca.w && cb.x < -k * cb.w && cc.x < -k * cc.w) return false;
    if (ca.y > k * ca.w && cb.y > k * cb.w && cc.y > k * cc.w) return false;
    if (ca.y < -k * ca.w && cb.y < -k * cb.w && cc.y < -k * cc.w) return false;
    return true;
}

// ON TRIAL (PT-114): a ray's whole way (from `from`, `reach` metres along `dir`) against the camera's view as in_view judges a
// landed triangle: the regions the whole way lies in. Bit 0: behind the camera (w <= 0 all along). Bits 1 to 4: in front of
// it (w > 0 all along) and beyond one side of the widened view (x > k w, x < -k w, y > k w, y < -k w: in_view's four tests).
// Each region is convex, so the way lies in one when both its ends do. A landed triangle whose three corners' ways lie in
// one region is one in_view turns away wherever on their ways the corners landed (a corner that landed nowhere stays at
// the way's start): behind, all three w <= 0; beyond a side, all three w > 0 and all three beyond it.
// Slack: each end must be inside by SLACK times the sum of its terms' sizes, which is convex along the way too, so every
// point between is inside by that much of its own sum: a hundred times and more what a landed point's float arithmetic
// (the ray's hit distance, the point, in_view's own products) can be off by.
uint way_out_of_view(float3 from, float3 dir, float reach) {
    const float SLACK = 1e-4, k = 1.5; // (k: in_view's)
    uint regions = 31u;
    for (uint end = 0; end < 2u; ++end) {
        const float3 p = from + dir * (end == 0u ? 0.0 : reach) - g.cameraPos.xyz;
        const float4 c = mul(g.viewProj, float4(p, 1.0));
        const float4 size = mul(abs(g.viewProj), float4(abs(p), 1.0)) * SLACK;
        uint here = 0u;
        if (c.w < -size.w) here = 1u;
        else if (c.w > size.w) {
            if (c.x - k * c.w > size.x + k * size.w) here |= 2u;
            if (-c.x - k * c.w > size.x + k * size.w) here |= 4u;
            if (c.y - k * c.w > size.y + k * size.w) here |= 8u;
            if (-c.y - k * c.w > size.y + k * size.w) here |= 16u;
        }
        regions &= here;
    }
    return all(isfinite(from + dir * reach)) ? regions : 0u;
}
// ON TRIAL (PT-114): the direction from a corner to one of the tile's lights, as corner_light takes it.
float3 corner_to_light(uint li, float3 x) {
    if (li == SUN) return sun_direction();
    LightRecord l = load_light(li);
    l.position = light_point(l, li);
    float3 toLight; float dist;
    light_incident(l, x, toLight, dist);
    return toLight;
}

// A landed triangle into the buffers the acceleration structure is built over: its corners (wound to face the side its light
// came from) and its light per area at each. False when it was not written (past the room, or not finite).
// from: the direction its light came from (towards the water), kept in the light record's bytes 40 to 43 (pack_normal) for
// the trial that reads it (pt_common caustic_lit, TGPT_OPTION_CAUSTICS_LIT); nothing else reads those bytes.
bool store_triangle(float3 pa, float3 pb, float3 pc, float3 la, float3 lb, float3 lc, float edge, float3 from) {
    if (!all(isfinite(la + lb + lc))) return false;
    if (!all(isfinite(pa + pb + pc))) return false;
    RWByteAddressBuffer light = ResourceDescriptorHeap[g.caustics.x];
    RWByteAddressBuffer corners = ResourceDescriptorHeap[asuint(g.causticGrid.y)];
    RWByteAddressBuffer counters = ResourceDescriptorHeap[g.caustics.z];
    uint at;
    counters.InterlockedAdd(0, 1u, at);
    if (at >= g.caustics.w) return false; // past the room this frame (counted: tracer.cpp makes more room for the next)
    // Its corners: the acceleration structure's vertices. Its light: floats (a focused line's light is far above a half's
    // range, and clamped it was light lost), and its tolerance, whose largest is how far caustic_light's ray reaches.
    const float tolerance = caustic_tolerance(edge);
    corners.Store4(at * CAUSTIC_CORNERS, uint4(asuint(pa), 0u));
    corners.Store4(at * CAUSTIC_CORNERS + 16, uint4(asuint(pb), 0u));
    corners.Store4(at * CAUSTIC_CORNERS + 32, uint4(asuint(pc), 0u));
    light.Store4(at * CAUSTIC_LIGHT, uint4(asuint(la), asuint(lb.x)));
    light.Store4(at * CAUSTIC_LIGHT + 16, uint4(asuint(lb.yz), asuint(lc.xy)));
    light.Store2(at * CAUSTIC_LIGHT + 32, uint2(asuint(lc.z), asuint(tolerance)));
    const uint packed = pack_normal(from);
    light.Store(at * CAUSTIC_LIGHT + 40, packed);
    if (asuint(tolerance) > counters.Load(4)) counters.InterlockedMax(4, asuint(tolerance)); // positive floats order as their bits
    // The checksum (CHECKSUM): a hash of the record as it was written, added up (the order of the writes does not count).
    uint h0 = 0x9E3779B9u, h1 = 0x7F4A7C15u;
    const uint words[20] = { asuint(pa.x), asuint(pa.y), asuint(pa.z), asuint(pb.x), asuint(pb.y), asuint(pb.z), asuint(pc.x), asuint(pc.y), asuint(pc.z),
                             asuint(la.x), asuint(la.y), asuint(la.z), asuint(lb.x), asuint(lb.y), asuint(lb.z), asuint(lc.x), asuint(lc.y), asuint(lc.z),
                             asuint(tolerance), packed };
    for (uint w = 0; w < 20u; ++w) { h0 = pcg(h0 ^ words[w]); h1 = pcg(h1 + words[w] * 0x85EBCA6Bu + w); }
    s_check += uint2(h0, h1);
    s_marks |= 1u << TILE_WROTE;
    return true;
}

// ON TRIAL (TGPT_OPTION_CAUSTICS_SPLIT): where corner j's ray meets a plane (through p, facing m). Its own ray where it has
// one that comes down onto the plane's front; where it has none (it landed nowhere) or runs along the plane, the way of the
// corner whose plane it is (along: the beam taken as parallel there).
float3 part_corner(uint branch, uint j, float3 p, float3 m, float3 along) {
    const float3 x = gs_water[j];
    const float3 way = gs_land[branch][j] - x;
    const float reach2 = dot(way, way);
    float3 dir = any(gs_normal[branch][j] != 0.0) && reach2 > 1e-8 ? way * rsqrt(reach2) : along;
    if (!(dot(dir, m) < -0.05)) dir = along;
    return x + dir * max(dot(p - x, m) / dot(dir, m), 0.0);
}
// ON TRIAL (TGPT_OPTION_CAUSTICS_SPLIT): corner i's part of a triangle whose corners met different surfaces. The light through
// a water triangle is its share times the mean of what its three corners carry, so a corner's part is a third of the share
// times what it carries. It lands on the plane of what that corner met (through its landing point, by the smooth normal
// there), on the triangle the three corners' rays cut out of that plane (the beam's cut there: rays that bunch make it
// small and bright, as on a whole triangle), full at the corner's own point and falling to nothing at the other two: read
// by the barycentric weights its mean is a third of the corner's light per area, and its sum the corner's part exactly.
// Where the three corners did meet one plane the three parts add up to the whole triangle as it was written before.
// What this does not do (docs/agents/water-9.md): the part of a cut that lies past the edge of what the corner met is read
// by nothing there, so a rim of a cell's width still gets less than its light (half of what it lost before), and a thing
// thinner than a cell gets light only in the frames a corner lands on it.
// Tried and taken out: a part kept full (not falling off) towards a corner whose ray passed this plane and landed on another
// thing behind it. It lit a 10 cm box's rim 5 % better and did nothing for thin things, and where two things meet at a
// slope (a rock in the sand) the bed at its foot would read both planes: light counted twice. The parts as they are add
// up to 1 wherever they are read together, so they cannot.
void emit_part(uint branch, uint i, uint j, uint k, float share, float cell, uint li) {
    const float3 q = gs_carried[branch][i], n = gs_normal[branch][i];
    if (!any(n != 0.0) || !any(q > 0.0)) return;
    const float3 p = gs_land[branch][i];
    const float3 way = p - gs_water[i];
    const float reach2 = dot(way, way);
    if (!(reach2 > 1e-10)) return;
    const float3 along = way * rsqrt(reach2);
    // The plane faces the corner's ray by a fifth at least: a smooth normal turned from its own ray (a round thing met at
    // its rim) would cut the beam nowhere, or behind it.
    const float facing = dot(n, -along);
    const float3 m = facing < 0.2 ? normalize(n - along * (0.2 - facing)) : n;
    float3 pb = part_corner(branch, j, p, m, along), pc = part_corner(branch, k, p, m, along);
    const float3 nt = cross(pb - p, pc - p);
    const float twice = length(nt);
    if (!(twice > 1e-12)) return;
    if (!in_view(p, pb, pc)) return;
    if (dot(nt, way) > 0.0) { const float3 swap = pb; pb = pc; pc = swap; }
    const float edge = max(length(pb - p), max(length(pc - p), length(pc - pb)));
    const float3 l = to_storage(q * (share / (0.5 * twice) / PI));
    if (store_triangle(p, pb, pc, l, 0.0, 0.0, edge, -along) && li == SUN && branch == 1u) s_sum[SUM_LANDED] += flux_units(share, q / 3.0, cell);
}

// One triangle of the water (corners a, b, c, wound upwards as the grid gives them: (c - a) x (b - a) points up) for one branch:
// the light through it, landed where its corners' rays met the scene, into the hash.
void emit_triangle(uint branch, uint a, uint b, uint c, uint li, uint level) {
    const float3 na = gs_normal[branch][a], nb = gs_normal[branch][b], nc = gs_normal[branch][c];
    // A corner off the slot's water has no point of it: no triangle of the water there. (Its normal of 0 left the triangle
    // out before; the trial below goes on where a corner landed nowhere, so this is said by itself.)
    if (gs_on[a] == 0u || gs_on[b] == 0u || gs_on[c] == 0u) return;
    const bool landed = any(na != 0.0) && any(nb != 0.0) && any(nc != 0.0);
    float3 qa = gs_carried[branch][a], qb = gs_carried[branch][b], qc = gs_carried[branch][c];
    if (!any(qa + qb + qc > 0.0)) return;
    const float3 xa = gs_water[a], xb = gs_water[b], xc = gs_water[c];
    const float3 up = cross(xc - xa, xb - xa); // twice the water triangle's area, along its upper side
    float share;
    if (li == SUN) share = max(0.0, 0.5 * dot(up, sun_direction()));
    else {
        const float3 y = light_point(load_light(li), li);
        share = dot(up, y - xa) > 0.0 ? solid_angle(xa - y, xb - y, xc - y) : 0.0;
    }
    if (!(share > 0.0)) return;
    // Its corners landed on one surface: their normals agree (the meshes' smooth normals, gs_normal: a rock's facets bend the
    // flat ones a little at every edge, which is one surface still), and a triangle of some breadth lies along it (one across a
    // gap, from a step's top to its foot, lies along neither). Left out, its light is missed within one landed cell of such an
    // edge (the grid's resolution: finer cells, a narrower seam). ON TRIAL (TGPT_OPTION_CAUSTICS_SPLIT): not left out; each
    // corner's part lands on the plane of what that corner met (emit_part), as does the part of a corner that landed where
    // another of its triangle landed nowhere.
    bool one = landed && !(dot(na, nb) < 0.5 || dot(nb, nc) < 0.5 || dot(na, nc) < 0.5);
    float3 pa = gs_land[branch][a], pb = gs_land[branch][b], pc = gs_land[branch][c];
    float3 nt = cross(pb - pa, pc - pa);
    const float twice = length(nt);
    if (one && !(twice > 1e-12)) return;
    const float edge = max(length(pb - pa), max(length(pc - pa), length(pc - pb)));
    if (one && twice > 0.02 * edge * edge && abs(dot(nt / twice, normalize(na + nb + nc))) < 0.5) one = false;
    const float cell = caustic_cell(level);
    const bool counted = li == SUN && branch == 1u; // the counters: the sun's light under the water
    if (!one) {
        if (counted && in_view(pa, pb, pc)) { const uint units = flux_units(share, (qa + qb + qc) / 3.0, cell); s_sum[SUM_CARRIED] += units; s_sum[SUM_SPLIT] += units; }
        if ((g.settings2.y & OPTION_SPLIT) != 0u) {
            emit_part(branch, a, b, c, share, cell, li); emit_part(branch, b, c, a, share, cell, li); emit_part(branch, c, a, b, share, cell, li);
        }
        return;
    }
    if (!in_view(pa, pb, pc)) return;
    const float3 beam = normalize((pa - xa) + (pb - xb) + (pc - xc)); // the way its light came
    if (counted) {
        s_sum[SUM_CARRIED] += flux_units(share, (qa + qb + qc) / 3.0, cell);
        // The rippling light: the triangle's landed area across its beam, the light through it (no shadow, no absorption in
        // it: the waves' focusing alone), and that light times its brightness there (held to 64 times the unfocused light).
        const float across = 0.5 * abs(dot(nt, beam)) / (cell * cell), through = share / (cell * cell);
        if (across > 0.0) {
            s_sum[SUM_ACROSS] += uint(min(across * FLUX_UNIT + 0.5, 65535.0));
            s_sum[SUM_SHARE] += uint(min(through * FLUX_UNIT + 0.5, 65535.0));
            s_sum[SUM_SQUARE] += uint(min(through * min(through / across, 64.0) * FLUX_UNIT + 0.5, 4.0e6));
            s_sum[SUM_DEPTH] += uint(min(length(pa - xa) * 100.0 + 0.5, 5000.0));
            s_sum[SUM_COUNT] += 1u;
        }
    }
    // Wound to face the side its light came from (caustic_covers takes the receiving surface's side by it).
    if (dot(nt, pa - xa) > 0.0) { const float3 swap = pb; pb = pc; pc = swap; const float3 swapQ = qb; qb = qc; qc = swapQ; }
    // Its light per area at each corner: the light through the water's triangle over the area it landed on, shared between the
    // corners as they carry it (the lamp's cone and cookie, the shadow, the Fresnel split vary across a cell). Read across the
    // triangle by its barycentric weights (caustic_light), its mean is the triangle's light: none is made or lost.
    const float density = share / (0.5 * twice) / PI;
    const float3 la = to_storage(qa * density), lb = to_storage(qb * density), lc = to_storage(qc * density);
    store_triangle(pa, pb, pc, la, lb, lc, edge, -beam);
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(16, 16, 1)]
void main(uint3 group : SV_GroupID, uint3 tid : SV_GroupThreadID, uint index : SV_GroupIndex) {
    uint3 gid = group;
    if (pass.a.x == 0u) {
        if (g.caustics.x == INVALID) return;
        RWByteAddressBuffer counters = ResourceDescriptorHeap[g.caustics.z];
        if (index < COUNTERS) counters.Store(index * 4, 0u);
        return;
    }
    if (pass.a.x == 2u) {
        // The room past this frame's triangles: inactive (a NaN x at each corner), so the build leaves the last frame's there out.
        if (g.caustics.x == INVALID) return;
        RWByteAddressBuffer corners = ResourceDescriptorHeap[asuint(g.causticGrid.y)];
        RWByteAddressBuffer counters = ResourceDescriptorHeap[g.caustics.z];
        const uint i = gid.x * 256u + index;
        if (i < g.caustics.w && i >= counters.Load(0)) {
            const uint nan = 0x7FC00000u;
            corners.Store(i * CAUSTIC_CORNERS, nan); corners.Store(i * CAUSTIC_CORNERS + 16, nan); corners.Store(i * CAUSTIC_CORNERS + 32, nan);
        }
        return;
    }
    if (g.caustics.x == INVALID) return;
    if (pass.a.x == 3u) {
        // ON TRIAL (PT-114): the lights' pick of each listed tile, a thread a tile (pass.b.x: the tiles listed).
        const uint place = group.x * (TILE * TILE) + index;
        if (place >= pass.b.x) return;
        ByteAddressBuffer tileList = ResourceDescriptorHeap[pass.a.z];
        RWByteAddressBuffer picks = ResourceDescriptorHeap[pass.b.y];
        const uint tile = tileList.Load(place * 4u);
        const uint3 its = uint3(tile & 63u, (tile >> 6) & 63u, (tile >> 12) & 31u);
        const uint itsSlot = its.z % SLOTS, itsLevel = its.z / SLOTS;
        TilePick p; p.light[0] = CAUSTIC_NONE; p.light[1] = CAUSTIC_NONE; p.light[2] = CAUSTIC_NONE; p.third = 0.0;
        if (itsLevel < uint(g.causticGrid.z) && water_slot(itsSlot)) {
            const float itsCell = caustic_cell(itsLevel);
            const float2 from = g.causticLevels[itsLevel].xy + float2(int2(its.xy) * int(CELLS)) * itsCell;
            p = pick_tile(itsSlot, from, from + float(CELLS) * itsCell, (its.z * 64u + its.y) * 64u + its.x);
        }
        picks.Store4(place * 16u, uint4(p.light[0], p.light[1], p.light[2], asuint(p.third)));
        return;
    }
    // ON TRIAL (PT-114): the group's tile from the list.
    const bool listed = pass.a.z != INVALID;
    bool viewCut = false;
    if (listed) {
        ByteAddressBuffer tileList = ResourceDescriptorHeap[pass.a.z];
        const uint tile = tileList.Load(group.x * 4u);
        gid = uint3(tile & 63u, (tile >> 6) & 63u, (tile >> 12) & 31u);
        viewCut = (pass.a.w & 1u) == 0u && (g.settings2.y & OPTION_SPLIT) == 0u;
    }
    const uint slot = gid.z % SLOTS, level = gid.z / SLOTS;
    if (level >= uint(g.causticGrid.z) || !water_slot(slot)) return;
    const float cell = caustic_cell(level);
    const float4 lv = g.causticLevels[level];
    const int2 first = int2(gid.xy) * int(CELLS);
    if (any(first >= int(GRID))) return;
    // The finer level's square is that level's: a tile wholly inside it has nothing to do here.
    const bool inner = lv.z > -1e8;
    const int2 innerLo = int2(lv.zw), innerHi = innerLo + int(GRID / 2);
    if (inner && all(first >= innerLo) && all(first + int(CELLS) <= innerHi)) return;
    // Nor one off the slot's water.
    const float2 lo = lv.xy + float2(first) * cell, hi = lo + float(CELLS) * cell;
    const float4 extent = water_row(slot, 13);
    const float margin = water_row(slot, 12).z;
    if (any(hi < extent.xy - margin) || any(lo > extent.zw + margin)) return;
#if defined(PT_SPEED_MEASURE) || defined(PT_CAUSTICS_PARTS)
    // The measuring build alone (TGPT_TEST_CAUSTIC_PARTS, tracer.cpp: a test's): where every thread of the pass stops, for
    // the stage's time by part. 1 here (the dispatch and a tile's first tests), 2 after the lights' pick, 3 after the corner's
    // ray down onto the level water, 4 after its point on the waves, 5 after the shadow rays, 6 after the rays on (no
    // triangle is written); anything else: all of it.
    const uint stop = pass.a.y;
    if (stop == 1u) return;
#else
    const uint stop = 0u;
#endif

    { for (uint s = 0; s < SUMS; ++s) s_sum[s] = 0u; }
    if (index == 0u) { // pick_lights' barriers come before any thread adds to them
        for (uint s = 0; s < SUMS; ++s) gs_sum[s] = 0u;
        gs_marks = 0u; gs_check[0] = 0u; gs_check[1] = 0u;
        for (uint v = 0; v < PICKS; ++v) gs_view[v] = 0x1F1Fu;
    }
    // The tile's lights and what each one's light counts for: read where the pick had its own pass (ON TRIAL, PT-114),
    // else picked here by the group's threads.
    uint tileLight[PICKS]; float tileScale[PICKS];
    if (listed && pass.b.y != INVALID) {
        RWByteAddressBuffer picks = ResourceDescriptorHeap[pass.b.y];
        const uint4 p = picks.Load4(group.x * 16u);
        tileLight[0] = p.x; tileLight[1] = p.y; tileLight[2] = p.z;
        tileScale[0] = 1.0; tileScale[1] = 1.0; tileScale[2] = asfloat(p.w);
    } else {
        if (pick_lights(index, slot, lo, hi, (gid.z * 64u + gid.y) * 64u + gid.x, stop)) return;
        for (uint own = 0; own < PICKS; ++own) { tileLight[own] = gs_light[own]; tileScale[own] = gs_scale[own]; }
    }
#if defined(PT_SPEED_MEASURE) || defined(PT_CAUSTICS_PARTS)
    if (stop == 2u) return;
    if (stop == 3u) {
        uint own = slot; float3 base, baseNormal;
        gs_on[index] = all(first + int2(tid.xy) <= int(GRID)) && water_base(own, lv.xy + float2(first + int2(tid.xy)) * cell, base, baseNormal) ? 1u : 0u;
        gs_water[index] = base;
        return;
    }
#endif
    const int2 corner = first + int2(tid.xy);
    float3 x = 0.0, n = float3(0.0, 1.0, 0.0);
    float lift = 0.0, steep = 0.0;
    // ON TRIAL (PT-114): a corner is needed when one of the up to four cells round it in this tile writes triangles (main's
    // "emits" below, for that cell): a triangle is made from a cell's own four corners alone.
    bool needed = true;
    if (listed && (pass.a.w & 2u) == 0u) {
        needed = false;
        for (int back = 0; back < 4; ++back) {
            const int2 c = int2(tid.xy) - int2(back & 1, back >> 1); // the cell's first corner in the tile
            if (any(c < 0) || any(c >= int(CELLS))) continue;
            const int2 at = first + c;
            if (all(at < int(GRID)) && !(inner && all(at >= innerLo) && all(at < innerHi))) needed = true;
        }
    }
    const bool ok = needed && all(corner <= int(GRID)) && water_point(slot, lv.xy + float2(corner) * cell, x, n, lift, steep);
    gs_water[index] = x;
    gs_on[index] = ok ? 1u : 0u;
    if (ok) s_marks |= 1u << TILE_WATER;
    if (ok) { s_sum[SUM_STEEP] += uint(min(steep * steep, 4.0) * FLUX_UNIT + 0.5); s_sum[SUM_CORNERS] += 1u; }
    // What the waves are where the tracer reads them, for the log: a tile's first corner counts (one in 256), and the first
    // of them a frame is the slot's sample (tracer.cpp says where HDRP's waves put the surface there: still water shows as 0).
    if (ok && index == 0u) {
        RWByteAddressBuffer counters = ResourceDescriptorHeap[g.caustics.z];
        const uint high = uint(min(abs(lift) * 10000.0, 4e9)), slope = uint(min(steep * 10000.0, 4e9));
        if (high > counters.Load((8 + slot) * 4)) counters.InterlockedMax((8 + slot) * 4, high);
        if (slope > counters.Load((12 + slot) * 4)) counters.InterlockedMax((12 + slot) * 4, slope);
        if (counters.Load((4 + slot) * 4) == 0u) {
            uint was;
            counters.InterlockedCompareExchange((4 + slot) * 4, 0u, 1u, was);
            if (was == 0u) {
                const float2 at = lv.xy + float2(corner) * cell;
                counters.Store4((16 + slot * 8) * 4, uint4(asuint(at), asuint(x.xy)));
                counters.Store4((20 + slot * 8) * 4, uint4(asuint(x.z), asuint(n)));
            }
        }
    }
    GroupMemoryBarrierWithGroupSync();
#if defined(PT_SPEED_MEASURE) || defined(PT_CAUSTICS_PARTS)
    if (stop == 4u) return;
#endif
    // ON TRIAL (PT-114): which of the tile's lights can land anything in view. Each corner's two rays on for each light, by
    // the corner's point and normal and the light's direction as corner_light and corner_rays have them (the same calls),
    // against the regions out of view; the tile's light keeps a region only while every corner's ray lies in it.
    if (viewCut) {
        for (uint v = 0; v < PICKS; ++v) {
            const uint li = tileLight[v];
            if (li == CAUSTIC_NONE || !ok) continue; // (a corner off the water is in no triangle)
            // A corner whose ray is not traced stays where it is (its triangles' corner is its own point): the point alone.
            // The tests that decide whether a ray is traced (corner_light's cosine, corner_rays' "upwards") are taken with
            // room to spare here, so a ray they let through by a last bit is still held against the view.
            const float3 toLight = corner_to_light(li, x);
            uint up = way_out_of_view(x, 0.0, 0.0), down = up;
            if (dot(n, toLight) > 5e-5) {
                const Interface boundary = dielectric_interface(-toLight, n, 1.0, WATER_IOR);
                const float3 r = boundary.reflected, t = boundary.transmitted;
                if (r.y > -1e-4) up = way_out_of_view(x, r, CORNER_OFFSET + g.params.y);
                if (any(t != 0.0)) down = way_out_of_view(x, t, CORNER_OFFSET + min(g.params.y, UNDER_REACH));
            }
            const uint mine = up | (down << 8);
            if (mine != 0x1F1Fu) InterlockedAnd(gs_view[v], mine);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    // Its cell (tid's corner its first): in this level's grid and not the finer level's.
    const bool emits = tid.x < CELLS && tid.y < CELLS && all(corner < int(GRID)) && !(inner && all(corner >= innerLo) && all(corner < innerHi));
    for (uint k = 0; k < PICKS; ++k) {
        const uint li = tileLight[k];
        if (li == CAUSTIC_NONE) continue;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure2): the grid's corners and lights, no ray of the lights'
        if (speed_measure2(SPEED_M2_CAUSTIC_BARE)) continue;
#endif
        // ON TRIAL (PT-114): the branches of this light that can land in view from this tile (both, without the trial).
        const uint seen = viewCut ? ((gs_view[k] & 31u) != 0u ? 0u : 1u) | (((gs_view[k] >> 8) & 31u) != 0u ? 0u : 2u) : 3u;
        if (seen == 0u) continue;
        const CornerLight sent = corner_light(index, li, tileScale[k], ok, x, n);
        s_marks |= gs_lit[index] << TILE_LIT;
        GroupMemoryBarrierWithGroupSync();
#ifdef PT_SPEED_MEASURE // the measuring build alone: the shadow rays, no ray on from the wave
        if (speed_measure2(SPEED_M2_CAUSTIC_SHADOW)) continue;
#endif
#if defined(PT_SPEED_MEASURE) || defined(PT_CAUSTICS_PARTS)
        if (stop == 5u) continue;
#endif
        corner_rays(index, tid.xy, slot, sent, x, n, seen);
        GroupMemoryBarrierWithGroupSync();
#if defined(PT_SPEED_MEASURE) || defined(PT_CAUSTICS_PARTS)
        if (stop == 6u) continue;
#endif
        if (emits) {
            for (uint branch = 0; branch < 2; ++branch) {
                if ((seen & (1u << branch)) == 0u) continue; // (PT-114: nothing of it passes in_view)
                emit_triangle(branch, index, index + 1, index + TILE + 1, li, level);
                emit_triangle(branch, index, index + TILE + 1, index + TILE, li, level);
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
    // The sums of the sun's light as it landed, for the log (the counters' [48 + 8 i + level]): each thread's to its group's,
    // the group's to the counters.
    { for (uint s = 0; s < SUMS; ++s) if (s_sum[s] != 0u) InterlockedAdd(gs_sum[s], s_sum[s]); }
    if (s_marks != 0u) InterlockedOr(gs_marks, s_marks);
    if (any(s_check != 0u)) { InterlockedAdd(gs_check[0], s_check.x); InterlockedAdd(gs_check[1], s_check.y); }
    GroupMemoryBarrierWithGroupSync();
    if (index == 0u) {
        RWByteAddressBuffer counters = ResourceDescriptorHeap[g.caustics.z];
        for (uint s = 0; s < SUMS; ++s) if (gs_sum[s] != 0u) counters.InterlockedAdd((48 + 8 * s + level) * 4, gs_sum[s]);
        // The tile's marks (TILE_COUNTS) and its triangles' checksum.
        const uint marks = gs_marks | (1u << TILE_RAN);
        for (uint m = 0; m < 4u; ++m) if ((marks & (1u << m)) != 0u) counters.InterlockedAdd((TILE_COUNTS + 32 * m + 4 * level + slot) * 4, 1u);
        if (gs_check[0] != 0u) counters.InterlockedAdd(CHECKSUM * 4, gs_check[0]);
        if (gs_check[1] != 0u) counters.InterlockedAdd((CHECKSUM + 1) * 4, gs_check[1]);
    }
}
