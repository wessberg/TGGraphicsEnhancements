// SPDX-License-Identifier: MIT
// ReSTIR GI (Ouyang et al. 2021): initial samples, then NVIDIA RTXDI's temporal reuse (pt_rtxdi.hlsli). Each deferred
// pixel traces cosine-distributed rays from HDRP's surface (one to sixteen, the plugin's rays per pixel; one that a
// normal map points into the surface itself counts as a sample that found no light, since mirroring it back out would
// double its direction's density without its weight saying so); the path vertex
// each finds is shaded (sun, a light that reaches it, more diffuse bounces) and the candidates compete by their target
// function, the winner becoming this frame's sample (a ray into the sky: a point far along it). RTXDI then resamples
// the previous frame's reservoir around the reprojected pixel, permuted every frame for the denoisers, weighted by the
// reconnection Jacobian, within the GI history (in frames) and a maximum sample age (with TGPT_OPTION_GI_VISIBILITY: the
// previous pixel counted for the chosen sample only if it sees it, and no age limit; tracer.cpp). The boiling filter runs
// next (pt_gi_boil), then the spatial pass. The rays' own mean (their light over pi, as they are cosine-distributed) goes to
// the spatial pass in the diffuse signal, which takes a share of it as there are more of them (pt_gi_spatial).
#define RTXDI_GI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#include "pt_rtxdi.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/GI/TemporalResampling.hlsli"

void gi_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    RAB_Surface surface = RAB_GetGBufferSurface(int2(p), false);
    if (!RAB_IsSurfaceValid(surface)) { RTXDI_StoreGIReservoir(RTXDI_EmptyGIReservoir(), g.rtxdiReservoirs, p, g.rtxdi.z); return; }
    Surface s = surface.s;

    Rng rng = make_rng(p, g.size.z, 1);
    float offset = ray_offset(s.distance);
    float3 origin = s.position + s.geoNormal * offset;
    const uint rays = clamp(g.grid.w, 1u, 16u);
    // Feeding the radiance cache: the first ray's path its last vertex's cell, and that ray the surface's own (one ray
    // a pixel, whatever the rays per pixel: more cost more without filling the cache any better).
    const bool feeds = cache_feeds(p, 0u);
    // The rays gather this surface's diffuse light, whose emissive lights ReSTIR DI samples at its position (the paths
    // count what that leaves: emission_weight).
    g_pathFrom = RAB_GetSurfaceWorldPos(surface); g_pathGathers = true;
    float wsum = 0.0, selectedTarget = 0.0;
    float3 selectedPosition = 0.0, selectedNormal = 0.0, selectedRadiance = 0.0, mean = 0.0;
    for (uint k = 0; k < rays; ++k) {
        float3 dir = cosine_sample(s.normal, rng.next2());
        const bool above = dot(dir, s.geoNormal) > 0.0;
        const bool feed = feeds && k == 0;
        Hit hit;
        float3 radiance = 0.0, position = 0.0, normal = 0.0;
        const bool found = above && trace_from_surface(origin, dir, 0.0, g.params.y, near_back(s.distance), hit);
        PT_REORDER(found ? hit.material : 0xFFFFu); // with SER: the path's shading, grouped by material
        if (found) {
            radiance = clamp_radiance(shade_path(hit, dir, g.settings.z, rng, feed), LIMIT_INDIRECT);
            position = hit.position; normal = hit.normal;
        } else if (above) {
            radiance = clamp_radiance(sky_radiance(dir), LIMIT_INDIRECT);
            position = s.position + dir * GI_SKY_DISTANCE; normal = -dir;
        }
        // A cell's light is the mean about the surface's own normal (geoNormal), as its other feeders draw their rays; this ray
        // was drawn about the shading normal: weighted by the ratio of the two densities (at most 4), and counted as no light
        // where it points below the surface. Left out there, the cache averaged the rays above it alone.
        if (feed) {
            const float cs = dot(s.normal, dir);
            cache_add(s.position, s.geoNormal, radiance * (cs > 1e-3 ? min(saturate(dot(s.geoNormal, dir)) / cs, 4.0) : 0.0), rng);
        }
        mean += radiance;
        float cosTheta = saturate(dot(s.normal, dir));
        float target = luminance(radiance) * cosTheta;
        // Source density cos/pi: the candidate's weight is target / density.
        float w = cosTheta > 0.0 ? target * PI / cosTheta : 0.0;
        wsum += w;
        if (k == 0 || (w > 0.0 && rng.next() * wsum < w)) { selectedPosition = position; selectedNormal = normal; selectedRadiance = radiance; selectedTarget = target; }
    }
    RWTexture2D<float4> own = ResourceDescriptorHeap[g.signals.y];
    own[p] = float4(to_storage(mean / float(rays)), 0.0);
    // This frame's sample, weighted over its rays, counts as one sample (M 1, even when it carries no light, so the
    // history doesn't outweigh a frame that found darkness): RTXDI's GI history is in frames.
    RTXDI_GIReservoir r = RTXDI_MakeGIReservoir(selectedPosition, selectedNormal, selectedRadiance, 1.0);
    r.weightSum = selectedTarget > 0.0 ? wsum / (float(rays) * selectedTarget) : 0.0;
    // Path vertices are stored relative to the scene origin: when it moves (every 512 m) the history is not reused.
    if ((g.size.w & FLAG_RESET) == 0 && (g.settings2.y & OPTION_NO_TEMPORAL) == 0 && all(g.prevOriginDelta.xyz == 0.0)) {
        RTXDI_RandomSamplerState rtxdiRng = RTXDI_InitRandomSampler(p, g.size.z, 5);
        // Not permuted on a complex surface (a strand, a slat, a blade: pt_prepare), as for direct light.
        RTXDI_GITemporalResamplingParameters tparams = g.giTemporal;
        if (s.complex) tparams.enablePermutationSampling = false;
        r = RTXDI_GITemporalResampling(p, surface, rtxdi_motion(p, s), g.rtxdi.w, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, tparams);
    }
    // A reservoir that isn't finite would stay forever (it wins every comparison it takes part in, and its age never
    // grows) and spread to its neighbours through reuse: dropped, as RTXDI's DI storage does and its GI storage doesn't.
    if (!(r.weightSum < 1e30) || !all(r.radiance < 1e30)) r = RTXDI_EmptyGIReservoir();
    RTXDI_StoreGIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.z);
}
PT_ENTRY(gi_pixel, gi)
