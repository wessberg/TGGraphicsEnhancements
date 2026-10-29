// SPDX-License-Identifier: MIT
// Included inside the real renderer harness after its ordinary scene has warmed up.
if(waterOnly) {
    #include "water10_gpu.inl"
    char focus[256]{}; GetEnvironmentVariableA("TGPT_TEST_WATER_CHECK",focus,sizeof(focus));
    auto waterCheck=[&](bool good,const char* label) {
        if(!focus[0] || std::strcmp(focus,label)==0) expect(good,label);
        else std::printf("skip water check: %s\n",label);
    };
    // The open sea as the game sends it (TGPT_TEST_WATER_SEA=1 with this fixture's switch): alone, and it ends the test.
    #include "water_sea_gpu.inl"
    // What the caustics land on what lies under the water (TGPT_TEST_WATER_LANDED=1 with this fixture's switch): alone too.
    #include "water_landed_gpu.inl"
    ownRays=true; sunOn=false; skyIntensity=1e-6f; reflectionBoost=0; tracedGain=1;
    setExposure(1.f); // Analytic references below are scene radiance, without pre-exposure.
    SetLights(nullptr, 0);
    TGPT_Material bed{}; bed.baseColor[3]=1; bed.emissive[0]=bed.emissive[1]=bed.emissive[2]=1;
    bed.albedoTile=bed.emissiveTile=bed.maskTile=bed.alphaTile=-1;
    bed.flags=TGPT_MATERIAL_DOUBLE_SIDED;
    bed.metallic=1; // black emitter: no reflected light to contaminate the analytic transmission reference
    bed.colorTexture=bed.normalTexture=bed.maskTexture=-1;
    expect(SetMaterials(12, &bed, 1)==0, "water test emissive bed");
    TGPT_WaterCaustics water{}; water.size=sizeof(water); water.abi=TGPT_ABI;
    water.worldToWater[0]=water.worldToWater[5]=water.worldToWater[10]=1;
    water.top=water.bottom=0.4f;
    water.bounds[0]=water.bounds[1]=-4; water.bounds[2]=water.bounds[3]=4;
    const M4 bottom=translate_scale({0, 0, 0}, {8, 1, 8}), level=translate_scale({0, 0.4f, 0}, {8, 1, 8});
    TGPT_Material mirror=bed; mirror.emissive[0]=mirror.emissive[1]=mirror.emissive[2]=0;
    mirror.baseColor[0]=mirror.baseColor[1]=mirror.baseColor[2]=1; mirror.metallic=1; mirror.smoothness=1;
    expect(SetMaterials(13, &mirror, 1)==0, "water test mirror material");
    auto scene=[&](bool wet, bool reflected=false, float slope=0) {
        std::vector<TGPT_Instance> list;
        std::vector<uint32_t> ids;
        auto add=[&](M4 matrix, uint32_t flags) {
            TGPT_Instance in{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
            in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=flags;
            ids.push_back(12); list.push_back(in);
        };
        M4 bedPose=bottom; bedPose.m[1][0]=8*slope; bedPose.m[1][2]=8*slope;
        add(bedPose, 0); if(wet) add(level, TGPT_INSTANCE_WATER);
        if(reflected) { add(translate_scale({0, 1, 0}, {8, 1, 8}), 0); ids.back()=13; }
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "water test scene");
        expect(SetWaterCaustics(wet ? &water : nullptr, wet ? 1 : 0)==0, "water test boundary");
        objects.clear(); objects.push_back({0, bedPose, {4}, false, false});
    };
    const Camera down=make_camera({0, 2, -0.01f}, {0, 0, 0}, 30.f, float(W)/float(H));
    uint32_t serial=60000;
    const uint32_t options=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_CAUSTICS|TGPT_OPTION_NO_BOILING;
    auto average=[&](const Camera& view, uint32_t frames, const char* name) {
        std::vector<float> mean(size_t(W)*H*3, 0);
        cut=true; run(TGPT_VIEW_NONE, view, view, serial++, options); cut=false;
        // (12 frames first: a water mesh the scene traced as something else before is read back, which takes 8.)
        for(uint32_t i=0;i<frames+12;++i) {
            run(TGPT_VIEW_NONE, view, view, serial++, options);
            const auto frame=readback();
            if(i>=12) for(size_t p=0;p<mean.size();++p) mean[p]+=frame[p]/float(frames);
        }
        if(name) {
            save(mean, name, true);
            char stages[2048]{}; if(GetStageTimes) GetStageTimes(stages,sizeof(stages));
            std::printf("      WATER %s exposure %.3f gain %.3f; %s\n",name,exposure,tracedGain,stages);
        }
        return mean;
    };
    // TGPT_TEST_WATER_COST=1: what a lake in view costs, at 1280 x 720 and nothing else run: a floor half a kilometre wide
    // seen from eye height towards the horizon, dry and then under 40 cm of water with the fixture's waves (one band,
    // 32 x 32 texels over 4 m, 12 cm high), each stage's GPU time over about the last second, and the pieces the water's
    // surface is traced in (the native log's status line has their triangles).
    if(waterCost) {
        constexpr UINT costSize=32;
        std::vector<float> costWaves(size_t(costSize)*costSize*4, 0), costSlopes(costWaves.size(), 0);
        for(UINT y=0;y<costSize;++y) for(UINT x=0;x<costSize;++x) {
            const size_t at=(size_t(y)*costSize+x)*4;
            const double phase=2*3.141592653589793*(x+0.5)/costSize;
            costWaves[at]=float(0.12*std::sin(phase)); costWaves[at+1]=0.04f;
            costSlopes[at]=float(0.12*2*3.141592653589793/4*std::cos(phase)); costSlopes[at+2]=4;
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width=desc.Height=costSize; desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count=1; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> waveTexture, slopeTexture;
        D3D11_SUBRESOURCE_DATA waveData{costWaves.data(), costSize*16, 0}, slopeData{costSlopes.data(), costSize*16, 0};
        expect(SUCCEEDED(device->CreateTexture2D(&desc, &waveData, &waveTexture)) && SUCCEEDED(device->CreateTexture2D(&desc, &slopeData, &slopeTexture)), "water cost run wave arrays");
        water.displacement=uint64_t(uintptr_t(waveTexture.Get())); water.normals=uint64_t(uintptr_t(slopeTexture.Get()));
        water.bandCount=1; water.bands[0][0]=0.25f; water.bands[0][1]=0.137f; water.bands[0][3]=1; water.fades[0][1]=1; water.maxDisplacement=0.12f;
        water.extinction[0]=0.45f; water.extinction[1]=0.12f; water.extinction[2]=0.08f;
        water.bounds[0]=water.bounds[1]=-256; water.bounds[2]=water.bounds[3]=256;
        const M4 floorPose=translate_scale({0, 0, 0}, {512, 1, 512});
        const Camera eye=make_camera({0.5f, 2.1f, 0.25f}, {40.f, 0.4f, 12.f}, 60.f, float(W)/float(H));
        for(int wet=0;wet<2;++wet) {
            std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
            for(int k=0;k<=wet;++k) {
                const M4 matrix=k ? translate_scale({0, 0.4f, 0}, {512, 1, 512}) : floorPose;
                TGPT_Instance in{};
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
                in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k ? TGPT_INSTANCE_WATER : 0u;
                ids.push_back(12); list.push_back(in);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "water cost run scene");
            expect(SetWaterCaustics(wet ? &water : nullptr, wet ? 1 : 0)==0, "water cost run water");
            objects.clear(); objects.push_back({0, floorPose, {4}, false, false});
            uint32_t frame=70000+uint32_t(wet)*1000;
            const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_CAUSTICS|TGPT_OPTION_NO_BOILING;
            cut=true; run(TGPT_VIEW_NONE, eye, eye, frame++, raw); cut=false;
            const auto t0=std::chrono::steady_clock::now();
            for(int i=0;i<240;++i) { run(TGPT_VIEW_NONE, eye, eye, frame++, raw); context->Flush(); }
            const double frameMs=std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-t0).count()/240.0;
            const auto picture=readback(); save(picture, wet ? "water-cost-lake.png" : "water-cost-dry.png", true);
            char stages[2048]{}; if(GetStageTimes) GetStageTimes(stages, sizeof(stages));
            ReadStatus(&status);
            std::printf("      WATER COST %s at %ux%u: %s; %.1f ms a frame in all here, %u instances traced, GPU %.2f ms; render thread %.2f ms a frame, of it recording %.2f ms\n", wet ? "lake" : "dry ", W, H, stages, frameMs,
                        status.instancesTraced, status.gpuMs, status.frameMs, status.recordMs);
        }
        expect(status.framesFailed==0, "water cost run frames complete without GPU errors");
        return failures ? 1 : 0;
    }
    scene(false); const auto dry=average(down, 16, "water-dry.png");
    scene(true); const auto pool=average(down, 256, "water-still.png");
    double drySum=0, wetSum=0, worst=0;
    for(UINT y=8;y<H-8;++y) for(UINT x=8;x<W-8;++x) {
        const size_t p=(size_t(y)*W+x)*3;
        drySum+=dry[p]; wetSum+=pool[p];
        double neighbours=0;
        for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) if(dx || dy)
            neighbours+=pool[(size_t(int(y)+dy)*W+int(x)+dx)*3]/8.0;
        if(neighbours>0) worst=std::max(worst, double(pool[p])/neighbours);
    }
    const double n=1.3333, f=std::pow((n-1)/(n+1), 2), expected=(1-f)/(n*n);
    std::printf("      WATER bed ratio %.6f, exact normal-incidence %.6f, worst neighbour ratio %.4f\n", wetSum/drySum, expected, worst);
    waterCheck(drySum>1 && std::fabs(wetSum/drySum-expected)<0.008, "water radiance crossing carries 1/n squared with Fresnel");
    waterCheck(worst<1.08, "undenoised water mean has no isolated bright dots");
    water.extinction[0]=water.extinction[1]=water.extinction[2]=1;
    scene(true); const auto absorbed=average(down, 256, "water-absorbed.png");
    double absorptionSum=0;
    for(UINT y=H/3;y<H*2/3;++y) for(UINT x=W/3;x<W*2/3;++x) {
        const size_t p=(size_t(y)*W+x)*3; absorptionSum+=absorbed[p]/pool[p];
    }
    const double absorptionRatio=absorptionSum/double((H*2/3-H/3)*(W*2/3-W/3));
    std::printf("      WATER absorption %.6f, Beer-Lambert %.6f\n", absorptionRatio, std::exp(-0.4));
    waterCheck(std::fabs(absorptionRatio-std::exp(-0.4))<0.015, "water absorption belongs to each ray's travelled segment");
    const Camera submerged=make_camera({0, 0.2f, -0.001f}, {0, 0, 0}, 10.f, float(W)/float(H));
    const auto underwater=average(submerged, 256, "water-underwater.png");
    double below=0;
    for(size_t p=0;p<underwater.size();p+=3) below+=underwater[p];
    below/=double(W*H);
    std::printf("      WATER underwater bed %.6f, expected %.6f\n", below, std::exp(-0.2));
    waterCheck(std::fabs(below-std::exp(-0.2))<0.015, "underwater camera attenuates the camera-to-bed segment");
    water.extinction[0]=water.extinction[1]=water.extinction[2]=0;
    scene(true);
    const Camera critical=make_camera({0, 0.2f, 0}, {0, 0.55f, 1}, 4.f, float(W)/float(H));
    const auto internal=average(critical, 64, "water-total-internal.png");
    double tir=0; for(size_t p=0;p<internal.size();p+=3) tir+=internal[p]; tir/=double(W*H);
    std::printf("      WATER total internal reflection %.6f, emissive bed 1\n", tir);
    waterCheck(std::fabs(tir-1.0)<0.015, "water viewed from below totally internally reflects beyond the critical angle");
    scene(true, true);
    const Camera mirrorDown=make_camera({0, 0.8f, -0.001f}, {0, 0.4f, 0}, 10.f, float(W)/float(H));
    const Camera mirrorUp=make_camera({0, 0.8f, -0.001f}, {0, 1.2f, 0}, 10.f, float(W)/float(H));
    const auto directWater=average(mirrorDown, 256, "water-camera.png");
    const auto mirrorWater=average(mirrorUp, 256, "water-in-mirror.png");
    double directSum=0, mirrorSum=0;
    for(size_t p=0;p<mirrorWater.size();p+=3) { directSum+=directWater[p]; mirrorSum+=mirrorWater[p]; }
    std::printf("      WATER mirror/camera %.6f\n", mirrorSum/directSum);
    waterCheck(directSum>1 && std::fabs(mirrorSum/directSum-1.0)<0.025, "a mirror sees the same water boundary and transmitted bed as the camera");
    // A uniform sky makes exact Fresnel reflectance the many-sample reference, independent of cube sampling.
    std::array<std::array<float,16>,6> whiteFaces{};
    D3D11_SUBRESOURCE_DATA skyData[6]{};
    for(size_t face=0;face<6;++face) { whiteFaces[face].fill(1); skyData[face]={whiteFaces[face].data(), 32, 0}; }
    D3D11_TEXTURE2D_DESC skyDesc{}; skyDesc.Width=skyDesc.Height=2; skyDesc.ArraySize=6; skyDesc.MipLevels=1;
    skyDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; skyDesc.SampleDesc.Count=1;
    skyDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE; skyDesc.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> whiteSky;
    expect(SUCCEEDED(device->CreateTexture2D(&skyDesc, skyData, &whiteSky)), "water test sky texture");
    expect(SetSky(uint64_t(uintptr_t(whiteSky.Get())))==0, "water test uniform sky"); skyIntensity=1;
    bed.emissive[0]=bed.emissive[1]=bed.emissive[2]=0;
    expect(SetMaterials(12, &bed, 1)==0, "water test black bed"); scene(true);
    const Camera grazing=make_camera({0, 0.7f, -1}, {0, 0.4f, 1}, 4.f, float(W)/float(H));
    auto fresnel=[&](double cosine) {
        const double transmitted=std::sqrt(1-(1-cosine*cosine)/(n*n));
        const double s=(cosine-n*transmitted)/(cosine+n*transmitted), p=(n*cosine-transmitted)/(n*cosine+transmitted);
        return (s*s+p*p)*0.5;
    };
    auto ray=[&](const Camera& view, UINT x, UINT y) {
        const float clip[4]={2*(float(x)+0.5f)/W-1, 1-2*(float(y)+0.5f)/H, 1, 1}; float out[4]{};
        for(int r=0;r<4;++r) for(int c=0;c<4;++c) out[r]+=view.invViewProj.m[r][c]*clip[c];
        return normalize(V3{out[0]/out[3],out[1]/out[3],out[2]/out[3]});
    };
    const auto skyReflection=average(grazing, 256, "water-grazing.png");
    double reflectionSum=0, reflectionReference=0;
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
        const auto d=ray(grazing,x,y); reflectionReference+=fresnel(-d.y);
        reflectionSum+=skyReflection[(size_t(y)*W+x)*3];
    }
    std::printf("      WATER grazing %.6f, exact Fresnel %.6f\n", reflectionSum/(W*H), reflectionReference/(W*H));
    waterCheck(std::fabs(reflectionSum/reflectionReference-1.0)<0.025, "water mirrors the sky at grazing angles with exact Fresnel");
    skyIntensity=1e-6f; bed.emissive[0]=bed.emissive[1]=bed.emissive[2]=1;
    expect(SetMaterials(12, &bed, 1)==0, "water wave test emissive bed");
    constexpr UINT waveSize=32;
    std::vector<float> displacement(size_t(waveSize)*waveSize*4,0), gradient(displacement.size(),0);
    for(UINT y=0;y<waveSize;++y) for(UINT x=0;x<waveSize;++x) {
        const size_t at=(size_t(y)*waveSize+x)*4;
        const double phase=2*3.141592653589793*(x+0.5)/waveSize;
        displacement[at]=float(0.12*std::sin(phase)); displacement[at+1]=0.04f;
        gradient[at]=float(0.12*2*3.141592653589793/4*std::cos(phase)); gradient[at+2]=4;
    }
    D3D11_TEXTURE2D_DESC waves{}; waves.Width=waves.Height=waveSize; waves.MipLevels=waves.ArraySize=1;
    waves.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; waves.SampleDesc.Count=1; waves.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> waveTexture, gradientTexture;
    D3D11_SUBRESOURCE_DATA waveData{displacement.data(),waveSize*16,0}, gradientData{gradient.data(),waveSize*16,0};
    expect(SUCCEEDED(device->CreateTexture2D(&waves,&waveData,&waveTexture)) && SUCCEEDED(device->CreateTexture2D(&waves,&gradientData,&gradientTexture)), "water wave simulation arrays");
    water.displacement=uint64_t(uintptr_t(waveTexture.Get())); water.normals=uint64_t(uintptr_t(gradientTexture.Get()));
    water.bandCount=1; water.bands[0][0]=0.25f; water.bands[0][3]=1; water.fades[0][1]=1; water.maxDisplacement=0.12f;
    scene(true);
    auto wave=[&](double x, double* derivative=nullptr) {
        const double texel=(x+2.25*0.04)*waveSize/4-0.5;
        const int left=int(std::floor(texel)); const double fraction=texel-left;
        const double a=displacement[(size_t((left%int(waveSize)+waveSize)%waveSize))*4];
        const double b=displacement[(size_t(((left+1)%int(waveSize)+waveSize)%waveSize))*4];
        if(derivative) *derivative=(b-a)*waveSize/4;
        return 0.4+a*(1-fraction)+b*fraction;
    };
    auto waterTime=[&](const Camera& view,V3 d) {
        double lo=0,hi=5;
        for(int step=0;step<40;++step) { const double t=(lo+hi)*0.5; if(view.position.y+d.y*t>wave(view.position.x+d.x*t)) lo=t; else hi=t; }
        return (lo+hi)*0.5;
    };
    for(int i=0;i<12;++i) run(TGPT_VIEW_NORMALS,down,down,serial++,options);
    const auto waveNormals=readback(); save(waveNormals,"water-wave-normals.png",false);
    double normalError=0;
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
        const auto d=ray(down,x,y); const double t=waterTime(down,d); double slope;
        wave(down.position.x+d.x*t,&slope); const V3 normal=normalize({float(-slope),1,0});
        const size_t at=(size_t(y)*W+x)*3;
        normalError+=std::fabs(waveNormals[at]*2-1-normal.x)+std::fabs(waveNormals[at+1]*2-1-normal.y);
    }
    normalError/=double(W*H);
    std::printf("      WATER displaced normal mean error %.6f\n",normalError);
    waterCheck(normalError<0.018, "camera rays meet the actual waves including their sideways displacement");
    const auto waveBed=average(down,128,"water-wave-bed.png"); double waveSum=0,waveReference=0;
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
        const auto d=ray(down,x,y); const double t=waterTime(down,d); double slope; wave(down.position.x+d.x*t,&slope);
        const V3 normal=normalize({float(-slope),1,0}); waveReference+=(1-fresnel(-dot(d,normal)))/(n*n);
        waveSum+=waveBed[(size_t(y)*W+x)*3];
    }
    std::printf("      WATER wave bed %.6f, Fresnel/Snell reference %.6f\n",waveSum/(W*H),waveReference/(W*H));
    waterCheck(std::fabs(waveSum/waveReference-1.0)<0.025, "undenoised wavy pool converges to its many-sample transport reference");
    scene(true,true);
    const auto waveMirror=average(mirrorUp,128,"water-waves-in-mirror.png");
    // Keep the same ceiling mirror in both scenes: it sends water's reflected light back to the pool.
    // Removing it for the direct view would change the lighting by about 2 %, even for a perfectly flat pool.
    const auto waveDirect=average(mirrorDown,128,"water-waves-camera.png");
    double waveMirrorSum=0,waveDirectSum=0;
    for(size_t p=0;p<waveMirror.size();p+=3) {waveMirrorSum+=waveMirror[p];waveDirectSum+=waveDirect[p];}
    std::printf("      WATER wavy mirror/camera %.6f\n",waveMirrorSum/waveDirectSum);
    waterCheck(std::fabs(waveMirrorSum/waveDirectSum-1.0)<0.025,"a mirror sees the same displaced waves and transport as the camera");
    scene(true,false,0.4f);
    const Camera coast=make_camera({1.1f,2,-0.01f},{1.1f,0.4f,0},40.f,float(W)/float(H));
    for(int i=0;i<12;++i) run(TGPT_VIEW_NORMALS,coast,coast,serial++,options);
    const auto shore=readback(); save(shore,"water-wave-shore.png",false);
    UINT mismatches=0,waterPixels=0,landPixels=0,compared=0;
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
        const auto d=ray(coast,x,y); const double wetTime=waterTime(coast,d);
        const double dryTime=(0.4*(coast.position.x+coast.position.z)-coast.position.y)/(d.y-0.4*(d.x+d.z));
        if(std::fabs(wetTime-dryTime)<0.01) continue;
        const bool expectedWater=wetTime<dryTime, actualWater=shore[(size_t(y)*W+x)*3]>0.37;
        ++compared; if(expectedWater) ++waterPixels; else ++landPixels; if(expectedWater!=actualWater) ++mismatches;
    }
    std::printf("      WATER shore water %u land %u mismatches %u/%u\n",waterPixels,landPixels,mismatches,compared);
    waterCheck(waterPixels>200 && landPixels>200 && mismatches<compared/50, "waves meet a sloping shore at the nearest surface without a flat waterline step");
    // ---- The waves as triangles in the acceleration structure (PT-71 item 5, docs/agents/water-4.md) ----
    // The waves' texels off the lattice's points, as a current's offset moves them in the game: the lattice's points fell
    // on the texels' own corners above, where straight pieces between them are the bilinear data itself.
    const float bandOffset=0.137f;
    water.bands[0][1]=bandOffset;
    auto surfaceY=[&](double x) { // the game's wave data over a displaced point's x: bilinear between its texels
        const double texel=((x+2.25*0.04)*0.25-bandOffset)*waveSize-0.5;
        const double cell=std::floor(texel), fraction=texel-cell;
        auto at=[&](double i) { const int k=int(std::fmod(std::fmod(i, double(waveSize))+waveSize, double(waveSize))); return double(displacement[size_t(k)*4]); };
        return 0.4+at(cell)*(1-fraction)+at(cell+1)*fraction;
    };
    // A floor (the bed's material, dark now) and the water's level quad over it, each a square of its own size.
    bed.emissive[0]=bed.emissive[1]=bed.emissive[2]=0;
    expect(SetMaterials(12, &bed, 1)==0, "water geometry test dark floor");
    auto lake=[&](float span, float floorY, float floorSpan) {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        const M4 floorPose=translate_scale({0, floorY, 0}, {floorSpan, 1, floorSpan});
        for(int k=0;k<2;++k) {
            const M4 matrix=k ? translate_scale({0, 0.4f, 0}, {span, 1, span}) : floorPose;
            TGPT_Instance in{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
            in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k ? TGPT_INSTANCE_WATER : 0u;
            ids.push_back(12); list.push_back(in);
        }
        water.bounds[0]=water.bounds[1]=-span*0.5f; water.bounds[2]=water.bounds[3]=span*0.5f;
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "water geometry test scene");
        expect(SetWaterCaustics(&water, 1)==0, "water geometry test waves");
        objects.clear(); objects.push_back({0, floorPose, {4}, false, false});
    };
    // 1. The surface a camera ray meets is the game's wave surface. What the tracer reports under the screen's centre
    // (pt_probe: a camera ray's nearest hit, in the structure every ray is traced in) against the wave data along the same
    // ray: how far the point it met lies over or under the data's surface there. Near, where every lattice edge of a wave
    // texel's length (12.5 cm here) is fully on the waves; and across a lake half a kilometre wide, where the edges fully on
    // the waves grow with the distance (up to 1/32 of it) and shorter waves are no longer followed (these are 4 m long, 12 cm
    // high).
    auto offSurface=[&](const Camera& view, double& distance) {
        for(int i=0;i<12;++i) run(TGPT_VIEW_NONE, view, view, serial++, options);
        ReadStatus(&status);
        distance=status.pickFound ? double(status.pickDistance) : -1.0;
        if(distance<0) return 1e9;
        const auto d=ray(view, W/2, H/2);
        return std::fabs(double(view.position.y)+double(d.y)*distance-surfaceY(double(view.position.x)+double(d.x)*distance));
    };
    lake(8, 0, 8);
    double nearWorst=0, nearLevel=0;
    const V3 nearTargets[6]={{0.9f, 0.4f, 1.1f}, {-1.3f, 0.4f, 0.4f}, {1.7f, 0.4f, -0.8f}, {-0.45f, 0.4f, -1.6f}, {2.6f, 0.4f, 2.2f}, {0.21f, 0.4f, 0.33f}};
    for(const V3& target:nearTargets) {
        const Camera view=make_camera({0.2f, 1.5f, -0.3f}, target, 20.f, float(W)/float(H));
        double distance; const double off=offSurface(view, distance);
        const auto d=ray(view, W/2, H/2);
        nearWorst=std::max(nearWorst, off);
        nearLevel=std::max(nearLevel, std::fabs(surfaceY(double(view.position.x)+double(d.x)*distance)-0.4));
        std::printf("      WATER surface near: %.3f m along the ray, %.3f mm off the wave data\n", distance, off*1000.0);
    }
    std::printf("      WATER surface near: at most %.3f mm off the wave data (level water would be up to %.1f mm off)\n", nearWorst*1000.0, nearLevel*1000.0);
    waterCheck(nearWorst<0.002 && nearLevel>0.05, "the surface a camera ray meets is the game's wave surface, to 2 mm near the camera");
    lake(512, 0, 512);
    char units[256]{};
    double farOff[4]{}, farDistance[4]{};
    const float farAhead[4]={30.f, 60.f, 120.f, 170.f};
    bool farOk=true;
    for(int k=0;k<4;++k) {
        const Camera view=make_camera({0.5f, 3.f, 0.25f}, {0.5f+farAhead[k], 0.4f, 0.25f+0.31f*farAhead[k]}, 4.f, float(W)/float(H));
        farOff[k]=offSurface(view, farDistance[k]);
        // What lattice edges of 1/32 of the distance (the longest that are fully on the waves there) leave of a 4 m wave
        // 12 cm high: its sag between two points (an edge squared over 8, times the wave's curvature), and never more than
        // the wave itself where it is no longer followed.
        const double edge=farDistance[k]/32.0, curvature=0.12*std::pow(2*3.141592653589793/4.0, 2);
        const double allowed=std::min(0.25, edge*edge/8.0*curvature)+0.002;
        if(!(farOff[k]<=allowed)) farOk=false;
        std::printf("      WATER surface far: %.1f m along the ray, %.1f mm off the wave data (an edge of %.2f m there allows %.1f mm)\n",
            farDistance[k], farOff[k]*1000.0, edge, allowed*1000.0);
    }
    { char stages[2048]{}; if(GetStageTimes) GetStageTimes(stages, sizeof(stages)); ReadStatus(&status);
      std::printf("      WATER lake 512 m: %u instances traced (2 of them the floor and the level water: the rest are the groups the surface's pieces are traced in); %s\n", status.instancesTraced, stages); }
    waterCheck(farOk, "far from the camera the surface stays within what its coarser lattice allows of the wave data");
    // 2. The same surface for a shadow ray. The sun over the water's open edge (the quad ends at z = -4, its waves
    // running along that edge) onto a dry floor 10 cm under the water's level: a floor point is lit where its ray to the
    // sun passes over the wave at the edge, dark where it passes under it. The shadow's outline on the floor is the wave's
    // own; level water would cast a straight one.
    // Three pictures of it, and what each can say (water 4b, 2026-10-08; until then one picture, taken with the Caustics
    // row off while "off" still meant "the surface stops the light and nothing brings it through": a black shadow of
    // clear water, which no row setting shows any more):
    //   A. An OPAQUE liquid (it takes all light within millimetres), the row ON: the straight ray stops at the surface and
    //      nothing comes through the liquid, so the floor shows where the shadow rays met the waves: the wave's outline.
    //      This is the check of the surface a shadow ray meets.
    //   C. Clear water, the row OFF: a light's straight ray passes the surface by that row's definition
    //      (pt_water_surface.hlsli light_mask), and clear water takes nothing: no shadow at all.
    //   D. Clear water, the row ON, which is what a player sees: the straight ray stops, and the sun's light arrives bent
    //      through the wave (pt_caustics.hlsl). MEASURED AND PRINTED, NOT JUDGED: nothing here says where that light
    //      should land beyond an open edge (it needs a reference of its own, as the caustics' sine wave has).
    // (The rest of this fixture runs with the row off: `options`.)
    lake(8, 0.3f, 16);
    sunOn=true;
    const uint32_t rowOn=options&~uint32_t(TGPT_OPTION_NO_CAUSTICS);
    UINT shadowCompared=0, shadowWrong=0, shadowLit=0, shadowDark=0, levelDiffers=0, offLit=0, clearLit=0;
    double clearDarkSum=0, clearLitSum=0;
    const float edgeZ=-4.f, floorY=0.3f;
    auto liquid=[&](float extinction) {
        water.extinction[0]=water.extinction[1]=water.extinction[2]=extinction;
        expect(SetWaterCaustics(&water, 1)==0, "water wave shadow: the liquid's extinction");
    };
    for(int k=0;k<3;++k) {
        const float x0=0.55f+0.9f*float(k);
        const Camera view=make_camera({x0, 1.3f, -4.085f}, {x0, floorY, -4.08f}, 8.f, float(W)/float(H));
        auto shot=[&](uint32_t with) {
            std::vector<float> sum(size_t(W)*H*3, 0);
            cut=true; run(TGPT_VIEW_DIRECT, view, view, serial++, with); cut=false;
            for(int i=0;i<20;++i) {
                run(TGPT_VIEW_DIRECT, view, view, serial++, with);
                const auto frame=readback();
                if(i>=4) for(size_t p=0;p<sum.size();++p) sum[p]+=frame[p]/16.f;
            }
            return sum;
        };
        liquid(2000.f);
        const auto mean=shot(rowOn);
        liquid(0.f);
        const auto meanOff=shot(options), meanClear=shot(rowOn);
        char name[64]; std::snprintf(name, sizeof(name), "water-wave-shadow-%d.png", k); save(mean, name, true);
        std::snprintf(name, sizeof(name), "water-wave-shadow-row-off-%d.png", k); save(meanOff, name, true);
        std::snprintf(name, sizeof(name), "water-wave-shadow-clear-row-on-%d.png", k); save(meanClear, name, true);
        float brightest=0; for(size_t p=0;p<mean.size();p+=3) brightest=std::max(brightest, mean[p]);
        float brightestOff=0; for(size_t p=0;p<meanOff.size();p+=3) brightestOff=std::max(brightestOff, meanOff[p]);
        float brightestClear=0; for(size_t p=0;p<meanClear.size();p+=3) brightestClear=std::max(brightestClear, meanClear[p]);
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const auto d=ray(view, x, y);
            const double t=(double(floorY)-double(view.position.y))/double(d.y);
            const double fx=double(view.position.x)+double(d.x)*t, fz=double(view.position.z)+double(d.z)*t;
            if(fz>double(edgeZ)-0.006) continue; // the water itself, and its very edge
            // Along the ray to the sun to the plane of the water's edge: over the wave there, or under it. The ray starts
            // where the tracer starts a camera surface's shadow rays: 2 cm and 0.2 % of the camera's distance over it
            // (pt_common ray_offset, for a surface that is not marked as lying on its own traced mesh).
            const double start=double(floorY)+0.02+0.002*t;
            const double s=(double(edgeZ)-fz)/double(sunDir.z);
            const double over=start+double(sunDir.y)*s-surfaceY(fx+double(sunDir.x)*s);
            const double overLevel=start+double(sunDir.y)*s-0.4;
            if(std::fabs(over)<0.006) continue; // within a pixel or two of the outline (and the sun's own half-shadow)
            const bool lit=over>0, shown=mean[(size_t(y)*W+x)*3]>0.5f*brightest;
            ++shadowCompared; if(lit) ++shadowLit; else ++shadowDark;
            if(lit!=shown) ++shadowWrong;
            if(lit!=(overLevel>0)) ++levelDiffers;
            if(!lit && meanOff[(size_t(y)*W+x)*3]>0.5f*brightestOff) ++offLit;
            const double clear=double(meanClear[(size_t(y)*W+x)*3]);
            if(lit) clearLitSum+=clear; else { clearDarkSum+=clear; if(clear>0.5*double(brightestClear)) ++clearLit; }
        }
    }
    sunOn=false;
    const double clearShare=shadowDark && shadowLit && clearLitSum>0 ? (clearDarkSum/shadowDark)/(clearLitSum/shadowLit) : 0.0;
    std::printf("      WATER wave shadow, an opaque liquid, the Caustics row on: lit %u dark %u, wrong %u of %u (level water would differ in %u)\n",
                shadowLit, shadowDark, shadowWrong, shadowCompared, levelDiffers);
    std::printf("      WATER wave shadow, clear water, the row off: %u of the %u pixels in the wave's straight shadow are lit\n", offLit, shadowDark);
    std::printf("      WATER wave shadow, clear water, the row on (NOT JUDGED, no reference): %u of the %u pixels in the wave's straight shadow are lit;"
                " they hold %.3f of the open floor's light on average\n", clearLit, shadowDark, clearShare);
    waterCheck(shadowLit>500 && shadowDark>500 && levelDiffers>shadowCompared/20 && shadowWrong<shadowCompared/100,
               "a shadow ray meets the same waves: the sun's shadow of an opaque liquid's edge on dry ground has the wave's outline");
    waterCheck(shadowDark>500 && offLit>shadowDark-shadowDark/100,
               "with the Caustics row off a light's straight ray passes clear water: its edge casts no shadow");
    ReadStatus(&status);
    std::printf("      WATER GPU %.3f ms at %ux%u, traced %llu failed %llu\n", status.gpuMs, W, H,
        (unsigned long long)status.framesTraced, (unsigned long long)status.framesFailed);
    expect(status.framesFailed==0, "water frames complete without GPU errors");
    std::printf("%s water surface checks\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
