// SPDX-License-Identifier: MIT
// Harness-only synchronous readbacks. Disabled during normal play; never use their frame times as performance data.
struct Diagnostics {
    static constexpr uint32_t width=64,height=36,stages=9;
    std::filesystem::path folder;
    std::ofstream metadata;
    ComPtr<ID3D11ComputeShader> shader;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11Texture2D> reduced,readback;
    ComPtr<ID3D11UnorderedAccessView> uav;
    std::vector<float> samples;
    uint32_t capturedFrames=0;
    bool capture_guides_now() const {
        return active() && (capturedFrames==8 || capturedFrames==9 || capturedFrames==16 || capturedFrames==17 || capturedFrames==24 || capturedFrames==25);
    }
    // Six frames per diagnostic take, at the original guide resolution. Keep the source bits
    // as well as the converted bits so orientation, viewport and marker decoding are testable.
    void guide_image(const TGNR_Frame& f,const char* name,ID3D11Resource* source,DlssRect rect,bool flip) noexcept {
        if(!capture_guides_now()) return;
        try {
            ComPtr<ID3D11Texture2D> texture;
            check(source->QueryInterface(IID_PPV_ARGS(&texture)),"NR full guide texture");
            D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
            uint32_t bytes=0;
            switch(desc.Format) {
            case DXGI_FORMAT_R32G32B32A32_TYPELESS: case DXGI_FORMAT_R32G32B32A32_FLOAT: bytes=16; break;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT:
            case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: bytes=8; break;
            case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
            case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT:
            case DXGI_FORMAT_R11G11B10_FLOAT: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: bytes=4; break;
            default: throw std::runtime_error(std::string("Unsupported full-guide format ")+std::to_string(desc.Format)+" for "+name);
            }
            if(desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || !rect.width || !rect.height ||
               rect.x>desc.Width || rect.y>desc.Height || rect.width>desc.Width-rect.x || rect.height>desc.Height-rect.y)
                throw std::runtime_error("Invalid full-guide diagnostic region.");
            // Depth/stencil copies must cover the entire subresource. Crop the mapped rows on CPU.
            desc.MipLevels=1; desc.BindFlags=desc.MiscFlags=0;
            desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            check(host->game_device()->CreateTexture2D(&desc,nullptr,&staging),"NR full guide staging");
            auto context=host->game_context(); context->CopySubresourceRegion(staging.Get(),0,0,0,0,source,0,nullptr);
            std::ofstream file(folder/("guide-"+std::to_string(f.frame)+"-"+name+".bin"),std::ios::binary);
            const uint32_t header[]{0x3147524e,1,f.frame,f.camera,rect.width,rect.height,uint32_t(desc.Format),bytes,flip?1u:0u,f.flags,rect.x,rect.y};
            file.write(reinterpret_cast<const char*>(header),sizeof(header));
            D3D11_MAPPED_SUBRESOURCE map{};
            check(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map),"NR full guide map");
            for(uint32_t y=0;y<rect.height;++y) file.write(static_cast<const char*>(map.pData)+size_t(y+rect.y)*map.RowPitch+size_t(rect.x)*bytes,size_t(rect.width)*bytes);
            context->Unmap(staging.Get(),0);
            file.close(); if(!file) throw std::runtime_error("Cannot write full guide capture.");
        } catch(const std::exception& e) { fail(e); }
    }
    bool replayRequested=false,replayCaptured=false,replayReset=false,replayResetEveryFrame=false;
    TGNR_Frame replayFrame{};
    float inputScale=1; // Harness-only reversible HDR conditioning; normal play always uses 1.
    uint32_t toneMap=0; // 0: current peak Reinhard; 1: per-channel Reinhard. Harness only.
    bool toneMapReset=false;
    void set_tone_map(uint32_t value) {
        if(!active() || replayCaptured || value>1) throw std::runtime_error("NR tone-map comparison requires active diagnostics and a supported mode.");
        toneMap=value; toneMapReset=true; // Both comparison modes start from the same empty history.
    }
    void set_input_scale(float value) {
        if(!active() || replayCaptured || !std::isfinite(value) || value<0.125f || value>8)
            throw std::runtime_error("NR input scale requires active diagnostics, no retained replay and a value in [0.125,8].");
        inputScale=value;
    }
    bool active() const { return !folder.empty(); }
    void set_replay(uint32_t flags) {
        if((flags!=0 && flags!=1 && flags!=3 && flags!=7) || (flags && !active()))
            throw std::runtime_error("Replay requires active NR stage diagnostics and valid reset flags.");
        replayRequested=(flags&1)!=0; replayReset=(flags&2)!=0; replayResetEveryFrame=(flags&4)!=0;
        replayCaptured=false; replayFrame={};
    }
    bool replaying(const TGNR_Frame& f) {
        if(!active() || !replayCaptured) return false;
        const auto& s=replayFrame;
        if(s.camera==f.camera && s.width==f.width && s.height==f.height && s.renderWidth==f.renderWidth &&
           s.renderHeight==f.renderHeight && (s.flags&~TGNR_RESET)==(f.flags&~TGNR_RESET)) return true;
        fail(std::runtime_error("Replay camera, size or colour/depth convention changed."));
        return false;
    }
    void configure(const wchar_t* path) {
        set_replay(0); inputScale=1; toneMap=0; toneMapReset=false;
        folder.clear(); metadata.close(); metadata.clear(); samples.clear(); capturedFrames=0;
        if(!path || !*path) return;
        std::filesystem::path next(path);
        if(!next.is_absolute() || std::filesystem::exists(next))
            throw std::runtime_error("NR diagnostic output must be a new absolute directory.");
        std::filesystem::create_directories(next);
        metadata.open(next/L"frames.csv");
        if(!metadata) throw std::runtime_error("Cannot open NR diagnostic metadata.");
        metadata<<"frame,camera,width,height,flags,reset,strength,model,tone,detail,skin,mask,motionScaleX,motionScaleY,jitterX,jitterY,exposurePresent,replaySourceFrame,replayReset,inputScale,replayResetEveryFrame,passes,toneMap,nearPlane,farPlane\n";
        metadata<<std::setprecision(9);
        folder=next;
        log("NR stage diagnostics enabled; synchronous readbacks invalidate frame-time comparisons.");
    }
    void fail(const std::exception& e) noexcept {
        try { std::ofstream(folder/L"error.txt")<<e.what(); log(std::string("NR stage diagnostics stopped: ")+e.what()); }
        catch(...) {}
        replayRequested=replayCaptured=replayReset=replayResetEveryFrame=false; replayFrame={}; inputScale=1; toneMap=0; toneMapReset=false;
        folder.clear(); metadata.close();
    }
    void prepare() {
        if(shader) return;
        static const char* code=R"NRDIAG(
cbuffer Params : register(b0) { uint2 origin,extent,grid; uint mode,flip; float4 scale; };
Texture2D<float4> source : register(t0);
RWTexture2D<float4> result : register(u0);
groupshared float4 sums[64];
[numthreads(64,1,1)] void Reduce(uint3 group:SV_GroupID,uint lane:SV_GroupIndex) {
 uint2 lo=group.xy*extent/grid,hi=(group.xy+1)*extent/grid;
 uint2 span=hi-lo; uint count=span.x*span.y; float4 sum=mode==5 ? float4(3.402823e38,-3.402823e38,0,0):0;
 for(uint i=lane;i<count;i+=64) {
  uint2 p=lo+uint2(i%span.x,i/span.x);
  if(flip) p.y=extent.y-1-p.y;
  float4 v=source.Load(int3(origin+p,0));
  if(mode==5) {
   // True pixel extrema and fractions, not extrema of averaged colour. Nonfinite pixels
   // are already rejected by the ordinary stage; include them in the fallback count too.
   if(!all(isfinite(v.rgb))) { sum.w+=1; continue; }
   sum.x=min(sum.x,min(v.r,min(v.g,v.b))); sum.y=max(sum.y,max(v.r,max(v.g,v.b)));
   sum.z+=any(v.rgb<0) ? 1:0;
   float3 decoded=lerp(v.rgb/12.92,pow(max((v.rgb+0.055)/1.055,0),2.4),step(0.04045,v.rgb));
   sum.w+=(!all(isfinite(decoded)) || max(decoded.r,max(decoded.g,decoded.b))>=1) ? 1:0;
  }
  else if(mode==2) sum+=v;
  else if(!all(isfinite(v.rgb))) sum.w+=1;
  else if(mode==1 || mode==4) { float2 mv=v.xy*scale.xy; sum.xyz+=float3(mode==4 ? mv:abs(mv),length(mv)); }
  else sum.xyz+=v.rgb;
 }
 sums[lane]=sum; GroupMemoryBarrierWithGroupSync();
 for(uint s=32;s>0;s/=2) {
  if(lane<s) {
   if(mode==5) sums[lane]=float4(min(sums[lane].x,sums[lane+s].x),max(sums[lane].y,sums[lane+s].y),sums[lane].zw+sums[lane+s].zw);
   else sums[lane]+=sums[lane+s];
  }
  GroupMemoryBarrierWithGroupSync();
 }
 if(lane==0) result[group.xy]=mode==5 ? float4(sums[0].xy,sums[0].zw/max(count,1)):sums[0]/max(count,1);
}
)NRDIAG";
        auto device=host->game_device();
        ComPtr<ID3DBlob> blob,error;
        auto hr=D3DCompile(code,std::strlen(code),nullptr,nullptr,nullptr,"Reduce","cs_5_0",0,0,&blob,&error);
        if(FAILED(hr)) throw DxError(hr,error ? static_cast<const char*>(error->GetBufferPointer()) : "NR diagnostic shader");
        // Assign shader last so a failed resource creation can be retried safely.
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=48; cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&cb,nullptr,&constants),"NR diagnostic constants");
        D3D11_TEXTURE2D_DESC d{}; d.Width=width; d.Height=height; d.MipLevels=d.ArraySize=1;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
        check(device->CreateTexture2D(&d,nullptr,&reduced),"NR diagnostic reduction");
        check(device->CreateUnorderedAccessView(reduced.Get(),nullptr,&uav),"NR diagnostic UAV");
        d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check(device->CreateTexture2D(&d,nullptr,&readback),"NR diagnostic staging");
        check(device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shader),"NR diagnostic shader");
    }
    void sample(ID3D11Resource* source,DlssRect rect,bool flip,uint32_t mode=0,float sx=1,float sy=1) noexcept {
        if(!active()) return;
        try {
            prepare(); auto context=host->game_context();
            ComPtr<ID3D11Texture2D> texture;
            check(source->QueryInterface(IID_PPV_ARGS(&texture)),"NR diagnostic source");
            D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
            ComPtr<ID3D11ShaderResourceView> srv;
            if(desc.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || desc.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB)
                check(host->game_device()->CreateShaderResourceView(source,nullptr,&srv),"NR diagnostic sRGB view");
            else {
                const auto readAs=(mode==2 || mode==3) && (desc.Format==DXGI_FORMAT_R32_FLOAT || desc.Format==DXGI_FORMAT_R32_TYPELESS)
                    ? ReadAs::Depth : (mode && mode!=5) ? ReadAs::Motion : ReadAs::Color;
                srv=typed_srv(host->game_device(),source,readAs,&desc);
            }
            if(!rect.width || !rect.height || rect.x>desc.Width || rect.y>desc.Height ||
               rect.width>desc.Width-rect.x || rect.height>desc.Height-rect.y)
                throw std::runtime_error("NR diagnostic source region is invalid.");
            struct Params { uint32_t x,y,w,h,gx,gy,mode,flip; float sx,sy,z,w2; };
            const uint32_t gx=mode==2 ? 1:width,gy=mode==2 ? 1:height;
            const Params params{rect.x,rect.y,rect.width,rect.height,gx,gy,mode,flip?1u:0u,sx,sy,0,0};
            context->OMSetRenderTargets(0,nullptr,nullptr);
            context->UpdateSubresource(constants.Get(),0,nullptr,&params,0,0);
            ID3D11Buffer* cb=constants.Get(); context->CSSetConstantBuffers(0,1,&cb);
            context->CSSetShader(shader.Get(),nullptr,0);
            ID3D11ShaderResourceView* input=srv.Get(); context->CSSetShaderResources(0,1,&input);
            ID3D11UnorderedAccessView* output=uav.Get(); context->CSSetUnorderedAccessViews(0,1,&output,nullptr);
            context->Dispatch(gx,gy,1);
            input=nullptr; output=nullptr;
            context->CSSetShaderResources(0,1,&input); context->CSSetUnorderedAccessViews(0,1,&output,nullptr);
            context->CopyResource(readback.Get(),reduced.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            // Reserve before Map: allocation failure must not leave a mapped D3D11 resource.
            const size_t offset=samples.size(); samples.resize(offset+size_t(gx)*gy*4);
            check(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped),"NR diagnostic readback");
            for(uint32_t y=0;y<gy;++y) std::memcpy(samples.data()+offset+size_t(y)*gx*4,
                static_cast<const char*>(mapped.pData)+size_t(y)*mapped.RowPitch,size_t(gx)*16);
            context->Unmap(readback.Get(),0);
        } catch(const std::exception& e) { fail(e); }
    }
    void finish(const TGNR_Frame& f,bool reset,float sx,float sy,ID3D11Resource* exposure,float usedInputScale) noexcept {
        if(!active()) return;
        try {
            if(exposure) sample(exposure,{0,0,1,1},false,2);
            else samples.insert(samples.end(),4,std::numeric_limits<float>::quiet_NaN());
            if(!active()) return;
            if(samples.size()!=size_t(width)*height*4*stages+4) throw std::runtime_error("NR diagnostic stage count mismatch.");
            const uint32_t header[]={0x3144524e,4,f.frame,f.camera,width,height,stages,f.flags}; // NRD1, little endian
            std::ofstream file(folder/("nr-"+std::to_string(f.frame)+".bin"),std::ios::binary);
            file.write(reinterpret_cast<const char*>(header),sizeof(header));
            file.write(reinterpret_cast<const char*>(samples.data()),std::streamsize(samples.size()*sizeof(float)));
            file.close(); if(!file) throw std::runtime_error("Cannot write NR diagnostic frame.");
            metadata<<f.frame<<','<<f.camera<<','<<f.width<<','<<f.height<<','<<f.flags<<','<<reset<<','
                <<f.strength<<','<<f.model<<','<<f.localTone<<','<<f.localStructure<<','<<f.skinStructure<<','<<f.autoMask<<','
                <<sx<<','<<sy<<','<<f.jitterX<<','<<f.jitterY<<','<<(exposure?1:0)<<','<<(replayCaptured?replayFrame.frame:0)<<','<<replayReset<<','<<usedInputScale<<','<<replayResetEveryFrame<<','<<f.passes<<','<<toneMap<<','<<f.nearPlane<<','<<f.farPlane<<'\n';
            metadata.flush(); if(!metadata) throw std::runtime_error("Cannot write NR diagnostic metadata.");
            ++capturedFrames;
        } catch(const std::exception& e) { fail(e); }
    }
};
