// SPDX-License-Identifier: MIT
// PT-112, the sky (docs/agents/sky-2.md), ON TRIAL: the tracer's own sky's passes (the model: pt_sky.hlsli). Pass a.x the
// step, a.y the texture it writes (UAV); tracer.cpp record_sky runs them before every pass that reads the sky:
//   0  the transmittance table (256 x 64), when the atmosphere's numbers change
//   1  the multiple scattering's table (32 x 32, Hillaire 2020 5.5), right after it
//   2  the ground's irradiance per unit of a body's light (256), right after it
//   3  the clouds baked as HDRP bakes its Cloud Layer, when HDRP would (its baking hash)
//   4  the cloudless sky's light on the clouds from below, every frame: SKY_AMBIENT_GROUPS groups of 64 directions each, a
//      partial sum each (texels 1 on), then 7 adds them up into texel 0
//   5  the sky map every ray reads (1024 x 512 latitude-longitude, as sky_radiance reads it), every frame
//   6  one of the map's mips from the one above it (a.z: that one's UAV), every frame
#include "pt_common.hlsli"
#include "pt_sky.hlsli"

static const uint SKY_TRANSMITTANCE_STEPS = 64u, SKY_MULTIPLE_DIRECTIONS = 64u, SKY_MULTIPLE_STEPS = 24u;
static const uint SKY_GROUND_DIRECTIONS = 128u, SKY_GROUND_STEPS = 24u, SKY_AMBIENT_GROUPS = 32u, SKY_AMBIENT_STEPS = 24u; // tracer.cpp kSkyAmbientGroups
static const float SKY_CLOUD_ANISOTROPY = 0.7;   // HDRP's for the clouds' ambient probe
groupshared float3 s_ambient[64];

// A point of a set of n spread evenly over the sphere (a Fibonacci spiral).
float3 sky_sphere_point(uint i, uint n) {
    const float z = 1.0 - (2.0 * float(i) + 1.0) / float(n);
    const float r = sqrt(saturate(1.0 - z * z)), phi = float(i) * 2.39996323;
    return float3(r * cos(phi), z, r * sin(phi));
}

// 0: the transmittance from (height, cosine) to the atmosphere's top, marched in even steps.
float4 transmittance_texel(uint2 p) {
    const float2 hm = sky_transmittance_texel((float2(p) + 0.5) / float2(SKY_TRANSMITTANCE_SIZE));
    const float tEnd = sky_to_top(hm.x, hm.y), dt = tEnd / float(SKY_TRANSMITTANCE_STEPS);
    float3 depth = 0.0;
    for (uint i = 0u; i < SKY_TRANSMITTANCE_STEPS; ++i) depth += sky_medium(sky_after(hm.x, hm.y, (float(i) + 0.5) * dt).x).extinction * dt;
    return float4(exp(-depth), 1.0);
}

// 1: Hillaire's transfer for light scattered more than once, at (height, the sun's cosine): the second order's light L2 and
// the share f a unit of light at the point scatters back into it, over every direction (isotropic, as he takes it after
// many bounces), with the ground's bounce; the series sums to L2 / (1 - f).
float4 multiple_texel(uint2 p) {
    const float n = float(SKY_MULTIPLE_SIZE);
    const float mu = float(p.x) / (n - 1.0) * 2.0 - 1.0, h = float(p.y) / (n - 1.0) * sky_D();
    const float3 up = float3(0.0, 1.0, 0.0), sun = float3(sqrt(saturate(1.0 - mu * mu)), mu, 0.0);
    float3 second = 0.0, transfer = 0.0;
    const float hc = clamp(h, 0.0, sky_D() - 1.0);
    for (uint d = 0u; d < SKY_MULTIPLE_DIRECTIONS; ++d) {
        const float3 V = sky_sphere_point(d, SKY_MULTIPLE_DIRECTIONS);
        const float m0 = dot(up, V);
        const float toGround = sky_to_ground(hc, m0);
        const bool ground = toGround >= 0.0;
        const float tEnd = ground ? toGround : sky_to_top(hc, m0);
        float3 T = 1.0, L = 0.0, F = 0.0;
        for (uint i = 0u; i < SKY_MULTIPLE_STEPS; ++i) {
            const float a = float(i) / float(SKY_MULTIPLE_STEPS), b = float(i + 1u) / float(SKY_MULTIPLE_STEPS);
            const float ta = a * a * tEnd, tb = b * b * tEnd, dt = tb - ta, tm = 0.5 * (ta + tb);
            const float hm = sky_after(hc, m0, tm).x;
            const float3 nrm = normalize(up * (sky_R() + hc) + V * tm);
            const SkyMedium m = sky_medium(hm);
            const float3 scatter = m.air + m.aerosol;
            const float3 stepT = exp(-m.extinction * dt);
            const float3 over = T * (1.0 - stepT) / m.extinction;
            L += over * scatter * sky_transmittance(hm, dot(nrm, sun)) / (4.0 * PI);
            F += over * scatter;
            T *= stepT;
        }
        if (ground) {
            const float3 gn = normalize(up * (sky_R() + hc) + V * tEnd);
            const float c = dot(gn, sun);
            L += T * g.sky[6].rgb / PI * sky_transmittance(0.0, c) * saturate(c);
        }
        second += L; transfer += F;
    }
    // Over the sphere with the isotropic phase: 4 pi / n directions times 1 / (4 pi).
    second /= float(SKY_MULTIPLE_DIRECTIONS); transfer /= float(SKY_MULTIPLE_DIRECTIONS);
    return float4(second / max(1.0 - transfer, 1e-3), 1.0);
}

// 2: the ground's irradiance with a body of unit light at the cosine: straight through the atmosphere, and the sky above it
// (cosine-weighted directions over the hemisphere: pi times their mean radiance).
float4 ground_texel(uint2 p) {
    const float n = float(SKY_GROUND_SIZE);
    const float mu = float(p.x) / (n - 1.0) * 2.0 - 1.0;
    const float3 up = float3(0.0, 1.0, 0.0), sun = float3(sqrt(saturate(1.0 - mu * mu)), mu, 0.0);
    float3 e = sky_transmittance(0.0, mu) * saturate(mu);
    float3 sky = 0.0;
    for (uint d = 0u; d < SKY_GROUND_DIRECTIONS; ++d) {
        // Cosine-weighted: an even disc lifted onto the hemisphere.
        const float r = sqrt((float(d) + 0.5) / float(SKY_GROUND_DIRECTIONS)), phi = float(d) * 2.39996323;
        const float3 V = float3(r * cos(phi), sqrt(saturate(1.0 - r * r)), r * sin(phi));
        sky += sky_march(0.0, up, V, SKY_GROUND_STEPS, 0.0, true, sun, true).light;
    }
    e += PI * sky / float(SKY_GROUND_DIRECTIONS);
    return float4(e, 1.0);
}

// 3: the clouds as HDRP's Cloud Layer bakes them: per layer, per direction of its map, its opacity from the map's channels
// by their weights; lit, the sun's light it scatters towards the eye through a slab 800 m thick at its altitude.
float cloud_map_opacity(SkyLayer layer, float2 uv) {
    if (layer.map < 0 || g.textures.x == INVALID) return 0.0;
    Texture2D<float4> map = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + uint(layer.map))];
    const float4 w = layer.opacities / max(dot(layer.opacities, 1.0), 1.0);
    return dot(map.SampleLevel(s_linearWrap, uv, 0), w);
}
// Where the layer's slab lies along a direction from the ground: its start and its length.
float2 cloud_slab(float3 dir, float altitude) {
    const float start = sky_cloud_distance(dir, altitude);
    return float2(start, sky_cloud_distance(dir, altitude + SKY_CLOUD_SLAB) - start);
}
// The layer's density at a point (relative to the ground point under the camera): HDRP's, a shell around the slab's middle
// as thick as the map's opacity in that direction.
float cloud_density(SkyLayer layer, float3 position) {
    const float3 dir = normalize(position);
    float2 uv = sky_cloud_uv(dir); uv.x -= layer.rotation;
    const float thickness = cloud_map_opacity(layer, uv);
    const float2 slab = cloud_slab(dir, layer.altitude);
    const float fromMiddle = length(dir * (slab.x + 0.5 * slab.y) - position);
    return fromMiddle > slab.y * thickness ? 0.0 : 0.1 * thickness;
}
float2 cloud_texel(uint2 p, uint width, uint rows) {
    const uint l = p.y / rows;
    const SkyLayer layer = sky_layer(l);
    float2 uv = (float2(p.x, p.y - l * rows) + 0.5) / float2(width, rows);
    uv.x -= layer.rotation;
    const float opacity = cloud_map_opacity(layer, uv);
    const int steps = int(layer.steps + 0.5);
    if (steps <= 0) return opacity;
    const float3 dir = sky_cloud_direction(uv);
    const float angle = layer.rotation * 2.0 * PI;
    const float3 sun = sky_turn_up(g.sky[10].xyz, float2(cos(angle), sin(angle)));
    const float c = dot(dir, sun);
    float phase[2];
    for (uint o = 0u; o < 2u; ++o) { const float k = pow(0.75, float(o)); phase[o] = sky_henyey_greenstein(0.7 * k, c) + sky_henyey_greenstein(-0.3 * k, c); }
    if (!(opacity > 0.0)) return 0.0;
    const float2 slab = cloud_slab(dir, layer.altitude);
    const float3 position = dir * (slab.x + 0.5 * (1.0 - opacity) * slab.y);
    // Towards the sun from there to the slab's top (at most 500 m), in the layer's steps.
    const float R = sky_R(), top = layer.altitude + SKY_CLOUD_SLAB;
    const float3 q = position + float3(0.0, R, 0.0);
    const float r = length(q), mu = dot(q, sun) / r;
    const float inside = top * (2.0 * R + top) - (2.0 * R * position.y + dot(position, position)); // top's r^2 less this r^2
    const float exitDist = clamp(r * (-mu + sqrt(max(mu * mu + inside / (r * r), 0.0))), 0.0, 500.0);
    const float step = exitDist / float(steps);
    float extinction = 0.0;
    for (int j = 1; j <= steps; ++j) extinction += cloud_density(layer, position + sun * step * float(j));
    float luminance = 0.0;
    for (uint o2 = 0u; o2 < 2u; ++o2) { const float k = pow(0.75, float(o2)); luminance += exp(-step * extinction * layer.sigma * k) * phase[o2] * k; }
    const float through = exp(-0.1 * opacity * layer.sigma * slab.y * opacity);
    return float2(luminance * (1.0 - through), 1.0 - through);
}

// 5: the map's direction for its texel, as sky_radiance reads it (u from atan2(x, z), v from the zenith down).
float3 map_direction(uint2 p, uint2 size) {
    const float2 uv = (float2(p) + 0.5) / float2(size);
    const float phi = (uv.x - 0.5) * 2.0 * PI, theta = uv.y * PI;
    return float3(sin(theta) * sin(phi), cos(theta), sin(theta) * cos(phi));
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID, uint t : SV_GroupIndex, uint2 group : SV_GroupID) {
    const uint mode = pass.a.x;
    if (mode == 4u) {
        // A direction each, of directions spread evenly over the sphere: the cloudless sky (no discs: HDRP's probe is of its
        // cube map) weighed by the clouds' phase towards the way down; a group's sum into its texel.
        const uint n = 64u * SKY_AMBIENT_GROUPS;
        const float3 V = sky_sphere_point(group.x * 64u + t, n);
        float3 haze;
        s_ambient[t] = sky_atmosphere(V, false, SKY_AMBIENT_STEPS, haze) * sky_cornette_shanks(SKY_CLOUD_ANISOTROPY, -V.y) * (4.0 * PI / float(n));
        GroupMemoryBarrierWithGroupSync();
        for (uint s = 32u; s > 0u; s >>= 1u) {
            if (t < s) s_ambient[t] += s_ambient[t + s];
            GroupMemoryBarrierWithGroupSync();
        }
        if (t == 0u) { RWTexture2D<float4> o = ResourceDescriptorHeap[pass.a.y]; o[uint2(1u + group.x, 0)] = float4(s_ambient[0], 1.0); }
        return;
    }
    if (mode == 7u) {
        if (any(p != 0u)) return;
        RWTexture2D<float4> o = ResourceDescriptorHeap[pass.a.y];
        float3 sum = 0.0;
        for (uint k = 1u; k <= SKY_AMBIENT_GROUPS; ++k) sum += o[uint2(k, 0)].rgb;
        o[uint2(0, 0)] = float4(sum, 1.0);
        return;
    }
    if (mode == 3u) {
        RWTexture2D<float2> clouds = ResourceDescriptorHeap[pass.a.y];
        uint w, h; clouds.GetDimensions(w, h);
        if (any(p >= uint2(w, h))) return;
        const uint rows = h / sky_cloud_layers();
        clouds[p] = rows > 0u ? cloud_texel(p, w, rows) : 0.0;
        return;
    }
    RWTexture2D<float4> o = ResourceDescriptorHeap[pass.a.y];
    uint w, h; o.GetDimensions(w, h);
    if (any(p >= uint2(w, h))) return;
    if (mode == 0u) o[p] = transmittance_texel(p);
    else if (mode == 1u) o[p] = multiple_texel(p);
    else if (mode == 2u) o[p] = ground_texel(p);
    else if (mode == 5u) o[p] = float4(max(sky_for_map(map_direction(p, uint2(w, h))), 0.0), 1.0);
    else if (mode == 6u) {
        RWTexture2D<float4> above = ResourceDescriptorHeap[pass.a.z];
        // (The last mips of the 2:1 map: the one above is a single row, so its texels are clamped into it, never read as 0.)
        uint aw, ah; above.GetDimensions(aw, ah);
        const uint2 q = p * 2u, q1 = min(q + 1u, uint2(aw, ah) - 1u);
        o[p] = 0.25 * (above[q] + above[uint2(q1.x, q.y)] + above[uint2(q.x, q1.y)] + above[q1]);
    }
}
