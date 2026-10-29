// SPDX-License-Identifier: MIT
// TGPT_TEST_DECAL_MOVE_ONLY=1 (PT-134, docs/agents/fixt.md and docs/agents/stale.md F1; included in tgpt_gpu_test right
// after its warm-up, and run alone: the whole test does not take it): a mesh decal still shows, and the device still
// stands, after its mesh's structure was compacted and its old memory given back in QUIET frames.
//   The fault it holds (scene.cpp upload_decals, compact_meshes): a mesh decal has a top level of its own, built with the
//   ADDRESS of its mesh's bottom level. A still mesh's bottom level is built at full size in the builds' own chunks and,
//   two or three frames later, copied compacted to another address; the old piece is given back, and 120 collects after
//   the chunk's last piece left the whole chunk goes back to the driver. Until 2026-10-12 upload_decals looked at a
//   decal's top level again only when the decals changed, a mesh was converted in that very frame, a texture came or
//   went or the origin moved: in a quiet frame the top level kept the old address, and every hit in the decal's box ran
//   a ray query into memory that was no longer there (a fault, or a walk with no end: a reset of the GPU).
//   The scene: the decals check's floor, and ONE mesh decal on it whose mesh is registered here, so that its bottom
//   level is new. The decal is handed over BEFORE its mesh is converted (as a scene arrives in the game); from then on
//   nothing is registered and the decals are not touched: every frame is quiet.
//   What is judged, frame by frame, WITHOUT a read of freed memory: with the test's switch TGPT_TEST_SCENE_ORDER the
//   tracer says (scene.cpp record, after upload_decals) each named top level's mesh's address as it is now and the
//   address the top level was built with, whenever either changes. They must never differ. On a tracer with the fault
//   (the mutant: upload_decals' early way out without the address check) they differ in the frame of the compaction:
//   THE MODE FAILS THERE AND ENDS. The old piece is still whole in that frame (it is given back at that frame's fence,
//   its chunk 120 collects later), so no frame is traced through memory that is gone and no reset is needed to show it.
//   Then, on a tracer without the fault: frames until the tracer's own line says the full-size builds' chunks went back
//   to the driver (TGPT_TEST_MEMORY2_OFF=0: nothing switched off, the arenas' line said at every change), thirty more,
//   and the picture: the first hits' colour on the floor is the decal's red, the same as with the decal handed over
//   anew, and (the control) no longer red once the decal is taken away. After every frame the status is read: a failed
//   frame, an error or "disabled" (a lost device: the tracer's own fence wait ends a hung frame) fails the mode at once.
{
    std::printf("\nA mesh decal follows its mesh when the mesh's structure is compacted in a quiet frame (PT-134)\n");
    SetEnvironmentVariableA("TGPT_TEST_SCENE_ORDER", "1");
    SetEnvironmentVariableA("TGPT_TEST_MEMORY2_OFF", "0");
    expect(SetDecals!=nullptr, "decal export exists");
    if(!SetDecals) { std::printf("FAILED: no decal export\n"); return 1; }
    const fs::path moveLog=root/"TGPathTracing"/"TGPathTracing.Native.log";
    // The tracer's log from where this mode starts: the whole lines added since the last call that hold the marker.
    std::uintmax_t moveLogAt=0;
    { std::error_code sized; const auto size=fs::file_size(moveLog, sized); if(!sized) moveLogAt=size; }
    auto new_lines=[&]() {
        std::vector<std::string> found;
        std::ifstream in(moveLog, std::ios::binary);
        if(!in) return found;
        in.seekg(std::streamoff(moveLogAt));
        std::string rest((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const size_t whole=rest.rfind('\n');
        if(whole==std::string::npos) return found;
        moveLogAt+=whole+1;
        size_t at=0;
        while(at<=whole) {
            const size_t end=rest.find('\n', at);
            const std::string line=rest.substr(at, end-at);
            if(line.find("PT test, mesh decals' top levels:")!=std::string::npos || line.find("PT memory, the arenas")!=std::string::npos) found.push_back(line);
            at=end+1;
        }
        return found;
    };
    const auto savedObjects=objects;
    const bool savedOwn=ownRays; ownRays=true;
    const uint32_t savedBounces=bounces; bounces=1;
    auto dm=materials[0]; dm.albedoTile=-1; dm.flags=TGPT_MATERIAL_DOUBLE_SIDED;
    dm.baseColor[0]=dm.baseColor[1]=dm.baseColor[2]=0.5f; dm.metallic=0; dm.smoothness=0.1f;
    SetMaterials(0, &dm, 1);
    objects={ {0, translate_scale({0, 0, 0}, {10, 1, 12}), {0}} };
    {
        TGPT_Instance inst{}; for(int r=0;r<3;++r) for(int c=0;c<4;++c) inst.transform[4*r+c]=objects[0].world.m[r][c];
        inst.mesh=quadHandle; inst.materialBase=0; inst.mask=0xFF; inst.renderingLayerMask=1;
        const uint32_t floorMaterial=0;
        expect(SetScene(&inst, 1, &floorMaterial, 1)==0, "the floor alone");
    }
    const Camera dc=make_camera({0, 3, -7}, {0, 1, 2}, 55.f, float(W)/float(H));
    // The decals check's own decal: 8 m square on the floor, red.
    auto decal=[] {
        TGPT_Decal d{};
        d.worldToDecal[0]=1.f/8; d.worldToDecal[5]=1; d.worldToDecal[10]=1.f/8;
        d.normalToWorld[0]=1; d.normalToWorld[6]=1; d.normalToWorld[9]=1;
        d.boundsMin[0]=d.boundsMin[2]=-4; d.boundsMin[1]=-0.5f;
        d.boundsMax[0]=d.boundsMax[2]=4; d.boundsMax[1]=0.5f;
        d.layerMask=1; d.flags=1;
        d.baseColor[0]=0.8f; d.baseColor[1]=0.04f; d.baseColor[2]=0.02f; d.baseColor[3]=1;
        d.emissiveBlend[3]=1; d.uvScaleOffset[0]=d.uvScaleOffset[1]=1;
        d.fade[0]=d.fade[3]=1;
        d.remapMin[1]=d.remapMax[1]=1; d.remapMin[3]=1;
        for(auto& t:d.textures) t=-1;
        return d;
    };
    // ... as a mesh decal: its own triangles, a hundredth of a metre over the floor.
    auto mesh_decal=[&](int handle) {
        auto mesh=decal(); mesh.flags|=1024|2048; mesh.reserved[0]=uint32_t(handle); mesh.reserved[1]=0;
        mesh.graphWorld[0]=mesh.graphWorld[10]=8; mesh.graphWorld[5]=1; mesh.graphWorld[7]=0.01f;
        mesh.worldToDecal[7]=-0.01f; mesh.boundsMin[1]=mesh.boundsMax[1]=0.01f;
        return mesh;
    };
    const uint32_t options=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_CLAMPS|TGPT_OPTION_NO_BOILING|TGPT_OPTION_NO_GI_BOILING;
    uint32_t frames=0;
    auto one=[&](uint32_t index) { run(TGPT_VIEW_ALBEDO, dc, dc, index, options); ++frames; };
    // The first hits' colour, the mean of five frames after two.
    auto shot=[&]() {
        std::vector<float> sum(size_t(W)*H*3, 0);
        for(int f=0;f<7;++f) {
            one(f ? uint32_t(72000+f) : 0u);
            auto img=readback();
            if(f>=2) for(size_t i=0;i<img.size();++i) sum[i]+=img[i]/5;
        }
        return sum;
    };
    // The floor's pixels inside the decal (the decals check's region).
    std::vector<size_t> floorPixels;
    for(UINT y=0;y<H;++y) for(UINT x=0;x<W;++x) {
        const float v[4]={(x+0.5f)/W*2-1, 1-(y+0.5f)/H*2, 1, 1}; float p[4]{};
        for(int r=0;r<4;++r) for(int k=0;k<4;++k) p[r]+=dc.invViewProj.m[r][k]*v[k];
        const V3 dir=normalize(V3{p[0]/p[3], p[1]/p[3], p[2]/p[3]});
        if(dir.y>=-0.01f || dir.z<=0) continue;
        const V3 floor=dc.position+dir*(-dc.position.y/dir.y);
        if(floor.z<3.8f && std::fabs(floor.x)<3.3f && std::fabs(floor.z)<3.3f) floorPixels.push_back((size_t(y)*W+x)*3);
    }
    expect(floorPixels.size()>1000, "the decal's floor has measured pixels");
    auto mean=[](const std::vector<float>& img, const std::vector<size_t>& region, int channel) {
        double sum=0; for(size_t i:region) sum+=img[i+channel]; return sum/std::max(size_t(1), region.size());
    };
    auto difference=[](const std::vector<float>& a, const std::vector<float>& b, const std::vector<size_t>& region) {
        double sum=0; for(size_t i:region) for(int k=0;k<3;++k) sum+=std::fabs(a[i+k]-b[i+k]); return sum/std::max(size_t(1), region.size()*3);
    };
    auto lost=[&](uint64_t failedBefore) { ReadStatus(&status); return status.disabled || status.lastError!=0 || status.framesFailed>failedBefore; };
    SetDecals(nullptr, 0);
    const auto off=shot();
    ReadStatus(&status);
    const uint64_t failedBefore=status.framesFailed;
    const uint32_t readyBefore=status.meshesReady;
    (void)new_lines(); // (what the frames so far said is not this decal's)

    // The decal's own mesh, new: the room's quad once more. And the decal at once, before the mesh is converted.
    MeshBuffers moveQuad=make_mesh(quad, {{0, 2, 1, 0, 3, 2}});
    const int moveHandle=register_mesh(moveQuad, 0x50543134d1ull, true);
    expect(moveHandle>0, "the decal's own mesh registered");
    auto md=mesh_decal(moveHandle);
    expect(SetDecals(&md, 1)==0, "the mesh decal handed over before its mesh is converted");
    // Quiet frames, one at a time; after each, what the tracer said.
    unsigned long long firstAt=0, nowAt=0, builtWith=0;
    unsigned named=0, stale=0;
    uint32_t builtFrame=0, movedFrame=0, staleFrame=0, returnedFrame=0, convertedFrame=0;
    bool moved=false, returned=false, wentStale=false;
    double chunksMb=-1.0, mostChunksMb=-1.0;
    std::string lastTop, staleTop;
    const uint32_t loopStart=frames;
    for(uint32_t i=0;i<700;++i) {
        one(73000u+i);
        if(lost(failedBefore)) {
            expect(false, "the device stands in the quiet frames");
            std::printf("FAILED: the device was lost %u frames in (disabled %u, last error 0x%08X, %llu frames failed): the run ends here\n", frames-loopStart,
                        status.disabled, unsigned(status.lastError), (unsigned long long)(status.framesFailed-failedBefore));
            return 1;
        }
        if(!convertedFrame && status.meshesReady>readyBefore) convertedFrame=frames-loopStart;
        for(const std::string& line:new_lines()) {
            const size_t top=line.find("PT test, mesh decals' top levels:");
            if(top!=std::string::npos) {
                lastTop=line.substr(top);
                std::printf("      frame %u: %s\n", frames-loopStart, lastTop.c_str());
                unsigned n=0, s=0;
                if(std::sscanf(lastTop.c_str(), "PT test, mesh decals' top levels: %u named by a record, %u built", &n, &s)!=2) continue;
                named=n; stale=s;
                const size_t each=lastTop.find(" | decal ");
                unsigned decalIndex=0, meshHandle=0; unsigned long long at=0, built=0;
                if(each!=std::string::npos && std::sscanf(lastTop.c_str()+each, " | decal %u mesh %u at 0x%llx built with 0x%llx", &decalIndex, &meshHandle, &at, &built)==4 &&
                   meshHandle==unsigned(moveHandle)) {
                    nowAt=at; builtWith=built;
                    if(!firstAt && built) { firstAt=built; builtFrame=frames-loopStart; }
                    if(firstAt && at && at!=firstAt && !moved) { moved=true; movedFrame=frames-loopStart; }
                }
                if(stale && !wentStale) { wentStale=true; staleFrame=frames-loopStart; staleTop=lastTop; }
            } else {
                const size_t at=line.find("waiting to be compacted ");
                if(at==std::string::npos) continue;
                chunksMb=std::atof(line.c_str()+at+std::strlen("waiting to be compacted "));
                mostChunksMb=std::max(mostChunksMb, chunksMb);
                if(moved && chunksMb==0.0 && !returned) { returned=true; returnedFrame=frames-loopStart; }
            }
        }
        if(wentStale) break;
        if(returned && frames-loopStart>=returnedFrame+30) break;
        context->Flush();
        if(!firstAt) Sleep(5); // (the mesh is read back on a thread of its own)
    }
    const uint32_t quietFrames=frames-loopStart;
    std::printf("      %u frames: the mesh converted in frame %u; its decal's top level first built in frame %u with 0x%llx; the mesh's structure %s (frame %u, now at 0x%llx); "
                "the top level built with 0x%llx at the end", quietFrames, convertedFrame, builtFrame, firstAt, moved ? "moved to another address" : "DID NOT MOVE", movedFrame,
                nowAt, builtWith);
    // (A run that ends at the stale address never waits for the chunks: nothing to say of them.)
    if(!wentStale) std::printf("; the full-size builds' chunks %s (frame %u; %.1f MB at most, %.1f MB at the end)", returned ? "went back to the driver" : "DID NOT GO BACK",
                               returnedFrame, mostChunksMb, chunksMb);
    std::printf("\n");
    expect(firstAt!=0 && named==1, "the tracer says the mesh decal's top level was built, and with which address");
    if(wentStale) {
        expect(false, "no frame is recorded with a mesh decal's top level that names an address its mesh has left");
        std::printf("FAILED: in frame %u the decal's top level still named the address its mesh's structure had just been copied away from (PT-134's fault):\n"
                    "        %s\n        The run ends here. That piece is whole until this frame's fence and its chunk for 120 collects more: no frame was traced through memory "
                    "that is gone.\n", staleFrame, staleTop.c_str());
        return 1;
    }
    expect(moved, "the mesh's structure was compacted to another address in a quiet frame (else the check shows nothing)");
    expect(moved && movedFrame>builtFrame, "and that was after its decal's top level was built with the first address");
    expect(stale==0 && builtWith==nowAt && nowAt!=firstAt, "the top level was built again with the address the mesh's structure has now");
    expect(returned && mostChunksMb>0.0, "the full-size builds' chunk went back to the driver after the mesh had left it");
    if(failures) { std::printf("FAILED %d checks: the order of events the check needs did not happen; its picture would show nothing\n", failures); return 1; }
    // The picture, thirty frames after the old memory went back.
    const auto after=shot();
    if(lost(failedBefore)) { expect(false, "the device stands after the old memory went back"); std::printf("FAILED: the device was lost (last error 0x%08X)\n", unsigned(status.lastError)); return 1; }
    const double red=mean(after, floorPixels, 0), green=mean(after, floorPixels, 1), fromOff=difference(after, off, floorPixels);
    std::printf("      the floor inside the decal after the move: red %.6f green %.6f (bare floor: red %.6f green %.6f); off the bare floor's by %.6f\n",
                red, green, mean(off, floorPixels, 0), mean(off, floorPixels, 1), fromOff);
    save(off, "134-decal-move-bare.png", false); save(after, "134-decal-move-after.png", false);
    expect(red>green*2 && fromOff>0.1, "the mesh decal still paints the floor after its mesh moved and the old memory went back");
    // The same decal handed over anew (its top level built now, with today's address): the same picture.
    SetDecals(nullptr, 0); const auto removed=shot();
    SetDecals(&md, 1); const auto anew=shot();
    std::printf("      against the decal handed over anew: %.6f; with the decal taken away: %.6f off the bare floor's (red %.6f green %.6f)\n",
                difference(after, anew, floorPixels), difference(removed, off, floorPixels), mean(removed, floorPixels, 0), mean(removed, floorPixels, 1));
    expect(difference(after, anew, floorPixels)<0.002, "and the picture is the one of the decal handed over anew");
    expect(difference(removed, off, floorPixels)<0.001 && !(mean(removed, floorPixels, 0)>mean(removed, floorPixels, 1)*2), "the control: with the decal taken away the floor is bare (the picture sees a decal that is gone)");
    ReadStatus(&status);
    std::printf("      frames traced %llu, failed %llu since the mode began, last error 0x%08X\n", (unsigned long long)status.framesTraced,
                (unsigned long long)(status.framesFailed-failedBefore), unsigned(status.lastError));
    expect(!status.disabled && status.framesFailed==failedBefore && status.lastError==0, "no frame failed and the device stands");
    SetDecals(nullptr, 0); SetMaterials(0, materials.data(), uint32_t(materials.size()));
    objects=savedObjects; ownRays=savedOwn; bounces=savedBounces;
    SetScene(instances.data(), uint32_t(instances.size()), indices.data(), uint32_t(indices.size()));
    if(failures) { std::printf("FAILED %d checks\n", failures); return 1; }
    std::printf("decal move checks passed\n");
    return 0;
}
