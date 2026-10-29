// SPDX-License-Identifier: MIT
// One edge-aware a-trous wavelet iteration (SVGF's spatial stage, Schied et al. 2017): a 5x5 B3-spline
// kernel spread by the step size, weighted down across surfaces, normals and, by the noise of the two
// pixels, across luminance. The variance is filtered along so each iteration trusts the image a little more; alpha
// carries its square root, the standard deviation (in half precision the variance of a hot spot overflowed).
// Each pixel takes a mean over the neighbours it accepts, and the luminance weight looks at this pixel's own noise (a
// 3x3 Gaussian of the variance): a calm pixel refuses a neighbour far brighter than its noise explains. That LOSES
// LIGHT where light arrives as rare bright samples (PT-75: a candle-lit floor's direct light kept 0.75 of its mean, a
// metal's reflections 0.68). For some hours on 2026-10-08 the pass kept light exactly instead (every weight the same
// seen from either pixel, by both pixels' noise; a pixel kept what its neighbours did not take). In the game every
// rare bright sample then became a soft round disc held by the history: the owner, 18:15: "actual orbs, like a
// particle effect ... It almost looks like an overlay ... seemingly anywhere" (the tavern: 37 places a frame against
// 3). A sample's light spread over 24 neighbours twice is the kernel's own stamp. So: do not make this pass keep
// the light of rare bright samples again; make those samples less rare in the tracer (PT-85), and the loss goes
// with them. The planes' and the normals' terms still take both pixels' values.
// Specular signals also stay within similar roughness. A reflection is filtered by what it SHOWS (PT-80; the owner: "we
// need our tracer to be able to perfectly render the world in a mirror"): pt_specular leaves each pixel the normal and
// the reflectance of the surface its ray met (pack_seen), and its hit distance says where that surface is. On a surface
// as smooth as a mirror (SEEN_PICTURE_ROUGHNESS) a neighbour counts only as far as it shows the same surface: its
// normal, its plane, and its reflectance to within a few hundredths (SAME_REFLECTANCE), so an even wall in the mirror is
// denoised and a texture's cells, a letter, an edge stay as traced (light is only ever exchanged between pixels that
// show the same thing: nothing of the picture is taken apart and put together again). A pixel with no guide of what it
// shows (a mirror in the mirror, water in it) is left to its history alone. Up to SEEN_BLUR_ROUGHNESS the two filters
// blend: a rough surface's reflection is a blur of what it reflects, filtered across the surface as before. Before, the
// reflections' filter took a mirror's picture for noise on a flat surface: its first iteration smeared it, and the
// feedback smeared the history again every frame (in game a mirror kept 0.09 of the reference's detail with NRD against
// 1.24 with no denoiser; the harness's mirror 0.22). The water's own path is filtered with water alone and, mirror-like,
// by the first, finest iteration only, as it was.
// The second iteration feeds the first one's result back into the temporal history (SVGF's feedback), which is read
// at the pixel only: the first iteration is finished by then. Not a picture's: fed back, the little that crosses a
// texture's faint detail each frame would add up over the history's 16 frames.
// ON TRIAL (PT-95, PT-19; docs/agents/nrd-relief.md; pass.b.y, the plugin's hidden "Reflection relief trial"; INVALID or
// 0: off, every weight as before). A guard's hammered iron helmet came out smooth bright metal with NRD, where the frame
// with no denoiser had the game's relief (its face kept 0.59 of 10.51 of the detail): NRD takes the bounced diffuse
// light alone, a metal has none, and its reflections go through this pass. On anything but a near mirror the normals'
// term below is loose (a bump tilted 10 degrees counts 0.94 from a summed roughness of 0.8 up), and the feedback keeps
// the history smooth: fed back every frame, the history holds the blurred picture, each frame's sample brings a
// sixteenth of the relief back, and against that smooth history the brightness term sees nothing to stop at. The same
// to two digits a minute after loading and at the run's end: it is where the filter settles, not a start-up. (Which of
// the two it is, the loose normals or the feedback, is read from the code and not measured: the trial's bits are its test.)
// Bit 4 (RELIEF_HISTORY): the first iteration is not fed back into the history, as on a mirror: a pixel's history is
// its own samples' mean, the relief is in it, and the brightness term (which lets through what the noise explains)
// stops at it. Bit 1 (RELIEF_LOBE): two pixels exchange a reflection only as far as their normals gather the same
// light: by the lobe itself (lobe_held), at every roughness. Bit 2 (RELIEF_REFLECTANCE): and only as far as they
// reflect alike (their specular colour, same_reflecting): a metal's base map stays in its reflection. Glass, water and
// hair with its highlights keep their filter as it was (plain_reflector).
//   pass.a: input, output, step size, 1 for a specular signal   pass.b: history to feed, or INVALID; y: the trial's bits
#include "pt_common.hlsli"

static const uint RELIEF_LOBE = 1u, RELIEF_REFLECTANCE = 2u, RELIEF_HISTORY = 4u;
// How much of a pixel's own lobe a neighbour's normal still holds: GGX's distribution about the one normal, read at the
// other, over its peak: what a small light's highlight at the lobe's middle falls to when the normal tilts that far (1
// for the same normal; at roughness 0.4, 0.60 at 5 degrees and 0.22 at 10; at 0.8, 0.83 at 15). The same seen from
// either pixel (the roughness is the pair's mean). The sine by the cross product: exact for normals that barely differ.
float lobe_held(float3 normal, float3 other, float roughness) {
    const float a = roughness_alpha(roughness), a2 = a * a;
    const float c = saturate(dot(normal, other));
    const float3 x = cross(normal, other);
    const float held = a2 / max(dot(x, x) + a2 * c * c, 1e-12);
    return saturate(held * held);
}
// A pixel's material word (pt_prepare: its specular colour's bytes and its kind's bits) says it is a plain reflecting
// surface: not glass or water (bit 2), nor hair with its highlights (bit 64: its lobes lie along the strand).
bool plain_reflector(uint2 word, uint kind) { return ((word.y >> 24) & 66u) == 0u && kind != SEEN_WATER && kind != SEEN_THROUGH; }
// How far two pixels reflect alike: e to the minus ten times their specular colours' largest difference of a channel
// over the larger of the two (0.37 at a tenth apart, 0.05 at three tenths).
float same_reflecting(uint2 word, uint2 other) {
    const float3 a = srgb_to_linear(unpack_rgba8(word.y).rgb), b = srgb_to_linear(unpack_rgba8(other.y).rgb);
    const float3 differ = abs(a - b) / max(max(a, b), 1e-4);
    return exp(-10.0 * max(differ.r, max(differ.g, differ.b)));
}

// How fast two reflectances that differ stop counting as the same surface: e to the minus this times their largest
// difference of a channel over the larger of the two (0.5 at one hundredth apart, 0.04 at five).
static const float SAME_REFLECTANCE = 64.0;

// What a pixel's reflection shows (pack_seen), and where: along the reflected ray (through glass: the camera's own) as
// far as its hit distance. picture: how far the reflection is a picture of what it shows (1 on a mirror) and not a rough
// lobe's blur (0).
struct Shown { uint kind; bool guide; float3 normal; float3 reflectance; float3 position; float distance; float picture; };
Shown load_shown(uint2 p, Surface s) {
    RWTexture2D<uint2> seen = ResourceDescriptorHeap[g.material.z];
    RWTexture2D<float4> raw = ResourceDescriptorHeap[g.signals.w];
    const uint2 code = seen[p];
    Shown o;
    o.kind = seen_kind(code);
    o.distance = raw[p].a;
    o.guide = (o.kind == SEEN_REFLECTED || o.kind == SEEN_THROUGH) && !seen_picture(code) && o.distance > 0.0 && o.distance < HIT_DISTANCE_MISS * 0.5;
    o.normal = seen_normal(code);
    o.reflectance = seen_reflectance(code);
    const float3 view = normalize(s.position - g.cameraPos.xyz);
    o.position = s.position + (o.kind == SEEN_THROUGH ? view : reflect(view, s.normal)) * (o.guide ? o.distance : 0.0);
    o.picture = o.kind == SEEN_WATER ? 0.0 : saturate((SEEN_BLUR_ROUGHNESS - s.roughness) / (SEEN_BLUR_ROUGHNESS - SEEN_PICTURE_ROUGHNESS));
    return o;
}
// The water's own path on a mirror-like surface: the first, finest iteration only.
bool water_rests(Shown o, Surface s, int step) { return o.kind == SEEN_WATER && s.roughness < 0.15 && step > 1; }

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> input = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass.a.y];
    int step = int(pass.a.z);
    const bool specular = pass.a.w == 1 && g.material.z != INVALID;
    float4 center = input[p];
    Surface s = load_surface(p, g.surface.x, g.surface.z, INVALID);
    Shown shown = (Shown)0;
    if (specular && s.valid && s.deferred) shown = load_shown(p, s);
    // ON TRIAL (the header): the reflections' filter on a plain reflecting surface; off: nothing of it is read.
    const uint relief = specular && pass.b.y != INVALID && g.material.x != INVALID ? pass.b.y : 0u;
    uint2 word = uint2(0u, 0u);
    if (relief != 0u) { RWTexture2D<uint2> words = ResourceDescriptorHeap[g.material.x]; word = words[p]; }
    const bool plain = relief != 0u && s.valid && s.deferred && plain_reflector(word, shown.kind);
    // (On trial, RELIEF_HISTORY: such a surface's history keeps its own samples; nothing is fed back into it.)
    if (pass.b.x != INVALID && !(plain && (relief & RELIEF_HISTORY) != 0u)) {
        RWTexture2D<float4> history = ResourceDescriptorHeap[pass.b.x];
        const float4 before = history[p];
        history[p] = float4(lerp(center.rgb, before.rgb, shown.picture), before.a);
    }
    if (!s.valid || !s.deferred) { output[p] = center; return; }
    // pass.b.z 1 (the reflections' passes while NRD runs, tracer.cpp): NRD takes every surface's reflections but glass's,
    // water's and what is seen from inside water (pt_nrd_prep nrd_takes), so only those are filtered here; the rest pass
    // through as they are (still real values for a glass pixel's neighbours to weigh), NRD's result replacing them after.
    if (specular && pass.b.z == 1u && !(s.glass || s.water || s.underwater)) { output[p] = center; return; }
    if (specular && water_rests(shown, s, step)) { output[p] = center; return; }
    const float kernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };
    // The variance the luminance weight trusts: a 3x3 Gaussian around the pixel.
    float variance3 = 0.0, gaussian = 0.0;
    for (int vy = -1; vy <= 1; ++vy) {
        for (int vx = -1; vx <= 1; ++vx) {
            int2 q = clamp(int2(p) + int2(vx, vy), int2(0, 0), int2(g.size.xy) - 1);
            float w = (vx == 0 ? 0.5 : 0.25) * (vy == 0 ? 0.5 : 0.25);
            float deviation = input[q].a;
            variance3 += deviation * deviation * w; gaussian += w;
        }
    }
    variance3 /= gaussian;
    const float lc = luminance(center.rgb);
    const float sigma = 4.0 * sqrt(max(variance3, 1e-10)) + 1e-4;
    float3 sum = center.rgb; float variance = center.a * center.a; float weights = 1.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) continue;
            int2 q = int2(p) + int2(x, y) * step;
            if (any(q < 0) || any(q >= int2(g.size.xy))) continue;
            Surface sq = load_surface(uint2(q), g.surface.x, g.surface.z, INVALID);
            if (!sq.valid || !sq.deferred) continue;
            float4 c = input[q];
            // Edges by the surfaces' own orientation; normal-map detail only softly, or it keeps the grain. An edge is where the
            // surface turns by its own orientation and by its smooth normal both (PT-04): across the flat faces of a round pot
            // the smooth normal barely turns, and the pot is filtered as the round thing it is; by its faces alone, each face
            // was filtered on its own into one tone (two faces of a 12-sided pot weighed 0.0001). A wall's normal map still
            // agrees in orientation, and a real corner turns both.
            const float faces = pow(saturate(dot(s.geoNormal, sq.geoNormal)), 64.0);
            const float edge = (g.settings2.y & OPTION_FACETED) != 0 ? faces : max(faces, pow(saturate(dot(s.normal, sq.normal)), 64.0));
            // Normal-map detail: diffuse light barely changes with it, and a reflection only as much as its lobe is
            // narrow; glossy surfaces keep their normals' detail, rough ones are filtered across it.
            const float normalPower = specular ? lerp(32.0, 4.0, saturate((s.roughness + sq.roughness) * 1.25)) : 4.0;
            float wn = edge * pow(saturate(dot(s.normal, sq.normal)), normalPower);
            // Off each other's plane, by the farther of the two.
            const float3 apart = sq.position - s.position;
            float wp = exp(-max(abs(dot(s.geoNormal, apart)), abs(dot(sq.geoNormal, apart))) / ((0.02 + 0.005 * (s.distance + sq.distance)) * float(step)));
            float wr = specular ? exp(-abs(sq.roughness - s.roughness) * 12.0) : 1.0;
            // What the two pixels show (a mirror's reflection): the same surface, or the neighbour does not count.
            float ws = 1.0;
            if (specular) {
                const Shown o = load_shown(uint2(q), sq);
                // The water's own path with water alone, and none of it into a pixel that rests.
                if ((shown.kind == SEEN_WATER) != (o.kind == SEEN_WATER) || water_rests(o, sq, step)) continue;
                const float picture = 0.5 * (shown.picture + o.picture);
                if (picture > 0.0) {
                    float same = 0.0;
                    if (shown.guide && o.guide && shown.kind == o.kind) {
                        const float3 differ = abs(shown.reflectance - o.reflectance) / max(shown.reflectance, o.reflectance);
                        const float3 between = o.position - shown.position;
                        same = pow(saturate(dot(shown.normal, o.normal)), 16.0) *
                               exp(-max(abs(dot(shown.normal, between)), abs(dot(o.normal, between))) /
                                   ((0.02 + 0.005 * (s.distance + shown.distance + sq.distance + o.distance)) * float(step))) *
                               exp(-SAME_REFLECTANCE * max(differ.r, max(differ.g, differ.b)));
                    } else if (shown.kind == SEEN_NONE && o.kind == SEEN_NONE) same = 1.0; // the sky, both
                    ws = lerp(1.0, same, picture);
                }
                // ON TRIAL (the header): between two plain reflecting surfaces, the normals' term is the lobe's own, and the
                // neighbour counts as far as it reflects alike.
                if (plain) {
                    RWTexture2D<uint2> words = ResourceDescriptorHeap[g.material.x];
                    const uint2 other = words[q];
                    if (plain_reflector(other, o.kind)) {
                        if ((relief & RELIEF_LOBE) != 0u) wn = edge * lobe_held(s.normal, sq.normal, 0.5 * (s.roughness + sq.roughness));
                        if ((relief & RELIEF_REFLECTANCE) != 0u) ws *= same_reflecting(word, other);
                    }
                }
            }
            // Against this pixel's own noise: a neighbour far brighter than it explains does not count (the orbs, above).
            float wl = exp(-abs(luminance(c.rgb) - lc) / sigma);
            float w = kernel[abs(x)] * kernel[abs(y)] / (kernel[0] * kernel[0]) * wn * wp * wl * wr * ws;
            sum += c.rgb * w; variance += c.a * c.a * w * w; weights += w;
        }
    }
    output[p] = float4(sum / weights, sqrt(variance) / weights);
}
