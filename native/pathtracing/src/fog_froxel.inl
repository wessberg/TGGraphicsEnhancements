// SPDX-License-Identifier: MIT
// PT-118: the camera's fog in cells. Included after dispatch() in tracer.cpp. No SDK records or public ABI additions.
// Real 3D volumes keep reads between cells from crossing slices accidentally. RGBA32F: a cell's light has no bound on a
// sample. kFogSlices: pt_froxel.hlsli's FOG_SLICES (48 to 20 m, 48 more to the far distance).
constexpr uint32_t kFogSlices=96;
Texture make_fog_grid(uint32_t w, uint32_t h, DXGI_FORMAT format=DXGI_FORMAT_R32G32B32A32_FLOAT) {
    Texture t; t.width=w; t.height=h; t.format=format;
    t.state=D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D; d.Width=w; d.Height=h;
    d.DepthOrArraySize=UINT16(kFogSlices); d.MipLevels=1; d.Format=t.format; d.SampleDesc.Count=1;
    d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const auto props=heap_props(D3D12_HEAP_TYPE_DEFAULT);
    check(st.d12->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &d, t.state, nullptr, IID_PPV_ARGS(&t.resource)), "Create fog froxels");
    try {
        t.srv=st.heap.allocate(); t.uav=st.heap.allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC s{}; s.Format=t.format; s.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;
        s.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; s.Texture3D.MipLevels=1;
        st.d12->CreateShaderResourceView(t.resource.Get(), &s, st.heap.cpu(t.srv));
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{}; u.Format=t.format; u.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D;
        u.Texture3D.WSize=kFogSlices;
        st.d12->CreateUnorderedAccessView(t.resource.Get(), nullptr, &u, st.heap.cpu(t.uav));
    } catch(...) { st.heap.release(t.srv); st.heap.release(t.uav); throw; }
    return t;
}

// The passes of a frame: the cells' light (pt_fog_froxel), each pixel's walk through the cells over its whole way
// (pt_fog_integrate), and what the cells do not hold (pt_fog_tail: the lamps' light right by them).
template<class Mark>
bool record_fog_froxels(ID3D12GraphicsCommandList* list, Barriers& b, const TGPT_Frame& f, bool reset,
                        const FrameConstants& k, uint32_t output, const Mark& mark) {
    if(!st.fogFroxel || !st.fogIntegrate || !st.fogTail) return false;
    const float reference=k.radiance[0];
    const uint32_t w=(st.width+15u)/16u, h=(st.height+15u)/16u;
    try {
        for(auto& t:st.fogGrid) if(!t.resource) { t=make_fog_grid(w,h); st.fogGridFrame=0; }
    } catch(const std::exception& e) {
        log_throttled(std::string("The fog's cells could not be made; no traced fog this frame: ")+e.what());
        st.fogGridFrame=0; return false;
    }
    const uint32_t now=f.frameIndex&1u;
    // What the cells' pass is told (its pass.a.z): bit 0 the light the fog scatters more than once.
    const uint32_t mode=(f.flags&TGPT_FRAME_FOG_WALK) ? 1u : 0u;
    const bool history=st.fogGridFrame!=0 && st.fogGridFrame+1==f.frameIndex && !reset &&
                       st.fogGridScene==st.scene.clear_generation() && st.fogGridMode==mode;
    b.transition(st.fogGrid[now], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if(history) b.transition(st.fogGrid[now^1u], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    b.flush(list);
    uint32_t referenceBits=0; std::memcpy(&referenceBits, &st.fogGridReference, sizeof(referenceBits));
    const uint32_t values[8]={st.fogGrid[now].uav, history ? st.fogGrid[now^1u].srv : kInvalid, mode, referenceBits,
                              kInvalid, kInvalid, kInvalid, kInvalid};
    list->SetPipelineState(st.fogFroxel.Get()); list->SetComputeRoot32BitConstants(1,8,values,0);
    list->Dispatch((w+7u)/8u,(h+7u)/8u,kFogSlices);
    b.uav(nullptr); b.transition(st.fogGrid[now], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); b.flush(list);
    mark(11);
    dispatch(list, st.fogIntegrate.Get(), {output,st.fogGrid[now].srv}, b);
    mark(12);
    dispatch(list, st.fogTail.Get(), {output}, b);
    mark(13);
    st.fogGridFrame=f.frameIndex; st.fogGridScene=st.scene.clear_generation(); st.fogGridMode=mode;
    st.fogGridReference=reference>0.f && std::isfinite(reference) ? reference : 1.f;
    static uint32_t saidW=0,saidH=0;
    if(w!=saidW || h!=saidH) {
        saidW=w; saidH=h;
        log("The fog's cells: "+std::to_string(w)+" x "+std::to_string(h)+" x "+std::to_string(kFogSlices)+
            " (48 slices to 20 m, 48 more to the far distance), history 254 frames; a lamp's light right by it traced per pixel");
    }
    return true;
}
