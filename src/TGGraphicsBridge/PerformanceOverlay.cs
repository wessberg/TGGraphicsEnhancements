// SPDX-License-Identifier: MIT
// The performance overlay: a panel in a corner of the screen showing what the game and the mods built on
// the bridge are doing, switched on in the Video settings (the last row there, see PerformanceOverlayRow),
// FoA Mod Manager or the config. The bridge's own section has the frame rate the game renders at, frame
// times, the render and output resolution, VSync and the frame cap, and CPU, GPU and video memory use;
// each mod adds a section of its own through AddSection. It is drawn with Unity's IMGUI after the game's
// UI, the text is rebuilt four times a second, and nothing is measured while it is off.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using BepInEx.Configuration;
using UnityEngine;
using UnityEngine.Rendering;
namespace TGGraphicsBridge
{
    public enum OverlayCorner { TopLeft, TopRight, BottomLeft, BottomRight }
    public static class PerformanceOverlay
    {
        // The Video section, as the mods' Video rows: FoA Mod Manager lists exactly the Video tab's rows.
        private const string Section = "Video", RetiredSection = "Debug";
        private sealed class Part
        {
            internal string Title;
            internal int Order;
            internal Func<IEnumerable<string>> Lines;
        }
        private static readonly List<Part> parts = new List<Part>();
        private static ConfigEntry<bool> shown;
        private static ConfigEntry<OverlayCorner> corner;
        private static ConfigEntry<float> size;
        private static ConfigurationManagerAttributes cornerRow, sizeRow;
        private static bool storing;
        // A value changed in FoA Mod Manager or the config file, not through the Video row.
        public static event Action Changed;
        internal static OverlayCorner Corner => corner != null ? corner.Value : OverlayCorner.TopLeft;
        internal static float Size => size != null ? Mathf.Clamp(size.Value, 0.5f, 2f) : 1f;
        // Frame times of the last 600 rendered frames, in seconds.
        private static readonly float[] times = new float[600];
        private static int count, next;
        private static float nextText;
        private static string text = "";
        private static GUIStyle style, wrapped;
        private static int styleFont;
        public static bool Visible => shown != null && shown.Value;
        // The frame rate the game renders at (before frame generation), over the last second.
        public static float RenderedFps { get; private set; }
        // A mod's section, below the bridge's own ones in ascending order; its lines are read four times
        // a second while the overlay shows. A second section with the same title replaces the first.
        public static void AddSection(string title, int order, Func<IEnumerable<string>> lines)
        {
            if (string.IsNullOrEmpty(title) || lines == null) return;
            lock (parts)
            {
                parts.RemoveAll(p => p.Title == title);
                parts.Add(new Part { Title = title, Order = order, Lines = lines });
                parts.Sort((a, b) => a.Order.CompareTo(b.Order));
            }
        }
        internal const string Description = "Shows a panel in a corner of the screen: frame rate and frame times, render and output resolution, " +
            "VSync and the frame cap, CPU, GPU and video memory use, and what TG Frame Generation and TG Better Upscaling are doing.";
        internal const string PositionDescription = "The corner of the screen the performance overlay shows in.";
        internal const string SizeDescription = "The performance overlay's text size: 100% is 15 pixels at 1080p, scaled with the resolution.";
        internal static void Bind(ConfigFile config)
        {
            var orphans = typeof(ConfigFile).GetProperty("OrphanedEntries", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic |
                System.Reflection.BindingFlags.Public)?.GetValue(config, null) as Dictionary<ConfigDefinition, string>;
            shown = config.Bind(Section, "Performance overlay", false, new ConfigDescription(Description, null, Row("Performance Overlay", 10)));
            cornerRow = Row("Overlay Position", 11, "TopLeft=Top Left;TopRight=Top Right;BottomLeft=Bottom Left;BottomRight=Bottom Right");
            corner = config.Bind(Section, "Performance overlay position", OverlayCorner.TopLeft, new ConfigDescription(PositionDescription, null, cornerRow));
            sizeRow = Row("Overlay Size", 12);
            size = config.Bind(Section, "Performance overlay size", 1f, new ConfigDescription(SizeDescription, new AcceptableValueRange<float>(0.5f, 2f), sizeRow));
            // A test build's Debug entries, carried over once and dropped.
            if (orphans != null)
            {
                if (bool.TryParse(Take(orphans, "Show debug overlay"), out bool wasShown) && wasShown) shown.Value = true;
                if (Enum.TryParse(Take(orphans, "Debug overlay corner"), out OverlayCorner wasCorner)) corner.Value = wasCorner;
                if (float.TryParse(Take(orphans, "Debug overlay size"), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float wasSize))
                    size.Value = Mathf.Clamp(wasSize, 0.5f, 2f);
                SafeConfig.Save(config, BridgePlugin.Name);
            }
            config.SettingChanged += OnSettingChanged;
            RefreshRows();
        }
        private static void OnSettingChanged(object sender, SettingChangedEventArgs e)
        {
            if (e.ChangedSetting.Definition.Section != Section) return;
            RefreshRows();
            if (!storing) Changed?.Invoke();
        }
        // An entry of the retired section, removed from the file's orphans; "" when there is none.
        private static string Take(Dictionary<ConfigDefinition, string> orphans, string key)
        {
            var definition = new ConfigDefinition(RetiredSection, key);
            string value;
            if (!orphans.TryGetValue(definition, out value)) return "";
            orphans.Remove(definition);
            return value ?? "";
        }
        private static ConfigurationManagerAttributes Row(string name, int order, string choices = null) => new ConfigurationManagerAttributes
        {
            DisplayName = name, DispName = name, DisplaySection = Section, Category = Section, SortOrder = order, ChoiceLabels = choices, Visible = true
        };
        // Position and size are listed only while the overlay is on, as in the Video tab.
        private static void RefreshRows()
        {
            bool on = shown != null && shown.Value;
            if (cornerRow != null) cornerRow.Visible = on;
            if (sizeRow != null) sizeRow.Visible = on;
            ModManagers.RequestFoARefresh();
        }
        // The Video row applied these values.
        internal static void Store(bool on, OverlayCorner where, float scale)
        {
            if (shown == null) return;
            storing = true;
            try { shown.Value = on; corner.Value = where; size.Value = Mathf.Clamp(scale, 0.5f, 2f); }
            finally { storing = false; }
            RefreshRows();
        }
        // Every frame, from the bridge's per-frame component.
        internal static void Tick(float now)
        {
            if (!Visible) { count = 0; next = 0; text = ""; return; }
            times[next] = Time.unscaledDeltaTime;
            next = (next + 1) % times.Length;
            if (count < times.Length) count++;
            if (now < nextText) return;
            nextText = now + 0.25f;
            text = Build();
        }
        // From OnGUI.
        internal static void Draw()
        {
            if (!Visible || text.Length == 0 || Event.current == null || Event.current.type != EventType.Repaint) return;
            float scale = Size * Mathf.Max(Screen.height, 480) / 1080f;
            int font = Mathf.Max(8, Mathf.RoundToInt(15f * scale));
            if (style == null || styleFont != font)
            {
                style = new GUIStyle(GUI.skin.label) { fontSize = font, richText = true, wordWrap = false, alignment = TextAnchor.UpperLeft };
                style.normal.textColor = Color.white;
                styleFont = font;
            }
            var content = new GUIContent(text);
            var draw = style;
            Vector2 measured = style.CalcSize(content);
            // A long line wraps at under half the screen's width instead of running across it.
            float widest = Mathf.Max(200f, Screen.width * 0.45f);
            if (measured.x > widest)
            {
                if (wrapped == null || wrapped.fontSize != font) { wrapped = new GUIStyle(style) { wordWrap = true }; wrapped.normal.textColor = Color.white; }
                draw = wrapped;
                measured = new Vector2(widest, wrapped.CalcHeight(content, widest));
            }
            float pad = 8f * scale, margin = 12f * scale, width = measured.x + 2f * pad, height = measured.y + 2f * pad;
            var where = corner.Value;
            float x = where == OverlayCorner.TopRight || where == OverlayCorner.BottomRight ? Screen.width - width - margin : margin;
            float y = where == OverlayCorner.BottomLeft || where == OverlayCorner.BottomRight ? Screen.height - height - margin : margin;
            var previous = GUI.color;
            GUI.color = new Color(0f, 0f, 0f, 0.7f);
            GUI.DrawTexture(new Rect(x, y, width, height), Texture2D.whiteTexture);
            GUI.color = previous;
            GUI.Label(new Rect(x + pad, y + pad, measured.x, measured.y), content, draw);
        }
        private static string Build()
        {
            var sb = new StringBuilder();
            // What the mods found wrong, first: a screenshot of the overlay shows it.
            var problems = Notices.Current(NoticeLevel.Warning);
            if (problems.Count > 0)
            {
                Title(sb, "Problems (details in the Video settings)");
                foreach (var n in problems) sb.Append(n.Source).Append(": ").Append(n.Label).Append('\n');
            }
            Title(sb, "Performance");
            foreach (var line in PerformanceLines()) sb.Append(line).Append('\n');
            Part[] snapshot;
            lock (parts) snapshot = parts.ToArray();
            foreach (var part in snapshot)
            {
                Title(sb, part.Title);
                try { foreach (var line in part.Lines() ?? Enumerable.Empty<string>()) sb.Append(line).Append('\n'); }
                catch (Exception ex) { sb.Append("Unavailable (").Append(ex.GetType().Name).Append(")\n"); }
            }
            return sb.ToString().TrimEnd('\n');
        }
        private static void Title(StringBuilder sb, string title)
        {
            if (sb.Length > 0) sb.Append('\n');
            sb.Append("<b>").Append(title).Append("</b>\n");
        }
        private static IEnumerable<string> PerformanceLines()
        {
            var lines = new List<string>();
            // The last second's frames for the rate and the average; all 600 for the slowest 1%.
            float second = 0f; int frames = 0;
            for (int i = 1; i <= count && second < 1f; i++) { second += times[(next - i + times.Length) % times.Length]; frames++; }
            RenderedFps = second > 0f ? frames / second : 0f;
            string rate = second > 0f ? RenderedFps.ToString("0") + " fps · " + (1000f * second / frames).ToString("0.0") + " ms" : "measuring...";
            if (count >= 100)
            {
                var sorted = new float[count];
                for (int i = 0; i < count; i++) sorted[i] = times[(next - 1 - i + times.Length) % times.Length];
                Array.Sort(sorted);
                float slow = sorted[Mathf.Min(count - 1, (int)(count * 0.99f))];
                if (slow > 0f) rate += " · 1% low " + (1f / slow).ToString("0") + " fps (" + (1000f * slow).ToString("0.0") + " ms)";
            }
            lines.Add("Rendered " + rate);
            int width = Screen.width, height = Screen.height;
            string resolution = width + "x" + height;
            try
            {
                var render = DynamicResolutionHandler.instance.GetScaledSize(new Vector2Int(width, height));
                if (render.x > 0 && render.y > 0 && (render.x != width || render.y != height))
                    resolution = render.x + "x" + render.y + " → " + resolution + " (" + (100f * render.x / width).ToString("0.#") + "%)";
            }
            catch { } // no HDRP dynamic resolution: the output size alone
            lines.Add("Resolution " + resolution);
            int vsync = QualitySettings.vSyncCount, cap = Application.targetFrameRate;
            lines.Add("VSync " + (vsync <= 0 ? "off" : vsync == 1 ? "on" : "on, every " + vsync + " refreshes") + " · frame cap " + (cap > 0 ? cap + " fps" : "none"));
            if (GraphicsBridge.TryReadPerformance(out var p) && p.Valid != 0)
            {
                var use = new List<string>();
                if ((p.Valid & 1u) != 0) use.Add("CPU " + p.CpuProcess.ToString("0") + "% (system " + p.CpuSystem.ToString("0") + "%)");
                if ((p.Valid & 2u) != 0) use.Add("GPU " + p.GpuProcess.ToString("0") + "% (system " + p.GpuSystem.ToString("0") + "%)");
                if (use.Count > 0) lines.Add(string.Join(" · ", use.ToArray()));
                if ((p.Valid & 4u) != 0)
                    lines.Add("VRAM " + Gigabytes(p.VramProcess) + " GB of " + Gigabytes(p.VramBudget) + " GB budget (" + Gigabytes(p.VramTotal) + " GB on the GPU)");
            }
            else lines.Add("CPU, GPU and VRAM: waiting for the native bridge");
            return lines;
        }
        private static string Gigabytes(ulong bytes) => (bytes / (1024.0 * 1024.0 * 1024.0)).ToString("0.0");
    }
}
