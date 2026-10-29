// SPDX-License-Identifier: MIT
// Observations of the actual SR/RR invocation. Borrowed resources and parameters are valid only
// during the callback. The bridge owns discovery, so consumers do not need FG installed.
#pragma once
#include "bridge_api.hpp"
#include <cmath>
namespace tgb {
enum class DlssStage : uint32_t { Created, Before, After, Released };
enum class DlssSource : uint32_t { SuperResolution=1, RayReconstruction=2 };
struct DlssRect { uint32_t x=0,y=0,width=0,height=0; };
struct DlssInputs {
    uint32_t size=sizeof(DlssInputs);
    DlssSource source=DlssSource::SuperResolution;
    ID3D11DeviceContext* context=nullptr;
    const void* feature=nullptr;
    const void* parameters=nullptr; // NVSDK_NGX_Parameter, for diagnostics only
    ID3D11Resource *color=nullptr,*depth=nullptr,*motion=nullptr,*output=nullptr;
    DlssRect colorRect{},depthRect{},motionRect{},outputRect{};
    uint32_t flags=0,reset=0;
    float motionScaleX=0,motionScaleY=0,jitterX=0,jitterY=0;
    bool valid=false,success=false;
};
using DlssObserver=void (__cdecl*)(DlssStage,const DlssInputs*) noexcept;
using SubscribeDlss=bool (__cdecl*)(DlssObserver);
using PublishDlss=void (__cdecl*)(DlssStage,const DlssInputs*);
inline bool subscribe_dlss(DlssObserver callback) {
    const auto module=GetModuleHandleW(L"TGGraphicsBridge.Native.dll");
    const auto subscribe=reinterpret_cast<SubscribeDlss>(GetProcAddress(module,"TGGraphicsBridge_SubscribeDlss"));
    return subscribe && subscribe(callback);
}
// BU publishes its selected D3D11 resources at the RR handoff, before transfer to the shared D3D12
// device. These are the resources RR actually uses, including any future PT input replacement.
inline void publish_dlss(DlssStage stage,const DlssInputs& inputs) {
    static const auto publish=reinterpret_cast<PublishDlss>(GetProcAddress(GetModuleHandleW(L"TGGraphicsBridge.Native.dll"),"TGGraphicsBridge_PublishDlss"));
    if(publish) publish(stage,&inputs);
}
inline bool valid_dlss_inputs(const DlssInputs& in) {
    const auto rect=[](DlssRect r) { return r.width && r.height && r.width<=16384 && r.height<=16384 && r.x<=16384-r.width && r.y<=16384-r.height; };
    return in.size==sizeof(in) && in.context && in.color && in.depth && in.motion && in.output &&
        rect(in.colorRect) && rect(in.depthRect) && rect(in.motionRect) && rect(in.outputRect) &&
        in.colorRect.width==in.depthRect.width && in.colorRect.height==in.depthRect.height &&
        std::isfinite(in.motionScaleX) && std::isfinite(in.motionScaleY) && in.motionScaleX!=0 && in.motionScaleY!=0 &&
        std::isfinite(in.jitterX) && std::isfinite(in.jitterY);
}
} // namespace tgb
