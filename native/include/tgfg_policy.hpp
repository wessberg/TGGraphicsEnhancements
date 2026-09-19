// SPDX-License-Identifier: MIT
#pragma once
#include "tgfg_api.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace tgfg {
enum class Reason : uint32_t {
    Ready=0, NoClient=1, WaitingBackend=2, UnsupportedAPI=3, Suspended=4, UserOff=5,
    CapsUnknown=6, MultiplierUnsupported=7, DynamicUnsupported=8, UnsupportedFormat=9,
    NativeError=10, OptionsAccepted=11, InputDispatched=12, ClientTimeout=13,
    MissingRenderEvent=14, NoDlssInputs=15, StaleFrame=16, MultipleDlssViews=17,
    UnsupportedQueue=18, InputContractInvalid=19, ExistingStreamline=20,
    RuntimeNotSupported=21, CameraInvalid=22, PresentFailed=23,
    BridgeNotCreated=24, InteropFailure=25, UnsupportedColorSpace=26,
    SourceDeviceMismatch=27, UnsupportedSourceLayout=28, BridgeReady=29, VsyncUnsupported=30
};
inline bool valid(const TGFG_Request& r) {
    return r.size==sizeof(r) && r.abi==TGFG_ABI && r.mode<=2 &&
        r.fixedMultiplier>=2 && r.fixedMultiplier<=6 &&
        std::isfinite(r.dynamicTargetFps) && r.dynamicTargetFps>=0 && r.dynamicTargetFps<=1000 &&
        std::isfinite(r.reflexBaseFpsLimit) && r.reflexBaseFpsLimit>=0 && r.reflexBaseFpsLimit<=1000 &&
        r.reflexMode<=2;
}
inline bool valid(const TGFG_Camera& c) {
    if(c.size!=sizeof(c) || c.abi!=TGFG_ABI || c.width==0 || c.height==0 || c.width>32768 || c.height>32768) return false;
    const float* blocks[]={c.viewToClip,c.clipToView,c.clipToPrev,c.prevToClip};
    for(auto p:blocks) for(int i=0;i<16;++i) if(!std::isfinite(p[i])) return false;
    const float* vectors[]={c.position,c.up,c.right,c.forward};
    for(auto p:vectors) for(int i=0;i<3;++i) if(!std::isfinite(p[i])) return false;
    return std::isfinite(c.nearPlane) && std::isfinite(c.farPlane) &&
        std::isfinite(c.fovRadians) && std::isfinite(c.aspect) &&
        c.nearPlane>0 && c.farPlane>c.nearPlane && c.fovRadians>0 && c.fovRadians<3.141593f && c.aspect>0;
}
struct Readiness {
    bool sdk=false, caps=false, renderFrame=false, inputs=false, inputContract=false;
    uint32_t ageMs=0, maxGenerated=0, dynamicSupported=0, format=0, evalCount=0;
    bool fatal=false, dx11Bridge=false;
};
inline Reason eligibility(const TGFG_Request& r, const Readiness& s) {
    if(!valid(r)) return Reason::NoClient;
    if(r.mode==0) return Reason::UserOff;
    if(r.sourceGraphicsApi!=11) return Reason::UnsupportedAPI;
    if(!s.dx11Bridge) return Reason::BridgeNotCreated;
    if(s.ageMs>1000) return Reason::ClientTimeout;
    if(r.suspensionMask) return Reason::Suspended;
    if(s.fatal) return Reason::NativeError;
    if(!s.sdk) return Reason::WaitingBackend;
    if(!s.caps) return Reason::CapsUnknown;
    if(s.format==10 || s.format==0) return Reason::UnsupportedFormat; // scRGB/FP16 or unknown
    if(r.fixedMultiplier-1>s.maxGenerated) return Reason::MultiplierUnsupported; // fixed multiplier or dynamic upper bound
    if(r.mode==2 && !s.dynamicSupported) return Reason::DynamicUnsupported;
    if(!s.renderFrame) return Reason::MissingRenderEvent;
    if(s.evalCount>1) return Reason::MultipleDlssViews;
    if(s.evalCount==0) return Reason::NoDlssInputs;
    if(!s.inputContract) return Reason::InputContractInvalid;
    if(!s.inputs) return Reason::NoDlssInputs;
    return Reason::Ready;
}
inline uint32_t frame_limit_us(float fps) {
    return fps>0 && std::isfinite(fps) ? static_cast<uint32_t>(std::lround(1000000.0/std::max(1.0f,fps))) : 0;
}
inline bool supported_present_format(uint32_t f) {
    return f==24 || f==28 || f==29 || f==87 || f==91; // RGB10A2/RGBA8/BGRA8 (+sRGB views)
}
} // namespace tgfg
