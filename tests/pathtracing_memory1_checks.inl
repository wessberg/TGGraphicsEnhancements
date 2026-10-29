// SPDX-License-Identifier: MIT
// PT-87 (docs/agents/memory-1.md): what a setting or a scene does not need is not held, and the picture does not know.
// Alone: TGPT_TEST_MEMORY1_ONLY=1 (included by pathtracing_gpu_test.cpp right after the warm-up; a minute or two).
//
// The tracer lets go of what frames no longer run (the built-in filters' textures, a denoiser's own, the caustics' room)
// and makes it again when a frame needs it. TGPT_TEST_KEEP_IDLE=1 (read by the native side) keeps everything, as before
// PT-87, and gives the caustics the room of their limit, as before. So each case runs the same frames twice, once keeping
// and once letting go, and compares the two: the memory must differ and every pixel must be the same.
// (docs/agents/memory-2.md) Cases 5 and on are the cuts that give back memory the tracer holds and does not use: each runs
// the same frames with the cut and without it (TGPT_TEST_MEMORY2_OFF, scene.hpp memory2_off) and compares every pixel.
// TGPT_TEST_MEMORY2_ONLY=1 runs those alone.
const bool memory2Only=GetEnvironmentVariableA("TGPT_TEST_MEMORY2_ONLY", nullptr, 0)>0;
if(GetEnvironmentVariableA("TGPT_TEST_MEMORY1_ONLY", nullptr, 0)>0 || memory2Only) {
    auto memory=[&]() { TGPT_Diagnostics d{sizeof(d), TGPT_ABI}; Sleep(1100); render(7, nullptr); ReadDiagnostics(&d); return d; };
    auto mb=[](uint64_t bytes) { return double(bytes)/1048576.0; };
    struct Differ { double most=0, mean=0; };
    auto differ=[](const std::vector<float>& a, const std::vector<float>& b) {
        Differ d; if(a.size()!=b.size() || a.empty()) { d.most=d.mean=1e30; return d; }
        double sum=0;
        for(size_t i=0;i<a.size();++i) { const double x=std::fabs(double(a[i])-double(b[i])); sum+=x; d.most=std::max(d.most, std::isfinite(x) ? x : 1e30); }
        d.mean=sum/double(a.size()); return d;
    };
    const uint32_t quiet=TGPT_OPTION_NO_RADIANCE_CACHE; // the cache's cells are written by whichever thread comes first
    ReadStatus(&status);
    const bool nrd=(status.features&TGPT_FEATURE_NRD)!=0;
    // 1. The built-in filters' textures. Twelve frames of the built-in denoiser, 140 of no denoiser (more than the 120 after
    // which what is idle goes), twelve of the built-in denoiser again: the frames' numbers run on, so the second dozen
    // resumes the filters without any other history starting over.
    struct Run { std::vector<float> first, raw, again; TGPT_Diagnostics a{}, b{}, c{}; };
    auto filters=[&](bool keep, uint32_t idleOptions) {
        SetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", keep ? "1" : nullptr);
        Run r;
        for(uint32_t i=0;i<12;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 31000+i : 0, quiet);
        r.first=readback(); r.a=memory();
        for(uint32_t i=0;i<140;++i) run(TGPT_VIEW_NONE, cam, cam, 31012+i, quiet|idleOptions);
        r.raw=readback(); r.b=memory();
        for(uint32_t i=0;i<12;++i) run(TGPT_VIEW_NONE, cam, cam, 31152+i, quiet);
        r.again=readback(); r.c=memory();
        return r;
    };
    if(!memory2Only) {
        // A run first that is not judged: the first one after the warm-up starts from another state than those that follow
        // a run like themselves (measured 2026-10-09: it differed from the two after it by 0.307 in a channel, and they not).
        filters(true, TGPT_OPTION_NO_DENOISER);
        const Run kept=filters(true, TGPT_OPTION_NO_DENOISER), gone=filters(false, TGPT_OPTION_NO_DENOISER), keptAgain=filters(true, TGPT_OPTION_NO_DENOISER);
        const uint64_t before=gone.a.histories+gone.a.targets, idle=gone.b.histories+gone.b.targets, after=gone.c.histories+gone.c.targets;
        std::printf("      the built-in filters' textures (%u x %u): histories + targets %.1f MB with the built-in denoiser, %.1f MB after 140 frames of no denoiser, "
                    "%.1f MB with it again (kept: %.1f, %.1f, %.1f MB)\n", W, H, mb(before), mb(idle), mb(after),
                    mb(kept.a.histories+kept.a.targets), mb(kept.b.histories+kept.b.targets), mb(kept.c.histories+kept.c.targets));
        // Eighteen textures: twelve of 8 bytes a pixel and six of 16.
        expect(before>idle && before-idle>=uint64_t(W)*H*(12u*8u+6u*16u), "with no denoiser the built-in filters' eighteen textures are let go");
        expect(after==before, "and made again when the built-in denoiser runs again");
        expect(kept.b.histories+kept.b.targets==kept.a.histories+kept.a.targets, "kept by the test's switch, nothing is let go (the comparison's other side)");
        const Differ first=differ(kept.first, gone.first), raw=differ(kept.raw, gone.raw), again=differ(kept.again, gone.again);
        const Differ floorFirst=differ(kept.first, keptAgain.first), floorAgain=differ(kept.again, keptAgain.again);
        const Differ next=differ(gone.again, keptAgain.again);
        std::printf("      kept against let go, the largest difference of any pixel's channel: before %.3g, with no denoiser %.3g, the built-in denoiser again %.3g "
                    "(mean %.3g); the same run twice, both kept: %.3g and %.3g; let go against the kept run after it: %.3g (mean %.3g)\n",
                    first.most, raw.most, again.most, again.mean, floorFirst.most, floorAgain.most, next.most, next.mean);
        // (Twice what the same run differs by from itself, and the least a half float's step can be told apart by.)
        expect(first.most<=floorFirst.most*2.0+1e-6 && raw.most<=std::max(floorFirst.most, floorAgain.most)*2.0+1e-6,
               "until anything is let go the two runs are the same frames (no more apart than the same run twice)");
        expect(again.most<=floorAgain.most*2.0+1e-6, "the built-in denoiser's picture is the same from textures made again as from textures kept");
        save(gone.again, "87-filters-made-again.png", true); save(kept.again, "87-filters-kept.png", true);
    }
    // And with NRD, where the build has it: the bounced light's six go (NRD takes it), the direct light's and the reflections'
    // twelve stay; back with the built-in denoiser they are made again, and NRD's own go 120 frames later.
    if(memory2Only) {}
    else if(nrd) {
        const Run kept=filters(true, TGPT_OPTION_NRD), gone=filters(false, TGPT_OPTION_NRD);
        const uint64_t before=gone.a.histories+gone.a.targets, idle=gone.b.histories+gone.b.targets, after=gone.c.histories+gone.c.targets;
        std::printf("      with NRD: histories + targets %.1f MB with the built-in denoiser, %.1f MB after 140 frames of NRD, %.1f MB with the built-in one again; "
                    "NRD's own %.1f MB while it runs\n", mb(before), mb(idle), mb(after), mb(gone.b.denoiser));
        expect(before>idle && before-idle>=uint64_t(W)*H*(4u*8u+2u*16u) && before-idle<uint64_t(W)*H*(12u*8u+6u*16u),
               "with NRD the bounced light's six textures are let go and the direct light's and the reflections' stay");
        expect(after==before && gone.b.denoiser>0, "and made again when the built-in denoiser runs again; NRD's own are there while it runs");
        const Differ with=differ(kept.raw, gone.raw), again=differ(kept.again, gone.again);
        std::printf("      kept against let go: NRD's picture %.3g, the built-in denoiser's after it %.3g\n", with.most, again.most);
        expect(with.most<1e-3 && again.most<1e-3, "NRD's picture and the built-in denoiser's after it do not know whether the idle textures were kept");
        SetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", nullptr);
        for(uint32_t i=0;i<140;++i) run(TGPT_VIEW_NONE, cam, cam, 31200+i, quiet|TGPT_OPTION_NO_DENOISER);
        const TGPT_Diagnostics after140=memory();
        std::printf("      NRD's own after 140 frames of no denoiser: %.1f MB\n", mb(after140.denoiser));
        expect(after140.denoiser==0, "NRD's textures and pools are let go once no frame runs it");
    } else std::printf("      (this build has no NRD: its part of the check does not run)\n");
    // 2. The wet ground's store and the caustics' room. A pool of HDRP's water with a sine wave over the sunlit floor (the
    // caustics' own fixture, further down). Each run: 40 frames with no denoiser from the same numbers, the last one read;
    // between runs 130 frames without the water's slot, after which the room is let go (not when kept).
    if(!memory2Only) {
        const double period=0.4, amplitude=0.03; const int texels=128;
        ComPtr<ID3D11Texture2D> waveDisplacement, waveGradient;
        {
            std::vector<float> disp(size_t(texels)*texels*4, 0.f), grad(size_t(texels)*texels*4, 0.f);
            for(int y=0;y<texels;++y) for(int x=0;x<texels;++x) {
                const double phase=2.0*3.14159265358979*(double(x)+0.5)/texels;
                disp[(size_t(y)*texels+x)*4]=float(amplitude*std::sin(phase));
                grad[(size_t(y)*texels+x)*4]=float(amplitude*2.0*3.14159265358979/period*std::cos(phase));
            }
            D3D11_TEXTURE2D_DESC d{}; d.Width=d.Height=texels; d.MipLevels=1; d.ArraySize=1; d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; d.SampleDesc.Count=1;
            d.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA a{disp.data(), UINT(texels*16), 0}, b{grad.data(), UINT(texels*16), 0};
            expect(SUCCEEDED(device->CreateTexture2D(&d, &a, &waveDisplacement)) && SUCCEEDED(device->CreateTexture2D(&d, &b, &waveGradient)), "HDRP's wave buffers for the memory check");
        }
        auto water=[&](bool on) {
            TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI;
            c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
            c.displacement=uint64_t(uintptr_t(waveDisplacement.Get())); c.normals=uint64_t(uintptr_t(waveGradient.Get()));
            c.bands[0][0]=float(1.0/period); c.bands[0][3]=1.f; c.fades[0][0]=0.f; c.fades[0][1]=1.f; c.bandCount=1;
            c.maxDisplacement=float(amplitude);
            c.top=0.4f; c.bottom=0.4f;
            c.bounds[0]=-1.5f; c.bounds[1]=-1.2f; c.bounds[2]=0.9f; c.bounds[3]=0.4f;
            return SetWaterCaustics(on ? &c : nullptr, on ? 1u : 0u)==0;
        };
        const TGPT_Diagnostics dry=memory();
        {
            auto list=instances; auto listIndices=indices;
            const M4 pool=translate_scale({-0.3f, 0.4f, -0.4f}, {2.4f, 1.f, 1.6f});
            TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=pool.m[r][c];
            w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=TGPT_INSTANCE_WATER;
            listIndices.push_back(0); list.push_back(w);
            expect(SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0, "the room with a pool of water in it");
        }
        const Camera poolCam=make_camera({-0.9f, 1.6f, -2.2f}, {-0.9f, 0.f, -0.5f}, 60.f, float(W)/float(H));
        struct Pool { std::vector<float> last; TGPT_Diagnostics with{}; };
        auto pool_run=[&](bool keep) {
            SetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", keep ? "1" : nullptr);
            Pool p;
            expect(water(true), "the pool's water slot");
            for(uint32_t i=0;i<40;++i) run(TGPT_VIEW_DIRECT, poolCam, poolCam, i ? 33000+i : 0, quiet|TGPT_OPTION_NO_DENOISER);
            p.last=readback(); p.with=memory();
            return p;
        };
        // The gap: no water slot for 130 frames (never kept: the room goes, so that every run starts from none).
        auto gap=[&]() {
            SetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", nullptr);
            expect(water(false), "no water slot");
            for(uint32_t i=0;i<130;++i) run(TGPT_VIEW_DIRECT, poolCam, poolCam, 34000+i, quiet|TGPT_OPTION_NO_DENOISER);
            return memory();
        };
        pool_run(false); gap(); // the first run settles how many triangles a frame lands (what the next frames build over)
        const Pool fitted=pool_run(false); const TGPT_Diagnostics between=gap();
        const Pool again=pool_run(false); gap();
        const Pool whole=pool_run(true);
        std::printf("      the caustics' memory with the pool: %.1f MB with room for what a frame builds over, %.1f MB with the limit's room (as before); "
                    "%.1f MB after 130 frames without water; the wet ground's store %.1f MB before any water, %.1f MB with it\n",
                    mb(fitted.with.caustics), mb(whole.with.caustics), mb(between.caustics), mb(dry.wetStore), mb(fitted.with.wetStore));
        expect(dry.wetStore==0 && fitted.with.wetStore>0, "the wet ground's store is made by the first frame that has water");
        expect(fitted.with.caustics>0 && whole.with.caustics>=fitted.with.caustics+(100ull<<20), "the caustics' room is what a frame builds over, not the limit's (over 100 MB less here)");
        expect(between.caustics<fitted.with.caustics, "without a water slot the caustics' room is let go");
        const Differ rooms=differ(fitted.last, whole.last), same=differ(fitted.last, again.last);
        size_t lit=0; for(size_t i=0;i<fitted.last.size();i+=3) lit+=fitted.last[i+1]>0.05f;
        std::printf("      the pool's picture, the small room against the limit's: the largest difference of any pixel's channel %.3g (mean %.3g); the small room twice: %.3g "
                    "(mean %.3g); lit pixels %zu\n", rooms.most, rooms.mean, same.most, same.mean, lit);
        expect(lit>size_t(W)*H/50, "the pool's picture shows the lit floor");
        // The triangles land in the order the GPU's threads come: two runs of the same frames differ by rounding, and no
        // more than that may lie between the two rooms.
        expect(rooms.mean<=same.mean*2.0+1e-7 && rooms.most<=same.most*2.0+1e-4, "the caustics' picture is the same with the small room as with the limit's");
        save(fitted.last, "87-caustics-small-room.png", true); save(whole.last, "87-caustics-limit-room.png", true);
        // 3. A room that GROWS in the middle of a run: a pool of twice the size lands about twice the triangles: more than
        // half of the smallest room, so the frames build over twice as many after the first few, and fewer than that room
        // holds, so none is ever left out (left out, the first frames would differ from run to run by the order the GPU's
        // threads come in: a lake over the whole floor did, by 91.8 in a channel's sum, with either room).
        // Each round: 40 frames of the lake from a room made from nothing while few triangles were landing (so the room
        // starts small and grows with a copy of the frame before's light, where the limit's room never moves), every
        // frame read and summed; then 60 frames of the pool (what lands falls again) and the gap (the room goes).
        // The sum of all 40 frames must be the same with the grown room as with the limit's: one wrong frame shows.
        auto lake=[&](bool on) {
            auto list=instances; auto listIndices=indices;
            const M4 at=on ? translate_scale({-0.3f, 0.4f, -0.4f}, {3.4f, 1.f, 2.3f}) : translate_scale({-0.3f, 0.4f, -0.4f}, {2.4f, 1.f, 1.6f});
            TGPT_Instance w{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) w.transform[r*4+c]=at.m[r][c];
            w.mesh=quadHandle; w.materialBase=uint32_t(listIndices.size()); w.mask=0xFF; w.flags=TGPT_INSTANCE_WATER;
            listIndices.push_back(0); list.push_back(w);
            TGPT_WaterCaustics c{}; c.size=sizeof(c); c.abi=TGPT_ABI;
            c.worldToWater[0]=c.worldToWater[5]=c.worldToWater[10]=1.f;
            c.displacement=uint64_t(uintptr_t(waveDisplacement.Get())); c.normals=uint64_t(uintptr_t(waveGradient.Get()));
            c.bands[0][0]=float(1.0/period); c.bands[0][3]=1.f; c.fades[0][0]=0.f; c.fades[0][1]=1.f; c.bandCount=1;
            c.maxDisplacement=float(amplitude);
            c.top=0.4f; c.bottom=0.4f;
            if(on) { c.bounds[0]=-2.0f; c.bounds[1]=-1.55f; c.bounds[2]=1.4f; c.bounds[3]=0.75f; }
            else { c.bounds[0]=-1.5f; c.bounds[1]=-1.2f; c.bounds[2]=0.9f; c.bounds[3]=0.4f; }
            return SetScene(list.data(), uint32_t(list.size()), listIndices.data(), uint32_t(listIndices.size()))==0 && SetWaterCaustics(&c, 1u)==0;
        };
        struct Lake { std::vector<double> sum; TGPT_Diagnostics with{}; };
        auto lake_round=[&](bool keep) {
            SetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", keep ? "1" : nullptr);
            Lake l; l.sum.assign(size_t(W)*H*3, 0.0);
            expect(lake(true), "a pool of twice the size");
            for(uint32_t i=0;i<40;++i) {
                run(TGPT_VIEW_DIRECT, poolCam, poolCam, i ? 35000+i : 0, quiet|TGPT_OPTION_NO_DENOISER);
                const auto frame=readback(); for(size_t j=0;j<frame.size();++j) l.sum[j]+=double(frame[j]);
            }
            l.with=memory();
            // What lands falls back to the pool's, and the room goes: the next round starts small again.
            SetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", nullptr);
            expect(lake(false), "the pool again");
            for(uint32_t i=0;i<60;++i) run(TGPT_VIEW_DIRECT, poolCam, poolCam, 36000+i, quiet|TGPT_OPTION_NO_DENOISER);
            gap();
            return l;
        };
        auto differ_sums=[](const std::vector<double>& a, const std::vector<double>& b) {
            Differ d; double sum=0;
            for(size_t i=0;i<a.size();++i) { const double x=std::fabs(a[i]-b[i]); sum+=x; d.most=std::max(d.most, std::isfinite(x) ? x : 1e30); }
            d.mean=a.empty() ? 1e30 : sum/double(a.size()); return d;
        };
        lake_round(false); // not judged: it leaves the count of landed triangles where every later round finds it
        const Lake grown=lake_round(false), grownAgain=lake_round(false), limit=lake_round(true);
        const Differ lakeRooms=differ_sums(grown.sum, limit.sum), lakeSame=differ_sums(grown.sum, grownAgain.sum);
        std::printf("      a pool of twice the size: the caustics' memory %.1f MB with the room grown to what lands, %.1f MB with the limit's room; the sum of its 40 frames, "
                    "the grown room against the limit's: the largest difference of any pixel's channel %.3g (mean %.3g); the grown room twice: %.3g (mean %.3g)\n",
                    mb(grown.with.caustics), mb(limit.with.caustics), lakeRooms.most, lakeRooms.mean, lakeSame.most, lakeSame.mean);
        expect(grown.with.caustics>fitted.with.caustics+(8ull<<20), "over the larger pool the room has grown past the small pool's (more triangles land)");
        expect(lakeRooms.mean<=lakeSame.mean*2.0+1e-7 && lakeRooms.most<=lakeSame.most*2.0+1e-4,
               "a room that grows in the middle of a run gives the same 40 frames as the limit's room (the frame before's light goes along)");
        SetEnvironmentVariableA("TGPT_TEST_KEEP_IDLE", nullptr);
        expect(SetWaterCaustics(nullptr, 0u)==0 && SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()))==0, "the room without water again");
    }
    // 4. Off gives the memory back (release_frame_memory): the plugin's maintenance event says "off" (render event 7 with data
    // 1) and no frame has been handed over for five seconds. What only a traced frame needs goes; the next frames make it
    // again and show the same picture as the same frames did before.
    if(!memory2Only) {
        auto twelve=[&]() { for(uint32_t i=0;i<12;++i) run(TGPT_VIEW_NONE, cam, cam, i ? 37000+i : 0, quiet); return readback(); };
        twelve(); // (not judged: the state the water's rounds left)
        const auto before=twelve(); const TGPT_Diagnostics on=memory();
        Sleep(5300);
        render(7, reinterpret_cast<void*>(uintptr_t(1)));
        const TGPT_Diagnostics off=memory();
        const auto after=twelve(); const TGPT_Diagnostics back=memory();
        const Differ same=differ(before, after);
        std::printf("      off for five seconds: targets + histories + denoiser %.1f MB while tracing, %.1f MB off, %.1f MB tracing again; the same twelve frames before and after: "
                    "the largest difference of any pixel's channel %.3g (mean %.3g)\n", mb(on.targets+on.histories+on.denoiser), mb(off.targets+off.histories+off.denoiser),
                    mb(back.targets+back.histories+back.denoiser), same.most, same.mean);
        expect(off.histories==0 && off.targets<on.targets/4 && off.total+(100ull<<20)<on.total, "off, what only a traced frame needs is let go");
        expect(back.targets==on.targets && back.histories==on.histories, "and the next traced frames make it again");
        // (Not pixel for pixel by necessity: the frames after it start from nothing, the ones before from a run before them,
        // and a run's first frames are known to differ by where the last run left off. The same picture all the same.)
        bool finite=true; for(const float v:after) finite=finite && std::isfinite(v);
        expect(finite && same.mean<=0.01, "the picture after it is the picture before it");
        // Not off (data 0: a pause for textures, or an older plugin): nothing goes.
        Sleep(5300);
        render(7, nullptr);
        const TGPT_Diagnostics paused=memory();
        expect(paused.targets==on.targets && paused.histories==on.histories, "a pause that is not 'off' lets go of nothing");
    }
    // 5 and on (docs/agents/memory-2.md): memory held and not used is given back.
#include "pathtracing_memory2_checks.inl"
    std::printf(failures ? "FAILED %d checks\n" : (memory2Only ? "memory-2 checks passed\n" : "memory-1 and memory-2 checks passed\n"), failures);
    return failures ? 1 : 0;
}
