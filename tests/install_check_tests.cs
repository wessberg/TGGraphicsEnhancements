// SPDX-License-Identifier: MIT
// Where the mods' files end up when an archive is unpacked or deployed the wrong way, and what the
// install check tells the player, on game folders built for each case in a temporary folder: the
// correct layout, the archive unpacked into BepInEx, into BepInEx/plugins (a mod manager's plugin
// type), into a folder of its own, its BepInEx contents straight into the game folder, a missing
// preloader, the other game branch's build, TG Frame Generation 1.0 left behind, and nothing installed.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using BepInEx;

namespace TGGraphicsBridge
{
    // Stand-ins for the notices, which draw with Unity.
    public enum NoticeLevel { Info = 0, Warning = 1, Problem = 2 }
    public static class Notices
    {
        public static readonly Dictionary<string, (NoticeLevel Level, string Source, string Label, string Detail, string Summary)> Set_ = new Dictionary<string, (NoticeLevel, string, string, string, string)>();
        public static void Set(string id, NoticeLevel level, string source, string label, string detail) => Set_[id] = (level, source, label, detail, detail);
        public static void Set(string id, NoticeLevel level, string source, string label, string summary, string detail) => Set_[id] = (level, source, label, detail, summary);
        public static void Clear(string id) => Set_.Remove(id);
    }
}

internal static class Program
{
    private static int failures;
    private static void Expect(bool condition, string what)
    {
        if (!condition) { Console.WriteLine("FAIL: " + what); failures++; }
    }
    private static string root;
    private static readonly List<(TGGraphicsBridge.NoticeLevel Level, string Source, string Label, string Detail, string Summary)> everyNotice =
        new List<(TGGraphicsBridge.NoticeLevel, string, string, string, string)>();
    private static void Touch(string relative, string content = "")
    {
        string path = Path.Combine(root, relative.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path));
        File.WriteAllText(path, content);
    }
    // The archive's files, under prefix (the folder its BepInEx folder went into).
    private static void Archive(string prefix, bool il2cpp = false, bool preloader = true)
    {
        string p = prefix.Length > 0 ? prefix + "/" : "";
        if (preloader) Touch(p + "BepInEx/patchers/TGFrameGeneration.Autoload.dll", il2cpp ? "MZ ... BepInEx.Preloader.Core ..." : "MZ ... Mono.Cecil ...");
        Touch(p + "BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll", il2cpp ? "MZ ... Il2CppInterop.Runtime ..." : "MZ ... mscorlib ...");
        Touch(p + "BepInEx/plugins/TGFrameGeneration/TGGraphicsBridge.dll");
        Touch(p + "BepInEx/TGFrameGeneration/graphics-bridge-extension.json",
            "{ \"name\": \"TG Frame Generation\", \"native\": \"Native/TGFG.Native.dll\", \"bridge\": \"Native/TGGraphicsBridge.Native.dll\", \"bridgeVersion\": \"1.2.0\" }");
        Touch(p + "BepInEx/TGFrameGeneration/Native/TGFG.Native.dll");
        Touch(p + "BepInEx/TGFrameGeneration/Native/TGGraphicsBridge.Native.dll");
    }
    private static void Game(string name)
    {
        root = Path.Combine(Path.GetTempPath(), "tg-install-check-" + name + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
        Directory.CreateDirectory(root);
        Touch("Fall of Avalon.exe");
        foreach (var folder in new[] { "BepInEx/core", "BepInEx/plugins", "BepInEx/patchers", "BepInEx/config", "Fall of Avalon_Data/Managed" })
            Directory.CreateDirectory(Path.Combine(root, folder));
        typeof(Paths).GetMethod("SetExecutablePath", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)
            .Invoke(null, new object[] { Path.Combine(root, "Fall of Avalon.exe"), null, null, null });
        TGGraphicsBridge.Notices.Set_.Clear();
        TGGraphicsBridge.InstallCheck.Reset();
    }
    private static (TGGraphicsBridge.NoticeLevel Level, string Source, string Label, string Detail, string Summary)? Notice(string id) =>
        TGGraphicsBridge.Notices.Set_.TryGetValue(id, out var n) ? n : ((TGGraphicsBridge.NoticeLevel, string, string, string, string)?)null;
    private static string Report()
    {
        var sb = new StringBuilder();
        TGGraphicsBridge.InstallCheck.Append(sb);
        return sb.ToString();
    }
    private static void Run(string loadedFrom)
    {
        TGGraphicsBridge.InstallCheck.LoadedFrom = name => name == "TGFrameGeneration" && loadedFrom != null ? Path.Combine(root, loadedFrom.Replace('/', Path.DirectorySeparatorChar)) : null;
        TGGraphicsBridge.InstallCheck.Run();
        everyNotice.AddRange(TGGraphicsBridge.Notices.Set_.Values);
        // TG_VERBOSE=1 prints what the player would read.
        if (Environment.GetEnvironmentVariable("TG_VERBOSE") == "1")
            foreach (var notice in TGGraphicsBridge.Notices.Set_)
                Console.WriteLine("[" + Path.GetFileName(root) + "] " + notice.Value.Source + " | " + notice.Value.Label + " | " + notice.Value.Summary + "\n    " + notice.Value.Detail);
    }
    private static int Main(string[] args)
    {
        // check <game exe> [<loaded plugin path under the game folder>]: the check on a real game folder,
        // read-only, printing what the player would read.
        if (args.Length >= 2 && args[0] == "check")
        {
            typeof(Paths).GetMethod("SetExecutablePath", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)
                .Invoke(null, new object[] { args[1], null, null, null });
            root = Paths.GameRootPath;
            string loaded = args.Length >= 3 ? args[2] : null;
            TGGraphicsBridge.InstallCheck.LoadedFrom = name => loaded != null ? Path.Combine(root, loaded.Replace("TGFrameGeneration", name).Replace('/', Path.DirectorySeparatorChar)) : null;
            TGGraphicsBridge.InstallCheck.Run();
            foreach (var notice in TGGraphicsBridge.Notices.Set_)
                Console.WriteLine(notice.Value.Level + " | " + notice.Value.Source + " | " + notice.Value.Label + " | " + notice.Value.Detail + "\n");
            Console.WriteLine(Report());
            return 0;
        }
        // report <game exe> <pid> <out file>: the text report's log sections from a real game folder, read-only,
        // for the session of that process (as the game would build them), written to out file.
        if (args.Length >= 4 && args[0] == "report")
        {
            typeof(Paths).GetMethod("SetExecutablePath", BindingFlags.Static | BindingFlags.NonPublic | BindingFlags.Public)
                .Invoke(null, new object[] { args[1], null, null, null });
            string bepinex = Paths.BepInExRootPath;
            var entries = new List<TGGraphicsBridge.ReportEntry>();
            foreach (var file in new[] { "LogOutput.log", "TGGraphicsBridge/Autoload.log", "TGGraphicsBridge/TGGraphicsBridge.Native.log",
                "TGFrameGeneration/Native/TGFG.Native.log", "TGFrameGeneration/Native/sl.log", "TGBetterUpscaling/Native/TGBetterUpscaling.Native.log" })
            {
                string path = Path.Combine(bepinex, file.Replace('/', Path.DirectorySeparatorChar));
                if (File.Exists(path)) entries.Add(new TGGraphicsBridge.ReportEntry { Name = file, Path = path });
            }
            foreach (var config in Directory.GetFiles(Paths.ConfigPath, "com.wessberg.*.cfg"))
                entries.Add(new TGGraphicsBridge.ReportEntry { Name = Path.GetFileName(config), Path = config });
            entries.Add(new TGGraphicsBridge.ReportEntry { Name = "Where the mods' files are", Text = TGGraphicsBridge.InstallCheck.CompactListing() });
            string text = TGGraphicsBridge.ReportText.Build("(summary)", entries, int.Parse(args[2]));
            File.WriteAllText(args[3], text);
            Console.WriteLine("Report: " + Encoding.UTF8.GetByteCount(text) / 1024 + " KB, " + text.Split('\n').Length + " lines");
            return 0;
        }
        var made = new List<string>();
        try
        {
            Game("correct"); made.Add(root);
            Archive("");
            Run("BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            Expect(TGGraphicsBridge.Notices.Set_.Count == 0, "correct layout: no notices (" + string.Join(", ", TGGraphicsBridge.Notices.Set_.Keys) + ")");
            Expect(Report().Contains("TG Frame Generation is installed where BepInEx runs it."), "correct layout: reported installed");
            Expect(TGGraphicsBridge.InstallCheck.ProblemFor("TGFrameGeneration") == "", "correct layout: no problem for the tooltip");

            Game("inside-bepinex"); made.Add(root);
            Archive("BepInEx");
            Run(null);
            var n = Notice("install.TGFrameGeneration.misplaced");
            Expect(n.HasValue && n.Value.Level == TGGraphicsBridge.NoticeLevel.Problem && n.Value.Label == "Wrong folder", "unpacked into BepInEx: wrong folder");
            Expect(n.HasValue && n.Value.Detail.Contains("BepInEx/BepInEx") && n.Value.Detail.Contains("Move the BepInEx folder from BepInEx into the game folder"),
                "unpacked into BepInEx: says where and how: " + n?.Detail);
            Expect(n.HasValue && !n.Value.Detail.Contains("Vortex"), "no Vortex advice without Vortex");
            Expect(Notice("install.TGFrameGeneration.incomplete") == null, "unpacked into BepInEx: not also reported as missing files");
            Expect(TGGraphicsBridge.InstallCheck.ProblemFor("TGFrameGeneration").Contains("BepInEx/BepInEx"), "unpacked into BepInEx: tooltip text");

            Game("vortex-plugin"); made.Add(root);
            Touch("vortex.deployment.json", "{}");
            Archive("BepInEx/plugins");
            Run("BepInEx/plugins/BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            n = Notice("install.TGFrameGeneration.misplaced");
            Expect(n.HasValue && n.Value.Detail.Contains("BepInEx/plugins/BepInEx") && n.Value.Detail.StartsWith("Its files are in BepInEx/plugins/BepInEx"),
                "deployed as a plugin: says where: " + n?.Detail);
            Expect(n.HasValue && n.Value.Detail.Contains("In Vortex, change the mod's type to Engine Injector"), "deployed as a plugin: Vortex advice");
            Expect(Report().Contains("was loaded from BepInEx/plugins/BepInEx/plugins/TGFrameGeneration"), "deployed as a plugin: where it loaded from");

            Game("own-folder"); made.Add(root);
            Archive("TGFrameGeneration - mono");
            Run(null);
            n = Notice("install.TGFrameGeneration.misplaced");
            Expect(n.HasValue && n.Value.Detail.Contains("TGFrameGeneration - mono/BepInEx") && n.Value.Detail.Contains("from TGFrameGeneration - mono into the game folder"),
                "unpacked into a folder of its own: " + n?.Detail);

            Game("contents-in-root"); made.Add(root);
            Touch("patchers/TGFrameGeneration.Autoload.dll");
            Touch("plugins/TGFrameGeneration/TGFrameGeneration.dll");
            Touch("TGFrameGeneration/graphics-bridge-extension.json", "{ \"native\": \"Native/TGFG.Native.dll\" }");
            Run(null);
            n = Notice("install.TGFrameGeneration.misplaced");
            Expect(n.HasValue && n.Value.Detail.Contains("directly in the game folder instead of inside its BepInEx folder") &&
                n.Value.Detail.Contains("Move those folders into the BepInEx folder"), "BepInEx contents in the game folder: " + n?.Detail);

            Game("leftover-copy"); made.Add(root);
            Archive("");
            Archive("TGFrameGeneration-1.2.0");
            Run("BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            n = Notice("install.TGFrameGeneration.copy");
            Expect(n.HasValue && n.Value.Level == TGGraphicsBridge.NoticeLevel.Warning && n.Value.Detail.StartsWith("BepInEx runs it from the right place") &&
                n.Value.Detail.Contains("TGFrameGeneration-1.2.0/BepInEx"), "installed plus a leftover copy: a warning about the copy: " + n?.Detail);
            Expect(Notice("install.TGFrameGeneration.misplaced") == null && TGGraphicsBridge.InstallCheck.ProblemFor("TGFrameGeneration") == "",
                "installed plus a leftover copy: not a broken install");

            // What a Vortex uninstall can leave: its backups of an earlier install (here the IL2CPP build) restored
            // to the right place, and the new version deployed as a plugin into BepInEx/plugins/BepInEx.
            Game("stale-other-branch"); made.Add(root);
            Touch("vortex.deployment.json", "{}");
            Archive("", il2cpp: true);
            Archive("BepInEx/plugins");
            Run("BepInEx/plugins/BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            n = Notice("install.TGFrameGeneration.misplaced");
            Expect(n.HasValue && n.Value.Level == TGGraphicsBridge.NoticeLevel.Problem && n.Value.Label == "Wrong folder",
                "stale IL2CPP files plus a misplaced copy: wrong folder, not a leftover copy");
            Expect(n.HasValue && n.Value.Detail.Contains("The files in the right place are the IL2CPP build") && n.Value.Detail.Contains("left from an earlier install") &&
                n.Value.Detail.Contains("Delete them first: BepInEx/patchers/TGFrameGeneration.Autoload.dll") && n.Value.Detail.Contains("In Vortex"),
                "stale IL2CPP files: says what they are and to delete them: " + n?.Detail);
            Expect(Notice("install.TGFrameGeneration.copy") == null && TGGraphicsBridge.Notices.Set_.Values.All(v => !v.Detail.Contains("installed correctly") && !v.Detail.Contains("from the right place")),
                "stale IL2CPP files: nothing claims it is installed correctly");
            Expect(Notice("install.TGFrameGeneration.runtime") == null, "stale IL2CPP files: one row, not a second one for the branch");

            Game("stale-preloader-only"); made.Add(root);
            Archive("");
            Touch("BepInEx/patchers/TGFrameGeneration.Autoload.dll", "MZ ... BepInEx.Preloader.Core ...");
            Run("BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            n = Notice("install.TGFrameGeneration.runtime");
            Expect(n.HasValue && n.Value.Detail.Contains("The IL2CPP build of TG Frame Generation is installed"),
                "the IL2CPP preloader with the Mono plugin: the branch problem, although the plugin loaded: " + n?.Detail);

            Game("no-preloader"); made.Add(root);
            Archive("", preloader: false);
            Run("BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            n = Notice("install.TGFrameGeneration.incomplete");
            Expect(n.HasValue && n.Value.Detail.Contains("BepInEx/patchers/TGFrameGeneration.Autoload.dll"), "missing preloader named: " + n?.Detail);

            Game("other-branch"); made.Add(root);
            Archive("", il2cpp: true);
            Run(null);
            n = Notice("install.TGFrameGeneration.runtime");
            Expect(n.HasValue && n.Value.Detail.Contains("The IL2CPP build of TG Frame Generation is installed") && n.Value.Detail.Contains("ends in \"mono\""),
                "IL2CPP build on the Mono branch: " + n?.Detail);

            Game("not-loaded"); made.Add(root);
            Archive("");
            Run(null);
            n = Notice("install.TGFrameGeneration.notloaded");
            Expect(n.HasValue && n.Value.Detail.Contains("LogOutput.log"), "Mono build not loaded: points at BepInEx's log");

            Game("legacy"); made.Add(root);
            Touch("BepInEx/patchers/TGFG.Autoload.dll");
            Touch("BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            Run("BepInEx/plugins/TGFrameGeneration/TGFrameGeneration.dll");
            n = Notice("install.TGFrameGeneration.legacy");
            Expect(n.HasValue && n.Value.Label == "Old version", "1.0 preloader without a manifest: old version");
            Expect(Notice("install.TGFrameGeneration.incomplete") == null, "1.0 installed: not also reported as missing files");

            Game("absent"); made.Add(root);
            Run(null);
            Expect(TGGraphicsBridge.Notices.Set_.Count == 0, "nothing installed: no notices");
            Expect(Report().Contains("TG Frame Generation is not installed."), "nothing installed: reported");
        }
        finally
        {
            foreach (var folder in made) { try { Directory.Delete(folder, true); } catch { } }
        }
        foreach (var n in everyNotice)
        {
            Expect(n.Label.Length <= 16, "row value too long for the game's value field: " + n.Label);
            Expect(n.Summary.Length <= 240, "tooltip too long for the game's tooltip column (" + n.Summary.Length + "): " + n.Summary);
            // Players know the game's versions by the Nexus files, not by runtime names.
            Expect(n.Summary.IndexOf("IL2CPP", StringComparison.OrdinalIgnoreCase) < 0 && n.Summary.IndexOf("BepInEx 6", StringComparison.Ordinal) < 0,
                "tooltip uses a runtime name players don't know: " + n.Summary);
        }
        if (failures > 0) { Console.WriteLine(failures + " install check(s) failed."); return 1; }
        Console.WriteLine("Install checks passed: correct layout, unpacked into BepInEx, into BepInEx/plugins (Vortex), into a folder of its own, " +
            "BepInEx contents in the game folder, a leftover copy beside a correct install, stale other-branch files beside a misplaced copy, missing preloader, the other branch's build, not loaded, 1.0 left behind, not installed.");
        return 0;
    }
}
