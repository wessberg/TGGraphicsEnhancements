// SPDX-License-Identifier: MIT
// The traced scene on the render thread: meshes read back from the game's D3D11 device, converted to
// compact positions/UVs/colors/32-bit indices on a worker thread, uploaded to D3D12 with one bottom-level
// acceleration structure each; materials, instances and lights as structured buffers; and one top-level
// acceleration structure a frame, relative to a scene origin near the camera.
#pragma once
#include "runtime.hpp"
#include "light_grid.hpp" // the light grid's cells and what their builder keeps between builds (build_light_grid)
#if TGPT_MEGA_GEOMETRY
#include "mega_geometry.hpp" // PT-109: only in a build configured with -DTGPT_MEGA_GEOMETRY=ON
#endif
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <set>
#include <thread>

namespace tgpt {

// PT-87, memory-2's cuts (docs/agents/memory-2.md): each keeps the picture, so each has a switch for a test alone
// (tests/pathtracing_memory1_checks.inl), which runs the same frames with a cut and without and compares every pixel.
// TGPT_TEST_MEMORY2_OFF=<sum> puts back what was before: 1 the still meshes' full-size builds in the one arena with
// everything else, 2 the plants' structures as built (not compacted), 4 the water's structures as built and its pools'
// pages kept. Read where a frame's scene is recorded.
constexpr uint32_t kMemory2BuildChunks=1, kMemory2Plants=2, kMemory2Water=4;
// What a build WITHOUT the test's switch leaves as it was before (memory3, 2026-10-10: "cuts 2 and 3 wait"). The plants'
// compaction passes its check (every pixel the same) but what a refit of a compacted structure costs a frame has not been
// seen in the game; the water's cut compiles and has never been run with water. Take a name out of this sum to turn its
// cut on in the game; the check sets the switch and so runs each cut whatever stands here.
// 2026-10-11 ~13:30: both on (0). The owner's session had the textures held back near the video memory budget for a long
// time, stutter and a GPU hang; the water's pools then held 848 MB of pages for 196 MB of blocks in use, the plants' posed
// copies and structures 720 MB. To be seen in the game at once: the beach (water) and the forest (plants).
constexpr uint32_t kMemory2Waiting=0;
inline uint32_t memory2_off() {
    char text[16]{};
    return GetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", text, sizeof text)>0 ? uint32_t(std::strtoul(text, nullptr, 10)) : kMemory2Waiting;
}

// previous: a character's positions as the previous frame skinned them, a swaying cloth's as the previous frame posed them
// (kInvalid for any other mesh): a hit's own motion. With kMeshPreviousDelta, a group of plants in the wind: each vertex's
// move since the previous frame (pt_sway step 5; pt_primary hit_motion).
// uvs2: the mesh's fourth set of texture coordinates (PT-64; kInvalid without).
struct MeshRecordGpu { uint32_t positions, uvs, colors, indices, geometryBase, geometryCount, vertexCount, normals, previous; uint32_t uvs2=kInvalid, tangents=kInvalid, decalColors=kInvalid; };
// flags: kGeometryClosed where the submesh's surface closes a volume (scene.cpp mark_closed): glass has an inside only
// there (pt_dielectric.hlsli glass_solid); an open sheet of it is a thin pane.
// layers: a plant group's submesh keeps its member's rendering layers here (kGeometryOwnLayers), which decals ask for;
// the group's one instance has no layers of its own.
// The flags' upper 24 bits (kGeometrySolidShift on): how many of the submesh's triangles, from its first, are of pieces
// that close a volume (mark_closed puts those first; PT-40: light walks inside a material drawn from both sides only
// there, pt_common piece_solid). All of them where the submesh is closed; a submesh of 16.7 million triangles or more
// counts its first 16.7 million at most.
constexpr uint32_t kMeshPreviousDelta=0x40000000u;
struct GeometryRecordGpu { uint32_t indexStart, indexCount, layers, flags; };
constexpr uint32_t kGeometryClosed=1, kGeometryOwnLayers=2;
constexpr uint32_t kGeometrySolidShift=8, kGeometrySolidBits=0xFFFFFF00u, kGeometrySolidMax=0xFFFFFFu;
// previous: a thing the game moves (TGPT_SetMoving): its row in the frame's previous transforms (GpuScene::movingPreviousSrv,
// three float4 rows each: object to world a frame ago, relative to this frame's origin), kInvalid for everything else.
struct InstanceRecordGpu { uint32_t mesh, materialBase, flags, previous, renderingLayerMask; };
// A water mesh of the scene as the plugin hands it over, level (pt_water_surface.hlsli WaterRecord): cell is the size its
// waves want of a triangle near the camera (metres: half a texel of the slot's finest wave band; 0 for still water).
struct WaterRecordGpu { float transform[12]; uint32_t mesh, instance, slot; float cell; };
static_assert(sizeof(WaterRecordGpu)==64, "water geometry record");
static_assert(sizeof(MeshRecordGpu)==48 && sizeof(GeometryRecordGpu)==16 && sizeof(InstanceRecordGpu)==20, "shader records");

// Emissive triangles as lights: a submesh's triangles in clusters of kClusterTriangles consecutive ones (a mesh's index
// order keeps them together), each with its bounds and area in the mesh's own space, and its area spread over the axes
// (each triangle's area times its normal's squared components: an instance's scale makes it the cluster's area in the
// world, world_area in scene.cpp). Made with the conversion and kept when the CPU copies go: the light grid places a
// cluster by them, before the GPU has read its triangles (pt_emissive).
constexpr uint32_t kClusterTriangles=32;
struct TriangleCluster { float lo[3], hi[3], area, spread[3]; };

struct MeshData {
    std::vector<float> positions, uvs, tangents, decalColors;
    std::vector<float> uvs2;                   // the fourth set of texture coordinates, where the mesh has one (PT-64)
    std::vector<uint32_t> colors, indices;
    std::vector<uint32_t> normals;             // octahedral, 16:16 (pt_common.hlsli unpack_normal), where the mesh has them
    std::vector<GeometryRecordGpu> geometries; // index ranges into indices, one per submesh
    uint32_t vertexCount=0;
    std::vector<TriangleCluster> clusters;     // per submesh, from its first cluster (geometryClusters) on
    std::vector<uint32_t> geometryClusters;    // per submesh its first cluster, and the count after the last
};

struct Mesh {
    int32_t handle=0;
    enum class State { Queued, Copied, Converting, Converted, Ready, Failed } state=State::Queued;
    std::unique_ptr<MeshSource> source;
    std::array<ComPtr<ID3D11Buffer>, 5> staging; // vertex streams 0..3, indices
    std::array<std::vector<uint8_t>, 5> bytes;   // read back, for the worker
    uint64_t copiedFrame=0;
    std::chrono::steady_clock::time_point copiedAt{}; // when its read-back was started (pump_d3d11 maps it by the clock too)
    MeshData data;
    // Views into the geometry and acceleration structure arenas, which own the memory.
    D3D12_GPU_VIRTUAL_ADDRESS positionsVa=0, indicesVa=0, blasVa=0;
    uint32_t positionsSrv=kInvalid, uvsSrv=kInvalid, colorsSrv=kInvalid, indicesSrv=kInvalid, normalsSrv=kInvalid;
    uint32_t uvs2Srv=kInvalid, tangentsSrv=kInvalid, decalColorsSrv=kInvalid;
    uint32_t geometryBase=0;
    uint64_t triangles=0, bytesOnGpu=0, blasBytes=0, ommBytes=0;
    std::vector<uint64_t> geometryAllocations, accelerationAllocations;
    // Opacity micromaps (DirectX Raytracing 1.2), for the materials the first alpha-tested instance that can have them
    // gives the submeshes: baked from what the alpha test reads (the game's own texture, or the atlas tiles once they are
    // in), then linked into a second BLAS that every instance with those materials uses from then on (an instance whose
    // materials differ keeps the plain one: PT-110's M5). Made anew for the materials in use when the ones they were made for changed (a texture mip
    // streaming replaced, another cutoff) or no instance has used them for kOmmUnusedFrames (ommUsedFrame: the last
    // frame one did).
    enum class Omm { Unchecked, Waiting, Linked, None } omm=Omm::Unchecked;
    std::vector<uint32_t> ommMaterials;  // per submesh: the material index the micromaps were made for
    std::vector<uint64_t> ommSignatures; // and that material's contents then
    uint32_t alphaTriangles=0;
    D3D12_GPU_VIRTUAL_ADDRESS ommBlasVa=0, ommArrayVa=0;
    uint64_t ommUsedFrame=0;
    // PT-110 (docs/agents/omm-1.md), for the log alone: what the bake made of its cut-out triangles, counted on the CPU
    // from the bake's own output once it has been read back (ommCounted; count_omm). Its pieces by state (kOmmPieces a
    // triangle: bare, covered, unknown) and the triangles with no unknown piece: those a ray's query never hands the shader.
    bool ommCounted=false;
    uint64_t ommPieces[3]{}, ommSettledTriangles=0;
    // (And whether a cut-out material of it lets light through, a leaf's, a blade's or thin cloth's: until round 20 the bake
    // left every covered piece of such a material unknown on purpose; since then only thin cloth's, pt_omm_bake. The count
    // tells them apart: the review's M2 against its M7.)
    bool ommLeaf=false;
    // Its micromaps' subdivision level (kOmmLevel, or a leaf's kOmmLevelLeaves: omm_level), and so their bytes a triangle.
    uint32_t ommLevel=4;
    // PT-110's M5: which build of its micromaps the linked structure is (counted up at each, so never 0 once it has
    // any), for the compaction that follows a build some frames later (GpuScene::compact_meshes).
    uint32_t ommSerial=0;
    // PT-110's M1 (on trial): what a plant group's structure needs to link this mesh's micromaps to its own copy of the
    // mesh's triangles. ommIndices: the bake's index buffer (one index a cut-out triangle: its micromap in the array, or
    // a special index), kept while the micromaps live where the mesh is one the wind moves, and named again at every
    // refit of a group that links it. ommFirst: per submesh, its first triangle's place in that buffer (kInvalid: no
    // cut-out). ommWantedFrame: the frame its micromaps were last asked for (a new group waits a little for them).
    Buffer ommIndices;
    std::vector<uint32_t> ommFirst;
    uint64_t ommWantedFrame=0;
    // HDRP's water (a TGPT_INSTANCE_WATER instance names it): its positions and indices stay on the CPU, for the water
    // units' plan (scene_water.cpp).
    bool water=false;
    // A character: its positions in a buffer of its own that the tracer's skin pass writes every frame (raw
    // UAV), and its BLAS updated in place from them (fast to build, storage reserved when it became ready): refitted
    // to them, and rebuilt every kCharacterRebuild frames (sinceBuild: frames since it was, kInvalid before the first).
    bool skinned=false;
    Buffer skinPositions;
    uint32_t skinUav=kInvalid;
    // And as the frame before skinned it (pt_skin moves them over before it writes the new ones): skinFrame is the tracer's
    // frame that last wrote the positions, so a character new or left out for a frame starts with no motion.
    Buffer skinPrevious; uint32_t skinPreviousUav=kInvalid, skinPreviousSrv=kInvalid; uint64_t skinFrame=0;
    // ON TRIAL (PT-92): its vertices' smooth normals as the game's skinning left them this frame (pt_skin writes them with the
    // positions; 4 bytes a vertex, packed as a mesh's own: pt_common pack_normal). Its mesh record names them in the frames
    // the skinning output brings them (skinned_positions), and none in the others: a hit's Hit.rounded is then its triangle's.
    Buffer skinNormals; uint32_t skinNormalsUav=kInvalid, skinNormalsSrv=kInvalid;
    uint64_t skinScratchBytes=0, skinUpdateBytes=0; // the scratch a build of it needs, and a refit (far less)
    uint32_t sinceBuild=kInvalid;
    // PT-110 (docs/agents/omm-1.md, M3): per submesh, whether its structure marks it solid (its material is no cut-out and
    // no glass), so that a ray's query settles its triangles itself and only the character's cut-outs (hair, torn cloth)
    // reach the shader's alpha test. The marks of the last build: a refit must name them again, and a change of them
    // (a material's alpha test switched on or off) builds the structure anew (GpuScene::record).
    std::vector<uint8_t> skinOpaque;
#if TGPT_MEGA_GEOMETRY
    // PT-109 on trial (megageo-5): its micromaps as the clusters' own micromap array, for plant groups made of clusters (mega_geometry.hpp).
    MegaMicromaps megaMaps;
#endif
};

// PT-110 (docs/agents/omm-1.md), counted for the log alone ("PT cut-outs"): the traced world's cut-outs this frame, by kind.
// A cut-out triangle is one of a submesh whose material is alpha-tested: the shader's alpha test decides what a ray meets
// of it, unless an opacity micromap settles the piece the ray crosses first. `mapped`: the cut-out triangles traced with
// a micromap this frame; `counted`: those of them whose bake has been counted (Mesh::ommCounted), with their pieces by
// state (bare, covered, unknown) and `settled`, the triangles without one unknown piece. `plain`: the triangles of
// submeshes that are no cut-out and reach the shader's test all the same, because their instance is non-opaque as a
// whole; `spared`: such triangles that do not, because their own structure marks their submesh solid (M3: characters, the
// groups of plants). `hair`: the cut-out triangles of hair (strands, hair drawn as a layer), which can have no micromap today.
struct CutoutCount {
    uint32_t instances=0;
    uint64_t triangles=0, mapped=0, counted=0, settled=0, plain=0, spared=0, hair=0;
    uint64_t pieces[3]{};
    uint64_t leafUnknown=0; // of pieces[2]: those of meshes with a leaf's, a blade's or thin cloth's cut-out (Mesh::ommLeaf)
};
enum CutoutKind : uint32_t { kCutStill, kCutPlants, kCutCharacters, kCutCloth, kCutKinds };

struct SceneStats {
    // PT-110: the frame's cut-outs by kind (CutoutCount), the mesh decals' own structures (searched whole, no alpha test),
    // and the frame's "Micromaps trial" (GpuScene::ommTrial).
    CutoutCount cutouts[kCutKinds];
    uint32_t decalMeshes=0, ommTrial=0;
    uint32_t meshesReady=0, meshesFailed=0, meshesQueued=0, instancesTraced=0;
    // The listed instances left out of the frame because their mesh is not built yet (PT-98: still to come).
    uint32_t instancesWaiting=0;
    uint64_t triangles=0, geometryBytes=0, accelerationBytes=0;
    uint32_t gridCellsLit=0, gridEntries=0, gridMostInCell=0, gridBuilds=0, gridFarBuilds=0;
    uint32_t ommMeshes=0, ommWaiting=0, ommInstances=0; uint64_t ommTriangles=0, ommBytes=0;
    uint32_t charactersTraced=0; uint64_t characterTriangles=0;
    uint32_t swayingInstances=0; // instances all of whose materials the wind moves (kMaskSways)
    uint32_t clothInstances=0;   // instances with a thin cloth material (TGPT_MATERIAL_THIN_CLOTH)
    uint32_t clothSwayed=0;      // instances traced in the pose the wind gives them (TGPT_SetClothWind: their own copy, pt_sway)
    uint32_t clothSwayedVertices=0, clothSwayCopies=0;
    // Grass and leaves in the wind's pose (TGPT_SetPlantWind): the instances and the groups they are in, their vertices and
    // triangles, the groups made this frame, and the instances still waiting for their group's turn (traced still).
    uint32_t plantInstances=0, plantGroups=0, plantVertices=0, plantBuilt=0, plantWaiting=0; uint64_t plantTriangles=0;
    // Things the game moves (TGPT_SetMoving): those traced this frame where HDRP draws them, those hidden, and the scene's
    // instances they stood in for.
    uint32_t movingTraced=0, movingHidden=0, movingReplaced=0;
    uint32_t seeThroughInstances=0; // instances with glass HDRP draws transparent (TGPT_MATERIAL_SEE_THROUGH, kMaskSeeThrough)
    uint32_t waterInstances=0;   // HDRP's water (TGPT_INSTANCE_WATER, kMaskWater)
    // Its surface, the waves as triangles (scene_water.cpp): the units traced this frame, their triangles and vertices, the
    // units made this frame, and the units kept in all.
    uint32_t waterUnits=0, waterUnitVertices=0, waterUnitsMade=0, waterUnitsKept=0; uint64_t waterUnitTriangles=0;
    // PT-99 (docs/agents/waterblas.md): the groups those units are traced in (one bottom level, one refit and one
    // instance each), the groups built this frame (the new units', and one put together of several), the triangles built
    // for them, the units collapsed in their groups this frame (not wanted, not traced) and the geometries of dropped units.
    uint32_t waterGroups=0, waterGroupsBuilt=0, waterGroupsMerged=0, waterCollapsed=0, waterDead=0; uint64_t waterBuiltTriangles=0;
    uint32_t hairLayerInstances=0; // instances with hair drawn as a layer (TGPT_MATERIAL_HAIR_LAYER, kMaskStrands)
    uint32_t subsurfaceMaterials=0; // materials light walks inside (TGPT_MATERIAL_SUBSURFACE): pt_walk has pixels to look for
    // PT-99 (docs/agents/speed-1.md): the instances traced with every triangle non-opaque because one of their materials is
    // alpha-tested and they have no micromaps (a ray's query hands each triangle it crosses to the shader's test), their
    // triangles, and those of them that belong to submeshes that are no cut-out: tested for nothing.
    uint32_t shaderTestedInstances=0; uint64_t shaderTestedTriangles=0, shaderTestedOpaque=0;
    // Emissive surfaces (build_emitters): submeshes and triangles in the emitter records, the clusters and triangles sampled
    // as lights, and the submeshes left out for the triangle budget.
    uint32_t emitters=0, emitterTriangles=0, emitterClustersSampled=0, emitterTrianglesSampled=0, emittersOverBudget=0, emitterBuilds=0;
};

// The emitter records' layout (pt_common.hlsli EMITTER_*): light indices from kEmitterBase on name them, triangles in their
// slots first and the clusters the light grid lists after them.
constexpr uint32_t kEmitterBase=1u<<20;
// Up to this many emissive triangles are sampled as lights or kept as a light's own surface, the brightest submeshes first.
// The rest are still met by paths, whole (emission_weight), so the budget costs noise, not light; but with the radiance
// clamps a rare bright path's light was cut, and the harness's busiest scenes filled the old 16,384 (36 submeshes over it).
// PT-50: 16 times that, buffers growing with what a scene has (80 bytes a triangle, two frames' records).
constexpr uint32_t kEmitterBudget=1u<<18;
// The pass that writes the emitter records (pt_emissive.hlsl): what one emissive submesh of an instance is, where its
// triangles' slots start, and each cluster's slots, what it is (EMITTER_SAMPLED) and how far its light is sampled.
struct EmitterGpu { float transform[12]; uint32_t mesh, geometry, material, firstSlot, triangles, firstCluster, key, flags; };
struct ClusterInfoGpu { uint32_t firstSlot, count, state; float reach; };
static_assert(sizeof(EmitterGpu)==80 && sizeof(ClusterInfoGpu)==16, "emitter records");

// Instance masks (pt_common.hlsli MASK_*): an instance all of whose materials the wind moves (TGPT_MATERIAL_SWAYS) has its
// own bit, which rays from HDRP's surfaces leave out near them; everything else has the rest. An instance with see-through
// glass (TGPT_MATERIAL_SEE_THROUGH) has one more bit, which the camera ray looking for such glass asks for alone. HDRP's
// water (TGPT_INSTANCE_WATER) has a bit of its own and nothing else: only the water query (MASK_WATER) asks for it, every
// other ray leaves it out (MASK_ALL). An instance with hair drawn as a layer (TGPT_MATERIAL_HAIR_LAYER: a head of hair, a
// beard, a face with its brows) has one more, which the camera ray looking for such hair (pt_hair) asks for alone. The
// water's own surface, its waves as triangles (a water unit), has the top bit and nothing else (kMaskWaves): it is in
// MASK_ALL, so every ray meets it, and a ray that must pass it leaves that one bit out.
constexpr uint32_t kMaskSways=0x02, kMaskSeeThrough=0x04, kMaskWater=0x08, kMaskStrands=0x10, kMaskStill=0x61, kMaskWaves=0x80;
// Round 20: a still instance holding leaves or grass (a translucent cut-out) has kMaskFoliage of kMaskStill's three bits, any
// other kMaskStillPlain: every ray mask holds all three, and a light's ray meets foliage in a pass of its own (pt_common
// foliage_blocks), whose covered micromap pieces are opaque to every other ray (pt_omm_bake).
constexpr uint32_t kMaskFoliage=0x40, kMaskStillPlain=0x21;

// A cloth the game sways in the wind (TGPT_SetClothWind) near the camera, traced in the pose HDRP draws it: its own copy of
// the mesh's positions, which the sway pass (pt_sway.hlsl) writes as the game's Cloth_Wind shader moves them, its own BLAS
// refitted to them, and a mesh record of its own (the mesh's texture coordinates, colors, indices and normals, these
// positions). Kept while its instance is traced, by its mesh, materials and transform.
struct SwayCopy {
    int32_t mesh=0; uint32_t materialBase=0;
    Buffer positions, blas;     // positions: this frame's pose, then (at previousAt) the previous frame's (pt_sway modes 3, 4)
    uint32_t positionsSrv=kInvalid, positionsUav=kInvalid, previousSrv=kInvalid, previousAt=0;
    uint64_t scratchBytes=0, updateBytes=0; // the scratch its build needs, and a refit
    bool built=false;
    bool moving=false;          // its previous pose is not this one (a pose made, a frame after the one before)
    float builtTime=0;          // the shader time its pose was made for
    uint64_t usedFrame=0;       // the last frame its instance was traced
    uint64_t posedFrame=0;      // the last frame sway() looked at it
};
// The sway pass's record per dispatch (pt_sway.hlsl SwayRecord).
struct SwayRecordGpu {
    float toObject[12];
    float time, speed, wind, noiseScale;
    float intensity[3]; uint32_t flags;
    float direction[2]; uint32_t indexStart, indexCount;
    uint32_t positionsIn, uvs, colors, indices;
    uint32_t positionsOut, vertexCount, mode, previousAt;
};
static_assert(sizeof(SwayRecordGpu)==128, "sway record");
// Every swaying cloth in the traced scene in its own pose, the nearest first (PT-50: until 2026-10-07 96 within 60 m, which
// the harness's busiest scenes reached, the rest traced still while HDRP swayed them). Buffer sizes far past what the
// game shows at once (240 thin cloths, 47,908 swayed vertices in the harness's busiest scenes).
constexpr uint32_t kMaxSwayCopies=4096, kSwayVertexBudget=1u<<24;

// Grass and leaves in the game's wind (TGPT_SetPlantWind, PT-63: "of course our shadows and reflections should move
// alongside the grass and leaves swaying in the wind"). There are thousands of them (10,671 vegetation instances at the
// harness's vista against 240 cloths), so they are not posed one copy and one BLAS each: the instances of one cell of the
// world (kPlantCell metres a side) are one group, a mesh of its own made of its members' vertices in the group's space
// (its texture coordinates, colors, normals and indices merged once, pt_sway steps 3 and 4), posed every frame as the
// game's vertex shaders pose them (step 5) and its one BLAS refitted. Every such instance is in a group: no count and no
// distance leaves one still. A group is made anew when one of its members changes (another level of detail, an instance
// come or gone); at most kPlantBuildsPerFrame groups and kPlantBuildTriangles triangles are made a frame, and until its
// turn a group's instances are traced where they stand still (TGPT_INSTANCE_SWAYS), as all of them were before.
constexpr double kPlantCell=16.0;
constexpr uint32_t kPlantBuildsPerFrame=24;
// PT-110's M1 (on trial): a new group waits this many frames for the micromaps its members' meshes have been asked
// for (they are baked within a few frames), so that it is built once, with them, and not twice.
constexpr uint64_t kPlantMapWait=30;
constexpr uint64_t kPlantBuildTriangles=1500000;
#if TGPT_MEGA_GEOMETRY
// PT-109 on trial (the hidden "Mega Geometry trial" 4, 5, 6; docs/agents/megageo-4.md): a cell's small plants (grass,
// bushes, ferns) in a group of their own beside its trees. Nothing in the scene names a plant's kind (a wind is grass's or
// leaves', and bushes and ferns sway as leaves, as trees do), so a plant is small by its height in the world: its mesh's
// box (MeshData::clusters, made for every mesh) turned and scaled by its instance, at most kMegaSmallPlantHeight metres
// (the game's grass 0.5 to 0.75 m high, a fern 0.4 m, a hazel bush 0.73 m; its trees 13 to 22 m). Such a group's key is
// its cell's with kMegaSmallTag added to the second number (a cell's numbers stay far below it).
constexpr double kMegaSmallPlantHeight=4.0;
constexpr int64_t kMegaSmallTag=int64_t(1)<<40;
#endif
// pt_sway.hlsl's PlantGroup, PlantMember, PlantRange and PlantWind.
struct PlantGroupGpu {
    uint32_t members, memberFirst, memberCount, ranges;
    uint32_t rangeFirst, rangeCount, vertexCount, indexCount;
    uint32_t dataOut, positionsAt, uvsAt, colorsAt;
    uint32_t normalsAt, rangeAt, indicesAt, winds;
    uint32_t noise, previousAt, flags, pad2; // flags 1: its pose of the frame before is in its buffer (step 5's moves from it)
    float time, speed, direction, pad3;
    float camera[3], pad4;
    float forward[3], pad5;
    float origin[3], pad6;
};
struct PlantMemberGpu { float toGroup[12]; float toObject[12]; uint32_t positionsIn, uvsIn, colorsIn, normalsIn; uint32_t indicesIn, vertexBase, vertexCount, pad; };
struct PlantRangeGpu { uint32_t member, wind, indexStartIn, indexCount; uint32_t indexStartOut, pad0, pad1, pad2; };
struct PlantWindGpu { uint32_t kind, flags, noiseOffset, noiseSize; float strength, bendHeight, pivotY, fadeX; float fadeY, pad0, pad1, pad2; };
static_assert(sizeof(PlantGroupGpu)==144 && sizeof(PlantMemberGpu)==128 && sizeof(PlantRangeGpu)==32 && sizeof(PlantWindGpu)==48, "plant wind records");
// A plant group's submeshes as its structure's build and every refit name them (scene.cpp plant_geometry). The
// descriptions point into these arrays: they must not be copied once made.
struct PlantGeometry {
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
    std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangles;
    std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> links;
};
struct PlantGroup {
    uint64_t signature=0;               // of its members (each one's mesh, materials and transform), whatever their order
    double origin[3]{};                 // its space's origin in the world (its cell's corner, at its first member's height)
    Buffer data, blas;                  // positions, texture coordinates, colors, normals, each vertex's range, indices; its BLAS
    uint32_t dataUav=kInvalid, positionsSrv=kInvalid, uvsSrv=kInvalid, colorsSrv=kInvalid, normalsSrv=kInvalid, indicesSrv=kInvalid;
    uint32_t positionsAt=0, uvsAt=0, colorsAt=0, normalsAt=0, rangeAt=0, indicesAt=0; // where each part starts in data, in bytes
    // Each vertex's move since the frame before (8 bytes a vertex, at previousAt; pt_sway steps 5 and 6): the camera rays'
    // motion on it (MeshRecordGpu.previous with kMeshPreviousDelta). moving: they are not all nothing; posedFrame: the
    // last frame pose_plants looked at it.
    uint32_t previousSrv=kInvalid, previousAt=0;
    bool moving=false;
    uint64_t posedFrame=0;
    uint32_t vertexCount=0, indexCount=0;
    std::vector<uint32_t> sources;              // its members (instances_ indices), in the order of their keys
    std::vector<PlantMemberGpu> members;
    std::vector<PlantRangeGpu> ranges;          // one per member submesh; member counts from this group's first
    std::vector<uint32_t> rangeMaterials;       // each range's material (its wind is looked up every frame)
    std::vector<GeometryRecordGpu> geometries;  // the same submeshes as the hit shading reads them: index ranges in its merged indices
    std::vector<uint8_t> opaque;                // per geometry: no alpha test
    // PT-110, for the log alone: its members with an alpha-tested material, their cut-out triangles, and their triangles of
    // plain submeshes: those that reach the shader's test all the same, and those its structure marks solid (CutoutCount).
    uint32_t cutoutMembers=0; uint64_t cutoutTriangles=0, plainTested=0, plainSpared=0;
    // PT-110's M1 (on trial: the plugin's hidden "Micromaps trial" 32): the micromaps of its members' meshes, linked into
    // its own structure. A micromap hangs on a triangle's texture coordinates, not on where the triangle stands, and a
    // group holds each member's triangles in the member's own order: so a cut-out submesh of it names the member mesh's
    // micromap array and index buffer (links, per submesh; array 0: none), at its build and again, unchanged, at every
    // refit. memberMaps: per member, which build of its mesh's micromaps it links (Mesh::ommSerial; 0: none): a group
    // whose member's micromaps went or were made anew is another group, and is never traced again. mapMeshes: the
    // linked meshes with how many members link each, for the log's count.
    struct Link { D3D12_GPU_VIRTUAL_ADDRESS array=0, indices=0; };
    std::vector<Link> links;
    std::vector<uint32_t> memberMaps;
    std::vector<std::pair<int32_t, uint32_t>> mapMeshes;
    bool linked=false;
    uint64_t scratchBytes=0, updateBytes=0, triangles=0; // the scratch its build needs, and a refit
    bool merged=false, built=false, alpha=false;
    bool foliage=false;                         // a submesh of it is leaves or grass (kMaskFoliage)
    float posedFor[10]{};                       // the wind and camera its pose was made for
    uint64_t usedFrame=0;
    // PT-87 (docs/agents/memory-2.md): its BLAS is built so that it may be compacted (compactable: the flag every later
    // refit must name too), and compacted once the size has been read back (GpuScene::compact_plants): from then on the
    // compacted one is refitted in place. serial tells this group from another made later in the same cell.
    bool compactable=false, compacted=false;
    uint64_t serial=0;
    // PT-99: the description its build and every refit name (pose_plants makes it at the group's first pose and keeps it:
    // everything it is made from is written only where the group is made).
    PlantGeometry geometry; bool geometryMade=false;
#if TGPT_MEGA_GEOMETRY
    // PT-109 on trial (the hidden "Mega Geometry trial" 5; mega_geometry.hpp MegaMeshClusters): a group of small plants
    // whose bottom level is built every frame from its members' clusters, stamped from their meshes' templates into `clas`
    // (one room a cluster). One record a cluster (`geometries`), each with its submesh's material (`geometryMaterials`, in
    // place of rangeMaterials in the frame's material list); `members` read their indices in the cut's order (step 4).
    // megaMembers: what each member's clusters are stamped from (its mesh, which of its submeshes are solid, its first
    // vertex and its first record in the group); `stamps`: the group's stamps, made when its templates are there.
    bool clusters=false;
    Buffer clas;
    uint64_t megaRoom=0; // immutable allocation size; querying the driver for it every posed frame was costly
    std::vector<uint32_t> geometryMaterials;
    // (megageo-5: `linked`, the member's submeshes whose clusters link its mesh's micromaps, of build `serial`; 0: none.)
    struct MegaMember { int32_t mesh=0; uint64_t opaque=0, linked=0; uint32_t vertexBase=0, geometryBase=0, serial=0; };
    std::vector<MegaMember> megaMembers;
    std::vector<MegaClusterBuilder::Stamp> stamps;
    // megageo-5, NVIDIA's layout (the trial's 7 and 8): each member its own bottom level built from its clusters, in `blas` at
    // its place, with an instance of its own (the group's record and place: GpuScene::record). `copies`: per member, its
    // bottom level's place in `blas` (dest: the offset) and its stamps (first, count in `stamps`). `mapped`: its clusters
    // link micromaps (the calls then carry FLAG_ALLOW_OMM, and so do its templates').
    bool perCopy=false, mapped=false;
    std::vector<MegaClusterBuilder::Bottom> copies;
#endif
};

// The water's surface: HDRP's waves as triangles in the acceleration structure (PT-71 item 5; docs/agents/water-4.md has
// the design and why it leaves no crack). Every base triangle of a water mesh carries a lattice that halves again and
// again; a lattice point lies on the game's displaced surface where the camera is near enough to want it, and on the line
// between its two parents where it is not, sliding from the one to the other in between (pt_water_mesh.hlsl). So cutting
// finer than needed changes nothing, and a unit may cut as fine as its nearest point needs.
//   A unit: some base triangles of one water mesh that lie together (a range of them along a space-filling curve), or one
//   piece of a base triangle too large to cut whole (a node of its lattice), each cut in 4^m triangles: at most
//   kWaterUnitTriangles in all, m at most kWaterLevels. Its vertices (positions, undisplaced points, positions a frame ago)
//   are written anew every frame it is wanted (pt_water_mesh.hlsl reads nothing of the frame before), its indices are one
//   pattern shared by every unit. It is dropped kWaterKeepFrames after its last use. No count of units and no distance
//   ends the water.
//   Which units a frame wants: plan_water, from the camera's position alone (a cell of kWaterCellPerMetre a metre of distance,
//   never finer than half a texel of the slot's finest wave band: WaterSlot.cell).
//   A GROUP (PT-99, docs/agents/waterblas.md; until 2026-10-12 every unit had a bottom level, a block of vertices, a mesh
//   record and an instance of its own: 1,400 to 1,800 refit calls a frame, 2 to 3 ms of the CPU that records): some units
//   of one slot and one cut level m, a mesh of one submesh a unit, as a group of plants is one of a submesh a plant.
//   ONE block of the vertex pool (all their positions, then all their undisplaced points, then all their positions a frame
//   ago, each unit's one after another), ONE bottom level of a geometry a unit, refitted by one call a frame, one mesh
//   record, a geometry record a unit, one instance. A hit names its unit by the instance's ID and GeometryIndex(), through
//   the code every mesh of several submeshes goes through: no pass that traces knows of groups.
//   The shared pattern serves a group as it served a unit: a level's pattern is the indices of base triangle 0, 1, 2, ...,
//   each numbered from t x kWaterLatticeVertices[m]; the unit that starts at base triangle T of its group reads it from
//   T x 4^m x 3 on (its geometry record's indexStart), and those are its vertices' numbers in the group's block. So the
//   pattern holds a group's worth a level (kWaterGroupTriangles), and a group's units share one m.
//   Units come and go; a group is not built anew for that. A unit of a group that this frame does not want is COLLAPSED:
//   the pose pass writes all its vertices to one point (WaterTriGpu.node bit 7), once; its triangles have no area, no ray
//   meets them, and the group is refitted as every frame (a degenerate triangle is legal in a build and a refit, and may
//   become a real one again at a later refit: DXR's "degenerate primitives"; never NaN, an "inactive" one, which may
//   not). Wanted again, it is posed as always. A unit dropped leaves its geometry behind, dead, until its group is made anew.
//   New units of a frame are built together, a group a slot and level. The many small groups a walk makes are put together
//   again by a budget that changes speed alone (plan_water: at most one group a frame made of several, at most
//   kWaterMergeTriangles built for it, only while half as many again are in use as the fewest that could hold the
//   frame's units, kWaterGroupsSpare more than those at least, or a group is half dead).
constexpr uint32_t kInstanceWaves=0x400;        // InstanceRecordGpu.flags (pt_common.hlsli INSTANCE_WAVES); its slot in bits 8 and 9
constexpr uint32_t kWaterLevels=5;
constexpr uint32_t kWaterUnitTriangles=2048;
constexpr double kWaterCellPerMetre=1.0/64.0;   // pt_water_mesh.hlsl CELL_PER_METRE
constexpr uint32_t kWaterKeepFrames=120;
constexpr uint32_t kWaterLattice=32;            // a lattice edge's finest pieces (2^kWaterLevels)
constexpr uint32_t kWaterLatticeVertices[kWaterLevels+1]={3, 6, 15, 45, 153, 561}; // (2^m + 1)(2^m + 2) / 2
// A group: at most this many triangles (16 units' worth: a build of it is a millisecond's kind of work, and the top level
// still sorts the water into dozens of boxes) and this many units (the frame's material list holds as many entries of the
// water's one material: scene.cpp waterMaterialBase). Neither ends anything: more units are more groups.
constexpr uint32_t kWaterGroupTriangles=kWaterUnitTriangles*16;
constexpr uint32_t kWaterGroupMembers=256;
constexpr uint32_t kWaterGroupsSpare=16;
constexpr uint32_t kWaterMergeTriangles=kWaterGroupTriangles;
constexpr uint32_t kWaterLevelIndices=kWaterGroupTriangles*3; // the shared pattern's indices a level
constexpr uint32_t kWaterTriCollapse=128;       // WaterTriGpu.node: the unit is not wanted this frame (pt_water_mesh.hlsl)
// pt_water_mesh.hlsl WaterTri: one base triangle (or one node of its lattice) of a unit, where its vertices go.
// (With kWaterTriCollapse: index, i and j are the bits of the one point all its vertices go to, record is not read.)
struct WaterTriGpu { uint32_t record, index, node, i, j, at, span, target; };
static_assert(sizeof(WaterTriGpu)==32, "water triangle record");
// What the tracer says of each slot's water before a frame is recorded (set_water): whether it is there, the cell its
// waves want (half a texel of its finest wave band; 0: still water), and its up direction in the world.
struct WaterSlot { bool live=false; float cell=0; float up[3]{0, 1, 0}; };
// A water mesh instance on the CPU: its base triangles in the world along a space-filling curve (their second and third
// corner exchanged where that makes their front the water's outward side), and a tree of ranges over them.
struct WaterSurface {
    struct Triangle { double corner[3][3]; double longest; uint32_t index; bool swapped; };
    struct Range { double lo[3], hi[3], longest; uint32_t first, count; int32_t children[2]; };
    std::vector<Triangle> triangles;
    std::vector<Range> ranges; // ranges[0]: all of them
    uint64_t usedFrame=0;
};
struct WaterGroup;
struct WaterUnit {
    bool made=false;                                         // (false: a new entry of the map, to be filled)
    std::array<uint64_t, 4> key{};                           // its own key in the map
    uint32_t vertexCount=0, triangles=0, level=0, slot=0;    // triangles: base triangles (or one node), each cut 4^level times
    uint64_t usedFrame=0;
    float nearest=0;                                         // how near the camera was to it when it was last wanted (metres; PT-87's count by distance)
    double corner[3]{};                                      // its first base triangle's first corner in the world: where it is collapsed to
    // Its group, and which of the group's geometries it is (null until the frame it is first wanted, and after its group
    // was made anew in a frame that did not want it: it then holds nothing on the GPU).
    WaterGroup* group=nullptr; uint32_t member=0;
    std::vector<WaterTriGpu> tris;                           // record: set for the frame it is used in; at, span, target: its place in its group
};
// A group of units (above): what is kept of it is its own and never moves while it lives. A member's place (first: its
// first base triangle among the group's) is relative to the group's block; the members are never reordered or taken out
// (a refit must name the geometries of its build): a unit dropped leaves its entry, unit null.
struct WaterGroup {
    struct Member {
        WaterUnit* unit=nullptr;        // null: dropped (its geometry stays, collapsed, until the group is made anew)
        uint32_t first=0, count=0;      // its base triangles among the group's
        double corner[3]{};             // WaterUnit::corner
        bool collapsed=false;           // its vertices hold the one point now
    };
    uint32_t slot=0, level=0;
    uint32_t vertexPage=kInvalid, vertexAt=0, vertexBlock=0; // its block of the vertex pool: page, offset, the block's bytes
    uint32_t blasPage=kInvalid, blasAt=0, blasBlock=0;       // and of the acceleration structure pool
    uint32_t srv=kInvalid;                                   // its vertices (raw: positions, undisplaced points, positions a frame ago)
    uint32_t vertexCount=0, baseTriangles=0;                 // of all its members
    uint64_t scratchBytes=0, updateBytes=0;                  // the scratch its build needs, and a refit
    bool built=false;
    std::vector<Member> members;
    // The description its build and every refit name, made with the group (the pages of the water's pools and the pattern
    // never move: a page goes back only when no block of it is held, the pattern with the water).
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometry;
    // Its members' geometry records as a hit reads them, in their order (each one's stretch of its level's pattern):
    // what they are made of is the group's own and never written again, so they are made with it.
    std::vector<GeometryRecordGpu> records;
    // A member may be marked collapsed (false: none is, for certain; true: look).
    bool anyCollapsed=false;
    // This frame's (plan_water counts them anew every frame): its members the frame wants and their base triangles, its
    // dropped members and theirs, and its place among the frame's water mesh records (kInvalid: no member is wanted,
    // it is not traced this frame).
    uint32_t used=0, usedBase=0, dead=0, deadBase=0, use=kInvalid;
    uint64_t usedTriangles() const { return uint64_t(usedBase)<<(2*level); }
    uint64_t deadTriangles() const { return uint64_t(deadBase)<<(2*level); }
    uint64_t serial=0;
};
// A pool of equal pages cut into blocks (a group's vertices, a group's structure).
// PT-87 (docs/agents/memory-2.md): a block is the bytes asked for rounded up to an eighth of the power of two below them
// (it was the next power of two: a quarter of every page lost on average, 96 MB of pages for 762 pieces in a tavern); and a
// page in which nothing has had a block for kWaterKeepFrames goes back to the driver (its place in the list is taken by
// the next page made: a group names its page by its number). live, liveBytes: the blocks held now.
// PT-99 (docs/agents/waterblas.md): a page keeps its FREE STRETCHES (offset, bytes; two that touch are one), and a block
// is cut from the first stretch of the first page that holds it. Until then a block given back waited for the next
// asked for of exactly its size, which every unit of a kind was; the groups' blocks are of every size, a walk makes
// and lets go of some every frame, and the pages would have filled with blocks nothing asked for again.
struct WaterPool {
    struct Page { Buffer buffer; uint32_t uav=kInvalid; uint32_t live=0; uint64_t idleSince=0; std::map<uint32_t, uint32_t> free; };
    std::vector<Page> pages;
    uint64_t liveBytes=0;
};

// Opacity micromaps: 4^4 micro-triangles of 2 bits each, 64 bytes a triangle; a leaf's and a blade of grass's at level 6
// since round 20 (kOmmLevelLeaves; omm_bytes: 4^level / 4 bytes a triangle). kOmmLevelMost: pt_omm_bake.hlsl's OMM_LEVEL_MOST.
constexpr uint32_t kOmmLevel=4, kOmmBytes=64, kOmmLevelLeaves=6, kOmmLevelMost=6;
constexpr uint64_t kOmmPieces=256; // 4^kOmmLevel pieces a triangle
constexpr uint32_t omm_bytes(uint32_t level) { return (1u<<(2u*level))/4u; }
// A mesh's micromaps no instance has traced with for this many frames are made anew for the materials now in use.
constexpr uint64_t kOmmUnusedFrames=8;

// The light grid: 8 m cells in a box around the camera, each listing every point and spot light whose
// range reaches it. Path vertices choose among the lights that can light them, not among all of them.
constexpr uint32_t kGridDims[3]={48, 20, 48};
constexpr double kGridCell=8.0, kGridSnap=32.0, kGridPad=0.25;
// The far lists' coarse cells (build_light_grid, pt_common.hlsli GRID_COARSE): this many cells a side. And how far a light
// may have moved since they were made before they are made anew (metres: their weights only steer the sampling).
constexpr uint32_t kGridCoarse=4;
constexpr double kFarStale=4.0;

class GpuScene {
public:
    GpuScene();
    void collect(uint64_t completed);
    void memory(TGPT_Diagnostics&) const;
    // Of that, what the water's surface holds (its pieces' pages, indices, scratch, records and read-backs), in bytes.
    uint64_t water_memory() const;
    // PT-87, the owner's question of 2026-10-09 ("We removed limits so that ... we trace infinitely out into the distance.
    // isn't that the primary reason for our perf woes?"): what the traced world holds by its distance from the camera, for
    // the log's "PT memory by distance" line. Nothing is cut by it: it only counts. A mesh is counted at its NEAREST
    // instance (it would stay for that one whatever went beyond), an instance at its own place, a group of plants at its
    // cell's middle, a piece of the water's surface at where the camera was nearest to it when it was last wanted, and a
    // texture of the game's at the nearest instance with a material that reads it (textureBytes: share::open_bytes).
    // The last place of each row: what no instance names (characters' meshes, textures only hair, decals or a material
    // program read).
    struct DistanceBands {
        static constexpr int kBands=5;
        static constexpr double kEdges[kBands-1]={50.0, 150.0, 500.0, 1500.0}; // metres
        uint32_t instances[kBands+1]{}, meshes[kBands+1]{}, plantGroups[kBands+1]{}, waterUnits[kBands+1]{}, textureCount[kBands+1]{};
        uint64_t instanceTriangles[kBands+1]{}, plantTriangles[kBands+1]{}, waterTriangles[kBands+1]{};
        uint64_t geometry[kBands+1]{}, acceleration[kBands+1]{}, plants[kBands+1]{}, water[kBands+1]{}, textures[kBands+1]{};
        static int band(double metres) { int b=0; while(b<kBands-1 && metres>=kEdges[b]) ++b; return b; }
    };
    void distance_bands(const double camera[3], const std::vector<uint64_t>& textureBytes, DistanceBands& out) const;
    void water_bands(DistanceBands& out) const; // (scene_water.cpp: its part of the above)
    // PT-87 (docs/agents/memory-2.md): the arenas' chunks and what lives in them, bytes: the meshes' geometry, the
    // acceleration structures that stay (compacted ones, characters', micromaps'), and the still meshes' full-size builds
    // on their way to being compacted (chunks of their own, which go back to the driver when the builds have left).
    struct ArenaUse { uint64_t geometryChunks=0, geometryUsed=0, keptChunks=0, keptUsed=0, buildChunks=0, buildUsed=0; };
    ArenaUse arena_use() const;
    void maintenance(DescriptorHeap& heap, DeferredRelease& release, uint64_t fence, const std::vector<TGPT_SkinnedInstance>& skinned) { retire_meshes(heap, release, fence, skinned); }
    ~GpuScene();
    // Takes what the plugin registered since the last frame; a scene the plugin cleared is dropped first (true then).
    bool intake(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue);
    // The frame about to be recorded: the scene's instance list it was submitted with (TGPT_SetScene; a new list is taken
    // whole) and the things the game moves in it then (TGPT_SetMoving; null: none).
    void set_lists(std::shared_ptr<const SceneList> list, std::shared_ptr<const MovingList> moving);
    // The plugin's clear this scene has taken in: texture bakes wait while a newer one is pending (their tiles
    // would be marked settled here and then dropped with the scene).
    uint32_t clear_generation() const { return clearGeneration_; }
    // PT-46: hair's and eyes' own maps read by number at a hit (pt_common.hlsli map_record), as uint4 rows: per detail tile
    // kDetailRecordRows (its maps' handles, then the kind and which maps it has, then the game shader's values), then per
    // 64x64 tile kLobeRecordRows (hair's strands: its maps' handles and kind, then its values). The handles are resolved to
    // numbers (texture_share) before each upload. Render thread; a record of 0xFF bytes is none.
    static constexpr uint32_t kDetailRecordRows=14, kLobeRecordRows=2, kLobeRecordBase=kMaxDetailTiles*kDetailRecordRows;
    static constexpr uint32_t kMapRecordRows=kLobeRecordBase+kMaxTiles*kLobeRecordRows;
    // maps: the D3D11 texture (ID3D11Resource*, 0 none) each of the record's first mapCount words (its handles) was read from.
    void set_map_record(uint32_t row, const uint32_t (*rows)[4], uint32_t count, const uint64_t* maps=nullptr, uint32_t mapCount=0);
    // PT-45 phase 3: mip streaming made a map anew (TGPT_SwapSharedTexture): every handle read from from, or from to before
    // it had a number, becomes handle (to's, -1 none). How many changed.
    uint32_t swap_map_texture(uint64_t from, uint64_t to, int32_t handle);
    uint32_t map_records_srv() const { return mapRecordsBuffer_.srv; }
    // PT-64: the material programs' records (TGPT_SetMaterialPrograms), uploaded as uint4 rows with a row of globals before
    // them (row 0: the game's saturation adjustment; pt_materials.hlsli), their textures' handles resolved to numbers before
    // each upload as the materials' are. kInvalid while the scene has none.
    uint32_t programs_srv() const { return programBuffer_.srv; }
    // Render-thread diagnosis from the uploaded records, not from successful D3D11 handle registration.
    // Missing bits name texture slots in MaterialPrograms.cs; UINT32_MAX means the record itself is unavailable.
    uint32_t program_missing(uint32_t material, uint32_t& kind) const;
    void set_program_saturation(float saturation) { if(saturation!=programSaturation_) { programSaturation_=saturation; programsDirty_=true; } }
    // D3D11, inside the tracer's isolated context state: copies for new meshes and read-backs of copied ones.
    void pump_d3d11(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t frame);
    // PT-109 (docs/agents/megageo-1.md), counted for the log alone ("PT structures"): what a frame's acceleration structure
    // work is, by kind. record and record_omm call structureStamp where a kind's commands end on the list (the tracer puts a
    // GPU timestamp there: the time since the stamp before it is that kind's) and count what they built or refitted in
    // structureWork, which the tracer sums and empties each frame. The top level's `triangles` are its instances.
    enum StructureKind : uint32_t { kStructureOther, kStructureSkinning, kStructureNewMeshes, kStructureCompaction, kStructureDecals, kStructureCloth,
                                    kStructurePlants, kStructureWater, kStructureCharacters, kStructureMicromaps, kStructureTopLevel, kStructureKinds };
    struct StructureWork { uint32_t builds=0, refits=0; uint64_t triangles=0, bytes=0; };
    std::function<void(uint32_t)> structureStamp;
    StructureWork structureWork[kStructureKinds];
    void structure_stamp(uint32_t kind) { if(structureStamp) structureStamp(kind); }
    // PT-125: the plants' work hung the GPU on walks in the forest (2026-10-12: "the work that did not end is plants's").
    // Its steps are numbered as they are recorded (each compacting copy, each pose pass, each build or refit), the tracer
    // writes the number as a GPU progress marker once the step is done (structureNote), and the last frames' lists are
    // kept so that the log can say which step did not end (describe_plant_steps). noteFrame: the frame being recorded.
    std::function<void(uint32_t)> structureNote; uint32_t noteFrame=0;
    struct PlantStep { uint8_t kind; int32_t x, z; uint32_t triangles; uint64_t bytes, scratch; };
    struct PlantStepFrame { uint32_t frame=~0u; std::vector<PlantStep> steps; };
    PlantStepFrame plantStepFrames_[4]; uint32_t plantStepAt_=0;
    void plant_step(uint8_t kind, double x, double z, uint64_t triangles, uint64_t bytes, uint64_t scratch);
    std::string describe_plant_steps(uint32_t frame24, uint32_t done, bool sameFrame) const;
#if TGPT_MEGA_GEOMETRY
    // PT-109: what the driver reported of RTX Mega Geometry on the shared device (tracer.cpp device_ready, mega_probe),
    // and the plugin's hidden "Mega Geometry trial" for the frame about to be recorded (TGPT_Frame.flags,
    // TGPT_FRAME_MEGA_SHIFT): 1, 2 or 3, record makes the frame's top level a partitioned one (megaTop_; cells of 64,
    // 128 or 256 m), where the driver reports it and no call of it failed; the plain build serves everywhere else.
    MegaCaps megaCaps;
    uint32_t megaTrial=0;
#endif
    // D3D12: uploads, BLAS builds, record buffers, lights and this frame's TLAS. The origin is where the
    // TLAS puts world position zero (the camera, snapped to a coarse grid).
    // Characters whose positions the tracer's skin pass wrote this frame are rebuilt and traced with the rest.
    void record(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                DeferredRelease& release, uint64_t retireValue, const double origin[3], const double camera[3],
                const std::vector<TGPT_SkinnedInstance>& skinned);
    // The game's shader time and the wind on its swaying cloth for the frame about to be recorded (TGPT_SetClothWind); the
    // sway pass (pt_sway) the tracer made, which record dispatches before the BLAS it moves are refitted.
    void set_wind(float time, const std::vector<TGPT_ClothWind>& winds);
    ID3D12PipelineState* swayPass=nullptr;
    // PT-98: what a frame may take in goes by the clock, not by the frame. `frames` is how many frames of 60 a second the
    // time since the last traced frame holds (1 at 60 a second and above, 7.5 at 8 a second; the tracer clamps it, tracer.cpp
    // frame_clock). Every "a frame" budget of the scene (mesh read-backs, BLAS builds, micromap bakes, plant groups) is
    // multiplied by it, so the traced world arrives as soon at 8 frames a second as at 60: the same work a second, in
    // fewer and larger pieces. Nothing is left out or made coarser; a frame that takes more in is longer by that work.
    void set_clock(double frames) { clock_=frames>=1.0 ? frames : 1.0; }
    // The water's waves as triangles: each slot's water for the frame about to be recorded, and the pass that poses the
    // units (pt_water_mesh.hlsl, the tracer's; it reads the frame's constants, which are bound before record).
    void set_water(const WaterSlot slots[4]) { for(int i=0;i<4;++i) waterSlots_[i]=slots[i]; }
    ID3D12PipelineState* waterPass=nullptr;
    // The game's wind on its grass and leaves for the frame about to be recorded (TGPT_SetPlantWind), and the winds' noise
    // maps (TGPT_SetWindNoise): record poses every instance with such a material (PlantGroup).
    void set_plant_wind(const TGPT_PlantWindFrame& frame, const std::vector<TGPT_PlantWind>& winds, std::shared_ptr<const WindNoise> noise);
    // A ready character mesh with this many vertices: the buffer its positions go to (its UAV in uav), else null.
    // previousUav: its previous positions' (kInvalid: none); fresh: they hold nothing of the frame before `frame`.
    // ON TRIAL (PT-92), smooth: this frame's skinning output brings its vertices' normals; normals then is the buffer they go
    // to (its UAV in normalsUav) and its mesh record names them, else null and its record names none.
    Buffer* skinned_positions(int32_t handle, uint32_t vertexCount, uint32_t& uav, uint64_t frame, uint32_t& previousUav, bool& fresh, Buffer*& previous,
                              bool smooth, uint32_t& normalsUav, Buffer*& normals);
    // D3D12, after the frame constants are bound and the atlas is readable: bakes opacity micromaps for
    // meshes whose alpha-tested textures are in the atlas, builds their micromap arrays and relinks their
    // BLAS, which instances use from the next frame's TLAS on.
    void record_omm(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release,
                    uint64_t retireValue, ID3D12PipelineState* bake);
    // D3D12, after the frame constants are bound (the pass reads the scene's records, the atlas and the light grid): writes
    // the emitter records build_emitters prepared this frame, if it did (pt_emissive.hlsl).
    void record_emitters(ID3D12GraphicsCommandList4* list, ID3D12PipelineState* pass);
    // D3D11 baked (or gave up on) an atlas tile this frame: micromaps may read it from now on.
    void tile_settled(int32_t tile);
    void alpha_tile_settled(int32_t tile);
    // Every GPU object goes to the deferred release queue; registrations are forgotten.
    void clear(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue);
    SceneStats stats() const;
    // Set when the tracer runs the Shader Model 6.9 passes, whose ray queries accept micromaps; allowed
    // per frame (TGPT_OPTION_NO_MICROMAPS turns them off for every instance).
    bool ommEnabled=false, ommAllowed=true;
    // FOR MEASURING (PT-110, docs/agents/omm-1.md): the plugin's hidden "Micromaps trial" for the frame about to be
    // recorded (TGPT_Frame.flags, TGPT_FRAME_OMM_SHIFT; tgpt_api.h TGPT_OMM_TRIAL_*), 0 as it ships. Its numbers make a
    // kind of cut-out solid, or leave the micromaps unused, for a measuring step: record reads it where it gives each
    // instance its flags. And the frame's cut-outs as the log tells them ("PT cut-outs": cutout_text).
    uint32_t ommTrial=0;
    std::string cutout_text() const;
    // Whether the frame's rays read the game's own textures (the shared range is in the heap and TGPT_OPTION_ATLAS_TEXTURES is
    // off: pt_common.hlsli shared_on). A cut-out's micromaps are baked from the texture its alpha test reads.
    bool sharedTextures=false;

    uint32_t decalsSrv=kInvalid, decalNodesSrv=kInvalid, decalNodeCount=0;
    uint32_t waterRecordsSrv=kInvalid, waterRecordCount=0;
    // The highest corner (world y) of every water surface whose pieces this frame traces, before its waves (plan_water);
    // -1e300 while none is traced. tracer.cpp checks the plugin's highest point of the water against it (PT-99: speed_trial 1).
    double waterHighest=-1e300;
    double waterHighestSlot[4]={-1e300, -1e300, -1e300, -1e300}; // (round 20: the same, per slot: tracer.cpp's waterTop is at least that)
    // PT-114 (docs/agents/caustgrid.md; scene_water.cpp): the tiles of a square grid over the world's x and z under which a
    // slot's level water can lie: its water meshes' triangles as the top level of the last recorded frame holds them (what
    // the water query's ray straight down can meet). A bit a tile in rows[z] (n tiles a side, 64 at most; the first tile's
    // corner at x0, z0 in the world; each `tile` metres wide); a triangle counts for every tile its footprint comes within
    // `margin` metres of. False, and every bit set, when the CPU does not hold every triangle of the slot's water (a mesh
    // still on its way back from the card, no water pass): nothing is proved then, and the caller takes every tile.
    bool water_tiles(uint32_t slot, double x0, double z0, double tile, uint32_t n, double margin, uint64_t* rows) const;
    uint32_t instancesSrv=kInvalid, meshesSrv=kInvalid, geometriesSrv=kInvalid, materialsSrv=kInvalid;
    // The things the game moves this frame: where each was a frame ago (InstanceRecordGpu.previous's rows), kInvalid without.
    uint32_t movingPreviousSrv=kInvalid;
    uint32_t materialIndicesSrv=kInvalid, lightsSrv=kInvalid, lightCount=0;
    // The nodes of the lamps' search tree (lamp_tree.hpp), which lies after the frame's lights in their buffer, its root at
    // record number lightCount (0: no lamp a ray could run into). For the tracer's log.
    uint32_t lampTreeNodes=0;
    // The previous traced frame's lights, positions relative to this frame's origin: RTXDI's temporal reuse weighs a
    // light at the previous surface as it was then (count 0: none).
    uint32_t previousLightsSrv=kInvalid, previousLightCount=0;
    // The brightest light's illuminance 1 m away (a directional light's: its own), luminance, HDRP's units; emissive
    // submeshes sampled as lights count with the intensity their triangles have together at most.
    float brightest_light() const;
    void brightest_parts(float& lamp, float& emitter, float& emitterRaw) const; // (PT-136, for the log)
std::string brightest_lamps_text() const;
float lamps_reference(float* brightest, float* median) const; // (PT-136)
    // The Emissive Lights row, for the frame about to be recorded.
    bool emissiveLights=false;
    // What the frame's passes read of the emissive triangles: the emitter records this frame and the ones the previous frame
    // read (the same buffer until a build writes the other), their counts, the instance table (an instance's submeshes'
    // first slots), the static instances it covers, the clusters in the light grid and the first cluster's index there.
    uint32_t emittersSrv=kInvalid, previousEmittersSrv=kInvalid, emitterCount=0, previousEmitterCount=0;
    uint32_t emitterTableSrv=kInvalid, emitterTableInstances=0, sampledClusters=0, clusterBase=0;
    uint32_t gridCellsSrv=kInvalid, gridIndicesSrv=kInvalid;
    float gridMin[3]{};                              // the grid's corner relative to the origin
    D3D12_GPU_VIRTUAL_ADDRESS tlas=0;
    bool hasTlas=false;

private:
    void worker();
    void convert(Mesh& mesh);
    void upload_meshes(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release,
                       uint64_t retireValue, const std::vector<Mesh*>& meshes, Barriers& barriers);
    // positions: another buffer of the mesh's vertices (a swaying cloth's copy), built to be refitted.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs(const Mesh& mesh, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& storage,
                                                                     D3D12_GPU_VIRTUAL_ADDRESS positions=0) const;
    // This frame's swaying cloths (instances_ indices): their copies moved by the sway pass and their BLAS refitted.
    void sway(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
              uint64_t retireValue, const std::vector<std::pair<SwayCopy*, size_t>>& work, Barriers& barriers);
    void release_sway_copy(SwayCopy& c, DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue);
    // Grass and leaves in the wind. A group in use this frame: where its materials start in the frame's material index list.
    struct PlantUse { PlantGroup* group=nullptr; uint32_t materialBase=0; bool fresh=false; };
    // Which instances are in a group this frame (grouped, by instances_ index) and the groups in use; groups whose members
    // changed are made anew (their buffers and member records; the GPU's part is pose_plants'), within the frame's budget.
    // PT-99 (docs/agents/reccpu.md): the plants by cell, kept between frames while everything they are made from is
    // what it was (scene.cpp plan_plants lists it; plan_plant_list makes them, plan_plant_list_reference is the way they
    // were made every frame until 2026-10-12, for the check).
    struct PlantPlan {
        // A plant: the key its group's signature is made of, its instance (instances_ index); and, set again every frame,
        // which build of its mesh's micromaps it can link (0: none) and whether they are on their way.
        struct Member { uint64_t key=0; uint32_t source=0; uint32_t link=0; bool pending=false; };
        // A cell: its members (first, count, in `members`), the signature of their keys and count, their vertices and indices.
        struct Cell { std::pair<int64_t, int64_t> cell; uint32_t first=0, count=0; uint64_t signature=0, vertices=0, indices=0; };
        std::vector<Cell> cells;      // in the order of their cells
        std::vector<Member> members;  // cell by cell, each cell's in the order of (key, source)
        std::vector<uint32_t> order;  // the plants in the order of the scene's list: each one's place in `members`
        // What it was made from.
        bool valid=false; uint64_t listEpoch=0, materialsEpoch=0, meshEpoch=0; std::vector<uint32_t> windMaterials, windNow;
        // Each instance's key, kept while the scene's list and the materials are the ones it was hashed from and its mesh
        // has as many submeshes (keyGeometries; 0: not hashed yet).
        uint64_t keyListEpoch=0, keyMaterialsEpoch=0; std::vector<uint64_t> keys; std::vector<uint32_t> keyGeometries;
        // Room for the making, and the counts for the log ("PT plants' plan 10s").
        struct Candidate { std::pair<int64_t, int64_t> cell; uint64_t key; uint32_t source, number; };
        std::vector<Candidate> candidates; std::vector<uint8_t> windy;
        uint64_t made=0, kept=0, checks=0, whyList=0, whyMaterials=0, whyMeshes=0, whyWinds=0, since=0; uint32_t sinceCheck=0;
        bool off=false; // a check found a difference: made every frame for the rest of the session
    };
    PlantPlan plantPlan_;
    // For the log ("PT plants' plan 10s"): what PT-125's marks cost the CPU (plant_step), timed in one frame of 64.
    double plantMarksMs_=0.0; uint64_t plantMarks_=0, plantMarkFrames_=0;
    uint64_t materialsEpoch_=1, meshEpoch_=1; // counted on wherever materials_, or a mesh's being ready and its records, are written
    uint64_t listEpoch_=1;                    // counted on wherever instances_ and materialIndices_ become other bytes (set_lists, clear)
    void plan_plant_list(PlantPlan& plan, bool split);
    void plan_plant_list_reference(PlantPlan& out, bool split);
    static bool same_plant_list(const PlantPlan& a, const PlantPlan& b);
    void plan_plants(ID3D12Device5* device, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue,
                     std::vector<uint8_t>& grouped, std::vector<PlantUse>& uses);
    // The groups in use merged where new, posed for this frame and their BLAS built or refitted.
    void pose_plants(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                     uint64_t retireValue, const std::vector<PlantUse>& uses, const double camera[3], Barriers& barriers);
    void release_plant_group(PlantGroup& g, DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue);
    // The water's surface (scene_water.cpp). plan_water: which units this frame wants of the water meshes it traces
    // (waterRecords, each one's instances_ index in waterSources), made where new, and the groups they are traced in
    // (made where new, put together by a budget); the groups' mesh records (waterMeshRecords_) and their units' geometry
    // records (waterGeometries_: geometryBase is where the frame's list will hold the first). pose_water: the units'
    // vertices written for this frame and their groups' BLAS built or refitted, then an instance a group.
    void plan_water(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release, uint64_t retireValue,
                    const std::vector<WaterRecordGpu>& waterRecords, const std::vector<uint32_t>& waterSources, const double origin[3], const double camera[3],
                    uint32_t geometryBase, Barriers& barriers);
    void pose_water(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release, uint64_t retireValue,
                    uint32_t meshBase, uint32_t materialBase, std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& descs, std::vector<InstanceRecordGpu>& records, Barriers& barriers);
    void release_water(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue);
    // A unit about to leave the map: its group's entry no longer names it.
    void water_unit_gone(WaterUnit& u);
    // A group of these units, in this order (all of one slot and level, all wanted this frame): its blocks, its view, its
    // description, each unit's place in it. Null where a block could not be had (nothing is changed then).
    WaterGroup* water_group_make(ID3D12Device5* device, DescriptorHeap& heap, const std::vector<WaterUnit*>& units);
    // A group let go: its blocks back in their pools, its view given back, its units without a group.
    void water_group_free(WaterGroup& g, DescriptorHeap* heap);
    // FOR A CHECK (recordCheck_): what is kept of the groups agrees with itself and with what would be made again.
    void water_check();
    // A water mesh whose triangles the CPU no longer holds (its mesh was traced as something else first): copied back from
    // the tracer's own buffers, ready some frames later (true once the mesh has them again).
    bool water_triangles(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, Mesh& mesh, DeferredRelease& release, uint64_t retireValue, Barriers& barriers);
    bool water_block(ID3D12Device5* device, DescriptorHeap& heap, WaterPool& pool, uint64_t bytes, bool blas, uint32_t& page, uint32_t& at, uint32_t& block);
    void water_block_free(WaterPool& pool, uint32_t page, uint32_t at, uint32_t block);
    void build_blas(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, Mesh& mesh, uint64_t& scratchOffset, uint64_t compactSize=0);
    void build_light_grid(const double corner[3]);
    // The light lists' entries into gridIndexBuffer_: the cells' through the frame's ring, the far lists' copied on the GPU
    // from an upload of their own that is written only when they are made anew (PT-99; scene.cpp).
    void upload_light_lists(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                            DeferredRelease& release, uint64_t retireValue, Barriers& barriers, ID3D12Resource* staged=nullptr, uint64_t stagedAt=0);
    // The emissive submeshes of the static instances this frame traces (records, their instances_ indices in sources):
    // which are lights, their slots, clusters and instance table, uploaded for record_emitters when anything they depend
    // on changed.
    void build_emitters(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                        uint64_t retireValue, const double origin[3], const std::vector<uint32_t>& sources, Barriers& barriers);
    // materials: the instance's submeshes' material indices (the scene's list, or a moving thing's own).
    void want_omm(Mesh& mesh, const uint32_t* materials);
    bool omm_matches(const Mesh& mesh, const uint32_t* materials) const;
    // Whether a mesh with these materials can have micromaps; whether the ones it has were made for materials that
    // changed since; an alpha-tested instance's turn at its mesh's micromaps (true: it traces with them this frame);
    // and a mesh's micromaps let go of (their memory once the GPU is past this frame).
    bool omm_possible(const Mesh& mesh, const uint32_t* materials) const;
    bool omm_stale(const Mesh& mesh) const;
    uint32_t omm_level(const uint32_t* materials, size_t count, uint64_t alphaTriangles) const; // (for measuring: TGPT_OMM_TRIAL_LEAVES_*)
    bool omm_instance(Mesh& mesh, const uint32_t* materials, uint64_t retireValue);
    void release_omm(Mesh& mesh, uint64_t retireValue);
    // Whether a material's alpha test reads the game's own texture this frame (pt_common.hlsli shared_cutout).
    bool shared_cutout(uint32_t material) const;
    // A stage's scratch for the acceleration structures it builds this frame, side by side (scene.cpp scratch_place).
    void fit_scratch(ID3D12Device5* device, DeferredRelease& release, uint64_t retireValue, Buffer& scratch, uint64_t& peak,
                     uint64_t total, uint64_t largest, const char* whose);
    // A structured buffer re-created at the next power of two when it has to grow; its contents are
    // copied from the upload ring (or a buffer of its own) each time they change.
    struct Structured { Buffer buffer; uint32_t srv=kInvalid, capacity=0, stride=0; };
    void upload_structured(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                           DeferredRelease& release, uint64_t retireValue, Structured& target, const void* data, uint32_t count, uint32_t stride, Barriers& barriers,
                           ID3D12Resource* staged=nullptr, uint64_t stagedAt=0);

    // (PT-110's M5, omm: the mesh's structure with micromaps, not its plain one; `bytes` its size as built and `serial`
    // which build of the mesh's micromaps it is, Mesh::ommSerial: maps made anew since are another structure.)
    struct CompactEntry { int32_t handle; uint64_t source; bool omm=false; uint64_t bytes=0; uint32_t serial=0; };
    struct CompactBatch { Buffer readback; uint64_t fence=0; std::vector<CompactEntry> entries; };
    std::deque<CompactBatch> compactBatches_;
    uint64_t completed_=0, compactBefore_=0, compactAfter_=0;
    void compact_meshes(ID3D12Device5*, ID3D12GraphicsCommandList4*, uint64_t);
    // PT-87 (docs/agents/memory-2.md): the same for the plants' groups, whose structures were kept as built (67 bytes a
    // triangle: 428 MB of a town's 783). A group's build names the size its structure compacts to; some frames later the
    // size is here, the structure is copied compacted into a buffer of that size, and the wind's refits go on in that one.
    // An entry: the group's cell and serial (a group made anew in the cell meanwhile is another), in the read-back's order.
    struct PlantCompactEntry { std::pair<int64_t, int64_t> cell; uint64_t serial; };
    struct PlantCompactBatch { Buffer readback; uint64_t fence=0; std::vector<PlantCompactEntry> entries; };
    std::deque<PlantCompactBatch> plantCompactBatches_;
    uint64_t plantSerial_=0, plantCompactBefore_=0, plantCompactAfter_=0; uint32_t plantCompacted_=0;
    void compact_plants(ID3D12Device5*, ID3D12GraphicsCommandList4*, DeferredRelease&, uint64_t, Barriers&);
    std::vector<int32_t> retiredMeshes_;
    void retire_meshes(DescriptorHeap&, DeferredRelease&, uint64_t, const std::vector<TGPT_SkinnedInstance>&);
    // The water's surface: each slot's water this frame, whether the scene's list holds any, the surfaces and the units by
    // key, the units this frame uses (sorted by how often they cut, the finest first) with their mesh records and triangle
    // records as last uploaded, the pools, the pattern of indices and the lattice's table (both made once).
    WaterSlot waterSlots_[4];
    struct WaterReadback { Buffer buffer; uint64_t positionBytes=0, indexBytes=0, frame=0; };
    std::map<int32_t, WaterReadback> waterReadbacks_;
    bool sceneHasWater_=false;
    uint64_t waterFrame_=0;
    std::map<uint64_t, WaterSurface> waterSurfaces_;
    // PT-114 (water_tiles): each slot's surfaces of the last plan (plan_water; the map's own, which a plan keeps while it
    // uses them), and whether they are all of the slot's water in the top level (false from the moment a frame's record
    // begins until its plan has them: a record that ends early proves nothing).
    std::vector<const WaterSurface*> waterCover_[4];
    bool waterCoverKnown_[4]={false, false, false, false};
    std::map<std::array<uint64_t, 4>, WaterUnit> waterUnits_;
    // PT-99: the same units by a hash of their keys (plan_water looks every piece up every frame): each key's unit IN the
    // map above (a map's nodes stay where they are until they are erased). Written wherever the map gains or loses a unit.
    struct WaterKeyHash {
        size_t operator()(const std::array<uint64_t, 4>& k) const noexcept {
            uint64_t h=k[0];
            for(int i=1;i<4;++i) h^=k[i]+0x9E3779B97F4A7C15ull+(h<<6)+(h>>2);
            h^=h>>32; h*=0xD6E8FEB86659FD93ull; h^=h>>32;
            return size_t(h);
        }
    };
    std::unordered_map<std::array<uint64_t, 4>, WaterUnit*, WaterKeyHash> waterUnitIndex_;
    std::vector<WaterUnit*> waterUses_;
    // PT-99 (docs/agents/waterblas.md): the groups the units are traced in (scene.hpp WaterGroup), in the order they were
    // made; those with a unit this frame wants, in that order (their mesh records' and instances' order); each one's
    // units' geometry records (the frame's list holds them after the plants' groups'); the origin the collapsed units'
    // points were written for; this frame's counts for the log; the test's switch (TGPT_TEST_WATER_UNGROUPED: a group
    // holds one unit and nothing is put together: a bottom level, a refit and an instance a unit, as before).
    std::vector<std::unique_ptr<WaterGroup>> waterGroups_;
    std::vector<WaterGroup*> waterGroupUses_;
    std::vector<GeometryRecordGpu> waterGeometries_;
    double waterOrigin_[3]{};
    uint64_t waterGroupSerial_=0, waterGroupsMadeEver_=0, waterMergesEver_=0;
    uint32_t waterGroupsNew_=0, waterGroupsMerged_=0, waterMergedFrom_=0, waterCollapsed_=0, waterDead_=0;
    uint64_t waterBuiltTriangles_=0;
    uint32_t waterUsedVertices_=0; uint64_t waterUsedTriangles_=0; // this frame's traced units', for the log and the status
    bool waterUngrouped_=false;
    std::vector<MeshRecordGpu> waterMeshRecords_, uploadedWaterMeshRecords_;
    std::vector<WaterRecordGpu> uploadedWaterRecords_; // the water meshes' records as last uploaded (waterRecordBuffer_)
    std::vector<WaterTriGpu> waterTris_, uploadedWaterTris_;
    uint32_t waterLevelTris_[kWaterLevels+1]{}, waterMade_=0;
    WaterPool waterVertexPool_, waterBlasPool_;
    uint32_t waterCutsOff_=0, waterPagesBack_=0; // PT-87: the test's switch as plan_water found it; pages given back since the scene started
    bool waterCutsTest_=false;                   // the switch is set (to anything): the check reads the water's line of every frame that made pieces
    Buffer waterIndices_, waterScratch_;
    uint32_t waterIndicesSrv_=kInvalid;
    Structured waterTriBuffer_, waterTableBuffer_;
    std::vector<uint32_t> waterTable_;               // the lattice's vertices level by level, then where each point is among them
    std::vector<std::unique_ptr<Mesh>> meshes_;      // by handle - 1
    std::vector<Mesh*> pending_;                     // not yet ready or failed
    // A stackless interval BVH: leaves keep draw order, so a hit needs no capped overlap list or GPU sort.
    // Projector nodes test a point; nodes containing mesh decals test the incoming ray segment.
    struct DecalNode { float lo[3]; uint32_t next; float hi[3]; uint32_t decal; uint32_t mesh=0, pad[3]{}; };
    static_assert(sizeof(DecalNode)==48, "decal tree shader record");
    // mesh: the handle of the mesh whose bottom level `instance` names BY ITS ADDRESS, while the uploaded decal record
    // names this top level (0: no record does). That address is not the mesh's for good: a still mesh's structure is
    // copied compacted to another place a few frames after its build, and goes when the mesh does. upload_decals looks
    // at it every frame (docs/agents/stale.md F1).
    struct DecalMesh { Buffer tlas; uint32_t srv=kInvalid; D3D12_RAYTRACING_INSTANCE_DESC instance{}; uint32_t mesh=0; };
    std::vector<DecalMesh> decalMeshes_;
    Buffer decalScratch_;
    std::vector<TGPT_Decal> decals_;
    std::vector<DecalNode> decalNodes_; uint64_t decalLastLog_=0;
    bool decalsDirty_=true; uint64_t decalShareGeneration_=~0ull;
    double decalOrigin_[3]{};
    Structured decalBuffer_, decalNodeBuffer_;
    void upload_decals(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                       DeferredRelease& release, uint64_t retireValue, const double origin[3], Barriers& barriers);
    std::vector<TGPT_Material> materials_;
    std::vector<TGPT_Material> gpuMaterials_;        // as uploaded: shared textures' handles resolved to numbers (PT-46)
    uint64_t shareGeneration_=~0ull;                 // share::generation() they were resolved at
    std::vector<uint32_t> mapRecords_, gpuMapRecords_; // set_map_record's rows (handles), and as uploaded (numbers)
    std::unordered_map<size_t, uint64_t> mapTextures_;  // a handle's word in mapRecords_: the D3D11 texture it was read from
    bool mapRecordsDirty_=false;
    uint64_t mapShareGeneration_=~0ull;
    std::vector<uint32_t> programRows_, gpuProgramRows_; // TGPT_SetMaterialPrograms' rows (handles), and as uploaded (numbers)
    bool programsDirty_=false;
    uint64_t programShareGeneration_=~0ull;
    float programSaturation_=0;
    Structured programBuffer_;
    std::vector<TGPT_Instance> instances_;
    std::vector<uint32_t> materialIndices_;
    std::shared_ptr<const SceneList> sceneList_;     // the list instances_ and materialIndices_ were taken from
    std::shared_ptr<const MovingList> moving_;       // this frame's things the game moves
    Structured movingPreviousBuffer_;
    uint64_t replacedGlowHash_=0;                    // the glowing instances the moving things stand in for, as the emitters were built
    std::vector<uint8_t> replaced_;                  // by instances_ index: a moving thing stands in for it this frame
    std::vector<TGPT_Light> lights_, previousLights_;
    bool materialsDirty_=true, indicesDirty_=true, meshRecordsDirty_=true;
    uint32_t subsurfaceMaterials_=0; // of materials_: those with TGPT_MATERIAL_SUBSURFACE (SceneStats.subsurfaceMaterials)
    std::vector<MeshRecordGpu> meshRecords_;
    std::vector<GeometryRecordGpu> geometryRecords_;
    Structured meshBuffer_, geometryBuffer_, materialBuffer_, indexListBuffer_, instanceBuffer_, lightBuffer_, previousLightBuffer_;
    Structured mapRecordsBuffer_;
    Structured gridCellBuffer_, gridIndexBuffer_;
    std::vector<uint32_t> gridCells_, gridIndices_; // cells: first entry, count; entries: light, cumulative weight (the cells' alone: the far lists' are farEntries_)
    std::vector<float> gridWeights_;                 // an entry's weight while the grid is built
    light_grid::Scratch gridScratch_;                // room the cells' builder keeps between builds (no result)
    // The far lists' entries as the GPU copies them behind the cells' (upload_light_lists): written once when the far
    // lists are made, a new buffer each time (farUploadDirty_).
    Buffer farUpload_; bool farUploadDirty_=true;
    // FOR A CHECK (PT-99, docs/agents/reccpu.md): the environment's TGPT_TEST_RECORD_CHECK, read once: every result that
    // is made a cheaper way is also made the old way and compared, each time it is made; the log says so once for each
    // (0 the light grid's cells, 1 the plants' plan, 3 the water's index, 4 the water's groups), and at every difference.
    bool recordCheck_=false, recordCheckRead_=false, recordCheckSaid_[5]{};
    // The far lists (build_light_grid), kept from build to build: each list's first entry and count, the entries (light,
    // cumulative weight), and what they were made from (each light's kind and place, the grid's corner).
    struct FarLight { float position[3]; bool usable, fallsOff; };
    std::vector<uint32_t> farCells_, farEntries_;
    std::vector<FarLight> farLights_;
    double farCorner_[3]{};
    double gridCorner_[3]{};
    // What the grid was last made for (record: the corner, each light's place to a quarter metre, kind, range to a
    // sixteenth and falls-off mark, the clusters' start and count, each cluster's place, range and red), the same numbers
    // of this frame, and how many lights the kept ones are of. Compared whole where a hash of them was compared.
    std::vector<uint64_t> gridKey_, gridKeyNow_; size_t gridKeyLights_=0;
    bool gridBuilt_=false;
    // For the log ("PT light grid 10s"): how often the grid was made anew, and what had changed each time.
    struct GridWhy { uint32_t builds=0, first=0, corner=0, count=0, kind=0, range=0, place=0, flag=0, clusters=0, rangeMost=0, placeMost=0; uint64_t frames=0, since=0;
                     double cellsMs=0, farMs=0, waitedMs=0; };
    GridWhy gridWhy_;
    // The grid's build beside the record (record, build_light_grid): the corner it is made for, and how long its two
    // parts took on its own thread (the cells, the far lists). Written by the build, read by record after it.
    double gridJobCorner_[3]{}, gridJobMs_[2]{};
    // The grid's two arrays as its build leaves them for the GPU (PT-99): upload buffers of the scene's own, each written
    // again only once the GPU is past the frame that last copied from it (fence against completed_). gridStage_: the one
    // record chose for this frame's build (-1: none, the arrays go through the frame's ring); gridStaged_: the build
    // left them there, the cells at 0 and the cells' entries at gridStagedEntriesAt_; gridStageWanted_: their bytes.
    struct GridStage { Buffer buffer; uint64_t fence=0; };
    std::array<GridStage, 3> gridStages_;
    int gridStage_=-1; bool gridStaged_=false; uint64_t gridStagedEntriesAt_=0, gridStageWanted_=0;
    // Emissive triangles. The CPU part is made again when the scene, its materials, a mesh or the row changed; the GPU
    // part (the records) also when a light moved (a light's own surface is found by the lights near it) or the origin.
    struct EmitterSlots { uint32_t first=0, count=0; uint64_t build=0; };
    std::unordered_map<uint64_t, EmitterSlots> emitterSlots_;     // by emitter (instance transform, mesh, submesh, material)
    std::vector<std::pair<uint32_t, uint32_t>> freeSlots_;         // free slot ranges (first, count)
    uint32_t slotCapacity_=0;                                      // slots handed out so far, the clusters' records after them
    uint64_t emitterBuild_=0;
    bool emittersDirty_=true, emissiveLightsBuilt_=false;
    uint64_t emitterLightsHash_=0; double emitterOrigin_[3]{};
    std::vector<EmitterGpu> emitterList_;                          // this build's emitters, their transforms relative to the origin then
    std::vector<std::array<float, 12>> emitterWorld_;              // and in world space
    std::vector<uint32_t> slotOwners_, emitterTable_;              // per slot its emitter; the instance table
    // The instance table is BY RECORD NUMBER, and the records are laid out anew every frame (a plant has none of its own
    // once its group is made): so what the table was made from is kept, each emitter's instance (instances_ index),
    // submesh, first slot and its mesh's submesh count, with the frame's records it was last laid out for (each record's
    // instances_ index), and build_emitters lays the table out again whenever the records moved (docs/agents/stale.md F2).
    struct EmitterPlace { uint32_t source, geometry, first, geometries; };
    std::vector<EmitterPlace> emitterPlaces_;
    std::vector<uint32_t> emitterTableSources_;
    std::vector<ClusterInfoGpu> clusterInfo_;
    std::vector<TGPT_Light> clusterLights_;                        // the sampled clusters as the light grid takes them (world, type 3)
    float brightestEmitter_=0.f, brightestEmitterRaw_=0.f;
    Structured emitterListBuffer_, slotOwnerBuffer_, clusterInfoBuffer_, emitterTableBuffer_;
    std::array<Buffer, 2> emitterRecords_; std::array<uint32_t, 2> emitterRecordsSrv_{kInvalid, kInvalid}, emitterRecordsUav_{kInvalid, kInvalid};
    std::array<uint32_t, 2> emitterRecordsCapacity_{}, emitterRecordsCount_{};
    int emitterCurrent_=-1;                                        // the buffer the last build wrote
    bool emitterPassPending_=false;
    // The wind on swaying cloth (TGPT_SetClothWind) by material, the shader time, the copies by key and the records the mesh
    // buffer holds after the registered meshes' (this frame's copies, in order) as last uploaded.
    float windTime_=0;
    std::unordered_map<uint32_t, TGPT_ClothWind> winds_;
    std::vector<uint8_t> clothWindy_; bool clothWindPastTable_=false; // which materials winds_ holds, by a table (set_wind)
    std::unordered_map<uint64_t, SwayCopy> swayCopies_;
    std::vector<MeshRecordGpu> swayRecords_, uploadedSwayRecords_;
    Structured swayRecordBuffer_;
    Structured waterRecordBuffer_;
    Buffer swayScratch_;
    uint64_t swayFrame_=0;
    // Grass and leaves in the wind: the frame's wind, each material's wind (by material index, into plantWinds_), the noise
    // maps and the ones uploaded, the groups by cell, and the records the mesh and geometry buffers hold after the swaying
    // cloths' (this frame's groups, in order) as last uploaded.
    TGPT_PlantWindFrame plantFrame_{};
    std::vector<TGPT_PlantWind> plantWinds_;
    std::unordered_map<uint32_t, uint32_t> plantWindOf_;
    std::vector<uint32_t> plantWindTable_; // the same by a table: a material's wind, kInvalid without one (set_plant_wind)
    bool plantWindPastTable_=false;        // a wind's material is numbered past the table: the map is asked for such numbers
    std::shared_ptr<const WindNoise> windNoise_, uploadedNoise_;
    std::map<std::pair<int64_t, int64_t>, PlantGroup> plantGroups_;
    std::vector<MeshRecordGpu> plantRecords_, uploadedPlantRecords_;
    std::vector<GeometryRecordGpu> plantGeometries_, uploadedPlantGeometries_;
    std::vector<PlantWindGpu> uploadedPlantWinds_;
    std::vector<PlantGroup*> uploadedPlantGroups_;   // whose member and range records the buffers hold, in order
    Structured plantGroupBuffer_, plantMemberBuffer_, plantRangeBuffer_, plantWindBuffer_, noiseBuffer_;
    Buffer plantScratch_;
    std::vector<uint64_t> materialSignatures_;       // per material: what opacity micromaps depend on
    std::vector<uint8_t> tilesSettled_, alphaTilesSettled_;
    std::deque<Mesh*> ommQueue_;
    uint64_t ommInputBytes_=0;
    bool ommBudgetLogged_=false;
    // PT-110, for the log alone: a frame's bakes on their way back to the CPU in one read-back buffer (a mesh's part: its
    // pieces, 64 bytes a triangle, then its indices, 4 bytes a triangle), counted into each mesh once the GPU is past the
    // frame that copied them (count_omm), and then let go. `fence`: that frame's. A mesh that goes meanwhile, or whose
    // micromaps do, has its part forgotten (forget_omm_readback).
    struct OmmReadback { struct Part { Mesh* mesh=nullptr; uint32_t triangles=0; uint64_t at=0; uint32_t bytes=kOmmBytes; }; Buffer buffer; uint64_t fence=0; std::vector<Part> parts; };
    std::deque<OmmReadback> ommReadbacks_;
    void forget_omm_readback(const Mesh* mesh) { for(auto& r:ommReadbacks_) for(auto& p:r.parts) if(p.mesh==mesh) p.mesh=nullptr; }
    void count_omm(DeferredRelease& release, uint64_t retireValue);
    uint32_t ommTrialLast_=0;                        // the trial as the frame before had it (a partitioned top level is written whole when it changes)
    uint64_t ommCompactBefore_=0, ommCompactAfter_=0; // PT-110's M5: the structures with micromaps compacted since the scene started, for the log
    uint32_t ommCompacted_=0;
    // PT-110's M1: the index buffers of micromaps let go of, until the GPU is past the frame that did (release_omm has
    // no release queue of its own): each with that frame's fence.
    std::deque<std::pair<uint64_t, Buffer>> ommIndicesRetired_;
    Buffer scratch_, tlasScratch_, tlasBuffer_, ommScratch_, skinScratch_;
#if TGPT_MEGA_GEOMETRY
    MegaTopLevel megaTop_;                           // PT-109 on trial: the partitioned top level (megaTrial 1)
    // The still meshes' bottom levels built at their address since the partitioned top level was last told (a new mesh's,
    // a compacted copy, one with micromaps): an instance of one of them is written again whatever its slot holds, since
    // a new structure may stand where a retired one stood. Kept only while the partitioned top level lives.
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> megaFresh_;
    void mega_fresh(D3D12_GPU_VIRTUAL_ADDRESS va) { if(va && megaTop_.bytes()) megaFresh_.push_back(va); }
    // PT-109's small plants as clusters (on trial, megaTrial 5): each wanted mesh's cut and templates, by its handle; the
    // meshes the plan wanted cut (a small plant's mesh without one); the positions and indices of such a mesh read back
    // from the geometry arena (its CPU copies are gone once it is ready), each with the frame it was recorded in; the
    // frame's stamps and bottom levels; and whether a call of the clusters failed (they are then off for good, said once).
    std::map<int32_t, MegaMeshClusters> megaMeshes_;
    std::set<int32_t> megaWanted_;
    mutable std::unordered_map<int32_t, std::array<float, 6>> megaBoxes_; // each plant mesh's box (mega_small), by its handle
    struct MegaReadback { Buffer buffer; uint64_t fence=0, positionBytes=0, indexBytes=0; };
    std::map<int32_t, MegaReadback> megaReadbacks_;
    MegaClusterBuilder megaBuilder_;
    double megaCpu_[8]{}; // plan, pose/build, clusters (inside pose), top, scene, rewrites, bottom levels, bottom calls
    bool megaClustersFailed_=false;
    uint64_t megaCutTriangles_=0, megaCutMeshes_=0; double megaCutSeconds_=0; // (for the log)
    bool mega_cut_mesh(Mesh& mesh, const float* positions, const uint32_t* indices, size_t indexCount);
    void mega_prepare(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue, Barriers& barriers);
    bool mega_small(const Mesh& mesh, const float transform[12]) const;
    void mega_fail(const std::string& what);
    // The trial's numbers (megageo-5): plants made of clusters at all (5: the small ones, in groups of their own; 7 and 8:
    // every one, each its own bottom level, NVIDIA's layout), where the driver and its pipelines have them and no call
    // failed; and whether those clusters carry their meshes' micromaps (5 and 7, where the driver reports the clusters' own).
    bool mega_clusters_on() const { return (megaTrial==5 || megaTrial==7 || megaTrial==8) && megaCaps.clusters && mega_cluster_pipelines() && !megaClustersFailed_; }
    bool mega_maps_on() const { return mega_clusters_on() && (megaTrial==5 || megaTrial==7) && megaCaps.omm && mega_cluster_micromaps(); }
    // (A plant of the trial's clusters: every plant at 7 and 8, a small one at 5.)
    bool mega_cluster_plant(const Mesh& mesh, const float transform[12]) const { return megaTrial==7 || megaTrial==8 || mega_small(mesh, transform); }
    // How many plant meshes' micromaps were made again for the clusters' own micromap array (baked before the trial asked for it), for the log.
    uint64_t megaRebaked_=0, megaMapsBuilt_=0, megaMapsFailed_=0;
#endif
    // The most each stage's scratch was asked for since the last look (record: every 256 frames a buffer four times
    // larger than that goes), and whether the signatures were made with the game's own textures read.
    uint64_t skinScratchPeak_=0, plantScratchPeak_=0, swayScratchPeak_=0;
    bool sharedLast_=false;
    std::vector<uint32_t> combinedIndices_;          // the material index list with this frame's characters' appended
    bool combinedLast_=false;                        // the last upload had characters' indices
    uint64_t tlasCapacity_=0;
    uint32_t clearGeneration_=0;
    uint64_t readBytesThisFrame_=0;
    double clock_=1.0;                               // set_clock: this frame's share of a second, in frames of 60 a second
    // A budget counted "a frame", as much as that many frames of 60 a second would take in.
    uint64_t by_clock(uint64_t aFrame) const { return uint64_t(double(aFrame)*clock_+0.5); }

    // The conversion worker.
    std::thread thread_;
    std::mutex workMutex_;
    std::condition_variable workSignal_;
    std::deque<Mesh*> work_, done_;
    Mesh* converting_=nullptr;
    std::condition_variable idleSignal_;
    bool stopping_=false;
    mutable std::mutex statsMutex_;
    SceneStats stats_;
};
} // namespace tgpt
