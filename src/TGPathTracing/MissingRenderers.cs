// SPDX-License-Identifier: MIT
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using Awaken.Kandra;
using Newtonsoft.Json.Linq;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.VFX;

namespace TGPathTracing
{
    internal static partial class MissingWorld
    {
        private static string DrawRule(Census c,Component component,bool enabled)
        {
            if(!component.gameObject.activeInHierarchy) return "inactive GameObject";
            if(!enabled) return "disabled component";
            if((c.Camera.cullingMask&(1<<component.gameObject.layer))==0) return "camera culling mask excludes layer "+component.gameObject.layer;
            return "";
        }
        private static void Renderers(Census c)
        {
            foreach(var group in UnityEngine.Object.FindObjectsByType<LODGroup>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                if(!group.enabled) continue;
                var lods=group.GetLODs(); var scale=group.transform.lossyScale;
                int chosen=WorldRules.GroupLevel(lods.Select(l=>l.screenRelativeTransitionHeight).ToArray(),group.size*Mathf.Max(Mathf.Abs(scale.x),Mathf.Max(Mathf.Abs(scale.y),Mathf.Abs(scale.z))),
                    Vector3.Distance(c.Camera.transform.position,group.transform.TransformPoint(group.localReferencePoint)),c.Camera.fieldOfView,c.Bias,c.MaximumLod);
                for(int i=0;i<lods.Length;++i) if(i!=chosen) foreach(var r in lods[i].renderers) if(r!=null) c.LodSkips[r.GetInstanceID()]="LODGroup "+group.name+": level "+i+", camera selects "+chosen;
                if(chosen>=0 && chosen<lods.Length) foreach(var r in lods[chosen].renderers) if(r!=null) c.LodSkips.Remove(r.GetInstanceID());
            }
            foreach(var r in UnityEngine.Object.FindObjectsByType<Renderer>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                if(!r.gameObject.scene.IsValid()) continue;
                string rule=DrawRule(c,r,r.enabled);
                if(r.forceRenderingOff) rule="Renderer.forceRenderingOff";
                if(r.shadowCastingMode==ShadowCastingMode.ShadowsOnly) rule="Renderer.shadowCastingMode ShadowsOnly";
                if(c.LodSkips.TryGetValue(r.GetInstanceID(),out string lodRule)) rule=lodRule;
                var row=c.Row("renderer:"+r.GetInstanceID(),r.name,PathOf(r),r.GetType().FullName,r.bounds,rule);
                if(row==null) continue;
                row["isVisibleAnyCamera"]=r.isVisible; row["staticBatch"]=r.isPartOfStaticBatch;
                row["hasPropertyBlock"]=r.HasPropertyBlock(); row["renderingLayers"]=r.renderingLayerMask;
                Mesh mesh=null;
                if(r is SkinnedMeshRenderer skin) mesh=skin.sharedMesh;
                else { var f=r.GetComponent<MeshFilter>(); if(f!=null) mesh=f.sharedMesh; }
                var materials=r.sharedMaterials;
                int start=r.isPartOfStaticBatch && r is MeshRenderer mr ? mr.subMeshStartIndex : 0;
                int[] subs=mesh==null ? new int[0] : r.isPartOfStaticBatch ? Enumerable.Range(start,materials.Length).ToArray() : Enumerable.Range(0,mesh.subMeshCount).ToArray();
                string guard=rule.Length>0?"SceneCapture renderer state/LOD: "+rule:"";
                if(!(r is MeshRenderer)) guard="SceneCapture enumerates MeshRenderer only; "+r.GetType().Name+" has no geometry adapter (Kandra is separate)";
                else if(!Session.Settings.SceneObjects.Value) guard="SceneCapture.Tick/Follow: SceneObjects setting Off";
                Describe(c,row,mesh,materials,r.localToWorldMatrix,subs,guard);
                if(r is ParticleSystemRenderer ps)
                {
                    var system=r.GetComponent<ParticleSystem>();
                    row["particleRenderMode"]=ps.renderMode.ToString(); row["particleCount"]=system!=null ? (JToken)system.particleCount : JValue.CreateNull();
                    var choices=new Mesh[ps.meshCount]; ps.GetMeshes(choices);
                    row["particleMeshes"]=new JArray(choices.Where(m=>m!=null).Select(m=>new JObject { ["name"]=m.name,["vertices"]=m.vertexCount,["submeshes"]=m.subMeshCount }));
                    row["trace"]=c.Traced?Excluded("no ParticleSystem geometry adapter; SceneCapture only takes MeshRenderer, not particles/cards/trails"):Unknown("PT is Off");
                }
                bool allDecals=materials.Length>0 && materials.All(DecalCapture.IsMeshMaterial);
                if(allDecals) row["trace"]=DecalMembership(c,r.GetInstanceID(),false,row,mesh,r.localToWorldMatrix,subs);
            }
        }
        private static void Characters(Census c)
        {
            var manager=KandraRendererManager.Instance;
            foreach(var r in UnityEngine.Object.FindObjectsByType<KandraRenderer>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                if(!r.gameObject.scene.IsValid()) continue;
                bool registered=manager!=null && manager.IsRegistered(r.RenderingId);
                string rule=DrawRule(c,r,r.enabled);
                uint mask=0;
                if(registered && manager.VisibilityCullingManager!=null)
                {
                    uint slot=KandraRendererManager.USlot(r.RenderingId);
                    var masks=manager.VisibilityCullingManager.layerMasks;
                    if(slot<masks.Length) { mask=masks[slot]; if((mask&(uint)c.Camera.cullingMask)==0) rule="Kandra culling layer mask outside camera mask (may be held hidden by tracer in On)"; }
                }
                Bounds bounds;
                if(registered)
                { manager.GetBoundsAndRootBone(r.RenderingId,out var sphere,out _); bounds=new Bounds(new Vector3(sphere.x,sphere.y,sphere.z),Vector3.one*(2*sphere.w)); }
                else { bounds=new Bounds(r.transform.position,Vector3.zero); if(rule.Length==0) rule="Kandra rendering ID not registered; posed bounds unavailable"; }
                var row=c.Row("kandra:"+r.GetInstanceID(),r.name,PathOf(r),"Awaken.Kandra.KandraRenderer",bounds,rule); if(row==null) continue;
                row["renderingId"]=r.RenderingId;
                row["kandraCullingMask"]=mask;
                var rd=r.rendererData; var mesh=rd.mesh; var mats=rd.RenderingMaterials;
                var materials=new JArray(); if(mats!=null) for(int s=0;s<mats.Length;++s) materials.Add(Material(mats[s],s,true)); row["materials"]=materials;
                int drawn=mesh!=null?Math.Min(mesh.submeshes!=null?mesh.submeshes.Length:1,mats!=null?mats.Length:0):0;
                if(mesh!=null)
                {
                    row["mesh"]=new JObject { ["id"]=mesh.GetInstanceID(),["name"]=mesh.name,["vertices"]=mesh.vertexCount,["submeshes"]=drawn };
                    long tris=0; for(uint s=0;s<drawn;++s) tris+=rd.RenderingMesh.IndexCount(s)/3; row["triangles"]=tris;
                }
                var registeredMaterials=new JArray();
                if(registered)
                {
                    var reader=typeof(CharacterCapture).GetMethod("RegisteredMaterial",Any);
                    for(int s=0;s<drawn;++s)
                    {
                        object[] args={manager,KandraRendererManager.USlot(r.RenderingId),s,null};
                        var actual=reader?.Invoke(null,args) as Material;
                        var material=Material(actual,s,true); material["readNote"]=Convert.ToString(args[3]); registeredMaterials.Add(material);
                    }
                }
                row["registeredDrawMaterials"]=registeredMaterials;
                object entry=Cache(typeof(CharacterCapture),"characters",r.GetInstanceID());
                int handle=entry==null?0:Convert.ToInt32(Get(entry,"Handle")); row["registeredMesh"]=handle;
                var found=new JArray(); bool accepted=false,missing=false;
                if(skins!=null) for(int i=0;i<skins.Length;++i) if(skins[i].Mesh==handle && handle>0)
                {
                    var t=skins[i]; var subs=new JArray(); var ms=new[]{t.M0,t.M1,t.M2,t.M3,t.M4,t.M5,t.M6,t.M7};
                    for(int s=0;s<drawn;++s)
                    {
                        bool kept=s<t.SubmeshCount && s<ms.Length && ms[s]!=0 && MaterialRule(mats[s],true).Length==0;
                        accepted|=kept; missing|=!kept;
                        subs.Add(new JObject { ["submesh"]=s,["material"]=s<ms.Length?(JToken)ms[s]:JValue.CreateNull(),["trianglesKept"]=kept });
                    }
                    found.Add(new JObject { ["route"]="SetSkinned",["slot"]=i,["mesh"]=handle,["frame"]=skinFrame,["submeshes"]=subs });
                }
                string why=!Session.Settings.Characters.Value?"CharacterCapture: Characters setting Off":rule;
                var observed=DecisionFor("kandra:"+r.GetInstanceID(),skinFrame);
                row["captureDecision"]=observed;
                if(observed!=null && ((string)observed["rule"]).Length>0) why=(string)observed["rule"];
                if(why.Length==0 && drawn==0) why="CharacterCapture: no drawn submeshes (minimum of mesh/material counts)";
                if(why.Length==0 && mats!=null && mats.Length>0 && mats.All(m=>MaterialRule(m,true).Length>0)) why=MaterialRule(mats[0],true);
                if(why.Length==0 && entry!=null && Convert.ToBoolean(Get(entry,"Failed"))) why="CharacterCapture.Find: triangle/UV readback or mesh registration failed; see plugin log";
                if(!c.Traced) row["trace"]=Unknown("PT is Off");
                else if(skinResult!=0 || skinFrame<Time.frameCount-2) row["trace"]=Unknown("skinned submission missing/stale/refused");
                else if(found.Count>0) row["trace"]=new JObject { ["status"]=accepted?(missing?"partial":"included"):"excluded",["instances"]=found,["rule"]=missing?"CharacterLeftOut, material 0, or submesh >= 8; see each slot":"accepted SetSkinned; not proof of correct skin/emission shading" };
                else if(why.Length>0) row["trace"]=Excluded(why);
                else if(entry!=null && Convert.ToBoolean(Get(entry,"Pending"))) row["trace"]=new JObject { ["status"]="pending",["rule"]="CharacterCapture.Find: asynchronous index/UV readback pending",["instances"]=new JArray() };
                else row["trace"]=Unknown("not submitted; inspect CharacterCapture instance-data, 256-renderer/2097152-vertex capacity and per-renderer exception gates");
            }
        }
        private static JObject DecalMembership(Census c,int id,bool projector,JObject row,Mesh mesh,Matrix4x4 world,int[] subs)
        {
            if(!c.Traced) return Unknown("PT is Off; decal uses separate material-change records");
            if(decalFrame<Time.frameCount-2 || decalResult!=0) return Unknown("decal submission missing, stale or refused");
            var matches=new JArray();
            var observed=new JArray();
            if(projector)
            { var d=DecisionFor("projector:"+id,decalFrame); if(d!=null) observed.Add(d); }
            else foreach(JObject material in (JArray)row["materials"])
            { var d=DecisionFor("decal:"+id+":"+(int)material["slot"],decalFrame); if(d!=null) observed.Add(d); }
            row["captureDecisions"]=observed;
            object cached=mesh==null?null:Cache(typeof(SceneCapture),"decalMeshes",mesh.GetInstanceID());
            int handle=cached==null?0:Convert.ToInt32(Get(cached,"Handle"));
            if(decals!=null) for(int i=0;i<decals.Length;++i)
            {
                var d=decals[i]; bool isMesh=(d.Flags&1024)!=0;
                if(projector==isMesh) continue;
                if(isMesh && (d.Reserved0!=handle || !subs.Contains((int)d.Reserved1))) continue;
                var actual=new Matrix4x4(); actual.SetRow(0,d.GraphWorld0); actual.SetRow(1,d.GraphWorld1); actual.SetRow(2,d.GraphWorld2); actual.m33=1;
                if(!Same(actual,world)) continue;
                matches.Add(new JObject { ["route"]="SetDecals",["slot"]=i,["mesh"]=handle,["frame"]=decalFrame,["flags"]=d.Flags,["submesh"]=d.Reserved1 });
            }
            if(matches.Count>0) return new JObject { ["status"]="included",["instances"]=matches,["rule"]="accepted decal record matched by geometry/transform; receiver layers, maps, fade and shading still matter" };
            if(observed.Count>0 && observed.All(d=>((string)d["rule"]).Length>0))
                return Excluded(string.Join("; ",observed.Select(d=>(string)d["rule"]).Distinct()));
            if(row["cameraEligible"]?.Value<bool>()==false) return Excluded("DecalCapture: "+(string)row["drawRule"]);
            var get=typeof(HDCamera).GetMethod("TryGet",Any); var ps=get.GetParameters(); var args=new object[ps.Length]; args[0]=c.Camera;
            for(int i=2;i<args.Length;++i) args[i]=ps[i].DefaultValue;
            if((bool)get.Invoke(null,args) && args[1] is HDCamera hd && !hd.frameSettings.IsEnabled(FrameSettingsField.Decals))
                return Excluded("DecalCapture: camera frame settings disable Decals");
            return Unknown("DecalCapture no matched record: frame-settings, unsupported shader/geometry, missing shared map, zero size or distance/fade gate; see coverage/log");
        }
        private static void Projectors(Census c)
        {
            foreach(var p in UnityEngine.Object.FindObjectsByType<DecalProjector>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                var scale=p.scaleMode==DecalScaleMode.InheritFromHierarchy?p.transform.lossyScale:Vector3.one;
                var ss=scale; if(scale.z<0) ss.y*=-1; if((ss.x<0)^(ss.y<0)^(ss.z<0)) ss.z*=-1;
                var size=new Vector3(p.size.x*ss.x,p.size.z*ss.z,p.size.y*ss.y);
                var os=scale; if(scale.z<0) { os.y*=-1; os.z*=-1; }
                var pivot=new Vector3(p.pivot.x*os.x,-p.pivot.z*os.z,p.pivot.y*os.y);
                var world=Matrix4x4.TRS(p.transform.position,p.transform.rotation*Quaternion.Euler(scale.z>=0?-90:90,0,0),Vector3.one)*Matrix4x4.TRS(pivot,Quaternion.identity,size);
                var bounds=TransformBounds(new Bounds(Vector3.zero,Vector3.one),world);
                string rule=DrawRule(c,p,p.enabled); if(p.material==null) rule="no projector material";
                if(p.fadeFactor<=0) rule="projector fadeFactor <= 0";
                var row=c.Row("projector:"+p.GetInstanceID(),p.name,PathOf(p),"HDRP DecalProjector",bounds,rule); if(row==null) continue;
                row["materials"]=new JArray(Material(p.material,0)); row["drawDistance"]=p.drawDistance; row["fadeFactor"]=p.fadeFactor; row["fadeScale"]=p.fadeScale;
                row["trace"]=DecalMembership(c,p.GetInstanceID(),true,row,null,world,new int[0]);
            }
        }
        private static void Effects(Census c)
        {
            foreach(var vfx in UnityEngine.Object.FindObjectsByType<VisualEffect>(FindObjectsInactive.Include,FindObjectsSortMode.None))
            {
                var r=vfx.GetComponent<Renderer>();
                if(r==null) { c.Coverage.Add(new JObject { ["source"]="VFX bounds",["ok"]=false,["error"]=PathOf(vfx)+": no renderer exposes bounds; GPU system attributes unavailable" }); continue; }
                var row=c.Row("vfx:"+vfx.GetInstanceID(),vfx.name,PathOf(vfx),"VisualEffect",r.bounds,DrawRule(c,vfx,vfx.enabled)); if(row==null) continue;
                var mats=new JArray(); var shared=r.sharedMaterials; for(int i=0;i<shared.Length;++i) mats.Add(Material(shared[i],i)); row["materials"]=mats;
                row["asset"]=vfx.visualEffectAsset!=null?vfx.visualEffectAsset.name:"<none>"; row["aliveParticles"]=vfx.aliveParticleCount;
                row["meshNote"]="GPU output geometry and triangle count are not exposed by this API";
                row["trace"]=c.Traced?Excluded("no VFX simulation-output geometry adapter in SceneCapture"):Unknown("PT is Off");
            }
        }
    }
}
