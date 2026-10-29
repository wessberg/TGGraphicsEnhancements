// SPDX-License-Identifier: MIT
// PT-76. Included inside the real GPU harness: where the whole test always had these checks, and by the glass-only run
// (TGPT_TEST_GLASS_ONLY) after the glass fixtures. Needs: index (the next frame index), the room's scene and materials.
        // Coloured glass tints the light through it (transmittance, pt_common.hlsli) by its material's own absorption
        // (PT-76: HDRP's transmittance color over its absorption distance; until 2026-10-09 by its base color, which is paint
        // on glass, not the glass): a traced-only cube of green glass, 0.8 m of which leaves (0.15, 0.6, 0.15) of the light,
        // floating over the sunlit floor lets through, of the direct light the same cube opaque stops there, what its
        // absorption leaves over each ray's way inside it (0.8 m straight down, less where a ray cuts a corner: worked out
        // here over the rays the cube stops), with the sun alone and with a lamp alone above it (RTXDI's final shading);
        // grey glass (0.36 over 0.8 m) the same in every channel, clear glass all of it. And at path vertices: under a
        // traced-only sheet of it over the ceiling's opening (an open sheet: a thin pane 0.4 m thick, which leaves the
        // square root of those colors), the bounced light under the ceiling turns green. Undenoised, unclamped, without
        // the boiling filters, averaged over 32 frames with the same frame indices for every variant, so only the material
        // differs. In this file so that the glass-only run (TGPT_TEST_GLASS_ONLY) has them too: the whole test includes
        // it where the checks always stood.
        {
            const uint32_t first=index;
            const uint32_t green=uint32_t(materials.size());
            TGPT_Material greenGlass=materials[9];
            { const double left[3]={0.15, 0.6, 0.15}; for(int c=0;c<3;++c) greenGlass.absorption[c]=float(-std::log(left[c])/0.8); }
            expect(SetMaterials(green, &greenGlass, 1)==0, "green glass set");
            // Tinted along straight lines (TGPT_OPTION_NO_CAUSTICS): through a cube's hard edges most of the light is bent away
            // from its straight shadow (the caustic's own test is the glass ball's).
            const uint32_t honest=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_CAUSTICS;
            TGPT_Light lamp{}; lamp.position[0]=-0.3f; lamp.position[1]=3.2f; lamp.position[2]=2.f; lamp.type=0;
            lamp.color[0]=lamp.color[1]=lamp.color[2]=30.f; lamp.range=10; lamp.radius=0.05f; lamp.id=400;
            // material <0: none; 0 the opaque white one. sheet: the sheet over the ceiling's opening instead of the cube.
            auto averaged=[&](int material, bool sheet, bool lampOnly, uint32_t view) {
                auto list=instances; auto listIndices=indices;
                if(material>=0) {
                    TGPT_Instance t{};
                    const M4 w=sheet ? translate_scale({0.f, 5.9f, 4.f}, {12.f, 1.f, 4.f}) : translate_scale({-0.3f, 1.f, 2.f}, {0.8f, 0.8f, 0.8f});
                    for(int r=0;r<3;++r) for(int c=0;c<4;++c) t.transform[r*4+c]=w.m[r][c];
                    t.mesh=sheet ? quadHandle : cubeHandle; t.materialBase=uint32_t(listIndices.size()); t.mask=0xFF;
                    t.flags=material==0 ? 0u : TGPT_INSTANCE_ALPHA_TEST;
                    for(int s=0;s<(sheet ? 1 : 2);++s) listIndices.push_back(uint32_t(material));
                    list.push_back(t);
                }
                expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "scene with glass over the floor");
                sunOn=!lampOnly;
                expect(lampOnly ? SetLights(&lamp, 1)==0 : SetLights(nullptr, 0)==0, lampOnly ? "the lamp above the cube alone" : "the sun alone");
                std::vector<double> acc(size_t(W)*H*3, 0.0);
                index=first;
                for(uint32_t i=0;i<40;++i) {
                    run(view, cam, cam, i ? index++ : 0, honest);
                    if(i>=8) { const auto r=readback(); for(size_t j=0;j<acc.size();++j) acc[j]+=r[j]/32.0; }
                }
                return acc;
            };
            auto lum=[](const std::vector<double>& v, size_t j) { return 0.2126*v[j]+0.7152*v[j+1]+0.0722*v[j+2]; };
            // What a closed cube of glass lets through along straight lines: its absorption (per metre) over each ray's way
            // inside it, averaged over the rays the cube stops on their way to the floor (points of the floor round its shadow,
            // each one's ray towards the sun or the lamp: the way inside the cube by its three pairs of faces).
            auto leaves=[&](const float* sigma, bool lampOnly) {
                const V3 centre{-0.3f, 1.f, 2.f}; const double half=0.4;
                std::array<double, 3> sum{}; double count=0;
                uint32_t state=12345u;
                auto next=[&]() { state=state*1664525u+1013904223u; return double(state>>8)/16777216.0; };
                for(int i=0;i<200000;++i) {
                    const V3 p{centre.x+float(next()*4.0-2.0), 0.f, centre.z+float(next()*4.0-2.0)};
                    const V3 d=lampOnly ? normalize(V3{lamp.position[0], lamp.position[1], lamp.position[2]}-p) : sunDir;
                    const double o[3]={double(p.x-centre.x), double(p.y-centre.y), double(p.z-centre.z)}, dd[3]={d.x, d.y, d.z};
                    double t0=0, t1=1e30; bool hit=true;
                    for(int k=0;k<3 && hit;++k) {
                        if(std::fabs(dd[k])<1e-9) { hit=std::fabs(o[k])<half; continue; }
                        double near0=(-half-o[k])/dd[k], far0=(half-o[k])/dd[k]; if(near0>far0) std::swap(near0, far0);
                        t0=std::max(t0, near0); t1=std::min(t1, far0); hit=t1>t0;
                    }
                    if(!hit) continue;
                    for(int c=0;c<3;++c) sum[c]+=std::exp(-double(sigma[c])*(t1-t0));
                    count+=1;
                }
                return std::array<double, 3>{sum[0]/std::max(count, 1.0), sum[1]/std::max(count, 1.0), sum[2]/std::max(count, 1.0)};
            };
            // Of the direct light the opaque cube stops (where it stops most of it), the part the glass one lets through.
            auto through=[&](const std::vector<double>& none, const std::vector<double>& opaque, const std::vector<double>& glass, int& pixels) {
                double blocked[3]{}, passed[3]{}; pixels=0;
                for(size_t j=0;j+2<none.size();j+=3) {
                    if(!(lum(none, j)>0.02) || lum(opaque, j)>0.3*lum(none, j)) continue;
                    ++pixels;
                    for(int c=0;c<3;++c) { blocked[c]+=none[j+c]-opaque[j+c]; passed[c]+=glass[j+c]-opaque[j+c]; }
                }
                return std::array<double, 3>{passed[0]/std::max(blocked[0], 1e-9), passed[1]/std::max(blocked[1], 1e-9), passed[2]/std::max(blocked[2], 1e-9)};
            };
            for(int lampOnly=0;lampOnly<2;++lampOnly) {
                const auto none=averaged(-1, false, lampOnly, TGPT_VIEW_DIRECT), opaque=averaged(0, false, lampOnly, TGPT_VIEW_DIRECT);
                const auto greenCube=averaged(int(green), false, lampOnly, TGPT_VIEW_DIRECT);
                const auto greyCube=averaged(9, false, lampOnly, TGPT_VIEW_DIRECT), clearCube=averaged(10, false, lampOnly, TGPT_VIEW_DIRECT);
                if(!lampOnly) { std::vector<float> f(greenCube.size()); for(size_t j=0;j<f.size();++j) f[j]=float(greenCube[j]); save(f, "35-green-glass-shadow.png", true); }
                int pixels=0;
                const auto tg=through(none, opaque, greenCube, pixels), ty=through(none, opaque, greyCube, pixels), tc=through(none, opaque, clearCube, pixels);
                const char* light=lampOnly ? "a lamp" : "the sun";
                const auto eg=leaves(greenGlass.absorption, lampOnly!=0), ey=leaves(materials[9].absorption, lampOnly!=0);
                std::printf("      glass cube's shadow from %s (%d pixels), of the light an opaque one stops: green %.3f %.3f %.3f (%.3f %.3f %.3f by its absorption over the way inside), grey %.3f %.3f %.3f (%.3f), clear %.3f %.3f %.3f\n",
                    light, pixels, tg[0], tg[1], tg[2], eg[0], eg[1], eg[2], ty[0], ty[1], ty[2], ey[0], tc[0], tc[1], tc[2]);

                expect(pixels>int(300.0*W*H/(1280.0*720.0)), lampOnly ? "an opaque cube shadows the floor from the lamp above it" : "an opaque cube shadows the floor from the sun");
                // The pixels are those the opaque cube shades by 70 % and more: the shadow's outline, which a pixel only half
                // covers, is left out, and there the way inside the glass is shortest (a corner cut). At the glass-only run's
                // 320x180 that outline is a large part of a shadow 12 pixels across, and what is measured lies under the
                // worked-out mean over all of it (2026-10-09, a lamp: grey 0.443 against 0.524, green's red 0.237 against 0.339):
                // looser there; at full size it is a few pixels in thousands.
                const double wide=opticalOnly ? 0.14 : 0.08, narrow=opticalOnly ? 0.12 : 0.06;
                expect(std::fabs(tg[1]-eg[1])<wide && std::fabs(tg[0]-eg[0])<narrow && std::fabs(tg[2]-eg[2])<narrow && tg[0]/tg[1]<0.6,
                       lampOnly ? "green glass tints a lamp's light through it green, channel by channel" : "green glass tints the sun's light through it green, channel by channel");
                expect(std::fabs(ty[0]-ey[0])<wide && std::fabs(ty[1]-ey[1])<wide && std::fabs(ty[2]-ey[2])<wide, "grey glass lets through as much of every channel as its absorption leaves over the way inside it");
                expect(tc[0]>0.93 && tc[1]>0.93 && tc[2]>0.93, "clear glass lets the light through");
            }
            // Path vertices: the bounced light under the ceiling (read-back rows are bottom-up), lit by the floor and walls the
            // sun reaches through the sheet (a single crossing: the square root of its color, twice the green against red),
            // without the sky, whose light comes in through the room's open front as well. The emissive panel's light (warm)
            // doesn't come through the sheet: 1.6 times measured, 1.0 with glass passed by chance as before.
            auto tint=[&](const std::vector<double>& v) {
                double r=0, gr=0;
                for(UINT y=650*H/720;y<700*H/720;++y) for(UINT x=500*W/1280;x<780*W/1280;++x) { const size_t j=(size_t(y)*W+x)*3; r+=v[j]; gr+=v[j+1]; }
                std::printf("        (its red %.5f and green %.5f a pixel)\n", r/std::max(1.0, double((700*H/720-650*H/720)*(780*W/1280-500*W/1280))),
                            gr/std::max(1.0, double((700*H/720-650*H/720)*(780*W/1280-500*W/1280))));
                return gr/std::max(r, 1e-9);
            };
            skyIntensity=1e-6f; // 0 would mean the default, 1
            const double open=tint(averaged(-1, true, false, TGPT_VIEW_INDIRECT)), greenSheet=tint(averaged(int(green), true, false, TGPT_VIEW_INDIRECT));
            const double greySheet=tint(averaged(9, true, false, TGPT_VIEW_INDIRECT));
            skyIntensity=1;
            std::printf("      bounced light under the ceiling, green/red: open %.3f, under a sheet of green glass %.3f (%.2f times), of grey glass %.3f (%.2f times)\n",
                open, greenSheet, greenSheet/std::max(open, 1e-9), greySheet, greySheet/std::max(open, 1e-9));
            expect(greenSheet>open*1.35, "light through green glass reaches path vertices green (bounced light)");
            expect(std::fabs(greySheet/std::max(open, 1e-9)-1.0)<0.1, "grey glass leaves the bounced light's color");
            sunOn=true;
            expect(SetLights(&light, 1)==0, "one light again");
            expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "scene without the glass");
        }
