// SPDX-License-Identifier: MIT
// Standalone arithmetic reference comparator; this stays in the worker's private evidence folder.
struct DecalRecord { float4 graph[24]; int graphTextures[8]; float4 graphWorld[3]; float4 toDecal[3]; float4 graphEnv,graphEnv2,graphViewZ;uint samplers[12];float samplerBias[12]; };
struct DecalGraphInput {float2 uv,gx,gy; float3 position,dx,dy,normal;float4 tangent,color;};
struct DecalGraphOutput {float4 color,normal,mask;};
struct Globals {uint4 hair;float4 cameraPos; float4 cacheOrigin;};
static const Globals g={uint4(0,0,0,0),float4(0,2,-3,0),float4(0,0,0,0)};
Texture2D<float4> tex0:register(t0); Texture2D<float4> tex1:register(t1); Texture2D<float4> tex2:register(t2); Texture2D<float4> tex3:register(t3);
Texture2D<float4> tex4:register(t4); Texture2D<float4> tex5:register(t5); Texture2D<float4> tex6:register(t6); Texture2D<float4> tex7:register(t7);
SamplerState wrapSampler:register(s0); StructuredBuffer<DecalRecord> records:register(t10);
float4 decal_graph_sample(int slot,uint samplerIndex,float mipBias,float2 uv,float2 dx,float2 dy,float4 fallback){
 switch(slot){case 0:return tex0.SampleGrad(wrapSampler,uv,dx,dy);case 1:return tex1.SampleGrad(wrapSampler,uv,dx,dy);case 2:return tex2.SampleGrad(wrapSampler,uv,dx,dy);case 3:return tex3.SampleGrad(wrapSampler,uv,dx,dy);case 4:return tex4.SampleGrad(wrapSampler,uv,dx,dy);case 5:return tex5.SampleGrad(wrapSampler,uv,dx,dy);case 6:return tex6.SampleGrad(wrapSampler,uv,dx,dy);default:return tex7.SampleGrad(wrapSampler,uv,dx,dy);}
}
float4 decal_graph_sample_level(int slot,uint samplerIndex,float2 uv,float lod,float4 fallback){
 switch(slot){case 0:return tex0.SampleLevel(wrapSampler,uv,lod);case 1:return tex1.SampleLevel(wrapSampler,uv,lod);case 2:return tex2.SampleLevel(wrapSampler,uv,lod);case 3:return tex3.SampleLevel(wrapSampler,uv,lod);case 4:return tex4.SampleLevel(wrapSampler,uv,lod);case 5:return tex5.SampleLevel(wrapSampler,uv,lod);case 6:return tex6.SampleLevel(wrapSampler,uv,lod);default:return tex7.SampleLevel(wrapSampler,uv,lod);}
}
float4 decal_graph_object_column(DecalRecord d,uint c){return float4(d.graphWorld[0][c],d.graphWorld[1][c],d.graphWorld[2][c],c==3?1:0);}
float4 decal_graph_inverse_column(DecalRecord d,uint c){return float4(d.toDecal[0][c],d.toDecal[1][c],d.toDecal[2][c],c==3?1:0);}
float dgselect(bool c,float a,float b){return c?a:b;}
float2 dgselect(bool2 c,float2 a,float2 b){return c?a:b;}
float3 dgselect(bool3 c,float3 a,float3 b){return c?a:b;}
float4 dgselect(bool4 c,float4 a,float4 b){return c?a:b;}
#define select dgselect
#include "graphs.hlsli"
struct VsOut {float4 position:SV_POSITION;float4 tangent:INTERP0;float4 uv:INTERP1;float4 world:INTERP2;float4 normal:INTERP3;float4 extra:INTERP4;};
VsOut vs(uint id:SV_VertexID){VsOut o;float2 uv=float2((id<<1)&2,id&2);o.position=float4(uv*float2(2,-2)+float2(-1,1),0.5,1);o.uv=float4(uv,0,0);o.tangent=float4(1,0,0,-1);o.normal=float4(0,1,0,0);o.world=float4(uv.x*2-1,-2,uv.y*2+2,1);o.extra=o.normal;return o;}
VsOut bfxvs(uint id:SV_VertexID){VsOut o=vs(id);o.world=1;return o;}
struct PsOut{float4 color:SV_TARGET0;float4 normal:SV_TARGET1;float4 mask:SV_TARGET2;};
PsOut main(VsOut i){DecalGraphInput at;at.uv=i.uv.xy;at.gx=ddx(at.uv);at.gy=ddy(at.uv);at.position=i.world.xyz;at.dx=ddx(at.position);at.dy=ddy(at.position);at.normal=i.normal.xyz;at.tangent=i.tangent;at.color=i.world;DecalGraphOutput o=GRAPH_FUNCTION(records[0],at);PsOut p;p.color=o.color;p.normal=o.normal;p.mask=o.mask;
#if MUTATED_GRAPH
p.color+=0.125;p.normal+=0.125;p.mask+=0.125;
#endif
return p;}
