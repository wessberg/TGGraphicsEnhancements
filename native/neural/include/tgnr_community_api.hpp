// SPDX-License-Identifier: MIT
// Private, versioned interface between TGNR and its community-runtime adapter.
#pragma once
#include <nvsdk_ngx.h>
#include <cstdint>
namespace tgnr {
inline constexpr uint32_t communityAbi=2;
struct CommunityInfo {
    uint32_t applicationId=0,apiVersion=0,requirementsResult=0,supportFlags=0;
};
struct CommunityApi {
    uint32_t size=sizeof(CommunityApi),abi=communityAbi;
    NVSDK_NGX_Result (__cdecl* initialize)(const wchar_t* file,const char* selectedSha256,const wchar_t* data,ID3D12Device*,IDXGIAdapter*,NVSDK_NGX_Parameter**,CommunityInfo*)=nullptr;
    decltype(&NVSDK_NGX_D3D12_GetScratchBufferSize) scratch=nullptr;
    decltype(&NVSDK_NGX_D3D12_CreateFeature) create=nullptr;
    decltype(&NVSDK_NGX_D3D12_EvaluateFeature) evaluate=nullptr;
    decltype(&NVSDK_NGX_D3D12_ReleaseFeature) release=nullptr;
};
using GetCommunityApi=int (__cdecl*)(CommunityApi*);
}
