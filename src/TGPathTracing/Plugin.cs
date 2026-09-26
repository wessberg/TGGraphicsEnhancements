// SPDX-License-Identifier: MIT
using System;
using BepInEx;
using TGGraphicsBridge;
using UnityEngine;
namespace TGPathTracing
{
    [BepInPlugin(Id, Name, Version)]
    [BepInDependency(BridgePlugin.Id, BepInDependency.DependencyFlags.HardDependency)]
    [BepInDependency("kane.tgfoa.mod-manager", BepInDependency.DependencyFlags.SoftDependency)]
    [BepInDependency("com.avalonmodmanager", BepInDependency.DependencyFlags.SoftDependency)]
    public sealed class PathTracingPlugin : BaseUnityPlugin
    {
        public const string Id = "com.wessberg.tgpathtracing";
        public const string Name = "TG Path Tracing";
        public const string Version = "1.2.0";
        private void Awake()
        {
            ModManagers.KeepOutOfAvalon(Id, Logger);
            // An error here would otherwise end the plugin without a trace: this game writes no Unity log.
            try { Session.Start(Config, Logger); }
            catch (Exception ex) { Logger.LogError(Name + " couldn't start; it stays off this session: " + ex); }
            gameObject.AddComponent<PathTracingBehaviour>();
        }
        private void OnDestroy() { Session.Stop(); }
    }
    public sealed class PathTracingBehaviour : MonoBehaviour
    {
        private void Update() { Session.Tick(); }
        private void OnApplicationQuit() { Session.Stop(); }
    }
}
