// SPDX-License-Identifier: MIT
// The traced light for the D3D11 composite, which lays it into HDRP's color buffer in this same frame, each pixel its
// own texel (PT-84; tracer.cpp composite_event waits for this pass on the GPU). Light is pre-exposed: output A holds
// the diffuse light (rgb) and the surface's distance from the camera (a, 0 where nothing was traced), output B the
// specular light and, in its alpha, what the texel holds (d3d11.hlsl composite).
// Own camera surfaces carry complete traced radiance; legacy G-buffer surfaces remain demodulated.
// Debug views store their colors in A instead. Ray Regeneration restores storage units and specular modulation
// in pt_rrg_guides before this pass. pass.a.z: with NRD, the float
// that brings its bounced light (in NRD's own units, tracer.cpp) back into the tracer's. pass.a.w, b.x: the tracer's guide
// outputs for Ray Reconstruction (traced_guides), which pt_prepare wrote first: the hair each pixel's ray crossed, or none.
// pass.b.w (PT-112 B, TGPT_FRAME_TRACED_FOG): the traced fog (pt_fog_integrate.hlsl, pt_fog_tail.hlsl), laid over the pixel; INVALID: none.
// PT-112, the sky, ON TRIAL (pt_sky.hlsli sky_draws_misses): the pixels whose camera ray met nothing drawn here, complete (B's
// alpha 1), where the plugin's hidden "Sky trial" leaves HDRP's sky out.
#include "pt_common.hlsli"
#include "pt_sky.hlsli"

float3 hash_color(uint v) { uint h = pcg(v * 2654435761u + 1u); return float3(h & 0xFF, (h >> 8) & 0xFF, (h >> 16) & 0xFF) / 255.0 * 0.8 + 0.2; }

float3 rr_clamp(float3 c) { float m = max(c.r, max(c.g, c.b)); return m > 256.0 ? c * (256.0 / m) : c; }

// The caustics (pt_caustics.hlsl, caustic_light): the sun's and the lamps' light traced off and through HDRP's water, where it
// landed on the surface the pixel shows, added to the direct light after its filter (the waves move it every frame: in a
// filter's history it would trail). Diffuse light, in storage units. None with the Caustics row off.
float3 caustics_at(Surface s) { return to_storage(caustic_light(s.position, s.geoNormal)); }

// Ray Reconstruction's guides where the tracer knows what the pixel shows better than HDRP's GBuffer, which the guides pass
// reads (d3d11.hlsl TGPT_GUIDES): the tracer's guide outputs g0 (color, sRGB, and how much of the pixel they describe) and g1
// (normal, octahedral; roughness; the primary hair guide's scalar specular albedo). Output C carries the final RGB specular
// reflectance with hit distance in A. The composite lays these over the guides pass's
// (d3d11.hlsl traced_guides). A surface HDRP draws forward that the tracer shades (an eye, hair HDRP draws whole cards of, a
// prop) has nothing in the GBuffer, and the guides pass gave it a grey albedo and a dielectric's reflectance: its traced
// color, normal, roughness and specular albedo, all of the pixel. Where the pixel's own camera ray crossed hair, pt_prepare
// wrote that hair's with its cover (pt_primary write_guides: the same every frame, whichever card the ray stopped at); over
// the surface the ray stopped at the two are blended by that cover (the normal the one covering more), as NVIDIA's RR
// guide (3.4.1) blends a transparent surface's guides over what lies behind it. Over a surface of the G-buffer (the skin
// under a brow) they stay as pt_prepare wrote them, and the composite lays them over the guides pass's by their cover.
float3 traced_guides(uint2 p, Surface s) {
    if (pass.a.w == INVALID || pass.b.x == INVALID) return 0.0;
    RWTexture2D<float4> g0 = ResourceDescriptorHeap[pass.a.w];
    RWTexture2D<float4> g1 = ResourceDescriptorHeap[pass.b.x];
    const float4 h0 = g0[p], h1 = g1[p];
    if (!(s.valid && s.forward)) return (h1.a * h1.a).xxx;
    const float c = saturate(h0.a);
    // The ray stopped at hair that covers part of the pixel, in front of a surface of HDRP's G-buffer (a brow's strand over
    // skin): the hair's guides stay with their cover, and the composite lays them over HDRP's guides for that surface by it:
    // the same blend as in the frames the ray passes the strand and the pixel is that surface (NVIDIA's guide 3.5: steady
    // guides). Laid on whole here, the guides flipped between the hair's and the blend from frame to frame.
    if (s.strands && c > 0.0 && c < 1.0) {
        Texture2D<float4> gb0 = ResourceDescriptorHeap[g.inputs0.x];
        if (gb0.Load(int3(p, 0)).a > 0.5) return (h1.a * h1.a).xxx;
    }
    const float3 v = normalize(g.cameraPos.xyz - s.position);
    float3 specular = specular_guide_albedo(s, saturate(dot(s.normal, v)));
    if (any(s.hairLobe > 0.0)) specular *= s.hairLobe.x; // hair with its highlights (pt_prepare): its specular map's texel
    const float3 albedo = lerp(s.glass ? 0.0 : saturate(s.albedo), srgb_to_linear(h0.rgb), c);
    specular = lerp(specular, h1.a * h1.a, c);
    g0[p] = float4(linear_to_srgb(saturate(albedo)), 1.0);
    // The primary hair guide still uses g1.a; the final RGB specular guide travels with hit distance in output C.
    g1[p] = float4(c > 0.5 ? h1.rg : guide_octahedral(s.normal), lerp(s.roughness, h1.b, c), h1.a);
    return saturate(specular);
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main(uint2 p : SV_DispatchThreadID) {
    if (any(p >= g.size.xy)) return;
    RWTexture2D<float4> outA = ResourceDescriptorHeap[g.scene1.w];
    RWTexture2D<float4> outB = ResourceDescriptorHeap[g.denoise2.w];
    Surface s = load_surface(p, g.surface.x, g.surface.z, g.material.x);
    const float3 rrSpecular = traced_guides(p, s);
    // PT-112 (A): the frame's own depth and motion for HDRP's buffers (tracer.cpp write_depth_motion writes them there when
    // the frame asks): the camera's own hit under HDRP's view-projection with the frame's sub-pixel shift (reversed-Z; 0,
    // the far plane, where the ray met nothing), and the pixel's motion as the tracer's reuse follows it (motion_at).
    {
        RWTexture2D<float> depthOut = ResourceDescriptorHeap[pass.b.y];
        RWTexture2D<float2> motionOut = ResourceDescriptorHeap[pass.b.z];
        float deviceDepth = 0.0;
        if (s.valid) {
            const float4 clip = mul(g.viewProj, float4(s.position - g.cameraPos.xyz, 1.0));
            if (clip.w > 1e-6) deviceDepth = saturate(clip.z / clip.w);
        }
        depthOut[p] = deviceDepth;
        motionOut[p] = motion_at(p);
    }
    uint view = g.settings.y;
    float ex = exposure();
    // Traced light is stored relative to the reference radiance, and shown with its own gain on top of HDRP's exposure.
    const float exStored0 = ex * reference_radiance() * traced_gain();
    float exStored = exStored0;
    // The traced fog over the pixel (pt_fog_integrate.hlsl; PT-112 B, the rule since 2026-10-11): its light toward the camera, shown as
    // the traced light is, and its transmittance. (A debug view has none: pass.b.w INVALID.)
    const bool tracedFog = pass.b.w != INVALID;
    float4 fog = float4(0.0, 0.0, 0.0, 1.0);
    if (tracedFog) {
        RWTexture2D<float4> fogIn = ResourceDescriptorHeap[pass.b.w];
        fog = fogIn[p];
        fog.rgb = all(fog.rgb < 1e30) ? max(fog.rgb, 0.0) * exStored0 : 0.0;
        fog.a = saturate(fog.a);
    }
    RWTexture2D<float4> diffuseFinal = ResourceDescriptorHeap[g.denoise2.z];
    float distance = s.valid ? s.distance : 0.0;
    // Glass with HDRP's sky behind it (see-through glass, pt_prepare; HDRP has no depth there): the texel says so (B's alpha
    // -2), and the composite lays it over HDRP's sky. (Until 2026-10-08 a glass texel also took the distance of HDRP's
    // surface behind it, which the composite matched it to a pixel by: it matches nothing now, each pixel has its own texel.)
    bool overSky = false;
    if (s.valid && s.glass) {
        Texture2D<float> hdrpDepth = ResourceDescriptorHeap[g.inputs1.x];
        overSky = !(hdrpDepth.Load(int3(p, 0)) > 0.0);
    }

    // For tgpt_gpu_test alone (tracer.cpp: TGPT_TEST_PSR_VIEW; g.psr.z is 0 in the game): what NRD was told of a pixel whose
    // surface was replaced (PT-127, pt_psr_record.hlsli), as a complete texel: 1 its view depth and its two hit distances,
    // 2 its normal, 3 its motion (times 64, and the depth's change times 16, about a half), 4 its roughness, its material
    // class over 4 and a 1, 5 the mirror's throughput. Black: no replaced surface. (6 to 9: pt_nrd_prep's back end shows a
    // part of the replaced pixel's light in the picture itself.)
    if (g.psr.z != 0u && g.psr.z <= 5u) {
        float3 c = 0.0;
        if (psr_replaced(p)) {
            const PsrRecord r = psr_load(p);
            if (g.psr.z == 1u) c = float3(r.motionDepth.w, psr_diffuse(r).w, psr_specular(r).w);
            else if (g.psr.z == 2u) c = psr_normal(r) * 0.5 + 0.5;
            else if (g.psr.z == 3u) c = float3(r.motionDepth.xy * 64.0, r.motionDepth.z * 16.0) + 0.5;
            else if (g.psr.z == 4u) c = float3(psr_roughness(r), psr_class(r) * 0.25, 1.0);
            else c = psr_throughput(r);
        }
        outA[p] = float4(c, max(distance, 1e-3)); outB[p] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    if (view == VIEW_COVERAGE || view == VIEW_MATERIALS || view == VIEW_EMISSIVE) {
        float3 rel = s.valid ? s.position - g.cameraPos.xyz : camera_relative_position(p, 1e-7);
        float3 dir = normalize(rel);
        float3 c = 0.0;
        RayDesc ray; ray.Origin = g.cameraPos.xyz; ray.Direction = dir; ray.TMin = 0.0; ray.TMax = g.params.y;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        // A camera ray, as pt_prepare's: it meets what only the camera sees and decides as HDRP does (alpha_opaque).
        ray_seed(g.cameraPos.xyz, dir);
        g_cameraRays = true;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_ALL, ray);
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
                                    q.CommittedObjectToWorld3x4(), dir, g.cameraPos.xyz + dir * q.CommittedRayT(), g.cameraPos.xyz);
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
    // anisotropic or iridescent, purple what the tracer lights as thin cloth; a surface HDRP draws forward cyan where the tracer found it along the camera ray (it lights
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
                // Thin cloth as the tracer takes it (its camera ray found TGPT_MATERIAL_THIN_CLOTH there, pt_prepare): purple. HDRP
                // draws the game's cloth as its standard material, so before this the view could not tell cloth from a wall (PT-49).
                if (s.valid && s.translucent && s.through == CLOTH_TRANSMISSION) c = float3(0.65, 0.3, 1.0);
                if (s.valid) c *=0.4 + 0.6 * saturate(dot(s.normal, normalize(g.cameraPos.xyz - s.position)));
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
    // The tracer's own first hit against HDRP's G-buffer (pt_primary yardstick_code; its colours: pt_common yardstick_color).
    if (view == VIEW_YARDSTICK) {
        float3 c = 0.0;
        if (g.primary.z != INVALID) { RWTexture2D<uint> yard = ResourceDescriptorHeap[g.primary.z]; c = yardstick_color(yard[p]); }
        outA[p] = float4(c, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (view == VIEW_NORMALS) { outA[p] = float4(s.valid ? s.normal * 0.5 + 0.5 : 0.0, max(distance, 1e-3)); outB[p] = 0.0; return; }
    if (view == VIEW_INDIRECT || view == VIEW_INDIRECT_RAW) {
        RWTexture2D<float4> raw = ResourceDescriptorHeap[g.signals.y];
        float3 d = s.valid && s.deferred ? (view == VIEW_INDIRECT ? diffuseFinal[p].rgb : raw[p].rgb) : 0.0;
        outA[p] = float4(d * exStored, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    if (view == VIEW_DIRECT) {
        RWTexture2D<float4> sun = ResourceDescriptorHeap[g.denoise1.x];
        float3 d = s.valid && s.deferred ? sun[p].rgb + caustics_at(s) : 0.0;
        outA[p] = float4(d * exStored, max(distance, 1e-3)); outB[p] = 0.0;
        return;
    }
    // The traced reflections' hit distance for Ray Reconstruction (pass a.y: its UAV), where the specular signal holds it.
    RWTexture2D<float4> outC = ResourceDescriptorHeap[pass.a.y]; // RGB specular albedo, A hit distance
    // A pixel whose own camera ray passed every hair card and met nothing behind them (pt_primary write_sky), where HDRP drew
    // a card into its depth and so no sky: the sky as a ray of the tracer's sees it, behind HDRP's fog as far as it lays its
    // fog over the sky, at HDRP's exposure alone (HDRP's own sky around it takes no traced gain). B's alpha 1: complete.
    // PT-112, the sky, on trial (pt_sky.hlsli): the pixels whose own camera ray met nothing, drawn here when HDRP's sky is
    // left out: the tracer's own sky per pixel (its discs, clouds and the atmosphere's light in front of them), or for the
    // measuring switch the map rays read; at HDRP's exposure alone and under the fog as the hair's sky below, complete (B's
    // alpha 1). Its distance the rays' most (A's alpha must say a texel was written).
    const bool missDrawn = !s.valid && sky_draws_misses();
    if (sky_through(s) || missDrawn) {
        const float3 dir = s.valid ? normalize(s.position - g.cameraPos.xyz) : normalize(camera_relative_position(p, 1e-7));
        const float3 sky = sky_draws_misses() ? sky_camera_miss(dir) : sky_radiance(dir);
        // (A sky the tracer draws is not held under Ray Reconstruction's 256: it has no noise, and HDRP's sky, the sun's disc
        // in it, never was.)
        const float3 shown = max(sky * ex * fog.a + fog.rgb, 0.0);
        outA[p] = float4(missDrawn ? shown : rr_clamp(shown), missDrawn ? max(g.params.y, 1.0) : distance); outB[p] = float4(0.0, 0.0, 0.0, 1.0); outC[p] = float4(rrSpecular, HIT_DISTANCE_MISS);
        return;
    }
    // (The traced fog over HDRP's sky, where the camera's ray met nothing: HDRP's sky through it, the fog's light added, as
    // the composite lays see-through glass over the sky: B's alpha -2, A the share of the sky it lets through.)
    if (!s.valid && tracedFog) {
        outA[p] = float4(fog.aaa, max(g.fogColor.w, 1.0)); outB[p] = float4(rr_clamp(fog.rgb), -2.0); outC[p] = float4(rrSpecular, HIT_DISTANCE_MISS);
        return;
    }
    if (!s.valid || !s.deferred) { outA[p] = 0.0; outB[p] = 0.0; outC[p] = float4(rrSpecular, HIT_DISTANCE_MISS); return; }
    RWTexture2D<float4> sun = ResourceDescriptorHeap[g.denoise1.x];
    RWTexture2D<float4> spec = ResourceDescriptorHeap[g.denoise1.z];
    float fromNrd = pass.a.z == INVALID ? 1.0 : asfloat(pass.a.z);
    // pass.a.x's 2: NRD ran (pt_nrd_prep): its result is all of the diffuse light, the direct and the bounced light and the
    // caustics, the leaf's own occlusion in it.
    const bool nrdAll = (pass.a.x & 2u) != 0u;
    float3 bounced = diffuseFinal[p].rgb * fromNrd;
    // With NRD, the light it could not hold (pt_nrd_prep held a channel at its ceiling: its half floats) comes back here as
    // traced, undenoised: no light is cut (PT-50). The share NRD was handed is in the bounced light's alpha.
    if (nrdAll) {
        RWTexture2D<float4> raw = ResourceDescriptorHeap[g.signals.y];
        const float4 r = raw[p];
        const float handed = saturate(r.a);
        const float3 traced = max(sun[p].rgb, 0.0) + max(r.rgb, 0.0) * min(s.ambientOcclusion, 1.0) + caustics_at(s);
        if (handed < 1.0 && all(traced < 1e30)) bounced += traced * (1.0 - handed);
    }
    // The direct light, denoised on its own, and the caustics (with NRD as documented, both in the bounced light's result).
    float3 diffuse = nrdAll ? bounced : bounced + sun[p].rgb + caustics_at(s);
    // A leaf's or a blade's own ambient occlusion (pt_prepare: its material's, as HDRP wrote it) on its bounced and sky light,
    // as HDRP takes it off a pixel's indirect light; its sun and lamps keep theirs.
    if (!nrdAll && s.ambientOcclusion < 1.0) diffuse -= bounced * (1.0 - s.ambientOcclusion);
    // See-through glass has no diffuse light: what it shows is in the specular signal (pt_specular's refraction).
    if (s.glass || s.water || s.underwater) diffuse = 0.0;
    float3 specular = spec[p].rgb;
    outC[p] = float4(rrSpecular, spec[p].a);
    // Highlights held (pt_common highlight_hold, PT-73 e): the gain's automatic part by how bright the pixel's traced light
    // already is, its diffuse light times its color plus its reflections (what pt_meter measures), both scaled alike (no
    // change of hue). Here, after every denoiser that runs before the composite (NRD's input stays linear in light). What a
    // surface emits itself is outside it, at the game's exposure as before.
    exStored *= highlight_hold(luminance(max(diffuse * s.albedo + specular, 0.0)) * exStored, traced_gain());
    // ON TRIAL (PT-40, pt_common highlight_shoulder; off or under its knee: times exactly 1): what is far past white rolls
    // off above the knee, after the hold, the same light and the same way (no change of hue, emission outside it).
    exStored *= highlight_rolled(luminance(max(diffuse * s.albedo + specular, 0.0)) * exStored);
    // The primary dielectric path includes the sky it sees. A sky-tagged texel
    // carries complete radiance, with no share of HDRP's background added to it.
    if (overSky) {
        outA[p] = float4(0.0, 0.0, 0.0, distance);
        outB[p] = float4(rr_clamp(max(specular * exStored * fog.a + fog.rgb, 0.0)), -2.0);
        return;
    }
    // A surface HDRP draws forward has no diffuse color in the GBuffer for the composite to multiply in: its traced
    // material's is multiplied in here, and B's alpha marks the light as complete.
    // What Ray Reconstruction takes is kept below 256 times white in each channel (as NVIDIA's RTXPT clamps its input):
    // a rare firefly far above everything around it stays in its history as detail otherwise.
    if (s.forward) {
        // And what it emits itself, which the tracer's own camera ray read at its hit (pt_primary write_own), at HDRP's exposure
        // alone: as the composite adds HDRP's GBuffer3 to a surface of the G-buffer, without the traced light's gain.
        float3 emitted = 0.0;
        if (g.primary.y != INVALID) { RWTexture2D<float4> own = ResourceDescriptorHeap[g.primary.y]; emitted = s.underwater ? 0.0 : own[p].rgb * ex; }
        outA[p] = float4(rr_clamp(max((diffuse * s.albedo * exStored + emitted) * fog.a + fog.rgb, 0.0)), distance);
        outB[p] = float4(rr_clamp(max(specular * exStored * fog.a, 0.0)), 1.0);
        return;
    }
    // Legacy output tag; own camera surfaces returned above with complete radiance.
    outA[p] = float4(rr_clamp(max(diffuse * exStored, 0.0)), distance);
    outB[p] = float4(rr_clamp(max(specular * exStored, 0.0)), s.glass ? -1.0 : 0.0);
}
