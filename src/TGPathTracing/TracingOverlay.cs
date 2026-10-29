// SPDX-License-Identifier: MIT
// TG Path Tracing's section of the performance overlay (TG Graphics Bridge's PerformanceOverlay).
using System.Collections.Generic;
namespace TGPathTracing
{
    internal static class TracingOverlay
    {
        internal static IEnumerable<string> Lines()
        {
            var lines = new List<string>();
            var settings = Session.Settings;
            if (settings == null) return lines;
            if (settings.CurrentMode == TracingMode.Off) { lines.Add("Off (" + settings.ModeKey.Value + " turns it on)"); return lines; }
            if (!Session.HaveStatus) { lines.Add(settings.Mode.Value + ": waiting for the native side"); return lines; }
            var s = Session.Status;
            if (s.Supported == 0) { lines.Add("Unavailable: " + Session.Tracer.UnsupportedReason()); return lines; }
            if (HdrpPasses.Failed) { lines.Add("Off after an error: " + HdrpPasses.Detail); return lines; }
            if (s.Starting != 0) { lines.Add(settings.Mode.Value + ": compiling shaders (" + Session.Diagnostics.PassesDone + " of " + Session.Diagnostics.PassesTotal + ")"); return lines; }
            lines.Add(settings.Mode.Value + (settings.CurrentView != DebugView.None ? " · view " + settings.View.Value : "") + " · " + s.RenderWidth + "x" + s.RenderHeight +
                " · GPU " + s.GpuMs.ToString("0.0") + " ms");
            lines.Add(s.InstancesTraced + " objects, " + (s.Triangles / 1000) + "k triangles, " + SceneLights.Count + " lights" + (SceneLights.Sun.Valid ? " + sun" : "") +
                (Arrival.Running ? " · " + Arrival.NoticeText : "")); // PT-98: everything still to come, not the native queue alone
            return lines;
        }
    }
}
