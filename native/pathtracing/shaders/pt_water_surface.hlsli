// SPDX-License-Identifier: MIT
// The water's surface: HDRP's waves as triangles in the acceleration structure (scene.cpp's water units, posed every frame
// by pt_water_mesh.hlsl and refitted). One surface for the camera's rays, a mirror's, a bounce's and a shadow's: they meet
// it as they meet any triangle (MASK_WAVES, part of MASK_ALL). Nothing here depends on the screen.
//   A unit's mesh record: its positions, then each vertex's undisplaced point, then its position a frame ago, each
//   vertexCount x 12 bytes in the one buffer. Its instance: INSTANCE_WAVES and the slot (bits 8 and 9). Its triangles wind
//   so that their front is the water's outward side (the air's).
#ifndef PT_WATER_SURFACE_HLSLI
#define PT_WATER_SURFACE_HLSLI
// A water mesh of the scene (scene.cpp WaterRecordGpu): its transform (object to the world, relative to the scene origin),
// its mesh record, its slot, and the cell its waves want near the camera (metres: a texel of its finest wave band).
struct WaterRecord { float4 transform[3]; uint mesh, instance, slot; float cell; };
// A ray that must pass the water (the caustics' own, which start on its surface).
static bool g_passWater = false;
// PT-42/81/94, default-off hidden switches, carried in retired ambient padding. No OPTION bit is free.
uint water_trials() { return uint(g.prevCameraPos.w); }
bool water_shore_foam_on() { return (water_trials() & 1u) != 0u; }
bool water_body_lamps_on() { return (water_trials() & 2u) != 0u; }
bool water_light_without_view() { return (water_trials() & 4u) != 0u; }
uint water_mask(uint mask) { return g_passWater ? mask & ~MASK_WAVES : mask; }
// A light's straight ray (a shadow ray, a reuse's visibility ray). With the Caustics row on it stops at the water's surface as
// any ray does: the light beyond came through the waves, bent (pt_caustics.hlsl). With the row off (TGPT_OPTION_NO_CAUSTICS)
// nothing traces that light, and the row's "off" is the straight light, as it is for glass (caustic_through): the surface
// does not stop the ray, and the water takes its share over the ray's way through it (water_straight). No Fresnel loss and
// no bending: that is what the row's "on" is.
bool water_passes_light() { return (g.settings2.y & OPTION_NO_CAUSTICS) != 0; }
uint light_mask(uint mask) { return g_passWater || water_passes_light() ? mask & ~MASK_WAVES : mask; }

// The game's displaced position of an undisplaced point of a slot's water, this frame: water_displace's, its normal unused.
float3 water_moved(uint slot, float3 base) {
    float3 position, normal; float lift, steep;
    water_displace(slot, base, float3(0.0, 1.0, 0.0), position, normal, lift, steep);
    return position;
}
// And where the waves of the frame before had it (the same undisplaced point): a hit's own motion.
float3 water_moved_before(uint slot, float3 base, float3 current) {
    const uint texture = g.waterPreviousTextures[slot];
    if (texture == INVALID) return current;
    Texture2DArray<float4> displacement = ResourceDescriptorHeap[NonUniformResourceIndex(texture)];
    const uint first = slot * 13;
    const uint count = uint(g.waterPrevious[first + 12].x);
    float3 move = 0.0;
    const float distance = length(base - g.prevCameraPos.xyz);
    for (uint band = 0; band < count; ++band) {
        const float4 u = g.waterPrevious[first + band * 3], v = g.waterPrevious[first + band * 3 + 1];
        const float4 extra = g.waterPrevious[first + band * 3 + 2];
        float fade = saturate(distance * extra.y + extra.z); fade *= fade; fade = fade * fade * (3.0 - 2.0 * fade);
        const float2 uv = float2(dot(u.xyz, base) + u.w, dot(v.xyz, base) + v.w);
        const float3 raw = displacement.SampleLevel(s_linearRepeat, float3(uv, band), 0).xyz * extra.x * fade;
        move += float3(-CHOPPINESS * raw.y, raw.x, -CHOPPINESS * raw.z);
    }
    return base + g.waterPrevious[first + 9].xyz * move.x + g.waterPrevious[first + 10].xyz * move.y + g.waterPrevious[first + 11].xyz * move.z;
}

// The surface's own smoothness at a point (HDRP's Smoothness of a water surface: its start value near the camera, its end
// value far out, going from the one to the other over a range of distances from the camera; tracer.cpp: near in
// waterScatter's w, far in the foam colour's w, the range as two floats' bits in waterFoamMaps' z and w; 1, a clear mirror
// facet, where the plugin sent none). The distance is the camera's because HDRP's is: the surface is the game's, as its
// waves' fades with the same distance are. 1 minus it is the perceptual roughness of the water's GGX facets.
float water_smoothness(uint slot, float3 position) {
    const float2 range = asfloat(g.waterFoamMaps[slot].zw);
    const float far = saturate((length(position - g.cameraPos.xyz) - range.x) / max(range.y, 1e-3));
    return saturate(lerp(g.waterScatter[slot].w, g.waterFoam[slot * 6 + 5].w, far));
}

// What fetch_hit adds where the triangle it read is the water's: the slot, the undisplaced point and the place a frame ago
// by the triangle's own barycentrics, and in rounded the game's own wave normal at that point (HDRP's gradient read, as its
// water shader shades its pixels), on the ray's side. normal stays the triangle's: which side the ray is on, and which
// medium a direction leads into, is the geometry's to say.
void water_hit(inout Hit h, InstanceRecord inst, MeshRecord mesh, uint3 idx, float2 bary) {
    const uint slot = (inst.flags >> 8) & 3u;
    ByteAddressBuffer vb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    const uint baseAt = mesh.vertexCount * 12u, beforeAt = mesh.vertexCount * 24u;
    const float3 w = float3(1.0 - bary.x - bary.y, bary.x, bary.y);
    const float3 b0 = asfloat(vb.Load3(baseAt + idx.x * 12)), b1 = asfloat(vb.Load3(baseAt + idx.y * 12)), b2 = asfloat(vb.Load3(baseAt + idx.z * 12));
    h.waterBase = b0 * w.x + b1 * w.y + b2 * w.z;
    h.waterPrevious = asfloat(vb.Load3(beforeAt + idx.x * 12)) * w.x + asfloat(vb.Load3(beforeAt + idx.y * 12)) * w.y +
                      asfloat(vb.Load3(beforeAt + idx.z * 12)) * w.z;
    h.water = slot + 1u;
    // The undisplaced triangle's own normal, on the outward side (its winding's front), tilted by the waves' gradient there.
    float3 level = cross(b1 - b0, b2 - b0);
    const float levelLength = length(level);
    level = levelLength > 1e-20 ? level / levelLength : water_row(slot, 10).xyz;
    float3 position, outward; float lift, steep;
    water_displace(slot, h.waterBase, level, position, outward, lift, steep);
    h.rounded = all(isfinite(outward)) ? (h.front ? outward : -outward) : h.normal;
}

// The water alone along a ray: its nearest crossing, as every other ray meets it. For what must know of the water before
// anything else (the camera's first walk, which medium a point lies in).
bool trace_water(float3 origin, float3 dir, float tmin, float tmax, out Hit hit) {
    hit = (Hit)0;
    if (g_passWater || g.waterGeometry.y == 0u) return false; // (no water in the scene: no query)
    RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY_WAVES(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_WAVES, ray);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
    hit = fetch_hit(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), q.CommittedTriangleBarycentrics(),
                    q.CommittedObjectToWorld3x4(), dir, origin + dir * q.CommittedRayT(), origin);
    return true;
}
// The slot whose water a point lies in, -1 in the air: the first of the water's surface straight over it, met from below,
// WITH NO GROUND BETWEEN (since 2026-10-11, PT-131): a camp under a rock arch with water over the rock was "in the water",
// the camera with it, so every pixel took the water's whole path from the camera and its light through the surface
// (reflections 320 ms a frame there, 11 with the Caustics row off, which builds no surface to be under). A second ray
// over the same stretch against the ground, rock and buildings alone (MASK_GROUND, every triangle solid: no texture is
// read), traced only where water was found overhead. A point on a surface starts a millimetre over it.
// KNOWN GAP, the other way round from before: a point in the water UNDER something solid that stands in the water (an
// arch under the sea, the underside of a boulder on the bed, a hull) counts as in the air. The game has no diving; the
// camera's own answer could be the game's (HDRP's underwater test of its water surfaces: not asked yet).
// (Two bodies one over the other still need more than this: docs/agents/water-4.md.)
// ON TRIAL (PT-99, speed_trial 1; off: asked wherever the scene holds water, as before): the height over which no water
// stands, relative to the scene origin: the highest point of any slot's water instances (FrameConstants.waterTop: the
// plugin's, from their meshes' bounds) and how far its waves can lift the surface over that (the reach water_over looks
// from). A point over it has no water above it, so the ray straight up that would say so is not traced. tracer.cpp sets
// the bit only in a frame in which the scene's own water triangles all lie at or under that highest point.
float water_ceiling() {
    // (Round 20: and the scene's own water of every slot, g.waterCeiling.x, which a slot the plugin gave no top holds too.)
    const float top = max(max(max(g.waterTop.x, g.waterTop.y), max(g.waterTop.z, g.waterTop.w)), g.waterCeiling.x);
    const float reach = max(max(water_row(0, 12).z, water_row(1, 12).z), max(water_row(2, 12).z, water_row(3, 12).z)) * (2.0 * CHOPPINESS + 1.0) + 0.05;
    return top + reach;
}
int water_medium(float3 p) {
    if (g_passWater || g.waterGeometry.y == 0u) return -1;
#ifdef PT_SPEED_MEASURE
    if (speed_measure(SPEED_M_NO_WATER_ASK)) return -1;
#endif
    if ((speed_trial() & SPEED_WATER_CEILING) != 0u && p.y > water_ceiling()) return -1;
    RayDesc ray; ray.Origin = p; ray.Direction = float3(0.0, 1.0, 0.0); ray.TMin = 0.0; ray.TMax = 1e30;
    PT_RAYQUERY_WAVES(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_WAVES, ray);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT || q.CommittedTriangleFrontFace()) return -1;
    RayDesc under; under.Origin = p; under.Direction = float3(0.0, 1.0, 0.0);
    under.TMin = max(1e-3, max(abs(p.x), max(abs(p.y), abs(p.z))) * 2e-6); under.TMax = q.CommittedRayT();
    if (under.TMax > under.TMin) {
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH) ground; // (these meshes may carry micromaps)
        ground.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_GROUND, under);
        ground.Proceed();
        if (ground.CommittedStatus() == COMMITTED_TRIANGLE_HIT) return -1;
    }
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    return int((instances[q.CommittedInstanceID()].flags >> 8) & 3u);
}
// With the Caustics row off: what the water takes of a light's straight ray, over each part of it that lies in water (the
// surface is found crossing by crossing, the nearest past the last: met from its air side the ray is in that slot's water
// from there, met from below it is out). inside: the slot the ray starts in (water_medium), -1 in the air. The loop's end is
// a loop's end, not a budget: 64 crossings of the surface along one straight ray.
// KNOWN GAP (not measured, found by reading): a ray that starts in the air, passes under a water mesh's OPEN edge (a sheet
// that ends in the air has no side) and leaves through the surface from below was in the water from the edge to there,
// and this counts none of it: it has seen no crossing on the way in. Too much light there, with the row off only. The
// row on has no such case (the ray stops at the surface). docs/agents/water-4.md, "Open edges".
float3 water_straight(float3 origin, float3 dir, float tmin, float tmax, int inside) {
    if (g.waterGeometry.y == 0u) return 1.0;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    float3 through = 1.0;
    float from = 0.0, t0 = tmin;
    [loop] for (uint crossing = 0; crossing < 64u && t0 < tmax; ++crossing) {
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = tmax;
        PT_RAYQUERY_WAVES(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE) q;
        q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_WAVES, ray);
        q.Proceed();
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) break;
        const float t = q.CommittedRayT();
        if (inside >= 0) through *= segment_transmittance(max(water_row(uint(inside), 14).xyz, 0.0), t - from);
        inside = q.CommittedTriangleFrontFace() ? int((instances[q.CommittedInstanceID()].flags >> 8) & 3u) : -1;
        from = t;
        t0 = t + max(1e-4, t * 1e-5);
    }
    if (inside >= 0) through *= segment_transmittance(max(water_row(uint(inside), 14).xyz, 0.0), max(tmax - from, 0.0));
    return through;
}
// The height of the water's surface straight over (or under) a point of the ground (x, z relative to the scene origin), and
// its slot: the same surface, met by a ray straight down from over the highest water (pt_wet: where the water stands now).
// False where no water lies over xz.
bool water_over(float2 xz, out uint slot, out float height) {
    slot = 0u; height = -1e30;
    const float top = max(max(g.waterTop.x, g.waterTop.y), max(g.waterTop.z, g.waterTop.w));
    const float bottom = min(min(g.waterBottom.x, g.waterBottom.y), min(g.waterBottom.z, g.waterBottom.w));
    if (!(top >= bottom)) return false;
    const float reach = max(max(water_row(0, 12).z, water_row(1, 12).z), max(water_row(2, 12).z, water_row(3, 12).z)) * (2.0 * CHOPPINESS + 1.0) + 0.05;
    RayDesc ray; ray.Origin = float3(xz.x, top + reach, xz.y); ray.Direction = float3(0.0, -1.0, 0.0); ray.TMin = 0.0; ray.TMax = top - bottom + 2.0 * reach;
    PT_RAYQUERY_WAVES(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_WAVES, ray);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return false;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    slot = (instances[q.CommittedInstanceID()].flags >> 8) & 3u;
    height = ray.Origin.y - q.CommittedRayT();
    return true;
}
#include "pt_water_foam.hlsli"
#endif
