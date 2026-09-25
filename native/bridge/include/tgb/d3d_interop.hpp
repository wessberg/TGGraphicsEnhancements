// SPDX-License-Identifier: MIT
// Header-only D3D11-to-D3D12 interop helpers for bridge extensions: shareable textures created on the
// game's D3D11 device and opened on the bridge's D3D12 device, a shared fence pair, and an isolated
// D3D11 pipeline state for work recorded on the game's immediate context. Resources are NT-handle
// shared with explicit fences, never keyed mutexes.
#pragma once
#include "bridge_api.hpp"
#include <wrl/client.h>
#include <stdexcept>
#include <string>
#include <sstream>
#include <utility>
#include <mutex>
#include <vector>

namespace tgb {
using Microsoft::WRL::ComPtr;

struct DxError : std::runtime_error {
    HRESULT hr;
    DxError(HRESULT value, const char* what) : std::runtime_error(what), hr(value) {}
    DxError(HRESULT value, const std::string& what) : std::runtime_error(what), hr(value) {}
};
inline void check(HRESULT hr, const char* what) { if(FAILED(hr)) throw DxError(hr, what); }
inline std::string hexhr(HRESULT hr) { std::ostringstream s; s<<"0x"<<std::hex<<std::uppercase<<uint32_t(hr); return s.str(); }

struct Handle {
    HANDLE value=nullptr;
    ~Handle() { if(value) CloseHandle(value); }
    Handle()=default; Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};

// GPU objects that submitted work may still use after a drain that could not be confirmed (a hung GPU,
// a five-second fence timeout). Freeing them would turn a stall into device removal or a crash, so they
// are kept for the rest of the process instead: a small leak on an already broken session.
inline void quarantine(IUnknown* object) {
    if(!object) return;
    static std::mutex lock;
    static auto* kept=new std::vector<ComPtr<IUnknown>>; // never destroyed: outlives every GPU object it holds
    std::lock_guard<std::mutex> guard(lock);
    kept->emplace_back(object);
}
inline bool same_object(IUnknown* a, IUnknown* b) {
    ComPtr<IUnknown> x, y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) &&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get()==y.Get();
}

// A texture created on the D3D11 device and opened on the D3D12 device. D3D12 sees it in COMMON
// state; every D3D12 user returns it there before D3D11 touches it again.
struct SharedTexture {
    ComPtr<ID3D11Texture2D> d11;
    ComPtr<ID3D12Resource> d12;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11RenderTargetView> rtv;
    UINT width=0, height=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    static SharedTexture make(ID3D11Device* d11, ID3D12Device* d12, UINT w, UINT h, DXGI_FORMAT f, UINT bind) {
        SharedTexture t; t.width=w; t.height=h; t.format=f;
        D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.Format=f;
        desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=bind;
        desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        check(d11->CreateTexture2D(&desc, nullptr, &t.d11), "Create D3D11 shared texture");
        ComPtr<IDXGIResource1> dxgi; check(t.d11.As(&dxgi), "Query shared texture IDXGIResource1");
        Handle shared;
        check(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE, nullptr, &shared.value), "Create texture NT handle");
        check(d12->OpenSharedHandle(shared.value, IID_PPV_ARGS(&t.d12)), "Open D3D11 texture on D3D12");
        const auto actual=t.d12->GetDesc();
        if(actual.Width!=w || actual.Height!=h || actual.Format!=f)
            throw DxError(E_UNEXPECTED, "An injector changed an interop texture descriptor");
        if(bind&D3D11_BIND_SHADER_RESOURCE) check(d11->CreateShaderResourceView(t.d11.Get(), nullptr, &t.srv), "Create shared SRV");
        if(bind&D3D11_BIND_UNORDERED_ACCESS) check(d11->CreateUnorderedAccessView(t.d11.Get(), nullptr, &t.uav), "Create shared UAV");
        if(bind&D3D11_BIND_RENDER_TARGET) check(d11->CreateRenderTargetView(t.d11.Get(), nullptr, &t.rtv), "Create shared RTV");
        return t;
    }
    bool matches(UINT w, UINT h, DXGI_FORMAT f) const { return d11 && width==w && height==h && format==f; }
};

// Records on the game's immediate context inside a private pipeline state and puts the game's
// state back afterwards, releasing every binding the scratch state would otherwise keep alive.
struct ContextScope {
    ID3D11DeviceContext4* context; ComPtr<ID3DDeviceContextState> game;
    ContextScope(ID3D11DeviceContext4* c, ID3DDeviceContextState* scratch) : context(c) {
        context->SwapDeviceContextState(scratch, &game);
    }
    ~ContextScope() {
        ID3D11ShaderResourceView* emptySRV[3]={nullptr,nullptr,nullptr}; ID3D11UnorderedAccessView* emptyUAV=nullptr;
        context->CSSetShaderResources(0, 3, emptySRV); context->PSSetShaderResources(0, 1, emptySRV);
        context->CSSetUnorderedAccessViews(0, 1, &emptyUAV, nullptr);
        ID3D11Buffer* emptyBuffer=nullptr; ID3D11SamplerState* emptySampler=nullptr;
        context->CSSetConstantBuffers(0, 1, &emptyBuffer); context->PSSetConstantBuffers(0, 1, &emptyBuffer);
        context->PSSetConstantBuffers(13, 1, &emptyBuffer);
        context->CSSetSamplers(0, 1, &emptySampler); context->PSSetSamplers(0, 1, &emptySampler);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->SwapDeviceContextState(game.Get(), nullptr);
    }
    ContextScope(const ContextScope&)=delete; ContextScope& operator=(const ContextScope&)=delete;
};

// What a game texture is read as. Unity creates some render textures typeless on D3D11 and depth
// buffers as depth-stencil formats; the typed view is the one its shaders use.
enum class ReadAs { Color, Depth, Motion };
inline DXGI_FORMAT typed_read_format(DXGI_FORMAT f, ReadAs as) {
    switch(as) {
    case ReadAs::Depth:
        switch(f) {
        case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_FLOAT: return f;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    case ReadAs::Motion:
        switch(f) {
        case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R8G8_SNORM: return f;
        // HDRP and URP motion vectors are always floating point (R16G16_SFloat).
        case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32G32_TYPELESS: return DXGI_FORMAT_R32G32_FLOAT;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    case ReadAs::Color:
        switch(f) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R11G11B10_FLOAT:
        case DXGI_FORMAT_R32G32B32_FLOAT: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R9G9B9E5_SHAREDEXP: return f;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_TYPELESS: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }
    return DXGI_FORMAT_UNKNOWN;
}
// The typed format a compute shader can write to (a UAV); unknown when the format has none.
inline DXGI_FORMAT typed_write_format(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case DXGI_FORMAT_R11G11B10_FLOAT: return DXGI_FORMAT_R11G11B10_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_UNORM: return DXGI_FORMAT_R16G16B16A16_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
// A single-sample, single-mip, shader-readable 2D texture read through its typed view.
inline ComPtr<ID3D11ShaderResourceView> typed_srv(ID3D11Device* device, ID3D11Resource* resource, ReadAs as, D3D11_TEXTURE2D_DESC* out=nullptr) {
    ComPtr<ID3D11Texture2D> texture; check(resource->QueryInterface(IID_PPV_ARGS(&texture)), "Input is not a 2D texture");
    D3D11_TEXTURE2D_DESC t{}; texture->GetDesc(&t);
    if(out) *out=t;
    if(t.ArraySize!=1 || t.SampleDesc.Count!=1 || !(t.BindFlags&D3D11_BIND_SHADER_RESOURCE))
        throw DxError(DXGI_ERROR_UNSUPPORTED, "Input must be a single-sample, shader-readable 2D texture");
    const auto format=typed_read_format(t.Format, as);
    if(format==DXGI_FORMAT_UNKNOWN)
        throw DxError(DXGI_ERROR_UNSUPPORTED, "Unsupported input format "+std::to_string(uint32_t(t.Format)));
    D3D11_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format=format; srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; srv.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> view; check(device->CreateShaderResourceView(resource, &srv, &view), "Create input SRV"); return view;
}

// A D3D12 fence shared with D3D11: work on either API waits for the other on the GPU through one
// timeline, D3D11 Signal(produced) -> D3D12 Wait -> work -> D3D12 Signal(retired) -> D3D11 Wait.
// A larger value never bypasses work associated with a smaller one.
struct SharedFence {
    ComPtr<ID3D12Fence> d12;
    ComPtr<ID3D11Fence> d11;
    Handle event;
    uint64_t value=0;
    void create(ID3D12Device* device12, ID3D11Device5* device11) {
        check(device12->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&d12)), "Create cross-API fence");
        Handle handle; check(device12->CreateSharedHandle(d12.Get(), nullptr, GENERIC_ALL, nullptr, &handle.value), "Export cross-API fence");
        check(device11->OpenSharedFence(handle.value, IID_PPV_ARGS(&d11)), "Open cross-API fence on D3D11");
        event.value=CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if(!event.value) throw DxError(HRESULT_FROM_WIN32(GetLastError()), "Create fence notification event");
    }
    bool ready() const { return d12 && d11 && event.value; }
    void reset() {
        d12.Reset(); d11.Reset(); value=0;
        if(event.value) { CloseHandle(event.value); event.value=nullptr; }
    }
    uint64_t next() { return ++value; }
    // CPU wait for a value the D3D12 queue signalled; five seconds is a hang, never a normal frame.
    // A removed device signals its fences to UINT64_MAX, which also wakes the wait: checked again after it.
    void wait_cpu(uint64_t target, DWORD timeoutMs=5000) {
        if(!target) return;
        auto completed=d12->GetCompletedValue();
        if(completed==UINT64_MAX) throw DxError(DXGI_ERROR_DEVICE_REMOVED, "Shared fence device removed");
        if(completed>=target) return;
        check(d12->SetEventOnCompletion(target, event.value), "Schedule shared fence CPU notification");
        if(WaitForSingleObject(event.value, timeoutMs)!=WAIT_OBJECT_0) throw DxError(DXGI_ERROR_DEVICE_HUNG, "Interop fence timeout");
        completed=d12->GetCompletedValue();
        if(completed==UINT64_MAX) throw DxError(DXGI_ERROR_DEVICE_REMOVED, "Shared fence device removed");
        if(completed<target) throw DxError(DXGI_ERROR_DEVICE_HUNG, "Interop fence woke before its value");
    }
};
} // namespace tgb
