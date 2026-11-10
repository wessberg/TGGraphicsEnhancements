// SPDX-License-Identifier: MIT NVIDIA NRD's inputs and results, as NRD's README sets them up (PT-127). Since
// that day NRD takes all the light a surface gives, split into its diffuse and its specular part at the
// camera's surface and divided by their material factors (NRD_MaterialFactors), with real hit distances, the
// shading normal and 2.5D motion. Until then (round 2 to 2026-10-11) RELAX took the bounced light alone, in
// its directional form, against the triangle's normal with a hit distance of 1, and the tracer's own filters
// took the direct light and the reflections: the tavern's wood grain and its small shadows were blurred away
// and a gravel ground came out milky (runs 20261011-141211 and -150230). RELAX (NVIDIA: "designed for
// RTXDI") or REBLUR, DIFFUSE_SPECULAR (nrd_denoiser.cpp; the hidden "NRD trial" picks REBLUR). The inputs,
// for every pixel NRD takes (nrd_takes): the view depth along the camera's axis (positive); motion to the
// previous frame (its texture coordinate minus this one's, and the view depth's change: pt_common
// motion_depth_change, the point's own previous position, a moving thing's too); the shading normal (the
// normal map's, the light was shaded with; hair's across its strand, facing the view, as NRD's hair tips
// ask, and hair's material ID 1, NRD's strandMaterialID) with linear roughness; the diffuse light (the
// direct and the bounced light and the caustics, under a leaf's own occlusion: the composite's sum) times
// the diffuse color over NRD's diffuse factor, with the bounced light's hit distance (pt_gi_spatial:
// ReSTIR's sample through the diffuse lobe's lens, else the pixel's own cosine ray's); the specular light
// (the reflections and the lights' highlights) over NRD's specular factor, with the reflection's hit
// distance (a miss: HIT_DISTANCE_MISS). REBLUR takes the distances normalized
// (REBLUR_FrontEnd_GetNormHitDist with its default parameters, which nrd_denoiser.cpp leaves). A pixel whose
// camera ray met a still pure mirror is handed as the surface the mirror shows (primary surface replacement,
// pt_psr_record.hlsli: NRD README's own section on it), its light and guides from the pixel's record. Glass,
// water and what is seen from inside it are no pixel of NRD's (its README: "not natively designed for
// volumetrics or transparency"): a depth beyond the denoising range, which NRD leaves alone; they keep the
// reflections' own filter (tracer.cpp). NRD's units put the diffuse light's median at a half (tracer.cpp,
// from the histogram below). What is brighter in a channel than NRD takes (nrd_ceiling) is handed at it: the
// share it was handed is left in the bounced light's alpha (the diffuse light) and the direct light's (the
// specular), and what was not handed comes back as traced (the composite for the diffuse light, the back end
// for the specular: PT-50, no light cut). pass.b.z: the mode. 0 clears the histogram's bins (one group):
// pass.b.y the histogram (UAV, uints 64 to 127: log2 of the diffuse light handed to NRD from -24 to +8, half
// an EV a bin, lit pixels only). 1 (RELAX) and 2 (REBLUR), the front end, which fills the histogram too:
// pass.a UAVs view depth, motion, normal and roughness, the diffuse input; pass.b.x float, storage units
// into NRD's; pass.b.w the specular input. 3 (RELAX) and 4 (REBLUR), the back end: pass.a.x the diffuse
// result (read, then written as linear light divided by the diffuse color, in NRD's units), pass.a.y the
// specular result, pass.a.z the reflections' own filter's result, into which the specular light is written
// for every pixel NRD took (in storage units, pass.b.x: NRD's into them), glass and water keeping their own.
#include "pt_common.hlsli"
#include "NRD.hlsli"

groupshared uint s_bins[64];

// NRD's ReblurHitDistanceParameters as it leaves them (A, B, C): the normalization REBLUR undoes with them.
static const float3 REBLUR_HIT_PARAMS = float3(3.0, 0.1, 20.0);
// Whether NRD takes the pixel: glass, water and what is seen from inside it keep their own.
bool nrd_takes(Surface s) { return s.valid && s.deferred && !s.glass && !s.water && !s.underwater; }
// The material factors NRD's README demodulates by (NRD_MaterialFactors), the same in the front and the back end. The
// tracer's diffuse signals are its light divided by the diffuse color already (the composite multiplies it back), so NRD
// is handed that times the color over NRD's diffuse factor, and its result comes back times the factor over the color:
// the pair is exact wherever NRD blurs nothing. (A channel of no diffuse color reflects nothing: 0 there.)
void material_factors(Surface s, out float3 diffFactor, out float3 specFactor) {
    NRD_MaterialFactors(s.normal, normalize(g.cameraPos.xyz - s.position), saturate(s.albedo), s.f0, s.roughness, diffFactor, specFactor);
}
float3 diffuse_to_nrd(Surface s, float3 diffFactor) { return saturate(s.albedo) / diffFactor; }
float3 diffuse_from_nrd(Surface s, float3 diffFactor) {
    const float3 a = saturate(s.albedo);
    return float3(a.x > 1e-4 ? diffFactor.x / a.x : 0.0, a.y > 1e-4 ? diffFactor.y / a.y : 0.0, a.z > 1e-4 ? diffFactor.z / a.z : 0.0);
}
// The most NRD is handed in a channel: RELAX keeps the square of its input in half floats (README: "x^2 must fit into FP16
// range", the input "in a sane range [0; 250]"); REBLUR keeps no second moments and takes what a half float holds.
float nrd_ceiling(bool reblur) { return reblur ? 0.5 * NRD_FP16_MAX : NRD_LIMIT; }
float channel_max(float3 c) { return max(c.x, max(c.y, c.z)); }
// A replaced pixel (PT-127): NRD's material factors of the surface the mirror shows, seen along the mirrored view, times
// what the mirror passes of its light. The same pair divides each part in the front end and multiplies it back in the
// back end. What the record's two parts do not hold (what that surface emits, the fog's own light on the way, the
// mirror's own highlights, a part held at NRD's ceiling) is the rest of the pixel's traced light and comes back as
// traced: no light is cut and none is added.
void psr_factors(PsrRecord r, out float3 d, out float3 s) {
    NRD_MaterialFactors(psr_normal(r), psr_view(r), saturate(psr_albedo(r)), psr_f0(r), psr_roughness(r), d, s);
    const float3 through = psr_throughput(r);
    d = max(d * through, 1e-6); s = max(s * through, 1e-6);
}
float psr_handed(float3 light, float ceiling) { return min(1.0, ceiling / max(channel_max(light), 1e-30)); }

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID, uint t : SV_GroupIndex) {
    const uint mode = pass.b.z;
    if (mode == 0u) { RWStructuredBuffer<uint> clear = ResourceDescriptorHeap[pass.b.y]; clear[64u + t] = 0u; return; }
    if (mode == 3u || mode == 4u) {
        if (any(p >= g.size.xy)) return;
        Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
        if (!nrd_takes(s)) return;
        const bool reblur = mode == 4u;
        RWTexture2D<float4> diffOut = ResourceDescriptorHeap[pass.a.x];
        RWTexture2D<float4> specOut = ResourceDescriptorHeap[pass.a.y];
        RWTexture2D<float4> own = ResourceDescriptorHeap[pass.a.z];
        RWTexture2D<float4> sun = ResourceDescriptorHeap[g.signals.z];
        RWTexture2D<float4> raw = ResourceDescriptorHeap[g.signals.w];
        if (psr_replaced(p)) {
            const PsrRecord replacement = psr_load(p);
            const float3 endDiffuse = psr_diffuse(replacement).rgb, endSpecular = psr_specular(replacement).rgb;
            float3 df, sf; psr_factors(replacement, df, sf);
            const float inverseScale = asfloat(pass.b.x);
            const float ceiling = nrd_ceiling(reblur);
            const float hd = psr_handed(endDiffuse / (df * inverseScale), ceiling);
            const float hs = psr_handed(endSpecular / (sf * inverseScale), ceiling);
            const float4 d = diffOut[p], sp = specOut[p], original = raw[p];
            const float3 denoisedD = max(reblur ? REBLUR_BackEnd_UnpackRadianceAndNormHitDist(d).rgb : RELAX_BackEnd_UnpackRadiance(d).rgb, 0.0);
            const float3 denoisedS = max(reblur ? REBLUR_BackEnd_UnpackRadianceAndNormHitDist(sp).rgb : RELAX_BackEnd_UnpackRadiance(sp).rgb, 0.0);
            const float3 residual = original.rgb - endDiffuse * hd - endSpecular * hs;
            float3 back = residual + (denoisedD * df + denoisedS * sf) * inverseScale;
            // (For tgpt_gpu_test alone, g.psr.z, 0 in the game: the pixel shows one part of that sum. 6 the diffuse part as NRD
            // returned it, 7 the specular part, 8 the rest that comes back as traced, 9 the diffuse part as it was handed.)
            if (g.psr.z == 6u) back = denoisedD * df * inverseScale;
            else if (g.psr.z == 7u) back = denoisedS * sf * inverseScale;
            else if (g.psr.z == 8u) back = abs(residual);
            else if (g.psr.z == 9u) back = endDiffuse * hd;
            own[p] = float4(max(back, 0.0), original.a);
            diffOut[p] = 0.0; // reassembled in the mirror signal; the primary mirror has no diffuse component
            return;
        }
        float3 diffFactor, specFactor;
        material_factors(s, diffFactor, specFactor);
        const float4 d = diffOut[p], sp = specOut[p];
        diffOut[p] = float4(max(reblur ? REBLUR_BackEnd_UnpackRadianceAndNormHitDist(d).rgb : RELAX_BackEnd_UnpackRadiance(d).rgb, 0.0) * diffuse_from_nrd(s, diffFactor), d.w);
        const float3 spec = max(reblur ? REBLUR_BackEnd_UnpackRadianceAndNormHitDist(sp).rgb : RELAX_BackEnd_UnpackRadiance(sp).rgb, 0.0);
        const float4 r = raw[p];
        const float handed = saturate(sun[p].a);
        float3 back = spec * specFactor * asfloat(pass.b.x);
        if (handed < 1.0 && all(r.rgb < 1e30)) back += max(r.rgb, 0.0) * (1.0 - handed);
        own[p] = float4(back, r.a);
        return;
    }
    RWStructuredBuffer<uint> histogram = ResourceDescriptorHeap[pass.b.y];
    s_bins[t] = 0u;
    GroupMemoryBarrierWithGroupSync();
    if (all(p < g.size.xy)) {
        RWTexture2D<float> viewZ = ResourceDescriptorHeap[pass.a.x];
        RWTexture2D<float4> motion = ResourceDescriptorHeap[pass.a.y];
        RWTexture2D<float4> normalRoughness = ResourceDescriptorHeap[pass.a.z];
        RWTexture2D<float4> diffuse = ResourceDescriptorHeap[pass.a.w];
        RWTexture2D<float4> specIn = ResourceDescriptorHeap[pass.b.w];
        Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
        const bool reblur = mode == 2u;
        if (psr_replaced(p)) {
            // The surface a still pure mirror shows, in the mirror's virtual world (pt_psr_capture.hlsli wrote it).
            const PsrRecord replacement = psr_load(p);
            const float4 endDiffuse = psr_diffuse(replacement), endSpecular = psr_specular(replacement);
            const float endRoughness = psr_roughness(replacement);
            const float z = replacement.motionDepth.w;
            viewZ[p] = z;
            motion[p] = float4(-replacement.motionDepth.xy, replacement.motionDepth.z, 0.0);
            normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(psr_normal(replacement), endRoughness, psr_class(replacement));
            float3 df, sf; psr_factors(replacement, df, sf);
            const float scale = asfloat(pass.b.x), ceiling = nrd_ceiling(reblur);
            float3 d = endDiffuse.rgb * scale / df;
            float3 sp = endSpecular.rgb * scale / sf;
            const float l = luminance(d);
            d *= psr_handed(d, ceiling); sp *= psr_handed(sp, ceiling);
            if (reblur) {
                diffuse[p] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(d, REBLUR_FrontEnd_GetNormHitDist(endDiffuse.w, z, REBLUR_HIT_PARAMS, 1.0), true);
                specIn[p] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(sp, REBLUR_FrontEnd_GetNormHitDist(endSpecular.w, z, REBLUR_HIT_PARAMS, endRoughness), true);
            } else {
                diffuse[p] = RELAX_FrontEnd_PackRadianceAndHitDist(d, endDiffuse.w, true);
                specIn[p] = RELAX_FrontEnd_PackRadianceAndHitDist(sp, endSpecular.w, true);
            }
            RWTexture2D<float4> bounced = ResourceDescriptorHeap[g.signals.y];
            RWTexture2D<float4> sun = ResourceDescriptorHeap[g.signals.z];
            bounced[p] = float4(bounced[p].rgb, 1.0); sun[p] = float4(sun[p].rgb, 1.0);
            if (l > 0.0 && l < 1e30) InterlockedAdd(s_bins[uint(clamp((log2(l) + 24.0) * 2.0, 0.0, 63.0))], 1u);
        } else if (!nrd_takes(s)) {
            viewZ[p] = 1e6;
            motion[p] = 0.0;
            normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(float3(0.0, 0.0, 1.0), 1.0, 0.0);
            diffuse[p] = 0.0;
            specIn[p] = 0.0;
        } else {
            // Clip-space w is the distance along the view direction (Unity's view space looks down -z).
            const float z = mul(g.viewProj, float4(s.position - g.cameraPos.xyz, 1.0)).w;
            viewZ[p] = z;
            motion[p] = float4(-motion_at(p), motion_depth_change(p, s.position), 0.0);
            // NRD's material class, the same at the camera's surfaces and at replaced ones (pt_common psr_capture): hair 1
            // (NRD's strand class); where the mirrors' records are made (psr_on: nrd_denoiser.cpp then has NRD compare the
            // classes), a surface of no diffuse colour 2 and any other 0, as NRD's README advises against mixing the diffuse
            // light of metals and of what is none.
            const float materialClass = (s.strands || s.hair) ? 1.0 : (psr_on() && all(s.albedo == 0.0) ? 2.0 : 0.0);
            normalRoughness[p] = NRD_FrontEnd_PackNormalAndRoughness(s.normal, s.roughness, materialClass);
            RWTexture2D<float4> bounced = ResourceDescriptorHeap[g.signals.y];
            RWTexture2D<float4> sun = ResourceDescriptorHeap[g.signals.z];
            RWTexture2D<float4> specRaw = ResourceDescriptorHeap[g.signals.w];
            const float4 b = bounced[p], d = sun[p], r = specRaw[p];
            const float scale = asfloat(pass.b.x);
            const float occlusion = s.ambientOcclusion < 1.0 ? s.ambientOcclusion : 1.0;
            float3 diffFactor, specFactor;
            material_factors(s, diffFactor, specFactor);
            // The caustics are traced before the denoisers (tracer.cpp) for this.
            const float3 caustics = to_storage(caustic_light(s.position, s.geoNormal));
            float3 light = max((d.rgb + b.rgb * occlusion + caustics) * scale, 0.0) * diffuse_to_nrd(s, diffFactor);
            const float l = luminance(light), ceiling = nrd_ceiling(reblur);
            float handed = 1.0;
            if (channel_max(light) > ceiling) { handed = ceiling / channel_max(light); light *= handed; }
            bounced[p] = float4(b.rgb, handed);
            float3 spec = max(r.rgb * scale, 0.0) / specFactor;
            float specHanded = 1.0;
            if (channel_max(spec) > ceiling) { specHanded = ceiling / channel_max(spec); spec *= specHanded; }
            sun[p] = float4(d.rgb, specHanded);
            // (0 or less: no sample; NRD reads 0 as a skipped lobe, so such a pixel is taken as open to the sky.)
            const float hitDiffuse = b.a > 0.0 ? min(b.a, HIT_DISTANCE_MISS) : HIT_DISTANCE_MISS;
            const float hitSpecular = r.a > 0.0 ? min(r.a, HIT_DISTANCE_MISS) : HIT_DISTANCE_MISS;
            if (reblur) {
                diffuse[p] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(light, REBLUR_FrontEnd_GetNormHitDist(hitDiffuse, z, REBLUR_HIT_PARAMS, 1.0), true);
                specIn[p] = REBLUR_FrontEnd_PackRadianceAndNormHitDist(spec, REBLUR_FrontEnd_GetNormHitDist(hitSpecular, z, REBLUR_HIT_PARAMS, s.roughness), true);
            } else {
                diffuse[p] = RELAX_FrontEnd_PackRadianceAndHitDist(light, hitDiffuse, true);
                specIn[p] = RELAX_FrontEnd_PackRadianceAndHitDist(spec, hitSpecular, true);
            }
            if (l > 0.0 && l < 1e30) InterlockedAdd(s_bins[uint(clamp((log2(l) + 24.0) * 2.0, 0.0, 63.0))], 1u);
        }
    }
    GroupMemoryBarrierWithGroupSync();
    if (s_bins[t] != 0u) InterlockedAdd(histogram[64u + t], s_bins[t]);
}
