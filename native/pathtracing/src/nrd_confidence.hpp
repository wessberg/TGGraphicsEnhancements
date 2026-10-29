// SPDX-License-Identifier: MIT
// Private resources for PT-127's optional direct-light confidence pass. No public ABI.
#pragma once
#include "gpu.hpp"

namespace tgpt {
struct NrdConfidenceResources {
    // Eleven 16-byte rows, exactly ConfidenceSample in pt_nrd_confidence.hlsl.
    struct SampleLayout { uint32_t rows[11][4]; };
    static_assert(sizeof(SampleLayout)==176, "NRD confidence sample stride");
    Buffer samples;
    uint32_t samplesUav=kInvalid;
    Texture gradient[2], position, normal, confidence;
    uint32_t frame=0, scene=0;
    bool havePrevious=false;

    void release(DescriptorHeap& heap, DeferredRelease& retired, uint64_t fence) {
        for(Texture* t:{&gradient[0], &gradient[1], &position, &normal, &confidence}) {
            retired.add(fence, t->resource.Get()); heap.release(t->srv); heap.release(t->uav); *t={};
        }
        retired.add(fence, samples.resource.Get()); heap.release(samplesUav);
        samples={}; samplesUav=kInvalid; havePrevious=false;
    }
    void ensure(ID3D12Device* device, DescriptorHeap& heap, uint32_t width, uint32_t height) {
        if(confidence) return;
        const uint32_t w=(width+4u)/5u, h=(height+4u)/5u;
        samples=make_buffer(device, uint64_t(w)*h*sizeof(SampleLayout), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create NRD confidence samples");
        samplesUav=structured_uav(device, heap, samples.resource.Get(), w*h, sizeof(SampleLayout));
        for(auto& t:gradient) t=make_texture(device, heap, w, h, DXGI_FORMAT_R32G32_FLOAT, "Create NRD lighting gradient");
        position=make_texture(device, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create NRD confidence positions");
        normal=make_texture(device, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create NRD confidence normals");
        confidence=make_texture(device, heap, width, height, DXGI_FORMAT_R16_FLOAT, "Create NRD lighting confidence");
        havePrevious=false;
    }
    uint64_t memory_bytes() const {
        uint64_t bytes=allocation_bytes(samples.resource.Get());
        for(const Texture* t:{&gradient[0], &gradient[1], &position, &normal, &confidence}) bytes+=allocation_bytes(t->resource.Get());
        return bytes;
    }
};
}
