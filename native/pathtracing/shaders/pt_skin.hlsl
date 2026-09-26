// SPDX-License-Identifier: MIT
// Characters: one character's skinned positions from this frame's shared texture (a vertex a texel, 4096 a
// row) into its position buffer, which its BLAS is rebuilt from and its hits read. Reads no frame constants.
//   pass.a: the texture (SRV), the positions (raw UAV), the first texel, the vertex count
#include "pt_common.hlsli"

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= pass.a.w) return;
    Texture2D<float4> skin = ResourceDescriptorHeap[pass.a.x];
    RWByteAddressBuffer positions = ResourceDescriptorHeap[pass.a.y];
    uint t = pass.a.z + id.x;
    float3 p = skin.Load(int3(t % 4096u, t / 4096u, 0)).xyz;
    if (!all(isfinite(p))) p = 0.0;
    positions.Store3(id.x * 12u, asuint(p));
}
