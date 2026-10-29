#pragma once
// SPDX-License-Identifier: MIT
// Real D3D11/MinHook observer check. No game, D3D12 transport or particle-capture claim.
#include "../native/pathtracing/src/vfx_observer.hpp"
#include <MinHook.h>
#include <d3dcompiler.h>
#include <cstdio>
using namespace tgpt::vfx;
struct TestHost final : tgb::Host {
    ComPtr<ID3D11Device5> device; ComPtr<ID3D11DeviceContext4> context;
    uint32_t abi() const noexcept override { return 1; }
    void log(const char*,const char* m) noexcept override { std::puts(m); }
    bool add_hook(void* t,void* d,void** o) noexcept override { return MH_CreateHook(t,d,o)==MH_OK && MH_EnableHook(t)==MH_OK; }
    bool add_vtable_hook(void*,size_t,void*) noexcept override { return false; }
    void* vtable_original(void*,size_t) noexcept override { return nullptr; }
    bool set_internal(bool) noexcept override { return false; }
    bool internal() noexcept override { return false; }
    HRESULT create_native_factory(REFIID,void**) noexcept override { return E_NOTIMPL; }
    bool trusted_signer(const wchar_t*,const wchar_t*) noexcept override { return false; }
    HMODULE load_signed(const wchar_t*,const wchar_t*) noexcept override { return nullptr; }
    ID3D11Device5* game_device() noexcept override { return device.Get(); }
    ID3D11DeviceContext4* game_context() noexcept override { return context.Get(); }
    uint32_t game_device_generation() noexcept override { return 1; }
    ID3D12Device* d3d12_device() noexcept override { return nullptr; }
    uint32_t gpu_vendor() noexcept override { return 0; }
    const wchar_t* bepinex_root() noexcept override { return L"."; }
};
static void check(bool yes,const char* what) { if(!yes) throw std::runtime_error(what); std::printf("PASS: %s\n",what); }
static ComPtr<ID3DBlob> compile(const char* code,const char* target) {
    ComPtr<ID3DBlob> b,e; auto hr=D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"main",target,0,0,&b,&e);
    if(FAILED(hr)) throw std::runtime_error(e ? static_cast<const char*>(e->GetBufferPointer()):"compile"); return b;
}
struct Data { ComPtr<ID3D11Buffer> buffer; ComPtr<ID3D11ShaderResourceView> read; ComPtr<ID3D11UnorderedAccessView> write; };
static Data data(ID3D11Device* d) {
    Data r; D3D11_BUFFER_DESC desc{}; desc.ByteWidth=64; desc.Usage=D3D11_USAGE_DEFAULT;
    desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS; desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; desc.StructureByteStride=16;
    float initial[16]{}; D3D11_SUBRESOURCE_DATA init{initial,0,0};
    check(SUCCEEDED(d->CreateBuffer(&desc,&init,&r.buffer)),"buffer created");
    check(SUCCEEDED(d->CreateShaderResourceView(r.buffer.Get(),nullptr,&r.read)),"SRV created");
    check(SUCCEEDED(d->CreateUnorderedAccessView(r.buffer.Get(),nullptr,&r.write)),"UAV created"); return r;
}
static float read_value(ID3D11Device* d,ID3D11DeviceContext* c,ID3D11Buffer* b) {
    D3D11_BUFFER_DESC desc{}; b->GetDesc(&desc); desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=desc.StructureByteStride=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> copy; check(SUCCEEDED(d->CreateBuffer(&desc,nullptr,&copy)),"test readback created"); c->CopyResource(copy.Get(),b);
    D3D11_MAPPED_SUBRESOURCE m{}; check(SUCCEEDED(c->Map(copy.Get(),0,D3D11_MAP_READ,0,&m)),"test readback mapped"); float v=static_cast<float*>(m.pData)[0]; c->Unmap(copy.Get(),0); return v;
}
