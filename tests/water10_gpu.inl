// SPDX-License-Identifier: MIT
// Only this worker's checks. TGPT_TEST_WATER_SURFACE=1 TGPT_TEST_WATER10_ONLY=1.
// TGPT_TEST_WATER10_PART=body|foam|view|mirror selects one. No denoiser, no full suite.
if(water10Only) {
    ownRays=true; sunOn=false; skyIntensity=1e-6f; reflectionBoost=0; tracedGain=1; rayDistance=500;
    setExposure(1.f); SetLights(nullptr,0);
    const double pi10=3.141592653589793, n10=1.3333;
    const double f10=(n10-1)*(n10-1)/((n10+1)*(n10+1));
    char part10[32]{}; GetEnvironmentVariableA("TGPT_TEST_WATER10_PART",part10,sizeof(part10));
    auto wants10=[&](const char* p) { return !part10[0] || std::strcmp(part10,p)==0; };
    const unsigned count10=128;
    const uint32_t raw10=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_SPATIAL;
    uint32_t serial10=150000;
    TGPT_Material black10{}; black10.baseColor[3]=1; black10.metallic=1;
    black10.albedoTile=black10.emissiveTile=black10.alphaTile=black10.maskTile=-1;
    black10.colorTexture=black10.normalTexture=black10.maskTexture=-1;
    black10.flags=TGPT_MATERIAL_DOUBLE_SIDED;
    TGPT_Material diffuse10=black10; diffuse10.metallic=0; diffuse10.smoothness=0;
    diffuse10.baseColor[0]=diffuse10.baseColor[1]=diffuse10.baseColor[2]=0.4f;
    TGPT_Material emit10=black10; emit10.emissive[0]=emit10.emissive[1]=emit10.emissive[2]=1;
    TGPT_Material all10[3]={black10,diffuse10,emit10}; SetMaterials(12,all10,3);
    TGPT_WaterCaustics w10{}; w10.size=sizeof(w10); w10.abi=TGPT_ABI;
    w10.worldToWater[0]=w10.worldToWater[5]=w10.worldToWater[10]=1;
    w10.bounds[0]=w10.bounds[1]=-8; w10.bounds[2]=w10.bounds[3]=8;
    w10.foamMaps[0]=w10.foamMaps[1]=-1;
    w10.flags=TGPT_WATER_SMOOTHNESS;
    w10.foam[2]=1; w10.foam[3]=1; w10.foamColor[0]=w10.foamColor[1]=w10.foamColor[2]=1;
    w10.scattering[3]=w10.foamColor[3]=1; w10.smoothnessFade[1]=1;
    auto scene10=[&](float bedHeight, uint32_t bedMaterial, bool emitter=false, float bedWidth=16, float bedZ=0) {
        std::vector<TGPT_Instance> ins; std::vector<uint32_t> ids;
        auto add=[&](M4 mat, uint32_t material, uint32_t flags) {
            TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=mat.m[r][c];
            in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.flags=flags; in.mask=0xFF;
            ins.push_back(in); ids.push_back(material);
        };
        add(translate_scale({0,bedHeight,bedZ},{bedWidth,1,bedWidth}),bedMaterial,0);
        add(translate_scale({0,0,0},{16,1,16}),12,TGPT_INSTANCE_WATER);
        if(emitter) add(mul(translate_scale({0,1,1},{1,1,1}),rot_x(pi)),14,0);
        expect(SetScene(ins.data(),uint32_t(ins.size()),ids.data(),uint32_t(ids.size()))==0,"water10 scene");
        expect(SetWaterCaustics(&w10,1)==0,"water10 water");
        objects.clear(); objects.push_back({0,translate_scale({0,bedHeight,bedZ},{bedWidth,1,bedWidth}),{0},false,false});
    };
    auto state10=[&](const char* name,const Camera& camera,float trial,uint32_t view=TGPT_VIEW_NONE) {
        waterTrial=trial; cut=true; run(view,camera,camera,serial10++,raw10); cut=false;
        for(unsigned j=0;j<16;++j) run(view,camera,camera,serial10++,raw10);
        std::vector<float> result(size_t(W)*H*3,0.f);
        const uint64_t before=GetTickCount64();
        for(unsigned j=0;j<count10;++j) {
            run(view,camera,camera,serial10++,raw10);
            const auto frame=readback(); for(size_t k=0;k<result.size();++k) result[k]+=frame[k]/float(count10);
        }
        ReadStatus(&status);
        double mean=0; unsigned pixels=0;
        for(UINT y=H/4;y<3*H/4;++y) for(UINT x=W/4;x<3*W/4;++x) {
            const size_t k=(size_t(y)*W+x)*3; mean+=(result[k]+result[k+1]+result[k+2])/3.0; ++pixels;
        }
        mean/=pixels;
        std::printf("      WATER10 %s: mean %.9g, GPU %.3f ms, %llu ms for %u frames, failed %llu\n",name,mean,status.gpuMs,
                    (unsigned long long)(GetTickCount64()-before),count10,(unsigned long long)status.framesFailed);
        const std::string file=std::string("water10-")+name+".png"; save(result,file.c_str(),true);
        expect(status.framesFailed==0,"water10 no failed frames");
        return mean;
    };
    TGPT_Light lamp10{}; lamp10.id=41; lamp10.type=0; lamp10.radius=0.05f; lamp10.range=20;
    lamp10.color[0]=lamp10.color[1]=lamp10.color[2]=20;
    lamp10.flags=TGPT_LIGHT_NO_RANGE|TGPT_LIGHT_NO_SPECULAR;
    lamp10.position[1]=2;
    const Camera down10=make_camera({0,-0.05f,-0.00001f},{0,-1.05f,0},0.1f,float(W)/float(H));
    if(wants10("body")) {
        w10.extinction[0]=w10.extinction[1]=w10.extinction[2]=0.2f;
        w10.scattering[0]=w10.scattering[1]=w10.scattering[2]=0.1f;
        scene10(-1.05f,12); SetLights(&lamp10,1);
        const double off=state10("body-off",down10,0), on=state10("body-above",down10,2);
        double reference=0;
        for(int j=0;j<10000;++j) {
            const double s=(j+0.5)/10000.0, depth=0.05+s;
            // Paraxial flat interface: a point lamp's irradiance on axis is I*(1-F)/(h+d/n)^2.
            reference+=std::exp(-0.2*s)*0.02/(4*pi10)*20*(1-f10)*std::exp(-0.2*depth)/std::pow(2+depth/n10,2)/10000;
        }
        std::printf("      WATER10 body above reference %.9g, measured/reference %.6f, off/reference %.6f\n",reference,on/reference,off/reference);
        expect(on/reference>0.92 && on/reference<1.10,"water10 lamp through flat water agrees with single-scatter integral");
        expect(off<reference*0.01,"water10 off preserves the unsampled no-specular lamp body");
        lamp10.position[0]=0.5f; lamp10.position[1]=-0.55f; SetLights(&lamp10,1);
        const double submerged=state10("body-submerged",down10,2); reference=0;
        for(int j=0;j<10000;++j) {
            const double s=(j+0.5)/10000.0, distance=std::sqrt(0.25+(s-0.5)*(s-0.5));
            reference+=std::exp(-0.2*s)*0.02/(4*pi10)*20*std::exp(-0.2*distance)/(distance*distance)/10000;
        }
        std::printf("      WATER10 body submerged reference %.9g, measured/reference %.6f\n",reference,submerged/reference);
        expect(submerged/reference>0.92 && submerged/reference<1.10,"water10 submerged lamp agrees with straight single-scatter integral");
    }
    if(wants10("view")) {
        w10.extinction[0]=w10.extinction[1]=w10.extinction[2]=0;
        w10.scattering[0]=w10.scattering[1]=w10.scattering[2]=0;
        // A 45-degree camera in air with a very narrow view: the bent ray's bed hit lies outside 1.5x the
        // straight camera frustum. The previous in_view cull cannot supply this directly visible refracted point.
        const double entryCos=std::sqrt(0.5), waterCos=std::sqrt(1-0.5/(n10*n10));
        const double entryRs=(entryCos-n10*waterCos)/(entryCos+n10*waterCos);
        const double entryRp=(n10*entryCos-waterCos)/(n10*entryCos+waterCos);
        const double entryScale=(1-0.5*(entryRs*entryRs+entryRp*entryRp))/(n10*n10);
        const Camera refracted=make_camera({0,1,-1},{0,0,0},0.1f,float(W)/float(H));
        lamp10.position[0]=0; lamp10.position[1]=2; lamp10.position[2]=float(1.05*entryCos/(n10*waterCos));
        // Isolate a small receiver: a wide diffuse bed sends light back through total internal reflection and
        // onto itself again. That is real extra light, absent from the single-hit analytic reference below.
        // A 4-cm receiver cannot meet those later bed hits (their lateral shift is metres at the critical angle).
        SetLights(&lamp10,1); scene10(-1.05f,13,false,0.04f,lamp10.position[2]);
        const double old=state10("view-lamp-old",refracted,0);
        const double lit=state10("view-lamp",refracted,4);
        const double reference=entryScale*(0.4+0.04/(2*(1+waterCos)))/pi10*20*(1-f10)/std::pow(2+1.05/n10,2);
        expect(old<0.1*reference,"water10 narrow view reproduces the old off-screen landed-light loss");
        std::printf("      WATER10 view lamp reference %.9g, measured/reference %.6f\n",reference,lit/reference);
        expect(lit/reference>0.90 && lit/reference<1.12,"water10 receiver asks lamp beyond the view and agrees with flat reference");
        SetLights(nullptr,0); sunOn=true;
        const double solar=state10("view-sun",refracted,4);
        // Oblique sun: flux crossing a horizontal flat interface is E*cos(theta_air)*(1-F).
        const double ca=sunDir.y, sa=std::sqrt(1-ca*ca), cw=std::sqrt(1-sa*sa/(n10*n10));
        const double rs=(ca-n10*cw)/(ca+n10*cw), rp=(n10*ca-cw)/(n10*ca+cw), T=1-0.5*(rs*rs+rp*rp);
        const double roughSpec=0.04/(2*pi10*(1+waterCos))*2*cw/(1+cw);
        const double solarReference=entryScale*sunLux*T*ca/cw*(0.4*cw/pi10+roughSpec);
        std::printf("      WATER10 view sun reference %.9g, measured/reference %.6f\n",solarReference,solar/solarReference);
        expect(solar/solarReference>0.90 && solar/solarReference<1.12,"water10 receiver asks sun outside the view and agrees with flat reference");
        sunOn=false;
    }
    if(wants10("foam")) {
        SetLights(nullptr,0); w10.extinction[0]=w10.extinction[1]=w10.extinction[2]=0;
        w10.scattering[0]=w10.scattering[1]=w10.scattering[2]=0;
        constexpr UINT sz=8; // the native wave-buffer contract accepts square arrays of at least 8 texels
        std::vector<float> move(sz*sz*4,0), gradient(sz*sz*4,0);
        for(unsigned j=0;j<sz*sz;++j) { move[j*4]=0.05f; gradient[j*4]=0.2f; gradient[j*4+2]=gradient[j*4+3]=4; }
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=sz; d.MipLevels=d.ArraySize=1; d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
        d.SampleDesc.Count=1; d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA md{move.data(),sz*16,0}, gd{gradient.data(),sz*16,0};
        ComPtr<ID3D11Texture2D> mt,gt;
        expect(SUCCEEDED(device->CreateTexture2D(&d,&md,&mt)) && SUCCEEDED(device->CreateTexture2D(&d,&gd,&gt)), "water10 supported wave arrays");
        w10.displacement=uint64_t(uintptr_t(mt.Get())); w10.normals=uint64_t(uintptr_t(gt.Get()));
        w10.bandCount=1; w10.bands[0][0]=1; w10.bands[0][3]=1; w10.fades[0][1]=1; w10.maxDisplacement=0.12f;
        const Camera foamCamera=make_camera({0,1,-0.001f},{0,0,0},4,float(W)/float(H));
        scene10(-0.05f,12);
        const double off=state10("foam-off",foamCamera,0,TGPT_VIEW_ALBEDO);
        const double shore=state10("foam-shallow",foamCamera,1,TGPT_VIEW_ALBEDO);
        scene10(-2,12); const double deep=state10("foam-deep",foamCamera,1,TGPT_VIEW_ALBEDO);
        std::printf("      WATER10 foam shallow %.6f, deep %.6f, off %.6f\n",shore,deep,off);
        expect(shore>0.9 && deep<0.001 && off<0.001,"water10 foam forms at the traced shore and disappears in deep water");
        w10.bandCount=0; w10.displacement=w10.normals=0; w10.maxDisplacement=0; scene10(-0.05f,12);
        const double still=state10("foam-still",foamCamera,1,TGPT_VIEW_ALBEDO);
        expect(still<0.001,"water10 still shallow water does not make foam");
    }
    if(wants10("mirror")) {
        SetLights(nullptr,0); w10.extinction[0]=w10.extinction[1]=w10.extinction[2]=0;
        w10.scattering[0]=w10.scattering[1]=w10.scattering[2]=0; w10.bandCount=0; w10.displacement=w10.normals=0;
        scene10(-1.05f,12,true);
        const Camera direct=make_camera({0,0.4f,1},{0,1,1.00001f},0.1f,float(W)/float(H));
        const Camera mirrored=make_camera({0,1,-1},{0,0,0},0.1f,float(W)/float(H));
        const double seen=state10("mirror-direct-emitter",direct,0);
        const double truthful=state10("mirror-emitter-alone",mirrored,0);
        lamp10.position[0]=0; lamp10.position[1]=lamp10.position[2]=1; lamp10.radius=0.2f; lamp10.flags=TGPT_LIGHT_NO_RANGE;
        SetLights(&lamp10,1); const double proxy=state10("mirror-with-proxy",mirrored,0);
        const double emitted=20/(pi10*0.2*0.2);
        std::printf("      WATER10 mirror direct %.6f, emitter alone %.6f, proxy %.6f, ratio %.3f, flux proxy radiance %.3f\n",
                    seen,truthful,proxy,proxy/truthful,emitted);
        expect(seen>0.95 && seen<1.05 && truthful<0.04 && truthful>0.02,"water10 mirror of actual emitter follows Fresnel");
        expect(proxy/truthful>100 && proxy/truthful<220,"water10 PT90 proxy emitter explains bright mirror");
    }
    ReadStatus(&status); expect(status.framesFailed==0,"water10 all focused frames traced");
    std::printf(failures ? "FAILED water10 checks: %d\n" : "PASS water10 checks\n",failures);
    return failures ? 1 : 0;
}
