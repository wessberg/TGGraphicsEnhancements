// SPDX-License-Identifier: MIT
// The radiance cache's frame (pt_common.hlsli): each cell takes the samples paths left in it this frame into the light it
// holds, a moving average over its samples of at most CACHE_FRAMES frames, and a cell no path has added to for
// CACHE_MAX_AGE frames is given up. pass.a.x 1: every cell emptied instead (a reset: another place, another scene).
#include "pt_common.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(256, 1, 1)]
void main(uint i : SV_DispatchThreadID) {
    if (i >= CACHE_SLOTS || !cache_on()) return;
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer sums = ResourceDescriptorHeap[g.cache.y];
    RWByteAddressBuffer light = ResourceDescriptorHeap[g.cache.z];
    if (pass.a.x == 1u) { keys.Store(i * 4u, 0u); sums.Store4(i * 16u, 0u); light.Store4(i * 16u, 0u); return; }
    if (keys.Load(i * 4u) == 0u) return;
    const uint4 s = sums.Load4(i * 16u);
    uint4 v = light.Load4(i * 16u);
    const uint history = v.w & 0xFFFFu;
    uint age = v.w >> 16;
    if (s.w > 0u) {
        sums.Store4(i * 16u, 0u);
        const float n = float(min(s.w, CACHE_SAMPLES_PER_FRAME));
        const float3 mean = float3(s.xyz) / (n * CACHE_SCALE) * reference_radiance();
        // Weighted by samples, and at least 1 / CACHE_FRAMES this frame's: a cell few paths reach still follows the light.
        const float total = float(history) + n;
        const float3 before = asfloat(v.xyz);
        const float3 after = history == 0u ? mean : lerp(before, mean, max(n / total, 1.0 / CACHE_FRAMES));
        v = uint4(asuint(after), uint(min(total, 65535.0)));
    } else if (++age > CACHE_MAX_AGE) {
        keys.Store(i * 4u, 0u);
        v = 0u;
    } else v.w = (age << 16) | history;
    light.Store4(i * 16u, v);
}
