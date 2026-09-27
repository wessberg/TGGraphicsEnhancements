// SPDX-License-Identifier: MIT
// The boiling filter for this frame's GI reservoirs (pt_boil.hlsli), between the temporal and the spatial pass (a pass
// of its own: with Shader Execution Reordering the GI pass is a ray generation shader, which can't synchronize a
// thread group). A reservoir far brighter than the rest of its 8x8 group (a rare bright path about to spread to its
// neighbours as a flash) is dropped; its weight is its light times its contribution weight, as in RTXDI's.
// With TGPT_OPTION_GI_VISIBILITY it is brought down to the group's bound instead: that option keeps a light path's
// history past 30 frames (tracer.cpp), and the long-lived dim paths it keeps in a dark place pull the group's average
// down, so dropping threw away a quarter of the light of the sand under a canvas, seen through a slit (brought down to
// the bound, 3 to 7 %). Beside a light 10 cm from a wall the bound keeps 83 % of the bounced light, dropping 79 %.
#include "pt_rtxdi.hlsli"
#include "pt_boil.hlsli"
#include "Rtxdi/GI/Reservoir.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID, uint2 local : SV_GroupThreadID) {
    // Every thread of the group takes part in the filter, which synchronizes the group.
    const bool inside = all(p < g.size.xy);
    RTXDI_GIReservoir r = RTXDI_EmptyGIReservoir();
    if (inside) r = RTXDI_LoadGIReservoir(g.rtxdiReservoirs, p, g.rtxdi.z);
    const float weight = luminance(r.radiance) * r.weightSum;
    const float bound = boiling_bound(local, g.giBoiling.boilingFilterStrength, weight);
    if (bound >= 0.0 && weight > bound) {
        if ((g.settings2.y & OPTION_GI_VISIBILITY) != 0) r.weightSum *= bound / weight;
        else r = RTXDI_EmptyGIReservoir();
    }
    if (inside) RTXDI_StoreGIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.z);
}
