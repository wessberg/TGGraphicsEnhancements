// SPDX-License-Identifier: MIT
// The upscaler's D3D11 shaders on WARP: the copies of color, depth and motion vectors into the shared
// textures, the bilinear fallback for a refused frame, and the rasterized write into a destination
// that offers no unordered-access view.
#include "tgb/d3d_interop.hpp"
#include "upscale_shader.hpp"
#include <d3dcompiler.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using Microsoft::WRL::ComPtr;
static int failures=0;
static void expect(bool condition, const char* what) { if(!condition) { std::printf("FAIL: %s\n", what); ++failures; } }
static void require(HRESULT hr, const char* what) { if(FAILED(hr)) { std::printf("FATAL: %s 0x%08X\n", what, unsigned(hr)); std::exit(1); } }
static float half_to_float(uint16_t h) {
    const uint32_t sign=(h>>15)&1, exponent=(h>>10)&31, mantissa=h&1023;
    float value;
    if(exponent==0) value=std::ldexp(float(mantissa), -24);
    else if(exponent==31) value=mantissa ? NAN : INFINITY;
    else value=std::ldexp(float(mantissa|1024), int(exponent)-25);
    return sign ? -value : value;
}
static ComPtr<ID3DBlob> compile(const char* macro, const char* entry, const char* profile) {
    const D3D_SHADER_MACRO defines[]={{macro, "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> code, errors;
    const HRESULT hr=D3DCompile(tgup::upscaleShader, std::strlen(tgup::upscaleShader), "upscale.hlsl", defines, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
    if(FAILED(hr) && errors) std::printf("%s\n", static_cast<const char*>(errors->GetBufferPointer()));
    require(hr, entry); return code;
}
struct Constants { uint32_t origin[2], extent[2]; float sourceScale[2], halfTexel[2]; };
static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device, UINT w, UINT h, DXGI_FORMAT f, UINT bind, const void* data=nullptr, UINT pitch=0) {
    D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.Format=f; desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=bind;
    D3D11_SUBRESOURCE_DATA init{data, pitch, 0};
    ComPtr<ID3D11Texture2D> t; require(device->CreateTexture2D(&desc, data ? &init : nullptr, &t), "create texture"); return t;
}
static std::vector<uint8_t> read(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* source, UINT bytesPerPixel) {
    D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging; require(device->CreateTexture2D(&desc, nullptr, &staging), "staging");
    context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{}; require(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map");
    std::vector<uint8_t> out(size_t(desc.Width)*desc.Height*bytesPerPixel);
    for(UINT y=0;y<desc.Height;++y) std::memcpy(&out[size_t(y)*desc.Width*bytesPerPixel], static_cast<uint8_t*>(mapped.pData)+size_t(y)*mapped.RowPitch, size_t(desc.Width)*bytesPerPixel);
    context->Unmap(staging.Get(), 0); return out;
}
int main() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    require(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context), "WARP device");
    auto copyCode=compile("TGUP_COPY", "main", "cs_5_0"), fallbackCode=compile("TGUP_FALLBACK", "main", "cs_5_0");
    auto vsCode=compile("TGUP_RASTER", "vsMain", "vs_5_0"), psCode=compile("TGUP_RASTER", "psMain", "ps_5_0");
    ComPtr<ID3D11ComputeShader> copyCS, fallbackCS; ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    require(device->CreateComputeShader(copyCode->GetBufferPointer(), copyCode->GetBufferSize(), nullptr, &copyCS), "copy CS");
    require(device->CreateComputeShader(fallbackCode->GetBufferPointer(), fallbackCode->GetBufferSize(), nullptr, &fallbackCS), "fallback CS");
    require(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &vs), "raster VS");
    require(device->CreatePixelShader(psCode->GetBufferPointer(), psCode->GetBufferSize(), nullptr, &ps), "raster PS");
    D3D11_BUFFER_DESC cbDesc{}; cbDesc.ByteWidth=sizeof(Constants); cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb; require(device->CreateBuffer(&cbDesc, nullptr, &cb), "constants");
    auto dispatch=[&](ID3D11ComputeShader* shader, ID3D11ShaderResourceView* srv, ID3D11UnorderedAccessView* uav, const Constants& c, ID3D11SamplerState* sampler=nullptr) {
        context->UpdateSubresource(cb.Get(), 0, nullptr, &c, 0, 0);
        auto* buffer=cb.Get(); context->CSSetShader(shader, nullptr, 0); context->CSSetConstantBuffers(0, 1, &buffer);
        context->CSSetShaderResources(0, 1, &srv); context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
        if(sampler) context->CSSetSamplers(0, 1, &sampler);
        context->Dispatch((c.extent[0]+7)/8, (c.extent[1]+7)/8, 1);
        ID3D11UnorderedAccessView* none=nullptr; context->CSSetUnorderedAccessViews(0, 1, &none, nullptr);
    };
    // A 9x7 allocation of which the render region is 7x5 from (0,0), as HDRP's RTHandles are.
    const UINT W=9, H=7, w=7, h=5;
    std::vector<float> color(W*H*4), depth(W*H), motion(W*H*2);
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
        const UINT i=y*W+x;
        for(UINT c=0;c<4;++c) color[i*4+c]=float(x)+float(y)*0.25f+float(c)*0.0625f;
        depth[i]=0.001f*float(i+1);
        motion[i*2]=float(x)/64.f; motion[i*2+1]=-float(y)/64.f;
    }
    // Color: RGBA32F source into an RGBA16F shared texture.
    auto colorSrc=texture(device.Get(), W, H, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, color.data(), W*16);
    auto colorDst=texture(device.Get(), w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11UnorderedAccessView> colorUav; require(device->CreateUnorderedAccessView(colorDst.Get(), nullptr, &colorUav), "color UAV");
    auto colorSrv=tgb::typed_srv(device.Get(), colorSrc.Get(), tgb::ReadAs::Color);
    Constants c{}; c.extent[0]=w; c.extent[1]=h;
    dispatch(copyCS.Get(), colorSrv.Get(), colorUav.Get(), c);
    auto colorOut=read(device.Get(), context.Get(), colorDst.Get(), 8);
    bool colorOk=true;
    for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) for(UINT k=0;k<4;++k) {
        uint16_t half; std::memcpy(&half, &colorOut[(size_t(y)*w+x)*8+k*2], 2);
        colorOk=colorOk && std::fabs(half_to_float(half)-color[(y*W+x)*4+k])<0.01f;
    }
    expect(colorOk, "color copy keeps the render region from (0,0)");
    // Depth: a typeless depth-style texture read through its typed R32_FLOAT view, into R32_FLOAT.
    auto depthSrc=texture(device.Get(), W, H, DXGI_FORMAT_R32_TYPELESS, D3D11_BIND_SHADER_RESOURCE, depth.data(), W*4);
    auto depthDst=texture(device.Get(), w, h, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11UnorderedAccessView> depthUav; require(device->CreateUnorderedAccessView(depthDst.Get(), nullptr, &depthUav), "depth UAV");
    auto depthSrv=tgb::typed_srv(device.Get(), depthSrc.Get(), tgb::ReadAs::Depth);
    dispatch(copyCS.Get(), depthSrv.Get(), depthUav.Get(), c);
    auto depthOut=read(device.Get(), context.Get(), depthDst.Get(), 4);
    bool depthOk=true;
    for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) { float v; std::memcpy(&v, &depthOut[(size_t(y)*w+x)*4], 4); depthOk=depthOk && v==depth[y*W+x]; }
    expect(depthOk, "depth copy is exact and unflipped");
    // Motion vectors: RG32F into the RG16F shared texture.
    auto motionSrc=texture(device.Get(), W, H, DXGI_FORMAT_R32G32_FLOAT, D3D11_BIND_SHADER_RESOURCE, motion.data(), W*8);
    auto motionDst=texture(device.Get(), w, h, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11UnorderedAccessView> motionUav; require(device->CreateUnorderedAccessView(motionDst.Get(), nullptr, &motionUav), "motion UAV");
    auto motionSrv=tgb::typed_srv(device.Get(), motionSrc.Get(), tgb::ReadAs::Motion);
    dispatch(copyCS.Get(), motionSrv.Get(), motionUav.Get(), c);
    auto motionOut=read(device.Get(), context.Get(), motionDst.Get(), 4);
    bool motionOk=true;
    for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) for(UINT k=0;k<2;++k) {
        uint16_t half; std::memcpy(&half, &motionOut[(size_t(y)*w+x)*4+k*2], 2);
        motionOk=motionOk && std::fabs(half_to_float(half)-motion[(y*W+x)*2+k])<0.001f;
    }
    expect(motionOk, "motion vector copy keeps sign and scale");
    // Fallback: a 2x2 render region of a 4x4 allocation enlarged to 4x4 with bilinear filtering.
    std::vector<float> region(4*4*4, 0.f);
    const float corners[4]={0.f, 1.f, 2.f, 3.f};
    for(UINT y=0;y<2;++y) for(UINT x=0;x<2;++x) for(UINT k=0;k<4;++k) region[(y*4+x)*4+k]=corners[y*2+x];
    auto regionSrc=texture(device.Get(), 4, 4, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_SHADER_RESOURCE, region.data(), 4*16);
    auto large=texture(device.Get(), 4, 4, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11UnorderedAccessView> largeUav; require(device->CreateUnorderedAccessView(large.Get(), nullptr, &largeUav), "fallback UAV");
    D3D11_SAMPLER_DESC samplerDesc{}; samplerDesc.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU=samplerDesc.AddressV=samplerDesc.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; samplerDesc.MaxLOD=D3D11_FLOAT32_MAX;
    ComPtr<ID3D11SamplerState> sampler; require(device->CreateSamplerState(&samplerDesc, &sampler), "sampler");
    auto regionSrv=tgb::typed_srv(device.Get(), regionSrc.Get(), tgb::ReadAs::Color);
    Constants fc{}; fc.extent[0]=4; fc.extent[1]=4; fc.sourceScale[0]=0.5f; fc.sourceScale[1]=0.5f; fc.halfTexel[0]=fc.halfTexel[1]=0.125f;
    dispatch(fallbackCS.Get(), regionSrv.Get(), largeUav.Get(), fc, sampler.Get());
    auto largeOut=read(device.Get(), context.Get(), large.Get(), 16);
    auto at=[&](UINT x, UINT y){ float v; std::memcpy(&v, &largeOut[(size_t(y)*4+x)*16], 4); return v; };
    // The corners land on the region's corner texels: nothing from the unrendered rest of the
    // allocation (all zero) is blended in at the far edge.
    expect(std::fabs(at(0,0)-0.f)<0.01f && std::fabs(at(3,3)-3.f)<0.01f && std::fabs(at(3,0)-1.f)<0.01f, "fallback samples only the render region");
    expect(at(1,0)>at(0,0) && at(0,1)>at(0,0), "fallback interpolates between render texels");
    // Raster write into an R11G11B10 destination that has a render target view and no UAV.
    auto rasterDst=texture(device.Get(), w, h, DXGI_FORMAT_R11G11B10_FLOAT, D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
    ComPtr<ID3D11RenderTargetView> rtv; require(device->CreateRenderTargetView(rasterDst.Get(), nullptr, &rtv), "RTV");
    Constants rc{}; rc.extent[0]=w; rc.extent[1]=h;
    context->UpdateSubresource(cb.Get(), 0, nullptr, &rc, 0, 0);
    auto* target=rtv.Get(); context->OMSetRenderTargets(1, &target, nullptr);
    D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1}; context->RSSetViewports(1, &viewport);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto* buffer=cb.Get(); auto* srv=colorSrv.Get();
    context->VSSetShader(vs.Get(), nullptr, 0); context->PSSetShader(ps.Get(), nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &buffer); context->PSSetShaderResources(0, 1, &srv); context->PSSetSamplers(0, 1, sampler.GetAddressOf());
    context->Draw(3, 0);
    auto rasterOut=read(device.Get(), context.Get(), rasterDst.Get(), 4);
    uint32_t packed; std::memcpy(&packed, &rasterOut[(size_t(2)*w+3)*4], 4);
    // R11G11B10: red is an unsigned 6e5 float in the low 11 bits.
    const uint32_t r=packed&0x7FF; const float red=std::ldexp(float((r&63)|64), int(r>>6)-15-6);
    expect(std::fabs(red-color[(2*W+3)*4])<0.1f, "raster write reaches a destination without a UAV");
    if(failures) { std::printf("%d upscaling shader checks failed\n", failures); return 1; }
    std::printf("Upscaling shader checks passed on WARP\n");
    return 0;
}
