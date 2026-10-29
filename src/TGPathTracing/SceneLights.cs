// SPDX-License-Identifier: MIT
// The lights rays can reach: HDRP's sun (the directional light HDRP itself treats as the main light),
// and the point and spot lights near the camera, in HDRP's units (lux for the sun, candela for punctual
// lights: Light.intensity holds the native unit in HDRP 17). Each light keeps a stable slot, so ReSTIR's
// reuse across frames always means the same light; a slot whose light is gone goes dark until reused.
// Only what HDRP lets light the world: lights with Affect Diffuse off (specular glints) and, with light
// layers on, lights limited to layers other than the world's default one are left out, and each light
// fades with its distance from the camera as HDRP fades it. And only the lights that stand for something that
// emits light (IsPlacedLight): a flame, a lamp or an effect by its names or look, or a glowing thing at it (a crystal, a
// spell's particles, a glowing rune); the ones placed to fake light (fill, fake area and fake bounce lights) are left out.
// Flames and lamps light as far as a physical light does (Reach), not to HDRP's range; an effect's and a glowing thing's
// lights only as far as HDRP's range, as the game draws them. Each is traced at the size of what emits it (Size).
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
        // Slots for this many lights at most: past what the game loads at once (985 in the harness's busiest scenes, 2026-10-07),
        // under the tracer's own (runtime.hpp kMaxLights). Until PT-50 it was 1,024, which the busiest scenes nearly reached.
        private const int MaxLights = 65536;
        private const float ScanInterval = 1f;
        private static readonly Dictionary<int, int> slots = new Dictionary<int, int>();
        private static readonly List<Light> slotLights = new List<Light>();
        private static readonly List<HDAdditionalLightData> slotData = new List<HDAdditionalLightData>();
        private static readonly Stack<int> freeSlots = new Stack<int>();
        private static NativeLight[] buffer = new NativeLight[64];
        private static float nextScan;
        private static bool radiiLogged;
        private static int placedLeftOut;
        private static readonly HashSet<string> placedLogged = new HashSet<string>(), noEmitterLogged = new HashSet<string>();
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
        // Out on the campaign map the game keeps its lights at the top of the scene, under no prop and with no name that says
        // what they belong to (its merged scenes: at Horns of the South 127 "PointLight_Cookies", 184 "PointLight_Ambience",
        // 25 "PointLight_Shadows" on their own, by the harness's state dump of 2026-10-06), so every torch and candle there
        // was left out and a torch lit nothing traced (a bridge at night: the man beside a torch lit cold by the sky). A
        // flame shows itself otherwise: a warm point light (WarmKelvin at most) with a cookie, the flicker pattern the game
        // gives its flames (2400 K, 1926 K and 1620 K there), or a warm one within PartnerReach of such a light (a torch's
        // PointLight_Ambience beside its PointLight_Cookies: 91 of the 127 have one within 1 m). A warm light alone, with no
        // cookie and none beside it, stays out: placed fill light looks the same (DEAD ENDS: whitelist, never blacklist).
        private const float WarmKelvin = 3500f, PartnerReach = 1f;
        private static readonly List<Vector3> flickering = new List<Vector3>();
        private static bool Warm(Light light)
        {
            if (light.useColorTemperature) return light.colorTemperature > 0f && light.colorTemperature <= WarmKelvin;
            var c = light.color;
            return c.r > 0f && c.r >= 1.5f * c.b && c.r >= c.g;
        }
        private static bool Flickers(Light light) => light.type == LightType.Point && light.cookie != null && Warm(light);
        // The warm flickering lights of the loaded scenes, gathered at each scan; the classifications that depended on them
        // are made again when they change.
        private static void GatherFlickering(List<Light> all)
        {
            int before = flickering.Count;
            flickering.Clear();
            foreach (var light in all) if (light != null && light.isActiveAndEnabled && Flickers(light)) flickering.Add(light.transform.position);
            if (flickering.Count != before) { namedCache.Clear(); placedCache.Clear(); sizes.Clear(); }
        }
        private static bool FlameByLook(Light light)
        {
            if (light.type != LightType.Point || !Warm(light)) return false;
            if (light.cookie != null) return true;
            var p = light.transform.position;
            foreach (var q in flickering) if ((q - p).sqrMagnitude <= PartnerReach * PartnerReach) return true;
            return false;
        }
        // Lights the player added on purpose: Conversation Lighting Overhaul's portrait light in dialogue.
        private static readonly string[] KeptNames = { "TGConversationLightingOverhaul" };
        private static bool Names(string name, string[] words)
        {
            foreach (var w in words) if (name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) return true;
            return false;
        }
        // A light by its names and look alone: kept for a flame, a lamp or an effect (and which kind of emitter that is), left
        // out otherwise. Kept while its parent stays the same: the game may attach a prop's light to the prop after the light
        // first shows up, and a candle taken for a placed light then would stay dark and untraced.
        private struct Named { internal bool placed, flame, kept; internal EmitterKind kind; internal int parent; }
        private static readonly Dictionary<int, Named> namedCache = new Dictionary<int, Named>();
        private static Named ByNames(Light light)
        {
            int id = light.GetInstanceID();
            var parentTransform = light.transform.parent;
            int parent = parentTransform != null ? parentTransform.GetInstanceID() : 0;
            if (namedCache.TryGetValue(id, out var cached) && cached.parent == parent) return cached;
            var n = new Named { parent = parent, kept = Names(light.name, KeptNames) };
            if (n.kept) n.placed = false;
            else if (Names(light.name, AlwaysPlacedNames)) n.placed = true;
            else
            {
                n.placed = true;
                var t = light.transform;
                for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)
                {
                    if (Names(t.name, SourceNames)) n.placed = false;
                    if (Names(t.name, FlameNames)) n.flame = true;
                    n.kind = LightRules.MostSpecific(n.kind, LightRules.KindOfName(t.name));
                }
                // A flame the names do not tell: a light under no prop, by its look (WarmKelvin, its flicker cookie).
                if (n.placed && light.transform.parent == null && FlameByLook(light)) { n.placed = false; n.flame = true; n.kind = EmitterKind.FlameByLook; }
            }
            namedCache[id] = n;
            return n;
        }
        // The glowing things near the camera (GatherGlows): the traced emissive surfaces (SceneCapture.Glows), effects drawn
        // glowing and emissive decals, those a light kept by its names or look stands at claimed.
        private static readonly List<GlowThing> things = new List<GlowThing>();
        // A light's classification: by its names and look, and where they leave it out, by what is at it (PT-47, the owner,
        // 2026-10-06: "make sure we only drop those fake area lights ... But emissive things, like crystals in a cave, or magic
        // ... of course they should still emit light"). A light within LightRules.GlowReach of a glowing thing no flame's or
        // lamp's light claims is that thing's light, kept with HDRP's colour, strength and range (an effect's reach); a light
        // with nothing glowing at it is the game's fill, fake area or fake bounce light, left out. Made again when the glowing
        // things change.
        // noEmitter: kept by a flame's or a lamp's name, but with no flame, lamp or glowing thing within LightRules.GlowReach (the
        // mineral cave's six warm "Point Light_35" to "_40" in a group named "torch_06", none at its torch: their reflections in
        // the wet floor were the owner's "4 of these yellow lights in the water ... I only see one torch"), left out as fill.
        private struct Class { internal bool placed, effect, glow, noEmitter; internal int parent; internal EmitterKind kind; internal GlowThing thing; internal float distance; }
        private static readonly Dictionary<int, Class> placedCache = new Dictionary<int, Class>(), glowKept = new Dictionary<int, Class>();
        // A light kept by its names that once had its flame at it, by its parent: kept while its parent stays the same, as a
        // flame's effect may pause between bursts.
        private static readonly Dictionary<int, int> emitterSeen = new Dictionary<int, int>();
        // Where and how far the glowing things were gathered (GatherGlows): a light beyond that is not judged by what is at it.
        private static Vector3 gatheredAt;
        private static float gatheredRadius = -1f;
        internal static bool IsPlacedLight(Light light) => Classify(light).placed;
        // A kept light an effect or a glowing thing stands for (a portal, a spell, a crystal): no flame or lamp in its name or its
        // parents'. It lights as far as HDRP's range.
        internal static bool IsEffectLight(Light light) => Classify(light).effect;
        private static Class Classify(Light light)
        {
            int id = light.GetInstanceID();
            var named = ByNames(light);
            if (placedCache.TryGetValue(id, out var cached) && cached.parent == named.parent) return cached;
            // Once kept for a glowing thing, kept while its parent stays the same: an effect's particles come and go between
            // bursts, and its light would go out and come back with them.
            if (glowKept.TryGetValue(id, out cached) && cached.parent == named.parent) { placedCache[id] = cached; return cached; }
            var c = new Class { placed = named.placed, effect = !named.placed && !named.kept && !named.flame, parent = named.parent, kind = named.kind };
            // Kept by a flame's or a lamp's name (not by its look, nor an effect's own light, whose name is its effect): its flame,
            // lamp or glowing thing must be at it (LightRules.EmitterAt), else it is a fill light the game filed under the prop.
            if (!named.placed && named.flame && named.kind != EmitterKind.FlameByLook && !(emitterSeen.TryGetValue(id, out int seenParent) && seenParent == named.parent))
            {
                var p = light.transform.position;
                // Beyond the gathered things nothing can be judged: kept by its name, judged again when it comes nearer.
                if (gatheredRadius < 0f || (p - gatheredAt).sqrMagnitude > (gatheredRadius - LightRules.GlowReach) * (gatheredRadius - LightRules.GlowReach)) return c;
                if (LightRules.EmitterAt(p.x, p.y, p.z, things, out _) >= 0) emitterSeen[id] = named.parent;
                else { c.placed = true; c.noEmitter = true; }
            }
            if (named.placed && !Names(light.name, AlwaysPlacedNames))
            {
                var p = light.transform.position;
                int at = LightRules.GlowAt(p.x, p.y, p.z, things, out float distance, out _);
                if (at >= 0) { c.placed = false; c.effect = true; c.glow = true; c.kind = EmitterKind.Glow; c.thing = things[at]; c.distance = distance; glowKept[id] = c; }
            }
            placedCache[id] = c;
            return c;
        }
        // What is at a light, for the log and the capture's report: the glowing thing it was kept for, or the nearest one within
        // 5 m and why it doesn't count.
        private static string WhatIsAt(Light light)
        {
            var c = Classify(light);
            if (c.glow) return "at a glowing " + LightRules.KindNames[c.thing.Kind] + " \"" + c.thing.Name + "\" " + c.distance.ToString("0.##") + " m away (its color " +
                ColorText(c.thing.R, c.thing.G, c.thing.B) + ", the light's " + LightColorText(light) + ")";
            var p = light.transform.position;
            int nearest = -1; float best = 5f;
            for (int i = 0; i < things.Count; i++) { float d = LightRules.Distance(p.x, p.y, p.z, things[i]); if (d < best) { best = d; nearest = i; } }
            if (nearest < 0) return "nothing glowing within 5 m";
            var t = things[nearest];
            return "nearest glowing " + LightRules.KindNames[t.Kind] + " \"" + t.Name + "\" " + best.ToString("0.##") + " m away" +
                (best > LightRules.GlowReach ? "" : !LightRules.IsThing(t) ? ", " + (2f * Mathf.Max(t.HalfX, Mathf.Max(t.HalfY, t.HalfZ))).ToString("0.#") + " m across (a wall or a volume, not a thing)" :
                    t.Claimed ? ", which a flame's or a lamp's light stands for" : "");
        }
        // Why a light kept by a flame's or a lamp's name is left out, for the log and the capture's report.
        private static string NoEmitterText(Light light)
        {
            string word = null;
            var t = light.transform;
            for (int depth = 0; t != null && depth <= 5 && word == null; depth++, t = t.parent)
                foreach (var w in FlameNames) if (t.name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) { word = "\"" + w + "\" (" + (depth == 0 ? "its name" : "\"" + t.name + "\"") + ")"; break; }
            return "its names say " + (word ?? "a flame or a lamp") + ", but no flame, lamp or glowing thing stands within " + LightRules.GlowReach.ToString("0.#") + " m of it: a fill light (" + WhatIsAt(light) + ")";
        }
        private static string ColorText(float r, float g, float b)
        {
            float s = Mathf.Max(r + g + b, 1e-6f);
            return (r / s).ToString("0.00") + "/" + (g / s).ToString("0.00") + "/" + (b / s).ToString("0.00");
        }
        private static string LightColorText(Light light) { var c = LightColor(light, null); return ColorText(c.x, c.y, c.z); }
        // Why IsPlacedLight keeps or leaves out a light, for the capture's report.
        internal static string Classification(Light light)
        {
            foreach (var w in KeptNames) if (light.name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) return "kept: added by a mod (\"" + w + "\")";
            foreach (var w in AlwaysPlacedNames) if (light.name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0) return "left out: its name has \"" + w + "\"";
            var c = Classify(light);
            if (c.noEmitter) return "left out: " + NoEmitterText(light);
            string size = c.placed ? "" : ", traced " + (2f * Size(light, light.GetComponent<HDAdditionalLightData>()).radius).ToString("0.###") + " m across";
            if (c.glow) return "kept: " + WhatIsAt(light) + " (lights as far as its HDRP range)" + size;
            string effect = c.effect ? " (an effect: lights as far as its HDRP range)" : "";
            var t = light.transform;
            for (int depth = 0; t != null && depth <= 5; depth++, t = t.parent)
                foreach (var w in SourceNames)
                    if (t.name.IndexOf(w, System.StringComparison.OrdinalIgnoreCase) >= 0)
                        return "kept: " + (depth == 0 ? "its name" : "parent \"" + t.name + "\"") + " has \"" + w + "\"" + effect + size;
            if (light.transform.parent == null && FlameByLook(light))
                return "kept: under no prop, a flame by its look (" + (light.cookie != null ? "warm, with its flicker cookie" : "warm, within " + PartnerReach.ToString("0.#") + " m of a flickering one") + ")" + size;
            return "left out: neither it nor 5 parents name a flame, lamp or effect, and " + WhatIsAt(light);
        }
        // The glowing things within the scene's radius, gathered at each scan: the traced instances with an emissive material,
        // effects drawn glowing (particles and visual effects whose material adds its light, glows by its own colors or is named
        // for fire or light) and emissive decals (the stone circles' glowing runes); then those a light kept by its names or look
        // stands at are claimed. The classifications and sizes that depended on them are made again when their number changes.
        private static readonly Dictionary<int, (bool glows, Color color)> effectMaterials = new Dictionary<int, (bool, Color)>();
        private static readonly string[] GlowWords = { "Additive", "Fire", "Flame", "Glow", "Emissive", "Ember", "Spark", "Magic", "Light", "Firefl", "Orb" };
        private static readonly string[] ColorNames = { "_EmissiveColor", "_UnlitColor", "_BaseColor", "_TintColor", "_Color" };
        private static void GatherGlows(List<Light> all, Vector3 camera, float radius, bool lightLayers)
        {
            int before = things.Count;
            things.Clear();
            // As far as a traced light reaches into the scene (MaxReach past its radius): a flame's light out there is judged by
            // what is at it too (Classify).
            gatheredAt = camera; gatheredRadius = radius + MaxReach + LightRules.GlowReach;
            float r2 = gatheredRadius * gatheredRadius;
            foreach (var g in SceneCapture.Glows)
            {
                float dx = g.X - camera.x, dy = g.Y - camera.y, dz = g.Z - camera.z;
                if (dx * dx + dy * dy + dz * dz <= r2) things.Add(g);
            }
            foreach (var r in Object.FindObjectsByType<ParticleSystemRenderer>(FindObjectsSortMode.None)) AddEffect(r, camera, r2);
            foreach (var r in Object.FindObjectsByType<UnityEngine.VFX.VFXRenderer>(FindObjectsSortMode.None)) AddEffect(r, camera, r2);
            foreach (var d in Object.FindObjectsByType<DecalProjector>(FindObjectsSortMode.None)) AddDecal(d, camera, r2);
            foreach (var light in all)
            {
                if (light == null || !light.isActiveAndEnabled || light.intensity <= 0f || (light.type != LightType.Point && light.type != LightType.Spot)) continue;
                var p = light.transform.position;
                if ((p - camera).sqrMagnitude > r2 || ByNames(light).placed || !LightsWorld(light.GetComponent<HDAdditionalLightData>(), lightLayers)) continue;
                LightRules.Claim(p.x, p.y, p.z, things);
            }
            if (things.Count != before) { placedCache.Clear(); sizes.Clear(); }
        }
        private static void AddEffect(Renderer r, Vector3 camera, float r2)
        {
            if (r == null || !r.enabled || !r.gameObject.activeInHierarchy) return;
            var b = r.bounds;
            if (b.extents.sqrMagnitude <= 0f || b.SqrDistance(camera) > r2) return;
            // A visual effect has a material for each of its outputs (the flame, the embers, the smoke, the warp of hot air):
            // it glows when any of them does. Only the first was read until 2026-10-08, and since the warp counts for nothing
            // (LightRules.IsDistortionOutput) a wall torch whose first output is its warp had no flame: its three lights were
            // left out as fill lights and the tavern's room went dark (PT-79).
            Material m = null; Color color = Color.black;
            r.GetSharedMaterials(effectScratch);
            foreach (var candidate in effectScratch) if (candidate != null && EffectGlows(candidate, out var c)) { m = candidate; color = c; break; }
            effectScratch.Clear();
            if (m == null) return;
            things.Add(new GlowThing { X = b.center.x, Y = b.center.y, Z = b.center.z, HalfX = b.extents.x, HalfY = b.extents.y, HalfZ = b.extents.z,
                R = color.r, G = color.g, B = color.b, Kind = LightRules.GlowEffect, Name = r.name + " (" + m.name + ")" });
        }
        private static readonly List<Material> effectScratch = new List<Material>();
        private static bool EffectGlows(Material m, out Color color)
        {
            int id = m.GetInstanceID();
            if (effectMaterials.TryGetValue(id, out var known)) { color = known.color; return known.glows; }
            string shaderName = m.shader != null ? m.shader.name : "";
            if (LightRules.IsDistortionOutput(shaderName)) { color = Color.black; effectMaterials[id] = (false, color); return false; }
            bool glows = (m.HasProperty("_DstBlend") && Mathf.Approximately(m.GetFloat("_DstBlend"), (float)BlendMode.One)) ||
                (m.HasProperty("_SurfaceType") && m.GetFloat("_SurfaceType") > 0.5f && m.HasProperty("_BlendMode") && Mathf.Approximately(m.GetFloat("_BlendMode"), 1f)) ||
                (m.HasProperty("_EmissiveColor") && m.GetColor("_EmissiveColor").maxColorComponent > 0.001f) ||
                (m.HasProperty("_EmissionColor") && m.GetColor("_EmissionColor").maxColorComponent > 1f) ||
                Names(shaderName + " " + m.name, GlowWords);
            color = Color.white;
            foreach (var name in ColorNames) if (m.HasProperty(name)) { var c = m.GetColor(name); if (c.maxColorComponent > 0.001f) { color = c.linear; break; } }
            effectMaterials[id] = (glows, color);
            return glows;
        }
        private static void AddDecal(DecalProjector d, Vector3 camera, float r2)
        {
            if (d == null || !d.isActiveAndEnabled) return;
            var m = d.material;
            if (m == null) return;
            bool affects = m.HasProperty("_AffectEmission") && m.GetFloat("_AffectEmission") > 0.5f;
            var e = m.HasProperty("_EmissiveColor") ? m.GetColor("_EmissiveColor") : Color.black;
            if (!affects && !(e.maxColorComponent > 0.001f && !m.HasProperty("_AffectEmission"))) return;
            var t = d.transform;
            var c = t.TransformPoint(d.pivot);
            if ((c - camera).sqrMagnitude > r2) return;
            var h = Vector3.Scale(d.size * 0.5f, t.lossyScale);
            Vector3 x = t.right * h.x, y = t.up * h.y, z = t.forward * h.z;
            if (e.maxColorComponent <= 0.001f) e = Color.white;
            things.Add(new GlowThing { X = c.x, Y = c.y, Z = c.z,
                HalfX = Mathf.Abs(x.x) + Mathf.Abs(y.x) + Mathf.Abs(z.x), HalfY = Mathf.Abs(x.y) + Mathf.Abs(y.y) + Mathf.Abs(z.y), HalfZ = Mathf.Abs(x.z) + Mathf.Abs(y.z) + Mathf.Abs(z.z),
                R = e.r, G = e.g, B = e.b, Kind = LightRules.GlowDecal, Name = d.name + " (" + m.name + ")" });
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
        // How far a kept light is sampled as a near light (the light grid lists it; past that the far lists sample it, and
        // its light goes on falling off with distance alone, TGPT_LIGHT_NO_RANGE, PT-50; until 2026-10-07 it stopped there):
        // as far as it still gives 50 lux, or its HDRP range if that is farther, at most 40 m. HDRP's range is an artist's
        // cut-off, and with the game's fill lights left out it made a chandelier's light stop 2.7 m below it and a candle's
        // 0.5 m from its flame. An effect's
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
        // One of the game's "Interior" scenes is loaded: the player is inside it (outdoors none is loaded: 164 loads of
        // the harness's saves on the map, none with one; 78 in a tavern or a house, each with exactly one beside the map).
        // What the map around it holds is not the interior's: WaterCaustics leaves the map's water out by it (PT-114).
        internal static bool InteriorLoaded { get; private set; }
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
        // The size a kept light is traced with: the size of what emits it, for its highlights and its shadows alike (the owner,
        // 2026-10-06, asked whether lamps get their real size for highlights only or for shadows too: "whatever is most
        // realistic"). Never HDRP's radius: an artist's value for soft shadows (this game gives candle and torch lights 0.25 to
        // 1.5 m and more), which the tracer drew as a glowing ball that big, so a lamp's highlight in a puddle was a long soft
        // smear where HDRP draws a point. Measured from the emitting thing at the light where there is one (the flame's
        // particles, a lantern's glowing glass, the crystal it was kept for: LightRules.MeasuredRadius, held to its kind's
        // range), else its kind's size (LightRules.DefaultRadius: a candle's flame 1.2 cm across, a torch's 8 cm, a lantern's
        // glass 10 cm, a brazier 30 cm, a bonfire 50 cm). Its cookie still shapes the light near it as HDRP's does. A mod's own
        // light (the dialogue light) stands for nothing in the world and keeps HDRP's radius.
        // And a flame's or a lamp's light is traced where its measured flame is (LightRules.FlameCentre, TGPT_LIGHT_AT_EMITTER),
        // never where HDRP has it: the tavern's candles have theirs in the wax 10 cm under the flame (21,149 cd), which HDRP
        // softens by its radius of 1 m (its fill-light falloff, I / (d^2 + r^2): the wax 3 cm away got I / 1.0009), and which the
        // tracer, at the flame's 1.2 cm, gave the whole wax from inside (I / d^2: 1,100 times that), white-hot (lights round 2).
        // The hidden "Lights sized as their emitters" key off traces every light at HDRP's place and radius, as before, to compare.
        private struct Traced { internal float radius; internal EmitterKind kind; internal bool measured, atEmitter; internal Vector3 local; internal string source; }
        private static readonly HashSet<int> photometryLogged = new HashSet<int>();
        private static readonly Dictionary<int, Traced> sizes = new Dictionary<int, Traced>();
        private static bool asEmitters = true;
        private static float TracedRadius(Light light, HDAdditionalLightData hd) => Size(light, hd).radius;
        private static Traced Size(Light light, HDAdditionalLightData hd)
        {
            int id = light.GetInstanceID();
            if (sizes.TryGetValue(id, out var s)) return s;
            s = default;
            if (Names(light.name, KeptNames)) s.radius = Mathf.Clamp(hd != null ? hd.shapeRadius : 0.05f, 0.02f, 0.3f);
            else
            {
                var c = Classify(light);
                var kind = c.kind != EmitterKind.Unknown ? c.kind : c.effect ? EmitterKind.Effect : EmitterKind.Unknown;
                s.kind = kind;
                if (!asEmitters) s.radius = LightRules.HdrpRadius(hd != null ? hd.shapeRadius : 0.05f, kind == EmitterKind.Fire || kind == EmitterKind.Brazier || kind == EmitterKind.Lava);
                else if (c.glow) { s.radius = LightRules.MeasuredRadius(kind, c.thing.HalfX, c.thing.HalfY, c.thing.HalfZ); s.measured = true; s.source = EmitterText(c.thing); }
                else
                {
                    var p = light.transform.position;
                    float best = LightRules.SearchReach(kind); int at = -1;
                    for (int i = 0; i < things.Count; i++)
                    {
                        if (!LightRules.IsThing(things[i])) continue;
                        float d = LightRules.Distance(p.x, p.y, p.z, things[i]);
                        if (d <= best) { best = d; at = i; }
                    }
                    if (at < 0) s.radius = LightRules.DefaultRadius(kind);
                    else
                    {
                        var t = things[at];
                        s.source = EmitterText(t);
                        s.radius = LightRules.MeasuredRadius(kind, t.HalfX, t.HalfY, t.HalfZ); s.measured = true;
                        // Its place in the light's own space, so a carried lantern's light stays at its flame as it turns.
                        if (LightRules.FlameCentre(kind, t)) { s.atEmitter = true; s.local = light.transform.InverseTransformPoint(new Vector3(t.X, t.Y, t.Z)); }
                    }
                }
            }
            sizes[id] = s;
            return s;
        }
        private static string EmitterText(GlowThing t) => t.Name + " [" + LightRules.KindNames[t.Kind] +
            "; bounds centre " + new Vector3(t.X, t.Y, t.Z).ToString("F3") + "; half extents " +
            new Vector3(t.HalfX, t.HalfY, t.HalfZ).ToString("F3") +
            (t.Kind == LightRules.GlowEffect ? "; culling bounds, not a measured particle flame" : "") + "]";
        private static void LogPhotometry(Light light, HDAdditionalLightData hd, Traced s, NativeLight n, Vector3 camera)
        {
            if ((s.kind != EmitterKind.Lamp && s.kind != EmitterKind.Candle) ||
                (light.transform.position - camera).sqrMagnitude > 100f || !photometryLogged.Add(light.GetInstanceID())) return;
            var paired = new System.Text.StringBuilder();
            foreach (var thing in things)
                if (thing.Kind == LightRules.GlowSurface && thing.Claimed &&
                    LightRules.Distance(light.transform.position.x, light.transform.position.y, light.transform.position.z, thing) <= LightRules.GlowReach)
                    paired.Append("; " + thing.Name + " key " + thing.Key);
            Session.Log.LogInfo("Light photometry: " + light.transform.parent?.name + "/" + light.name +
                "; Unity intensity " + light.intensity.ToString("F3") + " cd; colour " + light.color +
                "; temperature " + (light.useColorTemperature ? light.colorTemperature.ToString("F0") + " K" : "off") +
                "; original dimmer " + OriginalDimmer(light, hd).ToString("F3") + "; range " + light.range.ToString("F3") +
                " m; HDRP radius " + (hd != null ? hd.shapeRadius.ToString("F3") : "unknown") + " m" +
                "; HDRP position " + light.transform.position.ToString("F3") + "; traced position " + new Vector3(n.PositionX, n.PositionY, n.PositionZ).ToString("F3") +
                "; traced radius " + n.Radius.ToString("F4") + " m; traced RGB cd " + new Vector3(n.ColorR, n.ColorG, n.ColorB).ToString("F3") +
                "; emitter " + (s.source ?? "unknown: kind-size fallback") + "; nearby claimed emissive surfaces" + (paired.Length > 0 ? paired.ToString() : ": none") +
                "; " + HdrpPasses.ExposureText);
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
        private static string InventoryKey(Light light) => System.Text.RegularExpressions.Regex.Replace(light.name.Replace("(Clone)", ""), @"[\d_ ]+$", "").Trim();
        private static void LogInventory(IEnumerable<Light> lights)
        {
            var groups = new Dictionary<string, (int count, int shadowed, int fill, int glow, float minRadius, float maxRadius, float maxRange, string parent)>();
            foreach (var light in lights)
            {
                if (light == null || light.type == LightType.Directional) continue;
                string key = InventoryKey(light);
                if (inventoried.Contains(key)) continue;
                var hd = light.GetComponent<HDAdditionalLightData>();
                float radius = hd != null ? hd.shapeRadius : 0f;
                groups.TryGetValue(key, out var g);
                if (g.count == 0) { g.minRadius = radius; g.parent = light.transform.parent != null ? light.transform.parent.name : "(none)"; }
                var c = Classify(light);
                g.count++; if (light.shadows != LightShadows.None) g.shadowed++; if (c.placed) g.fill++; if (c.glow) g.glow++;
                g.minRadius = Mathf.Min(g.minRadius, radius); g.maxRadius = Mathf.Max(g.maxRadius, radius); g.maxRange = Mathf.Max(g.maxRange, light.range);
                groups[key] = g;
            }
            if (groups.Count == 0) return;
            foreach (var key in groups.Keys) inventoried.Add(key);
            var list = new List<KeyValuePair<string, (int count, int shadowed, int fill, int glow, float minRadius, float maxRadius, float maxRange, string parent)>>(groups);
            list.Sort((a, b) => b.Value.count.CompareTo(a.Value.count));
            var sb = new System.Text.StringBuilder("Lights by name, new this session (count, with shadows, HDRP's radius, range, a parent; * left out: placed, not a flame, lamp or effect, and " +
                "nothing glowing at it, or named for a flame or a lamp with none at it; + kept for a glowing thing at it; " + things.Count + " glowing things known now):");
            for (int i = 0; i < list.Count && i < 40; ++i)
            {
                var g = list[i].Value;
                sb.Append("\n  " + (g.fill > 0 ? "* " : g.glow > 0 ? "+ " : "  ") + "\"" + list[i].Key + "\" x" + g.count + ", shadows " + g.shadowed + ", radius " + g.minRadius.ToString("0.###") +
                    (g.maxRadius > g.minRadius ? "-" + g.maxRadius.ToString("0.###") : "") + " m, range up to " + g.maxRange.ToString("0.#") + " m, under \"" + g.parent + "\"" +
                    (g.glow > 0 ? ", " + g.glow + " at glowing things" : ""));
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

        internal static void Reset()
        {
            RestoreAll(); slots.Clear(); slotLights.Clear(); slotData.Clear(); freeSlots.Clear();
            namedCache.Clear(); placedCache.Clear(); glowKept.Clear(); sizes.Clear(); things.Clear(); effectMaterials.Clear(); emitterSeen.Clear(); gatheredRadius = -1f;
            Count = 0; nextScan = 0; Sun = default;
        }

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

        // PT-85 and PT-40 (docs/agents/flame-specks.md): a lamp's near size and the hidden "Lamp trial" settings as the one number
        // the tracer takes them as (TGPT_Frame.jitter's w): the near size in cm (bits 0-7, "Lamp near size", 10), then two bits each for the
        // reflected ball, the highlight and the shadow's disc (0 the lamp's own size, 1 HDRP's radius, 2 five times its own).
        // 0, the default of those three, leaves every lamp's size as it is.
        internal static float LampTrial(Settings s)
        {
            // Bits 14-21 (docs/agents/flame-near.md): what stands within that many centimetres of a lamp is found on purpose.
            return Mathf.Clamp(s.LampNearSize.Value, 0, 255) + 256 * Mathf.Clamp(s.LampTrialReflected.Value, 0, 2) +
                1024 * Mathf.Clamp(s.LampTrialHighlight.Value, 0, 2) + 4096 * Mathf.Clamp(s.LampTrialShadow.Value, 0, 2) +
                16384 * Mathf.Clamp(s.LampTrialAround.Value, 0, 255);
        }

        internal static void Tick(float now, Vector3 camera, bool wantLights, bool lightLayers)
        {
            UpdateSun();
            var tracer = Session.Tracer;
            if (tracer == null || !tracer.Connected) return;
            const float radius = float.PositiveInfinity; // PT-63: no distance leaves a light out (the Scene Radius row is gone)
            bool emitters = Session.Settings.LightsAsEmitters.Value;
            if (emitters != asEmitters) { asEmitters = emitters; sizes.Clear(); radiiLogged = false; }
            if (now >= nextScan)
            {
                nextScan = now + ScanInterval;
                // Lights gone or disabled give their slot back; lights in range take one.
                var alive = new HashSet<int>();
                int leftOut = 0;
                placedLeftOut = 0;
                var all = wantLights ? AllLights() : new List<Light>();
                GatherFlickering(all);
                GatherGlows(all, camera, radius, lightLayers);
                LogInventory(all);
                if (!effectsLogged && all.Count >= 20) { effectsLogged = true; LightEffectsInventory.Log(); }
                InteriorLoaded = InteriorScene();
                Interior = !Sun.Valid || InteriorScene();
                if (wantLights)
                    foreach (var light in all)
                    {
                        // A kept light (the dialogue light) holds its slot at intensity 0, so its traced light fades in with HDRP's.
                        if (light == null || !light.isActiveAndEnabled || (light.intensity <= 0f && !Names(light.name, KeptNames))) continue;
                        if (light.type != LightType.Point && light.type != LightType.Spot) continue;
                        // Every loaded light, however far (PT-50): its light falls off with distance alone. Only an effect's light,
                        // which stops at its HDRP range, is left out where that range does not reach the traced scene.
                        if (IsEffectLight(light))
                        {
                            float reach = radius + Reach(light, light.GetComponent<HDAdditionalLightData>());
                            if ((light.transform.position - camera).sqrMagnitude > reach * reach) continue;
                        }
                        int id = light.GetInstanceID();
                        HDAdditionalLightData data = slots.TryGetValue(id, out int existing) ? slotData[existing] : light.GetComponent<HDAdditionalLightData>();
                        if (!LightsWorld(data, lightLayers)) { ++leftOut; continue; }
                        // Lights placed to fake light (IsPlacedLight) would light the scene a second time.
                        var cls = Classify(light);
                        if (cls.placed)
                        {
                            Darken(light, data);
                            ++placedLeftOut;
                            // Once a name (its digits dropped) and what is at it: the evidence for the next look at the rule.
                            if (cls.noEmitter) { if (noEmitterLogged.Count < 60 && noEmitterLogged.Add(InventoryKey(light) + " under " + (light.transform.parent != null ? light.transform.parent.name : "")))
                                Session.Log.LogInfo("Lights: \"" + light.name + "\" under \"" + (light.transform.parent != null ? light.transform.parent.name : "(none)") +
                                    "\" left out (" + NoEmitterText(light) + "; color " + LightColorText(light) + ")"); }
                            else if (placedLogged.Count < 60 && placedLogged.Add("-" + InventoryKey(light)))
                                Session.Log.LogInfo("Lights: \"" + light.name + "\" under \"" + (light.transform.parent != null ? light.transform.parent.name : "(none)") +
                                    "\" left out (placed to fake light: no flame, lamp or effect in its names, " + WhatIsAt(light) + "; color " + LightColorText(light) + ")");
                            continue;
                        }
                        if (cls.glow && placedLogged.Count < 60 && placedLogged.Add("+" + InventoryKey(light)))
                            Session.Log.LogInfo("Lights: \"" + light.name + "\" under \"" + (light.transform.parent != null ? light.transform.parent.name : "(none)") + "\" kept: " +
                                WhatIsAt(light) + ", traced " + (2f * TracedRadius(light, data)).ToString("0.###") + " m across, its range " + light.range.ToString("0.#") + " m");
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
                // The glowing surfaces the traced lights stand for are their own surfaces (SceneCapture, the next scan): those kept
                // for one, and those a flame's or a lamp's light claims (a lantern's glass, a brazier's embers), which the native
                // rule (the light's radius and 25 cm) no longer reaches from a light as small as its flame.
                var own = new HashSet<long>();
                foreach (var t in things) if (t.Kind == LightRules.GlowSurface && t.Claimed) own.Add(t.Key);
                for (int i = 0; i < slotLights.Count; i++)
                {
                    var light = slotLights[i];
                    if (light == null) continue;
                    var c = Classify(light);
                    if (c.glow && c.thing.Kind == LightRules.GlowSurface) own.Add(c.thing.Key);
                }
                SceneCapture.OwnSurfaces = own;
                // Once a session with a few dozen lights: the sizes they are traced with, by what emits them (which set how sharp
                // their shadows and highlights are), and HDRP's radii they no longer take.
                if (!radiiLogged && slots.Count >= 20)
                {
                    radiiLogged = true;
                    var radii = new List<float>();
                    var byKind = new SortedDictionary<EmitterKind, (int count, int measured, float min, float max, int moved, float minMove, float maxMove)>();
                    for (int i = 0; i < slotLights.Count; i++)
                    {
                        if (slotLights[i] == null) continue;
                        if (slotData[i] != null) radii.Add(slotData[i].shapeRadius);
                        var s = Size(slotLights[i], slotData[i]);
                        byKind.TryGetValue(s.kind, out var k);
                        if (k.count == 0) { k.min = s.radius; k.max = s.radius; k.minMove = float.MaxValue; }
                        k.count++; if (s.measured) k.measured++; k.min = Mathf.Min(k.min, s.radius); k.max = Mathf.Max(k.max, s.radius);
                        if (s.atEmitter)
                        {
                            float move = (slotLights[i].transform.TransformPoint(s.local) - slotLights[i].transform.position).magnitude;
                            k.moved++; k.minMove = Mathf.Min(k.minMove, move); k.maxMove = Mathf.Max(k.maxMove, move);
                        }
                        byKind[s.kind] = k;
                    }
                    var sb = new System.Text.StringBuilder(asEmitters ? "Lights: traced at the size of what emits them (across), at its flame where measured:" :
                        "Lights: traced at HDRP's place and radius (\"Lights sized as their emitters\" off):");
                    foreach (var k in byKind)
                        sb.Append(" " + (k.Key == EmitterKind.Unknown ? "other" : k.Key.ToString()) + " x" + k.Value.count + " " + (2f * k.Value.min).ToString("0.###") +
                            (k.Value.max > k.Value.min ? "-" + (2f * k.Value.max).ToString("0.###") : "") + " m (" + k.Value.measured + " measured" +
                            (k.Value.moved > 0 ? ", " + k.Value.moved + " moved to their flame by " + k.Value.minMove.ToString("0.###") +
                                (k.Value.maxMove > k.Value.minMove ? "-" + k.Value.maxMove.ToString("0.###") : "") + " m" : "") + ");");
                    if (radii.Count > 0)
                    {
                        radii.Sort();
                        sb.Append(" HDRP's radii were " + radii[0].ToString("0.000") + " to " + radii[radii.Count - 1].ToString("0.000") + " m, median " + radii[radii.Count / 2].ToString("0.000") + " m");
                    }
                    Session.Log.LogInfo(sb.ToString());
                }
            }
            int count = slotLights.Count;
            var trial = Session.Settings;
            bool hdrpRadiusAsked = trial.LampTrialReflected.Value == 1 || trial.LampTrialHighlight.Value == 1 || trial.LampTrialShadow.Value == 1;
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
                    // Where its flame is, where one was measured (Size): nothing of its prop around it there (TGPT_LIGHT_AT_EMITTER).
                    var size = Size(light, hd);
                    var at = size.atEmitter ? t.TransformPoint(size.local) : p;
                    n.PositionX = at.x; n.PositionY = at.y; n.PositionZ = at.z;
                    n.ColorR = color.x; n.ColorG = color.y; n.ColorB = color.z;
                    n.Range = Reach(light, hd); n.DirectionX = f.x; n.DirectionY = f.y; n.DirectionZ = f.z;
                    n.Radius = size.radius;
                    var up = t.up;
                    n.UpX = up.x; n.UpY = up.y; n.UpZ = up.z;
                    int cookie = CookieOf(light);
                    n.Flags = cookie >= 0 ? (uint)(cookie + 1) : 0u;
                    if (HdrpShadows(light, hd, (p - camera).magnitude)) n.Flags |= Tgpt.LightHdrpShadows;
                    // No highlight where HDRP gives it none (the game's candles and fill lights often have their specular off).
                    if (hd != null && !hd.affectSpecular) n.Flags |= Tgpt.LightNoSpecular;
                    if (size.atEmitter) n.Flags |= Tgpt.LightAtEmitter;
                    // ON TRIAL (LampTrial): the radius HDRP gave the light, as the "Lights sized as their emitters" key off
                    // traced it (LightRules.HdrpRadius), in millimetres in the flags' top 12 bits. Only while a switch at 1
                    // reads it; the light's own radius stays its emitter's.
                    if (hdrpRadiusAsked)
                        n.Flags |= (uint)Mathf.Clamp(Mathf.RoundToInt(1000f * LightRules.HdrpRadius(hd != null ? hd.shapeRadius : 0.05f,
                            size.kind == EmitterKind.Fire || size.kind == EmitterKind.Brazier || size.kind == EmitterKind.Lava)), 1, 4095) << Tgpt.LightHdrpRadiusShift;
                    // Its light goes on as far as a real light's does (PT-50): Reach is only how far the tracer samples it as a
                    // near light. An effect's light keeps HDRP's window at its range (Reach).
                    if (!IsEffectLight(light)) n.Flags |= Tgpt.LightNoRange;
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
                if (light != null && light.isActiveAndEnabled) LogPhotometry(light, slotData[i], Size(light, slotData[i]), n, camera);
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
