// SPDX-License-Identifier: MIT
// The installed TG Path Tracing, driven and read from outside: its settings through its BepInEx config entries (as FoA
// Mod Manager changes them: the Video rows follow), its state by the names of its internal members. No build of it is
// referenced, so the runner measures whichever build is in the game folder; a member a build lacks reads as "n/a" or
// 0, and tests/source_checks.py (test_harness_reads_members_that_exist) fails when the tree renames one.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using BepInEx.Bootstrap;
using BepInEx.Configuration;
namespace TGPathTracing.Harness
{
    internal static class Traced
    {
        internal const string PluginId = "com.wessberg.tgpathtracing";
        private const BindingFlags Any = BindingFlags.Static | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        private static Assembly assembly;
        private static bool looked;
        private static readonly Dictionary<string, MemberInfo> members = new Dictionary<string, MemberInfo>();

        internal static bool Installed => Assembly() != null;
        private static Assembly Assembly()
        {
            if (looked) return assembly;
            looked = true;
            if (Chainloader.PluginInfos.TryGetValue(PluginId, out var info) && info != null && info.Instance != null) assembly = info.Instance.GetType().Assembly;
            return assembly;
        }
        private static MemberInfo Member(string type, string name)
        {
            string key = type + "." + name;
            if (members.TryGetValue(key, out var found)) return found;
            MemberInfo member = null;
            var t = Assembly() != null ? Assembly().GetType("TGPathTracing." + type) : null;
            if (t != null) member = (MemberInfo)t.GetProperty(name, Any) ?? t.GetField(name, Any);
            members[key] = member;
            return member;
        }
        private static object Value(MemberInfo member, object target)
        {
            try
            {
                if (member is PropertyInfo property) return property.GetValue(target, null);
                if (member is FieldInfo field) return field.GetValue(target);
            }
            catch { }
            return null;
        }
        // A static member of one of TG Path Tracing's classes.
        internal static object Static(string type, string name) => Value(Member(type, name), null);
        internal static string Text(string type, string name) => Convert.ToString(Static(type, name), CultureInfo.InvariantCulture) ?? "n/a";
        internal static bool Flag(string type, string name) => Static(type, name) is bool b && b;
        // Frozen-frame diagnostics need a fresh native snapshot. Session.Status updates only four times a second,
        // which would otherwise make consecutive screenshots falsely look like repeated traced frames.
        internal static void SampleFrameCounters(FrameInfo frame)
        {
            frame.ptTraced = frame.ptComposited = -1;
            try
            {
                object tracer = Static("Session", "Tracer");
                var read = tracer?.GetType().GetMethod("ReadStatus", Any);
                if (read == null) return;
                object[] args = { null };
                if (!(read.Invoke(tracer, args) is bool ok) || !ok || args[0] == null) return;
                frame.ptTraced = Convert.ToInt64(Value(Member("NativeStatus", "FramesTraced"), args[0]));
                frame.ptComposited = Convert.ToInt64(Value(Member("NativeStatus", "Composited"), args[0]));
            }
            catch { } // An older installed build may not expose the reader. -1 means unavailable, never a repeated frame.
        }
        // A field of the native side's memory diagnostics (TGPT_Diagnostics, read by the plugin four times a second), in bytes.
        internal static double Diagnostic(string field)
        {
            object diagnostics = Static("Session", "Diagnostics");
            if (diagnostics == null) return 0;
            object value = Value(Member("NativeDiagnostics", field), diagnostics);
            try { return value == null ? 0 : Convert.ToDouble(value, CultureInfo.InvariantCulture); } catch { return 0; }
        }
        // A field of the native side's status (TGPT_Status, read by the plugin four times a second).
        internal static double Status(string field)
        {
            if (!(Static("Session", "HaveStatus") is bool have) || !have) return 0;
            object status = Static("Session", "Status");
            if (status == null) return 0;
            object value = Value(Member("NativeStatus", field), status);
            try { return value == null ? 0 : Convert.ToDouble(value, CultureInfo.InvariantCulture); } catch { return 0; }
        }
        private static uint Constant(string name)
        {
            object value = Static("Tgpt", name);
            try { return value == null ? 0u : Convert.ToUInt32(value, CultureInfo.InvariantCulture); } catch { return 0u; }
        }
        internal static bool RayReconstructionActive => Flag("RayReconstruction", "Active");
        internal static bool NrdActive => ((uint)Status("Features") & Constant("FeatureNrdActive")) != 0;
        internal static bool RayRegenerationActive => ((uint)Status("Features") & Constant("FeatureRayRegenerationActive")) != 0;
        internal static string StageTimes()
        {
            try
            {
                object tracer = Static("Session", "Tracer");
                var method = tracer != null ? tracer.GetType().GetMethod("StageTimes", Any) : null;
                return method != null ? Convert.ToString(method.Invoke(tracer, null)) ?? "" : "";
            }
            catch { return ""; }
        }

        private static ConfigFile File(string plugin)
        {
            if (string.IsNullOrEmpty(plugin)) plugin = PluginId;
            return Chainloader.PluginInfos.TryGetValue(plugin, out var info) && info != null && info.Instance != null ? info.Instance.Config : null;
        }
        private static ConfigEntryBase Entry(Pair pair, out string why)
        {
            why = "";
            var file = File(pair.plugin);
            if (file == null) { why = "the plugin " + (string.IsNullOrEmpty(pair.plugin) ? PluginId : pair.plugin) + " is not loaded"; return null; }
            // Looked up every time: TG Path Tracing binds Denoising anew when the PC's denoisers become known.
            foreach (var definition in new List<ConfigDefinition>(file.Keys))
            {
                if (!string.Equals(definition.Key, pair.key, StringComparison.OrdinalIgnoreCase)) continue;
                if (!string.IsNullOrEmpty(pair.section) && !string.Equals(definition.Section, pair.section, StringComparison.OrdinalIgnoreCase)) continue;
                return file[definition];
            }
            why = "no config entry \"" + pair.key + "\"";
            return null;
        }
        // Sets one entry and says what it held and holds: BepInEx keeps a value the entry does not accept out (a denoiser
        // this PC does not run), which the caller reports.
        internal static bool Set(Pair pair, out string detail)
        {
            var entry = Entry(pair, out detail);
            if (entry == null) return false;
            string before = entry.GetSerializedValue();
            entry.SetSerializedValue(pair.value);
            // Read back through a fresh lookup: setting Denoising can make the plugin bind the entry anew.
            var now = Entry(pair, out _) ?? entry;
            string after = now.GetSerializedValue();
            detail = pair.key + ": " + before + " -> " + after;
            bool took = string.Equals(after, pair.value, StringComparison.OrdinalIgnoreCase);
            if (!took) detail += " (asked for " + pair.value + ")";
            return took;
        }
        internal static string Get(string key)
        {
            var entry = Entry(new Pair { key = key }, out _);
            return entry != null ? entry.GetSerializedValue() : "";
        }
        // Every entry of a plugin's config, "section/key=value": what the run found, and puts back when it ends.
        internal static List<KeyValuePair<ConfigDefinition, string>> Snapshot(string plugin)
        {
            var list = new List<KeyValuePair<ConfigDefinition, string>>();
            var file = File(plugin);
            if (file == null) return list;
            foreach (var definition in new List<ConfigDefinition>(file.Keys))
                list.Add(new KeyValuePair<ConfigDefinition, string>(definition, file[definition].GetSerializedValue()));
            return list;
        }
        internal static void Restore(string plugin, List<KeyValuePair<ConfigDefinition, string>> snapshot)
        {
            var file = File(plugin);
            if (file == null || snapshot == null) return;
            foreach (var pair in snapshot)
            {
                try
                {
                    if (!file.ContainsKey(pair.Key)) continue;
                    var entry = file[pair.Key];
                    if (entry.GetSerializedValue() != pair.Value) entry.SetSerializedValue(pair.Value);
                }
                catch { }
            }
        }
    }
}
