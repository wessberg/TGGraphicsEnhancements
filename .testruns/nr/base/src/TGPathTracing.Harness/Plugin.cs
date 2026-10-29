// SPDX-License-Identifier: MIT
// TG Path Tracing's in-game test runner: a developer's tool, in no release zip. It does nothing at all unless
// BepInEx/TGPathTracing/Harness/request.json exists when the game starts (tools/harness/Run-PtHarness.ps1 writes it):
// then it takes the request (renaming it, so the next start plays as normal), loads the saves it names, and writes the
// frames and reports the request asks for. docs/PT-HARNESS.md has the whole of it.
using System;
using System.IO;
using BepInEx;
using Newtonsoft.Json;
using UnityEngine;
namespace TGPathTracing.Harness
{
    [BepInPlugin(Id, Name, Version)]
    [BepInDependency(Traced.PluginId, BepInDependency.DependencyFlags.SoftDependency)]
    public sealed class HarnessPlugin : BaseUnityPlugin
    {
        public const string Id = "com.wessberg.tgpathtracing.harness";
        public const string Name = "TG Path Tracing Harness";
        public const string Version = "1.2.1";
        private void Awake()
        {
            try
            {
                string folder = Path.Combine(Paths.BepInExRootPath, "TGPathTracing", "Harness");
                string path = Path.Combine(folder, "request.json");
                if (!File.Exists(path)) { Logger.LogInfo(Name + ": no request, so it does nothing this session."); return; }
                string text = File.ReadAllText(path);
                string taken = Path.Combine(folder, "request.taken.json");
                if (File.Exists(taken)) File.Delete(taken);
                File.Move(path, taken);
                var request = JsonConvert.DeserializeObject<Request>(text);
                if (request == null || string.IsNullOrEmpty(request.outDir)) { Logger.LogError(Name + ": the request names no output folder; nothing is run."); return; }
                Logger.LogInfo(Name + " " + Version + ": run " + request.run + " into " + request.outDir + ".");
                Runner.Start(gameObject.AddComponent<HarnessBehaviour>(), request, Logger);
            }
            catch (Exception ex) { Logger.LogError(Name + " couldn't start a run: " + ex); }
        }
    }
    public sealed class HarnessBehaviour : MonoBehaviour
    {
        private void Update() { Runner.Tick(); }
        private void LateUpdate() { Runner.Tick(); }
    }
}
