// SPDX-License-Identifier: MIT
// Cloth in the wind (PT-49): a swaying cloth's traced copy moved as the game's TG/Clothes/Cloth_Wind vertex shader moves it
// (read from its compiled shader, docs/agents/cloth.md), so its traced triangles lie where HDRP draws them this frame. Each
// record is one dispatch: mode 1 copies the mesh's still positions into the copy (the vertices no swaying submesh moves), mode
// 2 moves the vertices one submesh's indices name; modes 3 (before 1, its pose of the frame before) and 4 (after 2, or for a
// pose kept: no move) copy the pose into the copy's previous positions (previousAt), the camera rays' motion (pt_primary
// hit_motion). Reads no frame constants (they are filled after the scene is recorded).
//   pass.a: the records (SRV), this dispatch's record
// Grass and leaves in the wind (PT-63): every instance whose material the game's TG/Vegetation/Vegetation_Grass or
// Vegetation_Leaves vertex shader sways is traced in the pose HDRP draws it this frame (docs/agents/world.md 4.1 has the
// maths, read from the compiled shaders). The instances of one cell of the world are one group: one buffer of merged
// vertices and indices in the group's own space, one BLAS refitted every frame. pass.a.z says which step a dispatch is:
//   3: a group's texture coordinates, colors and normals from its members' meshes, once (a thread a vertex)
//   4: its indices, counted from each member's first vertex, and which submesh each vertex is in, once (a thread an index)
//   5: this frame's pose (a thread a vertex), and each vertex's move since the frame before (previousAt, three halves: where
//      it was less where it is; none where the group's pose of the frame before is not in its buffer, flags bit 1 off)
//   6: no move (the pose of the frame before kept: the game paused, the wind still)
//   pass.a: the groups (SRV), this dispatch's group, the step
#include "pt_common.hlsli"

struct SwayRecord {
    float4 toObject[3];   // world to object, the 3x3 part (rows; w unused)
    float time, speed, wind, noiseScale;
    float3 intensity; uint flags; // TGPT_WIND_*
    float2 direction; uint indexStart, indexCount;
    uint positionsIn, uvs, colors, indices;
    uint positionsOut, vertexCount, mode, previousAt;
};
#define WIND_VERTEX_PAINT 1u
#define WIND_POSITION_UV 2u

// Unity Shader Graph's deterministic Simple Noise: Hash_Tchou_2_1 on the cell corners, value noise with a smoothstep, three
// octaves at the scale, half and a quarter of it, weighted 1/8, 1/4 and 1/2.
float tchou_hash(float2 cell) {
    uint2 v = uint2(int2(round(cell)));
    v.y ^= 1103515245u;
    v.x += v.y;
    v.x *= v.y;
    v.x ^= v.x >> 5u;
    v.x *= 0x27d4eb2du;
    return float(v.x >> 8) * (1.0 / float(0x00ffffff));
}
float value_noise(float2 uv) {
    const float2 i = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f);
    const float r0 = tchou_hash(i), r1 = tchou_hash(i + float2(1.0, 0.0)), r2 = tchou_hash(i + float2(0.0, 1.0)), r3 = tchou_hash(i + float2(1.0, 1.0));
    return lerp(lerp(r0, r1, f.x), lerp(r2, r3, f.x), f.y);
}
float simple_noise(float2 uv, float scale) {
    return value_noise(uv * scale) * 0.125 + value_noise(uv * (scale * 0.5)) * 0.25 + value_noise(uv * (scale * 0.25)) * 0.5;
}

// ---- Grass and leaves.
struct PlantGroup {
    uint members, memberFirst, memberCount, ranges;     // the member and range records (SRVs), this group's members in them
    uint rangeFirst, rangeCount, vertexCount, indexCount;
    uint dataOut, positionsAt, uvsAt, colorsAt;          // its buffer (UAV) and where each part starts in it, in bytes
    uint normalsAt, rangeAt, indicesAt, winds;           // ... and the wind records (SRV)
    uint noise, previousAt, flags, pad2;                 // the noise maps' bytes (SRV); the moves; 1: the last pose is in the buffer
    float time, speed, direction, pad3;                  // Time.time, _VegetationWindSpeed, _VegetationWindDirection (degrees)
    float3 camera; float pad4;                           // the camera, in the group's space
    float3 forward; float pad5;                          // its forward direction
    float3 origin; float pad6;                           // the group's origin in the world
};
struct PlantMember {
    float4 toGroup[3];    // object to the group's space (rows)
    float4 toObject[3];   // world to object, the 3x3 (rows; w unused)
    uint positionsIn, uvsIn, colorsIn, normalsIn;
    uint indicesIn, vertexBase, vertexCount, pad;
};
struct PlantRange { uint member, wind, indexStartIn, indexCount; uint indexStartOut, pad0, pad1, pad2; };
struct PlantWind {
    uint kind, flags, noiseOffset, noiseSize;            // PLANT_*; the map's first byte and its width and height (16 bits each)
    float strength, bendHeight, pivotY, fadeX;
    float fadeY, pad0, pad1, pad2;
};
#define PLANT_GRASS 1u
#define PLANT_LEAVES 2u
#define PLANT_BEND 1u
#define NOISE_CLAMP_U 0x100u
#define NOISE_CLAMP_V 0x200u
#define NOISE_POINT 0x400u

// A noise map's texel (one byte), wrapped or clamped as the game's sampler does.
float noise_texel(StructuredBuffer<uint> maps, PlantWind w, int2 p) {
    const int2 size = int2(w.noiseSize & 0xFFFFu, w.noiseSize >> 16);
    p.x = (w.flags & NOISE_CLAMP_U) ? clamp(p.x, 0, size.x - 1) : ((p.x % size.x) + size.x) % size.x;
    p.y = (w.flags & NOISE_CLAMP_V) ? clamp(p.y, 0, size.y - 1) : ((p.y % size.y) + size.y) % size.y;
    const uint i = w.noiseOffset + uint(p.y) * uint(size.x) + uint(p.x);
    return float((maps[i >> 2] >> ((i & 3u) * 8u)) & 0xFFu) * (1.0 / 255.0);
}
// The map at a texture coordinate, mip 0 (the game's shaders read it with SampleLevel 0): bilinear unless its filter is
// point. A wind with no map reads 1 (Unity binds white).
float noise_at(StructuredBuffer<uint> maps, PlantWind w, float2 uv) {
    if (w.noiseSize == 0u) return 1.0;
    const float2 size = float2(w.noiseSize & 0xFFFFu, w.noiseSize >> 16);
    if (w.flags & NOISE_POINT) return noise_texel(maps, w, int2(floor(uv * size)));
    const float2 t = uv * size - 0.5;
    const float2 f = floor(t);
    const float2 a = t - f;
    const int2 p = int2(f);
    return lerp(lerp(noise_texel(maps, w, p), noise_texel(maps, w, p + int2(1, 0)), a.x),
                lerp(noise_texel(maps, w, p + int2(0, 1)), noise_texel(maps, w, p + int2(1, 1)), a.x), a.y);
}
float3 rows_mul(float4 rows[3], float3 v) { return float3(dot(rows[0].xyz, v), dot(rows[1].xyz, v), dot(rows[2].xyz, v)); }

// Vegetation_Grass's vertex, in object space: pushed along the wind by a gust of the noise over the world, more the higher
// it stands in its object. W: its world position.
float3 pose_grass(PlantGroup grp, PlantMember m, PlantWind w, StructuredBuffer<uint> maps, float3 P, float3 W) {
    const float a = (450.0 - grp.direction) * 0.017453, b = (225.0 - grp.direction) * 0.017453;
    float sb, cb; sincos(b, sb, cb);
    const float2 c = W.xz - 0.5;
    const float2 R = float2(c.x * cb + c.y * sb, c.y * cb - c.x * sb) + 0.5;
    const float T = grp.speed * grp.time;
    const float2 uv1 = (W.xz * 0.001 + (R * 0.1 + T * 0.01)) * 0.2;
    const float2 uv2 = (W.xz * 0.001 - (R * 0.1 + T * 0.002)) * 0.2;
    const float gust = max(noise_at(maps, w, uv2), noise_at(maps, w, uv1));
    const float weight = saturate(saturate(P.y * 6.0) * gust);
    float k = grp.speed * w.strength * P.y * 0.0001 + 1.0;
    k = k * k;
    const float bend = k * k - k;
    float sa, ca; sincos(a, sa, ca);
    return P + weight * rows_mul(m.toObject, float3(ca, 0.0, sa) * bend);
}
// Vegetation_Leaves's vertex, in object space: turned about the wind's axis (the same in the world for every tree) by the
// noise, the wind's speed and the material's strength, weighed by its height, fading with the distance along the camera's
// view. W: its world position; G: the same in the group's space.
float3 pose_leaves(PlantGroup grp, PlantMember m, PlantWind w, StructuredBuffer<uint> maps, float3 P, float3 W, float3 G) {
    if ((w.flags & PLANT_BEND) == 0u) return P;
    const float theta = atan2(m.toObject[0].x, m.toObject[2].x);
    const float phi = ((450.0 - grp.direction) - theta * 57.295780) * 0.017453;
    float s0, c0; sincos(phi, s0, c0);
    const float2 axis = normalize(float2(c0, s0));
    const float2 uv = W.xy * 0.001 + grp.time * 0.02;
    const float n = noise_at(maps, w, uv) * noise_at(maps, w, uv + uv);
    float s, c; sincos(n * grp.speed * w.strength * 0.017453, s, c);
    const float omc = 1.0 - c;
    const float3 turned = float3((axis.x * axis.x * omc + c) * P.x - axis.y * s * P.y + axis.x * axis.y * omc * P.z,
                                 axis.y * s * P.x + c * P.y - axis.x * s * P.z,
                                 axis.x * axis.y * omc * P.x + axis.x * s * P.y + (axis.y * axis.y * omc + c) * P.z);
    const float3 bent = P + saturate((P.y + w.pivotY) / clamp(w.bendHeight, 0.1, 100.0)) * (turned - P);
    const float viewZ = -dot(grp.forward, G - grp.camera);
    const float fade = saturate((viewZ + w.fadeX) / (w.fadeX - w.fadeY));
    return bent + fade * (P - bent);
}

void plant(uint thread) {
    StructuredBuffer<PlantGroup> groups = ResourceDescriptorHeap[pass.a.x];
    const PlantGroup grp = groups[pass.a.y];
    const uint step = pass.a.z;
    StructuredBuffer<PlantMember> members = ResourceDescriptorHeap[grp.members];
    StructuredBuffer<PlantRange> ranges = ResourceDescriptorHeap[grp.ranges];
    RWByteAddressBuffer data = ResourceDescriptorHeap[grp.dataOut];
    if (step == 4u) {
        if (thread >= grp.indexCount) return;
        // The submesh this index is in: the last range that starts at or before it.
        uint lo = 0u, hi = grp.rangeCount;
        [loop] while (hi - lo > 1u) { const uint mid = (lo + hi) >> 1; if (ranges[grp.rangeFirst + mid].indexStartOut <= thread) lo = mid; else hi = mid; }
        const PlantRange r = ranges[grp.rangeFirst + lo];
        const PlantMember m = members[r.member];
        ByteAddressBuffer indicesIn = ResourceDescriptorHeap[NonUniformResourceIndex(m.indicesIn)];
        const uint v = indicesIn.Load((r.indexStartIn + (thread - r.indexStartOut)) * 4u);
        if (v >= m.vertexCount) { data.Store(grp.indicesAt + thread * 4u, m.vertexBase); return; }
        data.Store(grp.indicesAt + thread * 4u, m.vertexBase + v);
        // The vertex's range, written ONCE (this step runs for a new group alone) and read by every later frame's pose:
        // its number WITHIN THE GROUP. The ranges' buffer is laid out anew every frame, the groups in use one after
        // another, so a group's first range moves whenever a group before it comes or goes. Until 2026-10-12 the number
        // stored was the place in that frame's buffer (rangeFirst + lo): after a walk a group's vertices read other
        // groups' ranges and members, or records left over past the buffer's end, whose meshes had been let go, and a
        // read through such a mesh's view reset the GPU (PT-125: four resets in the forest, each in this pass's step 5).
        data.Store(grp.rangeAt + (m.vertexBase + v) * 4u, lo);
        return;
    }
    if (thread >= grp.vertexCount) return;
    if (step == 6u) { data.Store2(grp.previousAt + thread * 8u, uint2(0u, 0u)); return; }
    if (step == 3u) {
        // The member this vertex is of: the last that starts at or before it.
        uint lo = 0u, hi = grp.memberCount;
        [loop] while (hi - lo > 1u) { const uint mid = (lo + hi) >> 1; if (members[grp.memberFirst + mid].vertexBase <= thread) lo = mid; else hi = mid; }
        const PlantMember m = members[grp.memberFirst + lo];
        const uint v = thread - m.vertexBase;
        float2 uv = 0.0; uint color = 0xFFFFFFFFu; uint normal = pack_normal(float3(0.0, 1.0, 0.0));
        if (m.uvsIn != INVALID) { ByteAddressBuffer uvs = ResourceDescriptorHeap[NonUniformResourceIndex(m.uvsIn)]; uv = asfloat(uvs.Load2(v * 8u)); }
        if (m.colorsIn != INVALID) { ByteAddressBuffer colors = ResourceDescriptorHeap[NonUniformResourceIndex(m.colorsIn)]; color = colors.Load(v * 4u); }
        if (m.normalsIn != INVALID) {
            ByteAddressBuffer normals = ResourceDescriptorHeap[NonUniformResourceIndex(m.normalsIn)];
            // Into the group's space: by the transpose of world to object (right under any scale).
            const float3 n = unpack_normal(normals.Load(v * 4u));
            const float3 world = m.toObject[0].xyz * n.x + m.toObject[1].xyz * n.y + m.toObject[2].xyz * n.z;
            if (dot(world, world) > 1e-20) normal = pack_normal(normalize(world));
        }
        data.Store2(grp.uvsAt + thread * 8u, asuint(uv));
        data.Store(grp.colorsAt + thread * 4u, color);
        data.Store(grp.normalsAt + thread * 4u, normal);
        data.Store(grp.rangeAt + thread * 4u, INVALID);
        return;
    }
    // This frame's pose. A vertex no triangle names lies at the group's origin. Its range by its number within the group
    // (step 4), at the group's place in this frame's buffer; a number past the group's ranges (never written by step 4)
    // is taken as none, so no read ever leaves the group's own records.
    const uint rangeLocal = data.Load(grp.rangeAt + thread * 4u);
    float3 moved = 0.0;
    if (rangeLocal < grp.rangeCount) {
        const PlantRange r = ranges[grp.rangeFirst + rangeLocal];
        const PlantMember m = members[r.member];
        ByteAddressBuffer positionsIn = ResourceDescriptorHeap[NonUniformResourceIndex(m.positionsIn)];
        const float3 P = asfloat(positionsIn.Load3((thread - m.vertexBase) * 12u));
        const float3 G = float3(dot(m.toGroup[0].xyz, P) + m.toGroup[0].w, dot(m.toGroup[1].xyz, P) + m.toGroup[1].w, dot(m.toGroup[2].xyz, P) + m.toGroup[2].w);
        float3 posed = P;
        if (r.wind != INVALID) {
            StructuredBuffer<PlantWind> winds = ResourceDescriptorHeap[grp.winds];
            StructuredBuffer<uint> maps = ResourceDescriptorHeap[grp.noise];
            const PlantWind w = winds[r.wind];
            if (w.kind == PLANT_GRASS) posed = pose_grass(grp, m, w, maps, P, G + grp.origin);
            else if (w.kind == PLANT_LEAVES) posed = pose_leaves(grp, m, w, maps, P, G + grp.origin, G);
            if (!all(isfinite(posed))) posed = P;
        }
        moved = float3(dot(m.toGroup[0].xyz, posed) + m.toGroup[0].w, dot(m.toGroup[1].xyz, posed) + m.toGroup[1].w, dot(m.toGroup[2].xyz, posed) + m.toGroup[2].w);
    }
    const float3 was = (grp.flags & 1u) != 0u ? asfloat(data.Load3(grp.positionsAt + thread * 12u)) : moved;
    const float3 d = was - moved;
    data.Store2(grp.previousAt + thread * 8u, uint2(f32tof16(d.x) | (f32tof16(d.y) << 16), f32tof16(d.z)));
    data.Store3(grp.positionsAt + thread * 12u, asuint(moved));
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (pass.a.z != 0u) { plant(id.x); return; }
    StructuredBuffer<SwayRecord> records = ResourceDescriptorHeap[pass.a.x];
    const SwayRecord r = records[pass.a.y];
    const uint count = r.mode == 2u ? r.indexCount : r.vertexCount;
    if (id.x >= count) return;
    uint v = id.x;
    if (r.mode >= 3u) {
        RWByteAddressBuffer pose = ResourceDescriptorHeap[NonUniformResourceIndex(r.positionsOut)];
        pose.Store3(r.previousAt + v * 12u, pose.Load3(v * 12u));
        return;
    }
    if (r.mode == 2u) {
        ByteAddressBuffer indices = ResourceDescriptorHeap[NonUniformResourceIndex(r.indices)];
        v = indices.Load((r.indexStart + id.x) * 4u);
        if (v >= r.vertexCount) return;
    }
    ByteAddressBuffer positionsIn = ResourceDescriptorHeap[NonUniformResourceIndex(r.positionsIn)];
    RWByteAddressBuffer positionsOut = ResourceDescriptorHeap[NonUniformResourceIndex(r.positionsOut)];
    float3 p = asfloat(positionsIn.Load3(v * 12u));
    if (r.mode == 2u) {
        float2 uv = 0.0;
        if (r.flags & WIND_POSITION_UV) uv = p.xy;
        else if (r.uvs != INVALID) { ByteAddressBuffer uvs = ResourceDescriptorHeap[NonUniformResourceIndex(r.uvs)]; uv = asfloat(uvs.Load2(v * 8u)); }
        // The noise, scrolled by the game's time.
        const float n = simple_noise(uv + r.time * r.speed, r.noiseScale);
        // The push along the wind's direction, growing up the cloth (its texture's v), in world space, into the object's.
        const float3 push = uv.y * r.wind * float3(r.direction.x, 0.0, r.direction.y);
        const float3 local = p + float3(dot(r.toObject[0].xyz, push), dot(r.toObject[1].xyz, push), dot(r.toObject[2].xyz, push));
        // Painted vertices hold still where the artist painted them dark (a missing color reads as Unity's white).
        float3 mask = 1.0;
        if (r.flags & WIND_VERTEX_PAINT) {
            float3 color = 1.0;
            if (r.colors != INVALID) { ByteAddressBuffer colors = ResourceDescriptorHeap[NonUniformResourceIndex(r.colors)]; color = unpack_rgba8(colors.Load(v * 4u)).rgb; }
            mask = 2.0 * color;
        }
        p += r.intensity * (n * local * mask);
    }
    if (!all(isfinite(p))) p = asfloat(positionsIn.Load3(v * 12u));
    positionsOut.Store3(v * 12u, asuint(p));
}
