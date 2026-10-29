// SPDX-License-Identifier: MIT
// PT-127: correlated direct-light replay, isolated from the rendering passes. This is our
// implementation of NRD README's history-confidence method, not NVIDIA sample source.
// a: saved samples UAV, gradient UAV, previous position UAV, previous normal UAV.
// b.x: history valid. One thread per 5x5 render-pixel stratum. Records are 176 bytes.
#include "pt_rtxdi.hlsli"
#include "Rtxdi/DI/ReservoirStorage.hlsli"

struct ConfidenceSample {
    float4 positionDistance, normalRoughness, geometryThrough, albedoFlags, f0Ambient;
    float4 viewSpecular, strandMaterial, lobesUv;
    uint4 light; // index, identity, inverse PDF bits, RNG frame
    float4 radiance;
    float4 leaf; // the material's own transmission (xyz), available (w)
};

ConfidenceSample save_surface(RAB_Surface r) {
    Surface s = r.s;
    ConfidenceSample v = (ConfidenceSample)0;
    uint flags = uint(s.valid) | (uint(s.deferred) << 1) | (uint(s.translucent) << 2) |
        (uint(s.complex) << 3) | (uint(s.forward) << 4) | (uint(s.glass) << 5) |
        (uint(s.eye) << 6) | (uint(s.skin) << 7) | (uint(s.onMesh) << 8) |
        (uint(s.hair) << 9) | (uint(s.strands) << 10) | (uint(s.water) << 11) | (uint(s.underwater) << 12);
    v.positionDistance = float4(s.position, s.distance);
    v.normalRoughness = float4(s.normal, s.roughness);
    v.geometryThrough = float4(s.geoNormal, s.through);
    v.albedoFlags = float4(s.albedo, asfloat(flags));
    v.f0Ambient = float4(s.f0, s.ambientOcclusion);
    v.viewSpecular = float4(r.view, s.specularOcclusion);
    v.strandMaterial = float4(s.strand, asfloat(s.hairMaterial));
    v.lobesUv.xy = s.hairLobe;
    v.light.x = INVALID;
    return v;
}

RAB_Surface restore_surface(ConfidenceSample v) {
    RAB_Surface r = RAB_EmptySurface();
    r.s.position = v.positionDistance.xyz + g.prevOriginDelta.xyz;
    r.s.distance = v.positionDistance.w;
    r.s.normal = v.normalRoughness.xyz; r.s.roughness = v.normalRoughness.w;
    r.s.geoNormal = v.geometryThrough.xyz; r.s.through = v.geometryThrough.w;
    r.s.albedo = v.albedoFlags.xyz; r.s.f0 = v.f0Ambient.xyz;
    r.s.ambientOcclusion = v.f0Ambient.w; r.s.specularOcclusion = v.viewSpecular.w;
    r.view = v.viewSpecular.xyz; r.s.strand = v.strandMaterial.xyz;
    r.s.hairMaterial = asuint(v.strandMaterial.w); r.s.hairLobe = v.lobesUv.xy;
    const uint f = asuint(v.albedoFlags.w);
    r.s.valid = (f & 1u) != 0; r.s.deferred = (f & 2u) != 0; r.s.translucent = (f & 4u) != 0;
    r.s.complex = (f & 8u) != 0; r.s.forward = (f & 16u) != 0; r.s.glass = (f & 32u) != 0;
    r.s.eye = (f & 64u) != 0; r.s.skin = (f & 128u) != 0; r.s.onMesh = (f & 256u) != 0;
    r.s.hair = (f & 512u) != 0; r.s.strands = (f & 1024u) != 0;
    r.s.water = (f & 2048u) != 0; r.s.underwater = (f & 4096u) != 0;
    return r;
}

bool confidence_surface(RAB_Surface r) { return RAB_IsSurfaceValid(r) && !r.s.glass; }

float3 replay_light(RAB_Surface r, ConfidenceSample v, uint2 pixel) {
    if (!confidence_surface(r)) return 0.0;
    // ray_seed hashes g.size.z XOR g_raySalt, including alpha/glass random decisions.
    // Replaying only the disc UV with the new frame's visibility seed would measure noise.
    g_raySalt = g.size.z ^ v.light.w;
    Rng rng = make_rng(pixel, v.light.w, 47u);
    const float2 shadowUv = rng.next2(), sunUv = rng.next2();
    float3 radiance = 0.0;
    const uint index = v.light.x;
    const bool inRange = index != INVALID && (index >= EMITTER_BASE ? index - EMITTER_BASE < g.emissive.z : index < g.settings2.z);
    if (inRange && asfloat(v.light.z) > 0.0) {
        RAB_LightInfo light = RAB_LoadLightInfo(index, false);
        // A removed or replaced slot means the old light no longer contributes.
        if (light.l.id == v.light.y && light.l.id != 0u) {
            RAB_LightSample ls = RAB_SamplePolymorphicLight(light, r, v.lobesUv.zw);
            RAB_LightSample shadow = ls;
            if (!ls.emitter) shadow = RAB_SamplePolymorphicLight(light, r, shadowUv);
            float3 diffuse, specular;
            rtxdi_shade(ls, r, diffuse, specular);
            if (v.leaf.w > 0.0) diffuse = ls.irradiance * leaf_cos(r.s.normal, ls.dir, v.leaf.xyz) / PI;
            const float3 visibility = rtxdi_transmittance(r, shadow);
            radiance = (r.s.albedo * diffuse + specular) * asfloat(v.light.z) * visibility;
        }
    }
    if (g.sunColor.w > 0.0) {
        const float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, sunUv);
        const float nl = surface_cos(r.s, r.view, l);
        if (nl > 0.0) {
            const float3 origin = shadow_origin(r.s, l, surface_offset(r.s));
            float3 visibility = transmittance_from_surface(origin, l, 0.0, g.params.y, surface_near_back(r.s));
            if (g_glassMet >= 2 && any(visibility > 0.0))
                visibility = caustic_through(origin, r.s.normal, r.s.geoNormal, l, 0.0, true, visibility);
            float3 response = r.s.albedo * nl * terminator_softening(r.s, l) / PI;
            if (v.leaf.w > 0.0) response = r.s.albedo * leaf_cos(r.s.normal, l, v.leaf.xyz) / PI;
            if (dot(r.s.normal, l) > 0.0)
                response += r.s.hair ? hair_specular(r.s, r.view, l) : ggx_specular(r.s.normal, r.view, l, r.s.f0, roughness_alpha(r.s.roughness));
            radiance += g.sunColor.rgb * visibility * sun_fog(r.s.position) * response;
        }
    }
    g_raySalt = 0u;
    return finite_radiance(radiance);
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 cell : SV_DispatchThreadID) {
    const uint2 grid = (g.size.xy + 4u) / 5u;
    if (any(cell >= grid)) return;
    const uint at = cell.y * grid.x + cell.x;
    const uint2 pixel = min(cell * 5u + 2u, g.size.xy - 1u);
    RWStructuredBuffer<ConfidenceSample> samples = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float2> gradient = ResourceDescriptorHeap[pass.a.y];
    RWTexture2D<float4> position = ResourceDescriptorHeap[pass.a.z];
    RWTexture2D<uint2> normal = ResourceDescriptorHeap[pass.a.w];
    float2 change = 0.0;
    float4 oldPosition = 0.0;
    uint2 oldNormal = 0u;
    if (pass.b.x != 0u) {
        ConfidenceSample old = samples[at];
        RAB_Surface surface = restore_surface(old);
        if (confidence_surface(surface)) {
            const float3 now = replay_light(surface, old, pixel);
            change = float2(luminance(abs(now - old.radiance.xyz)), max(luminance(now), luminance(old.radiance.xyz)));
            oldPosition = float4(surface.s.position, surface.s.distance);
            oldNormal = uint2(pack_normal(surface.s.normal), pack_normal(surface.s.geoNormal));
            // Coordinates enter the alpha RNG hash. On an origin relocation they change even
            // for a static world; don't interpret its different alpha sample as changed light.
            if (any(g.prevOriginDelta.xyz != 0.0)) change.x = 0.0;
        }
    }
    gradient[cell] = change; position[cell] = oldPosition; normal[cell] = oldNormal;

    RAB_Surface current = RAB_GetGBufferSurface(int2(pixel), false);
    ConfidenceSample next = save_surface(current);
    float3 leafThrough;
    if (confidence_surface(current) && leaf_transmittance(pixel, current.s, leafThrough)) next.leaf = float4(leafThrough, 1.0);
    next.light.w = g.size.z;
    if (confidence_surface(current) && frame_has_lights()) {
        RTXDI_DIReservoir reservoir = RTXDI_LoadDIReservoir(g.rtxdiReservoirs, pixel, g.rtxdi.x);
        const float weight = RTXDI_GetDIReservoirInvPdf(reservoir);
        if (RTXDI_IsValidDIReservoir(reservoir) && weight > 0.0 && weight < 1e20) {
            const uint index = RTXDI_GetDIReservoirLightIndex(reservoir);
            RAB_LightInfo light = RAB_LoadLightInfo(index, false);
            next.light.xyz = uint3(index, light.l.id, asuint(weight));
            next.lobesUv.zw = RTXDI_GetDIReservoirSampleUV(reservoir);
        }
    }
    next.radiance = float4(replay_light(current, next, pixel), 0.0);
    samples[at] = next;
}
