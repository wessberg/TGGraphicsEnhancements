// SPDX-License-Identifier: MIT
// Windows-only compilation test of the EXACT embedded runtime HLSL.
// It creates no graphics device and is not a GPU correctness test.
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <cstring>
#include "interop_shader.hpp"
using Microsoft::WRL::ComPtr;
int main() {
    struct Test{const char* macro;const char* entry;const char* profile;};
    const Test tests[]={{"TGFG_DEPTH","main","cs_5_0"},{"TGFG_MOTION","main","cs_5_0"},
                        {"TGFG_DEPTH_FLIP","main","cs_5_0"},{"TGFG_MOTION_FLIP","main","cs_5_0"},
                        {"TGFG_HDR","vsMain","vs_5_0"},{"TGFG_HDR","psMain","ps_5_0"},
                        {"TGFG_HUDCOLOR","psMain","ps_5_0"},{"TGFG_HUDCHECK","main","cs_5_0"}};
    for(const auto& test:tests) {
        const D3D_SHADER_MACRO defines[]={{test.macro,"1"},{nullptr,nullptr}};
        ComPtr<ID3DBlob> code,error;
        const auto hr=D3DCompile(tgfg::interopShader,std::strlen(tgfg::interopShader),"TGFG/interop.hlsl",defines,nullptr,
            test.entry,test.profile,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
        if(FAILED(hr)){if(error)std::cerr.write(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize());return 1;}
        std::cout<<test.macro<<"/"<<test.entry<<": "<<code->GetBufferSize()<<" compiled bytes\n";
    }
    std::cout<<"Eight HLSL entrypoints compiled. No rendering test performed.\n";
}
