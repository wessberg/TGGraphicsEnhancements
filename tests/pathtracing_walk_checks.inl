// SPDX-License-Identifier: MIT
// PT-40, PT-73 c: who takes the scattering walk (docs/agents/who-walks.md). Alone with TGPT_TEST_WALKS_ONLY=1 (right
// after the warm-up), and in the whole test. Own camera rays, no denoiser, no reuse, no cache.
//   The game scatters light under a surface only where the material's subsurface feature is on, and as much as its mask
//   says there (0 to 1): TGPT_MATERIAL_SUBSURFACE with TGPT_Material.subsurfaceMask, or what the material's program
//   computes at the point (RealSkin). A material drawn from both sides walks only on the pieces of its mesh that close a
//   volume (scene.cpp mark_closed), one drawn one-sided as before.
//   Measured on a wax cylinder 4 cm thick with a lamp behind it: its side towards the camera is black as a plain surface
//   and lit only by what a walk brings through, so "how much walks" shows as light. And counted: TGPT_GetWalkPixels says
//   how many pixels stopped on a material light walks inside, and how many of them took the walk in a frame.
{
    std::printf("[walks] who takes the scattering walk: the game's mask, closed pieces, materials drawn from both sides\n");
    const auto objectsBefore=objects;
    const bool ownBefore=ownRays, sunBefore=sunOn; const float skyBefore=skyIntensity; const uint32_t bouncesBefore=bounces;
    ownRays=true; sunOn=false; skyIntensity=1e-6f; bounces=1; objects.clear();
    const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_SPATIAL|TGPT_OPTION_NO_TEMPORAL|TGPT_OPTION_NO_CLAMPS;
    const V3 centre{0, 1.5f, 0};
    const float aspect=float(W)/float(H), fov=60.f;
    // A 16-sided cylinder (2 cm radius, 6 cm tall), flat faces with their own normals, wound as the cube. caps: closed by
    // its two ends (without: an open tube). card: a quad of 2 x 4 cm beside it, in the same submesh, its triangles FIRST
    // (mark_closed has to put the cylinder's in front of them).
    auto prism=[&](bool caps, bool card) {
        const int sides=16; const float radius=0.02f, height=0.06f;
        std::vector<Vertex> v; std::vector<uint16_t> i;
        if(card) {
            const float x0=0.045f, x1=0.065f, y=0.02f;
            const uint16_t base=uint16_t(v.size());
            v.push_back({x0, -y, 0, 0, 0, -1, 0, 0}); v.push_back({x0, y, 0, 0, 0, -1, 0, 1}); v.push_back({x1, y, 0, 0, 0, -1, 1, 1}); v.push_back({x1, -y, 0, 0, 0, -1, 1, 0});
            i.insert(i.end(), {base, uint16_t(base+1), uint16_t(base+2), base, uint16_t(base+2), uint16_t(base+3)});
        }
        // The rim's 16 corners, each computed once: the sides and the ends meet in the very same numbers (the tracer
        // takes vertices at one place for one, and these are).
        const double step=2.0*3.14159265358979/sides;
        const float top=0.5f*height;
        std::vector<std::array<float,2>> ring(sides);
        for(int k=0;k<sides;++k) { const double a=step*(k+0.5); ring[size_t(k)]={float(radius*std::cos(a)), float(radius*std::sin(a))}; }
        for(int s=0;s<sides;++s) {
            const V3 n{float(std::cos(step*s)), 0, float(std::sin(step*s))};
            const auto a=ring[size_t((s+sides-1)%sides)], b=ring[size_t(s)];
            const uint16_t base=uint16_t(v.size());
            v.push_back({a[0], -top, a[1], n.x, n.y, n.z, 0, 0}); v.push_back({a[0], top, a[1], n.x, n.y, n.z, 0, 0});
            v.push_back({b[0], top, b[1], n.x, n.y, n.z, 0, 0}); v.push_back({b[0], -top, b[1], n.x, n.y, n.z, 0, 0});
            i.insert(i.end(), {base, uint16_t(base+1), uint16_t(base+2), base, uint16_t(base+2), uint16_t(base+3)});
        }
        if(caps) for(int e=-1;e<=1;e+=2) {
            const uint16_t base=uint16_t(v.size());
            v.push_back({0, float(e)*top, 0, 0, float(e), 0, 0, 0});
            for(int k=0;k<sides;++k) { const auto p=ring[size_t(e>0 ? sides-1-k : k)]; v.push_back({p[0], float(e)*top, p[1], 0, float(e), 0, 0, 0}); }
            for(int k=0;k<sides;++k) i.insert(i.end(), {base, uint16_t(base+1+k), uint16_t(base+1+(k+1)%sides)});
        }
        return make_mesh(v, {i});
    };
    MeshBuffers closedMesh=prism(true, false), tubeMesh=prism(false, false), mixedMesh=prism(true, true);
    const int closedHandle=register_mesh(closedMesh, 781), tubeHandle=register_mesh(tubeMesh, 782), mixedHandle=register_mesh(mixedMesh, 783);
    expect(closedHandle>0 && tubeHandle>0 && mixedHandle>0, "a closed cylinder, an open tube and a cylinder with a card beside it registered");
    // The wax of the test's other checks: scattering 10, 7 and 5 mm.
    TGPT_Material wax=materials[0]; wax.flags=TGPT_MATERIAL_SUBSURFACE; wax.subsurfaceMask=1.f;
    wax.subsurface[0]=100.f; wax.subsurface[1]=1.f/0.007f; wax.subsurface[2]=200.f;
    auto lampAt=[&](V3 p, float intensity, float range) {
        TGPT_Light l{}; l.position[0]=p.x; l.position[1]=p.y; l.position[2]=p.z; l.type=0;
        l.color[0]=l.color[1]=l.color[2]=intensity; l.range=range; l.radius=0.01f; l.id=431;
        expect(SetLights(&l, 1)==0, "a lamp set");
    };
    // One thing in the traced scene, with one material on each of its submeshes (and, with roof, a cube of the same
    // material placed by it: what the first thing's bounced light meets); built over ten frames.
    auto walkScene=[&](int mesh, M4 world, const TGPT_Material& material, int submeshes, const Camera& c, uint32_t seed, const M4* roof=nullptr) {
        expect(SetMaterials(75, &material, 1)==0, "the walk check's material");
        TGPT_Instance q[2]{};
        for(int r=0;r<3;++r) for(int k=0;k<4;++k) { q[0].transform[r*4+k]=world.m[r][k]; if(roof) q[1].transform[r*4+k]=roof->m[r][k]; }
        q[0].mesh=uint32_t(mesh); q[0].mask=0xFF;
        q[1].mesh=uint32_t(cubeHandle); q[1].mask=0xFF; q[1].materialBase=uint32_t(submeshes);
        const uint32_t ids[6]={75, 75, 75, 75, 75, 75};
        expect(SetScene(q, roof ? 2u : 1u, ids, uint32_t(submeshes)+(roof ? 2u : 0u))==0, "the walk check's scene");
        for(uint32_t i=0;i<10;++i) { run(TGPT_VIEW_NONE, c, c, seed+i, raw); context->Flush(); Sleep(20); }
    };
    // The mean of a view over frames, in a window about the screen's middle (wx, wy: its half size in pixels). spread: how
    // far one pixel of one frame lies from that mean (the standard deviation of its luminance over the mean's): the grain
    // of the picture with no denoiser.
    auto windowMean=[&](uint32_t view, const Camera& c, uint32_t first, uint32_t frames, uint32_t more, int wx, int wy, double* spread=nullptr) {
        std::array<double,3> sum{}; size_t count=0; double light=0, squares=0;
        for(uint32_t i=0;i<frames+4;++i) {
            run(view, c, c, first+i, raw|more);
            if(i<4) continue;
            const auto r=readback();
            for(int y=int(H)/2-wy;y<int(H)/2+wy;++y) for(int x=int(W)/2-wx;x<int(W)/2+wx;++x) {
                const size_t p=(size_t(y)*W+size_t(x))*3; for(int k=0;k<3;++k) sum[k]+=r[p+k]; ++count;
                const double l=0.2126*r[p]+0.7152*r[p+1]+0.0722*r[p+2]; light+=l; squares+=l*l;
            }
        }
        const double n=double(std::max<size_t>(count, 1));
        for(auto& s:sum) s/=n;
        if(spread) *spread=light>0 ? std::sqrt(std::max(0.0, squares/n-(light/n)*(light/n)))/(light/n) : 0.0;
        return sum;
    };
    // Who walked, summed over some frames of the yardstick's view: the pixels on a material light walks inside, and those
    // of them that took the walk. False where the tracer has no such count (a tracer from before this job).
    auto counted=[&](const Camera& c, uint32_t base, uint32_t frames, uint32_t more, double& on, double& walked) {
        on=walked=0;
        if(!GetWalkPixels) return false;
        for(uint32_t k=0;k<frames;++k) {
            const uint32_t index=base+k*16;
            run(TGPT_VIEW_YARDSTICK, c, c, index, raw|more);
            TGPT_Yardstick y{sizeof(TGPT_Yardstick), TGPT_ABI};
            uint32_t frame=0, a=0, b=0;
            if(!yardstickOf(index, c, raw|more, y, nullptr) || GetWalkPixels(&frame, &a, &b)!=0 || frame!=index) return false;
            on+=a; walked+=b;
        }
        return true;
    };
    const int wx=int(H*0.035f), wy=int(H*0.1f);
    const V3 behind{centre.x, centre.y, centre.z+0.25f}, eye{centre.x, centre.y, centre.z-0.16f};
    const Camera c=make_camera(eye, centre, fov, aspect);
    const M4 at=translate_scale(centre, {1, 1, 1});
    auto lum=[](const std::array<double,3>& v) { return 0.2126*v[0]+0.7152*v[1]+0.0722*v[2]; };

    // 1. The mask. The closed cylinder, drawn one-sided, lit from behind: with a mask of 1 its near side takes what the
    // walk brings through; with 0.5, half of that (the mean of the walk and the plain surface, which is black there); with 0
    // nothing, and no pixel walks.
    double full=0, onFull=0, walkedFull=0;
    {
        walkScene(closedHandle, at, wax, 1, c, 20000); lampAt(behind, 2.f, 3.f);
        double grainOne=0, grainHalf=0;
        const auto one=windowMean(TGPT_VIEW_DIRECT, c, 20020, 48, 0u, wx, wy, &grainOne);
        const auto plain=windowMean(TGPT_VIEW_DIRECT, c, 20020, 8, TGPT_OPTION_NO_SUBSURFACE, wx, wy);
        const bool countedOne=counted(c, 20100, 4, 0u, onFull, walkedFull);
        full=lum(one);
        TGPT_Material half=wax; half.subsurfaceMask=0.5f;
        walkScene(closedHandle, at, half, 1, c, 20200);
        const auto mid=windowMean(TGPT_VIEW_DIRECT, c, 20220, 96, 0u, wx, wy, &grainHalf);
        double onHalf=0, walkedHalf=0; const bool countedHalf=counted(c, 20400, 8, 0u, onHalf, walkedHalf);
        // The grain where the plain surface is black (the worst case: a pixel that does not walk shows nothing).
        std::printf("      one pixel of one frame against the mean, no denoiser (standard deviation over the mean): mask 1 %.2f, mask 0.5 %.2f\n", grainOne, grainHalf);
        TGPT_Material none=wax; none.subsurfaceMask=0.f;
        walkScene(closedHandle, at, none, 1, c, 20600);
        const auto zero=windowMean(TGPT_VIEW_DIRECT, c, 20620, 16, 0u, wx, wy);
        double onNone=0, walkedNone=0; const bool countedNone=counted(c, 20700, 4, 0u, onNone, walkedNone);
        std::printf("      a wax cylinder lit from behind, its near side (%d x %d pixels): mask 1 %.5f, mask 0.5 %.5f (%.3f of it), mask 0 %.6f, as a plain surface %.6f; "
                    "pixels on the material / that walked a frame: mask 1 %.0f / %.0f, mask 0.5 %.0f / %.0f (%.3f), mask 0 %.0f / %.0f\n",
                    2*wx, 2*wy, full, lum(mid), lum(mid)/full, lum(zero), lum(plain), onFull/4, walkedFull/4, onHalf/8, walkedHalf/8, walkedHalf/std::max(onHalf, 1.0), onNone/4, walkedNone/4);
        expect(full>0 && lum(plain)<0.02*full, "the cylinder's near side is lit by the walk alone (black as a plain surface)");
        bool channels=true; for(int k=0;k<3;++k) channels=channels && std::fabs(mid[k]/one[k]-0.5)<0.04;
        expect(channels, "a mask of 0.5 gives the mean of the walk and the plain surface, in each channel");
        expect(lum(zero)<0.02*full, "a mask of 0 is a plain surface");
        expect(countedOne && countedHalf && countedNone, "the tracer counts who walks (TGPT_GetWalkPixels)");
        expect(onFull>4000 && walkedFull>0.9*onFull && walkedFull<=onFull, "with a mask of 1 every pixel that meets the wax from outside walks");
        expect(onHalf>8000 && std::fabs(walkedHalf/onHalf-0.5*walkedFull/onFull)<0.01, "with a mask of 0.5 half of them walk in a frame");
        expect(onNone>4000 && walkedNone==0, "with a mask of 0 none walks, though the material names a profile");
    }
    // 2. Drawn from both sides. The same closed cylinder with a two-sided material walks as the one-sided one; with a card of
    // the same material beside it in the same submesh the cylinder still walks and the card does not; an open tube (no
    // ends) does not walk at all: lit from the front, it is lit exactly as a plain surface.
    {
        TGPT_Material both=wax; both.flags|=TGPT_MATERIAL_DOUBLE_SIDED;
        walkScene(closedHandle, at, both, 1, c, 21000); lampAt(behind, 2.f, 3.f);
        const auto closed=windowMean(TGPT_VIEW_DIRECT, c, 21020, 48, 0u, wx, wy);
        double onClosed=0, walkedClosed=0; const bool countedClosed=counted(c, 21100, 4, 0u, onClosed, walkedClosed);
        walkScene(mixedHandle, at, both, 1, c, 21200);
        const auto mixed=windowMean(TGPT_VIEW_DIRECT, c, 21220, 48, 0u, wx, wy);
        double onMixed=0, walkedMixed=0; const bool countedMixed=counted(c, 21300, 4, 0u, onMixed, walkedMixed);
        const V3 front{centre.x+0.05f, centre.y+0.05f, centre.z-0.30f};
        walkScene(tubeHandle, at, both, 1, c, 21400); lampAt(front, 2.f, 3.f);
        const auto tube=windowMean(TGPT_VIEW_DIRECT, c, 21420, 32, 0u, wx, wy);
        const auto tubePlain=windowMean(TGPT_VIEW_DIRECT, c, 21420, 32, TGPT_OPTION_NO_SUBSURFACE, wx, wy);
        double onTube=0, walkedTube=0; const bool countedTube=counted(c, 21500, 4, 0u, onTube, walkedTube);
        std::printf("      drawn from both sides: the closed cylinder's near side %.5f (%.3f of the one-sided one's); with a card beside it in its submesh %.5f (%.3f); "
                    "pixels on the material / walked: closed %.0f / %.0f, with the card %.0f / %.0f; an open tube lit from the front %.5f, as a plain surface %.5f (%.4f), %.0f / %.0f\n",
                    lum(closed), lum(closed)/full, lum(mixed), lum(mixed)/full, onClosed/4, walkedClosed/4, onMixed/4, walkedMixed/4,
                    lum(tube), lum(tubePlain), lum(tube)/std::max(lum(tubePlain), 1e-12), onTube/4, walkedTube/4);
        expect(std::fabs(lum(closed)/full-1.0)<0.04, "a closed wax cylinder drawn from both sides is lit as the one-sided one");
        expect(countedClosed && onClosed>4000 && std::fabs(walkedClosed/walkedFull-1.0)<0.01, "a closed thing drawn from both sides walks from its front as the one-sided one does");
        expect(std::fabs(lum(mixed)/full-1.0)<0.04, "a closed body with an open card in its submesh still walks (closed is asked piece by piece)");
        expect(countedMixed && std::fabs(walkedMixed/walkedClosed-1.0)<0.01 && onMixed>onClosed+4.0*500.0, "the card beside it is a sheet: on the material, and no walk");
        expect(lum(tubePlain)>0 && std::fabs(lum(tube)/lum(tubePlain)-1.0)<0.02, "an open sheet drawn from both sides is lit as before: a plain surface");
        expect(countedTube && onTube>4000 && walkedTube==0, "no pixel of an open sheet drawn from both sides walks");
    }
    // 3. A metal plate with the skin's profile and a mask of 0 (what clothes and armour carried: a profile named, the
    // feature off, and until this job a walk at every bounce and reflection). It is lit as the same metal without the
    // profile, and nothing walks. And what the direct light's stage costs with a material over the whole view, by its mask.
    {
        const V3 size{0.30f, 0.12f, 0.30f}, top{centre.x, centre.y+0.06f, centre.z}, lamp{centre.x, centre.y+1.26f, centre.z};
        const Camera above=make_camera({centre.x, centre.y+0.51f, centre.z-0.04f}, top, fov, aspect);
        const Camera close=make_camera({centre.x, centre.y+0.20f, centre.z-0.02f}, top, fov, aspect);
        TGPT_Material metal=materials[0]; metal.flags=0; metal.metallic=1.f; metal.smoothness=0.4f;
        metal.baseColor[0]=0.80f; metal.baseColor[1]=0.78f; metal.baseColor[2]=0.74f;
        TGPT_Material skinned=metal; skinned.flags=TGPT_MATERIAL_SUBSURFACE; skinned.subsurfaceMask=0.f;
        skinned.subsurface[0]=3333.3333f; skinned.subsurface[1]=6250.f; skinned.subsurface[2]=11111.1111f;
        const M4 plate=translate_scale(centre, size);
        const int w2=int(H*0.08f);
        walkScene(cubeHandle, plate, metal, 2, above, 22000); lampAt(lamp, 20.f, 4.f);
        const auto bare=windowMean(TGPT_VIEW_NONE, above, 22020, 32, 0u, w2, w2);
        walkScene(cubeHandle, plate, skinned, 2, above, 22200);
        const auto named=windowMean(TGPT_VIEW_NONE, above, 22020, 32, 0u, w2, w2); // the same frames' numbers as the bare plate's
        double onPlate=0, walkedPlate=0; const bool countedPlate=counted(above, 22300, 4, 0u, onPlate, walkedPlate);
        std::printf("      a metal plate under a lamp (%d x %d pixels): %.5f %.5f %.5f as it is, %.5f %.5f %.5f naming the skin's profile with a mask of 0 (%.4f of it); "
                    "pixels on the material / walked %.0f / %.0f\n", 2*w2, 2*w2, bare[0], bare[1], bare[2], named[0], named[1], named[2], lum(named)/std::max(lum(bare), 1e-12), onPlate/4, walkedPlate/4);
        expect(lum(bare)>0 && std::fabs(lum(named)/lum(bare)-1.0)<0.02, "a metal plate that names the skin's profile with a mask of 0 is lit as a plain metal");
        expect(countedPlate && onPlate>4000 && walkedPlate==0, "a metal plate with a mask of 0 takes no walk");
        // The grain of skin lit from the front with no denoiser (the red dots on a face in game, run 20261008-034922): one
        // pixel of one frame against the mean, in the direct light, by the mask. A walk's sample is coloured by how far it
        // went (the blue is absorbed first); a pixel that stays plain that frame has the smooth light of the surface.
        {
            TGPT_Material skin=materials[0]; skin.flags=TGPT_MATERIAL_SUBSURFACE; skin.metallic=0; skin.albedoTile=-1;
            skin.baseColor[0]=0.60f; skin.baseColor[1]=0.35f; skin.baseColor[2]=0.22f;
            skin.subsurface[0]=3333.3333f; skin.subsurface[1]=6250.f; skin.subsurface[2]=11111.1111f;
            const float shares[3]={1.f, 0.27f, 0.f};
            double grain[3]={0, 0, 0}; std::array<double,3> light[3];
            for(int k=0;k<3;++k) {
                skin.subsurfaceMask=shares[k];
                walkScene(cubeHandle, plate, skin, 2, above, 22340+uint32_t(k)*20);
                light[k]=windowMean(TGPT_VIEW_DIRECT, above, 22020, 16, 0u, w2, w2, &grain[k]);
            }
            std::printf("      skin lit from the front, its direct light (red, green, blue) and one pixel's spread over the mean: mask 1 %.4f %.4f %.4f, %.3f; "
                        "mask 0.27 (60 of the game's skin materials) %.4f %.4f %.4f, %.3f; mask 0 (a plain surface) %.4f %.4f %.4f, %.3f\n",
                        light[0][0], light[0][1], light[0][2], grain[0], light[1][0], light[1][1], light[1][2], grain[1], light[2][0], light[2][1], light[2][2], grain[2]);
        }
        // What it costs: the plate over the whole view (from 14 cm), a slab of the same material 1 m square half a metre
        // over it (what the plate's bounced light meets: the walk at a path's vertices), a lamp between them. Every stage's
        // GPU time over about a second, by who walks. "Mask 1" on the metal is the rule before this job: a material that
        // named a profile walked, whatever its mask.
        const M4 roof=translate_scale({centre.x, centre.y+0.61f, centre.z}, {1.f, 0.1f, 1.f});
        auto stage=[&](const char* what, const TGPT_Material& material, uint32_t seed, uint32_t more) {
            walkScene(cubeHandle, plate, material, 2, close, seed, &roof);
            lampAt({centre.x+0.15f, centre.y+0.30f, centre.z}, 20.f, 4.f);
            for(uint32_t i=0;i<70;++i) { run(TGPT_VIEW_NONE, close, close, i ? seed+20+i : 0, more); context->Flush(); Sleep(15); }
            char stages[1024]{};
            const bool read=GetStageTimes && GetStageTimes(stages, sizeof(stages))>0;
            std::printf("      cost, %s: %s\n", what, read ? stages : "(no stage times)");
        };
        TGPT_Material walkedMetal=skinned; walkedMetal.subsurfaceMask=1.f;
        TGPT_Material halfWax=wax; halfWax.subsurfaceMask=0.5f;
        TGPT_Material noWax=wax; noWax.subsurfaceMask=0.f;
        stage("a plain metal", metal, 22400, 0u);
        stage("the metal naming the skin's profile, mask 0 (now: no walk)", skinned, 22500, 0u);
        stage("the metal naming the skin's profile, mask 1 (the rule before: it walked)", walkedMetal, 22600, 0u);
        stage("wax, mask 1", wax, 22700, 0u);
        stage("wax, mask 0.5", halfWax, 22800, 0u);
        stage("wax, mask 0", noWax, 22900, 0u);
        stage("wax, mask 1, the walk switched off", wax, 23000, TGPT_OPTION_NO_SUBSURFACE);
    }
    // 4. Skin's mask per point (TGPT_PROGRAM_SKIN, texture 12 and values 12 to 14): saturate(the region's scatter scale x the
    // material's scale x the subsurface map's red). The share of a skin panel's pixels that walk in a frame is that mask.
    if(RegisterSharedTexture && SetMaterialPrograms && GetWalkPixels) {
        std::vector<ComPtr<ID3D11Texture2D>> keep;
        auto texture=[&](uint32_t rgba) {
            std::vector<uint32_t> px(64*64, rgba);
            D3D11_TEXTURE2D_DESC d{64, 64, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
            D3D11_SUBRESOURCE_DATA initial{px.data(), 256, 0}; ComPtr<ID3D11Texture2D> t;
            expect(SUCCEEDED(device->CreateTexture2D(&d, &initial, &t)), "a skin mask texture created"); keep.push_back(t);
            TGPT_SharedTexture info{sizeof(TGPT_SharedTexture), TGPT_ABI};
            const auto result=RegisterSharedTexture(uint64_t(reinterpret_cast<uintptr_t>(t.Get())), &info);
            expect(result>=0, "a skin mask texture shared"); return result>=0 ? info.handle : -1;
        };
        // Red 128 of 255 in the subsurface map; regions: red 64, green 128 (the body's); the head's: blue 64 (upper lip), and
        // red 32 (neck), green 64 (ear).
        const int grey=texture(0xFF808080), sss=texture(0xFF000080), regions=texture(0x00008040), face=texture(0x00400000), earNeck=texture(0x00004020);
        std::vector<uint32_t> rows;
        struct Skin { bool head, masked; float scatter[4], faceScatter[4], rest[4]; };
        auto record=[&](const Skin& s) {
            const std::vector<int32_t> maps={grey, -1, -1, -1, s.masked ? regions : -1, -1, -1, -1, s.head ? face : -1, s.head ? earNeck : -1, -1, -1, sss};
            std::vector<std::array<float,4>> values={
                {1, 1, 1, s.head ? 1.f : 0.f}, {0.2f, 0.8f, 1.f, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {1, 0, 0, 25}, {1, 1, 1, 0},
                {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
            values.push_back({s.scatter[0], s.scatter[1], s.scatter[2], s.scatter[3]});
            values.push_back({s.faceScatter[0], s.faceScatter[1], s.faceScatter[2], s.faceScatter[3]});
            values.push_back({s.rest[0], s.rest[1], s.rest[2], s.rest[3]});
            const uint32_t start=uint32_t(rows.size()/4); uint32_t present=0;
            for(size_t i=0;i<maps.size();++i) if(maps[i]>=0) present|=1u<<i;
            rows.insert(rows.end(), {TGPT_PROGRAM_SKIN, uint32_t(maps.size()), uint32_t(values.size()), present});
            for(auto h:maps) rows.push_back(uint32_t(h));
            while(rows.size()%4) rows.push_back(0xFFFFFFFFu);
            for(const auto& v:values) for(float f:v) { uint32_t u; std::memcpy(&u, &f, 4); rows.push_back(u); }
            return start;
        };
        // rest: the ear's, the neck's and the unmasked scatter scales, then the material's scale.
        const Skin skins[3]={
            {false, false, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1.f}},
            {false, true, {3, 0.25f, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 0.8f}},
            {true, true, {3, 0.25f, 1, 1}, {1, 1, 2, 1}, {0.5f, 3, 1, 0.4f}}};
        const double map=128.0/255, r=64.0/255, g=128.0/255, lip=64.0/255, neck=32.0/255, ear=64.0/255;
        const double want[3]={
            map,
            std::min(1.0, (3*r+0.25*g+(1-std::min(1.0, r+g)))*0.8*map),
            std::min(1.0, (3*r+0.25*g+2*lip+0.5*ear+3*neck+(1-std::min(1.0, r+g+lip+ear+neck)))*0.4*map)};
        uint32_t starts[3]; for(int k=0;k<3;++k) starts[k]=record(skins[k]);
        expect(SetMaterialPrograms(0, rows.data(), uint32_t(rows.size()/4))==0, "skin records with their subsurface map and scatter scales set");
        const M4 world=mul(translate_scale({0, 1.02f, 2}, {2, 2, 1}), rot_x(-pi/2));
        const Camera camera=make_camera({0, 1.02f, -1}, {0, 1.02f, 2}, 60.f, aspect);
        double got[3]={0, 0, 0}, pixels[3]={0, 0, 0}; bool all=true;
        for(int k=0;k<3;++k) {
            TGPT_Material m=materials[0]; m.program=TGPT_PROGRAM_SKIN; m.programRow=starts[k];
            m.baseColor[0]=m.baseColor[1]=m.baseColor[2]=1; m.metallic=0; m.smoothness=0;
            m.albedoTile=-1; m.colorTexture=m.maskTexture=m.normalTexture=m.emissiveTexture=-1;
            // Its own number says 1: the program's mask at the point is what counts once its textures are open.
            m.flags=TGPT_MATERIAL_SUBSURFACE; m.subsurfaceMask=1.f;
            m.subsurface[0]=3333.3333f; m.subsurface[1]=6250.f; m.subsurface[2]=11111.1111f;
            walkScene(cardHandle, world, m, 4, camera, 24000+uint32_t(k)*400);
            double on=0, walked=0;
            all=all && counted(camera, 24050+uint32_t(k)*400, 6, 0u, on, walked);
            got[k]=walked/std::max(on, 1.0); pixels[k]=on/6;
        }
        std::printf("      skin's mask per point, the share of a panel's pixels that walk in a frame: its map alone %.4f (%.4f), a body's regions and scale %.4f (%.4f), "
                    "a head's %.4f (%.4f); %.0f pixels\n", got[0], want[0], got[1], want[1], got[2], want[2], pixels[0]);
        expect(all && pixels[0]>20000, "a skin panel's pixels are counted");
        expect(std::fabs(got[0]-want[0])<0.01, "skin walks as often as its subsurface map says at the point");
        expect(std::fabs(got[1]-want[1])<0.01 && std::fabs(got[2]-want[2])<0.01, "skin's mask takes its regions' scatter scales and the material's scale, as the game's shader computes it");
        expect(SetMaterialPrograms(0, nullptr, 0)==0, "skin records cleared");
    } else expect(false, "the exports the skin mask check needs");
    objects=objectsBefore; ownRays=ownBefore; sunOn=sunBefore; skyIntensity=skyBefore; bounces=bouncesBefore;
    SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()));
    SetLights(&light, 1);
    for(uint32_t i=0;i<8;++i) run(TGPT_VIEW_NONE, cam, cam, i);
}
if(GetEnvironmentVariableA("TGPT_TEST_WALKS_ONLY", nullptr, 0)>0) {
    std::printf(failures ? "FAILED %d checks\n" : "walk checks passed\n", failures); return failures ? 1 : 0;
}
