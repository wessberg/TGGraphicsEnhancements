// SPDX-License-Identifier: MIT
// PT-109: what the NVIDIA driver reports of RTX Mega Geometry on the tracer's device (mega_geometry.hpp), through
// NVAPI (nvapi.h and nvapi64.lib of github.com/NVIDIA/nvapi, MIT; the implementation is the installed driver's
// nvapi64.dll, which the library loads when asked). Only compiled with CMake's TGPT_MEGA_GEOMETRY.
#include "mega_geometry.hpp"
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h> // nvapi.h names IDXGISwapChain once d3d12.h is in
#include <wrl/client.h>
#include <nvapi.h>
#include <meshoptimizer.h> // the clusters' cut (meshoptimizer, MIT; fetched by CMake with TGPT_MEGA_GEOMETRY)
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace tgpt {
namespace {
using Microsoft::WRL::ComPtr;

std::string status_text(NvAPI_Status s) {
    NvAPI_ShortString text{};
    if(NvAPI_GetErrorMessage(s, text)!=NVAPI_OK) text[0]=0;
    text[sizeof(text)-1]=0;
    return std::string(text[0] ? text : "no message")+" ("+std::to_string(int(s))+")";
}
std::string megabytes(uint64_t bytes) { char b[32]; std::snprintf(b, sizeof(b), "%.1f MB", double(bytes)/1048576.0); return b; }

// The yardstick the driver is asked about: a town square's plants and instances as the tracer counted them on
// 2026-10-09 (run 20261009-103336, `metal`: 11,303 plants posed for the wind in 636 groups, 9,513,841 vertices,
// 6,672,229 triangles; 16,234 instances traced). Today those plants hold 428 MB of plain bottom-level structures
// (about 67 bytes a triangle, docs/agents/memory-1.md) and the top level is built anew over every instance each frame.
constexpr uint32_t kYardGroups=636, kYardPlants=11303, kYardVertices=9513841, kYardTriangles=6672229, kYardInstances=16234;
// A cluster of the yardstick: at most 128 triangles and 256 vertices (the API's limit is 256 of each; the plants
// have 1.43 vertices a triangle), so at least 52,127 clusters; asked for half as many again, since no cut fills
// every cluster.
constexpr uint32_t kYardClusterTriangles=128, kYardClusterVertices=256, kYardClusters=(kYardTriangles/kYardClusterTriangles+1)*3/2;

bool requirements(ID3D12Device5* device, const NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS& inputs,
                  NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO& info, std::string& error) {
    NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS params{};
    params.version=NVAPI_GET_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO_PARAMS_VER;
    params.pInput=&inputs; params.pInfo=&info;
    const NvAPI_Status s=NvAPI_D3D12_GetRaytracingMultiIndirectClusterOperationRequirementsInfo(device, &params);
    if(s!=NVAPI_OK) { error=status_text(s); return false; }
    return true;
}
NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS triangle_inputs(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE type, uint32_t clusters,
                                                                               uint32_t triangles, uint32_t vertices) {
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS in{};
    in.maxArgCount=clusters; in.type=type;
    in.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
    in.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
    in.trianglesDesc.vertexFormat=uint32_t(DXGI_FORMAT_R32G32B32_FLOAT);
    in.trianglesDesc.maxGeometryIndexValue=4095;      // a group's submeshes (one a member's submesh)
    in.trianglesDesc.maxUniqueGeometryCountPerArg=1;  // a cluster is cut inside one submesh
    in.trianglesDesc.maxTriangleCountPerArg=kYardClusterTriangles; in.trianglesDesc.maxVertexCountPerArg=kYardClusterVertices;
    in.trianglesDesc.maxTotalTriangleCount=triangles; in.trianglesDesc.maxTotalVertexCount=vertices;
    in.trianglesDesc.minPositionTruncateBitCount=0;   // every bit of a position kept: the same triangles
    return in;
}
} // namespace

MegaCaps mega_probe(ID3D12Device* device, uint32_t vendor, std::vector<std::string>& lines) {
    MegaCaps caps;
    try {
        if(vendor!=0x10DE) {
            char b[160]; std::snprintf(b, sizeof(b), "Mega Geometry: not asked for, the card is not NVIDIA's (vendor 0x%04X); the plain acceleration structures serve", vendor);
            lines.push_back(b); return caps;
        }
        ComPtr<ID3D12Device5> d5;
        if(!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&d5)))) { lines.push_back("Mega Geometry: no ID3D12Device5; the plain acceleration structures serve"); return caps; }
        const NvAPI_Status started=NvAPI_Initialize();
        if(started!=NVAPI_OK) { lines.push_back("Mega Geometry: NVAPI did not start: "+status_text(started)+"; the plain acceleration structures serve"); return caps; }
        NvU32 driver=0; NvAPI_ShortString branch{}; NvAPI_ShortString library{};
        if(NvAPI_SYS_GetDriverAndBranchVersion(&driver, branch)==NVAPI_OK) caps.driver=driver; else branch[0]=0;
        if(NvAPI_GetInterfaceVersionString(library)!=NVAPI_OK) library[0]=0;
        branch[sizeof(branch)-1]=0; library[sizeof(library)-1]=0;
        NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAPS clusterCaps=NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_NONE;
        NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_CAPS topCaps=NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_CAP_NONE;
        const NvAPI_Status clusterStatus=NvAPI_D3D12_GetRaytracingCaps(device, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_CLUSTER_OPERATIONS, &clusterCaps, sizeof(clusterCaps));
        const NvAPI_Status topStatus=NvAPI_D3D12_GetRaytracingCaps(device, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_PARTITIONED_TLAS, &topCaps, sizeof(topCaps));
        caps.asked=clusterStatus==NVAPI_OK && topStatus==NVAPI_OK;
        caps.clusters=clusterStatus==NVAPI_OK && (clusterCaps&NVAPI_D3D12_RAYTRACING_CLUSTER_OPERATIONS_CAP_STANDARD)!=0;
        caps.partitionedTop=topStatus==NVAPI_OK && (topCaps&NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_CAP_STANDARD)!=0;
        // (megageo-5: NVAPI's own micromap arrays, the only ones its cluster calls document.)
        NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAPS ommCaps=NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_NONE;
        const NvAPI_Status ommStatus=NvAPI_D3D12_GetRaytracingCaps(device, NVAPI_D3D12_RAYTRACING_CAPS_TYPE_OPACITY_MICROMAP, &ommCaps, sizeof(ommCaps));
        caps.omm=ommStatus==NVAPI_OK && (ommCaps&NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_CAP_STANDARD)!=0;
        lines.push_back(std::string("Mega Geometry: the driver reports NVAPI's own opacity micromaps (which its clusters take): ")+(caps.omm ? "YES" : "no")+
                        (ommStatus==NVAPI_OK ? std::string() : ", the call failed: "+status_text(ommStatus)));
        {
            char b[640];
            std::snprintf(b, sizeof(b), "Mega Geometry: the driver %u.%02u (%s; NVAPI %s, header %u) reports cluster operations: %s (caps 0x%X%s%s), the partitioned top level: %s (caps 0x%X%s%s). "
                          "The header's limits: a cluster at most 256 triangles, 256 vertices and 256 submeshes, a submesh's number at most %u; a partitioned top level at most 16777216 instances and as many partitions",
                          driver/100, driver%100, branch, library, unsigned(NVAPI_SDK_VERSION),
                          caps.clusters ? "YES" : "no", unsigned(clusterCaps), clusterStatus==NVAPI_OK ? "" : ", the call failed: ", clusterStatus==NVAPI_OK ? "" : status_text(clusterStatus).c_str(),
                          caps.partitionedTop ? "YES" : "no", unsigned(topCaps), topStatus==NVAPI_OK ? "" : ", the call failed: ", topStatus==NVAPI_OK ? "" : status_text(topStatus).c_str(),
                          unsigned(NVAPI_D3D12_RAYTRACING_MAXIMUM_GEOMETRY_INDEX));
            lines.push_back(b);
        }
        // What the driver itself says the yardstick would hold (upper bounds: the driver sizes for the worst triangles).
        if(caps.clusters) {
            std::string text="Mega Geometry: the driver's own sizes for a town's plants ("+std::to_string(kYardPlants)+" plants in "+std::to_string(kYardGroups)+" groups, "+
                std::to_string(kYardTriangles)+" triangles, "+std::to_string(kYardVertices)+" vertices; today 428 MB of plain bottom levels) cut into "+std::to_string(kYardClusters)+
                " clusters of at most "+std::to_string(kYardClusterTriangles)+" triangles, upper bounds: ";
            std::string error;
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO info{};
            auto in=triangle_inputs(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLAS_FROM_TRIANGLES, kYardClusters, kYardTriangles, kYardVertices);
            text+=requirements(d5.Get(), in, info, error) ? "clusters built from triangles "+megabytes(info.resultDataMaxSizeInBytes)+" (scratch "+megabytes(info.scratchDataSizeInBytes)+")"
                                                          : "clusters built from triangles: "+error;
            // Templates: one a cluster of each distinct plant mesh (not counted yet: a twentieth of the clusters is asked
            // for), then every plant's clusters stamped from them with its posed vertices.
            info={}; in=triangle_inputs(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES, kYardClusters/20, kYardTriangles/20, kYardVertices/20);
            text+=requirements(d5.Get(), in, info, error) ? "; templates for a twentieth of them "+megabytes(info.resultDataMaxSizeInBytes)+" (scratch "+megabytes(info.scratchDataSizeInBytes)+")"
                                                          : "; templates: "+error;
            info={}; in=triangle_inputs(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES, kYardClusters, kYardTriangles, kYardVertices);
            text+=requirements(d5.Get(), in, info, error) ? "; clusters stamped from templates "+megabytes(info.resultDataMaxSizeInBytes)+" (scratch "+megabytes(info.scratchDataSizeInBytes)+")"
                                                          : "; clusters stamped from templates: "+error;
            info={};
            NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS blas{};
            blas.maxArgCount=kYardGroups; blas.type=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS;
            blas.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
            blas.mode=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_IMPLICIT_DESTINATIONS;
            blas.clasDesc.maxTotalClasCount=kYardClusters; blas.clasDesc.maxClasCountPerArg=kYardClusters/kYardGroups*8;
            text+=requirements(d5.Get(), blas, info, error) ? "; the "+std::to_string(kYardGroups)+" bottom levels built from those clusters "+megabytes(info.resultDataMaxSizeInBytes)+
                                                              " (scratch "+megabytes(info.scratchDataSizeInBytes)+")"
                                                            : "; bottom levels from clusters: "+error;
            lines.push_back(text);
        }
        if(caps.partitionedTop) {
            std::string text="Mega Geometry: the driver's own sizes for a partitioned top level (upper bounds; a town's plain one holds "+std::to_string(kYardInstances)+" instances): ";
            // Room for twice the town's instances; the partitions as a grid over the world, and the things that move in
            // the global one.
            const struct { uint32_t instances, partitions, perPartition, global; } asks[]={{32768, 256, 4096, 4096}, {32768, 1024, 2048, 4096}, {131072, 4096, 2048, 8192}};
            bool first=true;
            for(const auto& a:asks) {
                NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_INPUTS in{};
                in.flags=NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_FLAG_FAST_TRACE;
                in.instanceCount=a.instances; in.partitionCount=a.partitions; in.maxInstancePerPartitionCount=a.perPartition; in.maxInstanceInGlobalPartitionCount=a.global;
                NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO info{};
                NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS params{};
                params.version=NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS_VER; params.pInput=&in; params.pInfo=&info;
                const NvAPI_Status s=NvAPI_D3D12_GetRaytracingPartitionedTlasIndirectPrebuildInfo(d5.Get(), &params);
                text+=std::string(first ? "" : "; ")+std::to_string(a.instances)+" instances in "+std::to_string(a.partitions)+" partitions (at most "+std::to_string(a.perPartition)+
                    " in one, "+std::to_string(a.global)+" in the global one): "+(s==NVAPI_OK ? megabytes(info.resultDataMaxSizeInBytes)+", scratch "+megabytes(info.scratchDataSizeInBytes) : status_text(s));
                first=false;
            }
            // The plain top level of as many instances, by Direct3D's own prebuild question, to set beside it.
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS plain{};
            plain.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL; plain.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            plain.NumDescs=32768; plain.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO plainInfo{};
            d5->GetRaytracingAccelerationStructurePrebuildInfo(&plain, &plainInfo);
            text+="; a plain top level of 32768 instances: "+megabytes(plainInfo.ResultDataMaxSizeInBytes)+", scratch "+megabytes(plainInfo.ScratchDataSizeInBytes);
            lines.push_back(text);
        }
        if(!caps.clusters && !caps.partitionedTop) lines.push_back("Mega Geometry: this driver reports neither part; the plain acceleration structures serve");
    } catch(...) {
        lines.push_back("Mega Geometry: asking the driver failed; the plain acceleration structures serve");
        caps=MegaCaps{};
    }
    return caps;
}

// ---- The partitioned top level (mega_geometry.hpp, MegaTopLevel) ----
namespace {
// The partitions of what stands still: grid x grid cells over kTopSpan metres around the scene's origin, on the ground
// plane (x and z). What lies farther out goes into the border's cells. The origin moves in 256 m steps, a whole number
// of cells of every size the trial offers (64, 128, 256 m), so a thing keeps its cell between two steps.
constexpr float kTopSpan=2048.f;
constexpr uint32_t kTopGlobal=uint32_t(NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_PARTITION_INDEX_GLOBAL_PARTITION);
// The instance flags a partitioned top level can say, which Direct3D's own carry in the same bits.
constexpr uint32_t kTopFlags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE|
                             D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE|D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE;
static_assert(uint32_t(NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE)==uint32_t(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE) &&
              uint32_t(NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE)==uint32_t(D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE) &&
              uint32_t(NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_FORCE_OPAQUE)==uint32_t(D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) &&
              uint32_t(NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_INSTANCE_FLAG_FORCE_NON_OPAQUE)==uint32_t(D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE),
              "a partitioned top level's instance flags are Direct3D's, bit for bit");
using TopWrite=NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_WRITE_INSTANCE;
using TopUpdate=NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_ARG_UPDATE_INSTANCE;
// What the GPU reads of one build: how many operations, the operations (one of a kind at most), then their arguments.
struct TopHead { uint32_t count, pad; NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP ops[2]; };
static_assert(sizeof(TopHead)%8==0 && sizeof(TopWrite)%8==0 && sizeof(TopUpdate)%8==0, "the operations' arguments lie on 8-byte boundaries");

uint32_t top_cell(const D3D12_RAYTRACING_INSTANCE_DESC& d, uint32_t grid) {
    const float cell=kTopSpan/float(grid);
    const auto axis=[&](float v) { return uint32_t(std::clamp(std::floor(v/cell)+float(grid/2), 0.f, float(grid-1))); };
    return axis(d.Transform[0][3])+grid*axis(d.Transform[2][3]);
}
NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_INPUTS top_inputs(uint32_t capacity, uint32_t grid) {
    NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_INPUTS in{};
    in.flags=NVAPI_D3D12_RAYTRACING_PARTITIONED_TLAS_FLAG_FAST_TRACE; // as the plain top level's PREFER_FAST_TRACE
    in.instanceCount=capacity; in.partitionCount=grid*grid;
    // No limit of ours on where the instances lie: any one partition, and the global one, may hold them all.
    in.maxInstancePerPartitionCount=capacity; in.maxInstanceInGlobalPartitionCount=capacity;
    return in;
}
} // namespace

void MegaTopLevel::release(DeferredRelease& release, uint64_t retireValue) {
    for(Buffer* b:{&tlas_, &scratch_}) { if(*b) release.add(retireValue, b->resource.Get()); *b=Buffer{}; }
    slots_.clear(); capacity_=0; grid_=0; built_=false;
}

void MegaTopLevel::fail(const std::string& what, DeferredRelease& release, uint64_t retireValue, std::vector<std::string>& lines) {
    lines.push_back("Mega Geometry: the partitioned top level failed ("+what+"); the plain top level serves from now on");
    this->release(release, retireValue);
    failed_=true;
}

D3D12_GPU_VIRTUAL_ADDRESS MegaTopLevel::build(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, UploadRing& ring, DeferredRelease& release, uint64_t retireValue,
                                              const D3D12_RAYTRACING_INSTANCE_DESC* descs, size_t count, size_t still, const std::vector<D3D12_GPU_VIRTUAL_ADDRESS>& restless,
                                              const std::vector<D3D12_GPU_VIRTUAL_ADDRESS>& fresh, uint32_t grid, Barriers& barriers, Frame& frame, std::vector<std::string>& lines) {
    frame=Frame{};
    if(failed_ || !device || !list || !descs || !count || grid<2 || grid>64) return 0;
    try {
        // An instance flag it cannot say (mega_geometry.hpp): the plain build serves this frame, and what the structure
        // holds stays as it is for the next.
        for(size_t i=0;i<count;++i) if(descs[i].Flags&~kTopFlags) {
            if(!saidFlags_) {
                char b[224]; std::snprintf(b, sizeof(b), "Mega Geometry: an instance carries flags 0x%X, which a partitioned top level cannot say; the plain top level serves such frames", unsigned(descs[i].Flags));
                lines.push_back(b); saidFlags_=true;
            }
            return 0;
        }
        if(count>(1u<<24)) { fail("more than 16777216 instances", release, retireValue, lines); return 0; }
        // Its room and its partitions: fixed while it lives. The room is half as much again as the scene asks for; grown,
        // or asked for with cells of another size, it is made anew and written whole once.
        if(!tlas_ || count>capacity_ || grid!=grid_) {
            const uint32_t capacity=uint32_t(std::min<size_t>(1u<<24, std::max<size_t>(4096, (count*3/2+1023)/1024*1024)));
            const auto in=top_inputs(capacity, grid);
            NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO info{};
            NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS params{};
            params.version=NVAPI_GET_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PREBUILD_INFO_PARAMS_VER; params.pInput=&in; params.pInfo=&info;
            const NvAPI_Status sized=NvAPI_D3D12_GetRaytracingPartitionedTlasIndirectPrebuildInfo(device, &params);
            if(sized!=NVAPI_OK || !info.resultDataMaxSizeInBytes) { fail("its size: "+status_text(sized), release, retireValue, lines); return 0; }
            this->release(release, retireValue);
            tlas_=make_buffer(device, info.resultDataMaxSizeInBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                              D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create the partitioned top level");
            scratch_=make_buffer(device, std::max<uint64_t>(info.scratchDataSizeInBytes, 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the partitioned top level's scratch");
            capacity_=capacity; grid_=grid;
            char b[320];
            std::snprintf(b, sizeof(b), "Mega Geometry: the top level is partitioned (on trial): %zu instances, room for %u in %u partitions of %.0f m and a global one for what moves: %s, scratch %s",
                          count, capacity, grid*grid, double(kTopSpan/float(grid)), megabytes(info.resultDataMaxSizeInBytes).c_str(), megabytes(info.scratchDataSizeInBytes).c_str());
            lines.push_back(b);
        }
        // Slot by slot: written whole where its instance is another than the structure holds, told to take its bottom
        // level's new bounds where it is one of those posed, refitted or moved each frame, left alone otherwise.
        std::vector<TopWrite> writes; std::vector<TopUpdate> updates;
        if(slots_.size()<count) slots_.resize(count);
        for(size_t i=0;i<count;++i) {
            const D3D12_RAYTRACING_INSTANCE_DESC& d=descs[i];
            const bool moves=i>=still || std::binary_search(restless.begin(), restless.end(), d.AccelerationStructure);
            const uint32_t partition=moves ? kTopGlobal : top_cell(d, grid_);
            Slot& s=slots_[i];
            // (A bottom level built anew at the address the slot already names: another structure, other bounds.)
            const bool rebuilt=!fresh.empty() && std::binary_search(fresh.begin(), fresh.end(), d.AccelerationStructure);
            if(!built_ || !s.live || rebuilt || s.partition!=partition || std::memcmp(&s.desc, &d, sizeof(d))!=0) {
                TopWrite w{};
                std::memcpy(w.transform, d.Transform, sizeof(w.transform));
                w.instanceID=d.InstanceID; w.instanceMask=d.InstanceMask; w.instanceContributionToHitGroupIndex=d.InstanceContributionToHitGroupIndex;
                w.instanceFlags=d.Flags; w.instanceIndex=uint32_t(i); w.partitionIndex=partition; w.accelerationStructure=d.AccelerationStructure;
                writes.push_back(w);
                s.desc=d; s.partition=partition; s.live=1;
            } else if(moves) updates.push_back(TopUpdate{uint32_t(i), d.InstanceContributionToHitGroupIndex, d.AccelerationStructure});
        }
        // A slot the frame no longer fills: its instance out of the structure (no bottom level: it meets no ray).
        for(size_t i=count;i<slots_.size();++i) if(slots_[i].live) {
            TopWrite w{}; w.instanceIndex=uint32_t(i); w.partitionIndex=slots_[i].partition;
            writes.push_back(w);
            slots_[i].live=0; slots_[i].desc=D3D12_RAYTRACING_INSTANCE_DESC{};
        }
        frame.instances=uint32_t(count); frame.written=uint32_t(writes.size()); frame.updated=uint32_t(updates.size()); frame.bytes=tlas_.size; frame.whole=!built_;
        // Nothing changed: the structure of the frame before is this frame's, untouched.
        if(writes.empty() && updates.empty()) return tlas_.va();
        const uint64_t bytes=sizeof(TopHead)+writes.size()*sizeof(TopWrite)+updates.size()*sizeof(TopUpdate);
        void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0;
        if(!ring.allocate(bytes, 16, &cpu, &gpu)) {
            Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create the partitioned top level's operations");
            cpu=upload.mapped; gpu=upload.va();
            release.add(retireValue, upload.resource.Get());
        }
        TopHead head{};
        uint64_t at=sizeof(TopHead);
        if(!writes.empty()) {
            auto& op=head.ops[head.count++];
            op.type=NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_WRITE_INSTANCE; op.count=NvU32(writes.size());
            op.data.StartAddress=gpu+at; op.data.StrideInBytes=sizeof(TopWrite);
            std::memcpy(static_cast<uint8_t*>(cpu)+at, writes.data(), writes.size()*sizeof(TopWrite)); at+=writes.size()*sizeof(TopWrite);
        }
        if(!updates.empty()) {
            auto& op=head.ops[head.count++];
            op.type=NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_OP_UPDATE_INSTANCE; op.count=NvU32(updates.size());
            op.data.StartAddress=gpu+at; op.data.StrideInBytes=sizeof(TopUpdate);
            std::memcpy(static_cast<uint8_t*>(cpu)+at, updates.data(), updates.size()*sizeof(TopUpdate)); at+=updates.size()*sizeof(TopUpdate);
        }
        std::memcpy(cpu, &head, sizeof(head));
        NVAPI_D3D12_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_DESC desc{};
        desc.inputs=top_inputs(capacity_, grid_);
        // In place, as the plain top level is built in place: the frames before have traced theirs by the time the GPU
        // reaches this (one queue, in order).
        desc.srcAccelerationStructureData=built_ ? tlas_.va() : 0; desc.destAccelerationStructureData=tlas_.va();
        desc.scratchAccelerationStructureData=scratch_.va();
        desc.indirectOpCount=gpu; desc.indirectOps=gpu+offsetof(TopHead, ops);
        NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS params{};
        params.version=NVAPI_BUILD_RAYTRACING_PARTITIONED_TLAS_INDIRECT_PARAMS_VER; params.pDesc=&desc;
        barriers.flush(list);
        const NvAPI_Status built=NvAPI_D3D12_BuildRaytracingPartitionedTlasIndirect(list, &params);
        if(built!=NVAPI_OK) { fail("its build: "+status_text(built), release, retireValue, lines); return 0; }
        barriers.uav(tlas_.resource.Get()); barriers.flush(list);
        built_=true;
        return tlas_.va();
    } catch(const std::exception& e) {
        fail(e.what(), release, retireValue, lines);
        return 0;
    } catch(...) {
        fail("an exception", release, retireValue, lines);
        return 0;
    }
}

// ---- The plants as clusters (mega_geometry.hpp, MegaMeshClusters, MegaMicromaps and MegaClusterBuilder) ----
namespace {
std::atomic<bool> clusterPipelines{false}, clusterMicromaps{false};
using ClusterInputs=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INPUTS;
using ClusterDesc=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_DESC;
using TemplateArgs=NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS;
using StampArgs=NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS;
using BottomArgs=NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS;
constexpr auto kFastTrace=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_FAST_TRACE;
constexpr auto kExplicit=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_MODE_EXPLICIT_DESTINATIONS;
uint64_t aligned(uint64_t v, uint64_t a) { return (v+a-1)/a*a; }
// The triangles' inputs every template and stamp call names alike (NVIDIA's: 32-bit float positions, one submesh a cluster,
// every bit of a position kept: the same triangles).
// `omm`: FLAG_ALLOW_OMM, on every call that builds or reads an object with micromaps (nvapi.h: "All Operation Types & Modes
// require this field to be set correctly when interacting (building or consuming) objects that contain OMMs").
ClusterInputs triangles_call(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE type, uint32_t args, uint64_t triangles, uint64_t vertices, bool omm) {
    ClusterInputs in{};
    in.maxArgCount=args; in.type=type; in.mode=kExplicit;
    in.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS(kFastTrace|(omm ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE));
    in.trianglesDesc.vertexFormat=uint32_t(DXGI_FORMAT_R32G32B32_FLOAT);
    in.trianglesDesc.maxGeometryIndexValue=kMegaGeometryMost;
    in.trianglesDesc.maxUniqueGeometryCountPerArg=1;
    in.trianglesDesc.maxTriangleCountPerArg=kMegaClusterTriangles; in.trianglesDesc.maxVertexCountPerArg=kMegaClusterVertices;
    // (The totals never below one cluster's most: the driver refuses a call whose total is less than its per-cluster most.)
    in.trianglesDesc.maxTotalTriangleCount=uint32_t(std::clamp<uint64_t>(triangles, kMegaClusterTriangles, UINT32_MAX));
    in.trianglesDesc.maxTotalVertexCount=uint32_t(std::clamp<uint64_t>(vertices, kMegaClusterVertices, UINT32_MAX));
    in.trianglesDesc.minPositionTruncateBitCount=0;
    return in;
}
ClusterInputs bottoms_call(uint32_t args, uint32_t total, uint32_t most, bool omm) {
    ClusterInputs in{};
    in.maxArgCount=args; in.type=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS; in.mode=kExplicit;
    in.flags=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAGS(kFastTrace|(omm ? NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_ALLOW_OMM : NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_FLAG_NONE));
    in.clasDesc.maxTotalClasCount=std::max({total, most, 1u}); in.clasDesc.maxClasCountPerArg=std::max(most, 1u);
    return in;
}
NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO need(ID3D12Device5* device, const ClusterInputs& in, const char* what) {
    NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_REQUIREMENTS_INFO info{};
    std::string error;
    if(!requirements(device, in, info, error)) {
        char b[200];
        if(in.type==NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_BLAS_FROM_CLAS)
            std::snprintf(b, sizeof b, " (%u bottom levels, %u clusters in all, at most %u in one)", in.maxArgCount, in.clasDesc.maxTotalClasCount, in.clasDesc.maxClasCountPerArg);
        else
            std::snprintf(b, sizeof b, " (type %d, mode %d, %u clusters, %u triangles and %u vertices in all, at most %u and %u in one, submesh numbers to %u)", int(in.type), int(in.mode),
                          in.maxArgCount, in.trianglesDesc.maxTotalTriangleCount, in.trianglesDesc.maxTotalVertexCount, in.trianglesDesc.maxTriangleCountPerArg,
                          in.trianglesDesc.maxVertexCountPerArg, in.trianglesDesc.maxGeometryIndexValue);
        throw std::runtime_error(std::string(what)+": "+error+b);
    }
    return info;
}
void execute(ID3D12GraphicsCommandList4* list, const ClusterDesc& d, const char* what) {
    NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS p{};
    p.version=NVAPI_RAYTRACING_EXECUTE_MULTI_INDIRECT_CLUSTER_OPERATION_PARAMS_VER; p.pDesc=&d;
    const NvAPI_Status s=NvAPI_D3D12_RaytracingExecuteMultiIndirectClusterOperation(list, &p);
    if(s!=NVAPI_OK) throw std::runtime_error(std::string(what)+": "+status_text(s));
}
// Bytes for the GPU, in an upload buffer of the frame's ring or (when it is full) one of their own.
D3D12_GPU_VIRTUAL_ADDRESS upload_bytes(ID3D12Device* device, UploadRing* ring, DeferredRelease& release, uint64_t retireValue, const void* data, uint64_t bytes, const char* what) {
    void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0;
    if(!ring || !ring->allocate(bytes, 256, &cpu, &gpu)) {
        Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, what);
        cpu=upload.mapped; gpu=upload.va();
        release.add(retireValue, upload.resource.Get());
    }
    std::memcpy(cpu, data, size_t(bytes));
    return gpu;
}
} // namespace

void mega_enable_cluster_pipelines(ID3D12Device* device, const MegaCaps& caps, std::vector<std::string>& lines) {
    if(!caps.clusters) return;
    ComPtr<ID3D12Device5> d5;
    if(!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&d5)))) return;
    // (megageo-5: and NVAPI's own micromaps where the driver reports them, which the clusters then carry. NVAPI: "Support should
    // only be enabled for the specific features that are present": both are, in a build with the trial compiled in.)
    NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS p{};
    p.version=NVAPI_D3D12_SET_CREATE_PIPELINE_STATE_OPTIONS_PARAMS_VER;
    p.flags=NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_CLUSTER_SUPPORT|(caps.omm ? NVAPI_D3D12_PIPELINE_CREATION_STATE_FLAGS_ENABLE_OMM_SUPPORT : 0u);
    const NvAPI_Status s=NvAPI_D3D12_SetCreatePipelineStateOptions(d5.Get(), &p);
    clusterPipelines=s==NVAPI_OK; clusterMicromaps=s==NVAPI_OK && caps.omm;
    lines.push_back(s==NVAPI_OK ? std::string("Mega Geometry: the tracer's pipelines are made with the driver's support for structures built from clusters")+
                                  (caps.omm ? " and for NVAPI's opacity micromaps (which the clusters carry)" : " (not for NVAPI's micromaps: the driver does not report them, so clusters carry none)")+
                                  " (the trial's 5, 7 and 8 need it; it may cost a little on every trace, so a build with the trial compiled in is compared with itself)"
                                : "Mega Geometry: the driver refused the pipelines' support for clusters ("+status_text(s)+"); the trial's clusters stay off");
}
bool mega_cluster_pipelines() { return clusterPipelines; }
bool mega_cluster_micromaps() { return clusterMicromaps; }

bool mega_cut(const float* positions, uint32_t vertexCount, const uint32_t* indices, size_t indexCount, const std::vector<MegaSubmesh>& submeshes,
              MegaMeshClusters& out, std::string& error) {
    try {
        out=MegaMeshClusters{};
        if(!positions || !indices || !vertexCount || !indexCount) { error="the mesh has no positions or indices"; return false; }
        for(size_t i=0;i<indexCount;++i) if(indices[i]>=vertexCount) { error="an index names no vertex"; return false; }
        out.indices.assign(indices, indices+indexCount);
        out.source.assign(indexCount/3, 0u);
        // NVIDIA's settings for clusters that are traced (meshoptimizer's own preset for ray tracing, which its samples
        // take): the most triangles and vertices, at least a quarter of the triangles (a multiple of 4), fill 0.5.
        const size_t maxTris=kMegaClusterTriangles, maxVerts=kMegaClusterVertices, minTris=std::max<size_t>((maxTris/4)&~size_t(3), 1);
        double diagonals=0;
        // A triangle by its corners in their turn (the smallest first, the order around kept): meshoptimizer may hand a
        // triangle back turned (another corner first), never mirrored. Each triangle of the cut is matched to its own in
        // the submesh: its corners are put back in the mesh's own order (a micromap hangs on that order: the bake's
        // barycentrics are the mesh's), and its number in the submesh kept (the bake's indices are in that order).
        auto turned=[](uint32_t a, uint32_t b, uint32_t c) {
            if(a<=b && a<=c) return std::array<uint32_t, 3>{a, b, c};
            if(b<=a && b<=c) return std::array<uint32_t, 3>{b, c, a};
            return std::array<uint32_t, 3>{c, a, b};
        };
        for(uint32_t s=0;s<uint32_t(submeshes.size());++s) {
            out.firstCluster.push_back(uint32_t(out.clusters.size()));
            const MegaSubmesh& sub=submeshes[s];
            const size_t n=sub.indexCount-sub.indexCount%3;
            if(!n) continue;
            if(uint64_t(sub.indexStart)+n>indexCount) { error="a submesh's indices lie outside the mesh's"; return false; }
            if(sub.indexStart%3) { error="a submesh does not start on a whole triangle"; return false; }
            const uint32_t* idx=indices+sub.indexStart;
            std::vector<meshopt_Meshlet> meshlets(meshopt_buildMeshletsBound(n, maxVerts, minTris));
            std::vector<unsigned int> vertices(n); std::vector<unsigned char> local(n);
            const size_t count=meshopt_buildMeshletsSpatial(meshlets.data(), vertices.data(), local.data(), idx, n, positions, vertexCount, 12, maxVerts, minTris, maxTris, 0.5f);
            // The submesh's own triangles by their corners (several where the mesh repeats one: each is taken once).
            std::map<std::array<uint32_t, 3>, std::vector<uint32_t>> own;
            for(uint32_t t=0;t<uint32_t(n/3);++t) own[turned(idx[t*3], idx[t*3+1], idx[t*3+2])].push_back(t);
            // Its closed pieces are its first `solid` triangles (scene.cpp mark_closed): they go first in their cluster, so
            // that the cluster's record can say how many of its first triangles are closed (as a submesh's does).
            const uint32_t solidTris=std::min<uint32_t>(sub.solid, uint32_t(n/3));
            uint32_t at=sub.indexStart;
            for(size_t i=0;i<count;++i) {
                const meshopt_Meshlet& k=meshlets[i];
                meshopt_optimizeMeshlet(&vertices[k.vertex_offset], &local[k.triangle_offset], k.triangle_count, k.vertex_count);
                MegaMeshClusters::Cluster c; c.submesh=s; c.firstIndex=at; c.triangles=k.triangle_count; c.vertices=k.vertex_count;
                std::vector<uint32_t> first, rest; // (each: its number in the submesh)
                float lo[3]={1e30f, 1e30f, 1e30f}, hi[3]={-1e30f, -1e30f, -1e30f};
                for(uint32_t t=0;t<k.triangle_count;++t) {
                    const unsigned char* tri=&local[k.triangle_offset+t*3];
                    const uint32_t a=vertices[k.vertex_offset+tri[0]], b=vertices[k.vertex_offset+tri[1]], d=vertices[k.vertex_offset+tri[2]];
                    auto found=own.find(turned(a, b, d));
                    if(found==own.end() || found->second.empty()) { error="the cut made a triangle the submesh does not have"; return false; }
                    const uint32_t number=found->second.back(); found->second.pop_back();
                    (number<solidTris ? first : rest).push_back(number);
                    for(uint32_t v:{a, b, d}) for(int x=0;x<3;++x) { lo[x]=std::min(lo[x], positions[size_t(v)*3+x]); hi[x]=std::max(hi[x], positions[size_t(v)*3+x]); }
                }
                c.solid=uint32_t(first.size());
                uint32_t put=at;
                for(const auto* part:{&first, &rest}) for(uint32_t number:*part) {
                    for(int v=0;v<3;++v) out.indices[put+v]=idx[number*3+v];
                    out.source[put/3]=number;
                    put+=3;
                }
                diagonals+=std::sqrt(double(hi[0]-lo[0])*(hi[0]-lo[0])+double(hi[1]-lo[1])*(hi[1]-lo[1])+double(hi[2]-lo[2])*(hi[2]-lo[2]));
                at+=k.triangle_count*3;
                out.triangles+=k.triangle_count;
                out.clusters.push_back(c);
            }
            if(at!=sub.indexStart+n) { error="the cut lost triangles"; return false; }
        }
        out.firstCluster.push_back(uint32_t(out.clusters.size()));
        if(out.clusters.empty()) { error="the mesh has no triangles"; return false; }
        if(out.clusters.size()>kMegaGeometryMost+1ull) { error="more clusters than a group's records can name"; return false; }
        out.boxDiagonal=diagonals/double(out.clusters.size());
        return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}

bool mega_build_micromaps(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DeferredRelease& release, uint64_t retireValue, D3D12_GPU_VIRTUAL_ADDRESS input,
                          uint32_t count, uint32_t level, uint32_t bytes, uint32_t serial, MegaMicromaps& out, Barriers& barriers, std::string& error) {
    try {
        if(out.array) release.add(retireValue, out.array.resource.Get());
        out=MegaMicromaps{};
        if(!device || !list || !input || !count || !bytes) { error="no micromaps to build"; return false; }
        if(uint64_t(count)*bytes>UINT32_MAX) { error="more micromap bytes than NVAPI's descriptors can reach"; return false; }
        // One descriptor a micromap, as the DirectX Raytracing array's: the bake's micromaps one after another, 4-state, its
        // level; the same build preference (PREFER_FAST_TRACE).
        std::vector<NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC> descs(count);
        for(uint32_t i=0;i<count;++i) {
            descs[i].byteOffset=i*bytes; descs[i].subdivisionLevel=NvU16(level);
            descs[i].format=NvU16(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE);
        }
        const D3D12_GPU_VIRTUAL_ADDRESS descVa=upload_bytes(device, nullptr, release, retireValue, descs.data(), descs.size()*sizeof(descs[0]), "Upload a mesh's NVAPI micromap descriptors");
        NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_USAGE_COUNT usage{};
        usage.count=count; usage.subdivisionLevel=level; usage.format=NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE;
        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_INPUTS in{};
        in.flags=NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BUILD_FLAG_PREFER_FAST_TRACE;
        in.numOMMUsageCounts=1; in.pOMMUsageCounts=&usage;
        in.inputBuffer=input; in.perOMMDescs.StartAddress=descVa; in.perOMMDescs.StrideInBytes=sizeof(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);
        NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO info{};
        NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS sizeParams{};
        sizeParams.version=NVAPI_GET_RAYTRACING_OPACITY_MICROMAP_ARRAY_PREBUILD_INFO_PARAMS_VER; sizeParams.pDesc=&in; sizeParams.pInfo=&info;
        const NvAPI_Status sized=NvAPI_D3D12_GetRaytracingOpacityMicromapArrayPrebuildInfo(device, &sizeParams);
        if(sized!=NVAPI_OK || !info.resultDataMaxSizeInBytes) { error="its size: "+status_text(sized); return false; }
        // (Both 256-byte aligned: nvapi.h's NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_BYTE_ALIGNMENT; a buffer of its own
        // starts on 64 KB.)
        Buffer array=make_buffer(device, aligned(info.resultDataMaxSizeInBytes, 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create a mesh's NVAPI micromap array");
        Buffer scratch=make_buffer(device, aligned(std::max<uint64_t>(info.scratchDataSizeInBytes, 256), 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create a mesh's NVAPI micromap scratch");
        NVAPI_D3D12_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC desc{};
        desc.destOpacityMicromapArrayData=array.va(); desc.inputs=in; desc.scratchOpacityMicromapArrayData=scratch.va();
        NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS build{};
        build.version=NVAPI_BUILD_RAYTRACING_OPACITY_MICROMAP_ARRAY_PARAMS_VER; build.pDesc=&desc; build.numPostbuildInfoDescs=0; build.pPostbuildInfoDescs=nullptr;
        barriers.flush(list);
        const NvAPI_Status built=NvAPI_D3D12_BuildRaytracingOpacityMicromapArray(list, &build);
        release.add(retireValue, scratch.resource.Get());
        if(built!=NVAPI_OK) { release.add(retireValue, array.resource.Get()); error="its build: "+status_text(built); return false; }
        barriers.uav(array.resource.Get()); barriers.flush(list);
        out.array=std::move(array); out.serial=serial; out.count=count;
        return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}

void mega_release_maps(MegaMeshClusters& mesh, uint32_t serial, std::vector<Buffer>& gone) {
    if(!serial) return;
    for(auto it=mesh.templates.begin();it!=mesh.templates.end();) {
        if(it->first.serial!=serial) { ++it; continue; }
        if(it->second.buffer) gone.push_back(std::move(it->second.buffer));
        it=mesh.templates.erase(it);
    }
    auto order=mesh.ommOrders.find(serial);
    if(order!=mesh.ommOrders.end()) { if(order->second) gone.push_back(std::move(order->second)); mesh.ommOrders.erase(order); }
}

void mega_release(MegaMeshClusters& mesh, DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    if(heap) heap->release(mesh.orderSrv);
    mesh.orderSrv=kInvalid;
    if(mesh.order) release.add(retireValue, mesh.order.resource.Get());
    mesh.order=Buffer{};
    for(auto& [key, t]:mesh.templates) if(t.buffer) release.add(retireValue, t.buffer.resource.Get());
    mesh.templates.clear();
    for(auto& [serial, b]:mesh.ommOrders) if(b) release.add(retireValue, b.resource.Get());
    mesh.ommOrders.clear();
}

namespace {
// 32-bit values into a buffer of their own on the GPU, readable by the cluster calls (NON_PIXEL_SHADER_RESOURCE), recorded on
// the list.
Buffer upload_words(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DeferredRelease& release, uint64_t retireValue, const void* data, uint64_t bytes,
                    Barriers& barriers, const char* what) {
    Buffer into=make_buffer(device, std::max<uint64_t>(bytes, 4), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, what);
    Buffer upload=make_buffer(device, std::max<uint64_t>(bytes, 4), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, what);
    std::memcpy(upload.mapped, data, size_t(bytes));
    barriers.transition(into, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
    list->CopyBufferRegion(into.resource.Get(), 0, upload.resource.Get(), 0, bytes);
    barriers.transition(into, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.flush(list);
    release.add(retireValue, upload.resource.Get());
    return into;
}
} // namespace

bool mega_upload_order(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue,
                       MegaMeshClusters& mesh, Barriers& barriers, std::string& error) {
    try {
        if(mesh.order) return true;
        if(mesh.indices.empty()) { error="the cut's order is gone"; return false; }
        // Once: the templates' index buffer and the one pt_sway reads (a raw view of 32-bit indices).
        const uint64_t bytes=mesh.indices.size()*4;
        Buffer order=upload_words(device, list, release, retireValue, mesh.indices.data(), bytes, barriers, "Create a mesh's clusters' order");
        mesh.orderSrv=raw_srv(device, heap, order.resource.Get(), bytes);
        mesh.order=std::move(order);
        return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}

bool mega_templates(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release, uint64_t retireValue,
                    MegaMeshClusters& mesh, const MegaTemplateKey& key, D3D12_GPU_VIRTUAL_ADDRESS positions, uint32_t vertexCount,
                    const MegaMicromaps* maps, const std::vector<uint32_t>* ommFirst, Barriers& barriers, std::string& error) {
    try {
        if(!mega_upload_order(device, list, heap, release, retireValue, mesh, barriers, error)) return false;
        if(mesh.templates.count(key)) return true;
        const uint32_t count=uint32_t(mesh.clusters.size());
        // Micromaps to link: NVAPI's array of the mesh's micromaps (that build of them), and the bake's indices in the cut's
        // order (once a build: each cluster's part of it is that cluster's index buffer). A triangle of a submesh without
        // micromaps takes NVAPI's "no micromap" index (CLUSTER_SKIP_OMM); no template of such a submesh names the buffer.
        D3D12_GPU_VIRTUAL_ADDRESS ommOrder=0;
        if(key.linked) {
            if(!key.omm || !maps || !maps->ready() || maps->serial!=key.serial || !ommFirst || ommFirst->size()+1!=mesh.firstCluster.size()) {
                error="the micromaps it should link are not there"; return false;
            }
            auto found=mesh.ommOrders.find(key.serial);
            if(found==mesh.ommOrders.end()) {
                std::vector<int32_t> order(mesh.source.size(), int32_t(NVAPI_D3D12_RAYTRACING_OPACITY_MICROMAP_SPECIAL_INDEX_CLUSTER_SKIP_OMM));
                for(const auto& c:mesh.clusters) {
                    const uint32_t first=c.submesh<ommFirst->size() ? (*ommFirst)[c.submesh] : kInvalid;
                    if(first==kInvalid) continue;
                    for(uint32_t j=0;j<c.triangles;++j) {
                        const size_t at=c.firstIndex/3+j;
                        const uint64_t index=uint64_t(first)+mesh.source[at];
                        if(at>=order.size() || index>=maps->indices.size()) { error="a cluster's triangle has no micromap index"; return false; }
                        order[at]=maps->indices[size_t(index)];
                    }
                }
                found=mesh.ommOrders.emplace(key.serial, upload_words(device, list, release, retireValue, order.data(), order.size()*4, barriers,
                                                                      "Create a mesh's clusters' micromap indices")).first;
            }
            ommOrder=found->second.va();
        }
        uint64_t triangles=0, vertices=0;
        for(const auto& c:mesh.clusters) { triangles+=c.triangles; vertices+=c.vertices; }
        const ClusterInputs in=triangles_call(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_BUILD_CLUSTER_TEMPLATES_FROM_TRIANGLES, count, triangles, vertices, key.omm);
        const auto info=need(device, in, "the templates' size");
        // Each template in a room of the driver's own size for one (explicit destinations: one a cluster).
        const uint64_t room=aligned(std::max<uint64_t>(info.resultDataMaxSizeInBytes, 1), NVAPI_D3D12_RAYTRACING_CLUSTER_TEMPLATE_BYTE_ALIGNMENT);
        MegaMeshClusters::Templates t;
        t.buffer=make_buffer(device, room*count, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                             "Create a mesh's cluster templates");
        std::vector<TemplateArgs> args(count);
        std::vector<D3D12_GPU_VIRTUAL_ADDRESS> at(count);
        for(uint32_t c=0;c<count;++c) {
            const auto& k=mesh.clusters[c];
            TemplateArgs& a=args[c]; a={};
            a.clusterId=c; a.clusterFlags=0;
            a.triangleCount=k.triangles; a.vertexCount=k.vertices; a.positionTruncateBitCount=0;
            a.indexFormat=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_32BIT; a.opacityMicromapIndexFormat=0;
            // Its own record number in the mesh (a stamp adds its member's first), and the OPAQUE flag where its submesh is
            // solid for this pattern (as a plain group's geometry says it).
            const bool solid=k.submesh<64 && ((key.opaque>>k.submesh)&1ull);
            a.baseGeometryIndexAndFlags=c|(solid ? uint32_t(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_GEOMETRY_FLAG_OPAQUE) : 0u);
            a.indexBufferStride=0; a.vertexBufferStride=12; a.geometryIndexAndFlagsBufferStride=0; a.opacityMicromapIndexBufferStride=0;
            a.indexBuffer=mesh.order.va()+uint64_t(k.firstIndex)*4;
            a.vertexBuffer=positions && vertexCount ? positions : 0; // (only shapes the template; its vertices are not kept)
            a.geometryIndexAndFlagsBuffer=0; a.opacityMicromapArray=0; a.opacityMicromapIndexBuffer=0; a.instantiationBoundingBoxLimit=0;
            // A cut-out's cluster with its micromaps: NVAPI's array and its triangles' indices into it (32-bit, in its order).
            if(ommOrder && k.submesh<64 && ((key.linked>>k.submesh)&1ull)) {
                a.opacityMicromapArray=maps->array.va();
                a.opacityMicromapIndexBuffer=ommOrder+uint64_t(k.firstIndex/3)*4;
                a.opacityMicromapIndexFormat=NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_INDEX_FORMAT_32BIT;
            }
            at[c]=t.buffer.va()+room*c;
        }
        const D3D12_GPU_VIRTUAL_ADDRESS argsVa=upload_bytes(device, nullptr, release, retireValue, args.data(), args.size()*sizeof(TemplateArgs), "Upload a mesh's template arguments");
        Buffer dest=make_buffer(device, uint64_t(count)*8, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "Create a mesh's template addresses");
        {
            Buffer upload=make_buffer(device, uint64_t(count)*8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Upload a mesh's template addresses");
            std::memcpy(upload.mapped, at.data(), at.size()*8);
            barriers.transition(dest, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
            list->CopyBufferRegion(dest.resource.Get(), 0, upload.resource.Get(), 0, uint64_t(count)*8);
            barriers.transition(dest, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); barriers.flush(list);
            release.add(retireValue, upload.resource.Get());
        }
        Buffer sizes=make_buffer(device, uint64_t(count)*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "Create a mesh's template sizes");
        barriers.transition(sizes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Buffer scratch=make_buffer(device, std::max<uint64_t>(info.scratchDataSizeInBytes, 256), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                   D3D12_RESOURCE_STATE_COMMON, "Create a mesh's template scratch");
        barriers.transition(scratch, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); barriers.flush(list);
        ClusterDesc d{};
        d.inputs=in; d.batchResultData=0; d.batchScratchData=scratch.va();
        d.destinationAddressArray={dest.va(), 8}; d.resultSizeArray={sizes.va(), 4};
        d.indirectArgArray={argsVa, sizeof(TemplateArgs)}; d.indirectArgCount=0;
        execute(list, d, "build a mesh's cluster templates");
        barriers.uav(nullptr); barriers.flush(list);
        for(Buffer* b:{&dest, &sizes, &scratch}) release.add(retireValue, b->resource.Get());
        t.at=std::move(at);
        mesh.templates.emplace(key, std::move(t));
        return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}

uint64_t MegaClusterBuilder::stamp_bytes(ID3D12Device5* device, bool omm) {
    uint64_t& bytes=stampBytes_[omm ? 1 : 0];
    if(!bytes) {
        // One stamp of any cluster the cut makes, as the frame's call names it (explicit destinations: the size of one).
        const auto info=need(device, triangles_call(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES, 1, kMegaClusterTriangles,
                                                    kMegaClusterVertices, omm), "a stamp's size");
        bytes=aligned(std::max<uint64_t>(info.resultDataMaxSizeInBytes, 1), NVAPI_D3D12_RAYTRACING_CLAS_BYTE_ALIGNMENT);
    }
    return bytes;
}
uint64_t MegaClusterBuilder::bottom_bytes(ID3D12Device5* device, uint32_t clusters, bool omm) {
    // (megageo-6: the room of the bottom level's size class, the driver's answer for a call that says the class as its most.)
    const uint32_t cls=mega_bottom_class(clusters);
    auto found=bottomBytes_.find({cls, omm});
    if(found!=bottomBytes_.end()) return found->second;
    const auto info=need(device, bottoms_call(1, cls, cls, omm), "a bottom level's size");
    const uint64_t bytes=aligned(std::max<uint64_t>(info.resultDataMaxSizeInBytes, 1), D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    bottomBytes_.emplace(std::make_pair(cls, omm), bytes);
    return bytes;
}
void MegaClusterBuilder::release(DeferredRelease& release, uint64_t retireValue) {
    for(Buffer* b:{&scratch_, &dest_, &sizes_, &args_}) { if(*b) release.add(retireValue, b->resource.Get()); *b=Buffer{}; }
    for(auto& c:checks_) if(c.readback) release.add(retireValue, c.readback.resource.Get());
    checks_.clear(); parts_.clear(); classes_.clear(); rooms_.clear(); scratchBytes_=0;
}
bool MegaClusterBuilder::build(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, UploadRing& ring, DeferredRelease& release, uint64_t retireValue,
                               const std::vector<Part>& parts, bool omm, Barriers& barriers, std::string& error) {
    rewrote=false; bottomCalls=0;
    if(parts.empty()) return true;
    try {
        ++frames_;
        const uint64_t stampRoom=stamp_bytes(device, omm);
        // The same parts in the same order as the frame before (a group's stamps and rooms never change
        // while it lives): the arguments and destinations on the GPU are this frame's too. Else they are written anew, from
        // the parts straight into the frame's upload memory, and copied into args_ and dest_.
        const bool same=omm==omm_ && args_ && dest_ && parts.size()==parts_.size() &&
            std::equal(parts.begin(), parts.end(), parts_.begin(), [](const Part& a, const Part& b) {
                return a.serial && a.serial==b.serial && a.stamps==b.stamps && a.stampCount==b.stampCount &&
                    a.bottoms==b.bottoms && a.bottomCount==b.bottomCount && a.blas==b.blas && a.clas==b.clas &&
                    a.blasBytes==b.blasBytes && a.clasBytes==b.clasBytes;
            });
        if(!same) {
            parts_.clear();
            uint64_t stampTotal=0, bottomTotal=0, triangles=0, vertices=0;
            std::map<uint32_t, Class> byClass;
            for(const auto& p:parts) {
                if(!p.stamps || !p.bottoms || !p.stampCount || !p.bottomCount || !p.blas || !p.clas)
                    throw std::runtime_error("a group's cluster inputs are empty");
                stampTotal+=p.stampCount; bottomTotal+=p.bottomCount;
                if(stampTotal>UINT32_MAX || bottomTotal>UINT32_MAX || stampTotal+bottomTotal>UINT32_MAX)
                    throw std::runtime_error("the frame's cluster argument counts exceed the API's range");
                for(uint32_t i=0;i<p.stampCount;++i) {
                    const auto& s=p.stamps[i]; triangles+=s.triangles; vertices+=s.vertexCount;
                    if(!s.templ || !s.vertices || !s.triangles || s.triangles>kMegaClusterTriangles || !s.vertexCount || s.vertexCount>kMegaClusterVertices ||
                       s.dest<p.clas || s.dest-p.clas>p.clasBytes || stampRoom>p.clasBytes-(s.dest-p.clas) || s.dest%NVAPI_D3D12_RAYTRACING_CLAS_BYTE_ALIGNMENT)
                        throw std::runtime_error("a stamp's arguments or destination exceed its group's room");
                }
                if(triangles>UINT32_MAX || vertices>UINT32_MAX) throw std::runtime_error("the frame's cluster totals exceed the API's range");
                for(uint32_t i=0;i<p.bottomCount;++i) {
                    const Bottom& b=p.bottoms[i];
                    if(!b.count || b.count>kMegaGeometryMost+1u || uint64_t(b.first)+b.count>p.stampCount)
                        throw std::runtime_error("a bottom level names clusters its group does not have");
                    const uint64_t room=bottom_bytes(device, b.count, omm);
                    if(b.bytes<room || b.dest>p.blasBytes || b.bytes>p.blasBytes-b.dest || (p.blas+b.dest)%D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT)
                        throw std::runtime_error("a bottom level's destination is smaller than its call requires");
                    const uint32_t cls=mega_bottom_class(p.bottoms[i].count);
                    Class& c=byClass[cls]; c.clusters=cls; ++c.count;
                    if(uint64_t(c.total)+b.count>UINT32_MAX) throw std::runtime_error("a bottom-level call's total exceeds the API's range");
                    c.total+=b.count;
                }
            }
            const uint32_t stamps=uint32_t(stampTotal), bottomsAll=uint32_t(bottomTotal);
            if(!stamps || !bottomsAll) return true;
            classes_.clear(); { uint32_t first=0; for(auto& [cls, c]:byClass) { c.first=first; first+=c.count; classes_.push_back(c); } }
            stampCount_=stamps; triangles_=triangles; vertices_=vertices; omm_=omm;
            // One upload: the stamps' arguments, the bottom levels' (class by class), then every destination (the stamps',
            // then the bottom levels' in the same order). A bottom level reads its clusters' addresses from the stamps' part
            // of the destinations, as NVIDIA's samples read them.
            stampArgsAt_=0; bottomArgsAt_=aligned(uint64_t(stamps)*sizeof(StampArgs), 256);
            const uint64_t argsBytes=bottomArgsAt_+uint64_t(bottomsAll)*sizeof(BottomArgs), destAt=aligned(argsBytes, 256), destBytes=uint64_t(stamps+bottomsAll)*8;
            if(!args_ || args_.size<argsBytes) {
                if(args_) release.add(retireValue, args_.resource.Get());
                args_=make_buffer(device, std::max<uint64_t>(argsBytes*5/4, 65536), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, "Create the clusters' arguments");
            }
            if(!dest_ || dest_.size<destBytes) {
                if(dest_) release.add(retireValue, dest_.resource.Get());
                dest_=make_buffer(device, std::max<uint64_t>(destBytes*5/4, 65536), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "Create the clusters' addresses");
            }
            void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0; uint64_t offset=0; ID3D12Resource* source=nullptr;
            if(ring.allocate(destAt+destBytes, 256, &cpu, &gpu, &offset)) source=ring.resource();
            else {
                Buffer own=make_buffer(device, destAt+destBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Upload the clusters' arguments");
                cpu=own.mapped; offset=0; source=own.resource.Get(); release.add(retireValue, source);
            }
            auto* bytes=static_cast<uint8_t*>(cpu);
            auto* stampArgs=reinterpret_cast<StampArgs*>(bytes+stampArgsAt_);
            auto* bottomArgs=reinterpret_cast<BottomArgs*>(bytes+bottomArgsAt_);
            auto* dest=reinterpret_cast<D3D12_GPU_VIRTUAL_ADDRESS*>(bytes+destAt);
            rooms_.assign(bottomsAll, 0u);
            std::vector<uint32_t> placed(classes_.size(), 0u);
            uint32_t at=0;
            for(const auto& p:parts) {
                for(uint32_t i=0;i<p.stampCount;++i) {
                    const Stamp& s=p.stamps[i];
                    StampArgs a{}; a.clusterIdOffset=s.geometryOffset; a.geometryIndexOffset=s.geometryOffset; a.clusterTemplate=s.templ; a.vertexBuffer={s.vertices, 12};
                    std::memcpy(&stampArgs[at+i], &a, sizeof(a)); dest[at+i]=s.dest;
                }
                for(uint32_t i=0;i<p.bottomCount;++i) {
                    const Bottom& b=p.bottoms[i];
                    const uint32_t cls=mega_bottom_class(b.count);
                    size_t k=0; while(k<classes_.size() && classes_[k].clusters!=cls) ++k;
                    const uint32_t slot=classes_[k].first+placed[k]++;
                    BottomArgs a{}; a.clusterCount=b.count; a.reserved=0; a.clusterVAs=dest_.va()+uint64_t(at+b.first)*8;
                    std::memcpy(&bottomArgs[slot], &a, sizeof(a)); dest[stamps+slot]=p.blas+b.dest;
                    rooms_[slot]=uint32_t(std::min<uint64_t>(b.bytes, UINT32_MAX));
                }
                at+=p.stampCount;
            }
            barriers.transition(args_, D3D12_RESOURCE_STATE_COPY_DEST); barriers.transition(dest_, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
            list->CopyBufferRegion(args_.resource.Get(), 0, source, offset, argsBytes);
            list->CopyBufferRegion(dest_.resource.Get(), 0, source, offset+destAt, destBytes);
            barriers.transition(args_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.transition(dest_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            rewrote=true;
        }
        const uint32_t stamps=stampCount_;
        const uint32_t bottomsAll=uint32_t(rooms_.size());
        // Each call as the driver is told it, and its answer: the stamps' (every stamp one of a room of stampRoom), then a call
        // a class (every bottom level of it in a room of its class's). An answer larger than the rooms fails the trial.
        const ClusterInputs stampIn=triangles_call(NVAPI_D3D12_RAYTRACING_MULTI_INDIRECT_CLUSTER_OPERATION_TYPE_INSTANTIATE_CLUSTER_TEMPLATES, stamps, triangles_, vertices_, omm);
        if(rewrote) {
            const auto stampInfo=need(device, stampIn, "the stamps' call");
            if(stampInfo.resultDataMaxSizeInBytes>stampRoom) {
                char b[200]; std::snprintf(b, sizeof b, "the driver's size for a stamp of the frame's call (%llu bytes) is larger than a stamp's room (%llu)",
                                           (unsigned long long)stampInfo.resultDataMaxSizeInBytes, (unsigned long long)stampRoom);
                throw std::runtime_error(b);
            }
            uint64_t scratchBytes=std::max<uint64_t>(stampInfo.scratchDataSizeInBytes, 256);
            for(const auto& c:classes_) {
                const auto info=need(device, bottoms_call(c.count, c.total, c.clusters, omm), "a class's bottom levels' call");
                const uint64_t room=bottom_bytes(device, c.clusters, omm);
                if(info.resultDataMaxSizeInBytes>room) {
                    char b[240]; std::snprintf(b, sizeof b, "the driver's size for a bottom level of the frame's call of up to %u clusters (%llu bytes) is larger than its room (%llu)",
                                               c.clusters, (unsigned long long)info.resultDataMaxSizeInBytes, (unsigned long long)room);
                    throw std::runtime_error(b);
                }
                scratchBytes=std::max<uint64_t>(scratchBytes, info.scratchDataSizeInBytes);
            }
            scratchBytes_=scratchBytes;
        }
        // One scratch, used by the calls in turn (a barrier between them).
        if(!scratch_ || scratch_.size<scratchBytes_) {
            if(scratch_) release.add(retireValue, scratch_.resource.Get());
            scratch_=make_buffer(device, scratchBytes_*5/4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "Create the clusters' scratch");
        }
        const uint64_t sizesBytes=uint64_t(stamps+bottomsAll)*4;
        if(!sizes_ || sizes_.size<sizesBytes) {
            if(sizes_) release.add(retireValue, sizes_.resource.Get());
            sizes_=make_buffer(device, std::max<uint64_t>(sizesBytes*3/2, 4096), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, "Create the clusters' sizes");
        }
        barriers.transition(scratch_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); barriers.transition(sizes_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers.transition(args_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.transition(dest_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers.flush(list);
        ClusterDesc d{};
        d.inputs=stampIn; d.batchScratchData=scratch_.va();
        d.destinationAddressArray={dest_.va(), 8}; d.resultSizeArray={sizes_.va(), 4};
        d.indirectArgArray={args_.va()+stampArgsAt_, sizeof(StampArgs)}; d.indirectArgCount=0;
        execute(list, d, "stamp the clusters");
        barriers.uav(nullptr); barriers.flush(list);
        for(size_t k=0;k<classes_.size();++k) {
            const Class& c=classes_[k];
            ClusterDesc b{};
            b.inputs=bottoms_call(c.count, c.total, c.clusters, omm); b.batchScratchData=scratch_.va();
            b.destinationAddressArray={dest_.va()+uint64_t(stamps+c.first)*8, 8}; b.resultSizeArray={sizes_.va()+uint64_t(stamps+c.first)*4, 4};
            b.indirectArgArray={args_.va()+bottomArgsAt_+uint64_t(c.first)*sizeof(BottomArgs), sizeof(BottomArgs)}; b.indirectArgCount=0;
            execute(list, b, "build the plants' bottom levels from their clusters");
            barriers.uav(nullptr); barriers.flush(list); // (the scratch is the next call's)
            ++bottomCalls;
        }
        // Every kMegaSizeCheckEvery frames: the sizes the driver wrote, copied back to be held against their rooms (check).
        if(frames_%kMegaSizeCheckEvery==1 && checks_.size()<8) {
            SizeCheck c; c.fence=retireValue; c.stamps=stamps; c.stampRoom=stampRoom; c.rooms=rooms_;
            c.readback=make_buffer(device, sizesBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Read the clusters' sizes back");
            barriers.transition(sizes_, D3D12_RESOURCE_STATE_COPY_SOURCE); barriers.flush(list);
            list->CopyBufferRegion(c.readback.resource.Get(), 0, sizes_.resource.Get(), 0, sizesBytes);
            barriers.transition(sizes_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); barriers.flush(list);
            checks_.push_back(std::move(c));
        }
        if(rewrote) parts_=parts;
        return true;
    } catch(const std::exception& e) { parts_.clear(); error=e.what(); return false; }
}
bool MegaClusterBuilder::check(uint64_t completed, std::string& line, std::string& error) {
    line.clear();
    bool ok=true;
    while(!checks_.empty() && checks_.front().fence<=completed) {
        const SizeCheck& c=checks_.front();
        const auto* sizes=static_cast<const uint32_t*>(c.readback.mapped);
        uint32_t overStamps=0, overBottoms=0; uint64_t worstStampSize=0, worstBottomSize=0, worstBottomRoom=0;
        double stampShare=0, bottomShare=0;
        if(sizes) {
            for(uint32_t i=0;i<c.stamps;++i) {
                const double share=c.stampRoom ? double(sizes[i])/double(c.stampRoom) : 0.0;
                if(sizes[i]>c.stampRoom) ++overStamps;
                if(share>stampShare) { stampShare=share; worstStampSize=sizes[i]; }
            }
            for(size_t j=0;j<c.rooms.size();++j) {
                const uint32_t size=sizes[c.stamps+j];
                const double share=c.rooms[j] ? double(size)/double(c.rooms[j]) : 0.0;
                if(size>c.rooms[j]) ++overBottoms;
                if(share>bottomShare) { bottomShare=share; worstBottomSize=size; worstBottomRoom=c.rooms[j]; }
            }
            ++checked_; checkedStamps_+=c.stamps; checkedBottoms_+=c.rooms.size();
            worstStamp_=std::max(worstStamp_, stampShare); worstBottom_=std::max(worstBottom_, bottomShare);
        }
        char b[420];
        std::snprintf(b, sizeof b, "the driver's sizes of a frame's %u stamps and %zu bottom levels against their rooms: %u stamps and %u bottom levels larger; the largest stamp "
                      "%llu of its %llu bytes (%.0f %%), the fullest bottom level %llu of its %llu (%.0f %%); since the start %llu frames checked, the fullest %.0f %% and %.0f %%",
                      c.stamps, c.rooms.size(), overStamps, overBottoms, (unsigned long long)worstStampSize, (unsigned long long)c.stampRoom, 100.0*stampShare,
                      (unsigned long long)worstBottomSize, (unsigned long long)worstBottomRoom, 100.0*bottomShare, (unsigned long long)checked_, 100.0*worstStamp_, 100.0*worstBottom_);
        line=b;
        if(overStamps || overBottoms) { ok=false; error=b; }
        checks_.erase(checks_.begin());
    }
    return ok;
}
} // namespace tgpt
