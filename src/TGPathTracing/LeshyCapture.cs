// SPDX-License-Identifier: MIT
// Grass, plants, objects, ivy and trees placed by Leshy, the game's own vegetation renderer (it took over from
// Vegetation Studio): per scene, cells of instances of one prefab each, whose transforms it loads from its archive
// into memory for the cells near the camera and thins by its density setting (what it then draws). Each instance
// within reach becomes one traced instance per renderer of the prefab's LOD at its distance (Leshy's own rule,
// WorldRules.LeshyLevel; beyond the last Leshy draws nothing, and neither is anything traced). A tree past its last mesh
// Leshy draws as a billboard, a card that turns to the camera: no ray from anywhere else can meet that, so the tree's
// coarsest mesh is traced there instead (PT-63: until 2026-10-07 such trees were left out, a hole for the tracer's own
// camera rays). Grass and plants are traced as far as Leshy draws them, as everything else (PT-50:
// until 2026-10-07 only within 20 m, so past that grass cast no shadow and showed in no reflection). At most
// MaxInstances in all, nearest cells first: the tracer's own limit, far past the 1,224 the harness's scenes traced.
using System;
using System.Collections.Generic;
using Awaken.TG.LeshyRenderer;
using Unity.Mathematics;
using UnityEngine;
namespace TGPathTracing
{
    internal static class LeshyCapture
    {
        private const int MaxInstances = 1000000;
        private sealed class Prefab
        {
            internal LeshyPrefabs.RendererAuthoring[] Renderers; // without the billboard
            internal readonly float[] LodDistancesSq = new float[8];
            internal bool Small;                                 // grass or plants
            // A tree's billboard level and the mesh level traced in its place (-1: no billboard, or no mesh to trace).
            internal int CardLevel = -1, CardMeshLevel = -1;
        }
        private static readonly Dictionary<int, Prefab[]> prefabsByManager = new Dictionary<int, Prefab[]>();
        private static readonly List<(uint cell, float distance)> cells = new List<(uint, float)>();
        private static bool reported;
        internal static int Traced { get; private set; }
        // Trees past their last mesh at the last scan, traced as that mesh where the game draws a card.
        internal static int Cards { get; private set; }

        internal static void Reset() { prefabsByManager.Clear(); Traced = 0; Cards = 0; }

        internal static int Capture(Vector3 camera, SceneCapture.Sink sink)
        {
            int count = 0, instances = 0, grass = 0, managers = 0, cards = 0;
            float3 eye = new float3(camera.x, camera.y, camera.z);
            foreach (var manager in UnityEngine.Object.FindObjectsByType<LeshyManager>(FindObjectsSortMode.None))
            {
                if (manager == null || !manager.isActiveAndEnabled || manager.ForceDisable) continue;
                var prefabs = Prefabs(manager);
                if (prefabs == null) continue;
                var catalog = manager.EDITOR_Cells.cellsCatalog;
                var loading = manager.EDITOR_Loading;
                if (!catalog.IsCreated || !loading.filteredData.IsCreated) continue;
                ++managers;
                // The loaded cells, nearest first.
                cells.Clear();
                uint cellCount = Math.Min(catalog.Length, loading.filteredData.Length);
                for (uint c = 0; c < cellCount; ++c)
                {
                    var data = catalog[c];
                    if (data.prefabId >= prefabs.Length || prefabs[data.prefabId] == null || !loading.filteredData[c].IsValid) continue;
                    float distance = math.max(0f, math.distance(eye, data.bounds.Center) - math.length(data.bounds.Extents));
                    cells.Add((c, distance));
                }
                cells.Sort((a, b) => a.distance.CompareTo(b.distance));
                foreach (var (c, _) in cells)
                {
                    if (instances >= MaxInstances) break;
                    var prefab = prefabs[catalog[c].prefabId];
                    var placed = loading.filteredData[c];
                    for (uint k = 0; k < placed.Length && instances < MaxInstances; ++k)
                    {
                        var t = placed[k];
                        float d2 = math.distancesq(eye, t.position);
                        int lod = WorldRules.LeshyLevel(prefab.LodDistancesSq, d2);
                        if (lod < 0) continue; // beyond its last LOD Leshy draws nothing
                        bool card = lod == prefab.CardLevel;
                        if (card) { lod = prefab.CardMeshLevel; if (lod < 0) continue; }
                        var m = t.ToFloat4x4();
                        var world = new Matrix4x4(new Vector4(m.c0.x, m.c0.y, m.c0.z, m.c0.w), new Vector4(m.c1.x, m.c1.y, m.c1.z, m.c1.w),
                                                  new Vector4(m.c2.x, m.c2.y, m.c2.z, m.c2.w), new Vector4(m.c3.x, m.c3.y, m.c3.z, m.c3.w));
                        bool any = false;
                        foreach (var r in prefab.Renderers)
                            if ((r.lodMask & (1 << lod)) != 0 && r.mesh != null && sink(r.mesh, r.materials, world)) { ++count; any = true; }
                        if (any) { ++instances; if (prefab.Small) ++grass; if (card) ++cards; }
                    }
                }
            }
            Traced = instances; Cards = cards;
            if (!reported && managers > 0 && instances > 0)
            {
                reported = true;
                Session.Log.LogInfo("Leshy (the game's vegetation): " + instances + " instances traced near the camera (" + grass + " grass and plants, as far as Leshy draws them; " + cards + " trees past their last mesh traced as that mesh, where the game draws a card)" +
                    (instances >= MaxInstances ? ", the most traced at once" : ""));
            }
            return count;
        }

        // A manager's prefabs as it draws them (its quality's spawn distances cut their LODs), once.
        private static Prefab[] Prefabs(LeshyManager manager)
        {
            int id = manager.GetInstanceID();
            if (prefabsByManager.TryGetValue(id, out var known)) return known;
            Prefab[] result = null;
            try
            {
                var authoring = manager.EDITOR_Prefabs;
                if (authoring != null)
                {
                    var runtime = authoring.RuntimePrefabs(manager.EDITOR_Quality);
                    var source = authoring.Prefabs;
                    result = new Prefab[runtime.Length];
                    for (int i = 0; i < runtime.Length; ++i)
                    {
                        var renderers = runtime[i].renderers;
                        if (renderers == null || renderers.Length == 0) continue;
                        bool billboard = i < source.Length && source[i].HasBillboard;
                        var kept = renderers;
                        if (billboard) { kept = new LeshyPrefabs.RendererAuthoring[renderers.Length - 1]; Array.Copy(renderers, kept, kept.Length); }
                        var prefab = new Prefab
                        {
                            Renderers = kept,
                            Small = i < source.Length && (source[i].prefabType == LeshyPrefabs.PrefabType.Grass || source[i].prefabType == LeshyPrefabs.PrefabType.Plant)
                        };
                        // The distances as Leshy holds them (an unused level's is infinite: WorldRules.LeshyLevel).
                        var d = runtime[i].lodDistances;
                        for (int l = 0; l < 8; ++l)
                        {
                            float v = l < 4 ? d.c0[l] : d.c1[l - 4];
                            prefab.LodDistancesSq[l] = v * v;
                        }
                        if (billboard)
                        {
                            // The billboard is the last renderer, drawn at one level of its own (PrefabRuntime).
                            byte cardMask = renderers[renderers.Length - 1].lodMask;
                            for (int l = 0; l < 8; ++l) if ((cardMask & (1 << l)) != 0) prefab.CardLevel = l;
                            var masks = new List<byte>();
                            foreach (var r in kept) masks.Add(r.lodMask);
                            prefab.CardMeshLevel = prefab.CardLevel >= 0 ? WorldRules.CoarsestMeshLevel(masks, prefab.CardLevel) : -1;
                        }
                        result[i] = prefab;
                    }
                }
            }
            catch (Exception ex) { Session.Log.LogWarning("Leshy's prefabs could not be read; its vegetation stays out of the traced scene: " + ex.Message); }
            prefabsByManager[id] = result;
            return result;
        }
    }
}
