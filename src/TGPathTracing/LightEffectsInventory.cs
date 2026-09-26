// SPDX-License-Identifier: MIT
// Once a session: the meshes that fake light rather than receive it (light shafts, beams, glow cards, light cones:
// transparent or unlit, named or shaded like a light effect), grouped by shader and material name, so the ones a path
// tracer should not draw over its own light can be told apart. They are already invisible to rays; HDRP draws them.
using System;
using System.Collections.Generic;
using System.Text;
using UnityEngine;
namespace TGPathTracing
{
    internal static class LightEffectsInventory
    {
        private static readonly string[] EffectWords = { "Light", "Shaft", "Beam", "God", "Volum", "Glow", "Fake", "Cone", "Halo", "Flare", "Ray" };

        internal static void Log()
        {
            var groups = new Dictionary<string, (int count, string example)>();
            foreach (var r in UnityEngine.Object.FindObjectsByType<MeshRenderer>(FindObjectsSortMode.None))
            {
                if (r == null) continue;
                var materials = r.sharedMaterials;
                if (materials == null) continue;
                foreach (var m in materials)
                {
                    if (m == null || m.shader == null) continue;
                    string shader = m.shader.name;
                    bool transparent = (m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f) || m.renderQueue >= 2750 ||
                        shader.IndexOf("Unlit", StringComparison.OrdinalIgnoreCase) >= 0;
                    if (!transparent) continue;
                    string names = shader + " " + m.name + " " + r.name;
                    if (!Array.Exists(EffectWords, w => names.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0)) continue;
                    string key = shader + " | " + System.Text.RegularExpressions.Regex.Replace(m.name.Replace("(Instance)", "").Replace("(Clone)", ""), @"[\d_ ]+$", "").Trim();
                    groups.TryGetValue(key, out var g);
                    groups[key] = (g.count + 1, g.example ?? Probe.PathOf(r.transform));
                }
            }
            var list = new List<KeyValuePair<string, (int count, string example)>>(groups);
            list.Sort((a, b) => b.Value.count.CompareTo(a.Value.count));
            var sb = new StringBuilder("Light-effect meshes (transparent or unlit, named or shaded like light; shader | material, count, an example):");
            for (int i = 0; i < list.Count && i < 30; ++i) sb.Append("\n  " + list[i].Key + " x" + list[i].Value.count + ", e.g. \"" + list[i].Value.example + "\"");
            if (list.Count == 0) sb.Append(" none among GameObject renderers");
            Session.Log.LogInfo(sb.ToString());
        }
    }
}
