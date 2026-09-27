// SPDX-License-Identifier: MIT
// Whether turning frame generation on raised the framerate on screen. While the player plays without
// it, the rendered frame rate (which is then the rate on screen) is sampled once a second. When frame
// generation starts generating, it is given a few seconds to settle and then measured: frames on
// screen against the rate before. Less than 15% more is no gain, and the player is told so in a short
// message during play and in full at the top of the Video settings, with what this session shows that
// explains it: the game's Framerate Limit or VSync, a limit outside the game, video memory running out,
// rendering that fell much further than frame generation costs. Checked once each time frame
// generation starts or its settings change; the troubleshooting test run does its own measuring.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using TGGraphicsBridge;
using UnityEngine;
namespace TGFrameGeneration
{
    internal static class FrameRateCheck
    {
        private const string NoticeId = "fg.nogain";
        private enum Phase { Idle, Settling, Measuring, Done }
        private static Phase phase = Phase.Idle;
        private static readonly List<float> before = new List<float>(); // rendered fps, one per second without frame generation
        private static readonly List<float> rendered = new List<float>(), shown = new List<float>();
        private static float lastBefore = -1000f, lastSample, phaseStart;
        private static Selection checkedSelection;
        internal static string Last { get; private set; } = "Not checked yet.";
        // After every status read (ten a second), from Session.Tick.
        internal static void Tick(float now)
        {
            var selection = Session.Selected;
            CheckHybrid(now, selection);
            CheckMemory(now, selection);
            if (Troubleshooting.Busy) { Reset(); return; }
            bool playing = Session.Suspension == Suspend.None && Session.HaveStatus;
            if (!selection.Generating || !playing || !FrameRates.Generating)
            {
                // Frame generation off in play: the rate to compare with later.
                if (!selection.Generating && playing && FrameRates.Rendered > 1f && now - lastSample >= 1f)
                {
                    lastSample = now;
                    if (now - lastBefore > 30f) before.Clear(); // an old rate says nothing about this scene
                    before.Add(FrameRates.Rendered);
                    if (before.Count > 15) before.RemoveAt(0);
                    lastBefore = now;
                }
                if (!selection.Generating) { Notices.Clear(NoticeId); phase = Phase.Idle; }
                else if (phase == Phase.Settling || phase == Phase.Measuring) phase = Phase.Idle; // paused: measured again once it runs
                return;
            }
            if (phase == Phase.Done && !selection.SameAs(checkedSelection)) { phase = Phase.Idle; Notices.Clear(NoticeId); }
            switch (phase)
            {
                case Phase.Idle:
                    phase = Phase.Settling; phaseStart = now; checkedSelection = selection;
                    rendered.Clear(); shown.Clear();
                    return;
                case Phase.Settling:
                    if (now - phaseStart < 3f) return;
                    phase = Phase.Measuring; phaseStart = now; lastSample = now;
                    return;
                case Phase.Measuring:
                    if (now - lastSample >= 1f) { lastSample = now; rendered.Add(FrameRates.Rendered); shown.Add(FrameRates.Output); }
                    if (now - phaseStart < 5f) return;
                    phase = Phase.Done;
                    Evaluate(now, selection);
                    return;
            }
        }
        private static void Reset()
        {
            if (phase == Phase.Settling || phase == Phase.Measuring) phase = Phase.Idle;
        }
        private static float Median(List<float> values)
        {
            if (values.Count == 0) return 0f;
            var sorted = values.OrderBy(v => v).ToList();
            return sorted[sorted.Count / 2];
        }
        private static string Fps(float value) => value.ToString("0", CultureInfo.InvariantCulture);
        private static void Evaluate(float now, Selection selection)
        {
            float fgRendered = Median(rendered), fgShown = Median(shown);
            if (before.Count < 3 || now - lastBefore > 180f)
            {
                Last = "Frame generation shows " + Fps(fgShown) + " fps from " + Fps(fgRendered) + " rendered; no recent rate without it to compare with.";
                Session.Log.LogInfo(Last);
                return;
            }
            float baseline = Median(before);
            string technology = VideoCapabilities.Label(selection.Backend) + " Frame Generation";
            // Dynamic mode generates only as many frames as its target needs: reaching it is the gain.
            float refresh = 0f;
            try { refresh = (float)Screen.currentResolution.refreshRateRatio.value; } catch { }
            float target = selection.Mode == GenerationMode.Dynamic ? (selection.TargetFps > 0f ? selection.TargetFps : refresh) : 0f;
            if (fgShown >= baseline * 1.15f || (target > 0f && fgShown >= target * 0.9f))
            {
                Last = technology + " raised the framerate from " + Fps(baseline) + " to " + Fps(fgShown) + " fps on screen (" + Fps(fgRendered) + " rendered).";
                Session.Log.LogInfo(Last);
                Notices.Clear(NoticeId);
                return;
            }
            var causes = Causes(baseline, fgRendered, fgShown, selection);
            string numbers = "Before: " + Fps(baseline) + " fps. With " + technology + ": " + Fps(fgShown) + " fps on screen from " + Fps(fgRendered) + " rendered" +
                (selection.Backend == FrameGenerationSetting.FSR ? " (FSR doesn't report the frames it shows: on screen is the rendered rate times two)." : ".");
            string closing = causes.Count > 0 ? "" : " Nothing in this session explains it. Please create a troubleshooting report (the button under Performance Overlay in the Video settings) " +
                "and post it in a bug report on the mod's Nexus page.";
            string detail = "Frame generation isn't raising your framerate. " + numbers + " " + string.Join(" ", causes.Select(c => c.Value).ToArray()) + closing;
            // The row's tooltip and the message in play: the numbers and the likeliest cause, in a few words.
            string shortNumbers = Fps(baseline) + " fps before, " + Fps(fgShown) + " with " + VideoCapabilities.Label(selection.Backend) + " FG (" + Fps(fgRendered) + " rendered).";
            string summary = shortNumbers + " " + (causes.Count > 0 ? causes[0].Key : "Please create a troubleshooting report.");
            Last = detail;
            Session.Log.LogWarning(detail);
            Notices.Set(NoticeId, NoticeLevel.Warning, Plugin.Name, "No FPS gain", summary, detail);
            Notices.Show("fg.nogain", "Frame generation isn't raising your framerate", summary, 15f);
        }
        // DLSS Frame Generation with VSync on a display that another GPU drives (a laptop's panel on its
        // integrated GPU): NVIDIA's DLSS-G guide, on hybrid systems: "If VSync is enabled with Frame Generation
        // on a system that does not support IFLIP, high latency is expected." The full text, or "".
        private static string HybridVsyncDlss(Selection selection)
        {
            if (selection.Backend != FrameGenerationSetting.DLSS || !selection.Generating) return "";
            string display;
            try { display = GraphicsBridge.Fact("display.crossAdapter") == "1" ? GraphicsBridge.Fact("display.adapter") : ""; } catch { return ""; }
            if (display.Length == 0) return "";
            bool forced = DriverForcesVsync();
            if (QualitySettings.vSyncCount == 0 && !forced) return "";
            // The game's own VSync is turned off here (HybridVsync); what is left on is the driver's, which overrides it.
            string fix = forced
                ? "The mod turns the game's own VSync off here, but the NVIDIA driver forces it on for this game: set Vertical sync to \"Use the 3D application setting\" for Fall of Avalon in the NVIDIA App"
                : "Turn VSync off";
            return "Your display is connected to " + display + ", not to the GPU the game renders on, and VSync is on" + (forced ? " (forced by the NVIDIA driver)" : "") +
                ". NVIDIA's DLSS Frame Generation guide says that with VSync on such a system, high latency is expected. " + fix + ", or use the laptop's " +
                "setting that makes the dedicated GPU drive the display (often called MUX, dGPU or Ultimate mode).";
        }
        // The row's tooltip: the fix that applies.
        private static string HybridSummary() => DriverForcesVsync()
            ? "Your display is on another GPU and the NVIDIA driver forces VSync on: DLSS FG then has high latency. Set VSync to the 3D app setting in the NVIDIA App."
            : "Your display is on another GPU and VSync is on: NVIDIA says DLSS FG then has high latency. Turn VSync off.";
        // The NVIDIA driver forces VSync on for this game (its profile, else the global one), at any interval.
        private static bool DriverForcesVsync()
        {
            uint vsync = DriverSetting("nvidia.vsync");
            return vsync == 0x47814940 || vsync == 0x32610244 || vsync == 0x71271021 || vsync == 0x13245256;
        }
        // An NVIDIA driver setting as the bridge reports it ("0x3C@game"); 0 when unset or unread.
        private static uint DriverSetting(string key)
        {
            string text;
            try { text = GraphicsBridge.Fact(key); } catch { return 0; }
            int at = text.IndexOf('@');
            string value = at > 0 ? text.Substring(0, at) : text;
            if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) value = value.Substring(2);
            return uint.TryParse(value, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var number) ? number : 0;
        }
        // Video memory over Windows' budget, or system memory nearly full, while frame generation runs:
        // both make the game stutter as memory is moved out and back, whatever the framerate does. Held for
        // three checks two seconds apart, so a loading spike isn't reported.
        private static float nextMemoryCheck;
        private static int vramOver, ramLow;
        private static ulong vramWithoutFg;
        private static void CheckMemory(float now, Selection selection)
        {
            if (now < nextMemoryCheck) return;
            nextMemoryCheck = now + 2f;
            bool generating = selection.Generating && Session.Suspension == Suspend.None;
            BridgePerformance p;
            bool vram = GraphicsBridge.TryReadPerformance(out p) && (p.Valid & 4u) != 0 && p.VramBudget > 0;
            if (vram && !selection.Generating && Session.Suspension == Suspend.None) vramWithoutFg = p.VramProcess;
            vramOver = generating && vram && p.VramProcess > p.VramBudget / 100 * 98 ? vramOver + 1 : 0;
            if (vramOver >= 3)
                Notices.Set("fg.vram", NoticeLevel.Warning, Plugin.Name, "VRAM full",
                    "The game needs more video memory than Windows gives it, which causes stutter. Lower Texture Quality or the resolution.",
                    "The game uses " + Gb(p.VramProcess) + " GB of the " + Gb(p.VramBudget) + " GB of video memory Windows gives it" +
                    (vramWithoutFg > 0 && p.VramProcess > vramWithoutFg ? "; frame generation added about " + Gb(p.VramProcess - vramWithoutFg) + " GB" : "") +
                    ". Past the budget, Windows moves memory to system RAM and back, and the game stutters. Lower Texture Quality or the resolution, " +
                    "or close other programs that use the GPU.");
            else if (vramOver == 0) Notices.Clear("fg.vram");
            var memory = new MemoryStatus { Length = (uint)Marshal.SizeOf(typeof(MemoryStatus)) };
            bool ram = GlobalMemoryStatusEx(ref memory) && memory.TotalPhysical > 0;
            ramLow = generating && ram && (memory.AvailablePhysical < (1UL << 30) || memory.Load >= 95) ? ramLow + 1 : 0;
            if (ramLow >= 3)
                Notices.Set("fg.ram", NoticeLevel.Warning, Plugin.Name, "RAM full",
                    "Windows is almost out of memory, which causes stutter. Close other programs.",
                    "Only " + Gb(memory.AvailablePhysical) + " GB of " + Gb(memory.TotalPhysical) + " GB system memory is free (" + memory.Load +
                    "% in use). Windows then moves memory to disk and back, and the game stutters. Close other programs; " +
                    "lowering Texture Quality also helps, since video memory spills into system memory.");
            else if (ramLow == 0) Notices.Clear("fg.ram");
        }
        private static string Gb(ulong bytes) => (bytes / 1073741824.0).ToString("0.0", CultureInfo.InvariantCulture);
        // System memory as Windows reports it.
        internal static bool TryReadMemory(out ulong available, out ulong total, out uint load)
        {
            var memory = new MemoryStatus { Length = (uint)Marshal.SizeOf(typeof(MemoryStatus)) };
            bool ok = GlobalMemoryStatusEx(ref memory);
            available = memory.AvailablePhysical; total = memory.TotalPhysical; load = memory.Load;
            return ok && total > 0;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct MemoryStatus
        {
            public uint Length, Load;
            public ulong TotalPhysical, AvailablePhysical, TotalPageFile, AvailablePageFile, TotalVirtual, AvailableVirtual, AvailableExtendedVirtual;
        }
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GlobalMemoryStatusEx(ref MemoryStatus buffer);
        private static float nextHybridCheck;
        // Once a second: the hybrid-display warning on the Frame Generation row, whatever the framerate does.
        private static void CheckHybrid(float now, Selection selection)
        {
            if (now < nextHybridCheck) return;
            nextHybridCheck = now + 1f;
            string text = HybridVsyncDlss(selection);
            if (text.Length > 0)
                Notices.Set("fg.hybridvsync", NoticeLevel.Warning, Plugin.Name, "High latency", HybridSummary(), text);
            else Notices.Clear("fg.hybridvsync");
        }
        // What this session shows that keeps frame generation from raising the rate on screen, most likely
        // first: a short sentence with the fix (Key) and the full explanation (Value).
        private static List<KeyValuePair<string, string>> Causes(float baseline, float fgRendered, float fgShown, Selection selection)
        {
            var causes = new List<KeyValuePair<string, string>>();
            void Add(string brief, string full) => causes.Add(new KeyValuePair<string, string>(brief, full));
            int vsync = QualitySettings.vSyncCount, cap = Application.targetFrameRate;
            float refresh = 0f;
            try { refresh = (float)Screen.currentResolution.refreshRateRatio.value; } catch { }
            // A laptop's display on the integrated GPU: DLSS-G with VSync has high latency there by NVIDIA's own guide.
            string hybrid = HybridVsyncDlss(selection);
            if (hybrid.Length > 0) Add("Display on another GPU with VSync on: try VSync off.", hybrid);
            if (vsync >= 2 && refresh > 0f)
                // The technologies don't all follow a longer interval (DLSS-G uses every refresh), so no cap is claimed.
                Add("VSync every " + vsync + " refreshes may cause it. Try every refresh or off.",
                    "VSync is set to every " + vsync + " refreshes. Frame generation's swapchains don't all present that way (DLSS Frame Generation uses every refresh), " +
                    "which can cap or unsettle the frames on screen. Choose a Framerate Limit at your display's full " + Fps(refresh) + " Hz, or turn VSync off.");
            else if (vsync == 1 && refresh > 0f)
            {
                if (baseline >= refresh * 0.9f)
                    Add("You were already at your " + Fps(refresh) + " Hz refresh rate with VSync on.",
                        "You were already at your display's refresh rate (" + Fps(refresh) + " Hz) with VSync on, so there's no room for more frames on screen. Frame generation still helps where the framerate drops below it.");
                else
                    Add("VSync may hold it back. Try VSync off.",
                        "VSync is on, so the frames on screen can't pass your display's " + Fps(refresh) + " Hz, and at " + Math.Max(2, selection.Multiplier) +
                        "x rendering can't pass " + Fps(refresh / Math.Max(2, selection.Multiplier)) + " fps. Try VSync off in the Video settings; a G-SYNC or FreeSync " +
                        "display avoids tearing while the framerate is inside its range.");
            }
            if (vsync == 0 && cap > 0)
                Add("The game's Framerate Limit (" + cap + " fps) may cause it. Try Unlimited.",
                    "The game's Framerate Limit is " + cap + " fps. Set it to Unlimited, or to the rendered framerate you want: frame generation then shows " +
                    Math.Max(2, selection.Multiplier) + " times that.");
            // The driver's VSync and Max Frame Rate only when the frames on screen are at them: both count
            // generated frames, and below them they do nothing to frame generation.
            if (vsync == 0 && DriverSetting("nvidia.vsync") == 0x47814940 && refresh > 0f && fgShown >= refresh * 0.95f)
                Add("The NVIDIA driver's VSync holds it at your " + Fps(refresh) + " Hz.",
                    "The NVIDIA driver forces VSync on for this game, so the frames on screen can't pass your display's " + Fps(refresh) + " Hz, and they are there " +
                    "already. Frame generation still helps where the framerate drops below it.");
            uint driverCap = DriverSetting("nvidia.frameRateLimit");
            if (driverCap > 0 && fgShown >= driverCap * 0.95f)
                Add("NVIDIA Max Frame Rate (" + driverCap + " fps) may cause it.",
                    "The NVIDIA driver limits this game to " + driverCap + " fps (Max Frame Rate), generated frames included, and the frames on screen are at that " +
                    "limit. Raise it or set it to Off in the NVIDIA App for frame generation to add more; a limit a few fps under your display's refresh rate is fine.");
            foreach (var n in Notices.Current(NoticeLevel.Warning))
                if (n.Id == "compat.rtss" || n.Id == "compat.nvidia.vsync" || n.Id == "compat.nvidia.smoothmotion" || n.Id == "compat.lossless" || n.Id == "compat.optiscaler")
                    Add(n.Source + " (" + n.Label + ") may cause it.", n.Source + ": " + n.Label + ". " + n.Detail);
            if (GraphicsBridge.TryReadPerformance(out var p) && (p.Valid & 4u) != 0 && p.VramBudget > 0 && p.VramProcess > p.VramBudget / 100 * 92)
                Add("Video memory is nearly full. Lower Texture Quality.",
                    "The game uses " + (p.VramProcess / 1073741824.0).ToString("0.0", CultureInfo.InvariantCulture) + " GB of the " +
                    (p.VramBudget / 1073741824.0).ToString("0.0", CultureInfo.InvariantCulture) + " GB of video memory Windows gives it. Frame generation needs more; " +
                    "once it runs out, the GPU moves memory back and forth and slows down. Lower Texture Quality or the resolution.");
            if (TryReadMemory(out ulong free, out ulong total, out uint load) && (free < (1UL << 30) || load >= 95))
                Add("System memory is nearly full. Close other programs.",
                    "Only " + Gb(free) + " GB of " + Gb(total) + " GB system memory is free (" + load + "% in use). Windows moves memory to disk and back, " +
                    "which slows the game down. Close other programs.");
            // Frame generation costs a few milliseconds; rendering at 60% or less of the rate before (an exact
            // halving included) is something else.
            if (fgRendered < baseline * 0.6f)
            {
                string brief = "Rendering fell from " + Fps(baseline) + " to " + Fps(fgRendered) + " fps.";
                string why = "Rendering fell from " + Fps(baseline) + " to " + Fps(fgRendered) + " fps when frame generation turned on, much more than frame generation itself costs.";
                if (selection.Latency == LowLatencySetting.AntiLag2)
                {
                    brief += " Try Low Latency Off.";
                    why += " AMD Anti-Lag 2 is on with it; set Low Latency to Off to see whether that is the cause.";
                }
                string cross = "";
                try { cross = GraphicsBridge.Fact("display.crossAdapter") == "1" ? GraphicsBridge.Fact("display.adapter") : ""; } catch { }
                if (cross.Length > 0)
                    why += " Your display is connected to " + cross + ", not to the GPU the game renders on, so every frame is copied between the two GPUs; " +
                        "on a laptop, a setting that makes the dedicated GPU drive the display (often called MUX, dGPU or Ultimate mode) avoids that.";
                if (Session.HaveStatus && Session.Status.GpuVendor == 0x1002)
                    why += " If AMD Fluid Motion Frames is on in AMD Software for this game, turn it off.";
                Add(brief, why);
            }
            return causes;
        }
    }
}
