// SPDX-License-Identifier: MIT
#include "dx11_bridge.hpp"
#include <unordered_map>
#include <cstring>
#include <cmath>
#include <type_traits>
#include <array>
namespace tgfg {
namespace {
using CreateFn=decltype(&NVSDK_NGX_D3D11_CreateFeature);
using EvalFn=decltype(&NVSDK_NGX_D3D11_EvaluateFeature);
using EvalCFn=decltype(&NVSDK_NGX_D3D11_EvaluateFeature_C);
using ReleaseFn=decltype(&NVSDK_NGX_D3D11_ReleaseFeature);
struct Provider {
    HMODULE module=nullptr;
    std::atomic<CreateFn> create{nullptr};std::atomic<EvalFn> evaluate{nullptr};
    std::atomic<EvalCFn> evaluateC{nullptr};std::atomic<ReleaseFn> release{nullptr};
};
constexpr size_t providerCount=8;
std::array<Provider,providerCount> providers;
std::mutex providerMutex;
struct Feature {uint32_t flags=0,width=0,height=0,outWidth=0,outHeight=0;};
std::array<std::unordered_map<const NVSDK_NGX_Handle*,Feature>,providerCount> features;
thread_local bool inNgx=false;
struct Scope{bool previous=inNgx;Scope(){inNgx=true;}~Scope(){inNgx=previous;}};
decltype(&GetProcAddress) originalGetProcAddress=nullptr;
template<class T>bool read(const NVSDK_NGX_Parameter* p,const char* key,T& out){return p&&NVSDK_NGX_SUCCEED(p->Get(key,&out));}
uint32_t uint_value(const NVSDK_NGX_Parameter* p,const char* key,uint32_t fallback=0) {
    unsigned int u=0;if(read(p,key,u))return u;int v=0;return read(p,key,v)&&v>=0?uint32_t(v):fallback;
}
sl::float4x4 matrix(const float* p) {sl::float4x4 m;for(unsigned r=0;r<4;++r)m[r]={p[r*4],p[r*4+1],p[r*4+2],p[r*4+3]};return m;}
sl::float3 vector(const float* p) {return {p[0],p[1],p[2]};}
void capture_problem(Reason reason,const char* message) {
    auto& s=state();s.status.reason=uint32_t(reason);
    static std::string previous;static uint64_t last=0;
    if(previous!=message || GetTickCount64()-last>=5000){previous=message;last=GetTickCount64();log(message);}
}
void created(size_t provider,NVSDK_NGX_Feature feature,const NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle* handle) {
    if(feature!=NVSDK_NGX_Feature_SuperSampling)return;
    auto& s=state();std::lock_guard<std::recursive_mutex> lock(s.mutex);
    Feature f;f.flags=uint_value(p,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags);
    f.width=uint_value(p,NVSDK_NGX_Parameter_Width);f.height=uint_value(p,NVSDK_NGX_Parameter_Height);
    f.outWidth=uint_value(p,NVSDK_NGX_Parameter_OutWidth);f.outHeight=uint_value(p,NVSDK_NGX_Parameter_OutHeight);
    features[provider][handle]=f;s.status.flags|=16;
    log("Observed D3D11 DLSS SR feature; provider="+std::to_string(provider)+" render="+std::to_string(f.width)+"x"+std::to_string(f.height)+
        " output="+std::to_string(f.outWidth)+"x"+std::to_string(f.outHeight)+" flags="+std::to_string(f.flags));
}
struct Capture {bool pending=false;uint32_t id=0;};
Capture capture(size_t provider,ID3D11DeviceContext* context,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* p) {
    auto& s=state();std::lock_guard<std::recursive_mutex> lock(s.mutex);++s.status.capturedNgxCalls;
    const auto found=features[provider].find(handle);
    if(found==features[provider].end())return {};
    if(!s.deviceReady || !(s.status.bridgeFlags&1)){capture_problem(Reason::BridgeNotCreated,"D3D11 NGX observed, but the presentation bridge is absent");return {};}
    ++s.diag.ngxEvals;
    if(!haveRenderFrame){++s.diag.ngxNoFrameId;capture_problem(Reason::MissingRenderEvent,"NGX thread has no Unity render-event frame identity");return {};}
    auto* frame=s.frames.find(renderFrameId);
    if(!frame || !frame->cameraReady){++s.diag.ngxNoCamera;capture_problem(Reason::MissingRenderEvent,"NGX evaluation in a frame without a gameplay camera packet (menu, loading, or camera not matched)");return {};}
    if(!frame->renderBegun){++s.diag.ngxNotBegun;capture_problem(Reason::MissingRenderEvent,"NGX evaluation before the gameplay camera began rendering");return {};}
    if(frame->renderEnded){++s.diag.ngxAfterEnd;capture_problem(Reason::MissingRenderEvent,"NGX evaluation after the gameplay camera finished (another DLSS view)");return {};}
    ++frame->evalCount;
    if(frame->evalCount!=1){++s.diag.ngxMulti;frame->inputs=false;capture_problem(Reason::MultipleDlssViews,"Multiple DLSS evaluations in one camera interval; refusing ambiguous input selection");return {};}
    const auto& f=found->second;const auto& camera=frame->camera;
    if(f.outWidth!=camera.width || f.outHeight!=camera.height){++s.diag.ngxDims;capture_problem(Reason::CameraInvalid,"DLSS output dimensions differ from the managed camera output");return {};}
    s.ngxFlags=f.flags; // FSR/XeSS describe depth and motion vectors from the gameplay camera's DLSS feature
    ID3D11Resource *depth=nullptr,*motion=nullptr;
    if(!read(p,NVSDK_NGX_Parameter_Depth,depth)||!read(p,NVSDK_NGX_Parameter_MotionVectors,motion)||!depth||!motion){++s.diag.ngxParams;capture_problem(Reason::InputContractInvalid,"D3D11 DLSS depth/motion pointers are missing");return {};}
    const auto rw=uint_value(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,f.width);
    const auto rh=uint_value(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,f.height);
    const bool lowres=(f.flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)!=0;
    RectU de{uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X),uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y),rw,rh};
    RectU me{uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X),uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y),lowres?rw:f.outWidth,lowres?rh:f.outHeight};
    float sx=0,sy=0,jx=0,jy=0;
    if(!de.width||!de.height||!me.width||!me.height ||
       !read(p,NVSDK_NGX_Parameter_MV_Scale_X,sx)||!read(p,NVSDK_NGX_Parameter_MV_Scale_Y,sy)||
       !read(p,NVSDK_NGX_Parameter_Jitter_Offset_X,jx)||!read(p,NVSDK_NGX_Parameter_Jitter_Offset_Y,jy)||
       !std::isfinite(sx)||!std::isfinite(sy)||!std::isfinite(jx)||!std::isfinite(jy)||sx==0||sy==0) {
        ++s.diag.ngxParams;capture_problem(Reason::InputContractInvalid,"DLSS subrect, motion scale or jitter parameters are missing/invalid");return {};
    }
    frame->inputContract=true;s.status.flags|=32u;
    if(!valid(s.request)||s.request.mode==0||s.request.sourceGraphicsApi!=11||s.request.suspensionMask||s.fatal||GetTickCount64()-s.heartbeat>1000){++s.diag.ngxRequestOff;return {};}
    sl::Constants constants{};
    constants.cameraViewToClip=matrix(camera.viewToClip);constants.clipToCameraView=matrix(camera.clipToView);
    constants.clipToPrevClip=matrix(camera.clipToPrev);constants.prevClipToClip=matrix(camera.prevToClip);
    constants.cameraPos=vector(camera.position);constants.cameraUp=vector(camera.up);constants.cameraRight=vector(camera.right);constants.cameraFwd=vector(camera.forward);
    constants.cameraNear=camera.nearPlane;constants.cameraFar=camera.farPlane;constants.cameraFOV=camera.fovRadians;constants.cameraAspectRatio=camera.aspect;
    constants.jitterOffset={jx,jy};constants.mvecScale={sx/me.width,sy/me.height};
    // Optional in the SDK, but left at the INVALID_FLOAT sentinel otherwise.
    constants.cameraPinholeOffset={0.f,0.f};
    {const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};constants.clipToLensClip=matrix(identity);}
    constants.depthInverted=(f.flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)?sl::eTrue:sl::eFalse;
    constants.cameraMotionIncluded=sl::eTrue;constants.motionVectors3D=sl::eFalse;constants.motionVectorsDilated=sl::eFalse;
    constants.motionVectorsJittered=(f.flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)?sl::eTrue:sl::eFalse;
    constants.reset=(camera.resetHistory||uint_value(p,NVSDK_NGX_Parameter_Reset)||!s.status.lastAcceptedMode)?sl::eTrue:sl::eFalse;
    SLGuard own;
    if(!capture_dx11_inputs(renderFrameId,context,depth,motion,de,me,constants)){++s.diag.captureFailed;return {};}
    ++s.diag.captureOk;s.lastUpscalerFrame=renderFrameId;s.upscalerSeen=true;
    return {true,renderFrameId};
}
void finish(Capture capture,bool success) {
    if(!capture.pending)return;
    auto& s=state();std::lock_guard<std::recursive_mutex> lock(s.mutex);finish_dx11_capture(capture.id,success);
    if(auto* f=s.frames.find(capture.id)) {
        f->inputs=success;
        if(success){++s.status.successfulInputDispatches;s.status.reason=uint32_t(Reason::InputDispatched);}
    }
    if(!success) ++s.diag.evalFailed;
}
template<size_t I> NVSDK_NGX_Result NVSDK_CONV create_hook(ID3D11DeviceContext* c,NVSDK_NGX_Feature f,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h) {
    const bool inspect=!insideSL&&!inNgx;Scope nested;const auto result=providers[I].create.load()(c,f,p,h);
    if(inspect&&NVSDK_NGX_SUCCEED(result)&&h&&*h){try{created(I,f,p,*h);}catch(const std::exception& e){log(e.what());}}return result;
}
template<size_t I> NVSDK_NGX_Result NVSDK_CONV eval_hook(ID3D11DeviceContext* c,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb) {
    const bool inspect=!insideSL&&!inNgx;Scope nested;Capture input;
    if(inspect){try{input=capture(I,c,h,p);}catch(const std::exception& e){log(e.what());}}
    const auto result=providers[I].evaluate.load()(c,h,p,cb);
    if(inspect)finish(input,NVSDK_NGX_SUCCEED(result));return result;
}
template<size_t I> NVSDK_NGX_Result NVSDK_CONV eval_c_hook(ID3D11DeviceContext* c,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback_C cb) {
    const bool inspect=!insideSL&&!inNgx;Scope nested;Capture input;
    if(inspect){try{input=capture(I,c,h,p);}catch(const std::exception& e){log(e.what());}}
    const auto result=providers[I].evaluateC.load()(c,h,p,cb);
    if(inspect)finish(input,NVSDK_NGX_SUCCEED(result));return result;
}
template<size_t I> NVSDK_NGX_Result NVSDK_CONV release_hook(NVSDK_NGX_Handle* h) {
    const bool inspect=!insideSL&&!inNgx;Scope nested;
    if(inspect){std::lock_guard<std::recursive_mutex> lock(state().mutex);features[I].erase(h);}
    return providers[I].release.load()(h);
}
static_assert(std::is_same_v<CreateFn,decltype(&create_hook<0>)>);
static_assert(std::is_same_v<EvalFn,decltype(&eval_hook<0>)>);
static_assert(std::is_same_v<EvalCFn,decltype(&eval_c_hook<0>)>);
static_assert(std::is_same_v<ReleaseFn,decltype(&release_hook<0>)>);
#define THUNKS(name) name<0>,name<1>,name<2>,name<3>,name<4>,name<5>,name<6>,name<7>
const CreateFn createThunks[]={THUNKS(create_hook)};
const EvalFn evalThunks[]={THUNKS(eval_hook)};
const EvalCFn evalCThunks[]={THUNKS(eval_c_hook)};
const ReleaseFn releaseThunks[]={THUNKS(release_hook)};
#undef THUNKS
template<class Fn>FARPROC redirect(FARPROC p,std::atomic<Fn>& original,Fn shim) {
    Fn empty=nullptr;const auto target=reinterpret_cast<Fn>(p);
    if(original.compare_exchange_strong(empty,target)||original.load()==target)return reinterpret_cast<FARPROC>(shim);
    return p; // provider reloaded/changed: never pass its handles to an old target
}
FARPROC WINAPI get_proc_hook(HMODULE module,LPCSTR name) {
    const auto address=originalGetProcAddress(module,name);
    if(!address||insideSL||reinterpret_cast<uintptr_t>(name)<=0xffff)return address;
    const bool create=!std::strcmp(name,"NVSDK_NGX_D3D11_CreateFeature");
    const bool eval=!std::strcmp(name,"NVSDK_NGX_D3D11_EvaluateFeature");
    const bool evalC=!std::strcmp(name,"NVSDK_NGX_D3D11_EvaluateFeature_C");
    const bool release=!std::strcmp(name,"NVSDK_NGX_D3D11_ReleaseFeature");
    if(!create&&!eval&&!evalC&&!release)return address;
    std::lock_guard<std::mutex> lock(providerMutex);size_t index=0;
    for(;index<providerCount;++index)if(providers[index].module==module)break;
    if(index==providerCount) {
        for(index=0;index<providerCount;++index)if(!providers[index].module)break;
        if(index==providerCount)return address;
        providers[index].module=module;
        log("Discovered D3D11 NGX provider slot "+std::to_string(index));
    }
    auto& p=providers[index];
    if(create)return redirect(address,p.create,createThunks[index]);if(eval)return redirect(address,p.evaluate,evalThunks[index]);
    if(evalC)return redirect(address,p.evaluateC,evalCThunks[index]);return redirect(address,p.release,releaseThunks[index]);
}
} // namespace
// HDRP's own depth and motion vectors, for frames the game renders without DLSS (TAA, STP, no
// anti-aliasing). The custom pass runs before post-processing, so before this frame's DLSS pass:
// while DLSS supplied the previous frame's inputs, it supplies this frame's too.
void capture_hdrp_inputs(Frame& frame) {
    auto& s=state();
    frame.hdrpPending=false;
    if(s.upscalerSeen && frame.id-s.lastUpscalerFrame<=1u){++s.diag.hdrpUpscaler;return;}
    if(!valid(s.request)||s.request.mode==0||s.request.sourceGraphicsApi!=11||s.request.suspensionMask||s.fatal||GetTickCount64()-s.heartbeat>1000)return;
    if(!s.deviceReady || !(s.status.bridgeFlags&1))return;
    const auto& in=frame.hdrp;const auto& camera=frame.camera;
    const RectU rect{0,0,in.renderWidth,in.renderHeight};
    sl::Constants constants{};
    constants.cameraViewToClip=matrix(camera.viewToClip);constants.clipToCameraView=matrix(camera.clipToView);
    constants.clipToPrevClip=matrix(camera.clipToPrev);constants.prevClipToClip=matrix(camera.prevToClip);
    constants.cameraPos=vector(camera.position);constants.cameraUp=vector(camera.up);constants.cameraRight=vector(camera.right);constants.cameraFwd=vector(camera.forward);
    constants.cameraNear=camera.nearPlane;constants.cameraFar=camera.farPlane;constants.cameraFOV=camera.fovRadians;constants.cameraAspectRatio=camera.aspect;
    // HDRP's DLSS pass gives NGX the negated TAA jitter and a motion vector scale of minus the render
    // size, so every frame generation technology gets the same values as from a DLSS frame.
    constants.jitterOffset={-in.jitterX,-in.jitterY};constants.mvecScale={-1.f,-1.f};
    constants.cameraPinholeOffset={0.f,0.f};
    {const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};constants.clipToLensClip=matrix(identity);}
    const bool reversed=(in.flags&1u)!=0;
    constants.depthInverted=reversed?sl::eTrue:sl::eFalse;
    constants.cameraMotionIncluded=sl::eTrue;constants.motionVectors3D=sl::eFalse;constants.motionVectorsDilated=sl::eFalse;
    constants.motionVectorsJittered=sl::eFalse;
    constants.reset=(camera.resetHistory||!s.status.lastAcceptedMode)?sl::eTrue:sl::eFalse;
    // How FSR and XeSS read depth and motion vectors: as from a DLSS feature with render-resolution vectors.
    constexpr uint32_t described=NVSDK_NGX_DLSS_Feature_Flags_DepthInverted|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    s.ngxFlags=(s.ngxFlags&~described)|(reversed?NVSDK_NGX_DLSS_Feature_Flags_DepthInverted:0u)|NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    frame.inputContract=true;s.status.flags|=32u;
    SLGuard own;
    if(!capture_dx11_inputs_immediate(frame.id,reinterpret_cast<ID3D11Resource*>(in.depth),reinterpret_cast<ID3D11Resource*>(in.motion),rect,rect,constants))
        {++s.diag.hdrpRejected;return;}
    finish_dx11_capture(frame.id,true);
    frame.inputs=true;frame.hdrpCaptured=true;++s.diag.hdrpCaptured;
    ++s.status.successfulInputDispatches;s.status.reason=uint32_t(Reason::InputDispatched);
}
bool install_ngx_discovery() {
    const auto p=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcAddress");
    return add_hook(reinterpret_cast<void*>(p),reinterpret_cast<void*>(&get_proc_hook),reinterpret_cast<void**>(&originalGetProcAddress));
}
} // namespace tgfg
