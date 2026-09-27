// SPDX-License-Identifier: MIT
// Path-traced reflections: one GGX visible-normal sample per pixel from HDRP's normal,
// roughness and specular color; the path vertex it reaches is shaded like a GI vertex. Added to the
// specular signal the DI pass wrote. See-through glass adds what lies behind it (refracted) to the same signal.
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

// The lamps a reflection ray from position meets before tmax: each light's sphere (the size HDRP gives it, which its
// shadows soften by) shows its radiance, I / (pi r^2) with its range, cone and cookie as position gets them, where the
// ray meets it (from inside the sphere, where the ray leaves it). See-through glass (roughness 0.03) takes its lamps'
// highlights from here and not from the direct light's samples (pt_di_spatial), a point of the lamp's disc with the
// specular lobe evaluated there, whose peak ggx_d's 1e-7 caps for a surface that smooth: 2 % of a lamp's mirror image
// reached the harness's glass cube over 64 frames, with the clamps and boiling filters off as with them on, so bottles
// showed none of the candles around them.
float3 reflected_lamps(float3 position, float3 origin, float3 dir, float tmax) {
    uint first, count;
    if (!light_cell(position, first, count)) return 0.0;
    StructuredBuffer<uint2> entries = ResourceDescriptorHeap[g.grid.y];
    float3 sum = 0.0;
    for (uint i = 0; i < count; ++i) {
        const LightRecord l = load_light(entries[first + i].x);
        if (!reflected_as_sphere(l)) continue;
        const float3 oc = origin - l.position;
        const float b = dot(oc, dir), disc = b * b - (dot(oc, oc) - l.radius * l.radius);
        if (disc <= 0.0) continue;
        const float t = -b - sqrt(disc) > 0.0 ? -b - sqrt(disc) : -b + sqrt(disc);
        if (t <= 0.0 || t >= tmax) continue;
        float3 toLight; float dist;
        const float3 e = light_incident(l, position, toLight, dist);
        sum += e * max(dist * dist, l.radius * l.radius + 1e-4) / (PI * l.radius * l.radius);
    }
    return sum;
}

void specular_pixel(uint2 p) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> specular = ResourceDescriptorHeap[g.signals.w];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    if (!s.valid || !s.deferred) return;
    Rng rng = make_rng(p, g.size.z, 5);
    // A reflection or a view through glass counts what it meets whole (emission_weight): nothing samples it for this pixel.
    g_pathFrom = s.position; g_pathGathers = false;
    float3 v = normalize(g.cameraPos.xyz - s.position);
    float alpha = roughness_alpha(s.roughness);
    // A direction drawn about a smooth, normal-mapped normal can point into the surface's own triangle, where HDRP draws a
    // curve over flat triangles (a metal strap across a door): mirrored back out (above_surface), as the water's and the
    // bounced light's rays are. Into the surface, a reflection met the strap's own triangles from inside, and every other
    // triangle along it reflected the strap's dark body: a saw of dark teeth.
    float3 l = above_surface(ggx_sample(s.normal, v, alpha, rng.next2()), s.geoNormal);
    float nl = dot(s.normal, l), nv = saturate(dot(s.normal, v));
    float offset = ray_offset(s.distance);
    float3 added = 0.0;
    float hitDistance = HIT_DISTANCE_MISS;
    bool reflected = false;
    // Reflections on Glossy Surfaces (TGPT_OPTION_GLOSSY_REFLECTIONS): a rough surface's lobe is broad enough that what it
    // gathers is the light arriving around it, which the radiance cache holds for its cell: that, times what the lobe
    // reflects of it (the split sum's directional albedo), in place of a ray and the path it follows. See-through glass and
    // anything glossier trace as before.
    static const float GLOSSY_ROUGHNESS = 0.6;
    if ((g.settings2.y & OPTION_GLOSSY_REFLECTIONS) != 0 && s.roughness > GLOSSY_ROUGHNESS && !s.glass && cache_on() && nv > 0.0) {
        added = clamp_radiance(specular_albedo(s.f0, s.roughness, nv) * cache_light(s.position, s.geoNormal, rng), LIMIT_REFLECTION);
        hitDistance = 1.0; // for Ray Reconstruction, light from around the surface
        reflected = true;
    } else if (nl > 0.0 && nv > 0.0) {
        float3 h = normalize(v + l);
        // Visible-normal sampling leaves F * G1(l) as the estimator's weight.
        float3 weight = fresnel_schlick(s.f0, saturate(dot(v, h))) * smith_g1(nl, alpha * alpha);
        float3 origin = s.position + s.geoNormal * offset;
        Hit hit;
        float3 radiance;
        // One vertex (its emission and direct light, and the radiance cache's bounced light) keeps reflections to three rays a
        // pixel, and a fourth where the pixel feeds the cache.
        const bool found = trace_from_surface(origin, l, 0.0, g.params.y, near_back(s.distance), hit);
        PT_REORDER(found ? hit.material : 0xFFFFu); // with SER: the reflected surface's shading, grouped by material
        if (found) radiance = shade_path(hit, l, 1u, rng, cache_feeds(p, 2u));
        else radiance = sky_radiance(l);
        if (s.glass) radiance += reflected_lamps(s.position, origin, l, found ? length(hit.position - origin) : g.params.y);
        // And the sun's disc, where the ray escapes towards it: its illuminance over its solid angle (the direct light's
        // sample of it, a GGX lobe that narrow evaluated at a point of the disc, brought 2.5 % of it: ggx_d's 1e-7 caps its peak).
        if (s.glass && !found && g.sunColor.w > 0.0 && dot(l, g.sunDir.xyz) >= g.sunDir.w)
            radiance += g.sunColor.rgb / (2.0 * PI * max(1.0 - g.sunDir.w, 1e-7));
        added = clamp_radiance(weight * radiance, LIMIT_REFLECTION);
        hitDistance = found ? length(hit.position - origin) : HIT_DISTANCE_MISS;
        reflected = true;
    }
    // See-through glass (the smooth texels of a MATERIAL_GLASS surface, pt_prepare): into it at HDRP's surface (an index of
    // refraction of 1.5), out where the ray meets the glass again from inside (straight on past the critical angle), on to
    // what lies behind, shaded with its bounced light and the sky's as well (the Bounces setting's path: seen through glass,
    // a wall lit by bounced light alone was black by its direct light). Something other than glass met inside (a candle in a lantern)
    // is what it shows; no far side within a metre, and it is a thin pane, gone through straight. Tinted by the glass's
    // color and weighted by the light the Fresnel term lets in; its diffuse light goes (pt_compose).
    if (s.glass && nv > 0.0) {
        const float3 n = dot(s.normal, v) >= 0.0 ? s.normal : -s.normal;
        float3 t = refract(-v, n, 1.0 / GLASS_IOR);
        if (dot(t, t) < 0.5) t = -v;
        const float3 inside = s.position - s.geoNormal * min(offset, 0.004);
        StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
        Hit far;
        g_glassSolid = true;
        const bool farFound = trace_closest(inside, t, 0.0, 1.0, far);
        g_glassSolid = false;
        float3 seen = 0.0, origin2 = inside, dir2 = -v;
        float seenDistance = HIT_DISTANCE_MISS;
        bool shown = false;
        if (farFound && (materials[far.material].flags & MATERIAL_GLASS) != 0) {
            // far.normal faces the ray's origin, back into the glass: out of it (1.5 to 1), or on as it was past the critical angle.
            const float3 leaving = refract(t, far.normal, GLASS_IOR);
            dir2 = dot(leaving, leaving) < 0.5 ? t : leaving;
            origin2 = far.position - far.normal * 0.002;
        } else if (farFound) {
            seen = shade_path(far, t, 1u + min(g.settings.z, 3u), rng);
            seenDistance = length(far.position - s.position);
            shown = true;
        }
        if (!shown) {
            Hit behind;
            if (trace_closest(origin2, dir2, 0.0, g.params.y, behind)) {
                seen = shade_path(behind, dir2, 1u + min(g.settings.z, 3u), rng);
                seenDistance = length(behind.position - s.position);
            } else seen = sky_radiance(dir2);
        }
        // The hit distance Ray Reconstruction and Ray Regeneration move the specular signal by is that of the light it mostly
        // holds: where the glass lets through more than it reflects (by their weights, which unlike their light don't change
        // from frame to frame), the distance to what is seen behind it. Through a pane that lies as far behind the glass as
        // a mirror's image lies behind the mirror, so the image seen through it moves with what it shows.
        const float3 fresnel = fresnel_schlick(s.f0, nv);
        if (luminance((1.0 - fresnel) * s.albedo) > luminance(fresnel)) hitDistance = seenDistance;
        added += clamp_radiance((1.0 - fresnel_schlick(s.f0, nv)) * s.albedo * seen, LIMIT_REFLECTION);
        reflected = true;
    }
    if (!reflected) return;
    specular[p] = float4(specular[p].rgb + to_storage(added), hitDistance);
}
PT_ENTRY(specular_pixel, specular)
