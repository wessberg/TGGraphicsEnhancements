// SPDX-License-Identifier: MIT
// PT-87 (docs/agents/memory-4.md): the atlases' second count given back, and the picture does not know.
// Included by pathtracing_memory2_checks.inl (its case 11). Alone: TGPT_TEST_MEMORY2_ONLY=1 with TGPT_TEST_MEMORY4_ONLY=1.
//
// Windows counts a texture once for each device that uses it. The tracer's atlases (colour, alpha, cookies, hair's
// strands) are made on the game's device and read by the tracer's, so each was in the count twice. Now the game's device
// holds its side of an atlas only around a bake: it lets go two seconds after the last one (here after 6 traced frames,
// since a test's frames do not go by the clock: TGPT_TEST_ATLAS_SIDES=<frames>, 0 for "never, as before"), or after a
// second without any traced frame, and the next bake opens it again by a kept handle. Each run below bakes into all four
// atlases AFTER their sides were let go:
// a material's texture, an alpha-tested one, a light's cookie and a hair's strands, all on screen. The same frames with
// the sides let go and with the sides held must give the same picture pixel for pixel, and Windows' count must be lower
// by the atlases' size while no bake is near. Then the openings are made to fail (TGPT_TEST_ATLAS_SIDES_BREAK=<bits>, as
// if the driver had refused them): a side whose way back cannot be shown stays held, and a bake whose side cannot be
// opened waits and is baked when it can.
{
    auto sides=[](const char* frames) { SetEnvironmentVariableA("TGPT_TEST_ATLAS_SIDES", frames); };
    auto sides_break=[](const char* bits) { SetEnvironmentVariableA("TGPT_TEST_ATLAS_SIDES_BREAK", bits); };
    auto pointer=[](const ComPtr<ID3D11Texture2D>& t) { return uint64_t(uintptr_t(t.Get())); };
    // Windows' count once the game's device has flushed and finished, and a moment for the count to follow.
    auto settled_mb=[&]() {
        D3D11_QUERY_DESC qd{D3D11_QUERY_EVENT, 0}; ComPtr<ID3D11Query> q; device->CreateQuery(&qd, &q);
        if(q) { context->End(q.Get()); context->Flush(); BOOL done=FALSE; for(int i=0;i<3000 && (context->GetData(q.Get(), &done, sizeof done, 0)!=S_OK || !done);++i) Sleep(1); }
        Sleep(120);
        return windows_mb();
    };
    // This process's lines of the game's side of the atlases in the native log (the log is added to from run to run).
    auto side_lines=[&]() {
        std::ifstream log(memory2Log); std::string line; std::vector<std::string> found;
        while(std::getline(log, line)) {
            if(line.find("the shared device is here")!=std::string::npos) found.clear();
            size_t at=line.find("PT memory: the game's device"); if(at==std::string::npos) at=line.find("PT memory: no second handle");
            if(at!=std::string::npos) found.push_back(line.substr(at));
        }
        return found;
    };
    auto count_lines=[](const std::vector<std::string>& lines, size_t from, const std::string& what) {
        size_t n=0; for(size_t i=from;i<lines.size();++i) n+=lines[i].find(what)!=std::string::npos; return n;
    };
    auto rgba=[&](int size, const std::function<uint32_t(int, int)>& texel) {
        std::vector<uint32_t> px(size_t(size)*size);
        for(int y=0;y<size;++y) for(int x=0;x<size;++x) px[size_t(y)*size+x]=texel(x, y);
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=UINT(size); d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{px.data(), UINT(size*4), 0};
        ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, &init, &t); return t;
    };
    // What the runs bake: magenta and yellow stripes for the box's sides (no other thing in the room is magenta), a leaf
    // with striped holes for the shadow on the floor, a cookie that darkens half of the lamp's light.
    const ComPtr<ID3D11Texture2D> stripes=rgba(256, [](int x, int) { return (x/32)&1 ? 0xFF00FFFFu : 0xFFFF00FFu; });
    const ComPtr<ID3D11Texture2D> leafStripes=rgba(128, [](int x, int) { return (x/16)&1 ? 0x00307020u : 0xFF307020u; });
    ComPtr<ID3D11Texture2D> cookieTex;
    {
        const int size=16; std::vector<uint8_t> faces[6];
        for(int f=0;f<6;++f) {
            faces[f].resize(size_t(size)*size*4);
            for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
                const float u=(x+0.5f)/size*2-1, v=(y+0.5f)/size*2-1;
                V3 d;
                switch(f) { case 0: d={1, -v, -u}; break; case 1: d={-1, -v, u}; break; case 2: d={u, 1, v}; break; case 3: d={u, -1, -v}; break; case 4: d={u, -v, 1}; break; default: d={-u, -v, -1}; break; }
                const uint8_t c=d.x>0 ? 255 : 8;
                for(int k=0;k<3;++k) faces[f][(size_t(y)*size+x)*4+k]=c;
                faces[f][(size_t(y)*size+x)*4+3]=255;
            }
        }
        D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=size; d.MipLevels=1; d.ArraySize=6; d.Format=DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_SHADER_RESOURCE; d.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        D3D11_SUBRESOURCE_DATA init[6]; for(int f=0;f<6;++f) init[f]={faces[f].data(), UINT(size*4), 0};
        device->CreateTexture2D(&d, init, &cookieTex);
    }
    expect(stripes && leafStripes && cookieTex && RegisterCookie && RegisterHairColor && ReleaseHairColor, "the textures the atlases' check bakes");
    // Hair drawn as a layer (the hair layer's own fixture, further down in the whole test): a red card 2 cm before a grey
    // wall, its alpha kept at the map's own size in the strand atlas. Each hair below has a map of its own, so that each
    // bakes a block of the strand atlas (a block is baked once for a map).
    auto hair_of=[&](const ComPtr<ID3D11Texture2D>& map) {
        TGPT_HairColor hair{}; hair.size=sizeof(TGPT_HairColor); hair.abi=TGPT_ABI;
        hair.diffuseMap=pointer(map);
        hair.diffuseTilingOffset[0]=hair.diffuseTilingOffset[1]=1.f;
        for(int i=0;i<4;++i) hair.diffuseColor[i]=1.f;
        hair.rootColor[0]=hair.endColor[0]=0.8f; hair.rootColor[1]=hair.endColor[1]=0.05f; hair.rootColor[2]=hair.endColor[2]=0.04f;
        hair.rootColor[3]=hair.endColor[3]=1.f;
        hair.baseColorStrength=hair.globalStrength=hair.rootColorStrength=hair.endColorStrength=1.f; hair.diffuseStrength=1.f;
        hair.alphaClip=0.1f; hair.alphaPrepass=0.5f;
        return hair;
    };
    std::vector<ComPtr<ID3D11Texture2D>> hairMaps; // kept to the end: the strand atlas tells its blocks apart by their map
    auto hair_map=[&](int kind) {
        hairMaps.push_back(rgba(64, [kind](int x, int y) { return ((kind==0 ? (x/8+y/8)%2 : kind==1 ? (x/8)%2 : (y/8)%2)==0 ? 77u<<24 : 0u)|0x00FFFFFFu; }));
        return hairMaps.back();
    };
    uint64_t sideKey=0x87400000ull;
    const TGPT_HairColor checkeredHair=hair_of(hair_map(0));
    const uint64_t checkeredKey=++sideKey;
    const int checkeredTile=RegisterHairColor(checkeredKey, &checkeredHair);
    expect(checkeredTile>=4096, "the layer hair's colour in a detail tile, its strands in the strand atlas");
    auto card=materials[0]; card.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_HAIR_LAYER;
    card.albedoTile=checkeredTile; card.alphaTile=-1; card.alphaCutoff=0.f;
    card.baseColor[0]=card.baseColor[1]=card.baseColor[2]=card.baseColor[3]=1.f;
    card.uvScaleOffset[0]=card.uvScaleOffset[1]=1.f; card.uvScaleOffset[2]=card.uvScaleOffset[3]=0.f;
    card.hairAlpha=0x3800u|(0x3C00u<<16); card.smoothness=0.9f;
    const uint32_t cardIndex=uint32_t(materials.size())+8;
    {
        const V3 wallAt{-1.2f, 2.4f, 3.f}; const float half=0.5f;
        const V3 n=normalize(cam.position-wallAt), t=normalize(cross(n, V3{1, 0, 0})), b=cross(t, n);
        M4 turn=identity();
        const V3 axes[3]={t, n, b};
        for(int c=0;c<3;++c) { turn.m[0][c]=axes[c].x; turn.m[1][c]=axes[c].y; turn.m[2][c]=axes[c].z; }
        auto place=[&](const V3& at, float s) { return mul(translate_scale(at, {1, 1, 1}), mul(turn, translate_scale({0, 0, 0}, {2*s, 1, 2*s}))); };
        const M4 cardW=place(wallAt+n*0.02f, half), wallW=place(wallAt, half*1.6f);
        auto list=instances; auto listIndices=indices;
        for(int k=0;k<2;++k) {
            TGPT_Instance q{}; const M4& w=k ? wallW : cardW;
            for(int r=0;r<3;++r) for(int c=0;c<4;++c) q.transform[r*4+c]=w.m[r][c];
            q.mesh=k ? quadHandle : cardHandle; q.materialBase=uint32_t(listIndices.size()); q.mask=0xFF; q.flags=k ? 0u : TGPT_INSTANCE_ALPHA_TEST;
            listIndices.push_back(k ? 0u : cardIndex); list.push_back(q);
        }
        expect(SetMaterials(cardIndex, &card, 1)==0 && SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0,
               "the room with a card of layer hair before a wall");
    }
    // A bake into each of the four atlases that no picture shows: whatever the run before left, every side is held after it.
    auto prime=[&]() {
        const TGPT_HairColor hair=hair_of(hair_map(2));
        const uint64_t hairKey=++sideKey;
        const bool ok=RegisterTexture(++sideKey, pointer(stripes))>=0 && RegisterAlphaTexture(++sideKey, pointer(leafStripes), 0.5f)>=0 &&
            RegisterCookie(++sideKey, pointer(cookieTex))>=0 && RegisterHairColor(hairKey, &hair)>=4096;
        expect(ok, "a bake into each of the four atlases, which takes every side up");
        for(uint32_t i=0;i<2;++i) run(TGPT_VIEW_NONE, cam, cam, 44900+i, still);
        return hairKey;
    };
    // One run: every side held (prime); 20 frames from the same numbers, in which the sides are let go (not when held);
    // then a texture, an alpha-tested one, a cookie and a hair's strands registered and put on screen, baked by the next
    // frame; 27 more frames. Windows' count before the bakes, right after them, and at the end.
    struct SidesRun { std::vector<float> before, picture; double quiet=0, baked=0, after=0; };
    auto sides_run=[&](const char* frames) {
        sides(frames);
        SidesRun r;
        const uint64_t primeKey=prime();
        ownRays=true;
        for(uint32_t i=0;i<20;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 45000+i : 0, still);
        r.before=readback(); r.quiet=settled_mb();
        const TGPT_HairColor striped=hair_of(hair_map(1));
        const uint64_t hairKey=++sideKey;
        const int colourTile=RegisterTexture(++sideKey, pointer(stripes)), alphaTile=RegisterAlphaTexture(++sideKey, pointer(leafStripes), 0.5f);
        const int cookie=RegisterCookie(++sideKey, pointer(cookieTex)), hairTile=RegisterHairColor(hairKey, &striped);
        expect(colourTile>=0 && alphaTile>=0 && cookie>=0 && hairTile>=4096, "a texture, an alpha-tested one, a cookie and a hair registered while the sides are let go");
        auto box=materials[3]; box.albedoTile=colourTile;
        auto leafNow=materials[6]; leafNow.alphaTile=alphaTile;
        auto cardNow=card; cardNow.albedoTile=hairTile;
        TGPT_Light masked=light; masked.direction[0]=0; masked.direction[1]=0; masked.direction[2]=1; masked.up[0]=0; masked.up[1]=1; masked.up[2]=0;
        masked.flags=uint32_t(cookie)+1;
        expect(SetMaterials(3, &box, 1)==0 && SetMaterials(6, &leafNow, 1)==0 && SetMaterials(cardIndex, &cardNow, 1)==0 && SetLights(&masked, 1)==0,
               "the box, the leaf, the hair and the lamp read what was just registered");
        for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_NONE, cam, cam, 45020+i, still);
        r.baked=settled_mb();
        for(uint32_t i=0;i<24;++i) run(TGPT_VIEW_NONE, cam, cam, 45023+i, still);
        r.picture=readback(); r.after=settled_mb();
        ownRays=false;
        expect(SetMaterials(3, &materials[3], 1)==0 && SetMaterials(6, &materials[6], 1)==0 && SetMaterials(cardIndex, &card, 1)==0 && SetLights(&light, 1)==0,
               "the room as it was");
        ReleaseHairColor(hairKey); ReleaseHairColor(primeKey);
        for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, cam, cam, 45100+i, still);
        return r;
    };
    sides_break(nullptr);
    // What PT-87's other cuts let go of when frames no longer use it goes before the runs that are compared, not in the
    // middle of them: the built-in filters' textures after 120 frames without the built-in denoiser (196 MB here), the
    // structures' scratch room of the cases before (24 MB, some 230 frames after this scene was set). Windows' count is
    // then the atlases' alone to move.
    sides("6");
    for(uint32_t i=0;i<600;++i) run(TGPT_VIEW_NONE, cam, cam, 44000+i, still);
    sides_run("6"); // not judged: it makes the strand atlas, and the first run after another case starts from that case's state
    const size_t linesBefore=side_lines().size();
    const SidesRun gone=sides_run("6");
    const std::vector<std::string> goneLines=side_lines();
    const SidesRun held=sides_run("0"), goneAgain=sides_run("6");
    // The atlases' sizes as the native log's lines have them, and what they say is counted once with all four let go.
    const char* const names[4]={"the colour atlas", "the alpha atlas", "the cookie atlas", "hair's strand atlas"};
    double sizes[4]={0, 0, 0, 0}, once=0; size_t letGo[4]={0, 0, 0, 0}, takenUp[4]={0, 0, 0, 0};
    for(size_t i=linesBefore;i<goneLines.size();++i) {
        std::printf("      native log: %s\n", goneLines[i].c_str());
        once=std::max(once, number_after(goneLines[i], "of the tracer's own shared textures "));
        // (A line names the atlases that moved together, each with its size: "let go of the colour atlas (192 MB, ...), ...".)
        const bool go=goneLines[i].find("the game's device let go of")!=std::string::npos, up=goneLines[i].find("took up again for a bake")!=std::string::npos;
        for(int a=0;a<4;++a) {
            const std::string name=std::string(names[a])+" (";
            if(goneLines[i].find(name)==std::string::npos) continue;
            sizes[a]=number_after(goneLines[i], name.c_str());
            letGo[a]+=go; takenUp[a]+=up;
        }
    }
    const double all=sizes[0]+sizes[1]+sizes[2]+sizes[3];
    std::printf("      the atlases: colour %.0f MB, alpha %.0f, cookies %.0f, hair's strands %.0f: %.0f MB; with all four let go the native log says %.0f MB of the tracer's "
                "own shared textures are counted once\n", sizes[0], sizes[1], sizes[2], sizes[3], all, once);
    std::printf("      Windows' count, MB: before the bakes %.0f with the sides let go, %.0f with them held (%.0f less); right after the bakes %.0f and %.0f (%.0f less); "
                "27 frames later %.0f and %.0f (%.0f less); the run with the sides let go again: %.0f, %.0f, %.0f\n",
                gone.quiet, held.quiet, held.quiet-gone.quiet, gone.baked, held.baked, held.baked-gone.baked, gone.after, held.after, held.after-gone.after,
                goneAgain.quiet, goneAgain.baked, goneAgain.after);
    for(int a=0;a<4;++a) {
        char text[160];
        std::snprintf(text, sizeof text, "the game's device let go of %s twice in the run and took it up again for the run's bake (the native log's lines, each with Windows' count)", names[a]);
        expect(letGo[a]>=2 && takenUp[a]>=1 && sizes[a]>0, text);
    }
    expect(all>=330.0 && std::fabs(once-all)<=2.0, "all four atlases are counted once while no bake is near (over 330 MB of them)");
    expect(countKnown && std::fabs((held.quiet-gone.quiet)-all)<=24.0, "before the bakes Windows' count is lower by the atlases' size than with the sides held");
    expect(std::fabs(held.baked-gone.baked)<=24.0, "right after a bake into each, the count is what it is with the sides held (the game's device holds them while it bakes)");
    expect(std::fabs((held.after-gone.after)-all)<=24.0, "and lower by their size again once the bakes are six frames past");
    const Differ sidesCut=differ(gone.picture, held.picture), sidesSame=differ(gone.picture, goneAgain.picture), sidesBaked=differ(gone.before, gone.picture);
    size_t changed=0, lit=0;
    for(size_t i=0;i+2<gone.picture.size();i+=3) {
        lit+=gone.picture[i+1]>0.02f;
        changed+=std::fabs(gone.picture[i]-gone.before[i])+std::fabs(gone.picture[i+1]-gone.before[i+1])+std::fabs(gone.picture[i+2]-gone.before[i+2])>0.1f;
    }
    std::printf("      the picture after the bakes, the sides let go against held: the largest difference of any pixel's channel %.3g (mean %.3g); the same run twice: %.3g "
                "(mean %.3g); against the picture before the bakes %zu pixels changed (mean %.3g); lit pixels %zu\n",
                sidesCut.most, sidesCut.mean, sidesSame.most, sidesSame.mean, changed, sidesBaked.mean, lit);
    expect(lit>size_t(W)*H/20, "the picture shows the room");
    expect(changed>size_t(W)*H/100, "the bakes are in the picture: the box's stripes, the leaf's shadow, the lamp's cookie, the hair's strands");
    expect(sidesCut.most<=sidesSame.most*2.0+1e-6, "the picture is the same, pixel for pixel, whether the game's device held the atlases or opened them again for the bakes");
    save(gone.picture, "87-atlas-sides-let-go.png", true); save(held.picture, "87-atlas-sides-held.png", true); save(gone.before, "87-atlas-sides-before-the-bakes.png", true);

    // The rule as the game runs it, by the clock (no switch set): two seconds after its last bake a side goes.
    {
        sides(nullptr);
        const uint64_t primeKey=prime();
        const double up=settled_mb();
        const uint64_t began=GetTickCount64(); uint32_t frames=0;
        while(GetTickCount64()-began<2700) { run(TGPT_VIEW_NONE, cam, cam, 45200+frames, still); ++frames; }
        const double down=settled_mb();
        std::printf("      by the clock, as the game runs it: Windows' count %.0f MB with every side held, %.0f MB after 2.7 s (%u frames) without a bake (%.0f less)\n",
                    up, down, frames, up-down);
        expect(std::fabs((up-down)-all)<=24.0, "two seconds after its last bake the game's device lets go of an atlas (the game's own rule, no switch set)");
        ReleaseHairColor(primeKey);
    }

    // No frame for a second (path tracing off, or held back for textures): the maintenance event lets every side go. And
    // the memory line's own figures (written every ten seconds): ownSharedOnce is what is let go.
    {
        sides("6");
        const uint64_t primeKey=prime();
        const double up=settled_mb();
        Sleep(9300);
        memory(); // (a second without a frame, then the plugin's maintenance event)
        const double down=settled_mb();
        const std::string line=last_line("PT memory bytes:");
        const double ownOnce=number_after(line, " ownSharedOnce=")/1048576.0, own=number_after(line, " ownShared=")/1048576.0;
        const double letGoCount=number_after(line, " atlasSidesLetGo="), takenUpCount=number_after(line, " atlasSidesTakenUp=");
        std::printf("      a second without a traced frame: Windows' count %.0f MB with every side held, %.0f MB after the maintenance event (%.0f less); the memory line: "
                    "ownShared %.0f MB, ownSharedOnce %.0f MB, sides let go %.0f times and taken up again %.0f times\n", up, down, up-down, own, ownOnce, letGoCount, takenUpCount);
        expect(std::fabs((up-down)-all)<=24.0, "with no frame traced for a second the game's device lets go of every atlas");
        expect(std::fabs(ownOnce-all)<=2.0 && own>=ownOnce && letGoCount>=8 && takenUpCount>=8, "the memory line says what is counted once, and how often a side went and came");
        ReleaseHairColor(primeKey);
    }

    // The openings fail, as if the driver had refused them. The alpha atlas's SECOND opening (the proof of the way back,
    // before the first is let go): it stays held, and so does its second count. The colour atlas's opening for a bake:
    // the bake waits (the box keeps the white of a tile not baked yet), nothing is dropped, and the first frame that can
    // open it bakes it; from then on that side stays held too.
    {
        auto magenta=[&](const std::vector<float>& px) { size_t n=0; for(size_t i=0;i+2<px.size();i+=3) n+=px[i]>0.5f && px[i+2]>0.5f && px[i+1]<0.3f; return n; };
        sides("6");
        const uint64_t primeKey=prime();
        sides_break("18"); // 2: the alpha atlas's second opening; 16: the colour atlas's opening for a bake
        ownRays=true;
        for(uint32_t i=0;i<14;++i) run(TGPT_VIEW_ALBEDO, cam, cam, i ? 46000+i : 0, still);
        const double quiet=settled_mb();
        const auto first=readback();
        const int tile=RegisterTexture(++sideKey, pointer(stripes));
        auto box=materials[3]; box.albedoTile=tile;
        expect(tile>=0 && SetMaterials(3, &box, 1)==0, "the box reads a texture registered while the colour atlas cannot be opened");
        for(uint32_t i=0;i<6;++i) run(TGPT_VIEW_ALBEDO, cam, cam, 46020+i, still);
        const auto waiting=readback();
        sides_break("2");
        for(uint32_t i=0;i<3;++i) run(TGPT_VIEW_ALBEDO, cam, cam, 46030+i, still);
        const auto baked=readback();
        for(uint32_t i=0;i<14;++i) run(TGPT_VIEW_ALBEDO, cam, cam, 46040+i, still);
        const double stayed=settled_mb();
        ownRays=false;
        size_t moved=0;
        for(size_t i=0;i+2<first.size();i+=3) moved+=std::fabs(waiting[i]-first[i])+std::fabs(waiting[i+1]-first[i+1])+std::fabs(waiting[i+2]-first[i+2])>0.2f;
        const std::vector<std::string> lines=side_lines();
        const size_t proofLines=count_lines(lines, 0, "could not open the alpha atlas a second time"), waitLines=count_lines(lines, 0, "could not open the colour atlas again for a bake");
        for(const auto& l:lines) if(l.find("could not")!=std::string::npos) std::printf("      native log: %s\n", l.c_str());
        std::printf("      the openings made to fail: Windows' count %.0f MB with the alpha atlas kept (%.0f more than with all four let go); the box's magenta pixels: %zu before, "
                    "%zu while its bake waits (%zu pixels turned to the tile's white), %zu once the atlas could be opened; 14 frames later the count is %.0f MB (%.0f more)\n",
                    quiet, quiet-gone.quiet, magenta(first), magenta(waiting), moved, magenta(baked), stayed, stayed-gone.quiet);
        expect(proofLines>=1 && std::fabs((quiet-gone.quiet)-sizes[1])<=24.0, "an atlas whose way back cannot be shown stays held, as before, and the log says so");
        expect(waitLines>=1 && magenta(first)==0 && magenta(waiting)==0 && moved>1000, "a bake whose atlas cannot be opened waits: its tile keeps the white of a tile not baked yet, and the log says so");
        expect(magenta(baked)>1000, "and it is baked by the first frame that can open the atlas: nothing was dropped");
        expect(std::fabs((stayed-gone.quiet)-(sizes[0]+sizes[1]))<=24.0, "after a failed opening that atlas stays held too (the colour and the alpha atlas are counted twice again, as before)");
        expect(SetMaterials(3, &materials[3], 1)==0, "the box as it was");
        ReleaseHairColor(primeKey);
        save(waiting, "87-atlas-sides-bake-waits.png", false); save(baked, "87-atlas-sides-bake-landed.png", false);
    }
    sides(nullptr); sides_break(nullptr);
    ReleaseHairColor(checkeredKey);
    expect(SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room alone again");
    for(uint32_t i=0;i<4;++i) run(TGPT_VIEW_NONE, cam, cam, 46100+i, still);
    counted("after the atlases' sides");
}
