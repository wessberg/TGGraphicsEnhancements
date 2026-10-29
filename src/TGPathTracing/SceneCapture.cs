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
        // Sooner once the camera has moved, or what the game scales LOD distances by has changed (a zoom, the Draw Distance
        // row): the levels chosen for where it was would stand in for the ones now drawn (PT-61: the traced level is the
        // drawn one). A metre: with thousands of instances in reach any step crosses some switch distance.
        private const float MovedScanInterval = 0.5f, MovedDistance = 1f;
        private static float lastScan, lastScanScale;
        private static Vector3 lastScanCamera;
        // The LOD bias and the maximum LOD level HDRP culls the camera with, and its field of view: HDRP sets the quality
        // settings to its camera's frame settings for the length of its culling (HDRenderPipeline.TryCull), which is when
        // Unity's LOD groups, Entities Graphics and Medusa read them. The game writes its Draw Distance row into both.
        private static float drawnLodBias = 1f, drawnFov = 60f;
        private static int drawnMaximumLod;
        private static void ReadDrawnLod()
        {
            var cam = Camera.main;
            drawnLodBias = QualitySettings.lodBias; drawnMaximumLod = QualitySettings.maximumLODLevel;
            if (cam == null) return;
            drawnFov = cam.fieldOfView;
            try
            {
                if (GraphicsSettings.currentRenderPipeline is UnityEngine.Rendering.HighDefinition.HDRenderPipelineAsset asset)
                {
                    var frame = UnityEngine.Rendering.HighDefinition.HDCamera.GetOrCreate(cam).frameSettings;
                    drawnLodBias = frame.GetResolvedLODBias(asset); drawnMaximumLod = frame.GetResolvedMaximumLODLevel(asset);
                }
            }
            catch (Exception) { } // the quality settings then: what the game's own watcher keeps equal to them
        }
        // What a distance is multiplied by before it is compared with a LOD range (Entities Graphics, Drake, Medusa).
        private static float DrawnScale => WorldRules.LodDistanceScale(drawnFov, drawnLodBias);
        // Per scan, for the log: LOD groups traced at another level while the drawn one's meshes register; statically batched
        // renderers traced and the combined meshes they are in; Drake entities with a LOD range and no reference point.
        private static int lodStandIns, batchedTraced, batchesTraced, batchedWaiting, drakeStray;
        private static int drakeShadowOnly, drakeHidden, objectsLeftOut, objectsStandIn;
        // Whether shadow-only meshes stand in for drawn geometry: only while Entities Graphics' entities can't be read.
        private static bool standIns = true;
        private static readonly HashSet<string> undrawnSeen = new HashSet<string>();
        // Registering a mesh reads its native buffers, which waits for Unity's render thread: a few a frame at the game's
        // own frame rate. By the clock, not by the frame (PT-98; the owner, 2026-10-09, of a guard's textures and a town
        // square filling in: "it took a LONG time before it kicked in"): at the 8 frames a second the tracer ran at, 6 a
        // frame were 45 meshes a second, and the square's 1,533 took over half a minute. So: MeshesPerFrame at least, and
        // on until a share of the last frame's time is spent (MeshShare): a slow frame takes in what a second of fast ones
        // would have, and the world is whole about as soon whatever the frame rate.
        // The share is counted from the END of the frame's first mesh (the same day, the second look): the first call into
        // Unity of a frame waits until the render thread has finished the frame it is drawing, which at 8 frames a second
        // is most of a frame, so a clock started before it has spent its tenth before the second mesh, and the limit is
        // still 6 a frame. The meshes after the first cost about a tenth of a millisecond each at most (run
        // 20261009-154425: a town's first 1,190 were registered within 1.2 s of fast frames, in a tenth of each; the
        // timeline's line gives the time: Arrival.Registered), so a tenth of the frame is about 1,000 meshes a second at
        // any frame rate. While meshes wait a frame is up to a tenth longer; when none waits it costs nothing.
        private const int MeshesPerFrame = 6;
        private const float MeshShare = 0.1f;
        // A mesh's instances are listed by the first scan after it has a handle. While registered meshes wait for that
        // (Arrival.ToPlace), the next scan comes ArrivalScanInterval after the last began, and never sooner than
        // ArrivalScanRest times what the last scan took (so scans take a quarter of the time at most while the world
        // arrives): it was the 2 s of ScanInterval whatever was waiting, and a town needs three scans to be whole.
        private const float ArrivalScanInterval = 0.25f, ArrivalScanRest = 4f;
        private static float lastScanSeconds;
        // The textures traced materials read are looked at this often (TextureShare.Refresh), not only once a scan: a
        // texture the game has just loaded sharp was read from its 64 x 64 tile until the next scan saw it.
        private const float TextureLookInterval = 0.25f;
        private static float nextTextureLook;
        // For measuring (the hidden "Arrival as before", Settings): the three rules above as they were until 2026-10-09
        // 16:00 (the share counted from the frame's start, a scan every ScanInterval whatever waits, the textures looked at
        // once a scan), so that one run of one scene shows the world arrive both ways. It goes when the game has answered.
        private static bool ArrivalAsBefore => Session.Settings != null && Session.Settings.ArrivalAsBefore.Value != 0;
        private static readonly System.Diagnostics.Stopwatch meshClock = new System.Diagnostics.Stopwatch();
        private struct MeshEntry { internal int Handle; internal int Submeshes; internal bool Failed; internal uint Seen; }
        private static uint meshScan;
        private static readonly Dictionary<int, MeshEntry> meshes = new Dictionary<int, MeshEntry>();
        private static readonly Dictionary<int, MeshEntry> decalMeshes = new Dictionary<int, MeshEntry>();
        private static readonly Dictionary<int, uint> materials = new Dictionary<int, uint>();
        private static readonly List<NativeMaterial> materialList = new List<NativeMaterial>();
        private static int materialsUploaded;
        private static readonly Dictionary<int, int> tiles = new Dictionary<int, int>();
        private static readonly Dictionary<int, int> detailTiles = new Dictionary<int, int>(); // see-through glass's, by texture
        // The hair materials that hold a tile (HairTile), by instance: the material (Unity's null once it is destroyed) and the
        // frame the tracer last read it in (HairTileSeen: every frame its character is traced). Detail tiles are a pool, 128
        // on the native side, where hair of the same maps and values shares one: when hair or see-through glass finds none
        // free, the hair that is gone or was not traced for HairTileIdleFrames gives its tile back (ReleaseIdleHairTiles).
        private struct HairHold { internal Material Material; internal int Seen; }
        private static readonly Dictionary<int, HairHold> hairHolds = new Dictionary<int, HairHold>();
        private const int HairTileIdleFrames = 600;
        private static readonly Dictionary<long, int> alphaTiles = new Dictionary<long, int>(); // by texture and cutoff
        private static bool alphaFullLogged; // once a session: the evidence whether the alpha atlas's 1,024 tiles are ever filled (PT-50)
        private static readonly Dictionary<long, int> maskTiles = new Dictionary<long, int>(); // by texture and channels
        private static readonly Dictionary<int, uint> characterMaterials = new Dictionary<int, uint>();
        // The hero's first-person body's materials (FirstPersonMaterial), by the character material each copies.
        private static readonly Dictionary<uint, uint> firstPersonMaterials = new Dictionary<uint, uint>();
        // Hair's colors by its material's index, read again every frame (RefreshHairColor): the game colors a character's hair
        // on its own clone of the material, sometimes after the character first shows. Read once, a far character's hair kept
        // the clone's default grey (_DiffuseColor 0.698) until it came near and another clone was read.
        private static readonly Dictionary<uint, HairState> hairColors = new Dictionary<uint, HairState>();
        // Hair materials whose color is their shader's, baked into a tile of their own (HairTile).
        private static readonly HashSet<uint> bakedHair = new HashSet<uint>();
        // Scalp and beard masks the tracer draws under the hair (LayerShell): the color the game gave each (_BaseColor).
        private static readonly Dictionary<uint, Color> shellColors = new Dictionary<uint, Color>();
        private static readonly HashSet<string> hairColorLogged = new HashSet<string>();
        // Hair materials by instance, and whether they were left out (transparent) when last read: the game switches a
        // character's hair between transparent and opaque with its distance (CharacterMaterial reads it again then).
        private static readonly Dictionary<int, bool> hairSeen = new Dictionary<int, bool>();
        private static int hairFlipsLogged;
        private static readonly HashSet<string> characterShadersSeen = new HashSet<string>();
        private static readonly string[] StrandNames = { "Hair", "Lash", "Brow", "Beard", "Fur", "Mustache", "Moustache" };
        private static readonly HashSet<string> unlitSeen = new HashSet<string>();
        private static readonly Dictionary<int, int> terrains = new Dictionary<int, int>();
        private static readonly HashSet<string> shadersSeen = new HashSet<string>();
        private static float nextScan;
        private static readonly Queue<Mesh> meshQueue = new Queue<Mesh>();
        // Meshes seen in the game and not yet handed to the tracer (the notice while the traced world is built).
        internal static int Waiting => meshQueue.Count;
        // For the arrival's timeline (Arrival.cs, PT-98): the meshes seen so far (registered, failed or waiting), the materials made.
        internal static int MeshesKnown => meshes.Count + meshQueue.Count;
        internal static int MaterialsKnown => materialList.Count;
        private static readonly HashSet<int> queued = new HashSet<int>();
        private static EntityQuery lodQuery, rangeQuery, plainQuery, strayQuery, entityQuery;
        private static World queryWorld;
        // The scan's line, and the things the game moves, placed every frame (Follow).
        internal static string Summary => scanSummary + FollowSummary;
        private static string scanSummary = "no scan yet";
        internal static int InstanceCount { get; private set; }
        // Material 0: rays pass through (transparent submeshes); material 1: neutral grey (a submesh with no material).
        private const uint Invisible = 0, Neutral = 1;
        // The traced instances that glow (an emissive material), each with its world bounds, from the last scan: SceneLights keeps
        // a light the game placed at one as that glowing thing's light (PT-47). A mesh's bounds by its handle.
        internal static List<GlowThing> Glows { get; private set; } = new List<GlowThing>();
        private static List<GlowThing> glowsNext = new List<GlowThing>();
        private static readonly Dictionary<int, Bounds> meshBounds = new Dictionary<int, Bounds>();
        // The glowing instances a kept light stands for (SceneLights, by OwnSurfaceKey): their emissive materials are traced as
        // that light's own surface (TGPT_MATERIAL_OWN_SURFACE), which lights nothing itself, so its light isn't counted twice
        // where the light sits farther from it than the native radius rule (the light's radius and 25 cm) reaches.
        internal static HashSet<long> OwnSurfaces = new HashSet<long>();
        private static readonly Dictionary<uint, uint> ownVariants = new Dictionary<uint, uint>();
        internal static long OwnSurfaceKey(int handle, float x, float y, float z) =>
            ((long)handle << 36) ^ ((long)Mathf.RoundToInt(x * 100f) * 73856093L) ^ ((long)Mathf.RoundToInt(y * 100f) * 19349663L) ^ ((long)Mathf.RoundToInt(z * 100f) * 83492791L);

        // Adds one instance of a mesh with a renderer's materials; false when it isn't traceable yet (or at all).
        internal delegate bool Sink(Mesh mesh, Material[] materials, Matrix4x4 world);
        private static List<NativeInstance> sinkInstances;
        private static List<uint> sinkIndices;
        private static bool AddMesh(Mesh mesh, Material[] shared, Matrix4x4 world) => AddMesh(mesh, shared, world, 0u);
        // A mesh's submesh materials, worked out once per material array (vegetation adds tens of thousands of
        // instances of a few prefabs a scan).
        private static readonly Dictionary<(int, Material[]), uint[]> materialLists = new Dictionary<(int, Material[]), uint[]>();
        private static bool AddMesh(Mesh mesh, Material[] shared, Matrix4x4 world, uint flags, uint renderingLayers = 1u)
        {
            if (shared == null || shared.Length == 0) return false;
            int handle = MeshHandle(mesh, out int submeshes);
            if (handle <= 0) return false;
            var list = SubmeshMaterials(handle, submeshes, shared);
            if (list.Length == 0) return false;
            AddInstance(sinkInstances, sinkIndices, handle, world, list, flags, renderingLayers);
            return true;
        }
        // A thing the game moves, as AddMesh adds a still one: placed for this frame (Follow), its identity naming it from frame
        // to frame (its previous transform).
        private static bool PlaceMesh(Mesh mesh, Material[] shared, in Matrix4x4 world, uint flags, long identity, uint renderingLayers = 1u)
        {
            if (shared == null || shared.Length == 0) return false;
            int handle = MeshHandle(mesh, out int submeshes);
            if (handle <= 0) return false;
            var list = SubmeshMaterials(handle, submeshes, shared);
            if (list.Length == 0) return false;
            Place(handle, world, list, flags, identity, renderingLayers);
            return true;
        }
        // A mesh's submeshes' materials for a renderer's array (empty: none of them is seen by rays).
        private static uint[] SubmeshMaterials(int handle, int submeshes, Material[] shared)
        {
            var key = (handle, shared);
            if (!materialLists.TryGetValue(key, out var list) || list.Length != submeshes)
            {
                list = new uint[submeshes];
                bool visible = false;
                for (int s = 0; s < submeshes; s++) { list[s] = MaterialIndex(shared[Mathf.Min(s, shared.Length - 1)]); visible |= list[s] != Invisible; }
                if (!visible) list = new uint[0];
                materialLists[key] = list;
            }
            return list;
        }

        // HDRP's water in the traced scene (WaterCaustics): met by the tracer's water query alone, its slot naming the surface
        // whose caustic pattern it carries. Its material is never read.
        private static bool AddWater(Mesh mesh, Matrix4x4 world, int slot)
        {
            int handle = MeshHandle(mesh, out int submeshes);
            if (handle <= 0 || submeshes <= 0) return false;
            var list = new uint[submeshes];
            for (int s = 0; s < submeshes; s++) list[s] = Neutral;
            AddInstance(sinkInstances, sinkIndices, handle, world, list, Tgpt.InstanceWater | ((uint)slot << 8));
            return true;
        }

        internal static void Reset()
        {
            VegetationCapture.Reset(); LeshyCapture.Reset(); WaterCaustics.Reset(); materialLists.Clear();
            meshes.Clear(); decalMeshes.Clear(); materials.Clear(); materialList.Clear(); tiles.Clear(); detailTiles.Clear(); hairHolds.Clear(); alphaTiles.Clear(); maskTiles.Clear(); characterMaterials.Clear(); firstPersonMaterials.Clear(); hairColors.Clear(); bakedHair.Clear(); shellColors.Clear(); eyeTiles.Clear(); hairSeen.Clear(); terrains.Clear(); meshQueue.Clear(); queued.Clear();
            GlassFinish.Reset();
            TextureShare.Reset();
            MaterialPrograms.Reset();
            exposureWeighted.Clear(); exposureFollowed = float.NaN; ResetFollow();
            clothWinds.Clear(); clothWindArray = new NativeClothWind[0];
            PlantWind.Reset();
            Glows = new List<GlowThing>(); glowsNext.Clear(); meshBounds.Clear(); OwnSurfaces = new HashSet<long>(); ownVariants.Clear();
            terrainMaterial = uint.MaxValue;
            materialsUploaded = 0; nextScan = 0; lastScan = 0; InstanceCount = 0; lastScanSeconds = 0; nextTextureLook = 0;
            medusaScenes.Clear();
            queryWorld = null;
        }

        internal static void Tick(float now, Vector3 camera)
        {
            RegisterQueued();
            FollowExposure();
            ReadDrawnLod();
            float scale = DrawnScale;
            bool moved = now >= lastScan + MovedScanInterval && WorldRules.LevelsMayHaveChanged((camera - lastScanCamera).sqrMagnitude, lastScanScale, scale, MovedDistance);
            // PT-98: registered meshes wait for a scan to list their instances.
            bool asBefore = ArrivalAsBefore;
            bool toPlace = !asBefore && Arrival.ToPlace > 0 && now >= lastScan + Mathf.Max(ArrivalScanInterval, ArrivalScanRest * lastScanSeconds);
            if (now < nextScan && !moved && !toPlace)
            {
                // Between scans: the textures, by the clock (and the program records a swap wrote into).
                if (!asBefore && now >= nextTextureLook && Session.Tracer != null && Session.Tracer.Connected)
                {
                    nextTextureLook = now + TextureLookInterval;
                    TextureShare.Refresh(materialList, materialsUploaded, Session.Tracer, false);
                    MaterialPrograms.Flush(Session.Tracer);
                }
                return;
            }
            nextScan = now + ScanInterval; nextTextureLook = now + TextureLookInterval;
            lastScan = now; lastScanCamera = camera; lastScanScale = scale;
            lodStandIns = 0; batchedTraced = 0; batchesTraced = 0; batchedWaiting = 0; drakeStray = 0;
            ++meshScan;
            var watch = Stopwatch.StartNew();
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            Arrival.ScanBegins(meshQueue.Count, materialList.Count); // PT-98: the timeline counts what this scan meets and finds new
            MaterialPrograms.ScanBegins();                           // PT-104: and what it meets that is drawn flat
            EnsureDefaults();
            var settings = Session.Settings;
            var instances = new List<NativeInstance>(4096);
            var indices = new List<uint>(8192);
            int renderers = 0, drake = 0, entities = 0, medusa = 0, terrain = 0, vegetation = 0, water = 0;
            string problems = "";
            sinkInstances = instances; sinkIndices = indices;
            glowsNext.Clear(); listedNext.Clear(); movingChunksSeen.Clear();
            if (settings.DrakeWorld.Value) { try { medusa = CaptureMedusa(camera); } catch (Exception ex) { problems += " medusa: " + ex.Message; } }
            bool covered = false;
            if (settings.DrakeWorld.Value) { try { entities = CaptureEntities(camera, instances, indices, out covered, false); } catch (Exception ex) { problems += " entities: " + ex.Message; } }
            if (standIns == covered)
            {
                standIns = !covered;
                Session.Log.LogInfo(standIns ? "Shadow-only meshes stand in for the geometry Entities Graphics draws near the camera (" + entityUnresolved +
                        " entities unresolved, " + entityPending + " meshes still registering)"
                    : "Shadow-only meshes are left out of the traced scene: the geometry Entities Graphics draws is traced and casts the shadows");
            }
            if (settings.SceneObjects.Value) { try { renderers = CaptureRenderers(camera, instances, indices); } catch (Exception ex) { problems += " renderers: " + ex.Message; } }
            else { followed.Clear(); followedGroups.Clear(); }
            if (settings.DrakeWorld.Value) { try { drake = CaptureDrake(camera, instances, indices, false); } catch (Exception ex) { problems += " drake: " + ex.Message; } }
            if (settings.Terrain.Value) { try { terrain = CaptureTerrains(instances, indices); } catch (Exception ex) { problems += " terrain: " + ex.Message; } }
            if (settings.Vegetation.Value) { try { vegetation = VegetationCapture.Capture(camera, AddMesh); } catch (Exception ex) { problems += " vegetation: " + ex.Message; } }
            if (settings.Vegetation.Value) { try { vegetation += LeshyCapture.Capture(camera, AddMesh); } catch (Exception ex) { problems += " leshy: " + ex.Message; } }
            try { water = WaterCaustics.Capture(camera, AddWater); } catch (Exception ex) { problems += " water: " + ex.Message; }
            TextureShare.Refresh(materialList, materialsUploaded, tracer); // PT-46: textures mip streaming made anew
            MaterialPrograms.Flush(tracer);                                 // PT-64: and the program records they wrote into
            if (materialList.Count > materialsUploaded)
            {
                var added = materialList.GetRange(materialsUploaded, materialList.Count - materialsUploaded).ToArray();
                MaterialPrograms.Flush(tracer); // PT-64: their records first, so no material names a record the tracer lacks
                if (tracer.SetMaterials((uint)materialsUploaded, added, (uint)added.Length) == 0) materialsUploaded = materialList.Count;
            }
            var sent = instances.ToArray();
            int result = tracer.SetScene(sent, (uint)instances.Count, indices.ToArray(), (uint)indices.Count);
            // A completed scan no longer uses these meshes. Native retirement also checks moving/character lists,
            // and the GPU fence, before returning storage. Looking at a mesh again registers it anew.
            if (result == 0)
            {
                var gone = new List<int>();
                foreach (var pair in meshes) if (pair.Value.Seen + 1 < meshScan) gone.Add(pair.Key);
                foreach (int id in gone)
                {
                    var old = meshes[id];
                    if (old.Handle > 0) { tracer.ReleaseMesh(old.Handle); meshNames.Remove(old.Handle); meshBounds.Remove(old.Handle); MaterialPrograms.Released(old.Handle); }
                    meshes.Remove(id);
                }
            }
            // The entities each listed instance stands for, by the list's index (MovedSinceListed, StandInFor): this list's.
            if (result == 0) { lastInstances = sent; var was = listed; listed = listedNext; listedNext = was; }
            movingChunks.IntersectWith(movingChunksSeen); // chunks found moving that are gone
            InstanceCount = instances.Count;
            // The glowing things the game moves, where the last frame placed them, beside the still ones (SceneLights claims a
            // light's own surface by them).
            glowsNext.AddRange(movingGlows);
            var glowsDone = glowsNext; glowsNext = Glows; Glows = glowsDone;
            watch.Stop();
            lastScanSeconds = (float)watch.Elapsed.TotalSeconds;
            Arrival.ScanEnds(watch.Elapsed.TotalMilliseconds, instances.Count, meshQueue.Count, materialList.Count);
            MaterialPrograms.ScanEnds(); // PT-104: one log line when the scan met a material drawn flat that the log has not named
            scanSummary =instances.Count + " instances (objects " + renderers + ", Drake " + drake + ", entities " + entities + ", Medusa " + medusa + ", terrain " + terrain + ", vegetation " + vegetation + ", water " + water + "), " + meshes.Count + " meshes, " +
                materialList.Count + " materials, " + tiles.Count + " textures, " + maskTiles.Count + " mask maps; scan " + watch.Elapsed.TotalMilliseconds.ToString("0.0") + " ms" +
                (meshQueue.Count > 0 ? "; " + meshQueue.Count + " meshes to register" : "") + (result != 0 ? "; scene refused (" + result + ")" : "") +
                (drakeShadowOnly + entityShadowOnly + drakeHidden + entityHidden > 0 ? "; entities the camera does not draw: " + (drakeShadowOnly + entityShadowOnly) +
                    (standIns ? " shadow-only (stand-ins), " : " shadow-only (left out), ") + (drakeHidden + entityHidden) + " on other layers (left out)" : "") +
                (entityUnresolved > 0 ? "; " + entityUnresolved + " entities whose mesh could not be resolved" : "") +
                (entityPending > 0 ? "; " + entityPending + " entity meshes registering" : "") +
                (objectsStandIn + objectsLeftOut > 0 ? "; objects the camera does not draw: " + objectsStandIn + (standIns ? " shadow-only (stand-ins), " : " shadow-only (left out), ") +
                    objectsLeftOut + " on other layers (left out)" : "") +
                (drakeUnloaded > 0 ? "; " + drakeUnloaded + " Drake entities the game has not loaded for drawing (left out)" : "") +
                // PT-63, PT-61: what the in-game run checks. The level of detail is the drawn one (the game's LOD bias and
                // maximum level); a group whose drawn level still registers is traced at another for now.
                "; LOD as drawn (bias " + drawnLodBias.ToString("0.##") + ", from level " + drawnMaximumLod + ")" +
                (lodStandIns > 0 ? ", " + lodStandIns + " groups at another level while theirs registers" : "") +
                (batchedTraced + batchedWaiting > 0 ? "; " + batchedTraced + " statically batched renderers traced in " + batchesTraced + " combined meshes" +
                    (batchedWaiting > 0 ? " (" + batchedWaiting + " more when their meshes are read)" : "") : "") +
                (drakeStray > 0 ? "; " + drakeStray + " Drake entities with a LOD range and no reference point (drawn always, traced)" : "") +
                (LeshyCapture.Cards > 0 ? "; " + LeshyCapture.Cards + " trees past their last mesh traced as that mesh (the game draws a card)" : "") +
                "; plants in the wind: " + PlantWind.Count + " materials posed as the game sways them" +
                "; " + DecalCapture.Summary + problems;
            TextureShare.Report(now);
        }

        // ------------------------------------------------------------------ meshes, materials, textures
        private static uint FormatOf(VertexAttributeFormat f) => (uint)f <= 5 ? (uint)f : 0xFFFFFFFFu;
        // LOD groups (an object's LODs share its reference point): the level traced is the level the game draws, by the game's
        // own rule (WorldRules; PT-61: the tracer's own camera rays show the traced mesh, so another level is another edge
        // on screen). Until 2026-10-07 the finest level was traced within 30 m whatever the game drew, which made the traced
        // mesh finer than the drawn one. While the drawn level has no registered mesh yet, one that has stands in (the
        // object was skipped until then: its shadow vanished and came back); a level the camera is about to reach is
        // registered ahead (WorldRules.EntityLevelNear).
        private static readonly List<float> tracedLevels = new List<float>();
        internal static int DecalMeshHandle(Mesh mesh, out int submeshes)
        {
            submeshes=0; if(mesh==null) return 0;
            int id=mesh.GetInstanceID();
            if(decalMeshes.TryGetValue(id,out var entry)) { submeshes=entry.Submeshes; return entry.Handle; }
            int handle;
            try { handle=RegisterMesh(mesh,out submeshes,true); }
            catch(Exception ex) { handle=0; Session.Log.LogWarning("Decal mesh could not be registered: "+mesh.name+": "+ex.Message); }
            decalMeshes[id]=new MeshEntry { Handle=handle,Submeshes=submeshes,Failed=handle<=0 }; return handle;
        }
        private static int MeshHandle(Mesh mesh, out int submeshes)
        {
            submeshes = 0;
            if (mesh == null) return 0;
            int id = mesh.GetInstanceID();
            if (meshes.TryGetValue(id, out var entry)) { entry.Seen = meshScan; meshes[id] = entry; submeshes = entry.Submeshes; Arrival.MeshAsked(false); return entry.Failed ? 0 : entry.Handle; }
            if (queued.Add(id)) meshQueue.Enqueue(mesh); // traced from the first scan after it is registered
            Arrival.MeshAsked(true);
            return 0;
        }
        private static void RegisterQueued()
        {
            double allowed = 1000.0 * MeshShare * Mathf.Clamp(Time.unscaledDeltaTime, 0f, 0.5f);
            meshClock.Restart();
            // The share is spent after the frame's first mesh, whose time holds the wait for the render thread (MeshShare).
            // (spentBefore: what is not counted against the share; nothing with the hidden "Arrival as before".)
            int registeredNow = 0; double firstMs = 0, spentBefore = 0;
            bool asBefore = ArrivalAsBefore;
            for (int n = 0; (n < MeshesPerFrame || meshClock.Elapsed.TotalMilliseconds - spentBefore < allowed) && meshQueue.Count > 0; n++)
            {
                var mesh = meshQueue.Dequeue();
                if (mesh == null) continue; // unloaded while waiting
                int id = mesh.GetInstanceID();
                queued.Remove(id);
                int handle;
                int submeshes;
                try { handle = RegisterMesh(mesh, out submeshes); }
                catch (Exception ex) { handle = -9; submeshes = 0; Session.Log.LogWarning("Mesh \"" + mesh.name + "\" could not be registered: " + ex.Message); }
                meshes[id] = new MeshEntry { Handle = handle, Submeshes = submeshes, Failed = handle <= 0, Seen = meshScan };
                if (handle > 0) { meshNames[handle] = mesh.name; meshBounds[handle] = mesh.bounds; MaterialPrograms.Registered(handle, mesh); }
                if (registeredNow++ == 0) { firstMs = meshClock.Elapsed.TotalMilliseconds; if (!asBefore) spentBefore = firstMs; }
            }
            if (registeredNow > 0) Arrival.Registered(registeredNow, meshClock.Elapsed.TotalMilliseconds, firstMs);
        }
        private static int RegisterMesh(Mesh mesh, out int submeshes, bool decalAttributes=false)
        {
            submeshes = mesh.subMeshCount;
            if (mesh.vertexCount == 0 || submeshes == 0 || !mesh.HasVertexAttribute(VertexAttribute.Position)) return -1;
            var d = new NativeMeshDesc { Size = 192, Abi = Tgpt.Abi, Key = (ulong)(uint)mesh.GetInstanceID(), UvStream = Tgpt.NoStream, ColorStream = Tgpt.NoStream, TangentStream = Tgpt.NoStream, NormalStream = Tgpt.NoStream };
            d.Uv2Stream = Tgpt.NoStream;
            d.Reserved=decalAttributes ? 1u : 0u;
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
            // Its vertex normals, where the glass bends the light through it (caustics), and to shade what the tracer draws from
            // its own mesh (see-through glass, props HDRP draws forward: Hit.rounded, PT-04); in the position's stream as a rule.
            if (mesh.HasVertexAttribute(VertexAttribute.Normal) && FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.Normal)) != 0xFFFFFFFFu)
            {
                d.NormalStream = (uint)mesh.GetVertexAttributeStream(VertexAttribute.Normal); d.NormalOffset = (uint)mesh.GetVertexAttributeOffset(VertexAttribute.Normal);
                d.NormalFormat = FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.Normal)); d.NormalDimension = (uint)mesh.GetVertexAttributeDimension(VertexAttribute.Normal);
            }
            if (decalAttributes && mesh.HasVertexAttribute(VertexAttribute.Tangent) && FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.Tangent)) != 0xFFFFFFFFu)
            {
                d.TangentStream = (uint)mesh.GetVertexAttributeStream(VertexAttribute.Tangent); d.TangentOffset = (uint)mesh.GetVertexAttributeOffset(VertexAttribute.Tangent);
                d.TangentFormat = FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.Tangent)); d.TangentDimension = (uint)mesh.GetVertexAttributeDimension(VertexAttribute.Tangent);
            }
            // PT-64, for the game's shaders the tracer computes at a hit: the mesh's fourth set of texture coordinates (the trunk
            // shader lays its bark by it) and its vertex colors (the cliff shader's moss follows their red; the cloth's sway is
            // masked by them where the game's is).
            if (mesh.HasVertexAttribute(VertexAttribute.TexCoord3) && FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.TexCoord3)) != 0xFFFFFFFFu &&
                mesh.GetVertexAttributeDimension(VertexAttribute.TexCoord3) >= 2)
            {
                d.Uv2Stream = (uint)mesh.GetVertexAttributeStream(VertexAttribute.TexCoord3); d.Uv2Offset = (uint)mesh.GetVertexAttributeOffset(VertexAttribute.TexCoord3);
                d.Uv2Format = FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.TexCoord3)); d.Uv2Dimension = (uint)mesh.GetVertexAttributeDimension(VertexAttribute.TexCoord3);
            }
            if (mesh.HasVertexAttribute(VertexAttribute.Color) && FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.Color)) != 0xFFFFFFFFu &&
                mesh.GetVertexAttributeDimension(VertexAttribute.Color) >= 3)
            {
                d.ColorStream = (uint)mesh.GetVertexAttributeStream(VertexAttribute.Color); d.ColorOffset = (uint)mesh.GetVertexAttributeOffset(VertexAttribute.Color);
                d.ColorFormat = FormatOf(mesh.GetVertexAttributeFormat(VertexAttribute.Color)); d.ColorDimension = (uint)mesh.GetVertexAttributeDimension(VertexAttribute.Color);
            }
            var streams = new IntPtr[4]; var strides = new uint[4];
            for (int s = 0; s < mesh.vertexBufferCount && s < 4; s++)
            {
                bool used = s == d.PositionStream || s == d.UvStream || s == d.NormalStream || s == d.Uv2Stream || s == d.TangentStream || s == d.ColorStream;
                if (!used) continue;
                streams[s] = mesh.GetNativeVertexBufferPtr(s); // waits for the render thread: once per mesh
                strides[s] = (uint)mesh.GetVertexBufferStride(s);
            }
            if (streams[d.PositionStream] == IntPtr.Zero) return -3;
            if (d.UvStream < 4 && streams[d.UvStream] == IntPtr.Zero) d.UvStream = Tgpt.NoStream;
            if (d.NormalStream < 4 && streams[d.NormalStream] == IntPtr.Zero) d.NormalStream = Tgpt.NoStream;
            if (d.Uv2Stream >= 4 || streams[d.Uv2Stream] == IntPtr.Zero) d.Uv2Stream = Tgpt.NoStream;
            if (d.ColorStream >= 4 || streams[d.ColorStream] == IntPtr.Zero) d.ColorStream = Tgpt.NoStream;
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
            TextureShare.Register(texture, native); // PT-45: opened by the tracer as it is, beside its tile
            if (tile >= 0 || native != IntPtr.Zero) tiles[id] = tile;
            return tile;
        }
        // A texture the camera sees from the atlas itself (see-through glass: the tracer draws it, HDRP's GBuffer doesn't hold
        // it): a 512x512 detail tile, filtered, where the 64x64 tile showed a jar's stains as blocks. The 64x64 tile once the
        // detail tiles are taken.
        private static int DetailTile(Texture texture)
        {
            if (!(texture is Texture2D)) return -1;
            int id = texture.GetInstanceID();
            if (detailTiles.TryGetValue(id, out int tile)) return tile;
            IntPtr native = texture.GetNativeTexturePtr();
            tile = native != IntPtr.Zero ? Session.Tracer.RegisterDetailTexture((ulong)(uint)id, native) : -1;
            TextureShare.Register(texture, native);
            // None left: hair that is gone gives its tile back, and the glass asks again.
            if (tile < 0 && native != IntPtr.Zero && ReleaseIdleHairTiles(0)) tile = Session.Tracer.RegisterDetailTexture((ulong)(uint)id, native);
            if (tile < 0) tile = Tile(texture);
            if (tile >= 0 || native != IntPtr.Zero) detailTiles[id] = tile;
            return tile;
        }
        // An alpha-tested texture's coverage at 256x256 for the alpha test (how much of each texel passes cutoff, the
        // material's over its base alpha): the 64x64 color tile turned straw, rope nets and leaves into opaque blocks
        // (black patches under straw on a floor), and averaged alpha thinned strands and fringe threads below the cutoff.
        // The material's AlphaCutoff then compares the coverage. -1 when the alpha atlas is full.
        private static int AlphaTile(Texture texture, float cutoff)
        {
            if (!(texture is Texture2D)) return -1;
            int id = texture.GetInstanceID();
            int level = Mathf.RoundToInt(Mathf.Clamp(cutoff, 0f, 1.004f) * 255f);
            long key = (long)(uint)id | ((long)level << 32);
            if (alphaTiles.TryGetValue(key, out int tile)) return tile;
            IntPtr native = texture.GetNativeTexturePtr();
            tile = native != IntPtr.Zero ? Session.Tracer.RegisterAlphaTexture(unchecked((ulong)key), native, level / 255f) : -1;
            if (tile == -3 && !alphaFullLogged) { alphaFullLogged = true; Session.Log.LogInfo("Alpha atlas full: " + alphaTiles.Count + " alpha-tested textures have tiles; the rest test alpha from their 64x64 color tile"); }
            if (tile >= 0 || native != IntPtr.Zero) alphaTiles[key] = tile;
            return tile;
        }
        // A mask map's tile in the atlas (RegisterMaskTexture: its bytes read as they are, metallic and smoothness from the
        // channels given), -1 when the atlas has no room left for mask maps.
        private static int MaskTile(Texture texture, uint channels)
        {
            if (!(texture is Texture2D)) return -1;
            long key = (long)(uint)texture.GetInstanceID() | ((long)channels << 32);
            if (maskTiles.TryGetValue(key, out int tile)) return tile;
            IntPtr native = texture.GetNativeTexturePtr();
            tile = native != IntPtr.Zero ? Session.Tracer.RegisterMaskTexture(unchecked((ulong)key), native, channels) : -1;
            TextureShare.Register(texture, native);
            if (tile >= 0 || native != IntPtr.Zero) maskTiles[key] = tile;
            return tile;
        }
        // Mask maps as the game's GBuffer passes read them (from their compiled code): the texture, metallic in its red, the
        // channel (0 red to 3 alpha) holding smoothness, and each one's range. HDRP Lit remaps both, and reads the mask map
        // only with its keyword on (else its constants); its layered version's base layer is the one AlbedoProperty takes the
        // colors of. The game's Shader Graphs read metallic as it is and smoothness from alpha (Cloth_Wind: from blue), some
        // times a smoothness slider. A shader not named here keeps the material's constants: another shader's red need not be
        // metallic (Cliff's holds rock formations with its toggle on; vegetation and the triplanar walls are never metallic).
        private sealed class MaskLayout
        {
            internal string Texture, Keyword;                            // the mask map, and the keyword without which it is not read
            internal uint SmoothnessChannel = 3;
            internal string MetallicMin, MetallicMax, SmoothnessMin, SmoothnessMax; // the remap's ends (absent: 0 to 1)
            internal string SmoothnessScale;                             // smoothness times this slider
            internal string Metallic, Smoothness;                        // the constants without the mask map (absent: _Metallic, _Smoothness)
            // PT-64, from the shaders' compiled code: its green is the occlusion the game takes off its bounced light; its red
            // is no metal (the game's plants: the shader never reads it).
            internal bool Occlusion, NoMetal;
        }
        private static readonly Dictionary<string, MaskLayout> MaskLayouts = new Dictionary<string, MaskLayout>
        {
            ["HDRP/Lit"] = new MaskLayout { Texture = "_MaskMap", Keyword = "_MASKMAP", MetallicMin = "_MetallicRemapMin", MetallicMax = "_MetallicRemapMax",
                SmoothnessMin = "_SmoothnessRemapMin", SmoothnessMax = "_SmoothnessRemapMax" },
            ["HDRP/LayeredLit"] = new MaskLayout { Texture = "_MaskMap0", Keyword = "_MASKMAP0", MetallicMin = "_MetallicRemapMin0", MetallicMax = "_MetallicRemapMax0",
                SmoothnessMin = "_SmoothnessRemapMin0", SmoothnessMax = "_SmoothnessRemapMax0", Metallic = "_Metallic0", Smoothness = "_Smoothness0" },
            ["TG/Character/Kandra_Lit"] = new MaskLayout { Texture = "_MaskMap", Occlusion = true },
            ["TG/Character/Kandra_Lit_Dynamic"] = new MaskLayout { Texture = "_MaskMap" },
            [MaterialPrograms.GrassShader] = new MaskLayout { Texture = "_MaskMap", Occlusion = true, NoMetal = true },
            [MaterialPrograms.LeavesShader] = new MaskLayout { Texture = "_MaskMap", Occlusion = true, NoMetal = true },
            ["TG/Character/GhostCrew"] = new MaskLayout { Texture = "_MaskMap" },
            ["TG/Architecture/KF_Architecture"] = new MaskLayout { Texture = "_Base_MaskMap", SmoothnessScale = "_Base_Smoothness" },
            ["TG/Clothes/Cloth_Wind"] = new MaskLayout { Texture = "_Mask_Map", SmoothnessChannel = 2, Occlusion = true },
            ["TG/Dissolve/Dissolve3D"] = new MaskLayout { Texture = "_MaskMap", SmoothnessScale = "_Smoothness", Occlusion = true },
            ["TG/Dissolve/Dissolve3D_Strawdad"] = new MaskLayout { Texture = "_MaskMap", SmoothnessScale = "_Smoothness" },
        };
        // Shader Graphs that tile their base color and mask map through properties of their own (so their compiled code reads
        // them), their textures' own scale and offset left at 1 and 0: KF_Architecture's times _Base_Tiling (without it a
        // wall's stone was traced that many times too coarse), Cloth_Wind's times _Tiling plus _Offset, Lit_No_VA's through
        // _TilingAndOffset. The alpha test and the micromaps read through the same coordinates.
        private static void ShaderGraphTiling(Material m, string shaderName, ref NativeMaterial mat)
        {
            if (shaderName == "TG/Architecture/KF_Architecture" && m.HasProperty("_Base_Tiling"))
            {
                mat.ScaleX = mat.ScaleY = m.GetFloat("_Base_Tiling"); mat.OffsetX = mat.OffsetY = 0f;
            }
            else if (shaderName == "TG/Clothes/Cloth_Wind" && m.HasProperty("_Tiling") && m.HasProperty("_Offset"))
            {
                Vector4 tiling = m.GetVector("_Tiling"), offset = m.GetVector("_Offset");
                mat.ScaleX = tiling.x; mat.ScaleY = tiling.y; mat.OffsetX = offset.x; mat.OffsetY = offset.y;
            }
            else if ((shaderName == MaterialPrograms.GrassShader || shaderName == MaterialPrograms.LeavesShader) && m.HasProperty("_Tiling"))
            {
                Vector4 tiling = m.GetVector("_Tiling");
                mat.ScaleX = tiling.x; mat.ScaleY = tiling.y; mat.OffsetX = mat.OffsetY = 0f;
            }
            else if (shaderName == "Shader Graphs/Lit_No_VA" && m.HasProperty("_TilingAndOffset"))
            {
                Vector4 st = m.GetVector("_TilingAndOffset");
                mat.ScaleX = st.x; mat.ScaleY = st.y; mat.OffsetX = st.z; mat.OffsetY = st.w;
            }
        }
        private static float FloatOr(Material m, string name, float fallback) => name != null && m.HasProperty(name) ? m.GetFloat(name) : fallback;
        private static uint Unorm8(float v) => (uint)Mathf.RoundToInt(Mathf.Clamp01(v) * 255f);
        // Metallic and smoothness texel by texel from the material's mask map where its shader has one MaskLayouts knows (and
        // maskMap allows), its constants otherwise; what BepInEx says of the mask map, or null.
        private static string ReadFinish(Material m, string shaderName, ref NativeMaterial mat, bool maskMap)
        {
            MaskLayouts.TryGetValue(shaderName, out var layout);
            string metallic = layout != null && layout.Metallic != null ? layout.Metallic : m.HasProperty("_Metallic") ? "_Metallic" : "_Metalness"; // Lit_No_VA's
            if (m.HasProperty(metallic)) mat.Metallic = Mathf.Clamp01(m.GetFloat(metallic));
            string smoothness = layout != null && layout.Smoothness != null ? layout.Smoothness : "_Smoothness";
            if (m.HasProperty(smoothness)) mat.Smoothness = Mathf.Clamp01(m.GetFloat(smoothness));
            if (!maskMap || layout == null || !m.HasProperty(layout.Texture)) return null;
            // HDRP Lit's specular color workflow: no metallic at all.
            bool specularColor = layout.Keyword != null && m.IsKeywordEnabled("_MATERIAL_FEATURE_SPECULAR_COLOR");
            if (specularColor) mat.Metallic = 0f;
            var texture = m.GetTexture(layout.Texture);
            if (texture == null || (layout.Keyword != null && !m.IsKeywordEnabled(layout.Keyword))) return null;
            uint channels = layout.SmoothnessChannel << 2; // metallic: red
            int tile = MaskTile(texture, channels);
            // PT-46: the mask map itself, read at a hit texel by texel; HDRP Lit's green is its ambient occlusion.
            int shared = TextureShare.Handle(texture, TextureShare.FieldMask);
            if (tile < 0 && shared < 0) return layout.Texture + " left out (the atlas has no room left for mask maps)";
            mat.MaskTexture = shared;
            mat.TextureFlags = channels | (layout.Keyword != null || layout.Occlusion ? Tgpt.TextureMaskOcclusion : 0u);
            // HDRP Lit's occlusion through the material's own range (PT-64: 102 of the game's materials end it at 0.39).
            if (layout.Keyword != null && m.HasProperty("_AORemapMin") && m.HasProperty("_AORemapMax"))
            {
                float aoLow = Mathf.Clamp01(m.GetFloat("_AORemapMin")), aoHigh = Mathf.Clamp01(m.GetFloat("_AORemapMax"));
                if (aoLow > 0.002f || aoHigh < 0.998f)
                    mat.TextureFlags |= Tgpt.TextureOcclusionRemap | (Unorm8(aoLow) << Tgpt.TextureOcclusionLowShift) | (Unorm8(aoHigh) << Tgpt.TextureOcclusionHighShift);
            }
            if (layout.NoMetal) { specularColor = true; mat.Metallic = 0f; }
            float metallicLow = specularColor ? 0f : FloatOr(m, layout.MetallicMin, 0f), metallicHigh = specularColor ? 0f : FloatOr(m, layout.MetallicMax, 1f);
            float smoothnessLow = FloatOr(m, layout.SmoothnessMin, 0f), smoothnessHigh = FloatOr(m, layout.SmoothnessMax, 1f);
            if (layout.SmoothnessScale != null) { float scale = Mathf.Clamp01(FloatOr(m, layout.SmoothnessScale, 1f)); smoothnessLow *= scale; smoothnessHigh *= scale; }
            if (tile >= 0) { mat.MaskTile = tile; mat.Flags |= Tgpt.MaterialMaskTile; }
            mat.MaskRemap = Unorm8(metallicLow) | (Unorm8(metallicHigh) << 8) | (Unorm8(smoothnessLow) << 16) | (Unorm8(smoothnessHigh) << 24);
            return layout.Texture + " (tile " + tile + "): metallic R " + metallicLow.ToString("0.##") + "-" + metallicHigh.ToString("0.##") +
                ", smoothness " + "RGBA"[(int)layout.SmoothnessChannel] + " " + smoothnessLow.ToString("0.##") + "-" + smoothnessHigh.ToString("0.##");
        }
        // The two materials every scene starts with: invisible to rays (index 0) and neutral grey (1).
        private static void EnsureDefaults()
        {
            if (materialList.Count != 0) return;
            materialList.Add(new NativeMaterial { BaseA = 1f, ScaleX = 1, ScaleY = 1, AlbedoTile = -1, EmissiveTile = -1, AlphaCutoff = 2f, Flags = Tgpt.MaterialAlphaTest });
            materialList.Add(new NativeMaterial { BaseR = 0.4f, BaseG = 0.4f, BaseB = 0.4f, BaseA = 1f, ScaleX = 1, ScaleY = 1, AlbedoTile = -1, EmissiveTile = -1 });
        }
        // Characters' parts left out of the traced scene: transparent and unlit ones (eye wetness, glow cards), the
        // game's invisible material (TG/NoRender: parts hidden under armour), and scalp and beard masks (stubble and
        // hairlines drawn as a shell on the skin: cut out coarsely for rays, they shadowed the face under them in blotches).
        // CharacterCapture does not register their triangles. The eyes are in the traced scene for camera rays only
        // (CharacterMaterial), which stop there and leave them to HDRP: as solid surfaces they stopped every ray leaving
        // the face around them.
        internal static bool CharacterLeftOut(Material m)
        {
            if (m == null || LayerBrow(m) || GlassFinish.IsCornea(m)) return false;
            var shader = m.shader;
            string shaderName = shader != null ? shader.name : "";
            return (m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f) || m.renderQueue >= 2750 ||
                shaderName.IndexOf("Unlit", StringComparison.OrdinalIgnoreCase) >= 0 || shaderName.IndexOf("NoRender", StringComparison.OrdinalIgnoreCase) >= 0 ||
                m.name.IndexOf("ScalpMask", StringComparison.OrdinalIgnoreCase) >= 0 || m.name.IndexOf("Stubble", StringComparison.OrdinalIgnoreCase) >= 0;
        }
        // Eyebrows of the game's RealHair, alpha-tested: traced whether the game draws them transparent (always, up close) or the
        // plugin holds them opaque. The tracer draws them as a layer where they have a detail tile, and they stay transparent
        // for HDRP then (CharacterCapture.Brows): held opaque, HDRP drew their whole cards into its depth (the game has no opaque
        // build of RealHair that cuts them), black bars over the skin (2026-10-06, round 4). Read as transparent, they were
        // left out of the traced scene.
        internal static bool LayerBrow(Material m) =>
            m != null && m.shader != null && m.shader.name.IndexOf("RealHair", StringComparison.OrdinalIgnoreCase) >= 0 &&
            m.name.IndexOf("Brow", StringComparison.OrdinalIgnoreCase) >= 0 && m.HasProperty("_AlphaCutoffEnable") && m.GetFloat("_AlphaCutoffEnable") > 0.5f;
        // Whether a character's material, read in its state now, is traced as hair drawn as a layer (TGPT_MATERIAL_HAIR_LAYER),
        // whose HDRP passes CharacterCapture hides while tracing.
        internal static bool IsHairLayer(Material m, bool textured)
        {
            if (m == null) return false;
            int key = unchecked(m.GetInstanceID() * 4 + (textured ? 1 : 0) + (CharacterLeftOut(m) ? 2 : 0));
            return characterMaterials.TryGetValue(key, out uint index) && index < materialList.Count && (materialList[(int)index].Flags & Tgpt.MaterialHairLayer) != 0;
        }
        // The skin of the hero's first-person body (CharacterCapture: the body the game loads under its first-person rig): the
        // character material index with TGPT_MATERIAL_FIRST_PERSON, a copy made once. The game loads no head in first person
        // (VHeroController loads the third-person body with the face only in third person): that body's mesh has the face cut
        // out for the camera, and the back of the head that is left is an open shell HDRP never draws from in front (its skin
        // is one-sided). The tracer's rays pass this material's back faces (pt_common first_person_inside). In the harness's
        // mirror (2026-10-07) the hero wore the shell's inside as a bare, faceless egg.
        internal static uint FirstPersonMaterial(uint index)
        {
            if (index >= materialList.Count || index == Invisible) return index;
            if (firstPersonMaterials.TryGetValue(index, out uint copy)) return copy;
            var mat = materialList[(int)index];
            mat.Flags |= Tgpt.MaterialFirstPerson;
            materialList.Add(mat);
            copy = (uint)(materialList.Count - 1);
            firstPersonMaterials[index] = copy;
            Session.Log.LogInfo("Characters: the hero's first-person body (material #" + index + " as #" + copy + ", " +
                ((mat.Flags & Tgpt.MaterialDoubleSided) != 0 ? "drawn two-sided: rays meet both of its sides" : "drawn one-sided: rays pass its back faces, as HDRP does not draw them") +
                "): the game loads no head in first person, and the back of the head its mesh keeps is not drawn from in front");
            return copy;
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
            // Read again when it turns transparent or opaque: the game makes a character's hair transparent near the camera
            // and opaque farther away (BodyFeatures.RefreshDistanceBand), on the same material. Read once while transparent (left
            // out), the hair the game draws opaque far away stayed invisible to rays, and its grey first layer showed instead.
            bool leftOut = CharacterLeftOut(m);
            int key = unchecked(m.GetInstanceID() * 4 + (textured ? 1 : 0) + (leftOut ? 2 : 0));
            if (hairSeen.TryGetValue(m.GetInstanceID(), out bool wasLeftOut) && wasLeftOut != leftOut && hairFlipsLogged++ < 12)
                Session.Log.LogInfo("Characters: hair material \"" + m.name + "\" turned " + (leftOut ? "transparent (left out; its first layer traced)" : "opaque (traced)") +
                    ", color " + (m.HasProperty("_DiffuseColor") ? m.GetColor("_DiffuseColor").ToString("0.00") : "none"));
            if (hairSeen.ContainsKey(m.GetInstanceID()) || (m.shader != null && m.shader.name.IndexOf("Hair", StringComparison.OrdinalIgnoreCase) >= 0))
                hairSeen[m.GetInstanceID()] = leftOut;
            if (characterMaterials.TryGetValue(key, out uint index))
            {
                if (hairColors.TryGetValue(index, out var read)) RefreshHairColor(m, index, read);
                else if (shellColors.TryGetValue(index, out var painted)) RefreshShellColor(m, index, painted);
                return index;
            }
            var shader = m.shader;
            string shaderName = shader != null ? shader.name : "";
            bool alphaTest = (m.HasProperty("_AlphaCutoffEnable") && m.GetFloat("_AlphaCutoffEnable") > 0.5f) || m.IsKeywordEnabled("_ALPHATEST_ON");
            bool strands = shaderName.IndexOf("Hair", StringComparison.OrdinalIgnoreCase) >= 0 ||
                (alphaTest && Array.Exists(StrandNames, n => m.name.IndexOf(n, StringComparison.OrdinalIgnoreCase) >= 0));
            bool skin = shaderName.IndexOf("Skin", StringComparison.OrdinalIgnoreCase) >= 0;
            bool eye = shaderName.StartsWith("TG/Character/Eye", StringComparison.OrdinalIgnoreCase);
            string how;
            uint world = !eye && (textured || GlassFinish.IsCornea(m)) && !CharacterLeftOut(m) ? MaterialIndex(m) : Invisible;
            if (eye)
            {
                // The eyes are the tracer's (the owner's rule: nothing HDRP-shaded while tracing), lit as a wet surface: their
                // shader's color baked texel by texel (EyeTile: the sclera, the iris with its pupil and limbal ring, from the
                // game's compiled eye shader). Traced from the sclera's texture alone they came out white. Seen by camera
                // and reflected rays alike. The separate cornea mesh is another traced dielectric
                // interface; its HDRP colour passes are hidden (CharacterCapture).
                int tile = textured ? EyeTile(m) : -1;
                materialList.Add(new NativeMaterial { BaseR = tile >= 0 ? 1f : 0.8f, BaseG = tile >= 0 ? 1f : 0.8f, BaseB = tile >= 0 ? 1f : 0.8f, BaseA = 1f,
                    ScaleX = 1, ScaleY = 1, AlbedoTile = tile, EmissiveTile = -1, AlphaCutoff = 0f, Smoothness = Mathf.Clamp01(FloatOr(m, "_Sclera_Smoothness", 0.69f)),
                    Flags = Tgpt.MaterialAlphaTest | Tgpt.MaterialEye });
                index = (uint)(materialList.Count - 1);
                how = "the eyes: traced for every ray, with a separate cornea, " + (tile >= 0 ? "their shader's color baked texel by texel (" +
                    (tile >= 4096 ? "a 512x512 tile" : "a 64x64 tile") + ")" : "a plain white (no texture coordinates)") + ", the eye material beneath the cornea";
            }
            else if (CharacterLeftOut(m))
            {
                index = Invisible;
                bool shell = IsShell(m);
                how = shell ? "is a scalp or beard mask (a shell on the skin), left out of the traced scene" : "is transparent, left out of the traced scene";
                // A scalp or beard mask is the hair's base under its cards: the game paints the skin there in the hair's color
                // (_BaseColor, from the character's hair config) through the mask's texture, blended by its alpha. The tracer
                // draws it itself, as the hair layer's lowest layer (pt_hair): its texture baked into a detail tile with its
                // alpha at its own size, as hair's is; seen by that layer alone (camera-only: no other ray meets it, so it
                // shadows nothing). Left out, with its renderer hidden from HDRP (CharacterCapture.HideRenderer), a crown
                // was thin strands over bare skin (a pink veil, 2026-10-06 round 3).
                int tile = shell && textured && LayerShell(m) ? ShellTile(m) : -1;
                if (tile >= Tgpt.FirstDetailTile)
                {
                    Color painted = ColorOr(m, "_BaseColor");
                    Linear(painted, out float r, out float g, out float b, out float a);
                    materialList.Add(new NativeMaterial { BaseR = r, BaseG = g, BaseB = b, BaseA = a, ScaleX = 1, ScaleY = 1, AlbedoTile = tile, EmissiveTile = -1,
                        AlphaCutoff = 0f, Smoothness = 0.3f, AlphaTile = -1, MaskTile = -1, HairLobeTile = -1,
                        Flags = Tgpt.MaterialAlphaTest | Tgpt.MaterialCameraOnly | Tgpt.MaterialHairLayer });
                    index = (uint)(materialList.Count - 1);
                    shellColors[index] = painted;
                    how = "is a scalp or beard mask (a shell on the skin), drawn by the tracer as the hair layer's lowest layer (its texture in a 512x512 tile, color " +
                        ColorText(m, "_BaseColor") + ", alpha clip " + CharacterCapture.GameCut(m, "_AlphaCutoff", 0.05f).ToString("0.##") + ")";
                }
            }
            else if (world != Invisible && (GlassFinish.IsCornea(m) || materialList[(int)world].AlbedoTile >= 0))
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
                    // Strands a few texels wide cover only part of a 256x256 alpha tile's texel (averaged, they fell below
                    // the usual cutoff and a hair cast no shadow on the neck under it). A ray stops at hair by chance, as
                    // much of the texel as its strands cover (MaterialStrands), so a layer of hair lets part of the light
                    // through: cut out where a fifth of a texel was covered, each card cast a full, hard shadow. The cutoff
                    // serves the alpha test where no alpha tile could be made.
                    if (!cut)
                    {
                        string albedo = AlbedoProperty(m);
                        int tile = albedo != null ? AlphaTile(m.GetTexture(albedo), 0.5f / Mathf.Max(mat.BaseA, 1e-3f)) : -1;
                        mat.Flags |= Tgpt.MaterialAlphaTest;
                        if (tile >= 0) { mat.AlphaTile = tile; mat.Flags |= Tgpt.MaterialAlphaTile; }
                    }
                    mat.AlphaCutoff = Mathf.Min(cut ? mat.AlphaCutoff : 0.5f, 0.2f);
                    if ((mat.Flags & Tgpt.MaterialAlphaTile) != 0) mat.Flags |= Tgpt.MaterialStrands;
                    // Shaded by the tracer, as everything is while tracing: camera rays stop at it wherever they meet it
                    // (pt_common alpha_opaque: HDRP drew hair at that pixel), and its color is its shader's. The game's
                    // RealHair colors its diffuse map by _DiffuseColor times _DiffuseStrength (read as the texture alone
                    // it came out grey); its finish is between _SmoothnessMin and _SmoothnessMax.
                    bool realHair = shaderName.IndexOf("RealHair", StringComparison.OrdinalIgnoreCase) >= 0;
                    int baked = -1;
                    if (realHair)
                    {
                        // Its color is its shader's, texel by texel (HairTile): _DiffuseColor alone left hair whose color is in
                        // its root-to-end gradient grey (the bartender's: _DiffuseColor 0.70, the auburn in _RootColor and
                        // _EndColor). The tile holds the hair at its mesh's own coordinates.
                        baked = HairTile(m);
                        if (baked >= 0)
                        {
                            mat.AlbedoTile = baked; mat.BaseR = mat.BaseG = mat.BaseB = 1f;
                            mat.ScaleX = mat.ScaleY = 1f; mat.OffsetX = mat.OffsetY = 0f;
                        }
                        else { var tint = HairColor(m, new Vector3(mat.BaseR, mat.BaseG, mat.BaseB)); mat.BaseR = tint.x; mat.BaseG = tint.y; mat.BaseB = tint.z; }
                        if (m.HasProperty("_SmoothnessMin") && m.HasProperty("_SmoothnessMax"))
                            mat.Smoothness = Mathf.Clamp01(0.5f * (m.GetFloat("_SmoothnessMin") + m.GetFloat("_SmoothnessMax")));
                        HairLobes(m, ref mat);
                        // Drawn by the tracer as a layer over HDRP's surface, as the game draws hair up close (its HDRP passes
                        // hidden: CharacterCapture), where its color has a detail tile, which its strands' alpha at the texture's
                        // own size goes with (TGPT_RegisterHairColor). Otherwise it stays held opaque and found where HDRP drew it.
                        if (baked >= Tgpt.FirstDetailTile) { mat.Flags |= Tgpt.MaterialHairLayer; mat.HairAlpha = HairAlpha(m); }
                    }
                    materialList.Add(mat); index = (uint)(materialList.Count - 1);
                    if (realHair) hairColors[index] = ReadHairState(m);
                    if (baked >= 0) bakedHair.Add(index);
                    how = ((mat.Flags & Tgpt.MaterialStrands) != 0 ? "hair, stopping rays as much as its strands cover a texel" : "hair, cut out by its texture's alpha (a fifth of a texel covered)") +
                        ((mat.Flags & Tgpt.MaterialHairLayer) != 0 ? ", drawn by the tracer as a layer over HDRP's surface (alpha clip " + CharacterCapture.GameCut(m, "_AlphaClip", 0.1f).ToString("0.##") +
                            ", depth prepass " + CharacterCapture.GameCut(m, "_DepthPrepass", 0.5f).ToString("0.##") + ", alpha power " + FloatOr(m, "_AlphaPower", 1f).ToString("0.##") +
                            ", remap " + FloatOr(m, "_AlphaRemap", 1f).ToString("0.##") + ")" : "") +
                        ", shaded by the tracer (" + (baked >= 0 ? "its shader's color baked texel by texel: root " + ColorText(m, "_RootColor") + ", end " + ColorText(m, "_EndColor") +
                        ", diffuse " + ColorText(m, "_DiffuseColor") : "color " + mat.BaseR.ToString("0.00") + ", " + mat.BaseG.ToString("0.00") + ", " + mat.BaseB.ToString("0.00")) +
                        ", smoothness " + mat.Smoothness.ToString("0.00") +
                        ((mat.Flags & Tgpt.MaterialHairLobes) != 0 ? "; its highlights along its strands: tint " + mat.HairTintR.ToString("0.00") + ", " + mat.HairTintG.ToString("0.00") + ", " +
                            mat.HairTintB.ToString("0.00") + " x " + mat.HairStrength.ToString("0.###") + " and " + mat.HairSecondaryStrength.ToString("0.###") + ", shifts " +
                            mat.HairShift.ToString("0.00") + " and " + mat.HairSecondaryShift.ToString("0.00") + ", smoothness " + mat.HairSmoothnessMin.ToString("0.00") + " to " +
                            mat.HairSmoothnessMax.ToString("0.00") + " and " + mat.HairSecondarySmoothness.ToString("0.00") : "") + ")";
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
            {
                Session.Log.LogInfo("Characters: shader \"" + shaderName + "\" (material \"" + m.name + "\") " + how);
                // What the shaders of hair, eyes, eyebrows and lashes color them by (the tracer shades them all: the eyes'
                // textures and the brows' and lashes' still to be read from these).
                bool strandLike = strands || m.name.IndexOf("Brow", StringComparison.OrdinalIgnoreCase) >= 0 || m.name.IndexOf("Lash", StringComparison.OrdinalIgnoreCase) >= 0;
                if ((strandLike || eye) && shader != null)
                    Session.Log.LogInfo("Characters: shader \"" + shaderName + "\" (material \"" + m.name + "\") colors, values and textures: " + ShaderValues(m, shader));
            }
            characterMaterials[key] = index;
            if (!materialNames.ContainsKey(index)) materialNames[index] = m.name + " (" + shaderName + ", a character's)";
            return index;
        }
        // What the tracer made of a character's material, for the F8 report: its traced material (for either texture state it
        // was read in) and color, or that it was never read.
        internal static string DescribeCharacterMaterial(Material m)
        {
            if (m == null) return "no material";
            var parts = new List<string>();
            int id = m.GetInstanceID();
            for (int variant = 0; variant < 4; variant++)
            {
                if (!characterMaterials.TryGetValue(unchecked(id * 4 + variant), out uint index)) continue;
                string state = ((variant & 1) != 0 ? "textured" : "untextured") + ((variant & 2) != 0 ? ", read while transparent" : ", read while opaque");
                if (index == Invisible) { parts.Add(state + ": invisible to rays"); continue; }
                if (index >= materialList.Count) { parts.Add(state + ": #" + index); continue; }
                var mat = materialList[(int)index];
                parts.Add(state + ": #" + index + " color (" + mat.BaseR.ToString("0.00") + ", " + mat.BaseG.ToString("0.00") + ", " + mat.BaseB.ToString("0.00") + ")" +
                    (bakedHair.Contains(index) ? ", its shader's color baked (root " + ColorText(m, "_RootColor") + ", end " + ColorText(m, "_EndColor") + ")" : "") +
                    ((mat.Flags & Tgpt.MaterialStrands) != 0 ? ", strands" : ""));
            }
            return parts.Count > 0 ? string.Join("; ", parts.ToArray()) : "never read";
        }
        // RealHair's color: its diffuse map times _DiffuseColor times _DiffuseStrength (linear).
        private static Vector3 HairColor(Material m, Vector3 fallback)
        {
            var tint = m.HasProperty("_DiffuseColor") ? LinearColor(m.GetColor("_DiffuseColor")) : fallback;
            float strength = m.HasProperty("_DiffuseStrength") ? Mathf.Clamp(m.GetFloat("_DiffuseStrength"), 0f, 4f) : 1f;
            return tint * strength;
        }
        // The colors the game gives a character's hair (CharacterHairColor.ApplyTo sets these on its clone of the material, its
        // highlights' _SpecularTint too).
        private struct HairState { internal Color Diffuse, Root, End, HighlightA, HighlightB, Specular; internal float Strength; }
        private static HairState ReadHairState(Material m) => new HairState
        {
            Diffuse = ColorOr(m, "_DiffuseColor"), Root = ColorOr(m, "_RootColor"), End = ColorOr(m, "_EndColor"),
            HighlightA = ColorOr(m, "_HighlightAColor"), HighlightB = ColorOr(m, "_HighlightBColor"), Specular = ColorOr(m, "_SpecularTint"),
            Strength = FloatOr(m, "_DiffuseStrength", 1f)
        };
        private static Color ColorOr(Material m, string name) => m.HasProperty(name) ? m.GetColor(name) : Color.white;
        private static string ColorText(Material m, string name)
        {
            var c = ColorOr(m, name);
            return "(" + c.r.ToString("0.00") + ", " + c.g.ToString("0.00") + ", " + c.b.ToString("0.00") + ")";
        }
        // A hair material's colors as the game has them now: where they changed since they were read, its baked tile is baked
        // again (or, without one, its traced color follows).
        private static void RefreshHairColor(Material m, uint index, HairState read)
        {
            HairTileSeen(m);
            var now = ReadHairState(m);
            if ((now.Diffuse == read.Diffuse && now.Root == read.Root && now.End == read.End && now.HighlightA == read.HighlightA &&
                 now.HighlightB == read.HighlightB && now.Specular == read.Specular && Mathf.Abs(now.Strength - read.Strength) < 1e-4f) || index >= materialList.Count) return;
            hairColors[index] = now;
            bool changed = false;
            // Its tile first, the traced material read after it: colored anew, hair that shared its detail tile reads another
            // one, which HairTile puts into the traced material (FollowHairTile). Read before, the copy written back below
            // put the old tile into it again, and the hair kept the colors it shared.
            bool baked = bakedHair.Contains(index);
            if (baked) HairTile(m);
            var mat = materialList[(int)index];
            if (!baked)
            {
                var tint = HairColor(m, new Vector3(mat.BaseR, mat.BaseG, mat.BaseB));
                mat.BaseR = tint.x; mat.BaseG = tint.y; mat.BaseB = tint.z;
                changed = true;
            }
            // Its highlights' tint follows too.
            if ((mat.Flags & Tgpt.MaterialHairLobes) != 0) { Linear(now.Specular, out mat.HairTintR, out mat.HairTintG, out mat.HairTintB, out _); changed = true; }
            if (changed)
            {
                materialList[(int)index] = mat;
                var tracer = Session.Tracer;
                if (index < materialsUploaded && tracer != null && tracer.Connected) tracer.SetMaterials(index, new[] { mat }, 1);
            }
            if (hairColorLogged.Add(m.name) && hairColorLogged.Count <= 16)
                Session.Log.LogInfo("Characters: hair \"" + m.name + "\" colored after it was first read: root " + ColorText(m, "_RootColor") + ", end " +
                    ColorText(m, "_EndColor") + ", diffuse " + ColorText(m, "_DiffuseColor") + (bakedHair.Contains(index) ? ", its tile baked again" : ", its traced color follows"));
        }
        // The iris/sclera shader colour; the cornea is a separate dielectric material.
        private static readonly Dictionary<ulong, int> eyeTiles = new Dictionary<ulong, int>();
        // The eye shader's color (TGPT_RegisterEyeColor, read from the game's compiled eye shader), baked into a detail tile,
        // under a key from what it is made of: characters with the same eyes share one tile. -1 where it can't be baked.
        private static int EyeTile(Material m)
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return -1;
            var d = new NativeEyeColor { Size = 88, Abi = Tgpt.Abi };
            var sclera = m.HasProperty("_Sclera_BaseColor") ? m.GetTexture("_Sclera_BaseColor") as Texture2D : null;
            var iris = m.HasProperty("_Iris_BaseColor") ? m.GetTexture("_Iris_BaseColor") as Texture2D : null;
            d.ScleraMap = NativeTexture(m, "_Sclera_BaseColor"); d.IrisMap = NativeTexture(m, "_Iris_BaseColor");
            Linear(ColorOr(m, "_Sclera_Tint"), out d.ScleraR, out d.ScleraG, out d.ScleraB, out d.ScleraA);
            Linear(ColorOr(m, "_Iris_Tint"), out d.IrisR, out d.IrisG, out d.IrisB, out d.IrisA);
            d.ScleraTiling = FloatOr(m, "_Sclera_Tiling", 1f); d.IrisTiling = FloatOr(m, "_Iris_Tiling", 1f);
            d.PupilSize = FloatOr(m, "_Pupil_Size", 1f); d.RingSize = FloatOr(m, "_Limbal_Ring_Size", 0f);
            d.RingIntensity = FloatOr(m, "_Limbal_Ring_Intensity", 1f); d.EmissionIntensity = FloatOr(m, "_Iris_Emission_Intensity", 1f);
            unchecked
            {
                ulong key = 1469598103934665603UL;
                void Mix(float v) { key = (key ^ (ulong)BitConverter.ToUInt32(BitConverter.GetBytes(v), 0)) * 1099511628211UL; }
                key = (key ^ (ulong)(uint)(sclera != null ? sclera.GetInstanceID() : 0)) * 1099511628211UL;
                key = (key ^ (ulong)(uint)(iris != null ? iris.GetInstanceID() : 0)) * 1099511628211UL;
                foreach (var v in new[] { d.ScleraR, d.ScleraG, d.ScleraB, d.IrisR, d.IrisG, d.IrisB, d.ScleraTiling, d.IrisTiling, d.PupilSize, d.RingSize, d.RingIntensity, d.EmissionIntensity }) Mix(v);
                if (eyeTiles.TryGetValue(key, out int known)) return known;
                int tile = tracer.RegisterEyeColor(key, ref d);
                eyeTiles[key] = tile;
                return tile;
            }
        }
        // RealHair's highlights (TGPT_MATERIAL_HAIR_LOBES), as its compiled shader feeds HDRP's Kajiya-Kay hair: two lobes along
        // the strands, the first shifted along the normal by lerp(_SpecularShiftMin, _SpecularShiftMax, its ID map; the range is
        // 0.05 in the game's hair, so the middle), the second _SecondarySpecularShift further; the first as smooth as
        // lerp(_SmoothnessMin, _SmoothnessMax, its mask map's alpha to _SmoothnessPower), the second _SecondarySmoothness; each
        // tinted by its specular map times _SpecularTint times _SpecularMultiplier, or _SecondarySpecularMultiplier. The strands'
        // direction (its flow map), the smoothness and the specular map texel by texel go into a lobe tile (TGPT_RegisterHairLobes).
        private static void HairLobes(Material m, ref NativeMaterial mat)
        {
            mat.HairLobeTile = -1; mat.Flags &= ~Tgpt.MaterialHairLobes;
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            var lobes = new NativeHairLobes { Size = 48, Abi = Tgpt.Abi, FlowMap = NativeTexture(m, "_FlowMap"), MaskMap = NativeTexture(m, "_MaskMap"),
                SpecularMap = NativeTexture(m, "_SpecularMap"), SmoothnessPower = FloatOr(m, "_SmoothnessPower", 1f), FlowFlipGreen = FloatOr(m, "_FlowMapFlipGreen", 0f) };
            int tile = tracer.RegisterHairLobes(unchecked((ulong)(uint)m.GetInstanceID()), ref lobes);
            if (tile < 0) return;
            mat.HairLobeTile = tile; mat.Flags |= Tgpt.MaterialHairLobes;
            mat.HairShift = 0.5f * (FloatOr(m, "_SpecularShiftMin", 0f) + FloatOr(m, "_SpecularShiftMax", 0f));
            mat.HairSecondaryShift = mat.HairShift + FloatOr(m, "_SecondarySpecularShift", 0f);
            mat.HairSmoothnessMin = Mathf.Clamp01(FloatOr(m, "_SmoothnessMin", 0.5f)); mat.HairSmoothnessMax = Mathf.Clamp01(FloatOr(m, "_SmoothnessMax", 0.5f));
            mat.HairSecondarySmoothness = Mathf.Clamp01(FloatOr(m, "_SecondarySmoothness", 0.5f));
            mat.HairStrength = Mathf.Max(FloatOr(m, "_SpecularMultiplier", 1f), 0f); mat.HairSecondaryStrength = Mathf.Max(FloatOr(m, "_SecondarySpecularMultiplier", 1f), 0f);
            Linear(ColorOr(m, "_SpecularTint"), out mat.HairTintR, out mat.HairTintG, out mat.HairTintB, out _);
        }
        // RealHair's color as its shader computes it (TGPT_RegisterHairColor, read from the game's compiled shader), baked
        // texel by texel into a tile of its own, under the material's key (called again, it bakes the tile again). -1 where
        // it can't be baked.
        private static int HairTile(Material m)
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return -1;
            var d = new NativeHairColor { Size = 240, Abi = Tgpt.Abi };
            d.DiffuseMap = NativeTexture(m, "_DiffuseMap"); d.RootMap = NativeTexture(m, "_RootMap"); d.IdMap = NativeTexture(m, "_IDMap");
            d.MaskMap = NativeTexture(m, "_MaskMap"); d.BlendMap = NativeTexture(m, "_BlendMap");
            Vector4 tiling = m.HasProperty("_Diffuse_Tiling") ? m.GetVector("_Diffuse_Tiling") : new Vector4(1f, 1f, 0f, 0f);
            Vector4 offset = m.HasProperty("_Diffuse_Offset") ? m.GetVector("_Diffuse_Offset") : Vector4.zero;
            d.TilingX = tiling.x; d.TilingY = tiling.y; d.OffsetX = offset.x; d.OffsetY = offset.y;
            Linear(ColorOr(m, "_DiffuseColor"), out d.DiffuseR, out d.DiffuseG, out d.DiffuseB, out d.DiffuseA);
            Linear(ColorOr(m, "_RootColor"), out d.RootR, out d.RootG, out d.RootB, out d.RootA);
            Linear(ColorOr(m, "_EndColor"), out d.EndR, out d.EndG, out d.EndB, out d.EndA);
            Linear(ColorOr(m, "_HighlightAColor"), out d.HighlightAR, out d.HighlightAG, out d.HighlightAB, out d.HighlightAA);
            Linear(ColorOr(m, "_HighlightBColor"), out d.HighlightBR, out d.HighlightBG, out d.HighlightBB, out d.HighlightBA);
            Vector4 a = m.HasProperty("_HighlightADistribution") ? m.GetVector("_HighlightADistribution") : Vector4.zero;
            Vector4 b = m.HasProperty("_HighlightBDistribution") ? m.GetVector("_HighlightBDistribution") : Vector4.zero;
            d.AStart = a.x; d.APeak = a.y; d.AEnd = a.z; d.AOverlapEnd = FloatOr(m, "_HighlightAOverlapEnd", 0f);
            d.BStart = b.x; d.BPeak = b.y; d.BEnd = b.z; d.BOverlapEnd = FloatOr(m, "_HighlightBOverlapEnd", 0f);
            d.BaseColorStrength = FloatOr(m, "_BaseColorStrength", 1f); d.GlobalStrength = FloatOr(m, "_GlobalStrength", 0f);
            d.RootColorStrength = FloatOr(m, "_RootColorStrength", 0f); d.EndColorStrength = FloatOr(m, "_EndColorStrength", 0f);
            d.InvertRootMap = FloatOr(m, "_InvertRootMap", 0f); d.HighlightBlend = FloatOr(m, "_HighlightBlend", 0f);
            d.HighlightAStrength = FloatOr(m, "_HighlightAStrength", 0f); d.HighlightBStrength = FloatOr(m, "_HighlightBStrength", 0f);
            d.HighlightAOverlapInvert = FloatOr(m, "_HighlightAOverlapInvert", 0f); d.HighlightBOverlapInvert = FloatOr(m, "_HighlightBOverlapInvert", 0f);
            d.DiffuseStrength = Mathf.Clamp(FloatOr(m, "_DiffuseStrength", 1f), 0f, 4f); d.BlendStrength = FloatOr(m, "_BlendStrength", 0f);
            d.AoStrength = FloatOr(m, "_AOStrength", 0f); d.AoOccludeAll = FloatOr(m, "_AOOccludeAll", 0f);
            // Its alpha test, for hair drawn as a layer: no hair below _AlphaClip, and a texel at least _DepthPrepass hides the
            // cards behind it (the game's transparent depth prepass, where it has one).
            d.AlphaClip = Mathf.Clamp01(CharacterCapture.GameCut(m, "_AlphaClip", 0.1f));
            // (The game's values: the layer's hiding raises both cuts past 1 on the material, CharacterCapture.GameCut.)
            d.AlphaPrepass = FloatOr(m, "_TransparentDepthPrepassEnable", 1f) > 0.5f ? Mathf.Clamp01(CharacterCapture.GameCut(m, "_DepthPrepass", 0.5f)) : 0f;
            int id = m.GetInstanceID();
            ulong key = unchecked((ulong)(uint)id);
            bool held = hairHolds.ContainsKey(id);
            int tile = tracer.RegisterHairColor(key, ref d);
            // No detail tile left for it (a 64x64 one came back, which takes it off the hair layer): hair that is gone gives
            // its tile back, and it asks again.
            if (!held && tile >= 0 && tile < Tgpt.FirstDetailTile && ReleaseIdleHairTiles(id))
            {
                tracer.ReleaseHairColor(key);
                tile = tracer.RegisterHairColor(key, ref d);
            }
            if (tile >= 0) hairHolds[id] = new HairHold { Material = m, Seen = Time.frameCount };
            // Colored anew, hair that shared its detail tile reads another one: its traced materials follow.
            if (held && tile >= Tgpt.FirstDetailTile) FollowHairTile(id, tile);
            return tile;
        }
        private static void HairTileSeen(Material m)
        {
            int id = m.GetInstanceID();
            if (hairHolds.TryGetValue(id, out var hold) && hold.Seen != Time.frameCount) { hold.Seen = Time.frameCount; hairHolds[id] = hold; }
        }
        // A hair material's traced materials (either texture state, read while opaque or transparent) take the detail tile its
        // color is in now.
        private static void FollowHairTile(int id, int tile)
        {
            var tracer = Session.Tracer;
            for (int variant = 0; variant < 4; variant++)
            {
                if (!characterMaterials.TryGetValue(unchecked(id * 4 + variant), out uint index) || index >= materialList.Count || !bakedHair.Contains(index)) continue;
                var mat = materialList[(int)index];
                if (mat.AlbedoTile == tile || mat.AlbedoTile < Tgpt.FirstDetailTile) continue;
                mat.AlbedoTile = tile;
                materialList[(int)index] = mat;
                if (index < materialsUploaded && tracer != null && tracer.Connected) tracer.SetMaterials(index, new[] { mat }, 1);
            }
        }
        // Hair that is gone gives its tile back to the pool: its material destroyed, or not traced for HairTileIdleFrames (its
        // character left the traced range). Its traced materials are forgotten with it, so that it is read anew, and gets a tile
        // again, if it comes back. keep: the material that is asking (0: none). True when a detail tile came free.
        private static bool ReleaseIdleHairTiles(int keep)
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected || hairHolds.Count == 0) return false;
            int now = Time.frameCount;
            List<int> gone = null;
            foreach (var pair in hairHolds)
                if (pair.Key != keep && (pair.Value.Material == null || now - pair.Value.Seen >= HairTileIdleFrames)) (gone ?? (gone = new List<int>())).Add(pair.Key);
            if (gone == null) return false;
            bool freed = false;
            foreach (int id in gone)
            {
                if (tracer.ReleaseHairColor(unchecked((ulong)(uint)id)) > 0) freed = true;
                hairHolds.Remove(id);
                for (int variant = 0; variant < 4; variant++) characterMaterials.Remove(unchecked(id * 4 + variant));
            }
            Session.Log.LogInfo("Detail tiles: " + gone.Count + " hair material" + (gone.Count == 1 ? "" : "s") + " no longer traced gave their tiles back" + (freed ? "" : " (none came free: others share them)"));
            return freed;
        }
        // How RealHair turns its texture's alpha into the hair's (read from its compiled shader, depth and forward passes alike):
        // pow(saturate(alpha / _AlphaRemap), _AlphaPower), for hair drawn as a layer (TGPT_Material.hairAlpha: the power in the
        // low half, the remap in the high). The game's hair has powers of 1, 0.5 (Tex_H_m008) and 0.25 (Tex_H_f001): taken as
        // the texture's alpha, such hair let the surface behind it show through its strands.
        private static uint HairAlpha(Material m)
        {
            float power = Mathf.Clamp(FloatOr(m, "_AlphaPower", 1f), 0.01f, 16f), remap = Mathf.Clamp(FloatOr(m, "_AlphaRemap", 1f), 0.001f, 16f);
            return Mathf.FloatToHalf(power) | ((uint)Mathf.FloatToHalf(remap) << 16);
        }
        // A scalp or beard mask (by name, as CharacterLeftOut knows them), and whether the tracer can draw it: it has a texture.
        internal static bool IsShell(Material m) => m != null &&
            (m.name.IndexOf("ScalpMask", StringComparison.OrdinalIgnoreCase) >= 0 || m.name.IndexOf("Stubble", StringComparison.OrdinalIgnoreCase) >= 0);
        internal static bool LayerShell(Material m)
        {
            if (!IsShell(m)) return false;
            string albedo = AlbedoProperty(m);
            return albedo != null && m.GetTexture(albedo) is Texture2D;
        }
        // Its texture through the hair color bake (TGPT_RegisterHairColor: no new call, no tile of another kind): the map's own
        // color (base color strength 1, every gradient, highlight and occlusion off, white tints) at the map's tiling, and its
        // alpha at its own size in the strand atlas. No hair below the mask's own cutoff (the game's, 0.05); no depth prepass
        // (the game blends a mask by its alpha). Its color (_BaseColor) is the traced material's, so a recolor bakes nothing.
        private static int ShellTile(Material m)
        {
            var tracer = Session.Tracer;
            string albedo = AlbedoProperty(m);
            if (tracer == null || !tracer.Connected || albedo == null) return -1;
            var d = new NativeHairColor { Size = 240, Abi = Tgpt.Abi };
            d.DiffuseMap = NativeTexture(m, albedo);
            if (d.DiffuseMap == 0UL) return -1;
            Vector2 scale = m.GetTextureScale(albedo), offset = m.GetTextureOffset(albedo);
            d.TilingX = scale.x; d.TilingY = scale.y; d.OffsetX = offset.x; d.OffsetY = offset.y;
            d.DiffuseR = d.DiffuseG = d.DiffuseB = d.DiffuseA = 1f;
            d.BaseColorStrength = 1f; d.DiffuseStrength = 1f;
            d.AlphaClip = Mathf.Clamp01(CharacterCapture.GameCut(m, "_AlphaCutoff", 0.05f));
            d.AlphaPrepass = 0f;
            // Keyed by what the tile holds (the texture, its tiling, the cutoff), not by the material: every character's clone
            // of a mask shares one tile (its color is the material's), where hair takes one a material. Bit 40 keeps these keys
            // apart from hair's (a material's 32-bit id).
            uint what = unchecked((uint)(scale.GetHashCode() * 31 + offset.GetHashCode()) * 31u + (uint)d.AlphaClip.GetHashCode());
            ulong key = (1UL << 40) | ((ulong)(what & 0x1FFFFFu) << 41) | unchecked((uint)m.GetTexture(albedo).GetInstanceID());
            return tracer.RegisterHairColor(key, ref d);
        }
        // The game colors a mask after it first shows, as it does the hair: the traced color follows.
        private static void RefreshShellColor(Material m, uint index, Color read)
        {
            Color now = ColorOr(m, "_BaseColor");
            if (now == read || index >= materialList.Count) return;
            shellColors[index] = now;
            var mat = materialList[(int)index];
            Linear(now, out mat.BaseR, out mat.BaseG, out mat.BaseB, out mat.BaseA);
            materialList[(int)index] = mat;
            var tracer = Session.Tracer;
            if (index < materialsUploaded && tracer != null && tracer.Connected) tracer.SetMaterials(index, new[] { mat }, 1);
        }
        private static ulong NativeTexture(Material m, string name)
        {
            var t = m.HasProperty(name) ? m.GetTexture(name) as Texture2D : null;
            if (t == null) return 0UL;
            IntPtr native = t.GetNativeTexturePtr();
            TextureShare.Character(t, native); // characters' maps (hair, eyes): PT-45, followed when mip streaming makes them anew
            return unchecked((ulong)native.ToInt64());
        }
        private static void Linear(Color c, out float r, out float g, out float b, out float a) { var l = c.linear; r = l.r; g = l.g; b = l.b; a = c.a; }
        // A material's colors, vectors, numbers and textures as its shader names them, for the log.
        private static string ShaderValues(Material m, Shader shader)
        {
            var sb = new System.Text.StringBuilder();
            try
            {
                for (int i = 0; i < shader.GetPropertyCount() && sb.Length < 6000; i++)
                {
                    string name = shader.GetPropertyName(i);
                    switch (shader.GetPropertyType(i))
                    {
                        case ShaderPropertyType.Color: case ShaderPropertyType.Vector: sb.Append(name).Append(' ').Append(m.GetVector(name).ToString("0.###")).Append("; "); break;
                        case ShaderPropertyType.Float: case ShaderPropertyType.Range: sb.Append(name).Append(' ').Append(m.GetFloat(name).ToString("0.###")).Append("; "); break;
                        case ShaderPropertyType.Texture:
                            var t = m.GetTexture(name);
                            sb.Append(name).Append(t != null ? " (\"" + t.name + "\" " + t.width + "x" + t.height + ", scale " + m.GetTextureScale(name).ToString("0.###") +
                                ", offset " + m.GetTextureOffset(name).ToString("0.###") + "); " : " (none); ");
                            break;
                    }
                }
            }
            catch (Exception ex) { sb.Append("(" + ex.Message + ")"); }
            return sb.ToString();
        }
        // Materials added since the last upload go to the native side now (characters' arrive between scans).
        internal static void FlushMaterials()
        {
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected || materialList.Count <= materialsUploaded) return;
            var added = materialList.GetRange(materialsUploaded, materialList.Count - materialsUploaded).ToArray();
            MaterialPrograms.Flush(tracer);
            if (tracer.SetMaterials((uint)materialsUploaded, added, (uint)added.Length) == 0) materialsUploaded = materialList.Count;
        }
        private static readonly string[] AlbedoNames = { "_BaseColorMap", "_BaseMap", "_MainTex", "_AlbedoMap", "_Albedo", "_BaseColorTexture", "_DiffuseMap", "_Diffuse", "_ColorMap", "_UnlitColorMap" };
        private static readonly string[] ColorNames = { "_BaseColor", "_Color", "_AlbedoColor", "_Tint", "_TintColor", "_UnlitColor" };
        private static readonly string[] UnlitColorNames = { "_UnlitColor", "_BaseColor", "_Color" };
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
        // For the F8 capture's report: names of the meshes, instances and materials the traced scene holds.
        private static readonly Dictionary<int, string> meshNames = new Dictionary<int, string>();
        private static readonly Dictionary<uint, string> materialNames = new Dictionary<uint, string>();
        // A traced material by its index, for a log line (PT-95 on trial: TextureShare.Audit names who reads a texture).
        internal static string MaterialLabel(int index) => index >= 0 && materialNames.TryGetValue((uint)index, out var name) ? "\"" + name + "\"" : "material #" + index;
        private static NativeInstance[] lastInstances;
        // The yardstick's numbers in words, the materials by their names (PT-59): the tracer's own first hit against HDRP's
        // G-buffer over the last frame counted. Empty until one was.
        internal static unsafe string YardstickText(NativeTracer tracer)
        {
            if (tracer == null || !tracer.Yardstick(out var y)) return "";
            double all = y.Pixels, same = Math.Max(y.Where[1], 1u);
            var sb = new System.Text.StringBuilder();
            sb.Append("the same surface " + (100.0 * y.Where[1] / all).ToString("0.0") + " % of the screen (of those pixels another colour " + (100.0 * y.Colour / same).ToString("0.0") +
                " %, another normal " + (100.0 * y.Normal / same).ToString("0.0") + " %, another roughness " + (100.0 * y.Roughness / same).ToString("0.0") + " %, drawn forward " +
                (100.0 * y.Forward / same).ToString("0.0") + " %), the tracer's hit nearer " + (100.0 * y.Where[2] / all).ToString("0.00") + " %, farther " + (100.0 * y.Where[3] / all).ToString("0.00") +
                " %, nothing traced where the game drew " + (100.0 * y.Where[4] / all).ToString("0.00") + " %, something traced where the game has sky " + (100.0 * y.Where[5] / all).ToString("0.00") +
                " %, sky for both " + (100.0 * y.Where[0] / all).ToString("0.0") + " %; its own motion more than a pixel from the game's " + (100.0 * y.Motion / all).ToString("0.00") + " %");
            // PT-40, who takes the scattering walk: the screen's share on materials light walks inside, and what took it this frame.
            if (tracer.WalkPixels(out uint onSubsurface, out uint walked))
                sb.Append("; on materials light walks inside " + (100.0 * onSubsurface / all).ToString("0.00") + " % of the screen, " + (100.0 * walked / all).ToString("0.00") + " % took the walk in this frame");
            // PT-71 item 4: the things the game moves, placed where HDRP draws them this frame (the yardstick's kind 3, by place).
            uint movingPixels = 0;
            for (int w = 1; w < 6; ++w) movingPixels += y.Kinds[3 * 6 + w];
            if (movingPixels > 0)
                sb.Append("; things the game moves " + (100.0 * movingPixels / all).ToString("0.00") + " % of the screen, of their pixels the same surface as drawn " +
                    (100.0 * y.Kinds[3 * 6 + 1] / movingPixels).ToString("0.0") + " %, nearer " + (100.0 * y.Kinds[3 * 6 + 2] / movingPixels).ToString("0.0") +
                    " %, farther " + (100.0 * y.Kinds[3 * 6 + 3] / movingPixels).ToString("0.0") + " %");
            // PT-64: each with how its own pixels differ, so a line tells the material's part (colour, normal, roughness) from the
            // geometry's (place: the tracer's hit nearer, farther or over sky there).
            if (y.MaterialCount > 0) sb.Append("; materials with the most differing pixels (% of the screen, % of its own pixels; of its own pixels: in another place, another colour, normal, roughness):");
            for (int i = 0; i < y.MaterialCount && i < 16; ++i)
            {
                uint material = y.Materials[i * 7], pixels = Math.Max(y.Materials[i * 7 + 1], 1u), differing = y.Materials[i * 7 + 2];
                sb.Append((i > 0 ? "; " : " ") + (materialNames.TryGetValue(material, out var name) ? "\"" + name + "\"" : "material") + " #" + material + " " +
                    (100.0 * differing / all).ToString("0.00") + ", " + (100.0 * differing / pixels).ToString("0") + " (place " + (100.0 * y.Materials[i * 7 + 3] / pixels).ToString("0") +
                    ", colour " + (100.0 * y.Materials[i * 7 + 4] / pixels).ToString("0") + ", normal " + (100.0 * y.Materials[i * 7 + 5] / pixels).ToString("0") +
                    ", roughness " + (100.0 * y.Materials[i * 7 + 6] / pixels).ToString("0") + ")");
            }
            return sb.ToString();
        }
        internal static string DescribeTraced(uint instance, uint material)
        {
            string what = lastInstances != null && instance < lastInstances.Length
                ? "instance " + instance + ", mesh \"" + (meshNames.TryGetValue(lastInstances[instance].Mesh, out var mesh) ? mesh : "#" + lastInstances[instance].Mesh) + "\""
                : "instance " + instance + " (past the scene's " + (lastInstances != null ? lastInstances.Length : 0) + ": a thing the game moves, a character, or a piece of the water's surface)";
            return what + ", material " + material + (materialNames.TryGetValue(material, out var name) ? " \"" + name + "\"" : "");
        }
        internal static uint MaterialIndex(Material m)
        {
            if (m == null) return Neutral;
            // DBuffer meshes modify a receiver; they are not opaque geometry or shadow casters.
            if (DecalCapture.IsMeshMaterial(m)) return Invisible;
            int id = m.GetInstanceID();
            if (materials.TryGetValue(id, out uint index)) return index;
            index = ReadMaterial(m);
            if (index > Neutral && (m.IsKeywordEnabled("_DISABLE_DECALS") || (m.HasProperty("_SupportDecals") && m.GetFloat("_SupportDecals") == 0)))
            {
                var receiver = materialList[(int)index]; receiver.Flags |= Tgpt.MaterialNoDecals; materialList[(int)index] = receiver;
            }
            materials[id] = index;
            materialNames[index] = m.name + " (" + (m.shader != null ? m.shader.name : "no shader") +
                ((materialList[(int)index].Flags & Tgpt.MaterialMaskTile) != 0 ? ", its mask map traced" : "") +
                (materialList[(int)index].Program != 0 ? ", computed as its shader computes it" : "") + ")";
            return index;
        }
        private static uint ReadMaterial(Material m)
        {
            var shader = m.shader;
            string shaderName = shader != null ? shader.name : "";
            // HDRP's water system draws the lakes, rivers and sea with passes of its own (WaterGBuffer), from a mesh renderer's
            // mesh too (custom water, which its renderer may still carry): to rays that would be a flat, solid plane, shadowing
            // the lake bed and stopping the water's own reflection rays in the troughs of the waves HDRP displaces the drawn
            // surface by on the GPU. Rays pass through water; its reflections are traced from HDRP's own surface (render event 4).
            if (m.FindPass("WaterGBuffer") >= 0)
            {
                if (shadersSeen.Add(shaderName)) Session.Log.LogInfo("Traced material shader \"" + shaderName + "\": HDRP's water, invisible to rays (its reflections are traced from its own surface).");
                return Invisible;
            }
            // Opaque Unlit surfaces emit their shader colour to every ray. Transparent
            // effects remain in the later particles/transparency migration.
            bool unlit = shaderName.IndexOf("Unlit", StringComparison.OrdinalIgnoreCase) >= 0;
            bool transparent = !LayerBrow(m) && ((m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f) || m.renderQueue >= 2750 ||
                shaderName.IndexOf("NoRender", StringComparison.OrdinalIgnoreCase) >= 0);
            bool unlitSurface = unlit && !transparent;
            // Glass HDRP draws as a transparent surface (a jar, a window, a potion: GlassFinish's names): the tracer draws it,
            // classified as TGPT_MATERIAL_SEE_THROUGH, and HDRP's own passes for it are
            // hidden while tracing. Left invisible to rays, the alchemist's jars neither refracted nor shadowed anything.
            bool seeThrough = transparent && !unlit && GlassFinish.IsGlass(m) && shaderName.IndexOf("NoRender", StringComparison.OrdinalIgnoreCase) < 0;
            if ((transparent && !seeThrough) || (unlit && !unlitSurface))
            {
                // PT-35: a transparent material named like glass that rays will not see, and why (once per name).
                if (transparent) GlassFinish.ExplainNotSeeThrough(m, shaderName, unlit);
                if (shadersSeen.Add(shaderName)) Session.Log.LogInfo("Traced material shader \"" + shaderName + "\": invisible to rays (transparent or unlit).");
                return Invisible;
            }
            TextureShare.BeginMaterial();
            var mat = new NativeMaterial { BaseA = 1f, ScaleX = 1f, ScaleY = 1f, AlbedoTile = -1, EmissiveTile = -1, Smoothness = 0.5f, ColorTexture = -1, NormalTexture = -1, MaskTexture = -1,
                EmissiveTexture = -1 };
            var baseColor = Vector3.one; float alpha = 1f;
            // HDRP's Unlit keeps its color in _UnlitColor and _UnlitColorMap; its _Color and _MainTex are there for Unity's
            // lightmapper and stay white.
            foreach (var name in unlitSurface ? UnlitColorNames : ColorNames) if (m.HasProperty(name)) { var c = m.GetColor(name); baseColor = LinearColor(c); alpha = c.a; break; }
            // The game's grass and leaves (TG/Vegetation) tint their texture between two colours by a noise over the world: what a
            // ray meets of them is that texture times the two's mean. Times white, the light bounced off grass was 1.7 to 2.1 times
            // HDRP's own colour of it (the Materials view at Horns of the South), and pale.
            if (!unlitSurface && m.HasProperty("_HealthyColor") && m.HasProperty("_DryColor"))
                baseColor = Vector3.Scale(baseColor, 0.5f * (LinearColor(m.GetColor("_HealthyColor")) + LinearColor(m.GetColor("_DryColor"))));
            string albedo = unlitSurface && m.HasProperty("_UnlitColorMap") ? (m.GetTexture("_UnlitColorMap") != null ? "_UnlitColorMap" : null) : AlbedoProperty(m);
            if (albedo != null)
            {
                mat.AlbedoTile = seeThrough ? DetailTile(m.GetTexture(albedo)) : Tile(m.GetTexture(albedo));
                mat.ColorTexture = TextureShare.Handle(m.GetTexture(albedo), TextureShare.FieldColor); // PT-46: read as the game reads it, the tile the fallback
                var scale = m.GetTextureScale(albedo); var offset = m.GetTextureOffset(albedo);
                mat.ScaleX = scale.x; mat.ScaleY = scale.y; mat.OffsetX = offset.x; mat.OffsetY = offset.y;
            }
            mat.BaseR = baseColor.x; mat.BaseG = baseColor.y; mat.BaseB = baseColor.z; mat.BaseA = alpha;
            ShaderGraphTiling(m, shaderName, ref mat);
            // Read the material finish for opaque surfaces and dielectric interfaces alike.
            string mask = ReadFinish(m, shaderName, ref mat, true);
            if (!unlitSurface) { mat.NormalTexture = TextureShare.Normal(m, shaderName, out float normalScale); mat.NormalScale = normalScale; } // PT-46
            // PT-64. The game's graphs scale a normal map with Shader Graph's Normal Strength (Kandra_Lit's compiled code).
            if (shaderName.StartsWith("TG/", StringComparison.Ordinal) || shaderName.StartsWith("Shader Graphs/", StringComparison.Ordinal)) mat.TextureFlags |= Tgpt.TextureGraphNormal;
            // A back face of HDRP's Double-Sided "Flip" mode turns its bumps around with the surface (its constants -1, -1, -1).
            if (m.HasProperty("_DoubleSidedEnable") && m.GetFloat("_DoubleSidedEnable") > 0.5f && m.HasProperty("_DoubleSidedConstants") && m.GetVector("_DoubleSidedConstants").x < 0f)
                mat.TextureFlags |= Tgpt.TextureFlipBack;
            // The material's own shader, where the tracer computes it at a hit (its fields above set as that reads them).
            string program = unlitSurface ? null : MaterialPrograms.Build(m, shaderName, ref mat);
            // HDRP stores the emissive color already multiplied by its intensity (linear, in nits): HDRP Lit's shader reads
            // that alone (its material editor writes _EmissiveColorLDR times _EmissiveIntensity into it), and nits are the
            // tracer's radiance (its lights in candela and lux, as HDRP's). The shader then divides it by the exposure as far
            // as the exposure weight is below 1 (at 0 a glow shows equally bright whatever the exposure), and multiplies it by
            // the albedo with Albedo Affects Emissive: the light it casts must be the light the camera sees.
            // HDRP's own shaders add _EmissiveColor whatever the material's lightmap flags say: HDRP's Baked Emission toggle writes
            // EmissiveIsBlack while it is off (its default), which only keeps a glow out of baked lightmaps. Read as "no glow", it
            // left every glowing surface of the game unread: no emissive submesh in any harness run up to 2026-10-06, and the
            // mineral cave's crystals (HDRP/Lit with an emissive color map) lit nothing (PT-34). Other shaders keep the flag's word.
            bool notBaked = (m.globalIlluminationFlags & MaterialGlobalIlluminationFlags.EmissiveIsBlack) != 0;
            float exposureWeight = 1f; Color emission = Color.black;
            // PT-64. The game's graphs that glow (read from their compiled code: Kandra_Lit, its grass and its leaves) show their
            // emissive color times their map times 2^(intensity - 3), brought back out of the exposure as far as the exposure
            // itself says (so it shows equally bright whatever the exposure: GraphExposure), whatever the material's baked flag
            // or its leftover HDRP Lit values say. Read HDRP Lit's way, a character's glowing parts were traced at their plain
            // color in nits: some ten thousand times too dark by day.
            GraphGlow.TryGetValue(shaderName, out var glow);
            if (m.HasProperty("_EmissiveColor") && (!notBaked || glow != null || shaderName.StartsWith("HDRP/", StringComparison.Ordinal)))
            {
                var e = m.GetColor("_EmissiveColor");
                if (glow != null) e *= m.HasProperty(glow.Intensity) ? Mathf.Pow(2f, m.GetFloat(glow.Intensity) - 3f) : 0f;
                string emissiveMap = glow != null ? glow.Map : "_EmissiveColorMap";
                if (e.maxColorComponent > 0.001f)
                {
                    emission = e;
                    exposureWeight = glow != null ? GraphExposure : m.HasProperty("_EmissiveExposureWeight") ? Mathf.Clamp01(m.GetFloat("_EmissiveExposureWeight")) : 1f;
                    var shown = e * ExposureScale(exposureWeight);
                    mat.EmissiveR = shown.r; mat.EmissiveG = shown.g; mat.EmissiveB = shown.b;
                    if (m.HasProperty(emissiveMap) && m.GetTexture(emissiveMap) != null)
                    {
                        mat.EmissiveTile = Tile(m.GetTexture(emissiveMap));
                        // The map itself, read at the hit (PT-64: no 64x64 copy where the tracer has it open).
                        mat.EmissiveTexture = TextureShare.Handle(m.GetTexture(emissiveMap), TextureShare.FieldEmissive);
                        if (glow != null && glow.RedOnly) mat.TextureFlags |= Tgpt.TextureEmissiveRed;
                    }
                    if (m.HasProperty("_AlbedoAffectEmissive") && m.GetFloat("_AlbedoAffectEmissive") > 0.5f) mat.Flags |= Tgpt.MaterialEmissiveAlbedo;
                    if (emissiveLogged++ < 40)
                        Session.Log.LogInfo("Emissive material \"" + m.name + "\" (" + shaderName + "): " + e.r.ToString("0.###") + ", " + e.g.ToString("0.###") + ", " +
                            e.b.ToString("0.###") + " nits" + (mat.EmissiveTile >= 0 ? " times its emissive map" : "") +
                            ((mat.Flags & Tgpt.MaterialEmissiveAlbedo) != 0 ? " times its albedo" : "") +
                            (exposureWeight < 0.999f ? ", exposure weight " + exposureWeight.ToString("0.##") + " (x" + ExposureScale(exposureWeight).ToString("0.#") + " at the exposure now)" : "") +
                            (notBaked ? ", Baked Emission off (read all the same)" : ""));
                }
                // A glow map with no glow color: dark to the tracer, glowing only if the game sets its color per renderer.
                else if (m.IsKeywordEnabled("_EMISSIVE_COLOR_MAP") && emissiveLogged++ < 40)
                    Session.Log.LogInfo("Emissive material \"" + m.name + "\" (" + shaderName + ") has an emissive map but _EmissiveColor " + e + ": traced dark");
            }
            else if (!m.HasProperty("_EmissiveColor")) LogUnreadEmission(m, shaderName);
            bool alphaTest = (m.HasProperty("_AlphaCutoffEnable") && m.GetFloat("_AlphaCutoffEnable") > 0.5f) || m.IsKeywordEnabled("_ALPHATEST_ON") ||
                shaderName.IndexOf("Leaves", StringComparison.OrdinalIgnoreCase) >= 0 || shaderName.IndexOf("Grass", StringComparison.OrdinalIgnoreCase) >= 0 ||
                shaderName.IndexOf("Billboard", StringComparison.OrdinalIgnoreCase) >= 0;
            if (alphaTest && (mat.AlbedoTile >= 0 || mat.ColorTexture >= 0))
            {
                mat.Flags |= Tgpt.MaterialAlphaTest;
                mat.AlphaCutoff = m.HasProperty("_AlphaCutoff") ? m.GetFloat("_AlphaCutoff") : m.HasProperty("_Cutoff") ? m.GetFloat("_Cutoff") : 0.5f;
                int alphaTile = AlphaTile(m.GetTexture(albedo), mat.AlphaCutoff / Mathf.Max(mat.BaseA, 1e-3f));
                // Its tile holds coverage for that cutoff: cut out where less than half a texel is covered.
                // Keep the material's cutoff for its original texture. The fallback tile stores coverage and uses 0.5.
                if (alphaTile >= 0) { mat.AlphaTile = alphaTile; mat.Flags |= Tgpt.MaterialAlphaTile; }
            }
            if (m.HasProperty("_DoubleSidedEnable") && m.GetFloat("_DoubleSidedEnable") > 0.5f) mat.Flags |= Tgpt.MaterialDoubleSided;
            // The game's shaders that bend their vertices in the wind (grass, leaves, banners and laundry): traced where they
            // stand still, which rays from HDRP's surfaces don't trust near them (pt_common.hlsli MASK_SWAYS).
            if (Sways(shaderName)) mat.Flags |= Tgpt.MaterialSways;
            // Lit through from behind where a path meets it, as HDRP lights it through: the game's grass and leaves, and HDRP
            // Lit's Translucent material type.
            if (Translucent(shaderName, m)) mat.Flags |= Tgpt.MaterialTranslucent;
            // Thin cloth that hangs (the game's cloth in the wind, banners, curtains, canvas, laundry, a sheet drawn from both sides)
            // lets light through its weave: found by what it is (ClothRules), characters' clothes left as they are.
            else if (ClothRules.IsThinCloth(shaderName, m.name, (mat.Flags & Tgpt.MaterialDoubleSided) != 0))
            {
                mat.Flags |= Tgpt.MaterialThinCloth;
                // Tested per ray, so that a light's shadow ray can pass its weave (pt_common alpha_opaque): solid where its texture
                // isn't cut out, as glass is.
                if ((mat.Flags & Tgpt.MaterialAlphaTest) == 0) { mat.AlphaCutoff = 0f; mat.Flags |= Tgpt.MaterialAlphaTest; }
            }
            // Glass: what the game itself draws see-through (seeThrough: a transparent material GlassFinish names as glass, a
            // cornea). Light uses its optical values and finish at every interface, and its alpha (its base color's times its
            // texture's) is the share of a texel that is paint, a label or a lid on it: a lit surface there (pt_common
            // see_through_cover), as HDRP blends it.
            // A bottle the game draws OPAQUE is not glass, whatever its name: its material is a painted, opaque surface (the
            // tavern's cider, mead, vodka, wine and beer bottles and its painted window panes are HDRP/Lit with surface type 0,
            // the glaze, the label and the cork in one texture), and that is what is traced. Taken for glass by their names
            // (until 2026-10-08), with no color of their own since the glass migration, they were clear glass from cork to
            // base: black shapes showing the dark wall behind them, their colors and labels gone (run 20261007-224654).
            if (seeThrough)
            {
                if ((mat.Flags & Tgpt.MaterialAlphaTest) == 0) mat.AlphaCutoff = 0f;
                mat.Flags |= Tgpt.MaterialAlphaTest | Tgpt.MaterialGlass | Tgpt.MaterialSeeThrough;
                GlassFinish.ReadOptics(m, ref mat);
                GlassFinish.Add(m);
                // Its tile is what the camera sees of its texture where the game's own isn't shared (PT-46).
                GlassFinish.AddSeeThrough(m, alpha, albedo == null ? "no texture" :
                    mat.AlbedoTile >= Tgpt.FirstDetailTile ? "its texture's tile 512x512 (a detail tile)" :
                    mat.AlbedoTile >= 0 ? "its texture's tile 64x64 (no detail tile left: its stains in blocks)" : "its texture not read");
            }
            else if (!unlitSurface) GlassFinish.ExplainOpaque(m, shaderName);
            // PT-40: the light inside it, by the material's own diffusion profile: a closed thing with subsurface scattering (wax,
            // skin) is walked through where a path meets it, a leaf or a blade passes its own share of the light from behind.
            if (!unlitSurface) HdrpPasses.ReadSubsurface(m, shaderName, ref mat);
            if (unlitSurface)
            {
                // Every ray applies its alpha cut; the surface emits its own colour.
                if ((mat.Flags & Tgpt.MaterialAlphaTest) == 0) mat.AlphaCutoff = 0f;
                mat.Flags |= Tgpt.MaterialAlphaTest;
                mat.SurfaceModel = 1;
                if (unlitSeen.Add(m.name))
                    Session.Log.LogInfo("Unlit material \"" + m.name + "\": emission visible to every ray (albedo " + (albedo ?? "none") + ")");
            }
            if (shadersSeen.Add(shaderName))
                Session.Log.LogInfo("Traced material shader \"" + shaderName + "\": albedo " + (albedo ?? "none") + (mat.AlbedoTile >= 0 ? " (tile " + mat.AlbedoTile + ")" : "") +
                    ", color (" + baseColor.x.ToString("0.00") + ", " + baseColor.y.ToString("0.00") + ", " + baseColor.z.ToString("0.00") + ")" +
                    ((mat.Flags & Tgpt.MaterialAlphaTest) != 0 ? ", alpha test " + mat.AlphaCutoff.ToString("0.00") : "") +
                    (mat.EmissiveR + mat.EmissiveG + mat.EmissiveB > 0 ? ", emissive" : "") + (mask != null ? ", mask map " + mask : "") +
                    (program != null ? ", computed as the game's shader computes it" : "") +
                    (albedo == null ? "; its textures: " + string.Join(", ", m.GetTexturePropertyNames()) : ""));
            materialList.Add(mat);
            TextureShare.Bind(materialList.Count - 1); // PT-46: re-registered when mip streaming makes its textures anew
            if (exposureWeight < 0.999f && mat.EmissiveR + mat.EmissiveG + mat.EmissiveB > 0f) exposureWeighted.Add((materialList.Count - 1, emission, exposureWeight));
            if (shaderName == ClothRules.ClothWindShader) AddClothWind(m, (uint)(materialList.Count - 1));
            // PT-63: grass, leaves and trunks in the game's wind are traced in the pose HDRP draws them (PlantWind).
            PlantWind.Add(m, shaderName, (uint)(materialList.Count - 1));
            return (uint)(materialList.Count - 1);
        }
        internal static bool Translucent(string shaderName, Material m) =>
            shaderName.IndexOf("Vegetation_Grass", StringComparison.OrdinalIgnoreCase) >= 0 ||
            shaderName.IndexOf("Vegetation_Leaves", StringComparison.OrdinalIgnoreCase) >= 0 ||
            (shaderName == "HDRP/Lit" && m.HasProperty("_MaterialID") && Mathf.RoundToInt(m.GetFloat("_MaterialID")) == 5);
        // The wind on every material the game sways as cloth (TG/Clothes/Cloth_Wind), by its traced material index: sent with
        // each frame (SendClothWind) so the tracer moves its copy as HDRP's vertex shader moves the drawn one (pt_sway, PT-49).
        private static readonly List<NativeClothWind> clothWinds = new List<NativeClothWind>();
        private static NativeClothWind[] clothWindArray = new NativeClothWind[0];
        private static void AddClothWind(Material m, uint index)
        {
            if (clothWinds.Count >= 256) return; // TGPT_MAX_CLOTH_WIND
            Vector4 intensity = m.HasProperty("_Intensity") ? m.GetVector("_Intensity") : Vector4.zero;
            Vector4 direction = m.HasProperty("_Direction") ? m.GetVector("_Direction") : Vector4.zero;
            float worldSpace = m.HasProperty("_UseWorldSpace") ? m.GetFloat("_UseWorldSpace") : 0f;
            float vertexPaint = m.HasProperty("_UseVertexPaint") ? m.GetFloat("_UseVertexPaint") : 0f;
            var w = new NativeClothWind
            {
                Material = index,
                Speed = m.HasProperty("_Speed") ? m.GetFloat("_Speed") : 0f,
                Wind = m.HasProperty("_Wind") ? m.GetFloat("_Wind") : 0f,
                NoiseScale = m.HasProperty("_NoiseScale") ? m.GetFloat("_NoiseScale") : 0f,
                IntensityX = intensity.x, IntensityY = intensity.y, IntensityZ = intensity.z,
                DirectionX = direction.x, DirectionY = direction.y,
                // As the compiled shader uses them (its UnityPerMaterial's 8th float4: x _UseWorldSpace, y _UseVertexPaint): the
                // first masks the sway by the vertex colors, the second takes the noise from the position (tgpt_api.h).
                Flags = (worldSpace != 0f ? ClothRules.WindVertexPaint : 0u) | (vertexPaint != 0f ? ClothRules.WindPositionUv : 0u),
            };
            clothWinds.Add(w);
            clothWindArray = clothWinds.ToArray();
            if (clothWinds.Count <= 40)
                Session.Log.LogInfo("Cloth in the wind \"" + m.name + "\" (material " + index + "): speed " + w.Speed.ToString("0.###") + ", wind " + w.Wind.ToString("0.###") +
                    ", noise scale " + w.NoiseScale.ToString("0.###") + ", intensity (" + intensity.x.ToString("0.###") + ", " + intensity.y.ToString("0.###") + ", " +
                    intensity.z.ToString("0.###") + "), direction (" + direction.x.ToString("0.###") + ", " + direction.y.ToString("0.###") + "), world space " + worldSpace +
                    ", vertex paint " + vertexPaint + ": traced in the pose HDRP draws it");
        }
        // This frame's shader time (HDRP's _TimeParameters.x is Time.time) and the wind on the cloth: the tracer poses its copies so.
        internal static void SendClothWind(NativeTracer tracer)
        {
            if (tracer == null || !tracer.Connected) return;
            tracer.SetClothWind(Time.time, clothWindArray, (uint)clothWindArray.Length);
            PlantWind.Send(tracer);
        }
        // What the wind moves and the tracer may still trace standing still: cloth past the copies the tracer poses
        // (scene.hpp kMaxSwayCopies), which rays then do not trust near their start. Grass, leaves and trunks are posed, every
        // one of them (PlantWind), so they are never flagged: every ray meets them where they are (PT-63).
        internal static bool Sways(string shaderName) => shaderName.IndexOf("Cloth_Wind", StringComparison.OrdinalIgnoreCase) >= 0;
        // HDRP's exposure multiplier now (HdrpPasses reads it back), or the game's usual one outdoors (EV 13) until it has.
        private static float CurrentExposure() => float.IsNaN(HdrpPasses.LiveExposureLog) ? 1f / (1.2f * 8192f) : Mathf.Pow(2f, HdrpPasses.LiveExposureLog);
        // What HDRP Lit multiplies an emissive color by for its exposure weight: lerp(1 / exposure, 1, weight).
        internal static float ExposureScale(float weight)
        {
            if (weight == GraphExposure) weight = Mathf.Clamp01(CurrentExposure()); // the game's graphs: the exposure itself is the weight
            return weight >= 0.999f ? 1f : Mathf.Lerp(1f / Mathf.Max(CurrentExposure(), 1e-12f), 1f, weight);
        }
        // An exposure weight that stands for "the exposure itself" (ExposureScale): what the game's glowing graphs compute.
        private const float GraphExposure = -1f;
        // The game's graphs that glow by HDRP's _EmissiveColor name, each with its map and its intensity (an exposure value) as
        // their compiled code reads them; its grass and leaves scale by the map's red alone.
        private sealed class Glow { internal string Map, Intensity; internal bool RedOnly; }
        private static readonly Dictionary<string, Glow> GraphGlow = new Dictionary<string, Glow>
        {
            ["TG/Character/Kandra_Lit"] = new Glow { Map = "_EmissiveColorMap", Intensity = "_EmissionIntensity" },
            [MaterialPrograms.GrassShader] = new Glow { Map = "_EmissiveMap", Intensity = "_EmissiveIntensity", RedOnly = true },
            [MaterialPrograms.LeavesShader] = new Glow { Map = "_EmissiveMap", Intensity = "_EmissiveIntensiy", RedOnly = true },
        };
        // Materials whose emission follows the exposure (a weight below 1): their color, weight and index, sent again whenever the
        // exposure moved by a twentieth of a stop, so the light they cast stays what the camera sees them glow with.
        private static readonly List<(int index, Color emission, float weight)> exposureWeighted = new List<(int, Color, float)>();
        private static float exposureFollowed = float.NaN;
        private static int emissiveLogged;
        private static void FollowExposure()
        {
            float now = HdrpPasses.LiveExposureLog;
            if (exposureWeighted.Count == 0 || float.IsNaN(now) || (!float.IsNaN(exposureFollowed) && Mathf.Abs(now - exposureFollowed) < 0.05f)) return;
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            exposureFollowed = now;
            foreach (var (index, emission, weight) in exposureWeighted)
            {
                if (index >= materialList.Count) continue;
                var mat = materialList[index];
                var shown = emission * ExposureScale(weight);
                mat.EmissiveR = shown.r; mat.EmissiveG = shown.g; mat.EmissiveB = shown.b;
                materialList[index] = mat;
                if (index < materialsUploaded) tracer.SetMaterials((uint)index, new[] { mat }, 1u);
            }
        }
        // Once per shader: emission by names other than HDRP Lit's, which the tracer doesn't read (a glowing surface drawn with
        // a shader of the game's own lights nothing), so the log tells which.
        private static readonly HashSet<string> unreadEmissionSeen = new HashSet<string>();
        private static void LogUnreadEmission(Material m, string shaderName)
        {
            var shader = m.shader;
            if (shader == null || !unreadEmissionSeen.Add(shaderName)) return;
            var found = new List<string>();
            for (int i = 0; i < shader.GetPropertyCount(); i++)
            {
                string name = shader.GetPropertyName(i);
                if (name.IndexOf("emiss", StringComparison.OrdinalIgnoreCase) < 0 && name.IndexOf("glow", StringComparison.OrdinalIgnoreCase) < 0) continue;
                var type = shader.GetPropertyType(i);
                string value = type == ShaderPropertyType.Color ? m.GetColor(name).ToString() : type == ShaderPropertyType.Vector ? m.GetVector(name).ToString()
                    : type == ShaderPropertyType.Texture ? (m.GetTexture(name) != null ? "a texture" : "no texture") : m.GetFloat(name).ToString("0.###");
                found.Add(name + " " + value);
            }
            if (found.Count > 0)
                Session.Log.LogInfo("Traced material shader \"" + shaderName + "\" has emission the tracer doesn't read (not HDRP Lit's _EmissiveColor): " +
                    string.Join(", ", found.ToArray()) + " (material \"" + m.name + "\")");
        }
        private static bool IsAlphaTested(uint material) => material == Invisible || (materialList[(int)material].Flags & Tgpt.MaterialAlphaTest) != 0;

        private static void AddInstance(List<NativeInstance> instances, List<uint> indices, int handle, in Matrix4x4 m, uint[] submeshMaterials, uint flags = 0u, uint renderingLayers = 1u)
        {
            var inst = new NativeInstance
            {
                M00 = m.m00, M01 = m.m01, M02 = m.m02, M03 = m.m03, M10 = m.m10, M11 = m.m11, M12 = m.m12, M13 = m.m13,
                M20 = m.m20, M21 = m.m21, M22 = m.m22, M23 = m.m23, Mesh = handle, MaterialBase = (uint)indices.Count, Mask = 0xFF, Flags = flags, RenderingLayerMask = renderingLayers
            };
            bool alpha = false;
            uint glowing = uint.MaxValue; float brightest = 0f;
            // PT-104, for the log alone: a mesh without texture coordinates gives the tracer no frame for a normal map.
            bool noCoordinates = MaterialPrograms.NoCoordinates(handle), flat = false;
            foreach (uint material in submeshMaterials)
            {
                indices.Add(material); alpha |= IsAlphaTested(material);
                if (material >= (uint)materialList.Count) continue;
                var mm = materialList[(int)material];
                if (mm.Program == MaterialPrograms.Terrain) MaterialPrograms.TerrainDrawn(handle, material); // PT-104: the log names the mesh
                else if (noCoordinates) flat |= MaterialPrograms.FlatDrawn(handle, material, mm.Program, mm.NormalTexture);
                float e = Mathf.Max(mm.EmissiveR, Mathf.Max(mm.EmissiveG, mm.EmissiveB));
                if (e > brightest && (mm.Flags & Tgpt.MaterialCameraOnly) == 0) { brightest = e; glowing = material; }
            }
            if (flat) MaterialPrograms.FlatInstance();
            if (alpha) inst.Flags |= Tgpt.InstanceAlphaTest;
            if (glowing != uint.MaxValue && (flags & Tgpt.InstanceWater) == 0)
                AddGlow(indices, inst.MaterialBase, submeshMaterials.Length, handle, m, glowing, OwnSurfaceKey(handle, m.m03, m.m13, m.m23), glowsNext);
            instances.Add(inst);
        }
        // A glowing instance: its world bounds and color for SceneLights (Glows, through into); one a kept light stands for
        // (OwnSurfaces, by key: a still instance's by its place, a moving thing's by its identity) has its emissive materials
        // traced as that light's own surface.
        private static void AddGlow(List<uint> indices, uint first, int count, int handle, in Matrix4x4 m, uint glowing, long key, List<GlowThing> into)
        {
            if (!meshBounds.TryGetValue(handle, out var b)) return;
            var c = m.MultiplyPoint3x4(b.center); var x = b.extents;
            var mat = materialList[(int)glowing];
            float sum = Mathf.Max(mat.EmissiveR + mat.EmissiveG + mat.EmissiveB, 1e-6f);
            into.Add(new GlowThing
            {
                X = c.x, Y = c.y, Z = c.z,
                HalfX = Mathf.Abs(m.m00) * x.x + Mathf.Abs(m.m01) * x.y + Mathf.Abs(m.m02) * x.z,
                HalfY = Mathf.Abs(m.m10) * x.x + Mathf.Abs(m.m11) * x.y + Mathf.Abs(m.m12) * x.z,
                HalfZ = Mathf.Abs(m.m20) * x.x + Mathf.Abs(m.m21) * x.y + Mathf.Abs(m.m22) * x.z,
                R = mat.EmissiveR / sum, G = mat.EmissiveG / sum, B = mat.EmissiveB / sum,
                Kind = LightRules.GlowSurface, Key = key,
                Name = (meshNames.TryGetValue(handle, out var mesh) ? mesh : "#" + handle) + " (" + (materialNames.TryGetValue(glowing, out var name) ? name : "#" + glowing) + ")"
            });
            if (!OwnSurfaces.Contains(key)) return;
            for (int i = 0; i < count; i++)
            {
                uint material = indices[(int)first + i];
                if (material >= (uint)materialList.Count) continue;
                var mm = materialList[(int)material];
                if (mm.EmissiveR + mm.EmissiveG + mm.EmissiveB > 0f) indices[(int)first + i] = OwnSurfaceVariant(material);
            }
        }
        // A copy of an emissive material traced as a kept light's own surface (TGPT_MATERIAL_OWN_SURFACE), its emission following
        // the exposure as the original's does.
        private static uint OwnSurfaceVariant(uint material)
        {
            if (ownVariants.TryGetValue(material, out uint variant)) return variant;
            var mat = materialList[(int)material];
            mat.Flags |= Tgpt.MaterialOwnSurface;
            variant = (uint)materialList.Count;
            materialList.Add(mat);
            materialNames[variant] = (materialNames.TryGetValue(material, out var name) ? name : "#" + material) + ", a kept light's own surface";
            int weighted = exposureWeighted.FindIndex(w => w.index == (int)material);
            if (weighted >= 0) exposureWeighted.Add(((int)variant, exposureWeighted[weighted].emission, exposureWeighted[weighted].weight));
            ownVariants[material] = variant;
            return variant;
        }

        // ------------------------------------------------------------------ mesh renderers
        // No distance leaves an object out (PT-63, the owner of the 250 m of the Scene Radius row that is gone: "Why? Do that"):
        // what the game draws is traced, at the level it draws it, however far off. A statically batched renderer never moves:
        // listed here. Any other a script may move, enable or disable between two scans (PT-71 item 4; the owner: "What moves is
        // traced as it moves"): placed every frame where it is then (FollowRenderers), never listed.
        private static int CaptureRenderers(Vector3 camera, List<NativeInstance> instances, List<uint> indices)
        {
            // One LOD per group: the one Unity draws from this camera (WorldRules.GroupLevel), the others left out; none where
            // Unity culls the group (smaller on screen than its last level: the game draws nothing there, PT-61). A group of
            // one level is culled the same way. A renderer two levels share is traced when either is drawn. The groups are
            // kept: the levels of the renderers placed every frame are chosen again then.
            followedGroupsNext.Clear();
            foreach (var group in UnityEngine.Object.FindObjectsByType<LODGroup>(FindObjectsSortMode.None))
            {
                if (group == null) continue;
                var lods = group.GetLODs();
                if (lods.Length == 0) continue;
                var g = new FollowedGroup { Group = group, Levels = new int[lods.Length][] };
                for (int i = 0; i < lods.Length; i++)
                {
                    g.Heights.Add(lods[i].screenRelativeTransitionHeight);
                    var ids = new List<int>();
                    if (lods[i].renderers != null) foreach (var r in lods[i].renderers) if (r != null) ids.Add(r.GetInstanceID());
                    g.Levels[i] = ids.ToArray();
                }
                followedGroupsNext.Add(g);
            }
            var skip = new HashSet<int>();
            SkipUndrawnLevels(followedGroupsNext, camera, skip);
            var cam = Camera.main;
            int count = 0;
            // Statically batched renderers by their combined mesh and its place (StaticBatch below).
            var batches = new Dictionary<(int, Matrix4x4, uint), StaticBatch>();
            int cullingMask = cam != null ? cam.cullingMask : ~0;
            objectsLeftOut = 0; objectsStandIn = 0;
            followedNext.Clear();
            foreach (var renderer in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
            {
                if (renderer == null) continue;
                if (!renderer.isPartOfStaticBatch)
                {
                    var own = renderer.GetComponent<MeshFilter>();
                    if (own != null && own.sharedMesh != null)
                        followedNext.Add(new Followed { Renderer = renderer, Mesh = own.sharedMesh, Materials = renderer.sharedMaterials, Id = renderer.GetInstanceID() });
                    continue;
                }
                if (!renderer.enabled || !renderer.gameObject.activeInHierarchy || renderer.forceRenderingOff) continue;
                if (skip.Contains(renderer.GetInstanceID())) continue;
                // Statically batched objects the camera does not draw. Shadow-only ones (the castle's CM_*_Cluster_* meshes) are coarser
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
                    if (standIn != null && AddMesh(standIn.sharedMesh, renderer.sharedMaterials, renderer.localToWorldMatrix, Tgpt.InstanceStandIn, renderer.renderingLayerMask)) { ++count; ++objectsStandIn; }
                    continue;
                }
                var filter = renderer.GetComponent<MeshFilter>();
                var world = renderer.localToWorldMatrix;
                // A statically batched renderer (PT-63: "that sounds like a big problem, if things suddenly disappear when PT
                // is enabled. Fix that"): its mesh is the batch's combined mesh, in which it draws its own run of submeshes;
                // its matrix is the batch root's (the combined vertices are in that space). Gathered here, traced below, once
                // a combined mesh. Until 2026-10-07 all of them were left out.
                var combined = filter != null ? filter.sharedMesh : null;
                if (combined == null) continue;
                var key = (combined.GetInstanceID(), world, renderer.renderingLayerMask);
                if (!batches.TryGetValue(key, out var batch)) batches[key] = batch = new StaticBatch { Mesh = combined, World = world, Layers = renderer.renderingLayerMask };
                batch.Members.Add((renderer.subMeshStartIndex, renderer.sharedMaterials));
            }
            var keptFollowed = followed; followed = followedNext; followedNext = keptFollowed;
            var keptGroups = followedGroups; followedGroups = followedGroupsNext; followedGroupsNext = keptGroups;
            foreach (var batch in batches.Values)
            {
                int handle = MeshHandle(batch.Mesh, out int submeshes);
                if (handle <= 0) { batchedWaiting += batch.Members.Count; continue; } // traced from the scan after the combined mesh is read
                var list = new uint[submeshes]; // Invisible (0): a submesh no drawn member names
                bool visible = false; int members = 0;
                var memberMaterials = new List<uint>();
                foreach (var (start, shared) in batch.Members)
                {
                    memberMaterials.Clear();
                    if (shared != null) foreach (var m in shared) memberMaterials.Add(m != null ? MaterialIndex(m) : Neutral);
                    if (!WorldRules.PlaceBatchMember(list, start, memberMaterials)) continue;
                    ++members;
                    foreach (uint m in memberMaterials) visible |= m != Invisible;
                }
                if (!visible) continue;
                AddInstance(instances, indices, handle, batch.World, list, 0, batch.Layers);
                ++count; ++batchesTraced; batchedTraced += members;
            }
            if (batchedTraced + batchedWaiting > 0 && !loggedBatched)
            {
                loggedBatched = true;
                Session.Log.LogInfo("Statically batched renderers are traced in their combined meshes: " + (batchedTraced + batchedWaiting) + " renderers in " + batches.Count +
                    " combined meshes near the camera (until 2026-10-07 they were left out of the traced scene).");
            }
            return count;
        }
        private static bool loggedBatched;
        private sealed class StaticBatch { internal uint Layers; internal Mesh Mesh; internal Matrix4x4 World; internal readonly List<(int start, Material[] materials)> Members = new List<(int, Material[])>(); }
        // The levels the game does not draw of each LOD group from the camera at camera (WorldRules.GroupLevel): their renderers.
        private static void SkipUndrawnLevels(List<FollowedGroup> groups, Vector3 camera, HashSet<int> skip)
        {
            skip.Clear();
            foreach (var g in groups)
            {
                var group = g.Group;
                if (group == null || !group.enabled) continue;
                var t = group.transform;
                var ls = t.lossyScale;
                float size = group.size * Mathf.Max(Mathf.Abs(ls.x), Mathf.Max(Mathf.Abs(ls.y), Mathf.Abs(ls.z)));
                float distance = Vector3.Distance(t.TransformPoint(group.localReferencePoint), camera);
                int chosen = WorldRules.GroupLevel(g.Heights, size, distance, drawnFov, drawnLodBias, drawnMaximumLod);
                for (int i = 0; i < g.Levels.Length; i++) if (i != chosen) foreach (int id in g.Levels[i]) skip.Add(id);
                if (chosen >= 0 && chosen < g.Levels.Length) foreach (int id in g.Levels[chosen]) skip.Remove(id);
            }
        }

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
            strayQuery = em.CreateEntityQuery(new EntityQueryDesc
            {
                All = new[] { ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<LocalToWorld>(), ComponentType.ReadOnly<LODRange>() },
                None = new[] { ComponentType.ReadOnly<LODWorldReferencePoint>() }
            });
            entityQuery = em.CreateEntityQuery(new EntityQueryDesc
            {
                All = new[] { ComponentType.ReadOnly<MaterialMeshInfo>(), ComponentType.ReadOnly<LocalToWorld>() },
                None = new[] { ComponentType.ReadOnly<DrakeMeshMaterialComponent>(), ComponentType.ReadOnly<DisableRendering>() }
            });
        }
        private struct DrakeKey : IEquatable<DrakeKey>
        {
            internal ushort Mesh; internal int X, Y, Z, R; internal uint Layers;
            public bool Equals(DrakeKey o) => Mesh == o.Mesh && X == o.X && Y == o.Y && Z == o.Z && R == o.R && Layers == o.Layers;
            public override bool Equals(object o) => o is DrakeKey k && Equals(k);
            public override int GetHashCode() => (((Mesh * 397 ^ X) * 397 ^ Y) * 397 ^ Z) * 397 ^ R ^ (int)Layers;
        }
        // Entities: the entities of its mesh-material pairs (the scan lists each as its instance's, listedNext); First: the
        // lowest of them, its identity from frame to frame where the game moves it (Follow).
        private sealed class DrakeRenderer { internal uint Layers; internal ushort Mesh; internal bool StandIn; internal Matrix4x4 Matrix; internal readonly Dictionary<int, ushort> Materials = new Dictionary<int, ushort>();
                                             internal readonly List<Entity> Entities = new List<Entity>(); internal Entity First = Entity.Null; }
        private struct DrakeLod { internal uint Layers; internal DrakeMeshMaterialComponent Mm; internal LocalToWorld Ltw; internal Entity E; internal float Min; internal bool Drawn, Near, StandIn; }
        // Per scan, for the log: Drake entities the game has not loaded for drawing or has turned rendering off for.
        private static int drakeUnloaded;
        private static T Loaded<T>(AsyncOperationHandle<T> handle, ushort counter) where T : UnityEngine.Object =>
            counter > 0 && handle.IsValid() && handle.IsDone && handle.Status == AsyncOperationStatus.Succeeded ? handle.Result : null;
        // follow: this frame's things the game moves (Follow; FollowedChunk names their chunks), each placed where it is now;
        // else the scan's list, every other chunk.
        private static int CaptureDrake(Vector3 camera, List<NativeInstance> instances, List<uint> indices, bool follow)
        {
            var world = World.DefaultGameObjectInjectionWorld;
            var manager = DrakeRendererManager.Instance;
            if (world == null || !world.IsCreated || manager == null) return 0;
            var loading = manager.LoadingManager;
            var em = world.EntityManager;
            EnsureQueries(em, world);
            var cam = Camera.main;
            float lodScale = DrawnScale;
            int cullingMask = cam != null ? cam.cullingMask : ~0;
            float3 c = camera;
            var renderers = new Dictionary<DrakeKey, DrakeRenderer>();
            // Entities the camera does not draw: shadow-only ones traced as stand-ins, those on layers the camera leaves
            // out left out (see CaptureRenderers); counted for the log.
            var filterHandle = em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RenderFilterSettings>();
            var mmHandle = em.GetComponentTypeHandle<DrakeMeshMaterialComponent>(true);
            var ltwHandle = em.GetComponentTypeHandle<LocalToWorld>(true);
            var rangeHandle = em.GetComponentTypeHandle<LODRange>(true);
            var referenceHandle = em.GetComponentTypeHandle<LODWorldReferencePoint>(true);
            var loadedHandle = em.GetComponentTypeHandle<MaterialMeshInfo>(true);
            var disabledHandle = em.GetComponentTypeHandle<DisableRendering>(true);
            var staticHandle = em.GetComponentTypeHandle<Static>(true);
            var entityHandle = em.GetEntityTypeHandle();
            bool standIn = false; uint renderingLayers = uint.MaxValue;
            // The chunks this pass takes: the scan every chunk but those placed every frame; Follow those, and a listed chunk
            // whose entities it finds moved since the scan (MovedSinceListed), from that frame on.
            bool Take(in ArchetypeChunk chunk)
            {
                bool foundMoving = movingChunks.Contains(chunk.SequenceNumber);
                bool placedEveryFrame = MovingRules.PlacedEveryFrame(true, chunk.Has(ref staticHandle), foundMoving);
                if (!follow) { if (foundMoving) movingChunksSeen.Add(chunk.SequenceNumber); return !placedEveryFrame; }
                if (!placedEveryFrame && !MovedSinceListed(chunk, ref ltwHandle, entityHandle)) return false;
                StandInFor(chunk, entityHandle);
                return true;
            }
            // Whether the game draws a chunk's entities at all: Drake gives an entity its MaterialMeshInfo when it loads it for
            // drawing and takes it away when it unloads it (DrakeStateJob: by the entity's visible range against the scaled
            // distance to its LOD reference point, the far end times the Draw Distance row's extra distance), and Entities
            // Graphics draws nothing with DisableRendering. The game's own state is read instead of working its rule out
            // again: until 2026-10-07 the visible range was compared with the plain distance to the entity's own position,
            // so entities were traced up to 15 % farther than drawn, and not as far as drawn with extra draw distance.
            bool LoadedForDrawing(in ArchetypeChunk chunk) => chunk.Has(ref loadedHandle) && !chunk.Has(ref disabledHandle);
            bool Drawn(in ArchetypeChunk chunk)
            {
                standIn = false; renderingLayers = uint.MaxValue;
                if (!chunk.Has(filterHandle)) return true;
                var settings = chunk.GetSharedComponent(filterHandle, em); renderingLayers = settings.RenderingLayerMask;
                if ((cullingMask & (1 << settings.Layer)) == 0) { if (!follow) drakeHidden += chunk.Count; return false; }
                if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { if (!follow) drakeShadowOnly += chunk.Count; if (!standIns) return false; standIn = true; }
                return true;
            }
            void Collect(in DrakeMeshMaterialComponent mm, in LocalToWorld ltw, Entity entity)
            {
                float4x4 v = ltw.Value;
                var key = new DrakeKey { Layers = renderingLayers, Mesh = mm.meshIndex, X = (int)math.round(v.c3.x * 100f), Y = (int)math.round(v.c3.y * 100f), Z = (int)math.round(v.c3.z * 100f),
                    R = (int)math.round((v.c0.x + v.c1.y * 3f + v.c2.z * 7f + v.c0.y * 11f + v.c2.x * 13f) * 1000f) };
                if (!renderers.TryGetValue(key, out var r))
                {
                    r = new DrakeRenderer { Mesh = mm.meshIndex, StandIn = standIn, Layers = renderingLayers };
                    r.Matrix.SetColumn(0, new Vector4(v.c0.x, v.c0.y, v.c0.z, v.c0.w)); r.Matrix.SetColumn(1, new Vector4(v.c1.x, v.c1.y, v.c1.z, v.c1.w));
                    r.Matrix.SetColumn(2, new Vector4(v.c2.x, v.c2.y, v.c2.z, v.c2.w)); r.Matrix.SetColumn(3, new Vector4(v.c3.x, v.c3.y, v.c3.z, v.c3.w));
                    renderers[key] = r;
                }
                r.Materials[mm.submesh] = mm.materialIndex;
                r.Entities.Add(entity);
                if (r.First == Entity.Null || entity.Index < r.First.Index) r.First = entity;
            }
            if (!follow) { drakeShadowOnly = 0; drakeHidden = 0; drakeUnloaded = 0; }
            var meshData = loading.MeshLoadingData; var materialData = loading.MaterialLoadingData;
            // Which LOD of each group is traced (WorldRules.TracedLevels): the drawn level, and the levels about to be, by group.
            var groups = new Dictionary<int3, List<DrakeLod>>();
            using (var chunks = lodQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (!Take(chunk) || !Drawn(chunk)) continue;
                    // A level the game has not loaded is not drawn; it may still stand in while the drawn one registers.
                    bool loaded = LoadedForDrawing(chunk);
                    if (!loaded && !follow) drakeUnloaded += chunk.Count;
                    var mm = chunk.GetNativeArray(ref mmHandle); var ltw = chunk.GetNativeArray(ref ltwHandle); var ents = chunk.GetNativeArray(entityHandle);
                    var range = chunk.GetNativeArray(ref rangeHandle); var reference = chunk.GetNativeArray(ref referenceHandle);
                    for (int i = 0; i < chunk.Count; i++)
                    {
                        float3 at = reference[i].Value;
                        float d = math.distance(at, c) * lodScale;
                        bool drawn = loaded && WorldRules.EntityLevelDrawn(range[i].LODMask, range[i].MinDist, range[i].MaxDist, d, drawnMaximumLod);
                        bool near = !drawn && WorldRules.EntityLevelNear(range[i].MinDist, range[i].MaxDist, d);
                        if (!drawn && !near) continue;
                        var key = new int3((int)math.round(at.x * 100f), (int)math.round(at.y * 100f), (int)math.round(at.z * 100f));
                        if (!groups.TryGetValue(key, out var list)) groups[key] = list = new List<DrakeLod>();
                        list.Add(new DrakeLod { Mm = mm[i], Ltw = ltw[i], E = ents[i], Min = range[i].MinDist, Near = near, Drawn = drawn, StandIn = standIn, Layers = renderingLayers });
                    }
                }
            }
            // Entities with a LOD range and no reference point: Entities Graphics chooses levels only in chunks that have
            // both, so it draws these always (within their visible range, where they have one). Until 2026-10-07 they matched
            // none of the queries here and stayed out of the traced scene.
            using (var chunks = strayQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (!Take(chunk) || !Drawn(chunk)) continue;
                    if (!LoadedForDrawing(chunk)) { if (!follow) drakeUnloaded += chunk.Count; continue; }
                    var mm = chunk.GetNativeArray(ref mmHandle); var ltw = chunk.GetNativeArray(ref ltwHandle); var ents = chunk.GetNativeArray(entityHandle);
                    for (int i = 0; i < chunk.Count; i++) { Collect(mm[i], ltw[i], ents[i]); if (!follow) ++drakeStray; }
                }
            }
            bool DrakeReady(ushort meshIndex)
            {
                if (meshIndex >= meshData.Count) return false;
                var md = meshData[meshIndex];
                var mesh = Loaded(md.loadingHandle, md.counter);
                return mesh != null && MeshHandle(mesh, out _) > 0;
            }
            var levels = new List<float>(); var drawnLevels = new List<bool>(); var nearLevels = new List<bool>();
            foreach (var list in groups.Values)
            {
                levels.Clear(); drawnLevels.Clear(); nearLevels.Clear();
                foreach (var e in list) { levels.Add(e.Min); drawnLevels.Add(e.Drawn); nearLevels.Add(e.Near); }
                int standing = WorldRules.TracedLevels(levels, drawnLevels, nearLevels, l => { bool all = true; foreach (var e in list) if (e.Min == l) all &= DrakeReady(e.Mm.meshIndex); return all; }, tracedLevels);
                if (!follow) lodStandIns += standing;
                foreach (float level in tracedLevels)
                    foreach (var e in list) if (e.Min == level) { standIn = e.StandIn; renderingLayers = e.Layers; Collect(e.Mm, e.Ltw, e.E); }
            }
            // Entities with a visible range and no LOD range: traced while the game has them loaded for drawing (see LoadedForDrawing).
            using (var chunks = rangeQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (!Take(chunk) || !Drawn(chunk)) continue;
                    if (!LoadedForDrawing(chunk)) { if (!follow) drakeUnloaded += chunk.Count; continue; }
                    var mm = chunk.GetNativeArray(ref mmHandle); var ltw = chunk.GetNativeArray(ref ltwHandle); var ents = chunk.GetNativeArray(entityHandle);
                    for (int i = 0; i < chunk.Count; i++) Collect(mm[i], ltw[i], ents[i]);
                }
            }
            using (var chunks = plainQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    if (!Take(chunk) || !Drawn(chunk)) continue;
                    if (!LoadedForDrawing(chunk)) { if (!follow) drakeUnloaded += chunk.Count; continue; }
                    var mm = chunk.GetNativeArray(ref mmHandle); var ltw = chunk.GetNativeArray(ref ltwHandle); var ents = chunk.GetNativeArray(entityHandle);
                    for (int i = 0; i < chunk.Count; i++) Collect(mm[i], ltw[i], ents[i]);
                }
            }
            int count = 0;
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
                if (follow) Place(handle, r.Matrix, list, r.StandIn ? Tgpt.InstanceStandIn : 0u, EntityIdentity(r.First), r.Layers);
                else
                {
                    AddInstance(instances, indices, handle, r.Matrix, list, r.StandIn ? Tgpt.InstanceStandIn : 0u, r.Layers);
                    int at = instances.Count - 1;
                    foreach (var e in r.Entities) listedNext[e] = at;
                }
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
            internal int Mesh, X, Y, Z, R; internal uint Layers;
            public bool Equals(EntityKey o) => Mesh == o.Mesh && X == o.X && Y == o.Y && Z == o.Z && R == o.R && Layers == o.Layers;
            public override bool Equals(object o) => o is EntityKey k && Equals(k);
            public override int GetHashCode() => (((Mesh * 397 ^ X) * 397 ^ Y) * 397 ^ Z) * 397 ^ R ^ (int)Layers;
        }
        private sealed class EntityRenderer { internal uint Layers; internal Mesh Mesh; internal Matrix4x4 Matrix; internal readonly Dictionary<int, Material> Materials = new Dictionary<int, Material>();
                                              internal readonly List<Entity> Entities = new List<Entity>(); internal Entity First = Entity.Null; }
        private unsafe struct EntityLod { internal uint Layers; internal MaterialMeshInfo Mmi; internal float4x4 V; internal Entity E; internal bool Baked; internal RenderMeshArray Array; internal MaterialMeshInfo* Pairs; internal float Min; internal bool Drawn, Near; }
        private static int entityShadowOnly, entityHidden, entityUnresolved;
        private static bool entitiesLogged;
        // Whether the drawn geometry near the camera is in the traced scene comes back in covered: Entities Graphics could
        // be read, and at most a tenth of its entities there are unresolved or waiting for their meshes to register
        // (shadow-only meshes stand in for them until then). Its own shadow-only entities are always left out.
        private static int entityPending;
        // follow: as CaptureDrake's. Nothing here is placed every frame until it is found moving (MovedSinceListed): these are
        // the castle's baked entities, which the game has no system to move.
        private static unsafe int CaptureEntities(Vector3 camera, List<NativeInstance> instances, List<uint> indices, out bool covered, bool follow)
        {
            covered = false;
            if (!follow) { entityPending = 0; entityShadowOnly = 0; entityHidden = 0; entityUnresolved = 0; }
            var world = World.DefaultGameObjectInjectionWorld;
            if (world == null || !world.IsCreated) return 0;
            var graphics = world.GetExistingSystemManaged<EntitiesGraphicsSystem>();
            if (graphics == null) return 0;
            var em = world.EntityManager;
            EnsureQueries(em, world);
            var cam = Camera.main;
            float lodScale = DrawnScale;
            int cullingMask = cam != null ? cam.cullingMask : ~0;
            float3 c = camera;
            var filterHandle = em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RenderFilterSettings>();
            var arrayHandle = em.GetSharedComponentTypeHandle<RenderMeshArray>();
            var setHandle = em.GetSharedComponentTypeHandle<Unity.Entities.Graphics.RendererRange>();
            var infoHandle = em.GetComponentTypeHandle<MaterialMeshInfo>(true);
            var ltwHandle = em.GetComponentTypeHandle<LocalToWorld>(true);
            var rangeHandle = em.GetComponentTypeHandle<LODRange>(true);
            var referenceHandle = em.GetComponentTypeHandle<LODWorldReferencePoint>(true);
            var entityHandle = em.GetEntityTypeHandle();
            var renderers = new Dictionary<EntityKey, EntityRenderer>();
            var meshCache = new Dictionary<uint, Mesh>();
            var materialCache = new Dictionary<uint, Material>();
            bool baked = false; uint renderingLayers = uint.MaxValue;
            RenderMeshArray array = default;
            // One mesh-material pair of an entity at v: its mesh and material, from Entities Graphics' registry (runtime
            // IDs) or the chunk's RenderMeshArray.
            bool Add(MaterialMeshInfo mmi, float4x4 v, Entity entity)
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
                var key = new EntityKey { Layers = renderingLayers, Mesh = mesh.GetInstanceID(), X = (int)math.round(v.c3.x * 100f), Y = (int)math.round(v.c3.y * 100f), Z = (int)math.round(v.c3.z * 100f),
                    R = (int)math.round((v.c0.x + v.c1.y * 3f + v.c2.z * 7f + v.c0.y * 11f + v.c2.x * 13f) * 1000f) };
                if (!renderers.TryGetValue(key, out var r))
                {
                    r = new EntityRenderer { Mesh = mesh, Layers = renderingLayers };
                    r.Matrix.SetColumn(0, new Vector4(v.c0.x, v.c0.y, v.c0.z, v.c0.w)); r.Matrix.SetColumn(1, new Vector4(v.c1.x, v.c1.y, v.c1.z, v.c1.w));
                    r.Matrix.SetColumn(2, new Vector4(v.c2.x, v.c2.y, v.c2.z, v.c2.w)); r.Matrix.SetColumn(3, new Vector4(v.c3.x, v.c3.y, v.c3.z, v.c3.w));
                    renderers[key] = r;
                }
                r.Materials[Mathf.Max(0, (int)mmi.SubMesh)] = material;
                r.Entities.Add(entity);
                if (r.First == Entity.Null || entity.Index < r.First.Index) r.First = entity;
                return true;
            }
            // The mesh one mesh-material pair of an entity draws (as Add resolves it), for whether a LOD's meshes are registered.
            Mesh PairMesh(MaterialMeshInfo mmi, bool isBaked, RenderMeshArray meshes)
            {
                if (mmi.IsRuntimeMesh)
                {
                    uint id = mmi.MeshID.value;
                    if (id == 0) return null;
                    if (!meshCache.TryGetValue(id, out var mesh)) meshCache[id] = mesh = graphics.GetMesh(mmi.MeshID);
                    return mesh;
                }
                return isBaked ? meshes.GetMesh(mmi) : null;
            }
            void AddEntity(MaterialMeshInfo mmi, float4x4 v, MaterialMeshInfo* pairs, Entity entity)
            {
                bool added = false;
                if (!mmi.HasMaterialMeshIndexRange) added = Add(mmi, v, entity);
                else if (pairs != null)
                {
                    var span = mmi.MaterialMeshIndexRange;
                    for (int j = span.start; j < span.end; j++) added |= Add(pairs[j], v, entity);
                }
                if (!added && !follow) ++entityUnresolved;
            }
            // Which LOD of each group is traced (WorldRules.TracedLevels): the drawn level.
            var groups = new Dictionary<int3, List<EntityLod>>();
            using (var chunks = entityQuery.ToArchetypeChunkArray(Allocator.Temp))
            {
                foreach (var chunk in chunks)
                {
                    renderingLayers = uint.MaxValue;
                    bool placedEveryFrame = MovingRules.PlacedEveryFrame(false, false, movingChunks.Contains(chunk.SequenceNumber));
                    if (!follow && placedEveryFrame) { movingChunksSeen.Add(chunk.SequenceNumber); continue; }
                    if (follow)
                    {
                        if (!placedEveryFrame && !MovedSinceListed(chunk, ref ltwHandle, entityHandle)) continue;
                        StandInFor(chunk, entityHandle);
                    }
                    if (chunk.Has(filterHandle))
                    {
                        var settings = chunk.GetSharedComponent(filterHandle, em); renderingLayers = settings.RenderingLayerMask;
                        if ((cullingMask & (1 << settings.Layer)) == 0) { if (!follow) entityHidden += chunk.Count; continue; }
                        if (settings.ShadowCastingMode == ShadowCastingMode.ShadowsOnly) { if (!follow) entityShadowOnly += chunk.Count; continue; }
                    }
                    baked = chunk.Has(arrayHandle);
                    array = baked ? chunk.GetSharedComponentManaged(arrayHandle, em) : default;
                    MaterialMeshInfo* pairs = chunk.Has(setHandle) ? chunk.GetSharedComponent(setHandle, em).MaterialMeshInfos : null;
                    var info = chunk.GetNativeArray(ref infoHandle); var ltw = chunk.GetNativeArray(ref ltwHandle); var ents = chunk.GetNativeArray(entityHandle);
                    bool lod = chunk.Has(ref rangeHandle) && chunk.Has(ref referenceHandle);
                    var range = lod ? chunk.GetNativeArray(ref rangeHandle) : default;
                    var reference = lod ? chunk.GetNativeArray(ref referenceHandle) : default;
                    for (int i = 0; i < chunk.Count; i++)
                    {
                        float4x4 v = ltw[i].Value;
                        if (lod)
                        {
                            float3 at = reference[i].Value;
                            float d = math.distance(at, c) * lodScale;
                            bool drawn = WorldRules.EntityLevelDrawn(range[i].LODMask, range[i].MinDist, range[i].MaxDist, d, drawnMaximumLod);
                            bool near = !drawn && WorldRules.EntityLevelNear(range[i].MinDist, range[i].MaxDist, d);
                            if (!drawn && !near) continue;
                            var key = new int3((int)math.round(at.x * 100f), (int)math.round(at.y * 100f), (int)math.round(at.z * 100f));
                            if (!groups.TryGetValue(key, out var list)) groups[key] = list = new List<EntityLod>();
                            list.Add(new EntityLod { Mmi = info[i], V = v, E = ents[i], Baked = baked, Array = array, Pairs = pairs, Min = range[i].MinDist, Drawn = drawn, Near = near, Layers = renderingLayers });
                            continue;
                        }
                        AddEntity(info[i], v, pairs, ents[i]);
                    }
                }
            }
            bool EntityReady(in EntityLod e)
            {
                if (!e.Mmi.HasMaterialMeshIndexRange) { var mesh = PairMesh(e.Mmi, e.Baked, e.Array); return mesh != null && MeshHandle(mesh, out _) > 0; }
                if (e.Pairs == null) return false;
                var span = e.Mmi.MaterialMeshIndexRange;
                for (int j = span.start; j < span.end; j++) { var mesh = PairMesh(e.Pairs[j], e.Baked, e.Array); if (mesh == null || MeshHandle(mesh, out _) <= 0) return false; }
                return true;
            }
            var levels = new List<float>(); var drawnLevels = new List<bool>(); var nearLevels = new List<bool>();
            foreach (var list in groups.Values)
            {
                levels.Clear(); drawnLevels.Clear(); nearLevels.Clear();
                foreach (var e in list) { levels.Add(e.Min); drawnLevels.Add(e.Drawn); nearLevels.Add(e.Near); }
                int standing = WorldRules.TracedLevels(levels, drawnLevels, nearLevels, l => { bool all = true; foreach (var e in list) if (e.Min == l) all &= EntityReady(e); return all; }, tracedLevels);
                if (!follow) lodStandIns += standing;
                foreach (float level in tracedLevels)
                    foreach (var e in list) if (e.Min == level) { baked = e.Baked; array = e.Array; renderingLayers = e.Layers; AddEntity(e.Mmi, e.V, e.Pairs, e.E); }
            }
            int count = 0;
            foreach (var r in renderers.Values)
            {
                int handle = MeshHandle(r.Mesh, out int submeshes);
                if (handle <= 0) { if (!follow && !meshes.ContainsKey(r.Mesh.GetInstanceID())) ++entityPending; continue; }
                var list = new uint[submeshes];
                bool any = false;
                for (int s = 0; s < submeshes; s++)
                {
                    uint material = Invisible; // a submesh no entity draws (a transparent one drawn otherwise)
                    if (r.Materials.TryGetValue(s, out var m)) material = m != null ? MaterialIndex(m) : Neutral;
                    list[s] = material; any |= material != Invisible;
                }
                if (!any) continue;
                if (follow) Place(handle, r.Matrix, list, 0u, EntityIdentity(r.First), r.Layers);
                else
                {
                    AddInstance(instances, indices, handle, r.Matrix, list, 0u, r.Layers);
                    int at = instances.Count - 1;
                    foreach (var e in r.Entities) listedNext[e] = at;
                }
                ++count;
            }
            if (follow) return count;
            covered = (entityUnresolved + entityPending) * 10 <= renderers.Count + entityUnresolved;
            if (count > 0 && !entitiesLogged)
            {
                entitiesLogged = true;
                Session.Log.LogInfo("Entities Graphics: " + count + " instances drawn without Drake traced near the camera (" + renderers.Count + " found, " +
                    meshCache.Count + " meshes from its registry)");
            }
            return count;
        }

        // ------------------------------------------------------------------ what the game moves (PT-71 item 4)
        // Since the tracer's own camera rays draw every pixel (PT-59), the traced scene is the picture: a thing the game moves
        // traced where the scan left it (every 0.5 to 2 s) stood there on screen. So what the game moves is placed every frame,
        // as HDRP draws it then (Follow, right before the frame is handed over), with where it was the frame before for its own
        // motion (TGPT_SetMoving): every GameObject mesh renderer that is not statically batched; Drake's movable entities (no
        // Static tag: LinkedTransformSystem copies their GameObject's matrix every frame: the weapon in a hand, placed items,
        // doors); and any entity the scan listed that is found to have moved since (MovedSinceListed: the game turns windmills,
        // flies its birds and lifts its elevator's chains on entities it tags Static), from that frame on, its listed copy not
        // traced. What appears, disappears or changes its level between two scans does so the same frame.
        private sealed class Followed { internal MeshRenderer Renderer; internal Mesh Mesh; internal Material[] Materials; internal int Id; }
        // A LOD group as the scan found it: its levels' heights and their renderers (by instance), for the level drawn this frame.
        private sealed class FollowedGroup { internal LODGroup Group; internal readonly List<float> Heights = new List<float>(); internal int[][] Levels; }
        private static List<Followed> followed = new List<Followed>(), followedNext = new List<Followed>();
        private static List<FollowedGroup> followedGroups = new List<FollowedGroup>(), followedGroupsNext = new List<FollowedGroup>();
        private static readonly HashSet<int> followSkip = new HashSet<int>();
        // The listed entities by the scan list's index of their instance (this list's, and the next scan's while it is made).
        private static Dictionary<Entity, int> listed = new Dictionary<Entity, int>(), listedNext = new Dictionary<Entity, int>();
        // Chunks the scan listed whose entities were found moving (by ArchetypeChunk.SequenceNumber): placed every frame from then
        // on and left out of the scans while they live (the scan keeps those it still meets, movingChunksSeen).
        private static readonly HashSet<ulong> movingChunks = new HashSet<ulong>(), movingChunksSeen = new HashSet<ulong>();
        // The ECS's version at the last frame placed: a chunk whose LocalToWorld no system wrote since is not compared.
        private static uint followVersion;
        // This frame's things, their materials, the listed instances they stand in for, and their glows; where each thing (by
        // its identity) was the frame before, and is now.
        private static readonly List<NativeMoving> moving = new List<NativeMoving>();
        private static readonly List<uint> movingIndices = new List<uint>();
        private static readonly HashSet<int> standingIn = new HashSet<int>();
        private static List<GlowThing> movingGlows = new List<GlowThing>(), movingGlowsNext = new List<GlowThing>();
        private static readonly MovingRules.Previous<Matrix4x4> previous = new MovingRules.Previous<Matrix4x4>();
        // For the log (FollowSummary): the last frame's counts, its cost and the most over the last report, and the listed chunks
        // found moving so far.
        private static int followPlaced, followChanged, followStoodIn, followRenderers, followLeftOut, followFoundMoving, followFailedSends;
        private static double followMs, followMostMs;
        private static string followProblem = "";
        private static int followFrame = -1;
        internal static string FollowSummary =>
            "; moving: " + followPlaced + " things placed this frame where HDRP draws them (" + followChanged + " moved since the frame before; " +
            followRenderers + " GameObject renderers, the rest entities; " + movingChunks.Count + " listed chunks found moving, " + followFoundMoving + " so far), " +
            followStoodIn + " of the scan's instances stood in for, " + followLeftOut + " renderers the camera does not draw; the oldest traced transform of a " +
            "moving thing 0 frames old; " + followMs.ToString("0.00") + " ms this frame, at most " + followMostMs.ToString("0.00") + " ms" +
            (followFailedSends > 0 ? "; refused " + followFailedSends + " times" : "") + (followProblem.Length > 0 ? "; last problem: " + followProblem : "");

        private static void ResetFollow()
        {
            followed.Clear(); followedNext.Clear(); followedGroups.Clear(); followedGroupsNext.Clear(); followSkip.Clear();
            listed.Clear(); listedNext.Clear(); movingChunks.Clear(); movingChunksSeen.Clear(); followVersion = 0;
            moving.Clear(); movingIndices.Clear(); standingIn.Clear(); movingGlows.Clear(); movingGlowsNext.Clear(); previous.Clear();
            followMostMs = 0; followProblem = ""; followFrame = -1;
        }
        private static long EntityIdentity(Entity e) => MovingRules.EntityIdentity(e.Index, e.Version);
        private static bool SameMatrix(in Matrix4x4 a, in Matrix4x4 b) =>
            MovingRules.SameTransform(a.m00, a.m01, a.m02, a.m03, a.m10, a.m11, a.m12, a.m13, a.m20, a.m21, a.m22, a.m23,
                                      b.m00, b.m01, b.m02, b.m03, b.m10, b.m11, b.m12, b.m13, b.m20, b.m21, b.m22, b.m23);
        // A moving thing's glow is kept as a light's own surface by its identity, wherever it goes (OwnSurfaceKey keys a still one
        // by its place).
        internal static long MoverSurfaceKey(int handle, long identity) => ((long)handle << 36) ^ unchecked(identity * (long)0x9E3779B97F4A7C15UL) ^ 0x5BD1E995L;

        // Every frame, right before it is handed over (HdrpPasses): what the game moves, where HDRP draws it now.
        internal static void Follow(NativeTracer tracer, Vector3 camera)
        {
            if (tracer == null || !tracer.Connected) return;
            if (followFrame == Time.frameCount) return; // once a frame
            followFrame = Time.frameCount;
            var watch = Stopwatch.StartNew();
            moving.Clear(); movingIndices.Clear(); standingIn.Clear(); movingGlowsNext.Clear();
            followChanged = 0; followRenderers = 0; followLeftOut = 0;
            var settings = Session.Settings;
            try
            {
                if (settings.SceneObjects.Value) FollowRenderers(camera);
                if (settings.DrakeWorld.Value)
                {
                    var world = World.DefaultGameObjectInjectionWorld;
                    if (world != null && world.IsCreated)
                    {
                        var em = world.EntityManager;
                        // The systems that write LocalToWorld (Drake's linked transforms, its rotations, the flocks) run in jobs.
                        em.CompleteDependencyBeforeRO<LocalToWorld>();
                        uint version = em.GlobalSystemVersion;
                        CaptureDrake(camera, null, null, true);
                        CaptureEntities(camera, null, null, out _, true);
                        followVersion = version;
                    }
                }
                if (materialList.Count > materialsUploaded)
                {
                    var added = materialList.GetRange(materialsUploaded, materialList.Count - materialsUploaded).ToArray();
                    if (tracer.SetMaterials((uint)materialsUploaded, added, (uint)added.Length) == 0) materialsUploaded = materialList.Count;
                }
            }
            catch (Exception ex)
            {
                // Nothing placed rather than last frame's things where they were.
                followProblem = ex.GetType().Name + " " + ex.Message;
                moving.Clear(); movingIndices.Clear(); standingIn.Clear();
            }
            // A thing whose materials are not uploaded yet waits a frame (its indices would name none).
            if (materialsUploaded < materialList.Count) { moving.Clear(); movingIndices.Clear(); }
            var replaced = new uint[standingIn.Count];
            int k = 0;
            foreach (int at in standingIn) replaced[k++] = (uint)at;
            if (tracer.SetMoving(moving.ToArray(), (uint)moving.Count, movingIndices.ToArray(), (uint)movingIndices.Count, replaced, (uint)replaced.Length) != 0) ++followFailedSends;
            previous.EndFrame();
            var g = movingGlows; movingGlows = movingGlowsNext; movingGlowsNext = g;
            followPlaced = moving.Count; followStoodIn = replaced.Length;
            watch.Stop();
            followMs = watch.Elapsed.TotalMilliseconds;
            followMostMs = Math.Max(followMostMs, followMs);
        }
        // The report shows the most a frame took since the last one.
        internal static void FollowReported() => followMostMs = 0;

        // GameObject mesh renderers: each one the scan found, as it is now (enabled, active, on a layer the camera draws, its
        // drawn level of detail, its matrix).
        private static void FollowRenderers(Vector3 camera)
        {
            var cam = Camera.main;
            int cullingMask = cam != null ? cam.cullingMask : ~0;
            SkipUndrawnLevels(followedGroups, camera, followSkip);
            foreach (var f in followed)
            {
                var r = f.Renderer;
                if (r == null || !r.enabled || r.forceRenderingOff || f.Mesh == null) continue;
                var go = r.gameObject;
                if (!go.activeInHierarchy || followSkip.Contains(f.Id)) continue;
                // Objects the camera does not draw: on layers it leaves out (left out), or shadow-only (left out, but as
                // stand-ins while Entities Graphics cannot be read: see CaptureRenderers). Named in the log, once each.
                bool shadowOnly = r.shadowCastingMode == ShadowCastingMode.ShadowsOnly, hidden = (cullingMask & (1 << go.layer)) == 0;
                if ((shadowOnly || hidden) && undrawnSeen.Add(r.name) && undrawnSeen.Count <= 60)
                    Session.Log.LogInfo((hidden || !standIns ? "Left out" : "Traced as a stand-in") + " though the camera does not draw it: \"" + r.name + "\" (" +
                        (hidden ? "layer " + LayerMask.LayerToName(go.layer) : "shadow-only") + (r.transform.parent != null ? ", under \"" + r.transform.parent.name + "\"" : "") + ")");
                if (hidden || (shadowOnly && !standIns)) { ++followLeftOut; continue; }
                if (PlaceMesh(f.Mesh, f.Materials, r.localToWorldMatrix, shadowOnly ? Tgpt.InstanceStandIn : 0u, MovingRules.RendererIdentity(f.Id), r.renderingLayerMask)) ++followRenderers;
            }
        }

        // A thing the game moves, where it is this frame: a TGPT_Moving with the transform its identity had the frame before
        // (none: this one, no motion of its own), its submeshes' materials in the frame's own list, its glow as a still
        // instance's (kept as a light's own surface by its identity).
        private static void Place(int handle, in Matrix4x4 m, uint[] submeshMaterials, uint flags, long identity, uint renderingLayers = 1u)
        {
            var p = previous.Was(identity, m);
            if (!SameMatrix(p, m)) ++followChanged;
            var t = new NativeMoving
            {
                M00 = m.m00, M01 = m.m01, M02 = m.m02, M03 = m.m03, M10 = m.m10, M11 = m.m11, M12 = m.m12, M13 = m.m13, M20 = m.m20, M21 = m.m21, M22 = m.m22, M23 = m.m23,
                Mesh = handle, MaterialBase = (uint)movingIndices.Count, Mask = 0xFF, Flags = flags, RenderingLayerMask = renderingLayers,
                P00 = p.m00, P01 = p.m01, P02 = p.m02, P03 = p.m03, P10 = p.m10, P11 = p.m11, P12 = p.m12, P13 = p.m13, P20 = p.m20, P21 = p.m21, P22 = p.m22, P23 = p.m23
            };
            bool alpha = false;
            uint glowing = uint.MaxValue; float brightest = 0f;
            foreach (uint material in submeshMaterials)
            {
                movingIndices.Add(material); alpha |= IsAlphaTested(material);
                if (material >= (uint)materialList.Count) continue;
                var mm = materialList[(int)material];
                float e = Mathf.Max(mm.EmissiveR, Mathf.Max(mm.EmissiveG, mm.EmissiveB));
                if (e > brightest && (mm.Flags & Tgpt.MaterialCameraOnly) == 0) { brightest = e; glowing = material; }
            }
            if (alpha) t.Flags |= Tgpt.InstanceAlphaTest;
            if (glowing != uint.MaxValue) AddGlow(movingIndices, t.MaterialBase, submeshMaterials.Length, handle, m, glowing, MoverSurfaceKey(handle, identity), movingGlowsNext);
            moving.Add(t);
        }

        // A chunk the scan listed whose LocalToWorld some system wrote since the last frame: its listed entities compared value by
        // value with the scan's list; one that moved makes the chunk placed every frame from now on (found the frame it moves).
        private static bool MovedSinceListed(in ArchetypeChunk chunk, ref ComponentTypeHandle<LocalToWorld> ltwHandle, EntityTypeHandle entityHandle)
        {
            if (lastInstances == null || listed.Count == 0 || !chunk.DidChange(ref ltwHandle, followVersion)) return false;
            var ents = chunk.GetNativeArray(entityHandle); var ltw = chunk.GetNativeArray(ref ltwHandle);
            for (int i = 0; i < chunk.Count; i++)
            {
                if (!listed.TryGetValue(ents[i], out int at) || at >= lastInstances.Length) continue;
                var v = ltw[i].Value;
                var inst = lastInstances[at];
                if (MovingRules.SameTransform(inst.M00, inst.M01, inst.M02, inst.M03, inst.M10, inst.M11, inst.M12, inst.M13, inst.M20, inst.M21, inst.M22, inst.M23,
                                              v.c0.x, v.c1.x, v.c2.x, v.c3.x, v.c0.y, v.c1.y, v.c2.y, v.c3.y, v.c0.z, v.c1.z, v.c2.z, v.c3.z)) continue;
                movingChunks.Add(chunk.SequenceNumber); ++followFoundMoving;
                if (followFoundMoving <= 20)
                    Session.Log.LogInfo("Moving: entities the scan listed as still move (" + chunk.Count + " in a chunk, the first at " + v.c3.x.ToString("0.0") + ", " +
                        v.c3.y.ToString("0.0") + ", " + v.c3.z.ToString("0.0") + "): placed every frame from now on");
                return true;
            }
            return false;
        }
        // The listed instances a chunk placed this frame stands in for: not traced (their entities are placed where they are now).
        private static void StandInFor(in ArchetypeChunk chunk, EntityTypeHandle entityHandle)
        {
            if (listed.Count == 0) return;
            foreach (int at in MovingRules.StandingIn(chunk.GetNativeArray(entityHandle), listed)) standingIn.Add(at);
        }

        // ------------------------------------------------------------------ Medusa (Awaken's BatchRendererGroup renderer)
        // Some large static sets are drawn by Medusa, not Drake: per scene, renderers (a mesh's submeshes with their
        // materials, and the LODs they belong to) placed by instance matrices baked into a file in the game's Medusa
        // archive, which Medusa reads into a GPU buffer only. The matrices are read from that file once per scene
        // (Unity's async reader: the file is inside a mounted archive); each instance's LOD is chosen as Medusa chooses
        // it (the first whose squared distance, kept per instance, the scaled camera distance is under).
        private sealed class MedusaScene { internal Matrix4x4[] Matrices; }
        private static readonly Dictionary<int, MedusaScene> medusaScenes = new Dictionary<int, MedusaScene>();
        private static int CaptureMedusa(Vector3 camera)
        {
            int count = 0;
            float scale = DrawnScale;
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
                        float d2 = dx * dx + dy * dy + dz * dz;
                        // Medusa's LOD: the lowest one whose distance the camera is within (none: its bit shift wraps to LOD 0).
                        int lod = WorldRules.MedusaLevel(d2 * scaleSq, l => brg.LodDistanceSq(t, (byte)l));
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
            ReadDrawnLod();
            float lodScale = DrawnScale;
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
                            (WorldRules.EntityLevelDrawn(range.LODMask, range.MinDist, range.MaxDist, d, drawnMaximumLod) ? " (the level drawn: traced)" : " (not the level drawn)");
                    }
                    else if (em.HasComponent<LODRange>(e)) lod = ", a LOD range without a reference point (not captured)";
                    else if (em.HasComponent<DrakeRendererVisibleRangeComponent>(e))
                    {
                        var v = em.GetComponentData<DrakeRendererVisibleRangeComponent>(e).value;
                        float d2 = math.distancesq(em.GetComponentData<LocalToWorld>(e).Value.c3.xyz, c);
                        lod = ", visible " + math.sqrt(v.x).ToString("0.#") + "-" + math.sqrt(v.y).ToString("0.#") + " (scaled) at " + math.sqrt(d2).ToString("0.#") + " m" +
                            (em.HasComponent<MaterialMeshInfo>(e) ? " (loaded for drawing: traced)" : " (not loaded: not traced)");
                    }
                    else lod = ", no LOD";
                    string shadows = em.HasComponent<Unity.Entities.Graphics.RenderFilterSettings>(e) ? em.GetSharedComponent<Unity.Entities.Graphics.RenderFilterSettings>(e).ShadowCastingMode.ToString() : "no filter settings";
                    // Its material, and whether rays see it: a traced mesh whose material is transparent is invisible to them
                    // (glass jars HDRP draws as transparent read "traced" here without it).
                    var materialData = manager.LoadingManager.MaterialLoadingData;
                    Material material = mm.materialIndex < materialData.Count ? Loaded(materialData[mm.materialIndex].loadingHandle, materialData[mm.materialIndex].counter) : null;
                    string seen = material == null ? ", its material not loaded" : ", material \"" + material.name + "\" (" + (material.shader != null ? material.shader.name : "no shader") + ")" +
                        (MaterialIndex(material) == Invisible ? ": invisible to rays (transparent, unlit or water: HDRP draws it after the traced light)" : "");
                    sb.Append("\n  Drake mesh \"" + (mesh != null ? mesh.name : mm.meshIndex < meshData.Count ? "(not loaded: not traced)" : "(no such mesh)") + "\" submesh " + mm.submesh + seen + ", " + shadows +
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
                            lod = ", LOD " + range.MinDist.ToString("0.#") + "-" + range.MaxDist.ToString("0.#") + " at " + d.ToString("0.#") +
                                (WorldRules.EntityLevelDrawn(range.LODMask, range.MinDist, range.MaxDist, d, drawnMaximumLod) ? " (the level drawn: traced)" : " (not the level drawn)");
                        }
                        text = mmi.HasMaterialMeshIndexRange ? "(a material-mesh range without its RendererRange: not traced)\"" :
                            (mesh != null ? mesh.name : "(unresolved: not traced)") + "\" submesh " + mmi.SubMesh + (pairCount > 1 ? " (and " + (pairCount - 1) + " more)" : "") + ", material \"" + (material != null ? material.name + " (" + material.shader.name + ")" : "none") + "\", " + shadows + lod +
                            (mesh != null && meshes.TryGetValue(mesh.GetInstanceID(), out var entry) ? (entry.Failed ? ", its mesh failed to register" : ", its mesh registered") : mesh != null ? ", its mesh not registered yet" : "");
                    }
                    catch (Exception ex) { text = "(" + ex.GetType().Name + ": " + ex.Message + ")\""; }
                    sb.Append("\n  entity (Entities Graphics, not Drake) mesh \"" + text);
                }
            }
            sb.Append("\n  Entities Graphics entities without Drake there: " + plain + " (of " + em.CreateEntityQuery(ComponentType.ReadOnly<MaterialMeshInfo>()).CalculateEntityCount() + " drawn entities in the world)");
        }
        internal static void ProbeMedusa(Vector3 at, Vector3 eye, System.Text.StringBuilder sb, float margin, int maxLines)
        {
            ReadDrawnLod();
            float scale = DrawnScale;
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
                        int lod = WorldRules.MedusaLevel((centre - eye).sqrMagnitude * scale * scale, l => brg.LodDistanceSq(t, (byte)l));
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
        private static int CaptureTerrains(List<NativeInstance> instances, List<uint> indices)
        {
            int count = 0;
            foreach (var t in Terrain.activeTerrains)
            {
                if (t == null || !t.isActiveAndEnabled || t.terrainData == null) continue;
                var data = t.terrainData;
                var origin = t.transform.position;
                int id = t.GetInstanceID();
                if (!terrains.TryGetValue(id, out int handle))
                {
                    handle = BuildTerrain(t);
                    terrains[id] = handle;
                }
                if (handle <= 0) continue;
                AddInstance(instances, indices, handle, Matrix4x4.Translate(origin), new[] { TerrainMaterial }, 0, t.renderingLayerMask);
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
