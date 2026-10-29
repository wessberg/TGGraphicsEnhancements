// SPDX-License-Identifier: MIT
#include "dx11_bridge.hpp"
#include <tgb/dlss_inputs.hpp>
#include <unordered_map>
#include <cstring>
#include <cmath>
#include <type_traits>
#include <array>
namespace tgfg {
namespace {
sl::float4x4 matrix(const float* p) {sl::float4x4 m;for(unsigned r=0;r<4;++r)m[r]={p[r*4],p[r*4+1],p[r*4+2],p[r*4+3]};return m;}
sl::float3 vector(const float* p) {return {p[0],p[1],p[2]};}
void capture_problem(Reason reason,const char* message) {
    auto& s=state();s.status.reason=uint32_t(reason);
    static std::string previous;static uint64_t last=0;
    if(previous!=message || GetTickCount64()-last>=5000){previous=message;last=GetTickCount64();log(message);}
}
struct Capture {bool pending=false;uint32_t id=0;};
Capture capture(const tgb::DlssInputs& in) {
    auto* context=in.context;
    auto& s=state();std::lock_guard<std::recursive_mutex> lock(s.mutex);++s.status.capturedNgxCalls;
    if(!s.deviceReady || !(s.status.bridgeFlags&1)){capture_problem(Reason::BridgeNotCreated,"D3D11 NGX observed, but the presentation bridge is absent");return {};}
    ++s.diag.ngxEvals;
    if(!haveRenderFrame){++s.diag.ngxNoFrameId;capture_problem(Reason::MissingRenderEvent,"NGX thread has no Unity render-event frame identity");return {};}
    auto* frame=s.frames.find(renderFrameId);
    if(!frame || !frame->cameraReady){++s.diag.ngxNoCamera;capture_problem(Reason::MissingRenderEvent,"NGX evaluation in a frame without a gameplay camera packet (menu, loading, or camera not matched)");return {};}
    if(!frame->renderBegun){++s.diag.ngxNotBegun;capture_problem(Reason::MissingRenderEvent,"NGX evaluation before the gameplay camera began rendering");return {};}
    if(frame->renderEnded){++s.diag.ngxAfterEnd;capture_problem(Reason::MissingRenderEvent,"NGX evaluation after the gameplay camera finished (another DLSS view)");return {};}
    ++frame->evalCount;
    if(frame->evalCount!=1){++s.diag.ngxMulti;frame->inputs=false;capture_problem(Reason::MultipleDlssViews,"Multiple DLSS evaluations in one camera interval; refusing ambiguous input selection");return {};}
    const auto& camera=frame->camera;
    const auto outWidth=in.outputRect.width,outHeight=in.outputRect.height;
    if(outWidth!=camera.width || outHeight!=camera.height){++s.diag.ngxDims;capture_problem(Reason::CameraInvalid,"DLSS output dimensions differ from the managed camera output");return {};}
    s.ngxFlags=in.flags;
    auto* depth=in.depth; auto* motion=in.motion;
    const auto& d=in.depthRect; const auto& m=in.motionRect;
    const RectU de{d.x,d.y,d.width,d.height},me{m.x,m.y,m.width,m.height};
    const float sx=in.motionScaleX,sy=in.motionScaleY,jx=in.jitterX,jy=in.jitterY;
    if(!in.valid) { ++s.diag.ngxParams; capture_problem(Reason::InputContractInvalid,"DLSS input packet is missing or invalid"); return {}; }
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
    constants.depthInverted=(in.flags&NVSDK_NGX_DLSS_Feature_Flags_DepthInverted)?sl::eTrue:sl::eFalse;
    constants.cameraMotionIncluded=sl::eTrue;constants.motionVectors3D=sl::eFalse;constants.motionVectorsDilated=sl::eFalse;
    constants.motionVectorsJittered=(in.flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered)?sl::eTrue:sl::eFalse;
    constants.reset=(camera.resetHistory||in.reset||!s.status.lastAcceptedMode)?sl::eTrue:sl::eFalse;
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
thread_local Capture pending;
void __cdecl observe(tgb::DlssStage stage,const tgb::DlssInputs* in) noexcept {
    if(!in || insideSL) return;
    try {
        if(stage==tgb::DlssStage::Created) {
            auto& s=state(); std::lock_guard<std::recursive_mutex> lock(s.mutex); s.status.flags|=16;
            log("Observed shared DLSS SR feature: render="+std::to_string(in->colorRect.width)+"x"+std::to_string(in->colorRect.height)+
                " output="+std::to_string(in->outputRect.width)+"x"+std::to_string(in->outputRect.height)+" flags="+std::to_string(in->flags));
        } else if(stage==tgb::DlssStage::Before) pending=capture(*in);
        else if(stage==tgb::DlssStage::After) { finish(pending,in->success); pending={}; }
    } catch(const std::exception& e) { log(e.what()); } catch(...) { }
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
    return tgb::subscribe_dlss(&observe);
}
} // namespace tgfg
