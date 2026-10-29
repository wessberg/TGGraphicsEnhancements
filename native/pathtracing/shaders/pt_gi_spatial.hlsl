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
    // This frame's rays' own mean (pt_gi), before the pixel's result takes its place, and its first ray's hit distance.
    const float3 own = diffuse[p].rgb * reference_radiance();
    const float ownDistance = diffuse[p].a;

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
        float offset = surface_offset(s);
        // A leaf's or a blade's sample from behind it (gathers_behind, pt_gi): seen from its far side, and lit through it with
        // its tint (leaf_tint) where the grey share weighed it.
        const bool back = gathers_behind(s) && dot(r.position - s.position, s.geoNormal) < 0.0;
        float3 origin = s.position + s.geoNormal * (back ? -offset : offset);
        float3 toSample = r.position - origin;
        float dist = max(length(toSample), 1e-4);
        float3 dir = toSample / dist;
        bool sky = gi_sample_is_sky(r.position, origin);
        if (!sky) hitDistance = dist;
        // Thin cloth between the two lets this ray through by chance, as it lets a light's (g_bounceRay, alpha_opaque), and tints
        // the sample's light (cloth_walk): what a canopy leaves of the sky for what lies under it, whichever pixel found the
        // sample (its light is kept over the chance its own ray had of passing: pt_gi's held).
        const float reach = sky ? g.params.y : max(dist - 0.02, 0.0);
        // Its own chance at the cloth (g_raySalt), apart from the ray that found the sample: its direction differs from that
        // ray's in the last bits alone, and about one in ten passed the cloth because that ray had (1.2 times the light).
        // Where the scattering walk takes the pixel (pt_walk's walk_own, the same test), it replaces this pixel's diffuse light:
        // the sample's visibility ray and shading would be thrown away, so they are not made (docs/agents/review-perf-astra.md
        // 8, 2026-10-11). Its hit distance stays, which the denoisers read.
        uint walkMaterial; float3 walkShape;
        const bool walks = own_rays() && subsurface_on() && s.valid && s.deferred && !s.glass && !s.strands && !s.hair &&
            own_material(p, walkMaterial) && subsurface_material(walkMaterial, walkShape) && own_walks(p);
        bool blocked = true;
        if (!walks) {
            g_bounceRay = true; g_clothMet = 0; g_raySalt = 0x76697369u;
            blocked = occluded_from_surface(origin, dir, 0.0, sky ? g.params.y : max(dist - 0.02, 0.0), surface_near_back(s));
            g_bounceRay = false; g_raySalt = 0;
        }
        if (!blocked) {
            float3 clothTint = 1.0; uint clothPassed;
            if (g_clothMet > 0) clothTint = cloth_walk(origin, dir, 0.0, reach, g_clothMet, clothPassed);
            const float c = dot(s.normal, dir);
            result = r.radiance * clothTint * (back ? leaf_tint(p, s) * (s.through * saturate(-c)) : (float3)saturate(c)) / PI * W;
        }
        // The distance the denoisers read (NRD's README: "hitT of a chosen sample cannot be simply passed ... must be
        // probabilistically filtered through the lens of the actual BRDF lobes ... If such hitT is rejected, in-lobe hit
        // distance must be used as the fallback"): ReSTIR's sample kept by the diffuse lobe's odds for its direction (its
        // cosine: the lobe's density over its most), else the pixel's own cosine-drawn ray's (pt_gi).
        const float lobe = saturate(back ? -dot(s.normal, dir) : dot(s.normal, dir));
        const float u = float(pcg(p.x * 7919u + p.y * 104729u + g.size.z * 2654435761u) & 0xFFFFFFu) / 16777216.0;
        if (!(u < lobe) && ownDistance > 0.0) hitDistance = ownDistance;
    } else if (ownDistance > 0.0) hitDistance = ownDistance;
    // More rays per pixel: a share of the light is their own mean, as if the reuse's were worth REUSE_RAYS of them (the share
    // (rays - 1) / (rays - 1 + REUSE_RAYS): 30 % at 8 rays, 48 % at 16). However many rays a pixel traces, ReSTIR's reuse
    // shades it with one sample, whose error it shares with its neighbours and the next frames (boiling, which more rays
    // didn't reduce: they only competed for that sample); the rays' own mean is independent from pixel to pixel and frame to
    // frame, noise the denoisers take out. A share of (rays - 1) / rays made a frame noisier at 8 rays than at 1 (the
    // reuse's light is worth many rays). One ray keeps the reuse's light alone.
    static const float REUSE_RAYS = 16.0;
    const uint rays = clamp(g.grid.w, 1u, 16u);
    if (rays > 1u && all(isfinite(own))) {
        const float share = float(rays - 1u) / (float(rays - 1u) + REUSE_RAYS);
        result = lerp(result, max(own, 0.0), share);
    }
    // The capture's comparison of what boils: the rays' own mean alone, which nothing shares between pixels or frames.
    if ((g.settings2.y & OPTION_GI_OWN_RAYS) != 0) result = all(isfinite(own)) ? max(own, 0.0) : 0.0;
    // Indirect Color (pt_common indirect_color): the bounced light's color scaled around its brightness, as far as no channel
    // goes negative.
    result = indirect_color(result);
    // Alpha: the hit distance the denoisers read (NRD, AMD Ray Regeneration).
    diffuse[p] = float4(to_storage(max(result, 0.0)), hitDistance);
}
