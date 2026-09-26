// SPDX-License-Identifier: MIT
// Path traced frames on Unity's render thread, pipelined by one frame:
//
//   Render event 1 of frame N (recorded by the plugin right after HDRP wrote its GBuffer)
//     1. D3D11, the game's immediate context in an isolated pipeline state: mesh read-backs, material
//        textures into the atlas, the sky into a latitude-longitude map, and HDRP's GBuffer, depth,
//        stencil, motion vectors and exposure into frame N's set of textures shared with D3D12.
//     2. D3D11 signals the producer fence; the private D3D12 compute queue waits for it on the GPU.
//     3. D3D12: uploads and acceleration structure builds, ReSTIR DI, ReSTIR GI and reflections, the
//        denoisers, and frame N's traced light (divided by the diffuse color) into
//        frame N's output textures. It runs alongside HDRP's shadows and lighting and signals the
//        consumer fence when done.
//   Render event 2 of frame N (a custom pass after opaque lighting and the sky, before fog)
//     4. D3D11 applies frame N-1's traced light, reprojected through frame N's motion vectors and
//        multiplied by frame N's own diffuse color, to HDRP's color buffer. Frame N-1's work finished a
//        frame ago, so D3D11 does not wait for D3D12 work of its own frame: the two APIs never
//        serialize within a frame (a GPU wait on a fence the other API signals costs milliseconds).
//
// Each fence is signalled by one API only, so its value never goes backwards. Inputs and outputs are
// double-buffered by frame parity; before D3D11 overwrites a set, D3D12 has finished the frame that read it.
#include "scene.hpp"
#include "ray_regeneration.hpp"
#include "d3d11_shader.hpp"
#include "pt_shaders.hpp"
#include <d3dcompiler.h>
#include <chrono>
#include <map>
#include <cmath>
#include <cstring>

namespace tgpt {
namespace {
constexpr uint32_t kFrames=3;
constexpr uint32_t kHeapSize=1u<<18;
constexpr uint64_t kRingSize=48ull<<20;
// Tags the game's immediate context. ReShade makes a texture's GetDevice return its proxy device, whose
// COM identity differs from the device the bridge hands out; the proxy forwards private data, so the tag
// recognises the game's device through it (as TG Better Upscaling does). An unrelated device still fails.
const GUID kContextTag={0x3f6d2c81,0x57a4,0x4b0e,{0xa1,0x9c,0x2e,0x64,0xd8,0x0b,0x71,0x5a}};

// Kept in step with FrameConstants in pt_common.hlsli.
struct FrameConstants {
    float invViewProj[16], viewProj[16], prevViewProj[16];
    float cameraPos[4], prevCameraPos[4], prevOriginDelta[4], sunDir[4], sunColor[4];
    uint32_t size[4], settings[4], settings2[4];
    float params[4];
    uint32_t inputs0[4], inputs1[4], scene0[4], scene1[4], surface[4], material[4], signals[4], denoise0[4], denoise1[4], denoise2[4];
    float gridMin[4];
    uint32_t grid[4];
    uint32_t extra[4];      // x: the alpha atlas (SRV)
};
static_assert(sizeof(FrameConstants)==544, "frame constants layout");

struct Shared { tgb::SharedTexture t; uint32_t srv=kInvalid, uav=kInvalid; };
struct D11Constants { uint32_t c0[4]; float c1[4]; };
// Kept in step with the Composite cbuffer in d3d11.hlsl.
struct CompositeConstants { float invViewProj[16]; float cameraDelta[4]; uint32_t frameSize[4]; };
static_assert(sizeof(CompositeConstants)==96, "composite constants layout");

// One frame's HDRP textures as D3D12 reads them, and its traced light as the next frame's composite reads it.
struct Inputs { Shared gb0, nrm, gb2, gb3, depth, motion, exposure; };
struct Outputs { Shared a, b; };
// The traced light a frame left for the next one's composite.
struct Result {
    bool valid=false;
    uint64_t frame=0, retire=0;
    uint32_t parity=0, width=0, height=0;
    double camera[3]{};
};

struct State {
    // D3D11
    uint32_t generation=0;
    ComPtr<ID3D11Device5> d11;
    ComPtr<ID3D11DeviceContext4> c11;
    ComPtr<ID3DDeviceContextState> scratch;
    ComPtr<ID3D11ComputeShader> csAlbedo, csFloat4, csDepth, csMotion, csExposure, csSkyCube, csSkyArray, csAtlas, csAlpha, csComposite, csGuides;
    // DLSS Ray Reconstruction's guides (D3D11, the render size), and replaced ones kept alive a while.
    struct Guide { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11UnorderedAccessView> uav; };
    std::array<Guide, 3> guides; uint32_t guideWidth=0, guideHeight=0;
    std::vector<std::pair<uint64_t, ComPtr<ID3D11Texture2D>>> retiredGuides;
    ComPtr<ID3D11VertexShader> vsComposite; ComPtr<ID3D11PixelShader> psComposite;
    ComPtr<ID3D11BlendState> compositeBlend; ComPtr<ID3D11RasterizerState> raster; ComPtr<ID3D11DepthStencilState> noDepth;
    ComPtr<ID3D11Buffer> constants, compositeConstants;
    ComPtr<ID3D11SamplerState> linearClamp, linearWrap;
    bool d3d11Ready=false, d3d12Ready=false; // set once everything ensure_d3d11 / ensure_d3d12 make exists
    bool recording=false;                    // a command list is being recorded (trace)
    // D3D12
    ComPtr<ID3D12Device> sharedDevice;
    bool dxr12=false;       // Shader Model 6.9 and raytracing tier 1.2 (device_ready)
    bool serReorders=false; // and Shader Execution Reordering does reorder on this GPU
    ComPtr<ID3D12Device5> d12;
    ComPtr<ID3D12CommandQueue> queue;
    tgb::SharedFence producer, consumer;
    std::array<ComPtr<ID3D12CommandAllocator>, kFrames> allocators;
    std::array<ComPtr<ID3D12GraphicsCommandList4>, kFrames> lists;
    std::array<uint64_t, kFrames> retired{};
    std::array<UploadRing, kFrames> rings;
    uint32_t listIndex=0;
    DescriptorHeap heap;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> prepare, gi, giSpatial, di, diSpatial, specular, temporal, atrous, compose, ommBake, rrgGuides, skinUnpack;
    // DirectX Raytracing 1.2: the Shader Model 6.9 passes (their ray queries accept opacity micromaps),
    // and the GI and reflection passes as ray generation shaders that reorder their threads (SER).
    bool useDxr12=false;
    ComPtr<ID3D12StateObject> giSer, specularSer; Buffer serTable;
    ComPtr<ID3D12QueryHeap> queries; Buffer queryReadback; double ticksPerMs=0;
    std::array<bool, kFrames> queried{};
    DeferredRelease release;
    // Shared with D3D11.
    Shared atlas, alphaAtlas, sky;
    // Characters: their skinned positions a frame, per parity (D3D11 writes one while D3D12 may read the other).
    std::array<Shared, 2> skin;
    struct SkinWork { uint32_t firstTexel=0, count=0; TGPT_SkinnedInstance instance{}; };
    std::array<std::vector<SkinWork>, 2> skinWork;
    // The D3D11 copy pass, compiled for the skinning output's layout (raw, or structured of a given stride).
    std::map<uint32_t, ComPtr<ID3D11ComputeShader>> skinCs;
    ComPtr<ID3D11Buffer> skinSourceBuffer; ComPtr<ID3D11ShaderResourceView> skinSourceSrv; uint32_t skinSourceKey=0;
    bool skinLogged=false;
    std::array<Inputs, 2> inputs;
    std::array<Outputs, 2> outputs;
    std::array<uint64_t, 2> inputsRetire{}; // the consumer value after which D3D12 no longer reads each input set
    uint32_t width=0, height=0;
    // D3D12 only, per render size.
    std::array<Texture, 2> surface, normal; Texture material;
    std::array<Buffer, 2> giRes, diRes; std::array<uint32_t, 2> giUav{kInvalid, kInvalid}, diUav{kInvalid, kInvalid};
    Texture diffuseRaw, sunRaw, specRaw, tmpA, tmpB, tmpC, tmpD, directTmp, directOut;
    std::array<Texture, 2> diffuseHist, diffuseMoments, directHist, directMoments, specHist, specMoments;
    // AMD FSR Ray Regeneration (Radeon RX 9000): its guides at the render size and its context. A failure
    // leaves the built-in denoiser for the rest of the session.
    Texture rrgDepth, rrgMotion, rrgNormals, rrgDiffuse, rrgSpecular;
    RayRegeneration rrg;
    bool rrgFailed=false, rrgActive=false;
    bool denoisedLast=false; // the built-in denoiser ran last frame (its history is current)
    uint32_t parity=0;
    GpuScene scene;
    Result latest;
    // Bookkeeping.
    bool broken=false;
    uint64_t frame=0;
    double origin[3]{}, prevOrigin[3]{};
    bool haveOrigin=false, resetNext=true;
    uint32_t skyVersion=0; uint64_t skyFrame=0; bool skyValid=false;
    uint64_t lastSummary=0, summaryFrames=0; double summaryGpu=0, summaryCpu=0;
    std::array<double, 4> phase{}, summaryPhase{}; // event 1: D3D11 work, flush, waiting for a free list, recording
    uint32_t failures=0;
    uint64_t composited=0, compositeWaits=0, compositeSkipped=0;
} st;

ComPtr<ID3DBlob> compile_d3d11(const char* macro, const char* entry, const char* profile, const D3D_SHADER_MACRO* extra=nullptr) {
    std::vector<D3D_SHADER_MACRO> defines{{macro, "1"}};
    for(const D3D_SHADER_MACRO* d=extra; d && d->Name; ++d) defines.push_back(*d);
    defines.push_back({nullptr, nullptr});
    ComPtr<ID3DBlob> bytecode, errors;
    const HRESULT r=D3DCompile(d3d11Shader, std::strlen(d3d11Shader), "TGPathTracing/d3d11.hlsl", defines.data(), nullptr,
        entry, profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors);
    if(FAILED(r) && errors) log(std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()));
    check(r, "Compile path tracing D3D11 shader"); return bytecode;
}
ComPtr<ID3D11ComputeShader> make_cs(const char* macro) {
    auto b=compile_d3d11(macro, "main", "cs_5_0"); ComPtr<ID3D11ComputeShader> cs;
    check(st.d11->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &cs), "Create path tracing D3D11 compute shader"); return cs;
}

void release_shared(Shared& s) {
    if(s.t.d12) st.release.add(st.consumer.value, s.t.d12.Get());
    st.heap.release(s.srv); st.heap.release(s.uav);
    s=Shared{};
}
Shared make_shared(UINT w, UINT h, DXGI_FORMAT format, bool d12Writes) {
    Shared s;
    s.t=tgb::SharedTexture::make(st.d11.Get(), st.d12.Get(), w, h, format, D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    s.srv=texture_srv(st.d12.Get(), st.heap, s.t.d12.Get(), format);
    if(d12Writes) s.uav=texture_uav(st.d12.Get(), st.heap, s.t.d12.Get(), format);
    return s;
}

void release_d3d11() {
    st.csAlbedo.Reset(); st.csFloat4.Reset(); st.csDepth.Reset(); st.csMotion.Reset(); st.csExposure.Reset();
    st.csSkyCube.Reset(); st.csSkyArray.Reset(); st.csAtlas.Reset(); st.csAlpha.Reset(); st.csComposite.Reset(); st.vsComposite.Reset(); st.psComposite.Reset();
    st.csGuides.Reset(); st.guides={}; st.guideWidth=st.guideHeight=0; st.retiredGuides.clear();
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.guides=TGPT_Guides{sizeof(TGPT_Guides), TGPT_ABI}; }
    st.compositeBlend.Reset(); st.raster.Reset(); st.noDepth.Reset(); st.constants.Reset(); st.compositeConstants.Reset();
    st.linearClamp.Reset(); st.linearWrap.Reset();
    if(st.c11) {
        UINT size=0; void* none=nullptr;
        if(SUCCEEDED(st.c11->GetPrivateData(kContextTag, &size, nullptr))) st.c11->SetPrivateData(kContextTag, 0, none);
    }
    st.scratch.Reset(); st.c11.Reset(); st.d11.Reset();
    st.d3d11Ready=false;
}
bool on_game_device(ID3D11Resource* resource) {
    ComPtr<ID3D11Device> device; resource->GetDevice(&device);
    if(!device) return false;
    if(tgb::same_object(device.Get(), st.d11.Get())) return true;
    ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
    UINT value=0, size=sizeof(value);
    return context && SUCCEEDED(context->GetPrivateData(kContextTag, &size, &value)) && size==sizeof(value) && value==1;
}
// Every resource tied to the D3D12 device or to the game's D3D11 device, after the GPU finished with them.
void release_all() {
    if(st.queue && st.consumer.ready()) { try { st.consumer.wait_cpu(st.consumer.value); } catch(...) {} }
    st.atlas=Shared{}; st.alphaAtlas=Shared{}; st.sky=Shared{};
    st.skin={}; for(auto& w:st.skinWork) w.clear();
    st.skinCs.clear(); st.skinSourceBuffer.Reset(); st.skinSourceSrv.Reset(); st.skinSourceKey=0;
    for(auto& in:st.inputs) in=Inputs{};
    for(auto& out:st.outputs) out=Outputs{};
    st.scene.clear(nullptr, st.release, st.consumer.value);
    st.release.clear();
    st.surface={}; st.normal={}; st.material={}; st.giRes={}; st.diRes={};
    st.diffuseRaw={}; st.sunRaw={}; st.specRaw={}; st.tmpA={}; st.tmpB={}; st.tmpC={}; st.tmpD={}; st.directTmp={}; st.directOut={};
    st.diffuseHist={}; st.diffuseMoments={}; st.directHist={}; st.directMoments={}; st.specHist={}; st.specMoments={};
    st.width=st.height=0; st.inputsRetire={}; st.latest=Result{};
    st.producer.reset(); st.consumer.reset();
    st.heap.reset();
    st.allocators={}; st.lists={}; st.rings={}; st.retired.fill(0);
    st.queue.Reset(); st.root.Reset();
    st.prepare.Reset(); st.gi.Reset(); st.giSpatial.Reset(); st.di.Reset(); st.diSpatial.Reset(); st.specular.Reset();
    st.temporal.Reset(); st.atrous.Reset(); st.compose.Reset(); st.queries.Reset(); st.queryReadback=Buffer{};
    st.ommBake.Reset(); st.skinUnpack.Reset(); st.giSer.Reset(); st.specularSer.Reset(); st.serTable=Buffer{}; st.useDxr12=false; st.scene.ommEnabled=false;
    st.rrg.destroy(); st.rrgGuides.Reset(); st.rrgActive=false;
    st.rrgDepth={}; st.rrgMotion={}; st.rrgNormals={}; st.rrgDiffuse={}; st.rrgSpecular={};
    st.skyValid=false; st.haveOrigin=false; st.resetNext=true;
    st.d3d12Ready=false;
    release_d3d11();
}

bool ensure_d3d11() {
    auto& h=host();
    const uint32_t generation=h.game_device_generation();
    if(st.d11 && generation==st.generation && st.d3d11Ready) return true;
    ID3D11Device5* device=h.game_device(); ID3D11DeviceContext4* context=h.game_context();
    if(!device || !context) return false;
    // A start that failed part-way is undone and tried again; so is everything on a new device.
    if(st.d11 && generation!=st.generation) log("The game created a new D3D11 device; the path tracer rebuilds its resources");
    if(st.d11) release_all();
    try {
        st.d11=device; st.c11=context; st.generation=generation;
        const UINT one=1; st.c11->SetPrivateData(kContextTag, sizeof(one), &one);
        const auto level=st.d11->GetFeatureLevel(); D3D_FEATURE_LEVEL chosen{};
        check(st.d11->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &chosen, &st.scratch), "Create isolated D3D11 pipeline state");
        st.csAlbedo=make_cs("TGPT_COPY_ALBEDO"); st.csFloat4=make_cs("TGPT_COPY_FLOAT4"); st.csDepth=make_cs("TGPT_COPY_DEPTH");
        st.csMotion=make_cs("TGPT_COPY_MOTION"); st.csExposure=make_cs("TGPT_COPY_EXPOSURE"); st.csSkyCube=make_cs("TGPT_SKY_CUBE");
        st.csSkyArray=make_cs("TGPT_SKY_CUBEARRAY"); st.csAtlas=make_cs("TGPT_ATLAS"); st.csAlpha=make_cs("TGPT_ALPHA"); st.csComposite=make_cs("TGPT_COMPOSITE");
        st.csGuides=make_cs("TGPT_GUIDES");
        auto vs=compile_d3d11("TGPT_COMPOSITE_RASTER", "vsMain", "vs_5_0"), ps=compile_d3d11("TGPT_COMPOSITE_RASTER", "psMain", "ps_5_0");
        check(st.d11->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &st.vsComposite), "Create composite VS");
        check(st.d11->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &st.psComposite), "Create composite PS");
        D3D11_BLEND_DESC blend{}; auto& rt=blend.RenderTarget[0];
        rt.BlendEnable=TRUE; rt.SrcBlend=D3D11_BLEND_ONE; rt.DestBlend=D3D11_BLEND_SRC_ALPHA; rt.BlendOp=D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha=D3D11_BLEND_ZERO; rt.DestBlendAlpha=D3D11_BLEND_ONE; rt.BlendOpAlpha=D3D11_BLEND_OP_ADD; rt.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(st.d11->CreateBlendState(&blend, &st.compositeBlend), "Create composite blend state");
        D3D11_RASTERIZER_DESC raster{}; raster.FillMode=D3D11_FILL_SOLID; raster.CullMode=D3D11_CULL_NONE; raster.DepthClipEnable=TRUE;
        check(st.d11->CreateRasterizerState(&raster, &st.raster), "Create composite raster state");
        D3D11_DEPTH_STENCIL_DESC depth{}; check(st.d11->CreateDepthStencilState(&depth, &st.noDepth), "Create composite depth state");
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=sizeof(D11Constants); cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(st.d11->CreateBuffer(&cb, nullptr, &st.constants), "Create path tracing D3D11 constants");
        cb.ByteWidth=sizeof(CompositeConstants);
        check(st.d11->CreateBuffer(&cb, nullptr, &st.compositeConstants), "Create composite constants");
        D3D11_SAMPLER_DESC s{}; s.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; s.MaxLOD=D3D11_FLOAT32_MAX;
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP; check(st.d11->CreateSamplerState(&s, &st.linearClamp), "Create clamp sampler");
        s.AddressU=s.AddressV=s.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; check(st.d11->CreateSamplerState(&s, &st.linearWrap), "Create wrap sampler");
    } catch(...) { release_all(); throw; }
    st.d3d11Ready=true;
    return true;
}

ComPtr<ID3D12PipelineState> make_pso(const unsigned char* code, size_t size) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{}; d.pRootSignature=st.root.Get(); d.CS.pShaderBytecode=code; d.CS.BytecodeLength=size;
    ComPtr<ID3D12PipelineState> pso; check(st.d12->CreateComputePipelineState(&d, IID_PPV_ARGS(&pso)), "Create path tracing pipeline"); return pso;
}
// A ray tracing pipeline of one ray generation shader (a pass that traces with ray queries only, so no
// hit groups or payload), bound to the root signature every pass shares.
ComPtr<ID3D12StateObject> make_raygen(const unsigned char* code, size_t size, const wchar_t* name) {
    D3D12_EXPORT_DESC exported{name, nullptr, D3D12_EXPORT_FLAG_NONE};
    D3D12_DXIL_LIBRARY_DESC library{}; library.DXILLibrary.pShaderBytecode=code; library.DXILLibrary.BytecodeLength=size;
    library.NumExports=1; library.pExports=&exported;
    D3D12_GLOBAL_ROOT_SIGNATURE root{st.root.Get()};
    D3D12_RAYTRACING_SHADER_CONFIG shader{4, 8};
    D3D12_RAYTRACING_PIPELINE_CONFIG pipeline{1};
    const D3D12_STATE_SUBOBJECT subobjects[]={
        {D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &library}, {D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &root},
        {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shader}, {D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG, &pipeline}};
    D3D12_STATE_OBJECT_DESC d{}; d.Type=D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE; d.NumSubobjects=UINT(std::size(subobjects)); d.pSubobjects=subobjects;
    ComPtr<ID3D12StateObject> so; check(st.d12->CreateStateObject(&d, IID_PPV_ARGS(&so)), "Create ray generation pipeline"); return so;
}
// The pass pipelines: Shader Model 6.9 where DirectX Raytracing 1.2 is available, 6.6 otherwise, and
// with Shader Execution Reordering where the GPU reorders. A 6.9 set the driver refuses falls back to 6.6.
void create_pipelines() {
    st.useDxr12=false; st.giSer.Reset(); st.specularSer.Reset(); st.serTable=Buffer{};
    if(st.dxr12) {
        try {
            st.prepare=make_pso(shaders::prepareDxr12, sizeof(shaders::prepareDxr12));
            st.gi=make_pso(shaders::giDxr12, sizeof(shaders::giDxr12));
            st.giSpatial=make_pso(shaders::giSpatialDxr12, sizeof(shaders::giSpatialDxr12));
            st.di=make_pso(shaders::diDxr12, sizeof(shaders::diDxr12));
            st.diSpatial=make_pso(shaders::diSpatialDxr12, sizeof(shaders::diSpatialDxr12));
            st.specular=make_pso(shaders::specularDxr12, sizeof(shaders::specularDxr12));
            st.temporal=make_pso(shaders::temporalDxr12, sizeof(shaders::temporalDxr12));
            st.atrous=make_pso(shaders::atrousDxr12, sizeof(shaders::atrousDxr12));
            st.compose=make_pso(shaders::composeDxr12, sizeof(shaders::composeDxr12));
            st.useDxr12=true;
        } catch(const DxError& e) { log(std::string("The Shader Model 6.9 passes could not be created (")+e.what()+" "+tgb::hexhr(e.hr)+"); the 6.6 passes run without opacity micromaps"); }
    }
    if(!st.useDxr12) {
        st.prepare=make_pso(shaders::prepare, sizeof(shaders::prepare));
        st.gi=make_pso(shaders::gi, sizeof(shaders::gi));
        st.giSpatial=make_pso(shaders::giSpatial, sizeof(shaders::giSpatial));
        st.di=make_pso(shaders::di, sizeof(shaders::di));
        st.diSpatial=make_pso(shaders::diSpatial, sizeof(shaders::diSpatial));
        st.specular=make_pso(shaders::specular, sizeof(shaders::specular));
        st.temporal=make_pso(shaders::temporal, sizeof(shaders::temporal));
        st.atrous=make_pso(shaders::atrous, sizeof(shaders::atrous));
        st.compose=make_pso(shaders::compose, sizeof(shaders::compose));
    }
    st.ommBake=make_pso(shaders::ommBake, sizeof(shaders::ommBake));
    st.rrgGuides=make_pso(shaders::rrgGuides, sizeof(shaders::rrgGuides));
    st.skinUnpack=make_pso(shaders::skinUnpack, sizeof(shaders::skinUnpack));
    // Every ray query of the 6.9 passes accepts micromaps; a 6.6 query must never meet one.
    st.scene.ommEnabled=st.useDxr12;
    if(st.useDxr12 && st.serReorders) {
        try {
            st.giSer=make_raygen(shaders::giSer, sizeof(shaders::giSer), L"gi");
            st.specularSer=make_raygen(shaders::specularSer, sizeof(shaders::specularSer), L"specular");
            // The shader table: each ray generation record on its own 64-byte boundary.
            st.serTable=make_buffer(st.d12.Get(), 2*D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                D3D12_RESOURCE_STATE_GENERIC_READ, "Create ray generation shader table");
            ComPtr<ID3D12StateObjectProperties> gi, specular;
            check(st.giSer.As(&gi), "Ray generation pipeline properties"); check(st.specularSer.As(&specular), "Ray generation pipeline properties");
            const void* ids[2]={gi->GetShaderIdentifier(L"gi"), specular->GetShaderIdentifier(L"specular")};
            if(!ids[0] || !ids[1]) throw DxError(E_FAIL, "a ray generation shader has no identifier");
            for(int i=0;i<2;++i) std::memcpy(static_cast<uint8_t*>(st.serTable.mapped)+i*D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT, ids[i], D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES);
        } catch(const DxError& e) {
            log(std::string("Shader Execution Reordering could not be set up (")+e.what()+" "+tgb::hexhr(e.hr)+"); GI and reflections run as compute passes");
            st.giSer.Reset(); st.specularSer.Reset(); st.serTable=Buffer{};
        }
    }
}
bool ensure_d3d12() {
    if(st.d3d12Ready) return true;
    if(!st.sharedDevice || !st.d11) return false;
    // A start that fails part-way leaves nothing behind: the next frame starts over (or, after enough
    // failures, path tracing turns off), never records with what exists.
    try {
        check(st.sharedDevice.As(&st.d12), "The shared D3D12 device has no ID3D12Device5");
        // A compute queue: its work overlaps HDRP's graphics work instead of taking turns with it.
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_COMPUTE;
        check(st.d12->CreateCommandQueue(&q, IID_PPV_ARGS(&st.queue)), "Create path tracing queue");
        st.producer.create(st.d12.Get(), st.d11.Get());
        st.consumer.create(st.d12.Get(), st.d11.Get());
        for(uint32_t i=0;i<kFrames;++i) {
            check(st.d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&st.allocators[i])), "Create path tracing allocator");
            ComPtr<ID3D12GraphicsCommandList> list;
            check(st.d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, st.allocators[i].Get(), nullptr, IID_PPV_ARGS(&list)), "Create path tracing list");
            check(list.As(&st.lists[i]), "The command list has no ID3D12GraphicsCommandList4");
            check(st.lists[i]->Close(), "Close initial path tracing list");
            st.rings[i].create(st.d12.Get(), kRingSize);
        }
        st.retired.fill(0);
        st.heap.create(st.d12.Get(), kHeapSize);
        check(st.d12->CreateRootSignature(0, shaders::prepare, sizeof(shaders::prepare), IID_PPV_ARGS(&st.root)), "Create path tracing root signature");
        create_pipelines();
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count=kFrames*2;
        check(st.d12->CreateQueryHeap(&qh, IID_PPV_ARGS(&st.queries)), "Create path tracing timestamps");
        st.queryReadback=make_buffer(st.d12.Get(), kFrames*16, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create timestamp read-back");
        UINT64 frequency=0; if(SUCCEEDED(st.queue->GetTimestampFrequency(&frequency)) && frequency) st.ticksPerMs=double(frequency)/1000.0;
        st.queried.fill(false);
        // The atlas starts white: a texture not baked yet leaves its material's own color.
        st.atlas=make_shared(kAtlasSize, kAtlasSize, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        { const float white[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.atlas.t.uav.Get(), white); }
        // The alpha atlas starts opaque: a tile not baked yet keeps its surface solid, as before its texture came.
        st.alphaAtlas=make_shared(kAtlasSize, kAtlasSize, DXGI_FORMAT_R8_UNORM, false);
        { const float one[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.alphaAtlas.t.uav.Get(), one); }
        st.sky=make_shared(kSkyWidth, kSkyHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        { const float blue[4]={0.4f, 0.6f, 1.0f, 1}; st.c11->ClearUnorderedAccessViewFloat(st.sky.t.uav.Get(), blue); }
        log("Path tracing ready: private D3D12 compute queue, producer and consumer fences, "+std::to_string(kHeapSize)+" descriptors, "+
            (st.useDxr12 ? std::string("Shader Model 6.9 passes (opacity micromaps")+(st.giSer ? ", Shader Execution Reordering)" : ")") : std::string("Shader Model 6.6 passes")));
    } catch(...) { release_all(); throw; }
    st.d3d12Ready=true;
    return true;
}

void set_constants(const D11Constants& c) { st.c11->UpdateSubresource(st.constants.Get(), 0, nullptr, &c, 0, 0); }
void run_cs(ID3D11ComputeShader* cs, std::initializer_list<ID3D11ShaderResourceView*> srvs, ID3D11UnorderedAccessView* uav, UINT x, UINT y,
            ID3D11SamplerState* sampler=nullptr) {
    auto* cb=st.constants.Get();
    st.c11->CSSetShader(cs, nullptr, 0); st.c11->CSSetConstantBuffers(0, 1, &cb);
    std::vector<ID3D11ShaderResourceView*> views(srvs);
    st.c11->CSSetShaderResources(0, UINT(views.size()), views.data());
    st.c11->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
    if(sampler) st.c11->CSSetSamplers(0, 1, &sampler);
    st.c11->Dispatch(x, y, 1);
    std::vector<ID3D11ShaderResourceView*> none(views.size(), nullptr); st.c11->CSSetShaderResources(0, UINT(none.size()), none.data());
    ID3D11UnorderedAccessView* noUav=nullptr; st.c11->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
}

// A texture from the game's device read through a view with the given format.
ComPtr<ID3D11ShaderResourceView> view_as(ID3D11Resource* resource, DXGI_FORMAT format) {
    D3D11_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=format; d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels=1;
    ComPtr<ID3D11ShaderResourceView> v; check(st.d11->CreateShaderResourceView(resource, &d, &v), "Create input view"); return v;
}
D3D11_TEXTURE2D_DESC texture_desc(ID3D11Resource* resource) {
    ComPtr<ID3D11Texture2D> t; check(resource->QueryInterface(IID_PPV_ARGS(&t)), "Input is not a 2D texture");
    D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); return d;
}
DXGI_FORMAT stencil_view(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
    case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}
// A material texture's color view: typeless color formats are read as sRGB, as Unity creates color textures.
DXGI_FORMAT color_view(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
    case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM_SRGB;
    case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM_SRGB;
    case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM_SRGB;
    case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM_SRGB;
    case DXGI_FORMAT_BC4_TYPELESS: return DXGI_FORMAT_BC4_UNORM;
    case DXGI_FORMAT_BC5_TYPELESS: return DXGI_FORMAT_BC5_UNORM;
    case DXGI_FORMAT_BC6H_TYPELESS: return DXGI_FORMAT_BC6H_UF16;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
    default: return f;
    }
}

// The atlas and the sky are single textures the last frame's D3D12 work may still be reading: D3D11 writes
// them only after that work is done (a GPU wait, and only when it is not done already).
void wait_for_scene_readers() {
    if(st.consumer.value && st.consumer.d12->GetCompletedValue()<st.consumer.value)
        check(st.c11->Wait(st.consumer.d11.Get(), st.consumer.value), "D3D11 wait before updating the atlas or the sky");
}

void bake_textures() {
    std::vector<TextureBake> bakes;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=st.scene.clear_generation()) return; // after the scene has taken the clear in
        const size_t n=std::min<size_t>(e.newTextures.size(), 24);
        bakes.assign(std::make_move_iterator(e.newTextures.begin()), std::make_move_iterator(e.newTextures.begin()+n));
        e.newTextures.erase(e.newTextures.begin(), e.newTextures.begin()+n);
    }
    if(!bakes.empty()) wait_for_scene_readers();
    for(auto& b:bakes) {
        // Whatever becomes of it, the tile is final from this frame on (a texture that can't be read keeps it white).
        st.scene.tile_settled(b.tile);
        try {
            ComPtr<ID3D11Texture2D> t; check(b.texture.As(&t), "Material texture is not 2D");
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
            // Texture arrays, multisampled and unreadable textures keep their material's own color.
            if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format);
            v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
            ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(st.d11->CreateShaderResourceView(t.Get(), &v, &srv))) { log_throttled("A material texture (format "+std::to_string(d.Format)+") has no color view; its material keeps its own color"); continue; }
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/128.f));
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kTilesPerRow*kTileSize; c.c0[1]=uint32_t(b.tile)/kTilesPerRow*kTileSize; c.c1[0]=std::min(lod, float(d.MipLevels-1));
            set_constants(c);
            run_cs(st.csAtlas.Get(), {srv.Get()}, st.atlas.t.uav.Get(), kTileSize/8, kTileSize/8, st.linearWrap.Get());
        } catch(const std::exception& e) { log_throttled(std::string("Material texture bake failed: ")+e.what()); }
    }
}

// Alpha-tested materials' textures into their 256x256 alpha tiles, a few a frame, like the color tiles.
void bake_alpha_textures() {
    std::vector<TextureBake> bakes;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        if(e.clearGeneration!=st.scene.clear_generation()) return; // after the scene has taken the clear in
        const size_t n=std::min<size_t>(e.newAlphaTextures.size(), 8);
        bakes.assign(std::make_move_iterator(e.newAlphaTextures.begin()), std::make_move_iterator(e.newAlphaTextures.begin()+n));
        e.newAlphaTextures.erase(e.newAlphaTextures.begin(), e.newAlphaTextures.begin()+n);
    }
    if(!bakes.empty()) wait_for_scene_readers();
    for(auto& b:bakes) {
        st.scene.alpha_tile_settled(b.tile);
        try {
            ComPtr<ID3D11Texture2D> t; check(b.texture.As(&t), "Material texture is not 2D");
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
            if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format);
            v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
            ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(st.d11->CreateShaderResourceView(t.Get(), &v, &srv))) { log_throttled("An alpha-tested texture (format "+std::to_string(d.Format)+") has no view; its alpha tile stays opaque"); continue; }
            // Sampled 2x2 a texel at the mip nearest twice the tile's size.
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/float(2*kAlphaTileSize)));
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kAlphaTilesPerRow*kAlphaTileSize; c.c0[1]=uint32_t(b.tile)/kAlphaTilesPerRow*kAlphaTileSize; c.c1[0]=std::min(lod, float(d.MipLevels-1));
            set_constants(c);
            run_cs(st.csAlpha.Get(), {srv.Get()}, st.alphaAtlas.t.uav.Get(), kAlphaTileSize/8, kAlphaTileSize/8, st.linearWrap.Get());
        } catch(const std::exception& e) { log_throttled(std::string("Alpha texture bake failed: ")+e.what()); }
    }
}

// Characters' skinned positions from the game's skinning output into this frame's shared texture, a dispatch
// each, in the order given until the texture is full. What was copied is this parity's skin work for D3D12.
void copy_skinned(const ComPtr<ID3D11Buffer>& source, uint32_t stride, const std::vector<TGPT_SkinnedInstance>& skinned, uint32_t parity) {
    auto& work=st.skinWork[parity];
    work.clear();
    if(!source || skinned.empty()) return;
    D3D11_BUFFER_DESC bd{}; source->GetDesc(&bd);
    const bool structured=(bd.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)!=0, raw=(bd.MiscFlags&D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS)!=0;
    if(structured) stride=bd.StructureByteStride;
    if((!structured && !raw) || !(bd.BindFlags&D3D11_BIND_SHADER_RESOURCE) || stride<12 || stride%4) {
        if(!st.skinLogged) { st.skinLogged=true; log("Characters are not traced: the skinning output is neither a readable structured nor a raw buffer (misc 0x"+
            tgb::hexhr(HRESULT(bd.MiscFlags))+", stride "+std::to_string(stride)+")"); }
        return;
    }
    const uint32_t vertices=bd.ByteWidth/stride;
    // Views of the game's buffer, remade when it is replaced (Unity grows it by re-creating it).
    const uint32_t key=(structured ? 0x10000u : 0u)|stride;
    if(source.Get()!=st.skinSourceBuffer.Get() || !st.skinSourceSrv) {
        D3D11_SHADER_RESOURCE_VIEW_DESC v{};
        if(structured) { v.Format=DXGI_FORMAT_UNKNOWN; v.ViewDimension=D3D11_SRV_DIMENSION_BUFFER; v.Buffer.FirstElement=0; v.Buffer.NumElements=vertices; }
        else { v.Format=DXGI_FORMAT_R32_TYPELESS; v.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX; v.BufferEx.FirstElement=0; v.BufferEx.NumElements=bd.ByteWidth/4; v.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW; }
        st.skinSourceSrv.Reset();
        check(st.d11->CreateShaderResourceView(source.Get(), &v, &st.skinSourceSrv), "Create the skinning output's view");
        st.skinSourceBuffer=source;
    }
    auto& cs=st.skinCs[key];
    if(!cs) {
        const std::string rest=std::to_string((stride-12)/4);
        const D3D_SHADER_MACRO defines[]={{structured ? "SKIN_STRUCTURED" : "SKIN_RAW", "1"}, {"SKIN_REST", rest.c_str()}, {nullptr, nullptr}};
        auto b=compile_d3d11("TGPT_SKIN", "main", "cs_5_0", defines);
        check(st.d11->CreateComputeShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &cs), "Create the character copy shader");
        log("Characters: skinning output "+std::string(structured ? "structured" : "raw")+", "+std::to_string(stride)+" bytes a vertex, "+std::to_string(vertices)+" vertices");
    }
    if(!st.skin[parity].t.d12) st.skin[parity]=make_shared(kSkinWidth, kSkinRows, DXGI_FORMAT_R32G32B32A32_FLOAT, false);
    uint32_t texel=0;
    for(const auto& s:skinned) {
        if(!s.vertexCount || uint64_t(s.firstVertex)+s.vertexCount>vertices || texel+s.vertexCount>kSkinTexels) continue;
        // A mesh once a frame: two unpacks into one position buffer and two builds of one BLAS would race on the GPU.
        if(std::any_of(work.begin(), work.end(), [&](const State::SkinWork& w) { return w.instance.mesh==s.mesh; })) continue;
        D11Constants c{}; c.c0[0]=s.firstVertex; c.c0[1]=s.vertexCount; c.c0[2]=texel; c.c0[3]=stride;
        set_constants(c);
        run_cs(cs.Get(), {st.skinSourceSrv.Get()}, st.skin[parity].t.uav.Get(), (s.vertexCount+63)/64, 1);
        State::SkinWork w; w.firstTexel=texel; w.count=s.vertexCount; w.instance=s;
        work.push_back(w);
        texel+=s.vertexCount;
    }
}

void update_sky() {
    ComPtr<ID3D11Resource> sky; uint32_t version=0;
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); sky=e.sky; version=e.skyVersion; }
    // HDRP re-renders its sky as the time of day moves: resampled every half second or so.
    if(!sky || (version==st.skyVersion && st.skyValid && st.frame-st.skyFrame<30)) return;
    st.skyVersion=version; st.skyFrame=st.frame;
    ComPtr<ID3D11Texture2D> t; if(FAILED(sky.As(&t))) return;
    wait_for_scene_readers();
    D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
    if(!(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) || d.ArraySize<6) { log_throttled("HDRP's sky is not a cubemap; the tracer keeps a plain sky"); return; }
    D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format);
    const bool array=d.ArraySize>6;
    if(array) { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBEARRAY; v.TextureCubeArray.MipLevels=d.MipLevels; v.TextureCubeArray.NumCubes=1; }
    else { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE; v.TextureCube.MipLevels=d.MipLevels; }
    ComPtr<ID3D11ShaderResourceView> srv; check(st.d11->CreateShaderResourceView(t.Get(), &v, &srv), "Create sky view");
    D11Constants c{}; c.c0[0]=kSkyWidth; c.c0[1]=kSkyHeight; c.c1[0]=std::min(1.f, float(d.MipLevels-1));
    set_constants(c);
    run_cs(array ? st.csSkyArray.Get() : st.csSkyCube.Get(), {srv.Get()}, st.sky.t.uav.Get(), kSkyWidth/8, kSkyHeight/8, st.linearClamp.Get());
    if(!st.skyValid) log("Sky: HDRP's "+std::string(array ? "cubemap array" : "cubemap")+" "+std::to_string(d.Width)+"px, resampled every 30 frames");
    st.skyValid=true;
}

void ensure_frame_textures(uint32_t w, uint32_t h) {
    if(st.width==w && st.height==h && st.outputs[0].a.t.d11) return;
    // Whatever used the old textures has to be finished on the GPU first.
    st.consumer.wait_cpu(st.consumer.value);
    for(auto& in:st.inputs) for(Shared* s:{&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure}) release_shared(*s);
    for(auto& out:st.outputs) { release_shared(out.a); release_shared(out.b); }
    auto drop=[](Texture& t) { if(t.resource) st.release.add(st.consumer.value, t.resource.Get()); st.heap.release(t.srv); st.heap.release(t.uav); t=Texture{}; };
    for(auto& t:st.surface) drop(t);
    for(auto& t:st.normal) drop(t);
    drop(st.material);
    for(Texture* t:{&st.diffuseRaw, &st.sunRaw, &st.specRaw, &st.tmpA, &st.tmpB, &st.tmpC, &st.tmpD, &st.directTmp, &st.directOut,
                    &st.rrgDepth, &st.rrgMotion, &st.rrgNormals, &st.rrgDiffuse, &st.rrgSpecular}) drop(*t);
    for(auto* a:{&st.diffuseHist, &st.diffuseMoments, &st.directHist, &st.directMoments, &st.specHist, &st.specMoments}) for(auto& t:*a) drop(t);
    for(int i=0;i<2;++i) {
        if(st.giRes[i]) st.release.add(st.consumer.value, st.giRes[i].resource.Get());
        if(st.diRes[i]) st.release.add(st.consumer.value, st.diRes[i].resource.Get());
        st.heap.release(st.giUav[i]); st.heap.release(st.diUav[i]);
    }
    auto* d=st.d12.Get(); auto& heap=st.heap;
    for(int i=0;i<2;++i) {
        auto& in=st.inputs[i];
        in.gb0=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        in.nrm=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        in.gb2=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        in.gb3=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        in.depth=make_shared(w, h, DXGI_FORMAT_R32_FLOAT, false);
        in.motion=make_shared(w, h, DXGI_FORMAT_R16G16_FLOAT, false);
        in.exposure=make_shared(1, 1, DXGI_FORMAT_R32_FLOAT, false);
        st.outputs[i].a=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        st.outputs[i].b=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
        st.surface[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create surface texture");
        st.normal[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create normal texture");
        st.diffuseHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create diffuse history");
        st.diffuseMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create diffuse moments");
        st.directHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light history");
        st.directMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light moments");
        st.specHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular history");
        st.specMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular moments");
        st.giRes[i]=make_buffer(d, uint64_t(w)*h*32, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create GI reservoirs");
        st.diRes[i]=make_buffer(d, uint64_t(w)*h*16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create DI reservoirs");
        st.giUav[i]=structured_uav(d, heap, st.giRes[i].resource.Get(), w*h, 32);
        st.diUav[i]=structured_uav(d, heap, st.diRes[i].resource.Get(), w*h, 16);
    }
    st.material=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create material texture");
    st.diffuseRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create diffuse signal");
    st.sunRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light signal");
    st.specRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular signal");
    st.tmpA=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create denoiser ping");
    st.tmpB=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create denoiser pong");
    st.tmpC=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular denoiser ping");
    st.tmpD=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular denoiser pong");
    st.directTmp=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light denoiser ping");
    st.directOut=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light denoiser output");
    st.width=w; st.height=h; st.resetNext=true; st.latest=Result{}; st.inputsRetire={};
    // Per pixel: inputs 2x28 B, outputs 2x16 B, surfaces 2x24 B, material 8 B, reservoirs 2x48 B, signals and denoiser 21x8 B.
    const double mb=double(w)*h*(2*28+2*16+2*24+8+2*48+21*8)/1048576.0;
    log("Render size "+std::to_string(w)+"x"+std::to_string(h)+": tracer textures and reservoirs recreated (about "+std::to_string(int(mb))+" MB)");
}

// HDRP's frame into one input set, over the render rectangle.
void copy_inputs(const TGPT_Frame& f, Inputs& in) {
    auto* gb0=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.gbuffer0));
    auto* nb=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.normal));
    auto* gb2=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.gbuffer2));
    auto* gb3=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.gbuffer3));
    auto* depth=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.depth));
    auto* motion=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.motion));
    auto* exposure=reinterpret_cast<ID3D11Resource*>(uintptr_t(f.exposure));
    const UINT w=f.renderWidth, h=f.renderHeight, gx=(w+7)/8, gy=(h+7)/8;
    const auto gb0Desc=texture_desc(gb0), depthDesc=texture_desc(depth);
    if(gb0Desc.Width<w || gb0Desc.Height<h || depthDesc.Width<w || depthDesc.Height<h) throw DxError(E_INVALIDARG, "HDRP's GBuffer is smaller than the render size");
    // GBuffer0: bytes as they are (sRGB-encoded) when a UNORM view is possible, re-encoded otherwise.
    D11Constants c{}; c.c0[0]=w; c.c0[1]=h;
    DXGI_FORMAT albedoView=DXGI_FORMAT_R8G8B8A8_UNORM;
    switch(gb0Desc.Format) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: break;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: albedoView=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; c.c0[2]|=1; break;
    default: albedoView=tgb::typed_read_format(gb0Desc.Format, tgb::ReadAs::Color); c.c0[2]|=1; break;
    }
    if(albedoView==DXGI_FORMAT_UNKNOWN) throw DxError(DXGI_ERROR_UNSUPPORTED, "Unsupported GBuffer0 format "+std::to_string(gb0Desc.Format));
    auto albedoSrv=view_as(gb0, albedoView);
    ComPtr<ID3D11ShaderResourceView> stencilSrv;
    const DXGI_FORMAT sv=stencil_view(depthDesc.Format);
    if(sv!=DXGI_FORMAT_UNKNOWN) {
        D3D11_SHADER_RESOURCE_VIEW_DESC d{}; d.Format=sv; d.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels=1;
        if(FAILED(st.d11->CreateShaderResourceView(depth, &d, &stencilSrv))) stencilSrv.Reset();
    }
    if(stencilSrv) c.c0[2]|=2;
    else log_throttled("HDRP's stencil can't be read (depth format "+std::to_string(depthDesc.Format)+"); every surface counts as deferred");
    set_constants(c);
    run_cs(st.csAlbedo.Get(), {albedoSrv.Get(), stencilSrv.Get()}, in.gb0.t.uav.Get(), gx, gy);
    c.c0[2]=0; set_constants(c);
    run_cs(st.csFloat4.Get(), {tgb::typed_srv(st.d11.Get(), nb, tgb::ReadAs::Color).Get()}, in.nrm.t.uav.Get(), gx, gy);
    if(gb2) run_cs(st.csFloat4.Get(), {tgb::typed_srv(st.d11.Get(), gb2, tgb::ReadAs::Color).Get()}, in.gb2.t.uav.Get(), gx, gy);
    if(gb3) run_cs(st.csFloat4.Get(), {tgb::typed_srv(st.d11.Get(), gb3, tgb::ReadAs::Color).Get()}, in.gb3.t.uav.Get(), gx, gy);
    run_cs(st.csDepth.Get(), {tgb::typed_srv(st.d11.Get(), depth, tgb::ReadAs::Depth).Get()}, in.depth.t.uav.Get(), gx, gy);
    ComPtr<ID3D11ShaderResourceView> motionSrv;
    if(motion) motionSrv=tgb::typed_srv(st.d11.Get(), motion, tgb::ReadAs::Motion);
    c.c0[2]=motionSrv ? 1u : 0u; set_constants(c);
    run_cs(st.csMotion.Get(), {motionSrv.Get()}, in.motion.t.uav.Get(), gx, gy);
    ComPtr<ID3D11ShaderResourceView> exposureSrv;
    if(exposure) { try { exposureSrv=tgb::typed_srv(st.d11.Get(), exposure, tgb::ReadAs::Motion); } catch(...) { exposureSrv.Reset(); } }
    c.c0[2]=exposureSrv ? 1u : 0u; c.c1[0]=1.f; set_constants(c);
    run_cs(st.csExposure.Get(), {exposureSrv.Get()}, in.exposure.t.uav.Get(), 1, 1);
}

void dispatch(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso, std::initializer_list<uint32_t> constants, Barriers& barriers) {
    uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid);
    size_t i=0; for(uint32_t v:constants) if(i<8) values[i++]=v;
    list->SetPipelineState(pso);
    list->SetComputeRoot32BitConstants(1, 8, values, 0);
    list->Dispatch((st.width+7)/8, (st.height+7)/8, 1);
    barriers.uav(nullptr); barriers.flush(list);
}
// AMD Ray Regeneration's guides at the render size, made when it first denoises.
void ensure_rrg_textures() {
    if(st.rrgDepth) return;
    auto* d=st.d12.Get(); const uint32_t w=st.width, h=st.height;
    st.rrgDepth=make_texture(d, st.heap, w, h, DXGI_FORMAT_R32_FLOAT, "Create Ray Regeneration depth");
    st.rrgMotion=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create Ray Regeneration motion");
    st.rrgNormals=make_texture(d, st.heap, w, h, DXGI_FORMAT_R10G10B10A2_UNORM, "Create Ray Regeneration normals");
    st.rrgDiffuse=make_texture(d, st.heap, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, "Create Ray Regeneration diffuse albedo");
    st.rrgSpecular=make_texture(d, st.heap, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, "Create Ray Regeneration specular albedo");
}

// A pass as a ray generation shader over the render size (Shader Execution Reordering), or as its compute
// pipeline where there is none.
void dispatch_pass(ID3D12GraphicsCommandList4* list, ID3D12StateObject* rays, uint32_t record, ID3D12PipelineState* pso, Barriers& barriers) {
    if(!rays) { dispatch(list, pso, {}, barriers); return; }
    uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid);
    list->SetPipelineState1(rays);
    list->SetComputeRoot32BitConstants(1, 8, values, 0);
    D3D12_DISPATCH_RAYS_DESC d{};
    d.RayGenerationShaderRecord.StartAddress=st.serTable.va()+uint64_t(record)*D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;
    d.RayGenerationShaderRecord.SizeInBytes=D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
    d.Width=st.width; d.Height=st.height; d.Depth=1;
    list->DispatchRays(&d);
    barriers.uav(nullptr); barriers.flush(list);
}

// Records and submits the D3D12 part of a frame into the input and output sets of this parity. Returns
// the consumer fence value it signals.
uint64_t trace(const TGPT_Frame& f, uint32_t parity) {
    const uint32_t index=st.listIndex++%kFrames;
    const auto waitStart=std::chrono::steady_clock::now();
    st.consumer.wait_cpu(st.retired[index]);
    const auto recordStart=std::chrono::steady_clock::now();
    st.phase[2]=std::chrono::duration<double, std::milli>(recordStart-waitStart).count();
    const uint64_t retire=st.consumer.value+1;
    st.release.collect(st.consumer.d12->GetCompletedValue());
    // The timestamps this slot resolved when it last ran.
    if(st.queried[index] && st.ticksPerMs>0) {
        const auto* t=static_cast<const uint64_t*>(st.queryReadback.mapped)+index*2;
        if(t[1]>t[0]) { const double ms=double(t[1]-t[0])/st.ticksPerMs; auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.status.gpuMs=float(ms); st.summaryGpu+=ms; }
    }
    auto* list=st.lists[index].Get();
    check(st.allocators[index]->Reset(), "Reset path tracing allocator");
    check(list->Reset(st.allocators[index].Get(), nullptr), "Reset path tracing list");
    // From here a failure leaves the scene's bookkeeping ahead of a list that never runs (see trace_event).
    st.recording=true;
    st.rings[index].reset();
    ID3D12DescriptorHeap* heaps[]={st.heap.get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(st.root.Get());
    list->EndQuery(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*2);

    // The scene origin follows the camera in 256 m steps, keeping positions small next to it.
    const double cam[3]={f.cameraPosition[0], f.cameraPosition[1], f.cameraPosition[2]};
    std::memcpy(st.prevOrigin, st.origin, sizeof(st.origin));
    if(!st.haveOrigin || std::fabs(cam[0]-st.origin[0])>512 || std::fabs(cam[1]-st.origin[1])>512 || std::fabs(cam[2]-st.origin[2])>512) {
        for(int i=0;i<3;++i) st.origin[i]=std::round(cam[i]/256.0)*256.0;
        if(!st.haveOrigin) std::memcpy(st.prevOrigin, st.origin, sizeof(st.origin));
        st.haveOrigin=true;
    }
    st.scene.ommAllowed=!(f.options&TGPT_OPTION_NO_MICROMAPS);
    // The frame constants' slot is taken and bound now and filled once the scene is recorded, so every root argument
    // is set before the first dispatch (the character unpack reads none of them).
    void* cpu=nullptr; D3D12_GPU_VIRTUAL_ADDRESS gpu=0;
    if(!st.rings[index].allocate(sizeof(FrameConstants), 256, &cpu, &gpu)) {
        // This frame's ring is full: the constants get a buffer of their own.
        Buffer upload=make_buffer(st.d12.Get(), sizeof(FrameConstants), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create frame constants");
        cpu=upload.mapped; gpu=upload.va();
        st.release.add(retire, upload.resource.Get());
    }
    list->SetComputeRootConstantBufferView(0, gpu);
    list->SetComputeRootShaderResourceView(2, st.scene.tlas ? st.scene.tlas : gpu);
    // Characters: this frame's skinned positions from the shared texture into each one's position buffer, for the
    // scene to rebuild their BLAS from.
    std::vector<TGPT_SkinnedInstance> characters;
    {
        auto& work=st.skinWork[parity];
        Shared& skin=st.skin[parity];
        if(!work.empty() && skin.t.d12 && st.skinUnpack) {
            Barriers sb;
            sb.transition_from(skin.t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            std::vector<std::pair<Buffer*, uint32_t>> targets(work.size(), {nullptr, kInvalid});
            for(size_t i=0;i<work.size();++i) {
                targets[i].first=st.scene.skinned_positions(work[i].instance.mesh, work[i].count, targets[i].second);
                if(targets[i].first) sb.transition(*targets[i].first, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            sb.flush(list);
            list->SetPipelineState(st.skinUnpack.Get());
            for(size_t i=0;i<work.size();++i) {
                if(!targets[i].first) continue;
                const uint32_t values[8]={skin.srv, targets[i].second, work[i].firstTexel, work[i].count, kInvalid, kInvalid, kInvalid, kInvalid};
                list->SetComputeRoot32BitConstants(1, 8, values, 0);
                list->Dispatch((work[i].count+63)/64, 1, 1);
                characters.push_back(work[i].instance);
            }
            sb.uav(nullptr);
            for(auto& t:targets) if(t.first) sb.transition(*t.first, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            sb.transition_from(skin.t.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
            sb.flush(list);
        }
    }
    st.scene.record(st.d12.Get(), list, st.heap, st.rings[index], st.release, retire, st.origin, cam, characters);

    auto& in=st.inputs[parity];
    auto& out=st.outputs[parity];
    const bool reset=st.resetNext || (f.flags&TGPT_FRAME_RESET);
    st.resetNext=false;
    const uint32_t cur=st.parity, prev=1-st.parity;
    // AMD Ray Regeneration denoises instead of the built-in denoiser where it runs. Its context follows the
    // render size; the GPU finishes the old one's frames before it is replaced.
    bool rrg=(f.options&TGPT_OPTION_RAY_REGENERATION) && !(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) &&
        !st.rrgFailed && rrg_available() && f.debugView<TGPT_VIEW_COUNT;
    if(rrg && st.rrg.needs_context(st.width, st.height)) {
        st.consumer.wait_cpu(st.consumer.value);
        std::string error;
        if(!st.rrg.ensure(st.d12.Get(), st.width, st.height, error)) {
            st.rrgFailed=true; rrg=false;
            log("AMD Ray Regeneration could not start ("+error+"); the built-in denoiser serves for the rest of the session");
        }
    }
    if(rrg) {
        try { ensure_rrg_textures(); }
        catch(const DxError& e) {
            st.rrgFailed=true; rrg=false;
            log(std::string("AMD Ray Regeneration's guides could not be created (")+e.what()+"); the built-in denoiser serves for the rest of the session");
        }
    }
    const bool denoise=!(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) && !rrg;
    const uint32_t atrousIterations=denoise ? 5 : 0; // steps 1 to 16: about 60 pixels each way
    FrameConstants k{};
    std::memcpy(k.invViewProj, f.invViewProj, sizeof(k.invViewProj)); std::memcpy(k.viewProj, f.viewProj, sizeof(k.viewProj));
    std::memcpy(k.prevViewProj, f.prevViewProj, sizeof(k.prevViewProj));
    for(int i=0;i<3;++i) {
        k.cameraPos[i]=float(cam[i]-st.origin[i]);
        k.prevCameraPos[i]=float(double(f.prevCameraPosition[i])-st.origin[i]);
        k.prevOriginDelta[i]=float(st.prevOrigin[i]-st.origin[i]);
        k.sunDir[i]=f.sunDirection[i];
        k.sunColor[i]=f.sunColor[i];
    }
    k.sunDir[3]=std::cos(std::clamp(f.sunDirection[3], 0.0001f, 0.2f)); k.sunColor[3]=f.sunColor[3];
    // Its history froze while Ray Reconstruction, Ray Regeneration or no denoiser served: it starts over.
    const bool resumeDenoiser=denoise && !st.denoisedLast;
    st.denoisedLast=denoise;
    k.size[0]=st.width; k.size[1]=st.height; k.size[2]=f.frameIndex; k.size[3]=(reset ? 1u : 0u)|(resumeDenoiser ? 2u : 0u);
    k.settings[0]=0; k.settings[1]=f.debugView<TGPT_VIEW_COUNT ? f.debugView : 0; k.settings[2]=std::clamp(f.bounces, 1u, 4u); k.settings[3]=std::min(f.spatialSamples, 8u);
    k.settings2[0]=std::clamp(f.temporalCap, 1u, 64u); k.settings2[1]=rrg ? f.options : (f.options&~TGPT_OPTION_RAY_REGENERATION); k.settings2[2]=st.scene.lightCount; k.settings2[3]=atrousIterations;
    k.prevOriginDelta[3]=std::clamp(f.indirectColor, 0.f, 4.f); // Indirect Color, applied where the bounced light is shaded
    k.cameraPos[3]=std::isfinite(f.reflectionBoost) ? std::clamp(f.reflectionBoost, 0.f, 1.f) : 0.f; // Reflection Boost, in the prepare pass
    k.params[0]=std::isfinite(f.clampExposure) && f.clampExposure>0.f ? f.clampExposure : 0.f; k.params[1]=f.maxRayDistance>1 ? f.maxRayDistance : 1000.f; k.params[2]=f.skyIntensity>0 ? f.skyIntensity : 1.f;
    k.params[3]=std::max(8.f, 30.f*float(st.width)/1920.f);
    k.inputs0[0]=in.gb0.srv; k.inputs0[1]=in.nrm.srv; k.inputs0[2]=in.gb2.srv; k.inputs0[3]=in.gb3.srv;
    k.inputs1[0]=in.depth.srv; k.inputs1[1]=in.motion.srv; k.inputs1[2]=in.exposure.srv; k.inputs1[3]=st.sky.srv;
    k.scene0[0]=st.scene.instancesSrv; k.scene0[1]=st.scene.meshesSrv; k.scene0[2]=st.scene.geometriesSrv; k.scene0[3]=st.scene.materialsSrv;
    k.scene1[0]=st.scene.materialIndicesSrv; k.scene1[1]=st.scene.lightsSrv; k.scene1[2]=st.atlas.srv; k.scene1[3]=out.a.uav;
    k.extra[0]=st.alphaAtlas.srv; k.extra[1]=k.extra[2]=k.extra[3]=kInvalid;
    k.surface[0]=st.surface[cur].uav; k.surface[1]=st.surface[prev].uav; k.surface[2]=st.normal[cur].uav; k.surface[3]=st.normal[prev].uav;
    k.material[0]=st.material.uav; k.material[1]=st.giUav[cur]; k.material[2]=st.giUav[prev]; k.material[3]=st.diUav[cur];
    k.signals[0]=st.diUav[prev]; k.signals[1]=st.diffuseRaw.uav; k.signals[2]=st.sunRaw.uav; k.signals[3]=st.specRaw.uav;
    for(int i=0;i<3;++i) k.gridMin[i]=st.scene.gridMin[i];
    k.gridMin[3]=float(1.0/kGridCell);
    k.grid[0]=st.scene.gridCellsSrv; k.grid[1]=st.scene.gridIndicesSrv;
    k.grid[2]=kGridDims[0]|(kGridDims[1]<<8)|(kGridDims[2]<<16); k.grid[3]=std::clamp(f.raysPerPixel, 1u, 8u);
    k.denoise0[0]=st.diffuseHist[cur].uav; k.denoise0[1]=st.diffuseHist[prev].uav; k.denoise0[2]=st.diffuseMoments[cur].uav; k.denoise0[3]=st.diffuseMoments[prev].uav;
    const uint32_t finalDiffuse=!denoise ? st.diffuseRaw.uav : (atrousIterations%2 ? st.tmpA.uav : (atrousIterations ? st.tmpB.uav : st.diffuseHist[cur].uav));
    // The direct light (the sun and the lamps): its own temporal accumulation and two a-trous iterations, ending in directOut.
    k.denoise1[0]=denoise ? st.directOut.uav : st.sunRaw.uav; k.denoise1[1]=kInvalid;
    // Reflections: temporal accumulation and four a-trous iterations (steps 1, 2, 4, 8), ending in D.
    k.denoise1[2]=denoise ? st.tmpD.uav : st.specRaw.uav; k.denoise1[3]=st.specHist[prev].uav;
    k.denoise2[0]=st.tmpA.uav; k.denoise2[1]=st.tmpB.uav; k.denoise2[2]=finalDiffuse; k.denoise2[3]=out.b.uav;
    std::memcpy(cpu, &k, sizeof(k));
    list->SetComputeRootConstantBufferView(0, gpu);
    list->SetComputeRootShaderResourceView(2, st.scene.tlas);

    // The shared textures are in COMMON between frames (D3D11 uses them in between) and return there.
    Barriers b;
    const std::initializer_list<Shared*> reads={&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure, &st.sky, &st.atlas, &st.alphaAtlas};
    const std::initializer_list<Shared*> writes={&out.a, &out.b};
    for(Shared* s:reads) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(Shared* s:writes) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    b.flush(list);
    // Opacity micromaps for meshes whose textures are in the atlas now; their instances use them next frame.
    st.scene.record_omm(st.d12.Get(), list, st.heap, st.release, retire, st.ommBake.Get());

    dispatch(list, st.prepare.Get(), {}, b);
    dispatch_pass(list, st.giSer.Get(), 0, st.gi.Get(), b);
    dispatch(list, st.giSpatial.Get(), {}, b);
    dispatch(list, st.di.Get(), {}, b);
    dispatch(list, st.diSpatial.Get(), {}, b);
    if(!(f.options&TGPT_OPTION_NO_SPECULAR)) dispatch_pass(list, st.specularSer.Get(), 1, st.specular.Get(), b);
    if(denoise) {
        dispatch(list, st.temporal.Get(), {st.diffuseRaw.uav, st.diffuseHist[cur].uav, st.diffuseHist[prev].uav, st.diffuseMoments[cur].uav,
                                           st.diffuseMoments[prev].uav, 32u, 1u, 0u}, b);
        // The second iteration feeds the first one's result back into this frame's history (SVGF).
        uint32_t input=st.diffuseHist[cur].uav;
        for(uint32_t i=0;i<atrousIterations;++i) {
            const uint32_t output=(i%2==0) ? st.tmpA.uav : st.tmpB.uav;
            dispatch(list, st.atrous.Get(), {input, output, 1u<<i, 0u, i==1 ? st.diffuseHist[cur].uav : kInvalid}, b);
            input=output;
        }
        // The direct light keeps its shadows: its own variance steers its filter, two short iterations (steps 1, 2).
        dispatch(list, st.temporal.Get(), {st.sunRaw.uav, st.directHist[cur].uav, st.directHist[prev].uav, st.directMoments[cur].uav,
                                           st.directMoments[prev].uav, 16u, 1u, 0u}, b);
        dispatch(list, st.atrous.Get(), {st.directHist[cur].uav, st.directTmp.uav, 1u, 0u, kInvalid}, b);
        dispatch(list, st.atrous.Get(), {st.directTmp.uav, st.directOut.uav, 2u, 0u, st.directHist[cur].uav}, b);
        dispatch(list, st.temporal.Get(), {st.specRaw.uav, st.specHist[cur].uav, st.specHist[prev].uav, st.specMoments[cur].uav,
                                           st.specMoments[prev].uav, 16u, 1u, 0u}, b);
        dispatch(list, st.atrous.Get(), {st.specHist[cur].uav, st.tmpC.uav, 1u, 1u, kInvalid}, b);
        dispatch(list, st.atrous.Get(), {st.tmpC.uav, st.tmpD.uav, 2u, 1u, st.specHist[cur].uav}, b);
        dispatch(list, st.atrous.Get(), {st.tmpD.uav, st.tmpC.uav, 4u, 1u, kInvalid}, b);
        dispatch(list, st.atrous.Get(), {st.tmpC.uav, st.tmpD.uav, 8u, 1u, kInvalid}, b);
    }
    st.rrgActive=false;
    if(rrg) {
        dispatch(list, st.rrgGuides.Get(), {st.rrgDepth.uav, st.rrgMotion.uav, st.rrgNormals.uav, st.rrgDiffuse.uav, st.rrgSpecular.uav}, b);
        RrgFrame r;
        r.width=st.width; r.height=st.height; r.frameIndex=f.frameIndex; r.reset=reset;
        r.linearDepth=st.rrgDepth.resource.Get(); r.motion=st.rrgMotion.resource.Get(); r.normals=st.rrgNormals.resource.Get();
        r.diffuseAlbedo=st.rrgDiffuse.resource.Get(); r.specularAlbedo=st.rrgSpecular.resource.Get();
        r.indirectDiffuse=st.diffuseRaw.resource.Get(); r.directDiffuse=st.sunRaw.resource.Get(); r.indirectSpecular=st.specRaw.resource.Get();
        // HDRP's matrices in Unity's memory order are the SDK's row-major, row-vector ones as they stand;
        // its jitter is negated as for AMD's upscaler.
        std::memcpy(r.view, f.view, sizeof(r.view)); std::memcpy(r.projection, f.nonJitteredProj, sizeof(r.projection));
        r.jitter[0]=-f.jitter[0]; r.jitter[1]=-f.jitter[1];
        for(int i=0;i<3;++i) r.cameraDelta[i]=f.prevCameraPosition[i]-f.cameraPosition[i];
        std::string error;
        st.rrgActive=st.rrg.dispatch(list, r, error);
        // AMD's backend binds its own descriptor heap and root signature: the tracer's go back.
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(st.root.Get());
        list->SetComputeRootConstantBufferView(0, gpu);
        list->SetComputeRootShaderResourceView(2, st.scene.tlas);
        b.uav(nullptr); b.flush(list);
        if(!st.rrgActive) {
            st.rrgFailed=true;
            log("AMD Ray Regeneration failed ("+error+"); the built-in denoiser serves from the next frame on");
        }
    }
    // The reflections Ray Regeneration's guides divided by their albedo are multiplied back in.
    dispatch(list, st.compose.Get(), {rrg ? 1u : 0u}, b);

    for(Shared* s:reads) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    for(Shared* s:writes) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    b.flush(list);
    list->EndQuery(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*2+1);
    list->ResolveQueryData(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*2, 2, st.queryReadback.resource.Get(), index*16);
    check(list->Close(), "Close path tracing list");
    ID3D12CommandList* lists[]={list};
    st.queue->ExecuteCommandLists(1, lists);
    st.recording=false;
    st.queried[index]=true;
    st.phase[3]=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-recordStart).count();
    st.parity=1-st.parity;
    st.retired[index]=retire;
    return retire;
}

void summarise(const TGPT_Frame& f, double cpuMs) {
    const uint64_t now=GetTickCount64();
    ++st.summaryFrames; st.summaryCpu+=cpuMs;
    for(size_t i=0;i<st.phase.size();++i) st.summaryPhase[i]+=st.phase[i];
    if(!st.lastSummary) { st.lastSummary=now; return; }
    if(now-st.lastSummary<10000) return;
    const auto s=st.scene.stats();
    TGPT_Status status{}; { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); status=e.status; }
    const double n=double(st.summaryFrames);
    auto ms=[](double v) { char b[32]; std::snprintf(b, sizeof(b), "%.2f", v); return std::string(b); };
    log("Path tracing "+std::to_string((now-st.lastSummary)/1000)+"s: frames="+std::to_string(st.summaryFrames)+
        " render="+std::to_string(st.width)+"x"+std::to_string(st.height)+" gpu="+ms(st.summaryGpu/n)+"ms cpu="+ms(st.summaryCpu/n)+"ms (d3d11 "+ms(st.summaryPhase[0]/n)+
        " flush "+ms(st.summaryPhase[1]/n)+" wait "+ms(st.summaryPhase[2]/n)+" record "+ms(st.summaryPhase[3]/n)+") composited="+std::to_string(st.composited)+
        " gpuWaits="+std::to_string(st.compositeWaits)+" skipped="+std::to_string(st.compositeSkipped)+
        " meshes ready/queued/failed="+std::to_string(s.meshesReady)+"/"+std::to_string(s.meshesQueued)+"/"+std::to_string(s.meshesFailed)+
        " instances traced/registered="+std::to_string(s.instancesTraced)+"/"+std::to_string(status.instances)+" triangles="+std::to_string(s.triangles)+
        " lights="+std::to_string(st.scene.lightCount)+" (grid: "+std::to_string(s.gridCellsLit)+" cells lit, "+
        (s.gridCellsLit ? std::to_string(s.gridEntries/s.gridCellsLit) : std::string("0"))+" lights a lit cell on average, at most "+
        std::to_string(s.gridMostInCell)+", "+std::to_string(s.gridBuilds)+" builds) rays/pixel="+std::to_string(std::clamp(f.raysPerPixel, 1u, 8u))+
        " textures="+std::to_string(status.textures)+" materials="+std::to_string(status.materials)+
        (st.useDxr12 ? " micromaps: meshes="+std::to_string(s.ommMeshes)+" waiting="+std::to_string(s.ommWaiting)+" triangles="+std::to_string(s.ommTriangles)+
            " instances="+std::to_string(s.ommInstances)+" "+std::to_string(s.ommBytes>>20)+"MB ser="+(st.giSer ? "on" : "off") : std::string(" sm=6.6"))+
        " characters="+std::to_string(s.charactersTraced)+" ("+std::to_string(s.characterTriangles/1000)+"k triangles)"+
        " memory="+std::to_string((s.geometryBytes+s.accelerationBytes)>>20)+"MB sky="+(st.skyValid ? "hdrp" : "plain"));
    st.lastSummary=now; st.summaryFrames=0; st.summaryGpu=0; st.summaryCpu=0; st.summaryPhase={};
    st.composited=st.compositeWaits=st.compositeSkipped=0;
}

void publish_status(const TGPT_Frame& f, double cpuMs, bool ok) {
    const auto s=st.scene.stats();
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    if(ok) ++e.status.framesTraced; else ++e.status.framesFailed;
    e.status.frameMs=float(cpuMs);
    e.status.d3d11Ms=float(st.phase[0]); e.status.flushMs=float(st.phase[1]); e.status.waitMs=float(st.phase[2]); e.status.recordMs=float(st.phase[3]);
    e.status.meshesReady=s.meshesReady; e.status.meshesFailed=s.meshesFailed; e.status.meshesQueued=s.meshesQueued;
    e.status.instancesTraced=s.instancesTraced; e.status.triangles=s.triangles; e.status.geometryBytes=s.geometryBytes;
    e.status.accelerationBytes=s.accelerationBytes; e.status.renderWidth=f.renderWidth; e.status.renderHeight=f.renderHeight;
    e.status.features=(st.useDxr12 ? TGPT_FEATURE_DXR12 : 0u)|(st.giSer ? TGPT_FEATURE_SER : 0u)|
        (rrg_available() && !st.rrgFailed ? TGPT_FEATURE_RAY_REGENERATION : 0u)|(st.rrgActive ? TGPT_FEATURE_RAY_REGENERATION_ACTIVE : 0u);
    e.status.micromapMeshes=s.ommMeshes; e.status.micromapInstances=s.ommInstances; e.status.micromapsWaiting=s.ommWaiting;
}

void fail(const std::string& message, HRESULT hr) {
    log_throttled("Path tracing frame failed: "+message+" "+tgb::hexhr(hr));
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    e.status.lastError=hr;
    if(++st.failures>=30 && !e.status.disabled) {
        e.status.disabled=1;
        log("Path tracing failed 30 frames in a row; it is off for the rest of this session");
    }
}

// What a frame's composite needs: the previous frame's result, and the input set this frame copied.
struct CompositeWork { Result previous; uint32_t parity=0; bool inputs=false; };
std::array<CompositeWork, 8> compositeWork;

void trace_event(uint32_t sequence) {
    TGPT_Frame frame{};
    ComPtr<ID3D11Buffer> skinSource; uint32_t skinStride=0; std::vector<TGPT_SkinnedInstance> skinned;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.frames[sequence%e.frames.size()];
        if(!slot.occupied || slot.sequence!=sequence) { log_throttled("Path tracing event for an unknown frame"); return; }
        frame=slot.frame;
        skinSource=slot.skinSource; skinStride=slot.skinStride; skinned=slot.skinned;
    }
    const auto started=std::chrono::steady_clock::now();
    auto& work=compositeWork[sequence%compositeWork.size()];
    work=CompositeWork{};
    // Only a result of the event right before this one: its outputs are the set this frame does not write.
    work.previous=(st.latest.valid && st.latest.frame==st.frame) ? st.latest : Result{};
    st.latest.valid=false;
    const uint32_t parity=uint32_t(st.frame+1)&1u;
    bool produced=false, traced=false; uint64_t retire=0;
    tgb::InternalScope internal(&host());
    try {
        if(st.broken) throw DxError(E_FAIL, "the tracer is off after a D3D12 error");
        if(!ensure_d3d11()) throw DxError(E_FAIL, "the game's D3D11 device has not been observed");
        if(!ensure_d3d12()) throw DxError(E_FAIL, "TG Graphics Bridge created no D3D12 device");
        // Descriptors released from here on may be read by every frame up to the one about to be traced.
        st.heap.set_retire(st.consumer.value+1);
        st.heap.collect(st.consumer.d12->GetCompletedValue());
        const bool cleared=st.scene.intake(&st.heap, st.release, st.consumer.value+1);
        auto lap=[last=std::chrono::steady_clock::now()](double& into) mutable {
            const auto now=std::chrono::steady_clock::now(); into=std::chrono::duration<double, std::milli>(now-last).count(); last=now; };
        {
            tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
            // A cleared scene hands its alpha tiles out again: opaque until the new textures are baked, not the old ones' alpha.
            if(cleared && st.alphaAtlas.t.uav) {
                wait_for_scene_readers();
                const float one[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.alphaAtlas.t.uav.Get(), one);
            }
            st.scene.pump_d3d11(st.d11.Get(), st.c11.Get(), st.frame);
            bake_textures();
            bake_alpha_textures();
            update_sky();
            ensure_frame_textures(frame.renderWidth, frame.renderHeight);
            // D3D12 must be done with the frame that last read this input set (two frames ago) before it is overwritten.
            const uint64_t busy=st.inputsRetire[parity];
            if(busy && st.consumer.d12->GetCompletedValue()<busy) check(st.c11->Wait(st.consumer.d11.Get(), busy), "D3D11 wait before reusing an input set");
            copy_inputs(frame, st.inputs[parity]);
            try { copy_skinned(skinSource, skinStride, skinned, parity); }
            catch(const std::exception& e) { st.skinWork[parity].clear(); log_throttled(std::string("Characters could not be copied: ")+e.what()); }
            work.inputs=true; work.parity=parity;
        }
        lap(st.phase[0]);
        const uint64_t producedValue=st.producer.next();
        check(st.c11->Signal(st.producer.d11.Get(), producedValue), "Signal path tracing inputs");
        st.c11->Flush();
        lap(st.phase[1]);
        check(st.queue->Wait(st.producer.d12.Get(), producedValue), "D3D12 wait for path tracing inputs");
        produced=true;
        retire=trace(frame, parity);
        const HRESULT signalled=st.queue->Signal(st.consumer.d12.Get(), retire);
        if(FAILED(signalled)) { st.broken=true; throw DxError(signalled, "D3D12 signal after path tracing"); }
        st.consumer.value=retire;
        st.inputsRetire[parity]=retire;
        traced=true;
        st.failures=0;
    } catch(const DxError& e) {
        fail(e.what(), e.hr);
    } catch(const std::exception& e) {
        fail(e.what(), E_FAIL);
    }
    if(st.recording) {
        // The scene marked meshes built and resources transitioned for a list that never runs: nothing it
        // says can be trusted any more, so path tracing stays off for the session.
        st.recording=false; st.broken=true;
        log("A path tracing frame failed while it was being recorded; path tracing stays off for the rest of this session");
    }
    if(produced && !traced && st.queue && st.consumer.ready()) {
        // The D3D12 side never ran this frame's work; its fence still moves on so nothing waits on it.
        const uint64_t v=st.consumer.value+1;
        if(SUCCEEDED(st.queue->Signal(st.consumer.d12.Get(), v))) { st.consumer.value=v; st.inputsRetire[parity]=v; }
    }
    ++st.frame;
    if(traced) {
        st.latest.valid=true; st.latest.frame=st.frame; st.latest.retire=retire; st.latest.parity=parity;
        st.latest.width=frame.renderWidth; st.latest.height=frame.renderHeight;
        for(int i=0;i<3;++i) st.latest.camera[i]=frame.cameraPosition[i];
    }
    const double ms=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-started).count();
    publish_status(frame, ms, traced);
    if(traced) summarise(frame, ms);
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    auto& slot=e.frames[sequence%e.frames.size()];
    if(slot.occupied && slot.sequence==sequence) { slot.traced=work.inputs; slot.retire=retire; }
}

// DLSS Ray Reconstruction's guides from this frame's GBuffer copies, into textures of exactly the render
// size that TGPT_GetGuides hands out. A size change replaces them; the old ones live on for eight frames,
// since the plugin may already have handed their pointers to TG Better Upscaling.
void write_guides(const TGPT_Frame& frame, const Inputs& in) {
    const uint32_t w=frame.renderWidth, h=frame.renderHeight;
    while(!st.retiredGuides.empty() && st.retiredGuides.front().first+8<=st.frame) st.retiredGuides.erase(st.retiredGuides.begin());
    if(!st.guides[0].texture || st.guideWidth!=w || st.guideHeight!=h) {
        for(auto& g:st.guides) if(g.texture) st.retiredGuides.emplace_back(st.frame, g.texture);
        const DXGI_FORMAT formats[3]={DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT};
        for(int i=0;i<3;++i) {
            D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=1; d.ArraySize=1; d.Format=formats[i]; d.SampleDesc.Count=1;
            d.Usage=D3D11_USAGE_DEFAULT; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
            st.guides[i]=State::Guide{};
            check(st.d11->CreateTexture2D(&d, nullptr, &st.guides[i].texture), "Create Ray Reconstruction guide");
            check(st.d11->CreateUnorderedAccessView(st.guides[i].texture.Get(), nullptr, &st.guides[i].uav), "Create Ray Reconstruction guide UAV");
        }
        st.guideWidth=w; st.guideHeight=h;
    }
    CompositeConstants c{};
    std::memcpy(c.invViewProj, frame.invViewProj, sizeof(c.invViewProj));
    c.frameSize[0]=w; c.frameSize[1]=h;
    // Reflection Boost: the guides describe the surfaces as the tracer shades them.
    c.cameraDelta[3]=std::isfinite(frame.reflectionBoost) ? std::clamp(frame.reflectionBoost, 0.f, 1.f) : 0.f;
    st.c11->UpdateSubresource(st.compositeConstants.Get(), 0, nullptr, &c, 0, 0);
    {
        tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
        ID3D11ShaderResourceView* srvs[4]={in.gb0.t.srv.Get(), in.nrm.t.srv.Get(), in.gb2.t.srv.Get(), in.depth.t.srv.Get()};
        ID3D11UnorderedAccessView* uavs[3]={st.guides[0].uav.Get(), st.guides[1].uav.Get(), st.guides[2].uav.Get()};
        ID3D11Buffer* cbs[2]={st.constants.Get(), st.compositeConstants.Get()};
        st.c11->CSSetShader(st.csGuides.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 2, cbs);
        st.c11->CSSetShaderResources(0, 4, srvs); st.c11->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
        st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
        ID3D11ShaderResourceView* noSrvs[4]{}; st.c11->CSSetShaderResources(0, 4, noSrvs);
        ID3D11UnorderedAccessView* noUavs[3]{}; st.c11->CSSetUnorderedAccessViews(0, 3, noUavs, nullptr);
    }
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    e.guides.width=w; e.guides.height=h; e.guides.frameIndex=frame.frameIndex;
    e.guides.diffuseAlbedo=uint64_t(uintptr_t(st.guides[0].texture.Get()));
    e.guides.specularAlbedo=uint64_t(uintptr_t(st.guides[1].texture.Get()));
    e.guides.normalRoughness=uint64_t(uintptr_t(st.guides[2].texture.Get()));
}

void composite_event(uint32_t sequence) {
    uint64_t color=0; TGPT_Frame frame{};
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.frames[sequence%e.frames.size()];
        if(!slot.occupied || slot.sequence!=sequence) return;
        color=slot.color; frame=slot.frame;
        slot.occupied=false;
    }
    const CompositeWork work=compositeWork[sequence%compositeWork.size()];
    // Ray Reconstruction's guides come first: they are needed whether or not there is light to apply yet.
    if((frame.flags&TGPT_FRAME_GUIDES) && work.inputs && st.d11 && st.csGuides && frame.renderWidth==st.width && frame.renderHeight==st.height) {
        tgb::InternalScope internal(&host());
        try { write_guides(frame, st.inputs[work.parity]); }
        catch(const DxError& e) { log_throttled(std::string("Ray Reconstruction guides failed: ")+e.what()+" "+tgb::hexhr(e.hr)); }
        catch(const std::exception& e) { log_throttled(std::string("Ray Reconstruction guides failed: ")+e.what()); }
    }
    const Result& prev=work.previous;
    if(!color || !work.inputs || !prev.valid || !st.d11 || prev.parity==work.parity ||
       prev.width!=frame.renderWidth || prev.height!=frame.renderHeight || frame.renderWidth!=st.width || frame.renderHeight!=st.height) {
        ++st.compositeSkipped; return;
    }
    tgb::InternalScope internal(&host());
    try {
        auto* target=reinterpret_cast<ID3D11Resource*>(uintptr_t(color));
        if(!on_game_device(target)) { log_throttled("HDRP's color buffer is not on the game's device; the traced light is not applied"); return; }
        const auto desc=texture_desc(target);
        const uint32_t w=frame.renderWidth, h=frame.renderHeight;
        if(desc.Width<w || desc.Height<h) { log_throttled("HDRP's color buffer is smaller than the render size"); return; }
        // The previous frame's work has had a frame to finish: a GPU wait only when it has not.
        if(st.consumer.d12->GetCompletedValue()<prev.retire) { check(st.c11->Wait(st.consumer.d11.Get(), prev.retire), "D3D11 wait for the traced light"); ++st.compositeWaits; }
        CompositeConstants c{};
        std::memcpy(c.invViewProj, frame.invViewProj, sizeof(c.invViewProj));
        for(int i=0;i<3;++i) c.cameraDelta[i]=float(double(frame.cameraPosition[i])-prev.camera[i]);
        // 1: Ray Reconstruction denoises, so the light is taken from the nearest texel (resampling would
        // correlate neighbouring pixels' noise).
        c.frameSize[0]=w; c.frameSize[1]=h; c.frameSize[3]=frame.debugView<TGPT_VIEW_COUNT ? frame.debugView : 0;
        c.frameSize[2]=(frame.options&TGPT_OPTION_RAY_RECONSTRUCTION) ? 1u : 0u;
        st.c11->UpdateSubresource(st.compositeConstants.Get(), 0, nullptr, &c, 0, 0);
        const auto& in=st.inputs[work.parity];
        const auto& out=st.outputs[prev.parity];
        // The exposures this frame's color and the previous frame's traced light were pre-exposed with.
        ID3D11ShaderResourceView* srvs[8]={in.gb0.t.srv.Get(), in.gb3.t.srv.Get(), in.depth.t.srv.Get(), in.motion.t.srv.Get(), out.a.t.srv.Get(), out.b.t.srv.Get(),
                                           in.exposure.t.srv.Get(), st.inputs[prev.parity].exposure.t.srv.Get()};
        ID3D11Buffer* cbs[2]={st.constants.Get(), st.compositeConstants.Get()};
        const auto uavFormat=tgb::typed_write_format(desc.Format);
        ++st.composited;
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); ++e.status.composited; }
        if((desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS) && uavFormat!=DXGI_FORMAT_UNKNOWN) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC v{}; v.Format=uavFormat; v.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11UnorderedAccessView> uav;
            if(SUCCEEDED(st.d11->CreateUnorderedAccessView(target, &v, &uav))) {
                tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
                st.c11->CSSetShader(st.csComposite.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 2, cbs);
                st.c11->CSSetShaderResources(0, 8, srvs);
                auto* u=uav.Get(); st.c11->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
                st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
                ID3D11ShaderResourceView* none[8]{}; st.c11->CSSetShaderResources(0, 8, none);
                ID3D11Buffer* noCbs[2]{}; st.c11->CSSetConstantBuffers(0, 2, noCbs);
                return;
            }
        }
        const auto rtvFormat=tgb::typed_read_format(desc.Format, tgb::ReadAs::Color);
        if(!(desc.BindFlags&D3D11_BIND_RENDER_TARGET) || rtvFormat==DXGI_FORMAT_UNKNOWN) { log_throttled("HDRP's color buffer can't be written"); return; }
        D3D11_RENDER_TARGET_VIEW_DESC v{}; v.Format=rtvFormat; v.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> rtv; check(st.d11->CreateRenderTargetView(target, &v, &rtv), "Create composite RTV");
        tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
        auto* rt=rtv.Get(); st.c11->OMSetRenderTargets(1, &rt, nullptr);
        st.c11->OMSetBlendState(st.compositeBlend.Get(), nullptr, UINT_MAX); st.c11->OMSetDepthStencilState(st.noDepth.Get(), 0);
        D3D11_VIEWPORT viewport{0, 0, float(w), float(h), 0, 1}; st.c11->RSSetViewports(1, &viewport); st.c11->RSSetState(st.raster.Get());
        st.c11->IASetInputLayout(nullptr); st.c11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        st.c11->VSSetShader(st.vsComposite.Get(), nullptr, 0); st.c11->PSSetShader(st.psComposite.Get(), nullptr, 0);
        st.c11->PSSetConstantBuffers(0, 2, cbs); st.c11->PSSetShaderResources(0, 8, srvs);
        st.c11->Draw(3, 0);
        ID3D11ShaderResourceView* none[8]{}; st.c11->PSSetShaderResources(0, 8, none);
        ID3D11Buffer* noCbs[2]{}; st.c11->PSSetConstantBuffers(0, 2, noCbs);
    } catch(const DxError& e) { log_throttled(std::string("Path tracing composite failed: ")+e.what()+" "+tgb::hexhr(e.hr)); }
      catch(const std::exception& e) { log_throttled(std::string("Path tracing composite failed: ")+e.what()); }
}
} // namespace

void device_ready(ID3D12Device* device, uint32_t vendor) {
    std::string reason;
    ComPtr<ID3D12Device5> d5;
    if(!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&d5)))) reason="The Direct3D 12 runtime is too old for ray tracing (no ID3D12Device5).";
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 o5{};
    if(reason.empty() && (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &o5, sizeof(o5))) || o5.RaytracingTier<D3D12_RAYTRACING_TIER_1_1))
        reason="This GPU or driver has no DirectX Raytracing 1.1 (inline ray tracing); path tracing needs an RTX 20, RX 6000, Arc or newer GPU.";
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
    if(reason.empty() && (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))) || sm.HighestShaderModel<D3D_SHADER_MODEL_6_6))
        reason="The GPU driver does not support Shader Model 6.6; updating the driver may help.";
    D3D12_FEATURE_DATA_D3D12_OPTIONS o{};
    if(reason.empty() && (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &o, sizeof(o))) || o.ResourceBindingTier<D3D12_RESOURCE_BINDING_TIER_3))
        reason="The GPU does not support resource binding tier 3.";
    // DirectX Raytracing 1.2: Shader Model 6.9 and tier 1.2 bring Shader Execution Reordering and opacity micromaps.
    D3D12_FEATURE_DATA_SHADER_MODEL sm69{D3D_SHADER_MODEL_6_9};
    const bool dxr12=reason.empty() && o5.RaytracingTier>=D3D12_RAYTRACING_TIER_1_2 &&
        SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm69, sizeof(sm69))) && sm69.HighestShaderModel>=D3D_SHADER_MODEL_6_9;
    st.dxr12=dxr12;
    // D3D12_FEATURE_D3D12_OPTIONS22 (Agility SDK 1.619, newer than this Windows SDK's header).
    struct Options22 { BOOL serActuallyReorders, byteOffsetViews; UINT max1DDispatch, max1DDispatchMesh; } o22{};
    st.serReorders=dxr12 && SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE(65), &o22, sizeof(o22))) && o22.serActuallyReorders;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.sharedDevice=device; e.vendor=vendor; e.unsupported=reason;
        e.status.deviceReady=1; e.status.supported=reason.empty() ? 1u : 0u;
    }
    st.sharedDevice=device;
    if(reason.empty()) rrg_probe(device, vendor);
    if(reason.empty()) {
        char model[8]; std::snprintf(model, sizeof(model), "%X", unsigned(sm.HighestShaderModel));
        log("Shared D3D12 device: DirectX Raytracing tier "+std::to_string(int(o5.RaytracingTier))+", shader model 0x"+model+"; path tracing available; "+
            (dxr12 ? std::string("DirectX Raytracing 1.2 (opacity micromaps) available; Shader Execution Reordering ")+(st.serReorders ? "reorders" : "does not reorder on this GPU")
                   : std::string("no DirectX Raytracing 1.2 (Shader Model 6.9 and tier 1.2)")));
    } else log("Path tracing is unavailable: "+reason);
}

void __stdcall render_event(int eventId, void* data) {
    try {
        const uint32_t sequence=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
        if(eventId==TGPT_EVENT_TRACE) trace_event(sequence);
        else if(eventId==TGPT_EVENT_COMPOSITE) composite_event(sequence);
    } catch(...) { log_throttled("Path tracing event failed with an exception; the frame keeps HDRP's lighting"); }
}
} // namespace tgpt
