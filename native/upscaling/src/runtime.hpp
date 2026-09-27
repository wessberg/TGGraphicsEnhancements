// SPDX-License-Identifier: MIT
#pragma once
#include "tgb/d3d_interop.hpp"
#include "tgup_api.h"
#include "tgup_policy.hpp"
#include <memory>
#include <string>
#include <vector>

namespace tgup {
using Microsoft::WRL::ComPtr;
using tgb::DxError;
using tgb::check;
using tgb::hexhr;

tgb::Host& host();
void log(const std::string&);
// For conditions that can repeat every frame: an identical message at most every 5 s.
void log_throttled(const std::string&);
std::wstring native_root();

// One frame's inputs as the SDKs see them: shared textures in D3D12 COMMON state. Depth and motion
// vectors are at render resolution, exactly the render size; the output is exactly the output size.
struct Inputs {
    ID3D12Resource* color=nullptr;
    ID3D12Resource* depth=nullptr;
    ID3D12Resource* motion=nullptr;
    ID3D12Resource* output=nullptr;
    // Ray Reconstruction's guides (the render size): diffuse albedo, specular albedo, normals with roughness.
    ID3D12Resource* diffuseAlbedo=nullptr;
    ID3D12Resource* specularAlbedo=nullptr;
    ID3D12Resource* normalRoughness=nullptr;
    // Its optional inputs: the traced reflections' hit distance, and the color before transparency (null without).
    ID3D12Resource* specularHitDistance=nullptr;
    ID3D12Resource* colorBeforeTransparency=nullptr;
    const TGUP_Frame* frame=nullptr;
    bool reset=false;
};

// An upscaling SDK on the bridge's D3D12 device. All calls come from the render thread.
class Backend {
public:
    virtual ~Backend()=default;
    virtual const char* name() const=0;
    // The context has to be created (again) for this frame; the caller idles the GPU first.
    virtual bool stale(const TGUP_Frame& frame) const=0;
    virtual bool prepare(const TGUP_Frame& frame, std::string& error)=0;
    // Records the upscale into the open list. false: the SDK refused the frame.
    virtual bool record(ID3D12GraphicsCommandList* list, const Inputs& in, std::string& error)=0;
    // The list record() wrote into never runs (the SDK refused the frame, or closing the list failed).
    virtual void discarded() {}
    virtual void describe(TGUP_Status&) const {}
};

// AMD FSR upscaling through the FSR API (FidelityFX SDK 2.x): FSR 4 on Radeon RX 9000, FSR 3.1 elsewhere.
bool load_fsr_runtime(const std::wstring& folder);
void probe_fsr(ID3D12Device* device);
bool fsr_available();
size_t fsr_version_count();
std::string fsr_version_name(size_t index);
std::unique_ptr<Backend> create_fsr_backend(ID3D12Device* device);
// FSR's quality modes and their ratios as AMD's runtime reports them, after probe_fsr.
std::vector<TGUP_Mode> fsr_modes();

// Intel XeSS Super Resolution (XeSS SDK 3.x): XMX on Intel Arc, DP4a on other GPUs with Shader Model 6.4.
bool load_xess_runtime(const std::wstring& folder);
void probe_xess(ID3D12Device* device);
bool xess_available();
uint32_t xess_version();
std::unique_ptr<Backend> create_xess_backend(ID3D12Device* device);
// XeSS's quality presets and their ratios as Intel's runtime reports them, after probe_xess.
std::vector<TGUP_Mode> xess_modes();

// NVIDIA DLSS Ray Reconstruction (NGX, the shared D3D12 device, Native/DLSS/nvngx_dlssd.dll): denoise and
// upscale of a path-traced frame, with TG Path Tracing's guides. Probed once per device (any thread).
bool rr_probe(ID3D12Device* device);
bool rr_available();
std::string rr_unavailable_reason();
std::unique_ptr<Backend> create_rr_backend(ID3D12Device* device);

// NVIDIA DLSS Super Resolution for the game's own DLSS: the NVIDIA-signed nvngx_dlss.dll in
// Native/DLSS replaces an older one wherever NVIDIA's runtime loads it from. false: not bundled, not
// signed, or the hook failed; the game keeps its own.
bool install_dlss_dll(const std::wstring& folder);
// The DLL NVIDIA's runtime last loaded for the game's DLSS (TGUP_DLSS_*), with the versions involved.
uint32_t dlss_dll(uint64_t& loaded, uint64_t& requested, uint64_t& bundled);
// The DLSS overrides in the NVIDIA driver's profile for the game, as last read.
void dlss_override(TGUP_DlssOverride& out);

// The render-thread side of TG Better Upscaling: submitted frames, the D3D11/D3D12 hand-off and the status.
void upscaler_device_ready(ID3D12Device* device, uint32_t vendor);
int32_t upscaler_submit(const TGUP_Frame& frame);
int32_t upscaler_set_output(uint32_t sequence, uint64_t output);
int32_t upscaler_read_status(TGUP_Status& out);
void* upscaler_render_event();
} // namespace tgup
