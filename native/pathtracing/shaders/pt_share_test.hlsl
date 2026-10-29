// SPDX-License-Identifier: MIT
// PT-45, the start-up self-test of texture sharing (texture_share.cpp self_test): one mip of a texture the game's kind of
// D3D11 device made shareable, read through the very view the tracer makes of it, against a texture D3D12 made itself
// from the same bytes, read the same way. Every texel must come out bit for bit the same. Per test slot, three counters
// in the results buffer: texels that differ, texels read, and texels that are not all zero (a view that reads nothing
// would otherwise agree with itself). Its own root signature: it runs before, and apart from, the tracer's passes.
// volume 1: a 3D texture (PT-112, fog volumes' masks), one slice of the mip per z of the dispatch.
#define SHARE_TEST_ROOT "RootFlags(CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED), RootConstants(num32BitConstants=8, b0)"

cbuffer ShareTest : register(b0) {
    uint sharedView, referenceView, results, slot, mip, width, height, volume;
};

[RootSignature(SHARE_TEST_ROOT)]
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    const bool inside = id.x < width && id.y < height;
    bool differs = false, lit = false;
    if (inside) {
        uint4 a, b;
        if (volume != 0u) {
            Texture3D<float4> games = ResourceDescriptorHeap[sharedView];
            Texture3D<float4> own = ResourceDescriptorHeap[referenceView];
            a = asuint(games.Load(int4(id, mip)));
            b = asuint(own.Load(int4(id, mip)));
        } else {
            Texture2D<float4> games = ResourceDescriptorHeap[sharedView];
            Texture2D<float4> own = ResourceDescriptorHeap[referenceView];
            a = asuint(games.Load(int3(id.xy, mip)));
            b = asuint(own.Load(int3(id.xy, mip)));
        }
        differs = any(a != b);
        lit = any(a != 0u);
    }
    const uint different = WaveActiveCountBits(differs), read = WaveActiveCountBits(inside), nonzero = WaveActiveCountBits(lit);
    if (WaveIsFirstLane()) {
        RWByteAddressBuffer counts = ResourceDescriptorHeap[results];
        uint ignored;
        counts.InterlockedAdd(slot * 16u, different, ignored);
        counts.InterlockedAdd(slot * 16u + 4u, read, ignored);
        counts.InterlockedAdd(slot * 16u + 8u, nonzero, ignored);
    }
}
