// SPDX-License-Identifier: MIT
// PT-127, primary surface replacement (pt_psr_record.hlsli): the reflections' pass fills a mirror pixel's record. Included
// by pt_specular alone. Nothing here changes a traced sample: the light is the pixel's own reflection ray's, taken apart
// (pt_common shade_seen writes the end's diffuse and specular light into the record as it adds them up), and the two
// distance rays draw their random numbers from a sequence of their own.
// What the pass's thread carries for it across its rays (the pass is regrouped by Shader Execution Reordering, and every
// value alive across a ray travels with the thread): the record's slot, one word (g_psrSlot), and the fog's transmittance
// over the reflection ray, one float. Everything else lies in the record, in memory, between the steps.

// The camera's surface is a pure mirror that stands still: no roughness and no diffuse colour as the surface record holds
// them (a delta event: NRD README's rule; a merely shiny metal is not one), its shading normal the triangle's (a bumped
// mirror's curvature is not followed), and the camera pass marked it as standing still this frame (psr_primary).
bool psr_mirror(Surface s, uint2 p) {
    if (!psr_on() || !s.valid || !s.deferred || s.glass || s.water || s.underwater ||
        s.translucent || s.skin || s.eye || s.strands || s.hair || s.roughness != 0.0 || any(s.albedo != 0.0) ||
        dot(s.normal, s.geoNormal) < 1.0 - 1e-5) return false;
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    return psr_state(records[psr_index(p)].surface) == PSR_CANDIDATE;
}

// Where the point a ray met was in the frame before (a moving thing's, a posed character's, a swaying plant's).
float3 psr_previous_point(QueryHit h, float3 position) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const InstanceRecord inst = instances[h.instance];
    const MeshRecord mesh = meshes[inst.mesh];
    const uint3 tri = triangle_indices(mesh, h.geometry, h.primitive);
    const float3 bary = float3(1.0 - h.bary.x - h.bary.y, h.bary);
    float3 delta = 0.0;
    if (mesh.previous != INVALID) {
        const bool packed = (mesh.previous & 0x40000000u) != 0u;
        ByteAddressBuffer previous = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.previous & ~0x40000000u)];
        ByteAddressBuffer current = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
        [unroll] for (uint i = 0; i < 3; ++i) {
            float3 d;
            if (packed) {
                const uint2 bits = previous.Load2(tri[i] * 8u);
                d = float3(f16tof32(bits.x), f16tof32(bits.x >> 16u), f16tof32(bits.y));
            } else d = asfloat(previous.Load3(tri[i] * 12u)) - asfloat(current.Load3(tri[i] * 12u));
            delta += d * bary[i];
        }
        delta = mul((float3x3)h.toWorld, delta);
    } else if (inst.previous != INVALID && g.primary.w != INVALID) {
        ByteAddressBuffer vertices = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
        float3 local = 0.0;
        [unroll] for (uint i = 0; i < 3; ++i) local += asfloat(vertices.Load3(tri[i] * 12u)) * bary[i];
        StructuredBuffer<float4> rows = ResourceDescriptorHeap[g.primary.w];
        const float3x4 before = float3x4(rows[inst.previous * 3u], rows[inst.previous * 3u + 1u], rows[inst.previous * 3u + 2u]);
        delta = mul(before, float4(local, 1.0)) - mul(h.toWorld, float4(local, 1.0));
    }
    return position + delta;
}

// Before the reflection's end is shaded: the depth and the motion NRD is told, both known from the two hits alone. The
// end as the camera sees it in the mirror lies on the pixel's own view ray, the reflection ray's length behind the mirror
// (NRD README's rule for the virtual position: on the primary hit's view vector, further along it); where it was a frame ago: its own displacement since then, mirrored in the mirror's plane (the mirror
// stands still). False where either does not project: the pixel then stays the mirror's own.
bool psr_begin(uint2 p, Surface mirror, Hit endpoint, QueryHit query) {
    const float3 viewRay = normalize(mirror.position - g.cameraPos.xyz);
    const float3 virtualPoint = mirror.position + viewRay * length(endpoint.position - mirror.position);
    const float3 previous = psr_previous_point(query, endpoint.position);
    const float3 virtualPrevious = virtualPoint + reflect(previous - endpoint.position, mirror.normal);
    const float4 now = mul(g.motionViewProj, float4(virtualPoint - g.cameraPos.xyz, 1.0));
    const float4 was = mul(g.prevViewProj, float4(virtualPrevious - g.cameraPos.xyz, 1.0));
    if (!(now.w > 1e-6 && was.w > 1e-6)) return false;
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    records[psr_index(p)].motionDepth = float4((now.xy / now.w - was.xy / was.w) * float2(0.5, -0.5), was.w - now.w, now.w);
    return true;
}

// A hit distance from the reflection's end along one direction of a lobe of its own (NRD README's rules: the
// distance starts at the replaced surface, the first bounce after it is handed unchanged, and where the light came from
// a chosen sample, as ReSTIR's or a cache's, a distance within the lobe stands in).
float psr_lobe_distance(Hit endpoint, float3 direction) {
    if (!(dot(endpoint.normal, direction) > 0.0)) return HIT_DISTANCE_MISS;
    Hit next;
    const float3 origin = endpoint.position + endpoint.normal * optical_epsilon(endpoint.position);
    return trace_closest(origin, direction, 0.0, g.params.y, next) ? min(length(next.position - endpoint.position), HIT_DISTANCE_MISS) : HIT_DISTANCE_MISS;
}

// After the end is shaded and the fog over the reflection ray is known (throughput: what the mirror reflects of the end's
// light toward the camera, per channel): the record completed, or left as it is where the end was no ordinary surface
// (shade_seen wrote nothing: hair, skin, glass, water, a mirror in the mirror) or the mirror passes none of its light.
void psr_finish(uint2 p, Surface mirror, Hit endpoint, float3 incoming, float3 throughput) {
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    const uint slot = psr_index(p);
    PsrRecord r = records[slot];
    const uint state = psr_state(r.surface);
    if (state != PSR_CAPTURED && state != PSR_REFLECTED) return;
    const float3 normal = psr_normal(r); // the end's shading normal, as shade_seen lit it
    if (!all(throughput > 0.0) || dot(normal, endpoint.normal) <= -0.9999 || dot(normal, -incoming) <= 0.0) return;
    const float roughness = psr_roughness(r);
    // The two distances, each along a direction of its lobe above the surface (drawn again where one points into it: a ray
    // into the surface is no sky).
    Rng guideRng = make_rng(p, g.size.z, 73u);
    const float coneWidth = g_coneWidth, coneSpread = g_coneSpread;
    float4 diffuse = psr_diffuse(r), specular = psr_specular(r);
    ray_cone(endpoint.footprint, DIFFUSE_SPREAD);
    // PT-129 (docs/agents/nosun.md): PSR_TRIES draws at most, each. Until 2026-10-12 both loops drew until a direction came
    // out above the surface, which a smooth, normal-mapped surface met at a low angle may never give: its lobe (an alpha of
    // 1e-4 at the smoothest) lies about the mirrored direction, and where the shading normal sends that under the triangle
    // every draw is refused (millions of draws for one pixel: the GPU would hang in this pass). After the last draw the
    // direction is left as it is: psr_lobe_distance hands the sky's distance for one that points into the surface. A pixel
    // that finds its direction within PSR_TRIES draws (nearly every one) draws the same numbers as before.
    static const uint PSR_TRIES = 64u;
    float3 diffuseDirection = normal;
    [loop] for (uint diffuseTry = 0u; diffuseTry < PSR_TRIES; ++diffuseTry) {
        diffuseDirection = cosine_sample(normal, guideRng.next2());
        if (!(dot(diffuseDirection, endpoint.normal) <= 0.0)) break;
    }
    diffuse.w = psr_lobe_distance(endpoint, diffuseDirection);
    // (A glossy metal's own reflection ray gave its distance already: shade_seen, PSR_REFLECTED.)
    if (state == PSR_CAPTURED) {
        ray_cone(endpoint.footprint, reflection_alpha(roughness));
        float3 specularDirection = normal;
        [loop] for (uint specularTry = 0u; specularTry < PSR_TRIES; ++specularTry) {
            specularDirection = ggx_sample(normal, -incoming, reflection_alpha(roughness), guideRng.next2());
            if (!(dot(specularDirection, endpoint.normal) <= 0.0 || dot(specularDirection, normal) <= 0.0)) break;
        }
        specular.w = psr_lobe_distance(endpoint, specularDirection);
    }
    ray_cone(coneWidth, coneSpread);
    diffuse.rgb *= throughput; specular.rgb *= throughput;
    records[slot].light = psr_light(diffuse, specular);
    // In the mirror's world: the normal mirrored (README: the normal at the replaced surface, mirrored), and the
    // direction the end is seen from (README: the ray direction is handed in the virtual world too).
    const float3 n = mirror.normal;
    records[slot].surface = uint4(pack_normal(reflect(normal, n)),
        pack_normal16(reflect(-incoming, n)) | (f32tof16(throughput.b) << 16u),
        (r.surface.z & 0x00FFFFFFu) | (PSR_VALID << 24u), g.psr.w);
    records[slot].material = uint4(r.material.xyz, psr_halves(throughput.r, throughput.g));
}
