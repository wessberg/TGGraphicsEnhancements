// SPDX-License-Identifier: MIT
// Other software in or around the game that changes what the mods built on the bridge do, found from
// the modules loaded into the game, the files beside Fall of Avalon.exe, running programs and the
// NVIDIA driver's settings for the game:
//   - OptiScaler, DLSS Enabler and the DLSSG-to-FSR3 mod replace the game's upscaler, DLSS or the
//     swapchain themselves (the same things these mods do), usually through a proxy DLL such as
//     dxgi.dll, winmm.dll or version.dll in the game folder;
//   - frame rate limits outside the game (RivaTuner Statistics Server, the NVIDIA driver's Max Frame
//     Rate) cap the frames frame generation presents, and frame generation outside the game (NVIDIA
//     Smooth Motion, Lossless Scaling) generates frames on top of the mod's;
//   - a DLSS Frame Generation override in the NVIDIA driver's profile can force it off.
// Problems become notices; everything found, known or not, goes into the report.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using BepInEx;
namespace TGGraphicsBridge
{
    public static class Compatibility
    {
        private sealed class Module { internal string Name, Path, Product, Description, Company, Version; }
        private static readonly List<string> findings = new List<string>();
        private static List<Module> modules = new List<Module>();
        private static bool scanned, scanning;
        private static float nextDriverCheck;
        private static string driverReport = "";
        // Proxy DLL names other tools use to load before the game's own DirectX runtime.
        private static readonly HashSet<string> ProxyNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "dxgi.dll", "d3d11.dll", "d3d12.dll", "dinput8.dll", "winmm.dll", "version.dll", "winhttp.dll", "wininet.dll", "dbghelp.dll", "nvngx.dll", "_nvngx.dll"
        };
        public static bool OptiScaler { get; private set; }
        // Once the plugins have loaded, on a thread of its own: searching the game folder and reading every
        // module's version take long enough to hitch a frame. The install check goes first.
        internal static void Start()
        {
            if (scanned || scanning) return;
            scanning = true;
            new Thread(Scan) { IsBackground = true, Name = "TG install and compatibility check" }.Start();
        }
        private static void Scan()
        {
            try { InstallCheck.Run(); }
            catch (Exception ex) { Add("The install check failed: " + ex.Message); }
            try
            {
                var list = new List<Module>();
                using (var process = Process.GetCurrentProcess())
                    foreach (ProcessModule m in process.Modules)
                    {
                        var module = new Module { Name = m.ModuleName, Path = m.FileName };
                        try
                        {
                            var info = m.FileVersionInfo;
                            module.Product = info.ProductName ?? ""; module.Description = info.FileDescription ?? ""; module.Company = info.CompanyName ?? "";
                            module.Version = info.FileVersion ?? "";
                        }
                        catch { module.Product = module.Description = module.Company = module.Version = ""; }
                        list.Add(module);
                    }
                modules = list;
                string game = Paths.GameRootPath;
                var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                try { foreach (var file in Directory.GetFiles(game)) files.Add(Path.GetFileName(file)); } catch { }
                CheckOptiScaler(list, files);
                CheckDlssEnabler(list, files);
                CheckRivaTuner(list, true);
                CheckOthers(list, files);
                CheckPrograms(true);
            }
            catch (Exception ex) { Add("The check for other software failed: " + ex.Message); }
            finally { scanned = true; scanning = false; }
        }
        private static void Add(string finding) { lock (findings) findings.Add(finding); }
        private static bool Mentions(Module m, string text) =>
            m.Name.IndexOf(text, StringComparison.OrdinalIgnoreCase) >= 0 || m.Product.IndexOf(text, StringComparison.OrdinalIgnoreCase) >= 0 ||
            m.Description.IndexOf(text, StringComparison.OrdinalIgnoreCase) >= 0;
        private static bool InGameFolder(Module m)
        {
            try { return string.Equals(Path.GetFullPath(Path.GetDirectoryName(m.Path) ?? "").TrimEnd('\\'), Path.GetFullPath(Paths.GameRootPath).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase); }
            catch { return false; }
        }
        private static void CheckOptiScaler(List<Module> list, HashSet<string> files)
        {
            var loaded = list.Where(m => Mentions(m, "OptiScaler")).ToList();
            var left = new[] { "OptiScaler.ini", "OptiScaler.log", "OptiScaler.dll", "OptiScaler.asi" }.Where(files.Contains).ToList();
            if (loaded.Count == 0 && left.Count == 0) return;
            OptiScaler = loaded.Count > 0;
            string modulesText = string.Join(", ", loaded.Select(m => m.Name).ToArray());
            if (OptiScaler)
            {
                Notices.Set("compat.optiscaler", NoticeLevel.Problem, "OptiScaler", "Conflict",
                    "It replaces upscaling and the swapchain as these mods do, so they break each other. Remove OptiScaler to use them.",
                    "OptiScaler is running in the game (" + modulesText + "). It replaces the game's upscaling and swapchain, which TG Better Upscaling and " +
                    "TG Frame Generation also do, so they can't work together: frame generation and FSR or XeSS may fail or misbehave. Use either OptiScaler " +
                    "or these mods. To remove OptiScaler, run its remove script in the game folder, or delete " + modulesText + " and OptiScaler.ini there, then restart the game.");
                Add("OptiScaler is loaded: " + modulesText + (left.Count > 0 ? "; files " + string.Join(", ", left.ToArray()) : "") + ".");
            }
            else
            {
                Notices.Set("compat.optiscaler", NoticeLevel.Warning, "OptiScaler", "Leftover files",
                    "Its files, such as OptiScaler.ini, are still in the game folder. Remove them if OptiScaler still loads.",
                    "The game folder has " + string.Join(", ", left.ToArray()) + " from OptiScaler, but OptiScaler isn't running. If it was renamed to load " +
                    "another way, remove it: OptiScaler can't work together with these mods.");
                Add("OptiScaler files without OptiScaler loaded: " + string.Join(", ", left.ToArray()) + ".");
            }
        }
        private static void CheckDlssEnabler(List<Module> list, HashSet<string> files)
        {
            var enabler = list.Where(m => Mentions(m, "dlss-enabler") || Mentions(m, "DLSS Enabler")).Select(m => m.Name).ToList();
            enabler.AddRange(new[] { "dlss-enabler.dll", "dlss-enabler-upscaler.dll", "dlss-enabler.log" }.Where(files.Contains).Where(f => !enabler.Contains(f)));
            if (enabler.Count > 0)
            {
                Notices.Set("compat.dlssenabler", NoticeLevel.Problem, "DLSS Enabler", "Conflict",
                    "It replaces DLSS and its frame generation. Remove it from the game folder.",
                    "DLSS Enabler is installed in the game folder (" + string.Join(", ", enabler.ToArray()) + "). It replaces DLSS and its frame generation, " +
                    "which TG Frame Generation runs through NVIDIA's own Streamline. Remove DLSS Enabler from the game folder and restart the game.");
                Add("DLSS Enabler: " + string.Join(", ", enabler.ToArray()) + ".");
            }
            var nukem = list.Where(m => Mentions(m, "dlssg_to_fsr3")).Select(m => m.Name).ToList();
            nukem.AddRange(new[] { "dlssg_to_fsr3_amd_is_better.dll", "dlssg_to_fsr3.ini", "dlssg_to_fsr3.log" }.Where(files.Contains).Where(f => !nukem.Contains(f)));
            if (nukem.Count > 0)
            {
                Notices.Set("compat.dlssgtofsr3", NoticeLevel.Problem, "DLSSG to FSR3", "Conflict",
                    "It replaces DLSS Frame Generation. Remove it; TG Frame Generation has FSR Frame Generation itself.",
                    "The DLSSG-to-FSR3 mod is installed (" + string.Join(", ", nukem.ToArray()) + "). It replaces DLSS Frame Generation with FSR's. " +
                    "TG Frame Generation offers FSR Frame Generation itself; remove that mod's files from the game folder and restart the game.");
                Add("DLSSG-to-FSR3: " + string.Join(", ", nukem.ToArray()) + ".");
            }
        }
        // RivaTuner Statistics Server hooks every game; its frame rate limit caps the frames frame generation presents.
        // first: the startup scan, which also writes the report's finding; later checks only follow the notice.
        private static void CheckRivaTuner(List<Module> list, bool first)
        {
            var hooks = list.FirstOrDefault(m => string.Equals(m.Name, "RTSSHooks64.dll", StringComparison.OrdinalIgnoreCase));
            if (hooks == null) { Notices.Clear("compat.rtss"); return; }
            string folder = Path.GetDirectoryName(hooks.Path) ?? "";
            string exe = Path.GetFileName(Process.GetCurrentProcess().MainModule?.FileName ?? "Fall of Avalon.exe");
            double limit = Limit(Path.Combine(Path.Combine(folder, "Profiles"), exe + ".cfg"));
            string where = "this game's profile";
            if (limit < 0) { limit = Limit(Path.Combine(Path.Combine(folder, "Profiles"), "Global")); where = "its global profile"; }
            if (first) Add("RivaTuner Statistics Server is hooked into the game" + (limit > 0 ? ", with a " + limit.ToString("0.##", CultureInfo.InvariantCulture) + " fps frame rate limit in " + where : limit == 0 ? ", no frame rate limit" : "") + ".");
            if (limit <= 0) Notices.Clear("compat.rtss");
            else
                Notices.Set("compat.rtss", NoticeLevel.Warning, "RivaTuner", limit.ToString("0.##", CultureInfo.InvariantCulture) + " fps limit",
                    "It limits the game to " + limit.ToString("0.##", CultureInfo.InvariantCulture) + " fps, generated frames included. Set its framerate limit to 0 for the game.",
                    "RivaTuner Statistics Server limits this game to " + limit.ToString("0.##", CultureInfo.InvariantCulture) + " fps (" + where + "). It limits the frames " +
                    "frame generation shows as well as the rendered ones, which can keep frame generation from raising the framerate and makes its frame pacing uneven. " +
                    "Set RivaTuner's Framerate limit to 0 for Fall of Avalon.exe, or use the game's own Framerate Limit.");
        }
        // [Framerate] Limit (and LimitDenominator) of a RivaTuner profile; -1 when the file has none.
        private static double Limit(string profile)
        {
            try
            {
                if (!File.Exists(profile)) return -1;
                bool section = false; double limit = -1, denominator = 1;
                foreach (var raw in File.ReadAllLines(profile))
                {
                    string line = raw.Trim();
                    if (line.StartsWith("[")) { section = string.Equals(line, "[Framerate]", StringComparison.OrdinalIgnoreCase); continue; }
                    if (!section) continue;
                    int equals = line.IndexOf('=');
                    if (equals <= 0) continue;
                    string key = line.Substring(0, equals).Trim(), value = line.Substring(equals + 1).Trim();
                    if (string.Equals(key, "Limit", StringComparison.OrdinalIgnoreCase) && double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var parsed)) limit = parsed;
                    else if (string.Equals(key, "LimitDenominator", StringComparison.OrdinalIgnoreCase) && double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var d) && d > 0) denominator = d;
                }
                return limit < 0 ? -1 : limit / denominator;
            }
            catch { return -1; }
        }
        private static void CheckOthers(List<Module> list, HashSet<string> files)
        {
            if (list.Any(m => Mentions(m, "SpecialK")))
            {
                Notices.Set("compat.specialk", NoticeLevel.Warning, "Special K", "May conflict",
                    "Its frame pacing and swapchain changes can break frame generation. Try without it.",
                    "Special K is running in the game. Its frame pacing, frame limiter and swapchain changes can conflict with frame generation's own swapchain. " +
                    "If frame generation misbehaves, try without Special K.");
                Add("Special K is loaded.");
            }
            foreach (var name in new[] { "Uniscaler.asi", "fsr3.asi", "fsr2fsr3.asi" }.Where(files.Contains))
            {
                Notices.Set("compat.uniscaler", NoticeLevel.Problem, "FSR 3 mod", "Conflict",
                    name + " replaces the game's swapchain like TG Frame Generation. Remove it and its files.",
                    "An FSR 3 frame generation mod is in the game folder (" + name + "). It replaces the game's swapchain like TG Frame Generation does. " +
                    "Remove it and its files from the game folder and restart the game.");
                Add("FSR 3 mod file: " + name + ".");
            }
            // Every proxy DLL beside the game, known or not, for the report.
            foreach (var m in list.Where(m => ProxyNames.Contains(m.Name) && InGameFolder(m)))
                Add("Loaded from the game folder: " + m.Name + " (" + Describe(m) + ")");
            foreach (var name in files.Where(f => ProxyNames.Contains(f) && !list.Any(m => string.Equals(m.Name, f, StringComparison.OrdinalIgnoreCase) && InGameFolder(m))))
                Add("In the game folder but not loaded: " + name + ".");
            foreach (var name in files.Where(f => f.EndsWith(".asi", StringComparison.OrdinalIgnoreCase) || f.EndsWith(".addon64", StringComparison.OrdinalIgnoreCase)))
                Add("Plugin file in the game folder: " + name + ".");
            foreach (var m in list.Where(m => Mentions(m, "ReShade"))) Add("ReShade is loaded: " + m.Name + " " + m.Version + ".");
        }
        private static void CheckPrograms(bool first)
        {
            try
            {
                var running = Process.GetProcessesByName("LosslessScaling");
                bool lossless = running.Length > 0;
                foreach (var process in running) process.Dispose();
                if (!lossless) Notices.Clear("compat.lossless");
                else
                {
                    Notices.Set("compat.lossless", NoticeLevel.Warning, "Lossless Scaling", "Running",
                        "If its frame generation is on for this game, turn it off: it runs on top of this mod's.",
                        "Lossless Scaling is running. If its frame generation is on for this game, it generates frames on top of TG Frame Generation's; " +
                        "use one or the other. Its own scaling also interferes with the game's upscaling.");
                    if (first) Add("Lossless Scaling is running.");
                }
            }
            catch { }
        }
        private static string Describe(Module m)
        {
            var parts = new[] { m.Product, m.Description, m.Company, m.Version }.Where(p => !string.IsNullOrEmpty(p)).Distinct().ToArray();
            return parts.Length > 0 ? string.Join(", ", parts) : "no version information";
        }
        // Every few seconds from the bridge's per-frame component: the NVIDIA driver's settings arrive
        // from the native side once it has read them.
        // Every 30 s after the startup scan, on a thread of its own: RivaTuner's limit and Lossless Scaling
        // change while the game runs (changing or closing them is the fix their notices give), so their
        // notices follow; starting either later is noticed too.
        private static float nextRefresh = 30f;
        private static volatile bool refreshing;
        private static void Refresh()
        {
            try
            {
                var list = new List<Module>();
                using (var process = Process.GetCurrentProcess())
                    foreach (ProcessModule m in process.Modules) { list.Add(new Module { Name = m.ModuleName, Path = m.FileName }); m.Dispose(); }
                CheckRivaTuner(list, false);
                CheckPrograms(false);
            }
            catch { } // the next refresh tries again
            finally { refreshing = false; }
        }
        internal static void Tick(float now)
        {
            if (scanned && !refreshing && now >= nextRefresh)
            {
                nextRefresh = now + 30f; refreshing = true;
                new Thread(Refresh) { IsBackground = true, Name = "TG compatibility refresh" }.Start();
            }
            if (now < nextDriverCheck) return;
            nextDriverCheck = now + 10f;
            var facts = GraphicsBridge.Facts();
            CheckRuntime(facts);
            if (!facts.TryGetValue("nvidia.drs", out var drs) || drs != "read") return;
            var sb = new StringBuilder();
            foreach (var fact in facts.Where(f => f.Key.StartsWith("nvidia.", StringComparison.OrdinalIgnoreCase)).OrderBy(f => f.Key))
                sb.Append(fact.Key.Substring(7)).Append('=').Append(fact.Value).Append(' ');
            driverReport = sb.ToString().Trim();
            uint limit = DriverValue(facts, "nvidia.frameRateLimit", out string limitWhere);
            if (limit > 0)
                Notices.Set("compat.nvidia.fps", NoticeLevel.Warning, "NVIDIA Max Frame Rate", limit + " fps",
                    "The driver limits this game to " + limit + " fps, generated frames included. Turn Max Frame Rate off for it in the NVIDIA App.",
                    "The NVIDIA driver limits this game to " + limit + " fps (Max Frame Rate in the " + limitWhere + "). That limits the frames frame generation shows, " +
                    "which can keep it from raising the framerate. Set Max Frame Rate to Off in the NVIDIA App or NVIDIA Control Panel, or use the game's own Framerate Limit.");
            else Notices.Clear("compat.nvidia.fps");
            uint smooth = DriverValue(facts, "nvidia.smoothMotion", out string smoothWhere);
            if (smooth != 0)
                Notices.Set("compat.nvidia.smoothmotion", NoticeLevel.Problem, "NVIDIA Smooth Motion", "On",
                    "The driver's own frame generation runs on top of this mod's. Turn it off for this game in the NVIDIA App.",
                    "NVIDIA Smooth Motion is on for this game (" + smoothWhere + "). It is the driver's own frame generation and runs on top of TG Frame Generation's. " +
                    "Turn Smooth Motion off for Fall of Avalon in the NVIDIA App (Graphics > Program Settings), or turn frame generation off here.");
            else Notices.Clear("compat.nvidia.smoothmotion");
            uint vsync = DriverValue(facts, "nvidia.vsync", out string vsyncWhere);
            if (vsync == 0x47814940 || vsync == 0x32610244 || vsync == 0x71271021 || vsync == 0x13245256)
                Notices.Set("compat.nvidia.vsync", NoticeLevel.Warning, "NVIDIA Vertical Sync", "Forced on",
                    "The driver forces VSync on for this game. Set Vertical sync to the 3D application setting in the NVIDIA App.",
                    "The NVIDIA driver forces VSync on for this game (" + vsyncWhere + "), whatever the game's own VSync setting says. With frame generation, VSync holds " +
                    "the frames on screen to the display's refresh rate. Set Vertical sync to \"Use the 3D application setting\" in the NVIDIA App or Control Panel.");
            else Notices.Clear("compat.nvidia.vsync");
            uint dlssg = DriverValue(facts, "nvidia.dlssgMode", out string dlssgWhere);
            if (dlssg == 1)
                Notices.Set("compat.nvidia.dlssg", NoticeLevel.Warning, "NVIDIA DLSS override", "FG forced off",
                    "The NVIDIA App forces DLSS Frame Generation off for this game. Reset that override.",
                    "The NVIDIA driver's profile for this game forces DLSS Frame Generation off (" + dlssgWhere + "), so DLSS Frame Generation can't generate frames. " +
                    "Reset the DLSS Frame Generation override for Fall of Avalon in the NVIDIA App.");
            else Notices.Clear("compat.nvidia.dlssg");
        }
        // NVIDIA's, AMD's and Intel's runtimes need the Visual C++ Redistributable; the mods' own DLLs carry theirs.
        private static void CheckRuntime(Dictionary<string, string> facts)
        {
            if (!facts.TryGetValue("windows.vcruntime", out var runtime)) return;
            if (runtime == "missing" || runtime.Contains("missing"))
                Notices.Set("compat.vcruntime", NoticeLevel.Problem, "Visual C++ runtime", "Missing",
                    "Install the latest Microsoft Visual C++ Redistributable (x64), then restart the game.",
                    "The Microsoft Visual C++ runtime (" + (runtime == "missing" ? "msvcp140.dll" : "vcruntime140_1.dll") + ") is neither installed in Windows nor in the game folder. " +
                    "NVIDIA Streamline, AMD FSR and Intel XeSS need it, so DLSS Frame Generation, FSR and XeSS can't load. " +
                    "Install the latest Microsoft Visual C++ Redistributable (x64) from Microsoft, then restart the game.");
            else Notices.Clear("compat.vcruntime");
        }
        // A DWORD the native side read as "0x3C@game" or "unset"; where says which profile set it.
        private static uint DriverValue(Dictionary<string, string> facts, string key, out string where)
        {
            where = "";
            if (!facts.TryGetValue(key, out var text) || text == "unset") return 0;
            int at = text.IndexOf('@');
            string value = at > 0 ? text.Substring(0, at) : text;
            where = at > 0 && text.Substring(at + 1) == "game" ? "game's driver profile" : "global driver settings";
            if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) value = value.Substring(2);
            return uint.TryParse(value, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var number) ? number : 0;
        }
        internal static void Append(StringBuilder sb)
        {
            sb.AppendLine("== Other software ==");
            if (!scanned) sb.AppendLine("Not checked yet.");
            lock (findings)
            {
                if (findings.Count == 0 && scanned) sb.AppendLine("Nothing known to interfere.");
                foreach (var finding in findings) sb.AppendLine(finding);
            }
            if (driverReport.Length > 0) sb.AppendLine("NVIDIA driver settings (game profile, else global; unset = the driver's default): " + driverReport);
            var list = modules;
            if (list.Count == 0) return;
            sb.AppendLine();
            sb.AppendLine("Loaded modules outside Windows (name, product, version):");
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            foreach (var m in list.Where(m => !m.Path.StartsWith(windows, StringComparison.OrdinalIgnoreCase)).OrderBy(m => m.Name))
                sb.AppendLine("  " + m.Name + " | " + Describe(m) + (InGameFolder(m) ? " | game folder" : ""));
        }
    }
}
