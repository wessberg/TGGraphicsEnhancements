// SPDX-License-Identifier: MIT
// What the player reads when frame generation doesn't run or a technology is missing: the first thing
// that went wrong, from the installation to the game's swapchain, with what to do about it. The facts
// come from TG Graphics Bridge (its install check, the Direct3D 12 runtime and device, Windows) and
// from this mod's native side (Streamline, DLSS-G, FSR, XeSS and Anti-Lag 2 as each runtime reported
// them). The tooltips, the performance overlay and the troubleshooting report all use these.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using TGGraphicsBridge;
namespace TGFrameGeneration
{
    internal static class Explain
    {
        private static string Get(Dictionary<string, string> facts, string key) => facts != null && facts.ContainsKey(key) ? facts[key] : "";
        private static Dictionary<string, string> BridgeFacts()
        {
            try { return GraphicsBridge.Facts(); } catch { return new Dictionary<string, string>(); } // a bridge from before the facts
        }
        private static string Windows(Dictionary<string, string> facts)
        {
            string product = Get(facts, "windows.product"), build = Get(facts, "windows.build");
            return product.Length > 0 ? product + (build.Length > 0 ? " build " + build : "") : "this Windows";
        }
        // One reason at three lengths: Short for the performance overlay ("installation problem: wrong
        // folder"), Short and Fix for the Frame Generation row's tooltip, which the game shows as a narrow
        // column that runs off the screen past a few lines, and Full for the log and the report.
        internal struct Why { internal string Short, Fix, Full; }
        private static Why Because(string brief, string fix, string full) => new Why { Short = brief, Fix = fix, Full = full };
        // Why frame generation isn't available in this session, as sentences.
        internal static string Unavailable() => UnavailableWhy().Full;
        internal static Why UnavailableWhy()
        {
            string install = "", label = "";
            try { install = InstallCheck.ProblemFor("TGFrameGeneration"); label = InstallCheck.ProblemLabelFor("TGFrameGeneration"); } catch { }
            if (install.Length > 0)
                return Because("installation problem" + (label.Length > 0 ? ": " + label.ToLowerInvariant() : ""), InstallFix(),
                    "TG Frame Generation isn't installed correctly. " + install);
            foreach (var folder in new[] { "TGGraphicsBridge", "TGFrameGeneration" })
                if (File.Exists(Path.Combine(Path.Combine(BepInEx.Paths.BepInExRootPath, folder), "disable-autoload.txt")))
                    return Because("switched off by disable-autoload.txt", "Delete BepInEx/" + folder + "/disable-autoload.txt.",
                        "It's switched off by BepInEx/" + folder + "/disable-autoload.txt. Delete that file and restart the game.");
            bool optiScaler = false;
            try { optiScaler = Compatibility.OptiScaler; } catch { }
            var why = Technical();
            // OptiScaler running is the likelier cause of whatever failed after it.
            if (optiScaler)
                return Because("OptiScaler is running", "Remove OptiScaler from the game folder.",
                    "OptiScaler is running in the game, and it conflicts with this mod: remove it from the game folder. " + why.Full);
            return why;
        }
        // The install problem's own short fix, as the install check wrote it for the tooltip.
        private static string InstallFix()
        {
            // In a method of its own, for a bridge from before the alerts (see NativeVideoSettings.RegisterAlerts).
            try { return InstallFixFromBridge(); } catch { return ""; }
        }
        private static string InstallFixFromBridge() =>
            Notices.For(AlertRow.FrameGeneration).FirstOrDefault(n => n.Id.StartsWith("install.", StringComparison.Ordinal))?.Summary ?? "";
        // The first step that is missing, from the native DLL to the game's swapchain.
        private static Why Technical()
        {
            if (!GraphicsBridge.Loaded)
                return Because("its preloader didn't run", "Reinstall the mod into the game folder.",
                    "Its native part didn't load: BepInEx didn't run its preloader, BepInEx/patchers/TGFrameGeneration.Autoload.dll. " +
                    "Check that the file is there; BepInEx/TGGraphicsBridge/Autoload.log says more.");
            if (!GraphicsBridge.TryReadStatus(out var bridge) || bridge.Bootstrapped != 1)
                return Because("the graphics bridge didn't start", "BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log says why.",
                    "TG Graphics Bridge, which the mod runs on, didn't start. BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log says why.");
            var native = Session.Bridge?.Facts() ?? new Dictionary<string, string>();
            if (!GraphicsBridge.IsExtensionActive(Plugin.Name))
            {
                string streamline = Get(native, "streamline");
                // What the native side found at startup: a Streamline that loads later (RenoDX's DLSS 5) runs beside the mod's.
                if (streamline.StartsWith("foreign", StringComparison.Ordinal))
                    return Because("another mod runs NVIDIA Streamline", "Remove the mod that brings sl.interposer.dll.",
                        "Another mod already runs NVIDIA Streamline in the game (sl.interposer.dll" +
                        (streamline.Length > 8 ? ", from " + streamline.Substring(8) : "") + "), and a second copy can't run beside it. Frame generation needs that mod removed.");
                return Because("its native part didn't start", "BepInEx/TGFrameGeneration/Native/TGFG.Native.log says why.",
                    "Its native part didn't start. BepInEx/TGFrameGeneration/Native/TGFG.Native.log says why" +
                    (Get(native, "failure.fatal").Length > 0 ? ": " + Get(native, "failure.fatal") : "."));
            }
            if (bridge.SwapchainsSeen == 0) return Because("the game has no window yet", "", "The game hasn't created its window yet.");
            var facts = BridgeFacts();
            if (bridge.D3D12DeviceReady == 0) return D3D12(facts, bridge.LastError);
            if (bridge.SwapchainsProvided == 0)
            {
                string failure = Get(native, "failure");
                if (UnityEngine.Screen.fullScreenMode == UnityEngine.FullScreenMode.ExclusiveFullScreen)
                    return Because("Exclusive Fullscreen", "Choose Borderless Fullscreen and restart the game.",
                        "Frame generation can't run in Exclusive Fullscreen. Choose Borderless Fullscreen in the Video settings and restart the game.");
                return Because("it couldn't take over the game's presentation", "BepInEx/TGFrameGeneration/Native/TGFG.Native.log says why.",
                    "It couldn't take over the game's presentation" + (failure.Length > 0 ? " (" + failure + ")" : "") +
                    ". BepInEx/TGFrameGeneration/Native/TGFG.Native.log says more.");
            }
            return Because("waiting for the GPU's report", "", "Its native part hasn't reported what this GPU supports yet.");
        }
        // The shared Direct3D 12 device could not be created.
        private static Why D3D12(Dictionary<string, string> facts, int lastError)
        {
            string error = Get(facts, "d3d12.error");
            if (error.Length == 0 && lastError != 0) error = "0x" + lastError.ToString("X8");
            string brief = "no Direct3D 12 device" + (error.Length > 0 ? " (" + error + ")" : "");
            string device = Get(facts, "d3d12.device");
            if (device == "other adapter")
                return Because("Direct3D 12 came up on another GPU", "Set the game to one GPU in Windows' graphics settings.",
                    "The Direct3D 12 device came up on another GPU than the one the game renders on, so it can't be used. " +
                    "If this PC has two GPUs, set Fall of Avalon to the same one in Windows Settings > System > Display > Graphics.");
            // 0x887E0003 is the Agility SDK mismatch this game has on older Windows, but Windows returns it for a
            // missing or unloadable D3D12Core.dll too: the mismatch is named only when the bridge found it.
            string agility = Get(facts, "agility.state");
            bool mismatch = agility == "too-late" || agility == "corrected" || agility == "failed";
            if (string.Equals(error, "0x887E0003", StringComparison.OrdinalIgnoreCase) && !mismatch)
                return Because(brief, "Verify the game's files in Steam and install the latest Windows updates.",
                    "Windows refused the Direct3D 12 runtime (error 0x887E0003): the game's D3D12Core.dll (in its D3D12 folder) is missing, can't load, or doesn't " +
                    "match what the game asks for (" + (agility.Length > 0 ? "the bridge found: " + agility : "the bridge found nothing to correct") + "; " + Windows(facts) +
                    "). Verify the game's files in Steam, install the latest Windows updates, then restart the game.");
            if (string.Equals(error, "0x887E0003", StringComparison.OrdinalIgnoreCase))
            {
                string state = agility;
                string text = "Windows refused to create a Direct3D 12 device (error 0x887E0003): the game asks for a Direct3D 12 runtime (Agility SDK " +
                    Get(facts, "agility.requested") + ") that doesn't match the one it ships (" + Get(facts, "agility.shipped") + "), and " + Windows(facts) +
                    " is too old to provide its own. ";
                if (state == "too-late")
                    return Because(brief, "Remove tools that load Direct3D 12 early, such as OptiScaler.",
                        text + "The mod corrects the game's request when it starts before anything else loads Direct3D 12, but something loaded it first here. " +
                        "Remove other graphics tools that load early (OptiScaler, or DLLs such as d3d12.dll, dxgi.dll or winmm.dll that another mod put in the game folder) and restart the game.");
                if (state == "corrected")
                    return Because(brief, "Install the latest Windows updates and graphics driver.",
                        text + "The mod corrected the game's request, but Windows still refused. Install the latest Windows updates and graphics driver, then restart the game.");
                return Because(brief, "Install the latest Windows updates.", text + "Install the latest Windows updates, then restart the game.");
            }
            if (string.Equals(error, "0x887A0004", StringComparison.OrdinalIgnoreCase))
                return Because(brief, "Install the latest graphics driver.",
                    "This GPU or its driver doesn't support Direct3D 12 (error 0x887A0004). Install the latest graphics driver.");
            if (string.Equals(error, "0x887A0005", StringComparison.OrdinalIgnoreCase) || string.Equals(error, "0x887A0006", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(error, "0x887A0007", StringComparison.OrdinalIgnoreCase))
                return Because(brief, "Restart the game.",
                    "The graphics driver stopped responding while the Direct3D 12 device was created (error " + error + "). Restart the game; if it happens again, install the latest graphics driver.");
            return Because(brief, "Install the latest graphics driver.",
                "No Direct3D 12 device could be created on this GPU" + (error.Length > 0 ? " (error " + error + ")" : "") +
                ". Install the latest graphics driver; BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log says more.");
        }
        // Why DLSS Frame Generation isn't offered on an NVIDIA GPU; "" when it is, or on another vendor's GPU.
        internal static string DlssMissing(uint vendor)
        {
            if (vendor != 0x10DE) return "";
            var native = Session.Bridge?.Facts() ?? new Dictionary<string, string>();
            string dlssg = Get(native, "dlssg"), streamline = Get(native, "streamline");
            if (dlssg == "supported") return "";
            // Windows' "module not found" (126) and "procedure not found" (127): a DLL Streamline depends on is
            // missing, or lacks a function it needs. The Visual C++ runtime is the usual one, but not the only one.
            if (streamline == "load-failed 126" || streamline == "load-failed 127")
            {
                string runtime = Get(BridgeFacts(), "windows.vcruntime");
                if (runtime == "missing" || runtime.Contains("missing"))
                    return "DLSS Frame Generation isn't offered because NVIDIA Streamline needs the Microsoft Visual C++ runtime, which isn't installed. Install the latest Visual C++ Redistributable (x64), then restart the game.";
                return "DLSS Frame Generation isn't offered because NVIDIA Streamline couldn't load (Windows error " + streamline.Substring(12) +
                    (streamline.EndsWith("126", StringComparison.Ordinal) ? ": a DLL it depends on is missing" : ": a DLL it depends on lacks a function it needs") +
                    "). Installing the latest Visual C++ Redistributable (x64) often fixes it.";
            }
            if (streamline.Length > 0 && streamline != "ready")
                return "DLSS Frame Generation isn't offered because NVIDIA Streamline didn't start (" + streamline + ").";
            if (dlssg.EndsWith("eErrorOSDisabledHWS", StringComparison.Ordinal))
                return "DLSS Frame Generation needs Windows' Hardware-accelerated GPU scheduling, which is off. Turn it on in Windows Settings under System > Display > Graphics, then restart the PC.";
            if (dlssg.EndsWith("eErrorDriverOutOfDate", StringComparison.Ordinal))
                return "DLSS Frame Generation needs a newer NVIDIA driver. Install the latest one.";
            if (dlssg.EndsWith("eErrorOSOutOfDate", StringComparison.Ordinal))
                return "DLSS Frame Generation needs a newer Windows. Install the latest Windows updates.";
            if (dlssg.EndsWith("eErrorAdapterNotSupported", StringComparison.Ordinal) || dlssg.EndsWith("eErrorNoSupportedAdapterFound", StringComparison.Ordinal) ||
                dlssg.EndsWith("eErrorFeatureNotSupported", StringComparison.Ordinal))
                return "DLSS Frame Generation needs a GeForce RTX 40 or 50 series GPU.";
            if (dlssg.Length > 0) return "DLSS Frame Generation isn't offered: NVIDIA's runtime reported " + dlssg.Replace("unsupported ", "") + ".";
            return "";
        }
        // Why XeSS Frame Generation isn't offered when this PC doesn't meet Intel's requirements; "" otherwise.
        internal static string XessMissing()
        {
            string xess = Get(Session.Bridge?.Facts(), "xess");
            if (xess.StartsWith("windows-too-old", StringComparison.Ordinal))
                return "XeSS Frame Generation needs Windows 10 21H1 (build 19043) or newer, and this PC has build " + xess.Substring(16) + ". Install the latest Windows updates.";
            if (xess.StartsWith("vcruntime-", StringComparison.Ordinal))
                return "XeSS Frame Generation needs the Microsoft Visual C++ runtime 14.40 or newer" +
                    (xess.StartsWith("vcruntime-too-old ", StringComparison.Ordinal) ? " (this PC has " + xess.Substring(18) + ")" : "") +
                    ". Install the latest Visual C++ Redistributable (x64).";
            return "";
        }
        // The same in a few words, for the performance overlay's "Waiting:" line.
        internal static string WaitingShort(NativeStatus status)
        {
            switch (status.Reason)
            {
                case Reason.VsyncUnsupported: return "VSync is on";
                case Reason.UnsupportedFormat: return "unsupported output format";
                case Reason.NoDlssInputs: return "no motion vectors yet";
                case Reason.BackendUnavailable:
                case Reason.BackendFailed: return "the technology failed to start";
                case Reason.NativeError:
                case Reason.PresentFailed:
                case Reason.InteropFailure: return "stopped after an error";
                case Reason.ExistingStreamline: return "another Streamline";
                case Reason.UnsupportedColorSpace: return "unsupported HDR mode";
                case Reason.ResolutionTooLow: return "resolution too low";
                default: return status.Reason.ToString();
            }
        }
        // Why frame generation is selected but isn't generating frames right now; "" when it is.
        internal static string Waiting(NativeStatus status)
        {
            switch (status.Reason)
            {
                case Reason.VsyncUnsupported: return "NVIDIA's runtime can't run DLSS Frame Generation with VSync on here. Turn VSync off in the Video settings.";
                case Reason.UnsupportedFormat: return "The game's output format isn't one frame generation supports.";
                case Reason.NoDlssInputs: return "No motion vectors and depth from the game yet.";
                case Reason.BackendUnavailable:
                case Reason.BackendFailed:
                    return "The selected technology failed to start on this GPU" + (status.BackendResult != 0 ? " (error " + status.BackendResult + ")" : "") +
                        " and is off for this session. BepInEx/TGFrameGeneration/Native/TGFG.Native.log says more.";
                case Reason.NativeError:
                case Reason.PresentFailed:
                case Reason.InteropFailure:
                    return "A runtime error stopped frame generation for this session. BepInEx/TGFrameGeneration/Native/TGFG.Native.log says more.";
                case Reason.ExistingStreamline: return "Another mod runs NVIDIA Streamline in the game, so DLSS Frame Generation can't.";
                case Reason.UnsupportedColorSpace: return "The game's color space (HDR mode) isn't one frame generation supports.";
                case Reason.ResolutionTooLow:
                {
                    string minimum = Get(Session.Bridge?.Facts(), "dlssg.minimum");
                    return "The game's resolution is below the minimum DLSS Frame Generation runs at" + (minimum.Length > 0 && minimum != "0" ? " (" + minimum + " pixels on each side)" : "") +
                        ". It resumes when you raise the resolution.";
                }
                default: return "";
            }
        }
    }
}
