// SPDX-License-Identifier: MIT
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using BepInEx;
using UnityEngine;
namespace TGFrameGeneration
{
    internal static class Diagnostics
    {
        internal static void Write()
        {
            try
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
                    ", DLSS inputs " + (s.Capabilities.InputSource ? "available" : "unavailable (not an NVIDIA GPU)")));
                sb.AppendLine("Bridge: " + Session.Bridge.Detail);
                sb.AppendLine("Frame bridge: " + FrameBridge.Detail + " | camera markers queued: " + FrameBridge.CamerasQueued);
                sb.AppendLine("Status: " + Session.StatusText);
                sb.AppendLine("Video tab: " + NativeVideoSettings.Status);
                AppendAutoloadDiagnostic(sb);
                sb.AppendLine("\nLoaded native module names (paths omitted):");
                using(var process=Process.GetCurrentProcess())
                    foreach(ProcessModule m in process.Modules)
                        sb.AppendLine(m.ModuleName + " | " + m.FileVersionInfo.FileVersion);
                sb.AppendLine("\nLoaded assembly identities:");
                foreach(var assembly in AppDomain.CurrentDomain.GetAssemblies()) sb.AppendLine(assembly.FullName);
                string folder=Path.Combine(Paths.BepInExRootPath,"TGFrameGeneration","diagnostics");
                Directory.CreateDirectory(folder);
                string path=Path.Combine(folder,DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + ".txt");
                File.WriteAllText(path,sb.ToString());
                Session.Log.LogInfo("Diagnostic report written: " + path + ". Review before sharing.");
            }
            catch(Exception ex) { Session.Log.LogError("Diagnostic report failed: " + ex); }
        }
        private static void AppendAutoloadDiagnostic(StringBuilder sb)
        {
            string root = Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration");
            sb.AppendLine("\nAutomatic startup: BepInEx preloader; no TGFG launcher required.");
            sb.AppendLine("Autoload log: " + Path.Combine(root, "Autoload.log"));
            sb.AppendLine("Native log: " + Path.Combine(root, "Native", "TGFG.Native.log"));
            sb.AppendLine("Install manifest present: " + File.Exists(Path.Combine(root, "Install-manifest.json")));
            sb.AppendLine("Autoload disabled by marker: " + File.Exists(Path.Combine(root, "disable-autoload.txt")));
            try
            {
                string path = Path.Combine(root, "Autoload.log");
                if (!File.Exists(path))
                {
                    sb.AppendLine("No autoload log. Check patcher installation and BepInEx preloader errors.");
                    return;
                }
                // Bounded read: an append-only log must not exhaust memory in the report writer.
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                                                   FileShare.ReadWrite | FileShare.Delete))
                {
                    long offset = Math.Max(0, stream.Length - 32768);
                    stream.Seek(offset, SeekOrigin.Begin);
                    var bytes = new byte[(int)Math.Min(32768, stream.Length - offset)];
                    int used = 0;
                    while (used < bytes.Length)
                    {
                        int read = stream.Read(bytes, used, bytes.Length - used);
                        if (read == 0) break;
                        used += read;
                    }
                    string tail = Encoding.UTF8.GetString(bytes, 0, used);
                    if (offset != 0)
                    {
                        int newline = tail.IndexOf('\n');
                        tail = newline >= 0 ? tail.Substring(newline + 1) : "";
                    }
                    var lines = tail.Split(new[] { '\n' });
                    sb.AppendLine("Autoload log tail (may include earlier sessions; compare PIDs and UTC):");
                    for (int i = Math.Max(0, lines.Length - 40); i < lines.Length; i++)
                        sb.AppendLine(lines[i].TrimEnd('\r'));
                }
            }
            catch (Exception ex) { sb.AppendLine("Autoload log read failed: " + ex.Message); }
        }
    }
}
