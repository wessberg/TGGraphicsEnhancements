// SPDX-License-Identifier: MIT
// PT-85 (docs/agents/reuse-shift.md): what a pass reuses of the frame before is fetched where the pixel's point was in
// THAT frame's pixel grid. Alone with TGPT_TEST_REUSE_ONLY=1 (after the warm-up and the edges check; some minutes).
//   ON TRIAL: the fix is behind TGPT_Frame.jitter's z (the plugin's hidden setting "Reuse follows camera shift", 0 by
//   default), and this check measures the same scene with the switch off (the tracer as it was), on (1: the tracer's own
//   histories follow the shifts) and at 2 (ReSTIR's reuse as well), beside the control without any shift.
//   Since PT-84 the camera's rays carry HDRP's sub-pixel shift, another one every frame, so the same pixel looks at
//   another point in each frame of a still world. A pass that fetches the frame before's data at "the pixel's place
//   less its motion" then lands up to a render pixel beside the point: on a thin thing, an edge, a shadow line that is
//   another surface, its history is refused, and the raw sample shows for a frame. In the game the Direct layer went
//   from 6 specks a frame to 90 with the shift (PT-85).
//   The scene, still, the tracer's own camera rays, the game's ReSTIR histories (4 frames direct, 8 bounced) and its own
//   denoiser for the debug layers: a rough floor; a glossy ball and a glossy table top; thin things, each kind and width
//   its own colour so that the albedo view tells them apart: a fan of flat bars and a row of upright slats, each half a
//   pixel, one, two and four pixels wide; a lattice of one-pixel bars; a row of square posts (two faces, one of them
//   under a pixel wide). Four lamps near and, as in the game's tavern, most lamps far (eight that light the scene from
//   another cell of the light grid and 200 behind a wall: one candidate a frame of 208, so a raw sample is rare and bright).
//   Sixteen shifts (a pixel's quarters) in turn, six rounds of them after two. A frame is compared with the frames of the
//   SAME shift only (one round apart): under the same shift every pixel looks at the very same point, so nothing of
//   what differs is a thin thing drawn by another pixel (in the game the upscaler resolves that; here nothing does): it
//   is the light's own noise. The control, the shift held at zero, is counted the very same way. In 8-bit levels of
//   the layer as a picture (one scale for every run, the Direct layer's mean at level 130 as in the game):
//     specks: pixels 40 levels over their usual (the median of their shift's six frames);
//     jumps: pixels more than 20 levels from what they were a round before;
//     change: the mean step from a round to the next;
//   for the whole picture, split into the pixels where the shift changes which surface a pixel or a neighbour shows
//   ("edges") and the rest ("inside"), and for each kind of thing by what the pixel shows under that frame's shift.
//   And whether the denoised light itself follows the shift: the sixteen shifts' frames laid out 4 x 4 to a pixel, as an
//   ideal upscaler would, the step between two samples across a pixel's border against the step inside a pixel (on
//   "inside" pixels: shadows and shading only). A light that stays on the pixel grid while the surfaces move shows
//   steps at the borders alone. detail: how sharp the light's own picture is (the frames' mean, its step from an inside
//   pixel to the next).
//   And with the camera moving 0.4 pixels a frame, to one side for eight frames and back for eight, so that a round's
//   frames again share camera and shift with the round before's: the same three numbers, over all pixels.
//   Pictures: 85-reuse-<layer>-<shift|still>.png (one frame), ...-specks.png (the frames' mean, specks red),
//   85-reuse-direct-16-frames.png (a quarter of the screen laid out 4 x 4 to a pixel).
{
    std::printf("[reuse] what is reused of the frame before is fetched where the point was in that frame's pixel grid\n");
    const bool keepSun=sunOn, keepOwn=ownRays, keepLate=lateMatrices; const float keepSky=skyIntensity, keepTrial=reuseTrial;
    const uint32_t keepNumbers[5]={history, giHistory, bounces, spatialSamples, rays};
    history=4; giHistory=8; bounces=2; spatialSamples=4; rays=1; sunOn=false; skyIntensity=1e-6f; ownRays=true; lateMatrices=false;
    // The ball.
    std::vector<Vertex> ball; std::vector<uint16_t> ballIndices;
    {
        const int stacks=48, slices=96;
        for(int i=0;i<=stacks;++i) for(int j=0;j<=slices;++j) {
            const float th=pi*float(i)/stacks, ph=2.f*pi*float(j)/slices;
            const float x=std::sin(th)*std::cos(ph), y=std::cos(th), z=std::sin(th)*std::sin(ph);
            ball.push_back({x*0.5f, y*0.5f, z*0.5f, x, y, z, float(j)/slices, float(i)/stacks});
        }
        for(int i=0;i<stacks;++i) for(int j=0;j<slices;++j) { // wound as Unity winds them: cross(v1 - v0, v2 - v0) out of the ball
            const uint16_t a=uint16_t(i*(slices+1)+j), b=uint16_t(a+slices+1);
            ballIndices.insert(ballIndices.end(), {a, uint16_t(a+1), b, uint16_t(a+1), uint16_t(b+1), b});
        }
    }
    MeshBuffers reuseBallMesh=make_mesh(ball, {ballIndices});
    const int reuseBall=register_mesh(reuseBallMesh, 0x7E05E);
    expect(reuseBall>0, "the reuse check's ball registered");
    // The kinds of thing, each its own flat colour (the albedo view tells them apart). wide: two pixels and wider.
    struct ReuseKind { const char* name; float r, g, b, smoothness; bool wide; };
    static const ReuseKind kinds[13]={
        {"the floor", 0.5f, 0.5f, 0.5f, 0.2f, true}, {"the ball", 0.10f, 0.25f, 0.22f, 0.75f, true}, {"the table top", 0.30f, 0.18f, 0.08f, 0.8f, true},
        {"flat bar, 0.5 px", 0.8f, 0.1f, 0.1f, 0.3f, false}, {"flat bar, 1 px", 0.8f, 0.3f, 0.1f, 0.3f, false}, {"flat bar, 2 px", 0.8f, 0.5f, 0.1f, 0.3f, true}, {"flat bar, 4 px", 0.8f, 0.7f, 0.1f, 0.3f, true},
        {"lattice, 1 px", 0.1f, 0.6f, 0.1f, 0.3f, false}, {"square post", 0.15f, 0.2f, 0.7f, 0.6f, false},
        {"slat, 0.5 px", 0.6f, 0.1f, 0.3f, 0.5f, false}, {"slat, 1 px", 0.6f, 0.1f, 0.5f, 0.5f, false}, {"slat, 2 px", 0.6f, 0.1f, 0.7f, 0.5f, true}, {"slat, 4 px", 0.6f, 0.1f, 0.9f, 0.5f, true}};
    const int kindCount=13;
    auto reuseMaterials=materials;
    const uint32_t firstKind=uint32_t(reuseMaterials.size());
    for(int k=0;k<kindCount;++k) {
        TGPT_Material m=materials[0]; m.albedoTile=-1; m.colorTexture=m.normalTexture=m.maskTexture=m.emissiveTexture=-1;
        m.baseColor[0]=kinds[k].r; m.baseColor[1]=kinds[k].g; m.baseColor[2]=kinds[k].b; m.smoothness=kinds[k].smoothness; m.metallic=0.f;
        reuseMaterials.push_back(m);
    }
    uint32_t reuseInstances=0;
    {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> index;
        auto put=[&](int mesh, const M4& world, std::initializer_list<uint32_t> its) {
            TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
            in.mesh=mesh; in.materialBase=uint32_t(index.size()); in.mask=0xFF;
            for(uint32_t m:its) index.push_back(firstKind+m);
            list.push_back(in);
        };
        // A flat piece facing up: its size, a turn about the vertical, its place.
        auto flatPiece=[&](V3 at, float angle, float sx, float sz, uint32_t kind) {
            const float cs=std::cos(angle), sn=std::sin(angle);
            M4 m=identity();
            m.m[0][0]=cs*sx; m.m[0][2]=sn*sz; m.m[0][3]=at.x; m.m[1][3]=at.y; m.m[2][0]=-sn*sx; m.m[2][2]=cs*sz; m.m[2][3]=at.z;
            put(cardHandle, m, {kind});
        };
        put(quadHandle, translate_scale({0, 0, -4}, {80, 1, 80}), {0});
        put(quadHandle, mul(translate_scale({-12, 5, -15}, {1, 10, 60}), rot_z(-pi/2)), {0}); // a wall facing +x: 200 lamps stand behind it
        put(reuseBall, translate_scale({2.5f, 0.5f, -3.7f}, {1, 1, 1}), {1});
        flatPiece({1.0f, 0.45f, -4.9f}, 0.25f, 1.2f, 0.8f, 2);
        // The fan: flat bars 2.2, 4.5, 9 and 18 mm wide (a pixel is some 4.5 mm there), each at two angles, 1 m up.
        const float widths[4]={0.0022f, 0.0045f, 0.009f, 0.018f};
        for(int k=0;k<8;++k) flatPiece({0.35f+0.24f*float(k), 1.0f, -4.0f+0.05f*float(k%3)}, (k%2 ? -0.5f : 0.3f)+0.07f*float(k), widths[k/2], 1.6f, uint32_t(3+k/2));
        // The lattice: seven bars each way, a pixel wide, 80 cm up.
        for(int k=0;k<7;++k) {
            const float o=-0.36f+0.12f*float(k), turn=0.2f, cs=std::cos(turn), sn=std::sin(turn);
            flatPiece({2.3f+cs*o, 0.8f, -5.1f-sn*o}, turn, 0.0045f, 0.9f, 7);
            flatPiece({2.3f+sn*o, 0.8f, -5.1f+cs*o}, turn+pi/2, 0.0045f, 0.9f, 7);
        }
        // The posts: 11 mm square, 90 cm tall (the face towards the far lamps is under a pixel wide from here).
        for(int k=0;k<10;++k) put(cubeHandle, translate_scale({0.5f+0.13f*float(k), 0.45f, -3.1f+0.03f*float(k)}, {0.011f, 0.9f, 0.011f}), {8, 8});
        // The slats: upright, facing the camera's side (-z), 1 m tall, 3.7, 7.5, 15 and 30 mm wide (a pixel is some 7.5 mm
        // across them there), three of each.
        const float slatWidths[4]={0.0037f, 0.0075f, 0.015f, 0.030f};
        for(int k=0;k<12;++k) put(cardHandle, mul(translate_scale({4.2f+0.11f*float(k), 0.5f, -2.6f}, {slatWidths[k/3], 1.f, 1.f}), rot_x(-pi/2)), {uint32_t(9+k/3)});
        expect(SetMaterials(0, reuseMaterials.data(), uint32_t(reuseMaterials.size()))==0 &&
               SetScene(list.data(), uint32_t(list.size()), index.data(), uint32_t(index.size()))==0, "the reuse check's scene: a floor, a ball, a table, thin bars, a lattice, posts, slats");
        reuseInstances=uint32_t(list.size());
    }
    {
        std::vector<TGPT_Light> lamps;
        auto lamp=[&](float x, float y, float z, bool distant, uint32_t id) { // (not "far": a macro of windows.h)
            TGPT_Light t{}; t.position[0]=x; t.position[1]=y; t.position[2]=z; t.type=0; t.color[0]=8.f; t.color[1]=5.f; t.color[2]=2.5f;
            t.range=distant ? 0.5f : 12.f; t.radius=0.03f; t.id=id; if(distant) t.flags=TGPT_LIGHT_NO_RANGE;
            lamps.push_back(t);
        };
        // Four in the scene's own cell of the light grid (x 0 to 8 m).
        lamp(1.2f, 2.2f, -4.4f, false, 2000); lamp(2.9f, 1.6f, -5.3f, false, 2001); lamp(0.6f, 1.4f, -3.0f, false, 2002); lamp(3.3f, 2.4f, -2.7f, false, 2003);
        // Eight that fall off with distance alone, 0.85 m and more outside that cell: the far list's.
        static const float at[8][3]={{-1.0f, 1.0f, -5.5f}, {-1.6f, 1.8f, -4.6f}, {-0.9f, 2.6f, -3.6f}, {-2.2f, 1.3f, -3.8f},
                                     {-1.2f, 0.7f, -6.3f}, {-2.4f, 2.3f, -5.6f}, {-0.85f, 1.6f, -6.8f}, {-1.9f, 0.9f, -5.0f}};
        for(uint32_t i=0;i<8;++i) lamp(at[i][0], at[i][1], at[i][2], true, 2010+i);
        for(uint32_t i=0;i<200;++i) lamp(-14.f-0.8f*float(i%20), 1.f+0.6f*float(i/20), -30.f+1.4f*float(i%20), true, 2100+i); // behind the wall
        expect(SetLights(lamps.data(), uint32_t(lamps.size()))==0, "the reuse check's lamps: four near, eight far, 200 behind a wall");
    }
    const V3 eyeAt{-0.6f, 2.4f, -8.0f}, eyeTo{1.6f, 0.5f, -4.0f};
    const float fov=36.f, aspect=float(W)/float(H);
    const size_t pixels=size_t(W)*H;
    // The ball is read back and built over the first frames: until all of the scene is traced.
    {
        const Camera c=make_camera(eyeAt, eyeTo, fov, aspect);
        bool whole=false;
        for(uint32_t i=0;i<200 && !whole;++i) {
            run(TGPT_VIEW_NONE, c, c, i); context->Flush(); Sleep(20);
            ReadStatus(&status);
            whole=i>=4 && status.meshesQueued==0 && status.instancesTraced==reuseInstances;
        }
        expect(whole, "the reuse check's whole scene is traced before it measures");
    }
    // The sixteen shifts: a pixel's quarters (column, row), each differing from the one before in both (the edges check's).
    const int quarter[16][2]={{0,0},{2,2},{1,3},{3,1},{0,2},{2,0},{1,1},{3,3},{0,1},{2,3},{1,2},{3,0},{0,3},{2,1},{1,0},{3,2}};
    auto shiftOf=[&](int s, double& jx, double& jy) { jx=(quarter[s][0]+0.5)/4.0-0.5; jy=(quarter[s][1]+0.5)/4.0-0.5; };
    // What each pixel shows under each shift, and under none ([16]): the albedo view (no light, no noise), by the kinds'
    // colours (kindCount: none of them). edge: the pixel or one of its eight neighbours shows another kind under some shift
    // than the pixel shows under the first.
    std::vector<std::vector<uint8_t>> shown(17, std::vector<uint8_t>(pixels, uint8_t(kindCount)));
    std::vector<uint8_t> edge(pixels, 0);
    {
        const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_SPATIAL;
        std::vector<uint8_t> differs(pixels, 0);
        for(int k=-4;k<17;++k) {
            const int s=k<0 ? k+16 : k; double jx=0, jy=0; if(s<16) shiftOf(s, jx, jy);
            const Camera c=make_camera(eyeAt, eyeTo, fov, aspect, float(jx), float(jy), int(W), int(H));
            run(TGPT_VIEW_ALBEDO, c, c, uint32_t(k+4), raw);
            if(k<0) continue;
            const auto px=readback();
            auto& kind=shown[size_t(s)];
            for(size_t i=0;i<pixels;++i) {
                for(int n=0;n<kindCount;++n)
                    if(std::fabs(px[i*3]-kinds[n].r)<0.03f && std::fabs(px[i*3+1]-kinds[n].g)<0.03f && std::fabs(px[i*3+2]-kinds[n].b)<0.03f) { kind[i]=uint8_t(n); break; }
                if(s>0 && s<16 && kind[i]!=shown[0][i]) differs[i]=1;
            }
        }
        // Against the neighbours' kind under the first shift too: a border that no shift happens to move is still a border.
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const size_t i=size_t(y)*W+x; bool e=false;
            for(int dy=-1;dy<=1 && !e;++dy) for(int dx=-1;dx<=1 && !e;++dx) {
                const int qx=int(x)+dx, qy=int(y)+dy;
                if(qx<0 || qy<0 || qx>=int(W) || qy>=int(H)) continue;
                const size_t q=size_t(qy)*W+size_t(qx);
                e=differs[q]!=0 || shown[0][q]!=shown[0][i];
            }
            edge[i]=e ? 1 : 0;
        }
    }
    size_t edgePixels=0; for(uint8_t e:edge) edgePixels+=e;
    std::printf("      of %zu pixels, %zu (%.1f %%) are edges: the shift changes which surface they or a neighbour show\n", pixels, edgePixels, 100.0*double(edgePixels)/double(pixels));
    expect(edgePixels>pixels/200 && edgePixels<pixels/10, "the scene has thin things and edges, and far more that is neither");
    const uint32_t rounds=6, warm=32, measured=16*rounds;
    // [0] inside, [1] edges. specks, jumps: pixels a frame; change: levels; grid: the step across a pixel's border over the
    // step inside a pixel, along x and along y (inside pixels, the sixteen shifts laid out; 0 without the shift).
    // detail: the step from one inside pixel to the next in the frames' mean picture (how sharp the light's own picture is).
    // kind...: per kind of thing (and one more: none of them), the pixels that show it a frame, its specks and jumps a frame.
    struct ReuseStat { double mean=0, specks[2]={0, 0}, jumps[2]={0, 0}, change[2]={0, 0}, grid[2]={0, 0}, detail=0; double kindPixels[14]={}, kindSpecks[14]={}, kindJumps[14]={}; };
    uint32_t reuseIndex=85000;
    double levelScale=0.0; // the layer's light into the picture's: set by the first run
    // A level in 128ths (16 bits a pixel a frame).
    auto levelOf=[&](float l) { const double x=std::max(double(l), 0.0)*levelScale; return uint16_t(std::lround(128.0*255.0*std::pow(x/(1.0+x), 1.0/2.2))); };
    std::vector<std::vector<uint16_t>> lastLevels; std::vector<float> lastMean; std::vector<uint16_t> lastSpecks;
    // moving: the camera goes 3.3 mm to the side a frame (some 0.4 pixels at the things), eight frames one way and eight
    // back (a round's frames share camera and shift with the round before's), the previous camera handed as it was.
    // trial: the switch (TGPT_Frame.jitter's z) for the sequence's frames.
    auto sequence=[&](uint32_t view, bool shifted, float trial, uint32_t options=0, bool late=false, bool moving=false) {
        ReuseStat r;
        reuseTrial=trial;
        auto& level=lastLevels; level.assign(measured, {});
        std::vector<float> light(pixels);
        double total=0;
        lateMatrices=late;
        for(uint32_t i=0;i<warm+measured;++i) {
            double jx=0, jy=0; if(shifted) shiftOf(int(i%16), jx, jy); // the measured frames start at shift 0
            auto side=[](uint32_t n) { const uint32_t k=n%16; return 0.0033f*float(k<8 ? k : 16-k); };
            const V3 step{moving ? side(i) : 0.f, 0, 0}, back{moving && i ? side(i-1) : 0.f, 0, 0};
            const Camera c=make_camera(eyeAt+step, eyeTo+step, fov, aspect, float(jx), float(jy), int(W), int(H));
            const Camera before=moving && i ? make_camera(eyeAt+back, eyeTo+back, fov, aspect) : c;
            run(view, c, before, i ? reuseIndex++ : 0, options);
            if(i<warm) continue;
            const auto px=readback();
            for(size_t p=0;p<pixels;++p) { const float v=0.2126f*px[p*3]+0.7152f*px[p*3+1]+0.0722f*px[p*3+2]; light[p]=std::isfinite(v) ? std::min(v, 1e5f) : 1e5f; total+=double(light[p]); }
            if(!(levelScale>0.0)) { // the layer's mean at level 130, where the game's Direct layer has its: from the first measured frame
                double sum=0; for(float v:light) sum+=double(v);
                levelScale=sum>0.0 ? 0.294*double(pixels)/sum : 1.0;
            }
            auto& l=level[i-warm]; l.resize(pixels);
            for(size_t p=0;p<pixels;++p) l[p]=levelOf(light[p]);
        }
        lateMatrices=false; reuseTrial=0.f;
        r.mean=total/double(pixels*measured);
        auto& speckMap=lastSpecks; speckMap.assign(pixels, 0);
        auto& average=lastMean; average.assign(pixels, 0.f);
        const float one=1.f/128.f;
        double specks[2]={0, 0}, jumps[2]={0, 0}, change[2]={0, 0}; size_t counted[2]={0, 0};
        for(size_t p=0;p<pixels;++p) {
            const int e=edge[p]; ++counted[e];
            double sum=0;
            for(uint32_t s=0;s<16;++s) {
                const int kind=moving ? kindCount : int(shown[shifted ? s : 16u][p]);
                float v[16]; // a shift's frames, one a round
                for(uint32_t k=0;k<rounds;++k) { v[k]=float(level[s+16*k][p])*one; sum+=double(v[k]); }
                r.kindPixels[kind]+=1.0/16.0;
                for(uint32_t k=1;k<rounds;++k) { const float d=std::fabs(v[k]-v[k-1]); change[e]+=double(d); if(d>20.f) { jumps[e]+=1.0; r.kindJumps[kind]+=1.0/double(measured-16); } }
                float sorted[16]; std::copy(v, v+rounds, sorted); std::nth_element(sorted, sorted+rounds/2, sorted+rounds);
                const float usual=sorted[rounds/2];
                for(uint32_t k=0;k<rounds;++k) if(v[k]>usual+40.f) { specks[e]+=1.0; ++speckMap[p]; r.kindSpecks[kind]+=1.0/double(measured); }
            }
            average[p]=float(sum/double(measured));
        }
        for(int e=0;e<2;++e) {
            r.specks[e]=specks[e]/double(measured); r.jumps[e]=jumps[e]/double(measured-16);
            r.change[e]=counted[e] ? change[e]/(double(counted[e])*double(measured-16)) : 0.0;
        }
        // How sharp the light's own picture is: the frames' mean, its step from an inside pixel to the next (levels).
        {
            double steps=0; size_t pairs=0;
            for(UINT y=1;y+2<H;++y) for(UINT x=1;x+2<W;++x) {
                const size_t p=size_t(y)*W+x;
                if(edge[p] || edge[p+1] || edge[p+W]) continue;
                steps+=std::fabs(double(average[p+1])-double(average[p]))+std::fabs(double(average[p+W])-double(average[p])); pairs+=2;
            }
            r.detail=pairs ? steps/double(pairs) : 0.0;
        }
        if(shifted && !moving) {
            // The sixteen shifts laid out 4 x 4 to a pixel (each sample the mean of its shift's frames): the step between
            // two samples side by side, across a pixel's border and inside a pixel, on inside pixels.
            int frameOf[4][4]; for(int s=0;s<16;++s) frameOf[quarter[s][0]][quarter[s][1]]=s;
            auto sample=[&](size_t p, int sx, int sy) { const size_t s=size_t(frameOf[sx][sy]); double v=0; for(uint32_t k=0;k<rounds;++k) v+=double(level[s+16*k][p]); return v/(128.0*double(rounds)); };
            double border[2]={0, 0}, inside[2]={0, 0}; size_t borders[2]={0, 0}, insides[2]={0, 0};
            for(UINT y=1;y+2<H;++y) for(UINT x=1;x+2<W;++x) {
                const size_t p=size_t(y)*W+x, px1=p+1, py1=p+W;
                if(edge[p]) continue;
                double mine[4][4]; for(int a=0;a<4;++a) for(int b=0;b<4;++b) mine[a][b]=sample(p, a, b);
                for(int a=0;a<4;++a) {
                    for(int b=0;b<3;++b) { inside[0]+=std::fabs(mine[b+1][a]-mine[b][a]); inside[1]+=std::fabs(mine[a][b+1]-mine[a][b]); }
                    insides[0]+=3; insides[1]+=3;
                    if(!edge[px1]) { border[0]+=std::fabs(sample(px1, 0, a)-mine[3][a]); ++borders[0]; }
                    if(!edge[py1]) { border[1]+=std::fabs(sample(py1, a, 0)-mine[a][3]); ++borders[1]; }
                }
            }
            for(int k=0;k<2;++k) r.grid[k]=borders[k] && inside[k]>0.0 ? (border[k]/double(borders[k]))/(inside[k]/double(insides[k])) : 0.0;
        }
        return r;
    };
    auto say=[&](const char* layer, const char* what, const ReuseStat& s, const ReuseStat& z) {
        std::printf("      %-8s %-24s | specks a frame, edges %7.1f (still %6.1f), inside %7.1f (still %6.1f) | jumps a frame, edges %8.1f (still %7.1f), inside %8.1f (still %7.1f) | "
                    "change, edges %5.2f (still %5.2f), inside %5.3f (still %5.3f) | the light on the grid x%.2f, x%.2f | detail %.3f (still %.3f) | mean %.5f (still %.5f)\n",
                    layer, what, s.specks[1], z.specks[1], s.specks[0], z.specks[0], s.jumps[1], z.jumps[1], s.jumps[0], z.jumps[0],
                    s.change[1], z.change[1], s.change[0], z.change[0], s.grid[0], s.grid[1], s.detail, z.detail, s.mean, z.mean);
        std::fflush(stdout);
    };
    // By kind of thing: of a thousand pixels that show it in a frame, how many are specks and how many jump: with the
    // shift and the switch off, with the switch on, and without any shift.
    auto sayKinds=[&](const char* layer, const ReuseStat& off, const ReuseStat& on, const ReuseStat& z) {
        for(int k=0;k<kindCount;++k) {
            const double po=std::max(off.kindPixels[k], 1e-9), pn=std::max(on.kindPixels[k], 1e-9), pz=std::max(z.kindPixels[k], 1e-9);
            std::printf("      %-8s by kind: %-17s | %8.0f pixels a frame | specks in a thousand: switch off %7.2f, on %7.2f, no shift %6.2f | jumps in a thousand: off %7.2f, on %7.2f, no shift %6.2f\n",
                        layer, kinds[k].name, off.kindPixels[k], 1000.0*off.kindSpecks[k]/po, 1000.0*on.kindSpecks[k]/pn, 1000.0*z.kindSpecks[k]/pz,
                        1000.0*off.kindJumps[k]/po, 1000.0*on.kindJumps[k]/pn, 1000.0*z.kindJumps[k]/pz);
        }
        std::fflush(stdout);
    };
    // A picture of the last sequence: one frame, or the frames' mean with the specks red. Flipped as save() flips.
    auto pictureOf=[&](int which, const char* name) {
        std::vector<uint8_t> out(pixels*3);
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const size_t p=size_t(H-1-y)*W+x, o=(size_t(y)*W+x)*3;
            const uint8_t v=uint8_t(std::lround(std::clamp(which==0 ? float(lastLevels[measured-1][p])/128.f : lastMean[p], 0.f, 255.f)));
            out[o]=out[o+1]=out[o+2]=v;
            if(which==1 && lastSpecks[p]) { out[o]=255; out[o+1]=out[o+2]=0; }
        }
        write_png(images/name, int(W), int(H), out);
    };
    // The sixteen shifts' frames of the last sequence laid out 4 x 4 to a pixel: a quarter of the screen about the fan.
    auto laidOut=[&](const char* name) {
        int frameOf[4][4]; for(int s=0;s<16;++s) frameOf[quarter[s][0]][quarter[s][1]]=s;
        const UINT x0=W*3/8, y0=H*3/8;
        std::vector<uint8_t> out(pixels*3);
        for(UINT fy=0;fy<H;++fy) for(UINT fx=0;fx<W;++fx) {
            const UINT x=std::min(x0+fx/4, W-1), y=std::min(y0+fy/4, H-1);
            const size_t s=size_t(frameOf[fx%4][fy%4]);
            const size_t p=size_t(y)*W+x, o=(size_t(H-1-fy)*W+fx)*3;
            double v=0; for(uint32_t k=0;k<rounds;++k) v+=double(lastLevels[s+16*k][p]);
            const uint8_t byte=uint8_t(std::lround(std::clamp(v/(128.0*double(rounds)), 0.0, 255.0)));
            out[o]=out[o+1]=out[o+2]=byte;
        }
        write_png(images/name, int(W), int(H), out);
    };
    auto all=[](const double v[2]) { return v[0]+v[1]; };
    // The layers as the game's debug views show them (its own denoiser: history, a-trous), ReSTIR's reuse on.
    const ReuseStat directStill=sequence(TGPT_VIEW_DIRECT, false, 0.f);
    pictureOf(0, "85-reuse-direct-still.png"); pictureOf(1, "85-reuse-direct-still-specks.png");
    const ReuseStat directOff=sequence(TGPT_VIEW_DIRECT, true, 0.f);
    pictureOf(0, "85-reuse-direct-shift-off.png"); pictureOf(1, "85-reuse-direct-shift-off-specks.png"); laidOut("85-reuse-direct-16-frames-off.png");
    say("Direct", "the switch off", directOff, directStill);
    const ReuseStat directOn=sequence(TGPT_VIEW_DIRECT, true, 1.f);
    pictureOf(0, "85-reuse-direct-shift-on.png"); pictureOf(1, "85-reuse-direct-shift-on-specks.png"); laidOut("85-reuse-direct-16-frames-on.png");
    say("Direct", "the switch on", directOn, directStill);
    // 2: ReSTIR's reuse across frames fetched in the previous grid as well. Its own search finds its surface without that;
    // with it the light choices stay with one point for longer and the history averages them less (the change inside).
    const ReuseStat directAll=sequence(TGPT_VIEW_DIRECT, true, 2.f);
    say("Direct", "2: ReSTIR's fetch too", directAll, directStill);
    std::printf("      Direct, the change from round to round inside flat surfaces (levels): no shift %.3f, the switch off %.3f, on %.3f, at 2 %.3f\n",
                directStill.change[0], directOff.change[0], directOn.change[0], directAll.change[0]);
    sayKinds("Direct", directOff, directOn, directStill);
    const ReuseStat specularStill=sequence(TGPT_VIEW_SPECULAR, false, 0.f), specularOff=sequence(TGPT_VIEW_SPECULAR, true, 0.f);
    pictureOf(1, "85-reuse-specular-shift-off-specks.png");
    say("Specular", "the switch off", specularOff, specularStill);
    const ReuseStat specularOn=sequence(TGPT_VIEW_SPECULAR, true, 1.f);
    pictureOf(0, "85-reuse-specular-shift-on.png"); pictureOf(1, "85-reuse-specular-shift-on-specks.png");
    say("Specular", "the switch on", specularOn, specularStill);
    sayKinds("Specular", specularOff, specularOn, specularStill);
    const ReuseStat bouncedStill=sequence(TGPT_VIEW_INDIRECT, false, 0.f), bouncedOff=sequence(TGPT_VIEW_INDIRECT, true, 0.f), bouncedOn=sequence(TGPT_VIEW_INDIRECT, true, 1.f);
    say("Indirect", "the switch off", bouncedOff, bouncedStill); say("Indirect", "the switch on", bouncedOn, bouncedStill);
    // Handed the matrices without the shift, as the game handed them until the plugin read them earlier: the same.
    const ReuseStat directLate=sequence(TGPT_VIEW_DIRECT, true, 1.f, 0, true);
    say("Direct", "on, matrices handed late", directLate, directStill);
    // ReSTIR's own part: its raw samples (no denoiser).
    const ReuseStat rawStill=sequence(TGPT_VIEW_DIRECT, false, 0.f, TGPT_OPTION_NO_DENOISER), rawOff=sequence(TGPT_VIEW_DIRECT, true, 0.f, TGPT_OPTION_NO_DENOISER),
                    rawOn=sequence(TGPT_VIEW_DIRECT, true, 1.f, TGPT_OPTION_NO_DENOISER);
    say("Direct", "no denoiser, off", rawOff, rawStill); say("Direct", "no denoiser, on", rawOn, rawStill);
    // The camera moving slowly (0.4 pixels a frame): the fetch lands between pixels with and without the shift.
    const ReuseStat movingStill=sequence(TGPT_VIEW_DIRECT, false, 0.f, 0, false, true), movingOff=sequence(TGPT_VIEW_DIRECT, true, 0.f, 0, false, true),
                    movingOn=sequence(TGPT_VIEW_DIRECT, true, 1.f, 0, false, true);
    const ReuseStat movingSpecularStill=sequence(TGPT_VIEW_SPECULAR, false, 0.f, 0, false, true), movingSpecularOff=sequence(TGPT_VIEW_SPECULAR, true, 0.f, 0, false, true),
                    movingSpecularOn=sequence(TGPT_VIEW_SPECULAR, true, 1.f, 0, false, true);
    auto sayMoving=[&](const char* layer, const ReuseStat& off, const ReuseStat& on, const ReuseStat& z) {
        const double inside=double(pixels-edgePixels), edges=double(edgePixels);
        auto whole=[&](const ReuseStat& s) { return (s.change[0]*inside+s.change[1]*edges)/double(pixels); };
        std::printf("      %-8s the camera moving        | specks a frame: switch off %7.1f, on %7.1f, no shift %6.1f | jumps a frame: off %8.1f, on %8.1f, no shift %7.1f | change: off %5.3f, on %5.3f, no shift %5.3f | mean: off %.5f, on %.5f, no shift %.5f\n",
                    layer, all(off.specks), all(on.specks), all(z.specks), all(off.jumps), all(on.jumps), all(z.jumps), whole(off), whole(on), whole(z), off.mean, on.mean, z.mean);
    };
    sayMoving("Direct", movingOff, movingOn, movingStill); sayMoving("Specular", movingSpecularOff, movingSpecularOn, movingSpecularStill);
    // What must hold (the numbers: docs/agents/reuse-shift.md).
    std::printf("      Direct: specks a frame, switch off %.1f, on %.1f, no shift %.1f; jumps %.1f, %.1f, %.1f. Specular: specks %.1f, %.1f, %.1f; jumps %.1f, %.1f, %.1f\n",
                all(directOff.specks), all(directOn.specks), all(directStill.specks), all(directOff.jumps), all(directOn.jumps), all(directStill.jumps),
                all(specularOff.specks), all(specularOn.specks), all(specularStill.specks), all(specularOff.jumps), all(specularOn.jumps), all(specularStill.jumps));
    std::fflush(stdout);
    expect(std::fabs(directOn.mean/std::max(directStill.mean, 1e-12)-1.0)<0.03 && std::fabs(specularOn.mean/std::max(specularStill.mean, 1e-12)-1.0)<0.05 &&
           std::fabs(directOn.mean/std::max(directOff.mean, 1e-12)-1.0)<0.02 && std::fabs(specularOn.mean/std::max(specularOff.mean, 1e-12)-1.0)<0.03,
           "the switch leaves each layer's mean light as it is (against no shift: direct within 3 percent, reflections within 5; against the switch off: 2 and 3)");
    expect(std::fabs(directLate.mean/std::max(directOn.mean, 1e-12)-1.0)<0.02 && all(directLate.jumps)<=1.5*all(directOn.jumps)+50.0,
           "matrices handed without the shift give the same picture as matrices that hold it");
    expect(directOff.grid[1]>1.3 && all(directOff.jumps)>4.0*all(directStill.jumps),
           "with the switch off this scene shows what the game shows: the Direct layer jumps several times as much under the shift, its light on the pixel grid");
    expect(directOn.grid[0]>0.85 && directOn.grid[0]<1.15 && directOn.grid[1]>0.85 && directOn.grid[1]<1.15 &&
           specularOn.grid[0]>0.85 && specularOn.grid[0]<1.15 && specularOn.grid[1]>0.85 && specularOn.grid[1]<1.15,
           "with the switch on the denoised light follows the camera's shift (no steps at the pixels' borders: within 15 percent)");
    expect(all(directOn.jumps)<0.6*all(directOff.jumps) && all(specularOn.jumps)<0.8*all(specularOff.jumps),
           "with the switch on the Direct layer jumps under 0.6 times as much as with it off, the Specular layer under 0.8 times");
    expect(directOn.change[0]<1.1*directOff.change[0] && specularOn.change[0]<1.15*specularOff.change[0],
           "with the switch on the light inside flat surfaces changes no more from round to round than with it off (no more boiling)");
    sunOn=keepSun; ownRays=keepOwn; lateMatrices=keepLate; skyIntensity=keepSky; reuseTrial=keepTrial;
    history=keepNumbers[0]; giHistory=keepNumbers[1]; bounces=keepNumbers[2]; spatialSamples=keepNumbers[3]; rays=keepNumbers[4];
    expect(SetMaterials(0, materials.data(), uint32_t(materials.size()))==0 &&
           SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0 && SetLights(&light, 1)==0, "the harness's room again after the reuse check");
    if(reuseBall>0) expect(ReleaseMesh(reuseBall)==0, "the reuse check's ball released");
    for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, cam, cam, i);
}
if(GetEnvironmentVariableA("TGPT_TEST_REUSE_ONLY", nullptr, 0)>0) {
    std::printf(failures ? "FAILED %d checks\n" : "reuse checks passed\n", failures); return failures ? 1 : 0;
}
