// SPDX-License-Identifier: MIT
// PT-64: the game's own shaders at a hit. Since the tracer's own camera rays make every pixel's surface (pt_primary), what
// the player sees of a surface is what a hit reads of its material; read HDRP Lit's way alone, the game's rock was one
// stretched pale texture, its grass olive, its ground flat grey and its furniture too bright (the yardstick, 2026-10-07).
// So each of the game's Shader Graphs that draws the world has a program here: what its GBuffer pass computes (base color,
// metallic, smoothness, occlusion and the normal), from the material's own textures and values, written from the game's
// compiled code (docs/agents/materials.md has each one's reading and where it was checked). Included by pt_common.hlsli.
//
// A material names its program and its record (MaterialRecord.program, programRow). The records are rows of four words
// (FrameConstants.textures.w; row 0 the globals, a record's first row at 1 + programRow): the program, its texture count,
// its value rows and a bit for each texture the material has; then the textures' numbers in the tracer's heap (-1: the
// material has none: the shader's default); then its values. A program whose textures are not all open is not run: the
// material reads HDRP Lit's way from its own fields until they are.
#define PROGRAM_LIT 0u
#define PROGRAM_DETAIL 1u
#define PROGRAM_ARCHITECTURE 2u
#define PROGRAM_CLIFF 3u
#define PROGRAM_GRASS 4u
#define PROGRAM_LEAVES 5u
#define PROGRAM_TRUNK 6u
#define PROGRAM_TERRAIN 7u
#define PROGRAM_TRIPLANAR 8u
#define PROGRAM_SKIN 9u

struct Program { uint kind; uint textures; uint first; uint values; bool ready; };
uint4 program_word(uint row) { StructuredBuffer<uint4> rows = ResourceDescriptorHeap[g.textures.w]; return rows[row]; }
Program program_of(MaterialRecord m) {
    Program p;
    p.kind = PROGRAM_LIT; p.textures = 0u; p.first = 0u; p.values = 0u; p.ready = false;
    if (m.program == PROGRAM_LIT || g.textures.w == INVALID || !shared_on()) return p;
    const uint4 head = program_word(1u + m.programRow);
    if (head.x != m.program || head.y > 32u) return p;
    p.kind = head.x; p.textures = head.y; p.first = 2u + m.programRow; p.values = p.first + (head.y + 3u) / 4u;
    uint open = 0u;
    for (uint r = 0u; r * 4u < head.y; ++r) {
        const uint4 n = program_word(p.first + r);
        open |= ((int(n.x) >= 0 ? 1u : 0u) | (int(n.y) >= 0 ? 2u : 0u) | (int(n.z) >= 0 ? 4u : 0u) | (int(n.w) >= 0 ? 8u : 0u)) << (r * 4u);
    }
    p.ready = (head.w & ~open) == 0u;
    return p;
}
int program_texture(Program p, uint i) { return int(program_word(p.first + i / 4u)[i % 4u]); }
float4 program_value(Program p, uint i) { return asfloat(program_word(p.values + i)); }
// A program's texture, or what Unity binds where the material has none (none: white, black, or a flat normal).
float4 program_sample(Program p, uint i, float2 st, float2 gx, float2 gy, float4 none) {
    const int n = program_texture(p, i);
    if (n < 0) return none;
    return shared_sample(n, st, gx, gy);
}
float4 program_sample_clamped(Program p, uint i, float2 st, float2 gx, float2 gy, float4 none) {
    const int n = program_texture(p, i);
    if (n < 0) return none;
    return shared_sample_clamped(uint(n), st, gx, gy);
}
static const float4 MAP_WHITE = float4(1.0, 1.0, 1.0, 1.0), MAP_BLACK = float4(0.0, 0.0, 0.0, 0.0), MAP_FLAT = float4(0.5, 0.5, 1.0, 1.0);

// ---------------------------------------------------------------- what the game's graphs compute with
// A normal map as Unity unpacks it (red and green, or alpha and green), then Shader Graph's Normal Strength.
float3 graph_normal(float4 packed, float strength) {
    packed.a *= packed.r;
    const float2 xy = packed.ag * 2.0 - 1.0;
    const float z = max(1e-16, sqrt(1.0 - saturate(dot(xy, xy))));
    return float3(xy * strength, lerp(1.0, z, saturate(strength)));
}
float3 graph_normal_raw(float4 packed) {
    packed.a *= packed.r;
    const float2 xy = packed.ag * 2.0 - 1.0;
    return float3(xy, max(1e-16, sqrt(1.0 - saturate(dot(xy, xy)))));
}
// Shader Graph's Normal Blend (its default mode): the bumps of both, the flat parts multiplied.
float3 graph_normal_blend(float3 a, float3 b) { return normalize(float3(a.xy + b.xy, a.z * b.z)); }
// The value noise of the game's graphs (Shader Graph's Simple Noise, as their compiled code computes it): a cell's hash,
// the four around a point blended smoothly, three octaves.
float graph_hash(float2 cell) {
    uint2 v = uint2(int2(round(cell)));
    v.y ^= 1103515245u; v.x += v.y; v.x *= v.y; v.x ^= v.x >> 5; v.x *= 0x27d4eb2du;
    return float(v.x >> 8) * (1.0 / 16777215.0);
}
float graph_value_noise(float2 uv) {
    const float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f);
    const float r0 = graph_hash(i), r1 = graph_hash(i + float2(1.0, 0.0)), r2 = graph_hash(i + float2(0.0, 1.0)), r3 = graph_hash(i + float2(1.0, 1.0));
    return lerp(lerp(r0, r1, f.x), lerp(r2, r3, f.x), f.y);
}
float graph_simple_noise(float2 uv, float scale) {
    return graph_value_noise(uv * scale) * 0.125 + graph_value_noise(uv * (scale * 0.5)) * 0.25 + graph_value_noise(uv * (scale * 0.25)) * 0.5;
}
// Shader Graph's Hue node (normalized): the color's hue moved by offset turns of the wheel.
float3 graph_hue(float3 c, float offset) {
    const float4 p = c.g >= c.b ? float4(c.g, c.b, 0.0, -1.0 / 3.0) : float4(c.b, c.g, -1.0, 2.0 / 3.0);
    const float4 q = c.r >= p.x ? float4(c.r, p.y, p.z, p.x) : float4(p.x, p.y, p.w, c.r);
    const float d = q.x - min(q.w, q.y), e = 1e-10;
    const float v = d == 0.0 ? q.x : q.x + e;
    float h = abs(q.z + (q.w - q.y) / (6.0 * d + e)) + offset;
    h = h < 0.0 ? h + 1.0 : h > 1.0 ? h - 1.0 : h;
    const float3 k = abs(frac(h + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return v * lerp(1.0, saturate(k - 1.0), d / (q.x + e));
}
// The game's global saturation (its $Globals' _GlobalSaturationAdjustment, row 0 of the records): every one of its graphs
// scales the color's distance from its own grey by one plus it.
float3 graph_saturation(float3 c) {
    const float s = 1.0 + asfloat(program_word(0u).x);
    const float grey = dot(c, float3(0.212673, 0.715152, 0.072175));
    return grey + s * (c - grey);
}

// ---------------------------------------------------------------- a hit as the game's shader sees its pixel
// The hit's position in the world (HDRP's absolute world position: the graphs' noise and planar layers are laid over it).
float3 hit_world(Hit h) { return h.position + g.cacheOrigin.xyz; }
// Its object's axes in the world (the columns of unity_ObjectToWorld), and a world position or direction in the object's own
// space (unity_WorldToObject).
void hit_axes(Hit h, out float3 c0, out float3 c1, out float3 c2, out float3 t) {
    c0 = float3(h.toWorld[0].x, h.toWorld[1].x, h.toWorld[2].x); c1 = float3(h.toWorld[0].y, h.toWorld[1].y, h.toWorld[2].y);
    c2 = float3(h.toWorld[0].z, h.toWorld[1].z, h.toWorld[2].z); t = float3(h.toWorld[0].w, h.toWorld[1].w, h.toWorld[2].w);
}
float3 hit_to_object(Hit h, float3 v, bool position) {
    float3 c0, c1, c2, t; hit_axes(h, c0, c1, c2, t);
    const float det = dot(c0, cross(c1, c2));
    if (!(abs(det) > 1e-30)) return v;
    const float3 d = position ? v - t : v;
    return float3(dot(cross(c1, c2), d), dot(cross(c2, c0), d), dot(cross(c0, c1), d)) / det;
}
float2 hit_scale(Hit h) {
    float3 c0, c1, c2, t; hit_axes(h, c0, c1, c2, t);
    return float2(length(c0), length(c1));
}
// The upscaler's mip bias as a factor on a footprint (material_gradients').
float mip_scale() { const float bias = asfloat(g.hair.w); return bias < 0.0 && bias > -8.0 ? exp2(bias) : 1.0; }
// The ray's footprint in a planar mapping of the world (u along a, v along b: the world's axes, per metre scale).
void planar_gradients(Hit h, float3 a, float3 b, float scale, out float2 gx, out float2 gy) {
    const float s = scale * mip_scale();
    gx = float2(dot(h.footX, a), dot(h.footX, b)) * s; gy = float2(dot(h.footY, a), dot(h.footY, b)) * s;
}
// The camera's forward axis (the game's graphs read HDRP's view matrix for it), and the direction the sun's light travels
// (HDRP's main directional light's forward; none: 0).
float3 camera_forward() {
    const float4 a = mul(g.invViewProj, float4(0.0, 0.0, 1.0, 1.0)), b = mul(g.invViewProj, float4(0.0, 0.0, 0.25, 1.0));
    const float3 d = b.xyz / b.w - a.xyz / a.w;
    const float l = length(d);
    return l > 1e-20 ? d / l : float3(0.0, 0.0, 1.0);
}
float3 sun_forward() { return g.sunColor.w > 0.0 ? -g.sunDir.xyz : 0.0; }

// The tangent frame a normal map is read in at a hit: the mesh's smooth normal, its tangent along the triangle's dP/du, its
// bitangent on dP/dv's side (a mirrored island flips it). ok false where the triangle has no texture directions.
struct TangentFrame { float3 t; float3 b; float3 n; bool ok; };
TangentFrame hit_frame(Hit h) {
    TangentFrame f;
    f.n = h.rounded; f.t = 0.0; f.b = 0.0; f.ok = false;
    float3 t = h.dpdu - f.n * dot(f.n, h.dpdu);
    const float tl = length(t);
    if (!(tl > 1e-12)) return f;
    f.t = t / tl;
    f.b = cross(f.n, f.t);
    if (dot(f.b, h.dpdv) < 0.0) f.b = -f.b;
    f.ok = true;
    return f;
}
// A tangent-space normal in the world, kept on the side the ray came from.
float3 frame_normal(TangentFrame f, float3 tn, Hit h) {
    if (!f.ok) return h.normal;
    const float3 w = f.t * tn.x + f.b * tn.y + f.n * tn.z;
    const float wl = length(w);
    if (!(wl > 1e-12)) return h.normal;
    const float3 bumped = w / wl;
    const float c = dot(bumped, h.normal);
    return c >= 0.05 ? bumped : normalize(bumped + h.normal * (0.05 - c));
}
float3 frame_vector(TangentFrame f, float3 tn) { return f.t * tn.x + f.b * tn.y + f.n * tn.z; }

// What a program makes of a hit: the base color (before metallic splits it), metallic, smoothness, the occlusion the game
// takes off its bounced light, and the shading normal in the world.
struct MaterialPoint { float3 color; float metallic; float smoothness; float occlusion; float3 normal; };

// ---------------------------------------------------------------- HDRP Lit's detail map (PROGRAM_DETAIL)
// Texture 0: the detail map (red: albedo, green and alpha: its normal's y and x, blue: smoothness; grey is "no change").
// Values: [0] its tiling and offset over the mesh's coordinates (the base map's folded in where HDRP links them),
// [1] the albedo, normal and smoothness scales. Applied over HDRP Lit's own reading (o), as far as the mask map's blue
// lets it (detailMask; 1 without a mask map), as HDRP's Lit applies it.
void program_detail(Hit h, MaterialRecord m, Program p, float detailMask, float3 tangentNormal, TangentFrame f, inout MaterialPoint o) {
    const float4 st = program_value(p, 0u), scales = program_value(p, 1u);
    const float s = mip_scale();
    const float4 d = program_sample(p, 0u, h.uv * st.xy + st.zw, h.uvGradX * st.xy * s, h.uvGradY * st.xy * s, float4(0.5, 0.5, 0.5, 0.5));
    const float albedo = d.r * 2.0 - 1.0, smooth = d.b * 2.0 - 1.0;
    const float speed = saturate(abs(albedo) * scales.x);
    float3 overlay = lerp(sqrt(saturate(o.color)), albedo < 0.0 ? 0.0 : 1.0, speed * speed);
    overlay *= overlay;
    o.color = lerp(o.color, saturate(overlay), detailMask);
    const float smoothSpeed = saturate(abs(smooth) * scales.z);
    o.smoothness = lerp(o.smoothness, saturate(lerp(o.smoothness, smooth < 0.0 ? 0.0 : 1.0, smoothSpeed)), detailMask);
    // Its normal over the base's, as reoriented normal mapping lays one bump over another.
    const float2 dxy = (d.ag * 2.0 - 1.0) * scales.y;
    const float3 dn = normalize(float3(dxy, max(1e-16, sqrt(1.0 - saturate(dot(d.ag * 2.0 - 1.0, d.ag * 2.0 - 1.0))))));
    const float3 t = tangentNormal + float3(0.0, 0.0, 1.0), u = dn * float3(-1.0, -1.0, 1.0);
    const float3 blended = t * dot(t, u) / max(t.z, 1e-6) - u;
    o.normal = frame_normal(f, normalize(lerp(tangentNormal, blended, detailMask)), h);
}

// ---------------------------------------------------------------- TG/Architecture/KF_Architecture (PROGRAM_ARCHITECTURE)
// The material's own fields hold its base layer (color map and tint, normal map, mask map; uvST the base tiling).
// Textures: 0 the overlay's color, 1 its normal, 2 its mask, 3 the micro detail normal, 4 the overlay's mask noise.
// Values: [0] overlay tint, overlay smoothness; [1] base normal strength, overlay normal strength, overlay tiling, micro
// detail tiling; [2] overlay slope (two ends), bottom height, micro detail strength; [3] mask tiling, mask offset (xy), base
// smoothness; [4] overlay on, mask texture on, add the occlusion mask, distance-based smoothness (each 0 or 1);
// [5] occlusion multiply.
MaterialPoint program_architecture(Hit h, MaterialRecord m, Program p, TangentFrame f) {
    const float4 v0 = program_value(p, 0u), v1 = program_value(p, 1u), v2 = program_value(p, 2u), v3 = program_value(p, 3u), v4 = program_value(p, 4u);
    const float occlusionMultiply = program_value(p, 5u).x;
    const bool overlay = v4.x != 0.0;
    const float s = mip_scale();
    const float2 uvB = h.uv * m.uvST.xy + m.uvST.zw, gxB = h.uvGradX * m.uvST.xy * s, gyB = h.uvGradY * m.uvST.xy * s;
    const float2 uvO = h.uv * v1.z, gxO = h.uvGradX * v1.z * s, gyO = h.uvGradY * v1.z * s;
    const float3 base = (m.colorTexture >= 0 ? shared_sample(m.colorTexture, uvB, gxB, gyB).rgb : 1.0) * m.baseColor.rgb;
    // Read only where the overlay is on (PT-73 c): its color goes nowhere else.
    float3 overlayColor = 1.0;
    if (overlay) overlayColor = program_sample(p, 0u, uvO, gxO, gyO, MAP_WHITE).rgb;
    const float4 baseNormal = m.normalTexture >= 0 ? shared_sample(m.normalTexture, uvB, gxB, gyB) : MAP_FLAT;
    const float3 nB = graph_normal(baseNormal, v1.x);
    const float3 nO = graph_normal(program_sample(p, 1u, uvO, gxO, gyO, MAP_FLAT), v1.y);
    // The overlay's share by slope: the two layers' bumps laid over the mesh's own normal, its height in the world.
    const float3 both = graph_normal_blend(nB, nO);
    const float3 N = h.rounded;
    const float3 sloped = normalize(float3(both.xy + N.xy, N.z * both.z));
    float share = saturate(lerp(v2.x, v2.y, (sloped.y + 1.0) * 0.5));
    const float4 mask = m.maskTexture >= 0 ? shared_sample(m.maskTexture, uvB, gxB, gyB) : MAP_BLACK;
    if (v4.z != 0.0) share = max(share, 1.0 - mask.g);
    // Its mask noise, laid over the world from three sides.
    const float3 P = hit_world(h);
    const float k = v3.x * 0.5;
    float2 gx, gy;
    float noise = 0.0;
    if (v4.y != 0.0) { // read only where the mask texture is on (PT-73 c): masked takes it nowhere else
        planar_gradients(h, float3(0, 1, 0), float3(0, 0, 1), k, gx, gy);
        noise = program_sample(p, 4u, P.yz * k + v3.yz, gx, gy, MAP_WHITE).b * N.x * N.x;
        planar_gradients(h, float3(1, 0, 0), float3(0, 0, 1), k, gx, gy);
        noise += program_sample(p, 4u, P.xz * k + v3.yz, gx, gy, MAP_WHITE).b * N.y * N.y;
        planar_gradients(h, float3(1, 0, 0), float3(0, 1, 0), k, gx, gy);
        noise += program_sample(p, 4u, P.xy * k + v3.yz, gx, gy, MAP_WHITE).b * N.z * N.z;
    }
    const float masked = v4.y != 0.0 ? share * noise : share;
    float3 n = overlay ? normalize(lerp(nB, nO, share)) : nB;
    // The micro detail normal, tiled by the object's size.
    const float2 tile = hit_scale(h) * v1.w;
    const float3 micro = graph_normal_raw(program_sample(p, 3u, uvB * tile, gxB * tile, gyB * tile, MAP_FLAT));
    n = normalize(float3(micro.xy * v2.w + n.xy, lerp(1.0, micro.z, saturate(v2.w)) * n.z));
    // The overlay over the object's foot: by a noise over the object, as far up as the bottom height says.
    const float3 worldNormal = frame_vector(f, n);
    const float3 q = hit_to_object(h, h.position + worldNormal, true);
    const float foot = graph_simple_noise(q.xy, 8.0);
    const float height = hit_to_object(h, h.position, true).y;
    const float bottom = saturate(foot * height * height * q.y - v2.z);
    const float cover = min(max(1.0 - bottom, masked), 1.0);
    MaterialPoint o;
    float3 color = overlay ? lerp(base, overlayColor * v0.rgb, cover) : base;
    color = graph_saturation(color);
    // A slight turn of its hue from place to place.
    o.color = graph_hue(color, graph_simple_noise(P.xz, 1.0) * 0.06 - 0.03);
    const float4 overlayMask = program_sample(p, 2u, uvO, gxO, gyO, MAP_BLACK);
    float smooth = mask.a * v3.w;
    if (overlay) smooth = lerp(smooth, overlayMask.a * v0.w, cover);
    if (v4.w != 0.0) smooth = lerp(smooth, 0.1, min(length(h.position - g.cameraPos.xyz) * 0.05, 1.0));
    o.smoothness = smooth;
    const float occlusionMixed = lerp(mask.g, overlayMask.g, cover);
    o.metallic = overlay ? lerp(mask.r, overlayMask.r, cover) : mask.r;
    o.occlusion = lerp(occlusionMixed, ((overlay ? occlusionMixed : mask.g) + 1.0) / 101.0, occlusionMultiply);
    o.normal = frame_normal(f, n, h);
    return o;
}

// ---------------------------------------------------------------- TG/Environment/Cliffs/Cliff (PROGRAM_CLIFF)
// The material's own fields hold layer 0 (the formation's own color, normal and mask maps over the mesh's coordinates; its
// tint the base color). Textures: 0 layer 1's color (the rock up close, tiled by the object's size), 1 its normal, 2 layer
// 2's color (moss), 3 its normal, 4 the moss's mask over the world. Values: [0] layer 0's normal strength, curvature,
// occlusion to base color, occlusion-based smoothness; [1] layer 1's tint and normal strength; [2] layer 2's tint and normal
// strength; [3] layer 1 tiling, layer 2 tiling, layer 2 coverage, its occlusion-based mask; [4] smoothness ends of layer 0
// and of layer 1; [5] of layer 2, the world mask's tiling and its influence; [6] the fuzz vector and fuzziness;
// [7] the lower layers' share of the normal under moss, the mask map a formation mask (0 or 1), layer 2 on (0 or 1: the
// material's _L2 keyword).
MaterialPoint program_cliff(Hit h, MaterialRecord m, Program p, TangentFrame f) {
    const float4 v0 = program_value(p, 0u), v1 = program_value(p, 1u), v2 = program_value(p, 2u), v3 = program_value(p, 3u), v4 = program_value(p, 4u);
    const float4 v5 = program_value(p, 5u), v6 = program_value(p, 6u), v7 = program_value(p, 7u);
    const float s = mip_scale();
    const float2 gx0 = h.uvGradX * s, gy0 = h.uvGradY * s;
    const float4 layer0 = m.colorTexture >= 0 ? shared_sample(m.colorTexture, h.uv, gx0, gy0) : MAP_BLACK;
    const float2 scale = hit_scale(h);
    const float2 uvS = h.uv * scale, gxS = gx0 * scale, gyS = gy0 * scale;
    const float4 layer1 = program_sample(p, 0u, uvS * v3.x, gxS * v3.x, gyS * v3.x, MAP_WHITE);
    // Layer 1 (the rock's tiled detail) takes over from the formation's own color toward the camera, within 200 m.
    const float near = 1.0 - min(length(h.position - g.cameraPos.xyz) * 0.005, 1.0);
    float3 base = lerp(layer0.rgb * m.baseColor.rgb, layer1.rgb * v1.rgb, near);
    const float4 mask = m.maskTexture >= 0 ? shared_sample(m.maskTexture, h.uv, gx0, gy0) : MAP_BLACK;
    const float edges = saturate(mask.g * 2.0 - 0.8), hollows = saturate(mask.a * 2.0 - 0.5);
    base = 1.0 - (1.0 - edges * v0.y * (1.0 - 0.5 * near)) * (1.0 - base);
    base = lerp(base, base * hollows, v0.z);
    const float4 layer2 = program_sample(p, 2u, uvS * v3.y, gxS * v3.y, gyS * v3.y, MAP_WHITE);
    const float3 P = hit_world(h);
    float2 gx, gy;
    planar_gradients(h, float3(1, 0, 0), float3(0, 0, 1), v5.z, gx, gy);
    const float worldMask = program_sample(p, 4u, P.xz * v5.z, gx, gy, MAP_BLACK).r;
    const float3 moss = graph_hue(v2.rgb, graph_simple_noise(P.xz, 1.0) * 0.24 - 0.12) * layer2.rgb;
    const float4 packed0 = m.normalTexture >= 0 ? shared_sample(m.normalTexture, h.uv, gx0, gy0) : MAP_FLAT;
    const float3 n0 = graph_normal(packed0, v0.x);
    const float t1 = v3.x * 8.0;
    const float3 n1 = graph_normal(program_sample(p, 1u, uvS * t1, gxS * t1, gyS * t1, MAP_FLAT), v1.w);
    const float3 n01 = lerp(n0, graph_normal_blend(n1, n0), near);
    // Moss where the rock faces up, where it is hollow, and where the world's mask and the mesh's painted red let it. A
    // material without the layer (its _L2 keyword off) shows no moss and none of its bumps, but the same cover still raises
    // its smoothness and flattens its normal, as the shader's variant without the layer computes it.
    const bool mossed = v7.z != 0.0;
    const float3 up = normalize(frame_vector(f, n01));
    const float slope = saturate(up.y * 10.0 + v3.z * 18.0 - 15.0) + (1.0 - mask.a) * v3.w;
    float w = saturate((worldMask - (v5.w * 1.3 - 0.3)) * (1.0 / 0.3));
    w = min(w * w * (3.0 - 2.0 * w), 1.0);
    const float cover = saturate((slope - w) * h.color.r);
    const float strength2 = mossed ? cover * v2.w : 0.0;
    const float3 raw2 = graph_normal_raw(program_sample(p, 3u, uvS * v3.y, gxS * v3.y, gyS * v3.y, MAP_FLAT));
    const float3 n2 = float3(raw2.xy * strength2, lerp(1.0, raw2.z, saturate(strength2)));
    // The moss's fuzz: brighter where its bumps, leaned toward the sun's light, face along the camera's axis.
    const float3 lean = normalize(float3(n0.xy + n2.xy, n2.z * n0.z));
    const float3 sun = sun_forward();
    const float3 fuzzed = normalize(frame_vector(f, normalize(float3(lean.xy * 0.5 + sun.xy, lerp(1.0, lean.z, 0.5) * sun.z))));
    const float along = saturate(dot(camera_forward(), fuzzed));
    const float fuzz = pow(max(1.0 - along, 0.0), abs(v6.x)) * v6.z - along * v6.y + 1.0;
    MaterialPoint o;
    o.color = graph_saturation(mossed ? lerp(base, lerp(moss, moss * fuzz, v6.w), cover) : base);
    const float s0 = lerp(v4.x, v4.y, layer0.r), s1 = lerp(v4.z, v4.w, layer1.r), s2 = lerp(v5.x, v5.y, layer2.r);
    float smooth = s0 + near * (1.0 - s0) * s1;
    smooth = smooth + cover * (1.0 - smooth) * s2;
    smooth = saturate(max(smooth, (1.0 - mask.a) * v0.w));
    const bool formation = v7.y != 0.0;
    o.smoothness = formation ? smooth : mask.a;
    o.metallic = formation ? 0.0 : mask.r;
    o.occlusion = formation ? mask.a : mask.g;
    const float under = saturate(v7.x - cover + 1.0);
    o.normal = frame_normal(f, normalize(float3(n01.xy * under + n2.xy, n2.z * lerp(1.0, n01.z, under))), h);
    return o;
}

// ---------------------------------------------------------------- TG/Vegetation/Vegetation_Grass, _Leaves (PROGRAM_GRASS, _LEAVES)
// The material's own fields hold its base map (uvST its tiling), its normal map and scale and its mask map (smoothness in
// alpha, occlusion in green, no metal). Texture 0: the game's color variation noise. Values: [0] the healthy color and the
// variation's scale, [1] the dry color; grass alone: [2] the ground's color and its mask's hardness, [3] ground color on,
// fuzzy shading on (each 0 or 1). The game's wind adds a little to a blade's color as a gust passes (_WindAddColor): not
// read, the tracer has no gusts (docs/agents/materials.md).
float3 program_plant_color(Hit h, MaterialRecord m, Program p, bool leaves, float3 shading) {
    const float4 v0 = program_value(p, 0u), v1 = program_value(p, 1u);
    const float s = mip_scale();
    const float2 st = h.uv * m.uvST.xy + m.uvST.zw;
    const float3 map = m.colorTexture >= 0 ? shared_sample(m.colorTexture, st, h.uvGradX * m.uvST.xy * s, h.uvGradY * m.uvST.xy * s).rgb : 1.0;
    const float3 P = hit_world(h);
    float variation;
    float2 gx, gy;
    if (leaves) {
        // Over the world from three sides, by the mesh's own normal.
        const float k = v0.w * 0.01;
        float3 w = abs(h.rounded);
        w /= max(w.x + w.y + w.z, 1e-6);
        planar_gradients(h, float3(0, 0, 1), float3(0, 1, 0), k, gx, gy);
        variation = program_sample(p, 0u, P.zy * k, gx, gy, MAP_WHITE).r * w.x;
        planar_gradients(h, float3(1, 0, 0), float3(0, 0, 1), k, gx, gy);
        variation += program_sample(p, 0u, P.xz * k, gx, gy, MAP_WHITE).r * w.y;
        planar_gradients(h, float3(1, 0, 0), float3(0, 1, 0), k, gx, gy);
        variation += program_sample(p, 0u, P.xy * k, gx, gy, MAP_WHITE).r * w.z;
    } else {
        const float k = v0.w * 0.001;
        planar_gradients(h, float3(1, 0, 0), float3(0, 0, 1), k, gx, gy);
        variation = program_sample(p, 0u, P.xz * k, gx, gy, MAP_WHITE).r;
    }
    float3 color = lerp(map * v0.rgb, map * v1.rgb, variation);
    if (!leaves) {
        const float4 v2 = program_value(p, 2u), v3 = program_value(p, 3u);
        // Toward the ground's color at the blade's foot.
        if (v3.x != 0.0) color = lerp(v2.rgb, color, saturate(hit_to_object(h, h.position, true).y * v2.w));
        // "Fuzzy shading": half again as bright, but for what is within a metre of the camera's own plane.
        if (v3.y != 0.0) {
            const float along = saturate(dot(camera_forward(), shading + (h.position - g.cameraPos.xyz)));
            const float grey = dot(color, float3(0.212673, 0.715152, 0.072175));
            color = lerp(color * (1.0 + 0.5 * along), grey + (color - grey) * 0.5, 1.0 - along);
        }
    }
    return graph_saturation(color);
}

// ---------------------------------------------------------------- TG/Vegetation/Vegetation_Trunk (PROGRAM_TRUNK)
// The material's own fields hold the trunk (its color map and color, tiling and offset, normal map and scale, mask map).
// Textures: 0 the bark's color, 1 its normal, 2 its mask, 3 the mask that lays bark over the trunk (over the mesh's first
// coordinates; the bark's maps over its fourth set, Hit.uv2). Values: [0] the bark's color and normal scale, [1] its
// tiling and offset.
MaterialPoint program_trunk(Hit h, MaterialRecord m, Program p, TangentFrame f) {
    const float4 v0 = program_value(p, 0u), v1 = program_value(p, 1u);
    const float s = mip_scale();
    const float2 uvT = h.uv * m.uvST.xy + m.uvST.zw, gxT = h.uvGradX * m.uvST.xy * s, gyT = h.uvGradY * m.uvST.xy * s;
    const float2 uvB = h.uv2 * v1.xy + v1.zw, gxB = h.uv2GradX * v1.xy * s, gyB = h.uv2GradY * v1.xy * s;
    const float3 trunk = (m.colorTexture >= 0 ? shared_sample(m.colorTexture, uvT, gxT, gyT).rgb : 1.0) * m.baseColor.rgb;
    const float3 bark = program_sample(p, 0u, uvB, gxB, gyB, MAP_WHITE).rgb * v0.rgb;
    const float share = program_sample(p, 3u, h.uv, h.uvGradX * s, h.uvGradY * s, MAP_BLACK).r;
    const float4 maskT = m.maskTexture >= 0 ? shared_sample(m.maskTexture, uvT, gxT, gyT) : MAP_BLACK;
    const float4 maskB = program_sample(p, 2u, uvB, gxB, gyB, MAP_BLACK);
    const float3 nT = graph_normal(m.normalTexture >= 0 ? shared_sample(m.normalTexture, uvT, gxT, gyT) : MAP_FLAT, m.normalScale);
    const float3 nB = graph_normal(program_sample(p, 1u, uvB, gxB, gyB, MAP_FLAT), v0.w);
    MaterialPoint o;
    o.color = graph_saturation(lerp(trunk, bark, share));
    o.metallic = 0.0;
    o.smoothness = lerp(maskT.a, maskB.a, share);
    o.occlusion = lerp(maskT.g, maskB.g, share);
    o.normal = frame_normal(f, normalize(lerp(nT, nB, share)), h);
    return o;
}

// ---------------------------------------------------------------- TG/Environment/MeshTerrain (PROGRAM_TERRAIN)
// Eight layers laid flat over the world (x and z), blended by two splat maps laid over the mesh's own extent, each layer's
// share sharpened by its height. Textures: 0 and 1 the splat maps (layers 0 to 3 and 4 to 7 in their four channels); then
// per layer its color, normal and mask maps (2 + 3 i, 3 + 3 i, 4 + 3 i). Values: [0] the layers' offset over the world (x, z),
// the splat maps' size in the mesh's metres, the height transition; then per layer [1 + 2 i] its tint and tiling (metres a
// repeat) and [2 + 2 i] its normal strength, the height and the smoothness its mask's blue and alpha are scaled by;
// [17] 1 where the splat maps clamp at their edge.
// The frame the layers' bumps are turned by is the object's x axis, as the game's (PT-104; on trial until 2026-10-10,
// docs/agents/terrain-1.md and rule-1.md; the rule since). The game's shader turns them by the mesh's tangents, and most of
// the game's ground meshes bring none, nor texture coordinates (576 of its 613, read from its files): Unity then hands the
// shader its own tangent for every vertex, the object's x axis, and the bitangent is cross(normal, tangent), times -1 in a
// mirrored object. Until then the frame was the one of the mesh's own texture directions, which such a mesh has none of:
// frame_normal answered the flat normal and the ground had no relief at all (the game, run 20261010-092027: another normal
// than the game's on 63 % of the terrain's pixels flat, on 4 % with this frame, on 54 % with the bitangent the other way;
// its layers' normal strengths are 0.3 to 4).
// NOT YET, a defect: the sign by the mesh. The other 37 ground meshes bring the tangent (1, 0, 0, -1) at every vertex: the
// same axis, and by the game's rule (the bitangent times the tangent's w) the other sign. The game draws them (the
// plugin's log names them), and their bumps are turned with this sign too (docs/agents/rule-1.md: what it takes).
MaterialPoint program_terrain(Hit h, MaterialRecord m, Program p, TangentFrame f) {
    const float4 v0 = program_value(p, 0u), v17 = program_value(p, 17u);
    const float3 P = hit_world(h);
    const float2 flat = float2(P.x + v0.x, P.z - v0.y - 1.0);
    const float3 local = hit_to_object(h, h.position, true);
    const float2 splatUv = local.xz / v0.z;
    const float3 sx = hit_to_object(h, h.footX, false), sy = hit_to_object(h, h.footY, false);
    const float2 sgx = sx.xz / v0.z * mip_scale(), sgy = sy.xz / v0.z * mip_scale();
    const bool clamped = v17.x != 0.0;
    const float4 splat0 = clamped ? program_sample_clamped(p, 0u, splatUv, sgx, sgy, MAP_BLACK) : program_sample(p, 0u, splatUv, sgx, sgy, MAP_BLACK);
    const float4 splat1 = clamped ? program_sample_clamped(p, 1u, splatUv, sgx, sgy, MAP_BLACK) : program_sample(p, 1u, splatUv, sgx, sgy, MAP_BLACK);
    float3 colors[8]; float3 normals[8]; float4 masks[8]; float weights[8];
    float most = 0.0;
    for (uint i = 0u; i < 8u; ++i) {
        // A layer the splat maps give no share here is not read (PT-73 c): its weight is 0 whatever its maps hold, and its
        // color, normal and mask are multiplied by that weight below. Most of a ground is painted with two or three of the eight.
        const float painted = i < 4u ? splat0[i] : splat1[i - 4u];
        if (painted == 0.0) { colors[i] = 0.0; normals[i] = 0.0; masks[i] = 0.0; weights[i] = 0.0; continue; }
        const float4 a = program_value(p, 1u + 2u * i), b = program_value(p, 2u + 2u * i);
        const float scale = 1.0 / a.w;
        float2 gx, gy; planar_gradients(h, float3(1, 0, 0), float3(0, 0, 1), scale, gx, gy);
        const float2 uv = flat * scale;
        colors[i] = program_sample(p, 2u + 3u * i, uv, gx, gy, MAP_WHITE).rgb * a.rgb;
        normals[i] = graph_normal(program_sample(p, 3u + 3u * i, uv, gx, gy, MAP_FLAT), b.x);
        masks[i] = program_sample(p, 4u + 3u * i, uv, gx, gy, MAP_BLACK);
        const float height = masks[i].b == 0.0 ? b.y : masks[i].b * b.y;
        masks[i].a *= b.z;
        weights[i] = painted * height;
        most = max(most, weights[i]);
    }
    const float transition = max(v0.w, 1e-5);
    float total = 0.0;
    for (uint j = 0u; j < 8u; ++j) { weights[j] *= saturate(transition - abs(weights[j] - most)); total += weights[j]; }
    total = max(total, 1e-5);
    MaterialPoint o;
    o.color = 0.0; o.metallic = 0.0; o.smoothness = 0.0; o.occlusion = 0.0;
    float3 n = 0.0;
    for (uint k = 0u; k < 8u; ++k) {
        const float w = weights[k] / total;
        o.color += colors[k] * w; n += normals[k] * w;
        o.metallic += masks[k].r * w; o.occlusion += masks[k].g * w; o.smoothness += masks[k].a * w;
    }
    o.color = graph_saturation(o.color);
    o.smoothness = clamp(o.smoothness, 0.0, 0.9);
    TangentFrame own = f;
    float3 c0, c1, c2, origin; hit_axes(h, c0, c1, c2, origin);
    const float along = length(c0);
    if (along > 1e-12) {
        own.n = h.rounded; own.t = c0 / along; own.ok = true;
        own.b = cross(own.n, own.t) * (dot(c0, cross(c1, c2)) < 0.0 ? -1.0 : 1.0);
    }
    o.normal = frame_normal(own, n * float3(1.0, -1.0, 1.0), h);
    return o;
}

// ---------------------------------------------------------------- TG/Architecture/TriplanarArchitecture2 (PROGRAM_TRIPLANAR)
// Two layers and a blend map, each laid over the world from three sides (its position turned about the vertical by the
// material's rotation), each side weighed by the mesh normal's share raised to a sharpness. Layer B comes in where the blend
// map and layer B's height let it; four dirt channels of the blend map darken it toward a tint. Its normal maps are laid
// from the same three sides, the blend map's own normal over them, and the whole bump halved where the surface faces away
// from the sun. Textures: 0 to 2 layer A's color, normal and mask; 3 to 5 layer B's; 6 the blend map; 7 its normal map.
// Values: [0] layer A's tint and tiling, [1] layer B's; [2] the triplanar sharpness of A, B and the blend map, the blend
// map's tiling; [3] A's and B's normal strength, the blend normal's strength (_Thickness) and _Blend; [4] the blend map's
// remap ends, the feather, the height-based blend; [5] the dirt tint and dirt A; [6] dirt B, C, D, the rotation in degrees.
static const float4 MAP_GREY = float4(0.5, 0.5, 0.5, 0.5);
struct Triplanar { float3 axisX; float3 axisZ; float3 q; float3 w; };
// The world position turned about the vertical (its x and z by the rotation), and a side's weights by the sharpness.
Triplanar triplanar_frame(Hit h, float degrees) {
    Triplanar t;
    float s, c; sincos(degrees * 0.017453292, s, c);
    t.axisX = float3(c, 0.0, s); t.axisZ = float3(-s, 0.0, c);
    const float3 P = hit_world(h);
    t.q = float3(dot(t.axisX, P), P.y, dot(t.axisZ, P));
    t.w = 0.0;
    return t;
}
float3 triplanar_weights(float3 n, float sharpness) {
    const float3 w = pow(max(abs(n), 0.0), min(sharpness * 63.0 + 1.0, 158.0));
    return w / dot(w, 1.0);
}
float4 triplanar_sample(Program p, uint i, Triplanar t, float3 w, float scale, Hit h, float4 none) {
    // A side whose weight is 0 is not read (PT-73 c): its sample is multiplied by that weight, and a wall's own normal leaves
    // one or two of the three sides at 0 (triplanar_weights' power takes a small share to nothing).
    float2 gx, gy;
    float4 v = 0.0;
    if (w.x != 0.0) { planar_gradients(h, t.axisZ, float3(0, 1, 0), scale, gx, gy); v = program_sample(p, i, t.q.zy * scale, gx, gy, none) * w.x; }
    if (w.y != 0.0) { planar_gradients(h, t.axisX, t.axisZ, scale, gx, gy); v += program_sample(p, i, t.q.xz * scale, gx, gy, none) * w.y; }
    if (w.z != 0.0) { planar_gradients(h, t.axisX, float3(0, 1, 0), scale, gx, gy); v += program_sample(p, i, t.q.xy * scale, gx, gy, none) * w.z; }
    return v;
}
// A normal map laid from three sides, each side's bump added to the mesh normal's other two axes (Shader Graph's Triplanar
// node, normal type), in the world.
float3 triplanar_normal(Program p, uint i, Triplanar t, float3 w, float scale, Hit h, float3 n) {
    // A side whose weight is 0 is not read (as triplanar_sample): its term below is multiplied by that weight.
    float2 gx, gy;
    float3 x = 0.0, y = 0.0, z = 0.0;
    if (w.x != 0.0) { planar_gradients(h, t.axisZ, float3(0, 1, 0), scale, gx, gy); x = graph_normal_raw(program_sample(p, i, t.q.zy * scale, gx, gy, MAP_FLAT)); }
    if (w.y != 0.0) { planar_gradients(h, t.axisX, t.axisZ, scale, gx, gy); y = graph_normal_raw(program_sample(p, i, t.q.xz * scale, gx, gy, MAP_FLAT)); }
    if (w.z != 0.0) { planar_gradients(h, t.axisX, float3(0, 1, 0), scale, gx, gy); z = graph_normal_raw(program_sample(p, i, t.q.xy * scale, gx, gy, MAP_FLAT)); }
    return float3(x.z * n.x, n.y + x.y, n.z + x.x) * w.x + float3(n.x + y.x, y.z * n.y, n.z + y.y) * w.y + float3(n.x + z.x, n.y + z.y, z.z * n.z) * w.z;
}
// A world vector in the hit's tangent frame, normalized, then Shader Graph's Normal Strength.
float3 tangent_strength(TangentFrame f, float3 v, float strength) {
    float3 ts = normalize(float3(dot(f.t, v), dot(f.b, v), dot(f.n, v)));
    return float3(ts.xy * strength, lerp(1.0, ts.z, saturate(strength)));
}
MaterialPoint program_triplanar(Hit h, MaterialRecord m, Program p, TangentFrame f) {
    const float4 v0 = program_value(p, 0u), v1 = program_value(p, 1u), v2 = program_value(p, 2u), v3 = program_value(p, 3u);
    const float4 v4 = program_value(p, 4u), v5 = program_value(p, 5u), v6 = program_value(p, 6u);
    const Triplanar t = triplanar_frame(h, v6.w);
    const float3 N = h.rounded;
    const float3 wA = triplanar_weights(N, v2.x), wB = triplanar_weights(N, v2.y), wBl = triplanar_weights(N, v2.z);
    const float blendScale = v2.w * 0.09 + 0.01;
    const float3 colorA = triplanar_sample(p, 0u, t, wA, v0.w, h, MAP_WHITE).rgb * v0.rgb;
    const float3 colorB = triplanar_sample(p, 3u, t, wB, v1.w, h, MAP_WHITE).rgb * v1.rgb;
    const float4 maskA = triplanar_sample(p, 2u, t, wA, v0.w, h, MAP_GREY), maskB = triplanar_sample(p, 5u, t, wB, v1.w, h, MAP_GREY);
    const float4 blend = triplanar_sample(p, 6u, t, wBl, blendScale, h, MAP_WHITE);
    // The blend: a smoothstep of _Blend about the remapped blend map, as wide as twice the feather.
    const float feather = v4.z;
    const float e0 = (blend.r - v4.x) / (v4.y - v4.x) * (2.0 * feather + 1.0) - 2.0 * feather;
    float b = saturate((v3.w - e0) * (1.0 / (2.0 * feather)));
    b = min(b * b * (3.0 - 2.0 * b), 1.0);
    // Layer B where the blend leaves room and its height is low, as far as the height-based blend says.
    const float hb = v4.w * 2.0 - 1.0;
    const float r = (2.0 * (1.0 - maskB.b) * (1.0 - b) - 1.0) * (1.0 - abs(hb)) + hb;
    float mB = saturate((r + 0.05) * 10.0);
    mB = min(mB * mB * (3.0 - 2.0 * mB), 1.0);
    float3 color = lerp(colorA, colorB, mB);
    // The dirt: the blend map's red (about its middle) by dirt A, then its green, blue and alpha screened over by B, C, D.
    float dirt = v5.w * (2.0 * blend.r - 1.0);
    dirt += v6.x * (1.0 - dirt) * blend.g;
    dirt += v6.y * (1.0 - dirt) * blend.b;
    dirt += v6.z * (1.0 - dirt) * blend.a;
    color = lerp(color, color * v5.rgb, dirt);
    MaterialPoint o;
    o.color = color;
    o.metallic = 0.0;
    o.smoothness = lerp(maskA.a, maskB.a, mB);
    o.occlusion = lerp(maskA.g, maskB.g, mB);
    if (!f.ok) { o.normal = h.normal; return o; }
    const float3 nA = tangent_strength(f, triplanar_normal(p, 1u, t, wA, v0.w, h, N), v3.x);
    const float3 nB = tangent_strength(f, triplanar_normal(p, 4u, t, wB, v1.w, h, N), v3.y);
    const float3 n = lerp(nA, nB, mB);
    const float k = b * v3.z * v3.w;
    const float3 bl = normalize(mul(float3x3(f.t, f.b, f.n), triplanar_normal(p, 7u, t, wBl, blendScale, h, N)));
    const float3 both = normalize(float3(bl.xy * k + n.xy, lerp(1.0, bl.z, saturate(k)) * n.z));
    // Half the bump where the surface faces away from the sun (none without one).
    const float lit = g.sunColor.w > 0.0 ? saturate(dot(N, g.sunDir.xyz)) : 0.0;
    o.normal = frame_normal(f, normalize(lerp(both * 0.5 + float3(0.0, 0.0, 0.5), both, lit)), h);
    return o;
}

// ---------------------------------------------------------------- TG/Character/RealSkin (PROGRAM_SKIN)
// Head and body share the skin, tattoo, region-smoothness and micro-normal layers. Only the head reads the face's
// colour/normal blend, cavity and extra region masks. All maps use the mesh UV, except the tiled micro normal.
// Record layout: MaterialPrograms.cs. Derived from the shipped head/body GBuffer variants (materials-2.md).
// The scattering walk still owns transport; this supplies the actual surface reflectance to that walk, also off screen.
MaterialPoint program_skin(Hit h, Program p, TangentFrame f) {
    const float4 tint = program_value(p, 0u), finish = program_value(p, 1u), regions = program_value(p, 2u);
    const float4 faceRegions = program_value(p, 3u), extra = program_value(p, 4u), strengths = program_value(p, 5u);
    const bool head = tint.w != 0.0;
    const float2 gx = h.uvGradX * mip_scale(), gy = h.uvGradY * mip_scale();
    float3 color = program_sample(p, 0u, h.uv, gx, gy, MAP_WHITE).rgb * tint.rgb;
    const float4 mask = program_sample(p, 1u, h.uv, gx, gy, MAP_WHITE);
    const float4 rgba = program_sample(p, 4u, h.uv, gx, gy, MAP_BLACK);
    float4 face = 0.0;
    float2 earNeck = 0.0;
    float cavity = 1.0;
    if (head) {
        const float3 overlay = program_sample(p, 5u, h.uv, gx, gy, float4(0.5, 0.5, 0.5, 0.5)).rgb;
        const float3 blended = select(color <= 0.5, 2.0 * color * overlay, 1.0 - 2.0 * (1.0 - color) * (1.0 - overlay));
        color = lerp(color, blended, strengths.z);
        const float3 cavities = saturate(program_sample(p, 7u, h.uv, gx, gy, MAP_WHITE).gba);
        const float3 powered = pow(cavities, program_value(p, 6u).xyz);
        cavity = powered.x * powered.y * powered.z;
        color *= cavity;
        face = program_sample(p, 8u, h.uv, gx, gy, MAP_BLACK);
        // This texture stores neck in red, ear in green.
        earNeck = program_sample(p, 9u, h.uv, gx, gy, MAP_BLACK).gr;
    }
    const float4 tattoos = program_sample(p, 10u, h.uv, gx, gy, MAP_BLACK);
    for (uint i = 0u; i < 4u; ++i) {
        const float4 ink = program_value(p, 7u + i);
        color = lerp(color, ink.rgb, tattoos[i] * ink.a);
    }
    MaterialPoint o;
    o.color = color;
    o.metallic = 0.0;
    o.occlusion = mask.g * cavity;
    const float unmasked = 1.0 - saturate(dot(rgba + face, 1.0) + earNeck.x + earNeck.y);
    o.smoothness = saturate(lerp(finish.x, finish.y, pow(saturate(mask.a), finish.z)) + dot(rgba, regions) +
                           dot(face, faceRegions) + dot(earNeck, extra.xy) + unmasked * finish.w + extra.z);
    // PT-40, the skin's subsurface mask at the point (the share of its diffuse light that scatters under it, hit_material):
    // its subsurface map's red times its scale, by the scatter scale of the region the point lies in (the same region
    // masks as the smoothness's; the unmasked scale where none is). Texture 12 and values 12 to 14 of a record that has
    // them (MaterialPrograms.cs); a record without leaves the material's own number.
    if (p.textures > 12u) {
        const float4 scatter = program_value(p, 12u), faceScatter = program_value(p, 13u), restScatter = program_value(p, 14u);
        const float region = dot(rgba, scatter) + dot(face, faceScatter) + dot(earNeck, restScatter.xy) + unmasked * restScatter.z;
        g_pointSubsurface = saturate(region * restScatter.w * program_sample(p, 12u, h.uv, gx, gy, MAP_WHITE).r);
    }
    float3 n = graph_normal(program_sample(p, 2u, h.uv, gx, gy, MAP_FLAT), strengths.x);
    if (head) n = graph_normal_blend(n, graph_normal(program_sample(p, 6u, h.uv, gx, gy, MAP_FLAT), strengths.y));
    const float tile = strengths.w;
    n = graph_normal_blend(n, graph_normal(program_sample(p, 3u, h.uv * tile, gx * tile, gy * tile, MAP_FLAT), mask.b * extra.w));
    const float4 tattooPacked = program_sample(p, 11u, h.uv, gx, gy, MAP_FLAT);
    const float4 tattooStrengths = tattoos * program_value(p, 11u);
    float3 tattooNormal = graph_normal_blend(graph_normal(tattooPacked, tattooStrengths.x), graph_normal(tattooPacked, tattooStrengths.y));
    tattooNormal = graph_normal_blend(tattooNormal, graph_normal(tattooPacked, tattooStrengths.z));
    tattooNormal = graph_normal_blend(tattooNormal, graph_normal(tattooPacked, tattooStrengths.w));
    o.normal = frame_normal(f, graph_normal_blend(n, tattooNormal), h);
    return o;
}
