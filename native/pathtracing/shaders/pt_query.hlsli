// SPDX-License-Identifier: MIT
// A ray from a surface traced to what it meets, nothing of it fetched yet (the hit's ids, barycentrics, distance and
// transform): for a pass that regroups its threads (Shader Execution Reordering) by what the ray met and fetches the hit
// after. Included after pt_common.hlsli by pt_gi.hlsl and pt_specular.hlsl alone.
#ifndef PT_QUERY_HLSLI
#define PT_QUERY_HLSLI
// A ray's hit before anything of it is fetched: what the ray query knows of it.
struct QueryHit { uint instance; uint geometry; uint primitive; float2 bary; float t; float3x4 toWorld; };
// trace_from_surface (pt_common.hlsli) up to where it fetches the hit: the same queries, the same rules for what the wind
// moves, a near back face and the hero's own body, line for line. KEEP THE TWO SIDE BY SIDE (one function for both changes
// the code of every pass that traces from a surface: docs/agents/rule-2.md). Read by the two passes that regroup their
// threads on what a ray met before fetching it: the bounced light's first ray (pt_gi) and the reflections' ray (pt_specular,
// since 2026-10-11: docs/agents/review-perf-astra.md 1, NVIDIA's SER whitepaper: reorder once the hit is known, before the
// material's work, carrying as little as can be).
bool query_from_surface(float3 origin, float3 dir, float tmin, float tmax, float nearBack, out QueryHit met) {
    ray_seed(origin, dir);
    met = (QueryHit)0;
    uint mask = MASK_ALL;
    float t0 = tmin;
    for (uint pass = 0; pass < 5 && t0 < tmax; ++pass) {
        const float end = mask == MASK_STILL ? min(tmax, SWAY_REACH) : tmax;
        if (t0 >= end) { mask = MASK_ALL; t0 = max(t0, SWAY_REACH); continue; }
        RayDesc ray; ray.Origin = origin; ray.Direction = dir; ray.TMin = t0; ray.TMax = end;
        PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
        q.TraceRayInline(g_scene, PT_RAY_FLAGS, water_mask(mask), ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
                alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
                q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            if (mask == MASK_STILL) { mask = MASK_ALL; t0 = max(t0, min(tmax, SWAY_REACH)); continue; }
            break;
        }
        const float t = q.CommittedRayT();
#ifdef PT_SPEED_MEASURE // the measuring build alone: the first query's answer, with no second look (as trace_from_surface)
        if (speed_measure(SPEED_M_ONE_QUERY)) {
            met.instance = q.CommittedInstanceID(); met.geometry = q.CommittedGeometryIndex(); met.primitive = q.CommittedPrimitiveIndex();
            met.bary = q.CommittedTriangleBarycentrics(); met.t = t; met.toWorld = q.CommittedObjectToWorld3x4();
            return true;
        }
#endif
        StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
        if (mask != MASK_STILL && t < SWAY_REACH && (instances[q.CommittedInstanceID()].flags & INSTANCE_SWAYS) != 0) { mask = MASK_STILL; continue; }
        if (pass < 4 && passes_through(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace(), t, nearBack)) { t0 = t + 1e-3; continue; }
        if (pass < 4 && first_person_inside(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedTriangleFrontFace())) { t0 = t + 1e-3; continue; }
        met.instance = q.CommittedInstanceID(); met.geometry = q.CommittedGeometryIndex(); met.primitive = q.CommittedPrimitiveIndex();
        met.bary = q.CommittedTriangleBarycentrics(); met.t = t; met.toWorld = q.CommittedObjectToWorld3x4();
        return true;
    }
    met = (QueryHit)0;
    return false;
}
#endif
