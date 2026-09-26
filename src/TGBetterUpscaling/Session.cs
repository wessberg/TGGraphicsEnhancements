// SPDX-License-Identifier: MIT
using System;
using BepInEx.Configuration;
using BepInEx.Logging;
using TGGraphicsBridge;
using UnityEngine;
using UnityEngine.Rendering;
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
        private static bool rrWanted; // TG Path Tracing wants Ray Reconstruction, and it can run
        private static bool rrReapply; // the game's Upscaling setting still has to be applied again for it
        private static bool rrPrepared; // NGX was asked to start for Ray Reconstruction
        private static float rrPreparedAt;
        // No failure yet: long enough ago. Not int.MinValue, whose distance to any frame overflows to a negative
        // number, so Ray Reconstruction never counted as succeeding and TG Path Tracing kept denoising itself.
        private static ulong rrFailedSeen; private static int rrFailedFrame = -1000;
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
        // Whether Ray Reconstruction's last frames were upscaled: read each frame it runs. After a refused frame,
        // TG Path Tracing denoises its own light again for half a second (it hands it over undenoised only while
        // Ray Reconstruction keeps succeeding).
        internal static bool RayReconstructionSucceeding()
        {
            if (!Upscaler.Connected || !Upscaler.ReadStatus(out var s)) return false;
            if (s.FramesFailed != rrFailedSeen) { rrFailedSeen = s.FramesFailed; rrFailedFrame = Time.frameCount; }
            return s.Usable(NativeFlags.BackendRayReconstruction) && Time.frameCount - rrFailedFrame > 30;
        }

        internal static string StatusText
        {
            get
            {
                if (!HaveStatus) return Upscaler?.Detail ?? "Not initialized";
                var s = Status;
                if (s.DeviceReady == 0) return "TG Graphics Bridge has no D3D12 device yet (" + GraphicsBridge.Describe() + ")";
                string Tech(uint backend, string name) => name + " " + (!s.Has(backend) ? "unavailable" : (s.Disabled & (1u << (int)backend)) != 0 ? "off after failures" : "available");
                string active = s.ActiveBackend == NativeFlags.BackendFsr ? "FSR (version " + ActiveFsrVersion() + ")"
                    : s.ActiveBackend == NativeFlags.BackendXess ? "XeSS (preset " + XessPreset(s.XessPreset) + ")"
                    : s.ActiveBackend == NativeFlags.BackendRayReconstruction ? "DLSS Ray Reconstruction" : "none";
                return Tech(NativeFlags.BackendFsr, "FSR") + " (" + s.FsrVersionCount + " versions), " + Tech(NativeFlags.BackendXess, "XeSS") + " " + s.XessVersionText +
                    // Ray Reconstruction only exists for TG Path Tracing: without it, the line reads as before.
                    (PathTracingLink.Found ? ", " + Tech(NativeFlags.BackendRayReconstruction, "DLSS Ray Reconstruction") : "") +
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
                uint backend = StpHost.RayReconstruction ? NativeFlags.BackendRayReconstruction
                    : StpHost.Hosting == UpscalerSetting.FSR ? NativeFlags.BackendFsr : NativeFlags.BackendXess;
                MotionVectors.Request(UpscalingPlugin.Name, StpHost.Active && HaveStatus && Status.Usable(backend));
                // TG Path Tracing starting or stopping to trace switches DLSS between Super Resolution and Ray
                // Reconstruction: the game applies its Upscaling setting again, which the hook sets up for either.
                // TG Path Tracing decides (its Denoising row); RenoDX's DLSS 5 works alongside Ray Reconstruction.
                bool pathTracing = PathTracingLink.Wanted;
                // NGX starts for Ray Reconstruction only once TG Path Tracing asks for it, on the render thread.
                if (pathTracing && !rrPrepared && HaveStatus && Status.GpuVendor == 0x10DE && !Status.Has(NativeFlags.BackendRayReconstruction))
                {
                    rrPrepared = true; rrPreparedAt = now;
                    var prepare = new CommandBuffer { name = "TG Better Upscaling: start NGX for Ray Reconstruction" };
                    prepare.IssuePluginEventAndData(Upscaler.RenderEvent, NativeFlags.EventPrepareRayReconstruction, IntPtr.Zero);
                    Graphics.ExecuteCommandBuffer(prepare);
                    prepare.Release();
                }
                // Whether NGX runs Ray Reconstruction here, for TG Path Tracing's Denoising row (the render thread has a
                // moment to start NGX after the request).
                if (rrPrepared && HaveStatus)
                {
                    if (Status.Has(NativeFlags.BackendRayReconstruction)) PathTracingLink.Report(true);
                    else if (now - rrPreparedAt > 3f) PathTracingLink.Report(false);
                }
                bool wanted = UpscalerRow.Current == UpscalerSetting.DLSS && !StpHost.Failed && pathTracing &&
                    HaveStatus && Status.Usable(NativeFlags.BackendRayReconstruction);
                if (wanted != rrWanted) { rrWanted = wanted; rrReapply = wanted != StpHost.RayReconstruction; }
                // Applied once the settings screen is closed, however long that takes.
                if (rrReapply && UpscalerRow.Reapply()) rrReapply = false;
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
