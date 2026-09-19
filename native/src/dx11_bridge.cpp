// SPDX-License-Identifier: MIT
#include "dx11_bridge.hpp"
#include "fg_backends.hpp"
#include "interop_shader.hpp"
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
struct ColorBuffer { SharedTexture source,encoded; };
struct InputSlot {
    uint32_t id=0;bool occupied=false,valid=false,hudlessValid=false;
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
        ID3D11ShaderResourceView* emptySRV=nullptr;ID3D11UnorderedAccessView* emptyUAV=nullptr;
        context->CSSetShaderResources(0,1,&emptySRV);context->PSSetShaderResources(0,1,&emptySRV);
        context->CSSetUnorderedAccessViews(0,1,&emptyUAV,nullptr);
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
    ComPtr<ID3D11ComputeShader> depthCS_,motionCS_;
    ComPtr<ID3D11VertexShader> colorVS_;
    ComPtr<ID3D11PixelShader> hdrPS_;
    ComPtr<ID3D11Buffer> copyCB_;
    ComPtr<ID3D11RasterizerState> raster_;
    ComPtr<ID3D11DepthStencilState> noDepth_;
    DXGI_SWAP_CHAIN_DESC1 clientDesc_{};
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_{};
    HWND hwnd_=nullptr;
    UINT virtualIndex_=0,physicalFlags_=0;
    DXGI_COLOR_SPACE_TYPE sourceSpace_=DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    bool broken_=false,registered_=false;

    void wait_cpu(uint64_t value) {
        if(!value)return;
        if(shared12_->GetCompletedValue()==UINT64_MAX) throw DxError(DXGI_ERROR_DEVICE_REMOVED,"Shared fence device removed");
        if(shared12_->GetCompletedValue()>=value)return;
        check(shared12_->SetEventOnCompletion(value,event_.value),"Schedule shared fence CPU notification");
        if(WaitForSingleObject(event_.value,5000)!=WAIT_OBJECT_0) throw DxError(DXGI_ERROR_DEVICE_HUNG,"Interop fence timeout");
    }
    void drain() {
        if(!queue_ || !shared12_ || !shared11_ || !event_.value || !c11_) return;
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
        check(d11_->CreateComputeShader(d->GetBufferPointer(),d->GetBufferSize(),nullptr,&depthCS_),"Create depth conversion shader");
        auto m=compile("TGFG_MOTION","main","cs_5_0");
        check(d11_->CreateComputeShader(m->GetBufferPointer(),m->GetBufferSize(),nullptr,&motionCS_),"Create motion conversion shader");
        auto vs=compile("TGFG_HDR","vsMain","vs_5_0"),ps=compile("TGFG_HDR","psMain","ps_5_0");
        check(d11_->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&colorVS_),"Create conversion VS");
        check(d11_->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&hdrPS_),"Create scRGB-to-HDR10 PS");
        D3D11_BUFFER_DESC cb{};cb.ByteWidth=16;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(d11_->CreateBuffer(&cb,nullptr,&copyCB_),"Create copy constants");
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
    void encode_hdr(ID3D11ShaderResourceView* source,SharedTexture& target) {
        ContextScope scope(c11_.Get(),scratch_.Get());
        auto* rtv=target.rtv.Get();c11_->OMSetRenderTargets(1,&rtv,nullptr);
        c11_->OMSetBlendState(nullptr,nullptr,UINT_MAX);c11_->OMSetDepthStencilState(noDepth_.Get(),0);
        D3D11_VIEWPORT v{0,0,float(target.width),float(target.height),0,1};
        c11_->RSSetViewports(1,&v);c11_->RSSetState(raster_.Get());
        c11_->IASetInputLayout(nullptr);c11_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c11_->VSSetShader(colorVS_.Get(),nullptr,0);c11_->GSSetShader(nullptr,nullptr,0);
        c11_->HSSetShader(nullptr,nullptr,0);c11_->DSSetShader(nullptr,nullptr,0);
        c11_->PSSetShader(hdrPS_.Get(),nullptr,0);c11_->PSSetShaderResources(0,1,&source);c11_->Draw(3,0);
    }
    std::vector<ColorBuffer> make_colors(const DXGI_SWAP_CHAIN_DESC1& d) {
        std::vector<ColorBuffer> colors(virtual_buffer_count(d.BufferCount,d.SwapEffect));
        for(auto& c:colors) {
            c.source=SharedTexture::make(d11_.Get(),state().device.Get(),d.Width,d.Height,d.Format,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
            if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)
                c.encoded=SharedTexture::make(d11_.Get(),state().device.Get(),d.Width,d.Height,DXGI_FORMAT_R10G10B10A2_UNORM,D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE);
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
        sl::ResourceTag tags[]={
            {&d,sl::kBufferTypeDepth,sl::ResourceLifecycle::eValidUntilPresent,&de},
            {&m,sl::kBufferTypeMotionVectors,sl::ResourceLifecycle::eValidUntilPresent,&me},
            {slot.hudlessValid?&h:nullptr,sl::kBufferTypeHUDLessColor,sl::ResourceLifecycle::eValidUntilPresent,&he},
            {nullptr,sl::kBufferTypeUIColorAndAlpha,sl::ResourceLifecycle::eValidUntilPresent}};
        // The D3D12 queue already waits on the D3D11 producer signal. Inputs are
        // not reused by D3D11 until the consumer handoff at the end of Present.
        r=s.sl.slSetTagForFrame(*frame.token,s.viewport,tags,4,nullptr);
        frame.inputsSubmitted=r==sl::Result::eOk;
        if(r==sl::Result::eOk) ++s.diag.tagged;
        else {
            s.status.lastSetOptionsResult=int32_t(r);++s.diag.tagFailed;
            if(!s.diag.tagFailLogged){s.diag.tagFailLogged=true;log("slSetTagForFrame returned "+std::to_string(int(r)));}
        }
        if(slot.hudlessValid)s.status.bridgeFlags|=16u;else s.status.bridgeFlags&=~16u;
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
        if(enable && recorded) ++s.diag.tagged;
        note_generation(frame,why,enable && recorded);
    }
    HRESULT present(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* params) {
        SLGuard own; // private D3D12 calls are never rediscovered as game calls
        PresentDepthScope depth;
        if(!depth.entered) {
            log("Recursive bridge Present detected; rejecting re-entry");
            return DXGI_ERROR_INVALID_CALL;
        }
        if(broken_) return DXGI_ERROR_DEVICE_REMOVED;
        if(flags&DXGI_PRESENT_TEST) return output_->Present(sync,flags);
        // A nonblocking Present cannot be emulated by a path that enqueues copies
        // before knowing whether Present will accept them. Do not consume a frame.
        if(flags&DXGI_PRESENT_DO_NOT_WAIT) return DXGI_ERROR_WAS_STILL_DRAWING;
        if(params && (params->DirtyRectsCount || params->pScrollRect || params->pScrollOffset))
            return DXGI_ERROR_UNSUPPORTED;
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
    HRESULT resize(UINT count,UINT width,UINT height,DXGI_FORMAT format,UINT flags) {
        SLGuard own;
        RECT bounds{};if(!GetClientRect(hwnd_,&bounds))return HRESULT_FROM_WIN32(GetLastError());
        auto plan=plan_resize(count,width,height,format,clientDesc_.BufferCount,clientDesc_.Format,
            UINT(std::max<LONG>(0,bounds.right-bounds.left)),UINT(std::max<LONG>(0,bounds.bottom-bounds.top)));
        if(!plan.valid) return DXGI_ERROR_INVALID_CALL;
        if(flags&(DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE|DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED|DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT))return DXGI_ERROR_UNSUPPORTED;
        auto next=clientDesc_;next.Width=plan.width;next.Height=plan.height;next.Format=DXGI_FORMAT(plan.format);next.BufferCount=plan.count;next.Flags=flags;
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
        for(auto& slot:state().frames.slots)if(slot.occupied)state().frames.release(slot.id);
        haveRenderFrame=false;
        buffers();allocatorRetired_.fill(0);
        if(changedFormat)sourceSpace_=DXGI_COLOR_SPACE_TYPE(default_color_space(clientDesc_.Format));
        apply_color_space();state().status.sourceFormat=clientDesc_.Format;
        if(clientDesc_.Format==DXGI_FORMAT_R16G16B16A16_FLOAT)state().status.bridgeFlags|=8;else state().status.bridgeFlags&=~8u;
        if(generator_) generator_->resized(plan.width,plan.height,DXGI_FORMAT(output_format(plan.format)));
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
    ~Dx11Swapchain() {
        std::lock_guard<std::recursive_mutex> lock(state().mutex);SLGuard own;
        if(registered_) {
            if(generator_) generator_->quiesce();
            else { options_off(); for(auto& slot:state().frames.slots)if(slot.occupied)clear_tags(&slot.value); }
        }
        try {drain();}catch(const std::exception& e){log(std::string("Bridge teardown: ")+e.what());}
        if(registered_) {
            active=nullptr;state().swapchain=nullptr;state().presentQueue.Reset();state().status.bridgeFlags=0;
            for(auto& slot:state().frames.slots)if(slot.occupied)state().frames.release(slot.id);
        }
        if(output_)output_->SetFullscreenState(FALSE,nullptr);
        for(auto& b:backbuffers_)b.Reset();output_.Reset();
        generator_.reset();
        if(c11_)c11_->SetPrivateData(kImmediateContextTag,0,nullptr);
    }
    ID3D11DeviceContext* immediate_context() {return c11_.Get();}
    // True when the context is our immediate context, directly or through a forwarding wrapper.
    bool same_context(ID3D11DeviceContext* context) {
        if(!context) return false;
        if(same_object(context,c11_.Get())) return true;
        void* tag=nullptr; UINT size=sizeof(tag);
        return SUCCEEDED(context->GetPrivateData(kImmediateContextTag,&size,&tag)) && size==sizeof(tag) && tag==this;
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
        copy_input(dv.Get(),slot.depth,de,depthCS_.Get());copy_input(mv.Get(),slot.motion,me,motionCS_.Get());
        slot.constants=constants;return true;
    }
    void finish(uint32_t id,bool success) {auto& slot=inputs_[id%inputs_.size()];if(slot.occupied&&slot.id==id)slot.valid=success;}
    int hudless(uint32_t id,ID3D11DeviceContext* context,ID3D11Resource* color) {
        if(!same_context(context) || !color)return -1;
        auto& slot=inputs_[id%inputs_.size()];if(!slot.occupied || slot.id!=id || !slot.valid)return -2;
        ComPtr<ID3D11Texture2D> texture;check(color->QueryInterface(IID_PPV_ARGS(&texture)),"HUD-less texture type");
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        if(d.Width!=clientDesc_.Width || d.Height!=clientDesc_.Height || d.Format!=clientDesc_.Format || d.SampleDesc.Count!=1 || d.ArraySize!=1 || d.MipLevels!=1)return -3;
        ensure(slot.hudless,d.Width,d.Height,DXGI_FORMAT(output_format(d.Format)),D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET);
        if(d.Format==DXGI_FORMAT_R16G16B16A16_FLOAT) {
            ComPtr<ID3D11ShaderResourceView> view;check(d11_->CreateShaderResourceView(texture.Get(),nullptr,&view),"HUD-less SRV");encode_hdr(view.Get(),slot.hudless);
        } else c11_->CopyResource(slot.hudless.d11.Get(),texture.Get());
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
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID n,UINT size,const void* p) override {return output_->SetPrivateData(n,size,p);}
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID n,const IUnknown* p) override {return output_->SetPrivateDataInterface(n,p);}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID n,UINT* size,void* p) override {return output_->GetPrivateData(n,size,p);}
    HRESULT STDMETHODCALLTYPE GetParent(REFIID id,void** out) override {return clientFactory_->QueryInterface(id,out);}
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID id,void** out) override {return d11_->QueryInterface(id,out);}
    HRESULT STDMETHODCALLTYPE Present(UINT sync,UINT flags) override {return guarded([&]{return present(sync,flags,nullptr);});}
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT index,REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;std::lock_guard<std::recursive_mutex> lock(state().mutex);
        if(index>=colors_.size()) {log("Unsupported client GetBuffer index="+std::to_string(index));return DXGI_ERROR_INVALID_CALL;}
        return colors_[index].source.d11->QueryInterface(id,out);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL value,IDXGIOutput* target) override {
        return guarded([&]{pause_generation();drain();const auto r=output_->SetFullscreenState(value,target);if(SUCCEEDED(r))fullscreen_.Windowed=!value;return r;});
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* value,IDXGIOutput** out) override {return output_->GetFullscreenState(value,out);}
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* d) override {
        if(!d)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(state().mutex);*d={};
        d->BufferDesc.Width=clientDesc_.Width;d->BufferDesc.Height=clientDesc_.Height;d->BufferDesc.Format=clientDesc_.Format;
        d->BufferDesc.RefreshRate=fullscreen_.RefreshRate;d->BufferDesc.ScanlineOrdering=fullscreen_.ScanlineOrdering;d->BufferDesc.Scaling=fullscreen_.Scaling;
        d->SampleDesc=clientDesc_.SampleDesc;d->BufferUsage=clientDesc_.BufferUsage;d->BufferCount=clientDesc_.BufferCount;
        d->OutputWindow=hwnd_;d->Windowed=fullscreen_.Windowed;d->SwapEffect=clientDesc_.SwapEffect;d->Flags=clientDesc_.Flags;return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT n,UINT w,UINT h,DXGI_FORMAT f,UINT flags) override {return guarded([&]{return resize(n,w,h,f,flags);});}
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* desc) override {
        if(!desc)return E_POINTER;return guarded([&]{pause_generation();drain();auto d=*desc;d.Format=DXGI_FORMAT(output_format(d.Format==DXGI_FORMAT_UNKNOWN?clientDesc_.Format:d.Format));return output_->ResizeTarget(&d);});
    }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** out) override {return output_->GetContainingOutput(out);}
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* out) override {return output_->GetFrameStatistics(out);}
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* out) override {if(!out)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(state().mutex);*out=UINT(state().status.sourcePresents);return S_OK;}
    HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* d) override {if(!d)return E_POINTER;std::lock_guard<std::recursive_mutex> lock(state().mutex);*d=clientDesc_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* d) override {return output_->GetFullscreenDesc(d);}
    HRESULT STDMETHODCALLTYPE GetHwnd(HWND* w) override {if(!w)return E_POINTER;*w=hwnd_;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID,void** out) override {if(!out)return E_POINTER;*out=nullptr;return E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE Present1(UINT sync,UINT flags,const DXGI_PRESENT_PARAMETERS* p) override {return guarded([&]{return present(sync,flags,p);});}
    BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override {return output_->IsTemporaryMonoSupported();}
    HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** out) override {return output_->GetRestrictToOutput(out);}
    HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* c) override {return output_->SetBackgroundColor(c);}
    HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* c) override {return output_->GetBackgroundColor(c);}
    HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION r) override {return r==DXGI_MODE_ROTATION_IDENTITY?output_->SetRotation(r):DXGI_ERROR_UNSUPPORTED;}
    HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* r) override {return output_->GetRotation(r);}
    HRESULT STDMETHODCALLTYPE SetSourceSize(UINT w,UINT h) override {if(w!=clientDesc_.Width||h!=clientDesc_.Height)return DXGI_ERROR_UNSUPPORTED;return output_->SetSourceSize(w,h);}
    HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* w,UINT* h) override {return output_->GetSourceSize(w,h);}
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
        if(!m)return E_POINTER;if(m->_11!=1||m->_22!=1||m->_12||m->_21||m->_31||m->_32)return DXGI_ERROR_UNSUPPORTED;return output_->SetMatrixTransform(m);
    }
    HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* m) override {return output_->GetMatrixTransform(m);}
    UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override {std::lock_guard<std::recursive_mutex> lock(state().mutex);return virtualIndex_;}
    HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE cs,UINT* flags) override {
        if(!flags)return E_POINTER;*flags=0;if(!source_color_supported(clientDesc_.Format,cs))return S_OK;
        return output_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE(output_color_space(clientDesc_.Format,cs)),flags);
    }
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE cs) override {
        return guarded([&]{if(!source_color_supported(clientDesc_.Format,cs))return DXGI_ERROR_UNSUPPORTED;
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
            const auto hr=output_->SetHDRMetaData(type,size,data);
            if(SUCCEEDED(hr)) { const auto* bytes=static_cast<const uint8_t*>(data); hdrType_=type; hdrMetadata_.assign(bytes,bytes+(data?size:0)); }
            return hr;
        });
    }
};
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
    if(!s.bridgeAllowed || active)return S_FALSE;
    if(desc.SampleDesc.Count!=1 || desc.Stereo || desc.BufferCount<1 || desc.BufferCount>16 ||
       desc.SwapEffect==DXGI_SWAP_EFFECT_SEQUENTIAL || !source_format_supported(desc.Format) ||
       (desc.AlphaMode!=DXGI_ALPHA_MODE_UNSPECIFIED && desc.AlphaMode!=DXGI_ALPHA_MODE_IGNORE) ||
       (desc.Flags&(DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE|DXGI_SWAP_CHAIN_FLAG_HW_PROTECTED|DXGI_SWAP_CHAIN_FLAG_RESTRICTED_CONTENT))) {
        failure(Reason::UnsupportedSourceLayout,"D3D11 swapchain layout is outside the supported bridge contract; using original creation path");return S_FALSE;
    }
    ComPtr<ID3D11Device> game;if(FAILED(device->QueryInterface(IID_PPV_ARGS(&game))))return S_FALSE;
    s.status.sourceApi=11;
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
void shutdown_dx11_bridge() { // Explicit off, no unsafe live DLL unload.
    std::lock_guard<std::recursive_mutex> lock(state().mutex);options_off();
}
} // namespace tgfg
