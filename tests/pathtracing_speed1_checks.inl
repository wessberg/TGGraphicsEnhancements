// SPDX-License-Identifier: MIT
// NOT RUN YET (2026-10-09): this mode compiles, and its one start was stopped before its first frame (the tracer's passes
// were still being made, at 33 GB of memory: docs/agents/speed-1.md). Its numbers and its checks' verdicts are still to
// come; the run ends itself over 8 GB of its own memory (TGPT_TEST_MEMORY_CEILING_MB, pathtracing_gpu_test.cpp).
// TGPT_TEST_SPEED1_ONLY=1 (docs/agents/speed-1.md, PT-99; included in tgpt_gpu_test's speed study, after its materials
// 100 to 109 are set; the run is at 1920 x 1080, near the game's 1924 x 1084 rays): the speed switches of
// TGPT_Frame.prevCameraPosition's w (pt_common speed_trial), on a scene that is heavy the way a town of the game is by day.
//   The scene: a ground of the game's terrain shader, some 80 buildings of its architecture, triplanar and HDRP Lit
//   shaders, crates, standing figures (half of them a subsurface material: the walk), glass, 48 glowing cubes (sampled as
//   lights in clusters), 8,000 tufts of grass and 250 trees whose leaves and blades are cut-outs read from a shared texture
//   (as the game's are) and stand in the wind's pose (groups, which have no opacity micromaps), 510 lamps, 41 decals, and
//   a sea beside it with its bed 2.4 m under it. The sun and the sky are on. The tracer's own camera rays, one ray a pixel,
//   one bounce, no denoiser, the Emissive Lights row on: the plugin's defaults.
//   1. The same picture (judged): 16 frames from a reset, three views (the town; the shore, with the sea's bed beside the
//      water; from under the water), each with the switches off, off again (what two runs of the same frames differ by,
//      if anything) and on. With the switches on no pixel may differ by more than the two runs with them off do.
//      A fourth run with Indirect Color at 0.9 must differ: the check sees a picture that changed.
//   2. What it saves (printed, never judged: other programs may share the GPU): the stages' GPU times with the switches
//      off and on, each the least of three windows of about a second (TGPT_GetStageTimes' means).
//   3. With TGPT_TEST_SPEED1_TABLE=1 as well: the table of docs/agents/speed-1.md. The stages' times with one part of
//      the work left out at a time (the measuring build's switches, speed_measure: in a build without them those rows
//      equal the first), with options of the tracer's, and with parts of the scene left out.
//   TGPT_TEST_SPEED1_SCENE=<bits>: the parts of the scene (1 the 510 lamps, else 8; 2 the plants; 4 the sea; 8 the glowing
//   cubes; 16 the decals; 32 the subsurface figures and the glass; default all those. 64: every solid instance flagged as
//   holding a cut-out, so that the shader tests each of its triangles, as it does for a building with one cut-out submesh.
//   128: the lamps fall off with distance alone (TGPT_LIGHT_NO_RANGE), as the plugin's lamps do: the far lists then hold
//   every one of them, as in the game, and a reflection ray finds those it runs into by the lamps' search tree
//   (pt_common reflected_lamps: until 2026-10-10 a loop that asked all 510).
//   TGPT_TEST_SPEED1_SWITCHES=<number> (docs/agents/perf-1.md "perf2"): the switches the test turns on, in place of 1, on
//   the same four views and with their row in the times: a trial's numbers in a build made to try it (2 and 4 with
//   CMake's TGPT_REGROUP_WATER). The fourth view, the town over the water, shows the town's lamps mirrored in the sea.
{
    const int failuresBefore=failures;
    auto envNumber=[](const char* name, uint32_t fallback) { char t[32]{}; return GetEnvironmentVariableA(name, t, sizeof t)>0 ? uint32_t(std::strtoul(t, nullptr, 0)) : fallback; };
    const bool table=GetEnvironmentVariableA("TGPT_TEST_SPEED1_TABLE", nullptr, 0)>0;
    const uint32_t allParts=envNumber("TGPT_TEST_SPEED1_SCENE", 63u);
    if(!SetPlantWind || !SetWindNoise || !SetWaterCaustics || !SetDecals) { std::printf("FAILED: the speed-1 scene needs the plant wind, water and decal exports\n"); return 1; }
    uint32_t seed=0x12345u;
    auto rnd=[&]() { seed=seed*1664525u+1013904223u; return float(seed>>8)/16777216.f; };
    auto rot_y=[&](float a) { M4 r=identity(); r.m[0][0]=std::cos(a); r.m[0][2]=std::sin(a); r.m[2][0]=-std::sin(a); r.m[2][2]=std::cos(a); return r; };
    // A cut-out's own texture, 512 x 512 with all its mips, as the game's leaves and grass have one: kind 0 leaves (discs,
    // 44 % covered), 1 blades of grass (stripes that narrow towards the top, v = 1).
    auto cutout=[&](int kind) {
        const UINT size=512; std::vector<std::vector<uint32_t>> levels; std::vector<D3D11_SUBRESOURCE_DATA> init;
        levels.emplace_back(size_t(size)*size);
        for(UINT y=0;y<size;++y) for(UINT x=0;x<size;++x) {
            bool covered;
            if(kind==0) { const int cx=int(x%64u)-32, cy=int(y%64u)-32; covered=cx*cx+cy*cy<24*24; }
            else covered=float(x%32u)<14.f*(1.f-0.8f*float(y)/float(size));
            const float shade=0.75f+0.25f*rnd();
            const uint32_t r=uint32_t(std::lround(255.f*(kind==0 ? 0.42f : 0.50f)*shade)), g=uint32_t(std::lround(255.f*(kind==0 ? 0.66f : 0.72f)*shade)), b=uint32_t(std::lround(255.f*0.30f*shade));
            levels[0][size_t(y)*size+x]=r|(g<<8)|(b<<16)|(covered ? 0xFF000000u : 0u);
        }
        for(UINT n=size/2;n>=1;n/=2) {
            std::vector<uint32_t> down(size_t(n)*n);
            const std::vector<uint32_t>& up=levels.back();
            for(UINT y=0;y<n;++y) for(UINT x=0;x<n;++x) {
                uint32_t p=0;
                for(int c=0;c<4;++c) { uint32_t sum=0; for(UINT k=0;k<4;++k) sum+=(up[size_t(y*2+(k>>1))*(n*2)+x*2+(k&1u)]>>(8*c))&0xFFu; p|=((sum+2)/4)<<(8*c); }
                down[size_t(y)*n+x]=p;
            }
            levels.push_back(std::move(down));
        }
        UINT n=size; for(const auto& l:levels) { init.push_back({l.data(), n*4, 0}); n/=2; }
        D3D11_TEXTURE2D_DESC d{size, size, UINT(levels.size()), 1, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, {1, 0}, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
        ComPtr<ID3D11Texture2D> t; device->CreateTexture2D(&d, init.data(), &t); keep.push_back(t);
        TGPT_SharedTexture i{}; i.size=sizeof(i); i.abi=TGPT_ABI;
        return t && RegisterSharedTexture(uint64_t(uintptr_t(t.Get())), &i)>=0 ? int32_t(i.handle) : -1;
    };
    const int32_t leafMap=cutout(0), grassMap=cutout(1);
    // 110 leaves, 111 grass (cut-outs lit through from behind), 112 a trunk, 113 a glowing thing, 114 armour (a smooth metal).
    TGPT_Material more[5];
    more[0]=six[1]; more[0].flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_TRANSLUCENT|TGPT_MATERIAL_DOUBLE_SIDED; more[0].alphaCutoff=0.5f; more[0].colorTexture=leafMap;
    more[0].uvScaleOffset[0]=more[0].uvScaleOffset[1]=1.f;
    more[1]=more[0]; more[1].colorTexture=grassMap;
    more[2]=six[1];
    more[3]=six[0]; more[3].baseColor[0]=more[3].baseColor[1]=more[3].baseColor[2]=0.2f; more[3].emissive[0]=5.f; more[3].emissive[1]=3.f; more[3].emissive[2]=1.2f;
    more[4]=six[1]; more[4].maskTexture=-1; more[4].metallic=1.f; more[4].smoothness=0.85f;
    if(leafMap<0 || grassMap<0 || SetMaterials(110, more, 5)!=0) { std::printf("FAILED: the speed-1 scene's materials\n"); return 1; }
    // A tuft of grass (three crossed cards) and a tree (a trunk of two cards, a crown of 28 leaf cards).
    std::vector<Vertex> tuft, tree; std::vector<uint16_t> tuftIndices, trunkIndices, crownIndices;
    auto standing=[&](std::vector<Vertex>& verts, std::vector<uint16_t>& list, V3 middle, float turn, float width, float y0, float y1, int rows) {
        const float dx=std::cos(turn), dz=std::sin(turn);
        const uint16_t base=uint16_t(verts.size());
        for(int r=0;r<=rows;++r) {
            const float v=float(r)/float(rows), y=middle.y+y0+(y1-y0)*v;
            verts.push_back({middle.x-0.5f*width*dx, y, middle.z-0.5f*width*dz, -dz, 0, dx, 0, v});
            verts.push_back({middle.x+0.5f*width*dx, y, middle.z+0.5f*width*dz, -dz, 0, dx, 1, v});
        }
        for(int r=0;r<rows;++r) { const uint16_t q=uint16_t(base+r*2); list.insert(list.end(), {q, uint16_t(q+2), uint16_t(q+1), uint16_t(q+1), uint16_t(q+2), uint16_t(q+3)}); }
    };
    for(int c=0;c<3;++c) standing(tuft, tuftIndices, V3{0, 0, 0}, 0.2f+float(c)*pi/3.f, 0.6f, 0.f, 0.7f, 2);
    for(int c=0;c<2;++c) standing(tree, trunkIndices, V3{0, 0, 0}, float(c)*pi/2.f, 0.35f, 0.f, 3.2f, 4);
    for(int c=0;c<28;++c) { const V3 at{(rnd()-0.5f)*3.2f, 3.6f+(rnd()-0.5f)*2.4f, (rnd()-0.5f)*3.2f}; standing(tree, crownIndices, at, rnd()*pi, 1.6f, -0.7f, 0.7f, 1); }
    MeshBuffers tuftMesh=make_mesh(tuft, {tuftIndices}), treeMesh=make_mesh(tree, {trunkIndices, crownIndices});
    const int tuftHandle=register_mesh(tuftMesh, 0x5eed01), treeHandle=register_mesh(treeMesh, 0x5eed02);
    if(tuftHandle<=0 || treeHandle<=0) { std::printf("FAILED: the speed-1 scene's plants\n"); return 1; }
    // The winds' noise maps (grass reads key 7, leaves and trunks key 9), smooth, between 0.2 and 1.
    {
        std::vector<uint8_t> gust(64*64), mask(64*64);
        for(int y=0;y<64;++y) for(int x=0;x<64;++x) {
            gust[size_t(y)*64+size_t(x)]=uint8_t(std::lround(255.f*(0.6f+0.4f*std::sin(float(x)*0.31f+1.3f)*std::cos(float(y)*0.23f+0.7f))));
            mask[size_t(y)*64+size_t(x)]=uint8_t(std::lround(255.f*(0.6f+0.4f*std::sin(float(x)*0.19f+1.3f)*std::cos(float(y)*0.41f+0.7f))));
        }
        if(SetWindNoise(7, 64, 64, 0, gust.data())!=0 || SetWindNoise(9, 64, 64, 0, mask.data())!=0) { std::printf("FAILED: the speed-1 scene's wind noise\n"); return 1; }
    }
    const Camera townCam=make_camera({0, 1.7f, -17}, {2, 2.6f, 6}, 60.f, float(W)/float(H));
    const Camera shoreCam=make_camera({-33, 2.2f, -6}, {-60, -2.0f, 4}, 60.f, float(W)/float(H));
    const Camera underCam=make_camera({-70, -1.6f, 0}, {-84, -2.9f, 9}, 60.f, float(W)/float(H));
    const Camera seaCam=make_camera({-78, 1.4f, 2}, {0, 2.2f, 0}, 60.f, float(W)/float(H)); // over the sea, the town's lamps mirrored in it
    // The town, or part of it. wind: the plants in the wind's pose (a group a cell of the world, no micromaps), as the game
    // traces them; without, they stand as plain instances of their meshes, which get micromaps.
    auto town=[&](uint32_t parts, bool wind) {
        seed=0x7001u;
        std::vector<TGPT_Instance> list; std::vector<uint32_t> index;
        auto put=[&](int mesh, const M4& world, std::initializer_list<uint32_t> materialsOf, uint32_t flags) {
            TGPT_Instance in{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=world.m[r][c];
            in.mesh=mesh; in.materialBase=uint32_t(index.size()); in.mask=0xFF; in.flags=flags; in.renderingLayerMask=1;
            for(uint32_t m:materialsOf) index.push_back(m);
            list.push_back(in);
        };
        // parts 64: every solid thing's instance is flagged as one that holds a cut-out, as a building with one cut-out or glass
        // submesh is in the game: all its triangles are then tested by the shader (scene.cpp: FORCE_NON_OPAQUE), none is a cut-out.
        const uint32_t solid=(parts&64u) ? uint32_t(TGPT_INSTANCE_ALPHA_TEST) : 0u;
        put(quadHandle, translate_scale({20, 0, 0}, {120, 1, 160}), {106u}, solid); // the ground: x -40 to 80
        const uint32_t walls[4]={103u, 107u, 101u, 108u};
        int built=0;
        for(int gz=-5;gz<=5;++gz) for(int gx=-2;gx<=5;++gx) {
            const float x=float(gx)*13.f, z=float(gz)*13.f;
            if(std::fabs(x)<20.f && std::fabs(z)<20.f) continue; // the square
            const float h=6.f+8.f*rnd(), w=8.f+2.f*rnd();
            put(cubeHandle, translate_scale({x, h*0.5f, z}, {w, h, w}), {walls[built%4], walls[(built+1)%4]}, solid); ++built;
        }
        const uint32_t crates[3]={101u, 102u, 114u};
        for(int i=0;i<80;++i) {
            const float s=0.6f+0.6f*rnd();
            put(cubeHandle, mul(translate_scale({-18.f+36.f*rnd(), s*0.5f, -18.f+36.f*rnd()}, {s, s, s}), rot_y(rnd()*pi)), {crates[i%3], crates[i%3]}, solid);
        }
        for(int i=0;i<40;++i) {
            const uint32_t m=(parts&32u) && i%2==0 ? 105u : (i%4==1 ? 114u : 101u);
            put(cubeHandle, mul(translate_scale({-16.f+32.f*rnd(), 0.9f, -12.f+28.f*rnd()}, {0.5f, 1.8f, 0.3f}), rot_y(rnd()*pi)), {m, m}, solid);
        }
        if(parts&32u) for(int i=0;i<6;++i) put(cubeHandle, translate_scale({-10.f+4.f*float(i), 0.5f, -4.f+2.f*float(i%3)}, {0.8f, 1.f, 0.8f}), {104u, 104u}, TGPT_INSTANCE_ALPHA_TEST);
        if(parts&8u) for(int i=0;i<48;++i) put(cubeHandle, translate_scale({-17.f+34.f*rnd(), 0.2f+2.5f*rnd(), -17.f+34.f*rnd()}, {0.4f, 0.4f, 0.4f}), {113u, 113u}, 0);
        if(parts&2u) {
            for(int i=0;i<8000;++i) {
                const float s=0.8f+0.6f*rnd();
                put(tuftHandle, mul(translate_scale({-38.f+116.f*rnd(), 0, -78.f+156.f*rnd()}, {s, s, s}), rot_y(rnd()*2.f*pi)), {111u}, TGPT_INSTANCE_ALPHA_TEST);
            }
            for(int i=0;i<250;++i) {
                const float s=0.8f+0.5f*rnd();
                put(treeHandle, mul(translate_scale({-38.f+116.f*rnd(), 0, -78.f+156.f*rnd()}, {s, s, s}), rot_y(rnd()*2.f*pi)), {112u, 110u}, TGPT_INSTANCE_ALPHA_TEST);
            }
        }
        if(parts&4u) {
            put(quadHandle, translate_scale({-120, -3, 0}, {160, 1, 200}), {101u}, solid); // the sea's bed
            put(quadHandle, translate_scale({-120, -0.6f, 0}, {160, 1, 200}), {0u}, TGPT_INSTANCE_WATER); // slot 0
        }
        if(SetScene(list.data(), uint32_t(list.size()), index.data(), uint32_t(index.size()))!=0) return false;
        std::vector<TGPT_Light> lamps;
        auto lamp=[&](V3 p, float range, float r, float g, float b) {
            TGPT_Light l{}; l.position[0]=p.x; l.position[1]=p.y; l.position[2]=p.z; l.type=0; l.color[0]=r; l.color[1]=g; l.color[2]=b; l.range=range; l.radius=0.05f;
            if(parts&128u) l.flags=TGPT_LIGHT_NO_RANGE; // as the plugin's lamps: no window at the range, on the far lists
            l.id=uint32_t(1000+lamps.size()); lamps.push_back(l);
        };
        if(parts&1u) {
            for(int i=0;i<48;++i) lamp({-18.f+6.f*float(i%7), 3.f, -18.f+6.f*float(i/7)}, 12.f, 30, 20, 12);     // the square's lanterns
            for(int i=0;i<60;++i) lamp({10.f+2.f*rnd(), 3.5f+rnd(), 10.f+2.f*rnd()}, 6.f, 4, 3, 2);               // one chandelier's candles
            for(int i=0;i<402;++i) lamp({-38.f+116.f*rnd(), 1.f+2.f*rnd(), -78.f+156.f*rnd()}, 6.f+4.f*rnd(), 14, 9, 5); // the town's
        } else for(int i=0;i<8;++i) lamp({-13.5f+9.f*float(i%4), 3.f, -6.f+12.f*float(i/4)}, 14.f, 30, 20, 12);
        if(SetLights(lamps.data(), uint32_t(lamps.size()))!=0) return false;
        TGPT_WaterCaustics sea{}; sea.size=sizeof(sea); sea.abi=TGPT_ABI;
        sea.worldToWater[0]=sea.worldToWater[5]=sea.worldToWater[10]=1.f;
        sea.top=sea.bottom=-0.6f; sea.bounds[0]=-200.f; sea.bounds[1]=-100.f; sea.bounds[2]=-40.f; sea.bounds[3]=100.f;
        sea.extinction[0]=0.35f; sea.extinction[1]=0.12f; sea.extinction[2]=0.08f;
        if(SetWaterCaustics((parts&4u) ? &sea : nullptr, (parts&4u) ? 1u : 0u)!=0) return false;
        std::vector<TGPT_Decal> decals;
        if(parts&16u) for(int i=0;i<41;++i) {
            const float x=-16.f+32.f*rnd(), z=-16.f+32.f*rnd(), s=3.f;
            TGPT_Decal d{};
            d.worldToDecal[0]=1.f/s; d.worldToDecal[3]=-x/s; d.worldToDecal[5]=1.f; d.worldToDecal[10]=1.f/s; d.worldToDecal[11]=-z/s;
            d.normalToWorld[0]=1; d.normalToWorld[6]=1; d.normalToWorld[9]=1; // tangent x, bitangent z, normal +y
            d.boundsMin[0]=x-0.5f*s; d.boundsMin[1]=-0.5f; d.boundsMin[2]=z-0.5f*s; d.boundsMax[0]=x+0.5f*s; d.boundsMax[1]=0.5f; d.boundsMax[2]=z+0.5f*s;
            d.layerMask=1; d.flags=1;
            d.baseColor[0]=0.35f; d.baseColor[1]=0.25f; d.baseColor[2]=0.15f; d.baseColor[3]=0.7f;
            d.emissiveBlend[3]=1; d.uvScaleOffset[0]=d.uvScaleOffset[1]=1; d.fade[0]=d.fade[3]=1;
            d.remapMin[1]=d.remapMax[1]=1; d.remapMin[3]=1;
            for(auto& t:d.textures) t=-1;
            decals.push_back(d);
        }
        if(SetDecals(decals.empty() ? nullptr : decals.data(), uint32_t(decals.size()))!=0) return false;
        TGPT_PlantWind grassWind{}; grassWind.material=111; grassWind.kind=TGPT_PLANT_GRASS; grassWind.noise=7; grassWind.strength=80.f;
        TGPT_PlantWind leafWind{}; leafWind.material=110; leafWind.kind=TGPT_PLANT_LEAVES; leafWind.noise=9; leafWind.flags=TGPT_PLANT_BEND;
        leafWind.strength=0.9f; leafWind.bendHeight=1.f; leafWind.pivotOffsetY=-1.f; leafWind.fade[0]=0.f; leafWind.fade[1]=150.f;
        TGPT_PlantWind trunkWind=leafWind; trunkWind.material=112; trunkWind.bendHeight=2.f; trunkWind.pivotOffsetY=0.f;
        const TGPT_PlantWind winds[3]={grassWind, leafWind, trunkWind};
        TGPT_PlantWindFrame frame{}; frame.time=3.f; frame.speed=30.f; frame.direction=40.f; frame.forward[2]=1.f; // the world stands still, as in the harness
        return SetPlantWind(&frame, winds, wind && (parts&2u) ? 3u : 0u)==0;
    };
    sunOn=true; skyIntensity=1.f; ownRays=true; bounces=1; rays=1;
    const uint32_t options=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_EMISSIVE_LIGHTS;
    uint32_t frame=1;
    auto settle=[&](const char* what, uint32_t frames) {
        for(uint32_t i=0;i<frames;++i) { run(TGPT_VIEW_NONE, townCam, townCam, i ? frame++ : 0, options); context->Flush(); }
        ReadStatus(&status);
        std::printf("      speed-1 scene (%s): %u instances traced of %u, %llu triangles, %u lights, micromaps on %u meshes (%u instances trace with them), "
                    "%s, frames traced %llu failed %llu, GPU %.2f ms a frame\n", what, status.instancesTraced, status.instances, (unsigned long long)status.triangles, status.lights,
                    status.micromapMeshes, status.micromapInstances,
                    (status.features&TGPT_FEATURE_SER) ? "Shader Execution Reordering on" : ((status.features&TGPT_FEATURE_DXR12) ? "DirectX Raytracing 1.2 without reordering" : "DirectX Raytracing 1.1"),
                    (unsigned long long)status.framesTraced, (unsigned long long)status.framesFailed, double(status.gpuMs));
        std::fflush(stdout);
    };
    if(!town(allParts, true)) { std::printf("FAILED: the speed-1 scene\n"); return 1; }
    settle("the whole town, its plants in the wind's pose", 120);
    expect(status.framesFailed==0, "the speed-1 scene traces without a failed frame");

    // 1. The same picture.
    auto picture=[&](const Camera& c, float trial, uint32_t with) {
        speedTrial=trial;
        for(uint32_t i=0;i<16;++i) { run(TGPT_VIEW_NONE, c, c, i, with); context->Flush(); }
        speedTrial=0.f;
        return readback();
    };
    struct Differs { double most=0, mean=0, meanA=0, meanB=0; size_t pixels=0; };
    auto differ=[&](const std::vector<float>& a, const std::vector<float>& b) {
        Differs d; double sum=0, sa=0, sb=0;
        for(size_t i=0;i+2<a.size() && i+2<b.size();i+=3) {
            double most=0;
            for(int k=0;k<3;++k) { const double fa=std::isfinite(a[i+k]) ? a[i+k] : 0.0, fb=std::isfinite(b[i+k]) ? b[i+k] : 0.0; most=std::max(most, std::fabs(fa-fb)); sum+=std::fabs(fa-fb); sa+=fa; sb+=fb; }
            d.most=std::max(d.most, most); if(most>0) ++d.pixels;
        }
        const double n=double(std::max<size_t>(a.size(), 1));
        d.mean=sum/n; d.meanA=sa/n; d.meanB=sb/n;
        return d;
    };
    // Every switch of speed_trial that is for players: bit 1; or the number TGPT_TEST_SPEED1_SWITCHES gives (above).
    const float allOn=float(envNumber("TGPT_TEST_SPEED1_SWITCHES", 1u));
    if(allOn!=1.f) std::printf("      speed-1: the switches on trial are %u (TGPT_TEST_SPEED1_SWITCHES)\n", unsigned(allOn));
    struct View { const char* name; const Camera* camera; const char* file; };
    const View views[4]={{"the town", &townCam, "speed1-town"}, {"the shore", &shoreCam, "speed1-shore"}, {"under the water", &underCam, "speed1-under"},
                         {"the town over the water", &seaCam, "speed1-sea"}};
    for(const View& v:views) {
        if(v.camera!=&townCam && !(allParts&4u)) continue;
        const auto off=picture(*v.camera, 0.f, options), again=picture(*v.camera, 0.f, options), on=picture(*v.camera, allOn, options);
        const Differs repeat=differ(off, again), switched=differ(off, on);
        save(off, (std::string(v.file)+"-off.png").c_str(), true); save(on, (std::string(v.file)+"-on.png").c_str(), true);
        std::printf("      speed-1 picture, %s, 16 frames: mean %.6f with the switches off, %.6f on; pixels that differ %zu (largest %.6g, mean %.3g); "
                    "two runs with them off: %zu (largest %.6g, mean %.3g)\n", v.name, switched.meanA, switched.meanB, switched.pixels, switched.most, switched.mean,
                    repeat.pixels, repeat.most, repeat.mean);
        expect(switched.meanA>1e-4, "the speed-1 view shows a lit picture");
        expect(switched.most<=repeat.most*1.5+1e-9 && std::fabs(switched.meanB-switched.meanA)<=2.0*std::fabs(repeat.meanB-repeat.meanA)+1e-7*switched.meanA,
               "the speed switches leave the picture as it is (no pixel differs by more than two runs without them do)");
        if(v.camera==&townCam) {
            const float savedColor=indirectColor; indirectColor=0.9f;
            const Differs changed=differ(off, picture(*v.camera, 0.f, options));
            indirectColor=savedColor;
            std::printf("      speed-1 picture, the check's own: Indirect Color 0.9 against 1: %zu pixels differ (largest %.6g)\n", changed.pixels, changed.most);
            expect(changed.most>repeat.most*4.0+1e-6 && changed.pixels>size_t(W)*H/100, "the picture check sees a picture that did change");
        }
        if(table && v.camera==&underCam) {
            // The measuring build alone: with the question never asked (8192) the light under the water changes; with the
            // switch for players it does not (above). In a build without the measuring switches this row differs by nothing.
            const Differs never=differ(off, picture(*v.camera, 8192.f, options));
            std::printf("      speed-1 picture, under the water, the question never asked (the measuring build's 8192): %zu pixels differ (largest %.6g)%s\n",
                        never.pixels, never.most, never.pixels ? "" : " - nothing: not a measuring build, or no point under the water asks");
        }
    }

    // 2. and 3. The stages' GPU times.
    static const char* const kStageNames[10]={"scene ", "prepare ", "bounced light ", "bounced light spatial reuse ", "direct light ", "reflections ", "radiance cache ", "denoising ", "caustics ", "composite "};
    struct Times { double stage[10]; double gpu; int windows; };
    const Camera* timeCam=&townCam;
    auto timed=[&](uint32_t with, float trial) {
        Times t{}; for(double& s:t.stage) s=1e30; t.gpu=1e30;
        speedTrial=trial;
        char last[1024]{}, now[1024]{};
        GetStageTimes(last, sizeof(last));
        int changes=0;
        const auto start=std::chrono::steady_clock::now();
        // The first two windows after a change of switches may hold frames from before it: the three after them count.
        while(t.windows<3 && std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<40.0) {
            run(TGPT_VIEW_NONE, *timeCam, *timeCam, frame++, with); context->Flush();
            ReadStatus(&status);
            if(changes>=2) t.gpu=std::min(t.gpu, double(status.gpuMs));
            GetStageTimes(now, sizeof(now));
            if(std::strcmp(now, last)==0) continue;
            std::memcpy(last, now, sizeof(last));
            if(++changes<=2) continue;
            for(int s=0;s<10;++s) { const char* at=std::strstr(now, kStageNames[s]); if(at) t.stage[s]=std::min(t.stage[s], std::atof(at+std::strlen(kStageNames[s]))); }
            ++t.windows;
        }
        speedTrial=0.f;
        return t;
    };
    Times base{};
    auto row=[&](const char* name, const Times& t, bool against) {
        std::printf("      speed-1 time | %-46s | frame %7.2f | bounced %7.2f | reflections %7.2f | direct %6.2f | prepare %6.2f | spatial %5.2f | caustics %5.2f | scene %5.2f | composite %5.2f |",
                    name, t.gpu, t.stage[2], t.stage[5], t.stage[4], t.stage[1], t.stage[3], t.stage[8], t.stage[0], t.stage[9]);
        if(against) std::printf(" against the first: bounced %+7.2f, reflections %+7.2f, direct %+6.2f, frame %+7.2f", t.stage[2]-base.stage[2], t.stage[5]-base.stage[5], t.stage[4]-base.stage[4], t.gpu-base.gpu);
        std::printf("%s\n", t.windows<3 ? " (fewer than three windows)" : "");
        std::fflush(stdout);
    };
    std::printf("      speed-1 times: milliseconds of the GPU, the least of three windows of about a second each; %ux%u, one ray a pixel, one bounce, no denoiser\n", W, H);
    base=timed(options, 0.f); row("as it is (the switches off)", base, false);
    { const Times t=timed(options, 1.f); row("switch 1: which water, only under its top", t, true); }
    if(allOn!=1.f) {
        // The switches on trial, from the town's square and from over the sea (the lamps mirrored in it), each beside "as it is".
        { const Times t=timed(options, allOn); row("the switches of TGPT_TEST_SPEED1_SWITCHES", t, true); }
        { const Times t=timed(options, 0.f); row("as it is, again", t, true); }
        timeCam=&seaCam;
        const Times seaBase=timed(options, 0.f); row("over the sea: as it is", seaBase, false);
        { const Times saved=base; base=seaBase; const Times t=timed(options, allOn); row("over the sea: the switches on trial", t, true);
          const Times again=timed(options, 0.f); row("over the sea: as it is, again", again, true); base=saved; }
        timeCam=&townCam;
    }
    if(table) {
        struct Row { const char* name; uint32_t add, remove, trial, bouncesOf, raysOf; };
        const Row rows[]={
            {"M 16: the pass's own ray not traced", 0, 0, 16, 1, 1},
            {"M 32: what the ray met not shaded", 0, 0, 32, 1, 1},
            {"M 64: materials without textures, wet, decals", 0, 0, 64, 1, 1},
            {"M 128: no decals", 0, 0, 128, 1, 1},
            {"M 256: no sun at path vertices", 0, 0, 256, 1, 1},
            {"M 512: no lamps at path vertices", 0, 0, 512, 1, 1},
            {"M 1024: lamps weighed, no shadow ray", 0, 0, 1024, 1, 1},
            {"M 768: no sun and no lamps", 0, 0, 768, 1, 1},
            {"M 2048: no path feeds the cache", 0, 0, 2048, 1, 1},
            {"M 4096: the cache not read", 0, 0, 4096, 1, 1},
            {"M 8192: which water never asked", 0, 0, 8192, 1, 1},
            {"M 16384: no glass walk, no water path", 0, 0, 16384, 1, 1},
            {"switch 8: plain compute passes (with TGPT_TEST_PLAIN_PASSES)", 0, 0, 8, 1, 1},
            {"option: no subsurface walk", TGPT_OPTION_NO_SUBSURFACE, 0, 0, 1, 1},
            {"option: no temporal reuse", TGPT_OPTION_NO_TEMPORAL, 0, 0, 1, 1},
            {"option: no radiance cache", TGPT_OPTION_NO_RADIANCE_CACHE, 0, 0, 1, 1},
            {"option: Emissive Lights off", 0, TGPT_OPTION_EMISSIVE_LIGHTS, 0, 1, 1},
            {"option: Caustics off", TGPT_OPTION_NO_CAUSTICS, 0, 0, 1, 1},
            {"row Reflections: Glossy Surfaces", TGPT_OPTION_GLOSSY_REFLECTIONS, 0, 0, 1, 1},
            {"row Reflections: Off", TGPT_OPTION_NO_SPECULAR, 0, 0, 1, 1},
            {"row Bounces: 2", 0, 0, 0, 2, 1},
            {"row Rays Per Pixel: 2", 0, 0, 0, 1, 2},
            {"as it is, again", 0, 0, 0, 1, 1},
        };
        for(const Row& r:rows) {
            bounces=r.bouncesOf; rays=r.raysOf;
            const Times t=timed((options|r.add)&~r.remove, float(r.trial));
            bounces=1; rays=1;
            row(r.name, t, true);
        }
        struct Part { const char* name; uint32_t parts; bool wind; bool micromapsOff; };
        const Part partsOf[]={
            {"scene: plants standing still (micromaps)", allParts, false, false},
            {"scene: plants standing still, micromaps off", allParts, false, true},
            {"scene: no plants", allParts&~2u, true, false},
            {"scene: no sea", allParts&~4u, true, false},
            {"scene: 8 lamps for the 510", allParts&~1u, true, false},
            {"scene: no glowing cubes", allParts&~8u, true, false},
            {"scene: no decals", allParts&~16u, true, false},
            {"scene: no subsurface figures, no glass", allParts&~32u, true, false},
            {"scene: every solid instance tested by the shader", allParts|64u, true, false},
            {"scene: the whole town again", allParts, true, false},
        };
        uint32_t lastParts=allParts; bool lastWind=true;
        for(const Part& p:partsOf) {
            if(p.parts!=lastParts || p.wind!=lastWind) {
                if(!town(p.parts, p.wind)) { std::printf("FAILED: the speed-1 scene, %s\n", p.name); return 1; }
                settle(p.name, 120);
                lastParts=p.parts; lastWind=p.wind;
            }
            const Times t=timed(options|(p.micromapsOff ? TGPT_OPTION_NO_MICROMAPS : 0u), 0.f);
            row(p.name, t, true);
            if((p.parts&4u) && !p.wind && !p.micromapsOff) { const Times s=timed(options, 1.f); row("  and switch 1", s, true); }
        }
    }
    ReadStatus(&status);
    expect(status.framesFailed==0, "the speed-1 runs traced without a failed frame");
    SetDecals(nullptr, 0); SetWaterCaustics(nullptr, 0);
    { TGPT_PlantWindFrame none{}; SetPlantWind(&none, nullptr, 0); }
    const int own=failures-failuresBefore;
    if(failuresBefore) std::printf("      (%d checks of the test's earlier parts failed before the speed-1 checks: not theirs, the run is at another size)\n", failuresBefore);
    std::printf(own ? "FAILED %d speed-1 checks\n" : "speed-1 checks passed\n", own);
    return own ? 1 : 0;
}
