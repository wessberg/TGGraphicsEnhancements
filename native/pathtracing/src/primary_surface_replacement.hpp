// SPDX-License-Identifier: MIT
#pragma once
#include "gpu.hpp"
namespace tgpt {
struct PrimarySurfaceReplacementResources {
    // pt_psr_record.hlsli's PsrRecord: the virtual surface's motion and depth, then three rows of packed words (its
    // light, its normal and state, its material).
    struct RecordLayout { float motionDepth[4]; uint32_t light[4], surface[4], material[4]; };
    static_assert(sizeof(RecordLayout)==64, "PsrRecord shader stride");
    Buffer records;
    uint32_t uav=kInvalid;
    void release(DescriptorHeap& heap, DeferredRelease& retired, uint64_t fence) {
        retired.add(fence, records.resource.Get()); heap.release(uav);
        records={}; uav=kInvalid;
    }
    void ensure(ID3D12Device* device, DescriptorHeap& heap, uint32_t width, uint32_t height) {
        if(records.resource) return;
        records=make_buffer(device, uint64_t(width)*height*sizeof(RecordLayout), D3D12_HEAP_TYPE_DEFAULT,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create the mirrors' records for NRD");
        uav=structured_uav(device, heap, records.resource.Get(), width*height, sizeof(RecordLayout));
    }
    uint64_t memory_bytes() const { return allocation_bytes(records.resource.Get()); }
};
}
