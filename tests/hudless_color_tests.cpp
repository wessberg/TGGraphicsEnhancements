// SPDX-License-Identifier: MIT
// Execute the actual capture shader on D3D11 WARP: viewport cropping, Y orientation and encoding.
// This validates conversion, not the game's injection point or vendor frame generation.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <fstream>
#include <iterator>
#include <vector>
#include "interop_shader.hpp"
#include "tgfg_interop_policy.hpp"
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr) { if(FAILED(hr)) throw std::runtime_error("D3D11 call failed: "+std::to_string(hr)); }
static ComPtr<ID3DBlob> compile(const char* macro,const char* entry,const char* profile) {
    D3D_SHADER_MACRO defines[]={{macro,"1"},{nullptr,nullptr}};
    ComPtr<ID3DBlob> code,error;
    HRESULT hr=D3DCompile(tgfg::interopShader,std::strlen(tgfg::interopShader),nullptr,defines,nullptr,entry,profile,
                         D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
    if(FAILED(hr) && error) std::cerr<<static_cast<const char*>(error->GetBufferPointer());
    check(hr);return code;
}
static double srgb(double x) { x=std::max(0.0,x); return x<=0.0031308?12.92*x:1.055*std::pow(x,1.0/2.4)-0.055; }
static float half_float(uint16_t value) {
    const unsigned exponent=(value>>10)&31, fraction=value&1023;
    const float magnitude=exponent?std::ldexp(1.f+float(fraction)/1024.f,int(exponent)-15):std::ldexp(float(fraction),-24);
    return value&0x8000?-magnitude:magnitude;
}
static unsigned test_inputs(ID3D11Device* device,ID3D11DeviceContext* ctx) {
    ctx->ClearState();
    // Offset viewport inside a larger RT: each row and column has distinguishable depth/vector data.
    float pixels[5][5][4]{};
    for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x) {
        pixels[y][x][0]=float(1+5*y+x)/32.f;
        pixels[y][x][1]=-float(1+5*y+x)/64.f;
    }
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=5;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{pixels,sizeof(pixels[0]),0};
    ComPtr<ID3D11Texture2D> source;check(device->CreateTexture2D(&d,&initial,&source));
    ComPtr<ID3D11ShaderResourceView> srv;check(device->CreateShaderResourceView(source.Get(),nullptr,&srv));
    auto* view=srv.Get();ctx->CSSetShaderResources(0,1,&view);
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;check(device->CreateBuffer(&bd,nullptr,&cb));
    const unsigned params[]={1,2,3,2};ctx->UpdateSubresource(cb.Get(),0,nullptr,params,0,0);
    auto* buffer=cb.Get();ctx->CSSetConstantBuffers(0,1,&buffer);
    unsigned checks=0;
    for(unsigned motion=0;motion<2;++motion)for(unsigned flip=0;flip<2;++flip) {
        const char* macro=motion?(flip?"TGFG_MOTION_FLIP":"TGFG_MOTION"):(flip?"TGFG_DEPTH_FLIP":"TGFG_DEPTH");
        auto code=compile(macro,"main","cs_5_0");
        ComPtr<ID3D11ComputeShader> shader;check(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader));
        d.Width=5;d.Height=4;d.Format=motion?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32_FLOAT;
        d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;
        ComPtr<ID3D11Texture2D> target,staging;check(device->CreateTexture2D(&d,nullptr,&target));
        ComPtr<ID3D11UnorderedAccessView> uav;check(device->CreateUnorderedAccessView(target.Get(),nullptr,&uav));
        d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d,nullptr,&staging));
        const float zero[4]{};ctx->ClearUnorderedAccessViewFloat(uav.Get(),zero);
        auto* output=uav.Get();ctx->CSSetUnorderedAccessViews(0,1,&output,nullptr);
        ctx->CSSetShader(shader.Get(),nullptr,0);ctx->Dispatch(1,1,1);
        ctx->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};check(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        bool passed=true;
        for(unsigned y=0;y<4;++y)for(unsigned x=0;x<5;++x)for(unsigned c=0;c<(motion?2u:1u);++c) {
            const auto* row=static_cast<const char*>(mapped.pData)+y*mapped.RowPitch;
            const float actual=motion?half_float(reinterpret_cast<const uint16_t*>(row)[2*x+c]):reinterpret_cast<const float*>(row)[x];
            const float expected=x<3 && y<2?pixels[2+(flip?1-y:y)][1+x][c]:0;
            ++checks;
            if(actual!=expected) {passed=false;std::cerr<<macro<<" ("<<x<<","<<y<<","<<c<<") expected "<<expected<<" got "<<actual<<"\n";}
        }
        ctx->Unmap(staging.Get(),0);
        ID3D11UnorderedAccessView* empty=nullptr;ctx->CSSetUnorderedAccessViews(0,1,&empty,nullptr);
        if(!passed)throw std::runtime_error("Depth/motion orientation mismatch");
    }
    return checks;
}
int main(int argc,char** argv) try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> ctx;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&ctx));
    const auto vs=compile("TGFG_HDR","vsMain","vs_5_0"),ps=compile("TGFG_HUDCOLOR","psMain","ps_5_0");
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
    check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex));
    check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel));
    // Padding is intentionally very different from the 2x2 valid region.
    float data[4][4][4];
    for(auto& row:data) for(auto& p:row) {p[0]=p[1]=p[2]=9999;p[3]=1;}
    const float values[2][2][3]={{{0.001f,0.18f,0.8f},{0.4f,0.03f,0.1f}},{{1.0f,0.2f,0.002f},{0.08f,0.6f,0.5f}}};
    for(unsigned y=0;y<2;++y) for(unsigned x=0;x<2;++x) for(unsigned c=0;c<3;++c) data[y][x][c]=values[y][x][c];
    D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=4;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{data,sizeof(data[0]),0};
    ComPtr<ID3D11Texture2D> source,target,staging;
    check(device->CreateTexture2D(&d,&initial,&source));
    ComPtr<ID3D11ShaderResourceView> srv;check(device->CreateShaderResourceView(source.Get(),nullptr,&srv));
    d.Width=d.Height=2;d.BindFlags=D3D11_BIND_RENDER_TARGET;
    check(device->CreateTexture2D(&d,nullptr,&target));
    ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
    d.BindFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&d,nullptr,&staging));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=16;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;check(device->CreateBuffer(&bd,nullptr,&cb));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster));
    ctx->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{0,0,2,2,0,1};ctx->RSSetViewports(1,&viewport);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vertex.Get(),nullptr,0);ctx->PSSetShader(pixel.Get(),nullptr,0);
    auto* view=srv.Get();ctx->PSSetShaderResources(0,1,&view);
    auto* buffer=cb.Get();ctx->PSSetConstantBuffers(0,1,&buffer);
    auto* renderTarget=rtv.Get();ctx->OMSetRenderTargets(1,&renderTarget,nullptr);
    const double matrix[3][3]={{0.6274039,0.3292830,0.0433131},{0.0690973,0.9195404,0.0113623},{0.0163914,0.0880133,0.8955953}};
    unsigned checks=0;
    for(unsigned flip=0;flip<2;++flip) for(unsigned mode=0;mode<5;++mode) {
        const unsigned params[]={flip,mode,2,2};ctx->UpdateSubresource(cb.Get(),0,nullptr,params,0,0);
        ctx->Draw(3,0);ctx->CopyResource(staging.Get(),target.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};check(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
        bool passed=true;
        for(unsigned y=0;y<2;++y) for(unsigned x=0;x<2;++x) for(unsigned c=0;c<3;++c) {
            const auto& rgb=values[flip?1-y:y][x];
            double expected=srgb(rgb[c]);
            if(mode==4) expected=rgb[c];
            else if(mode) {
                double nits=rgb[c];
                if(mode!=3) {nits=0;for(unsigned k=0;k<3;++k)nits+=matrix[c][k]*rgb[k];}
                if(mode==1)nits*=80;
                expected=tgfg::pq_encode_nits(nits);
            }
            const float actual=reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[4*x+c];
            ++checks;
            if(!std::isfinite(actual) || std::abs(actual-expected)>0.0001) {
                passed=false;std::cerr<<"flip="<<flip<<" mode="<<mode<<" ("<<x<<","<<y<<","<<c<<") expected "<<expected<<" got "<<actual<<"\n";
            }
        }
        ctx->Unmap(staging.Get(),0);
        if(!passed)return 1;
    }
    std::cout<<checks<<" WARP shader comparisons passed (SDR, scRGB, Rec709 nits, Rec2020 nits; flipped and unflipped padded viewport).\n";
    std::cout<<test_inputs(device.Get(),ctx.Get())<<" WARP depth/motion comparisons passed (original/flipped; offset viewport; R32 depth and signed FP16 motion; dispatch bounds).\n";
    if(argc>1) {
        // Optional local check of the exact stripped final shader extracted from
        // RenoDX Unity v0.2026.325.2215. Never load/execute the add-on DLL itself.
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<char> code((std::istreambuf_iterator<char>(file)),{});
        if(code.empty())throw std::runtime_error("Missing extracted RenoDX shader");
        ComPtr<ID3D11PixelShader> reno;check(device->CreatePixelShader(code.data(),code.size(),nullptr,&reno));
        auto rvs=compile("TGFG_HDR","vsReno","vs_5_0");
        ComPtr<ID3D11VertexShader> renoVertex;check(device->CreateVertexShader(rvs->GetBufferPointer(),rvs->GetBufferSize(),nullptr,&renoVertex));
        D3D11_BUFFER_DESC rb{};rb.ByteWidth=512;rb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> injected;check(device->CreateBuffer(&rb,nullptr,&injected));
        ComPtr<ID3D11DeviceContext1> context1;check(ctx.As(&context1));
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;sd.MaxLOD=D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sample;check(device->CreateSamplerState(&sd,&sample));
        // test_inputs changed pipeline state. Restore the same vertex shader used
        // in production and a nonuniform 2x2 source to check UV orientation too.
        float quadPixels[2][2][4];
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x) {
            for(unsigned c=0;c<3;++c)quadPixels[y][x][c]=values[y][x][c];quadPixels[y][x][3]=1;
        }
        d.Usage=D3D11_USAGE_DEFAULT;d.CPUAccessFlags=0;d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA si{quadPixels,sizeof(quadPixels[0]),0};
        ComPtr<ID3D11Texture2D> input;check(device->CreateTexture2D(&d,&si,&input));
        ComPtr<ID3D11ShaderResourceView> inputView;check(device->CreateShaderResourceView(input.Get(),nullptr,&inputView));
        auto* inputSrv=inputView.Get();auto* inject=injected.Get();auto* samp=sample.Get();
        ctx->RSSetViewports(1,&viewport);ctx->RSSetState(raster.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(renoVertex.Get(),nullptr,0);ctx->PSSetShader(reno.Get(),nullptr,0);
        ctx->PSSetShaderResources(0,1,&inputSrv);ctx->PSSetSamplers(0,1,&samp);
        ctx->PSSetConstantBuffers(13,1,&inject);ctx->OMSetRenderTargets(1,&renderTarget,nullptr);
        unsigned compared=0;
        for(unsigned instanced=0;instanced<2;++instanced)
        for(UINT first:{0u,16u})for(float white:{80.f,203.f})for(unsigned gamma=0;gamma<3;++gamma) {
            float settings[128]{};auto* live=settings+first*4;
            live[0]=1;live[1]=1000;live[2]=white;live[3]=white;
            live[4]=float(gamma);live[46]=1; // scRGB, inspected installed shader's cb13[11].z
            const UINT count=16;context1->PSSetConstantBuffers1(13,1,&inject,&first,&count);
            ctx->UpdateSubresource(injected.Get(),0,nullptr,settings,0,0);
            if(instanced)ctx->DrawInstanced(3,1,0,0);else ctx->Draw(3,0);
            ctx->CopyResource(staging.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            check(ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
            bool passed=true;
            for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)for(unsigned c=0;c<3;++c) {
                double expected=values[y][x][c];
                if(gamma)expected=std::pow(srgb(expected),gamma==1?2.2:2.4);
                expected*=white/80.f;
                const auto actual=reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[4*x+c];
                if(!std::isfinite(actual) || std::abs(actual-expected)>0.0002) {
                    passed=false;std::cerr<<"RenoDX instanced="<<instanced<<" white="<<white<<" gamma="<<gamma<<" ("<<x<<","<<y<<","<<c<<") expected "<<expected<<" got "<<actual<<"\n";
                }
                ++compared;
            }
            ctx->Unmap(staging.Get(),0);if(!passed)throw std::runtime_error("Installed RenoDX final shader replay mismatch");
        }
        std::cout<<compared<<" installed RenoDX final-shader WARP comparisons passed (Draw/DrawInstanced, live white/gamma changes, constant-buffer ranges and UV orientation).\n";
    }
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
