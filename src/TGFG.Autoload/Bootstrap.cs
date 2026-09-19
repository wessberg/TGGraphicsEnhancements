// SPDX-License-Identifier: MIT
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using BepInEx;
using BepInEx.Logging;

namespace TGFG.Autoload
{
    internal static class Bootstrap
    {
        private const uint ExpectedAbi = 0x00050000;
        private const uint LoadLibrarySearchDllLoadDir = 0x00000100;
        private const uint LoadLibrarySearchDefaultDirs = 0x00001000;
        private static int attempted;
        // Keep the reference for process lifetime. Hooks may still reference the DLL
        // even if bootstrap fails partway through. Never FreeLibrary after bootstrap.
        private static IntPtr nativeModule;
        private static ManualLogSource logger;
        private static string logPath;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate uint GetAbi();
        // Matches DWORD WINAPI TGFG_Bootstrap(void*) in the v0.3 source.
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate uint NativeBootstrap(IntPtr unused);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        private static extern uint GetModuleFileNameW(IntPtr module, StringBuilder filename, uint size);
        [DllImport("user32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool EnumWindows(EnumWindow callback, IntPtr argument);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private delegate bool EnumWindow(IntPtr window, IntPtr argument);
        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern int GetClassNameW(IntPtr window, StringBuilder name, int maxCount);

        internal static void Run(string runtime)
        {
            if (Interlocked.Exchange(ref attempted, 1) != 0) return;
            try
            {
                logger = Logger.CreateLogSource("TGFG.Autoload");
                if (Environment.OSVersion.Platform != PlatformID.Win32NT || IntPtr.Size != 8)
                    throw new PlatformNotSupportedException("TGFG's current native backend requires Windows x64.");

                string root = Path.GetFullPath(Path.Combine(Paths.BepInExRootPath, "TGFrameGeneration"));
                Directory.CreateDirectory(root);
                logPath = Path.Combine(root, "Autoload.log");
                int pid;
                using (var process = Process.GetCurrentProcess()) pid = process.Id;
                Write("=== TGFG.Autoload 0.5.0; PID=" + pid + "; " + runtime + " ===");
                Write("Invoked by BepInEx preloader Initialize; no launcher, Unity call, or background worker.");
                Write("Patcher=" + typeof(Bootstrap).Assembly.Location);

                if (File.Exists(Path.Combine(root, "disable-autoload.txt")))
                {
                    Write("DISABLED by BepInEx/TGFrameGeneration/disable-autoload.txt. Native backend not loaded.");
                    return;
                }

                // Restrict this bootstrap to the intended game, not every Unity
                // process sharing a BepInEx install or started by a mod manager.
                string exe = ModulePath(IntPtr.Zero);
                if (!string.Equals(Path.GetFileName(exe), "Fall of Avalon.exe", StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Unexpected executable: " + exe);
                string nativePath = Path.Combine(root, "Native", "TGFG.Native.dll");
                if (GetModuleHandleW("TGFG.Native.dll") != IntPtr.Zero)
                    throw new InvalidOperationException("TGFG.Native.dll was already loaded. Do not combine autoload with TGFG.Launcher or a second bootstrap.");
                if (GetModuleHandleW("sl.interposer.dll") != IntPtr.Zero)
                    throw new InvalidOperationException("An independent Streamline interposer is already loaded. Refusing a second owner.");

                // TGFG.Streamline.dll is NVIDIA's unmodified sl.interposer.dll under a private
                // name, so Streamline-aware injectors do not hook TGFG's own instance.
                string[] required = {
                    "TGFG.Streamline.dll", "sl.common.dll", "sl.dlss_g.dll",
                    "sl.reflex.dll", "sl.pcl.dll", "nvngx_dlssg.dll"
                };
                RequireAmd64Pe(nativePath);
                foreach (string name in required)
                    RequireAmd64Pe(Path.Combine(root, "Native", "Streamline", name));

                Write("Before bootstrap: d3d11.dll mapped=" + Mapped("d3d11.dll") +
                      ", d3d12.dll mapped=" + Mapped("d3d12.dll") +
                      ", dxgi.dll mapped=" + Mapped("dxgi.dll") +
                      ", Unity windows=" + UnityWindowCount((uint)pid));
                Write("Module/window presence does not establish device or swapchain creation order.");
                Write("Native=" + nativePath + "; SHA256=" + Sha256(nativePath));

                nativeModule = LoadLibraryExW(nativePath, IntPtr.Zero,
                    LoadLibrarySearchDllLoadDir | LoadLibrarySearchDefaultDirs);
                if (nativeModule == IntPtr.Zero)
                {
                    int error = Marshal.GetLastWin32Error();
                    throw new Win32Exception(error, "LoadLibraryExW failed for " + nativePath +
                        ". Error 126 can mean a missing dependency, not a missing TGFG.Native.dll.");
                }
                Write("Native loaded in PID=" + pid + " from " + ModulePath(nativeModule));
                uint abi = Bind<GetAbi>("TGFG_GetAbiVersion")();
                if (abi != ExpectedAbi)
                    throw new InvalidOperationException("Native ABI 0x" + abi.ToString("X8") +
                        " does not match expected 0x" + ExpectedAbi.ToString("X8") + ".");

                // Synchronous: Unity is not allowed to race ahead of hook installation.
                // Do NOT move this to ThreadPool/Task.Run or a plugin Awake() callback.
                uint code = Bind<NativeBootstrap>("TGFG_Bootstrap")(IntPtr.Zero);
                Write("TGFG_Bootstrap returned " + code + " in PID=" + pid + ".");
                if (code != 0)
                    throw new InvalidOperationException("Native bootstrap failed (code " + code +
                        "). See BepInEx/TGFrameGeneration/Native/TGFG.Native.log. Partial hooks may remain; restart after correcting the cause.");
                Write("AUTOLOAD COMPLETED. This proves the bootstrap returned success, NOT that a game swapchain was intercepted or FG works.");
                Write("Next checkpoint: in-game native PID matches " + pid +
                      ", sourceApi=11, presentationApi=12, bridge and copy counters advance with Mode Off.");
            }
            catch (Exception ex)
            {
                // An ordinary loader/file error should not prevent BepInEx's other
                // plugins from loading. Native access violations cannot be recovered here.
                Write("AUTOLOAD FAILED: " + ex, true);
            }
        }

        private static T Bind<T>(string symbol) where T : Delegate
        {
            IntPtr address = GetProcAddress(nativeModule, symbol);
            if (address == IntPtr.Zero) throw new EntryPointNotFoundException(symbol);
            return Marshal.GetDelegateForFunctionPointer<T>(address);
        }
        private static bool Mapped(string module) { return GetModuleHandleW(module) != IntPtr.Zero; }
        private static string ModulePath(IntPtr module)
        {
            var buffer = new StringBuilder(32768);
            uint count = GetModuleFileNameW(module, buffer, (uint)buffer.Capacity);
            if (count == 0 || count >= buffer.Capacity)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot resolve module path.");
            return buffer.ToString();
        }
        private static int UnityWindowCount(uint pid)
        {
            int count = 0;
            EnumWindow callback = (window, unused) => {
                uint owner;
                GetWindowThreadProcessId(window, out owner);
                if (owner == pid)
                {
                    var name = new StringBuilder(256);
                    GetClassNameW(window, name, name.Capacity);
                    if (name.ToString().StartsWith("Unity", StringComparison.Ordinal)) count++;
                }
                return true;
            };
            bool success = EnumWindows(callback, IntPtr.Zero);
            GC.KeepAlive(callback);
            return success ? count : -1;
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
                    throw new BadImageFormatException("Expected AMD64 native runtime: " + path);
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
                if (logger != null)
                {
                    if (error) logger.LogError(message); else logger.LogInfo(message);
                }
                else Console.Error.WriteLine("[TGFG.Autoload] " + message);
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
