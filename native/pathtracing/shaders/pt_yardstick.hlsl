// SPDX-License-Identifier: MIT
// The yardstick's numbers (PT-59): how many pixels' own camera-ray hit differs from HDRP's G-buffer, and how, counted from
// the word pt_prepare left for each pixel (pt_common yardstick: FrameConstants.primary.z). tracer.cpp reads the counters back
// a few frames later, logs their shares and hands them to the plugin (TGPT_GetYardstick), which names the materials.
//   pass.a.x: the counters (UAV, uints); pass.a.y: 0 clears them (thread x + 1032 y is the counter), 1 counts every pixel.
// The counters: [0..5] pixels by where the two surfaces are (YARD_SKY .. YARD_EXTRA), [6] all pixels; [8] of the pixels on the
// same surface, those with another normal, [9] colour, [10] roughness, [12] those HDRP drew forward (nothing to compare);
// [11] pixels whose own motion is more than a pixel from HDRP's; [16 + 8 kind + where] by the kind of instance hit
// (YARD_KIND_*); from YARD_COUNTERS on, YARD_PER_MATERIAL a material: its pixels, those of them that differ (another place, or
// another reading of the same surface), and how (PT-64): in another place (the tracer's hit nearer, farther or over sky: the
// geometry's, not the material's), and on the same surface another colour, another normal, another roughness. A material
// past the table's end counts in its last row.
#include "pt_common.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    RWStructuredBuffer<uint> counters = ResourceDescriptorHeap[pass.a.x];
    if (pass.a.y == 0u) {
        const uint i = p.x + p.y * 1032u;
        if (p.x < 1032u && i < YARD_COUNTERS + YARD_PER_MATERIAL * YARD_MATERIALS) counters[i] = 0u;
        return;
    }
    if (any(p >= g.size.xy) || g.primary.z == INVALID) return;
    RWTexture2D<uint> y = ResourceDescriptorHeap[g.primary.z];
    const uint code = y[p];
    const uint where = code & 7u, kind = (code >> 8) & 7u, material = code >> 12;
    InterlockedAdd(counters[min(where, 5u)], 1u);
    InterlockedAdd(counters[6], 1u);
    if ((code & YARD_MOTION) != 0u) InterlockedAdd(counters[11], 1u);
    // PT-40, who takes the scattering walk (TGPT_GetWalkPixels): [13] the pixels whose own ray took it this frame, [14] those
    // on a material light walks inside.
    if ((code & YARD_WALKS) != 0u) InterlockedAdd(counters[13], 1u);
    float3 walkShape;
    if (material < YARD_NO_MATERIAL - 1u && subsurface_material(material, walkShape)) InterlockedAdd(counters[14], 1u);
    bool differs = where == YARD_NEARER || where == YARD_FARTHER || where == YARD_EXTRA;
    if (where == YARD_SAME) {
        if ((code & YARD_NORMAL) != 0u) InterlockedAdd(counters[8], 1u);
        if ((code & YARD_COLOUR) != 0u) InterlockedAdd(counters[9], 1u);
        if ((code & YARD_ROUGH) != 0u) InterlockedAdd(counters[10], 1u);
        if ((code & YARD_FORWARD) != 0u) InterlockedAdd(counters[12], 1u);
        differs = (code & (YARD_NORMAL | YARD_COLOUR | YARD_ROUGH)) != 0u;
    }
    if (material == YARD_NO_MATERIAL) return;
    InterlockedAdd(counters[16u + 8u * min(kind, 4u) + min(where, 5u)], 1u);
    const uint row = YARD_COUNTERS + YARD_PER_MATERIAL * min(material, YARD_MATERIALS - 1u);
    InterlockedAdd(counters[row], 1u);
    if (differs) InterlockedAdd(counters[row + 1u], 1u);
    if (where == YARD_SAME) {
        if ((code & YARD_COLOUR) != 0u) InterlockedAdd(counters[row + 3u], 1u);
        if ((code & YARD_NORMAL) != 0u) InterlockedAdd(counters[row + 4u], 1u);
        if ((code & YARD_ROUGH) != 0u) InterlockedAdd(counters[row + 5u], 1u);
    } else if (differs) InterlockedAdd(counters[row + 2u], 1u);
}
