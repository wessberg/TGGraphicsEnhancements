// SPDX-License-Identifier: MIT
// PT-118: the camera's fog in cells. No surface pass includes this file.
// Independent implementation of the frustum mapping described by RTX Remix (report: fog-froxel.md).
#pragma once
// The cells: the render size / 16 across, and in depth
//   48 slices to FOG_REACH (20 m), spaced by an exponent of 2 (Remix's cells, its reach and its spacing), and
//   48 more from there to the far distance (g.params.y: as far as any ray goes), each deeper than the one before by one
//   factor (at the game's 1,000 km: a quarter deeper each; the fog's light changes slowly out there).
// Remix's cells reach a set distance (what it does past them is not known here: its sources are not in this repo).
// The game's fog does not end there (its Max Fog Distance is 5 km, a hill may stand further off), and until
// 2026-10-12 the way past 20 m was traced per pixel, one point a frame, with no history and no filter. Where most of
// the fog lies past 20 m (daylight outdoors) that pass was the fog, and its grain the picture's: in the game's foggy
// valley the frame-to-frame change sat over the sky, where the ways are long, 3.6 times the per-pixel route's (which
// blended a tenth of each frame into the last and filtered 5 x 5 pixels); it read the mist's cache at points nobody
// fed (the cause of 7 % and 24 % less light in the game's two daylight scenes: feeding it there brought the light
// back to 1 %); and its lamps came from the lists of one point of the way (PT-132). Now the cells hold the whole
// way: the same estimator, the same history, the same walk. In the game (runs 20261012-051553 and -053455, the
// world frozen): the valley's flicker 1.17 -> 0.17 against the per-pixel route's 0.33, calmer in every scene with
// no denoiser and under NRD, the light the same but where that route lost a ranged lamp's (docs/agents/fogrule2.md).
// Neither reach is a medium boundary: the walk (pt_fog_integrate) goes on to the way's end in its last slice whatever
// the far distance is.
static const uint FOG_NEAR_SLICES = 48u;
static const uint FOG_FAR_SLICES = 48u;
static const uint FOG_SLICES = FOG_NEAR_SLICES + FOG_FAR_SLICES;
static const uint FOG_TILE = 16u;
static const float FOG_REACH = 20.0;
float fog_far() { return clamp(g.params.y, 2.0 * FOG_REACH, 1e7); }
uint3 fog_grid_size() { return uint3((g.size.xy + FOG_TILE - 1u) / FOG_TILE, FOG_SLICES); }
// The view depth where slice s begins (s may lie between two slices), and the slice a view depth falls in.
float fog_slice_depth(float slice) {
    if (slice <= float(FOG_NEAR_SLICES)) { const float z = slice / float(FOG_NEAR_SLICES); return FOG_REACH * z * z; }
    return FOG_REACH * pow(fog_far() / FOG_REACH, (slice - float(FOG_NEAR_SLICES)) / float(FOG_FAR_SLICES));
}
float fog_depth_slice(float depth) {
    if (!(depth > FOG_REACH)) return float(FOG_NEAR_SLICES) * sqrt(max(depth, 0.0) / FOG_REACH);
    return float(FOG_NEAR_SLICES) + float(FOG_FAR_SLICES) * log(depth / FOG_REACH) / log(fog_far() / FOG_REACH);
}

float3 fog_grid_ray(float2 uv) {
    const float2 ndc = float2(2.0 * uv.x - 1.0, 1.0 - 2.0 * uv.y);
    float4 clip = float4(ndc, 1.0, 1.0);
    const float4 first = mul(g.invViewProj, clip);
    const float4 unjittered = mul(g.motionViewProj, first);
    // Remove the projection's subpixel jitter. Both projections share the same view and differ by an NDC offset.
    clip.xy += ndc - unjittered.xy / unjittered.w;
    const float4 world = mul(g.invViewProj, clip);
    const float3 ray = normalize(world.xyz / world.w);
    // One unit of view depth, rather than radial distance: cells follow the camera frustum.
    return ray / max(mul(g.motionViewProj, float4(ray, 0.0)).w, 1e-6);
}
float3 fog_grid_point(float3 cell) {
    return fog_grid_ray(cell.xy / float2(fog_grid_size().xy)) * fog_slice_depth(cell.z);
}
bool fog_grid_previous(float3 relative, out float3 uvw) {
    // prevViewProj already includes current-to-previous camera translation (the existing motion-vector contract).
    const float4 clip = mul(g.prevViewProj, float4(relative, 1.0));
    uvw = float3(clip.x / max(clip.w, 1e-6) * 0.5 + 0.5,
                 0.5 - clip.y / max(clip.w, 1e-6) * 0.5, fog_depth_slice(clip.w) / float(FOG_SLICES));
    return clip.w > 0.0 && all(uvw >= 0.0) && all(uvw <= 1.0);
}
float3 fog_grid_uvw(float3 relative) {
    const float4 clip = mul(g.motionViewProj, float4(relative, 1.0));
    return float3(clip.x / max(clip.w, 1e-6) * 0.5 + 0.5,
                  0.5 - clip.y / max(clip.w, 1e-6) * 0.5, fog_depth_slice(clip.w) / float(FOG_SLICES));
}

// ---- A lamp's light, in two parts that sum to it exactly at every point (PT-118, docs/agents/fogfix.md) A
// lamp's or a glowing surface's light falls with the square of the distance d: right by it it changes within
// millimetres, and a cell is centimetres to most of a metre deep. One value a cell cannot hold that (it
// converged to a blob the size of the cell, brighter or dimmer by where the lamp sat in its cell; and the
// mean of 1 / d^2 over a cell has no bound on a sample, so the blob boiled). So the light e a lamp gives a
// point is split by a radius r tied to the cell's size there: the cells hold e d^2 / (d^2 + r^2) (as bright
// as e far from the lamp, never above I / r^2: smooth at a cell's size), each pixel's own ray adds e r^2 /
// (d^2 + r^2) (all of e right by the lamp, falling as 1 / d^4 beyond r: fog_lamps_residual). No light is cut
// and none is counted twice: the two shares sum to 1 at every point, whatever r is. r is one and a half
// times a cell's WIDTH at the point's own view depth (16 pixels: the depth / 58 at 1080 rows), so both
// passes know it from the point alone. The width, not the depth (a cell is 5 to 20 times deeper than wide):
// the camera's ray sums the light along its way whatever the cells' depth is (a cell's value is the mean
// over the cell, and the walk's weights between slices sum to each slice's own depth), so only across the
// screen must the cells' share be smooth. Departure from RTX Remix, named (PT-127's rule): Remix puts all of
// a light into its cells and cuts what a sample has above a firefly threshold. Here nothing is cut.
static const float FOG_SMOOTH = 1.5; // r in cell widths
float fog_smooth_radius(float depth) {
    const float2 perCell = 2.0 / (float2(length(g.motionViewProj[0].xyz), length(g.motionViewProj[1].xyz)) * float2(fog_grid_size().xy));
    return FOG_SMOOTH * max(depth, 1e-4) * max(perCell.x, perCell.y);
}
float fog_smooth_share(float d2, float r) { return d2 / (d2 + r * r); }

// The stretch [t0, t1] of a way (a + dir t, t in [0, len]) that lies in the light grid's cell holding its point at t:
// every point of it has the same light lists (pt_common light_lists). Outside the grid: all of the way before it enters
// the grid or after it leaves it (one list holds every light there).
void fog_light_cell_stretch(float3 a, float3 dir, float len, float t, out float t0, out float t1) {
    t0 = 0.0; t1 = len;
    if (g.grid.x == INVALID) return;
    const float3 dims = float3(g.grid.z & 0xFF, (g.grid.z >> 8) & 0xFF, (g.grid.z >> 16) & 0xFF);
    const float3 o = (a - g.gridMin.xyz) * g.gridMin.w, d = dir * g.gridMin.w; // in cells
    const float3 c = floor(o + d * t);
    const bool inside = all(c >= 0.0) && all(c < dims);
    const float3 lo = inside ? c : float3(0.0, 0.0, 0.0), hi = inside ? c + 1.0 : dims;
    const float3 inv = 1.0 / select(abs(d) > 1e-12, d, float3(1e-12, 1e-12, 1e-12));
    const float3 ta = (lo - o) * inv, tb = (hi - o) * inv;
    const float enter = max(max(min(ta.x, tb.x), min(ta.y, tb.y)), min(ta.z, tb.z));
    const float leave = min(min(max(ta.x, tb.x), max(ta.y, tb.y)), max(ta.z, tb.z));
    if (inside) { t0 = clamp(enter, 0.0, t); t1 = clamp(leave, t, len); return; }
    if (!(leave > enter) || leave <= 0.0 || enter >= len) return; // the way never meets the grid
    if (t <= enter) t1 = clamp(enter, t, len); else t0 = clamp(leave, 0.0, t);
}

// What the cells do not hold of the lamps' and glowing surfaces' light along a camera ray's whole way (from the camera
// along dir over [0, len]; the height fog within lenHeight; viewRate: view depth a metre of the way): their light right
// by them, e r^2 / (d^2 + r^2), scattered toward the camera. Exact on average, for a light with a range or a glowing
// surface too (which only the lists of the light grid's cells within its reach hold: PT-132):
//   the way stretch by stretch, each the light grid's cell it crosses (8 m; past the grid's 384 m one stretch, whose one
//   list holds every light), with the lists there, which hold every light that reaches any point of that stretch; of all
//   the stretches' candidates ONE lamp, chosen by what this share of its light can send into its stretch (its strength
//   times the integral of 1 / d^2 - 1 / (d^2 + r^2) along it: its strength and its place alone, no cookie, window, cone or
//   texture read for a lamp that is not chosen; the choice only says how often a lamp is drawn, never how much light it
//   gives); one point of that stretch drawn in proportion to 1 / d^2 from it (equiangular sampling, Kulla and Fajardo
//   2012); there its light by its traced shadow ray, HDRP's fog and the boxes on its way, the share, the phase
//   function, the medium's scattering and the transmittance back to the camera.
// The stretches within the first 20 m of view depth are all asked (where a lamp by the camera fills the screen). Past
// them a stretch is asked with a chance (what the height fog lets through to it times 20 m over its distance, a 64th at
// least) and its lamps weigh one over that chance: a way to the horizon crosses 30 and more cells, each with nine
// candidates, and this share of a lamp's light is a few pixels wide out there. The mean is unchanged; nothing is cut.
// Most rays pass no lamp closely, and this share is then next to nothing: the shadow ray is paid for only with the
// chance the unshadowed light has against level (a light this pixel's fog would not show: the caller's), and what it
// brings is divided by that chance. The mean is unchanged; nothing is cut.
float3 fog_lamps_residual(float3 dir, float len, float lenHeight, float viewRate, float level, inout Rng rng) {
    if (!(len > 0.0) || !frame_has_lights()) return 0.0;
    const float3 a = g.cameraPos.xyz;
    float wsum = 0.0, chosenTarget = 0.0, chosenFrom = 0.0, chosenTo = 0.0;
    LightRecord chosen = (LightRecord)0;
    float3 chosenPoint = 0.0; float2 chosenU = 0.0;
    // (A straight way crosses at most the sum of the grid's three sides in cells, and the two stretches outside it.)
    const uint most = (g.grid.z & 0xFFu) + ((g.grid.z >> 8) & 0xFFu) + ((g.grid.z >> 16) & 0xFFu) + 4u;
    float t0 = 0.0;
    [loop] for (uint stretch = 0u; stretch < most && t0 < len; ++stretch) {
        const float probe = min(t0 + max(1e-3, 1e-5 * t0), len);
        float enter, t1;
        fog_light_cell_stretch(a, dir, len, probe, enter, t1);
        t1 = max(t1, probe);
        float asked = 1.0;
        if (t0 * viewRate >= FOG_REACH) {
            asked = clamp(exp(-fog_depth_between(a, a + dir * min(t0, lenHeight))) * FOG_REACH / (t0 * viewRate), 1.0 / 64.0, 1.0);
            if (rng.next() >= asked) { t0 = t1; continue; }
        }
        LightLists lists;
        if (light_lists(a + dir * probe, lists)) {
            const uint candidates = lists.candidates;
            const float u = rng.next();
            for (uint c = 0; c < candidates; ++c) {
                float invPdf;
                const uint picked = lists_candidate(lists, c, u, invPdf);
                if (!(invPdf > 0.0)) continue;
                LightRecord lamp = load_light(picked);
                if (lamp.type == 2u) continue; // a directional light has no place to be near
                // Its strength: a lamp's own; a glowing triangle's its cluster's (all its triangles' together) times its share.
                float strength = luminance(lamp.color);
                if (lamp.type == LIGHT_CLUSTER) {
                    const float whole = invPdf;
                    lamp = load_light(cluster_triangle(lamp, rng.next(), invPdf));
                    if (!(invPdf > 0.0)) continue;
                    strength *= whole / invPdf;
                }
                const float2 pu = rng.next2();
                float3 y = lamp.position;
                if (lamp.type == LIGHT_TRIANGLE) { float2 st; y = triangle_point(lamp, pu, st); }
                const float along = dot(y - a, dir);
                const float h = max(length(y - (a + dir * along)), 1e-3);
                const float r = fog_smooth_radius(clamp(along, t0, t1) * viewRate), wide = sqrt(h * h + r * r);
                const float span = (atan2(t1 - along, h) - atan2(t0 - along, h)) / h - (atan2(t1 - along, wide) - atan2(t0 - along, wide)) / wide;
                // (Floors: every lamp that can give light must be drawable.)
                const float target = max(strength, 1e-6) * max(span, 1e-9);
                const float w = target * invPdf / (float(candidates) * asked);
                wsum += w;
                if (w > 0.0 && rng.next() * wsum < w) { chosenTarget = target; chosen = lamp; chosenPoint = y; chosenU = pu; chosenFrom = t0; chosenTo = t1; }
            }
        }
        t0 = t1;
    }
    if (!(chosenTarget > 0.0) || !(chosenTo > chosenFrom)) return 0.0;
    const float along = dot(chosenPoint - a, dir);
    const float h = max(length(chosenPoint - (a + dir * along)), 1e-3);
    const float from = atan2(chosenFrom - along, h), to = atan2(chosenTo - along, h);
    const float t = clamp(along + h * tan(lerp(from, to, rng.next())), chosenFrom, chosenTo);
    const float offset = t - along;
    const float pdf = h / (max(to - from, 1e-12) * (h * h + offset * offset));
    const float3 x = a + dir * t;
    float3 l, y; float dist;
    const float3 e = chosen.type == LIGHT_TRIANGLE ? triangle_incident(chosen, x, chosenU, l, dist, y) : light_incident(chosen, x, l, dist);
    if (!any(e > 0.0)) return 0.0;
    const float share = 1.0 - fog_smooth_share(dist * dist, fog_smooth_radius(t * viewRate));
    const float3 scattering = fog_scattering_at(x, t < lenHeight);
    if (!any(scattering > 0.0)) return 0.0;
    const float through = exp(-fog_depth_between(a, a + dir * min(t, lenHeight))) *
                          (fog_box_count() != 0u ? fog_boxes_transmittance(a, dir, t, INVALID, rng) : 1.0);
    float3 light = scattering * through * e * share * fog_phase(fog_anisotropy(), dot(l, dir)) / pdf * (wsum / chosenTarget);
    const float chance = saturate(luminance(light) / max(level, 1e-30));
    if (!(chance > 0.0) || rng.next() >= chance) return 0.0;
    float tmax; float3 toLamp;
    if (chosen.type == LIGHT_TRIANGLE) toLamp = triangle_shadow_ray(x, chosenPoint, tmax);
    else toLamp = shadow_ray(chosen, x, rng.next2(), tmax);
    return light * transmittance(x, toLamp, 0.0, tmax) * fog_boxes_toward(x, toLamp, tmax, rng) / chance;
}
