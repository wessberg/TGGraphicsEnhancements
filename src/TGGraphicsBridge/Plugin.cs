// SPDX-License-Identifier: MIT
using System;
using BepInEx;
using UnityEngine;
#if BEPINEX_IL2CPP
using BepInEx.Unity.IL2CPP;
#endif
namespace TGGraphicsBridge
{
    // TG Graphics Bridge's gameplay plugin. The native bridge itself starts from the BepInEx preloader
    // (<mod>.Autoload); this plugin reports what it did and carries what TG Frame Generation and TG
    // Upscaling share in the game: the Video-tab plumbing, the mod-manager integration and the
    // motion-vector fixes both of them need. Both mods bundle it under the same GUID, and BepInEx loads
    // the copy with the highest version (logging that it skips the other).
    [BepInPlugin(Id, Name, Version)]
    [BepInDependency("kane.tgfoa.mod-manager", BepInDependency.DependencyFlags.SoftDependency)]
    [BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)]
#if BEPINEX_IL2CPP
    public sealed class BridgePlugin : BasePlugin
#else
    public sealed class BridgePlugin : BaseUnityPlugin
#endif
    {
        public const string Id = "com.wessberg.tggraphicsbridge";
        public const string Name = "TG Graphics Bridge";
        public const string Version = "1.2.3";
#if BEPINEX_IL2CPP
        public override void Load()
        {
            BridgeLog.Log = Log;
            Config.SaveOnConfigSet = false; // bound first, then saved once (SafeConfig)
            Step("its performance overlay setting", () => PerformanceOverlay.Bind(Config));
            Step("its troubleshooting report setting", () => Troubleshooting.Bind(Config));
            Step("saving its settings", () => { SafeConfig.Take(Config, Name); SafeConfig.Save(Config, Name); });
            Start();
            Step("adding its per-frame component", () => AddComponent<BridgeBehaviour>());
        }
        public override bool Unload() { Stop(); return false; } // the native bridge is process-pinned
#else
        private void Awake()
        {
            BridgeLog.Log = Logger;
            Config.SaveOnConfigSet = false; // bound first, then saved once (SafeConfig)
            Step("its performance overlay setting", () => PerformanceOverlay.Bind(Config));
            Step("its troubleshooting report setting", () => Troubleshooting.Bind(Config));
            Step("saving its settings", () => { SafeConfig.Take(Config, Name); SafeConfig.Save(Config, Name); });
            Start();
            Step("adding its per-frame component", () => gameObject.AddComponent<BridgeBehaviour>());
        }
        private void OnDestroy() { Stop(); }
#endif
        private static void Start()
        {
            Step("keeping out of Avalon Mod Manager", () => ModManagers.KeepOutOfAvalon(Id, BridgeLog.Log));
            Step("reading the native bridge", GraphicsBridge.Discover);
            Step("the Kandra motion-vector history", KandraMotionHistory.Initialize);
            // A second copy is expected when both mods are installed: BepInEx logs that it skipped it.
            Step("reporting", () =>
            {
                string folder = System.IO.Path.GetFileName(System.IO.Path.GetDirectoryName(typeof(BridgePlugin).Assembly.Location) ?? "");
                BridgeLog.Log.LogInfo("TG Graphics Bridge " + Version + " loaded (the copy bundled in plugins/" + folder + "). Graphics API: " +
                    SystemInfo.graphicsDeviceType + ". " + GraphicsBridge.Describe());
            });
        }
        // Each step on its own: under IL2CPP an exception out of Load() makes BepInEx skip both mods,
        // which depend on this plugin, and under Mono it would skip the per-frame component.
        private static void Step(string what, Action step)
        {
            try { step(); }
            catch (Exception ex) { BridgeLog.Log.LogError("TG Graphics Bridge: " + what + " failed; the rest starts regardless. " + ex); }
        }
        internal static void Stop()
        {
            FirstPersonMotion.Restore();
            KandraMotionHistory.Dispose();
#if BEPINEX_IL2CPP
            Il2CppOptionConditions.Dispose();
#endif
        }
    }
    // Interop attributes are not System.Attribute, and Unity ignores execution order on injected types.
#if !BEPINEX_IL2CPP
    [DefaultExecutionOrder(-32000)]
#endif
    public sealed class BridgeBehaviour : MonoBehaviour
    {
#if BEPINEX_IL2CPP
        public BridgeBehaviour(IntPtr pointer) : base(pointer) { }
#endif
        // Each piece of per-frame work stops on its own failure; none takes down what the two mods share
        // (the Video-tab hooks, the Kandra history guard), which stay until the game quits.
        private bool motionFailed, statusFailed, overlayFailed, checksFailed, reportFailed, installChecked;
        private void Update()
        {
            float now = Time.unscaledTime;
            MainThread.Run(); // catches its own failures
            if (!checksFailed)
            {
                try
                {
                    // Once every plugin has loaded: where the mods are installed and what else runs in the game.
                    if (!installChecked) { installChecked = true; Compatibility.Start(); }
                    Compatibility.Tick(now);
                }
                catch (Exception ex) { checksFailed = true; BridgeLog.Log.LogError("TG Graphics Bridge stopped its installation and compatibility checks: " + ex); }
            }
            RowAlerts.Tick(now); // catches its own failures
            if (!reportFailed)
            {
                try { Troubleshooting.Tick(now); }
                catch (Exception ex)
                {
                    reportFailed = true;
                    BridgeLog.Log.LogError("TG Graphics Bridge stopped its troubleshooting report: " + ex);
                    try { Troubleshooting.Abandon(); } catch { } // the player's settings back first
                }
            }
            if (!motionFailed)
            {
                try { FirstPersonMotion.Tick(now); }
                catch (Exception ex)
                {
                    motionFailed = true;
                    BridgeLog.Log.LogError("TG Graphics Bridge stopped its first-person motion fix: " + ex);
                    try { FirstPersonMotion.Restore(); } catch { }
                }
            }
            ModManagers.Tick(); // catches its own failures
            PerformanceOverlayRow.Tick(now); // catches its own failures
            if (!overlayFailed)
            {
                try { PerformanceOverlay.Tick(now); }
                catch (Exception ex) { overlayFailed = true; BridgeLog.Log.LogError("TG Graphics Bridge stopped its performance overlay: " + ex); }
            }
            if (!statusFailed)
            {
                try { GraphicsBridge.Tick(now); }
                catch (Exception ex)
                {
                    statusFailed = true;
                    BridgeLog.Log.LogError("TG Graphics Bridge stopped its status reports: " + ex);
                }
            }
        }
        private void OnGUI()
        {
            if (overlayFailed) return;
            try { PerformanceOverlay.Draw(); Notices.Draw(); }
            catch (Exception ex) { overlayFailed = true; BridgeLog.Log.LogError("TG Graphics Bridge stopped its performance overlay: " + ex); }
        }
        private void OnApplicationQuit() { BridgePlugin.Stop(); }
    }
    internal static class BridgeLog
    {
        internal static BepInEx.Logging.ManualLogSource Log;
    }
}
