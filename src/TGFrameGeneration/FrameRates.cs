// SPDX-License-Identifier: MIT
// The frame rate the game renders at and the one on screen, over the last second, from every status
// read (ten a second). Rendered frames are the game's presents; frames on screen come from the
// technology's own count: DLSS-G and XeSS-FG report the frames they present, and AMD's FSR API
// generates exactly one frame per rendered frame (the maximum its runtime reports), so FSR's output is
// the rendered rate times that. The rates are timed by when each read happened, not by when a reader
// asks: a reader's window holds a varying number of reads.
using System.Collections.Generic;
namespace TGFrameGeneration
{
    internal static class FrameRates
    {
        private struct Sample { public float Time; public ulong Source, Output; }
        private static readonly List<Sample> window = new List<Sample>();
        internal static float Rendered { get; private set; }
        internal static float Output { get; private set; }
        // Frame generation presented generated frames over the window (accepted and not paused).
        internal static bool Generating { get; private set; }
        internal static void Update(NativeStatus status, float at, FrameGenerationSetting backend, bool suspended)
        {
            if (window.Count > 0)
            {
                var newest = window[window.Count - 1];
                if (at == newest.Time) return;
                if (at < newest.Time || status.SourcePresents < newest.Source || status.OutputFramesReported < newest.Output) window.Clear();
            }
            window.Add(new Sample { Time = at, Source = status.SourcePresents, Output = status.OutputFramesReported });
            while (window.Count > 2 && at - window[1].Time >= 1f) window.RemoveAt(0);
            var first = window[0];
            float dt = at - first.Time;
            if (dt <= 0f) return;
            Rendered = (status.SourcePresents - first.Source) / dt;
            Generating = (GenerationMode)status.LastAcceptedMode != GenerationMode.Off && !suspended && backend != FrameGenerationSetting.Off;
            if (!Generating) Output = Rendered;
            else if (backend == FrameGenerationSetting.FSR) Output = Rendered * (status.FsrMaxGenerated + 1);
            else Output = (status.OutputFramesReported - first.Output) / dt;
        }
    }
}
