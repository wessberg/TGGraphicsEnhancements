// SPDX-License-Identifier: MIT
// Shared by every path tracing pass: the frame constants, scene records, packing, sampling, BRDFs,
// ray traversal with alpha testing, and the shading of path vertices. Shader Model 6.6: every resource
// is reached through ResourceDescriptorHeap by the index the constants give.
#pragma once
#include "pt_root.hlsli"

static const float PI = 3.14159265358979;
static const uint INVALID = 0xFFFFFFFFu;
static const uint ATLAS_SIZE = 4096;
static const uint TILE_SIZE = 64;
static const uint TILES_PER_ROW = ATLAS_SIZE / TILE_SIZE;
static const uint ALPHA_TILE_SIZE = 256;   // the alpha atlas: alpha-tested materials' alpha, R8
static const uint ALPHA_TILES_PER_ROW = ATLAS_SIZE / ALPHA_TILE_SIZE;
static const uint COOKIE_TILE_SIZE = 64;     // lights' cookies (runtime.hpp kCookie*)
static const uint COOKIE_TILES_PER_ROW = 1024 / COOKIE_TILE_SIZE;

#define VIEW_NONE 0
#define VIEW_ALBEDO 1
#define VIEW_NORMALS 2
#define VIEW_COVERAGE 3
#define VIEW_MATERIALS 4
#define VIEW_INDIRECT 5
#define VIEW_INDIRECT_RAW 6
#define VIEW_DIRECT 7
#define VIEW_SURFACES 8
#define VIEW_SPECULAR 9
#define VIEW_EMISSIVE 10
#define OPTION_NO_SPATIAL 1
#define OPTION_NO_TEMPORAL 2
#define OPTION_NO_DENOISER 4
#define OPTION_NO_SPECULAR 8
#define OPTION_RAY_RECONSTRUCTION 16
#define OPTION_RAY_REGENERATION 64
#define OPTION_NO_PASS_THROUGH 128
#define OPTION_DI_VISIBILITY 256
#define OPTION_SPATIAL_FEEDBACK 512
#define OPTION_NO_BOILING 2048        // for testing (tgpt_api.h)
#define OPTION_NO_CLAMPS 4096         // for testing
#define OPTION_EXACT_JACOBIAN 8192    // for testing
#define OPTION_OFFSET_RECEIVER 16384  // for testing
#define OPTION_GI_VISIBILITY 2097152  // bounced light's reuse with visibility (tgpt_api.h)
#define OPTION_NO_HDRP_SHADOWS 4194304 // what HDRP shades itself (hair, eyes) keeps HDRP's light
#define OPTION_GI_OWN_RAYS 524288      // the capture's comparison: the bounced light from the pixel's own rays alone (tgpt_api.h)
#define OPTION_DI_NO_REUSE 1048576     // the capture's comparison: the direct light without ReSTIR DI's reuse
#define OPTION_GLOSSY_REFLECTIONS 262144 // Reflections on Glossy Surfaces (tgpt_api.h) unshadowed (pt_hdrp_shadow)
// Miss distance for the denoisers' hit-distance channels (FP16's largest, as AMD's sample writes it).
#define HIT_DISTANCE_MISS 65504.0

// DirectX Raytracing 1.2 (Shader Model 6.9, built with PT_DXR12): ray queries see opacity micromaps, which
// settle most alpha-tested triangles without the alpha test. With PT_SER the passes whose threads shade
// the most different things run as ray generation shaders, and Shader Execution Reordering regroups
// their threads by the material their ray hit before shading.
#ifdef PT_DXR12
#define PT_RAYQUERY(flags) RayQuery<flags, RAYQUERY_FLAG_ALLOW_OPACITY_MICROMAPS>
#else
#define PT_RAYQUERY(flags) RayQuery<flags>
#endif
#ifdef PT_SER
#define PT_REORDER(key) dx::MaybeReorderThread(uint(key) & 0xFFFFu, 16)
#define PT_ENTRY(body, name) [shader("raygeneration")] void name() { body(DispatchRaysIndex().xy); }
#else
#define PT_REORDER(key)
#define PT_ENTRY(body, name) [RootSignature(PT_ROOT_SIGNATURE)] [numthreads(8, 8, 1)] void main(uint2 p : SV_DispatchThreadID) { body(p); }
#endif
#define FLAG_RESET 1
#define FLAG_DENOISER_RESET 2 // the built-in denoiser resumes after frames it skipped: its history is stale
#define MATERIAL_DOUBLE_SIDED 1
#define MATERIAL_ALPHA_TEST 2
#define MATERIAL_VERTEX_COLOR 4
#define MATERIAL_ALPHA_TILE 8
#define MATERIAL_CAMERA_ONLY 16
#define MATERIAL_HDRP_SHADED 32
#define MATERIAL_GLASS 64
#define MATERIAL_MASK_TILE 1024
#define MATERIAL_EMISSIVE_ALBEDO 128 // the emission times the albedo (HDRP's Albedo Affects Emissive)
#define MATERIAL_STRANDS 4096 // hair: its alpha tile's coverage is the chance a ray stops there (alpha_opaque)
#define MATERIAL_TRANSLUCENT 256 // grass, leaves: lit through from behind at path vertices (direct_diffuse)
#define MATERIAL_THIN_CLOTH 512  // banners, laundry, canvas: lit through from behind, less than a leaf (CLOTH_TRANSMISSION)
#define MATERIAL_SWAYS 8192 // moved by the wind as HDRP draws it (MASK_SWAYS)
// Set while a camera ray is traced (pt_prepare): only those see MATERIAL_CAMERA_ONLY surfaces.
static bool g_cameraRays = false;
// The ray being traced's own number (ray_seed), for glass to let it through by.
static uint g_raySeed = 0;
// Set while the refraction path of see-through glass looks for the glass's far side (pt_specular): glass is solid to it.
static bool g_glassSolid = false;
// Set while a shadow ray takes glass's color instead of its chance (transmittance): glass lets it on, counting how often it
// was met, and glass_walk tints the ray by it afterwards.
static bool g_glassTint = false;
static uint g_glassMet = 0;
static const float GLASS_IOR = 1.5;

// NVIDIA RTXDI's parameter blocks (its ReSTIR DI and GI reuse, pt_rtxdi.hlsli), shared with tracer.cpp.
#include "Rtxdi/DI/ReSTIRDIParameters.h"
#include "Rtxdi/GI/ReSTIRGIParameters.h"

// Kept in step with FrameConstants in tracer.cpp.
struct FrameConstants {
    float4x4 invViewProj;   // clip to camera-relative world (HDRP's, jittered)
    float4x4 viewProj;      // camera-relative world to clip
    float4x4 prevViewProj;  // previous camera-relative world to clip
    float4 cameraPos;       // xyz: camera relative to the scene origin; w: Reflection Boost (0 off)
    float4 prevCameraPos;   // xyz: previous camera relative to the current scene origin; w: the share of HDRP's ambient
                            // light what it shades itself keeps (TGPT_Frame.forward: pt_compose adds the traced for the rest)
    float4 prevOriginDelta; // xyz: previous origin - current origin; w: Indirect Color (1 as traced)
    float4 sunDir;          // xyz towards the sun; w: cos of its angular radius
    float4 sunColor;        // rgb: lux; w: 1 with a sun
    uint4 size;             // width, height, frame index, flags
    uint4 settings;         // thick diffusion profiles (a bit each), debug view, bounces, spatial samples
    uint4 settings2;        // the previous frame's light count, options, light count, a-trous iterations
    float4 params;          // unused, max ray distance, sky intensity, spatial radius (pixels)
    uint4 inputs0;          // SRVs: albedo+deferred flag, normal buffer, GBuffer2, GBuffer3
    uint4 inputs1;          // SRVs: depth, motion, exposure, sky
    uint4 scene0;           // SRVs: instances, meshes, geometries, materials
    uint4 scene1;           // SRVs: material indices, lights, atlas; UAV: output
    uint4 surface;          // UAVs: surface cur/prev, normal cur/prev
    uint4 material;         // UAVs: material cur, RTXDI's GI reservoirs (both frames' arrays); the shares of HDRP's light the traced
                            // shadows leave (pt_hdrp_shadow's history, INVALID without it); RTXDI's DI reservoirs
    uint4 signals;          // SRV: lights' cookies; UAVs: diffuse raw, sun raw, specular raw
    uint4 denoise0;         // UAVs: diffuse history cur/prev, moments cur/prev
    uint4 denoise1;         // UAVs: sun history cur/prev, specular history cur/prev
    uint4 denoise2;         // UAVs: a-trous ping, pong; the index holding the final diffuse; unused
    float4 gridMin;         // xyz: the light grid's corner relative to the origin; w: 1 / cell size
    uint4 grid;             // SRVs: grid cells (first, count), light indices; dimensions (8 bits each); rays per pixel
    uint4 extra;            // SRV: the alpha atlas; UAV: material prev; SRV: RTXDI's neighbour offsets; SRV: the previous frame's lights
    // RTXDI's ReSTIR DI reuse, with the parameters ReSTIRDI.cpp documents.
    RTXDI_RuntimeParameters rtxdiRuntime;          // neighbour offset mask, no checkerboard, frame index
    RTXDI_ReservoirBufferParameters rtxdiReservoirs;
    RTXDI_DITemporalResamplingParameters diTemporal;
    RTXDI_BoilingFilterParameters diBoiling;
    RTXDI_DISpatialResamplingParameters diSpatial;
    uint4 rtxdi;                                   // reservoir arrays: DI this frame, DI previous, GI this frame, GI previous
    // RTXDI's ReSTIR GI reuse, with the parameters ReSTIRGI.cpp documents.
    RTXDI_GITemporalResamplingParameters giTemporal;
    RTXDI_GISpatialResamplingParameters giSpatial;
    RTXDI_BoilingFilterParameters giBoiling;
    // x: the reference radiance traced light is stored relative to (a white surface in full sun, or lit by the brightest
    // light 1 m away; tracer.cpp); y: the previous frame's; z: the one the radiance clamps measure against (the sun's, or
    // the brightest light's 25 cm away); w: 1 when the scene has instances the wind moves (MASK_SWAYS), plus 2 with thin cloth.
    float4 radiance;
    // Light scattered under skin, per HDRP diffusion profile (its GBuffer index): Burley's shape per channel (1 / the
    // scattering distance, per metre) and the filter radius in metres, 0 for none (pt_sss.hlsl).
    float4 sssProfiles[16];
    uint4 cache;        // the radiance cache's buffers (UAVs, raw): its keys, this frame's sums, its light; w: 1 while it is on
    float4 cacheOrigin; // xyz: the scene origin in the world, in metres (the cache's cells stay where they are when it moves)
    // Emissive triangles (scene.cpp build_emitters): the emitter records (SRV), the previous frame's, and their counts.
    uint4 emissive;
    // The instance table (SRV: an instance's submeshes' first slots), the static instances it covers, the clusters sampled as
    // lights (0: none, or the Emissive Lights row off) and the first one's record.
    uint4 emissive2;
    // Light through thin skin, per HDRP diffusion profile (its GBuffer index), as HDRP's constants have it
    // (TGPT_SetSkinTransmission): [2i] its transmission tint / 4 and the thickness remap's start (mm), [2i + 1] Burley's
    // shape per channel (per mm) and the remap's span (mm). A tint of 0: no light through (skin_transmittance).
    float4 transmission[32];
    // HDRP's height fog, as its Fog volume sets it (traced_gain_at): extinction per metre (0 without fog), its base height in
    // the world, 1 / its scale height; w: the player's Exposure as a factor, the part of traced_gain that isn't automatic.
    float4 fog;
};
ConstantBuffer<FrameConstants> g : register(b0);
struct PassConstants { uint4 a; uint4 b; };
ConstantBuffer<PassConstants> pass : register(b1);
RaytracingAccelerationStructure g_scene : register(t0);
SamplerState s_linearClamp : register(s0);
SamplerState s_linearWrap : register(s1);
SamplerState s_pointClamp : register(s2);

struct InstanceRecord { uint mesh; uint materialBase; uint flags; uint pad; };
struct MeshRecord { uint positions; uint uvs; uint colors; uint indices; uint geometryBase; uint geometryCount; uint vertexCount; uint pad; };
struct GeometryRecord { uint indexStart; uint indexCount; uint pad0; uint pad1; };
struct MaterialRecord {
    float4 baseColor; float4 uvST;
    float3 emissive; float metallic;
    int albedoTile; int emissiveTile; float smoothness; float alphaCutoff;
    uint flags; int alphaTile; int maskTile; uint maskRemap; // alphaTile: with MATERIAL_ALPHA_TILE, the alpha atlas's tile;
                                                              // maskTile, maskRemap: with MATERIAL_MASK_TILE (metallic_smoothness)
};
struct LightRecord {
    float3 position; uint type;
    float3 color; float range;
    float3 direction; float radius;
    float angleScale; float angleOffset; uint id; uint flags; // flags: 1 + its cookie, 0 without one
    float3 up; float cookieScale;
};

// ---------------------------------------------------------------- packing
float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 srgb_to_linear(float3 c) { return select(c <= 0.04045, c / 12.92, pow((c + 0.055) / 1.055, 2.4)); }
float3 linear_to_srgb(float3 c) { return select(c <= 0.0031308, c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055); }
// HDRP's FastSRGBToLinear, the inverse of the FastLinearToSRGB it writes GBuffer2 with.
float3 fast_srgb_to_linear(float3 c) { return c * (c * (c * 0.305306011 + 0.682171111) + 0.012522878); }
float2 oct_wrap(float2 v) { return (1.0 - abs(v.yx)) * select(v.xy >= 0.0, 1.0, -1.0); }
uint pack_normal(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    uint2 q = uint2(round(saturate(e * 0.5 + 0.5) * 65535.0));
    return q.x | (q.y << 16);
}
float3 unpack_normal(uint p) {
    float2 e = float2(p & 0xFFFF, p >> 16) / 65535.0 * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}
// The same in 22 bits (11:11), for the geometric normal beside the roughness.
uint pack_normal22(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
    uint2 q = uint2(round(saturate(e * 0.5 + 0.5) * 2047.0));
    return q.x | (q.y << 11);
}
float3 unpack_normal22(uint p) {
    float2 e = float2(p & 0x7FF, (p >> 11) & 0x7FF) / 2047.0 * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}
// HDRP's normal buffer: octahedral normal in 12:12 bits over RGB, perceptual roughness in A.
float3 decode_hdrp_normal(float4 v) {
    uint3 i = (uint3)(v.rgb * 255.5);
    uint hi = i.z >> 4, lo = i.z & 15;
    uint2 cb = i.xy | uint2(lo << 8, hi << 8);
    float2 f = float2(cb) / 4095.0 * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = max(-n.z, 0.0);
    n.xy += float2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}
uint2 pack_half3(float3 c, uint flags) { return uint2(f32tof16(c.x) | (f32tof16(c.y) << 16), f32tof16(c.z) | (flags << 16)); }
float3 unpack_half3(uint2 p) { return float3(f16tof32(p.x & 0xFFFF), f16tof32(p.x >> 16), f16tof32(p.y & 0xFFFF)); }
uint pack_rgba8(float4 c) { uint4 q = uint4(round(saturate(c) * 255.0)); return q.x | (q.y << 8) | (q.z << 16) | (q.w << 24); }
float4 unpack_rgba8(uint p) { return float4(p & 0xFF, (p >> 8) & 0xFF, (p >> 16) & 0xFF, p >> 24) / 255.0; }

// The primary surface as the prepare pass stores it.
struct Surface {
    float3 position;   // relative to the scene origin
    float distance;    // from the camera; 0 for the sky
    float3 normal;     // HDRP's, with normal maps: for shading
    float3 geoNormal;  // the surface's own orientation, from depth: for deciding which pixels share light
    float roughness;   // perceptual
    float3 albedo;     // HDRP's diffuse color, linear
    float3 f0;
    bool deferred;     // shaded by the tracer: HDRP's deferred material, or a surface HDRP draws forward that the camera ray found
    bool translucent;  // HDRP's transmission (grass, leaves): lit from behind too (translucent_cos)
    bool complex;      // thin or finely detailed where the pixel is (pt_prepare): ReSTIR's reuse isn't permuted there
    bool valid;
    bool forward;      // drawn forward by HDRP (hair, some props): its color is the traced material's, not in the GBuffer
    bool glass;        // a smooth texel of glass (MATERIAL_GLASS) in the GBuffer: seen through (pt_specular), no diffuse light
    float3 transmittance; // thin skin (an ear, a nostril): HDRP's transmittance of light from behind (skin_transmission), else 0
    bool hdrpShaded;   // drawn forward and shaded by HDRP, found as such along the camera ray (hair, eyes): pt_hdrp_shadow
    bool eye;          // of those, one seen by camera rays only (an eye: MATERIAL_CAMERA_ONLY): pt_compose gives it bounced light
    float through;     // the share a translucent surface lets through from behind (translucent_cos): a leaf's, or thin cloth's
};
// The cosine a light arriving along l lights a surface with: from in front, and for a translucent one also from behind, the
// part it lets through (through): a grass blade's or a leaf's (TRANSMISSION: without it every blade facing away from the
// sun was black, where HDRP lights it through), or thin cloth's, the light through its weave (CLOTH_TRANSMISSION, as white
// linen lets through a quarter to a third of it: a banner held up to the sun shows the light behind it, dimmer than a leaf).
static const float TRANSMISSION = 0.5;
static const float CLOTH_TRANSMISSION = 0.3;
float translucent_cos(float3 normal, float3 l, float through) {
    float c = dot(normal, l);
    return c > 0.0 ? c : -c * through;
}
// Light through thin skin from a light along l (HDRP's thin-object transmission, as the game's skin profile has it): the
// skin's transmittance times the cosine HDRP lights it through with, wrapped 15 degrees past the terminator
// (TRANSMISSION_WRAP_LIGHT). Lit as a solid surface, a backlit ear or a hand held up to a candle was as dark as the back
// of a head. The traced geometry decides whether any of it gets there (through_skin, transmitted_unoccluded).
float3 skin_transmission(Surface s, float3 l) {
    const float w = 0.258819; // cos(90 - 15 degrees)
    return s.transmittance * saturate((w - dot(s.normal, l)) / ((1.0 + w) * (1.0 + w)));
}
// Whether light along dir reaches thin skin from behind it (its own triangle faces away from the light): through the skin.
bool through_skin(Surface s, float3 dir) { return any(s.transmittance > 0.0) && dot(s.geoNormal, dir) < 0.0; }
// Where a shadow ray towards a light along dir leaves the surface: a little above it, and for a translucent leaf lit
// from behind on the side the light is on (a ray from the camera's side would cross the leaf and be stopped by it).
// An opaque surface always starts outside: switching sides where its own flat triangle turned from the light moved
// the origin some 4 cm into a head, and the jump left a hard edge across faces (terminator_softening fades the light
// out there instead).
float3 shadow_origin(float3 position, float3 geoNormal, float3 dir, float offset, bool translucent) {
    return position + geoNormal * (!translucent || dot(geoNormal, dir) >= 0.0 ? offset : -offset);
}
Surface load_surface(uint2 p, uint surfaceIndex, uint normalIndex, uint materialIndex) {
    RWTexture2D<float4> surf = ResourceDescriptorHeap[surfaceIndex];
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[normalIndex];
    Surface s;
    float4 a = surf[p];
    uint2 n = nrm[p];
    s.position = a.xyz; s.distance = a.w;
    s.normal = unpack_normal(n.x);
    s.geoNormal = (n.y >> 10) != 0 ? unpack_normal22(n.y >> 10) : s.normal;
    s.roughness = float(n.y & 0xFF) / 255.0;
    s.deferred = (n.y & 0x100) != 0;
    s.translucent = (n.y & 0x200) != 0;
    s.through = s.translucent ? TRANSMISSION : 0.0;
    s.valid = a.w > 0.0;
    if (materialIndex != INVALID) {
        RWTexture2D<uint2> mat = ResourceDescriptorHeap[materialIndex];
        uint2 m = mat[p];
        float4 color = unpack_rgba8(m.x);
        s.albedo = srgb_to_linear(color.rgb);
        s.forward = color.a > 0.5;
        float4 spec = unpack_rgba8(m.y);
        s.f0 = srgb_to_linear(spec.rgb);
        const uint bits = m.y >> 24; // pt_prepare: 1 a complex surface, 2 glass, 4 thin cloth, 8 an eye, 16 thin skin, 32 shaded by HDRP
        if (s.translucent && (bits & 4u) != 0) s.through = CLOTH_TRANSMISSION;
        s.complex = (bits & 1u) != 0;
        s.glass = (bits & 2u) != 0;
        s.hdrpShaded = (bits & 32u) != 0;
        s.eye = (bits & 8u) != 0;
        // Thin skin keeps its transmittance where the specular color is kept: that of a transmission material is always a
        // dielectric's (pt_prepare), which Reflection Boost leaves on skin.
        s.transmittance = (bits & 16u) != 0 ? s.f0 : 0.0;
        if ((bits & 16u) != 0) s.f0 = 0.04;
    } else { s.albedo = 0.5; s.f0 = 0.04; s.forward = false; s.complex = false; s.glass = false; s.transmittance = 0.0; s.hdrpShaded = false; s.eye = false; }
    return s;
}

// ---------------------------------------------------------------- random numbers
uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
struct Rng {
    uint state;
    float next() { state = pcg(state); return float(state >> 8) * (1.0 / 16777216.0); }
    float2 next2() { return float2(next(), next()); }
};
Rng make_rng(uint2 pixel, uint frame, uint salt) {
    Rng r; r.state = pcg(pixel.x + pcg(pixel.y + pcg(frame * 7919u + salt * 0x9E3779B9u)));
    return r;
}

// ---------------------------------------------------------------- sampling
void basis(float3 n, out float3 t, out float3 b) {
    float s = n.z >= 0.0 ? 1.0 : -1.0;
    float a = -1.0 / (s + n.z);
    float c = n.x * n.y * a;
    t = float3(1.0 + s * n.x * n.x * a, s * c, -s * n.x);
    b = float3(c, s + n.y * n.y * a, -n.y);
}
float3 to_world(float3 v, float3 n) { float3 t, b; basis(n, t, b); return v.x * t + v.y * b + v.z * n; }
float3 cosine_sample(float3 n, float2 u) {
    float r = sqrt(u.x), phi = 2.0 * PI * u.y;
    return normalize(to_world(float3(r * cos(phi), r * sin(phi), sqrt(max(0.0, 1.0 - u.x))), n));
}
float3 cone_sample(float3 dir, float cosMax, float2 u) {
    float c = 1.0 - u.x * (1.0 - cosMax);
    float s = sqrt(max(0.0, 1.0 - c * c)), phi = 2.0 * PI * u.y;
    return normalize(to_world(float3(s * cos(phi), s * sin(phi), c), dir));
}
float2 disk_sample(float2 u) { float r = sqrt(u.x), phi = 2.0 * PI * u.y; return r * float2(cos(phi), sin(phi)); }
// Taming the Shadow Terminator (Chiang, Li and Burley 2019): a shading normal (smooth, normal-mapped) can face a light
// the surface's own flatter triangle faces away from, and the traced shadow then cuts the light off in a step along
// the triangles (faces, rounded armour, pillars). The light fades out towards the geometric terminator instead. Not
// for a translucent surface, lit from behind on purpose.
float terminator_softening(Surface s, float3 l) {
    if (s.translucent) return 1.0;
    float ns = dot(s.normal, l), ng = dot(s.geoNormal, l), nn = dot(s.geoNormal, s.normal);
    if (ns <= 0.0 || nn <= 0.0) return 1.0;
    float g = saturate(ng / (ns * nn));
    return g * (1.0 + g * (1.0 - g));
}

// ---------------------------------------------------------------- BRDF
// With the grazing reflectance of a surface that barely reflects scaled down with it, as Unreal's and Frostbite's Schlick are
// (f90 = saturate(50 f0), 1 from a reflectance of 2 % on): below that no real material lies, and HDRP's Unlit (f0 0,
// pt_prepare) reflects nothing at all, as in the game.
float3 fresnel_schlick(float3 f0, float cosTheta) {
    const float f90 = saturate(50.0 * dot(f0, float3(0.3333, 0.3333, 0.3333)));
    return f0 + (f90 - f0) * pow(1.0 - saturate(cosTheta), 5.0);
}
// d is never below a2 (nh at most 1), so the denominator never nears 0 for a real roughness; the floor only keeps a NaN out.
// With 1e-7 added to it, the peak of any surface smoother than a roughness of about 0.12 was capped (a2 / 1e-7 against
// 1 / (pi a2)): a glossy floor's, a puddle's, polished metal's and Reflection Boost's highlights of the sun and the lamps
// lost up to 99.9 % of their light, and glass 97.5 %.
float ggx_d(float nh, float a2) { float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / max(PI * d * d, 1e-30); }
float smith_g1(float nx, float a2) { return 2.0 * nx / (nx + sqrt(a2 + (1.0 - a2) * nx * nx) + 1e-7); }
float roughness_alpha(float perceptual) { float r = max(perceptual, 0.045); return r * r; }
// The directional albedo of the specular lobe (Karis' analytic fit of the split-sum lookup table).
float3 specular_albedo(float3 f0, float perceptual, float nv) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = perceptual * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * nv)) * r.x + r.y;
    float2 ab = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + ab.y;
}
// The albedo of what a surface's specular signal holds, for the denoisers' guides: its specular lobe's, and on see-through
// glass the light it lets through as well (pt_specular weights what lies behind by 1 - F and the glass's color).
float3 specular_guide_albedo(Surface s, float nv) {
    float3 a = specular_albedo(s.f0, s.roughness, nv);
    if (s.glass) a += (1.0 - fresnel_schlick(s.f0, nv)) * s.albedo;
    return a;
}
// Specular BRDF times cos(theta_l).
float3 ggx_specular(float3 n, float3 v, float3 l, float3 f0, float alpha) {
    float3 h = normalize(v + l);
    float nl = saturate(dot(n, l)), nv = saturate(dot(n, v)), nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    if (nl <= 0.0 || nv <= 0.0) return 0.0;
    float a2 = alpha * alpha;
    return fresnel_schlick(f0, vh) * ggx_d(nh, a2) * smith_g1(nl, a2) * smith_g1(nv, a2) / (4.0 * nv + 1e-6);
}
// Visible-normal sampling of GGX (Heitz 2018); returns the reflected direction.
float3 ggx_sample(float3 n, float3 v, float alpha, float2 u) {
    float3 t, b; basis(n, t, b);
    float3 vl = float3(dot(v, t), dot(v, b), dot(v, n));
    float3 vh = normalize(float3(alpha * vl.x, alpha * vl.y, vl.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    float3 t1 = lensq > 0.0 ? float3(-vh.y, vh.x, 0.0) * rsqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 t2 = cross(vh, t1);
    float r = sqrt(u.x), phi = 2.0 * PI * u.y;
    float p1 = r * cos(phi), p2 = r * sin(phi);
    float s = 0.5 * (1.0 + vh.z);
    p2 = (1.0 - s) * sqrt(1.0 - p1 * p1) + s * p2;
    float3 nh = p1 * t1 + p2 * t2 + sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
    float3 hl = normalize(float3(alpha * nh.x, alpha * nh.y, max(0.0, nh.z)));
    float3 h = hl.x * t + hl.y * b + hl.z * n;
    return reflect(-v, h);
}

// ---------------------------------------------------------------- camera
// HDRP's ComputeWorldSpacePosition with UNITY_UV_STARTS_AT_TOP: pixel centers, reversed-Z depth.
float3 camera_relative_position(uint2 pixel, float depth) {
    float2 uv = (float2(pixel) + 0.5) / float2(g.size.xy);
    float4 clip = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    float4 w = mul(g.invViewProj, clip);
    return w.xyz / w.w;
}
// The texture coordinate a camera-relative position projects to this frame, and whether it is on screen.
bool project(float3 cameraRelative, out float2 uv) {
    float4 clip = mul(g.viewProj, float4(cameraRelative, 1.0));
    if (clip.w <= 1e-4) { uv = 0; return false; }
    float2 ndc = clip.xy / clip.w;
    uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    return all(uv >= 0.0) && all(uv <= 1.0);
}
// Traced light is stored (signals, the built-in denoiser's history) relative to the reference radiance, which keeps
// it within half precision's range whatever units the game lights in (HDRP's sun: up to about 10^5 lux); the compose
// pass multiplies it back out.
float reference_radiance() { return g.radiance.x > 0.0 && g.radiance.x < 1e30 ? g.radiance.x : 1.0; }
float3 to_storage(float3 c) { return c / reference_radiance(); }
float clamp_reference() { return g.radiance.z > 0.0 && g.radiance.z < 1e30 ? g.radiance.z : reference_radiance(); }
// The traced light's own exposure on top of HDRP's (params.x, from TGPT_Frame.tracedGain; HdrpPasses adapts it).
float traced_gain() { return g.params.x > 0.0 && g.params.x < 1e6 ? g.params.x : 1.0; }
// HDRP's height fog between two points (relative to the scene origin), as optical depth: its extinction (fog.x, per metre)
// below its base height (fog.y, in the world), falling off as e^-((h - base) / H) above it (fog.z: 1 / H). 0 without fog.
// Over a segment the height changes evenly with the distance: the part of it below the base is homogeneous, and above it
// the density's mean over the heights it spans.
float height_fog_depth(float3 a, float3 b) {
    const float k = g.fog.x, base = g.fog.y, rcpH = g.fog.z;
    if (!(k > 0.0) || !(k < 1e6) || !(rcpH >= 0.0)) return 0.0;
    const float len = length(b - a);
    const float lo = min(a.y, b.y) + g.cacheOrigin.y, hi = max(a.y, b.y) + g.cacheOrigin.y;
    const float span = hi - lo;
    if (span < 1e-3) return k * len * (lo < base ? 1.0 : exp(-(lo - base) * rcpH));
    const float below = saturate((base - lo) / span);
    const float from = max(lo, base), d = (hi - from) * rcpH;
    const float mean = exp(-(from - base) * rcpH) * (d > 1e-4 ? (1.0 - exp(-d)) / d : 1.0);
    return k * len * (below + (1.0 - below) * mean);
}
// The traced light's gain at a surface: its automatic part (it lifts dark places towards a mid tone) fades as HDRP's fog hides
// the surface, the player's Exposure (fog.w: its factor) stays. HDRP lays the game's fog over the composite at the game's
// exposure, and through thick fog the gain lifted what the mist hides: a castle at night showed through it, where the game
// shows only mist.
float traced_gain_at(float3 position) {
    const float player = g.fog.w > 0.0 && g.fog.w < 1e6 ? g.fog.w : 1.0;
    const float automatic = traced_gain() / player;
    return player * (1.0 + (automatic - 1.0) * exp(-height_fog_depth(g.cameraPos.xyz, position)));
}
float exposure() {
    Texture2D<float> e = ResourceDescriptorHeap[g.inputs1.z];
    float v = e.Load(int3(0, 0, 0));
    return (v > 0.0 && v < 1e20) ? v : 1.0;
}

// ---------------------------------------------------------------- scene
float4 atlas_sample(int tile, float2 uv) {
    Texture2D<float4> atlas = ResourceDescriptorHeap[g.scene1.z];
    uint2 origin = uint2(uint(tile) % TILES_PER_ROW, uint(tile) / TILES_PER_ROW) * TILE_SIZE;
    float2 f = frac(uv);
    uint2 texel = min(uint2(f * TILE_SIZE), TILE_SIZE - 1);
    float4 c = atlas.Load(int3(origin + texel, 0));
    return float4(srgb_to_linear(c.rgb), c.a);
}
// An alpha-tested material's alpha at 256x256, filtered bilinearly within its tile (wrapping, as the texture repeats).
// Point-sampled, a fringe's or leaf's edge stepped from texel to texel as the shading points moved with the camera's
// jitter, and the shadows of rug fringes flickered. The micromap bake reads the same 2x2 footprint (pt_omm_bake).
float alpha_sample(int tile, float2 uv) {
    Texture2D<float> alphas = ResourceDescriptorHeap[g.extra.x];
    uint2 origin = uint2(uint(tile) % ALPHA_TILES_PER_ROW, uint(tile) / ALPHA_TILES_PER_ROW) * ALPHA_TILE_SIZE;
    float2 t = frac(uv) * ALPHA_TILE_SIZE - 0.5;
    float2 f = frac(t);
    int2 i0 = int2(floor(t));
    uint2 a = uint2(i0) & (ALPHA_TILE_SIZE - 1u), b = uint2(i0 + 1) & (ALPHA_TILE_SIZE - 1u);
    float a00 = alphas.Load(int3(origin + uint2(a.x, a.y), 0)), a10 = alphas.Load(int3(origin + uint2(b.x, a.y), 0));
    float a01 = alphas.Load(int3(origin + uint2(a.x, b.y), 0)), a11 = alphas.Load(int3(origin + uint2(b.x, b.y), 0));
    return lerp(lerp(a00, a10, f.x), lerp(a01, a11, f.x), f.y);
}
struct Hit {
    float3 position;
    float3 normal;     // geometric, world, towards the ray's origin
    float2 uv;
    float4 color;      // vertex color, white without
    uint material;
    uint instance;     // the TLAS instance (a TGPT_SetScene index, then characters)
    uint geometry;     // the submesh
    uint primitive;    // the triangle in it
    bool front;        // its front face met (the side its triangles face in the mesh's own space, as DXR's front face)
};
uint3 triangle_indices(MeshRecord mesh, uint geometry, uint primitive) {
    StructuredBuffer<GeometryRecord> geometries = ResourceDescriptorHeap[g.scene0.z];
    GeometryRecord geo = geometries[mesh.geometryBase + geometry];
    ByteAddressBuffer ib = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.indices)];
    return ib.Load3((geo.indexStart + primitive * 3) * 4);
}
float2 interpolate_uv(MeshRecord mesh, uint3 idx, float2 bary) {
    if (mesh.uvs == INVALID) return 0.0;
    ByteAddressBuffer ub = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs)];
    float2 t0 = asfloat(ub.Load2(idx.x * 8)), t1 = asfloat(ub.Load2(idx.y * 8)), t2 = asfloat(ub.Load2(idx.z * 8));
    return t0 * (1.0 - bary.x - bary.y) + t1 * bary.x + t2 * bary.y;
}
// Glass's color where a ray crosses it: its base color by its texture there.
float3 glass_color(InstanceRecord inst, MaterialRecord m, uint geometry, uint primitive, float2 bary) {
    float3 c = m.baseColor.rgb;
    if (m.albedoTile >= 0) {
        StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
        MeshRecord glassMesh = meshes[inst.mesh];
        float2 guv = interpolate_uv(glassMesh, triangle_indices(glassMesh, geometry, primitive), bary);
        c *= atlas_sample(m.albedoTile, guv * m.uvST.xy + m.uvST.zw).rgb;
    }
    return c;
}
// A ray's own chance at a triangle, for what stops it by chance (u uniform in [0, 1)): the same whenever the ray meets that
// triangle again (traversal may report it more than once), independent between triangles, of every submesh (a primitive's
// index is its submesh's own: without the submesh, two layers of hair with the same indices let a ray through both or neither,
// and a pair of half-covered layers cast the shadow of one), and between kinds of decision (salt).
float ray_chance(uint instanceId, uint geometry, uint primitive, uint salt) {
    return float(pcg(g_raySeed ^ pcg(primitive * 0x9E3779B9u + pcg(instanceId + geometry * 0x85EBCA6Bu) + salt)) >> 8) * (1.0 / 16777216.0);
}
// Alpha test of a candidate triangle: false means the ray passes through.
bool alpha_opaque(uint instanceId, uint geometry, uint primitive, float2 bary) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    InstanceRecord inst = instances[instanceId];
    MaterialRecord m = materials[materialIndices[inst.materialBase + geometry]];
    if ((m.flags & MATERIAL_ALPHA_TEST) == 0) return true;
    // Seen by camera rays only (eyes, unlit decals): every other ray passes through it.
    if ((m.flags & MATERIAL_CAMERA_ONLY) != 0 && !g_cameraRays) return false;
    // A camera ray stops at a surface HDRP shades itself wherever it meets one: the pixel is one HDRP drew (forward, at
    // the depth the ray looks around), and HDRP's own alpha test decided what covers it. By the tracer's, a camera ray
    // slipped between hair strands HDRP drew there to the scalp behind, and the traced skin replaced HDRP's hair from
    // frame to frame (far hair flickering grey and dark). Other rays keep the strands' own alpha.
    if ((m.flags & MATERIAL_HDRP_SHADED) != 0 && g_cameraRays) return true;
    // Glass lets a ray through by chance, as bright as its color is (each crossing the square root of its luminance: a
    // closed object's two, its luminance), whether it carries a shadow, the sky's or bounced light (as the light it
    // stops, the shadow of a bottle is as dark: sky rays it stopped every time left clear glass half a solid shadow).
    // The rest meet it, as camera rays always do. A shadow ray tinted by its color instead goes on (transmittance).
    if ((m.flags & MATERIAL_GLASS) != 0 && !g_cameraRays && !g_glassSolid) {
        if (g_glassTint) { ++g_glassMet; return false; }
        const float3 c = glass_color(inst, m, geometry, primitive, bary);
        const float passes = sqrt(saturate(dot(c, float3(0.2126, 0.7152, 0.0722))));
        return ray_chance(instanceId, geometry, primitive, 0u) >= passes;
    }
    // Without a texture the base color's alpha decides alone (a cutoff above 1 makes a submesh invisible to rays). An
    // alpha tile holds coverage for the material's cutoff (its base alpha included), which alphaCutoff is compared with.
    float a = m.baseColor.a;
    if (m.albedoTile >= 0) {
        MeshRecord mesh = meshes[inst.mesh];
        float2 uv = interpolate_uv(mesh, triangle_indices(mesh, geometry, primitive), bary);
        float2 st = uv * m.uvST.xy + m.uvST.zw;
        a = (m.flags & MATERIAL_ALPHA_TILE) ? alpha_sample(m.alphaTile, st) : a * atlas_sample(m.albedoTile, st).a;
    }
    // Hair stops a ray by chance, as much of the texel as its strands cover (a camera ray stopped above: HDRP shades hair),
    // so a layer of strands lets part of the light through between them. Cut out where a fifth of a texel was covered,
    // every card of hair cast a full, hard-edged shadow, as dark as a solid sheet.
    if ((m.flags & MATERIAL_STRANDS) != 0) return ray_chance(instanceId, geometry, primitive, 0x68616972u) < a;
    // Every alpha tile's coverage stops the rays other than the camera's by chance too, as much of the texel as is covered:
    // what is finer than the tile's texels (straw on a floor, a grass blade's tip, a chain's links, a fringe) lets the light
    // between its parts through. Cut out where half a texel was covered, the straw lying on a tavern's floor was a solid mat
    // to rays, and its shadow blacked out the floor seen between the strands. A camera ray keeps HDRP's cutoff: the surface
    // it finds must be the one HDRP drew.
    if ((m.flags & MATERIAL_ALPHA_TILE) != 0 && !g_cameraRays) return ray_chance(instanceId, geometry, primitive, 0x616C7068u) < a;
    return a >= m.alphaCutoff;
}
Hit fetch_hit(uint instanceId, uint geometry, uint primitive, float2 bary, float3x4 objectToWorld, float3 rayDir, float3 hitPos) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    InstanceRecord inst = instances[instanceId];
    MeshRecord mesh = meshes[inst.mesh];
    uint3 idx = triangle_indices(mesh, geometry, primitive);
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    float3 p0 = asfloat(pb.Load3(idx.x * 12)), p1 = asfloat(pb.Load3(idx.y * 12)), p2 = asfloat(pb.Load3(idx.z * 12));
    float3 w0 = mul(objectToWorld, float4(p0, 1.0)), w1 = mul(objectToWorld, float4(p1, 1.0)), w2 = mul(objectToWorld, float4(p2, 1.0));
    Hit h;
    h.instance = instanceId;
    h.geometry = geometry; h.primitive = primitive;
    h.position = hitPos;
    float3 n = cross(w1 - w0, w2 - w0);
    float len = length(n);
    n = len > 1e-12 ? n / len : -rayDir;
    // The front face is the side cross(v1 - v0, v2 - v0) faces in the mesh's own space (Unity's clockwise winding), which a
    // mirroring transform turns around in the world.
    const float3x3 linearPart = (float3x3)objectToWorld;
    h.front = dot(n, rayDir) * determinant(linearPart) < 0.0;
    h.normal = dot(n, rayDir) > 0.0 ? -n : n;
    h.uv = interpolate_uv(mesh, idx, bary);
    h.color = 1.0;
    if (mesh.colors != INVALID) {
        ByteAddressBuffer cb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.colors)];
        float4 c0 = unpack_rgba8(cb.Load(idx.x * 4)), c1 = unpack_rgba8(cb.Load(idx.y * 4)), c2 = unpack_rgba8(cb.Load(idx.z * 4));
        h.color = c0 * (1.0 - bary.x - bary.y) + c1 * bary.x + c2 * bary.y;
    }
    h.material = materialIndices[inst.materialBase + geometry];
    return h;
}
// A material's metallic and smoothness where a ray meets it (st: the base color map's coordinates, which HDRP Lit samples
// its mask map with): with a mask map (MATERIAL_MASK_TILE), the lerp of each one's ends (maskRemap, 8 bits each) by its
// texel's value, stored inverted (d3d11.hlsl TGPT_ATLAS: a tile not baked yet, white, reads as their low ends); without,
// the material's constants.
float2 metallic_smoothness(MaterialRecord m, float2 st) {
    if ((m.flags & MATERIAL_MASK_TILE) == 0) return float2(m.metallic, m.smoothness);
    Texture2D<float4> atlas = ResourceDescriptorHeap[g.scene1.z];
    uint2 origin = uint2(uint(m.maskTile) % TILES_PER_ROW, uint(m.maskTile) / TILES_PER_ROW) * TILE_SIZE;
    uint2 texel = min(uint2(frac(st) * TILE_SIZE), TILE_SIZE - 1);
    const float4 t = atlas.Load(int3(origin + texel, 0));
    const float4 ends = unpack_rgba8(m.maskRemap); // metallic low, high; smoothness low, high
    return float2(lerp(ends.x, ends.y, 1.0 - t.r), lerp(ends.z, ends.w, 1.0 - t.a));
}
// A material's emission at a point (st: its texture coordinates with the material's tiling applied), in nits, as HDRP's Lit
// computes it: the emissive color (with its intensity and exposure weight, from the plugin) times the emissive map, and times
// the albedo where Albedo Affects Emissive is on. Path vertices and the light sampling of emissive triangles both read it.
float3 material_emission(MaterialRecord m, float2 st) {
    float3 e = m.emissive;
    if (!any(e > 0.0)) return 0.0;
    if (m.emissiveTile >= 0) e *= atlas_sample(m.emissiveTile, st).rgb;
    if (m.flags & MATERIAL_EMISSIVE_ALBEDO) e *= m.albedoTile >= 0 ? m.baseColor.rgb * atlas_sample(m.albedoTile, st).rgb : m.baseColor.rgb;
    return e;
}
// How often a ray other than a camera ray meets the surface at a point, as alpha_opaque decides: its emission reaches as
// much of the world. By chance, as much as an alpha tile covers (hair's too), or cut out by its alpha test; glass by chance,
// as bright as its color is; seen by camera rays only, never.
float emission_coverage(MaterialRecord m, float2 st) {
    if ((m.flags & MATERIAL_ALPHA_TEST) == 0) return 1.0;
    if ((m.flags & MATERIAL_CAMERA_ONLY) != 0) return 0.0;
    if ((m.flags & MATERIAL_GLASS) != 0) {
        float3 c = m.baseColor.rgb;
        if (m.albedoTile >= 0) c *= atlas_sample(m.albedoTile, st).rgb;
        return 1.0 - sqrt(saturate(dot(c, float3(0.2126, 0.7152, 0.0722))));
    }
    float a = m.baseColor.a;
    if (m.albedoTile >= 0) a = (m.flags & MATERIAL_ALPHA_TILE) ? alpha_sample(m.alphaTile, st) : a * atlas_sample(m.albedoTile, st).a;
    if ((m.flags & (MATERIAL_ALPHA_TILE | MATERIAL_STRANDS)) != 0 && m.albedoTile >= 0) return saturate(a);
    return a >= m.alphaCutoff ? 1.0 : 0.0;
}
// albedo, f0 and roughness (perceptual) are the diffuse color and the finish of a surface the camera sees (pt_prepare takes
// them for one HDRP draws forward); reflectance is what a path vertex, which is shaded diffusely, sends on; through: the share
// it lets through from behind (direct_diffuse), a leaf's (TGPT_MATERIAL_TRANSLUCENT) or thin cloth's (TGPT_MATERIAL_THIN_CLOTH).
struct SurfaceMaterial { float3 albedo; float3 emissive; float3 f0; float roughness; float3 reflectance; float through; };
SurfaceMaterial hit_material(Hit h) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    MaterialRecord m = materials[h.material];
    SurfaceMaterial s;
    float3 base = m.baseColor.rgb;
    const float2 st = h.uv * m.uvST.xy + m.uvST.zw;
    if (m.albedoTile >= 0) base *= atlas_sample(m.albedoTile, st).rgb;
    if (m.flags & MATERIAL_VERTEX_COLOR) base *= h.color.rgb;
    // HDRP Lit's metallic workflow, as the game's GBuffer passes compute it: a metal's base color is its specular color, and
    // it has no diffuse color (diffuse baseColor * (1 - metallic), fresnel0 lerp(0.04, baseColor, metallic)). A path vertex
    // is shaded diffusely and can't follow a metal's reflection, so it sends the reflected light on as diffuse light of the
    // same energy: a metal's reflectance, its specular color (averaged over directions, about fresnel0), is its base color,
    // as a dielectric's diffuse color is. With its diffuse color alone a metal bounced no light and was black in a
    // reflection or behind glass (armour, pots, weapons); before mask maps, a metal kept a fifth of its color there.
    const float2 finish = saturate(metallic_smoothness(m, st));
    base = saturate(base);
    s.albedo = base * (1.0 - finish.x);
    s.f0 = lerp(0.04, base, finish.x);
    s.roughness = 1.0 - finish.y;
    s.reflectance = base;
    s.through = (m.flags & MATERIAL_TRANSLUCENT) != 0 ? TRANSMISSION : (m.flags & MATERIAL_THIN_CLOTH) != 0 ? CLOTH_TRANSMISSION : 0.0;
    // Emission from the front face only, unless the material is double-sided: HDRP culls a single-sided surface's back, so
    // nothing ever sees it glow from there.
    s.emissive = h.front || (m.flags & MATERIAL_DOUBLE_SIDED) != 0 ? material_emission(m, st) : 0.0;
    return s;
}

// ---------------------------------------------------------------- traversal
// A ray's own number for the glass it crosses: its origin, direction and the frame.
void ray_seed(float3 origin, float3 dir) {
    g_raySeed = pcg(asuint(origin.x) ^ pcg(asuint(origin.y) ^ pcg(asuint(origin.z) ^ pcg(asuint(dir.x) ^ pcg(asuint(dir.y) ^ pcg(g.size.z))))));
}
bool trace_closest(float3 origin, float3 dir, float tmin, float tmax, out Hit hit) {
    ray_seed(origin, dir);
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) { hit = (Hit)0; return false; }
    float t = q.CommittedRayT();
    hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                    q.CommittedObjectToWorld3x4(), dir, origin + dir * t);
    return true;
}
bool occluded_any(float3 origin, float3 dir, float tmin, float tmax) {
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}
bool occluded(float3 origin, float3 dir, float tmin, float tmax) {
    ray_seed(origin, dir);
    return occluded_any(origin, dir, tmin, tmax);
}
// HDRP Lit's transmission features (2, and 3 with subsurface scattering) through a diffusion profile the managed side
// marks solid (settings.x, a bit per profile: skin, thick objects), where transmission otherwise means a thin surface lit
// through from behind. The profile is in GBuffer2's blue channel with the transmission mask (HDRP's PackFloatInt8bit with
// 16 profiles), except for colored transmission (bit 3 of its alpha), whose blue holds the color: always thin.
bool solid_transmission(float4 gb2, uint feature) {
    if (feature != 2u && feature != 3u) return false;
    if ((uint(gb2.a * 255.0 + 0.5) & 8u) != 0) return false;
    uint profile = uint(gb2.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u;
    return ((g.settings.x >> profile) & 1u) != 0;
}
// HDRP's transmittance through skin (Lit's FillMaterialTransmission) for a GBuffer that solid_transmission accepts: its
// thickness in green, remapped to the profile's millimetres, the light Burley's profile lets through that far
// (DiffusionProfile's ComputeTransmittanceDisney: tint / 4 x (e^-St + 3 e^-St/3)), times the transmission mask packed
// with the profile in blue (PackFloatInt8bit).
float3 skin_transmittance(float4 gb2) {
    const uint profile = uint(gb2.b / (16.0 / 255.0) + 1.0 / 255.0) & 15u;
    const float mask = saturate((gb2.b - float(profile) * (16.0 / 255.0)) / (15.0 / 255.0));
    const float4 tint = g.transmission[profile * 2u], shape = g.transmission[profile * 2u + 1u];
    const float3 third = exp(-shape.rgb * (tint.w + shape.w * gb2.g) / 3.0);
    return max(tint.rgb, 0.0) * third * (third * third + 3.0) * mask;
}
// Rays leave a surface a little above it: raster and traced geometry can differ in LOD and precision.
float ray_offset(float distanceFromCamera) { return 0.02 + distanceFromCamera * 0.002; }

// Rays from HDRP's surface can start inside the traced mesh where the two differ by more than that offset: an
// arch drawn as a smooth curve over the flat triangles it is traced with (tessellation), displacement, a depth
// offset. The first thing such a ray meets is the inside of that mesh, a back face, close by, and every ray from
// there finds no light: black patches in the shape of the traced triangles. A back face that near is passed
// through, the ray going on from just beyond it. Farther away, or on an alpha-tested (two-sided) material, it is a
// real hit. The material decides, not the object: a character's hair or a building's ivy made every back face of
// the object a real hit, and on a face, where the sun's shadow ray leaves the smooth drawn surface into its flatter
// triangles at the terminator, the inside of the head stopped it (hard-edged shadows across faces). Front faces
// are the side a mesh's triangles face in its own space (Unity's clockwise winding, which DXR's default matches),
// so mirrored instances keep theirs. Only for rays from the G-buffer's surfaces: paths continue from traced hits,
// which lie on the traced mesh.
// A stand-in (a shadow-only mesh the game casts a building's shadows with, mostly the drawn geometry but coarser
// on curves: arches, vaults) is passed through within three times that distance whichever way it faces: the drawn
// surface can lie inside or behind it there. Farther away it shadows and bounces light like any mesh.
#define INSTANCE_STAND_IN 4
#define INSTANCE_CHARACTER 8
#define INSTANCE_SWAYS 32 // every material of it sways (MASK_SWAYS)
// What the wind moves (TGPT_MATERIAL_SWAYS: grass, leaves, a banner, laundry, as HDRP's shaders bend them) is traced where it
// stands still, and near a surface HDRP drew the two need not agree: a ray from a blade of grass met the blade's own still copy
// beside it, a strip of laundry the strip's (its inner side black), and the ground the drawn grass swayed away from lay in the
// still grass's shadow (black spots swaying with the grass). Rays from HDRP's surfaces meet an instance all of whose
// materials the wind moves (its own instance mask, scene.cpp) only from SWAY_REACH on; paths go on from traced hits, which
// lie on the still meshes, and see all of it.
#define MASK_STILL 0xFDu
#define MASK_SWAYS 0x02u
static const float SWAY_REACH = 0.2;
bool scene_sways() { return (uint(g.radiance.w + 0.5) & 1u) != 0; }
bool scene_cloth() { return (uint(g.radiance.w + 0.5) & 2u) != 0; } // any TGPT_MATERIAL_THIN_CLOTH in it
float near_back(float distanceFromCamera) { return 0.15 + distanceFromCamera * 0.01; }
bool passes_through(uint instanceId, uint geometry, bool frontFace, float t, float nearBack) {
    if (t >= nearBack * 3.0 || (g.settings2.y & OPTION_NO_PASS_THROUGH) != 0) return false;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    InstanceRecord inst = instances[instanceId];
    if (inst.flags & INSTANCE_STAND_IN) return true;
    if (frontFace || t >= nearBack) return false;
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    return (materials[materialIndices[inst.materialBase + geometry]].flags & MATERIAL_ALPHA_TEST) == 0;
}
bool trace_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack, out Hit hit) {
    ray_seed(origin, dir);
    hit = (Hit)0;
    // Everything first. Only where the closest hit is something the wind moves within SWAY_REACH (a blade of grass beside
    // the one drawn there, the grass over the ground) does the ray look again: for what stands still up to SWAY_REACH, and
    // then for everything from there. Rays elsewhere took two queries each (what stands still, then what sways).
    uint mask = 0xFFu;
    float t0 = tmin;
    for (uint pass = 0; pass < 5 && t0 < tmax; ++pass) {
        const float end = mask == MASK_STILL ? min(tmax, SWAY_REACH) : tmax;
        if (t0 >= end) { mask = 0xFFu; t0 = max(t0, SWAY_REACH); continue; }
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = end;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, mask, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            // Nothing that stands still before SWAY_REACH: everything from there.
            if (mask == MASK_STILL) { mask = 0xFFu; t0 = max(t0, min(tmax, SWAY_REACH)); continue; }
            break;
        }
        const float t = q.CommittedRayT();
        StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
        if (mask != MASK_STILL && t < SWAY_REACH && (instances[q.CommittedInstanceID()].flags & INSTANCE_SWAYS) != 0) { mask = MASK_STILL; continue; }
        if (pass < 4 && passes_through(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace(), t, nearBack)) { t0 = t + 1e-3; continue; }
        hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                        q.CommittedObjectToWorld3x4(), dir, origin + dir * t);
        return true;
    }
    return false;
}
// Near the surface, where a hit may be passed through, the nearest hit decides: with the first hit found accepted,
// a passable one found first moved the ray on past a nearer real blocker (a leak). Beyond that, any hit blocks.
bool occluded_from_surface_rays(float3 origin, float3 dir, float tmin, float tmax, float nearBack) {
    const float nearEnd = min(tmax, nearBack * 3.0);
    float t0 = tmin;
    // What the wind moves blocks from SWAY_REACH on (to nearEnd here, beyond it with everything else).
    const float s0 = max(tmin, SWAY_REACH);
    if (scene_sways() && s0 < nearEnd) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = s0; ray.TMax = nearEnd;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_SWAYS, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) return true;
    }
    for (uint pass = 0; pass < 3 && t0 < nearEnd; ++pass) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = nearEnd;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_STILL, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const float t = q.CommittedRayT();
        if (pass < 2 && passes_through(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace(), t, nearBack)) { t0 = t + 1e-3; continue; }
        return true;
    }
    if (nearEnd >= tmax) return false;
    RayDesc far; far.Origin = origin; far.Direction = dir; far.TMin = max(nearEnd, tmin); far.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) f;
    f.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, far);
    while (f.Proceed()) {
        if (f.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            alpha_opaque(f.CandidateInstanceID(), f.CandidateGeometryIndex(), f.CandidatePrimitiveIndex(), f.CandidateTriangleBarycentrics()))
            f.CommitNonOpaqueTriangleHit();
    }
    return f.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}
bool occluded_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack) {
    ray_seed(origin, dir);
    return occluded_from_surface_rays(origin, dir, tmin, tmax, nearBack);
}
// The light a shadow ray brings through the glass it crosses, channel by channel: each crossing lets through the square
// root of the glass's color there (a closed object's two crossings its color, as bright as the chance the other rays pass
// it by), so the sun through a green bottle reaches the table green, not just dimmer. The crossings are found in order
// along the ray, the nearest past the last each time, by a query that commits glass alone: the visibility test can't
// take the product itself, as it may report a triangle more than once (the geometry is built without
// NO_DUPLICATE_ANYHIT_INVOCATION) and its closest-hit part reports candidates beyond the hit it keeps. It met glass that
// many times at most (met), and the ray found nothing else in the way: only such rays pay for the walk.
float3 glass_walk(float3 origin, float3 dir, float tmin, float tmax, uint met) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    float3 through = 1.0;
    float t0 = tmin;
    for (uint crossing = 0; crossing < min(met, 8u) && t0 < tmax; ++crossing) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = tmax;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_CULL_OPAQUE) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
        while (q.Proceed()) {
            if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
            const InstanceRecord inst = instances[q.CandidateInstanceID()];
            if ((materials[materialIndices[inst.materialBase + q.CandidateGeometryIndex()]].flags & MATERIAL_GLASS) != 0) q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const InstanceRecord inst = instances[q.CommittedInstanceID()];
        const MaterialRecord m = materials[materialIndices[inst.materialBase + q.CommittedGeometryIndex()]];
        through *= sqrt(saturate(glass_color(inst, m, q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics())));
        const float t = q.CommittedRayT();
        t0 = t + max(1e-4, t * 1e-5);
    }
    return through;
}
// Final shading's shadow rays (a light sample's at a G-buffer surface, the sun's and a light's at path vertices): 0 where
// something stops the ray, otherwise the glass's tint along it (glass_walk). RTXDI's reuse keeps the binary test, with
// glass passed by chance.
// Each ray its own number (ray_seed), for what stops it by chance (hair's strands): left from the ray before, the sun's
// shadow ray took the decisions of the pixel's earlier rays (a half-covered hair card's shadow 0.6 as dark, not 0.5).
float3 transmittance(float3 origin, float3 dir, float tmin, float tmax) {
    ray_seed(origin, dir);
    g_glassTint = true; g_glassMet = 0;
    const bool blocked = occluded_any(origin, dir, tmin, tmax);
    g_glassTint = false;
    if (blocked) return 0.0;
    return g_glassMet > 0 ? glass_walk(origin, dir, tmin, tmax, g_glassMet) : 1.0;
}
float3 transmittance_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack) {
    ray_seed(origin, dir);
    g_glassTint = true; g_glassMet = 0;
    const bool blocked = occluded_from_surface_rays(origin, dir, tmin, tmax, nearBack);
    g_glassTint = false;
    if (blocked) return 0.0;
    return g_glassMet > 0 ? glass_walk(origin, dir, tmin, tmax, g_glassMet) : 1.0;
}
// Light reaching thin skin from behind (through_skin) comes through the skin itself: from just under the surface the ray
// towards the light must leave the skin within THIN_SKIN, through a back face of an opaque surface (the ear's far side,
// a finger's), and go on to the light unblocked. Through a head behind a face it meets no far side that close, and
// anything met first (a front face, hair) is in the way. A character the traced scene lacks lets nothing through.
static const float THIN_SKIN = 0.03;
bool transmitted_unoccluded(Surface s, float3 dir, float tmax) {
    // Just inside: a few float steps of the world positions characters are traced in (a step is 0.24 mm 2 to 4 km out).
    const float3 origin = s.position - s.geoNormal * (0.0002 + 1.5e-7 * (length(g.cacheOrigin.xyz) + length(s.position)));
    ray_seed(origin, dir);
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = 0.0; ray.TMax = min(THIN_SKIN, tmax);
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
            alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT || q.CommittedTriangleFrontFace()) return false;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    if ((materials[materialIndices[instances[q.CommittedInstanceID()].materialBase + q.CommittedGeometryIndex()]].flags & MATERIAL_ALPHA_TEST) != 0) return false;
    const float t = q.CommittedRayT() + 1e-3;
    return t >= tmax || !occluded_any(origin, dir, t, tmax);
}
// A direction chosen around a normal-mapped normal can point into the surface itself: mirrored back out.
float3 above_surface(float3 dir, float3 geoNormal) { float d = dot(dir, geoNormal); return d < 0.0 ? dir - 2.0 * d * geoNormal : dir; }

// ---------------------------------------------------------------- lights and sky
float3 sky_radiance(float3 dir) {
    Texture2D<float4> sky = ResourceDescriptorHeap[g.inputs1.w];
    float u = atan2(dir.x, dir.z) * (0.5 / PI) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    return sky.SampleLevel(s_linearWrap, float2(u, v), 0).rgb * g.params.z;
}
float smooth_window(float d2, float scale, float bias) { float f = d2 * scale; float s = saturate(bias - f * f); return s * s; }
// HDRP's cookie: a mask on a light's intensity by direction, in the light's own space (x right, y up, z forward): a
// point light's cubemap (baked as an octahedral map), a spot's texture across its cone. The game's candle and torch
// lights carry them (PointLight_Cookies): without, the table right under a candlestick got its full light.
float3 light_cookie(LightRecord l, float3 fromLight) {
    uint cookie = l.flags & 0xFFFFu;
    if (cookie == 0u || g.signals.x == INVALID) return 1.0;
    float3 right = cross(l.up, l.direction); // Unity's left-handed axes: right = up x forward
    float3 ls = float3(dot(fromLight, right), dot(fromLight, l.up), dot(fromLight, l.direction));
    float2 uv;
    if (l.type == 1u) {
        if (ls.z <= 1e-4) return 0.0;
        uv = saturate(ls.xy / ls.z * l.cookieScale * 0.5 + 0.5);
    } else {
        float3 n = ls / max(abs(ls.x) + abs(ls.y) + abs(ls.z), 1e-6);
        float2 e = n.z >= 0.0 ? n.xy : oct_wrap(n.xy);
        uv = saturate(e * 0.5 + 0.5);
    }
    Texture2D<float4> cookies = ResourceDescriptorHeap[g.signals.x];
    uint index = cookie - 1u;
    int2 origin = int2(index % COOKIE_TILES_PER_ROW, index / COOKIE_TILES_PER_ROW) * int(COOKIE_TILE_SIZE);
    float2 t = uv * COOKIE_TILE_SIZE - 0.5;
    float2 f = frac(t);
    int2 i0 = int2(floor(t));
    int2 a = clamp(i0, 0, int(COOKIE_TILE_SIZE) - 1), b = clamp(i0 + 1, 0, int(COOKIE_TILE_SIZE) - 1);
    float3 c00 = cookies.Load(int3(origin + int2(a.x, a.y), 0)).rgb, c10 = cookies.Load(int3(origin + int2(b.x, a.y), 0)).rgb;
    float3 c01 = cookies.Load(int3(origin + int2(a.x, b.y), 0)).rgb, c11 = cookies.Load(int3(origin + int2(b.x, b.y), 0)).rgb;
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}
// Light arriving at position from light: irradiance on a surface facing it, and the direction and distance to it.
float3 light_incident(LightRecord l, float3 position, out float3 dir, out float dist) {
    if (l.type == 2) { dir = -l.direction; dist = 1e7; return l.color; }
    float3 d = l.position - position;
    float d2 = max(dot(d, d), 1e-4);
    dist = sqrt(d2); dir = d / dist;
    float atten = smooth_window(d2, 1.0 / max(l.range * l.range, 1e-4), 1.0) / max(d2, l.radius * l.radius + 1e-4);
    if (l.type == 1) { float s = saturate(dot(-dir, l.direction) * l.angleScale + l.angleOffset); atten *= s * s; }
    return l.color * atten * light_cookie(l, -dir);
}
// The traced scene's emissive triangles as lights (scene.cpp build_emitters, pt_emissive.hlsl): light indices from
// EMITTER_BASE on are records of the emitter buffer. First each emissive submesh's triangles, in slots that keep their place
// from frame to frame (a slot holding the same triangle, by its identity, is the same light for ReSTIR's temporal reuse, as
// a light's slot is), then the clusters of up to 32 of them the light grid lists. A triangle's record: its first corner
// (position) and two edges (direction, up) wound so that their cross product faces its front; its reach (range), past which
// its light is left to the paths that meet it; within its cluster, its cumulative share of the cluster's light (color.x), and
// its area (color.y); its material (radius's bits), its corners' texture coordinates with the material's tiling as halves
// (angleScale, angleOffset, cookieScale); its identity (id, 0 for an empty slot); what it is (flags, EMITTER_*). A cluster's
// (LIGHT_CLUSTER): its first slot (flags) and its triangles (cookieScale's bits).
static const uint EMITTER_BASE = 1u << 20;
static const uint CLUSTER_TRIANGLES = 32; // scene.hpp kClusterTriangles: a submesh's triangles in clusters of this many
#define LIGHT_CLUSTER 3u
#define LIGHT_TRIANGLE 4u
#define EMITTER_SAMPLED 1u   // sampled as a light: a path that meets it counts what the sampling leaves (emission_weight)
#define EMITTER_PAIRED 2u    // a kept light's own surface (a lantern's glass round its candle's light): it lights nothing itself
#define EMITTER_TWO_SIDED 4u // emits from both faces (a double-sided material)
LightRecord load_emitter(uint slot, bool previousFrame) {
    StructuredBuffer<LightRecord> records = ResourceDescriptorHeap[previousFrame ? g.emissive.y : g.emissive.x];
    LightRecord t = records[slot];
    // The previous frame's records are relative to its origin (when the origin moves, a build writes this frame's into the
    // other buffer): moved to this frame's, as the previous frame's surfaces and lights are.
    if (previousFrame) t.position += g.prevOriginDelta.xyz;
    return t;
}
// Whether anything is sampled as a light this frame: the lights, or emissive clusters in the light grid.
bool frame_has_lights() { return g.settings2.z > 0 || g.emissive2.z > 0; }
LightRecord load_light(uint i) {
    if (i >= EMITTER_BASE) return load_emitter(i - EMITTER_BASE, false);
    StructuredBuffer<LightRecord> lights = ResourceDescriptorHeap[g.scene1.y]; return lights[i];
}
// A light see-through glass reflects as the sphere it is traced as (pt_specular reflected_lamps), whose highlight on glass
// the direct light's samples then leave out (pt_di_spatial): a point light's or a spot's.
#define LIGHT_NO_SPECULAR 0x20000u // TGPT_LIGHT_NO_SPECULAR: HDRP's light makes no highlight
bool reflected_as_sphere(LightRecord l) { return l.type <= 1u && l.radius > 0.0 && (l.flags & LIGHT_NO_SPECULAR) == 0u; }
// The previous frame's light in the same slot, its position relative to this frame's origin (none: count 0).
LightRecord load_previous_light(uint i) {
    if (i >= EMITTER_BASE) return load_emitter(i - EMITTER_BASE, true);
    StructuredBuffer<LightRecord> lights = ResourceDescriptorHeap[g.extra.w]; return lights[i];
}
float2 unpack_st(float packed) { const uint u = asuint(packed); return float2(f16tof32(u & 0xFFFF), f16tof32(u >> 16)); }
// A point of an emissive triangle (u uniform over the unit square: uniform over its area) and its texture coordinates.
float3 triangle_point(LightRecord t, float2 u, out float2 st) {
    const float su = sqrt(u.x);
    const float b1 = su * (1.0 - u.y), b2 = su * u.y;
    const float2 st0 = unpack_st(t.angleScale), st1 = unpack_st(t.angleOffset), st2 = unpack_st(t.cookieScale);
    st = st0 + (st1 - st0) * b1 + (st2 - st0) * b2;
    return t.position + t.direction * b1 + t.up * b2;
}
// The light arriving at position from a point of an emissive triangle (u picks it, uniformly over its area), times the
// triangle's area: uniform sampling's estimate of the triangle's irradiance on a surface facing it, as light_incident gives a
// point light's. Its emission there (texture and coverage, as a path meeting it there sees), the cosine it leaves the
// triangle at (its front only unless two-sided), and its reach's window (emission_weight gives paths the rest). y: the point.
float3 triangle_incident(LightRecord t, float3 position, float2 u, out float3 dir, out float dist, out float3 y) {
    float2 st;
    y = triangle_point(t, u, st);
    const float3 d = y - position;
    const float d2 = max(dot(d, d), 1e-8);
    dist = sqrt(d2); dir = d / dist;
    if ((t.flags & (EMITTER_SAMPLED | EMITTER_PAIRED)) != EMITTER_SAMPLED) return 0.0;
    const float3 n = cross(t.direction, t.up);
    const float twiceArea = length(n);
    float c = -dot(n, dir) / max(twiceArea, 1e-20);
    if ((t.flags & EMITTER_TWO_SIDED) != 0) c = abs(c);
    const float w = smooth_window(d2, 1.0 / max(t.range * t.range, 1e-8), 1.0);
    if (c <= 0.0 || w <= 0.0) return 0.0;
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const MaterialRecord m = materials[asuint(t.radius)];
    return material_emission(m, st) * (emission_coverage(m, st) * c * w * 0.5 * twiceArea / max(d2, 1e-4));
}
// A cell candidate that is a cluster of emissive triangles (cell_candidate's): one of its triangles, drawn by its share of
// the cluster's light (their cumulative shares), as the light index returned; invPdf is divided by that share.
uint cluster_triangle(LightRecord cluster, float u, inout float invPdf) {
    const uint first = cluster.flags, count = max(asuint(cluster.cookieScale), 1u);
    uint lo = 0, hi = count - 1;
    while (lo < hi) { uint mid = (lo + hi) >> 1; if (load_emitter(first + mid, false).color.x < u) lo = mid + 1; else hi = mid; }
    const float p = load_emitter(first + lo, false).color.x - (lo > 0 ? load_emitter(first + lo - 1, false).color.x : 0.0);
    invPdf = p > 0.0 ? invPdf / p : 0.0;
    return EMITTER_BASE + first + lo;
}
// A shadow ray to a point of an emissive triangle, ending just short of it (the triangle itself would stop it).
float3 triangle_shadow_ray(float3 origin, float3 y, out float tmax) {
    const float3 v = y - origin;
    const float len = max(length(v), 1e-4);
    tmax = max(len - max(1e-3 * len, 2e-3), 0.0);
    return v / len;
}

// A shadow ray towards a random point of the light's disc as seen from origin, the size HDRP gives the light
// (its Radius, 2.5 cm unless an artist changed it; the plugin sends at least 2 cm): a candle or a torch casts a
// crisp shadow that softens with distance from what casts it, a large light a soft one. (A 10 cm minimum here
// blurred every lamp's shadow; the several lights of a fire or a chandelier each cast theirs, as real flames do.)
float3 shadow_ray(LightRecord light, float3 origin, float2 u, out float tmax) {
    float3 c = light.position - origin;
    float3 n = c * rsqrt(max(dot(c, c), 1e-8));
    float3 t, b; basis(n, t, b);
    float2 disk = disk_sample(u) * light.radius;
    float3 v = c + t * disk.x + b * disk.y;
    float dist = length(v);
    tmax = max(dist - 0.05, 0.0); // the lamp's own mesh around the light does not block it
    return v / max(dist, 1e-4);
}

// The lights whose range reaches a position: its light grid cell's list. Outside the grid, none.
bool light_cell(float3 position, out uint first, out uint count) {
    first = 0; count = 0;
    if (g.grid.x == INVALID || !frame_has_lights()) return false;
    int3 dims = int3(g.grid.z & 0xFF, (g.grid.z >> 8) & 0xFF, (g.grid.z >> 16) & 0xFF);
    int3 c = int3(floor((position - g.gridMin.xyz) * g.gridMin.w));
    if (any(c < 0) || any(c >= dims)) return false;
    StructuredBuffer<uint2> cells = ResourceDescriptorHeap[g.grid.x];
    uint2 cell = cells[(c.z * dims.y + c.y) * dims.x + c.x];
    first = cell.x; count = cell.y;
    return count > 0;
}
uint cell_light(uint first, uint count, inout Rng rng) {
    StructuredBuffer<uint2> entries = ResourceDescriptorHeap[g.grid.y];
    return entries[first + min(uint(rng.next() * count), count - 1)].x;
}
// Candidate c of n from a cell of count lights, and one over the probability it was drawn with. With no more lights
// than candidates, every light once (as if each were drawn with probability 1 / count). With more, the n are drawn
// systematically along the cell's cumulative weights from one random u (each light's luminance over its squared
// distance from the cell: scene.cpp build_light_grid), so the lights that can matter here come up about as often as
// they do: drawn evenly, a room's chandelier came up as rarely as the faintest candle glow among 40 lights, and its
// light was noise the denoiser smoothed away. Weighted by one over the light's own probability, the candidates are
// an unbiased estimate as before; drawn independently, the same light came up several times while others were missed.
uint cell_candidate(uint first, uint count, uint n, uint c, float u, out float invPdf) {
    StructuredBuffer<uint2> entries = ResourceDescriptorHeap[g.grid.y];
    if (count <= n) { invPdf = float(count); return entries[first + (min(uint(u * count), count - 1) + c) % count].x; }
    float t = (float(c) + u) / float(n);
    uint lo = 0, hi = count - 1;
    while (lo < hi) { uint mid = (lo + hi) >> 1; if (asfloat(entries[first + mid].y) < t) lo = mid + 1; else hi = mid; }
    float p = asfloat(entries[first + lo].y) - (lo > 0 ? asfloat(entries[first + lo - 1].y) : 0.0);
    invPdf = p > 0.0 ? 1.0 / p : 0.0;
    return entries[first + lo].x;
}

// Next-event estimation at a diffuse path vertex: the sun, and one of the lights that reach it, each with
// a shadow ray (tinted by the glass it crosses). Up to eight of the cell's lights compete by their unshadowed irradiance
// (resampled importance sampling), so a light next to the vertex is chosen as often as it matters. An emissive cluster
// among them competes with one of its triangles and a point of it, whose shadow ray goes there. A translucent vertex (a
// blade of grass, a leaf: TGPT_MATERIAL_TRANSLUCENT) is lit from behind too, as the camera's translucent pixels are
// (translucent_cos), its shadow rays leaving from the side the light is on: lit from its own side alone, the underside of
// every sunlit blade was black to the rays that met it, and the ground under grass took no light through the grass.
float3 direct_diffuse(float3 position, float3 n, float3 albedo, float offset, inout Rng rng, float through) {
    float3 result = 0.0;
    if (g.sunColor.w > 0.0) {
        float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
        float nl = translucent_cos(n, l, through);
        if (nl > 0.0) result += g.sunColor.rgb * nl * transmittance(position + n * (dot(n, l) >= 0.0 ? offset : -offset), l, 0.0, g.params.y);
    }
    uint first, count;
    if (light_cell(position, first, count)) {
        const uint candidates = min(count, 8u);
        const float u = rng.next();
        float wsum = 0.0, chosenTarget = 0.0;
        float3 chosenE = 0.0, chosenL = 0.0, chosenPoint = 0.0;
        LightRecord chosen = (LightRecord)0;
        for (uint c = 0; c < candidates; ++c) {
            float invPdf;
            LightRecord light = load_light(cell_candidate(first, count, candidates, c, u, invPdf));
            float3 l, y; float dist;
            float3 e;
            if (light.type == LIGHT_CLUSTER) {
                light = load_light(cluster_triangle(light, rng.next(), invPdf));
                e = triangle_incident(light, position, rng.next2(), l, dist, y);
            } else {
                e = light_incident(light, position, l, dist); y = light.position;
                // A path vertex a few centimetres from a flame (a candle's own holder, the wick's surroundings) takes its
                // light as if 25 cm away at the nearest: I / d^2 there made rare, very bright bounce samples, sparkles that
                // the boiling filter can't catch among dark neighbours. The table under a candle still bounces what it
                // gets (up to 16 times the light at 1 m).
                float near = max(dist * dist, light.radius * light.radius + 1e-4);
                if (light.type != 2u && near < 0.0625 && (g.settings2.y & OPTION_NO_CLAMPS) == 0) e *= near / 0.0625;
            }
            float target = luminance(e) * translucent_cos(n, l, through);
            float w = target * invPdf;
            wsum += w;
            if (w > 0.0 && rng.next() * wsum < w) { chosenTarget = target; chosenE = e; chosenL = l; chosen = light; chosenPoint = y; }
        }
        if (chosenTarget > 0.0) {
            float3 origin = position + n * (dot(n, chosenL) >= 0.0 ? offset : -offset);
            float tmax;
            float3 dir;
            if (chosen.type == LIGHT_TRIANGLE) dir = triangle_shadow_ray(origin, chosenPoint, tmax);
            else { dir = shadow_ray(chosen, origin, rng.next2(), tmax); tmax = max(tmax - offset, 0.0); }
            result += chosenE * translucent_cos(n, chosenL, through) * (wsum / (float(candidates) * chosenTarget)) * transmittance(origin, dir, 0.0, tmax);
        }
    }
    return result * albedo / PI;
}
float3 direct_diffuse(float3 position, float3 n, float3 albedo, float offset, inout Rng rng) { return direct_diffuse(position, n, albedo, offset, rng, 0.0); }
// ---------------------------------------------------------------- radiance cache
#include "pt_restir.hlsli" // the radiance clamps its samples take
// A world-space hash grid of the bounced light arriving at surfaces: per cell, the mean radiance that cosine-distributed
// rays from its surfaces bring back (irradiance / pi, which a diffuse surface of albedo a sends on times a). Paths end on
// it: the last vertex a path shades takes its cell's light where it had none, so a path of the Bounces setting's length,
// a reflection (one vertex) and what glass shows carry the light of every bounce after theirs (a reflected wall lit only
// by bounced light was black). It is filled where it is read: the paths a pixel feeds it with (a quarter of them) trace one more
// ray from their last vertex for that vertex's cell, a ray that ends on the cache in turn, so light bounces on through it
// from frame to frame; and the bounced light's first rays from HDRP's surfaces count for theirs. Cells are the world's
// (the scene origin moves), CACHE_CELL across up to 5 m from the camera and twice as large with each doubling of the
// distance beyond, one per side of a surface (by the axis its normal is nearest); pt_cache.hlsl resolves them each frame.
static const uint CACHE_SLOTS = 1u << 20;          // tracer.cpp kCacheSlots
static const uint CACHE_BUCKET = 8u;               // the slots a cell may take, from its bucket's first
static const float CACHE_SCALE = 4096.0;           // the fixed point of a frame's sums, in storage units (to_storage)
static const uint CACHE_SAMPLES_PER_FRAME = 512u;  // a cell's samples a frame at most
static const float CACHE_SAMPLE_MOST = 2000.0;     // a sample's channel at most, in storage units: 512 of them fit 32 bits
static const float CACHE_CELL = 0.1;               // the finest cells, in metres
static const float CACHE_PER_METRE = 0.02;         // a cell's size for each metre from the camera, beyond the finest
static const float CACHE_FRAMES = 16.0;            // the longest a cell's light is averaged over, in frames
static const uint CACHE_MAX_AGE = 240u;            // frames without a sample before a cell is given up
bool cache_on() { return g.cache.w != 0u && g.cache.x != INVALID; }
// Whether a pixel's paths feed the cache this frame: a quarter of the screen's 8x8 tiles, another quarter each frame
// (whole tiles, so that the threads of a wave take the extra ray together or not at all). salt: a pass's own quarter.
bool cache_feeds(uint2 p, uint salt) { return cache_on() && ((pcg((p.x >> 3) + (p.y >> 3) * 4096u) + g.size.z + salt) & 3u) == 0u; }
uint cache_level(float3 position) {
    const float d = length(position - g.cameraPos.xyz);
    return uint(clamp(floor(log2(max(d * CACHE_PER_METRE, CACHE_CELL) / CACHE_CELL) + 0.5), 0.0, 15.0));
}
// The first slot of a cell's bucket (in bytes, per 4-byte key), and the key the slot holding it carries (never 0, an
// empty slot's).
uint2 cache_key(float3 position, float3 normal, uint level) {
    const int3 c = int3(floor((position + g.cacheOrigin.xyz) / (CACHE_CELL * exp2(float(level)))));
    const float3 a = abs(normal);
    const uint side = (a.x >= a.y && a.x >= a.z) ? (normal.x < 0.0 ? 1u : 0u) : (a.y >= a.z ? (normal.y < 0.0 ? 3u : 2u) : (normal.z < 0.0 ? 5u : 4u));
    const uint h = pcg(asuint(c.x) ^ pcg(asuint(c.y) ^ pcg(asuint(c.z) ^ pcg(level | (side << 4)))));
    return uint2((h % (CACHE_SLOTS / CACHE_BUCKET)) * CACHE_BUCKET, pcg(h ^ 0x6A09E667u) | 1u);
}
// The light the cache holds for a surface point's cell, 0 where it holds none yet. The point is moved at random across
// a cell's width within the surface's plane first: cells' edges become noise the denoisers take out, not steps.
float3 cache_light(float3 position, float3 normal, inout Rng rng) {
    const uint level = cache_level(position);
    float3 t, b; basis(normal, t, b);
    const float2 u = rng.next2() - 0.5;
    const uint2 key = cache_key(position + (t * u.x + b * u.y) * (CACHE_CELL * exp2(float(level))), normal, level);
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer light = ResourceDescriptorHeap[g.cache.z];
    for (uint i = 0; i < CACHE_BUCKET; ++i) {
        if (keys.Load((key.x + i) * 4u) != key.y) continue;
        const uint4 v = light.Load4((key.x + i) * 16u);
        return (v.w & 0xFFFFu) != 0u ? asfloat(v.xyz) : 0.0;
    }
    return 0.0;
}
// One sample of the light arriving at a surface point (along a cosine-distributed ray) into this frame's sums of its
// cell, which it takes a slot for if it has none. A full bucket drops it.
// Each channel is bounded on its own: the radiance clamps bound a sample's luminance, and a saturated colour within them (a
// blue is up to 14 times its luminance) summed past 32 bits, wrapping its cell's light to a sliver of it.
void cache_add(float3 position, float3 normal, float3 radiance, inout Rng rng) {
    float3 v = to_storage(radiance);
    if (!all(and(v >= 0.0, v < 1e30))) return; // NaN, infinite or negative
    v = min(v, CACHE_SAMPLE_MOST) * CACHE_SCALE;
    const uint2 key = cache_key(position, normal, cache_level(position));
    RWByteAddressBuffer keys = ResourceDescriptorHeap[g.cache.x];
    RWByteAddressBuffer sums = ResourceDescriptorHeap[g.cache.y];
    uint slot = INVALID;
    for (uint i = 0; i < CACHE_BUCKET && slot == INVALID; ++i) if (keys.Load((key.x + i) * 4u) == key.y) slot = key.x + i;
    for (uint j = 0; j < CACHE_BUCKET && slot == INVALID; ++j) {
        uint prior;
        keys.InterlockedCompareExchange((key.x + j) * 4u, 0u, key.y, prior);
        if (prior == 0u || prior == key.y) slot = key.x + j;
    }
    if (slot == INVALID) return;
    uint count;
    sums.InterlockedAdd(slot * 16u + 12u, 1u, count);
    if (count >= CACHE_SAMPLES_PER_FRAME) return;
    const uint3 q = uint3(v + rng.next()); // rounded at random: unbiased however small the light
    sums.InterlockedAdd(slot * 16u, q.x); sums.InterlockedAdd(slot * 16u + 4u, q.y); sums.InterlockedAdd(slot * 16u + 8u, q.z);
}
// Where the path shade_path follows was sampled from (a vertex's shading position), and whether its first ray gathers light
// for a diffuse surface there: set by the pass that calls it. pt_gi's rays do (the camera's surface, whose ReSTIR DI samples
// the emissive lights); a reflection's or a view through glass does not (ReSTIR DI gives no emissive triangle's specular
// light, and nothing samples what glass lets through). Each later ray of a path gathers for the vertex it leaves, whose light
// sampling (direct_diffuse) takes the emissive lights in.
static float3 g_pathFrom = 0.0;
static bool g_pathGathers = false;
// The share of its emission a path counts where it meets an emissive triangle, having come from `from`. A triangle sampled as
// a light was taken in there by the light sampling as far as its reach's window, 1 at the triangle falling to 0 at its reach
// (triangle_incident): a ray that gathers for a diffuse surface counts the rest, so the two count each point's light exactly
// once between them, unbiased whatever the reach, and the image converges to the same light with the Emissive Lights row on
// or off (only the noise differs). A kept light's own surface (EMITTER_PAIRED) lights nothing, its light being the light's,
// but is seen: in a reflection, through glass. The rest counts whole, as does everything seen from a vertex outside the light
// grid (nothing is sampled there). Whatever gathers a path's light at its vertices (the radiance cache) weighs it the same.
// The record of the emissive triangle a ray met, if it is one of the emitters' (the instance table: an instance's entry, then
// its submeshes' first slots).
bool emitter_record(Hit hit, out LightRecord t) {
    t = (LightRecord)0;
    if (g.emissive2.x == INVALID || hit.instance >= g.emissive2.y) return false;
    StructuredBuffer<uint> table = ResourceDescriptorHeap[g.emissive2.x];
    const uint entry = table[hit.instance];
    if (entry == INVALID) return false;
    const uint slot = table[entry + hit.geometry];
    if (slot == INVALID) return false;
    t = load_emitter(slot + hit.primitive, false);
    return true;
}
float emission_weight(Hit hit, float3 from, bool gathers) {
    LightRecord t;
    if (!gathers || !emitter_record(hit, t)) return 1.0;
    if ((t.flags & EMITTER_PAIRED) != 0) return 0.0;
    uint cellFirst, cellCount;
    if ((t.flags & EMITTER_SAMPLED) == 0 || !light_cell(from, cellFirst, cellCount)) return 1.0;
    const float3 d = hit.position - from;
    return 1.0 - smooth_window(dot(d, d), 1.0 / max(t.range * t.range, 1e-8), 1.0);
}
float3 path_emission(Hit hit, float3 emissive, float3 from, bool gathers) {
    return any(emissive > 0.0) ? emissive * emission_weight(hit, from, gathers) : 0.0;
}
// What a ray from a path's last vertex brings back to it for the cache: the next surface's own light (its emission as a
// ray gathering for that vertex counts it: the vertex's light sampling takes the emissive lights in) and, past it, the
// cache's (or the sky's).
float3 cache_sample(Hit hit, float offset, inout Rng rng) {
    const float3 dir = cosine_sample(hit.normal, rng.next2());
    Hit next;
    if (!trace_closest(hit.position + hit.normal * offset, dir, 0.0, g.params.y, next)) return sky_radiance(dir);
    SurfaceMaterial m = hit_material(next);
    const float nextOffset = 0.01 + length(next.position - g.cameraPos.xyz) * 0.001;
    return path_emission(next, m.emissive, hit.position, true) + direct_diffuse(next.position, next.normal, m.reflectance, nextOffset, rng, m.through) +
           m.reflectance * cache_light(next.position, next.normal, rng);
}

// Radiance leaving a path vertex hit from direction dir, following the path diffusely for more bounces and ending on the
// radiance cache (feed: this path adds to it). Its emission as path_emission weighs it for the vertex each ray left
// (g_pathFrom and g_pathGathers for the first).
float3 shade_path(Hit hit, float3 dir, uint bounces, inout Rng rng, bool feed) {
    float3 throughput = 1.0, radiance = 0.0;
    float3 from = g_pathFrom; bool gathers = g_pathGathers;
    for (uint bounce = 0; bounce < 4; ++bounce) {
        SurfaceMaterial m = hit_material(hit);
        float dist = length(hit.position - g.cameraPos.xyz);
        float offset = 0.01 + dist * 0.001;
        radiance += throughput * (path_emission(hit, m.emissive, from, gathers) + direct_diffuse(hit.position, hit.normal, m.reflectance, offset, rng, m.through));
        if (bounce + 1 >= bounces) {
            if (cache_on()) {
                radiance += throughput * m.reflectance * cache_light(hit.position, hit.normal, rng);
                if (feed) cache_add(hit.position, hit.normal, clamp_radiance(cache_sample(hit, offset, rng), LIMIT_INDIRECT), rng);
            }
            break;
        }
        throughput *= m.reflectance;
        if (bounce >= 1) {
            float p = saturate(max(throughput.r, max(throughput.g, throughput.b)));
            if (rng.next() >= p) break;
            throughput /= max(p, 1e-3);
        }
        from = hit.position; gathers = true;
        float3 next = cosine_sample(hit.normal, rng.next2());
        Hit h2;
        if (!trace_closest(hit.position + hit.normal * offset, next, 0.0, g.params.y, h2)) { radiance += throughput * sky_radiance(next); break; }
        hit = h2; dir = next;
    }
    return radiance;
}
float3 shade_path(Hit hit, float3 dir, uint bounces, inout Rng rng) { return shade_path(hit, dir, bounces, rng, false); }
