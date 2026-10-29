// SPDX-License-Identifier: MIT
using System;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
#if BEPINEX_IL2CPP
using BepInEx.Unity.IL2CPP;
#endif
namespace TGBetterUpscaling
{
    [BepInPlugin(Id, Name, Version)]
    [BepInDependency(BridgePlugin.Id, BepInDependency.DependencyFlags.HardDependency)]
    [BepInDependency("kane.tgfoa.mod-manager", BepInDependency.DependencyFlags.SoftDependency)]
    [BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)]
#if BEPINEX_IL2CPP
    public sealed class UpscalingPlugin : BasePlugin
#else
    public sealed class UpscalingPlugin : BaseUnityPlugin
#endif
    {
        public const string Id = "com.wessberg.tgbetterupscaling";
        public const string Name = "TG Better Upscaling";
        public const string Version = "1.2.3";
#if BEPINEX_IL2CPP
        public override void Load()
        {
            ModManagers.KeepOutOfAvalon(Id, Log);
            try { Session.Start(Config, Log); }
            catch (Exception ex) { Log.LogError(Name + " couldn't start; it stays off this session: " + ex); }
            AddComponent<UpscalingBehaviour>();
        }
        public override bool Unload() { Session.Stop(); return false; } // the native side is process-pinned
#else
        private void Awake()
        {
            ModManagers.KeepOutOfAvalon(Id, Logger);
            // An error here would otherwise end the plugin without a trace: this game writes no Unity log.
            try { Session.Start(Config, Logger); }
            catch (Exception ex) { Logger.LogError(Name + " couldn't start; it stays off this session: " + ex); }
            gameObject.AddComponent<UpscalingBehaviour>();
        }
        private void OnDestroy() { Session.Stop(); }
#endif
    }
    public sealed class UpscalingBehaviour : MonoBehaviour
    {
#if BEPINEX_IL2CPP
        public UpscalingBehaviour(IntPtr pointer) : base(pointer) { }
#endif
        private void Update() { Session.Tick(); }
        private void OnApplicationQuit() { Session.Stop(); }
    }
}
