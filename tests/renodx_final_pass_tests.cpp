// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "renodx_final_pass.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
using Microsoft::WRL::ComPtr;
static ComPtr<ID3DBlob> compile(const std::string& source) {
    ComPtr<ID3DBlob> code,error;
    const auto hr=D3DCompile(source.data(),source.size(),nullptr,nullptr,nullptr,"main","ps_4_0",0,0,&code,&error);
    if(FAILED(hr))throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Compile failed");
    return code;
}
int main(int argc,char** argv) try {
    const std::string source=R"(
Texture2D<float4> BlitTexture:register(t0);
SamplerState BlitSampler_s:register(s0);
cbuffer shader_injection:register(b13) {float4 settings;};
float4 main(float2 uv:TEXCOORD0,float4 position:SV_Position):SV_Target {return BlitTexture.Sample(BlitSampler_s,uv)*settings;}
)";
    auto code=compile(source);
    if(!tgfg::is_renodx_final_pass(code->GetBufferPointer(),code->GetBufferSize()))throw std::runtime_error("Missed final-output binding contract");
    for(const auto& replacement:std::vector<std::pair<std::string,std::string>>{
        {"BlitTexture","SceneTexture"},{"b13","b12"},{"t0","t1"},{"BlitSampler_s","OtherSampler"}}) {
        auto wrong=source;size_t p=0;
        while((p=wrong.find(replacement.first,p))!=std::string::npos) {wrong.replace(p,replacement.first.size(),replacement.second);p+=replacement.second.size();}
        auto other=compile(wrong);
        if(tgfg::is_renodx_final_pass(other->GetBufferPointer(),other->GetBufferSize()))throw std::runtime_error("Accepted unrelated shader");
    }
    if(tgfg::is_renodx_final_pass(nullptr,0))throw std::runtime_error("Accepted missing shader");
    std::cout<<"RenoDX shader recognition positive and negative fixtures passed.\n";
    if(argc>1) {
        std::ifstream file(argv[1],std::ios::binary);
        if(!file)throw std::runtime_error("Cannot open installed add-on");
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
        unsigned found=0;
        for(size_t p=0;p+32<bytes.size();++p) {
            if(std::memcmp(bytes.data()+p,"DXBC",4)!=0)continue;
            uint32_t size=0;std::memcpy(&size,bytes.data()+p+24,4);
            if(size<32 || size>bytes.size()-p)continue;
            if(tgfg::is_renodx_final_pass(bytes.data()+p,size)) {
                ++found;std::cout<<"Installed add-on final shader: offset="<<p<<" bytes="<<size<<"\n";
                if(argc>2) {std::ofstream out(argv[2],std::ios::binary);out.write(bytes.data()+p,size);}
            }
        }
        if(found!=1)throw std::runtime_error("Expected exactly one installed final-output shader, found "+std::to_string(found));
    }
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<"\n";return 1;}
