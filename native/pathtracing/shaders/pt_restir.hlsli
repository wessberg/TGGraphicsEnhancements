// SPDX-License-Identifier: MIT
// What the passes that trace light hand on: finite light, never cut (NVIDIA RTXDI does ReSTIR's reuse: pt_rtxdi.hlsli).
#pragma once

// No radiance clamps (PT-50, the owner 2026-10-07: "There is no budgets or limits there, this is PT", "dont do shortcuts, we
// want realism and correctness"): until then light past a limit against the scene's clamp reference (a white surface in
// full sun, or lit by the brightest light 25 cm away) was cut: 48 times it for ReSTIR DI's direct light, 8 for a traced
// reflection, 4 for light arriving from a path vertex. That kept rare bright paths (a vertex right by a lamp, an emissive,
// the sky map's sun) out of ReSTIR's reservoirs and the denoisers' history, and took their light away for good. Now only
// what is not a number (NaN, infinite: inf * 0 would make a NaN that ReSTIR keeps) is dropped, and a negative channel
// rounded to 0. What would tame the rare bright paths without taking light away: more samples where they come from (light
// sampling that finds the lamp a vertex sits by, the emissive lights sampled by RTXDI), and the denoisers' own outlier
// handling, which spreads a firefly's light instead of cutting it.
float3 finite_radiance(float3 radiance) {
    return luminance(radiance) < 1e30 ? max(radiance, 0.0) : 0.0;
}
