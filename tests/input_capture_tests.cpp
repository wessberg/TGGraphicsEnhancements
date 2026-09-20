// SPDX-License-Identifier: MIT
#include "fsr_input_capture.hpp"
#include <iostream>
#include <vector>

namespace tgfg {
State& state() { static State instance;return instance; }
void log(const std::string& text) { std::cout<<text<<'\n'; }
}
static void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
int main() {
    try {
        using namespace tgfg;
        const auto root=std::filesystem::current_path()/("input-capture-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        state().root=(root/"Native").wstring();
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&context)),"Create WARP device");
        const DXGI_FORMAT formats[]={DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT};
        std::array<ComPtr<ID3D11Texture2D>,3> textures;
        // Odd width deliberately exercises removal of staging RowPitch padding.
        D3D11_TEXTURE2D_DESC desc{};desc.Width=7;desc.Height=5;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
        for(size_t i=0;i<3;++i) {
            desc.Format=formats[i];
            require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&textures[i])),"Create input texture");
        }
        FsrInputCapture capture;
        for(uint32_t n=0;n<3;++n) {
            for(size_t i=0;i<3;++i) {
                std::array<uint32_t,35> pixels;
                for(size_t p=0;p<pixels.size();++p) pixels[p]=uint32_t(n*1000+i*100+p);
                context->UpdateSubresource(textures[i].Get(),0,nullptr,pixels.data(),7*4,0);
            }
            capture.tick(device.Get(),context.Get(),true,100+n,textures[0].Get(),textures[1].Get(),textures[2].Get(),"\"test\":true");
            require(!std::filesystem::exists(root/"diagnostics"),"Readback must not write while capturing");
        }
        require(!capture.wants_frame(),"Capture must stop at three frames");
        context->Flush();
        for(unsigned i=0;i<5000 && capture.started();++i) {
            capture.tick(device.Get(),context.Get(),true,0,nullptr,nullptr,nullptr,"");Sleep(1);
        }
        require(!capture.started() && !capture.wants_frame(),"Readback must complete and release its textures");
        size_t directories=0;
        const char* names[]={"color","depth","motion"};
        for(const auto& entry:std::filesystem::directory_iterator(root/"diagnostics")) {
            ++directories;
            for(uint32_t n=0;n<3;++n) {
                require(std::filesystem::exists(entry.path()/(std::to_string(n)+".json")),"Missing manifest");
                for(size_t i=0;i<3;++i) {
                    const auto file=entry.path()/(std::to_string(n)+"-"+names[i]+".bin");
                    require(std::filesystem::file_size(file)==35*4,"Unexpected row padding in file");
                    std::array<uint32_t,35> pixels{};
                    std::ifstream stream(file,std::ios::binary);stream.read(reinterpret_cast<char*>(pixels.data()),sizeof(pixels));
                    for(size_t p=0;p<pixels.size();++p)
                        require(pixels[p]==n*1000+i*100+p,"Frame contents overwritten or row order incorrect");
                }
            }
        }
        require(directories==1,"Expected one capture directory");
        capture.tick(device.Get(),context.Get(),true,104,textures[0].Get(),textures[1].Get(),textures[2].Get(),"\"test\":true");
        require(!capture.started(),"Completed capture must remain idle");
        capture.tick(device.Get(),context.Get(),false,0,nullptr,nullptr,nullptr,"");
        require(capture.wants_frame(),"Disabling must permit rearming");
        std::cout<<"315 row-packed values from three distinct GPU frames verified\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
