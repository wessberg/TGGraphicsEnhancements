// SPDX-License-Identifier: MIT
// TG Frame Generation's part of the troubleshooting report (TG Graphics Bridge's Troubleshooting):
//   - the test steps: frame generation off with each low latency technology this GPU offers, then each
//     frame generation technology with each low latency technology it runs with (Off where that is
//     allowed), at 2x, and once more at the highest multiplier where a technology offers more;
//   - while another mod's steps run, frame generation is off; at the end the player's settings apply;
//   - each step's measurement: frames on screen, the presenting technology and latency technology, why
//     it isn't generating if it isn't, and where the presenting thread's time goes (native timings);
//   - the report section (this mod's diagnostic report and what its native side knows) and its logs.
// A step is a Selection override: the settings are never written, so the Video rows, FoA Mod Manager
// and the config file keep the player's choices whatever happens during the test.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
namespace TGFrameGeneration
{
    internal static class FrameGenerationSweep
    {
        private static float began;
        private static ulong sourceAt, outputAt;
        private static bool statusAtBegin;
        internal static void Register()
        {
            Troubleshooting.AddSweep(Plugin.Name, "each frame generation and low latency setting", 20, Steps, Neutral, Restore);
            Troubleshooting.AddMeasure("Frame generation", Begin, End);
            Troubleshooting.AddReport(Plugin.Name + " " + Plugin.Version, 20, Report, Files);
        }
        private static IList<SweepStep> Steps()
        {
            var steps = new List<SweepStep>();
            var caps = Session.Settings.Capabilities;
            if (caps == null) { Session.Log.LogInfo("Troubleshooting: no frame generation steps. " + Explain.Unavailable()); return steps; }
            if (Screen.fullScreenMode == FullScreenMode.ExclusiveFullScreen)
            {
                Session.Log.LogInfo("Troubleshooting: no frame generation steps, the game runs in Exclusive Fullscreen, where frame generation can't run.");
                return steps;
            }
            foreach (var latency in caps.AllowedLatencies(FrameGenerationSetting.Off))
                steps.Add(Step("Frame generation off, low latency " + VideoCapabilities.Label(latency), FrameGenerationSetting.Off, 2, latency));
            foreach (var backend in caps.Backends.Where(b => b != FrameGenerationSetting.Off))
            {
                foreach (var latency in caps.AllowedLatencies(backend))
                    steps.Add(Step(VideoCapabilities.Label(backend) + " Frame Generation 2x, low latency " + VideoCapabilities.Label(latency), backend, 2, latency));
                int max = caps.MaxMultiplier(backend);
                if (max > 2)
                {
                    var latency = caps.PreferredLatency(backend);
                    steps.Add(Step(VideoCapabilities.Label(backend) + " Frame Generation " + max + "x, low latency " + VideoCapabilities.Label(latency), backend, max, latency));
                }
            }
            return steps;
        }
        private static SweepStep Step(string name, FrameGenerationSetting backend, int multiplier, LowLatencySetting latency)
        {
            var selection = new Selection
            {
                Backend = backend, Mode = backend == FrameGenerationSetting.Off ? GenerationMode.Off : GenerationMode.Fixed,
                Multiplier = multiplier, MaxMultiplier = multiplier, TargetFps = 0f, Latency = latency
            };
            return new SweepStep { Name = name, Apply = () => Apply(name, selection), Settle = backend == FrameGenerationSetting.Off ? 2f : 3f };
        }
        private static void Apply(string name, Selection selection)
        {
            Session.Override = selection;
            Session.Bridge?.Note("Troubleshooting: " + name + " applied");
        }
        // While another mod's steps run: frame generation off, with the player's low latency choice where Off allows it.
        private static void Neutral()
        {
            var caps = Session.Settings.Capabilities;
            var latency = Session.Settings.LowLatency;
            if (caps != null && !caps.AllowedLatencies(FrameGenerationSetting.Off).Contains(latency)) latency = caps.PreferredLatency(FrameGenerationSetting.Off);
            Session.Override = new Selection { Backend = FrameGenerationSetting.Off, Mode = GenerationMode.Off, Multiplier = 2, MaxMultiplier = 2, Latency = latency };
            // A measurement cut short (a pause, a stop) never reached End: the native summaries resume their 5 s cadence.
            Session.Bridge?.Note("Troubleshooting: frame generation off while another mod's steps run", endWindow: true);
        }
        private static void Restore()
        {
            Session.Override = null;
            Session.Bridge?.Note("Troubleshooting: the player's frame generation settings", endWindow: true);
        }
        private static void Begin()
        {
            began = Time.unscaledTime;
            statusAtBegin = Session.HaveStatus;
            sourceAt = Session.HaveStatus ? Session.Status.SourcePresents : 0;
            outputAt = Session.HaveStatus ? Session.Status.OutputFramesReported : 0;
            Session.Bridge?.ReadTimings(true, out _);
            Session.Bridge?.Note("Troubleshooting: measuring " + Session.Selected.Describe(), endWindow: true, hold: true);
        }
        private static string End()
        {
            if (!Session.HaveStatus || !statusAtBegin) return "no native status (" + Session.StatusText + ")";
            var s = Session.Status;
            var selection = Session.Selected;
            float seconds = Mathf.Max(0.001f, Time.unscaledTime - began);
            float rendered = (s.SourcePresents - sourceAt) / seconds;
            bool generating = (GenerationMode)s.LastAcceptedMode != GenerationMode.Off && selection.Generating;
            var sb = new StringBuilder();
            if (generating)
            {
                float shown = selection.Backend == FrameGenerationSetting.FSR ? rendered * (s.FsrMaxGenerated + 1) : (s.OutputFramesReported - outputAt) / seconds;
                sb.Append("on screen ").Append(shown.ToString("0.0", CultureInfo.InvariantCulture)).Append(" fps (")
                  .Append(rendered > 0f ? (shown / rendered).ToString("0.0", CultureInfo.InvariantCulture) : "?").Append("x of ")
                  .Append(rendered.ToString("0.0", CultureInfo.InvariantCulture)).Append(" presented")
                  .Append(selection.Backend == FrameGenerationSetting.FSR ? "; FSR doesn't report its frames, so rendered times " + (s.FsrMaxGenerated + 1) : "").Append(')');
            }
            else
            {
                sb.Append("not generating");
                string waiting = Explain.Waiting(s);
                if (selection.Generating) sb.Append(" (").Append(waiting.Length > 0 ? waiting : s.Reason.ToString()).Append(')');
                sb.Append(", ").Append(rendered.ToString("0.0", CultureInfo.InvariantCulture)).Append(" fps presented");
            }
            sb.Append(", output ").Append(s.ActiveBackend == 2 ? "FSR" : s.ActiveBackend == 3 ? "XeSS" : "standard/DLSS-G")
              .Append(", low latency ").Append(VideoCapabilities.Label((LowLatencySetting)s.ActiveLatency));
            if (Session.Bridge != null && Session.Bridge.ReadTimings(false, out var timing)) sb.Append("; ").Append(timing.Describe());
            if (s.BackendFailedMask != 0) sb.Append("; failed this session: 0x").Append(s.BackendFailedMask.ToString("X")).Append(" (error ").Append(s.BackendResult).Append(')');
            string text = sb.ToString();
            Session.Bridge?.Note("Troubleshooting: done: " + text, endWindow: true);
            return text;
        }
        private static string Report()
        {
            var sb = new StringBuilder();
            sb.AppendLine(Diagnostics.Build(false));
            sb.AppendLine("Frame generation check: " + FrameRateCheck.Last);
            if (Session.Settings.Capabilities == null) sb.AppendLine("Why frame generation is unavailable: " + Explain.Unavailable());
            string dlss = Explain.DlssMissing(Session.HaveStatus ? Session.Status.GpuVendor : 0);
            if (dlss.Length > 0) sb.AppendLine(dlss);
            sb.AppendLine("Native facts:");
            foreach (var fact in (Session.Bridge?.Facts() ?? new Dictionary<string, string>()).OrderBy(f => f.Key)) sb.AppendLine("  " + fact.Key + " = " + fact.Value);
            return sb.ToString();
        }
        private static IEnumerable<string> Files()
        {
            string native = Path.Combine(Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration"), "Native");
            return new[] { Path.Combine(native, "TGFG.Native.log"), Path.Combine(native, "sl.log") };
        }
    }
}
