// SPDX-License-Identifier: MIT
// The game's fog as a medium the tracer's rays cross (PT-112 step B, the rule since 2026-10-11; docs/agents/fog-2.md):
// the light a point of the fog scatters (fog_light: the sun, the sky, one lamp or glowing surface, each by its traced shadow
// ray), the mist's own cells in the radiance cache for the light scattered more than once, and the fog over a reflected or
// refracted ray's way (traced_fog_on_way, fogged's traced form). Included at the end of pt_common.hlsli: everything here is
// built on what that file defines. The camera's own fog is traced with it too, in cells (pt_fog_froxel.hlsl and
// pt_froxel.hlsli; until 2026-10-12 per pixel, pt_fog.hlsl: PT-118).
// The medium: the camera's blended Fog volume as HDRP fogs with it (FrameConstants.fog, height_fog_depth), its albedo and
// anisotropy (FrameConstants.fogMedium, TGPT_Frame.fogMedium: the plugin's FillFog; all 0, not sent: the fog colour's rgb,
// even scattering).
float3 fog_albedo() {
    const float3 a = g.fogMedium.rgb;
    return dot(a, 1.0) > 0.0 && all(a < 2.0) ? a : saturate(g.fogColor.rgb);
}
float fog_anisotropy() { return clamp(g.fogMedium.w, -0.99, 0.99); }

float fog_depth_between(float3 a, float3 b) { return height_fog_depth(a, b); }
// HDRP's CornetteShanksPhasePartVarying times its constant part (VolumeRendering.hlsl): per steradian, for light whose way
// turns by an angle of cosine c (1: straight on).
float fog_phase(float anisotropy, float c) {
    const float g2 = anisotropy * anisotropy;
    return (3.0 / (8.0 * PI)) * (1.0 - g2) / (2.0 + g2) * (1.0 + c * c) / pow(max(1.0 + g2 - 2.0 * anisotropy * c, 1e-6), 1.5);
}
float fog_boxes_toward(float3 x, float3 l, float tmax, inout Rng rng); // (below: the fog boxes' transmittance on the light's way)
// The light that reaches a point x of the fog straight, scattered toward -d (d: the way the light leaving x goes on, toward
// the camera's side: the camera ray's direction at the first point, the walk's at the later ones). lamps false: the sun, the
// sky and directional lights alone (the lamps and glowing surfaces then come from fog_lamps_along, drawn along the way).
float3 fog_light(float3 x, float3 d, float anisotropy, inout Rng rng, bool lamps) {
    float3 light = 0.0;
    if (g.sunColor.w > 0.0) {
        const float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
        light += g.sunColor.rgb * fog_phase(anisotropy, dot(l, d)) * transmittance(x, l, 0.0, g.params.y) * sun_fog(x) * fog_boxes_toward(x, l, g.params.y, rng);
    }
    {
        // The sky's: one direction over the whole sphere (its chance 1 / 4 pi a steradian), above the horizon.
        const float2 u = rng.next2();
        const float z = 1.0 - 2.0 * u.x, r = sqrt(max(0.0, 1.0 - z * z)), phi = 2.0 * PI * u.y;
        const float3 w = float3(r * cos(phi), z, r * sin(phi));
        if (w.y > 0.0 && !occluded(x, w, 0.0, g.params.y))
            light += sky_radiance(w) * directional_fog(x, w.y) * fog_boxes_toward(x, w, g.params.y, rng) * (4.0 * PI * fog_phase(anisotropy, dot(w, d)));
    }
    LightLists lists;
    if (light_lists(x, lists)) {
        // One lamp or glowing surface, chosen among the candidates by the light it brings here (as direct_light chooses).
        const uint candidates = lists.candidates;
        const float u = rng.next();
        float wsum = 0.0, chosenTarget = 0.0;
        float3 chosenE = 0.0, chosenL = 0.0, chosenPoint = 0.0;
        LightRecord chosen = (LightRecord)0;
        for (uint c = 0; c < candidates; ++c) {
            float invPdf;
            const uint picked = lists_candidate(lists, c, u, invPdf);
            if (!(invPdf > 0.0)) continue; // weight 0 (a far candidate its cell's list holds too: weight 0, review-perf-astra 7)
            LightRecord lamp = load_light(picked);
            if (!lamps && lamp.type != 2u) continue; // fog_lamps_along's
            float3 l, y; float dist; float3 e;
            if (lamp.type == LIGHT_CLUSTER) {
                lamp = load_light(cluster_triangle(lamp, rng.next(), invPdf));
                e = triangle_incident(lamp, x, rng.next2(), l, dist, y);
            } else { e = light_incident(lamp, x, l, dist); y = lamp.position; }
            const float target = luminance(e) * fog_phase(anisotropy, dot(l, d));
            const float w = target * invPdf;
            wsum += w;
            if (w > 0.0 && rng.next() * wsum < w) { chosenTarget = target; chosenE = e; chosenL = l; chosen = lamp; chosenPoint = y; }
        }
        if (chosenTarget > 0.0) {
            float tmax; float3 toLamp;
            if (chosen.type == LIGHT_TRIANGLE) toLamp = triangle_shadow_ray(x, chosenPoint, tmax);
            else toLamp = shadow_ray(chosen, x, rng.next2(), tmax);
            light += chosenE * fog_phase(anisotropy, dot(chosenL, d)) * (wsum / (float(candidates) * chosenTarget)) * transmittance(x, toLamp, 0.0, tmax) *
                     fog_boxes_toward(x, toLamp, tmax, rng);
        }
    }
    return light;
}
// The distance along a way from a with the given optical depth, within [0, len]: the height fog's depth inverted in closed
// form (its density k below the base height, k e^-((h - base) / H) above it: along a straight way the depth is k t below,
// A (1 - e^-(c t)) / c above with A the density where that stretch starts and c the way's climb over H), piece by piece
// across the base. Until 2026-10-11 22 halvings, each a depth (docs/agents/review-perf-astra.md 5); the same distance
// but for rounding.
float fog_distance(float3 a, float3 dir, float len, float want) {
    const float k = g.fog.x, base = g.fog.y, rcpH = g.fog.z;
    if (!(want > 0.0)) return 0.0;
    if (!(k > 0.0) || !(k < 1e6) || !(rcpH >= 0.0)) return len;
    const float h0 = a.y + g.cacheOrigin.y, c = dir.y * rcpH;
    // The way within the base layer (constant density), then above it (or the other way round going down).
    float t = 0.0, left = want;
    if (h0 < base) {
        const float toBase = dir.y > 1e-6 ? (base - h0) / dir.y : 1e30; // where a climbing way leaves the layer
        if (left <= k * toBase) return min(left / k, len);
        t = toBase; left -= k * toBase;
        // Above the base from here: the density starts at k.
        if (abs(c) < 1e-9) return min(t + left / k, len);
        const float x = 1.0 - left * c / k;
        return x > 0.0 ? min(t - log(x) / c, len) : len;
    }
    // Starting above the base.
    const float A = k * exp(-(h0 - base) * rcpH);
    if (abs(c) < 1e-9) return min(left / max(A, 1e-30), len);
    if (dir.y > 0.0) {
        const float x = 1.0 - left * c / A;
        return x > 0.0 ? min(-log(x) / c, len) : len;
    }
    // Going down: thicker until the base, then constant.
    const float toBase = (h0 - base) / -dir.y;
    const float atBase = (k - A) / -c; // the depth from here to the base: A (e^(|c| toBase) - 1) / |c|
    if (left <= atBase) return min(log(1.0 + left * -c / A) / -c, len);
    return min(toBase + (left - atBase) / k, len);
}

// ---- The game's fog boxes (PT-112; TGPT_SetFogBoxes, tgpt_api.h TGPT_FogBox): HDRP's Local Volumetric Fog, part of the
// medium as HDRP voxelizes them (ShaderPassVoxelize.hlsl and the default Fog Volume graph: a 3D mask scrolled through the
// box, the faces' and the camera distance's fades), added to the height fog (the game's are all Additive). Their density
// changes from point to point, so a ray crosses each by tracking against its bound (its extinction times the most its mask
// holds): ratio tracking for the light through it, delta tracking for where it scatters; both exact on average, no step
// size and no cap. Every box the plugin sends, at any distance (HDRP leaves out the far ones).
#define FOG_BOX_EXPONENTIAL 1u
#define FOG_BOX_INVERT 2u
#define FOG_BOX_ALPHA_ONLY 4u
#define FOG_BOX_CLAMP 8u
#define FOG_BOX_POINT 16u
struct FogBox {
    float3 center; float extinction; float3 right; float rcpDistFadeLen; float3 up; float endTimesRcpDistFadeLen;
    float3 extents; uint flags; float3 albedo; int mask; float3 rcpPosFade; float maskMost; float3 rcpNegFade; float3 tiling; float3 scroll;
};
uint fog_box_count() { return g.fogBoxes.x != INVALID ? g.fogBoxes.y : 0u; }
FogBox fog_box(uint i) {
    ByteAddressBuffer records = ResourceDescriptorHeap[g.fogBoxes.x];
    const uint at = i * 144u;
    const float4 a = asfloat(records.Load4(at)), b = asfloat(records.Load4(at + 16u)), c = asfloat(records.Load4(at + 32u));
    const uint4 d = records.Load4(at + 48u), e = records.Load4(at + 64u);
    const float4 f = asfloat(records.Load4(at + 80u)), h = asfloat(records.Load4(at + 96u)), k = asfloat(records.Load4(at + 112u)), m = asfloat(records.Load4(at + 128u));
    FogBox box;
    box.center = a.xyz; box.extinction = a.w; box.right = b.xyz; box.rcpDistFadeLen = b.w; box.up = c.xyz; box.endTimesRcpDistFadeLen = c.w;
    box.extents = asfloat(d.xyz); box.flags = d.w; box.albedo = asfloat(e.xyz); box.mask = int(e.w);
    box.rcpPosFade = f.xyz; box.maskMost = f.w; box.rcpNegFade = h.xyz; box.tiling = k.xyz; box.scroll = m.xyz;
    return box;
}
// The most a box's extinction can be anywhere in it: the bound its tracking runs against.
float fog_box_bound(FogBox b) { return b.extinction * b.maskMost; }
// x in the box's own space: [-1, 1] on each axis inside (HDRP: right, up, right x up, over the half extents).
float3 fog_box_space(FogBox b, float3 x) {
    const float3 r = x - b.center;
    return float3(dot(r, b.right), dot(r, b.up), dot(r, cross(b.right, b.up))) / b.extents;
}
// Where a way from a along dir, within [0, tmax], is inside the box: [t0, t1]; false where it never is.
bool fog_box_span(FogBox b, float3 a, float3 dir, float tmax, out float t0, out float t1) {
    const float3 o = fog_box_space(b, a);
    const float3 d = float3(dot(dir, b.right), dot(dir, b.up), dot(dir, cross(b.right, b.up))) / b.extents;
    // PT-129 (docs/agents/nosun.md): a way that is no number is inside no box. min and max pass a NaN by, so a way whose
    // start or direction is not finite (a garbage vertex) came out below as inside EVERY box over all of [0, tmax]: with
    // the game's ray end of 1e6 m the tracking of each box then took a million steps and more, at a point where the box
    // has no fog to end it. A finite way is as before.
    if (!all(isfinite(o)) || !all(isfinite(d))) { t0 = 0.0; t1 = 0.0; return false; }
    const float3 inv = 1.0 / d;
    const float3 lo = (-1.0 - o) * inv, hi = (1.0 - o) * inv;
    const float3 enter = min(lo, hi), leave = max(lo, hi);
    t0 = max(max(enter.x, enter.y), max(enter.z, 0.0));
    t1 = min(min(leave.x, leave.y), min(leave.z, tmax));
    return t1 > t0;
}
// The box's extinction at x (per metre) and its scattering (per metre, per channel), as HDRP voxelizes it.
float fog_box_extinction(FogBox b, float3 x, out float3 scattering) {
    scattering = 0.0;
    const float3 cs = fog_box_space(b, x);
    if (any(abs(cs) > 1.0)) return 0.0;
    const float3 n = saturate(cs * 0.5 + 0.5);
    float4 mask = 1.0;
    if (b.mask >= 0) {
        Texture3D<float4> t = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + uint(b.mask))];
        float3 uvw = n * b.tiling + b.scroll;
        if ((b.flags & FOG_BOX_POINT) != 0u) mask = t.SampleLevel(s_pointClamp, (b.flags & FOG_BOX_CLAMP) != 0u ? uvw : frac(uvw), 0.0);
        else if ((b.flags & FOG_BOX_CLAMP) != 0u) mask = t.SampleLevel(s_linearClamp, uvw, 0.0);
        else mask = t.SampleLevel(s_linearRepeat, uvw, 0.0);
        if ((b.flags & FOG_BOX_ALPHA_ONLY) != 0u) mask.rgb = 1.0;
    }
    // ComputeVolumeFadeFactor: the faces' fades (Remap10 toward the positive faces, Remap01 from the negative ones), then the
    // camera distance's.
    const float3 toPositive = saturate((1.0 - n) * b.rcpPosFade), fromNegative = saturate(n * b.rcpNegFade);
    float fade = toPositive.x * toPositive.y * toPositive.z * fromNegative.x * fromNegative.y * fromNegative.z;
    if ((b.flags & FOG_BOX_EXPONENTIAL) != 0u) fade = fade > 0.0 ? pow(fade, 2.2) : 0.0;
    if ((b.flags & FOG_BOX_INVERT) != 0u) fade = 1.0 - fade;
    fade *= saturate(b.endTimesRcpDistFadeLen - length(x - g.cameraPos.xyz) * b.rcpDistFadeLen);
    const float extinction = max(mask.a * b.extinction * fade, 0.0);
    scattering = saturate(mask.rgb * b.albedo * extinction);
    return extinction;
}
// PT-129 (docs/agents/nosun.md): the most steps the tracking of one box takes for one way. A box takes its bound times the
// way's length inside it on average (40 in the game's 40 m boxes of 1 / m), so no picture reaches this; it ends a way whose
// steps no longer move it (a bound so large that a step is less than a float holds at t: the loops below had no other end
// there, where the box's own fog is 0 at the point, as at a faded face).
static const uint FOG_BOX_STEPS = 65536u;
// The light a way from a along dir, over [0, len], lets through the boxes (each but box skip): ratio tracking, its mean the
// transmittance exactly. It stops once what is left is under 1e-4 of the light (going on only takes the rest).
float fog_boxes_transmittance(float3 a, float3 dir, float len, uint skip, inout Rng rng) {
    float through = 1.0;
    const uint count = fog_box_count();
    for (uint i = 0; i < count && through > 1e-4; ++i) {
        if (i == skip) continue;
        const FogBox b = fog_box(i);
        float t0, t1;
        if (!fog_box_span(b, a, dir, len, t0, t1)) continue;
        const float bound = fog_box_bound(b);
        float t = t0;
        [loop] for (uint step = 0u; step < FOG_BOX_STEPS; ++step) {
            t -= log(1.0 - rng.next()) / bound;
            if (t >= t1) break;
            float3 scattering;
            through *= 1.0 - saturate(fog_box_extinction(b, a + dir * t, scattering) / bound);
            if (through < 1e-4) break;
        }
    }
    return through;
}
// The first point in box b's own medium where a way from a along dir, within [0, tmax], is scattered or absorbed (delta
// tracking against its bound): its distance and the albedo there; false: the way crosses it untouched.
bool fog_box_collide(FogBox b, float3 a, float3 dir, float tmax, inout Rng rng, out float t, out float3 albedo) {
    albedo = 0.0; t = tmax;
    float t0, t1;
    if (!fog_box_span(b, a, dir, tmax, t0, t1)) return false;
    const float bound = fog_box_bound(b);
    float s = t0;
    [loop] for (uint step = 0u; step < FOG_BOX_STEPS; ++step) {
        s -= log(1.0 - rng.next()) / bound;
        if (s >= t1) return false;
        float3 scattering;
        const float extinction = fog_box_extinction(b, a + dir * s, scattering);
        if (rng.next() * bound < extinction) { t = s; albedo = scattering / max(extinction, 1e-12); return true; }
    }
    return false;
}
// The chance a box has at most to scatter a way (1 less the transmittance of its bound over the way's stretch in it).
float fog_box_share(FogBox b, float3 a, float3 dir, float len) {
    float t0, t1;
    return fog_box_span(b, a, dir, len, t0, t1) ? 1.0 - exp(-fog_box_bound(b) * (t1 - t0)) : 0.0;
}
// One point where the fog scatters light toward the start of a way (a along dir; the height fog over [0, lenHeight], the
// boxes over [0, lenBoxes]), and the weight its light (fog_light, the mist's cell) is taken with: the height fog or one of
// the boxes the way crosses, chosen in proportion to its share (throughHeight: the height fog's transmittance over its
// stretch); the height fog's point drawn as it scatters (an exponential cut at the way's end), a box's by delta tracking in
// it; the rest of the medium's transmittance to the point taken along. The mean is the medium's light over the way exactly.
// roulette: the way pays for a point only with the chance the medium has to scatter it (at most 1), the weight divided by it
// (a short reflection indoors: almost never); without it, every call draws one. False: no point (its light is 0).
bool fog_scatter_point(float3 a, float3 dir, float lenHeight, float lenBoxes, float throughHeight, bool roulette, inout Rng rng, out float3 x, out float3 weight) {
    x = a; weight = 0.0;
    const float height = saturate(1.0 - throughHeight);
    const uint count = fog_box_count();
    float boxes = 0.0;
    for (uint i = 0; i < count; ++i) boxes += fog_box_share(fog_box(i), a, dir, lenBoxes);
    const float total = height + boxes;
    if (!(total > 0.0)) return false;
    float scale = total;
    if (roulette) {
        const float keep = min(total, 1.0);
        if (rng.next() >= keep) return false;
        scale /= keep;
    }
    float pick = count != 0u ? rng.next() * total : 0.0;
    if (pick < height) {
        const float t = fog_distance(a, dir, lenHeight, -log(max(1.0 - rng.next() * height, 1e-30)));
        x = a + dir * t;
        weight = fog_albedo() * scale * (count != 0u ? fog_boxes_transmittance(a, dir, t, INVALID, rng) : 1.0);
        return true;
    }
    pick -= height;
    for (uint j = 0; j < count; ++j) {
        const FogBox b = fog_box(j);
        const float share = fog_box_share(b, a, dir, lenBoxes);
        if (pick >= share && j + 1u < count) { pick -= share; continue; }
        if (!(share > 0.0)) return false;
        float t; float3 albedo;
        if (!fog_box_collide(b, a, dir, lenBoxes, rng, t, albedo)) return false;
        x = a + dir * t;
        weight = albedo * (scale / share) * exp(-fog_depth_between(a, a + dir * min(t, lenHeight))) * fog_boxes_transmittance(a, dir, t, j, rng);
        return true;
    }
    return false;
}
// Free flight through the whole medium from a along dir, within len: the first point where the height fog or any box
// scatters or absorbs the way (each drawn on its own, the nearest wins), and the albedo there; false: none within len.
bool fog_flight(float3 a, float3 dir, float len, inout Rng rng, out float3 x, out float3 albedo) {
    float best = len; albedo = 0.0;
    const float wanted = -log(1.0 - rng.next());
    if (fog_depth_between(a, a + dir * len) > wanted) { best = fog_distance(a, dir, len, wanted); albedo = fog_albedo(); }
    const uint count = fog_box_count();
    for (uint i = 0; i < count; ++i) {
        float t; float3 boxAlbedo;
        if (fog_box_collide(fog_box(i), a, dir, best, rng, t, boxAlbedo) && t < best) { best = t; albedo = boxAlbedo; }
    }
    x = a + dir * best;
    return best < len;
}
// The boxes' transmittance toward a light far away (the sun's, the sky's) or a lamp tmax away.
float fog_boxes_toward(float3 x, float3 l, float tmax, inout Rng rng) { return fog_box_count() != 0u ? fog_boxes_transmittance(x, l, tmax, INVALID, rng) : 1.0; }
// ---- The light the mist scatters more than once: cells of its own in the radiance cache (2026-10-11)
// What arrives at a point of the fog from the fog around it (the light every other point of the mist scatters toward it:
// the second order of scattering and every one after it), averaged over directions, is held per cell, in the radiance
// cache's own buffers (pt_common; pt_cache resolves these cells as it does the surfaces', a moving average over at most
// CACHE_FRAMES frames), in cells of a seventh kind (side 6 beside a surface's six), four times the size of a surface's cell
// at the same distance from the camera. It is read where a camera ray's fog point lies, and fed by a quarter of the
// screen's 8 x 8 blocks each frame, as the surfaces' cells are (cache_feeds): from the fog point one ray in a direction
// drawn over the whole sphere, flying as far as the fog draws (free flight, the fog's own odds), and there the light that
// reaches that point straight (fog_light) and what that point's cell holds, times the albedo. Where the flight meets a
// surface or leaves the fog first, the sample is 0 (the light surfaces send into the fog: a gap). So the cells' light
// builds up order by order over the frames, as the surfaces' bounced light does through theirs. The phase function's
// shape is not kept for these orders (the mean over directions stands for each: light scattered many times has nearly
// forgotten its way); a cell is an average over its size (the radiance cache's own approximation).
uint fog_cache_level(float3 x) { return min(cache_level(x) + 2u, 15u); }
uint2 fog_cache_key(float3 x, uint level) {
    const int3 c = int3(floor((x + g.cacheOrigin.xyz) / (CACHE_CELL * exp2(float(level)))));
    const uint h = pcg(asuint(c.x) ^ pcg(asuint(c.y) ^ pcg(asuint(c.z) ^ pcg(level | (6u << 4)))));
    return uint2((h % (CACHE_SLOTS / CACHE_BUCKET)) * CACHE_BUCKET, pcg(h ^ 0x6A09E667u) | 1u);
}
// The cell's light at x, moved at random within a cell's width first (its edges become noise, not steps); 0 where none yet.
float3 fog_cache_light(float3 x, inout Rng rng) {
    const uint level = fog_cache_level(x);
    const float3 jitter = (float3(rng.next2(), rng.next()) - 0.5) * (CACHE_CELL * exp2(float(level)));
    const uint2 key = fog_cache_key(x + jitter, level);
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer cells = ResourceDescriptorHeap[g.cache.z];
    for (uint i = 0; i < CACHE_BUCKET; ++i) {
        if (keys.Load((key.x + i) * 4u) != key.y) continue;
        const uint4 v = cells.Load4((key.x + i) * 16u);
        return (v.w & 0xFFFFu) != 0u ? asfloat(v.xyz) : 0.0;
    }
    return 0.0;
}
// One sample into x's cell (pt_common cache_add's way: a slot taken if the cell has none, a full bucket drops it).
void fog_cache_add(float3 x, float3 radiance, inout Rng rng) {
    float3 v = to_storage(radiance);
    if (!all(and(v >= 0.0, v < 1e30))) return;
    v = min(v, CACHE_SAMPLE_MOST) * CACHE_SCALE;
    const uint2 key = fog_cache_key(x, fog_cache_level(x));
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer sums = ResourceDescriptorHeap[g.cache.y];
    uint slot = INVALID;
    for (uint i = 0; i < CACHE_BUCKET && slot == INVALID; ++i) if (keys.Load((key.x + i) * 4u) == key.y) slot = key.x + i;
    for (uint j = 0; j < CACHE_BUCKET && slot == INVALID; ++j) {
        uint prior;
        keys.InterlockedCompareExchange((key.x + j) * 4u, 0u, key.y, prior);
        if (prior == 0u || prior == key.y) slot = key.x + j;
    }
    if (slot == INVALID) return;
    uint count;
    sums.InterlockedAdd(slot * 16u + 12u, 1u, count);
    if (count >= CACHE_SAMPLES_PER_FRAME) return;
    const uint3 q = uint3(v + rng.next());
    sums.InterlockedAdd(slot * 16u, q.x); sums.InterlockedAdd(slot * 16u + 4u, q.y); sums.InterlockedAdd(slot * 16u + 8u, q.z);
}

// How far a ray from x along d goes before it meets the traced scene (its closest hit; tmax where it meets nothing).
float scene_distance(float3 x, float3 d, float tmax) {
    ray_seed(x, d);
    RayDesc ray; ray.Origin = x; ray.Direction = d; ray.TMin = 0.0; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, light_mask(MASK_ALL), ray);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? q.CommittedRayT() : tmax;
}

// ---- The lamps' light in the fog along a way (2026-10-11): equiangular sampling
// The height fog's extinction at x (per metre), as height_fog_depth integrates it: the Fog volume's own, constant below its
// base height and thinning as e^-((h - base) / H) above it.
float height_fog_density(float3 x) {
    const float k = g.fog.x, base = g.fog.y, rcpH = g.fog.z;
    if (!(k > 0.0) || !(k < 1e6) || !(rcpH >= 0.0)) return 0.0;
    const float h = x.y + g.cacheOrigin.y;
    return k * (h < base ? 1.0 : exp(-(h - base) * rcpH));
}
// The medium's scattering at x (per metre, per channel): the height fog's (height: x lies where the way crosses it) and every
// fog box's.
float3 fog_scattering_at(float3 x, bool height) {
    float3 scattering = height ? fog_albedo() * height_fog_density(x) : 0.0;
    const uint count = fog_box_count();
    for (uint i = 0; i < count; ++i) { float3 s; fog_box_extinction(fog_box(i), x, s); scattering += s; }
    return scattering;
}
// The chance the medium has at most to scatter a way (the height fog's, 1 less throughHeight, and each box's bound's).
float fog_way_share(float3 a, float3 dir, float lenBoxes, float throughHeight) {
    float share = saturate(1.0 - throughHeight);
    const uint count = fog_box_count();
    for (uint i = 0; i < count; ++i) share += fog_box_share(fog_box(i), a, dir, lenBoxes);
    return share;
}
// The light lamps and glowing surfaces send into the fog along a way and the fog scatters toward the way's start (a along
// dir over [0, len]; the height fog's part within lenHeight, the boxes' over the whole way). A lamp's light in fog is
// brightest where the way passes it (1/r^2), which a point drawn as the fog scatters (free flight) finds seldom: a torch's
// mist was grain (night, flicker 2.9). So: one lamp or glowing surface, chosen among the candidates of the lists at anchor
// (a point on the way where the fog scatters serves best. Every list holds every light that falls off with distance
// alone, near or far, so for those any anchor is unbiased. NOT for a light with a range, an effect's, nor for a glowing
// surface: only the lists of the light grid's cells within its reach hold it, and where the anchor falls outside them
// its light on this way is lost: PT-132, open for the ways of reflected and refracted rays, which this serves; the
// camera's own fog asks the lists of every cell its way crosses: pt_froxel fog_lamps_residual)
// by the light it can send into the whole way (its light where the way passes nearest, times its squared distance
// there and the angle the way spans from it, over its distance from the way's line: the integral of 1/r^2 along the way),
// and one point of the way drawn in proportion to 1/r^2 from it (equiangular sampling, Kulla and Fajardo 2012). There: its
// light straight (its traced shadow ray, HDRP's fog and the boxes on its way), the phase function, the medium's scattering
// and the transmittance back to the way's start. Exact on average (but for PT-132, above). Directional lights stay
// fog_light's (lamps false).
float3 fog_lamps_along(float3 a, float3 dir, float len, float lenHeight, float3 anchor, float anisotropy, inout Rng rng) {
    LightLists lists;
    if (!(len > 0.0) || !light_lists(anchor, lists)) return 0.0;
    const uint candidates = lists.candidates;
    const float u = rng.next();
    float wsum = 0.0, chosenTarget = 0.0;
    LightRecord chosen = (LightRecord)0;
    float3 chosenPoint = 0.0; float2 chosenU = 0.0;
    for (uint c = 0; c < candidates; ++c) {
        float invPdf;
        const uint picked = lists_candidate(lists, c, u, invPdf);
        if (!(invPdf > 0.0)) continue; // weight 0 (a far candidate its cell's list holds too: weight 0, review-perf-astra 7)
        LightRecord lamp = load_light(picked);
        if (lamp.type == 2u) continue; // a directional light: fog_light's
        if (lamp.type == LIGHT_CLUSTER) lamp = load_light(cluster_triangle(lamp, rng.next(), invPdf));
        const float2 pu = rng.next2();
        float3 y = lamp.position;
        if (lamp.type == LIGHT_TRIANGLE) { float2 st; y = triangle_point(lamp, pu, st); }
        const float along = dot(y - a, dir);
        const float lineDistance = max(length(y - (a + dir * along)), 1e-3);
        const float span = atan2(len - along, lineDistance) - atan2(-along, lineDistance);
        const float3 nearest = a + dir * clamp(along, 0.0, len);
        float3 l, yy; float dist;
        const float3 e = lamp.type == LIGHT_TRIANGLE ? triangle_incident(lamp, nearest, pu, l, dist, yy) : light_incident(lamp, nearest, l, dist);
        // (A small floor: a spot's cone or a surface's back may hide it from the nearest point and not from the rest.)
        const float target = max(luminance(e) * dist * dist, 1e-6) * span / lineDistance;
        const float w = target * invPdf;
        wsum += w;
        if (w > 0.0 && rng.next() * wsum < w) { chosenTarget = target; chosen = lamp; chosenPoint = y; chosenU = pu; }
    }
    if (!(chosenTarget > 0.0)) return 0.0;
    const float along = dot(chosenPoint - a, dir);
    const float lineDistance = max(length(chosenPoint - (a + dir * along)), 1e-3);
    const float from = atan2(-along, lineDistance), to = atan2(len - along, lineDistance);
    const float t = clamp(along + lineDistance * tan(lerp(from, to, rng.next())), 0.0, len);
    const float offset = t - along;
    const float pdf = lineDistance / (max(to - from, 1e-12) * (lineDistance * lineDistance + offset * offset));
    const float3 x = a + dir * t;
    float3 l, y; float dist;
    const float3 e = chosen.type == LIGHT_TRIANGLE ? triangle_incident(chosen, x, chosenU, l, dist, y) : light_incident(chosen, x, l, dist);
    if (!any(e > 0.0)) return 0.0;
    const float3 scattering = fog_scattering_at(x, t < lenHeight);
    if (!any(scattering > 0.0)) return 0.0;
    float tmax; float3 toLamp;
    if (chosen.type == LIGHT_TRIANGLE) toLamp = triangle_shadow_ray(x, chosenPoint, tmax);
    else toLamp = shadow_ray(chosen, x, rng.next2(), tmax);
    const float3 visible = transmittance(x, toLamp, 0.0, tmax) * fog_boxes_toward(x, toLamp, tmax, rng);
    if (!any(visible > 0.0)) return 0.0;
    const float through = exp(-fog_depth_between(a, a + dir * min(t, lenHeight))) *
                          (fog_box_count() != 0u ? fog_boxes_transmittance(a, dir, t, INVALID, rng) : 1.0);
    return scattering * through * e * fog_phase(anisotropy, dot(l, dir)) * visible / pdf * (wsum / (float(candidates) * chosenTarget));
}

// The traced fog over a reflected or refracted ray's own way (fogged's form while the fog is traced, FLAG_TRACED_FOG): what
// the ray met dimmed by the fog's transmittance over the way (to HDRP's Max Fog Distance over the sky, as the camera's fog),
// and the light the fog scatters back along it: with the chance the fog has to scatter the ray at all over that way (1 less
// the transmittance), at one point of it drawn as the fog draws it (free flight), the light that point scatters toward the
// ray's start (fog_light, and the mist's cell for every later order), times the albedo. The mean is the fog's light over the
// way exactly, as the camera's own fog takes it; a ray through little fog pays for the light only that rarely (a short
// reflection indoors: almost never). Until 2026-10-11 mirrors took HDRP's formula: the sky's colour times a tint (fogged).
// The game's fog boxes too (PT-112): over the whole way, to the sky as well (they end where they end).
float3 traced_fog_on_way(float3 radiance, float3 from, float3 dir, float dist, inout Rng rng) {
    const bool sky = !(dist < HIT_DISTANCE_MISS * 0.5);
    const float reach = sky ? (g.fogColor.w > 0.0 && g.fogColor.w < 1e7 ? g.fogColor.w : 0.0) : dist;
    const float boxReach = sky ? g.params.y : dist;
    float depth = reach > 0.0 ? fog_depth_between(from, from + dir * reach) : 0.0;
    if (!(depth >= 0.0) || !(depth < 1e30)) depth = 0.0;
    const bool boxes = fog_box_count() != 0u;
    if (!(depth > 1e-6) && !boxes) return radiance;
    const float through = exp(-depth);
    const float surfaceTransmission = through * (boxes ? fog_boxes_transmittance(from, dir, boxReach, INVALID, rng) : 1.0);
#ifdef PT_PSR_CAPTURE
    g_psrFogTransmission = surfaceTransmission;
#endif
    float3 result = radiance * surfaceTransmission;
    float3 x, weight;
    const bool scatters = fog_scatter_point(from, dir, reach, boxReach, through, true, rng, x, weight);
    if (scatters) {
        float3 scattered = fog_light(x, dir, fog_anisotropy(), rng, false);
        if (cache_on()) scattered += fog_cache_light(x, rng);
        result += weight * scattered;
    }
    // The lamps' light along the way (fog_lamps_along), paid for with the chance the medium has to scatter at all (a short
    // reflection indoors: almost never), divided by it.
    const float keep = min(fog_way_share(from, dir, boxReach, through), 1.0);
    if (keep > 0.0 && rng.next() < keep)
        result += fog_lamps_along(from, dir, boxReach, reach, scatters ? x : from, fog_anisotropy(), rng) / keep;
    return result;
}
