// SPDX-License-Identifier: MIT
// ReSTIR DI (Bitterli et al. 2020): candidates, then NVIDIA RTXDI's temporal reuse (pt_rtxdi.hlsli) and a boiling filter.
// The lights the light grid lists at the pixel (its cell) compete by their unshadowed contribution: all of them
// when there are no more than eight, otherwise eight drawn by how much each can matter there (cell_candidate), each
// with a point of its disc, and one more from the far list for every light the cell does not list (light_lists). RTXDI then resamples the previous frame's reservoir at the reprojected pixel, permuted within a 4x4
// block every frame so that neighbouring pixels don't reuse the same samples (NVIDIA's advice for DLSS Ray
// Reconstruction and AMD's for Ray Regeneration, which both assume uncorrelated samples), within its temporal history
// (in frames) and with its bias correction; a boiling filter drops a reservoir far brighter than the rest of its
// thread group (a rare light spreading as a flash). The result is this frame's reservoir, which the spatial pass and
// the next frame's temporal reuse read: spatial results never feed back into time.
// No visibility reuse by default (dropping a light the pixel doesn't see, keeping its sample count, as RTXDI's
// RTXDI_StoreVisibilityInDIReservoir does): with spatial reuse's Basic normalization, which counts a neighbour's samples
// whether or not its light was dropped, it darkened lit ground within the spatial radius of every shadow (measured in
// tgpt_gpu_test: a lamp's shadow edge 14 pixels wide instead of 0.8, for 13% less error at night with 25 torches).
// With RTXDI's ray-traced bias correction, which counts a neighbour only if it sees the chosen light, the edge is still
// 4.4 pixels wide (for 7% less error at night): the option TGPT_OPTION_DI_VISIBILITY turns both on, for testing (fewer
// bright dots inside strong lights' shadows, where reuse otherwise keeps a weak visible light's large weight for frames).
// The shading pass traces the final visibility.
#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#include "pt_rtxdi.hlsli"
#include "pt_boil.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/DI/TemporalResampling.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID, uint2 local : SV_GroupThreadID) {
    // Every thread of the group reaches the boiling filter, which synchronizes the group.
    const bool inside = all(p < g.size.xy);
    RTXDI_DIReservoir r = RTXDI_EmptyDIReservoir();
    RAB_Surface surface = RAB_GetGBufferSurface(int2(p), false); // empty outside the image
    if (inside && RAB_IsSurfaceValid(surface) && frame_has_lights()) {
        Rng rng = make_rng(p, g.size.z, 3);
        LightLists lists;
        if (light_lists(surface.s.position, lists)) {
            const uint candidates = lists.candidates;
            const float u = rng.next();
            for (uint c = 0; c < candidates; ++c) {
                float invPdf;
                uint i = lists_candidate(lists, c, u, invPdf);
                // A candidate of weight 0 (a far candidate its cell's list holds too: weight 0, review-perf-astra 7) counts as streamed (its sample count, as
                // RTXDI_StreamSample's), with nothing of it loaded or evaluated.
                if (!(invPdf > 0.0)) { r.M += 1; continue; }
                RAB_LightInfo light = RAB_LoadLightInfo(i, false);
                // An emissive cluster: one of its triangles is the candidate (uv then picks a point of it).
                if (light.l.type == LIGHT_CLUSTER) { i = cluster_triangle(light.l, rng.next(), invPdf); light = RAB_LoadLightInfo(i, false); }
                float2 uv = rng.next2();
                RAB_LightSample ls = RAB_SamplePolymorphicLight(light, surface, uv);
                RTXDI_StreamSample(r, i, uv, rng.next(), RAB_GetLightSampleTargetPdfForSurface(ls, surface), invPdf);
            }
            RTXDI_FinalizeResampling(r, 1.0, r.M);
            r.M = 1; // a frame's candidates count as one sample, as RTXDI's own initial sampling does: history is in frames
            // Initial visibility (RTXDI's default, an option here): a chosen light the pixel doesn't see is dropped, its
            // sample count kept, so reuse never spreads a light into the shadow it casts.
            if ((g.settings2.y & OPTION_DI_VISIBILITY) != 0 && RTXDI_IsValidDIReservoir(r)) {
                RAB_LightSample chosen = RAB_SamplePolymorphicLight(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(r), false), surface,
                                                                    RTXDI_GetDIReservoirSampleUV(r));
                if (!rtxdi_visible(surface, chosen)) RTXDI_StoreVisibilityInDIReservoir(r, 0.0, true);
            }
        }
        if ((g.size.w & FLAG_RESET) == 0 && (g.settings2.y & (OPTION_NO_TEMPORAL | OPTION_DI_NO_REUSE)) == 0) {
            RTXDI_RandomSamplerState rtxdiRng = RTXDI_InitRandomSampler(p, g.size.z, 1);
            int2 temporalPixel;
            RAB_LightSample selected = RAB_EmptyLightSample();
            // Not permuted on a complex surface (a strand, a slat, a blade: pt_prepare), as NVIDIA's RTXDI sample does.
            RTXDI_DITemporalResamplingParameters tparams = g.diTemporal;
            if (surface.s.complex) tparams.enablePermutationSampling = false;
            r = RTXDI_DITemporalResampling(p, surface, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs,
                                           rtxdi_motion(p, surface.s), g.rtxdi.y, tparams, temporalPixel, selected);
        }
    }
    if (g.diBoiling.enableBoilingFilter) {
        // Compared by the light it gives (its weight W times its target function: the unshadowed light the pixel gets),
        // not by W alone as RTXDI's own filter does: W is large where a weak light was chosen, so the stricter filter
        // (pt_boil) dropped weak lights' light instead of fireflies.
        if (boiling_outlier(local, g.diBoiling.boilingFilterStrength, r.weightSum * r.targetPdf)) r = RTXDI_EmptyDIReservoir();
    }
    if (inside) RTXDI_StoreDIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.x);
}
