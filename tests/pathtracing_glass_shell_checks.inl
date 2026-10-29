// SPDX-License-Identifier: MIT
// PT-13 / PT-35: only this small, undenoised fixture runs with TGPT_TEST_GLASS_SHELL_ONLY.
// The red/green ratio of the emitting ruler gives its height independent of glass throughput;
// blue/green gives straight stripes. Thus a bright but shifted view cannot pass as clear glass.
if (glassShellOnly) {
    ownRays=true; sunOn=false; skyIntensity=0.f; SetLights(nullptr,0);
    const uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_CAUSTICS|TGPT_OPTION_NO_CLAMPS;
    std::vector<Vertex> vv; std::vector<uint16_t> ii;
    // Optional local game-mesh export (positions, vertex normals and triangles only); no game asset lives in the repo.
    const char* meshPath=std::getenv("TGPT_TEST_JAR_MESH");
    if(meshPath) {
        std::ifstream input(meshPath); unsigned nv=0,nt=0; input>>nv>>nt;
        expect(input.good() && nv>0 && nv<65536 && nt>0,"local jar mesh export opens");
        vv.resize(nv); ii.resize(size_t(nt)*3);
        for(auto& a:vv) input>>a.px>>a.py>>a.pz>>a.nx>>a.ny>>a.nz;
        for(auto& a:ii) { unsigned value=0; input>>value; a=uint16_t(value); }
        expect(!input.fail(),"local jar mesh export is complete");
    } else {
        // A 27 cm wide, 49 cm tall, 5 mm shell. Its inner wall has an extra ring and a smooth normal tilted by 4 degrees,
        // like the measured inner normals of Mesh_Prop_Jars_01. The actual two wall planes are parallel.
        constexpr int count=32; constexpr float outer=.135f,inner=.130f,top=.49f;
        auto vertex=[&](float radius,float y,int k,float sign,float ny) {
            const float a=2*pi*(k%count)/count;
            vv.push_back({radius*std::cos(a),y,radius*std::sin(a),sign*std::cos(a),ny,sign*std::sin(a),0,0});
        };
        auto quad=[&](uint16_t a) { ii.insert(ii.end(),{a,uint16_t(a+1),uint16_t(a+2),uint16_t(a+2),uint16_t(a+1),uint16_t(a+3)}); };
        for(int k=0;k<count;++k) {
            auto a=uint16_t(vv.size());
            vertex(outer,0,k,1,0); vertex(outer,top,k,1,0); vertex(outer,0,k+1,1,0); vertex(outer,top,k+1,1,0); quad(a);
            for(int ring=0;ring<2;++ring) {
                a=uint16_t(vv.size());
                vertex(inner,top*ring/2,k+1,-1,-.07f); vertex(inner,top*(ring+1)/2,k+1,-1,-.07f);
                vertex(inner,top*ring/2,k,-1,-.07f); vertex(inner,top*(ring+1)/2,k,-1,-.07f); quad(a);
            }
            a=uint16_t(vv.size());
            vertex(inner,top,k,0,1); vertex(inner,top,k+1,0,1); vertex(outer,top,k,0,1); vertex(outer,top,k+1,0,1); quad(a);
            a=uint16_t(vv.size());
            vertex(inner,0,k+1,0,-1); vertex(inner,0,k,0,-1); vertex(outer,0,k+1,0,-1); vertex(outer,0,k,0,-1); quad(a);
        }
    }
    MeshBuffers shell=make_mesh(vv,{ii}); const int shellId=register_mesh(shell,7420);
    expect(shellId>0,"thin jar shell registered");
    auto glass=materials[10]; glass.ior=1.5f; glass.smoothness=.62f; glass.albedoTile=-1; glass.baseColor[3]=0;
    glass.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_SEE_THROUGH;
    glass.baseColor[0]=glass.baseColor[1]=glass.baseColor[2]=0.f; // black film: it blocks its share without a lit return
    for(float& a:glass.absorption) a=0; glass.opticalReserved[0]=0;
    constexpr uint32_t glassId=230, firstBand=231;
    SetMaterials(glassId,&glass,1);
    std::vector<TGPT_Instance> scene; std::vector<uint32_t> mats;
    auto add=[&](int mesh,const M4& transform,uint32_t material) {
        TGPT_Instance in{}; in.mesh=uint32_t(mesh); in.mask=0xFF; in.flags=TGPT_INSTANCE_ALPHA_TEST;
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=transform.m[r][c];
        in.materialBase=uint32_t(mats.size()); mats.push_back(material); scene.push_back(in);
    };
    constexpr int bands=384; constexpr float bottom=-.25f,span=1.f,wall=.4f,cameraZ=-1.f,fov=35.f;
    for(int band=0;band<bands;++band) {
        const float y=bottom+span*(band+.5f)/bands;
        auto m=materials[0]; m.surfaceModel=1; m.flags=TGPT_MATERIAL_DOUBLE_SIDED; m.albedoTile=-1;
        m.baseColor[0]=y-bottom; m.baseColor[1]=1; m.baseColor[2]=.5f+.4f*std::sin(2*pi*(y-.26f)/.08f); m.baseColor[3]=1;
        SetMaterials(firstBand+band,&m,1);
        add(quadHandle,mul(translate_scale({0,y,wall},{1,span/bands,1}),rot_x(-pi/2)),firstBand+band);
    }
    const Camera eye=make_camera({0,.26f,cameraZ},{0,.26f,0},fov,float(W)/float(H));
    objects.clear(); objects.push_back({0,mul(translate_scale({0,.26f,wall},{1,1,1}),rot_x(-pi/2)),{0}});
    auto measure=[&](float trial,const char* name,int frames) {
        glassTrial=trial;
        expect(SetScene(scene.data(),uint32_t(scene.size()),mats.data(),uint32_t(mats.size()))==0,"jar ruler scene submitted");
        std::vector<float> mean(size_t(W)*H*3,0.f);
        run(TGPT_VIEW_NONE,eye,eye,0,raw);
        for(int frame=0;frame<frames+8;++frame) {
            run(TGPT_VIEW_NONE,eye,eye,uint32_t(41000+frame),raw);
            if(frame<8) continue;
            const auto px=readback(); for(size_t k=0;k<mean.size();++k) mean[k]+=px[k]/frames;
        }
        save(mean,name,true);
        std::ofstream dump(images/(std::string(name)+".f32"),std::ios::binary);
        dump.write(reinterpret_cast<const char*>(mean.data()),std::streamsize(mean.size()*sizeof(float)));
        return mean;
    };
    const auto bare=measure(0,"jar-ruler-bare.png",16);
    add(shellId,identity(),glassId);
    const auto old=measure(7,"jar-ruler-trial7.png",96);
    // Mutation: keep the new check and force the faulty trial 7 transport in the same shader binary.
    const bool mutant=std::getenv("TGPT_TEST_JAR_MUTANT")!=nullptr;
    const auto fixed=measure(mutant ? 7.f : 15.f,"jar-ruler-trial15.png",96);
    glass.baseColor[3]=.175f; SetMaterials(glassId,&glass,1);
    const auto film=measure(15,"jar-ruler-film15.png",96);
    const double pixelsPerMetre=double(H)/(2*std::tan(fov*pi/360)*(wall-cameraZ));
    auto score=[&](const std::vector<float>& image) {
        std::vector<double> shifts; double brightness=0,plain=0,contrast=0,reference=0;
        // Middle half of the shell's projected width; the middle 40 rows of its body.
        const int halfWidth=int(.135*.5*double(H)/(2*std::tan(fov*pi/360)*-cameraZ));
        for(int y=int(H)/2-20;y<int(H)/2+20;++y) for(int x=int(W)/2-halfWidth;x<int(W)/2+halfWidth;++x) {
            const size_t k=(size_t(y)*W+x)*3; const double a=std::max(double(image[k+1]),1e-10),b=std::max(double(bare[k+1]),1e-10);
            shifts.push_back(std::abs(image[k]/a-bare[k]/b)*pixelsPerMetre);
            const double cb=image[k+2]/a-.5,rb=bare[k+2]/b-.5;
            contrast+=cb*cb; reference+=rb*rb; brightness+=a; plain+=b;
        }
        std::sort(shifts.begin(),shifts.end());
        return std::array<double,4>{shifts[shifts.size()/2],shifts[size_t(.95*(shifts.size()-1))],std::sqrt(contrast/reference),brightness/plain};
    };
    const auto a=score(old),b=score(fixed),c=score(film);
    std::printf("jar ruler (%s, %ux%u): trial7 shift p50 %.3f p95 %.3f px, normalized contrast %.3f, light %.3f; trial15 %.3f %.3f px, contrast %.3f, light %.3f; film15 %.3f %.3f px, contrast %.3f, light %.3f\n",
        meshPath ? "game mesh" : "made shell",W,H,a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3],c[0],c[1],c[2],c[3]);
    expect(a[0]>2.0,"trial 7 reproduces broad displacement through a thin shell");
    expect(b[0]<1.0 && b[0]<a[0]*.35 && b[1]<2.0,"actual boundary planes keep the middle-half ruler within a pixel, without broad warping");
    expect(b[2]>.80 && c[2]>.75,"undenoised glass preserves the ruler stripes, including the film");
    expect(c[3]/b[3]>.60 && c[3]/b[3]<.76,"17.5 percent film twice retains about 68 percent of the transmitted light");
    ReadStatus(&status);
    std::printf("jar totals: frames traced %llu, failed %llu\n",static_cast<unsigned long long>(status.framesTraced),static_cast<unsigned long long>(status.framesFailed));
    expect(status.framesFailed==0,"jar fixture traced every submitted frame without a device failure");
    std::printf(failures ? "FAILED %d jar shell checks\n" : "jar shell checks passed\n",failures);
    return failures ? 1 : 0;
}
