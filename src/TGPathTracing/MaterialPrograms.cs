// SPDX-License-Identifier: MIT
// PT-64: the game's own shaders at a hit. Since the tracer's own camera rays make every pixel's surface, what the player
// sees of a surface is what a hit reads of its material. Read HDRP Lit's way alone, the game's rock was one stretched pale
// texture, its grass olive, its ground flat grey and its furniture too bright (the yardstick, 2026-10-07). So for each of
// the game's Shader Graphs that draws the world this builds a record of the material's own textures and values, and the
// tracer computes what the shader's GBuffer pass computes from them (pt_materials.hlsli, written from the game's compiled
// code: docs/agents/materials.md). The values are read by the properties' own names; a color the shader takes as a
// Color is sent linear, a Vector as it is.
using System;
using System.Collections.Generic;
using UnityEngine;
namespace TGPathTracing
{
    internal static class MaterialPrograms
    {
        internal const uint Lit = 0, Detail = 1, Architecture = 2, Cliff = 3, Grass = 4, Leaves = 5, Trunk = 6, Terrain = 7, Triplanar = 8, Skin = 9;
        internal const string ArchitectureShader = "TG/Architecture/KF_Architecture", CliffShader = "TG/Environment/Cliffs/Cliff";
        internal const string GrassShader = "TG/Vegetation/Vegetation_Grass", LeavesShader = "TG/Vegetation/Vegetation_Leaves";
        internal const string TrunkShader = "TG/Vegetation/Vegetation_Trunk", TerrainShader = "TG/Environment/MeshTerrain";
        internal const string TriplanarShader = "TG/Architecture/TriplanarArchitecture2";
        // The records, four words a row (TGPT_SetMaterialPrograms), and how many rows the tracer has.
        private static readonly List<uint> words = new List<uint>();
        private static int sentRows;
        private static readonly HashSet<int> changedRows = new HashSet<int>();
        private static readonly int[] built = new int[10];
        private static readonly HashSet<string> logged = new HashSet<string>();
        // For the log: the textures the records name, those the tracer had not open when asked (their program waits for them:
        // TextureShare's line says why a texture has no number), and the count the last line was written at.
        private static int texturesNamed, texturesWaiting, summarised;
        // PT-104 (docs/agents/terrain-1.md, rule-1.md), for the log: what each traced mesh brings of the two things a normal
        // map can be turned by (1 texture coordinates, 2 tangents), with its name; the pairs of terrain material and kind
        // of mesh the log has named; and what is drawn flat (FlatDrawn): this scan's instances that hold a material with a
        // normal map on a mesh without texture coordinates, those materials each with one such mesh, and the materials
        // the log has named.
        private static readonly Dictionary<int, int> meshBrings = new Dictionary<int, int>();
        private static readonly Dictionary<int, string> meshLabels = new Dictionary<int, string>();
        private static readonly HashSet<long> terrainSaid = new HashSet<long>();
        private static int flatInstances;
        private static readonly Dictionary<uint, int> flatMaterials = new Dictionary<uint, int>();
        private static readonly HashSet<uint> flatSaid = new HashSet<uint>();

        internal static void Reset()
        {
            words.Clear(); sentRows = 0; changedRows.Clear(); Array.Clear(built, 0, built.Length); logged.Clear();
            texturesNamed = texturesWaiting = summarised = 0;
            meshBrings.Clear(); meshLabels.Clear(); terrainSaid.Clear(); flatInstances = 0; flatMaterials.Clear(); flatSaid.Clear();
            var tracer = Session.Tracer;
            if (tracer != null && tracer.Connected) tracer.SetMaterialPrograms(0, null, 0);
        }
        // New records, and rows a texture made anew wrote its handle into, go to the tracer.
        internal static void Flush(NativeTracer tracer)
        {
            if (tracer == null || !tracer.Connected) return;
            int rows = words.Count / 4;
            if (rows > sentRows)
            {
                var added = words.GetRange(sentRows * 4, (rows - sentRows) * 4).ToArray();
                if (tracer.SetMaterialPrograms((uint)sentRows, added, (uint)(rows - sentRows)) == 0) sentRows = rows;
            }
            if (rows != summarised)
            {
                summarised = rows;
                Session.Log.LogInfo("Materials computed as the game's shaders compute them (PT-64): " + Summary() + "; " + texturesNamed + " textures named, " + texturesWaiting +
                    " of them not open when asked (a material whose program waits for one is read HDRP Lit's way until it is)");
            }
            if (changedRows.Count == 0) return;
            foreach (int row in changedRows)
                if (row < sentRows) tracer.SetMaterialPrograms((uint)row, words.GetRange(row * 4, 4).ToArray(), 1);
            changedRows.Clear();
        }
        // TextureShare: the texture a record's word names was made anew (mip streaming) or opened later.
        internal static void SetWord(int word, int handle)
        {
            if (word < 0 || word >= words.Count) return;
            words[word] = unchecked((uint)handle);
            changedRows.Add(word / 4);
        }
        internal static string Summary()
        {
            return "detail " + built[Detail] + ", architecture " + built[Architecture] + ", cliff " + built[Cliff] + ", grass " + built[Grass] + ", leaves " + built[Leaves] +
                ", trunk " + built[Trunk] + ", terrain " + built[Terrain] + ", triplanar " + built[Triplanar] + ", skin " + built[Skin] + " (" + (words.Count / 4) + " rows)";
        }

        // SceneCapture, of a mesh it registered: what it brings of the two things a normal map can be turned by.
        internal static void Registered(int handle, Mesh mesh)
        {
            if (handle <= 0 || mesh == null) return;
            meshBrings[handle] = (mesh.HasVertexAttribute(UnityEngine.Rendering.VertexAttribute.TexCoord0) ? 1 : 0) |
                (mesh.HasVertexAttribute(UnityEngine.Rendering.VertexAttribute.Tangent) ? 2 : 0);
            meshLabels[handle] = "\"" + mesh.name + "\" (" + mesh.vertexCount + " vertices)";
        }
        internal static void Released(int handle) { meshBrings.Remove(handle); meshLabels.Remove(handle); }
        // SceneCapture, of an instance whose material runs the terrain's program: the log names the first mesh of each kind
        // a terrain material is drawn with, and what the game's shader and the tracer turn its bumps by there. The tracer
        // turns every ground's bumps by the object's x axis with the sign +1 (pt_materials program_terrain: what Unity
        // hands the game's shader for a mesh without tangents; the game, run 20261010-092027). NOT BUILT: the sign by the
        // mesh, for the ground meshes that bring tangents of their own (in the game's files all (1, 0, 0, -1): the same
        // axis, and by the game's rule, the bitangent times the tangent's w, the other sign).
        internal static void TerrainDrawn(int handle, uint material)
        {
            if (!meshBrings.TryGetValue(handle, out int brings)) brings = 4;
            if (!terrainSaid.Add(((long)material << 3) | (long)brings)) return;
            string mesh = meshLabels.TryGetValue(handle, out var label) ? label : "#" + handle;
            string what = brings == 4 ? "of which the plugin has not noted what it brings" :
                "which brings " + ((brings & 1) != 0 ? "texture coordinates" : "no texture coordinates") + " and " + ((brings & 2) != 0 ? "tangents of its own" : "no tangents");
            string game = brings == 4 ? "" : (brings & 2) != 0
                ? ": the game's shader turns its layers' bumps by those tangents, which the path tracer does not read"
                : ": the game's shader turns its layers' bumps by the tangent Unity hands it for such a mesh (the object's x axis)";
            string traced = brings != 4 && (brings & 2) != 0
                ? "the path tracer by the object's x axis with the sign +1, whatever those tangents hold (in the game's files they are (1, 0, 0, -1): the same axis and, by the game's rule, the other sign; the sign by the mesh is not built)"
                : "the path tracer by the object's x axis too, with the sign +1";
            Session.Log.LogInfo("Terrain material " + SceneCapture.MaterialLabel((int)material) + " is drawn with mesh " + mesh + ", " + what + game + "; " + traced + ".");
        }
        // PT-104, what is still drawn flat, said and not changed. The tracer turns a normal map by a frame it makes of the
        // mesh's texture coordinates (pt_materials hit_frame). A mesh that brings none has no frame, and frame_normal then
        // answers the flat normal without a word: for HDRP Lit's way and for every program but the terrain's. SceneCapture
        // tells this of every instance a scan lists: whether its mesh is known to bring no texture coordinates
        // (NoCoordinates, once an instance), each such instance's material that reads a normal map (FlatDrawn: its own
        // normal map open to the tracer, or a program whose normal maps are in its record), and the instance itself
        // (FlatInstance). The scan's end writes one line when it met a material the log has not named yet.
        internal static void ScanBegins() { flatInstances = 0; flatMaterials.Clear(); }
        internal static bool NoCoordinates(int handle) => meshBrings.TryGetValue(handle, out int brings) && (brings & 1) == 0;
        internal static bool FlatDrawn(int handle, uint material, uint program, int normalTexture)
        {
            if (program == Terrain || !(normalTexture >= 0 || program == Triplanar || program == Skin)) return false;
            if (!flatMaterials.ContainsKey(material)) flatMaterials[material] = handle;
            return true;
        }
        internal static void FlatInstance() { ++flatInstances; }
        internal static void ScanEnds()
        {
            bool news = false;
            foreach (uint material in flatMaterials.Keys) news |= flatSaid.Add(material);
            if (!news) return;
            var names = new System.Text.StringBuilder();
            int named = 0;
            foreach (var pair in flatMaterials)
            {
                if (named == 12) { names.Append(", and ").Append(flatMaterials.Count - named).Append(" more"); break; }
                names.Append(named++ == 0 ? "" : ", ").Append(SceneCapture.MaterialLabel((int)pair.Key)).Append(" on mesh ")
                    .Append(meshLabels.TryGetValue(pair.Value, out var label) ? label : "#" + pair.Value);
            }
            Session.Log.LogInfo(flatInstances + " instances of " + flatMaterials.Count + " materials have a normal map and a mesh without texture coordinates: drawn flat " +
                "(the path tracer makes a normal map's frame of the mesh's texture coordinates, and has none there): " + names + ".");
        }

        private static float F(Material m, string name, float fallback = 0f) => m.HasProperty(name) ? m.GetFloat(name) : fallback;
        private static Vector4 V(Material m, string name) => m.HasProperty(name) ? m.GetVector(name) : Vector4.zero;
        private static Vector3 C(Material m, string name)
        {
            if (!m.HasProperty(name)) return Vector3.one;
            var l = m.GetColor(name).linear;
            return new Vector3(l.r, l.g, l.b);
        }
        private static Texture T(Material m, string name) => m.HasProperty(name) ? m.GetTexture(name) : null;
        private static Vector4 V4(Vector3 v, float w) => new Vector4(v.x, v.y, v.z, w);
        private static uint Bits(float f) => unchecked((uint)BitConverter.ToInt32(BitConverter.GetBytes(f), 0));

        // A record: its first row, its textures' handles (each watched by TextureShare, which writes a new handle into the
        // word when the texture is made anew), its values. also: textures the program reads through the material's own
        // fields, which hold the program back until the tracer has them open too. Its first row's number.
        private static int Record(uint program, Texture[] textures, Texture[] also, Vector4[] values)
        {
            int row = words.Count / 4;
            int count = textures.Length + also.Length;
            uint present = 0;
            for (int i = 0; i < count; ++i) if ((i < textures.Length ? textures[i] : also[i - textures.Length]) is Texture2D) present |= 1u << i;
            words.Add(program); words.Add((uint)count); words.Add((uint)values.Length); words.Add(present);
            int first = words.Count;
            for (int i = 0; i < ((count + 3) / 4) * 4; ++i) words.Add(0xFFFFFFFFu);
            for (int i = 0; i < count; ++i)
            {
                var t = i < textures.Length ? textures[i] : also[i - textures.Length];
                if (!(t is Texture2D)) continue;
                int handle = TextureShare.Handle(t, TextureShare.FieldProgram + first + i);
                words[first + i] = unchecked((uint)handle);
                ++texturesNamed; if (handle < 0) ++texturesWaiting;
            }
            foreach (var v in values) { words.Add(Bits(v.x)); words.Add(Bits(v.y)); words.Add(Bits(v.z)); words.Add(Bits(v.w)); }
            ++built[program];
            return row;
        }
        private static void Use(ref NativeMaterial mat, uint program, int row) { mat.Program = program; mat.ProgramRow = (uint)row; }
        // The material's own three maps as the tracer's fields name them (set by the caller before): their textures.
        private static Texture[] Own(Material m, string color, string normal, string mask) => new[] { T(m, color), T(m, normal), T(m, mask) };

        // The material's own color map, normal map and mask map into the tracer's fields, as the game's graphs read them.
        private static void Main(Material m, ref NativeMaterial mat, string color, string normal, string normalScale, string mask, Vector3 tint, Vector4 st)
        {
            mat.BaseR = tint.x; mat.BaseG = tint.y; mat.BaseB = tint.z;
            mat.ScaleX = st.x; mat.ScaleY = st.y; mat.OffsetX = st.z; mat.OffsetY = st.w;
            var c = T(m, color);
            mat.ColorTexture = c != null ? TextureShare.Handle(c, TextureShare.FieldColor) : -1;
            var n = T(m, normal);
            mat.NormalTexture = n != null ? TextureShare.Handle(n, TextureShare.FieldNormal) : -1;
            mat.NormalScale = normalScale != null ? F(m, normalScale, 1f) : 1f;
            mat.TextureFlags |= Tgpt.TextureGraphNormal;
            var k = T(m, mask);
            bool laidOut = mat.MaskTexture >= 0 || (mat.Flags & Tgpt.MaterialMaskTile) != 0; // SceneCapture.ReadFinish knew its layout
            mat.MaskTexture = k != null ? TextureShare.Handle(k, TextureShare.FieldMask) : -1;
            if (!laidOut)
            {
                // Until its program runs: no metal and its smoothness constant (the remap's ends both at it), no occlusion.
                uint smooth = (uint)Mathf.RoundToInt(Mathf.Clamp01(mat.Smoothness) * 255f);
                mat.MaskRemap = (smooth << 16) | (smooth << 24);
                mat.TextureFlags &= ~(Tgpt.TextureMaskOcclusion | 15u);
            }
        }

        // The material's program, where its shader has one: mat's own fields set as the program reads them, its record built.
        // What the log says of it, or null where the shader has none (HDRP Lit's way then).
        internal static string Build(Material m, string shaderName, ref NativeMaterial mat)
        {
            string what = null;
            switch (shaderName)
            {
                case ArchitectureShader:
                {
                    float tiling = F(m, "_Base_Tiling", 1f);
                    Main(m, ref mat, "_Base_BaseColor", "_Base_NormalMap", "_Base_NormalStrenght", "_Base_MaskMap", C(m, "_Base_Tint"), new Vector4(tiling, tiling, 0f, 0f));
                    var slope = V(m, "_Overlay_Slope"); var offset = V(m, "_Overlay_Mask_Offset");
                    var values = new[]
                    {
                        V4(C(m, "_Overlay_Tint"), F(m, "_Overlay_Smoothness", 1f)),
                        new Vector4(F(m, "_Base_NormalStrenght", 1f), F(m, "_Overlay_NormalStrenght", 1f), F(m, "_Overlay_Tiling", 1f), F(m, "_MicroDetail_Tiling", 1f)),
                        new Vector4(slope.x, slope.y, F(m, "_BottomHeight"), F(m, "_MicroDetail_NormalStrength")),
                        new Vector4(F(m, "_Overlay_Mask_Tiling", 1f), offset.x, offset.y, F(m, "_Base_Smoothness", 1f)),
                        new Vector4(F(m, "_Overlay"), F(m, "_Overlay_Mask_Texture"), F(m, "_Base_AddOcclusionMask"), F(m, "_DistanceBasedSmoothness")),
                        new Vector4(F(m, "_Base_OcclusionMultiply"), 0f, 0f, 0f),
                    };
                    var textures = new[] { T(m, "_Overlay_BaseColor"), T(m, "_Overlay_NormalMap"), T(m, "_Overlay_MaskMap"),
                        T(m, "_Texture2DAsset_7356ca227c394173bc0cf696a6955ebb_Out_0_Texture2D"), T(m, "_Texture2DAsset_8a2048f5460c4e8a86b4a96f0f56ea4c_Out_0_Texture2D") };
                    Use(ref mat, Architecture, Record(Architecture, textures, Own(m, "_Base_BaseColor", "_Base_NormalMap", "_Base_MaskMap"), values));
                    what = "its tint, normal map, " + (F(m, "_Overlay") != 0f ? "overlay layer, " : "") + "micro detail and hue";
                    if (m.IsKeywordEnabled("_PARALLAX_OCCLUSION_MAPPING")) what += "; its parallax relief is not traced";
                    if (T(m, "_EmissionMap") != null) what += "; its glow (_EmissionMap) is not traced";
                    break;
                }
                case CliffShader:
                {
                    Main(m, ref mat, "_L0_BaseMap", "_L0_NormalMap", "_L0_NormalMapSterngth", "_L0_MaskMap", C(m, "_L0_Tint"), new Vector4(1f, 1f, 0f, 0f));
                    var fuzz = V(m, "_Vector3");
                    var values = new[]
                    {
                        new Vector4(F(m, "_L0_NormalMapSterngth", 1f), F(m, "_L0_Curvature"), F(m, "_L0_OcclusionToBaseColor"), F(m, "_L0_OcclusionBasedSmoothness")),
                        V4(C(m, "_L1_Tint"), F(m, "_L1_NormalMapSterngth", 1f)),
                        V4(C(m, "_L2_Tint"), F(m, "_L2_NormalMapSterngth", 1f)),
                        new Vector4(F(m, "_L1_Tiling", 1f), F(m, "_L2_Tiling", 1f), F(m, "_L2_Coverage"), F(m, "_L2_OcclusionBasedMask")),
                        new Vector4(F(m, "_L0_SmoothnessMin"), F(m, "_L0_SmoothnessMax", 1f), F(m, "_L1_SmoothnessMin"), F(m, "_L1_SmoothnessMax", 1f)),
                        new Vector4(F(m, "_L2_SmoothnessMin"), F(m, "_L2_SmoothnessMax", 1f), F(m, "_L2_WSTextureMaskTiling"), F(m, "_L2_WSTextureMaskInfluence")),
                        new Vector4(fuzz.x, fuzz.y, fuzz.z, F(m, "_Fuzziness")),
                        new Vector4(F(m, "_L2_BottomLayersNormalInfluence"), F(m, "_L0_CliffFormationMaskMap"), m.IsKeywordEnabled("_L2") ? 1f : 0f, 0f),
                    };
                    var textures = new[] { T(m, "_L1_BaseColor"), T(m, "_L1_NormalMap"), T(m, "_L2_BaseColor"), T(m, "_L2_NormalMap"), T(m, "_L2_WSTextureMask") };
                    Use(ref mat, Cliff, Record(Cliff, textures, Own(m, "_L0_BaseMap", "_L0_NormalMap", "_L0_MaskMap"), values));
                    what = "its three layers (the formation's own, the rock up close" + (m.IsKeywordEnabled("_L2") ? ", moss" : "") + ")";
                    if (m.IsKeywordEnabled("_EMISSIVE")) what += "; its glow (_EmissiveTex) is not traced";
                    break;
                }
                case GrassShader:
                case LeavesShader:
                {
                    bool leaves = shaderName == LeavesShader;
                    string color = leaves ? "_BaseColorMap" : "_BaseMap";
                    var tiling = V(m, "_Tiling");
                    if (!m.HasProperty("_Tiling")) tiling = new Vector4(1f, 1f, 0f, 0f);
                    // Its own fields: the map, tinted for HDRP Lit's way by the mean of its two colors until the program's
                    // textures are open (the base color SceneCapture gave it stays).
                    var c = T(m, color);
                    mat.ColorTexture = c != null ? TextureShare.Handle(c, TextureShare.FieldColor) : -1;
                    mat.ScaleX = tiling.x; mat.ScaleY = tiling.y; mat.OffsetX = 0f; mat.OffsetY = 0f;
                    var n = T(m, "_NormalMap");
                    mat.NormalTexture = n != null ? TextureShare.Handle(n, TextureShare.FieldNormal) : -1;
                    mat.NormalScale = F(m, "_NormalScale", 1f);
                    mat.TextureFlags |= Tgpt.TextureGraphNormal;
                    var noise = T(m, leaves ? "_Triplanar_aaed0fa9fb4d47aa961e3d87b0791a9e_Texture_1_Texture2D" : "_Texture2DAsset_b0f3eb0864144cf3a2b0f146160c9dd9_Out_0_Texture2D");
                    var values = leaves
                        ? new[] { V4(C(m, "_HealthyColor"), F(m, "_ColorVariationScale", 1f)), V4(C(m, "_DryColor"), 0f) }
                        : new[] { V4(C(m, "_HealthyColor"), F(m, "_ColorVariationScale", 1f)), V4(C(m, "_DryColor"), 0f),
                                  V4(C(m, "_GroundBaseColor"), F(m, "_GroundBaseMaskHardness", 1f)),
                                  new Vector4(m.IsKeywordEnabled("_GROUNDBASECOLOR") ? 1f : 0f, m.IsKeywordEnabled("_FUZZYSHADING") ? 1f : 0f, 0f, 0f) };
                    Use(ref mat, leaves ? Leaves : Grass, Record(leaves ? Leaves : Grass, new[] { noise }, Own(m, color, "_NormalMap", "_MaskMap"), values));
                    what = "its healthy and dry colors by the game's noise over the world" + (!leaves && m.IsKeywordEnabled("_GROUNDBASECOLOR") ? ", the ground's color at its foot" : "") +
                        (!leaves && m.IsKeywordEnabled("_FUZZYSHADING") ? ", its fuzzy shading" : "");
                    if (!leaves && F(m, "_WindAddColor") != 0f) what += "; the color a gust adds (_WindAddColor " + F(m, "_WindAddColor").ToString("0.##") + ") is not traced";
                    break;
                }
                case TrunkShader:
                {
                    var tiling = m.HasProperty("_TrunkTiling") ? V(m, "_TrunkTiling") : new Vector4(1f, 1f, 0f, 0f); var offset = V(m, "_TrunkOffset");
                    Main(m, ref mat, "_TrunkBaseColorMap", "_TrunkNormalMap", "_TrunkNormalScale", "_TrunkMaskMap", C(m, "_TrunkBaseColor"), new Vector4(tiling.x, tiling.y, offset.x, offset.y));
                    var barkTiling = m.HasProperty("_BarkTiling") ? V(m, "_BarkTiling") : new Vector4(1f, 1f, 0f, 0f); var barkOffset = V(m, "_BarkOffset");
                    var values = new[] { V4(C(m, "_BarkBaseColor"), F(m, "_BarkNormalScale", 1f)), new Vector4(barkTiling.x, barkTiling.y, barkOffset.x, barkOffset.y) };
                    var textures = new[] { T(m, "_BarkBaseColorMap"), T(m, "_BarkNormalMap"), T(m, "_BarkMaskMap"), T(m, "_TrunkBarkMask") };
                    Use(ref mat, Trunk, Record(Trunk, textures, Own(m, "_TrunkBaseColorMap", "_TrunkNormalMap", "_TrunkMaskMap"), values));
                    what = "its trunk and bark layers";
                    break;
                }
                case TerrainShader:
                {
                    var offset = V(m, "_Offset");
                    var values = new List<Vector4> { new Vector4(offset.x, offset.z, Mathf.Max(F(m, "_SplatResolution", 1f), 1e-6f), F(m, "_HeightTransition")) };
                    var textures = new List<Texture> { T(m, "_Splat0"), T(m, "_Splat1") };
                    for (int i = 0; i < 8; ++i)
                    {
                        string l = "_L" + i + "_";
                        var tint = m.HasProperty(l + "BaseColorRemapMax") ? V(m, l + "BaseColorRemapMax") : Vector4.one;
                        var maskMax = m.HasProperty(l + "MaskMapRemapMax") ? V(m, l + "MaskMapRemapMax") : Vector4.one;
                        float tile = F(m, l + "Tiling", 1f);
                        values.Add(new Vector4(tint.x, tint.y, tint.z, Mathf.Abs(tile) > 1e-6f ? tile : 1f));
                        values.Add(new Vector4(F(m, l + "NormalStrength", 1f), maskMax.z, maskMax.w, 0f));
                        textures.Add(T(m, l + "BaseColor")); textures.Add(T(m, l + "Normal")); textures.Add(T(m, l + "MaskMap"));
                    }
                    var splat = T(m, "_Splat0");
                    values.Add(new Vector4(splat != null && splat.wrapMode == TextureWrapMode.Clamp ? 1f : 0f, 0f, 0f, 0f));
                    // HDRP Lit's way until its textures are open: its first layer's color, tinted as the layer is.
                    var first = m.HasProperty("_L0_BaseColorRemapMax") ? V(m, "_L0_BaseColorRemapMax") : Vector4.one;
                    mat.BaseR = first.x; mat.BaseG = first.y; mat.BaseB = first.z;
                    int terrainRow = Record(Terrain, textures.ToArray(), new Texture[0], values.ToArray());
                    Use(ref mat, Terrain, terrainRow);
                    // The record's words after its head: its textures' numbers (for the log below).
                    int numbers = terrainRow * 4 + 4;
                    if (logged.Add(TerrainShader + "|" + m.name))
                    {
                        // What its program was handed (PT-104): were a layer's normal map missing or its strength 0, the tracer
                        // would answer a flat layer and nobody would be told.
                        var inv = System.Globalization.CultureInfo.InvariantCulture;
                        var said = new System.Text.StringBuilder();
                        Func<int, int> number = word => unchecked((int)words[word]);
                        for (int i = 0; i < 8; ++i)
                            said.Append(i == 0 ? "" : "; ").Append(i).Append(": ").Append(number(numbers + 2 + 3 * i)).Append(", ").Append(number(numbers + 3 + 3 * i)).Append(", ")
                                .Append(number(numbers + 4 + 3 * i)).Append(", ").Append(values[1 + 2 * i].w.ToString("0.##", inv)).Append(" m, ").Append(values[2 + 2 * i].x.ToString("0.##", inv));
                        Session.Log.LogInfo("Terrain material \"" + m.name + "\": splat maps " + number(numbers) + " and " + number(numbers + 1) + " over " +
                            values[0].z.ToString("0.#", inv) + " m" + (values[17].x != 0f ? " (clamped)" : "") + ", height transition " + values[0].w.ToString("0.###", inv) +
                            "; its layers' color, normal and mask map (the tracer's numbers; -1: the material has none, or not open yet), metres a repeat and normal strength: " + said + ".");
                    }
                    what = "its eight layers over the world by its two splat maps";
                    if (m.IsKeywordEnabled("_MATERIAL_FEATURE_SUBSURFACE_SCATTERING")) what += "; its subsurface variant is read as the plain one";
                    break;
                }
                case TriplanarShader:
                {
                    var remap = m.HasProperty("_BlendMap_Remap") ? V(m, "_BlendMap_Remap") : new Vector4(0f, 1f, 0f, 0f);
                    var values = new[]
                    {
                        V4(C(m, "_LayerA_Tint"), F(m, "_LayerA_Tiling", 1f)),
                        V4(C(m, "_LayerB_Tint"), F(m, "_LayerB_Tiling", 1f)),
                        new Vector4(F(m, "_LayerA_TriplanarBlend"), F(m, "_LayerB_TriplanarBlend"), F(m, "_Blend_TriplanarBlend"), F(m, "_Blend_Tiling")),
                        new Vector4(F(m, "_LayerA_NormalStrength", 1f), F(m, "_LayerB_NormalStrength", 1f), F(m, "_Thickness"), F(m, "_Blend")),
                        new Vector4(remap.x, remap.y, F(m, "_Blend_Feather"), F(m, "_HeightBasedBlend")),
                        V4(C(m, "_Dirt_Tint"), F(m, "_DirtA_Strength")),
                        new Vector4(F(m, "_DirtB_Strength"), F(m, "_DirtC_Strength"), F(m, "_DirtD_Strength"), F(m, "_TriplanarRotation")),
                    };
                    var textures = new[] { T(m, "_LayerA_BaseMap"), T(m, "_LayerA_NormalMap"), T(m, "_LayerA_MaskMap"), T(m, "_LayerB_BaseMap"), T(m, "_LayerB_NormalMap"),
                        T(m, "_LayerB_MaskMap"), T(m, "_Texture2DAsset_e2eaa2b5c46d476fb8c1e7a6b8c53af5_Out_0_Texture2D"), T(m, "_BlendNormalMap") };
                    Use(ref mat, Triplanar, Record(Triplanar, textures, new Texture[0], values));
                    what = "its two layers and blend map laid over the world from three sides, its dirt and its normal maps";
                    break;
                }
                case "TG/Character/RealSkin":
                {
                    bool head = m.IsKeywordEnabled("BOOLEAN_IS_HEAD_ON");
                    var values = new List<Vector4>
                    {
                        V4(C(m, "_DiffuseColor"), head ? 1f : 0f),
                        new Vector4(F(m, "_SmoothnessMin"), F(m, "_SmoothnessMax", 1f), F(m, "_SmoothnessPower", 1f), F(m, "_UnmaskedSmoothnessMod")),
                        new Vector4(F(m, "_RSmoothnessMod"), F(m, "_GSmoothnessMod"), F(m, "_BSmoothnessMod"), F(m, "_ASmoothnessMod")),
                        new Vector4(F(m, "_CheekSmoothnessMod"), F(m, "_ForeheadSmoothnessMod"), F(m, "_UpperLipSmoothnessMod"), F(m, "_ChinSmoothnessMod")),
                        new Vector4(F(m, "_EarSmoothnessMod"), F(m, "_NeckSmoothnessMod"), F(m, "_MicroSmoothnessMod"), F(m, "_MicroNormalStrength", 0.5f)),
                        new Vector4(F(m, "_NormalStrength", 1f), F(m, "_NormalBlendStrength", 0.5f), F(m, "_ColorBlendStrength", 0.5f), F(m, "_MicroNormalTiling", 25f)),
                        new Vector4(F(m, "_MouthCavityAO", 1f), F(m, "_NostrilCavityAO", 1f), F(m, "_LipsCavityAO", 1f), 0f),
                    };
                    foreach (string channel in new[] { "A", "B", "C", "D" })
                    {
                        string property = "_TattooColor" + channel;
                        values.Add(V4(C(m, property), m.HasProperty(property) ? m.GetColor(property).a : 1f));
                    }
                    values.Add(new Vector4(F(m, "_TattooNormalA", 1f), F(m, "_TattooNormalB", 1f), F(m, "_TattooNormalC", 1f), F(m, "_TattooNormalD", 1f)));
                    // PT-40, its subsurface mask per point (values 12 to 14, texture 12; pt_materials program_skin): how much of
                    // the skin's diffuse light scatters under it there. Its shader: saturate(the scatter scale of the region
                    // the point lies in x _SubsurfaceScale x _SSSMap's red); the regions are the smoothness's (the head's
                    // only on a head: its textures are null on a body, and read as no region).
                    values.Add(new Vector4(F(m, "_RScatterScale", 1f), F(m, "_GScatterScale", 1f), F(m, "_BScatterScale", 1f), F(m, "_AScatterScale", 1f)));
                    values.Add(new Vector4(F(m, "_CheekScatterScale", 1f), F(m, "_ForeheadScatterScale", 1f), F(m, "_UpperLipScatterScale", 1f), F(m, "_ChinScatterScale", 1f)));
                    values.Add(new Vector4(F(m, "_EarScatterScale", 1f), F(m, "_NeckScatterScale", 1f), F(m, "_UnmaskedScatterScale", 1f), F(m, "_SubsurfaceScale", 1f)));
                    var textures = new[] { T(m, "_DiffuseMap"), T(m, "_MaskMap"), T(m, "_NormalMap"), T(m, "_MicroNormalMap"), T(m, "_RGBAMask"),
                        head ? T(m, "_ColorBlendMap") : null, head ? T(m, "_NormalBlendMap") : null, head ? T(m, "_MNAOMap") : null,
                        head ? T(m, "_CFULCMask") : null, head ? T(m, "_EarNeckMask") : null, T(m, "_TattooMaskMap"), T(m, "_TattooNormalMap"), T(m, "_SSSMap") };
                    Use(ref mat, Skin, Record(Skin, textures, new Texture[0], values.ToArray()));
                    what = "its diffuse tint, " + (head ? "head blend and cavity masks, " : "") + "region smoothness, micro normals, tattoo color/normals and its subsurface mask per point (its map, scale and regions); tattoo emission and its thickness map remain unread";
                    break;
                }
                case "TG/Dissolve/Dissolve3D":
                {
                    // These base properties are named differently from Lit. The dissolve's moving cut and edge emission
                    // still need the graph's coverage program; this is only the underlying surface, not that feature.
                    Main(m, ref mat, "_BaseMap", "_NormalMap", "_NormalStrenght", "_MaskMap", C(m, "_BaseTint"), new Vector4(1f, 1f, 0f, 0f));
                    what = "its base tint and normal strength; the moving dissolve cut and edge emission remain unread";
                    break;
                }
                case "HDRP/Lit":
                {
                    var detail = T(m, "_DetailMap");
                    if (!m.IsKeywordEnabled("_DETAIL_MAP") || detail == null) break;
                    // Its tiling over the mesh's coordinates: its own, over the base map's where HDRP links the two.
                    Vector2 scale = m.GetTextureScale("_DetailMap"), bias = m.GetTextureOffset("_DetailMap");
                    if (F(m, "_LinkDetailsWithBase", 1f) > 0.5f)
                    {
                        bias = new Vector2(mat.OffsetX * scale.x + bias.x, mat.OffsetY * scale.y + bias.y);
                        scale = new Vector2(mat.ScaleX * scale.x, mat.ScaleY * scale.y);
                    }
                    var values = new[] { new Vector4(scale.x, scale.y, bias.x, bias.y), new Vector4(F(m, "_DetailAlbedoScale", 1f), F(m, "_DetailNormalScale", 1f), F(m, "_DetailSmoothnessScale", 1f), 0f) };
                    Use(ref mat, Detail, Record(Detail, new[] { detail }, Own(m, "_BaseColorMap", "_NormalMap", "_MaskMap"), values));
                    what = "its detail map";
                    break;
                }
            }
            if (what != null && logged.Add(shaderName))
                Session.Log.LogInfo("Traced material shader \"" + shaderName + "\": computed as the game's shader computes it (" + what + "; material \"" + m.name + "\")");
            return what;
        }
    }
}
