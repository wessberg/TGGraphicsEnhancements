// SPDX-License-Identifier: MIT
// Read-only, bounded reflection: never serialize a Unity object graph or instantiate a material/profile.
using System;
using System.Collections;
using System.Linq;
using System.Reflection;
using Newtonsoft.Json.Linq;
using UnityEngine;
using UnityEngine.Rendering;
using UObj = UnityEngine.Object;

namespace TGPathTracing.Harness
{
    internal static class EffectsRead
    {
        internal const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        internal static string Unknown(string why) => "unknown: " + why;
        internal static object Get(object target, string name)
        {
            if (target == null) throw new InvalidOperationException("no object for " + name);
            var t = target as Type ?? target.GetType();
            for (; t != null; t = t.BaseType)
            {
                var p = t.GetProperty(name, Any | BindingFlags.DeclaredOnly);
                if (p != null) return p.GetValue(target is Type ? null : target, null);
                var f = t.GetField(name, Any | BindingFlags.DeclaredOnly);
                if (f != null) return f.GetValue(target is Type ? null : target);
            }
            throw new MissingMemberException((target as Type ?? target.GetType()).FullName, name);
        }
        internal static JToken Try(Func<object> read)
        {
            try { return Value(read()); }
            catch (Exception e) { return Unknown(e.GetBaseException().GetType().Name + ": " + e.GetBaseException().Message); }
        }
        internal static Type Type(string name) => AppDomain.CurrentDomain.GetAssemblies().Select(a => a.GetType(name, false)).FirstOrDefault(t => t != null);
        internal static JToken Member(object target, string name) => Try(() => Get(target, name));
        internal static JObject Members(object target, params string[] names)
        {
            var result = new JObject();
            foreach (string name in names) result[name] = Member(target, name);
            return result;
        }
        internal static object Call(object target, string name, params object[] args)
        {
            if (target == null) throw new InvalidOperationException("no object for " + name);
            var t = target as Type ?? target.GetType();
            var method = t.GetMethods(Any).FirstOrDefault(m => m.Name == name && m.GetParameters().Length == args.Length &&
                m.GetParameters().Select((p, i) => args[i] == null || p.ParameterType.IsInstanceOfType(args[i])).All(b => b));
            if (method == null) throw new MissingMethodException(t.FullName, name);
            return method.Invoke(target is Type ? null : target, args);
        }
        internal static JObject Identity(UObj obj)
        {
            if (obj == null) return new JObject { ["state"] = "none" };
            var r = new JObject { ["id"] = obj.GetInstanceID(), ["name"] = obj.name, ["type"] = obj.GetType().FullName };
            if (obj is Component c)
            {
                var names = new System.Collections.Generic.List<string>();
                for (var t = c.transform; t != null; t = t.parent) names.Add(t.name + "[" + t.GetSiblingIndex() + "]");
                names.Reverse();
                r["path"] = string.Join("/", names); r["scene"] = c.gameObject.scene.name; r["scenePath"] = c.gameObject.scene.path;
                r["sceneHandle"] = c.gameObject.scene.handle; r["activeSelf"] = c.gameObject.activeSelf; r["activeInHierarchy"] = c.gameObject.activeInHierarchy;
                r["enabled"] = c is Renderer rr ? (JToken)rr.enabled : c is Behaviour b ? (JToken)b.enabled : Unknown("component has no enabled property");
                r["transform"] = Members(c.transform, "position", "rotation", "lossyScale", "localToWorldMatrix");
                r["cameraDistance"] = Camera.main != null ? (JToken)Vector3.Distance(Camera.main.transform.position, c.transform.position) : Unknown("no main camera");
            }
            return r;
        }
        internal static JToken Value(object o, int depth = 0)
        {
            if (o == null) return JValue.CreateNull();
            if (o is JToken token) return token;
            if (o is string || o is bool || o is char || o is decimal || o.GetType().IsPrimitive) return JToken.FromObject(o);
            if (o is Enum) return o.ToString();
            if (o is Vector2 v2) return new JArray(v2.x, v2.y);
            if (o is Vector3 v3) return new JArray(v3.x, v3.y, v3.z);
            if (o is Vector4 v4) return new JArray(v4.x, v4.y, v4.z, v4.w);
            if (o is Quaternion q) return new JArray(q.x, q.y, q.z, q.w);
            if (o is Color c) return new JArray(c.r, c.g, c.b, c.a);
            if (o is Color32 c32) return new JArray(c32.r, c32.g, c32.b, c32.a);
            if (o is Bounds b) return new JObject { ["center"] = Value(b.center), ["size"] = Value(b.size) };
            if (o is Matrix4x4 mat) { var a = new JArray(); for (int i = 0; i < 16; i++) a.Add(mat[i]); return a; }
            if (o is Texture tex) return Texture(tex);
            if (o is UObj u) return Identity(u);
            if (depth > 12) return Unknown("nested value exceeds diagnostic depth: " + o.GetType().FullName);
            if (o is IEnumerable seq) { var a = new JArray(); foreach (object item in seq) a.Add(Value(item, depth + 1)); return a; }
            return Public(o, depth + 1);
        }
        internal static JObject Public(object o, int depth = 0)
        {
            var r = new JObject();
            if (o == null) { r["state"] = Unknown("null object"); return r; }
            foreach (var f in o.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
                r[f.Name] = SafeNested(() => f.GetValue(o), depth);
            foreach (var p in o.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance))
                if (p.CanRead && p.GetIndexParameters().Length == 0) r[p.Name] = SafeNested(() => p.GetValue(o, null), depth);
            if (!r.HasValues) r["state"] = Unknown("no public data members on " + o.GetType().FullName);
            return r;
        }
        private static JToken SafeNested(Func<object> read, int depth)
        {
            try { return Value(read(), depth + 1); } catch (Exception e) { return Unknown(e.GetBaseException().Message); }
        }
        internal static JObject Texture(Texture tex)
        {
            var r = Identity(tex);
            if (tex == null) return r;
            r["dimensions"] = Members(tex, "width", "height", "dimension", "graphicsFormat", "mipmapCount");
            // A 2D texture has no depth property; that is explicitly inapplicable, not an assumed depth.
            r["depth"] = tex.dimension == TextureDimension.Tex3D || tex.dimension == TextureDimension.Tex2DArray ? Member(tex, tex is RenderTexture ? "volumeDepth" : "depth") : "not applicable";
            r["format"] = Member(tex, "format");
            return r;
        }
        internal static JObject Material(Material m)
        {
            var r = Identity(m); if (m == null) return r;
            r["shader"] = m.shader != null ? m.shader.name : Unknown("no shader");
            r["renderQueue"] = m.renderQueue; r["keywords"] = new JArray(m.shaderKeywords);
            var passes = new JArray(); r["passes"] = passes;
            for (int i = 0; i < m.passCount; i++)
            {
                string name = m.GetPassName(i);
                passes.Add(new JObject { ["index"] = i, ["name"] = name, ["enabled"] = Try(() => m.GetShaderPassEnabled(name)) });
            }
            r["renderState"] = "Declared properties below; effective compiled pass blend/depth/cull state is unknown: runtime API does not expose shader code";
            r["unsupportedShaderOperations"] = Unknown("compiled shader operations cannot be enumerated by the runtime material API");
            var values = new JObject(); r["properties"] = values;
            if (m.shader != null) for (int i = 0; i < m.shader.GetPropertyCount(); i++)
            {
                string name = m.shader.GetPropertyName(i); var kind = m.shader.GetPropertyType(i);
                values[name] = Try(() =>
                {
                    switch (kind)
                    {
                        case ShaderPropertyType.Texture: return new JObject { ["texture"] = Texture(m.GetTexture(name)), ["scale"] = Value(m.GetTextureScale(name)), ["offset"] = Value(m.GetTextureOffset(name)) };
                        case ShaderPropertyType.Color: return Value(m.GetColor(name));
                        case ShaderPropertyType.Vector: return Value(m.GetVector(name));
                        case ShaderPropertyType.Int: return m.GetInteger(name);
                        default: return m.GetFloat(name);
                    }
                });
            }
            return r;
        }
        internal static bool Loaded(Component c) => c != null && c.gameObject.scene.IsValid() && c.gameObject.scene.isLoaded;
    }
}
