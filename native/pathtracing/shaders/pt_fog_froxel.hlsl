// SPDX-License-Identifier: MIT
// PT-118: the fog's light in the cells of a camera-aligned grid, as RTX Remix builds its fog (pt_froxel.hlsli: the
// cells; pt_fog_integrate: each pixel's walk through them; pt_fog_tail: the lamps' light right by them). No surface
// shader growth.
// pass.a: current volume UAV, previous volume SRV (INVALID on reset), bit 0 the light scattered more than once, the
// previous frame's reference radiance (a float's bits).
// RGB: the light scattered toward the camera, before the medium's albedo and extinction, in storage units. A: history
// length in frames (0: the cell has no light yet: the camera has seen none of it).
// What a cell holds: the sun's and the sky's light, the mist's cache (bit 0), and of every lamp and glowing surface the
// share that is smooth at a cell's size (pt_froxel fog_smooth_share); the rest of a lamp's light, right by it, is each
// pixel's own (pt_fog_tail). The lamps: 8 candidates of the point's light lists, one chosen by its light here, its
// shadow ray traced, one sample a frame, up to 254 frames of history, no bound on a sample.
// Departures from RTX Remix, named (PT-127's rule): no firefly clamp (nothing is cut); no temporal reservoirs, no
// second samples at a fresh cell and no Gaussian filter over the cells (on trial until 2026-10-12: the coordinator's
// game runs showed the same picture with and without them, PT-118); a cell's point over the whole cell, not its
// central half; only points the camera's rays read (below); cells out to the far distance, not to 20 m
// (pt_froxel.hlsli).
#include "pt_common.hlsli"
#include "pt_froxel.hlsli"

// The lamps' and glowing surfaces' share for the cells at x (radius: pt_froxel fog_smooth_share's).
float3 froxel_lamps(float3 x, float3 d, float radius, inout Rng rng) {
    const float anisotropy = fog_anisotropy();
    float3 light = 0.0;
    LightLists lists;
    if (light_lists(x, lists)) {
        float sum = 0.0, targetChosen = 0.0;
        float3 incidentChosen = 0.0, directionChosen = 0.0, pointChosen = 0.0;
        LightRecord chosen = (LightRecord)0;
        [loop] for (uint i = 0u; i < 8u; ++i) {
            // Randomly select a stratum, then sample it. lists_candidate's inverse PDF includes its stratum share;
            // this retains all far lights even with exactly eight proposals (never truncate the near/far lists).
            float invPdf;
            const uint c = min(uint(rng.next() * lists.candidates), lists.candidates - 1u);
            const uint index = lists_candidate(lists, c, rng.next(), invPdf);
            invPdf /= 8.0;
            if (!(invPdf > 0.0)) continue;
            LightRecord lamp = load_light(index);
            float3 l, y, e; float dist;
            if (lamp.type == LIGHT_CLUSTER) {
                lamp = load_light(cluster_triangle(lamp, rng.next(), invPdf));
                e = triangle_incident(lamp, x, rng.next2(), l, dist, y);
            } else { e = light_incident(lamp, x, l, dist); y = lamp.position; }
            // A directional light has no place to be near: all of it is the cells'.
            if (lamp.type != 2u) e *= fog_smooth_share(dist * dist, radius);
            const float target = luminance(e) * fog_phase(anisotropy, dot(l, d));
            const float weight = target * invPdf;
            sum += weight;
            if (weight > 0.0 && rng.next() * sum < weight) { chosen = lamp; incidentChosen = e; directionChosen = l; pointChosen = y; targetChosen = target; }
        }
        if (targetChosen > 0.0) {
            float dist; float3 l;
            if (chosen.type == LIGHT_TRIANGLE) l = triangle_shadow_ray(x, pointChosen, dist);
            else l = shadow_ray(chosen, x, rng.next2(), dist);
            light += incidentChosen * fog_phase(anisotropy, dot(directionChosen, d)) * (sum / targetChosen) *
                     transmittance(x, l, 0.0, dist) * fog_boxes_toward(x, l, dist, rng);
        }
    }
    return light;
}

float3 froxel_direct(float3 x, float3 d, float radius, inout Rng rng) {
    const float anisotropy = fog_anisotropy();
    float3 light = 0.0;
    // KEEP THIS SHAPE (2026-10-12, docs/agents/fogfix.md, PT-133; the white tavern). The sun's and the sky's light go into
    // a variable of their own and are added to light before the lamps. Written as "light += the sun's" inside "if there is
    // a sun", then the sky's, then the lamps' loop, then "light += the lamps'" (as this pass had it until then), the
    // Shader Model 6.9 variant gave cells of 1e30 storage units and more in every frame WITHOUT a sun (the game's
    // interiors; the check's "no sun" cases): light, zero on the path that skips the sun's code, came out of the
    // lamps' loop as garbage in its green channel, sometimes its red. dxc's DXIL for it is right (a phi that takes the
    // zero vector on the skipping edge); what runs it is not (driver 616.92). Which shapes trip it is not known: after
    // any change here, run the check (tests/pathtracing_fog_froxel_checks.inl), which fails by a factor of thousands.
    float3 sunSky = 0.0;
    if (g.sunColor.w > 0.0) {
        const float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
        sunSky += g.sunColor.rgb * fog_phase(anisotropy, dot(l, d)) * transmittance(x, l, 0.0, g.params.y) *
                  sun_fog(x) * fog_boxes_toward(x, l, g.params.y, rng);
    }
    {
        const float2 u = rng.next2();
        const float z = 1.0 - 2.0 * u.x, r = sqrt(max(0.0, 1.0 - z * z)), phi = 2.0 * PI * u.y;
        const float3 skyDir = float3(r * cos(phi), z, r * sin(phi));
        if (skyDir.y > 0.0 && !occluded(x, skyDir, 0.0, g.params.y))
            sunSky += sky_radiance(skyDir) * directional_fog(x, skyDir.y) * fog_boxes_toward(x, skyDir, g.params.y, rng) *
                      (4.0 * PI * fog_phase(anisotropy, dot(skyDir, d)));
    }
    light += sunSky;
    light += froxel_lamps(x, d, radius, rng);
    return light;
}

// The frame before's light at a place of its grid (uvw), between the eight cells around it, of those that have light
// (a cell the camera has seen none of has none and no history: it counts for nothing, here as in the walk). The history
// length handed on is the cells' own times the share of the place they stand for: a cell that inherits its light from
// one far corner alone trusts it little and follows its own samples soon.
float4 fog_history(Texture3D<float4> previous, float3 uvw) {
    const float3 q = uvw * float3(fog_grid_size()) - 0.5;
    const int3 lo = int3(floor(q)), most = int3(fog_grid_size()) - 1;
    const float3 f = q - floor(q);
    float4 sum = 0.0; float weights = 0.0;
    [unroll] for (uint i = 0u; i < 8u; ++i) {
        const int3 o = int3(i & 1u, (i >> 1) & 1u, (i >> 2) & 1u);
        const float4 c = previous.Load(int4(clamp(lo + o, 0, most), 0));
        const float3 s = lerp(1.0 - f, f, float3(o));
        const float w = s.x * s.y * s.z;
        if (c.a > 0.0 && all(isfinite(c))) { sum += c * w; weights += w; }
    }
    if (!(weights > 1e-4)) return 0.0;
    return float4(sum.rgb / weights, sum.a / weights * saturate(weights));
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint3 cell : SV_DispatchThreadID) {
    if (any(cell >= fog_grid_size())) return;
    RWTexture3D<float4> output = ResourceDescriptorHeap[pass.a.x];
    if (!(g.fog.x > 0.0) && fog_box_count() == 0u) { output[cell] = 0.0; return; } // no medium this frame
    Rng rng = make_rng(cell.xy, g.size.z, 0x66726fu + cell.z * 67u);
    const float3 center = float3(cell) + 0.5;
    float3 previousUv;
    const bool inPrevious = fog_grid_previous(fog_grid_point(center), previousUv);
    float4 history = 0.0;
    if (pass.a.y != INVALID && inPrevious) {
        Texture3D<float4> previous = ResourceDescriptorHeap[pass.a.y];
        history = fog_history(previous, previousUv);
        history.rgb *= asfloat(pass.a.w) / reference_radiance();
    }
    // A point drawn over the WHOLE cell: the cell's value is then the mean of the light over it, and what the camera's
    // rays read between cells sums to the light that is there. (Until 2026-10-11 over its central half, as Remix does:
    // a light that sat in a cell's middle was then counted up to twice, one near its edge not at all, for good: the grid
    // read a lamp 0.3 m from the camera 6 % too bright in the check.) Reprojection follows the cell CENTER, not this point
    // (otherwise a stationary view blends neighbouring histories and blurs each frame).
    // Only where the camera's rays read: a point past the end of the way through its own pixel (behind the surface the
    // pixel shows: in a wall, under the ground, outside the room where the sun shines; over the sky past HDRP's Max Fog
    // Distance) is no point of the cell's: the cell's value is the mean over the part of it the camera sees. Up to four
    // points are drawn until one is read (the first that is read is drawn evenly over that part; a cell seen through a
    // gap a few pixels wide then finds its light in a few frames). None read: no sample and no mist cache fed this
    // frame, the cell keeps what it has, and a cell the camera has never seen any of stays without light and history.
    // Until 2026-10-12 every point took its sample, as in Remix: a cell that straddles a wall or the ground then held the
    // light of both sides (the sunlit outside in a room's cells along its walls; under the ground no sun and no sky),
    // and the rays of every such point were traced for light no pixel reads.
    float3 at = center, relative = 0.0;
    bool read = false;
    RWTexture2D<float4> surfaces = ResourceDescriptorHeap[g.surface.x]; // (load_surface's first texture: the place, w above 0 where there is one)
    [loop] for (uint attempt = 0u; attempt < 4u && !read; ++attempt) {
        at = center + (float3(rng.next2(), rng.next()) - 0.5);
        relative = fog_grid_point(at);
        const uint2 pixel = min(uint2(at.xy / float2(fog_grid_size().xy) * float2(g.size.xy)), g.size.xy - 1u);
        const float4 seen = surfaces[pixel];
        float wayEnd;
        if (seen.w > 0.0) wayEnd = length(seen.xyz - g.cameraPos.xyz);
        else wayEnd = fog_box_count() != 0u ? g.params.y : (g.fogColor.w > 0.0 && g.fogColor.w < 1e7 ? g.fogColor.w : 0.0);
        read = length(relative) <= wayEnd;
    }
    if (!read) { output[cell] = history; return; }
    const float3 x = g.cameraPos.xyz + relative, d = normalize(relative);
    float3 light = froxel_direct(x, d, fog_smooth_radius(fog_slice_depth(at.z)), rng);
    if ((pass.a.z & 1u) != 0u && cache_on()) {
        light += fog_cache_light(x, rng);
        // Feed the mist's cache where it is read: one unbiased free-flight estimate per cell.
        const float2 u = rng.next2();
        const float z = 1.0 - 2.0 * u.x, r = sqrt(max(0.0, 1.0 - z * z)), phi = 2.0 * PI * u.y;
        const float3 w = float3(r * cos(phi), z, r * sin(phi));
        const float reach = scene_distance(x, w, g.params.y);
        float3 arriving = 0.0, y, albedo;
        if (fog_flight(x, w, reach, rng, y, albedo)) arriving = albedo * (fog_light(y, w, fog_anisotropy(), rng, true) + fog_cache_light(y, rng));
        fog_cache_add(x, arriving, rng);
    }
    // All of it, with no bound on a sample: nothing is cut. (A lamp's share here is at most its strength over r^2; the
    // part that had no bound, right by the lamp, is not the cells'.)
    const float age = min(history.a + 1.0, 254.0);
    output[cell] = float4(lerp(history.rgb, to_storage(max(light, 0.0)), 1.0 / age), age);
}
