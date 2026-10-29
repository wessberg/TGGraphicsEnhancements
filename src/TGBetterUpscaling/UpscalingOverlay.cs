// SPDX-License-Identifier: MIT
// TG Better Upscaling's section of the performance overlay (TG Graphics Bridge's PerformanceOverlay): the upscaler,
// its version and quality mode, and for DLSS the DLL the game loaded and the model NVIDIA's DLL uses.
using System.Collections.Generic;
using Awaken.TG.Main.Settings.Graphics;
namespace TGBetterUpscaling
{
    internal static class UpscalingOverlay
    {
        internal static IEnumerable<string> Lines()
        {
            var lines = new List<string>();
            var settings = Session.Settings;
            var upscaler = UpscalerRow.Current; // a troubleshooting step's while one runs
            var mode = settings.Mode(upscaler);
            string modeText = mode != null ? mode.Name + " (" + mode.Scale.ToString("0.#") + "%)" : "at the game's " + GameScale() + " render scale";
            // The game's own FSR 2 upscales where AMD's runtime reported no FSR to this mod (game builds from October 2026).
            bool gameFsr = upscaler == UpscalerSetting.FSR && settings.Capabilities != null && !settings.Capabilities.Fsr && settings.Capabilities.GameFsr != null;
            switch (upscaler)
            {
                case UpscalerSetting.None:
                    lines.Add("Off: the game renders at the output resolution");
                    break;
                case UpscalerSetting.STP:
                    lines.Add("STP (the game's own) at " + GameScale() + " render scale");
                    break;
                case UpscalerSetting.DLSS when StpHost.RayReconstruction:
                    lines.Add("DLSS Ray Reconstruction " + modeText + " · denoises TG Path Tracing's light");
                    break;
                case UpscalerSetting.DLSS:
                {
                    int source = Session.Upscaler.DlssDll(out ulong loaded, out ulong requested, out ulong bundled);
                    bool overridden = Session.Upscaler.TryDlssOverride(out var driver);
                    lines.Add("DLSS " + modeText + " · preset " + DlssPreset(mode, loaded, overridden ? driver : (DlssOverride?)null));
                    if (overridden && DriverMode(driver) is string forced) lines.Add(forced);
                    lines.Add(source == NativeUpscaler.DlssBundled ? "DLSS " + NativeUpscaler.VersionText(loaded) + " (bundled; the game folder has " + NativeUpscaler.VersionText(requested) + ")"
                        : source == NativeUpscaler.DlssKept ? "DLSS " + NativeUpscaler.VersionText(loaded) + (loaded >= bundled
                            ? " (the game folder's, not older than the bundled " + NativeUpscaler.VersionText(bundled) + ")"
                            : " (the game folder's, kept with the DLSS mod's set it came with)")
                        : "DLSS DLL not loaded yet");
                    break;
                }
                case UpscalerSetting.FSR when gameFsr:
                    lines.Add(settings.Capabilities.GameFsr + " (the game's own FSR 2) at " + GameScale() + " render scale");
                    break;
                case UpscalerSetting.FSR:
                    lines.Add("FSR " + (Session.HaveStatus ? Session.ActiveFsrVersion() : "?") + " " + modeText + " · sharpness " + (settings.Sharpness * 100f).ToString("0") + "%");
                    break;
                case UpscalerSetting.XeSS:
                    lines.Add("XeSS " + (Session.HaveStatus ? Session.Status.XessVersionText : "?") + " " + modeText +
                        (Session.HaveStatus && Session.Status.XessPreset != 0 ? " · runs " + Session.XessPreset(Session.Status.XessPreset) : ""));
                    break;
            }
            if (((upscaler == UpscalerSetting.FSR && !gameFsr) || upscaler == UpscalerSetting.XeSS || StpHost.RayReconstruction) && Session.HaveStatus && Session.Status.OutputWidth > 0)
            {
                var s = Session.Status;
                lines.Add("Upscales " + s.RenderWidth + "x" + s.RenderHeight + " → " + s.OutputWidth + "x" + s.OutputHeight +
                    (StpHost.Active ? "" : " (not upscaling now: STP stands in)"));
            }
            return lines;
        }
        private static string GameScale()
        {
            var up = UpscalerRow.Game();
            return up != null ? up.QualityScaling.ToString("0.#") + "%" : "?";
        }
        // NGX has no query for the model it uses. A preset override in the NVIDIA driver's profile for the
        // game (the NVIDIA App's DLSS override) replaces it in the driver; without one, and with no preset
        // hint from the game (it sets none), it is the DLL's default for the quality level, which the
        // header of the DLSS SDK release that DLL comes from names: DLSS 310.9 uses K for DLAA, Quality and
        // Balanced, M for Performance, L for Ultra Performance.
        private static string DlssPreset(QualityMode mode, ulong loaded, DlssOverride? driver)
        {
            uint preset = driver?.PresetOverride ?? 0;
            if (preset == 0xFFFFFF) return "the latest (NVIDIA driver override)";
            if (preset >= 1 && preset <= 26) return ((char)('A' + preset - 1)) + " (NVIDIA driver override)";
            bool known = (loaded >> 48) == 310 && ((loaded >> 32) & 0xFFFF) == 9;
            if (mode == null || !known) return "the DLL's default";
            string letter = mode.Id == QualityModes.DlssPerformance ? "M" : mode.Id == QualityModes.DlssUltraPerformance ? "L" : "K";
            return letter + " (DLSS 310.9's default for " + mode.Name + ")";
        }
        // A performance mode, render scale or DLAA the driver's profile forces over the game's
        // (NGX_DLSS_SR_MODE, NGX_DLSS_SR_OVERRIDE_SCALING_RATIO, NGX_DLAA_OVERRIDE). The render size shown in
        // the Performance section is what the game renders at, which DLSS receives either way.
        private static string DriverMode(DlssOverride driver)
        {
            if (driver.DlaaOverride == 1) return "The NVIDIA driver forces DLAA";
            if (driver.ScalingRatio >= 33 && driver.ScalingRatio <= 100 && (driver.ModeOverride == 6 || driver.ModeOverride == uint.MaxValue))
                return "The NVIDIA driver forces a " + driver.ScalingRatio + "% render scale";
            switch (driver.ModeOverride)
            {
                case 0: return "The NVIDIA driver forces Performance";
                case 1: return "The NVIDIA driver forces Balanced";
                case 2: return "The NVIDIA driver forces Quality";
                case 4: return "The NVIDIA driver forces DLAA";
                case 5: return "The NVIDIA driver forces Ultra Performance";
                case 6: return "The NVIDIA driver forces a custom render scale" + (driver.ScalingRatio >= 33 && driver.ScalingRatio <= 100 ? " of " + driver.ScalingRatio + "%" : "");
                default: return null; // 3: the game's own mode; not set
            }
        }
    }
}
