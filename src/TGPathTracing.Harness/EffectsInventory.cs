// SPDX-License-Identifier: MIT
// Opt-in diagnostics only. Reads loaded scenes (including inactive and off-camera objects), never simulates,
// pauses, bakes, changes a material, sets a shader value, creates a volume profile or calls tracer capture.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using R = TGPathTracing.Harness.EffectsRead;

namespace TGPathTracing.Harness
{
    internal static class EffectsInventory
    {
        private const int PositionSample = 8; // Diagnostic sample only; inventory counts/ranges include ALL particles.
        private const float NearbyMetres = 5f; // Evidence window, never an emitter-ownership or capture rule.
        private static void Write(string path, JToken data) => File.WriteAllText(path, data.ToString(Formatting.Indented), new UTF8Encoding(false));
        private static JArray Components<T>(Func<T, object> read) where T : Component
        {
            var a = new JArray();
            foreach (var c in Resources.FindObjectsOfTypeAll<T>().Where(R.Loaded).OrderBy(c => c.GetInstanceID()))
                a.Add(new JObject { ["identity"] = R.Try(() => R.Identity(c)), ["data"] = R.Try(() => read(c)) });
            return a;
        }
        private static JArray DynamicComponents(string typeName, Func<Component, object> read)
        {
            var a = new JArray(); var type = R.Type(typeName);
            if (type == null) { a.Add(new JObject { ["error"] = R.Unknown("type not loaded: " + typeName) }); return a; }
            foreach (var o in Resources.FindObjectsOfTypeAll(type)) if (o is Component c && R.Loaded(c))
                a.Add(new JObject { ["identity"] = R.Try(() => R.Identity(c)), ["data"] = R.Try(() => read(c)) });
            return a;
        }
        internal static IEnumerator BeforeFreeze(Scenario s, string dir)
        {
            if (!s.steps.Any(step => step.effects)) yield break;
            var samples = new JArray();
            for (int i = 0; i < 3; i++)
            {
                if (i != 0) yield return new WaitForSecondsRealtime(1f);
                yield return new WaitForEndOfFrame();
                samples.Add(R.Try(() => Snapshot()));
            }
            Write(Path.Combine(dir, "effects-live.json"), new JObject { ["schemaVersion"] = 1, ["scenario"] = s.key,
                ["note"] = "Before harness freeze; timeScale is measured, not assumed live. Adds two real seconds to this diagnostic recipe only.", ["samples"] = samples });
        }
        internal static IEnumerator Run(string dir, string scenario, bool traced, bool expectedTraced)
        {
            var data = new JObject { ["schemaVersion"] = 1, ["scenario"] = scenario, ["traced"] = traced, ["expectedTraced"] = expectedTraced,
                ["utc"] = DateTime.UtcNow.ToString("o"), ["unity"] = Application.unityVersion,
                ["coverage"] = "All loaded scenes, including inactive and off-camera objects. IDs are valid within this game session; scene/path identify across runs.",
                ["nearbyMetres"] = NearbyMetres, ["nearbyMeaning"] = "distance between light position and effect bounds (or transform without a renderer); not ownership",
                ["sampleLimit"] = PositionSample, ["particleRangeCoverage"] = "all particles returned by GetParticles, no count/capacity truncation",
                ["camera"] = R.Try(() => R.Identity(Camera.main)), ["effects"] = R.Try(() => GatherEffects()),
                ["lights"] = R.Try(() => Components<Light>(l => Light(l, traced))),
                ["fog"] = R.Try(() => Fog()) };
            Write(Path.Combine(dir, "effects.json"), data); // Keep inventory even if a later step fails.
            var samples = new JArray();
            for (int i = 0; i < 3; i++)
            {
                if (i != 0) yield return new WaitForSecondsRealtime(1f);
                yield return new WaitForEndOfFrame();
                samples.Add(R.Try(() => Snapshot()));
            }
            data["freezeSamples"] = samples;
            data["freezeMeaning"] = "Three rendered frames at least one real second apart. Compare timeScale, particles and shaderTimes; a still diagnostic sample does not prove all particles or custom shader clocks are frozen.";
            Write(Path.Combine(dir, "effects.json"), data);
            var text = new StringBuilder("# Effects inventory\n\nAll loaded scenes; nearby lights do not establish ownership. Full detail and unknown reasons: effects.json. Multi-shader systems count in each family.\n\nRenderer / shader | Systems | Live (known) | Unknown counts\n--- | ---: | ---: | ---:\n");
            var families = new SortedDictionary<string, long[]>();
            if (data["effects"] is JArray effects) foreach (var e in effects)
            {
                var row = e as JObject ?? new JObject();
                if (!(row["identity"] is JObject identity) || identity["id"] == null) continue;
                var shaders = row["materials"] is JArray mats ? mats.Select(m => m is JObject material ? (string)material["shader"] ?? "unknown" : "unknown").Distinct().ToArray() : new string[0];
                foreach (string shader in shaders.Length > 0 ? shaders : new[] { "unknown" })
                {
                    string key = Cell(row["rendererType"]) + " / " + Cell(shader);
                    if (!families.TryGetValue(key, out var counts)) families[key] = counts = new long[3];
                    counts[0]++;
                    if (row["liveCount"]?.Type == JTokenType.Integer) counts[1] += (long)row["liveCount"]; else counts[2]++;
                }
            }
            foreach (var f in families) text.Append(f.Key).Append(" | ").Append(f.Value[0]).Append(" | ").Append(f.Value[1]).Append(" | ").Append(f.Value[2]).Append('\n');
            text.Append("\nFrozen-state samples: ").Append(samples.Count).Append(". The Python effects report compares them and the live sequence. Fog and light tables are in that report.\n");
            File.WriteAllText(Path.Combine(dir, "effects.md"), text.ToString(), new UTF8Encoding(false));
        }
        private static string Cell(object o) => (o == null ? "unknown" : o.ToString()).Replace("|", "\\|").Replace("\r", " ").Replace("\n", " ");
        private static JArray GatherEffects()
        {
            var a = new JArray(); var seen = new HashSet<int>();
            var lights = Resources.FindObjectsOfTypeAll<Light>().Where(R.Loaded).ToArray();
            Action<Component, Renderer> add = (c, r) =>
            {
                if (r != null) seen.Add(r.GetInstanceID());
                var e = new JObject { ["identity"] = R.Try(() => R.Identity(c)), ["rendererType"] = r != null ? r.GetType().Name : "unknown: no renderer",
                    ["liveCount"] = c is ParticleSystem ps ? R.Try(() => ps.particleCount) : R.Unknown("not a particle system or no public count"),
                    ["renderer"] = R.Try(() => Renderer(r)), ["materials"] = R.Try(() => Materials(r)),
                    ["tracer"] = R.Try(() => EffectsTracer.Effect(r)), ["lights"] = R.Try(() => Associations(c, r, lights)) };
                a.Add(e); // Identity survives any failure reading an individual system.
                if (c is ParticleSystem particle) e["particleSystem"] = R.Try(() => Particles(particle));
                else if (c.GetType().FullName == "UnityEngine.VFX.VisualEffect")
                { e["liveCount"] = R.Member(c, "aliveParticleCount"); e["vfx"] = R.Try(() => Vfx(c, r)); }
                else if (r is TrailRenderer || r is LineRenderer)
                    e["ribbon"] = R.Members(r, "positionCount", "time", "emitting", "loop", "useWorldSpace", "widthMultiplier", "widthCurve", "colorGradient", "alignment", "textureMode", "minVertexDistance", "numCornerVertices", "numCapVertices", "generateLightingData", "shadowBias");
            };
            foreach (var ps in Resources.FindObjectsOfTypeAll<ParticleSystem>().Where(R.Loaded))
                try { add(ps, ps.GetComponent<ParticleSystemRenderer>()); } catch (Exception ex) { a.Add(new JObject { ["identity"] = R.Identity(ps), ["error"] = R.Unknown(ex.Message) }); }
            var vfxType = R.Type("UnityEngine.VFX.VisualEffect");
            if (vfxType == null) a.Add(new JObject { ["error"] = R.Unknown("UnityEngine.VFX.VisualEffect type not loaded") });
            else foreach (var o in Resources.FindObjectsOfTypeAll(vfxType)) if (o is Component c && R.Loaded(c))
                try { add(c, c.GetComponent<Renderer>()); } catch (Exception ex) { a.Add(new JObject { ["identity"] = R.Identity(c), ["error"] = R.Unknown(ex.Message) }); }
            foreach (var r in Resources.FindObjectsOfTypeAll<Renderer>().Where(R.Loaded))
            {
                if (seen.Contains(r.GetInstanceID())) continue;
                try
                {
                    if (r is TrailRenderer || r is LineRenderer || r is ParticleSystemRenderer || r.GetType().Name == "VFXRenderer") add(r, r);
                    else if (r is MeshRenderer)
                    {
                        // Include unknown verdicts too: failure to read a material must not silently exclude a candidate.
                        bool rejected = r.sharedMaterials.Any(m => !(EffectsTracer.Material(m) is JObject verdict) || (bool?)verdict["rejected"] == true);
                        if (rejected) add(r, r);
                    }
                }
                catch (Exception ex) { a.Add(new JObject { ["identity"] = R.Identity(r), ["error"] = R.Unknown(ex.Message) }); }
            }
            return a;
        }
        private static JObject Renderer(Renderer r)
        {
            var d = R.Identity(r); if (r == null) { d["state"] = R.Unknown("renderer absent"); return d; }
            d["state"] = R.Members(r, "enabled", "isVisible", "forceRenderingOff", "bounds", "localBounds", "renderingLayerMask", "shadowCastingMode", "receiveShadows", "sortingLayerID", "sortingOrder", "motionVectorGenerationMode");
            d["cameraFrustumIntersectsBounds"] = R.Try(() => Camera.main != null ? (object)GeometryUtility.TestPlanesAABB(GeometryUtility.CalculateFrustumPlanes(Camera.main), r.bounds) : R.Unknown("no main camera"));
            if (r is ParticleSystemRenderer p)
            {
                d["particleOutput"] = R.Members(p, "renderMode", "alignment", "sortMode", "normalDirection", "cameraVelocityScale", "velocityScale", "lengthScale", "minParticleSize", "maxParticleSize", "pivot", "flip", "allowRoll", "freeformStretching", "rotateWithStretchDirection", "enableGPUInstancing", "meshCount", "trailMaterial");
                d["vertexStreams"] = R.Try(() => { var streams = new List<ParticleSystemVertexStream>(); p.GetActiveVertexStreams(streams); return streams; });
                d["trailVertexStreams"] = R.Try(() => { var streams = new List<ParticleSystemVertexStream>(); R.Call(p, "GetActiveTrailVertexStreams", streams); return streams; });
                d["meshes"] = R.Try(() => { var meshes = new Mesh[p.meshCount]; int n = p.GetMeshes(meshes); return new JObject { ["returnedCount"] = n, ["choices"] = new JArray(meshes.Take(n).Select(m => R.Identity(m))) }; });
            }
            return d;
        }
        private static JArray Materials(Renderer r)
        {
            var a = new JArray(); if (r == null) return a;
            for (int i = 0; i < r.sharedMaterials.Length; i++)
            {
                var m = r.sharedMaterials[i]; var item = R.Try(() => R.Material(m));
                if (item is JObject d) { d["slot"] = i; d["tracerMaterial"] = EffectsTracer.Material(m); }
                a.Add(item);
            }
            // Trail material can differ from the billboard material, even when it is not in sharedMaterials.
            if (r is ParticleSystemRenderer p && p.trailMaterial != null)
            {
                var d = R.Material(p.trailMaterial); d["slot"] = "trails"; d["tracerMaterial"] = EffectsTracer.Material(p.trailMaterial); a.Add(d);
            }
            return a;
        }
        private static JObject Particles(ParticleSystem ps)
        {
            var d = R.Members(ps, "particleCount", "isPlaying", "isPaused", "isStopped", "isEmitting", "time", "randomSeed", "useAutoRandomSeed");
            foreach (string module in new[] { "main", "emission", "shape", "velocityOverLifetime", "limitVelocityOverLifetime", "inheritVelocity", "lifetimeByEmitterSpeed", "forceOverLifetime", "colorOverLifetime", "colorBySpeed", "sizeOverLifetime", "sizeBySpeed", "rotationOverLifetime", "rotationBySpeed", "noise", "textureSheetAnimation", "trails", "lights", "customData" })
                d[module] = R.Try(() => R.Public(R.Get(ps, module)));
            var flip = ps.textureSheetAnimation; var sprites = new JArray();
            for (int i = 0; i < flip.spriteCount; i++) { int index = i; sprites.Add(R.Try(() => R.Identity(flip.GetSprite(index)))); }
            d["flipbookSprites"] = sprites;
            d["particleLightTemplate"] = R.Try(() => ps.lights.light != null ? (object)new JObject { ["identity"] = R.Identity(ps.lights.light), ["data"] = Light(ps.lights.light, false),
                ["note"] = "Light module template, not a per-particle light instance. Public API does not enumerate generated particle light identities." } : "none: module has no light template");
            d["custom1"] = R.Try(() => { var values = new List<Vector4>(); ps.GetCustomParticleData(values, ParticleSystemCustomData.Custom1); return values; });
            d["custom2"] = R.Try(() => { var values = new List<Vector4>(); ps.GetCustomParticleData(values, ParticleSystemCustomData.Custom2); return values; });
            d["current"] = R.Try(() => CurrentParticles(ps, true));
            return d;
        }
        private static JObject CurrentParticles(ParticleSystem ps, bool all)
        {
            int count = ps.particleCount;
            var buffer = new ParticleSystem.Particle[all ? count : Math.Min(PositionSample, count)];
            int n = ps.GetParticles(buffer); var first = new JArray();
            var sizeMin = new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity); var sizeMax = -sizeMin;
            var velMin = sizeMin; var velMax = sizeMax; var colMin = new Vector4(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity); var colMax = -colMin;
            float lifeMin = float.PositiveInfinity, lifeMax = float.NegativeInfinity, startMin = lifeMin, startMax = lifeMax;
            for (int i = 0; i < n; i++)
            {
                var p = buffer[i];
                if (i < PositionSample) first.Add(new JObject { ["index"] = i, ["seed"] = p.randomSeed, ["position"] = R.Value(p.position), ["velocity"] = R.Value(p.velocity), ["remainingLifetime"] = p.remainingLifetime, ["startLifetime"] = p.startLifetime });
                if (!all) continue;
                Vector3 size = p.GetCurrentSize3D(ps), velocity = p.velocity; Color color = p.GetCurrentColor(ps); var rgba = new Vector4(color.r, color.g, color.b, color.a);
                sizeMin = Vector3.Min(sizeMin, size); sizeMax = Vector3.Max(sizeMax, size); velMin = Vector3.Min(velMin, velocity); velMax = Vector3.Max(velMax, velocity);
                colMin = Vector4.Min(colMin, rgba); colMax = Vector4.Max(colMax, rgba); lifeMin = Mathf.Min(lifeMin, p.remainingLifetime); lifeMax = Mathf.Max(lifeMax, p.remainingLifetime);
                startMin = Mathf.Min(startMin, p.startLifetime); startMax = Mathf.Max(startMax, p.startLifetime);
            }
            return new JObject { ["liveCount"] = count, ["returnedCount"] = n, ["allParticlesRead"] = all && n == count,
                ["simulationSpace"] = ps.main.simulationSpace.ToString(), ["simulationTransform"] = R.Identity(ps.main.customSimulationSpace),
                ["firstParticles"] = first, ["identityCaution"] = "array index and seed do not guarantee persistent particle identity",
                ["ranges"] = !all ? (JToken)"not sampled in freeze probe" : n == 0 ? (JToken)"empty: no live particles" : new JObject {
                    ["sizeMin"] = R.Value(sizeMin), ["sizeMax"] = R.Value(sizeMax), ["colorMin"] = R.Value(colMin), ["colorMax"] = R.Value(colMax),
                    ["velocityMin"] = R.Value(velMin), ["velocityMax"] = R.Value(velMax), ["remainingLifetime"] = new JArray(lifeMin, lifeMax), ["startLifetime"] = new JArray(startMin, startMax) } };
        }
        internal static JObject Vfx(Component effect, Renderer renderer)
        {
            var d = R.Members(effect, "enabled", "pause", "playRate", "culled", "aliveParticleCount", "initialEventName", "startSeed", "resetSeedOnPlay", "visualEffectAsset");
            foreach (string category in new[] { "System", "ParticleSystem", "SpawnSystem", "OutputEvent" })
            {
                d[category + "s"] = R.Try(() =>
                {
                    var names = new List<string>(); R.Call(effect, "Get" + category + "Names", names); var list = new JArray();
                    foreach (string name in names)
                    {
                        var item = new JObject { ["name"] = name };
                        if (category == "ParticleSystem") item["info"] = R.Try(() => R.Public(R.Call(effect, "GetParticleSystemInfo", name)));
                        if (category == "SpawnSystem") item["info"] = R.Try(() =>
                        {
                            object state = R.Call(effect, "GetSpawnSystemInfo", name);
                            try { return R.Members(state, "playing", "spawnCount", "deltaTime", "totalTime", "loopState", "loopIndex", "loopCount", "loopDuration", "delayBeforeLoop", "delayAfterLoop"); }
                            finally { (state as IDisposable)?.Dispose(); }
                        });
                        list.Add(item);
                    }
                    return list;
                });
            }
            d["exposedProperties"] = R.Try(() =>
            {
                object asset = R.Get(effect, "visualEffectAsset"); var propType = R.Type("UnityEngine.VFX.VFXExposedProperty");
                if (propType == null) return R.Unknown("VFXExposedProperty type not loaded");
                var list = (IList)Activator.CreateInstance(typeof(List<>).MakeGenericType(propType)); R.Call(asset, "GetExposedProperties", list); var values = new JArray();
                foreach (object p in list)
                {
                    string name = (string)R.Get(p, "name"); var type = (Type)R.Get(p, "type");
                    string getter = type == typeof(float) ? "Float" : type == typeof(int) ? "Int" : type == typeof(uint) ? "UInt" : type == typeof(bool) ? "Bool" :
                        typeof(Texture).IsAssignableFrom(type) ? "Texture" : type.Name;
                    values.Add(new JObject { ["name"] = name, ["type"] = type.FullName, ["value"] = R.Try(() => R.Call(effect, "Get" + getter, name)) });
                }
                return values;
            });
            d["observedOutputMaterialSlots"] = Materials(renderer);
            d["outputMapping"] = R.Unknown("public VisualEffect API does not enumerate render-output contexts or map material slots to systems; OutputEvents are events, not render outputs");
            d["particleAttributes"] = R.Unknown("public VisualEffect API does not expose internal GPU particle positions, sizes, colours, velocity, lifetime, custom streams or flipbook selection");
            foreach (string field in new[] { "simulationSpace", "simulationTime", "cullingMode", "currentSizeRange", "currentColorRange", "velocityRange", "lifetimeRange", "flipbook", "customVertexStreams", "trails", "meshChoices" })
                d[field] = R.Unknown("not exposed per system/output by the public VisualEffect API; exposed graph properties and observed materials are listed separately");
            return d;
        }
        private static JObject Associations(Component effect, Renderer renderer, Light[] lights)
        {
            var attached = new JArray(); var nearby = new JArray();
            foreach (var light in lights)
            {
                bool child = light.transform.IsChildOf(effect.transform), parent = effect.transform.IsChildOf(light.transform);
                float distance = renderer != null ? Mathf.Sqrt(renderer.bounds.SqrDistance(light.transform.position)) : Vector3.Distance(effect.transform.position, light.transform.position);
                if (child || parent) attached.Add(new JObject { ["id"] = light.GetInstanceID(), ["relation"] = child ? "same object or descendant" : "light on ancestor", ["distance"] = distance });
                else if (distance <= NearbyMetres) nearby.Add(new JObject { ["id"] = light.GetInstanceID(), ["distance"] = distance, ["sameParent"] = light.transform.parent != null && light.transform.parent == effect.transform.parent });
            }
            return new JObject { ["attached"] = attached, ["nearby"] = nearby, ["ownership"] = "unknown: hierarchy and proximity are evidence only; particle light module recorded separately" };
        }
        private static JObject Light(Light l, bool traced)
        {
            var d = R.Members(l, "enabled", "isActiveAndEnabled", "type", "intensity", "color", "useColorTemperature", "colorTemperature", "range", "spotAngle", "innerSpotAngle", "shadows", "cullingMask", "renderingLayerMask", "cookie", "cookieSize", "lightUnit");
            var hd = l.GetComponent<HDAdditionalLightData>();
            d["hdrp"] = hd == null ? (JToken)"none: no HDAdditionalLightData" : R.Members(hd, "lightUnit", "intensity", "luxAtDistance", "lightDimmer", "affectDiffuse", "affectSpecular", "lightlayersMask", "affectsVolumetric", "volumetricDimmer", "volumetricShadowDimmer", "volumetricFadeDistance", "shadowDimmer", "fadeDistance", "shapeRadius", "shapeWidth", "shapeHeight", "applyRangeAttenuation");
            d["tracer"] = EffectsTracer.Light(l, traced); return d;
        }
        private static JObject Fog()
        {
            return new JObject { ["blendedCameraFog"] = R.Try(() =>
            {
                // Read an existing HDCamera only. GetOrCreate would violate inventory-only semantics.
                var camera = Camera.main;
                if (camera == null) return R.Unknown("no main camera");
                var get = typeof(HDCamera).GetMethod("TryGet", R.Any);
                if (get == null) return R.Unknown("HDCamera.TryGet unavailable");
                var parameters = get.GetParameters();
                var args = new object[parameters.Length]; args[0] = camera;
                for (int i = 2; i < args.Length; i++) args[i] = parameters[i].DefaultValue;
                if (!(bool)get.Invoke(null, args)) return R.Unknown("no existing HDCamera for main camera");
                var hd = args[1];
                var stack = R.Get(hd, "volumeStack") as VolumeStack;
                if (stack == null) return R.Unknown("no existing main-camera volume stack");
                return VolumeFog(stack.GetComponent<UnityEngine.Rendering.HighDefinition.Fog>());
            }), ["localVolumes"] = DynamicComponents("UnityEngine.Rendering.HighDefinition.LocalVolumetricFog", c =>
            {
                object parameters = R.Get(c, "parameters"); var p = R.Public(parameters);
                p["volumeMask"] = R.Member(parameters, "volumeMask");
                p["materialMask"] = R.Try(() => R.Material(R.Get(parameters, "materialMask") as Material));
                return new JObject { ["parameters"] = p, ["runtimeBounds"] = R.Members(c, "scaledSize", "effectiveScale"),
                    ["boundsMeaning"] = "Oriented bounds: transform plus parameters.size and scaleMode. Runtime bounds are HDRP cache fields; inactive volumes may have stale caches." };
            }), ["fogOverrides"] = Components<Volume>(v =>
            {
                // sharedProfile alone would miss runtime profile clones. Reading profile itself creates one.
                var profile = v.HasInstantiatedProfile() ? R.Get(v, "m_InternalProfile") as VolumeProfile : v.sharedProfile;
                var d = R.Members(v, "enabled", "isGlobal", "priority", "weight", "blendDistance");
                d["profile"] = R.Identity(profile); var fogs = new JArray();
                if (profile != null) foreach (var c in profile.components) if (c is UnityEngine.Rendering.HighDefinition.Fog fog) fogs.Add(VolumeFog(fog));
                d["fog"] = fogs; d["layer"] = v.gameObject.layer;
                d["colliderBounds"] = R.Try(() => new JArray(v.GetComponents<Component>().Where(c => c != null && c.GetType().Name.EndsWith("Collider", StringComparison.Ordinal)).Select(c => new JObject { ["identity"] = R.Identity(c), ["bounds"] = R.Member(c, "bounds") })));
                return d;
            }) };
        }
        private static JObject VolumeFog(UnityEngine.Rendering.HighDefinition.Fog fog)
        {
            var d = R.Identity(fog); if (fog == null) { d["state"] = R.Unknown("no Fog component"); return d; }
            d["active"] = fog.active;
            foreach (var f in fog.GetType().GetFields(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance))
                if (typeof(VolumeParameter).IsAssignableFrom(f.FieldType)) d[f.Name] = R.Try(() => { var p = f.GetValue(fog); return R.Members(p, "overrideState", "value"); });
            return d;
        }
        private static JObject Snapshot()
        {
            return new JObject { ["frame"] = Time.frameCount, ["realtime"] = Time.realtimeSinceStartup, ["time"] = Time.time, ["timeScale"] = Time.timeScale,
                ["shaderTimes"] = R.Try(() =>
                {
                    var cb = R.Get(RenderPipelineManager.currentPipeline, "m_ShaderVariablesGlobalCB");
                    return R.Members(cb, "_Time", "_LastTime", "_SinTime", "_CosTime", "unity_DeltaTime", "_TimeParameters", "_LastTimeParameters", "_FrameCount");
                }),
                ["shaderTimeCaution"] = "HDRP CPU constant-buffer mirror after rendering; custom material clocks/GPU graph time and whether each shader uses these values remain unknown",
                ["particles"] = Components<ParticleSystem>(ps => new JObject { ["systemTime"] = ps.time, ["current"] = CurrentParticles(ps, false) }),
                ["vfx"] = DynamicComponents("UnityEngine.VFX.VisualEffect", c => R.Members(c, "aliveParticleCount", "pause", "playRate", "culled")) };
        }
    }
}
