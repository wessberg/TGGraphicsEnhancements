// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using Awaken.TG.LeshyRenderer;
using AwesomeTechnologies.VegetationStudio;
using AwesomeTechnologies.VegetationSystem;
using Newtonsoft.Json.Linq;
using Unity.Mathematics;
using UnityEngine;

namespace TGPathTracing
{
    internal static partial class MissingWorld
    {
        private static void Leshy(Census c)
        {
            foreach(var manager in UnityEngine.Object.FindObjectsByType<LeshyManager>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                string rule=DrawRule(c,manager,manager.enabled); if(manager.ForceDisable) rule="Leshy ForceDisable";
                var authoring=manager.EDITOR_Prefabs;
                if(authoring==null) throw new InvalidOperationException(PathOf(manager)+": Leshy authoring unavailable");
                // RuntimePrefabs constructs descriptions from authored prefabs and current quality; it does
                // not load cells, create instances or touch LeshyCapture's cached descriptions.
                var runtime=authoring.RuntimePrefabs(manager.EDITOR_Quality); var source=authoring.Prefabs;
                var catalog=manager.EDITOR_Cells.cellsCatalog; var loading=manager.EDITOR_Loading;
                if(!catalog.IsCreated || !loading.filteredData.IsCreated) throw new InvalidOperationException(PathOf(manager)+": Leshy cell/filtered instance data unavailable");
                uint count=Math.Min(catalog.Length,loading.filteredData.Length);
                for(uint cell=0;cell<count;++cell)
                {
                    var data=catalog[cell];
                    var cellBounds=new Bounds(data.bounds.Center,2*(Vector3)data.bounds.Extents);
                    if(Project(c.Camera,cellBounds,c.Planes)==null) continue;
                    if(data.prefabId>=runtime.Length) throw new InvalidOperationException("Leshy prefab index outside runtime list");
                    var prefab=runtime[data.prefabId]; var renderers=prefab.renderers;
                    var placed=loading.filteredData[cell];
                    if(!placed.IsValid) continue; // the game has no loaded instances in this cell
                    var d=prefab.lodDistances; var distances=new float[8];
                    for(int k=0;k<8;++k) { float v=k<4?d.c0[k]:d.c1[k-4]; distances[k]=v*v; }
                    if(renderers==null) throw new InvalidOperationException("Leshy prefab has no renderer descriptions");
                    bool billboard=data.prefabId<source.Length && source[data.prefabId].HasBillboard;
                    for(uint k=0;k<placed.Length;++k)
                    {
                        var t=placed[k]; var world=Matrix(t.ToFloat4x4());
                        int lod=WorldRules.LeshyLevel(distances,math.distancesq((float3)c.Camera.transform.position,t.position));
                        if(lod<0) continue;
                        for(int part=0;part<renderers.Length;++part)
                        {
                            var r=renderers[part]; if((r.lodMask&(1<<lod))==0) continue;
                            bool card=billboard && part==renderers.Length-1;
                            var bounds=r.mesh!=null?TransformBounds(r.mesh.bounds,world):cellBounds;
                            string name=r.mesh!=null?r.mesh.name:"Leshy prefab "+data.prefabId;
                            var row=c.Row("leshy:"+manager.GetInstanceID()+":"+cell+":"+k+":"+part,name,PathOf(manager)+"/prefab:"+data.prefabId+"/cell:"+cell+"/instance:"+k+"/renderer:"+part,card?"Leshy billboard":"Leshy instanced mesh",bounds,rule);
                            if(row==null) continue;
                            row["lod"]=lod; row["prefabType"]=data.prefabId<source.Length?source[data.prefabId].prefabType.ToString():"unknown";
                            string guard=rule.Length>0?"LeshyCapture: "+rule:!Session.Settings.Vegetation.Value?"SceneCapture: Vegetation setting Off":"";
                            if(card) guard="LeshyCapture.Prefabs removes billboard; Capture substitutes coarsest mesh or skips when none exists";
                            Describe(c,row,r.mesh,r.materials,world,r.mesh==null?new int[0]:Enumerable.Range(0,r.mesh.subMeshCount).ToArray(),guard);
                            if(card)
                            {
                                int coarse=WorldRules.CoarsestMeshLevel(renderers.Take(renderers.Length-1).Select(x=>x.lodMask).ToArray(),lod);
                                var alternatives=new JArray();
                                for(int a=0;a<renderers.Length-1;++a) if(coarse>=0 && (renderers[a].lodMask&(1<<coarse))!=0 && renderers[a].mesh!=null)
                                {
                                    var rr=renderers[a]; var cache=Cache(typeof(SceneCapture),"meshes",rr.mesh.GetInstanceID()); int handle=cache==null?0:Convert.ToInt32(Get(cache,"Handle"));
                                    alternatives.Add(new JObject { ["mesh"]=rr.mesh.name,["membership"]=Membership(c,handle,world,Enumerable.Range(0,rr.mesh.subMeshCount).ToArray(),"") });
                                }
                                row["substitutes"]=alternatives;
                            }
                        }
                    }
                }
            }
        }
        private static void Vegetation(Census c)
        {
            var manager=VegetationStudioManager.Instance; var systems=manager!=null?manager.VegetationSystemList:null;
            if(systems==null) return;
            foreach(var system in systems)
            {
                if(system==null) continue;
                // Complete existing jobs before reading their arrays; do not schedule a draw or a load.
                system.CompleteCellLoading();
                var packages=system.VegetationPackageProList;
                for(int cell=0;cell<system.LoadedVegetationCellList.Count;++cell)
                {
                    var loaded=system.LoadedVegetationCellList[cell]; if(loaded==null) continue;
                    for(int p=0;p<loaded.VegetationPackageInstancesList.Count && p<packages.Count;++p)
                    {
                        var placed=loaded.VegetationPackageInstancesList[p]; var package=packages[p]; if(placed==null || package==null) continue;
                        placed.LoadVegetationJobHandle.Complete();
                        for(int item=0;item<placed.VegetationItemMatrixList.Count && item<package.VegetationInfoList.Count;++item)
                        {
                            var info=package.VegetationInfoList[item]; if(info==null) continue;
                            var list=placed.VegetationItemMatrixList[item]; if(!list.IsCreated) continue;
                            bool wanted=info.VegetationType==VegetationType.Tree || info.VegetationType==VegetationType.LargeObjects || info.VegetationType==VegetationType.Objects;
                            var root=info.VegetationPrefab;
                            if(root==null && list.Length>0) { c.Coverage.Add(new JObject { ["source"]="Vegetation Studio prefab",["ok"]=false,["error"]=PathOf(system)+"/item:"+item+": texture/billboard-only prefab has no GameObject bounds; "+list.Length+" instances" }); continue; }
                            if(root==null) continue;
                            var parts=root.GetComponentsInChildren<Renderer>(true);
                            for(int k=0;k<list.Length;++k)
                            {
                                var t=list[k]; var world=Matrix4x4.TRS(t.Position,t.Rotation,new Vector3(t.Scale.x,t.Scale.y,t.Scale.z));
                                for(int part=0;part<parts.Length;++part)
                                {
                                    var r=parts[part]; var f=r.GetComponent<MeshFilter>(); var mesh=f!=null?f.sharedMesh:null;
                                    var matrix=world*root.transform.worldToLocalMatrix*r.localToWorldMatrix;
                                    if(mesh==null) continue;
                                    var row=c.Row("vsp:"+system.GetInstanceID()+":"+cell+":"+p+":"+item+":"+k+":"+part,r.name,PathOf(system)+"/cell:"+cell+"/package:"+p+"/item:"+item+"/instance:"+k+"/"+r.name,"Vegetation Studio "+info.VegetationType,TransformBounds(mesh.bounds,matrix),DrawRule(c,system,system.enabled));
                                    if(row==null) continue;
                                    row["visibilityNote"]="loaded instance; VSP internal density/distance/LOD/billboard culling not proven by prefab renderer bounds";
                                    if((bool)row["cameraEligible"]) row["cameraEligible"]=JValue.CreateNull();
                                    string guard=!wanted?"VegetationCapture.Wanted: only Tree, LargeObjects and Objects; Grass/Plants excluded":!Session.Settings.Vegetation.Value?"SceneCapture Vegetation setting Off":"";
                                    Describe(c,row,mesh,r.sharedMaterials,matrix,Enumerable.Range(0,mesh.subMeshCount).ToArray(),guard);
                                }
                            }
                        }
                    }
                }
            }
        }
        private static void Medusa(Census c)
        {
            foreach(var manager in UnityEngine.Object.FindObjectsByType<Awaken.ECS.MedusaRenderer.MedusaRendererManager>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                var access=new Awaken.ECS.MedusaRenderer.MedusaRendererManager.EditorAccess(manager); var brg=access.BrgRenderer;
                if(brg.IsNull || !brg.IsValid) continue;
                var renderers=access.Renderers; int count=access.TransformsCount; if(renderers==null || count<=0) continue;
                // Read the game's own immutable archive. Never invoke SceneCapture.MedusaMatrices,
                // which changes the production capture cache, even while PT is Off.
                string path=Awaken.ECS.MedusaRenderer.MedusaPersistence.MatricesPath(brg.BasePath);
                var matrices=new Matrix4x4[count];
                using(var stream=File.OpenRead(path)) using(var reader=new BinaryReader(stream))
                {
                    if(stream.Length<(long)count*48) throw new InvalidDataException("Medusa matrix archive is truncated: "+path);
                    for(int i=0;i<count;++i) matrices[i]=new Matrix4x4(new Vector4(reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle(),0),new Vector4(reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle(),0),new Vector4(reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle(),0),new Vector4(reader.ReadSingle(),reader.ReadSingle(),reader.ReadSingle(),1));
                }
                var xs=brg.Xs; var ys=brg.Ys; var zs=brg.Zs;
                float scale=WorldRules.LodDistanceScale(c.Camera.fieldOfView,c.Bias);
                for(uint r=0;r<renderers.Length;++r)
                {
                    var data=renderers[r].renderData; if(data==null) continue; var placed=brg.TransformIndices(r);
                    for(uint k=0;k<placed.Length;++k)
                    {
                        uint t=placed[k]; if(t>=count) throw new InvalidDataException("Medusa transform index beyond archive");
                        float d2=(new Vector3(xs[t],ys[t],zs[t])-c.Camera.transform.position).sqrMagnitude*scale*scale;
                        int lod=WorldRules.MedusaLevel(d2,l=>brg.LodDistanceSq(t,(byte)l)); if((renderers[r].lodMask&(1<<lod))==0) continue;
                        for(int pair=0;pair<data.Count;++pair)
                        {
                            var datum=data[pair]; var mesh=datum.mesh; if(mesh==null) continue;
                            var row=c.Row("medusa:"+manager.GetInstanceID()+":"+r+":"+t+":"+pair,mesh.name,PathOf(manager)+"/renderer:"+r+"/transform:"+t+"/pair:"+pair,"Medusa instanced mesh",TransformBounds(mesh.bounds,matrices[t]),DrawRule(c,manager,manager.enabled));
                            Describe(c,row,mesh,new[]{datum.material},matrices[t],new[]{(int)datum.subMeshIndex},!Session.Settings.DrakeWorld.Value?"SceneCapture DrakeWorld setting Off":"");
                        }
                    }
                }
            }
        }
        private static void Terrains(Census c)
        {
            foreach(var terrain in UnityEngine.Object.FindObjectsByType<Terrain>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                var data=terrain.terrainData; if(data==null) continue;
                string rule=DrawRule(c,terrain,terrain.enabled); var origin=terrain.transform.position;
                var ground=c.Row("terrain:"+terrain.GetInstanceID(),terrain.name,PathOf(terrain),"Unity Terrain heightmap",new Bounds(origin+data.size*0.5f,data.size),rule);
                if(ground!=null)
                {
                    var h=Cache(typeof(SceneCapture),"terrains",terrain.GetInstanceID()); int handle=h==null?0:Convert.ToInt32(h);
                    ground["trace"]=Membership(c,handle,Matrix4x4.Translate(origin),new[]{0},!Session.Settings.Terrain.Value?"SceneCapture Terrain setting Off":"");
                    ground["heightmapResolution"]=data.heightmapResolution;
                    ground["materials"]=new JArray(Material(terrain.materialTemplate,0));
                }
                var trees=data.treeInstances; var prototypes=data.treePrototypes;
                for(int i=0;i<trees.Length;++i)
                {
                    var t=trees[i]; if(t.prototypeIndex<0 || t.prototypeIndex>=prototypes.Length) throw new InvalidDataException("terrain tree prototype out of range");
                    var root=prototypes[t.prototypeIndex].prefab; if(root==null) continue;
                    var position=origin+Vector3.Scale(t.position,data.size);
                    var world=Matrix4x4.TRS(position,Quaternion.Euler(0,t.rotation*Mathf.Rad2Deg,0),new Vector3(t.widthScale,t.heightScale,t.widthScale));
                    var renderers=root.GetComponentsInChildren<Renderer>(true);
                    for(int part=0;part<renderers.Length;++part)
                    {
                        var r=renderers[part]; var f=r.GetComponent<MeshFilter>(); var mesh=f!=null?f.sharedMesh:null;
                        var m=world*root.transform.worldToLocalMatrix*r.localToWorldMatrix;
                        var bounds=mesh!=null?TransformBounds(mesh.bounds,m):new Bounds(position,Vector3.one*t.heightScale);
                        string draw=rule; if(!terrain.drawTreesAndFoliage) draw="Terrain.drawTreesAndFoliage false";
                        if(Vector3.Distance(position,c.Camera.transform.position)>terrain.treeDistance) draw="beyond Terrain.treeDistance";
                        var row=c.Row("tree:"+terrain.GetInstanceID()+":"+i+":"+part,r.name,PathOf(terrain)+"/tree:"+i+"/"+r.name,"Unity Terrain tree",bounds,draw);
                        Describe(c,row,mesh,r.sharedMaterials,m,mesh==null?new int[0]:Enumerable.Range(0,mesh.subMeshCount).ToArray(),"SceneCapture.CaptureTerrains only builds heightmap; no treeInstances adapter");
                        if(row!=null) { row["visibilityNote"]="prefab levels enumerated; Unity billboard/LOD selection not public here"; if((bool)row["cameraEligible"]) row["cameraEligible"]=JValue.CreateNull(); }
                    }
                }
                var details=data.detailPrototypes;
                // Engine-generated per-instance transforms, with the game's scatter/density/jitter.
                // Reading these does not spawn objects or call the game's renderer twice.
                for(int patchY=0;patchY<data.detailPatchCount;++patchY) for(int patchX=0;patchX<data.detailPatchCount;++patchX) for(int layer=0;layer<details.Length;++layer)
                {
                    var prototype=details[layer];
                    var instances=data.ComputeDetailInstanceTransforms(patchX,patchY,layer,terrain.detailObjectDensity,out Bounds patchBounds);
                    for(int i=0;i<instances.Length;++i)
                    {
                        var d=instances[i]; var position=origin+new Vector3(d.posX,d.posY,d.posZ);
                        string draw=rule; if(!terrain.drawTreesAndFoliage) draw="Terrain.drawTreesAndFoliage false";
                        if(Vector3.Distance(position,c.Camera.transform.position)>terrain.detailObjectDistance) draw="beyond Terrain.detailObjectDistance";
                        var root=prototype.prototype; var parts=root!=null?root.GetComponentsInChildren<MeshRenderer>(true):new MeshRenderer[0];
                        string ident="detail:"+terrain.GetInstanceID()+":"+patchX+":"+patchY+":"+layer+":"+i;
                        if(parts.Length==0)
                        {
                            var row=c.Row(ident+":card",prototype.prototypeTexture!=null?prototype.prototypeTexture.name:"terrain detail card",PathOf(terrain)+"/"+ident,"Unity Terrain detail card",new Bounds(position+Vector3.up*d.scaleY*0.5f,new Vector3(d.scaleXZ,d.scaleY,d.scaleXZ)),draw);
                            if(row==null) continue;
                            row["meshNote"]="camera-facing generated card; no Mesh or triangle count exposed by the terrain API";
                            row["trace"]=c.Traced?Excluded("SceneCapture.CaptureTerrains has no detail instance/card adapter"):Unknown("PT is Off");
                        }
                        for(int part=0;part<parts.Length;++part)
                        {
                            var r=parts[part]; var f=r.GetComponent<MeshFilter>(); var mesh=f!=null?f.sharedMesh:null; if(mesh==null) continue;
                            var m=Matrix4x4.TRS(position,Quaternion.Euler(0,d.rotationY*Mathf.Rad2Deg,0),new Vector3(d.scaleXZ,d.scaleY,d.scaleXZ))*root.transform.worldToLocalMatrix*r.localToWorldMatrix;
                            var row=c.Row(ident+":"+part,r.name,PathOf(terrain)+"/"+ident+"/"+r.name,"Unity Terrain detail mesh",TransformBounds(mesh.bounds,m),draw);
                            Describe(c,row,mesh,r.sharedMaterials,m,Enumerable.Range(0,mesh.subMeshCount).ToArray(),"SceneCapture.CaptureTerrains has no detail instance adapter");
                        }
                    }
                }
            }
        }
    }
}
