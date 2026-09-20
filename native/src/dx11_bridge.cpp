// SPDX-License-Identifier: MIT
#include "dx11_bridge.hpp"
#include "fg_backends.hpp"
#include "interop_shader.hpp"
#include "fsr_input_capture.hpp"
#include "renodx_final_pass.hpp"
#include "d3d11_draw_observer.hpp"
#include <d3dcompiler.h>
#include <filesystem>
#include <array>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <functional>
#include <memory>
#include <algorithm>
#include <utility>
#include <map>

namespace tgfg {
namespace {
struct DxError : std::runtime_error {
    HRESULT hr;
    DxError(HRESULT value,const char* what):std::runtime_error(what),hr(value){}
    DxError(HRESULT value,const std::string& what):std::runtime_error(what),hr(value){}
};
void check(HRESULT hr,const char* what) { if(FAILED(hr)) throw DxError(hr,what); }
std::string hexhr(HRESULT hr) { std::ostringstream s;s<<"0x"<<std::hex<<std::uppercase<<uint32_t(hr);return s.str(); }
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value) CloseHandle(value);}
    Handle()=default; Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
bool same_object(IUnknown* a,IUnknown* b) {
    ComPtr<IUnknown> x,y;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&x))) &&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&y))) && x.Get()==y.Get();
}
// Private-data tag placed on the real immediate context. Graphics wrappers such as ReShade hand
// the game (and so NVIDIA's Unity plugin) a proxy context with its own COM identity, but they
// forward private data to the context they wrap, so the tag still identifies the real one.
// {7C1B5F2E-3D4A-4C61-9F7B-2A6E1D0C9B41}
const GUID kImmediateContextTag={0x7c1b5f2e,0x3d4a,0x4c61,{0x9f,0x7b,0x2a,0x6e,0x1d,0x0c,0x9b,0x41}};
const GUID kRenoFinalTag={0xe3fa3158,0x28fa,0x4fa2,{0x94,0xd0,0x7a,0x11,0xce,0x0a,0xa8,0xc7}};
using CreatePixelShaderFn=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
CreatePixelShaderFn realCreatePixelShader=nullptr;
std::atomic<bool> renoPending{false};
std::atomic<bool> renoDrawSeen{false};
std::array<std::atomic<uint64_t>,D3D11DrawObserver::Count> renoDrawCalls{};
thread_local bool replayingReno=false;
HRESULT STDMETHODCALLTYPE reno_create_pixel_shader(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11PixelShader**);
void reno_after_draw(ID3D11DeviceContext*,D3D11DrawObserver::Call) noexcept;
struct SharedTexture {
    ComPtr<ID3D11Texture2D> d11;
    ComPtr<ID3D12Resource> d12;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    ComPtr<ID3D11RenderTargetView> rtv;
    UINT width=0,height=0; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
    static SharedTexture make(ID3D11Device* d11,ID3D12Device* d12,UINT w,UINT h,DXGI_FORMAT f,UINT bind) {
        SharedTexture t; t.width=w;t.height=h;t.format=f;
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.Format=f;
        desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=bind;
        // NT handles plus explicit shared fences, not keyed-mutex ownership.
        desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        check(d11->CreateTexture2D(&desc,nullptr,&t.d11),"Create D3D11 shared texture");
        ComPtr<IDXGIResource1> dxgi;check(t.d11.As(&dxgi),"Query shared texture IDXGIResource1");
        Handle shared;
        check(dxgi->CreateSharedHandle(nullptr,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&shared.value),"Create texture NT handle");
        check(d12->OpenSharedHandle(shared.value,IID_PPV_ARGS(&t.d12)),"Open D3D11 texture on D3D12");
        const auto actual=t.d12->GetDesc();
        if(actual.Width!=w || actual.Height!=h || actual.Format!=f)
            throw DxError(E_UNEXPECTED,"An injector changed an interop texture descriptor");
        if(bind&D3D11_BIND_SHADER_RESOURCE) check(d11->CreateShaderResourceView(t.d11.Get(),nullptr,&t.srv),"Create shared SRV");
        if(bind&D3D11_BIND_UNORDERED_ACCESS) check(d11->CreateUnorderedAccessView(t.d11.Get(),nullptr,&t.uav),"Create shared UAV");
        if(bind&D3D11_BIND_RENDER_TARGET) check(d11->CreateRenderTargetView(t.d11.Get(),nullptr,&t.rtv),"Create shared RTV");
        return t;
    }
    bool matches(UINT w,UINT h,DXGI_FORMAT f)const{return d11 && width==w && height==h && format==f;}
};
struct ColorBuffer {
    SharedTexture source,encoded;
    ComPtr<ID3D11ShaderResourceView> compareSrv; // source read as UNORM, like the HUD-less copy of it
};
struct InputSlot {
    uint32_t id=0;bool occupied=false,valid=false,hudlessValid=false;
    // Depth and motion are always presentation-oriented; see camera_orientation.hpp.
    SharedTexture depth,motion,hudless;
    sl::Constants constants{};
};
struct ContextScope {
    ID3D11DeviceContext4* context;ComPtr<ID3DDeviceContextState> game;
    ContextScope(ID3D11DeviceContext4* c,ID3DDeviceContextState* scratch):context(c) {
        context->SwapDeviceContextState(scratch,&game);
    }
    ~ContextScope() {
        // Avoid retaining game input resources in the reusable scratch state.
        ID3D11ShaderResourceView* emptySRV[3]={nullptr,nullptr,nullptr};ID3D11UnorderedAccessView* emptyUAV=nullptr;
        context->CSSetShaderResources(0,3,emptySRV);context->PSSetShaderResources(0,1,emptySRV);
        context->CSSetUnorderedAccessViews(0,1,&emptyUAV,nullptr);
        ID3D11Buffer* emptyBuffer=nullptr;ID3D11SamplerState* emptySampler=nullptr;
        context->PSSetConstantBuffers(13,1,&emptyBuffer);context->PSSetSamplers(0,1,&emptySampler);
        context->OMSetRenderTargets(0,nullptr,nullptr);
        context->SwapDeviceContextState(game.Get(),nullptr);
    }
};
const char* output_label(Output o) { return o==Output::FSR?"AMD FSR":o==Output::XeSS?"Intel XeSS":"standard"; }
constexpr UINT kMaxLatency=16;
class Dx11Swapchain;
Dx11Swapchain* active=nullptr; // non-owning; all accesses under State::mutex
thread_local uint32_t presentDepth=0;

struct PresentDepthScope {
    bool entered=false;
    PresentDepthScope() : entered(++presentDepth==1) {}
    ~PresentDepthScope() { --presentDepth; }
};

class Dx11Swapchain final : public IDXGISwapChain4 {
    std::atomic<ULONG> refs_{1};
    ComPtr<ID3D11Device5> d11_;
    ComPtr<ID3D11DeviceContext4> c11_;
    ComPtr<ID3DDeviceContextState> scratch_;
    ComPtr<IDXGIFactory> clientFactory_;
    ComPtr<ID3D12Device> proxyDevice_;
    ComPtr<IDXGISwapChain4> output_;
    ComPtr<ID3D12CommandQueue> queue_, proxyQueue_; // native queue; Streamline's view of it for its swapchain
    ComPtr<IDXGIOutput> restrictTo_;
    std::unique_ptr<GeneratorOutput> generator_; // FSR/XeSS swapchain owner; null for the standard swapchain
    // Replayed onto a recreated presentation swapchain.
    DXGI_HDR_METADATA_TYPE hdrType_=DXGI_HDR_METADATA_TYPE_NONE;
    std::vector<uint8_t> hdrMetadata_;
    // The client's frame-latency object: a bridge-owned semaphore, independent of the presentation swapchain.
    bool waitable_=false;
    UINT maxLatency_=1;
    Handle latencySemaphore_;
    LARGE_INTEGER qpcFrequency_{}, lastPresent_{};
    uint32_t fallbackPresentId_=0;
    ResyncState resync_;
    ComPtr<ID3D12Fence> shared12_;
    ComPtr<ID3D11Fence> shared11_;
    Handle event_;
    FenceTimeline timeline_;
    uint64_t lastRetired_=0;
    std::array<ComPtr<ID3D12CommandAllocator>,3> allocators_;
    std::array<ComPtr<ID3D12GraphicsCommandList>,3> lists_;
    std::array<ComPtr<ID3D12Resource>,3> backbuffers_;
    std::array<uint64_t,3> allocatorRetired_{};
    std::vector<ColorBuffer> colors_;
    std::array<InputSlot,3> inputs_;
    FsrInputCapture inputCapture_;
    bool renoReplay_=false;
    uint32_t renoFrameId_=0;
    uint64_t renoReplays_=0;
    uint32_t renoSkipReasons_=0;
    uint64_t renoLastDiagnostic_=0;
    SharedTexture renoRaw_,renoOutput_; // D3D11 intermediate images; never submitted to FG
    ComPtr<ID3D11ComputeShader> depthCS_,motionCS_,hudCheckCS_;
    // HUD-less verification: one counter, copied to a small staging ring and read two presents later.
    ComPtr<ID3D11Buffer> hudCounter_;
    ComPtr<ID3D11UnorderedAccessView> hudCounterUAV_;
    std::array<ComPtr<ID3D11Buffer>,3> hudStaging_;
    std::array<uint32_t,3> hudStagingSamples_{};
    size_t hudStagingIndex_=0;
    // The previously presented frame, kept only to tell a stale capture from a fresh one.
    ComPtr<ID3D11Texture2D> previous_;
    ComPtr<ID3D11ShaderResourceView> previousSrv_;
    UINT previousWidth_=0,previousHeight_=0;DXGI_FORMAT previousFormat_=DXGI_FORMAT_UNKNOWN;bool previousValid_=false;
    ComPtr<ID3D11VertexShader> colorVS_,renoVS_;
    ComPtr<ID3D11PixelShader> hdrPS_,hudColorPS_;
    ComPtr<ID3D11Buffer> copyCB_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> noDepth_;
    // Keyed by raw GUID bytes; DXGI imposes no ordering of its own.
    struct GuidLess { bool operator()(const GUID& a,const GUID& b) const { return std::memcmp(&a,&b,sizeof(GUID))<0; } };
    std::map<GUID,std::vector<uint8_t>,GuidLess> privateData_;
    std::map<GUID,ComPtr<IUnknown>,GuidLess> privateInterfaces_;
    DXGI_SWAP_CHAIN_DESC1 clientDesc_{};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_{};
    HWND hwnd_=nullptr;
    UINT virtualIndex_=0,physicalFlags_=0;
    DXGI_COLOR_SPACE_TYPE sourceSpace_=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    bool broken_=false,registered_=false,retired_=false,reportedContextThreads_=false;
    DWORD contextThread_=0; // first thread seen using the game's immediate context

    void wait_cpu(uint64_t value) {
        if(!value)return;
        if(shared12_->GetCompletedValue()==UINT64_MAX) throw DxError(DXGI_ERROR_DEVICE_REMOVED,"Shared fence device removed");
        if(shared12_->GetCompletedValue()>=value)return;
        check(shared12_->SetEventOnCompletion(value,event_.value),"Schedule shared fence CPU notification");
        if(WaitForSingleObject(event_.value,5000)!=WAIT_OBJECT_0) throw DxError(DXGI_ERROR_DEVICE_HUNG,"Interop fence timeout");
    }
    void drain() {
        if(!queue_ || !shared12_ || !shared11_ || !event_.value || !c11_) return;
        // The game leaves multithread protection off on its immediate context, so D3D11 does no locking
        // of its own and only one thread may use it at a time. The bridge touches it here from a
        // present, a resize and a handover. Whether those are really the same thread is the game's
        // business, not something the bridge can assume, so say so once if they ever differ.
        const DWORD thread=GetCurrentThreadId();
        if(!contextThread_) contextThread_=thread;
        else if(contextThread_!=thread && !reportedContextThreads_) {
            reportedContextThreads_=true;
            log("The bridge used the game's immediate context from thread "+std::to_string(thread)+
                " having previously used it from thread "+std::to_string(contextThread_)+
                "; the game has multithread protection off, so this ordering depends on the game");
        }
        // Includes unpresented D3D11 capture commands as well as D3D12 work.
        const auto produced=timeline_.next();
        check(c11_->Signal(shared11_.Get(),produced),"Signal D3D11 drain");c11_->Flush();
        check(queue_->Wait(shared12_.Get(),produced),"Wait for D3D11 drain");
        const auto& s=state();
        if(s.lastInputsFence && s.lastInputsFenceValue)
            check(queue_->Wait(s.lastInputsFence.Get(),s.lastInputsFenceValue),"Wait for DLSS-G input consumption");
        lastRetired_=timeline_.next();check(queue_->Signal(shared12_.Get(),lastRetired_),"Signal D3D12 drain");
        wait_cpu(lastRetired_);
    }
    ComPtr<ID3DBlob> compile(const char* macro,const char* entry,const char* profile) {
        const D3D_SHADER_MACRO defines[]={{macro,"1"},{nullptr,nullptr}};
        ComPtr<ID3DBlob> bytecode,errors;
        const HRESULT r=D3DCompile(interopShader,std::strlen(interopShader),"TGFG/interop.hlsl",defines,nullptr,
            entry,profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&bytecode,&errors);
        if(FAILED(r) && errors) log(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
        check(r,"Compile interop shader");return bytecode;
    }
    void shaders() {
        auto d=compile("TGFG_DEPTH","main","cs_5_0");
        check(d11_->CreateComputeShader(d->GetBufferPointer(),d->GetBufferSize(),nullptr,&depthCS_),"Create depth orientation shader");
        auto m=compile("TGFG_MOTION","main","cs_5_0");
        check(d11_->CreateComputeShader(m->GetBufferPointer(),m->GetBufferSize(),nullptr,&motionCS_),"Create motion orientation shader");
        auto vs=compile("TGFG_HDR","vsMain","vs_5_0"),ps=compile("TGFG_HDR","psMain","ps_5_0");
        check(d11_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&colorVS_),"Create conversion VS");
        auto rvs=compile("TGFG_HDR","vsReno","vs_5_0");
        check(d11_->CreateVertexShader(rvs->GetBufferPointer(),rvs->GetBufferSize(),nullptr,&renoVS_),"Create RenoDX replay VS");
        check(d11_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&hdrPS_),"Create scRGB-to-HDR10 PS");
        auto hp=compile("TGFG_HUDCOLOR","psMain","ps_5_0");
        check(d11_->CreatePixelShader(hp->GetBufferPointer(),hp->GetBufferSize(),nullptr,&hudColorPS_),"Create HDRP HUD-less color PS");
        auto hc=compile("TGFG_HUDCHECK","main","cs_5_0");
        check(d11_->CreateComputeShader(hc->GetBufferPointer(),hc->GetBufferSize(),nullptr,&hudCheckCS_),"Create HUD-less comparison shader");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(d11_->CreateBuffer(&cb,nullptr,&copyCB_),"Create copy constants");
        // The comparison result is a single counter: the bridge's only CPU readback, never an image.
        D3D11_BUFFER_DESC counter{};counter.ByteWidth=16;counter.Usage=D3D11_USAGE_DEFAULT;counter.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        counter.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;counter.StructureByteStride=4;
        check(d11_->CreateBuffer(&counter,nullptr,&hudCounter_),"Create HUD-less comparison counter");
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_UNKNOWN;uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;uav.Buffer.NumElements=4;
        check(d11_->CreateUnorderedAccessView(hudCounter_.Get(),&uav,&hudCounterUAV_),"Create HUD-less comparison counter view");
        D3D11_BUFFER_DESC staging{};staging.ByteWidth=16;staging.Usage=D3D11_USAGE_STAGING;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        for(auto& buffer:hudStaging_) check(d11_->CreateBuffer(&staging,nullptr,&buffer),"Create HUD-less comparison readback");
        D3D11_RASTERIZER_DESC raster{};raster.FillMode=D3D11_FILL_SOLID;raster.CullMode=D3D11_CULL_NONE;raster.DepthClipEnable=TRUE;
        check(d11_->CreateRasterizerState(&raster,&raster_),"Create conversion rasterizer");
        D3D11_DEPTH_STENCIL_DESC depth{};depth.DepthEnable=FALSE;depth.StencilEnable=FALSE;
        check(d11_->CreateDepthStencilState(&depth,&noDepth_),"Create disabled depth state");
    }
    ComPtr<ID3D11ShaderResourceView> source_srv(ID3D11Resource* resource,bool depth) {
        ComPtr<ID3D11Texture2D> texture;check(resource->QueryInterface(IID_PPV_ARGS(&texture)),"Input is not a 2D texture");
        D3D11_TEXTURE2D_DESC t{};texture->GetDesc(&t);
        if(t.ArraySize!=1 || t.MipLevels!=1 || t.SampleDesc.Count!=1 || !(t.BindFlags&D3D11_BIND_SHADER_RESOURCE))
            throw DxError(DXGI_ERROR_UNSUPPORTED,"NGX input must be a single-sample, single-mip shader-readable 2D texture");
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=t.Format;srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
        if(depth) {
            switch(t.Format) {
            case DXGI_FORMAT_R32_TYPELESS:case DXGI_FORMAT_D32_FLOAT:srv.Format=DXGI_FORMAT_R32_FLOAT;break;
            case DXGI_FORMAT_R16_TYPELESS:case DXGI_FORMAT_D16_UNORM:srv.Format=DXGI_FORMAT_R16_UNORM;break;
            case DXGI_FORMAT_R24G8_TYPELESS:case DXGI_FORMAT_D24_UNORM_S8_UINT:srv.Format=DXGI_FORMAT_R24_UNORM_X8_TYPELESS;break;
            case DXGI_FORMAT_R32G8X24_TYPELESS:case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:srv.Format=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;break;
            case DXGI_FORMAT_R32_FLOAT:case DXGI_FORMAT_R16_UNORM:case DXGI_FORMAT_R16_FLOAT:break;
            default:throw DxError(DXGI_ERROR_UNSUPPORTED,"Unsupported depth SRV format");
            }
        } else {
            switch(t.Format) {
            case DXGI_FORMAT_R16G16_FLOAT:case DXGI_FORMAT_R32G32_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_FLOAT:case DXGI_FORMAT_R32G32B32A32_FLOAT:
            case DXGI_FORMAT_R16G16_SNORM:case DXGI_FORMAT_R8G8_SNORM:break;
            // Unity creates some render textures typeless on D3D11. Motion vectors in HDRP/URP are
            // always floating point (R16G16_SFloat), so the float view is the correct reading.
            case DXGI_FORMAT_R16G16_TYPELESS:srv.Format=DXGI_FORMAT_R16G16_FLOAT;break;
            case DXGI_FORMAT_R32G32_TYPELESS:srv.Format=DXGI_FORMAT_R32G32_FLOAT;break;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;break;
            case DXGI_FORMAT_R32G32B32A32_TYPELESS:srv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;break;
            default:throw DxError(DXGI_ERROR_UNSUPPORTED,"Unsupported motion-vector SRV format");
            }
        }
        ComPtr<ID3D11ShaderResourceView> view;check(d11_->CreateShaderResourceView(resource,&srv,&view),"Create NGX input SRV");return view;
    }
    void ensure(SharedTexture& t,UINT w,UINT h,DXGI_FORMAT f,UINT bind) {
        if(t.matches(w,h,f)) return;
        // GPU completion, not COM reference count, governs replacing resources.
        drain();auto next=SharedTexture::make(d11_.Get(),state().device.Get(),w,h,f,bind);t=std::move(next);
    }
    void copy_input(ID3D11ShaderResourceView* source,SharedTexture& target,RectU rect,ID3D11ComputeShader* shader) {
        ContextScope scope(c11_.Get(),scratch_.Get());
        const uint32_t constants[]={rect.x,rect.y,rect.width,rect.height};
        c11_->UpdateSubresource(copyCB_.Get(),0,nullptr,constants,0,0);
        auto* cb=copyCB_.Get();auto* uav=target.uav.Get();
        c11_->CSSetShader(shader,nullptr,0);c11_->CSSetConstantBuffers(0,1,&cb);
        c11_->CSSetShaderResources(0,1,&source);c11_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        c11_->Dispatch((rect.width+7)/8,(rect.height+7)/8,1);
    }
    void encode_hdr(ID3D11ShaderResourceView* source,SharedTexture& target,const uint32_t* hudParams=nullptr) {
        ContextScope scope(c11_.Get(),scratch_.Get());
        auto* rtv=target.rtv.Get();c11_->OMSetRenderTargets(1,&rtv,nullptr);
        c11_->OMSetBlendState(nullptr,nullptr,UINT_MAX);c11_->OMSetDepthStencilState(noDepth_.Get(),0);
        D3D11_VIEWPORT v{0,0,float(target.width),float(target.height),0,1};
        c11_->RSSetViewports(1,&v);c11_->RSSetState(raster_.Get());
        c11_->IASetInputLayout(nullptr);c11_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c11_->VSSetShader(colorVS_.Get(),nullptr,0);c11_->GSSetShader(nullptr,nullptr,0);
        c11_->HSSetShader(nullptr,nullptr,0);c11_->DSSetShader(nullptr,nullptr,0);
        if(hudParams) {
            c11_->UpdateSubresource(copyCB_.Get(),0,nullptr,hudParams,0,0);
            auto* cb=copyCB_.Get();c11_->PSSetConstantBuffers(0,1,&cb);
        }
        c11_->PSSetShader(hudParams?hudColorPS_.Get():hdrPS_.Get(),nullptr,0);c11_->PSSetShaderResources(0,1,&source);c11_->Draw(3,0);
    }
    std::vector<ColorBuffer> make_colors(const DXGI_SWAP_CHAIN_DESC1& d) {
        std::vector<ColorBuffer> colors(virtual_buffer_count(d.BufferCount,d.SwapEffect));
        for(auto& c:colors) {
            c.source=SharedTexture::make(d11_.Get(),state().device.Get(),d.Width,d.Height,d.Format,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
            if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)
                c.encoded=SharedTexture::make(d11_.Get(),state().device.Get(),d.Width,d.Height,DXGI_FORMAT_R10G10B10A2_UNORM,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
            // An sRGB-typed source would read back linearised; the HUD-less copy is UNORM, so compare as
            // UNORM. A fully typed sRGB texture refuses that second interpretation with E_INVALIDARG, and
            // a view used only to verify the HUD-less capture must never stop the bridge from existing:
            // without it there is no frame generation at all. Fall back to no comparison view, which
            // leaves the HUD interpolated rather than taking the whole bridge down with it.
            if(DXGI_FORMAT(unorm_format(d.Format))!=d.Format) {
                D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT(unorm_format(d.Format));view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view.Texture2D.MipLevels=1;
                const HRESULT r=d11_->CreateShaderResourceView(c.source.d11.Get(),&view,&c.compareSrv);
                if(FAILED(r)) {
                    c.compareSrv.Reset();
                    log_throttled("Cannot read the "+std::to_string(d.Format)+" back buffer as UNORM for the HUD-less check "+
                                  hexhr(r)+"; frame generation continues with the HUD interpolated");
                }
            } else c.compareSrv=c.source.srv;
        }
        return colors;
    }
    void buffers() { for(UINT i=0;i<3;++i) check(output_->GetBuffer(i,IID_PPV_ARGS(&backbuffers_[i])),"Get presentation buffer"); }
    void apply_color_space() {
        const auto out=DXGI_COLOR_SPACE_TYPE(output_color_space(clientDesc_.Format,sourceSpace_));UINT support=0;
        check(output_->CheckColorSpaceSupport(out,&support),"Query output color space");
        if(!(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) throw DxError(DXGI_ERROR_UNSUPPORTED,"Output does not support requested color space; no silent HDR-to-SDR fallback");
        check(output_->SetColorSpace1(out),"Set presentation color space");
        state().status.outputColorSpace=out;
        log_display_luminance(out);
    }
    // What FSR uses to turn the presented frame back into linear light: for PQ it scales by
    // 10000/maxLuminance, so this range decides the whole range its comparisons work in. It comes from
    // the game's HDR metadata when it sets any, and otherwise from the display. DLSS-G and XeSS are
    // never given it, which makes it the one colour input only FSR depends on.
    void log_display_luminance(DXGI_COLOR_SPACE_TYPE space) {
        std::ostringstream line;
        line<<"Presentation color space "<<uint32_t(space)<<(space==12?" (HDR10 PQ)":space==1?" (linear scRGB)":" (SDR sRGB)");
        ComPtr<IDXGIOutput> output; ComPtr<IDXGIOutput6> output6; DXGI_OUTPUT_DESC1 desc{};
        if(SUCCEEDED(output_->GetContainingOutput(&output)) && SUCCEEDED(output.As(&output6)) && SUCCEEDED(output6->GetDesc1(&desc))) {
            line<<"; display reports "<<desc.MinLuminance<<" to "<<desc.MaxLuminance<<" nits ("
                <<desc.MaxFullFrameLuminance<<" full frame), color space "<<uint32_t(desc.ColorSpace)<<", "<<desc.BitsPerColor<<" bits";
            if(space==12 && desc.MaxLuminance<=0)
                line<<". The display reports no peak luminance, so AMD FSR falls back to whatever the game's HDR metadata says";
        } else line<<"; the display's luminance range could not be read";
        log(line.str());
    }
    void tag_inputs(InputSlot& slot,Frame& frame) {
        auto& s=state();SLGuard guard;
        auto r=s.sl.slSetConstants(slot.constants,*frame.token,s.viewport);
        if(r!=sl::Result::eOk) {
            frame.inputsSubmitted=false;s.status.lastSetOptionsResult=int32_t(r);++s.diag.tagFailed;
            if(!s.diag.tagFailLogged){s.diag.tagFailLogged=true;log("slSetConstants returned "+std::to_string(int(r)));}
            return;
        }
        sl::Extent de{0,0,slot.depth.width,slot.depth.height},me{0,0,slot.motion.width,slot.motion.height};
        sl::Resource d(sl::ResourceType::eTex2d,slot.depth.d12.Get(),D3D12_RESOURCE_STATE_COMMON);
        sl::Resource m(sl::ResourceType::eTex2d,slot.motion.d12.Get(),D3D12_RESOURCE_STATE_COMMON);
        sl::Resource h(sl::ResourceType::eTex2d,slot.hudless.d12.Get(),D3D12_RESOURCE_STATE_COMMON);
        sl::Extent he{0,0,clientDesc_.Width,clientDesc_.Height};
        const bool hudless=slot.hudlessValid && s.hudlessOk; // verified against the presented frames first
        sl::ResourceTag tags[]={
            {&d,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&de},
            {&m,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,&me},
            {hudless?&h:nullptr,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent,&he},
            {nullptr,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent}};
        // The D3D12 queue already waits on the D3D11 producer signal. Inputs are
        // not reused by D3D11 until the consumer handoff at the end of Present.
        r=s.sl.slSetTagForFrame(*frame.token,s.viewport,tags,4,nullptr);
        frame.inputsSubmitted=r==sl::Result::eOk;
        if(r==sl::Result::eOk) { ++s.diag.tagged; if(hudless) ++s.diag.hudlessTagged; }
        else {
            s.status.lastSetOptionsResult=int32_t(r);++s.diag.tagFailed;
            if(!s.diag.tagFailLogged){s.diag.tagFailLogged=true;log("slSetTagForFrame returned "+std::to_string(int(r)));}
        }
        if(hudless && r==sl::Result::eOk)s.status.bridgeFlags|=16u;else s.status.bridgeFlags&=~16u;
    }
    static void create_native_device(IDXGIAdapter* adapter) {
        const auto path=std::filesystem::path(system_directory())/L"d3d12.dll";
        const HMODULE module=LoadLibraryW(path.c_str());
        if(!module) throw DxError(HRESULT_FROM_WIN32(GetLastError()),"Load the system d3d12.dll");
        const auto create=reinterpret_cast<decltype(&::D3D12CreateDevice)>(GetProcAddress(module,"D3D12CreateDevice"));
        if(!create) throw DxError(E_NOINTERFACE,"The system d3d12.dll has no D3D12CreateDevice");
        ComPtr<ID3D12Device> device;
        check(create(adapter,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"Create private D3D12 presentation device");
        state().device=device;
    }
    void pause_generation() { if(generator_) generator_->quiesce(); else options_off(); }
    // The presentation swapchain for one frame generation technology: the standard swapchain (a
    // Streamline proxy that also hosts DLSS-G when Streamline is bound) or the FSR/XeSS SDK swapchain.
    void create_output(Output kind) {
        auto& s=state();
        DXGI_SWAP_CHAIN_DESC1 outputDesc=clientDesc_;outputDesc.BufferCount=3;outputDesc.Format=DXGI_FORMAT(output_format(clientDesc_.Format));
        outputDesc.SampleDesc={1,0};outputDesc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        outputDesc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;outputDesc.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
        // A client that waits on a frame-latency object gets the bridge's own (GetFrameLatencyWaitableObject),
        // which outlives this swapchain; the swapchain itself throttles through a blocking Present.
        outputDesc.Flags&=~UINT(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        physicalFlags_=outputDesc.Flags;
        ComPtr<IDXGIFactory2> factory;
        if(kind==Output::Standard && s.slReady) check(s.sl.createFactory2(0,IID_PPV_ARGS(&factory)),"Create private Streamline DXGI factory");
        else check(create_native_factory(IID_PPV_ARGS(&factory)),"Create native DXGI factory");
        check(factory->MakeWindowAssociation(hwnd_,DXGI_MWA_NO_ALT_ENTER),"Keep fullscreen transitions under game control");
        if(kind==Output::Standard) {
            ComPtr<IDXGISwapChain1> output;
            check(factory->CreateSwapChainForHwnd(s.slReady?proxyQueue_.Get():queue_.Get(),hwnd_,&outputDesc,&fullscreen_,restrictTo_.Get(),&output),
                "Create private Streamline output swapchain");
            check(output.As(&output_),"Query Streamline IDXGISwapChain4");
            s.haveOptions=false; // a recreated Streamline swapchain receives the full DLSS-G options again
        } else {
            OutputParams params; params.hwnd=hwnd_; params.desc=outputDesc; params.fullscreen=&fullscreen_;
            params.device=s.device.Get(); params.queue=queue_.Get(); params.factory=factory.Get();
            params.hdr=output_color_space(clientDesc_.Format,sourceSpace_)==12; params.inputFlags=s.ngxFlags;
            std::string error;
            generator_=kind==Output::FSR?create_fsr_output(params,error):create_xess_output(params,error);
            if(!generator_) throw DxError(E_FAIL,error);
            output_=generator_->swapchain();
        }
        buffers();apply_color_space();
        if(hdrType_!=DXGI_HDR_METADATA_TYPE_NONE)
            output_->SetHDRMetaData(hdrType_,UINT(hdrMetadata_.size()),hdrMetadata_.empty()?nullptr:hdrMetadata_.data());
        s.swapchain=output_.Get();s.output=kind;s.status.activeBackend=uint32_t(kind);
        s.status.swapchainFormat=outputDesc.Format;
    }
    void destroy_output() {
        auto& s=state();
        if(generator_) generator_->quiesce();
        else if(output_) { options_off(); for(auto& slot:s.frames.slots) if(slot.occupied) clear_tags(&slot.value); }
        drain();
        if(output_) {
            BOOL fullscreen=FALSE;
            if(SUCCEEDED(output_->GetFullscreenState(&fullscreen,nullptr)) && fullscreen) output_->SetFullscreenState(FALSE,nullptr);
        }
        for(auto& b:backbuffers_)b.Reset();
        s.swapchain=nullptr; output_.Reset();
        generator_.reset(); // SDK contexts go after the last reference to their swapchain
        // DLSS-G's input fence belonged to the Streamline swapchain just released.
        s.lastInputsFence.Reset(); s.lastInputsFenceValue=0;
    }
    Output desired_output() {
        auto& s=state();
        // Without a live plugin request the current swapchain stays; the plugin decides the technology.
        if(!valid(s.request) || GetTickCount64()-s.heartbeat>1000) return s.output;
        auto want=output_for(s.request);
        const uint32_t bit=1u<<uint32_t(want);
        if(want!=Output::Standard && (!(s.status.backendsAvailable&bit) || (s.status.backendFailedMask&bit))) want=Output::Standard;
        return want;
    }
    // Only between frames, on the presenting thread: D3D11 keeps its buffers and never notices.
    void switch_output(Output want) {
        auto& s=state();
        log(std::string("Presentation switching from the ")+output_label(s.output)+" to the "+output_label(want)+" swapchain");
        destroy_output();
        try { create_output(want); }
        catch(const DxError& e) {
            s.status.backendFailedMask|=1u<<uint32_t(want); s.status.backendResult=e.hr;
            log(std::string(e.what())+" "+hexhr(e.hr)+"; "+output_label(want)+" frame generation is disabled for this session");
            destroy_output();
            create_output(Output::Standard);
        }
        allocatorRetired_.fill(0); lastPresent_.QuadPart=0;
        log(std::string("Presenting through the ")+output_label(s.output)+" swapchain");
    }
    // FSR/XeSS: the frame's generation decision and SDK work, recorded before the list closes.
    void record_generation(ID3D12GraphicsCommandList* cmd,Frame* frame,uint32_t presentId,float frameMs) {
        auto& s=state();
        GenerationInputs in{};
        bool have=false;
        if(frame && frame->inputs) {
            auto& slot=inputs_[renderFrameId%inputs_.size()];
            if(slot.occupied && slot.valid && slot.id==renderFrameId) {
                in.depth=slot.depth.d12.Get(); in.motion=slot.motion.d12.Get();
                in.depthWidth=slot.depth.width; in.depthHeight=slot.depth.height;
                in.motionWidth=slot.motion.width; in.motionHeight=slot.motion.height;
                in.jitterX=slot.constants.jitterOffset.x; in.jitterY=slot.constants.jitterOffset.y;
                // Streamline's normalised scale back to NGX's scale to render-resolution pixels.
                in.motionScaleX=slot.constants.mvecScale.x*float(slot.motion.width);
                in.motionScaleY=slot.constants.mvecScale.y*float(slot.motion.height);
                in.reset=slot.constants.reset==sl::eTrue; in.camera=&frame->camera; in.frameTimeMs=frameMs;
                if(slot.hudlessValid && s.hudlessOk) { // verified against the presented frames first
                    in.hudless=slot.hudless.d12.Get(); in.hudlessWidth=slot.hudless.width; in.hudlessHeight=slot.hudless.height;
                }
                have=true;
            }
        }
        if(frame) frame->inputsSubmitted=have;
        const auto maxGenerated=generator_->max_generated();
        const auto ready=frame_readiness(frame,s.status.swapchainFormat,maxGenerated,0,output_for(s.request)==s.output);
        auto why=eligibility(s.request,ready);
        const bool enable=why==Reason::Ready;
        const bool recorded=generator_->record(cmd,presentId,enable?&in:nullptr,generated_frames(s.request,maxGenerated));
        if(enable && !recorded) { why=Reason::BackendFailed; if(frame) frame->inputsSubmitted=false; }
        if(enable && recorded) { ++s.diag.tagged; if(in.hudless) ++s.diag.hudlessTagged; }
        if(enable && recorded && in.hudless) s.status.bridgeFlags|=16u; else s.status.bridgeFlags&=~16u;
        note_generation(frame,why,enable && recorded);
    }
    // Everything the HUD-less verification concluded, dropped together, because it only ever described
    // one buffer configuration: the shared trust flag, the counter readbacks still in flight from the
    // old buffers, and the previously presented frame the staleness check compares against. Without
    // this a session verified on UNORM buffers would keep supplying captures after a resize into a
    // configuration where the comparison cannot run at all.
    void forget_hudless_verification(const char* why) {
        reset_hudless_verification(why);
        hudStagingSamples_.fill(0); previousValid_=false;
    }
    // Compares the frame about to be presented with the HUD-less capture at a sparse grid, on the
    // D3D11 context before the hand-off. Only HUD-sized regions may differ: something that redraws the
    // frame after the camera (a post-process injector at Present) or a stale copy differs everywhere,
    // and the captures are then withheld from the technologies (note_hudless_difference).
    void verify_hudless(InputSlot& slot,uint32_t id,ColorBuffer& color,SharedTexture* final) {
        if(!slot.occupied || slot.id!=id || !slot.hudlessValid || !slot.hudless.matches(final->width,final->height,final->format)) return;
        // No comparable view of the presented frame: the capture cannot be verified, so it is never
        // supplied. Presentation and frame generation are unaffected.
        if(final!=&color.encoded && !color.compareSrv) return;
        constexpr UINT stride=4;
        const UINT sx=final->width/stride,sy=final->height/stride;
        if(!sx || !sy) return;
        // The previously presented frame, as UNORM like the HUD-less copy (CopyResource accepts the sRGB view).
        const auto compareFormat=DXGI_FORMAT(unorm_format(final->format));
        if(!previous_ || previousWidth_!=final->width || previousHeight_!=final->height || previousFormat_!=compareFormat) {
            D3D11_TEXTURE2D_DESC desc{};desc.Width=final->width;desc.Height=final->height;desc.Format=compareFormat;
            desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            previous_.Reset();previousSrv_.Reset();
            check(d11_->CreateTexture2D(&desc,nullptr,&previous_),"Create previous-frame texture");
            check(d11_->CreateShaderResourceView(previous_.Get(),nullptr,&previousSrv_),"Create previous-frame SRV");
            previousWidth_=final->width;previousHeight_=final->height;previousFormat_=compareFormat;previousValid_=false;
        }
        if(previousValid_) {
            ContextScope scope(c11_.Get(),scratch_.Get());
            const UINT zero[4]{};c11_->ClearUnorderedAccessViewUint(hudCounterUAV_.Get(),zero);
            const uint32_t constants[]={stride,0,sx,sy};
            c11_->UpdateSubresource(copyCB_.Get(),0,nullptr,constants,0,0);
            ID3D11ShaderResourceView* views[]={final==&color.encoded?color.encoded.srv.Get():color.compareSrv.Get(),slot.hudless.srv.Get(),previousSrv_.Get()};
            auto* cb=copyCB_.Get();auto* uav=hudCounterUAV_.Get();
            c11_->CSSetShader(hudCheckCS_.Get(),nullptr,0);c11_->CSSetConstantBuffers(0,1,&cb);
            c11_->CSSetShaderResources(0,3,views);c11_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
            c11_->Dispatch((sx+7)/8,(sy+7)/8,1);
            c11_->CopyResource(hudStaging_[hudStagingIndex_].Get(),hudCounter_.Get());
            hudStagingSamples_[hudStagingIndex_]=sx*sy;
        }
        c11_->CopyResource(previous_.Get(),final->d11.Get());previousValid_=true;
        // The result submitted two presents ago; one still in flight is skipped, never waited for.
        const size_t oldest=(hudStagingIndex_+1)%hudStaging_.size();
        if(hudStagingSamples_[oldest]) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT hr=c11_->Map(hudStaging_[oldest].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
            if(SUCCEEDED(hr)) {
                const auto* counters=static_cast<const uint32_t*>(mapped.pData);
                const uint32_t differing=counters[0],changed=counters[1],moving=counters[2];
                c11_->Unmap(hudStaging_[oldest].Get(),0);
                note_hudless_difference(differing,changed,moving,hudStagingSamples_[oldest]);
            }
            if(hr!=DXGI_ERROR_WAS_STILL_DRAWING) hudStagingSamples_[oldest]=0;
        }
        hudStagingIndex_=oldest;
    }
    void capture_fsr_inputs(float frameMs) {
        auto& s=state();
        const bool armed=(s.debugFlags&32u)!=0;
        if(!armed) { inputCapture_.tick(d11_.Get(),c11_.Get(),false,0,nullptr,nullptr,nullptr,"");return; }
        if(!inputCapture_.wants_frame() && !inputCapture_.started()) return;
        InputSlot* slot=nullptr;
        std::ostringstream metadata;metadata.precision(9);
        auto* frame=haveRenderFrame?s.frames.find(renderFrameId):nullptr;
        if(armed && inputCapture_.wants_frame() && s.output==Output::FSR &&
           s.request.mode && !s.request.suspensionMask && s.status.lastAcceptedMode &&
           frame && frame->inputs && !frame->camera.resetHistory && s.hudlessOk) {
            auto& candidate=inputs_[renderFrameId%inputs_.size()];
            float movement=0.f;
            for(size_t i=0;i<16;++i)
                movement=std::max(movement,std::abs(frame->camera.clipToPrev[i]-(i%5==0?1.f:0.f)));
            if(candidate.occupied && candidate.valid && candidate.id==renderFrameId &&
               candidate.hudlessValid && (inputCapture_.started() || movement>0.001f)) {
                slot=&candidate;
                const auto& c=slot->constants;
                metadata<<"\"frameTimeMs\":"<<frameMs<<",\"ngxInputs\":"<<(s.upscalerSeen && s.lastUpscalerFrame==renderFrameId?"true":"false")
                    <<",\"ngxFlags\":"<<s.ngxFlags<<",\"outputColorSpace\":"<<s.status.outputColorSpace
                    <<",\"motionScaleUv\":["<<c.mvecScale.x<<","<<c.mvecScale.y<<"]"
                    <<",\"jitterPixels\":["<<c.jitterOffset.x<<","<<c.jitterOffset.y<<"]"
                    <<",\"depthInverted\":"<<(c.depthInverted==sl::eTrue?"true":"false")
                    <<",\"motionJittered\":"<<(c.motionVectorsJittered==sl::eTrue?"true":"false")
                    <<",\"near\":"<<frame->camera.nearPlane<<",\"far\":"<<frame->camera.farPlane
                    <<",\"fovRadians\":"<<frame->camera.fovRadians<<",\"clipToPrev\":[";
                for(size_t i=0;i<16;++i) { if(i) metadata<<",";metadata<<frame->camera.clipToPrev[i]; }
                metadata<<"]";
            }
        }
        inputCapture_.tick(d11_.Get(),c11_.Get(),armed,slot?slot->id:0,
            slot?slot->hudless.d11.Get():nullptr,slot?slot->depth.d11.Get():nullptr,
            slot?slot->motion.d11.Get():nullptr,metadata.str());
    }
    HRESULT present(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* params) {
        SLGuard own; // private D3D12 calls are never rediscovered as game calls
        PresentDepthScope depth;
        if(!depth.entered) {
            log("Recursive bridge Present detected; rejecting re-entry");
            return DXGI_ERROR_INVALID_CALL;
        }
        if(retired_) {
            // Presenting to a swapchain the game has already replaced. Still hand back a latency
            // permit, or a client that waits before each frame stalls on an object nothing drives.
            if(latencySemaphore_.value) ReleaseSemaphore(latencySemaphore_.value,1,nullptr);
            return S_OK;
        }
        if(broken_) return DXGI_ERROR_DEVICE_REMOVED;
        if(flags&DXGI_PRESENT_TEST) {
            // An occlusion probe can report that a resize is owed, but a clean probe presents nothing
            // and so cannot retire one that already is.
            const HRESULT test=output_->Present(sync,flags); resync_.present_status(test==S_OK); return test;
        }
        // A nonblocking Present cannot be emulated by a path that enqueues copies
        // before knowing whether Present will accept them. Do not consume a frame.
        if(flags&DXGI_PRESENT_DO_NOT_WAIT) return DXGI_ERROR_WAS_STILL_DRAWING;
        if(params && (params->DirtyRectsCount || params->pScrollRect || params->pScrollOffset))
            return DXGI_ERROR_UNSUPPORTED;
        if(renoReplay_ && renoPending.load(std::memory_order_relaxed) && GetTickCount64()-renoLastDiagnostic_>=5000) {
            renoLastDiagnostic_=GetTickCount64();
            std::ostringstream message;
            message<<"RenoDX replay still pending; observed draw/indexed/instanced/indexedInstanced/indirect/indexedIndirect/commandLists=";
            for(size_t i=0;i<renoDrawCalls.size();++i)message<<(i?"/":"")<<renoDrawCalls[i].load(std::memory_order_relaxed);
            message<<"; recognized draw seen="<<renoDrawSeen.load(std::memory_order_relaxed);
            log(message.str());
        }
        auto& s=state();
        if(!source_color_supported(clientDesc_.Format,sourceSpace_)) return DXGI_ERROR_UNSUPPORTED;
        const auto want=desired_output();
        if(want!=s.output || (generator_ && generator_->stale(s.ngxFlags))) switch_output(want);
        const auto latency=update_latency();
        const uint32_t interval=latency==Latency::XeLL?frame_limit_us(s.request.baseFpsLimit):0;
        // XeLL's sleep mode may only change while the GPU is idle.
        if(xell_mode_pending(latency==Latency::XeLL,interval)) { drain(); xell_apply(latency==Latency::XeLL,interval); }
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        const float frameMs=lastPresent_.QuadPart && qpcFrequency_.QuadPart ?
            float(double(now.QuadPart-lastPresent_.QuadPart)*1000.0/double(qpcFrequency_.QuadPart)) : 0.f;
        lastPresent_=now;
        auto& color=colors_.at(virtualIndex_);
        SharedTexture* final=&color.source;
        if(clientDesc_.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            encode_hdr(color.source.srv.Get(),color.encoded);final=&color.encoded;
        }
        if(haveRenderFrame && s.hudlessRequested) verify_hudless(inputs_[renderFrameId%inputs_.size()],renderFrameId,color,final);
        capture_fsr_inputs(frameMs);
        const auto produced=timeline_.next();
        check(c11_->Signal(shared11_.Get(),produced),"Signal D3D11 producer");c11_->Flush();
        check(queue_->Wait(shared12_.Get(),produced),"D3D12 wait for DX11 frame");
        // Required even though Unity itself only sees D3D11 buffers.
        const UINT index=output_->GetCurrentBackBufferIndex();
        if(index>=3) throw DxError(E_UNEXPECTED,"Presentation swapchain returned an invalid backbuffer index");
        wait_cpu(allocatorRetired_[index]);
        check(allocators_[index]->Reset(),"Reset bridge command allocator");
        check(lists_[index]->Reset(allocators_[index].Get(),nullptr),"Reset bridge command list");
        auto* cmd=lists_[index].Get();
        D3D12_RESOURCE_BARRIER b[2]{};
        for(auto& barrier:b) {barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;}
        b[0].Transition.pResource=final->d12.Get();b[0].Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b[0].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
        b[1].Transition.pResource=backbuffers_[index].Get();b[1].Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;b[1].Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        cmd->ResourceBarrier(2,b);cmd->CopyResource(backbuffers_[index].Get(),final->d12.Get());
        for(auto& barrier:b) std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        cmd->ResourceBarrier(2,b);
        auto* frame=haveRenderFrame?s.frames.find(renderFrameId):nullptr;
        if(frame && frame->inputs) ++s.diag.presentsWithInputs;
        // XeSS pairs its present IDs with XeLL's frame IDs, which are the game's frame numbers.
        const uint32_t presentId=haveRenderFrame?renderFrameId:++fallbackPresentId_;
        if(generator_) record_generation(cmd,frame,presentId,frameMs);
        else if(frame && frame->inputs) {
            auto& slot=inputs_[renderFrameId%inputs_.size()];
            if(s.dlssReady && frame->token && slot.occupied && slot.valid && slot.id==renderFrameId) tag_inputs(slot,*frame);
            else frame->inputsSubmitted=false;
        }
        check(cmd->Close(),"Close bridge command list");
        ID3D12CommandList* submit[]={cmd};queue_->ExecuteCommandLists(1,submit);++s.status.copiedFrames;
        // Always evaluate: Off must actively switch DLSS-G off (emergency key, menu), and the
        // present markers keep Reflex, XeLL or Anti-Lag 2 fed even when frame generation is not requested.
        // No per-frame logging here; the pipeline summary covers it every few seconds.
        before_present(output_.Get());
        if(generator_) generator_->before_present(presentId);
        // Respect VSync unless the NVIDIA runtime explicitly says it cannot be
        // used with FG. In that case suspend FG, not the user's VSync setting.
        else if(sync && !s.vsyncSupported && s.status.lastAcceptedMode) {options_off();s.status.reason=uint32_t(Reason::VsyncUnsupported);}
        const HRESULT hr=output_->Present(sync,flags);
        resync_.present_status(hr==S_OK); // a mode change owes a resize; occlusion is treated the same way, conservatively
        if(FAILED(hr)) log("Private presentation Present returned "+hexhr(hr));
        if(generator_) generator_->after_present();
        after_present(hr);
        if(FAILED(hr)) {broken_=true;return hr;}
        // This field is a borrowed NVIDIA fence. State holds its own COM ref.
        // Transfer its dependency to our shared timeline, then to D3D11.
        s.status.completionFenceUsed=0;
        if(s.lastInputsFence && s.lastInputsFenceValue) {
            check(queue_->Wait(s.lastInputsFence.Get(),s.lastInputsFenceValue),"Wait for NVIDIA input processing");
            s.status.completionFenceUsed=1;
        }
        // eBlockPresentingClientQueue is enforced in DLSSGOptions. Thus this
        // signal follows both our copy and SL's input use, even in Off frames.
        lastRetired_=timeline_.next();check(queue_->Signal(shared12_.Get(),lastRetired_),"Signal D3D12 consumer completion");
        allocatorRetired_[index]=lastRetired_;
        check(c11_->Wait(shared11_.Get(),lastRetired_),"D3D11 wait before reusing shared inputs");c11_->Flush();
        ++s.status.sourcePresents;
        if(latencySemaphore_.value) ReleaseSemaphore(latencySemaphore_.value,1,nullptr); // one presented frame retired
        virtualIndex_=next_virtual_buffer(virtualIndex_,clientDesc_.BufferCount,clientDesc_.SwapEffect);
        // A successful DXGI Present unbinds the render target in flip mode.
        if(clientDesc_.SwapEffect==DXGI_SWAP_EFFECT_FLIP_DISCARD || clientDesc_.SwapEffect==DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL)
            c11_->OMSetRenderTargets(0,nullptr,nullptr);
        return hr;
    }
    // FoA calls ResizeBuffers with the configuration already in effect - seven times in a minute of
    // Options use in one capture - and every call costs a GPU drain, a full rebuild of both the client
    // and presentation buffers, a DLSS-G off/on bracket that Streamline reports as a repeated
    // slDLSSGSetOptions, and a frame over 100 ms. Skipping one cannot break a client: not invalidating
    // its back buffer is weaker than invalidating it, and GetBuffer keeps returning the same texture at
    // the same size and format. It is skipped only when nothing could need re-synchronising - every
    // client-visible parameter unchanged, no display transition or Present status owing a resize, and a
    // presentation swapchain whose descriptor still reports the geometry it was given. That last check is
    // a sanity check on the descriptor, not proof that nothing needs re-synchronising; ResyncState is what
    // carries that, because a fullscreen or target-mode change requires a resize before any Present has
    // had the chance to report anything.
    bool redundant_resize(const DXGI_SWAP_CHAIN_DESC1& next,const ResizePlan& plan,UINT flags) {
        if(broken_ || !output_ || resync_.pending) return false;
        // Returning S_OK for a skipped resize must not hide a device that went away since the last
        // operation: the client would keep unusable resources until some later call noticed.
        if(d11_->GetDeviceRemovedReason()!=S_OK) return false;
        if(auto* d12=state().device.Get(); d12 && d12->GetDeviceRemovedReason()!=S_OK) return false;
        if(!resize_is_redundant(plan,flags,clientDesc_.BufferCount,clientDesc_.Width,clientDesc_.Height,
                                clientDesc_.Format,clientDesc_.Flags)) return false;
        DXGI_SWAP_CHAIN_DESC1 live{};
        if(FAILED(output_->GetDesc1(&live))) return false;
        // Geometry only: an FSR or XeSS presentation proxy owns its buffer count and flags and need
        // not report the ones the bridge asked for.
        return live.Width==next.Width && live.Height==next.Height &&
               live.Format==DXGI_FORMAT(output_format(next.Format));
    }
    HRESULT resize(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) {
        SLGuard own;
        // A bridge the game has already replaced must not resize: its buffers, counters and frame slots
        // now belong to its successor, and reporting success would be a lie a caller can act on, since
        // GetDesc1 and GetBuffer still describe and hand out the old buffers. Zero width, height,
        // format and buffer count each mean "keep" or "take from the window" rather than "no change",
        // so there is no reliable no-op to accept either. ReShade treats this error as leaving the
        // device usable and restores its previous description.
        if(surrendered(__func__)) return DXGI_ERROR_INVALID_CALL;
        RECT bounds{};if(!GetClientRect(hwnd_,&bounds))return HRESULT_FROM_WIN32(GetLastError());
        auto plan=plan_resize(count,width,height,format,clientDesc_.BufferCount,clientDesc_.Format,
            UINT(std::max<LONG>(0,bounds.right-bounds.left)),UINT(std::max<LONG>(0,bounds.bottom-bounds.top)));
        if(!plan.valid) return DXGI_ERROR_INVALID_CALL;
        if(flags&(DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE|DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED|DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT))return DXGI_ERROR_UNSUPPORTED;
        auto next=clientDesc_;next.Width=plan.width;next.Height=plan.height;next.Format=DXGI_FORMAT(plan.format);next.BufferCount=plan.count;next.Flags=flags;
        // DXGI is allowed to change the fullscreen state by itself; MakeWindowAssociation only stops
        // Alt+Enter. Such a transition owes a resize exactly as an explicit one does, and nothing else
        // here would notice it, so reconcile the live state with what the bridge last recorded.
        BOOL live=FALSE;
        if(FAILED(output_->GetFullscreenState(&live,nullptr))) resync_.display_transition();
        else if(bool(live)==bool(fullscreen_.Windowed)) { resync_.display_transition(); fullscreen_.Windowed=!live; }
        if(redundant_resize(next,plan,flags)) {
            auto& counted=state();
            if(++counted.resizesSkipped==1 || counted.resizesSkipped%50==0)
                log("Skipped a ResizeBuffers to the configuration already in effect ("+std::to_string(next.Width)+"x"+
                    std::to_string(next.Height)+", format "+std::to_string(uint32_t(next.Format))+"); "+
                    std::to_string(counted.resizesSkipped)+" so far");
            return S_OK;
        }
        if(generator_) generator_->quiesce();
        else { options_off(); for(auto& slot:state().frames.slots) if(slot.occupied)clear_tags(&slot.value); }
        drain();
        // Build replacements transactionally before touching the current chain.
        auto colors=make_colors(next);
        for(auto& b:backbuffers_)b.Reset();
        const UINT physicalFlags=flags&~UINT(DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT); // see create_output
        const auto result=output_->ResizeBuffers(3,plan.width,plan.height,DXGI_FORMAT(output_format(plan.format)),physicalFlags);
        if(FAILED(result)) {buffers();return result;}
        const bool changedFormat=next.Format!=clientDesc_.Format;
        clientDesc_=next;physicalFlags_=physicalFlags;colors_=std::move(colors);virtualIndex_=0;
        for(auto& slot:inputs_)slot=InputSlot{};
        forget_hudless_verification("the swapchain buffers changed");
        for(auto& slot:state().frames.slots)if(slot.occupied)state().frames.release(slot.id);
        haveRenderFrame=false;
        buffers();allocatorRetired_.fill(0);
        if(changedFormat)sourceSpace_=DXGI_COLOR_SPACE_TYPE(default_color_space(clientDesc_.Format));
        apply_color_space();state().status.sourceFormat=clientDesc_.Format;
        if(clientDesc_.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)state().status.bridgeFlags|=8;else state().status.bridgeFlags&=~8u;
        if(generator_) generator_->resized(plan.width,plan.height,DXGI_FORMAT(output_format(plan.format)));
        resync_.resized(); // whatever a display transition owed has now been paid
        ++state().resizes;
        log("DX11 bridge resized to "+std::to_string(plan.width)+"x"+std::to_string(plan.height));return S_OK;
    }
    template<class Fn> HRESULT guarded(Fn&& fn) noexcept {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        try {const HRESULT r=fn();state().status.lastInteropHresult=r;return r;}
        catch(const DxError& e){state().status.lastInteropHresult=e.hr;failure(Reason::InteropFailure,std::string(e.what())+" "+hexhr(e.hr),true);broken_=true;options_off();return e.hr;}
        catch(const std::exception& e){failure(Reason::InteropFailure,e.what(),true);broken_=true;return E_FAIL;}
        catch(...){failure(Reason::InteropFailure,"Unknown exception at D3D11 bridge boundary",true);broken_=true;return E_FAIL;}
    }
public:
    void initialize(ID3D11Device* game,IDXGIFactory* factory,HWND window,const DXGI_SWAP_CHAIN_DESC1& desc,
                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrictTo) {
        SLGuard own;auto& s=state();hwnd_=window;clientFactory_=factory;clientDesc_=desc;restrictTo_=restrictTo;
        if(full)fullscreen_=*full;else fullscreen_.Windowed=TRUE;
        RECT rect{};check(GetClientRect(window,&rect)?S_OK:HRESULT_FROM_WIN32(GetLastError()),"Read game window dimensions");
        if(!clientDesc_.Width)clientDesc_.Width=UINT(std::max<LONG>(1,rect.right-rect.left));
        if(!clientDesc_.Height)clientDesc_.Height=UINT(std::max<LONG>(1,rect.bottom-rect.top));
        sourceSpace_=DXGI_COLOR_SPACE_TYPE(default_color_space(clientDesc_.Format));
        check(game->QueryInterface(IID_PPV_ARGS(&d11_)),"Need ID3D11Device5 for shared fences");
        ComPtr<ID3D11DeviceContext> context;game->GetImmediateContext(&context);
        check(context.As(&c11_),"Need ID3D11DeviceContext4 for GPU Signal/Wait");
        {void* self=this;check(c11_->SetPrivateData(kImmediateContextTag,sizeof(self),&self),"Tag the immediate context");}
        // A D3D11 immediate context may only be used by one thread at a time, and the bridge uses the
        // game's: drain() signals and flushes on it from a present, a resize and a bridge handover,
        // which need not be the thread the game renders on. state().mutex serialises the bridge against
        // itself and says nothing about the game's own calls. Whether that is a hazard here depends on
        // whether the game turned multithread protection on, so record what it actually did.
        {
            ComPtr<ID3D11Multithread> multithread;
            if(SUCCEEDED(c11_.As(&multithread)))
                log(std::string("Game D3D11 immediate context multithread protection: ")+
                    (multithread->GetMultithreadProtected()?"on":"off"));
            else log("Game D3D11 immediate context does not expose ID3D11Multithread");
        }
        if(GetModuleHandleW(L"renodx-unityengine.addon64")) {
            auto** dv=*reinterpret_cast<void***>(d11_.Get());
            renoReplay_=add_hook(dv[15],reinterpret_cast<void*>(&reno_create_pixel_shader),reinterpret_cast<void**>(&realCreatePixelShader)) &&
                D3D11DrawObserver::install(c11_.Get(),add_hook,reno_after_draw);
            log(renoReplay_?"RenoDX Unity HUD-less final-pass replay installed; awaiting a recognized output shader":
                "RenoDX Unity HUD-less final-pass hooks unavailable");
        }
        const auto level=game->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
        check(d11_->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&scratch_),"Create isolated D3D11 pipeline state");
        ComPtr<IDXGIDevice> dxgi;check(game->QueryInterface(IID_PPV_ARGS(&dxgi)),"Get game DXGI device");
        ComPtr<IDXGIAdapter> adapter;check(dxgi->GetAdapter(&adapter),"Get game graphics adapter");
        DXGI_ADAPTER_DESC ad{};check(adapter->GetDesc(&ad),"Read game adapter LUID");
        if(s.device) {
            const auto luid=s.device->GetAdapterLuid();
            if(luid.HighPart!=ad.AdapterLuid.HighPart || luid.LowPart!=ad.AdapterLuid.LowPart)
                throw DxError(DXGI_ERROR_UNSUPPORTED,"A different adapter cannot reuse the Streamline instance");
            proxyDevice_=s.bridgeDeviceProxy;
        } else {
            s.status.gpuVendor=ad.VendorId;
            // Streamline hosts DLSS-G and Reflex on NVIDIA GPUs; otherwise a plain D3D12 device serves
            // FSR, XeSS and the other latency technologies.
            if(s.initialized && ad.VendorId==0x10DE) {
                check(s.sl.createDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&proxyDevice_)),"Create private D3D12 presentation device");
                on_device(proxyDevice_.Get());
            }
            if(!s.device) { proxyDevice_.Reset(); create_native_device(adapter.Get()); }
            if(s.slReady) s.bridgeDeviceProxy=proxyDevice_; else proxyDevice_.Reset();
            s.deviceReady=true;
            probe_vendor_backends(s.device.Get(),ad.VendorId);
            s.status.flags|=4u; // capability probes complete
        }
        D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        if(proxyDevice_) {
            // The proxy sees mandatory CreateCommandQueue; submissions use its native interface.
            check(proxyDevice_->CreateCommandQueue(&q,IID_PPV_ARGS(&proxyQueue_)),"Create private present queue");
            void* nativeQueue=nullptr;
            if(s.sl.slGetNativeInterface(proxyQueue_.Get(),&nativeQueue)!=sl::Result::eOk || !nativeQueue)throw DxError(E_FAIL,"Unwrap present queue");
            queue_.Attach(static_cast<ID3D12CommandQueue*>(nativeQueue));
        } else check(s.device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue_)),"Create private present queue");
        check(s.device->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&shared12_)),"Create cross-API fence");
        Handle fenceHandle;check(s.device->CreateSharedHandle(shared12_.Get(),nullptr,GENERIC_ALL,nullptr,&fenceHandle.value),"Export cross-API fence");
        check(d11_->OpenSharedFence(fenceHandle.value,IID_PPV_ARGS(&shared11_)),"Open cross-API fence on D3D11");
        event_.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event_.value)throw DxError(HRESULT_FROM_WIN32(GetLastError()),"Create fence notification event");
        for(UINT i=0;i<3;++i) {
            check(s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators_[i])),"Create bridge command allocator");
            check(s.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators_[i].Get(),nullptr,IID_PPV_ARGS(&lists_[i])),"Create bridge command list");
            check(lists_[i]->Close(),"Close initial command list");
        }
        shaders();colors_=make_colors(clientDesc_);
        forget_hudless_verification("a new bridge swapchain was created");
        waitable_=(clientDesc_.Flags&DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)!=0;
        QueryPerformanceFrequency(&qpcFrequency_);
        // Frame generation technologies other than DLSS switch to their own swapchain on the first request.
        create_output(Output::Standard);
        s.presentQueue=queue_;s.status.sourceApi=11;s.status.presentationApi=12;
        s.status.sourceFormat=clientDesc_.Format;
        s.status.bridgeFlags=1u|2u|4u|(clientDesc_.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?8u:0u);
        active=this;registered_=true;
        std::ostringstream flags; flags<<std::hex<<std::uppercase<<clientDesc_.Flags<<", GPU vendor 0x"<<s.status.gpuVendor;
        log("DX11 bridge ready: source format="+std::to_string(clientDesc_.Format)+" -> D3D12 format="+std::to_string(s.status.swapchainFormat)+
            "; same-adapter shared textures/fences; virtual buffers="+std::to_string(colors_.size())+"; output buffers=3; client flags=0x"+flags.str()+
            "; frame generation available: DLSS "+((s.status.backendsAvailable&2u)?"yes":"no")+", FSR "+((s.status.backendsAvailable&4u)?"yes":"no")+
            ", XeSS "+((s.status.backendsAvailable&8u)?"up to "+std::to_string(s.status.xessMaxGenerated)+" generated":std::string("no")));
    }
    HWND window() const { return hwnd_; }
    // Give up the shared pipeline now, rather than when the game gets round to releasing this object.
    // Unity replaces the swapchain instead of resizing it for some display changes, and it creates the
    // replacement before releasing the old one: without this, the new bridge is refused because one is
    // already active, and this one's destructor then clears `active`, so the game presents through its
    // own swapchain for the rest of the session and frame generation never returns. The destructor
    // calls this too, so the work happens exactly once either way.
    void retire() {
        // The lock comes before the idempotence check, not after: a destructor that set the flag and
        // then waited for the mutex would let a replacement skip an unfinished handover and hand the
        // successor state that this object is still about to clear.
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(retired_) return;
        retired_=true;
        renoPending.store(false,std::memory_order_relaxed);
        SLGuard own;
        if(registered_) {
            if(generator_) generator_->quiesce();
            else { options_off(); for(auto& slot:state().frames.slots)if(slot.occupied)clear_tags(&slot.value); }
        }
        bool idle=true;
        try {drain();}catch(const std::exception& e){idle=false;log(std::string("Bridge teardown: ")+e.what());}
        if(registered_ && active==this) {
            // Say so here rather than only where a replacement asks for it: a bridge also gives this up
            // when the game releases it, and that path was silent, which left a surrendered bridge with
            // no successor looking like nothing had happened at all.
            log("The bridge gave up the presentation swapchain; until the game releases it, calls on it "
                "are answered without one");
            auto& s=state();
            active=nullptr;s.swapchain=nullptr;s.presentQueue.Reset();s.status.bridgeFlags=0;
            for(auto& slot:s.frames.slots)if(slot.occupied)s.frames.release(slot.id);
            // Both belong to the output being given up, exactly as at a backend switch. The fence is
            // DLSS-G's, and an asynchronous error reported against this output must not be handed to
            // the successor, whose first present would otherwise consume it and go fatal.
            s.lastInputsFence.Reset(); s.lastInputsFenceValue=0;
            s.asyncApiError.store(S_OK,std::memory_order_relaxed);
            registered_=false;
        }
        // Presents and input capture stop here: a retired bridge never touches the pipeline again,
        // however long the game holds its reference.
        broken_=true;
        // A client that waits on the frame-latency object before each frame would block forever on a
        // swapchain that no longer presents, because nothing else will release its permit. Fill the
        // semaphore so anyone already waiting wakes and later waits pass straight through.
        if(latencySemaphore_.value) while(ReleaseSemaphore(latencySemaphore_.value,1,nullptr)) {}
        if(idle) {
            if(output_)output_->SetFullscreenState(FALSE,nullptr);
            fullscreen_.Windowed=TRUE; // GetDesc and GetFullscreenDesc must agree with GetFullscreenState
            for(auto& b:backbuffers_)b.Reset();output_.Reset();
            generator_.reset();
        } else {
            // A drain that did not complete proves nothing about the GPU, and a five-second wait_cpu
            // timeout is not completion. Freeing resources that submitted work may still reference
            // turns a stall into device removal, so this bridge keeps them until it is destroyed.
            log("Bridge retirement could not confirm the GPU was idle; its presentation resources are "
                "kept until the game releases the swapchain, and the replacement may fail to be created");
        }
        // Released before any replacement tags the same context, and only while it is still ours.
        if(c11_) {
            void* tagged=nullptr; UINT size=sizeof(tagged);
            if(SUCCEEDED(c11_->GetPrivateData(kImmediateContextTag,&size,&tagged)) && tagged==this)
                c11_->SetPrivateData(kImmediateContextTag,0,nullptr);
        }
    }
    ~Dx11Swapchain() { retire(); }
    ID3D11DeviceContext* immediate_context() {return c11_.Get();}
    // True when the context is our immediate context, directly or through a forwarding wrapper.
    bool same_context(ID3D11DeviceContext* context) {
        if(!context) return false;
        if(same_object(context,c11_.Get())) return true;
        void* tag=nullptr; UINT size=sizeof(tag);
        return SUCCEEDED(context->GetPrivateData(kImmediateContextTag,&size,&tag)) && size==sizeof(tag) && tag==this;
    }
    void replay_reno_final(ID3D11DeviceContext* context,ID3D11PixelShader* shader) {
        if(!renoReplay_ || !renoPending.load(std::memory_order_relaxed))return;
        // Report each rejected condition once, without per-draw log spam. A recognized
        // shader alone does not prove that the replay actually reaches a valid frame.
        const auto skipped=[this](uint32_t bit,const char* reason) {
            if(!(renoSkipReasons_&bit)) {
                renoSkipReasons_|=bit;
                log(std::string("RenoDX HUD-less final pass skipped: ")+reason);
            }
        };
        if(!same_context(context)) {skipped(1,"different immediate context");return;}
        auto& s=state();
        auto& slot=inputs_[renoFrameId_%inputs_.size()];
        if(!s.hudlessRequested || !haveRenderFrame || renderFrameId!=renoFrameId_ ||
           !slot.occupied || !slot.valid || slot.id!=renoFrameId_) {skipped(2,"no matching current frame inputs");return;}
        ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);
        if(!target) {skipped(4,"no render target");return;}
        ComPtr<ID3D11Resource> resource;target->GetResource(&resource);
        bool backbuffer=false;
        for(auto& color:colors_)backbuffer|=same_object(resource.Get(),color.source.d11.Get());
        if(!backbuffer) {skipped(8,"render target is not the source backbuffer");return;}
        UINT count=1;D3D11_VIEWPORT viewport{};context->RSGetViewports(&count,&viewport);
        if(count!=1 || viewport.TopLeftX!=0 || viewport.TopLeftY!=0 ||
           viewport.Width!=float(clientDesc_.Width) || viewport.Height!=float(clientDesc_.Height)) {
            skipped(16,"viewport does not cover the source backbuffer");return;
        }
        ComPtr<ID3D11Buffer> constants;UINT firstConstant=0,numConstants=0;
        c11_->PSGetConstantBuffers1(13,1,&constants,&firstConstant,&numConstants);
        ComPtr<ID3D11SamplerState> sampler;context->PSGetSamplers(0,1,&sampler);
        if(!constants || !numConstants || !sampler) {skipped(32,"missing live b13 constants or s0 sampler");return;}
        // Preserve the game's live constants (including preset, gamma, brightness and
        // gamut) by binding the exact buffer used by RenoDX's output draw. No CPU readback.
        ensure(renoOutput_,clientDesc_.Width,clientDesc_.Height,DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        {
            ContextScope scope(c11_.Get(),scratch_.Get());
            auto* rtv=renoOutput_.rtv.Get();c11_->OMSetRenderTargets(1,&rtv,nullptr);
            c11_->OMSetBlendState(nullptr,nullptr,UINT_MAX);c11_->OMSetDepthStencilState(noDepth_.Get(),0);
            c11_->RSSetViewports(1,&viewport);c11_->RSSetState(raster_.Get());
            c11_->IASetInputLayout(nullptr);c11_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            c11_->VSSetShader(renoVS_.Get(),nullptr,0);c11_->GSSetShader(nullptr,nullptr,0);
            c11_->HSSetShader(nullptr,nullptr,0);c11_->DSSetShader(nullptr,nullptr,0);
            c11_->PSSetShader(shader,nullptr,0);
            auto* cb=constants.Get();c11_->PSSetConstantBuffers1(13,1,&cb,&firstConstant,&numConstants);
            auto* smp=sampler.Get();c11_->PSSetSamplers(0,1,&smp);
            auto* view=renoRaw_.srv.Get();c11_->PSSetShaderResources(0,1,&view);
            c11_->Draw(3,0);
        }
        encode_hdr(renoOutput_.srv.Get(),slot.hudless);
        slot.hudlessValid=true;renoPending.store(false,std::memory_order_relaxed);
        if(++renoReplays_==1 || renoReplays_%600==0)
            log("RenoDX Unity HUD-less final pass replayed "+std::to_string(renoReplays_)+" times; frame "+std::to_string(renoFrameId_));
    }
    bool capture(uint32_t id,ID3D11DeviceContext* context,ID3D11Resource* depth,ID3D11Resource* motion,
                 RectU de,RectU me,const sl::Constants& constants) {
        if(broken_) return false;
        if(!same_context(context)) {
            state().status.reason=uint32_t(Reason::SourceDeviceMismatch);
            log_throttled("NGX context is not the presentation device's immediate context (an unrecognised graphics wrapper?)");
            return false;
        }
        if(context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        ComPtr<ID3D11Texture2D> dt,mt;check(depth->QueryInterface(IID_PPV_ARGS(&dt)),"Depth texture type");check(motion->QueryInterface(IID_PPV_ARGS(&mt)),"Motion texture type");
        D3D11_TEXTURE2D_DESC dd{},md{};dt->GetDesc(&dd);mt->GetDesc(&md);
        if(!fits(de,dd.Width,dd.Height) || !fits(me,md.Width,md.Height))return false;
        auto dv=source_srv(depth,true),mv=source_srv(motion,false);
        auto& slot=inputs_[id%inputs_.size()];slot.occupied=true;slot.id=id;slot.valid=false;slot.hudlessValid=false;
        ensure(slot.depth,de.width,de.height,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        // R16G16_FLOAT: HDRP writes motion vectors as R16G16_SFloat, so the copy is lossless, and it is
        // the standard DLSS-G motion format. R32G32_FLOAT is not NT-handle shareable on NVIDIA's D3D11
        // driver (CreateTexture2D returns E_INVALIDARG), which blocked every capture before.
        ensure(slot.motion,me.width,me.height,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
        // HDRP renders into Y-inverted RTs. Its final blit (and our HUD-less color copy) flips those
        // to display orientation. Every runtime samples depth and motion at the coordinates of the
        // frame it interpolates, which is the presented one, and none of them can be told that a
        // tagged texture is upside down - a projection matrix describes a sign convention, not
        // texture row order. So the rows follow the color for DLSS-G, FSR and XeSS alike, with the
        // vertical motion scale and jitter negated to match. TGFG_SetCamera turns the clip-space
        // matrices the same way, so the camera describes the space these buffers are in.
        copy_input(dv.Get(),slot.depth,de,depthCS_.Get());
        copy_input(mv.Get(),slot.motion,me,motionCS_.Get());
        slot.constants=constants;
        slot.constants.mvecScale.y=-slot.constants.mvecScale.y;
        slot.constants.jitterOffset.y=-slot.constants.jitterOffset.y;
        return true;
    }
    void finish(uint32_t id,bool success) {auto& slot=inputs_[id%inputs_.size()];if(slot.occupied&&slot.id==id)slot.valid=success;}
    // The frame's HUD-less image, in the client's size and format, copied (scRGB: PQ-encoded) into the
    // slot's shareable texture, which has the output swapchain's format like the presented frame.
    int hudless(uint32_t id,ID3D11Texture2D* texture) {
        auto& slot=inputs_[id%inputs_.size()];if(!slot.occupied || slot.id!=id || !slot.valid)return -2;
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        if(d.Width!=clientDesc_.Width || d.Height!=clientDesc_.Height || unorm_format(d.Format)!=unorm_format(clientDesc_.Format) ||
           d.SampleDesc.Count!=1 || d.ArraySize!=1 || d.MipLevels!=1)return -3;
        ensure(slot.hudless,d.Width,d.Height,DXGI_FORMAT(output_format(d.Format)),D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            ComPtr<ID3D11ShaderResourceView> view;check(d11_->CreateShaderResourceView(texture,nullptr,&view),"HUD-less SRV");encode_hdr(view.Get(),slot.hudless);
        } else c11_->CopyResource(slot.hudless.d11.Get(),texture);
        slot.hudlessValid=true;return 0;
    }
    int hudless(uint32_t id,ID3D11DeviceContext* context,ID3D11Resource* color) {
        if(!same_context(context) || !color)return -1;
        ComPtr<ID3D11Texture2D> texture;check(color->QueryInterface(IID_PPV_ARGS(&texture)),"HUD-less texture type");
        return hudless(id,texture.Get());
    }
    // Render event 4 is embedded in HDRP's AfterPostProcess pass, while its color is still alive.
    // Match the final blit's viewport, Y flip and transfer function before comparing or tagging it.
    int hudless_frame(const TGFG_Hudless& packet) {
        if(broken_)return -1;
        auto& slot=inputs_[packet.frameId%inputs_.size()];
        if(!slot.occupied || slot.id!=packet.frameId || !slot.valid)return -2;
        slot.hudlessValid=false;
        auto* resource=reinterpret_cast<ID3D11Resource*>(uintptr_t(packet.color));
        ComPtr<ID3D11Device> device;resource->GetDevice(&device);
        if(!same_object(device.Get(),d11_.Get())) {
            // ReShade makes resource->GetDevice return its proxy device, whose COM
            // identity differs from the underlying presentation device. Resolve its
            // immediate context and use the same forwarded private-data identity as
            // the working NGX depth/motion capture. An unrelated device still fails.
            ComPtr<ID3D11DeviceContext> context;
            if(device)device->GetImmediateContext(&context);
            if(!same_context(context.Get()))return -4;
        }
        ComPtr<ID3D11Texture2D> texture;check(resource->QueryInterface(IID_PPV_ARGS(&texture)),"HDRP color texture type");
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        if(packet.width!=clientDesc_.Width || packet.height!=clientDesc_.Height ||
           !fits({0,0,packet.width,packet.height},d.Width,d.Height) || d.SampleDesc.Count!=1 || d.ArraySize!=1)return -3;
        const bool pq=output_color_space(clientDesc_.Format,sourceSpace_)==12;
        if((packet.flags&2u) && !pq)return -5; // don't mistake HDR nits for SDR linear light
        DXGI_FORMAT format=d.Format;
        // Unity uses typeless resources with typed views; preserve its sRGB decode when applicable.
        switch(format) {
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: format=DXGI_FORMAT_R16G16B16A16_FLOAT; break;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            format=(packet.flags&8u)?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            format=(packet.flags&8u)?DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:DXGI_FORMAT_B8G8R8A8_UNORM; break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R11G11B10_FLOAT: break;
        default:return -6;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=format;
        desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;desc.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> view;check(d11_->CreateShaderResourceView(resource,&desc,&view),"HDRP color SRV");
        ensure(slot.hudless,packet.width,packet.height,DXGI_FORMAT(output_format(clientDesc_.Format)),D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        if(renoReplay_ && pq && !(packet.flags&2u)) {
            // RenoDX Unity finishes its output conversion during ReShade's final
            // backbuffer copy, after HDRP's pre-UI capture. Retain scene color until
            // that draw supplies its actual shader and live settings, then PQ encode.
            ensure(renoRaw_,packet.width,packet.height,DXGI_FORMAT_R16G16B16A16_FLOAT,
                D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
            const uint32_t rawParams[]={packet.flags&1u,4u,packet.width,packet.height};
            encode_hdr(view.Get(),renoRaw_,rawParams);
            renoFrameId_=packet.frameId;renoPending.store(true,std::memory_order_relaxed);
            return 0;
        }
        const uint32_t mode=!pq?0u:!(packet.flags&2u)?1u:(packet.flags&4u)?3u:2u;
        const uint32_t params[]={packet.flags&1u,mode,packet.width,packet.height};
        encode_hdr(view.Get(),slot.hudless,params);
        slot.hudlessValid=true;return 0;
    }
    // D3D11-facing swapchain methods, with a stable logical buffer zero. Unsupported
    // indexed-buffer/dirty-rectangle clients are rejected. Unknown QI requests are NOT
    // forwarded to the D3D12 chain: doing that would leak a second COM identity.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDXGIObject)||id==__uuidof(IDXGIDeviceSubObject)||id==__uuidof(IDXGISwapChain)||
           id==__uuidof(IDXGISwapChain1)||id==__uuidof(IDXGISwapChain2)||id==__uuidof(IDXGISwapChain3)||id==__uuidof(IDXGISwapChain4)) {
            *out=static_cast<IDXGISwapChain4*>(this);AddRef();return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs_;}
    // Destruction is serialised with the registry: retirement runs under state().mutex and takes
    // time (quiesce, drain), so a final Release on another thread could otherwise delete the object
    // out from under it. Holding the mutex across the decrement makes the last reference and the
    // handover mutually exclusive.
    ULONG STDMETHODCALLTYPE Release() override {
        std::unique_lock<std::recursive_mutex> lock(state().mutex);
        const auto n=--refs_;
        // Retire while still holding the lock, so the registry no longer points at this object before
        // anyone else can run; the destructor's own call then finds the work already done.
        if(!n) { retire(); lock.unlock(); delete this; }
        return n;
    }
    // A bridge that has given up its presentation swapchain still answers COM calls: the game holds
    // its reference until it chooses to release it, and Unity queries a swapchain it is about to drop
    // - the crash that found this was GetFullscreenState arriving after retirement. Every method that
    // forwards to the presentation swapchain therefore checks first, because after retirement there is
    // nothing to forward to and guarded() cannot help: a null dereference is not a C++ exception.
    bool surrendered(const char* method) const {
        if(!retired_ && output_) return false;
        log_throttled(std::string("The game called ")+method+" on a bridge that has given up its "
                      "presentation swapchain; answering without one");
        return true;
    }
    // The client's private data belongs to the object the client holds, not to whichever presentation
    // swapchain happens to be behind it. Forwarding it meant every backend switch, which replaces that
    // swapchain, and every retirement silently discarded it. ReShade keeps its proxy pointer here, so
    // losing the association breaks a swapchain the game is still using.
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID name,UINT size,const void* data) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        privateInterfaces_.erase(name);
        if(!data || !size) { privateData_.erase(name); return S_OK; }
        const auto* bytes=static_cast<const uint8_t*>(data);
        privateData_[name].assign(bytes,bytes+size);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID name,const IUnknown* unknown) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        privateData_.erase(name);
        if(!unknown) { privateInterfaces_.erase(name); return S_OK; }
        privateInterfaces_[name]=const_cast<IUnknown*>(unknown); // the ComPtr takes the reference
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID name,UINT* size,void* data) override {
        if(!size) return E_INVALIDARG;
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        auto answer=[&](const void* from,UINT need)->HRESULT {
            if(!data) { *size=need; return S_OK; }
            if(*size<need) { *size=need; return DXGI_ERROR_MORE_DATA; }
            *size=need; std::memcpy(data,from,need); return S_OK;
        };
        if(auto it=privateInterfaces_.find(name); it!=privateInterfaces_.end()) {
            auto* raw=it->second.Get();
            const auto hr=answer(&raw,UINT(sizeof(raw)));
            if(hr==S_OK && data) raw->AddRef(); // the caller owns the reference it was handed
            return hr;
        }
        if(auto it=privateData_.find(name); it!=privateData_.end()) return answer(it->second.data(),UINT(it->second.size()));
        *size=0; return DXGI_ERROR_NOT_FOUND;
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override {return clientFactory_->QueryInterface(id,out);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out) override {return d11_->QueryInterface(id,out);}
    HRESULT STDMETHODCALLTYPE Present(UINT sync,UINT flags) override {return guarded([&]{return present(sync,flags,nullptr);});}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(index>=colors_.size()) {log("Unsupported client GetBuffer index="+std::to_string(index));return DXGI_ERROR_INVALID_CALL;}
        return colors_[index].source.d11->QueryInterface(id,out);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL value,IDXGIOutput* target) override {
        return guarded([&]{
            if(surrendered(__func__))return HRESULT(DXGI_ERROR_INVALID_CALL);
            pause_generation();drain();resync_.display_transition();
            const auto r=output_->SetFullscreenState(value,target);if(SUCCEEDED(r))fullscreen_.Windowed=!value;return r;});
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* value,IDXGIOutput** out) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(surrendered(__func__)) { if(value)*value=FALSE; if(out)*out=nullptr; return S_OK; }
        return output_->GetFullscreenState(value,out);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* d) override {
        if(!d)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(state().mutex);*d={};
        d->BufferDesc.Width=clientDesc_.Width;d->BufferDesc.Height=clientDesc_.Height;d->BufferDesc.Format=clientDesc_.Format;
        d->BufferDesc.RefreshRate=fullscreen_.RefreshRate;d->BufferDesc.ScanlineOrdering=fullscreen_.ScanlineOrdering;d->BufferDesc.Scaling=fullscreen_.Scaling;
        d->SampleDesc=clientDesc_.SampleDesc;d->BufferUsage=clientDesc_.BufferUsage;d->BufferCount=clientDesc_.BufferCount;
        d->OutputWindow=hwnd_;d->Windowed=fullscreen_.Windowed;d->SwapEffect=clientDesc_.SwapEffect;d->Flags=clientDesc_.Flags;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags) override {return guarded([&]{return resize(n,w,h,f,flags);});}
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* desc) override {
        if(!desc)return E_POINTER;return guarded([&]{
            if(surrendered(__func__))return HRESULT(DXGI_ERROR_INVALID_CALL);
            pause_generation();drain();resync_.display_transition();
            auto d=*desc;d.Format=DXGI_FORMAT(output_format(d.Format==DXGI_FORMAT_UNKNOWN?clientDesc_.Format:d.Format));
            return output_->ResizeTarget(&d);});
    }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** out) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!out)return E_POINTER;if(surrendered(__func__)){*out=nullptr;return DXGI_ERROR_INVALID_CALL;}
        return output_->GetContainingOutput(out);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* out) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(surrendered(__func__))return DXGI_ERROR_INVALID_CALL;return output_->GetFrameStatistics(out);}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* out) override {if(!out)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(state().mutex);*out=UINT(state().status.sourcePresents);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* d) override {if(!d)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(state().mutex);*d=clientDesc_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* d) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!d)return E_POINTER;if(surrendered(__func__)){*d=fullscreen_;return S_OK;}
        return output_->GetFullscreenDesc(d);}
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* w) override {if(!w)return E_POINTER;*w=hwnd_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID,void** out) override {if(!out)return E_POINTER;*out=nullptr;return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE Present1(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* p) override {return guarded([&]{return present(sync,flags,p);});}
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(surrendered(__func__))return FALSE;return output_->IsTemporaryMonoSupported();}
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** out) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!out)return E_POINTER;if(surrendered(__func__)){*out=nullptr;return S_OK;}
        return output_->GetRestrictToOutput(out);}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* c) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(surrendered(__func__))return DXGI_ERROR_INVALID_CALL;return output_->SetBackgroundColor(c);}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* c) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(surrendered(__func__))return DXGI_ERROR_INVALID_CALL;return output_->GetBackgroundColor(c);}
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION r) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(r!=DXGI_MODE_ROTATION_IDENTITY)return DXGI_ERROR_UNSUPPORTED;
        if(surrendered(__func__))return DXGI_ERROR_INVALID_CALL;return output_->SetRotation(r);}
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* r) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!r)return E_POINTER;if(surrendered(__func__)){*r=DXGI_MODE_ROTATION_IDENTITY;return S_OK;}
        return output_->GetRotation(r);}
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w,UINT h) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(w!=clientDesc_.Width||h!=clientDesc_.Height)return DXGI_ERROR_UNSUPPORTED;
        if(surrendered(__func__))return DXGI_ERROR_INVALID_CALL;return output_->SetSourceSize(w,h);}
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* w,UINT* h) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(surrendered(__func__)){if(w)*w=clientDesc_.Width;if(h)*h=clientDesc_.Height;return S_OK;}
        return output_->GetSourceSize(w,h);}
    // Frame-latency object semantics for a client that created its swapchain with the waitable flag.
    // It must stay valid while the presentation swapchain is recreated for another frame generation
    // technology, so it is a bridge-owned semaphore released once per presented frame.
    HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT n) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!waitable_ || n<1 || n>kMaxLatency) return DXGI_ERROR_INVALID_CALL;
        maxLatency_=n; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* n) override {
        if(!n)return E_POINTER; std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!waitable_) return DXGI_ERROR_INVALID_CALL;
        *n=maxLatency_; return S_OK;
    }
    HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!waitable_) return nullptr;
        if(!latencySemaphore_.value) {
            latencySemaphore_.value=CreateSemaphoreW(nullptr,LONG(maxLatency_),LONG(kMaxLatency),nullptr);
            log("The game waits on a frame-latency object; the bridge provides it (maximum latency "+std::to_string(maxLatency_)+")");
        }
        HANDLE copy=nullptr;
        if(!latencySemaphore_.value || !DuplicateHandle(GetCurrentProcess(),latencySemaphore_.value,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS)) return nullptr;
        return copy;
    }
    HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* m) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!m)return E_POINTER;if(m->_11!=1||m->_22!=1||m->_12||m->_21||m->_31||m->_32)return DXGI_ERROR_UNSUPPORTED;
        if(surrendered(__func__))return DXGI_ERROR_INVALID_CALL;return output_->SetMatrixTransform(m);
    }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* m) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(surrendered(__func__))return DXGI_ERROR_INVALID_CALL;return output_->GetMatrixTransform(m);}
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override {std::lock_guard<std::recursive_mutex> lock(state().mutex);return virtualIndex_;}
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE cs,UINT* flags) override {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(!flags)return E_POINTER;*flags=0;if(!source_color_supported(clientDesc_.Format,cs))return S_OK;
        if(surrendered(__func__))return S_OK; // no support to report without a presentation swapchain
        return output_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE(output_color_space(clientDesc_.Format,cs)),flags);
    }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE cs) override {
        return guarded([&]{if(!source_color_supported(clientDesc_.Format,cs))return HRESULT(DXGI_ERROR_UNSUPPORTED);
            if(surrendered(__func__))return HRESULT(DXGI_ERROR_INVALID_CALL);
            UINT support=0;auto out=DXGI_COLOR_SPACE_TYPE(output_color_space(clientDesc_.Format,cs));
            auto hr=output_->CheckColorSpaceSupport(out,&support);if(FAILED(hr))return hr;
            if(!(support&DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))return DXGI_ERROR_UNSUPPORTED;
            hr=output_->SetColorSpace1(out);if(SUCCEEDED(hr)){sourceSpace_=cs;state().status.outputColorSpace=out;}return hr;});
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags,const UINT* masks,IUnknown*const* queues) override {
        if(masks||queues)return DXGI_ERROR_UNSUPPORTED;return ResizeBuffers(n,w,h,f,flags);
    }
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE type,UINT size,void* data) override {
        return guarded([&]{
            if(surrendered(__func__))return HRESULT(DXGI_ERROR_INVALID_CALL);
            const auto hr=output_->SetHDRMetaData(type,size,data);
            if(SUCCEEDED(hr)) { const auto* bytes=static_cast<const uint8_t*>(data); hdrType_=type; hdrMetadata_.assign(bytes,bytes+(data?size:0)); }
            // AMD FSR takes its maximum luminance from this when the game sets it, and divides by it.
            if(type==DXGI_HDR_METADATA_TYPE_HDR10 && data && size>=sizeof(DXGI_HDR_METADATA_HDR10)) {
                const auto& m=*static_cast<const DXGI_HDR_METADATA_HDR10*>(data);
                log("The game set HDR10 metadata: mastering luminance "+std::to_string(m.MinMasteringLuminance)+" to "+
                    std::to_string(m.MaxMasteringLuminance)+" (0.0001 nit / 1 nit units), max content light level "+
                    std::to_string(m.MaxContentLightLevel)+", frame average "+std::to_string(m.MaxFrameAverageLightLevel)+
                    ". AMD FSR linearises the frame against the maximum"+
                    (m.MaxMasteringLuminance==0?"; it is zero, which leaves FSR without a usable range":""));
            } else if(SUCCEEDED(hr)) log("The game set HDR metadata of type "+std::to_string(uint32_t(type))+", "+std::to_string(size)+" bytes");
            return hr;
        });
    }
};
HRESULT STDMETHODCALLTYPE reno_create_pixel_shader(ID3D11Device* device,const void* code,SIZE_T size,
    ID3D11ClassLinkage* linkage,ID3D11PixelShader** out) {
    const auto hr=realCreatePixelShader(device,code,size,linkage,out);
    if(SUCCEEDED(hr) && out && *out && is_renodx_final_pass(code,size)) {
        const UINT tag=1;const auto tagged=(*out)->SetPrivateData(kRenoFinalTag,sizeof(tag),&tag);
        log(SUCCEEDED(tagged)?"Recognized RenoDX Unity final-output shader for HUD-less replay":
            "Recognized RenoDX Unity shader, but attaching its replay tag failed");
    }
    return hr;
}
void reno_after_draw(ID3D11DeviceContext* context,D3D11DrawObserver::Call call) noexcept {
    if(replayingReno)return;
    renoDrawCalls[call.kind].fetch_add(1,std::memory_order_relaxed);
    // ExecuteCommandList may restore arbitrary old state, so count it for diagnostics
    // but never infer the executed shader from its post-call pipeline bindings.
    if(call.kind==D3D11DrawObserver::Execute)return;
    if(!renoPending.load(std::memory_order_relaxed) && renoDrawSeen.load(std::memory_order_relaxed))return;
    try {
        ComPtr<ID3D11PixelShader> shader;context->PSGetShader(&shader,nullptr,nullptr);
        UINT tag=0,size=sizeof(tag);
        if(!shader || FAILED(shader->GetPrivateData(kRenoFinalTag,&size,&tag)) || size!=sizeof(tag) || tag!=1)return;
        if(!renoDrawSeen.exchange(true,std::memory_order_relaxed)) {
            log("Observed RenoDX final-output draw: kind="+std::to_string(call.kind)+" count="+std::to_string(call.count)+
                " instances="+std::to_string(call.instances)+" contextType="+std::to_string(context->GetType())+
                " capturePending="+std::to_string(renoPending.load(std::memory_order_relaxed)));
        }
        // Fullscreen copies can use a triangle, quad, indexed mesh, or indirect draw.
        // The recognized pixel shader and full backbuffer/viewport identify the pass;
        // the replay uses its own fullscreen triangle, independently of game geometry.
        if(call.kind<=D3D11DrawObserver::IndexedInstanced && (!call.count || call.instances!=1))return;
        replayingReno=true;
        std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(active)active->replay_reno_final(context,shader.Get());
    } catch(const std::exception& e) {
        renoPending.store(false,std::memory_order_relaxed);
        log_throttled(std::string("RenoDX HUD-less final-pass replay failed: ")+e.what());
    } catch(...) {renoPending.store(false,std::memory_order_relaxed);}
    replayingReno=false;
}
} // namespace

bool is_game_window(HWND window,UINT width,UINT height) {
    DWORD pid=0;if(!window || !GetWindowThreadProcessId(window,&pid) || pid!=GetCurrentProcessId())return false;
    if(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD)return false;
    RECT r{};if(!GetClientRect(window,&r))return false;
    if(!width)width=UINT(std::max<LONG>(0,r.right-r.left));if(!height)height=UINT(std::max<LONG>(0,r.bottom-r.top));
    return width>=320 && height>=200; // skip runtime probe windows; first eligible game HWND only
}
HRESULT try_create_dx11_bridge(IUnknown* device,IDXGIFactory* factory,HWND hwnd,const DXGI_SWAP_CHAIN_DESC1& desc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* full,IDXGIOutput* restrictTo,IDXGISwapChain1** out) {
    if(!out)return E_POINTER;*out=nullptr;
    if(insideSL || !device || !factory || !is_game_window(hwnd,desc.Width,desc.Height))return S_FALSE;
    auto& s=state();std::lock_guard<std::recursive_mutex> lock(s.mutex);
    if(!s.bridgeAllowed)return S_FALSE;
    if(active && active->window()!=hwnd) return S_FALSE; // another window is not ours to take over
    if(desc.SampleDesc.Count!=1 || desc.Stereo || desc.BufferCount<1 || desc.BufferCount>16 ||
       desc.SwapEffect==DXGI_SWAP_EFFECT_SEQUENTIAL || !source_format_supported(desc.Format) ||
       (desc.AlphaMode!=DXGI_ALPHA_MODE_UNSPECIFIED && desc.AlphaMode!=DXGI_ALPHA_MODE_IGNORE) ||
       (desc.Flags&(DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE|DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED|DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT))) {
        failure(Reason::UnsupportedSourceLayout,"D3D11 swapchain layout is outside the supported bridge contract; using original creation path");return S_FALSE;
    }
    ComPtr<ID3D11Device> game;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&game))))return S_FALSE;
    s.status.sourceApi=11;
    // Everything that can refuse this request has now refused it, so the handover cannot throw away a
    // working bridge for a swapchain that was never going to be served. It still has to happen before
    // the replacement is created: DXGI allows only one flip-model swapchain per window.
    if(active) {
        log("The game replaced its swapchain for this window; retiring the previous bridge so the new one can take over");
        active->retire();
    }
    try {
        auto bridge=std::make_unique<Dx11Swapchain>();bridge->initialize(game.Get(),factory,hwnd,desc,full,restrictTo);
        *out=bridge.release();s.status.reason=uint32_t(Reason::BridgeReady);return S_OK;
    } catch(const DxError& e) {s.status.lastInteropHresult=e.hr;failure(Reason::InteropFailure,std::string(e.what())+" "+hexhr(e.hr));return e.hr;}
      catch(const std::exception& e) {failure(Reason::InteropFailure,e.what());return E_FAIL;}
}
bool capture_dx11_inputs(uint32_t id,ID3D11DeviceContext* context,ID3D11Resource* depth,ID3D11Resource* motion,
    RectU de,RectU me,const sl::Constants& constants) {
    std::lock_guard<std::recursive_mutex> lock(state().mutex);
    if(!active || !context || !depth || !motion)return false;
    // Capture runs once per frame: repeat failures are logged at most every 5 s per message.
    static std::string lastMessage; static uint64_t lastLogged=0;
    auto report=[](const std::string& message){
        auto& s=state(); s.status.reason=uint32_t(Reason::InputContractInvalid);
        const uint64_t now=GetTickCount64();
        if(message!=lastMessage || now-lastLogged>=5000) { lastMessage=message; lastLogged=now; log("Input capture failed: "+message); }
    };
    try{return active->capture(id,context,depth,motion,de,me,constants);}
    catch(const DxError& e){state().status.lastInteropHresult=e.hr;report(std::string(e.what())+" "+hexhr(e.hr));return false;}
    catch(const std::exception& e){report(e.what());return false;}
}
bool capture_dx11_inputs_immediate(uint32_t id,ID3D11Resource* depth,ID3D11Resource* motion,
    RectU de,RectU me,const sl::Constants& constants) {
    std::lock_guard<std::recursive_mutex> lock(state().mutex);
    return active && capture_dx11_inputs(id,active->immediate_context(),depth,motion,de,me,constants);
}
void finish_dx11_capture(uint32_t id,bool success) {std::lock_guard<std::recursive_mutex> lock(state().mutex);if(active)active->finish(id,success);}
int capture_dx11_hudless(uint32_t id,ID3D11DeviceContext* context,ID3D11Resource* color) {
    std::lock_guard<std::recursive_mutex> lock(state().mutex);if(!active)return -10;
    try{return active->hudless(id,context,color);}catch(const std::exception& e){log(e.what());return -11;}
}
int capture_dx11_hudless_frame(const TGFG_Hudless& packet) {
    std::lock_guard<std::recursive_mutex> lock(state().mutex);if(!active)return -10;
    try{return active->hudless_frame(packet);}catch(const std::exception& e){log_throttled(std::string("HUD-less capture failed: ")+e.what());return -11;}
}
void shutdown_dx11_bridge() { // Explicit off, no unsafe live DLL unload.
    std::lock_guard<std::recursive_mutex> lock(state().mutex);options_off();
}
} // namespace tgfg
