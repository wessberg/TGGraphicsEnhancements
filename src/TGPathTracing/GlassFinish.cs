// SPDX-License-Identifier: MIT
// Glass and cornea classification, optical values, and suppression of duplicate HDRP
// transparent draws. Camera and secondary rays evaluate the same dielectric material.
// Glass is what the game itself draws see-through (a transparent material named like glass, a cornea); its alpha is the
// share of a texel that is paint, a label or a lid on it. A bottle the game draws opaque is an opaque, painted thing.
using System;
using System.Collections.Generic;
using UnityEngine;
namespace TGPathTracing
{
    internal static class GlassFinish
    {
        private static readonly string[] Names = { "Bottle", "Glass", "Flask", "Vial", "Potion", "Phial", "Decanter", "Carafe", "Beaker", "Jar" };
        // What holds glass is not glass (a bottle rack, a jar's shelf).
        private static readonly string[] NotGlass = { "Rack", "Shelf", "Crate", "Box", "Holder", "Stand", "Basket", "Case", "Cork", "Label", "Table", "Barrel",
            "Chest", "Cabinet", "Cart", "Wood", "Metal", "Iron" };
        private static readonly HashSet<string> logged = new HashSet<string>();
        // Glass HDRP draws transparent (TGPT_MATERIAL_SEE_THROUGH), which the tracer draws in its place: HDRP's own passes for
        // it (all but its shadow caster) are hidden while tracing and given back when it stops.
        private static readonly List<Material> seeThrough = new List<Material>();
        private static readonly Dictionary<Material, List<string>> hiddenPasses = new Dictionary<Material, List<string>>();
        private static bool tracing;

        // A glass word of its own in the material's name: "WineBottle", "Bottles", "Glass_01"; not "Jarl" or "Glassblower".
        internal static bool IsGlass(Material m)
        {
            if (m == null) return false;
            if (IsCornea(m)) return true;
            foreach (var w in NotGlass) if (m.name.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0) return false;
            return NamedGlass(m.name);
        }
        internal static bool IsCornea(Material m) => m != null && m.name.IndexOf("Cornea", StringComparison.OrdinalIgnoreCase) >= 0;
        // HDRP Lit reads its refraction values (_Ior, _TransmittanceColor, _ATDistance) only with a refraction model on
        // (_RefractionModel 1 planar, 2 sphere, 3 thin: its HAS_REFRACTION code). With none, the game shows nothing of them:
        // every glass material of run 20261007-224654 has _RefractionModel 0, and the jars' and the broken glass's _Ior 1.01
        // is a value no pixel of the game was ever drawn with.
        internal static bool Refracts(Material m) => m != null && m.HasProperty("_RefractionModel") && m.GetFloat("_RefractionModel") > 0.5f;
        internal static void ReadOptics(Material m, ref NativeMaterial mat)
        {
            // A cornea is a clear shell: no paint on it (BaseA 0: pt_common see_through_cover), white where its color is asked.
            if (IsCornea(m)) { mat.BaseR = mat.BaseG = mat.BaseB = 1f; mat.BaseA = 0f; }
            // The index of refraction the game's shader shows: its own _Ior where it refracts by it; else the one of the
            // mirror reflection it does show, HDRP Lit's 4 % of a dielectric (1.5), a cornea's 1.376. Read whatever the
            // shader does with it, the jars' unused 1.01 reflected 0.0025 % (no highlight on glass that has one in the game).
            bool refracts = Refracts(m);
            // The game's shader bends and blurs what is behind this glass by the material's index, roughness and normal
            // (a refraction model on): the tracer's view through it does the same (pt_common OPTICAL_REFRACTS, read while
            // "Glass trial" 1 is on trial: without the bit the view crosses the mesh's own smooth face).
            if (refracts) mat.OpticalReserved1 |= 1u;
            mat.Ior = refracts && m.HasProperty("_Ior") ? m.GetFloat("_Ior") : IsCornea(m) ? 1.376f : 1.5f;
            if (!(mat.Ior > 0f) || float.IsInfinity(mat.Ior)) mat.Ior = 1.5f;
            // HDRP's absorption: its transmittance color is what is left of the light after _ATDistance metres (the property
            // was asked for as "_AbsorptionDistance" until 2026-10-08, a name HDRP does not have: nothing was ever read).
            if (refracts && m.HasProperty("_TransmittanceColor") && m.HasProperty("_ATDistance"))
            {
                var c = m.GetColor("_TransmittanceColor");
                float d = Mathf.Max(m.GetFloat("_ATDistance"), 1e-6f);
                mat.AbsorptionR = -Mathf.Log(Mathf.Clamp(c.r, 1e-6f, 1f)) / d;
                mat.AbsorptionG = -Mathf.Log(Mathf.Clamp(c.g, 1e-6f, 1f)) / d;
                mat.AbsorptionB = -Mathf.Log(Mathf.Clamp(c.b, 1e-6f, 1f)) / d;
                // Where the glass's mesh closes a volume, that absorption takes its share over a ray's way inside it. An
                // open sheet (a pane of one quad, a shard) has no inside: it is a thin pane as thick as the material says
                // (HDRP Lit's refraction thickness, in metres; its Thin model's own 5 mm), and tints by the way through that
                // (PT-76; pt_common pane_transmittance). The thickness map, where a material has one, is not read: the
                // material's number alone. (The Thin model's 5 mm is from HDRP's documentation of that model; to be checked
                // against the game's compiled shader when a material of the game uses it: none does today.)
                float thickness = m.GetFloat("_RefractionModel") > 2.5f ? 0.005f : m.HasProperty("_Thickness") ? Mathf.Max(m.GetFloat("_Thickness"), 0f) : 0f;
                mat.OpticalReserved2 = BitConverter.ToUInt32(BitConverter.GetBytes(thickness), 0);
            }
        }
        private static bool NamedGlass(string name)
        {
            foreach (var w in Names)
            {
                int i = name.IndexOf(w, StringComparison.OrdinalIgnoreCase);
                if (i < 0) continue;
                int end = i + w.Length;
                if (end >= name.Length || !char.IsLower(name[end]) || (name[end] == 's' && (end + 1 >= name.Length || !char.IsLower(name[end + 1])))) return true;
            }
            return false;
        }
        // PT-35: one line per material that says it is taken for see-through glass, with what decides how it looks: its alpha
        // (times its texture's: the game's jars are 1 here and 0.165 in their texture's glass) and how its texture is read.
        internal static void AddSeeThrough(Material m, float alpha, string texture)
        {
            if (m == null || seeThrough.Contains(m)) return;
            seeThrough.Add(m);
            if (tracing) Hide(m);
            Session.Log.LogInfo("Glass: \"" + m.name + "\" (" + (m.shader != null ? m.shader.name : "no shader") +
                ") is transparent in the game: taken for see-through glass, drawn by the tracer while tracing (its material reflects and refracts, " +
                "the mesh supplies each interface), HDRP's own passes hidden; base color alpha " + alpha.ToString("0.##") + " (times its texture's), " + texture);
        }
        // PT-35: why a transparent material named like glass is not drawn as see-through glass (once per name): rays don't see it.
        internal static void ExplainNotSeeThrough(Material m, string shaderName, bool unlit)
        {
            if (m == null || !NamedGlass(m.name) || !logged.Add("not:" + m.name)) return;
            string why = unlit ? "its shader is unlit" : shaderName.IndexOf("NoRender", StringComparison.OrdinalIgnoreCase) >= 0 ? "its shader draws nothing" : null;
            if (why == null) foreach (var w in NotGlass) if (m.name.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0) { why = "its name also says \"" + w + "\" (not glass)"; break; }
            Session.Log.LogInfo("Glass: \"" + m.name + "\" (" + shaderName + ") is transparent and named like glass, but not drawn as see-through glass: " +
                (why ?? "no rule took it") + "; invisible to rays, HDRP draws it");
        }
        // Path tracing on or off (HdrpPasses): HDRP's passes for see-through glass hidden, or given back.
        internal static void HideSeeThrough(bool on)
        {
            if (on == tracing) return;
            tracing = on;
            if (on) foreach (var m in seeThrough) Hide(m);
            else RestorePasses();
        }
        private static void Hide(Material m)
        {
            if (m == null || m.shader == null || hiddenPasses.ContainsKey(m)) return;
            var passes = new List<string>();
            try
            {
                var lightMode = new UnityEngine.Rendering.ShaderTagId("LightMode");
                for (int p = 0; p < m.shader.passCount; ++p)
                {
                    string mode = m.shader.FindPassTagValue(p, lightMode).name;
                    if (string.IsNullOrEmpty(mode) || mode == "ShadowCaster" || passes.Contains(mode) || !m.GetShaderPassEnabled(mode)) continue;
                    m.SetShaderPassEnabled(mode, false);
                    passes.Add(mode);
                }
            }
            catch (Exception ex) { Session.Log.LogWarning("Glass: \"" + m.name + "\" could not be hidden from HDRP: " + ex.Message); }
            hiddenPasses[m] = passes;
        }
        private static void RestorePasses()
        {
            foreach (var entry in hiddenPasses)
                try { if (entry.Key != null) foreach (var pass in entry.Value) entry.Key.SetShaderPassEnabled(pass, true); } catch { }
            hiddenPasses.Clear();
        }
        // BepInEx names each glass material once.
        internal static void Add(Material m)
        {
            if (m != null && logged.Add(m.name))
            {
                var optics = new NativeMaterial();
                ReadOptics(m, ref optics);
                bool refracts = Refracts(m);
                Session.Log.LogInfo("Glass: \"" + m.name + "\" is a surface of camera and secondary rays; index of refraction " +
                    optics.Ior.ToString("0.###") + (refracts && m.HasProperty("_Ior") ? " (material)" : IsCornea(m) ? " (a cornea's)" :
                        " (its shader's 4 % reflection: no refraction model" + (m.HasProperty("_Ior") ? ", its _Ior " + m.GetFloat("_Ior").ToString("0.###") + " unused by the game)" : ")")) +
                    ", absorption per metre " + optics.AbsorptionR.ToString("0.###") + ", " + optics.AbsorptionG.ToString("0.###") +
                    ", " + optics.AbsorptionB.ToString("0.###") + (refracts && m.HasProperty("_TransmittanceColor") && m.HasProperty("_ATDistance") ?
                        " (material; as an open sheet a pane " + BitConverter.ToSingle(BitConverter.GetBytes(optics.OpticalReserved2), 0).ToString("0.###") + " m thick)" :
                        " (no volume absorption in the game's shader; clear)") +
                    "; uses its material roughness; its paint, labels and lids by its alpha");
            }
        }
        // A material named like glass that the game draws opaque (once per name): what the game shows of it is a painted,
        // opaque thing (a stoneware bottle's glaze, its label and its cork in one texture), and that is what is traced.
        internal static void ExplainOpaque(Material m, string shaderName)
        {
            if (m == null || !IsGlass(m) || !logged.Add("opaque:" + m.name)) return;
            Session.Log.LogInfo("Glass: \"" + m.name + "\" (" + shaderName + ") is named like glass but opaque in the game (surface type 0, queue " + m.renderQueue +
                "): traced as the opaque surface the game draws, with its own color, label and finish; no ray passes it");
        }
        // A new scene: its glass is read again; what was hidden is given back first.
        internal static void Reset() { RestorePasses(); seeThrough.Clear(); if (tracing) { tracing = false; } }
    }
}
