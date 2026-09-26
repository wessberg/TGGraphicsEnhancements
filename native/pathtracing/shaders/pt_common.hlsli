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

#define VIEW_NONE 0
#define VIEW_ALBEDO 1
#define VIEW_NORMALS 2
#define VIEW_COVERAGE 3
#define VIEW_MATERIALS 4
#define VIEW_INDIRECT 5
#define VIEW_INDIRECT_RAW 6
#define VIEW_DIRECT 7
#define OPTION_NO_SPATIAL 1
#define OPTION_NO_TEMPORAL 2
#define OPTION_NO_DENOISER 4
#define OPTION_NO_SPECULAR 8
#define OPTION_RAY_RECONSTRUCTION 16
#define OPTION_RAY_REGENERATION 64
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

// Kept in step with FrameConstants in tracer.cpp.
struct FrameConstants {
    float4x4 invViewProj;   // clip to camera-relative world (HDRP's, jittered)
    float4x4 viewProj;      // camera-relative world to clip
    float4x4 prevViewProj;  // previous camera-relative world to clip
    float4 cameraPos;       // xyz: camera relative to the scene origin; w: Reflection Boost (0 off)
    float4 prevCameraPos;   // xyz: previous camera relative to the current scene origin
    float4 prevOriginDelta; // xyz: previous origin - current origin; w: Indirect Color (1 as traced)
    float4 sunDir;          // xyz towards the sun; w: cos of its angular radius
    float4 sunColor;        // rgb: lux; w: 1 with a sun
    uint4 size;             // width, height, frame index, flags
    uint4 settings;         // unused, debug view, bounces, spatial samples
    uint4 settings2;        // temporal cap, options, light count, a-trous iterations
    float4 params;          // clamp exposure (0: HDRP's current), max ray distance, sky intensity, spatial radius (pixels)
    uint4 inputs0;          // SRVs: albedo+deferred flag, normal buffer, GBuffer2, GBuffer3
    uint4 inputs1;          // SRVs: depth, motion, exposure, sky
    uint4 scene0;           // SRVs: instances, meshes, geometries, materials
    uint4 scene1;           // SRVs: material indices, lights, atlas; UAV: output
    uint4 surface;          // UAVs: surface cur/prev, normal cur/prev
    uint4 material;         // UAVs: material cur, GI reservoirs cur/prev, DI reservoirs cur
    uint4 signals;          // UAVs: DI reservoirs prev, diffuse raw, sun raw, specular raw
    uint4 denoise0;         // UAVs: diffuse history cur/prev, moments cur/prev
    uint4 denoise1;         // UAVs: sun history cur/prev, specular history cur/prev
    uint4 denoise2;         // UAVs: a-trous ping, pong; the index holding the final diffuse; unused
    float4 gridMin;         // xyz: the light grid's corner relative to the origin; w: 1 / cell size
    uint4 grid;             // SRVs: grid cells (first, count), light indices; dimensions (8 bits each); rays per pixel
    uint4 extra;            // SRV: the alpha atlas; unused
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
    uint flags; int alphaTile; uint pad1; uint pad2; // alphaTile: with MATERIAL_ALPHA_TILE, the alpha atlas's tile
};
struct LightRecord {
    float3 position; uint type;
    float3 color; float range;
    float3 direction; float radius;
    float angleScale; float angleOffset; uint id; uint flags;
};
struct GIReservoir { float3 pos; uint normal; uint2 radiance; float W; float M; };
struct DIReservoir { uint light; float W; float M; float pad; };

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
    bool deferred;
    bool translucent;  // HDRP's transmission (grass, leaves): lit from behind too (translucent_cos)
    bool valid;
};
// The cosine a light arriving along l lights a surface with: from in front, and for a translucent one (a grass blade,
// a leaf) also from behind, the part a thin blade lets through. Without it every blade facing away from the sun was
// black, where HDRP lights it through.
static const float TRANSMISSION = 0.5;
float translucent_cos(float3 normal, float3 l, bool translucent) {
    float c = dot(normal, l);
    return c > 0.0 ? c : (translucent ? -c * TRANSMISSION : 0.0);
}
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
    s.valid = a.w > 0.0;
    if (materialIndex != INVALID) {
        RWTexture2D<uint2> mat = ResourceDescriptorHeap[materialIndex];
        uint2 m = mat[p];
        s.albedo = srgb_to_linear(unpack_rgba8(m.x).rgb);
        s.f0 = srgb_to_linear(unpack_rgba8(m.y).rgb);
    } else { s.albedo = 0.5; s.f0 = 0.04; }
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
float3 fresnel_schlick(float3 f0, float cosTheta) { return f0 + (1.0 - f0) * pow(1.0 - saturate(cosTheta), 5.0); }
float ggx_d(float nh, float a2) { float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (PI * d * d + 1e-7); }
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
// An alpha-tested material's alpha at 256x256 (point-sampled, like the color tiles).
float alpha_sample(int tile, float2 uv) {
    Texture2D<float> alphas = ResourceDescriptorHeap[g.extra.x];
    uint2 origin = uint2(uint(tile) % ALPHA_TILES_PER_ROW, uint(tile) / ALPHA_TILES_PER_ROW) * ALPHA_TILE_SIZE;
    uint2 texel = min(uint2(frac(uv) * ALPHA_TILE_SIZE), ALPHA_TILE_SIZE - 1);
    return alphas.Load(int3(origin + texel, 0));
}
struct Hit {
    float3 position;
    float3 normal;     // geometric, world, towards the ray's origin
    float2 uv;
    float4 color;      // vertex color, white without
    uint material;
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
// Alpha test of a candidate triangle: false means the ray passes through.
bool alpha_opaque(uint instanceId, uint geometry, uint primitive, float2 bary) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    InstanceRecord inst = instances[instanceId];
    MaterialRecord m = materials[materialIndices[inst.materialBase + geometry]];
    if ((m.flags & MATERIAL_ALPHA_TEST) == 0) return true;
    // Without a texture the base color's alpha decides alone (a cutoff above 1 makes a submesh invisible to rays).
    float a = m.baseColor.a;
    if (m.albedoTile >= 0) {
        MeshRecord mesh = meshes[inst.mesh];
        float2 uv = interpolate_uv(mesh, triangle_indices(mesh, geometry, primitive), bary);
        float2 st = uv * m.uvST.xy + m.uvST.zw;
        a *= (m.flags & MATERIAL_ALPHA_TILE) ? alpha_sample(m.alphaTile, st) : atlas_sample(m.albedoTile, st).a;
    }
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
    h.position = hitPos;
    float3 n = cross(w1 - w0, w2 - w0);
    float len = length(n);
    n = len > 1e-12 ? n / len : -rayDir;
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
struct SurfaceMaterial { float3 albedo; float3 emissive; };
SurfaceMaterial hit_material(Hit h) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    MaterialRecord m = materials[h.material];
    SurfaceMaterial s;
    float3 base = m.baseColor.rgb;
    if (m.albedoTile >= 0) base *= atlas_sample(m.albedoTile, h.uv * m.uvST.xy + m.uvST.zw).rgb;
    if (m.flags & MATERIAL_VERTEX_COLOR) base *= h.color.rgb;
    s.albedo = saturate(base * (1.0 - m.metallic * 0.8));
    s.emissive = m.emissive;
    if (m.emissiveTile >= 0) s.emissive *= atlas_sample(m.emissiveTile, h.uv * m.uvST.xy + m.uvST.zw).rgb;
    return s;
}

// ---------------------------------------------------------------- traversal
bool trace_closest(float3 origin, float3 dir, float tmin, float tmax, out Hit hit) {
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
bool occluded(float3 origin, float3 dir, float tmin, float tmax) {
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
float near_back(float distanceFromCamera) { return 0.15 + distanceFromCamera * 0.01; }
bool passes_through(uint instanceId, uint geometry, bool frontFace, float t, float nearBack) {
    if (t >= nearBack * 3.0) return false;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    InstanceRecord inst = instances[instanceId];
    if (inst.flags & INSTANCE_STAND_IN) return true;
    if (frontFace || t >= nearBack) return false;
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    return (materials[materialIndices[inst.materialBase + geometry]].flags & MATERIAL_ALPHA_TEST) == 0;
}
bool trace_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack, out Hit hit) {
    float t0 = tmin;
    for (uint pass = 0; pass < 3; ++pass) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = tmax;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const float t = q.CommittedRayT();
        if (pass < 2 && passes_through(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace(), t, nearBack)) { t0 = t + 1e-3; continue; }
        hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                        q.CommittedObjectToWorld3x4(), dir, origin + dir * t);
        return true;
    }
    hit = (Hit)0;
    return false;
}
// Near the surface, where a hit may be passed through, the nearest hit decides: with the first hit found accepted,
// a passable one found first moved the ray on past a nearer real blocker (a leak). Beyond that, any hit blocks.
bool occluded_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack) {
    const float nearEnd = min(tmax, nearBack * 3.0);
    float t0 = tmin;
    for (uint pass = 0; pass < 3 && t0 < nearEnd; ++pass) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = nearEnd;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, 0xFF, ray);
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
// Light arriving at position from light: irradiance on a surface facing it, and the direction and distance to it.
float3 light_incident(LightRecord l, float3 position, out float3 dir, out float dist) {
    if (l.type == 2) { dir = -l.direction; dist = 1e7; return l.color; }
    float3 d = l.position - position;
    float d2 = max(dot(d, d), 1e-4);
    dist = sqrt(d2); dir = d / dist;
    float atten = smooth_window(d2, 1.0 / max(l.range * l.range, 1e-4), 1.0) / max(d2, l.radius * l.radius + 1e-4);
    if (l.type == 1) { float s = saturate(dot(-dir, l.direction) * l.angleScale + l.angleOffset); atten *= s * s; }
    return l.color * atten;
}
LightRecord load_light(uint i) { StructuredBuffer<LightRecord> lights = ResourceDescriptorHeap[g.scene1.y]; return lights[i]; }

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
    if (g.grid.x == INVALID || g.settings2.z == 0) return false;
    int3 dims = int3(g.grid.z & 0xFF, (g.grid.z >> 8) & 0xFF, (g.grid.z >> 16) & 0xFF);
    int3 c = int3(floor((position - g.gridMin.xyz) * g.gridMin.w));
    if (any(c < 0) || any(c >= dims)) return false;
    StructuredBuffer<uint2> cells = ResourceDescriptorHeap[g.grid.x];
    uint2 cell = cells[(c.z * dims.y + c.y) * dims.x + c.x];
    first = cell.x; count = cell.y;
    return count > 0;
}
uint cell_light(uint first, uint count, inout Rng rng) {
    StructuredBuffer<uint> indices = ResourceDescriptorHeap[g.grid.y];
    return indices[first + min(uint(rng.next() * count), count - 1)];
}
// Candidate c of n from a cell of count lights, without repeats: every light once when n == count, otherwise n
// spread evenly from a random start. Each candidate is still any of the cell's lights with probability 1 / count,
// so resampling weights stay target * count; drawn independently, the same light came up several times while
// others (the one whose shadow falls here) were missed, and the choice flickered between frames.
uint cell_candidate(uint first, uint count, uint n, uint c, uint start) {
    StructuredBuffer<uint> indices = ResourceDescriptorHeap[g.grid.y];
    return indices[first + (start + (c * count) / n) % count];
}

// Next-event estimation at a diffuse path vertex: the sun, and one of the lights that reach it, each with
// a shadow ray. Up to eight of the cell's lights compete by their unshadowed irradiance (resampled
// importance sampling), so a light next to the vertex is chosen as often as it matters.
float3 direct_diffuse(float3 position, float3 n, float3 albedo, float offset, inout Rng rng) {
    float3 result = 0.0;
    if (g.sunColor.w > 0.0) {
        float3 l = cone_sample(g.sunDir.xyz, g.sunDir.w, rng.next2());
        float nl = dot(n, l);
        if (nl > 0.0 && !occluded(position + n * offset, l, 0.0, g.params.y)) result += g.sunColor.rgb * nl;
    }
    uint first, count;
    if (light_cell(position, first, count)) {
        const uint candidates = min(count, 8u);
        const uint start = min(uint(rng.next() * count), count - 1);
        float wsum = 0.0, chosenTarget = 0.0;
        float3 chosenE = 0.0, chosenL = 0.0;
        LightRecord chosen = (LightRecord)0;
        for (uint c = 0; c < candidates; ++c) {
            LightRecord light = load_light(cell_candidate(first, count, candidates, c, start));
            float3 l; float dist;
            float3 e = light_incident(light, position, l, dist);
            float target = luminance(e) * saturate(dot(n, l));
            float w = target * float(count); // target / (1 / count)
            wsum += w;
            if (w > 0.0 && rng.next() * wsum < w) { chosenTarget = target; chosenE = e; chosenL = l; chosen = light; }
        }
        if (chosenTarget > 0.0) {
            float3 origin = position + n * offset;
            float tmax;
            float3 dir = shadow_ray(chosen, origin, rng.next2(), tmax);
            if (!occluded(origin, dir, 0.0, max(tmax - offset, 0.0)))
                result += chosenE * saturate(dot(n, chosenL)) * (wsum / (float(candidates) * chosenTarget));
        }
    }
    return result * albedo / PI;
}
// Radiance leaving a path vertex hit from direction dir, following the path diffusely for more bounces.
float3 shade_path(Hit hit, float3 dir, uint bounces, inout Rng rng) {
    float3 throughput = 1.0, radiance = 0.0;
    for (uint bounce = 0; bounce < 4; ++bounce) {
        SurfaceMaterial m = hit_material(hit);
        float dist = length(hit.position - g.cameraPos.xyz);
        float offset = 0.01 + dist * 0.001;
        radiance += throughput * (m.emissive + direct_diffuse(hit.position, hit.normal, m.albedo, offset, rng));
        if (bounce + 1 >= bounces) break;
        throughput *= m.albedo;
        if (bounce >= 1) {
            float p = saturate(max(throughput.r, max(throughput.g, throughput.b)));
            if (rng.next() >= p) break;
            throughput /= max(p, 1e-3);
        }
        float3 next = cosine_sample(hit.normal, rng.next2());
        Hit h2;
        if (!trace_closest(hit.position + hit.normal * offset, next, 0.0, g.params.y, h2)) { radiance += throughput * sky_radiance(next); break; }
        hit = h2; dir = next;
    }
    return radiance;
}
