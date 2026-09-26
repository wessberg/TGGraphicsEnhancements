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

struct MeshData {
    std::vector<float> positions, uvs;
    std::vector<uint32_t> colors, indices;
    std::vector<GeometryRecordGpu> geometries; // index ranges into indices, one per submesh
    uint32_t vertexCount=0;
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
    // UAV), and its BLAS rebuilt in place from them (fast to build, storage reserved when it became ready).
    bool skinned=false;
    Buffer skinPositions;
    uint32_t skinUav=kInvalid;
    uint64_t skinScratchBytes=0;
};

struct SceneStats {
    uint32_t meshesReady=0, meshesFailed=0, meshesQueued=0, instancesTraced=0;
    uint64_t triangles=0, geometryBytes=0, accelerationBytes=0;
    uint32_t gridCellsLit=0, gridEntries=0, gridMostInCell=0, gridBuilds=0;
    uint32_t ommMeshes=0, ommWaiting=0, ommInstances=0; uint64_t ommTriangles=0, ommBytes=0;
    uint32_t charactersTraced=0; uint64_t characterTriangles=0;
};

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
    std::vector<TGPT_Light> lights_;
    bool materialsDirty_=true, indicesDirty_=true, meshRecordsDirty_=true;
    std::vector<MeshRecordGpu> meshRecords_;
    std::vector<GeometryRecordGpu> geometryRecords_;
    Structured meshBuffer_, geometryBuffer_, materialBuffer_, indexListBuffer_, instanceBuffer_, lightBuffer_;
    Structured gridCellBuffer_, gridIndexBuffer_;
    std::vector<uint32_t> gridCells_, gridIndices_, gridFill_; // cells: first index, count
    double gridCorner_[3]{};
    uint64_t gridHash_=0;
    bool gridBuilt_=false;
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
