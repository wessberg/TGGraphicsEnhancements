// SPDX-License-Identifier: MIT
// Reflections from the first surface and complete dielectric radiance. Camera,
// mirror and bounce rays use the same glass material and interface transport.
// PT-127 (pt_psr_record.hlsli): where the camera's surface is a still pure mirror and NRD denoises, the pass also fills
// the pixel's record of what the mirror shows (pt_psr_capture.hlsli; PT_PSR_CAPTURE compiles the parts of pt_common and
// pt_medium that take the light apart, into this pass alone).
#define PT_PSR_CAPTURE
#include "pt_common.hlsli"
#include "pt_query.hlsli"
#include "pt_restir.hlsli"
#include "pt_psr_capture.hlsli"

void specular_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> specular = ResourceDescriptorHeap[g.signals.w];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!s.valid || !s.deferred) return;
    // What the pixel's reflection shows, for its filter (pt_common pack_seen; PT-80): nothing, until its ray's end is shaded.
    RWTexture2D<uint2> seenGuide = ResourceDescriptorHeap[g.material.z];
    seenGuide[p] = uint2(0u, 0u);
    Rng rng = make_rng(p, g.size.z, 5);
    // What the pixel shows of what its ray meets (glass: what lies behind it; water: its own path; anything else: its
    // reflection) is shaded by one call of shade_seen below, whichever it is: shade_seen is the tracer's largest piece of
    // code (with the water's path inside it), and a call a kind made the pass three times as long to make.
    Hit seen = (Hit)0;
    float3 seenDir = 0.0;
    bool shade = false;
    float3 glassWeight = 1.0, glassLight = 0.0;
    float glassDistance = HIT_DISTANCE_MISS;
    bool glassFound = false;
    if (s.glass) {
        RWTexture2D<uint2> optical = ResourceDescriptorHeap[g.hair.x];
        const uint2 ids = optical[p];
        seen.position = s.position; seen.normal = s.geoNormal; seen.rounded = s.normal;
        // (The glass's words: pt_primary write_traced.)
        seen.material = ids.y & 0xFFFFFu; seen.instance = ids.x & 0xFFFFFFu; seen.front = (ids.x & 0x80000000u) != 0;
        g_glassFirst = (ids.x & 0x40000000u) != 0 ? 1 : 0; // whether the triangle the camera's ray met has an inside
        seen.footprint = pixel_angle() * length(s.position - g.cameraPos.xyz);
        SurfaceMaterial material = (SurfaceMaterial)0;
        material.albedo = s.albedo; material.normal = s.normal; material.roughness = s.roughness;
        // ON TRIAL (glass_clear): the pixel's normal is the mesh's smooth one (seen.rounded, which the view through is bent
        // by); its reflection is drawn about the material's normal-mapped one, from the words' spare bits.
        if ((ids.x & 0x20000000u) != 0) material.normal = unpack_normal16(((ids.x >> 24) & 0xFu) | ((ids.y >> 20) << 4));
        seenDir = normalize(s.position - g.cameraPos.xyz);
        g_pathFrom = s.position; g_pathGathers = false;
        ray_cone(seen.footprint, pixel_angle());
        g_glassDrawn = true; // this pixel is glass, not its paint, by its own camera ray's draw (pt_primary write_traced)
        glassFound = dielectric_follow(seen, seenDir, material, glassWeight, rng, glassDistance, glassLight);
        shade = glassFound && any(glassWeight > 0.0);
    }
    const bool water = !s.glass && (s.water || s.underwater);
    if (!s.glass && (g.settings2.y & OPTION_NO_SPECULAR) != 0) return;
    if (water) {
        // The water's own surface where the camera's ray meets it (the same triangle pt_primary met), or, seen from inside
        // the water, the path from the camera on (WATER_HERE): shade_path's one water path takes it from there.
        seenDir = normalize(s.position - g.cameraPos.xyz);
        seen.position = g.cameraPos.xyz; seen.water = WATER_HERE;
        if (s.water && !s.underwater) {
            Hit boundary;
            if (trace_water(g.cameraPos.xyz, seenDir, 0.0, length(s.position - g.cameraPos.xyz) + 0.01, boundary)) seen = boundary;
        }
        shade = true;
    }
    // Hair reflects its surroundings in its second lobe's tint, as HDRP's Kajiya-Kay hair takes the sky's and the probes' light.
    float3 tint = 1.0;
    if (s.hair) {
        StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
        const MaterialRecord m = materials[s.hairMaterial];
        tint = s.hairLobe.x * m.hairTint * m.hairStrength2;
    }
    // A reflection or a view through glass counts what it meets whole (emission_weight): nothing samples it for this pixel.
    g_pathFrom = s.position; g_pathGathers = false;
    float3 v = normalize(g.cameraPos.xyz - s.position);
    // The lobe the ray is drawn from: the surface's own, a mirror's too (reflection_alpha, PT-46: with roughness_alpha's floor
    // a perfect mirror's rays strayed and its reflections came out soft).
    float alpha = reflection_alpha(s.roughness);
    // A direction drawn about a smooth, normal-mapped normal can point into the surface's own triangle, where HDRP draws a
    // curve over flat triangles (a metal strap across a door): mirrored back out (above_surface), as the water's and the
    // bounced light's rays are. Into the surface, a reflection met the strap's own triangles from inside, and every other
    // triangle along it reflected the strap's dark body: a saw of dark teeth.
    float3 l = above_surface(ggx_sample(s.normal, v, alpha, rng.next2()), s.geoNormal);
    float nl = dot(s.normal, l), nv = saturate(dot(s.normal, v));
    float offset = surface_offset(s);
    // The ray cone (PT-46): one pixel's width where the camera sees the surface, widening on by the pixel's angle and the
    // lobe's own width (HDRP's alpha): a mirror shows what it reflects as sharp as the camera would see it from there, its
    // footprint the camera pixel's carried over the whole way, to the mirror and on. For testing, none: the top mip.
    // (Glass keeps the cone its interfaces left, water the camera ray's own.)
    if (s.glass || water) { }
    else if ((g.settings2.y & OPTION_REFLECTION_TOP_MIP) != 0) ray_cone(0.0, 0.0);
    else ray_cone(pixel_angle() * s.distance, pixel_angle() + alpha);
    float3 added = 0.0;
    float hitDistance = HIT_DISTANCE_MISS;
    bool reflected = false;
    // Reflections on Glossy Surfaces (TGPT_OPTION_GLOSSY_REFLECTIONS): a rough surface's lobe is broad enough that what it
    // gathers is the light arriving around it, which the radiance cache holds for its cell: that, times what the lobe
    // reflects of it (the split sum's directional albedo), in place of a ray and the path it follows. See-through glass and
    // anything glossier trace as before.
    static const float GLOSSY_ROUGHNESS = 0.6;
    float3 weight = 0.0, origin = s.position;
    bool traced = false, found = false;
    QueryHit met = (QueryHit)0; bool fetchAfter = false; // the reflection ray's answer, fetched after the regrouping
    if (s.glass || water) {
        // Its own ray's end is shaded below.
    } else if ((g.settings2.y & OPTION_GLOSSY_REFLECTIONS) != 0 && s.roughness > GLOSSY_ROUGHNESS && cache_on() && nv > 0.0) {
        added = finite_radiance(tint * specular_albedo(s.f0, s.roughness, nv) * cache_light(s.position, s.geoNormal, rng));
        hitDistance = 1.0; // for Ray Reconstruction, light from around the surface
        reflected = true;
    } else if (nl > 0.0 && nv > 0.0) {
        float3 h = normalize(v + l);
        // Visible-normal sampling leaves F * G1(l) as the estimator's weight.
        weight = tint * fresnel_schlick(s.f0, saturate(dot(v, h))) * smith_g1(nl, alpha * alpha);
        origin = s.position + s.geoNormal * offset;
        // One vertex (its emission and direct light, and the radiance cache's bounced light) keeps reflections to three rays a
        // pixel, and a fourth where the pixel feeds the cache (and one more where it is a smooth metal: shade_seen). Shaded as
        // the camera's own pixel of it is (PT-46: shade_seen), its diffuse and specular light. The ray passes the inside of the
        // hero's first-person body, as every ray traced from a surface does (first_person_inside), and off a mirror-smooth
        // surface (not skin) it sees what only the camera sees: an eye, an unlit decal.
        // The ray is traced here and what it met is fetched AFTER the regrouping below (round 20, PT-117: the review's first
        // finding; the bounced light's first ray has done so since rule-2): the fetch's reads and the material's work run in
        // groups that met the same material, and what crosses the call is the query's answer, not the whole hit.
        g_mirrorRay = s.roughness < 0.15 && !s.skin;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure): the ray, or the shading of what it met, left out
        found = !speed_measure(SPEED_M_NO_RAY) && !speed_measure2(SPEED_M2_NO_RAY) && query_from_surface(origin, l, 0.0, g.params.y, surface_near_back(s), met);
#else
        found = query_from_surface(origin, l, 0.0, g.params.y, surface_near_back(s), met);
#endif
        g_mirrorRay = false;
        seenDir = l; traced = true; shade = found; fetchAfter = found;
    }
    // ON TRIAL (pt_common lamp_around; off: nothing of it runs): the lobe's rays of this pixel (this frame's or not: one the
    // normal map pointed under the surface was not traced) are weighed against what stands by a lamp, found on purpose
    // below; the ray just traced tells shade_seen so (g_nearPending, which it reads and clears).
    const bool lobeRays = !s.glass && !water && !(reflected && !traced) && nv > 0.0 && lamp_around() > 0.0;
    const NearRay lobeRay = near_ray(s.position, s.normal, s.geoNormal, v, alpha);
    if (lobeRays && traced && found) g_nearPending = lobeRay;
#ifdef PT_SER
    // With SER: the shading of what is seen, regrouped by the material its ray met and by whether the pixel feeds the cache
    // (16 bits), or in a frame with the frame word's bit 4 (tracer.cpp ReflectionsSort) with no hint at all: the same picture,
    // and which is faster depends on the scene (R19, PT-99: no hint where the camera sees much water, the beach 75.5 ms
    // against 91 to 93; the material in a town, 22.9 against 30.6). One call, its bit count worked out (DXC copies a call
    // whose bits are another constant on another path: rule-2).
    uint keyMaterial = shade ? seen.material : 0xFFFFu;
    if (fetchAfter) {
        StructuredBuffer<InstanceRecord> keyInstances = ResourceDescriptorHeap[g.scene0.x];
        StructuredBuffer<uint> keyMaterials = ResourceDescriptorHeap[g.scene1.x];
        keyMaterial = keyMaterials[keyInstances[met.instance].materialBase + met.geometry]; // (fetch_hit's own h.material)
    }
    dx::MaybeReorderThread(regroup_key(keyMaterial, cache_feeds(p, 2u)) & 0xFFFFu, select((g.size.w & 4u) != 0u, 0u, 16u));
#endif
    if (fetchAfter) seen = fetch_hit(met.instance, met.geometry, met.primitive, met.bary, met.toWorld, seenDir, origin + seenDir * met.t, origin);
    float3 shown = 0.0;
#ifdef PT_SPEED_MEASURE
    if (speed_measure(SPEED_M_NO_SHADE)) shade = false;
#endif
    // PT-127: a still pure mirror's pixel, where NRD denoises: the record's depth and motion now, from the two hits; its
    // light as shade_seen adds it up (g_psrSlot is the one word of it carried across the rays below).
    g_psrSlot = traced && found && psr_mirror(s, p) && psr_begin(p, s, seen, met) ? psr_index(p) : INVALID;
    if (shade) shown = shade_seen(seen, seenDir, rng, cache_feeds(p, 2u));
    g_nearPending.alpha = 0.0;
#ifdef PT_SPEED_MEASURE // the measuring build alone: the pixel's counters, what it sees shaded, go to the tracer's buffer (pt_common speed_count_flush)
    speed_count_flush(1u);
#endif
    if (s.glass) {
        if (any(glassWeight > 0.0)) glassLight += glassWeight * (glassFound ? shown : sky_radiance(seenDir));
        specular[p] = float4(to_storage(finite_radiance(glassLight)), glassFound ? glassDistance : HIT_DISTANCE_MISS);
        // The surface behind the glass, where its walk ended on one (the sky behind it: none).
        if (shade) seenGuide[p] = pack_seen(SEEN_THROUGH, g_seenNormal, g_seenReflectance);
        return;
    }
    if (water) {
        // The whole of a water pixel's light is this signal (pt_compose leaves it no diffuse light); the distance the
        // denoisers move it by is the path's first event's past the surface (g_waterGuide).
        specular[p] = float4(shown / g.radiance.x, g_waterGuide);
        seenGuide[p] = pack_seen(SEEN_WATER, 0.0, 0.0);
        return;
    }
    if (traced) {
        const Hit hit = seen;
        float3 radiance = found ? shown : sky_radiance(l);
        // HDRP's fog over the way from the surface to what it reflects (PT-37: fogged); the camera's way to the surface is
        // HDRP's own. The lamps and the sun added below are dimmed by the fog on their way already (light_incident, sun_fog).
        g_psrFogTransmission = 1.0;
        radiance = fogged(radiance, origin, l, found ? length(hit.position - origin) : HIT_DISTANCE_MISS, rng);
        added = finite_radiance(weight * radiance);
        // PT-127: the mirror's record completed: its two hit distances, and what the mirror passes of its end's light (the
        // lobe's weight, the surface's specular occlusion, the fog over the ray: fogged left its transmittance).
        if (g_psrSlot != INVALID) { psr_finish(p, s, hit, l, weight * s.specularOcclusion * g_psrFogTransmission); g_psrSlot = INVALID; }
        // From the surface itself, not from the ray's start a little off it: NVIDIA's DLSS-RR guide (3.4.9) wants the distance
        // from the specular ray's origin "on the Primary Surface" to its hit, by which it moves the reflected image as its
        // mirror image moves. The way along the ray from the surface to the plane of what it met (exact for a flat one, where
        // the ray's start moved the hit along it); from the ray's start, a ceiling 6 m over a floor mirror came out 1.6 cm near.
        if (found) {
            const float3 toHit = hit.position - s.position;
            const float across = dot(hit.normal, l);
            const float along = abs(across) > 0.2 ? dot(hit.normal, toHit) / across : 0.0;
            hitDistance = along > 0.0 ? along : length(toHit);
        }
        reflected = true;
    }
    // ON TRIAL (pt_common lamp_around_light): what stands right by a lamp, found on purpose for this pixel's lobe: times the
    // weight the lobe's own ray carries for that direction (visible-normal sampling's F * G1, as above), with HDRP's fog over
    // the way as the traced ray's light has it.
    float3 nearAdded = 0.0;
    if (lobeRays) {
        float3 nearDir, nearPoint, nearNormal;
        const float3 lit = lamp_around_light(lobeRay, offset, surface_near_back(s), true, s.roughness < 0.15 && !s.skin, rng, nearDir, nearPoint, nearNormal);
        const float nearNl = dot(s.normal, nearDir);
        if (nearNl > 0.0 && any(lit > 0.0)) {
            const float3 start = s.position + s.geoNormal * offset;
            const float3 nearWeight = tint * fresnel_schlick(s.f0, saturate(dot(v, normalize(v + nearDir)))) * smith_g1(nearNl, alpha * alpha);
            nearAdded = finite_radiance(nearWeight * fogged(lit, start, nearDir, length(nearPoint - start), rng));
        }
    }
    added += nearAdded;
    // A leaf or a blade of grass reflects its surroundings as far as its material's specular occlusion leaves them (pt_prepare:
    // HDRP's, from its mask map), as HDRP's reflections on it are: a blade deep in a tuft sees little of the sky. 1 elsewhere.
    added *= s.specularOcclusion;
    // ON TRIAL: this frame's lobe ray was not traced, but its on-purpose light was found: the light alone, the distance as it was.
    if (!reflected && any(nearAdded > 0.0)) { specular[p] = float4(specular[p].rgb + to_storage(added), specular[p].a); return; }
    if (!reflected) return;
    specular[p] = float4(specular[p].rgb + to_storage(added), hitDistance);
    // The surface the reflected ray met (the sky, or a rough surface's light from the radiance cache: none).
    if (traced && found) seenGuide[p] = pack_seen(SEEN_REFLECTED, g_seenNormal, g_seenReflectance);
}
PT_ENTRY(specular_pixel, specular)
