// SPDX-License-Identifier: MIT
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
std::mutex featureMutex;
thread_local bool inNgx=false;
struct Scope{bool previous=inNgx;Scope(){inNgx=true;}~Scope(){inNgx=previous;}};
decltype(&GetProcAddress) originalGetProcAddress=nullptr;
template<class T>bool read(const NVSDK_NGX_Parameter* p,const char* key,T& out){return p&&NVSDK_NGX_SUCCEED(p->Get(key,&out));}
uint32_t uint_value(const NVSDK_NGX_Parameter* p,const char* key,uint32_t fallback=0) {
    unsigned int u=0;if(read(p,key,u))return u;int v=0;return read(p,key,v)&&v>=0?uint32_t(v):fallback;
}

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
template<size_t I> NVSDK_NGX_Result NVSDK_CONV create_hook(ID3D11DeviceContext* c,NVSDK_NGX_Feature f,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** h) {
    const bool inspect=!internalCall&&!inNgx;Scope nested;const auto result=providers[I].create.load()(c,f,p,h);
    if(inspect&&NVSDK_NGX_SUCCEED(result)&&h&&*h){try{created(I,f,p,*h);}catch(const std::exception& e){log(e.what());}}return result;
}
template<size_t I> NVSDK_NGX_Result NVSDK_CONV eval_hook(ID3D11DeviceContext* c,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback cb) {
    const bool inspect=!internalCall&&!inNgx;Scope nested;DlssInputs input;
    if(inspect){try{input=observe(I,c,h,p); notify(DlssStage::Before,input);}catch(const std::exception& e){log(e.what());}}
    const auto result=providers[I].evaluate.load()(c,h,p,cb);
    if(inspect && input.feature) { input.success=NVSDK_NGX_SUCCEED(result); notify(DlssStage::After,input); }return result;
}
template<size_t I> NVSDK_NGX_Result NVSDK_CONV eval_c_hook(ID3D11DeviceContext* c,const NVSDK_NGX_Handle* h,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback_C cb) {
    const bool inspect=!internalCall&&!inNgx;Scope nested;DlssInputs input;
    if(inspect){try{input=observe(I,c,h,p); notify(DlssStage::Before,input);}catch(const std::exception& e){log(e.what());}}
    const auto result=providers[I].evaluateC.load()(c,h,p,cb);
    if(inspect && input.feature) { input.success=NVSDK_NGX_SUCCEED(result); notify(DlssStage::After,input); }return result;
}
template<size_t I> NVSDK_NGX_Result NVSDK_CONV release_hook(NVSDK_NGX_Handle* h) {
    const bool inspect=!internalCall&&!inNgx;Scope nested;
    if(inspect){std::lock_guard<std::mutex> lock(featureMutex); features[I].erase(h);}
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
    if(!address||internalCall||reinterpret_cast<uintptr_t>(name)<=0xffff)return address;
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
