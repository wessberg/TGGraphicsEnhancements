// SPDX-License-Identifier: MIT
// ReSTIR GI's reuse across frames, a pass of its own in the screen's order, right after pt_gi in every frame (PT-99,
// PT-110; the rule since 2026-10-10, docs/agents/ser-2.md and rule-2.md). It was the end of pt_gi until then, in the pass
// whose threads Shader Execution Reordering regroups: work in the screen's order after a regrouping (this pass reads the
// pixel's surface, up to five places of the previous frame's surfaces and one of its reservoirs, and writes the pixel)
// is what turns the regrouping into a loss (NVIDIA's guidance; NVIDIA's RTXDI sample keeps the two apart as well).
// pt_gi has left the frame's own sample in this frame's reservoir array, whole (pt_gi_own.hlsli). Here: RTXDI resamples
// the previous frame's reservoir around the reprojected pixel with it, by its own random numbers (the pixel, the frame,
// 5) and the parameters the tracer sets (g.giTemporal); a reservoir that is not finite is dropped; the result is stored in
// the same place, in RTXDI's own form, for every pixel pt_gi left a sample for. A thread reads and writes its own pixel
// of this frame's array and reads others of the previous frame's alone. The boiling filter (pt_gi_boil) and the spatial
// pass follow.
#define RTXDI_GI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#include "pt_rtxdi.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/GI/TemporalResampling.hlsli"
#include "pt_gi_own.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RAB_Surface surface = RAB_GetGBufferSurface(int2(p), false);
    if (!RAB_IsSurfaceValid(surface)) return; // (pt_gi stored the empty reservoir, in RTXDI's own form)
    const Surface s = surface.s;
    RTXDI_GIReservoir r = own_sample_load(p);
    // Path vertices are stored relative to the scene origin: when it moves (every 512 m) the history is not reused.
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure2): no reuse across frames, to time it
    if ((g.size.w & FLAG_RESET) == 0 && (g.settings2.y & OPTION_NO_TEMPORAL) == 0 && all(g.prevOriginDelta.xyz == 0.0) && !speed_measure2(SPEED_M2_NO_REUSE)) {
#else
    if ((g.size.w & FLAG_RESET) == 0 && (g.settings2.y & OPTION_NO_TEMPORAL) == 0 && all(g.prevOriginDelta.xyz == 0.0)) {
#endif
        // The ray cone pt_gi's own rays leave behind them, for the ray that checks a sample is seen (TGPT_OPTION_GI_VISIBILITY).
        ray_cone(pixel_angle() * s.distance, DIFFUSE_SPREAD);
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
