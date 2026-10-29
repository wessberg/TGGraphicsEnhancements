// SPDX-License-Identifier: MIT
// NR-30/31/32: opt-in by supplying a community DLL. The caller-name exception is
// confined to this TGNR-owned module, TGNeuralRendering.nvngx.dll. Never replace the driver DLL,
// hook Windows trust, patch the model, or route any signed-runtime consumer through this adapter.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include "tgnr_community_api.hpp"
#include "tgnr_runtime_profile.hpp"
#include "runtime_identity.hpp"
#include "community_parameters.hpp"
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace {
constexpr auto feature=static_cast<NVSDK_NGX_Feature>(18);
std::recursive_mutex mutex;
// Process lifetime, like the extension. The bridge owns the device. No NGX core shutdown here.
HMODULE model=nullptr;
std::unique_ptr<tgnr::RuntimeFileLock> fileLock;
std::unique_ptr<tgnr::CommunityParameters> parameters;
std::unordered_set<NVSDK_NGX_Handle*> handles;
decltype(&NVSDK_NGX_D3D12_GetScratchBufferSize) modelScratch=nullptr;
decltype(&NVSDK_NGX_D3D12_CreateFeature) modelCreate=nullptr;
decltype(&NVSDK_NGX_D3D12_EvaluateFeature) modelEvaluate=nullptr;
decltype(&NVSDK_NGX_D3D12_ReleaseFeature) modelRelease=nullptr;
bool ready=false;
template<class T> T symbol(const char* name) {
    auto fn=reinterpret_cast<T>(GetProcAddress(model,name));
    if(!fn) throw std::runtime_error("Community runtime export missing"); return fn;
}
// A lock guard survives every model call. Besides serialization, this prevents tail calls from
// moving the model's return address into the main extension and breaking its caller-name check.
NVSDK_NGX_Result __cdecl initialize(const wchar_t* path,const char* selectedSha256,const wchar_t* data,ID3D12Device* device,IDXGIAdapter* adapter,NVSDK_NGX_Parameter** out,tgnr::CommunityInfo* info) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!path || !selectedSha256 || !data || !device || !adapter || !out || !info || model) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *out=nullptr; *info={};
    try {
        const auto file=std::filesystem::canonical(path);
        auto locked=std::make_unique<tgnr::RuntimeFileLock>(file.c_str());
        // Detect a file replaced since bootstrap, without pinning any release's hash.
        if(!locked->matches(selectedSha256)) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        // A prior owner of this module could have another initialization or patched imports.
        if(GetModuleHandleW(L"nvngx_dlssnr.dll")) return NVSDK_NGX_Result_FAIL_FeatureAlreadyExists;
        model=LoadLibraryExW(file.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!model) return NVSDK_NGX_Result_FAIL_PlatformError;
        fileLock=std::move(locked); // Keep the verified file and loaded module for the process.
        std::vector<wchar_t> loaded(32768);
        if(!GetModuleFileNameW(model,loaded.data(),DWORD(loaded.size())) || !std::filesystem::equivalent(file,loaded.data())) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        using Init=NVSDK_NGX_Result (NVSDK_CONV*)(unsigned long long,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version,const NVSDK_NGX_Parameter*);
        auto init=symbol<Init>("NVSDK_NGX_D3D12_Init_Ext");
        auto populate=symbol<NVSDK_NGX_Result (NVSDK_CONV*)(NVSDK_NGX_Parameter*)>("NVSDK_NGX_D3D12_PopulateParameters_Impl");
        auto requirements=symbol<decltype(&NVSDK_NGX_D3D12_GetFeatureRequirements)>("NVSDK_NGX_D3D12_GetFeatureRequirements");
        info->applicationId=symbol<unsigned (NVSDK_CONV*)()>("NVSDK_NGX_GetApplicationId")();
        info->apiVersion=symbol<unsigned (NVSDK_CONV*)()>("NVSDK_NGX_GetAPIVersion")();
        if(!tgnr::community_api_supported(info->apiVersion)) return NVSDK_NGX_Result_FAIL_InvalidParameter;
        modelScratch=symbol<decltype(modelScratch)>("NVSDK_NGX_D3D12_GetScratchBufferSize");
        modelCreate=symbol<decltype(modelCreate)>("NVSDK_NGX_D3D12_CreateFeature");
        modelEvaluate=symbol<decltype(modelEvaluate)>("NVSDK_NGX_D3D12_EvaluateFeature");
        modelRelease=symbol<decltype(modelRelease)>("NVSDK_NGX_D3D12_ReleaseFeature");
        NVSDK_NGX_FeatureDiscoveryInfo discovery{}; discovery.SDKVersion=static_cast<NVSDK_NGX_Version>(info->apiVersion);
        discovery.FeatureID=feature; discovery.Identifier.IdentifierType=NVSDK_NGX_Application_Identifier_Type_Application_Id;
        discovery.Identifier.v.ApplicationId=info->applicationId; discovery.ApplicationDataPath=data;
        NVSDK_NGX_FeatureRequirement required{};
        auto result=requirements(adapter,&discovery,&required);
        info->requirementsResult=uint32_t(result); info->supportFlags=uint32_t(required.FeatureSupported);
        if(NVSDK_NGX_FAILED(result)) return result;
        if(info->supportFlags) return NVSDK_NGX_Result_FAIL_FeatureNotSupported;
        result=init(info->applicationId,data,device,static_cast<NVSDK_NGX_Version>(info->apiVersion),nullptr);
        if(NVSDK_NGX_FAILED(result)) return result;
        parameters=std::make_unique<tgnr::CommunityParameters>();
        result=populate(parameters.get()); if(NVSDK_NGX_FAILED(result)) return result;
        ready=true; *out=parameters.get(); return NVSDK_NGX_Result_Success;
    } catch(...) { return NVSDK_NGX_Result_FAIL_PlatformError; }
}
NVSDK_NGX_Result NVSDK_CONV scratch(NVSDK_NGX_Feature f,const NVSDK_NGX_Parameter* p,size_t* bytes) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!ready || f!=feature || p!=parameters.get() || !bytes) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    return modelScratch(f,p,bytes);
}
NVSDK_NGX_Result NVSDK_CONV create(ID3D12GraphicsCommandList* list,NVSDK_NGX_Feature f,NVSDK_NGX_Parameter* p,NVSDK_NGX_Handle** out) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!ready || f!=feature || p!=parameters.get() || !list || !out) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *out=nullptr;
    auto result=modelCreate(list,f,p,out);
    if(NVSDK_NGX_SUCCEED(result) && *out) handles.insert(*out);
    return result;
}
NVSDK_NGX_Result NVSDK_CONV evaluate(ID3D12GraphicsCommandList* list,const NVSDK_NGX_Handle* handle,const NVSDK_NGX_Parameter* p,PFN_NVSDK_NGX_ProgressCallback callback) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!ready || !list || p!=parameters.get() || !handles.count(const_cast<NVSDK_NGX_Handle*>(handle))) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    return modelEvaluate(list,handle,p,callback);
}
NVSDK_NGX_Result NVSDK_CONV release(NVSDK_NGX_Handle* handle) {
    std::lock_guard<std::recursive_mutex> guard(mutex);
    if(!ready || !handles.count(handle)) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    auto result=modelRelease(handle);
    if(NVSDK_NGX_SUCCEED(result)) handles.erase(handle);
    return result;
}
}
extern "C" __declspec(dllexport) int __cdecl TGNR_GetCommunityApi(tgnr::CommunityApi* out) {
    if(!out || out->size!=sizeof(*out) || out->abi!=tgnr::communityAbi) return -1;
    out->initialize=initialize; out->scratch=scratch; out->create=create; out->evaluate=evaluate; out->release=release;
    return 0;
}
