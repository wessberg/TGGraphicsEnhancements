// SPDX-License-Identifier: MIT
// What the caustics land on what lies under the water (PT-81, docs/agents/water-9.md; TGPT_TEST_WATER_SURFACE=1 with
// TGPT_TEST_WATER_LANDED=1, at 640 x 360; it runs alone and ends the test). Included at the top of
// water_surface_gpu.inl's block. A worker's own small check: it prints numbers and judges little.
//
// 1. Small things under still water, seen from straight above: a box of 10 cm, a bar 2 cm thick lying on the bed and a
//    bar 1 cm thick 3 cm over it (a crab's body and legs), 40 cm of water, the sun 57 degrees up. The same picture with the
//    Caustics row off (a straight sun: the reference), on, on with the trial that lands the light of triangles whose
//    corners met different surfaces (TGPT_OPTION_CAUSTICS_SPLIT), and with the trial that keeps the light's direction too
//    (TGPT_OPTION_CAUSTICS_LIT). Each thing's light and the bed's beside it against the reference: still water lets
//    through 1 less its Fresnel share, 0.979 at this sun, and bends the light without spreading it.
// 2. Ripples over a flat bed 1 m down: waves of 1 m, 9 mm high. The native log's status line says the waves' slopes, the
//    depth and how much the landed light goes up and down (pt_caustics.hlsl's sums); the same three numbers are worked out
//    here on the CPU from the same waves, ray by ray, for the log's to be held against.
// Each state runs 23 s at least, and its ticks are printed: a status line of the native log (every 10 s, the tick count
// at its start) belongs to a state when it lies 10 s or more after the state's first tick and before its last.
if(waterLanded) {
    ownRays=true; sunOn=true; skyIntensity=1e-6f; reflectionBoost=0; tracedGain=1; rayDistance=500;
    setExposure(1.f);
    SetLights(nullptr, 0);
    const double pi=3.141592653589793;
    // For looking closer: TGPT_TEST_LANDED_BED, the bed's albedo (0.4); TGPT_TEST_LANDED_SECONDS, how long a state runs
    // at least (23: a whole status line of the native log; less when only the pictures are wanted); TGPT_TEST_LANDED_PART,
    // one part alone ("still" or "ripples").
    auto landedNumber=[&](const char* name, double otherwise) { char v[32]{}; return GetEnvironmentVariableA(name, v, sizeof(v))>0 ? std::atof(v) : otherwise; };
    const float bedAlbedo=float(landedNumber("TGPT_TEST_LANDED_BED", 0.4));
    const uint64_t stateTicks=uint64_t(1000.0*landedNumber("TGPT_TEST_LANDED_SECONDS", 23.0));
    char landedPart[32]{}; GetEnvironmentVariableA("TGPT_TEST_LANDED_PART", landedPart, sizeof(landedPart));
    auto landedRuns=[&](const char* part) { return !landedPart[0] || !std::strcmp(landedPart, part); };
    auto plain=[&](float r, float g, float b, float smoothness) {
        TGPT_Material m{}; m.baseColor[0]=r; m.baseColor[1]=g; m.baseColor[2]=b; m.baseColor[3]=1;
        m.albedoTile=m.emissiveTile=m.maskTile=m.alphaTile=-1; m.flags=TGPT_MATERIAL_DOUBLE_SIDED;
        m.colorTexture=m.normalTexture=m.maskTexture=-1; m.smoothness=smoothness;
        return m;
    };
    // 12 the bed (grey), 13 the box (red), 14 the bars (green), 15 a glossy box (blue) for the highlight.
    TGPT_Material landedMaterials[4]={plain(bedAlbedo, bedAlbedo, bedAlbedo, 0.f), plain(0.6f, 0.05f, 0.05f, 0.f), plain(0.05f, 0.6f, 0.05f, 0.f), plain(0.05f, 0.05f, 0.6f, 0.85f)};
    expect(SetMaterials(12, landedMaterials, 4)==0, "landed test materials");
    constexpr UINT landedSize=64;
    const double landedPatch=4.0, waveCount=4.0, waveHeight=0.0092, waveNumber=2*pi*waveCount/landedPatch;
    std::vector<float> landedMove(size_t(landedSize)*landedSize*4, 0.f), landedExtra(landedMove.size(), 0.f);
    for(UINT j=0;j<landedSize;++j) for(UINT i=0;i<landedSize;++i) {
        const double ax=2*pi*waveCount*(i+0.5)/landedSize, az=2*pi*waveCount*(j+0.5)/landedSize;
        const size_t at=(size_t(j)*landedSize+i)*4;
        landedMove[at]=float(waveHeight*(std::cos(ax)+std::cos(az)));
        landedExtra[at]=float(-waveHeight*waveNumber*std::sin(ax)); landedExtra[at+1]=float(-waveHeight*waveNumber*std::sin(az));
        landedExtra[at+2]=landedExtra[at+3]=4.f; // HDRP's Jacobian of water at rest: no sideways move here
    }
    D3D11_TEXTURE2D_DESC landedDesc{}; landedDesc.Width=landedDesc.Height=landedSize; landedDesc.MipLevels=landedDesc.ArraySize=1;
    landedDesc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; landedDesc.SampleDesc.Count=1; landedDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA landedMoveData{landedMove.data(), landedSize*16, 0}, landedExtraData{landedExtra.data(), landedSize*16, 0};
    ComPtr<ID3D11Texture2D> landedMoveTexture, landedExtraTexture;
    expect(SUCCEEDED(device->CreateTexture2D(&landedDesc, &landedMoveData, &landedMoveTexture)) && SUCCEEDED(device->CreateTexture2D(&landedDesc, &landedExtraData, &landedExtraTexture)), "landed test wave arrays");
    TGPT_WaterCaustics pond{}; pond.size=sizeof(pond); pond.abi=TGPT_ABI;
    pond.worldToWater[0]=pond.worldToWater[5]=pond.worldToWater[10]=1;
    pond.bounds[0]=pond.bounds[1]=-32; pond.bounds[2]=pond.bounds[3]=32;
    const M4 landedBed=translate_scale({0, 0, 0}, {64, 1, 64});
    auto landedScene=[&](float level, bool things, bool ripples) {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        auto add=[&](int mesh, const M4& matrix, uint32_t material, uint32_t flags) {
            TGPT_Instance in{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
            in.mesh=mesh; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=flags;
            ids.push_back(material); if(mesh==cubeHandle) ids.push_back(material);
            list.push_back(in);
        };
        add(quadHandle, landedBed, 12, 0);
        add(quadHandle, translate_scale({0, level, 0}, {64, 1, 64}), 12, TGPT_INSTANCE_WATER);
        if(things) {
            add(cubeHandle, translate_scale({0.f, 0.05f, 0.f}, {0.10f, 0.10f, 0.10f}), 13, 0);      // the box: 10 cm
            add(cubeHandle, translate_scale({-0.25f, 0.01f, 0.f}, {0.02f, 0.02f, 0.20f}), 14, 0);   // a bar 2 cm thick on the bed
            add(cubeHandle, translate_scale({0.25f, 0.035f, 0.f}, {0.01f, 0.01f, 0.20f}), 14, 0);   // a bar 1 cm thick, 3 cm over the bed
            add(cubeHandle, translate_scale({0.f, 0.05f, 0.30f}, {0.10f, 0.10f, 0.10f}), 15, 0);    // a glossy box, for the highlight
        }
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "landed test scene");
        pond.top=pond.bottom=level;
        pond.displacement=ripples ? uint64_t(uintptr_t(landedMoveTexture.Get())) : 0; pond.normals=ripples ? uint64_t(uintptr_t(landedExtraTexture.Get())) : 0;
        pond.bandCount=ripples ? 1u : 0u; pond.bands[0][0]=float(1.0/landedPatch); pond.bands[0][3]=1; pond.fades[0][1]=1;
        pond.maxDisplacement=ripples ? float(2*waveHeight) : 0.f;
        expect(SetWaterCaustics(&pond, 1)==0, "landed test water");
        objects.clear(); objects.push_back({0, landedBed, {4}, false, false});
    };
    uint32_t landedSerial=120000;
    auto state=[&](const char* name, const Camera& view, uint32_t options) {
        cut=true; run(TGPT_VIEW_NONE, view, view, landedSerial++, options); cut=false;
        for(int i=0;i<24;++i) run(TGPT_VIEW_NONE, view, view, landedSerial++, options);
        const uint64_t begin=GetTickCount64();
        std::vector<float> mean(size_t(W)*H*3, 0.f);
        uint32_t frames=0; const uint32_t kept=96;
        while(frames<kept || GetTickCount64()-begin<stateTicks) {
            run(TGPT_VIEW_NONE, view, view, landedSerial++, options);
            if(frames<kept) { const auto frame=readback(); for(size_t p=0;p<mean.size();++p) mean[p]+=frame[p]/float(kept); }
            ++frames;
        }
        ReadStatus(&status);
        std::printf("      LANDED %s: ticks %llu to %llu, %u frames, GPU %.2f ms a frame, failed frames %llu\n", name, (unsigned long long)begin,
                    (unsigned long long)GetTickCount64(), frames, status.gpuMs, (unsigned long long)status.framesFailed);
        char file[96]; std::snprintf(file, sizeof(file), "water-landed-%s.png", name); save(mean, file, true);
        return mean;
    };
    auto lum=[&](const std::vector<float>& px, size_t p) { return 0.2126*double(px[p*3])+0.7152*double(px[p*3+1])+0.0722*double(px[p*3+2]); };
    const uint32_t raw=TGPT_OPTION_NO_DENOISER;

    // 1. Small things under still water.
    if(landedRuns("still")) {
        landedScene(0.4f, true, false);
        const Camera above=make_camera({0.f, 2.2f, -0.01f}, {0.f, 0.f, 0.f}, 24.f, float(W)/float(H));
        const auto straight=state("still-row-off", above, raw|TGPT_OPTION_NO_CAUSTICS);
        // What a pixel shows, by the reference's colours: 1 the box (red), 2 a bar (green), 3 the glossy box (blue), 4 the
        // bed in the sun, 5 the bed in a shadow; 0 anything else (an edge between two).
        std::vector<uint8_t> kind(size_t(W)*H, 0);
        std::vector<double> bedLights;
        for(size_t p=0;p<kind.size();++p) {
            const double r=straight[p*3], g=straight[p*3+1], b=straight[p*3+2], most=std::max(r, std::max(g, b));
            if(most<=0) { kind[p]=5; continue; }
            if(r>2.5*g && r>2.5*b) kind[p]=1;
            else if(g>2.5*r && g>2.5*b) kind[p]=2;
            else if(b>2.5*r && b>2.5*g) kind[p]=3;
            else if(std::fabs(r-g)<0.12*most && std::fabs(g-b)<0.12*most) { kind[p]=4; bedLights.push_back(lum(straight, p)); }
        }
        std::sort(bedLights.begin(), bedLights.end());
        const double bedMedian=bedLights.empty() ? 0.0 : bedLights[bedLights.size()/2];
        for(size_t p=0;p<kind.size();++p) if(kind[p]==4 && lum(straight, p)<0.5*bedMedian) kind[p]=5;
        // The bed beside a thing: within 3 cm of one of its pixels (the picture is 0.935 m high at the bed).
        const double perMetre=double(H)/0.935; const int beside=int(std::lround(0.03*perMetre));
        std::vector<uint8_t> nearThing(kind.size(), 0);
        for(int y=0;y<int(H);++y) for(int x=0;x<int(W);++x) {
            const uint8_t k=kind[size_t(y)*W+x];
            if(k<1 || k>3) continue;
            for(int dy=-3*beside;dy<=3*beside;++dy) for(int dx=-3*beside;dx<=3*beside;++dx) {
                const int qx=x+dx, qy=y+dy;
                if(qx<0 || qy<0 || qx>=int(W) || qy>=int(H)) continue;
                uint8_t& mark=nearThing[size_t(qy)*W+qx];
                const uint8_t ring=(std::abs(dx)<=beside && std::abs(dy)<=beside) ? 1 : 2;
                if(!mark || ring<mark) mark=ring;
            }
        }
        // The parts: 0 the box, 1 the bar 2 cm thick on the bed (the picture's left half), 2 the bar 1 cm thick over the bed
        // (its right half), 3 the glossy box, 4 the sunlit bed within 3 cm of a thing, 5 the bed within 3 cm of a thing
        // (sunlit or in its shadow), 6 the sunlit bed far from every thing (over 9 cm), 7 the whole picture.
        constexpr int partCount=8;
        const char* partNames[partCount]={"the box", "the 2 cm bar on the bed", "the 1 cm bar over the bed", "the glossy box", "the sunlit bed within 3 cm of a thing", "all the bed within 3 cm of a thing", "the open sunlit bed", "the whole picture"};
        auto parts=[&](const std::vector<float>& px, double* sums, double* peak) {
            for(int k=0;k<partCount;++k) sums[k]=0;
            *peak=0;
            for(size_t p=0;p<kind.size();++p) {
                const double l=lum(px, p); const uint8_t k=kind[p];
                sums[7]+=l;
                if(k==1) sums[0]+=l;
                if(k==2) sums[(p%W)<W/2 ? 1 : 2]+=l;
                if(k==3) { sums[3]+=l; *peak=std::max(*peak, l); }
                if((k==4 || k==5) && nearThing[p]==1) { sums[5]+=l; if(k==4) sums[4]+=l; }
                if(k==4 && !nearThing[p]) sums[6]+=l;
            }
        };
        double reference[partCount], referencePeak; parts(straight, reference, &referencePeak);
        size_t counts[6]={0, 0, 0, 0, 0, 0}; for(uint8_t k:kind) ++counts[k];
        std::printf("      LANDED still water, the picture's pixels: box %zu, bars %zu, glossy box %zu, sunlit bed %zu, bed in shadow %zu, other %zu; the bed's median light %.5f; 3 cm are %d pixels\n",
                    counts[1], counts[2], counts[3], counts[4], counts[5], counts[0], bedMedian, beside);
        const double n=1.3333, cosIn=double(sunDir.y), sinOut=std::sqrt(1-cosIn*cosIn)/n, cosOut=std::sqrt(1-sinOut*sinOut);
        const double rs=(cosIn-n*cosOut)/(cosIn+n*cosOut), rp=(n*cosIn-cosOut)/(n*cosIn+cosOut), through=1-0.5*(rs*rs+rp*rp);
        std::printf("      LANDED still water lets through %.4f of the sun's light at this sun (Fresnel): what every part should read against the row off\n", through);
        struct Variant { const char* name; uint32_t options; };
        const Variant variants[3]={{"still-row-on", raw}, {"still-split", raw|TGPT_OPTION_CAUSTICS_SPLIT}, {"still-split-lit", raw|TGPT_OPTION_CAUSTICS_SPLIT|TGPT_OPTION_CAUSTICS_LIT}};
        for(const Variant& v:variants) {
            const auto picture=state(v.name, above, v.options);
            double sums[partCount], peak; parts(picture, sums, &peak);
            // Against the row off, and (in brackets) that over the open bed's own ratio: 1 there is "lit as the open bed is".
            const double open=reference[6]>0 ? sums[6]/reference[6] : 0.0;
            std::printf("      LANDED %s against the row off:", v.name);
            for(int k=0;k<partCount;++k) {
                const double ratio=reference[k]>0 ? sums[k]/reference[k] : 0.0;
                std::printf(" %s x%.3f (%.3f)%s", partNames[k], ratio, open>0 ? ratio/open : 0.0, k<partCount-1 ? ";" : "");
            }
            std::printf("; the glossy box's brightest pixel %.4f (row off: %.4f)\n", peak, referencePeak);
        }
    }

    // 2. Ripples over a flat bed 1 m down: what the log should say, worked out here from the same waves ray by ray (the
    // pass's own sums: a triangle's light is its area across the sun, its landed area across its beam; the brightness's
    // spread over what the light landed on).
    if(landedRuns("ripples")) {
        const double depth=1.0;
        const int cells=400; const double period=2*pi/waveNumber, step=period/cells;
        const V3 sun=sunDir;
        struct Corner { double x[3], p[3]; };
        std::vector<Corner> corners(size_t(cells+1)*(cells+1));
        double slopeSquares=0, rayLengths=0;
        for(int j=0;j<=cells;++j) for(int i=0;i<=cells;++i) {
            const double x=i*step, z=j*step, hx=-waveHeight*waveNumber*std::sin(waveNumber*x), hz=-waveHeight*waveNumber*std::sin(waveNumber*z);
            const double length=std::sqrt(hx*hx+1+hz*hz), nx=-hx/length, ny=1/length, nz=-hz/length;
            const double cosI=nx*sun.x+ny*sun.y+nz*sun.z, eta=1/1.3333, k=eta*cosI-std::sqrt(1-eta*eta*(1-cosI*cosI));
            const double tx=-eta*sun.x+k*nx, ty=-eta*sun.y+k*ny, tz=-eta*sun.z+k*nz;
            Corner& c=corners[size_t(j)*(cells+1)+i];
            c.x[0]=x; c.x[1]=depth+waveHeight*(std::cos(waveNumber*x)+std::cos(waveNumber*z)); c.x[2]=z;
            const double reach=c.x[1]/-ty;
            c.p[0]=x+tx*reach; c.p[1]=0; c.p[2]=z+tz*reach;
            if(i<cells && j<cells) { slopeSquares+=hx*hx+hz*hz; rayLengths+=reach; }
        }
        double across=0, light=0, square=0;
        auto triangle=[&](const Corner& a, const Corner& b, const Corner& c) {
            double ub[3], uc[3], pb[3], pc[3], beam[3];
            for(int k=0;k<3;++k) { ub[k]=b.x[k]-a.x[k]; uc[k]=c.x[k]-a.x[k]; pb[k]=b.p[k]-a.p[k]; pc[k]=c.p[k]-a.p[k]; beam[k]=(a.p[k]-a.x[k])+(b.p[k]-b.x[k])+(c.p[k]-c.x[k]); }
            const double up[3]={uc[1]*ub[2]-uc[2]*ub[1], uc[2]*ub[0]-uc[0]*ub[2], uc[0]*ub[1]-uc[1]*ub[0]};
            const double share=std::max(0.0, 0.5*(up[0]*sun.x+up[1]*sun.y+up[2]*sun.z));
            const double nt[3]={pb[1]*pc[2]-pb[2]*pc[1], pb[2]*pc[0]-pb[0]*pc[2], pb[0]*pc[1]-pb[1]*pc[0]};
            const double beamLength=std::sqrt(beam[0]*beam[0]+beam[1]*beam[1]+beam[2]*beam[2]);
            const double area=0.5*std::fabs(nt[0]*beam[0]+nt[1]*beam[1]+nt[2]*beam[2])/beamLength;
            if(!(area>0)) return;
            across+=area; light+=share; square+=share*share/area;
        };
        for(int j=0;j<cells;++j) for(int i=0;i<cells;++i) {
            const Corner& a=corners[size_t(j)*(cells+1)+i], &b=corners[size_t(j)*(cells+1)+i+1], &c=corners[size_t(j+1)*(cells+1)+i+1], &d=corners[size_t(j+1)*(cells+1)+i];
            triangle(a, b, c); triangle(a, c, d);
        }
        const double mean=light/across, spread=std::sqrt(std::max(square/across/(mean*mean)-1, 0.0));
        std::printf("      LANDED ripples, worked out on the CPU: two crossing waves of %.2f m, each %.1f mm up and down (the surface moves %.1f mm at most), slopes %.3f (root mean square), the bed %.0f cm down along the ray, the landed light's ups and downs %.1f %%\n",
                    period, waveHeight*1000, waveHeight*2000, std::sqrt(slopeSquares/double(cells*cells)), 100*rayLengths/double(cells*cells), 100*spread);
        landedScene(float(depth), false, true);
        const Camera over=make_camera({0.f, 4.f, -0.01f}, {0.f, 0.f, 0.f}, 40.f, float(W)/float(H));
        const auto still=state("ripples-row-off", over, raw|TGPT_OPTION_NO_CAUSTICS);
        const auto rippled=state("ripples-row-on", over, raw);
        // The picture's own ups and downs (the bed seen through the waves, which bend the view a little): the row on over the
        // row off, pixel by pixel, each the mean of 5 x 5 pixels.
        double sum=0, squares=0, count=0;
        for(UINT y=20;y+20<H;y+=5) for(UINT x=20;x+20<W;x+=5) {
            double on=0, off=0;
            for(UINT dy=0;dy<5;++dy) for(UINT dx=0;dx<5;++dx) { const size_t p=size_t(y+dy)*W+x+dx; on+=lum(rippled, p); off+=lum(still, p); }
            if(!(off>0)) continue;
            const double ratio=on/off; sum+=ratio; squares+=ratio*ratio; count+=1;
        }
        const double ratioMean=count>0 ? sum/count : 0, ratioSpread=count>0 ? std::sqrt(std::max(squares/count-ratioMean*ratioMean, 0.0))/std::max(ratioMean, 1e-9) : 0;
        std::printf("      LANDED ripples, the picture: the row on over the row off x%.3f on average, its ups and downs %.1f %% (blocks of 5 x 5 pixels, %g of them)\n", ratioMean, 100*ratioSpread, count);
    }
    ReadStatus(&status);
    expect(status.framesFailed==0, "landed test frames complete without GPU errors");
    return failures ? 1 : 0;
}
