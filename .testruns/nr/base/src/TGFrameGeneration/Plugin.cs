// SPDX-License-Identifier: MIT
using System;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
#if BEPINEX_IL2CPP
using BepInEx.Unity.IL2CPP;
#endif
namespace TGFrameGeneration
{
    [BepInPlugin(Id, Name, Version)]
    [BepInDependency(BridgePlugin.Id, BepInDependency.DependencyFlags.HardDependency)]
    [BepInDependency("kane.tgfoa.mod-manager", BepInDependency.DependencyFlags.SoftDependency)]
    [BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)]
#if BEPINEX_IL2CPP
    public sealed class Plugin : BasePlugin
#else
    public sealed class Plugin : BaseUnityPlugin
#endif
    {
        public const string Id = "com.wessberg.tgframegeneration";
        public const string Name = "TG Frame Generation";
        public const string Version = "1.2.1";
#if BEPINEX_IL2CPP
        public override void Load()
        {
            ModManagers.KeepOutOfAvalon(Id, Log);
            try { Session.Start(Config, Log); }
            catch (Exception ex) { Log.LogError(Name + " couldn't start; it stays off this session: " + ex); }
            AddComponent<RuntimeBehaviour>();
        }
        public override bool Unload() { Session.Stop(); return false; } // Native backend is process-pinned.
#else
        private void Awake()
        {
            ModManagers.KeepOutOfAvalon(Id, Logger);
            // An error here would otherwise end the plugin without a trace: this game writes no Unity log.
            try { Session.Start(Config, Logger); }
            catch (Exception ex) { Logger.LogError(Name + " couldn't start; it stays off this session: " + ex); }
            gameObject.AddComponent<RuntimeBehaviour>();
        }
        private void OnDestroy() { Session.Stop(); }
#endif
    }
    // Interop attributes are not System.Attribute, and Unity ignores execution order on injected types.
#if !BEPINEX_IL2CPP
    [DefaultExecutionOrder(-32000)]
#endif
    public sealed class RuntimeBehaviour : MonoBehaviour
    {
#if BEPINEX_IL2CPP
        public RuntimeBehaviour(IntPtr pointer) : base(pointer) { }
#endif
        // The frame normally began at the start of the PlayerLoop (FrameStart); here only when it didn't.
        private void Update() { Session.Tick(); FrameStart.Update(); }
        private void LateUpdate() { FrameBridge.EndSimulation(); }
        private void OnApplicationQuit() { Session.Stop(); }
    }
}
