// SPDX-License-Identifier: MIT
// A step's "upscaler": one of TG Better Upscaling's choices for this step, through its own test entry (the config keeps
// the player's choice). "end" puts the player's choice back.
using System;
using System.Collections;
using System.Linq;
using System.Reflection;
using BepInEx.Bootstrap;

namespace TGPathTracing.Harness
{
    internal static class UpscalerStep
    {
        private const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance;
        internal static bool Set(string name, out string detail)
        {
            if (!Chainloader.PluginInfos.TryGetValue("com.wessberg.tgbetterupscaling", out var plugin) || plugin.Instance == null) { detail = "TG Better Upscaling is not loaded"; return false; }
            var row = plugin.Instance.GetType().Assembly.GetType("TGBetterUpscaling.UpscalerRow", true);
            if (name == "end") { row.GetMethod("EndTest", Any).Invoke(null, null); detail = "the player's upscaler is back"; return true; }
            var choices = ((IEnumerable)row.GetProperty("Testable", Any).GetValue(null)).Cast<object>().ToArray();
            var choice = choices.FirstOrDefault(c => string.Equals(c.ToString(), name, StringComparison.OrdinalIgnoreCase));
            if (choice == null) { detail = "upscaler " + name + " is not offered (offered: " + string.Join(", ", choices.Select(c => c.ToString()).ToArray()) + ")"; return false; }
            row.GetMethod("Test", Any).Invoke(null, new[] { choice });
            detail = "upscaler = " + name;
            return true;
        }
    }
}
