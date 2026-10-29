// SPDX-License-Identifier: MIT
// ReSTIR GI (Ouyang et al. 2021), this frame's samples. Each deferred pixel traces cosine-distributed rays from HDRP's
// surface (one to sixteen, the plugin's rays per pixel; one that a normal map points into the surface itself counts as a
// sample that found no light, since mirroring it back out would double its direction's density without its weight saying
// so); the path vertex each finds is shaded (sun, a light that reaches it, more diffuse bounces) and the candidates compete
// by their target function, the winner becoming this frame's sample (a ray into the sky: a point far along it), which is
// stored whole for the next pass (pt_gi_own.hlsli). Then, in passes of their own, in the screen's order: NVIDIA RTXDI's
// temporal reuse (pt_gi_reuse: the previous frame's reservoir around the reprojected pixel, permuted every frame for the
// denoisers, weighted by the reconnection Jacobian, within the GI history (in frames) and a maximum sample age; with
// TGPT_OPTION_GI_VISIBILITY: the previous pixel counted for the chosen sample only if it sees it, and no age limit;
// tracer.cpp), the boiling filter (pt_gi_boil), then the spatial pass. The rays' own mean (their light over pi, as they are
// cosine-distributed) goes to the spatial pass in the diffuse signal, which takes a share of it as there are more of them
// (pt_gi_spatial).
// Shader Execution Reordering (PT_SER: this pass as a ray generation shader; PT-99, PT-110, the rule since 2026-10-10,
// docs/agents/ser-2.md and rule-2.md): the threads are regrouped ONCE, by the material the pixel's first ray met (since round
// 20), after that ray is traced and before its hit is fetched, outside the loop over the pixel's rays, and nothing in the
// screen's order follows in this pass (the reuse across frames is pt_gi_reuse's). What a thread carries across the call is
// the cost: the call inside that loop (with RTXDI's reuse in this pass) kept 2,560 bytes a thread by the driver's own count,
// and this arrangement 0. The game, run 20261010-130621, the same picture, no denoiser (the bounced light's stage, ms; a tavern, a
// town, a beach): 9.5 / 22.7 / 28.9, against 41.9 / 57.7 / 61.5 with the call inside the loop and 13.7 / 27.6 / 64.6 as
// a plain compute pass. The compute passes (Shader Model 6.6 and 6.9, a GPU that does not reorder) run the same
// computation without the call.
#define RTXDI_GI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#include "pt_rtxdi.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/GI/TemporalResampling.hlsli"
#include "pt_gi_own.hlsli"

#include "pt_query.hlsli" // QueryHit, query_from_surface (the reflections' pass reads it too, 2026-10-11)

void gi_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    RAB_Surface surface = RAB_GetGBufferSurface(int2(p), false);
    if (!RAB_IsSurfaceValid(surface)) { RTXDI_StoreGIReservoir(RTXDI_EmptyGIReservoir(), g.rtxdiReservoirs, p, g.rtxdi.z); return; }
    Surface s = surface.s;

    Rng rng = make_rng(p, g.size.z, 1);
    float offset = surface_offset(s);
    // A leaf or a blade of grass gathers the light from behind it too, as HDRP adds the far side's light times its transmittance
    // (gathers_behind): a share of its rays, as large as the far side's share of its light (its grey share against the front's
    // 1), leaves from its far side, cosine-distributed about the reversed normal. Each ray's light is weighed by its side's
    // share of the rays, and the far side's by the leaf's tint (leaf_tint) for its own mean; ReSTIR's target weighs it with the
    // grey share (RAB_GetGISampleTargetPdfForSurface), and pt_gi_spatial's final shading tints it. Gathered on the camera's side
    // alone, a leaf seen from below under an overcast sky, where all light is the sky's, got almost none from above.
    float behind = gathers_behind(s) ? s.through : 0.0;
    float backShare = behind / (1.0 + behind);
#ifndef PT_NO_LAMP_AROUND
    // ON TRIAL (pt_common lamp_around; CMake's TGPT_LAMP_AROUND, OFF: no line of it here): what stands right by a lamp, found
    // on purpose for this surface (not a leaf's, whose rays leave from both sides: left to chance), once a pixel whatever its
    // rays, before the rays draw theirs; each ray below then counts the rest of that light at what it meets (g_nearPending).
    // nearLight: its share of the mean the rays bring; it is a candidate of the reservoir's like a ray's, its point the sample's.
    g_pathFrom = RAB_GetSurfaceWorldPos(surface); g_pathGathers = true;
    NearRay around = near_ray(s.position, s.normal, s.geoNormal, s.normal, 0.0);
    float3 nearLight = 0.0, nearDir = 0.0, nearPoint = 0.0, nearNormal = 0.0;
    if (lamp_around() > 0.0 && !(behind > 0.0)) {
        around.alpha = -1.0;
        nearLight = finite_radiance(lamp_around_light(around, offset, surface_near_back(s), false, false, rng, nearDir, nearPoint, nearNormal));
    }
#endif
    // THE PIXEL'S FIRST RAY, ahead of the loop over its rays: drawn as the loop's first turn would draw it (the same random
    // numbers, in the same order) and traced; what it met is not fetched yet.
    // Thin cloth in the ray's way lets it through its weave by chance (g_bounceRay, alpha_opaque: CLOTH_TRANSMISSION), as it
    // lets a light's ray through: the sky's light under a canopy. The sample keeps its light over the chance its ray had of
    // getting there (held, below: 1 / 0.3 for each cloth passed, 1 / 0.7 where cloth stopped it: cloth_stops), and the ray
    // that checks it is seen passes cloth by the same chance and takes its tint (pt_gi_spatial): so a neighbour's sample
    // reused across a canopy's edge is dimmed by the canopy as this pixel's own is. arrives: what the ray brings this surface
    // now (the passed cloth's tint, read before the path's own rays), for its own mean and the cache.
    bool firstBack = false;
    if (behind > 0.0) firstBack = rng.next() < backShare;
    const float3 firstDir = cosine_sample(firstBack ? -s.normal : s.normal, rng.next2());
    g_bounceRay = true; g_clothMet = 0;
    ray_cone(pixel_angle() * s.distance, DIFFUSE_SPREAD); // a diffuse bounce: what it meets is read over a wide footprint (PT-46)
    QueryHit met;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure): the ray, or the shading of what it met, left out
    const bool firstFound = dot(firstDir, firstBack ? -s.geoNormal : s.geoNormal) > 0.0 && !speed_measure(SPEED_M_NO_RAY) && !speed_measure2(SPEED_M2_NO_RAY) &&
        query_from_surface(s.position + (firstBack ? -s.geoNormal : s.geoNormal) * offset, firstDir, 0.0, g.params.y, surface_near_back(s), met);
#else
    const bool firstFound = dot(firstDir, firstBack ? -s.geoNormal : s.geoNormal) > 0.0 &&
        query_from_surface(s.position + (firstBack ? -s.geoNormal : s.geoNormal) * offset, firstDir, 0.0, g.params.y, surface_near_back(s), met);
#endif
    g_bounceRay = false;
    const uint firstCloth = g_clothMet;
#ifdef PT_SER
    // THE REGROUPING (Shader Execution Reordering), once, with no hint of our own, outside the rays' loop, before the hit is
    // fetched. Across it a thread carries the first ray's direction, its hit as the query gave it, how much thin cloth it met
    // and the random state; not the pixel's surface, not the fetched hit, not the loop's sums (they begin after it). The pixel
    // and its surface are read again after it (the same textures, which no pass writes meanwhile: the same numbers), and what
    // follows from the surface is computed again. Never a call inside a loop of this pass, nor work in the screen's order
    // after it (the header).
    // The key (round 20, PT-99): the material the first ray met and whether the pixel feeds the radiance cache (regroup_key,
    // 16 bits), as the reflections' pass has it, worked out before the call and not kept after it. Run 20261011-060805, the
    // world running, the same picture: the tavern 32.2 -> 30.6 ms of the tracer, the forest 82.3 -> 78.1. (The study of
    // 10-10 that found a key 20 ms dearer than none was made while this pass carried 2,560 bytes a thread across the
    // call; it carries 0 since rule-2.)
    uint material = 0xFFFFu;
    if (firstFound) {
        StructuredBuffer<InstanceRecord> keyInstances = ResourceDescriptorHeap[g.scene0.x];
        StructuredBuffer<uint> keyMaterials = ResourceDescriptorHeap[g.scene1.x];
        material = keyMaterials[keyInstances[met.instance].materialBase + met.geometry];
    }
    dx::MaybeReorderThread(regroup_key(material, cache_feeds(p, 0u)) & 0xFFFFu, 16);
    p = DispatchRaysIndex().xy;
    surface = RAB_GetGBufferSurface(int2(p), false); s = surface.s;
    offset = surface_offset(s);
    behind = gathers_behind(s) ? s.through : 0.0;
    backShare = behind / (1.0 + behind);
#endif
    const uint rays = clamp(g.grid.w, 1u, 16u);
    // Feeding the radiance cache: the first ray's path its last vertex's cell, and that ray the surface's own (one ray
    // a pixel, whatever the rays per pixel: more cost more without filling the cache any better).
    const bool feeds = cache_feeds(p, 0u);
    // The rays gather this surface's diffuse light, whose emissive lights ReSTIR DI samples at its position (the paths
    // count what that leaves: emission_weight).
    g_pathFrom = RAB_GetSurfaceWorldPos(surface); g_pathGathers = true;
    const float3 backTint = behind > 0.0 ? leaf_tint(p, s) : float3(1.0, 1.0, 1.0);
    // The first ray's hit, fetched here (with SER: after the regrouping).
    Hit firstHit = (Hit)0;
    if (firstFound) {
        const float3 start = s.position + (firstBack ? -s.geoNormal : s.geoNormal) * offset;
        firstHit = fetch_hit(met.instance, met.geometry, met.primitive, met.bary, met.toWorld, firstDir, start + firstDir * met.t, start);
    }
    float wsum = 0.0, selectedTarget = 0.0;
    float3 selectedPosition = 0.0, selectedNormal = 0.0, selectedRadiance = 0.0, mean = 0.0;
    for (uint k = 0; k < rays; ++k) {
        // The first turn takes the ray traced ahead of the loop; a later turn (more rays a pixel) draws and traces its own,
        // hit and all.
        bool back = firstBack;
        if (k > 0) { back = false; if (behind > 0.0) back = rng.next() < backShare; }
        const float3 sn = back ? -s.normal : s.normal, gn = back ? -s.geoNormal : s.geoNormal;
        const float3 origin = s.position + gn * offset;
        float3 dir = firstDir;
        if (k > 0) dir = cosine_sample(sn, rng.next2());
        const bool above = dot(dir, gn) > 0.0;
        const bool feed = feeds && k == 0 && !back;
        Hit hit = firstHit;
        bool found = firstFound;
        uint clothMet = firstCloth;
        ray_cone(pixel_angle() * s.distance, DIFFUSE_SPREAD);
        if (k > 0) {
            g_bounceRay = true; g_clothMet = 0;
#ifdef PT_SPEED_MEASURE
            found = above && !speed_measure(SPEED_M_NO_RAY) && !speed_measure2(SPEED_M2_NO_RAY) && trace_from_surface(origin, dir, 0.0, g.params.y, surface_near_back(s), hit);
#else
            found = above && trace_from_surface(origin, dir, 0.0, g.params.y, surface_near_back(s), hit);
#endif
            g_bounceRay = false;
            clothMet = g_clothMet;
        } else ray_seed(origin, dir); // the first ray's own number and start, as its trace left them (the cloth's walk reads them)
        float3 clothTint = 1.0; uint clothPassed = 0;
        if (clothMet > 0) clothTint = cloth_walk(origin, dir, 0.0, found ? max(length(hit.position - origin) - 1e-3, 0.0) : g.params.y, clothMet, clothPassed);
        const float held = pow(1.0 / CLOTH_TRANSMISSION, float(clothPassed)) * (found && cloth_stops(hit) ? 1.0 / (1.0 - CLOTH_TRANSMISSION) : 1.0);
        const float3 arrives = clothPassed > 0 ? clothTint * pow(CLOTH_TRANSMISSION, float(clothPassed)) : float3(1.0, 1.0, 1.0);
        float3 radiance = 0.0, position = 0.0, normal = 0.0;
        if (found) {
#ifndef PT_NO_LAMP_AROUND
            if (around.alpha != 0.0 && clothMet == 0) g_nearPending = around; // ON TRIAL: read and cleared by shade_path (a ray that passed thin cloth is left to chance)
#endif
#ifdef PT_SPEED_MEASURE
            if (!speed_measure(SPEED_M_NO_SHADE))
#endif
            radiance = held * finite_radiance(shade_path(hit, dir, g.settings.z, rng, feed));
            position = hit.position; normal = hit.normal;
        } else if (above) {
            radiance = held * finite_radiance(sky_radiance(dir));
            position = s.position + dir * GI_SKY_DISTANCE; normal = -dir;
        }
        // A cell's light is the mean about the surface's own normal (geoNormal), as its other feeders draw their rays; this ray
        // was drawn about the shading normal: weighted by the ratio of the two densities (at most 4), and counted as no light
        // where it points below the surface. Left out there, the cache averaged the rays above it alone.
        if (feed) {
            const float cs = dot(s.normal, dir);
#ifndef PT_NO_LAMP_AROUND
            // ON TRIAL: with what was found on purpose, weighed the same way for its own direction (the cell's light is
            // this one sample's sum of both; two samples would halve each). Off: the line below.
            if (around.alpha != 0.0) {
                const float ns = dot(s.normal, nearDir);
                cache_add(s.position, s.geoNormal, radiance * arrives * (cs > 1e-3 ? min(saturate(dot(s.geoNormal, dir)) / cs, 4.0) : 0.0) +
                          nearLight * (ns > 1e-3 ? min(saturate(dot(s.geoNormal, nearDir)) / ns, 4.0) : 0.0), rng);
            } else
#endif
            cache_add(s.position, s.geoNormal, radiance * arrives * (cs > 1e-3 ? min(saturate(dot(s.geoNormal, dir)) / cs, 4.0) : 0.0), rng);
        }
        mean += radiance * arrives * (1.0 + behind) * (back ? backTint : float3(1.0, 1.0, 1.0));
        float cosTheta = saturate(dot(sn, dir));
        float target = luminance(radiance) * cosTheta * (back ? behind : 1.0);
        // Source density cos/pi (times its side's share of the rays on a leaf): the candidate's weight is target / density.
        float w = cosTheta > 0.0 ? target * PI / (cosTheta * (back ? backShare : 1.0 - backShare)) : 0.0;
        wsum += w;
        if (k == 0 || (w > 0.0 && rng.next() * wsum < w)) { selectedPosition = position; selectedNormal = normal; selectedRadiance = radiance; selectedTarget = target; }
    }
#ifndef PT_NO_LAMP_AROUND
    // ON TRIAL: what was found on purpose adds to the rays' mean (it is not one more ray of theirs to average with: the
    // weighing already split the light between the two ways), so it counts times their number in sums divided by it.
    if (around.alpha != 0.0) {
        mean += nearLight * float(rays);
        const float cosNear = saturate(dot(s.normal, nearDir));
        const float targetNear = luminance(nearLight) * cosNear;
        const float wNear = cosNear > 0.0 ? targetNear * PI / cosNear * float(rays) : 0.0;
        wsum += wNear;
        if (wNear > 0.0 && rng.next() * wsum < wNear) { selectedPosition = nearPoint; selectedNormal = nearNormal; selectedRadiance = nearLight; selectedTarget = targetNear; }
    }
#endif
#ifdef PT_SPEED_MEASURE // the measuring build alone: the pixel's counters, its rays all shaded, go to the tracer's buffer (pt_common speed_count_flush)
    speed_count_flush(0u);
#endif
    RWTexture2D<float4> own = ResourceDescriptorHeap[g.signals.y];
    // Alpha: the first ray's hit distance (cosine-drawn: the diffuse lobe's own; HIT_DISTANCE_MISS: none), the in-lobe
    // distance pt_gi_spatial falls back on for the denoisers (NRD's README: a ReSTIR sample's distance is not simply passed).
    own[p] = float4(to_storage(mean / float(rays)), firstFound ? met.t : HIT_DISTANCE_MISS);
    // This frame's sample, weighted over its rays, counts as one sample (M 1, even when it carries no light, so the
    // history doesn't outweigh a frame that found darkness): RTXDI's GI history is in frames. Stored whole for pt_gi_reuse,
    // which resamples it with the previous frame's and stores the result in RTXDI's own form (pt_gi_own.hlsli).
    RTXDI_GIReservoir r = RTXDI_MakeGIReservoir(selectedPosition, selectedNormal, selectedRadiance, 1.0);
    r.weightSum = selectedTarget > 0.0 ? wsum / (float(rays) * selectedTarget) : 0.0;
    own_sample_store(p, r);
}
PT_ENTRY(gi_pixel, gi)
