// SPDX-License-Identifier: MIT
// CPU-only model fixture. It never dereferences a graphics pointer or renders anything.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <nvsdk_ngx_params.h>
#include <cstdint>
#include <cstring>
#define EXPORT extern "C" __declspec(dllexport)
namespace {
unsigned initializations=0;
unsigned handleToken=0;
bool mode(const char* value) {
    char selected[32]{}; GetEnvironmentVariableA("TGNR_ADAPTER_TEST_MODE",selected,sizeof(selected));
    return std::strcmp(selected,value)==0;
}
}
EXPORT unsigned NVSDK_CONV NVSDK_NGX_GetApplicationId() { return 123; }
EXPORT unsigned NVSDK_CONV NVSDK_NGX_GetAPIVersion() { return mode("api") ? 0x14:0x13; }
EXPORT unsigned __cdecl TGNR_TestInitCalls() { return initializations; }
EXPORT NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetFeatureRequirements(IDXGIAdapter*,const NVSDK_NGX_FeatureDiscoveryInfo* info,NVSDK_NGX_FeatureRequirement* out) {
    if(info->Identifier.v.ApplicationId!=123 || info->FeatureID!=static_cast<NVSDK_NGX_Feature>(18)) return NVSDK_NGX_Result_FAIL_InvalidParameter;
    *out={};
    out->FeatureSupported=static_cast<NVSDK_NGX_Feature_Support_Result>(mode("unsupported") ? 2:0);
    return NVSDK_NGX_Result_Success;
}
EXPORT NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_Init_Ext(unsigned long long id,const wchar_t*,ID3D12Device*,NVSDK_NGX_Version version,const NVSDK_NGX_Parameter*) {
    ++initializations;
    return id==123 && uint32_t(version)==0x13 ? NVSDK_NGX_Result_Success:NVSDK_NGX_Result_FAIL_InvalidParameter;
}
EXPORT NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_PopulateParameters_Impl(NVSDK_NGX_Parameter* p) {
    p->Set("Fixture",123u); return NVSDK_NGX_Result_Success;
}
EXPORT NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_GetScratchBufferSize(NVSDK_NGX_Feature,const NVSDK_NGX_Parameter*,size_t* bytes) {
    *bytes=0; return NVSDK_NGX_Result_Success;
}
EXPORT NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_CreateFeature(ID3D12GraphicsCommandList*,NVSDK_NGX_Feature,NVSDK_NGX_Parameter*,NVSDK_NGX_Handle** out) {
    *out=reinterpret_cast<NVSDK_NGX_Handle*>(&handleToken); return NVSDK_NGX_Result_Success;
}
EXPORT NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_EvaluateFeature(ID3D12GraphicsCommandList*,const NVSDK_NGX_Handle*,const NVSDK_NGX_Parameter*,PFN_NVSDK_NGX_ProgressCallback) {
    return NVSDK_NGX_Result_Success;
}
EXPORT NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_D3D12_ReleaseFeature(NVSDK_NGX_Handle*) { return NVSDK_NGX_Result_Success; }
