// SPDX-License-Identifier: MIT
using System;
using BepInEx;
using UnityEngine;
#if BEPINEX_IL2CPP
using BepInEx.Unity.IL2CPP;
#endif
namespace TGFrameGeneration
{
    [BepInPlugin(Id, Name, Version)]
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
        public const string Version = "0.5.0";
#if BEPINEX_IL2CPP
        public override void Load()
        {
            ModManagers.KeepOutOfAvalon(Id, Log);
            Session.Start(Config, Log);
            AddComponent<RuntimeBehaviour>();
        }
        public override bool Unload() { Session.Stop(); return false; } // Native backend is process-pinned.
#else
        private void Awake()
        {
            ModManagers.KeepOutOfAvalon(Id, Logger);
            Session.Start(Config, Logger);
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
        private void Update() { Session.Tick(); FrameBridge.BeginFrame(); }
        private void LateUpdate() { FrameBridge.EndSimulation(); }
        private void OnApplicationQuit() { Session.Stop(); }
    }
}
