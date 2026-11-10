// SPDX-License-Identifier: MIT
#include "nrd_denoiser.hpp"
#if TGPT_NRD
// NVIDIA NRD (fetched beside the repository, compiled in; NVIDIA RTX SDKs License: object code only, docs/SOURCES.md).
#include <NRD.h>
namespace tgpt::shaders {
#include "pt_nrd_prep.h" // the prep pass, compiled by DXC at build time
}
#endif
#include <cmath>
#include <cstring>

namespace tgpt {
#if TGPT_NRD
namespace {
constexpr nrd::Identifier kRelax=1;
constexpr uint32_t kSlots=4; // frames in flight whose descriptors may still be read (the tracer keeps 3)

struct Runtime {
    ComPtr<ID3D12Device> device;
    nrd::Instance* instance=nullptr;
    ComPtr<ID3D12RootSignature> root;
    std::vector<ComPtr<ID3D12PipelineState>> pipelines;
    ComPtr<ID3D12DescriptorHeap> heap;
    uint32_t increment=0, tableSize=0, textures=0, setsPerSlot=0;
    // The prep pass, on the tracer's root signature (recreated with it).
    ComPtr<ID3D12PipelineState> prep; ID3D12RootSignature* prepRoot=nullptr;
    uint32_t kind=1; // NrdFrame::kind the instance runs
    bool available=false;
} rt;

// The denoiser an instance runs, by NrdFrame::kind (REBLUR on trial: TGPT_FRAME_NRD_*). (Until 2026-10-11 RELAX's directional
// form for the bounced light alone: pt_nrd_prep.hlsl says why it went.)
nrd::Denoiser denoiser_of(uint32_t kind) { return kind==2 ? nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR : nrd::Denoiser::RELAX_DIFFUSE_SPECULAR; }
const char* name_of(uint32_t kind) { return kind==2 ? "REBLUR, the diffuse and the specular light" : "RELAX, the diffuse and the specular light"; }

// nrd::Format in its order.
constexpr DXGI_FORMAT kFormats[]={
    DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_SNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT,
    DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_SNORM, DXGI_FORMAT_R8G8_UINT, DXGI_FORMAT_R8G8_SINT,
    DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
    DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16_SNORM, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT, DXGI_FORMAT_R16_FLOAT,
    DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_SNORM, DXGI_FORMAT_R16G16_UINT, DXGI_FORMAT_R16G16_SINT, DXGI_FORMAT_R16G16_FLOAT,
    DXGI_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_SNORM, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT, DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32_FLOAT,
    DXGI_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_SINT, DXGI_FORMAT_R32G32_FLOAT,
    DXGI_FORMAT_R32G32B32_UINT, DXGI_FORMAT_R32G32B32_SINT, DXGI_FORMAT_R32G32B32_FLOAT,
    DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT, DXGI_FORMAT_R32G32B32A32_FLOAT,
    DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UINT, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R9G9B9E5_SHAREDEXP};
static_assert(sizeof(kFormats)/sizeof(kFormats[0])==size_t(nrd::Format::MAX_NUM), "nrd::Format order");

void release_runtime() {
    if(rt.instance) nrd::DestroyInstance(*rt.instance);
    rt=Runtime{};
}

// NRD's root signature: the constant buffer as a root descriptor, the textures and storage textures as one table
// (textures first, storage textures after the most any pipeline takes), and its samplers as static ones (all as the
// instance describes its registers and spaces). Version 1.0: a table's unused descriptors may hold anything.
ComPtr<ID3D12RootSignature> make_root(ID3D12Device* device, const nrd::InstanceDesc& d) {
    auto* d3d12=GetModuleHandleW(L"d3d12.dll");
    if(!d3d12) d3d12=LoadLibraryW(L"d3d12.dll");
    auto serialize=d3d12 ? reinterpret_cast<PFN_D3D12_SERIALIZE_ROOT_SIGNATURE>(GetProcAddress(d3d12, "D3D12SerializeRootSignature")) : nullptr;
    if(!serialize) throw DxError(E_NOINTERFACE, "d3d12.dll has no D3D12SerializeRootSignature");
    D3D12_DESCRIPTOR_RANGE ranges[2]{}; UINT rangeCount=0;
    if(d.descriptorPoolDesc.perSetTexturesMaxNum) {
        auto& r=ranges[rangeCount++]; r.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; r.NumDescriptors=d.descriptorPoolDesc.perSetTexturesMaxNum;
        r.BaseShaderRegister=d.resourcesBaseRegisterIndex; r.RegisterSpace=d.resourcesSpaceIndex; r.OffsetInDescriptorsFromTableStart=0;
    }
    if(d.descriptorPoolDesc.perSetStorageTexturesMaxNum) {
        auto& r=ranges[rangeCount++]; r.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV; r.NumDescriptors=d.descriptorPoolDesc.perSetStorageTexturesMaxNum;
        r.BaseShaderRegister=d.resourcesBaseRegisterIndex; r.RegisterSpace=d.resourcesSpaceIndex; r.OffsetInDescriptorsFromTableStart=d.descriptorPoolDesc.perSetTexturesMaxNum;
    }
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    params[0].Descriptor.ShaderRegister=d.constantBufferRegisterIndex; params[0].Descriptor.RegisterSpace=d.constantBufferAndSamplersSpaceIndex;
    params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    params[1].DescriptorTable.NumDescriptorRanges=rangeCount; params[1].DescriptorTable.pDescriptorRanges=ranges;
    std::vector<D3D12_STATIC_SAMPLER_DESC> samplers(d.samplersNum);
    for(uint32_t i=0;i<d.samplersNum;++i) {
        auto& s=samplers[i];
        s.Filter=d.samplers[i]==nrd::Sampler::NEAREST_CLAMP ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        s.AddressU=s.AddressV=s.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP; s.MaxLOD=D3D12_FLOAT32_MAX;
        s.ShaderRegister=d.samplersBaseRegisterIndex+i; s.RegisterSpace=d.constantBufferAndSamplersSpaceIndex; s.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters=2; desc.pParameters=params;
    desc.NumStaticSamplers=UINT(samplers.size()); desc.pStaticSamplers=samplers.data();
    ComPtr<ID3DBlob> blob, errors;
    const HRESULT hr=serialize(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
    if(FAILED(hr)) throw DxError(hr, std::string("Serialize NRD's root signature: ")+(errors ? static_cast<const char*>(errors->GetBufferPointer()) : ""));
    ComPtr<ID3D12RootSignature> root;
    check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)), "Create NRD's root signature");
    return root;
}

D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle(uint32_t i) { auto h=rt.heap->GetCPUDescriptorHandleForHeapStart(); h.ptr+=size_t(i)*rt.increment; return h; }
D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle(uint32_t i) { auto h=rt.heap->GetGPUDescriptorHandleForHeapStart(); h.ptr+=uint64_t(i)*rt.increment; return h; }

bool make_runtime(ID3D12Device* device, uint32_t kind) {
    try {
        const nrd::LibraryDesc& library=*nrd::GetLibraryDesc();
        if(library.versionMajor!=NRD_VERSION_MAJOR || library.versionMinor!=NRD_VERSION_MINOR) throw DxError(E_FAIL, "NRD library and header versions differ");
        if(library.normalEncoding!=nrd::NormalEncoding::R10_G10_B10_A2_UNORM || library.roughnessEncoding!=nrd::RoughnessEncoding::LINEAR)
            throw DxError(E_FAIL, "NRD was built with another normal or roughness encoding than pt_nrd_prep.hlsl writes");
        const nrd::DenoiserDesc denoiser{kRelax, denoiser_of(kind)};
        nrd::InstanceCreationDesc create{}; create.denoisers=&denoiser; create.denoisersNum=1;
        if(nrd::CreateInstance(create, rt.instance)!=nrd::Result::SUCCESS || !rt.instance) { rt.instance=nullptr; throw DxError(E_FAIL, "nrd::CreateInstance failed"); }
        const nrd::InstanceDesc& d=*nrd::GetInstanceDesc(*rt.instance);
        rt.root=make_root(device, d);
        for(uint32_t i=0;i<d.pipelinesNum;++i) {
            const auto& shader=d.pipelines[i].computeShaderDXIL;
            if(!shader.bytecode || !shader.size) throw DxError(E_FAIL, "NRD has no DXIL for pipeline "+std::to_string(i));
            D3D12_COMPUTE_PIPELINE_STATE_DESC p{}; p.pRootSignature=rt.root.Get(); p.CS.pShaderBytecode=shader.bytecode; p.CS.BytecodeLength=SIZE_T(shader.size);
            ComPtr<ID3D12PipelineState> pso; check(device->CreateComputePipelineState(&p, IID_PPV_ARGS(&pso)), "Create NRD pipeline");
            rt.pipelines.push_back(pso);
        }
        rt.textures=d.descriptorPoolDesc.perSetTexturesMaxNum;
        rt.tableSize=d.descriptorPoolDesc.perSetTexturesMaxNum+d.descriptorPoolDesc.perSetStorageTexturesMaxNum;
        rt.setsPerSlot=std::max(d.descriptorPoolDesc.setsMaxNum, 1u);
        D3D12_DESCRIPTOR_HEAP_DESC h{}; h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; h.NumDescriptors=std::max(kSlots*rt.setsPerSlot*rt.tableSize, 1u);
        h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&rt.heap)), "Create NRD's descriptor heap");
        rt.increment=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        rt.device=device; rt.kind=kind; rt.available=true;
        log("NVIDIA NRD "+std::to_string(library.versionMajor)+"."+std::to_string(library.versionMinor)+"."+std::to_string(library.versionBuild)+
            " available ("+name_of(kind)+", "+std::to_string(d.pipelinesNum)+" pipelines, up to "+std::to_string(rt.setsPerSlot)+" dispatches a frame)");
        return true;
    } catch(const DxError& e) {
        log(std::string("NVIDIA NRD is unavailable (")+e.what()+" "+tgb::hexhr(e.hr)+"); the built-in denoiser serves");
        release_runtime();
        return false;
    }
}
} // namespace

bool nrd_probe(ID3D12Device* device) {
    if(rt.available && rt.device.Get()==device) return true;
    release_runtime();
    if(!device) return false;
    return make_runtime(device, 1);
}
bool nrd_use(uint32_t kind) {
    if(!rt.available) return false;
    if(rt.kind==kind) return true;
    ComPtr<ID3D12Device> device=rt.device;
    release_runtime();
    return make_runtime(device.Get(), kind);
}
uint32_t nrd_kind() { return rt.kind; }
bool nrd_available() { return rt.available; }
void nrd_shutdown() { release_runtime(); }

ID3D12PipelineState* nrd_prep_for(ID3D12RootSignature* tracerRoot) {
    if(!rt.available || !tracerRoot) return nullptr;
    if(rt.prep && rt.prepRoot==tracerRoot) return rt.prep.Get();
    D3D12_COMPUTE_PIPELINE_STATE_DESC p{}; p.pRootSignature=tracerRoot; p.CS.pShaderBytecode=shaders::nrdPrep; p.CS.BytecodeLength=sizeof(shaders::nrdPrep);
    rt.prep.Reset(); rt.prepRoot=nullptr;
    check(rt.device->CreateComputePipelineState(&p, IID_PPV_ARGS(&rt.prep)), "Create NRD prep pipeline");
    rt.prepRoot=tracerRoot;
    return rt.prep.Get();
}

void Nrd::destroy() { pools_.clear(); width_=height_=0; restart_=true; }

bool Nrd::ensure(ID3D12Device* device, uint32_t width, uint32_t height, std::string& error) {
    if(!needs_pools(width, height)) return true;
    destroy();
    if(!rt.available || device!=rt.device.Get()) { error="not available"; return false; }
    try {
        const nrd::InstanceDesc& d=*nrd::GetInstanceDesc(*rt.instance);
        const uint32_t count=d.permanentPoolSize+d.transientPoolSize;
        pools_.resize(count);
        for(uint32_t i=0;i<count;++i) {
            const nrd::TextureDesc& t=i<d.permanentPoolSize ? d.permanentPool[i] : d.transientPool[i-d.permanentPoolSize];
            if(uint32_t(t.format)>=uint32_t(nrd::Format::MAX_NUM)) throw DxError(E_INVALIDARG, "NRD asked for an unknown texture format");
            const uint32_t factor=std::max<uint32_t>(t.downsampleFactor, 1);
            D3D12_RESOURCE_DESC r{}; r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; r.Width=(width+factor-1)/factor; r.Height=(height+factor-1)/factor;
            r.DepthOrArraySize=1; r.MipLevels=1; r.Format=kFormats[uint32_t(t.format)]; r.SampleDesc.Count=1; r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            const auto props=heap_props(D3D12_HEAP_TYPE_DEFAULT);
            check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&pools_[i].resource)), "Create NRD pool texture");
            pools_[i].format=r.Format; pools_[i].state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    } catch(const DxError& e) { error=std::string(e.what())+" "+tgb::hexhr(e.hr); destroy(); return false; }
    width_=width; height_=height; restart_=true;
    log("NVIDIA NRD textures for "+std::to_string(width)+"x"+std::to_string(height)+": "+std::to_string(pools_.size())+" in its pools");
    return true;
}

bool Nrd::dispatch(ID3D12GraphicsCommandList* list, uint32_t slot, UploadRing& ring, const NrdFrame& f, std::string& error) {
    if(!rt.available || pools_.empty()) { error="not ready"; return false; }
    const nrd::InstanceDesc& d=*nrd::GetInstanceDesc(*rt.instance);
    nrd::CommonSettings c{};
    std::memcpy(c.viewToClipMatrix, f.viewToClip, sizeof(c.viewToClipMatrix)); std::memcpy(c.viewToClipMatrixPrev, f.viewToClipPrev, sizeof(c.viewToClipMatrixPrev));
    std::memcpy(c.worldToViewMatrix, f.worldToView, sizeof(c.worldToViewMatrix)); std::memcpy(c.worldToViewMatrixPrev, f.worldToViewPrev, sizeof(c.worldToViewMatrixPrev));
    // 2.5D motion (README: recommended): the previous texture coordinate minus this one's, and the view depth's change in z
    // (pt_nrd_prep). Hair as NRD's strands (material ID 1: pt_nrd_prep).
    c.motionVectorScale[0]=c.motionVectorScale[1]=c.motionVectorScale[2]=1.f;
    c.strandMaterialID=1.f;
    for(int i=0;i<2;++i) { c.cameraJitter[i]=std::clamp(f.jitter[i], -0.5f, 0.5f); c.cameraJitterPrev[i]=std::clamp(f.jitterPrev[i], -0.5f, 0.5f); }
    c.resourceSize[0]=c.resourceSizePrev[0]=c.rectSize[0]=c.rectSizePrev[0]=uint16_t(f.width);
    c.resourceSize[1]=c.resourceSizePrev[1]=c.rectSize[1]=c.rectSizePrev[1]=uint16_t(f.height);
    c.denoisingRange=f.denoisingRange;
    c.frameIndex=f.frameIndex;
    c.isHistoryConfidenceAvailable=f.diffuseConfidence.resource && f.specularConfidence.resource;
    c.accumulationMode=restart_ ? nrd::AccumulationMode::CLEAR_AND_RESTART : f.reset ? nrd::AccumulationMode::RESTART : nrd::AccumulationMode::CONTINUE;
    if(nrd::SetCommonSettings(*rt.instance, c)!=nrd::Result::SUCCESS) { error="NRD refused this frame's common settings"; return false; }
    // NVIDIA's defaults, but for three things below. The firefly suppression is on (REBLUR's default,
    // not RELAX's), which takes a pixel brighter than all eight around it down to the brightest of
    // them., which takes a pixel brighter than all eight around it down to the brightest of them. It
    // costs light (PT-75: 5 of 100 parts of a closed room's bounced light, 6 to 10 where the sky comes
    // through small holes, blue most: tgpt_gpu_test nrdMean), and it was off for some hours on
    // 2026-10-08. In the game every scene was then sprinkled with dots that linger. A rare bright path
    // held for as long as RELAX's history is worse than the light it carries. The light comes back by
    // making those paths less rare in the tracer, never by this switch.
    if(f.kind!=rt.kind) { error="NRD's instance is for another kind of frame"; return false; }
    // NRD's history as its documentation sets it: "Always accumulate in 'seconds' not in 'frames', use
    // 'GetMaxAccumulatedFrameNum' for conversion" (NRDSettings.h), at the real frame rate (README: frame generation's frames
    // are not the rate). Its default time (half a second), the fast history five times shorter ("usually 5x-7x"), the frames
    // rebuilt after a reset fewer still: most > fast > fix, as NRDSettings.h orders them. At 60 frames a second NRD's own
    // defaults (30, 6, 3); at the tracer's 10 today 5, 1 and 0.
    uint32_t most=0, fast=0, fix=0;
    if(f.frameRate>0.f && std::isfinite(f.frameRate)) {
        const bool reblur=f.kind==2;
        most=std::clamp(nrd::GetMaxAccumulatedFrameNum(reblur ? nrd::REBLUR_DEFAULT_ACCUMULATION_TIME : nrd::RELAX_DEFAULT_ACCUMULATION_TIME, f.frameRate),
                        1u, reblur ? nrd::REBLUR_MAX_HISTORY_FRAME_NUM : nrd::RELAX_MAX_HISTORY_FRAME_NUM);
        fast=std::max(1u, uint32_t(float(most)/5.f+0.5f));
        if(fast>=most) fast=most>1 ? most-1 : most;
        fix=std::min(3u, fast>0 ? fast-1 : 0u);
    }
    if(f.kind==2) {
        // ON TRIAL (TGPT_FRAME_NRD_*, 2): REBLUR: its defaults (the hit distances' normalization among them, which
        // pt_nrd_prep's front end takes alike) and no pre-pass blur of the diffuse light, as for RELAX below.
        nrd::ReblurSettings reblur{};
        if(f.primarySurfaceReplacement) reblur.minMaterialForDiffuse=reblur.minMaterialForSpecular=0.f;
        if(most) { reblur.maxAccumulatedFrameNum=most; reblur.maxFastAccumulatedFrameNum=fast; reblur.historyFixFrameNum=fix; }
        reblur.enableAntiFirefly=true;
        reblur.diffusePrepassBlurRadius=0.f;
        reblur.hitDistanceReconstructionMode=nrd::HitDistanceReconstructionMode::OFF;
        if(nrd::SetDenoiserSettings(*rt.instance, kRelax, &reblur)!=nrd::Result::SUCCESS) { error="NRD refused REBLUR's settings"; return false; }
    } else {
        nrd::RelaxSettings relax{};
        if(f.primarySurfaceReplacement) relax.minMaterialForDiffuse=relax.minMaterialForSpecular=0.f;
        if(most) {
            relax.diffuseMaxAccumulatedFrameNum=relax.specularMaxAccumulatedFrameNum=most;
            relax.diffuseMaxFastAccumulatedFrameNum=relax.specularMaxFastAccumulatedFrameNum=fast;
            relax.historyFixFrameNum=fix;
        }
        relax.enableAntiFirefly=true;
        // No pre-pass blur of the diffuse light: every pixel traces its own, the signal NRD's documentation calls relatively
        // clean, for which "pre-pass may introduce additional blur" (it spread the light into what should stay dark); the
        // specular light keeps its pre-pass, which finds the hit distance the reflections are tracked by.
        relax.diffusePrepassBlurRadius=0.f;
        relax.hitDistanceReconstructionMode=nrd::HitDistanceReconstructionMode::OFF;
        if(nrd::SetDenoiserSettings(*rt.instance, kRelax, &relax)!=nrd::Result::SUCCESS) { error="NRD refused RELAX's settings"; return false; }
    }
    const nrd::DispatchDesc* dispatches=nullptr; uint32_t count=0;
    if(nrd::GetComputeDispatches(*rt.instance, &kRelax, 1, dispatches, count)!=nrd::Result::SUCCESS) { error="NRD returned no dispatches"; return false; }
    if(count>rt.setsPerSlot) { error="NRD asked for more dispatches than its descriptor heap holds"; return false; }
    auto external_for=[&](nrd::ResourceType type) -> const NrdTexture* {
        switch(type) {
        case nrd::ResourceType::IN_VIEWZ: return &f.viewZ;
        case nrd::ResourceType::IN_MV: return &f.motion;
        case nrd::ResourceType::IN_NORMAL_ROUGHNESS: return &f.normalRoughness;
        case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST: return &f.diffuse;
        case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST: return &f.specular;
        case nrd::ResourceType::IN_DIFF_CONFIDENCE: return &f.diffuseConfidence;
        case nrd::ResourceType::IN_SPEC_CONFIDENCE: return &f.specularConfidence;
        case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST: return &f.diffuseOut;
        case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST: return &f.specularOut;
        default: return nullptr;
        }
    };
    // Every dispatch checked and every constant block reserved before a command is recorded: a failure leaves the command
    // list and every texture's state as they were (the caller then hands compose the bounced light as traced).
    std::vector<D3D12_GPU_VIRTUAL_ADDRESS> constantsOf(count, 0);
    {
        D3D12_GPU_VIRTUAL_ADDRESS last=0;
        for(uint32_t i=0;i<count;++i) {
            const nrd::DispatchDesc& dispatch=dispatches[i];
            if(dispatch.pipelineIndex>=rt.pipelines.size()) { error="NRD named an unknown pipeline"; return false; }
            const nrd::PipelineDesc& pipeline=d.pipelines[dispatch.pipelineIndex];
            uint32_t n=0;
            for(uint32_t range=0;range<pipeline.resourceRangesNum;++range)
                for(uint32_t j=0;j<pipeline.resourceRanges[range].descriptorsNum;++j, ++n) {
                    if(n>=dispatch.resourcesNum) { error="NRD's dispatch lists fewer resources than its pipeline takes"; return false; }
                    const nrd::ResourceDesc& r=dispatch.resources[n];
                    if(r.type==nrd::ResourceType::PERMANENT_POOL || r.type==nrd::ResourceType::TRANSIENT_POOL) {
                        const uint32_t index=r.indexInPool+(r.type==nrd::ResourceType::TRANSIENT_POOL ? d.permanentPoolSize : 0u);
                        if(index>=pools_.size()) { error="NRD named a pool texture it did not describe"; return false; }
                    } else {
                        const NrdTexture* t=external_for(r.type);
                        if(!t || !t->resource || !t->state) { error=std::string("NRD asked for ")+nrd::GetResourceTypeString(r.type)+", which the tracer does not provide"; return false; }
                    }
                }
            if(dispatch.constantBufferDataSize && (!dispatch.constantBufferDataMatchesPreviousDispatch || !last)) {
                void* cpu=nullptr;
                if(!ring.allocate(dispatch.constantBufferDataSize, 256, &cpu, &last)) { error="the frame's upload ring is full"; return false; }
                std::memcpy(cpu, dispatch.constantBufferData, dispatch.constantBufferDataSize);
            }
            constantsOf[i]=last;
        }
    }

    ID3D12DescriptorHeap* heaps[]={rt.heap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(rt.root.Get());
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    auto transition=[&](ID3D12Resource* r, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES to) {
        if(state==to) return;
        D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource=r;
        b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore=state; b.Transition.StateAfter=to;
        barriers.push_back(b); state=to;
    };
    auto flush=[&]() { if(!barriers.empty()) { list->ResourceBarrier(UINT(barriers.size()), barriers.data()); barriers.clear(); } };
    const NrdTexture* external[]={&f.viewZ, &f.motion, &f.normalRoughness, &f.diffuse, &f.diffuseOut, &f.specular, &f.specularOut,
                                 &f.diffuseConfidence, &f.specularConfidence};
    for(uint32_t i=0;i<count;++i) {
        const nrd::DispatchDesc& dispatch=dispatches[i];
        const nrd::PipelineDesc& pipeline=d.pipelines[dispatch.pipelineIndex];
        const uint32_t base=((slot%kSlots)*rt.setsPerSlot+i)*rt.tableSize;
        uint32_t n=0;
        for(uint32_t range=0;range<pipeline.resourceRangesNum;++range) {
            const bool storage=pipeline.resourceRanges[range].descriptorType==nrd::DescriptorType::STORAGE_TEXTURE;
            for(uint32_t j=0;j<pipeline.resourceRanges[range].descriptorsNum;++j, ++n) {
                const nrd::ResourceDesc& r=dispatch.resources[n];
                ID3D12Resource* resource=nullptr; D3D12_RESOURCE_STATES* state=nullptr; DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
                if(r.type==nrd::ResourceType::PERMANENT_POOL || r.type==nrd::ResourceType::TRANSIENT_POOL) {
                    const uint32_t index=r.indexInPool+(r.type==nrd::ResourceType::TRANSIENT_POOL ? d.permanentPoolSize : 0u);
                    resource=pools_[index].resource.Get(); state=&pools_[index].state; format=pools_[index].format;
                } else {
                    const NrdTexture* t=external_for(r.type);
                    resource=t->resource; state=t->state; format=t->format;
                }
                transition(resource, *state, storage ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                if(storage) {
                    D3D12_UNORDERED_ACCESS_VIEW_DESC v{}; v.Format=format; v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
                    rt.device->CreateUnorderedAccessView(resource, nullptr, &v, cpu_handle(base+rt.textures+j));
                } else {
                    D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=format; v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
                    v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2D.MipLevels=1;
                    rt.device->CreateShaderResourceView(resource, &v, cpu_handle(base+j));
                }
            }
        }
        flush();
        list->SetPipelineState(rt.pipelines[dispatch.pipelineIndex].Get());
        if(constantsOf[i]) list->SetComputeRootConstantBufferView(0, constantsOf[i]);
        list->SetComputeRootDescriptorTable(1, gpu_handle(base));
        list->Dispatch(dispatch.gridWidth, dispatch.gridHeight, 1);
        D3D12_RESOURCE_BARRIER uav{}; uav.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV; uav.UAV.pResource=nullptr;
        barriers.push_back(uav); flush();
    }
    // The tracer's textures rest in UNORDERED_ACCESS between its passes.
    for(const NrdTexture* t:external) if(t->resource && t->state) transition(t->resource, *t->state, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    flush();
    restart_=false;
    return true;
}
#else
bool nrd_probe(ID3D12Device*) { log("NVIDIA NRD is not in this build; the built-in denoiser serves"); return false; }
bool nrd_available() { return false; }
bool nrd_use(uint32_t) { return false; }
uint32_t nrd_kind() { return 1; }
void nrd_shutdown() {}
ID3D12PipelineState* nrd_prep_for(ID3D12RootSignature*) { return nullptr; }
void Nrd::destroy() { pools_.clear(); width_=height_=0; restart_=true; }
bool Nrd::ensure(ID3D12Device*, uint32_t, uint32_t, std::string& error) { error="not in this build"; return false; }
bool Nrd::dispatch(ID3D12GraphicsCommandList*, uint32_t, UploadRing&, const NrdFrame&, std::string& error) { error="not in this build"; return false; }
#endif
} // namespace tgpt
