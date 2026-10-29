// SPDX-License-Identifier: MIT
// Read-only evidence for NR compatibility takes. No dependency on a particular FG build.
using System;
using System.Collections.Generic;
using System.Reflection;
using BepInEx.Bootstrap;
namespace TGPathTracing.Harness
{
    internal static class NeuralCompatibility
    {
        private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        internal static Dictionary<string, object> Upscaling()
        {
            if (!Chainloader.PluginInfos.TryGetValue("com.wessberg.tgbetterupscaling", out var plugin) || plugin.Instance == null) return null;
            var values = new Dictionary<string, object>();
            try
            {
                var session = plugin.Instance.GetType().Assembly.GetType("TGBetterUpscaling.Session");
                values["HaveStatus"] = session.GetField("HaveStatus", Any).GetValue(null);
                values["Description"] = session.GetProperty("StatusText", Any).GetValue(null);
                var status = session.GetField("Status", Any).GetValue(null);
                foreach (var field in status.GetType().GetFields(Any))
                    if (!field.IsStatic && field.FieldType.IsPrimitive) values[field.Name] = field.GetValue(status);
            }
            catch (Exception ex) { values["error"] = ex.ToString(); }
            return values;
        }
        internal static Dictionary<string, object> BridgePerformance()
        {
            var values = new Dictionary<string, object>();
            try
            {
                if (!Chainloader.PluginInfos.TryGetValue("com.wessberg.tggraphicsbridge", out var plugin) || plugin.Instance == null) return null;
                var bridge = plugin.Instance.GetType().Assembly.GetType("TGGraphicsBridge.GraphicsBridge");
                var args = new object[] { null };
                if (!(bridge.GetMethod("TryReadPerformance", Any)?.Invoke(null, args) is bool read) || !read) return null;
                foreach (var field in args[0].GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
                    if (field.FieldType.IsPrimitive) values[field.Name] = field.GetValue(args[0]);
            }
            catch (Exception ex) { values["error"] = ex.ToString(); }
            return values;
        }
        internal static Dictionary<string, object> FrameGeneration()
        {
            if (!Chainloader.PluginInfos.TryGetValue("com.wessberg.tgframegeneration", out var plugin) || plugin.Instance == null) return null;
            var values = new Dictionary<string, object>();
            try
            {
                var session = plugin.Instance.GetType().Assembly.GetType("TGFrameGeneration.Session");
                foreach (var name in new[] { "HaveStatus", "StatusTime", "Suspension" })
                {
                    var value = session?.GetField(name, Any)?.GetValue(null);
                    values[name] = value is Enum ? value.ToString() : value;
                }
                if (!(values["HaveStatus"] is bool ready) || !ready) return values;
                var status = session.GetField("Status", Any)?.GetValue(null);
                if (status == null) return values;
                foreach (var field in status.GetType().GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance))
                    if (field.FieldType.IsPrimitive) values[field.Name] = field.GetValue(status);
                values["Reason"] = status.GetType().GetProperty("Reason", Any)?.GetValue(status)?.ToString();
            }
            catch (Exception ex) { values["error"] = ex.ToString(); }
            return values;
        }
    }
}
