// SPDX-License-Identifier: MIT
// The lights rays can reach: HDRP's sun (the directional light HDRP itself treats as the main light),
// and the point and spot lights near the camera, in HDRP's units (lux for the sun, candela for punctual
// lights: Light.intensity holds the native unit in HDRP 17). Each light keeps a stable slot, so ReSTIR's
// reuse across frames always means the same light; a slot whose light is gone goes dark until reused.
// Only what HDRP lets light the world: lights with Affect Diffuse off (specular glints) and, with light
// layers on, lights limited to layers other than the world's default one are left out, and each light
// fades with its distance from the camera as HDRP fades it. And only the lights that stand for something that
// emits light (IsPlacedLight): the ones placed to fake light are left out. Flames and lamps light as far as a physical
// light does (Reach), not to HDRP's range; an effect's lights (a portal, a spell) only as far as HDRP's range, as the game
// draws them.
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing
{
    internal static class SceneLights
    {
        // HdrpShadows: HDRP renders the sun's shadow cascades (with the camera's shadow maps on, HdrpPasses.ShadowMaps).
        internal struct SunLight { internal bool Valid, HdrpShadows; internal Vector3 Direction, Color; internal float AngularRadius; }
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
        // VFX_Fire/PointLight_Fire), whatever the light itself is called. This game names a prop's own room light
        // "PointLight_Ambience" as often as "PointLight_Cookies" (candlesticks, wall candelabras, torches): a candlestick
        // whose only light is its PointLight_Ambience lit nothing while its neighbour's PointLight_Cookies_01 lit the
        // room. The rest were placed to fake the light raster rendering lacks: fill lights outside any source prop (the
        // "Lights" and "LIT" groups of plain "Point Light"s, PointLight_Ambience on its own, unnamed lights up to 25 m
        // across), the light carried with the player (HeroLight), light for volumetric fog. The bounced light is traced,
        // so they would light the scene a second time.
        private static readonly string[] AlwaysPlacedNames = { "HeroLight", "Volumetric", "Fake" };
        private static readonly string[] SourceNames = { "Fire", "Flame", "Torch", "Candle", "Candela", "Chandelier", "Brazier", "Hearth", "Forge", "Furnace",
            "Lamp", "Lantern", "Sconce", "Ember", "Lava", "VFX", "Magic", "Spell", "Projectile", "Portal", "Explosion" };
        // The sources above that are flames or lamps; a kept light with none of them in its name or its parents' is an effect's.
        private static readonly string[] FlameNames = { "Fire", "Flame", "Torch", "Candle", "Candela", "Chandelier", "Brazier", "Hearth", "Forge", "Furnace",
            "Lamp", "Lantern", "Sconce", "Ember", "Lava" };
        // Lights the player added on purpose: Conversation Lighting Overhaul's portrait light in dialogue.
        private static readonly string[] KeptNames = { "TGConversationLightingOverhaul" };
        // A light's classification, kept while its parent stays the same: the game may attach a prop's light to the prop
        // after the light first shows up, and a candle taken for a placed light then would stay dark and untraced.
        private static readonly Dictionary<int, (bool placed, bool effect, int parent)> placedCache = new Dictionary<int, (bool, bool, int)>();
        private static bool Names(string name, string[] words)
        {
            foreach (var w in words) if (name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) return true;
            return false;
        }
        internal static bool IsPlacedLight(Light light) => Classify(light).placed;
        // A kept light an effect stands for (a portal, a spell): no flame or lamp in its name or its parents'.
        internal static bool IsEffectLight(Light light) => Classify(light).effect;
        private static (bool placed, bool effect, int parent) Classify(Light light)
        {
            int id = light.GetInstanceID();
            var parentTransform = light.transform.parent;
            int parent = parentTransform != null ? parentTransform.GetInstanceID() : 0;
            if (placedCache.TryGetValue(id, out var cached) && cached.parent == parent) return cached;
            bool placed, flame = false, kept = Names(light.name, KeptNames);
            if (kept) placed = false;
            else if (Names(light.name, AlwaysPlacedNames)) placed = true;
            else
            {
                placed = true;
                var t = light.transform;
                for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)
                {
                    if (Names(t.name, SourceNames)) placed = false;
                    if (Names(t.name, FlameNames)) { flame = true; break; }
                }
            }
            var result = (placed, !placed && !kept && !flame, parent);
            placedCache[id] = result;
            return result;
        }
        // Why IsPlacedLight keeps or leaves out a light, for the capture's report.
        internal static string Classification(Light light)
        {
            foreach (var w in KeptNames) if (light.name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) return "kept: added by a mod (\"" + w + "\")";
            foreach (var w in AlwaysPlacedNames) if (light.name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) return "left out: its name has \"" + w + "\"";
            string effect = IsEffectLight(light) ? " (an effect: lights as far as its HDRP range)" : "";
            var t = light.transform;
            for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)
                foreach (var w in SourceNames)
                    if (t.name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0)
                        return "kept: " + (depth == 0 ? "its name" : "parent \"" + t.name + "\"") + " has \"" + w + "\"" + effect;
            return "left out: neither it nor 5 parents name a flame, lamp or effect";
        }
        // Every Light in the loaded scenes, including those Object.FindObjectsByType leaves out (HideFlags.DontSave:
        // Conversation Lighting Overhaul's dialogue light, which was never traced), and no prefab assets.
        internal static List<Light> AllLights()
        {
            var list = new List<Light>();
            foreach (var l in Resources.FindObjectsOfTypeAll<Light>())
                if (l != null && l.gameObject.scene.IsValid() && l.gameObject.scene.isLoaded) list.Add(l);
            return list;
        }
        // How far a kept light is traced: as far as it still gives 50 lux (inverse square, as a physical light falls off),
        // or its HDRP range if that is farther, at most 40 m. HDRP's range is an artist's cut-off, and with the game's fill
        // lights left out it made a chandelier's light stop 2.7 m below it and a candle's 0.5 m from its flame. An effect's
        // light keeps HDRP's range: the game makes it a glow on the effect itself (a portal's two 131,072 cd lights stop
        // 0.6 m from it), and traced 40 m far they lit the whole alcove around the portal white.
        internal const float ReachLux = 50f, MaxReach = 40f;
        internal static float Reach(Light light, HDAdditionalLightData hd)
        {
            if (IsEffectLight(light)) return Mathf.Max(light.range, 0.01f);
            var c = LightColor(light, hd);
            float intensity = Mathf.Max(c.x, Mathf.Max(c.y, c.z));
            return Mathf.Max(light.range, Mathf.Min(Mathf.Sqrt(Mathf.Max(intensity, 0f) / ReachLux), MaxReach));
        }
        // An interior: no sun (the game turns it off indoors), or one of its "Interior" scenes loaded. Interiors float in
        // the sky above the world (the tavern at 500 m), so a ray that slips through a seam between walls and floor sees
        // the open sky, about 100 times brighter than the room: HdrpPasses gives rays no sky there.
        internal static bool Interior { get; private set; }
        private static bool InteriorScene()
        {
            for (int i = 0; i < UnityEngine.SceneManagement.SceneManager.sceneCount; i++)
                if (UnityEngine.SceneManagement.SceneManager.GetSceneAt(i).name.StartsWith("Interior", System.StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }
        // Every light this mod changes while tracing, with what the game gave it: a placed light is darkened (its light dimmer
        // 0, and no fog: Darken), and a traced one carries the traced light's gain, because HDRP still lights what it
        // shades itself (hair up close, glass, eyes) with the lights the tracer traces, at the game's exposure, while the
        // traced light has a gain on top (HdrpPasses.TracedGain): so what HDRP shades comes out as bright as the traced
        // surfaces around it. One record per light, taken the first time either change touches it, so whichever of the two a
        // light went through, and in whatever order (a light first taken for a placed one and then for a real source), path
        // tracing turning off gives it back exactly what the game had. The traced light itself is taken with that dimmer.
        private static readonly Dictionary<int, (HDAdditionalLightData data, float dimmer, bool volumetric)> originals = new Dictionary<int, (HDAdditionalLightData, float, bool)>();
        private static readonly HashSet<int> darkened = new HashSet<int>(), gained = new HashSet<int>();
        private static (HDAdditionalLightData data, float dimmer, bool volumetric) Original(int id, HDAdditionalLightData data)
        {
            if (!originals.TryGetValue(id, out var entry)) { entry = (data, data.lightDimmer, data.affectsVolumetric); originals[id] = entry; }
            return entry;
        }
        private static float OriginalDimmer(Light light, HDAdditionalLightData hd) =>
            hd == null ? 1f : light != null && originals.TryGetValue(light.GetInstanceID(), out var entry) ? entry.dimmer : hd.lightDimmer;
        private static void ApplyGain(float gain)
        {
            for (int i = 0; i < slotLights.Count; i++)
            {
                var light = slotLights[i]; var hd = slotData[i];
                if (light == null || hd == null) continue;
                int id = light.GetInstanceID();
                var entry = Original(id, hd);
                // Darkened before as a placed light: its fog comes back with it.
                if (darkened.Remove(id) && hd.affectsVolumetric != entry.volumetric) hd.affectsVolumetric = entry.volumetric;
                gained.Add(id);
                float want = entry.dimmer * gain;
                if (Mathf.Abs(hd.lightDimmer - want) > 0.01f * Mathf.Max(want, 1e-3f)) hd.lightDimmer = want;
            }
        }
        // A traced light that left the traced scene has the game's dimmers again (unless it is darkened as a placed one now).
        private static void RestoreGained(int id)
        {
            if (!gained.Remove(id) || darkened.Contains(id) || !originals.TryGetValue(id, out var entry)) return;
            try { if (entry.data != null) { entry.data.lightDimmer = entry.dimmer; entry.data.affectsVolumetric = entry.volumetric; } } catch { }
            originals.Remove(id);
        }
        private static void RestoreAll()
        {
            int count = originals.Count;
            foreach (var entry in originals.Values)
                try { if (entry.data != null) { entry.data.lightDimmer = entry.dimmer; entry.data.affectsVolumetric = entry.volumetric; } } catch { }
            originals.Clear(); darkened.Clear(); gained.Clear();
            if (count > 0) Session.Log.LogInfo("Lights: " + count + " light the world and the fog as the game has them again (path tracing off)");
        }
        // A light's cookie for the tracer: its index (TGPT_RegisterCookie), asked once per texture (GetNativeTexturePtr waits
        // for the render thread), -1 without one.
        private static readonly Dictionary<int, int> cookies = new Dictionary<int, int>();
        private static int CookieOf(Light light)
        {
            var texture = light.cookie;
            if (texture == null) return -1;
            int key = texture.GetInstanceID();
            if (cookies.TryGetValue(key, out int index)) return index;
            index = -1;
            try
            {
                var pointer = texture.GetNativeTexturePtr();
                if (pointer != System.IntPtr.Zero) index = Session.Tracer.RegisterCookie(unchecked((ulong)(uint)key), pointer);
                Session.Log.LogInfo("Lights: cookie \"" + texture.name + "\" (" + texture.dimension + ", " + texture.width + "px) " +
                    (index >= 0 ? "traced (masks \"" + light.name + "\" and every light with it)" : "not traced (" + index + ")"));
            }
            catch (System.Exception ex) { Session.Log.LogWarning("Lights: cookie \"" + texture.name + "\" could not be read: " + ex.Message); }
            cookies[key] = index;
            return index;
        }
        // The size a kept light is traced with: HDRP's radius sets only its highlights and soft shadows, not how its light
        // falls off (inverse square to 1 cm), but the tracer lights a surface from a sphere of that size, which caps the
        // light right beside it at I / r^2. This game gives candle and torch lights up to 1 m and more (soft edges for
        // lights that cast no shadows in the game): a flame is traced at most 30 cm across, a fire (a fireplace, a
        // brazier, a bonfire) 1 m, with its cookie shaping the light near it as HDRP's does.
        private static readonly string[] FireNames = { "Fire", "Hearth", "Forge", "Furnace", "Brazier", "Bonfire", "Bonefire", "Lava" };
        private static float TracedRadius(Light light, HDAdditionalLightData hd)
        {
            float radius = hd != null ? hd.shapeRadius : 0.05f;
            bool fire = false;
            var t = light.transform;
            for (int depth = 0; t != null && depth <= 5 && !fire; depth++, t = t.parent) fire = Names(t.name, FireNames);
            return Mathf.Clamp(radius, 0.02f, fire ? 1f : 0.3f);
        }
        // Whether HDRP shadows a light itself: it casts shadows, the camera renders shadow maps (the game's Shadows toggle turns
        // them off), and it is nearer than the distance HDRP fades its shadow out at. What HDRP shades itself (hair, eyes) takes
        // the traced shadows only of the lights it doesn't (TGPT_LIGHT_HDRP_SHADOWS): never a shadow twice.
        private static bool HdrpShadows(Light light, HDAdditionalLightData hd, float distance) =>
            HdrpPasses.ShadowMaps && light.shadows != LightShadows.None && (hd == null || (hd.shadowDimmer > 0f && distance < hd.shadowFadeDistance));
        internal static int SlotOf(Light light) => light != null && slots.TryGetValue(light.GetInstanceID(), out int slot) ? slot : -1;
        internal static string TracedColorText(int slot)
        {
            if (slot < 0 || slot >= buffer.Length) return "no data";
            var n = buffer[slot];
            return "color x intensity (" + n.ColorR.ToString("0.###") + ", " + n.ColorG.ToString("0.###") + ", " + n.ColorB.ToString("0.###") + ")";
        }
        internal static bool DarkenedDimmer(Light light, out float dimmer)
        {
            dimmer = 0f;
            if (light == null || !darkened.Contains(light.GetInstanceID()) || !originals.TryGetValue(light.GetInstanceID(), out var entry)) return false;
            dimmer = entry.dimmer;
            return true;
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

        internal static void Reset() { RestoreAll(); slots.Clear(); slotLights.Clear(); slotData.Clear(); freeSlots.Clear(); placedCache.Clear(); Count = 0; nextScan = 0; Sun = default; }

        // Placed lights light no surface while the traced light replaces HDRP's: HDRP still shades transparent surfaces, hair
        // and eyes itself, and those glowed with the fill lights the tracer leaves out (hair and eyes shone in a dark tavern).
        // Their light dimmer goes to 0; the fog they light is an interior's haze, the artist's as much as a fog volume is, and
        // stays as the game has it with the Fill Light Fog row on (off, they stop lighting it: affectsVolumetric; its fill
        // lights, reaching 46 m, filled the tavern with a red haze against the traced light then). Both come back when path
        // tracing stops (one record per light with the traced lights' gain: originals). Not the fog's dimmer: the game's light overrides (LightWithOverride, time of day and triggers)
        // remember it when one starts and write it back when it stops, so a fill light an override started on while it
        // was dark lit no fog again after path tracing turned off. Nothing in the game sets affectsVolumetric.
        private static bool darken;
        internal static void DarkenPlaced(bool on)
        {
            if (on == darken) return;
            darken = on;
            if (!on) RestoreAll();
            else nextScan = 0f; // the next scan finds them
        }
        private static void Darken(Light light, HDAdditionalLightData data)
        {
            if (!darken || data == null) return;
            int id = light.GetInstanceID();
            var entry = Original(id, data);
            darkened.Add(id); gained.Remove(id);
            if (data.lightDimmer != 0f) data.lightDimmer = 0f;
            // Every scan: the row takes effect within a scan's interval.
            var settings = Session.Settings;
            bool fog = entry.volumetric && (settings == null || settings.FillLightFog.Value);
            if (data.affectsVolumetric != fog) data.affectsVolumetric = fog;
        }

        private static Vector3 LightColor(Light light, HDAdditionalLightData hd)
        {
            var c = light.color.linear * light.intensity;
            if (light.useColorTemperature) c *= Mathf.CorrelatedColorTemperatureToRGB(light.colorTemperature);
            return new Vector3(c.r, c.g, c.b) * OriginalDimmer(light, hd);
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
                var all = wantLights ? AllLights() : new List<Light>();
                LogInventory(all);
                if (!effectsLogged && all.Count >= 20) { effectsLogged = true; LightEffectsInventory.Log(); }
                Interior = !Sun.Valid || InteriorScene();
                if (wantLights)
                    foreach (var light in all)
                    {
                        // A kept light (the dialogue light) holds its slot at intensity 0, so its traced light fades in with HDRP's.
                        if (light == null || !light.isActiveAndEnabled || (light.intensity <= 0f && !Names(light.name, KeptNames))) continue;
                        if (light.type != LightType.Point && light.type != LightType.Spot) continue;
                        float reach = radius + Reach(light, light.GetComponent<HDAdditionalLightData>());
                        if ((light.transform.position - camera).sqrMagnitude > reach * reach) continue;
                        int id = light.GetInstanceID();
                        HDAdditionalLightData data = slots.TryGetValue(id, out int existing) ? slotData[existing] : light.GetComponent<HDAdditionalLightData>();
                        if (!LightsWorld(data, lightLayers)) { ++leftOut; continue; }
                        // Lights placed to fake light (IsPlacedLight) would light the scene a second time.
                        if (IsPlacedLight(light))
                        {
                            Darken(light, data);
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
                foreach (int id in gone) { int slot = slots[id]; slots.Remove(id); slotLights[slot] = null; slotData[slot] = null; freeSlots.Push(slot); RestoreGained(id); }
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
                // Its identity (the Light's instance id, never 0): the tracer reuses a slot's light across frames only
                // while the slot holds the same light, not one that took over a freed slot.
                var n = new NativeLight { Id = light != null ? (uint)light.GetInstanceID() : 0u };
                if (light != null && light.isActiveAndEnabled)
                {
                    var hd = slotData[i];
                    var t = light.transform;
                    var p = t.position; var f = t.forward;
                    // Without HDRP's fade with the camera's distance: a lamp past it went dark and cast no shadow, then
                    // came in as the camera neared (shadows popping in); the light grid keeps distant lights cheap.
                    var color = LightColor(light, hd);
                    // A light a mod adds for a look it set at the game's exposure (the dialogue light) isn't brightened with the
                    // traced light where the view is dark: in a candle-lit room its spot on a face came out three times as bright
                    // as the mod makes it (the traced light x3.1 there).
                    if (Names(light.name, KeptNames)) color *= HdrpPasses.AutomaticGainInverse;
                    n.PositionX = p.x; n.PositionY = p.y; n.PositionZ = p.z;
                    n.ColorR = color.x; n.ColorG = color.y; n.ColorB = color.z;
                    n.Range = Reach(light, hd); n.DirectionX = f.x; n.DirectionY = f.y; n.DirectionZ = f.z;
                    n.Radius = TracedRadius(light, hd);
                    var up = t.up;
                    n.UpX = up.x; n.UpY = up.y; n.UpZ = up.z;
                    int cookie = CookieOf(light);
                    n.Flags = cookie >= 0 ? (uint)(cookie + 1) : 0u;
                    if (HdrpShadows(light, hd, (p - camera).magnitude)) n.Flags |= Tgpt.LightHdrpShadows;
                    // No highlight where HDRP gives it none (the game's candles and fill lights often have their specular off).
                    if (hd != null && !hd.affectSpecular) n.Flags |= Tgpt.LightNoSpecular;
                    if (light.type == LightType.Spot)
                    {
                        n.Type = Tgpt.LightSpot;
                        // HDRP's spot cone (HDGpuLightsBuilder): smooth between the inner and outer angles.
                        float inner = hd != null ? hd.innerSpotPercent / 100f : 0f;
                        float cosOuter = Mathf.Clamp01(Mathf.Cos(light.spotAngle * 0.5f * Mathf.Deg2Rad));
                        float cosInner = Mathf.Clamp01(Mathf.Cos(light.spotAngle * 0.5f * inner * Mathf.Deg2Rad));
                        float span = Mathf.Max(0.0001f, cosInner - cosOuter);
                        n.AngleScale = 1f / span; n.AngleOffset = -cosOuter * n.AngleScale;
                        n.CookieScale = 1f / Mathf.Max(Mathf.Tan(light.spotAngle * 0.5f * Mathf.Deg2Rad), 1e-3f);
                    }
                    else { n.Type = Tgpt.LightPoint; n.AngleScale = 0f; n.AngleOffset = 1f; }
                }
                buffer[i] = n;
            }
            Count = slots.Count;
            if (darken) ApplyGain(HdrpPasses.TracedGain);
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
            Sun.HdrpShadows = sun.shadows != LightShadows.None && (hd == null || hd.shadowDimmer > 0f);
            Sun.Direction = -sun.transform.forward;
            Sun.Color = color;
            Sun.AngularRadius = hd != null ? Mathf.Clamp(hd.angularDiameter * 0.5f * Mathf.Deg2Rad, 0.0005f, 0.1f) : 0.0047f;
        }
    }
}
