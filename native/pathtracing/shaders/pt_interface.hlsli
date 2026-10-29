// SPDX-License-Identifier: MIT
// One boundary between two clear media, for glass and for water alike (pt_dielectric.hlsli's glass, pt_water_path.hlsli's
// water, pt_caustics.hlsl's light through the waves): exact Fresnel, Snell's law, total internal reflection, and what a
// medium takes from a ray over its way. Directions point along the path, from its last vertex towards the boundary.
// Included by pt_common before the traversal; it reads no pixel and traces nothing.
#ifndef PT_INTERFACE_HLSLI
#define PT_INTERFACE_HLSLI

static const float WATER_IOR = 1.3333; // HDRP's water index

// Unpolarized Fresnel reflectance of a dielectric interface; eta: the far side's index over the near side's.
float fresnel_dielectric(float cosi, float eta) {
    cosi = saturate(abs(cosi));
    const float sint2 = (1.0 - cosi * cosi) / (eta * eta);
    if (sint2 >= 1.0) return 1.0;
    const float cost = sqrt(1.0 - sint2);
    const float rs = (cosi - eta * cost) / (cosi + eta * cost), rp = (eta * cosi - cost) / (eta * cosi + cost);
    return 0.5 * (rs * rs + rp * rp);
}

struct Interface {
    float3 reflected;
    float3 transmitted; // zero at total internal reflection
    float reflectance;  // the share of the power that is reflected (1 at total internal reflection, 0 between equal media)
    float eta;          // the near side's index over the far side's
};
// facing: the boundary's normal on the side the path comes from (a microfacet's for rough glass). ni, nt: the index on that
// side and beyond it.
Interface dielectric_interface(float3 direction, float3 facing, float ni, float nt) {
    Interface r;
    r.eta = ni / nt;
    r.reflected = reflect(direction, facing);
    r.reflectance = ni == nt ? 0.0 : fresnel_dielectric(dot(direction, facing), nt / ni);
    r.transmitted = r.reflectance >= 1.0 ? 0.0 : refract(direction, facing, r.eta);
    return r;
}
// What a camera path's radiance takes on when it crosses the boundary: the indices' ratio squared (the same light fills a
// narrower or wider cone beyond it). A photon's power takes none. Looking from air at a glowing bed: 1 / n squared.
float interface_radiance(Interface b) { return b.eta * b.eta; }

// What is left of a ray after a way through a medium: extinction is its absorption and what it scatters away, per metre.
// Per segment, before paths are summed; what is scattered in comes from events in the medium, never from its boundary.
float3 segment_transmittance(float3 extinction, float distance) {
    return exp(-max(extinction, 0.0) * max(distance, 0.0));
}
#endif
