// SPDX-License-Identifier: MIT
// Grass, plants, objects, ivy and trees placed by Leshy, the game's own vegetation renderer (it took over from
// Vegetation Studio): per scene, cells of instances of one prefab each, whose transforms it loads from its archive
// into memory for the cells near the camera and thins by its density setting (what it then draws). Each instance
// within reach becomes one traced instance per renderer of the prefab's LOD at its distance (the first whose
// distance, in metres, it is within; beyond the last Leshy draws nothing, and neither is anything traced). Billboards
// (camera-facing cards) are left out. Grass and plants are traced only within GrassDistance: they are many and small,
// and near the camera is where they shadow and occlude each other and the ground. At most MaxInstances in all,
// nearest cells first.
using System;
using System.Collections.Generic;
using Awaken.TG.LeshyRenderer;
using Unity.Mathematics;
using UnityEngine;
namespace TGPathTracing
{
    internal static class LeshyCapture
    {
        private const float GrassDistance = 20f;
        private const int MaxInstances = 60000;
        private sealed class Prefab
        {
            internal LeshyPrefabs.RendererAuthoring[] Renderers; // without the billboard
            internal readonly float[] LodDistancesSq = new float[8];
            internal bool Small;                                 // grass or plants
        }
        private static readonly Dictionary<int, Prefab[]> prefabsByManager = new Dictionary<int, Prefab[]>();
        private static readonly List<(uint cell, float distance)> cells = new List<(uint, float)>();
        private static bool reported;
        internal static int Traced { get; private set; }

        internal static void Reset() { prefabsByManager.Clear(); Traced = 0; }

        internal static int Capture(Vector3 camera, float radius, SceneCapture.Sink sink)
        {
            int count = 0, instances = 0, grass = 0, managers = 0;
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
                // The loaded cells in reach, nearest first.
                cells.Clear();
                uint cellCount = Math.Min(catalog.Length, loading.filteredData.Length);
                for (uint c = 0; c < cellCount; ++c)
                {
                    var data = catalog[c];
                    if (data.prefabId >= prefabs.Length || prefabs[data.prefabId] == null || !loading.filteredData[c].IsValid) continue;
                    float reach = prefabs[data.prefabId].Small ? Math.Min(GrassDistance, radius) : radius;
                    float distance = math.max(0f, math.distance(eye, data.bounds.Center) - math.length(data.bounds.Extents));
                    if (distance <= reach) cells.Add((c, distance));
                }
                cells.Sort((a, b) => a.distance.CompareTo(b.distance));
                foreach (var (c, _) in cells)
                {
                    if (instances >= MaxInstances) break;
                    var prefab = prefabs[catalog[c].prefabId];
                    float reach = prefab.Small ? Math.Min(GrassDistance, radius) : radius;
                    float reachSq = reach * reach;
                    var placed = loading.filteredData[c];
                    for (uint k = 0; k < placed.Length && instances < MaxInstances; ++k)
                    {
                        var t = placed[k];
                        float d2 = math.distancesq(eye, t.position);
                        if (d2 > reachSq) continue;
                        int lod = -1;
                        for (int l = 0; l < 8; ++l) if (d2 < prefab.LodDistancesSq[l]) { lod = l; break; }
                        if (lod < 0) continue; // beyond its last LOD Leshy draws nothing
                        var m = t.ToFloat4x4();
                        var world = new Matrix4x4(new Vector4(m.c0.x, m.c0.y, m.c0.z, m.c0.w), new Vector4(m.c1.x, m.c1.y, m.c1.z, m.c1.w),
                                                  new Vector4(m.c2.x, m.c2.y, m.c2.z, m.c2.w), new Vector4(m.c3.x, m.c3.y, m.c3.z, m.c3.w));
                        bool any = false;
                        foreach (var r in prefab.Renderers)
                            if ((r.lodMask & (1 << lod)) != 0 && r.mesh != null && sink(r.mesh, r.materials, world)) { ++count; any = true; }
                        if (any) { ++instances; if (prefab.Small) ++grass; }
                    }
                }
            }
            Traced = instances;
            if (!reported && managers > 0 && instances > 0)
            {
                reported = true;
                Session.Log.LogInfo("Leshy (the game's vegetation): " + instances + " instances traced near the camera (" + grass + " grass and plants, within " +
                    GrassDistance.ToString("0") + " m)" + (instances >= MaxInstances ? ", the most traced at once" : ""));
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
                        var d = runtime[i].lodDistances;
                        for (int l = 0; l < 8; ++l)
                        {
                            float v = l < 4 ? d.c0[l] : d.c1[l - 4];
                            prefab.LodDistancesSq[l] = float.IsInfinity(v) || float.IsNaN(v) ? -1f : v * v; // no such LOD: never chosen
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
