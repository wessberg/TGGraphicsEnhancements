// SPDX-License-Identifier: MIT
// PT-83. Read-only diagnostic, armed only by the separate harness's missing recipe.
// An accepted submission proves membership in the native input, not that a ray hit it.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;

namespace TGPathTracing
{
    internal static partial class MissingWorld
    {
        internal static bool Enabled;
        private static NativeInstance[] scene;
        private static uint[] sceneMaterials, movingMaterials, replaced;
        private static NativeMoving[] movers;
        private static NativeSkinnedInstance[] skins;
        private static NativeDecal[] decals;
        private static int sceneFrame = -1, movingFrame = -1, skinFrame = -1, decalFrame = -1;
        private static int sceneResult = -1, movingResult = -1, skinResult = -1, decalResult = -1;
        private static Dictionary<string,(int frame,string rule,int slot)> decisions;
        internal static void Decision(string id,string rule,int slot=-1)
        { if(Enabled) decisions[id]=(Time.frameCount,rule,slot); }
        private static JObject DecisionFor(string id,int frame)
        {
            if(decisions!=null && decisions.TryGetValue(id,out var d) && d.frame==frame)
                return new JObject { ["frame"]=d.frame,["rule"]=d.rule,["slot"]=d.slot };
            return null;
        }
        // Arrays passed to these native calls are fresh, owned arrays and are never changed afterwards.
        internal static void Scene(NativeInstance[] a, uint[] m, int result)
        { if (!Enabled) return; sceneResult=result; if(result==0) { scene=a; sceneMaterials=m; sceneFrame=Time.frameCount; } }
        internal static void Moving(NativeMoving[] a, uint[] m, uint[] r, int result)
        { if (!Enabled) return; movingResult=result; if(result==0) { movers=a; movingMaterials=m; replaced=r; movingFrame=Time.frameCount; } }
        internal static void Skinned(NativeSkinnedInstance[] a, int result)
        { if (!Enabled) return; skinResult=result; if(result==0) { skins=a ?? new NativeSkinnedInstance[0]; skinFrame=Time.frameCount; } }
        internal static void Decals(NativeDecal[] a, int result)
        { if (!Enabled) return; decalResult=result; if(result==0) { decals=a; decalFrame=Time.frameCount; } }
        internal static void Arm(bool on)
        {
            Enabled=on; scene=null; movers=null; skins=null; decals=null;
            sceneMaterials=null; movingMaterials=null; replaced=null;
            sceneFrame=movingFrame=skinFrame=decalFrame=-1;
            sceneResult=movingResult=skinResult=decalResult=-1;
            decisions=on?new Dictionary<string,(int,string,int)>():null;
        }
        internal static bool Ready() => Enabled && sceneFrame>=0 && movingFrame>=Time.frameCount-2 && skinFrame>=Time.frameCount-2;
        private const BindingFlags Any=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.Static;
        private static object Get(object target, string name)
        {
            if(target==null) return null;
            var type=target as Type ?? target.GetType();
            for(var t=type;t!=null;t=t.BaseType)
            {
                var field=t.GetField(name,Any|BindingFlags.DeclaredOnly);
                if(field!=null) return field.GetValue(target is Type ? null : target);
                var property=t.GetProperty(name,Any|BindingFlags.DeclaredOnly);
                if(property!=null) return property.GetValue(target is Type ? null : target);
            }
            throw new MissingMemberException(type.FullName,name);
        }
        private static object Cache(Type type,string name,object key)
        { var d=Get(type,name) as IDictionary; return d!=null && d.Contains(key) ? d[key] : null; }
        private static JObject Unknown(string rule) => new JObject { ["status"]="unknown",["rule"]=rule,["instances"]=new JArray() };
        private static JObject Excluded(string rule) => new JObject { ["status"]="excluded",["rule"]=rule,["instances"]=new JArray() };
        private static string PathOf(Component c)
        {
            var parts=new List<string>();
            for(var t=c.transform;t!=null;t=t.parent) parts.Add(t.name+"["+t.GetSiblingIndex()+"]");
            parts.Reverse(); return c.gameObject.scene.name+"/"+string.Join("/",parts);
        }
        private static JArray V(Vector3 v) => new JArray(v.x,v.y,v.z);
        private static Bounds TransformBounds(Bounds b,Matrix4x4 m)
        {
            var e=b.extents;
            return new Bounds(m.MultiplyPoint3x4(b.center),2*new Vector3(
                Mathf.Abs(m.m00)*e.x+Mathf.Abs(m.m01)*e.y+Mathf.Abs(m.m02)*e.z,
                Mathf.Abs(m.m10)*e.x+Mathf.Abs(m.m11)*e.y+Mathf.Abs(m.m12)*e.z,
                Mathf.Abs(m.m20)*e.x+Mathf.Abs(m.m21)*e.y+Mathf.Abs(m.m22)*e.z));
        }
        private static bool Finite(float x) => !float.IsNaN(x) && !float.IsInfinity(x);
        // Clip box edges to the near plane before projecting: a corner behind the camera must
        // not invert the rectangle and make a large nearby object disappear from the census.
        private static float[] Project(Camera camera,Bounds bounds,Plane[] planes)
        {
            if(!Finite(bounds.center.x+ bounds.center.y+bounds.center.z+bounds.extents.sqrMagnitude))
                throw new InvalidOperationException("non-finite bounds");
            if(!GeometryUtility.TestPlanesAABB(planes,bounds)) return null;
            var points=new Vector3[8]; var lo=bounds.min; var hi=bounds.max;
            for(int i=0;i<8;++i) points[i]=new Vector3((i&1)==0?lo.x:hi.x,(i&2)==0?lo.y:hi.y,(i&4)==0?lo.z:hi.z);
            float left=1,right=0,top=1,bottom=0; bool any=false;
            void Include(Vector3 p)
            {
                var v=camera.WorldToViewportPoint(p);
                if(v.z<camera.nearClipPlane*0.999f) return;
                left=Mathf.Min(left,v.x); right=Mathf.Max(right,v.x);
                top=Mathf.Min(top,1-v.y); bottom=Mathf.Max(bottom,1-v.y); any=true;
            }
            for(int i=0;i<8;++i)
            {
                Include(points[i]);
                foreach(int bit in new[]{1,2,4}) if((i&bit)==0)
                {
                    var a=points[i]; var b=points[i|bit];
                    float za=Vector3.Dot(a-camera.transform.position,camera.transform.forward)-camera.nearClipPlane;
                    float zb=Vector3.Dot(b-camera.transform.position,camera.transform.forward)-camera.nearClipPlane;
                    if((za<0)!=(zb<0)) Include(Vector3.LerpUnclamped(a,b,za/(za-zb)));
                }
            }
            if(!any) return null;
            return new[]{Mathf.Clamp01(left),Mathf.Clamp01(top),Mathf.Clamp01(right),Mathf.Clamp01(bottom)};
        }
        private sealed class Census
        {
            internal Camera Camera; internal Plane[] Planes; internal JArray Boxes, Rows=new JArray(), Coverage=new JArray();
            internal Dictionary<int,List<Submitted>> Submitted=new Dictionary<int,List<Submitted>>();
            internal Dictionary<int,string> LodSkips=new Dictionary<int,string>();
            internal bool Traced; internal float Bias; internal int MaximumLod;
            internal JObject Row(string id,string name,string path,string type,Bounds bounds,string drawRule)
            {
                var rect=Project(Camera,bounds,Planes); if(rect==null) return null;
                var overlaps=new JArray();
                foreach(JObject box in Boxes)
                {
                    var b=(JArray)box["rect"];
                    float area=Mathf.Max(0,Mathf.Min(rect[2],(float)b[2])-Mathf.Max(rect[0],(float)b[0]))*
                        Mathf.Max(0,Mathf.Min(rect[3],(float)b[3])-Mathf.Max(rect[1],(float)b[1]));
                    if(area>0) overlaps.Add(new JObject { ["box"]=(string)box["name"],["area"]=area });
                }
                if(overlaps.Count==0) return null;
                var row=new JObject { ["id"]=id,["name"]=name,["path"]=path,["type"]=type,
                    ["bounds"]=new JObject { ["center"]=V(bounds.center),["size"]=V(bounds.size) },
                    ["rect"]=new JArray(rect),["overlaps"]=overlaps,
                    ["cameraEligible"]=drawRule.Length==0,["drawRule"]=drawRule,
                    ["mesh"]=JValue.CreateNull(),["triangles"]=JValue.CreateNull(),["materials"]=new JArray(),
                    ["trace"]=Unknown("source has no membership evidence yet") };
                Rows.Add(row); return row;
            }
            internal void Source(string name,Action read)
            {
                int before=Rows.Count;
                try { read(); Coverage.Add(new JObject { ["source"]=name,["ok"]=true,["rows"]=Rows.Count-before }); }
                catch(Exception e) { Coverage.Add(new JObject { ["source"]=name,["ok"]=false,["rows"]=Rows.Count-before,["error"]=e.GetBaseException().Message }); }
            }
        }
        private sealed class Submitted
        { internal string Route; internal int Slot,Frame,Mesh; internal uint MaterialBase,Flags,Layers; internal Matrix4x4 World; internal uint[] Materials; }
        private static Matrix4x4 Matrix(NativeInstance t) => new Matrix4x4(
            new Vector4(t.M00,t.M10,t.M20,0),new Vector4(t.M01,t.M11,t.M21,0),new Vector4(t.M02,t.M12,t.M22,0),new Vector4(t.M03,t.M13,t.M23,1));
        private static bool Same(Matrix4x4 a,Matrix4x4 b)
        { for(int i=0;i<16;++i) if(Mathf.Abs(a[i]-b[i])>0.0001f) return false; return true; }
        private static void Index(Census c)
        {
            void Add(Submitted item)
            { if(!c.Submitted.TryGetValue(item.Mesh,out var list)) c.Submitted[item.Mesh]=list=new List<Submitted>(); list.Add(item); }
            var removed=new HashSet<uint>(replaced ?? new uint[0]);
            if(scene!=null) for(int i=0;i<scene.Length;++i)
            {
                var t=scene[i]; if(removed.Contains((uint)i)) continue;
                Add(new Submitted { Route="SetScene",Slot=i,Frame=sceneFrame,Mesh=t.Mesh,MaterialBase=t.MaterialBase,Flags=t.Flags,Layers=t.RenderingLayerMask,World=Matrix(t),Materials=sceneMaterials });
            }
            if(movers!=null) for(int i=0;i<movers.Length;++i)
            {
                var t=movers[i];
                var n=new NativeInstance { M00=t.M00,M01=t.M01,M02=t.M02,M03=t.M03,M10=t.M10,M11=t.M11,M12=t.M12,M13=t.M13,M20=t.M20,M21=t.M21,M22=t.M22,M23=t.M23 };
                Add(new Submitted { Route="SetMoving",Slot=i,Frame=movingFrame,Mesh=t.Mesh,MaterialBase=t.MaterialBase,Flags=t.Flags,Layers=t.RenderingLayerMask,World=Matrix(n),Materials=movingMaterials });
            }
        }
        private static JObject Membership(Census c,int handle,Matrix4x4 world,int[] submeshes,string absentRule)
        {
            if(!c.Traced) return Unknown("PT is Off; this census records game draw candidates only");
            if(sceneResult!=0 || movingResult!=0 || movingFrame<Time.frameCount-2)
                return Unknown("native scene/moving submission missing, stale or refused; see submissions");
            var matches=new JArray(); bool visible=false,missing=false;
            if(handle>0 && c.Submitted.TryGetValue(handle,out var list)) foreach(var s in list)
            {
                if(!Same(s.World,world)) continue;
                var slots=new JArray();
                foreach(int sub in submeshes)
                {
                    long at=(long)s.MaterialBase+sub;
                    if(sub<0 || s.Materials==null || at>=s.Materials.Length) { missing=true; continue; }
                    uint mat=s.Materials[at]; visible |= mat!=0 && (s.Flags&Tgpt.InstanceHidden)==0;
                    missing |= mat==0 || (s.Flags&Tgpt.InstanceHidden)!=0;
                    slots.Add(new JObject { ["submesh"]=sub,["material"]=mat });
                }
                matches.Add(new JObject { ["route"]=s.Route,["slot"]=s.Slot,["mesh"]=handle,["frame"]=s.Frame,["flags"]=s.Flags,["renderingLayers"]=s.Layers,["submeshes"]=slots });
            }
            if(matches.Count>0) return new JObject { ["status"]=visible ? (missing?"partial":"included") : "excluded",
                ["rule"]=missing?"submitted submesh material 0 (Invisible) or hidden instance":"accepted native submission; shading/alpha/texture correctness not established",["instances"]=matches };
            return absentRule.Length>0 ? Excluded(absentRule) : Unknown("no matching mesh/transform in accepted submissions; no proven rejection rule (scan, LOD or source-resolution gap)");
        }
        private static string MaterialRule(Material m,bool character=false)
        {
            if(m==null) return "";
            if(character)
                return SceneCapture.CharacterLeftOut(m) && !SceneCapture.LayerShell(m) ? "SceneCapture.CharacterLeftOut: transparent/high queue, Unlit/NoRender, ScalpMask or Stubble; not a brow/cornea/shell exception" : "";
            if(DecalCapture.IsMeshMaterial(m)) return "SceneCapture.MaterialIndex: DBuffer mesh goes to DecalCapture (not ordinary geometry)";
            if(m.FindPass("WaterGBuffer")>=0) return "SceneCapture.ReadMaterial: WaterGBuffer goes to WaterCaustics (not ordinary geometry)";
            string shader=m.shader!=null?m.shader.name:"";
            bool unlit=shader.IndexOf("Unlit",StringComparison.OrdinalIgnoreCase)>=0;
            bool noRender=shader.IndexOf("NoRender",StringComparison.OrdinalIgnoreCase)>=0;
            bool transparent=!SceneCapture.LayerBrow(m) && ((m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType")>0.5f)||m.renderQueue>=2750||noRender);
            if(transparent && (unlit || !GlassFinish.IsGlass(m) || noRender))
                return "SceneCapture.ReadMaterial: transparent (_SurfaceType > 0.5 / queue >= 2750 / NoRender), without accepted non-Unlit glass exception";
            return "";
        }
        private static JObject Material(Material m,int slot,bool character=false)
        {
            if(m==null) return new JObject { ["slot"]=slot,["name"]="<no material>",["shader"]="",["queue"]=0,["keywords"]=new JArray(),["rule"]="null uses neutral material" };
            var properties=new JObject();
            foreach(string key in new[]{"_SurfaceType","_AlphaCutoffEnable","_AlphaCutoff","_AlphaClip","_AlphaPower","_AlphaRemap","_CullMode","_CullModeForward","_DoubleSidedEnable","_BlendMode","_ZWrite","_SrcBlend","_DstBlend","_EmissiveIntensity","_EmissiveExposureWeight","_DecalMeshDepthBias","_DecalMeshViewBias"})
                if(m.HasProperty(key)) properties[key]=m.GetFloat(key);
            if(m.shader!=null) for(int i=0;i<m.shader.GetPropertyCount();++i)
            {
                string key=m.shader.GetPropertyName(i);
                if(!m.HasProperty(key)) continue;
                switch(m.shader.GetPropertyType(i))
                {
                    case ShaderPropertyType.Float: case ShaderPropertyType.Range: properties[key]=m.GetFloat(key); break;
                    case ShaderPropertyType.Int: properties[key]=m.GetInteger(key); break;
                    case ShaderPropertyType.Color: var color=m.GetColor(key); properties[key]=new JArray(color.r,color.g,color.b,color.a); break;
                    case ShaderPropertyType.Vector: var v=m.GetVector(key); properties[key]=new JArray(v.x,v.y,v.z,v.w); break;
                }
            }
            var passes=new JArray();
            for(int i=0;i<m.passCount;++i) { string p=m.GetPassName(i); passes.Add(new JObject { ["name"]=p,["enabled"]=m.GetShaderPassEnabled(p) }); }
            var textures=new JArray();
            foreach(string key in m.GetTexturePropertyNames())
            { var t=m.GetTexture(key); if(t!=null) textures.Add(new JObject { ["property"]=key,["id"]=t.GetInstanceID(),["name"]=t.name,["width"]=t.width,["height"]=t.height }); }
            var result=new JObject { ["slot"]=slot,["id"]=m.GetInstanceID(),["name"]=m.name,["shader"]=m.shader!=null?m.shader.name:"<no shader>",
                ["queue"]=m.renderQueue,["keywords"]=new JArray(m.shaderKeywords),["properties"]=properties,["passes"]=passes,["textures"]=textures,["rule"]=MaterialRule(m,character) };
            object index=Cache(typeof(SceneCapture),"materials",m.GetInstanceID());
            result["cachedIndex"]=index==null ? JValue.CreateNull() : JToken.FromObject(index);
            if(index is uint ix && Get(typeof(SceneCapture),"materialList") is IList ml && ix<ml.Count)
            { result["nativeFlags"]=Convert.ToUInt32(Get(ml[(int)ix],"Flags")); result["nativeProgram"]=Convert.ToUInt32(Get(ml[(int)ix],"Program")); }
            return result;
        }
        private static void Describe(Census c,JObject row,Mesh mesh,Material[] materials,Matrix4x4 world,int[] submeshes,string guard="")
        {
            if(row==null) return;
            var mats=new JArray(); if(materials!=null) for(int i=0;i<materials.Length;++i) mats.Add(Material(materials[i],i)); row["materials"]=mats;
            if(mesh==null) { row["trace"]=c.Traced?Excluded(guard.Length>0?guard:"no mesh resolved for this source"):Unknown("PT is Off"); return; }
            row["mesh"]=new JObject { ["id"]=mesh.GetInstanceID(),["name"]=mesh.name,["vertices"]=mesh.vertexCount,["submeshes"]=mesh.subMeshCount };
            long triangles=0; var geometry=new JArray();
            foreach(int sub in submeshes) if(sub>=0 && sub<mesh.subMeshCount)
            { var d=mesh.GetSubMesh(sub); if(d.topology==MeshTopology.Triangles) triangles+=d.indexCount/3; geometry.Add(new JObject { ["submesh"]=sub,["indices"]=d.indexCount,["topology"]=d.topology.ToString() }); }
            row["triangles"]=triangles; row["geometry"]=geometry;
            object cache=Cache(typeof(SceneCapture),"meshes",mesh.GetInstanceID()); int handle=cache==null?0:Convert.ToInt32(Get(cache,"Handle"));
            row["registeredMesh"]=handle;
            string reason=guard;
            if(reason.Length==0 && (materials==null || materials.Length==0)) reason="SceneCapture.AddMesh/PlaceMesh: no shared materials";
            if(reason.Length==0 && materials!=null && materials.Length>0 && materials.All(m=>MaterialRule(m).Length>0)) reason=string.Join("; ",materials.Select(m=>MaterialRule(m)).Distinct());
            if(reason.Length==0 && cache!=null && Convert.ToBoolean(Get(cache,"Failed"))) reason="SceneCapture.RegisterMesh failed (handle "+handle+"): empty/unsupported geometry, missing buffer, native refusal or read exception";
            // Never read the next instance's materials when a static batch member names an
            // invalid submesh. Keep the invalid geometry visible as a diagnostic instead.
            var validSubmeshes=submeshes.Where(s=>s>=0 && s<mesh.subMeshCount).ToArray();
            row["trace"]=Membership(c,handle,world,validSubmeshes,reason);
            if(c.Traced && validSubmeshes.Length!=submeshes.Length)
            {
                row["trace"]["status"]=(string)row["trace"]["status"]=="included"?"partial":(string)row["trace"]["status"];
                row["trace"]["rule"]+="; source names a submesh outside the mesh's range (not read from adjacent native material entries)";
            }
            if(c.Traced && ((string)row["trace"]["status"]=="included" || (string)row["trace"]["status"]=="partial") &&
                geometry.Any(g=>(string)g["topology"]!="Triangles" || (int)g["indices"]<3))
            {
                row["trace"]["status"]=triangles>0?"partial":"excluded";
                row["trace"]["rule"]="GpuScene mesh conversion replaces non-triangle or fewer-than-three-index submeshes with degenerate triangles; inspect geometry entries";
            }
            if(c.Traced && handle==0 && reason.Length==0 && Get(typeof(SceneCapture),"queued") is HashSet<int> queued && queued.Contains(mesh.GetInstanceID()))
                row["trace"]=new JObject { ["status"]="pending",["rule"]="SceneCapture.MeshHandle queued; up to six registrations per frame, followed by another scan",["instances"]=new JArray() };
        }
        internal static string Capture(string boxesJson,bool traced)
        {
            if(!Enabled) throw new InvalidOperationException("missing diagnostic was not armed");
            var cam=Camera.main; if(cam==null) throw new InvalidOperationException("no main camera");
            var c=new Census { Camera=cam,Planes=GeometryUtility.CalculateFrustumPlanes(cam),Boxes=JArray.Parse(boxesJson),Traced=traced,Bias=QualitySettings.lodBias,MaximumLod=QualitySettings.maximumLODLevel };
            // TryGet is internal in the shipped HDRP. GetOrCreate would mutate camera state.
            var tryGet=typeof(HDCamera).GetMethod("TryGet",Any);
            if(tryGet==null) throw new MissingMethodException("HDCamera.TryGet");
            var parameters=tryGet.GetParameters(); var args=new object[parameters.Length]; args[0]=cam;
            for(int i=2;i<args.Length;++i) args[i]=parameters[i].DefaultValue;
            var hd=(bool)tryGet.Invoke(null,args)?args[1] as HDCamera:null;
            if(hd!=null && GraphicsSettings.currentRenderPipeline is HDRenderPipelineAsset asset)
            { c.Bias=hd.frameSettings.GetResolvedLODBias(asset); c.MaximumLod=hd.frameSettings.GetResolvedMaximumLODLevel(asset); }
            Index(c);
            c.Source("Unity renderers",()=>Renderers(c));
            c.Source("Drake and Entities Graphics",()=>Entities(c));
            c.Source("Kandra",()=>Characters(c));
            c.Source("Leshy",()=>Leshy(c));
            c.Source("Vegetation Studio",()=>Vegetation(c));
            c.Source("Medusa",()=>Medusa(c));
            c.Source("Unity terrain, trees and details",()=>Terrains(c));
            c.Source("Decal projectors",()=>Projectors(c));
            c.Source("VFX",()=>Effects(c));
            var doc=new JObject { ["schema"]=1,["frame"]=Time.frameCount,["traced"]=traced,["boxes"]=c.Boxes,["rows"]=c.Rows,["coverage"]=c.Coverage,
                ["camera"]=new JObject { ["position"]=V(cam.transform.position),["rotation"]=V(cam.transform.eulerAngles),["fov"]=cam.fieldOfView,["width"]=cam.pixelWidth,["height"]=cam.pixelHeight,["cullingMask"]=cam.cullingMask,["lodBias"]=c.Bias,["maximumLod"]=c.MaximumLod },
                ["submissions"]=new JObject { ["sceneFrame"]=sceneFrame,["sceneResult"]=sceneResult,["movingFrame"]=movingFrame,["movingResult"]=movingResult,["skinFrame"]=skinFrame,["skinResult"]=skinResult,["decalFrame"]=decalFrame,["decalResult"]=decalResult },
                ["meaning"]="Camera-eligible bounds candidates, including occluded objects. GPU occlusion, alpha and final pixels are not established. Instance slots name accepted SetScene/SetMoving/SetSkinned inputs, not GPU TLAS indices. Unknown is not an exclusion." };
            return doc.ToString(Formatting.Indented);
        }
    }
}
