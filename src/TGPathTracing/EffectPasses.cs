// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;

namespace TGPathTracing
{
    // Transitional raster effects (PT-06/PT-41/PT-73). Simulation keeps running.
    // Find distortion by the pass's LightMode, including unnamed VFX outputs.
    // Additive Unlit VFX without precomputed velocity write the unavailable-motion
    // marker over their invisible border. Select their colour-only variant.
    // Retire this adapter when these outputs are surfaces of the tracer's rays.
    internal static class EffectPasses
    {
        private const string Motion = "_TransparentWritingMotionVec", Keyword = "_TRANSPARENT_WRITES_MOTION_VEC";
        private const string Distortion = "DistortionVectors";
        private static readonly ShaderTagId LightMode = new ShaderTagId("LightMode");
        private struct Saved { internal bool Distortion, DistortionEnabled, Motion; internal float Value; internal bool Keyword; }
        private static readonly Dictionary<Material, Saved> saved = new Dictionary<Material, Saved>();
        private static float nextScan;
        private static bool active, warned;

        private static bool HasDistortion(Material m)
        {
            for (int i = 0; i < m.passCount; ++i)
                if (m.shader.FindPassTagValue(i, LightMode).name == Distortion) return true;
            return false;
        }
        private static bool WritesUnavailableMotion(Material m)
        {
            return m.shader.name.StartsWith("Hidden/VFX/", StringComparison.Ordinal) &&
                m.shader.name.EndsWith("Shader Graph Quad - Unlit", StringComparison.Ordinal) &&
                m.HasProperty(Motion) && m.HasProperty("_SrcBlend") && m.GetFloat("_SrcBlend") == 1f &&
                m.HasProperty("_DstBlend") && m.GetFloat("_DstBlend") == 1f &&
                m.HasProperty("_ZWrite") && m.GetFloat("_ZWrite") == 0f &&
                (!m.HasProperty("_AddPrecomputedVelocity") || m.GetFloat("_AddPrecomputedVelocity") == 0f);
        }
        private static void Apply(Material m, Saved s, bool on)
        {
            if (m == null) return;
            if (s.Distortion) m.SetShaderPassEnabled(Distortion, on ? false : s.DistortionEnabled);
            if (!s.Motion) return;
            m.SetFloat(Motion, on ? 0f : s.Value);
            if (!on && s.Keyword) m.EnableKeyword(Keyword); else m.DisableKeyword(Keyword);
        }
        internal static void Hold(bool on)
        {
            if (!on)
            {
                foreach (var pair in saved) Apply(pair.Key, pair.Value, false);
                saved.Clear(); active = false; nextScan = 0f;
                return;
            }
            try
            {
                foreach (var pair in saved) Apply(pair.Key, pair.Value, true);
                if (active && Time.unscaledTime < nextScan) return;
                active = true; nextScan = Time.unscaledTime + 1f;
                // Loaded materials include off-screen fires and effects not yet spawned.
                // Never disable the entire VFX renderer: it also draws flames and smoke.
                foreach (var m in Resources.FindObjectsOfTypeAll<Material>())
                {
                    if (m == null || m.shader == null || saved.ContainsKey(m)) continue;
                    bool distortion = HasDistortion(m), motion = WritesUnavailableMotion(m);
                    if (!distortion && !motion) continue;
                    var s = new Saved { Distortion = distortion, DistortionEnabled = m.GetShaderPassEnabled(Distortion),
                        Motion = motion, Value = motion ? m.GetFloat(Motion) : 0f, Keyword = motion && m.IsKeywordEnabled(Keyword) };
                    saved.Add(m, s); Apply(m, s, true);
                    Session.Log.LogInfo("Traced effect material \"" + m.shader.name + "\": distortion pass=" + distortion +
                        ", background motion=" + motion + "; original states restored on Off");
                }
            }
            catch (Exception ex)
            {
                if (!warned) { warned = true; Session.Log.LogWarning("Effect pass switch failed: " + ex.Message); }
            }
        }
    }
}
