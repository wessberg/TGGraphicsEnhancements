// SPDX-License-Identifier: MIT
// PT-127: primary surface replacement, as NVIDIA NRD's README sets it up ("INTERACTION WITH PRIMARY SURFACE
// REPLACEMENTS"): where the camera sees a pure mirror (no roughness, no diffuse colour: a delta event) that stands
// still, NRD is told of the first surface the mirror shows instead: its depth and motion in the mirror's virtual world
// (on the pixel's own view ray, as far behind the mirror as the surface is in front of it), its normal mirrored, its
// roughness and material, its diffuse and its specular light apart, each over its material factor, with hit distances
// that start at that surface. The rule wherever NRD denoises and the tracer fires its own camera rays (tracer.cpp makes
// the records then; no setting). In the game, 2026-10-11 (run 20261011-213531, a tavern's mirror, the world running): the
// detail the mirror's picture keeps under NRD 2.41 against 0.47 without (no denoiser: 3.78).
// One record a pixel, 64 bytes, written only where the camera's ray met such a mirror: the camera pass marks the pixel
// (psr_primary), the reflections' pass fills it (pt_psr_capture.hlsli) and pt_nrd_prep hands it to NRD and takes NRD's
// result back. A record counts only in the frame that wrote it (its serial against g.psr.w): nothing is cleared.
// Not here (docs/agents/psr-2.md): glass and water (two delta branches, each needing guides and a signal of its own), a
// mirror seen in a mirror, a mirror that moves or is curved, hair and skin and see-through things seen in a mirror.
#pragma once
float3 unpack_normal(uint p); // pt_common, below this file's include
float3 unpack_normal16(uint p);
struct PsrRecord {
    float4 motionDepth; // this frame's texture coordinate minus the previous one's, the previous view depth minus this one's, this view depth
    uint4 light;        // half floats: x diffuse r, g; y diffuse b, its hit distance; z specular r, g; w specular b, its hit distance (storage units, times the mirror's throughput)
    uint4 surface;      // x the shading normal (pack_normal; the reflections' pass mirrors it), y the view direction in the mirror's world (16 bits) and the throughput's blue (half); z roughness (half), NRD's material class (8 bits), the state (8); w the frame's serial
    uint4 material;     // half floats: x diffuse colour r, g; y its b and the specular colour's r; z the specular colour's g, b; w the mirror's throughput r, g
};
// A record's state, in the frame of its serial: the camera's ray met a still pure mirror; its reflection's end is an
// ordinary surface and its light is in the record; the same, with the end's own reflection ray's distance; complete.
static const uint PSR_CANDIDATE = 1u, PSR_CAPTURED = 2u, PSR_REFLECTED = 3u, PSR_VALID = 4u;
bool psr_on() { return g.psr.x != INVALID; }
uint psr_index(uint2 p) { return p.y * g.size.x + p.x; }
uint psr_halves(float a, float b) { return f32tof16(min(a, 65504.0)) | (f32tof16(min(b, 65504.0)) << 16u); }
float2 psr_floats(uint v) { return float2(f16tof32(v), f16tof32(v >> 16u)); }
uint psr_state_word(float roughness, uint materialClass, uint state) { return f32tof16(roughness) | ((materialClass & 0xFFu) << 16u) | (state << 24u); }
uint psr_state(uint4 surface) { return surface.w == g.psr.w ? surface.z >> 24u : 0u; }
float4 psr_diffuse(PsrRecord r) { return float4(psr_floats(r.light.x), psr_floats(r.light.y)); }
float4 psr_specular(PsrRecord r) { return float4(psr_floats(r.light.z), psr_floats(r.light.w)); }
uint4 psr_light(float4 diffuse, float4 specular) { return uint4(psr_halves(diffuse.x, diffuse.y), psr_halves(diffuse.z, diffuse.w), psr_halves(specular.x, specular.y), psr_halves(specular.z, specular.w)); }
float3 psr_normal(PsrRecord r) { return unpack_normal(r.surface.x); }
float3 psr_view(PsrRecord r) { return unpack_normal16(r.surface.y & 0xFFFFu); }
float psr_roughness(PsrRecord r) { return f16tof32(r.surface.z); }
float psr_class(PsrRecord r) { return float((r.surface.z >> 16u) & 0xFFu); }
float3 psr_albedo(PsrRecord r) { return float3(psr_floats(r.material.x), f16tof32(r.material.y)); }
float3 psr_f0(PsrRecord r) { return float3(f16tof32(r.material.y >> 16u), psr_floats(r.material.z)); }
float3 psr_throughput(PsrRecord r) { return float3(psr_floats(r.material.w), f16tof32(r.surface.y >> 16u)); }
// Whether NRD is told of another surface than the camera's at this pixel (one row read where it is not).
bool psr_replaced(uint2 p) {
    if (!psr_on()) return false;
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    return psr_state(records[psr_index(p)].surface) == PSR_VALID;
}
PsrRecord psr_load(uint2 p) {
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    return records[psr_index(p)];
}
