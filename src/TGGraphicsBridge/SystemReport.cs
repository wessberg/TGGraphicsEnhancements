// SPDX-License-Identifier: MIT
// The system part of the troubleshooting report: Windows, CPU, memory, the GPUs and display, the game
// and its runtime, BepInEx and the plugins it loaded.
using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Linq;
using System.Text;
using BepInEx;
using UnityEngine;
namespace TGGraphicsBridge
{
    internal static class SystemReport
    {
        internal static void Append(StringBuilder sb)
        {
            var facts = GraphicsBridge.Facts();
            string Fact(string key) => facts.ContainsKey(key) ? facts[key] : "";
            sb.AppendLine("== System ==");
            string windows = (Fact("windows.product") + " " + Fact("windows.version")).Trim();
            sb.AppendLine("Windows: " + (windows.Length > 0 ? windows + ", build " + Fact("windows.build") : SystemInfo.operatingSystem));
            string runtime = Fact("windows.vcruntime");
            if (runtime.Length > 0)
                sb.AppendLine("Visual C++ runtime: " + (runtime == "missing" ? "not installed. NVIDIA Streamline, AMD FSR and Intel XeSS can't load without it: install the latest Microsoft Visual C++ Redistributable (x64)." : runtime));
            var memory = new MemoryStatus { Length = (uint)Marshal.SizeOf(typeof(MemoryStatus)) };
            string free = GlobalMemoryStatusEx(ref memory) && memory.TotalPhysical > 0
                ? " (" + (memory.AvailablePhysical / 1073741824.0).ToString("0.0", CultureInfo.InvariantCulture) + " GB free, " + memory.Load + "% in use)" : "";
            sb.AppendLine("CPU: " + SystemInfo.processorType + " (" + SystemInfo.processorCount + " threads), RAM " + (SystemInfo.systemMemorySize / 1024f).ToString("0.#", CultureInfo.InvariantCulture) + " GB" + free);
            string adapter = Fact("adapter.game");
            sb.AppendLine("GPU: " + (adapter.Length > 0 ? adapter : SystemInfo.graphicsDeviceName) +
                (Fact("adapter.game.vramMB").Length > 0 ? ", " + Fact("adapter.game.vramMB") + " MB" : ", " + SystemInfo.graphicsMemorySize + " MB") +
                (Fact("adapter.game.driver").Length > 0 ? ", driver " + Fact("adapter.game.driver") : "") +
                (Fact("adapter.game.vendor").Length > 0 ? ", vendor " + Fact("adapter.game.vendor") + " device " + Fact("adapter.game.device") : ""));
            if (Fact("display.crossAdapter") == "1")
                sb.AppendLine("Display: connected to " + Fact("display.adapter") + ", not to the GPU the game renders on (hybrid graphics): every frame is copied between the GPUs.");
            else if (Fact("display.crossAdapter") == "unknown") sb.AppendLine("Display: the GPU it is connected to couldn't be identified.");
            else if (Fact("display.adapter").Length > 0) sb.AppendLine("Display: connected to the GPU the game renders on.");
            var mode = Screen.currentResolution;
            string hdr = "";
            try { hdr = HDROutputSettings.main.available ? (HDROutputSettings.main.active ? ", HDR on" : ", HDR available but off") : ", no HDR"; } catch { }
            sb.AppendLine("Screen: " + Screen.width + "x" + Screen.height + " " + Screen.fullScreenMode + ", display " + mode.width + "x" + mode.height + " at " +
                mode.refreshRateRatio.value.ToString("0.##", CultureInfo.InvariantCulture) + " Hz" + hdr);
            int cap = Application.targetFrameRate, vsync = QualitySettings.vSyncCount;
            sb.AppendLine("Game frame pacing: VSync " + (vsync <= 0 ? "off" : vsync == 1 ? "on" : "on, every " + vsync + " refreshes") + ", Framerate Limit " + (cap > 0 ? cap + " fps" : "none"));
            sb.AppendLine("Game: " + Application.productName + " " + Application.version + ", Unity " + Application.unityVersion + ", " + SystemInfo.graphicsDeviceType + " (" + SystemInfo.graphicsDeviceVersion + ")");
#if BEPINEX_IL2CPP
            sb.AppendLine("Runtime: IL2CPP (the game's default branch), BepInEx " + typeof(Paths).Assembly.GetName().Version);
#else
            sb.AppendLine("Runtime: Mono (the game's mono branch), BepInEx " + typeof(Paths).Assembly.GetName().Version);
#endif
            if (Fact("agility").Length > 0) sb.AppendLine(Fact("agility"));
            sb.AppendLine("Plugins loaded by BepInEx:");
            try
            {
#if BEPINEX_IL2CPP
                var plugins = BepInEx.Unity.IL2CPP.IL2CPPChainloader.Instance.Plugins.Values;
#else
                var plugins = BepInEx.Bootstrap.Chainloader.PluginInfos.Values;
#endif
                foreach (var plugin in plugins.OrderBy(p => p.Metadata.Name))
                    sb.AppendLine("  " + plugin.Metadata.Name + " " + plugin.Metadata.Version + " (" + plugin.Metadata.GUID + ")");
            }
            catch (Exception ex) { sb.AppendLine("  unavailable: " + ex.Message); }
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct MemoryStatus
        {
            public uint Length, Load;
            public ulong TotalPhysical, AvailablePhysical, TotalPageFile, AvailablePageFile, TotalVirtual, AvailableVirtual, AvailableExtendedVirtual;
        }
        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GlobalMemoryStatusEx(ref MemoryStatus buffer);
    }
}
