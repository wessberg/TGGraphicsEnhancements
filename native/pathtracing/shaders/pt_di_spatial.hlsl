// SPDX-License-Identifier: MIT
// ReSTIR DI spatial reuse (NVIDIA RTXDI, its Basic bias correction; pt_rtxdi.hlsli) and direct lighting. Neighbours'
// reservoirs from this frame's temporal pass compete with the pixel's; the survivor gets a shadow ray (final visibility,
// never reused, and tinted by the glass it crosses: through a green bottle, a lamp's light and the sun's reach the table green).
// The sun is always sampled on its own, one shadow ray into a cone the size of its disc. Writes the direct diffuse light
// of the sun and the lamps (divided by the diffuse color) as a signal of its own, apart from the bounced light: its
// shadows are sharp and its noise low, and denoised together with the bounced light's they were blurred away. And the
// specular light of both.
#define RTXDI_ALLOWED_BIAS_CORRECTION RTXDI_BIAS_CORRECTION_RAY_TRACED
#include "pt_rtxdi.hlsli"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"
#include "Rtxdi/DI/SpatialResampling.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> sun = ResourceDescriptorHeap[g.signals.z];
    RWTexture2D<float4> specular = ResourceDescriptorHeap[g.signals.w];
    RAB_Surface surface = RAB_GetGBufferSurface(int2(p), false);
    const bool feedback = (g.settings2.y & OPTION_SPATIAL_FEEDBACK) != 0;
    if (!RAB_IsSurfaceValid(surface) || !frame_has_lights()) {
        if (feedback) RTXDI_StoreDIReservoir(RTXDI_EmptyDIReservoir(), g.rtxdiReservoirs, p, g.rtxdi.y);
        if (!RAB_IsSurfaceValid(surface)) { sun[p] = float4(0.0, 0.0, 0.0, -1.0); specular[p] = float4(0.0, 0.0, 0.0, -1.0); return; }
    }
    Surface s = surface.s;

    Rng rng = make_rng(p, g.size.z, 4);
    float offset = ray_offset(s.distance);
    float nearBack = near_back(s.distance);
    float3 localDiffuse = 0.0, spec = 0.0;

    if (frame_has_lights()) {
        RTXDI_DIReservoir r = RTXDI_LoadDIReservoir(g.rtxdiReservoirs, p, g.rtxdi.x);
        RAB_LightSample ls = RAB_EmptyLightSample();
        if ((g.settings2.y & (OPTION_NO_SPATIAL | OPTION_DI_NO_REUSE)) == 0 && g.diSpatial.numSamples > 0) {
            RTXDI_RandomSamplerState rtxdiRng = RTXDI_InitRandomSampler(p, g.size.z, 2);
            r = RTXDI_DISpatialResampling(p, surface, r, rtxdiRng, g.rtxdiRuntime, g.rtxdiReservoirs, g.rtxdi.x, g.diSpatial, ls);
        }
        // With spatial feedback (an option), the result is what the next frame's temporal reuse starts from.
        if (feedback) RTXDI_StoreDIReservoir(r, g.rtxdiReservoirs, p, g.rtxdi.y);
        // The light sample is taken from the resulting reservoir: RTXDI's Basic spatial reuse only returns one when a
        // neighbour's sample won (a pixel that kept its own got no direct light at all).
        if (RTXDI_IsValidDIReservoir(r))
            ls = RAB_SamplePolymorphicLight(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(r), false), surface, RTXDI_GetDIReservoirSampleUV(r));
        float W = RTXDI_GetDIReservoirInvPdf(r);
        // The shadow ray goes to a new point of the light's disc every frame: the reservoir keeps its point for up to 20
        // frames, so a penumbra's noise held still and then jumped (fringes' shadows on floors flickered). Its expected
        // visibility is the light's, and Ray Reconstruction takes out noise that changes every frame. Not an emissive
        // triangle's, which can be large: its light is shaded at the reservoir's point, and seen there or not (another
        // point's visibility times this one's light is not the light that arrives where a wall hides half a glowing panel).
        RAB_LightSample shadowPoint = ls;
        if (RTXDI_IsValidDIReservoir(r) && !ls.emitter)
            shadowPoint = RAB_SamplePolymorphicLight(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(r), false), surface, rng.next2());
        // Through glass, tinted by its color (transmittance_from_surface).
        float3 through = 0.0;
        if (RTXDI_IsValidDIReservoir(r) && W > 0.0 && W < 1e20) through = rtxdi_transmittance(surface, shadowPoint);
        if (any(through > 0.0)) {
            float3 d, sp;
            rtxdi_shade(ls, surface, d, sp);
            localDiffuse = d * W * through; spec = sp * W * through;
            // See-through glass's reflection ray finds this light's sphere itself (pt_specular reflected_lamps).
            if (s.glass && reflected_as_sphere(RAB_LoadLightInfo(RTXDI_GetDIReservoirLightIndex(r), false).l)) spec = 0.0;
        }
    }
    float3 sunDiffuse = 0.0;
    if (g.sunColor.w > 0.0) {
        float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
        float nl = translucent_cos(s.normal, l, s.through); // a grass blade, a leaf or thin cloth is lit through from behind too
        float3 sunOrigin = shadow_origin(s.position, s.geoNormal, l, offset, s.translucent);
        // Thin skin takes the light HDRP transmits through it too (skin_transmission): a little past the terminator in front,
        // and from behind through the skin itself (through_skin).
        const float3 skinThrough = g.sunColor.rgb * skin_transmission(s, l) / PI;
        float3 through = 0.0;
        if (nl > 0.0 && !through_skin(s, l)) through = transmittance_from_surface(sunOrigin, l, 0.0, g.params.y, nearBack);
        if (any(through > 0.0)) {
            sunDiffuse = g.sunColor.rgb * nl * terminator_softening(s, l) / PI;
            sunDiffuse = (sunDiffuse + skinThrough) * through;
            // See-through glass's reflection ray finds the sun's disc itself (pt_specular).
            if (dot(s.normal, l) > 0.0 && !s.glass) spec += g.sunColor.rgb * through * ggx_specular(s.normal, surface.view, l, s.f0, roughness_alpha(s.roughness));
        } else if (through_skin(s, l) && transmitted_unoccluded(s, l, g.params.y)) sunDiffuse = skinThrough;
    }
    sun[p] = float4(to_storage(sunDiffuse + clamp_radiance(localDiffuse, LIMIT_DIRECT)), 1.0);
    specular[p] = float4(to_storage(clamp_radiance(spec, LIMIT_DIRECT)), HIT_DISTANCE_MISS); // the reflection pass sets its own
}
