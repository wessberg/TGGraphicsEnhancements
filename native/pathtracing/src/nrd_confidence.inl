// SPDX-License-Identifier: MIT
// Included after dispatch() in tracer.cpp. Confidence is entirely separate from the big passes.
// THE RULE whenever NRD runs, since 2026-10-11 (PT-127): NVIDIA's README calls IN_DIFF_CONFIDENCE "optional, but highly
// recommended", and the owner's ruling is to follow the guides. The game, the tavern: the same picture, 0.46 ms; with the
// world frozen no boil of its own (0.23 against 0.24, run 20261011-214719); with the candles running NRD follows them
// sooner (boil 0.29 against 0.25, run 20261011-190338). Until then a hidden trial (frame flag bit 21, free again).
bool record_nrd_confidence(ID3D12GraphicsCommandList* list, Barriers& b, const TGPT_Frame& f, bool reset, uint32_t kind) {
    (void)f;
    if(!st.nrdConfidenceTrace || !st.nrdConfidenceFilter) return false;
    auto& c=st.nrdConfidence;
    try {
        const bool allocated=bool(c.confidence);
        c.ensure(st.d12.Get(), st.heap, st.width, st.height);
        if(!allocated) log("NRD's history confidence: direct-light replay, 5x5 strata, five spatial passes; "+std::to_string(c.memory_bytes())+" bytes");
    } catch(const std::exception& e) {
        c.release(st.heap, st.release, st.consumer.value);
        log_throttled(std::string("NRD lighting confidence unavailable this frame: ")+e.what());
        return false;
    }
    const bool previous=c.havePrevious && c.frame+1u==f.frameIndex && c.scene==st.scene.clear_generation() && !reset;
    const uint32_t gx=(c.position.width+7u)/8u, gy=(c.position.height+7u)/8u;
    b.transition(c.samples, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for(Texture* t:{&c.gradient[0], &c.gradient[1], &c.position, &c.normal, &c.confidence})
        b.transition(*t, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    b.flush(list);
    dispatch(list, st.nrdConfidenceTrace.Get(), {c.samplesUav, c.gradient[0].uav, c.position.uav, c.normal.uav, previous ? 1u : 0u}, b, gx, gy);
    uint32_t read=0;
    for(uint32_t stride=1;stride<=5;++stride) {
        dispatch(list, st.nrdConfidenceFilter.Get(), {c.gradient[read].uav, c.gradient[read^1u].uav, c.position.uav, c.normal.uav, stride, kind}, b, gx, gy);
        read^=1u;
    }
    dispatch(list, st.nrdConfidenceFilter.Get(), {c.gradient[read].uav, c.confidence.uav, c.position.uav, c.normal.uav, 0u, kind}, b);
    c.frame=f.frameIndex; c.scene=st.scene.clear_generation(); c.havePrevious=true;
    return true;
}
