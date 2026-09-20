// SPDX-License-Identifier: MIT
#pragma once
#include <d3d11.h>
#include <d3d11shader.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstring>
#include <cstdint>

namespace tgfg {
// Recognize RenoDX Unity's final_0x20133A8B replacement by its reflected binding
// contract or an inspected stripped DXBC identity (RenoDX strips reflection in releases).
// The actual shader and live b13 buffer are replayed; no RenoDX math is duplicated.
inline bool is_renodx_final_pass(const void* code,size_t size) {
    if(!code || !size)return false;
    if(size==3992 && std::memcmp(code,"DXBC",4)==0) {
        // RenoDX Unity v0.2026.325.2215: disassembled and independently exercised
        // from the installed add-on. Keep unknown stripped shaders unsupported.
        constexpr unsigned char checksum[]={0x66,0xce,0x04,0x42,0x02,0xe8,0xbf,0xe1,
            0x5f,0x65,0xbf,0x99,0x8f,0x2d,0x3b,0x63};
        if(std::memcmp(static_cast<const char*>(code)+4,checksum,sizeof(checksum))==0)return true;
    }
    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflection;
    if(FAILED(D3DReflect(code,size,IID_PPV_ARGS(&reflection))))return false;
    D3D11_SHADER_DESC desc{};
    if(FAILED(reflection->GetDesc(&desc)) || D3D11_SHVER_GET_TYPE(desc.Version)!=D3D11_SHVER_PIXEL_SHADER ||
       desc.BoundResources!=3 || desc.OutputParameters!=1 || desc.InputParameters!=2)return false;
    D3D11_SIGNATURE_PARAMETER_DESC uv{},position{};
    if(FAILED(reflection->GetInputParameterDesc(0,&uv)) || FAILED(reflection->GetInputParameterDesc(1,&position)) ||
       std::strcmp(uv.SemanticName,"TEXCOORD")!=0 || uv.SemanticIndex!=0 || uv.Register!=0 || uv.Mask!=3 ||
       position.SystemValueType!=D3D_NAME_POSITION || position.Register!=1)return false;
    bool texture=false,sampler=false,constants=false;
    for(UINT i=0;i<desc.BoundResources;++i) {
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        if(FAILED(reflection->GetResourceBindingDesc(i,&binding)) || binding.BindCount!=1)return false;
        texture|=binding.Type==D3D_SIT_TEXTURE && binding.BindPoint==0 &&
            binding.Dimension==D3D_SRV_DIMENSION_TEXTURE2D && std::strcmp(binding.Name,"BlitTexture")==0;
        sampler|=binding.Type==D3D_SIT_SAMPLER && binding.BindPoint==0 && std::strcmp(binding.Name,"BlitSampler_s")==0;
        constants|=binding.Type==D3D_SIT_CBUFFER && binding.BindPoint==13 && std::strcmp(binding.Name,"shader_injection")==0;
    }
    return texture && sampler && constants;
}
}
