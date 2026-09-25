// SPDX-License-Identifier: MIT
using System;
using BepInEx.Configuration;
using BepInEx.Logging;
using TGGraphicsBridge;
using UnityEngine;
namespace TGBetterUpscaling
{
    internal static class Session
    {
        internal static Settings Settings;
        internal static ManualLogSource Log;
        internal static NativeUpscaler Upscaler;
        internal static UpscaleStatus Status;
        internal static bool HaveStatus;
        private static bool stopped = true;
        private static float nextPoll, nextDiscovery;
        private static string lastReport = "";
        internal static void Start(ConfigFile config, ManualLogSource log)
        {
            if (!stopped) return;
            NativeUpscaler.Validate();
            // Bound with BepInEx's saving off, then saved once through SafeConfig (Settings.Save): a config file
            // that can't be written must not stop the mod here, before it has logged anything.
            Log = log; config.SaveOnConfigSet = false; Settings = new Settings(config); Settings.Save(); Upscaler = new NativeUpscaler();
            stopped = false; nextPoll = nextDiscovery = 0;
            StpHost.Initialize();
            // The performance overlay's section; a bridge from before the overlay has no such call.
            try { PerformanceOverlay.AddSection("Upscaling", 10, UpscalingOverlay.Lines); }
            catch (Exception ex) { Log.LogWarning("No performance overlay section: " + ex.Message); }
            // The troubleshooting report's steps, measurement and section; a bridge from before it has none.
            try { UpscalingSweep.Register(); }
            catch (Exception ex) { Log.LogWarning("No troubleshooting report part: " + ex.Message); }
            Log.LogInfo("TG Better Upscaling " + UpscalingPlugin.Version + " loaded. Graphics API: " + SystemInfo.graphicsDeviceType +
                ". Upscaler " + Settings.Upscaler + " (" + Settings.File.ConfigFilePath + ").");
            // Started: the bridge otherwise reports that it loaded and didn't start.
            try { ClaimRow(); } catch { }
        }
        private static void ClaimRow() => RowAlerts.Claim(AlertRow.Upscaling);
        internal static string StatusText
        {
            get
            {
                if (!HaveStatus) return Upscaler?.Detail ?? "Not initialized";
                var s = Status;
                if (s.DeviceReady == 0) return "TG Graphics Bridge has no D3D12 device yet (" + GraphicsBridge.Describe() + ")";
                string Tech(uint backend, string name) => name + " " + (!s.Has(backend) ? "unavailable" : (s.Disabled & (1u << (int)backend)) != 0 ? "off after failures" : "available");
                string active = s.ActiveBackend == NativeFlags.BackendFsr ? "FSR (version " + ActiveFsrVersion() + ")"
                    : s.ActiveBackend == NativeFlags.BackendXess ? "XeSS (preset " + XessPreset(s.XessPreset) + ")" : "none";
                return Tech(NativeFlags.BackendFsr, "FSR") + " (" + s.FsrVersionCount + " versions), " + Tech(NativeFlags.BackendXess, "XeSS") + " " + s.XessVersionText +
                    " | active " + active +
                    " " + s.RenderWidth + "x" + s.RenderHeight + " -> " + s.OutputWidth + "x" + s.OutputHeight +
                    " | upscaled " + s.FramesUpscaled + ", refused " + s.FramesFailed + ", enlarged " + s.FramesFallback +
                    (s.LastResult != 0 ? " | last error 0x" + s.LastResult.ToString("X8") : "") + " | GPU vendor 0x" + s.GpuVendor.ToString("X4");
            }
        }
        internal static void Tick()
        {
            if (stopped) return;
            try
            {
                float now = Time.unscaledTime;
                UpscalerRow.Tick(now); // catches its own failures
                if (Settings.DumpNow.Value) { Settings.DumpNow.Value = false; Diagnostics.Write(); }
                if (now < nextPoll) return;
                nextPoll = now + 0.25f;
                if (!Upscaler.Connected && now >= nextDiscovery) { nextDiscovery = now + 5f; Upscaler.Discover(); }
                HaveStatus = Upscaler.ReadStatus(out Status);
                // The capabilities are settled by UpscalerRow once the game has created its Upscaling setting.
                // TG Graphics Bridge corrects the game's motion vectors while FSR or XeSS reads them.
                uint backend = StpHost.Hosting == UpscalerSetting.FSR ? NativeFlags.BackendFsr : NativeFlags.BackendXess;
                MotionVectors.Request(UpscalingPlugin.Name, StpHost.Active && HaveStatus && Status.Usable(backend));
                string report = StatusText + " | " + StpHost.Hosting;
                if (report != lastReport && (now - lastReportTime > 10f || !SameCounters(report, lastReport)))
                {
                    lastReport = report; lastReportTime = now;
                    Log.LogInfo(report);
                }
            }
            catch (Exception ex)
            {
                Log.LogError("TG Better Upscaling failed closed: " + ex);
                Stop();
            }
        }
        // The version FSR runs: the newest AMD's runtime reports for this GPU.
        internal static string ActiveFsrVersion()
        {
            var versions = Settings.Capabilities?.FsrVersions;
            uint active = Status.ActiveFsrVersion;
            return versions != null && active < versions.Length ? versions[active] : "#" + active;
        }
        // xess_quality_settings_t
        internal static string XessPreset(uint preset)
        {
            switch (preset)
            {
                case 100: return "Ultra Performance";
                case 101: return "Performance";
                case 102: return "Balanced";
                case 103: return "Quality";
                case 104: return "Ultra Quality";
                case 105: return "Ultra Quality Plus";
                case 106: return "Native Anti-Aliasing";
                default: return preset.ToString();
            }
        }
        private static float lastReportTime = -100f;
        // Only the frame counters changed: logged at most every 10 s.
        private static bool SameCounters(string a, string b)
        {
            int i = a.IndexOf(" | upscaled ", StringComparison.Ordinal), j = b.IndexOf(" | upscaled ", StringComparison.Ordinal);
            int ia = a.IndexOf(" | ", i + 1, StringComparison.Ordinal), jb = b.IndexOf(" | ", j + 1, StringComparison.Ordinal);
            if (i < 0 || j < 0 || ia < 0 || jb < 0) return false;
            return a.Substring(0, i) == b.Substring(0, j) && a.Substring(ia) == b.Substring(jb);
        }
        internal static void Stop()
        {
            if (stopped) return;
            stopped = true;
            MotionVectors.Request(UpscalingPlugin.Name, false);
            StpHost.Dispose();
        }
    }
}
