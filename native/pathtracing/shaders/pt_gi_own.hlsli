// SPDX-License-Identifier: MIT
// The frame's own sample of ReSTIR GI on its way from pt_gi (the rays and their shading, the pass Shader Execution
// Reordering regroups) to pt_gi_reuse (RTXDI's reuse across frames, in the screen's order), in this frame's reservoir
// array, with nothing rounded that the reuse reads (PT-99, PT-110; the two passes apart since 2026-10-10,
// docs/agents/ser-2.md and rule-2.md). A stored reservoir holds its position and its weight whole, its light and its
// normal packed. A frame's own sample counts 1 and has age 0, so three words of its record say nothing yet: the count's,
// the packed light's and the spare one. Its light goes into them whole. Its normal is packed as every record's (the reuse
// does not read the frame's own sample's normal, it only stores it again, packed the same way). The reuse pass reads the
// sample back and ALWAYS stores a record of RTXDI's own form in its place, before any other pass reads the array: the
// tracer dispatches pt_gi_reuse right after pt_gi, in every frame (tracer.cpp, the bounced light's stage).
void own_sample_store(uint2 p, RTXDI_GIReservoir r) {
    RTXDI_PackedGIReservoir packed = RTXDI_PackGIReservoir(r, 0);
    packed.packed_radiance = asuint(r.radiance.x); packed.unused = r.radiance.y; packed.packed_miscData_age_M = asuint(r.radiance.z);
    RTXDI_StorePackedGIReservoir(packed, g.rtxdiReservoirs, p, g.rtxdi.z);
}
RTXDI_GIReservoir own_sample_load(uint2 p) {
    const RTXDI_PackedGIReservoir packed = rtxdi_gi_reservoirs()[RTXDI_ReservoirPositionToPointer(g.rtxdiReservoirs, p, g.rtxdi.z)];
    RTXDI_GIReservoir r = RTXDI_EmptyGIReservoir();
    r.position = packed.position;
    r.normal = RTXDI_DecodeNormalizedVectorFromSnorm2x16(packed.packed_normal);
    r.radiance = float3(asfloat(packed.packed_radiance), packed.unused, asfloat(packed.packed_miscData_age_M));
    r.weightSum = packed.weight;
    r.M = 1; r.age = 0;
    return r;
}
