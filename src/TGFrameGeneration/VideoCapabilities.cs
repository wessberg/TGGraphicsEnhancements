// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using UnityEngine;
namespace TGFrameGeneration
{
    // What the native side reported, with the rules and labels of the Video-tab rows. The Video tab and
    // the mod-manager entries (Settings) both take their choices from here: only what works on this GPU.
    internal sealed class VideoCapabilities
    {
        internal static readonly FrameGenerationSetting[] Technologies = { FrameGenerationSetting.DLSS, FrameGenerationSetting.FSR, FrameGenerationSetting.XeSS };
        internal readonly bool Known, DlssDynamic, Reflex, XeLL, XeLLStandalone, AntiLag2, InputSource;
        private readonly int[] maxMultiplier = new int[4]; // per FrameGenerationSetting; total multiplier, 0 = unavailable
        internal VideoCapabilities(bool known, NativeStatus status)
        {
            Known = known;
            // No native report: nothing is offered. A technology is only ever offered once this GPU's runtime
            // reported it, so DLSS Frame Generation never shows up on a GPU without it.
            if (!known) return;
            foreach (var backend in Technologies)
                maxMultiplier[(int)backend] = status.Has(backend) ? (int)Math.Min(status.MaxGenerated(backend) + 1, (uint)Settings.MaxMultiplierBound) : 0;
            DlssDynamic = status.DynamicSupported != 0;
            Reflex = status.Supports(LowLatencySetting.Reflex);
            XeLLStandalone = status.Supports(LowLatencySetting.XeLL);
            XeLL = XeLLStandalone || status.Has(FrameGenerationSetting.XeSS); // XeSS Frame Generation brings XeLL on any GPU
            AntiLag2 = status.Supports(LowLatencySetting.AntiLag2);
            // Motion vectors and depth come from DLSS upscaling (NVIDIA only) or, when TGFG's HDRP custom
            // pass is installed, from HDRP itself on any GPU.
            InputSource = HdrpInputs.Available || status.GpuVendor == 0 || status.GpuVendor == 0x10DE;
        }
        internal int MaxMultiplier(FrameGenerationSetting backend) => backend == FrameGenerationSetting.Off ? 0 : maxMultiplier[(int)backend];
        internal int HighestMultiplier => Technologies.Max(b => MaxMultiplier(b));
        // DLSS Fixed/Dynamic choice: shown when the runtime reports multi frame generation and Dynamic.
        internal bool HasMode => MaxMultiplier(FrameGenerationSetting.DLSS) > 2 && DlssDynamic;
        internal FrameGenerationSetting[] Backends =>
            new[] { FrameGenerationSetting.Off }.Concat(Technologies.Where(b => MaxMultiplier(b) >= 2)).ToArray();
        internal LowLatencySetting[] Latencies
        {
            get
            {
                var choices = new List<LowLatencySetting> { LowLatencySetting.Off };
                if (Reflex) { choices.Add(LowLatencySetting.Reflex); choices.Add(LowLatencySetting.ReflexBoost); }
                if (XeLL) choices.Add(LowLatencySetting.XeLL);
                if (AntiLag2) choices.Add(LowLatencySetting.AntiLag2);
                return choices.ToArray();
            }
        }
        // What may run with a frame generation choice. DLSS Frame Generation requires Reflex and XeSS
        // Frame Generation runs with XeLL only, both by their vendor's rule. FSR requires none: AMD pairs
        // it with Anti-Lag 2, which needs an AMD GPU, so on other GPUs Off is a real choice and the only
        // way to run FSR without a latency technology that knows nothing of its generated frames.
        internal LowLatencySetting[] AllowedLatencies(FrameGenerationSetting selected)
        {
            var allowed = new List<LowLatencySetting>();
            if (selected == FrameGenerationSetting.DLSS) allowed.AddRange(new[] { LowLatencySetting.Reflex, LowLatencySetting.ReflexBoost });
            else if (selected == FrameGenerationSetting.XeSS) allowed.Add(LowLatencySetting.XeLL);
            else
            {
                allowed.Add(LowLatencySetting.Off);
                allowed.AddRange(new[] { LowLatencySetting.Reflex, LowLatencySetting.ReflexBoost, LowLatencySetting.AntiLag2 });
                if (XeLLStandalone) allowed.Add(LowLatencySetting.XeLL);
            }
            var available = Latencies;
            var result = allowed.Where(l => available.Contains(l)).ToArray();
            return result.Length > 0 ? result : new[] { LowLatencySetting.Off };
        }
        // The choice made when the current one is ruled out: Reflex, then AMD's and Intel's own, then Off.
        internal LowLatencySetting PreferredLatency(FrameGenerationSetting selected)
        {
            var allowed = AllowedLatencies(selected);
            foreach (var l in new[] { LowLatencySetting.Reflex, LowLatencySetting.XeLL, LowLatencySetting.AntiLag2, LowLatencySetting.Off })
                if (allowed.Contains(l)) return l;
            return allowed[0];
        }
        // Row labels, shared so the Video tab and the mod managers read the same.
        internal static string Label(FrameGenerationSetting backend) => backend.ToString();
        internal static string Label(LowLatencySetting latency)
        {
            switch (latency)
            {
                case LowLatencySetting.Reflex: return "NVIDIA Reflex";
                case LowLatencySetting.ReflexBoost: return "NVIDIA Reflex + Boost";
                case LowLatencySetting.XeLL: return "Intel XeLL";
                case LowLatencySetting.AntiLag2: return "AMD Anti-Lag 2";
                default: return "Off";
            }
        }
        internal static string MultiplierLabel(int multiplier) => multiplier + "x";
        internal static string MaxMultiplierLabel(int multiplier) => "Up to " + multiplier + "x";
        internal static string TargetLabel(float target) => target <= 0 ? "Auto" : target.ToString("0", CultureInfo.InvariantCulture);
        // Dynamic mode targets: Auto (0) plus common refresh rates up to the display's maximum.
        internal static float[] Targets()
        {
            float max = 0;
            try
            {
                foreach (var r in Screen.resolutions) max = Mathf.Max(max, (float)r.refreshRateRatio.value);
                if (max <= 0) max = (float)Screen.currentResolution.refreshRateRatio.value;
            }
            catch { }
            if (max <= 0 || float.IsNaN(max)) max = 240;
            var list = new List<float> { 0 };
            foreach (var c in new[] { 60f, 72f, 90f, 100f, 120f, 144f, 165f, 175f, 180f, 200f, 240f, 280f, 300f, 360f, 480f, 500f })
                if (c <= max + 0.5f) list.Add(c);
            float rounded = Mathf.Round(max);
            if (!list.Any(v => Mathf.Abs(v - rounded) < 0.6f)) list.Add(rounded);
            list.Sort();
            return list.ToArray();
        }
        internal static float NearestTarget(float[] targets, float value)
        {
            if (value <= 0) return 0;
            float best = 0, distance = float.MaxValue;
            foreach (var t in targets)
            {
                if (t <= 0) continue;
                float d = Mathf.Abs(t - value);
                if (d < distance) { distance = d; best = t; }
            }
            return best;
        }
    }
}
