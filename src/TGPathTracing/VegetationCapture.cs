// SPDX-License-Identifier: MIT
// Trees and large objects placed by Vegetation Studio Pro, which draws them with GPU instancing and
// keeps no GameObjects for them. Its loaded cells hold every placed instance near the camera (per
// package and item, a list of position, rotation and scale, not culled to any view); each item names
// the prefab it instances. Each instance becomes one traced instance per renderer of the prefab's LOD
// that Unity would pick at that distance. Grass and plants are left out: too many and too small to
// matter for bounced light, and HDRP still draws them.
using System.Collections.Generic;
using AwesomeTechnologies.VegetationStudio;
using AwesomeTechnologies.VegetationSystem;
using Unity.Collections;
using UnityEngine;
namespace TGPathTracing
{
    internal static class VegetationCapture
    {
        private sealed class Part { internal Mesh Mesh; internal Material[] Materials; internal Matrix4x4 Local; }
        private sealed class Prefab
        {
            internal readonly List<List<Part>> Lods = new List<List<Part>>();
            internal readonly List<float> Heights = new List<float>(); // screen-relative transition heights
            internal float Size = 1f;
        }
        private static readonly Dictionary<int, Prefab> prefabs = new Dictionary<int, Prefab>();
        private static bool reported, reportedNone;
        private static readonly Dictionary<VegetationType, int> seen = new Dictionary<VegetationType, int>();

        internal static void Reset() => prefabs.Clear();

        private static Prefab Describe(GameObject root)
        {
            int id = root.GetInstanceID();
            if (prefabs.TryGetValue(id, out var prefab)) return prefab;
            prefab = new Prefab();
            var toRoot = root.transform.worldToLocalMatrix;
            Part PartOf(Renderer r)
            {
                var mr = r as MeshRenderer;
                var filter = mr != null ? mr.GetComponent<MeshFilter>() : null;
                if (filter == null || filter.sharedMesh == null) return null;
                return new Part { Mesh = filter.sharedMesh, Materials = mr.sharedMaterials, Local = toRoot * r.transform.localToWorldMatrix };
            }
            var group = root.GetComponentInChildren<LODGroup>(true);
            if (group != null && group.lodCount > 0)
            {
                prefab.Size = group.size;
                foreach (var lod in group.GetLODs())
                {
                    var parts = new List<Part>();
                    if (lod.renderers != null) foreach (var r in lod.renderers) { var p = r != null ? PartOf(r) : null; if (p != null) parts.Add(p); }
                    prefab.Lods.Add(parts);
                    prefab.Heights.Add(lod.screenRelativeTransitionHeight);
                }
            }
            else
            {
                var parts = new List<Part>();
                foreach (var r in root.GetComponentsInChildren<MeshRenderer>(true)) { var p = PartOf(r); if (p != null) parts.Add(p); }
                prefab.Lods.Add(parts);
                prefab.Heights.Add(0f);
                var bounds = new Bounds(); bool first = true;
                foreach (var r in root.GetComponentsInChildren<Renderer>(true)) { if (first) { bounds = r.bounds; first = false; } else bounds.Encapsulate(r.bounds); }
                prefab.Size = Mathf.Max(bounds.size.x, Mathf.Max(bounds.size.y, bounds.size.z));
            }
            prefabs[id] = prefab;
            return prefab;
        }

        private static bool Wanted(VegetationType type) => type == VegetationType.Tree || type == VegetationType.LargeObjects || type == VegetationType.Objects;

        internal static int Capture(Vector3 camera, SceneCapture.Sink sink)
        {
            var manager = VegetationStudioManager.Instance;
            var systems = manager != null ? manager.VegetationSystemList : null;
            if (systems == null || systems.Count == 0)
            {
                if (!reportedNone) { reportedNone = true; Session.Log.LogInfo("Vegetation Studio: " + (manager == null ? "no manager" : "no vegetation systems") + " in this scene."); }
                return 0;
            }
            int cells = 0; seen.Clear();
            var cam = Camera.main;
            float lodScale = cam != null ? QualitySettings.lodBias / (2f * Mathf.Tan(cam.fieldOfView * 0.5f * Mathf.Deg2Rad)) : 1f;
            int count = 0;
            foreach (var system in systems)
            {
                if (system == null || !system.isActiveAndEnabled) continue;
                system.CompleteCellLoading(); // the cells' instance lists are written by jobs
                var packages = system.VegetationPackageProList;
                foreach (var cell in system.LoadedVegetationCellList)
                {
                    if (cell == null) continue;
                    ++cells;
                    for (int p = 0; p < cell.VegetationPackageInstancesList.Count && p < packages.Count; p++)
                    {
                        var instances = cell.VegetationPackageInstancesList[p];
                        var package = packages[p];
                        if (instances == null || package == null) continue;
                        instances.LoadVegetationJobHandle.Complete();
                        for (int i = 0; i < instances.VegetationItemMatrixList.Count && i < package.VegetationInfoList.Count; i++)
                        {
                            var info = package.VegetationInfoList[i];
                            if (info == null) continue;
                            NativeList<MatrixInstance> list = instances.VegetationItemMatrixList[i];
                            if (!list.IsCreated || list.Length == 0) continue;
                            seen.TryGetValue(info.VegetationType, out int n); seen[info.VegetationType] = n + list.Length;
                            if (!Wanted(info.VegetationType) || info.VegetationPrefab == null) continue;
                            var prefab = Describe(info.VegetationPrefab);
                            if (prefab.Lods.Count == 0) continue;
                            for (int j = 0; j < list.Length; j++)
                            {
                                var m = list[j];
                                var position = new Vector3(m.Position.x, m.Position.y, m.Position.z);
                                float distance2 = (position - camera).sqrMagnitude;
                                var scale = new Vector3(m.Scale.x, m.Scale.y, m.Scale.z);
                                float height = prefab.Size * Mathf.Max(scale.x, Mathf.Max(scale.y, scale.z)) * lodScale / Mathf.Max(Mathf.Sqrt(distance2), 0.01f);
                                int lod = prefab.Lods.Count - 1; // past the last LOD the coarsest still shadows
                                for (int l = 0; l < prefab.Heights.Count; l++) if (height >= prefab.Heights[l]) { lod = l; break; }
                                Quaternion rotation = m.Rotation;
                                var world = Matrix4x4.TRS(position, rotation, scale);
                                foreach (var part in prefab.Lods[lod]) if (sink(part.Mesh, part.Materials, world * part.Local)) ++count;
                            }
                        }
                    }
                }
            }
            if (!reported && cells > 0)
            {
                reported = true;
                var types = new List<string>();
                foreach (var pair in seen) types.Add(pair.Key + " " + pair.Value);
                Session.Log.LogInfo("Vegetation Studio: " + systems.Count + " systems, " + cells + " loaded cells near the camera; instances by type: " +
                    (types.Count > 0 ? string.Join(", ", types.ToArray()) : "none") + "; " + count + " traced (trees, large objects and objects only).");
            }
            return count;
        }
    }
}
