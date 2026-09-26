// SPDX-License-Identifier: MIT
// The lights rays can reach: HDRP's sun (the directional light HDRP itself treats as the main light),
// and the point and spot lights near the camera, in HDRP's units (lux for the sun, candela for punctual
// lights: Light.intensity holds the native unit in HDRP 17). Each light keeps a stable slot, so ReSTIR's
// reuse across frames always means the same light; a slot whose light is gone goes dark until reused.
// Only what HDRP lets light the world: lights with Affect Diffuse off (specular glints) and, with light
// layers on, lights limited to layers other than the world's default one are left out, and each light
// fades with its distance from the camera as HDRP fades it. And only the lights that stand for something that
// emits light (IsPlacedLight): the ones placed to fake light are left out.
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class SceneLights
    {
        internal struct SunLight { internal bool Valid; internal Vector3 Direction, Color; internal float AngularRadius; }
        internal static SunLight Sun;
        private const int MaxLights = 1024;
        private const float ScanInterval = 1f;
        private static readonly Dictionary<int, int> slots = new Dictionary<int, int>();
        private static readonly List<Light> slotLights = new List<Light>();
        private static readonly List<HDAdditionalLightData> slotData = new List<HDAdditionalLightData>();
        private static readonly Stack<int> freeSlots = new Stack<int>();
        private static NativeLight[] buffer = new NativeLight[64];
        private static float nextScan;
        private static bool radiiLogged;
        private static int placedLeftOut;
        private static readonly HashSet<string> placedLogged = new HashSet<string>();
        internal static int PlacedLeftOut => placedLeftOut;
        // Only lights that stand for something that emits light are traced: a flame, a candle, a lamp, a magic effect
        // (the light or one of its parents, up to five levels, names one: Light_Int_Brazier_01_Lit/PointLight_main,
        // VFX_Fire/PointLight_Fire). The rest were placed to fake the light raster rendering lacks: fill lights
        // (PointLight_Ambience, the "LIT" groups of plain "Point Light"s, unnamed lights up to 25 m across), the light
        // carried with the player (HeroLight), dialogue lights, light for volumetric fog. The bounced light is traced,
        // so they would light the scene a second time. A light named as a fill is left out even inside a lamp's prefab
        // (the PointLight_Ambience beside a brazier's own light).
        private static readonly string[] PlacedNames = { "Ambien", "Fill", "Bounce", "Fake", "Indirect", "HeroLight", "Volumetric" };
        private static readonly string[] SourceNames = { "Fire", "Flame", "Torch", "Candle", "Candela", "Chandelier", "Brazier", "Hearth", "Forge", "Furnace",
            "Lamp", "Lantern", "Sconce", "Ember", "Lava", "VFX", "Magic", "Spell", "Projectile", "Portal", "Explosion" };
        // Lights the player added on purpose: Conversation Lighting Overhaul's portrait light in dialogue.
        private static readonly string[] KeptNames = { "TGConversationLightingOverhaul" };
        private static readonly Dictionary<int, bool> placedCache = new Dictionary<int, bool>();
        private static bool Names(string name, string[] words)
        {
            foreach (var w in words) if (name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) return true;
            return false;
        }
        internal static bool IsPlacedLight(Light light)
        {
            int id = light.GetInstanceID();
            if (placedCache.TryGetValue(id, out bool placed)) return placed;
            if (Names(light.name, KeptNames)) placed = false;
            else if (Names(light.name, PlacedNames)) placed = true;
            else
            {
                placed = true;
                var t = light.transform;
                for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)
                    if (Names(t.name, SourceNames)) { placed = false; break; }
            }
            placedCache[id] = placed;
            return placed;
        }
        // The scene's lights grouped by name (digits and "(Clone)" dropped), marked where IsPlacedLight leaves them out:
        // every name the first time it appears in a session, to catch a real source it takes for a placed light.
        private static readonly HashSet<string> inventoried = new HashSet<string>();
        private static bool effectsLogged;
        private static void LogInventory(IEnumerable<Light> lights)
        {
            var groups = new Dictionary<string, (int count, int shadowed, int fill, float minRadius, float maxRadius, float maxRange, string parent)>();
            foreach (var light in lights)
            {
                if (light == null || light.type == LightType.Directional) continue;
                string key = System.Text.RegularExpressions.Regex.Replace(light.name.Replace("(Clone)", ""), @"[\d_ ]+$", "").Trim();
                if (inventoried.Contains(key)) continue;
                var hd = light.GetComponent<HDAdditionalLightData>();
                float radius = hd != null ? hd.shapeRadius : 0f;
                groups.TryGetValue(key, out var g);
                if (g.count == 0) { g.minRadius = radius; g.parent = light.transform.parent != null ? light.transform.parent.name : "(none)"; }
                g.count++; if (light.shadows != LightShadows.None) g.shadowed++; if (IsPlacedLight(light)) g.fill++;
                g.minRadius = Mathf.Min(g.minRadius, radius); g.maxRadius = Mathf.Max(g.maxRadius, radius); g.maxRange = Mathf.Max(g.maxRange, light.range);
                groups[key] = g;
            }
            if (groups.Count == 0) return;
            foreach (var key in groups.Keys) inventoried.Add(key);
            var list = new List<KeyValuePair<string, (int count, int shadowed, int fill, float minRadius, float maxRadius, float maxRange, string parent)>>(groups);
            list.Sort((a, b) => b.Value.count.CompareTo(a.Value.count));
            var sb = new System.Text.StringBuilder("Lights by name, new this session (count, with shadows, radius, range, a parent; * left out: placed, not a flame, lamp or effect):");
            for (int i = 0; i < list.Count && i < 40; ++i)
            {
                var g = list[i].Value;
                sb.Append("\n  " + (g.fill > 0 ? "* " : "  ") + "\"" + list[i].Key + "\" x" + g.count + ", shadows " + g.shadowed + ", radius " + g.minRadius.ToString("0.###") +
                    (g.maxRadius > g.minRadius ? "-" + g.maxRadius.ToString("0.###") : "") + " m, range up to " + g.maxRange.ToString("0.#") + " m, under \"" + g.parent + "\"");
            }
            if (list.Count > 40) sb.Append("\n  ... " + (list.Count - 40) + " more names");
            Session.Log.LogInfo(sb.ToString());
        }
        internal static int Count { get; private set; }
        internal static int LeftOut { get; private set; } // in range, but not lighting the world in HDRP either

        // HDRP's light layers: with them off every light lights every object. The world is on the default layer.
        private static bool LightsWorld(HDAdditionalLightData hd, bool lightLayers)
        {
            if (hd == null) return true;
            if (!hd.affectDiffuse) return false;
            if (!lightLayers) return true;
            uint mask = (uint)hd.lightlayersMask;
            return (int)mask < 0 || (mask & 1u) != 0;
        }
        // HDRP's distance fade (HDUtils.ComputeLinearDistanceFade): full to 90% of the fade distance, gone at it.
        private static float Fade(float distance, float fadeDistance)
        {
            if (!(fadeDistance > 0f)) return 1f;
            float start = 0.9f * fadeDistance;
            return 1f - Mathf.Clamp01((distance - start) / (fadeDistance - start));
        }

        internal static void Reset() { slots.Clear(); slotLights.Clear(); slotData.Clear(); freeSlots.Clear(); placedCache.Clear(); Count = 0; nextScan = 0; Sun = default; }

        private static Vector3 LightColor(Light light, HDAdditionalLightData hd)
        {
            var c = light.color.linear * light.intensity;
            if (light.useColorTemperature) c *= Mathf.CorrelatedColorTemperatureToRGB(light.colorTemperature);
            float dimmer = hd != null ? hd.lightDimmer : 1f;
            return new Vector3(c.r, c.g, c.b) * dimmer;
        }

        internal static void Tick(float now, Vector3 camera, bool wantLights, bool lightLayers)
        {
            UpdateSun();
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            float radius = Session.Settings.SceneRadius.Value;
            if (now >= nextScan)
            {
                nextScan = now + ScanInterval;
                // Lights gone or disabled give their slot back; lights in range take one.
                var alive = new HashSet<int>();
                int leftOut = 0;
                placedLeftOut = 0;
                var all = wantLights ? Object.FindObjectsByType<Light>(FindObjectsSortMode.None) : new Light[0];
                LogInventory(all);
                if (!effectsLogged && all.Length >= 20) { effectsLogged = true; LightEffectsInventory.Log(); }
                if (wantLights)
                    foreach (var light in all)
                    {
                        if (light == null || !light.isActiveAndEnabled || light.intensity <= 0f) continue;
                        if (light.type != LightType.Point && light.type != LightType.Spot) continue;
                        float reach = radius + light.range;
                        if ((light.transform.position - camera).sqrMagnitude > reach * reach) continue;
                        int id = light.GetInstanceID();
                        HDAdditionalLightData data = slots.TryGetValue(id, out int existing) ? slotData[existing] : light.GetComponent<HDAdditionalLightData>();
                        if (!LightsWorld(data, lightLayers)) { ++leftOut; continue; }
                        // Lights placed to fake light (IsPlacedLight) would light the scene a second time.
                        if (IsPlacedLight(light))
                        {
                            ++placedLeftOut;
                            if (placedLogged.Add(light.name) && placedLogged.Count <= 8)
                                Session.Log.LogInfo("Lights: \"" + light.name + "\" left out (placed to fake light, not a flame, lamp or effect)");
                            continue;
                        }
                        alive.Add(id);
                        if (slots.ContainsKey(id)) continue;
                        int slot;
                        if (freeSlots.Count > 0) slot = freeSlots.Pop();
                        else if (slotLights.Count < MaxLights) { slot = slotLights.Count; slotLights.Add(null); slotData.Add(null); }
                        else continue;
                        slots[id] = slot; slotLights[slot] = light; slotData[slot] = data;
                    }
                LeftOut = leftOut;
                var gone = new List<int>();
                foreach (var entry in slots) if (!alive.Contains(entry.Key)) gone.Add(entry.Key);
                foreach (int id in gone) { int slot = slots[id]; slots.Remove(id); slotLights[slot] = null; slotData[slot] = null; freeSlots.Push(slot); }
                // Once a session with a few dozen lights: the sizes HDRP gives them, which set how soft their shadows are.
                if (!radiiLogged && slots.Count >= 20)
                {
                    radiiLogged = true;
                    var radii = new List<float>();
                    foreach (var d in slotData) if (d != null) radii.Add(d.shapeRadius);
                    if (radii.Count > 0)
                    {
                        radii.Sort();
                        Session.Log.LogInfo("Lights: HDRP radius " + radii[0].ToString("0.000") + " to " + radii[radii.Count - 1].ToString("0.000") + " m, median " +
                            radii[radii.Count / 2].ToString("0.000") + " m, over " + radii.Count + " lights (traced at least 0.02 m; their shadows soften with it)");
                    }
                }
            }
            int count = slotLights.Count;
            if (buffer.Length < count) buffer = new NativeLight[Mathf.NextPowerOfTwo(count)];
            for (int i = 0; i < count; i++)
            {
                var light = slotLights[i];
                var n = new NativeLight { Id = (uint)i };
                if (light != null && light.isActiveAndEnabled)
                {
                    var hd = slotData[i];
                    var t = light.transform;
                    var p = t.position; var f = t.forward;
                    var color = LightColor(light, hd) * (hd != null ? Fade(Vector3.Distance(p, camera), hd.fadeDistance) : 1f);
                    n.PositionX = p.x; n.PositionY = p.y; n.PositionZ = p.z;
                    n.ColorR = color.x; n.ColorG = color.y; n.ColorB = color.z;
                    n.Range = light.range; n.DirectionX = f.x; n.DirectionY = f.y; n.DirectionZ = f.z;
                    n.Radius = hd != null ? Mathf.Max(hd.shapeRadius, 0.02f) : 0.05f;
                    if (light.type == LightType.Spot)
                    {
                        n.Type = Tgpt.LightSpot;
                        // HDRP's spot cone (HDGpuLightsBuilder): smooth between the inner and outer angles.
                        float inner = hd != null ? hd.innerSpotPercent / 100f : 0f;
                        float cosOuter = Mathf.Clamp01(Mathf.Cos(light.spotAngle * 0.5f * Mathf.Deg2Rad));
                        float cosInner = Mathf.Clamp01(Mathf.Cos(light.spotAngle * 0.5f * inner * Mathf.Deg2Rad));
                        float span = Mathf.Max(0.0001f, cosInner - cosOuter);
                        n.AngleScale = 1f / span; n.AngleOffset = -cosOuter * n.AngleScale;
                    }
                    else { n.Type = Tgpt.LightPoint; n.AngleScale = 0f; n.AngleOffset = 1f; }
                }
                buffer[i] = n;
            }
            Count = slots.Count;
            tracer.SetLights(buffer, (uint)count);
        }

        private static void UpdateSun()
        {
            Sun = default;
            var pipeline = RenderPipelineManager.currentPipeline as HDRenderPipeline;
            var sun = pipeline != null ? pipeline.GetMainLight() : null;
            if (sun == null || !sun.isActiveAndEnabled || sun.type != LightType.Directional) return;
            var hd = sun.GetComponent<HDAdditionalLightData>();
            var color = LightColor(sun, hd);
            if (color.x + color.y + color.z <= 0f) return;
            Sun.Valid = true;
            Sun.Direction = -sun.transform.forward;
            Sun.Color = color;
            Sun.AngularRadius = hd != null ? Mathf.Clamp(hd.angularDiameter * 0.5f * Mathf.Deg2Rad, 0.0005f, 0.1f) : 0.0047f;
        }
    }
}
