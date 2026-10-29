// Reproduce the shipped Unlit VFX ForwardOnly output at D3D11's actual MRT boundary.
// It is additive, no depth writes, and writes (2,0) to motion even at alpha zero.
// A small temporal gather demonstrates history rejection, not an emulation of DLSS.
{
    const char* shader=R"(
float4 vs(uint i:SV_VertexID):SV_Position { float2 p=float2((i<<1)&2,i&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); }
float4 flame(float4 p) { if(any(p.xy<8)||any(p.xy>=24)) discard; float f=length(p.xy-16)<2 ? 0.2 : 0; return float4(f,f,f,0); }
struct Out { float4 colour:SV_Target0; float4 motion:SV_Target1; };
Out marked(float4 p:SV_Position) { Out o; o.colour=flame(p); o.motion=float4(2,0,0,1); return o; }
float4 colour(float4 p:SV_Position):SV_Target0 { return flame(p); }
)";
    auto vcode=compile(shader,"vs","vs_5_0"), mcode=compile(shader,"marked","ps_5_0"), ccode=compile(shader,"colour","ps_5_0");
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps[2];
    device->CreateVertexShader(vcode->GetBufferPointer(),vcode->GetBufferSize(),nullptr,&vs);
    device->CreatePixelShader(mcode->GetBufferPointer(),mcode->GetBufferSize(),nullptr,&ps[0]);
    device->CreatePixelShader(ccode->GetBufferPointer(),ccode->GetBufferSize(),nullptr,&ps[1]);
    ComPtr<ID3D11Texture2D> tex[2], staging;
    ComPtr<ID3D11RenderTargetView> views[2];
    D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=32; d.MipLevels=d.ArraySize=1;
    d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_RENDER_TARGET;
    for(int i=0;i<2;++i) { device->CreateTexture2D(&d,nullptr,&tex[i]); device->CreateRenderTargetView(tex[i].Get(),nullptr,&views[i]); }
    d.BindFlags=0; d.Usage=D3D11_USAGE_STAGING; d.CPUAccessFlags=D3D11_CPU_ACCESS_READ; device->CreateTexture2D(&d,nullptr,&staging);
    D3D11_BLEND_DESC bd{}; bd.IndependentBlendEnable=TRUE;
    for(auto& t:bd.RenderTarget) { t.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL; t.SrcBlend=t.SrcBlendAlpha=D3D11_BLEND_ONE; t.DestBlend=t.DestBlendAlpha=D3D11_BLEND_ONE; t.BlendOp=t.BlendOpAlpha=D3D11_BLEND_OP_ADD; }
    bd.RenderTarget[0].BlendEnable=TRUE;
    ComPtr<ID3D11BlendState> blend; device->CreateBlendState(&bd,&blend);
    ComPtr<ID3D11RasterizerState> rast; D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; device->CreateRasterizerState(&rd,&rast);
    auto pixels=[&](int index) {
        std::vector<float> out(32*32*4); context->CopyResource(staging.Get(),tex[index].Get());
        D3D11_MAPPED_SUBRESOURCE map{}; context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map);
        for(int y=0;y<32;++y) std::memcpy(out.data()+y*128,static_cast<const char*>(map.pData)+y*map.RowPitch,128*sizeof(float));
        context->Unmap(staging.Get(),0); return out;
    };
    double noise[2]{}; int markers[2]{};
    for(int mode=0;mode<2;++mode) {
        std::mt19937 rng(1481); std::uniform_real_distribution<float> U(-0.08f,0.08f);
        std::vector<double> history(1024,0.3); double variance=0;
        for(int frame=0;frame<64;++frame) {
            std::vector<float> wall(4096); for(int j=0;j<1024;++j) { const float n=U(rng); for(int c=0;c<3;++c) wall[j*4+c]=0.3f+n; }
            context->UpdateSubresource(tex[0].Get(),0,nullptr,wall.data(),32*16,0);
            const float backgroundMotion[4]={0.01f,-0.02f,0,0}; context->ClearRenderTargetView(views[1].Get(),backgroundMotion);
            ID3D11RenderTargetView* target[]={views[0].Get(),views[1].Get()}; context->OMSetRenderTargets(2,target,nullptr);
            context->OMSetBlendState(blend.Get(),nullptr,~0u); context->RSSetState(rast.Get());
            D3D11_VIEWPORT vp{0,0,32,32,0,1}; context->RSSetViewports(1,&vp);
            context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(vs.Get(),nullptr,0); context->PSSetShader(ps[mode].Get(),nullptr,0); context->Draw(3,0);
            context->OMSetRenderTargets(0,nullptr,nullptr);
            const auto colorPixels=pixels(0), motionPixels=pixels(1);
            double colourError=0, motionError=0;
            for(int y=8;y<24;++y) for(int x=8;x<24;++x) {
                if(x>=13&&x<=18&&y>=13&&y<=18) continue;
                const int j=y*32+x; bool marker=motionPixels[j*4]>1;
                if(frame==0&&marker) ++markers[mode];
                if(mode==1) motionError=std::max(motionError,double(std::fabs(motionPixels[j*4]-backgroundMotion[0])+std::fabs(motionPixels[j*4+1]-backgroundMotion[1])));
                colourError=std::max(colourError,double(std::fabs(colorPixels[j*4]-wall[j*4])));
                const double h=marker ? colorPixels[j*4] : history[j]*0.875+colorPixels[j*4]*0.125;
                if(frame>=16) variance+=(h-history[j])*(h-history[j]); history[j]=h;
            }
            if(frame==0) {
                expect(colourError<1e-7,"an invisible additive flame border changes no background colour");
                if(mode==1) expect(motionError<1e-7,"colour-only VFX preserves nonzero background motion too");
            }
        }
        noise[mode]=std::sqrt(variance/(48*220))*255;
    }
    std::printf("      transparent quad: %d/%d invisible-border motion markers; temporal frame change %.3f -> %.3f levels with colour-only output\n",markers[0],markers[1],noise[0],noise[1]);
    expect(markers[0]==220 && markers[1]==0,"the shipped motion marker covers the invisible quad; colour-only output leaves the wall's vectors");
    expect(noise[1]<noise[0]*0.15,"keeping background motion removes quad-shaped history rejection");
    context->OMSetBlendState(nullptr,nullptr,~0u);
}
// SPDX-License-Identifier: MIT
// PT-73: transport checks run alone with TGPT_TEST_CANDLE_ONLY, and in the full suite.
{
    std::printf("[candle] coloured slabs under a uniform white environment, own camera rays, no denoiser\n");
    const auto objectsBefore=objects;
    const bool ownBefore=ownRays, sunBefore=sunOn; const float skyBefore=skyIntensity;
    const uint32_t bouncesBefore=bounces;
    ownRays=true; sunOn=false; skyIntensity=1; bounces=1;
    objects.clear(); SetLights(nullptr,0);
    ComPtr<ID3D11Texture2D> whiteSky;
    {
        const uint16_t pixels[4]={float_to_half(1.f),float_to_half(1.f),float_to_half(1.f),float_to_half(1.f)};
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=d.MipLevels=1; d.ArraySize=6;
        d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        D3D11_SUBRESOURCE_DATA init[6]; for(auto& f:init) f={pixels,8,0};
        expect(SUCCEEDED(device->CreateTexture2D(&d,init,&whiteSky)),"uniform white environment made");
        SetSky(uint64_t(uintptr_t(whiteSky.Get())));
    }
    const V3 centre{0,1,0}, scale{0.3f,0.12f,0.3f};
    Camera eye=make_camera({0,1.5f,-0.025f},{0,1.06f,0},60.f,float(W)/H);
    Object drawn{1,translate_scale(centre,scale),{0,0},false,false,3}; drawn.gb2b=95; objects.push_back(drawn);
    TGPT_Instance slab{}; const M4 world=translate_scale(centre,scale);
    for(int r=0;r<3;++r) for(int c=0;c<4;++c) slab.transform[r*4+c]=world.m[r][c];
    slab.mesh=cubeHandle; slab.mask=0xFF; const uint32_t ids[2]={74,74};
    const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_RADIANCE_CACHE;
    auto measure=[&](TGPT_Material mat, uint32_t seed) {
        SetMaterials(74,&mat,1); SetScene(&slab,1,ids,2);
        for(uint32_t i=0;i<8;++i) { run(TGPT_VIEW_INDIRECT_RAW,eye,eye,i,raw); context->Flush(); Sleep(10); }
        std::array<double,3> sum{}; size_t count=0;
        for(uint32_t i=0;i<24;++i) {
            run(TGPT_VIEW_INDIRECT_RAW,eye,eye,seed+i,raw); const auto im=readback();
            for(UINT y=H/2-24;y<H/2+24;++y) for(UINT x=W/2-32;x<W/2+32;++x) {
                for(int c=0;c<3;++c) sum[c]+=im[(size_t(y)*W+x)*3+c]; ++count;
            }
        }
        for(auto& v:sum) v/=double(count); return sum;
    };
    TGPT_Material plain=materials[0]; for(int c=0;c<3;++c) plain.baseColor[c]=1; plain.flags=0; plain.metallic=0;
    const auto reference=measure(plain,800);
    const float colors[5][3]={{0.9f,0.85f,0.7f},{0.6f,0.35f,0.22f},{1,1,1},{0.9f,0.85f,0.7f},{0.6f,0.35f,0.22f}};
    const char* names[]={"bulk cream wax","skin","white","finite cream wax","skin 110 m from scene origin"};
    for(int k=0;k<5;++k) {
        TGPT_Material mat=plain; mat.flags=TGPT_MATERIAL_SUBSURFACE; mat.subsurfaceMask=1.f;
        for(int c=0;c<3;++c) { mat.baseColor[c]=colors[k][c]; mat.subsurface[c]=(k==1 || k==4) ? (c==0 ? 3333.3333f : c==1 ? 6250.f : 11111.1111f) : k==0 ? 1000.f : 100.f; }
        if(k==4) {
            // Same submillimetre skin in the coordinate range of the game's 256 m
            // origin cells. Translation must not change a material's colour.
            const V3 at{-110,110,-110}; const M4 shifted=translate_scale(at,scale);
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) slab.transform[r*4+c]=shifted.m[r][c];
            objects[0].world=shifted;
            eye=make_camera(at+V3{0,.5f,-.025f},at+V3{0,.06f,0},60.f,float(W)/H);
        }
        double want[3]={1,1,1};
        if(k==3) {
            // A finite slab in light from ALL sides returns reflected plus transmitted
            // light. It can exceed its bulk reflectance. Check that with an independent
            // geometric walk, rather than darkening the tracer to enforce the wrong limit.
            double a[3], shape[3]={100,100,100};
            for(int c=0;c<3;++c) { const double srgb=1.055*std::pow(double(colors[k][c]),1.0/2.4)-0.055; a[c]=std::pow((std::round(srgb*255)/255+0.055)/1.055,2.4); want[c]=0; }
            const WalkMedium medium=walk_medium(a,shape); const auto planes=walk_box(centre,scale);
            const double start[3]={centre.x,planes[3].d,centre.z}; std::mt19937_64 rng(771923);
            for(int i=0;i<200000;++i) { double where[3],carry[3]; int face,flights;
                if(walk_reference(planes,start,3,medium,rng,where,face,carry,flights)) for(int c=0;c<3;++c) want[c]+=carry[c]/a[c]/200000;
            }
        }
        const auto got=measure(mat,900+100*k);
        bool ok=true; std::printf("      %s: demodulated reflectance",names[k]);
        for(int c=0;c<3;++c) { double ratio=got[c]/reference[c]; std::printf(" %.5f",ratio); ok&=std::isfinite(ratio)&&std::fabs(ratio/want[c]-1)<0.035; }
        std::printf(" (expected %.5f %.5f %.5f; colour applied once)\n",want[0],want[1],want[2]);
        expect(ok,k==3 ? "finite cream slab returns reflected plus transmitted light as an independent volume walk predicts" :
            "coloured or white bulk slab retains its diffuse colour under even white light within 3.5 percent");
    }
    objects=objectsBefore; ownRays=ownBefore; sunOn=sunBefore; skyIntensity=skyBefore; bounces=bouncesBefore;
    SetScene(instances.data(),uint32_t(instances.size()),indices.data(),uint32_t(indices.size()));
    SetSky(uint64_t(uintptr_t(sky.Get()))); SetLights(&light,1);
    for(uint32_t i=0;i<8;++i) run(TGPT_VIEW_NONE,cam,cam,i);
}
if(GetEnvironmentVariableA("TGPT_TEST_CANDLE_ONLY",nullptr,0)>0) {
    std::printf(failures ? "FAILED %d checks\n" : "candle checks passed\n",failures); return failures ? 1 : 0;
}

