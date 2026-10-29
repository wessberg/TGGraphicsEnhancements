// SPDX-License-Identifier: MIT
// Both placements see sharp, scene-linear colour. Lens effects and the game's/RenoDX's tone
// mapping stay downstream. The same host covers vanilla DLSS and BU's STP replacement for RR.
using System;
using HarmonyLib;
using TGGraphicsBridge;
using TGGraphicsBridge.Internal;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.HighDefinition;
using UnityEngine.Rendering.RenderGraphModule;
#if BEPINEX_IL2CPP
using Il2CppInterop.Runtime.Injection;
#endif
namespace TGNeuralRendering
{
    internal static class UpscalePlacement
    {
        private static Harmony harmony;
        private static GameObject host;
        private static CustomPassVolume volume;
        private static NeuralScenePass pass;
        private static int recordedFrame = -1, executedFrame = -1;
        private static int missingExecutions;
        private sealed class ScheduleState
        {
            internal DynamicResolutionHandler Handler;
            internal DynamicResolutionHandler.UpsamplerScheduleType Previous;
        }
        internal static void Start()
        {
            if (!CustomPassRecorder.Ready) throw new MissingMemberException("HDRP custom pass recording");
            var upscale = AccessTools.Method(typeof(HDRenderPipeline), "DoUpscalingAndAntiAliasing");
            var postProcess = AccessTools.Method(typeof(HDRenderPipeline), "RenderPostProcess");
            if (upscale == null || postProcess == null) throw new MissingMemberException("HDRP upscaling placement");
#if BEPINEX_IL2CPP
            Il2CppTypes.Register<NeuralScenePass>();
#endif
            host = new GameObject("TGNR upscaler placement") { hideFlags = HideFlags.HideAndDontSave };
            host.SetActive(false); UnityEngine.Object.DontDestroyOnLoad(host);
            // Recorded explicitly at the upscaler; never also run as a global custom pass.
            volume = host.AddComponent<CustomPassVolume>(); volume.isGlobal = false;
            volume.injectionPoint = CustomPassInjectionPoint.BeforePostProcess;
            pass = new NeuralScenePass { name = "TGNR sharp scene", targetColorBuffer = CustomPass.TargetBuffer.None, targetDepthBuffer = CustomPass.TargetBuffer.None };
            harmony = new Harmony(Plugin.Id + ".placement");
            harmony.Patch(postProcess, prefix: new HarmonyMethod(typeof(UpscalePlacement), nameof(BeginPost)),
                finalizer: new HarmonyMethod(typeof(UpscalePlacement), nameof(EndPost)));
            harmony.Patch(upscale, prefix: new HarmonyMethod(typeof(UpscalePlacement), nameof(Before)),
                postfix: new HarmonyMethod(typeof(UpscalePlacement), nameof(After)));
        }
        private static void BeginPost(HDCamera hdCamera, out ScheduleState __state)
        {
            __state = null;
            if (!Session.Enabled || !SceneCapture.Gameplay(hdCamera)) return;
            // Some HDRP configurations schedule DLSS after DoF/post. While NR is on, both choices
            // need the sharp scene. Change only this camera's graph recording; restore even on error.
            var handler = DynamicResolutionHandler.instance;
            __state = new ScheduleState { Handler = handler, Previous = handler.upsamplerSchedule };
            handler.upsamplerSchedule = DynamicResolutionHandler.UpsamplerScheduleType.BeforePost;
        }
        private static Exception EndPost(Exception __exception, ScheduleState __state)
        {
            if (__state != null) __state.Handler.upsamplerSchedule = __state.Previous;
            return __exception;
        }
        private static void Before(HDRenderPipeline __instance, RenderGraph renderGraph, HDCamera hdCamera,
            TextureHandle source, TextureHandle depthBuffer, TextureHandle motionVectors)
        {
            if (Session.BeforeUpscaling) Record(__instance, renderGraph, hdCamera, source, depthBuffer, motionVectors);
        }
        private static void After(HDRenderPipeline __instance, RenderGraph renderGraph, HDCamera hdCamera,
            TextureHandle depthBuffer, TextureHandle motionVectors, TextureHandle __result)
        {
            if (!Session.BeforeUpscaling) Record(__instance, renderGraph, hdCamera, __result, depthBuffer, motionVectors);
        }
        private static void Record(HDRenderPipeline pipeline, RenderGraph graph, HDCamera camera,
            TextureHandle color, TextureHandle depth, TextureHandle motion)
        {
            if (!Session.Enabled || !SceneCapture.Gameplay(camera)) return;
            try
            {
                if (recordedFrame == Time.frameCount) throw new InvalidOperationException("Neural Rendering was scheduled twice for the gameplay camera.");
                if (recordedFrame >= 0 && executedFrame != recordedFrame)
                {
                    if (++missingExecutions >= 3) throw new InvalidOperationException("HDRP did not execute the recorded Neural Rendering pass.");
                }
                else missingExecutions = 0;
                // ExecuteInternal declares each valid target read/write in the render graph. The
                // in-place native event therefore finishes before SR or the next post effect reads it.
                CustomPassRecorder.Record(pass, volume, pipeline, graph, camera, new CustomPass.RenderTargets {
                    colorBufferRG = color, depthBufferRG = depth, motionVectorBufferRG = motion });
                recordedFrame = Time.frameCount;
            }
            catch (Exception ex) { SceneCapture.Fail(ex); }
        }
        internal static void Execute(CustomPassContext context)
        {
            executedFrame = Time.frameCount;
            SceneCapture.Render(context);
        }
        internal static void Stop()
        {
            harmony?.UnpatchSelf(); harmony = null;
            if (host != null) UnityEngine.Object.Destroy(host);
            host = null; volume = null; pass = null;
            recordedFrame = executedFrame = -1; missingExecutions = 0;
        }
    }
    internal sealed class NeuralScenePass : CustomPass
    {
#if BEPINEX_IL2CPP
        public NeuralScenePass(IntPtr pointer) : base(pointer) { }
        internal NeuralScenePass() : base(ClassInjector.DerivedConstructorPointer<NeuralScenePass>())
        {
            ClassInjector.DerivedConstructorBody(this);
            Il2CppTypes.Construct(this, typeof(CustomPass), "NativeMethodInfoPtr__ctor_Protected_Void_0");
        }
        private new void Execute(CustomPassContext ctx) => UpscalePlacement.Execute(ctx);
#else
        protected override void Execute(CustomPassContext ctx) => UpscalePlacement.Execute(ctx);
#endif
    }
}
