// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using BepInEx;
namespace TGGraphicsBridge
{
    // TGGraphicsBridge.Native.dll's status. The preloader loaded it and registered the extensions; this
    // only reads what happened, so the mods built on the bridge can say why something is missing.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    public struct BridgeStatus
    {
        public uint Size, Abi;
        public uint Bootstrapped; // 0 not yet, 1 hooks installed, 2 failed
        public uint ExtensionsRegistered, ExtensionsActive, ExtensionsFailed;
        public uint Needs;
        public int PresentationProvider, DeviceCreator; // extension indices, -1 none / the bridge
        public uint GameDeviceObserved, GameDeviceGeneration;
        public uint D3D12DeviceReady, GpuVendor;
        public uint SwapchainsSeen, SwapchainsProvided;
        public int LastError;
        public uint NativeLoadPid;
    }
    // CPU, GPU and video memory use (TGB_Performance), sampled by the native bridge for the performance overlay.
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    public struct BridgePerformance
    {
        public uint Size, Abi;
        public uint Valid;                       // bit 0 CPU, bit 1 GPU, bit 2 video memory
        public float CpuProcess, CpuSystem;      // percent of all logical processors
        public float GpuProcess, GpuSystem;      // percent busy of the busiest GPU engine type
        public uint Reserved;
        public ulong VramProcess, VramBudget, VramTotal; // bytes
    }
    public static class GraphicsBridge
    {
        public const uint Abi = 0x00010100;
        internal const int StatusSize = 68, PerformanceSize = 56;
        public const string NativeName = "TGGraphicsBridge.Native.dll";
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint GetAbi();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int Read(ref BridgeStatus status);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetName(uint index, byte[] buffer, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetState(uint index);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReadPerf(ref BridgePerformance performance);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int ReadText(byte[] buffer, uint size);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void Note([MarshalAs(UnmanagedType.LPStr)] string text);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        private static Read read;
        private static GetName name;
        private static GetState state;
        private static ReadPerf readPerformance;
        private static ReadText readDiagnostics;
        private static Note logNote;
        private static float nextPoll;
        private static string lastReport;
        public static string Root => Path.Combine(Paths.BepInExRootPath, "TGGraphicsBridge");
        public static bool Loaded => read != null;
        public static string Detail { get; private set; } = "TGGraphicsBridge.Native.dll is not loaded: see BepInEx/TGGraphicsBridge/Autoload.log.";
        internal static void Discover()
        {
            if (Loaded) return;
            try
            {
                var module = GetModuleHandleW(NativeName);
                if (module == IntPtr.Zero)
                {
                    Detail = NativeName + " is not loaded. Check that BepInEx/patchers/TGFrameGeneration.Autoload.dll or TGBetterUpscaling.Autoload.dll " +
                        "exists and read BepInEx/TGGraphicsBridge/Autoload.log; start the game normally through Steam.";
                    return;
                }
                uint abi = Bind<GetAbi>(module, "TGGraphicsBridge_GetAbiVersion")();
                if (abi != Abi) { Detail = "Native ABI 0x" + abi.ToString("X8") + " does not match this plugin's 0x" + Abi.ToString("X8") + ": reinstall TG Frame Generation and TG Better Upscaling from the same release."; return; }
                name = Bind<GetName>(module, "TGGraphicsBridge_GetExtensionName");
                state = Bind<GetState>(module, "TGGraphicsBridge_GetExtensionState");
                read = Bind<Read>(module, "TGGraphicsBridge_ReadStatus");
                // Only the performance overlay reads it; a bridge without it still serves everything else.
                IntPtr performance = GetProcAddress(module, "TGGraphicsBridge_ReadPerformance");
                if (performance != IntPtr.Zero && Marshal.SizeOf(typeof(BridgePerformance)) == PerformanceSize)
                    readPerformance = Marshal.GetDelegateForFunctionPointer<ReadPerf>(performance);
                // The explanations and the troubleshooting report read these; an older bridge has neither.
                IntPtr diagnostics = GetProcAddress(module, "TGGraphicsBridge_ReadDiagnostics");
                if (diagnostics != IntPtr.Zero) readDiagnostics = Marshal.GetDelegateForFunctionPointer<ReadText>(diagnostics);
                IntPtr note = GetProcAddress(module, "TGGraphicsBridge_LogNote");
                if (note != IntPtr.Zero) logNote = Marshal.GetDelegateForFunctionPointer<Note>(note);
                Detail = "Bound " + NativeName + ".";
            }
            catch (Exception ex) { read = null; Detail = "Native binding failed: " + ex.Message; }
        }
        private static T Bind<T>(IntPtr module, string symbol) where T : Delegate
        {
            IntPtr address = GetProcAddress(module, symbol);
            if (address == IntPtr.Zero) throw new EntryPointNotFoundException(symbol);
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        public static bool TryReadStatus(out BridgeStatus status)
        {
            status = new BridgeStatus { Size = StatusSize, Abi = Abi };
            return read != null && read(ref status) == 0;
        }
        // The first call starts the native sampling, twice a second from then on.
        public static bool TryReadPerformance(out BridgePerformance performance)
        {
            performance = new BridgePerformance { Size = PerformanceSize, Abi = Abi };
            return readPerformance != null && readPerformance(ref performance) == 0;
        }
        // What the native bridge learned about this session (TGGraphicsBridge_ReadDiagnostics): the Windows
        // build, the Direct3D 12 runtime and device, the game's adapter and display, NVIDIA driver
        // settings. Empty before the bridge started or with a bridge that has no such export.
        public static Dictionary<string, string> Facts() => ParseFacts(readDiagnostics == null ? "" : ReadNative(ReadDiagnostics));
        private static int ReadDiagnostics(byte[] buffer, uint size) => readDiagnostics(buffer, size);
        public static string Fact(string key) => Facts().TryGetValue(key, out var value) ? value : "";
        // One line in TGGraphicsBridge.Native.log.
        public static void LogNote(string text)
        {
            try { logNote?.Invoke(Ascii(text)); } catch { }
        }
        internal static string ReadNative(Func<byte[], uint, int> read)
        {
            if (read == null) return "";
            try
            {
                var buffer = new byte[16384];
                int length = read(buffer, (uint)buffer.Length);
                if (length >= buffer.Length) { buffer = new byte[length + 1]; length = read(buffer, (uint)buffer.Length); }
                return length > 0 ? Encoding.UTF8.GetString(buffer, 0, Math.Min(length, buffer.Length - 1)) : "";
            }
            catch { return ""; }
        }
        public static Dictionary<string, string> ParseFacts(string text)
        {
            var facts = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (var line in (text ?? "").Split('\n'))
            {
                int equals = line.IndexOf('=');
                if (equals > 0) facts[line.Substring(0, equals).Trim()] = line.Substring(equals + 1).TrimEnd('\r');
            }
            return facts;
        }
        // The native logs are written as bytes; anything outside ASCII becomes '?'.
        public static string AsciiText(string text) => Ascii(text);
        internal static string Ascii(string text)
        {
            var chars = (text ?? "").ToCharArray();
            for (int i = 0; i < chars.Length; i++) if (chars[i] > 126 || (chars[i] < 32 && chars[i] != '\t')) chars[i] = '?';
            return new string(chars);
        }
        // Registered extensions in load order, with whether each one started.
        public static List<KeyValuePair<string, bool>> Extensions()
        {
            var list = new List<KeyValuePair<string, bool>>();
            if (!TryReadStatus(out var status) || name == null || state == null) return list;
            var buffer = new byte[256];
            for (uint i = 0; i < status.ExtensionsRegistered; i++)
            {
                int length = name(i, buffer, (uint)buffer.Length);
                list.Add(new KeyValuePair<string, bool>(length > 0 ? Encoding.UTF8.GetString(buffer, 0, length) : "#" + i, state(i) == 1));
            }
            return list;
        }
        public static bool IsExtensionActive(string extension)
        {
            foreach (var e in Extensions()) if (e.Key == extension) return e.Value;
            return false;
        }
        public static string Describe()
        {
            if (!TryReadStatus(out var s)) return Detail;
            var extensions = Extensions();
            var names = new List<string>();
            foreach (var e in extensions) names.Add(e.Key + (e.Value ? "" : " (inactive)"));
            string Of(int index) => index >= 0 && index < extensions.Count ? extensions[index].Key : "none";
            return "Bridge " + (s.Bootstrapped == 1 ? "started" : s.Bootstrapped == 2 ? "FAILED to start" : "not started") +
                " | extensions: " + (names.Count > 0 ? string.Join(", ", names.ToArray()) : "none") +
                " | game D3D11 device " + (s.GameDeviceObserved != 0 ? "observed (generation " + s.GameDeviceGeneration + ")" : "not seen yet") +
                " | D3D12 device " + (s.D3D12DeviceReady != 0 ? "ready, created by " + (s.DeviceCreator >= 0 ? Of(s.DeviceCreator) : "the bridge") : "not created") +
                " | GPU vendor 0x" + s.GpuVendor.ToString("X4") +
                " | swapchain provider " + Of(s.PresentationProvider) + ", game swapchains " + s.SwapchainsSeen + ", replaced " + s.SwapchainsProvided +
                (s.LastError != 0 ? " | last error 0x" + s.LastError.ToString("X8") : "") + " | native PID " + s.NativeLoadPid;
        }
        internal static void Tick(float now)
        {
            if (now < nextPoll) return;
            nextPoll = now + 5f;
            if (!Loaded) Discover();
            ReportVideoMemory();
            string report = Describe();
            if (report == lastReport) return;
            lastReport = report;
            BridgeLog.Log.LogInfo(report);
        }
        // The game's video memory in the log whenever it has moved by a quarter gigabyte, so a leak shows as a
        // climbing series of lines next to what the mods logged, and a warning once it nears Windows' budget.
        private const ulong VideoMemoryStep = 256UL << 20;
        private static ulong loggedVideoMemory;
        private static bool videoMemoryWarned;
        private static void ReportVideoMemory()
        {
            if (!TryReadPerformance(out var p) || (p.Valid & 4) == 0 || p.VramBudget == 0) return;
            ulong used = p.VramProcess;
            bool moved = used > loggedVideoMemory ? used - loggedVideoMemory >= VideoMemoryStep : loggedVideoMemory - used >= VideoMemoryStep;
            if (loggedVideoMemory == 0 || moved)
            {
                loggedVideoMemory = used;
                BridgeLog.Log.LogInfo("Video memory: the game uses " + (used / 1073741824.0).ToString("0.00") + " GB of its " +
                    (p.VramBudget / 1073741824.0).ToString("0.00") + " GB budget, frame " + UnityEngine.Time.frameCount + ".");
            }
            if (!videoMemoryWarned && used > p.VramBudget / 10 * 9)
            {
                videoMemoryWarned = true;
                BridgeLog.Log.LogWarning("Video memory is nearly exhausted: the game uses over 90% of its budget. The lines above show when it grew.");
            }
        }
        // The bridge's own startup, for the diagnostic reports of the mods built on it.
        public static void AppendDiagnostics(StringBuilder sb)
        {
            string root = Root;
            sb.AppendLine("\nTG Graphics Bridge " + BridgePlugin.Version + ": " + Describe());
            sb.AppendLine("Automatic startup: the BepInEx preloader each mod bundles (BepInEx/patchers/<mod>.Autoload.dll); one starts the bridge.");
            sb.AppendLine("Loaded bridge plugin: " + typeof(BridgePlugin).Assembly.Location);
            sb.AppendLine("Autoload log: " + Path.Combine(root, "Autoload.log"));
            sb.AppendLine("Native log: " + Path.Combine(root, "TGGraphicsBridge.Native.log"));
            sb.AppendLine("Autoload disabled by marker: " + File.Exists(Path.Combine(root, "disable-autoload.txt")));
            AppendTail(sb, Path.Combine(root, "Autoload.log"), "Autoload log", 40);
        }
        // The end of an append-only log, bounded so a long history cannot exhaust memory in a report.
        public static void AppendTail(StringBuilder sb, string path, string title, int lines)
        {
            try
            {
                if (!File.Exists(path)) { sb.AppendLine("No " + title.ToLowerInvariant() + " at " + path + "."); return; }
                using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    long offset = Math.Max(0, stream.Length - 32768);
                    stream.Seek(offset, SeekOrigin.Begin);
                    var bytes = new byte[(int)Math.Min(32768, stream.Length - offset)];
                    int used = 0;
                    while (used < bytes.Length)
                    {
                        int count = stream.Read(bytes, used, bytes.Length - used);
                        if (count == 0) break;
                        used += count;
                    }
                    string tail = Encoding.UTF8.GetString(bytes, 0, used);
                    if (offset != 0)
                    {
                        int newline = tail.IndexOf('\n');
                        tail = newline >= 0 ? tail.Substring(newline + 1) : "";
                    }
                    var all = tail.Split('\n');
                    sb.AppendLine(title + " tail (may include earlier sessions; compare PIDs and times):");
                    for (int i = Math.Max(0, all.Length - lines); i < all.Length; i++) sb.AppendLine(all[i].TrimEnd('\r'));
                }
            }
            catch (Exception ex) { sb.AppendLine(title + " read failed: " + ex.Message); }
        }
    }
}
