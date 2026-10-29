// SPDX-License-Identifier: MIT
// PT-63: projector data, never HDRP's DBuffer. All rays read these material changes at their hit.
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;

namespace TGPathTracing
{
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    internal unsafe struct NativeDecal
    {
        internal Vector4 D0, D1, D2, N0, N1, N2;
        internal Vector3 Min; internal uint Flags;
        internal Vector3 Max; internal uint Layers;
        internal Vector4 Color, EmissionBlend, Uv, RemapMin, RemapMax;
        internal int ColorMap, NormalMap, MaskMap, EmissionMap;
        internal Vector4 Fade;
        internal uint Kind, Reserved0, Reserved1, Reserved2;
        internal Vector4 P0, P1, P2, P3;
        internal fixed float Graph[96];
        internal fixed int GraphTextures[8];
        internal Vector4 GraphWorld0, GraphWorld1, GraphWorld2, GraphEnv, GraphEnv2, GraphViewZ;
        internal fixed uint Samplers[12]; internal fixed float SamplerBias[12];
    }

    internal static class DecalCapture
    {
        private const BindingFlags Any = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
        private static readonly Type SystemType = typeof(DecalProjector).Assembly.GetType("UnityEngine.Rendering.HighDefinition.DecalSystem");
        private static readonly PropertyInfo SystemInstance = SystemType?.GetProperty("instance", Any);
        private static readonly PropertyInfo SystemDistance = SystemType?.GetProperty("DrawDistance", Any);
        private static readonly FieldInfo Sets = SystemType?.GetField("m_DecalSets", Any);
        private static readonly PropertyInfo Handle = typeof(DecalProjector).GetProperty("Handle", Any);
        private static readonly FieldInfo HandleIndex = Handle?.PropertyType.GetField("m_Index", Any);
        private static readonly Dictionary<int, int> MaterialOrder = new Dictionary<int, int>();
        private static readonly HashSet<string> Reported = new HashSet<string>();
        private static readonly List<DecalProjector> Sorted = new List<DecalProjector>();
        private static readonly List<NativeDecal> Records = new List<NativeDecal>();
        private static float nextLog;
        internal static bool IsMeshMaterial(Material m) => m != null && (m.FindPass("DBufferMesh") >= 0 || m.FindPass("DecalMeshForwardEmissive") >= 0);
        private sealed class MeshDraw { internal Renderer Renderer; internal Material Material; internal Mesh Mesh; internal int Slot, Geometry; internal Matrix4x4 World; }
        private static readonly List<MeshDraw> MeshDraws = new List<MeshDraw>();
        private static readonly MaterialPropertyBlock RendererProperties = new MaterialPropertyBlock();
        private static readonly MaterialPropertyBlock SlotProperties = new MaterialPropertyBlock();
        private static MaterialPropertyBlock activeProperties;
        internal static float Float(Material m, string name) => activeProperties != null && activeProperties.HasFloat(name) ? activeProperties.GetFloat(name) : m.GetFloat(name);
        internal static Vector4 Vector(Material m, string name) => activeProperties != null && activeProperties.HasVector(name) ? activeProperties.GetVector(name) : m.GetVector(name);
        internal static Color ColorValue(Material m, string name) => activeProperties != null && activeProperties.HasColor(name) ? activeProperties.GetColor(name) : m.GetColor(name);
        internal static Texture TextureValue(Material m, string name) => activeProperties != null && activeProperties.HasTexture(name) ? activeProperties.GetTexture(name) : m.GetTexture(name);
        private static int CompareMeshes(MeshDraw a, MeshDraw b)
        {
            int order = SortingLayer.GetLayerValueFromID(a.Renderer.sortingLayerID).CompareTo(SortingLayer.GetLayerValueFromID(b.Renderer.sortingLayerID));
            if (order != 0) return order;
            order = a.Renderer.sortingOrder.CompareTo(b.Renderer.sortingOrder); if (order != 0) return order;
            order = a.Material.renderQueue.CompareTo(b.Material.renderQueue); if (order != 0) return order;
            order = a.Renderer.rendererPriority.CompareTo(b.Renderer.rendererPriority); if (order != 0) return order;
            order = a.Material.GetInstanceID().CompareTo(b.Material.GetInstanceID()); if (order != 0) return order;
            order = a.Renderer.GetInstanceID().CompareTo(b.Renderer.GetInstanceID());
            return order != 0 ? order : a.Slot.CompareTo(b.Slot);
        }
        private static string CaptureMeshes(HDCamera hd, bool enabled, bool layers, bool perChannel, Dictionary<string,int> shaders)
        {
            MeshDraws.Clear(); int slots = 0, unsupported = 0, waiting = 0, missing = 0, traced = 0, biased = 0;
            var skip = MeshLodSkips(hd.camera);
            foreach (var renderer in UnityEngine.Object.FindObjectsByType<Renderer>(FindObjectsSortMode.None))
            {
                if (renderer == null || !renderer.enabled || renderer.forceRenderingOff || !renderer.gameObject.activeInHierarchy || skip.Contains(renderer.GetInstanceID())) continue;
                if (!enabled || (hd.camera.cullingMask & (1 << renderer.gameObject.layer)) == 0 || renderer.shadowCastingMode == UnityEngine.Rendering.ShadowCastingMode.ShadowsOnly) continue;
                var shared = renderer.sharedMaterials;
                for(int slot=0;slot<shared.Length;++slot)
                {
                    var m = shared[slot]; if (!IsMeshMaterial(m)) continue;
                    ++slots; string shader = m.shader != null ? m.shader.name : "missing shader";
                    shaders[shader] = shaders.TryGetValue(shader,out int count) ? count+1 : 1;
                    var filter = renderer.GetComponent<MeshFilter>();
                    // GPU-generated particle and skinned draws require the pose owners' geometry stream.
                    if (!(renderer is MeshRenderer) || filter == null || filter.sharedMesh == null) { if (MissingWorld.Enabled) MissingWorld.Decision("decal:"+renderer.GetInstanceID()+":"+slot,"DecalCapture: non-MeshRenderer or missing MeshFilter/sharedMesh"); ++unsupported; continue; }
                    var mesh = filter.sharedMesh;
                    int geometry = renderer.isPartOfStaticBatch ? ((MeshRenderer)renderer).subMeshStartIndex+slot : Mathf.Min(slot,mesh.subMeshCount-1);
                    if (geometry<0 || geometry>=mesh.subMeshCount) { if (MissingWorld.Enabled) MissingWorld.Decision("decal:"+renderer.GetInstanceID()+":"+slot,"DecalCapture: submesh outside mesh range"); ++unsupported; continue; }
                    MeshDraws.Add(new MeshDraw { Renderer=renderer, Material=m, Mesh=mesh, Slot=slot, Geometry=geometry, World=renderer.localToWorldMatrix });
                }
            }
            MeshDraws.Sort(CompareMeshes);
            foreach(var draw in MeshDraws)
            {
                int handle = SceneCapture.DecalMeshHandle(draw.Mesh,out int submeshes);
                if(handle<=0) { if (MissingWorld.Enabled) MissingWorld.Decision("decal:"+draw.Renderer.GetInstanceID()+":"+draw.Slot,"DecalCapture: mesh registration returned "+handle); ++waiting; continue; }
                RendererProperties.Clear(); SlotProperties.Clear();
                draw.Renderer.GetPropertyBlock(RendererProperties); draw.Renderer.GetPropertyBlock(SlotProperties,draw.Slot);
                // Unity replaces the renderer-wide block with a per-material block when that block exists.
                activeProperties = !SlotProperties.isEmpty ? SlotProperties : !RendererProperties.isEmpty ? RendererProperties : null;
                try
                {
                    var world=draw.World; var inverse=world.inverse; var bounds=draw.Renderer.bounds;
                    var d = new NativeDecal { D0=inverse.GetRow(0), D1=inverse.GetRow(1), D2=inverse.GetRow(2),
                        GraphWorld0=world.GetRow(0),GraphWorld1=world.GetRow(1),GraphWorld2=world.GetRow(2),
                        Min=bounds.min,Max=bounds.max,Layers=draw.Renderer.renderingLayerMask,
                        Reserved0=(uint)handle,Reserved1=(uint)draw.Geometry, Uv=new Vector4(1,1,0,0),Fade=new Vector4(1,0,0,1),
                        ColorMap=-1,NormalMap=-1,MaskMap=-1,EmissionMap=-1 };
                    if(!ReadMaterial(draw.Material,ref d,world,hd,perChannel,out bool absent)) { if (MissingWorld.Enabled) MissingWorld.Decision("decal:"+draw.Renderer.GetInstanceID()+":"+draw.Slot,"DecalCapture.ReadMaterial: unsupported shader/program"); ++unsupported; continue; }
                    if(absent) { if (MissingWorld.Enabled) MissingWorld.Decision("decal:"+draw.Renderer.GetInstanceID()+":"+draw.Slot,"DecalCapture.ReadMaterial: required shared map missing"); ++missing; continue; }
                    if(!draw.Material.GetShaderPassEnabled("DBufferMesh")) d.Flags &= ~31u;
                    if(!draw.Material.GetShaderPassEnabled("DecalMeshForwardEmissive")) d.Flags &= ~32u;
                    // All 15 shipped mesh decal shaders cull back faces (serialized pass state = 2).
                    d.Flags |= 1024u|2048u; if(!layers) d.Flags |= 512u;
                    d.GraphEnv.w=hd.camera.orthographic ? 1 : 0;
                    if(Value(draw.Material,"_DecalMeshDepthBias",0)!=0 || Value(draw.Material,"_DecalMeshViewBias",0)!=0) ++biased;
                    if (MissingWorld.Enabled) MissingWorld.Decision("decal:"+draw.Renderer.GetInstanceID()+":"+draw.Slot,"",Records.Count);
                    Records.Add(d); ++traced;
                }
                finally { activeProperties=null; }
            }
            return slots+" mesh slots, "+traced+" captured, "+waiting+" awaiting mesh, "+unsupported+" geometry/shader gaps, "+missing+" missing maps, "+biased+" raster-biased";
        }
        private static HashSet<int> MeshLodSkips(Camera camera)
        {
            var skip = new HashSet<int>(); var heights = new List<float>();
            foreach(var group in UnityEngine.Object.FindObjectsByType<LODGroup>(FindObjectsSortMode.None))
            {
                if(group==null || !group.enabled) continue; var lods=group.GetLODs(); var scale=group.transform.lossyScale;
                heights.Clear(); foreach(var lod in lods) heights.Add(lod.screenRelativeTransitionHeight);
                int chosen=WorldRules.GroupLevel(heights,group.size*Mathf.Max(Mathf.Abs(scale.x),Mathf.Max(Mathf.Abs(scale.y),Mathf.Abs(scale.z))),
                    Vector3.Distance(group.transform.TransformPoint(group.localReferencePoint),camera.transform.position),camera.fieldOfView,QualitySettings.lodBias,QualitySettings.maximumLODLevel);
                for(int i=0;i<lods.Length;++i) if(i!=chosen && lods[i].renderers!=null) foreach(var r in lods[i].renderers) if(r!=null) skip.Add(r.GetInstanceID());
                if(chosen>=0 && chosen<lods.Length && lods[chosen].renderers!=null) foreach(var r in lods[chosen].renderers) if(r!=null) skip.Remove(r.GetInstanceID());
            }
            return skip;
        }
        internal static string Summary { get; private set; } = "decals not captured yet";

        private static float Value(Material m, string name, float fallback) => m.HasProperty(name) ? Float(m,name) : fallback;
        private static Color Colour(Material m, string name, Color fallback, bool linear)
        {
            var c = m.HasProperty(name) ? ColorValue(m,name) : fallback;
            return linear ? c.linear : c;
        }
        // Defaults serialized in the 15 shipped DBuffer shaders; no replacement texture is allocated.
        private static int DefaultTexture(Material m, string property)
        {
            switch((m.shader != null ? m.shader.name : "")+"|"+property)
            {
                case "HDRP/Decal|_BaseColorMap": return -2;
                case "HDRP/Decal|_NormalMap": return -4;
                case "HDRP/Decal|_MaskMap": return -2;
                case "HDRP/Decal|_EmissiveColorMap": return -2;
                case "KriptoFX/BFX_Decal|_CutoutTex": return -3;
                case "KriptoFX/BFX_Decal|_MainTex": return -3;
                case "Shader Graphs/Decal_Lit_EmissionPanning|_BaseColor": return -2;
                case "Shader Graphs/Decal_Lit_EmissionPanning|_MaskMap": return -2;
                case "Shader Graphs/Decal_Lit_EmissionPanning|_NormalMap": return -2;
                case "Shader Graphs/Decal_Lit_EmissionPanning|_SubSGThreePannerNoise_feb1bd8b9b4d41879d67ec79749587a9_NoiseTexture_1667152965_Texture2D": return -2;
                case "Shader Graphs/Decal_Lit_EmissionPanning|_SubSGLitBase_69b18b66c8b141c3977c9b36a5457d45_EmissiveColorMap_476838032_Texture2D": return -2;
                case "Shader Graphs/VFXGraph_Decal_Blood|_BaseMap": return -2;
                case "Shader Graphs/VFXGraph_Decal_Blood|_NormalMap": return -2;
                case "TG/Decal/Decal_AlphaErosion|_BaseColor": return -2;
                case "TG/Decal/Decal_AlphaErosion|_NormalMap": return -4;
                case "TG/Decal/Decal_AlphaErosion|_EmissiveMap": return -3;
                case "TG/Decal/Decal_EmberBurning|_SampleTexture2D_6e30d047e7a147d184008ec084421565_Texture_1_Texture2D": return -2;
                case "TG/Decal/Decal_EmberBurning|_SampleTexture2D_b6bddb7ab41a4f3dadf0cf70fc0e8863_Texture_1_Texture2D": return -2;
                case "TG/Decal/Decal_EmberBurning|_Texture2DAsset_c2b280a6867d45dc96f983f5e1dc8f9e_Out_0_Texture2D": return -2;
                case "TG/Decal/Decal_EmberBurning|_SampleTexture2D_86eadc32f6914d33a553c55af3a93f19_Texture_1_Texture2D": return -4;
                case "TG/Decal/Decal_GlowingCracks|_Normal_Map": return -4;
                case "TG/Decal/Decal_GlowingCracks|_Noise_Texture": return -2;
                case "TG/Decal/Decal_GlowingCracks|_Mask": return -3;
                case "TG/Decal/Decal_GlowingCracks|_SampleTexture2D_2996dfed630e40f5a34708da3826001d_Texture_1_Texture2D": return -2;
                case "TG/Decal/Decal_Mask|_ColorTexture": return -2;
                case "TG/Decal/Decal_Mask|_NormalTexture": return -4;
                case "TG/Decal/Decal_Mask|_StepControl": return -2;
                case "TG/Decal/Decal_RadialGradient_3Panning|_Noise": return -2;
                case "TG/Decal/Decal_RadialGradient_3Panning_Lit|_Noise": return -2;
                case "TG/Decal/Decal_RadialGradient_3Panning_Lit|_NormalMap": return -2;
                case "TG/Decal/Decal_Wet|_Mask": return -3;
                case "TG/Decal/Decal_Wet|_Normal_Map": return -2;
                case "TG/Decal/Decal_Wet_Ocean|_Mask": return -3;
                case "TG/Decal/Decal_Wet_Ocean|_SubSGThreePannerNoise_cc9b226949a04694a22cb5c2b7ffd314_NoiseTexture_1667152965_Texture2D": return -2;
                case "TG/Decal/Decal_WyrdGhost|_SampleTexture2D_cd7b6aeaeda64cbca468ed078882de9e_Texture_1_Texture2D": return -2;
                case "TG/Decal/Decal_WyrdGhost|_SampleTexture2D_cbafffec34ad4f0cb877722c4f52962b_Texture_1_Texture2D": return -2;
                case "TG/Decal/Decal_WyrdGhost|_SampleTexture2D_b7052f5c812e4594a7af32fb9247d342_Texture_1_Texture2D": return -4;
                case "TG/Decal/Decal_WyrdGhost|_SampleTexture2D_f9b71e63f7b04bf8a9c18bfa3c518198_Texture_1_Texture2D": return -2;
                case "TG/Decal/Decal_WyrdGhost|_SampleTexture2D_a3a0db7dbbb24affa1c0a8a59615a9dd_Texture_1_Texture2D": return -2;
                case "TG/VFX/VFXGraph_Decal_FireGround|_SampleTexture2D_875ab4a44abc4d3f8c487961576292bb_Texture_1_Texture2D": return -2;
                case "TG/VFX/VFXGraph_Decal_FireGround|_SampleTexture2D_4ae7e257997640b18a82117fde36e339_Texture_1_Texture2D": return -4;
                case "TG/VFX/VFXGraph_Decal_FireGround|_SampleTexture2D_ddb0fda3cfea48e2b3b82f0048ba2c14_Texture_1_Texture2D": return -2;
                case "TG/VFX/VFXGraph_Decal_FireGround|_SampleTexture2D_7d4d73911ba34da98616581247b245b9_Texture_1_Texture2D": return -4;
                case "TG/VFX/VFXGraph_Decal_ForeDwellers_Annihilation|_SampleTexture2D_0afecae06a8f4f68b942b8e8bfeb6486_Texture_1_Texture2D": return -2;
                case "TG/VFX/VFXGraph_Decal_ForeDwellers_Annihilation|_SampleTexture2D_da8a60593f984e2e8b68866ce5573677_Texture_1_Texture2D": return -2;
                case "TG/VFX/VFXGraph_Decal_ForeDwellers_Annihilation|_SampleTexture2D_71cc351a522a4c1ba69c18b07a22ca0d_Texture_1_Texture2D": return -2;
                case "TG/VFX/VFXGraph_Decal_ForeDwellers_Annihilation|_SampleTexture2D_ddcf76b56c734feaa4db66dc04b0e589_Texture_1_Texture2D": return -2;
                default: return -1;
            }
        }
        private static readonly Dictionary<int, (uint index, float bias)> MapSamplers = new Dictionary<int, (uint, float)>();
        private static unsafe void CaptureSamplers(ref NativeDecal d)
        {
            fixed(uint* samplers=d.Samplers) fixed(float* bias=d.SamplerBias) fixed(int* graph=d.GraphTextures)
            {
                int[] maps={d.ColorMap,d.NormalMap,d.MaskMap,d.EmissionMap};
                for(int k=0;k<12;++k)
                {
                    int handle=k<4 ? maps[k] : graph[k-4];
                    if(MapSamplers.TryGetValue(handle,out var state)) { samplers[k]=state.index; bias[k]=state.bias; }
                }
            }
        }
        internal static int Map(Material m, string name, ref bool missing)
        {
            var texture = m.HasProperty(name) ? TextureValue(m,name) : null;
            if (texture == null) return DefaultTexture(m,name);
            if (texture == Texture2D.whiteTexture) return -2;
            if (texture == Texture2D.blackTexture) return -3;
            // Follow mip-streaming replacement of this original resource; make no texture copy.
            TextureShare.Register(texture);
            TextureShare.WatchStandalone(texture);
            int handle = TextureShare.Handle(texture);
            if (handle < 0) missing = true;
            else
            {
                // Unity 6000 Texture.anisoLevel: ForceEnable leaves zero off, otherwise raises 1..8 to 9.
                int aniso=Mathf.Clamp(texture.anisoLevel,0,16);
                if(QualitySettings.anisotropicFiltering==AnisotropicFiltering.Disable) aniso=0;
                else if(QualitySettings.anisotropicFiltering==AnisotropicFiltering.ForceEnable && aniso>0) aniso=Mathf.Max(9,aniso);
                uint index=(uint)(((int)texture.filterMode*17+aniso)*16+(int)texture.wrapModeU*4+(int)texture.wrapModeV);
                MapSamplers[handle]=(index,texture.mipMapBias);
            }
            return handle;
        }
        private static uint Effects(Material m)
        {
            string[] names = { "_AffectAlbedo", "_AffectNormal", "_AffectMetal", "_AffectAO", "_AffectSmoothness", "_AffectEmission" };
            uint flags = 0;
            for (int i = 0; i < names.Length; ++i) if (Value(m, names[i], 0) != 0) flags |= 1u << i;
            return flags;
        }
        private static int Index(DecalProjector p)
        {
            object handle = Handle?.GetValue(p);
            return handle != null && HandleIndex != null ? (int)HandleIndex.GetValue(handle) : p.GetInstanceID();
        }
        private static int Compare(DecalProjector a, DecalProjector b)
        {
            int order = ((int)Value(a.material, "_DrawOrder", 0)).CompareTo((int)Value(b.material, "_DrawOrder", 0));
            if (order != 0) return order;
            int ai = a.material.GetInstanceID(), bi = b.material.GetInstanceID();
            // HDRP inserts each equal-order material set BEFORE the previous set (DecalSystem.CreateDrawData).
            if (ai != bi) return MaterialOrder[bi].CompareTo(MaterialOrder[ai]);
            return Index(a).CompareTo(Index(b));
        }
        private static bool ReadMaterial(Material m, ref NativeDecal d, Matrix4x4 world, HDCamera hd, bool perChannel, out bool missing)
        {
            missing = false;
            string shader = m.shader != null ? m.shader.name : "missing shader";
                bool hdrp = shader == "HDRP/Decal", mask = shader == "TG/Decal/Decal_Mask";
                bool wet = shader == "TG/Decal/Decal_Wet", erosion = shader == "TG/Decal/Decal_AlphaErosion";
                bool ocean = shader == "TG/Decal/Decal_Wet_Ocean", panning = shader == "Shader Graphs/Decal_Lit_EmissionPanning";
                bool blood = shader == "Shader Graphs/VFXGraph_Decal_Blood";
                uint graph = DecalGraphCapture.Kind(shader);
                if (!hdrp && !mask && !wet && !erosion && !ocean && !panning && !blood && graph == 0)
                {
                    if (Reported.Add(shader)) Session.Log.LogWarning("Decal shader not decoded for rays: " + shader + " (" + m.name + ")");
                    return false;
                }
            d.Flags = Effects(m);
            d.Kind = graph != 0 ? graph : mask ? 1u : wet ? 2u : erosion ? 3u : ocean ? 4u : panning ? 5u : blood ? 6u : 0u;
            d.Fade.w = mask || wet ? Value(m, "_Normal_Strength", 1) : erosion || panning || blood ? Value(m, "_NormalStrength", 1) : 1;
                // HDRP's three-target mode only changes smoothness, not the receiver's metal/AO channels.
                if (!perChannel) d.Flags &= ~12u;
                missing = false;
                if (graph != 0)
                {
                    d.EmissionBlend.w = 1;
                    DecalGraphCapture.Read(m, ref d, world, hd, ref missing);
                }
                else if (hdrp)
                {
                    d.Color = Colour(m, "_BaseColor", Color.white, true);
                    d.ColorMap = Map(m, "_BaseColorMap", ref missing);
                    if ((d.Flags & 2) != 0) d.NormalMap = Map(m, "_NormalMap", ref missing);
                    if ((d.Flags & 28) != 0 || Value(m, "_NormalBlendSrc", 0) != 0) d.MaskMap = Map(m, "_MaskMap", ref missing);
                    bool hasMask = m.HasProperty("_MaskMap") && TextureValue(m, "_MaskMap") != null;
                    d.RemapMin = new Vector4(Value(m, hasMask ? "_MetallicRemapMin" : "_Metallic", 0), Value(m, hasMask ? "_AORemapMin" : "_AO", 1),
                        Value(m, hasMask ? "_SmoothnessRemapMin" : "_Smoothness", 0.5f), Value(m, "_DecalMaskMapBlueScale", 1));
                    d.RemapMax = hasMask ? new Vector4(Value(m, "_MetallicRemapMax", 1), Value(m, "_AORemapMax", 1), Value(m, "_SmoothnessRemapMax", 1), 0) : d.RemapMin;
                    if (Value(m, "_NormalBlendSrc", 0) != 0) d.Flags |= 64;
                    if (Value(m, "_MaskBlendSrc", 0) != 0) d.Flags |= 128;
                    var emission = Colour(m, "_EmissiveColor", Color.black, false) * SceneCapture.ExposureScale(Value(m, "_EmissiveExposureWeight", 1));
                    d.EmissionBlend = new Vector4(emission.r, emission.g, emission.b, Value(m, "_DecalBlend", 1));
                    if ((d.Flags & 32) != 0) d.EmissionMap = Map(m, "_EmissiveColorMap", ref missing);
                }
                else if (blood)
                {
                    d.Color = Colour(m, "_Color", Color.white, true); d.EmissionBlend.w = 1;
                    d.ColorMap = Map(m, "_BaseMap", ref missing); d.NormalMap = Map(m, "_NormalMap", ref missing);
                    d.RemapMin = d.RemapMax = new Vector4(0, 1, Value(m, "_Smoothness", 0), 1);
                    d.P0 = new Vector4(Value(m, "_Animator", 1), Value(m, "_ColorIntensity", 1), 0, 0);
                }
                else if (ocean)
                {
                    d.Color = Colour(m, "_Color", Color.white, true); d.EmissionBlend.w = 1;
                    d.ColorMap = Map(m, "_Mask", ref missing);
                    d.EmissionMap = Map(m, "_SubSGThreePannerNoise_cc9b226949a04694a22cb5c2b7ffd314_NoiseTexture_1667152965_Texture2D", ref missing);
                    float speed = Value(m, "_Speed", 0.5f);
                    d.P0 = new Vector4(speed * Time.time, 0, Value(m, "_ColorAffectsNormal", 0.5f), Mathf.Abs(1.5f - speed * Mathf.Sin(Time.time)));
                    d.P1 = Vector(m, "_Tiling");
                    d.RemapMin = d.RemapMax = new Vector4(0, 1, Value(m, "_Smoothness", 1), 1);
                }
                else if (panning)
                {
                    d.Color = Color.white; d.EmissionBlend.w = 1;
                    d.ColorMap = Map(m, "_BaseColor", ref missing); d.NormalMap = Map(m, "_NormalMap", ref missing); d.MaskMap = Map(m, "_MaskMap", ref missing);
                    d.EmissionMap = Map(m, "_SubSGThreePannerNoise_feb1bd8b9b4d41879d67ec79749587a9_NoiseTexture_1667152965_Texture2D", ref missing);
                    var speed = Vector(m, "_Speed");
                    d.P0 = new Vector4(speed.x * Time.time, speed.y * Time.time, Value(m, "_NoiseMultiply", 8), Value(m, "_Max", 0.31f));
                    var tint = Colour(m, "_Color", Color.white, true);
                    d.P1 = new Vector4(tint.r, tint.g, tint.b, Value(m, "_Alpha", 0.5f));
                    d.RemapMin = d.RemapMax = new Vector4(0, 1, Value(m, "_Smoothness", 0.9f), 1);
                }
                else if (wet)
                {
                    d.Color = Color.white; d.EmissionBlend.w = 1;
                    d.NormalMap = Map(m, "_Normal_Map", ref missing);
                    d.RemapMin = d.RemapMax = new Vector4(0, 1, Value(m, "_Smoothness", 1), 1);
                    d.P0 = new Vector4(Value(m, "_Mask_Roundness", 1000), Value(m, "_Noise_Scale", 20), Value(m, "_Noise_Power", 10), Value(m, "_Noise_Multiply", 1000));
                    var pan = Vector(m, "_Normal_Panner");
                    d.P1 = new Vector4(pan.x * Time.time, pan.y * Time.time, Value(m, "_Normal_Tiling", 1), 0);
                }
                else if (erosion)
                {
                    d.Color = Colour(m, "_Tint", Color.white, true); d.EmissionBlend.w = 1;
                    d.ColorMap = Map(m, "_BaseColor", ref missing); d.NormalMap = Map(m, "_NormalMap", ref missing);
                    d.RemapMin = d.RemapMax = new Vector4(0, 1, Value(m, "_Smoothness", 0.5f), 1);
                    d.P0 = new Vector4(Value(m, "_AlphaErosionIntensity", 0), Value(m, "_AlphaErosionFeather", 0.2f), 0, 0);
                    // The shipped graph feeds its exposure value into the emission weight as well.
                    float inverseExposure = SceneCapture.ExposureScale(0);
                    var emission = Colour(m, "_EmissiveColor", Color.white, true) * (inverseExposure - 1 + 1 / Mathf.Max(inverseExposure, 1e-12f));
                    d.EmissionBlend = new Vector4(emission.r, emission.g, emission.b, 1);
                    d.EmissionMap = Map(m, "_EmissiveMap", ref missing);
                }
                else
                {
                    d.Color = Colour(m, "_Color", Color.white, true);
                    d.ColorMap = Map(m, "_ColorTexture", ref missing); d.NormalMap = Map(m, "_NormalTexture", ref missing); d.MaskMap = Map(m, "_StepControl", ref missing);
                    d.RemapMin = d.RemapMax = new Vector4(0, 1, Value(m, "_Smoothness", 0.9f), 1);
                    d.EmissionBlend.w = 1;
                    d.P0 = new Vector4(Value(m, "_StepAmount", 1), Value(m, "_StepEdge", 0.1f), Value(m, "_Alpha", 1), 0);
                }
            CaptureSamplers(ref d);
            return true;
        }

        internal static void Capture(HDCamera hd)
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            var timer = Stopwatch.StartNew();
            var camera = hd.camera;
            Sorted.Clear(); Records.Clear(); MaterialOrder.Clear(); MapSamplers.Clear();
            object system = SystemInstance?.GetValue(null);
            var sets = system != null ? Sets?.GetValue(system) as IDictionary : null;
            if (sets != null) foreach (DictionaryEntry pair in sets) MaterialOrder[(int)pair.Key] = MaterialOrder.Count;
            float distance = system != null && SystemDistance != null ? Convert.ToSingle(SystemDistance.GetValue(system)) : float.PositiveInfinity;
            int all = 0, unsupported = 0, missingMaps = 0, glowing = 0, faded = 0;
            // The game's own decal settings for the camera, not HDRP's this frame: while tracing HDRP draws no decal (GamePasses,
            // PT-112 A), and the traced ones stay (round 19: following HDRP's, the tracer left every decal out).
            bool layers = GamePasses.GameDecalLayers;
            bool enabled = GamePasses.GameDecals;
            var asset = UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline as HDRenderPipelineAsset;
            bool perChannel = asset != null && asset.currentPlatformRenderPipelineSettings.decalSettings.perChannelMask;
            var shaders = new Dictionary<string, int>();
            string meshSummary=CaptureMeshes(hd, enabled, layers, perChannel, shaders);
            int meshRecords=Records.Count;
            foreach (var p in UnityEngine.Object.FindObjectsByType<DecalProjector>(FindObjectsSortMode.None))
            {
                if (p == null || !p.isActiveAndEnabled || p.material == null) continue;
                ++all;
                if (!enabled || (camera.cullingMask & (1 << p.gameObject.layer)) == 0) continue;
                int id = p.material.GetInstanceID();
                if (!MaterialOrder.ContainsKey(id)) MaterialOrder[id] = MaterialOrder.Count;
                Sorted.Add(p);
            }
            Sorted.Sort(Compare);
            foreach (var p in Sorted)
            {
                var m = p.material;
                string shader = m.shader != null ? m.shader.name : "missing shader";
                shaders[shader] = shaders.TryGetValue(shader, out int n) ? n + 1 : 1;
                var scale = p.scaleMode == DecalScaleMode.InheritFromHierarchy ? p.transform.lossyScale : Vector3.one;
                var rotation = p.transform.rotation * Quaternion.Euler(scale.z >= 0 ? -90 : 90, 0, 0);
                var sizeScale = scale;
                if (scale.z < 0) sizeScale.y *= -1;
                if ((sizeScale.x < 0) ^ (sizeScale.y < 0) ^ (sizeScale.z < 0)) sizeScale.z *= -1;
                var size = new Vector3(p.size.x * sizeScale.x, p.size.z * sizeScale.z, p.size.y * sizeScale.y);
                if (size.x == 0 || size.y == 0 || size.z == 0) { if (MissingWorld.Enabled) MissingWorld.Decision("projector:"+p.GetInstanceID(),"DecalCapture: zero-size projector"); ++faded; continue; }
                var offsetScale = scale;
                if (scale.z < 0) { offsetScale.y *= -1; offsetScale.z *= -1; }
                var pivot = new Vector3(p.pivot.x * offsetScale.x, -p.pivot.z * offsetScale.z, p.pivot.y * offsetScale.y);
                var basis = Matrix4x4.TRS(p.transform.position, rotation, Vector3.one);
                var world = basis * Matrix4x4.TRS(pivot, Quaternion.identity, size);
                var inverse = world.inverse;
                Vector3 centre = world.MultiplyPoint3x4(Vector3.zero);
                var extent = new Vector3(Mathf.Abs(world.m00) + Mathf.Abs(world.m01) + Mathf.Abs(world.m02),
                    Mathf.Abs(world.m10) + Mathf.Abs(world.m11) + Mathf.Abs(world.m12), Mathf.Abs(world.m20) + Mathf.Abs(world.m21) + Mathf.Abs(world.m22)) * 0.5f;
                // HDRP's sphere radius is the diagonal of its oriented box, not the larger world AABB.
                float radius = size.magnitude * 0.5f;
                float reach = Mathf.Min(distance, p.drawDistance) + radius;
                float away = Vector3.Distance(camera.transform.position, centre);
                float fade = away < reach ? (p.fadeScale >= 1 ? 1 : Mathf.Clamp01((reach - away) / (reach * (1 - p.fadeScale)))) : 0;
                fade *= p.fadeFactor;
                if (!(fade > 0)) { if (MissingWorld.Enabled) MissingWorld.Decision("projector:"+p.GetInstanceID(),"DecalCapture: distance/fade <= 0 (distance "+away+", reach "+reach+", fade "+fade+")"); ++faded; continue; }
                float angleScale = 0, angleBias = 0;
                if (layers && p.startAngleFade < 180)
                {
                    float start = p.startAngleFade / 180, end = p.endAngleFade / 180;
                    float span = Mathf.Max(end - start, 0.0001f);
                    angleScale = (2f / 9f) / span; angleBias = (end - 0.5f) / span;
                }
                var normal = basis;
                normal.SetColumn(1, basis.GetColumn(2)); normal.SetColumn(2, basis.GetColumn(1));
                var d = new NativeDecal
                {
                    D0 = inverse.GetRow(0), D1 = inverse.GetRow(1), D2 = inverse.GetRow(2),
                    N0 = normal.GetRow(0), N1 = normal.GetRow(1), N2 = normal.GetRow(2),
                    Min = centre - extent, Max = centre + extent, Layers = layers ? (uint)p.decalLayerMask : uint.MaxValue,
                    Flags = 0, Kind = 0,
                    Uv = new Vector4(p.uvScale.x, p.uvScale.y, p.uvBias.x, p.uvBias.y),
                    Fade = new Vector4(fade, angleScale, angleBias, 1),
                    GraphWorld0 = world.GetRow(0), GraphWorld1 = world.GetRow(1), GraphWorld2 = world.GetRow(2),
                    ColorMap = -1, NormalMap = -1, MaskMap = -1, EmissionMap = -1
                };
                if (!ReadMaterial(m, ref d, world, hd, perChannel, out bool missing)) { if (MissingWorld.Enabled) MissingWorld.Decision("projector:"+p.GetInstanceID(),"DecalCapture.ReadMaterial: unsupported shader/program"); ++unsupported; continue; }
                if (!m.GetShaderPassEnabled("DBufferProjector")) d.Flags &= ~31u;
                if (!m.GetShaderPassEnabled("DecalProjectorForwardEmissive")) d.Flags &= ~32u;
                if (p.affectsTransparency) d.Flags |= 256;
                if (!layers) d.Flags |= 512;
                if (missing) { if (MissingWorld.Enabled) MissingWorld.Decision("projector:"+p.GetInstanceID(),"DecalCapture.ReadMaterial: required shared map missing"); ++missingMaps; continue; }
                if ((d.Flags & 32) != 0) ++glowing;
                if (MissingWorld.Enabled) MissingWorld.Decision("projector:"+p.GetInstanceID(),"",Records.Count);
                Records.Add(d);
            }
            var sent = Records.ToArray();
            int result = tracer.SetDecals(sent);
            timer.Stop();
            Summary = all + " projectors, " + (sent.Length-meshRecords) + " captured, " + glowing + " glowing, " + faded + " faded, " + unsupported + " shader gaps, " + missingMaps + " missing shared maps, " + (perChannel ? "4RT" : "3RT") + "; capture " + timer.Elapsed.TotalMilliseconds.ToString("0.00") + " ms";
            if (result != 0) Summary += "; native refused " + result;
            if (Time.unscaledTime >= nextLog)
            {
                nextLog = Time.unscaledTime + 10;
                Session.Log.LogInfo("Decals: " + Summary);
                Session.Log.LogInfo("Decal meshes: " + meshSummary);
                foreach (var pair in shaders) Session.Log.LogInfo("  decal shader " + pair.Key + ": " + pair.Value);
            }
        }
    }
}
