// SPDX-License-Identifier: MIT
// PT-81/94: direct light at a water vertex. Directions are sampled, then traced through the actual wave geometry.
// A refracted aim is ONLY an importance proposal: a full-sphere component gives every direction positive probability.
// A missed root, another focusing branch, a tiny lamp or a point beyond any grid costs variance, never light.
// The transported radiance is evaluated on the traced connection: Fresnel, radiance's n^2, extinction, foam, visibility.
#ifndef PT_WATER_LIGHTS_HLSLI
#define PT_WATER_LIGHTS_HLSLI

float water_ball_distance(float3 p, float3 d, LightRecord lamp) {
    const float3 oc = p - lamp.position;
    const float b = dot(oc, d), c = dot(oc, oc) - lamp.radius * lamp.radius;
    const float det = b * b - c;
    if (!(det > 0.0)) return -1.0;
    const float near = -b - sqrt(det), far = -b + sqrt(det);
    return near > 0.0 ? near : far;
}
// The existing light object's finite emitter. PT-90: this is its flux-normalized proxy, NOT yet the flame material's
// visible radiance. Both routes use the same source here; changing the emitter contract is a separate job.
float3 water_ball_radiance(LightRecord lamp, float3 from) {
    float3 d; float distance;
    const float3 incident = light_incident(lamp, from, d, distance);
    return incident / max(light_falloff(lamp, distance * distance) * PI * lamp.radius * lamp.radius, 1e-30);
}

// Smooth-surface shooting used to aim the proposal. Iteration count changes the proposal alone.
float2 water_aim_error(float3 p, float3 d, float3 target, bool sun, float3 tx, float3 ty) {
    Hit h;
    if (!trace_water(p, d, water_epsilon(p), 1e30, h) || h.front) return 0.0;
    const float3 air = sun ? target : normalize(target - h.position);
    const float3 wanted = -refract(-air, -h.rounded, 1.0 / WATER_IOR);
    return float2(dot(d - wanted, tx), dot(d - wanted, ty));
}
float3 water_light_aim(uint medium, float3 p, float3 target, bool sun, bool submerged) {
    if (submerged || water_passes_light()) return sun ? target : normalize(target - p);
    float3 axis = sun ? -refract(-target, float3(0, 1, 0), 1.0 / WATER_IOR) : normalize(target - p);
    // Snell's flat-water solution starts the search; it is never used to evaluate light or visibility.
    Hit top;
    if (!sun && trace_water(p, float3(0, 1, 0), 0.0, 1e30, top)) {
        const float depth = max(top.position.y - p.y, 0.0), height = max(target.y - top.position.y, 0.0);
        const float2 lateral = target.xz - p.xz;
        const float span = length(lateral);
        float lo = 0.0, hi = 1.0 / WATER_IOR;
        [loop] for (uint i = 0u; i < 20u; ++i) {
            const float s = (lo + hi) * 0.5, sa = WATER_IOR * s;
            const float reach = depth * s / sqrt(max(1.0 - s*s, 1e-12)) + height * sa / sqrt(max(1.0 - sa*sa, 1e-12));
            if (reach < span) lo = s; else hi = s;
        }
        const float s = (lo + hi) * 0.5;
        axis = float3(lateral.x * s / max(span, 1e-12), sqrt(1.0 - s*s), lateral.y * s / max(span, 1e-12));
    }
    axis = normalize(axis);
    float3 tx, ty; basis(axis, tx, ty);
    float2 at = 0.0;
    [loop] for (uint step = 0u; step < 5u; ++step) {
        const float3 d = normalize(axis + tx * at.x + ty * at.y);
        const float2 e = water_aim_error(p, d, target, sun, tx, ty);
        const float h = 0.002;
        const float2 ex = (water_aim_error(p, normalize(axis + tx * (at.x+h) + ty * at.y), target, sun, tx, ty) - e) / h;
        const float2 ey = (water_aim_error(p, normalize(axis + tx * at.x + ty * (at.y+h)), target, sun, tx, ty) - e) / h;
        const float det = ex.x * ey.y - ex.y * ey.x;
        if (abs(det) < 1e-6) break;
        const float2 change = float2(ey.y*e.x - ey.x*e.y, ex.x*e.y - ex.y*e.x) / det;
        if (!all(isfinite(change))) break;
        at -= change * min(1.0, 0.25 / max(length(change), 1e-20));
    }
    return normalize(axis + tx * at.x + ty * at.y);
}
float3 water_light_direction(float3 axis, float width, inout Rng rng, out float pdf) {
    const float narrow = cos(clamp(width, 0.002, PI));
    const float broad = min(narrow, cos(0.2));
    const float choose = rng.next();
    const float3 d = cone_sample(axis, choose < 0.85 ? narrow : (choose < 0.95 ? broad : -1.0), rng.next2());
    const float c = dot(d, axis);
    pdf = 0.05 / (4.0 * PI);
    if (c >= narrow) pdf += 0.85 / (2.0 * PI * (1.0 - narrow));
    if (c >= broad) pdf += 0.10 / (2.0 * PI * (1.0 - broad));
    return d;
}

float3 water_connected_light(uint medium, float3 p, float3 d, LightRecord lamp, bool sun, bool surface, inout Rng rng) {
    const float epsilon = water_epsilon(p);
    const float3 sigma = max(water_row(medium, 14).xyz, 0.0);
    Hit h;
    const bool met = trace_water(p, d, epsilon, 1e30, h);
    const float boundaryDistance = met ? length(h.position - p) : 1e30;
    const float ballDistance = sun ? -1.0 : water_ball_distance(p, d, lamp);
    // A submerged source is already sampled by direct_light on a solid receiver, but not at a scattering vertex.
    if (!sun && ballDistance > epsilon && (ballDistance < boundaryDistance || water_passes_light())) {
        if (surface) return 0.0;
        const float end = max(ballDistance - epsilon, 0.0);
        if (water_passes_light())
            return water_ball_radiance(lamp, p) * sun_rays(p, d, epsilon, end, p, d, 0.0, 1u, false, int(medium));
        return water_ball_radiance(lamp, p) * segment_transmittance(sigma, ballDistance) *
            sun_rays(p, d, epsilon, end, p, d, 0.0, 1u, true, -1);
    }
    if (!met || h.front || h.water != medium + 1u) return 0.0;
    const float3 view = -d, normal = facing_normal(h.rounded, view);
    const float alpha = reflection_alpha(1.0 - water_smoothness(medium, h.position));
    const float3 facet = normalize(view + ggx_sample(normal, view, alpha, rng.next2()));
    const Interface boundary = dielectric_interface(d, facet, WATER_IOR, 1.0);
    if (!(boundary.reflectance < 1.0)) return 0.0;
    const float3 air = normalize(boundary.transmitted);
    if (!(dot(air, h.normal) < 0.0)) return 0.0;
    float3 source = 0.0;
    float end = 1e30;
    if (sun) {
        if (dot(air, g.sunDir.xyz) < g.sunDir.w) return 0.0;
        source = g.sunColor.rgb * sun_fog(h.position) / (2.0 * PI * max(1.0 - g.sunDir.w, 1e-7));
    } else {
        end = water_ball_distance(h.position, air, lamp);
        if (!(end > epsilon)) return 0.0;
        source = water_ball_radiance(lamp, h.position);
    }
    const float3 airStart = h.position + air * (4.0 * epsilon);
    // Exactly one crossing is sampled here. Extra crossings remain the continuation path's; neither is counted twice.
    Hit nextWater;
    if (trace_water(airStart, air, epsilon, end, nextWater)) return 0.0;
    const float through = (1.0 - boundary.reflectance) * interface_radiance(boundary) *
        smith_g1(abs(dot(normal, air)), alpha * alpha) * (1.0 - water_foam(medium, h.waterBase));
    return source * through * segment_transmittance(sigma, boundaryDistance) *
        sun_rays(p, d, epsilon, max(boundaryDistance - 4.0 * epsilon, 0.0), airStart, air,
                 max(end - 4.0 * epsilon, 0.0), 2u, true, -1);
}
float3 water_light_response(float3 d, bool surface, float3 n, float3 shading, float3 albedo, float3 f0, float alpha, float3 view, float through, float3 tint) {
    if (!surface) return (1.0 / (4.0 * PI)).xxx;
    if (!(dot(n, d) > 0.0) && !(through > 0.0)) return 0.0;
    const float cosine = dot(shading, d);
    const float3 diffuse = albedo * (cosine > 0.0 ? cosine.xxx : -cosine * through * tint) / PI;
    return diffuse + (dot(n, d) > 0.0 ? ggx_specular(shading, view, d, f0, alpha) : 0.0);
}
float3 water_vertex_lights(uint medium, float3 p, bool surface, float3 n, float3 shading, float3 albedo, float3 f0,
                           float alpha, float3 view, float through, float3 tint, inout Rng rng) {
    float3 sum = 0.0;
    // Sun here is for underwater surfaces on the PT-94 trial. The body's existing sun sampler stays unchanged.
    if (surface && g.sunColor.w > 0.0) {
        const float3 axis = water_light_aim(medium, p, g.sunDir.xyz, true, false);
        float pdf;
        const float3 d = water_light_direction(axis, 2.0 * acos(g.sunDir.w) / WATER_IOR, rng, pdf);
        sum += water_connected_light(medium, p, d, (LightRecord)0, true, true, rng) *
               water_light_response(d, true, n, shading, albedo, f0, alpha, view, through, tint) / pdf;
    }
    LightLists lists;
    if (!light_lists(p, lists)) return sum;
    const float choice = rng.next();
    [loop] for (uint i = 0; i < lists.candidates; ++i) {
        float invPdf;
        const uint index = lists_candidate(lists, i, choice, invPdf);
        if (!(invPdf > 0.0) || index >= EMITTER_BASE) continue; // mesh emitters stay on the ordinary path, counted there
        LightRecord lamp = load_light(index);
        if (lamp.type > 1u || !(lamp.radius > 0.0)) continue;
        const bool submerged = water_medium(lamp.position) == int(medium);
        if (surface && submerged) continue; // direct_light already owns this straight connection
        const float3 axis = water_light_aim(medium, p, lamp.position, false, submerged);
        // Proposal width only; never a bound on which connection may contribute.
        const float width = 2.0 * asin(min(1.0, lamp.radius / max(length(lamp.position-p), 1e-10)));
        float pdf;
        const float3 d = water_light_direction(axis, width, rng, pdf);
        sum += water_connected_light(medium, p, d, lamp, false, surface, rng) *
               water_light_response(d, surface, n, shading, albedo, f0, alpha, view, through, tint) * (invPdf / (float(lists.candidates) * pdf));
    }
    return sum;
}
#endif
