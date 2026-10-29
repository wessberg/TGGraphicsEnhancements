// SPDX-License-Identifier: MIT
// Temporal accumulation of one signal (SVGF's first stage, Schied et al. 2017). The previous frame's
// history is fetched where the pixel's point was in THAT frame's pixel grid (its motion and the two frames'
// sub-pixel shifts: pt_common previous_place), from the four texels round that place whose surfaces match
// this pixel's (when none of the four does, from the nine round the nearest: a thing a pixel wide), and
// blended with this frame's value by the number of frames accumulated. With moments,
// the luminance's first two moments give the variance the a-trous passes steer by; without, the
// history's alpha holds the accumulated length.
// Reflections are filtered as they are, not divided by their directional albedo: measured in the harness,
// dividing gave normal-mapped surfaces more sparkle and flicker (small lights' highlights), not less.
// A mirror's reflection (PT-80; pass.b.w 1: the reflections' signal) is a picture of what it shows, which moves across
// the mirror as the camera moves, not with the mirror's face: on a surface smooth enough to show one (pt_atrous's
// picture, from half of it up) the history is fetched where the shown point was on screen last frame: the point as far
// behind the surface along the camera's ray as the reflected ray went on after it (a flat mirror's image of what it shows,
// and behind glass the thing itself), taken as standing still. A history texel counts only if it showed the same point
// (its own surface and hit distance, kept in the moments' fourth value). Followed by the mirror's face, a moving
// camera's mirror image trailed over 16 frames.
//   pass.a: raw, history out, history in, moments out   pass.b: moments in, maximum length, flags, 1 for the reflections
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> raw = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> historyOut = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float4> historyIn = ResourceDescriptorHeap[pass.a.z];
    bool moments = pass.a.w != INVALID;
    float maxLength = float(pass.b.y);
    const float4 traced = raw[p];
    float3 current = traced.rgb;
    Surface s = load_surface(p, g.surface.x, g.surface.z, INVALID);
    if (!s.valid || !s.deferred) {
        historyOut[p] = float4(current, 0.0);
        if (moments) { RWTexture2D<float4> mo = ResourceDescriptorHeap[pass.a.w]; mo[p] = 0.0; }
        return;
    }
    // The reflections' signal: its hit distance (kept beside the moments for the next frame), and whether the pixel shows
    // a picture to follow.
    const bool reflections = moments && pass.b.w == 1u && g.material.z != INVALID;
    const float shownDistance = reflections ? traced.a : 0.0;
    bool follows = false;
    float3 shownAt = 0.0;
    if (reflections && shownDistance > 0.0 && shownDistance < HIT_DISTANCE_MISS * 0.5) {
        RWTexture2D<uint2> seen = ResourceDescriptorHeap[g.material.z];
        const uint kind = seen_kind(seen[p]);
        follows = (kind == SEEN_REFLECTED || kind == SEEN_THROUGH) &&
                  (SEEN_BLUR_ROUGHNESS - s.roughness) / (SEEN_BLUR_ROUGHNESS - SEEN_PICTURE_ROUGHNESS) >= 0.5;
        shownAt = s.position + normalize(s.position - g.cameraPos.xyz) * shownDistance;
    }
    float3 history = 0.0; float4 historyMoments = 0.0; float weightSum = 0.0; float historyLength = 0.0;
    if ((g.size.w & (FLAG_RESET | FLAG_DENOISER_RESET)) == 0) {
        float2 mv = motion_at(p);
        if (follows) {
            // The rule the motion vectors are made by (pt_primary hit_motion), for the shown point.
            const float4 c = mul(g.motionViewProj, float4(shownAt - g.cameraPos.xyz, 1.0)), was = mul(g.prevViewProj, float4(shownAt - g.cameraPos.xyz, 1.0));
            if (c.w > 1e-6 && was.w > 1e-6) { float2 v = c.xy / c.w - was.xy / was.w; v.y = -v.y; mv = v * 0.5; }
            else follows = false;
        }
        // In the previous frame's own pixel grid (its sub-pixel shift is another one: previous_place, PT-85).
        float2 prevPixel = previous_place(p, mv) - 0.5;
        int2 base = int2(floor(prevPixel));
        float2 f = prevPixel - float2(base);
        const int2 nearest = int2(floor(prevPixel + 0.5));
        RWTexture2D<float4> momentsIn = ResourceDescriptorHeap[moments ? pass.b.x : pass.a.z];
        // The four texels round the place, each by how near it lies. And a second try when none of them showed this
        // pixel's surface: the nine texels round the nearest one, each that did show it counting alike (SVGF's own
        // second try, Schied et al. 2017). A thing about a pixel wide, a slat's or a blade's edge, is drawn by other
        // pixels in every frame as the camera's shift moves (PT-84): the four texels round where its point was can all
        // have shown what lies behind it, while the texel beside them holds its history. Every texel is still checked
        // against its own surface: nothing is taken from a neighbour blindly.
        bool second = false;
        for (int i = 0; i < 13; ++i) {
            if (i == 4) {
                // (On trial with the rest: g.look.w 0, the plugin's hidden setting off, leaves the four alone as before.)
                if (weightSum > 0.01 || !(g.look.w > 0.5)) break;
                second = true; history = 0.0; historyMoments = 0.0; historyLength = 0.0; weightSum = 0.0;
            }
            const int2 o = second ? int2((i - 4) % 3, (i - 4) / 3) - 1 : int2(i & 1, i >> 1);
            const int2 q = (second ? nearest : base) + o;
            if (any(q < 0) || any(q >= int2(g.size.xy))) continue;
            Surface sp = load_surface(uint2(q), g.surface.y, g.surface.w, INVALID);
            sp.position += g.prevOriginDelta.xyz;
            if (!sp.valid || !sp.deferred || dot(sp.geoNormal, s.geoNormal) < 0.85) continue;
            if (abs(dot(s.geoNormal, sp.position - s.position)) > 0.05 + 0.02 * s.distance) continue;
            float w = second ? 1.0 : (o.x ? f.x : 1.0 - f.x) * (o.y ? f.y : 1.0 - f.y);
            if (w <= 0.0) continue;
            const float4 hm = moments ? momentsIn[q] : float4(0.0, 0.0, 0.0, 0.0);
            if (follows) {
                // The point that texel showed, from the previous camera: the same one, or its history is another picture's.
                if (!(hm.w > 0.0 && hm.w < HIT_DISTANCE_MISS * 0.5)) continue;
                const float3 showed = sp.position + normalize(sp.position - g.prevCameraPos.xyz) * hm.w;
                if (length(showed - shownAt) > 0.05 + 0.02 * (s.distance + shownDistance)) continue;
            }
            float4 h = historyIn[q];
            history += h.rgb * w;
            if (moments) historyMoments += hm * w;
            else historyLength += h.a * w;
            weightSum += w;
        }
    }
    float len;
    if (weightSum > 0.01) {
        history /= weightSum;
        if (moments) { historyMoments /= weightSum; len = historyMoments.z; } else len = historyLength / weightSum;
        // The history is stored relative to the previous frame's reference radiance: into this frame's.
        float rescale = g.radiance.y > 0.0 && g.radiance.y < 1e30 ? g.radiance.y / reference_radiance() : 1.0;
        history *= rescale;
        historyMoments.xy *= float2(rescale, rescale * rescale);
    } else { len = 0.0; }
    len = min(len + 1.0, maxLength);
    float alpha = len > 1.0 ? max(1.0 / len, 1.0 / maxLength) : 1.0;
    float3 color = lerp(history, current, alpha);
    if (!all(isfinite(color))) color = current;
    if (moments) {
        RWTexture2D<float4> momentsOut = ResourceDescriptorHeap[pass.a.w];
        float l = luminance(current);
        float2 m = len > 1.0 ? lerp(historyMoments.xy, float2(l, l * l), alpha) : float2(l, l * l);
        // The spread of single frames over time. ReSTIR reuses a sample for about a frame and its temporal
        // neighbour is permuted, so the frames differ and this is a fair measure (a 3x3 spread of the raw
        // signal used to stand in for it, which took every sharp shadow edge for noise).
        float variance = max(0.0, m.y - m.x * m.x);
        // The a-trous passes filter the accumulated value, whose noise is that spread over the frames it holds.
        // Steered by the single frames' spread, they took every shadow smaller than the noise of one frame for
        // noise: a chair's or a rope net's shadow was blurred away.
        variance /= len;
        // Few frames of history: their variance says little, so filter hard until it settles.
        if (len < 4.0) variance = max(variance, l * l * (4.0 - len));
        momentsOut[p] = float4(m, len, shownDistance);
        // The standard deviation: the variance of a hot spot passed half precision's largest value.
        historyOut[p] = float4(color, sqrt(variance));
    } else {
        historyOut[p] = float4(color, len);
    }
}
