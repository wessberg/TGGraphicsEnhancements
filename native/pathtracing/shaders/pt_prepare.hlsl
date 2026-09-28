// SPDX-License-Identifier: MIT
// HDRP's GBuffer as the tracer uses it: world position from depth, the normal and roughness from the
// normal buffer, the diffuse color and specular color from GBuffers 0 and 2 (Lit's encoding). A surface HDRP draws
// forward (hair, beards, some props; its lighting is HDRP's own, with the sky's ambient light and none of the traced
// light) is looked for along the camera ray in the traced scene instead, within a few centimetres of HDRP's depth:
// where it is there, the traced material gives its color and roughness and the tracer shades it like the rest.
#include "pt_common.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    Texture2D<float4> gb0 = ResourceDescriptorHeap[g.inputs0.x];
    Texture2D<float4> nb = ResourceDescriptorHeap[g.inputs0.y];
    Texture2D<float4> gb2 = ResourceDescriptorHeap[g.inputs0.z];
    Texture2D<float> depthTex = ResourceDescriptorHeap[g.inputs1.x];
    RWTexture2D<float4> surf = ResourceDescriptorHeap[g.surface.x];
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
    RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];

    float depth = depthTex.Load(int3(p, 0));
    if (!(depth > 0.0)) { surf[p] = 0.0; nrm[p] = uint2(pack_normal(float3(0, 1, 0)), 0); mat[p] = 0; return; }
    float3 rel = camera_relative_position(p, depth);
    float4 a = gb0.Load(int3(p, 0));
    float4 n = nb.Load(int3(p, 0));
    float4 s = gb2.Load(int3(p, 0));
    float3 normal = decode_hdrp_normal(n);
    bool deferred = a.a > 0.5;
    uint feature = uint(s.a * 255.0 + 0.5) & 7u;
    // Standard Lit stores the diffuse color in GBuffer0 and the specular color (fast sRGB) in GBuffer2;
    // the other features keep the base color and imply the default dielectric response.
    float3 f0 = feature == 0 ? fast_srgb_to_linear(s.rgb) : 0.04;
    float roughness = saturate(n.a);
    const float drawnRoughness = roughness; const float3 drawnF0 = f0;
    // Reflection Boost, a debugging aid: every surface glossier and more reflective than the game made it, so the
    // traced reflections show everywhere and can be judged (1: nearly a mirror). Not skin (HDRP's transmission through a
    // solid profile): the game's is glossy already (a roughness of 0.18), and at 0.1 its reflectance doubled, so a neck
    // mirrored the dark chin and collar above it in sharp patches, like a rash.
    float boost = solid_transmission(s, feature) ? 0.0 : g.cameraPos.w;
    roughness = lerp(roughness, 0.05, boost);
    f0 = lerp(f0, 0.6, 0.7 * boost);
    // The geometric normal from the positions of a neighbour along each axis, facing the camera; the
    // shading normal where they are sky. Device depth is affine across a plane in screen space, so the
    // side whose next two texels continue this one's depth in a straight line lies on this pixel's plane
    // (at a crease the nearer-depth side can belong to the other plane).
    int2 last = int2(g.size.xy) - 1;
    int2 l = clamp(int2(p) - int2(1, 0), 0, last), r = clamp(int2(p) + int2(1, 0), 0, last);
    int2 u = clamp(int2(p) - int2(0, 1), 0, last), d = clamp(int2(p) + int2(0, 1), 0, last);
    float dl = depthTex.Load(int3(l, 0)), dr = depthTex.Load(int3(r, 0)), du = depthTex.Load(int3(u, 0)), dd = depthTex.Load(int3(d, 0));
    float dl2 = depthTex.Load(int3(clamp(int2(p) - int2(2, 0), 0, last), 0)), dr2 = depthTex.Load(int3(clamp(int2(p) + int2(2, 0), 0, last), 0));
    float du2 = depthTex.Load(int3(clamp(int2(p) - int2(0, 2), 0, last), 0)), dd2 = depthTex.Load(int3(clamp(int2(p) + int2(0, 2), 0, last), 0));
    float3 geo = normal;
    bool right = abs(2.0 * dr - dr2 - depth) < abs(2.0 * dl - dl2 - depth), down = abs(2.0 * dd - dd2 - depth) < abs(2.0 * du - du2 - depth);
    float dx = right ? dr : dl, dy = down ? dd : du;
    if (dx > 0.0 && dy > 0.0) { // at the image's edge a neighbour is the pixel itself: no cross product, the shading normal
        float3 px = camera_relative_position(uint2(right ? r : l), dx) - rel;
        float3 py = camera_relative_position(uint2(down ? d : u), dy) - rel;
        float3 c = cross(px, py);
        if (dot(c, c) > 1e-12) {
            c = normalize(c);
            geo = dot(c, rel) > 0.0 ? -c : c;
        }
    }
    // HDRP Lit's transmission features (2, and 3 with subsurface scattering) are lit through from behind as well, as grass
    // blades and leaves are, unless their diffusion profile is a solid one (skin, thick objects: solid_transmission). Lit
    // like a leaf, skin showed hard, blotchy shadows across cheeks and came out pale.
    bool translucent = (feature == 2u || feature == 3u) && !solid_transmission(s, feature);
    float3 color = a.rgb; // sRGB, as GBuffer0 holds it
    bool forward = false;
    bool hdrpShaded = false; // found as a surface HDRP shades itself: HDRP's light, darkened by the traced shadows (pt_hdrp_shadow)
    bool eye = false;        // of those, one seen by camera rays only: an eye (pt_compose gives it the traced bounced light)
    bool hairLike = false;   // a character's hair, shaded by the tracer: lit through from behind as thin cloth is (mat bit 4)
    // Within 60 m (a distant card or impostor keeps HDRP's look); eyes and unlit decals are in the traced scene for camera
    // rays only (MATERIAL_CAMERA_ONLY). The eyes stop the ray and stay HDRP's (MATERIAL_HDRP_SHADED): its eye shader
    // refracts the iris under the cornea from textures of its own. Missing from the traced scene, they let the ray on to
    // the eye socket just behind them, whose skin the tracer then lit in their place (white eyes without irises).
    if (!deferred && length(rel) < 60.0) {
        float dist = length(rel);
        float3 dir = rel / max(dist, 1e-6);
        float window = 0.03 + 0.01 * dist;
        Hit hit;
        g_cameraRays = true;
        const bool found = trace_closest(g.cameraPos.xyz, dir, max(dist - window, 0.0), dist + window, hit);
        g_cameraRays = false;
        if (found) {
            StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
            // What HDRP shades itself (the eyes, until the tracer reads their textures) keeps HDRP's light, darkened by the
            // traced shadows. Everything else drawn forward is shaded by the tracer, a character's hair too: camera rays stop
            // at its strands wherever they meet them (alpha_opaque), so they no longer slip between the cards of a far
            // character's hair to the head behind.
            const bool hdrp = (materials[hit.material].flags & MATERIAL_HDRP_SHADED) != 0;
            const bool strands = (materials[hit.material].flags & MATERIAL_STRANDS) != 0;
            hdrpShaded = hdrp;
            // Its color the traced material's (the bounced light pt_compose gives it is times it): GBuffer0 holds nothing of a
            // surface drawn forward.
            if (hdrp) color = linear_to_srgb(saturate(hit_material(hit).albedo));
            eye = hdrp && (materials[hit.material].flags & MATERIAL_CAMERA_ONLY) != 0;
            if (!hdrp) {
                // The traced material's finish, texel by texel where it has a mask map: its roughness, and a metal's specular
                // color from its base color (a metal drawn forward had a dielectric's 0.04 and a fifth of its color as diffuse).
                const SurfaceMaterial traced = hit_material(hit);
                color = linear_to_srgb(saturate(traced.albedo));
                normal = hit.normal; geo = hit.normal; // towards the camera; GBuffers 1 and 2 hold nothing of its own
                // Reflection Boost leaves hair as the game made it, as it leaves skin.
                const float forwardBoost = strands ? 0.0 : boost;
                roughness = lerp(traced.roughness, 0.05, forwardBoost);
                f0 = lerp(traced.f0, 0.6, 0.7 * forwardBoost);
                // HDRP's Unlit (seen by camera rays only, lit by the tracer as the floor under it) reflects nothing, as in the
                // game: with a dielectric's 0.04 and the default finish, a portal's black core reflected the two bright lights
                // inside it, a glowing white orb where the game shows a dark one.
                if ((materials[hit.material].flags & MATERIAL_CAMERA_ONLY) != 0) { f0 = 0.0; roughness = 1.0; }
                // Hair lets light through from behind, as thin cloth does (its CLOTH_TRANSMISSION, near RealHair's own
                // _TransmittanceMultiplier of 0.33): the sun or a lamp behind a head lights its hair's edges.
                translucent = strands;
                hairLike = strands;
                deferred = true; forward = true;
            }
        }
    }
    // Glass drawn into the GBuffer, as the game draws its bottles: seen through (pt_specular's refraction) and given a glass's
    // mirror finish where its texture makes it glossy (a label or a cork in the same texture is rough there and stays solid;
    // a mask map's upper end was already 1, so the game's smoothness couldn't be raised instead). The GBuffer says nothing
    // of the material, so the camera ray finds it, as it finds a forward surface; only glossy dielectric texels within 15 m
    // look. With the Reflections row off there is no refraction (the specular pass doesn't run): glass is lit as the solid
    // surface HDRP drew, which was black as glass without it.
    // The same camera ray finds thin cloth (TGPT_MATERIAL_THIN_CLOTH: a banner, laundry, a canvas awning) within 30 m, where the
    // scene has any: lit through from behind as light passes through its weave, dimmer than a leaf lets it (CLOTH_TRANSMISSION).
    bool glass = false, cloth = false;
    const bool glassCandidate = drawnRoughness < 0.5 && max(drawnF0.r, max(drawnF0.g, drawnF0.b)) < 0.1 && length(rel) < 15.0 &&
        (g.settings2.y & OPTION_NO_SPECULAR) == 0;
    const bool clothCandidate = scene_cloth() && length(rel) < 30.0;
    if (deferred && !forward && !translucent && (glassCandidate || clothCandidate)) {
        const float dist = length(rel);
        const float3 dir = rel / max(dist, 1e-6);
        const float window = 0.03 + 0.01 * dist;
        Hit hit;
        g_cameraRays = true;
        const bool found = trace_closest(g.cameraPos.xyz, dir, max(dist - window, 0.0), dist + window, hit);
        g_cameraRays = false;
        if (found) {
            StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
            const uint flags = materials[hit.material].flags;
            glass = glassCandidate && (flags & MATERIAL_GLASS) != 0;
            cloth = clothCandidate && (flags & MATERIAL_THIN_CLOTH) != 0;
        }
        if (glass) { roughness = 0.03; f0 = 0.04; } // glass: nearly a mirror, a dielectric's reflectance
        if (cloth) translucent = true;
    }
    // A complex surface, as NVIDIA's RTXDI sample calls it (thin or finely detailed: a strand of hair, a fence's slat, a
    // blade of grass): a neighbour's normal turns more than 35 degrees from this one's (not its depth: a flat floor seen
    // at a grazing angle jumps in depth from pixel to pixel too). The temporal reuse isn't permuted there (pt_di, pt_gi),
    // which "makes more noise on thin, high-detail objects".
    bool complex = false;
    {
        const int2 around[4] = { l, r, u, d };
        const float3 own = decode_hdrp_normal(n);
        [unroll] for (uint k = 0; k < 4; ++k)
            if (any(around[k] != int2(p)) && depthTex.Load(int3(around[k], 0)) > 0.0 && dot(decode_hdrp_normal(nb.Load(int3(around[k], 0))), own) < 0.82) complex = true;
    }
    // Thin skin: HDRP's transmittance where it lets light through from behind (skin_transmission), kept in place of the
    // specular color, which for transmission is a dielectric's anyway (load_surface puts it back).
    float3 transmittance = 0.0;
    if (deferred && !forward && (feature == 2u || feature == 3u) && solid_transmission(s, feature)) transmittance = skin_transmittance(s);
    const bool thin = any(transmittance >= 0.5 / 255.0);
    surf[p] = float4(rel + g.cameraPos.xyz, length(rel));
    nrm[p] = uint2(pack_normal(normal), (uint(roughness * 255.0 + 0.5) & 0xFF) | (deferred ? 0x100u : 0u) | (translucent ? 0x200u : 0u) | (pack_normal22(geo) << 10));
    mat[p] = uint2(pack_rgba8(float4(color, forward ? 1.0 : 0.0)),
                   pack_rgba8(float4(linear_to_srgb(saturate(thin ? transmittance : f0)),
                                     float((complex ? 1u : 0u) | (glass ? 2u : 0u) | (cloth || hairLike ? 4u : 0u) | (eye ? 8u : 0u) | (thin ? 16u : 0u) | (hdrpShaded ? 32u : 0u)) / 255.0)));
}
