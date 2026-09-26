// SPDX-License-Identifier: MIT
#include "scene.hpp"
#include <chrono>
#include <cmath>
#include <cstring>

namespace tgpt {
namespace {
constexpr uint64_t kReadBudget=64ull<<20;         // bytes of mesh data copied to staging per frame
constexpr uint32_t kBuildsPerFrame=48;            // BLAS builds per frame
constexpr uint64_t kTrianglesPerFrame=3000000;    // and their triangles
constexpr uint64_t kChunk=256ull<<20;             // arena chunk size
constexpr uint64_t kOmmTrianglesPerFrame=262144;  // alpha-tested triangles given micromaps per frame
constexpr uint64_t kOmmBudget=256ull<<20;         // micromap input bytes over the whole scene

float half_to_float(uint16_t h) {
    const uint32_t sign=(h>>15)&1, exponent=(h>>10)&31, mantissa=h&1023;
    float value;
    if(exponent==0) value=std::ldexp(float(mantissa), -24);
    else if(exponent==31) value=mantissa ? 0.f : 65504.f;
    else value=std::ldexp(float(mantissa|1024), int(exponent)-25);
    return sign ? -value : value;
}
uint32_t format_bytes(uint32_t format) {
    switch(format) { case TGPT_VF_FLOAT32: return 4; case TGPT_VF_FLOAT16: case TGPT_VF_UNORM16: case TGPT_VF_SNORM16: return 2; default: return 1; }
}
// One vertex attribute of up to four components; missing ones read as 0 (w as 1).
void read_attribute(const uint8_t* p, uint32_t format, uint32_t dimension, float out[4]) {
    out[0]=out[1]=out[2]=0.f; out[3]=1.f;
    for(uint32_t i=0;i<dimension && i<4;++i) {
        switch(format) {
        case TGPT_VF_FLOAT32: { float f; std::memcpy(&f, p+i*4, 4); out[i]=f; break; }
        case TGPT_VF_FLOAT16: { uint16_t h; std::memcpy(&h, p+i*2, 2); out[i]=half_to_float(h); break; }
        case TGPT_VF_UNORM16: { uint16_t v; std::memcpy(&v, p+i*2, 2); out[i]=float(v)/65535.f; break; }
        case TGPT_VF_SNORM16: { int16_t v; std::memcpy(&v, p+i*2, 2); out[i]=std::max(-1.f, float(v)/32767.f); break; }
        case TGPT_VF_UNORM8: out[i]=float(p[i])/255.f; break;
        case TGPT_VF_SNORM8: out[i]=std::max(-1.f, float(int8_t(p[i]))/127.f); break;
        default: break;
        }
    }
}
float finite_or_zero(float f) { return std::isfinite(f) ? f : 0.f; }

// Sub-allocations from large buffers: thousands of small committed resources would each round up to
// 64 KB. Never freed piecemeal; a cleared scene drops the chunks.
struct Arena {
    std::deque<Buffer> chunks; // a deque: ranges keep pointers to their chunk
    uint64_t offset=0;
    D3D12_RESOURCE_STATES initial=D3D12_RESOURCE_STATE_COMMON;
    D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE;
    struct Range { Buffer* chunk=nullptr; uint64_t offset=0, size=0; D3D12_GPU_VIRTUAL_ADDRESS va() const { return chunk->va()+offset; } };
    Range allocate(ID3D12Device* device, uint64_t size, uint64_t alignment, const char* what) {
        size=(size+alignment-1)/alignment*alignment;
        uint64_t start=chunks.empty() ? 0 : (offset+alignment-1)/alignment*alignment;
        if(chunks.empty() || start+size>chunks.back().size) {
            chunks.push_back(make_buffer(device, std::max(kChunk, size), D3D12_HEAP_TYPE_DEFAULT, flags, initial, what));
            start=0;
        }
        offset=start+size;
        Range r; r.chunk=&chunks.back(); r.offset=start; r.size=size; return r;
    }
    void release_all(DeferredRelease& release, uint64_t fence) {
        for(auto& c:chunks) release.add(fence, c.resource.Get());
        chunks.clear(); offset=0;
    }
};
Arena geometryArena, accelerationArena;
bool arenasReady=false;
void ensure_arenas() {
    if(arenasReady) return;
    geometryArena.initial=D3D12_RESOURCE_STATE_COMMON; geometryArena.flags=D3D12_RESOURCE_FLAG_NONE;
    accelerationArena.initial=D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE; accelerationArena.flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    arenasReady=true;
}
} // namespace

GpuScene::GpuScene() { thread_=std::thread([this]{ worker(); }); }
GpuScene::~GpuScene() {
    { std::lock_guard<std::mutex> lock(workMutex_); stopping_=true; }
    workSignal_.notify_all();
    if(thread_.joinable()) thread_.detach(); // the process is ending; the worker holds no GPU objects
}

void GpuScene::worker() {
    for(;;) {
        Mesh* mesh=nullptr;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workSignal_.wait(lock, [this]{ return stopping_ || !work_.empty(); });
            if(stopping_) return;
            mesh=work_.front(); work_.pop_front();
            converting_=mesh;
        }
        try { convert(*mesh); }
        catch(const std::exception& e) { log_throttled(std::string("Mesh conversion failed: ")+e.what()); mesh->state=Mesh::State::Failed; }
        catch(...) { mesh->state=Mesh::State::Failed; }
        {
            std::lock_guard<std::mutex> lock(workMutex_);
            done_.push_back(mesh);
            converting_=nullptr;
        }
        idleSignal_.notify_all();
    }
}

// Read-back bytes to compact float3 positions, float2 UVs, RGBA8 colors and 32-bit indices with the
// submeshes' base vertices applied. Submeshes that aren't triangle lists keep their geometry slot as one
// degenerate triangle, so a ray's geometry index still names the submesh (and its material).
void GpuScene::convert(Mesh& mesh) {
    const MeshSource& s=*mesh.source;
    MeshData& d=mesh.data;
    if(s.cpu) {
        d.positions=s.positions; d.uvs=s.uvs; d.colors=s.colors; d.indices=s.indices; d.vertexCount=s.vertexCount;
        for(const auto& sub:s.submeshes) d.geometries.push_back(GeometryRecordGpu{sub.indexStart, sub.indexCount, 0, 0});
        if(d.geometries.empty()) d.geometries.push_back(GeometryRecordGpu{0, uint32_t(s.indices.size()), 0, 0});
        mesh.state=Mesh::State::Converted; return;
    }
    auto attribute_fits=[&](uint32_t stream, uint32_t offset, uint32_t format, uint32_t dimension, uint32_t vertices) {
        if(stream>=4) return false;
        const auto& bytes=mesh.bytes[stream];
        const uint64_t need=uint64_t(vertices-1)*s.strides[stream]+offset+uint64_t(format_bytes(format))*dimension;
        return !bytes.empty() && s.strides[stream]>0 && need<=bytes.size();
    };
    const uint32_t vertices=s.vertexCount;
    if(!attribute_fits(s.positionStream, s.positionOffset, s.positionFormat, s.positionDimension, vertices))
        throw std::runtime_error("the vertex buffer is smaller than its positions ("+std::to_string(mesh.bytes[s.positionStream].size())+" bytes)");
    d.vertexCount=vertices;
    d.positions.resize(size_t(vertices)*3);
    const uint8_t* pb=mesh.bytes[s.positionStream].data();
    for(uint32_t v=0;v<vertices;++v) {
        float a[4]; read_attribute(pb+size_t(v)*s.strides[s.positionStream]+s.positionOffset, s.positionFormat, s.positionDimension, a);
        d.positions[size_t(v)*3]=finite_or_zero(a[0]); d.positions[size_t(v)*3+1]=finite_or_zero(a[1]); d.positions[size_t(v)*3+2]=finite_or_zero(a[2]);
    }
    if(attribute_fits(s.uvStream, s.uvOffset, s.uvFormat, s.uvDimension, vertices)) {
        d.uvs.resize(size_t(vertices)*2);
        const uint8_t* ub=mesh.bytes[s.uvStream].data();
        for(uint32_t v=0;v<vertices;++v) {
            float a[4]; read_attribute(ub+size_t(v)*s.strides[s.uvStream]+s.uvOffset, s.uvFormat, s.uvDimension, a);
            d.uvs[size_t(v)*2]=finite_or_zero(a[0]); d.uvs[size_t(v)*2+1]=finite_or_zero(a[1]);
        }
    }
    if(attribute_fits(s.colorStream, s.colorOffset, s.colorFormat, s.colorDimension, vertices)) {
        d.colors.resize(vertices);
        const uint8_t* cb=mesh.bytes[s.colorStream].data();
        for(uint32_t v=0;v<vertices;++v) {
            float a[4]; read_attribute(cb+size_t(v)*s.strides[s.colorStream]+s.colorOffset, s.colorFormat, s.colorDimension, a);
            uint32_t packed=0;
            for(int i=0;i<4;++i) packed|=uint32_t(std::lround(std::clamp(a[i], 0.f, 1.f)*255.f))<<(8*i);
            d.colors[v]=packed;
        }
    }
    const auto& ib=mesh.bytes[4];
    const uint32_t indexSize=s.indexFormat==32 ? 4 : 2;
    const uint64_t indexCount=ib.size()/indexSize;
    for(const auto& sub:s.submeshes) {
        GeometryRecordGpu g{uint32_t(d.indices.size()), 0, 0, 0};
        if(sub.topology==0 && uint64_t(sub.indexStart)+sub.indexCount<=indexCount && sub.indexCount>=3) {
            const uint32_t count=sub.indexCount-sub.indexCount%3;
            for(uint32_t i=0;i<count;++i) {
                uint32_t index;
                if(indexSize==4) std::memcpy(&index, ib.data()+(size_t(sub.indexStart)+i)*4, 4);
                else { uint16_t index16; std::memcpy(&index16, ib.data()+(size_t(sub.indexStart)+i)*2, 2); index=index16; }
                index+=sub.baseVertex;
                d.indices.push_back(index<vertices ? index : 0u);
            }
            g.indexCount=count;
        } else {
            d.indices.insert(d.indices.end(), {0u, 0u, 0u});
            g.indexCount=3;
        }
        d.geometries.push_back(g);
    }
    for(auto& b:mesh.bytes) { b.clear(); b.shrink_to_fit(); }
    mesh.state=Mesh::State::Converted;
}

bool GpuScene::intake(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    auto& e=engine();
    std::vector<std::pair<int32_t, std::unique_ptr<MeshSource>>> added;
    bool cleared=false;
    {
        std::lock_guard<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=clearGeneration_) { clearGeneration_=e.clearGeneration; cleared=true; }
        added.swap(e.newMeshes);
        if(e.materialsDirty) { materials_=e.materials; e.materialsDirty=false; materialsDirty_=true; }
        if(e.instancesDirty) { instances_=e.instances; materialIndices_=e.materialIndices; e.instancesDirty=false; indicesDirty_=true; }
        if(e.lightsDirty) { lights_=e.lights; e.lightsDirty=false; }
    }
    // Everything registered before the clear was dropped with it; what arrived since is kept.
    if(cleared) { clear(heap, release, retireValue); log("The plugin cleared the scene; the traced scene starts over"); }
    for(auto& entry:added) {
        const int32_t handle=entry.first;
        if(handle<=0) continue;
        if(meshes_.size()<size_t(handle)) meshes_.resize(size_t(handle));
        auto mesh=std::make_unique<Mesh>();
        mesh->skinned=entry.second && entry.second->skinned;
        mesh->handle=handle; mesh->source=std::move(entry.second);
        pending_.push_back(mesh.get());
        meshes_[size_t(handle)-1]=std::move(mesh);
    }
    return cleared;
}

void GpuScene::pump_d3d11(ID3D11Device* device, ID3D11DeviceContext* context, uint64_t frame) {
    readBytesThisFrame_=0;
    std::vector<Mesh*> toConvert;
    for(Mesh* mesh:pending_) {
        if(mesh->state==Mesh::State::Queued) {
            if(mesh->source->cpu) { mesh->state=Mesh::State::Converting; toConvert.push_back(mesh); continue; }
            if(readBytesThisFrame_>=kReadBudget) continue;
            try {
                auto stage=[&](ID3D11Buffer* source, ComPtr<ID3D11Buffer>& staging) {
                    if(!source) return;
                    D3D11_BUFFER_DESC d{}; source->GetDesc(&d);
                    D3D11_BUFFER_DESC s{}; s.ByteWidth=d.ByteWidth; s.Usage=D3D11_USAGE_STAGING; s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                    s.MiscFlags=d.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; s.StructureByteStride=s.MiscFlags ? d.StructureByteStride : 0;
                    HRESULT hr=device->CreateBuffer(&s, nullptr, &staging);
                    if(FAILED(hr)) { s.MiscFlags=0; s.StructureByteStride=0; hr=device->CreateBuffer(&s, nullptr, &staging); }
                    check(hr, "Create mesh read-back buffer");
                    context->CopyResource(staging.Get(), source);
                    readBytesThisFrame_+=d.ByteWidth;
                };
                const auto& src=*mesh->source;
                std::array<bool, 4> used{};
                used[src.positionStream]=true;
                if(src.uvStream<4) used[src.uvStream]=true;
                if(src.colorStream<4) used[src.colorStream]=true;
                for(size_t i=0;i<4;++i) if(used[i]) stage(src.vertexBuffers[i].Get(), mesh->staging[i]);
                stage(src.indexBuffer.Get(), mesh->staging[4]);
                mesh->copiedFrame=frame; mesh->state=Mesh::State::Copied;
            } catch(const std::exception& e) {
                log_throttled(std::string("Mesh read-back could not start: ")+e.what());
                mesh->state=Mesh::State::Failed;
                std::lock_guard<std::mutex> lock(workMutex_); done_.push_back(mesh);
            }
        } else if(mesh->state==Mesh::State::Copied && frame>=mesh->copiedFrame+2) {
            bool all=true, failed=false;
            for(size_t i=0;i<5;++i) {
                auto& staging=mesh->staging[i];
                if(!staging) continue;
                D3D11_MAPPED_SUBRESOURCE mapped{};
                const HRESULT hr=context->Map(staging.Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
                if(hr==DXGI_ERROR_WAS_STILL_DRAWING) { all=false; continue; }
                if(FAILED(hr)) { log_throttled("Mesh read-back map failed: "+tgb::hexhr(hr)); failed=true; break; }
                D3D11_BUFFER_DESC d{}; staging->GetDesc(&d);
                mesh->bytes[i].assign(static_cast<const uint8_t*>(mapped.pData), static_cast<const uint8_t*>(mapped.pData)+d.ByteWidth);
                context->Unmap(staging.Get(), 0);
                staging.Reset();
            }
            if(failed) {
                mesh->state=Mesh::State::Failed;
                std::lock_guard<std::mutex> lock(workMutex_); done_.push_back(mesh);
            } else if(all) {
                for(auto& b:mesh->source->vertexBuffers) b.Reset();
                mesh->source->indexBuffer.Reset();
                mesh->state=Mesh::State::Converting;
                toConvert.push_back(mesh);
            }
        }
    }
    if(!toConvert.empty()) {
        { std::lock_guard<std::mutex> lock(workMutex_); for(auto* m:toConvert) work_.push_back(m); }
        workSignal_.notify_all();
    }
}

// Allocates every mesh's ranges first, so the arena chunks (new ones included) can all go to COPY_DEST
// before any copy is recorded, and back to shader reads before the builds.
void GpuScene::upload_meshes(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release,
                             uint64_t retireValue, const std::vector<Mesh*>& meshes, Barriers& barriers) {
    struct Copy { const void* data; uint64_t bytes; Arena::Range range; };
    std::vector<Copy> copies;
    uint64_t total=0;
    for(Mesh* m:meshes) {
        auto& d=m->data;
        auto place=[&](const void* data, uint64_t bytes, uint32_t& srv) -> D3D12_GPU_VIRTUAL_ADDRESS {
            if(!bytes) { srv=kInvalid; return 0; }
            auto range=geometryArena.allocate(device, bytes, 16, "Create geometry arena");
            copies.push_back(Copy{data, bytes, range});
            total+=bytes;
            const uint32_t i=heap.allocate();
            D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=DXGI_FORMAT_R32_TYPELESS; v.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
            v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Buffer.FirstElement=range.offset/4; v.Buffer.NumElements=UINT(bytes/4);
            v.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
            device->CreateShaderResourceView(range.chunk->resource.Get(), &v, heap.cpu(i));
            srv=i;
            return range.va();
        };
        if(m->skinned) {
            // Written every frame by the skin pass before the rebuild; nothing to upload.
            const uint64_t bytes=std::max<uint64_t>(d.positions.size()*4, 16);
            m->skinPositions=make_buffer(device, bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_COMMON, "Create character positions");
            m->positionsSrv=raw_srv(device, heap, m->skinPositions.resource.Get(), bytes);
            m->skinUav=raw_uav(device, heap, m->skinPositions.resource.Get(), bytes);
            m->positionsVa=m->skinPositions.va();
        } else m->positionsVa=place(d.positions.data(), d.positions.size()*4, m->positionsSrv);
        place(d.uvs.data(), d.uvs.size()*4, m->uvsSrv);
        place(d.colors.data(), d.colors.size()*4, m->colorsSrv);
        m->indicesVa=place(d.indices.data(), d.indices.size()*4, m->indicesSrv);
        m->bytesOnGpu=(d.positions.size()+d.uvs.size()+d.colors.size()+d.indices.size())*4;
    }
    if(copies.empty()) return;
    Buffer upload=make_buffer(device, total, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create mesh upload buffer");
    for(auto& c:geometryArena.chunks) barriers.transition(c, D3D12_RESOURCE_STATE_COPY_DEST);
    barriers.flush(list);
    uint64_t at=0;
    for(const auto& c:copies) {
        std::memcpy(static_cast<uint8_t*>(upload.mapped)+at, c.data, c.bytes);
        list->CopyBufferRegion(c.range.chunk->resource.Get(), c.range.offset, upload.resource.Get(), at, c.bytes);
        at+=c.bytes;
    }
    for(auto& c:geometryArena.chunks) barriers.transition(c, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers.flush(list);
    release.add(retireValue, upload.resource.Get());
}

D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS GpuScene::blas_inputs(const Mesh& mesh, std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>& storage) const {
    const auto& d=mesh.data;
    storage.assign(d.geometries.size(), D3D12_RAYTRACING_GEOMETRY_DESC{});
    for(size_t i=0;i<storage.size();++i) {
        auto& g=storage[i];
        g.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NONE; // instances decide: forced opaque unless alpha-tested
        g.Triangles.VertexBuffer.StartAddress=mesh.positionsVa; g.Triangles.VertexBuffer.StrideInBytes=12;
        g.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; g.Triangles.VertexCount=d.vertexCount;
        g.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT; g.Triangles.IndexCount=d.geometries[i].indexCount;
        g.Triangles.IndexBuffer=mesh.indicesVa ? mesh.indicesVa+uint64_t(d.geometries[i].indexStart)*4 : 0;
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
    in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    // A character's is rebuilt every frame: fast to build.
    in.Flags=mesh.skinned ? D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD : D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    in.NumDescs=UINT(storage.size()); in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.pGeometryDescs=storage.data();
    return in;
}

void GpuScene::build_blas(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, Mesh& mesh, uint64_t& scratchOffset) {
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    const auto in=blas_inputs(mesh, geometries);
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
    if(!info.ResultDataMaxSizeInBytes) throw DxError(E_FAIL, "BLAS prebuild reported no size");
    auto result=accelerationArena.allocate(device, info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, "Create acceleration structure arena");
    const uint64_t scratchStart=(scratchOffset+255)/256*256;
    if(scratchStart+info.ScratchDataSizeInBytes>scratch_.size) throw DxError(E_FAIL, "BLAS scratch too small");
    mesh.blasVa=result.va();
    if(mesh.skinned) {
        // Built every frame from that frame's positions (none are there yet): only the storage now.
        mesh.skinScratchBytes=info.ScratchDataSizeInBytes;
    } else {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.DestAccelerationStructureData=result.va(); desc.ScratchAccelerationStructureData=scratch_.va()+scratchStart; desc.Inputs=in;
        list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
        scratchOffset=scratchStart+info.ScratchDataSizeInBytes;
    }
    mesh.bytesOnGpu+=result.size;
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.accelerationBytes+=result.size;
}

void GpuScene::upload_structured(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                                 DeferredRelease& release, uint64_t retireValue, Structured& target, const void* data, uint32_t count, uint32_t stride, Barriers& barriers) {
    const uint32_t needed=std::max<uint32_t>(count, 1);
    if(!target.buffer || target.capacity<needed || target.stride!=stride) {
        if(target.buffer) release.add(retireValue, target.buffer.resource.Get());
        heap.release(target.srv);
        uint32_t capacity=64; while(capacity<needed) capacity*=2;
        target.buffer=make_buffer(device, uint64_t(capacity)*stride, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, "Create scene record buffer");
        target.capacity=capacity; target.stride=stride;
        target.srv=structured_srv(device, heap, target.buffer.resource.Get(), capacity, stride);
    }
    if(!count) { barriers.transition(target.buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); return; }
    const uint64_t bytes=uint64_t(count)*stride;
    barriers.transition(target.buffer, D3D12_RESOURCE_STATE_COPY_DEST); barriers.flush(list);
    void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0; uint64_t offset=0;
    if(ring.allocate(bytes, 16, &cpu, &gpu, &offset)) {
        std::memcpy(cpu, data, bytes);
        list->CopyBufferRegion(target.buffer.resource.Get(), 0, ring.resource(), offset, bytes);
    } else {
        Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create record upload buffer");
        std::memcpy(upload.mapped, data, bytes);
        list->CopyBufferRegion(target.buffer.resource.Get(), 0, upload.resource.Get(), 0, bytes);
        release.add(retireValue, upload.resource.Get());
    }
    barriers.transition(target.buffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

Buffer* GpuScene::skinned_positions(int32_t handle, uint32_t vertexCount, uint32_t& uav) {
    uav=kInvalid;
    if(handle<=0 || size_t(handle)>meshes_.size()) return nullptr;
    Mesh* m=meshes_[size_t(handle)-1].get();
    if(!m || !m->skinned || m->state!=Mesh::State::Ready || !m->skinPositions || meshRecords_.size()<=size_t(handle) ||
       meshRecords_[size_t(handle)].vertexCount!=vertexCount) return nullptr;
    uav=m->skinUav;
    return &m->skinPositions;
}

void GpuScene::record(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, UploadRing& ring,
                      DeferredRelease& release, uint64_t retireValue, const double origin[3], const double camera[3],
                      const std::vector<TGPT_SkinnedInstance>& skinned) {
    ensure_arenas();
    Barriers barriers;
    // Converted meshes: upload and build, within this frame's budget.
    std::vector<Mesh*> converted;
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        uint64_t triangles=0;
        while(!done_.empty() && converted.size()<kBuildsPerFrame && triangles<kTrianglesPerFrame) {
            Mesh* m=done_.front(); done_.pop_front();
            if(m->state==Mesh::State::Converted) { converted.push_back(m); triangles+=m->data.indices.size()/3; }
            else if(m->state==Mesh::State::Failed) {
                std::lock_guard<std::mutex> s(statsMutex_); ++stats_.meshesFailed;
                log_throttled("A mesh could not be read back (key "+std::to_string(m->source ? m->source->key : 0)+"); it is left out of the traced scene");
            }
        }
    }
    if(!converted.empty()) {
        upload_meshes(device, list, heap, release, retireValue, converted, barriers);
        // Scratch for all of this frame's builds, side by side.
        uint64_t scratchNeeded=0;
        for(Mesh* m:converted) {
            std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
            const auto in=blas_inputs(*m, geos);
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{}; device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
            scratchNeeded+=(info.ScratchDataSizeInBytes+255)/256*256+256;
        }
        if(scratch_.size<scratchNeeded) {
            if(scratch_) release.add(retireValue, scratch_.resource.Get());
            scratch_=make_buffer(device, std::max<uint64_t>(scratchNeeded, 32ull<<20), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create BLAS scratch");
        }
        uint64_t scratchOffset=0;
        for(Mesh* m:converted) {
            try {
                build_blas(device, list, *m, scratchOffset);
                m->geometryBase=uint32_t(geometryRecords_.size());
                for(const auto& g:m->data.geometries) geometryRecords_.push_back(g);
                if(meshRecords_.size()<=size_t(m->handle)) meshRecords_.resize(size_t(m->handle)+1, MeshRecordGpu{kInvalid, kInvalid, kInvalid, kInvalid, 0, 0, 0, 0});
                meshRecords_[size_t(m->handle)]=MeshRecordGpu{m->positionsSrv, m->uvsSrv, m->colorsSrv, m->indicesSrv, m->geometryBase,
                    uint32_t(m->data.geometries.size()), m->data.vertexCount, 0};
                m->triangles=m->data.indices.size()/3;
                m->state=Mesh::State::Ready;
                meshRecordsDirty_=true;
                std::lock_guard<std::mutex> s(statsMutex_);
                ++stats_.meshesReady; stats_.triangles+=m->triangles; stats_.geometryBytes+=m->bytesOnGpu;
            } catch(const std::exception& e) {
                m->state=Mesh::State::Failed;
                log_throttled(std::string("A mesh's acceleration structure could not be built: ")+e.what());
                std::lock_guard<std::mutex> s(statsMutex_); ++stats_.meshesFailed;
            }
            // The CPU copies have served their purpose; the GPU has its own (the geometry records stay).
            m->data.positions=std::vector<float>(); m->data.uvs=std::vector<float>(); m->data.colors=std::vector<uint32_t>(); m->data.indices=std::vector<uint32_t>();
            m->source.reset();
        }
        barriers.uav(nullptr); barriers.flush(list);
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [](Mesh* m){ return m->state==Mesh::State::Ready || m->state==Mesh::State::Failed; }), pending_.end());
    {
        std::lock_guard<std::mutex> s(statsMutex_);
        stats_.meshesQueued=uint32_t(pending_.size());
    }
    if(meshRecordsDirty_ || !meshBuffer_.buffer) {
        upload_structured(device, list, heap, ring, release, retireValue, meshBuffer_, meshRecords_.data(), uint32_t(meshRecords_.size()), sizeof(MeshRecordGpu), barriers);
        upload_structured(device, list, heap, ring, release, retireValue, geometryBuffer_, geometryRecords_.data(), uint32_t(geometryRecords_.size()), sizeof(GeometryRecordGpu), barriers);
        meshRecordsDirty_=false;
    }
    if(materialsDirty_ || !materialBuffer_.buffer) {
        upload_structured(device, list, heap, ring, release, retireValue, materialBuffer_, materials_.data(), uint32_t(materials_.size()), sizeof(TGPT_Material), barriers);
        materialsDirty_=false;
        // What a material's micromaps depend on: its alpha, tiling, texture, cutoff and flags.
        materialSignatures_.resize(materials_.size());
        for(size_t i=0;i<materials_.size();++i) {
            const TGPT_Material& m=materials_[i];
            uint64_t h=1469598103934665603ull;
            auto mix=[&h](const void* p, size_t n) { for(size_t b=0;b<n;++b) { h^=static_cast<const uint8_t*>(p)[b]; h*=1099511628211ull; } };
            mix(&m.baseColor[3], 4); mix(m.uvScaleOffset, 16); mix(&m.albedoTile, 4); mix(&m.alphaCutoff, 4); mix(&m.flags, 4); mix(&m.alphaTile, 4);
            materialSignatures_[i]=h;
        }
    }
    // Characters' materials go after the static list, freshly each frame they are traced.
    const uint32_t staticIndexCount=uint32_t(materialIndices_.size());
    std::vector<uint32_t> characterBase(skinned.size(), kInvalid);
    if(!skinned.empty()) {
        combinedIndices_=materialIndices_;
        for(size_t i=0;i<skinned.size();++i) {
            const auto& s=skinned[i];
            if(s.mesh<=0 || size_t(s.mesh)>=meshRecords_.size()) continue;
            const uint32_t geometries=meshRecords_[size_t(s.mesh)].geometryCount;
            characterBase[i]=uint32_t(combinedIndices_.size());
            for(uint32_t g=0;g<geometries;++g) {
                const uint32_t n=std::clamp(s.submeshCount, 1u, TGPT_SKINNED_MATERIALS);
                combinedIndices_.push_back(s.materials[std::min(g, n-1)]);
            }
        }
    }
    if(indicesDirty_ || !indexListBuffer_.buffer || !skinned.empty() || combinedLast_) {
        const auto& list_=skinned.empty() ? materialIndices_ : combinedIndices_;
        upload_structured(device, list, heap, ring, release, retireValue, indexListBuffer_, list_.data(), uint32_t(list_.size()), 4, barriers);
        indicesDirty_=false; combinedLast_=!skinned.empty();
    }
    // Instances whose mesh is ready, relative to the origin; each one's record is its TLAS instance ID.
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> descs; descs.reserve(instances_.size());
    std::vector<InstanceRecordGpu> records; records.reserve(instances_.size());
    const uint32_t materialCount=uint32_t(materials_.size()), indexCount=staticIndexCount;
    uint32_t ommInstances=0;
    for(const auto& inst:instances_) {
        if(inst.flags&TGPT_INSTANCE_HIDDEN) continue;
        if(inst.mesh<=0 || size_t(inst.mesh)>meshes_.size()) continue;
        Mesh* m=meshes_[size_t(inst.mesh)-1].get();
        if(!m || m->state!=Mesh::State::Ready) continue;
        // Every submesh needs a material index, and every index a material.
        const uint32_t geometryCount=meshRecords_[size_t(inst.mesh)].geometryCount;
        if(uint64_t(inst.materialBase)+geometryCount>indexCount) continue;
        bool materialsOk=true;
        for(uint32_t gi=0;gi<geometryCount;++gi) if(materialIndices_[inst.materialBase+gi]>=materialCount) { materialsOk=false; break; }
        if(!materialsOk) continue;
        D3D12_RAYTRACING_INSTANCE_DESC d{};
        bool finite=true;
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
            float v=inst.transform[r*4+c];
            if(c==3) v=float(double(v)-origin[r]);
            if(!std::isfinite(v)) finite=false;
            d.Transform[r][c]=v;
        }
        if(!finite) continue;
        d.InstanceID=uint32_t(records.size())&0xFFFFFF;
        d.InstanceMask=inst.mask ? (inst.mask&0xFF) : 0xFF;
        const bool alpha=(inst.flags&TGPT_INSTANCE_ALPHA_TEST)!=0;
        if(alpha && ommEnabled && m->omm==Mesh::Omm::Unchecked) want_omm(*m, inst.materialBase);
        // With micromaps the BLAS's own flags decide (its alpha-tested submeshes non-opaque, the rest
        // opaque); an instance whose materials differ from the ones they were made for goes without them.
        const bool linked=m->omm==Mesh::Omm::Linked;
        const bool omm=linked && alpha && ommAllowed && omm_matches(*m, inst.materialBase);
        d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        if(omm) ++ommInstances;
        else d.Flags|=(linked ? D3D12_RAYTRACING_INSTANCE_FLAG_DISABLE_OMMS : D3D12_RAYTRACING_INSTANCE_FLAG_NONE)|
            (alpha ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
        d.AccelerationStructure=linked ? m->ommBlasVa : m->blasVa;
        descs.push_back(d);
        records.push_back(InstanceRecordGpu{uint32_t(inst.mesh), inst.materialBase, inst.flags, 0});
    }
    // Characters: each BLAS rebuilt in place from the positions the skin pass wrote this frame, then an
    // instance like any other.
    uint32_t characters=0; uint64_t characterTriangles=0;
    if(!skinned.empty()) {
        std::vector<std::pair<Mesh*, size_t>> todo;
        uint64_t scratchNeeded=0;
        for(size_t i=0;i<skinned.size();++i) {
            const auto& s=skinned[i];
            if(characterBase[i]==kInvalid || s.mesh<=0 || size_t(s.mesh)>meshes_.size()) continue;
            Mesh* m=meshes_[size_t(s.mesh)-1].get();
            if(!m || !m->skinned || m->state!=Mesh::State::Ready || !m->blasVa) continue;
            bool materialsOk=true;
            const uint32_t geometries=meshRecords_[size_t(s.mesh)].geometryCount;
            for(uint32_t g=0;g<geometries;++g) if(combinedIndices_[characterBase[i]+g]>=materialCount) { materialsOk=false; break; }
            if(!materialsOk) continue;
            todo.emplace_back(m, i);
            scratchNeeded+=(m->skinScratchBytes+255)/256*256+256;
        }
        if(!todo.empty()) {
            if(skinScratch_.size<scratchNeeded) {
                if(skinScratch_) release.add(retireValue, skinScratch_.resource.Get());
                skinScratch_=make_buffer(device, std::max<uint64_t>(scratchNeeded*3/2, 8ull<<20), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create character BLAS scratch");
            }
            uint64_t offset=0;
            for(auto& [m, i]:todo) {
                std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geos;
                const auto in=blas_inputs(*m, geos);
                D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
                offset=(offset+255)/256*256;
                desc.DestAccelerationStructureData=m->blasVa; desc.ScratchAccelerationStructureData=skinScratch_.va()+offset; desc.Inputs=in;
                list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
                offset+=m->skinScratchBytes;
            }
            barriers.uav(nullptr); barriers.flush(list);
            for(auto& [m, i]:todo) {
                const auto& s=skinned[i];
                D3D12_RAYTRACING_INSTANCE_DESC d{};
                bool finite=true;
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) {
                    float v=s.transform[r*4+c];
                    if(c==3) v=float(double(v)-origin[r]);
                    if(!std::isfinite(v)) finite=false;
                    d.Transform[r][c]=v;
                }
                if(!finite) continue;
                d.InstanceID=uint32_t(records.size())&0xFFFFFF;
                d.InstanceMask=0xFF;
                // A character with an alpha-tested material (hair, torn cloth, the plugin's invisible one) is tested
                // texel by texel through its texture coordinates, like the world's; one without is forced opaque.
                bool alpha=false;
                const uint32_t geometries=meshRecords_[size_t(s.mesh)].geometryCount;
                for(uint32_t g=0;g<geometries && !alpha;++g) alpha=(materials_[combinedIndices_[characterBase[i]+g]].flags&TGPT_MATERIAL_ALPHA_TEST)!=0;
                d.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE|
                    (alpha ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
                d.AccelerationStructure=m->blasVa;
                descs.push_back(d);
                records.push_back(InstanceRecordGpu{uint32_t(s.mesh), characterBase[i], alpha ? TGPT_INSTANCE_ALPHA_TEST : 0u, 0});
                ++characters; characterTriangles+=m->triangles;
            }
        }
    }
    upload_structured(device, list, heap, ring, release, retireValue, instanceBuffer_, records.data(), uint32_t(records.size()), sizeof(InstanceRecordGpu), barriers);
    std::vector<TGPT_Light> lights=lights_;
    for(auto& l:lights) for(int i=0;i<3;++i) l.position[i]=float(double(l.position[i])-origin[i]);
    upload_structured(device, list, heap, ring, release, retireValue, lightBuffer_, lights.data(), uint32_t(lights.size()), sizeof(TGPT_Light), barriers);
    lightCount=uint32_t(lights.size());
    // The grid follows the camera in 32 m steps and is rebuilt when it moves or a light moves or changes
    // range (positions compared to a quarter metre, which the cells' padding covers); flicker keeps it.
    double corner[3];
    for(int i=0;i<3;++i) corner[i]=std::floor(camera[i]/kGridSnap)*kGridSnap-double(kGridDims[i])*kGridCell*0.5;
    uint64_t hash=1469598103934665603ull;
    auto mix=[&hash](uint64_t v) { for(int b=0;b<8;++b) { hash^=(v>>(b*8))&0xFF; hash*=1099511628211ull; } };
    for(double c:corner) mix(uint64_t(int64_t(c)));
    for(const auto& l:lights_) {
        for(float p:l.position) mix(uint64_t(int64_t(std::llround(double(p)*4.0))));
        mix(l.type); mix(uint64_t(std::llround(double(l.range)*16.0)));
    }
    if(!gridBuilt_ || hash!=gridHash_) {
        build_light_grid(corner);
        upload_structured(device, list, heap, ring, release, retireValue, gridCellBuffer_, gridCells_.data(), uint32_t(gridCells_.size()/2), 8, barriers);
        upload_structured(device, list, heap, ring, release, retireValue, gridIndexBuffer_, gridIndices_.data(), uint32_t(gridIndices_.size()), 4, barriers);
        gridHash_=hash; gridBuilt_=true;
    }
    for(int i=0;i<3;++i) gridMin[i]=float(gridCorner_[i]-origin[i]);
    gridCellsSrv=gridCellBuffer_.srv; gridIndicesSrv=gridIndexBuffer_.srv;
    barriers.flush(list);

    // The top level: rebuilt each frame (it is small next to the tracing).
    D3D12_GPU_VIRTUAL_ADDRESS descAddress=0;
    if(!descs.empty()) {
        const uint64_t bytes=descs.size()*sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
        void* cpu=nullptr;
        if(!ring.allocate(bytes, 16, &cpu, &descAddress)) {
            Buffer upload=make_buffer(device, bytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create instance upload buffer");
            cpu=upload.mapped; descAddress=upload.va();
            release.add(retireValue, upload.resource.Get());
        }
        std::memcpy(cpu, descs.data(), bytes);
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS in{};
    in.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    in.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    in.NumDescs=UINT(descs.size()); in.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; in.InstanceDescs=descAddress;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};
    device->GetRaytracingAccelerationStructurePrebuildInfo(&in, &info);
    if(!tlasBuffer_ || tlasBuffer_.size<info.ResultDataMaxSizeInBytes) {
        if(tlasBuffer_) release.add(retireValue, tlasBuffer_.resource.Get());
        tlasBuffer_=make_buffer(device, std::max<uint64_t>(info.ResultDataMaxSizeInBytes*3/2, 1ull<<20), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, "Create TLAS");
    }
    if(!tlasScratch_ || tlasScratch_.size<info.ScratchDataSizeInBytes) {
        if(tlasScratch_) release.add(retireValue, tlasScratch_.resource.Get());
        tlasScratch_=make_buffer(device, std::max<uint64_t>(info.ScratchDataSizeInBytes*3/2, 1ull<<20), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create TLAS scratch");
    }
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
    desc.DestAccelerationStructureData=tlasBuffer_.va(); desc.ScratchAccelerationStructureData=tlasScratch_.va(); desc.Inputs=in;
    list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
    barriers.uav(tlasBuffer_.resource.Get()); barriers.flush(list);
    tlas=tlasBuffer_.va(); hasTlas=true;
    instancesSrv=instanceBuffer_.srv; meshesSrv=meshBuffer_.srv; geometriesSrv=geometryBuffer_.srv; materialsSrv=materialBuffer_.srv;
    materialIndicesSrv=indexListBuffer_.srv; lightsSrv=lightBuffer_.srv;
    std::lock_guard<std::mutex> s(statsMutex_);
    stats_.instancesTraced=uint32_t(descs.size());
    stats_.ommInstances=ommInstances; stats_.ommWaiting=uint32_t(ommQueue_.size());
    stats_.charactersTraced=characters; stats_.characterTriangles=characterTriangles;
}

void GpuScene::tile_settled(int32_t tile) {
    if(tile<0 || uint32_t(tile)>=kMaxTiles) return;
    if(tilesSettled_.size()<=size_t(tile)) tilesSettled_.resize(size_t(tile)+1, 0);
    tilesSettled_[size_t(tile)]=1;
}
void GpuScene::alpha_tile_settled(int32_t tile) {
    if(tile<0 || uint32_t(tile)>=kMaxAlphaTiles) return;
    if(alphaTilesSettled_.size()<=size_t(tile)) alphaTilesSettled_.resize(size_t(tile)+1, 0);
    alphaTilesSettled_[size_t(tile)]=1;
}

// The first alpha-tested instance of a mesh decides its submeshes' materials; a mesh without an
// alpha-tested submesh among them needs no micromaps.
void GpuScene::want_omm(Mesh& mesh, uint32_t materialBase) {
    const size_t n=mesh.data.geometries.size();
    mesh.ommMaterials.assign(n, kInvalid); mesh.ommSignatures.assign(n, 0);
    uint64_t alphaTriangles=0;
    for(size_t g=0;g<n;++g) {
        const uint32_t index=materialIndices_[materialBase+g];
        mesh.ommMaterials[g]=index; mesh.ommSignatures[g]=materialSignatures_[index];
        if(materials_[index].flags&TGPT_MATERIAL_ALPHA_TEST) alphaTriangles+=mesh.data.geometries[g].indexCount/3;
    }
    if(!alphaTriangles || alphaTriangles>0xFFFFFFFFull/kOmmBytes) { mesh.omm=Mesh::Omm::None; return; }
    mesh.alphaTriangles=uint32_t(alphaTriangles);
    mesh.omm=Mesh::Omm::Waiting;
    ommQueue_.push_back(&mesh);
}
bool GpuScene::omm_matches(const Mesh& mesh, uint32_t materialBase) const {
    for(size_t g=0;g<mesh.ommMaterials.size();++g) {
        const uint32_t index=materialIndices_[materialBase+g];
        if(index!=mesh.ommMaterials[g] || materialSignatures_[index]!=mesh.ommSignatures[g]) return false;
    }
    return true;
}

void GpuScene::record_omm(ID3D12Device5* device, ID3D12GraphicsCommandList4* list, DescriptorHeap& heap, DeferredRelease& release,
                          uint64_t retireValue, ID3D12PipelineState* bake) {
    if(!ommEnabled || !bake || ommQueue_.empty()) return;
    // Meshes whose alpha-tested textures are all in the atlas, within this frame's budget.
    std::vector<Mesh*> batch; uint64_t triangles=0;
    for(auto it=ommQueue_.begin(); it!=ommQueue_.end() && triangles<kOmmTrianglesPerFrame;) {
        Mesh* m=*it;
        if(m->state!=Mesh::State::Ready || m->omm!=Mesh::Omm::Waiting) { it=ommQueue_.erase(it); continue; }
        bool stale=false, settled=true;
        for(size_t g=0;g<m->ommMaterials.size();++g) {
            const uint32_t index=m->ommMaterials[g];
            if(index>=materials_.size() || materialSignatures_[index]!=m->ommSignatures[g]) { stale=true; break; }
            const TGPT_Material& mat=materials_[index];
            // Micromaps are baked from the texels the alpha test reads: the alpha tile when the material has one.
            const bool alphaTile=(mat.flags&TGPT_MATERIAL_ALPHA_TILE) && mat.alphaTile>=0;
            const int32_t tile=alphaTile ? mat.alphaTile : mat.albedoTile;
            const std::vector<uint8_t>& ready=alphaTile ? alphaTilesSettled_ : tilesSettled_;
            if((mat.flags&TGPT_MATERIAL_ALPHA_TEST) && tile>=0 && (size_t(tile)>=ready.size() || !ready[size_t(tile)])) settled=false;
        }
        // A material that changed since: the next alpha-tested instance decides again.
        if(stale) { m->omm=Mesh::Omm::Unchecked; it=ommQueue_.erase(it); continue; }
        if(!settled) { ++it; continue; }
        const uint64_t bytes=uint64_t(m->alphaTriangles)*kOmmBytes;
        if(ommInputBytes_+bytes>kOmmBudget) {
            if(!ommBudgetLogged_) { log("Opacity micromaps reached their "+std::to_string(kOmmBudget>>20)+" MB budget; further alpha-tested meshes keep the alpha test alone"); ommBudgetLogged_=true; }
            m->omm=Mesh::Omm::None; it=ommQueue_.erase(it); continue;
        }
        if(!batch.empty() && triangles+m->alphaTriangles>kOmmTrianglesPerFrame) { ++it; continue; }
        ommInputBytes_+=bytes; triangles+=m->alphaTriangles;
        batch.push_back(m); it=ommQueue_.erase(it);
    }
    if(batch.empty()) return;

    // Per mesh: every alpha-tested triangle's micromap (input), index and descriptor, baked on the GPU.
    struct Work {
        Mesh* mesh=nullptr; std::vector<uint32_t> first; uint32_t count=0; Buffer input, indices, descs;
        D3D12_RAYTRACING_OPACITY_MICROMAP_HISTOGRAM_ENTRY histogram{};
        D3D12_RAYTRACING_OPACITY_MICROMAP_ARRAY_DESC array{};
        std::vector<D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC> triangles;
        std::vector<D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC> links;
        std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS arrayInputs{}, blasInputs{};
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO arrayInfo{}, blasInfo{};
        Arena::Range arrayRange, blasRange;
    };
    std::vector<Work> work(batch.size());
    Barriers barriers;
    list->SetPipelineState(bake);
    for(size_t w=0;w<batch.size();++w) {
        Work& k=work[w]; k.mesh=batch[w];
        const auto& geos=k.mesh->data.geometries;
        k.first.assign(geos.size(), kInvalid);
        for(size_t g=0;g<geos.size();++g)
            if(materials_[k.mesh->ommMaterials[g]].flags&TGPT_MATERIAL_ALPHA_TEST) { k.first[g]=k.count; k.count+=geos[g].indexCount/3; }
        k.input=make_buffer(device, uint64_t(k.count)*kOmmBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create micromap input");
        k.indices=make_buffer(device, uint64_t(k.count)*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create micromap indices");
        k.descs=make_buffer(device, uint64_t(k.count)*sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create micromap descriptors");
        auto* descs=static_cast<D3D12_RAYTRACING_OPACITY_MICROMAP_DESC*>(k.descs.mapped);
        for(uint32_t i=0;i<k.count;++i) {
            D3D12_RAYTRACING_OPACITY_MICROMAP_DESC d{}; d.ByteOffset=i*kOmmBytes; d.SubdivisionLevel=kOmmLevel; d.Format=D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE;
            descs[i]=d;
        }
        const uint32_t inputUav=raw_uav(device, heap, k.input.resource.Get(), k.input.size);
        const uint32_t indexUav=raw_uav(device, heap, k.indices.resource.Get(), k.indices.size);
        for(size_t g=0;g<geos.size();++g) {
            if(k.first[g]==kInvalid) continue;
            const uint32_t count=geos[g].indexCount/3;
            const uint32_t constants[8]={k.mesh->indicesSrv, k.mesh->uvsSrv, k.mesh->ommMaterials[g], k.first[g], inputUav, indexUav, count, geos[g].indexStart};
            list->SetComputeRoot32BitConstants(1, 8, constants, 0);
            list->Dispatch(std::min(count, 32768u), (count+32767)/32768, 1);
        }
        // Their slots return once this frame is done (the heap defers every release).
        heap.release(inputUav); heap.release(indexUav);
    }
    barriers.uav(nullptr);
    for(auto& k:work) { barriers.transition(k.input, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); barriers.transition(k.indices, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); }
    barriers.flush(list);

    // The micromap arrays, then each mesh's BLAS again with its alpha-tested submeshes linked to them.
    uint64_t scratchNeeded=0;
    for(auto& k:work) {
        k.histogram.Count=k.count; k.histogram.SubdivisionLevel=kOmmLevel; k.histogram.Format=D3D12_RAYTRACING_OPACITY_MICROMAP_FORMAT_OC1_4_STATE;
        k.array.NumOmmHistogramEntries=1; k.array.pOmmHistogram=&k.histogram; k.array.InputBuffer=k.input.va();
        k.array.PerOmmDescs.StartAddress=k.descs.va(); k.array.PerOmmDescs.StrideInBytes=sizeof(D3D12_RAYTRACING_OPACITY_MICROMAP_DESC);
        k.arrayInputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_OPACITY_MICROMAP_ARRAY;
        k.arrayInputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        k.arrayInputs.NumDescs=1; k.arrayInputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; k.arrayInputs.pOpacityMicromapArrayDesc=&k.array;
        device->GetRaytracingAccelerationStructurePrebuildInfo(&k.arrayInputs, &k.arrayInfo);
        const auto& d=k.mesh->data;
        const size_t n=d.geometries.size();
        k.triangles.assign(n, D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC{}); k.links.assign(n, D3D12_RAYTRACING_GEOMETRY_OMM_LINKAGE_DESC{});
        k.geometries.assign(n, D3D12_RAYTRACING_GEOMETRY_DESC{});
        for(size_t g=0;g<n;++g) {
            auto& t=k.triangles[g];
            t.VertexBuffer.StartAddress=k.mesh->positionsVa; t.VertexBuffer.StrideInBytes=12;
            t.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT; t.VertexCount=d.vertexCount;
            t.IndexFormat=DXGI_FORMAT_R32_UINT; t.IndexCount=d.geometries[g].indexCount;
            t.IndexBuffer=k.mesh->indicesVa ? k.mesh->indicesVa+uint64_t(d.geometries[g].indexStart)*4 : 0;
            auto& geo=k.geometries[g];
            if(k.first[g]==kInvalid) { geo.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES; geo.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE; geo.Triangles=t; continue; }
            auto& l=k.links[g];
            l.OpacityMicromapIndexBuffer.StartAddress=k.indices.va()+uint64_t(k.first[g])*4; l.OpacityMicromapIndexBuffer.StrideInBytes=4;
            l.OpacityMicromapIndexFormat=DXGI_FORMAT_R32_UINT; l.OpacityMicromapBaseLocation=0;
            l.OpacityMicromapArray=0; // its place is known once the array is allocated
            geo.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_OMM_TRIANGLES; geo.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            geo.OmmTriangles.pTriangles=&t; geo.OmmTriangles.pOmmLinkage=&l;
        }
        k.blasInputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        k.blasInputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_DISABLE_OMMS;
        k.blasInputs.NumDescs=UINT(n); k.blasInputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY; k.blasInputs.pGeometryDescs=k.geometries.data();
        device->GetRaytracingAccelerationStructurePrebuildInfo(&k.blasInputs, &k.blasInfo);
        scratchNeeded+=(k.arrayInfo.ScratchDataSizeInBytes+255)/256*256+256+(k.blasInfo.ScratchDataSizeInBytes+255)/256*256+256;
    }
    if(ommScratch_.size<scratchNeeded) {
        if(ommScratch_) release.add(retireValue, ommScratch_.resource.Get());
        ommScratch_=make_buffer(device, std::max<uint64_t>(scratchNeeded, 16ull<<20), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create micromap scratch");
    }
    uint64_t scratch=0;
    auto build=[&](const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& in, const D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO& info, Arena::Range& range) {
        if(!info.ResultDataMaxSizeInBytes) throw DxError(E_FAIL, "prebuild reported no size");
        range=accelerationArena.allocate(device, info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, "Create acceleration structure arena");
        scratch=(scratch+255)/256*256;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc{};
        desc.DestAccelerationStructureData=range.va(); desc.ScratchAccelerationStructureData=ommScratch_.va()+scratch; desc.Inputs=in;
        list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
        scratch+=info.ScratchDataSizeInBytes;
    };
    std::vector<bool> ok(work.size(), true);
    for(size_t w=0;w<work.size();++w) {
        try { build(work[w].arrayInputs, work[w].arrayInfo, work[w].arrayRange); }
        catch(const std::exception& e) { ok[w]=false; log_throttled(std::string("An opacity micromap array could not be built: ")+e.what()); }
    }
    barriers.uav(nullptr); barriers.flush(list);
    uint64_t triangleCount=0, bytes=0; uint32_t meshes=0;
    for(size_t w=0;w<work.size();++w) {
        Work& k=work[w];
        if(ok[w]) {
            for(size_t g=0;g<k.links.size();++g) if(k.first[g]!=kInvalid) k.links[g].OpacityMicromapArray=k.arrayRange.va();
            try { build(k.blasInputs, k.blasInfo, k.blasRange); }
            catch(const std::exception& e) { ok[w]=false; log_throttled(std::string("A BLAS with opacity micromaps could not be built: ")+e.what()); }
        }
        if(ok[w]) {
            k.mesh->ommBlasVa=k.blasRange.va(); k.mesh->omm=Mesh::Omm::Linked;
            ++meshes; triangleCount+=k.count; bytes+=k.arrayRange.size+k.blasRange.size;
        } else k.mesh->omm=Mesh::Omm::None;
        // Micromap inputs are read by the builds only.
        release.add(retireValue, k.input.resource.Get()); release.add(retireValue, k.indices.resource.Get()); release.add(retireValue, k.descs.resource.Get());
    }
    barriers.uav(nullptr); barriers.flush(list);
    std::lock_guard<std::mutex> s(statsMutex_);
    stats_.ommMeshes+=meshes; stats_.ommTriangles+=triangleCount; stats_.ommBytes+=bytes; stats_.accelerationBytes+=bytes;
}

void GpuScene::build_light_grid(const double corner[3]) {
    const uint32_t dx=kGridDims[0], dy=kGridDims[1], dz=kGridDims[2];
    const size_t cellCount=size_t(dx)*dy*dz;
    std::memcpy(gridCorner_, corner, sizeof(gridCorner_));
    gridCells_.assign(cellCount*2, 0);
    gridFill_.assign(cellCount, 0);
    // Counted, then filled: every cell's lights are contiguous, and no cell has a cap that would drop one.
    for(int pass=0;pass<2;++pass) {
        for(size_t i=0;i<lights_.size();++i) {
            const TGPT_Light& l=lights_[i];
            if(l.type>1 || !(l.range>0.f) || !std::isfinite(l.range) ||
               !std::isfinite(l.position[0]) || !std::isfinite(l.position[1]) || !std::isfinite(l.position[2])) continue;
            const double r=double(l.range)+kGridPad;
            int lo[3], hi[3]; bool inside=true;
            for(int a=0;a<3;++a) {
                const double p=double(l.position[a])-corner[a];
                lo[a]=std::max(0, int(std::floor((p-r)/kGridCell)));
                hi[a]=std::min(int(kGridDims[a])-1, int(std::floor((p+r)/kGridCell)));
                if(lo[a]>hi[a]) inside=false;
            }
            if(!inside) continue;
            for(int z=lo[2];z<=hi[2];++z) for(int y=lo[1];y<=hi[1];++y) for(int x=lo[0];x<=hi[0];++x) {
                // The light's sphere of influence against the cell's box.
                const int c[3]={x, y, z};
                double d2=0;
                for(int a=0;a<3;++a) {
                    const double p=double(l.position[a])-corner[a], min=c[a]*kGridCell, max=min+kGridCell;
                    const double d=p<min ? min-p : (p>max ? p-max : 0.0);
                    d2+=d*d;
                }
                if(d2>r*r) continue;
                const size_t cell=(size_t(z)*dy+size_t(y))*dx+size_t(x);
                if(pass==0) ++gridCells_[cell*2+1];
                else gridIndices_[gridCells_[cell*2]+gridFill_[cell]++]=uint32_t(i);
            }
        }
        if(pass==0) {
            uint32_t total=0, most=0, lit=0;
            for(size_t c=0;c<cellCount;++c) {
                gridCells_[c*2]=total; total+=gridCells_[c*2+1];
                most=std::max(most, gridCells_[c*2+1]); lit+=gridCells_[c*2+1] ? 1u : 0u;
            }
            gridIndices_.assign(total, 0);
            std::lock_guard<std::mutex> s(statsMutex_);
            stats_.gridCellsLit=lit; stats_.gridEntries=total; stats_.gridMostInCell=most; ++stats_.gridBuilds;
        }
    }
}

void GpuScene::clear(DescriptorHeap* heap, DeferredRelease& release, uint64_t retireValue) {
    {
        // The worker may still hold a mesh: let it finish, and drop what it has not started.
        std::unique_lock<std::mutex> lock(workMutex_);
        work_.clear();
        idleSignal_.wait_for(lock, std::chrono::seconds(10), [this]{ return converting_==nullptr; });
        if(converting_) {
            // A mesh that never finishes converting keeps its object for the rest of the process.
            for(auto& m:meshes_) if(m.get()==converting_) (void)m.release();
        }
        done_.clear();
    }
    for(auto& m:meshes_) {
        if(!m || !heap) continue;
        heap->release(m->positionsSrv); heap->release(m->uvsSrv); heap->release(m->colorsSrv); heap->release(m->indicesSrv);
        heap->release(m->skinUav);
        if(m->skinPositions) release.add(retireValue, m->skinPositions.resource.Get());
    }
    meshes_.clear(); pending_.clear();
    ommQueue_.clear(); tilesSettled_.clear(); alphaTilesSettled_.clear(); materialSignatures_.clear(); combinedLast_=true; ommInputBytes_=0; ommBudgetLogged_=false;

    geometryArena.release_all(release, retireValue);
    accelerationArena.release_all(release, retireValue);
    for(Structured* s:{&meshBuffer_, &geometryBuffer_, &materialBuffer_, &indexListBuffer_, &instanceBuffer_, &lightBuffer_}) {
        if(s->buffer) release.add(retireValue, s->buffer.resource.Get());
        if(heap) heap->release(s->srv);
        *s=Structured{};
    }
    for(Buffer* b:{&scratch_, &tlasScratch_, &tlasBuffer_, &ommScratch_, &skinScratch_}) { if(*b) release.add(retireValue, b->resource.Get()); *b=Buffer{}; }
    meshRecords_.clear(); geometryRecords_.clear();
    materialsDirty_=indicesDirty_=meshRecordsDirty_=true;
    hasTlas=false; tlas=0;
    std::lock_guard<std::mutex> s(statsMutex_);
    stats_=SceneStats{};
}

SceneStats GpuScene::stats() const { std::lock_guard<std::mutex> s(statsMutex_); return stats_; }
} // namespace tgpt
