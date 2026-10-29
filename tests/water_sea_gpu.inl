// SPDX-License-Identifier: MIT
// The open sea as the game sends it (PT-81; TGPT_TEST_WATER_SURFACE=1 with TGPT_TEST_WATER_SEA=1, at 640 x 360; it runs
// alone and ends the test). Included at the top of water_surface_gpu.inl's block.
//
// The game's own numbers for "Ocean" (harness run 20261008-083001, LogOutput.log): three bands, 64 x 64 texels a band, the
// waves move up to 0.665 m (so they are up to 0.2956 m high: HDRP's largest move is 2.25 times its largest height). The foam
// texture's texels (run 20261008-124239): mean 123, 114, 142, 89 of 255, red from 90 to 165.
// What no log held when this was written, taken from the defaults of the game's own HDRP build (docs/agents/water-6.md):
// the bands' sizes (500 m, 500 / 7.37 m, 10 m) and fades. Since run 20261008-144459 the log holds the rest, and the
// fixture has them: the foam's amount 1 (threshold 12.233), intensity 0.54, tiling 0.25, the water's scattering colour
// and what it takes a metre, its smoothness 0.947 near and 0 far. HDRP's simulation writes each band's Jacobian as 4 where the water is at rest (its foam threshold is 4, 8 and
// 12 times a factor near 1 for one, two and three bands): the waves below are this fixture's own, their Jacobian worked
// out from their own sideways move in that convention.
if(waterSea) {
    ownRays=true; sunOn=true; skyIntensity=1; reflectionBoost=0; tracedGain=1; rayDistance=4000;
    setExposure(1.f);
    SetLights(nullptr, 0);
    TGPT_Material sand{}; sand.baseColor[0]=0.32f; sand.baseColor[1]=0.24f; sand.baseColor[2]=0.15f; sand.baseColor[3]=1;
    sand.albedoTile=sand.emissiveTile=sand.maskTile=sand.alphaTile=-1; sand.flags=TGPT_MATERIAL_DOUBLE_SIDED;
    sand.colorTexture=sand.normalTexture=sand.maskTexture=-1;
    expect(SetMaterials(12, &sand, 1)==0, "sea test sand");
    const double pi=3.141592653589793, chop=2.25;
    constexpr UINT seaSize=64;
    const double patch[3]={500.0, 500.0/(5.0+45.0*(250.0/4750.0)), 10.0};
    struct SeaWave { int m, n; double a, phase; };
    // Whole waves a patch (so each band repeats as HDRP's does): a swell of 70 to 160 m, wind waves of 4 to 7 m, ripples of
    // 0.6 to 1.4 m; their heights add up to 0.296 m at most.
    const std::vector<SeaWave> spectrum[3]={
        {{3, 1, 0.045, 0.3}, {4, -2, 0.035, 1.9}, {5, 2, 0.025, 4.1}, {7, 1, 0.015, 2.6}},
        {{9, 2, 0.050, 0.7}, {11, -4, 0.040, 3.3}, {14, 3, 0.030, 5.2}, {8, -7, 0.030, 1.1}, {17, 1, 0.020, 4.4}},
        {{7, 2, 0.0020, 0.2}, {9, -5, 0.0016, 2.2}, {13, 4, 0.0012, 3.9}, {16, -3, 0.0012, 5.5}}};
    std::vector<float> seaMove[3], seaExtra[3];
    double lowest[3]={4, 4, 4};
    for(int b=0;b<3;++b) {
        seaMove[b].assign(size_t(seaSize)*seaSize*4, 0.f); seaExtra[b].assign(size_t(seaSize)*seaSize*4, 0.f);
        for(UINT j=0;j<seaSize;++j) for(UINT i=0;i<seaSize;++i) {
            const double u=(i+0.5)/seaSize, v=(j+0.5)/seaSize;
            double h=0, sx=0, sz=0, hx=0, hz=0, dxx=0, dzz=0, dxz=0;
            for(const SeaWave& w:spectrum[b]) {
                const double kx=2*pi*w.m/patch[b], kz=2*pi*w.n/patch[b], k=std::hypot(kx, kz);
                const double angle=2*pi*(w.m*u+w.n*v)+w.phase, c=std::cos(angle), s=std::sin(angle);
                h+=w.a*c; sx+=w.a*kx/k*s; sz+=w.a*kz/k*s; hx-=w.a*kx*s; hz-=w.a*kz*s;
                dxx+=w.a*kx*kx/k*c; dzz+=w.a*kz*kz/k*c; dxz+=w.a*kx*kz/k*c;
            }
            // The water's own sideways move is -chop x (sx, sz): towards the crests. Its Jacobian as HDRP's simulation
            // writes it: (2 + d move.x / dx) (2 + d move.z / dz) - (d move.x / dz) (d move.z / dx), 4 at rest.
            const double jacobian=(2-chop*dxx)*(2-chop*dzz)-chop*dxz*chop*dxz;
            lowest[b]=std::min(lowest[b], jacobian);
            const size_t at=(size_t(j)*seaSize+i)*4;
            seaMove[b][at]=float(h); seaMove[b][at+1]=float(sx); seaMove[b][at+2]=float(sz);
            seaExtra[b][at]=float(hx); seaExtra[b][at+1]=float(hz); seaExtra[b][at+2]=seaExtra[b][at+3]=float(jacobian);
        }
    }
    D3D11_TEXTURE2D_DESC seaDesc{}; seaDesc.Width=seaDesc.Height=seaSize; seaDesc.MipLevels=1; seaDesc.ArraySize=3;
    seaDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; seaDesc.SampleDesc.Count=1; seaDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA moveData[3], extraData[3];
    for(int b=0;b<3;++b) { moveData[b]={seaMove[b].data(), seaSize*16, 0}; extraData[b]={seaExtra[b].data(), seaSize*16, 0}; }
    ComPtr<ID3D11Texture2D> seaMoveTexture, seaExtraTexture;
    expect(SUCCEEDED(device->CreateTexture2D(&seaDesc, moveData, &seaMoveTexture)) && SUCCEEDED(device->CreateTexture2D(&seaDesc, extraData, &seaExtraTexture)), "sea test wave arrays");
    // The foam texture: the fixture's own patterns, made as the game's foam texture is (docs/agents/water-7.md: alpha a
    // coarse web of bright lines round dark cells, red a fine web, green soft clouds, blue a fine grain), and each
    // channel's values spread as the game's are (the game's values at its texture's 0, 5, 10, 25, 50, 75, 90, 95 and
    // 100 %): a pattern's texels are ranked, and each takes the game's value at its rank. At the sea's tiling of 0.25 a
    // texel is 8 mm, the fine web's cells about 9 cm, the coarse web's about 36 cm.
    constexpr UINT foamSide=512;
    std::vector<uint32_t> foamTexels(size_t(foamSide)*foamSide, 0u);
    double foamMean[4]={0, 0, 0, 0};
    {
        const double atShare[9]={0, 0.05, 0.10, 0.25, 0.50, 0.75, 0.90, 0.95, 1.0};
        const double gameValue[4][9]={{0.094, 0.290, 0.290, 0.322, 0.420, 0.612, 0.804, 0.839, 1.000},
                                      {0.016, 0.176, 0.204, 0.318, 0.443, 0.557, 0.686, 0.765, 0.969},
                                      {0.000, 0.322, 0.388, 0.482, 0.569, 0.647, 0.710, 0.741, 0.937},
                                      {0.051, 0.157, 0.188, 0.220, 0.278, 0.443, 0.686, 0.749, 0.922}};
        std::vector<double> pattern(size_t(foamSide)*foamSide); std::vector<uint32_t> order(pattern.size());
        for(int c=0;c<4;++c) {
            for(UINT y=0;y<foamSide;++y) for(UINT x=0;x<foamSide;++x) {
                const double u=2*pi*(x+0.5)/foamSide, v=2*pi*(y+0.5)/foamSide;
                double value;
                // A web: bright where a wavy field crosses 0. Clouds: the field itself. Grain: a hash of the texel.
                if(c==0) value=-std::fabs(0.5*std::sin(23*u+1.3*std::sin(7*v))+0.5*std::sin(19*v+0.7+1.1*std::sin(9*u)));
                else if(c==1) value=0.5*std::sin(2*u+2*v+0.9+0.8*std::sin(3*v))+0.5*std::sin(3*v-2*u+2.1);
                else if(c==2) { uint32_t h=(x*73856093u)^(y*19349663u); h^=h>>13; h*=0x5bd1e995u; h^=h>>15; value=double(h&0xFFFFu); }
                else value=-std::fabs(0.6*std::sin(5*u+1.7*std::sin(3*v+0.4))+0.4*std::sin(6*v+4*u+1.0));
                pattern[size_t(y)*foamSide+x]=value;
            }
            for(size_t i=0;i<order.size();++i) order[i]=uint32_t(i);
            std::sort(order.begin(), order.end(), [&](uint32_t l, uint32_t r) { return pattern[l]<pattern[r]; });
            for(size_t rank=0;rank<order.size();++rank) {
                const double share=(double(rank)+0.5)/double(order.size());
                int k=0; while(k<7 && share>atShare[k+1]) ++k;
                const double value=gameValue[c][k]+(gameValue[c][k+1]-gameValue[c][k])*(share-atShare[k])/(atShare[k+1]-atShare[k]);
                const uint32_t byte=uint32_t(std::clamp(std::lround(value*255.0), 0l, 255l));
                foamMean[c]+=double(byte)/double(order.size()); foamTexels[order[rank]]|=byte<<(8*c);
            }
        }
    }
    D3D11_TEXTURE2D_DESC foamDesc{foamSide, foamSide, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_SUBRESOURCE_DATA foamData{foamTexels.data(), foamSide*4, 0};
    ComPtr<ID3D11Texture2D> foamTexture; device->CreateTexture2D(&foamDesc, &foamData, &foamTexture);
    TGPT_SharedTexture foamShared{sizeof(TGPT_SharedTexture), TGPT_ABI};
    expect(foamTexture && RegisterSharedTexture(uint64_t(uintptr_t(foamTexture.Get())), &foamShared)>=0, "sea test foam texture is the shared original");
    std::printf("      SEA the fixture's foam texture: mean %.0f, %.0f, %.0f, %.0f of 255 (the game's: 123, 114, 142, 89); each band's lowest Jacobian %.3f, %.3f, %.3f (4 at rest)\n",
                foamMean[0], foamMean[1], foamMean[2], foamMean[3], lowest[0], lowest[1], lowest[2]);

    TGPT_WaterCaustics sea{}; sea.size=sizeof(sea); sea.abi=TGPT_ABI;
    sea.worldToWater[0]=sea.worldToWater[5]=sea.worldToWater[10]=1;
    sea.displacement=uint64_t(uintptr_t(seaMoveTexture.Get())); sea.normals=uint64_t(uintptr_t(seaExtraTexture.Get()));
    sea.bandCount=3;
    const float fadeStart[3]={1500, 300, 50}, fadeOver[3]={3000, 800, 200};
    for(int b=0;b<3;++b) {
        sea.bands[b][0]=float(1.0/patch[b]); sea.bands[b][1]=0.137f*float(b+1); sea.bands[b][2]=0.071f*float(b+1); sea.bands[b][3]=1;
        sea.fades[b][0]=-1.f/fadeOver[b]; sea.fades[b][1]=1.f+fadeStart[b]/fadeOver[b];
    }
    // The water's body as the game's "Ocean" has it (run 20261008-144459, LogOutput.log line 861): what it takes a metre
    // (absorption distance 4 m, refraction colour 0.614 / 0.736 / 0.731) and its scattering colour.
    sea.extinction[0]=0.377f; sea.extinction[1]=0.258f; sea.extinction[2]=0.263f;
    sea.top=sea.bottom=0; sea.bounds[0]=sea.bounds[1]=-1024; sea.bounds[2]=sea.bounds[3]=1024; sea.maxDisplacement=0.665f;
    sea.scattering[0]=0.257f; sea.scattering[1]=0.604f; sea.scattering[2]=0.469f;
    auto threshold=[](double amount) { return float(12.0*std::pow(0.8+0.28*amount, 0.25)); };
    // The foam as the game's "Ocean" has it (the same log line): the surface's amount 1 (HDRP's threshold 12.233),
    // intensity 0.54, tiling 0.25, smoothness 0.3.
    const float foamIntensity=0.54f;
    sea.foam[0]=threshold(1.0); sea.foam[1]=foamIntensity; sea.foam[2]=0.7f; sea.foam[3]=0.25f;
    sea.foamFlow[0]=1; sea.foamFlow[1]=0; sea.foamFlow[2]=10; sea.foamFlow[3]=0.6f;
    sea.foamMaps[0]=foamShared.handle; sea.foamMaps[1]=-1;
    sea.foamColor[0]=sea.foamColor[1]=sea.foamColor[2]=1;
    // The surface's own smoothness as the game's "Ocean" has it (the same log line): 0.947 near, 0 far, from 100 m over 500 m.
    sea.flags=TGPT_WATER_SMOOTHNESS; sea.scattering[3]=0.947f; sea.foamColor[3]=0.f; sea.smoothnessFade[0]=100.f; sea.smoothnessFade[1]=500.f;

    // A beach that slopes into the sea at 8 in 100 (the waterline along x = 0), the sea's level quad over it, two
    // kilometres of each; the camera on the beach, 2.6 m over the sand, looking out and down as the game's did.
    const float span=2048, slope=-0.08f;
    M4 beach=translate_scale({0, 0, 0}, {span, 1, span}); beach.m[1][0]=slope*span;
    {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        for(int k=0;k<2;++k) {
            const M4 matrix=k ? translate_scale({0, 0, 0}, {span, 1, span}) : beach;
            TGPT_Instance in{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
            in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k ? TGPT_INSTANCE_WATER : 0u;
            ids.push_back(12); list.push_back(in);
        }
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "sea test scene");
        objects.clear(); objects.push_back({0, beach, {4}, false, false});
    }
    const Camera shore=make_camera({-8.f, 3.2f, 0.f}, {22.f, -4.f, 6.f}, 58.7f, float(W)/float(H));
    auto seaRay=[&](UINT x, UINT y) {
        const float clip[4]={2*(float(x)+0.5f)/W-1, 1-2*(float(y)+0.5f)/H, 1, 1}; float out[4]{};
        for(int r=0;r<4;++r) for(int c=0;c<4;++c) out[r]+=shore.invViewProj.m[r][c]*clip[c];
        return normalize(V3{out[0]/out[3], out[1]/out[3], out[2]/out[3]});
    };
    // What a pixel shows by its camera ray and the level surfaces: 1 the sea within 60 m, 2 the sea beyond 150 m, 3 the
    // dry sand, 4 the sky just over the horizon, 0 anything else (the waterline, the bed seen through shallow water).
    std::vector<uint8_t> region(size_t(W)*H, 0);
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
        const V3 d=seaRay(x, y);
        uint8_t kind=0;
        if(d.y>0.01f) kind=d.y<0.08f ? 4 : 0;
        else if(d.y<-1e-4f) {
            const double level=-double(shore.position.y)/double(d.y), atX=double(shore.position.x)+double(d.x)*level;
            const double dry=(double(slope)*double(shore.position.x)-double(shore.position.y))/(double(d.y)-double(slope)*double(d.x));
            const double dryX=double(shore.position.x)+double(d.x)*dry;
            if(dry>0 && dryX<-1.0) kind=3;
            else if(atX>6.0 && atX<60.0) kind=1;
            else if(atX>150.0) kind=2;
        }
        region[size_t(y)*W+x]=kind;
    }
    uint32_t seaSerial=90000;
    struct SeaNumbers { double cover=0, coverMost=0, foamy=0, thin=0, nearSea=0, farSea=0, sandLight=0, skyLight=0; double nearColour[3]={0, 0, 0}; };
    auto picture=[&](const char* name, uint32_t options, uint32_t frames) {
        SeaNumbers numbers;
        expect(SetWaterCaustics(&sea, 1)==0, "sea test water");
        cut=true; run(TGPT_VIEW_NONE, shore, shore, seaSerial++, options); cut=false;
        for(int i=0;i<12;++i) run(TGPT_VIEW_ALBEDO, shore, shore, seaSerial++, options);
        const auto cover=readback();
        std::vector<float> mean(size_t(W)*H*3, 0);
        for(uint32_t i=0;i<frames+12;++i) {
            run(TGPT_VIEW_NONE, shore, shore, seaSerial++, options);
            if(i<12) continue;
            const auto frame=readback();
            for(size_t p=0;p<mean.size();++p) mean[p]+=frame[p]/float(frames);
        }
        char file[96]; std::snprintf(file, sizeof(file), "water-sea-%s.png", name); save(mean, file, true);
        std::snprintf(file, sizeof(file), "water-sea-%s-foam.png", name); save(cover, file, false);
        double count[5]={0, 0, 0, 0, 0}, light[5]={0, 0, 0, 0, 0};
        for(size_t p=0;p<region.size();++p) {
            const uint8_t kind=region[p];
            if(!kind) continue;
            const double l=0.2126*mean[p*3]+0.7152*mean[p*3+1]+0.0722*mean[p*3+2];
            count[kind]+=1; light[kind]+=l;
            if(kind==1) {
                const double c=cover[p*3];
                numbers.cover+=c; numbers.coverMost=std::max(numbers.coverMost, c); if(c>0.02) numbers.foamy+=1;
                for(int k=0;k<3;++k) numbers.nearColour[k]+=mean[p*3+k];
            }
        }
        // How thin the foam is: of the sea's pixels that are foam (over a half), how many have open water (under a tenth)
        // two pixels to a side, up or down. Threads and lace: nearly all. Clouds of foam metres wide: few.
        {
            double foamPixels=0, edged=0;
            for(UINT y=2;y+2<H;++y) for(UINT x=2;x+2<W;++x) {
                const size_t q=size_t(y)*W+x;
                if(region[q]!=1 || cover[q*3]<=0.5f) continue;
                foamPixels+=1;
                if(cover[(q-2)*3]<0.1f || cover[(q+2)*3]<0.1f || cover[(q-2*size_t(W))*3]<0.1f || cover[(q+2*size_t(W))*3]<0.1f) edged+=1;
            }
            numbers.thin=foamPixels>0 ? edged/foamPixels : 0.0;
        }
        for(int kind=1;kind<5;++kind) if(count[kind]>0) light[kind]/=count[kind];
        if(count[1]>0) { numbers.cover/=count[1]; numbers.foamy/=count[1]; for(double& c:numbers.nearColour) c/=count[1]; }
        numbers.nearSea=light[1]; numbers.farSea=light[2]; numbers.sandLight=light[3]; numbers.skyLight=light[4];
        ReadStatus(&status);
        std::printf("      SEA %s: foam covers %.4f of the sea within 60 m on average (at most %.3f; %.1f %% of its pixels have any; %.0f %% of its foam pixels have open water two pixels away); "
                    "the sea is %.2f times as bright as the dry sand near, %.2f times far, the sky at the horizon %.2f times "
                    "(near sea %.4f: %.4f, %.4f, %.4f; sand %.4f; pixels %g, %g, %g, %g; GPU %.2f ms, failed frames %llu)\n",
                    name, numbers.cover, numbers.coverMost, 100.0*numbers.foamy, 100.0*numbers.thin, numbers.nearSea/std::max(numbers.sandLight, 1e-9),
                    numbers.farSea/std::max(numbers.sandLight, 1e-9), numbers.skyLight/std::max(numbers.sandLight, 1e-9),
                    numbers.nearSea, numbers.nearColour[0], numbers.nearColour[1], numbers.nearColour[2], numbers.sandLight,
                    count[1], count[2], count[3], count[4], status.gpuMs, (unsigned long long)status.framesFailed);
        return numbers;
    };
    const uint32_t asInGame=TGPT_OPTION_NO_DENOISER;
    // TGPT_TEST_SEA_PART: one part alone (foam: 1 to 4; cost: 5; lamp: 6; body: 7; width: 7w; glint: 8); unset, all of them.
    char seaPart[32]{}; GetEnvironmentVariableA("TGPT_TEST_SEA_PART", seaPart, sizeof(seaPart));
    auto seaRuns=[&](const char* part) { return !seaPart[0] || !std::strcmp(seaPart, part); };
    if(seaRuns("foam")) {
    // 1. As the game sends it: three bands, the foam on, the Caustics row on.
    const SeaNumbers whole=picture("as-sent", asInGame, 48);
    expect(whole.cover<0.10, "a three-band sea is not a sheet of foam: foam covers under a tenth of it");
    expect(whole.foamy>0.002 && whole.foamy<0.35, "the sea's foam follows its waves: some of its pixels have foam, most have none");
    expect(whole.nearSea<whole.sandLight, "the open sea near the shore is darker than the sunlit sand beside it");
    expect(whole.thin>0.8, "the sea's foam is thin threads and lace, not clouds: nearly every foam pixel has open water two pixels away");
    // 2. The same sea with less of the surface's foam amount (HDRP's threshold with it: 11.35 at 0, 11.99 at 0.7, 12.23 at
    // 1). At rest the three bands' Jacobians add up to 12, so at the upper end HDRP's own measure is over 0 on water at
    // rest; the tracer's foam starts later (FOAM_ONSET), where the water is squeezed markedly: a sea at rest has none.
    sea.foam[0]=threshold(0.0); const SeaNumbers least=picture("foam-amount-0", asInGame, 16);
    sea.foam[0]=threshold(0.7); const SeaNumbers less=picture("foam-amount-0.7", asInGame, 16);
    expect(least.cover<=less.cover+0.002 && less.cover<=whole.cover+0.002 && least.cover<0.002, "more of the surface's foam amount is more foam");
    sea.foam[0]=threshold(1.0);
    // 3. The water's own colour: the same sea with no foam (the surface's foam intensity 0).
    sea.foam[1]=0; const SeaNumbers bare=picture("no-foam", asInGame, 48); sea.foam[1]=foamIntensity;
    expect(bare.cover<1e-4, "with the foam's intensity 0 the sea has no foam");
    std::printf("      SEA the bare sea's colour over the dry sand's light: %.3f, %.3f, %.3f (the surface's scattering colour: 0.257, 0.604, 0.469)\n",
                bare.nearColour[0]/std::max(bare.sandLight, 1e-9), bare.nearColour[1]/std::max(bare.sandLight, 1e-9), bare.nearColour[2]/std::max(bare.sandLight, 1e-9));
    // 4. With the Caustics row off (a light's straight ray passes the surface): for the difference alone.
    picture("row-off", asInGame|TGPT_OPTION_NO_CAUSTICS, 48);
    }
    // 5. What the caustics stage costs where no light reaches the water (PT-80: 12 ms a frame in a forest with no water in
    // view, the open sea's level running under the whole land): this sea under ground, a slab 1 m over it from edge to
    // edge with the camera over the slab, and four lamps over the ground whose light has no range, as the game's lamps
    // (so every tile of the caustics' grid has its three lights). Each stage's GPU time, first with the sea open.
    auto stageTimes=[&](const char* name) {
        cut=true; run(TGPT_VIEW_NONE, shore, shore, seaSerial++, asInGame); cut=false;
        for(int i=0;i<90;++i) { run(TGPT_VIEW_NONE, shore, shore, seaSerial++, asInGame); context->Flush(); }
        char stages[2048]{}; if(GetStageTimes) GetStageTimes(stages, sizeof(stages));
        ReadStatus(&status);
        std::printf("      SEA COST %s: %s; GPU %.2f ms a frame\n", name, stages, status.gpuMs);
    };
    if(seaRuns("cost")) {
        std::vector<TGPT_Light> lamps;
        for(int i=0;i<4;++i) {
            TGPT_Light lamp{}; lamp.position[0]=-6.f+4.f*float(i); lamp.position[1]=3.f; lamp.position[2]=-5.f+3.f*float(i); lamp.type=0;
            lamp.color[0]=40.f; lamp.color[1]=26.f; lamp.color[2]=12.f; lamp.range=12.f; lamp.radius=0.04f; lamp.id=900+uint32_t(i);
            lamp.flags|=TGPT_LIGHT_NO_RANGE;
            lamps.push_back(lamp);
        }
        expect(SetLights(lamps.data(), uint32_t(lamps.size()))==0, "sea cost run lamps");
        stageTimes("the sea open to the sun and four lamps");
        // PT-131 (docs/agents/campspeed.md): the same sea in the camera's view under a roof 30 m up, from edge to edge. No
        // sun reaches the water, and every scatter point's sun through the waves (its tiles, or its own patch of waves)
        // ends at a shadow ray the roof stops: what that work costs when it brings nothing.
        {
            const M4 roof=translate_scale({0, 30, 0}, {span, 1, span});
            std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
            for(int k=0;k<3;++k) {
                const M4 matrix=k==0 ? beach : k==1 ? translate_scale({0, 0, 0}, {span, 1, span}) : roof;
                TGPT_Instance in{};
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
                in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k==1 ? TGPT_INSTANCE_WATER : 0u;
                ids.push_back(12); list.push_back(in);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "sea cost run scene under a roof");
            expect(SetWaterCaustics(&sea, 1)==0, "sea cost run water under a roof");
            stageTimes("the same sea under a roof 30 m up (in view; no sun reaches it)");
        }
        const M4 ground=translate_scale({0, 1, 0}, {span, 1, span});
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        for(int k=0;k<3;++k) {
            const M4 matrix=k==0 ? beach : k==1 ? translate_scale({0, 0, 0}, {span, 1, span}) : ground;
            TGPT_Instance in{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
            in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k==1 ? TGPT_INSTANCE_WATER : 0u;
            ids.push_back(12); list.push_back(in);
        }
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "sea cost run scene under ground");
        expect(SetWaterCaustics(&sea, 1)==0, "sea cost run water");
        objects.clear(); objects.push_back({0, ground, {4}, false, false});
        stageTimes("the same sea under ground (no light reaches it)");
        SetLights(nullptr, 0);
    }
    // 6. A lamp inside its own opaque emitter (a lantern's glass ball the game draws opaque, a crystal's body: the
    // harness's lantern was one, PT-80) throws the same light through the water as the bare lamp. The caustics' shadow
    // ray aims at a point inside the emitter's ball; ended 5 cm short of that point alone it met the emitter's own body
    // for the points on the ball's far side, and that frame's caustic of the lamp was gone. A lamp 0.5 m over 0.3 m of
    // water at night, its ball 7 cm, a box of 8 cm in it; the bed's light under it (through the water: the caustics'
    // alone), the same frames with and without the box.
    if(seaRuns("lamp")) {
        sunOn=false; skyIntensity=1e-6f;
        sea.foam[1]=0;
        TGPT_Light lamp{}; lamp.position[0]=4.f; lamp.position[1]=0.5f; lamp.position[2]=0.f; lamp.type=0;
        lamp.color[0]=40.f; lamp.color[1]=26.f; lamp.color[2]=12.f; lamp.range=12.f; lamp.radius=0.07f; lamp.id=950;
        lamp.flags|=TGPT_LIGHT_AT_EMITTER;
        expect(SetLights(&lamp, 1)==0, "sea lamp");
        const Camera over=make_camera({5.5f, 3.f, -0.01f}, {5.5f, -0.4f, 0.f}, 40.f, float(W)/float(H));
        auto bedLight=[&](bool boxed, const char* name) {
            std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
            for(int k=0;k<(boxed ? 3 : 2);++k) {
                const M4 matrix=k==0 ? beach : k==1 ? translate_scale({0, 0, 0}, {span, 1, span}) : translate_scale({4.f, 0.5f, 0.f}, {0.08f, 0.08f, 0.08f});
                TGPT_Instance in{};
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
                in.mesh=k==2 ? cubeHandle : quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k==1 ? TGPT_INSTANCE_WATER : 0u;
                ids.push_back(12); if(k==2) ids.push_back(12);
                list.push_back(in);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "sea lamp scene");
            expect(SetWaterCaustics(&sea, 1)==0, "sea lamp water");
            objects.clear(); objects.push_back({0, beach, {4}, false, false});
            uint32_t lampSerial=95000; // the same frames for both: the same points of the lamp's ball, the same grid
            std::vector<float> mean(size_t(W)*H*3, 0);
            cut=true; run(TGPT_VIEW_NONE, over, over, lampSerial++, asInGame); cut=false;
            for(int i=0;i<108;++i) {
                run(TGPT_VIEW_NONE, over, over, lampSerial++, asInGame);
                if(i<12) continue;
                const auto frame=readback();
                for(size_t p=0;p<mean.size();++p) mean[p]+=frame[p]/96.f;
            }
            save(mean, name, true);
            // The bed's light without the lamp's own mirror image in the water: the mean under the 99th percentile.
            std::vector<double> light(size_t(W)*H);
            for(size_t p=0;p<light.size();++p) light[p]=0.2126*mean[p*3]+0.7152*mean[p*3+1]+0.0722*mean[p*3+2];
            std::vector<double> sorted=light; std::sort(sorted.begin(), sorted.end());
            const double cap=sorted[sorted.size()*99/100];
            double sum=0; for(double v:light) sum+=std::min(v, cap);
            return sum/double(light.size());
        };
        const double bare=bedLight(false, "water-sea-lamp-bare.png"), boxed=bedLight(true, "water-sea-lamp-in-its-emitter.png");
        std::printf("      SEA a lamp over the water: the bed's light with the lamp inside an opaque emitter is %.3f of the bare lamp's (%.5f, %.5f)\n",
                    boxed/std::max(bare, 1e-12), boxed, bare);
        expect(bare>1e-4 && boxed>0.95*bare, "a lamp inside its own opaque emitter throws the same light through the water");
        SetLights(nullptr, 0); sea.foam[1]=foamIntensity; sunOn=true; skyIntensity=1;
    }
    // 6b. The caustics' grid, tile by tile (PT-114, docs/agents/caustgrid.md): by its name alone, TGPT_TEST_SEA_PART=grid.
    #include "water_grid_gpu.inl"
    // 7. The sun lights the water's body (PT-81, PT-85). Until 2026-10-08 a path inside the water got the sun only when it
    // happened to leave straight at the sun's disc: one path in a hundred thousand, carrying it all (single green dots, a
    // dark sea in every other frame). Now every scatter point asks for it through the waves (pt_water_sun.hlsli).
    // A camera 4 m over water 24 m deep, 300 m out, looking down and away from the sun (so the sun's mirror image is not in
    // the picture): every pixel is the water's body and the sky's mirror image; the bed is out of the light's reach.
    // (The camera and its frames stand before the part: the part after it, the cell by the ray's width, takes the same.)
    const Camera deep=make_camera({300.f, 4.f, 0.f}, {303.f, 0.f, -4.f}, 58.7f, float(W)/float(H));
    struct Deep { std::vector<float> mean; double colour[3]={0, 0, 0}; double largest=0; double gpuMs=0; std::string stages; };
    // The mean of `count` frames, each pixel's colour; and the largest single sample of any pixel (its luminance).
    auto frames=[&](const char* name, uint32_t options, uint32_t count, bool sun) {
        Deep d; d.mean.assign(size_t(W)*H*3, 0.f);
        sunOn=sun;
        expect(SetWaterCaustics(&sea, 1)==0, "sea body water");
        cut=true; run(TGPT_VIEW_NONE, deep, deep, seaSerial++, options); cut=false;
        for(uint32_t i=0;i<count+12;++i) {
            run(TGPT_VIEW_NONE, deep, deep, seaSerial++, options);
            if(i<12) continue;
            const auto frame=readback();
            ReadStatus(&status); d.gpuMs+=double(status.gpuMs)/double(count); // (the status holds the last frame's time: the mean of them all)
            for(size_t p=0;p<frame.size();p+=3) {
                for(int k=0;k<3;++k) d.mean[p+k]+=frame[p+k]/float(count);
                d.largest=std::max(d.largest, 0.2126*double(frame[p])+0.7152*double(frame[p+1])+0.0722*double(frame[p+2]));
            }
        }
        for(size_t p=0;p<d.mean.size();p+=3) for(int k=0;k<3;++k) d.colour[k]+=double(d.mean[p+k])/double(size_t(W)*H);
        { char stages[2048]{}; if(GetStageTimes) GetStageTimes(stages, sizeof(stages)); d.stages=stages; }
        if(name) { char file[96]; std::snprintf(file, sizeof(file), "water-sea-body-%s.png", name); save(d.mean, file, true); }
        sunOn=true;
        return d;
    };
    if(seaRuns("body")) {
        sea.foam[1]=0;
        const double waterIndex=1.3333;
        auto fresnel=[&](double cosIn) {
            const double sin2=(1.0-cosIn*cosIn)/(waterIndex*waterIndex), cosOut=std::sqrt(std::max(1.0-sin2, 0.0));
            const double rs=(cosIn-waterIndex*cosOut)/(cosIn+waterIndex*cosOut), rp=(waterIndex*cosIn-cosOut)/(waterIndex*cosIn+cosOut);
            return 0.5*(rs*rs+rp*rp);
        };
        auto deepRay=[&](UINT x, UINT y) {
            const float clip[4]={2*(float(x)+0.5f)/W-1, 1-2*(float(y)+0.5f)/H, 1, 1}; float out[4]{};
            for(int r=0;r<4;++r) for(int c=0;c<4;++c) out[r]+=deep.invViewProj.m[r][c]*clip[c];
            return normalize(V3{out[0]/out[3], out[1]/out[3], out[2]/out[3]});
        };
        const uint64_t deepWaves=sea.displacement, deepNormals=sea.normals;
        const float gameScatter[3]={sea.scattering[0], sea.scattering[1], sea.scattering[2]};
        // (a) Still water with a fiftieth of the game's scattering colour, so that one scattering is all but 2 % of the light:
        // the sun's share of the picture (the sun on, less the sun off) against the closed form. A ray into still deep
        // water that scatters once: (1 - F(view)) / n^2 x albedo / 4 pi x E (1 - F(sun)) cos(sun) / cos(sun in the water)
        // / (1 + cos(view in the water) / cos(sun in the water)); what the water takes a metre drops out.
        sea.displacement=0; sea.normals=0;
        for(int k=0;k<3;++k) sea.scattering[k]=0.02f*gameScatter[k];
        const Deep stillLit=frames("still-thin", asInGame, 96, true), stillDark=frames(nullptr, asInGame, 96, false);
        // What turning the sun on does to a picture the sun cannot light (the same water with no scattering colour at all):
        // not nothing (about -0.0001: the traced light is kept in units of the brightest light, so its rounding moves with
        // the sun). The sun's share of the picture is taken clear of it.
        for(int k=0;k<3;++k) sea.scattering[k]=0;
        const Deep noneLit=frames(nullptr, asInGame, 96, true), noneDark=frames(nullptr, asInGame, 96, false);
        for(int k=0;k<3;++k) sea.scattering[k]=0.02f*gameScatter[k];
        double closed[3]={0, 0, 0};
        const double sunCos=double(sunDir.y), sunUnder=std::sqrt(1.0-(1.0-sunCos*sunCos)/(waterIndex*waterIndex));
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const V3 d=deepRay(x, y);
            const double viewCos=-double(d.y), viewUnder=std::sqrt(1.0-(1.0-viewCos*viewCos)/(waterIndex*waterIndex));
            const double one=(1.0-fresnel(viewCos))/(waterIndex*waterIndex)/(4.0*3.141592653589793)*double(sunLux)*(1.0-fresnel(sunCos))*sunCos/sunUnder/(1.0+viewUnder/sunUnder);
            for(int k=0;k<3;++k) closed[k]+=one*double(sea.scattering[k])/double(size_t(W)*H);
        }
        double stillShare[3]; for(int k=0;k<3;++k) stillShare[k]=stillLit.colour[k]-stillDark.colour[k]-(noneLit.colour[k]-noneDark.colour[k]);
        std::printf("      SEA BODY still water, a fiftieth of the game's scattering: the sun's share of the picture %.5f, %.5f, %.5f; the closed form for one scattering "
                    "%.5f, %.5f, %.5f (x%.3f, x%.3f, x%.3f); with the sun off the picture is %.5f, %.5f, %.5f; the sun's share of the same picture with no scattering at all %.5f, %.5f, %.5f (taken off)\n",
                    stillShare[0], stillShare[1], stillShare[2], closed[0], closed[1], closed[2],
                    stillShare[0]/closed[0], stillShare[1]/closed[1], stillShare[2]/closed[2], stillDark.colour[0], stillDark.colour[1], stillDark.colour[2],
                    noneLit.colour[0]-noneDark.colour[0], noneLit.colour[1]-noneDark.colour[1], noneLit.colour[2]-noneDark.colour[2]);
        bool closedOk=true; for(int k=0;k<3;++k) closedOk=closedOk && stillShare[k]>0.97*closed[k] && stillShare[k]<1.05*closed[k];
        expect(closedOk, "the sun lights still deep water's body as the closed form for one scattering says (a further 2 % at most from the next ones)");
        // (b) The game's sea: its waves and its own scattering colour. The waves move the light about inside the water and
        // take none away: the sun's share of the picture is the still water's with the same colour (within 8 %: the
        // surface mirrors a little more of a slanting sun on a wave's far side, a little less on its near side).
        for(int k=0;k<3;++k) sea.scattering[k]=gameScatter[k];
        const Deep flatLit=frames("still", asInGame, 64, true), flatDark=frames(nullptr, asInGame, 64, false);
        sea.displacement=deepWaves; sea.normals=deepNormals;
        const Deep few=frames("waves-16", asInGame, 16, true);
        const Deep many=frames("waves", asInGame, 256, true), dark=frames("waves-sun-off", asInGame, 64, false);
        double flatShare[3], waveShare[3];
        for(int k=0;k<3;++k) { flatShare[k]=flatLit.colour[k]-flatDark.colour[k]; waveShare[k]=many.colour[k]-dark.colour[k]; }
        std::printf("      SEA BODY the game's sea 24 m deep: the sun's share of the picture with its waves %.5f, %.5f, %.5f, as still water %.5f, %.5f, %.5f "
                    "(x%.3f, x%.3f, x%.3f); the whole picture %.5f, %.5f, %.5f, with the sun off %.5f, %.5f, %.5f\n",
                    waveShare[0], waveShare[1], waveShare[2], flatShare[0], flatShare[1], flatShare[2],
                    waveShare[0]/flatShare[0], waveShare[1]/flatShare[1], waveShare[2]/flatShare[2],
                    many.colour[0], many.colour[1], many.colour[2], dark.colour[0], dark.colour[1], dark.colour[2]);
        bool wavesOk=true; for(int k=0;k<3;++k) wavesOk=wavesOk && waveShare[k]>0.92*flatShare[k] && waveShare[k]<1.08*flatShare[k];
        expect(wavesOk, "waves move the sun's light about inside the water and take none: the wavy sea's body is as bright as still water's");
        expect(waveShare[1]>waveShare[0] && waveShare[1]>waveShare[2], "the sunlit sea's body is green");
        // (c) No picture depends on a rare bright sample: 16 frames show the colour 256 frames do, and no single sample of
        // any pixel is far over the picture's mean (a path that ran into the sun's disc carried about 100,000 times it).
        const double lum16=0.2126*few.colour[0]+0.7152*few.colour[1]+0.0722*few.colour[2], lum256=0.2126*many.colour[0]+0.7152*many.colour[1]+0.0722*many.colour[2];
        std::printf("      SEA BODY 16 frames' mean is %.4f of 256 frames'; the largest single sample of any pixel is %.1f times the picture's mean in 256 frames "
                    "(%.1f in the 16, %.1f with the sun off, %.1f in still water); GPU %.2f ms a frame with the sun, %.2f without, %.2f in still water\n",
                    lum16/lum256, many.largest/lum256, few.largest/lum256, dark.largest/lum256, flatLit.largest/lum256, many.gpuMs, dark.gpuMs, flatLit.gpuMs);
        expect(std::fabs(lum16/lum256-1.0)<0.03, "the sea's colour in 16 frames is its colour in 256: no rare bright sample carries it");
        expect(many.largest<60.0*lum256, "no sample in the water is far over the picture's mean: no path carries the whole sun");
        // (c2) The wave corners kept, in tiles (docs/agents/water-8.md: a slot's corners made once a frame, a record a tile
        // of what its corners' rays can do, a point summing the few tiles a ray can reach it through). The same sea with
        // every patch made at its own point (TGPT_TEST_SUN_GRID=0: no grid, the way before): the same light, and each
        // way's time. (The frames above ran as the game does: from the tiles.) And with 2 and with 8 of the tiles left
        // summed (TGPT_TEST_SUN_PICK; the tracer's own number is between): the same light again, other noise, other time.
        {
            char before[16]{}; const bool had=GetEnvironmentVariableA("TGPT_TEST_SUN_GRID", before, sizeof(before))>0;
            SetEnvironmentVariableA("TGPT_TEST_SUN_GRID", "0");
            const Deep atPoint=frames("waves-at-point", asInGame, 256, true);
            SetEnvironmentVariableA("TGPT_TEST_SUN_GRID", had ? before : nullptr);
            SetEnvironmentVariableA("TGPT_TEST_SUN_PICK", "2");
            const Deep two=frames(nullptr, asInGame, 256, true);
            SetEnvironmentVariableA("TGPT_TEST_SUN_PICK", "8");
            const Deep eight=frames(nullptr, asInGame, 256, true);
            SetEnvironmentVariableA("TGPT_TEST_SUN_PICK", nullptr);
            double pointShare[3], twoShare[3], eightShare[3]; bool same=true;
            for(int k=0;k<3;++k) {
                pointShare[k]=atPoint.colour[k]-dark.colour[k]; twoShare[k]=two.colour[k]-dark.colour[k]; eightShare[k]=eight.colour[k]-dark.colour[k];
                same=same && std::fabs(waveShare[k]/pointShare[k]-1.0)<0.01 && std::fabs(twoShare[k]/pointShare[k]-1.0)<0.01 && std::fabs(eightShare[k]/pointShare[k]-1.0)<0.01;
            }
            const double lumPoint=0.2126*atPoint.colour[0]+0.7152*atPoint.colour[1]+0.0722*atPoint.colour[2];
            std::printf("      SEA BODY the corners kept, in tiles: the sun's share of the picture is x%.4f, x%.4f, x%.4f of the same sea with every patch made at its point "
                        "(%.5f, %.5f, %.5f); with 2 tiles summed x%.4f, x%.4f, x%.4f, with 8 x%.4f, x%.4f, x%.4f; the largest single sample %.1f times the mean from the tiles, "
                        "%.1f made at the point, %.1f with 2 tiles, %.1f with 8; GPU %.2f ms a frame from the tiles, %.2f made at the point, %.2f with 2 tiles, %.2f with 8, %.2f with the sun off "
                        "(the sun's part: %.2f ms against %.2f made at the point)\n",
                        waveShare[0]/pointShare[0], waveShare[1]/pointShare[1], waveShare[2]/pointShare[2], pointShare[0], pointShare[1], pointShare[2],
                        twoShare[0]/pointShare[0], twoShare[1]/pointShare[1], twoShare[2]/pointShare[2], eightShare[0]/pointShare[0], eightShare[1]/pointShare[1], eightShare[2]/pointShare[2],
                        many.largest/lum256, atPoint.largest/lumPoint, two.largest/lumPoint, eight.largest/lumPoint, many.gpuMs, atPoint.gpuMs, two.gpuMs, eight.gpuMs, dark.gpuMs,
                        many.gpuMs-dark.gpuMs, atPoint.gpuMs-dark.gpuMs);
            std::printf("      SEA BODY stages from the tiles: %s\n      SEA BODY stages made at the point: %s\n", many.stages.c_str(), atPoint.stages.c_str());
            expect(same, "the sun's light from the kept wave corners' tiles is the light of patches made at their points (within a hundredth)");
            expect(atPoint.largest<60.0*lumPoint, "made at the point, no sample in the water is far over the picture's mean either");
        }
        // (d) The Caustics row off: the straight light, unbent and with no loss at the surface (the row's own meaning).
        const Deep rowOff=frames("waves-row-off", asInGame|TGPT_OPTION_NO_CAUSTICS, 64, true), rowOffDark=frames(nullptr, asInGame|TGPT_OPTION_NO_CAUSTICS, 64, false);
        std::printf("      SEA BODY the Caustics row off: the sun's share of the picture %.5f, %.5f, %.5f (x%.3f, x%.3f, x%.3f of the row on); the largest sample %.1f times the mean; GPU %.2f ms\n",
                    rowOff.colour[0]-rowOffDark.colour[0], rowOff.colour[1]-rowOffDark.colour[1], rowOff.colour[2]-rowOffDark.colour[2],
                    (rowOff.colour[0]-rowOffDark.colour[0])/waveShare[0], (rowOff.colour[1]-rowOffDark.colour[1])/waveShare[1], (rowOff.colour[2]-rowOffDark.colour[2])/waveShare[2],
                    rowOff.largest/lum256, rowOff.gpuMs);
        expect(rowOff.colour[1]-rowOffDark.colour[1]>0.8*waveShare[1] && rowOff.largest<60.0*lum256, "with the Caustics row off the sun lights the body by its straight ray, and no path carries the whole sun");
        sea.foam[1]=foamIntensity;
    }
    // 7w. The sun's patch made at a point: the cell by the ray's width (PT-131, docs/agents/campfix.md;
    // pt_water_sun.hlsli). What it must keep is the PICTURE: its mean, and its pattern at the size of a band of it. The
    // same deep sea as part 7, every patch made at its point (TGPT_TEST_SUN_GRID=0), the sun's share of the picture (the
    // sun on, less the sun off) over 256 frames.
    //   THE CELL: a patch is cut into cells no finer than half the width of the path's own ray there. A coarser cell is
    // summed as every other (a triangle's sun by the area of its three moved corners across the light).
    //   every path's ray taken as 1 cm wide: no cell is made coarser, which is the patch as it was before the rule
    //   ("the rule off": what the others are held against; not by the hidden Speed trial 64, so that this part still
    //   says something once that switch is gone);
    //   the rule on, as the game runs it (it acts here too: after a path's first scatter point its ray is a bounce's);
    //   every path's ray taken as 1 m wide, and as 10 km wide (TGPT_TEST_SUN_WIDTH, centimetres): the coarsest cells
    //   there are (half the way to the surface).
    // Once at the fixture's sun (57 degrees up) and once at a sun 8 degrees up. Held to: the share of the whole picture
    // within 0.5 % at the high sun (within 2 % at the low one, where the share is a tenth of the picture and carries
    // ten times its noise), and at the high sun the share of each of the picture's 8 upright and 8 level bands within
    // 1 % (said for the low sun, not held).
    // (2026-10-12: the first form of the rule gave a coarser cell's corner "its own facet's sun". This part, then with
    // looser numbers and no bands, showed the share 0.6 to 1.1 % up and one band 1.8 % brighter, while the same cells
    // summed the plain way were within 0.05 %, whole and band by band: that form came out. The same day's other rule,
    // "the shadow first", had a part here under half a roof; it gained nothing in the game and came out: campfix.md.)
    if(seaRuns("width")) {
        sea.foam[1]=0;
        char gridBefore[16]{}; const bool hadGrid=GetEnvironmentVariableA("TGPT_TEST_SUN_GRID", gridBefore, sizeof(gridBefore))>0;
        SetEnvironmentVariableA("TGPT_TEST_SUN_GRID", "0");
        const float trialBefore=speedTrial; const V3 sunBefore=sunDir;
        struct WidthSun { const char* name; const char* file; float height; double whole; bool bandsHeld; };
        const WidthSun widthSuns[2]={{"the fixture's sun, 57 degrees up", "", 1.f, 0.005, true}, {"a sun 8 degrees up", "-low", 0.0916f, 0.02, false}};
        // A band's share of the sun (green): 8 upright bands, then 8 level ones.
        auto bands=[&](const Deep& lit, const Deep& dark) {
            std::array<double, 16> sum{};
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const double v=double(lit.mean[(size_t(y)*W+x)*3+1])-double(dark.mean[(size_t(y)*W+x)*3+1]);
                sum[std::min<UINT>(x*8/W, 7)]+=v; sum[8+std::min<UINT>(y*8/H, 7)]+=v;
            }
            return sum;
        };
        for(const WidthSun& ws:widthSuns) {
            sunDir=normalize({0.35f, ws.height, 0.55f});
            char file[4][48];
            const char* kinds[4]={"off", "own", "1m", "wide"};
            for(int k=0;k<4;++k) std::snprintf(file[k], sizeof(file[k]), "width%s-%s", ws.file, kinds[k]);
            SetEnvironmentVariableA("TGPT_TEST_SUN_WIDTH", nullptr);
            speedTrial=0.f;
            const Deep dark=frames(nullptr, asInGame, 256, false); // (as many frames as the lit ones: a low sun's share is small)
            SetEnvironmentVariableA("TGPT_TEST_SUN_WIDTH", "1"); const Deep before=frames(file[0], asInGame, 256, true);
            SetEnvironmentVariableA("TGPT_TEST_SUN_WIDTH", nullptr); const Deep own=frames(file[1], asInGame, 256, true);
            SetEnvironmentVariableA("TGPT_TEST_SUN_WIDTH", "100"); const Deep metre=frames(file[2], asInGame, 256, true);
            SetEnvironmentVariableA("TGPT_TEST_SUN_WIDTH", "1000000"); const Deep wide=frames(file[3], asInGame, 256, true);
            SetEnvironmentVariableA("TGPT_TEST_SUN_WIDTH", nullptr);
            const Deep* runs[3]={&own, &metre, &wide};
            double share[3], ratio[3][3], worstBand[3]={0, 0, 0}; bool wholeSame=true, bandsSame=true;
            const std::array<double, 16> bandBefore=bands(before, dark);
            for(int r=0;r<3;++r) {
                const std::array<double, 16> band=bands(*runs[r], dark);
                for(int k=0;k<16;++k) worstBand[r]=std::max(worstBand[r], std::fabs(band[k]/std::max(bandBefore[k], 1e-12)-1.0));
                bandsSame=bandsSame && worstBand[r]<0.01;
            }
            for(int k=0;k<3;++k) {
                share[k]=before.colour[k]-dark.colour[k];
                for(int r=0;r<3;++r) {
                    ratio[r][k]=(runs[r]->colour[k]-dark.colour[k])/std::max(share[k], 1e-12);
                    wholeSame=wholeSame && std::fabs(ratio[r][k]-1.0)<ws.whole;
                }
            }
            const double lum=0.2126*before.colour[0]+0.7152*before.colour[1]+0.0722*before.colour[2];
            std::printf("      SEA WIDTH %s: the sun's share of the picture with the rule off %.5f, %.5f, %.5f (the whole picture %.5f, %.5f, %.5f; with the sun off %.5f, %.5f, %.5f); "
                        "the rule on, the camera's own rays x%.4f, x%.4f, x%.4f; every ray taken as 1 m wide x%.4f, x%.4f, x%.4f; as 10 km wide x%.4f, x%.4f, x%.4f; "
                        "the band of the picture farthest from its own share with the rule off (16 bands, green): %.2f %% with the camera's own rays, %.2f %% at 1 m, %.2f %% at 10 km; "
                        "the largest single sample %.1f times the mean with the rule off, %.1f on, %.1f at 1 m, %.1f at 10 km; "
                        "GPU %.2f ms a frame with the rule off, %.2f on, %.2f at 1 m, %.2f at 10 km, %.2f with the sun off\n",
                        ws.name, share[0], share[1], share[2], before.colour[0], before.colour[1], before.colour[2], dark.colour[0], dark.colour[1], dark.colour[2],
                        ratio[0][0], ratio[0][1], ratio[0][2], ratio[1][0], ratio[1][1], ratio[1][2], ratio[2][0], ratio[2][1], ratio[2][2],
                        100.0*worstBand[0], 100.0*worstBand[1], 100.0*worstBand[2],
                        before.largest/lum, own.largest/lum, metre.largest/lum, wide.largest/lum, before.gpuMs, own.gpuMs, metre.gpuMs, wide.gpuMs, dark.gpuMs);
            std::printf("      SEA WIDTH stages with the rule off: %s\n      SEA WIDTH stages at 10 km: %s\n", before.stages.c_str(), wide.stages.c_str());
            expect(share[1]>0.0, "the sun lights the deep sea's body in the width part's frames");
            expect(wholeSame, ws.bandsHeld ? "the cell by the ray's width keeps the mean of the sun's light in the water (the camera's own rays, 1 m and 10 km: within 0.5 %)"
                                           : "the cell by the ray's width keeps the mean of the sun's light in the water under a low sun (the camera's own rays, 1 m and 10 km: within 2 %)");
            if(ws.bandsHeld) expect(bandsSame, "the cell by the ray's width keeps the picture's pattern at the size of a band (each of 16 bands' share of the sun within 1 %)");
        }
        sunDir=sunBefore;
        speedTrial=trialBefore;
        SetEnvironmentVariableA("TGPT_TEST_SUN_GRID", hadGrid ? gridBefore : nullptr);
        sea.foam[1]=foamIntensity;
    }
    // 8. The water's own smoothness (PT-81: HDRP's, 0.947 near the camera for the game's sea, 0 far out). Until 2026-10-08 the
    // tracer's water was a clear mirror facet, and the sun's glint a chance hit of the sun's disc. Now a facet of the
    // surface, drawn from the water's GGX lobe, mirrors the path, and the sun is asked for through that lobe.
    // Still water with no scattering colour (so the sun's share of the picture is its mirror image alone), 300 m out, the
    // camera 4 m over it looking where still water shows the sun: the image is in the picture's middle.
    if(seaRuns("glint")) {
        sea.foam[1]=0;
        const uint64_t glintWaves=sea.displacement, glintNormals=sea.normals;
        const float glintScatter[4]={sea.scattering[0], sea.scattering[1], sea.scattering[2], sea.scattering[3]};
        const float glintFar=sea.foamColor[3];
        sea.displacement=0; sea.normals=0; sea.scattering[0]=sea.scattering[1]=sea.scattering[2]=0;
        const V3 eye{300.f, 4.f, 0.f};
        const Camera toSun=make_camera(eye, {eye.x+sunDir.x, eye.y-sunDir.y, eye.z+sunDir.z}, 58.7f, float(W)/float(H));
        const V3 forward=normalize(V3{sunDir.x, -sunDir.y, sunDir.z});
        const double tanHalf=std::tan(58.7*3.141592653589793/360.0), pixelAngle=(2.0*tanHalf/double(H))*(2.0*tanHalf/double(H));
        const double waterIndex=1.3333, sunCos=double(sunDir.y);
        const double sin2=(1.0-sunCos*sunCos)/(waterIndex*waterIndex), cosOut=std::sqrt(1.0-sin2);
        const double rs=(sunCos-waterIndex*cosOut)/(sunCos+waterIndex*cosOut), rp=(waterIndex*sunCos-cosOut)/(waterIndex*sunCos+cosOut);
        // The composite caps a pixel at 256 times the exposure's white (pt_compose rr_clamp: what is brighter is burnt out on
        // any screen), and the sun's image in smooth water is over 1,000 at this fixture's exposure of 1: at a sixty-fourth
        // of it the whole image is under the cap and its light can be added up.
        const float glintExposure=1.f/64.f;
        setExposure(glintExposure);
        const double mirrored=0.5*(rs*rs+rp*rp)*double(sunLux)*double(glintExposure); // what a mirror sends the camera of the sun: Fresnel's share of its light
        struct Glint { double light=0, peak=0, wide=0, burnt=0; };
        auto glint=[&](const char* name, float smoothness) {
            sea.flags=smoothness<1.f ? TGPT_WATER_SMOOTHNESS : 0u; sea.scattering[3]=smoothness; sea.foamColor[3]=smoothness;
            std::vector<float> lit(size_t(W)*H, 0.f), dark(size_t(W)*H, 0.f), most(size_t(W)*H, 0.f);
            for(int pass=0;pass<2;++pass) {
                sunOn=pass==0;
                expect(SetWaterCaustics(&sea, 1)==0, "sea glint water");
                cut=true; run(TGPT_VIEW_NONE, toSun, toSun, seaSerial++, asInGame); cut=false;
                const uint32_t count=pass==0 ? 64u : 16u;
                for(uint32_t i=0;i<count+12;++i) {
                    run(TGPT_VIEW_NONE, toSun, toSun, seaSerial++, asInGame);
                    if(i<12) continue;
                    const auto frame=readback();
                    for(size_t q=0;q<lit.size();++q) {
                        const float l=0.2126f*frame[q*3]+0.7152f*frame[q*3+1]+0.0722f*frame[q*3+2];
                        (pass==0 ? lit : dark)[q]+=l/float(count);
                        if(pass==0) most[q]=std::max(most[q], l);
                    }
                }
            }
            sunOn=true;
            Glint gl;
            for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                const float clip[4]={2*(float(x)+0.5f)/W-1, 1-2*(float(y)+0.5f)/H, 1, 1}; float out[4]{};
                for(int r=0;r<4;++r) for(int c=0;c<4;++c) out[r]+=toSun.invViewProj.m[r][c]*clip[c];
                const V3 d=normalize(V3{out[0]/out[3], out[1]/out[3], out[2]/out[3]});
                const double along=double(d.x)*forward.x+double(d.y)*forward.y+double(d.z)*forward.z;
                const size_t q=size_t(y)*W+x;
                const double share=double(lit[q])-double(dark[q]);
                gl.light+=share*pixelAngle*along*along*along;
                gl.peak=std::max(gl.peak, share);
            }
            for(size_t q=0;q<lit.size();++q) {
                const double share=double(lit[q])-double(dark[q]);
                if(share>0.1*gl.peak) gl.wide+=1;
                if(share>0.25*gl.peak) gl.burnt=std::max(gl.burnt, double(most[q])/double(lit[q]));
            }
            std::vector<float> picture(size_t(W)*H*3);
            for(size_t q=0;q<lit.size();++q) picture[q*3]=picture[q*3+1]=picture[q*3+2]=lit[q];
            char file[96]; std::snprintf(file, sizeof(file), "water-sea-glint-%s.png", name); save(picture, file, true);
            std::printf("      SEA GLINT smoothness %.3f: the sun's image sends the camera %.4f (a mirror: %.4f, x%.3f); its brightest pixel %.1f; %g pixels over a tenth of that; "
                        "the largest single sample of a pixel in it is %.1f times that pixel's mean\n",
                        double(smoothness), gl.light, mirrored, gl.light/mirrored, gl.peak, gl.wide, gl.burnt);
            return gl;
        };
        const Glint clear=glint("mirror", 1.f), game=glint("game", 0.947f), rough=glint("rough", 0.7f);
        expect(std::fabs(clear.light/mirrored-1.0)<0.06, "still water's image of the sun carries Fresnel's share of the sun's light");
        expect(std::fabs(game.light/clear.light-1.0)<0.08, "the water's own smoothness spreads the sun's glint and keeps its light");
        // (The smoothest surface's lobe towards a light is as wide as a smoothness of 0.955's, roughness_alpha's floor: the
        // game's 0.947 is a little wider. By the two lobes and the sun's own disc: 0.7 of the peak, 1.4 times the area.)
        expect(game.peak<0.9*clear.peak && game.wide>1.1*clear.wide, "at the game's smoothness the sun's glint is softer and wider than a mirror's");
        expect(rough.peak<0.05*clear.peak && rough.wide>20.0*clear.wide, "rough water shows the sun as a wide sheen");
        expect(clear.burnt<12.0 && game.burnt<12.0 && rough.burnt<12.0, "no sample of the sun's glint is far over its pixel's mean");
        sea.displacement=glintWaves; sea.normals=glintNormals;
        for(int k=0;k<4;++k) sea.scattering[k]=glintScatter[k];
        sea.foamColor[3]=glintFar; sea.flags=TGPT_WATER_SMOOTHNESS; sea.foam[1]=foamIntensity;
        setExposure(1.f);
    }
    // 9. The water's pieces in groups (PT-99, docs/agents/waterblas.md; scene.hpp WaterGroup). Until 2026-10-12 every piece
    // of the surface had a bottom level, a refit and an instance of its own; now some dozens of pieces share one, and a
    // hit names its piece by the instance's ID and the geometry's index. THE SAME TRIANGLES, THE SAME RECORDS: a walk over
    // this sea (out 96 m, back at twice the pace, a stand of 140 frames so that the pieces of the walk are dropped and
    // their groups made anew, out again), every frame taken three times with the same random numbers: in groups,
    // ungrouped (the tracer's own switch for this test, TGPT_TEST_WATER_UNGROUPED: a group a piece, the calls of before),
    // and in groups again. A hit that read another piece's record would put another wave's normal and motion there.
    // Each frame's picture is hashed (every bit of every channel), every 4th is kept and compared channel by channel.
    // The hashes are printed: a run of this part with a DLL from before the groups prints what the old code traced.
    if(seaRuns("groups")) {
        const uint32_t walkFrames=212;
        // (TGPT_TEST_SEA_WALK_PACE=<metres a frame>: another pace than 3 m a frame, which is far faster than the game's
        // walk (0.2 to 0.8 m a frame); for what the pools hold at a pace like the game's, and for the walks' hashes
        // against a DLL from before the groups. AT ANOTHER PACE THE THREE WALKS ARE NOT HELD TO ONE ANOTHER: at 0.5 m a
        // frame, where a walk ends 8 m from where the next begins, they differ from one another in 15 frames (some
        // dozens of channels, by 0.15 at most), and main's DLL of 2026-10-12 08:10, which has no groups, gives the same
        // three hashes as the groups' DLL: something of an earlier walk reaches a later one when they come that near,
        // by the old code as by this. At 3 m a frame (48 m apart) nothing does.)
        float pace=3.f;
        { char text[32]{}; if(GetEnvironmentVariableA("TGPT_TEST_SEA_WALK_PACE", text, sizeof(text))>0 && std::atof(text)>0.0) pace=float(std::atof(text)); }
        auto walkCamera=[&](uint32_t i) {
            const float along=pace/3.f*(i<32 ? 3.f*float(i) : i<48 ? 93.f-6.2f*float(i-31) : i<188 ? 0.f : i<204 ? 3.f*float(i-187) : 48.f);
            const V3 eye{-8.f+std::max(along, 0.f), 3.2f, 0.35f*std::max(along, 0.f)};
            return make_camera(eye, {eye.x+30.f, eye.y-7.2f, eye.z+6.f}, 58.7f, float(W)/float(H));
        };
        auto hash_of=[](const std::vector<float>& px) {
            uint64_t h=1469598103934665603ull;
            const uint8_t* bytes=reinterpret_cast<const uint8_t*>(px.data());
            for(size_t b=0;b<px.size()*sizeof(float);++b) { h^=bytes[b]; h*=1099511628211ull; }
            return h;
        };
        struct Walk { std::vector<uint64_t> hashes; std::vector<std::vector<float>> kept; uint64_t all=1469598103934665603ull; double gpuMs=0; };
        const fs::path groupsLog=root/"TGPathTracing"/"TGPathTracing.Native.log";
        auto water_lines=[&]() {
            std::vector<std::string> found; std::ifstream log(groupsLog); std::string line;
            while(std::getline(log, line)) if(line.find("The water's surface:")!=std::string::npos) found.push_back(line);
            return found;
        };
        auto after=[](const std::string& line, const char* phrase, bool before) {
            // The number after a phrase (before: the number that ends right before it); -1 where the phrase is not there.
            const size_t at=line.find(phrase);
            if(at==std::string::npos) return -1.0;
            if(!before) return std::atof(line.c_str()+at+std::strlen(phrase));
            size_t from=at; while(from>0 && (std::isdigit(static_cast<unsigned char>(line[from-1])) || line[from-1]=='.')) --from;
            return std::atof(line.substr(from, at-from).c_str());
        };
        char cutsBefore[16]{}; const bool hadCuts=GetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", cutsBefore, sizeof(cutsBefore))>0;
        SetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", "0"); // (no cut off; set at all, the water's line is in the log every frame that made or built something)
        struct Counts { double pieces=0, groups=0, groupsMost=0, built=0, merges=0, collapsed=0, dropped=0, lines=0; };
        auto walk=[&](const char* name, bool ungrouped, Counts& counts) {
            Walk w; w.hashes.reserve(walkFrames);
            SetEnvironmentVariableA("TGPT_TEST_WATER_UNGROUPED", ungrouped ? "1" : nullptr);
            expect(SetWaterCaustics(&sea, 1)==0, "sea groups water");
            // Before every walk: 32 frames at its first place that are not judged, the first of them after a cut. What
            // comes once has come by then (the sea's mesh is read back from the tracer's own copy over its first frames),
            // and every walk starts from the same frames.
            {
                const Camera start=walkCamera(0);
                for(uint32_t i=0;i<32;++i) run(TGPT_VIEW_NONE, start, start, i ? 96000+i : 0, asInGame);
            }
            const size_t linesBefore=water_lines().size();
            for(uint32_t i=0;i<walkFrames;++i) {
                const Camera now=walkCamera(i), before=walkCamera(i ? i-1 : 0);
                run(TGPT_VIEW_NONE, now, before, 97000+i, asInGame);
                const auto frame=readback();
                const uint64_t h=hash_of(frame);
                w.hashes.push_back(h); w.all=(w.all^h)*1099511628211ull;
                if((i&3u)==3u) w.kept.push_back(frame);
                ReadStatus(&status); w.gpuMs+=double(status.gpuMs)/double(walkFrames);
            }
            const auto lines=water_lines();
            for(size_t l=linesBefore;l<lines.size();++l) {
                const std::string& line=lines[l];
                counts.lines+=1;
                counts.pieces=std::max(counts.pieces, after(line, "The water's surface: ", false));
                counts.groupsMost=std::max(counts.groupsMost, after(line, " pieces in ", false));
                counts.groups=after(line, " pieces in ", false);
                counts.built+=std::max(after(line, " built this frame (", true), 0.0);
                counts.merges+=std::max(after(line, " put together of ", true), 0.0);
                counts.collapsed+=std::max(after(line, " pieces collapsed this frame", true), 0.0);
                counts.dropped=std::max(counts.dropped, after(line, " geometries of dropped pieces", true));
            }
            std::printf("      SEA GROUPS %s: all %u frames' hash %016llx; frames 0, 31, 47, 100, 187, 211: %016llx %016llx %016llx %016llx %016llx %016llx; GPU %.2f ms a frame; "
                        "the log's water lines of the walk: %.0f, at most %.0f pieces in at most %.0f groups (%.0f at the end), %.0f groups built in all, %.0f of them put together "
                        "of several, %.0f pieces collapsed in all, at most %.0f geometries of dropped pieces\n",
                        name, walkFrames, (unsigned long long)w.all, (unsigned long long)w.hashes[0], (unsigned long long)w.hashes[31], (unsigned long long)w.hashes[47],
                        (unsigned long long)w.hashes[100], (unsigned long long)w.hashes[187], (unsigned long long)w.hashes[211], w.gpuMs,
                        counts.lines, counts.pieces, counts.groupsMost, counts.groups, counts.built, counts.merges, counts.collapsed, counts.dropped);
            return w;
        };
        // A hit reads its material at its instance's base plus its geometry's number, and the water's base is the last of
        // the frame's list: a piece that is not its group's first reads past the water's one entry of before. So the
        // list holds the water's material once for every piece a group can have (scene.cpp waterMaterialBase). To see
        // it: every material this scene does not use (0 to 11; the sand is 12) is clear glass for this part, which a
        // light's ray passes. A list one entry long would hand the water's later pieces whatever lies behind it (a
        // fresh buffer: 0), and the sun would reach the bed through them unstopped.
        {
            TGPT_Material trap=sand; trap.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS; trap.alphaCutoff=0.f;
            trap.baseColor[0]=trap.baseColor[1]=trap.baseColor[2]=1.f;
            const std::vector<TGPT_Material> traps(12, trap);
            expect(SetMaterials(0, traps.data(), uint32_t(traps.size()))==0, "sea groups: the unused materials are glass");
        }
        // The sea is two triangles two kilometres long: each of its pieces is ONE cut of one base triangle. A water mesh
        // of many small triangles (a river, a lake) is cut otherwise: a piece is a RANGE of base triangles (8, 64, 512
        // of them) cut a few times, and a group then holds pieces of several base triangles each, one after another in
        // its block. So a second water of that kind for this part: the test's mound of 512 triangles (24 m by 12 m, 1.5 m
        // a triangle) as a sheet of the same slot's water a metre over the sea, which the walk passes 2 m above.
        {
            std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
            for(int k=0;k<3;++k) {
                const M4 matrix=k==0 ? beach : k==1 ? translate_scale({0, 0, 0}, {span, 1, span}) : translate_scale({42.f, 1.f, 34.f}, {1, 1, 1});
                TGPT_Instance in{};
                for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
                in.mesh=k==2 ? terrainHandle : quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k ? TGPT_INSTANCE_WATER : 0u;
                ids.push_back(12); list.push_back(in);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "sea groups scene: the sea and a sheet of water of 512 small triangles");
            objects.clear(); objects.push_back({0, beach, {4}, false, false});
        }
        Counts groupedCounts, ungroupedCounts, againCounts;
        const Walk grouped=walk("in groups", false, groupedCounts);
        const Walk ungrouped=walk("ungrouped (a group a piece, as before)", true, ungroupedCounts);
        const Walk again=walk("in groups again", false, againCounts);
        SetEnvironmentVariableA("TGPT_TEST_WATER_UNGROUPED", nullptr);
        SetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", hadCuts ? cutsBefore : nullptr);
        struct Apart { uint32_t frames=0, first=0xFFFFFFFFu, keptFrames=0; size_t channels=0, mostChannels=0; double most=0, sum=0; };
        auto apart=[&](const Walk& a, const Walk& b) {
            Apart d;
            for(uint32_t i=0;i<walkFrames;++i) if(a.hashes[i]!=b.hashes[i]) { ++d.frames; d.first=std::min(d.first, i); }
            for(size_t k=0;k<a.kept.size() && k<b.kept.size();++k) {
                size_t channels=0;
                for(size_t p=0;p<a.kept[k].size();++p) {
                    // (Bit for bit: two NaNs of the same bits are the same picture.)
                    if(std::memcmp(&a.kept[k][p], &b.kept[k][p], sizeof(float))==0) continue;
                    ++channels;
                    const double apartBy=std::fabs(double(a.kept[k][p])-double(b.kept[k][p]));
                    if(std::isfinite(apartBy)) { d.most=std::max(d.most, apartBy); d.sum+=apartBy; } else d.most=1e30;
                }
                if(channels) ++d.keptFrames;
                d.channels+=channels; d.mostChannels=std::max(d.mostChannels, channels);
            }
            return d;
        };
        const Apart byGroups=apart(grouped, ungrouped), byRun=apart(grouped, again);
        const double keptChannels=double(grouped.kept.size())*double(size_t(W)*H*3);
        auto say=[&](const char* what, const Apart& d) {
            std::printf("      SEA GROUPS %s: %u of %u frames' hashes differ (the first: frame %d); of the %zu frames kept, %u differ, in %zu of %.0f channels (%.6f %%; "
                        "at most %zu in one frame), by at most %.6g (mean of those %.6g)\n", what, d.frames, walkFrames, d.frames ? int(d.first) : -1, grouped.kept.size(),
                        d.keptFrames, d.channels, keptChannels, 100.0*double(d.channels)/keptChannels, d.mostChannels, d.most, d.channels ? d.sum/double(d.channels) : 0.0);
        };
        say("in groups against ungrouped", byGroups);
        say("in groups, the same walk twice", byRun);
        // The record's self-check (TGPT_TEST_RECORD_CHECK: what is kept of the groups from frame to frame, their blocks in
        // the pools, the frame's material list; scene_water.cpp water_check) ran every frame of the three walks.
        if(GetEnvironmentVariableA("TGPT_TEST_RECORD_CHECK", nullptr, 0)>0) {
            size_t agreed=0, differed=0; std::string first;
            std::ifstream log(groupsLog); std::string line;
            while(std::getline(log, line)) {
                if(line.find("PT record check: the water's groups agree")!=std::string::npos) ++agreed;
                if(line.find("PT record check: THE WATER'S")!=std::string::npos) { if(!differed) first=line; ++differed; }
            }
            std::printf("      SEA GROUPS the record's self-check: \"the water's groups agree\" %zu time(s), \"THE WATER'S ... DIFFER\" %zu time(s)%s%s\n", agreed, differed,
                        differed ? ": " : "", differed ? first.substr(0, 600).c_str() : "");
            expect(agreed>0 && differed==0, "the record's self-check found the water's groups, their blocks and the material list in order at every frame of the walks");
        } else std::printf("      SEA GROUPS the record's self-check was not asked for (TGPT_TEST_RECORD_CHECK)\n");
        size_t lit=0; for(size_t p=0;p<grouped.kept[0].size();p+=3) lit+=grouped.kept[0][p+1]>0.001f;
        expect(lit>size_t(W)*H/2, "the walk's pictures show the sea and the shore");
        if(pace==3.f) {
            expect(byRun.frames==0, "the same walk in groups twice is the same picture, bit for bit");
            expect(byGroups.frames==0, "the water's pieces in groups trace the picture they traced one bottom level a piece, bit for bit");
        } else std::printf("      SEA GROUPS at %.2f m a frame the three walks are not held to one another (see the part's first lines): the hashes above against a DLL's from "
                           "before the groups are what to read\n", double(pace));
        expect(ungroupedCounts.groupsMost>=ungroupedCounts.pieces && ungroupedCounts.pieces>100, "ungrouped, every piece is a group of its own (some hundreds of them)");
        expect(groupedCounts.groupsMost>0 && groupedCounts.groupsMost*4<=groupedCounts.pieces, "in groups, the pieces are traced in a quarter as many structures at most");
        expect(groupedCounts.merges>0 && groupedCounts.collapsed>0 && groupedCounts.dropped>0,
               "the walk made the groups do everything they do: groups put together, pieces collapsed, dropped pieces' geometries");
    }
    ReadStatus(&status);
    expect(status.framesFailed==0, "sea frames complete without GPU errors");
    std::printf("%s sea checks\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
