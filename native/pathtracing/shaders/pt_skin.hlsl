// SPDX-License-Identifier: MIT
// Characters: one character's skinned positions from this frame's shared texture (a vertex a texel, 4096 a
// row) into its position buffer, which its BLAS is rebuilt from and its hits read. Reads no frame constants.
//   pass.a: the texture (SRV), the positions (raw UAV), the first texel, the vertex count
//   pass.b.x: its previous positions (raw UAV; INVALID: none kept), which take what the positions held, the frame before's,
//   for a hit's own motion (pt_primary hit_motion); pass.b.y: 1 when they held nothing of the frame before (the character is
//   new, or was not traced last frame): its previous positions are then this frame's, no motion.
//   pass.b.z, ON TRIAL (PT-92): its normals (raw UAV, 4 bytes a vertex; INVALID: the texture brings none). The texel's alpha
//   then holds the game's own skinned normal of the vertex as its skinning left it (TGPT_SetSkinned: a unit vector as an
//   octahedral map, its two coordinates half floats from 0 to 1, x in the low 16 bits, with SKIN_NORMAL_MARK set on the way
//   here), which goes into the buffer as the tracer packs a mesh's normals (pack_normal): the character's mesh record names
//   it, and fetch_hit reads its smooth normal (Hit.rounded) there as it does any other mesh's.
#include "pt_common.hlsli"

// Set on the packed normal while it travels in a float channel (d3d11.hlsl TGPT_SKIN): no valid one has the bit (a half
// float of 0 to 1 is at most 0x3C00), and with it every one is an ordinary float, whatever is done to floats on the way.
static const uint SKIN_NORMAL_MARK = 0x40000000u;
float3 skinned_normal(uint packed) {
    packed &= ~SKIN_NORMAL_MARK;
    const float2 e = saturate(float2(f16tof32(packed & 0xFFFFu), f16tof32(packed >> 16))) * 2.0 - 1.0;
    float3 n = float3(e, 1.0 - abs(e.x) - abs(e.y));
    const float t = saturate(-n.z);
    n.xy += select(n.xy >= 0.0, -t, t);
    return normalize(n);
}

[RootSignature(PT_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= pass.a.w) return;
    Texture2D<float4> skin = ResourceDescriptorHeap[pass.a.x];
    RWByteAddressBuffer positions = ResourceDescriptorHeap[pass.a.y];
    uint t = pass.a.z + id.x;
    const float4 texel = skin.Load(int3(t % 4096u, t / 4096u, 0));
    float3 p = texel.xyz;
    if (!all(isfinite(p))) p = 0.0;
    if (pass.b.x != INVALID) {
        RWByteAddressBuffer previous = ResourceDescriptorHeap[pass.b.x];
        previous.Store3(id.x * 12u, pass.b.y == 1u ? asuint(p) : positions.Load3(id.x * 12u));
    }
    positions.Store3(id.x * 12u, asuint(p));
    if (pass.b.z != INVALID) {
        RWByteAddressBuffer normals = ResourceDescriptorHeap[pass.b.z];
        normals.Store(id.x * 4u, pack_normal(skinned_normal(asuint(texel.w))));
    }
}
