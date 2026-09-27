// SPDX-License-Identifier: MIT
// The traced scene on the render thread: meshes read back from the game's D3D11 device, converted to
// compact positions/UVs/colors/32-bit indices on a worker thread, uploaded to D3D12 with one bottom-level
// acceleration structure each; materials, instances and lights as structured buffers; and one top-level
// acceleration structure a frame, relative to a scene origin near the camera.
#pragma once
#include "runtime.hpp"
#include <condition_variable>
#include <thread>

namespace tgpt {

struct MeshRecordGpu { uint32_t positions, uvs, colors, indices, geometryBase, geometryCount, vertexCount, pad; };
struct GeometryRecordGpu { uint32_t indexStart, indexCount, pad0, pad1; };
struct InstanceRecordGpu { uint32_t mesh, materialBase, flags, pad; };
static_assert(sizeof(MeshRecordGpu)==32 && sizeof(GeometryRecordGpu)==16 && sizeof(InstanceRecordGpu)==16, "shader records");

// Emissive triangles as lights: a submesh's triangles in clusters of kClusterTriangles consecutive ones (a mesh's index
// order keeps them together), each with its bounds and area in the mesh's own space, and its area spread over the axes
// (each triangle's area times its normal's squared components: an instance's scale makes it the cluster's area in the
// world, world_area in scene.cpp). Made with the conversion and kept when the CPU copies go: the light grid places a
// cluster by them, before the GPU has read its triangles (pt_emissive).
constexpr uint32_t kClusterTriangles=32;
struct TriangleCluster { float lo[3], hi[3], area, spread[3]; };

struct MeshData {
    std::vector<float> positions, uvs;
    std::vector<uint32_t> colors, indices;
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
    MeshData data;
    // Views into the geometry and acceleration structure arenas, which own the memory.
    D3D12_GPU_VIRTUAL_ADDRESS positionsVa=0, indicesVa=0, blasVa=0;
    uint32_t positionsSrv=kInvalid, uvsSrv=kInvalid, colorsSrv=kInvalid, indicesSrv=kInvalid;
    uint32_t geometryBase=0;
    uint64_t triangles=0, bytesOnGpu=0;
    // Opacity micromaps (DirectX Raytracing 1.2), for the materials the first alpha-tested instance gives
    // the submeshes: baked once the alpha-tested submeshes' textures are in the atlas, then linked into a
    // second BLAS that every instance uses from then on (with micromaps disabled where its materials differ).
    enum class Omm { Unchecked, Waiting, Linked, None } omm=Omm::Unchecked;
    std::vector<uint32_t> ommMaterials;  // per submesh: the material index the micromaps were made for
    std::vector<uint64_t> ommSignatures; // and that material's contents then
    uint32_t alphaTriangles=0;
    D3D12_GPU_VIRTUAL_ADDRESS ommBlasVa=0;
    // A character: its positions in a buffer of its own that the tracer's skin pass writes every frame (raw
    // UAV), and its BLAS updated in place from them (fast to build, storage reserved when it became ready): refitted
    // to them, and rebuilt every kCharacterRebuild frames (sinceBuild: frames since it was, kInvalid before the first).
    bool skinned=false;
    Buffer skinPositions;
    uint32_t skinUav=kInvalid;
    uint64_t skinScratchBytes=0;
    uint32_t sinceBuild=kInvalid;
};

struct SceneStats {
    uint32_t meshesReady=0, meshesFailed=0, meshesQueued=0, instancesTraced=0;
    uint64_t triangles=0, geometryBytes=0, accelerationBytes=0;
    uint32_t gridCellsLit=0, gridEntries=0, gridMostInCell=0, gridBuilds=0;
    uint32_t ommMeshes=0, ommWaiting=0, ommInstances=0; uint64_t ommTriangles=0, ommBytes=0;
    uint32_t charactersTraced=0; uint64_t characterTriangles=0;
    uint32_t swayingInstances=0; // instances all of whose materials the wind moves (kMaskSways)
    uint32_t clothInstances=0;   // instances with a thin cloth material (TGPT_MATERIAL_THIN_CLOTH)
    // Emissive surfaces (build_emitters): submeshes and triangles in the emitter records, the clusters and triangles sampled
    // as lights, and the submeshes left out for the triangle budget.
    uint32_t emitters=0, emitterTriangles=0, emitterClustersSampled=0, emitterTrianglesSampled=0, emittersOverBudget=0, emitterBuilds=0;
};

// The emitter records' layout (pt_common.hlsli EMITTER_*): light indices from kEmitterBase on name them, triangles in their
// slots first and the clusters the light grid lists after them.
constexpr uint32_t kEmitterBase=1u<<20;
// Up to this many emissive triangles are sampled as lights or kept as a light's own surface, the brightest submeshes first.
constexpr uint32_t kEmitterBudget=16384;
// The pass that writes the emitter records (pt_emissive.hlsl): what one emissive submesh of an instance is, where its
// triangles' slots start, and each cluster's slots, what it is (EMITTER_SAMPLED) and how far its light is sampled.
struct EmitterGpu { float transform[12]; uint32_t mesh, geometry, material, firstSlot, triangles, firstCluster, key, flags; };
struct ClusterInfoGpu { uint32_t firstSlot, count, state; float reach; };
static_assert(sizeof(EmitterGpu)==80 && sizeof(ClusterInfoGpu)==16, "emitter records");

// Instance masks (pt_common.hlsli MASK_*): an instance all of whose materials the wind moves (TGPT_MATERIAL_SWAYS) has its
// own bit, which rays from HDRP's surfaces leave out near them; everything else has the rest.
constexpr uint32_t kMaskSways=0x02, kMaskStill=0xFD;

// Opacity micromaps: 4^4 micro-triangles of 2 bits each, 64 bytes a triangle.
constexpr uint32_t kOmmLevel=4, kOmmBytes=64;

// The light grid: 8 m cells in a box around the camera, each listing every point and spot light whose
// range reaches it. Path vertices choose among the lights that can light them, not among all of them.
constexpr uint32_t kGridDims[3]={48, 20, 48};
constexpr double kGridCell=8.0, kGridSnap=32.0, kGridPad=0.25;

class GpuScene {
public:
    GpuScene();
    ~GpuScene();
    // Takes what the plugin registered since the last frame; a scene the plugin cleared is dropped first (true then).
    bool intake(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue);
    // The plugin's clear this scene has taken in: texture bakes wait while a newer one is pending (their tiles
    // would be marked settled here and then dropped with the scene).
    uint32_t clear_generation() const { return clearGeneration_; }
    // D3D11, inside the tracer's isolated context state: copies for new meshes and read-backs of copied ones.
    void pump_d3d11(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t frame);
    // D3D12: uploads, BLAS builds, record buffers, lights and this frame's TLAS. The origin is where the
    // TLAS puts world position zero (the camera, snapped to a coarse grid).
    // Characters whose positions the tracer's skin pass wrote this frame are rebuilt and traced with the rest.
    void record(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                DeferredRelease& release, uint64_t retireValue, const double origin[3], const double camera[3],
                const std::vector<TGPT_SkinnedInstance>& skinned);
    // A ready character mesh with this many vertices: the buffer its positions go to (its UAV in uav), else null.
    Buffer* skinned_positions(int32_t handle, uint32_t vertexCount, uint32_t& uav);
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

    uint32_t instancesSrv=kInvalid, meshesSrv=kInvalid, geometriesSrv=kInvalid, materialsSrv=kInvalid;
    uint32_t materialIndicesSrv=kInvalid, lightsSrv=kInvalid, lightCount=0;
    // The previous traced frame's lights, positions relative to this frame's origin: RTXDI's temporal reuse weighs a
    // light at the previous surface as it was then (count 0: none).
    uint32_t previousLightsSrv=kInvalid, previousLightCount=0;
    // The brightest light's illuminance 1 m away (a directional light's: its own), luminance, HDRP's units; emissive
    // submeshes sampled as lights count with the intensity their triangles have together at most.
    float brightest_light() const;
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
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blas_inputs(const Mesh& mesh, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& storage) const;
    void build_blas(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, Mesh& mesh, uint64_t& scratchOffset);
    void build_light_grid(const double corner[3]);
    // The emissive submeshes of the static instances this frame traces (records, their instances_ indices in sources):
    // which are lights, their slots, clusters and instance table, uploaded for record_emitters when anything they depend
    // on changed.
    void build_emitters(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring, DeferredRelease& release,
                        uint64_t retireValue, const double origin[3], const std::vector<uint32_t>& sources, Barriers& barriers);
    void want_omm(Mesh& mesh, uint32_t materialBase);
    bool omm_matches(const Mesh& mesh, uint32_t materialBase) const;
    // A structured buffer re-created at the next power of two when it has to grow; its contents are
    // copied from the upload ring (or a buffer of its own) each time they change.
    struct Structured { Buffer buffer; uint32_t srv=kInvalid, capacity=0, stride=0; };
    void upload_structured(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                           DeferredRelease& release, uint64_t retireValue, Structured& target, const void* data, uint32_t count, uint32_t stride, Barriers& barriers);

    std::vector<std::unique_ptr<Mesh>> meshes_;      // by handle - 1
    std::vector<Mesh*> pending_;                     // not yet ready or failed
    std::vector<TGPT_Material> materials_;
    std::vector<TGPT_Instance> instances_;
    std::vector<uint32_t> materialIndices_;
    std::vector<TGPT_Light> lights_, previousLights_;
    bool materialsDirty_=true, indicesDirty_=true, meshRecordsDirty_=true;
    std::vector<MeshRecordGpu> meshRecords_;
    std::vector<GeometryRecordGpu> geometryRecords_;
    Structured meshBuffer_, geometryBuffer_, materialBuffer_, indexListBuffer_, instanceBuffer_, lightBuffer_, previousLightBuffer_;
    Structured gridCellBuffer_, gridIndexBuffer_;
    std::vector<uint32_t> gridCells_, gridIndices_, gridFill_; // cells: first entry, count; entries: light, cumulative weight
    std::vector<float> gridWeights_;                            // an entry's weight while the grid is built
    double gridCorner_[3]{};
    uint64_t gridHash_=0;
    bool gridBuilt_=false;
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
    std::vector<ClusterInfoGpu> clusterInfo_;
    std::vector<TGPT_Light> clusterLights_;                        // the sampled clusters as the light grid takes them (world, type 3)
    float brightestEmitter_=0.f;
    Structured emitterListBuffer_, slotOwnerBuffer_, clusterInfoBuffer_, emitterTableBuffer_;
    std::array<Buffer, 2> emitterRecords_; std::array<uint32_t, 2> emitterRecordsSrv_{kInvalid, kInvalid}, emitterRecordsUav_{kInvalid, kInvalid};
    std::array<uint32_t, 2> emitterRecordsCapacity_{}, emitterRecordsCount_{};
    int emitterCurrent_=-1;                                        // the buffer the last build wrote
    bool emitterPassPending_=false;
    std::vector<uint64_t> materialSignatures_;       // per material: what opacity micromaps depend on
    std::vector<uint8_t> tilesSettled_, alphaTilesSettled_;
    std::deque<Mesh*> ommQueue_;
    uint64_t ommInputBytes_=0;
    bool ommBudgetLogged_=false;
    Buffer scratch_, tlasScratch_, tlasBuffer_, ommScratch_, skinScratch_;
    std::vector<uint32_t> combinedIndices_;          // the material index list with this frame's characters' appended
    bool combinedLast_=false;                        // the last upload had characters' indices
    uint64_t tlasCapacity_=0;
    uint32_t clearGeneration_=0;
    uint64_t readBytesThisFrame_=0;

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
