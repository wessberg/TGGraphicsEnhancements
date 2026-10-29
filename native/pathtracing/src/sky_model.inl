// SPDX-License-Identifier: MIT
// PT-112, the sky, ON TRIAL (docs/agents/sky-1.md, sky-2.md): the tracer's own sky on its own device, behind the plugin's
// hidden "Sky trial" (tgpt_api.h TGPT_Sky). Included by tracer.cpp, where its State, FrameConstants and dispatch live.
// fill_sky puts the frame's sky into the constants (and makes its textures when a frame first asks for them); record_sky
// records its passes (pt_sky.hlsl) right after the scene's, before any pass reads the sky: the atmosphere's tables when its
// numbers changed, the clouds' bake when HDRP would bake them, the clouds' ambient light and the sky map with its mips every
// frame. The map then stands in the place of HDRP's resampled one (FrameConstants.inputs1's w) for every ray that leaves the
// scene, and of HDRP's sky at its fog's mips (fogMip's w) for the fog along the rays; the composite draws the camera's misses.

// A texture with every mip, each written by its own view (the map: pt_sky.hlsl modes 5 and 6).
State::MipTexture make_mip_texture(ID3D12Device* device, DescriptorHeap& heap, uint32_t w, uint32_t h, uint32_t mips, DXGI_FORMAT format, const char* what) {
    State::MipTexture t; t.mips=mips; t.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=w; d.Height=h; d.DepthOrArraySize=1; d.MipLevels=UINT16(mips);
    d.Format=format; d.SampleDesc.Count=1; d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto props=heap_props(D3D12_HEAP_TYPE_DEFAULT);
    check(device->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &d, t.state, nullptr, IID_PPV_ARGS(&t.resource)), what);
    t.srv=heap.allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{}; sv.Format=format; sv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; sv.Texture2D.MipLevels=mips;
    device->CreateShaderResourceView(t.resource.Get(), &sv, heap.cpu(t.srv));
    for(uint32_t m=0;m<mips && m<t.uav.size();++m) {
        t.uav[m]=heap.allocate();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv{}; uv.Format=format; uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D; uv.Texture2D.MipSlice=m;
        device->CreateUnorderedAccessView(t.resource.Get(), nullptr, &uv, heap.cpu(t.uav[m]));
    }
    return t;
}

uint64_t sky_hash(uint64_t h, const void* data, size_t bytes) {
    const auto* b=static_cast<const uint8_t*>(data);
    for(size_t i=0;i<bytes;++i) { h^=b[i]; h*=1099511628211ull; }
    return h;
}

// The textures the frame's sky needs, made when it first asks; and whether its tables and clouds must be made again.
void ensure_sky_textures(const TGPT_Sky& s) {
    auto* d=st.d12.Get(); auto& heap=st.heap;
    const DXGI_FORMAT half4=DXGI_FORMAT_R16G16B16A16_FLOAT;
    if(!st.skyTransmittance) { st.skyTransmittance=make_texture(d, heap, kSkyTransmittanceW, kSkyTransmittanceH, half4, "Create the sky's transmittance"); st.skyTablesKey=0; }
    if(!st.skyMultiple) { st.skyMultiple=make_texture(d, heap, kSkyMultiple, kSkyMultiple, half4, "Create the sky's multiple scattering"); st.skyTablesKey=0; }
    if(!st.skyGround) { st.skyGround=make_texture(d, heap, kSkyGround, 1, half4, "Create the sky's ground irradiance"); st.skyTablesKey=0; }
    if(!st.skyAmbient) st.skyAmbient=make_texture(d, heap, 1+kSkyAmbientGroups, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, "Create the clouds' ambient light");
    if(!st.skyMap.resource) {
        st.skyMap=make_mip_texture(d, heap, kSkyMapW, kSkyMapH, kSkyMapMips, half4, "Create the sky map");
        log("Sky (on trial): the tracer's own sky map, "+std::to_string(kSkyMapW)+" x "+std::to_string(kSkyMapH)+" with "+
            std::to_string(kSkyMapMips)+" mips, "+std::to_string(allocation_bytes(st.skyMap.resource.Get())>>10)+" KB; its tables "+
            std::to_string((allocation_bytes(st.skyTransmittance.resource.Get())+allocation_bytes(st.skyMultiple.resource.Get())+
                            allocation_bytes(st.skyGround.resource.Get()))>>10)+" KB");
    }
    // The clouds' bake at HDRP's resolution (the height half of it for the upper hemisphere), its layers one under the other.
    const bool clouds=(s.flags&TGPT_SKY_CLOUDS) && s.cloudWidth>=8 && s.cloudHeight>=8;
    const uint32_t layers=s.layers[1].on ? 2u : 1u, cw=s.cloudWidth, ch=s.cloudHeight*layers;
    if(clouds && (st.skyClouds.width!=cw || st.skyClouds.height!=ch)) {
        if(st.skyClouds.resource) { st.release.add(st.consumer.value, st.skyClouds.resource.Get()); heap.release(st.skyClouds.srv); heap.release(st.skyClouds.uav); }
        st.skyClouds=make_texture(d, heap, cw, ch, DXGI_FORMAT_R16G16_FLOAT, "Create the baked clouds");
        st.skyCloudsKey=0;
        log("Sky (on trial): the clouds baked at "+std::to_string(cw)+" x "+std::to_string(ch)+" ("+std::to_string(layers)+" layers, HDRP's resolution), "+
            std::to_string(allocation_bytes(st.skyClouds.resource.Get())>>10)+" KB");
    }
    // The tables hang on the planet and the atmosphere's media alone (not on the bodies, the camera or HDRP's controls).
    uint64_t tables=1469598103934665603ull;
    tables=sky_hash(tables, &s.planetRadius, sizeof(float));
    tables=sky_hash(tables, s.airExtinction, offsetof(TGPT_Sky, horizonTint)-offsetof(TGPT_Sky, airExtinction));
    tables=sky_hash(tables, &s.ozoneStart, sizeof(float)); tables=sky_hash(tables, &s.ozoneWidth, sizeof(float));
    if(tables!=st.skyTablesKey) { st.skyTablesKey=tables; st.skyTablesPending=true; }
    if(clouds) {
        // HDRP's baking hash (its sun's turn by whole degrees, the maps, the opacities, the lighting), and what the bake reads
        // here besides: the layers' numbers, the maps' handles (a map opened later), the planet, the size.
        // (Not the wind: the bake is of the map where it stands, the wind moves it as it is read.)
        uint64_t key=sky_hash(1469598103934665603ull, &s.cloudsHash, sizeof(uint32_t));
        for(const TGPT_SkyLayer& l:s.layers) {
            key=sky_hash(key, l.opacities, sizeof(l.opacities)); key=sky_hash(key, &l.rotation, 4*sizeof(float));
            key=sky_hash(key, &l.map, sizeof(int32_t)); key=sky_hash(key, &l.on, sizeof(uint32_t));
        }
        key=sky_hash(key, &s.planetRadius, sizeof(float)); key=sky_hash(key, &cw, 4); key=sky_hash(key, &ch, 4);
        if(key!=st.skyCloudsKey) { st.skyCloudsKey=key; st.skyCloudsPending=true; }
    }
}

void say_sky_mode(uint32_t mode, uint32_t flags) {
    const uint32_t said=mode|(flags&TGPT_SKY_HDRP_PIXELS)<<8;
    if(said==st.skyModeSaid) return;
    const bool first=st.skyModeSaid==UINT32_MAX;
    st.skyModeSaid=said;
    if(mode==TGPT_SKY_HDRP) { if(!first) log("Sky: HDRP's, as before (its cube map resampled for the rays)"); return; }
    if(mode==TGPT_SKY_MEASURE) { log("Sky (on trial, measuring): HDRP's sky passes left out by the plugin; the camera's misses drawn from the map the rays read (HDRP's last)"); return; }
    log((flags&TGPT_SKY_HDRP_PIXELS) ? "Sky (on trial): the tracer's own sky for the rays; HDRP draws what the camera sees of it (the game's star layer at night)"
                                     : "Sky (on trial): the tracer's own sky, for the rays and for every pixel where the camera sees it");
}

// The frame's sky into the constants (pt_common.hlsli FrameConstants.sky*; pt_sky.hlsli reads them).
void fill_sky(FrameConstants& k, const TGPT_Frame& f) {
    std::fill(std::begin(k.skyMaps), std::end(k.skyMaps), kInvalid);
    k.skyMaps2[0]=k.skyMaps2[1]=kInvalid; k.skyMaps2[2]=0; k.skyMaps2[3]=0;
    st.skyOwn=false;
    // The game's textures (the cloud maps, the flowmaps, the bodies' surfaces) by their handles to their numbers in the heap
    // this frame, as every shared texture is read (share::resolve: -1 while one is not open; sky-3: the raw handles were
    // read as numbers, and no cloud showed). The clouds' bake hangs on these numbers: a map opened later bakes again.
    TGPT_Sky s=st.skyModel;
    // (A layer that is off, or a body past the count, holds the plugin's zeros: no texture.)
    for(TGPT_SkyLayer& l:s.layers) { if(l.on) { share::resolve(&l.map, 1); share::resolve(&l.flowmap, 1); } else l.map=l.flowmap=-1; }
    for(uint32_t i=0;i<TGPT_SKY_MAX_BODIES;++i) { if(i<s.bodyCount) share::resolve(&s.bodies[i].surfaceTexture, 1); else s.bodies[i].surfaceTexture=-1; }
    const uint32_t mode=s.size==sizeof(TGPT_Sky) ? s.mode : TGPT_SKY_HDRP;
    say_sky_mode(mode, s.flags);
    if(mode==TGPT_SKY_MEASURE) { k.skyMaps2[2]=TGPT_SKY_MEASURE; return; }
    if(mode!=TGPT_SKY_OWN) return;
    // (Without its own sky the plugin has still left HDRP's out: the misses are drawn from HDRP's map, as the measuring switch
    // draws them, unless HDRP draws them itself this frame.)
    const uint32_t fallback=(s.flags&TGPT_SKY_HDRP_PIXELS) ? TGPT_SKY_HDRP : TGPT_SKY_MEASURE;
    if(!st.skyPass || st.skyFailed) { k.skyMaps2[2]=fallback; return; }
    try { ensure_sky_textures(s); }
    catch(const DxError& e) {
        st.skyFailed=true; k.skyMaps2[2]=fallback;
        log(std::string("Sky (on trial): the tracer's own sky could not be made (")+e.what()+"); the rays read HDRP's sky, the misses are drawn from it");
        return;
    }
    st.skyOwn=true;
    auto row=[](float (&r)[4], const float* xyz, float w) { r[0]=xyz[0]; r[1]=xyz[1]; r[2]=xyz[2]; r[3]=w; };
    auto bits=[](uint32_t v) { float x; std::memcpy(&x, &v, 4); return x; };
    // The camera in the planet's space, in double: its way up from the planet's centre and its height (at least 1 m, as HDRP
    // keeps it above the ground).
    const double R=s.planetRadius;
    double o[3]; for(int i=0;i<3;++i) o[i]=double(f.cameraPosition[i])-double(s.planetCenter[i]);
    const double len=std::sqrt(o[0]*o[0]+o[1]*o[1]+o[2]*o[2]);
    const float up[3]={float(o[0]/len), float(o[1]/len), float(o[2]/len)};
    const float height=float(std::max(len-R, 1.0));
    row(k.sky[0], up, float(R));
    row(k.sky[1], s.airExtinction, 1.f/s.airScaleHeight);
    row(k.sky[2], s.airScattering, s.atmosphereDepth);
    row(k.sky[3], s.aerosolScattering, s.aerosolExtinction);
    row(k.sky[4], s.ozoneExtinction, 1.f/s.aerosolScaleHeight);
    const float g=std::clamp(s.aerosolAnisotropy, -0.999f, 0.999f);
    k.sky[5][0]=s.ozoneStart+0.5f*s.ozoneWidth; k.sky[5][1]=s.ozoneWidth>0.f ? 2.f/s.ozoneWidth : 0.f;
    k.sky[5][2]=g; k.sky[5][3]=float(3.0/(8.0*3.14159265358979)*(1.0-g*g)/(2.0+g*g));
    row(k.sky[6], s.groundAlbedo, height);
    // HDRP's Horizon Zenith Shift: the tint's blend t made exp(10 k t) y - y, y = 1 / (exp(10 k) - 1) (k 0: nearly even).
    const double shift=std::abs(s.horizonZenithShift)<1e-6f ? 1e-6 : double(s.horizonZenithShift);
    const double sx=10.0*shift, sy=1.0/(std::exp(sx)-1.0);
    row(k.sky[7], s.horizonTint, float(sx));
    row(k.sky[8], s.zenithTint, float(sy));
    const uint32_t bodies=std::min(s.bodyCount, kSkyBodies);
    k.sky[9][0]=s.colorSaturation; k.sky[9][1]=s.alphaSaturation; k.sky[9][2]=s.alphaMultiplier; k.sky[9][3]=bits(bodies);
    row(k.sky[10], s.hdrpSun, s.intensity);
    std::memset(k.skyBodies, 0, sizeof(k.skyBodies));
    for(uint32_t i=0;i<bodies;++i) {
        const TGPT_SkyBody& b=s.bodies[i];
        float (*r)[4]=&k.skyBodies[i*kSkyBodyRows];
        float dir[3]={b.direction[0], b.direction[1], b.direction[2]};
        const float dl=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
        if(dl>0.f) for(float& x:dir) x/=dl;
        const float radius=std::clamp(b.angularRadius, 0.f, 1.5f), flare=std::max(b.flareSize, 1e-7f);
        row(r[0], dir, radius);
        row(r[1], b.color, bits(b.flags));
        row(r[2], b.surface, std::cos(radius));
        row(r[3], b.flare, std::cos(std::min(radius+flare, 3.14159265f)));
        row(r[4], b.sunTravel, b.earthshine);
        row(r[5], b.right, bits(uint32_t(b.surfaceTexture)));
        row(r[6], b.up, flare);
        r[7][0]=b.flareFalloff; r[7][1]=b.distance;
    }
    std::memset(k.skyClouds, 0, sizeof(k.skyClouds));
    for(uint32_t l=0;l<2;++l) {
        const TGPT_SkyLayer& c=s.layers[l];
        float (*r)[4]=&k.skyClouds[l*kSkyLayerRows];
        for(int i=0;i<4;++i) r[0][i]=c.opacities[i];
        r[1][0]=c.rotation; r[1][1]=c.steps; r[1][2]=c.sigma; r[1][3]=c.altitude;
        row(r[2], c.light, bits(uint32_t(c.map)));
        r[3][0]=c.scroll[0]; r[3][1]=c.scroll[1]; r[3][2]=c.scrolled; r[3][3]=bits(c.distortion);
        r[4][0]=c.ambientDimmer; r[4][1]=bits(uint32_t(c.flowmap)); r[4][2]=bits(c.on ? 1u : 0u);
    }
    k.skyClouds[2*kSkyLayerRows][0]=s.cloudOpacity;
    uint32_t flags=s.flags&(TGPT_SKY_HDRP_PIXELS|TGPT_SKY_CLOUDS|TGPT_SKY_CLOUDS_UPPER|TGPT_SKY_SHOW_MAP);
    if(!st.skyClouds) flags&=~TGPT_SKY_CLOUDS;
    k.skyMaps[0]=st.skyTransmittance.srv; k.skyMaps[1]=st.skyMultiple.srv; k.skyMaps[2]=st.skyGround.srv; k.skyMaps[3]=st.skyMap.srv;
    k.skyMaps2[0]=(flags&TGPT_SKY_CLOUDS) ? st.skyClouds.srv : kInvalid; k.skyMaps2[1]=st.skyAmbient.uav; k.skyMaps2[2]=TGPT_SKY_OWN|flags<<8;
    // The rays read the tracer's map, and the fog along them its mips (in the fog's Sky colour mode).
    k.inputs1[3]=st.skyMap.srv;
    if(f.fogColor[3]>0.5f) {
        for(int i=0;i<3;++i) k.fogColor[i]=std::isfinite(f.fogColor[i]) ? std::clamp(f.fogColor[i], 0.f, 64.f) : 0.f;
        std::memcpy(&k.fogMip[3], &st.skyMap.srv, 4); k.skyMaps2[3]=st.skyMap.mips;
    }
}

// The sky's passes (pt_sky.hlsl), after the frame's constants are bound and before any pass reads the sky.
void record_sky(ID3D12GraphicsCommandList* list, Barriers& b) {
    if(!st.skyOwn || !st.skyPass) return;
    const auto read=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, write=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    ID3D12PipelineState* pass=st.skyPass.Get();
    if(st.skyTablesPending) {
        b.transition(st.skyTransmittance, write); b.flush(list);
        dispatch(list, pass, {0u, st.skyTransmittance.uav}, b, kSkyTransmittanceW/8, kSkyTransmittanceH/8);
        b.transition(st.skyTransmittance, read); b.transition(st.skyMultiple, write); b.flush(list);
        dispatch(list, pass, {1u, st.skyMultiple.uav}, b, kSkyMultiple/8, kSkyMultiple/8);
        b.transition(st.skyMultiple, read); b.transition(st.skyGround, write); b.flush(list);
        dispatch(list, pass, {2u, st.skyGround.uav}, b, kSkyGround/8, 1);
        b.transition(st.skyGround, read); b.flush(list);
        st.skyTablesPending=false;
        if(st.skyTablesMade++==0) log("Sky (on trial): the atmosphere's tables made (transmittance, multiple scattering, the ground's irradiance)");
    }
    if(st.skyCloudsPending && st.skyClouds) {
        b.transition(st.skyClouds, write); b.flush(list);
        dispatch(list, pass, {3u, st.skyClouds.uav}, b, (st.skyClouds.width+7)/8, (st.skyClouds.height+7)/8);
        b.transition(st.skyClouds, read); b.flush(list);
        st.skyCloudsPending=false; ++st.skyCloudsBaked;
    }
    dispatch(list, pass, {4u, st.skyAmbient.uav}, b, kSkyAmbientGroups, 1);
    dispatch(list, pass, {7u, st.skyAmbient.uav}, b, 1, 1);
    b.transition(st.skyMap.resource.Get(), st.skyMap.state, write); b.flush(list);
    dispatch(list, pass, {5u, st.skyMap.uav[0]}, b, kSkyMapW/8, kSkyMapH/8);
    for(uint32_t m=1;m<st.skyMap.mips;++m) {
        const uint32_t w=std::max(kSkyMapW>>m, 1u), h=std::max(kSkyMapH>>m, 1u);
        dispatch(list, pass, {6u, st.skyMap.uav[m], st.skyMap.uav[m-1]}, b, (w+7)/8, (h+7)/8);
    }
    b.transition(st.skyMap.resource.Get(), st.skyMap.state, read); b.flush(list);
}
