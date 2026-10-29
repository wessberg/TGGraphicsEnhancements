// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Linq;
using Awaken.ECS.DrakeRenderer.Authoring;
using Awaken.ECS.DrakeRenderer.Components;
using Unity.Collections;
using Unity.Entities;
using Unity.Mathematics;
using Unity.Rendering;
using Unity.Transforms;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.ResourceManagement.AsyncOperations;
using Newtonsoft.Json.Linq;

namespace TGPathTracing
{
    internal static partial class MissingWorld
    {
        private static Matrix4x4 Matrix(float4x4 v) => new Matrix4x4(new Vector4(v.c0.x,v.c0.y,v.c0.z,v.c0.w),new Vector4(v.c1.x,v.c1.y,v.c1.z,v.c1.w),new Vector4(v.c2.x,v.c2.y,v.c2.z,v.c2.w),new Vector4(v.c3.x,v.c3.y,v.c3.z,v.c3.w));
        private static T Loaded<T>(AsyncOperationHandle<T> h,ushort count) where T:UnityEngine.Object => count>0 && h.IsValid() && h.IsDone && h.Status==AsyncOperationStatus.Succeeded?h.Result:null;
        private static unsafe void Entities(Census c)
        {
            foreach(var world in World.All)
            {
                if(!world.IsCreated) continue;
                var em=world.EntityManager; var graphics=world.GetExistingSystemManaged<EntitiesGraphicsSystem>();
                var drake=DrakeRendererManager.Instance;
                // Diagnostic query, including disabled entities. No capture query/filter/cache is changed.
                using(var query=em.CreateEntityQuery(new EntityQueryDesc {
                    All=new[]{ComponentType.ReadOnly<LocalToWorld>()},
                    Any=new[]{ComponentType.ReadOnly<DrakeMeshMaterialComponent>(),ComponentType.ReadOnly<MaterialMeshInfo>()},
                    Options=EntityQueryOptions.IncludeDisabledEntities|EntityQueryOptions.IgnoreComponentEnabledState }))
                {
                    em.CompleteDependencyBeforeRO<LocalToWorld>();
                    var fh=em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RenderFilterSettings>();
                    var ah=em.GetSharedComponentTypeHandle<RenderMeshArray>();
                    var sh=em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RendererRange>();
                    var ih=em.GetComponentTypeHandle<MaterialMeshInfo>(true);
                    var dh=em.GetComponentTypeHandle<DrakeMeshMaterialComponent>(true);
                    var th=em.GetComponentTypeHandle<LocalToWorld>(true);
                    var rh=em.GetComponentTypeHandle<LODRange>(true);
                    var ph=em.GetComponentTypeHandle<LODWorldReferencePoint>(true);
                    var bh=em.GetComponentTypeHandle<WorldRenderBounds>(true);
                    var disabled=em.GetComponentTypeHandle<DisableRendering>(true);
                    var inactive=em.GetComponentTypeHandle<Disabled>(true);
                    var eh=em.GetEntityTypeHandle();
                    using(var chunks=query.ToArchetypeChunkArray(Allocator.Temp)) foreach(var chunk in chunks)
                    {
                        bool isDrake=chunk.Has(ref dh),hasInfo=chunk.Has(ref ih),baked=chunk.Has(ah);
                        var array=baked?chunk.GetSharedComponentManaged(ah,em):default;
                        MaterialMeshInfo* pairs=chunk.Has(sh)?chunk.GetSharedComponent(sh,em).MaterialMeshInfos:null;
                        var entities=chunk.GetNativeArray(eh); var transforms=chunk.GetNativeArray(ref th);
                        var info=hasInfo?chunk.GetNativeArray(ref ih):default; var dm=isDrake?chunk.GetNativeArray(ref dh):default;
                        bool lod=chunk.Has(ref rh)&&chunk.Has(ref ph),bounded=chunk.Has(ref bh);
                        var ranges=lod?chunk.GetNativeArray(ref rh):default; var refs=lod?chunk.GetNativeArray(ref ph):default;
                        var bounds=bounded?chunk.GetNativeArray(ref bh):default;
                        string chunkRule=chunk.Has(ref inactive)?"entity Disabled":chunk.Has(ref disabled)?"entity DisableRendering":"";
                        uint layers=uint.MaxValue;
                        if(chunk.Has(fh))
                        {
                            var filter=chunk.GetSharedComponent(fh,em); layers=filter.RenderingLayerMask;
                            if((c.Camera.cullingMask&(1<<filter.Layer))==0) chunkRule="RenderFilterSettings layer outside camera culling mask";
                            if(filter.ShadowCastingMode==ShadowCastingMode.ShadowsOnly) chunkRule="RenderFilterSettings ShadowsOnly";
                        }
                        for(int i=0;i<chunk.Count;++i)
                        {
                            var e=entities[i]; var matrix=Matrix(transforms[i].Value); string rule=chunkRule;
                            if(!hasInfo) rule="Drake entity has no MaterialMeshInfo: game has not loaded it for drawing";
                            if(hasInfo && !em.IsComponentEnabled<MaterialMeshInfo>(e)) rule="MaterialMeshInfo component disabled";
                            if(lod)
                            {
                                var r=ranges[i]; float d=math.distance(refs[i].Value,(float3)c.Camera.transform.position)*WorldRules.LodDistanceScale(c.Camera.fieldOfView,c.Bias);
                                if(!WorldRules.EntityLevelDrawn(r.LODMask,r.MinDist,r.MaxDist,d,c.MaximumLod)) rule="LODRange outside selected level: scaled distance "+d+" not in ["+r.MinDist+","+r.MaxDist+") / maximum LOD "+c.MaximumLod;
                            }
                            Bounds? bound=null;
                            if(bounded) { var b=bounds[i].Value; bound=new Bounds(b.Center,2*(Vector3)b.Extents); }
                            string baseId="entity:"+world.Name+":"+e.Index+":"+e.Version;
                            string name=em.GetName(e); if(string.IsNullOrEmpty(name)) name=e.ToString();
                            string path=world.Name+"/"+name+" ["+e.Index+":"+e.Version+"]";
                            void Add(Mesh mesh,Material material,int sub,string suffix,string resolve)
                            {
                                if(mesh==null && bound==null)
                                { c.Coverage.Add(new JObject { ["source"]="entity bounds",["ok"]=false,["error"]=baseId+": no WorldRenderBounds or resolved mesh: "+resolve }); return; }
                                var b=bound??TransformBounds(mesh.bounds,matrix);
                                var row=c.Row(baseId+suffix,name,path,isDrake?"Drake entity":"Entities Graphics entity",b,rule); if(row==null) return;
                                row["entity"]=new JObject { ["world"]=world.Name,["index"]=e.Index,["version"]=e.Version,["materialMeshInfo"]=hasInfo,["renderingLayers"]=layers };
                                string guard=rule.Length>0?"SceneCapture entity draw gate: "+rule:"";
                                if(world!=World.DefaultGameObjectInjectionWorld) guard="SceneCapture only enumerates World.DefaultGameObjectInjectionWorld";
                                if(!Session.Settings.DrakeWorld.Value) guard="SceneCapture: DrakeWorld setting Off";
                                if(mesh==null) guard="SceneCapture cannot resolve mesh: "+resolve;
                                Describe(c,row,mesh,new[]{material},matrix,new[]{sub},guard);
                                row["materialSubmesh"]=sub; row["resolve"]=resolve;
                            }
                            void Pair(MaterialMeshInfo mm,int pair)
                            {
                                Mesh mesh=null; Material material=null;
                                if(graphics!=null && mm.IsRuntimeMesh && mm.MeshID.value!=0) mesh=graphics.GetMesh(mm.MeshID);
                                else if(baked && !mm.IsRuntimeMesh) mesh=array.GetMesh(mm);
                                if(graphics!=null && mm.Material>=0) material=graphics.GetMaterial(mm.MaterialID);
                                else if(baked && mm.Material<0) material=array.GetMaterial(mm);
                                Add(mesh,material,Mathf.Max(0,(int)mm.SubMesh),":"+pair,"MaterialMeshInfo mesh "+mm.Mesh+", material "+mm.Material+", baked array "+baked);
                            }
                            if(isDrake)
                            {
                                var mm=dm[i]; Mesh mesh=null; Material material=null;
                                if(drake!=null)
                                {
                                    var md=drake.LoadingManager.MeshLoadingData; var mt=drake.LoadingManager.MaterialLoadingData;
                                    if(mm.meshIndex<md.Count) { var d=md[mm.meshIndex]; mesh=Loaded(d.loadingHandle,d.counter); }
                                    if(mm.materialIndex<mt.Count) { var d=mt[mm.materialIndex]; material=Loaded(d.loadingHandle,d.counter); }
                                }
                                Add(mesh,material,mm.submesh,":0","Drake meshIndex "+mm.meshIndex+", materialIndex "+mm.materialIndex+"; only completed successful loading handles with counter > 0");
                            }
                            else if(!info[i].HasMaterialMeshIndexRange) Pair(info[i],0);
                            else if(pairs!=null)
                            { var span=info[i].MaterialMeshIndexRange; for(int j=span.start;j<span.end;++j) Pair(pairs[j],j-span.start); }
                            else Add(null,null,0,":0","MaterialMeshInfo range has no RendererRange pair pointer");
                        }
                    }
                }
            }
        }
    }
}
