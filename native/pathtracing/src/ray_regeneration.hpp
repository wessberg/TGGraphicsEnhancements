// SPDX-License-Identifier: MIT
// AMD FSR Ray Regeneration (the FidelityFX SDK's denoiser) for the traced light, on the tracer's own D3D12
// command list: AMD's signed loader and denoiser DLLs from Native/FidelityFX. It runs on a Radeon RX 9000
// or newer only; elsewhere the probe says so and the built-in denoiser serves.
#pragma once
#include "runtime.hpp"

namespace tgpt {
// Loads the runtime and asks it whether this device runs the denoiser (AMD GPUs only); logs either way.
bool rrg_probe(ID3D12Device* device, uint32_t vendor);
bool rrg_available();

// One frame's inputs, in the SDK's conventions. The signals are denoised in place.
struct RrgFrame {
    uint32_t width=0, height=0, frameIndex=0;
    bool reset=false;
    ID3D12Resource *linearDepth=nullptr, *motion=nullptr, *normals=nullptr, *diffuseAlbedo=nullptr, *specularAlbedo=nullptr;
    ID3D12Resource *indirectDiffuse=nullptr, *directDiffuse=nullptr, *indirectSpecular=nullptr;
    float view[16]{}, projection[16]{};  // storage row-major with row vectors, which Unity's memory order is as it stands
    float jitter[2]{}, cameraDelta[3]{}; // render pixels; previous camera position minus this one's
};

class RayRegeneration {
public:
    ~RayRegeneration() { destroy(); }
    // Whether a frame of this size needs a new context: the caller then makes sure the GPU is done with the
    // old one's frames before ensure() replaces it.
    bool needs_context(uint32_t width, uint32_t height) const { return !context_ || width!=width_ || height!=height_; }
    bool ensure(ID3D12Device* device, uint32_t width, uint32_t height, std::string& error);
    bool dispatch(ID3D12GraphicsCommandList* list, const RrgFrame& f, std::string& error);
    void destroy();
private:
    void* context_=nullptr;
    uint32_t width_=0, height_=0;
};
} // namespace tgpt
