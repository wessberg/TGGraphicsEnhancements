// SPDX-License-Identifier: MIT
// PT-87 (docs/agents/memory-2.md): memory the tracer holds and does not use is given back, and the picture does not know.
// Included by pathtracing_memory1_checks.inl (its cases 5 and on); alone with TGPT_TEST_MEMORY2_ONLY=1.
//
// Each cut has a switch on the native side for this check alone (scene.hpp memory2_off: TGPT_TEST_MEMORY2_OFF=1 the still
// meshes' full-size builds in the one arena with everything else, 2 the plants' structures as built, 4 the water's as
// built and its pools' pages kept). A case runs the same frames with the cut, without it and with it again: the memory
// must differ, and every pixel of the two pictures must be the same (no more apart than the same run twice).
{
    const fs::path memory2Log=root/"TGPathTracing"/"TGPathTracing.Native.log";
    // The native log's last line with a marker: the arenas' and the pools' own figures are in no export.
    auto last_line=[&](const char* marker) {
        std::ifstream log(memory2Log); std::string line, found;
        while(std::getline(log, line)) if(line.find(marker)!=std::string::npos) found=line;
        return found;
    };
    // The number that follows a phrase of such a line (-1: the phrase is not in it).
    auto number_after=[](const std::string& line, const char* phrase) {
        const size_t at=line.find(phrase);
        return at==std::string::npos ? -1.0 : std::atof(line.c_str()+at+std::strlen(phrase));
    };
    auto cuts_off=[](uint32_t off) { SetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", std::to_string(off).c_str()); };
    const uint32_t still=quiet|TGPT_OPTION_NO_DENOISER;
    // What Windows counts for this process on the card (the count its budget is weighed against), MB, beside what the
    // tracer's own line sums: printed between the cases. The difference is what no figure of the line has (PT-87).
    ComPtr<IDXGIDevice> countDevice; ComPtr<IDXGIAdapter> countAdapter; ComPtr<IDXGIAdapter3> countAdapter3;
    const bool countKnown=SUCCEEDED(device.As(&countDevice)) && SUCCEEDED(countDevice->GetAdapter(&countAdapter)) && SUCCEEDED(countAdapter.As(&countAdapter3));
    auto windows_mb=[&]() { DXGI_QUERY_VIDEO_MEMORY_INFO m{}; if(countKnown) countAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &m); return double(m.CurrentUsage)/1048576.0; };
    auto windows_other_mb=[&]() { DXGI_QUERY_VIDEO_MEMORY_INFO m{}; if(countKnown) countAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &m); return double(m.CurrentUsage)/1048576.0; };
    auto counted=[&](const char* when) {
        const TGPT_Diagnostics d=memory(); const double w=windows_mb();
        std::printf("      Windows counts %.0f MB for this process %s (and %.0f MB of the PC's own memory beside it); the tracer's line sums %.0f MB, %.0f of it rings and "
                    "read-backs: %.0f MB of the count are in no figure of the line if the rings are the PC's memory, %.0f MB if they are the card's\n",
                    w, when, windows_other_mb(), mb(d.total), mb(d.transfer), w-mb(d.total-std::min(d.total, d.transfer)), w-mb(d.total));
        return w;
    };
    // (memory3) The tracer's start, step by step, as its native log has it: each step's move of Windows' count.
    // (The log is added to from run to run: this run's part starts at the last "the shared device is here".)
    auto start_lines=[&](size_t skip) {
        std::ifstream log(memory2Log); std::string line; std::vector<std::string> found;
        while(std::getline(log, line)) {
            if(line.find("the shared device is here")!=std::string::npos) found.clear();
            const size_t from=line.find("PT memory at start"), since=line.find("PT memory since the tracer's start");
            if(from!=std::string::npos || since!=std::string::npos) found.push_back(line.substr(std::min(from, since)));
        }
        for(size_t i=skip;i<found.size();++i) std::printf("      native log: %s\n", found[i].c_str());
        return found.size();
    };
    const size_t startLines=start_lines(0);
    expect(startLines>=10, "the native log has the tracer's start step by step in Windows' count");
    counted("before the cases");

    // (memory4: TGPT_TEST_MEMORY4_ONLY=1, with TGPT_TEST_MEMORY2_ONLY=1, runs case 11 alone: cases 5 to 10 are left out.)
    const bool memory4Only=GetEnvironmentVariableA("TGPT_TEST_MEMORY4_ONLY", nullptr, 0)>0;
    if(!memory4Only) {
    // 5. The still meshes' full-size builds have chunks of their own, which go back once their structures are compacted.
    // 48 bumpy sheets of 31,250 triangles (a structure of some 2.5 MB as built, a third of it compacted), registered
    // twelve at a time while frames run, as a world arrives: in ONE arena a compacted copy lands in whatever hole is free
    // among the next sheets' full-size builds, and the chunks stay whole around a few compacted pieces each.
    {
        auto sheet=[&](uint32_t seed) {
            const int n=125; std::vector<Vertex> v; v.reserve(size_t(n+1)*(n+1)); std::vector<uint16_t> at; at.reserve(size_t(n)*n*6);
            for(int j=0;j<=n;++j) for(int i=0;i<=n;++i) {
                const float x=float(i)/n-0.5f, z=float(j)/n-0.5f;
                v.push_back({x, 0.02f*std::sin(37.f*x+float(seed))*std::cos(29.f*z+0.7f*float(seed)), z, 0, 1, 0, float(i)/n*4, float(j)/n*4});
            }
            for(int j=0;j<n;++j) for(int i=0;i<n;++i) {
                const uint16_t a=uint16_t(j*(n+1)+i), b=uint16_t(a+1), c=uint16_t(a+n+1), d=uint16_t(c+1);
                at.insert(at.end(), {a, d, b, a, c, d}); // wound as the room's quad is
            }
            return make_mesh(v, {at});
        };
        const int kSheets=48;
        struct Sheets { std::vector<float> picture; TGPT_Diagnostics d{}; std::string arenas; uint64_t structures=0; uint32_t arrivalFrames=0; };
        uint64_t sheetKey=0x87200000ull;
        auto sheets_run=[&](uint32_t off) {
            cuts_off(off);
            Sheets s;
            std::vector<MeshBuffers> meshes; meshes.reserve(kSheets); std::vector<int> made;
            ReadStatus(&status); uint32_t ready=status.meshesReady;
            for(int wave=0;wave<4;++wave) {
                for(int k=0;k<kSheets/4;++k) { meshes.push_back(sheet(uint32_t(wave*(kSheets/4)+k))); made.push_back(register_mesh(meshes.back(), ++sheetKey)); }
                ready+=uint32_t(kSheets/4);
                for(int i=0;i<300;++i) {
                    run(TGPT_VIEW_NONE, cam, cam, 38000+(++s.arrivalFrames), still);
                    ReadStatus(&status); if(status.meshesReady>=ready) break;
                    context->Flush(); Sleep(5);
                }
            }
            expect(status.meshesReady>=ready, "the sheets were read back and built");
            // In the room: a field of 8 x 6 over the floor, each 0.9 m, where the camera's own rays find them.
            auto list=instances; auto listIndices=indices;
            for(int k=0;k<kSheets;++k) {
                const M4 world=translate_scale({-3.5f+float(k%8), 0.05f+0.01f*float(k/8), -3.f+float(k/8)}, {0.9f, 1.f, 0.9f});
                TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
                in.mesh=made[size_t(k)]; in.materialBase=uint32_t(listIndices.size()); in.mask=0xFF;
                listIndices.push_back(3u); list.push_back(in);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with the sheets in it");
            // 150 frames from the same numbers in every run: more than the 120 an emptied chunk of full-size builds stays.
            ownRays=true;
            for(uint32_t i=0;i<150;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 39000+i : 0, still);
            ownRays=false;
            s.picture=readback(); s.d=memory(); s.arenas=last_line("PT memory, the arenas");
            ReadStatus(&status); s.structures=status.accelerationBytes;
            // The sheets go again: the next run starts from the room alone.
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room alone again");
            for(int h:made) ReleaseMesh(h);
            for(uint32_t i=0;i<12;++i) { run(TGPT_VIEW_NONE, cam, cam, 39200+i, still); context->Flush(); }
            return s;
        };
        sheets_run(0); // not judged: the first run after another case starts from that case's state
        const Sheets apart=sheets_run(0), one=sheets_run(1), apartAgain=sheets_run(0);
        cuts_off(0);
        const double waiting=number_after(apart.arenas, "waiting to be compacted "), kept=number_after(apart.arenas, "the structures that stay "),
            keptUsed=number_after(apart.arenas, "in chunks, "), oneKept=number_after(one.arenas, "the structures that stay "), oneUsed=number_after(one.arenas, "in chunks, ");
        std::printf("      48 sheets of 31,250 triangles, arriving twelve at a time (%u, %u, %u frames): acceleration structures %.1f MB with the full-size builds in chunks "
                    "of their own, %.1f MB with one arena for everything (as before); the meshes' own structures %.1f and %.1f MB\n"
                    "      the arenas by the native log: apart, %.1f MB of chunks stay with %.1f in use and %.1f MB wait; one arena, %.1f MB of chunks with %.1f in use\n",
                    apart.arrivalFrames, one.arrivalFrames, apartAgain.arrivalFrames, mb(apart.d.acceleration), mb(one.d.acceleration), mb(apart.structures), mb(one.structures),
                    kept, keptUsed, waiting, oneKept, oneUsed);
        expect(!apart.arenas.empty() && waiting==0.0, "the full-size builds' chunks are given back once their structures are compacted");
        expect(apart.structures==one.structures, "the same structures either way (what the meshes' own count says)");
        expect(apart.d.acceleration<=one.d.acceleration, "and no more is held than with one arena for everything");
        // (A chunk is 16 MB and a compacted sheet about 1 MB: under a sheet's size is lost at each chunk's end, and the last chunk is as full as it is.)
        expect(kept>=0.0 && kept-keptUsed<=16.0+kept/8.0, "what stays is packed: a chunk and an eighth of free room at most in the chunks that stay");
        const Differ sheetsCut=differ(apart.picture, one.picture), sheetsSame=differ(apart.picture, apartAgain.picture);
        size_t seen=0; for(size_t i=0;i<apart.picture.size();i+=3) seen+=apart.picture[i+1]>0.02f;
        std::printf("      the room with the sheets, chunks apart against one arena: the largest difference of any pixel's channel %.3g (mean %.3g); the same run twice: %.3g "
                    "(mean %.3g); lit pixels %zu\n", sheetsCut.most, sheetsCut.mean, sheetsSame.most, sheetsSame.mean, seen);
        expect(seen>size_t(W)*H/20, "the picture shows the room");
        expect(sheetsCut.most<=sheetsSame.most*2.0+1e-6, "the picture is the same wherever the structures lie");
        save(apart.picture, "87-sheets-chunks-apart.png", true); save(one.picture, "87-sheets-one-arena.png", true);
    }
    counted("after the sheets");

    // 6. The plants' structures compacted. A field of grass in the wind through the room and around it (141 x 141 tufts of
    // three crossed cards a quarter metre apart: 477,144 triangles, a group a 16 m cell of the world), the wind's time
    // moving on every frame: each group's structure is built, compacted some frames later and refitted in the compacted
    // copy from then on. Without the cut it stays as built. 60 frames a run; the last one and the sum of the last ten are
    // compared.
    {
        expect(SetPlantWind!=nullptr && SetWindNoise!=nullptr, "the plant wind exports");
        auto turn_y=[&](float a) { M4 r=identity(); r.m[0][0]=std::cos(a); r.m[0][2]=std::sin(a); r.m[2][0]=-std::sin(a); r.m[2][2]=std::cos(a); return r; };
        std::vector<Vertex> tuft; std::vector<uint16_t> tuftAt;
        for(int c=0;c<3;++c) {
            const float a=0.2f+float(c)*pi/3.f, dx=std::cos(a), dz=std::sin(a);
            const uint16_t base=uint16_t(tuft.size());
            for(int r=0;r<=4;++r) {
                const float y=float(r)/4.f;
                tuft.push_back({-0.15f*dx, y, -0.15f*dz, -dz, 0, dx, 0, y});
                tuft.push_back({0.15f*dx, y, 0.15f*dz, -dz, 0, dx, 1, y});
            }
            for(int r=0;r<4;++r) { const uint16_t q=uint16_t(base+r*2); tuftAt.insert(tuftAt.end(), {q, uint16_t(q+2), uint16_t(q+1), uint16_t(q+1), uint16_t(q+2), uint16_t(q+3)}); }
        }
        MeshBuffers tuftMesh=make_mesh(tuft, {tuftAt});
        ReadStatus(&status); const uint32_t readyBefore=status.meshesReady;
        const int tuftHandle=register_mesh(tuftMesh, 0x87210001ull);
        expect(tuftHandle>0, "a tuft of grass");
        for(uint32_t i=0;i<300 && status.meshesReady<=readyBefore;++i) { run(TGPT_VIEW_NONE, cam, cam, 40500+i, still); context->Flush(); Sleep(5); ReadStatus(&status); }
        expect(status.meshesReady>readyBefore, "the tuft was read back and built");
        std::vector<uint8_t> gusts(64*64);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) gusts[size_t(y)*64+size_t(x)]=uint8_t(std::lround(255.f*(0.6f+0.4f*std::sin(float(x)*0.31f+1.3f)*std::cos(float(y)*0.23f+0.7f))));
        expect(SetWindNoise(7, 64, 64, 0, gusts.data())==0, "the wind's noise map");
        auto grass=materials[0];
        expect(SetMaterials(50, &grass, 1)==0, "the grass's material");
        TGPT_PlantWind grassWind{}; grassWind.material=50; grassWind.kind=TGPT_PLANT_GRASS; grassWind.noise=7; grassWind.strength=80.f;
        const V3 lookAlong{-cam.view.m[2][0], -cam.view.m[2][1], -cam.view.m[2][2]};
        struct Plants { std::vector<float> picture; std::vector<double> sum; TGPT_Diagnostics d{}; double built=0, compacted=0; };
        const char* plantsLine="The plants' structures compacted";
        auto plants_run=[&](uint32_t off) {
            cuts_off(off);
            Plants p; p.sum.assign(size_t(W)*H*3, 0.0);
            const std::string before=last_line(plantsLine);
            auto list=instances; auto listIndices=indices;
            const int side=141;
            for(int z=0;z<side;++z) for(int x=0;x<side;++x) {
                const M4 world=mul(translate_scale({-17.5f+float(x)*0.25f, 0, -20.f+float(z)*0.25f}, {0.5f, 0.5f, 0.5f}), turn_y(float((x*37+z*91)%360)*pi/180.f));
                TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=world.m[r][k];
                q.mesh=tuftHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF;
                listIndices.push_back(50); list.push_back(q);
            }
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room in a field of grass");
            ownRays=true;
            for(uint32_t i=0;i<60;++i) {
                TGPT_PlantWindFrame frame{}; frame.time=3.f+float(i)/60.f; frame.speed=30.f; frame.direction=40.f;
                frame.forward[0]=lookAlong.x; frame.forward[1]=lookAlong.y; frame.forward[2]=lookAlong.z;
                SetPlantWind(&frame, &grassWind, 1u);
                run(TGPT_VIEW_NONE, cam, cam, i ? 40000+i : 0, still);
                if(i>=50) { const auto frameNow=readback(); for(size_t j=0;j<frameNow.size();++j) p.sum[j]+=double(frameNow[j]); }
            }
            ownRays=false;
            p.picture=readback(); p.d=memory();
            const std::string after=last_line(plantsLine);
            p.built=std::max(0.0, number_after(after, "built="))-std::max(0.0, number_after(before, "built="));
            p.compacted=std::max(0.0, number_after(after, "compacted="))-std::max(0.0, number_after(before, "compacted="));
            // The field goes again (its groups with the wind): the next run makes them anew.
            TGPT_PlantWindFrame none{}; SetPlantWind(&none, nullptr, 0);
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room alone again");
            for(uint32_t i=0;i<8;++i) { run(TGPT_VIEW_NONE, cam, cam, 40100+i, still); context->Flush(); }
            return p;
        };
        plants_run(0); // not judged
        const Plants packed=plants_run(0), asBuilt=plants_run(2), packedAgain=plants_run(0);
        cuts_off(0);
        std::printf("      a field of 19,881 tufts of grass in the wind (477,144 triangles): the plants' posed copies and structures %.1f MB with the structures compacted, "
                    "%.1f MB as built (as before); by the native log %.1f MB of structures became %.1f MB (%.0f %% of their size as built)\n",
                    mb(packed.d.plants), mb(asBuilt.d.plants), packed.built/1048576.0, packed.compacted/1048576.0, packed.built>0 ? 100.0*packed.compacted/packed.built : 0.0);
        expect(packed.built>0 && packed.compacted>0 && packed.compacted<packed.built, "every group's structure was compacted to less than it was built at");
        expect(asBuilt.built==0, "with the test's switch the structures stay as built (the comparison's other side)");
        expect(packed.d.plants<asBuilt.d.plants && double(asBuilt.d.plants-packed.d.plants)>=0.9*(packed.built-packed.compacted),
               "the plants hold that much less (what the device counts)");
        const Differ plantsCut=differ(packed.picture, asBuilt.picture), plantsSame=differ(packed.picture, packedAgain.picture);
        auto differ_sums2=[](const std::vector<double>& a, const std::vector<double>& b) {
            Differ d; double sum=0;
            for(size_t i=0;i<a.size();++i) { const double x=std::fabs(a[i]-b[i]); sum+=x; d.most=std::max(d.most, std::isfinite(x) ? x : 1e30); }
            d.mean=a.empty() ? 1e30 : sum/double(a.size()); return d;
        };
        const Differ plantsSum=differ_sums2(packed.sum, asBuilt.sum), plantsSumSame=differ_sums2(packed.sum, packedAgain.sum);
        size_t grown=0; for(size_t i=0;i<packed.picture.size();i+=3) grown+=packed.picture[i+1]>0.02f;
        std::printf("      the field's 60th frame, compacted against as built: the largest difference of any pixel's channel %.3g (mean %.3g); the same run twice: %.3g (mean %.3g); "
                    "the sum of its last ten frames: %.3g (mean %.3g) against %.3g (mean %.3g); lit pixels %zu\n",
                    plantsCut.most, plantsCut.mean, plantsSame.most, plantsSame.mean, plantsSum.most, plantsSum.mean, plantsSumSame.most, plantsSumSame.mean, grown);
        expect(grown>size_t(W)*H/20, "the picture shows the field");
        expect(plantsCut.most<=plantsSame.most*2.0+1e-6 && plantsSum.most<=plantsSumSame.most*2.0+1e-5,
               "grass in the wind looks the same from compacted structures as from structures as built, frame after frame");
        save(packed.picture, "87-plants-compacted.png", true); save(asBuilt.picture, "87-plants-as-built.png", true);
        ReleaseMesh(tuftHandle);
        for(uint32_t i=0;i<8;++i) { run(TGPT_VIEW_NONE, cam, cam, 40200+i, still); context->Flush(); }
    }
    counted("after the field of grass (the plants' pass ran for the first time)");

    // 7. The water's pieces: blocks that fit, and pages given back. NOT RUN YET (memory3, 2026-10-10: the worker who wrote
    // the cut was stopped before this case ran once; the cut is off in a build without the test's switch, scene.hpp
    // kMemory2Waiting). It runs only with TGPT_TEST_MEMORY2_WATER=1, so that the check passes without it.
    // A lake of HDRP's water 400 m across, through the room at knee height: some hundreds of pieces, cut finer the nearer
    // the camera (their vertices and their structures in blocks of two pools). With the cut a block is an eighth of a
    // power of two above what a piece needs, not the next power of two; and 250 frames after the water has gone its pages
    // are back with the driver, where they stayed for as long as the scene lived.
    if(GetEnvironmentVariableA("TGPT_TEST_MEMORY2_WATER", nullptr, 0)>0) {
        const double period=0.4, amplitude=0.03; const int texels=128;
        ComPtr<ID3D11Texture2D> waveDisplacement, waveGradient;
        {
            std::vector<float> disp(size_t(texels)*texels*4, 0.f), grad(size_t(texels)*texels*4, 0.f);
            for(int y=0;y<texels;++y) for(int x=0;x<texels;++x) {
                const double phase=2.0*3.14159265358979*(double(x)+0.5)/texels;
                disp[(size_t(y)*texels+x)*4]=float(amplitude*std::sin(phase));
                grad[(size_t(y)*texels+x)*4]=float(amplitude*2.0*3.14159265358979/period*std::cos(phase));
            }
            D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=texels; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; d.SampleDesc.Count=1;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA a{disp.data(), UINT(texels*16), 0}, b{grad.data(), UINT(texels*16), 0};
            expect(SUCCEEDED(device->CreateTexture2D(&d, &a, &waveDisplacement)) && SUCCEEDED(device->CreateTexture2D(&d, &b, &waveGradient)), "HDRP's wave buffers for the lake");
        }
        auto lake=[&](bool on) {
            auto list=instances; auto listIndices=indices;
            if(on) {
                const M4 world=translate_scale({-0.3f, 0.4f, -0.4f}, {400.f, 1.f, 400.f});
                TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=world.m[r][c];
                w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=TGPT_INSTANCE_WATER;
                listIndices.push_back(0); list.push_back(w);
            }
            TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI;
            c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
            c.displacement=uint64_t(uintptr_t(waveDisplacement.Get())); c.normals=uint64_t(uintptr_t(waveGradient.Get()));
            c.bands[0][0]=float(1.0/period); c.bands[0][3]=1.f; c.fades[0][0]=0.f; c.fades[0][1]=1.f; c.bandCount=1;
            c.maxDisplacement=float(amplitude);
            c.top=0.4f; c.bottom=0.4f;
            c.bounds[0]=-1.5f; c.bounds[1]=-1.2f; c.bounds[2]=0.9f; c.bounds[3]=0.4f; // the caustics' light over the pool's place alone
            return SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0 && SetWaterCaustics(on ? &c : nullptr, on ? 1u : 0u)==0;
        };
        const Camera lakeCam=make_camera({-0.9f, 1.6f, -2.2f}, {-0.9f, 0.f, -0.5f}, 60.f, float(W)/float(H));
        struct Lake2 { std::vector<float> picture; TGPT_Diagnostics with{}, gone{}; std::string line; };
        const char* waterLine="The water's surface:";
        auto lake_run=[&](uint32_t off) {
            cuts_off(off);
            Lake2 l;
            expect(lake(true), "a lake through the room and far around it");
            ownRays=true;
            for(uint32_t i=0;i<30;++i) run(TGPT_VIEW_NONE, lakeCam, lakeCam, i ? 42000+i : 0, still);
            ownRays=false;
            l.picture=readback(); l.with=memory(); l.line=last_line(waterLine);
            // The water goes: 120 frames on its pieces are dropped, and (with the cut) 120 after that the pages they lay in.
            expect(lake(false), "the room without water again");
            for(uint32_t i=0;i<260;++i) run(TGPT_VIEW_NONE, cam, cam, 42100+i, still);
            l.gone=memory();
            return l;
        };
        lake_run(0); // not judged
        const Lake2 fitted=lake_run(0), powers=lake_run(4), fittedAgain=lake_run(0);
        cuts_off(0);
        auto pieces=[&](const Lake2& l) { return number_after(l.line, "The water's surface: "); };
        auto vertexBlocks=[&](const Lake2& l) { const size_t at=l.line.find("the vertices' "); return at==std::string::npos ? -1.0 : number_after(l.line.substr(at), " MB with "); };
        auto blasBlocks=[&](const Lake2& l) { const size_t at=l.line.find("the structures' "); return at==std::string::npos ? -1.0 : number_after(l.line.substr(at), " MB with "); };
        auto pages=[&](const Lake2& l) { return number_after(l.line, " kept, "); };
        std::printf("      a lake 400 m across (%.0f pieces): blocks that fit, %.1f MB of vertices and %.1f MB of structures in %.0f MB of pages; blocks of the next power of two "
                    "(as before), %.1f and %.1f MB in %.0f MB of pages\n      the water's part of the tracer's memory with the lake: %.1f MB against %.1f MB; 260 frames after "
                    "the water went: %.1f MB against %.1f MB\n", pieces(fitted), vertexBlocks(fitted), blasBlocks(fitted), pages(fitted), vertexBlocks(powers), blasBlocks(powers),
                    pages(powers), mb(fitted.with.meshGeometry+fitted.with.acceleration), mb(powers.with.meshGeometry+powers.with.acceleration),
                    mb(fitted.gone.meshGeometry+fitted.gone.acceleration), mb(powers.gone.meshGeometry+powers.gone.acceleration));
        expect(pieces(fitted)>100 && pieces(fitted)==pieces(powers), "the same pieces of water either way, some hundreds of them");
        expect(blasBlocks(fitted)>0 && blasBlocks(fitted)<=0.92*blasBlocks(powers) && vertexBlocks(fitted)>0 && vertexBlocks(fitted)<=0.92*vertexBlocks(powers),
               "the pieces' blocks take at least a twelfth less than blocks of the next power of two");
        expect(pages(fitted)<=pages(powers), "and no more pages");
        expect(fitted.gone.meshGeometry+fitted.gone.acceleration+(40ull<<20)<=powers.gone.meshGeometry+powers.gone.acceleration,
               "once the water has gone its pages go back to the driver (they stayed, as long as the scene lived)");
        const Differ lakeCut=differ(fitted.picture, powers.picture), lakeSame=differ(fitted.picture, fittedAgain.picture);
        size_t wet=0; for(size_t i=0;i<fitted.picture.size();i+=3) wet+=fitted.picture[i+1]>0.02f;
        std::printf("      the lake's picture, blocks that fit against blocks of the next power of two: the largest difference of any pixel's channel %.3g (mean %.3g); the same "
                    "run twice: %.3g (mean %.3g); lit pixels %zu\n", lakeCut.most, lakeCut.mean, lakeSame.most, lakeSame.mean, wet);
        expect(wet>size_t(W)*H/20, "the picture shows the lake");
        expect(lakeCut.mean<=lakeSame.mean*2.0+1e-7 && lakeCut.most<=lakeSame.most*2.0+1e-4, "the water looks the same wherever its pieces' blocks lie");
        save(fitted.picture, "87-lake-blocks-fitted.png", true); save(powers.picture, "87-lake-blocks-powers.png", true);
        expect(SetWaterCaustics(nullptr, 0u)==0 && SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room without water");
        counted("after the lake (the water's passes ran for the first time)");
    }

    // 8. What Windows counts when the tracer gives memory back: off for five seconds (the plugin's maintenance event says
    // so), what only a traced frame needs is let go, and comes back with the next frames. Printed, for PT-87's question
    // whether Windows counts the tracer's OWN shared textures (HDRP's frame's copies, the outputs) once or twice: if the
    // count falls by more than the line does, by about the size of those textures, it counted them twice.
    {
        auto twelve=[&]() { for(uint32_t i=0;i<12;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 43000+i : 0, still); };
        twelve(); twelve();
        const TGPT_Diagnostics on=memory(); const double onCount=windows_mb();
        Sleep(5300);
        render(7, reinterpret_cast<void*>(uintptr_t(1)));
        const TGPT_Diagnostics offNow=memory(); const double offCount=windows_mb();
        twelve();
        const TGPT_Diagnostics back=memory(); const double backCount=windows_mb();
        const std::string size=last_line("tracer textures and reservoirs recreated");
        const double both=number_after(size, "when a frame runs them; ");
        std::printf("      off for five seconds: the tracer's line %.0f -> %.0f -> %.0f MB, Windows' count %.0f -> %.0f -> %.0f MB: the line fell by %.0f and the count by %.0f, "
                    "and they came back by %.0f and %.0f; %.0f MB of the render size's textures are ones both devices hold\n",
                    mb(on.total), mb(offNow.total), mb(back.total), onCount, offCount, backCount, mb(on.total)-mb(offNow.total), onCount-offCount,
                    mb(back.total)-mb(offNow.total), backCount-offCount, both);
        expect(offNow.total<on.total && back.total>offNow.total, "off lets go and the next frames make again (memory-1's cut, here for the count)");
        counted("after off and on again");
    }

    // 9. (memory3) What a texture both devices hold costs in Windows' count, by how it is made and held: measured here on
    // this card and driver, for the ways of holding the tracer's own textures and the game's that Windows would count
    // once. A 4096 x 4096 RGBA8 texture (64 MB) each time; the count is read after each step, the game's device flushed
    // and waited for first. Printed, not judged: it is the driver's and Windows' behaviour, not the tracer's.
    {
        ComPtr<ID3D12Device> d12;
        {
            ComPtr<IDXGIDevice> dxgiDevice; ComPtr<IDXGIAdapter> adapter;
            auto create=reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12CreateDevice"));
            if(create && SUCCEEDED(device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) create(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d12));
        }
        expect(d12!=nullptr, "the bridge's D3D12 device, for the count's probe (one device an adapter in a process)");
        const UINT side=4096;
        auto settle=[&]() {
            D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0}; ComPtr<ID3D11Query> q; device->CreateQuery(&qd, &q);
            if(q) { context->End(q.Get()); context->Flush(); BOOL done=FALSE; for(int i=0;i<3000 && (context->GetData(q.Get(), &done, sizeof done, 0)!=S_OK || !done);++i) Sleep(1); }
            Sleep(60);
        };
        // The steps of one way, as "name +moved" from the step before, in MB.
        std::string trail; double at=0;
        auto begin=[&](const char* name) { settle(); at=windows_mb(); trail=name; };
        auto step=[&](const char* what) { settle(); const double now=windows_mb(); char t[96]; std::snprintf(t, sizeof t, "; %s %+.0f", what, now-at); trail+=t; at=now; };
        auto done=[&]() { std::printf("      %s (MB of Windows' count, each from the step before)\n", trail.c_str()); };
        auto clear11=[&](ID3D11Texture2D* t) {
            ComPtr<ID3D11UnorderedAccessView> uav; const float grey[4]={0.5f, 0.5f, 0.5f, 1.f};
            if(SUCCEEDED(device->CreateUnorderedAccessView(t, nullptr, &uav))) context->ClearUnorderedAccessViewFloat(uav.Get(), grey);
        };
        std::vector<uint8_t> texels(size_t(side)*side*4, 0x55);
        const D3D11_SUBRESOURCE_DATA filled{texels.data(), side*4, 0};
        if(d12) {
            // A. As the tracer makes its own (tgb::SharedTexture::make): the game's device makes it with an NT handle, for
            // shaders to read and write, with nothing in it; the tracer's device opens it; the game's device then writes it.
            {
                D3D11_TEXTURE2D_DESC d{side, side, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS, 0,
                                       D3D11_RESOURCE_MISC_SHARED|D3D11_RESOURCE_MISC_SHARED_NTHANDLE};
                ComPtr<ID3D11Texture2D> t; ComPtr<IDXGIResource1> dxgi; ComPtr<ID3D12Resource> opened; HANDLE handle=nullptr;
                begin("A, the tracer's own way (64 MB, made empty on the game's device with an NT handle)");
                device->CreateTexture2D(&d, nullptr, &t); step("the game's device makes it");
                if(t && SUCCEEDED(t.As(&dxgi)) && SUCCEEDED(dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE, nullptr, &handle))) {
                    d12->OpenSharedHandle(handle, IID_PPV_ARGS(&opened)); CloseHandle(handle); handle=nullptr;
                }
                step(opened ? "the tracer's device opens it" : "(the tracer's device could not open it)");
                if(t) clear11(t.Get());
                step("the game's device writes it");
                if(opened) { ID3D12Pageable* p=opened.Get(); d12->Evict(1, &p); step("the tracer's device evicts its side"); d12->MakeResident(1, &p); step("makes it resident again"); }
                // The game's device lets go of its side while the tracer's holds the texture, and takes it up again by a
                // handle kept meanwhile: what an atlas could do between its bakes (written now and then by the game's
                // device, read every frame by the tracer's). Are the texels still there after?
                HANDLE kept=nullptr; ComPtr<ID3D11Device1> device1; ComPtr<ID3D11Texture2D> again; bool same=false;
                if(t && dxgi) dxgi->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE, nullptr, &kept);
                dxgi.Reset(); t.Reset(); context->ClearState(); step("the game's device lets go (the tracer's still holds it)");
                if(kept && SUCCEEDED(device.As(&device1))) device1->OpenSharedResource1(kept, IID_PPV_ARGS(&again));
                step(again ? "the game's device opens it again by a kept handle" : "(the game's device could not open it again)");
                if(again) {
                    D3D11_TEXTURE2D_DESC one{1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0};
                    ComPtr<ID3D11Texture2D> read; D3D11_MAPPED_SUBRESOURCE map{}; const D3D11_BOX box{side/2, side/2, 0, side/2+1, side/2+1, 1};
                    if(SUCCEEDED(device->CreateTexture2D(&one, nullptr, &read))) {
                        context->CopySubresourceRegion(read.Get(), 0, 0, 0, 0, again.Get(), 0, &box);
                        if(SUCCEEDED(context->Map(read.Get(), 0, D3D11_MAP_READ, 0, &map))) {
                            const uint8_t* px=static_cast<const uint8_t*>(map.pData); same=px[0]>=127 && px[0]<=128 && px[3]==255; context->Unmap(read.Get(), 0);
                        }
                    }
                    step(same ? "reads back the texel it wrote before (still there)" : "reads a texel back (NOT what it wrote before)");
                    clear11(again.Get()); step("writes it again");
                }
                again.Reset(); context->ClearState(); step("the game's device lets go again");
                if(kept) CloseHandle(kept);
                opened.Reset(); step("the tracer's device lets go too");
                done();
            }
            // B. As the game's textures are held (texture_share.cpp): made full on the game's device with the legacy flag, for
            // shaders to read; the tracer's device opens it by the legacy handle.
            {
                D3D11_TEXTURE2D_DESC d{side, side, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_SHARED};
                ComPtr<ID3D11Texture2D> t; ComPtr<IDXGIResource> dxgi; ComPtr<ID3D12Resource> opened; HANDLE handle=nullptr;
                begin("B, the game's textures' way (64 MB, made full on the game's device with the legacy flag)");
                device->CreateTexture2D(&d, &filled, &t); step("the game's device makes it");
                if(t && SUCCEEDED(t.As(&dxgi)) && SUCCEEDED(dxgi->GetSharedHandle(&handle)) && handle) d12->OpenSharedHandle(handle, IID_PPV_ARGS(&opened));
                step(opened ? "the tracer's device opens it" : "(the tracer's device could not open it)");
                if(opened) { ID3D12Pageable* p=opened.Get(); d12->Evict(1, &p); step("the tracer's device evicts its side"); d12->MakeResident(1, &p); step("makes it resident again"); }
                opened.Reset(); step("the tracer's device lets go");
                dxgi.Reset(); t.Reset(); step("the game's device lets go");
                done();
            }
            // The same texture with no flag, for what the flag itself costs.
            {
                D3D11_TEXTURE2D_DESC d{side, side, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
                ComPtr<ID3D11Texture2D> t;
                begin("B0, the same texture with no sharing flag");
                device->CreateTexture2D(&d, &filled, &t); step("the game's device makes it");
                t.Reset(); step("lets go");
                done();
            }
            // C. The other way round: the tracer's device makes it, in a heap that may be shared, and the game's device opens it.
            {
                D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC rd{}; rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width=side; rd.Height=side; rd.DepthOrArraySize=1; rd.MipLevels=1;
                rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count=1; rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
                ComPtr<ID3D12Resource> made; ComPtr<ID3D11Texture2D> opened; ComPtr<ID3D11Device1> device1; HANDLE handle=nullptr;
                begin("C, the other way round (64 MB, made on the tracer's device in a heap that may be shared)");
                HRESULT hr=d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&made));
                if(FAILED(hr)) { // (simultaneous access is for a few formats and uses only: without it)
                    rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                    hr=d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&made));
                }
                step(made ? "the tracer's device makes it" : "(the tracer's device could not make it)");
                if(made && SUCCEEDED(d12->CreateSharedHandle(made.Get(), nullptr, GENERIC_ALL, nullptr, &handle)) && SUCCEEDED(device.As(&device1))) {
                    device1->OpenSharedResource1(handle, IID_PPV_ARGS(&opened)); CloseHandle(handle); handle=nullptr;
                }
                step(opened ? "the game's device opens it" : "(the game's device could not open it)");
                if(opened) { clear11(opened.Get()); step("the game's device writes it"); }
                if(made) { ID3D12Pageable* p=made.Get(); d12->Evict(1, &p); step("the tracer's device evicts its side"); d12->MakeResident(1, &p); step("makes it resident again"); }
                opened.Reset(); step("the game's device lets go");
                made.Reset(); step("the tracer's device lets go");
                done();
            }
            // D. One of the tracer's own alone (what the surfaces, signals and histories are): no second device.
            {
                D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC rd{}; rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; rd.Width=side; rd.Height=side; rd.DepthOrArraySize=1; rd.MipLevels=1;
                rd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count=1; rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                ComPtr<ID3D12Resource> made;
                begin("D, one of the tracer's own alone (64 MB, no second device)");
                d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&made)); step("the tracer's device makes it");
                if(made) { ID3D12Pageable* p=made.Get(); d12->Evict(1, &p); step("evicts it"); d12->MakeResident(1, &p); step("makes it resident again"); }
                made.Reset(); step("lets go");
                // And an upload buffer of the same size (what the tracer's three rings are): where does Windows count it?
                hp.Type=D3D12_HEAP_TYPE_UPLOAD;
                D3D12_RESOURCE_DESC bd{}; bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width=uint64_t(side)*side*4; bd.Height=1; bd.DepthOrArraySize=1; bd.MipLevels=1;
                bd.SampleDesc.Count=1; bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                ComPtr<ID3D12Resource> ring; const double otherBefore=windows_other_mb();
                d12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ring)); step("an upload buffer of 64 MB is made");
                const double otherWith=windows_other_mb();
                ring.Reset(); step("and let go");
                done();
                std::printf("      (the PC's own memory, as Windows counts it for the card's use, moved by %+.0f MB with that upload buffer)\n", otherWith-otherBefore);
            }
        }
        counted("after the count's probe");
    }

    // 10. (memory3) The cap's trial. The registry opens a texture of the game's only while Windows' count stays under a
    // share of the budget, with four times the texture's size of room. That count holds every texture the tracer has
    // opened a second time, and the tracer's own shared textures a second time: no memory of the card's. With
    // TGPT_TEST_SHARE_COUNT_ONCE set those second counts are taken out before the count is weighed (off without it: the
    // rule is as it was). Here: one 2048 BC3 opened (5.38 MB, now twice in the count), a limit 18 MB above the count, and
    // two more such textures, which each want 21.5 MB of room: as counted there is none; counted once there is.
    {
        auto bc3=[&](UINT size) {
            UINT mips=0; while((size>>mips)>0) ++mips;
            std::vector<std::vector<uint8_t>> data(mips); std::vector<D3D11_SUBRESOURCE_DATA> init(mips);
            for(UINT k=0;k<mips;++k) { const UINT b=(std::max(1u, size>>k)+3)/4; data[k].assign(size_t(b)*b*16, uint8_t(0x41+k)); init[k]={data[k].data(), b*16, 0}; }
            D3D11_TEXTURE2D_DESC d{size, size, mips, 1, DXGI_FORMAT_BC3_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, init.data(), &t); return t;
        };
        auto shared=[&](ID3D11Texture2D* t, TGPT_SharedTexture& i) { i=TGPT_SharedTexture{}; i.size=sizeof(i); i.abi=TGPT_ABI; return t ? RegisterSharedTexture(uint64_t(uintptr_t(t)), &i) : -100; };
        auto first=bc3(2048), second=bc3(2048), third=bc3(2048);
        context->Flush(); Sleep(100);
        TGPT_SharedTexture a{}, b{}, c{}, d{};
        const int32_t opened=shared(first.Get(), a);
        const double count=windows_mb();
        SetEnvironmentVariableA("TGPT_TEST_SHARE_LIMIT_MB", std::to_string(int64_t(count)+18).c_str());
        const int32_t asCounted=shared(second.Get(), b);
        SetEnvironmentVariableA("TGPT_TEST_SHARE_COUNT_ONCE", "1");
        const int32_t once=shared(third.Get(), c);
        SetEnvironmentVariableA("TGPT_TEST_SHARE_COUNT_ONCE", nullptr);
        const int32_t asCountedAgain=shared(second.Get(), d);
        SetEnvironmentVariableA("TGPT_TEST_SHARE_LIMIT_MB", nullptr);
        const int32_t later=shared(second.Get(), d);
        std::printf("      the cap's trial: one 2048 BC3 open (%d; the tracer holds %.1f MB of the game's textures open); at a limit 18 MB above Windows' count of %.0f MB "
                    "a second one: %d as the count stands, a third: %d with the second counts taken out, the second again without the trial: %d; with no limit: %d\n",
                    opened, a.openBytes/1048576.0, count, asCounted, once, asCountedAgain, later);
        expect(opened>=0 && asCounted==TGPT_SHARE_BUDGET && asCountedAgain==TGPT_SHARE_BUDGET,
               "as Windows counts, a texture is kept back near the limit (the rule, unchanged without the trial's switch)");
        expect(once>=0, "with the trial's switch the second counts are taken out of the count first, and the texture is opened");
        expect(later>=0, "with room again the texture the cap kept back is opened when asked again");
    }
    } // (cases 5 to 10)
    // 11. (memory4, docs/agents/memory-4.md) The atlases' second count given back: the game's device holds its side of an
    // atlas only around a bake.
#include "pathtracing_memory4_checks.inl"
    start_lines(startLines); // (what the native log added of those lines during the cases)
    cuts_off(0);
    SetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", nullptr);
}
