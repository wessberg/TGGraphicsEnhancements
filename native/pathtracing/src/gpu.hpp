// SPDX-License-Identifier: MIT
// Small D3D12 helpers for the path tracer: buffers and textures with tracked states, one shader-visible
// descriptor heap every pass indexes directly (Shader Model 6.6), a per-frame upload ring, and a queue
// of objects released once the GPU has finished the frame that used them.
#pragma once
#include "tgb/d3d_interop.hpp"
#include <algorithm>
#include <deque>
#include <vector>

namespace tgpt {
using Microsoft::WRL::ComPtr;
using tgb::check;
using tgb::DxError;

constexpr uint32_t kInvalid=0xFFFFFFFFu;

inline D3D12_HEAP_PROPERTIES heap_props(D3D12_HEAP_TYPE type) { D3D12_HEAP_PROPERTIES p{}; p.Type=type; return p; }
inline D3D12_RESOURCE_DESC buffer_desc(uint64_t size, D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=std::max<uint64_t>(size, 256);
    d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=flags;
    return d;
}

inline uint64_t allocation_bytes(ID3D12Resource* resource) {
    if(!resource) return 0;
    ComPtr<ID3D12Device> device; if(FAILED(resource->GetDevice(IID_PPV_ARGS(&device)))) return 0;
    const auto desc=resource->GetDesc(); const auto bytes=device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
    return bytes==UINT64_MAX ? 0 : bytes;
}
struct Buffer {
    ComPtr<ID3D12Resource> resource;
    uint64_t size=0;
    D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
    void* mapped=nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS va() const { return resource ? resource->GetGPUVirtualAddress() : 0; }
    explicit operator bool() const { return resource!=nullptr; }
};
inline Buffer make_buffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const char* what) {
    Buffer b; b.size=std::max<uint64_t>(size, 256); b.state=state;
    const auto props=heap_props(heap); const auto desc=buffer_desc(b.size, flags);
    // A buffer in a default heap starts in COMMON whatever it asks for (the debug layer says so, message 1328), and its
    // first use promotes it; upload and readback heaps take their one state. Acceleration structures are the exception:
    // they must be created in their own state and never leave it (created in COMMON, the debug layer reported thousands
    // of errors a run, the TLAS's root SRV among them).
    const D3D12_RESOURCE_STATES initial=heap==D3D12_HEAP_TYPE_DEFAULT && state!=D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
        ? D3D12_RESOURCE_STATE_COMMON : state;
    check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, initial, nullptr, IID_PPV_ARGS(&b.resource)), what);
    if(heap==D3D12_HEAP_TYPE_UPLOAD || heap==D3D12_HEAP_TYPE_READBACK) {
        D3D12_RANGE none{0, 0};
        check(b.resource->Map(0, heap==D3D12_HEAP_TYPE_READBACK ? nullptr : &none, &b.mapped), what);
    }
    return b;
}

struct Texture {
    ComPtr<ID3D12Resource> resource;
    uint32_t width=0, height=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
    uint32_t srv=kInvalid, uav=kInvalid;
    explicit operator bool() const { return resource!=nullptr; }
};

// One CPU/GPU descriptor heap; slots are handed out and back through a free list. A released slot may still
// be read by frames the GPU has not finished: it returns to the free list once the consumer fence passes the
// value set_retire named when it was released (the frame being recorded then), in collect.
// Every slot's state is kept (since 2026-10-11, PT-118: the fog grid read a texture another owner's view had replaced): a
// slot given back twice would sit in the free list twice and go to two owners, the later view replacing the earlier. A
// second give-back, and a view written into a slot that is not handed out, are refused or noted with the line that did
// it (take_misuse, which the tracer logs).
class DescriptorHeap {
public:
    struct Misuse { uint32_t index; uint8_t state; bool write; const char* file; int line; const char* gaveBackFile; int gaveBackLine; };
private:
    ComPtr<ID3D12DescriptorHeap> heap_;
    uint32_t capacity_=0, increment_=0, next_=0;
    std::vector<uint32_t> free_;
    std::deque<std::pair<uint64_t, uint32_t>> retiring_;
    uint64_t retireAt_=0;
    std::vector<uint8_t> state_;                               // 0 free or never used, 1 handed out, 2 given back and waiting
    std::vector<std::pair<const char*, int>> gaveBack_;        // where each slot was last given back
    mutable std::vector<Misuse> misuse_; mutable uint64_t misuseCount_=0;
    void note(uint32_t index, bool write, const char* file, int line) const {
        ++misuseCount_;
        if(misuse_.size()<32) misuse_.push_back({index, index<capacity_ ? state_[index] : uint8_t(255), write, file, line,
                                                 index<capacity_ ? gaveBack_[index].first : nullptr, index<capacity_ ? gaveBack_[index].second : 0});
    }
public:
    void create(ID3D12Device* device, uint32_t capacity) {
        D3D12_DESCRIPTOR_HEAP_DESC d{}; d.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; d.NumDescriptors=capacity;
        d.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&heap_)), "Create path tracing descriptor heap");
        capacity_=capacity; increment_=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV); next_=0; free_.clear(); retiring_.clear();
        state_.assign(capacity, 0); gaveBack_.assign(capacity, {nullptr, 0}); misuse_.clear(); misuseCount_=0;
    }
    ID3D12DescriptorHeap* get() const { return heap_.Get(); }
    uint32_t allocate() {
        if(!free_.empty()) { const uint32_t i=free_.back(); free_.pop_back(); state_[i]=1; return i; }
        if(next_>=capacity_) throw DxError(E_OUTOFMEMORY, "The path tracer ran out of descriptors");
        state_[next_]=1; return next_++;
    }
    // What went wrong since the last call (32 at most, and how many in all), for the log.
    std::vector<Misuse> take_misuse(uint64_t* total=nullptr) { if(total) *total=misuseCount_; std::vector<Misuse> m; m.swap(misuse_); return m; }
    // count slots in a row, for good (the heap's whole life): only right after create, before anything comes back.
    uint32_t allocate_range(uint32_t count) {
        if(count>capacity_-next_) throw DxError(E_OUTOFMEMORY, "The path tracer ran out of descriptors");
        const uint32_t first=next_; next_+=count; std::fill(state_.begin()+first, state_.begin()+first+count, uint8_t(1)); return first;
    }
    // The fence value after which the GPU has finished every frame recorded so far (never decreases).
    void set_retire(uint64_t value) { retireAt_=std::max(retireAt_, value); }
    // (file, line: the caller's, by the compiler.) A slot that is not handed out is not given back again: noted, refused.
    void release(uint32_t index, const char* file=__builtin_FILE(), int line=__builtin_LINE()) {
        if(index==kInvalid) return;
        if(index>=capacity_ || state_[index]!=1) { note(index, false, file, line); return; }
        state_[index]=2; gaveBack_[index]={file, line};
        retiring_.emplace_back(retireAt_, index);
    }
    void collect(uint64_t completed) {
        while(!retiring_.empty() && retiring_.front().first<=completed) { const uint32_t i=retiring_.front().second; state_[i]=0; free_.push_back(i); retiring_.pop_front(); }
    }
    // Where a view is written. A slot that is not handed out is noted (the view is still written: its reader may be alive).
    D3D12_CPU_DESCRIPTOR_HANDLE cpu(uint32_t index, const char* file=__builtin_FILE(), int line=__builtin_LINE()) const {
        if(index>=capacity_ || state_[index]!=1) note(index, true, file, line);
        auto h=heap_->GetCPUDescriptorHandleForHeapStart(); h.ptr+=size_t(index)*increment_; return h;
    }
    void reset() { heap_.Reset(); capacity_=next_=0; free_.clear(); retiring_.clear(); retireAt_=0; state_.clear(); gaveBack_.clear(); misuse_.clear(); misuseCount_=0; }
};

// Views written into the shared heap. Raw buffers are read as ByteAddressBuffer.
// at: where the view starts in the buffer, in bytes (a multiple of 16: D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT).
inline uint32_t raw_srv(ID3D12Device* device, DescriptorHeap& heap, ID3D12Resource* buffer, uint64_t bytes, uint64_t at=0) {
    const uint32_t i=heap.allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=DXGI_FORMAT_R32_TYPELESS; d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Buffer.FirstElement=at/4; d.Buffer.NumElements=UINT(std::max<uint64_t>(bytes/4, 1));
    d.Buffer.Flags=D3D12_BUFFER_SRV_FLAG_RAW;
    device->CreateShaderResourceView(buffer, &d, heap.cpu(i)); return i;
}
inline uint32_t raw_uav(ID3D12Device* device, DescriptorHeap& heap, ID3D12Resource* buffer, uint64_t bytes) {
    const uint32_t i=heap.allocate();
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{}; d.Format=DXGI_FORMAT_R32_TYPELESS; d.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    d.Buffer.NumElements=UINT(std::max<uint64_t>(bytes/4, 1)); d.Buffer.Flags=D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(buffer, nullptr, &d, heap.cpu(i)); return i;
}
inline uint32_t structured_srv(ID3D12Device* device, DescriptorHeap& heap, ID3D12Resource* buffer, uint32_t count, uint32_t stride) {
    const uint32_t i=heap.allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=DXGI_FORMAT_UNKNOWN; d.ViewDimension=D3D12_SRV_DIMENSION_BUFFER;
    d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Buffer.NumElements=std::max<uint32_t>(count, 1); d.Buffer.StructureByteStride=stride;
    device->CreateShaderResourceView(buffer, &d, heap.cpu(i)); return i;
}
inline uint32_t structured_uav(ID3D12Device* device, DescriptorHeap& heap, ID3D12Resource* buffer, uint32_t count, uint32_t stride) {
    const uint32_t i=heap.allocate();
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{}; d.Format=DXGI_FORMAT_UNKNOWN; d.ViewDimension=D3D12_UAV_DIMENSION_BUFFER;
    d.Buffer.NumElements=std::max<uint32_t>(count, 1); d.Buffer.StructureByteStride=stride;
    device->CreateUnorderedAccessView(buffer, nullptr, &d, heap.cpu(i)); return i;
}
inline uint32_t texture_srv(ID3D12Device* device, DescriptorHeap& heap, ID3D12Resource* texture, DXGI_FORMAT format) {
    const uint32_t i=heap.allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=format; d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; d.Texture2D.MipLevels=1;
    device->CreateShaderResourceView(texture, &d, heap.cpu(i)); return i;
}
inline uint32_t texture_uav(ID3D12Device* device, DescriptorHeap& heap, ID3D12Resource* texture, DXGI_FORMAT format) {
    const uint32_t i=heap.allocate();
    D3D12_UNORDERED_ACCESS_VIEW_DESC d{}; d.Format=format; d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(texture, nullptr, &d, heap.cpu(i)); return i;
}
inline Texture make_texture(ID3D12Device* device, DescriptorHeap& heap, uint32_t w, uint32_t h, DXGI_FORMAT format, const char* what) {
    Texture t; t.width=w; t.height=h; t.format=format; t.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=w; d.Height=h; d.DepthOrArraySize=1; d.MipLevels=1;
    d.Format=format; d.SampleDesc.Count=1; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto props=heap_props(D3D12_HEAP_TYPE_DEFAULT);
    check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &d, t.state, nullptr, IID_PPV_ARGS(&t.resource)), what);
    t.srv=texture_srv(device, heap, t.resource.Get(), format); t.uav=texture_uav(device, heap, t.resource.Get(), format);
    return t;
}

// Batches resource barriers; flushed before each dispatch or copy that depends on them.
class Barriers {
    std::vector<D3D12_RESOURCE_BARRIER> list_;
public:
    void transition(ID3D12Resource* r, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES to) {
        if(!r || state==to) return;
        D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=r;
        b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore=state; b.Transition.StateAfter=to;
        list_.push_back(b); state=to;
    }
    // A resource whose state is known at this point without being tracked (the shared textures).
    void transition_from(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) { D3D12_RESOURCE_STATES s=from; transition(r, s, to); }
    void transition(Buffer& b, D3D12_RESOURCE_STATES to) { transition(b.resource.Get(), b.state, to); }
    void transition(Texture& t, D3D12_RESOURCE_STATES to) { transition(t.resource.Get(), t.state, to); }
    void uav(ID3D12Resource* r=nullptr) { D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; b.UAV.pResource=r; list_.push_back(b); }
    void flush(ID3D12GraphicsCommandList* list) { if(!list_.empty()) { list->ResourceBarrier(UINT(list_.size()), list_.data()); list_.clear(); } }
};

// A linear upload allocator per frame slot; the slot's memory is reused once the GPU finished that frame.
class UploadRing {
    Buffer buffer_; uint64_t offset_=0;
    // PT-87 (docs/agents/memory-2.md, memory3): the most any frame has taken of it since it was made, and how often a
    // frame found it full. For the memory line: Windows counts the ring against the card's budget, so its size matters.
    uint64_t peak_=0, full_=0;
public:
    void create(ID3D12Device* device, uint64_t size) { buffer_=make_buffer(device, size, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create path tracing upload ring"); offset_=0; peak_=full_=0; }
    void reset() { offset_=0; }
    bool valid() const { return bool(buffer_); }
    uint64_t capacity() const { return buffer_.size; }
    uint64_t peak() const { return peak_; }
    uint64_t found_full() const { return full_; }
    // Returns false when the slot is full; the caller then uploads through a buffer of its own.
    bool allocate(uint64_t bytes, uint64_t alignment, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu, uint64_t* offset=nullptr) {
        const uint64_t start=(offset_+alignment-1)/alignment*alignment;
        if(start+bytes>buffer_.size) { ++full_; return false; }
        *cpu=static_cast<uint8_t*>(buffer_.mapped)+start; *gpu=buffer_.va()+start; if(offset) *offset=start;
        offset_=start+bytes; peak_=std::max(peak_, offset_); return true;
    }
    ID3D12Resource* resource() const { return buffer_.resource.Get(); }
};

// Objects the GPU may still use until a consumer fence value completes.
class DeferredRelease {
    std::deque<std::pair<uint64_t, ComPtr<IUnknown>>> items_;
public:
    void add(uint64_t fence, IUnknown* object) { if(object) items_.emplace_back(fence, object); }
    void collect(uint64_t completed) { while(!items_.empty() && items_.front().first<=completed) items_.pop_front(); }
    void clear() { items_.clear(); }
    size_t size() const { return items_.size(); }
    uint64_t bytes() const { uint64_t n=0; for(const auto& i:items_) { ComPtr<ID3D12Resource> r; if(SUCCEEDED(i.second.As(&r))) n+=allocation_bytes(r.Get()); } return n; }
};
} // namespace tgpt
