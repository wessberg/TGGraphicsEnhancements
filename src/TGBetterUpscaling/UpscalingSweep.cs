// SPDX-License-Identifier: MIT
// TG Better Upscaling's part of the troubleshooting report (TG Graphics Bridge's Troubleshooting): one
// test step per upscaler the game's Upscaling row offers (Off, STP, DLSS, FSR, XeSS as this GPU has
// them), each at the player's own quality mode for it, selected through the game's row and applied the
// way the game applies it; the config keeps the player's choice, which the end of the test puts back.
// Each step's measurement is what the performance overlay shows for upscaling; the report section is
// this mod's diagnostic report, and its native log goes into the zip.
using System;
using System.Collections.Generic;
using System.Linq;
using TGGraphicsBridge;
namespace TGBetterUpscaling
{
    internal static class UpscalingSweep
    {
        internal static void Register()
        {
            Troubleshooting.AddSweep(UpscalingPlugin.Name, "each upscaler", 10, Steps, Restore, Restore);
            Troubleshooting.AddMeasure("Upscaling", null, Measure);
            Troubleshooting.AddReport(UpscalingPlugin.Name + " " + UpscalingPlugin.Version, 10, () => Diagnostics.Build(false), () => new[] { Diagnostics.NativeLog });
        }
        private static IList<SweepStep> Steps()
        {
            var steps = new List<SweepStep>();
            foreach (var upscaler in UpscalerRow.Testable)
            {
                var mode = Session.Settings.Mode(upscaler);
                string name = upscaler == UpscalerSetting.None ? "Upscaling off" : "Upscaling " + Capabilities.Label(upscaler) + (mode != null ? " " + mode.Name : "");
                var chosen = upscaler;
                steps.Add(new SweepStep { Name = name, Apply = () => UpscalerRow.Test(chosen), Settle = 3f });
            }
            if (steps.Count == 0) Session.Log.LogInfo("Troubleshooting: no upscaling steps (" + UpscalerRow.Status + ")");
            return steps;
        }
        private static void Restore() => UpscalerRow.EndTest();
        private static string Measure() => string.Join("; ", UpscalingOverlay.Lines().ToArray());
    }
}
