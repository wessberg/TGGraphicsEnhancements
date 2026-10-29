// SPDX-License-Identifier: MIT
// PT-84: the traced picture carries the sub-pixel shift of the frame it is shown in (docs/agents/edges.md). Alone with
// TGPT_TEST_EDGES_ONLY=1 (right after the warm-up, some seconds), and in the whole test.
//   HDRP shifts its camera by a fraction of a pixel every frame and tells its upscaler that shift; the upscaler lays each
//   frame's samples down where they were taken, and edges come out smooth. So what the composite leaves in HDRP's color
//   buffer in a frame must be the scene sampled at that frame's shift: not at the frame before's (the light shown a frame
//   after it was traced), and not at none (the camera's rays through the plain pixel centres, as in the game until
//   2026-10-08, when every traced edge was a stair of render pixels and every thin thing a row of dots).
//   The scene: a blue floor; over it a red bar about a pixel wide, 17 degrees off the screen's vertical; and a green card
//   with round holes cut out by its texture's alpha. Shown in the albedo view with the tracer's own camera rays: no
//   light, no noise, each pixel is the colour of what its ray met. What it should meet is worked out here on the CPU from
//   the camera alone (no matrix of the test's): a pixel's centre looks at the point that lies +shift pixels from it in the
//   picture without a shift. Pixels whose sample point lies within 0.03 pixels of an edge, or 1.5 texels of a hole's rim,
//   are left out.
//   Sixteen frames, their shifts the 4 x 4 grid of a pixel's quarters in a mixed order, the camera still. Counted over
//   them: the pixels that are not what their own frame's shift says, not what the frame before's says, not what no shift
//   says. Passing: the first is next to nothing beside the other two. Three times: the matrices handed with the shift in
//   them (as HDRP's own are while it records its opaque passes, which the plugin now reads); handed WITHOUT it and the
//   jitter told all the same (as the plugin handed them before: the tracer then puts the shift in itself and says so in
//   its log, camera_shift.hpp); and with the camera moving 2 cm a frame.
//   Pictures: the sixteen frames laid out as an ideal upscaler would, each sample where its shift says, 4 x 4 to a pixel
//   (84-edges-16-frames.png; the same for the matrices handed without the shift, 84-edges-16-frames-late.png), one of the
//   frames alone at that size (84-edges-one-frame.png), and what the CPU says the scene is (84-edges-scene.png).
{
    std::printf("[edges] the traced picture carries the sub-pixel shift of the frame it is shown in\n");
    if(RegisterSharedTexture) {
        struct EdgeCard { V3 at; double angle, sx, sz; int cls; bool holes; };
        const EdgeCard edgeCards[3]={{{0, 0, 0}, 0.0, 12.0, 12.0, 3, false}, {{-0.9f, 1.f, 0.2f}, 0.3, 0.008, 3.0, 1, false}, {{1.f, 1.f, 0.3f}, 0.2, 1.8, 1.8, 2, true}};
        const UINT side=2048; const double holeRadius=0.08;
        // The card's texture: green, alpha 0 inside sixteen round holes (the same seen from either end of u or v).
        ComPtr<ID3D11Texture2D> lace;
        {
            std::vector<uint32_t> texels(size_t(side)*side);
            for(UINT y=0;y<side;++y) for(UINT x=0;x<side;++x) {
                const double u=(x+0.5)/side, v=(y+0.5)/side;
                const double cu=(std::floor(u*4.0)+0.5)/4.0, cv=(std::floor(v*4.0)+0.5)/4.0;
                texels[size_t(y)*side+x]=std::hypot(u-cu, v-cv)<holeRadius ? 0x0000FF00u : 0xFF00FF00u;
            }
            D3D11_TEXTURE2D_DESC td{side, side, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            D3D11_SUBRESOURCE_DATA init{texels.data(), side*4, 0};
            expect(SUCCEEDED(device->CreateTexture2D(&td, &init, &lace)), "the card's texture with holes created");
        }
        TGPT_SharedTexture laceShared{sizeof(TGPT_SharedTexture), TGPT_ABI};
        const int32_t laceRegistered=lace ? RegisterSharedTexture(uint64_t(uintptr_t(lace.Get())), &laceShared) : -1;
        expect(laceRegistered>=0 && laceShared.handle>=0, "the card's texture shared");
        if(laceRegistered>=0 && laceShared.handle>=0) {
            auto ms=materials;
            const uint32_t first=uint32_t(ms.size());
            TGPT_Material floorMat=materials[0]; floorMat.albedoTile=-1; floorMat.baseColor[0]=floorMat.baseColor[1]=0.f; floorMat.baseColor[2]=0.8f;
            TGPT_Material barMat=materials[0]; barMat.albedoTile=-1; barMat.baseColor[0]=0.8f; barMat.baseColor[1]=barMat.baseColor[2]=0.f;
            TGPT_Material laceMat=materials[6]; laceMat.colorTexture=laceShared.handle; // the native resolver supplies MATERIAL_SHARED, as for the game
            laceMat.baseColor[0]=laceMat.baseColor[1]=laceMat.baseColor[2]=laceMat.baseColor[3]=1.f; laceMat.alphaCutoff=0.5f;
            ms.push_back(floorMat); ms.push_back(barMat); ms.push_back(laceMat);
            TGPT_Instance placed[3]{}; const uint32_t ids[3]={first, first+1, first+2};
            for(int k=0;k<3;++k) {
                const EdgeCard& c=edgeCards[k];
                const float cs=float(std::cos(c.angle)), sn=float(std::sin(c.angle));
                // Object to world: the card's size, a turn about the vertical, its place.
                const float rows[12]={cs*float(c.sx), 0, sn*float(c.sz), c.at.x,  0, 1, 0, c.at.y,  -sn*float(c.sx), 0, cs*float(c.sz), c.at.z};
                for(int i=0;i<12;++i) placed[k].transform[i]=rows[i];
                placed[k].mesh=cardHandle; placed[k].materialBase=uint32_t(k); placed[k].mask=0xFF;
                placed[k].flags=c.holes ? TGPT_INSTANCE_ALPHA_TEST : 0u;
            }
            expect(SetMaterials(0, ms.data(), uint32_t(ms.size()))==0 && SetScene(placed, 3, ids, 3)==0, "the edges scene set");
            const bool ownBefore=ownRays; ownRays=true;
            const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_SPATIAL;
            const V3 eyeAt{0, 4, -4}, eyeTo{0, 1, 0};
            const float fov=60.f, aspect=float(W)/float(H);
            const size_t pixels=size_t(W)*H;
            // What a sample point of the picture WITHOUT a shift shows (x to the right, y down the render target's rows, in
            // pixels): 0 nothing, 1 the bar, 2 the card, 3 the floor. From the camera's place and its three axes as
            // make_camera makes them, no matrix. rim: the point lies within 1.5 texels of a hole's rim.
            struct EdgeEye { V3 from; double r[3], u[3], f[3], t; };
            auto eyeOf=[&](V3 from, V3 to) {
                const V3 f=normalize(to-from), r=normalize(cross({0, 1, 0}, f)), u=cross(f, r);
                EdgeEye e; e.from=from; e.t=1.0/std::tan(double(fov)*3.14159265358979323846/360.0);
                e.r[0]=r.x; e.r[1]=r.y; e.r[2]=r.z; e.u[0]=u.x; e.u[1]=u.y; e.u[2]=u.z; e.f[0]=f.x; e.f[1]=f.y; e.f[2]=f.z;
                return e;
            };
            double cardCos[3], cardSin[3]; for(int k=0;k<3;++k) { cardCos[k]=std::cos(edgeCards[k].angle); cardSin[k]=std::sin(edgeCards[k].angle); }
            auto classAt=[&](const EdgeEye& e, double sx, double sy, bool& rim) {
                // The render target's rows run up the screen: its y is GL's.
                const double nx=(2.0*sx/double(W)-1.0)*double(aspect)/e.t, ny=(2.0*sy/double(H)-1.0)/e.t;
                const double dx=e.r[0]*nx+e.u[0]*ny+e.f[0], dy=e.r[1]*nx+e.u[1]*ny+e.f[1], dz=e.r[2]*nx+e.u[2]*ny+e.f[2];
                int cls=0; double nearest=1e30;
                if(!(dy<0.0)) return cls; // the cards face up, and are seen from above only
                for(int k=0;k<3;++k) {
                    const EdgeCard& c=edgeCards[k];
                    const double at=(double(c.at.y)-double(e.from.y))/dy;
                    if(!(at>0.0 && at<nearest)) continue;
                    const double qx=double(e.from.x)+at*dx-double(c.at.x), qz=double(e.from.z)+at*dz-double(c.at.z);
                    const double lx=(cardCos[k]*qx-cardSin[k]*qz)/c.sx, lz=(cardSin[k]*qx+cardCos[k]*qz)/c.sz;
                    if(std::fabs(lx)>0.5 || std::fabs(lz)>0.5) continue;
                    if(c.holes) {
                        const double u=lx+0.5, v=lz+0.5, cu=(std::floor(u*4.0)+0.5)/4.0, cv=(std::floor(v*4.0)+0.5)/4.0, d=std::hypot(u-cu, v-cv);
                        if(std::fabs(d-holeRadius)*double(side)<1.5) rim=true;
                        if(d<holeRadius) continue;
                    }
                    nearest=at; cls=c.cls;
                }
                return cls;
            };
            // A whole frame of it for a camera and a shift: each pixel's centre looks at the point +shift pixels from it.
            // sure: the same thing 0.03 pixels to each side, and no hole's rim near.
            struct EdgeFrame { std::vector<uint8_t> cls, sure; };
            auto expectedFrame=[&](V3 from, V3 to, double jx, double jy) {
                const EdgeEye e=eyeOf(from, to);
                EdgeFrame out; out.cls.resize(pixels); out.sure.resize(pixels);
                const double probe=0.03, probes[4][2]={{probe, 0}, {-probe, 0}, {0, probe}, {0, -probe}};
                for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
                    const double sx=x+0.5+jx, sy=y+0.5+jy;
                    bool rim=false; const int cls=classAt(e, sx, sy, rim);
                    bool sure=!rim;
                    for(int k=0;k<4 && sure;++k) sure=classAt(e, sx+probes[k][0], sy+probes[k][1], rim)==cls && !rim;
                    out.cls[size_t(y)*W+x]=uint8_t(cls); out.sure[size_t(y)*W+x]=sure ? 1 : 0;
                }
                return out;
            };
            // What a pixel of the color buffer shows, by its colour: 4 is none of the scene's four.
            auto shownClass=[&](const std::vector<float>& px, size_t i) {
                const float R=px[i*3], G=px[i*3+1], B=px[i*3+2];
                return R>0.3f && G<0.15f && B<0.15f ? 1 : G>0.3f && R<0.15f && B<0.15f ? 2 : B>0.3f && R<0.15f && G<0.15f ? 3 : (R<0.05f && G<0.05f && B<0.05f) ? 0 : 4;
            };
            // The sixteen shifts: a pixel's quarters (column, row), each differing from the one before in both.
            const int quarter[16][2]={{0,0},{2,2},{1,3},{3,1},{0,2},{2,0},{1,1},{3,3},{0,1},{2,3},{1,2},{3,0},{0,3},{2,1},{1,0},{3,2}};
            auto shiftOf=[&](int s, double& jx, double& jy) { jx=(quarter[s][0]+0.5)/4.0-0.5; jy=(quarter[s][1]+0.5)/4.0-0.5; };
            std::vector<EdgeFrame> still(16);
            for(int s=0;s<16;++s) { double jx, jy; shiftOf(s, jx, jy); still[s]=expectedFrame(eyeAt, eyeTo, jx, jy); }
            const EdgeFrame plain=expectedFrame(eyeAt, eyeTo, 0.0, 0.0);
            struct EdgeTally { size_t own=0, before=0, none=0, judged=0; };
            const fs::path edgesLog=root/"TGPathTracing"/"TGPathTracing.Native.log";
            std::error_code edgesEc; const auto logFrom=fs::exists(edgesLog, edgesEc) ? fs::file_size(edgesLog, edgesEc) : 0;
            // The camera still. A frame with the last shift first, so that the first judged frame has a frame before it.
            auto stillRun=[&](bool late, uint32_t base, std::vector<std::vector<float>>& frames) {
                EdgeTally t; lateMatrices=late; frames.assign(16, {});
                for(int k=-1;k<16;++k) {
                    const int s=(k+16)%16; double jx, jy; shiftOf(s, jx, jy);
                    const Camera c=make_camera(eyeAt, eyeTo, fov, aspect, float(jx), float(jy), int(W), int(H));
                    run(TGPT_VIEW_ALBEDO, c, c, base+uint32_t(k+1), raw);
                    if(k<0) continue;
                    frames[s]=readback();
                    const EdgeFrame& own=still[s]; const EdgeFrame& before=still[(s+15)%16];
                    for(size_t i=0;i<pixels;++i) {
                        const int shown=shownClass(frames[s], i);
                        t.own+=own.sure[i] && shown!=own.cls[i]; t.before+=before.sure[i] && shown!=before.cls[i]; t.none+=plain.sure[i] && shown!=plain.cls[i];
                        t.judged+=own.sure[i];
                    }
                }
                lateMatrices=false;
                return t;
            };
            auto carries=[](const EdgeTally& t) { const size_t least=std::min(t.before, t.none); return least>1000 && t.own<=std::max<size_t>(16, least/50); };
            std::vector<std::vector<float>> shifted, late;
            const EdgeTally a=stillRun(false, 13000, shifted);
            std::printf("      the matrices hold the shift: of %zu pixel samples in 16 frames, %zu are not what their frame's shift says; %zu not what the frame before's says, %zu not what no shift says\n",
                        a.judged, a.own, a.before, a.none);
            expect(carries(a), "each frame's picture is the scene sampled at that frame's own sub-pixel shift (not the frame before's, not none)");
            const EdgeTally b=stillRun(true, 13100, late);
            std::printf("      the matrices handed without the shift, as the game handed them: %zu not what their frame's shift says; %zu not the frame before's, %zu not what no shift says\n",
                        b.own, b.before, b.none);
            expect(carries(b), "handed the camera's matrices without the shift, the tracer's rays carry the told shift all the same");
            {
                // And it says so, once for each way the matrices came (extension.cpp, camera_shift.hpp).
                bool held=false, applied=false;
                std::ifstream in(edgesLog, std::ios::binary); in.seekg(std::streamoff(logFrom)); std::string line;
                while(std::getline(in, line)) {
                    held=held || line.find("The camera's rays carry HDRP's sub-pixel shift")!=std::string::npos;
                    applied=applied || line.find("the tracer put it into its camera rays itself")!=std::string::npos;
                }
                expect(held && applied, "the tracer's log says how the matrices came: holding the shift, and without it");
            }
            // The camera moving 2 cm a frame to the side (some 2.4 pixels at the cards): each frame still shows its own
            // camera's scene at its own shift, and not the frame before's picture.
            {
                EdgeTally m; EdgeFrame lastFrame;
                for(int k=-1;k<16;++k) {
                    const int s=(k+16)%16; double jx, jy; shiftOf(s, jx, jy);
                    const V3 step{0.02f*float(k+1), 0, 0}, back{0.02f*float(k), 0, 0};
                    const Camera c=make_camera(eyeAt+step, eyeTo+step, fov, aspect, float(jx), float(jy), int(W), int(H));
                    const Camera p=k<0 ? c : make_camera(eyeAt+back, eyeTo+back, fov, aspect);
                    run(TGPT_VIEW_ALBEDO, c, p, 13200+uint32_t(k+1), raw);
                    EdgeFrame own=expectedFrame(eyeAt+step, eyeTo+step, jx, jy);
                    if(k>=0) {
                        const auto px=readback();
                        for(size_t i=0;i<pixels;++i) {
                            const int shown=shownClass(px, i);
                            m.own+=own.sure[i] && shown!=own.cls[i]; m.before+=lastFrame.sure[i] && shown!=lastFrame.cls[i]; m.judged+=own.sure[i];
                        }
                    }
                    lastFrame=std::move(own);
                }
                std::printf("      the camera moving 2 cm a frame: of %zu pixel samples, %zu are not what their own frame's camera and shift say; %zu not the frame before's picture\n",
                            m.judged, m.own, m.before);
                expect(m.before>1000 && m.own<=std::max<size_t>(16, m.before/50), "with the camera moving, each frame's picture is that frame's own: its camera, its shift");
            }
            // Pictures: a quarter of the screen's width and height about the bar and the card's near half, 4 x 4 to a pixel.
            {
                const UINT x0=W*3/8, y0=H*2/5;
                int frameOf[4][4]; for(int s=0;s<16;++s) frameOf[quarter[s][0]][quarter[s][1]]=s;
                auto toByte=[](float v) { return uint8_t(std::lround(std::clamp(std::pow(std::clamp(v, 0.f, 1.f), 1.f/2.2f), 0.f, 1.f)*255.f)); };
                const float palette[5][3]={{0, 0, 0}, {0.8f, 0, 0}, {0, 1, 0}, {0, 0, 0.8f}, {1, 1, 1}};
                // which: 0 the sixteen frames, each sample where its shift says; 1 one frame alone; 2 what the scene is.
                auto picture=[&](const std::vector<std::vector<float>>& frames, int which, const char* name) {
                    std::vector<uint8_t> out(pixels*3);
                    for(UINT fy=0;fy<H;++fy) for(UINT fx=0;fx<W;++fx) {
                        const UINT x=std::min(x0+fx/4, W-1), y=std::min(y0+fy/4, H-1);
                        const int s=frameOf[fx%4][fy%4];
                        const size_t i=size_t(y)*W+x, o=(size_t(H-1-fy)*W+fx)*3; // the render target is upside down against the screen
                        for(int k=0;k<3;++k) out[o+k]=toByte(which==2 ? palette[still[s].cls[i]][k] : frames[which==1 ? 0 : s][i*3+k]);
                    }
                    write_png(images/name, int(W), int(H), out);
                };
                if(shifted.size()==16 && !shifted[15].empty()) { picture(shifted, 0, "84-edges-16-frames.png"); picture(shifted, 1, "84-edges-one-frame.png"); picture(shifted, 2, "84-edges-scene.png"); }
                if(late.size()==16 && !late[15].empty()) picture(late, 0, "84-edges-16-frames-late.png");
            }
            ownRays=ownBefore;
            SetMaterials(0, materials.data(), uint32_t(materials.size()));
            SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()));
            for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, cam, cam, i);
        }
    } else expect(false, "the export the edges check needs");
}
if(GetEnvironmentVariableA("TGPT_TEST_EDGES_ONLY", nullptr, 0)>0) {
    std::printf(failures ? "FAILED %d checks\n" : "edges checks passed\n", failures); return failures ? 1 : 0;
}
