// SPDX-License-Identifier: MIT
// Problems the mods found in this session, and short messages during play. A notice has a source (the
// mod it concerns, or the other software), a label of a few words and a full explanation with what to
// do about it. Problems and warnings show on the Video rows they concern (RowAlerts): an alert icon
// after the row's name and the explanation as its tooltip, and at the top of the performance overlay. A toast
// is a panel at the top of the screen for a few seconds: what the troubleshooting run is doing, or
// that frame generation isn't raising the framerate. Everything here is also written to the log once.
using System;
using System.Collections.Generic;
using System.Linq;
using UnityEngine;
namespace TGGraphicsBridge
{
    public enum NoticeLevel { Info = 0, Warning = 1, Problem = 2 }
    // The Video rows a notice concerns: the Frame Generation row, the game's upscaling row (Dynamic
    // Resolution, which TG Better Upscaling extends), or both.
    [Flags] public enum AlertRow { None = 0, FrameGeneration = 1, Upscaling = 2, NeuralRendering = 4 }
    // Three lengths of the same problem: Label is the row's value in the Video settings and must stay a
    // word or two (the game's value field is narrow); Summary is the row's tooltip, a sentence or two with
    // the fix (the game's tooltip is a narrow column that runs off the screen past a few lines); Detail is
    // the full explanation, for the log and the troubleshooting report.
    public sealed class Notice
    {
        public string Id, Source, Label, Summary, Detail;
        public NoticeLevel Level;
        public AlertRow Rows;
    }
    public static class Notices
    {
        private static readonly Dictionary<string, Notice> notices = new Dictionary<string, Notice>();
        private static int version;
        // Increases whenever the set of notices or one of them changes; the Video rows rebuild on it.
        public static int Version { get { lock (notices) return version; } }
        // Sets or replaces a notice. Logged when it is new or has changed.
        public static void Set(string id, NoticeLevel level, string source, string label, string detail) => Set(id, level, source, label, detail, detail);
        public static void Set(string id, NoticeLevel level, string source, string label, string summary, string detail)
        {
            if (string.IsNullOrEmpty(id)) return;
            bool changed;
            lock (notices)
            {
                changed = !notices.TryGetValue(id, out var old) || old.Level != level || old.Source != source || old.Label != label || old.Summary != summary || old.Detail != detail;
                if (changed) { notices[id] = new Notice { Id = id, Level = level, Source = source, Label = label, Summary = summary, Detail = detail, Rows = RowsOf(id, source) }; ++version; }
            }
            if (!changed) return;
            string text = source + ": " + label + ". " + detail;
            if (level == NoticeLevel.Problem) BridgeLog.Log?.LogWarning("Problem: " + text);
            else if (level == NoticeLevel.Warning) BridgeLog.Log?.LogWarning("Warning: " + text);
            else BridgeLog.Log?.LogInfo("Note: " + text);
        }
        // A mod's own notices go on its row; other software on the rows of what it interferes with.
        private static AlertRow RowsOf(string id, string source)
        {
            if (source == "TG Frame Generation") return AlertRow.FrameGeneration;
            if (source == "TG Better Upscaling") return AlertRow.Upscaling;
            if (source == "TG Neural Rendering") return AlertRow.NeuralRendering;
            if (source == "TG Graphics Bridge") return AlertRow.FrameGeneration | AlertRow.Upscaling | AlertRow.NeuralRendering;
            switch (id)
            {
                case "compat.optiscaler": case "compat.dlssenabler": case "compat.specialk": case "compat.vcruntime":
                    return AlertRow.FrameGeneration | AlertRow.Upscaling;
                default: return AlertRow.FrameGeneration;
            }
        }
        // The problems and warnings that concern a row, most serious first.
        public static List<Notice> For(AlertRow row, NoticeLevel atLeast = NoticeLevel.Warning) => Current(atLeast).Where(n => (n.Rows & row) != 0).ToList();
        public static void Clear(string id)
        {
            lock (notices) { if (notices.Remove(id)) ++version; else return; }
            BridgeLog.Log?.LogInfo("Cleared: " + id + ".");
        }
        public static bool Has(string id) { lock (notices) return notices.ContainsKey(id); }
        // Most serious first, then by source.
        public static List<Notice> Current(NoticeLevel atLeast = NoticeLevel.Info)
        {
            lock (notices)
                return notices.Values.Where(n => n.Level >= atLeast).OrderByDescending(n => n.Level).ThenBy(n => n.Source).ThenBy(n => n.Id).ToList();
        }
        // The explanations of one mod's notices, for its own tooltips.
        public static string DetailsFor(string source, NoticeLevel atLeast = NoticeLevel.Warning) =>
            string.Join(" ", Current(atLeast).Where(n => n.Source == source).Select(n => n.Detail).ToArray());
        // The most serious of one mod's notices, or null: short enough for its own rows' tooltips.
        public static Notice First(string source, NoticeLevel atLeast = NoticeLevel.Warning) => Current(atLeast).FirstOrDefault(n => n.Source == source);

        private sealed class Toast { internal string Title, Text; internal float Until; internal string Key; }
        private static readonly List<Toast> toasts = new List<Toast>();
        private static GUIStyle titleStyle, textStyle;
        private static int styleFont;
        // A panel at the top of the screen for this long. A toast with the same key replaces the last one.
        public static void Show(string key, string title, string text, float seconds = 12f)
        {
            float until = Time.unscaledTime + Mathf.Clamp(seconds, 1f, 600f);
            lock (toasts)
            {
                toasts.RemoveAll(t => t.Key == key);
                toasts.Add(new Toast { Key = key, Title = title ?? "", Text = text ?? "", Until = until });
            }
        }
        public static void Hide(string key) { lock (toasts) toasts.RemoveAll(t => t.Key == key); }
        // From OnGUI, after the performance overlay.
        internal static void Draw()
        {
            if (Event.current == null || Event.current.type != EventType.Repaint) return;
            Toast[] shown;
            float now = Time.unscaledTime;
            lock (toasts)
            {
                toasts.RemoveAll(t => t.Until <= now);
                if (toasts.Count == 0) return;
                shown = toasts.ToArray();
            }
            float scale = Mathf.Max(Screen.height, 480) / 1080f;
            int font = Mathf.Max(9, Mathf.RoundToInt(17f * scale));
            if (titleStyle == null || styleFont != font)
            {
                titleStyle = new GUIStyle(GUI.skin.label) { fontSize = font, fontStyle = FontStyle.Bold, richText = false, wordWrap = true, alignment = TextAnchor.UpperLeft };
                titleStyle.normal.textColor = new Color(1f, 0.85f, 0.45f);
                textStyle = new GUIStyle(GUI.skin.label) { fontSize = Mathf.Max(8, font - 2), richText = false, wordWrap = true, alignment = TextAnchor.UpperLeft };
                textStyle.normal.textColor = Color.white;
                styleFont = font;
            }
            float width = Mathf.Min(Screen.width - 40f * scale, 760f * scale), pad = 12f * scale, y = 24f * scale;
            float inner = width - 2f * pad;
            foreach (var toast in shown)
            {
                var title = new GUIContent(toast.Title);
                var text = new GUIContent(toast.Text);
                float titleHeight = toast.Title.Length > 0 ? titleStyle.CalcHeight(title, inner) : 0f;
                float textHeight = toast.Text.Length > 0 ? textStyle.CalcHeight(text, inner) : 0f;
                float height = titleHeight + textHeight + 2f * pad + (titleHeight > 0f && textHeight > 0f ? 4f * scale : 0f);
                float x = (Screen.width - width) * 0.5f;
                var previous = GUI.color;
                GUI.color = new Color(0f, 0f, 0f, 0.78f);
                GUI.DrawTexture(new Rect(x, y, width, height), Texture2D.whiteTexture);
                GUI.color = previous;
                if (titleHeight > 0f) GUI.Label(new Rect(x + pad, y + pad, inner, titleHeight), title, titleStyle);
                if (textHeight > 0f) GUI.Label(new Rect(x + pad, y + pad + titleHeight + (titleHeight > 0f ? 4f * scale : 0f), inner, textHeight), text, textStyle);
                y += height + 8f * scale;
            }
        }
    }
}
