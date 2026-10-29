// SPDX-License-Identifier: MIT
// PT-114 (docs/agents/caustgrid.md): the caustics' grid runs only the tiles that can do anything, and writes what it wrote
// (TGPT_TEST_WATER_SURFACE=1, TGPT_TEST_WATER_SEA=1, TGPT_TEST_SEA_PART=grid: by its name alone, it is no part of the
// fixture's whole run). Included in water_sea_gpu.inl, after its lamp part: the sea fixture's beach, sea and waves.
//
// The world: the beach and the open sea (slot 0), and on the land a level terrace with a river over it (slot 1): two long
// narrow sheets of water that cross at a slant, 0.4 m deep, on a terrace over the sand, so that the river's extent (the rectangle round its meshes,
// as the game's rivers and lakes have one) is mostly dry land. The sun, and three lamps whose light has no range, as
// the game's lamps (every tile of the grid has its three lights). The same world with a slab 1 m over the sea from edge
// to edge: the sea under ground, as it lies under a town (nothing of it is lit; the river stays open).
//
// 1. THE SAME TRIANGLES AND THE SAME PICTURE with the trial's bit (TGPT_FRAME_CAUSTICS_GRID) off, on, and off again,
//    from five cameras (the sea's bed through shallow water; the water from afar; no water in view, the sea behind the
//    camera; the river's bed; the sea under ground): 16 frames each with the same frame numbers (the same random numbers),
//    the first with a reset. Per frame, by the tracer's own counters (TGPT_LOG_CAUSTICS_COUNTERS: a log line a frame): the
//    triangles written, their largest tolerance and their checksum (a sum of a hash of every written triangle's corners,
//    light, tolerance and direction: the same triangles, whatever order the threads wrote them in). Per pixel: the frames
//    4 to 15 of the picture as shown (no denoiser) and of the direct light's view (the sun and the caustics on the camera's
//    surfaces alone: no bounced light in it). "Off again" against "off" says what two runs of the same thing differ by.
// 2. THE CUT CUTS: with the bit on, fewer tiles run where the river's extent is under the grid.
// 3. THE STAGE'S TIME, off, on, off (each over more than two of the tracer's one-second windows), per camera.
// TGPT_TEST_GRID_SPLIT=1 with it: instead of 1 to 3, the stage's time by part (TGPT_TEST_CAUSTIC_PARTS, tracer.cpp: the
//    measuring build's stops and the stage's other passes left out one at a time), for three of the cameras;
//    TGPT_TEST_GRID_LAMPS=<count>: with that many more lamps.
if(!std::strcmp(seaPart, "grid")) {
    std::printf("\nThe caustics' grid runs only the tiles that can do anything, and writes what it wrote (PT-114)\n");
    SetEnvironmentVariableA("TGPT_LOG_CAUSTICS_COUNTERS", "1");
    const fs::path gridLog=root/"TGPathTracing"/"TGPathTracing.Native.log";
    std::uintmax_t gridLogAt=0;
    { std::error_code sized; const auto size=fs::file_size(gridLog, sized); if(!sized) gridLogAt=size; }
    struct GridCount { unsigned frame=0, triangles=0, tolerance=0, check0=0, check1=0; unsigned long long ran=0, water=0, lit=0, wrote=0; };
    // The tracer's counter lines added to its log since the last call.
    auto gridCounts=[&]() {
        std::vector<GridCount> found;
        std::ifstream in(gridLog, std::ios::binary);
        if(!in) return found;
        in.seekg(std::streamoff(gridLogAt));
        std::string rest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t whole=rest.rfind('\n');
        if(whole==std::string::npos) return found;
        gridLogAt+=whole+1;
        size_t at=0;
        while(at<=whole) {
            const size_t end=rest.find('\n', at);
            const std::string line=rest.substr(at, end-at);
            const size_t mark=line.find("Caustics frame ");
            GridCount c;
            if(mark!=std::string::npos && std::sscanf(line.c_str()+mark, "Caustics frame %u: %u triangles, tolerance %x, tiles ran %llu, on water %llu, a light reached %llu, wrote %llu, checksum %x %x",
                                                      &c.frame, &c.triangles, &c.tolerance, &c.ran, &c.water, &c.lit, &c.wrote, &c.check0, &c.check1)==9) found.push_back(c);
            at=end+1;
        }
        return found;
    };

    auto gridTurn=[&](float a) { M4 r=identity(); r.m[0][0]=std::cos(a); r.m[0][2]=std::sin(a); r.m[2][0]=-std::sin(a); r.m[2][2]=std::cos(a); return r; };
    const float gridTerraceY=7.5f, gridRiverY=7.9f; // (over the sand, which is 6.8 m high at the terrace's far edge)
    const M4 gridTerrace=translate_scale({-55.f, gridTerraceY, 0.f}, {60.f, 1.f, 90.f});
    const M4 gridSheet[2]={mul(translate_scale({-55.f, gridRiverY, 0.f}, {1, 1, 1}), mul(gridTurn(0.6f), translate_scale({0, 0, 0}, {70.f, 1.f, 3.f}))),
                           mul(translate_scale({-52.f, gridRiverY, 4.f}, {1, 1, 1}), mul(gridTurn(-0.5f), translate_scale({0, 0, 0}, {60.f, 1.f, 2.5f})))};
    TGPT_WaterCaustics gridWater[2]={sea, sea};
    {
        TGPT_WaterCaustics& river=gridWater[1];
        river.top=river.bottom=gridRiverY; river.maxDisplacement=0.3f*sea.maxDisplacement; river.foam[1]=0;
        for(int b=0;b<3;++b) river.bands[b][3]=0.3f;
        river.bounds[0]=river.bounds[1]=1e30f; river.bounds[2]=river.bounds[3]=-1e30f;
        for(const M4& sheet:gridSheet) for(int corner=0;corner<4;++corner) {
            const float lx=corner&1 ? 0.5f : -0.5f, lz=corner&2 ? 0.5f : -0.5f;
            const float wx=sheet.m[0][0]*lx+sheet.m[0][2]*lz+sheet.m[0][3], wz=sheet.m[2][0]*lx+sheet.m[2][2]*lz+sheet.m[2][3];
            river.bounds[0]=std::min(river.bounds[0], wx); river.bounds[1]=std::min(river.bounds[1], wz);
            river.bounds[2]=std::max(river.bounds[2], wx); river.bounds[3]=std::max(river.bounds[3], wz);
        }
        std::printf("      SEA GRID the river's extent: x %.1f to %.1f, z %.1f to %.1f (two sheets of 70 x 3 m and 60 x 2.5 m in it)\n",
                    river.bounds[0], river.bounds[2], river.bounds[1], river.bounds[3]);
    }
    auto gridWorld=[&](bool underGround) {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        auto put=[&](const M4& matrix, uint32_t flags) {
            TGPT_Instance in{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
            in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=flags;
            ids.push_back(12); list.push_back(in);
        };
        put(beach, 0u);
        put(translate_scale({0, 0, 0}, {span, 1, span}), TGPT_INSTANCE_WATER);
        put(gridTerrace, 0u);
        for(const M4& sheet:gridSheet) put(sheet, TGPT_INSTANCE_WATER|(1u<<8));
        if(underGround) put(translate_scale({0, 1, 0}, {span, 1, span}), 0u);
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "grid test scene");
        expect(SetWaterCaustics(gridWater, 2)==0, "grid test water: the sea and the river");
        objects.clear(); objects.push_back({0, beach, {4}, false, false});
    };
    {
        std::vector<TGPT_Light> lamps;
        const float at[3][3]={{6.f, 3.f, 2.f}, {-55.f, 10.f, 1.f}, {-200.f, 20.f, 50.f}};
        for(int i=0;i<3;++i) {
            TGPT_Light lamp{}; for(int a=0;a<3;++a) lamp.position[a]=at[i][a];
            lamp.type=0; lamp.color[0]=40.f; lamp.color[1]=26.f; lamp.color[2]=12.f; lamp.range=12.f; lamp.radius=0.04f; lamp.id=960+uint32_t(i);
            lamp.flags|=TGPT_LIGHT_NO_RANGE;
            lamps.push_back(lamp);
        }
        // TGPT_TEST_GRID_LAMPS=<count> (with the split): that many more lamps without a range, far out on the land, as a
        // town's hundreds of lamps are to the sea's tiles (each tile weighs every lamp for its pick).
        char more[16]{}; GetEnvironmentVariableA("TGPT_TEST_GRID_LAMPS", more, sizeof(more));
        for(int i=0;i<std::min(std::max(std::atoi(more), 0), 4000);++i) {
            TGPT_Light lamp{}; lamp.position[0]=-150.f-6.f*float(i%40); lamp.position[1]=40.f; lamp.position[2]=-120.f+6.f*float(i/40);
            lamp.type=0; lamp.color[0]=4.f; lamp.color[1]=2.6f; lamp.color[2]=1.2f; lamp.range=12.f; lamp.radius=0.04f; lamp.id=2000+uint32_t(i);
            lamp.flags|=TGPT_LIGHT_NO_RANGE;
            lamps.push_back(lamp);
        }
        if(lamps.size()>3) std::printf("      SEA GRID %zu lamps in all\n", lamps.size());
        expect(SetLights(lamps.data(), uint32_t(lamps.size()))==0, "grid test lamps");
    }
    struct GridView { const char* name; Camera camera; bool underGround; };
    const GridView gridViews[5]={
        {"the sea's bed through shallow water", make_camera({5.5f, 3.f, -0.01f}, {5.5f, -0.4f, 0.f}, 40.f, float(W)/float(H)), false},
        {"the water from afar", make_camera({-120.f, 12.5f, 0.f}, {0.f, 0.f, 10.f}, 58.7f, float(W)/float(H)), false},
        {"no water in view (the sea behind the camera)", make_camera({-120.f, 12.5f, 0.f}, {-240.f, 14.f, 0.f}, 58.7f, float(W)/float(H)), false},
        {"the river's bed", make_camera({-58.f, 11.f, -1.f}, {-54.f, 7.5f, 1.f}, 50.f, float(W)/float(H)), false},
        {"the sea under ground", make_camera({-8.f, 3.2f, 0.f}, {22.f, -4.f, 6.f}, 58.7f, float(W)/float(H)), true}};
    const uint32_t gridOptions=asInGame;
    // The trial's steps as frame flags: 1 the pick's own pass, 2 and only tiles with water, 3 and no light out of view.
    auto gridFlags=[](uint32_t step) { return (step>=1 ? TGPT_FRAME_CAUSTICS_GRID : 0u)|(step>=2 ? TGPT_FRAME_CAUSTICS_GRID_WATER : 0u)|(step>=3 ? TGPT_FRAME_CAUSTICS_GRID_VIEW : 0u); };
    // TGPT_TEST_GRID_STEP=<1 to 3>: the step the check holds against "off" (3 without it: all of the trial).
    uint32_t gridStep=3;
    { char asked[8]{}; if(GetEnvironmentVariableA("TGPT_TEST_GRID_STEP", asked, sizeof(asked))>0 && asked[0]>='1' && asked[0]<='3') gridStep=uint32_t(asked[0]-'0'); }
    std::printf("      SEA GRID the check holds the trial's step %u against the trial off\n", gridStep);
    char gridSplit[8]{}; GetEnvironmentVariableA("TGPT_TEST_GRID_SPLIT", gridSplit, sizeof(gridSplit));

    // The stage's time: frames for more than two of the tracer's one-second windows (TGPT_GetStageTimes says the last whole
    // window's mean), then what it says of the caustics' stage.
    auto gridTime=[&](const Camera& c, uint32_t step, uint32_t parts, double* frameMs=nullptr) {
        fogFrameFlags=gridFlags(step);
        SetEnvironmentVariableA("TGPT_TEST_CAUSTIC_PARTS", parts ? std::to_string(parts).c_str() : nullptr);
        const auto start=std::chrono::steady_clock::now();
        uint32_t n=0;
        cut=true; run(TGPT_VIEW_NONE, c, c, 60000, gridOptions); cut=false;
        while(n<60 || std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count()<2600.0) { run(TGPT_VIEW_NONE, c, c, 60001+n, gridOptions); context->Flush(); ++n; }
        char stages[2048]{}; if(GetStageTimes) GetStageTimes(stages, sizeof(stages));
        SetEnvironmentVariableA("TGPT_TEST_CAUSTIC_PARTS", nullptr);
        fogFrameFlags=0;
        ReadStatus(&status); if(frameMs) *frameMs=status.gpuMs;
        const char* at=std::strstr(stages, "caustics ");
        return at ? std::atof(at+9) : -1.0;
    };

    if(gridSplit[0]) {
        // The stage's time by part. Stops (the pass's threads end there): 1 at a tile's first tests, 2 after the lights' pick,
        // 3 after the corner's ray down, 4 after its point on the waves, 5 after the shadow rays, 6 after the rays on.
        const struct { uint32_t parts; const char* name; } gridParts[]={
            {0, "all of it"}, {0x1F00, "none of it (no pass of the stage)"}, {0x1000, "no tile dispatched (the other passes and the build)"},
            {1, "the tiles end at their first tests"}, {7, "... after the pick's first step (each thread's share of the lights)"}, {8, "... after its second (the first thread joins the shares: the two strongest)"},
            {9, "... after its third (each thread's draw among the rest)"}, {2, "... after the lights' pick (the first thread joins the draws)"},
            {3, "... after the corners' rays down"}, {4, "... after the corners' points on the waves"},
            {5, "... after the shadow rays"}, {6, "... after the rays on (no triangle written)"},
            {0x100, "all but the pass for where the water has been"}, {0x200, "all but the kept wave corners' pass"}, {0x400, "all but the emptying of the room"},
            {0x800, "all but the structure's build"}, {0, "all of it, again"}};
        for(int v:{4, 2, 0}) {
            gridWorld(gridViews[v].underGround);
            for(const uint32_t step:{0u, gridStep}) for(const auto& part:gridParts) {
                double frame=0; const double ms=gridTime(gridViews[v].camera, step, part.parts, &frame);
                std::printf("      SEA GRID split, %s, the trial %s: %.3f ms the stage (the frame %.2f ms) with %s\n", gridViews[v].name, step ? "on" : "off", ms, frame, part.name);
                std::fflush(stdout);
            }
        }
    } else {
        constexpr uint32_t kGridFrames=16, kGridFirstCompared=4;
        struct GridSeq { std::vector<std::vector<float>> frames; std::vector<GridCount> counts; };
        auto gridRun=[&](const Camera& c, bool on, uint32_t view) {
            GridSeq s;
            fogFrameFlags=on ? gridFlags(gridStep) : 0u;
            // First the room for the landed triangles settles (it follows the last frames' count, and a frame that wants more
            // than its room loses the rest): frames of other numbers, then theirs out of the log.
            cut=true; run(view, c, c, 50000, gridOptions); cut=false;
            for(uint32_t i=1;i<12;++i) run(view, c, c, 50000+i, gridOptions);
            for(uint32_t i=0;i<kGridFrames+4;++i) {
                run(view, c, c, i, gridOptions); // (its number 0: a reset, and the radiance cache emptied)
                if(i>=kGridFirstCompared && i<kGridFrames) s.frames.push_back(readback());
            }
            fogFrameFlags=0;
            for(const GridCount& count:gridCounts()) if(count.frame<kGridFrames) s.counts.push_back(count);
            return s;
        };
        struct GridDiff { bool counters=false; uint32_t countedFrames=0; double largest=0, mean=0, light=0; uint64_t differing=0, values=0; };
        auto gridCompare=[&](const GridSeq& a, const GridSeq& b) {
            GridDiff d;
            d.counters=a.counts.size()==kGridFrames && b.counts.size()==kGridFrames;
            for(size_t i=0;i<a.counts.size() && i<b.counts.size();++i) {
                const GridCount& x=a.counts[i]; const GridCount& y=b.counts[i];
                if(x.frame==y.frame && x.triangles==y.triangles && x.tolerance==y.tolerance && x.check0==y.check0 && x.check1==y.check1) ++d.countedFrames;
                else d.counters=false;
            }
            for(size_t f=0;f<a.frames.size() && f<b.frames.size();++f) for(size_t p=0;p<a.frames[f].size();++p) {
                const double x=a.frames[f][p], y=b.frames[f][p], gap=std::fabs(x-y);
                ++d.values; d.light+=x;
                if(gap>0) { ++d.differing; d.mean+=gap; d.largest=std::max(d.largest, gap); }
            }
            if(d.values) { d.mean/=double(d.values); d.light/=double(d.values); }
            return d;
        };
        for(const GridView& v:gridViews) {
            gridWorld(v.underGround);
            for(const uint32_t view:{uint32_t(TGPT_VIEW_NONE), uint32_t(TGPT_VIEW_DIRECT)}) {
                const GridSeq off=gridRun(v.camera, false, view), on=gridRun(v.camera, true, view), again=gridRun(v.camera, false, view);
                const GridDiff cutDiff=gridCompare(off, on), floorDiff=gridCompare(off, again);
                const GridCount none{};
                const GridCount& offCount=off.counts.size()>8 ? off.counts[8] : none; const GridCount& onCount=on.counts.size()>8 ? on.counts[8] : none;
                std::printf("      SEA GRID same, %s, %s: frame 8 wrote %u triangles off and %u on (checksum %08X %08X and %08X %08X); tiles ran %llu off and %llu on "
                            "(on water %llu and %llu, a light reached %llu and %llu, wrote %llu and %llu); counters equal in %u of %u frames on against off (%u off again against off); "
                            "pixels on against off: %llu of %llu values differ, the largest by %.6g, on average %.3g (the picture's mean value %.4g); off again against off: %llu differ, the largest by %.6g, on average %.3g\n",
                            v.name, view==TGPT_VIEW_NONE ? "the picture" : "the direct light's view", offCount.triangles, onCount.triangles, offCount.check0, offCount.check1, onCount.check0, onCount.check1,
                            offCount.ran, onCount.ran, offCount.water, onCount.water, offCount.lit, onCount.lit, offCount.wrote, onCount.wrote,
                            cutDiff.countedFrames, kGridFrames, floorDiff.countedFrames,
                            (unsigned long long)cutDiff.differing, (unsigned long long)cutDiff.values, cutDiff.largest, cutDiff.mean, cutDiff.light,
                            (unsigned long long)floorDiff.differing, floorDiff.largest, floorDiff.mean);
                expect(floorDiff.counters, "two runs of the same frames write the same caustic triangles (the check's own ground)");
                expect(cutDiff.counters, "with the grid's trial on every frame writes the same caustic triangles: their count, their largest tolerance and their checksum");
                // (Fewer tiles may count as on water or lit with the trial: a corner no cell needs is not worked out, and a
                // light that cannot land in view is not traced. The tiles that write are the same.)
                expect(onCount.wrote==offCount.wrote && onCount.water<=offCount.water && onCount.lit<=offCount.lit, "with the grid's trial on the same tiles write");
                expect(onCount.ran<=offCount.ran && onCount.ran>=onCount.water, "with the grid's trial on no more tiles run, and every tile with water runs");
                if(gridStep>=2) expect(onCount.ran<offCount.ran, "the grid's trial leaves tiles out: fewer run than without it");
                // The picture: as two runs of the same thing differ, no more (where they do not differ at all: not at all).
                expect(cutDiff.differing<=2*floorDiff.differing && cutDiff.largest<=2.0*floorDiff.largest,
                       "with the grid's trial on the frames are the same pixel for pixel, within what two runs of the same frames differ by");
            }
        }
        for(const GridView& v:gridViews) {
            gridWorld(v.underGround);
            double frame[5]={0, 0, 0, 0, 0}, ms[5];
            const uint32_t steps[5]={0, 1, 2, 3, 0};
            for(int i=0;i<5;++i) ms[i]=gridTime(v.camera, steps[i], 0, &frame[i]);
            std::printf("      SEA GRID time, %s: the caustics' stage %.3f ms off, %.3f ms at step 1 (the pick's own pass), %.3f ms at step 2 (and only tiles with water), "
                        "%.3f ms at step 3 (and no light out of view), %.3f ms off again (a frame %.2f, %.2f, %.2f, %.2f, %.2f ms)\n",
                        v.name, ms[0], ms[1], ms[2], ms[3], ms[4], frame[0], frame[1], frame[2], frame[3], frame[4]);
        }
    }
    SetEnvironmentVariableA("TGPT_LOG_CAUSTICS_COUNTERS", nullptr);
    SetLights(nullptr, 0);
    // (The fixture's own world again, for whatever comes after.)
    {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        for(int k=0;k<2;++k) {
            const M4 matrix=k ? translate_scale({0, 0, 0}, {span, 1, span}) : beach;
            TGPT_Instance in{};
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=matrix.m[r][c];
            in.mesh=quadHandle; in.materialBase=uint32_t(ids.size()); in.mask=0xFF; in.flags=k ? TGPT_INSTANCE_WATER : 0u;
            ids.push_back(12); list.push_back(in);
        }
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "sea test scene, again");
        expect(SetWaterCaustics(&sea, 1)==0, "sea test water, again");
        objects.clear(); objects.push_back({0, beach, {4}, false, false});
    }
}
