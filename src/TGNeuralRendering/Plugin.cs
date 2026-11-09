// SPDX-License-Identifier: MIT
using System;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
#if BEPINEX_IL2CPP
using BepInEx.Unity.IL2CPP;
#endif
namespace TGNeuralRendering
{
    [BepInPlugin(Id, Name, Version)]
    [BepInDependency("com.wessberg.tggraphicsbridge", "1.2.4")]
    [BepInDependency("kane.tgfoa.mod-manager", BepInDependency.DependencyFlags.SoftDependency)]
    [BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)]
#if BEPINEX_IL2CPP
    public sealed class Plugin : BasePlugin
#else
    public sealed class Plugin : BaseUnityPlugin
#endif
    {
        public const string Id = "com.wessberg.tgneuralrendering", Name = "TG Neural Rendering", Version = "1.2.4";
#if BEPINEX_IL2CPP
        public override void Load()
        {
            ModManagers.KeepOutOfAvalon(Id, Log);
            try { Session.Start(Config, Log); AddComponent<NeuralBehaviour>(); }
            catch (Exception ex) { Log.LogError(Name + " could not start: " + ex); }
        }
        public override bool Unload() { Session.Stop(); return false; }
#else
        private void Awake()
        {
            ModManagers.KeepOutOfAvalon(Id, Logger);
            try { Session.Start(Config, Logger); gameObject.AddComponent<NeuralBehaviour>(); }
            catch (Exception ex) { Logger.LogError(Name + " could not start: " + ex); }
        }
        private void OnDestroy() => Session.Stop();
#endif
    }
    public sealed class NeuralBehaviour : MonoBehaviour
    {
#if BEPINEX_IL2CPP
        public NeuralBehaviour(IntPtr pointer) : base(pointer) { }
#endif
        private void Update() => Session.Tick();
        private void OnApplicationQuit() => Session.Stop();
    }
}
