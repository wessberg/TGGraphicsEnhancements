// SPDX-License-Identifier: MIT
// Inspect the installed manager's own discovered rows, then capture the actual open menu.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using BepInEx.Bootstrap;
using Newtonsoft.Json;
using UnityEngine;
namespace TGPathTracing.Harness
{
    internal static class NeuralManagerAudit
    {
        private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        private static object Get(object target, string name) => target.GetType().GetProperty(name, Any)?.GetValue(target)
            ?? target.GetType().GetField(name, Any)?.GetValue(target);
        private static object Call(Type type, object instance, string name, params object[] args) => type.GetMethod(name, Any)?.Invoke(instance, args)
            ?? null;
        internal static IEnumerator Run(string dir, StepResult result)
        {
            Type api = null, pluginType = null;
            object manager = null;
            string selection = null;
            bool wasOpen = false, opened = false;
            HashSet<string> expanded = null;
            string sectionKey = null;
            bool wasExpanded = false;
            try
            {
                var assembly = AppDomain.CurrentDomain.GetAssemblies().Single(a => a.GetName().Name == "FoAModManager");
                api = assembly.GetType("FoAModManager.FoAModManagerApi", true);
                pluginType = assembly.GetType("FoAModManager.Plugin", true);
                manager = pluginType.GetField("_instance", Any).GetValue(null);
                selection = Convert.ToString(Get(manager, "_selectedPluginKey"));
                wasOpen = (bool)api.GetProperty("IsOpen", Any).GetValue(null);
                Call(api, null, "Refresh");
                var plugins = ((IEnumerable)Get(manager, "_plugins")).Cast<object>();
                var nr = plugins.Single(p => Convert.ToString(Get(p, "Guid")) == Neural.PluginId);
                Call(pluginType, manager, "SelectPlugin", Get(nr, "SelectionKey"));
                expanded = (HashSet<string>)Get(manager, "_expandedSettingSections");
                sectionKey = (string)Call(pluginType, null, "BuildSettingSectionStateKey", Neural.PluginId, "Video");
                wasExpanded = expanded.Contains(sectionKey);
                expanded.Add(sectionKey);
                Call(api, null, "Open");
                opened = true;
            }
            catch (Exception ex) { result.error += " FoA menu audit could not open: " + ex.Message; }
            try
            {
                if (!opened) yield break;
                // Let IMGUI build and draw the selected mod. This is outside performance measurements.
                yield return new WaitForSecondsRealtime(.5f);
                yield return new WaitForEndOfFrame();
                try
                {
                    var selected = Call(pluginType, manager, "GetSelectedPlugin");
                    if (!(bool)api.GetProperty("IsOpen", Any).GetValue(null) || Convert.ToString(Get(selected, "Guid")) != Neural.PluginId)
                        throw new InvalidOperationException("The requested NR menu is not open.");
                    var rows = ((IEnumerable)Get(selected, "Settings")).Cast<object>().Select(row => new {
                        name = Convert.ToString(Get(row, "DisplayName")), key = Convert.ToString(Get(row, "Key")),
                        section = Convert.ToString(Get(row, "DisplaySection")), hidden = (bool)Get(row, "Hidden"),
                        order = Convert.ToInt32(Get(row, "Order")), value = Convert.ToString(Get(row, "DraftValue")),
                        choices = Get(row, "ChoiceValues"), labels = Get(row, "ChoiceLabels"),
                        description = Convert.ToString(Get(row, "Description"))
                    }).ToArray();
                    File.WriteAllText(Path.Combine(dir, "foa-menu.json"), JsonConvert.SerializeObject(rows, Formatting.Indented));
                    var issues = ((IEnumerable)Get(selected, "Issues")).Cast<object>().Select(issue => new {
                        severity = Convert.ToString(Get(issue, "Severity")), message = Convert.ToString(Get(issue, "Message")),
                        detail = Convert.ToString(Get(issue, "Detail")), blocking = (bool)Get(issue, "IsBlocking")
                    }).ToArray();
                    File.WriteAllText(Path.Combine(dir, "foa-issues.json"), JsonConvert.SerializeObject(issues, Formatting.Indented));
                    var texture = ScreenCapture.CaptureScreenshotAsTexture();
                    try { File.WriteAllBytes(Path.Combine(dir, "foa-menu.png"), ImageConversion.EncodeToPNG(texture)); }
                    finally { UnityEngine.Object.Destroy(texture); }
                    var config = Chainloader.PluginInfos[Neural.PluginId].Instance.Config;
                    string Value(string key) => Convert.ToString(config[new BepInEx.Configuration.ConfigDefinition("Video", key)].BoxedValue);
                    var expected = new List<string> { "Neural Rendering" };
                    if (Value("Neural Rendering") == "On")
                    {
                        expected.AddRange(new[] { "Placement", "Strength", "Local Tone", "Advanced" });
                        if (Value("Advanced") == "On")
                        {
                            expected.AddRange(new[] { "Style", "Passes", "Detail", "Automatic Mask" });
                            if (Value("Automatic Mask") == "On") expected.Add("Skin Detail");
                        }
                    }
                    var visible = rows.Where(row => !row.hidden).OrderBy(row => row.order).ToArray();
                    if (!visible.Select(row => row.name).SequenceEqual(expected))
                        throw new InvalidOperationException("Visible FoA rows differ: " + string.Join(", ", visible.Select(row => row.name)));
                    if (visible.Any(row => row.section != "Video" || string.IsNullOrWhiteSpace(row.description)))
                        throw new InvalidOperationException("A visible NR row has no tooltip or the wrong section.");
                    var mode = rows.Single(row => row.key == "Neural Rendering");
                    var labels = (IDictionary<string, string>)mode.labels;
                    if (labels["Off"] != "Off" || labels["On"] != "DLSS") throw new InvalidOperationException("Wrong Neural Rendering labels.");
                    // The manager omits hidden entries from its catalogue entirely.
                    if (expected.Contains("Style"))
                    {
                        var styleLabels = (IDictionary<string, string>)rows.Single(row => row.key == "Model").labels;
                        if (!new[] { "A", "B", "C", "D" }.Select(key => styleLabels[key]).SequenceEqual(new[] { "Default", "A", "B", "C" }))
                            throw new InvalidOperationException("Wrong style labels.");
                    }
                }
                catch (Exception ex) { result.error += " FoA menu audit failed: " + ex.Message; }
            }
            finally
            {
                if (expanded != null && sectionKey != null && !wasExpanded) expanded.Remove(sectionKey);
                if (manager != null && selection != null) Call(pluginType, manager, "SelectPlugin", selection);
                if (opened && !wasOpen) Call(api, null, "Close");
            }
        }
    }
}
