// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using Awaken.Kandra;
using Awaken.TG.Main.Heroes;
using UnityEngine;

namespace TGGraphicsBridge
{
    // The captured hands had camera-only vectors. Shipped character materials disable
    // MOTIONVECTORS despite having compiled motion passes and Kandra previous positions.
    // Enable those passes on opaque materials used by the first-person Kandra rig while a mod
    // that reads the game's object motion vectors (frame generation, FSR or XeSS) asks for it.
    internal static class FirstPersonMotion
    {
        private const string MotionPass = "MotionVectors";
        private static readonly Dictionary<int, Material> changed = new Dictionary<int, Material>();
        private static readonly HashSet<int> seen = new HashSet<int>();
        private static readonly List<int> retired = new List<int>();
        private static float nextScan;
        private static int lastRoot;
        private static bool failed;

        internal static void Tick(float now)
        {
            if (failed) return;
            try
            {
                if (!MotionVectors.Requested)
                {
                    Restore(); return;
                }
                if (now < nextScan) return;
                nextScan = now + 0.5f;
                var controller = Hero.Current?.VHeroController;
                var root = controller == null ? null : controller.fppParent;
                if (root == null) { Restore(); return; }
                seen.Clear();
                int enabled = 0, eligible = 0;
                var renderers = root.GetComponentsInChildren<KandraRenderer>(true);
                foreach (var renderer in renderers)
                {
                    if (renderer == null) continue;
                    var materials = renderer.rendererData.RenderingMaterials;
                    if (materials == null) continue;
                    foreach (var material in materials)
                    {
                        if (material == null || material.shader == null || material.renderQueue >= 2500 ||
                            (material.HasProperty("_SurfaceType") && material.GetFloat("_SurfaceType") != 0)) continue;
                        int id = material.GetInstanceID();
                        if (!seen.Add(id)) continue;
                        bool hasPass = false;
                        for (int pass = 0; pass < material.passCount; ++pass)
                            if (string.Equals(material.GetPassName(pass), MotionPass, StringComparison.OrdinalIgnoreCase))
                            { hasPass = true; break; }
                        if (!hasPass) continue;
                        ++eligible;
                        if (material.GetShaderPassEnabled(MotionPass)) continue;
                        // Keep the actual rendering material: Kandra owns its instances and refcounts.
                        changed[id] = material;
                        material.SetShaderPassEnabled(MotionPass, true);
                        ++enabled;
                        BridgeLog.Log.LogInfo("First-person motion pass enabled: " + renderer.name + " / " +
                            material.name + " (" + material.shader.name + ")");
                    }
                }
                // Equipment/body replacements can retire materials while the same rig remains.
                retired.Clear();
                foreach (var entry in changed)
                    if (!seen.Contains(entry.Key) || entry.Value == null) retired.Add(entry.Key);
                foreach (int id in retired)
                {
                    if (changed[id] != null) changed[id].SetShaderPassEnabled(MotionPass, false);
                    changed.Remove(id);
                }
                if (root.GetInstanceID() != lastRoot || enabled != 0)
                {
                    lastRoot = root.GetInstanceID();
                    BridgeLog.Log.LogInfo("First-person motion (for " + MotionVectors.Owners + "): " + renderers.Length + " Kandra renderers, " +
                        eligible + " opaque motion-pass materials, " + enabled + " enabled this scan.");
                }
            }
            catch (Exception error)
            {
                Restore(); failed = true;
                BridgeLog.Log?.LogWarning("First-person motion pass correction disabled: " + error.Message);
            }
        }

        internal static void Restore()
        {
            foreach (var material in changed.Values)
                if (material != null) material.SetShaderPassEnabled(MotionPass, false);
            changed.Clear(); seen.Clear(); retired.Clear(); lastRoot = 0; nextScan = 0;
        }
    }
}
