// SPDX-License-Identifier: MIT
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
namespace TGBetterUpscaling
{
    internal static class Diagnostics
    {
        internal static string NativeLog => Path.Combine(Paths.BepInExRootPath, "TGBetterUpscaling", "Native", "TGBetterUpscaling.Native.log");
        internal static void Write()
        {
            try
            {
                string folder = Path.Combine(Paths.BepInExRootPath, "TGBetterUpscaling", "diagnostics");
                Directory.CreateDirectory(folder);
                string path = Path.Combine(folder, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + ".txt");
                File.WriteAllText(path, Build(true));
                Session.Log.LogInfo("Diagnostic report written: " + path + ". Review before sharing.");
            }
            catch (Exception ex) { Session.Log.LogError("Diagnostic report failed: " + ex); }
        }
        // The report's text; the troubleshooting report carries the logs and modules in its own parts.
        internal static string Build(bool everything)
        {
            var sb = new StringBuilder();
            sb.AppendLine("TGBetterUpscaling " + UpscalingPlugin.Version + " diagnostic report");
            sb.AppendLine("UTC: " + DateTime.UtcNow.ToString("O"));
            using (var identity = Process.GetCurrentProcess()) sb.AppendLine("Managed process PID: " + identity.Id);
#if BEPINEX_IL2CPP
            sb.AppendLine("Runtime target: IL2CPP / net6.0");
#else
            sb.AppendLine("Runtime target: Mono / net472");
#endif
            sb.AppendLine("Unity: " + Application.unityVersion + " | game " + Application.version);
            sb.AppendLine("API: " + SystemInfo.graphicsDeviceType + " / " + SystemInfo.graphicsDeviceVersion + " | reversed Z " + SystemInfo.usesReversedZBuffer);
            sb.AppendLine("GPU: " + SystemInfo.graphicsDeviceName);
            sb.AppendLine("Screen: " + Screen.width + "x" + Screen.height);
            var s = Session.Settings;
            sb.AppendLine("Settings: upscaler " + s.Upscaler + ", FSR sharpness " + s.Sharpness.ToString("0.00") +
                ", auto exposure " + s.AutoExposure.Value + ", input checker " + s.FsrInputChecker.Value + ", debug view " + s.FsrDebugView.Value + " (" + s.File.ConfigFilePath + ")");
            foreach (var u in Settings.WithModes)
            {
                var mode = s.Mode(u);
                if (mode != null) sb.AppendLine("Quality mode for " + u + ": " + mode);
            }
            var caps = s.Capabilities;
            sb.AppendLine("Capabilities: " + (caps == null ? "not reported yet" :
                "STP " + caps.Stp + ", DLSS " + caps.Dlss + ", FSR " + caps.Fsr + ", XeSS " + caps.Xess + ", FSR versions offered " + string.Join(", ", caps.FsrVersions) +
                ", the game's own FSR 2 " + (caps.GameFsr != null ? "offered (\"" + caps.GameFsr + "\")" : "not offered")));
            sb.AppendLine("Native: " + Session.Upscaler.Detail);
            sb.AppendLine("Status: " + Session.StatusText);
            sb.AppendLine("STP host: " + StpHost.Describe());
            sb.AppendLine("Video tab: " + UpscalerRow.Status);
            sb.AppendLine("Overlay: " + string.Join(" | ", new System.Collections.Generic.List<string>(UpscalingOverlay.Lines()).ToArray()));
            sb.AppendLine("Native log: " + NativeLog);
            if (!everything) return sb.ToString();
            GraphicsBridge.AppendTail(sb, NativeLog, "Native log", 60);
            GraphicsBridge.AppendDiagnostics(sb);
            sb.AppendLine("\nLoaded native module names (paths omitted):");
            using (var process = Process.GetCurrentProcess())
                foreach (ProcessModule m in process.Modules)
                    sb.AppendLine(m.ModuleName + " | " + m.FileVersionInfo.FileVersion);
            return sb.ToString();
        }
    }
}
