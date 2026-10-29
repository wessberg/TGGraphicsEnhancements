// SPDX-License-Identifier: MIT
// PT-118: each camera ray's walk through the cells (pt_fog_froxel), over its whole way. The medium stays pt_medium's:
// closed-form height transmittance and the same ratio tracking for every local fog box. No density voxelization: in
// every slice the way crosses one point is drawn as the medium scatters there, and the cells' light is read at it.
// pass.a: camera fog UAV (rgb the fog's light toward the camera in storage units, a the way's transmittance), the
// cells' SRV.
// What the cells hold of a lamp's light is its smooth share (pt_froxel fog_smooth_share); pt_fog_tail adds the rest.
#include "pt_common.hlsli"
#include "pt_froxel.hlsli"

// An 8x8 pixel group spans at most two ordinary froxel columns. Stage a 3x3 neighbourhood (with a
// conservative extra column), every slice, once for the group. This is the SAME trilinear radiance field;
// it does not integrate density or move samples. Unusual projected surface positions fall back to the texture.
// A cell the camera has seen none of has no light and no history (alpha 0: pt_fog_froxel takes no sample at a point
// no pixel reads). It is left out of what is read between cells: each cell counts with weight 1 or 0, and the sum is
// divided by the weights'.
groupshared float4 fogTile[3 * 3 * FOG_SLICES];
static int2 fogTileBase;
float4 fog_cell_counted(float4 c) { return c.a > 0.0 ? float4(c.rgb, 1.0) : float4(0.0, 0.0, 0.0, 0.0); }
float3 fog_cached_light(float2 uv, float depth) {
    const float3 q = float3(uv * float2(fog_grid_size().xy), fog_depth_slice(depth)) - 0.5;
    const int3 lo = int3(floor(q));
    const int2 local = lo.xy - fogTileBase;
    const float3 f = frac(q);
    const int last = int(FOG_SLICES) - 1;
    float4 a, b;
    if (any(local < 0) || any(local > 1)) {
        Texture3D<float4> grid = ResourceDescriptorHeap[pass.a.y];
        const int3 most = int3(fog_grid_size()) - 1;
        const int3 p0 = clamp(lo, 0, most), p1 = clamp(lo + 1, 0, most);
        a = lerp(lerp(fog_cell_counted(grid.Load(int4(p0.x, p0.y, p0.z, 0))), fog_cell_counted(grid.Load(int4(p1.x, p0.y, p0.z, 0))), f.x),
                 lerp(fog_cell_counted(grid.Load(int4(p0.x, p1.y, p0.z, 0))), fog_cell_counted(grid.Load(int4(p1.x, p1.y, p0.z, 0))), f.x), f.y);
        b = lerp(lerp(fog_cell_counted(grid.Load(int4(p0.x, p0.y, p1.z, 0))), fog_cell_counted(grid.Load(int4(p1.x, p0.y, p1.z, 0))), f.x),
                 lerp(fog_cell_counted(grid.Load(int4(p0.x, p1.y, p1.z, 0))), fog_cell_counted(grid.Load(int4(p1.x, p1.y, p1.z, 0))), f.x), f.y);
    } else {
        const uint xy = uint(local.x + local.y * 3);
        const uint z0 = uint(clamp(lo.z, 0, last)) * 9u, z1 = uint(clamp(lo.z + 1, 0, last)) * 9u;
        a = lerp(lerp(fogTile[z0 + xy], fogTile[z0 + xy + 1u], f.x),
                 lerp(fogTile[z0 + xy + 3u], fogTile[z0 + xy + 4u], f.x), f.y);
        b = lerp(lerp(fogTile[z1 + xy], fogTile[z1 + xy + 1u], f.x),
                 lerp(fogTile[z1 + xy + 3u], fogTile[z1 + xy + 4u], f.x), f.y);
    }
    const float4 r = lerp(a, b, f.z);
    return r.a > 1e-4 ? r.rgb / r.a : 0.0;
}
// Where slice s of the way ends, in metres along it: the slice's far side, and the way's own end in the last slice
// (the cells' far side is no end of the medium: a way that goes on past it is walked to its end, with the last
// cells' light).
float fog_slice_end(uint slice, float viewRate, float end) {
    return slice + 1u == FOG_SLICES ? end : min(end, fog_slice_depth(float(slice + 1u)) / viewRate);
}

// Exact homogeneous-height specialisation. The estimator's sample, weight and transmittance are unchanged;
// avoid rebuilding world points and evaluating height crossings for each segment that lies entirely below base.
float4 fog_integrate_homogeneous(float2 uv, float viewRate, float end, inout Rng rng) {
    const float density = g.fog.x;
    float3 sum = 0.0;
    float through = 1.0, begin = 0.0;
    [loop] for (uint slice = 0u; slice < FOG_SLICES && begin < end; ++slice) {
        const float next = fog_slice_end(slice, viewRate, end);
        const float segmentThrough = exp(-density * (next - begin));
        const float scatter = 1.0 - segmentThrough;
        if (scatter > 0.0) {
            const float t = begin - log(max(1.0 - rng.next() * scatter, 1e-30)) / density;
            const float3 light = fog_cached_light(uv, t * viewRate);
            sum += (through * scatter) * light;
        }
        through *= segmentThrough;
        begin = next;
    }
    return float4(sum * fog_albedo(), through);
}

void fog_integrate_segment(float3 dir, float2 uv, float viewRate, float begin, float end, float heightEnd,
                           bool crossesBoxes, inout Rng rng, inout float3 radiance, inout float through) {
    if (!(end > begin)) return;
    const float3 a = g.cameraPos.xyz + dir * begin;
    const float lengthBoxes = end - begin;
    const float lengthHeight = max(min(end, heightEnd) - begin, 0.0);
    const float heightThrough = exp(-fog_depth_between(a, a + dir * lengthHeight));
    float3 x, weight;
    bool scatters;
    if (crossesBoxes) scatters = fog_scatter_point(a, dir, lengthHeight, lengthBoxes, heightThrough, false, rng, x, weight);
    else {
        // The interval contains height fog alone: the same conditional free flight, without enumerating boxes.
        const float scatter = saturate(1.0 - heightThrough);
        scatters = scatter > 0.0;
        x = a + dir * fog_distance(a, dir, lengthHeight, -log(max(1.0 - rng.next() * scatter, 1e-30)));
        weight = fog_albedo() * scatter;
    }
    if (scatters) {
        const float depth = dot(x - g.cameraPos.xyz, dir) * viewRate;
        const float3 light = fog_cached_light(uv, depth);
        radiance += through * weight * light;
    }
    through *= heightThrough * (crossesBoxes ? fog_boxes_toward(a, dir, lengthBoxes, rng) : 1.0);
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID, uint2 group : SV_GroupID, uint lane : SV_GroupIndex) {
    fogTileBase = int2(floor(fog_grid_uvw(camera_relative_position(group * 8u, 1.0)).xy * float2(fog_grid_size().xy) - 0.5));
    Texture3D<float4> grid = ResourceDescriptorHeap[pass.a.y];
    for (uint i = lane; i < 3u * 3u * FOG_SLICES; i += 64u) {
        const int2 xy = clamp(fogTileBase + int2(i % 3u, (i / 3u) % 3u), 0, int2(fog_grid_size().xy) - 1);
        fogTile[i] = fog_cell_counted(grid.Load(int4(xy, i / 9u, 0)));
    }
    GroupMemoryBarrierWithGroupSync();
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass.a.x];
    const Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    // The way: to what the pixel shows; over the sky the height fog to HDRP's Max Fog Distance (as HDRP fogs the sky) and
    // the game's fog boxes as far as rays go (they end where they end).
    float3 dir; float heightEnd;
    if (s.valid) { const float3 d = s.position - g.cameraPos.xyz; heightEnd = length(d); dir = d / max(heightEnd, 1e-6); }
    else { dir = normalize(camera_relative_position(p, 1.0)); heightEnd = g.fogColor.w > 0.0 && g.fogColor.w < 1e7 ? g.fogColor.w : 0.0; }
    const float boxEnd = s.valid ? heightEnd : g.params.y;
    if (!(g.fog.x > 0.0) && fog_box_count() == 0u) { output[p] = float4(0.0, 0.0, 0.0, 1.0); return; }
    const float end = fog_box_count() != 0u ? max(heightEnd, boxEnd) : heightEnd;
    const float viewRate = max(mul(g.motionViewProj, float4(dir, 0.0)).w, 1e-6);
    const float2 uv = fog_grid_uvw(dir).xy; // constant along the camera ray, independent of the slice
    Rng rng = make_rng(p, g.size.z, 0x696e7467u);
    // The world can contain local volumes that this ray never enters. Check each box ONCE before the slice
    // walk. A miss permits the exact height-only path even when unrelated boxes exist elsewhere in the scene.
    float firstBox = end, lastBox = 0.0;
    for (uint k = 0u; k < fog_box_count(); ++k) {
        float enter, leave;
        if (fog_box_span(fog_box(k), g.cameraPos.xyz, dir, end, enter, leave)) { firstBox = min(firstBox, enter); lastBox = max(lastBox, leave); }
    }
    const float highest = max(g.cameraPos.y, g.cameraPos.y + dir.y * heightEnd) + g.cacheOrigin.y;
    if (!(lastBox > firstBox) && g.fog.x > 0.0 && g.fog.x < 1e6 && g.fog.z >= 0.0 &&
        (highest <= g.fog.y || g.fog.z == 0.0)) {
        output[p] = fog_integrate_homogeneous(uv, viewRate, heightEnd, rng);
        return;
    }
    float3 radiance = 0.0;
    float through = 1.0, begin = 0.0;
    [loop] for (uint slice = 0u; slice < FOG_SLICES && begin < end; ++slice) {
        const float next = fog_slice_end(slice, viewRate, end);
        fog_integrate_segment(dir, uv, viewRate, begin, next, heightEnd, next > firstBox && begin < lastBox, rng, radiance, through);
        begin = next;
    }
    output[p] = float4(radiance, through);
}
