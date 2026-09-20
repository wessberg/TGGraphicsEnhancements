// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "d3d11_draw_observer.hpp"
#include <d3d11_4.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using Observer=tgfg::D3D11DrawObserver;
static ID3D11DeviceContext* expectedContext=nullptr;
static ID3D11PixelShader* expectedShader=nullptr;
static std::array<unsigned,Observer::Count> observed{};
static bool wrongShader=false;
static Observer::Call last{};
static void check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("D3D11 fixture failed");}
static void observe(ID3D11DeviceContext* context,Observer::Call call) noexcept {
    if(context!=expectedContext)return;
    ++observed[call.kind];last=call;
    if(call.kind==Observer::Execute)return;
    ComPtr<ID3D11PixelShader> shader;context->PSGetShader(&shader,nullptr,nullptr);
    wrongShader|=shader.Get()!=expectedShader;
}
static bool install(void* target,void* detour,void** original) {
    return MH_CreateHook(target,detour,original)==MH_OK && MH_EnableHook(target)==MH_OK;
}
int main() try {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context));
    // Production installs through Context4, while game/add-on calls may use the base interface.
    ComPtr<ID3D11DeviceContext4> context4;check(context.As(&context4));
    if(MH_Initialize()!=MH_OK || !Observer::install(context4.Get(),install,observe))throw std::runtime_error("Cannot install production draw observer");
    expectedContext=context.Get();
    const char* source=R"(
float4 vs(uint id:SV_VertexID):SV_Position {
    id=id%3;return float4(id==2?3:-1,id==1?3:-1,0,1);
}
float4 ps():SV_Target {return float4(0.25,0.5,0.75,1);}
)";
    ComPtr<ID3DBlob> vs,ps;
    check(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"vs","vs_5_0",0,0,&vs,nullptr));
    check(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"ps","ps_5_0",0,0,&ps,nullptr));
    ComPtr<ID3D11VertexShader> vertex;ComPtr<ID3D11PixelShader> pixel;
    check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex));
    check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel));
    expectedShader=pixel.Get();
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=2;td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target,readback;check(device->CreateTexture2D(&td,nullptr,&target));
    ComPtr<ID3D11RenderTargetView> rtv;check(device->CreateRenderTargetView(target.Get(),nullptr,&rtv));
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&td,nullptr,&readback));
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;
    ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster));
    const UINT indices[]={0,0,0,0,1,2,0,1,2};
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof(indices);bd.BindFlags=D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{indices,0,0};ComPtr<ID3D11Buffer> index;
    check(device->CreateBuffer(&bd,&data,&index));
    const UINT args[]={0,0,0,0,3,1,3,2,0};
    bd.BindFlags=0;bd.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;data.pSysMem=args;
    ComPtr<ID3D11Buffer> indirect;check(device->CreateBuffer(&bd,&data,&indirect));
    const UINT indexedArgs[]={0,0,0,0,6,1,3,0,2};data.pSysMem=indexedArgs;
    ComPtr<ID3D11Buffer> indexedIndirect;check(device->CreateBuffer(&bd,&data,&indexedIndirect));
    auto* renderTarget=rtv.Get();D3D11_VIEWPORT viewport{0,0,2,2,0,1};
    context->OMSetRenderTargets(1,&renderTarget,nullptr);context->RSSetViewports(1,&viewport);
    context->RSSetState(raster.Get());context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R32_UINT,0);context->VSSetShader(vertex.Get(),nullptr,0);
    for(unsigned method=0;method<6;++method) {
        observed.fill(0);const float black[4]{};context->ClearRenderTargetView(rtv.Get(),black);
        context->PSSetShader(pixel.Get(),nullptr,0);
        switch(method) {
        case Observer::Draw: context->Draw(4,3);break; // non-three-vertex call must reach observer
        case Observer::Indexed: context->DrawIndexed(6,3,0);break;
        case Observer::Instanced: context->DrawInstanced(3,1,3,2);break;
        case Observer::IndexedInstanced: context->DrawIndexedInstanced(6,1,3,0,2);break;
        case Observer::Indirect: context->DrawInstancedIndirect(indirect.Get(),16);break;
        case Observer::IndexedIndirect: context->DrawIndexedInstancedIndirect(indexedIndirect.Get(),16);break;
        }
        // Simulate the injector restoring bindings immediately after its draw returns.
        context->PSSetShader(nullptr,nullptr,0);
        unsigned calls=0;for(auto n:observed)calls+=n;
        if(calls!=1 || observed[method]!=1 || wrongShader || last.kind!=method)
            throw std::runtime_error("Observer missed/duplicated a draw or ran after shader restoration");
        context->CopyResource(readback.Get(),target.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped));
        bool good=true;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)for(unsigned c=0;c<4;++c) {
            const float actual=reinterpret_cast<const float*>(static_cast<const char*>(mapped.pData)+y*mapped.RowPitch)[4*x+c];
            good&=std::abs(actual-0.25f*(c+1))<0.0001f;
        }
        context->Unmap(readback.Get(),0);
        if(!good)throw std::runtime_error("Draw arguments were not forwarded correctly to GPU");
    }
    ComPtr<ID3D11DeviceContext> deferred;check(device->CreateDeferredContext(0,&deferred));
    const float clear[4]{};deferred->ClearRenderTargetView(rtv.Get(),clear);
    ComPtr<ID3D11CommandList> list;check(deferred->FinishCommandList(FALSE,&list));
    observed.fill(0);context->ExecuteCommandList(list.Get(),TRUE);
    if(observed[Observer::Execute]!=1)throw std::runtime_error("Missed command-list execution");
    MH_Uninitialize();
    std::cout<<"Production draw detours passed: all six draw APIs, quad/indexed geometry, live shader timing, GPU output, command-list observation.\n";
    return 0;
} catch(const std::exception& e) {MH_Uninitialize();std::cerr<<e.what()<<"\n";return 1;}
