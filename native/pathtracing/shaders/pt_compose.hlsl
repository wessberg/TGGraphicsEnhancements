// SPDX-License-Identifier: MIT
// The traced light for the D3D11 composite, which applies it one frame later (reprojected through the
// next frame's motion vectors, so D3D11 never waits for D3D12 work of its own frame). Light is stored
// divided by the diffuse color and pre-exposed: output A holds the diffuse light (rgb) and the surface's
// distance from the camera (a, 0 where nothing was traced), output B the specular light.
// The composite multiplies the next frame's own albedo back in and adds its emission. For a pixel HDRP shades itself,
// A holds instead the share of HDRP's light the traced shadows leave, and B's alpha is 2.
// Debug views store their colors in A instead. pass.a.x: 1 when AMD's Ray Regeneration denoised the
// reflections divided by their albedo (pt_rrg_guides), which is multiplied back in here. pass.a.z: with NRD, the float
// that brings its bounced light (in NRD's own units, tracer.cpp) back into the tracer's.
#include "pt_common.hlsli"

float3 hash_color(uint v) { uint h = pcg(v * 2654435761u + 1u); return float3(h & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF) / 255.0 * 0.8 + 0.2; }

float3 rr_clamp(float3 c) { float m = max(c.r, max(c.g, c.b)); return m > 256.0 ? c * (256.0 / m) : c; }

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> outA = ResourceDescriptorHeap[g.scene1.w];
    RWTexture2D<float4> outB = ResourceDescriptorHeap[g.denoise2.w];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    uint view = g.settings.y;
    float ex = exposure();
    // Traced light is stored relative to the reference radiance, and shown with its own gain on top of HDRP's exposure.
    float exStored = ex * reference_radiance() * (s.valid ? traced_gain_at(s.position) : traced_gain());
    RWTexture2D<float4> diffuseFinal = ResourceDescriptorHeap[g.denoise2.z];
    float distance = s.valid ? s.distance : 0.0;

    if (view == VIEW_COVERAGE || view == VIEW_MATERIALS || view == VIEW_EMISSIVE) {
        float3 rel = s.valid ? s.position - g.cameraPos.xyz : camera_relative_position(p, 1e-7);
        float3 dir = normalize(rel);
        float3 c = 0.0;
        RayDesc ray; ray.Origin = g.cameraPos.xyz; ray.Direction = dir; ray.TMin = 0.0; ray.TMax = g.params.y;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        // A camera ray, as pt_prepare's: it meets what only the camera sees and decides as HDRP does (alpha_opaque).
        ray_seed(g.cameraPos.xyz, dir);
        g_cameraRays = true;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        g_cameraRays = false;
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            if (view == VIEW_COVERAGE) {
                c = hash_color(q.CommittedInstanceID());
                // Where the traced scene lies behind HDRP's surface, HDRP draws something the tracer lacks: darker.
                if (s.valid && q.CommittedRayT() > s.distance * 1.02 + 0.1) c *= 0.25;
            } else {
                Hit hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                                    q.CommittedObjectToWorld3x4(), dir, g.cameraPos.xyz + dir * q.CommittedRayT());
                SurfaceMaterial m = hit_material(hit);
                const float facing = 0.35 + 0.65 * saturate(dot(hit.normal, -dir));
                if (view == VIEW_MATERIALS) c = m.albedo * facing + m.emissive * ex;
                else {
                    // What the tracer makes of each emissive surface: green sampled as a light, blue a kept light's own surface
                    // (it lights nothing itself), red lighting only through the rays that meet it (the Emissive Lights row
                    // off, too dim, over the budget, moving, a character's); everything else dim grey.
                    c = 0.15 * luminance(m.albedo) * facing;
                    LightRecord t;
                    if (any(m.emissive > 0.0)) {
                        const uint flags = emitter_record(hit, t) ? t.flags : 0u;
                        c = ((flags & EMITTER_PAIRED) != 0 ? float3(0.15, 0.35, 1.0) : (flags & EMITTER_SAMPLED) != 0 ? float3(0.1, 1.0, 0.2) : float3(1.0, 0.15, 0.1)) * facing;
                    }
                }
            }
        }
        outA[p] = float4(c, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    // How HDRP draws each pixel: grey its standard deferred material, blue subsurface scattering, green transmission, red
    // transmission with subsurface scattering (the tracer lights both of those through from behind, as a leaf), orange
    // either of those through a solid diffusion profile (skin: the tracer lights it as a solid surface), yellow
    // anisotropic or iridescent; a surface HDRP draws forward cyan where the tracer found it along the camera ray (it lights
    // it), magenta where it didn't (HDRP's own shading stays); black nothing (sky).
    if (view == VIEW_SURFACES) {
        Texture2D<float> depthIn = ResourceDescriptorHeap[g.inputs1.x];
        Texture2D<float4> gb0In = ResourceDescriptorHeap[g.inputs0.x];
        Texture2D<float4> gb2In = ResourceDescriptorHeap[g.inputs0.z];
        float3 c = 0.0;
        if (depthIn.Load(int3(p, 0)) > 0.0) {
            if (gb0In.Load(int3(p, 0)).a <= 0.5) c = s.forward ? float3(0.0, 0.9, 1.0) : float3(1.0, 0.0, 1.0);
            else {
                float4 gb2 = gb2In.Load(int3(p, 0));
                uint feature = uint(gb2.a * 255.0 + 0.5) & 7u;
                c = feature == 0u ? float3(0.5, 0.5, 0.5) : feature == 1u ? float3(0.1, 0.3, 1.0) : feature == 2u ? float3(0.1, 0.9, 0.1) :
                    feature == 3u ? float3(1.0, 0.1, 0.1) : float3(1.0, 0.9, 0.1);
                if (solid_transmission(gb2, feature)) c = float3(1.0, 0.55, 0.1);
                if (s.valid) c *= 0.4 + 0.6 * saturate(dot(s.normal, normalize(g.cameraPos.xyz - s.position)));
            }
        }
        outA[p] = float4(c, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (view == VIEW_SPECULAR) {
        RWTexture2D<float4> specIn = ResourceDescriptorHeap[g.denoise1.z];
        outA[p] = float4(s.valid && s.deferred ? specIn[p].rgb * exStored : 0.0, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (view == VIEW_ALBEDO) { outA[p] = float4(s.valid ? s.albedo : 0.0, max(distance, 1e-3)); outB[p] = 0.0; return; }
    if (view == VIEW_NORMALS) { outA[p] = float4(s.valid ? s.normal * 0.5 + 0.5 : 0.0, max(distance, 1e-3)); outB[p] = 0.0; return; }
    if (view == VIEW_INDIRECT || view == VIEW_INDIRECT_RAW) {
        RWTexture2D<float4> raw = ResourceDescriptorHeap[g.signals.y];
        float3 d = s.valid && s.deferred ? (view == VIEW_INDIRECT ? diffuseFinal[p].rgb : raw[p].rgb) : 0.0;
        outA[p] = float4(d * exStored, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (view == VIEW_DIRECT) {
        RWTexture2D<float4> sun = ResourceDescriptorHeap[g.denoise1.x];
        float3 d = s.valid && s.deferred ? sun[p].rgb : 0.0;
        outA[p] = float4(d * exStored, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    // The traced reflections' hit distance for Ray Reconstruction (pass a.y: its UAV), where the specular signal holds it.
    RWTexture2D<float> outC = ResourceDescriptorHeap[pass.a.y];
    // A pixel HDRP shades itself (hair, eyes): the share of its light the traced shadows leave (pt_hdrp_shadow's history),
    // which the composite multiplies HDRP's color by; B's alpha 2 marks it. Every such pixel, those no shadow reaches too
    // (a share of 1): left out, one was no texel at all, and the composite spread its shadowed neighbours' shares over it.
    // B's color: light the composite adds to HDRP's. An eye takes the traced bounced light for the share of HDRP's ambient
    // light taken from it (HdrpPasses scales it by the sky's light at the camera, prevCameraPos.w: a hundredth of it indoors,
    // and the whites of the eyes went dark between the lids): its traced color (the eye's own texture) times the radiance
    // cache's light where it is. Not hair: its traced material lacks the tint HDRP's hair shader gives it, and blonde hair
    // under a stall's roof glowed far brighter than the woman it belongs to.
    if (s.valid && !s.deferred && s.hdrpShaded && g.material.z != INVALID) {
        RWTexture2D<float2> shares = ResourceDescriptorHeap[g.material.z];
        const float share = shares[p].x;
        const float taken = 1.0 - saturate(g.prevCameraPos.w);
        float3 bounced = 0.0;
        if (s.eye && taken > 0.0 && cache_on()) {
            Rng rng = make_rng(p, g.size.z, 13);
            [unroll] for (uint i = 0; i < 4; ++i) bounced += 0.25 * cache_light(s.position, s.geoNormal, rng);
        }
        const float3 add = s.albedo * bounced * (taken * ex * traced_gain_at(s.position));
        outA[p] = float4(share, share, share, distance); outB[p] = float4(rr_clamp(max(add, 0.0)), 2.0); outC[p] = HIT_DISTANCE_MISS;
        return;
    }
    if (!s.valid || !s.deferred) { outA[p] = 0.0; outB[p] = 0.0; outC[p] = HIT_DISTANCE_MISS; return; }
    RWTexture2D<float4> sun = ResourceDescriptorHeap[g.denoise1.x];
    RWTexture2D<float4> spec = ResourceDescriptorHeap[g.denoise1.z];
    float fromNrd = pass.a.z == INVALID ? 1.0 : asfloat(pass.a.z);
    float3 diffuse = diffuseFinal[p].rgb * fromNrd + sun[p].rgb; // the direct light, denoised on its own
    // See-through glass has no diffuse light: what it shows is in the specular signal (pt_specular's refraction).
    if (s.glass) diffuse = 0.0;
    float3 specular = spec[p].rgb;
    outC[p] = spec[p].a;
    if (pass.a.x == 1u) specular *= specular_guide_albedo(s, saturate(dot(s.normal, normalize(g.cameraPos.xyz - s.position))));
    // A surface HDRP draws forward has no diffuse color in the GBuffer for the composite to multiply in: its traced
    // material's is multiplied in here, and B's alpha marks the light as complete.
    // What Ray Reconstruction takes is kept below 256 times white in each channel (as NVIDIA's RTXPT clamps its input):
    // a rare firefly far above everything around it stays in its history as detail otherwise.
    if (s.forward) {
        outA[p] = float4(rr_clamp(max(diffuse * s.albedo * exStored, 0.0)), distance);
        outB[p] = float4(rr_clamp(max(specular * exStored, 0.0)), 1.0);
        return;
    }
    // B's alpha -1: see-through glass, whose Ray Reconstruction guides the composite makes a near-mirror's that covers the
    // light it lets through (d3d11.hlsl).
    outA[p] = float4(rr_clamp(max(diffuse * exStored, 0.0)), distance);
    outB[p] = float4(rr_clamp(max(specular * exStored, 0.0)), s.glass ? -1.0 : 0.0);
}
