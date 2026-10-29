// SPDX-License-Identifier: MIT
// Whether the mods built on the bridge are installed where BepInEx runs them. Each mod's archive holds
// a BepInEx folder that goes into the game folder: BepInEx/patchers/<Mod>.Autoload.dll (the preloader
// that starts the native side before the game creates its device), BepInEx/plugins/<Mod>/ (the
// plugin and its copy of the bridge) and BepInEx/<Mod>/ (the native DLLs and their manifest).
//
// The usual mistakes put that folder one level too deep: unpacked into BepInEx itself
// (BepInEx/BepInEx/...), into BepInEx/plugins (which is also where a mod manager deploys a mod it
// takes for an ordinary BepInEx plugin), or into a folder of its own inside the game folder (Windows'
// "Extract All" names one after the archive). BepInEx still finds a plugin anywhere under
// BepInEx/plugins, so the mod's rows appear, but its preloader never runs and nothing native starts.
// Others: the archive for the other game branch (IL2CPP on Mono or the reverse), a partial copy, and
// TG Frame Generation 1.0's preloader left behind.
//
// Run once the plugins have loaded, on the compatibility check's thread; the findings become notices
// and a section of the report.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using BepInEx;
namespace TGGraphicsBridge
{
    public static class InstallCheck
    {
        private sealed class Mod { internal string Folder, Title; }
        private static readonly Mod[] Mods =
        {
            new Mod { Folder = "TGFrameGeneration", Title = "TG Frame Generation" },
            new Mod { Folder = "TGBetterUpscaling", Title = "TG Better Upscaling" },
            new Mod { Folder = "TGNeuralRendering", Title = "TG Neural Rendering" }
        };
        // Folders that never hold a mod's files, and are large: skipped by the search.
        private static readonly HashSet<string> Skipped = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "Fall of Avalon_Data", "MonoBleedingEdge", "dotnet", "D3D12", "reshade-shaders", "cache", "interop", "core", "unity-libs", "Backups", "diagnostics"
        };
        private static readonly List<string> findings = new List<string>();
        private static readonly Dictionary<string, string> problems = new Dictionary<string, string>();
        private static bool done;
        private static string listing = "";
        // Where BepInEx loaded a mod's plugin from, or null when it didn't; tests replace it.
        internal static Func<string, string> LoadedFrom = Loaded;
        private static string Loaded(string name)
        {
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                try { if (assembly.GetName().Name == name) return assembly.Location ?? ""; } catch { }
            }
            return null;
        }
        // For tests: forget a previous run.
        internal static void Reset()
        {
            done = false; listing = "";
            lock (findings) findings.Clear();
            lock (problems) { problems.Clear(); labels.Clear(); }
        }
        // The first problem found with a mod's installation, as a sentence for its own tooltips; "" if none.
        public static string ProblemFor(string folder)
        {
            lock (problems) return problems.TryGetValue(folder, out var text) ? text : "";
        }
        public static bool Ran => done;
        internal static void Run()
        {
            lock (findings) { if (done) return; done = true; }
            var found = new List<string>();
            try { Search(Paths.GameRootPath, 0, found); } catch (Exception ex) { Add("The game folder could not be searched: " + ex.Message); }
            bool vortex = false;
            try { vortex = Directory.GetFiles(Paths.GameRootPath, "vortex.deployment*.json").Length > 0; } catch { }
            // First: with 1.0 still installed, its files are what's missing from 1.1's layout, not a partial copy.
            bool legacy = CheckLegacy();
            foreach (var mod in Mods) Check(mod, found, vortex, legacy && mod == Mods[0]);
            foreach (var folder in new[] { "TGGraphicsBridge", "TGFrameGeneration", "TGBetterUpscaling", "TGNeuralRendering" })
            {
                string marker = Path.Combine(Path.Combine(Paths.BepInExRootPath, folder), "disable-autoload.txt");
                if (!File.Exists(marker)) continue;
                Add("BepInEx/" + folder + "/disable-autoload.txt exists: the native side of " + (folder == "TGGraphicsBridge" ? "both mods" : folder) + " is switched off.");
                Notices.Set("install." + folder + ".disabled", NoticeLevel.Warning, folder == "TGGraphicsBridge" ? "TG Graphics Bridge" : Title(folder), "Switched off",
                    "A disable-autoload.txt in BepInEx/" + folder + " switches it off. Delete that file.",
                    "BepInEx/" + folder + "/disable-autoload.txt switches the native side off. Delete that file and restart the game.");
            }
            if (findings.Count == 0) Add("Every installed mod's files are where BepInEx runs them.");
            listing = BuildListing();
        }
        private static string Title(string folder) => Mods.FirstOrDefault(m => m.Folder == folder)?.Title ?? folder;
        private static void Add(string finding) { lock (findings) findings.Add(finding); }
        // label: the row's value (a word or two); summary: its tooltip (a sentence or two); detail: the report's text.
        private static void Problem(Mod mod, string kind, string label, string summary, string detail)
        {
            lock (problems) if (!problems.ContainsKey(mod.Folder)) { problems[mod.Folder] = detail; labels[mod.Folder] = label; }
            Notices.Set("install." + mod.Folder + "." + kind, NoticeLevel.Problem, mod.Title, label, summary, detail);
            Add(mod.Title + ": " + label + ". " + detail);
        }
        private static readonly Dictionary<string, string> labels = new Dictionary<string, string>();
        // The first installation problem's row value ("Wrong folder"), for the mods' own short texts; "" if none.
        public static string ProblemLabelFor(string folder)
        {
            lock (problems) return labels.TryGetValue(folder, out var text) ? text : "";
        }
        private static string Relative(string path)
        {
            string root = Path.GetFullPath(Paths.GameRootPath).TrimEnd('\\', '/');
            string full = Path.GetFullPath(path).TrimEnd('\\', '/');
            if (string.Equals(full, root, StringComparison.OrdinalIgnoreCase)) return "the game folder";
            // Forward slashes, as every other path the mods name for the player.
            return (full.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) ? full.Substring(root.Length + 1) : full).Replace('\\', '/');
        }
        private static bool Same(string a, string b)
        {
            try { return string.Equals(Path.GetFullPath(a).TrimEnd('\\', '/'), Path.GetFullPath(b).TrimEnd('\\', '/'), StringComparison.OrdinalIgnoreCase); }
            catch { return false; }
        }
        // Up to six levels below the game folder: every DLL and manifest a mod's archive contains.
        private static void Search(string folder, int depth, List<string> found)
        {
            if (depth > 6) return;
            foreach (var file in Directory.GetFiles(folder))
            {
                string name = Path.GetFileName(file);
                if (name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) && Mods.Any(m => name.StartsWith(m.Folder, StringComparison.OrdinalIgnoreCase)) ||
                    string.Equals(name, "graphics-bridge-extension.json", StringComparison.OrdinalIgnoreCase))
                    found.Add(file);
            }
            foreach (var sub in Directory.GetDirectories(folder))
            {
                if (Skipped.Contains(Path.GetFileName(sub))) continue;
                try
                {
                    // A junction or symbolic link (Vortex deploys with those) is followed only one level into the plugins.
                    if ((File.GetAttributes(sub) & FileAttributes.ReparsePoint) != 0 && depth > 3) continue;
                    Search(sub, depth + 1, found);
                }
                catch { }
            }
        }
        private static void Check(Mod mod, List<string> found, bool vortex, bool legacy)
        {
            string plugin = Path.Combine(Path.Combine(Paths.PluginPath, mod.Folder), mod.Folder + ".dll");
            string preloader = Path.Combine(Paths.PatcherPluginPath, mod.Folder + ".Autoload.dll");
            string folder = Path.Combine(Paths.BepInExRootPath, mod.Folder);
            string manifest = Path.Combine(folder, "graphics-bridge-extension.json");
            string loadedFrom = LoadedFrom(mod.Folder);
            bool loaded = loadedFrom != null;
            loadedFrom = loadedFrom ?? "";
            // Copies of the plugin, preloader or manifest anywhere else: the archive's BepInEx folder went in one level too deep.
            var misplaced = found.Where(f =>
            {
                string name = Path.GetFileName(f);
                string parent = Path.GetFileName(Path.GetDirectoryName(f) ?? "");
                if (string.Equals(name, mod.Folder + ".dll", StringComparison.OrdinalIgnoreCase)) return string.Equals(parent, mod.Folder, StringComparison.OrdinalIgnoreCase) && !Same(f, plugin);
                if (string.Equals(name, mod.Folder + ".Autoload.dll", StringComparison.OrdinalIgnoreCase)) return !Same(f, preloader);
                if (string.Equals(name, "graphics-bridge-extension.json", StringComparison.OrdinalIgnoreCase)) return string.Equals(parent, mod.Folder, StringComparison.OrdinalIgnoreCase) && !Same(f, manifest);
                return false;
            }).ToList();
            bool installed = File.Exists(plugin) || File.Exists(preloader) || File.Exists(manifest) || loaded || misplaced.Count > 0;
            if (!installed) { Add(mod.Title + " is not installed."); return; }
            if (loaded && loadedFrom.Length > 0 && !Same(loadedFrom, plugin))
                Add(mod.Title + " was loaded from " + Relative(loadedFrom) + ", not from " + Relative(plugin) + ".");
            // What runs, not which files exist: files in the right place can be a build for the other game
            // branch or an older version, left from an earlier install (a mod manager restores its backups of
            // them when a mod is uninstalled), while BepInEx runs the plugin from a copy in the wrong folder.
            bool runsHere = loaded && (loadedFrom.Length > 0 ? Same(loadedFrom, plugin) : File.Exists(plugin));
            string otherHere = OtherRuntime(plugin) ?? OtherRuntime(preloader);
            string versionHere = FileVersion(plugin) ?? FileVersion(preloader), versionRunning = loadedFrom.Length > 0 ? FileVersion(loadedFrom) : null;
            bool sameBuild = otherHere == null && versionHere != null && versionHere == versionRunning;
            // Installed where it runs (or the same build twice), with a second copy somewhere else: an unpacked
            // archive left behind after its BepInEx folder was merged. It works; the copy is only in the way.
            if (misplaced.Count > 0 && File.Exists(preloader) && File.Exists(manifest) && (runsHere || sameBuild))
            {
                string sample = misplaced.FirstOrDefault(f => f.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) && Path.GetFileName(f).Length == mod.Folder.Length + 4) ?? misplaced[0];
                string root = ArchiveRoot(sample) ?? Paths.GameRootPath;
                string copy = Same(root, Paths.GameRootPath) ? "the plugins, patchers and " + mod.Folder + " folders directly in the game folder" : Relative(root) + "/BepInEx";
                Notices.Set("install." + mod.Folder + ".copy", NoticeLevel.Warning, mod.Title, "Extra copy",
                    "A second copy of its files is in " + copy + ". Delete it.",
                    "BepInEx runs it from the right place, but a second copy of its files is in " + copy + "." +
                    (runsHere ? "" : " BepInEx loaded the plugin from that copy, the same version.") + " Delete that copy and restart the game.");
                Add(mod.Title + " is installed, with another copy in " + string.Join(", ", misplaced.Select(Relative).ToArray()) + ".");
                misplaced.Clear(); // the installed files are checked as usual below
            }
            if (misplaced.Count > 0)
            {
                // Where the archive's BepInEx folder ended up.
                string sample = misplaced.FirstOrDefault(f => f.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) && Path.GetFileName(f).Length == mod.Folder.Length + 4) ?? misplaced[0];
                string root = ArchiveRoot(sample) ?? Paths.GameRootPath;
                string where = Relative(root);
                string how, move;
                if (Same(root, Paths.GameRootPath))
                {
                    // The contents of the archive's BepInEx folder went straight into the game folder.
                    how = "Its plugins, patchers and " + mod.Folder + " folders are directly in the game folder instead of inside its BepInEx folder, " +
                        "so BepInEx never runs its preloader and nothing native starts. ";
                    move = "move those folders into the BepInEx folder and let Windows merge them with the ones there. Then restart the game.";
                }
                else
                {
                    how = "Its files are in " + where + "/BepInEx instead of the game folder's own BepInEx folder, " +
                        "so BepInEx never runs its preloader and nothing native starts. ";
                    move = "move the BepInEx folder from " + where + " into the game folder, next to Fall of Avalon.exe, and let Windows merge it with the " +
                        "BepInEx folder there. Then restart the game.";
                }
                string fix = vortex ? "In Vortex, change the mod's type to Engine Injector and deploy again. Without Vortex, " + move
                    : char.ToUpperInvariant(move[0]) + move.Substring(1);
                // The tooltip: where the files are and the one fix, in two or three short sentences.
                string summary = (Same(root, Paths.GameRootPath) ? "Its folders are in the game folder, not in BepInEx, so it can't start. "
                        : "Its files are in " + where + "/BepInEx, where they can't start. ") +
                    (vortex ? "In Vortex, set its mod type to Engine Injector and deploy again."
                        : Same(root, Paths.GameRootPath) ? "Move them into BepInEx." : "Move that BepInEx folder into the game folder.");
                // Whatever sits in the right place meanwhile: what it is, and that it goes first.
                string stale = "";
                if (File.Exists(plugin) || File.Exists(preloader))
                {
                    // Players know the game's two versions by the Nexus files ("mono" or not), not by runtime names.
                    summary += " First delete the old copy in BepInEx/plugins/" + mod.Folder + (otherHere != null ? " (for " + VersionName(otherHere) + ")" : "") + ".";
                    string what = otherHere != null
                        ? "the " + otherHere + " build (for " + VersionName(otherHere) + ")" + (versionHere != null ? " of version " + versionHere : "") + ", left from an earlier install, which this version of the game can't run"
                        : versionHere != null && versionRunning != null && versionHere != versionRunning
                            ? "version " + versionHere + ", left from an earlier install, and BepInEx runs version " + versionRunning + " from the wrong folder instead"
                            : "left from an earlier install, and BepInEx doesn't run them";
                    stale = "The files in the right place are " + what + ". Delete them first: BepInEx/patchers/" + mod.Folder +
                        ".Autoload.dll, BepInEx/plugins/" + mod.Folder + " and BepInEx/" + mod.Folder + ". ";
                }
                Problem(mod, "misplaced", "Wrong folder", summary, how + stale + fix);
                Add(mod.Title + " files outside the places BepInEx runs them: " + string.Join(", ", misplaced.Select(Relative).ToArray()));
                if (stale.Length > 0) Add(mod.Title + " in the right place: " + (otherHere ?? "this branch's") + " build, version " + (versionHere ?? "unknown") + "; running: " + (versionRunning ?? "unknown") + ".");
                return; // the files in the right place are described above
            }
            var missing = new List<string>();
            if (!File.Exists(preloader)) missing.Add(Relative(preloader));
            if (!File.Exists(plugin)) missing.Add(Relative(plugin));
            if (!File.Exists(manifest)) missing.Add(Relative(manifest));
            else
            {
                try
                {
                    string json = File.ReadAllText(manifest);
                    foreach (var key in new[] { "native", "bridge" })
                    {
                        string relative = Field(json, key);
                        if (relative != null && !File.Exists(Path.Combine(folder, relative.Replace('/', Path.DirectorySeparatorChar))))
                            missing.Add(Relative(Path.Combine(folder, relative.Replace('/', Path.DirectorySeparatorChar))));
                    }
                }
                catch (Exception ex) { Add(mod.Title + "'s manifest could not be read: " + ex.Message); }
            }
            if (missing.Count > 0 && misplaced.Count == 0 && !legacy)
                Problem(mod, "incomplete", "Missing files",
                    missing.Count + " of its files " + (missing.Count == 1 ? "is" : "are") + " missing. Unpack the whole archive into the game folder again.",
                    "These files of " + mod.Title + " are missing: " + string.Join(", ", missing.ToArray()) +
                    ". Unpack the whole archive into the game folder, next to Fall of Avalon.exe, and restart the game.");
            // The plugin or the preloader built for the other game branch: BepInEx skips it without an error.
            if (otherHere != null)
                Problem(mod, "runtime", "Wrong build",
                    "This copy is for " + VersionName(otherHere) + ", but you play " + VersionName(IsIl2Cpp ? "IL2CPP" : "Mono") + ". Install the file " +
                    (IsIl2Cpp ? "without \"mono\" in its name." : "ending in \"mono\"."),
                    "The " + otherHere + " build of " + mod.Title + " is installed, but the game runs its " +
                    (IsIl2Cpp ? "default branch (IL2CPP, BepInEx 6)" : "Mono branch (BepInEx 5)") + ". Remove it and install the " +
                    (IsIl2Cpp ? "file without \"mono\" in its name" : "file whose name ends in \"mono\"") + " from the Nexus page.");
            else if (!loaded && File.Exists(plugin))
                Problem(mod, "notloaded", "Not loaded", "BepInEx didn't load it. BepInEx/LogOutput.log says why.",
                    "BepInEx found " + Relative(plugin) + " but didn't load it. BepInEx/LogOutput.log says why.");
            if (missing.Count == 0 && otherHere == null && runsHere) Add(mod.Title + " is installed where BepInEx runs it.");
        }
        // A DLL's file version as major.minor.build, or null (no file, no version).
        private static string FileVersion(string path)
        {
            try
            {
                if (!File.Exists(path)) return null;
                string version = System.Diagnostics.FileVersionInfo.GetVersionInfo(path).FileVersion;
                if (string.IsNullOrEmpty(version)) return null;
                var parts = version.Split('.');
                return string.Join(".", parts.Take(Math.Min(3, parts.Length)).ToArray());
            }
            catch { return null; }
        }
        private static bool IsIl2Cpp
        {
#if BEPINEX_IL2CPP
            get { return true; }
#else
            get { return false; }
#endif
        }
        // The game version a build is for, as players know it from the Nexus files: the normal one (IL2CPP,
        // BepInEx 6) or the Mono one, whose files end in "mono" (BepInEx 5).
        private static string VersionName(string runtime) => runtime == "IL2CPP" ? "the game's normal version" : "the game's Mono version";
        // "IL2CPP" or "Mono" when a plugin or preloader DLL was built for the other game branch, else null.
        // The IL2CPP plugins reference Il2CppInterop and the IL2CPP preloaders BepInEx 6's
        // BepInEx.Preloader.Core; the Mono builds reference neither.
        private static string OtherRuntime(string file)
        {
            try
            {
                if (!File.Exists(file)) return null;
                string text = Encoding.ASCII.GetString(File.ReadAllBytes(file));
                bool il2cpp = text.IndexOf("Il2CppInterop", StringComparison.Ordinal) >= 0 || text.IndexOf("BepInEx.Preloader.Core", StringComparison.Ordinal) >= 0;
                if (il2cpp && !IsIl2Cpp) return "IL2CPP";
                if (!il2cpp && IsIl2Cpp) return "Mono";
            }
            catch { }
            return null;
        }
        // The folder an archive's BepInEx folder was unpacked into, from a file of it.
        private static string ArchiveRoot(string file)
        {
            var directory = new DirectoryInfo(Path.GetDirectoryName(file) ?? "");
            for (var d = directory; d != null; d = d.Parent)
                if (string.Equals(d.Name, "BepInEx", StringComparison.OrdinalIgnoreCase) && !Same(d.FullName, Paths.BepInExRootPath))
                    return d.Parent?.FullName;
            // No BepInEx folder of its own: its contents went straight into a folder (plugins/<Mod> under it).
            for (var d = directory; d != null; d = d.Parent)
                if (string.Equals(d.Name, "plugins", StringComparison.OrdinalIgnoreCase) || string.Equals(d.Name, "patchers", StringComparison.OrdinalIgnoreCase))
                    return d.Parent?.FullName;
            return directory.FullName;
        }
        // Whether TG Frame Generation 1.0 (its own preloader, no 1.1 manifest) is what is installed.
        private static bool CheckLegacy()
        {
            string legacy = Path.Combine(Paths.PatcherPluginPath, "TGFG.Autoload.dll");
            if (!File.Exists(legacy)) return false;
            var mod = Mods[0];
            if (!File.Exists(Path.Combine(Path.Combine(Paths.BepInExRootPath, mod.Folder), "graphics-bridge-extension.json")))
            {
                Problem(mod, "legacy", "Old version", "Version 1.0 is still installed. Delete it (BepInEx/patchers/TGFG.Autoload.dll first), then install this one.",
                    "BepInEx/patchers/TGFG.Autoload.dll is TG Frame Generation 1.0's preloader, which runs a graphics bridge of its own. " +
                    "Delete it and the rest of 1.0 (BepInEx/plugins/TGFrameGeneration and BepInEx/TGFrameGeneration), then install the current version.");
                return true;
            }
            Notices.Set("install.TGFrameGeneration.legacyfile", NoticeLevel.Warning, mod.Title, "Old file",
                "Delete BepInEx/patchers/TGFG.Autoload.dll, left from version 1.0.",
                "BepInEx/patchers/TGFG.Autoload.dll is left over from TG Frame Generation 1.0 and is no longer used. Delete it.");
            Add("Left over from TG Frame Generation 1.0: BepInEx/patchers/TGFG.Autoload.dll.");
            return false;
        }
        private static string Field(string json, string key)
        {
            var match = System.Text.RegularExpressions.Regex.Match(json, "\"" + System.Text.RegularExpressions.Regex.Escape(key) + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
            return match.Success ? System.Text.RegularExpressions.Regex.Unescape(match.Groups[1].Value) : null;
        }
        internal static void Append(StringBuilder sb)
        {
            sb.AppendLine("== Installation ==");
            sb.AppendLine("Game folder: " + Paths.GameRootPath);
            lock (findings) foreach (var finding in findings) sb.AppendLine(finding);
        }
        internal static string Listing() => listing.Length > 0 ? listing : BuildListing();
        // For the text report: the game folder's own files (proxy DLLs, other tools), the preloaders, the
        // plugin folders by name, and every file of these mods wherever the search found one.
        internal static string CompactListing()
        {
            var sb = new StringBuilder();
            try
            {
                var root = new DirectoryInfo(Paths.GameRootPath);
                sb.AppendLine("Game folder: " + string.Join(", ", root.GetFiles().Where(f => !f.Name.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
                    .Select(f => f.Name).OrderBy(n => n).ToArray()));
                sb.AppendLine("Folders: " + string.Join(", ", root.GetDirectories().Select(d => d.Name).OrderBy(n => n).ToArray()));
                if (Directory.Exists(Paths.PatcherPluginPath))
                    sb.AppendLine("BepInEx/patchers: " + string.Join(", ", Directory.GetFiles(Paths.PatcherPluginPath, "*", SearchOption.AllDirectories)
                        .Select(f => Relative(f).Substring("BepInEx/patchers/".Length)).ToArray()));
                if (Directory.Exists(Paths.PluginPath))
                    sb.AppendLine("BepInEx/plugins: " + string.Join(", ", new DirectoryInfo(Paths.PluginPath).GetDirectories().Select(d => d.Name + "/").ToArray()));
                var found = new List<string>();
                Search(Paths.GameRootPath, 0, found);
                foreach (var file in found.OrderBy(f => f))
                {
                    var info = new FileInfo(file);
                    string version = FileVersion(file);
                    sb.AppendLine("  " + Relative(file) + "  " + info.Length + " bytes, " + info.LastWriteTime.ToString("yyyy-MM-dd HH:mm", System.Globalization.CultureInfo.InvariantCulture) +
                        (version != null ? ", " + version : "") + ((info.Attributes & FileAttributes.ReparsePoint) != 0 ? ", link" : "") +
                        (OtherRuntime(file) != null ? ", " + OtherRuntime(file) + " build" : ""));
                }
            }
            catch (Exception ex) { sb.AppendLine("Listing failed: " + ex.Message); }
            return sb.ToString().TrimEnd();
        }
        // The game folder's files and BepInEx's folders, with sizes and dates, for the report.
        private static string BuildListing()
        {
            var sb = new StringBuilder();
            try
            {
                sb.AppendLine("Game folder (top level):");
                foreach (var file in new DirectoryInfo(Paths.GameRootPath).GetFiles().OrderBy(f => f.Name))
                    sb.AppendLine("  " + file.Name + "  " + file.Length + " bytes  " + file.LastWriteTime.ToString("yyyy-MM-dd HH:mm", System.Globalization.CultureInfo.InvariantCulture));
                foreach (var dir in new DirectoryInfo(Paths.GameRootPath).GetDirectories().OrderBy(d => d.Name)) sb.AppendLine("  " + dir.Name + "/");
                sb.AppendLine();
                sb.AppendLine("BepInEx:");
                List(sb, new DirectoryInfo(Paths.BepInExRootPath), 1);
            }
            catch (Exception ex) { sb.AppendLine("Listing failed: " + ex.Message); }
            return sb.ToString();
        }
        private static void List(StringBuilder sb, DirectoryInfo folder, int depth)
        {
            if (depth > 6) return;
            string indent = new string(' ', depth * 2);
            foreach (var file in folder.GetFiles().OrderBy(f => f.Name))
                if (!file.Name.EndsWith(".vortex_backup", StringComparison.OrdinalIgnoreCase))
                    sb.AppendLine(indent + file.Name + "  " + file.Length + " bytes  " + file.LastWriteTime.ToString("yyyy-MM-dd HH:mm", System.Globalization.CultureInfo.InvariantCulture));
            foreach (var sub in folder.GetDirectories().OrderBy(d => d.Name))
            {
                sb.AppendLine(indent + sub.Name + "/");
                if (Skipped.Contains(sub.Name)) continue;
                try { List(sb, sub, depth + 1); } catch { }
            }
        }
    }
}
