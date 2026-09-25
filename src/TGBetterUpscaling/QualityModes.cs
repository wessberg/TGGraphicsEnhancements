// SPDX-License-Identifier: MIT
// Each upscaler's own quality modes, in place of the game's render-scale slider. A mode is what its
// vendor tunes: NVIDIA's driver picks the DLSS model for each mode, and every vendor recommends its own
// render resolution per mode, so a mode's name means about the same cost and image for DLSS, FSR and
// XeSS alike. Only the modes a runtime reports are offered, at the ratio it reports. Free of Unity and
// game types, so tests/QualityModes.Tests.csproj runs it on its own.
using System.Collections.Generic;
using System.Linq;
namespace TGBetterUpscaling
{
    internal sealed class QualityMode
    {
        internal readonly int Id;       // DLSS: NGX's perf quality value; FSR: FfxApiUpscaleQualityMode; XeSS: xess_quality_settings_t
        internal readonly string Name;  // the vendor's own name, also the stored value
        internal readonly float Scale;  // render scale per axis, in percent
        internal QualityMode(int id, string name, float scale) { Id = id; Name = name; Scale = scale; }
        public override string ToString() => Name + " (" + Scale.ToString("0.#") + "%)";
    }
    internal static class QualityModes
    {
        // NVSDK_NGX_PerfQuality_Value, which Unity's DLSSQuality mirrors; DLAA is NGX's 5, which Unity's
        // enum leaves out. Highest render scale first.
        internal const int DlssPerformance = 0, DlssBalanced = 1, DlssQuality = 2, DlssUltraPerformance = 3, DlssDlaa = 5;
        internal static readonly int[] DlssIds = { DlssDlaa, DlssQuality, DlssBalanced, DlssPerformance, DlssUltraPerformance };
        internal const string DefaultName = "Quality";
        internal static string DlssName(int id)
        {
            switch (id)
            {
                case DlssDlaa: return "DLAA";
                case DlssQuality: return "Quality";
                case DlssBalanced: return "Balanced";
                case DlssPerformance: return "Performance";
                case DlssUltraPerformance: return "Ultra Performance";
                default: return "#" + id;
            }
        }
        // FfxApiUpscaleQualityMode
        internal static string FsrName(int id)
        {
            switch (id)
            {
                case 0: return "Native AA";
                case 1: return "Quality";
                case 2: return "Balanced";
                case 3: return "Performance";
                case 4: return "Ultra Performance";
                default: return "#" + id;
            }
        }
        // xess_quality_settings_t
        internal static string XessName(int id)
        {
            switch (id)
            {
                case 106: return "Native Anti-Aliasing";
                case 105: return "Ultra Quality Plus";
                case 104: return "Ultra Quality";
                case 103: return "Quality";
                case 102: return "Balanced";
                case 101: return "Performance";
                case 100: return "Ultra Performance";
                default: return "#" + id;
            }
        }
        // Every name a runtime of this upscaler can report, so a stored choice survives until the runtime
        // has reported what it offers in this session.
        internal static readonly string[] DlssNames = DlssIds.Select(DlssName).ToArray();
        internal static readonly string[] FsrNames = Enumerable.Range(0, 5).Select(FsrName).ToArray();
        internal static readonly string[] XessNames = Enumerable.Range(0, 7).Select(i => XessName(106 - i)).ToArray();
        // Highest render scale first, one mode per name, only scales an upscaler can render at.
        internal static QualityMode[] Sorted(IEnumerable<QualityMode> modes) =>
            (modes ?? Enumerable.Empty<QualityMode>()).Where(m => m != null && m.Scale > 0f && m.Scale <= 100.5f)
                .GroupBy(m => m.Name).Select(g => g.First()).OrderByDescending(m => m.Scale).ToArray();
        // The mode a player starts on, from the game's render scale: the highest-quality mode that renders
        // no more pixels than that, so moving from the slider to modes never costs frames. Half a point of
        // tolerance absorbs the game's own values (its 2.99 divisor, its presets at 50.1%, 58.9% and
        // 66.8%); a 100% mode needs 99.5% or more. Below every mode, the lowest.
        internal static QualityMode Default(IList<QualityMode> modes, float percent)
        {
            if (modes == null || modes.Count == 0) return null;
            foreach (var mode in modes.OrderByDescending(m => m.Scale))
                if (mode.Scale <= percent + 0.5f) return mode;
            return modes.OrderBy(m => m.Scale).First();
        }
        internal static QualityMode Find(IList<QualityMode> modes, string name) => modes?.FirstOrDefault(m => m.Name == name);
        // What Restore Defaults and an unknown stored name fall back to: each vendor's "Quality".
        internal static QualityMode Fallback(IList<QualityMode> modes) => Find(modes, DefaultName) ?? Default(modes, 66.7f);
        // The render scale the game is given for a mode: a tenth of a point above the vendor's size, so
        // that HDRP's rounding never lands a pixel below the smallest input a mode accepts.
        internal static float RenderScale(QualityMode mode) => mode.Scale >= 99.95f ? 100f : System.Math.Min(100f, mode.Scale + 0.1f);
    }
}
