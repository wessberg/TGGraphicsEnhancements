// RealSkin's head/body material arithmetic, independently evaluated from the shipped GBuffer variants.
// Included after scene warm-up; own camera rays and raw material views, never a denoised lighting comparison.
auto skinMaterialChecks=[&] {
    if(!RegisterSharedTexture || !SetMaterialPrograms) { expect(false, "skin material exports available"); return; }
    const bool savedOwn=ownRays; const float savedBoost=reflectionBoost;
    ownRays=true; reflectionBoost=0;
    std::vector<ComPtr<ID3D11Texture2D>> keep;
    auto texture=[&](uint32_t rgba, bool srgb=false) {
        std::vector<uint32_t> px(64*64, rgba);
        D3D11_TEXTURE2D_DESC d{64,64,1,1,srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,{1,0},D3D11_USAGE_DEFAULT,D3D11_BIND_SHADER_RESOURCE,0,0};
        D3D11_SUBRESOURCE_DATA initial{px.data(),256,0}; ComPtr<ID3D11Texture2D> t;
        expect(SUCCEEDED(device->CreateTexture2D(&d,&initial,&t)), "skin check texture created"); keep.push_back(t);
        TGPT_SharedTexture info{sizeof(TGPT_SharedTexture),TGPT_ABI};
        const auto result=RegisterSharedTexture(uint64_t(reinterpret_cast<uintptr_t>(t.Get())),&info);
        expect(result>=0, "skin check reads the shared texture"); return result>=0 ? info.handle : -1;
    };
    const int grey=texture(0xFF808080,true), overlay=texture(0xFFE6E6E6,true);
    const int mask=texture(0x8080FF00), regions=texture(0x00008040), cavity=texture(0xFFBF801A);
    const int face=texture(0x00400000), earNeck=texture(0x00004020), tattoo=texture(0x00000040);
    const int tilt=texture(0xFFFF80BF);
    std::vector<uint32_t> rows;
    auto record=[&](bool head, bool ink, bool bumped) {
        const std::vector<int32_t> maps={grey,mask,bumped ? tilt : -1,-1,regions,head ? overlay : -1,-1,
            head ? cavity : -1,head ? face : -1,head ? earNeck : -1,ink ? tattoo : -1,-1};
        const std::vector<std::array<float,4>> values={
            {1,1,1,head ? 1.f : 0.f}, {0.2f,0.8f,2.f,0.1f}, {0.2f,-0.1f,0.3f,0.4f},
            {0,0,0.1f,0}, {0.3f,-0.2f,0.03f,0}, {0.5f,0,0.4f,25}, {1,2,1,0},
            {0.6f,0.2f,0.1f,0.5f}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0}};
        const uint32_t start=uint32_t(rows.size()/4); uint32_t present=0;
        for(size_t i=0;i<maps.size();++i) if(maps[i]>=0) present|=1u<<i;
        rows.insert(rows.end(),{TGPT_PROGRAM_SKIN,12,12,present});
        for(auto h:maps) rows.push_back(uint32_t(h));
        for(const auto& v:values) for(float f:v) { uint32_t u; std::memcpy(&u,&f,4); rows.push_back(u); }
        return start;
    };
    std::vector<TGPT_Material> list=materials;
    auto add=[&](bool head,bool ink,bool bumped) {
        auto m=materials[0]; m.program=TGPT_PROGRAM_SKIN; m.programRow=record(head,ink,bumped);
        m.baseColor[0]=m.baseColor[1]=m.baseColor[2]=1; m.metallic=0; m.smoothness=0;
        m.albedoTile=-1; m.colorTexture=m.maskTexture=m.normalTexture=m.emissiveTexture=-1;
        list.push_back(m); return uint32_t(list.size()-1);
    };
    const auto body=add(false,false,false), head=add(true,false,false), inked=add(true,true,false), bumped=add(false,false,true);
    // Dissolve3D's uncut base: its own tint, graph normal strength and mask-alpha times Smoothness.
    auto dissolve=materials[0]; dissolve.program=TGPT_PROGRAM_LIT; dissolve.albedoTile=-1;
    dissolve.baseColor[0]=0.7f; dissolve.baseColor[1]=0.5f; dissolve.baseColor[2]=0.3f;
    dissolve.colorTexture=grey; dissolve.normalTexture=tilt; dissolve.normalScale=0.5f;
    dissolve.maskTexture=mask; dissolve.maskRemap=0xCC000000u; // metal 0, smoothness 0..0.8
    dissolve.textureFlags=TGPT_TEXTURE_GRAPH_NORMAL|TGPT_TEXTURE_MASK_OCCLUSION|12u;
    const uint32_t dissolveId=uint32_t(list.size()); list.push_back(dissolve);
    expect(SetMaterialPrograms(0,rows.data(),uint32_t(rows.size()/4))==0,"skin records set");
    expect(SetMaterials(0,list.data(),uint32_t(list.size()))==0,"skin materials set");
    const M4 world=mul(translate_scale({0,1.02f,2},{2,2,1}),rot_x(-pi/2));
    const Camera camera=make_camera({0,1.02f,-1},{0,1.02f,2},60.f,float(W)/float(H));
    struct Sample { double rgb[3]; int rough; };
    auto sample=[&](uint32_t material,uint32_t view) {
        TGPT_Instance panel{}; panel.mesh=cardHandle; panel.mask=0xFF;
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) panel.transform[r*4+c]=world.m[r][c];
        const uint32_t ids[4]={material,material,material,material};
        expect(SetScene(&panel,1,ids,4)==0,"skin panel set");
        for(uint32_t f=0;f<7;++f) run(view,camera,camera,f ? 12400+f : 0,TGPT_OPTION_NO_DENOISER);
        const auto px=readback(); Sample s{}; int count=0;
        for(int y=int(H)/2-8;y<=int(H)/2+8;++y) for(int x=int(W)/2-8;x<=int(W)/2+8;++x) {
            const size_t p=(size_t(y)*W+x)*3; for(int c=0;c<3;++c) s.rgb[c]+=px[p+c]; ++count;
        }
        for(double& c:s.rgb) c/=count;
        ReadStatus(&status); s.rough=int((status.pickSurface>>16)&255); return s;
    };
    const auto b=sample(body,TGPT_VIEW_ALBEDO), h=sample(head,TGPT_VIEW_ALBEDO), t=sample(inked,TGPT_VIEW_ALBEDO);
    const auto n=sample(bumped,TGPT_VIEW_NORMALS);
    const double base=0.2158605001, blend=0.7912979403, q=128.0/255, r=64.0/255, ear=64.0/255, neck=32.0/255;
    const double headColor=base*(0.6+0.8*blend)*q*std::pow(191.0/255,2);
    const double bodySmooth=0.2+0.6*q*q+(1-r-q)*0.1+0.03;
    const double headSmooth=0.2+0.6*q*q+0.03+r*0.1+ear*0.3-neck*0.2;
    std::printf("      RealSkin body/head RGB %.5f/%.5f (%.5f/%.5f), roughness %d/%d (%.1f/%.1f); tattoo %.5f %.5f %.5f; normal %.4f %.4f %.4f\n",
        b.rgb[0],h.rgb[0],base,headColor,b.rough,h.rough,255*(1-bodySmooth),255*(1-headSmooth),t.rgb[0],t.rgb[1],t.rgb[2],n.rgb[0]*2-1,n.rgb[1]*2-1,n.rgb[2]*2-1);
    expect(std::fabs(b.rgb[0]-base)<0.004 && std::fabs(h.rgb[0]-headColor)<0.004,
        "RealSkin head overlay and cavity use the mask's green blue alpha, while body keeps its diffuse map");
    expect(std::abs(b.rough-int(std::lround(255*(1-bodySmooth))))<=2 && std::abs(h.rough-int(std::lround(255*(1-headSmooth))))<=2,
        "RealSkin region masks, head ear/neck masks and unmasked smoothness match the game's values");
    bool tattooMatches=true; const double colors[3]={0.6,0.2,0.1};
    for(int c=0;c<3;++c) tattooMatches &= std::fabs(t.rgb[c]-(headColor*(1-r*0.5)+colors[c]*r*0.5))<0.004;
    expect(tattooMatches,"RealSkin tattoo uses its own color and opacity after the head cavity");
    // 191/255 unpacked x, half strength; Shader Graph moves z halfway from sqrt(1-x*x-y*y) to one.
    const double nx=(2*191.0/255-1)*0.5, ny=(2*128.0/255-1)*0.5;
    const double nz=0.5+0.5*std::sqrt(1-4*nx*nx-4*ny*ny), len=std::sqrt(nx*nx+ny*ny+nz*nz);
    expect(std::fabs((n.rgb[0]*2-1)-nx/len)<0.012 && std::fabs(std::fabs(n.rgb[2]*2-1)-nz/len)<0.012,
        "RealSkin normal strength follows the game's graph normal, not Lit's normal scale");
    const auto d=sample(dissolveId,TGPT_VIEW_ALBEDO), dn=sample(dissolveId,TGPT_VIEW_NORMALS);
    expect(std::fabs(d.rgb[0]-base*0.7)<0.004 && std::fabs(d.rgb[1]-base*0.5)<0.004 && std::fabs(d.rgb[2]-base*0.3)<0.004 &&
           std::abs(d.rough-int(std::lround(255*(1-q*0.8))))<=2 && std::fabs((dn.rgb[0]*2-1)-nx/len)<0.012,
        "Dissolve3D underlying surface uses its tint, graph normal strength and alpha times Smoothness");
    expect(SetMaterialPrograms(0,nullptr,0)==0,"skin records cleared");
    expect(SetMaterials(0,materials.data(),uint32_t(materials.size()))==0,"original materials restored after skin checks");
    expect(SetScene(instances.data(),uint32_t(instances.size()),indices.data(),uint32_t(indices.size()))==0,"original scene restored after skin checks");
    ownRays=savedOwn; reflectionBoost=savedBoost;
};
skinMaterialChecks();
if(GetEnvironmentVariableA("TGPT_TEST_SKIN_MATERIAL_ONLY",nullptr,0)>0) {
    std::printf(failures ? "FAILED %d checks\n" : "skin material checks passed\n",failures); return failures ? 1 : 0;
}
