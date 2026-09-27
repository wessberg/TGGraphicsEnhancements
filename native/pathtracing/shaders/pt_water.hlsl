// SPDX-License-Identifier: MIT
// Traced reflections on water, and on the other transparent surfaces HDRP gives screen-space reflections (render event 4,
// tracer.cpp water_event). HDRP draws them after the frame's own trace has started, so they get a pass of their own, from
// HDRP's depth, normals and stencil at its transparent SSR step: one GGX visible-normal sample a pixel from the water's
// normal and roughness, and what it meets shaded as pt_specular shades what lies behind glass (its direct light and the
// Bounces setting's bounced light: by its direct light alone a bank in shade reflected black), the sky where it meets
// nothing. The light arriving along the reflection, unweighted: HDRP's water and transparent shading weighs it by their
// own Fresnel term, as they weigh HDRP's own reflections. HDRP's water is nobody's denoising input (Ray Reconstruction and
// the denoisers see the opaque surfaces under it), so it is accumulated over frames here: reprojected through the
// reflection's mirror image first (the reflection in a flat water moves with the camera as the reflected thing would,
// seen through a window at that distance), then through the surface itself.
//   pass.a: this frame's depth (0 where HDRP takes no reflection), normal buffer, HDRP's reflections (unused), output
//   pass.b: the previous frame's output, history out, previous history, flags (1: the previous water frame is this one's
//           predecessor)
// Output: rgb the reflected light pre-exposed with this frame's exposure and the traced light's gain, a the reflection's
// hit distance (HIT_DISTANCE_MISS for the sky, 0 where nothing was traced). History: rgb in storage units, a frames.
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

// Frames a reflection is averaged over at most: long enough to settle its bounced light's noise, short enough that it
// follows the waves and the light (12 frames: a fifth of a second at 60 frames a second).
static const float WATER_HISTORY = 12.0;

// The surface's own orientation from its depth (the neighbour along each axis whose next two texels continue its depth in
// a straight line lies on its plane, as in pt_prepare), unoriented; the fallback where no neighbour has water, and at the
// image's edge (where a neighbour is the pixel itself).
float3 depth_normal(Texture2D<float> depthTex, int2 p, float depth, float3 rel, float3 fallback) {
    const int2 last = int2(g.size.xy) - 1;
    const int2 l = clamp(p - int2(1, 0), 0, last), r = clamp(p + int2(1, 0), 0, last);
    const int2 u = clamp(p - int2(0, 1), 0, last), d = clamp(p + int2(0, 1), 0, last);
    const float dl = depthTex.Load(int3(l, 0)), dr = depthTex.Load(int3(r, 0)), du = depthTex.Load(int3(u, 0)), dd = depthTex.Load(int3(d, 0));
    const float dl2 = depthTex.Load(int3(clamp(p - int2(2, 0), 0, last), 0)), dr2 = depthTex.Load(int3(clamp(p + int2(2, 0), 0, last), 0));
    const float du2 = depthTex.Load(int3(clamp(p - int2(0, 2), 0, last), 0)), dd2 = depthTex.Load(int3(clamp(p + int2(0, 2), 0, last), 0));
    const bool right = dr > 0.0 && (!(dl > 0.0) || abs(2.0 * dr - dr2 - depth) < abs(2.0 * dl - dl2 - depth));
    const bool down = dd > 0.0 && (!(du > 0.0) || abs(2.0 * dd - dd2 - depth) < abs(2.0 * du - du2 - depth));
    const float dx = right ? dr : dl, dy = down ? dd : du;
    if (!(dx > 0.0) || !(dy > 0.0)) return fallback;
    const float3 px = camera_relative_position(uint2(right ? r : l), dx) - rel;
    const float3 py = camera_relative_position(uint2(down ? d : u), dy) - rel;
    const float3 c = cross(px, py);
    return dot(c, c) > 1e-12 ? normalize(c) : fallback;
}

// The previous frame's accumulated reflection seen at a camera-relative position (through HDRP's previous view, which takes
// this frame's camera-relative positions), from the four texels around it whose reflection met something as far away:
// (rgb, frames), frames 0 for none.
float4 previous_reflection(float3 at, float reach) {
    RWTexture2D<float4> previousOutput = ResourceDescriptorHeap[pass.b.x];
    RWTexture2D<float4> previousHistory = ResourceDescriptorHeap[pass.b.z];
    const float4 clip = mul(g.prevViewProj, float4(at, 1.0));
    if (clip.w <= 1e-4) return 0.0;
    const float2 uv = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
    const float2 t = uv * float2(g.size.xy) - 0.5;
    const int2 base = int2(floor(t));
    const float2 f = t - float2(base);
    const bool miss = reach >= HIT_DISTANCE_MISS * 0.5;
    float4 sum = 0.0; float weights = 0.0;
    [unroll] for (int i = 0; i < 4; ++i) {
        const int2 o = int2(i & 1, i >> 1), q = base + o;
        if (any(q < 0) || any(q >= int2(g.size.xy))) continue;
        const float before = previousOutput[q].a;
        if (!(before > 0.0)) continue;
        const bool missBefore = before >= HIT_DISTANCE_MISS * 0.5;
        if (miss != missBefore || (!miss && abs(before - reach) > 0.1 * reach + 0.1)) continue;
        const float w = (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
        const float4 h = previousHistory[q];
        if (w <= 0.0 || !(h.a > 0.0)) continue;
        sum += h * w; weights += w;
    }
    return weights > 0.05 ? sum / weights : 0.0;
}

void water_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    Texture2D<float> depthTex = ResourceDescriptorHeap[pass.a.x];
    Texture2D<float4> normals = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass.a.w];
    RWTexture2D<float4> history = ResourceDescriptorHeap[pass.b.y];
    const float depth = depthTex.Load(int3(p, 0));
    if (!(depth > 0.0)) { output[p] = 0.0; history[p] = 0.0; return; }
    const float3 rel = camera_relative_position(p, depth);
    const float dist = length(rel);
    const float3 v = -rel / max(dist, 1e-6);
    const float4 nb = normals.Load(int3(p, 0));
    float3 n = decode_hdrp_normal(nb);
    // The water's own orientation, on its normal's side. Seen from under it the pixel keeps HDRP's reflection (its water
    // shades its underside itself); the far side of a wave, turned from the camera, reflects as the water's plane does.
    float3 geo = depth_normal(depthTex, int2(p), depth, rel, n);
    geo = dot(geo, n) >= 0.0 ? geo : -geo;
    if (dot(geo, v) <= 0.0) { output[p] = 0.0; history[p] = 0.0; return; }
    if (dot(n, v) < 0.02) n = geo;
    Rng rng = make_rng(p, g.size.z, 11);
    const float3 l = above_surface(ggx_sample(n, v, roughness_alpha(saturate(nb.a)), rng.next2()), geo);
    const float3 position = rel + g.cameraPos.xyz;
    Hit hit;
    float3 radiance; float reach;
    if (trace_from_surface(position + geo * ray_offset(dist), l, 0.0, g.params.y, near_back(dist), hit)) {
        // As many paths from what the reflection meets as Rays Per Pixel gives the bounced light.
        const uint paths = clamp(g.grid.w, 1u, 8u);
        radiance = 0.0;
        for (uint path = 0; path < paths; ++path) radiance += shade_path(hit, l, 1u + min(g.settings.z, 3u), rng);
        radiance /= float(paths);
        // Leaves and grass (translucent) are lit through from behind as well: shade_path's light sampling (direct_diffuse).
        reach = max(length(hit.position - position), 1e-3);
    } else { radiance = sky_radiance(l); reach = HIT_DISTANCE_MISS; }
    radiance = to_storage(clamp_radiance(radiance, LIMIT_REFLECTION));
    float3 color = radiance; float frames = 1.0;
    if ((pass.b.w & 1u) != 0 && (g.size.w & FLAG_RESET) == 0) {
        float4 before = previous_reflection(-v * (dist + min(reach, 1e4)), reach);
        if (!(before.a > 0.0)) before = previous_reflection(rel, reach);
        if (before.a > 0.0) {
            // Stored relative to the previous frame's reference radiance: into this frame's.
            const float rescale = g.radiance.y > 0.0 && g.radiance.y < 1e30 ? g.radiance.y / reference_radiance() : 1.0;
            frames = min(before.a + 1.0, WATER_HISTORY);
            color = lerp(before.rgb * rescale, radiance, 1.0 / frames);
            if (!all(isfinite(color))) { color = radiance; frames = 1.0; }
        }
    }
    history[p] = float4(color, frames);
    output[p] = float4(max(color, 0.0) * (exposure() * reference_radiance() * traced_gain_at(position)), reach);
}
PT_ENTRY(water_pixel, water)
