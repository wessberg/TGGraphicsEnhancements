// SPDX-License-Identifier: MIT
// PT-109 part 2, step (a) (docs/agents/megageo-2.md): the rays of tests/megageo_cluster_bench.cpp. One ray a pixel from a
// camera over the field of plants, then from each hit one ray in a direction of a hash (incoherent, as bounced light's
// rays are). Each ray writes what it hit: instance, geometry index, primitive index and distance, so the program can tell
// the plain structures' answers from the clusters' triangle by triangle, and time the same rays through both.
#define BenchRS "RootConstants(num32BitConstants=16, b0), SRV(t0), UAV(u0)"

RaytracingAccelerationStructure scene : register(t0);
RWStructuredBuffer<uint4> hits : register(u0);
cbuffer Camera : register(b0) {
    float3 position; uint width;
    float3 forward; uint height;
    float3 right; uint mode;   // 0: every triangle solid (RAY_FLAG_FORCE_OPAQUE); 1: every triangle a candidate, committed in the loop
    float3 up; uint seed;
};

uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

uint4 trace(float3 origin, float3 direction) {
    RayDesc ray; ray.Origin = origin; ray.Direction = direction; ray.TMin = 0.0; ray.TMax = 10000.0;
    uint4 result = uint4(0xFFFFFFFFu, 0, 0, asuint(-1.0));
    if (mode == 0) {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        q.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
        q.Proceed();
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            result = uint4(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), asuint(q.CommittedRayT()));
    } else {
        RayQuery<RAY_FLAG_NONE> q;
        q.TraceRayInline(scene, RAY_FLAG_NONE, 0xFF, ray);
        while (q.Proceed()) {
            if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) q.CommitNonOpaqueTriangleHit();
        }
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            result = uint4(q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(), asuint(q.CommittedRayT()));
    }
    return result;
}

[RootSignature(BenchRS)]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const uint pixel = id.y * width + id.x;
    const float2 ndc = (float2(id.xy) + 0.5) / float2(width, height) * 2.0 - 1.0;
    const float aspect = float(width) / float(height);
    const float3 direction = normalize(forward + ndc.x * aspect * 0.577 * right - ndc.y * 0.577 * up);
    const uint4 first = trace(position, direction);
    hits[pixel] = first;
    uint4 second = uint4(0xFFFFFFFFu, 0, 0, asuint(-1.0));
    if (first.x != 0xFFFFFFFFu) {
        const float3 at = position + direction * (asfloat(first.w) - 0.001);
        const uint h1 = hash(pixel * 3u + seed), h2 = hash(h1 ^ 0x9e3779b9u);
        const float z = float(h1 & 0xFFFFu) / 65535.0 * 2.0 - 1.0, a = float(h2 & 0xFFFFu) / 65535.0 * 6.2831853;
        const float r = sqrt(max(0.0, 1.0 - z * z));
        second = trace(at, float3(r * cos(a), z, r * sin(a)));
    }
    hits[width * height + pixel] = second;
}
