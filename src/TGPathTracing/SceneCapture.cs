// SPDX-License-Identifier: MIT
// The traced scene, gathered from what FoA renders near the camera every couple of seconds:
//   - GameObjects with mesh renderers (interactive props, characters' held items, the odd static object),
//     one LOD per LOD group, chosen the way Unity chooses it;
//   - the world Awaken's ECS renderer (Drake) draws: every entity with a Drake mesh-material pair whose
//     mesh and material its loading manager has loaded, grouped back into renderers, one LOD each;
//   - the entities Entities Graphics draws without Drake (most of the castle: walls, arches, battlements), their
//     meshes and materials from Entities Graphics' own registry, one LOD each;
//   - Unity terrains, as meshes built from their heightmaps.
// Each mesh is registered once (the native side reads its GPU buffers back and builds its BLAS); each
// material is reduced to what rays need (base color and texture, emission, alpha test) and each texture
// is baked once into the tracer's atlas. Also the large static sets Awaken's Medusa renderer draws. Shadow-only
// meshes (coarser copies of the drawn buildings the game casts their shadows with) are left out: the drawn geometry
// casts the shadows. Only where Entities Graphics can't be read do they stand in for it (rays from a drawn surface
// pass through them close by). Meshes on layers the camera leaves out are left out. Characters come from CharacterCapture. What is not captured (vegetation drawn by
// Vegetation Studio, particles, water) is simply absent from the traced scene: HDRP still draws and lights it.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
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
namespace TGPathTracing
{
    internal static class SceneCapture
    {
        private const float ScanInterval = 2f;
        // Sooner once the camera has moved: LODs chosen for where it was would stand in for the ones now drawn.
        private const float MovedScanInterval = 0.5f, MovedDistance = 4f;
        private static float lastScan;
        private static Vector3 lastScanCamera;
        private static int drakeShadowOnly, drakeHidden, objectsLeftOut, objectsStandIn;
        // Whether shadow-only meshes stand in for drawn geometry: only while Entities Graphics' entities can't be read.
        private static bool standIns = true;
        private static readonly HashSet<string> undrawnSeen = new HashSet<string>();
        // Registering a mesh reads its native buffers, which waits for Unity's render thread: a few a frame.
        private const int MeshesPerFrame = 6;
        private struct MeshEntry { internal int Handle; internal int Submeshes; internal bool Failed; }
        private static readonly Dictionary<int, MeshEntry> meshes = new Dictionary<int, MeshEntry>();
        private static readonly Dictionary<int, uint> materials = new Dictionary<int, uint>();
        private static readonly List<NativeMaterial> materialList = new List<NativeMaterial>();
        private static int materialsUploaded;
        private static readonly Dictionary<int, int> tiles = new Dictionary<int, int>(), alphaTiles = new Dictionary<int, int>();
        private static readonly Dictionary<int, uint> characterMaterials = new Dictionary<int, uint>();
        private static readonly HashSet<string> characterShadersSeen = new HashSet<string>();
        private static readonly string[] StrandNames = { "Hair", "Lash", "Brow", "Beard", "Fur", "Mustache", "Moustache" };
        private static readonly Dictionary<int, int> terrains = new Dictionary<int, int>();
        private static readonly HashSet<string> shadersSeen = new HashSet<string>();
        private static float nextScan;
        private static readonly Queue<Mesh> meshQueue = new Queue<Mesh>();
        private static readonly HashSet<int> queued = new HashSet<int>();
        private static EntityQuery lodQuery, rangeQuery, plainQuery, entityQuery;
        private static World queryWorld;
        internal static string Summary { get; private set; } = "no scan yet";
        internal static int InstanceCount { get; private set; }
        // Material 0: rays pass through (transparent submeshes); material 1: neutral grey (a submesh with no material).
        private const uint Invisible = 0, Neutral = 1;

        // Adds one instance of a mesh with a renderer's materials; false when it isn't traceable yet (or at all).
        internal delegate bool Sink(Mesh mesh, Material[] materials, Matrix4x4 world);
        private static List<NativeInstance> sinkInstances;
        private static List<uint> sinkIndices;
        private static bool AddMesh(Mesh mesh, Material[] shared, Matrix4x4 world) => AddMesh(mesh, shared, world, 0u);
        // A mesh's submesh materials, worked out once per material array (vegetation adds tens of thousands of
        // instances of a few prefabs a scan).
        private static readonly Dictionary<(int, Material[]), uint[]> materialLists = new Dictionary<(int, Material[]), uint[]>();
        private static bool AddMesh(Mesh mesh, Material[] shared, Matrix4x4 world, uint flags)
        {
            if (shared == null || shared.Length == 0) return false;
            int handle = MeshHandle(mesh, out int submeshes);
            if (handle <= 0) return false;
            var key = (handle, shared);
            if (!materialLists.TryGetValue(key, out var list) || list.Length != submeshes)
            {
                list = new uint[submeshes];
                bool visible = false;
                for (int s = 0; s < submeshes; s++) { list[s] = MaterialIndex(shared[Mathf.Min(s, shared.Length - 1)]); visible |= list[s] != Invisible; }
                if (!visible) list = new uint[0];
                materialLists[key] = list;
            }
            if (list.Length == 0) return false;
            AddInstance(sinkInstances, sinkIndices, handle, world, list, flags);
            return true;
        }

        internal static void Reset()
        {
            VegetationCapture.Reset(); LeshyCapture.Reset(); materialLists.Clear();
            meshes.Clear(); materials.Clear(); materialList.Clear(); tiles.Clear(); alphaTiles.Clear(); characterMaterials.Clear(); terrains.Clear(); meshQueue.Clear(); queued.Clear();
            terrainMaterial = uint.MaxValue;
            materialsUploaded = 0; nextScan = 0; lastScan = 0; InstanceCount = 0;
            medusaScenes.Clear();
            queryWorld = null;
        }

        internal static void Tick(float now, Vector3 camera)
        {
            RegisterQueued();
            bool moved = now >= lastScan + MovedScanInterval && (camera - lastScanCamera).sqrMagnitude > MovedDistance * MovedDistance;
            if (now < nextScan && !moved) return;
            nextScan = now + ScanInterval;
            lastScan = now; lastScanCamera = camera;
            var watch = Stopwatch.StartNew();
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            EnsureDefaults();
            var settings = Session.Settings;
            float radius = settings.SceneRadius.Value;
            var instances = new List<NativeInstance>(4096);
            var indices = new List<uint>(8192);
            int renderers = 0, drake = 0, entities = 0, medusa = 0, terrain = 0, vegetation = 0;
            string problems = "";
            sinkInstances = instances; sinkIndices = indices;
            if (settings.DrakeWorld.Value) { try { medusa = CaptureMedusa(camera, radius); } catch (Exception ex) { problems += " medusa: " + ex.Message; } }
            bool covered = false;
            if (settings.DrakeWorld.Value) { try { entities = CaptureEntities(camera, radius, instances, indices, out covered); } catch (Exception ex) { problems += " entities: " + ex.Message; } }
            if (standIns == covered)
            {
                standIns = !covered;
                Session.Log.LogInfo(standIns ? "Shadow-only meshes stand in for the geometry Entities Graphics draws near the camera (" + entityUnresolved +
                        " entities unresolved, " + entityPending + " meshes still registering)"
                    : "Shadow-only meshes are left out of the traced scene: the geometry Entities Graphics draws is traced and casts the shadows");
            }
            if (settings.SceneObjects.Value) { try { renderers = CaptureRenderers(camera, radius, instances, indices); } catch (Exception ex) { problems += " renderers: " + ex.Message; } }
            if (settings.DrakeWorld.Value) { try { drake = CaptureDrake(camera, radius, instances, indices); } catch (Exception ex) { problems += " drake: " + ex.Message; } }
            if (settings.Terrain.Value) { try { terrain = CaptureTerrains(camera, radius, instances, indices); } catch (Exception ex) { problems += " terrain: " + ex.Message; } }
            if (settings.Vegetation.Value) { try { vegetation = VegetationCapture.Capture(camera, radius, AddMesh); } catch (Exception ex) { problems += " vegetation: " + ex.Message; } }
            if (settings.Vegetation.Value) { try { vegetation += LeshyCapture.Capture(camera, radius, AddMesh); } catch (Exception ex) { problems += " leshy: " + ex.Message; } }
            if (materialList.Count > materialsUploaded)
            {
                var added = materialList.GetRange(materialsUploaded, materialList.Count - materialsUploaded).ToArray();
                if (tracer.SetMaterials((uint)materialsUploaded, added, (uint)added.Length) == 0) materialsUploaded = materialList.Count;
            }
            int result = tracer.SetScene(instances.ToArray(), (uint)instances.Count, indices.ToArray(), (uint)indices.Count);
            InstanceCount = instances.Count;
            watch.Stop();
            Summary = instances.Count + " instances (objects " + renderers + ", Drake " + drake + ", entities " + entities + ", Medusa " + medusa + ", terrain " + terrain + ", vegetation " + vegetation + "), " + meshes.Count + " meshes, " +
                materialList.Count + " materials, " + tiles.Count + " textures; scan " + watch.Elapsed.TotalMilliseconds.ToString("0.0") + " ms" +
                (meshQueue.Count > 0 ? "; " + meshQueue.Count + " meshes to register" : "") + (result != 0 ? "; scene refused (" + result + ")" : "") +
                (drakeShadowOnly + entityShadowOnly + drakeHidden + entityHidden > 0 ? "; entities the camera does not draw: " + (drakeShadowOnly + entityShadowOnly) +
                    (standIns ? " shadow-only (stand-ins), " : " shadow-only (left out), ") + (drakeHidden + entityHidden) + " on other layers (left out)" : "") +
                (entityUnresolved > 0 ? "; " + entityUnresolved + " entities whose mesh could not be resolved" : "") +
                (entityPending > 0 ? "; " + entityPending + " entity meshes registering" : "") +
                (objectsStandIn + objectsLeftOut > 0 ? "; objects the camera does not draw: " + objectsStandIn + (standIns ? " shadow-only (stand-ins), " : " shadow-only (left out), ") +
                    objectsLeftOut + " on other layers (left out)" : "") + problems;
        }

        // ------------------------------------------------------------------ meshes, materials, textures
        private static uint FormatOf(VertexAttributeFormat f) => (uint)f <= 5 ? (uint)f : 0xFFFFFFFFu;
        private static int MeshHandle(Mesh mesh, out int submeshes)
        {
            submeshes = 0;
            if (mesh == null) return 0;
            int id = mesh.GetInstanceID();
            if (meshes.TryGetValue(id, out var entry)) { submeshes = entry.Submeshes; return entry.Failed ? 0 : entry.Handle; }
            if (queued.Add(id)) meshQueue.Enqueue(mesh); // traced from the first scan after it is registered
            return 0;
        }
        private static void RegisterQueued()
        {
            for (int n = 0; n < MeshesPerFrame && meshQueue.Count > 0; n++)
            {
                var mesh = meshQueue.Dequeue();
                if (mesh == null) continue; // unloaded while waiting
                int id = mesh.GetInstanceID();
                queued.Remove(id);
                int handle;
                int submeshes;
                try { handle = RegisterMesh(mesh, out submeshes); }
                catch (Exception ex) { handle = -9; submeshes = 0; Session.Log.LogWarning("Mesh \"" + mesh.name + "\" could not be registered: " + ex.Message); }
                meshes[id] = new MeshEntry { Handle = handle, Submeshes = submeshes, Failed = handle <= 0 };
            }
        }
        private static int RegisterMesh(Mesh mesh, out int submeshes)
        {
            submeshes = mesh.subMeshCount;
            if (mesh.vertexCount == 0 || submeshes == 0 || !mesh.HasVertexAttribute(VertexAttribute.Position)) return -1;
            var d = new NativeMeshDesc { Size = 144, Abi = Tgpt.Abi, Key = (ulong)(uint)mesh.GetInstanceID(), UvStream = Tgpt.NoStream, ColorStream = Tgpt.NoStream };
            var positionFormat = mesh.GetVertexAttributeFormat(VertexAttribute.Position);
            d.PositionStream = (uint)mesh.GetVertexAttributeStream(VertexAttribute.Position);
            d.PositionOffset = (uint)mesh.GetVertexAttributeOffset(VertexAttribute.Position);
            d.PositionFormat = FormatOf(positionFormat); d.PositionDimension = (uint)mesh.GetVertexAttributeDimension(VertexAttribute.Position);
            if (d.PositionFormat == 0xFFFFFFFFu || d.PositionStream > 3) return -2;
            if (mesh.HasVertexAttribute(VertexAttribute.TexCoord0) && FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.TexCoord0)) != 0xFFFFFFFFu)
            {
                d.UvStream = (uint)mesh.GetVertexAttributeStream(VertexAttribute.TexCoord0); d.UvOffset = (uint)mesh.GetVertexAttributeOffset(VertexAttribute.TexCoord0);
                d.UvFormat = FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.TexCoord0)); d.UvDimension = (uint)mesh.GetVertexAttributeDimension(VertexAttribute.TexCoord0);
            }
            var streams = new IntPtr[4]; var strides = new uint[4];
            for (int s = 0; s < mesh.vertexBufferCount && s < 4; s++)
            {
                bool used = s == d.PositionStream || s == d.UvStream;
                if (!used) continue;
                streams[s] = mesh.GetNativeVertexBufferPtr(s); // waits for the render thread: once per mesh
                strides[s] = (uint)mesh.GetVertexBufferStride(s);
            }
            if (streams[d.PositionStream] == IntPtr.Zero) return -3;
            if (d.UvStream < 4 && streams[d.UvStream] == IntPtr.Zero) d.UvStream = Tgpt.NoStream;
            d.Vb0 = Ptr(streams[0]); d.Vb1 = Ptr(streams[1]); d.Vb2 = Ptr(streams[2]); d.Vb3 = Ptr(streams[3]);
            d.Stride0 = strides[0]; d.Stride1 = strides[1]; d.Stride2 = strides[2]; d.Stride3 = strides[3];
            var ib = mesh.GetNativeIndexBufferPtr();
            if (ib == IntPtr.Zero) return -4;
            d.IndexBuffer = Ptr(ib);
            d.IndexFormat = mesh.indexFormat == IndexFormat.UInt32 ? 32u : 16u;
            d.VertexCount = (uint)mesh.vertexCount;
            var subs = new NativeSubmesh[submeshes];
            for (int i = 0; i < submeshes; i++)
            {
                var sm = mesh.GetSubMesh(i);
                subs[i] = new NativeSubmesh { IndexStart = (uint)sm.indexStart, IndexCount = (uint)sm.indexCount, BaseVertex = (uint)sm.baseVertex, Topology = (uint)sm.topology };
            }
            var pin = GCHandle.Alloc(subs, GCHandleType.Pinned);
            try
            {
                d.SubmeshCount = (uint)submeshes; d.Submeshes = pin.AddrOfPinnedObject();
                return Session.Tracer.RegisterMesh(ref d);
            }
            finally { pin.Free(); }
        }
        internal static int CpuMesh(int key, float[] positions, float[] uvs, uint[] colors, uint[] tris)
        {
            var handles = new[] { GCHandle.Alloc(positions, GCHandleType.Pinned), uvs != null ? GCHandle.Alloc(uvs, GCHandleType.Pinned) : default,
                colors != null ? GCHandle.Alloc(colors, GCHandleType.Pinned) : default, GCHandle.Alloc(tris, GCHandleType.Pinned) };
            try
            {
                var d = new NativeCpuMeshDesc
                {
                    Size = 56, Abi = Tgpt.Abi, Key = (ulong)(uint)key, Positions = handles[0].AddrOfPinnedObject(),
                    Uvs = uvs != null ? handles[1].AddrOfPinnedObject() : IntPtr.Zero, Colors = colors != null ? handles[2].AddrOfPinnedObject() : IntPtr.Zero,
                    Indices = handles[3].AddrOfPinnedObject(), VertexCount = (uint)(positions.Length / 3), IndexCount = (uint)tris.Length
                };
                return Session.Tracer.RegisterCpuMesh(ref d);
            }
            finally { foreach (var h in handles) if (h.IsAllocated) h.Free(); }
        }
        private static int Tile(Texture texture)
        {
            if (!(texture is Texture2D)) return -1;
            int id = texture.GetInstanceID();
            if (tiles.TryGetValue(id, out int tile)) return tile;
            IntPtr native = texture.GetNativeTexturePtr();
            tile = native != IntPtr.Zero ? Session.Tracer.RegisterTexture((ulong)(uint)id, native) : -1;
            if (tile >= 0 || native != IntPtr.Zero) tiles[id] = tile;
            return tile;
        }
        // An alpha-tested texture's alpha at 256x256 for the alpha test: the 64x64 color tile turned straw, rope nets
        // and leaves into opaque blocks (black patches under straw on a floor). -1 when the alpha atlas is full.
        private static int AlphaTile(Texture texture)
        {
            if (!(texture is Texture2D)) return -1;
            int id = texture.GetInstanceID();
            if (alphaTiles.TryGetValue(id, out int tile)) return tile;
            IntPtr native = texture.GetNativeTexturePtr();
            tile = native != IntPtr.Zero ? Session.Tracer.RegisterAlphaTexture((ulong)(uint)id, native) : -1;
            if (tile >= 0 || native != IntPtr.Zero) alphaTiles[id] = tile;
            return tile;
        }
        // The two materials every scene starts with: invisible to rays (index 0) and neutral grey (1).
        private static void EnsureDefaults()
        {
            if (materialList.Count != 0) return;
            materialList.Add(new NativeMaterial { BaseA = 1f, ScaleX = 1, ScaleY = 1, AlbedoTile = -1, EmissiveTile = -1, AlphaCutoff = 2f, Flags = Tgpt.MaterialAlphaTest });
            materialList.Add(new NativeMaterial { BaseR = 0.4f, BaseG = 0.4f, BaseB = 0.4f, BaseA = 1f, ScaleX = 1, ScaleY = 1, AlbedoTile = -1, EmissiveTile = -1 });
        }
        // Characters' parts left out of the traced scene: transparent and unlit ones (eye wetness, glow cards), the
        // game's invisible material (TG/NoRender: parts hidden under armour), and the eyes (TG/Character/Eye: the
        // eye the game draws lies just inside the traced eyeball, so every ray from it started inside and the eyes
        // were black). CharacterCapture does not register their triangles.
        internal static bool CharacterLeftOut(Material m)
        {
            if (m == null) return false;
            var shader = m.shader;
            string shaderName = shader != null ? shader.name : "";
            return (m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f) || m.renderQueue >= 2750 ||
                shaderName.IndexOf("Unlit", StringComparison.OrdinalIgnoreCase) >= 0 || shaderName.IndexOf("NoRender", StringComparison.OrdinalIgnoreCase) >= 0 ||
                shaderName.StartsWith("TG/Character/Eye", StringComparison.OrdinalIgnoreCase);
        }
        // A character's material (CharacterCapture). With the character's texture coordinates (textured), read as the
        // world's are (its texture, color and alpha test): rays see its colors and its cut-outs (hair, torn cloth) texel
        // by texel. Hair without an alpha test of its own is cut out by its texture's alpha; skin never is (skin shaders
        // keep other data in that alpha). Without them: the material's own color, dimmed where a texture would give it
        // (a texture averages about that), hair invisible to rays, other alpha-tested parts solid.
        internal static uint CharacterMaterial(Material m, bool textured)
        {
            EnsureDefaults();
            if (m == null) return Neutral;
            int key = unchecked(m.GetInstanceID() * 2 + (textured ? 1 : 0));
            if (characterMaterials.TryGetValue(key, out uint index)) return index;
            var shader = m.shader;
            string shaderName = shader != null ? shader.name : "";
            bool alphaTest = (m.HasProperty("_AlphaCutoffEnable") && m.GetFloat("_AlphaCutoffEnable") > 0.5f) || m.IsKeywordEnabled("_ALPHATEST_ON");
            bool strands = shaderName.IndexOf("Hair", StringComparison.OrdinalIgnoreCase) >= 0 ||
                (alphaTest && Array.Exists(StrandNames, n => m.name.IndexOf(n, StringComparison.OrdinalIgnoreCase) >= 0));
            bool skin = shaderName.IndexOf("Skin", StringComparison.OrdinalIgnoreCase) >= 0;
            string how;
            uint world = textured && !CharacterLeftOut(m) ? MaterialIndex(m) : Invisible;
            if (CharacterLeftOut(m)) { index = Invisible; how = "is transparent, left out of the traced scene"; }
            else if (world != Invisible && materialList[(int)world].AlbedoTile >= 0)
            {
                var mat = materialList[(int)world];
                bool cut = (mat.Flags & Tgpt.MaterialAlphaTest) != 0;
                if (cut && skin)
                {
                    mat.Flags &= ~(Tgpt.MaterialAlphaTest | Tgpt.MaterialAlphaTile); mat.AlphaTile = -1;
                    materialList.Add(mat); index = (uint)(materialList.Count - 1); how = "traced with its texture (skin: not cut out)";
                }
                else if (strands)
                {
                    // Strands a few texels wide average out in the 256x256 alpha tile, below the usual cutoff (a hair
                    // cast no shadow on the neck under it): hair is cut out where a fifth of a texel is covered.
                    if (!cut)
                    {
                        string albedo = AlbedoProperty(m);
                        int tile = albedo != null ? AlphaTile(m.GetTexture(albedo)) : -1;
                        mat.Flags |= Tgpt.MaterialAlphaTest;
                        if (tile >= 0) { mat.AlphaTile = tile; mat.Flags |= Tgpt.MaterialAlphaTile; }
                    }
                    mat.AlphaCutoff = Mathf.Min(cut ? mat.AlphaCutoff : 0.5f, 0.2f);
                    materialList.Add(mat); index = (uint)(materialList.Count - 1); how = "hair, cut out by its texture's alpha (a fifth of a texel covered)";
                }
                else { index = world; how = cut ? "traced with its texture, cut out by its alpha" : "traced with its texture"; }
            }
            else if (strands) { index = Invisible; how = "hair, invisible to rays (no texture to cut it out by)"; }
            else
            {
                var mat = new NativeMaterial { BaseA = 1f, ScaleX = 1f, ScaleY = 1f, AlbedoTile = -1, EmissiveTile = -1, Smoothness = 0.4f };
                var color = new Vector3(0.5f, 0.5f, 0.5f);
                foreach (var name in ColorNames) if (m.HasProperty(name)) { color = LinearColor(m.GetColor(name)); break; }
                if (AlbedoProperty(m) != null) color *= 0.45f;
                mat.BaseR = color.x; mat.BaseG = color.y; mat.BaseB = color.z;
                if (m.HasProperty("_Smoothness")) mat.Smoothness = Mathf.Clamp01(m.GetFloat("_Smoothness"));
                materialList.Add(mat);
                index = (uint)(materialList.Count - 1);
                how = alphaTest ? "alpha-tested, traced solid (no texture coordinates)" : "traced with its color (no texture coordinates)";
            }
            if (characterShadersSeen.Add(shaderName + "|" + how))
                Session.Log.LogInfo("Characters: shader \"" + shaderName + "\" (material \"" + m.name + "\") " + how);
            characterMaterials[key] = index;
            return index;
        }
        // Materials added since the last upload go to the native side now (characters' arrive between scans).
        internal static void FlushMaterials()
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected || materialList.Count <= materialsUploaded) return;
            var added = materialList.GetRange(materialsUploaded, materialList.Count - materialsUploaded).ToArray();
            if (tracer.SetMaterials((uint)materialsUploaded, added, (uint)added.Length) == 0) materialsUploaded = materialList.Count;
        }
        private static readonly string[] AlbedoNames = { "_BaseColorMap", "_BaseMap", "_MainTex", "_AlbedoMap", "_Albedo", "_BaseColorTexture", "_DiffuseMap", "_Diffuse", "_ColorMap" };
        private static readonly string[] ColorNames = { "_BaseColor", "_Color", "_AlbedoColor", "_Tint", "_TintColor" };
        private static string AlbedoProperty(Material m)
        {
            foreach (var name in AlbedoNames) if (m.HasProperty(name) && m.GetTexture(name) != null) return name;
            foreach (var name in m.GetTexturePropertyNames())
            {
                string n = name.ToLowerInvariant();
                // "_Base_Map" (TG cloth), "_LayerA_BaseMap" (TG triplanar architecture: sampled through the mesh's UVs, a
                // different pattern but the same colors for the light bouncing off it).
                if ((n.Contains("albedo") || n.Contains("basecolor") || n.Contains("diffuse") || n.Contains("maintex") || n.Contains("basemap") || n.Contains("base_map")) &&
                    !n.Contains("normal") && !n.Contains("mask") &&
                    !n.Contains("detail") && m.GetTexture(name) != null) return name;
            }
            return null;
        }
        private static Vector3 LinearColor(Color c) { var l = c.linear; return new Vector3(l.r, l.g, l.b); }
        internal static uint MaterialIndex(Material m)
        {
            if (m == null) return Neutral;
            int id = m.GetInstanceID();
            if (materials.TryGetValue(id, out uint index)) return index;
            index = ReadMaterial(m);
            materials[id] = index;
            return index;
        }
        private static uint ReadMaterial(Material m)
        {
            var shader = m.shader;
            string shaderName = shader != null ? shader.name : "";
            // Unlit meshes are sky domes, glow cards and distant impostors: as opaque surfaces they would block the sky.
            // TG/NoRender is the game's invisible material.
            bool transparent = (m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f) || m.renderQueue >= 2750 ||
                shaderName.IndexOf("Unlit", StringComparison.OrdinalIgnoreCase) >= 0 || shaderName.IndexOf("NoRender", StringComparison.OrdinalIgnoreCase) >= 0;
            if (transparent)
            {
                if (shadersSeen.Add(shaderName)) Session.Log.LogInfo("Traced material shader \"" + shaderName + "\": invisible to rays (transparent or unlit).");
                return Invisible;
            }
            var mat = new NativeMaterial { BaseA = 1f, ScaleX = 1f, ScaleY = 1f, AlbedoTile = -1, EmissiveTile = -1, Smoothness = 0.5f };
            var baseColor = Vector3.one; float alpha = 1f;
            foreach (var name in ColorNames) if (m.HasProperty(name)) { var c = m.GetColor(name); baseColor = LinearColor(c); alpha = c.a; break; }
            string albedo = AlbedoProperty(m);
            if (albedo != null)
            {
                mat.AlbedoTile = Tile(m.GetTexture(albedo));
                var scale = m.GetTextureScale(albedo); var offset = m.GetTextureOffset(albedo);
                mat.ScaleX = scale.x; mat.ScaleY = scale.y; mat.OffsetX = offset.x; mat.OffsetY = offset.y;
            }
            mat.BaseR = baseColor.x; mat.BaseG = baseColor.y; mat.BaseB = baseColor.z; mat.BaseA = alpha;
            if (m.HasProperty("_Metallic")) mat.Metallic = Mathf.Clamp01(m.GetFloat("_Metallic"));
            if (m.HasProperty("_Smoothness")) mat.Smoothness = Mathf.Clamp01(m.GetFloat("_Smoothness"));
            // HDRP stores the emissive color already multiplied by its intensity (linear, in nits).
            if (m.HasProperty("_EmissiveColor") && (m.globalIlluminationFlags & MaterialGlobalIlluminationFlags.EmissiveIsBlack) == 0)
            {
                var e = m.GetColor("_EmissiveColor");
                if (e.maxColorComponent > 0.001f)
                {
                    mat.EmissiveR = e.r; mat.EmissiveG = e.g; mat.EmissiveB = e.b;
                    if (m.HasProperty("_EmissiveColorMap") && m.GetTexture("_EmissiveColorMap") != null) mat.EmissiveTile = Tile(m.GetTexture("_EmissiveColorMap"));
                }
            }
            bool alphaTest = (m.HasProperty("_AlphaCutoffEnable") && m.GetFloat("_AlphaCutoffEnable") > 0.5f) || m.IsKeywordEnabled("_ALPHATEST_ON") ||
                shaderName.IndexOf("Leaves", StringComparison.OrdinalIgnoreCase) >= 0 || shaderName.IndexOf("Grass", StringComparison.OrdinalIgnoreCase) >= 0 ||
                shaderName.IndexOf("Billboard", StringComparison.OrdinalIgnoreCase) >= 0;
            if (alphaTest && mat.AlbedoTile >= 0)
            {
                mat.Flags |= Tgpt.MaterialAlphaTest;
                mat.AlphaCutoff = m.HasProperty("_AlphaCutoff") ? m.GetFloat("_AlphaCutoff") : m.HasProperty("_Cutoff") ? m.GetFloat("_Cutoff") : 0.5f;
                mat.AlphaCutoff = Mathf.Clamp(mat.AlphaCutoff, 0.05f, 0.95f);
                int alphaTile = AlphaTile(m.GetTexture(albedo));
                if (alphaTile >= 0) { mat.AlphaTile = alphaTile; mat.Flags |= Tgpt.MaterialAlphaTile; }
            }
            if (m.HasProperty("_DoubleSidedEnable") && m.GetFloat("_DoubleSidedEnable") > 0.5f) mat.Flags |= Tgpt.MaterialDoubleSided;
            if (shadersSeen.Add(shaderName))
                Session.Log.LogInfo("Traced material shader \"" + shaderName + "\": albedo " + (albedo ?? "none") + (mat.AlbedoTile >= 0 ? " (tile " + mat.AlbedoTile + ")" : "") +
                    ", color (" + baseColor.x.ToString("0.00") + ", " + baseColor.y.ToString("0.00") + ", " + baseColor.z.ToString("0.00") + ")" +
                    ((mat.Flags & Tgpt.MaterialAlphaTest) != 0 ? ", alpha test " + mat.AlphaCutoff.ToString("0.00") : "") +
                    (mat.EmissiveR + mat.EmissiveG + mat.EmissiveB > 0 ? ", emissive" : "") +
                    (albedo == null ? "; its textures: " + string.Join(", ", m.GetTexturePropertyNames()) : ""));
            materialList.Add(mat);
            return (uint)(materialList.Count - 1);
        }
        private static bool IsAlphaTested(uint material) => material == Invisible || (materialList[(int)material].Flags & Tgpt.MaterialAlphaTest) != 0;

        private static void AddInstance(List<NativeInstance> instances, List<uint> indices, int handle, in Matrix4x4 m, uint[] submeshMaterials, uint flags = 0u)
        {
            var inst = new NativeInstance
            {
                M00 = m.m00, M01 = m.m01, M02 = m.m02, M03 = m.m03, M10 = m.m10, M11 = m.m11, M12 = m.m12, M13 = m.m13,
                M20 = m.m20, M21 = m.m21, M22 = m.m22, M23 = m.m23, Mesh = handle, MaterialBase = (uint)indices.Count, Mask = 0xFF, Flags = flags
            };
            bool alpha = false;
            foreach (uint material in submeshMaterials) { indices.Add(material); alpha |= IsAlphaTested(material); }
            if (alpha) inst.Flags |= Tgpt.InstanceAlphaTest;
            instances.Add(inst);
        }

        // ------------------------------------------------------------------ mesh renderers
        private static int CaptureRenderers(Vector3 camera, float radius, List<NativeInstance> instances, List<uint> indices)
        {
            float r2 = radius * radius;
            // One LOD per group: the one Unity would draw from this camera, the others left out.
            var skip = new HashSet<int>();
            var cam = Camera.main;
            float lodScale = cam != null ? QualitySettings.lodBias / (2f * Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad)) : 1f;
            foreach (var group in UnityEngine.Object.FindObjectsByType<LODGroup>(FindObjectsSortMode.None))
            {
                if (group == null || !group.enabled) continue;
                var lods = group.GetLODs();
                if (lods.Length <= 1) continue;
                var t = group.transform;
                var ls = t.lossyScale;
                float size = group.size * Mathf.Max(Mathf.Abs(ls.x), Mathf.Max(Mathf.Abs(ls.y), Mathf.Abs(ls.z)));
                float distance = Vector3.Distance(t.TransformPoint(group.localReferencePoint), camera);
                float height = size * lodScale / Mathf.Max(distance, 0.01f);
                int chosen = -1;
                for (int i = 0; i < lods.Length; i++) if (height >= lods[i].screenRelativeTransitionHeight) { chosen = i; break; }
                if (chosen < 0) chosen = lods.Length - 1; // beyond the last LOD Unity culls; the tracer keeps the coarsest for light
                for (int i = 0; i < lods.Length; i++)
                {
                    if (i == chosen || lods[i].renderers == null) continue;
                    foreach (var r in lods[i].renderers) if (r != null) skip.Add(r.GetInstanceID());
                }
            }
            int count = 0, batched = 0;
            int cullingMask = cam != null ? cam.cullingMask : ~0;
            objectsLeftOut = 0; objectsStandIn = 0;
            foreach (var renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
            {
                if (renderer == null || !renderer.enabled || !renderer.gameObject.activeInHierarchy || renderer.forceRenderingOff) continue;
                if (renderer.bounds.SqrDistance(camera) > r2 || skip.Contains(renderer.GetInstanceID())) continue;
                // Objects the camera does not draw. Shadow-only ones (the castle's CM_*_Cluster_* meshes) are coarser
                // copies of the drawn buildings the game casts their shadows with: left out, the drawn geometry (which
                // Entities Graphics draws) casts the shadows; they cast shadows the drawn walls don't and let light leak
                // where rays passed through them. Only where Entities Graphics' entities can't be read are they traced
                // as stand-ins, which rays leaving a drawn surface pass through close by. Those on layers the camera
                // leaves out (a rain-obstacle sea) are left out. Named in the log, once each.
                bool shadowOnly = renderer.shadowCastingMode == ShadowCastingMode.ShadowsOnly, hidden = (cullingMask & (1 << renderer.gameObject.layer)) == 0;
                if ((shadowOnly || hidden) && undrawnSeen.Add(renderer.name) && undrawnSeen.Count <= 60)
                    Session.Log.LogInfo((hidden || !standIns ? "Left out" : "Traced as a stand-in") + " though the camera does not draw it: \"" + renderer.name + "\" (" +
                        (hidden ? "layer " + LayerMask.LayerToName(renderer.gameObject.layer) : "shadow-only") +
                        (renderer.transform.parent != null ? ", under \"" + renderer.transform.parent.name + "\"" : "") + ")");
                if (hidden) { ++objectsLeftOut; continue; }
                if (shadowOnly)
                {
                    if (!standIns) { ++objectsStandIn; continue; }
                    var standIn = renderer.GetComponent<MeshFilter>();
                    if (standIn != null && AddMesh(standIn.sharedMesh, renderer.sharedMaterials, renderer.localToWorldMatrix, Tgpt.InstanceStandIn)) { ++count; ++objectsStandIn; }
                    continue;
                }
                if (renderer.isPartOfStaticBatch) { ++batched; continue; } // combined meshes would be traced once per member
                var filter = renderer.GetComponent<MeshFilter>();
                if (filter != null && AddMesh(filter.sharedMesh, renderer.sharedMaterials, renderer.localToWorldMatrix)) ++count;
            }
            if (batched > 0 && !loggedBatched) { loggedBatched = true; Session.Log.LogInfo(batched + " statically batched renderers near the camera are left out of the traced scene."); }
            return count;
        }
        private static bool loggedBatched;

        // ------------------------------------------------------------------ Drake (Awaken's ECS renderer)
        private static void EnsureQueries(EntityManager em, World world)
        {
            if (queryWorld == world) return;
            queryWorld = world;
            lodQuery = em.CreateEntityQuery(ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<LocalToWorld>(),
                ComponentType.ReadOnly<LODRange>(), ComponentType.ReadOnly<LODWorldReferencePoint>());
            rangeQuery = em.CreateEntityQuery(new EntityQueryDesc
            {
                All = new[] { ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<LocalToWorld>(), ComponentType.ReadOnly<DrakeRendererVisibleRangeComponent>() },
                None = new[] { ComponentType.ReadOnly<LODRange>() }
            });
            plainQuery = em.CreateEntityQuery(new EntityQueryDesc
            {
                All = new[] { ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<LocalToWorld>() },
                None = new[] { ComponentType.ReadOnly<LODRange>(), ComponentType.ReadOnly<DrakeRendererVisibleRangeComponent>() }
            });
            entityQuery = em.CreateEntityQuery(new EntityQueryDesc
            {
                All = new[] { ComponentType.ReadOnly<MaterialMeshInfo>(), ComponentType.ReadOnly<LocalToWorld>() },
                None = new[] { ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<DisableRendering>() }
            });
        }
        private struct DrakeKey : IEquatable<DrakeKey>
        {
            internal ushort Mesh; internal int X, Y, Z, R;
            public bool Equals(DrakeKey o) => Mesh == o.Mesh && X == o.X && Y == o.Y && Z == o.Z && R == o.R;
            public override bool Equals(object o) => o is DrakeKey k && Equals(k);
            public override int GetHashCode() => (((Mesh * 397 ^ X) * 397 ^ Y) * 397 ^ Z) * 397 ^ R;
        }
        private sealed class DrakeRenderer { internal ushort Mesh; internal bool StandIn; internal Matrix4x4 Matrix; internal readonly Dictionary<int, ushort> Materials = new Dictionary<int, ushort>(); }
        private static T Loaded<T>(AsyncOperationHandle<T> handle, ushort counter) where T : UnityEngine.Object =>
            counter > 0 && handle.IsValid() && handle.IsDone && handle.Status == AsyncOperationStatus.Succeeded ? handle.Result : null;
        private static int CaptureDrake(Vector3 camera, float radius, List<NativeInstance> instances, List<uint> indices)
        {
            var world = World.DefaultGameObjectInjectionWorld;
            var manager = DrakeRendererManager.Instance;
            if (world == null || !world.IsCreated || manager == null) return 0;
            var loading = manager.LoadingManager;
            var em = world.EntityManager;
            EnsureQueries(em, world);
            var cam = Camera.main;
            float lodScale = cam != null ? 2f * Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad) / Mathf.Max(QualitySettings.lodBias, 0.01f) : 1f;
            int cullingMask = cam != null ? cam.cullingMask : ~0;
            float3 c = camera;
            float r2 = radius * radius;
            var renderers = new Dictionary<DrakeKey, DrakeRenderer>();
            // Entities the camera does not draw: shadow-only ones traced as stand-ins, those on layers the camera leaves
            // out left out (see CaptureRenderers); counted for the log.
            var filterHandle = em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RenderFilterSettings>();
            var mmHandle = em.GetComponentTypeHandle<DrakeMeshMaterialComponent>(true);
            var ltwHandle = em.GetComponentTypeHandle<LocalToWorld>(true);
            var rangeHandle = em.GetComponentTypeHandle<LODRange>(true);
            var referenceHandle = em.GetComponentTypeHandle<LODWorldReferencePoint>(true);
            var visibleHandle = em.GetComponentTypeHandle<DrakeRendererVisibleRangeComponent>(true);
            bool standIn = false;
            bool Drawn(in ArchetypeChunk chunk)
            {
                standIn = false;
                if (!chunk.Has(filterHandle)) return true;
                var settings = chunk.GetSharedComponent(filterHandle, em);
                if ((cullingMask & (1 << settings.Layer)) == 0) { drakeHidden += chunk.Count; return false; }
                if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { drakeShadowOnly += chunk.Count; if (!standIns) return false; standIn = true; }
                return true;
            }
            void Collect(in DrakeMeshMaterialComponent mm, in LocalToWorld ltw)
            {
                float4x4 v = ltw.Value;
                var key = new DrakeKey { Mesh = mm.meshIndex, X = (int)math.round(v.c3.x * 100f), Y = (int)math.round(v.c3.y * 100f), Z = (int)math.round(v.c3.z * 100f),
                    R = (int)math.round((v.c0.x + v.c1.y * 3f + v.c2.z * 7f + v.c0.y * 11f + v.c2.x * 13f) * 1000f) };
                if (!renderers.TryGetValue(key, out var r))
                {
                    r = new DrakeRenderer { Mesh = mm.meshIndex, StandIn = standIn };
                    r.Matrix.SetColumn(0, new Vector4(v.c0.x, v.c0.y, v.c0.z, v.c0.w)); r.Matrix.SetColumn(1, new Vector4(v.c1.x, v.c1.y, v.c1.z, v.c1.w));
                    r.Matrix.SetColumn(2, new Vector4(v.c2.x, v.c2.y, v.c2.z, v.c2.w)); r.Matrix.SetColumn(3, new Vector4(v.c3.x, v.c3.y, v.c3.z, v.c3.w));
                    renderers[key] = r;
                }
                r.Materials[mm.submesh] = mm.materialIndex;
            }
            drakeShadowOnly = 0; drakeHidden = 0;
            using (var chunks = lodQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (!Drawn(chunk)) continue;
                    var mm = chunk.GetNativeArray(ref mmHandle); var ltw = chunk.GetNativeArray(ref ltwHandle);
                    var range = chunk.GetNativeArray(ref rangeHandle); var reference = chunk.GetNativeArray(ref referenceHandle);
                    for (int i = 0; i < chunk.Count; i++)
                    {
                        if (math.distancesq(ltw[i].Value.c3.xyz, c) > r2) continue;
                        float d = math.distance(reference[i].Value, c) * lodScale;
                        if (d < range[i].MinDist || d >= range[i].MaxDist) continue;
                        Collect(mm[i], ltw[i]);
                    }
                }
            }
            using (var chunks = rangeQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (!Drawn(chunk)) continue;
                    var mm = chunk.GetNativeArray(ref mmHandle); var ltw = chunk.GetNativeArray(ref ltwHandle); var visible = chunk.GetNativeArray(ref visibleHandle);
                    for (int i = 0; i < chunk.Count; i++)
                    {
                        float d2 = math.distancesq(ltw[i].Value.c3.xyz, c);
                        if (d2 > r2 || d2 < visible[i].value.x || d2 >= visible[i].value.y) continue;
                        Collect(mm[i], ltw[i]);
                    }
                }
            }
            using (var chunks = plainQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (!Drawn(chunk)) continue;
                    var mm = chunk.GetNativeArray(ref mmHandle); var ltw = chunk.GetNativeArray(ref ltwHandle);
                    for (int i = 0; i < chunk.Count; i++) if (math.distancesq(ltw[i].Value.c3.xyz, c) <= r2) Collect(mm[i], ltw[i]);
                }
            }
            int count = 0;
            var meshData = loading.MeshLoadingData; var materialData = loading.MaterialLoadingData;
            foreach (var r in renderers.Values)
            {
                if (r.Mesh >= meshData.Count) continue;
                var md = meshData[r.Mesh];
                var mesh = Loaded(md.loadingHandle, md.counter);
                if (mesh == null) continue;
                int handle = MeshHandle(mesh, out int submeshes);
                if (handle <= 0) continue;
                var list = new uint[submeshes];
                bool any = false;
                for (int s = 0; s < submeshes; s++)
                {
                    uint material = Neutral;
                    if (r.Materials.TryGetValue(s, out ushort mi) && mi < materialData.Count)
                    {
                        var data = materialData[mi];
                        var m = Loaded(data.loadingHandle, data.counter);
                        material = m != null ? MaterialIndex(m) : Neutral;
                    }
                    else if (r.Materials.Count > 0) material = Invisible; // a submesh Drake draws no entity for (a transparent one)
                    list[s] = material; any |= material != Invisible;
                }
                if (!any) continue;
                AddInstance(instances, indices, handle, r.Matrix, list, r.StandIn ? Tgpt.InstanceStandIn : 0u);
                ++count;
            }
            return count;
        }

        // ------------------------------------------------------------------ Entities Graphics entities without Drake
        // Most of the castle (walls, arches, battlements, stairwells) is drawn by Entities Graphics entities that carry no
        // Drake component: their meshes and materials are registered with Entities Graphics' BatchRendererGroup at
        // runtime, MaterialMeshInfo holds their IDs, and EntitiesGraphicsSystem hands the Mesh and Material back (a baked
        // entity's RenderMeshArray, where it has one). The castle's entities draw several submeshes each: their
        // MaterialMeshInfo is a range into the pairs their chunk's RendererRange points to (the game's own addition to
        // Entities Graphics, which its EmitDrawCommandsJob reads the same way). One LOD each, chosen as for Drake; the
        // pairs of one mesh at one place are one instance with the materials they draw, its other submeshes invisible.
        private struct EntityKey : IEquatable<EntityKey>
        {
            internal int Mesh, X, Y, Z, R;
            public bool Equals(EntityKey o) => Mesh == o.Mesh && X == o.X && Y == o.Y && Z == o.Z && R == o.R;
            public override bool Equals(object o) => o is EntityKey k && Equals(k);
            public override int GetHashCode() => (((Mesh * 397 ^ X) * 397 ^ Y) * 397 ^ Z) * 397 ^ R;
        }
        private sealed class EntityRenderer { internal Mesh Mesh; internal Matrix4x4 Matrix; internal readonly Dictionary<int, Material> Materials = new Dictionary<int, Material>(); }
        private static int entityShadowOnly, entityHidden, entityUnresolved;
        private static bool entitiesLogged;
        // Whether the drawn geometry near the camera is in the traced scene comes back in covered: Entities Graphics could
        // be read, and at most a tenth of its entities there are unresolved or waiting for their meshes to register
        // (shadow-only meshes stand in for them until then). Its own shadow-only entities are always left out.
        private static int entityPending;
        private static unsafe int CaptureEntities(Vector3 camera, float radius, List<NativeInstance> instances, List<uint> indices, out bool covered)
        {
            covered = false; entityPending = 0;
            entityShadowOnly = 0; entityHidden = 0; entityUnresolved = 0;
            var world = World.DefaultGameObjectInjectionWorld;
            if (world == null || !world.IsCreated) return 0;
            var graphics = world.GetExistingSystemManaged<EntitiesGraphicsSystem>();
            if (graphics == null) return 0;
            var em = world.EntityManager;
            EnsureQueries(em, world);
            var cam = Camera.main;
            float lodScale = cam != null ? 2f * Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad) / Mathf.Max(QualitySettings.lodBias, 0.01f) : 1f;
            int cullingMask = cam != null ? cam.cullingMask : ~0;
            float3 c = camera;
            float r2 = radius * radius;
            var filterHandle = em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RenderFilterSettings>();
            var arrayHandle = em.GetSharedComponentTypeHandle<RenderMeshArray>();
            var setHandle = em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RendererRange>();
            var infoHandle = em.GetComponentTypeHandle<MaterialMeshInfo>(true);
            var ltwHandle = em.GetComponentTypeHandle<LocalToWorld>(true);
            var rangeHandle = em.GetComponentTypeHandle<LODRange>(true);
            var referenceHandle = em.GetComponentTypeHandle<LODWorldReferencePoint>(true);
            var renderers = new Dictionary<EntityKey, EntityRenderer>();
            var meshCache = new Dictionary<uint, Mesh>();
            var materialCache = new Dictionary<uint, Material>();
            bool baked = false;
            RenderMeshArray array = default;
            // One mesh-material pair of an entity at v: its mesh and material, from Entities Graphics' registry (runtime
            // IDs) or the chunk's RenderMeshArray.
            bool Add(MaterialMeshInfo mmi, float4x4 v)
            {
                Mesh mesh = null; Material material = null;
                if (mmi.IsRuntimeMesh)
                {
                    uint id = mmi.MeshID.value;
                    if (id == 0) return false;
                    if (!meshCache.TryGetValue(id, out mesh)) meshCache[id] = mesh = graphics.GetMesh(mmi.MeshID);
                }
                else if (baked) mesh = array.GetMesh(mmi);
                if (mesh == null) return false;
                if (mmi.Material >= 0)
                {
                    uint id = mmi.MaterialID.value;
                    if (!materialCache.TryGetValue(id, out material)) materialCache[id] = material = graphics.GetMaterial(mmi.MaterialID);
                }
                else if (baked) material = array.GetMaterial(mmi);
                var key = new EntityKey { Mesh = mesh.GetInstanceID(), X = (int)math.round(v.c3.x * 100f), Y = (int)math.round(v.c3.y * 100f), Z = (int)math.round(v.c3.z * 100f),
                    R = (int)math.round((v.c0.x + v.c1.y * 3f + v.c2.z * 7f + v.c0.y * 11f + v.c2.x * 13f) * 1000f) };
                if (!renderers.TryGetValue(key, out var r))
                {
                    r = new EntityRenderer { Mesh = mesh };
                    r.Matrix.SetColumn(0, new Vector4(v.c0.x, v.c0.y, v.c0.z, v.c0.w)); r.Matrix.SetColumn(1, new Vector4(v.c1.x, v.c1.y, v.c1.z, v.c1.w));
                    r.Matrix.SetColumn(2, new Vector4(v.c2.x, v.c2.y, v.c2.z, v.c2.w)); r.Matrix.SetColumn(3, new Vector4(v.c3.x, v.c3.y, v.c3.z, v.c3.w));
                    renderers[key] = r;
                }
                r.Materials[Mathf.Max(0, (int)mmi.SubMesh)] = material;
                return true;
            }
            using (var chunks = entityQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (chunk.Has(filterHandle))
                    {
                        var settings = chunk.GetSharedComponent(filterHandle, em);
                        if ((cullingMask & (1 << settings.Layer)) == 0) { entityHidden += chunk.Count; continue; }
                        if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { entityShadowOnly += chunk.Count; continue; }
                    }
                    baked = chunk.Has(arrayHandle);
                    array = baked ? chunk.GetSharedComponentManaged(arrayHandle, em) : default;
                    MaterialMeshInfo* pairs = chunk.Has(setHandle) ? chunk.GetSharedComponent(setHandle, em).MaterialMeshInfos : null;
                    var info = chunk.GetNativeArray(ref infoHandle); var ltw = chunk.GetNativeArray(ref ltwHandle);
                    bool lod = chunk.Has(ref rangeHandle) && chunk.Has(ref referenceHandle);
                    var range = lod ? chunk.GetNativeArray(ref rangeHandle) : default;
                    var reference = lod ? chunk.GetNativeArray(ref referenceHandle) : default;
                    for (int i = 0; i < chunk.Count; i++)
                    {
                        float4x4 v = ltw[i].Value;
                        if (math.distancesq(v.c3.xyz, c) > r2) continue;
                        if (lod)
                        {
                            float d = math.distance(reference[i].Value, c) * lodScale;
                            if (d < range[i].MinDist || d >= range[i].MaxDist) continue;
                        }
                        var mmi = info[i];
                        bool added = false;
                        if (!mmi.HasMaterialMeshIndexRange) added = Add(mmi, v);
                        else if (pairs != null)
                        {
                            var span = mmi.MaterialMeshIndexRange;
                            for (int j = span.start; j < span.end; j++) added |= Add(pairs[j], v);
                        }
                        if (!added) ++entityUnresolved;
                    }
                }
            }
            int count = 0;
            foreach (var r in renderers.Values)
            {
                int handle = MeshHandle(r.Mesh, out int submeshes);
                if (handle <= 0) { if (!meshes.ContainsKey(r.Mesh.GetInstanceID())) ++entityPending; continue; }
                var list = new uint[submeshes];
                bool any = false;
                for (int s = 0; s < submeshes; s++)
                {
                    uint material = Invisible; // a submesh no entity draws (a transparent one drawn otherwise)
                    if (r.Materials.TryGetValue(s, out var m)) material = m != null ? MaterialIndex(m) : Neutral;
                    list[s] = material; any |= material != Invisible;
                }
                if (!any) continue;
                AddInstance(instances, indices, handle, r.Matrix, list);
                ++count;
            }
            covered = (entityUnresolved + entityPending) * 10 <= renderers.Count + entityUnresolved;
            if (count > 0 && !entitiesLogged)
            {
                entitiesLogged = true;
                Session.Log.LogInfo("Entities Graphics: " + count + " instances drawn without Drake traced near the camera (" + renderers.Count + " found, " +
                    meshCache.Count + " meshes from its registry)");
            }
            return count;
        }

        // ------------------------------------------------------------------ Medusa (Awaken's BatchRendererGroup renderer)
        // Some large static sets are drawn by Medusa, not Drake: per scene, renderers (a mesh's submeshes with their
        // materials, and the LODs they belong to) placed by instance matrices baked into a file in the game's Medusa
        // archive, which Medusa reads into a GPU buffer only. The matrices are read from that file once per scene
        // (Unity's async reader: the file is inside a mounted archive); each instance's LOD is chosen as Medusa chooses
        // it (the first whose squared distance, kept per instance, the scaled camera distance is under).
        private sealed class MedusaScene { internal Matrix4x4[] Matrices; }
        private static readonly Dictionary<int, MedusaScene> medusaScenes = new Dictionary<int, MedusaScene>();
        private static int CaptureMedusa(Vector3 camera, float radius)
        {
            int count = 0;
            var cam = Camera.main;
            float scale = cam != null ? 2f * Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad) / Mathf.Max(QualitySettings.lodBias, 0.01f) : 1f;
            float scaleSq = scale * scale;
            foreach (var manager in UnityEngine.Object.FindObjectsByType<Awaken.ECS.MedusaRenderer.MedusaRendererManager>(FindObjectsSortMode.None))
            {
                if (manager == null || !manager.isActiveAndEnabled) continue;
                var access = new Awaken.ECS.MedusaRenderer.MedusaRendererManager.EditorAccess(manager);
                var brg = access.BrgRenderer;
                if (brg.IsNull || !brg.IsValid) continue;
                var renderers = access.Renderers;
                int transforms = access.TransformsCount;
                if (renderers == null || transforms <= 0) continue;
                var matrices = MedusaMatrices(manager, brg.BasePath, transforms, renderers.Length);
                if (matrices == null) continue;
                var xs = brg.Xs; var ys = brg.Ys; var zs = brg.Zs; var radii = brg.Radii;
                for (uint r = 0; r < (uint)renderers.Length; r++)
                {
                    var data = renderers[r].renderData;
                    if (data == null || data.Count == 0) continue;
                    int lodMask = renderers[r].lodMask;
                    var placed = brg.TransformIndices(r);
                    for (uint k = 0; k < placed.Length; k++)
                    {
                        uint t = placed[k];
                        if (t >= (uint)transforms) continue;
                        float dx = xs[t] - camera.x, dy = ys[t] - camera.y, dz = zs[t] - camera.z;
                        float d2 = dx * dx + dy * dy + dz * dz, reach = radius + radii[t];
                        if (d2 > reach * reach) continue;
                        // Medusa's LOD: the lowest one whose distance the camera is within (none: its bit shift wraps to LOD 0).
                        float scaled = d2 * scaleSq; int lod = 0;
                        for (byte l = 0; l < 8; l++) if (scaled < brg.LodDistanceSq(t, l)) { lod = l; break; }
                        if ((lodMask & (1 << lod)) == 0) continue;
                        count += AddMedusaInstance(data, matrices[t]);
                    }
                }
            }
            return count;
        }
        // One instance per mesh of a Medusa renderer: its submeshes with the materials the renderer draws them with,
        // the others invisible to rays.
        private static int AddMedusaInstance(List<Awaken.ECS.MedusaRenderer.RenderDatum> data, Matrix4x4 world)
        {
            int added = 0;
            for (int i = 0; i < data.Count; i++)
            {
                var mesh = data[i].mesh;
                if (mesh == null) continue;
                bool first = true;
                for (int j = 0; j < i && first; j++) if (data[j].mesh == mesh) first = false;
                if (!first) continue;
                int handle = MeshHandle(mesh, out int submeshes);
                if (handle <= 0) continue;
                var list = new uint[submeshes];
                for (int s = 0; s < submeshes; s++) list[s] = Invisible;
                bool any = false;
                for (int j = i; j < data.Count; j++)
                {
                    if (data[j].mesh != mesh || data[j].subMeshIndex >= submeshes) continue;
                    list[data[j].subMeshIndex] = MaterialIndex(data[j].material);
                    any |= list[data[j].subMeshIndex] != Invisible;
                }
                if (!any) continue;
                AddInstance(sinkInstances, sinkIndices, handle, world, list);
                ++added;
            }
            return added;
        }
        // A scene's instance matrices (PackedMatrix: the 3x4 object-to-world, column by column, 48 bytes each), once.
        private static unsafe Matrix4x4[] MedusaMatrices(Awaken.ECS.MedusaRenderer.MedusaRendererManager manager, string basePath, int transforms, int renderers)
        {
            int id = manager.GetInstanceID();
            if (medusaScenes.TryGetValue(id, out var known)) return known.Matrices;
            var scene = new MedusaScene();
            medusaScenes[id] = scene;
            string path = Awaken.ECS.MedusaRenderer.MedusaPersistence.MatricesPath(basePath);
            var buffer = new NativeArray<float>(transforms * 12, Allocator.Persistent, NativeArrayOptions.UninitializedMemory);
            try
            {
                var command = new Unity.IO.LowLevel.Unsafe.ReadCommand
                {
                    Offset = 0, Size = (long)transforms * 48, Buffer = Unity.Collections.LowLevel.Unsafe.NativeArrayUnsafeUtility.GetUnsafePtr(buffer)
                };
                var read = Unity.IO.LowLevel.Unsafe.AsyncReadManager.Read(path, &command, 1);
                read.JobHandle.Complete();
                var status = read.Status;
                read.Dispose();
                Unity.IO.LowLevel.Unsafe.AsyncReadManager.CloseCachedFileAsync(path).Complete();
                if (status != Unity.IO.LowLevel.Unsafe.ReadStatus.Complete)
                {
                    Session.Log.LogWarning("Medusa (" + manager.gameObject.scene.name + "): its instance matrices could not be read (" + status + "); its buildings stay out of the traced scene");
                    return null;
                }
                var matrices = new Matrix4x4[transforms];
                for (int t = 0; t < transforms; t++)
                {
                    int b = t * 12;
                    matrices[t] = new Matrix4x4(new Vector4(buffer[b], buffer[b + 1], buffer[b + 2], 0f), new Vector4(buffer[b + 3], buffer[b + 4], buffer[b + 5], 0f),
                                                new Vector4(buffer[b + 6], buffer[b + 7], buffer[b + 8], 0f), new Vector4(buffer[b + 9], buffer[b + 10], buffer[b + 11], 1f));
                }
                scene.Matrices = matrices;
                Session.Log.LogInfo("Medusa (" + manager.gameObject.scene.name + "): " + transforms + " instances of " + renderers + " renderers, traced near the camera");
                return matrices;
            }
            finally { buffer.Dispose(); }
        }

        // ------------------------------------------------------------------ Probe: what Drake and Medusa have at a point
        private static bool Around(Unity.Mathematics.AABB b, float3 p, float margin) => math.all(math.abs(p - b.Center) <= b.Extents + margin);
        internal static unsafe void ProbeDrake(Vector3 at, Vector3 eye, System.Text.StringBuilder sb, float margin, int maxLines)
        {
            var world = World.DefaultGameObjectInjectionWorld;
            var manager = DrakeRendererManager.Instance;
            if (world == null || !world.IsCreated || manager == null) { sb.Append("\n  Drake: no world"); return; }
            var em = world.EntityManager;
            var cam = Camera.main;
            float lodScale = cam != null ? 2f * Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad) / Mathf.Max(QualitySettings.lodBias, 0.01f) : 1f;
            var meshData = manager.LoadingManager.MeshLoadingData;
            float3 p = at, c = eye;
            int found = 0;
            using (var query = em.CreateEntityQuery(ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<WorldRenderBounds>()))
            using (var entities = query.ToEntityArray(Allocator.Temp))
            {
                foreach (var e in entities)
                {
                    if (!Around(em.GetComponentData<WorldRenderBounds>(e).Value, p, margin)) continue;
                    if (++found > maxLines) continue;
                    var mm = em.GetComponentData<DrakeMeshMaterialComponent>(e);
                    Mesh mesh = mm.meshIndex < meshData.Count ? Loaded(meshData[mm.meshIndex].loadingHandle, meshData[mm.meshIndex].counter) : null;
                    string lod;
                    if (em.HasComponent<LODRange>(e) && em.HasComponent<LODWorldReferencePoint>(e))
                    {
                        var range = em.GetComponentData<LODRange>(e);
                        float d = math.distance(em.GetComponentData<LODWorldReferencePoint>(e).Value, c) * lodScale;
                        lod = ", LOD mask " + range.LODMask + " for " + range.MinDist.ToString("0.#") + "-" + range.MaxDist.ToString("0.#") + " at " + d.ToString("0.#") +
                            (d >= range.MinDist && d < range.MaxDist ? " (traced)" : " (not this LOD)");
                    }
                    else if (em.HasComponent<LODRange>(e)) lod = ", a LOD range without a reference point (not captured)";
                    else if (em.HasComponent<DrakeRendererVisibleRangeComponent>(e))
                    {
                        var v = em.GetComponentData<DrakeRendererVisibleRangeComponent>(e).value;
                        float d2 = math.distancesq(em.GetComponentData<LocalToWorld>(e).Value.c3.xyz, c);
                        lod = ", visible " + math.sqrt(v.x).ToString("0.#") + "-" + math.sqrt(v.y).ToString("0.#") + " m at " + math.sqrt(d2).ToString("0.#") +
                            (d2 >= v.x && d2 < v.y ? " (traced)" : " (out of range)");
                    }
                    else lod = ", no LOD";
                    string shadows = em.HasComponent<Unity.Entities.Graphics.RenderFilterSettings>(e) ? em.GetSharedComponent<Unity.Entities.Graphics.RenderFilterSettings>(e).ShadowCastingMode.ToString() : "no filter settings";
                    sb.Append("\n  Drake mesh \"" + (mesh != null ? mesh.name : mm.meshIndex < meshData.Count ? "(not loaded: not traced)" : "(no such mesh)") + "\" submesh " + mm.submesh + ", " + shadows +
                        (em.HasComponent<MaterialMeshInfo>(e) ? ", drawn" : ", not loaded for drawing") + lod +
                        (mesh != null && meshes.TryGetValue(mesh.GetInstanceID(), out var entry) ? (entry.Failed ? ", its mesh failed to register" : "") : mesh != null ? ", its mesh not registered yet" : ""));
                }
            }
            sb.Append("\n  Drake entities there: " + found);
            // Entities Graphics entities drawn without Drake (CaptureEntities).
            var graphics = world.GetExistingSystemManaged<EntitiesGraphicsSystem>();
            int plain = 0;
            using (var query = em.CreateEntityQuery(new EntityQueryDesc
            {
                All = new[] { ComponentType.ReadOnly<MaterialMeshInfo>(), ComponentType.ReadOnly<WorldRenderBounds>() },
                None = new[] { ComponentType.ReadOnly<DrakeMeshMaterialComponent>() }
            }))
            using (var entities = query.ToEntityArray(Allocator.Temp))
            {
                foreach (var e in entities)
                {
                    if (!Around(em.GetComponentData<WorldRenderBounds>(e).Value, p, margin)) continue;
                    if (++plain > maxLines) continue;
                    string text;
                    try
                    {
                        var mmi = em.GetComponentData<MaterialMeshInfo>(e);
                        Mesh mesh = null; Material material = null;
                        int pairCount = 1;
                        if (mmi.HasMaterialMeshIndexRange && graphics != null && em.HasComponent<Unity.Entities.Graphics.RendererRange>(e))
                        {
                            // The first pair names it; the range's length says how many submeshes it draws.
                            var pairs = em.GetSharedComponent<Unity.Entities.Graphics.RendererRange>(e).MaterialMeshInfos;
                            var span = mmi.MaterialMeshIndexRange;
                            if (pairs != null && span.length > 0)
                            {
                                mmi = pairs[span.start]; pairCount = span.length;
                                mesh = graphics.GetMesh(mmi.MeshID); material = graphics.GetMaterial(mmi.MaterialID);
                            }
                        }
                        else if (!mmi.HasMaterialMeshIndexRange)
                        {
                            if (mmi.IsRuntimeMesh) mesh = graphics != null ? graphics.GetMesh(mmi.MeshID) : null;
                            else if (em.HasComponent<RenderMeshArray>(e)) mesh = em.GetSharedComponentManaged<RenderMeshArray>(e).GetMesh(mmi);
                            if (mmi.Material >= 0) material = graphics != null ? graphics.GetMaterial(mmi.MaterialID) : null;
                            else if (em.HasComponent<RenderMeshArray>(e)) material = em.GetSharedComponentManaged<RenderMeshArray>(e).GetMaterial(mmi);
                        }
                        string shadows = em.HasComponent<Unity.Entities.Graphics.RenderFilterSettings>(e) ? em.GetSharedComponent<Unity.Entities.Graphics.RenderFilterSettings>(e).ShadowCastingMode.ToString() : "no filter settings";
                        string lod = "";
                        if (em.HasComponent<LODRange>(e) && em.HasComponent<LODWorldReferencePoint>(e))
                        {
                            var range = em.GetComponentData<LODRange>(e);
                            float d = math.distance(em.GetComponentData<LODWorldReferencePoint>(e).Value, c) * lodScale;
                            lod = ", LOD " + range.MinDist.ToString("0.#") + "-" + range.MaxDist.ToString("0.#") + " at " + d.ToString("0.#") + (d >= range.MinDist && d < range.MaxDist ? "" : " (not this LOD)");
                        }
                        text = mmi.HasMaterialMeshIndexRange ? "(a material-mesh range without its RendererRange: not traced)\"" :
                            (mesh != null ? mesh.name : "(unresolved: not traced)") + "\" submesh " + mmi.SubMesh + (pairCount > 1 ? " (and " + (pairCount - 1) + " more)" : "") + ", material \"" + (material != null ? material.name + " (" + material.shader.name + ")" : "none") + "\", " + shadows + lod +
                            (mesh != null && meshes.TryGetValue(mesh.GetInstanceID(), out var entry) ? (entry.Failed ? ", its mesh failed to register" : ", traced") : mesh != null ? ", its mesh not registered yet" : "");
                    }
                    catch (Exception ex) { text = "(" + ex.GetType().Name + ": " + ex.Message + ")\""; }
                    sb.Append("\n  entity (Entities Graphics, not Drake) mesh \"" + text);
                }
            }
            sb.Append("\n  Entities Graphics entities without Drake there: " + plain + " (of " + em.CreateEntityQuery(ComponentType.ReadOnly<MaterialMeshInfo>()).CalculateEntityCount() + " drawn entities in the world)");
        }
        internal static void ProbeMedusa(Vector3 at, Vector3 eye, System.Text.StringBuilder sb, float margin, int maxLines)
        {
            var cam = Camera.main;
            float scale = cam != null ? 2f * Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad) / Mathf.Max(QualitySettings.lodBias, 0.01f) : 1f;
            int found = 0;
            foreach (var manager in UnityEngine.Object.FindObjectsByType<Awaken.ECS.MedusaRenderer.MedusaRendererManager>(FindObjectsSortMode.None))
            {
                if (manager == null) continue;
                var access = new Awaken.ECS.MedusaRenderer.MedusaRendererManager.EditorAccess(manager);
                var brg = access.BrgRenderer;
                if (brg.IsNull || !brg.IsValid) continue;
                var renderers = access.Renderers;
                int transforms = access.TransformsCount;
                if (renderers == null || transforms <= 0) continue;
                var matrices = MedusaMatrices(manager, brg.BasePath, transforms, renderers.Length);
                var xs = brg.Xs; var ys = brg.Ys; var zs = brg.Zs; var radii = brg.Radii;
                for (uint r = 0; r < (uint)renderers.Length; r++)
                {
                    var data = renderers[r].renderData;
                    var placed = brg.TransformIndices(r);
                    for (uint k = 0; k < placed.Length; k++)
                    {
                        uint t = placed[k];
                        if (t >= (uint)transforms) continue;
                        var centre = new Vector3(xs[t], ys[t], zs[t]);
                        if ((centre - at).magnitude > radii[t] + margin) continue;
                        if (++found > maxLines) continue;
                        float scaled = (centre - eye).sqrMagnitude * scale * scale; int lod = 0;
                        for (byte l = 0; l < 8; l++) if (scaled < brg.LodDistanceSq(t, l)) { lod = l; break; }
                        var names = new List<string>();
                        if (data != null) foreach (var d in data) if (d.mesh != null && !names.Contains(d.mesh.name)) names.Add(d.mesh.name);
                        string placement = matrices != null ? ", its matrix " + ((Vector3)matrices[t].GetColumn(3) - centre).magnitude.ToString("0.0") + " m from its bounds' centre" : ", no matrices";
                        sb.Append("\n  Medusa (" + manager.gameObject.scene.name + ") \"" + string.Join(", ", names.ToArray()) + "\", LOD " + lod + " of mask " + renderers[r].lodMask +
                            (((renderers[r].lodMask >> lod) & 1) != 0 ? " (traced)" : " (not this LOD)") + placement + ", radius " + radii[t].ToString("0.0"));
                    }
                }
            }
            sb.Append("\n  Medusa instances there: " + found);
        }

        // ------------------------------------------------------------------ terrain
        private static int CaptureTerrains(Vector3 camera, float radius, List<NativeInstance> instances, List<uint> indices)
        {
            int count = 0;
            foreach (var t in Terrain.activeTerrains)
            {
                if (t == null || !t.isActiveAndEnabled || t.terrainData == null) continue;
                var data = t.terrainData;
                var origin = t.transform.position;
                var bounds = new Bounds(origin + data.size * 0.5f, data.size);
                if (bounds.SqrDistance(camera) > radius * radius) continue;
                int id = t.GetInstanceID();
                if (!terrains.TryGetValue(id, out int handle))
                {
                    handle = BuildTerrain(t);
                    terrains[id] = handle;
                }
                if (handle <= 0) continue;
                AddInstance(instances, indices, handle, Matrix4x4.Translate(origin), new[] { TerrainMaterial });
                ++count;
            }
            return count;
        }
        private static uint terrainMaterial = uint.MaxValue;
        private static uint TerrainMaterial
        {
            get
            {
                if (terrainMaterial == uint.MaxValue)
                {
                    materialList.Add(new NativeMaterial { BaseR = 1, BaseG = 1, BaseB = 1, BaseA = 1, ScaleX = 1, ScaleY = 1, AlbedoTile = -1, EmissiveTile = -1, Flags = Tgpt.MaterialVertexColor });
                    terrainMaterial = (uint)(materialList.Count - 1);
                }
                return terrainMaterial;
            }
        }
        // A grid of at most 257x257 vertices over the heightmap, colored by the terrain layers' weights: each
        // layer's color is its diffuse remap's midpoint tinted towards earth, since the layers' textures can't be read here.
        private static int BuildTerrain(Terrain t)
        {
            var data = t.terrainData;
            int res = Mathf.Min(data.heightmapResolution, 257);
            if (res < 2) return -1;
            var size = data.size;
            var heights = data.GetInterpolatedHeights(0f, 0f, res, res, 1f / (res - 1), 1f / (res - 1));
            var positions = new float[res * res * 3];
            var colors = new uint[res * res];
            float[,,] alpha = null; int aw = 0, ah = 0, layers = 0;
            try { aw = data.alphamapWidth; ah = data.alphamapHeight; layers = data.alphamapLayers; if (layers > 0) alpha = data.GetAlphamaps(0, 0, aw, ah); } catch { alpha = null; }
            var layerColors = new Vector3[Mathf.Max(layers, 1)];
            var terrainLayers = data.terrainLayers;
            for (int l = 0; l < layerColors.Length; l++)
            {
                var tint = new Vector3(0.22f, 0.21f, 0.15f);
                if (terrainLayers != null && l < terrainLayers.Length && terrainLayers[l] != null)
                {
                    var remap = (terrainLayers[l].diffuseRemapMin + terrainLayers[l].diffuseRemapMax) * 0.5f;
                    tint = Vector3.Scale(tint, new Vector3(Mathf.Clamp(remap.x * 2f, 0.3f, 1.5f), Mathf.Clamp(remap.y * 2f, 0.3f, 1.5f), Mathf.Clamp(remap.z * 2f, 0.3f, 1.5f)));
                }
                layerColors[l] = tint;
            }
            for (int z = 0; z < res; z++)
                for (int x = 0; x < res; x++)
                {
                    int i = z * res + x;
                    positions[i * 3] = (float)x / (res - 1) * size.x;
                    positions[i * 3 + 1] = heights[z, x];
                    positions[i * 3 + 2] = (float)z / (res - 1) * size.z;
                    var color = layerColors[0];
                    if (alpha != null)
                    {
                        int ax = Mathf.Clamp(x * (aw - 1) / (res - 1), 0, aw - 1), az = Mathf.Clamp(z * (ah - 1) / (res - 1), 0, ah - 1);
                        color = Vector3.zero;
                        for (int l = 0; l < layers; l++) color += layerColors[l] * alpha[az, ax, l];
                    }
                    colors[i] = Pack(color);
                }
            var tris = new uint[(res - 1) * (res - 1) * 6];
            int k = 0;
            for (int z = 0; z < res - 1; z++)
                for (int x = 0; x < res - 1; x++)
                {
                    uint a = (uint)(z * res + x), b = a + 1, c = a + (uint)res, d = c + 1;
                    tris[k++] = a; tris[k++] = c; tris[k++] = b; tris[k++] = b; tris[k++] = c; tris[k++] = d;
                }
            int handle = CpuMesh(t.GetInstanceID(), positions, null, colors, tris);
            Session.Log.LogInfo("Terrain \"" + t.name + "\" traced as a " + res + "x" + res + " grid over " + size.x.ToString("0") + " x " + size.z.ToString("0") + " m (" + (handle > 0 ? "registered" : "refused " + handle) + ").");
            return handle;
        }
        private static uint Pack(Vector3 c) =>
            (uint)Mathf.RoundToInt(Mathf.Clamp01(c.x) * 255f) | ((uint)Mathf.RoundToInt(Mathf.Clamp01(c.y) * 255f) << 8) | ((uint)Mathf.RoundToInt(Mathf.Clamp01(c.z) * 255f) << 16) | 0xFF000000u;
        private static ulong Ptr(IntPtr p) => unchecked((ulong)p.ToInt64());
    }
}
