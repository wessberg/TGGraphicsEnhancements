// SPDX-License-Identifier: MIT
// TG Frame Generation's section of the performance overlay (TG Graphics Bridge's PerformanceOverlay): the technology
// and its mode, the frame rate on screen against the rendered one (FrameRates), and the low latency technology.
// While the troubleshooting test runs, it shows the test step's choice rather than the player's.
using System.Collections.Generic;
using UnityEngine;
namespace TGFrameGeneration
{
    internal static class FrameGenerationOverlay
    {
        internal static IEnumerable<string> Lines()
        {
            var lines = new List<string>();
            var settings = Session.Settings;
            var selected = Session.Selected;
            var backend = selected.Backend;
            var mode = selected.Mode;
            if (!Session.HaveStatus)
            {
                lines.Add("Not running: " + Session.UnavailableShort()); // the full explanation is in the settings and the log
                return lines;
            }
            var status = Session.Status;
            if (mode == GenerationMode.Off || backend == FrameGenerationSetting.Off)
            {
                lines.Add("Off" + (Session.Override.HasValue ? " (troubleshooting test)" : ""));
                lines.Add("Low latency " + (status.ActiveLatency == 0 ? "off" : VideoCapabilities.Label((LowLatencySetting)status.ActiveLatency)));
                return lines;
            }
            string technology = backend == FrameGenerationSetting.DLSS ? "DLSS Frame Generation" : backend == FrameGenerationSetting.FSR ? "FSR Frame Generation" : "XeSS Frame Generation";
            int fixedTotal = Mathf.Min(selected.Multiplier, (int)status.MaxGenerated(backend) + 1);
            lines.Add(technology + " · " + (mode == GenerationMode.Dynamic
                ? "Dynamic, up to " + Mathf.Min(selected.MaxMultiplier, (int)status.MaxGenerated(backend) + 1) + "x, target " + selected.TargetFps.ToString("0") + " fps"
                : "Fixed " + fixedTotal + "x (" + (fixedTotal - 1) + " generated per rendered frame)") + (Session.Override.HasValue ? " (troubleshooting test)" : ""));
            if (Session.Suspension != Suspend.None) lines.Add("Paused: " + Session.Suspension);
            else if ((GenerationMode)status.LastAcceptedMode == GenerationMode.Off)
            {
                string why = "";
                try { why = Explain.WaitingShort(status); } catch { }
                lines.Add("Waiting: " + (why.Length > 0 ? why : status.Reason.ToString()));
            }
            else if (FrameRates.Rendered > 0f)
                lines.Add("On screen " + FrameRates.Output.ToString("0") + " fps · " + (FrameRates.Output / FrameRates.Rendered).ToString("0.0") + "x the rendered " + FrameRates.Rendered.ToString("0") + " fps");
            lines.Add("Low latency " + (status.ActiveLatency == 0 ? "off" : VideoCapabilities.Label((LowLatencySetting)status.ActiveLatency)) +
                (settings.BaseLimit.Value > 0f ? " · base frame cap " + settings.BaseLimit.Value.ToString("0") + " fps" : ""));
            return lines;
        }
    }
}
