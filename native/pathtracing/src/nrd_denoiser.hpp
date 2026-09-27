// SPDX-License-Identifier: MIT
// NVIDIA's Real-Time Denoisers (NRD; RELAX for the bounced light), the fallback for GPUs that run
// neither DLSS Ray Reconstruction nor AMD Ray Regeneration, as DOOM: The Dark Ages falls back to it. NRD (fetched
// beside the repository and compiled in, its shaders embedded as DXIL) describes each frame's compute dispatches;
// this runs them on the tracer's own command list with a root signature of NRD's layout (a root constant buffer, one
// table of textures and storage textures, static samplers), a descriptor heap of its own with a region per frame in
// flight, constants from the frame's upload ring, and NRD's texture pools. A build without the SDK has no NRD, and
// the built-in denoiser serves.
#pragma once
#include "runtime.hpp"
#include "gpu.hpp"

namespace tgpt {
// A texture NRD reads or writes, and where its state is kept (NRD's dispatches move it and put it back).
struct NrdTexture { ID3D12Resource* resource=nullptr; D3D12_RESOURCE_STATES* state=nullptr; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN; };

// One frame's inputs in NRD's conventions: matrices column-major with column vectors (Unity's memory order as it
// stands), world-to-view without jitter for this frame and the previous one in the same world space (the camera's
// move between them is the difference of their translations), and the jitter in render pixels.
struct NrdFrame {
    uint32_t width=0, height=0, frameIndex=0;
    bool reset=false;
    float worldToView[16]{}, worldToViewPrev[16]{}, viewToClip[16]{}, viewToClipPrev[16]{};
    float jitter[2]{}, jitterPrev[2]{};
    float denoisingRange=1e5f; // view depth beyond it is left alone (the sky's texels)
    NrdTexture viewZ, motion, normalRoughness, diffuse, diffuseOut;
};

// Creates NRD's instance, root signature and pipelines on the device (once per device); logs either way. False
// without NRD in this build.
bool nrd_probe(ID3D12Device* device);
bool nrd_available();
// The front end (pt_nrd_prep.hlsl): the tracer's surfaces and signals into NRD's inputs, on the tracer's root
// signature (made again when that is). Null without NRD.
ID3D12PipelineState* nrd_prep_for(ID3D12RootSignature* tracerRoot);
void nrd_shutdown();

class Nrd {
public:
    ~Nrd() { destroy(); }
    // Whether a frame of this size needs new texture pools: the caller makes sure the GPU is done with the old ones.
    bool needs_pools(uint32_t width, uint32_t height) const { return pools_.empty() || width!=width_ || height!=height_; }
    bool ensure(ID3D12Device* device, uint32_t width, uint32_t height, std::string& error);
    // Records NRD's dispatches for this frame (slot: the frame-in-flight index its descriptors and constants may use).
    // It binds its own descriptor heap and root signature: the caller binds its own again afterwards.
    bool dispatch(ID3D12GraphicsCommandList* list, uint32_t slot, UploadRing& ring, const NrdFrame& f, std::string& error);
    void destroy();
private:
    struct Pool { ComPtr<ID3D12Resource> resource; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN; D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS; };
    std::vector<Pool> pools_;
    uint32_t width_=0, height_=0;
    bool restart_=true; // new pools hold garbage: the first frame clears them
};
} // namespace tgpt
