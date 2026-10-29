from pathlib import Path
import shutil
def write(path,s):
    p=Path(path); p.parent.mkdir(parents=True,exist_ok=True)
    if p.exists():
        b=Path('.testruns/nr/before-shared-ngx')/path; b.parent.mkdir(parents=True,exist_ok=True)
        if not b.exists(): shutil.copyfile(p,b)
    p.write_bytes(s.replace('\r\n','\n').replace('\n','\r\n').encode())
s=Path('native/framegen/src/ngx_capture.cpp').read_text()
discovery=s[s.index('using CreateFn='):s.index('sl::float4x4 matrix')]
discovery=discovery.replace('std::array<std::unordered_map<const NVSDK_NGX_Handle*,Feature>,providerCount> features;',
    'std::array<std::unordered_map<const NVSDK_NGX_Handle*,Feature>,providerCount> features;\nstd::mutex featureMutex;')
hooks=s[s.index('template<size_t I> NVSDK_NGX_Result'):s.index('} // namespace\n// HDRP')]
hooks=hooks.replace('!insideSL&&!inNgx','!internalCall&&!inNgx').replace('||insideSL||','||internalCall||')
hooks=hooks.replace('Scope nested;Capture input;','Scope nested;DlssInputs input;')
hooks=hooks.replace('input=capture(I,c,h,p);','input=observe(I,c,h,p); notify(DlssStage::Before,input);')
hooks=hooks.replace('if(inspect)finish(input,NVSDK_NGX_SUCCEED(result));',
    'if(inspect && input.feature) { input.success=NVSDK_NGX_SUCCEED(result); notify(DlssStage::After,input); }')
hooks=hooks.replace('std::lock_guard<std::recursive_mutex> lock(state().mutex);features[I].erase(h);',
    'std::lock_guard<std::mutex> lock(featureMutex); features[I].erase(h);')
new='''// SPDX-License-Identifier: MIT
// Discovery extracted from TGFG: one process-wide NGX observer, shared by FG and NR.
#include "core.hpp"
#include <tgb/dlss_inputs.hpp>
#include <nvsdk_ngx.h>
#include <array>
#include <unordered_map>
#include <memory>
#include <cstring>
#include <type_traits>
namespace tgb { namespace {
std::mutex observerMutex;
std::shared_ptr<const std::vector<DlssObserver>> observers=std::make_shared<const std::vector<DlssObserver>>();
void notify(DlssStage stage,const DlssInputs& in) {
    if(!in.feature) return;
    auto listeners=std::atomic_load(&observers);
    for(auto callback:*listeners) callback(stage,&in);
}
'''+discovery+'''
void created(size_t provider,NVSDK_NGX_Feature feature,const NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle* handle) {
    if(feature!=NVSDK_NGX_Feature_SuperSampling) return;
    Feature f; f.flags=uint_value(p,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags);
    f.width=uint_value(p,NVSDK_NGX_Parameter_Width); f.height=uint_value(p,NVSDK_NGX_Parameter_Height);
    f.outWidth=uint_value(p,NVSDK_NGX_Parameter_OutWidth); f.outHeight=uint_value(p,NVSDK_NGX_Parameter_OutHeight);
    { std::lock_guard<std::mutex> lock(featureMutex); features[provider][handle]=f; }
    DlssInputs in; in.feature=handle; in.parameters=p; in.flags=f.flags;
    in.colorRect={0,0,f.width,f.height}; in.outputRect={0,0,f.outWidth,f.outHeight};
    notify(DlssStage::Created,in);
}
DlssInputs observe(size_t provider,ID3D11DeviceContext* context,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* p) {
    Feature f;
    { std::lock_guard<std::mutex> lock(featureMutex);
      const auto found=features[provider].find(handle); if(found==features[provider].end()) return {};
      f=found->second; }
    DlssInputs in; in.context=context; in.feature=handle; in.parameters=p; in.flags=f.flags;
    read(p,NVSDK_NGX_Parameter_Color,in.color); read(p,NVSDK_NGX_Parameter_Output,in.output);
    read(p,NVSDK_NGX_Parameter_Depth,in.depth); read(p,NVSDK_NGX_Parameter_MotionVectors,in.motion);
    const auto rw=uint_value(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,f.width);
    const auto rh=uint_value(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,f.height);
    const bool lowres=(f.flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes)!=0;
    in.colorRect={uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X),uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y),rw,rh};
    in.depthRect={uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X),uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y),rw,rh};
    in.motionRect={uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X),uint_value(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y),lowres?rw:f.outWidth,lowres?rh:f.outHeight};
    in.outputRect={uint_value(p,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X),uint_value(p,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y),f.outWidth,f.outHeight};
    read(p,NVSDK_NGX_Parameter_MV_Scale_X,in.motionScaleX); read(p,NVSDK_NGX_Parameter_MV_Scale_Y,in.motionScaleY);
    read(p,NVSDK_NGX_Parameter_Jitter_Offset_X,in.jitterX); read(p,NVSDK_NGX_Parameter_Jitter_Offset_Y,in.jitterY);
    in.reset=uint_value(p,NVSDK_NGX_Parameter_Reset);
    in.valid=valid_dlss_inputs(in); return in;
}
'''+hooks+'''
} } // namespace tgb
extern "C" __declspec(dllexport) bool __cdecl TGGraphicsBridge_SubscribeDlss(tgb::DlssObserver callback) {
    if(!callback) return false;
    try {
        std::lock_guard<std::mutex> lock(tgb::observerMutex);
        auto previous=std::atomic_load(&tgb::observers);
        for(auto old:*previous) if(old==callback) return true;
        const auto address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcAddress");
        if(!tgb::add_hook(reinterpret_cast<void*>(address),reinterpret_cast<void*>(&tgb::get_proc_hook),reinterpret_cast<void**>(&tgb::originalGetProcAddress))) return false;
        auto next=std::make_shared<std::vector<tgb::DlssObserver>>(*previous); next->push_back(callback);
        std::atomic_store(&tgb::observers,std::shared_ptr<const std::vector<tgb::DlssObserver>>(next));
        return true;
    } catch(...) { return false; }
}
extern "C" __declspec(dllexport) void __cdecl TGGraphicsBridge_PublishDlss(tgb::DlssStage stage,const tgb::DlssInputs* in) {
    if(!in || in->size!=sizeof(*in) || in->source!=tgb::DlssSource::RayReconstruction) return;
    try { tgb::notify(stage,*in); } catch(...) { }
}
'''
write('native/bridge/src/dlss_inputs.cpp',new)
# FG keeps its camera/frame association, readiness and transfer logic. Only discovery/parameter decode moves.
a=s.index('using CreateFn='); b=s.index('sl::float4x4 matrix',a)
s=s[:a]+s[b:]
a=s.index('void created('); b=s.index('struct Capture',a); s=s[:a]+s[b:]
s=s.replace('Capture capture(size_t provider,ID3D11DeviceContext* context,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* p) {',
'''Capture capture(const tgb::DlssInputs& in) {
    auto* context=in.context;''')
s=s.replace('    const auto found=features[provider].find(handle);\n    if(found==features[provider].end())return {};\n','')
s=s.replace('const auto& f=found->second;const auto& camera=frame->camera;',
    'const auto& camera=frame->camera;\n    const auto outWidth=in.outputRect.width,outHeight=in.outputRect.height;')
s=s.replace('f.outWidth!=camera.width || f.outHeight!=camera.height','outWidth!=camera.width || outHeight!=camera.height')
a=s.index('    s.ngxFlags=f.flags;'); b=s.index('    frame->inputContract=true;',a)
s=s[:a]+'''    s.ngxFlags=in.flags;
    auto* depth=in.depth; auto* motion=in.motion;
    const auto& d=in.depthRect; const auto& m=in.motionRect;
    const RectU de{d.x,d.y,d.width,d.height},me{m.x,m.y,m.width,m.height};
    const float sx=in.motionScaleX,sy=in.motionScaleY,jx=in.jitterX,jy=in.jitterY;
    if(!in.valid) { ++s.diag.ngxParams; capture_problem(Reason::InputContractInvalid,"DLSS input packet is missing or invalid"); return {}; }
'''+s[b:]
s=s.replace('f.flags&NVSDK','in.flags&NVSDK').replace('uint_value(p,NVSDK_NGX_Parameter_Reset)','in.reset')
a=s.index('template<size_t I> NVSDK_NGX_Result'); b=s.index('} // namespace\n// HDRP',a)
s=s[:a]+'''thread_local Capture pending;
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
'''+s[b:]
a=s.index('    const auto p=GetProcAddress(',s.index('bool install_ngx_discovery()'))
b=s.index('\n}',a)
s=s[:a]+'    return tgb::subscribe_dlss(&observe);'+s[b:]
s=s.replace('#include "dx11_bridge.hpp"','#include "dx11_bridge.hpp"\n#include <tgb/dlss_inputs.hpp>')
write('native/framegen/src/ngx_capture.cpp',s)
cm=Path('CMakeLists.txt').read_text(); cm=cm.replace('native/bridge/src/ngx_session.cpp)','native/bridge/src/ngx_session.cpp native/bridge/src/dlss_inputs.cpp)')
cm=cm.replace('target_include_directories(TGGraphicsBridge.Native PRIVATE ${BRIDGE_INCLUDE})','target_include_directories(TGGraphicsBridge.Native PRIVATE ${BRIDGE_INCLUDE} "${NGX_INCLUDE_DIR}")')
write('CMakeLists.txt',cm)
print('Moved NGX observation into bridge; FG consumes the shared callback.')
