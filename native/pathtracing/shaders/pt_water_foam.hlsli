// SPDX-License-Identifier: MIT
#ifndef PT_WATER_FOAM_HLSLI
#define PT_WATER_FOAM_HLSLI
// The water's foam: how much of the surface at a point is foam. A material cover (a path meets foam there with that
// chance); its light comes from the traced paths.
//
// WHERE it forms is the game's simulation's to say: HDRP writes, for each band of waves, how much the waves' sideways move
// squeezes the surface (its Jacobian: 4 where the water is at rest, less where it is squeezed, as on a crest that
// steepens), and the surface has a threshold for the sum over its bands (4, 8 and 12 times a factor near 1 for one, two
// and three bands), an intensity and a mask. `past` below is how far the squeeze is past that threshold, in HDRP's own
// measure: 0 at the threshold, 1 where HDRP calls the foam full.
//
// HOW MUCH and WHAT IT LOOKS LIKE is our own (2026-10-08, the owner: "our own foam"; until then the last lines here were a
// port of Unity's foam erosion). Written from two things, neither of them shader code:
//   1. What the game's foam texture holds (HDRP's FoamMask, looked at channel by channel, docs/agents/water-7.md): alpha a
//      COARSE WEB (bright borders round dark cells of 20 to 50 cm at the sea's tiling), red a FINE WEB (cells of 3 to 6 cm),
//      green soft CLOUDS (0.4 to 0.8 m), blue a fine GRAIN.
//   2. How foam behaves on water. It is made where a wave breaks, as a dense white mat. Its bubbles burst from the middle
//      of each cell outwards: the borders between cells are the thickest and hold out longest, so an ageing mat opens into
//      a web, the web thins to threads, the threads break up, patchily, and are gone.
// So: the cover (0 to 1) is how much of the surface is still foam, and the texture says WHICH part: the places that hold
// out longest. Each channel is first turned into its share (how much of the texture lies under the texel's value, 0 to 1,
// even over the texture: foam_shares). A point is foam where the coarse web's share is in its top cover^0.6, the fine web's
// in its top cover^0.35 and the grain's in its top cover^0.05: the three are independent, so together they keep cover^1 of
// the surface, the cover itself. (The grain has the smallest part: with a fifth it cut every thread into specks of a few
// millimetres; so the threads are whole, 1 to 3 cm wide on the game's texture at the sea's tiling.) At a cover of 0.05 that is thin threads along the coarse web's thickest borders, at 0.3 a
// lace, at 0.8 a mat with holes. The clouds make foam older in some places and younger in others (the cover a little
// lower or higher there, the same on average).
//   No foam where the surface is merely not stretched: it starts FOAM_ONSET of the way from HDRP's threshold to its full
// foam, and is full where HDRP's is. FOAM_ONSET is set from the game's own picture of one sea (run 20261008-145509: the
// game has foam on 1 to 3 % of the open water, its most squeezed places; with the foam starting at HDRP's threshold the
// tracer had it on 46 %). It is the one number here that is fitted, to one scene: the plugin now logs the squeeze's own
// distribution for each surface, which is what can settle it.
//   The foam rides the water: it is laid over the undisplaced surface (so it moves with the waves) and drifts with the
// surface's current.
static const float FOAM_ONSET = 0.45;    // of HDRP's run from its threshold to its full foam: none before
static const float FOAM_PATCHES = 1.5;   // how unevenly foam ages (0: evenly; 2: the most that keeps the cover's mean)
static const float FOAM_EDGE = 0.04;     // how soft a web's edge is, in shares
static const float FOAM_DRIFT = 0.3;     // metres a second the foam drifts at full current influence (a wind-blown surface
                                         // moves at about 3 % of the wind's speed)
// The game's foam texture, channel by channel: the values at the texture's 0, 5, 10, 25, 50, 75, 90, 95 and 100 % (red,
// green, blue, alpha), measured over its 2048 x 2048 texels. Another foam texture has other values: its foam would cover
// somewhat more or less than the cover says.
static const float FOAM_AT[9] = { 0.0, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 1.0 };
static const float4 FOAM_VALUES[9] = {
    float4(0.094, 0.016, 0.000, 0.051), float4(0.290, 0.176, 0.322, 0.157), float4(0.290, 0.204, 0.388, 0.188),
    float4(0.322, 0.318, 0.482, 0.220), float4(0.420, 0.443, 0.569, 0.278), float4(0.612, 0.557, 0.647, 0.443),
    float4(0.804, 0.686, 0.710, 0.686), float4(0.839, 0.765, 0.741, 0.749), float4(1.000, 0.969, 0.937, 0.922) };
// How much of the texture lies under each channel's value: 0 to 1, even over the texture.
float4 foam_shares(float4 value) {
    float4 share = 0.0;
    for (uint i = 0; i < 8u; ++i)
        share += (FOAM_AT[i + 1] - FOAM_AT[i]) * saturate((value - FOAM_VALUES[i]) / max(FOAM_VALUES[i + 1] - FOAM_VALUES[i], 1e-5));
    return share;
}

// Shore foam: a local breaking-energy closure, not a port of the game's foam. The bed is a traced surface, not
// camera depth. The wave height/depth ratio gives depth-limited breaking; the speed scale sqrt(g H) converts slope
// and crest run-up to a production rate. Bubble coverage is 1-exp(-rate * 2 seconds). The two-second lifetime and
// onset 0.30..0.78 are model parameters, not measured for this beach. No stored camera-centred history is required.
// Current and previous wave positions keep the trailing crest's activity for one simulation frame; this does not
// claim a transported bubble simulation. The existing foam texture supplies the bubble shapes and current drift.
float shore_foam_cover(uint slot, float3 base) {
    if (!water_shore_foam_on()) return 0.0;
    float3 p, n; float lift, steep;
    water_displace(slot, base, water_row(slot, 10).xyz, p, n, lift, steep);
    const float height = 2.0 * max(abs(lift), abs(water_moved_before(slot, base, p).y - base.y));
    if (!(height > 1e-5) || !(water_row(slot, 12).x > 0.0)) return 0.0;
    RayDesc ray; ray.Origin = p; ray.Direction = float3(0.0, -1.0, 0.0); ray.TMin = 1e-4; ray.TMax = 1e30;
    // A material state cannot inherit whether its caller was a camera, mirror, shadow or bounce ray.
    // The bed query has its own fixed world-point alpha sample and finest texture footprint; restore every touched state.
    const bool camera = g_cameraRays, primary = g_primaryRays, mirror = g_mirrorRay, light = g_lightRay, bounce = g_bounceRay;
    const bool glassSolid = g_glassSolid, glassTint = g_glassTint;
    const uint seed = g_raySeed;
    const float3 rayOrigin = g_rayOrigin;
    const float width = g_coneWidth, spread = g_coneSpread, candidate = g_candidateT;
    g_cameraRays = false; g_primaryRays = false; g_mirrorRay = false; g_lightRay = false; g_bounceRay = false;
    g_glassSolid = true; g_glassTint = false;
    g_raySeed = pcg(asuint(p.x) ^ pcg(asuint(p.y) ^ pcg(asuint(p.z))));
    g_rayOrigin = p; g_coneWidth = 0.0; g_coneSpread = 0.0;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES) q;
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, MASK_SOLID, ray);
    while (q.Proceed()) {
        g_candidateT = q.CandidateTriangleRayT();
        if (alpha_opaque(q.CandidateInstanceID(), q.CandidateGeometryIndex(), q.CandidatePrimitiveIndex(), q.CandidateTriangleBarycentrics()))
            q.CommitNonOpaqueTriangleHit();
    }
    g_cameraRays = camera; g_primaryRays = primary; g_mirrorRay = mirror; g_lightRay = light; g_bounceRay = bounce;
    g_glassSolid = glassSolid; g_glassTint = glassTint; g_raySeed = seed; g_rayOrigin = rayOrigin;
    g_coneWidth = width; g_coneSpread = spread; g_candidateT = candidate;
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return 0.0;
    const float depth = q.CommittedRayT();
    const float breaking = smoothstep(0.30, 0.78, height / max(depth, 1e-4));
    const float crest = saturate(lift / max(0.5 * height, 1e-4));
    const float rate = breaking * sqrt(9.81 * height) * (steep + crest) / max(depth, 0.02);
    return 1.0 - exp(-2.0 * rate);
}

float water_foam(uint slot, float3 base) {
    const uint at = slot * 6;
    const float4 settings = g.waterFoam[at];
    const int4 maps = g.waterFoamMaps[slot];
    if (!water_shore_foam_on() && (!(settings.y > 0.0) || maps.x < 0 || g.waterNormals[slot] == INVALID)) return 0.0;
    const float4 info = water_row(slot, 12);
    float jacobian = 0.0;
    // Every band of the surface counts, the third too. HDRP's threshold (settings.x, its _SimulationFoamAmount) is made
    // for the sum over all of them: 4, 8 and 12 times a factor near 1 for one, two and three bands, each band's Jacobian
    // being 4 where its water is at rest. Summed over two bands of a three-band sea, the threshold of 11.35 to 12.23 stood
    // 3.35 to 4.23 over the sum at rest: full cover at every point, to the horizon (PT-81: the sea a pale sheet).
    for (uint band = 0; band < uint(info.x); ++band) {
        if (g.waterNormals[slot] == INVALID) break;
        Texture2DArray<float4> gradients = ResourceDescriptorHeap[NonUniformResourceIndex(g.waterNormals[slot])];
        const float4 u = water_row(slot, band * 3), v = water_row(slot, band * 3 + 1), extra = water_row(slot, band * 3 + 2);
        const float2 uv = float2(dot(u.xyz, base) + u.w, dot(v.xyz, base) + v.w);
        float fade = saturate(length(base - g.cameraPos.xyz) * extra.y + extra.z);
        fade *= fade; fade = fade * fade * (3.0 - 2.0 * fade);
        jacobian += lerp(4.0, bicubic(gradients, uv, band, info.y).z, fade);
    }
    const float2 local = float2(dot(g.waterFoam[at + 3], float4(base, 1)), dot(g.waterFoam[at + 4], float4(base, 1)));
    const float4 maskMap = g.waterFoam[at + 1];
    float mask = 1.0;
    if (maps.y >= 0) {
        Texture2D<float4> source = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + uint(maps.y))];
        mask = source.SampleLevel(s_linearClamp, local * maskMap.xy + maskMap.zw, 0).r;
    }
    // How far the squeeze is past HDRP's threshold (0 there, 1 at HDRP's full foam), and so how much of the surface is foam.
    const float past = settings.x - jacobian;
    const float crestCover = saturate((past - FOAM_ONSET) / (1.0 - FOAM_ONSET)) * saturate(settings.y) * saturate(mask);
    const float shoreCover = shore_foam_cover(slot, base);
    // The union of both bubble populations has no join or seam into the old offshore bands.
    const float cover = water_shore_foam_on() ? 1.0 - (1.0 - crestCover) * (1.0 - shoreCover) : crestCover;
    if (!(cover > 0.0)) return 0.0;
    if (maps.x < 0) return cover; // no game foam texture: the same coverage, with stochastic material selection
    // Which part: the texture, laid over the undisplaced water and drifting with the current (its direction, the time, how
    // much the surface lets the current carry its foam).
    const float4 flow = g.waterFoam[at + 2];
    const float2 drift = frac(flow.xy * (flow.z * flow.w * FOAM_DRIFT * settings.w));
    Texture2D<float4> texture = ResourceDescriptorHeap[NonUniformResourceIndex(g.textures.x + uint(maps.x))];
    const float4 share = foam_shares(texture.SampleLevel(s_linearRepeat, local * settings.w - drift, 0)); // r fine web, g clouds, b grain, a coarse web
    const float here = saturate(cover + (share.g - 0.5) * FOAM_PATCHES * cover * (1.0 - cover));
    // What each of the three keeps: the coarse web its top here^0.6, the fine web its top here^0.35, the grain its top
    // here^0.05 (a soft edge of FOAM_EDGE; nothing at a cover of 0 and all at 1).
    const float3 kept = pow(here, float3(0.6, 0.35, 0.05));
    const float3 from = lerp(1.0 + 0.5 * FOAM_EDGE, -0.5 * FOAM_EDGE, kept);
    const float3 held = saturate((float3(share.a, share.r, share.b) - from) / FOAM_EDGE + 0.5);
    return held.x * held.y * held.z;
}
#endif
