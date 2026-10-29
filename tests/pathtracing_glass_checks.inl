// PT-71 item 6. Included inside the real GPU harness after its scene warm-up.
// All measurements average raw frames, without any denoiser or radiance cache.
{
    const auto savedObjects=objects;
    const bool savedOwn=ownRays, savedSun=sunOn;
    const float savedSky=skyIntensity;
    ownRays=true; sunOn=false; skyIntensity=1e-6f;
    SetLights(nullptr,0);
    constexpr uint32_t raw=TGPT_OPTION_NO_DENOISER|TGPT_OPTION_NO_RADIANCE_CACHE|TGPT_OPTION_NO_CAUSTICS|TGPT_OPTION_NO_CLAMPS;
    constexpr uint32_t glassId=210, signId=211, eyeId=212, mirrorId=213;
    auto glass=materials[10]; glass.ior=1.f; glass.smoothness=1.f;
    glass.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_SEE_THROUGH; glass.alphaCutoff=0;
    glass.baseColor[3]=0.f; // clear: none of it is paint (the game's alpha is the share of a texel its color covers, PT-35)
    auto sign=materials[0]; sign.surfaceModel=1; sign.flags=TGPT_MATERIAL_DOUBLE_SIDED;
    sign.baseColor[0]=0.4f; sign.baseColor[1]=0.6f; sign.baseColor[2]=0.8f;
    auto eyeMat=materials[0]; eyeMat.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_EYE|TGPT_MATERIAL_DOUBLE_SIDED;
    eyeMat.alphaCutoff=0; eyeMat.smoothness=0.2f;
    eyeMat.baseColor[0]=0.25f; eyeMat.baseColor[1]=0.45f; eyeMat.baseColor[2]=0.7f;
    auto mirror=materials[0]; mirror.metallic=1; mirror.smoothness=1;
    mirror.baseColor[0]=mirror.baseColor[1]=mirror.baseColor[2]=1;
    SetMaterials(glassId,&glass,1); SetMaterials(signId,&sign,1);
    SetMaterials(eyeId,&eyeMat,1); SetMaterials(mirrorId,&mirror,1);
    std::vector<TGPT_Instance> opticalScene; std::vector<uint32_t> opticalMaterials;
    auto add=[&](int mesh, M4 transform, uint32_t material, int submeshes=1) {
        TGPT_Instance in{}; in.mesh=uint32_t(mesh); in.mask=0xFF; in.flags=TGPT_INSTANCE_ALPHA_TEST;
        for(int r=0;r<3;++r) for(int c=0;c<4;++c) in.transform[r*4+c]=transform.m[r][c];
        in.materialBase=uint32_t(opticalMaterials.size());
        for(int i=0;i<submeshes;++i) opticalMaterials.push_back(material);
        opticalScene.push_back(in);
    };
    const auto backdrop=mul(translate_scale({0,0,3},{12,12,1}),rot_x(-pi/2));
    const Camera front=make_camera({0,0,-4},{0,0,0},45.f,float(W)/float(H));
    auto reset=[&]() {
        opticalScene.clear(); opticalMaterials.clear(); objects.clear();
        add(quadHandle,backdrop,signId);
        objects.push_back({0,backdrop,{0}}); // HDRP's deliberately different colour is only a yardstick.
    };
    auto measure=[&](const Camera& camera, const char* image, int frames=24) {
        expect(SetScene(opticalScene.data(),uint32_t(opticalScene.size()),opticalMaterials.data(),uint32_t(opticalMaterials.size()))==0,"optical scene submitted");
        std::vector<float> mean(size_t(W)*H*3,0.f);
        run(TGPT_VIEW_NONE,camera,camera,0,raw);
        for(int i=0;i<frames+3;++i) {
            run(TGPT_VIEW_NONE,camera,camera,uint32_t(20000+i),raw);
            if(i<3) continue;
            const auto px=readback();
            for(size_t k=0;k<mean.size();++k) mean[k]+=px[k]/float(frames);
        }
        if(image) save(mean,image,true);
        return mean;
    };
    auto patch=[&](const std::vector<float>& px, int dx=0, int half=12) {
        std::array<double,3> value{};
        for(int y=int(H)/2-half;y<int(H)/2+half;++y) for(int x=int(W)/2+dx-half;x<int(W)/2+dx+half;++x)
            for(int c=0;c<3;++c) value[c]+=px[(size_t(y)*W+x)*3+c]/double(4*half*half);
        return value;
    };
    reset(); const auto plainImage=measure(front,"glass-00-unlit.png",8); const auto plain=patch(plainImage);
    std::printf("      optical unlit %.6f %.6f %.6f\n",plain[0],plain[1],plain[2]);
    expect(plain[0]>0.001 && std::abs(plain[1]/plain[0]-1.5)<0.04 && std::abs(plain[2]/plain[0]-2)<0.05,
           "an Unlit forward surface emits its own colour without HDRP shading or a light");

    // Eighty actual interfaces, not a single material pretending to be many panes.
    reset();
    for(int pane=0;pane<40;++pane) add(cubeHandle,translate_scale({0,0,-1.f+0.035f*pane},{1.2f,1.2f,0.012f}),glassId,2);
    const auto many=patch(measure(front,"glass-01-forty-panes.png",48));
    std::printf("      eighty interfaces / unobstructed: %.5f\n",many[1]/plain[1]);
    expect(many[1]>plain[1]*0.85 && many[1]<plain[1]*1.15,"eighty glass interfaces retain the expected light without a pane cap");

    reset(); glass.smoothness=.05f; SetMaterials(glassId,&glass,1);
    M4 slant=identity(); slant.m[0][0]=slant.m[2][2]=std::sqrt(.5f); slant.m[0][2]=std::sqrt(.5f); slant.m[2][0]=-std::sqrt(.5f);
    add(cubeHandle,mul(slant,translate_scale({0,0,0},{3,3,.1f})),glassId,2);
    const auto matched=patch(measure(front,"glass-01b-matched-rough.png",24));
    std::printf("      index-matched rough pane transmission %.5f\n",matched[1]/plain[1]);
    expect(std::abs(matched[1]/plain[1]-1.)<.05,"equal IOR media have no rough boundary that absorbs light");
    glass.smoothness=1.f;

    reset(); glass.absorption[0]=5; glass.absorption[1]=1; glass.absorption[2]=0.2f; SetMaterials(glassId,&glass,1);
    for(int pane=0;pane<2;++pane) add(cubeHandle,translate_scale({0,0,-0.5f+pane},{1.5f,1.5f,0.1f}),glassId,2);
    const auto tinted=patch(measure(front,"glass-02-absorption.png",32));
    std::printf("      20 cm absorption measured %.5f %.5f %.5f; expected %.5f %.5f %.5f\n",tinted[0]/plain[0],tinted[1]/plain[1],tinted[2]/plain[2],std::exp(-1.),std::exp(-.2),std::exp(-.04));
    expect(std::abs(tinted[0]/plain[0]-std::exp(-1.))<0.07 && std::abs(tinted[1]/plain[1]-std::exp(-.2))<0.07 && std::abs(tinted[2]/plain[2]-std::exp(-.04))<0.07,
           "glass tint follows Beer-Lambert distance through both panes");

    reset(); for(float& v:glass.absorption) v=0; glass.ior=1.5f;
    // A coloured diffuse base cannot tint explicitly clear optical coefficients.
    glass.baseColor[0]=.25f; glass.baseColor[1]=.5f; glass.baseColor[2]=.75f; SetMaterials(glassId,&glass,1);
    add(cubeHandle,translate_scale({0,0,0},{1.5f,1.5f,0.1f}),glassId,2);
    const auto refractive=patch(measure(front,"glass-03-fresnel.png",48));
    std::printf("      IOR 1.5 pane transmission %.5f (normal-incidence multiple-reflection sum 0.92308)\n",refractive[1]/plain[1]);
    expect(refractive[1]>plain[1]*0.89 && refractive[1]<plain[1]*0.96,"material IOR controls Fresnel reflection and refraction");

    // A 45-degree, 0.8 m pane displaces the centre ray by 0.2633 m. A narrow
    // green target sits there: straight-through rays see the blue backdrop.
    // Fresnel transmission alone cannot prove that the ray actually bends.
    reset();
    add(cubeHandle,mul(slant,translate_scale({0,0,0},{3,3,.8f})),glassId,2);
    auto target=sign; target.baseColor[0]=target.baseColor[2]=.02f; target.baseColor[1]=1.f;
    constexpr uint32_t targetId=215; SetMaterials(targetId,&target,1);
    const double cosIncident=std::sqrt(.5), eta=1./1.5;
    const double cosTransmitted=std::sqrt(1.-eta*eta*.5);
    const float displacement=float(.8*(cosTransmitted-eta*cosIncident)*cosIncident/cosTransmitted);
    add(quadHandle,mul(translate_scale({displacement,0,2.9f},{.18f,1,1}),rot_x(-pi/2)),targetId);
    const auto bent=patch(measure(front,"glass-03b-snell-displacement.png",48),0,2);
    std::printf("      slanted pane displacement %.5f m: target RGB %.6f %.6f %.6f\n",displacement,bent[0],bent[1],bent[2]);
    expect(bent[1]>.08 && bent[1]>bent[0]*8 && bent[1]>bent[2]*8,
           "Snell refraction reaches the analytically displaced target behind a slanted pane");

    reset(); add(cubeHandle,translate_scale({0,0,0},{1.5f,1.5f,.1f}),glassId,2);
    expect(SetScene(opticalScene.data(),uint32_t(opticalScene.size()),opticalMaterials.data(),uint32_t(opticalMaterials.size()))==0,"guide pane restored");

    // Producing RR inputs does not run RR. The raw frame remains undenoised.
    glass.smoothness=.6f; SetMaterials(glassId,&glass,1);
    rayReconstruction=true;
    for(uint32_t i=0;i<8;++i) run(TGPT_VIEW_NONE,front,front,25000+i,raw);
    rayReconstruction=false;
    const auto getOpticalGuides=reinterpret_cast<decltype(&TGPT_GetGuides)>(GetProcAddress(pt,"TGPT_GetGuides"));
    TGPT_Guides opticalGuides{sizeof(TGPT_Guides),TGPT_ABI};
    const bool haveGuides=getOpticalGuides && getOpticalGuides(&opticalGuides)==0 && opticalGuides.diffuseAlbedo && opticalGuides.normalRoughness && opticalGuides.specularAlbedo;
    double guideRough=0,guideDiffuse=0,guideSpecular=0;
    if(haveGuides) {
        const auto nr=readGuide(opticalGuides.normalRoughness),da=readGuide(opticalGuides.diffuseAlbedo),sa=readGuide(opticalGuides.specularAlbedo);
        for(int y=int(H)/2-6;y<int(H)/2+6;++y) for(int x=int(W)/2-6;x<int(W)/2+6;++x) {
            const size_t k=(size_t(y)*W+x)*4; guideRough+=nr[k+3]/144.; guideDiffuse+=(da[k]+da[k+1]+da[k+2])/432.; guideSpecular+=(sa[k]+sa[k+1]+sa[k+2])/432.;
        }
    }
    std::printf("      glass guide roughness %.5f, diffuse albedo %.5f, specular albedo %.5f\n",guideRough,guideDiffuse,guideSpecular);
    expect(haveGuides && std::abs(guideRough-.4)<.02 && guideDiffuse<.02 && guideSpecular>.95,
           "glass guides contain its own roughness and zero diffuse albedo");
    glass.smoothness=1.f;

    // Closed hollow jar: outer wall, inner wall and top/bottom rims, with smooth
    // vertex normals. Its thickness varies along the ray near the silhouette.
    std::vector<Vertex> jarVertices; std::vector<uint16_t> jarTriangles;
    constexpr int sides=64;
    auto vertex=[&](float radius,float y,float angle,float normalSign,float ny) {
        jarVertices.push_back({radius*std::cos(angle),y,radius*std::sin(angle),normalSign*std::cos(angle),ny,normalSign*std::sin(angle),angle/(2*pi),(y+0.5f)});
    };
    for(int k=0;k<sides;++k) {
        const float a=2*pi*k/sides,b=2*pi*(k+1)/sides;
        for(int face=0;face<4;++face) {
            const uint16_t first=uint16_t(jarVertices.size());
            if(face==0) { vertex(.5f,-.5f,a,1,0); vertex(.5f,.5f,a,1,0); vertex(.5f,-.5f,b,1,0); vertex(.5f,.5f,b,1,0); }
            if(face==1) { vertex(.44f,-.5f,b,-1,0); vertex(.44f,.5f,b,-1,0); vertex(.44f,-.5f,a,-1,0); vertex(.44f,.5f,a,-1,0); }
            if(face==2) { vertex(.44f,.5f,a,0,1); vertex(.44f,.5f,b,0,1); vertex(.5f,.5f,a,0,1); vertex(.5f,.5f,b,0,1); }
            if(face==3) { vertex(.44f,-.5f,b,0,-1); vertex(.44f,-.5f,a,0,-1); vertex(.5f,-.5f,b,0,-1); vertex(.5f,-.5f,a,0,-1); }
            jarTriangles.insert(jarTriangles.end(),{first,uint16_t(first+1),uint16_t(first+2),uint16_t(first+2),uint16_t(first+1),uint16_t(first+3)});
        }
    }
    MeshBuffers jarMesh=make_mesh(jarVertices,{jarTriangles}); const int jarHandle=register_mesh(jarMesh,7216);
    expect(jarHandle>0,"closed hollow optical jar registered");
    glass.ior=1; glass.absorption[0]=5; glass.absorption[1]=1; glass.absorption[2]=.2f; SetMaterials(glassId,&glass,1);
    auto pane=glass; for(float& v:pane.absorption) v=0; constexpr uint32_t paneId=214; SetMaterials(paneId,&pane,1);
    reset(); add(jarHandle,identity(),glassId);
    add(cubeHandle,translate_scale({0,0,-1},{1.5f,1.5f,.03f}),paneId,2);
    add(cubeHandle,translate_scale({0,0,-1.5f},{1.5f,1.5f,.03f}),paneId,2);
    // The jar's mesh is converted and built over the first frames (on the scene's worker thread): measured from the fourth
    // frame on, two of the 48 frames had no jar yet on a busy machine (its red 0.556 where 46 frames with it give 0.536).
    measure(front,nullptr,12);
    const auto jarImage=measure(front,"glass-04-jar-behind-two-panes.png",48);
    const auto jarMiddle=patch(jarImage),jarRim=patch(jarImage,int(std::lround(99.0*H/720)),std::max(1,int(H/180)));
    std::printf("      jar middle/rim red transmission %.5f %.5f, green %.5f %.5f\n",jarMiddle[0]/plain[0],jarRim[0]/plain[0],jarMiddle[1]/plain[1],jarRim[1]/plain[1]);
    expect(jarMiddle[1]>plain[1]*.78 && jarMiddle[1]<plain[1]*.98 && jarMiddle[0]<jarMiddle[1]*plain[0]/plain[1]*.75,
           "a jar behind two panes remains visible with its own tint");
    expect(jarRim[0]<jarMiddle[0]*.88,"a glass rim darkens by the longer traced distance through its wall");

    // The same iris and curved clear shell, viewed from a virtual camera or through
    // a perfect mirror. Its normal, material and lighting must be the same.
    glass.ior=1.376f; for(float& v:glass.absorption) v=0;
    glass.baseColor[0]=glass.baseColor[1]=glass.baseColor[2]=1; SetMaterials(glassId,&glass,1);
    TGPT_Light eyeLight=light; eyeLight.position[0]=-1; eyeLight.position[1]=1; eyeLight.position[2]=-1;
    eyeLight.color[0]=eyeLight.color[1]=eyeLight.color[2]=8; SetLights(&eyeLight,1);
    const M4 iris=mul(translate_scale({-2.f/3.f,0,0},{.5f,.5f,1}),rot_x(-pi/2));
    opticalScene.clear();opticalMaterials.clear();objects.clear();
    add(quadHandle,iris,eyeId); add(jarHandle,translate_scale({-2.f/3.f,0,0},{.7f,.7f,.7f}),glassId);
    const Camera virtualEye=make_camera({1,0,-5},{0,0,-2},45.f,float(W)/H);
    objects.push_back({0,iris,{0}});
    const auto directEye=patch(measure(virtualEye,"glass-05-eye-direct.png",64),0,6);
    const M4 mirrorAt=mul(translate_scale({0,0,-2},{4,4,1}),rot_x(pi/2));
    add(quadHandle,mirrorAt,mirrorId); objects.push_back({0,mirrorAt,{8}});
    const Camera reflectedEye=make_camera({1,0,1},{0,0,-2},45.f,float(W)/H);
    const auto mirrorEye=patch(measure(reflectedEye,"glass-06-eye-mirror.png",64),0,6);
    std::printf("      eye direct %.6f %.6f %.6f; mirror %.6f %.6f %.6f\n",directEye[0],directEye[1],directEye[2],mirrorEye[0],mirrorEye[1],mirrorEye[2]);
    expect(directEye[1]>0.001 && directEye[2]>directEye[1]*1.25 && directEye[0]<directEye[1]*.8 && mirrorEye[1]>directEye[1]*.8 && mirrorEye[1]<directEye[1]*1.2 && std::abs(mirrorEye[2]/mirrorEye[1]-directEye[2]/directEye[1])<.15,
           "an eye beneath a refracting cornea has the same colour and light in a mirror");

    // ---- 2026-10-08 (run 20261007-224654: bottles as black shapes, jars without labels, dark eyes, dark lines in hair).
    // Paint on see-through glass (PT-35): the game's alpha is the share of a texel that its color covers, a lit surface
    // there (a jar's label, its lid, a stain); the rest is the glass. A sheet half covered by green paint in front of the
    // glowing backdrop is half the paint as a lamp lights it and half the backdrop.
    {
        TGPT_Light paintLight=light; paintLight.position[0]=-1; paintLight.position[1]=1; paintLight.position[2]=-2.5f;
        paintLight.color[0]=paintLight.color[1]=paintLight.color[2]=8; SetLights(&paintLight,1);
        auto paint=glass; paint.ior=1.f; for(float& v:paint.absorption) v=0; paint.smoothness=.2f; paint.albedoTile=-1;
        paint.baseColor[0]=.1f; paint.baseColor[1]=.8f; paint.baseColor[2]=.1f;
        constexpr uint32_t paintId=216;
        const M4 sheet=mul(translate_scale({0,0,0},{1.5f,1.5f,1}),rot_x(-pi/2));
        auto painted=[&](float alpha,const char* image) {
            paint.baseColor[3]=alpha; SetMaterials(paintId,&paint,1);
            reset(); add(quadHandle,sheet,paintId);
            return patch(measure(front,image,48));
        };
        const auto whole=painted(1.f,"glass-07-paint-whole.png"),half=painted(.5f,"glass-07b-paint-half.png"),none=painted(0.f,nullptr);
        std::printf("      paint on glass: whole %.5f %.5f %.5f, half %.5f %.5f %.5f, none %.5f %.5f %.5f (the backdrop %.5f %.5f %.5f)\n",
                    whole[0],whole[1],whole[2],half[0],half[1],half[2],none[0],none[1],none[2],plain[0],plain[1],plain[2]);
        bool mixes=whole[1]>.02 && whole[1]>whole[0]*3 && whole[1]>whole[2]*3;
        for(int c=0;c<3;++c) {
            const double expected=.5*whole[c]+.5*plain[c];
            mixes=mixes && std::abs(half[c]-expected)<.1*expected+.004 && std::abs(none[c]-plain[c])<.05*plain[c]+.002;
        }
        expect(mixes,"paint on see-through glass covers its share of it: a lit surface there, the glass beside it");
        SetLights(nullptr,0);
    }
    // Glass has a volume only where its mesh closes one. An open sheet (a pane of one quad, a shard, a cornea's shell) is a
    // thin pane: it reflects as glass's two faces do and lets the rest straight through; what is behind it is neither bent
    // nor dimmed by entering a solid it never leaves.
    {
        auto thin=glass; thin.ior=1.5f; for(float& v:thin.absorption) v=0; thin.smoothness=1.f; thin.baseColor[3]=0.f; thin.albedoTile=-1;
        constexpr uint32_t thinId=217; SetMaterials(thinId,&thin,1);
        reset(); add(quadHandle,mul(translate_scale({0,0,0},{1.5f,1.5f,1}),rot_x(-pi/2)),thinId);
        const auto sheet=patch(measure(front,"glass-08-open-sheet.png",48));
        std::printf("      an open sheet of glass, index 1.5: %.5f of the backdrop behind it (a pane's two faces let 0.92308 through)\n",sheet[1]/plain[1]);
        expect(sheet[1]>plain[1]*.89 && sheet[1]<plain[1]*.96,"an open sheet of glass is a thin pane: what is behind it stays as bright as behind a pane");
        // An eye behind such a shell (a cornea's, index 1.376) keeps its brightness (0.951 of it comes through the shell).
        thin.ior=1.376f; SetMaterials(thinId,&thin,1);
        TGPT_Light shellLight=light; shellLight.position[0]=-1; shellLight.position[1]=1; shellLight.position[2]=-1;
        shellLight.color[0]=shellLight.color[1]=shellLight.color[2]=8; SetLights(&shellLight,1);
        const M4 irisAt=mul(translate_scale({0,0,0},{.5f,.5f,1}),rot_x(-pi/2));
        opticalScene.clear(); opticalMaterials.clear(); objects.clear();
        add(quadHandle,irisAt,eyeId); objects.push_back({0,irisAt,{0}});
        const auto bare=patch(measure(front,"glass-09-eye-bare.png",48),0,6);
        add(quadHandle,mul(translate_scale({0,0,-.03f},{.7f,.7f,1}),rot_x(-pi/2)),thinId);
        const auto shelled=patch(measure(front,"glass-09b-eye-under-a-shell.png",48),0,6);
        std::printf("      an eye bare %.6f %.6f %.6f, under an open shell %.6f %.6f %.6f (%.3f of it)\n",bare[0],bare[1],bare[2],shelled[0],shelled[1],shelled[2],shelled[1]/std::max(bare[1],1e-9));
        expect(bare[1]>.001 && shelled[1]>bare[1]*.86 && shelled[1]<bare[1]*1.04,"an eye under a cornea's open shell is as bright as the shell lets it be seen");
        SetLights(nullptr,0);
    }
    // Hair's card lets light through to what is under it: lit from above, its underside sends on the share the camera's own
    // pixel of such a card shows when lit from behind (CLOTH_TRANSMISSION of its color), so a floor under a card of hair is
    // not black, as it is under an opaque card of the same color.
    {
        auto under=materials[0]; under.flags=0; under.baseColor[0]=under.baseColor[1]=under.baseColor[2]=.8f; under.baseColor[3]=1; under.albedoTile=-1;
        auto card=under; card.flags=TGPT_MATERIAL_DOUBLE_SIDED;
        auto strands=under; strands.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_STRANDS|TGPT_MATERIAL_DOUBLE_SIDED; strands.alphaCutoff=0;
        constexpr uint32_t underId=218, cardId=219, strandsId=220;
        SetMaterials(underId,&under,1); SetMaterials(cardId,&card,1); SetMaterials(strandsId,&strands,1);
        TGPT_Light above=light; above.position[0]=0; above.position[1]=2; above.position[2]=0;
        above.color[0]=above.color[1]=above.color[2]=8; SetLights(&above,1);
        const M4 floorAt=translate_scale({0,-1,0},{1,1,1}), cardAt=translate_scale({0,0,0},{1,1,1});
        const Camera low=make_camera({0,-.4f,-3},{0,-1,0},12.f,float(W)/float(H));
        auto floorUnder=[&](uint32_t cover,const char* image) {
            opticalScene.clear(); opticalMaterials.clear(); objects.clear();
            add(quadHandle,floorAt,underId); objects.push_back({0,floorAt,{0}});
            if(cover) add(quadHandle,cardAt,cover);
            return patch(measure(low,image,64),0,4);
        };
        const auto open=floorUnder(0,nullptr),shaded=floorUnder(cardId,"hair-10-floor-under-a-card.png"),haired=floorUnder(strandsId,"hair-10b-floor-under-hair.png");
        std::printf("      a floor under a lamp %.6f; under an opaque card %.6f; under a card of hair %.6f (%.3f of the open floor)\n",
                    open[1],shaded[1],haired[1],haired[1]/std::max(open[1],1e-9));
        expect(open[1]>.01 && shaded[1]<open[1]*.03 && haired[1]>open[1]*.08 && haired[1]<open[1]*.5,
               "a lit card of hair sends light on to what is under it, as much as its own lit-through share");
        SetLights(nullptr,0);
    }

    // ---- 2026-10-09 (PT-13, PT-35; docs/agents/glass-3.md). ON TRIAL behind TGPT_Frame.cameraPosition's w (the plugin's
    // hidden "Glass trial"; 0: glass as before). The Crypt's jars come out cloudy with dark blotches where the game's are
    // clear with the wall showing through. The game's own material (Mat_Prop_Jars_01): see-through, drawn from its front
    // faces alone, smoothness 0.60 to 0.64 on its glass and alpha 0.175 there (a film), no refraction model: HDRP shows what
    // is behind it sharp, and the roughness shapes only its highlight. A jar of its proportions (27 cm across, walls 5.4 mm,
    // a closed shell: four faces along a ray) in front of glowing bars 4 cm apart, 27 cm behind it; no lamp, so the film is
    // black and what the bars lose of their light is the film's cover.
    {
        std::vector<Vertex> shellVertices; std::vector<uint16_t> shellTriangles;
        constexpr int around=96; constexpr float outer=.135f, inner=.1296f, tall=.2f;
        auto corner=[&](float radius,float y,float angle,float normalSign,float ny) {
            shellVertices.push_back({radius*std::cos(angle),y,radius*std::sin(angle),normalSign*std::cos(angle),ny,normalSign*std::sin(angle),angle/(2*pi),y+.5f});
        };
        for(int k=0;k<around;++k) {
            const float a=2*pi*k/around,b=k+1==around ? 0.f : 2*pi*(k+1)/around; // the seam's two columns at the same place
            for(int face=0;face<4;++face) {
                const uint16_t first=uint16_t(shellVertices.size());
                if(face==0) { corner(outer,-tall,a,1,0); corner(outer,tall,a,1,0); corner(outer,-tall,b,1,0); corner(outer,tall,b,1,0); }
                if(face==1) { corner(inner,-tall,b,-1,0); corner(inner,tall,b,-1,0); corner(inner,-tall,a,-1,0); corner(inner,tall,a,-1,0); }
                if(face==2) { corner(inner,tall,a,0,1); corner(inner,tall,b,0,1); corner(outer,tall,a,0,1); corner(outer,tall,b,0,1); }
                if(face==3) { corner(inner,-tall,b,0,-1); corner(inner,-tall,a,0,-1); corner(outer,-tall,b,0,-1); corner(outer,-tall,a,0,-1); }
                shellTriangles.insert(shellTriangles.end(),{first,uint16_t(first+1),uint16_t(first+2),uint16_t(first+2),uint16_t(first+1),uint16_t(first+3)});
            }
        }
        MeshBuffers shellMesh=make_mesh(shellVertices,{shellTriangles}); const int shellHandle=register_mesh(shellMesh,7217);
        expect(shellHandle>0,"the Crypt's jar registered");
        auto film=glass; film.ior=1.5f; for(float& v:film.absorption) v=0; film.smoothness=.62f; film.albedoTile=-1;
        film.flags=TGPT_MATERIAL_ALPHA_TEST|TGPT_MATERIAL_GLASS|TGPT_MATERIAL_SEE_THROUGH; // single-sided, as the game's
        film.baseColor[0]=.2f; film.baseColor[1]=.25f; film.baseColor[2]=.17f; film.baseColor[3]=.175f;
        auto bar=sign; bar.baseColor[0]=bar.baseColor[1]=bar.baseColor[2]=1.f;
        constexpr uint32_t filmId=221, barId=222;
        SetMaterials(filmId,&film,1); SetMaterials(barId,&bar,1);
        SetLights(nullptr,0);
        const Camera jarCamera=make_camera({0,0,-1.2f},{0,0,0},20.f,float(W)/float(H));
        auto bars=[&](bool withJar) {
            reset();
            for(int i=-12;i<=12;++i) add(quadHandle,mul(translate_scale({.04f*i,0,.4f},{.02f,1,1}),rot_x(-pi/2)),barId);
            if(withJar) add(shellHandle,identity(),filmId);
        };
        // The bars' green over the jar's middle two fifths (clear of its bent outline), column by column: the mean, and how
        // far the columns differ (the bars' contrast: 0 when the view is blurred away).
        auto columns=[&](const std::vector<float>& px) {
            const int halfWidth=int(std::lround(22.0*H/180)), halfHeight=int(std::lround(30.0*H/180));
            std::vector<double> column(size_t(2*halfWidth),0.);
            for(int x=0;x<2*halfWidth;++x) for(int y=int(H)/2-halfHeight;y<int(H)/2+halfHeight;++y)
                column[size_t(x)]+=px[(size_t(y)*W+size_t(int(W)/2-halfWidth+x))*3+1]/double(2*halfHeight);
            double mean=0; for(double v:column) mean+=v/double(column.size());
            double spread=0; for(double v:column) spread+=(v-mean)*(v-mean)/double(column.size());
            return std::array<double,2>{mean,std::sqrt(spread)};
        };
        bars(false); const auto bare=columns(measure(jarCamera,"glass-11-bars.png",16));
        bars(true); measure(jarCamera,nullptr,12); // (the mesh is built over the first frames)
        auto through=[&](float trial,const char* image) {
            glassTrial=trial; const auto seen=columns(measure(jarCamera,image,64)); glassTrial=0.f; return seen;
        };
        const auto asNow=through(0.f,"glass-11b-jar-as-now.png"),clear=through(1.f,"glass-11c-jar-clear.png"),
                   drawn=through(2.f,"glass-11d-jar-drawn.png"),both=through(3.f,"glass-11e-jar-clear-drawn.png");
        std::printf("      the Crypt's jar before bars (mean %.5f, contrast %.5f): as now light %.3f contrast %.3f; clear %.3f %.3f; drawn faces %.3f %.3f; both %.3f %.3f (of the bare bars')\n",
                    bare[0],bare[1],asNow[0]/bare[0],asNow[1]/bare[1],clear[0]/bare[0],clear[1]/bare[1],drawn[0]/bare[0],drawn[1]/bare[1],both[0]/bare[0],both[1]/bare[1]);
        expect(bare[1]>bare[0]*.1 && clear[1]>bare[1]*.3 && clear[1]>asNow[1]*2,
               "on trial: what is seen through a see-through jar stays sharp, whatever roughness its highlight has");
        expect(both[0]>clear[0]*1.25 && both[0]<clear[0]*1.75,
               "on trial: a jar's film is on the faces the game draws, once a wall");
        // The game's jar itself: the same body with a lid in the same submesh, a disc whose rim is open (the Crypt's
        // Mesh_Prop_Jars_01: a closed body of 764 triangles and an open lid of 325). Its submesh does not close a volume, so
        // until the trial's bit 4 the whole jar is open sheets: each of a wall's two faces a pane of its own (four panes'
        // reflections and four layers of film along a ray), nothing bent. With bits 1, 2 and 4 its body is the solid shell
        // it is: more of the bars comes through, as sharp, and the outline bends them.
        {
            auto liddedVertices=shellVertices; auto liddedTriangles=shellTriangles;
            const uint16_t hub=uint16_t(liddedVertices.size());
            liddedVertices.push_back({0,tall+.01f,0,0,1,0,.5f,.5f});
            for(int k=0;k<around;++k) { const float a=2*pi*k/around; liddedVertices.push_back({.11f*std::cos(a),tall+.01f,.11f*std::sin(a),0,1,0,.5f,.5f}); }
            for(int k=0;k<around;++k) liddedTriangles.insert(liddedTriangles.end(),{hub,uint16_t(hub+1+(k+1)%around),uint16_t(hub+1+k)});
            MeshBuffers liddedMesh=make_mesh(liddedVertices,{liddedTriangles}); const int liddedHandle=register_mesh(liddedMesh,7218);
            expect(liddedHandle>0,"the Crypt's jar with its open lid registered");
            reset();
            for(int i=-12;i<=12;++i) add(quadHandle,mul(translate_scale({.04f*i,0,.4f},{.02f,1,1}),rot_x(-pi/2)),barId);
            add(liddedHandle,identity(),filmId);
            measure(jarCamera,nullptr,12);
            const auto sheets=through(0.f,"glass-11f-lidded-jar-as-now.png"),filmOnce=through(2.f,"glass-11g-lidded-jar-film-once.png"),
                       whole=through(7.f,"glass-11h-lidded-jar-all.png");
            std::printf("      the same jar with an open lid in its submesh: as now (open sheets) light %.3f contrast %.3f; its film once a wall %.3f %.3f; and its body a solid, seen through clear %.3f %.3f (of the bare bars')\n",
                        sheets[0]/bare[0],sheets[1]/bare[1],filmOnce[0]/bare[0],filmOnce[1]/bare[1],whole[0]/bare[0],whole[1]/bare[1]);
            expect(filmOnce[0]>sheets[0]*1.2 && whole[0]>sheets[0]*1.35 && whole[1]>sheets[1]*1.2,
                   "on trial: a jar whose open lid shares its submesh is glass with an inside all the same, its film once a wall");
        }
    }

    // ---- 2026-10-09 (PT-76; docs/agents/glass-3.md): a lamp's light through glass onto a floor, with every bounce.
    // (1) Once: the floor's own shadow ray brings the lamp's light through the glass; the floor's bounce ray, which meets the
    // glass and walks through it, must not bring the lamp again where it only passed through (until 2026-10-09 it did: a floor
    // under clear glass was nearly twice as bright as an open one, and no tint could show). To mutate this check, replace
    // dielectric_follow's `const bool gathers = g_walkGathers` with false and rebuild. The lamp is a ball 0.4 m in radius, large enough for a floor's
    // bounce rays to find it often. (2) Tinted by the glass's own absorption: an open sheet as a thin pane of its
    // material's thickness, a closed slab by the way inside it (10 cm of it, straight up, either way).
    {
        auto ground=materials[0]; ground.flags=0; ground.baseColor[0]=ground.baseColor[1]=ground.baseColor[2]=.8f; ground.baseColor[3]=1; ground.albedoTile=-1;
        auto clearPane=glass; clearPane.ior=1.5f; for(float& v:clearPane.absorption) v=0; clearPane.smoothness=1.f; clearPane.baseColor[3]=0.f; clearPane.albedoTile=-1;
        auto tintedSlab=clearPane; tintedSlab.absorption[0]=5; tintedSlab.absorption[1]=1; tintedSlab.absorption[2]=.2f;
        auto tintedPane=tintedSlab; { const float thick=.1f; std::memcpy(&tintedPane.opticalReserved[1], &thick, 4); }
        constexpr uint32_t groundId=223, clearId=224, paneId2=225, slabId=226;
        SetMaterials(groundId,&ground,1); SetMaterials(clearId,&clearPane,1); SetMaterials(paneId2,&tintedPane,1); SetMaterials(slabId,&tintedSlab,1);
        TGPT_Light above=light; above.position[0]=0; above.position[1]=2; above.position[2]=0; above.radius=.4f;
        above.color[0]=above.color[1]=above.color[2]=8; SetLights(&above,1);
        const M4 floorAt=translate_scale({0,-1,0},{1,1,1});
        const Camera low=make_camera({0,-.4f,-3},{0,-1,0},12.f,float(W)/float(H));
        auto floorUnder=[&](int mesh,M4 at,uint32_t cover,int submeshes,float trial,const char* image) {
            opticalScene.clear(); opticalMaterials.clear(); objects.clear();
            add(quadHandle,floorAt,groundId); objects.push_back({0,floorAt,{0}});
            if(cover) add(mesh,at,cover,submeshes);
            glassTrial=trial; const auto seen=patch(measure(low,image,64),0,int(std::lround(8.0*H/180))); glassTrial=0.f; return seen;
        };
        const M4 sheetAt=translate_scale({0,0,0},{1,1,1}), slabAt=translate_scale({0,0,0},{1,.1f,1});
        const auto open=floorUnder(0,sheetAt,0,1,0.f,nullptr);
        const auto once=floorUnder(quadHandle,sheetAt,clearId,1,0.f,"glass-12-floor-under-clear-glass.png");
        const auto paned=floorUnder(quadHandle,sheetAt,paneId2,1,0.f,"glass-12b-floor-under-a-tinted-pane.png");
        const auto slabbed=floorUnder(cubeHandle,slabAt,slabId,2,0.f,"glass-12c-floor-under-a-tinted-slab.png");
        const double leaves[3]={std::exp(-.5),std::exp(-.1),std::exp(-.02)};
        std::printf("      a floor under a lamp %.5f %.5f %.5f; under clear glass %.3f of it; under a tinted pane %.3f %.3f %.3f, under a tinted slab %.3f %.3f %.3f (their absorption leaves %.3f %.3f %.3f)\n",
                    open[0],open[1],open[2],once[1]/open[1],paned[0]/open[0],paned[1]/open[1],paned[2]/open[2],
                    slabbed[0]/open[0],slabbed[1]/open[1],slabbed[2]/open[2],leaves[0],leaves[1],leaves[2]);
        expect(open[1]>.01 && once[1]>open[1]*.9 && once[1]<open[1]*1.12,
               "a lamp's light through clear glass reaches a floor once: by the floor's own shadow ray, not again by its bounce ray");
        bool paneTints=true, slabTints=true;
        for(int c=0;c<3;++c) {
            paneTints=paneTints && std::abs(paned[c]/open[c]-leaves[c])<.07;
            slabTints=slabTints && std::abs(slabbed[c]/open[c]-leaves[c])<.07;
        }
        expect(paneTints,"an open sheet of glass tints a lamp's light by its absorption over its own thickness (a thin pane)");
        expect(slabTints,"closed glass tints a lamp's light by its absorption over the way inside it");
        SetLights(nullptr,0);
    }

    objects=savedObjects; ownRays=savedOwn; sunOn=savedSun; skyIntensity=savedSky;
    SetMaterials(0,materials.data(),uint32_t(materials.size())); SetLights(&light,1);
    SetScene(instances.data(),uint32_t(instances.size()),indices.data(),uint32_t(indices.size()));
    // (TGPT_TEST_GLASS_ONLY ends after the room's coloured-glass checks, which the test includes next.)
}
