// SPDX-License-Identifier: MIT
// HDRP's GBuffer as the tracer uses it: world position from depth, the normal and roughness from the
// normal buffer, the diffuse color and specular color from GBuffers 0 and 2 (Lit's encoding). A surface HDRP draws
// forward (hair, beards, some props; its lighting is HDRP's own, with the sky's ambient light and none of the traced
// light) is looked for along the camera ray in the traced scene instead, within a few centimetres of HDRP's depth:
// where it is there, the traced material gives its color and roughness and the tracer shades it like the rest.
#include "pt_common.hlsli"
#include "pt_primary.hlsli"

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
    // Hair's own a pixel (its strand and material) and the guides of the hair its camera ray crossed: none, until a pixel
    // below writes its own (load_surface and pt_compose read them for every pixel).
    if (g.hair.x != INVALID) { RWTexture2D<uint2> noHair = ResourceDescriptorHeap[g.hair.x]; noHair[p] = 0; }
    write_guides(p, 0.0, 0.0, 0.0, 0.0, 0.0);
    if (g.primary.y != INVALID) { RWTexture2D<float4> noEmission = ResourceDescriptorHeap[g.primary.y]; noEmission[p] = 0.0; }

    float depth = depthTex.Load(int3(p, 0));
    if (own_rays()) { primary_all(p, depth); return; }
    // Every pixel's surface from its own camera ray (PT-59, pt_primary primary_all): the tracer's first hit in the traced
    // scene. HDRP's G-buffer, below, is the route before it (TGPT_OPTION_GBUFFER_SURFACES), and what the yardstick compares
    // that hit with.
    if (!(depth > 0.0)) { surf[p] = 0.0; nrm[p] = uint2(pack_normal(float3(0, 1, 0)), 0); mat[p] = 0; return; }
    float3 rel = camera_relative_position(p, depth);
    const float hdrpDist = length(rel); // the distance the composite matches this pixel by, whatever surface it is lit as
    float4 a = gb0.Load(int3(p, 0));
    float4 n = nb.Load(int3(p, 0));
    float4 s = gb2.Load(int3(p, 0));
    float3 normal = decode_hdrp_normal(n);
    bool deferred = a.a > 0.5;
    // The pixel's own camera ray first (pt_primary): where it stops at hair, or passes a hair card HDRP drew there, the
    // surface is what that ray found, and the pixel is done. Elsewhere HDRP's G-buffer still gives the surface, below.
    if (primary_pixel(p, depth, deferred)) return;
    uint feature = uint(s.a * 255.0 + 0.5) & 7u;
    // Standard Lit stores the diffuse color in GBuffer0 and the specular color (fast sRGB) in GBuffer2;
    // the other features keep the base color and imply the default dielectric response.
    float3 f0 = feature == 0 ? fast_srgb_to_linear(s.rgb) : 0.04;
    float roughness = saturate(n.a);
    const float drawnRoughness = roughness; const float3 drawnF0 = f0;
    // Reflection Boost: every surface glossier than the game made it, a metal more so and mirroring more (pt_common
    // reflection_boost; 1: nearly a mirror). Not skin (HDRP's transmission through a
    // solid profile): the game's is glossy already (a roughness of 0.18), and at 0.1 its reflectance doubled, so a neck
    // mirrored the dark chin and collar above it in sharp patches, like a rash.
    float boost = solid_transmission(s, feature) ? 0.0 : g.cameraPos.w;
    // Nor grass and leaves (HDRP's transmission through a thin profile): the row is for the world's stone and brick, for
    // armour and metal. Boosted, a dry blade's reflectance doubled (0.04 to 0.079, its roughness 0.97 to 0.88) and every
    // blade sent a mirror ray: the one that met the sky turned white, its neighbour stayed dark (2.0 % of the grass near
    // white at Horns of the South, 0.7 % without the boost).
    if (feature == 2u || feature == 3u) boost = 0.0;
    reflection_boost(roughness, f0, boost);
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
    // A leaf or a blade of grass HDRP drew (its transmission through a thin profile), as pt_common's leaf_tint finds it.
    const bool leaf = deferred && translucent;
    // Its rays leave from its own face. A blade or a leaf's card a pixel or two wide has no neighbour on its own plane: the plane
    // through the pixels beside it (the next blade, the ground behind) was often edge-on to the camera, and every ray below it
    // counted as one that found no light (pt_gi), every reflection was mirrored in it (pt_specular), and where the game leaves
    // a leaf's back face unflipped (14 of its 128 grass and leaf materials: ferns, the field grass) it faced away from HDRP's
    // normal, so all its sky rays failed. Where it is more than 60 degrees off HDRP's normal, that normal serves: the card's
    // own, smooth over the leaf.
    if (leaf && dot(geo, normal) < 0.5) geo = normal;
    // Its mirror reflectance is its diffusion profile's, as HDRP shades it (Lit's FillMaterialTransmission): the game's foliage
    // profile has an index of refraction of 1.1, a reflectance of 0.0023 (88 of its 128 grass and leaf materials; its grass
    // profile 0.087, 23 of them), where the dielectric's 0.04 made every wet blade a small mirror (the white streaks over grass
    // in rain, 2.0 % of it near white at Horns of the South against none in the game's own picture). Skin keeps its 0.04.
    if (leaf) {
        const float profileF0 = profile_f0(leaf_profile(s, feature));
        if (profileF0 > 0.0) f0 = profileF0;
    }
    // And what the game's material says of how much of the sky and its surroundings a blade sees: HDRP's specular occlusion
    // (GBuffer2's red on its transmission features) and the material's ambient occlusion, which HDRP writes into GBuffer3's
    // green behind a tag where a pixel has no baked light and no emission (always while tracing holds the indirect light at
    // 0; Lit's AO_IN_GBUFFER3_TAG). Rays from a blade pass everything the wind moves within SWAY_REACH, so nothing else
    // stands for the blades around it: blades were lit evenly from root to tip (17 % of the game's grass pixels at Horns of the
    // South are dark, roots and depth; traced, 0.4 %). pt_compose takes the ambient occlusion off its bounced and sky light,
    // pt_specular the specular occlusion off its reflections, as HDRP does.
    // Skin's too (PT-43; HDRP's skin, transmission through a solid profile): the game's RealSkin darkens its mouth, nostrils and
    // lips by its own cavity occlusion (_MouthCavityAO, _NostrilCavityAO, _LipsCavityAO), which HDRP takes off its ambient light
    // alone, as Lit's ambient occlusion. Its specular occlusion is left as it was.
    float ambientOcclusion = 1.0, specularOcclusion = 1.0;
    const bool skinOcclusion = deferred && solid_transmission(s, feature);
    if (leaf || skinOcclusion) {
        if (g.inputs0.w != INVALID) {
            Texture2D<float4> gb3 = ResourceDescriptorHeap[g.inputs0.w];
            const float3 baked = gb3.Load(int3(p, 0)).rgb;
            if (abs(baked.r - 2048.0) < 1.0 && abs(baked.b - 1024.0) < 1.0) ambientOcclusion = saturate(baked.g);
        }
        if (leaf) specularOcclusion = saturate(s.r);
    }
    float3 color = a.rgb; // sRGB, as GBuffer0 holds it
    bool forward = false;
    bool eye = false;        // the traced eye material
    bool hairLike = false;   // a character's hair, shaded by the tracer: lit through from behind as thin cloth is (mat bit 4)
    bool hairLobes = false;  // of that, hair with its highlights (mat bit 64): its strand in the hair texture, hairLobe in its bytes
    float2 hairLobe = 0.0;   // its specular map's luminance and its smoothness share (hair_specular)
    // Legacy G-buffer material lookup. Eyes and opaque forward surfaces use their
    // traced materials; there is no HDRP shading-share fallback.
    if (!deferred) {
        float dist = length(rel);
        float3 dir = rel / max(dist, 1e-6);
        float window = 0.03 + 0.01 * dist;
        Hit hit;
        g_cameraRays = true;
        // Hair only where HDRP drew it: within 3 mm (and a tenth of a percent of the distance) of HDRP's depth. A card well
        // in front of it is one HDRP's alpha test cut away at this pixel (the lashes over an eye).
        g_strandsFrom = dist - (0.003 + 0.001 * dist);
        bool found = trace_closest(g.cameraPos.xyz, dir, max(dist - window, 0.0), dist + window, hit);
        g_strandsFrom = -1e30;
        // Hair and brows the game drew as whole cards (held opaque while tracing: the game has no build of their shaders that
        // cuts them so, and HDRP drew each card whole whatever its cut: LEGO hair, black brow cards, 2026-09-28): how much of
        // the pixel the card really covers (strand_coverage) blends its color with what the ray meets behind it (another
        // card, the skin under a brow, the wall past a head), which below half the pixel is, tinted by the card; so a thin
        // strand far off is its share of the pixel, and no pixel flips between the two from frame to frame. The card's
        // texture frame is taken before the ray on (uv_frame reads the last hit's transform).
        float3 dpdu = 0.0, dpdv = 0.0;
        bool frame = false, behindShown = false, mixed = false;
        float3 mixedAlbedo = 0.0;
        if (found) {
            StructuredBuffer<MaterialRecord> cardMaterials = ResourceDescriptorHeap[g.scene0.w];
            const MaterialRecord cm = cardMaterials[hit.material];
            if ((cm.flags & MATERIAL_STRANDS) != 0) {
                frame = uv_frame(hit, dpdu, dpdv);
                const float pixel = length(camera_relative_position(uint2(p.x + 1, p.y), depth) - rel) / max(abs(dot(dir, hit.normal)), 0.2);
                const float coverage = cm.albedoTile >= 0 ? strand_coverage(cm, hit.uv, dpdu, dpdv, frame, pixel) : 1.0;
                // What lies behind: from a tenth of a millimetre past the card (a brow's card lies a millimetre or two over the
                // skin: from 2.5 mm on, the ray began inside the head and met the skull's inside, unlit: black cards); the
                // inside of a mesh is no surface behind (a back face of one-sided geometry), and the card stays whole there.
                Hit behind;
                const float tCard = length(hit.position - g.cameraPos.xyz);
                if (coverage < 0.999 && trace_closest(g.cameraPos.xyz, dir, tCard + max(1e-4, 2e-5 * tCard), g.params.y, behind) &&
                    (behind.front || (cardMaterials[behind.material].flags & MATERIAL_DOUBLE_SIDED) != 0)) {
                    mixedAlbedo = lerp(hit_material(behind).albedo, hit_material(hit).albedo, coverage);
                    mixed = true;
                    if (coverage < 0.5) {
                        hit = behind; behindShown = true;
                        frame = uv_frame(hit, dpdu, dpdv);
                    }
                }
            }
        }
        g_cameraRays = false;
        if (found) {
            StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
            // What HDRP shades itself (the eyes, until the tracer reads their textures) keeps HDRP's light, darkened by the
            // traced shadows. Everything else drawn forward is shaded by the tracer, a character's hair too: camera rays stop
            // at its strands wherever they meet them (alpha_opaque), so they no longer slip between the cards of a far
            // character's hair to the head behind. For now, what HDRP draws forward on a character that the traced scene
            // lacks (a far character's hair, which no captured renderer holds: the ray meets the head under it) keeps HDRP's
            // shading too, until the tracer has it: traced, the head's skin showed as grey hair. A character's own forward
            // surfaces the ray meets, its hair and its eyes (MATERIAL_EYE), are the tracer's.
            StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
            const bool strands = (materials[hit.material].flags & MATERIAL_STRANDS) != 0;
            {
                // The traced material's finish, texel by texel where it has a mask map: its roughness, and a metal's specular
                // color from its base color (a metal drawn forward had a dielectric's 0.04 and a fifth of its color as diffuse).
                const SurfaceMaterial traced = hit_material(hit);
                color = linear_to_srgb(saturate(mixed ? mixedAlbedo : traced.albedo));
                // An eye keeps HDRP's normal where HDRP drew it (its depth pass writes the normal buffer): the traced mesh has
                // only its faces', and a wet eye's highlights broke up into facets. So does hair, from its normal map (the
                // card's flat face shaded each clump as one smooth piece), where the card the ray met is the one HDRP drew.
                const bool eyeSurface = (materials[hit.material].flags & MATERIAL_EYE) != 0;
                normal = (eyeSurface || (strands && !behindShown)) && dot(normal, hit.normal) > 0.5 ? normal : hit.normal;
                // Any other surface drawn forward (a prop) by its mesh's smooth normal (PT-04), as HDRP shades it; a character's
                // mesh brings none and keeps its triangle's.
                if (!eyeSurface && !strands) normal = facing_normal(hit.rounded, -dir);
                geo = hit.normal; // towards the camera; GBuffer2 holds nothing of its own
                // An eye is lit where the ray met it, on its traced mesh (onMesh: its rays leave from just above it, so the lids
                // shadow it; pt_common surface_offset), and marked an eye (bit 8, here without HDRP's shading).
                if (eyeSurface) { rel = hit.position - g.cameraPos.xyz; eye = true; }
                // Behind a card, the surface is lit where the ray met it (HDRP's depth is the card's, and on this route the
                // record's distance stays HDRP's).
                if (behindShown) rel = hit.position - g.cameraPos.xyz;
                // Reflection Boost leaves hair as the game made it, as it leaves skin.
                const float forwardBoost = strands ? 0.0 : boost;
                roughness = traced.roughness; f0 = traced.f0;
                reflection_boost(roughness, f0, forwardBoost);
                // HDRP's Unlit (seen by camera rays only, lit by the tracer as the floor under it) reflects nothing, as in the
                // game: with a dielectric's 0.04 and the default finish, a portal's black core reflected the two bright lights
                // inside it, a glowing white orb where the game shows a dark one.
                if ((materials[hit.material].flags & MATERIAL_CAMERA_ONLY) != 0 && !eyeSurface) { f0 = 0.0; roughness = 1.0; }
                // Hair lets light through from behind, as thin cloth does (its CLOTH_TRANSMISSION, near RealHair's own
                // _TransmittanceMultiplier of 0.33): the sun or a lamp behind a head lights its hair's edges.
                translucent = strands;
                hairLike = strands;
                deferred = true; forward = true;
                // Hair's highlights (hair_specular: HDRP's Kajiya-Kay hair, which the game's RealHair is): the strand's direction
                // from its flow map in the triangle's texture frame (the mesh's tangent along u, its bitangent along v), and
                // the specular map's and smoothness's texel. Its reflections are as rough as HDRP's hair takes its surroundings'
                // light (its second lobe's roughness, times 0.4 for the anisotropy it fixes at 0.8), at hair's reflectance.
                const MaterialRecord hm = materials[hit.material];
                if (strands && (hm.flags & MATERIAL_HAIR_LOBES) != 0 && hm.hairLobeTile >= 0 && frame) {
                    const float3 along = dpdu - normal * dot(normal, dpdu);
                    if (dot(along, along) > 1e-20) {
                        const float3 t = normalize(along);
                        float3 b = cross(normal, t);
                        if (dot(b, dpdv) < 0.0) b = -b;
                        const float4 lobe = hair_lobe(hm.hairLobeTile, hit);
                        const float2 f = lobe.xy * 2.0 - 1.0;
                        const float3 strand = f.x * t + f.y * b + sqrt(1.0 - min(dot(f, f), 1.0)) * normal;
                        if (dot(strand, strand) > 1e-12) {
                            RWTexture2D<uint2> hairTex = ResourceDescriptorHeap[g.hair.x];
                            hairTex[p] = uint2(pack_normal(normalize(strand)), hit.material);
                            hairLobe = float2(lobe.w, lobe.z);
                            hairLobes = true;
                            roughness = saturate((1.0 - hm.hairSmooth2) * 0.4);
                            f0 = HAIR_F0;
                        }
                    }
                }
            }
        }
    }
    // Cloth drawn into the GBuffer, as the game draws its bottles: seen through (pt_specular's refraction) and given a glass's
    // mirror finish where its texture makes it glossy (a label or a cork in the same texture is rough there and stays solid;
    // a mask map's upper end was already 1, so the game's smoothness couldn't be raised instead). The GBuffer says nothing
    // of the material, so the camera ray finds it, as it finds a forward surface; only glossy dielectric texels within 15 m
    // look. With the Reflections row off there is no refraction (the specular pass doesn't run): glass is lit as the solid
    // surface HDRP drew, which was black as glass without it.
    // The same camera ray finds thin cloth (TGPT_MATERIAL_THIN_CLOTH: a banner, laundry, a canvas awning) within 30 m, where the
    // scene has any: lit through from behind as light passes through its weave, dimmer than a leaf lets it (CLOTH_TRANSMISSION).
    bool cloth = false;
    const bool clothCandidate = scene_cloth() && length(rel) < 30.0;
    if (deferred && !forward && !translucent && clothCandidate) {
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
            cloth = clothCandidate && (flags & MATERIAL_THIN_CLOTH) != 0;
        }
        // Cloth the wind moves (the game's Cloth_Wind: a canopy, a banner) is traced where it stands still, and HDRP draws it
        // swayed away from there, near the camera by more than the window (6 cm for a canopy 3 m overhead): seen from under a
        // market's red canopy, its pixels were not taken for cloth, and it was a black sheet lit from the front alone, where the
        // game's glows (PT-39). Where the traced scene has nothing at all within the window, a swaying thin cloth within
        // CLOTH_SWAY_REACH of HDRP's depth is the cloth HDRP drew there (only there: a wall it hangs in front of is found first).
        static const float CLOTH_SWAY_REACH = 0.3;
        if (!found && clothCandidate && scene_sways() && window < CLOTH_SWAY_REACH) {
            Hit swayHit;
            g_cameraRays = true;
            const bool swayed = trace_closest(g.cameraPos.xyz, dir, max(dist - CLOTH_SWAY_REACH, 0.0), dist + CLOTH_SWAY_REACH, swayHit);
            g_cameraRays = false;
            if (swayed) {
                StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
                StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
                cloth = (materials[swayHit.material].flags & MATERIAL_THIN_CLOTH) != 0 && (instances[swayHit.instance].flags & INSTANCE_SWAYS) != 0;
            }
        }
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
    // Skin (a solid profile, HDRP's deferred skin): the light's fade towards the terminator is for it alone (terminator_softening).
    // Marked in the color's alpha, which otherwise holds only "drawn forward" (1): a quarter.
    const bool skin = deferred && !forward && solid_transmission(s, feature);
    // A character's skin on its own traced triangle (PT-43): the camera ray meets the character's mesh within a few millimetres
    // of HDRP's depth (Kandra's skinned positions, the same frame's as HDRP draws). Lit from that hit with that triangle's
    // orientation (onMesh), its rays leave from just above it (pt_common surface_offset) and meet the brow, the nose and the
    // lids around it; from ray_offset's 2 cm out they began beyond them, and the eye sockets took the light the cheeks did. A
    // hair card in front (a brow, a beard) is passed; where no character's triangle is that near (a prop with a skin profile,
    // drawn at another level of detail) the skin keeps ray_offset.
    bool skinOnMesh = false;
    if (skin && length(rel) < 30.0) {
        const float dist = length(rel);
        const float3 dir = rel / max(dist, 1e-6);
        const float near = 0.004 + 0.002 * dist;
        StructuredBuffer<MaterialRecord> skinMaterials = ResourceDescriptorHeap[g.scene0.w];
        StructuredBuffer<InstanceRecord> skinInstances = ResourceDescriptorHeap[g.scene0.x];
        float from = max(dist - near, 0.0);
        [loop] for (uint k = 0; k < 2; ++k) {
            Hit hit;
            if (!trace_closest(g.cameraPos.xyz, dir, from, dist + near, hit)) break;
            if ((skinMaterials[hit.material].flags & MATERIAL_STRANDS) != 0) { from = length(hit.position - g.cameraPos.xyz) + 1e-4; continue; }
            if ((skinInstances[hit.instance].flags & INSTANCE_CHARACTER) != 0) { rel = hit.position - g.cameraPos.xyz; geo = hit.normal; skinOnMesh = true; }
            break;
        }
    }
    surf[p] = float4(rel + g.cameraPos.xyz, hdrpDist);
    nrm[p] = uint2(pack_normal(normal), (uint(roughness * 255.0 + 0.5) & 0xFF) | (deferred ? 0x100u : 0u) | (translucent ? 0x200u : 0u) | (pack_normal22(geo) << 10));
    // Bit 128, a leaf (pt_common load_surface): its reflectance, a single number, in red alone, its ambient and specular
    // occlusion in green and blue.
    // Skin (pt_common load_surface): its reflectance, a dielectric's grey, in red, its ambient occlusion in green.
    const float3 specularBytes = hairLobes ? float3(hairLobe, 0.0)
        : leaf ? float3(linear_to_srgb(saturate(f0)).r, ambientOcclusion, specularOcclusion)
        : skin ? float3(linear_to_srgb(saturate(f0)).r, ambientOcclusion, 1.0)
        : linear_to_srgb(saturate(f0));
    // The color's alpha: 1 drawn forward, 7/16 a character's skin on its traced mesh, a quarter other skin, 0 the rest.
    mat[p] = uint2(pack_rgba8(float4(color, forward ? 1.0 : skinOnMesh ? 0.4375 : skin ? 0.25 : 0.0)),
                   pack_rgba8(float4(specularBytes,
                                     float((complex ? 1u : 0u) | (cloth || hairLike ? 4u : 0u) | (eye ? 8u : 0u) |
                                           (hairLobes ? 64u : 0u) | (leaf ? 128u : 0u)) / 255.0)));
}
