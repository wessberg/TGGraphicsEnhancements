// SPDX-License-Identifier: MIT
// TGPT_TEST_PLANT_SHIFT_ONLY=1 (PT-125, docs/agents/fixt.md; included in tgpt_gpu_test right after its warm-up, and run
// alone: the whole test does not take it): a group of plants in the wind keeps its pose and its picture when another
// group comes and goes BEFORE it in the frame's order.
//   The fault it holds (pt_sway.hlsl, steps 4 and 5): each vertex of a group keeps the number of its range (which submesh
//   of which member mesh it is of), written once when the group is made and read by every later frame's pose. The member
//   and range records of the groups in use lie one after another in buffers laid out anew every frame, in the order of
//   the groups' cells. Until 2026-10-12 the number kept was the range's place in THAT FRAME'S buffer: as soon as a group
//   before it came or went (a walk), a group's vertices were posed from other groups' records: plants thrown about or
//   gone, and, where the records read were left over from frames before and named meshes let go, a reset of the GPU.
//   The scene: a floor; group B, six tufts of grass and three bushes (a stem and a crown: two ranges each, twelve ranges
//   in all) in the cell of x and z 0 to 16, in front of the camera; group A, a bush and a tuft (three ranges) in the
//   cell before it in the order (z -16 to 0), BEHIND the camera: no camera ray meets it. The wind's clock runs, so every
//   group in use is posed again every frame (a pose is kept while the wind and the camera stand still: a still wind
//   would hide the fault). With the tracer's own camera rays and no denoiser.
//   Four runs of the same 48 frames, the same frame numbers and wind times, each from groups made anew:
//     the yardstick   B alone, the whole time; and once more (what two runs of the same frames differ by)
//     the shift       B alone for 16 frames (B is made, its vertices' numbers written); then A is there for 16 (B's
//                     records now start after A's); then A is gone again for 16
//     the walk        A and B there from the first frame (B made BEHIND A); after 16 frames A goes for good
//   Judged: the first hits' colour (TGPT_VIEW_ALBEDO: geometry alone, no light) at frame 30 and at frame 46, pixel by
//   pixel against the yardstick's at the same frame: no more pixels off than the yardstick's two runs are, plus a
//   hundredth of a percent of the picture. And a bound that needs no yardstick: every one of B's nine plants shows where
//   the scene put it (pixels that are not the bare floor's around its middle on the screen).
//   What the tracer SAYS (its log, with the test's switch TGPT_TEST_SCENE_ORDER: scene.cpp pose_plants) is checked too:
//   that A's group stood before B's, that B's ranges started at 0, then at 3, then at 0, and that B was the same group
//   throughout (its serial): a group made anew would write its numbers anew and show nothing.
//   Controls: the two shots of the yardstick differ (the wind did move B between them, so the pose is made each frame),
//   and a run with one bush 0.3 m off differs (the comparison sees a plant that moved).
//   THE MODE ENDS AT ITS FIRST FAILED STAGE. On a tracer with the fault (the mutant: pt_sway.hlsl's old number) that is
//   "A appeared": its later stages would have the pose read PAST the frame's records, which is what reset the GPU in the
//   game, and are never reached.
{
    std::printf("\nA group of plants keeps its pose when another group comes and goes before it in the frame's order (PT-125)\n");
    SetEnvironmentVariableA("TGPT_TEST_SCENE_ORDER", "1");
    const fs::path shiftLog=root/"TGPathTracing"/"TGPathTracing.Native.log";
    // The tracer's log from where this mode starts: the whole lines added since the last call that hold the marker.
    std::uintmax_t shiftLogAt=0;
    { std::error_code sized; const auto size=fs::file_size(shiftLog, sized); if(!sized) shiftLogAt=size; }
    auto new_lines=[&](const char* marker) {
        std::vector<std::string> found;
        std::ifstream in(shiftLog, std::ios::binary);
        if(!in) return found;
        in.seekg(std::streamoff(shiftLogAt));
        std::string rest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t whole=rest.rfind('\n');
        if(whole==std::string::npos) return found;
        shiftLogAt+=whole+1;
        size_t at=0;
        while(at<=whole) {
            const size_t end=rest.find('\n', at);
            const std::string line=rest.substr(at, end-at);
            if(line.find(marker)!=std::string::npos) found.push_back(line);
            at=end+1;
        }
        return found;
    };
    // The groups of a frame as the tracer said them, in the frame's order.
    struct Said { double x=0, z=0; unsigned long long serial=0; unsigned memberFirst=0, members=0, rangeFirst=0, ranges=0; };
    std::vector<Said> order; std::string orderLine;
    auto read_order=[&]() {
        for(const std::string& line:new_lines("PT test, the plants' groups in this frame's order:")) {
            orderLine=line.substr(line.find("PT test")); order.clear();
            for(size_t at=line.find(" | "); at!=std::string::npos; at=line.find(" | ", at+3)) {
                Said s;
                if(std::sscanf(line.c_str()+at, " | x %lf z %lf serial %llu members %u+%u ranges %u+%u", &s.x, &s.z, &s.serial, &s.memberFirst, &s.members, &s.rangeFirst, &s.ranges)==7) order.push_back(s);
            }
        }
    };
    const bool savedOwn=ownRays; ownRays=true;
    const auto savedObjects=objects;
    const uint32_t raw=TGPT_OPTION_NO_DENOISER;
    // The materials, after the test's own: a red stem, a blue crown, green grass; all plain (no cut-out, no micromaps) and
    // drawn from both sides, as the game's plants are (a card of a one-sided material is not met by a camera ray from
    // behind, pt_primary: a tuft turned away showed as a blade three pixels wide, and its shadow whole).
    auto all=materials;
    const uint32_t stem=uint32_t(all.size()), crown=stem+1, grass=stem+2;
    auto blue=materials[0]; blue.baseColor[0]=0.1f; blue.baseColor[1]=0.2f; blue.baseColor[2]=0.8f;
    all.push_back(materials[1]); all.push_back(blue); all.push_back(materials[2]);
    for(uint32_t m=stem;m<=grass;++m) all[m].flags|=TGPT_MATERIAL_DOUBLE_SIDED;
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
    // A bush 1.3 m high: two stem cards of six rows, a crown of eight cards of twelve rows. A tuft of grass 0.7 m high:
    // five cards of eight rows. (The megaplants check's plants.)
    const float bushHeight=1.3f, tuftHeight=0.7f;
    std::vector<Vertex> bush; std::vector<uint16_t> stemIndices, crownIndices;
    cards(bush, stemIndices, 2, 0.f, 0.12f, 0.f, 0.8f, 6);
    cards(bush, crownIndices, 8, 0.3f, 0.9f, 0.35f, bushHeight, 12);
    std::vector<Vertex> tuft; std::vector<uint16_t> tuftIndices;
    cards(tuft, tuftIndices, 5, 0.2f, 0.35f, 0.f, tuftHeight, 8);
    MeshBuffers shiftBush=make_mesh(bush, {stemIndices, crownIndices}), shiftTuft=make_mesh(tuft, {tuftIndices});
    ReadStatus(&status);
    const uint32_t readyBefore=status.meshesReady;
    const int bushHandle=register_mesh(shiftBush, 0x50543125a1ull), tuftHandle=register_mesh(shiftTuft, 0x50543125a2ull);
    expect(bushHandle>0 && tuftHandle>0, "a bush and a tuft of grass registered");
    std::vector<uint8_t> noise(64*64);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) noise[size_t(y)*64+size_t(x)]=uint8_t(std::lround(255.f*(0.6f+0.4f*std::sin(float(x)*0.31f+1.3f)*std::cos(float(y)*0.23f+0.7f))));
    expect(SetPlantWind!=nullptr && SetWindNoise!=nullptr && SetWindNoise(11, 64, 64, 0, noise.data())==0, "the wind's noise map");
    TGPT_PlantWind leafWind{}; leafWind.material=crown; leafWind.kind=TGPT_PLANT_LEAVES; leafWind.noise=11; leafWind.flags=TGPT_PLANT_BEND;
    leafWind.strength=0.9f; leafWind.bendHeight=1.f; leafWind.pivotOffsetY=-0.5f; leafWind.fade[0]=0.f; leafWind.fade[1]=150.f;
    TGPT_PlantWind stemWind=leafWind; stemWind.material=stem; stemWind.bendHeight=2.f; stemWind.pivotOffsetY=0.f;
    TGPT_PlantWind grassWind{}; grassWind.material=grass; grassWind.kind=TGPT_PLANT_GRASS; grassWind.noise=11; grassWind.strength=80.f;
    const std::vector<TGPT_PlantWind> winds={leafWind, stemWind, grassWind};
    // The camera: over the edge of B's cell, looking down at its plants; no sky in the picture.
    const Camera sc=make_camera({8, 6, -1}, {8, 0.4f, 6}, 55.f, float(W)/float(H));
    const V3 scForward=normalize(V3{8, 0.4f, 6}-V3{8, 6, -1});
    // The floor alone in the G-buffer (the camera's own rays do not read it), and in the traced scene.
    objects={ {0, translate_scale({8, 0, 8}, {120, 1, 120}), {0}} };
    struct Plant { V3 at; float scale, turn; bool isBush; };
    std::vector<Plant> groupB, groupA;
    for(int i=0;i<6;++i) groupB.push_back({{5.2f+1.1f*float(i), 0, 5.5f+0.9f*float((i*3)%4)}, 0.9f+0.05f*float(i%4), 0.9f*float(i), false});
    // (The bushes stand behind the tufts, so that each plant is clear of the others on the screen: 53 to 80 pixels a metre.)
    for(int j=0;j<3;++j) groupB.push_back({{5.6f+2.4f*float(j), 0, 10.6f+0.5f*float(j)}, 1.f+0.1f*float(j), 0.37f*float(j), true});
    groupA.push_back({{7.f, 0, -8.f}, 1.f, 0.4f, true});
    groupA.push_back({{9.f, 0, -6.5f}, 1.f, 1.9f, false});
    // The scene: the floor; B (plants: false, the floor alone); A where asked. nudge: B's second bush that far off in x.
    auto scene_with=[&](bool plants, bool withA, float nudge) {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> ids;
        { TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=objects[0].world.m[r][k];
          q.mesh=quadHandle; q.materialBase=uint32_t(ids.size()); q.mask=0xFF; ids.push_back(0u); list.push_back(q); }
        auto plant=[&](const Plant& p, float dx) {
            const M4 world=mul(translate_scale({p.at.x+dx, p.at.y, p.at.z}, {p.scale, p.scale, p.scale}), rot_y(p.turn));
            TGPT_Instance q{}; for(int r=0;r<3;++r) for(int k=0;k<4;++k) q.transform[r*4+k]=world.m[r][k];
            q.mesh=p.isBush ? bushHandle : tuftHandle; q.materialBase=uint32_t(ids.size()); q.mask=0xFF;
            if(p.isBush) { ids.push_back(stem); ids.push_back(crown); } else ids.push_back(grass);
            list.push_back(q);
        };
        if(plants) for(size_t i=0;i<groupB.size();++i) plant(groupB[i], i==7 ? nudge : 0.f);
        if(plants && withA) for(const Plant& p:groupA) plant(p, 0.f);
        expect(SetScene(list.data(), uint32_t(list.size()), ids.data(), uint32_t(ids.size()))==0, "the floor with its plants");
    };
    uint32_t frame=0;
    auto step=[&](uint32_t view) {
        TGPT_PlantWindFrame wf{}; wf.time=3.f+0.04f*float(frame); wf.speed=30.f; wf.direction=40.f;
        wf.forward[0]=scForward.x; wf.forward[1]=scForward.y; wf.forward[2]=scForward.z;
        SetPlantWind(&wf, winds.data(), uint32_t(winds.size()));
        run(view, sc, sc, frame ? 70000u+frame : 0u, raw); ++frame;
    };
    auto healthy=[&]() { ReadStatus(&status); return !status.disabled && status.lastError==0; };
    // The two meshes are read back and built over the first frames (the floor alone meanwhile).
    scene_with(false, false, 0.f);
    for(uint32_t i=0;i<300;++i) {
        run(TGPT_VIEW_NONE, sc, sc, 69000u+i, raw);
        ReadStatus(&status); if(status.meshesReady>=readyBefore+2) break;
        context->Flush(); Sleep(5);
    }
    expect(status.meshesReady>=readyBefore+2, "the bush and the tuft were read back and built");
    const uint64_t failedBefore=status.framesFailed;
    // The bare floor's first hits: what a pixel shows where no plant stands.
    std::vector<float> bare;
    { for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_ALBEDO, sc, sc, 69500u+i, raw); bare=readback(); }
    // A pixel differs where a channel is off by more than a thousandth and 2 % of itself.
    auto differ=[&](const std::vector<float>& a, const std::vector<float>& b) {
        size_t pixels=0;
        if(a.size()!=b.size() || a.empty()) return size_t(W)*H;
        for(size_t i=0;i+2<a.size();i+=3) {
            bool differs=false;
            for(int k=0;k<3;++k) {
                const double d=std::fabs(double(a[i+k])-double(b[i+k]));
                if(!(d<=1e30) || d>1e-3+0.02*std::fabs(double(a[i+k]))) differs=true;
            }
            if(differs) ++pixels;
        }
        return pixels;
    };
    // A plant's pixels: not the bare floor's colour (a channel off by more than 0.05).
    auto is_plant=[&](const std::vector<float>& px, size_t pixel) {
        for(int k=0;k<3;++k) if(!(std::fabs(px[pixel*3+k]-bare[pixel*3+k])<=0.05f)) return true;
        return false;
    };
    // How many of B's nine plants show where the scene put them: plant pixels within 16 pixels of the plant's middle on
    // the screen (its place, half its height up), ten at least. least: the fewest any plant has.
    auto standing=[&](const std::vector<float>& px, size_t& least) {
        uint32_t shown=0; least=size_t(-1);
        if(px.size()!=size_t(W)*H*3) { least=0; return shown; }
        for(const Plant& p:groupB) {
            const V3 mid{p.at.x, 0.5f*(p.isBush ? bushHeight : tuftHeight)*p.scale, p.at.z};
            const float v[4]={mid.x-sc.position.x, mid.y-sc.position.y, mid.z-sc.position.z, 1.f}; float c[4]{};
            for(int r=0;r<4;++r) for(int k=0;k<4;++k) c[r]+=sc.viewProj.m[r][k]*v[k];
            const int cx=int(std::lround((c[0]/c[3]+1.f)*0.5f*float(W)-0.5f)), cy=int(std::lround((1.f-c[1]/c[3])*0.5f*float(H)-0.5f));
            size_t count=0;
            for(int y=cy-16;y<=cy+16;++y) for(int x=cx-16;x<=cx+16;++x)
                if(x>=0 && y>=0 && x<int(W) && y<int(H) && is_plant(px, size_t(y)*W+size_t(x))) ++count;
            least=std::min(least, count);
            if(count>=10) ++shown;
        }
        return shown;
    };
    auto plant_pixels=[&](const std::vector<float>& px) { size_t n=0; for(size_t i=0;i<px.size()/3;++i) n+=is_plant(px, i); return n; };
    // The run's three stages of 16 frames; a stage's last two frames are the first hits' colour and the lit picture.
    const uint32_t kStage=16;
    struct Stage { std::vector<float> albedo, lit; std::vector<Said> order; std::string line; };
    unsigned long long serialFloor=0;
    auto begin_run=[&]() {
        // Every group goes: no wind and no plant for a few frames. The next run's groups are made anew.
        TGPT_PlantWindFrame none{}; SetPlantWind(&none, nullptr, 0);
        scene_with(false, false, 0.f);
        for(uint32_t i=0;i<4;++i) { run(TGPT_VIEW_NONE, sc, sc, 69800u+i, raw); context->Flush(); }
        read_order();
        for(const Said& s:order) serialFloor=std::max(serialFloor, s.serial);
        order.clear(); orderLine.clear();
        frame=0;
    };
    auto stage=[&](bool withA, float nudge) {
        Stage s;
        scene_with(true, withA, nudge);
        for(uint32_t i=0;i+2<kStage;++i) step(TGPT_VIEW_NONE);
        step(TGPT_VIEW_ALBEDO); s.albedo=readback();
        step(TGPT_VIEW_NONE); s.lit=readback();
        read_order(); s.order=order; s.line=orderLine;
        return s;
    };
    // The group in the cell of x and z 0 to 16 (B) and the one before it (A), in what a stage's last order said.
    auto group_at=[&](const Stage& s, double x, double z)->const Said* { for(const Said& g:s.order) if(g.x==x && g.z==z) return &g; return nullptr; };
    const size_t slack=size_t(W)*H/10000;

    // The yardstick: B alone.
    begin_run();
    const unsigned long long serialBeforeYardstick=serialFloor;
    const Stage y0=stage(false, 0.f), y1=stage(false, 0.f), y2=stage(false, 0.f);
    begin_run();
    const Stage again0=stage(false, 0.f), again1=stage(false, 0.f), again2=stage(false, 0.f);
    (void)y0; (void)again0;
    const size_t noise1=differ(y1.albedo, again1.albedo), noise2=differ(y2.albedo, again2.albedo);
    size_t leastY1=0, leastY2=0;
    const uint32_t shownY1=standing(y1.albedo, leastY1), shownY2=standing(y2.albedo, leastY2);
    const size_t plantsY1=plant_pixels(y1.albedo), windMoved=differ(y1.albedo, y2.albedo);
    std::printf("      the yardstick (B alone): %zu pixels show a plant at frame %u; %u of B's %zu plants show where they stand (the fewest plant pixels about one: %zu), "
                "%u at frame %u (%zu)\n      two runs of the same frames differ in %zu and %zu pixels; the wind moved %zu pixels between the two shots\n      native log: %s\n",
                plantsY1, kStage*2-2, shownY1, groupB.size(), leastY1, shownY2, kStage*3-2, leastY2, noise1, noise2, windMoved, y2.line.c_str());
    save(y1.albedo, "125-shift-yardstick-first-hits.png", false); save(y1.lit, "125-shift-yardstick-lit.png", true);
    expect(healthy(), "the device stands after the yardstick's runs");
    if(!healthy()) { std::printf("FAILED: the device was lost (last error 0x%08X): the run ends here\n", unsigned(status.lastError)); return 1; }
    expect(plantsY1>5000, "the camera sees B's plants");
    expect(shownY1==groupB.size() && shownY2==groupB.size(), "alone, every plant of B shows where the scene put it");
    expect(noise1<=slack && noise2<=slack, "two runs of the same frames show the same first hits");
    expect(windMoved>200, "the wind moves B between the two shots (its pose is made anew every frame)");
    { const Said* b=group_at(y2, 0, 0);
      expect(y2.order.size()==1 && b && b->rangeFirst==0 && b->ranges==12 && b->members==9 && b->serial>serialBeforeYardstick,
             "the tracer says: one group, B, its nine members' twelve ranges from the buffer's start"); }
    if(failures) { std::printf("FAILED %d checks: the yardstick itself is not in order, nothing after it would mean anything\n", failures); return 1; }
    // (TGPT_TEST_PLANT_SHIFT_LOOK=1, for a look, never judged: each of B's plants from 2.5 m, its first hits and the lit
    // picture, at the wind's time of frame 30; saved as 125-look-<plant>-first-hits.png and -lit.png. Grass is posed
    // by the wind alone; a bush's leaves also by where the camera stands, so its close-up is not its pose in the shots.)
    if(GetEnvironmentVariableA("TGPT_TEST_PLANT_SHIFT_LOOK", nullptr, 0)>0) {
        for(size_t i=0;i<groupB.size();++i) {
            const Plant& p=groupB[i];
            const V3 mid{p.at.x, 0.5f*(p.isBush ? bushHeight : tuftHeight)*p.scale, p.at.z};
            const Camera look=make_camera(mid+V3{-0.9f, 1.3f, -1.9f}, mid, 40.f, float(W)/float(H));
            TGPT_PlantWindFrame wf{}; wf.time=3.f+0.04f*float(kStage*2-2); wf.speed=30.f; wf.direction=40.f;
            wf.forward[0]=scForward.x; wf.forward[1]=scForward.y; wf.forward[2]=scForward.z;
            SetPlantWind(&wf, winds.data(), uint32_t(winds.size()));
            run(TGPT_VIEW_ALBEDO, look, look, 69600u+uint32_t(i)*8u, raw);
            save(readback(), ("125-look-"+std::to_string(i)+"-first-hits.png").c_str(), false);
            for(uint32_t f=1;f<8;++f) run(TGPT_VIEW_NONE, look, look, 69600u+uint32_t(i)*8u+f, raw);
            save(readback(), ("125-look-"+std::to_string(i)+"-lit.png").c_str(), true);
        }
        std::printf("      (TGPT_TEST_PLANT_SHIFT_LOOK: %zu close-ups of B's plants saved)\n", groupB.size());
    }

    // The shift: B alone, then A before it, then alone again.
    begin_run();
    const Stage s0=stage(false, 0.f);
    const Said* made=group_at(s0, 0, 0);
    const unsigned long long serialB=made ? made->serial : 0;
    expect(s0.order.size()==1 && made && made->rangeFirst==0 && made->serial>serialFloor, "B is made alone: its ranges start at 0");
    const Stage s1=stage(true, 0.f);
    {
        const Said* a=group_at(s1, 0, -16), *b=group_at(s1, 0, 0);
        std::printf("      A appeared. native log: %s\n", s1.line.c_str());
        expect(s1.order.size()==2 && a && b && a->rangeFirst==0 && a->ranges==3 && a->members==2, "the tracer says: A's group is there, FIRST in the frame's order");
        expect(b && b->rangeFirst==3 && b->memberFirst==2 && b->ranges==12, "and B's records now start after A's (its ranges at 3, its members at 2)");
        expect(b && serialB && b->serial==serialB, "and B is the group that was made alone (not made anew: its vertices keep the numbers written then)");
        size_t least=0;
        const uint32_t shown=standing(s1.albedo, least);
        const size_t off=differ(y1.albedo, s1.albedo);
        std::printf("      with A before it: %zu pixels of B's first hits differ from the yardstick's at the same frame (two runs of the yardstick: %zu); %u of B's %zu plants "
                    "show where they stand (the fewest plant pixels about one: %zu); %zu pixels show a plant (the yardstick: %zu)\n",
                    off, noise1, shown, groupB.size(), least, plant_pixels(s1.albedo), plantsY1);
        save(s1.albedo, "125-shift-a-before-first-hits.png", false); save(s1.lit, "125-shift-a-before-lit.png", true);
        expect(healthy(), "the device stands with A before B");
        expect(off<=noise1+slack, "with another group before it in the frame's order, B's first hits are what they were alone (its pose kept)");
        expect(shown==groupB.size(), "and every plant of B still shows where the scene put it");
    }
    if(failures) {
        std::printf("FAILED %d checks: a group's pose changed when another group came before it in the frame's order (PT-125's fault). The run ends here: the later stages "
                    "would have the pose read past the frame's records.\n", failures);
        return 1;
    }
    const Stage s2=stage(false, 0.f);
    {
        const Said* b=group_at(s2, 0, 0);
        std::printf("      A went. native log: %s\n", s2.line.c_str());
        expect(s2.order.size()==1 && b && b->rangeFirst==0 && b->serial==serialB, "the tracer says: B alone again, the same group, its ranges back at 0");
        size_t least=0;
        const uint32_t shown=standing(s2.albedo, least);
        const size_t off=differ(y2.albedo, s2.albedo);
        std::printf("      after A went: %zu pixels differ from the yardstick's (two runs of the yardstick: %zu); %u of B's %zu plants show where they stand (%zu)\n",
                    off, noise2, shown, groupB.size(), least);
        expect(healthy(), "the device stands after A went");
        expect(off<=noise2+slack, "after the group before it went again, B's first hits are what they were alone");
        expect(shown==groupB.size(), "and every plant of B still shows where the scene put it");
    }
    if(failures) { std::printf("FAILED %d checks: a group's pose changed when the group before it went. The run ends here.\n", failures); return 1; }
    for(const Said& s:s2.order) serialFloor=std::max(serialFloor, s.serial);
    for(const Said& s:s1.order) serialFloor=std::max(serialFloor, s.serial);

    // The walk: B made BEHIND A (its ranges at 3 when its numbers are written), then A goes for good. This is the order
    // that reset the GPU in the game: with the old number B's pose then read past the frame's records.
    begin_run();
    const Stage w0=stage(true, 0.f);
    const Said* madeBehind=group_at(w0, 0, 0);
    const unsigned long long serialBehind=madeBehind ? madeBehind->serial : 0;
    std::printf("      B made behind A. native log: %s\n", w0.line.c_str());
    expect(w0.order.size()==2 && group_at(w0, 0, -16) && madeBehind && madeBehind->rangeFirst==3 && madeBehind->serial>serialFloor,
           "the tracer says: B is made with A before it, its ranges at 3");
    const Stage w1=stage(false, 0.f), w2=stage(false, 0.f);
    {
        const Said* b=group_at(w1, 0, 0);
        std::printf("      A went. native log: %s\n", w1.line.c_str());
        expect(w1.order.size()==1 && b && b->rangeFirst==0 && serialBehind && b->serial==serialBehind, "the tracer says: A gone, B the same group, its ranges now at 0");
        size_t least1=0, least2=0;
        const uint32_t shown1=standing(w1.albedo, least1), shown2=standing(w2.albedo, least2);
        const size_t off1=differ(y1.albedo, w1.albedo), off2=differ(y2.albedo, w2.albedo);
        std::printf("      a group made behind another that then went: %zu and %zu pixels differ from the yardstick's at the same frames (two runs of the yardstick: %zu, %zu); "
                    "%u and %u of B's %zu plants show where they stand (%zu, %zu)\n", off1, off2, noise1, noise2, shown1, shown2, groupB.size(), least1, least2);
        save(w1.albedo, "125-shift-walk-first-hits.png", false);
        expect(healthy(), "the device stands after the group before B went");
        expect(off1<=noise1+slack && off2<=noise2+slack, "a group made behind another keeps its pose when that one goes (the walk that reset the GPU)");
        expect(shown1==groupB.size() && shown2==groupB.size(), "and every plant of it shows where the scene put it");
    }
    // The control: B alone with one bush 0.3 m off.
    begin_run();
    stage(false, 0.3f);
    const Stage c1=stage(false, 0.3f);
    const size_t controlSaw=differ(y1.albedo, c1.albedo);
    std::printf("      the control (one bush of B 0.3 m off): %zu pixels differ\n", controlSaw);
    expect(controlSaw>200, "the comparison sees a bush that stands 0.3 m off");
    ReadStatus(&status);
    std::printf("      frames traced %llu, failed %llu since the mode began, last error 0x%08X\n", (unsigned long long)status.framesTraced,
                (unsigned long long)(status.framesFailed-failedBefore), unsigned(status.lastError));
    expect(!status.disabled && status.framesFailed==failedBefore && status.lastError==0, "no frame failed and the device stands");
    { TGPT_PlantWindFrame none{}; SetPlantWind(&none, nullptr, 0); }
    objects=savedObjects; ownRays=savedOwn;
    SetMaterials(0, materials.data(), uint32_t(materials.size()));
    SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()));
    if(failures) { std::printf("FAILED %d checks\n", failures); return 1; }
    std::printf("plant shift checks passed\n");
    return 0;
}
