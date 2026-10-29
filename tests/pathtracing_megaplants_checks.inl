// SPDX-License-Identifier: MIT
// TGPT_TEST_MEGAPLANTS_ONLY=1 (PT-109, docs/agents/megageo-4.md and megageo-5.md; included in tgpt_gpu_test after its
// warm-up, and run alone: the whole test does not take it): NVIDIA RTX Mega Geometry's second piece, ON TRIAL: plant
// groups built from clusters stamped from templates (the hidden "Mega Geometry trial" 5, 7 and 8; TGPT_Frame.flags' bits 2
// to 5, here megaTrial), against the plain groups they stand in for.
//   The claim checked: THE SAME HITS. 5 (the small plants' groups of clusters, with their micromaps as NVAPI's arrays since
//   megageo-5) against the same groups plain with their micromaps (4); 7 (every group of clusters, each plant its own
//   bottom level, in the partitioned top level, with micromaps) and 8 (the same without micromaps) against one group a
//   cell, plain, as it ships (0). And 4 again (what two runs of the same frames differ by, if anything), 6 (the split
//   without micromaps: they change no hit). With the tracer's own camera rays and no denoiser. The scene: sixteen
//   bushes (a plain stem, a crown of checkered cut-out cards; a few hundred triangles, so a few clusters each) and twelve
//   tufts of grass (a plain material: their clusters are OPAQUE) in the wind's pose over four cells of 16 m, and two
//   trees four times a bush's size (5.2 m: no small plant, plain at 5, clusters at 7 and 8); the wind's clock runs, so
//   every group is posed, and every group of clusters stamped, every frame. After 24 frames: the lit picture, the first
//   hits' colour and the direct light. No pixel of 5 against 4, nor of 7 and 8 against 0, may differ more than 4 again
//   does (plus a hundredth of a percent of the picture: two triangles met at the very same distance may be told apart
//   another way by a structure built another way). The crown's cut-out is a plain cutoff, no alpha tile: an alpha tile's
//   coverage is drawn by a chance that hangs on the hit's instance, submesh record and triangle numbers (pt_common
//   ray_chance), which a split or a cluster numbers otherwise: the same picture on average, another draw at every partly
//   covered texel.
//   A run with one bush 0.3 m off must differ: the comparison sees a plant that moved.
//   And the tracer's log must say the clusters served (its line every 10 s and when the trial or its micromaps change:
//   groups stamped from clusters), that none of their calls failed, and, where the driver reports NVAPI's micromaps, that
//   the bushes' clusters link them at 5 and 7 (a micromap that is not used changes no hit: the count is the check). Where
//   the build has no Mega Geometry or the driver does not report clusters, the trial must change nothing: the same
//   comparison, and the run ends as SKIPPED (77).
//   Printed, never judged (other programs may share the GPU): a frame's GPU time with the plain groups, 5, 7 and 8.
{
    std::printf("\nThe small plants' groups built from clusters (PT-109's second piece, on trial) against the plain groups: the same hits\n");
    const int failuresBefore=failures;
    const uint32_t raw=TGPT_OPTION_NO_DENOISER;
    ownRays=true;
    // The materials, after the test's own: a plain stem, checkered cut-out leaves (the leaf texture's alpha at a cutoff of
    // a half: no alpha tile, no chance), plain grass.
    auto all=materials;
    const uint32_t stem=uint32_t(all.size()), checkered=stem+1, grass=stem+2;
    auto leaves=materials[6]; leaves.flags=TGPT_MATERIAL_ALPHA_TEST; leaves.alphaTile=-1; leaves.alphaCutoff=0.5f;
    all.push_back(materials[1]); all.push_back(leaves); all.push_back(materials[2]);
    expect(SetMaterials(0, all.data(), uint32_t(all.size()))==0, "the check's materials");
    auto rot_y=[&](float a) { M4 r=identity(); r.m[0][0]=std::cos(a); r.m[0][2]=std::sin(a); r.m[2][0]=-std::sin(a); r.m[2][2]=std::cos(a); return r; };
    auto cards=[&](std::vector<Vertex>& verts, std::vector<uint16_t>& list, int count, float turn, float width, float y0, float y1, int rows) {
        for(int c=0;c<count;++c) {
            const float a=turn+float(c)*pi/float(count), dx=std::cos(a), dz=std::sin(a);
            const uint16_t base=uint16_t(verts.size());
            for(int r=0;r<=rows;++r) {
                const float y=y0+(y1-y0)*float(r)/float(rows);
                verts.push_back({-0.5f*width*dx, y, -0.5f*width*dz, -dz, 0, dx, 0, float(r)/float(rows)});
                verts.push_back({0.5f*width*dx, y, 0.5f*width*dz, -dz, 0, dx, 1, float(r)/float(rows)});
            }
            for(int r=0;r<rows;++r) {
                const uint16_t q=uint16_t(base+r*2);
                list.insert(list.end(), {q, uint16_t(q+2), uint16_t(q+1), uint16_t(q+1), uint16_t(q+2), uint16_t(q+3)});
            }
        }
    };
    // A bush 1.3 m high: two stem cards of six rows (24 triangles), a crown of eight cards of twelve rows (192).
    std::vector<Vertex> bush; std::vector<uint16_t> stemIndices, crownIndices;
    cards(bush, stemIndices, 2, 0.f, 0.12f, 0.f, 0.8f, 6);
    cards(bush, crownIndices, 8, 0.3f, 0.9f, 0.35f, 1.3f, 12);
    // A tuft of grass 0.7 m high: five cards of eight rows (80 triangles).
    std::vector<Vertex> tuft; std::vector<uint16_t> tuftIndices;
    cards(tuft, tuftIndices, 5, 0.2f, 0.35f, 0.f, 0.7f, 8);
    // A different, denser mesh enters only during growth: its bottom level needs a larger size class than a tuft's.
    std::vector<Vertex> dense; std::vector<uint16_t> denseStem, denseCrown;
    cards(dense, denseStem, 3, 0.f, 0.16f, 0.f, 1.4f, 12);
    cards(dense, denseCrown, 16, 0.3f, 1.8f, 0.3f, 2.8f, 40);
    MeshBuffers bushMesh=make_mesh(bush, {stemIndices, crownIndices}), tuftMesh=make_mesh(tuft, {tuftIndices}), denseMesh=make_mesh(dense, {denseStem, denseCrown});
    const int bushHandle=register_mesh(bushMesh, 0x4d475031), tuftHandle=register_mesh(tuftMesh, 0x4d475032);
    const int denseHandle=register_mesh(denseMesh, 0x4d475033);
    expect(bushHandle>0 && tuftHandle>0 && denseHandle>0, "a bush, a tuft of grass and a larger cluster mesh");
    std::vector<uint8_t> noise(64*64);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) noise[size_t(y)*64+size_t(x)]=uint8_t(std::lround(255.f*(0.6f+0.4f*std::sin(float(x)*0.31f+1.3f)*std::cos(float(y)*0.23f+0.7f))));
    expect(SetPlantWind!=nullptr && SetWindNoise!=nullptr && SetWindNoise(11, 64, 64, 0, noise.data())==0, "the wind's noise map");
    TGPT_PlantWind leafWind{}; leafWind.material=checkered; leafWind.kind=TGPT_PLANT_LEAVES; leafWind.noise=11; leafWind.flags=TGPT_PLANT_BEND;
    leafWind.strength=0.9f; leafWind.bendHeight=1.f; leafWind.pivotOffsetY=-0.5f; leafWind.fade[0]=0.f; leafWind.fade[1]=150.f;
    TGPT_PlantWind stemWind=leafWind; stemWind.material=stem; stemWind.bendHeight=2.f; stemWind.pivotOffsetY=0.f;
    TGPT_PlantWind grassWind{}; grassWind.material=grass; grassWind.kind=TGPT_PLANT_GRASS; grassWind.noise=11; grassWind.strength=80.f;
    const std::vector<TGPT_PlantWind> winds={leafWind, stemWind, grassWind};
    const V3 camForward{-cam.view.m[2][0], -cam.view.m[2][1], -cam.view.m[2][2]};
    // The plants: over the cells about the room's middle (x and z across 0: four cells of 16 m), each turned and scaled its
    // own way; the two trees are the bush four times over (5.2 m: no small plant).
    auto scene_with=[&](float nudge, uint32_t extra=0, uint32_t generation=0) {
        auto list=instances; auto ids=indices;
        auto plant=[&](int mesh, const M4& world) {
            TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=world.m[r][k];
            q.mesh=mesh; q.materialBase=uint32_t(ids.size()); q.mask=0xFF;
            if(mesh!=tuftHandle) { q.flags=TGPT_INSTANCE_ALPHA_TEST; ids.push_back(stem); ids.push_back(checkered); } else ids.push_back(grass);
            list.push_back(q);
        };
        for(int i=0;i<16;++i) {
            const float x=-3.6f+0.48f*float(i)+(i==5 ? nudge : 0.f), z=-1.6f+0.9f*float((i*7)%5), s=0.8f+0.04f*float((i*5)%11);
            plant(bushHandle, mul(translate_scale({x, 0, z}, {s, s, s}), rot_y(0.37f*float(i))));
        }
        for(int i=0;i<12;++i) {
            const float x=-3.3f+0.6f*float(i), z=-1.2f+0.8f*float((i*3)%5), s=0.9f+0.05f*float(i%4);
            plant(tuftHandle, mul(translate_scale({x, 0, z}, {s, s, s}), rot_y(0.9f*float(i))));
        }
        plant(bushHandle, mul(translate_scale({-2.5f, 0, 3.2f}, {4, 4, 4}), rot_y(0.4f)));
        plant(bushHandle, mul(translate_scale({2.8f, 0, 2.6f}, {4, 4, 4}), rot_y(1.9f)));
        for(uint32_t i=0;i<extra;++i) {
            // Populate many cells without covering the camera. Replacement moves them across cell boundaries and changes
            // their ordering, even when the total stays the same. The camera scene remains present throughout.
            const float x=18.f+1.6f*float(i%48)+16.f*float(generation%2), z=5.f+1.6f*float(i/48);
            plant(i%19==0 ? denseHandle : i%3==0 ? bushHandle : tuftHandle,
                  mul(translate_scale({x, 0, z}, {1, 1, 1}), rot_y(0.17f*float(i+generation))));
        }
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "the room with its plants");
        return list.size();
    };
    uint32_t frame=0;
    auto step=[&](uint32_t view, uint32_t index) {
        TGPT_PlantWindFrame wf{}; wf.time=3.f+0.04f*float(frame); wf.speed=30.f; wf.direction=40.f;
        wf.forward[0]=camForward.x; wf.forward[1]=camForward.y; wf.forward[2]=camForward.z;
        SetPlantWind(&wf, winds.data(), uint32_t(winds.size()));
        run(view, cam, cam, index, raw); ++frame;
    };
    struct Shot { std::string what; std::vector<float> px; };
    // One run of the frames with the trial's number: the groups settle (made anew when the number changed: the meshes are
    // cut and their templates built over the first frames), then the three pictures, from the same clock each time.
    auto sequence=[&](uint32_t trial, float nudge) {
        megaTrial=trial;
        scene_with(nudge);
        frame=0;
        for(uint32_t i=0;i<40;++i) { step(TGPT_VIEW_NONE, 50000u+i); context->Flush(); }
        frame=0;
        std::vector<Shot> shots;
        auto frames=[&](uint32_t view, uint32_t n) { for(uint32_t i=0;i<n;++i) step(view, frame ? 51000u+frame : 0u); };
        frames(TGPT_VIEW_NONE, 24); shots.push_back({"the lit picture", readback()});
        frames(TGPT_VIEW_ALBEDO, 1); shots.push_back({"the first hits' colour", readback()});
        frames(TGPT_VIEW_DIRECT, 3); shots.push_back({"the direct light", readback()});
        megaTrial=0;
        return shots;
    };
    // The meshes are converted and built over the first frames.
    megaTrial=0; scene_with(0.f);
    for(uint32_t i=0;i<40;++i) { step(TGPT_VIEW_NONE, i ? 49000u+i : 0u); context->Flush(); Sleep(10); }
    ReadStatus(&status);
    std::printf("      warmed up: %u instances traced of %u listed\n", status.instancesTraced, status.instances);
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
    // megageo-5: the yardsticks are the plain groups WITH their micromaps: 4 (the small plants' split) for 5 (its groups of
    // clusters, now with micromaps), and 0 (one group a cell, as it ships) for 7 and 8 (the same groups, every plant its own
    // bottom level of clusters, in the partitioned top level; 8 without micromaps). Then 4 again (what two runs of the same
    // frames differ by), 6 (the split without micromaps: what they change, if anything) and the control (4 with a bush 0.3 m off).
    const auto split=sequence(4, 0.f);
    const auto clusters=sequence(5, 0.f);
    const auto merged=sequence(0, 0.f);
    const auto copies=sequence(7, 0.f);
    const auto copiesNoMaps=sequence(8, 0.f);
    auto between=[&](const std::vector<Shot>& base, const std::vector<Shot>& other) {
        std::vector<Off> off;
        if(other.size()==base.size()) for(size_t i=0;i<base.size();++i) off.push_back(differ(base[i].px, other[i].px));
        return off;
    };
    const std::vector<Off> offClusters=between(split, clusters), offAgain=between(split, sequence(4, 0.f)), offNoMaps=between(split, sequence(6, 0.f)),
                           offCopies=between(merged, copies), offCopiesNoMaps=between(merged, copiesNoMaps), offMerged=between(split, merged),
                           offControl=between(split, sequence(4, 0.3f));
    const size_t slack=size_t(W)*H/10000;
    const bool whole=!split.empty() && offClusters.size()==split.size() && offAgain.size()==split.size() && offNoMaps.size()==split.size() &&
                     offCopies.size()==split.size() && offCopiesNoMaps.size()==split.size() && offMerged.size()==split.size() && offControl.size()==split.size();
    expect(whole, "the nine runs took the same pictures");
    bool same=whole, sameCopies=whole, sameCopiesNoMaps=whole, sameMaps=whole;
    size_t worstShot=0, worstBy=0, controlSaw=0;
    for(size_t i=0;whole && i<split.size();++i) {
        std::printf("      %-24s pixels off: small plants as clusters (5) against their split (4) %zu; the split again %zu; the split without micromaps (6) %zu; "
                    "NVIDIA's layout (7) against one group a cell (0) %zu, without micromaps (8) %zu; 0 against 4 %zu; the control %zu (most %.4f; %.4f; %.4f; %.4f; %.4f)\n",
                    split[i].what.c_str(), offClusters[i].pixels, offAgain[i].pixels, offNoMaps[i].pixels, offCopies[i].pixels, offCopiesNoMaps[i].pixels, offMerged[i].pixels,
                    offControl[i].pixels, offClusters[i].most, offAgain[i].most, offNoMaps[i].most, offCopies[i].most, offCopiesNoMaps[i].most);
        if(offClusters[i].pixels>offAgain[i].pixels+slack) {
            same=false;
            if(offClusters[i].pixels-offAgain[i].pixels>worstBy) { worstBy=offClusters[i].pixels-offAgain[i].pixels; worstShot=i; }
        }
        if(offCopies[i].pixels>offAgain[i].pixels+slack) sameCopies=false;
        if(offCopiesNoMaps[i].pixels>offAgain[i].pixels+slack) sameCopiesNoMaps=false;
        if(offNoMaps[i].pixels>offAgain[i].pixels+slack) sameMaps=false;
        controlSaw=std::max(controlSaw, offControl[i].pixels);
    }
    expect(same, "with the small plants' groups made of clusters (with micromaps) every picture is their plain split's (no more off than two runs of it are)");
    expect(sameCopies, "with every plant its own bottom level of clusters (with micromaps) in the partitioned top level, every picture is the plain groups'");
    expect(sameCopiesNoMaps, "and without micromaps too");
    expect(sameMaps, "and the split without the small plants' micromaps is the split with them");
    if(!same && worstShot<split.size() && worstShot<clusters.size()) {
        std::printf("      the picture most off: %s (saved as megaplants-differs-split.png and megaplants-differs-clusters.png)\n", split[worstShot].what.c_str());
        save(split[worstShot].px, "megaplants-differs-split.png", true); save(clusters[worstShot].px, "megaplants-differs-clusters.png", true);
    }
    std::printf("      the control (a bush 0.3 m off): %zu pixels differ\n", controlSaw);
    expect(controlSaw>200, "the comparison sees a bush that stands 0.3 m off");
    // (Pictures saved for a look.)
    for(size_t i=0;i<split.size() && i<merged.size() && i<clusters.size() && i<copies.size();++i) {
        const std::string n=std::to_string(i);
        save(split[i].px, ("megaplants-split-"+n+".png").c_str(), true); save(merged[i].px, ("megaplants-merged-"+n+".png").c_str(), true);
        save(clusters[i].px, ("megaplants-clusters-"+n+".png").c_str(), true); save(copies[i].px, ("megaplants-copies-"+n+".png").c_str(), true);
    }
    // megageo-6: a walking scene grows beyond every initial argument/address/size buffer, then shrinks and replaces
    // groups. It mixes small and large bottom levels in the same frame. An unchanged-population test missed this.
    // Start each cycle small, keep trial 7 selected throughout, and check the final camera hits against the same scene
    // on plain structures. A GPU reset, missing group, fallback or unchecked size is a failure below.
    for(uint32_t cycle=0;cycle<2;++cycle) {
        megaTrial=7;
        for(uint32_t extra:{0u, 96u, 1536u, 192u, 1536u, 0u}) {
            scene_with(0.f, extra, cycle+uint32_t(extra==192));
            for(uint32_t n=0;n<48;++n) { step(TGPT_VIEW_NONE, 60000u+frame); context->Flush(); }
            ReadStatus(&status);
            std::printf("      growth cycle %u: %u extra plants, %u traced instances, failed %llu, recording %.3f ms\n",
                        cycle, extra, status.instancesTraced, (unsigned long long)status.framesFailed, double(status.recordMs));
            expect(!status.disabled && !status.framesFailed && status.lastError==0, "growing, shrinking and replacing cluster groups keeps the device running");
            if(status.disabled || status.framesFailed) return 1;
        }
    }
    frame=24; step(TGPT_VIEW_ALBEDO, 51024); const auto afterGrowth=readback();
    const auto restored=sequence(0, 0.f);
    expect(restored.size()>1 && differ(afterGrowth, restored[1].px).pixels<=slack,
           "after repeated buffer growth and group replacement the camera still sees the plain scene's first hits");
    // What the tracer said: the driver's answer at start-up, and whether the clusters served, with micromaps.
    std::string said;
    {
        std::ifstream in(root/"TGPathTracing"/"TGPathTracing.Native.log", std::ios::binary);
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t start=all.rfind("extension bootstrap");
        said=start==std::string::npos ? all : all.substr(start);
    }
    auto line_of=[&](const char* needle, bool last) {
        const size_t at=last ? said.rfind(needle) : said.find(needle); if(at==std::string::npos) return std::string();
        const size_t end=said.find('\n', at); return said.substr(at, end==std::string::npos ? std::string::npos : end-at);
    };
    const bool reported=said.find("reports cluster operations: YES")!=std::string::npos && said.find("pipelines are made with the driver's support for structures built from clusters")!=std::string::npos;
    const bool mapsReported=said.find("reports NVAPI's own opacity micromaps (which its clusters take): YES")!=std::string::npos;
    const bool served=said.find("groups of plants stamped from clusters this frame")!=std::string::npos;
    const bool failed=said.find("clusters are off for good")!=std::string::npos;
    for(const char* needle:{"Mega Geometry: the driver", "Mega Geometry: the tracer's pipelines", "Mega Geometry trial 5:", "Mega Geometry trial 7:", "Mega Geometry trial 8:",
                            "clusters are off for good", "a mesh could not be cut", "could not be made as NVAPI's array", "Mega Geometry trial: this build", "Mega Geometry size check:"}) {
        const std::string l=line_of(needle, true); if(!l.empty()) std::printf("      log: %.700s\n", l.c_str());
    }
    // How many of a trial's plants link micromaps, by its last line ("..., N of their M plants link them").
    auto linking=[&](const char* trial) {
        const std::string l=line_of(trial, true);
        const size_t at=l.find("micromaps in clusters: on, ");
        if(at==std::string::npos) return -1;
        return std::atoi(l.c_str()+at+std::strlen("micromaps in clusters: on, "));
    };
    if(reported) {
        expect(served, "the tracer's log says the plants' groups were stamped from clusters");
        expect(!failed, "no call of the clusters failed");
        uint32_t mostStamps=0, mostBottoms=0;
        for(size_t at=said.find("Mega Geometry size check:"); at!=std::string::npos; at=said.find("Mega Geometry size check:", at+1)) {
            unsigned stamps=0, bottoms=0;
            if(std::sscanf(said.c_str()+at, "Mega Geometry size check: the driver's sizes of a frame's %u stamps and %u bottom levels", &stamps, &bottoms)==2) {
                mostStamps=std::max(mostStamps, uint32_t(stamps)); mostBottoms=std::max(mostBottoms, uint32_t(bottoms));
            }
        }
        std::printf("      growth readbacks reached %u stamps and %u bottom levels\n", mostStamps, mostBottoms);
        expect(mostStamps>4096 && mostBottoms>=1500, "the growth actually exercised the cluster builder beyond its initial rooms");
        expect(said.find("Mega Geometry size check:")!=std::string::npos &&
               said.find("0 stamps and 0 bottom levels larger")!=std::string::npos,
               "the driver's completed result sizes were read and fit the reserved rooms");
        if(mapsReported) {
            const int five=linking("Mega Geometry trial 5:"), seven=linking("Mega Geometry trial 7:");
            std::printf("      plants whose clusters link micromaps: %d at 5, %d at 7\n", five, seven);
            expect(five>0 && seven>0, "where the driver reports NVAPI's micromaps the cut-out plants' clusters carry them (5 and 7)");
        }
    }
    // What each costs (printed, never judged: other programs may share the GPU): a frame's whole GPU time, the wind running,
    // in turn, two rounds of 120 frames on the grown scene: the plain groups, the small plants as clusters, NVIDIA's
    // layout, and it without micromaps. Also print the render thread's recording time (separate from GPU time).
    if(failures==failuresBefore) {
        const uint32_t trials[4]={0u, 5u, 7u, 8u};
        std::vector<float> gpu[4], cpu[4];
        for(uint32_t round=0;round<2;++round) for(uint32_t k=0;k<4;++k) {
            megaTrial=trials[k]; scene_with(0.f, 1536u);
            for(uint32_t n=0;n<120;++n) {
                step(TGPT_VIEW_NONE, 52000u+round*4000u+k*1000u+n); context->Flush();
                if(n>=40) { ReadStatus(&status); gpu[k].push_back(status.gpuMs); cpu[k].push_back(status.recordMs); }
            }
        }
        const char* names[4]={"the plain groups (0)          ", "small plants as clusters (5)  ", "NVIDIA's layout (7)           ", "NVIDIA's layout, no maps (8)  "};
        for(int k=0;k<4;++k) {
            auto& ms=gpu[k]; std::sort(ms.begin(), ms.end());
            if(ms.empty()) continue;
            std::printf("      in turn, %s: a frame's GPU time least %.2f ms, tenth quantile %.2f, median %.2f (%zu frames)\n", names[k], ms.front(), ms[ms.size()/10], ms[ms.size()/2], ms.size());
            std::sort(cpu[k].begin(), cpu[k].end());
            std::printf("      CPU recording median %.3f ms, p90 %.3f ms\n", cpu[k][cpu[k].size()/2], cpu[k][cpu[k].size()*9/10]);
        }
        megaTrial=0;
    }
    megaTrial=0;
    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room again");
    { TGPT_PlantWindFrame none{}; SetPlantWind(&none, nullptr, 0); }
    if(failures) { std::printf("FAILED %d checks\n", failures); return 1; }
    if(!reported) { std::printf("SKIPPED: this build or this driver has no clusters; with the trial set the pictures were the plain ones\n"); return 77; }
    std::printf("small plants as clusters checks passed\n");
    return 0;
}
