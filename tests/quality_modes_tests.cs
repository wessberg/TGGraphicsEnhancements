// SPDX-License-Identifier: MIT
// Which quality mode a render scale starts on (the first start, and the game's graphics presets):
// the highest-quality mode at or below that scale, per upscaler, with the modes the runtimes report.
using System;
using System.Collections.Generic;
using System.Linq;
using TGBetterUpscaling;

internal static class Program
{
    private static int failures;
    private static void Expect(bool condition, string what)
    {
        if (!condition) { Console.WriteLine("FAIL: " + what); failures++; }
    }
    private static QualityMode[] Modes(Func<int, string> name, params (int id, float ratio)[] reported) =>
        QualityModes.Sorted(reported.Select(r => new QualityMode(r.id, name(r.id), 100f / r.ratio)));
    private static int Main()
    {
        // As the runtimes report them on an RTX 5080 (FSR 3.1.5, XeSS 2.0.2); DLSS as NGX sizes it at 4K.
        var dlss = Modes(QualityModes.DlssName, (5, 1f), (2, 1.5f), (1, 3840f / 2227f), (0, 2f), (3, 3f));
        var fsr = Modes(QualityModes.FsrName, (0, 1f), (1, 1.5f), (2, 1.7f), (3, 2f), (4, 3f));
        var xess = Modes(QualityModes.XessName, (106, 1f), (105, 1.3f), (104, 1.5f), (103, 1.7f), (102, 2f), (101, 2.3f), (100, 3f));
        Expect(string.Join(",", fsr.Select(m => m.Name)) == "Native AA,Quality,Balanced,Performance,Ultra Performance", "highest render scale first");
        // The game's slider: 100 / (2.99 - 1.99 s). Its lowest position, 40.3% (the tester's), its presets and 100%.
        var cases = new (float percent, string dlss, string fsr, string xess)[]
        {
            (33.44f, "Ultra Performance", "Ultra Performance", "Ultra Performance"),
            (40.3f, "Ultra Performance", "Ultra Performance", "Ultra Performance"),
            (45f, "Ultra Performance", "Ultra Performance", "Performance"),
            (50.1f, "Performance", "Performance", "Balanced"),   // the game's Medium preset
            (58.9f, "Balanced", "Balanced", "Quality"),          // High
            (66.8f, "Quality", "Quality", "Ultra Quality"),      // Ultra
            (83.4f, "Quality", "Quality", "Ultra Quality Plus"),
            (99.4f, "Quality", "Quality", "Ultra Quality Plus"),
            (100f, "DLAA", "Native AA", "Native Anti-Aliasing"),
        };
        foreach (var c in cases)
        {
            Expect(QualityModes.Default(dlss, c.percent).Name == c.dlss, "DLSS at " + c.percent + "%: " + QualityModes.Default(dlss, c.percent).Name);
            Expect(QualityModes.Default(fsr, c.percent).Name == c.fsr, "FSR at " + c.percent + "%: " + QualityModes.Default(fsr, c.percent).Name);
            Expect(QualityModes.Default(xess, c.percent).Name == c.xess, "XeSS at " + c.percent + "%: " + QualityModes.Default(xess, c.percent).Name);
        }
        // Rounding down never renders more pixels than before: the start mode's scale is at most the slider's.
        for (float p = 33.4f; p <= 100f; p += 0.1f)
            foreach (var modes in new[] { dlss, fsr, xess })
                Expect(QualityModes.Default(modes, p).Scale <= p + 0.5f, "no start mode above the slider's scale at " + p);
        Expect(QualityModes.Default(fsr, 20f).Name == "Ultra Performance", "below every mode: the lowest");
        Expect(QualityModes.Default(new QualityMode[0], 50f) == null, "no modes, no start mode");
        // Without DLAA from NGX, 100% starts on Quality.
        var noDlaa = dlss.Where(m => m.Id != QualityModes.DlssDlaa).ToArray();
        Expect(QualityModes.Default(noDlaa, 100f).Name == "Quality", "no DLAA: Quality at 100%");
        Expect(QualityModes.Fallback(xess).Name == "Quality" && QualityModes.Fallback(dlss).Name == "Quality", "Restore Defaults picks each vendor's Quality");
        Expect(QualityModes.Find(fsr, "Balanced").Id == 2 && QualityModes.Find(fsr, "Ultra Quality") == null, "modes by the vendor's name");
        Expect(QualityModes.RenderScale(fsr[3]) > 50f && QualityModes.RenderScale(fsr[3]) < 50.2f && QualityModes.RenderScale(fsr[0]) == 100f,
            "a tenth of a point above the vendor's size, never above 100%");
        Expect(QualityModes.Sorted(new[] { new QualityMode(0, "A", 150f), new QualityMode(1, "B", 50f), null }).Length == 1, "impossible scales dropped");
        if (failures > 0) { Console.WriteLine(failures + " quality mode checks failed"); return 1; }
        Console.WriteLine("Quality mode checks passed: the round-down start mode per upscaler, the game's presets, DLAA/Native AA only at 100%.");
        return 0;
    }
}
