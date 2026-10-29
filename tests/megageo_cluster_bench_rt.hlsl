// SPDX-License-Identifier: MIT
// PT-109 part 2, step (a) (docs/agents/megageo-2.md): the same rays as megageo_cluster_bench.hlsl, traced with TraceRay
// from a ray generation shader (as the tracer's big passes trace), to tell whether the clusters' cost is the ray query's.
// The global root signature is the compute pass's (the program passes it as a subobject).
RaytracingAccelerationStructure scene : register(t0);
RWStructuredBuffer<uint4> hits : register(u0);
cbuffer Camera : register(b0) {
    float3 position; uint width;
    float3 forward; uint height;
    float3 right; uint mode;   // 0: every triangle solid (RAY_FLAG_FORCE_OPAQUE); 1: every triangle a candidate, accepted by the any-hit shader
    float3 up; uint seed;
};
struct Payload { uint4 hit; };

RaytracingShaderConfig config = { 16, 8 };
RaytracingPipelineConfig pipeline = { 1 };
TriangleHitGroup group = { "AnyHit", "ClosestHit" };

uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

uint4 trace(float3 origin, float3 direction) {
    RayDesc ray; ray.Origin = origin; ray.Direction = direction; ray.TMin = 0.0; ray.TMax = 10000.0;
    Payload p; p.hit = uint4(0xFFFFFFFFu, 0, 0, asuint(-1.0));
    TraceRay(scene, mode == 0 ? RAY_FLAG_FORCE_OPAQUE : RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, p);
    return p.hit;
}

[shader("raygeneration")]
void RayGen() {
    const uint2 id = DispatchRaysIndex().xy;
    const uint pixel = id.y * width + id.x;
    const float2 ndc = (float2(id) + 0.5) / float2(width, height) * 2.0 - 1.0;
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

[shader("miss")]
void Miss(inout Payload p) { }

[shader("closesthit")]
void ClosestHit(inout Payload p, in BuiltInTriangleIntersectionAttributes a) {
    p.hit = uint4(InstanceID(), GeometryIndex(), PrimitiveIndex(), asuint(RayTCurrent()));
}

[shader("anyhit")]
void AnyHit(inout Payload p, in BuiltInTriangleIntersectionAttributes a) { }
