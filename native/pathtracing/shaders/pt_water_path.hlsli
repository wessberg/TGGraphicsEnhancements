// SPDX-License-Identifier: MIT
// A path through water: its surface crossed or mirrored by chance (pt_interface.hlsli, the boundary glass uses too), the
// water's absorption and scattering over each stretch inside it, foam and whatever else it meets lit as a path vertex.
#ifndef PT_WATER_PATH_HLSLI
#define PT_WATER_PATH_HLSLI
float water_epsilon(float3 p) { return max(1e-5, max(abs(p.x), max(abs(p.y), abs(p.z))) * 5e-7); }
// ON TRIAL (PT-99, docs/agents/perf-1.md; CMake's TGPT_REGROUP_WATER, OFF by default: then none of this is in any pass. ON:
// in the two reordering passes alone, and there only with the hidden "Speed trial" numbers below): threads regrouped by
// what they do next. A GPU runs 32 pixels as one group, and the group runs every branch any of its pixels takes. With
// Shader Execution Reordering pt_gi and pt_specular regroup their threads once, by the material their ray met and by
// whether the pixel feeds the radiance cache (pt_common regroup_key); these go further:
//   2: the water's path, at every turn of its loop after the first (water_path): a turn is a scatter point's sun through
//      the waves, or a surface's material and lights, or the surface's crossing, and the loop goes on until the group's
//      longest path ends;
//   4: the points that make their own patch of waves, before they make it (water_sun).
// No thread's own numbers change (each keeps its random numbers and draws them in the same order): the picture is the same.
#if defined(PT_SER) && defined(PT_REGROUP_WATER)
#define PT_REGROUPS 1
static const uint SPEED_WATER_REGROUP = 2u, SPEED_WATER_REGROUP_PATCH = 4u;
void water_regroup(uint bit, uint key) { if ((speed_trial() & bit) != 0u) dx::MaybeReorderThread(key & 0xFFFFu, 16); }
#endif
#include "pt_water_sun.hlsli"
#include "pt_water_lights.hlsli"

// Unbounded path length, terminated by weighted roulette. No cache or screen-space material substitution.
// surfaceSlot/entering identify the incident medium when called from a known boundary; -1 queries the origin.
// startWidth: how wide the ray that brought the path here is at its start (its ray cone at the water; 0 from a point).
float3 water_path(float3 origin, float3 direction, int surfaceSlot, bool entering, float startWidth, inout Rng rng, out float guideDistance) {
    int medium = surfaceSlot >= 0 ? (entering ? -1 : surfaceSlot) : water_medium(origin);
    // The width of the path's own ray as it goes (PT-131; pt_water_sun.hlsli, the cell by the ray's width): at its start
    // the width of the ray that met the water, growing along each stretch by the cone's spread (g_coneSpread: the pass's
    // own ray's, a pixel's angle for the camera's). A facet that mirrors the path adds its lobe's width to the spread; one
    // that lets it through adds the lobe's times the indices' difference, and on the way INTO the water Snell's law
    // narrows the spread by the index; a solid's highlight adds its lobe, its diffuse light and a scatter point make
    // the spread a bounce's (DIFFUSE_SPREAD). Kept on the narrow side throughout (a ray that leaves the water is not
    // widened by the index, a scatter point counts as a bounce): too narrow costs time, too wide would cost the pattern.
    float rayWidth = max(startWidth, 0.0), raySpread = g_coneSpread >= 0.0 ? g_coneSpread : pixel_angle();
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): the path counted, and how it starts
    ++g_scWaterPaths;
    if (medium >= 0) ++g_scWaterBelow;
    if (length(origin - g.cameraPos.xyz) > 60.0) ++g_scWaterFar;
    g_scWaterAway += uint(min(length(origin - g.cameraPos.xyz), 10000.0));
    if (origin.y > g.cameraPos.y) { ++g_scWaterOver; g_scWaterUp += uint(min(origin.y - g.cameraPos.y, 1000.0)); }
    else g_scWaterDown += uint(min(g.cameraPos.y - origin.y, 1000.0));
    uint speedTurns = 0u;
#endif
    float3 weight = 1.0, radiance = 0.0;
    bool delta = true, primary = true;
    // Who has asked for the sun, so that it is counted once. A vertex inside the water asks for it through the surface: a
    // scatter point with water_sun, a surface under water with the caustics' landed light (or, the Caustics row off, both
    // with the straight ray). Either question covers the sun's light that crosses the surface ONCE on its way to the
    // vertex. So a path that leaves such a vertex, crosses the surface once and runs into the sun's disc has that light
    // already: it is not added again (asked, crossings, crossed). Until 2026-10-08 it was the only way a scatter point
    // got the sun (one path in a hundred thousand, carrying it all: PT-85's green dots), and it doubled the bed's.
    // What stays the disc's own: the camera's mirror chain (the sun's image in the water, the sun seen from under it), and
    // a chain that meets the surface twice or more after its vertex (no question asks for that light).
    // And the sun's own image in the water: a facet that mirrors the path is asked for the sun too (its glint: through the
    // lobe the water's smoothness makes), so a path that goes on from a mirroring facet and runs into the disc has that
    // light already (glinted). A lamp's ball and a glowing surface are wide against the lobe and are met by the lobe's own
    // directions, as before.
    bool asked = false, crossed = false, glinted = false;
    bool lampsAsked = false;
    uint crossings = 0;
    guideDistance = HIT_DISTANCE_MISS;
    uint vertices = 0;
    [loop] while (true) {
        Hit hit;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure3): the path ends after its first turn; its turns counted
        if (speedTurns >= 1u && speed_measure3(SPEED_M3_ONE_TURN)) break;
        ++speedTurns; ++g_scWaterTurns;
        if (medium >= 0) ++g_scWaterTurnsIn;
#endif
#ifdef PT_REGROUPS
        // ON TRIAL (water_regroup, above): the turn's free flight is drawn here, before the turn's ray (which draws no
        // random number: trace_closest), with the same two random numbers in the same order as below.
        float3 sigmaDrawn = 0.0; uint channelDrawn = 0u; float flightDrawn = 1e30;
        if (medium >= 0) {
            sigmaDrawn = max(water_row(uint(medium), 14).xyz, 0.0);
            channelDrawn = min(uint(rng.next() * 3.0), 2u);
            flightDrawn = sigmaDrawn[channelDrawn] > 0.0 ? -log(max(1.0 - rng.next(), 1e-38)) / sigmaDrawn[channelDrawn] : 1e30;
        }
#endif
        const bool met = trace_closest(origin, direction, water_epsilon(origin), 1e30, hit);
        const float distance = met ? length(hit.position - origin) : 1e30;
#ifdef PT_REGROUPS
        // ON TRIAL (water_regroup, above): what the thread does this turn is its key: 0 it scatters inside the water,
        // 0x2000 it meets the water's surface, 0x4000 something solid (and its material), 0x6000 nothing. Not on the
        // path's first turn: its threads all do the same there (the pass's own regrouping put them together).
        if (vertices > 0u || !primary)
            water_regroup(SPEED_WATER_REGROUP, medium >= 0 && flightDrawn < distance ? 0u : (!met ? 0x6000u : (hit.water != 0 ? 0x2000u : (0x4000u | (uint(hit.material) & 0x1FFFu)))));
#endif
        if (medium >= 0) {
#ifdef PT_REGROUPS
            const float3 sigma = sigmaDrawn;
            const float3 scatter = sigma * saturate(g.waterScatter[medium].rgb);
            const uint channel = channelDrawn;
            const float freeFlight = flightDrawn;
#else
            const float3 sigma = max(water_row(uint(medium), 14).xyz, 0.0);
            const float3 scatter = sigma * saturate(g.waterScatter[medium].rgb);
            // RGB channel-mixture free-flight sampling: attenuation is applied before accumulating paths.
            const uint channel = min(uint(rng.next() * 3.0), 2u);
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure3): the water's body scatters nothing
            const float freeFlight = sigma[channel] > 0.0 && !speed_measure3(SPEED_M3_NO_SCATTER) ? -log(max(1.0 - rng.next(), 1e-38)) / sigma[channel] : 1e30;
#else
            const float freeFlight = sigma[channel] > 0.0 ? -log(max(1.0 - rng.next(), 1e-38)) / sigma[channel] : 1e30;
#endif
#endif
            const float travel = min(distance, freeFlight);
            const float3 transmission = segment_transmittance(sigma, travel);
            if (freeFlight < distance) {
                const float pdf = dot(transmission * sigma, 1.0 / 3.0);
                if (!(pdf > 0.0)) break;
                if (guideDistance == HIT_DISTANCE_MISS) guideDistance = travel;
                weight *= transmission * scatter / pdf;
                origin += direction * travel;
                rayWidth += raySpread * travel; // (the ray's width at the scatter point; from here on in any direction)
                raySpread = max(raySpread, DIFFUSE_SPREAD);
                // The sun at the scatter point, through the waves: what the water sends on of it in every direction alike
                // (its phase function is even: 1 / 4 pi a steradian).
                radiance += weight * water_sun(uint(medium), origin, rayWidth, rng) / (4.0 * PI);
                lampsAsked = water_body_lamps_on();
                if (lampsAsked) radiance += weight * water_vertex_lights(uint(medium), origin, false, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, rng);
                asked = true; crossings = 0; crossed = false; glinted = false;
                const float z = 1.0 - 2.0 * rng.next(), phi = 2.0 * PI * rng.next();
                const float radius = sqrt(max(0.0, 1.0 - z * z));
                direction = float3(radius * cos(phi), z, radius * sin(phi));
                // The lamps and the sky reach the scatter point by this direction alone (the sun: water_sun above).
                delta = true;
            } else {
                const float pdf = dot(transmission, 1.0 / 3.0);
                if (!(pdf > 0.0)) break;
                weight *= transmission / pdf;
            }
            if (freeFlight < distance) {
                ++vertices;
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure2): the path ends after its first scatter point
                ++g_scWaterScatter;
                if (speed_measure2(SPEED_M2_ONE_SCATTER)) break;
#endif
                const float survive = min(0.95, max(weight.r, max(weight.g, weight.b)));
                if (!(survive > 0.0) || rng.next() >= survive) break;
                weight /= survive;
                continue;
            }
        }
        if (!primary && guideDistance == HIT_DISTANCE_MISS) guideDistance = met ? distance : HIT_DISTANCE_MISS;
#ifdef PT_SPEED_MEASURE // the measuring build alone: no lamp's ball is looked for along the path
        if (delta && !speed_measure2(SPEED_M2_NO_BALLS) && !(lampsAsked && (crossings == 0u || (crossings == 1u && crossed))))
#else
        if (delta && !(lampsAsked && (crossings == 0u || (crossings == 1u && crossed))))
#endif
            radiance += weight * reflected_lamps(origin, origin, direction, distance);
        if (!met) {
            radiance += weight * sky_radiance(direction);
            if (delta && !glinted && !(asked && crossings == 1u && crossed) && g.sunColor.w > 0.0 && dot(direction, g.sunDir.xyz) >= g.sunDir.w)
                radiance += weight * g.sunColor.rgb * sun_fog(origin) / (2.0 * PI * max(1.0 - g.sunDir.w, 1e-7));
            break;
        }
        rayWidth += raySpread * distance; // (the ray's width where it meets something)
        const bool isWater = hit.water != 0;
        const uint slot = isWater ? hit.water - 1 : 0;
        const bool foam = isWater && rng.next() < water_foam(slot, hit.waterBase);
        if (isWater && !foam) {
            // The boundary by the game's own wave normal there (hit.rounded, on the path's side) and the water's own
            // smoothness (water_smoothness: HDRP's, near and far): one of the surface's small facets, drawn from those the
            // path can see (GGX's visible normals, as glass's facets are: pt_dielectric), mirrors the path or lets it
            // through by the exact Fresnel share at that facet. The triangle (hit.normal) says which side a direction
            // leaves on. Where the waves are finer than the triangles (far from the camera) a mirrored direction can point
            // into the triangle: it is turned back out, as every normal-mapped mirror of the tracer's is (above_surface).
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
            ++g_scWaterSurface;
#endif
            const float ni = hit.front ? 1.0 : WATER_IOR, nt = hit.front ? WATER_IOR : 1.0;
            const float3 view = -direction;
            const float3 facing = facing_normal(hit.rounded, view);
            const float rough = 1.0 - water_smoothness(slot, hit.position);
            // The sun's glint: asked for at a point of the sun's disc, through the lobe the smoothness makes (a small bright
            // image where the water is smooth, a wide soft sheen where it is rough), by the exact Fresnel share at the
            // facet that mirrors the sun to the path. Until 2026-10-08 the surface was a clear mirror facet and the glint a
            // chance hit of the disc: single burnt pixels where the game's glints are soft.
#ifdef PT_SPEED_MEASURE // the measuring build alone: the glint is not asked for
            if (g.sunColor.w > 0.0 && !speed_measure2(SPEED_M2_NO_GLINT)) {
#else
            if (g.sunColor.w > 0.0) {
#endif
                const float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
                const float nl = dot(facing, l), nv = dot(facing, view);
                if (dot(hit.normal, l) > 0.0 && nl > 0.0 && nv > 0.0) {
                    const float a = roughness_alpha(rough), a2 = a * a;
                    const float3 h = normalize(view + l);
                    const float lobe = fresnel_dielectric(dot(view, h), nt / ni) * ggx_d(saturate(dot(facing, h)), a2) * smith_g1(nl, a2) * smith_g1(nv, a2) / (4.0 * nv);
                    if (lobe > 0.0 && isfinite(lobe)) {
                        const bool savedKnown = g_waterMediumKnown; const int savedMedium = g_waterMedium;
                        g_waterMediumKnown = true; g_waterMedium = medium;
                        radiance += weight * g.sunColor.rgb * lobe * sun_fog(hit.position) *
                                    transmittance(hit.position + hit.normal * (0.002 + 4.0 * water_epsilon(hit.position)), l, 0.0, g.params.y);
                        g_waterMediumKnown = savedKnown; g_waterMedium = savedMedium;
                    }
                }
            }
            const float alpha = reflection_alpha(rough);
            const float3 mirrored = ggx_sample(facing, view, alpha, rng.next2());
            const Interface boundary = dielectric_interface(direction, normalize(view + mirrored), ni, nt);
            const bool transmitted = rng.next() >= boundary.reflectance;
            ++crossings; crossed = transmitted;
            // (The ray's width from here on: rayWidth, at the top.)
            raySpread = !transmitted ? raySpread + alpha : hit.front ? (raySpread + alpha * (WATER_IOR - 1.0)) / WATER_IOR : raySpread + alpha * (WATER_IOR - 1.0);
            if (transmitted) {
                direction = boundary.transmitted;
                const float through = dot(direction, hit.normal);
                if (through > 0.0) direction -= 2.0 * through * hit.normal;
                weight *= interface_radiance(boundary);
                medium = hit.front ? int(slot) : -1;
            } else direction = above_surface(mirrored, hit.normal);
            direction = normalize(direction);
            // What the facets on the way out hide of the outgoing direction (Smith's, as glass's).
            weight *= smith_g1(abs(dot(facing, direction)), alpha * alpha);
            delta = true; glinted = !transmitted;
        } else {
#ifdef PT_SPEED_MEASURE // the measuring build alone: what the path meets that is no water is not shaded
            if (speed_measure2(SPEED_M2_NO_SOLID)) break;
            ++g_scWaterSolid;
#endif
            SurfaceMaterial m = (SurfaceMaterial)0;
            if (foam) {
                m.albedo = g.waterFoam[slot * 6 + 5].rgb; m.f0 = pow((WATER_IOR - 1.0) / (WATER_IOR + 1.0), 2.0);
                m.roughness = g.waterFoam[slot * 6].z; m.normal = hit.rounded;
            } else m = hit_material(hit);
            if (!foam) radiance += weight * path_emission(hit, m.emissive, origin, !delta);
            const float epsilon = water_epsilon(hit.position);
            // The path already knows its current medium. Do not trace upward again for every shadow sample.
            const bool savedKnown = g_waterMediumKnown; const int savedMedium = g_waterMedium;
            g_waterMediumKnown = true; g_waterMedium = medium;
            radiance += weight * direct_light(hit.position, hit.normal, m.normal, m.albedo, m.f0,
                                               roughness_alpha(m.roughness), -direction, epsilon, rng, m.through);
            g_waterMediumKnown = savedKnown; g_waterMedium = savedMedium;
            // Under water the sun and the lamps reach a surface only through the waves, bent: a straight shadow ray stops
            // at the water (occluded_any). That light is the caustics' (pt_caustics.hlsl: traced through the same waves,
            // absorbed over its way), read where it landed; nothing else of the path brings it (the sun's disc and the
            // lamps are met by mirror and refraction directions alone).
            // ON TRIAL (TGPT_OPTION_CAUSTICS_LIT; off: the line after, as before): that light as light from the direction
            // it came from, by the surface's own normal and with its highlight (caustic_lit), as direct_light gives the
            // straight light above.
#ifdef PT_SPEED_MEASURE // the measuring build alone: the landed light (the lamps' through the water with it) is not asked for
            if (medium >= 0 && !foam) ++g_scLanded;
            if (speed_measure2(SPEED_M2_NO_LANDED) || speed_measure3(SPEED_M3_NO_LANDED)) { }
            else
#endif
            if (medium >= 0 && !foam && water_light_without_view() && !water_passes_light())
                radiance += weight * water_vertex_lights(uint(medium), hit.position, true, hit.normal, m.normal,
                    m.albedo, m.f0, roughness_alpha(m.roughness), -direction, m.through, m.tint, rng);
            else if (medium >= 0 && !foam && (g.settings2.y & OPTION_CAUSTICS_LIT) != 0)
                radiance += weight * caustic_lit(hit.position, hit.normal, m.normal, m.albedo, m.f0, roughness_alpha(m.roughness), -direction, m.through, float3(1.0, 1.0, 1.0));
            else
            if (medium >= 0 && !foam) radiance += weight * m.albedo * caustic_light(hit.position, hit.normal);
            asked = medium >= 0 && !foam; crossings = 0; crossed = false; glinted = false;
            lampsAsked = asked && water_light_without_view() && !water_passes_light();
            // Mixture of diffuse and GGX visible normals, with the actual branch probability divided out.
            const float diffuse = max(m.albedo.r, max(m.albedo.g, m.albedo.b));
            const float specular = max(m.f0.r, max(m.f0.g, m.f0.b));
            const float chance = specular / max(diffuse + specular, 1e-20);
            const float3 view = -direction;
            if (rng.next() < chance) {
                const float alpha = reflection_alpha(m.roughness);
                direction = ggx_sample(m.normal, view, alpha, rng.next2());
                if (dot(hit.normal, direction) <= 0.0) break;
                const float3 halfVector = normalize(view + direction);
                weight *= fresnel_schlick(m.f0, saturate(dot(view, halfVector))) *
                          smith_g1(saturate(dot(m.normal, direction)), alpha * alpha) / chance;
                raySpread += alpha; // (the ray's width from here on: a highlight's lobe)
            } else {
                direction = cosine_sample(m.normal, rng.next2());
                if (dot(hit.normal, direction) <= 0.0) break;
                weight *= m.albedo / max(1.0 - chance, 1e-20);
                raySpread = max(raySpread, DIFFUSE_SPREAD); // (... a bounce's)
            }
            delta = false;
        }
        primary = false;
        origin = hit.position + direction * water_epsilon(hit.position);
        ++vertices;
        if (vertices >= 3) {
            const float survive = min(0.95, max(weight.r, max(weight.g, weight.b)));
            if (!(survive > 0.0) || rng.next() >= survive) break;
            weight /= survive;
        }
        if (!all(isfinite(weight)) || !any(weight > 0.0)) break;
    }
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): how long the path was
    if (speedTurns >= 8u) ++g_scWaterLong8;
    if (speedTurns >= 32u) ++g_scWaterLong32;
    if (speedTurns >= 128u) ++g_scWaterLong128;
#endif
    return radiance;
}
#endif
