// SPDX-License-Identifier: MIT
// The emissive triangles' records (scene.cpp build_emitters; their layout: pt_common.hlsli EMITTER_*), which the light
// sampling reads as it reads lights and paths meeting an emissive surface read to know what it is (emission_weight). Written
// when the emitters, the lights near them or the scene's origin changed, into the buffer the previous frame did not read.
// First every slot: a triangle of an emitter in the world (its transform relative to the origin), wound to face its front,
// with its texture coordinates, its average emission and its identity; a free slot as an empty record. Then each sampled
// cluster's triangles' weights into their cumulative shares of its light.
//   pass.a: the emitters (SRV), each slot's emitter (SRV), each cluster's slots, what it is and its reach (SRV), the step
//           (0 the triangles, 1 the clusters)
//   pass.b: the records (UAV), the slots, the clusters sampled as lights (their records after the slots)
#include "pt_common.hlsli"

struct Emitter { float4 row0, row1, row2; uint mesh, geometry, material, firstSlot; uint triangles, firstCluster, key, flags; };
struct ClusterInfo { uint firstSlot, count, state; float reach; };

// A kept light's own surface: what lies within the light's radius and this much more of it (a lantern's glass round its
// candle's light, a brazier's embers round its fire's). The game gives such a light the lamp's whole light and draws the glass
// glowing for the look alone; lit by the glass as well, the lamp's surroundings got its light twice.
static const float OWN_SURFACE = 0.25;
// Points of a triangle its emission is averaged over, uniform over its area (a Fibonacci lattice on the unit square).
static const uint AVERAGE_POINTS = 16;

float pack_half2(float2 v) { return asfloat(f32tof16(v.x) | (f32tof16(v.y) << 16)); }
// The point of the triangle a, b, c closest to p: the vertex, edge or face whose region p projects into.
float3 closest_on_triangle(float3 p, float3 a, float3 b, float3 c) {
    const float3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;
    const float3 bp = p - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));
    const float3 cp = p - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && d4 - d3 >= 0.0 && d5 - d6 >= 0.0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float inv = 1.0 / max(va + vb + vc, 1e-30);
    return a + ab * (vb * inv) + ac * (vc * inv);
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    RWStructuredBuffer<LightRecord> records = ResourceDescriptorHeap[pass.b.x];
    if (pass.a.w == 1u) {
        // A sampled cluster: its triangles' weights (color.x) into cumulative shares, the last exactly 1; all weightless,
        // evenly.
        if (id.x >= pass.b.z) return;
        const LightRecord cluster = records[pass.b.y + id.x];
        // At most a cluster's triangles (scene.cpp makes none bigger): a record that were ever wrong can't keep the GPU here.
        const uint first = cluster.flags, count = clamp(asuint(cluster.cookieScale), 1u, CLUSTER_TRIANGLES);
        float total = 0.0;
        for (uint k = 0; k < count; ++k) total += records[first + k].color.x;
        float running = 0.0;
        for (uint j = 0; j < count; ++j) {
            LightRecord t = records[first + j];
            running += total > 0.0 ? t.color.x : 1.0;
            t.color.x = j + 1 == count ? 1.0 : running / (total > 0.0 ? total : float(count));
            records[first + j] = t;
        }
        return;
    }
    if (id.x >= pass.b.y) return;
    StructuredBuffer<uint> owners = ResourceDescriptorHeap[pass.a.y];
    const uint owner = owners[id.x];
    LightRecord r = (LightRecord)0; // a free slot: no identity, no light
    if (owner == INVALID) { records[id.x] = r; return; }
    StructuredBuffer<Emitter> emitters = ResourceDescriptorHeap[pass.a.x];
    StructuredBuffer<ClusterInfo> clusters = ResourceDescriptorHeap[pass.a.z];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    const Emitter e = emitters[owner];
    const uint p = id.x - e.firstSlot;
    const MeshRecord mesh = meshes[e.mesh];
    const uint3 idx = triangle_indices(mesh, e.geometry, p);
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    const float3x4 toWorld = float3x4(e.row0, e.row1, e.row2);
    float3 v0 = mul(toWorld, float4(asfloat(pb.Load3(idx.x * 12)), 1.0));
    float3 v1 = mul(toWorld, float4(asfloat(pb.Load3(idx.y * 12)), 1.0));
    float3 v2 = mul(toWorld, float4(asfloat(pb.Load3(idx.z * 12)), 1.0));
    float2 uv0 = 0.0, uv1 = 0.0, uv2 = 0.0;
    if (mesh.uvs != INVALID) {
        ByteAddressBuffer ub = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs)];
        uv0 = asfloat(ub.Load2(idx.x * 8)); uv1 = asfloat(ub.Load2(idx.y * 8)); uv2 = asfloat(ub.Load2(idx.z * 8));
    }
    const MaterialRecord m = materials[e.material];
    // With the material's tiling, less whole tiles (the texture repeats), so that halves keep them to the texel.
    float2 st0 = uv0 * m.uvST.xy + m.uvST.zw, st1 = uv1 * m.uvST.xy + m.uvST.zw, st2 = uv2 * m.uvST.xy + m.uvST.zw;
    const float2 whole = floor(min(st0, min(st1, st2)));
    st0 -= whole; st1 -= whole; st2 -= whole;
    // Wound so that its edges' cross product faces its front: a mirroring transform turns the mesh's own winding around.
    if (determinant((float3x3)toWorld) < 0.0) { const float3 v = v1; v1 = v2; v2 = v; const float2 s = st1; st1 = st2; st2 = s; }
    const float3 e1 = v1 - v0, e2 = v2 - v0;
    const float area = 0.5 * length(cross(e1, e2));
    const ClusterInfo cluster = clusters[e.firstCluster + p / CLUSTER_TRIANGLES];
    uint flags = (cluster.state & EMITTER_SAMPLED) | ((m.flags & MATERIAL_DOUBLE_SIDED) != 0 ? EMITTER_TWO_SIDED : 0u);
    // A glowing surface the plugin found a kept light standing for, however far the light sits from it (a crystal's).
    if ((m.flags & MATERIAL_OWN_SURFACE) != 0) flags |= EMITTER_PAIRED;
    else if (g.settings2.z > 0) {
        uint first, count;
        if (light_cell((v0 + v1 + v2) / 3.0, first, count)) {
            StructuredBuffer<uint2> entries = ResourceDescriptorHeap[g.grid.y];
            for (uint k = 0; k < count; ++k) {
                const uint index = entries[first + k].x;
                if (index >= EMITTER_BASE) continue;
                const LightRecord light = load_light(index);
                if (light.type > 1u) continue;
                const float3 q = closest_on_triangle(light.position, v0, v1, v2) - light.position;
                const float own = light.radius + OWN_SURFACE;
                if (dot(q, q) <= own * own) { flags |= EMITTER_PAIRED; break; }
            }
        }
    }
    // Its average emission (with its coverage), for its share of its cluster's light (the light sampling draws it by that):
    // never 0 while its material emits, or a thin glowing line between the points read would never be drawn.
    float average = 0.0;
    for (uint k = 0; k < AVERAGE_POINTS; ++k) {
        const float2 u = float2((float(k) + 0.5) / float(AVERAGE_POINTS), frac(float(k) * 0.618034 + 0.5));
        const float su = sqrt(u.x);
        const float2 st = st0 + (st1 - st0) * (su * (1.0 - u.y)) + (st2 - st0) * (su * u.y);
        average += luminance(material_emission(m, st)) * emission_coverage(m, st);
    }
    average /= float(AVERAGE_POINTS);
    const float bound = luminance(m.emissive) * ((m.flags & MATERIAL_EMISSIVE_ALBEDO) != 0 ? luminance(m.baseColor.rgb) : 1.0);
    const float weight = (flags & EMITTER_PAIRED) != 0 ? 0.0 : area * max(average, bound / 256.0);
    r.position = v0; r.type = LIGHT_TRIANGLE;
    r.color = float3(weight, area, average); r.range = cluster.reach;
    r.direction = e1; r.radius = asfloat(e.material);
    r.angleScale = pack_half2(st0); r.angleOffset = pack_half2(st1);
    r.id = pcg(e.key ^ pcg(p + 0x9E3779B9u)) | 1u; r.flags = flags;
    r.up = e2; r.cookieScale = pack_half2(st2);
    records[id.x] = r;
}
