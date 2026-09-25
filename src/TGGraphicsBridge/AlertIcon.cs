// SPDX-License-Identifier: MIT
// The alert icon after the name of a Video row with a problem (RowAlerts). The game's rows have no icon
// of their own, only text, so this is a small image the bridge adds under the row's name label and
// keeps just after the text: an amber warning triangle drawn once into a texture, so it looks the same
// whatever font or language the game uses. Its own CanvasGroup ignores the row's, so a greyed-out row
// still shows it at full strength. The game reuses row views for other options, so every icon is
// taken off again as soon as its row no longer has a problem or shows another option.
using System.Collections.Generic;
using System.Linq;
using Awaken.TG.Main.Settings.Options.Views;
using TMPro;
using UnityEngine;
using UnityEngine.UI;
namespace TGGraphicsBridge
{
    internal static class AlertIcon
    {
        private const string Name = "TGAlertIcon";
        private sealed class Placed { internal GameObject Icon; internal VFocusableSetting View; internal TextMeshProUGUI Label; internal object Option; }
        private static readonly List<Placed> placed = new List<Placed>();
        private static Sprite sprite;
        // Icons on exactly these rows; every other row loses its icon.
        internal static void Show(List<VFocusableSetting> views)
        {
            for (int i = placed.Count - 1; i >= 0; i--)
            {
                var p = placed[i];
                bool keep = p.Icon != null && p.View != null && p.Label != null &&
                            views.Any(v => VideoTab.Same(v, p.View)) && Equals(VideoTab.Identity(p.View.GenericOption), p.Option);
                if (!keep) { Remove(p); placed.RemoveAt(i); }
            }
            foreach (var view in views)
            {
                var p = placed.FirstOrDefault(x => VideoTab.Same(x.View, view));
                if (p == null)
                {
                    p = Create(view);
                    if (p == null) continue;
                    placed.Add(p);
                }
                Place(p);
            }
        }
        internal static void Clear()
        {
            foreach (var p in placed) Remove(p);
            placed.Clear();
        }
        private static void Remove(Placed p)
        {
            if (p.Icon != null) Object.Destroy(p.Icon);
        }
        // Under the row's name label: the text that reads the option's name, else the first label.
        private static Placed Create(VFocusableSetting view)
        {
            var option = view.GenericOption;
            string name = option?.DisplayName?.Trim();
            TextMeshProUGUI label = null;
            foreach (var text in view.GetComponentsInChildren<TextMeshProUGUI>(true))
            {
                if (label == null) label = text;
                if (!string.IsNullOrEmpty(name) && text.text != null && text.text.Trim() == name) { label = text; break; }
            }
            if (label == null) return null;
            var icon = new GameObject(Name);
            icon.AddComponent<RectTransform>();
            icon.transform.SetParent(label.transform, false);
            var group = icon.AddComponent<CanvasGroup>();
            group.ignoreParentGroups = true; group.interactable = false; group.blocksRaycasts = false;
            var image = icon.AddComponent<Image>();
            image.sprite = Sprite();
            image.raycastTarget = false;
            image.preserveAspect = true;
            return new Placed { Icon = icon, View = view, Label = label, Option = VideoTab.Identity(option) };
        }
        // Just after the name, at the text's height; followed every tick, since the text can change size.
        private static void Place(Placed p)
        {
            var rect = p.Icon.GetComponent<RectTransform>();
            float size = Mathf.Max(8f, p.Label.fontSize * 0.9f);
            rect.anchorMin = new Vector2(0f, 0.5f); rect.anchorMax = new Vector2(0f, 0.5f); rect.pivot = new Vector2(0f, 0.5f);
            rect.sizeDelta = new Vector2(size, size);
            rect.anchoredPosition = new Vector2(p.Label.preferredWidth + size * 0.45f, 0f);
        }
        // An amber triangle with a dark exclamation mark, 64 pixels, drawn with 4x4 samples a pixel.
        private static Sprite Sprite()
        {
            if (sprite != null) return sprite;
            const int size = 64, samples = 4;
            var amber = new Color(0.95f, 0.67f, 0.2f);
            var mark = new Color(0.11f, 0.086f, 0.063f);
            var pixels = new Color32[size * size];
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    int inside = 0, marked = 0;
                    for (int sy = 0; sy < samples; sy++)
                        for (int sx = 0; sx < samples; sx++)
                        {
                            float u = (x + (sx + 0.5f) / samples) / size, v = (y + (sy + 0.5f) / samples) / size;
                            if (!Triangle(u, v)) continue;
                            ++inside;
                            if (Exclamation(u, v)) ++marked;
                        }
                    float coverage = inside / (float)(samples * samples);
                    var color = inside > 0 ? Color.Lerp(amber, mark, marked / (float)inside) : Color.clear;
                    color.a = coverage;
                    pixels[y * size + x] = color;
                }
            var texture = new Texture2D(size, size, TextureFormat.RGBA32, false);
            texture.filterMode = FilterMode.Bilinear;
            texture.wrapMode = TextureWrapMode.Clamp;
            texture.hideFlags = HideFlags.HideAndDontSave; // never unloaded with the game's unused assets
            texture.SetPixels32(pixels);
            texture.Apply(false, true);
            sprite = UnityEngine.Sprite.Create(texture, new Rect(0f, 0f, size, size), new Vector2(0.5f, 0.5f), 100f);
            sprite.hideFlags = HideFlags.HideAndDontSave;
            return sprite;
        }
        // Apex at the top, base at the bottom, in 0..1 texture space with y up.
        private static bool Triangle(float u, float v)
        {
            float ax = 0.5f, ay = 0.94f, bx = 0.04f, by = 0.08f, cx = 0.96f, cy = 0.08f;
            float d1 = (u - bx) * (ay - by) - (ax - bx) * (v - by);
            float d2 = (u - cx) * (by - cy) - (bx - cx) * (v - cy);
            float d3 = (u - ax) * (cy - ay) - (cx - ax) * (v - ay);
            bool negative = d1 < 0 || d2 < 0 || d3 < 0, positive = d1 > 0 || d2 > 0 || d3 > 0;
            return !(negative && positive);
        }
        private static bool Exclamation(float u, float v)
        {
            float dx = u - 0.5f;
            if (v >= 0.36f && v <= 0.72f && Mathf.Abs(dx) <= 0.055f - (0.72f - v) * 0.03f) return true; // the bar, narrowing downwards
            float dy = v - 0.22f;
            return dx * dx + dy * dy <= 0.06f * 0.06f; // the dot
        }
    }
}
