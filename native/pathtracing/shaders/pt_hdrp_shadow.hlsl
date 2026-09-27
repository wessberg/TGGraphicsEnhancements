// SPDX-License-Identifier: MIT
// Traced shadows on the surfaces HDRP shades itself (hair, eyes: where pt_prepare's camera ray found one, a surface HDRP
// shades or a character's forward pixel, and left it to HDRP; not what the traced scene lacks, which may be unlit, a glow
// card or an impostor): HDRP lights them with the lights the tracer traces (their dimmers carry the traced gain) and its
// ambient light, but shadows only the lights it has shadow maps for (the sun's cascades, a lamp or two), so a candle
// behind a wall lit an NPC's hair while the traced face beside it was dark. The share of HDRP's light that the traced
// shadows leave, R = (K + D' + A) / (K + D + A), is what the composite multiplies HDRP's color by (never above 1): D the
// direct light (Lambert, on HDRP's normal) of the lights HDRP doesn't shadow, D' the part of it the pixel sees, K the light
// of those it shadows itself, which stays (the sun as far as the traced sun reaches the pixel, standing in for HDRP's
// cascades; a lamp HDRP shadows, whole), A the bounced light arriving there (the radiance cache's). Without K, a candle
// behind a sunlit head took the sun's light with its own: the whole pixel was multiplied by A / (D + A), and hair and eyes
// in daylight went nearly black. Without the cache (its row off), or where its cell holds nothing yet, A is unknown and
// nothing is darkened. Emissive surfaces are left out: they light nothing HDRP shades.
// A frame traces one shadow ray a pixel: to a light drawn by how much it gives there, among up to eight of its light grid
// cell's and the sun (resampled importance sampling: the ray sees D'/D on average), and one to the sun where HDRP shadows
// it. The pixels feed the radiance cache like the GBuffer's (their bounced light's first ray, on their quarter of the
// frames). The filter pass averages that over the pixel's 3x3 neighbours on the same surface and over time, along HDRP's
// motion.
//   pass.a: this frame's shares (UAV, R16), the history (this frame's, UAV RG16: share, frames), the previous frame's
//           history (UAV RG16), 0 trace / 1 filter   pass.b.x (filter): 0 with the previous frame's history, 1 without
#include "pt_common.hlsli"

#define LIGHT_HDRP_SHADOWS 0x10000u // TGPT_LIGHT_HDRP_SHADOWS: HDRP shadows this light itself
static const float HDRP_SHADOW_RANGE = 60.0; // as far as pt_prepare looks for surfaces HDRP draws forward
static const float HISTORY = 32.0;           // frames the history averages over at most

bool hdrp_shaded(Surface s) { return s.valid && !s.deferred && s.hdrpShaded && s.distance < HDRP_SHADOW_RANGE; }

// The radiance cache's light for a point's cell (cache_light), and whether the cell holds any.
bool cache_known(float3 position, float3 normal, inout Rng rng, out float3 radiance) {
    radiance = 0.0;
    const uint level = cache_level(position);
    float3 t, b; basis(normal, t, b);
    const float2 u = rng.next2() - 0.5;
    const uint2 key = cache_key(position + (t * u.x + b * u.y) * (CACHE_CELL * exp2(float(level))), normal, level);
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer light = ResourceDescriptorHeap[g.cache.z];
    for (uint i = 0; i < CACHE_BUCKET; ++i) {
        if (keys.Load((key.x + i) * 4u) != key.y) continue;
        const uint4 v = light.Load4((key.x + i) * 16u);
        if ((v.w & 0xFFFFu) == 0u) return false;
        radiance = asfloat(v.xyz);
        return true;
    }
    return false;
}

void trace_share(uint2 p) {
    RWTexture2D<float> shares = ResourceDescriptorHeap[pass.a.x];
    const Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!hdrp_shaded(s) || !cache_on() || (g.settings2.y & OPTION_NO_HDRP_SHADOWS) != 0) { shares[p] = 1.0; return; }
    Rng rng = make_rng(p, g.size.z, 9);
    const float offset = ray_offset(s.distance);
    const float3 origin = s.position + s.geoNormal * offset;
    const float nearBack = near_back(s.distance);
    // The candidates: the sun unless HDRP shadows it (its cascades), and the cell's lights HDRP has no shadow map for
    // (wsum). The light of those HDRP shadows is kept.
    float wsum = 0.0, kept = 0.0;
    bool sun = false;
    LightRecord chosen = (LightRecord)0;
    if (g.sunColor.w > 0.0) {
        const float w = luminance(g.sunColor.rgb) * saturate(dot(s.normal, g.sunDir.xyz)) / PI;
        if (g.sunColor.w < 1.5) { wsum += w; sun = w > 0.0; }
        else if (w > 0.0 && !occluded_from_surface(origin, cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2()), 0.0, g.params.y, nearBack)) kept += w;
    }
    uint first, count;
    if (light_cell(s.position, first, count)) {
        const uint candidates = min(count, 8u);
        const float u = rng.next();
        for (uint c = 0; c < candidates; ++c) {
            float invPdf;
            const LightRecord light = load_light(cell_candidate(first, count, candidates, c, u, invPdf));
            if (light.type == LIGHT_CLUSTER) continue; // emissive triangles: no light of HDRP's
            float3 l; float dist;
            const float w = luminance(light_incident(light, s.position, l, dist)) * saturate(dot(s.normal, l)) / PI * invPdf / float(candidates);
            if ((light.flags & LIGHT_HDRP_SHADOWS) != 0) { kept += w; continue; }
            wsum += w;
            if (w > 0.0 && rng.next() * wsum < w) { chosen = light; sun = false; }
        }
    }
    float3 bounced;
    const bool known = cache_known(s.position, s.geoNormal, rng, bounced);
    // Its bounced light's first ray feeds the cache, as a GBuffer surface's does (pt_gi): drawn about the surface's own
    // normal, which a cell's light is the mean about, and gathering for the surface, whose emissive lights are sampled
    // (emission_weight: the share of a glowing surface's light the cache holds is the same whichever pass fed it).
    if (cache_feeds(p, 3u)) {
        const float3 dir = cosine_sample(s.geoNormal, rng.next2());
        g_pathFrom = s.position; g_pathGathers = true;
        Hit hit;
        float3 radiance;
        if (trace_from_surface(origin, dir, 0.0, g.params.y, nearBack, hit)) radiance = clamp_radiance(shade_path(hit, dir, 1u, rng), LIMIT_INDIRECT);
        else radiance = clamp_radiance(sky_radiance(dir), LIMIT_INDIRECT);
        cache_add(s.position, s.geoNormal, radiance, rng);
    }
    if (!(wsum > 0.0) || !known) { shares[p] = 1.0; return; }
    bool visible;
    if (sun) visible = !occluded_from_surface(origin, cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2()), 0.0, g.params.y, nearBack);
    else {
        float tmax;
        const float3 dir = shadow_ray(chosen, origin, rng.next2(), tmax);
        visible = !occluded_from_surface(origin, dir, 0.0, max(tmax - offset, 0.0), nearBack);
    }
    const float a = max(luminance(bounced), 0.0);
    shares[p] = visible ? 1.0 : saturate((kept + a) / (kept + wsum + a));
}

void filter_share(uint2 p) {
    RWTexture2D<float> shares = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float2> history = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float2> previous = ResourceDescriptorHeap[pass.a.z];
    const Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!hdrp_shaded(s)) { history[p] = float2(1.0, 0.0); return; }
    // The pixel's neighbours on the same surface (hair's strands lie at every depth: by position alone).
    const float near = 0.02 + 0.01 * s.distance;
    float sum = 0.0, n = 0.0;
    [unroll] for (int dy = -1; dy <= 1; ++dy) [unroll] for (int dx = -1; dx <= 1; ++dx) {
        const int2 q = int2(p) + int2(dx, dy);
        if (any(q < 0) || any(q >= int2(g.size.xy))) continue;
        const Surface sq = load_surface(uint2(q), g.surface.x, g.surface.z, g.material.x);
        if (!hdrp_shaded(sq) || length(sq.position - s.position) > near) continue;
        sum += shares[q]; n += 1.0;
    }
    const float current = n > 0.0 ? sum / n : shares[p];
    // The previous frame's at the pixel's place then (HDRP's motion), from the four texels around it that held the surface.
    float before = 0.0, frames = 0.0, weights = 0.0;
    if ((g.size.w & FLAG_RESET) == 0 && pass.b.x == 0u) {
        Texture2D<float2> motion = ResourceDescriptorHeap[g.inputs1.y];
        const float2 prevPixel = ((float2(p) + 0.5) / float2(g.size.xy) - motion.Load(int3(p, 0))) * float2(g.size.xy) - 0.5;
        const int2 base = int2(floor(prevPixel));
        const float2 f = prevPixel - float2(base);
        [unroll] for (int i = 0; i < 4; ++i) {
            const int2 o = int2(i & 1, i >> 1), q = base + o;
            if (any(q < 0) || any(q >= int2(g.size.xy))) continue;
            Surface sp = load_surface(uint2(q), g.surface.y, g.surface.w, g.extra.y);
            sp.position += g.prevOriginDelta.xyz;
            if (!hdrp_shaded(sp) || length(sp.position - s.position) > near * 2.0) continue;
            const float w = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
            const float2 h = previous[q];
            before += h.x * w; frames += h.y * w; weights += w;
        }
    }
    if (weights > 0.01) { before /= weights; frames /= weights; } else frames = 0.0;
    frames = min(frames + 1.0, HISTORY);
    const float share = saturate(lerp(before, current, 1.0 / frames));
    history[p] = float2(isfinite(share) ? share : 1.0, frames);
}

void hdrp_shadow_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    if (pass.a.w == 0u) trace_share(p); else filter_share(p);
}
[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) { hdrp_shadow_pixel(p); }
