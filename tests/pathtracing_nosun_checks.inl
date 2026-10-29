// SPDX-License-Identifier: MIT
// TGPT_TEST_NOSUN_ONLY=1 (PT-133, docs/agents/nosun.md; included in tgpt_gpu_test's speed study, after its materials 100
// to 109 and its eight lamps are set; alone, some minutes; the whole test does not take it): A FRAME WITHOUT A SUN.
//   Why: the game's interiors and nights have no sun (the plugin sends one only when the game's is valid: every number of
//   it then 0), and on 2026-10-11 the fog cells' Shader Model 6.9 pass returned 1e33 to 1e37 in such frames for a sum that
//   is zero on the branch that skips the sun's code, though the compiler's output is right: it is the driver's compile of
//   the 6.9 build that went wrong, by the code's shape. The tavern was white in the game while every offline check passed:
//   their scenes all had a sun.
//   What it does: a room closed all round (so the sun, on or off, lights nothing in it), lit by eight lamps and a glowing
//   panel, holding what the big passes shade: walls of HDRP Lit's three maps, boxes, a clear glass cube, a wax cube (the
//   scattering walk), a smooth metal cube (a reflection in a reflection), a hair card (strands), a pure mirror on the back
//   wall (primary surface replacement under NRD) and thin cloth. The tracer's own camera rays, the game's defaults (one
//   bounce, ReSTIR's reuse, the radiance cache, the glowing panel sampled as a light).
//   Each arrangement is traced four times from a reset, 32 frames to settle and the mean of the next 16:
//     A  with the sun (its code runs; the room's walls stop every one of its rays);
//     B  without it as this test's checks leave it out (TGPT_Frame.sunColor's w 0, direction and colour as they are);
//     N  B again with other random numbers: what two means differ by with nothing changed (the noise);
//     C  without it as the plugin sends it (direction, angular radius and colour 0 as well).
//   The arrangements: no denoiser; NRD (its confidence pass and its mirror records); HDRP's fog traced by the old route
//   with seven fog boxes across the view (pt_fog, and the fog over the reflections' rays); and the same fog by the grid's
//   cells (pt_fog_froxel and its three passes: the pass that showed the fault. Checked by mutation: the build that was
//   white in the game, fogfix's of 2026-10-11 23:00, fails this arrangement).
//   Judged: nothing not finite, nothing above 30,000 (the half float's largest is 65,504; the brightest thing here, a
//   lamp's own ball, is under 1,000); and A's and C's means against B's, the whole picture's and each block's of a 16 x 9
//   grid, within three times what N differs from B by (and 2 % of the block: 1 % for the whole picture).
//   The same frames under the tracer's Shader Model 6.6 passes: run it again with TGPT_TEST_SM66=1 (then it judges those);
//   and with TGPT_TEST_TRACE=<file> in both runs, tools/agents/nosun_compare.py lays the two sets side by side frame by
//   frame.
//   NOT in it (docs/agents/nosun.md says what was looked at by other modes): water and its caustics, decals, plants in the
//   wind, characters, Ray Reconstruction.
{
    std::printf("\n[nosun] a frame without a sun (PT-133): a closed room, the sun's code run and skipped\n");
    const int failuresBefore=failures;
    // Materials of its own, after the study's ten: 110 a hair card (strands), 111 a smooth metal, 112 a glowing panel,
    // 113 a pure mirror, 114 thin cloth.
    TGPT_Material own[5];
    own[0]=materials[0]; own[0].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_STRANDS; own[0].alphaTile=-1; own[0].alphaCutoff=0.f; own[0].albedoTile=-1;
    own[0].baseColor[0]=0.35f; own[0].baseColor[1]=0.22f; own[0].baseColor[2]=0.12f; own[0].baseColor[3]=1.f;
    own[1]=six[0]; own[1].metallic=1.f; own[1].smoothness=0.9f; own[1].baseColor[0]=0.9f; own[1].baseColor[1]=0.7f; own[1].baseColor[2]=0.4f;
    own[2]=six[0]; own[2].emissive[0]=6.f; own[2].emissive[1]=3.f; own[2].emissive[2]=1.f; own[2].emissiveTile=-1;
    own[3]=six[0]; own[3].metallic=1.f; own[3].smoothness=1.f; own[3].baseColor[0]=own[3].baseColor[1]=own[3].baseColor[2]=1.f;
    own[4]=six[0]; own[4].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_THIN_CLOTH; own[4].alphaCutoff=0.f;
    own[4].baseColor[0]=0.7f; own[4].baseColor[1]=0.2f; own[4].baseColor[2]=0.2f; own[4].baseColor[3]=1.f;
    expect(SetMaterials(110, own, 5)==0, "the no-sun check's materials set");
    {
        std::vector<TGPT_Instance> list; std::vector<uint32_t> index;
        auto put=[&](int mesh, const M4& world, uint32_t material, uint32_t flags) {
            TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
            in.mesh=mesh; in.materialBase=uint32_t(index.size()); in.mask=0xFF; in.flags=flags;
            index.push_back(material); index.push_back(material); list.push_back(in);
        };
        // The study's room (12 m square, 6 m tall, closed all round), its walls HDRP Lit with its three maps.
        put(quadHandle, translate_scale({0, 0, 0}, {12, 1, 12}), 101, 0);
        put(quadHandle, mul(translate_scale({0, 3, 6}, {12, 6, 1}), rot_x(-pi/2)), 101, 0);
        put(quadHandle, mul(translate_scale({-6, 3, 0}, {1, 6, 12}), rot_z(-pi/2)), 101, 0);
        put(quadHandle, mul(translate_scale({6, 3, 0}, {1, 6, 12}), rot_z(pi/2)), 101, 0);
        put(quadHandle, mul(translate_scale({0, 6, 0}, {12, 1, 12}), rot_x(pi)), 101, 0);
        put(quadHandle, mul(translate_scale({0, 3, -6}, {12, 6, 1}), rot_x(pi/2)), 101, 0);
        for(int i=0;i<6;++i) put(cubeHandle, translate_scale({-4.5f+1.8f*float(i), 0.5f+0.25f*float(i%3), 3.f}, {1.f, 1.f+0.5f*float(i%3), 1.f}), 101, 0);
        put(cubeHandle, translate_scale({-3.f, 0.4f, -1.f}, {0.8f, 0.8f, 0.8f}), 104, TGPT_INSTANCE_ALPHA_TEST); // clear glass
        put(cubeHandle, translate_scale({-1.2f, 0.4f, -1.f}, {0.8f, 0.8f, 0.8f}), 105, 0);                       // wax
        put(cubeHandle, translate_scale({0.6f, 0.4f, -1.f}, {0.8f, 0.8f, 0.8f}), 111, 0);                        // smooth metal
        put(quadHandle, mul(translate_scale({2.4f, 1.f, -1.f}, {1.2f, 1.6f, 1.f}), rot_x(-pi/2)), 110, TGPT_INSTANCE_ALPHA_TEST); // a hair card, facing the camera
        put(quadHandle, mul(translate_scale({3.9f, 1.2f, 0.f}, {1.2f, 1.6f, 1.f}), rot_x(-pi/2)), 114, TGPT_INSTANCE_ALPHA_TEST); // thin cloth, hanging
        put(quadHandle, mul(translate_scale({-5.98f, 3, 2}, {1, 1.2f, 2}), rot_z(-pi/2)), 112, 0);                // the glowing panel on the left wall
        put(quadHandle, mul(translate_scale({1.5f, 3.2f, 5.97f}, {4, 3, 1}), rot_x(-pi/2)), 113, 0);              // the mirror on the back wall
        expect(SetScene(list.data(), uint32_t(list.size()), index.data(), uint32_t(index.size()))==0, "the no-sun check's room set");
    }
    const Camera ncam=make_camera({0, 2.5f, -5.5f}, {0, 1.5f, 3}, 60.f, float(W)/float(H));
    const bool keepSun=sunOn, keepOwn=ownRays; const int keepForced=noSunForced; const float keepSky=skyIntensity; const uint32_t keepBounces=bounces;
    ownRays=true; skyIntensity=1e-6f; bounces=1;
    struct Mean { std::vector<float> image; double whole=0, blocks[144]{}; uint64_t notFinite=0, above=0; float largest=0; };
    const uint32_t settle=32, taken=16;
    // sun: 0 on, 1 off as the checks leave it out, 2 off as the plugin sends it. base: the frames' index (other random numbers).
    auto mean=[&](int sun, uint32_t base, uint32_t options) {
        Mean m; std::vector<double> sum(size_t(W)*H*3, 0.0);
        sunOn=sun==0; noSunForced=sun==2 ? 2 : 0;
        for(uint32_t i=0;i<settle+taken;++i) {
            cut=i==0; run(TGPT_VIEW_NONE, ncam, ncam, i ? base+i : 0, options); cut=false;
            if(i<settle) continue;
            const auto px=readback();
            for(size_t j=0;j<px.size();j+=3) {
                bool high=false;
                for(int k=0;k<3;++k) {
                    const float v=px[j+size_t(k)];
                    if(!std::isfinite(v)) { ++m.notFinite; continue; }
                    sum[j+size_t(k)]+=v; m.largest=std::max(m.largest, v); high|=v>3e4f;
                }
                m.above+=high;
            }
        }
        m.image.resize(sum.size());
        for(size_t j=0;j<sum.size();++j) m.image[j]=float(sum[j]/double(taken));
        for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
            const float* p=&m.image[(size_t(y)*W+x)*3];
            const double l=0.2126*p[0]+0.7152*p[1]+0.0722*p[2];
            m.whole+=l; m.blocks[(y*9u/H)*16u+x*16u/W]+=l;
        }
        const double pixels=double(W)*double(H);
        m.whole/=pixels; for(double& b:m.blocks) b/=pixels/144.0;
        return m;
    };
    // How far two means part: the whole picture's (a share of it), and the block that parts most (a share of that block;
    // a block darker than a fiftieth of the picture counts with the picture's fiftieth as its measure).
    struct Parted { double whole, block; int at; };
    auto parted=[&](const Mean& a, const Mean& b) {
        Parted p{std::abs(a.whole-b.whole)/std::max(b.whole, 1e-12), 0.0, 0};
        for(int k=0;k<144;++k) {
            const double d=std::abs(a.blocks[k]-b.blocks[k])/std::max(std::max(a.blocks[k], b.blocks[k]), 0.02*b.whole+1e-12);
            if(d>p.block) { p.block=d; p.at=k; }
        }
        return p;
    };
    struct Arrangement { const char* name; uint32_t options; bool fog; const char* png; bool grid=false; };
    const uint32_t game=TGPT_OPTION_EMISSIVE_LIGHTS;
    const Arrangement arrangements[]={{"no denoiser", game|TGPT_OPTION_NO_DENOISER, false, "nosun-none.png"}, {"NRD", game|TGPT_OPTION_NRD, false, "nosun-nrd.png"},
                                      {"HDRP's fog traced, seven fog boxes, no denoiser", game|TGPT_OPTION_NO_DENOISER, true, "nosun-fog.png"},
                                      {"HDRP's fog traced by the grid's cells, seven fog boxes, no denoiser", game|TGPT_OPTION_NO_DENOISER, true, "nosun-fog-grid.png", true}};
    uint32_t base=20000;
    for(const Arrangement& a:arrangements) {
        if(a.fog) {
            fogExtinction=1.f/39.9f; fogBase=100.f; fogRcpScale=0.02f; maxFogDistance=80.f; fogColorSent[3]=80.f;
            fogFrameFlags=TGPT_FRAME_TRACED_FOG|TGPT_FRAME_FOG_WALK; // (the cells are the camera's only fog since 2026-10-12: a.grid chooses nothing)
            fogColorSent[0]=0.6f; fogColorSent[1]=0.8f; fogColorSent[2]=0.9f; // (what the fog scatters: no fog medium is sent, so its colour's rgb)
            TGPT_FogBox boxes[7]{};
            for(uint32_t i=0;i<7;++i) {
                auto& box=boxes[i]; box.center[0]=-4.5f+1.5f*float(i); box.center[1]=1.5f+0.4f*float(i%3); box.center[2]=1.f-0.5f*float(i%2);
                box.extinction=0.2f+0.1f*float(i%3); box.right[0]=box.up[1]=1.f; box.endTimesRcpDistFadeLen=1.f; box.mask=-1; box.maskMost=1.f;
                for(int k=0;k<3;++k) { box.extents[k]=0.7f+0.2f*float((i+uint32_t(k))%3u); box.albedo[k]=0.8f; box.rcpPosFaceFade[k]=box.rcpNegFaceFade[k]=4.f; box.tiling[k]=1.f; }
            }
            expect(SetFogBoxes && SetFogBoxes(boxes, 7)==0, "the no-sun check's seven fog boxes set");
        }
        const Mean sun=mean(0, base, a.options), off=mean(1, base, a.options), again=mean(1, base+5000, a.options), zero=mean(2, base, a.options);
        base+=10000;
        const Parted noise=parted(again, off), withSun=parted(sun, off), asSent=parted(zero, off);
        const uint64_t notFinite=sun.notFinite+off.notFinite+again.notFinite+zero.notFinite, above=sun.above+off.above+again.above+zero.above;
        ReadStatus(&status);
        std::printf("NOSUN %s (%s passes): the picture's mean with the sun %.6f, without %.6f, without again %.6f, without as the plugin sends it %.6f; the largest value %.1f / %.1f / %.1f / %.1f; "
                    "not finite %llu, above 30,000 %llu\n", a.name, (status.features&TGPT_FEATURE_DXR12) ? "Shader Model 6.9" : "Shader Model 6.6", sun.whole, off.whole, again.whole, zero.whole,
                    double(sun.largest), double(off.largest), double(again.largest), double(zero.largest), (unsigned long long)notFinite, (unsigned long long)above);
        std::printf("      %s: parted from \"without\", whole picture / worst block (its place): the noise %.3f %% / %.2f %% (%d,%d); with the sun %.3f %% / %.2f %% (%d,%d); as the plugin sends it %.3f %% / %.2f %% (%d,%d)\n",
                    a.name, 100.0*noise.whole, 100.0*noise.block, noise.at%16, noise.at/16, 100.0*withSun.whole, 100.0*withSun.block, withSun.at%16, withSun.at/16,
                    100.0*asSent.whole, 100.0*asSent.block, asSent.at%16, asSent.at/16);
        save(off.image, a.png, true);
        expect(notFinite==0 && above==0, "without a sun, and with one, nothing is not finite and nothing is above 30,000");
        expect(off.whole>1e-4, "the room is lit (the check sees a picture)");
        expect(withSun.whole<=3.0*noise.whole+0.01 && withSun.block<=3.0*noise.block+0.02, "the sun's code skipped gives the picture its code run gives (the room's walls stop the sun)");
        expect(asSent.whole<=3.0*noise.whole+0.01 && asSent.block<=3.0*noise.block+0.02, "no sun as the plugin sends it (every number 0) gives the same picture");
        if(a.fog) { SetFogBoxes(nullptr, 0); fogExtinction=fogBase=fogRcpScale=0.f; maxFogDistance=0.f; for(float& v:fogColorSent) v=0.f; fogFrameFlags=0; }
    }
    ReadStatus(&status);
    expect(status.framesFailed==0, "the no-sun frames completed without a failed trace");
    sunOn=keepSun; ownRays=keepOwn; noSunForced=keepForced; skyIntensity=keepSky; bounces=keepBounces;
    std::printf(failures>failuresBefore ? "FAILED %d no-sun checks\n" : "no-sun checks passed\n", failures-failuresBefore);
    return failures ? 1 : 0;
}
