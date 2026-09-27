// SPDX-License-Identifier: MIT
// Glass while tracing. The game draws its bottles and flasks opaque, a label and a cork in the same texture: known by name
// (IsGlass), rays pass through them by chance, as bright as their color is (TGPT_MATERIAL_GLASS), and the tracer sees
// through their smooth texels (pt_prepare, pt_specular), with a glass's mirror finish there; a label or a cork, rough in
// the same texture, stays solid. The game's materials are left as they are.
using System;
using System.Collections.Generic;
using UnityEngine;
namespace TGPathTracing
{
    internal static class GlassFinish
    {
        // The traced material's smoothness, for the rays that meet the glass after another surface.
        internal const float Smoothness = 0.95f;
        private static readonly string[] Names = { "Bottle", "Glass", "Flask", "Vial", "Potion", "Phial", "Decanter", "Carafe", "Beaker", "Jar" };
        // What holds glass is not glass (a bottle rack, a jar's shelf).
        private static readonly string[] NotGlass = { "Rack", "Shelf", "Crate", "Box", "Holder", "Stand", "Basket", "Case", "Cork", "Label", "Table", "Barrel",
            "Chest", "Cabinet", "Cart", "Wood", "Metal", "Iron" };
        private static readonly HashSet<string> logged = new HashSet<string>();

        // A glass word of its own in the material's name: "WineBottle", "Bottles", "Glass_01"; not "Jarl" or "Glassblower".
        internal static bool IsGlass(Material m)
        {
            if (m == null) return false;
            string name = m.name;
            foreach (var w in NotGlass) if (name.IndexOf(w, StringComparison.OrdinalIgnoreCase) >= 0) return false;
            foreach (var w in Names)
            {
                int i = name.IndexOf(w, StringComparison.OrdinalIgnoreCase);
                if (i < 0) continue;
                int end = i + w.Length;
                if (end >= name.Length || !char.IsLower(name[end]) || (name[end] == 's' && (end + 1 >= name.Length || !char.IsLower(name[end + 1])))) return true;
            }
            return false;
        }
        // BepInEx names each glass material once.
        internal static void Add(Material m)
        {
            if (m != null && logged.Add(m.name))
                Session.Log.LogInfo("Glass: \"" + m.name + "\" lets light through as bright as its color, and is seen through where it is smooth");
        }
        internal static void Reset() { }
    }
}
