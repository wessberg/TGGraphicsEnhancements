// SPDX-License-Identifier: MIT
// A real bridge-device probe. Exit 77 means unavailable, never a successful NR evaluation.
#define NOMINMAX
#include <tgb/bridge_c_api.h>
#include <tgb/dlss_inputs.hpp>
#include "tgnr_api.h"
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <filesystem>
#include <cstdio>
#include <vector>
#include <cstring>
#include <string>
#include <cmath>
#include <fstream>
using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
template<class T> T symbol(HMODULE module,const char* name) {
    auto result=reinterpret_cast<T>(GetProcAddress(module,name));
    if(!result) throw std::runtime_error(std::string("Missing export ")+name); return result;
}
int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    if(argc<2) { std::puts("Usage: tgnr_gpu_smoke <scratch installed BepInEx root>"); return 1; }
    try {
        auto root=fs::absolute(argv[1]); auto native=root/"TGNeuralRendering"/"Native";
        auto bridge=LoadLibraryW((native/"TGGraphicsBridge.Native.dll").c_str());
        if(!bridge) throw std::runtime_error("Could not load the staged bridge");
        auto add=symbol<int(__cdecl*)(const wchar_t*)>(bridge,"TGGraphicsBridge_AddExtension");
        auto boot=symbol<uint32_t(__stdcall*)(void*)>(bridge,"TGGraphicsBridge_Bootstrap");
        if(add((native/"TGNeuralRendering.Native.dll").c_str()) || boot(nullptr)) throw std::runtime_error("Bridge bootstrap failed");
        auto nr=GetModuleHandleW(L"TGNeuralRendering.Native.dll"); if(!nr) throw std::runtime_error("NR extension not loaded");
        auto read=symbol<int(__cdecl*)(TGNR_Status*)>(nr,"TGNR_ReadStatus");
        auto reason=symbol<int(__cdecl*)(char*,uint32_t)>(nr,"TGNR_GetReason");
        auto submit=symbol<void*(__cdecl*)(const TGNR_Frame*,uint32_t)>(nr,"TGNR_QueueFrame");
        auto event=symbol<void*(__cdecl*)()>(nr,"TGNR_GetRenderEvent");
        auto render=reinterpret_cast<void(__stdcall*)(int,void*)>(event());
        WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=DefWindowProcW; wc.lpszClassName=L"TGNR probe"; RegisterClassW(&wc);
        HWND window=CreateWindowW(wc.lpszClassName,L"TGNR probe",WS_OVERLAPPEDWINDOW,0,0,1296,759,nullptr,nullptr,wc.hInstance,nullptr);
        ComPtr<IDXGIFactory4> factory; if(FAILED(CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)))) return 77;
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        D3D_FEATURE_LEVEL level=D3D_FEATURE_LEVEL_11_0;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,&level,1,D3D11_SDK_VERSION,&device,nullptr,&context))) return 77;
        DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width=1280; desc.Height=720; desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> swap;
        if(FAILED(factory->CreateSwapChainForHwnd(device.Get(),window,&desc,nullptr,nullptr,&swap))) throw std::runtime_error("Swapchain creation failed");
        TGNR_Status status{sizeof(status),TGNR_ABI}; read(&status); char detail[2048]{}; reason(detail,sizeof(detail));
        std::printf("NR state=%u result=0x%08X: %s\n",status.state,status.lastResult,detail);
        if(status.state!=TGNR_READY) {
            if(argc>2) {
                auto expected=uint32_t(std::stoul(argv[2]));
                if(status.state!=expected || status.evaluated!=0) return 1;
                std::puts("PASS: unavailable NR leaves a working bridge and original swapchain, no evaluation reported."); return 0;
            }
            std::puts("SKIP: NR was not available; creation/evaluation not proven."); return 77;
        }
        auto texture=[&](UINT w,UINT h,DXGI_FORMAT format,const void* data,UINT pitch) {
            D3D11_TEXTURE2D_DESC d{}; d.Width=w; d.Height=h; d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;
            d.Format=format; d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
            D3D11_SUBRESOURCE_DATA initial{data,pitch,0}; ComPtr<ID3D11Texture2D> result;
            if(FAILED(device->CreateTexture2D(&d,&initial,&result))) throw std::runtime_error("Probe texture creation failed"); return result;
        };
        auto pixels=[&](ID3D11Texture2D* input,UINT size) {
            D3D11_TEXTURE2D_DESC d{}; input->GetDesc(&d); d.BindFlags=d.MiscFlags=0; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; d.Usage=D3D11_USAGE_STAGING;
            ComPtr<ID3D11Texture2D> staging; if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))) throw std::runtime_error("Readback allocation failed");
            context->CopyResource(staging.Get(),input); D3D11_MAPPED_SUBRESOURCE map{};
            if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) throw std::runtime_error("Readback failed");
            std::vector<float> result(size*size*4);
            for(UINT y=0;y<size;++y) std::memcpy(result.data()+y*size*4,static_cast<const char*>(map.pData)+y*map.RowPitch,size*16);
            context->Unmap(staging.Get(),0); return result;
        };
        auto save=[&](const std::vector<float>& image,UINT size,const char* name) {
            std::ofstream file(root.parent_path()/(std::to_string(size)+"-"+name+".ppm"),std::ios::binary);
            file<<"P6\n"<<size<<" "<<size<<"\n255\n";
            for(size_t i=0;i<image.size();i+=4) for(int c=0;c<3;++c) {
                auto value=static_cast<unsigned char>(std::lround(std::max(0.f,std::min(1.f,image[i+c]))*255)); file.write(reinterpret_cast<char*>(&value),1);
            }
        };
        if(argc>2 && std::strcmp(argv[2],"--multipass")==0) {
            // Export exists only in the scratch fixture built by neural_multipass_mutations.py.
            const auto rawOutput=symbol<void(__cdecl*)(ID3D11Texture2D*)>(nr,"TGNR_TestCopyModelOutput");
            constexpr UINT size=256;
            std::vector<float> colors(size*size*4), depths(size*size,.5f), motion(size*size*2,0);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
                auto i=(y*size+x)*4; colors[i]=.05f+.7f*x/size; colors[i+1]=.1f+.6f*y/size;
                colors[i+2]=.15f+.2f*((x/16+y/16)%2); colors[i+3]=.37f;
            }
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            auto raw=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            TGNR_Frame f{sizeof(f),TGNR_ABI,7000,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,3,1,0,0,.1f,1000};
            auto evaluate=[&](const std::vector<float>& input,uint32_t count,bool reset) {
                context->UpdateSubresource(color.Get(),0,nullptr,input.data(),size*16,0);
                ++f.frame; f.passes=count; f.flags=(f.flags&(TGNR_HDR_INPUT|TGNR_SRGB_VIEW))|TGNR_REVERSED_DEPTH|(reset?TGNR_RESET:0);
                const auto before=status.evaluated, beforeModel=status.modelEvaluations;
                auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Multipass guides rejected"); render(1,guides);
                auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Multipass scene rejected"); render(2,scene);
                read(&status); reason(detail,sizeof(detail));
                if(status.state!=TGNR_READY || status.evaluated!=before+1 || status.modelEvaluations!=beforeModel+count)
                    throw std::runtime_error(std::string("Multipass evaluation count mismatch: ")+detail);
                auto out=pixels(color.Get(),size);
                for(size_t i=0;i<out.size();++i) {
                    if(!std::isfinite(out[i])) throw std::runtime_error("Multipass output is not finite");
                    if(i%4==3 && out[i]!=input[i]) throw std::runtime_error("Multipass changed alpha");
                }
                return out;
            };
            for(float strength:{.5f,1.f}) for(uint32_t count:{1u,2u,3u,4u,1u}) {
                f.strength=strength; f.flags=3;
                const auto actual=evaluate(colors,count,true);
                auto reference=colors;
                // Independent oracle: N single-pass calls with resets, feeding the full result forward.
                // Feed the exact encoded model result. A decode/encode round trip differs by <1e-6,
                // yet changes this model's result measurably; it is not an equivalent input.
                for(uint32_t i=0;i<count;++i) {
                    // Each chained stage is a fresh feature on its first frame; match that lifetime.
                    render(3,nullptr); read(&status);
                    reference=evaluate(reference,1,true);
                    if(i+1<count) {
                        rawOutput(raw.Get()); const auto encoded=pixels(raw.Get(),size);
                        for(UINT y=0;y<size;++y) std::memcpy(reference.data()+y*size*4,encoded.data()+(size-1-y)*size*4,size*16);
                        f.flags|=TGNR_SRGB_VIEW;
                    }
                }
                double worst=0, difference=0, error=0;
                for(size_t i=0;i<actual.size();++i) if(i%4!=3) {
                    worst=std::max(worst,double(std::abs(actual[i]-reference[i])));
                    error+=std::abs(actual[i]-reference[i]);
                    difference+=std::abs(actual[i]-colors[i]);
                }
                std::printf("Strength=%.2f passes=%u sequential reference max error=%.9f, mean=%.9f, effect=%.9f\n",strength,count,worst,error/(size*size*3),difference/(size*size*3));
                if(worst>1e-6 || difference/(size*size*3)<.0001) throw std::runtime_error("Multipass did not match sequential NR output");
                f.strength=0; f.flags=3;
                if(evaluate(colors,count,true)!=colors) throw std::runtime_error("Multipass zero strength changed the source");
            }
            f.strength=1;
            evaluate(colors,4,true);
            const auto resets=status.resets;
            evaluate(colors,4,false);
            if(status.resets!=resets) throw std::runtime_error("Multipass resets on every frame");
            f.flags=TGNR_HDR_INPUT;
            auto hdr=colors; for(size_t i=0;i<hdr.size();++i) if(i%4!=3) hdr[i]*=16;
            evaluate(hdr,4,true);
            f.strength=0;
            if(evaluate(hdr,4,false)!=hdr) throw std::runtime_error("Multipass zero strength changed HDR");
            const auto before=status.evaluated;
            render(3,nullptr); read(&status);
            if(status.evaluated!=before || pixels(color.Get(),size)!=hdr) throw std::runtime_error("Multipass Off changed the source");
            std::puts("PASS: 1/2/3/4 sequential NR passes, native invocation counts, history retention, SDR/HDR neutral, alpha and Off.");
            return 0;
        }
        const bool diagnostics=argc>3 && std::strcmp(argv[2],"--diagnostics")==0;
        auto diagnosticOutput=symbol<int(__cdecl*)(const wchar_t*)>(nr,"TGNR_SetDiagnostics");
        auto diagnosticExposure=symbol<int(__cdecl*)(void*,ID3D11Resource*)>(nr,"TGNR_QueueDiagnosticExposure");
        if(diagnostics && diagnosticOutput(fs::absolute(argv[3]).c_str())) throw std::runtime_error("Diagnostic output rejected");
        const float exposureData[]={0.125f,3.0f};
        auto exposure=texture(1,1,DXGI_FORMAT_R32G32_FLOAT,exposureData,8);
        for(uint32_t size:{256u,320u}) {
            std::vector<float> colors(size*size*4),depths(size*size,0.5f),motion(size*size*2,0);
            for(uint32_t y=0;y<size;++y) for(uint32_t x=0;x<size;++x) {
                auto i=4*(y*size+x); colors[i]=float(x)/size; colors[i+1]=float(y)/size; colors[i+2]=0.2f; colors[i+3]=1;
                motion[(y*size+x)*2]=1.0f/size; // known nonzero normalized movement
                motion[(y*size+x)*2+1]=(y<size/2 ? -2.0f:2.0f)/size; // signed Y and orientation reference
            }
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            save(colors,size,"source");
            TGNR_Frame f{sizeof(f),TGNR_ABI,size,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,3,1,0,0,0.1f,1000};
            const bool badViewport=argc>2 && std::strcmp(argv[2],"--bad-viewport")==0;
            if(badViewport) ++f.width;
            for(float strength:{0.f,0.5f,1.f}) {
              f.strength=strength; f.flags=3;
              for(int i=0;i<3;++i) {
                ++f.frame; auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Guides rejected"); render(1,guides);
                auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Color rejected");
                if(diagnostics && diagnosticExposure(scene,exposure.Get())) throw std::runtime_error("Diagnostic exposure rejected");
                render(2,scene);
                read(&status); reason(detail,sizeof(detail));
                std::printf("frame=%u evaluations=%llu result=0x%08X reason=%s\n",f.frame,status.evaluated,status.lastResult,detail);
                if(badViewport) {
                    if(status.state!=TGNR_FAILED || status.failed!=1 || status.evaluated!=0 || pixels(color.Get(),size)!=colors)
                        throw std::runtime_error("A refused viewport changed the scene or reported success");
                    std::puts("PASS: invalid viewport disabled NR alone and preserved the original scene."); return 0;
                }
                if(status.state!=TGNR_READY) return 1;
                const auto output=pixels(color.Get(),size);
                double difference=0,energy=0; size_t invalid=0;
                for(size_t p=0;p<output.size();p+=4) for(size_t c=0;c<3;++c) {
                    if(!std::isfinite(output[p+c])) ++invalid;
                    difference+=std::abs(output[p+c]-colors[p+c]); energy+=std::abs(output[p+c]);
                }
                difference/=size*size*3; energy/=size*size*3;
                std::printf("  image mean=%.8f difference=%.8f invalid=%zu\n",energy,difference,invalid);
                if(invalid || energy<0.001) throw std::runtime_error("NR output is invalid or black");
                if(strength==0 && difference>0.002) throw std::runtime_error("Strength zero does not preserve the input");
                if(strength==1 && difference<0.001) throw std::runtime_error("Full strength did not change the input");
                for(size_t p=3;p<output.size();p+=4) if(std::abs(output[p]-colors[p])>0.00001) throw std::runtime_error("NR changed source alpha");
                std::string label="strength-"+std::to_string(strength)+(i==0?"-first":"");
                save(output,size,label.c_str());
                // The next real frame starts from game color, never NR's previous output.
                context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
                f.flags=1;
              }
              const auto before=status.evaluated;
              render(3,nullptr); read(&status);
              if(status.state!=TGNR_READY || status.evaluated!=before) throw std::runtime_error("Disabling NR failed");
              if(pixels(color.Get(),size)!=colors) throw std::runtime_error("Disabling NR changed the original scene");
            }
        }
        if(status.evaluated!=18) throw std::runtime_error("NR did not evaluate every probe frame");
        if(diagnostics) diagnosticOutput(nullptr);
        uint32_t sequence=1000;
        for(auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_B8G8R8A8_TYPELESS}) {
            constexpr UINT size=256;
            std::vector<uint8_t> bytes(size*size*4);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
                auto p=(y*size+x)*4; bytes[p]=uint8_t(x); bytes[p+1]=uint8_t(y); bytes[p+2]=51; bytes[p+3]=uint8_t(x/2+64);
            }
            std::vector<float> depths(size*size,0.5f),motion(size*size*2,0);
            auto color=texture(size,size,format,bytes.data(),size*4);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            TGNR_Frame f{sizeof(f),TGNR_ABI,sequence++,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,7,0,0,0,0.1f,1000};
            auto guides=submit(&f,1); if(!guides) throw std::runtime_error("sRGB guides rejected"); render(1,guides);
            auto scene=submit(&f,2); if(!scene) throw std::runtime_error("sRGB scene rejected"); render(2,scene);
            read(&status); if(status.state!=TGNR_READY || status.evaluated!=18+sequence-1000) throw std::runtime_error("sRGB evaluation failed");
            D3D11_TEXTURE2D_DESC d{}; color->GetDesc(&d); d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging; if(FAILED(device->CreateTexture2D(&d,nullptr,&staging))) throw std::runtime_error("sRGB readback allocation failed");
            context->CopyResource(staging.Get(),color.Get()); D3D11_MAPPED_SUBRESOURCE map{};
            if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map))) throw std::runtime_error("sRGB readback failed");
            int worst=0;
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size*4;++x)
                worst=std::max(worst,std::abs(int(static_cast<const uint8_t*>(map.pData)[y*map.RowPitch+x])-int(bytes[y*size*4+x])));
            context->Unmap(staging.Get(),0);
            std::printf("sRGB format=%u neutral worst error=%d byte levels\n",format,worst);
            if(worst>1) throw std::runtime_error("sRGB transfer changed neutral RGB or alpha");
            render(3,nullptr);
        }
        {
            constexpr UINT size=256;
            std::vector<float> colors(size*size*4), depths(size*size,0.5f), motion(size*size*2,0);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
                auto p=(y*size+x)*4; float v=std::pow(2.f,float(x)/16.f-6.f);
                colors[p]=v; colors[p+1]=v*0.4f; colors[p+2]=v*float(y)/size; colors[p+3]=0.37f;
            }
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            TGNR_Frame f{sizeof(f),TGNR_ABI,2000,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,9,0,0,0,0.1f,1000};
            auto evaluate=[&]() {
                ++f.frame; context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
                auto before=status.evaluated;
                auto guides=submit(&f,1); if(!guides) throw std::runtime_error("HDR guides rejected"); render(1,guides);
                auto scene=submit(&f,2); if(!scene) throw std::runtime_error("HDR scene rejected"); render(2,scene);
                read(&status);
                if(status.state!=TGNR_READY || status.evaluated!=before+1) throw std::runtime_error("HDR evaluation failed");
                return pixels(color.Get(),size);
            };
            auto neutral=evaluate(); float worst=0;
            for(size_t p=0;p<colors.size();++p) {
                if(!std::isfinite(neutral[p])) throw std::runtime_error("HDR neutral produced invalid values");
                worst=std::max(worst,std::abs(neutral[p]-colors[p])/std::max(0.01f,colors[p]));
            }
            std::printf("HDR neutral maximum relative error %.6f over 0..981 scene units\n",worst);
            if(worst>0.002f) throw std::runtime_error("HDR neutral did not preserve highlights");
            if(std::memcmp(neutral.data(),colors.data(),colors.size()*sizeof(float))!=0)
                throw std::runtime_error("Zero strength changed original scene pixels");
            auto resets=status.resets;
            f.strength=0.5f; auto half=evaluate();
            if(status.resets!=resets) throw std::runtime_error("Intensity change reset history");
            // Same reset input for each style. Prove dispatch responds, without grading synthetic image quality.
            for(auto& v:colors) v=std::min(v,0.8f);
            f.flags=3; f.strength=1;
            std::vector<float> previous;
            const char* styles[]={"Default","A","B","C"};
            double greatestDifference=0;
            for(uint32_t model=0;model<4;++model) {
                f.model=model; auto output=evaluate();
                double difference=0;
                for(size_t p=0;p<output.size();++p) {
                    if(!std::isfinite(output[p])) throw std::runtime_error("Model output contains invalid values");
                    if(!previous.empty()) difference+=std::abs(output[p]-previous[p]);
                }
                difference/=output.size();
                std::printf("style %s difference from previous %.8f\n",styles[model],difference);
                greatestDifference=std::max(greatestDifference,difference);
                previous=std::move(output);
            }
            // A runtime may alias Default to a named style. It must still consume the selector.
            if(greatestDifference<0.00001) throw std::runtime_error("Model selector did not change output");
            render(3,nullptr);
        }
        if(diagnostics) {
            // Public RR publisher route: generic motion with X > 1 must remain untouched.
            // Unity SR decoding is checked against recorded game input (the public publisher accepts RR only).
            const UINT size=256;
            std::vector<float> colors(size*size*4,.25f),depths(size*size,.5f),motion(size*size*2,0);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) motion[(y*size+x)*2]=x<size/2 ? 2.f : -.01f;
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            for(auto source:{tgb::DlssSource::RayReconstruction}) {
                auto folder=fs::absolute(argv[3])/(source==tgb::DlssSource::SuperResolution ? "unity-motion" : "generic-motion");
                if(diagnosticOutput(folder.c_str())) throw std::runtime_error("Motion diagnostic output rejected");
                TGNR_Frame f{sizeof(f),TGNR_ABI,3000+uint32_t(source),1,reinterpret_cast<uint64_t>(color.Get()),0,0,size,size,size,size,3,0,0,0,.1f,1000};
                for(uint32_t iteration=0;iteration<26;++iteration,++f.frame) {
                auto arm=submit(&f,4); if(!arm) throw std::runtime_error("Observed camera rejected");
                diagnosticExposure(arm,exposure.Get()); render(4,arm);
                tgb::DlssInputs in; in.source=source; in.context=context.Get(); in.feature=&f;
                in.color=in.output=color.Get(); in.depth=depth.Get(); in.motion=mv.Get();
                in.colorRect=in.outputRect=in.depthRect=in.motionRect={0,0,size,size};
                in.flags=8; in.motionScaleX=in.motionScaleY=-float(size); in.valid=in.success=true;
                auto count=status.evaluated;
                tgb::publish_dlss(tgb::DlssStage::Before,in); tgb::publish_dlss(tgb::DlssStage::After,in);
                auto finish=submit(&f,5); if(!finish) throw std::runtime_error("Observed output rejected"); render(5,finish);
                read(&status); if(status.state!=TGNR_READY || status.evaluated!=count+1) throw std::runtime_error("Observed motion evaluation failed");
                }
                diagnosticOutput(nullptr); render(3,nullptr);
            }
        }
        if(diagnostics) {
            constexpr UINT size=256;
            std::vector<float> colors(size*size*4,2.f),graded(size*size*4),depths(size*size,.5f),motion(size*size*2,0);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
                auto i=4*(y*size+x); graded[i]=.1f+.6f*x/size; graded[i+1]=.1f+.6f*y/size; graded[i+2]=.2f; graded[i+3]=.37f;
            }
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto late=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,graded.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            for(uint32_t strength:{0u,1u}) {
                TGNR_Frame f{sizeof(f),TGNR_ABI,3500+strength,1,reinterpret_cast<uint64_t>(color.Get()),0,0,size,size,size,size,11,float(strength),0,0,.1f,1000};
                if(submit(&f,6) || submit(&f,7)) throw std::runtime_error("Late colour escaped diagnostic scope");
                auto folder=fs::absolute(argv[3])/("late-"+std::to_string(strength));
                if(diagnosticOutput(folder.c_str())) throw std::runtime_error("Late diagnostic output rejected");
                auto arm=submit(&f,4); if(!arm) throw std::runtime_error("Late arm rejected");
                diagnosticExposure(arm,exposure.Get()); render(4,arm);
                tgb::DlssInputs in; in.source=tgb::DlssSource::RayReconstruction; in.context=context.Get(); in.feature=&f;
                in.color=in.output=color.Get(); in.depth=depth.Get(); in.motion=mv.Get();
                in.colorRect=in.outputRect=in.depthRect=in.motionRect={0,0,size,size};
                in.flags=8; in.motionScaleX=in.motionScaleY=-float(size); in.valid=in.success=true;
                read(&status); const auto count=status.evaluated;
                tgb::publish_dlss(tgb::DlssStage::Before,in); tgb::publish_dlss(tgb::DlssStage::After,in);
                auto defer=submit(&f,7); if(!defer) throw std::runtime_error("Late deferral rejected"); render(7,defer);
                read(&status); if(status.evaluated!=count) throw std::runtime_error("Late comparison evaluated early");
                f.color=reinterpret_cast<uint64_t>(late.Get()); f.flags&=~TGNR_HDR_INPUT;
                auto finish=submit(&f,6); if(!finish) throw std::runtime_error("Late colour rejected"); render(6,finish);
                read(&status); if(status.state!=TGNR_READY || status.evaluated!=count+1) throw std::runtime_error("Late comparison evaluation failed");
                if(pixels(color.Get(),size)!=colors) throw std::runtime_error("Late comparison changed early HDR colour");
                auto out=pixels(late.Get(),size);
                if(!strength && out!=graded) throw std::runtime_error("Late zero strength changed graded colour");
                if(strength && out==graded) throw std::runtime_error("Late full strength did nothing");
                for(size_t i=0;i<out.size();++i) if(!std::isfinite(out[i]) || (i%4==3 && out[i]!=graded[i]))
                    throw std::runtime_error("Late comparison invalid colour/alpha");
                diagnosticOutput(nullptr); render(3,nullptr);
            }
            std::puts("PASS: deferred actual-DLSS guides, late colour evaluation, exact neutral and diagnostic scope.");
        }
        if(diagnostics) {
            auto conditioning=symbol<int(__cdecl*)(float)>(nr,"TGNR_SetDiagnosticInputScale");
            auto toneMap=symbol<int(__cdecl*)(uint32_t)>(nr,"TGNR_SetDiagnosticToneMap");
            if(toneMap(1)==0) throw std::runtime_error("Tone-map diagnostic escaped its scope");
            if(conditioning(4)==0) throw std::runtime_error("Input conditioning escaped diagnostic scope");
            constexpr UINT size=256;
            std::vector<float> colors(size*size*4),depths(size*size,.5f),motion(size*size*2,0);
            for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
                auto i=4*(y*size+x); colors[i]=float(x)/32; colors[i+1]=float(y)/64; colors[i+2]=.125f; colors[i+3]=.37f;
            }
            auto color=texture(size,size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),size*16);
            auto depth=texture(size,size,DXGI_FORMAT_R32_FLOAT,depths.data(),size*4);
            auto mv=texture(size,size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),size*8);
            uint32_t number=4000;
            for(float scale:{.25f,1.f,4.f,1.f,1.f,1.f}) {
                auto folder=fs::absolute(argv[3])/("signal-"+std::to_string(++number));
                if(diagnosticOutput(folder.c_str())) throw std::runtime_error("Input diagnostic output rejected");
                if(number==4005 && toneMap(1)) throw std::runtime_error("Per-channel tone-map diagnostic rejected");
                // The final case deliberately omits the setter to verify automatic restoration.
                if(number!=4004 && conditioning(scale)) throw std::runtime_error("Input conditioning rejected");
                for(float bad:{0.f,-1.f,INFINITY,NAN})
                    if(!conditioning(bad)) throw std::runtime_error("Invalid input conditioning accepted");
                // Nonzero strength exercises the HDR output shader, unlike the exact-zero bypass.
                TGNR_Frame f{sizeof(f),TGNR_ABI,number,1,reinterpret_cast<uint64_t>(color.Get()),reinterpret_cast<uint64_t>(depth.Get()),reinterpret_cast<uint64_t>(mv.Get()),size,size,size,size,11,1e-8f,0,0,.1f,1000};
                f.localTone=f.localStructure=f.skinStructure=1;
                auto guides=submit(&f,1); if(!guides) throw std::runtime_error("Signal guides rejected"); render(1,guides);
                auto scene=submit(&f,2); if(!scene) throw std::runtime_error("Signal scene rejected"); diagnosticExposure(scene,exposure.Get()); render(2,scene);
                read(&status); if(status.state!=TGNR_READY) throw std::runtime_error("Signal evaluation failed");
                const auto output=pixels(color.Get(),size); double worst=0;
                for(size_t i=0;i<output.size();++i) worst=std::max(worst,double(std::abs(output[i]-colors[i])/std::max(.001f,std::abs(colors[i]))));
                std::printf("Signal scale=%.2f inverse error=%.9f\n",scale,worst);
                if(!std::isfinite(worst) || worst>.001) throw std::runtime_error("Input conditioning inverse changed near-neutral HDR");
                context->UpdateSubresource(color.Get(),0,nullptr,colors.data(),size*16,0);
                diagnosticOutput(nullptr);
                if(conditioning(4)==0) throw std::runtime_error("Input conditioning survived diagnostic shutdown");
                if(toneMap(1)==0) throw std::runtime_error("Tone-map diagnostic survived shutdown");
            }
            render(3,nullptr);
        }
        std::puts("PASS: real NGX evaluation, neutral SDR/HDR, live intensity, Default/A/B/C controls, alpha, Off and resize. Synthetic probe is not an image-quality verdict.");
        return 0;
    } catch(const std::exception& e) { std::printf("FAIL: %s\n",e.what()); return 1; }
}
