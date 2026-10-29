// SPDX-License-Identifier: MIT
// One HDRP pass owns the order: scene processing first, then FG's HUD-less capture.
using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Rendering.HighDefinition;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGGraphicsBridge
{
    public enum ScenePostStage { NeuralRendering = 100, FrameGenerationCapture = 200 }
    public static class ScenePostProcess
    {
        private sealed class Consumer
        {
            internal Action<CustomPassContext> Execute;
            internal bool Active;
        }
        private static readonly SortedDictionary<ScenePostStage, Consumer> consumers = new SortedDictionary<ScenePostStage, Consumer>();
        private static GameObject host;
        public static void Register(ScenePostStage stage, Action<CustomPassContext> execute)
        {
            if (execute == null) throw new ArgumentNullException(nameof(execute));
            if (consumers.ContainsKey(stage)) throw new InvalidOperationException("Scene stage already registered: " + stage);
            EnsurePass();
            consumers.Add(stage, new Consumer { Execute = execute });
        }
        public static void SetActive(ScenePostStage stage, bool active)
        {
            if (consumers.TryGetValue(stage, out var consumer)) consumer.Active = active;
            bool any = false;
            foreach (var item in consumers.Values) any |= item.Active;
            if (host != null && host.activeSelf != any) host.SetActive(any);
        }
        public static void Unregister(ScenePostStage stage)
        {
            SetActive(stage, false);
            consumers.Remove(stage);
        }
        private static void EnsurePass()
        {
            if (host != null) return;
#if BEPINEX_IL2CPP
            Il2CppTypes.Register<ScenePostProcessPass>();
#endif
            host = new GameObject("TG scene processing before UI") { hideFlags = HideFlags.HideAndDontSave };
            host.SetActive(false);
            UnityEngine.Object.DontDestroyOnLoad(host);
            var volume = host.AddComponent<CustomPassVolume>();
            volume.isGlobal = true;
            // HDRP 17 CustomPass.ReadRenderTargets declares camera colour/depth/motion read-write
            // regardless of target binding. None avoids an unnecessary bind; it does not omit the
            // render-graph dependency. Both native events stay inside this graph pass's command list.
            volume.injectionPoint = CustomPassInjectionPoint.AfterPostProcess;
            volume.customPasses.Add(new ScenePostProcessPass {
                name = "TG scene processing then frame generation capture",
                targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None });
        }
        internal static void Execute(CustomPassContext context)
        {
            foreach (var item in consumers)
            {
                if (!item.Value.Active) continue;
                try { item.Value.Execute(context); }
                catch (Exception ex)
                {
                    item.Value.Active = false;
                    BridgeLog.Log?.LogError("Scene stage " + item.Key + " disabled: " + ex);
                }
            }
        }
    }
    internal sealed class ScenePostProcessPass : CustomPass
    {
#if BEPINEX_IL2CPP
        public ScenePostProcessPass(IntPtr pointer) : base(pointer) { }
        internal ScenePostProcessPass() : base(ClassInjector.DerivedConstructorPointer<ScenePostProcessPass>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");
        }
        private new void Execute(CustomPassContext ctx) => ScenePostProcess.Execute(ctx);
#else
        protected override void Execute(CustomPassContext ctx) => ScenePostProcess.Execute(ctx);
#endif
    }
}
