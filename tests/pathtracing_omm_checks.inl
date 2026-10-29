// SPDX-License-Identifier: MIT
// TGPT_TEST_OMM_ONLY=1 (PT-110, docs/agents/omm-1.md; included in tgpt_gpu_test after its warm-up, and run alone: the
// whole test does not take it): M3, a character's and a plant group's submeshes that are no cut-out are solid in their own
// structures (TGPT_Frame.flags' bits 6 to 13, here ommTrial: 0 as it ships, 16 the old way, every triangle of them handed to
// the shader's alpha test).
//   The claim checked: THE SAME HITS. The same frames are traced with the marks (0), without them (16), and with them
//   again (what two runs of the same frames differ by, if anything), with the tracer's own camera rays and no denoiser:
//     - a character whose sides are a plain material and whose lid is the plugin's invisible one (a cut-out no ray meets):
//       the sun must reach the inside through the lid either way, and the sides must stop it either way;
//     - two small trees in the wind's pose, in one group: a plain trunk each, the one's leaves invisible, the other's a
//       checkered cut-out; the wind's clock runs, so the group is refitted every frame.
//   After 24 frames: the lit picture, the first hits' colour and the direct light. No pixel of the run without the marks
//   may differ from the first by more than the third does (plus a hundredth of a percent of the picture: two triangles
//   met at the very same distance may be told apart another way by a structure built another way).
//   A fourth run with the trial's 1 and 2 (the plants' and the characters' cut-outs SOLID, the measuring numbers) must
//   differ: the lid and the invisible leaves then stop the sun. So the comparison sees a cut-out that turned solid.
//   And the tracer's log must count them ("PT cut-outs", every 10 s): 36 cut-out triangles of plants and 2 of the
//   character, 42 plain triangles spared the shader's test as it ships, and the same 42 handed to it with the 16.
//   It runs on either runtime. Micromaps themselves need DirectX Raytracing 1.2 (the staged Agility runtime): on Windows'
//   own (TGPT_TEST_NO_AGILITY=1) the scene holds none, and this check says nothing about them.
{
    std::printf("\nPlain submeshes of a character and of plants in the wind, solid in their own structures (PT-110's M3): the same hits\n");
    const int failuresBefore=failures;
    const uint32_t raw=TGPT_OPTION_NO_DENOISER;
    ownRays=true;
    // The materials, after the test's own: a plain trunk, invisible leaves, checkered leaves, a character's plain sides.
    auto all=materials;
    const uint32_t trunk=uint32_t(all.size()), unseen=trunk+1, checkered=trunk+2, body=trunk+3;
    all.push_back(materials[1]); all.push_back(materials[7]); all.push_back(materials[6]); all.push_back(materials[1]);
    expect(SetMaterials(0, all.data(), uint32_t(all.size()))==0, "the check's materials");
    // A small tree of cards (as the plants' own check makes them): a trunk of two crossed cards, a crown of three.
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
    std::vector<Vertex> tree; std::vector<uint16_t> trunkIndices, leafIndices;
    cards(tree, trunkIndices, 2, 0.f, 0.2f, 0.f, 1.7f, 4);
    cards(tree, leafIndices, 3, 0.5f, 1.2f, 1.1f, 2.0f, 3);
    MeshBuffers treeMesh=make_mesh(tree, {trunkIndices, leafIndices});
    const int treeHandle=register_mesh(treeMesh, 0x4f4d4d31);
    expect(treeHandle>0, "a small tree: a trunk and a crown");
    std::vector<uint8_t> noise(64*64, 190);
    expect(SetPlantWind!=nullptr && SetWindNoise!=nullptr && SetWindNoise(9, 64, 64, 0, noise.data())==0, "the wind's noise map");
    TGPT_PlantWind leafWind{}; leafWind.material=unseen; leafWind.kind=TGPT_PLANT_LEAVES; leafWind.noise=9; leafWind.flags=TGPT_PLANT_BEND;
    leafWind.strength=0.9f; leafWind.bendHeight=1.f; leafWind.pivotOffsetY=-1.f; leafWind.fade[0]=0.f; leafWind.fade[1]=150.f;
    TGPT_PlantWind checkWind=leafWind; checkWind.material=checkered;
    TGPT_PlantWind trunkWind=leafWind; trunkWind.material=trunk; trunkWind.bendHeight=2.f; trunkWind.pivotOffsetY=0.f;
    const std::vector<TGPT_PlantWind> winds={leafWind, checkWind, trunkWind};
    const V3 camForward{-cam.view.m[2][0], -cam.view.m[2][1], -cam.view.m[2][2]};
    // The room with the two trees in the sun by its right wall (one 16 m cell: one group of two members).
    auto list=instances; auto ids=indices;
    auto plant=[&](const M4& world, uint32_t leaves) {
        TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=world.m[r][k];
        q.mesh=treeHandle; q.materialBase=uint32_t(ids.size()); q.mask=0xFF; q.flags=TGPT_INSTANCE_ALPHA_TEST;
        ids.push_back(trunk); ids.push_back(leaves); list.push_back(q);
    };
    plant(mul(translate_scale({3.6f, 0, 0.4f}, {1.4f, 1.4f, 1.4f}), rot_y(0.3f)), unseen);
    plant(mul(translate_scale({4.7f, 0, 1.5f}, {1.4f, 1.4f, 1.4f}), rot_y(1.2f)), checkered);
    // The character (the whole test's own kind: a box whose vertices live in a skinning output and are written again):
    // five faces of the plain material, its lid of the invisible one.
    struct SkinVertex { float x, y, z; uint32_t packed[2]; };
    const uint32_t first=16, count=uint32_t(cube.size());
    std::vector<SkinVertex> verts(first+count, SkinVertex{});
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=UINT(verts.size()*sizeof(SkinVertex)); bd.Usage=D3D11_USAGE_DEFAULT;
    bd.BindFlags=D3D11_BIND_SHADER_RESOURCE; bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; bd.StructureByteStride=sizeof(SkinVertex);
    ComPtr<ID3D11Buffer> skinning; device->CreateBuffer(&bd, nullptr, &skinning);
    auto pose=[&](V3 c) {
        for(uint32_t i=0;i<count;++i) verts[first+i]=SkinVertex{c.x+cube[i].px*1.6f, c.y+cube[i].py*1.6f, c.z+cube[i].pz*1.6f, {0, 0}};
        context->UpdateSubresource(skinning.Get(), 0, nullptr, verts.data(), 0, 0);
    };
    std::vector<uint32_t> skinIndices(sides.begin(), sides.end()); skinIndices.insert(skinIndices.end(), top.begin(), top.end());
    const uint32_t starts[2]={0, uint32_t(sides.size())};
    TGPT_CpuMeshDesc md{sizeof(TGPT_CpuMeshDesc), TGPT_ABI}; md.key=0x4f4d4d32; md.indices=skinIndices.data(); md.vertexCount=count; md.indexCount=uint32_t(skinIndices.size());
    const int skinHandle=RegisterSkinnedMesh && skinning ? RegisterSkinnedMesh(&md, starts, 2) : -100;
    expect(skinHandle>0, "a character's triangles registered");
    TGPT_SkinnedInstance si{}; si.transform[0]=si.transform[5]=si.transform[10]=1.f;
    si.mesh=skinHandle; si.firstVertex=first; si.vertexCount=count; si.submeshCount=2; si.materials[0]=body; si.materials[1]=unseen;
    const V3 stands{-2.2f, 0.81f, 0.4f};
    expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "the room with the two trees");
    // A frame: the character's pose, the wind's clock a little on (the group is refitted), the frame.
    uint32_t frame=0;
    auto step=[&](uint32_t view, uint32_t index) {
        if(skinHandle>0) SetSkinned(uint64_t(uintptr_t(skinning.Get())), sizeof(SkinVertex), &si, 1);
        TGPT_PlantWindFrame wf{}; wf.time=3.f+0.04f*float(frame); wf.speed=30.f; wf.direction=40.f;
        wf.forward[0]=camForward.x; wf.forward[1]=camForward.y; wf.forward[2]=camForward.z;
        SetPlantWind(&wf, winds.data(), uint32_t(winds.size()));
        run(view, cam, cam, index, raw); ++frame;
    };
    struct Shot { std::string what; std::vector<float> px; };
    // One run of the frames with the trial's number: the structures settle (a character's is built anew and the group is
    // made anew when the number changed), then the three pictures.
    auto sequence=[&](uint32_t trial) {
        ommTrial=trial; frame=0;
        std::vector<Shot> shots;
        auto frames=[&](uint32_t view, uint32_t n) { for(uint32_t i=0;i<n;++i) step(view, frame ? 41000u+frame : 0u); };
        frames(TGPT_VIEW_NONE, 24); shots.push_back({"the lit picture", readback()});
        frames(TGPT_VIEW_ALBEDO, 1); shots.push_back({"the first hits' colour", readback()});
        frames(TGPT_VIEW_DIRECT, 3); shots.push_back({"the direct light", readback()});
        ommTrial=0;
        return shots;
    };
    // The meshes are converted and built over the first frames; the group is made, the character built.
    pose(stands);
    for(uint32_t i=0;i<40;++i) { step(TGPT_VIEW_NONE, i ? 40000u+i : 0u); context->Flush(); Sleep(10); }
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
    const auto marked=sequence(0);
    auto against=[&](const std::vector<Shot>& other) {
        std::vector<Off> off;
        if(other.size()==marked.size()) for(size_t i=0;i<marked.size();++i) off.push_back(differ(marked[i].px, other[i].px));
        return off;
    };
    const auto whole=sequence(TGPT_OMM_TRIAL_PLAIN_TESTED);
    const std::vector<Off> offWhole=against(whole), offAgain=against(sequence(0));
    const auto solid=sequence(TGPT_OMM_TRIAL_PLANTS_SOLID|TGPT_OMM_TRIAL_CHARACTERS_SOLID);
    const std::vector<Off> offSolid=against(solid);
    const size_t slack=size_t(W)*H/10000;
    const bool took=!marked.empty() && offWhole.size()==marked.size() && offAgain.size()==marked.size() && offSolid.size()==marked.size();
    expect(took, "the four runs took the same pictures");
    bool same=took; size_t controlSaw=0;
    for(size_t i=0;took && i<marked.size();++i) {
        std::printf("      %-24s differs from the marked run in pixels: every triangle tested %zu; marked again %zu; cut-outs solid %zu (most %.4f; %.4f; %.4f)\n",
                    marked[i].what.c_str(), offWhole[i].pixels, offAgain[i].pixels, offSolid[i].pixels, offWhole[i].most, offAgain[i].most, offSolid[i].most);
        if(offWhole[i].pixels>offAgain[i].pixels+slack) same=false;
        controlSaw=std::max(controlSaw, offSolid[i].pixels);
    }
    expect(same, "with a character's and a plant group's plain submeshes solid in their structures every picture is the one with every triangle tested (no more off than two runs of the same are)");
    expect(controlSaw>500, "the comparison sees cut-outs that turned solid (the lid and the invisible leaves stop the sun)");
    if(took) {
        save(marked[0].px, "omm-marked.png", true); save(whole[0].px, "omm-every-triangle-tested.png", true); save(solid[0].px, "omm-cut-outs-solid.png", true);
        save(marked[2].px, "omm-marked-direct.png", true); save(solid[2].px, "omm-cut-outs-solid-direct.png", true);
    }
    // What the tracer counts (its log's "PT cut-outs" line, every 10 s): 21 s as it ships, 21 s with every triangle tested,
    // so that a whole 10 s window of the log lies inside each.
    for(uint32_t round=0;round<2 && failures==failuresBefore;++round) {
        ommTrial=round ? TGPT_OMM_TRIAL_PLAIN_TESTED : 0u; frame=0;
        const auto began=std::chrono::steady_clock::now();
        uint32_t i=0;
        for(;std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<21.0;++i) { step(TGPT_VIEW_NONE, i ? 42000u+round*100000u+i : 0u); context->Flush(); }
        std::ifstream in(root/"TGPathTracing"/"TGPathTracing.Native.log", std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t at=text.rfind("PT cut-outs: ");
        const std::string line=at==std::string::npos ? std::string() : text.substr(at, text.find('\n', at)==std::string::npos ? std::string::npos : text.find('\n', at)-at);
        std::printf("      log (%s, %u frames): %.1500s\n", round ? "every triangle tested" : "as it ships", i, line.empty() ? "(no \"PT cut-outs\" line yet)" : line.c_str());
        const auto holds=[&](const char* part) { return line.find(part)!=std::string::npos; };
        expect(holds("plants in the wind's pose: 36 in 2 instances") && holds("characters: 2 in 1 instances"), "the log counts the plants' and the character's cut-out triangles");
        if(round==0) expect(holds("(42 more are spared it") && holds("Micromaps trial: 0 (as it ships)"), "as it ships, the 42 plain triangles of the trunks and of the character's sides are spared the shader's test");
        else expect(holds("(0 more are spared it") && holds("A MEASURING STEP"), "with the trial's 16 they go to the shader's test again, and the log says it is a measuring step");
    }
    ommTrial=0;
    TGPT_PlantWindFrame none{}; SetPlantWind(&none, nullptr, 0); SetSkinned(0, 0, nullptr, 0);
    SetMaterials(0, materials.data(), uint32_t(materials.size()));
    SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()));
    if(failures) { std::printf("FAILED %d checks\n", failures); return 1; }
    std::printf("cut-out structure checks passed\n");
    return 0;
}
