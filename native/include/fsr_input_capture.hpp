// SPDX-License-Identifier: MIT
#pragma once
#include "sl_runtime.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace tgfg {
// Optional, bounded readback of the actual FSR inputs. Queue all three frames before
// reading any of them, so disk IO does not change the motion between captured frames.
// Failure disables only this diagnostic, never presentation or frame generation.
class FsrInputCapture {
    struct Texture {
        ComPtr<ID3D11Texture2D> staging;
        D3D11_TEXTURE2D_DESC desc{};
        UINT bytesPerPixel=0;
    };
    struct Snapshot {
        std::array<Texture,3> textures;
        ComPtr<ID3D11Query> ready;
        std::string metadata;
        uint32_t id=0;
    };
    std::array<Snapshot,3> frames_;
    size_t count_=0;
    uint64_t bytes_=0;
    bool done_=false;
    static void check(HRESULT hr) {
        if(FAILED(hr)) throw std::runtime_error("D3D11 readback failed ("+std::to_string(uint32_t(hr))+")");
    }
    void discard() { frames_={};count_=0;bytes_=0; }
public:
    bool wants_frame() const { return !done_ && count_<frames_.size(); }
    bool started() const { return count_!=0; }
    void tick(ID3D11Device* device,ID3D11DeviceContext* context,bool armed,
              uint32_t id,ID3D11Texture2D* color,ID3D11Texture2D* depth,
              ID3D11Texture2D* motion,const std::string& metadata) {
        if(!armed) { if(count_) discard();done_=false;return; }
        if(done_) return;
        try {
            if(count_<frames_.size()) {
                if(!color || !depth || !motion) { if(count_) discard();return; }
                if(count_ && frames_[count_-1].id==id) return;
                auto& frame=frames_[count_];frame.id=id;frame.metadata=metadata;
                ID3D11Texture2D* sources[]={color,depth,motion};
                for(size_t i=0;i<frame.textures.size();++i) {
                    auto& t=frame.textures[i];sources[i]->GetDesc(&t.desc);
                    switch(t.desc.Format) {
                    case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM:
                    case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R32_FLOAT:
                    case DXGI_FORMAT_R16G16_FLOAT: t.bytesPerPixel=4;break;
                    default: throw std::runtime_error("unsupported capture format");
                    }
                    bytes_+=uint64_t(t.desc.Width)*t.desc.Height*t.bytesPerPixel;
                    if(bytes_>384ull*1024*1024) throw std::runtime_error("capture exceeds 384 MiB limit");
                    auto desc=t.desc;desc.Usage=D3D11_USAGE_STAGING;
                    desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                    check(device->CreateTexture2D(&desc,nullptr,&t.staging));
                    context->CopyResource(t.staging.Get(),sources[i]);
                }
                D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
                check(device->CreateQuery(&query,&frame.ready));context->End(frame.ready.Get());
                if(++count_==1) log("FSR input capture started: three source frames, once per arming");
                return; // Never read back on one of the three captured presents.
            }
            for(const auto& frame:frames_) {
                const auto hr=context->GetData(frame.ready.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if(hr==S_FALSE) return;
                check(hr);
            }
            const auto directory=std::filesystem::path(state().root).parent_path()/L"diagnostics"/
                (L"fsr-inputs-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
            std::filesystem::create_directories(directory);
            const char* names[]={"color","depth","motion"};
            for(size_t n=0;n<frames_.size();++n) {
                const auto& frame=frames_[n];
                std::ostringstream json;json.precision(9);
                json<<"{\"version\":2,\"frameId\":"<<frame.id<<","<<frame.metadata<<",\"textures\":{";
                for(size_t i=0;i<frame.textures.size();++i) {
                    const auto& t=frame.textures[i];
                    const std::string filename=std::to_string(n)+"-"+names[i]+".bin";
                    std::ofstream file(directory/filename,std::ios::binary);
                    file.exceptions(std::ios::badbit|std::ios::failbit);
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    check(context->Map(t.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped));
                    try {
                        for(UINT y=0;y<t.desc.Height;++y)
                            file.write(static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch,
                                       size_t(t.desc.Width)*t.bytesPerPixel);
                    } catch(...) { context->Unmap(t.staging.Get(),0);throw; }
                    context->Unmap(t.staging.Get(),0);file.close();
                    if(i) json<<",";
                    json<<"\""<<names[i]<<"\":{\"file\":\""<<filename<<"\",\"width\":"<<t.desc.Width
                        <<",\"height\":"<<t.desc.Height<<",\"dxgiFormat\":"<<uint32_t(t.desc.Format)<<"}";
                }
                json<<"}}\n";
                std::ofstream manifest(directory/(std::to_string(n)+".json"));
                manifest.exceptions(std::ios::badbit|std::ios::failbit);manifest<<json.str();manifest.close();
            }
            log("FSR input capture saved: "+directory.string()+" (three frames; diagnostic now idle)");
            done_=true;discard();
        } catch(const std::exception& error) {
            log(std::string("FSR input capture stopped: ")+error.what());done_=true;discard();
        }
    }
};
} // namespace tgfg
