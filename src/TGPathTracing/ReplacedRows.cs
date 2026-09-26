// SPDX-License-Identifier: MIT
// The game's own Video rows that path tracing replaces are greyed out while it is chosen, with the reason as
// their tooltip: shadows and their distance (the sun's and every lamp's shadows are traced; HDRP's shadow maps
// only still cast light shafts in fog and shade transparent surfaces), contact shadows, ambient occlusion and
// reflections (traced, and HDRPasses turns HDRP's off), and subsurface scattering (the traced light replaces
// the lit color of skin). The rows follow the Path Tracing row as it is set, before Apply, and keep the game's
// own conditions (contact shadows follow the shadows toggle); the values are untouched and apply again as soon
// as path tracing is off. The Mono version only, like the mod.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using Awaken.TG.Main.Settings;
using Awaken.TG.Main.Settings.Options;
using Awaken.TG.MVC;
using TGGraphicsBridge;
namespace TGPathTracing
{
    internal static class ReplacedRows
    {
        private const string Traced = "Path Tracing traces ";
        private const string Again = " Your setting applies again when Path Tracing is Off.";
        // The game's setting class, the fields holding its rows, and why path tracing replaces them.
        private static readonly (string type, string[] fields, string reason)[] Rows =
        {
            ("Shadows", new[] { "_shadowsToggle", "_shadowsDistance" }, Traced + "the shadows of the sun and every lamp. HDRP's shadow maps only still " +
                "cast light shafts in fog and shade transparent surfaces." + Again),
            ("Shadows", new[] { "_contactShadowsToggle" }, Traced + "shadows down to where objects touch, so contact shadows are off while it runs." + Again),
            ("SSAO", new[] { "<Option>k__BackingField" }, Traced + "the light that bounces into corners and creases, so ambient occlusion is off " +
                "while it runs." + Again),
            ("Reflections", new[] { "<Option>k__BackingField" }, Traced + "reflections (its Reflections row), so screen-space reflections are off while " +
                "it runs." + Again),
            ("SSS", new[] { "_enableToggle", "_quality" }, Traced + "the light on skin as on every other surface, so subsurface scattering has no " +
                "effect while it runs." + Again),
        };
        private static readonly FieldInfo InteractableField = typeof(PrefOption).GetField("_isInteractable", BindingFlags.NonPublic | BindingFlags.Instance);
        private static readonly FieldInfo TooltipField = typeof(PrefOption).GetField("_tooltipConstructor", BindingFlags.NonPublic | BindingFlags.Instance);
        private sealed class Held
        {
            internal PrefOption Option;
            internal Func<bool> Original, Wrapper;
            internal Func<string> OriginalTooltip;
            internal string Reason;
        }
        private static readonly List<Held> held = new List<Held>();
        private static bool failed, replaced;
        private static float nextPoll;

        // Path tracing is chosen (in the Path Tracing row, before Apply too) and can run here.
        internal static bool Replacing =>
            PathTracingRow.PendingMode == TracingMode.Full && Session.HaveStatus && Session.Status.Supported != 0 && !HdrpPasses.Failed;

        internal static void Tick(float now)
        {
            if (failed || now < nextPoll) return;
            nextPoll = now + 0.5f;
            try
            {
                if (InteractableField == null || TooltipField == null)
                {
                    failed = true;
                    Session.Log.LogWarning("The game's Video rows can't be greyed out for path tracing (its settings API changed); they stay as they are.");
                    return;
                }
                var master = World.Any<SettingsMaster>();
                var list = master != null ? VideoTab.GraphicSettings(master) : null;
                if (list == null) return;
                foreach (var row in Rows)
                {
                    var setting = list.OfType<ISetting>().FirstOrDefault(s => VideoTab.TypeName(s) == row.type);
                    if (setting == null) continue;
                    foreach (var name in row.fields)
                    {
                        var field = setting.GetType().GetField(name, BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance);
                        if (field?.GetValue(setting) is PrefOption option) Hold(option, row.reason);
                    }
                }
                Refresh();
            }
            catch (Exception ex)
            {
                failed = true;
                Session.Log.LogWarning("The game's Video rows can't be greyed out for path tracing; they stay as they are: " + ex);
                Release();
            }
        }

        // Wraps a row's interactability once (again if the game set its own since): greyed while path tracing is
        // chosen, else as the game decides.
        private static void Hold(PrefOption option, string reason)
        {
            var entry = held.FirstOrDefault(h => ReferenceEquals(h.Option, option));
            var current = InteractableField.GetValue(option) as Func<bool>;
            if (entry != null && ReferenceEquals(current, entry.Wrapper)) return;
            if (entry == null)
            {
                entry = new Held { Option = option, Reason = reason, OriginalTooltip = TooltipField.GetValue(option) as Func<string> };
                held.Add(entry);
                if (replaced) option.AddTooltip(() => entry.Reason);
            }
            var original = current;
            entry.Original = original;
            entry.Wrapper = () => !replaced && (original == null || original());
            option.SetInteractabilityFunction(entry.Wrapper);
        }

        // The Path Tracing row changed, or it was applied: the rows follow, with their tooltips.
        internal static void Refresh()
        {
            bool now = Replacing;
            if (now == replaced) return;
            replaced = now;
            foreach (var h in held)
            {
                var entry = h;
                if (replaced) entry.Option.AddTooltip(() => entry.Reason);
                else entry.Option.AddTooltip(entry.OriginalTooltip);
            }
            VideoTab.RefreshInteractability(held.Select(h => h.Option));
        }

        // The mod stops: every row as the game had it.
        internal static void Release()
        {
            foreach (var h in held)
            {
                try
                {
                    if (ReferenceEquals(InteractableField?.GetValue(h.Option), h.Wrapper)) h.Option.SetInteractabilityFunction(h.Original);
                    if (replaced) h.Option.AddTooltip(h.OriginalTooltip);
                }
                catch { }
            }
            try { VideoTab.RefreshInteractability(held.Select(h => h.Option)); } catch { }
            held.Clear();
            replaced = false;
        }
    }
}
