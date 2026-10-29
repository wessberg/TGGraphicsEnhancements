// SPDX-License-Identifier: MIT
// A radiance ray's glass interfaces. Included by pt_common after light sampling.
// There is no interface count: roulette changes variance, never the expected light.
float optical_ior(MaterialRecord m) { return m.ior > 0.0 ? m.ior : GLASS_IOR; }
float optical_epsilon(float3 p) { return max(2e-6, 2e-7 * max(abs(p.x), max(abs(p.y), abs(p.z)))); }

// The enclosing solid at a point, without a bounded medium stack. A candidate exit
// encloses the point only when it is that instance's first boundary along the ray.
// Ignore opaque objects (an iris can lie inside a cornea) and disconnected solids
// entered later along the ray. For nested solids, the nearest exit is the innermost.
// Only glass whose mesh closes a volume encloses anything (glass_solid): the back of an open sheet is no solid's exit.
uint optical_medium(float3 origin, float3 dir) {
    dir = normalize(float3(0.984, 0.131, 0.118));
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> indices = ResourceDescriptorHeap[g.scene1.x];
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = 0.0; ray.TMax = 1e30;
    // Opaque geometry is left out (PT-73 c): glass is alpha-tested, so every instance holding some is non-opaque (scene.cpp;
    // glass_walk finds glass the same way), and this ray, which ends nowhere, asked the shader about every opaque triangle
    // on its way through the whole scene, at every interface, to pass each by. The water's wave triangles are left out too
    // (MASK_SOLID): water is no glass solid's boundary.
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_SOLID, ray);
    uint medium = INVALID; float nearest = 1e30;
    while (q.Proceed()) {
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): the triangles this ray is handed
        ++g_scGlassCandidates;
#endif
        if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE || q.CandidateTriangleFrontFace()) continue;
        const uint inst = q.CandidateInstanceID();
        const uint material = indices[instances[inst].materialBase + q.CandidateGeometryIndex()];
        if ((materials[material].flags & MATERIAL_GLASS) == 0 || q.CandidateTriangleRayT() >= nearest) continue;
        if (!glass_solid(inst, q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex())) continue;
        RayDesc test = ray; test.TMax = q.CandidateTriangleRayT();
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) before;
        before.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_SOLID, test);
        bool earlier = false;
        while (before.Proceed()) {
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
            ++g_scGlassCandidates;
#endif
            if (before.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE || before.CandidateInstanceID() != inst) continue;
            const uint other = indices[instances[inst].materialBase + before.CandidateGeometryIndex()];
            if ((materials[other].flags & MATERIAL_GLASS) != 0 && before.CandidateTriangleRayT() < test.TMax - optical_epsilon(origin)) {
                earlier = true; before.Abort();
            }
        }
        if (!earlier) { nearest = q.CandidateTriangleRayT(); medium = material; }
    }
    return medium;
}

// The hit a walk starts at is glass by a draw already made for it (pt_specular: the pixel's own camera ray drew between
// the glass and its paint in pt_primary write_traced, and this pixel came out glass): its first interface is not drawn again.
static bool g_glassDrawn = false;
// And whether that first triangle has an inside (glass_solid: 1 yes, 0 no), where the caller knows it and the hit it hands
// over does not carry its submesh and triangle (pt_specular: a pixel's surface record); -1: asked of the hit itself.
static int g_glassFirst = -1;
// The ray a walk continues gathers light for a vertex that samples the lamps and the sun itself (a path's or the cache's
// vertex, a pixel's own bounce ray): their light THROUGH glass is already that vertex's own, by its shadow ray (tinted along
// the straight line, or bent and focused: caustic_through). The walk then counts a lamp's ball or the sun's disc only where
// it reflected off glass on its way, which no shadow ray finds. Counted by both ways (until 2026-10-09), the floor under a
// glass box got the sun twice: once tinted, once not (a grey box's shadow 0.17 of a solid one's where 0.64 was due, the
// light under a sheet of green glass no greener: PT-76). The camera's own view through glass, and a mirror's, count what
// they meet whole, as before: nothing samples for them. Set by the caller for its next walk; the walk clears it.
static bool g_walkGathers = false;
// Returns an opaque endpoint, or false on escape/roulette. On escape dir is the
// outgoing direction; roulette sets weight to zero. first is the primary hit's
// decoded material, or hit_material(hit) for any other ray.
// An opaque endpoint may be glass's own paint (glass_cover: a label, a lid, a stain, as much of the texel as the game's alpha
// covers): the ray met it with that chance, from either side, and the caller shades it as the lit surface it is.
bool dielectric_follow(inout Hit hit, inout float3 dir, SurfaceMaterial first,
                       inout float3 weight, inout Rng rng, out float travelled, inout float3 light) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    travelled = 0.0;
    SurfaceMaterial surface = first;
    bool drawn = g_glassDrawn;
    g_glassDrawn = false;
    int firstSolid = g_glassFirst;
    g_glassFirst = -1;
    const bool gathers = g_walkGathers;
    g_walkGathers = false;
    bool mirrored = false; // reflected off glass on its way: no vertex's shadow ray finds that light
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush): the walk counted
    ++g_scGlassWalks;
    uint speedInterfaces = 0u;
#endif
    while ((materials[hit.material].flags & MATERIAL_GLASS) != 0) {
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_measure3): the walk ends at its first interface; its interfaces counted
        if (speedInterfaces >= 1u && speed_measure3(SPEED_M3_GLASS_ONE)) { weight = 0.0; return false; }
        ++speedInterfaces; ++g_scGlassInterfaces;
#endif
        const MaterialRecord m = materials[hit.material];
        // The draw is the ray's own for this triangle (ray_chance: the ray that found the hit seeded it), so a caller that
        // walks on from a hit this walk ended at as paint (pt_specular: dielectric_follow, then shade_seen) finds paint again.
        if (!drawn) {
            const float cover = glass_cover(hit);
            if (cover > 0.0 && ray_chance(hit.instance, hit.geometry, hit.primitive, GLASS_PAINT_SALT) < cover) return true;
        }
        drawn = false;
        const float epsilon = optical_epsilon(hit.position);
        const float3 outside = hit.position + (hit.front ? hit.normal : -hit.normal) * epsilon;
        const uint outer = optical_medium(outside, hit.front ? -dir : dir);
        const float outsideIor = outer == INVALID ? 1.0 : optical_ior(materials[outer]);
        // Glass with an inside (glass_solid: its mesh closes a volume) is met from outside at a front face and from inside
        // at a back face. An open sheet has no inside: either face of it is a thin pane in what surrounds it.
        const bool solid = firstSolid >= 0 ? firstSolid == 1 : glass_solid(hit.instance, hit.geometry, hit.primitive);
        firstSolid = -1;
        const float ni = !solid || hit.front ? outsideIor : optical_ior(m);
        const float nt = !solid || hit.front ? optical_ior(m) : outsideIor;
        const float3 v = -dir;
        const float3 n = facing_normal(surface.normal, v);
        const float alpha = reflection_alpha(surface.roughness);
        const float3 reflected = ggx_sample(n, v, alpha, rng.next2());
        const float3 halfVector = normalize(v + reflected);
        const bool matched = ni == nt;
        // The boundary at the microfacet the reflected direction was drawn from (pt_interface.hlsli: water's too).
        Interface boundary = dielectric_interface(dir, halfVector, ni, nt);
        // ON TRIAL (pt_common glass_clear; off: nothing below differs): what goes through crosses the mesh's own smooth face
        // (its vertices' normal, no normal map, no microfacet), and that face's Fresnel term shares the ray out; what is
        // reflected keeps the material's lobe about its normal-mapped normal, as above.
        const bool clear = glass_clear(m);
        if (clear) {
            // A shading normal is not a boundary between media. On the jar it tilts by a different amount on each
            // side of a nearly parallel wall (inner 3.7 degrees upward, outer 0.1 in the measured body ray).
            // Use the actual plane on trial; no guessed thickness, paired face, skipped boundary or screen lookup.
            const float3 throughNormal = solid && (glass_trial() & GLASS_TRIAL_BOUNDARY) != 0u ? hit.normal : facing_normal(hit.rounded, v);
            const Interface face = dielectric_interface(dir, throughNormal, ni, nt);
            boundary.transmitted = face.transmitted; boundary.reflectance = face.reflectance; boundary.eta = face.eta;
        }
        float F = boundary.reflectance;
        // A pane's two faces, with the light going to and fro between them, reflect 2F / (1 + F) of it together; the rest
        // goes straight on: nothing is bent, and the light is as it was in what surrounds the pane (no index's square).
        if (!solid) F = 2.0 * F / (1.0 + F);
        const bool reflection = rng.next() < F;
        const float3 next = reflection ? reflected : solid ? boundary.transmitted : dir;
        // A VNDF sample on the wrong geometric side has zero BSDF, not a resampled
        // direction (which would silently change its density at silhouettes).
        if (dot(next, next) < 0.5 || (reflection ? dot(next, hit.normal) <= 0.0 : dot(next, hit.normal) >= 0.0)) {
            weight = 0.0; return false;
        }
        // Equal media have no optical boundary, including no microfacet masking.
        // (ON TRIAL, clear: nor does the smooth face a ray goes through mask anything.)
        weight *= matched || (!solid && !reflection) || (clear && !reflection) ? 1.0 : smith_g1(abs(dot(n, next)), alpha * alpha);
        if (!reflection && solid) weight *= interface_radiance(boundary);
        // An open sheet's own tint (PT-76): its material's absorption over the way through its thickness.
        if (!reflection && !solid) weight *= pane_transmittance(m, abs(dot(dir, hit.normal)));
        if (reflection) mirrored = true;
        const uint medium = !solid ? outer : reflection ? (hit.front ? outer : hit.material) : (hit.front ? hit.material : outer);
        const float3 absorption = medium == INVALID ? 0.0 : max(materials[medium].absorption, 0.0);
        // Zero extinction is clear glass. Diffuse paint colour is not a substitute
        // for absorption; each real segment below uses the optical coefficients.
        // 0.95 is a survival probability, not a layer limit. Its reciprocal keeps
        // arbitrarily long paths unbiased, including total internal reflection.
        const float survive = min(0.95, max(weight.r, max(weight.g, weight.b)));
        if (!(survive > 0.0) || rng.next() >= survive) { weight = 0.0; return false; }
        weight /= survive;
        const float3 from = hit.position;
        const float3 origin = from + hit.normal * (reflection ? epsilon : -epsilon);
        dir = normalize(next);
        ray_cone(hit.footprint, max(g_coneSpread, 0.0) + (matched || (!solid && !reflection) || (clear && !reflection) ? 0.0 : alpha));
        const bool found = trace_closest(origin, dir, 0.0, 1e30, hit);
        const float distance = found ? length(hit.position - from) : HIT_DISTANCE_MISS;
        travelled += distance;
        // Absorption is per metre of the actual ray segment, never per object.
        weight *= segment_transmittance(absorption, distance);
        if (!gathers || mirrored) light += weight * reflected_lamps(from, origin, dir, found ? distance : 1e30);
        if (!found) {
            if ((!gathers || mirrored) && g.sunColor.w > 0.0 && dot(dir, g.sunDir.xyz) >= g.sunDir.w)
                light += weight * g.sunColor.rgb * sun_fog(from) / (2.0 * PI * max(1.0 - g.sunDir.w, 1e-7));
            return false;
        }
#ifdef PT_SPEED_MEASURE // the measuring build alone (pt_common speed_count_flush)
        if (hit.water != 0) ++g_scGlassToWater;
#endif
        surface = hit_material(hit);
    }
    return true;
}
