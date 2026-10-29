// SPDX-License-Identifier: MIT
// Which of the game's materials are thin cloth (PT-39, PT-49: "we really need to make sure we detect cloth correctly. That also
// includes clothes hanging on a drier and stuff"), kept free of the game so tests/cloth_rules_tests.cs checks it against the
// census of the harness's scenes (tests/ClothRules.Tests.csproj, docs/agents/cloth.md). Recognised by what a material is, not
// one name at a time: the game's own cloth shaders whatever their materials are called, and a sheet of cloth by the word parts
// of its name and by being drawn from both sides.
using System;
using System.Collections.Generic;
using System.Text;
namespace TGPathTracing
{
    internal static class ClothRules
    {
        // The game's cloth shaders (TG/Clothes/Cloth_Wind sways its canopies, banners and laundry): thin cloth all of them.
        internal const string ClothFamily = "TG/Clothes/";
        // Words that name a hanging sheet of cloth: thin cloth however it is drawn (a tent's canvas modelled with both faces).
        private static readonly string[] Hanging = { "banner", "banners", "curtain", "curtains", "canvas", "tent", "tents", "flag", "flags", "sail",
            "sails", "awning", "awnings", "tarp", "laundry", "drape", "drapes", "canopy", "arras", "tapestry", "pennant", "pennants" };
        // Words that name cloth of any kind: thin cloth where HDRP draws it from both sides (a single sheet). One drawn from one
        // side is a closed shape: a statue's stone drapery, a wrapped body, a cloth-covered shield.
        private static readonly string[] Fabric = { "cloth", "cloths", "sheet", "sheets", "towel", "towels", "rag", "rags", "linen", "fabric",
            "veil", "blanket", "blankets" };
        // Compounds that end in one of these name cloth too ("tablecloth", "wagoncloth", "bedsheet"); shorter words would catch
        // words that only end like them ("content" in "tent").
        private static readonly string[] CompoundEnds = { "cloth", "sheet", "towel", "curtain", "banner", "canvas" };
        // Word parts of things that are no hanging cloth even when named so: lying on a floor, thick, or of stone, wood or rope.
        private static readonly string[] Not = { "floor", "rug", "carpet", "statue", "stone", "rock", "wood", "metal", "pole", "rope", "sack",
            "bag", "pillow", "fur", "leather", "hide", "net" };

        // shader: the material's shader name; material: its name; doubleSided: HDRP draws it from both sides (_DoubleSidedEnable).
        internal static bool IsThinCloth(string shader, string material, bool doubleSided)
        {
            shader = shader ?? "";
            if (shader.StartsWith(ClothFamily, StringComparison.OrdinalIgnoreCase)) return true;
            // Characters' clothes are no canopy (their own shaders: capes and sleeves shadow as solid cloth), and the interface is
            // no surface.
            if (shader.StartsWith("TG/Character/", StringComparison.OrdinalIgnoreCase) || shader.StartsWith("TG/UI/", StringComparison.OrdinalIgnoreCase) ||
                shader.StartsWith("UI/", StringComparison.OrdinalIgnoreCase) || shader.StartsWith("GUI/", StringComparison.OrdinalIgnoreCase) ||
                shader.StartsWith("Sprites/", StringComparison.OrdinalIgnoreCase) || shader.StartsWith("TextMeshPro", StringComparison.OrdinalIgnoreCase))
                return false;
            var words = Words(material);
            bool hanging = false, fabric = false;
            foreach (var w in words)
            {
                if (Array.IndexOf(Not, w) >= 0) return false;
                if (Array.IndexOf(Hanging, w) >= 0) hanging = true;
                if (Array.IndexOf(Fabric, w) >= 0 || EndsInCloth(w)) fabric = true;
            }
            return hanging || (fabric && doubleSided);
        }
        private static bool EndsInCloth(string word)
        {
            foreach (var end in CompoundEnds)
                if (word.Length > end.Length + 2 && word.EndsWith(end, StringComparison.Ordinal)) return true;
            return false;
        }
        // A name's word parts, lower case: split at anything but letters, and where a lower-case letter meets an upper-case one
        // ("Mat_BigTent_Plague_01": mat, big, tent, plague; "Mat_Wagoncloth_02": mat, wagoncloth).
        internal static List<string> Words(string name)
        {
            var words = new List<string>();
            if (string.IsNullOrEmpty(name)) return words;
            var current = new StringBuilder();
            char previous = ' ';
            foreach (char c in name)
            {
                bool letter = char.IsLetter(c);
                if (!letter || (char.IsUpper(c) && char.IsLower(previous)))
                {
                    if (current.Length > 0) { words.Add(current.ToString().ToLowerInvariant()); current.Clear(); }
                }
                if (letter) current.Append(c);
                previous = c;
            }
            if (current.Length > 0) words.Add(current.ToString().ToLowerInvariant());
            return words;
        }
        // The game's Cloth_Wind sways its vertices in its vertex shader; the tracer moves its traced copy the same way
        // (TGPT_SetClothWind). Its two switches, as the compiled shader uses them: the vertex colors mask the sway, and the noise
        // follows the object's position in place of the texture coordinates.
        internal const uint WindVertexPaint = 1, WindPositionUv = 2;
        internal const string ClothWindShader = "TG/Clothes/Cloth_Wind";
    }
}
