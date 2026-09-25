// SPDX-License-Identifier: MIT
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
namespace TGFrameGeneration
{
    internal static class Diagnostics
    {
        internal static void Write()
        {
            try
            {
                string folder=Path.Combine(Paths.BepInExRootPath,"TGFrameGeneration","diagnostics");
                Directory.CreateDirectory(folder);
                string path=Path.Combine(folder,DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + ".txt");
                File.WriteAllText(path,Build(true));
                Session.Log.LogInfo("Diagnostic report written: " + path + ". Review before sharing.");
            }
            catch(Exception ex) { Session.Log.LogError("Diagnostic report failed: " + ex); }
        }
        // The report's text; the troubleshooting report has the modules and assemblies in its own part.
        internal static string Build(bool everything)
        {
            var sb=new StringBuilder();
            sb.AppendLine("TGFrameGeneration " + Plugin.Version + " diagnostic report");
            sb.AppendLine("UTC: " + DateTime.UtcNow.ToString("O"));
            using(var identity=Process.GetCurrentProcess()) sb.AppendLine("Managed process PID: " + identity.Id);
#if BEPINEX_IL2CPP
            sb.AppendLine("Runtime target: IL2CPP / net6.0");
#else
            sb.AppendLine("Runtime target: Mono / net472");
#endif
            sb.AppendLine("Unity: " + Application.unityVersion);
            sb.AppendLine("Game version string: " + Application.version);
            sb.AppendLine("API: " + SystemInfo.graphicsDeviceType + " / " + SystemInfo.graphicsDeviceVersion);
            sb.AppendLine("GPU: " + SystemInfo.graphicsDeviceName);
            sb.AppendLine("Render size: " + Screen.width + "x" + Screen.height + " @ " + Screen.currentResolution.refreshRateRatio.value.ToString("0.##") + " Hz");
            var s=Session.Settings;
            sb.AppendLine("Video settings: frame generation " + s.FrameGeneration + ", mode " + s.Mode + ", multiplier " + s.Multiplier + "x, dynamic max " + s.MaxMultiplier +
                "x, target " + s.TargetFps + ", low latency " + s.LowLatency + ", base limit " + s.BaseLimit.Value + " (" + s.File.ConfigFilePath + ")");
            sb.AppendLine("Capabilities: " + (s.Capabilities == null ? "not reported yet" :
                "technologies " + string.Join(",", Array.ConvertAll(s.Capabilities.Backends, b => b.ToString())) +
                ", low latency " + string.Join(",", Array.ConvertAll(s.Capabilities.Latencies, l => l.ToString())) +
                ", frame generation inputs " + (s.Capabilities.InputSource ? "available" : "unavailable: no HDRP inputs and not an NVIDIA GPU")));
            sb.AppendLine("Bridge: " + Session.Bridge.Detail);
            sb.AppendLine("Frame bridge: " + FrameBridge.Detail + " | camera markers queued: " + FrameBridge.CamerasQueued);
            sb.AppendLine("HDRP inputs: " + HdrpInputs.Detail + " | packets: " + HdrpInputs.Packets);
            sb.AppendLine("HUD-less frame: " + HudlessUi.Detail);
            sb.AppendLine("Status: " + Session.StatusText);
            sb.AppendLine("Video tab: " + NativeVideoSettings.Status);
            sb.AppendLine("Native log: " + Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration", "Native", "TGFG.Native.log"));
            sb.AppendLine("Selected now: " + Session.Selected.Describe() + (Session.Override.HasValue ? " (a troubleshooting step)" : ""));
            if(!everything) return sb.ToString();
            GraphicsBridge.AppendDiagnostics(sb);
            sb.AppendLine("\nLoaded native module names (paths omitted):");
            using(var process=Process.GetCurrentProcess())
                foreach(ProcessModule m in process.Modules)
                    sb.AppendLine(m.ModuleName + " | " + m.FileVersionInfo.FileVersion);
            sb.AppendLine("\nLoaded assembly identities:");
            foreach(var assembly in AppDomain.CurrentDomain.GetAssemblies()) sb.AppendLine(assembly.FullName);
            return sb.ToString();
        }
    }
}
