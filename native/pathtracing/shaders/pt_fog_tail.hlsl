// SPDX-License-Identifier: MIT
// PT-118: what the cells do not hold of the camera's fog, per pixel, after pt_fog_integrate (a pass of its own: its ray
// queries must not occupy every thread of the walk through the slices): the lamps' and glowing surfaces' light right
// by them, over the pixel's whole way (pt_froxel fog_lamps_residual: the share of their light that changes within a
// cell, which no cell can hold).
// (Until 2026-10-12 this pass also traced the way past the cells' 20 m, one point a pixel: pt_froxel.hlsli says what
// that cost; the cells reach the far distance now.)
// pass.a: camera fog UAV (read/modify/write).
#include "pt_common.hlsli"
#include "pt_froxel.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    if (!(g.fog.x > 0.0) && fog_box_count() == 0u) return;
    const Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    float3 dir; float heightEnd;
    if (s.valid) { const float3 d = s.position - g.cameraPos.xyz; heightEnd = length(d); dir = d / max(heightEnd, 1e-6); }
    else { dir = normalize(camera_relative_position(p, 1.0)); heightEnd = g.fogColor.w > 0.0 && g.fogColor.w < 1e7 ? g.fogColor.w : 0.0; }
    const float boxEnd = s.valid ? heightEnd : g.params.y;
    const float end = fog_box_count() != 0u ? max(heightEnd, boxEnd) : heightEnd;
    const float viewRate = max(mul(g.motionViewProj, float4(dir, 0.0)).w, 1e-6);
    Rng rng = make_rng(p, g.size.z, 0x7461696cu);
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass.a.x];
    float4 fog = output[p];
    // (Its shadow ray is paid for by chance where it could add less than a hundredth of the cells' fog here, or of a
    // thousandth of white: fog_lamps_residual.)
    const float level = 0.01 * luminance(fog.rgb) * reference_radiance() + 0.001 / max(exposure() * traced_gain(), 1e-12);
    fog.rgb += to_storage(fog_lamps_residual(dir, end, min(end, heightEnd), viewRate, level, rng));
    output[p] = fog;
}
