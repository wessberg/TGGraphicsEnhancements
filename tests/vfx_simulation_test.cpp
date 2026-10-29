// SPDX-License-Identifier: MIT
// This tests raw simulation snapshots only, not a decoded game layout or D3D12 particle transport.
#include "vfx_test_support.hpp"
#include <cstdlib>
static std::vector<uint32_t> words(ID3D11DeviceContext* c,const sim::Snapshot& s) {
    D3D11_MAPPED_SUBRESOURCE m{};
    check(s.staging && SUCCEEDED(c->Map(s.staging.Get(),0,D3D11_MAP_READ,0,&m)),"snapshot mapped");
    auto begin=static_cast<const uint32_t*>(m.pData); std::vector<uint32_t> out(begin,begin+s.bytes/4);
    c->Unmap(s.staging.Get(),0); return out;
}
static Data raw(ID3D11Device* d,const std::vector<uint32_t>& initial,bool output) {
    Data r; D3D11_BUFFER_DESC b{}; b.ByteWidth=UINT(initial.size()*4); b.Usage=D3D11_USAGE_DEFAULT;
    b.BindFlags=D3D11_BIND_SHADER_RESOURCE|(output ? D3D11_BIND_UNORDERED_ACCESS:0); b.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA data{initial.data(),0,0}; check(SUCCEEDED(d->CreateBuffer(&b,&data,&r.buffer)),"raw buffer");
    D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=DXGI_FORMAT_R32_TYPELESS; s.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
    s.BufferEx.NumElements=UINT(initial.size()); s.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
    check(SUCCEEDED(d->CreateShaderResourceView(r.buffer.Get(),&s,&r.read)),"raw SRV");
    if(output) { D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=DXGI_FORMAT_R32_TYPELESS; u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
        u.Buffer.NumElements=UINT(initial.size()); u.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        check(SUCCEEDED(d->CreateUnorderedAccessView(r.buffer.Get(),&u,&r.write)),"raw UAV"); }
    return r;
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(!GetEnvironmentVariableW(L"TGPT_VFX_SIMULATION_ONLY",nullptr,0)) return 77;
        if(argc!=2) throw std::runtime_error("output directory required");
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> c; D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_1;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&fl,1,D3D11_SDK_VERSION,&device,nullptr,&c)),"D3D11 device");
        TestHost host; device.As(&host.device); c.As(&host.context); check(MH_Initialize()==MH_OK,"MinHook initialized"); install(host,true);
        check(state().hookFailures.empty(),"all diagnostic hooks installed");
        std::string shader=R"(
ByteAddressBuffer attributes:register(t0); ByteAddressBuffer live:register(t1);
RWByteAddressBuffer output:register(u0);
cbuffer C:register(b0) { uint base; uint liveOffset; uint count; uint frameId; };
[numthreads(1,1,1)] void main(uint3 tid:SV_DispatchThreadID) {
 uint i=tid.x; if(i>=count) return;
 uint index=live.Load(4*(liveOffset+i)); uint at=(base+index)*32;
 if(attributes.Load(at+28)==0) return;
 output.Store4(i*32,attributes.Load4(at));
 output.Store3(i*32+16,attributes.Load3(at+16)); output.Store(i*32+28,frameId);
})";
        char mutant[32]{}; GetEnvironmentVariableA("TGPT_VFX_MUTATION",mutant,32);
        auto mutate=[&](const char* a,const char* b) { auto p=shader.find(a); check(p!=std::string::npos,"mutation target"); shader.replace(p,std::strlen(a),b); };
        if(std::strcmp(mutant,"value")==0) mutate("attributes.Load4(at)","attributes.Load4(at)+uint4(1,0,0,0)");
        if(std::strcmp(mutant,"count")==0) mutate("i>=count","i>=count-1");
        if(std::strcmp(mutant,"index")==0) mutate("uint index=live.Load(4*(liveOffset+i))","uint index=i");
        if(std::strcmp(mutant,"frame")==0) mutate("Store(i*32+28,frameId)","Store(i*32+28,frameId-1)");
        auto code=compile(shader.c_str(),"cs_5_0"); ComPtr<ID3D11ComputeShader> cs;
        check(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs)),"candidate shader");
        check(sim::data().programs.at(shader_tag(cs.Get()).hash).rawOutput,"raw-UAV declaration detected");
        check(sim::data().programs.at(shader_tag(cs.Get()).hash).srvs==std::set<UINT>{0,1},"only declared SRVs selected");
        auto initCode=compile("RWByteAddressBuffer b:register(u0); [numthreads(1,1,1)] void main(){b.Store(0,123456);}","cs_5_0");
        ComPtr<ID3D11ComputeShader> initializer;
        device->CreateComputeShader(initCode->GetBufferPointer(),initCode->GetBufferSize(),nullptr,&initializer);
        auto initialized=raw(device.Get(),std::vector<uint32_t>(4,0),true); auto initUav=initialized.write.Get();
        c->CSSetShader(initializer.Get(),nullptr,0); c->CSSetUnorderedAccessViews(0,1,&initUav,nullptr); c->Dispatch(1,1,1);
        check(sim::data().latest.size()==1 && sim::data().current.empty(),"initialization retained before screenshot sampling");
        check(words(c.Get(),sim::data().latest.begin()->second->outputs[0])[0]==123456,"startup snapshot contains the actual initialization value");
        check(configure(argv[1])==0,"sampling configured"); frame(0);
        uint64_t lastResource=0;
        for(UINT pass=0;pass<3;++pass) {
            UINT n=pass==0 ? 4:pass==1 ? 128:2048;
            for(UINT system=0;system<3;++system) {
                const UINT base=5+system, listOffset=3+system, sequence=100+pass*10+system;
                std::vector<uint32_t> attrs((n+base)*8), indices(n+listOffset,0xffffffffu), zero(n*8), expected(n*8);
                for(UINT i=0;i<n;++i) {
                    UINT index=n-1-i; indices[listOffset+i]=index;
                    for(UINT k=0;k<7;++k) attrs[(base+index)*8+k]=10000*system+index*10+k+100*pass;
                    const bool alive=(i%5!=1); attrs[(base+index)*8+7]=alive ? 1:0;
                    if(alive) { for(UINT k=0;k<7;++k) expected[i*8+k]=10000*system+index*10+k+100*pass; expected[i*8+7]=sequence; }
                }
                Data source=raw(device.Get(),attrs,false), list=raw(device.Get(),indices,false), output=raw(device.Get(),zero,true);
                UINT constantData[128]{}; constantData[64]=base; constantData[65]=listOffset; constantData[66]=n; constantData[67]=sequence;
                D3D11_BUFFER_DESC bd{}; bd.ByteWidth=sizeof(constantData); bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
                D3D11_SUBRESOURCE_DATA init{constantData,0,0}; ComPtr<ID3D11Buffer> constants;
                check(SUCCEEDED(device->CreateBuffer(&bd,&init,&constants)),"batched constants");
                ComPtr<ID3D11DeviceContext> deferred; device->CreateDeferredContext(0,&deferred);
                auto ctx=system==1 ? deferred.Get():c.Get(); ComPtr<ID3D11DeviceContext1> ctx1; ctx->QueryInterface(IID_PPV_ARGS(&ctx1));
                ctx->CSSetShader(cs.Get(),nullptr,0); ID3D11ShaderResourceView* reads[]={source.read.Get(),list.read.Get()};
                ctx->CSSetShaderResources(0,2,reads); auto write=output.write.Get(); ctx->CSSetUnorderedAccessViews(0,1,&write,nullptr);
                auto cb=constants.Get(); UINT first=16,count=16; ctx1->CSSetConstantBuffers1(0,1,&cb,&first,&count);
                std::vector<uint32_t> indirectWords{0xffffffffu,0xffffffffu,n,1,1,0xffffffffu};
                ComPtr<ID3D11Buffer> indirect;
                if(system==2) {
                    D3D11_BUFFER_DESC argDesc{}; argDesc.ByteWidth=24; argDesc.Usage=D3D11_USAGE_DEFAULT;
                    argDesc.MiscFlags=D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
                    D3D11_SUBRESOURCE_DATA argData{indirectWords.data(),0,0};
                    device->CreateBuffer(&argDesc,&argData,&indirect); ctx->DispatchIndirect(indirect.Get(),8);
                } else ctx->Dispatch(n,1,1);
                if(system==1) { ComPtr<ID3D11CommandList> commands; deferred->FinishCommandList(FALSE,&commands); c->ExecuteCommandList(commands.Get(),TRUE); }
                std::shared_ptr<sim::Packet> packet;
                { std::lock_guard<std::mutex> lock(sim::data().mutex); packet=sim::data().current.back(); }
                check(packet->outputs.size()==1 && packet->inputs.size()==(system==2 ? 4u:3u),"exact declared binding count");
                check(packet->submittedAfter==int(pass),"snapshot frame ownership");
                check(words(c.Get(),packet->outputs[0])==expected,"all values, shuffled indices, dead entries and frame IDs exact");
                check(words(c.Get(),packet->inputs[0])==std::vector<uint32_t>(constantData,constantData+128),"nonzero constant-buffer range retained");
                check(packet->inputs[0].binding.json.find("\"firstConstant\":16")!=std::string::npos,"constant-buffer offset retained");
                check(packet->outputs[0].binding.resource!=lastResource,"resource replacement has fresh lifetime identity");
                check(packet->outputs[0].binding.resource==identity(output.buffer.Get()) &&
                      packet->inputs[1].binding.resource==identity(source.buffer.Get()) &&
                      packet->inputs[2].binding.resource==identity(list.buffer.Get()),"exact source allocation mapping");
                check(words(c.Get(),packet->inputs[1])==attrs && words(c.Get(),packet->inputs[2])==indices,"input attributes and shuffled live list exact");
                if(system==2) check(packet->indirect && packet->indirectOffset==8 && words(c.Get(),packet->inputs[3])==indirectWords,"indirect arguments captured before dispatch at nonzero offset");
                lastResource=packet->outputs[0].binding.resource;
                // Mutating original inputs after dispatch must not change that dispatch's snapshot.
                std::fill(attrs.begin(),attrs.end(),0xccccccccu); c->UpdateSubresource(source.buffer.Get(),0,nullptr,attrs.data(),0,0);
                auto saved=words(c.Get(),packet->inputs[1]); check(saved!=attrs,"input snapshot owns its dispatch data");
            }
            check(sim::data().current.size()==3,"three systems submitted without drops"); frame(int(pass)+1);
        }
        check(sim::data().current.empty(),"full frame drained"); frame(4);
        check(sim::data().current.empty(),"empty frame never re-submits retained startup records");
        const auto before=sim::data().serial.load(); sim::data().enabled=false;
        c->Dispatch(0,1,1); check(sim::data().serial==before,"hidden switch Off captures nothing");
        check(state().faults==0 && sim::data().faults==0,"no diagnostic faults");
        configure(nullptr); MH_DisableHook(MH_ALL_HOOKS); MH_Uninitialize();
        std::puts("VFX simulation snapshot focused test passed (no D3D12/game-layout claim)"); return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
