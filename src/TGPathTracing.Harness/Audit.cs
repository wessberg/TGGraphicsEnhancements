// SPDX-License-Identifier: MIT
// The state HDRP renders from, written as sorted "key<tab>value" lines, to compare the game before path tracing was
// ever on with the game after it was turned off again: every light (its intensity, dimmers, what it lights), every
// active volume's overrides and the values HDRP's volume stack ends up with for the main camera (fog, indirect light,
// ambient occlusion, reflections, exposure...), the camera's frame settings, the custom passes, every material's
// queue, keywords, switched-off passes and surface values, the water surfaces and the global shader keywords. It asks
// Unity and HDRP, never TG Path Tracing's own records of what it changed: a value the mod forgot to put back shows.
// Keys starting with "info|" are for reading, not for comparing (positions, counts, what is inactive).
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
namespace TGPathTracing.Harness
{
    internal static class Audit
    {
        private static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
        private const BindingFlags Fields = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        private static readonly string[] LightData =
        {
            "lightDimmer", "volumetricDimmer", "affectsVolumetric", "affectDiffuse", "affectSpecular", "shadowDimmer", "volumetricShadowDimmer",
            "fadeDistance", "shadowFadeDistance", "volumetricFadeDistance", "shapeRadius", "shapeWidth", "shapeHeight", "applyRangeAttenuation",
            "includeForRayTracing", "includeForPathTracing", "interactsWithSky", "angularDiameter", "innerSpotPercent", "luxAtDistance", "shadowNearPlane"
        };
        private static readonly string[] CameraData =
        {
            "antialiasing", "dithering", "stopNaNs", "allowDynamicResolution", "allowDeepLearningSuperSampling", "allowFidelityFX2SuperResolution",
            "customRenderingSettings", "clearColorMode", "taaSharpenStrength", "TAAQuality", "taaHistorySharpening", "fullscreenPassthrough", "hasPersistentHistory"
        };
        private static readonly string[] Passes =
        {
            "Forward", "ForwardOnly", "GBuffer", "DepthOnly", "DepthForwardOnly", "MotionVectors", "ShadowCaster", "TransparentDepthPrepass",
            "TransparentDepthPostpass", "TransparentBackface", "DistortionVectors", "RayTracingPrepass", "META", "SceneSelectionPass"
        };
        private static readonly string[] SurfaceValues =
        {
            "_SurfaceType", "_BlendMode", "_AlphaCutoffEnable", "_AlphaCutoff", "_AlphaClip", "_Cutoff", "_ZWrite", "_CullMode", "_SrcBlend", "_DstBlend",
            "_TransparentDepthPrepassEnable", "_TransparentDepthPostpassEnable", "_TransparentBackfaceEnable", "_EnableFogOnTransparent", "_AlphaToMask",
            "_StencilRef", "_StencilRefGBuffer", "_StencilRefDepth", "_ZTestGBuffer", "_ZTestDepthEqualForOpaque", "_ReceivesSSR", "_SupportDecals"
        };
        private static readonly Dictionary<Type, KeyValuePair<string, FieldInfo[]>[]> parameterFields = new Dictionary<Type, KeyValuePair<string, FieldInfo[]>[]>();
        private static readonly Dictionary<string, PropertyInfo> properties = new Dictionary<string, PropertyInfo>();
        private static Type[] componentTypes;

        internal static int Write(string path)
        {
            var lines = new List<string>(32768);
            Part(lines, "lights", Lights);
            Part(lines, "volumes", Volumes);
            Part(lines, "stack", Stack);
            Part(lines, "camera", CameraState);
            Part(lines, "passes", CustomPasses);
            Part(lines, "materials", Materials);
            Part(lines, "water", Water);
            Part(lines, "globals", Globals);
            lines.Sort(StringComparer.Ordinal);
            File.WriteAllLines(path, lines.ToArray(), new UTF8Encoding(false));
            return lines.Count;
        }
        private static void Part(List<string> lines, string name, Action<List<string>> write)
        {
            int before = lines.Count;
            try { write(lines); }
            catch (Exception ex) { lines.Add(name + "|error\t" + ex.GetType().Name + ": " + ex.Message.Replace('\n', ' ').Replace('\t', ' ')); }
            lines.Add("info|" + name + "|lines\t" + (lines.Count - before));
        }
        private static void Add(List<string> lines, string key, object value) => lines.Add(key + "\t" + Format(value));

        private static string Format(object value)
        {
            switch (value)
            {
                case null: return "null";
                case string s: return s.Replace('\n', ' ').Replace('\t', ' ');
                case bool b: return b ? "true" : "false";
                case float f: return f.ToString("G6", Inv);
                case double d: return d.ToString("G8", Inv);
                case Color c: return "(" + c.r.ToString("G5", Inv) + ", " + c.g.ToString("G5", Inv) + ", " + c.b.ToString("G5", Inv) + ", " + c.a.ToString("G5", Inv) + ")";
                case Vector2 v: return "(" + v.x.ToString("G5", Inv) + ", " + v.y.ToString("G5", Inv) + ")";
                case Vector3 v: return "(" + v.x.ToString("G5", Inv) + ", " + v.y.ToString("G5", Inv) + ", " + v.z.ToString("G5", Inv) + ")";
                case Vector4 v: return "(" + v.x.ToString("G5", Inv) + ", " + v.y.ToString("G5", Inv) + ", " + v.z.ToString("G5", Inv) + ", " + v.w.ToString("G5", Inv) + ")";
                case UnityEngine.Object o: return o == null ? "null" : o.GetType().Name + ":" + o.name;
                case AnimationCurve curve:
                {
                    var sb = new StringBuilder("curve");
                    foreach (var key in curve.keys) sb.Append(' ').Append(key.time.ToString("G4", Inv)).Append(':').Append(key.value.ToString("G4", Inv));
                    return sb.ToString();
                }
                case IFormattable formattable: return formattable.ToString(null, Inv);
                default: return (value.ToString() ?? "").Replace('\n', ' ').Replace('\t', ' ');
            }
        }
        // A public property by name, where this HDRP has it.
        private static bool Read(object target, string name, out object value)
        {
            value = null;
            var type = target.GetType();
            string key = type.FullName + "." + name;
            if (!properties.TryGetValue(key, out var property))
            {
                try { property = type.GetProperty(name, BindingFlags.Instance | BindingFlags.Public); } catch { property = null; }
                properties[key] = property;
            }
            if (property == null || !property.CanRead) return false;
            try { value = property.GetValue(target, null); return true; } catch { return false; }
        }
        private static string PathOf(Transform t, int levels)
        {
            var parts = new List<string>();
            for (int i = 0; t != null && i < levels; ++i, t = t.parent) parts.Add(t.name);
            parts.Reverse();
            return string.Join("/", parts.ToArray());
        }

        private static void Lights(List<string> lines)
        {
            int count = 0;
            foreach (var light in Resources.FindObjectsOfTypeAll<Light>())
            {
                if (light == null || !light.gameObject.scene.IsValid()) continue;
                ++count;
                string id = "light|" + PathOf(light.transform, 4) + "#" + light.GetInstanceID() + "|";
                Add(lines, "info|" + id + "position", light.transform.position);
                Add(lines, id + "on", light.isActiveAndEnabled);
                Add(lines, id + "type", light.type);
                Add(lines, id + "intensity", light.intensity);
                Add(lines, id + "color", light.color);
                Add(lines, id + "temperature", light.useColorTemperature ? light.colorTemperature : 0f);
                Add(lines, id + "range", light.range);
                Add(lines, id + "shadows", light.shadows);
                Add(lines, id + "cullingMask", light.cullingMask);
                Add(lines, id + "cookie", light.cookie);
                var data = light.GetComponent<HDAdditionalLightData>();
                if (data == null) continue;
                foreach (string name in LightData)
                    if (Read(data, name, out var value)) Add(lines, id + name, value);
            }
            lines.Add("info|lights|count\t" + count);
        }

        // A component's parameters by the names of the fields that hold them, and those of the objects it groups them in
        // (a Cloud Layer's two layers, each with its own "cast shadows"): the way to each as its fields from the component.
        private static KeyValuePair<string, FieldInfo[]>[] Parameters(Type type)
        {
            if (parameterFields.TryGetValue(type, out var found)) return found;
            var list = new List<KeyValuePair<string, FieldInfo[]>>();
            for (var t = type; t != null && t != typeof(VolumeComponent) && t != typeof(object); t = t.BaseType)
                foreach (var field in t.GetFields(Fields))
                {
                    if (typeof(VolumeParameter).IsAssignableFrom(field.FieldType)) { list.Add(new KeyValuePair<string, FieldInfo[]>(field.Name, new[] { field })); continue; }
                    var group = field.FieldType;
                    if (!group.IsClass || group.IsArray || group == typeof(string) || typeof(UnityEngine.Object).IsAssignableFrom(group) || typeof(Delegate).IsAssignableFrom(group)) continue;
                    foreach (var inner in group.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic))
                        if (typeof(VolumeParameter).IsAssignableFrom(inner.FieldType))
                            list.Add(new KeyValuePair<string, FieldInfo[]>(field.Name + "." + inner.Name, new[] { field, inner }));
                }
            found = list.ToArray();
            parameterFields[type] = found;
            return found;
        }
        private static VolumeParameter ParameterAt(object component, FieldInfo[] way)
        {
            object at = component;
            foreach (var field in way)
            {
                if (at == null) return null;
                try { at = field.GetValue(at); } catch { return null; }
            }
            return at as VolumeParameter;
        }
        private static string ParameterValue(VolumeParameter parameter)
        {
            if (parameter == null) return "null";
            try
            {
                var property = parameter.GetType().GetProperty("value", BindingFlags.Instance | BindingFlags.Public);
                if (property != null) return Format(property.GetValue(parameter, null));
            }
            catch { }
            return Format(parameter.ToString());
        }
        private static void Volumes(List<string> lines)
        {
            int active = 0;
            foreach (var volume in Resources.FindObjectsOfTypeAll<Volume>())
            {
                if (volume == null || !volume.gameObject.scene.IsValid()) continue;
                string id = "volume|" + PathOf(volume.transform, 3) + "#" + volume.GetInstanceID() + "|";
                if (!volume.isActiveAndEnabled) { lines.Add("info|" + id + "inactive\ttrue"); continue; }
                ++active;
                Add(lines, id + "global", volume.isGlobal);
                Add(lines, id + "weight", volume.weight);
                Add(lines, id + "priority", volume.priority);
                Add(lines, id + "blendDistance", volume.blendDistance);
                // volume.profile would make a copy of a shared profile: only read the one a volume already has.
                var profile = volume.HasInstantiatedProfile() ? volume.profile : volume.sharedProfile;
                if (profile == null) { lines.Add(id + "profile\tnull"); continue; }
                Add(lines, id + "profile", profile.name);
                foreach (var component in profile.components)
                {
                    if (component == null) continue;
                    string c = id + component.GetType().Name + ".";
                    Add(lines, c + "active", component.active);
                    foreach (var pair in Parameters(component.GetType()))
                    {
                        var parameter = ParameterAt(component, pair.Value);
                        if (parameter != null && parameter.overrideState) lines.Add(c + pair.Key + "\t" + ParameterValue(parameter));
                    }
                }
            }
            lines.Add("info|volumes|active\t" + active);
        }
        private static Type[] ComponentTypes()
        {
            if (componentTypes != null) return componentTypes;
            var list = new List<Type>();
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                Type[] types;
                try { types = assembly.GetTypes(); }
                catch (ReflectionTypeLoadException ex) { types = ex.Types; }
                catch { continue; }
                if (types == null) continue;
                foreach (var type in types)
                    if (type != null && !type.IsAbstract && typeof(VolumeComponent).IsAssignableFrom(type)) list.Add(type);
            }
            list.Sort((a, b) => string.CompareOrdinal(a.FullName, b.FullName));
            componentTypes = list.ToArray();
            return componentTypes;
        }
        // What every volume adds up to for the main camera: the values HDRP rendered the last frame with.
        private static void Stack(List<string> lines)
        {
            var camera = Camera.main;
            if (camera == null) { lines.Add("stack|error\tno main camera"); return; }
            var stack = HDCamera.GetOrCreate(camera).volumeStack;
            if (stack == null) { lines.Add("stack|error\tno volume stack"); return; }
            foreach (var type in ComponentTypes())
            {
                VolumeComponent component;
                try { component = stack.GetComponent(type); } catch { continue; }
                if (component == null) continue;
                string c = "stack|" + type.Name + ".";
                foreach (var pair in Parameters(type))
                    lines.Add(c + pair.Key + "\t" + ParameterValue(ParameterAt(component, pair.Value)));
            }
        }

        private static void CameraState(List<string> lines)
        {
            var camera = Camera.main;
            if (camera == null) { lines.Add("camera|error\tno main camera"); return; }
            Add(lines, "camera|name", camera.name);
            Add(lines, "camera|fieldOfView", camera.fieldOfView);
            Add(lines, "camera|near", camera.nearClipPlane);
            Add(lines, "camera|far", camera.farClipPlane);
            Add(lines, "camera|allowHDR", camera.allowHDR);
            Add(lines, "camera|cullingMask", camera.cullingMask);
            Add(lines, "camera|useOcclusionCulling", camera.useOcclusionCulling);
            var data = camera.GetComponent<HDAdditionalCameraData>();
            if (data != null)
                foreach (string name in CameraData)
                    if (Read(data, name, out var value)) Add(lines, "camera|data." + name, value);
            var hd = HDCamera.GetOrCreate(camera);
            foreach (FrameSettingsField field in Enum.GetValues(typeof(FrameSettingsField)))
            {
                if ((int)field < 0) continue;
                bool on;
                try { on = hd.frameSettings.IsEnabled(field); } catch { continue; }
                lines.Add("camera|frame." + field + "\t" + (on ? "true" : "false"));
            }
        }

        private static void CustomPasses(List<string> lines)
        {
            foreach (var volume in Resources.FindObjectsOfTypeAll<CustomPassVolume>())
            {
                if (volume == null || !volume.gameObject.scene.IsValid()) continue;
                string id = "pass|" + PathOf(volume.transform, 3) + "#" + volume.GetInstanceID() + "|";
                if (!volume.isActiveAndEnabled) { lines.Add("info|" + id + "inactive\ttrue"); continue; }
                Add(lines, id + "global", volume.isGlobal);
                Add(lines, id + "injectionPoint", volume.injectionPoint);
                Add(lines, id + "priority", volume.priority);
                var sb = new StringBuilder();
                if (volume.customPasses != null)
                    foreach (var pass in volume.customPasses)
                        if (pass != null) sb.Append(pass.GetType().Name).Append(':').Append(pass.name).Append(pass.enabled ? ":on " : ":off ");
                lines.Add(id + "passes\t" + sb.ToString().Trim());
            }
        }

        // One line a material: the shader, the queue, the keywords, the passes switched off and the values that decide how
        // its surface is drawn; a character's material (hair, eyes, skin) with every number its shader has.
        private static void Materials(List<string> lines)
        {
            int count = 0;
            var sb = new StringBuilder(512);
            foreach (var material in Resources.FindObjectsOfTypeAll<Material>())
            {
                if (material == null) continue;
                var shader = material.shader;
                if (shader == null) continue;
                string shaderName = shader.name;
                if (shaderName.StartsWith("Hidden/", StringComparison.Ordinal)) continue;
                ++count;
                sb.Length = 0;
                sb.Append("shader=").Append(shaderName).Append("; queue=").Append(material.renderQueue.ToString(Inv));
                var keywords = material.shaderKeywords;
                Array.Sort(keywords, StringComparer.Ordinal);
                sb.Append("; keywords=").Append(string.Join(" ", keywords));
                sb.Append("; off=");
                foreach (string pass in Passes)
                    if (!material.GetShaderPassEnabled(pass)) sb.Append(pass).Append(' ');
                sb.Append("; ");
                if (shaderName.StartsWith("TG/Character", StringComparison.Ordinal))
                {
                    int n = shader.GetPropertyCount();
                    for (int i = 0; i < n; i++)
                    {
                        var type = shader.GetPropertyType(i);
                        if (type != ShaderPropertyType.Float && type != ShaderPropertyType.Range && type != ShaderPropertyType.Int) continue;
                        string name = shader.GetPropertyName(i);
                        sb.Append(name).Append('=').Append(material.GetFloat(name).ToString("G6", Inv)).Append(' ');
                    }
                }
                else
                    foreach (string name in SurfaceValues)
                        if (material.HasFloat(name)) sb.Append(name).Append('=').Append(material.GetFloat(name).ToString("G6", Inv)).Append(' ');
                // Its glow as HDRP adds it, and its lightmap flags (4: Baked Emission off), which the tracer once took for "no glow".
                if (material.HasProperty("_EmissiveColor"))
                {
                    var e = material.GetColor("_EmissiveColor");
                    if (e.maxColorComponent > 0f)
                        sb.Append("_EmissiveColor=(").Append(e.r.ToString("G4", Inv)).Append(',').Append(e.g.ToString("G4", Inv)).Append(',').Append(e.b.ToString("G4", Inv)).Append(") ");
                    sb.Append("gi=").Append(((int)material.globalIlluminationFlags).ToString(Inv)).Append(' ');
                }
                lines.Add("material|" + material.name.Replace('\t', ' ').Replace('\n', ' ') + "#" + material.GetInstanceID() + "\t" + sb.ToString().TrimEnd());
            }
            lines.Add("info|materials|count\t" + count);
        }

        private static void Water(List<string> lines)
        {
            foreach (var surface in Resources.FindObjectsOfTypeAll<WaterSurface>())
            {
                if (surface == null || !surface.gameObject.scene.IsValid()) continue;
                string id = "water|" + surface.name + "#" + surface.GetInstanceID() + "|";
                Add(lines, id + "on", surface.isActiveAndEnabled);
                Add(lines, id + "caustics", surface.caustics);
                Add(lines, id + "causticsIntensity", surface.causticsIntensity);
                Add(lines, id + "causticsResolution", surface.causticsResolution);
                Add(lines, id + "underWater", surface.underWater);
                Add(lines, id + "customMaterial", surface.customMaterial);
            }
        }

        private static void Globals(List<string> lines)
        {
            var names = new List<string>();
            foreach (var keyword in Shader.enabledGlobalKeywords) names.Add(keyword.name);
            names.Sort(StringComparer.Ordinal);
            lines.Add("global|shaderKeywords\t" + string.Join(" ", names.ToArray()));
            Add(lines, "global|timeScale", Time.timeScale);
            Add(lines, "global|vSyncCount", QualitySettings.vSyncCount);
            Add(lines, "global|targetFrameRate", Application.targetFrameRate);
            Add(lines, "global|lodBias", QualitySettings.lodBias);
            Add(lines, "global|maximumLODLevel", QualitySettings.maximumLODLevel);
            Add(lines, "global|shadowDistance", QualitySettings.shadowDistance);
            Add(lines, "global|globalTextureMipmapLimit", QualitySettings.globalTextureMipmapLimit);
            Add(lines, "global|realtimeReflectionProbes", QualitySettings.realtimeReflectionProbes);
            Add(lines, "global|ambientIntensity", RenderSettings.ambientIntensity);
            Add(lines, "global|sun", RenderSettings.sun);
        }
    }
}
