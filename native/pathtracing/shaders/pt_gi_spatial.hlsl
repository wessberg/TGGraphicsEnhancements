// SPDX-License-Identifier: MIT
// ReSTIR GI spatial reuse (NVIDIA RTXDI; pt_rtxdi.hlsli) and shading. A few neighbours' reservoirs from this frame's
// temporal pass, on similar surfaces, compete with the pixel's own, weighted by the reconnection Jacobian (with
// TGPT_OPTION_GI_VISIBILITY each counted for the chosen sample only if it sees it: tracer.cpp); the
// survivor's path vertex is checked for visibility from this pixel's surface (no light leaks through walls between
// neighbours), and its radiance, cosine and contribution weight give the pixel's indirect diffuse light, divided by the
// diffuse color (the composite multiplies HDRP's full-resolution one back in). Spatial results feed back only with the
// option TGPT_OPTION_SPATIAL_FEEDBACK.
#define RTXDI_GI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#include "pt_rtxdi.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/GI/SpatialResampling.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> diffuse = ResourceDescriptorHeap[g.signals.y];
    RAB_Surface surface = RAB_GetGBufferSurface(int2(p), false);
    // Alpha: the length of the ray the light came along, for AMD's Ray Regeneration (negative: no surface).
    if (!RAB_IsSurfaceValid(surface)) {
        if ((g.settings2.y & OPTION_SPATIAL_FEEDBACK) != 0) RTXDI_StoreGIReservoir(RTXDI_EmptyGIReservoir(), g.rtxdiReservoirs, p, g.rtxdi.w);
        diffuse[p] = float4(0.0, 0.0, 0.0, -1.0); return;
    }
    Surface s = surface.s;
    // This frame's rays' own mean (pt_gi), before the pixel's result takes its place.
    const float3 own = diffuse[p].rgb * reference_radiance();

    RTXDI_GIReservoir r = RTXDI_LoadGIReservoir(g.rtxdiReservoirs, p, g.rtxdi.z);
    if ((g.settings2.y & OPTION_NO_SPATIAL) == 0 && g.giSpatial.numSamples > 0) {
        RTXDI_RandomSamplerState rtxdiRng = RTXDI_InitRandomSampler(p, g.size.z, 6);
        r = RTXDI_GISpatialResampling(p, surface, g.rtxdi.z, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, g.giSpatial);
    }
    // With spatial feedback (an option), the result is what the next frame's temporal reuse starts from.
    if ((g.settings2.y & OPTION_SPATIAL_FEEDBACK) != 0) RTXDI_StoreGIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.w);

    float3 result = 0.0;
    float hitDistance = HIT_DISTANCE_MISS;
    float W = r.weightSum;
    if (RTXDI_IsValidGIReservoir(r) && W > 0.0 && W < 1e20) {
        float offset = ray_offset(s.distance);
        float3 origin = s.position + s.geoNormal * offset;
        float3 toSample = r.position - origin;
        float dist = max(length(toSample), 1e-4);
        float3 dir = toSample / dist;
        bool sky = gi_sample_is_sky(r.position, origin);
        if (!sky) hitDistance = dist;
        bool blocked = occluded_from_surface(origin, dir, 0.0, sky ? g.params.y : max(dist - 0.02, 0.0), near_back(s.distance));
        if (!blocked) result = r.radiance * saturate(dot(s.normal, dir)) / PI * W;
    }
    // More rays per pixel: a share of the light is their own mean, as if the reuse's were worth REUSE_RAYS of them (the share
    // (rays - 1) / (rays - 1 + REUSE_RAYS): 30 % at 8 rays, 48 % at 16). However many rays a pixel traces, ReSTIR's reuse
    // shades it with one sample, whose error it shares with its neighbours and the next frames (boiling, which more rays
    // didn't reduce: they only competed for that sample); the rays' own mean is independent from pixel to pixel and frame to
    // frame, noise the denoisers take out. A share of (rays - 1) / rays made a frame noisier at 8 rays than at 1 (the
    // reuse's light is worth many rays). One ray keeps the reuse's light alone.
    static const float REUSE_RAYS = 16.0;
    const uint rays = clamp(g.grid.w, 1u, 16u);
    if (rays > 1u && all(isfinite(own))) result = lerp(result, max(own, 0.0), float(rays - 1u) / (float(rays - 1u) + REUSE_RAYS));
    // The capture's comparison of what boils: the rays' own mean alone, which nothing shares between pixels or frames.
    if ((g.settings2.y & OPTION_GI_OWN_RAYS) != 0) result = all(isfinite(own)) ? max(own, 0.0) : 0.0;
    // Indirect Color: the bounced light's color scaled around its brightness (1 as traced, 0 grey), which it keeps, but
    // never past the point where a channel would go negative: scaled plainly and clamped at 0, a candle's warm bounce off
    // brown wood became pure red at 3 (green and blue at 1 to 5 of 255 across the traced tavern), and brighter.
    float l = luminance(result);
    float scale = g.prevOriginDelta.w;
    [unroll] for (uint c = 0; c < 3; ++c)
        if (scale > 1.0 && result[c] < l) scale = max(1.0, min(scale, l / max(l - result[c], 1e-6)));
    result = l + (result - l) * scale;
    diffuse[p] = float4(to_storage(max(result, 0.0)), hitDistance);
}
