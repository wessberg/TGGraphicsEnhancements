// SPDX-License-Identifier: MIT
// PT-53 / PT-56 / PT-57 / PT-112 / PT-59 / PT-84: The tracer owns the first camera hit and its geometry
// guides for every pixel.

// ---------------------------------------------------------------- hair's strands
// A hair material's strands (tracer.cpp bake_hair, FrameConstants.strandTiles by its color tile's detail index): its block
// of the strand atlas (origin and size in texels; size 0: none, its color tile's alpha then), its alpha test (RealHair's
// _AlphaClip and _DepthPrepass), its diffuse map's tiling and offset; ready once its color was baked.
struct StrandTile { uint2 origin; uint size; float clip; float prepass; float4 tiling; bool ready; };
StrandTile strand_tile(MaterialRecord m) {
    StrandTile t;
    t.origin = 0; t.size = 0; t.clip = 0.1; t.prepass = 0.5; t.tiling = float4(1.0, 1.0, 0.0, 0.0); t.ready = false;
    if (m.albedoTile < int(MAX_TILES)) return t;
    const uint d = uint(m.albedoTile) - MAX_TILES;
    if (d >= DETAIL_TILES) return t;
    const float4 a = g.strandTiles[d * 2u], b = g.strandTiles[d * 2u + 1u];
    t.origin = uint2(asuint(a.x) & 0xFFFFu, asuint(a.x) >> 16);
    t.size = asuint(a.y) & 0x7FFFFFFFu;
    t.ready = (asuint(a.y) >> 31) != 0u;
    t.clip = a.z; t.prepass = a.w; t.tiling = b;
    if (t.size != 0u && g.hair.z == INVALID) t.size = 0u;
    return t;
}
// The strands' alpha at one level of their block, filtered bilinearly within it (wrapping, as the texture repeats).
float strand_level(StrandTile t, float2 st, uint level) {
    Texture2D<float> strands = ResourceDescriptorHeap[g.hair.z];
    const uint size = max(t.size >> level, 1u);
    const uint2 origin = t.origin >> level;
    const float2 x = st * float(size) - 0.5;
    const float2 f = frac(x);
    const int2 i0 = int2(floor(x));
    const uint2 a = uint2(i0) & (size - 1u), b = uint2(i0 + 1) & (size - 1u);
    const float a00 = strands.Load(int3(origin + uint2(a.x, a.y), level)), a10 = strands.Load(int3(origin + uint2(b.x, a.y), level));
    const float a01 = strands.Load(int3(origin + uint2(a.x, b.y), level)), a11 = strands.Load(int3(origin + uint2(b.x, b.y), level));
    return lerp(lerp(a00, a10, f.x), lerp(a01, a11, f.x), f.y);
}
// RealHair's alpha from its texture's, as the game's shader takes it (its depth and forward passes alike, read from the
// compiled shader): pow(saturate(a / _AlphaRemap), _AlphaPower), its two values in TGPT_Material.hairAlpha (each a half; 0:
// 1). The game's hair has _AlphaPower 0.5 (Tex_H_m008) and 0.25 (Tex_H_f001) besides 1: a texel of 0.25 is 0.5 and 0.71 of
// the pixel there. Taken as the texture's alpha, such hair let the surface behind show through its strands.
float hair_game_alpha(MaterialRecord m, float a) {
    const float power = f16tof32(m.hairAlpha & 0xFFFFu), remap = f16tof32(m.hairAlpha >> 16);
    a = saturate(a / (remap > 0.0 ? remap : 1.0));
    return power > 0.0 && power != 1.0 ? pow(a, power) : a;
}
// The hair's alpha at a point of its mesh's own coordinates, lod levels down from its block's full size (two levels blended),
// as the game's shader takes it (hair_game_alpha); without a block its color tile's (512x512, no mips). Anisotropic as the
// game's sampler (STRAND_ANISO): taps texels along the footprint's long axis (along: in the block's coordinates, from end
// to end), each at the level of its short one.
float strand_alpha(MaterialRecord m, StrandTile t, float2 uv, float lod, float2 along, uint taps) {
    if (t.size == 0u) return hair_game_alpha(m, m.baseColor.a * atlas_sample(m.albedoTile, uv * m.uvST.xy + m.uvST.zw).a);
    const float2 st = frac(uv * t.tiling.xy + t.tiling.zw);
    const float top = log2(float(t.size));
    lod = clamp(lod, 0.0, top);
    const uint l0 = uint(lod);
    const bool second = float(l0) + 1.0 <= top;
    float a = 0.0;
    for (uint k = 0; k < taps; ++k) {
        const float2 at = st + along * ((float(k) + 0.5) / float(taps) - 0.5);
        const float a0 = strand_level(t, at, l0);
        a += second ? lerp(a0, strand_level(t, at, l0 + 1u), lod - float(l0)) : a0;
    }
    return hair_game_alpha(m, m.baseColor.a * a / float(taps));
}
// How much of the pixel a texel of hair covers, as the game blends it: its alpha (strand_alpha), and all of it where that is at
// least the depth prepass (RealHair's forward pass outputs 1 there, read from its compiled shader: max(alpha, alpha >=
// _DepthPrepass)). Kept at its alpha there (until 2026-10-06), each such texel let the rest of the pixel show the surface HDRP
// drew behind the hair: the scalp in the hair's shade, black gaps between flat clumps.
float strand_cover(StrandTile t, float a) { return t.prepass > 0.0 && a >= t.prepass ? 1.0 : a; }
// A triangle of a hair instance: its corners in the world and their texture coordinates.
struct Tri { float3 w0, w1, w2; float2 t0, t1, t2; };
Tri load_tri(uint instanceId, uint geometry, uint primitive, float3x4 toWorld) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const MeshRecord mesh = meshes[instances[instanceId].mesh];
    const uint3 idx = triangle_indices(mesh, geometry, primitive);
    ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
    Tri r;
    r.w0 = mul(toWorld, float4(asfloat(pb.Load3(idx.x * 12)), 1.0));
    r.w1 = mul(toWorld, float4(asfloat(pb.Load3(idx.y * 12)), 1.0));
    r.w2 = mul(toWorld, float4(asfloat(pb.Load3(idx.z * 12)), 1.0));
    r.t0 = 0.0; r.t1 = 0.0; r.t2 = 0.0;
    if (mesh.uvs != INVALID) {
        ByteAddressBuffer ub = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.uvs)];
        r.t0 = asfloat(ub.Load2(idx.x * 8)); r.t1 = asfloat(ub.Load2(idx.y * 8)); r.t2 = asfloat(ub.Load2(idx.z * 8));
    }
    return r;
}
// The game reads hair's textures anisotropically: its quality setting forces anisotropic filtering on every texture (Ultra's
// anisotropicTextures 2, from globalgamemanagers; Unity's forced minimum level 9; the hair maps' own level is 1). So did the
// tracer's other readers of the game's textures (PT-46, s_anisoWrap), but not the strands: they were read at the geometric
// mean of the footprint's two sides, one level or two blurrier than the game where a card turns from the camera (the crown,
// the top of a head seen from the side), and at the top of a head their strands ran together into a thin even veil.
static const uint STRAND_ANISO = 9;
// Where a pixel's ray meets a triangle dist along it (a ray cone): the pixel's footprint there (its angle times the distance,
// stretched along the ray's slant) in texels of the block, through the triangle's own map from its plane to its texture
// coordinates; the level of its short side, the long side's length (at most STRAND_ANISO times the short one's level), less
// an upscaler's bias (g.hair.w), which HDRP's textures take too: the output's finer pixels get the finer strands, as the
// game's hair shows them.
struct StrandFootprint { float lod; float2 along; uint taps; };
StrandFootprint strand_footprint(StrandTile t, Tri tri, float3 dir, float dist, float pixelAngle) {
    StrandFootprint f;
    f.lod = 0.0; f.along = 0.0; f.taps = 1u;
    const float3 e1 = tri.w1 - tri.w0, e2 = tri.w2 - tri.w0;
    const float3 n = cross(e1, e2);
    const float area = length(n);
    const float2 d1 = (tri.t1 - tri.t0) * t.tiling.xy, d2 = (tri.t2 - tri.t0) * t.tiling.xy;
    const float g00 = dot(e1, e1), g01 = dot(e1, e2), g11 = dot(e2, e2), gd = g00 * g11 - g01 * g01;
    if (!(area > 1e-12) || !(gd > 1e-30) || t.size == 0u) return f;
    const float3 nn = n / area;
    const float c = dot(dir, nn);
    float3 slope = dir - nn * c;
    slope = dot(slope, slope) > 1e-12 ? normalize(slope) : normalize(e1);
    const float width = dist * pixelAngle;
    const float3 ax = slope * (width / max(abs(c), 0.05)), ay = cross(nn, slope) * width;
    float2 ab = float2(dot(ax, e1) * g11 - dot(ax, e2) * g01, dot(ax, e2) * g00 - dot(ax, e1) * g01) / gd;
    const float2 u = ab.x * d1 + ab.y * d2;
    ab = float2(dot(ay, e1) * g11 - dot(ay, e2) * g01, dot(ay, e2) * g00 - dot(ay, e1) * g01) / gd;
    const float2 v = ab.x * d1 + ab.y * d2;
    const float lu = length(u) * float(t.size), lv = length(v) * float(t.size);
    const float major = max(lu, lv), minor = max(min(lu, lv), 1e-6);
    f.taps = uint(clamp(ceil(major / minor - 0.01), 1.0, float(STRAND_ANISO)));
    f.along = f.taps > 1u ? (lu >= lv ? u : v) : 0.0;
    f.lod = log2(max(max(major / float(f.taps), minor), 1e-6)) + asfloat(g.hair.w);
    return f;
}
float tri_alpha(MaterialRecord m, StrandTile t, Tri tri, float2 bary, float3 dir, float dist, float pixelAngle) {
    const float2 uv = tri.t0 * (1.0 - bary.x - bary.y) + tri.t1 * bary.x + tri.t2 * bary.y;
    const StrandFootprint f = strand_footprint(t, tri, dir, dist, pixelAngle);
    return strand_alpha(m, t, uv, f.lod, f.along, f.taps);
}
// The strand's normal with the game's normal map, as RealHair reads it (its compiled forward pass: _NormalMap at the mesh's own
// coordinates, x its red times its alpha and y its green, each 2v - 1, times _NormalStrength, and z lerp(1, sqrt(1 - x^2 - y^2),
// the strength); the strength reaches the tracer as the material's normalScale), laid in the card's texture frame (x along u,
// y along v) about nv, the strand's normal facing the camera that the tracer lights it about, where the game has the mesh's
// own: the tracer has no vertex normals for characters, and the cards' flat faces lit each card as one piece. nv where the
// map is not shared.
float3 hair_normal(MaterialRecord m, Hit hit, float3 tu, float3 b, float3 nv) {
    if (!has_shared(m, m.normalTexture)) return nv;
    const float4 t = shared_sample(m.normalTexture, hit.uv, hit.uvGradX, hit.uvGradY);
    float2 xy = float2(t.r * t.a, t.g) * 2.0 - 1.0;
    const float z = lerp(1.0, sqrt(max(1.0 - min(dot(xy, xy), 1.0), 0.0)), saturate(m.normalScale));
    xy *= m.normalScale;
    float3 x = tu - nv * dot(nv, tu);
    if (!(dot(x, x) > 1e-12)) return nv;
    x = normalize(x);
    float3 y = cross(nv, x);
    if (dot(y, b) < 0.0) y = -y;
    const float3 w = x * xy.x + y * xy.y + nv * z;
    return dot(w, w) > 1e-12 ? normalize(w) : nv;
}

// ---------------------------------------------------------------- the camera ray
// A triangle where a camera ray crossed it: how far along the ray; for a hair card, how much of the pixel its texel covers.
struct CardHit { bool found; float t; float cover; uint instance, geometry, primitive; float2 bary; float3x4 toWorld; };
CardHit card_hit(float t, float cover, uint instance, uint geometry, uint primitive, float2 bary, float3x4 toWorld) {
    CardHit c;
    c.found = true; c.t = t; c.cover = cover; c.instance = instance; c.geometry = geometry; c.primitive = primitive; c.bary = bary; c.toWorld = toWorld;
    return c;
}
// What a camera ray met on its way (primary_walk).
struct PrimaryWalk {
    bool stopped;  // it stopped at something (stop): the pixel's surface
    bool hair;     // at a hair card, by the chance its texel covers
    CardHit stop;
    bool met;      // it crossed at least one hair card whose texel shows, stopped there or not: first is the nearest such
    CardHit first;
    // A card of such hair lies within `within` of the distance `at`, whatever its texel's alpha here: the card HDRP drew into
    // its depth at this pixel (HDRP's own alpha test let it through, at its own sampling of the texture; the tracer's reading
    // of the same texel may fall just under the clip, and the ray then passes it like any other it does not stop at).
    bool at;
};
// The camera ray of a pixel, from the camera as far as tmax: the tracer's own first hit. Every hair card it crosses
// (TGPT_MATERIAL_HAIR_LAYER, its texel at least its alpha clip) stops it with the chance that texel covers (strand_cover; the
// draw is ray_chance's: the ray's own, the same whenever traversal reports the triangle again and in a second walk along the
// same ray in the same frame, another every frame); everything else as a camera ray meets it (alpha_opaque with
// g_cameraRays, which the caller sets). The nearest thing that stops it is where it stopped. everything: the whole scene
// (MASK_ALL); else the instances holding such hair alone (MASK_STRANDS), which is all a pixel needs to know while HDRP's
// G-buffer still gives the rest. Walking everything, the ray is the camera's in full: it starts at the near plane (tmin),
// passes what the camera does not draw (a shadow-only stand-in; the back of a one-sided surface, which HDRP culls: the
// inside of a closed mesh, the inside of the hero's first-person body), and what covers part of a pixel stops it by chance
// as hair does (g_primaryRays: a cut-out's alpha tile, hair without a strand block).
// A shell under the hair (a scalp or beard mask: MATERIAL_CAMERA_ONLY with the hair bit) is passed: HDRP draws it into its
// G-buffer now that the hair's renderer is drawn again, a surface like the skin it is painted on; walking everything it is
// what it is in the traced scene, a surface only the camera sees.
static const uint PRIMARY_SALT = 0x7072696Du;
// Whether a surface that is no hair card stops the camera ray: as a camera ray meets it (alpha_opaque); walking everything a
// shell under hair, which alpha_opaque lets every camera ray pass (it was the hair layer's): the game blends it over the
// skin by its alpha, so it stops the ray as often as that (from its cut on), as a hair card does. By its cut alone, the
// game's 0.05, its faint edge was solid: the black hairline of hair held opaque at the blending cuts (2026-09-28).
bool primary_stops(MaterialRecord m, uint inst, uint geo, uint prim, float2 bary, bool everything) {
    if (everything && (m.flags & MATERIAL_HAIR_LAYER) != 0) {
        StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
        const float a = material_alpha(instances[inst], m, geo, prim, bary);
        return a >= m.alphaCutoff && ray_chance(inst, geo, prim, PRIMARY_SALT) < a;
    }
    return alpha_opaque(inst, geo, prim, bary);
}
PrimaryWalk primary_walk(float3 dir, float tmin, float tmax, bool everything, float pixelAngle, float at, float within) {
    ray_seed(g.cameraPos.xyz, dir);
    RayDesc ray; ray.Origin = g.cameraPos.xyz; ray.Direction = dir; ray.TMin = tmin; ray.TMax = tmax;
    PT_RAYQUERY(RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_NON_OPAQUE) q;
    const uint mask = everything ? MASK_SOLID : MASK_STRANDS; // the water's surface: primary_all's own query
    q.TraceRayInline(g_scene, RAY_FLAG_NONE, mask, ray);
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<uint> materialIndices = ResourceDescriptorHeap[g.scene1.x];
    PrimaryWalk w = (PrimaryWalk)0;
    w.first.t = 1e30;
    float cover = 0.0; // the committed candidate's: each commit is nearer than the last
    bool hair = false;
    while (q.Proceed()) {
        if (q.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE) continue;
        const uint inst = q.CandidateInstanceID(), geo = q.CandidateGeometryIndex(), prim = q.CandidatePrimitiveIndex();
        const InstanceRecord record = instances[inst];
        const MaterialRecord m = materials[materialIndices[record.materialBase + geo]];
        const float2 bary = q.CandidateTriangleBarycentrics();
        const float dist = q.CandidateTriangleRayT();
        if ((m.flags & MATERIAL_HAIR_LAYER) != 0 && ((m.flags & MATERIAL_CAMERA_ONLY) == 0 || !everything)) {
            if ((m.flags & MATERIAL_CAMERA_ONLY) != 0) continue;
            const StrandTile t = strand_tile(m);
            if (!t.ready) continue;
            if (abs(dist - at) <= within) w.at = true;
            const float3x4 toWorld = q.CandidateObjectToWorld3x4();
            const float a = tri_alpha(m, t, load_tri(inst, geo, prim, toWorld), bary, dir, dist, pixelAngle);
            if (!(a > 0.0) || a < t.clip) continue;
            const float c = strand_cover(t, a);
            if (dist < w.first.t) { w.met = true; w.first = card_hit(dist, c, inst, geo, prim, bary, toWorld); }
            if (ray_chance(inst, geo, prim, PRIMARY_SALT) < c) { cover = c; hair = true; q.CommitNonOpaqueTriangleHit(); }
        } else {
            if (everything && ((record.flags & INSTANCE_STAND_IN) != 0 ||
                               (!q.CandidateTriangleFrontFace() && (m.flags & (MATERIAL_DOUBLE_SIDED | MATERIAL_GLASS)) == 0))) continue;
            // (The inside of the hero's first-person body, the back of a head the game never draws from in front, is such a
            // back face: passed here at any distance, as every ray that shades what it meets passes it, pt_common
            // first_person_inside. Its front faces, a hand, the collar, are met, as HDRP draws them.)
            g_candidateT = dist;
            g_primaryRays = everything;
            const bool stops = primary_stops(m, inst, geo, prim, bary, everything);
            g_primaryRays = false;
            if (stops) { cover = 1.0; hair = false; q.CommitNonOpaqueTriangleHit(); }
        }
    }
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return w;
    w.stopped = true; w.hair = hair;
    w.stop = card_hit(q.CommittedRayT(), cover, q.CommittedInstanceID(), q.CommittedGeometryIndex(), q.CommittedPrimitiveIndex(),
                      q.CommittedTriangleBarycentrics(), q.CommittedObjectToWorld3x4());
    return w;
}

// ---------------------------------------------------------------- a hit as the pixel's surface
// A hit's own motion to the previous frame, as HDRP writes its motion vectors (texture coordinates, this frame's position
// less the previous one's, without jitter): where the point is now and where the same point of the same triangle was a frame
// ago, each through its frame's view-projection. A character's triangle by its previous skinned positions (MeshRecord.previous,
// pt_skin), a swaying cloth's by its previous pose (pt_sway modes 3 and 4), a group of plants in the wind's by each vertex's
// move since the previous frame (MESH_PREVIOUS_DELTA: three halves a vertex, where it was less where it is, pt_sway step 5);
// a thing the game moves (a weapon in a hand, a door: TGPT_SetMoving) by the transform it had a frame before
// (InstanceRecord.previous), its point taken back into its own space by this frame's; anything else is still in the world.
// Each by its MOVE added to the hit, never by its previous point placed anew (round 20): a character's positions are the
// world's, kilometres from the scene's origin, where a float resolves half a millimetre, and the previous point placed by
// the instance's transform against the ray's own hit made a still character move a few hundredths of a pixel, in a moire of
// roundings (the town's soldier, runs 20261011-020452 and -021316): with HDRP drawing nothing those are DLSS's motion
// vectors, and it dropped its history on him (flicker 0.30 against 0.22). A corner's move, where it was less where it is,
// is a difference of two nearby floats: exact, and exactly nothing for what stands still.
#define MESH_PREVIOUS_DELTA 0x40000000u
float3 vertex_move(ByteAddressBuffer moves, uint v) {
    const uint2 h = moves.Load2(v * 8u);
    return float3(f16tof32(h.x), f16tof32(h.x >> 16), f16tof32(h.y));
}
// z (since 2026-10-11, PT-127): the view depth's change, the previous frame's minus this one's, of the same point (its own
// previous position, a moving thing's too): the third value of the "2.5D" motion NRD's README recommends and AMD's Ray
// Regeneration asks for (pt_common motion_depth_change).
float3 point_motion(float3 now, float3 was) {
    const float4 c = mul(g.motionViewProj, float4(now - g.cameraPos.xyz, 1.0)), q = mul(g.prevViewProj, float4(was - g.cameraPos.xyz, 1.0));
    if (!(c.w > 1e-6 && q.w > 1e-6)) return 0.0;
    float2 v = c.xy / c.w - q.xy / q.w;
    v.y = -v.y;
    return float3(v * 0.5, q.w - c.w);
}
float3 hit_motion(CardHit h, float3 position) {
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const InstanceRecord record = instances[h.instance];
    const MeshRecord mesh = meshes[record.mesh];
    float3 was = position;
    if (mesh.previous != INVALID && (mesh.previous & MESH_PREVIOUS_DELTA) != 0) {
        const uint3 idx = triangle_indices(mesh, h.geometry, h.primitive);
        ByteAddressBuffer moves = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.previous & ~MESH_PREVIOUS_DELTA)];
        const float3 d = vertex_move(moves, idx.x) * (1.0 - h.bary.x - h.bary.y) + vertex_move(moves, idx.y) * h.bary.x + vertex_move(moves, idx.z) * h.bary.y;
        was = position + mul((float3x3)h.toWorld, d);
    } else if (mesh.previous != INVALID) {
        const uint3 idx = triangle_indices(mesh, h.geometry, h.primitive);
        ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.previous)];
        ByteAddressBuffer cb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
        const float3 d = (asfloat(pb.Load3(idx.x * 12)) - asfloat(cb.Load3(idx.x * 12))) * (1.0 - h.bary.x - h.bary.y) +
                         (asfloat(pb.Load3(idx.y * 12)) - asfloat(cb.Load3(idx.y * 12))) * h.bary.x +
                         (asfloat(pb.Load3(idx.z * 12)) - asfloat(cb.Load3(idx.z * 12))) * h.bary.y;
        was = position + mul((float3x3)h.toWorld, d);
    } else if (record.previous != INVALID && g.primary.w != INVALID) {
        const uint3 idx = triangle_indices(mesh, h.geometry, h.primitive);
        ByteAddressBuffer pb = ResourceDescriptorHeap[NonUniformResourceIndex(mesh.positions)];
        const float3 p = asfloat(pb.Load3(idx.x * 12)) * (1.0 - h.bary.x - h.bary.y) + asfloat(pb.Load3(idx.y * 12)) * h.bary.x + asfloat(pb.Load3(idx.z * 12)) * h.bary.y;
        StructuredBuffer<float4> rows = ResourceDescriptorHeap[g.primary.w];
        const float3x4 before = float3x4(rows[record.previous * 3u], rows[record.previous * 3u + 1u], rows[record.previous * 3u + 2u]);
        was = position + (mul(before, float4(p, 1.0)) - mul(h.toWorld, float4(p, 1.0)));
    }
    return point_motion(position, was);
}
// The tracer's own outputs beside the surface record (FrameConstants.primary, where they exist): the pixel's motion and its
// hit's emission (pt_compose adds it at HDRP's exposure, as the composite added HDRP's GBuffer3).
void write_own(uint2 p, float3 motion, float3 emission) {
    if (g.primary.x != INVALID) { RWTexture2D<float4> m = ResourceDescriptorHeap[g.primary.x]; m[p] = float4(motion, 0.0); }
    if (g.primary.y != INVALID) { RWTexture2D<float4> e = ResourceDescriptorHeap[g.primary.y]; e[p] = float4(max(emission, 0.0), float(water_medium(g.cameraPos.xyz) + 1)); }
}
// Hair where a camera ray stopped at one of its cards: the strand's direction (its flow map's in the card's texture frame:
// the mesh's tangent along u, its bitangent along v; along -v, root to tip, as the game's cards lie, without one), the normal
// it is lit about (the strand's own facing the camera, with the game's normal map: hair_normal), its colour, and its finish:
// with its highlights as rough as HDRP's hair takes its surroundings' light (its second lobe's roughness times 0.4) at hair's
// reflectance, else its material's. specular: its specular albedo as Ray Reconstruction's guide takes it (times its specular
// map's texel, the game's fine streaks).
struct HairPoint { Hit hit; float3 strand; float3 normal; float3 albedo; float roughness; float3 f0; bool lobes; float2 lobe; float specular; };
HairPoint hair_point(CardHit h, float3 dir) {
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    HairPoint r;
    r.hit = fetch_hit(h.instance, h.geometry, h.primitive, h.bary, h.toWorld, dir, g.cameraPos.xyz + dir * h.t, g.cameraPos.xyz);
    g_hitToWorld = h.toWorld;
    const MaterialRecord m = materials[r.hit.material];
    float3 strand = 0.0, tu = 0.0, b = 0.0;
    float2 lobe = float2(1.0, 0.5);
    const bool lobes = (m.flags & MATERIAL_HAIR_LOBES) != 0 && m.hairLobeTile >= 0;
    float3 dpdu, dpdv;
    const float3 n = r.hit.normal;
    if (uv_frame(r.hit, dpdu, dpdv)) {
        const float3 along = dpdu - n * dot(n, dpdu);
        if (dot(along, along) > 1e-20) {
            tu = normalize(along);
            b = cross(n, tu);
            if (dot(b, dpdv) < 0.0) b = -b;
            float2 f = float2(0.0, -1.0);
            if (lobes) { const float4 l = hair_lobe(m.hairLobeTile, r.hit); f = l.xy * 2.0 - 1.0; lobe = float2(l.w, l.z); }
            strand = f.x * tu + f.y * b + sqrt(1.0 - min(dot(f, f), 1.0)) * n;
        }
    }
    if (dot(strand, strand) < 1e-12) { float3 bt; basis(n, strand, bt); }
    strand = normalize(strand);
    const SurfaceMaterial traced = hit_material(r.hit);
    const float3 v = -dir;
    float3 nv = v - strand * dot(strand, v);
    nv = dot(nv, nv) > 1e-8 ? normalize(nv) : v;
    r.strand = strand;
    r.normal = dot(tu, tu) > 0.0 ? hair_normal(m, r.hit, tu, b, nv) : nv;
    r.albedo = traced.albedo;
    r.lobes = lobes; r.lobe = lobe;
    r.roughness = lobes ? saturate((1.0 - m.hairSmooth2) * 0.4) : traced.roughness;
    r.f0 = lobes ? (float3)HAIR_F0 : traced.f0;
    r.specular = dot(specular_albedo(r.f0, r.roughness, saturate(dot(r.normal, v))), 1.0 / 3.0) * (lobes ? lobe.x : 1.0);
    return r;
}
// The pixel's surface written as pt_prepare writes one from HDRP's G-buffer (load_surface reads either the same way):
// position and its distance from the camera (the hit's own with the tracer's own rays for every pixel; HDRP's at the pixel
// on the route before them), the shading normal and roughness, the triangle's own normal,
// the colour (alpha 1: lit by its traced colour, none in the G-buffer), the reflectance and the flags.
// Hair: lit through from behind and about its strand (bit 4, and its strand with HAIR_STRAND in the hair texture: pt_common
// strand_cos), with its highlights where it has them (bit 64, its specular map's and smoothness's texel in the bytes);
// a complex surface (bit 1: thin, finely detailed).
void write_hair(uint2 p, HairPoint h, float distance) {
    RWTexture2D<float4> surf = ResourceDescriptorHeap[g.surface.x];
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
    RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];
    RWTexture2D<uint2> hairTex = ResourceDescriptorHeap[g.hair.x];
    surf[p] = float4(h.hit.position, distance);
    nrm[p] = uint2(pack_normal(h.normal), (uint(h.roughness * 255.0 + 0.5) & 0xFF) | 0x100u | 0x200u | (pack_normal22(h.hit.normal) << 10));
    const float3 specularBytes = h.lobes ? float3(h.lobe, 0.0) : linear_to_srgb(saturate(h.f0));
    mat[p] = uint2(pack_rgba8(float4(linear_to_srgb(saturate(h.albedo)), 1.0)),
                   pack_rgba8(float4(specularBytes, float(1u | 4u | (h.lobes ? 64u : 0u)) / 255.0)));
    hairTex[p] = uint2(pack_normal(h.strand), h.hit.material | HAIR_STRAND);
}
// Anything else the tracer's own camera ray stopped at (what lies behind a hair card HDRP drew and the ray passed: the
// scalp, a collar, the wall past a head; with primary rays for every pixel, any surface): shaded from the traced scene alone,
// as pt_prepare shades a surface HDRP draws forward. Its colour, reflectance and roughness are the traced material's (its
// own textures where they are shared, PT-46), its normal the mesh's smooth one facing the camera, its rays leave from its own
// triangle. An eye is marked an eye (lit on its mesh); HDRP's Unlit reflects nothing; thin cloth and leaves are lit through.
// Reflection Boost is left off a character (what a ray meets behind hair is mostly skin, which the boost exempts).
// Its normal the mesh's smooth one with the game's own normal map where that is shared (material_normal, as a path vertex is
// lit), facing the camera. Glass in the traced scene (MATERIAL_GLASS) is seen through where its texel is glossy, as pt_prepare
// marks it from the G-buffer. Its emission goes to the tracer's own output (write_own).
// Read from the hit by the passes after this one, by the material the ray met (pt_common own_material, PT-58): light inside
// skin and wax (pt_walk: a random walk from this very hit, which is also what a thin ear or nostril lets through), a leaf's
// own share of the light from behind (leaf_transmittance), and the wet ground (hit_material, here).
// Not yet read from a hit, which the G-buffer route has: skin's softened terminator and cavity occlusion (its highlight's
// alone now: its diffuse light is the walk's), a leaf's thickness map and occlusion (its share is one number a material),
// decals, terrain's blended layers, parallax relief. The yardstick shows where.
// Returns what it read of the surface (the yardstick compares it with HDRP's) and the material's emission there.
// What the tracer's record says of the surface, before Reflection Boost (which both routes apply alike).
struct OwnRecord { float3 albedo; float3 normal; float roughness; float3 emissive; };
// The pixel's own ray takes the scattering walk this frame (PT-40; write_yardstick leaves it in the pixel's word as
// YARD_WALKS, which pt_walk reads): the ray stopped on a material light walks inside, on a piece of the mesh with an
// inside (subsurface_hit), and the ray's own draw fell under the game's subsurface mask at that point (the share of the
// diffuse light that scatters there). The pixel keeps its light as a plain surface's in the other frames, and their mean
// is the mask's blend of the two.
static bool g_ownWalks = false;
static const uint WALK_SALT = 0x77616C6Bu;
OwnRecord write_traced(uint2 p, Hit hit, float3 dir, float distance) {
    RWTexture2D<float4> surf = ResourceDescriptorHeap[g.surface.x];
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
    RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];
    RWTexture2D<uint2> hairTex = ResourceDescriptorHeap[g.hair.x];
    StructuredBuffer<MaterialRecord> materials = ResourceDescriptorHeap[g.scene0.w];
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    const uint flags = materials[hit.material].flags;
    const SurfaceMaterial traced = hit_material(hit);
    const bool eye = (flags & MATERIAL_EYE) != 0;
    float roughness = traced.roughness;
    float3 f0 = traced.f0;
    OwnRecord own;
    // (material_normal hands back the triangle's own normal where the material has no shared normal map: the smooth one then.)
    own.albedo = traced.albedo; own.normal = facing_normal(all(traced.normal == hit.normal) ? hit.rounded : traced.normal, -dir);
    own.roughness = traced.roughness; own.emissive = traced.emissive;
    // Glass, unless the ray met its paint: with the chance the game's alpha covers the texel (glass_cover; the camera ray's
    // own draw, a new one every frame), the pixel's surface is the label, the lid or the stain there, lit as any other
    // surface, and the glass's own interface the rest of the frames.
    bool glass = (flags & MATERIAL_GLASS) != 0;
    if (glass) {
        const float cover = glass_cover(hit);
        if (cover > 0.0 && ray_chance(hit.instance, hit.geometry, hit.primitive, GLASS_PAINT_SALT) < cover) glass = false;
    }
    if (glass) {
        const float ior = optical_ior(materials[hit.material]);
        f0 = pow((ior - 1.0) / (ior + 1.0), 2.0);
    } else if ((flags & MATERIAL_CAMERA_ONLY) != 0 && !eye) { f0 = 0.0; roughness = 1.0; }
    else if (!eye) reflection_boost(roughness, f0, (instances[hit.instance].flags & INSTANCE_CHARACTER) != 0 ? 0.0 : g.cameraPos.w);
    const bool translucent = traced.through > 0.0;
    // Hair without a strand block (MATERIAL_STRANDS alone) is lit through from behind as thin cloth is, as pt_prepare marks it.
    const bool thin = (flags & (MATERIAL_THIN_CLOTH | MATERIAL_STRANDS)) != 0;
    surf[p] = float4(hit.position, glass ? length(hit.position - g.cameraPos.xyz) : distance);
    // ON TRIAL (pt_common glass_clear): what goes through such glass crosses the mesh's own smooth face, so the pixel's normal
    // is that one (its vertices' over the triangle, no normal map: what pt_specular's walk bends the view by, at full
    // precision); the normal-mapped one, which its reflection is drawn about, goes with the glass's words below.
    const bool clearGlass = glass && glass_clear(materials[hit.material]);
    nrm[p] = uint2(pack_normal(clearGlass ? facing_normal(hit.rounded, -dir) : own.normal),
                   (uint(roughness * 255.0 + 0.5) & 0xFF) | 0x100u | (translucent || thin ? 0x200u : 0u) | (pack_normal22(hit.normal) << 10));
    // A complex surface (bit 1) only where it is one: a cut-out, or hair.
    const bool complex = (flags & (MATERIAL_ALPHA_TEST | MATERIAL_STRANDS)) != 0;
    mat[p] = uint2(pack_rgba8(float4(linear_to_srgb(saturate(traced.albedo)), 1.0)),
                   pack_rgba8(float4(linear_to_srgb(saturate(f0)),
                                     float((complex ? 1u : 0u) | (glass ? 2u : 0u) | (thin ? 4u : 0u) | (eye ? 8u : 0u)) / 255.0)));
    // Glass's two words, from which pt_specular's walk starts (the surface record keeps neither the submesh nor the triangle):
    // its instance (24 bits) and material (20), bit 31 its front face met, bit 30 the triangle met has an inside (glass_solid),
    // and, ON TRIAL (bit 29: clearGlass), its normal-mapped normal in the 16 bits left (4 above the instance, 12 above the
    // material). (A character's smooth normal for NRD's old guide plane, PT-92, went with it on 2026-10-11.)
    uint2 glassWords = uint2(hit.instance | (hit.front ? 0x80000000u : 0u) | (glass && glass_solid(hit.instance, hit.geometry, hit.primitive) ? 0x40000000u : 0u), hit.material);
    if (clearGlass) {
        const uint bumped = pack_normal16(own.normal);
        glassWords.x |= 0x20000000u | ((bumped & 0xFu) << 24);
        glassWords.y |= (bumped >> 4) << 20;
    }
    hairTex[p] = glass ? glassWords : uint2(0, 0);
    float3 walkShape;
    g_ownWalks = traced.subsurface > 0.0 && !glass && subsurface_hit(hit, walkShape) &&
        ray_chance(hit.instance, hit.geometry, hit.primitive, WALK_SALT) < traced.subsurface;
    return own;
}
// The ray passed everything: the sky, where HDRP drew a hair card and so no sky. Marked as no other pixel is (drawn
// forward, not lit: pt_common sky_through), and pt_compose gives it the sky as a ray of the tracer's sees it. Its record
// lies along the ray at `distance` (HDRP's surface's there: any place on the ray serves, the sky has none).
void write_sky(uint2 p, float3 dir, float distance) {
    RWTexture2D<float4> surf = ResourceDescriptorHeap[g.surface.x];
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
    RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];
    RWTexture2D<uint2> hairTex = ResourceDescriptorHeap[g.hair.x];
    surf[p] = float4(g.cameraPos.xyz + dir * distance, distance);
    nrm[p] = uint2(pack_normal(-dir), 0u);
    mat[p] = uint2(pack_rgba8(float4(0.0, 0.0, 0.0, 1.0)), 0u);
    hairTex[p] = 0;
}
// Ray Reconstruction's guides for the hair a pixel's ray crossed first, in the tracer's guide outputs (pass a.x and a.y: TGPT
// outputs g0 and g1, 8 bits a channel): its colour (sRGB) and how much of the pixel it covers; the normal it is lit about
// (octahedral), its roughness and the square root of its specular albedo. The same every frame, whether this frame's ray
// stopped at that card or passed it (NVIDIA's guide 3.5: guides are to be steady; the light is what flickers and what the
// denoiser averages). pt_compose lays the surface the ray stopped at under them by that cover, and the composite lays the
// result over the guides pass's, which HDRP's G-buffer describes (d3d11.hlsl traced_guides). None: no hair there.
void write_guides(uint2 p, float3 albedo, float cover, float3 normal, float roughness, float specular) {
    if (pass.a.x == INVALID || pass.a.y == INVALID) return;
    RWTexture2D<float4> g0 = ResourceDescriptorHeap[pass.a.x];
    RWTexture2D<float4> g1 = ResourceDescriptorHeap[pass.a.y];
    if (!(cover > 0.0)) { g0[p] = 0.0; g1[p] = 0.0; return; }
    g0[p] = float4(linear_to_srgb(saturate(albedo)), saturate(cover));
    g1[p] = float4(guide_octahedral(normal), saturate(roughness), sqrt(saturate(specular)));
}

// ---------------------------------------------------------------- the yardstick
// The tracer's own first hit against HDRP's G-buffer at a pixel, in one word (FrameConstants.primary.z; pt_compose shows it
// as TGPT_VIEW_YARDSTICK, pt_yardstick counts it for the log). This is how a gap between the traced scene and the drawn one
// is found: something the traced scene lacks, has too much of, holds at a coarser level of detail or in another pose, or
// reads another colour, normal or roughness for.
// Bits 0-2, where the two surfaces are: YARD_SKY both nothing; YARD_SAME at the same distance (within 1 cm and 1 %);
// YARD_NEARER the tracer's hit in front of HDRP's surface (the traced scene has something HDRP did not draw there, or a
// coarser mesh); YARD_FARTHER behind it (the traced scene lacks what HDRP drew, or holds it coarser, or in another pose);
// YARD_MISSING the tracer met nothing where HDRP drew a surface; YARD_EXTRA the tracer met a surface where HDRP has sky.
// Then, where they are the same surface: YARD_NORMAL the shading normals more than 25 degrees apart, YARD_COLOUR the diffuse
// colours more than 0.1 apart in a channel (linear), YARD_ROUGH the roughness more than 0.2 apart; YARD_MOTION the hit's own
// motion more than a pixel from HDRP's motion vector (any pixel with a hit and a surface). YARD_FORWARD: HDRP drew the pixel
// forward, so its G-buffer holds no colour, normal or roughness to compare (hair, eyes, some props).
// Bits 8-10 the kind of instance hit (YARD_KIND_*), bits 12-31 its material (TGPT_SetMaterials index; all ones: none).
uint yardstick_code(uint2 p, float depth, bool found, float hitDistance, OwnRecord own, float2 motion, uint instance, bool hair, uint material) {
    const bool sky = !(depth > 0.0);
    uint code = YARD_NO_MATERIAL << 12;
    if (!found) return code | (sky ? YARD_SKY : YARD_MISSING);
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    const uint flags = instances[instance].flags;
    const uint kind = hair ? YARD_KIND_HAIR : (flags & INSTANCE_CHARACTER) != 0 ? YARD_KIND_CHARACTER : (flags & INSTANCE_SWAYS) != 0 ? YARD_KIND_SWAYS :
        instances[instance].previous != INVALID ? YARD_KIND_MOVING : YARD_KIND_STILL;
    code = (min(material, YARD_NO_MATERIAL - 1u) << 12) | (kind << 8);
    if (sky) return code | YARD_EXTRA;
    const float surface = length(camera_relative_position(p, depth));
    const float tolerance = 0.01 + 0.01 * surface;
    Texture2D<float2> hdrpMotion = ResourceDescriptorHeap[g.inputs1.y];
    if (length((motion - hdrpMotion.Load(int3(p, 0))) * float2(g.size.xy)) > 1.0) code |= YARD_MOTION;
    if (hitDistance < surface - tolerance) return code | YARD_NEARER;
    if (hitDistance > surface + tolerance) return code | YARD_FARTHER;
    code |= YARD_SAME;
    Texture2D<float4> gb0 = ResourceDescriptorHeap[g.inputs0.x];
    Texture2D<float4> nb = ResourceDescriptorHeap[g.inputs0.y];
    const float4 a = gb0.Load(int3(p, 0));
    if (!(a.a > 0.5)) return code | YARD_FORWARD;
    const float4 n = nb.Load(int3(p, 0));
    if (dot(decode_hdrp_normal(n), own.normal) < 0.906) code |= YARD_NORMAL;
    const float3 d = abs(srgb_to_linear(a.rgb) - saturate(own.albedo));
    if (max(d.r, max(d.g, d.b)) > 0.1) code |= YARD_COLOUR;
    if (abs(saturate(n.a) - own.roughness) > 0.2) code |= YARD_ROUGH;
    return code;
}
void write_yardstick(uint2 p, uint code) {
    if (g.primary.z == INVALID) return;
    RWTexture2D<uint> y = ResourceDescriptorHeap[g.primary.z];
    y[p] = code | (g_ownWalks ? YARD_WALKS : 0u);
}

// ---------------------------------------------------------------- a pixel
// Every pixel's surface from its own camera ray (PT-59): the first thing the ray stops at in the whole traced scene, from the
// near plane on. Hair: write_hair. Anything else: write_traced. Nothing: the sky; where HDRP drew a surface there (the traced
// scene lacks it), the sky as the tracer sees it (write_sky), since HDRP drew none. The record's distance is the hit's own.
// Its motion and emission: write_own. And the yardstick's word for the pixel.
// PT-127, primary surface replacement (pt_psr_record.hlsli): the pixel whose camera ray stopped at a pure mirror that stands
// still this frame is marked for the reflections' pass (PSR_CANDIDATE, with the frame's serial: every other pixel's record
// is left as it lies and counts for nothing). Called after write_traced, by what it wrote: no roughness in the surface's
// byte and no diffuse colour (pt_psr_capture psr_mirror asks the rest). Standing still: no character, nothing the wind
// sways, no posed mesh, and its transform the frame before's. A GameObject can be followed every frame and yet stand still
// (the harness's mirror is one): the two transforms are compared, not the scene list's word for it. A mirror that moves
// needs its previous plane for the motion NRD is told (not built: such a pixel stays the mirror's own).
void psr_primary(uint2 p, PrimaryWalk w) {
    if (!psr_on()) return;
    RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
    RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];
    if ((nrm[p].y & 0xFFu) != 0u || (mat[p].x & 0x00FFFFFFu) != 0u) return;
    StructuredBuffer<InstanceRecord> instances = ResourceDescriptorHeap[g.scene0.x];
    StructuredBuffer<MeshRecord> meshes = ResourceDescriptorHeap[g.scene0.y];
    const InstanceRecord inst = instances[w.stop.instance];
    if ((inst.flags & (INSTANCE_CHARACTER | INSTANCE_SWAYS)) != 0u || meshes[inst.mesh].previous != INVALID) return;
    bool still = inst.previous == INVALID;
    if (!still && g.primary.w != INVALID) {
        StructuredBuffer<float4> rows = ResourceDescriptorHeap[g.primary.w];
        still = all(rows[inst.previous * 3u] == w.stop.toWorld[0]) &&
            all(rows[inst.previous * 3u + 1u] == w.stop.toWorld[1]) && all(rows[inst.previous * 3u + 2u] == w.stop.toWorld[2]);
    }
    if (!still) return;
    RWStructuredBuffer<PsrRecord> records = ResourceDescriptorHeap[g.psr.x];
    records[psr_index(p)].surface = uint4(0u, 0u, PSR_CANDIDATE << 24u, g.psr.w);
}
void primary_all(uint2 p, float depth) {
    const bool sky = !(depth > 0.0);
    const float3 nearAt = camera_relative_position(p, 1.0);
    const float3 toward = sky ? nearAt : camera_relative_position(p, depth);
    const float surface = sky ? 0.0 : length(toward);
    const float3 dir = normalize(toward);
    const float pixelAngle = length(normalize(camera_relative_position(p + uint2(1, 0), sky ? 1.0 : depth)) - dir);
    g_cameraRays = true;
    const PrimaryWalk w = primary_walk(dir, length(nearAt), g.params.y, true, pixelAngle, surface, 0.003 + 0.001 * surface);
    Hit waterHit;
    if (trace_water(g.cameraPos.xyz, dir, length(nearAt), w.stopped ? w.stop.t : g.params.y, waterHit)) {
        RWTexture2D<float4> surf = ResourceDescriptorHeap[g.surface.x];
        RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
        RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];
        const float distance = length(waterHit.position - g.cameraPos.xyz);
        surf[p] = float4(waterHit.position, distance);
        const float foam = water_foam(waterHit.water - 1, waterHit.waterBase);
        const uint roughness = uint(saturate(foam * g.waterFoam[(waterHit.water - 1) * 6].z) * 255.0 + 0.5);
        // Its normal the game's own wave normal there (waterHit.rounded), its geometric one the triangle's.
        nrm[p] = uint2(pack_normal(waterHit.rounded), roughness | 0x100u | (pack_normal22(waterHit.normal) << 10));
        const float f0 = pow((WATER_IOR - 1.0) / (WATER_IOR + 1.0), 2.0);
        mat[p] = uint2(pack_rgba8(float4(linear_to_srgb(foam * g.waterFoam[(waterHit.water - 1) * 6 + 5].rgb), 1.0)),  pack_rgba8(float4(linear_to_srgb(f0.xxx), 10.0 / 255.0)));
        write_own(p, point_motion(waterHit.position, waterHit.waterPrevious), 0.0);
        write_yardstick(p, (YARD_NO_MATERIAL << 12) | YARD_FORWARD);
        g_cameraRays = false;
        return;
    }
    if (w.met) {
        const HairPoint first = hair_point(w.first, dir);
        write_guides(p, first.albedo, w.first.cover, first.normal, first.roughness, first.specular);
    }
    OwnRecord own = (OwnRecord)0;
    float3 motion = 0.0;
    uint material = YARD_NO_MATERIAL;
    if (!w.stopped) {
        if (sky) {
            RWTexture2D<float4> surf = ResourceDescriptorHeap[g.surface.x];
            RWTexture2D<uint2> nrm = ResourceDescriptorHeap[g.surface.z];
            RWTexture2D<uint2> mat = ResourceDescriptorHeap[g.material.x];
            surf[p] = 0.0; nrm[p] = uint2(pack_normal(float3(0, 1, 0)), 0); mat[p] = 0;
        } else write_sky(p, dir, surface);
        const float3 far = g.cameraPos.xyz + dir * 1e4;
        motion = point_motion(far, far);
        write_own(p, motion, 0.0);
    } else {
        const float3 position = g.cameraPos.xyz + dir * w.stop.t;
        motion = hit_motion(w.stop, position);
        if (w.hair) {
            const HairPoint h = hair_point(w.stop, dir);
            write_hair(p, h, w.stop.t);
            own.albedo = h.albedo; own.normal = h.normal; own.roughness = h.roughness;
            material = h.hit.material;
        } else {
            const Hit hit = fetch_hit(w.stop.instance, w.stop.geometry, w.stop.primitive, w.stop.bary, w.stop.toWorld, dir, position, g.cameraPos.xyz);
            g_hitToWorld = w.stop.toWorld;
            own = write_traced(p, hit, dir, w.stop.t);
            psr_primary(p, w);
            material = hit.material;
        }
        write_own(p, motion, own.emissive);
    }
    g_cameraRays = false;
    write_yardstick(p, yardstick_code(p, depth, w.stopped, w.stop.t, own, motion.xy, w.stop.instance, w.stopped && w.hair, material));
}
// The route before it (TGPT_OPTION_GBUFFER_SURFACES): the pixel as its own camera ray finds it where hair is; true when it
// wrote the pixel's surface, false where HDRP's G-buffer still gives it (pt_prepare goes on). depth: HDRP's at the pixel (a
// surface: > 0); deferredPixel: HDRP drew it into its G-buffer.
// The ray walks the hair in its way (primary_walk over the instances holding such hair): up to just in front of a surface in
// the G-buffer (a brow's card lies a millimetre or two over the skin), and where HDRP drew the pixel forward a little past
// its depth, to find the card HDRP drew there. Stopped at a card: hair. Passed them all where HDRP's own surface was such a
// card (one lies at its depth): HDRP's buffers hold nothing of what is behind it, so the ray walks the whole scene (the same
// draws for the same cards) and what it stops at there is the surface, hair again, anything else, or the sky. Passed them
// all in front of a G-buffer surface: that surface, from the G-buffer.
bool primary_pixel(uint2 p, float depth, bool deferredPixel) {
    if (!(depth > 0.0) || g.hair.y != 1u) return false;
    const float3 toSurface = camera_relative_position(p, depth);
    const float surface = length(toSurface);
    const float3 dir = toSurface / max(surface, 1e-6);
    const float pixelAngle = length(normalize(camera_relative_position(p + uint2(1, 0), depth)) - dir);
    const float within = 0.003 + 0.001 * surface;
    const float tmax = min(deferredPixel ? surface - (0.0002 + 0.0001 * min(surface, 1e4)) : surface + (0.03 + 0.01 * surface), g.params.y);
    if (!(tmax > 0.0)) return false;
    g_cameraRays = true;
    const PrimaryWalk w = primary_walk(dir, 0.0, tmax, false, pixelAngle, surface, within);
    if (!w.met && (deferredPixel || !w.at)) { g_cameraRays = false; return false; }
    if (w.met) {
        const HairPoint first = hair_point(w.first, dir);
        write_guides(p, first.albedo, w.first.cover, first.normal, first.roughness, first.specular);
    }
    bool wrote = false;
    if (w.stopped && w.hair) {
        write_hair(p, hair_point(w.stop, dir), surface);
        wrote = true;
    } else if (!deferredPixel && w.at) {
        const PrimaryWalk all = primary_walk(dir, 0.0, g.params.y, true, pixelAngle, surface, within);
        if (all.stopped && all.hair) write_hair(p, hair_point(all.stop, dir), surface);
        else if (all.stopped) {
            const Hit hit = fetch_hit(all.stop.instance, all.stop.geometry, all.stop.primitive, all.stop.bary, all.stop.toWorld, dir, g.cameraPos.xyz + dir * all.stop.t, g.cameraPos.xyz);
            g_hitToWorld = all.stop.toWorld;
            write_own(p, 0.0, write_traced(p, hit, dir, surface).emissive);
        } else write_sky(p, dir, surface);
        wrote = true;
    }
    g_cameraRays = false;
    return wrote;
}
