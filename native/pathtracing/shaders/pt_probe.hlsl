// SPDX-License-Identifier: MIT
// How much of the sky the camera sees, for what HDRP still shades itself (transparent surfaces such as glass, smoke and
// water, and fog), whose ambient light and reflection probes see the sky with no walls in between: 256 rays from the
// camera in every direction (a Fibonacci sphere turned a little every frame), the sky's light along those that reach it
// against the sky's along all of them. The ratio (1 in the open, 0 in a closed room) scales HDRP's ambient light,
// reflections and fog ambient (tracer.cpp reads it back; HdrpPasses applies it). It counts the sky only: HDRP's ambient
// is the sky's light, and counting the lamps' light the paths found too made it rise in a lit room as if the sky were
// there. The rays start 30 cm out, past the player's own head.
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

groupshared float3 s_traced[64];
groupshared float3 s_sky[64];

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint t : SV_GroupIndex) {
    const float turn = float(pcg(g.size.z * 2654435761u + 7u) & 0xFFFFu) * (6.28318531 / 65536.0);
    float3 seen = 0.0, sky = 0.0;
    [unroll] for (uint k = 0; k < 4; ++k) {
        const uint i = t * 4u + k;
        const float z = 1.0 - 2.0 * (float(i) + 0.5) / 256.0;
        const float r = sqrt(saturate(1.0 - z * z));
        const float phi = float(i) * 2.39996323 + turn;
        const float3 dir = float3(r * cos(phi), z, r * sin(phi));
        const float3 open = clamp_radiance(sky_radiance(dir), LIMIT_INDIRECT);
        Hit hit;
        if (!trace_closest(g.cameraPos.xyz, dir, 0.3, g.params.y, hit)) seen += open;
        sky += open;
    }
    s_traced[t] = seen; s_sky[t] = sky;
    GroupMemoryBarrierWithGroupSync();
    if (t == 0) {
        float3 sumTraced = 0.0, sumSky = 0.0;
        for (uint j = 0; j < 64u; ++j) { sumTraced += s_traced[j]; sumSky += s_sky[j]; }
        RWStructuredBuffer<float4> probe = ResourceDescriptorHeap[pass.a.x];
        probe[0] = float4(sumTraced / 256.0, 1.0);
        probe[1] = float4(sumSky / 256.0, 1.0);
        // And what the traced scene has under the screen's centre, along the camera ray (for the F8 capture's report),
        // as camera rays see it (eyes and decals included).
        float3 centre = camera_relative_position(g.size.xy / 2u, 1e-7);
        float3 dir = normalize(centre);
        Hit pick;
        g_cameraRays = true;
        const bool found = trace_closest(g.cameraPos.xyz, dir, 0.0, g.params.y, pick);
        g_cameraRays = false;
        // With the surface there as HDRP drew it and as the tracer prepared it (pt_prepare): 1 found, 2 glass seen through,
        // 4 a GBuffer surface, 8 drawn forward; HDRP's roughness from bit 8 and the tracer's from bit 16 (0 to 255 each).
        Texture2D<float4> nb = ResourceDescriptorHeap[g.inputs0.y];
        const uint2 middle = g.size.xy / 2u;
        const Surface here = load_surface(middle, g.surface.x, g.surface.z, g.material.x);
        const uint bits = (found ? 1u : 0u) | (here.glass ? 2u : 0u) | (here.valid && here.deferred && !here.forward ? 4u : 0u) | (here.forward ? 8u : 0u) |
            (uint(saturate(nb.Load(int3(middle, 0)).a) * 255.0 + 0.5) << 8) | (uint(saturate(here.roughness) * 255.0 + 0.5) << 16);
        probe[2] = found ? float4(asfloat(pick.instance), asfloat(pick.material), length(pick.position - g.cameraPos.xyz), asfloat(bits)) : float4(0.0, 0.0, 0.0, asfloat(bits));
    }
}
