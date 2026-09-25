// SPDX-License-Identifier: MIT
// One upscaling request, where HDRP would run STP, on Unity's render thread:
//
//   1. D3D11 (game's immediate context, isolated pipeline state): HDRP's color, depth and motion
//      vectors are copied into textures shared with the bridge's D3D12 device.
//   2. D3D11 signals the shared fence and flushes; the private D3D12 queue waits for it on the GPU.
//   3. FSR or XeSS records the upscale into a D3D12 command list writing the shared output texture.
//   4. The queue signals; the game's D3D11 context waits for that on the GPU, never the CPU.
//   5. D3D11 writes the output into HDRP's destination, which post-processing reads next.
//
// Every D3D11 wait corresponds to a D3D12 signal that was actually submitted, including when the SDK
// refused the frame: the timeline can never leave the game's context waiting for a value that nothing
// signals. A refused frame gets a bilinear enlargement instead of the upscale, never a black image.
#include "runtime.hpp"
#include "upscale_shader.hpp"
#include <d3dcompiler.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace tgup {
namespace {
// Private-data tag on the game's real immediate context. Wrappers such as ReShade hand Unity a
// proxy device with its own COM identity but forward private data to what they wrap, so a texture
// whose device is the proxy still leads back to this tag.
// {5B0B0A4E-9D0C-4E7E-9B57-7A3C6D2F1E10}
const GUID kContextTag={0x5b0b0a4e,0x9d0c,0x4e7e,{0x9b,0x57,0x7a,0x3c,0x6d,0x2f,0x1e,0x10}};
constexpr uint32_t kRing=8;
constexpr uint32_t kFailuresBeforeDisable=5;

// What event 1 left for event 2: whether the SDK's image is in the shared output texture, and
// whether at least the render-resolution color reached the shared color texture for the fallback.
struct Outcome { bool upscaled=false, colorShared=false; };
struct Pending { uint32_t sequence=0; bool occupied=false, processed=false; TGUP_Frame frame{}; Outcome outcome; };

struct State {
    std::mutex mutex;
    std::array<Pending, kRing> ring;
    TGUP_Status status{sizeof(TGUP_Status), TGUP_ABI};
    // D3D11, on the game's device.
    uint32_t generation=0;
    ComPtr<ID3D11Device5> d11;
    ComPtr<ID3D11DeviceContext4> c11;
    ComPtr<ID3DDeviceContextState> scratch;
    ComPtr<ID3D11ComputeShader> copyCS, fallbackCS;
    ComPtr<ID3D11VertexShader> rasterVS;
    ComPtr<ID3D11PixelShader> rasterPS;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11SamplerState> linearClamp;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11DepthStencilState> noDepth;
    // D3D12, on the bridge's shared device.
    ComPtr<ID3D12Device> sharedDevice; // set by the bridge; d12 is the render thread's copy
    ComPtr<ID3D12Device> d12;
    ComPtr<ID3D12CommandQueue> queue;
    tgb::SharedFence fence;
    std::array<ComPtr<ID3D12CommandAllocator>, 3> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList>, 3> lists;
    std::array<uint64_t, 3> retired{};
    uint32_t listIndex=0;
    tgb::SharedTexture color, depth, motion, output;
    std::unique_ptr<Backend> fsr, xess;
    uint32_t lastBackend=0;
    std::array<uint32_t, 3> failures{};
    bool broken=false;              // the interop itself failed; only the fallback runs
    uint64_t lastSummary=0, summaryFrames=0, summaryFallback=0;
} st;

struct Constants { uint32_t origin[2], extent[2]; float sourceScale[2], halfTexel[2]; };
static_assert(sizeof(Constants)==32, "shader constants");

ComPtr<ID3DBlob> compile(const char* macro, const char* entry, const char* profile) {
    const D3D_SHADER_MACRO defines[]={{macro, "1"}, {nullptr, nullptr}};
    ComPtr<ID3DBlob> bytecode, errors;
    const HRESULT r=D3DCompile(upscaleShader, std::strlen(upscaleShader), "TGBetterUpscaling/upscale.hlsl", defines, nullptr,
        entry, profile, D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
    if(FAILED(r) && errors) log(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    check(r, "Compile upscaler shader"); return bytecode;
}

void release_d3d11() {
    st.copyCS.Reset(); st.fallbackCS.Reset(); st.rasterVS.Reset(); st.rasterPS.Reset(); st.constants.Reset();
    st.linearClamp.Reset(); st.raster.Reset(); st.noDepth.Reset(); st.scratch.Reset();
    st.color=tgb::SharedTexture{}; st.depth=tgb::SharedTexture{}; st.motion=tgb::SharedTexture{}; st.output=tgb::SharedTexture{};
    st.fence.reset();
    if(st.c11) {
        UINT size=0; void* none=nullptr;
        if(SUCCEEDED(st.c11->GetPrivateData(kContextTag, &size, nullptr))) st.c11->SetPrivateData(kContextTag, 0, none);
    }
    st.c11.Reset(); st.d11.Reset();
}
// The game's D3D11 device, as the bridge observed it; rebuilt whenever the game creates another.
bool ensure_d3d11() {
    auto& h=host();
    const uint32_t generation=h.game_device_generation();
    if(st.d11 && generation==st.generation) return true;
    ID3D11Device5* device=h.game_device(); ID3D11DeviceContext4* context=h.game_context();
    if(!device || !context) return false;
    if(st.d11) {
        log("The game created a new D3D11 device; the upscaler rebuilds its shared resources");
        bool idle=true;
        if(st.queue && st.fence.ready()) {
            try { st.fence.wait_cpu(st.fence.value); }
            catch(const std::exception& e) { idle=false; log(std::string("Upscaler teardown: ")+e.what()); }
            catch(...) { idle=false; }
        }
        if(!idle) {
            // Work the GPU may still run references these: kept for the rest of the session, never freed.
            for(auto* t:{&st.color, &st.depth, &st.motion, &st.output}) {
                tgb::quarantine(t->d11.Get()); tgb::quarantine(t->d12.Get()); tgb::quarantine(t->srv.Get());
                tgb::quarantine(t->uav.Get()); tgb::quarantine(t->rtv.Get());
            }
            for(auto& a:st.allocators) tgb::quarantine(a.Get());
            for(auto& l:st.lists) tgb::quarantine(l.Get());
            (void)st.fsr.release(); (void)st.xess.release(); // their SDK contexts, leaked while work may be in flight
            log("The GPU never confirmed it finished the upscaler's work; its resources are kept instead of freed");
        }
        st.fsr.reset(); st.xess.reset();
        release_d3d11();
    }
    st.d11=device; st.c11=context; st.generation=generation;
    const auto level=st.d11->GetFeatureLevel(); D3D_FEATURE_LEVEL chosen{};
    check(st.d11->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &chosen, &st.scratch), "Create isolated D3D11 pipeline state");
    auto c=compile("TGUP_COPY", "main", "cs_5_0");
    check(st.d11->CreateComputeShader(c->GetBufferPointer(), c->GetBufferSize(), nullptr, &st.copyCS), "Create copy shader");
    auto f=compile("TGUP_FALLBACK", "main", "cs_5_0");
    check(st.d11->CreateComputeShader(f->GetBufferPointer(), f->GetBufferSize(), nullptr, &st.fallbackCS), "Create fallback shader");
    auto vs=compile("TGUP_RASTER", "vsMain", "vs_5_0"), ps=compile("TGUP_RASTER", "psMain", "ps_5_0");
    check(st.d11->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &st.rasterVS), "Create raster copy VS");
    check(st.d11->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &st.rasterPS), "Create raster copy PS");
    D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(Constants); cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    check(st.d11->CreateBuffer(&cb, nullptr, &st.constants), "Create upscaler constants");
    D3D11_SAMPLER_DESC sampler{}; sampler.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; sampler.MaxLOD=D3D11_FLOAT32_MAX;
    check(st.d11->CreateSamplerState(&sampler, &st.linearClamp), "Create fallback sampler");
    D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
    check(st.d11->CreateRasterizerState(&raster, &st.raster), "Create raster copy state");
    D3D11_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable=FALSE; depth.StencilEnable=FALSE;
    check(st.d11->CreateDepthStencilState(&depth, &st.noDepth), "Create disabled depth state");
    const UINT one=1; st.c11->SetPrivateData(kContextTag, sizeof(one), &one);
    return true;
}
bool ensure_d3d12() {
    if(st.queue && st.fence.ready()) return true;
    if(!st.d12 || !st.d11) return false;
    D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    if(!st.queue) check(st.d12->CreateCommandQueue(&q, IID_PPV_ARGS(&st.queue)), "Create upscaler queue");
    st.fence.create(st.d12.Get(), st.d11.Get());
    for(size_t i=0;i<st.allocators.size();++i) {
        if(st.allocators[i]) continue;
        check(st.d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&st.allocators[i])), "Create upscaler allocator");
        check(st.d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, st.allocators[i].Get(), nullptr, IID_PPV_ARGS(&st.lists[i])), "Create upscaler list");
        check(st.lists[i]->Close(), "Close initial upscaler list");
    }
    st.retired.fill(0);
    log("Upscaler interop ready: private D3D12 queue, shared fence, three command lists");
    return true;
}
// A texture from the game's device, directly or through a wrapper's proxy device.
bool on_game_device(ID3D11Resource* resource) {
    ComPtr<ID3D11Device> device; resource->GetDevice(&device);
    if(!device) return false;
    if(tgb::same_object(device.Get(), st.d11.Get())) return true;
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    UINT value=0, size=sizeof(value);
    return context && SUCCEEDED(context->GetPrivateData(kContextTag, &size, &value)) && size==sizeof(value) && value==1;
}
void ensure_shared(tgb::SharedTexture& t, UINT w, UINT h, DXGI_FORMAT f) {
    if(t.matches(w, h, f)) return;
    // GPU completion, not the reference count, governs replacing a texture the other API used.
    if(st.fence.ready()) st.fence.wait_cpu(st.fence.value);
    t=tgb::SharedTexture::make(st.d11.Get(), st.d12.Get(), w, h, f, D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
}
void set_constants(const Constants& c) {
    st.c11->UpdateSubresource(st.constants.Get(), 0, nullptr, &c, 0, 0);
}
void copy(ID3D11ShaderResourceView* source, tgb::SharedTexture& target, UINT w, UINT h) {
    tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
    Constants c{}; c.extent[0]=w; c.extent[1]=h; set_constants(c);
    auto* cb=st.constants.Get(); auto* uav=target.uav.Get();
    st.c11->CSSetShader(st.copyCS.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 1, &cb);
    st.c11->CSSetShaderResources(0, 1, &source); st.c11->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
}
// Writes source into HDRP's destination over (0,0,w,h): through a UAV when the destination has one,
// otherwise through the rasterizer. bilinear: source covers the render size and is enlarged.
// sourceWidth/Height: the source allocation, for the bilinear enlargement's filtering bounds.
void write_destination(ID3D11Resource* destination, const D3D11_TEXTURE2D_DESC& desc, ID3D11ShaderResourceView* source,
                       UINT w, UINT h, bool bilinear, float scaleX, float scaleY, UINT sourceWidth=1, UINT sourceHeight=1) {
    Constants c{}; c.origin[0]=bilinear ? 1u : 0u; c.extent[0]=w; c.extent[1]=h; c.sourceScale[0]=scaleX; c.sourceScale[1]=scaleY;
    c.halfTexel[0]=0.5f/float(sourceWidth); c.halfTexel[1]=0.5f/float(sourceHeight);
    const auto format=tgb::typed_write_format(desc.Format);
    if((desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS) && format!=DXGI_FORMAT_UNKNOWN) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC view{}; view.Format=format; view.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11UnorderedAccessView> uav;
        if(SUCCEEDED(st.d11->CreateUnorderedAccessView(destination, &view, &uav))) {
            tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
            set_constants(c);
            auto* cb=st.constants.Get(); auto* target=uav.Get(); auto* sampler=st.linearClamp.Get();
            st.c11->CSSetShader(bilinear ? st.fallbackCS.Get() : st.copyCS.Get(), nullptr, 0);
            st.c11->CSSetConstantBuffers(0, 1, &cb); st.c11->CSSetSamplers(0, 1, &sampler);
            st.c11->CSSetShaderResources(0, 1, &source); st.c11->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
            st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
            return;
        }
    }
    const auto rtvFormat=tgb::typed_read_format(desc.Format, tgb::ReadAs::Color);
    if(!(desc.BindFlags&D3D11_BIND_RENDER_TARGET) || rtvFormat==DXGI_FORMAT_UNKNOWN)
        throw DxError(DXGI_ERROR_UNSUPPORTED, "HDRP's destination accepts neither an unordered-access nor a render-target view");
    D3D11_RENDER_TARGET_VIEW_DESC view{}; view.Format=rtvFormat; view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    ComPtr<ID3D11RenderTargetView> rtv; check(st.d11->CreateRenderTargetView(destination, &view, &rtv), "Create destination RTV");
    tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
    set_constants(c);
    auto* target=rtv.Get(); st.c11->OMSetRenderTargets(1, &target, nullptr);
    st.c11->OMSetBlendState(nullptr, nullptr, UINT_MAX); st.c11->OMSetDepthStencilState(st.noDepth.Get(), 0);
    D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1};
    st.c11->RSSetViewports(1, &viewport); st.c11->RSSetState(st.raster.Get());
    st.c11->IASetInputLayout(nullptr); st.c11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    st.c11->VSSetShader(st.rasterVS.Get(), nullptr, 0); st.c11->GSSetShader(nullptr, nullptr, 0);
    st.c11->HSSetShader(nullptr, nullptr, 0); st.c11->DSSetShader(nullptr, nullptr, 0);
    auto* cb=st.constants.Get(); auto* sampler=st.linearClamp.Get();
    st.c11->PSSetConstantBuffers(0, 1, &cb); st.c11->PSSetSamplers(0, 1, &sampler);
    st.c11->PSSetShader(st.rasterPS.Get(), nullptr, 0); st.c11->PSSetShaderResources(0, 1, &source);
    st.c11->Draw(3, 0);
}
Backend* backend_for(uint32_t id) {
    auto& slot=id==TGUP_BACKEND_FSR ? st.fsr : st.xess;
    if(!slot && st.d12) slot=id==TGUP_BACKEND_FSR ? create_fsr_backend(st.d12.Get()) : create_xess_backend(st.d12.Get());
    return slot.get();
}
void note_failure(uint32_t backend, const std::string& error, int32_t code) {
    std::unique_lock<std::mutex> lock(st.mutex);
    st.status.lastResult=code;
    ++st.status.framesFailed;
    if(backend<st.failures.size() && ++st.failures[backend]>=kFailuresBeforeDisable && !(st.status.disabled&(1u<<backend))) {
        st.status.disabled|=1u<<backend;
        lock.unlock();
        log(std::string(backend==TGUP_BACKEND_FSR ? "AMD FSR" : "Intel XeSS")+" failed "+std::to_string(kFailuresBeforeDisable)+
            " frames in a row ("+error+"); it is off for the rest of this session and the game's STP takes over");
    } else log_throttled("Upscaling refused a frame: "+error);
}
void summarise(const TGUP_Frame& f) {
    const uint64_t now=GetTickCount64();
    ++st.summaryFrames;
    if(!st.lastSummary) { st.lastSummary=now; return; }
    if(now-st.lastSummary<10000) return;
    log("Upscaling "+std::to_string((now-st.lastSummary)/1000)+"s: frames="+std::to_string(st.summaryFrames)+
        " fallback="+std::to_string(st.summaryFallback)+" technology="+(f.backend==TGUP_BACKEND_FSR ? "FSR" : "XeSS")+
        " render="+std::to_string(f.renderWidth)+"x"+std::to_string(f.renderHeight)+" output="+std::to_string(f.outputWidth)+"x"+
        std::to_string(f.outputHeight)+" sharpness="+std::to_string(f.sharpness)+(f.backend==TGUP_BACKEND_FSR ? " preExposure="+std::to_string(f.preExposure) : std::string())+" failed(total)="+std::to_string(st.status.framesFailed));
    st.lastSummary=now; st.summaryFrames=st.summaryFallback=0;
}

// Event 1: HDRP's inputs into the shared textures and, when the SDK accepts the frame, its upscale
// into the shared output texture. Nothing is written to HDRP's destination here.
Outcome process_inputs(const TGUP_Frame& f) {
    Outcome outcome;
    if(!st.d12) { std::lock_guard<std::mutex> lock(st.mutex); st.d12=st.sharedDevice; }
    std::string error; int32_t code=0;
    // Every refusal counts as a failed frame: five in a row turn the technology off and STP takes
    // over, rather than leaving HDRP's destination to this mod's enlargement, or unwritten, for good.
    try {
        if(!ensure_d3d11()) throw DxError(E_FAIL, "the request came before the game's D3D11 device was observed");
        auto* colorRes=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.color));
        auto* depthRes=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.depth));
        auto* motionRes=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.motion));
        if(!on_game_device(colorRes)) throw DxError(E_INVALIDARG, "the upscaling textures are not on the game's device");
        D3D11_TEXTURE2D_DESC colorDesc{}, depthDesc{}, motionDesc{};
        auto colorSrv=tgb::typed_srv(st.d11.Get(), colorRes, tgb::ReadAs::Color, &colorDesc);
        if(f.renderWidth>colorDesc.Width || f.renderHeight>colorDesc.Height) throw DxError(E_INVALIDARG, "the render size exceeds HDRP's color texture");
        if(!st.d12) throw DxError(E_FAIL, "TG Graphics Bridge created no D3D12 device");
        auto* backend=!st.broken && !(st.status.disabled&(1u<<f.backend)) ? backend_for(f.backend) : nullptr;
        if(!ensure_d3d12()) throw DxError(E_FAIL, "no D3D12 device from TG Graphics Bridge");
        // The color copy comes first: event 2 enlarges it if anything after this refuses the frame.
        ensure_shared(st.color, f.renderWidth, f.renderHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copy(colorSrv.Get(), st.color, f.renderWidth, f.renderHeight);
        outcome.colorShared=true;
        if(!backend) return outcome;
        auto depthSrv=tgb::typed_srv(st.d11.Get(), depthRes, tgb::ReadAs::Depth, &depthDesc);
        auto motionSrv=tgb::typed_srv(st.d11.Get(), motionRes, tgb::ReadAs::Motion, &motionDesc);
        if(f.renderWidth>depthDesc.Width || f.renderHeight>depthDesc.Height || f.renderWidth>motionDesc.Width || f.renderHeight>motionDesc.Height)
            throw DxError(E_INVALIDARG, "Depth or motion vectors are smaller than the render size");
        // A technology change or a context rebuild needs the GPU idle: the old context's work may
        // still be in flight on the queue.
        const bool switched=st.lastBackend!=f.backend;
        if(switched || backend->stale(f)) {
            st.fence.wait_cpu(st.fence.value);
            if(switched && st.lastBackend) {
                // Only one technology keeps its resources.
                if(st.lastBackend==TGUP_BACKEND_FSR) st.fsr.reset(); else st.xess.reset();
                backend=backend_for(f.backend);
            }
            if(!backend || !backend->prepare(f, error)) throw DxError(E_FAIL, error.empty() ? "context creation failed" : error);
        }
        ensure_shared(st.depth, f.renderWidth, f.renderHeight, DXGI_FORMAT_R32_FLOAT);
        ensure_shared(st.motion, f.renderWidth, f.renderHeight, DXGI_FORMAT_R16G16_FLOAT);
        ensure_shared(st.output, f.outputWidth, f.outputHeight, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copy(depthSrv.Get(), st.depth, f.renderWidth, f.renderHeight);
        copy(motionSrv.Get(), st.motion, f.renderWidth, f.renderHeight);
        // D3D11 -> D3D12. From here on the queue signals whatever happens, so the game's context is
        // never left waiting for a value nothing produces.
        const uint64_t produced=st.fence.next();
        check(st.c11->Signal(st.fence.d11.Get(), produced), "Signal D3D11 upscaling inputs"); st.c11->Flush();
        check(st.queue->Wait(st.fence.d12.Get(), produced), "D3D12 wait for upscaling inputs");
        const uint32_t index=st.listIndex++%uint32_t(st.lists.size());
        bool recorded=false;
        try {
            st.fence.wait_cpu(st.retired[index]);
            check(st.allocators[index]->Reset(), "Reset upscaler allocator");
            check(st.lists[index]->Reset(st.allocators[index].Get(), nullptr), "Reset upscaler list");
            Inputs in; in.color=st.color.d12.Get(); in.depth=st.depth.d12.Get(); in.motion=st.motion.d12.Get(); in.output=st.output.d12.Get();
            in.frame=&f; in.reset=(f.flags&TGUP_FLAG_RESET) || switched;
            const bool ok=backend->record(st.lists[index].Get(), in, error);
            check(st.lists[index]->Close(), "Close upscaler list");
            if(ok) {
                ID3D12CommandList* lists[]={st.lists[index].Get()};
                st.queue->ExecuteCommandLists(1, lists);
                recorded=true;
            }
        } catch(const DxError& e) { st.lists[index]->Close(); error=e.what(); code=e.hr; }
        const uint64_t retired=st.fence.next();
        const HRESULT signalled=st.queue->Signal(st.fence.d12.Get(), retired);
        if(FAILED(signalled)) {
            // The interop itself failed: neither technology can run again this session, so STP takes over.
            st.broken=true;
            { std::lock_guard<std::mutex> lock(st.mutex); st.status.disabled|=(1u<<TGUP_BACKEND_FSR)|(1u<<TGUP_BACKEND_XESS); }
            log("The D3D12 queue failed to signal ("+hexhr(signalled)+"); FSR and XeSS are off for this session and STP takes over");
            throw DxError(signalled, "D3D12 signal after upscaling");
        }
        st.retired[index]=retired;
        // The game's context waits on the GPU for the upscale before anything it records next.
        check(st.c11->Wait(st.fence.d11.Get(), retired), "D3D11 wait for the upscaled image");
        if(recorded) {
            outcome.upscaled=true;
            st.lastBackend=f.backend;
            std::lock_guard<std::mutex> lock(st.mutex);
            st.failures[f.backend]=0;
            ++st.status.framesUpscaled; st.status.activeBackend=f.backend;
            st.status.renderWidth=f.renderWidth; st.status.renderHeight=f.renderHeight;
            st.status.outputWidth=f.outputWidth; st.status.outputHeight=f.outputHeight;
            backend->describe(st.status);
        } else note_failure(f.backend, error.empty() ? "the SDK refused the frame" : error, code);
    } catch(const DxError& e) {
        note_failure(f.backend, std::string(e.what())+" "+hexhr(e.hr), e.hr);
    } catch(const std::exception& e) {
        note_failure(f.backend, e.what(), E_FAIL);
    }
    return outcome;
}
// Event 2 (or the end of event 1 when the destination came with the request): the upscaled image
// into HDRP's destination, or the bilinear enlargement of the shared color copy. Never leaves the
// destination unwritten, since post-processing would otherwise read last frame's pixels.
void write_output(const TGUP_Frame& f, const Outcome& outcome) {
    auto* outputRes=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.output));
    if(!outputRes || !st.d11) { log_throttled("Upscaling has no destination for this frame"); return; }
    if(!on_game_device(outputRes)) { log_throttled("HDRP's upscaling destination is not on the game's device"); return; }
    D3D11_TEXTURE2D_DESC outputDesc{};
    {
        ComPtr<ID3D11Texture2D> texture; check(outputRes->QueryInterface(IID_PPV_ARGS(&texture)), "Output is not a 2D texture");
        texture->GetDesc(&outputDesc);
    }
    if(f.outputWidth>outputDesc.Width || f.outputHeight>outputDesc.Height) throw DxError(E_INVALIDARG, "The output size exceeds HDRP's destination");
    if(outcome.upscaled && st.output.matches(f.outputWidth, f.outputHeight, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
        write_destination(outputRes, outputDesc, st.output.srv.Get(), f.outputWidth, f.outputHeight, false, 1.f, 1.f);
    } else if(outcome.colorShared && st.color.matches(f.renderWidth, f.renderHeight, DXGI_FORMAT_R16G16B16A16_FLOAT)) {
        // The shared copy is exactly the render size, so the whole texture is the region.
        write_destination(outputRes, outputDesc, st.color.srv.Get(), f.outputWidth, f.outputHeight, true, 1.f, 1.f, f.renderWidth, f.renderHeight);
        ++st.summaryFallback;
        std::lock_guard<std::mutex> lock(st.mutex); ++st.status.framesFallback;
    } else {
        // Only reachable without the bridge's device: enlarge HDRP's color directly.
        auto* colorRes=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.color));
        D3D11_TEXTURE2D_DESC colorDesc{};
        auto colorSrv=tgb::typed_srv(st.d11.Get(), colorRes, tgb::ReadAs::Color, &colorDesc);
        write_destination(outputRes, outputDesc, colorSrv.Get(), f.outputWidth, f.outputHeight, true,
            float(f.renderWidth)/float(colorDesc.Width), float(f.renderHeight)/float(colorDesc.Height), colorDesc.Width, colorDesc.Height);
        ++st.summaryFallback;
        std::lock_guard<std::mutex> lock(st.mutex); ++st.status.framesFallback;
    }
    summarise(f);
}
template<class Fn> void guarded(Fn&& fn) {
    tgb::InternalScope internal(&host());
    try { fn(); }
    catch(const DxError& e) {
        log_throttled(std::string("Upscaling failed: ")+e.what()+" "+hexhr(e.hr));
        std::lock_guard<std::mutex> lock(st.mutex); st.status.lastResult=e.hr;
    }
    catch(const std::exception& e) { log_throttled(std::string("Upscaling failed: ")+e.what()); }
    catch(...) { log_throttled("Upscaling failed with an unknown exception"); }
}
// Event 1 upscales, event 2 writes the result where HDRP expects it. The plugin issues them from
// two render-graph passes, the second of which is the one that declares HDRP's destination.
void render_event_body(int eventId, void* data) {
    if(eventId!=TGUP_EVENT_UPSCALE && eventId!=TGUP_EVENT_OUTPUT) return;
    const uint32_t sequence=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    TGUP_Frame frame{}; Outcome outcome;
    {
        // The main thread submits while this runs; the upscale itself touches only render-thread state.
        std::lock_guard<std::mutex> lock(st.mutex);
        auto& slot=st.ring[sequence%kRing];
        if(!slot.occupied || slot.sequence!=sequence) { log_throttled("Upscaling event for an unknown request; the frame keeps HDRP's destination"); return; }
        frame=slot.frame; outcome=slot.outcome;
        if(eventId==2) {
            if(!slot.processed) log_throttled("Upscaling output event before its input event");
            slot.occupied=false;
        }
    }
    if(eventId==1) {
        guarded([&]{ outcome=process_inputs(frame); });
        const bool single=frame.output!=0; // the destination came with the request
        {
            std::lock_guard<std::mutex> lock(st.mutex);
            auto& slot=st.ring[sequence%kRing];
            if(slot.occupied && slot.sequence==sequence) {
                slot.outcome=outcome; slot.processed=true;
                if(single) slot.occupied=false;
            }
        }
        if(single) guarded([&]{ write_output(frame, outcome); });
    } else guarded([&]{ write_output(frame, outcome); });
}
// Unity's render thread calls this through IssuePluginEventAndData: nothing may leave it. A frame that
// failed here keeps HDRP's destination, as for any refused frame.
void __stdcall render_event(int eventId, void* data) {
    try { render_event_body(eventId, data); } catch(...) { log_throttled("Upscaling event failed with an exception; the frame keeps HDRP's destination"); }
}
} // namespace

void upscaler_device_ready(ID3D12Device* device, uint32_t vendor) {
    std::lock_guard<std::mutex> lock(st.mutex);
    st.sharedDevice=device; st.status.gpuVendor=vendor; st.status.deviceReady=1;
    probe_fsr(device); probe_xess(device);
    st.status.available=(fsr_available() ? 1u<<TGUP_BACKEND_FSR : 0u)|(xess_available() ? 1u<<TGUP_BACKEND_XESS : 0u);
    st.status.fsrVersionCount=uint32_t(fsr_version_count());
    st.status.xessVersion=xess_version();
}
int32_t upscaler_submit(const TGUP_Frame& frame) {
    if(!valid(frame)) return -1;
    std::lock_guard<std::mutex> lock(st.mutex);
    if(!st.status.deviceReady) return -2;
    if(!(st.status.available&(1u<<frame.backend)) || (st.status.disabled&(1u<<frame.backend))) return -3;
    auto& slot=st.ring[frame.sequence%kRing];
    slot.sequence=frame.sequence; slot.frame=frame; slot.occupied=true; slot.processed=false; slot.outcome=Outcome{};
    return 0;
}
int32_t upscaler_set_output(uint32_t sequence, uint64_t output) {
    if(!output) return -1;
    std::lock_guard<std::mutex> lock(st.mutex);
    auto& slot=st.ring[sequence%kRing];
    if(!slot.occupied || slot.sequence!=sequence) return -2;
    slot.frame.output=output;
    return 0;
}
int32_t upscaler_read_status(TGUP_Status& out) {
    std::lock_guard<std::mutex> lock(st.mutex);
    out=st.status; return 0;
}
void* upscaler_render_event() { return reinterpret_cast<void*>(&render_event); }
} // namespace tgup
