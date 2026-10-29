// SPDX-License-Identifier: MIT
// Public SDK-to-driver NGX only. No model-export calls, caller shim, or second D3D12 device.
#include <tgb/d3d_interop.hpp>
#include <tgb/ngx_session.hpp>
#include "tgnr_policy.hpp"
#pragma warning(push, 0)
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers.h>
#pragma warning(pop)
#include <d3dcompiler.h>
#include <bcrypt.h>
#include <array>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstring>

namespace tgnr {
using namespace tgb;
// Version-specific public observation (docs/DLSS5-RESEARCH.md), isolated here.
constexpr auto featureId=static_cast<NVSDK_NGX_Feature>(18);
constexpr const char* projectId="b2cd5cb7-7a65-45e3-a488-0fab2ac96c51";
Host* host=nullptr;
std::recursive_mutex mutex;
TGNR_Status status{sizeof(TGNR_Status),TGNR_ABI,TGNR_WAITING};
std::string reason="Waiting for the graphics device.";
std::filesystem::path runtimeFile;
void log(const std::string& s) { if(host) host->log("TGNR",s.c_str()); }
void unavailable(uint32_t state,const std::string& text,uint32_t result=0) {
    status.state=state; status.lastResult=result; reason=text; log(text);
}
std::string fingerprint(const std::filesystem::path& file) {
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return "unavailable";
    if(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0) { BCryptCloseAlgorithmProvider(algorithm,0); return "unavailable"; }
    std::ifstream stream(file,std::ios::binary); std::array<char,4096> bytes{};
    bool ok=bool(stream);
    while(stream) { stream.read(bytes.data(),bytes.size()); if(BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(stream.gcount()),0)<0) ok=false; }
    std::array<UCHAR,32> digest{};
    ok=ok && !stream.bad() && BCryptFinishHash(hash,digest.data(),ULONG(digest.size()),0)>=0;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
    if(!ok) return "unavailable";
    std::ostringstream out; out<<std::hex<<std::setfill('0'); for(auto b:digest) out<<std::setw(2)<<unsigned(b); return out.str();
}
// Each packet owns its inputs before Unity queues the event. A full ring drops the new request;
// it never overwrites pointers belonging to a command buffer Unity has not executed yet.
struct Packet { bool busy=false; TGNR_Frame frame{}; ComPtr<ID3D11Resource> color,depth,motion; };
std::array<Packet,16> packets;
const char* shaders=R"NR(
cbuffer Copy : register(b0) { uint2 extent; uint mode; uint padding; };
Texture2D<float4> source : register(t0);
Texture2D<float4> original : register(t1);
RWTexture2D<float4> color : register(u0);
RWTexture2D<float> depth : register(u0);
RWTexture2D<float2> motion : register(u0);
float3 encode(float3 x) { return lerp(12.92*x,1.055*pow(max(x,0),1.0/2.4)-0.055,step(0.0031308,x)); }
float3 decode(float3 x) { return lerp(x/12.92,pow(max((x+0.055)/1.055,0),2.4),step(0.04045,x)); }
float peak(float3 x) { return max(0,max(x.r,max(x.g,x.b))); }
float3 compressHdr(float3 x) { return x/(1+peak(x)); }
float3 expandHdr(float3 x) { return x/max(1-peak(x),1.0/65504.0); }
[numthreads(8,8,1)] void Color(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=extent)) return; float4 v=source.Load(int3(p.x,extent.y-1-p.y,0));
 if(mode&2) v.rgb=compressHdr(max(v.rgb,0));
 color[p.xy]=float4((mode&1) ? saturate(v.rgb) : encode(saturate(v.rgb)),v.a);
}
[numthreads(8,8,1)] void Depth(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=extent)) return; depth[p.xy]=source.Load(int3(p.x,extent.y-1-p.y,0)).r;
}
[numthreads(8,8,1)] void Motion(uint3 p:SV_DispatchThreadID) {
 if(any(p.xy>=extent)) return; float2 v=source.Load(int3(p.x,extent.y-1-p.y,0)).xy;
 motion[p.xy]=v; // NGX scale (-displayWidth,+displayHeight) includes the upright Y conversion.
}
float4 Vertex(uint id:SV_VertexID):SV_Position {
 return float4(id==2 ? 3:-1,id==1 ? 3:-1,0,1);
}
float4 Output(float4 p:SV_Position):SV_Target {
 float4 v=source.Load(int3(uint(p.x),extent.y-1-uint(p.y),0));
 float4 before=original.Load(int3(uint(p.x),extent.y-1-uint(p.y),0));
 float3 rgb=decode(v.rgb);
 if(mode&2) {
   // NR consumes LDR. Before SR, use an invertible, hue-preserving shoulder and restore HDR.
   // A saturated/invalid model pixel has no finite inverse: preserve that input pixel instead.
   if(!all(isfinite(rgb)) || peak(rgb)>=1) rgb=decode(before.rgb);
   rgb=expandHdr(rgb);
 }
 return float4(rgb,before.a);
}
)NR";

struct Runtime {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3DDeviceContextState> contextState;
    ComPtr<ID3D11ComputeShader> copyColor,copyDepth,copyMotion;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> output;
    ComPtr<ID3D11Buffer> constants;
    SharedTexture color,depth,motion,result;
    SharedFence produced,consumed; // one signalling API per fence
    NVSDK_NGX_Parameter* params=nullptr;
    NVSDK_NGX_Handle* feature=nullptr;
    ComPtr<ID3D12Resource> scratch;
    ComPtr<ID3D12QueryHeap> timestamps;
    ComPtr<ID3D12Resource> timingReadback;
    uint64_t timestampFrequency=0;
    double gpuMilliseconds=0;
    TGNR_Frame guideFrame{},previous{};
    bool haveGuides=false,havePrevious=false,initialized=false;
    uint32_t generation=0,featureWidth=0,featureHeight=0;
    void disable() {
        if(consumed.ready()) consumed.wait_cpu(consumed.value);
        NgxScope ngx;
        if(feature) { check_ngx(NVSDK_NGX_D3D12_ReleaseFeature(feature),"NR release"); feature=nullptr; }
        color={}; depth={}; motion={}; result={}; scratch.Reset();
        featureWidth=featureHeight=0; haveGuides=havePrevious=false;
        status.gpuMilliseconds=0; status.transferBytes=0;
        log("NR disabled: feature and frame textures released; shared NGX session retained.");
    }
    ComPtr<ID3DBlob> compile(const char* entry,const char* profile) {
        ComPtr<ID3DBlob> code,error;
        auto hr=D3DCompile(shaders,std::strlen(shaders),"TGNR transfer",nullptr,nullptr,entry,profile,D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(hr)) throw DxError(hr,error ? std::string(static_cast<char*>(error->GetBufferPointer()),error->GetBufferSize()) : "Compile NR transfer");
        return code;
    }
    void prepare() {
        if(queue) {
            if(generation!=host->game_device_generation()) throw DxError(E_FAIL,"The graphics device changed; restart the game.");
            return;
        }
        auto d11=host->game_device(); if(!d11) throw DxError(E_FAIL,"The game device is unavailable.");
        generation=host->game_device_generation();
        D3D12_COMMAND_QUEUE_DESC q{}; check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"NR queue");
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"NR allocator");
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"NR command list");
        check(list->Close(),"NR close list");
        D3D12_QUERY_HEAP_DESC query{}; query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; query.Count=2;
        check(device->CreateQueryHeap(&query,IID_PPV_ARGS(&timestamps)),"NR timestamps");
        check(queue->GetTimestampFrequency(&timestampFrequency),"NR timestamp frequency");
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=16;
        buffer.Height=1; buffer.DepthOrArraySize=buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&timingReadback)),"NR timing readback");
        produced.create(device.Get(),d11); consumed.create(device.Get(),d11);
        D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0,chosen;
        check(d11->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&contextState),"NR context state");
        auto cs=[&](const char* entry,ComPtr<ID3D11ComputeShader>& target) {
            auto b=compile(entry,"cs_5_0"); check(d11->CreateComputeShader(b->GetBufferPointer(),b->GetBufferSize(),nullptr,&target),entry);
        };
        cs("Color",copyColor); cs("Depth",copyDepth); cs("Motion",copyMotion);
        auto vs=compile("Vertex","vs_5_0"), ps=compile("Output","ps_5_0");
        check(d11->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"NR output vertex");
        check(d11->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&output),"NR output pixel");
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=16; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(d11->CreateBuffer(&cb,nullptr,&constants),"NR constants");
    }
    void shared(SharedTexture& texture,uint32_t w,uint32_t h,DXGI_FORMAT format) {
        if(texture.matches(w,h,format)) return;
        consumed.wait_cpu(consumed.value);
        texture=SharedTexture::make(host->game_device(),device.Get(),w,h,format,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    }
    void copy(ID3D11Resource* from,SharedTexture& to,ReadAs as,ID3D11ComputeShader* shader,uint32_t mode=0) {
        D3D11_TEXTURE2D_DESC desc{};
        ComPtr<ID3D11Texture2D> texture; check(from->QueryInterface(IID_PPV_ARGS(&texture)),"NR input texture"); texture->GetDesc(&desc);
        ComPtr<ID3D11ShaderResourceView> srv;
        if(as==ReadAs::Color && (desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)) {
            if(desc.ArraySize!=1 || desc.SampleDesc.Count!=1) throw DxError(E_INVALIDARG,"NR requires a single-sample scene texture.");
            check(host->game_device()->CreateShaderResourceView(from,nullptr,&srv),"NR sRGB scene view");
            mode=0; // This typed view decodes sRGB in hardware; encode its linear sample for the model.
        } else srv=typed_srv(host->game_device(),from,as,&desc);
        if(desc.Width<to.width || desc.Height<to.height) throw DxError(E_INVALIDARG,"NR input viewport exceeds texture extent");
        auto c=host->game_context();
        const uint32_t cb[]={to.width,to.height,mode,0}; c->UpdateSubresource(constants.Get(),0,nullptr,cb,0,0);
        ID3D11Buffer* buffer=constants.Get(); c->CSSetConstantBuffers(0,1,&buffer);
        c->CSSetShader(shader,nullptr,0); ID3D11ShaderResourceView* read=srv.Get(); c->CSSetShaderResources(0,1,&read);
        ID3D11UnorderedAccessView* write=to.uav.Get(); c->CSSetUnorderedAccessViews(0,1,&write,nullptr);
        c->Dispatch((to.width+7)/8,(to.height+7)/8,1);
        read=nullptr; write=nullptr; c->CSSetShaderResources(0,1,&read); c->CSSetUnorderedAccessViews(0,1,&write,nullptr);
    }
    void guides(const Packet& p) {
        prepare(); const auto& f=p.frame;
        consumed.wait_cpu(consumed.value);
        shared(depth,f.renderWidth,f.renderHeight,DXGI_FORMAT_R32_FLOAT);
        shared(motion,f.renderWidth,f.renderHeight,DXGI_FORMAT_R16G16_FLOAT);
        ContextScope scope(host->game_context(),contextState.Get());
        copy(p.depth.Get(),depth,ReadAs::Depth,copyDepth.Get());
        copy(p.motion.Get(),motion,ReadAs::Motion,copyMotion.Get());
        guideFrame=f; haveGuides=true;
    }
    void begin_list() {
        consumed.wait_cpu(consumed.value);
        check(allocator->Reset(),"NR reset allocator"); check(list->Reset(allocator.Get(),nullptr),"NR reset list");
    }
    void submit_and_wait() {
        check(list->Close(),"NR close commands"); ID3D12CommandList* work=list.Get(); queue->ExecuteCommandLists(1,&work);
        const auto value=consumed.next(); check(queue->Signal(consumed.d12.Get(),value),"NR completion signal"); consumed.wait_cpu(value);
    }
    void check_ngx(NVSDK_NGX_Result r,const char* operation) {
        status.lastResult=uint32_t(r);
        if(NVSDK_NGX_FAILED(r)) throw DxError(E_FAIL,std::string(operation)+" returned "+hexhr(HRESULT(r)));
    }
    void initialize() {
        if(initialized) return;
        NgxScope lock;
        auto bridge=GetModuleHandleW(L"TGGraphicsBridge.Native.dll");
        if(!GetProcAddress(bridge,"TGGraphicsBridge_LockNgx") || !GetProcAddress(bridge,"TGGraphicsBridge_UnlockNgx"))
            throw DxError(E_FAIL,"TG Neural Rendering requires TG Graphics Bridge 1.2.2 or newer.");
        std::wstring nr=runtimeFile.parent_path().wstring();
        const auto rr=(std::filesystem::path(host->bepinex_root())/L"TGBetterUpscaling"/L"Native"/L"DLSS").wstring();
        const wchar_t* paths[]={nr.c_str(),rr.c_str()};
        NVSDK_NGX_FeatureCommonInfo info{}; info.PathListInfo.Path=paths; info.PathListInfo.Length=2;
        const auto data=(std::filesystem::path(host->bepinex_root())/L"TGNeuralRendering").wstring();
        NVSDK_NGX_FeatureDiscoveryInfo discovery{}; discovery.SDKVersion=NVSDK_NGX_Version_API; discovery.FeatureID=featureId;
        discovery.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Project_Id;
        discovery.Identifier.v.ProjectDesc={projectId,NVSDK_NGX_ENGINE_TYPE_UNITY,"6000.0"};
        discovery.ApplicationDataPath=data.c_str(); discovery.FeatureInfo=&info;
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> adapter;
        if(SUCCEEDED(host->create_native_factory(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapterByLuid(device->GetAdapterLuid(),IID_PPV_ARGS(&adapter)))) {
            NVSDK_NGX_FeatureRequirement requirements{};
            auto requirementQuery=NVSDK_NGX_D3D12_GetFeatureRequirements(adapter.Get(),&discovery,&requirements);
            log("NR requirements query="+hexhr(HRESULT(requirementQuery))+", support flags="+std::to_string(requirements.FeatureSupported));
            // Reserved feature IDs can lack this query in the public SDK. CapabilityParameters below
            // still must explicitly report NR. Respect an actual hardware/driver/OS refusal here.
            if(NVSDK_NGX_SUCCEED(requirementQuery) && (uint32_t(requirements.FeatureSupported)&14u))
                throw DxError(DXGI_ERROR_UNSUPPORTED,"The NVIDIA runtime rejected this GPU, driver or Windows version for Neural Rendering.");
        }
        check_ngx(NVSDK_NGX_D3D12_Init_with_ProjectID(projectId,NVSDK_NGX_ENGINE_TYPE_UNITY,"6000.0",data.c_str(),device.Get(),&info),"NGX initialization");
        // Keep this session alive: Streamline and RR may have features on the same bridge device.
        initialized=true;
        check_ngx(NVSDK_NGX_D3D12_GetCapabilityParameters(&params),"NGX capabilities");
        if(!params) throw DxError(E_FAIL,"NGX returned no parameter map");
        int available=0;
        auto query=params->Get("DLSSNR.Available",&available);
        log("NR capability query="+hexhr(HRESULT(query))+", available="+std::to_string(available));
        if(NVSDK_NGX_FAILED(query) || !available) throw DxError(DXGI_ERROR_UNSUPPORTED,"The NVIDIA runtime did not report Neural Rendering support.");
        // The driver may resolve another copy before this mod starts. Establish which file it loaded.
        auto model=GetModuleHandleW(L"nvngx_dlssnr.dll");
        std::vector<wchar_t> loaded(32768);
        if(!model || !GetModuleFileNameW(model,loaded.data(),DWORD(loaded.size())))
            throw DxError(E_FAIL,"The NVIDIA loader did not expose the Neural Rendering module path.");
        const std::filesystem::path actual(loaded.data());
        log("Loaded NR="+actual.u8string()+", SHA256="+fingerprint(actual));
        if(!std::filesystem::equivalent(actual,runtimeFile) || !host->trusted_signer(actual.c_str(),L"NVIDIA"))
            throw DxError(E_FAIL,"Another Neural Rendering runtime is already loaded; restart with the selected NVIDIA-signed copy.");
    }
    void region(const char* name,uint32_t w,uint32_t h) {
        const std::string prefix=std::string("DLSSNR.")+name+"Subrect";
        params->Set((prefix+"BaseX").c_str(),0u); params->Set((prefix+"BaseY").c_str(),0u);
        params->Set((prefix+"Width").c_str(),w); params->Set((prefix+"Height").c_str(),h);
    }
    void tuning(const TGNR_Frame& f) {
        params->Set("DLSSNR.Enabled",1u); params->Set("DLSSNR.Width",f.width); params->Set("DLSSNR.Height",f.height);
        params->Set(NVSDK_NGX_Parameter_Width,f.width); params->Set(NVSDK_NGX_Parameter_Height,f.height);
        params->Set(NVSDK_NGX_Parameter_OutWidth,f.width); params->Set(NVSDK_NGX_Parameter_OutHeight,f.height);
        params->Set("DLSSNR.Intensity",f.strength);
        params->Set("DLSSNR.Style",f.model);
        params->Set("DLSSNR.LocalToneStrength",f.localTone);
        params->Set("DLSSNR.LocalStructureStrength",f.localStructure);
        params->Set("DLSSNR.SkinStructureStrength",f.skinStructure);
        params->Set("DLSSNR.UseAutoMask",f.autoMask);
        // No separate weight preset or GlobalTone control: neither is established for this DLL.
    }
    void ensure_feature(const TGNR_Frame& f) {
        if(feature && f.width==featureWidth && f.height==featureHeight) return;
        consumed.wait_cpu(consumed.value);
        if(feature) { check_ngx(NVSDK_NGX_D3D12_ReleaseFeature(feature),"NR release"); feature=nullptr; }
        havePrevious=false; scratch.Reset();
        tuning(f); params->Set("CreationNodeMask",1u); params->Set("VisibilityNodeMask",1u);
        size_t scratchBytes=0;
        const auto scratchQuery=NVSDK_NGX_D3D12_GetScratchBufferSize(featureId,params,&scratchBytes);
        if(NVSDK_NGX_FAILED(scratchQuery)) {
            // The public SDK reserves feature 18 and may not implement its scratch query. Record
            // that failure separately, then let ordinary CreateFeature accept or reject no scratch.
            log("NR scratch query failed: "+hexhr(HRESULT(scratchQuery))+"; testing ordinary creation with no scratch.");
            scratchBytes=0;
        }
        if(scratchBytes) {
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=scratchBytes; d.Height=1;
            d.DepthOrArraySize=d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&scratch)),"NR scratch allocation");
        }
        params->Set(NVSDK_NGX_Parameter_Scratch,scratch.Get()); params->Set(NVSDK_NGX_Parameter_Scratch_SizeInBytes,static_cast<unsigned long long>(scratchBytes));
        begin_list(); auto r=NVSDK_NGX_D3D12_CreateFeature(list.Get(),featureId,params,&feature);
        // Even creation is submitted separately and completed before an evaluation can read its weights.
        submit_and_wait(); check_ngx(r,"NR creation");
        if(!feature) throw DxError(E_FAIL,"NR creation returned no handle");
        featureWidth=f.width; featureHeight=f.height;
        log("NR created through public NGX: "+std::to_string(f.width)+"x"+std::to_string(f.height)+", strength="+std::to_string(f.strength));
    }
    void transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource=resource; b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore=before; b.Transition.StateAfter=after; list->ResourceBarrier(1,&b);
    }
    void evaluate(const Packet& p) {
        const auto& f=p.frame;
        if(!haveGuides || !matching(guideFrame,f) || (havePrevious && previous.frame==f.frame && previous.camera==f.camera)) { ++status.skipped; return; }
        haveGuides=false;
        initialize(); prepare();
        NgxScope ngxLock;
        ensure_feature(f);
        shared(color,f.width,f.height,DXGI_FORMAT_R32G32B32A32_FLOAT);
        shared(result,f.width,f.height,DXGI_FORMAT_R32G32B32A32_FLOAT);
        auto context=host->game_context(); ContextScope scope(context,contextState.Get());
        copy(p.color.Get(),color,ReadAs::Color,copyColor.Get(),((f.flags&TGNR_SRGB_VIEW)?1u:0u)|((f.flags&TGNR_HDR_INPUT)?2u:0u));
        const auto ready=produced.next(); check(context->Signal(produced.d11.Get(),ready),"NR producer signal"); context->Flush();
        check(queue->Wait(produced.d12.Get(),ready),"NR producer wait");
        begin_list();
        for(auto* r:{color.d12.Get(),depth.d12.Get(),motion.d12.Get()}) transition(r,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(result.d12.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        params->Set("DLSSNR.Color",color.d12.Get()); params->Set("DLSSNR.Depth",depth.d12.Get());
        params->Set("DLSSNR.MVec",motion.d12.Get()); params->Set("DLSSNR.Output",result.d12.Get());
        tuning(f);
        bool reset=reset_history(f,previous,havePrevious);
        params->Set("DLSSNR.Reset",reset ? 1u:0u); params->Set("DLSSNR.DepthInverted",(f.flags&TGNR_REVERSED_DEPTH)?1u:0u);
        params->Set("DLSSNR.MVecScaleX",-float(f.width)); params->Set("DLSSNR.MVecScaleY",float(f.height));
        region("Color",f.width,f.height); region("Output",f.width,f.height);
        region("Depth",f.renderWidth,f.renderHeight); region("MVec",f.renderWidth,f.renderHeight);
        list->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        auto r=NVSDK_NGX_D3D12_EvaluateFeature(list.Get(),feature,params,nullptr);
        list->EndQuery(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        list->ResolveQueryData(timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,timingReadback.Get(),0);
        for(auto* resource:{color.d12.Get(),depth.d12.Get(),motion.d12.Get()}) transition(resource,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        transition(result.d12.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COMMON);
        submit_and_wait(); check_ngx(r,"NR evaluation");
        D3D12_RANGE range{0,16}; uint64_t* ticks=nullptr;
        check(timingReadback->Map(0,&range,reinterpret_cast<void**>(&ticks)),"NR read timestamps");
        gpuMilliseconds=timestampFrequency ? double(ticks[1]-ticks[0])*1000.0/double(timestampFrequency) : 0;
        D3D12_RANGE written{0,0}; timingReadback->Unmap(0,&written);
        // Commit only a completed successful output. A refused frame leaves Unity's color untouched.
        ComPtr<ID3D11Texture2D> texture; check(p.color.As(&texture),"NR output target"); D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
        D3D11_RENDER_TARGET_VIEW_DESC view{}; view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        view.Format=typed_read_format(desc.Format,ReadAs::Color);
        if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) view.Format=desc.Format;
        if((f.flags&TGNR_SRGB_VIEW) && view.Format==DXGI_FORMAT_R8G8B8A8_UNORM) view.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        if((f.flags&TGNR_SRGB_VIEW) && view.Format==DXGI_FORMAT_B8G8R8A8_UNORM) view.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        ComPtr<ID3D11RenderTargetView> target; check(host->game_device()->CreateRenderTargetView(texture.Get(),&view,&target),"NR scene RTV");
        ID3D11RenderTargetView* rt=target.Get(); context->OMSetRenderTargets(1,&rt,nullptr);
        D3D11_VIEWPORT viewport{0,0,float(f.width),float(f.height),0,1}; context->RSSetViewports(1,&viewport);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(output.Get(),nullptr,0);
        uint32_t cb[]={f.width,f.height,(f.flags&TGNR_HDR_INPUT)?2u:0u,0}; context->UpdateSubresource(constants.Get(),0,nullptr,cb,0,0);
        ID3D11Buffer* buffer=constants.Get(); context->PSSetConstantBuffers(0,1,&buffer);
        // Input color retains the source alpha and is already upright. Read alpha from it with the
        // same Y transform in the output shader, avoiding simultaneous SRV/RTV binding of Unity color.
        ID3D11ShaderResourceView* views[]={result.srv.Get(),color.srv.Get()}; context->PSSetShaderResources(0,2,views); context->Draw(3,0);
        ID3D11ShaderResourceView* empty=nullptr; context->PSSetShaderResources(1,1,&empty);
        previous=f; havePrevious=true; ++status.evaluated; status.lastFrame=f.frame; status.width=f.width; status.height=f.height;
        status.gpuMilliseconds=gpuMilliseconds;
        status.transferBytes=uint64_t(f.width)*f.height*32+uint64_t(f.renderWidth)*f.renderHeight*8;
        if(reset) ++status.resets;
        if(status.evaluated==1 || status.evaluated%300==0 || reset) log("NR frame="+std::to_string(f.frame)+", camera="+std::to_string(f.camera)+", evaluations="+std::to_string(status.evaluated)+", resets="+std::to_string(status.resets)+", GPU ms="+std::to_string(gpuMilliseconds)+", transfer bytes="+std::to_string(uint64_t(f.width)*f.height*32+uint64_t(f.renderWidth)*f.renderHeight*8)+", target format="+std::to_string(desc.Format)+", model="+std::to_string(f.model)+", intensity="+std::to_string(f.strength)+", tone="+std::to_string(f.localTone)+", detail="+std::to_string(f.localStructure)+", skin="+std::to_string(f.skinStructure)+", autoMask="+std::to_string(f.autoMask));
    }
};
// Process lifetime also quarantines resources on a fence timeout; never destroy submitted GPU work.
Runtime& runtime() { static auto* value=new Runtime; return *value; }
void __stdcall render(int event,void* data) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(event==3) {
        try { runtime().disable(); }
        catch(const std::exception& e) { unavailable(TGNR_FAILED,e.what()); }
        catch(...) { unavailable(TGNR_FAILED,"Neural Rendering could not release its feature."); }
        return;
    }
    const auto address=reinterpret_cast<uintptr_t>(data), start=reinterpret_cast<uintptr_t>(packets.data());
    if(address<start || address>=start+sizeof(packets) || (address-start)%sizeof(Packet)) return;
    auto p=static_cast<Packet*>(data);
    if(!p->busy) return;
    try {
        if(status.state==TGNR_READY) {
            if(event==1) runtime().guides(*p); else if(event==2) runtime().evaluate(*p);
        } else ++status.skipped;
    } catch(const std::exception& e) {
        ++status.failed; unavailable(TGNR_FAILED,e.what(),status.lastResult);
        quarantine(p->color.Get()); quarantine(p->depth.Get()); quarantine(p->motion.Get());
    } catch(...) { ++status.failed; unavailable(TGNR_FAILED,"Neural Rendering failed; restart the game."); }
    p->color.Reset(); p->depth.Reset(); p->motion.Reset(); p->busy=false;
}
class Extension final : public tgb::Extension {
public:
    const char* name() noexcept override { return "TG Neural Rendering"; }
    uint32_t needs() noexcept override { return NeedD3D12; }
    bool bootstrap() noexcept override {
        try {
            runtimeFile=neural_runtime_file(host->bepinex_root());
            if(runtimeFile.empty()) { unavailable(TGNR_MISSING,"The Neural Rendering DLL is missing. Put your nvngx_dlssnr.dll in BepInEx/TGNeuralRendering/Native/DLSS and restart."); return true; }
            log("Runtime="+runtimeFile.u8string()+", SHA256="+fingerprint(runtimeFile));
            if(!host->trusted_signer(runtimeFile.c_str(),L"NVIDIA")) {
                unavailable(TGNR_REJECTED,"The Neural Rendering DLL failed NVIDIA signature verification. Install an unmodified NVIDIA-signed file and restart."); return true;
            }
            std::ifstream binary(runtimeFile,std::ios::binary);
            IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 pe{};
            binary.read(reinterpret_cast<char*>(&dos),sizeof(dos));
            if(dos.e_magic!=IMAGE_DOS_SIGNATURE || dos.e_lfanew<sizeof(dos)) throw DxError(E_FAIL,"Invalid Neural Rendering DLL header.");
            binary.seekg(dos.e_lfanew); binary.read(reinterpret_cast<char*>(&pe),sizeof(pe));
            if(!binary || pe.Signature!=IMAGE_NT_SIGNATURE || pe.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 || pe.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC)
                throw DxError(E_FAIL,"Neural Rendering requires a Windows x64 runtime DLL.");
            DWORD ignored=0; DWORD size=GetFileVersionInfoSizeW(runtimeFile.c_str(),&ignored); std::vector<char> version(size);
            bool profileKnown=false;
            if(size && GetFileVersionInfoW(runtimeFile.c_str(),0,size,version.data())) {
                VS_FIXEDFILEINFO* info=nullptr; UINT length=0;
                if(VerQueryValueW(version.data(),L"\\",reinterpret_cast<void**>(&info),&length) && length>=sizeof(*info)) {
                    log("Runtime version="+std::to_string(HIWORD(info->dwFileVersionMS))+"."+std::to_string(LOWORD(info->dwFileVersionMS))+"."+std::to_string(HIWORD(info->dwFileVersionLS)));
                    profileKnown=info->dwFileVersionMS==MAKELONG(8,310) && info->dwFileVersionLS==0;
                }
            }
            if(!profileKnown) unavailable(TGNR_REJECTED,"This Neural Rendering DLL version has no validated input profile in this build (310.8.0.0 required).");
            return true;
        } catch(const std::exception& e) { unavailable(TGNR_REJECTED,e.what()); return true; } catch(...) { return false; }
    }
    void d3d12_device_ready(ID3D12Device* device,uint32_t vendor) noexcept override {
        std::lock_guard<std::recursive_mutex> guard(mutex);
        if(status.state!=TGNR_WAITING) return;
        if(vendor!=0x10de) { unavailable(TGNR_UNSUPPORTED,"DLSS Neural Rendering requires an NVIDIA GPU."); return; }
        try {
            runtime().device=device; runtime().initialize();
            status.state=TGNR_READY; reason.clear(); log("NR reported available; stage=AfterPostProcess before UI and FG; SDR sRGB, upright, real render-resolution guides. No evaluation yet.");
        } catch(const std::exception& e) { unavailable(TGNR_UNSUPPORTED,e.what(),status.lastResult); } catch(...) { unavailable(TGNR_FAILED,"NGX initialization failed."); }
    }
};
}
TGNR_EXPORT tgb::Extension* __cdecl TGGraphicsBridge_CreateExtension(tgb::Host* h,uint32_t abi) {
    if(!h || abi!=TGB_EXTENSION_ABI) return nullptr;
    tgnr::host=h; static tgnr::Extension extension; return &extension;
}
TGNR_EXPORT uint32_t __cdecl TGNR_GetAbiVersion() { return TGNR_ABI; }
TGNR_EXPORT void* __cdecl TGNR_GetRenderEvent() { return reinterpret_cast<void*>(&tgnr::render); }
TGNR_EXPORT int32_t __cdecl TGNR_ReadStatus(TGNR_Status* out) {
    if(!out || out->size!=sizeof(*out) || out->abi!=TGNR_ABI) return -1;
    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex); *out=tgnr::status; return 0;
}
TGNR_EXPORT int32_t __cdecl TGNR_GetReason(char* out,uint32_t size) {
    if(!out || !size) return -1;
    std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);
    auto count=std::min<size_t>(size-1,tgnr::reason.size()); std::memcpy(out,tgnr::reason.data(),count); out[count]=0; return int32_t(count);
}
TGNR_EXPORT void* __cdecl TGNR_QueueFrame(const TGNR_Frame* f,uint32_t stage) {
    if(!f || (stage!=1 && stage!=2) || !tgnr::valid(*f,stage==1)) return nullptr;
    try {
        std::lock_guard<std::recursive_mutex> guard(tgnr::mutex);
        if(tgnr::status.state!=TGNR_READY) return nullptr;
        for(auto& p:tgnr::packets) if(!p.busy) {
            p.frame=*f;
            p.color=reinterpret_cast<ID3D11Resource*>(f->color); p.depth=reinterpret_cast<ID3D11Resource*>(f->depth); p.motion=reinterpret_cast<ID3D11Resource*>(f->motion);
            p.busy=true; return &p;
        }
        ++tgnr::status.skipped; return nullptr;
    } catch(...) { return nullptr; }
}
