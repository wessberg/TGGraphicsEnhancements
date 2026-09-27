// SPDX-License-Identifier: MIT
// A boiling filter for ReSTIR's reservoirs (the idea of NVIDIA RTXDI's, Wyman et al.), over an 8x8 thread group and
// for any wave width: RTXDI's own sums per wave into shared memory sized for waves of at least 32 lanes, and Intel's
// GPUs run 8 or 16. Boiling is a rare, very bright sample that ReSTIR's reuse spreads to its neighbours as a flash; its
// weight stands far above the average of the nonzero weights around it. Every thread of the group must call it (it
// synchronizes the group), threads outside the image with weight 0.
#pragma once

groupshared float s_boilingWeights[64];
groupshared float s_boilingSum, s_boilingCount;

// The bar above which this thread's weight is an outlier of its group: the average nonzero weight of the group's other
// threads times 10 / strength - 9 (strength 1: that average, 0.2: 41 times it); -1 without a weight of its own or with
// fewer than 4 others to compare with. RTXDI's own filter counts the thread's weight in the average it is
// compared with, so an outlier raises its own bar: with no more nonzero weights in the group than that factor (41 of 64
// at 0.2: a candle-lit room, bounce rays that mostly find darkness) it can never fire, and with all 64 an outlier had
// to stand about 112 times above the others. That is where the sparkling showed.
float boiling_bound(uint2 local, float strength, float weight) {
    float own = (weight > 0.0 && weight < 1e30) ? weight : 0.0;
    uint index = local.x + local.y * 8u;
    s_boilingWeights[index] = own;
    GroupMemoryBarrierWithGroupSync();
    // One thread sums the group (every thread summing all 64 read shared memory 4,096 times a group).
    if (index == 0u) {
        float total = 0.0, nonzero = 0.0;
        for (uint i = 0; i < 64u; ++i) { float w = s_boilingWeights[i]; total += w; nonzero += w > 0.0 ? 1.0 : 0.0; }
        s_boilingSum = total; s_boilingCount = nonzero;
    }
    GroupMemoryBarrierWithGroupSync();
    float sum = s_boilingSum, count = s_boilingCount;
    float others = count - (own > 0.0 ? 1.0 : 0.0);
    if (own <= 0.0 || others < 4.0) return -1.0;
    return (sum - own) / others * (10.0 / clamp(strength, 1e-6, 1.0) - 9.0);
}
// True when this thread's weight is an outlier of its group (above that bar).
bool boiling_outlier(uint2 local, float strength, float weight) {
    const float bound = boiling_bound(local, strength, weight);
    return bound >= 0.0 && weight > bound;
}
