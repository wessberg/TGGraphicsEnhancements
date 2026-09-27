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
//   Render event 4 of frame N (right after HDRP's transparent SSR step, which water takes its reflections from)
//     5. D3D11 fills HDRP's reflections for water with frame N-1's traced ones, and hands frame N's water
//        surfaces to D3D12, which traces their reflections after frame N's own work, for frame N+1.
//
// Each fence is signalled by one API only, so its value never goes backwards. Inputs and outputs are
// double-buffered by frame parity; before D3D11 overwrites a set, D3D12 has finished the frame that read it.
#include "scene.hpp"
#include "ray_regeneration.hpp"
#include "nrd_denoiser.hpp"
#include "d3d11_shader.hpp"
#include "pt_shaders.hpp"
// NVIDIA RTXDI (fetched beside the repository, compiled in): its ReSTIR DI and GI parameter blocks and helpers.
#include <Rtxdi/DI/ReSTIRDIParameters.h>
#include <Rtxdi/GI/ReSTIRGIParameters.h>
#include <Rtxdi/RtxdiUtils.h>
#include <d3dcompiler.h>
#include <chrono>
#include <map>
#include <cmath>
#include <cstring>

namespace tgpt {
namespace {
constexpr uint32_t kFrames=3;
// What each stage of a frame costs the GPU (the F8 report, TGPT_GetStageTimes, and the log): a timestamp at the frame's start
// and after each stage, and a pair around the water's reflections (a submission of their own), after the frames' marks.
// The bounced light's paths (with their temporal reuse, pt_gi) and its spatial reuse (with the boiling filter) apart: the
// spatial reuse's visibility rays grow with Spatial Samples, the paths with Rays Per Pixel and Bounces.
constexpr uint32_t kStages=10;
constexpr const char* kStageNames[kStages]={"scene", "prepare", "bounced light", "bounced light spatial reuse", "direct light",
                                            "hair and eyes", "reflections", "radiance cache", "denoising", "composite"};
constexpr uint32_t kMarks=kStages+1, kWaterQueries=kFrames*kMarks, kQueries=kWaterQueries+kFrames*2;
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
    uint32_t extra[4];      // the alpha atlas (SRV), the previous frame's material (UAV), RTXDI's neighbour offsets (SRV)
    RTXDI_RuntimeParameters rtxdiRuntime;
    RTXDI_ReservoirBufferParameters rtxdiReservoirs;
    RTXDI_DITemporalResamplingParameters diTemporal;
    RTXDI_BoilingFilterParameters diBoiling;
    RTXDI_DISpatialResamplingParameters diSpatial;
    uint32_t rtxdi[4];      // reservoir arrays: DI this frame, DI previous, GI this frame, GI previous
    RTXDI_GITemporalResamplingParameters giTemporal;
    RTXDI_GISpatialResamplingParameters giSpatial;
    RTXDI_BoilingFilterParameters giBoiling;
    float radiance[4];      // the reference radiance (stored light), the previous frame's, the clamps' reference, 1 with swaying instances
    float sssProfiles[64];  // per diffusion profile: Burley's shape (rgb, per metre), filter radius in metres (TGPT_SetDiffusionProfiles)
    uint32_t cache[4];      // the radiance cache: its keys, sums and light (raw UAVs); 1 while it is on
    float cacheOrigin[4];   // the scene origin in the world, in metres (the cache's cells are the world's)
    uint32_t emissive[4];   // emissive triangles (scene.cpp build_emitters): the emitter records (SRV), the previous frame's, their counts
    uint32_t emissive2[4];  // the instance table (SRV), the static instances it covers, the clusters sampled as lights, the first one's index
    float transmission[128]; // per diffusion profile: its transmission tint / 4, remap start, shape (per mm), remap span (TGPT_SetSkinTransmission)
    float fog[4];           // HDRP's height fog (TGPT_Frame.fog): extinction per metre, base height (world), 1 / scale height; the player's exposure factor
};
static_assert(sizeof(FrameConstants)==1632, "frame constants layout");
// The radiance cache's cells (pt_common.hlsli CACHE_SLOTS): a key, a frame's sums (16 bytes) and the light (16 bytes) each.
constexpr uint32_t kCacheSlots=1u<<20;
// RTXDI's spatial reuse walks a low-discrepancy set of offsets in the unit disc (rtxdi::FillNeighborOffsetBuffer).
constexpr uint32_t kNeighborOffsets=8192;

struct Shared { tgb::SharedTexture t; uint32_t srv=kInvalid, uav=kInvalid; };
struct D11Constants { uint32_t c0[4]; float c1[4]; };
// Kept in step with the Composite cbuffer in d3d11.hlsl.
struct CompositeConstants { float invViewProj[16]; float cameraDelta[4]; uint32_t frameSize[4]; uint32_t extras[4]; };
static_assert(sizeof(CompositeConstants)==112, "composite constants layout");
// Kept in step with the Water cbuffer in d3d11.hlsl (TGPT_WATER).
struct WaterConstants { float invViewProj[16], tracedViewProj[16], tracedInvViewProj[16]; float cameraDelta[4]; uint32_t frameSize[4]; };
static_assert(sizeof(WaterConstants)==224, "water constants layout");
// Each water command list's copy of its frame's constants, in a slot of this size.
constexpr uint64_t kWaterConstantsStride=4096;
static_assert(sizeof(FrameConstants)<=kWaterConstantsStride, "the water pass's copy of the frame constants");
// HDRP's stencil bit for surfaces its SSR gives reflections (StencilUsage.TraceReflectionRay): its water sets it, and
// transparent surfaces that take them.
constexpr uint32_t kTraceReflectionRay=8;

// One frame's HDRP textures as D3D12 reads them, and its traced light as the next frame's composite reads it.
struct Inputs { Shared gb0, nrm, gb2, gb3, depth, motion, exposure; };
struct Outputs { Shared a, b, c; }; // c: the traced reflections' hit distance, for Ray Reconstruction
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
    ComPtr<ID3D11ComputeShader> csCookieCube, csCookie2D; // lights' cookies into their tiles
    // DLSS Ray Reconstruction's guides (D3D11, the render size), and replaced ones kept alive a while.
    struct Guide { ComPtr<ID3D11Texture2D> texture; ComPtr<ID3D11UnorderedAccessView> uav; };
    std::array<Guide, 5> guides; uint32_t guideWidth=0, guideHeight=0; // 3, 4: written by the composite
    std::vector<std::pair<uint64_t, ComPtr<ID3D11Texture2D>>> retiredGuides;
    ComPtr<ID3D11VertexShader> vsComposite; ComPtr<ID3D11PixelShader> psComposite;
    // Shadow Detail (TGPT_SetShadowDetail): in place through a UAV, or drawn over the image from a copy of it.
    ComPtr<ID3D11ComputeShader> csShadowDetail; ComPtr<ID3D11VertexShader> vsShadowDetail; ComPtr<ID3D11PixelShader> psShadowDetail;
    ComPtr<ID3D11Texture2D> shadowCopy; ComPtr<ID3D11ShaderResourceView> shadowCopySrv; D3D11_TEXTURE2D_DESC shadowCopyDesc{};
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
    ComPtr<ID3D12PipelineState> meter; // the traced light's histogram for its gain (pt_meter.hlsl)
    ComPtr<ID3D12PipelineState> prepare, probe, gi, giBoil, giSpatial, di, diSpatial, specular, temporal, atrous, compose, ommBake, rrgGuides, skinUnpack, sss, cache;
    ComPtr<ID3D12PipelineState> emissive; // the emitter records (pt_emissive.hlsl)
    ComPtr<ID3D12PipelineState> hdrpShadow; // traced shadows on what HDRP shades itself (pt_hdrp_shadow.hlsl)
    // The radiance cache (pt_common.hlsli): its keys, a frame's sums and its light, for kCacheSlots cells; whether it ran
    // last frame (turned back on, it starts empty), and the scene it holds the light of (a clear's generation).
    Buffer cacheKeys, cacheSums, cacheLight; uint32_t cacheKeysUav=kInvalid, cacheSumsUav=kInvalid, cacheLightUav=kInvalid;
    bool cacheRan=false; uint32_t cacheScene=0;
    // DirectX Raytracing 1.2: the Shader Model 6.9 passes (their ray queries accept opacity micromaps),
    // and the GI and reflection passes as ray generation shaders that reorder their threads (SER).
    bool useDxr12=false;
    ComPtr<ID3D12StateObject> giSer, specularSer; Buffer serTable;
    ComPtr<ID3D12QueryHeap> queries; Buffer queryReadback; double ticksPerMs=0;
    // The light at the camera (pt_probe.hlsl): its two sums, and a read-back slot per frame in flight.
    Buffer probeBuffer, probeReadback; uint32_t probeUav=kInvalid;
    // The traced light's histogram (pt_meter.hlsl, 64 bins) and a read-back slot per frame in flight.
    Buffer meterBuffer, meterReadback; uint32_t meterUav=kInvalid;
    std::array<bool, kFrames> queried{}, waterQueried{};
    DeferredRelease release;
    // Shared with D3D11.
    Shared atlas, alphaAtlas, sky, cookieAtlas;
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
    std::array<Texture, 2> surface, normal, material;
    // RTXDI's DI and GI reservoirs: a buffer each, two arrays (this frame's and the previous frame's) in its block-linear layout.
    Buffer diRes, giRes; uint32_t diUav=kInvalid, giUav=kInvalid, rtxdiRowPitch=0, rtxdiArrayPitch=0;
    Buffer neighborOffsets; uint32_t neighborOffsetsSrv=kInvalid;
    Texture diffuseRaw, sunRaw, specRaw, tmpA, tmpB, tmpC, tmpD, directTmp, directOut;
    std::array<Texture, 2> diffuseHist, diffuseMoments, directHist, directMoments, specHist, specMoments;
    // What HDRP shades itself: this frame's shares of its light the traced shadows leave, and their history (whether it ran
    // last frame: turned back on, it starts over).
    Texture hdrpShares; std::array<Texture, 2> hdrpHistory; bool hdrpRan=false;
    // AMD FSR Ray Regeneration (Radeon RX 9000): its guides at the render size and its context. A failure
    // leaves the built-in denoiser for the rest of the session.
    Texture rrgDepth, rrgMotion, rrgNormals, rrgDiffuse, rrgSpecular;
    RayRegeneration rrg;
    bool rrgFailed=false, rrgActive=false;
    // NVIDIA NRD (RELAX): its inputs and outputs at the render size (made when it first denoises), its texture pools,
    // and the last frame's camera for its previous-frame matrices. A failure leaves the built-in denoiser for the rest
    // of the session.
    Texture nrdViewZ, nrdMotion, nrdNormalRoughness, nrdDiffuseIn, nrdDiffuseOut;
    Nrd nrd;
    bool nrdFailed=false, nrdActive=false, nrdHavePrevious=false;
    double nrdScale=0; // the reference NRD's radiance is divided by: kept until its history starts over
    float nrdView[16]{}, nrdProjection[16]{}, nrdJitter[2]{}; double nrdCamera[3]{};
    bool denoisedLast=false; // the built-in denoiser ran last frame (its history is current)
    bool ownDenoisedLast=false; // the direct light's and the reflections' own filters ran last frame (built-in denoiser or NRD)
    uint32_t parity=0;
    GpuScene scene;
    Result latest;
    // Bookkeeping.
    bool broken=false;
    uint64_t frame=0;
    double origin[3]{}, prevOrigin[3]{};
    bool haveOrigin=false, resetNext=true;
    // The last traced frame's index (a gap means frames went untraced: their history no longer fits) and the reference
    // radiance its light was stored against.
    bool haveFrameIndex=false; uint32_t lastFrameIndex=0; double reference=0, clampReference=0;
    uint32_t skyVersion=0; uint64_t skyFrame=0; bool skyValid=false;
    uint64_t lastSummary=0, summaryFrames=0; double summaryGpu=0, summaryCpu=0;
    // The stages' GPU time (kStageNames): summed for the log's summary, and over about a second for TGPT_GetStageTimes.
    std::array<double, kStages> summaryStage{}, recentStage{}; double summaryWater=0, recentWater=0;
    uint32_t summaryStageFrames=0, summaryWaterFrames=0, recentFrames=0, recentWaterFrames=0; uint64_t recentStart=0;
    std::array<double, 4> phase{}, summaryPhase{}; // event 1: D3D11 work, flush, waiting for a free list, recording
    uint32_t failures=0;
    uint64_t composited=0, compositeWaits=0, compositeSkipped=0;
    // Traced reflections on water (render event 4, pt_water.hlsl): HDRP's depth where it takes reflections and its normal
    // buffer at its transparent SSR step, shared by water frame parity; the traced reflections (shared: the next frame's
    // D3D11 fills HDRP's reflections from them) and their history; a copy of HDRP's reflections for the D3D11 pass that
    // writes them; the pass's own command lists, each with a slot for its frame's constants.
    struct WaterInputs { Shared depth, nrm; };
    std::array<WaterInputs, 2> waterIn;
    std::array<Shared, 2> waterOut;
    std::array<Texture, 2> waterHist;
    std::array<uint64_t, 2> waterInRetire{}; // the consumer value after which D3D12 no longer reads each water input set
    ComPtr<ID3D11ComputeShader> csWaterDepth, csWater;
    ComPtr<ID3D11Buffer> waterConstants;
    ComPtr<ID3D12PipelineState> water;
    std::array<ComPtr<ID3D12CommandAllocator>, kFrames> waterAllocators;
    std::array<ComPtr<ID3D12GraphicsCommandList4>, kFrames> waterLists;
    std::array<uint64_t, kFrames> waterRetired{};
    Buffer waterFrameConstants;
    uint32_t waterListIndex=0, waterParity=0;
    bool waterLogged=false;
    // The frame event 1 traced last, for its water event: the frame, its constants and its input set.
    struct WaterBase { bool valid=false; uint32_t sequence=0, parity=0; TGPT_Frame frame{}; FrameConstants k{}; };
    WaterBase waterBase;
    // The reflections the last water event traced, for the next one to apply: where, when, and the camera they were seen by.
    struct WaterResult { bool valid=false; uint64_t retire=0; uint32_t parity=0, width=0, height=0, frameIndex=0; double camera[3]{}; float viewProj[16]{}, invViewProj[16]{}; };
    WaterResult waterLatest;
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
    st.csCookieCube.Reset(); st.csCookie2D.Reset();
    st.csSkyCube.Reset(); st.csSkyArray.Reset(); st.csAtlas.Reset(); st.csAlpha.Reset(); st.csComposite.Reset(); st.vsComposite.Reset(); st.psComposite.Reset();
    st.csShadowDetail.Reset(); st.vsShadowDetail.Reset(); st.psShadowDetail.Reset(); st.shadowCopy.Reset(); st.shadowCopySrv.Reset(); st.shadowCopyDesc={};
    st.csGuides.Reset(); st.guides={}; st.guideWidth=st.guideHeight=0; st.retiredGuides.clear();
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.guides=TGPT_Guides{sizeof(TGPT_Guides), TGPT_ABI}; }
    st.compositeBlend.Reset(); st.raster.Reset(); st.noDepth.Reset(); st.constants.Reset(); st.compositeConstants.Reset();
    st.csWaterDepth.Reset(); st.csWater.Reset(); st.waterConstants.Reset();
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
    st.atlas=Shared{}; st.alphaAtlas=Shared{}; st.sky=Shared{}; st.cookieAtlas=Shared{};
    st.skin={}; for(auto& w:st.skinWork) w.clear();
    st.skinCs.clear(); st.skinSourceBuffer.Reset(); st.skinSourceSrv.Reset(); st.skinSourceKey=0;
    for(auto& in:st.inputs) in=Inputs{};
    for(auto& out:st.outputs) out=Outputs{};
    for(auto& in:st.waterIn) in=State::WaterInputs{};
    st.waterOut={}; st.waterHist={}; st.waterInRetire={}; st.waterBase=State::WaterBase{}; st.waterLatest=State::WaterResult{};
    st.waterAllocators={}; st.waterLists={}; st.waterRetired.fill(0); st.waterFrameConstants=Buffer{}; st.water.Reset();
    st.scene.clear(nullptr, st.release, st.consumer.value);
    st.release.clear();
    st.surface={}; st.normal={}; st.material={}; st.giRes={}; st.diRes={}; st.neighborOffsets={}; st.neighborOffsetsSrv=kInvalid;
    st.diffuseRaw={}; st.sunRaw={}; st.specRaw={}; st.tmpA={}; st.tmpB={}; st.tmpC={}; st.tmpD={}; st.directTmp={}; st.directOut={};
    st.diffuseHist={}; st.diffuseMoments={}; st.directHist={}; st.directMoments={}; st.specHist={}; st.specMoments={};
    st.width=st.height=0; st.inputsRetire={}; st.latest=Result{};
    st.producer.reset(); st.consumer.reset();
    st.heap.reset();
    st.allocators={}; st.lists={}; st.rings={}; st.retired.fill(0);
    st.queue.Reset(); st.root.Reset();
    st.meter.Reset(); st.meterReadback=Buffer{}; st.meterBuffer=Buffer{}; st.meterUav=kInvalid;
    st.prepare.Reset(); st.probe.Reset(); st.probeReadback=Buffer{}; st.probeBuffer=Buffer{}; st.probeUav=kInvalid; st.gi.Reset(); st.giBoil.Reset(); st.giSpatial.Reset(); st.di.Reset(); st.diSpatial.Reset(); st.specular.Reset();
    st.temporal.Reset(); st.atrous.Reset(); st.compose.Reset(); st.sss.Reset(); st.emissive.Reset(); st.queries.Reset(); st.queryReadback=Buffer{};
    st.cache.Reset(); st.cacheKeys=Buffer{}; st.cacheSums=Buffer{}; st.cacheLight=Buffer{};
    st.hdrpShadow.Reset(); st.hdrpShares={}; st.hdrpHistory={}; st.hdrpRan=false;
    st.cacheKeysUav=st.cacheSumsUav=st.cacheLightUav=kInvalid; st.cacheRan=false;
    st.ommBake.Reset(); st.skinUnpack.Reset(); st.giSer.Reset(); st.specularSer.Reset(); st.serTable=Buffer{}; st.useDxr12=false; st.scene.ommEnabled=false;
    st.rrg.destroy(); st.rrgGuides.Reset(); st.rrgActive=false;
    st.rrgDepth={}; st.rrgMotion={}; st.rrgNormals={}; st.rrgDiffuse={}; st.rrgSpecular={};
    st.nrd.destroy(); st.nrdActive=false; st.nrdHavePrevious=false;
    st.nrdViewZ={}; st.nrdMotion={}; st.nrdNormalRoughness={}; st.nrdDiffuseIn={}; st.nrdDiffuseOut={};
    st.skyValid=false; st.haveOrigin=false; st.resetNext=true; st.haveFrameIndex=false; st.reference=0; st.clampReference=0;
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
        st.csCookieCube=make_cs("TGPT_COOKIE_CUBE"); st.csCookie2D=make_cs("TGPT_COOKIE_2D");
        st.csGuides=make_cs("TGPT_GUIDES");
        st.csWaterDepth=make_cs("TGPT_COPY_WATER_DEPTH"); st.csWater=make_cs("TGPT_WATER");
        auto vs=compile_d3d11("TGPT_COMPOSITE_RASTER", "vsMain", "vs_5_0"), ps=compile_d3d11("TGPT_COMPOSITE_RASTER", "psMain", "ps_5_0");
        check(st.d11->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &st.vsComposite), "Create composite VS");
        check(st.d11->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &st.psComposite), "Create composite PS");
        st.csShadowDetail=make_cs("TGPT_SHADOW_DETAIL");
        auto svs=compile_d3d11("TGPT_SHADOW_DETAIL_RASTER", "vsMain", "vs_5_0"), sps=compile_d3d11("TGPT_SHADOW_DETAIL_RASTER", "psMain", "ps_5_0");
        check(st.d11->CreateVertexShader(svs->GetBufferPointer(), svs->GetBufferSize(), nullptr, &st.vsShadowDetail), "Create Shadow Detail VS");
        check(st.d11->CreatePixelShader(sps->GetBufferPointer(), sps->GetBufferSize(), nullptr, &st.psShadowDetail), "Create Shadow Detail PS");
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
        cb.ByteWidth=sizeof(WaterConstants);
        check(st.d11->CreateBuffer(&cb, nullptr, &st.waterConstants), "Create water constants");
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
            st.probe=make_pso(shaders::probeDxr12, sizeof(shaders::probeDxr12));
            st.gi=make_pso(shaders::giDxr12, sizeof(shaders::giDxr12));
            st.giSpatial=make_pso(shaders::giSpatialDxr12, sizeof(shaders::giSpatialDxr12));
            st.di=make_pso(shaders::diDxr12, sizeof(shaders::diDxr12));
            st.diSpatial=make_pso(shaders::diSpatialDxr12, sizeof(shaders::diSpatialDxr12));
            st.specular=make_pso(shaders::specularDxr12, sizeof(shaders::specularDxr12));
            st.temporal=make_pso(shaders::temporalDxr12, sizeof(shaders::temporalDxr12));
            st.atrous=make_pso(shaders::atrousDxr12, sizeof(shaders::atrousDxr12));
            st.compose=make_pso(shaders::composeDxr12, sizeof(shaders::composeDxr12));
            st.water=make_pso(shaders::waterDxr12, sizeof(shaders::waterDxr12));
            st.hdrpShadow=make_pso(shaders::hdrpShadowDxr12, sizeof(shaders::hdrpShadowDxr12));
            st.useDxr12=true;
        } catch(const DxError& e) { log(std::string("The Shader Model 6.9 passes could not be created (")+e.what()+" "+tgb::hexhr(e.hr)+"); the 6.6 passes run without opacity micromaps"); }
    }
    if(!st.useDxr12) {
        st.prepare=make_pso(shaders::prepare, sizeof(shaders::prepare));
        st.probe=make_pso(shaders::probe, sizeof(shaders::probe));
        st.gi=make_pso(shaders::gi, sizeof(shaders::gi));
        st.giSpatial=make_pso(shaders::giSpatial, sizeof(shaders::giSpatial));
        st.di=make_pso(shaders::di, sizeof(shaders::di));
        st.diSpatial=make_pso(shaders::diSpatial, sizeof(shaders::diSpatial));
        st.specular=make_pso(shaders::specular, sizeof(shaders::specular));
        st.temporal=make_pso(shaders::temporal, sizeof(shaders::temporal));
        st.atrous=make_pso(shaders::atrous, sizeof(shaders::atrous));
        st.compose=make_pso(shaders::compose, sizeof(shaders::compose));
        st.water=make_pso(shaders::water, sizeof(shaders::water));
        st.hdrpShadow=make_pso(shaders::hdrpShadow, sizeof(shaders::hdrpShadow));
    }
    st.ommBake=make_pso(shaders::ommBake, sizeof(shaders::ommBake));
    st.rrgGuides=make_pso(shaders::rrgGuides, sizeof(shaders::rrgGuides));
    st.giBoil=make_pso(shaders::giBoil, sizeof(shaders::giBoil));
    st.meter=make_pso(shaders::meter, sizeof(shaders::meter));
    st.sss=make_pso(shaders::sss, sizeof(shaders::sss));
    st.cache=make_pso(shaders::cache, sizeof(shaders::cache));
    st.skinUnpack=make_pso(shaders::skinUnpack, sizeof(shaders::skinUnpack));
    st.emissive=make_pso(shaders::emissive, sizeof(shaders::emissive));
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
            check(st.d12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(&st.waterAllocators[i])), "Create water reflections allocator");
            ComPtr<ID3D12GraphicsCommandList> waterList;
            check(st.d12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, st.waterAllocators[i].Get(), nullptr, IID_PPV_ARGS(&waterList)), "Create water reflections list");
            check(waterList.As(&st.waterLists[i]), "The command list has no ID3D12GraphicsCommandList4");
            check(st.waterLists[i]->Close(), "Close initial water reflections list");
        }
        st.retired.fill(0); st.waterRetired.fill(0);
        st.waterFrameConstants=make_buffer(st.d12.Get(), kFrames*kWaterConstantsStride, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE,
                                           D3D12_RESOURCE_STATE_GENERIC_READ, "Create water reflections constants");
        st.heap.create(st.d12.Get(), kHeapSize);
        check(st.d12->CreateRootSignature(0, shaders::prepare, sizeof(shaders::prepare), IID_PPV_ARGS(&st.root)), "Create path tracing root signature");
        create_pipelines();
        D3D12_QUERY_HEAP_DESC qh{}; qh.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count=kQueries;
        check(st.d12->CreateQueryHeap(&qh, IID_PPV_ARGS(&st.queries)), "Create path tracing timestamps");
        st.queryReadback=make_buffer(st.d12.Get(), kQueries*8, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create timestamp read-back");
        st.probeBuffer=make_buffer(st.d12.Get(), 48, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create camera light probe");
        st.probeUav=structured_uav(st.d12.Get(), st.heap, st.probeBuffer.resource.Get(), 3, 16);
        st.probeReadback=make_buffer(st.d12.Get(), kFrames*48, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create camera light read-back");
        st.meterBuffer=make_buffer(st.d12.Get(), 256, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create traced light meter");
        st.meterUav=structured_uav(st.d12.Get(), st.heap, st.meterBuffer.resource.Get(), 64, 4);
        st.meterReadback=make_buffer(st.d12.Get(), kFrames*256, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "Create traced light meter read-back");
        // The radiance cache: 36 MB for its million cells, emptied before its first frame.
        st.cacheKeys=make_buffer(st.d12.Get(), uint64_t(kCacheSlots)*4, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create radiance cache keys");
        st.cacheSums=make_buffer(st.d12.Get(), uint64_t(kCacheSlots)*16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create radiance cache sums");
        st.cacheLight=make_buffer(st.d12.Get(), uint64_t(kCacheSlots)*16, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create radiance cache light");
        st.cacheKeysUav=raw_uav(st.d12.Get(), st.heap, st.cacheKeys.resource.Get(), uint64_t(kCacheSlots)*4);
        st.cacheSumsUav=raw_uav(st.d12.Get(), st.heap, st.cacheSums.resource.Get(), uint64_t(kCacheSlots)*16);
        st.cacheLightUav=raw_uav(st.d12.Get(), st.heap, st.cacheLight.resource.Get(), uint64_t(kCacheSlots)*16);
        st.cacheRan=false;
        UINT64 frequency=0; if(SUCCEEDED(st.queue->GetTimestampFrequency(&frequency)) && frequency) st.ticksPerMs=double(frequency)/1000.0;
        st.queried.fill(false);
        // The atlas starts white: a texture not baked yet leaves its material's own color.
        st.atlas=make_shared(kAtlasSize, kAtlasSize, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        { const float white[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.atlas.t.uav.Get(), white); }
        // The alpha atlas starts opaque: a tile not baked yet keeps its surface solid, as before its texture came.
        st.alphaAtlas=make_shared(kAtlasSize, kAtlasSize, DXGI_FORMAT_R8_UNORM, false);
        { const float one[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.alphaAtlas.t.uav.Get(), one); }
        // The cookie atlas starts white: a cookie not baked yet leaves its light unmasked.
        st.cookieAtlas=make_shared(kCookieAtlasSize, kCookieAtlasSize, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        { const float white[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.cookieAtlas.t.uav.Get(), white); }
        st.sky=make_shared(kSkyWidth, kSkyHeight, DXGI_FORMAT_R16G16B16A16_FLOAT, false);
        { const float blue[4]={0.4f, 0.6f, 1.0f, 1}; st.c11->ClearUnorderedAccessViewFloat(st.sky.t.uav.Get(), blue); }
        log("Path tracing ready: private D3D12 compute queue, producer and consumer fences, "+std::to_string(kHeapSize)+" descriptors, "+
            (st.useDxr12 ? std::string("Shader Model 6.9 passes (opacity micromaps")+(st.giSer ? ", Shader Execution Reordering)" : ")") : std::string("Shader Model 6.6 passes")));
    } catch(...) { release_all(); throw; }
    st.d3d12Ready=true;
    return true;
}

void set_constants(const D11Constants& c) { st.c11->UpdateSubresource(st.constants.Get(), 0, nullptr, &c, 0, 0); }
// Whether the game's device loads this format through a typed UAV (D3D11's optional formats), and whether it is a floating
// point one, as the D3D11 passes that read HDRP's color buffer back through a UAV declare it (float4; a UNORM target would
// need unorm float4): otherwise those passes draw over it from a copy.
bool typed_uav_load(DXGI_FORMAT format) {
    D3D11_FEATURE_DATA_FORMAT_SUPPORT2 s{format};
    return SUCCEEDED(st.d11->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &s, sizeof(s))) && (s.OutFormatSupport2&D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD);
}
bool float_format(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R32G32_FLOAT: return true;
    default: return false;
    }
}
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
// A mask map's view: its bytes as they are, typeless formats included (color_view reads those as sRGB). Through an sRGB
// view a metallic or smoothness of 0.5 in a color channel reads 0.21; a texture only an sRGB view can read has that undone
// after filtering (bake_textures).
DXGI_FORMAT linear_view(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS: return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_BC1_TYPELESS: return DXGI_FORMAT_BC1_UNORM;
    case DXGI_FORMAT_BC2_TYPELESS: return DXGI_FORMAT_BC2_UNORM;
    case DXGI_FORMAT_BC3_TYPELESS: return DXGI_FORMAT_BC3_UNORM;
    case DXGI_FORMAT_BC7_TYPELESS: return DXGI_FORMAT_BC7_UNORM;
    default: return color_view(f);
    }
}
bool decodes_srgb(DXGI_FORMAT f) {
    switch(f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_BC1_UNORM_SRGB: case DXGI_FORMAT_BC2_UNORM_SRGB: case DXGI_FORMAT_BC3_UNORM_SRGB: case DXGI_FORMAT_BC7_UNORM_SRGB: return true;
    default: return false;
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
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=b.mask ? linear_view(d.Format) : color_view(d.Format);
            v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels;
            ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(st.d11->CreateShaderResourceView(t.Get(), &v, &srv))) { log_throttled("A material texture (format "+std::to_string(d.Format)+") has no color view; its material keeps its own color"); continue; }
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/128.f));
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kTilesPerRow*kTileSize; c.c0[1]=uint32_t(b.tile)/kTilesPerRow*kTileSize; c.c1[0]=std::min(lod, float(d.MipLevels-1));
            // A mask map is stored as its bytes are (an sRGB view's decoding undone), its channels as c0.w names them.
            if(b.mask) { c.c0[2]=2u|(decodes_srgb(v.Format) ? 0u : 1u); c.c0[3]=b.channels; }
            set_constants(c);
            run_cs(st.csAtlas.Get(), {srv.Get()}, st.atlas.t.uav.Get(), kTileSize/8, kTileSize/8, st.linearWrap.Get());
        } catch(const std::exception& e) { log_throttled(std::string("Material texture bake failed: ")+e.what()); }
    }
}

// Lights' cookies into their 64x64 tiles, a few a frame: a point light's cubemap as an octahedral map, a spot's texture
// as it is, from the channel that holds the mask (alpha of an alpha-only texture, red of a single-channel one).
void bake_cookies() {
    std::vector<TextureBake> bakes;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        const size_t n=std::min<size_t>(e.newCookies.size(), 8);
        bakes.assign(std::make_move_iterator(e.newCookies.begin()), std::make_move_iterator(e.newCookies.begin()+n));
        e.newCookies.erase(e.newCookies.begin(), e.newCookies.begin()+n);
    }
    if(bakes.empty() || !st.cookieAtlas.t.uav) return;
    wait_for_scene_readers();
    for(auto& b:bakes) {
        try {
            ComPtr<ID3D11Texture2D> t; check(b.texture.As(&t), "Cookie is not a texture");
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d);
            if(d.SampleDesc.Count!=1 || !(d.BindFlags&D3D11_BIND_SHADER_RESOURCE)) continue;
            const bool cube=(d.MiscFlags&D3D11_RESOURCE_MISC_TEXTURECUBE) && d.ArraySize>=6;
            D3D11_SHADER_RESOURCE_VIEW_DESC v{}; v.Format=color_view(d.Format);
            if(cube) { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE; v.TextureCube.MipLevels=d.MipLevels; }
            else { v.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; v.Texture2D.MipLevels=d.MipLevels; }
            ComPtr<ID3D11ShaderResourceView> srv;
            if(FAILED(st.d11->CreateShaderResourceView(t.Get(), &v, &srv))) { log_throttled("A light's cookie (format "+std::to_string(d.Format)+") has no view; its light stays unmasked"); continue; }
            // An octahedral texel covers about a cube face's texel at half the tile's size per face.
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/float(cube ? kCookieTileSize/2 : kCookieTileSize)));
            const bool alphaOnly=d.Format==DXGI_FORMAT_A8_UNORM;
            const bool redOnly=d.Format==DXGI_FORMAT_R8_UNORM || d.Format==DXGI_FORMAT_BC4_UNORM || d.Format==DXGI_FORMAT_BC4_TYPELESS ||
                               d.Format==DXGI_FORMAT_R16_UNORM || d.Format==DXGI_FORMAT_R16_FLOAT;
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kCookieTilesPerRow*kCookieTileSize; c.c0[1]=uint32_t(b.tile)/kCookieTilesPerRow*kCookieTileSize;
            c.c1[0]=std::min(lod, float(d.MipLevels-1)); c.c1[1]=alphaOnly ? 1.f : redOnly ? 2.f : 0.f;
            set_constants(c);
            run_cs(cube ? st.csCookieCube.Get() : st.csCookie2D.Get(), {srv.Get()}, st.cookieAtlas.t.uav.Get(), kCookieTileSize/8, kCookieTileSize/8, st.linearClamp.Get());
        } catch(const std::exception& e) { log_throttled(std::string("Cookie bake failed: ")+e.what()); }
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
            // Sampled 4x4 a texel at the mip nearest four times the tile's size, each sample tested against the cutoff.
            const float lod=std::max(0.f, std::log2(float(std::max(d.Width, d.Height))/float(4*kAlphaTileSize)));
            D11Constants c{}; c.c0[0]=uint32_t(b.tile)%kAlphaTilesPerRow*kAlphaTileSize; c.c0[1]=uint32_t(b.tile)/kAlphaTilesPerRow*kAlphaTileSize; c.c1[0]=std::min(lod, float(d.MipLevels-1));
            c.c1[1]=b.cutoff;
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

// The water reflections' textures (render event 4), made at the render size when water first needs them.
void release_water_textures() {
    for(auto& in:st.waterIn) { release_shared(in.depth); release_shared(in.nrm); }
    for(auto& out:st.waterOut) release_shared(out);
    for(auto& t:st.waterHist) { if(t.resource) st.release.add(st.consumer.value, t.resource.Get()); st.heap.release(t.srv); st.heap.release(t.uav); t=Texture{}; }
    st.waterInRetire={}; st.waterLatest=State::WaterResult{};
}
void ensure_water_textures(uint32_t w, uint32_t h) {
    if(st.waterHist[1] && st.waterOut[0].t.width==w && st.waterOut[0].t.height==h) return;
    release_water_textures();
    try {
        for(int i=0;i<2;++i) {
            st.waterIn[i].depth=make_shared(w, h, DXGI_FORMAT_R32_FLOAT, false);
            st.waterIn[i].nrm=make_shared(w, h, DXGI_FORMAT_R8G8B8A8_UNORM, false);
            st.waterOut[i]=make_shared(w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
            st.waterHist[i]=make_texture(st.d12.Get(), st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create water reflections history");
        }
    } catch(...) { release_water_textures(); throw; } // all of them or none: the last one made stands for the set
}

void ensure_frame_textures(uint32_t w, uint32_t h) {
    if(st.width==w && st.height==h && st.outputs[0].a.t.d11) return;
    // Whatever used the old textures has to be finished on the GPU first.
    st.consumer.wait_cpu(st.consumer.value);
    release_water_textures();
    for(auto& in:st.inputs) for(Shared* s:{&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure}) release_shared(*s);
    for(auto& out:st.outputs) { release_shared(out.a); release_shared(out.b); release_shared(out.c); }
    auto drop=[](Texture& t) { if(t.resource) st.release.add(st.consumer.value, t.resource.Get()); st.heap.release(t.srv); st.heap.release(t.uav); t=Texture{}; };
    for(auto& t:st.surface) drop(t);
    for(auto& t:st.normal) drop(t);
    for(auto& t:st.material) drop(t);
    for(Texture* t:{&st.diffuseRaw, &st.sunRaw, &st.specRaw, &st.tmpA, &st.tmpB, &st.tmpC, &st.tmpD, &st.directTmp, &st.directOut,
                    &st.rrgDepth, &st.rrgMotion, &st.rrgNormals, &st.rrgDiffuse, &st.rrgSpecular,
                    &st.nrdViewZ, &st.nrdMotion, &st.nrdNormalRoughness, &st.nrdDiffuseIn, &st.nrdDiffuseOut}) drop(*t);
    st.nrd.destroy(); st.nrdHavePrevious=false;
    for(auto* a:{&st.diffuseHist, &st.diffuseMoments, &st.directHist, &st.directMoments, &st.specHist, &st.specMoments}) for(auto& t:*a) drop(t);
    drop(st.hdrpShares); for(auto& t:st.hdrpHistory) drop(t);
    for(Buffer* r:{&st.diRes, &st.giRes}) if(*r) st.release.add(st.consumer.value, r->resource.Get());
    st.heap.release(st.diUav); st.heap.release(st.giUav);
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
        st.outputs[i].c=make_shared(w, h, DXGI_FORMAT_R16_FLOAT, true);
        st.surface[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create surface texture");
        st.normal[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create normal texture");
        st.diffuseHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create diffuse history");
        // Moments in 32-bit floats: a hot spot's second moment passed half precision's largest value (65,504 at a
        // luminance of 256 in storage units), and a dim wall's fell below its normal range.
        st.diffuseMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create diffuse moments");
        st.directHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light history");
        st.directMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create direct light moments");
        st.specHist[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular history");
        st.specMoments[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create specular moments");
        // This frame's material and the previous frame's: RTXDI's temporal reuse weighs a light at the previous surface.
        st.material[i]=make_texture(d, heap, w, h, DXGI_FORMAT_R32G32_UINT, "Create material texture");
    }
    // RTXDI's reservoir layout (RTXDI_ReservoirPositionToPointer): 16x16 pixel blocks, block rows, then arrays.
    st.rtxdiRowPitch=((w+RTXDI_RESERVOIR_BLOCK_SIZE-1)/RTXDI_RESERVOIR_BLOCK_SIZE)*RTXDI_RESERVOIR_BLOCK_SIZE*RTXDI_RESERVOIR_BLOCK_SIZE;
    st.rtxdiArrayPitch=st.rtxdiRowPitch*((h+RTXDI_RESERVOIR_BLOCK_SIZE-1)/RTXDI_RESERVOIR_BLOCK_SIZE);
    st.diRes=make_buffer(d, uint64_t(st.rtxdiArrayPitch)*2*sizeof(RTXDI_PackedDIReservoir), D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create DI reservoirs");
    st.diUav=structured_uav(d, heap, st.diRes.resource.Get(), st.rtxdiArrayPitch*2, sizeof(RTXDI_PackedDIReservoir));
    st.giRes=make_buffer(d, uint64_t(st.rtxdiArrayPitch)*2*sizeof(RTXDI_PackedGIReservoir), D3D12_HEAP_TYPE_DEFAULT,
                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, "Create GI reservoirs");
    st.giUav=structured_uav(d, heap, st.giRes.resource.Get(), st.rtxdiArrayPitch*2, sizeof(RTXDI_PackedGIReservoir));
    if(!st.neighborOffsets) {
        std::vector<uint8_t> bytes(size_t(kNeighborOffsets)*2);
        rtxdi::FillNeighborOffsetBuffer(bytes.data(), kNeighborOffsets);
        st.neighborOffsets=make_buffer(d, uint64_t(kNeighborOffsets)*8, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, "Create RTXDI neighbour offsets");
        auto* offsets=static_cast<float*>(st.neighborOffsets.mapped);
        for(size_t i=0;i<bytes.size();++i) offsets[i]=float(int8_t(bytes[i]))/127.f; // signed bytes, as RTXDI's R8G8_SNORM view reads them
        st.neighborOffsetsSrv=structured_srv(d, heap, st.neighborOffsets.resource.Get(), kNeighborOffsets, 8);
    }
    st.diffuseRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create diffuse signal");
    st.sunRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light signal");
    st.specRaw=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular signal");
    st.tmpA=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create denoiser ping");
    st.tmpB=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create denoiser pong");
    st.tmpC=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular denoiser ping");
    st.tmpD=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create specular denoiser pong");
    st.directTmp=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light denoiser ping");
    st.directOut=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create direct light denoiser output");
    st.hdrpShares=make_texture(d, heap, w, h, DXGI_FORMAT_R16_FLOAT, "Create the shares of HDRP's light traced shadows leave");
    for(auto& t:st.hdrpHistory) t=make_texture(d, heap, w, h, DXGI_FORMAT_R16G16_FLOAT, "Create their history");
    st.hdrpRan=false;
    st.width=w; st.height=h; st.resetNext=true; st.latest=Result{}; st.inputsRetire={};
    // Per pixel: inputs 2x28 B, outputs 2x16 B, surfaces 2x24 B, material 2x8 B, reservoirs 2x(32+24) B, signals and denoiser 21x8 B,
    // HDRP's light's shadowed shares 2+2x4 B.
    const double mb=double(w)*h*(2*28+2*16+2*24+2*8+2*56+21*8+10)/1048576.0;
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

// One 8x8 group per 8x8 pixels of the render size, or the groups given.
void dispatch(ID3D12GraphicsCommandList* list, ID3D12PipelineState* pso, std::initializer_list<uint32_t> constants, Barriers& barriers,
              uint32_t groupsX=0, uint32_t groupsY=0) {
    uint32_t values[8]; std::fill(std::begin(values), std::end(values), kInvalid);
    size_t i=0; for(uint32_t v:constants) if(i<8) values[i++]=v;
    list->SetPipelineState(pso);
    list->SetComputeRoot32BitConstants(1, 8, values, 0);
    list->Dispatch(groupsX ? groupsX : (st.width+7)/8, groupsY ? groupsY : (st.height+7)/8, 1);
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

// NVIDIA NRD's inputs and output at the render size, made when it first denoises: view depth, 2.5D motion, normal and
// roughness in its encoding, the bounced light with its hit distance, and its result.
void ensure_nrd_textures() {
    if(st.nrdViewZ) return;
    auto* d=st.d12.Get(); const uint32_t w=st.width, h=st.height;
    st.nrdViewZ=make_texture(d, st.heap, w, h, DXGI_FORMAT_R32_FLOAT, "Create NRD view depth");
    st.nrdMotion=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD motion");
    st.nrdNormalRoughness=make_texture(d, st.heap, w, h, DXGI_FORMAT_R10G10B10A2_UNORM, "Create NRD normals");
    st.nrdDiffuseIn=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD diffuse input");
    st.nrdDiffuseOut=make_texture(d, st.heap, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, "Create NRD diffuse output");
}
// World-to-view for NRD in the scene's origin-relative space: HDRP's camera rotation (Unity's memory order, column
// vectors) and the camera's position, so the previous frame's differs from this one's by the camera's move.
void nrd_view(const float* view, const double* camera, float* out) {
    std::memcpy(out, view, 16*sizeof(float));
    const float p[3]={float(camera[0]-st.origin[0]), float(camera[1]-st.origin[1]), float(camera[2]-st.origin[2])};
    for(int r=0;r<3;++r) out[12+r]=-(view[r]*p[0]+view[4+r]*p[1]+view[8+r]*p[2]);
    out[3]=out[7]=out[11]=0.f; out[15]=1.f;
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

// The stages' mean GPU time over the frames summed, as text: "scene 1.20 ms, prepare 0.31 ms, ..., water 0.80 ms".
std::string stage_text(const std::array<double, kStages>& sums, uint32_t frames, double water, uint32_t waterFrames) {
    if(!frames) return "none yet";
    std::string text;
    for(uint32_t s=0;s<kStages;++s) {
        char b[64]; std::snprintf(b, sizeof(b), "%s%s %.2f ms", s ? ", " : "", kStageNames[s], sums[s]/frames);
        text+=b;
    }
    if(waterFrames) { char b[64]; std::snprintf(b, sizeof(b), ", water reflections %.2f ms", water/waterFrames); text+=b; }
    return text;
}
// About once a second, the stages' means since for TGPT_GetStageTimes (the F8 report).
void publish_stage_times() {
    const uint64_t now=GetTickCount64();
    if(!st.recentStart) { st.recentStart=now; return; }
    if(now-st.recentStart<1000 || !st.recentFrames) return;
    const std::string text=stage_text(st.recentStage, st.recentFrames, st.recentWater, st.recentWaterFrames);
    { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.stageTimes=text; }
    st.recentStage={}; st.recentWater=0; st.recentFrames=0; st.recentWaterFrames=0; st.recentStart=now;
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
        const auto* t=static_cast<const uint64_t*>(st.queryReadback.mapped)+index*kMarks;
        if(t[kStages]>t[0]) {
            const double ms=double(t[kStages]-t[0])/st.ticksPerMs; { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); e.status.gpuMs=float(ms); } st.summaryGpu+=ms;
            bool ordered=true; for(uint32_t s=0;s<kStages;++s) ordered=ordered && t[s+1]>=t[s];
            if(ordered) {
                for(uint32_t s=0;s<kStages;++s) { const double stage=double(t[s+1]-t[s])/st.ticksPerMs; st.summaryStage[s]+=stage; st.recentStage[s]+=stage; }
                ++st.summaryStageFrames; ++st.recentFrames;
            }
            publish_stage_times();
        }
        // And the sky the camera sees, against the open sky's.
        const auto* l=static_cast<const float*>(st.probeReadback.mapped)+index*12;
        const float traced=0.2126f*l[0]+0.7152f*l[1]+0.0722f*l[2], open=0.2126f*l[4]+0.7152f*l[5]+0.0722f*l[6];
        if(std::isfinite(traced) && std::isfinite(open) && open>0.f && l[3]>0.f) {
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            e.status.cameraLight=traced/open; for(int c=0;c<3;++c) e.status.cameraRadiance[c]=l[c];
        }
        {
            // What the traced scene had under the screen's centre.
            uint32_t pick[4]; std::memcpy(pick, l+8, sizeof(pick));
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            e.status.pickFound=(pick[3]&1u) ? 1u : 0u; e.status.pickInstance=pick[0]; e.status.pickMaterial=pick[1]; e.status.pickDistance=l[10];
            e.status.pickSurface=pick[3];
        }
        // And the traced light's histogram: its median and 90th percentile, log2 (bins of 24/64 EV from -16).
        const auto* bins=static_cast<const uint32_t*>(st.meterReadback.mapped)+index*64;
        uint64_t total=0; for(int i=0;i<64;++i) total+=bins[i];
        if(total>0) {
            auto percentile=[&](double q) {
                const double target=q*double(total); double below=0;
                for(int i=0;i<64;++i) {
                    if(bins[i] && below+bins[i]>=target) return float(-16.0+(double(i)+(target-below)/double(bins[i]))*24.0/64.0);
                    below+=bins[i];
                }
                return 8.f;
            };
            auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
            e.status.tracedLog2Mid=percentile(0.5); e.status.tracedLog2High=percentile(0.98);
            e.status.tracedSamples=uint32_t(std::min<uint64_t>(total, 0xFFFFFFFFull));
        }
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
    // The frame's start, and the end of each stage (kStageNames), whether or not it had work this frame.
    auto mark=[&](uint32_t stage) { list->EndQuery(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*kMarks+stage); };
    mark(0);

    // The scene origin follows the camera in 256 m steps, keeping positions small next to it.
    const double cam[3]={f.cameraPosition[0], f.cameraPosition[1], f.cameraPosition[2]};
    std::memcpy(st.prevOrigin, st.origin, sizeof(st.origin));
    if(!st.haveOrigin || std::fabs(cam[0]-st.origin[0])>512 || std::fabs(cam[1]-st.origin[1])>512 || std::fabs(cam[2]-st.origin[2])>512) {
        for(int i=0;i<3;++i) st.origin[i]=std::round(cam[i]/256.0)*256.0;
        if(!st.haveOrigin) std::memcpy(st.prevOrigin, st.origin, sizeof(st.origin));
        st.haveOrigin=true;
    }
    st.scene.ommAllowed=!(f.options&TGPT_OPTION_NO_MICROMAPS);
    st.scene.emissiveLights=(f.options&TGPT_OPTION_EMISSIVE_LIGHTS)!=0;
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
    // Frames that went untraced (path tracing turned off a while, another camera, a failed frame) leave history from
    // another view: it starts over.
    const bool gap=st.haveFrameIndex && f.frameIndex!=st.lastFrameIndex+1;
    const bool reset=st.resetNext || (f.flags&TGPT_FRAME_RESET) || gap;
    st.resetNext=false; st.haveFrameIndex=true; st.lastFrameIndex=f.frameIndex;
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
    // NVIDIA NRD (RELAX) denoises the bounced light instead of the built-in denoiser where it is the choice and started on
    // the device, for the image itself (debug views show the built-in denoiser's). The direct light and the reflections
    // keep their own filters (pt_nrd_prep says why). Its texture pools follow the render size; the GPU finishes the old
    // ones' frames before they are replaced.
    bool useNrd=(f.options&TGPT_OPTION_NRD) && !(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) && !rrg &&
        !st.nrdFailed && nrd_available() && f.debugView==TGPT_VIEW_NONE;
    ID3D12PipelineState* nrdPrep=nullptr;
    if(useNrd) {
        try {
            std::string error;
            if(st.nrd.needs_pools(st.width, st.height)) {
                st.consumer.wait_cpu(st.consumer.value);
                if(!st.nrd.ensure(st.d12.Get(), st.width, st.height, error)) throw DxError(E_FAIL, error);
            }
            ensure_nrd_textures();
            nrdPrep=nrd_prep_for(st.root.Get());
            if(!nrdPrep) throw DxError(E_FAIL, "no prep pass");
        } catch(const DxError& e) {
            st.nrdFailed=true; useNrd=false;
            log(std::string("NVIDIA NRD could not start (")+e.what()+"); the built-in denoiser serves for the rest of the session");
        }
    }
    bool sss=false; // light scattered under skin: a diffusion profile with a filter radius (TGPT_SetDiffusionProfiles)
    const bool denoise=!(f.options&(TGPT_OPTION_NO_DENOISER|TGPT_OPTION_RAY_RECONSTRUCTION)) && !rrg && !useNrd;
    const bool denoiseOwn=denoise || useNrd; // the direct light's and the reflections' own filters, which serve with NRD too
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
    k.prevCameraPos[3]=std::isfinite(f.forward[0]) ? std::clamp(f.forward[0], 0.f, 1.f) : 1.f; // HDRP's ambient kept on what it shades itself
    // Its history froze while Ray Reconstruction, Ray Regeneration or no denoiser served: it starts over.
    // With NRD only the direct light's and the reflections' filters run, whose histories carry on from the built-in
    // denoiser's (its bounced light's history starts over when it takes over again).
    const bool resumeDenoiser=(denoise && !st.denoisedLast) || (useNrd && !st.ownDenoisedLast);
    st.denoisedLast=denoise; st.ownDenoisedLast=denoiseOwn;
    k.size[0]=st.width; k.size[1]=st.height; k.size[2]=f.frameIndex; k.size[3]=(reset ? 1u : 0u)|(resumeDenoiser ? 2u : 0u);
    k.settings[0]=f.thickProfiles; k.settings[1]=f.debugView<TGPT_VIEW_COUNT ? f.debugView : 0; k.settings[2]=std::clamp(f.bounces, 1u, 4u); k.settings[3]=std::min(f.spatialSamples, 16u);
    k.settings2[0]=st.scene.previousLightsSrv!=kInvalid ? st.scene.previousLightCount : 0u; k.settings2[1]=rrg ? f.options : (f.options&~TGPT_OPTION_RAY_REGENERATION); k.settings2[2]=st.scene.lightCount; k.settings2[3]=atrousIterations;
    k.prevOriginDelta[3]=std::clamp(f.indirectColor, 0.f, 4.f); // Indirect Color, applied where the bounced light is shaded
    k.cameraPos[3]=std::isfinite(f.reflectionBoost) ? std::clamp(f.reflectionBoost, 0.f, 1.f) : 0.f; // Reflection Boost, in the prepare pass
    k.params[0]=std::isfinite(f.tracedGain) && f.tracedGain>0.f ? std::clamp(f.tracedGain, 1.f/64.f, 4096.f) : 1.f; k.params[1]=f.maxRayDistance>1 ? f.maxRayDistance : 1000.f; k.params[2]=f.skyIntensity>0 ? f.skyIntensity : 1.f;
    k.params[3]=std::max(8.f, 30.f*float(st.width)/1920.f);
    k.inputs0[0]=in.gb0.srv; k.inputs0[1]=in.nrm.srv; k.inputs0[2]=in.gb2.srv; k.inputs0[3]=in.gb3.srv;
    k.inputs1[0]=in.depth.srv; k.inputs1[1]=in.motion.srv; k.inputs1[2]=in.exposure.srv; k.inputs1[3]=st.sky.srv;
    k.scene0[0]=st.scene.instancesSrv; k.scene0[1]=st.scene.meshesSrv; k.scene0[2]=st.scene.geometriesSrv; k.scene0[3]=st.scene.materialsSrv;
    k.scene1[0]=st.scene.materialIndicesSrv; k.scene1[1]=st.scene.lightsSrv; k.scene1[2]=st.atlas.srv; k.scene1[3]=out.a.uav;
    k.extra[0]=st.alphaAtlas.srv; k.extra[1]=st.material[prev].uav; k.extra[2]=st.neighborOffsetsSrv; k.extra[3]=st.scene.previousLightsSrv;
    // The reference radiance the clamps measure against and traced light is stored relative to: a white surface in full
    // sun, or lit by the brightest light 1 m away, whichever is brighter. It depends on the scene's lights, never on
    // where the camera looks or on HDRP's exposure (clamps measured in pre-exposed units made a hall's bounced light
    // follow the auto exposure). Eased in log space over about ten frames: a bright light coming into range at night
    // would otherwise jump it, and the built-in denoiser rescales its history by the change (pt_temporal).
    // The clamps' own reference is the sun's, or the brightest light's 25 cm away (16 times its 1 m value): the table under
    // a candle and the walls of a fireplace are the bounced light's main sources indoors.
    {
        const double sun=f.sunColor[3]>0.f ? (0.2126*f.sunColor[0]+0.7152*f.sunColor[1]+0.0722*f.sunColor[2])/3.14159265358979 : 0.0;
        const double lamp=double(st.scene.brightest_light())/3.14159265358979;
        double ref=std::max(sun, lamp), clampRef=std::max(sun, 16.0*lamp);
        if(!(ref>1e-6) || !std::isfinite(ref)) ref=1.0;
        if(!(clampRef>1e-6) || !std::isfinite(clampRef)) clampRef=ref;
        auto ease=[reset](double previous, double target) {
            return (reset || !(previous>0.0)) ? target : std::exp(std::log(previous)+(std::log(target)-std::log(previous))*0.1);
        };
        const double previous=st.reference;
        st.reference=ease(previous, ref); st.clampReference=ease(st.clampReference, clampRef);
        k.radiance[0]=float(st.reference); k.radiance[1]=float(previous>0.0 ? previous : st.reference); k.radiance[2]=float(st.clampReference);
        { const auto s=st.scene.stats(); k.radiance[3]=float((s.swayingInstances>0 ? 1u : 0u)|(s.clothInstances>0 ? 2u : 0u)); } // what the wind moves; thin cloth
        for(int i=0;i<4;++i) k.fog[i]=std::isfinite(f.fog[i]) ? f.fog[i] : 0.f;
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); std::memcpy(k.sssProfiles, e.sssProfiles.data(), sizeof(k.sssProfiles)); sss=e.sss;
          std::memcpy(k.transmission, e.transmission.data(), sizeof(k.transmission)); }
    }
    // NVIDIA RTXDI's ReSTIR DI reuse, with the defaults of its ReSTIRDI.cpp except where noted: temporal history in
    // frames (the plugin's, 20 by default), permutation sampling with a new random number every frame, the boiling
    // filter at 0.2 (pt_boil.hlsli, for any wave width), its Basic bias correction for spatial reuse over the Spatial
    // Samples setting's neighbours within the spatial radius (its pairwise MIS let empty neighbours, out of every
    // lamp's range, take the pixel's own light's weight away), and no ray-traced bias correction (the shading pass
    // traces the final visibility, and pt_di drops a light its pixel doesn't see).
    k.rtxdiRuntime.neighborOffsetMask=kNeighborOffsets-1; k.rtxdiRuntime.activeCheckerboardField=0; k.rtxdiRuntime.frameIndex=f.frameIndex;
    k.rtxdiReservoirs.reservoirBlockRowPitch=st.rtxdiRowPitch; k.rtxdiReservoirs.reservoirArrayPitch=st.rtxdiArrayPitch;
    k.diTemporal.maxHistoryLength=f.diHistory ? std::clamp(f.diHistory, 1u, 64u) : 20u;
    // Basic bias correction; ray-traced (a neighbour counts only if it sees the chosen light) with the option that keeps
    // visibility in the reservoirs (pt_di.hlsl), which Basic's normalization would darken shadows' surroundings with.
    const bool diVisibility=(f.options&TGPT_OPTION_DI_VISIBILITY)!=0;
    k.diTemporal.biasCorrectionMode=diVisibility ? ReSTIRDI_TemporalBiasCorrectionMode::Raytraced : ReSTIRDI_TemporalBiasCorrectionMode::Basic;
    k.diTemporal.depthThreshold=0.1f; k.diTemporal.normalThreshold=0.5f;
    k.diTemporal.enableVisibilityShortcut=0; k.diTemporal.enablePermutationSampling=1;
    k.diTemporal.uniformRandomNumber=rtxdi::JenkinsHash(f.frameIndex);
    k.diBoiling.enableBoilingFilter=(f.options&TGPT_OPTION_NO_BOILING) ? 0u : 1u; k.diBoiling.boilingFilterStrength=0.2f;
    k.diSpatial.numSamples=std::min(f.spatialSamples, 16u); k.diSpatial.numDisocclusionBoostSamples=8;
    k.diSpatial.samplingRadius=k.params[3]; k.diSpatial.biasCorrectionMode=diVisibility ? ReSTIRDI_SpatialBiasCorrectionMode::Raytraced : ReSTIRDI_SpatialBiasCorrectionMode::Basic;
    k.diSpatial.depthThreshold=0.1f; k.diSpatial.normalThreshold=0.5f; k.diSpatial.targetHistoryLength=0;
    k.diSpatial.enableMaterialSimilarityTest=1;
    // Neighbours with a sample count of 2 or less are skipped as unconverged, which only means something with a longer
    // history: with 1 frame (or none) every neighbour has that count, and spatial reuse would do nothing.
    // Not discounted: RTXDI 3.1's Basic spatial reuse counts a neighbour it skips as naive in the MIS denominator all the
    // same, which darkened pixels for a moment after disocclusions.
    k.diSpatial.discountNaiveSamples=0u;
    // Its ReSTIR GI reuse, with the defaults of its ReSTIRGI.cpp except where noted: temporal history in frames (the
    // plugin's, 8 by default) over at most 30 frames of a sample's age, without the fallback to whatever surface was at
    // the pixel before in disocclusions (on in RTXDI's defaults, which call it biased, usually darker: surfaces a turn
    // of the camera reveals took their light from unrelated ones), permutation sampling (off in RTXDI's defaults; on
    // here, as for DI, for the denoisers), the boiling filter at 0.2 (its own pass: pt_gi_boil), and spatial reuse over
    // the Spatial Samples setting's neighbours within the spatial radius. The shading pass traces the final visibility.
    // TGPT_OPTION_GI_VISIBILITY (the Crevice Light row), measured against the plain path tracer on sand under a canvas
    // close over it (the harness's crevices): both reuses count a pixel for the chosen sample only if it sees it (RTXDI's
    // ray-traced bias correction, a shadow ray for each pixel counted), where Basic's counting them all left the canvas's
    // front edge 14 % short (the pixels further in don't see the sky through the slit under it); a history is kept
    // whatever its sample's age, where RTXDI drops the whole history at 30 frames, and in a dark place lit by rare paths
    // only the dark histories last that long: restarted, they counted the next lit path up to three times, and the sand
    // deep under the canvas came out 9 % too bright (17 % with 20 frames of history), while the history's own limit
    // already fades a stale sample by 8/9 a frame; and the boiling filter brings an outlier down to its group's bound
    // (pt_gi_boil). Together: the edge within 1 % and deeper in 3 to 9 % short, for more noise (in the harness's room the
    // denoised error is 15 to 30 % higher and a single frame's 11 %), so it stays a row, off, until it is seen in game.
    const bool giVisibility=(f.options&TGPT_OPTION_GI_VISIBILITY)!=0;
    const auto giBias=giVisibility ? RTXDI_GIBiasCorrectionMode::Raytraced : RTXDI_GIBiasCorrectionMode::Basic;
    k.giTemporal.depthThreshold=0.1f; k.giTemporal.normalThreshold=0.6f;
    k.giTemporal.maxHistoryLength=f.giHistory ? std::clamp(f.giHistory, 1u, 64u) : 8u;
    k.giTemporal.enableFallbackSampling=0; k.giTemporal.biasCorrectionMode=giBias;
    // A stored age stops at 255: the temporal pass never sees one above 256.
    k.giTemporal.maxReservoirAge=giVisibility ? 256u : 30u; k.giTemporal.enablePermutationSampling=1;
    k.giTemporal.uniformRandomNumber=rtxdi::JenkinsHash(f.frameIndex^0x9E3779B9u);
    k.giBoiling.enableBoilingFilter=(f.options&(TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_GI_BOILING)) ? 0u : 1u; k.giBoiling.boilingFilterStrength=0.2f;
    k.giSpatial.depthThreshold=0.1f; k.giSpatial.normalThreshold=0.6f;
    k.giSpatial.numSamples=std::min(f.spatialSamples, 16u); k.giSpatial.samplingRadius=k.params[3];
    k.giSpatial.biasCorrectionMode=giBias;
    // Reservoir arrays: this frame's and the previous one's, alternating. With spatial feedback (an option) they stay
    // put instead: temporal reuse writes array 1 from array 0, and spatial reuse writes its result into array 0, which
    // the next frame's temporal reuse reads; no pass reads and writes the same array.
    const bool feedback=(f.options&TGPT_OPTION_SPATIAL_FEEDBACK)!=0;
    const uint32_t rcur=feedback ? 1u : cur, rprev=feedback ? 0u : prev;
    k.rtxdi[0]=rcur; k.rtxdi[1]=rprev; k.rtxdi[2]=rcur; k.rtxdi[3]=rprev;
    k.surface[0]=st.surface[cur].uav; k.surface[1]=st.surface[prev].uav; k.surface[2]=st.normal[cur].uav; k.surface[3]=st.normal[prev].uav;
    k.material[0]=st.material[cur].uav; k.material[1]=st.giUav; k.material[2]=kInvalid; k.material[3]=st.diUav;
    k.signals[0]=st.cookieAtlas.srv; k.signals[1]=st.diffuseRaw.uav; k.signals[2]=st.sunRaw.uav; k.signals[3]=st.specRaw.uav;
    for(int i=0;i<3;++i) k.gridMin[i]=st.scene.gridMin[i];
    k.gridMin[3]=float(1.0/kGridCell);
    k.grid[0]=st.scene.gridCellsSrv; k.grid[1]=st.scene.gridIndicesSrv;
    k.grid[2]=kGridDims[0]|(kGridDims[1]<<8)|(kGridDims[2]<<16); k.grid[3]=std::clamp(f.raysPerPixel, 1u, 16u);
    k.denoise0[0]=st.diffuseHist[cur].uav; k.denoise0[1]=st.diffuseHist[prev].uav; k.denoise0[2]=st.diffuseMoments[cur].uav; k.denoise0[3]=st.diffuseMoments[prev].uav;
    const uint32_t finalDiffuse=!denoise ? st.diffuseRaw.uav : (atrousIterations%2 ? st.tmpA.uav : (atrousIterations ? st.tmpB.uav : st.diffuseHist[cur].uav));
    // The direct light (the sun and the lamps): its own temporal accumulation and two a-trous iterations, ending in directOut.
    k.denoise1[0]=denoiseOwn ? st.directOut.uav : st.sunRaw.uav; k.denoise1[1]=kInvalid;
    // Reflections: temporal accumulation and four a-trous iterations (steps 1, 2, 4, 8), ending in D.
    k.denoise1[2]=denoiseOwn ? st.tmpD.uav : st.specRaw.uav; k.denoise1[3]=st.specHist[prev].uav;
    k.denoise2[0]=st.tmpA.uav; k.denoise2[1]=st.tmpB.uav; k.denoise2[2]=finalDiffuse; k.denoise2[3]=out.b.uav;
    // NRD's result for the bounced light.
    if(useNrd) k.denoise2[2]=st.nrdDiffuseOut.uav;
    // The radiance cache, unless the Radiance Cache setting is off.
    const bool cache=!(f.options&TGPT_OPTION_NO_RADIANCE_CACHE) && st.cache && st.cacheKeysUav!=kInvalid;
    k.cache[0]=st.cacheKeysUav; k.cache[1]=st.cacheSumsUav; k.cache[2]=st.cacheLightUav; k.cache[3]=cache ? 1u : 0u;
    for(int i=0;i<3;++i) k.cacheOrigin[i]=float(st.origin[i]);
    // Emissive triangles: their records this frame and the previous frame's (the same until a build writes the other buffer),
    // and what paths meeting one need to know whether its light was sampled at the vertex they left (the instance table).
    k.emissive[0]=st.scene.emittersSrv; k.emissive[1]=st.scene.previousEmittersSrv;
    k.emissive[2]=st.scene.emittersSrv!=kInvalid ? st.scene.emitterCount : 0u; k.emissive[3]=st.scene.previousEmittersSrv!=kInvalid ? st.scene.previousEmitterCount : 0u;
    k.emissive2[0]=st.scene.emitterTableSrv; k.emissive2[1]=st.scene.emitterTableSrv!=kInvalid ? st.scene.emitterTableInstances : 0u;
    k.emissive2[2]=st.scene.emittersSrv!=kInvalid ? st.scene.sampledClusters : 0u; k.emissive2[3]=st.scene.clusterBase;
    // Traced shadows on what HDRP shades itself (hair, eyes: pt_hdrp_shadow), which take their bounced light from the radiance
    // cache; compose reads their history.
    const bool hdrpShadows=cache && !(f.options&TGPT_OPTION_NO_HDRP_SHADOWS) && st.hdrpShadow && st.hdrpShares;
    k.material[2]=hdrpShadows ? st.hdrpHistory[cur].uav : kInvalid;
    std::memcpy(cpu, &k, sizeof(k));
    // The frame's water surfaces are traced later in the frame (render event 4), with its constants.
    st.waterBase.k=k; st.waterBase.frame=f; st.waterBase.parity=parity;
    list->SetComputeRootConstantBufferView(0, gpu);
    list->SetComputeRootShaderResourceView(2, st.scene.tlas);

    // The shared textures are in COMMON between frames (D3D11 uses them in between) and return there.
    Barriers b;
    const std::initializer_list<Shared*> reads={&in.gb0, &in.nrm, &in.gb2, &in.gb3, &in.depth, &in.motion, &in.exposure, &st.sky, &st.atlas, &st.alphaAtlas, &st.cookieAtlas};
    const std::initializer_list<Shared*> writes={&out.a, &out.b, &out.c};
    for(Shared* s:reads) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(Shared* s:writes) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    b.flush(list);
    // Opacity micromaps for meshes whose textures are in the atlas now; their instances use them next frame.
    st.scene.record_omm(st.d12.Get(), list, st.heap, st.release, retire, st.ommBake.Get());
    // The emissive triangles' records, when their emitters, the lights near them or the origin changed.
    st.scene.record_emitters(list, st.emissive.Get());
    b.flush(list); mark(1); // the scene: its meshes, characters, micromaps, emitters and acceleration structures

    dispatch(list, st.prepare.Get(), {}, b);
    dispatch(list, st.probe.Get(), {st.probeUav}, b, 1, 1);
    b.flush(list); mark(2);
    // The radiance cache starts empty with another scene (TGPT_ClearScene) and when it is turned back on (its light
    // stopped following the scene's while it was off). Its cells are the world's: a camera cut or a quick turn keeps
    // them, which would otherwise darken the bounced light for the half second they take to fill again.
    if(cache && (!st.cacheRan || st.cacheScene!=st.scene.clear_generation())) dispatch(list, st.cache.Get(), {1u}, b, kCacheSlots/256, 1);
    st.cacheRan=cache; st.cacheScene=st.scene.clear_generation();
    dispatch_pass(list, st.giSer.Get(), 0, st.gi.Get(), b);
    b.flush(list); mark(3);
    if(k.giBoiling.enableBoilingFilter) dispatch(list, st.giBoil.Get(), {}, b);
    dispatch(list, st.giSpatial.Get(), {}, b);
    b.flush(list); mark(4);
    dispatch(list, st.di.Get(), {}, b);
    dispatch(list, st.diSpatial.Get(), {}, b);
    b.flush(list); mark(5);
    // The traced shadows on what HDRP shades itself feed the radiance cache too: before it takes this frame's samples in.
    // Traced, then filtered in space and time (without the history the first frame after they were off).
    if(hdrpShadows) {
        dispatch(list, st.hdrpShadow.Get(), {st.hdrpShares.uav, st.hdrpHistory[cur].uav, st.hdrpHistory[prev].uav, 0u}, b);
        dispatch(list, st.hdrpShadow.Get(), {st.hdrpShares.uav, st.hdrpHistory[cur].uav, st.hdrpHistory[prev].uav, 1u, st.hdrpRan ? 0u : 1u}, b);
    }
    st.hdrpRan=hdrpShadows;
    b.flush(list); mark(6);
    if(!(f.options&TGPT_OPTION_NO_SPECULAR)) dispatch_pass(list, st.specularSer.Get(), 1, st.specular.Get(), b);
    b.flush(list); mark(7);
    // Every pass that reads or feeds the radiance cache has run: its cells take this frame's samples in.
    if(cache) dispatch(list, st.cache.Get(), {0u}, b, kCacheSlots/256, 1);
    b.flush(list); mark(8);
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
    }
    if(denoiseOwn) {
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
    st.nrdActive=false;
    uint32_t nrdToStorage=kInvalid; // compose: NRD's result into the tracer's storage units (a float's bits)
    if(useNrd) {
        // NRD's radiance in units of its own: the tracer stores light divided by the scene's reference, which eases as
        // lights come and go, and NRD's history can't be rescaled (the built-in denoiser's is, pt_temporal). The scale
        // holds until NRD's history starts over, which it does too when the reference has moved more than a factor of four.
        const bool previous=st.nrdHavePrevious && !reset;
        double ratio=st.nrdScale>0.0 ? st.reference/st.nrdScale : 0.0;
        const bool restart=!previous || !(ratio>0.25 && ratio<4.0);
        if(restart) { st.nrdScale=st.reference; ratio=1.0; }
        const float toNrd=float(ratio), fromNrd=float(1.0/ratio);
        uint32_t toNrdBits; std::memcpy(&toNrdBits, &toNrd, 4);
        dispatch(list, nrdPrep, {st.nrdViewZ.uav, st.nrdMotion.uav, st.nrdNormalRoughness.uav, st.nrdDiffuseIn.uav, toNrdBits}, b);
        NrdFrame n;
        n.width=st.width; n.height=st.height; n.frameIndex=f.frameIndex;
        n.reset=restart;
        // This frame's camera and the previous one's: HDRP's rotation and projection without jitter, and its jitter.
        nrd_view(f.view, cam, n.worldToView);
        if(previous) nrd_view(st.nrdView, st.nrdCamera, n.worldToViewPrev); else std::memcpy(n.worldToViewPrev, n.worldToView, sizeof(n.worldToView));
        std::memcpy(n.viewToClip, f.nonJitteredProj, sizeof(n.viewToClip));
        std::memcpy(n.viewToClipPrev, previous ? st.nrdProjection : f.nonJitteredProj, sizeof(n.viewToClipPrev));
        for(int i=0;i<2;++i) { n.jitter[i]=f.jitter[i]; n.jitterPrev[i]=previous ? st.nrdJitter[i] : f.jitter[i]; }
        auto tex=[](Texture& t) { return NrdTexture{t.resource.Get(), &t.state, t.format}; };
        n.viewZ=tex(st.nrdViewZ); n.motion=tex(st.nrdMotion); n.normalRoughness=tex(st.nrdNormalRoughness);
        n.diffuse=tex(st.nrdDiffuseIn); n.diffuseOut=tex(st.nrdDiffuseOut);
        std::string error;
        st.nrdActive=st.nrd.dispatch(list, index, st.rings[index], n, error);
        // NRD binds its own descriptor heap and root signature: the tracer's go back.
        list->SetDescriptorHeaps(1, heaps);
        list->SetComputeRootSignature(st.root.Get());
        list->SetComputeRootConstantBufferView(0, gpu);
        list->SetComputeRootShaderResourceView(2, st.scene.tlas);
        b.uav(nullptr); b.flush(list);
        if(st.nrdActive) std::memcpy(&nrdToStorage, &fromNrd, 4);
        else {
            // Compose takes this frame's bounced light as traced (NRD recorded nothing: its checks run before any command).
            b.transition(st.diffuseRaw, D3D12_RESOURCE_STATE_COPY_SOURCE); b.transition(st.nrdDiffuseOut, D3D12_RESOURCE_STATE_COPY_DEST); b.flush(list);
            list->CopyResource(st.nrdDiffuseOut.resource.Get(), st.diffuseRaw.resource.Get());
            b.transition(st.diffuseRaw, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.transition(st.nrdDiffuseOut, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
            st.nrdFailed=true;
            log("NVIDIA NRD failed ("+error+"); the built-in denoiser serves from the next frame on");
        }
        std::memcpy(st.nrdView, f.view, sizeof(st.nrdView)); std::memcpy(st.nrdProjection, f.nonJitteredProj, sizeof(st.nrdProjection));
        for(int i=0;i<2;++i) st.nrdJitter[i]=f.jitter[i];
        for(int i=0;i<3;++i) st.nrdCamera[i]=cam[i];
        st.nrdHavePrevious=true;
    } else st.nrdHavePrevious=false;
    b.flush(list); mark(9);
    // The reflections Ray Regeneration's guides divided by their albedo are multiplied back in.
    dispatch(list, st.compose.Get(), {rrg ? 1u : 0u, out.c.uav, nrdToStorage}, b);
    // Light scattered under skin (pt_sss.hlsl), into the reflections' denoiser ping (free once they are composed) and back.
    if(sss && f.debugView==TGPT_VIEW_NONE) {
        dispatch(list, st.sss.Get(), {out.a.uav, st.tmpC.uav, 0u}, b);
        dispatch(list, st.sss.Get(), {out.a.uav, st.tmpC.uav, 1u}, b);
    }
    // The traced light as shown, for its gain: every other pixel into the histogram (cleared first).
    dispatch(list, st.meter.Get(), {st.meterUav, 0u}, b, 1, 1);
    dispatch(list, st.meter.Get(), {st.meterUav, 1u}, b, (st.width/2+8)/8, (st.height/2+8)/8);

    for(Shared* s:reads) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    for(Shared* s:writes) b.transition_from(s->t.d12.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    b.flush(list);
    b.transition(st.probeBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE); b.flush(list);
    list->CopyBufferRegion(st.probeReadback.resource.Get(), uint64_t(index)*48, st.probeBuffer.resource.Get(), 0, 48);
    b.transition(st.probeBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
    b.transition(st.meterBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE); b.flush(list);
    list->CopyBufferRegion(st.meterReadback.resource.Get(), uint64_t(index)*256, st.meterBuffer.resource.Get(), 0, 256);
    b.transition(st.meterBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); b.flush(list);
    mark(kStages);
    list->ResolveQueryData(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index*kMarks, kMarks, st.queryReadback.resource.Get(), uint64_t(index)*kMarks*8);
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
        std::to_string(s.gridMostInCell)+", "+std::to_string(s.gridBuilds)+" builds) rays/pixel="+std::to_string(std::clamp(f.raysPerPixel, 1u, 16u))+
        " textures="+std::to_string(status.textures)+" materials="+std::to_string(status.materials)+
        (st.useDxr12 ? " micromaps: meshes="+std::to_string(s.ommMeshes)+" waiting="+std::to_string(s.ommWaiting)+" triangles="+std::to_string(s.ommTriangles)+
            " instances="+std::to_string(s.ommInstances)+" "+std::to_string(s.ommBytes>>20)+"MB ser="+(st.giSer ? "on" : "off") : std::string(" sm=6.6"))+
        " characters="+std::to_string(s.charactersTraced)+" ("+std::to_string(s.characterTriangles/1000)+"k triangles)"+
        " emissive: "+std::to_string(s.emitters)+" submeshes, "+std::to_string(s.emitterTriangles)+" triangles, "+std::to_string(s.emitterTrianglesSampled)+
            " sampled as lights in "+std::to_string(s.emitterClustersSampled)+" clusters"+(s.emittersOverBudget ? ", "+std::to_string(s.emittersOverBudget)+" over the budget" : std::string())+
            " ("+std::to_string(s.emitterBuilds)+" builds)"+
        " memory="+std::to_string((s.geometryBytes+s.accelerationBytes)>>20)+"MB sky="+(st.skyValid ? "hdrp" : "plain")+
        " stages: "+stage_text(st.summaryStage, st.summaryStageFrames, st.summaryWater, st.summaryWaterFrames));
    st.lastSummary=now; st.summaryFrames=0; st.summaryGpu=0; st.summaryCpu=0; st.summaryPhase={};
    st.summaryStage={}; st.summaryStageFrames=0; st.summaryWater=0; st.summaryWaterFrames=0;
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
        (rrg_available() && !st.rrgFailed ? TGPT_FEATURE_RAY_REGENERATION : 0u)|(st.rrgActive ? TGPT_FEATURE_RAY_REGENERATION_ACTIVE : 0u)|
        (nrd_available() && !st.nrdFailed ? TGPT_FEATURE_NRD : 0u)|(st.nrdActive ? TGPT_FEATURE_NRD_ACTIVE : 0u);
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
    st.waterBase.valid=false;
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
            // Its color tiles too: white, as the atlas starts (a material's own color, a mask map's dielectric), not an old
            // texture's colors read as a mask map or the other way round.
            if(cleared && st.alphaAtlas.t.uav) {
                wait_for_scene_readers();
                const float one[4]={1, 1, 1, 1}; st.c11->ClearUnorderedAccessViewFloat(st.alphaAtlas.t.uav.Get(), one);
                if(st.atlas.t.uav) st.c11->ClearUnorderedAccessViewFloat(st.atlas.t.uav.Get(), one);
            }
            st.scene.pump_d3d11(st.d11.Get(), st.c11.Get(), st.frame);
            bake_textures();
            bake_alpha_textures();
            bake_cookies();
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
        st.waterBase.valid=true; st.waterBase.sequence=sequence;
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
        const DXGI_FORMAT formats[5]={DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT};
        for(int i=0;i<5;++i) {
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
    // Reflection Boost: the guides describe the surfaces as the tracer shades them (not skin: the thick profiles).
    c.cameraDelta[3]=std::isfinite(frame.reflectionBoost) ? std::clamp(frame.reflectionBoost, 0.f, 1.f) : 0.f;
    c.extras[1]=frame.thickProfiles;
    st.c11->UpdateSubresource(st.compositeConstants.Get(), 0, nullptr, &c, 0, 0);
    {
        tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
        ID3D11ShaderResourceView* srvs[7]={in.gb0.t.srv.Get(), in.nrm.t.srv.Get(), in.gb2.t.srv.Get(), in.depth.t.srv.Get(),
                                           in.gb3.t.srv.Get(), st.sky.t.srv.Get(), in.exposure.t.srv.Get()};
        ID3D11UnorderedAccessView* uavs[3]={st.guides[0].uav.Get(), st.guides[1].uav.Get(), st.guides[2].uav.Get()};
        ID3D11Buffer* cbs[2]={st.constants.Get(), st.compositeConstants.Get()};
        st.c11->CSSetShader(st.csGuides.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 2, cbs);
        st.c11->CSSetShaderResources(0, 7, srvs); st.c11->CSSetUnorderedAccessViews(0, 3, uavs, nullptr);
        st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
        ID3D11ShaderResourceView* noSrvs[7]{}; st.c11->CSSetShaderResources(0, 7, noSrvs);
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
        // Ray Reconstruction's specular hit distance and color before transparency, written with the light it applies
        // (the compute path only), for frames that asked for its guides.
        const bool extras=(frame.flags&TGPT_FRAME_GUIDES) && st.guides[3].uav && st.guides[4].uav && st.guideWidth==w && st.guideHeight==h;
        // The previous frame's work has had a frame to finish: a GPU wait only when it has not.
        if(st.consumer.d12->GetCompletedValue()<prev.retire) { check(st.c11->Wait(st.consumer.d11.Get(), prev.retire), "D3D11 wait for the traced light"); ++st.compositeWaits; }
        CompositeConstants c{};
        std::memcpy(c.invViewProj, frame.invViewProj, sizeof(c.invViewProj));
        for(int i=0;i<3;++i) c.cameraDelta[i]=float(double(frame.cameraPosition[i])-prev.camera[i]);
        // 1: Ray Reconstruction denoises, so the light is taken from the nearest texel (resampling would
        // correlate neighbouring pixels' noise).
        c.frameSize[0]=w; c.frameSize[1]=h; c.frameSize[3]=frame.debugView<TGPT_VIEW_COUNT ? frame.debugView : 0;
        c.frameSize[2]=(frame.options&TGPT_OPTION_RAY_RECONSTRUCTION) ? 1u : 0u;
        c.cameraDelta[3]=float(frame.frameIndex&0xFFFFu); // a new random pick every frame for pixels with no traced light yet
        c.extras[0]=extras ? 1u : 0u;
        st.c11->UpdateSubresource(st.compositeConstants.Get(), 0, nullptr, &c, 0, 0);
        const auto& in=st.inputs[work.parity];
        const auto& out=st.outputs[prev.parity];
        // The exposures this frame's color and the previous frame's traced light were pre-exposed with; this frame's normals
        // for Ray Reconstruction's guides on see-through glass.
        ID3D11ShaderResourceView* srvs[10]={in.gb0.t.srv.Get(), in.gb3.t.srv.Get(), in.depth.t.srv.Get(), in.motion.t.srv.Get(), out.a.t.srv.Get(), out.b.t.srv.Get(),
                                            in.exposure.t.srv.Get(), st.inputs[prev.parity].exposure.t.srv.Get(), out.c.t.srv.Get(), in.nrm.t.srv.Get()};
        ID3D11Buffer* cbs[2]={st.constants.Get(), st.compositeConstants.Get()};
        const auto uavFormat=tgb::typed_write_format(desc.Format);
        ++st.composited;
        { auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex); ++e.status.composited; }
        if((desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS) && uavFormat!=DXGI_FORMAT_UNKNOWN && float_format(uavFormat) && typed_uav_load(uavFormat)) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC v{}; v.Format=uavFormat; v.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11UnorderedAccessView> uav;
            if(SUCCEEDED(st.d11->CreateUnorderedAccessView(target, &v, &uav))) {
                tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
                st.c11->CSSetShader(st.csComposite.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 2, cbs);
                st.c11->CSSetShaderResources(0, 10, srvs);
                // With the extras, the guides too: the composite corrects them on see-through glass (d3d11.hlsl glass_guides).
                ID3D11UnorderedAccessView* u[6]={uav.Get(), extras ? st.guides[3].uav.Get() : nullptr, extras ? st.guides[4].uav.Get() : nullptr,
                                                 extras ? st.guides[0].uav.Get() : nullptr, extras ? st.guides[1].uav.Get() : nullptr, extras ? st.guides[2].uav.Get() : nullptr};
                st.c11->CSSetUnorderedAccessViews(0, 6, u, nullptr);
                st.c11->Dispatch((w+7)/8, (h+7)/8, 1);
                ID3D11ShaderResourceView* none[10]{}; st.c11->CSSetShaderResources(0, 10, none);
                ID3D11UnorderedAccessView* noUavs[6]{}; st.c11->CSSetUnorderedAccessViews(0, 6, noUavs, nullptr);
                ID3D11Buffer* noCbs[2]{}; st.c11->CSSetConstantBuffers(0, 2, noCbs);
                if(extras) {
                    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
                    e.guides.extrasFrameIndex=frame.frameIndex;
                    e.guides.specularHitDistance=uint64_t(uintptr_t(st.guides[3].texture.Get()));
                    e.guides.colorBeforeTransparency=uint64_t(uintptr_t(st.guides[4].texture.Get()));
                }
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
        st.c11->PSSetConstantBuffers(0, 2, cbs); st.c11->PSSetShaderResources(0, 10, srvs);
        st.c11->Draw(3, 0);
        ID3D11ShaderResourceView* none[10]{}; st.c11->PSSetShaderResources(0, 10, none);
        ID3D11Buffer* noCbs[2]{}; st.c11->PSSetConstantBuffers(0, 2, noCbs);
    } catch(const DxError& e) { log_throttled(std::string("Path tracing composite failed: ")+e.what()+" "+tgb::hexhr(e.hr)); }
      catch(const std::exception& e) { log_throttled(std::string("Path tracing composite failed: ")+e.what()); }
}

// Shadow Detail on HDRP's finished image (TGPT_SetShadowDetail, render event 3): in place through a UAV where the texture
// takes typed loads and stores, otherwise drawn over it from a copy.
void shadow_detail_event(uint32_t request) {
    uint64_t color=0; float strength=0;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.shadowDetail[request%e.shadowDetail.size()];
        if(!slot.pending || slot.request!=request) return;
        color=slot.color; strength=slot.strength; slot.pending=false;
    }
    if(!color || !(strength>0.f) || !st.d11 || !st.d3d11Ready || !st.csShadowDetail) return;
    tgb::InternalScope internal(&host());
    try {
        auto* target=reinterpret_cast<ID3D11Resource*>(uintptr_t(color));
        if(!on_game_device(target)) { log_throttled("HDRP's color buffer is not on the game's device; Shadow Detail is not applied"); return; }
        const auto desc=texture_desc(target);
        if(desc.ArraySize!=1 || desc.SampleDesc.Count!=1) { log_throttled("HDRP's color buffer is an array or multisampled; Shadow Detail is not applied"); return; }
        D11Constants c{}; c.c0[0]=desc.Width; c.c0[1]=desc.Height; c.c1[0]=strength;
        set_constants(c);
        ID3D11Buffer* cb=st.constants.Get();
        const auto uavFormat=tgb::typed_write_format(desc.Format);
        if((desc.BindFlags&D3D11_BIND_UNORDERED_ACCESS) && uavFormat!=DXGI_FORMAT_UNKNOWN && float_format(uavFormat) && typed_uav_load(uavFormat)) {
            D3D11_UNORDERED_ACCESS_VIEW_DESC v{}; v.Format=uavFormat; v.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11UnorderedAccessView> uav;
            if(SUCCEEDED(st.d11->CreateUnorderedAccessView(target, &v, &uav))) {
                tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
                st.c11->CSSetShader(st.csShadowDetail.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 1, &cb);
                auto* u=uav.Get(); st.c11->CSSetUnorderedAccessViews(0, 1, &u, nullptr);
                st.c11->Dispatch((desc.Width+7)/8, (desc.Height+7)/8, 1);
                ID3D11UnorderedAccessView* noUav=nullptr; st.c11->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
                ID3D11Buffer* noCb=nullptr; st.c11->CSSetConstantBuffers(0, 1, &noCb);
                return;
            }
        }
        const auto readFormat=tgb::typed_read_format(desc.Format, tgb::ReadAs::Color);
        if(!(desc.BindFlags&D3D11_BIND_RENDER_TARGET) || readFormat==DXGI_FORMAT_UNKNOWN) { log_throttled("HDRP's color buffer can't be written; Shadow Detail is not applied"); return; }
        if(!st.shadowCopy || st.shadowCopyDesc.Width!=desc.Width || st.shadowCopyDesc.Height!=desc.Height || st.shadowCopyDesc.Format!=desc.Format) {
            st.shadowCopy.Reset(); st.shadowCopySrv.Reset(); st.shadowCopyDesc={};
            D3D11_TEXTURE2D_DESC d=desc; d.MipLevels=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.MiscFlags=0; d.CPUAccessFlags=0; d.Usage=D3D11_USAGE_DEFAULT;
            check(st.d11->CreateTexture2D(&d, nullptr, &st.shadowCopy), "Create Shadow Detail copy");
            D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=readFormat; s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; s.Texture2D.MipLevels=1;
            check(st.d11->CreateShaderResourceView(st.shadowCopy.Get(), &s, &st.shadowCopySrv), "Create Shadow Detail copy view");
            st.shadowCopyDesc=desc;
        }
        st.c11->CopySubresourceRegion(st.shadowCopy.Get(), 0, 0, 0, 0, target, 0, nullptr);
        D3D11_RENDER_TARGET_VIEW_DESC v{}; v.Format=readFormat; v.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> rtv; check(st.d11->CreateRenderTargetView(target, &v, &rtv), "Create Shadow Detail RTV");
        tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
        auto* rt=rtv.Get(); st.c11->OMSetRenderTargets(1, &rt, nullptr);
        st.c11->OMSetBlendState(nullptr, nullptr, UINT_MAX); st.c11->OMSetDepthStencilState(st.noDepth.Get(), 0);
        D3D11_VIEWPORT viewport{0, 0, float(desc.Width), float(desc.Height), 0, 1}; st.c11->RSSetViewports(1, &viewport); st.c11->RSSetState(st.raster.Get());
        st.c11->IASetInputLayout(nullptr); st.c11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        st.c11->VSSetShader(st.vsShadowDetail.Get(), nullptr, 0); st.c11->PSSetShader(st.psShadowDetail.Get(), nullptr, 0);
        st.c11->PSSetConstantBuffers(0, 1, &cb); auto* srv=st.shadowCopySrv.Get(); st.c11->PSSetShaderResources(0, 1, &srv);
        st.c11->Draw(3, 0);
        ID3D11ShaderResourceView* noSrv=nullptr; st.c11->PSSetShaderResources(0, 1, &noSrv);
        ID3D11Buffer* noCb=nullptr; st.c11->PSSetConstantBuffers(0, 1, &noCb);
    } catch(const DxError& e) { log_throttled(std::string("Shadow Detail failed: ")+e.what()+" "+tgb::hexhr(e.hr)); }
      catch(const std::exception& e) { log_throttled(std::string("Shadow Detail failed: ")+e.what()); }
}

// Records and submits the water reflections of the frame event 1 traced last (pt_water.hlsl) on the tracer's queue, after
// that frame's own work, from the water input set of this parity into its output. Returns the consumer value it signals.
uint64_t trace_water(uint32_t parity) {
    const uint32_t index=st.waterListIndex++%kFrames;
    st.consumer.wait_cpu(st.waterRetired[index]);
    const uint64_t retire=st.consumer.value+1;
    // What this slot's water pass cost the GPU when it last ran.
    if(st.waterQueried[index] && st.ticksPerMs>0) {
        const auto* t=static_cast<const uint64_t*>(st.queryReadback.mapped)+kWaterQueries+index*2;
        if(t[1]>t[0]) { const double ms=double(t[1]-t[0])/st.ticksPerMs; st.summaryWater+=ms; ++st.summaryWaterFrames; st.recentWater+=ms; ++st.recentWaterFrames; }
    }
    auto* list=st.waterLists[index].Get();
    check(st.waterAllocators[index]->Reset(), "Reset water reflections allocator");
    check(list->Reset(st.waterAllocators[index].Get(), nullptr), "Reset water reflections list");
    st.recording=true;
    list->EndQuery(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kWaterQueries+index*2);
    ID3D12DescriptorHeap* heaps[]={st.heap.get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(st.root.Get());
    // The frame's constants as its trace had them: its camera, scene, lights, sky, exposure and gain.
    std::memcpy(static_cast<uint8_t*>(st.waterFrameConstants.mapped)+uint64_t(index)*kWaterConstantsStride, &st.waterBase.k, sizeof(FrameConstants));
    list->SetComputeRootConstantBufferView(0, st.waterFrameConstants.va()+uint64_t(index)*kWaterConstantsStride);
    list->SetComputeRootShaderResourceView(2, st.scene.tlas);
    auto& in=st.waterIn[parity];
    auto& out=st.waterOut[parity];
    auto& before=st.waterOut[1-parity];
    // The shared textures are in COMMON between frames and return there, as in trace().
    const std::array<ID3D12Resource*, 7> reads={in.depth.t.d12.Get(), in.nrm.t.d12.Get(), st.inputs[st.waterBase.parity].exposure.t.d12.Get(), st.sky.t.d12.Get(),
                                                st.atlas.t.d12.Get(), st.alphaAtlas.t.d12.Get(), st.cookieAtlas.t.d12.Get()};
    const std::array<ID3D12Resource*, 2> writes={out.t.d12.Get(), before.t.d12.Get()};
    Barriers b;
    for(auto* r:reads) b.transition_from(r, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    for(auto* r:writes) b.transition_from(r, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    b.flush(list);
    // The previous water frame's reflections are this one's history only if it traced the frame before this one.
    const auto& prev=st.waterLatest;
    const bool continues=prev.valid && prev.parity!=parity && prev.frameIndex+1==st.waterBase.frame.frameIndex &&
        prev.width==st.width && prev.height==st.height;
    dispatch(list, st.water.Get(), {in.depth.srv, in.nrm.srv, kInvalid, out.uav, before.uav, st.waterHist[parity].uav, st.waterHist[1-parity].uav,
                                    continues ? 1u : 0u}, b);
    for(auto* r:reads) b.transition_from(r, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    for(auto* r:writes) b.transition_from(r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    b.flush(list);
    list->EndQuery(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kWaterQueries+index*2+1);
    list->ResolveQueryData(st.queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, kWaterQueries+index*2, 2, st.queryReadback.resource.Get(), uint64_t(kWaterQueries+index*2)*8);
    st.waterQueried[index]=true;
    check(list->Close(), "Close water reflections list");
    ID3D12CommandList* lists[]={list};
    st.queue->ExecuteCommandLists(1, lists);
    st.recording=false;
    st.waterRetired[index]=retire;
    return retire;
}

// Traced reflections on water (TGPT_SetWater, render event 4), right after HDRP's transparent SSR step: the previous water
// frame's traced reflections into HDRP's reflections (d3d11.hlsl TGPT_WATER), then this frame's water surfaces to D3D12,
// whose reflections trace_water records after this frame's own work, for the next frame's event. Like the composite it
// applies light traced a frame ago, which has had that frame to finish: D3D11 never waits for D3D12 work of its own frame.
void water_event(uint32_t sequence) {
    Engine::Water request;
    {
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        auto& slot=e.water[sequence%e.water.size()];
        if(!slot.pending || slot.sequence!=sequence) return;
        request=slot; slot.pending=false;
    }
    // Only for the frame event 1 traced: its constants and input set are the ones the water's reflections need.
    if(st.broken || !st.d3d11Ready || !st.d3d12Ready || !st.waterBase.valid || st.waterBase.sequence!=sequence || !st.scene.tlas) return;
    const TGPT_Frame& frame=st.waterBase.frame;
    const uint32_t w=frame.renderWidth, h=frame.renderHeight;
    if(w!=st.width || h!=st.height) return;
    tgb::InternalScope internal(&host());
    const uint32_t parity=st.waterParity;
    bool applied=false, traced=false;
    try {
        auto* reflections=reinterpret_cast<ID3D11Resource*>(uintptr_t(request.reflections));
        auto* depth=reinterpret_cast<ID3D11Resource*>(uintptr_t(request.depth));
        auto* normal=reinterpret_cast<ID3D11Resource*>(uintptr_t(request.normal));
        if(!on_game_device(reflections) || !on_game_device(depth) || !on_game_device(normal)) {
            log_throttled("HDRP's reflections for water are not on the game's device; water keeps HDRP's reflections"); return;
        }
        const auto rd=texture_desc(reflections), dd=texture_desc(depth), nd=texture_desc(normal);
        // HDRP hands out its black default texture when its SSR has no history (a camera cut): smaller than the frame.
        if(rd.Width<w || rd.Height<h || dd.Width<w || dd.Height<h || nd.Width<w || nd.Height<h || rd.ArraySize!=1 || rd.SampleDesc.Count!=1) return;
        const auto uavFormat=tgb::typed_write_format(rd.Format);
        const auto readFormat=tgb::typed_read_format(rd.Format, tgb::ReadAs::Color);
        if(!(rd.BindFlags&D3D11_BIND_UNORDERED_ACCESS) || uavFormat==DXGI_FORMAT_UNKNOWN || readFormat==DXGI_FORMAT_UNKNOWN) {
            log_throttled("HDRP's reflections for water can't be written (format "+std::to_string(rd.Format)+"); water keeps HDRP's reflections"); return;
        }
        const DXGI_FORMAT sv=stencil_view(dd.Format);
        if(sv==DXGI_FORMAT_UNKNOWN) { log_throttled("HDRP's stencil can't be read (depth format "+std::to_string(dd.Format)+"); water keeps HDRP's reflections"); return; }
        st.heap.set_retire(st.consumer.value+1);
        ensure_water_textures(w, h);
        {
            tgb::ContextScope scope(st.c11.Get(), st.scratch.Get());
            // D3D12 must be done with the water frame that last read this input set (two water frames ago).
            const uint64_t busy=st.waterInRetire[parity];
            if(busy && st.consumer.d12->GetCompletedValue()<busy) check(st.c11->Wait(st.consumer.d11.Get(), busy), "D3D11 wait before reusing a water input set");
            const UINT gx=(w+7)/8, gy=(h+7)/8;
            D11Constants c{}; c.c0[0]=w; c.c0[1]=h; c.c0[2]=kTraceReflectionRay;
            set_constants(c);
            D3D11_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=sv; s.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D; s.Texture2D.MipLevels=1;
            ComPtr<ID3D11ShaderResourceView> stencilSrv; check(st.d11->CreateShaderResourceView(depth, &s, &stencilSrv), "Create the water's stencil view");
            run_cs(st.csWaterDepth.Get(), {tgb::typed_srv(st.d11.Get(), depth, tgb::ReadAs::Depth).Get(), stencilSrv.Get()}, st.waterIn[parity].depth.t.uav.Get(), gx, gy);
            c.c0[2]=0; set_constants(c);
            run_cs(st.csFloat4.Get(), {tgb::typed_srv(st.d11.Get(), normal, tgb::ReadAs::Color).Get()}, st.waterIn[parity].nrm.t.uav.Get(), gx, gy);
            // The previous water frame's reflections, if it traced the frame before this one.
            const auto& prev=st.waterLatest;
            if(prev.valid && prev.parity!=parity && prev.width==w && prev.height==h && prev.frameIndex+1==frame.frameIndex) {
                if(st.consumer.d12->GetCompletedValue()<prev.retire) check(st.c11->Wait(st.consumer.d11.Get(), prev.retire), "D3D11 wait for the traced water reflections");
                WaterConstants wc{};
                std::memcpy(wc.invViewProj, frame.invViewProj, sizeof(wc.invViewProj));
                std::memcpy(wc.tracedViewProj, prev.viewProj, sizeof(wc.tracedViewProj)); std::memcpy(wc.tracedInvViewProj, prev.invViewProj, sizeof(wc.tracedInvViewProj));
                for(int i=0;i<3;++i) wc.cameraDelta[i]=float(double(frame.cameraPosition[i])-prev.camera[i]);
                wc.cameraDelta[3]=1.f/std::max(request.multiplier, 1e-3f);
                wc.frameSize[0]=w; wc.frameSize[1]=h;
                st.c11->UpdateSubresource(st.waterConstants.Get(), 0, nullptr, &wc, 0, 0);
                D3D11_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=uavFormat; u.ViewDimension=D3D11_UAV_DIMENSION_TEXTURE2D;
                ComPtr<ID3D11UnorderedAccessView> uav; check(st.d11->CreateUnorderedAccessView(reflections, &u, &uav), "Create the view HDRP's reflections are written through");
                // HDRP's reflections themselves are only written: the traced reflection replaces them (t1 unused).
                ID3D11ShaderResourceView* srvs[4]={st.waterIn[parity].depth.t.srv.Get(), nullptr, st.waterOut[prev.parity].t.srv.Get(), st.waterIn[prev.parity].depth.t.srv.Get()};
                ID3D11Buffer* cbs[2]={st.constants.Get(), st.waterConstants.Get()};
                st.c11->CSSetShader(st.csWater.Get(), nullptr, 0); st.c11->CSSetConstantBuffers(0, 2, cbs);
                st.c11->CSSetShaderResources(0, 4, srvs);
                auto* target=uav.Get(); st.c11->CSSetUnorderedAccessViews(0, 1, &target, nullptr);
                st.c11->Dispatch(gx, gy, 1);
                ID3D11ShaderResourceView* noSrvs[4]{}; st.c11->CSSetShaderResources(0, 4, noSrvs);
                ID3D11UnorderedAccessView* noUav=nullptr; st.c11->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
                ID3D11Buffer* noCbs[2]{}; st.c11->CSSetConstantBuffers(0, 2, noCbs);
                applied=true;
            }
        }
        // This frame's water surfaces to D3D12 (flushed, so their reflections are traced while HDRP finishes the frame).
        const uint64_t producedValue=st.producer.next();
        check(st.c11->Signal(st.producer.d11.Get(), producedValue), "Signal water inputs");
        st.c11->Flush();
        check(st.queue->Wait(st.producer.d12.Get(), producedValue), "D3D12 wait for water inputs");
        const uint64_t retire=trace_water(parity);
        const HRESULT signalled=st.queue->Signal(st.consumer.d12.Get(), retire);
        if(FAILED(signalled)) { st.broken=true; throw DxError(signalled, "D3D12 signal after the water reflections"); }
        st.consumer.value=retire;
        st.waterInRetire[parity]=retire;
        // The frame's exposure (its input set) is read too.
        st.inputsRetire[st.waterBase.parity]=std::max(st.inputsRetire[st.waterBase.parity], retire);
        auto& latest=st.waterLatest;
        latest.valid=true; latest.retire=retire; latest.parity=parity; latest.width=w; latest.height=h; latest.frameIndex=frame.frameIndex;
        for(int i=0;i<3;++i) latest.camera[i]=frame.cameraPosition[i];
        std::memcpy(latest.viewProj, frame.viewProj, sizeof(latest.viewProj)); std::memcpy(latest.invViewProj, frame.invViewProj, sizeof(latest.invViewProj));
        st.waterParity=1-parity;
        traced=true;
        if(!st.waterLogged) {
            st.waterLogged=true;
            log("Water reflections: traced for HDRP's water and transparent surfaces from its transparent SSR step (its reflections "+
                std::to_string(rd.Width)+"x"+std::to_string(rd.Height)+" format "+std::to_string(rd.Format)+", depth format "+std::to_string(dd.Format)+")");
        }
    } catch(const DxError& e) { log_throttled(std::string("Water reflections failed: ")+e.what()+" "+tgb::hexhr(e.hr)); }
      catch(const std::exception& e) { log_throttled(std::string("Water reflections failed: ")+e.what()); }
    if(st.recording) {
        // As in trace_event: a list that failed while it was being recorded leaves the bookkeeping behind it.
        st.recording=false; st.broken=true;
        log("A water reflections frame failed while it was being recorded; path tracing stays off for the rest of this session");
    }
    auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
    if(traced) ++e.status.waterTraced;
    if(applied) ++e.status.waterApplied;
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
    // The passes read their textures back through typed UAVs: half and single precision color formats (the additional
    // formats a driver supports together) and the packed surfaces as R32G32_UINT, which a driver supports on its own.
    if(reason.empty() && !o.TypedUAVLoadAdditionalFormats)
        reason="The GPU driver can't load the tracer's textures through typed UAVs (no TypedUAVLoadAdditionalFormats).";
    if(reason.empty()) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{DXGI_FORMAT_R32G32_UINT};
        if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))) || !(fs.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD))
            reason="The GPU driver can't load R32G32_UINT textures through typed UAVs, which the tracer keeps its surfaces in.";
    }
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
    }
    st.sharedDevice=device;
    uint32_t denoisers=0;
    if(reason.empty()) {
        rrg_probe(device, vendor);
        nrd_probe(device);
        denoisers=(rrg_available() ? TGPT_FEATURE_RAY_REGENERATION : 0u)|(nrd_available() ? TGPT_FEATURE_NRD : 0u);
    }
    {
        // The device is ready once its denoisers are probed, and the two are published together: the managed side takes a
        // ready device's features as final, and a Denoising pick of NRD or Ray Regeneration was replaced mid-probe.
        auto& e=engine(); std::lock_guard<std::mutex> lock(e.mutex);
        e.status.features|=denoisers;
        e.status.deviceReady=1; e.status.supported=reason.empty() ? 1u : 0u;
    }
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
        else if(eventId==TGPT_EVENT_SHADOW_DETAIL) shadow_detail_event(sequence);
        else if(eventId==TGPT_EVENT_WATER) water_event(sequence);
    } catch(...) { log_throttled("Path tracing event failed with an exception; the frame keeps HDRP's lighting"); }
}
} // namespace tgpt
