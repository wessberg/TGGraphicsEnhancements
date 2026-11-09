// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using BepInEx;
using BepInEx.Logging;

namespace TGGraphicsBridge.Autoload
{
    // Loads TG Graphics Bridge's native DLL early enough to see the game create its Direct3D 11 device
    // and swapchain, registers the native extensions of the mods built on it, and starts it. A mod
    // takes part by shipping BepInEx/<its folder>/graphics-bridge-extension.json naming its DLL and the
    // copy of the bridge it bundles.
    //
    // Every mod built on the bridge bundles all of it, under its own folders, so a player downloads one
    // mod and Vortex never sees two mods deploy the same file. Each copy of this preloader is its own
    // assembly (<mod>.Autoload), and all of them run. One bridge starts per process:
    //   - the copy bundled with the newest bridge version starts it, with every installed extension;
    //   - a copy with an older bridge stays idle, unless no newer copy has started the bridge by the end
    //     of the preloader (its Finish), when it starts its own;
    //   - once a copy has started the bridge, a process-wide marker tells the others.
    // BepInEx itself keeps only the newest copy of the bridge's gameplay plugin (same GUID in every mod),
    // so the managed and native bridge come from the same version.
    internal static class Bootstrap
    {
        // The version of the bridge bundled with this copy, and the interface this preloader speaks.
        internal const string BridgeVersion = "1.2.4";
        private const uint ExpectedAbi = 0x00010100;
        private const string ManifestName = "graphics-bridge-extension.json";
        private const string StartedKey = "TGGraphicsBridge.Autoload.StartedBy";
        private const uint LoadLibrarySearchDllLoadDir = 0x00000100;
        private const uint LoadLibrarySearchDefaultDirs = 0x00001000;
        private static int attempted;
        private static bool deferred;
        private static List<Extension> discovered;
        // Kept for the process lifetime: hooks reference the DLL even when bootstrap fails part way.
        private static IntPtr nativeModule;
        private static ManualLogSource logger;
        private static string logPath, root;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbi();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Unicode)] private delegate int AddExtension([MarshalAs(UnmanagedType.LPWStr)] string path);
        // Matches uint32_t __stdcall TGGraphicsBridge_Bootstrap(void*).
        [UnmanagedFunctionPointer(CallingConvention.StdCall)] private delegate uint NativeBootstrap(IntPtr unused);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        private static extern uint GetModuleFileNameW(IntPtr module, StringBuilder filename, uint size);

        private sealed class Extension { internal string Name, Folder, Native, Bridge, Hash; internal Version BridgeVersion; }

        private static string StartedBy
        {
            get { return AppDomain.CurrentDomain.GetData(StartedKey) as string; }
            set { AppDomain.CurrentDomain.SetData(StartedKey, value); }
        }

        // The preloader's Initialize: start the bridge unless another installed mod bundles a newer one.
        internal static void Run(string runtime)
        {
            if (Interlocked.Exchange(ref attempted, 1) != 0) return;
            try
            {
                logger = Logger.CreateLogSource("TGGraphicsBridge.Autoload");
                if (Environment.OSVersion.Platform != PlatformID.Win32NT || IntPtr.Size != 8)
                    throw new PlatformNotSupportedException("TG Graphics Bridge requires Windows x64.");
                root = Path.GetFullPath(Path.Combine(Paths.BepInExRootPath, "TGGraphicsBridge"));
                // Only the log goes there: a folder that can't be created costs the log file, not the bridge.
                try { Directory.CreateDirectory(root); }
                catch (Exception ex) { logger.LogWarning("Could not create " + root + " (" + ex.Message + "); Autoload.log isn't written this session."); }
                logPath = Path.Combine(root, "Autoload.log");
                int pid;
                using (var process = Process.GetCurrentProcess()) pid = process.Id;
                Write("=== TGGraphicsBridge.Autoload " + BridgeVersion + " bundled with " + Identity.Title + "; PID=" + pid + "; " + runtime + " ===");
                if (StartedBy != null) { Write("The bridge was already started by " + StartedBy + "; this copy does nothing."); return; }
                if (File.Exists(Path.Combine(root, "disable-autoload.txt")))
                {
                    Write("DISABLED by BepInEx/TGGraphicsBridge/disable-autoload.txt. No native DLL is loaded.");
                    return;
                }
                // Restrict this bootstrap to the intended game, not every Unity process sharing a BepInEx
                // install or started by a mod manager.
                string exe = ModulePath(IntPtr.Zero);
                if (!string.Equals(Path.GetFileName(exe), "Fall of Avalon.exe", StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Unexpected executable: " + exe);
                // TG Frame Generation 1.0.x loaded its native DLL with a preloader of its own, which also
                // starts a graphics bridge of its own. Without a 1.1 manifest in its folder, that version is
                // still installed, and two bridges hooking the same device cannot run side by side.
                string legacy = Path.Combine(Paths.PatcherPluginPath, "TGFG.Autoload.dll");
                if (File.Exists(legacy))
                {
                    if (!File.Exists(Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration", ManifestName)))
                    {
                        Write("TG Frame Generation 1.0.x is installed (" + legacy + "). It runs a graphics bridge of its own, which cannot run " +
                              "beside this one: update TG Frame Generation to 1.1.0 or newer. Until then " + Identity.Title + "'s native side stays off.", true);
                        return;
                    }
                    Write("An older TG Frame Generation preloader is still installed at " + legacy + ". It is no longer used; delete it.", true);
                }

                if (File.Exists(Path.Combine(Paths.BepInExRootPath, Identity.Folder, "disable-autoload.txt")))
                {
                    Write(Identity.Title + " is disabled by BepInEx/" + Identity.Folder + "/disable-autoload.txt; this copy does nothing.");
                    return;
                }
                discovered = Discover(Paths.BepInExRootPath);
                if (discovered.Count == 0)
                {
                    Write("No mod built on TG Graphics Bridge is installed (no BepInEx/*/" + ManifestName + "). Nothing to load.");
                    return;
                }
                var own = Own();
                if (own == null) { Write(Identity.Title + "'s own " + ManifestName + " or bundled bridge is missing; this copy does nothing.", true); return; }
                if (own.BridgeVersion != new Version(BridgeVersion))
                    Write(Identity.Title + "'s " + ManifestName + " says bridge " + own.BridgeVersion + ", this preloader was built with " + BridgeVersion + ".", true);
                Extension newest = own;
                foreach (var e in discovered) if (e.BridgeVersion > newest.BridgeVersion) newest = e;
                if (newest.BridgeVersion > own.BridgeVersion)
                {
                    deferred = true;
                    Write(newest.Name + " bundles a newer TG Graphics Bridge (" + newest.BridgeVersion + " against " + own.BridgeVersion +
                          "); its preloader starts the bridge. This copy starts its own only if that one does not.");
                    return;
                }
                WarnAboutDifferentBuilds(own);
                Start(own);
            }
            catch (Exception ex)
            {
                // An ordinary loader/file error must not keep BepInEx's other plugins from loading.
                Write("AUTOLOAD FAILED: " + ex, true);
            }
        }
        // The preloader's Finish: the fallback when a newer copy was expected to start the bridge and did not.
        internal static void Finish()
        {
            if (!deferred || StartedBy != null) return;
            deferred = false;
            try
            {
                var own = Own();
                if (own == null) return;
                Write("No newer copy of the bridge started; " + Identity.Title + " starts its own (" + own.BridgeVersion + ").", true);
                Start(own);
            }
            catch (Exception ex) { Write("AUTOLOAD FAILED: " + ex, true); }
        }
        private static Extension Own()
        {
            foreach (var e in discovered)
                if (string.Equals(Path.GetFileName(e.Folder), Identity.Folder, StringComparison.OrdinalIgnoreCase) && e.Bridge != null) return e;
            return null;
        }
        // Copies of one bridge version are meant to be the same files. When they are not (a mod updated
        // without its bridge version, or a partial deployment), this copy runs and the player is told.
        private static void WarnAboutDifferentBuilds(Extension own)
        {
            foreach (var e in discovered)
            {
                if (e == own || e.Bridge == null || e.BridgeVersion != own.BridgeVersion) continue;
                try
                {
                    if (Sha256(e.Bridge) != Sha256(own.Bridge))
                        Write(own.Name + " and " + e.Name + " bundle different builds of TG Graphics Bridge " + own.BridgeVersion + "; " + own.Name +
                              "'s copy runs. Update both mods to their latest versions.", true);
                }
                catch (Exception ex) { Write("Cannot compare " + e.Name + "'s copy of the bridge: " + ex.Message, true); }
            }
        }
        private static void Start(Extension own)
        {
            string nativePath = own.Bridge;
            if (GetModuleHandleW("TGGraphicsBridge.Native.dll") != IntPtr.Zero)
                throw new InvalidOperationException("TGGraphicsBridge.Native.dll was already loaded by something other than these preloaders.");
            // Everything that reads a file happens before this copy claims the process: a bridge it cannot
            // read leaves the others free to start theirs, and an unreadable extension is left out.
            RequireAmd64Pe(nativePath);
            string bridgeHash = Sha256(nativePath);
            var usable = new List<Extension>();
            foreach (var e in discovered)
            {
                try
                {
                    RequireAmd64Pe(e.Native);
                    e.Hash = Sha256(e.Native);
                    usable.Add(e);
                }
                catch (Exception ex) { Write(e.Name + "'s native DLL cannot be used (" + ex.Message + "); the bridge starts without it.", true); continue; }
                if (GetModuleHandleW(Path.GetFileName(e.Native)) != IntPtr.Zero)
                    Write(e.Name + " was already loaded by something else (an older preloader?); the bridge starts it regardless.", true);
            }
            if (usable.Count == 0) { Write("No usable extension; the bridge is not loaded.", true); return; }
            Write("Native=" + nativePath + " (bundled with " + own.Name + ", bridge " + own.BridgeVersion + "); SHA256=" + bridgeHash);
            nativeModule = LoadLibraryExW(nativePath, IntPtr.Zero, LoadLibrarySearchDllLoadDir | LoadLibrarySearchDefaultDirs);
            if (nativeModule == IntPtr.Zero)
            {
                int error = Marshal.GetLastWin32Error();
                throw new Win32Exception(error, "LoadLibraryExW failed for " + nativePath +
                    ". Error 126 can mean a missing dependency, not a missing TGGraphicsBridge.Native.dll.");
            }
            // From here on this process has its bridge: no other copy may load another one.
            StartedBy = Identity.Title + " (bridge " + own.BridgeVersion + ", " + nativePath + ")";
            uint abi = Bind<GetAbi>("TGGraphicsBridge_GetAbiVersion")();
            if (abi != ExpectedAbi)
                throw new InvalidOperationException("Native ABI 0x" + abi.ToString("X8") + " does not match expected 0x" + ExpectedAbi.ToString("X8") + ".");
            var add = Bind<AddExtension>("TGGraphicsBridge_AddExtension");
            foreach (var e in usable)
            {
                int result = add(e.Native);
                Write("Extension " + e.Name + " (" + e.Native + ", SHA256=" + e.Hash + ") " +
                      (result == 0 ? "registered" : "refused: " + result), result != 0);
            }
            // Synchronous: Unity is not allowed to race ahead of hook installation.
            // Do NOT move this to ThreadPool/Task.Run or a plugin Awake() callback.
            int pid;
            using (var process = Process.GetCurrentProcess()) pid = process.Id;
            uint code = Bind<NativeBootstrap>("TGGraphicsBridge_Bootstrap")(IntPtr.Zero);
            Write("TGGraphicsBridge_Bootstrap returned " + code + " in PID=" + pid + ".");
            if (code != 0)
                throw new InvalidOperationException("Native bootstrap failed (code " + code + "). See BepInEx/TGGraphicsBridge/TGGraphicsBridge.Native.log.");
            Write("AUTOLOAD COMPLETED. This proves the bootstrap returned success, NOT that the game's swapchain was seen; " +
                  "TGGraphicsBridge.Native.log and each extension's own log say what happened next.");
        }

        // Every BepInEx/<folder>/graphics-bridge-extension.json whose native DLL lies inside that folder,
        // with the copy of the bridge that folder bundles.
        private static List<Extension> Discover(string bepinex)
        {
            var found = new List<Extension>();
            string[] folders;
            try { folders = Directory.GetDirectories(bepinex); } catch (Exception ex) { Write("Cannot list " + bepinex + ": " + ex.Message, true); return found; }
            Array.Sort(folders, StringComparer.OrdinalIgnoreCase);
            foreach (string folder in folders)
            {
                string manifest = Path.Combine(folder, ManifestName);
                if (!File.Exists(manifest)) continue;
                try
                {
                    string json = File.ReadAllText(manifest);
                    string name = Field(json, "name") ?? Path.GetFileName(folder);
                    string native = Inside(folder, Field(json, "native"), manifest, "native");
                    if (native == null) continue;
                    if (File.Exists(Path.Combine(folder, "disable-autoload.txt")))
                    {
                        Write(name + " is disabled by " + Path.Combine(folder, "disable-autoload.txt") + "; its native DLL is not loaded.");
                        continue;
                    }
                    // A folder a mod leaves behind when it is removed by hand (it keeps logs and backups):
                    // without the mod's preloader and plugin, its native DLL would run with nothing to control it.
                    string mod = Path.GetFileName(folder);
                    if (!File.Exists(Path.Combine(Paths.PatcherPluginPath, mod + ".Autoload.dll")) ||
                        !File.Exists(Path.Combine(Paths.PluginPath, mod, mod + ".dll")))
                    {
                        Write(name + " is not installed (no BepInEx/patchers/" + mod + ".Autoload.dll or BepInEx/plugins/" + mod + "/" + mod +
                              ".dll); its folder is left over and ignored.");
                        continue;
                    }
                    var e = new Extension { Name = name, Folder = folder, Native = native };
                    // The bridge this mod bundles; a mod without one can still be hosted by another's.
                    string bridge = Field(json, "bridge");
                    Version version;
                    if (bridge != null && Version.TryParse(Field(json, "bridgeVersion") ?? "", out version))
                    {
                        // A copy whose file is missing does not count: no other copy waits for it.
                        e.Bridge = Inside(folder, bridge, manifest, "bridge");
                        if (e.Bridge != null) e.BridgeVersion = version;
                    }
                    if (e.BridgeVersion == null) e.BridgeVersion = new Version(0, 0);
                    found.Add(e);
                    Write("Found " + name + " in " + folder + (e.Bridge != null ? " (bundles bridge " + e.BridgeVersion + ")" : ""));
                }
                catch (Exception ex) { Write("Cannot read " + manifest + ": " + ex.Message, true); }
            }
            return found;
        }
        // A file a manifest names: inside the manifest's own folder and present, or null.
        private static string Inside(string folder, string relative, string manifest, string field)
        {
            if (string.IsNullOrEmpty(relative)) { Write(manifest + " names no " + field + " DLL; ignored.", true); return null; }
            string rootPath = Path.GetFullPath(folder).TrimEnd('\\', '/') + Path.DirectorySeparatorChar;
            string path = Path.GetFullPath(Path.Combine(folder, relative.Replace('/', Path.DirectorySeparatorChar)));
            if (Path.IsPathRooted(relative) || !path.StartsWith(rootPath, StringComparison.OrdinalIgnoreCase))
            {
                Write(manifest + " points outside its own folder (" + relative + "); ignored.", true);
                return null;
            }
            if (!File.Exists(path)) { Write(manifest + ": " + path + " is missing; ignored.", true); return null; }
            return path;
        }
        // One string field of the flat manifest; the preloader carries no JSON library.
        private static string Field(string json, string key)
        {
            var match = Regex.Match(json, "\"" + Regex.Escape(key) + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
            return match.Success ? Regex.Unescape(match.Groups[1].Value) : null;
        }
        private static T Bind<T>(string symbol) where T : Delegate
        {
            IntPtr address = GetProcAddress(nativeModule, symbol);
            if (address == IntPtr.Zero) throw new EntryPointNotFoundException(symbol);
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        private static string ModulePath(IntPtr module)
        {
            var buffer = new StringBuilder(32768);
            uint count = GetModuleFileNameW(module, buffer, (uint)buffer.Capacity);
            if (count == 0 || count >= buffer.Capacity)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot resolve module path.");
            return buffer.ToString();
        }
        private static void RequireAmd64Pe(string path)
        {
            using (var stream = File.OpenRead(path))
            using (var reader = new BinaryReader(stream))
            {
                if (stream.Length < 64 || reader.ReadUInt16() != 0x5A4D)
                    throw new BadImageFormatException("Not a PE image: " + path);
                stream.Position = 0x3C;
                int pe = reader.ReadInt32();
                if (pe < 64 || pe > stream.Length - 6)
                    throw new BadImageFormatException("Invalid PE header: " + path);
                stream.Position = pe;
                if (reader.ReadUInt32() != 0x00004550 || reader.ReadUInt16() != 0x8664)
                    throw new BadImageFormatException("Expected an AMD64 native DLL: " + path);
            }
        }
        private static string Sha256(string path)
        {
            using (var stream = File.OpenRead(path))
            using (var hash = SHA256.Create())
                return BitConverter.ToString(hash.ComputeHash(stream)).Replace("-", "");
        }
        private static void Write(string message, bool error = false)
        {
            try
            {
                if (logger != null) { if (error) logger.LogWarning(message); else logger.LogInfo(message); }
                else Console.Error.WriteLine("[TGGraphicsBridge.Autoload] " + message);
            }
            catch { /* Logging must not abort other preloader plugins. */ }
            try
            {
                if (logPath != null)
                    File.AppendAllText(logPath, DateTime.UtcNow.ToString("o") + " " + message + Environment.NewLine);
            }
            catch { /* BepInEx logging above remains available in read-only folders. */ }
        }
    }
}
