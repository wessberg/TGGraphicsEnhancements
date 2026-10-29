// SPDX-License-Identifier: MIT
// TGPT_TEST_MEGA_ONLY=1 (PT-109, docs/agents/megageo-1.md; included in tgpt_gpu_test after its warm-up, and run alone:
// the whole test does not take it): NVIDIA RTX Mega Geometry's first piece, the PARTITIONED TOP LEVEL (the tracer's
// mega_geometry.hpp; TGPT_Frame.flags' bits 2 to 5, here megaTrial), against the plain top level it stands in for.
//   The claim checked: THE SAME INSTANCES, THE SAME HITS. The same frames are traced three times: with the plain top
//   level, with the partitioned one, and with the plain one again (what two runs of the same frames differ by, if
//   anything). With the tracer's own camera rays and no denoiser. In each run the scene changes the ways the game's does:
//     1. a field of 4,000 boxes over 450 m (many partitions) around the test's room, nothing moving;
//     2. a thing the game moves appears (TGPT_SetMoving), then moves 3 m;
//     3. a character's vertices move 5.5 m while its instance stays where it is: its bottom level is refitted in place,
//        so the top level must take its new bounds (what the game's characters, plants and water do every frame);
//     4. 300 boxes leave from the middle of the scene's list (every later instance changes its number);
//     5. 4,500 boxes arrive (more than the partitioned top level has room for: it is made anew, larger);
//     6. the camera goes 300 m (the scene's origin steps 256 m: every transform changes);
//     7. back to the first scene, and three frames in which nothing changes.
//   After each, the lit picture and the first hit's colour (the Albedo view). No pixel of the partitioned run may differ
//   from the plain run by more than the second plain run does (plus a hundredth of a percent of the picture: two triangles
//   met at the very same distance may be told apart another way by another structure).
//   A fourth run, plain, with the moved thing 0.5 m off, must differ: the check sees a picture that changed.
//   A fifth and a sixth, partitioned in the trial's other two sizes of cell (megaTrial 2: 128 m, 3: 256 m), judged as
//   the second.
//   And the tracer's log must say the partitioned top level served. Where the build has no Mega Geometry or the driver
//   does not report it, the trial must change nothing: the same comparison, and the run ends as SKIPPED (77).
//   Printed, never judged (other programs may share the GPU): the stages' GPU times with each top level.
{
    std::printf("\nThe partitioned top level (PT-109, on trial) against the plain one: the same instances, the same hits\n");
    const int failuresBefore=failures;
    const uint32_t raw=TGPT_OPTION_NO_DENOISER;
    ownRays=true;
    auto put=[](std::vector<TGPT_Instance>& list, std::vector<uint32_t>& ids, int mesh, const M4& world, uint32_t material) {
        TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
        in.mesh=mesh; in.materialBase=uint32_t(ids.size()); in.mask=0xFF;
        ids.push_back(material); ids.push_back(material); list.push_back(in);
    };
    // The room's own list, then the field: a box every 7 m, 64 by 64 (but where the room stands), of two materials.
    std::vector<TGPT_Instance> full=instances, less=instances, more; std::vector<uint32_t> fullIds=indices, lessIds=indices, moreIds;
    uint32_t boxes=0, left=0;
    for(int gz=0;gz<64;++gz) for(int gx=0;gx<64;++gx) {
        const V3 at{-224.f+7.f*float(gx)+1.5f*float(gz%2), 1.f+0.5f*float((gx*7+gz*3)%5), -224.f+7.f*float(gz)};
        if(std::fabs(at.x)<16.f && std::fabs(at.z)<16.f) continue;
        const M4 world=translate_scale(at, {2.f, 2.f+float((gx+gz)%3), 2.f});
        const uint32_t material=uint32_t((gx+gz)%2);
        put(full, fullIds, cubeHandle, world, material); ++boxes;
        // 300 of them gone from the list's first half: every instance after one of them has another number.
        if(gz<32 && (gx+gz*64)%7==3 && left<300) { ++left; continue; }
        put(less, lessIds, cubeHandle, world, material);
    }
    more=full; moreIds=fullIds;
    for(int gz=0;gz<67;++gz) for(int gx=0;gx<67;++gx) put(more, moreIds, cubeHandle, translate_scale({-231.f+7.f*float(gx), 9.f+float((gx+gz)%4), -231.f+7.f*float(gz)}, {1.5f, 1.5f, 1.5f}), uint32_t((gx*3+gz)%2));
    std::printf("      the field: %u boxes; the lists: %zu instances, %zu with 300 gone, %zu with 4,489 more\n", boxes, full.size(), less.size(), more.size());
    // A character (the whole test's own kind: a box whose vertices live in a skinning output and are written again).
    struct SkinVertex { float x, y, z; uint32_t packed[2]; };
    const uint32_t first=16, count=uint32_t(cube.size());
    std::vector<SkinVertex> verts(first+count, SkinVertex{});
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=UINT(verts.size()*sizeof(SkinVertex)); bd.Usage=D3D11_USAGE_DEFAULT;
    bd.BindFlags=D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride=sizeof(SkinVertex);
    ComPtr<ID3D11Buffer> skinning; device->CreateBuffer(&bd, nullptr, &skinning);
    auto pose=[&](V3 c) {
        for(uint32_t i=0;i<count;++i) verts[first+i]=SkinVertex{c.x+cube[i].px*0.8f, c.y+cube[i].py*0.8f, c.z+cube[i].pz*0.8f, {0, 0}};
        context->UpdateSubresource(skinning.Get(), 0, nullptr, verts.data(), 0, 0);
    };
    std::vector<uint32_t> skinIndices(sides.begin(), sides.end()); skinIndices.insert(skinIndices.end(), top.begin(), top.end());
    const uint32_t starts[2]={0, uint32_t(sides.size())};
    TGPT_CpuMeshDesc md{sizeof(TGPT_CpuMeshDesc), TGPT_ABI}; md.key=0x4d454741; md.indices=skinIndices.data(); md.vertexCount=count; md.indexCount=uint32_t(skinIndices.size());
    const int skinHandle=RegisterSkinnedMesh && skinning ? RegisterSkinnedMesh(&md, starts, 2) : -100;
    expect(skinHandle>0, "a character's triangles registered");
    TGPT_SkinnedInstance si{}; si.transform[0]=si.transform[5]=si.transform[10]=1.f;
    si.mesh=skinHandle; si.firstVertex=first; si.vertexCount=count; si.submeshCount=2; si.materials[0]=si.materials[1]=1;
    const V3 stands{-1.5f, 1.2f, 4.5f}, walked{4.0f, 1.2f, 4.5f};
    const uint32_t red[2]={1u, 1u};
    auto place=[](TGPT_Moving& t, const M4& now, const M4& before) {
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) { t.transform[r*4+c]=now.m[r][c]; t.previous[r*4+c]=before.m[r][c]; }
    };
    const Camera high=make_camera({0, 70.f, -150.f}, {0, 0, 20.f}, 60.f, float(W)/float(H));
    const Camera away=make_camera({300.f, 70.f, -150.f}, {150.f, 0, 20.f}, 60.f, float(W)/float(H));
    struct Shot { std::string what; std::vector<float> px; };
    // One run of the frames. trial: the top level (0 plain, 1 partitioned). nudge: the moved thing this far off (the control).
    auto sequence=[&](uint32_t trial, float nudge) {
        megaTrial=trial;
        std::vector<Shot> shots;
        uint32_t frame=0;
        auto frames=[&](const Camera& c, uint32_t view, uint32_t n) {
            for(uint32_t i=0;i<n;++i) {
                if(skinHandle>0) SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
                run(view, c, c, frame ? 31000u+frame : 0u, raw); ++frame;
            }
        };
        auto shoot=[&](const char* what, const Camera& c) {
            frames(c, TGPT_VIEW_NONE, 3); shots.push_back({std::string(what)+", the lit picture", readback()});
            frames(c, TGPT_VIEW_ALBEDO, 1); shots.push_back({std::string(what)+", the first hits' colour", readback()});
        };
        pose(stands);
        SetScene(full.data(), uint32_t(full.size()), fullIds.data(), uint32_t(fullIds.size())); SetMoving(nullptr, 0, nullptr, 0, nullptr, 0);
        shoot("1 the field from above", high);
        shoot("1 the room", cam);
        TGPT_Moving box{}; box.mesh=cubeHandle; box.materialBase=0; box.mask=0xFF;
        const M4 here=translate_scale({-1.2f+nudge, 0.5f, 2.5f}, {1, 1, 1}), there=translate_scale({1.8f+nudge, 0.5f, 2.5f}, {1, 1, 1});
        place(box, here, here); SetMoving(&box, 1, red, 2, nullptr, 0);
        shoot("2 a thing the game moves appears", cam);
        place(box, there, here); SetMoving(&box, 1, red, 2, nullptr, 0);
        frames(cam, TGPT_VIEW_NONE, 1);
        place(box, there, there); SetMoving(&box, 1, red, 2, nullptr, 0);
        shoot("2 it moved 3 m", cam);
        pose(walked);
        shoot("3 a character's vertices 5.5 m on", cam);
        SetScene(less.data(), uint32_t(less.size()), lessIds.data(), uint32_t(lessIds.size()));
        shoot("4 300 boxes gone from the list's middle", high);
        SetScene(more.data(), uint32_t(more.size()), moreIds.data(), uint32_t(moreIds.size()));
        shoot("5 4,489 boxes more", high);
        shoot("6 the camera 300 m on", away);
        SetScene(full.data(), uint32_t(full.size()), fullIds.data(), uint32_t(fullIds.size()));
        shoot("7 the first scene again", high);
        frames(high, TGPT_VIEW_NONE, 3);
        shoot("7 after three frames with nothing changed", cam);
        megaTrial=0;
        return shots;
    };
    // The character's mesh is converted and built over the first frames, and the field's instances are taken in.
    megaTrial=0; pose(stands);
    SetScene(full.data(), uint32_t(full.size()), fullIds.data(), uint32_t(fullIds.size())); SetMoving(nullptr, 0, nullptr, 0, nullptr, 0);
    for(uint32_t i=0;i<40;++i) {
        if(skinHandle>0) SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
        run(TGPT_VIEW_NONE, cam, cam, i ? 30000u+i : 0u, raw); context->Flush();
    }
    ReadStatus(&status);
    std::printf("      warmed up: %u instances traced of %u listed\n", status.instancesTraced, status.instances);
    expect(status.instancesTraced>=uint32_t(full.size()), "the field's instances are all traced");
    // A pixel differs where a channel is off by more than a thousandth and 2 % of itself.
    struct Off { size_t pixels=0; double most=0; };
    auto differ=[&](const std::vector<float>& a, const std::vector<float>& b) {
        Off off;
        for(size_t i=0;i+2<a.size() && i+2<b.size();i+=3) {
            bool differs=false;
            for(int k=0;k<3;++k) {
                const double d=std::fabs(double(a[i+k])-double(b[i+k]));
                if(!(d<=1e30)) { differs=true; off.most=1e30; continue; }
                off.most=std::max(off.most, d);
                if(d>1e-3+0.02*std::fabs(double(a[i+k]))) differs=true;
            }
            if(differs) ++off.pixels;
        }
        return off;
    };
    const auto plain=sequence(0, 0.f);
    const auto parted=sequence(1, 0.f);
    // (The other two runs are compared and let go at once: a run's pictures are a quarter of a gigabyte.)
    auto against=[&](const std::vector<Shot>& other) {
        std::vector<Off> off;
        if(other.size()==plain.size()) for(size_t i=0;i<plain.size();++i) off.push_back(differ(plain[i].px, other[i].px));
        return off;
    };
    const std::vector<Off> offParted=against(parted), offAgain=against(sequence(0, 0.f)), offControl=against(sequence(0, 0.5f));
    // The trial's other two sizes of cell (2: 128 m, 3: 256 m): the same frames once more each.
    const std::vector<Off> offWider=against(sequence(2, 0.f)), offWidest=against(sequence(3, 0.f));
    const size_t slack=size_t(W)*H/10000;
    const bool whole=!plain.empty() && offParted.size()==plain.size() && offAgain.size()==plain.size() && offControl.size()==plain.size() &&
                     offWider.size()==plain.size() && offWidest.size()==plain.size();
    expect(whole, "the six runs took the same pictures");
    bool same=whole, sameWider=whole;
    size_t worstShot=0, worstBy=0, controlSaw=0;
    for(size_t i=0;whole && i<plain.size();++i) {
        std::printf("      %-58s differs from plain in pixels: cells of 64 m %zu, 128 m %zu, 256 m %zu; plain again %zu (most %.4f, %.4f, %.4f; %.4f)\n", plain[i].what.c_str(),
                    offParted[i].pixels, offWider[i].pixels, offWidest[i].pixels, offAgain[i].pixels, offParted[i].most, offWider[i].most, offWidest[i].most, offAgain[i].most);
        if(offParted[i].pixels>offAgain[i].pixels+slack) {
            same=false;
            if(offParted[i].pixels-offAgain[i].pixels>worstBy) { worstBy=offParted[i].pixels-offAgain[i].pixels; worstShot=i; }
        }
        if(offWider[i].pixels>offAgain[i].pixels+slack || offWidest[i].pixels>offAgain[i].pixels+slack) sameWider=false;
        if(plain[i].what.rfind("2 ", 0)==0) controlSaw=std::max(controlSaw, offControl[i].pixels);
    }
    expect(same, "with the partitioned top level every picture is the plain one's (no more off than two plain runs are)");
    expect(sameWider, "and with its cells of 128 m and of 256 m");
    if(!same && worstShot<plain.size() && worstShot<parted.size()) {
        std::printf("      the picture most off: %s (saved as mega-differs-plain.png and mega-differs-partitioned.png)\n", plain[worstShot].what.c_str());
        save(plain[worstShot].px, "mega-differs-plain.png", true); save(parted[worstShot].px, "mega-differs-partitioned.png", true);
    }
    std::printf("      the control (the moved thing 0.5 m off): %zu pixels differ\n", controlSaw);
    expect(controlSaw>500, "the comparison sees a thing that stands 0.5 m off");
    if(!plain.empty() && !parted.empty()) { save(plain[0].px, "mega-field-plain.png", true); save(parted[0].px, "mega-field-partitioned.png", true); }
    // What the tracer said: the driver's answer at start-up, and whether the partitioned top level served.
    std::string said;
    {
        std::ifstream in(root/"TGPathTracing"/"TGPathTracing.Native.log", std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t start=all.rfind("extension bootstrap");
        said=start==std::string::npos ? all : all.substr(start);
    }
    auto line_of=[&](const char* needle) { const size_t at=said.find(needle); if(at==std::string::npos) return std::string(); const size_t end=said.find('\n', at); return said.substr(at, end==std::string::npos ? std::string::npos : end-at); };
    const bool reported=said.find("the partitioned top level: YES")!=std::string::npos;
    const bool served=said.find("Mega Geometry: the top level is partitioned")!=std::string::npos;
    const bool failed=said.find("the partitioned top level failed")!=std::string::npos;
    for(const char* needle:{"Mega Geometry: the driver", "Mega Geometry: the top level is partitioned", "Mega Geometry: an instance carries", "the partitioned top level failed", "Mega Geometry trial: this build"}) {
        const std::string l=line_of(needle); if(!l.empty()) std::printf("      log: %.300s\n", l.c_str());
    }
    {
        size_t made=0; for(size_t at=said.find("Mega Geometry: the top level is partitioned");at!=std::string::npos;at=said.find("Mega Geometry: the top level is partitioned", at+1)) ++made;
        std::printf("      the partitioned top level was made %zu times (first use, and each time its room grew or the trial came back on)\n", made);
    }
    if(reported) {
        expect(served, "the tracer's log says the partitioned top level served");
        expect(!failed, "no call of the partitioned top level failed");
    }
    // What each top level costs (printed, never judged: other programs may share the GPU). First the four in turn (plain,
    // and partitioned in cells of 64, 128 and 256 m), sixteen rounds of 200 frames of the larger scene from above with a
    // thing moving and a character refitted every frame: a frame's whole GPU time (TGPT_Status.gpuMs), so that what the
    // rays lose or gain through each structure is in it, and another program's load on the card falls on all alike.
    if(failures==failuresBefore) {
        SetScene(more.data(), uint32_t(more.size()), moreIds.data(), uint32_t(moreIds.size()));
        TGPT_Moving box{}; box.mesh=cubeHandle; box.materialBase=0; box.mask=0xFF;
        std::vector<float> gpu[4]; double seconds[4]={0, 0, 0, 0};
        const char* const names[4]={"plain             ", "partitioned, 64 m ", "partitioned, 128 m", "partitioned, 256 m"};
        uint32_t i=0;
        for(uint32_t round=0;round<16;++round) {
            const uint32_t trial=round%4;
            megaTrial=trial;
            const auto began=std::chrono::steady_clock::now();
            for(uint32_t n=0;n<200;++n, ++i) {
                const float x=-3.f+0.02f*float(i%300), xBefore=-3.f+0.02f*float(i ? (i-1)%300 : 0);
                place(box, translate_scale({x, 0.5f, 2.5f}, {1, 1, 1}), translate_scale({xBefore, 0.5f, 2.5f}, {1, 1, 1})); SetMoving(&box, 1, red, 2, nullptr, 0);
                if(skinHandle>0) SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
                run(TGPT_VIEW_NONE, high, high, i ? 32000u+i : 0u, raw); context->Flush();
                // (The first 40 frames of a round still hold the other structure's frames in flight and its making.)
                if(n>=40) { ReadStatus(&status); gpu[trial].push_back(status.gpuMs); }
            }
            seconds[trial]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count();
        }
        for(uint32_t trial=0;trial<4;++trial) {
            auto& ms=gpu[trial]; std::sort(ms.begin(), ms.end());
            if(ms.empty()) continue;
            std::printf("      in turn, %s top level, %zu instances: a frame's GPU time least %.2f ms, tenth quantile %.2f, median %.2f (%zu frames); %.1f frames a second by the clock\n",
                        names[trial], more.size()+2, ms.front(), ms[ms.size()/10], ms[ms.size()/2], ms.size(), seconds[trial]>0 ? 800.0/seconds[trial] : 0.0);
        }
        megaTrial=0;
    }
    // Then 21 s each, so that one whole 10 s window of the tracer's log lies inside each: its "PT structures" line (the
    // top level's own GPU time among the scene stage's kinds).
    for(uint32_t trial=0;trial<2 && failures==failuresBefore;++trial) {
        megaTrial=trial;
        SetScene(more.data(), uint32_t(more.size()), moreIds.data(), uint32_t(moreIds.size()));
        TGPT_Moving box{}; box.mesh=cubeHandle; box.materialBase=0; box.mask=0xFF;
        char stages[1024]{};
        const auto began=std::chrono::steady_clock::now();
        uint32_t i=0;
        for(;std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<21.0;++i) {
            const float x=-3.f+0.02f*float(i%300), xBefore=-3.f+0.02f*float(i ? (i-1)%300 : 0);
            place(box, translate_scale({x, 0.5f, 2.5f}, {1, 1, 1}), translate_scale({xBefore, 0.5f, 2.5f}, {1, 1, 1})); SetMoving(&box, 1, red, 2, nullptr, 0);
            if(skinHandle>0) SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
            run(TGPT_VIEW_NONE, high, high, i ? 33000u+trial*100000u+i : 0u, raw); context->Flush();
        }
        GetStageTimes(stages, sizeof(stages));
        std::printf("      %s top level, %zu instances, one thing moving, %u frames: %s\n", trial ? "partitioned" : "plain      ", more.size()+2, i, stages);
        std::ifstream in(root/"TGPathTracing"/"TGPathTracing.Native.log", std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t at=all.rfind("PT structures ");
        if(at!=std::string::npos) { const size_t end=all.find('\n', at); std::printf("      log: %.1400s\n", all.substr(at, end==std::string::npos ? std::string::npos : end-at).c_str()); }
        else std::printf("      (no \"PT structures\" line in the tracer's log yet)\n");
    }
    megaTrial=0; SetMoving(nullptr, 0, nullptr, 0, nullptr, 0); SetSkinned(0, 0, nullptr, 0);
    if(failures) { std::printf("FAILED %d checks\n", failures); return 1; }
    if(!reported) { std::printf("SKIPPED: this build or this driver has no partitioned top level; with the trial set the pictures were the plain ones\n"); return 77; }
    std::printf("partitioned top level checks passed\n");
    return 0;
}
