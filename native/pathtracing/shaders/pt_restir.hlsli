// SPDX-License-Identifier: MIT
// The radiance clamps shared by the passes that trace light (NVIDIA RTXDI does ReSTIR's reuse: pt_rtxdi.hlsli).
#pragma once

// Bounded against the scene's clamp reference (a white surface in full sun, or lit by the brightest light 25 cm away):
// fireflies from rare bright paths (a vertex right by a lamp, an emissive, the sky map's sun) would otherwise hold
// ReSTIR's reservoirs and the denoisers' history for many frames, while sunlit surfaces and those a lamp or a fire lights,
// whose light is the signal, stay within the bound (measured 1 m from the brightest light it cut the table under a
// candle, a candle-lit room's main source of bounced light). Never against HDRP's exposure: measured in pre-exposed units (and against a ten-second average of
// the exposure after that), a hall lit by a sunlit courtyard's bounce was mostly the clamp itself, about 80 times over
// it, so its bounced light rose and fell with the auto exposure as the camera turned between bright and dark.
static const float LIMIT_INDIRECT = 4.0;   // light arriving from a path vertex
static const float LIMIT_REFLECTION = 8.0; // a traced reflection
static const float LIMIT_DIRECT = 48.0;    // ReSTIR DI's direct light, sampled well enough to stay bright
float3 clamp_radiance(float3 radiance, float limit) {
    float l = luminance(radiance) / clamp_reference();
    if (!(l < 1e30)) return 0.0; // NaN or infinite: dropped (inf * 0 would make a NaN that ReSTIR keeps)
    if ((g.settings2.y & OPTION_NO_CLAMPS) != 0) return max(radiance, 0.0); // for testing: unclamped
    return (l > limit) ? radiance * (limit / l) : max(radiance, 0.0);
}
