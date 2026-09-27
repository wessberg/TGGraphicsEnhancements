// SPDX-License-Identifier: MIT
// The application side of NVIDIA's RTXDI library (github.com/NVIDIA-RTX/RTXDI-Library, fetched beside this repository
// and compiled in, never committed; docs/SOURCES.md): the bridge functions (RAB_*) its ReSTIR DI and GI reuse calls, over this
// tracer's surfaces, lights, BRDF and ray queries. RTXDI does the temporal and spatial reuse of the reservoirs, with
// the permutation sampling, boiling filter and bias correction NVIDIA tuned for denoisers; the tracer the rest: the
// candidates (its light grid knows which lights reach a pixel), the shadow rays and the shading.
#pragma once
#include "pt_common.hlsli"
#include "pt_restir.hlsli"

// RTXDI's resources, reached through the descriptor heap: the DI and GI reservoirs (a buffer each, with this frame's
// and the previous frame's array) and the neighbour offsets its spatial reuse walks.
RWStructuredBuffer<RTXDI_PackedDIReservoir> rtxdi_di_reservoirs() { RWStructuredBuffer<RTXDI_PackedDIReservoir> b = ResourceDescriptorHeap[g.material.w]; return b; }
RWStructuredBuffer<RTXDI_PackedGIReservoir> rtxdi_gi_reservoirs() { RWStructuredBuffer<RTXDI_PackedGIReservoir> b = ResourceDescriptorHeap[g.material.y]; return b; }
StructuredBuffer<float2> rtxdi_neighbor_offsets() { StructuredBuffer<float2> b = ResourceDescriptorHeap[g.extra.z]; return b; }
#define RTXDI_LIGHT_RESERVOIR_BUFFER rtxdi_di_reservoirs()
#define RTXDI_GI_RESERVOIR_BUFFER rtxdi_gi_reservoirs()
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER rtxdi_neighbor_offsets()

// A GI sample whose ray went into the sky is kept as a point this far along it (RTXDI's GI reservoirs hold a position):
// its reconnection Jacobian is 1, and farther than any ray distance marks it as sky.
static const float GI_SKY_DISTANCE = 10000.0;
bool gi_sample_is_sky(float3 samplePosition, float3 from) { return length(samplePosition - from) > GI_SKY_DISTANCE * 0.5; }

// ---------------------------------------------------------------- surfaces
struct RAB_Material { float3 albedo; float3 f0; float roughness; };
// A G-buffer surface, its position relative to this frame's scene origin (the previous frame's moved to it), and the
// direction to the camera that saw it.
struct RAB_Surface { Surface s; float3 view; };
RAB_Surface RAB_EmptySurface() { RAB_Surface r; r.s = (Surface)0; r.view = 0.0; return r; }
RAB_Surface RAB_GetGBufferSurface(int2 p, bool prevFrame) {
    RAB_Surface r = RAB_EmptySurface();
    if (any(p < 0) || any(p >= int2(g.size.xy))) return r;
    if (prevFrame) {
        r.s = load_surface(uint2(p), g.surface.y, g.surface.w, g.extra.y);
        r.s.position += g.prevOriginDelta.xyz;
        r.view = normalize(g.prevCameraPos.xyz - r.s.position);
    } else {
        r.s = load_surface(uint2(p), g.surface.x, g.surface.z, g.material.x);
        r.view = normalize(g.cameraPos.xyz - r.s.position);
    }
    return r;
}
bool RAB_IsSurfaceValid(RAB_Surface r) { return r.s.valid && r.s.deferred; }
float3 RAB_GetSurfaceWorldPos(RAB_Surface r) {
    // For testing (OPTION_OFFSET_RECEIVER): where the surface's rays start, off it along its normal (pt_gi, pt_gi_spatial), and
    // so where its lights are sampled from too (the bounced light is estimated there, the direct light at the surface).
    if ((g.settings2.y & OPTION_OFFSET_RECEIVER) != 0) return r.s.position + r.s.geoNormal * ray_offset(r.s.distance);
    return r.s.position;
}
// The surface's own orientation (from depth), which the tracer's reuse compared too: normal maps made neighbours on one
// wall look unlike each other.
float3 RAB_GetSurfaceNormal(RAB_Surface r) { return r.s.geoNormal; }
// Distance from the camera that saw it (a previous frame's surface from the previous camera), which the temporal
// motion's z matches.
float RAB_GetSurfaceLinearDepth(RAB_Surface r) { return r.s.distance; }
RAB_Material RAB_GetMaterial(RAB_Surface r) { RAB_Material m; m.albedo = r.s.albedo; m.f0 = r.s.f0; m.roughness = r.s.roughness; return m; }
// Close enough in roughness and colour to share a light choice (a rug's and the floor's differ).
bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b) {
    return abs(a.roughness - b.roughness) < 0.5 && abs(luminance(a.albedo) - luminance(b.albedo)) < 0.25 &&
           abs(luminance(a.f0) - luminance(b.f0)) < 0.25;
}
// Spatial neighbours off screen are mirrored back into it.
int2 RAB_ClampSamplePositionIntoView(int2 p, bool prevFrame) {
    int2 size = int2(g.size.xy);
    p = int2(p.x < 0 ? -p.x : p.x, p.y < 0 ? -p.y : p.y);
    p = int2(p.x >= size.x ? 2 * size.x - p.x - 2 : p.x, p.y >= size.y ? 2 * size.y - p.y - 2 : p.y);
    return clamp(p, int2(0, 0), size - 1);
}

// ---------------------------------------------------------------- lights
struct RAB_LightInfo { LightRecord l; };
// A point of a light as seen from a receiver: where it is, and the light arriving from it there (irradiance on a
// surface facing it; the light's range window and spot cone applied). emitter: a point of an emissive triangle.
struct RAB_LightSample { float3 position; float3 irradiance; float3 dir; float dist; bool valid; bool emitter; bool diffuseOnly; };
RAB_LightInfo RAB_EmptyLightInfo() { RAB_LightInfo i; i.l = (LightRecord)0; return i; }
RAB_LightSample RAB_EmptyLightSample() { RAB_LightSample s; s.position = 0.0; s.irradiance = 0.0; s.dir = 0.0; s.dist = 0.0; s.valid = false; s.emitter = false; s.diffuseOnly = false; return s; }
// The plugin keeps every light in a stable slot, with its identity (0: an empty slot): an index means the same light in
// both frames when the slot holds the same identity in both, and RTXDI's temporal reuse weighs a light at the previous
// surface with the previous frame's record (a torch that flickered or a light that just came into range is judged by
// what it was). A slot beyond a frame's count, or whose light changed, has no counterpart. Emissive triangles keep their
// slots the same way (scene.cpp build_emitters), each frame's records in their own buffer.
RAB_LightInfo RAB_LoadLightInfo(uint index, bool prevFrame) {
    RAB_LightInfo i = RAB_EmptyLightInfo();
    if (index >= EMITTER_BASE) { if (index - EMITTER_BASE < (prevFrame ? g.emissive.w : g.emissive.z)) i.l = load_emitter(index - EMITTER_BASE, prevFrame); }
    else if (!prevFrame) i.l = load_light(index);
    else if (index < g.settings2.x) i.l = load_previous_light(index);
    return i;
}
int RAB_TranslateLightIndex(uint index, bool currentToPrevious) {
    if (index >= EMITTER_BASE) {
        const uint slot = index - EMITTER_BASE;
        if (slot >= g.emissive.z || slot >= g.emissive.w) return -1;
        const LightRecord now = load_emitter(slot, false);
        return now.type == LIGHT_TRIANGLE && now.id != 0u && load_emitter(slot, true).id == now.id ? int(index) : -1;
    }
    if (index >= g.settings2.z || index >= g.settings2.x) return -1;
    uint id = load_light(index).id;
    return id != 0u && load_previous_light(index).id == id ? int(index) : -1;
}
// uv picks a point of the light's disc facing the receiver, the size HDRP gives the light (its Radius), which the
// reservoir keeps with the light: the shadow ray goes to that point, so a lamp's shadow softens as its size makes it.
// For an emissive triangle, a point of the triangle (triangle_incident: the light from there, times its area).
RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo info, RAB_Surface surface, float2 uv) {
    RAB_LightSample r = RAB_EmptyLightSample();
    LightRecord l = info.l;
    const float3 at = RAB_GetSurfaceWorldPos(surface);
    if (l.type == LIGHT_TRIANGLE) {
        float3 y;
        r.irradiance = triangle_incident(l, at, uv, r.dir, r.dist, y);
        r.position = y; r.emitter = true;
        r.valid = any(r.irradiance > 0.0);
        return r;
    }
    float3 toCentre; float centreDist;
    r.irradiance = light_incident(l, at, toCentre, centreDist);
    r.diffuseOnly = (l.flags & LIGHT_NO_SPECULAR) != 0;
    float3 t, b; basis(toCentre, t, b);
    float2 disk = disk_sample(uv) * l.radius;
    r.position = l.position + t * disk.x + b * disk.y;
    float3 d = r.position - at;
    r.dist = max(length(d), 1e-4);
    r.dir = d / r.dist;
    r.valid = any(r.irradiance > 0.0);
    return r;
}
// The light a sample shades a surface with: diffuse (divided by the diffuse colour) and specular. An emissive triangle's
// specular light is the reflection pass's (pt_specular), whose rays meet it as it looks and count it whole: taken here as
// well, a glossy floor's reflection of a glowing panel came out twice as bright. A light HDRP keeps from affecting specular
// (TGPT_LIGHT_NO_SPECULAR) makes no highlight: the game's candles lit a metal strap's facets, a few degrees apart, into a saw
// of bright and dark teeth that the game never shows.
void rtxdi_shade(RAB_LightSample ls, RAB_Surface surface, out float3 diffuse, out float3 specular) {
    diffuse = 0.0; specular = 0.0;
    if (!ls.valid) return;
    Surface s = surface.s;
    // Thin skin takes the light HDRP transmits through it too (skin_transmission; from behind, rtxdi_visible's ray goes
    // through the skin).
    const float3 through = ls.irradiance * skin_transmission(s, ls.dir) / PI;
    float nl = translucent_cos(s.normal, ls.dir, s.through); // a grass blade, a leaf or thin cloth is lit through from behind too
    if (nl <= 0.0) { diffuse = through; return; }
    diffuse = ls.irradiance * nl * terminator_softening(s, ls.dir) / PI;
    diffuse += through;
    if (dot(s.normal, ls.dir) > 0.0 && !ls.emitter && !ls.diffuseOnly) specular = ls.irradiance * ggx_specular(s.normal, surface.view, ls.dir, s.f0, roughness_alpha(s.roughness));
}
// ReSTIR's target function: the sample's unshadowed contribution at the surface.
float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample ls, RAB_Surface surface) {
    if (!RAB_IsSurfaceValid(surface)) return 0.0;
    float3 d, sp;
    rtxdi_shade(ls, surface, d, sp);
    return luminance(surface.s.albedo * d + sp);
}

// ---------------------------------------------------------------- visibility
// A shadow ray from the surface to the sample's point, with the tracer's rules for rays leaving HDRP's surfaces
// (shadow_origin, occluded_from_surface's pass-through near the surface); the lamp's own mesh around it doesn't count. To
// an emissive triangle's point it ends just short of the triangle (as triangle_shadow_ray's): 5 cm and the ray's offset
// short, as for a lamp, it passed through what lay against a glowing surface (17 cm of it 50 m from the camera).
float light_ray_end(RAB_LightSample ls, float dist, float offset) {
    return ls.emitter ? max(dist - max(1e-3 * dist, 2e-3), 0.0) : max(dist - 0.05 - offset, 0.0);
}
// Thin skin lit from behind: the light comes through the skin itself (transmitted_unoccluded).
bool rtxdi_through_skin(Surface s, RAB_LightSample ls) { return transmitted_unoccluded(s, ls.dir, light_ray_end(ls, ls.dist, 0.0)); }
bool rtxdi_visible(RAB_Surface surface, RAB_LightSample ls) {
    if (!ls.valid) return false;
    Surface s = surface.s;
    if (through_skin(s, ls.dir)) return rtxdi_through_skin(s, ls);
    float offset = ray_offset(s.distance);
    float3 origin = shadow_origin(s.position, s.geoNormal, ls.position - s.position, offset, s.translucent);
    float3 v = ls.position - origin;
    float dist = max(length(v), 1e-4);
    return !occluded_from_surface(origin, v / dist, 0.0, light_ray_end(ls, dist, offset), near_back(s.distance));
}
// The same ray for the final shading, tinted by the glass it crosses (transmittance_from_surface); reuse keeps rtxdi_visible.
// Thin skin lit from behind as there: from the surface, a lamp behind an ear was stopped by the ear's own front, so ears and
// fingers let the sun through (pt_di_spatial) but no lamp.
float3 rtxdi_transmittance(RAB_Surface surface, RAB_LightSample ls) {
    if (!ls.valid) return 0.0;
    Surface s = surface.s;
    if (through_skin(s, ls.dir)) return rtxdi_through_skin(s, ls) ? 1.0 : 0.0;
    float offset = ray_offset(s.distance);
    float3 origin = shadow_origin(s.position, s.geoNormal, ls.position - s.position, offset, s.translucent);
    float3 v = ls.position - origin;
    float dist = max(length(v), 1e-4);
    return transmittance_from_surface(origin, v / dist, 0.0, light_ray_end(ls, dist, offset), near_back(s.distance));
}
bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample ls) { return rtxdi_visible(surface, ls); }
// There is only this frame's acceleration structure: traced from the current surface, as RTXDI allows.
bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface previousSurface, RAB_LightSample ls) {
    return rtxdi_visible(currentSurface, ls);
}

// ---------------------------------------------------------------- ReSTIR GI
// The target function of a GI sample at a surface: the light arriving from its path vertex, times the cosine (the
// diffuse color is divided out of the traced light).
float RAB_GetGISampleTargetPdfForSurface(float3 samplePosition, float3 sampleRadiance, RAB_Surface surface) {
    float3 d = samplePosition - RAB_GetSurfaceWorldPos(surface);
    float len = length(d);
    return len > 0.0 ? luminance(sampleRadiance) * saturate(dot(surface.s.normal, d / len)) : 0.0;
}
// Whether a surface could have found a GI sample's path vertex: the shading pass's final visibility ray from it, and only
// above it (its rays never leave below it: pt_gi). With TGPT_OPTION_GI_VISIBILITY, ReSTIR GI's reuse counts a pixel for
// a sample only if so (tracer.cpp); the previous frame's surface is traced in this frame's scene.
bool rtxdi_gi_visible(RAB_Surface surface, float3 samplePosition) {
    Surface s = surface.s;
    const float offset = ray_offset(s.distance);
    const float3 origin = s.position + s.geoNormal * offset;
    const float3 toSample = samplePosition - origin;
    const float dist = max(length(toSample), 1e-4);
    const float3 dir = toSample / dist;
    if (dot(dir, s.geoNormal) <= 0.0) return false;
    const bool sky = gi_sample_is_sky(samplePosition, origin);
    return !occluded_from_surface(origin, dir, 0.0, sky ? g.params.y : max(dist - 0.02, 0.0), near_back(s.distance));
}
bool RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition) { return rtxdi_gi_visible(surface, samplePosition); }
bool RAB_GetTemporalConservativeVisibility(RAB_Surface currentSurface, RAB_Surface previousSurface, float3 samplePosition) { return rtxdi_gi_visible(previousSurface, samplePosition); }
// Large reconnection Jacobians come from very different geometry: the sample would be a poor fit here. As NVIDIA's
// RTXDI sample validates them: outside 1/10 to 10 the sample is discarded, and the rest clamped to 1/3 to 3 (unclamped,
// reuse could multiply a neighbour's bounced light up to 8 times near corners and contact: sparkles).
bool RAB_ValidateGISampleWithJacobian(inout float jacobian) {
    if ((g.settings2.y & OPTION_EXACT_JACOBIAN) != 0) return jacobian > 0.0 && jacobian < 1e30; // for testing: as it is
    if (jacobian > 10.0 || jacobian < 0.1) return false;
    jacobian = clamp(jacobian, 1.0 / 3.0, 3.0);
    return true;
}

// The motion RTXDI's temporal reuse follows: to the previous frame's pixel (HDRP's motion vectors, in pixels), and the
// change of the surface's distance from the camera (the previous camera's distance minus this one's).
float3 rtxdi_motion(uint2 p, Surface s) {
    Texture2D<float2> motion = ResourceDescriptorHeap[g.inputs1.y];
    float2 mv = motion.Load(int3(p, 0)) * float2(g.size.xy);
    return float3(-mv, length(s.position - g.prevCameraPos.xyz) - s.distance);
}
